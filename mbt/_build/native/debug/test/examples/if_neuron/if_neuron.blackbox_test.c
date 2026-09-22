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

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF;

struct _M0DTPC15error5Error127RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0BTPB6Logger;

struct _M0TP26RiantR8snn__mbt7Monitor;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0TP26RiantR8snn__mbt5Model;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1113;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TP26RiantR8snn__mbt4Time;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TWRPC15error5ErrorEu;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TPB8MutLocalGiE;

struct _M0TP26RiantR8snn__mbt14SpikingSynapse;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE;

struct _M0TPB4Show;

struct _M0TPB8MutLocalGfE;

struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1118;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGbE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0TPB8MutLocalGdE;

struct _M0BTPB4Show;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt2IFE;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TP26RiantR8snn__mbt12PoissonFixed;

struct _M0TPB5ArrayGsE;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0TWEu;

struct _M0TP26RiantR8snn__mbt11IFParameter;

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

struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF {
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* $0;
  struct _M0TPB5ArrayGiE* $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* $3;
  
};

struct _M0DTPC15error5Error127RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
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

struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1113 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
};

struct _M0TP26RiantR8snn__mbt9PostSpike {
  float $0;
  
};

struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1118 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

struct _M0TPB8MutLocalGdE {
  double $0;
  
};

struct _M0BTPB4Show {
  int32_t(* $method_0)(void*, struct _M0TPB6Logger);
  moonbit_string_t(* $method_1)(void*);
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

struct _M0TP26RiantR8snn__mbt12PoissonFixed {
  float $0;
  float $1;
  struct _M0TPB5ArrayGbE* $2;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1125(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1118(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1113(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1090(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1083(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples26if__neuron__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
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

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
);

int32_t _M0FP26RiantR8snn__mbt12record__zero(
  struct _M0TP26RiantR8snn__mbt5Model*
);

int32_t _M0FP26RiantR8snn__mbt11record__one(
  struct _M0TP26RiantR8snn__mbt7Monitor*,
  float
);

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor6new__v(
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

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t
);

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t);

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t);

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

int32_t _M0MP26RiantR8snn__mbt12PoissonFixed7set__mu(
  struct _M0TP26RiantR8snn__mbt12PoissonFixed*,
  float
);

struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0MP26RiantR8snn__mbt12PoissonFixed3new(
  float
);

int32_t _M0FP26RiantR8snn__mbt15sample__poisson(
  struct _M0TP26RiantR8snn__mbt7Xoshiro*,
  float
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

int32_t _M0MP26RiantR8snn__mbt7Monitor19ascii__plot_2einner(
  struct _M0TP26RiantR8snn__mbt7Monitor*,
  int32_t,
  int32_t
);

moonbit_string_t _M0FP26RiantR8snn__mbt19row__int__set__char(
  moonbit_string_t,
  int32_t,
  int32_t
);

moonbit_string_t _M0FP26RiantR8snn__mbt19format__axis__label(float);

float _M0MP26RiantR8snn__mbt7Monitor8duration(
  struct _M0TP26RiantR8snn__mbt7Monitor*
);

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

float _M0MPC15float5Float5round(float);

float _M0MPC15float5Float5floor(float);

float _M0MPC15float5Float5trunc(float);

int32_t _M0MPC15float5Float7to__int(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

struct _M0TPB5ArrayGsE* _M0MPC15array5Array4makeGsE(
  int32_t,
  moonbit_string_t
);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGsE(
  struct _M0TPB5ArrayGsE*,
  int32_t,
  moonbit_string_t
);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MPC15array5Array2atGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*,
  int32_t
);

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

struct _M0TPB5ArrayGsE* _M0MPC15array5Array20unsafe__make__uninitGsE(int32_t);

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(uint64_t*, int32_t);

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(uint32_t*, int32_t);

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(uint64_t);

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t);

moonbit_string_t _M0IPC14bool4BoolPB4Show10to__string(int32_t);

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

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

struct _M0TP26RiantR8snn__mbt7Monitor** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*
);

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

moonbit_string_t _M0MPC16string6String4make(int32_t, int32_t);

#define _M0FPB20unsafe__make__string moonbit_unsafe_make_string

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

moonbit_string_t _M0FPC15abort5abortGsE(moonbit_string_t);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_33 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_31 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_39 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_21 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[12]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 11, 44, 34, 
    109, 101, 115, 115, 97, 103, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[53]; 
} const moonbit_string_literal_47 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_30 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_17 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 32, 8594, 32, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_20 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[25]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 24, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 114, 101, 115, 117, 108, 116, 34, 
    44, 34, 102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_18 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_40 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[6]; 
} const moonbit_string_literal_25 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 5, 102, 97, 
    108, 115, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[10]; 
} const moonbit_string_literal_13 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 9, 93, 32, 
    40, 101, 109, 112, 116, 121, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_37 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_34 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_11 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[114]; 
} const moonbit_string_literal_45 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 113, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 105, 102, 95, 110, 101, 117, 114, 
    111, 110, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 
    116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 
    114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 
    115, 69, 114, 114, 111, 114, 46, 77, 111, 111, 110, 66, 105, 116, 
    84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 
    114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_24 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 116, 114, 
    117, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_43 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_15 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 32, 124, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[15]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 14, 105, 110, 
    118, 97, 108, 105, 100, 32, 108, 101, 110, 103, 116, 104, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_12 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 77, 111, 
    110, 105, 116, 111, 114, 91, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_32 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_14 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 32, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_10 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 102, 105, 
    114, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_44 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 73, 110, 115, 112, 101, 
    99, 116, 69, 114, 114, 111, 114, 46, 73, 110, 115, 112, 101, 99, 
    116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_41 =
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
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_16 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 116, 61, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_9 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 118, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_8 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 23, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 115, 116, 97, 114, 116, 34, 44, 34, 
    102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_26 =
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
} const moonbit_string_literal_19 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_42 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_36 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_23 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[116]; 
} const moonbit_string_literal_46 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 115, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 105, 102, 95, 110, 101, 117, 114, 
    111, 110, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 
    116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 
    114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 
    107, 105, 112, 84, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 
    116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 
    101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1125$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1125
  };

uint32_t const moonbit_layout_table_data[82] =
  {
    sizeof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1113)
    / 4, 1,
    offsetof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1113, $1)
    / 4
    * 2,
    sizeof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1118)
    / 4, 1,
    offsetof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1118, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
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
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt7Monitor) / 4, 4,
    offsetof(struct _M0TP26RiantR8snn__mbt7Monitor, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt7Monitor, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt7Monitor, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt7Monitor, $3) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGiE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGiE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF) / 4, 4,
    offsetof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF, $3) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGbE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGbE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt12PoissonFixed) / 4, 1,
    offsetof(struct _M0TP26RiantR8snn__mbt12PoissonFixed, $2) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt4Time) / 4, 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4Time, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4Time, $1) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2406
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1146,
  moonbit_string_t _M0L8filenameS1115,
  int32_t _M0L5indexS1117
) {
  struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1113* _closure_2437;
  struct _M0TWEu* _M0L13handle__startS1113;
  struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1118* _closure_2438;
  struct _M0TWssbEu* _M0L14handle__resultS1118;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1125;
  void* _M0L11_2atry__errS1140;
  struct moonbit_result_0 _tmp_2440;
  int32_t _handle__error__result_2441;
  int32_t _M0L6_2atmpS2394;
  void* _M0L3errS1141;
  moonbit_string_t _M0L4nameS1143;
  struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1144;
  moonbit_string_t _M0L7_2anameS1145;
  int32_t _M0L6_2acntS2431;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1115);
  _closure_2437
  = (struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1113*)moonbit_malloc(sizeof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1113));
  Moonbit_object_header(_closure_2437)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2437->code
  = &_M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1113;
  _closure_2437->$0 = _M0L5indexS1117;
  _closure_2437->$1 = _M0L8filenameS1115;
  _M0L13handle__startS1113 = (struct _M0TWEu*)_closure_2437;
  moonbit_incref_cycle_free(_M0L8filenameS1115);
  _closure_2438
  = (struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1118*)moonbit_malloc(sizeof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1118));
  Moonbit_object_header(_closure_2438)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2438->code
  = &_M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1118;
  _closure_2438->$0 = _M0L5indexS1117;
  _closure_2438->$1 = _M0L8filenameS1115;
  _M0L14handle__resultS1118 = (struct _M0TWssbEu*)_closure_2438;
  _M0L17error__to__stringS1125
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1125$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2440
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1146, _M0L8filenameS1115, _M0L5indexS1117, _M0L13handle__startS1113, _M0L14handle__resultS1118, _M0L17error__to__stringS1125);
  if (_tmp_2440.tag) {
    int32_t const _M0L5_2aokS2403 = _tmp_2440.data.ok;
    _handle__error__result_2441 = _M0L5_2aokS2403;
  } else {
    void* const _M0L6_2aerrS2404 = _tmp_2440.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1125);
    moonbit_decref_cycle_free(_M0L13handle__startS1113);
    _M0L11_2atry__errS1140 = _M0L6_2aerrS2404;
    goto join_1139;
  }
  if (_handle__error__result_2441) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1125);
    moonbit_decref_cycle_free(_M0L13handle__startS1113);
    _M0L6_2atmpS2394 = 1;
  } else {
    struct moonbit_result_0 _tmp_2442;
    int32_t _handle__error__result_2443;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2442
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1146, _M0L8filenameS1115, _M0L5indexS1117, _M0L13handle__startS1113, _M0L14handle__resultS1118, _M0L17error__to__stringS1125);
    if (_tmp_2442.tag) {
      int32_t const _M0L5_2aokS2401 = _tmp_2442.data.ok;
      _handle__error__result_2443 = _M0L5_2aokS2401;
    } else {
      void* const _M0L6_2aerrS2402 = _tmp_2442.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1125);
      moonbit_decref_cycle_free(_M0L13handle__startS1113);
      _M0L11_2atry__errS1140 = _M0L6_2aerrS2402;
      goto join_1139;
    }
    if (_handle__error__result_2443) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1125);
      moonbit_decref_cycle_free(_M0L13handle__startS1113);
      _M0L6_2atmpS2394 = 1;
    } else {
      struct moonbit_result_0 _tmp_2444;
      int32_t _handle__error__result_2445;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2444
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1146, _M0L8filenameS1115, _M0L5indexS1117, _M0L13handle__startS1113, _M0L14handle__resultS1118, _M0L17error__to__stringS1125);
      if (_tmp_2444.tag) {
        int32_t const _M0L5_2aokS2399 = _tmp_2444.data.ok;
        _handle__error__result_2445 = _M0L5_2aokS2399;
      } else {
        void* const _M0L6_2aerrS2400 = _tmp_2444.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1125);
        moonbit_decref_cycle_free(_M0L13handle__startS1113);
        _M0L11_2atry__errS1140 = _M0L6_2aerrS2400;
        goto join_1139;
      }
      if (_handle__error__result_2445) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1125);
        moonbit_decref_cycle_free(_M0L13handle__startS1113);
        _M0L6_2atmpS2394 = 1;
      } else {
        struct moonbit_result_0 _tmp_2446;
        int32_t _handle__error__result_2447;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2446
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1146, _M0L8filenameS1115, _M0L5indexS1117, _M0L13handle__startS1113, _M0L14handle__resultS1118, _M0L17error__to__stringS1125);
        if (_tmp_2446.tag) {
          int32_t const _M0L5_2aokS2397 = _tmp_2446.data.ok;
          _handle__error__result_2447 = _M0L5_2aokS2397;
        } else {
          void* const _M0L6_2aerrS2398 = _tmp_2446.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1125);
          moonbit_decref_cycle_free(_M0L13handle__startS1113);
          _M0L11_2atry__errS1140 = _M0L6_2aerrS2398;
          goto join_1139;
        }
        if (_handle__error__result_2447) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1125);
          moonbit_decref_cycle_free(_M0L13handle__startS1113);
          _M0L6_2atmpS2394 = 1;
        } else {
          struct moonbit_result_0 _tmp_2448;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2448
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1146, _M0L8filenameS1115, _M0L5indexS1117, _M0L13handle__startS1113, _M0L14handle__resultS1118, _M0L17error__to__stringS1125);
          moonbit_decref_cycle_free(_M0L13handle__startS1113);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1125);
          if (_tmp_2448.tag) {
            int32_t const _M0L5_2aokS2395 = _tmp_2448.data.ok;
            _M0L6_2atmpS2394 = _M0L5_2aokS2395;
          } else {
            void* const _M0L6_2aerrS2396 = _tmp_2448.data.err;
            _M0L11_2atry__errS1140 = _M0L6_2aerrS2396;
            goto join_1139;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2394) {
    void* _M0L129RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2405 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L129RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2405)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L129RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2405)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1140
    = _M0L129RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2405;
    goto join_1139;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1118);
  }
  goto joinlet_2439;
  join_1139:;
  _M0L3errS1141 = _M0L11_2atry__errS1140;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1144
  = (struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1141;
  _M0L7_2anameS1145 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1144->$0;
  _M0L6_2acntS2431
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1144));
  if (_M0L6_2acntS2431 > 1) {
    int32_t _M0L11_2anew__cntS2432 = _M0L6_2acntS2431 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1144), _M0L11_2anew__cntS2432);
    moonbit_incref_cycle_free(_M0L7_2anameS1145);
  } else if (_M0L6_2acntS2431 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1144);
  }
  _M0L4nameS1143 = _M0L7_2anameS1145;
  goto join_1142;
  goto joinlet_2449;
  join_1142:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1118(_M0L14handle__resultS1118, _M0L4nameS1143, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1118);
  moonbit_decref_cycle_free(_M0L4nameS1143);
  joinlet_2449:;
  joinlet_2439:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1125(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2393,
  void* _M0L3errS1126
) {
  void* _M0L1eS1128;
  moonbit_string_t _M0L1eS1130;
  moonbit_string_t _result_2452;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1126)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1131 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1126;
      moonbit_string_t _M0L4_2aeS1132 = _M0L10_2aFailureS1131->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1132);
      _M0L1eS1130 = _M0L4_2aeS1132;
      goto join_1129;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1133 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1126;
      moonbit_string_t _M0L4_2aeS1134 = _M0L15_2aInspectErrorS1133->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1134);
      _M0L1eS1130 = _M0L4_2aeS1134;
      goto join_1129;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1135 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1126;
      moonbit_string_t _M0L4_2aeS1136 = _M0L16_2aSnapshotErrorS1135->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1136);
      _M0L1eS1130 = _M0L4_2aeS1136;
      goto join_1129;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error127RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1137 =
        (struct _M0DTPC15error5Error127RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1126;
      moonbit_string_t _M0L4_2aeS1138 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1137->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1138);
      _M0L1eS1130 = _M0L4_2aeS1138;
      goto join_1129;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1126);
      _M0L1eS1128 = _M0L3errS1126;
      goto join_1127;
      break;
    }
  }
  join_1129:;
  return _M0L1eS1130;
  join_1127:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _result_2452 = _M0FP15Error10to__string(_M0L1eS1128);
  moonbit_decref_cycle_free(_M0L1eS1128);
  return _result_2452;
}

int32_t _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1118(
  struct _M0TWssbEu* _M0L6_2aenvS2390,
  moonbit_string_t _M0L10__testnameS1119,
  moonbit_string_t _M0L7messageS1120,
  int32_t _M0L7skippedS1121
) {
  struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1118* _M0L14_2acasted__envS2391;
  moonbit_string_t _M0L8filenameS1115;
  int32_t _M0L5indexS1117;
  moonbit_string_t _M0L10file__nameS1122;
  moonbit_string_t _M0L7messageS1123;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1124;
  moonbit_string_t _M0L6_2atmpS2392;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2391
  = (struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1118*)_M0L6_2aenvS2390;
  _M0L8filenameS1115 = _M0L14_2acasted__envS2391->$1;
  _M0L5indexS1117 = _M0L14_2acasted__envS2391->$0;
  if (!_M0L7skippedS1121 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1122
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1115, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1123
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1120, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1124
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1124, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1124, _M0L10file__nameS1122);
  moonbit_decref_cycle_free(_M0L10file__nameS1122);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1124, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1124, _M0L5indexS1117);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1124, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1124, _M0L7messageS1123);
  moonbit_decref_cycle_free(_M0L7messageS1123);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1124, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2392
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1124);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1124);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2392);
  moonbit_decref_cycle_free(_M0L6_2atmpS2392);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1113(
  struct _M0TWEu* _M0L6_2aenvS2387
) {
  struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1113* _M0L14_2acasted__envS2388;
  moonbit_string_t _M0L8filenameS1115;
  int32_t _M0L5indexS1117;
  moonbit_string_t _M0L10file__nameS1114;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1116;
  moonbit_string_t _M0L6_2atmpS2389;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2388
  = (struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fif__neuron__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1113*)_M0L6_2aenvS2387;
  _M0L8filenameS1115 = _M0L14_2acasted__envS2388->$1;
  _M0L5indexS1117 = _M0L14_2acasted__envS2388->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1114
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1115, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1116
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1116, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1116, _M0L10file__nameS1114);
  moonbit_decref_cycle_free(_M0L10file__nameS1114);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1116, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1116, _M0L5indexS1117);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1116, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2389
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1116);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1116);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2389);
  moonbit_decref_cycle_free(_M0L6_2atmpS2389);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1083;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1090;
  struct _M0TUsiE** _M0L6_2atmpS2386;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1097;
  moonbit_string_t* _M0L9cli__argsS1098;
  moonbit_string_t _M0L6_2atmpS2385;
  moonbit_string_t _M0L6_2atmpS2384;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1099;
  int32_t _M0L7_2abindS1100;
  moonbit_string_t* _M0L7_2abindS1101;
  int32_t _M0L6_2acntS2433;
  int32_t _M0L2__S1102;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1083 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1090 = 0;
  _M0L6_2atmpS2386 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1097
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1097)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1097->$0 = _M0L6_2atmpS2386;
  _M0L16file__and__indexS1097->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1098
  = _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1098)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2385 = (moonbit_string_t)_M0L9cli__argsS1098[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2385);
  moonbit_decref_cycle_free(_M0L9cli__argsS1098);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2384
  = _M0MP46RiantR8snn__mbt8examples26if__neuron__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2385);
  moonbit_decref_cycle_free(_M0L6_2atmpS2385);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1099
  = _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1090(_M0L51moonbit__test__driver__internal__split__mbt__stringS1090, _M0L6_2atmpS2384, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2384);
  _M0L7_2abindS1100 = _M0L10test__argsS1099->$1;
  _M0L7_2abindS1101 = _M0L10test__argsS1099->$0;
  _M0L6_2acntS2433
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1099));
  if (_M0L6_2acntS2433 > 1) {
    int32_t _M0L11_2anew__cntS2434 = _M0L6_2acntS2433 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1099), _M0L11_2anew__cntS2434);
    moonbit_incref_cycle_free(_M0L7_2abindS1101);
  } else if (_M0L6_2acntS2433 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1099);
  }
  _M0L2__S1102 = 0;
  while (1) {
    if (_M0L2__S1102 < _M0L7_2abindS1100) {
      moonbit_string_t _M0L3argS1103 =
        (moonbit_string_t)_M0L7_2abindS1101[_M0L2__S1102];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1104;
      moonbit_string_t _M0L4fileS1105;
      moonbit_string_t _M0L5rangeS1106;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1107;
      moonbit_string_t _M0L6_2atmpS2382;
      int32_t _M0L5startS1108;
      moonbit_string_t _M0L6_2atmpS2381;
      int32_t _M0L3endS1109;
      int32_t _M0L1iS1110;
      int32_t _M0L6_2atmpS2383;
      moonbit_incref_cycle_free(_M0L3argS1103);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1104
      = _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1090(_M0L51moonbit__test__driver__internal__split__mbt__stringS1090, _M0L3argS1103, 58);
      moonbit_decref_cycle_free(_M0L3argS1103);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1105
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1104, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1106
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1104, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1104);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1107
      = _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1090(_M0L51moonbit__test__driver__internal__split__mbt__stringS1090, _M0L5rangeS1106, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1106);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2382
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1107, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1108
      = _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1083(_M0L45moonbit__test__driver__internal__parse__int__S1083, _M0L6_2atmpS2382);
      moonbit_decref_cycle_free(_M0L6_2atmpS2382);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2381
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1107, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1107);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1109
      = _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1083(_M0L45moonbit__test__driver__internal__parse__int__S1083, _M0L6_2atmpS2381);
      moonbit_decref_cycle_free(_M0L6_2atmpS2381);
      _M0L1iS1110 = _M0L5startS1108;
      while (1) {
        if (_M0L1iS1110 < _M0L3endS1109) {
          struct _M0TUsiE* _M0L8_2atupleS2379;
          int32_t _M0L6_2atmpS2380;
          moonbit_incref_cycle_free(_M0L4fileS1105);
          _M0L8_2atupleS2379
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS2379)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS2379->$0 = _M0L4fileS1105;
          _M0L8_2atupleS2379->$1 = _M0L1iS1110;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1097, _M0L8_2atupleS2379);
          _M0L6_2atmpS2380 = _M0L1iS1110 + 1;
          _M0L1iS1110 = _M0L6_2atmpS2380;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1105);
        }
        break;
      }
      _M0L6_2atmpS2383 = _M0L2__S1102 + 1;
      _M0L2__S1102 = _M0L6_2atmpS2383;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1101);
    }
    break;
  }
  return _M0L16file__and__indexS1097;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1090(
  int32_t _M0L6_2aenvS2360,
  moonbit_string_t _M0L1sS1091,
  int32_t _M0L3sepS1092
) {
  moonbit_string_t* _M0L6_2atmpS2378;
  struct _M0TPB5ArrayGsE* _M0L3resS1093;
  struct _M0TPB8MutLocalGiE* _M0L1iS1094;
  struct _M0TPB8MutLocalGiE* _M0L5startS1095;
  int32_t _M0L3valS2373;
  int32_t _M0L6_2atmpS2374;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2378 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1093
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1093)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1093->$0 = _M0L6_2atmpS2378;
  _M0L3resS1093->$1 = 0;
  _M0L1iS1094
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1094)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1094->$0 = 0;
  _M0L5startS1095
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1095)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1095->$0 = 0;
  while (1) {
    int32_t _M0L3valS2361 = _M0L1iS1094->$0;
    int32_t _M0L6_2atmpS2362 = Moonbit_array_length(_M0L1sS1091);
    if (_M0L3valS2361 < _M0L6_2atmpS2362) {
      int32_t _M0L3valS2365 = _M0L1iS1094->$0;
      int32_t _M0L6_2atmpS2364;
      int32_t _M0L6_2atmpS2363;
      int32_t _M0L3valS2372;
      int32_t _M0L6_2atmpS2371;
      if (
        _M0L3valS2365 < 0
        || _M0L3valS2365 >= Moonbit_array_length(_M0L1sS1091)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2364 = _M0L1sS1091[_M0L3valS2365];
      _M0L6_2atmpS2363 = _M0L6_2atmpS2364;
      if (_M0L6_2atmpS2363 == _M0L3sepS1092) {
        int32_t _M0L3valS2367 = _M0L5startS1095->$0;
        int32_t _M0L3valS2368 = _M0L1iS1094->$0;
        moonbit_string_t _M0L6_2atmpS2366;
        int32_t _M0L3valS2370;
        int32_t _M0L6_2atmpS2369;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS2366
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1091, _M0L3valS2367, _M0L3valS2368);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1093, _M0L6_2atmpS2366);
        _M0L3valS2370 = _M0L1iS1094->$0;
        _M0L6_2atmpS2369 = _M0L3valS2370 + 1;
        _M0L5startS1095->$0 = _M0L6_2atmpS2369;
      }
      _M0L3valS2372 = _M0L1iS1094->$0;
      _M0L6_2atmpS2371 = _M0L3valS2372 + 1;
      _M0L1iS1094->$0 = _M0L6_2atmpS2371;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1094);
    }
    break;
  }
  _M0L3valS2373 = _M0L5startS1095->$0;
  _M0L6_2atmpS2374 = Moonbit_array_length(_M0L1sS1091);
  if (_M0L3valS2373 < _M0L6_2atmpS2374) {
    int32_t _M0L3valS2376 = _M0L5startS1095->$0;
    int32_t _M0L6_2atmpS2377;
    moonbit_string_t _M0L6_2atmpS2375;
    moonbit_decref_cycle_free(_M0L5startS1095);
    _M0L6_2atmpS2377 = Moonbit_array_length(_M0L1sS1091);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS2375
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1091, _M0L3valS2376, _M0L6_2atmpS2377);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1093, _M0L6_2atmpS2375);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1095);
  }
  return _M0L3resS1093;
}

int32_t _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1083(
  int32_t _M0L6_2aenvS2353,
  moonbit_string_t _M0L1sS1084
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1085;
  int32_t _M0L3lenS1086;
  int32_t _M0L7_2abindS1087;
  int32_t _M0L1iS1088;
  int32_t _result_2457;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1085
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1085)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1085->$0 = 0;
  _M0L3lenS1086 = Moonbit_array_length(_M0L1sS1084);
  _M0L7_2abindS1087 = 0;
  _M0L1iS1088 = _M0L7_2abindS1087;
  while (1) {
    if (_M0L1iS1088 < _M0L3lenS1086) {
      int32_t _M0L3valS2358 = _M0L3resS1085->$0;
      int32_t _M0L6_2atmpS2355 = _M0L3valS2358 * 10;
      int32_t _M0L6_2atmpS2357;
      int32_t _M0L6_2atmpS2356;
      int32_t _M0L6_2atmpS2354;
      int32_t _M0L6_2atmpS2359;
      if (
        _M0L1iS1088 < 0 || _M0L1iS1088 >= Moonbit_array_length(_M0L1sS1084)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2357 = _M0L1sS1084[_M0L1iS1088];
      _M0L6_2atmpS2356 = _M0L6_2atmpS2357 - 48;
      _M0L6_2atmpS2354 = _M0L6_2atmpS2355 + _M0L6_2atmpS2356;
      _M0L3resS1085->$0 = _M0L6_2atmpS2354;
      _M0L6_2atmpS2359 = _M0L1iS1088 + 1;
      _M0L1iS1088 = _M0L6_2atmpS2359;
      continue;
    }
    break;
  }
  _result_2457 = _M0L3resS1085->$0;
  moonbit_decref_cycle_free(_M0L3resS1085);
  return _result_2457;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples26if__neuron__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1082
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1082);
  return _M0L4selfS1082;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1052,
  moonbit_string_t _M0L12_2adiscard__S1053,
  int32_t _M0L12_2adiscard__S1054,
  struct _M0TWEu* _M0L12_2adiscard__S1055,
  struct _M0TWssbEu* _M0L12_2adiscard__S1056,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1057
) {
  struct moonbit_result_0 _result_2458;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _result_2458.tag = 1;
  _result_2458.data.ok = 0;
  return _result_2458;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1058,
  moonbit_string_t _M0L12_2adiscard__S1059,
  int32_t _M0L12_2adiscard__S1060,
  struct _M0TWEu* _M0L12_2adiscard__S1061,
  struct _M0TWssbEu* _M0L12_2adiscard__S1062,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1063
) {
  struct moonbit_result_0 _result_2459;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _result_2459.tag = 1;
  _result_2459.data.ok = 0;
  return _result_2459;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1064,
  moonbit_string_t _M0L12_2adiscard__S1065,
  int32_t _M0L12_2adiscard__S1066,
  struct _M0TWEu* _M0L12_2adiscard__S1067,
  struct _M0TWssbEu* _M0L12_2adiscard__S1068,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1069
) {
  struct moonbit_result_0 _result_2460;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _result_2460.tag = 1;
  _result_2460.data.ok = 0;
  return _result_2460;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1070,
  moonbit_string_t _M0L12_2adiscard__S1071,
  int32_t _M0L12_2adiscard__S1072,
  struct _M0TWEu* _M0L12_2adiscard__S1073,
  struct _M0TWssbEu* _M0L12_2adiscard__S1074,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1075
) {
  struct moonbit_result_0 _result_2461;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _result_2461.tag = 1;
  _result_2461.data.ok = 0;
  return _result_2461;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1076,
  moonbit_string_t _M0L12_2adiscard__S1077,
  int32_t _M0L12_2adiscard__S1078,
  struct _M0TWEu* _M0L12_2adiscard__S1079,
  struct _M0TWssbEu* _M0L12_2adiscard__S1080,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1081
) {
  struct moonbit_result_0 _result_2462;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _result_2462.tag = 1;
  _result_2462.data.ok = 0;
  return _result_2462;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1051
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter3new(
  
) {
  float _M0L1cS1033;
  float _M0L2glS1034;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_2463;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS1033 = -0x1p+0f;
  _M0L2glS1034 = -0x1p+0f;
  _block_2463
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_2463)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2463->$0 = _M0L1cS1033;
  _block_2463->$1 = _M0L2glS1034;
  _block_2463->$2 = 0x1.ep+3f;
  _block_2463->$3 = -0x1.9p+5f;
  _block_2463->$4 = -0x1.ep+5f;
  _block_2463->$5 = -0x1.18p+6f;
  _block_2463->$6 = 0x1.eb851eb851eb8p-5f;
  _block_2463->$7 = 0x1p+1f;
  _block_2463->$8 = 0x0p+0f;
  _block_2463->$9 = 0x0p+0f;
  _block_2463->$10 = 0x0p+0f;
  return _block_2463;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS1007,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS1009,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1012
) {
  struct _M0TPB5ArrayGfE* _M0L1vS1006;
  float _M0L2vtS2351;
  float _M0L2vrS2352;
  float _M0L6spreadS1008;
  int32_t _M0L7_2abindS1010;
  int32_t _M0L1kS1011;
  struct _M0TPB5ArrayGfE* _M0L1wS1014;
  struct _M0TPB5ArrayGbE* _M0L4fireS1015;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1016;
  struct _M0TPB5ArrayGfE* _M0L1iS1017;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS1018;
  struct _M0TPB5ArrayGfE* _M0L2geS1019;
  struct _M0TPB5ArrayGfE* _M0L2giS1020;
  struct _M0TPB5ArrayGfE* _M0L2heS1021;
  struct _M0TPB5ArrayGfE* _M0L2hiS1022;
  struct _M0TPB5ArrayGfE* _M0L3gluS1023;
  struct _M0TPB5ArrayGfE* _M0L4gabaS1024;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1025;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1026;
  float _M0L4e__eS1027;
  float _M0L4e__iS1028;
  float _M0L3treS1029;
  float _M0L3tdeS1030;
  float _M0L3triS1031;
  float _M0L3tdiS1032;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS2350;
  struct _M0TP26RiantR8snn__mbt2IF* _block_2465;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS1006 = _M0MPC15array5Array4makeGfE(_M0L1nS1007, 0x0p+0f);
  _M0L2vtS2351 = _M0L5paramS1009->$3;
  _M0L2vrS2352 = _M0L5paramS1009->$4;
  _M0L6spreadS1008 = _M0L2vtS2351 - _M0L2vrS2352;
  _M0L7_2abindS1010 = 0;
  _M0L1kS1011 = _M0L7_2abindS1010;
  while (1) {
    if (_M0L1kS1011 < _M0L1nS1007) {
      float _M0L2vrS2346 = _M0L5paramS1009->$4;
      float _M0L6_2atmpS2348;
      float _M0L6_2atmpS2347;
      float _M0L6_2atmpS2345;
      int32_t _M0L6_2atmpS2349;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2348 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1012);
      _M0L6_2atmpS2347 = _M0L6_2atmpS2348 * _M0L6spreadS1008;
      _M0L6_2atmpS2345 = _M0L2vrS2346 + _M0L6_2atmpS2347;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1006, _M0L1kS1011, _M0L6_2atmpS2345);
      _M0L6_2atmpS2349 = _M0L1kS1011 + 1;
      _M0L1kS1011 = _M0L6_2atmpS2349;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS1014 = _M0MPC15array5Array4makeGfE(_M0L1nS1007, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS1015 = _M0MPC15array5Array4makeGbE(_M0L1nS1007, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS1016 = _M0MPC15array5Array4makeGiE(_M0L1nS1007, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS1017 = _M0MPC15array5Array4makeGfE(_M0L1nS1007, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS1018 = _M0MPC15array5Array4makeGfE(_M0L1nS1007, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS1019 = _M0MPC15array5Array4makeGfE(_M0L1nS1007, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS1020 = _M0MPC15array5Array4makeGfE(_M0L1nS1007, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS1021 = _M0MPC15array5Array4makeGfE(_M0L1nS1007, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS1022 = _M0MPC15array5Array4makeGfE(_M0L1nS1007, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS1023 = _M0MPC15array5Array4makeGfE(_M0L1nS1007, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS1024 = _M0MPC15array5Array4makeGfE(_M0L1nS1007, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS1025 = _M0MPC15array5Array4makeGfE(_M0L1nS1007, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS1026 = _M0MPC15array5Array4makeGfE(_M0L1nS1007, 0x1p+0f);
  _M0L4e__eS1027 = 0x0p+0f;
  _M0L4e__iS1028 = -0x1.2cp+6f;
  _M0L3treS1029 = 0x1p+0f;
  _M0L3tdeS1030 = 0x1.8p+2f;
  _M0L3triS1031 = 0x1p-1f;
  _M0L3tdiS1032 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS2350 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS1009);
  _block_2465
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_2465)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2465->$0 = _M0L5paramS1009;
  _block_2465->$1 = _M0L6_2atmpS2350;
  _block_2465->$2 = _M0L1nS1007;
  _block_2465->$3 = _M0L1vS1006;
  _block_2465->$4 = _M0L1wS1014;
  _block_2465->$5 = _M0L4fireS1015;
  _block_2465->$6 = _M0L4tabsS1016;
  _block_2465->$7 = _M0L1iS1017;
  _block_2465->$8 = _M0L9syn__currS1018;
  _block_2465->$9 = _M0L2geS1019;
  _block_2465->$10 = _M0L2giS1020;
  _block_2465->$11 = _M0L2heS1021;
  _block_2465->$12 = _M0L2hiS1022;
  _block_2465->$13 = _M0L3gluS1023;
  _block_2465->$14 = _M0L4gabaS1024;
  _block_2465->$15 = _M0L7gsyn__eS1025;
  _block_2465->$16 = _M0L7gsyn__iS1026;
  _block_2465->$17 = _M0L4e__eS1027;
  _block_2465->$18 = _M0L4e__iS1028;
  _block_2465->$19 = _M0L3treS1029;
  _block_2465->$20 = _M0L3tdeS1030;
  _block_2465->$21 = _M0L3triS1031;
  _block_2465->$22 = _M0L3tdiS1032;
  return _block_2465;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_2466;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_2466
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_2466)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2466->$0 = 0x1p+1f;
  return _block_2466;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0FP26RiantR8snn__mbt12record__zero(
  struct _M0TP26RiantR8snn__mbt5Model* _M0L5modelS1000
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS999;
  int32_t _M0L7_2abindS1001;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS1002;
  int32_t _M0L2__S1003;
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L7_2abindS999 = _M0L5modelS1000->$2;
  _M0L7_2abindS1001 = _M0L7_2abindS999->$1;
  _M0L7_2abindS1002 = _M0L7_2abindS999->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1002);
  _M0L2__S1003 = 0;
  while (1) {
    if (_M0L2__S1003 < _M0L7_2abindS1001) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS1004 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS1002[
          _M0L2__S1003
        ];
      int32_t _M0L6_2atmpS2344;
      moonbit_incref_cycle_free(_M0L1mS1004);
      #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L1mS1004, 0x0p+0f);
      moonbit_decref_cycle_free(_M0L1mS1004);
      _M0L6_2atmpS2344 = _M0L2__S1003 + 1;
      _M0L2__S1003 = _M0L6_2atmpS2344;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1002);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt11record__one(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS996,
  float _M0L1tS998
) {
  int32_t _M0L11step__countS2330;
  int32_t _M0L6_2atmpS2329;
  int32_t _M0L11step__countS2332;
  int32_t _M0L9rec__stepS2333;
  int32_t _M0L6_2atmpS2331;
  moonbit_string_t _M0L3symS2336;
  float _M0L1vS997;
  struct _M0TPB5ArrayGfE* _M0L4dataS2334;
  struct _M0TPB5ArrayGfE* _M0L5timesS2335;
  #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L11step__countS2330 = _M0L1mS996->$6;
  _M0L6_2atmpS2329 = _M0L11step__countS2330 + 1;
  _M0L1mS996->$6 = _M0L6_2atmpS2329;
  _M0L11step__countS2332 = _M0L1mS996->$6;
  _M0L9rec__stepS2333 = _M0L1mS996->$5;
  _M0L6_2atmpS2331 = _M0L11step__countS2332 % _M0L9rec__stepS2333;
  if (_M0L6_2atmpS2331 != 0) {
    return 0;
  }
  _M0L3symS2336 = _M0L1mS996->$1;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS2336 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS2336)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS2336, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS2336) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS2339 = _M0L1mS996->$0;
    struct _M0TPB5ArrayGfE* _M0L1vS2337 = _M0L3popS2339->$3;
    int32_t _M0L6neuronS2338 = _M0L1mS996->$4;
    #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _M0L1vS997 = _M0MPC15array5Array2atGfE(_M0L1vS2337, _M0L6neuronS2338);
  } else {
    moonbit_string_t _M0L3symS2340 = _M0L1mS996->$1;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    if (
      _M0L3symS2340 == (moonbit_string_t)moonbit_string_literal_10.data
      || Moonbit_array_length(_M0L3symS2340)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_10.data)
         && 0
            == memcmp(_M0L3symS2340, (moonbit_string_t)moonbit_string_literal_10.data, Moonbit_array_length(_M0L3symS2340) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS2343 = _M0L1mS996->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2341 = _M0L3popS2343->$5;
      int32_t _M0L6neuronS2342 = _M0L1mS996->$4;
      #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2341, _M0L6neuronS2342)) {
        _M0L1vS997 = 0x1p+0f;
      } else {
        _M0L1vS997 = 0x0p+0f;
      }
    } else {
      _M0L1vS997 = 0x0p+0f;
    }
  }
  _M0L4dataS2334 = _M0L1mS996->$2;
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L4dataS2334, _M0L1vS997);
  _M0L5timesS2335 = _M0L1mS996->$3;
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L5timesS2335, _M0L1tS998);
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor6new__v(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS994,
  int32_t _M0L6neuronS995
) {
  float* _M0L6_2atmpS2328;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2325;
  float* _M0L6_2atmpS2327;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2326;
  struct _M0TP26RiantR8snn__mbt7Monitor* _block_2468;
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6_2atmpS2328 = moonbit_empty_float_array;
  _M0L6_2atmpS2325
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2325)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _M0L6_2atmpS2325->$0 = _M0L6_2atmpS2328;
  _M0L6_2atmpS2325->$1 = 0;
  _M0L6_2atmpS2327 = moonbit_empty_float_array;
  _M0L6_2atmpS2326
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2326)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _M0L6_2atmpS2326->$0 = _M0L6_2atmpS2327;
  _M0L6_2atmpS2326->$1 = 0;
  moonbit_incref_cycle_free(_M0L3popS994);
  _block_2468
  = (struct _M0TP26RiantR8snn__mbt7Monitor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Monitor));
  Moonbit_object_header(_block_2468)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _block_2468->$0 = _M0L3popS994;
  _block_2468->$1 = (moonbit_string_t)moonbit_string_literal_9.data;
  _block_2468->$2 = _M0L6_2atmpS2325;
  _block_2468->$3 = _M0L6_2atmpS2326;
  _block_2468->$4 = _M0L6neuronS995;
  _block_2468->$5 = 1;
  _block_2468->$6 = 0;
  return _block_2468;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS990
) {
  int32_t _M0L1nS989;
  int32_t _M0L7_2abindS991;
  int32_t _M0L1iS992;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS989 = _M0L1pS990->$2;
  _M0L7_2abindS991 = 0;
  _M0L1iS992 = _M0L7_2abindS991;
  while (1) {
    if (_M0L1iS992 < _M0L1nS989) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2302 = _M0L1pS990->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS2323 = _M0L1pS990->$9;
      float _M0L6_2atmpS2318;
      struct _M0TPB5ArrayGfE* _M0L1vS2322;
      float _M0L6_2atmpS2320;
      float _M0L4e__eS2321;
      float _M0L6_2atmpS2319;
      float _M0L6_2atmpS2315;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS2317;
      float _M0L6_2atmpS2316;
      float _M0L6_2atmpS2304;
      struct _M0TPB5ArrayGfE* _M0L2giS2314;
      float _M0L6_2atmpS2309;
      struct _M0TPB5ArrayGfE* _M0L1vS2313;
      float _M0L6_2atmpS2311;
      float _M0L4e__iS2312;
      float _M0L6_2atmpS2310;
      float _M0L6_2atmpS2306;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS2308;
      float _M0L6_2atmpS2307;
      float _M0L6_2atmpS2305;
      float _M0L6_2atmpS2303;
      int32_t _M0L6_2atmpS2324;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2318 = _M0MPC15array5Array2atGfE(_M0L2geS2323, _M0L1iS992);
      _M0L1vS2322 = _M0L1pS990->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2320 = _M0MPC15array5Array2atGfE(_M0L1vS2322, _M0L1iS992);
      _M0L4e__eS2321 = _M0L1pS990->$17;
      _M0L6_2atmpS2319 = _M0L6_2atmpS2320 - _M0L4e__eS2321;
      _M0L6_2atmpS2315 = _M0L6_2atmpS2318 * _M0L6_2atmpS2319;
      _M0L7gsyn__eS2317 = _M0L1pS990->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2316
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS2317, _M0L1iS992);
      _M0L6_2atmpS2304 = _M0L6_2atmpS2315 * _M0L6_2atmpS2316;
      _M0L2giS2314 = _M0L1pS990->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2309 = _M0MPC15array5Array2atGfE(_M0L2giS2314, _M0L1iS992);
      _M0L1vS2313 = _M0L1pS990->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2311 = _M0MPC15array5Array2atGfE(_M0L1vS2313, _M0L1iS992);
      _M0L4e__iS2312 = _M0L1pS990->$18;
      _M0L6_2atmpS2310 = _M0L6_2atmpS2311 - _M0L4e__iS2312;
      _M0L6_2atmpS2306 = _M0L6_2atmpS2309 * _M0L6_2atmpS2310;
      _M0L7gsyn__iS2308 = _M0L1pS990->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2307
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS2308, _M0L1iS992);
      _M0L6_2atmpS2305 = _M0L6_2atmpS2306 * _M0L6_2atmpS2307;
      _M0L6_2atmpS2303 = _M0L6_2atmpS2304 + _M0L6_2atmpS2305;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS2302, _M0L1iS992, _M0L6_2atmpS2303);
      _M0L6_2atmpS2324 = _M0L1iS992 + 1;
      _M0L1iS992 = _M0L6_2atmpS2324;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS981,
  float _M0L2dtS984
) {
  int32_t _M0L1nS980;
  int32_t _M0L7_2abindS982;
  int32_t _M0L1iS983;
  int32_t _M0L7_2abindS986;
  int32_t _M0L1iS987;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS980 = _M0L1pS981->$2;
  _M0L7_2abindS982 = 0;
  _M0L1iS983 = _M0L7_2abindS982;
  while (1) {
    if (_M0L1iS983 < _M0L1nS980) {
      struct _M0TPB5ArrayGfE* _M0L2heS2240 = _M0L1pS981->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS2245 = _M0L1pS981->$11;
      float _M0L6_2atmpS2242;
      struct _M0TPB5ArrayGfE* _M0L3gluS2244;
      float _M0L6_2atmpS2243;
      float _M0L6_2atmpS2241;
      struct _M0TPB5ArrayGfE* _M0L2hiS2246;
      struct _M0TPB5ArrayGfE* _M0L2hiS2251;
      float _M0L6_2atmpS2248;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2250;
      float _M0L6_2atmpS2249;
      float _M0L6_2atmpS2247;
      struct _M0TPB5ArrayGfE* _M0L2geS2252;
      struct _M0TPB5ArrayGfE* _M0L2geS2264;
      float _M0L6_2atmpS2254;
      struct _M0TPB5ArrayGfE* _M0L2geS2263;
      float _M0L6_2atmpS2262;
      float _M0L6_2atmpS2260;
      float _M0L3tdeS2261;
      float _M0L6_2atmpS2257;
      struct _M0TPB5ArrayGfE* _M0L2heS2259;
      float _M0L6_2atmpS2258;
      float _M0L6_2atmpS2256;
      float _M0L6_2atmpS2255;
      float _M0L6_2atmpS2253;
      struct _M0TPB5ArrayGfE* _M0L2heS2265;
      struct _M0TPB5ArrayGfE* _M0L2heS2274;
      float _M0L6_2atmpS2267;
      struct _M0TPB5ArrayGfE* _M0L2heS2273;
      float _M0L6_2atmpS2272;
      float _M0L6_2atmpS2270;
      float _M0L3treS2271;
      float _M0L6_2atmpS2269;
      float _M0L6_2atmpS2268;
      float _M0L6_2atmpS2266;
      struct _M0TPB5ArrayGfE* _M0L2giS2275;
      struct _M0TPB5ArrayGfE* _M0L2giS2287;
      float _M0L6_2atmpS2277;
      struct _M0TPB5ArrayGfE* _M0L2giS2286;
      float _M0L6_2atmpS2285;
      float _M0L6_2atmpS2283;
      float _M0L3tdiS2284;
      float _M0L6_2atmpS2280;
      struct _M0TPB5ArrayGfE* _M0L2hiS2282;
      float _M0L6_2atmpS2281;
      float _M0L6_2atmpS2279;
      float _M0L6_2atmpS2278;
      float _M0L6_2atmpS2276;
      struct _M0TPB5ArrayGfE* _M0L2hiS2288;
      struct _M0TPB5ArrayGfE* _M0L2hiS2297;
      float _M0L6_2atmpS2290;
      struct _M0TPB5ArrayGfE* _M0L2hiS2296;
      float _M0L6_2atmpS2295;
      float _M0L6_2atmpS2293;
      float _M0L3triS2294;
      float _M0L6_2atmpS2292;
      float _M0L6_2atmpS2291;
      float _M0L6_2atmpS2289;
      int32_t _M0L6_2atmpS2298;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2242 = _M0MPC15array5Array2atGfE(_M0L2heS2245, _M0L1iS983);
      _M0L3gluS2244 = _M0L1pS981->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2243 = _M0MPC15array5Array2atGfE(_M0L3gluS2244, _M0L1iS983);
      _M0L6_2atmpS2241 = _M0L6_2atmpS2242 + _M0L6_2atmpS2243;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS2240, _M0L1iS983, _M0L6_2atmpS2241);
      _M0L2hiS2246 = _M0L1pS981->$12;
      _M0L2hiS2251 = _M0L1pS981->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2248 = _M0MPC15array5Array2atGfE(_M0L2hiS2251, _M0L1iS983);
      _M0L4gabaS2250 = _M0L1pS981->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2249
      = _M0MPC15array5Array2atGfE(_M0L4gabaS2250, _M0L1iS983);
      _M0L6_2atmpS2247 = _M0L6_2atmpS2248 + _M0L6_2atmpS2249;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2246, _M0L1iS983, _M0L6_2atmpS2247);
      _M0L2geS2252 = _M0L1pS981->$9;
      _M0L2geS2264 = _M0L1pS981->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2254 = _M0MPC15array5Array2atGfE(_M0L2geS2264, _M0L1iS983);
      _M0L2geS2263 = _M0L1pS981->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2262 = _M0MPC15array5Array2atGfE(_M0L2geS2263, _M0L1iS983);
      _M0L6_2atmpS2260 = -_M0L6_2atmpS2262;
      _M0L3tdeS2261 = _M0L1pS981->$20;
      _M0L6_2atmpS2257 = _M0L6_2atmpS2260 / _M0L3tdeS2261;
      _M0L2heS2259 = _M0L1pS981->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2258 = _M0MPC15array5Array2atGfE(_M0L2heS2259, _M0L1iS983);
      _M0L6_2atmpS2256 = _M0L6_2atmpS2257 + _M0L6_2atmpS2258;
      _M0L6_2atmpS2255 = _M0L2dtS984 * _M0L6_2atmpS2256;
      _M0L6_2atmpS2253 = _M0L6_2atmpS2254 + _M0L6_2atmpS2255;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS2252, _M0L1iS983, _M0L6_2atmpS2253);
      _M0L2heS2265 = _M0L1pS981->$11;
      _M0L2heS2274 = _M0L1pS981->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2267 = _M0MPC15array5Array2atGfE(_M0L2heS2274, _M0L1iS983);
      _M0L2heS2273 = _M0L1pS981->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2272 = _M0MPC15array5Array2atGfE(_M0L2heS2273, _M0L1iS983);
      _M0L6_2atmpS2270 = -_M0L6_2atmpS2272;
      _M0L3treS2271 = _M0L1pS981->$19;
      _M0L6_2atmpS2269 = _M0L6_2atmpS2270 / _M0L3treS2271;
      _M0L6_2atmpS2268 = _M0L2dtS984 * _M0L6_2atmpS2269;
      _M0L6_2atmpS2266 = _M0L6_2atmpS2267 + _M0L6_2atmpS2268;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS2265, _M0L1iS983, _M0L6_2atmpS2266);
      _M0L2giS2275 = _M0L1pS981->$10;
      _M0L2giS2287 = _M0L1pS981->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2277 = _M0MPC15array5Array2atGfE(_M0L2giS2287, _M0L1iS983);
      _M0L2giS2286 = _M0L1pS981->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2285 = _M0MPC15array5Array2atGfE(_M0L2giS2286, _M0L1iS983);
      _M0L6_2atmpS2283 = -_M0L6_2atmpS2285;
      _M0L3tdiS2284 = _M0L1pS981->$22;
      _M0L6_2atmpS2280 = _M0L6_2atmpS2283 / _M0L3tdiS2284;
      _M0L2hiS2282 = _M0L1pS981->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2281 = _M0MPC15array5Array2atGfE(_M0L2hiS2282, _M0L1iS983);
      _M0L6_2atmpS2279 = _M0L6_2atmpS2280 + _M0L6_2atmpS2281;
      _M0L6_2atmpS2278 = _M0L2dtS984 * _M0L6_2atmpS2279;
      _M0L6_2atmpS2276 = _M0L6_2atmpS2277 + _M0L6_2atmpS2278;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS2275, _M0L1iS983, _M0L6_2atmpS2276);
      _M0L2hiS2288 = _M0L1pS981->$12;
      _M0L2hiS2297 = _M0L1pS981->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2290 = _M0MPC15array5Array2atGfE(_M0L2hiS2297, _M0L1iS983);
      _M0L2hiS2296 = _M0L1pS981->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2295 = _M0MPC15array5Array2atGfE(_M0L2hiS2296, _M0L1iS983);
      _M0L6_2atmpS2293 = -_M0L6_2atmpS2295;
      _M0L3triS2294 = _M0L1pS981->$21;
      _M0L6_2atmpS2292 = _M0L6_2atmpS2293 / _M0L3triS2294;
      _M0L6_2atmpS2291 = _M0L2dtS984 * _M0L6_2atmpS2292;
      _M0L6_2atmpS2289 = _M0L6_2atmpS2290 + _M0L6_2atmpS2291;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2288, _M0L1iS983, _M0L6_2atmpS2289);
      _M0L6_2atmpS2298 = _M0L1iS983 + 1;
      _M0L1iS983 = _M0L6_2atmpS2298;
      continue;
    }
    break;
  }
  _M0L7_2abindS986 = 0;
  _M0L1iS987 = _M0L7_2abindS986;
  while (1) {
    if (_M0L1iS987 < _M0L1nS980) {
      struct _M0TPB5ArrayGfE* _M0L3gluS2299 = _M0L1pS981->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2300;
      int32_t _M0L6_2atmpS2301;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS2299, _M0L1iS987, 0x0p+0f);
      _M0L4gabaS2300 = _M0L1pS981->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS2300, _M0L1iS987, 0x0p+0f);
      _M0L6_2atmpS2301 = _M0L1iS987 + 1;
      _M0L1iS987 = _M0L6_2atmpS2301;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS966,
  float _M0L2dtS975
) {
  int32_t _M0L1nS965;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S967;
  float _M0L2tmS968;
  float _M0L2elS969;
  float _M0L1rS970;
  float _M0L2vtS971;
  float _M0L2vrS972;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS2239;
  float _M0L11tabs__constS973;
  float _M0L6_2atmpS2238;
  int32_t _M0L11tabs__stepsS974;
  int32_t _M0L7_2abindS976;
  int32_t _M0L1iS977;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS965 = _M0L1pS966->$2;
  _M0L3p__S967 = _M0L1pS966->$0;
  _M0L2tmS968 = _M0L3p__S967->$2;
  _M0L2elS969 = _M0L3p__S967->$5;
  _M0L1rS970 = _M0L3p__S967->$6;
  _M0L2vtS971 = _M0L3p__S967->$3;
  _M0L2vrS972 = _M0L3p__S967->$4;
  _M0L5spikeS2239 = _M0L1pS966->$1;
  _M0L11tabs__constS973 = _M0L5spikeS2239->$0;
  _M0L6_2atmpS2238 = _M0L11tabs__constS973 / _M0L2dtS975;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS974 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2238);
  _M0L7_2abindS976 = 0;
  _M0L1iS977 = _M0L7_2abindS976;
  while (1) {
    if (_M0L1iS977 < _M0L1nS965) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS2198 = _M0L1pS966->$6;
      int32_t _M0L6_2atmpS2197;
      struct _M0TPB5ArrayGfE* _M0L1vS2204;
      struct _M0TPB5ArrayGfE* _M0L1vS2225;
      float _M0L6_2atmpS2206;
      float _M0L6_2atmpS2208;
      struct _M0TPB5ArrayGfE* _M0L1vS2224;
      float _M0L6_2atmpS2223;
      float _M0L6_2atmpS2222;
      float _M0L6_2atmpS2214;
      struct _M0TPB5ArrayGfE* _M0L1wS2221;
      float _M0L6_2atmpS2220;
      float _M0L6_2atmpS2217;
      struct _M0TPB5ArrayGfE* _M0L1iS2219;
      float _M0L6_2atmpS2218;
      float _M0L6_2atmpS2216;
      float _M0L6_2atmpS2215;
      float _M0L6_2atmpS2210;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2213;
      float _M0L6_2atmpS2212;
      float _M0L6_2atmpS2211;
      float _M0L6_2atmpS2209;
      float _M0L6_2atmpS2207;
      float _M0L6_2atmpS2205;
      struct _M0TPB5ArrayGbE* _M0L4fireS2226;
      struct _M0TPB5ArrayGfE* _M0L1vS2229;
      float _M0L6_2atmpS2228;
      int32_t _M0L6_2atmpS2227;
      struct _M0TPB5ArrayGfE* _M0L1vS2230;
      struct _M0TPB5ArrayGbE* _M0L4fireS2232;
      float _M0L6_2atmpS2231;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2234;
      struct _M0TPB5ArrayGbE* _M0L4fireS2236;
      int32_t _M0L6_2atmpS2235;
      int32_t _M0L6_2atmpS2196;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2197
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2198, _M0L1iS977);
      if (_M0L6_2atmpS2197 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS2199 = _M0L1pS966->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2200;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2203;
        int32_t _M0L6_2atmpS2202;
        int32_t _M0L6_2atmpS2201;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS2199, _M0L1iS977, 0);
        _M0L4tabsS2200 = _M0L1pS966->$6;
        _M0L4tabsS2203 = _M0L1pS966->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2202
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2203, _M0L1iS977);
        _M0L6_2atmpS2201 = _M0L6_2atmpS2202 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS2200, _M0L1iS977, _M0L6_2atmpS2201);
        goto join_978;
      }
      _M0L1vS2204 = _M0L1pS966->$3;
      _M0L1vS2225 = _M0L1pS966->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2206 = _M0MPC15array5Array2atGfE(_M0L1vS2225, _M0L1iS977);
      _M0L6_2atmpS2208 = _M0L2dtS975 / _M0L2tmS968;
      _M0L1vS2224 = _M0L1pS966->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2223 = _M0MPC15array5Array2atGfE(_M0L1vS2224, _M0L1iS977);
      _M0L6_2atmpS2222 = _M0L6_2atmpS2223 - _M0L2elS969;
      _M0L6_2atmpS2214 = -_M0L6_2atmpS2222;
      _M0L1wS2221 = _M0L1pS966->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2220 = _M0MPC15array5Array2atGfE(_M0L1wS2221, _M0L1iS977);
      _M0L6_2atmpS2217 = -_M0L6_2atmpS2220;
      _M0L1iS2219 = _M0L1pS966->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2218 = _M0MPC15array5Array2atGfE(_M0L1iS2219, _M0L1iS977);
      _M0L6_2atmpS2216 = _M0L6_2atmpS2217 + _M0L6_2atmpS2218;
      _M0L6_2atmpS2215 = _M0L1rS970 * _M0L6_2atmpS2216;
      _M0L6_2atmpS2210 = _M0L6_2atmpS2214 + _M0L6_2atmpS2215;
      _M0L9syn__currS2213 = _M0L1pS966->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2212
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS2213, _M0L1iS977);
      _M0L6_2atmpS2211 = _M0L1rS970 * _M0L6_2atmpS2212;
      _M0L6_2atmpS2209 = _M0L6_2atmpS2210 - _M0L6_2atmpS2211;
      _M0L6_2atmpS2207 = _M0L6_2atmpS2208 * _M0L6_2atmpS2209;
      _M0L6_2atmpS2205 = _M0L6_2atmpS2206 + _M0L6_2atmpS2207;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2204, _M0L1iS977, _M0L6_2atmpS2205);
      _M0L4fireS2226 = _M0L1pS966->$5;
      _M0L1vS2229 = _M0L1pS966->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2228 = _M0MPC15array5Array2atGfE(_M0L1vS2229, _M0L1iS977);
      _M0L6_2atmpS2227 = _M0L6_2atmpS2228 > _M0L2vtS971;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2226, _M0L1iS977, _M0L6_2atmpS2227);
      _M0L1vS2230 = _M0L1pS966->$3;
      _M0L4fireS2232 = _M0L1pS966->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2232, _M0L1iS977)) {
        _M0L6_2atmpS2231 = _M0L2vrS972;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS2233 = _M0L1pS966->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2231 = _M0MPC15array5Array2atGfE(_M0L1vS2233, _M0L1iS977);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2230, _M0L1iS977, _M0L6_2atmpS2231);
      _M0L4tabsS2234 = _M0L1pS966->$6;
      _M0L4fireS2236 = _M0L1pS966->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2236, _M0L1iS977)) {
        _M0L6_2atmpS2235 = _M0L11tabs__stepsS974;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS2237 = _M0L1pS966->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2235
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2237, _M0L1iS977);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2234, _M0L1iS977, _M0L6_2atmpS2235);
      goto join_978;
      goto joinlet_2473;
      join_978:;
      _M0L6_2atmpS2196 = _M0L1iS977 + 1;
      _M0L1iS977 = _M0L6_2atmpS2196;
      continue;
      joinlet_2473:;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS963
) {
  struct _M0TUmmmmE* _M0L1sS962;
  uint64_t _M0L6_2atmpS2195;
  struct _M0TUmmmmE* _M0L1tS964;
  uint64_t _M0L6_2atmpS2191;
  uint64_t _M0L6_2atmpS2192;
  uint64_t _M0L6_2atmpS2193;
  uint64_t _M0L6_2atmpS2194;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2474;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS962 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS963);
  _M0L6_2atmpS2195 = _M0L1sS962->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS964 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2195);
  _M0L6_2atmpS2191 = _M0L1sS962->$0;
  _M0L6_2atmpS2192 = _M0L1sS962->$1;
  _M0L6_2atmpS2193 = _M0L1sS962->$2;
  moonbit_decref_cycle_free(_M0L1sS962);
  _M0L6_2atmpS2194 = _M0L1tS964->$0;
  moonbit_decref_cycle_free(_M0L1tS964);
  _block_2474
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2474)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2474->$0 = _M0L6_2atmpS2191;
  _block_2474->$1 = _M0L6_2atmpS2192;
  _block_2474->$2 = _M0L6_2atmpS2193;
  _block_2474->$3 = _M0L6_2atmpS2194;
  return _block_2474;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS954) {
  uint64_t _M0L2s1S953;
  uint64_t _M0L2z1S955;
  uint64_t _M0L2s2S956;
  uint64_t _M0L2z2S957;
  uint64_t _M0L2s3S958;
  uint64_t _M0L2z3S959;
  uint64_t _M0L2s4S960;
  uint64_t _M0L2z4S961;
  struct _M0TUmmmmE* _block_2475;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S953 = _M0L4seedS954 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S955 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S953);
  _M0L2s2S956 = _M0L2s1S953 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S957 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S956);
  _M0L2s3S958 = _M0L2s2S956 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S959 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S958);
  _M0L2s4S960 = _M0L2s3S958 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S961 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S960);
  _block_2475 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2475)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2475->$0 = _M0L2z1S955;
  _block_2475->$1 = _M0L2z2S957;
  _block_2475->$2 = _M0L2z3S959;
  _block_2475->$3 = _M0L2z4S961;
  return _block_2475;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS951) {
  uint64_t _M0L6_2atmpS2190;
  uint64_t _M0L6_2atmpS2189;
  uint64_t _M0L1zS950;
  uint64_t _M0L6_2atmpS2188;
  uint64_t _M0L6_2atmpS2187;
  uint64_t _M0L1zS952;
  uint64_t _M0L6_2atmpS2186;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2190 = _M0L1zS951 >> 30;
  _M0L6_2atmpS2189 = _M0L1zS951 ^ _M0L6_2atmpS2190;
  _M0L1zS950 = _M0L6_2atmpS2189 * 13787848793156543929ull;
  _M0L6_2atmpS2188 = _M0L1zS950 >> 27;
  _M0L6_2atmpS2187 = _M0L1zS950 ^ _M0L6_2atmpS2188;
  _M0L1zS952 = _M0L6_2atmpS2187 * 10723151780598845931ull;
  _M0L6_2atmpS2186 = _M0L1zS952 >> 31;
  return _M0L1zS952 ^ _M0L6_2atmpS2186;
}

int32_t _M0FP26RiantR8snn__mbt13stimulate__if(
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L1sS939,
  float _M0L4timeS949,
  float _M0L2dtS941
) {
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS2173;
  struct _M0TPB5ArrayGbE* _M0L6activeS2172;
  int32_t _M0L6_2atmpS2171;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS2185;
  float _M0L4rateS2184;
  float _M0L6lambdaS940;
  struct _M0TPB5ArrayGiE* _M0L7_2abindS942;
  int32_t _M0L7_2abindS943;
  int32_t* _M0L7_2abindS944;
  int32_t _M0L2__S945;
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L5paramS2173 = _M0L1sS939->$0;
  _M0L6activeS2172 = _M0L5paramS2173->$2;
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS2171 = _M0MPC15array5Array2atGbE(_M0L6activeS2172, 0);
  if (!_M0L6_2atmpS2171) {
    return 0;
  }
  _M0L5paramS2185 = _M0L1sS939->$0;
  _M0L4rateS2184 = _M0L5paramS2185->$0;
  _M0L6lambdaS940 = _M0L4rateS2184 * _M0L2dtS941;
  if (_M0L6lambdaS940 <= 0x0p+0f) {
    return 0;
  }
  _M0L7_2abindS942 = _M0L1sS939->$1;
  _M0L7_2abindS943 = _M0L7_2abindS942->$1;
  _M0L7_2abindS944 = _M0L7_2abindS942->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS944);
  _M0L2__S945 = 0;
  while (1) {
    if (_M0L2__S945 < _M0L7_2abindS943) {
      int32_t _M0L1nS946 = (int32_t)_M0L7_2abindS944[_M0L2__S945];
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS2182 = _M0L1sS939->$3;
      int32_t _M0L1kS947;
      int32_t _M0L6_2atmpS2183;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
      _M0L1kS947
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS2182, _M0L6lambdaS940);
      if (_M0L1kS947 > 0) {
        struct _M0TPB5ArrayGfE* _M0L1gS2174 = _M0L1sS939->$2;
        struct _M0TPB5ArrayGfE* _M0L1gS2181 = _M0L1sS939->$2;
        float _M0L6_2atmpS2176;
        struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS2180;
        float _M0L2muS2178;
        float _M0L6_2atmpS2179;
        float _M0L6_2atmpS2177;
        float _M0L6_2atmpS2175;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0L6_2atmpS2176 = _M0MPC15array5Array2atGfE(_M0L1gS2181, _M0L1nS946);
        _M0L5paramS2180 = _M0L1sS939->$0;
        _M0L2muS2178 = _M0L5paramS2180->$1;
        _M0L6_2atmpS2179 = (float)_M0L1kS947;
        _M0L6_2atmpS2177 = _M0L2muS2178 * _M0L6_2atmpS2179;
        _M0L6_2atmpS2175 = _M0L6_2atmpS2176 + _M0L6_2atmpS2177;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0MPC15array5Array3setGfE(_M0L1gS2174, _M0L1nS946, _M0L6_2atmpS2175);
      }
      _M0L6_2atmpS2183 = _M0L2__S945 + 1;
      _M0L2__S945 = _M0L6_2atmpS2183;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS944);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0MP26RiantR8snn__mbt17PoissonStimulusIF3new(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS930,
  moonbit_string_t _M0L3symS936,
  float _M0L4rateS937,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS938
) {
  int32_t _M0L1nS929;
  int32_t* _M0L6_2atmpS2170;
  struct _M0TPB5ArrayGiE* _M0L7neuronsS931;
  int32_t _M0L7_2abindS932;
  int32_t _M0L1kS933;
  struct _M0TPB5ArrayGfE* _M0L1gS935;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L6_2atmpS2169;
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _block_2478;
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L1nS929 = _M0L3popS930->$2;
  _M0L6_2atmpS2170 = (int32_t*)moonbit_empty_int32_array;
  _M0L7neuronsS931
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L7neuronsS931)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 45, 0);
  _M0L7neuronsS931->$0 = _M0L6_2atmpS2170;
  _M0L7neuronsS931->$1 = 0;
  _M0L7_2abindS932 = 0;
  _M0L1kS933 = _M0L7_2abindS932;
  while (1) {
    if (_M0L1kS933 < _M0L1nS929) {
      int32_t _M0L6_2atmpS2168;
      #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
      _M0MPC15array5Array4pushGiE(_M0L7neuronsS931, _M0L1kS933);
      _M0L6_2atmpS2168 = _M0L1kS933 + 1;
      _M0L1kS933 = _M0L6_2atmpS2168;
      continue;
    }
    break;
  }
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  if (
    _M0L3symS936 == (moonbit_string_t)moonbit_string_literal_11.data
    || Moonbit_array_length(_M0L3symS936)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_11.data)
       && 0
          == memcmp(_M0L3symS936, (moonbit_string_t)moonbit_string_literal_11.data, Moonbit_array_length(_M0L3symS936) * 2)
  ) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2407 = _M0L3popS930->$13;
    moonbit_incref_cycle_free(_M0L8_2afieldS2407);
    _M0L1gS935 = _M0L8_2afieldS2407;
  } else {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2408 = _M0L3popS930->$14;
    moonbit_incref_cycle_free(_M0L8_2afieldS2408);
    _M0L1gS935 = _M0L8_2afieldS2408;
  }
  #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS2169 = _M0MP26RiantR8snn__mbt12PoissonFixed3new(_M0L4rateS937);
  moonbit_incref_cycle_free(_M0L3rngS938);
  _block_2478
  = (struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF));
  Moonbit_object_header(_block_2478)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 48, 0);
  _block_2478->$0 = _M0L6_2atmpS2169;
  _block_2478->$1 = _M0L7neuronsS931;
  _block_2478->$2 = _M0L1gS935;
  _block_2478->$3 = _M0L3rngS938;
  return _block_2478;
}

int32_t _M0MP26RiantR8snn__mbt12PoissonFixed7set__mu(
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L1pS927,
  float _M0L2muS928
) {
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L1pS927->$1 = _M0L2muS928;
  return 0;
}

struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0MP26RiantR8snn__mbt12PoissonFixed3new(
  float _M0L4rateS926
) {
  uint8_t* _M0L6_2atmpS2167;
  struct _M0TPB5ArrayGbE* _M0L6_2atmpS2166;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _block_2479;
  #line 23 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS2167 = (uint8_t*)moonbit_make_bytes_raw(1);
  _M0L6_2atmpS2167[0] = 1;
  _M0L6_2atmpS2166
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_M0L6_2atmpS2166)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 54, 0);
  _M0L6_2atmpS2166->$0 = _M0L6_2atmpS2167;
  _M0L6_2atmpS2166->$1 = 1;
  _block_2479
  = (struct _M0TP26RiantR8snn__mbt12PoissonFixed*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt12PoissonFixed));
  Moonbit_object_header(_block_2479)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
  _block_2479->$0 = _M0L4rateS926;
  _block_2479->$1 = 0x1p+0f;
  _block_2479->$2 = _M0L6_2atmpS2166;
  return _block_2479;
}

int32_t _M0FP26RiantR8snn__mbt15sample__poisson(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS924,
  float _M0L6lambdaS918
) {
  float _M0L6_2atmpS2165;
  float _M0L6_2atmpS2164;
  double _M0L1lS919;
  struct _M0TPB8MutLocalGdE* _M0L1pS920;
  struct _M0TPB8MutLocalGiE* _M0L1kS921;
  float _M0L6_2atmpS2163;
  int32_t _M0L8ten__lamS923;
  int32_t _M0L3capS922;
  int32_t _M0L3valS2162;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  if (_M0L6lambdaS918 <= 0x0p+0f) {
    return 0;
  }
  _M0L6_2atmpS2165 = -_M0L6lambdaS918;
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS2164 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2165);
  _M0L1lS919 = (double)_M0L6_2atmpS2164;
  _M0L1pS920
  = (struct _M0TPB8MutLocalGdE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGdE));
  Moonbit_object_header(_M0L1pS920)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1pS920->$0 = 0x1p+0;
  _M0L1kS921
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS921)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS921->$0 = 0;
  _M0L6_2atmpS2163 = _M0L6lambdaS918 * 0x1.4p+3f;
  #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L8ten__lamS923 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2163);
  if (_M0L8ten__lamS923 > 100) {
    _M0L3capS922 = _M0L8ten__lamS923;
  } else {
    _M0L3capS922 = 100;
  }
  while (1) {
    int32_t _M0L3valS2154 = _M0L1kS921->$0;
    int32_t _M0L6_2atmpS2153 = _M0L3valS2154 + 1;
    double _M0L3valS2156;
    double _M0L6_2atmpS2157;
    double _M0L6_2atmpS2155;
    double _M0L3valS2158;
    int32_t _M0L3valS2160;
    _M0L1kS921->$0 = _M0L6_2atmpS2153;
    _M0L3valS2156 = _M0L1pS920->$0;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
    _M0L6_2atmpS2157 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS924);
    _M0L6_2atmpS2155 = _M0L3valS2156 * _M0L6_2atmpS2157;
    _M0L1pS920->$0 = _M0L6_2atmpS2155;
    _M0L3valS2158 = _M0L1pS920->$0;
    if (_M0L3valS2158 < _M0L1lS919) {
      int32_t _M0L3valS2159;
      moonbit_decref_cycle_free(_M0L1pS920);
      _M0L3valS2159 = _M0L1kS921->$0;
      moonbit_decref_cycle_free(_M0L1kS921);
      return _M0L3valS2159 - 1;
    }
    _M0L3valS2160 = _M0L1kS921->$0;
    if (_M0L3valS2160 > _M0L3capS922) {
      int32_t _M0L3valS2161;
      moonbit_decref_cycle_free(_M0L1pS920);
      _M0L3valS2161 = _M0L1kS921->$0;
      moonbit_decref_cycle_free(_M0L1kS921);
      return _M0L3valS2161 - 1;
    }
    continue;
    break;
  }
  _M0L3valS2162 = _M0L1kS921->$0;
  moonbit_decref_cycle_free(_M0L1kS921);
  return _M0L3valS2162 - 1;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS916
) {
  uint64_t _M0L1uS915;
  uint64_t _M0L4bitsS917;
  double _M0L6_2atmpS2152;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS915 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS916);
  _M0L4bitsS917 = _M0L1uS915 >> 11;
  _M0L6_2atmpS2152 = (double)_M0L4bitsS917;
  return _M0L6_2atmpS2152 * 0x1p-53;
}

int32_t _M0FP26RiantR8snn__mbt12update__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS913,
  float _M0L2dtS914
) {
  struct _M0TPB5ArrayGfE* _M0L1tS2144;
  struct _M0TPB5ArrayGfE* _M0L1tS2147;
  float _M0L6_2atmpS2146;
  float _M0L6_2atmpS2145;
  struct _M0TPB5ArrayGiE* _M0L2ttS2148;
  struct _M0TPB5ArrayGiE* _M0L2ttS2151;
  int32_t _M0L6_2atmpS2150;
  int32_t _M0L6_2atmpS2149;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS2144 = _M0L1tS913->$0;
  _M0L1tS2147 = _M0L1tS913->$0;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2146 = _M0MPC15array5Array2atGfE(_M0L1tS2147, 0);
  _M0L6_2atmpS2145 = _M0L6_2atmpS2146 + _M0L2dtS914;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGfE(_M0L1tS2144, 0, _M0L6_2atmpS2145);
  _M0L2ttS2148 = _M0L1tS913->$1;
  _M0L2ttS2151 = _M0L1tS913->$1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2150 = _M0MPC15array5Array2atGiE(_M0L2ttS2151, 0);
  _M0L6_2atmpS2149 = _M0L6_2atmpS2150 + 1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGiE(_M0L2ttS2148, 0, _M0L6_2atmpS2149);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt7set__dt(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS911,
  float _M0L1vS912
) {
  #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS911->$2 = _M0L1vS912;
  return 0;
}

struct _M0TP26RiantR8snn__mbt4Time* _M0MP26RiantR8snn__mbt4Time3new() {
  float* _M0L6_2atmpS2143;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2140;
  int32_t* _M0L6_2atmpS2142;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2141;
  struct _M0TP26RiantR8snn__mbt4Time* _block_2481;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2143 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS2143[0] = 0x0p+0f;
  _M0L6_2atmpS2140
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2140)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _M0L6_2atmpS2140->$0 = _M0L6_2atmpS2143;
  _M0L6_2atmpS2140->$1 = 1;
  _M0L6_2atmpS2142 = (int32_t*)moonbit_make_int32_array_raw(1);
  _M0L6_2atmpS2142[0] = 0;
  _M0L6_2atmpS2141
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2141)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 45, 0);
  _M0L6_2atmpS2141->$0 = _M0L6_2atmpS2142;
  _M0L6_2atmpS2141->$1 = 1;
  _block_2481
  = (struct _M0TP26RiantR8snn__mbt4Time*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt4Time));
  Moonbit_object_header(_block_2481)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 60, 0);
  _block_2481->$0 = _M0L6_2atmpS2140;
  _block_2481->$1 = _M0L6_2atmpS2141;
  _block_2481->$2 = 0x1p-3f;
  return _block_2481;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS909
) {
  uint32_t _M0L1uS908;
  uint32_t _M0L4bitsS910;
  double _M0L6_2atmpS2139;
  double _M0L6_2atmpS2138;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS908 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS909);
  _M0L4bitsS910 = _M0L1uS908 >> 8;
  _M0L6_2atmpS2139 = (double)_M0L4bitsS910;
  _M0L6_2atmpS2138 = _M0L6_2atmpS2139 * 0x1p-24;
  return (float)_M0L6_2atmpS2138;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS907
) {
  uint64_t _M0L1uS906;
  uint64_t _M0L6_2atmpS2137;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS906 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS907);
  _M0L6_2atmpS2137 = _M0L1uS906 >> 32;
  return (uint32_t)_M0L6_2atmpS2137;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS899
) {
  uint64_t _M0L2s0S898;
  uint64_t _M0L2s1S900;
  uint64_t _M0L2s2S901;
  uint64_t _M0L2s3S902;
  uint64_t _M0L3tmpS903;
  uint64_t _M0L6_2atmpS2136;
  uint64_t _M0L3resS904;
  uint64_t _M0L1tS905;
  uint64_t _M0L6_2atmpS2126;
  uint64_t _M0L6_2atmpS2127;
  uint64_t _M0L2s2S2129;
  uint64_t _M0L6_2atmpS2128;
  uint64_t _M0L2s3S2131;
  uint64_t _M0L6_2atmpS2130;
  uint64_t _M0L2s2S2133;
  uint64_t _M0L6_2atmpS2132;
  uint64_t _M0L2s3S2135;
  uint64_t _M0L6_2atmpS2134;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S898 = _M0L1rS899->$0;
  _M0L2s1S900 = _M0L1rS899->$1;
  _M0L2s2S901 = _M0L1rS899->$2;
  _M0L2s3S902 = _M0L1rS899->$3;
  _M0L3tmpS903 = _M0L2s0S898 + _M0L2s3S902;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2136 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS903, 23);
  _M0L3resS904 = _M0L6_2atmpS2136 + _M0L2s0S898;
  _M0L1tS905 = _M0L2s1S900 << 17;
  _M0L6_2atmpS2126 = _M0L2s2S901 ^ _M0L2s0S898;
  _M0L1rS899->$2 = _M0L6_2atmpS2126;
  _M0L6_2atmpS2127 = _M0L2s3S902 ^ _M0L2s1S900;
  _M0L1rS899->$3 = _M0L6_2atmpS2127;
  _M0L2s2S2129 = _M0L1rS899->$2;
  _M0L6_2atmpS2128 = _M0L2s1S900 ^ _M0L2s2S2129;
  _M0L1rS899->$1 = _M0L6_2atmpS2128;
  _M0L2s3S2131 = _M0L1rS899->$3;
  _M0L6_2atmpS2130 = _M0L2s0S898 ^ _M0L2s3S2131;
  _M0L1rS899->$0 = _M0L6_2atmpS2130;
  _M0L2s2S2133 = _M0L1rS899->$2;
  _M0L6_2atmpS2132 = _M0L2s2S2133 ^ _M0L1tS905;
  _M0L1rS899->$2 = _M0L6_2atmpS2132;
  _M0L2s3S2135 = _M0L1rS899->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2134 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S2135, 45);
  _M0L1rS899->$3 = _M0L6_2atmpS2134;
  return _M0L3resS904;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS896, int32_t _M0L1kS897) {
  uint64_t _M0L6_2atmpS2123;
  int32_t _M0L6_2atmpS2125;
  uint64_t _M0L6_2atmpS2124;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2123 = _M0L1xS896 << (_M0L1kS897 & 63);
  _M0L6_2atmpS2125 = 64 - _M0L1kS897;
  _M0L6_2atmpS2124 = _M0L1xS896 >> (_M0L6_2atmpS2125 & 63);
  return _M0L6_2atmpS2123 | _M0L6_2atmpS2124;
}

int32_t _M0MP26RiantR8snn__mbt7Monitor19ascii__plot_2einner(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS843,
  int32_t _M0L5widthS853,
  int32_t _M0L6heightS855
) {
  struct _M0TPB5ArrayGfE* _M0L4dataS2122;
  int32_t _M0L1nS842;
  struct _M0TPB5ArrayGfE* _M0L4dataS2121;
  float _M0L6_2atmpS2120;
  struct _M0TPB8MutLocalGfE* _M0L2mnS845;
  struct _M0TPB5ArrayGfE* _M0L4dataS2119;
  float _M0L6_2atmpS2118;
  struct _M0TPB8MutLocalGfE* _M0L2mxS846;
  int32_t _M0L7_2abindS847;
  int32_t _M0L1iS848;
  float _M0L3valS2114;
  float _M0L3valS2115;
  float _M0L6_2atmpS2113;
  float _M0L6vrangeS851;
  int32_t _M0L7n__colsS852;
  int32_t _M0L7n__rowsS854;
  struct _M0TPB5ArrayGiE* _M0L12col__to__idxS856;
  struct _M0TPB5ArrayGsE* _M0L6canvasS860;
  moonbit_string_t _M0L9row__initS861;
  moonbit_string_t _M0L3padS862;
  int32_t _M0L7_2abindS863;
  int32_t _M0L1rS864;
  int32_t _M0L7_2abindS866;
  int32_t _M0L1cS867;
  float _M0L3valS2112;
  moonbit_string_t _M0L10max__labelS877;
  float _M0L3valS2110;
  float _M0L3valS2111;
  float _M0L6_2atmpS2109;
  float _M0L6_2atmpS2108;
  moonbit_string_t _M0L10mid__labelS878;
  float _M0L3valS2107;
  moonbit_string_t _M0L10min__labelS879;
  int32_t _M0L1aS881;
  int32_t _M0L1bS882;
  int32_t _M0L1cS883;
  int32_t _M0L1mS884;
  int32_t _M0L12label__widthS880;
  int32_t _M0L7_2abindS885;
  int32_t _M0L1rS886;
  struct _M0TPB5ArrayGfE* _M0L5timesS2106;
  int32_t _M0L4n__tS891;
  #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4dataS2122 = _M0L1mS843->$2;
  #line 236 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L1nS842 = _M0MPC15array5Array6lengthGfE(_M0L4dataS2122);
  if (_M0L1nS842 == 0) {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS844;
    moonbit_string_t _M0L3symS2060;
    moonbit_string_t _M0L6_2atmpS2059;
    #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0L18_2astring__builderS844
    = _M0MPB13StringBuilder21StringBuilder_2einner(17);
    #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS844, (moonbit_string_t)moonbit_string_literal_12.data);
    _M0L3symS2060 = _M0L1mS843->$1;
    #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS844, _M0L3symS2060);
    #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS844, (moonbit_string_t)moonbit_string_literal_13.data);
    #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0L6_2atmpS2059
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS844);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS844);
    #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0FPB7printlnGsE(_M0L6_2atmpS2059);
    moonbit_decref_cycle_free(_M0L6_2atmpS2059);
    return 0;
  }
  _M0L4dataS2121 = _M0L1mS843->$2;
  #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2120 = _M0MPC15array5Array2atGfE(_M0L4dataS2121, 0);
  _M0L2mnS845
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L2mnS845)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2mnS845->$0 = _M0L6_2atmpS2120;
  _M0L4dataS2119 = _M0L1mS843->$2;
  #line 243 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2118 = _M0MPC15array5Array2atGfE(_M0L4dataS2119, 0);
  _M0L2mxS846
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L2mxS846)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2mxS846->$0 = _M0L6_2atmpS2118;
  _M0L7_2abindS847 = 0;
  _M0L1iS848 = _M0L7_2abindS847;
  while (1) {
    if (_M0L1iS848 < _M0L1nS842) {
      struct _M0TPB5ArrayGfE* _M0L4dataS2063 = _M0L1mS843->$2;
      float _M0L1vS849;
      float _M0L3valS2061;
      float _M0L3valS2062;
      int32_t _M0L6_2atmpS2064;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L1vS849 = _M0MPC15array5Array2atGfE(_M0L4dataS2063, _M0L1iS848);
      _M0L3valS2061 = _M0L2mnS845->$0;
      if (_M0L1vS849 < _M0L3valS2061) {
        _M0L2mnS845->$0 = _M0L1vS849;
      }
      _M0L3valS2062 = _M0L2mxS846->$0;
      if (_M0L1vS849 > _M0L3valS2062) {
        _M0L2mxS846->$0 = _M0L1vS849;
      }
      _M0L6_2atmpS2064 = _M0L1iS848 + 1;
      _M0L1iS848 = _M0L6_2atmpS2064;
      continue;
    }
    break;
  }
  _M0L3valS2114 = _M0L2mxS846->$0;
  _M0L3valS2115 = _M0L2mnS845->$0;
  _M0L6_2atmpS2113 = _M0L3valS2114 - _M0L3valS2115;
  if (_M0L6_2atmpS2113 < 0x1.0c6f7a0b5ed8dp-20f) {
    _M0L6vrangeS851 = 0x1.0c6f7a0b5ed8dp-20f;
  } else {
    float _M0L3valS2116 = _M0L2mxS846->$0;
    float _M0L3valS2117 = _M0L2mnS845->$0;
    _M0L6vrangeS851 = _M0L3valS2116 - _M0L3valS2117;
  }
  if (_M0L5widthS853 > _M0L1nS842) {
    _M0L7n__colsS852 = _M0L1nS842;
  } else {
    _M0L7n__colsS852 = _M0L5widthS853;
  }
  _M0L7n__rowsS854 = _M0L6heightS855;
  #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L12col__to__idxS856 = _M0MPC15array5Array4makeGiE(_M0L7n__colsS852, 0);
  if (_M0L7n__colsS852 == 1) {
    #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0MPC15array5Array3setGiE(_M0L12col__to__idxS856, 0, 0);
  } else {
    int32_t _M0L7_2abindS857 = 0;
    int32_t _M0L1cS858 = _M0L7_2abindS857;
    while (1) {
      if (_M0L1cS858 < _M0L7n__colsS852) {
        int32_t _M0L6_2atmpS2068 = _M0L1nS842 - 1;
        int32_t _M0L6_2atmpS2066 = _M0L1cS858 * _M0L6_2atmpS2068;
        int32_t _M0L6_2atmpS2067 = _M0L7n__colsS852 - 1;
        int32_t _M0L6_2atmpS2065 = _M0L6_2atmpS2066 / _M0L6_2atmpS2067;
        int32_t _M0L6_2atmpS2069;
        #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
        _M0MPC15array5Array3setGiE(_M0L12col__to__idxS856, _M0L1cS858, _M0L6_2atmpS2065);
        _M0L6_2atmpS2069 = _M0L1cS858 + 1;
        _M0L1cS858 = _M0L6_2atmpS2069;
        continue;
      }
      break;
    }
  }
  #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6canvasS860
  = _M0MPC15array5Array4makeGsE(_M0L7n__rowsS854, (moonbit_string_t)moonbit_string_literal_0.data);
  _M0L9row__initS861 = (moonbit_string_t)moonbit_string_literal_14.data;
  #line 267 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L3padS862 = _M0MPC16string6String4make(_M0L7n__colsS852, 32);
  _M0L7_2abindS863 = 0;
  _M0L1rS864 = _M0L7_2abindS863;
  while (1) {
    if (_M0L1rS864 < _M0L7n__rowsS854) {
      moonbit_string_t _M0L6_2atmpS2070;
      int32_t _M0L6_2atmpS2071;
      #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L6_2atmpS2070 = moonbit_add_string(_M0L9row__initS861, _M0L3padS862);
      #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0MPC15array5Array3setGsE(_M0L6canvasS860, _M0L1rS864, _M0L6_2atmpS2070);
      _M0L6_2atmpS2071 = _M0L1rS864 + 1;
      _M0L1rS864 = _M0L6_2atmpS2071;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L3padS862);
    }
    break;
  }
  _M0L7_2abindS866 = 0;
  _M0L1cS867 = _M0L7_2abindS866;
  while (1) {
    if (_M0L1cS867 < _M0L7n__colsS852) {
      int32_t _M0L3idxS868;
      struct _M0TPB5ArrayGfE* _M0L4dataS2081;
      float _M0L1vS869;
      float _M0L3valS2080;
      float _M0L6_2atmpS2079;
      float _M0L10normalizedS870;
      float _M0L6_2atmpS2076;
      float _M0L6_2atmpS2078;
      float _M0L6_2atmpS2077;
      float _M0L6_2atmpS2075;
      int32_t _M0L14row__from__topS871;
      int32_t _M0L1rS872;
      moonbit_string_t _M0L3symS2074;
      int32_t _M0L2chS873;
      moonbit_string_t _M0L8row__strS874;
      int32_t _M0L6_2atmpS2073;
      int32_t _M0L6_2atmpS2072;
      moonbit_string_t _M0L8new__rowS875;
      int32_t _M0L6_2atmpS2082;
      #line 273 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L3idxS868
      = _M0MPC15array5Array2atGiE(_M0L12col__to__idxS856, _M0L1cS867);
      _M0L4dataS2081 = _M0L1mS843->$2;
      #line 274 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L1vS869 = _M0MPC15array5Array2atGfE(_M0L4dataS2081, _M0L3idxS868);
      _M0L3valS2080 = _M0L2mnS845->$0;
      _M0L6_2atmpS2079 = _M0L1vS869 - _M0L3valS2080;
      _M0L10normalizedS870 = _M0L6_2atmpS2079 / _M0L6vrangeS851;
      _M0L6_2atmpS2076 = (float)_M0L7n__rowsS854;
      _M0L6_2atmpS2078 = (float)1;
      _M0L6_2atmpS2077 = _M0L6_2atmpS2078 * _M0L10normalizedS870;
      _M0L6_2atmpS2075 = _M0L6_2atmpS2076 - _M0L6_2atmpS2077;
      #line 277 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L14row__from__topS871
      = _M0MPC15float5Float7to__int(_M0L6_2atmpS2075);
      if (_M0L14row__from__topS871 < 0) {
        _M0L1rS872 = 0;
      } else if (_M0L14row__from__topS871 >= _M0L7n__rowsS854) {
        _M0L1rS872 = _M0L7n__rowsS854 - 1;
      } else {
        _M0L1rS872 = _M0L14row__from__topS871;
      }
      _M0L3symS2074 = _M0L1mS843->$1;
      #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      if (
        _M0L3symS2074 == (moonbit_string_t)moonbit_string_literal_10.data
        || Moonbit_array_length(_M0L3symS2074)
           == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_10.data)
           && 0
              == memcmp(_M0L3symS2074, (moonbit_string_t)moonbit_string_literal_10.data, Moonbit_array_length(_M0L3symS2074) * 2)
      ) {
        if (_M0L1vS869 >= 0x1p-1f) {
          _M0L2chS873 = 42;
        } else {
          _M0L2chS873 = 46;
        }
      } else {
        _M0L2chS873 = 42;
      }
      #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L8row__strS874
      = _M0MPC15array5Array2atGsE(_M0L6canvasS860, _M0L1rS872);
      _M0L6_2atmpS2073 = Moonbit_array_length(_M0L9row__initS861);
      _M0L6_2atmpS2072 = _M0L1cS867 + _M0L6_2atmpS2073;
      #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L8new__rowS875
      = _M0FP26RiantR8snn__mbt19row__int__set__char(_M0L8row__strS874, _M0L6_2atmpS2072, _M0L2chS873);
      moonbit_decref_cycle_free(_M0L8row__strS874);
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0MPC15array5Array3setGsE(_M0L6canvasS860, _M0L1rS872, _M0L8new__rowS875);
      _M0L6_2atmpS2082 = _M0L1cS867 + 1;
      _M0L1cS867 = _M0L6_2atmpS2082;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L12col__to__idxS856);
    }
    break;
  }
  _M0L3valS2112 = _M0L2mxS846->$0;
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L10max__labelS877
  = _M0FP26RiantR8snn__mbt19format__axis__label(_M0L3valS2112);
  _M0L3valS2110 = _M0L2mxS846->$0;
  moonbit_decref_cycle_free(_M0L2mxS846);
  _M0L3valS2111 = _M0L2mnS845->$0;
  _M0L6_2atmpS2109 = _M0L3valS2110 + _M0L3valS2111;
  _M0L6_2atmpS2108 = _M0L6_2atmpS2109 / 0x1p+1f;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L10mid__labelS878
  = _M0FP26RiantR8snn__mbt19format__axis__label(_M0L6_2atmpS2108);
  _M0L3valS2107 = _M0L2mnS845->$0;
  moonbit_decref_cycle_free(_M0L2mnS845);
  #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L10min__labelS879
  = _M0FP26RiantR8snn__mbt19format__axis__label(_M0L3valS2107);
  _M0L1aS881 = Moonbit_array_length(_M0L10max__labelS877);
  _M0L1bS882 = Moonbit_array_length(_M0L10mid__labelS878);
  _M0L1cS883 = Moonbit_array_length(_M0L10min__labelS879);
  if (_M0L1aS881 > _M0L1bS882) {
    _M0L1mS884 = _M0L1aS881;
  } else {
    _M0L1mS884 = _M0L1bS882;
  }
  if (_M0L1mS884 > _M0L1cS883) {
    _M0L12label__widthS880 = _M0L1mS884;
  } else {
    _M0L12label__widthS880 = _M0L1cS883;
  }
  _M0L7_2abindS885 = 0;
  _M0L1rS886 = _M0L7_2abindS885;
  while (1) {
    if (_M0L1rS886 < _M0L7n__rowsS854) {
      moonbit_string_t _M0L5labelS887;
      int32_t _M0L6_2atmpS2087;
      int32_t _M0L10pad__countS888;
      moonbit_string_t _M0L6paddedS889;
      moonbit_string_t _M0L6_2atmpS2086;
      moonbit_string_t _M0L6_2atmpS2084;
      moonbit_string_t _M0L6_2atmpS2085;
      moonbit_string_t _M0L6_2atmpS2083;
      int32_t _M0L6_2atmpS2091;
      if (_M0L1rS886 == 0) {
        moonbit_incref_cycle_free(_M0L10max__labelS877);
        _M0L5labelS887 = _M0L10max__labelS877;
      } else {
        int32_t _M0L6_2atmpS2089 = _M0L7n__rowsS854 - 1;
        if (_M0L1rS886 == _M0L6_2atmpS2089) {
          moonbit_incref_cycle_free(_M0L10min__labelS879);
          _M0L5labelS887 = _M0L10min__labelS879;
        } else {
          int32_t _M0L6_2atmpS2090 = _M0L7n__rowsS854 / 2;
          if (_M0L1rS886 == _M0L6_2atmpS2090) {
            moonbit_incref_cycle_free(_M0L10mid__labelS878);
            _M0L5labelS887 = _M0L10mid__labelS878;
          } else {
            _M0L5labelS887 = (moonbit_string_t)moonbit_string_literal_0.data;
          }
        }
      }
      _M0L6_2atmpS2087 = Moonbit_array_length(_M0L5labelS887);
      if (_M0L12label__widthS880 > _M0L6_2atmpS2087) {
        int32_t _M0L6_2atmpS2088 = Moonbit_array_length(_M0L5labelS887);
        _M0L10pad__countS888 = _M0L12label__widthS880 - _M0L6_2atmpS2088;
      } else {
        _M0L10pad__countS888 = 0;
      }
      #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L6paddedS889 = _M0MPC16string6String4make(_M0L10pad__countS888, 32);
      #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L6_2atmpS2086 = moonbit_add_string(_M0L6paddedS889, _M0L5labelS887);
      moonbit_decref_cycle_free(_M0L5labelS887);
      moonbit_decref_cycle_free(_M0L6paddedS889);
      #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L6_2atmpS2084
      = moonbit_add_string(_M0L6_2atmpS2086, (moonbit_string_t)moonbit_string_literal_15.data);
      moonbit_decref_cycle_free(_M0L6_2atmpS2086);
      #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L6_2atmpS2085
      = _M0MPC15array5Array2atGsE(_M0L6canvasS860, _M0L1rS886);
      #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L6_2atmpS2083
      = moonbit_add_string(_M0L6_2atmpS2084, _M0L6_2atmpS2085);
      moonbit_decref_cycle_free(_M0L6_2atmpS2085);
      moonbit_decref_cycle_free(_M0L6_2atmpS2084);
      #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0FPB7printlnGsE(_M0L6_2atmpS2083);
      moonbit_decref_cycle_free(_M0L6_2atmpS2083);
      _M0L6_2atmpS2091 = _M0L1rS886 + 1;
      _M0L1rS886 = _M0L6_2atmpS2091;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L10min__labelS879);
      moonbit_decref_cycle_free(_M0L10mid__labelS878);
      moonbit_decref_cycle_free(_M0L6canvasS860);
    }
    break;
  }
  _M0L5timesS2106 = _M0L1mS843->$3;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4n__tS891 = _M0MPC15array5Array6lengthGfE(_M0L5timesS2106);
  if (_M0L4n__tS891 >= 2) {
    struct _M0TPB5ArrayGfE* _M0L5timesS2105 = _M0L1mS843->$3;
    float _M0L8t__startS892;
    struct _M0TPB5ArrayGfE* _M0L5timesS2103;
    int32_t _M0L6_2atmpS2104;
    float _M0L6t__endS893;
    int32_t _M0L6_2atmpS2102;
    int32_t _M0L6_2atmpS2100;
    int32_t _M0L6_2atmpS2101;
    int32_t _M0L6_2atmpS2099;
    int32_t _M0L12total__charsS894;
    int32_t _M0L6_2atmpS2098;
    moonbit_string_t _M0L8pad__strS895;
    moonbit_string_t _M0L6_2atmpS2096;
    moonbit_string_t _M0L6_2atmpS2097;
    moonbit_string_t _M0L6_2atmpS2095;
    moonbit_string_t _M0L6_2atmpS2093;
    moonbit_string_t _M0L6_2atmpS2094;
    moonbit_string_t _M0L6_2atmpS2092;
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0L8t__startS892 = _M0MPC15array5Array2atGfE(_M0L5timesS2105, 0);
    _M0L5timesS2103 = _M0L1mS843->$3;
    _M0L6_2atmpS2104 = _M0L4n__tS891 - 1;
    #line 332 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0L6t__endS893
    = _M0MPC15array5Array2atGfE(_M0L5timesS2103, _M0L6_2atmpS2104);
    _M0L6_2atmpS2102 = Moonbit_array_length(_M0L9row__initS861);
    moonbit_decref_cycle_free(_M0L9row__initS861);
    _M0L6_2atmpS2100 = _M0L6_2atmpS2102 + _M0L7n__colsS852;
    _M0L6_2atmpS2101 = Moonbit_array_length(_M0L10max__labelS877);
    moonbit_decref_cycle_free(_M0L10max__labelS877);
    _M0L6_2atmpS2099 = _M0L6_2atmpS2100 + _M0L6_2atmpS2101;
    _M0L12total__charsS894 = _M0L6_2atmpS2099 + 3;
    _M0L6_2atmpS2098 = _M0L12total__charsS894 - 4;
    #line 334 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0L8pad__strS895 = _M0MPC16string6String4make(_M0L6_2atmpS2098, 32);
    #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0L6_2atmpS2096
    = moonbit_add_string(_M0L8pad__strS895, (moonbit_string_t)moonbit_string_literal_16.data);
    moonbit_decref_cycle_free(_M0L8pad__strS895);
    #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0L6_2atmpS2097
    = _M0IPC15float5FloatPB4Show10to__string(_M0L8t__startS892);
    #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0L6_2atmpS2095 = moonbit_add_string(_M0L6_2atmpS2096, _M0L6_2atmpS2097);
    moonbit_decref_cycle_free(_M0L6_2atmpS2097);
    moonbit_decref_cycle_free(_M0L6_2atmpS2096);
    #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0L6_2atmpS2093
    = moonbit_add_string(_M0L6_2atmpS2095, (moonbit_string_t)moonbit_string_literal_17.data);
    moonbit_decref_cycle_free(_M0L6_2atmpS2095);
    #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0L6_2atmpS2094
    = _M0IPC15float5FloatPB4Show10to__string(_M0L6t__endS893);
    #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0L6_2atmpS2092 = moonbit_add_string(_M0L6_2atmpS2093, _M0L6_2atmpS2094);
    moonbit_decref_cycle_free(_M0L6_2atmpS2094);
    moonbit_decref_cycle_free(_M0L6_2atmpS2093);
    #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0FPB7printlnGsE(_M0L6_2atmpS2092);
    moonbit_decref_cycle_free(_M0L6_2atmpS2092);
  } else {
    moonbit_decref_cycle_free(_M0L10max__labelS877);
    moonbit_decref_cycle_free(_M0L9row__initS861);
  }
  #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0MP26RiantR8snn__mbt7Monitor8duration(_M0L1mS843);
  return 0;
}

moonbit_string_t _M0FP26RiantR8snn__mbt19row__int__set__char(
  moonbit_string_t _M0L1sS832,
  int32_t _M0L3posS835,
  int32_t _M0L2chS839
) {
  int32_t _M0L6_2atmpS2058;
  struct _M0TPB13StringBuilder* _M0L2sbS831;
  int32_t _M0L3lenS833;
  int32_t _M0L11prefix__endS834;
  int32_t _M0L6_2atmpS2050;
  moonbit_string_t _result_2489;
  #line 353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2058 = Moonbit_array_length(_M0L1sS832);
  #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L2sbS831
  = _M0MPB13StringBuilder21StringBuilder_2einner(_M0L6_2atmpS2058);
  _M0L3lenS833 = Moonbit_array_length(_M0L1sS832);
  if (_M0L3posS835 < _M0L3lenS833) {
    _M0L11prefix__endS834 = _M0L3posS835;
  } else {
    _M0L11prefix__endS834 = _M0L3lenS833;
  }
  if (_M0L11prefix__endS834 > 0) {
    moonbit_string_t _M0L6prefixS836;
    struct _M0TPB8MutLocalGiE* _M0L1kS837;
    #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0L6prefixS836 = _M0MPC16string6String4make(_M0L11prefix__endS834, 32);
    _M0L1kS837
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS837)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS837->$0 = 0;
    while (1) {
      int32_t _M0L3valS2044 = _M0L1kS837->$0;
      if (_M0L3valS2044 < _M0L11prefix__endS834) {
        int32_t _M0L3valS2047 = _M0L1kS837->$0;
        int32_t _M0L6_2atmpS2046;
        int32_t _M0L6_2atmpS2045;
        int32_t _M0L3valS2049;
        int32_t _M0L6_2atmpS2048;
        if (
          _M0L3valS2047 < 0
          || _M0L3valS2047 >= Moonbit_array_length(_M0L1sS832)
        ) {
          #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2046 = _M0L1sS832[_M0L3valS2047];
        #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
        _M0L6_2atmpS2045
        = _M0MPC16uint166UInt1616unsafe__to__char(_M0L6_2atmpS2046);
        #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
        _M0IPB13StringBuilderPB6Logger11write__char(_M0L2sbS831, _M0L6_2atmpS2045);
        _M0L3valS2049 = _M0L1kS837->$0;
        _M0L6_2atmpS2048 = _M0L3valS2049 + 1;
        _M0L1kS837->$0 = _M0L6_2atmpS2048;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS837);
      }
      break;
    }
    moonbit_decref_cycle_free(_M0L6prefixS836);
  }
  #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L2sbS831, _M0L2chS839);
  _M0L6_2atmpS2050 = _M0L3posS835 + 1;
  if (_M0L6_2atmpS2050 < _M0L3lenS833) {
    int32_t _M0L6_2atmpS2057 = _M0L3posS835 + 1;
    struct _M0TPB8MutLocalGiE* _M0L1kS840 =
      (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS840)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS840->$0 = _M0L6_2atmpS2057;
    while (1) {
      int32_t _M0L3valS2051 = _M0L1kS840->$0;
      if (_M0L3valS2051 < _M0L3lenS833) {
        int32_t _M0L3valS2054 = _M0L1kS840->$0;
        int32_t _M0L6_2atmpS2053;
        int32_t _M0L6_2atmpS2052;
        int32_t _M0L3valS2056;
        int32_t _M0L6_2atmpS2055;
        if (
          _M0L3valS2054 < 0
          || _M0L3valS2054 >= Moonbit_array_length(_M0L1sS832)
        ) {
          #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2053 = _M0L1sS832[_M0L3valS2054];
        #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
        _M0L6_2atmpS2052
        = _M0MPC16uint166UInt1616unsafe__to__char(_M0L6_2atmpS2053);
        #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
        _M0IPB13StringBuilderPB6Logger11write__char(_M0L2sbS831, _M0L6_2atmpS2052);
        _M0L3valS2056 = _M0L1kS840->$0;
        _M0L6_2atmpS2055 = _M0L3valS2056 + 1;
        _M0L1kS840->$0 = _M0L6_2atmpS2055;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS840);
      }
      break;
    }
  }
  #line 380 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _result_2489 = _M0MPB13StringBuilder10to__string(_M0L2sbS831);
  moonbit_decref_cycle_free(_M0L2sbS831);
  return _result_2489;
}

moonbit_string_t _M0FP26RiantR8snn__mbt19format__axis__label(
  float _M0L1vS828
) {
  float _M0L6scaledS827;
  float _M0L6_2atmpS2043;
  int32_t _M0L7roundedS829;
  float _M0L6_2atmpS2042;
  float _M0L12scaled__backS830;
  #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6scaledS827 = _M0L1vS828 * 0x1.388p+13f;
  #line 345 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2043 = _M0MPC15float5Float5round(_M0L6scaledS827);
  #line 345 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L7roundedS829 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2043);
  _M0L6_2atmpS2042 = (float)_M0L7roundedS829;
  _M0L12scaled__backS830 = _M0L6_2atmpS2042 / 0x1.388p+13f;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  return _M0IPC15float5FloatPB4Show10to__string(_M0L12scaled__backS830);
}

float _M0MP26RiantR8snn__mbt7Monitor8duration(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS826
) {
  struct _M0TPB5ArrayGfE* _M0L5timesS2034;
  int32_t _M0L6_2atmpS2033;
  struct _M0TPB5ArrayGfE* _M0L5timesS2038;
  struct _M0TPB5ArrayGfE* _M0L5timesS2041;
  int32_t _M0L6_2atmpS2040;
  int32_t _M0L6_2atmpS2039;
  float _M0L6_2atmpS2035;
  struct _M0TPB5ArrayGfE* _M0L5timesS2037;
  float _M0L6_2atmpS2036;
  #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L5timesS2034 = _M0L1mS826->$3;
  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2033 = _M0MPC15array5Array6lengthGfE(_M0L5timesS2034);
  if (_M0L6_2atmpS2033 < 2) {
    return 0x0p+0f;
  }
  _M0L5timesS2038 = _M0L1mS826->$3;
  _M0L5timesS2041 = _M0L1mS826->$3;
  #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2040 = _M0MPC15array5Array6lengthGfE(_M0L5timesS2041);
  _M0L6_2atmpS2039 = _M0L6_2atmpS2040 - 1;
  #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2035
  = _M0MPC15array5Array2atGfE(_M0L5timesS2038, _M0L6_2atmpS2039);
  _M0L5timesS2037 = _M0L1mS826->$3;
  #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2036 = _M0MPC15array5Array2atGfE(_M0L5timesS2037, 0);
  return _M0L6_2atmpS2035 - _M0L6_2atmpS2036;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS825) {
  double _M0L6_2atmpS2032;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2032 = (double)_M0L4selfS825;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2032);
}

float _M0MPC15float5Float5round(float _M0L4selfS824) {
  float _M0L6_2atmpS2031;
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\float\\round.mbt"
  _M0L6_2atmpS2031 = _M0L4selfS824 + 0x1p-1f;
  #line 145 "C:\\Users\\31379\\.moon\\lib\\core\\float\\round.mbt"
  return _M0MPC15float5Float5floor(_M0L6_2atmpS2031);
}

float _M0MPC15float5Float5floor(float _M0L4selfS823) {
  float _M0L7truncedS822;
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\float\\round.mbt"
  #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\float\\round.mbt"
  _M0L7truncedS822 = _M0MPC15float5Float5trunc(_M0L4selfS823);
  if (_M0L4selfS823 < _M0L7truncedS822) {
    return _M0L7truncedS822 - 0x1p+0f;
  } else {
    return _M0L7truncedS822;
  }
}

float _M0MPC15float5Float5trunc(float _M0L4selfS818) {
  uint32_t _M0L3u32S817;
  uint32_t _M0L6_2atmpS2030;
  uint32_t _M0L6_2atmpS2029;
  int32_t _M0L11biased__expS819;
  int32_t _M0L6_2atmpS2028;
  int32_t _M0L11mask__shiftS820;
  uint32_t _tmp_2490;
  int32_t _M0L6_2atmpS2027;
  int32_t _M0L6_2atmpS2026;
  uint32_t _M0L11trunc__maskS821;
  uint32_t _M0L6_2atmpS2025;
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\float\\round.mbt"
  _M0L3u32S817 = *(int32_t*)&_M0L4selfS818;
  _M0L6_2atmpS2030 = _M0L3u32S817 >> 23;
  _M0L6_2atmpS2029 = _M0L6_2atmpS2030 & 255u;
  _M0L11biased__expS819 = *(int32_t*)&_M0L6_2atmpS2029;
  if (_M0L11biased__expS819 < 127) {
    uint32_t _M0L6_2atmpS2024 = _M0L3u32S817 & 2147483648u;
    return *(float*)&_M0L6_2atmpS2024;
  } else if (_M0L11biased__expS819 >= 150) {
    return _M0L4selfS818;
  }
  _M0L6_2atmpS2028 = _M0L11biased__expS819 - 127;
  _M0L11mask__shiftS820 = _M0L6_2atmpS2028 + 8;
  _tmp_2490 = 2147483648u;
  _M0L6_2atmpS2027 = *(int32_t*)&_tmp_2490;
  _M0L6_2atmpS2026 = _M0L6_2atmpS2027 >> (_M0L11mask__shiftS820 & 31);
  _M0L11trunc__maskS821 = *(uint32_t*)&_M0L6_2atmpS2026;
  _M0L6_2atmpS2025 = _M0L3u32S817 & _M0L11trunc__maskS821;
  return *(float*)&_M0L6_2atmpS2025;
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS816) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS816 != _M0L4selfS816) {
    return 0;
  } else if (_M0L4selfS816 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS816 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS816;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS797,
  float _M0L4elemS799
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS796;
  int32_t _M0L1iS798;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS796 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS797);
  _M0L1iS798 = 0;
  while (1) {
    if (_M0L1iS798 < _M0L3lenS797) {
      float* _M0L3bufS2016 = _M0L3arrS796->$0;
      int32_t _M0L6_2atmpS2017;
      _M0L3bufS2016[_M0L1iS798] = _M0L4elemS799;
      _M0L6_2atmpS2017 = _M0L1iS798 + 1;
      _M0L1iS798 = _M0L6_2atmpS2017;
      continue;
    }
    break;
  }
  return _M0L3arrS796;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS802,
  int32_t _M0L4elemS804
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS801;
  int32_t _M0L1iS803;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS801 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS802);
  _M0L1iS803 = 0;
  while (1) {
    if (_M0L1iS803 < _M0L3lenS802) {
      uint8_t* _M0L3bufS2018 = _M0L3arrS801->$0;
      int32_t _M0L6_2atmpS2019;
      _M0L3bufS2018[_M0L1iS803] = _M0L4elemS804;
      _M0L6_2atmpS2019 = _M0L1iS803 + 1;
      _M0L1iS803 = _M0L6_2atmpS2019;
      continue;
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
      int32_t* _M0L3bufS2020 = _M0L3arrS806->$0;
      int32_t _M0L6_2atmpS2021;
      _M0L3bufS2020[_M0L1iS808] = _M0L4elemS809;
      _M0L6_2atmpS2021 = _M0L1iS808 + 1;
      _M0L1iS808 = _M0L6_2atmpS2021;
      continue;
    }
    break;
  }
  return _M0L3arrS806;
}

struct _M0TPB5ArrayGsE* _M0MPC15array5Array4makeGsE(
  int32_t _M0L3lenS812,
  moonbit_string_t _M0L4elemS814
) {
  struct _M0TPB5ArrayGsE* _M0L3arrS811;
  int32_t _M0L1iS813;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS811 = _M0MPC15array5Array20unsafe__make__uninitGsE(_M0L3lenS812);
  _M0L1iS813 = 0;
  while (1) {
    if (_M0L1iS813 < _M0L3lenS812) {
      moonbit_string_t* _M0L3bufS2022 = _M0L3arrS811->$0;
      moonbit_string_t _M0L6_2aoldS2409 =
        (moonbit_string_t)_M0L3bufS2022[_M0L1iS813];
      int32_t _M0L6_2atmpS2023;
      moonbit_incref_cycle_free(_M0L4elemS814);
      moonbit_decref_cycle_free(_M0L6_2aoldS2409);
      _M0L3bufS2022[_M0L1iS813] = _M0L4elemS814;
      _M0L6_2atmpS2023 = _M0L1iS813 + 1;
      _M0L1iS813 = _M0L6_2atmpS2023;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS814);
    }
    break;
  }
  return _M0L3arrS811;
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
    float* _M0L6_2atmpS2012;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2012 = _M0MPC15array5Array6bufferGfE(_M0L4selfS781);
    _M0L6_2atmpS2012[_M0L5indexS782] = _M0L5valueS783;
    moonbit_decref_cycle_free(_M0L6_2atmpS2012);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS785,
  int32_t _M0L5indexS786,
  int32_t _M0L5valueS787
) {
  int32_t _M0L3lenS784;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS784 = _M0L4selfS785->$1;
  if (_M0L5indexS786 >= 0 && _M0L5indexS786 < _M0L3lenS784) {
    uint8_t* _M0L6_2atmpS2013;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2013 = _M0MPC15array5Array6bufferGbE(_M0L4selfS785);
    _M0L6_2atmpS2013[_M0L5indexS786] = _M0L5valueS787;
    moonbit_decref_cycle_free(_M0L6_2atmpS2013);
  } else {
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
    int32_t* _M0L6_2atmpS2014;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2014 = _M0MPC15array5Array6bufferGiE(_M0L4selfS789);
    _M0L6_2atmpS2014[_M0L5indexS790] = _M0L5valueS791;
    moonbit_decref_cycle_free(_M0L6_2atmpS2014);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS793,
  int32_t _M0L5indexS794,
  moonbit_string_t _M0L5valueS795
) {
  int32_t _M0L3lenS792;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS792 = _M0L4selfS793->$1;
  if (_M0L5indexS794 >= 0 && _M0L5indexS794 < _M0L3lenS792) {
    moonbit_string_t* _M0L6_2atmpS2015;
    moonbit_string_t _M0L6_2aoldS2410;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2015 = _M0MPC15array5Array6bufferGsE(_M0L4selfS793);
    _M0L6_2aoldS2410 = (moonbit_string_t)_M0L6_2atmpS2015[_M0L5indexS794];
    moonbit_decref_cycle_free(_M0L6_2aoldS2410);
    _M0L6_2atmpS2015[_M0L5indexS794] = _M0L5valueS795;
    moonbit_decref_cycle_free(_M0L6_2atmpS2015);
  } else {
    moonbit_decref_cycle_free(_M0L5valueS795);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS766,
  int32_t _M0L5indexS767
) {
  int32_t _M0L3lenS765;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS765 = _M0L4selfS766->$1;
  if (_M0L5indexS767 >= 0 && _M0L5indexS767 < _M0L3lenS765) {
    float* _M0L6_2atmpS2007;
    float _result_2495;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2007 = _M0MPC15array5Array6bufferGfE(_M0L4selfS766);
    _result_2495 = (float)_M0L6_2atmpS2007[_M0L5indexS767];
    moonbit_decref_cycle_free(_M0L6_2atmpS2007);
    return _result_2495;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS769,
  int32_t _M0L5indexS770
) {
  int32_t _M0L3lenS768;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS768 = _M0L4selfS769->$1;
  if (_M0L5indexS770 >= 0 && _M0L5indexS770 < _M0L3lenS768) {
    uint8_t* _M0L6_2atmpS2008;
    int32_t _result_2496;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2008 = _M0MPC15array5Array6bufferGbE(_M0L4selfS769);
    _result_2496 = (int32_t)_M0L6_2atmpS2008[_M0L5indexS770];
    moonbit_decref_cycle_free(_M0L6_2atmpS2008);
    return _result_2496;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MPC15array5Array2atGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L4selfS772,
  int32_t _M0L5indexS773
) {
  int32_t _M0L3lenS771;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS771 = _M0L4selfS772->$1;
  if (_M0L5indexS773 >= 0 && _M0L5indexS773 < _M0L3lenS771) {
    struct _M0TP26RiantR8snn__mbt7Monitor** _M0L6_2atmpS2009;
    struct _M0TP26RiantR8snn__mbt7Monitor* _M0L6_2atmpS2411;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2009
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt7MonitorE(_M0L4selfS772);
    _M0L6_2atmpS2411
    = (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L6_2atmpS2009[
        _M0L5indexS773
      ];
    if (_M0L6_2atmpS2411) {
      moonbit_incref_cycle_free(_M0L6_2atmpS2411);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2009);
    return _M0L6_2atmpS2411;
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
    int32_t* _M0L6_2atmpS2010;
    int32_t _result_2497;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2010 = _M0MPC15array5Array6bufferGiE(_M0L4selfS775);
    _result_2497 = (int32_t)_M0L6_2atmpS2010[_M0L5indexS776];
    moonbit_decref_cycle_free(_M0L6_2atmpS2010);
    return _result_2497;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS778,
  int32_t _M0L5indexS779
) {
  int32_t _M0L3lenS777;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS777 = _M0L4selfS778->$1;
  if (_M0L5indexS779 >= 0 && _M0L5indexS779 < _M0L3lenS777) {
    moonbit_string_t* _M0L6_2atmpS2011;
    moonbit_string_t _M0L6_2atmpS2412;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2011 = _M0MPC15array5Array6bufferGsE(_M0L4selfS778);
    _M0L6_2atmpS2412 = (moonbit_string_t)_M0L6_2atmpS2011[_M0L5indexS779];
    moonbit_incref_cycle_free(_M0L6_2atmpS2412);
    moonbit_decref_cycle_free(_M0L6_2atmpS2011);
    return _M0L6_2atmpS2412;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS764) {
  moonbit_string_t _M0L6_2atmpS2006;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2006 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS764);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2006);
  moonbit_decref_cycle_free(_M0L6_2atmpS2006);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS763) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS763);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS748) {
  uint64_t _M0L4bitsS751;
  uint64_t _M0L6_2atmpS2005;
  uint64_t _M0L6_2atmpS2004;
  int32_t _M0L8ieeeSignS752;
  uint64_t _M0L12ieeeMantissaS753;
  uint64_t _M0L6_2atmpS2003;
  uint64_t _M0L6_2atmpS2002;
  int32_t _M0L12ieeeExponentS754;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS755;
  struct _M0TPB17FloatingDecimal64* _M0L1vS756;
  moonbit_string_t _result_2499;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS748 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_18.data;
  }
  if (_M0L3valS748 >= -0x1p+53 && _M0L3valS748 <= 0x1p+53) {
    if (_M0L3valS748 >= -0x1p+31 && _M0L3valS748 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS749;
      double _M0L6_2atmpS1991;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS749 = _M0MPC16double6Double7to__int(_M0L3valS748);
      _M0L6_2atmpS1991 = (double)_M0L1iS749;
      if (_M0L6_2atmpS1991 == _M0L3valS748) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS749, 10);
      }
    } else {
      int64_t _M0L1iS750;
      double _M0L6_2atmpS1992;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS750 = _M0MPC16double6Double9to__int64(_M0L3valS748);
      _M0L6_2atmpS1992 = (double)_M0L1iS750;
      if (_M0L6_2atmpS1992 == _M0L3valS748) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS750, 10);
      }
    }
  }
  _M0L4bitsS751 = *(int64_t*)&_M0L3valS748;
  _M0L6_2atmpS2005 = _M0L4bitsS751 >> 63;
  _M0L6_2atmpS2004 = _M0L6_2atmpS2005 & 1ull;
  _M0L8ieeeSignS752 = _M0L6_2atmpS2004 != 0ull;
  _M0L12ieeeMantissaS753 = _M0L4bitsS751 & 4503599627370495ull;
  _M0L6_2atmpS2003 = _M0L4bitsS751 >> 52;
  _M0L6_2atmpS2002 = _M0L6_2atmpS2003 & 2047ull;
  _M0L12ieeeExponentS754 = (int32_t)_M0L6_2atmpS2002;
  if (
    _M0L12ieeeExponentS754 == 2047
    || _M0L12ieeeExponentS754 == 0 && _M0L12ieeeMantissaS753 == 0ull
  ) {
    int32_t _M0L6_2atmpS1993 = _M0L12ieeeExponentS754 != 0;
    int32_t _M0L6_2atmpS1994 = _M0L12ieeeMantissaS753 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS752, _M0L6_2atmpS1993, _M0L6_2atmpS1994);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS755
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS753, _M0L12ieeeExponentS754);
  if (_M0L7_2abindS755 == 0) {
    uint32_t _M0L6_2atmpS1995;
    if (_M0L7_2abindS755) {
      moonbit_decref_cycle_free(_M0L7_2abindS755);
    }
    _M0L6_2atmpS1995 = *(uint32_t*)&_M0L12ieeeExponentS754;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS756 = _M0FPB3d2d(_M0L12ieeeMantissaS753, _M0L6_2atmpS1995);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS757 = _M0L7_2abindS755;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS758 = _M0L7_2aSomeS757;
    struct _M0TPB17FloatingDecimal64* _M0L1xS759 = _M0L4_2afS758;
    while (1) {
      uint64_t _M0L8mantissaS2001 = _M0L1xS759->$0;
      uint64_t _M0L1qS760 = _M0L8mantissaS2001 / 10ull;
      uint64_t _M0L8mantissaS1999 = _M0L1xS759->$0;
      uint64_t _M0L6_2atmpS2000 = 10ull * _M0L1qS760;
      uint64_t _M0L1rS761 = _M0L8mantissaS1999 - _M0L6_2atmpS2000;
      int32_t _M0L8exponentS1998;
      int32_t _M0L6_2atmpS1997;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1996;
      if (_M0L1rS761 != 0ull) {
        _M0L1vS756 = _M0L1xS759;
        break;
      }
      _M0L8exponentS1998 = _M0L1xS759->$1;
      moonbit_decref_cycle_free(_M0L1xS759);
      _M0L6_2atmpS1997 = _M0L8exponentS1998 + 1;
      _M0L6_2atmpS1996
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1996)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1996->$0 = _M0L1qS760;
      _M0L6_2atmpS1996->$1 = _M0L6_2atmpS1997;
      _M0L1xS759 = _M0L6_2atmpS1996;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2499 = _M0FPB9to__chars(_M0L1vS756, _M0L8ieeeSignS752);
  moonbit_decref_cycle_free(_M0L1vS756);
  return _result_2499;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS743,
  int32_t _M0L12ieeeExponentS745
) {
  uint64_t _M0L2m2S742;
  int32_t _M0L6_2atmpS1990;
  int32_t _M0L2e2S744;
  int32_t _M0L6_2atmpS1989;
  uint64_t _M0L6_2atmpS1988;
  uint64_t _M0L4maskS746;
  uint64_t _M0L8fractionS747;
  int32_t _M0L6_2atmpS1987;
  uint64_t _M0L6_2atmpS1986;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1985;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S742 = 4503599627370496ull | _M0L12ieeeMantissaS743;
  _M0L6_2atmpS1990 = _M0L12ieeeExponentS745 - 1023;
  _M0L2e2S744 = _M0L6_2atmpS1990 - 52;
  if (_M0L2e2S744 > 0) {
    return 0;
  }
  if (_M0L2e2S744 < -52) {
    return 0;
  }
  _M0L6_2atmpS1989 = -_M0L2e2S744;
  _M0L6_2atmpS1988 = 1ull << (_M0L6_2atmpS1989 & 63);
  _M0L4maskS746 = _M0L6_2atmpS1988 - 1ull;
  _M0L8fractionS747 = _M0L2m2S742 & _M0L4maskS746;
  if (_M0L8fractionS747 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1987 = -_M0L2e2S744;
  _M0L6_2atmpS1986 = _M0L2m2S742 >> (_M0L6_2atmpS1987 & 63);
  _M0L6_2atmpS1985
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1985)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1985->$0 = _M0L6_2atmpS1986;
  _M0L6_2atmpS1985->$1 = 0;
  return _M0L6_2atmpS1985;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS710,
  int32_t _M0L4signS708
) {
  moonbit_bytes_t _M0L6resultS706;
  int32_t _M0Lm5indexS707;
  uint64_t _M0L6outputS709;
  int32_t _M0L7olengthS711;
  int32_t _M0L8exponentS1984;
  int32_t _M0L6_2atmpS1983;
  int32_t _M0Lm3expS712;
  int32_t _M0L6_2atmpS1982;
  int32_t _M0L6_2atmpS1980;
  int32_t _M0L18scientificNotationS713;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS706 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS707 = 0;
  if (_M0L4signS708) {
    int32_t _M0L6_2atmpS1854 = _M0Lm5indexS707;
    int32_t _M0L6_2atmpS1855;
    if (
      _M0L6_2atmpS1854 < 0
      || _M0L6_2atmpS1854 >= Moonbit_array_length(_M0L6resultS706)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS706[_M0L6_2atmpS1854] = 45;
    _M0L6_2atmpS1855 = _M0Lm5indexS707;
    _M0Lm5indexS707 = _M0L6_2atmpS1855 + 1;
  }
  _M0L6outputS709 = _M0L1vS710->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS711 = _M0FPB17decimal__length17(_M0L6outputS709);
  _M0L8exponentS1984 = _M0L1vS710->$1;
  _M0L6_2atmpS1983 = _M0L8exponentS1984 + _M0L7olengthS711;
  _M0Lm3expS712 = _M0L6_2atmpS1983 - 1;
  _M0L6_2atmpS1982 = _M0Lm3expS712;
  if (_M0L6_2atmpS1982 >= -6) {
    int32_t _M0L6_2atmpS1981 = _M0Lm3expS712;
    _M0L6_2atmpS1980 = _M0L6_2atmpS1981 < 21;
  } else {
    _M0L6_2atmpS1980 = 0;
  }
  _M0L18scientificNotationS713 = !_M0L6_2atmpS1980;
  if (_M0L18scientificNotationS713) {
    int32_t _M0L7_2abindS714 = _M0L7olengthS711 - 1;
    uint64_t _M0L6outputS715;
    int32_t _M0L1iS716 = 0;
    uint64_t _M0L6outputS717 = _M0L6outputS709;
    int32_t _M0L6_2atmpS1856;
    int32_t _M0L6_2atmpS1860;
    int32_t _M0L6_2atmpS1859;
    int32_t _M0L6_2atmpS1858;
    int32_t _M0L6_2atmpS1857;
    int32_t _M0L6_2atmpS1864;
    int32_t _M0L6_2atmpS1865;
    int32_t _M0L6_2atmpS1866;
    int32_t _M0L6_2atmpS1867;
    int32_t _M0L6_2atmpS1868;
    int32_t _M0L6_2atmpS1874;
    int32_t _M0L6_2atmpS1907;
    moonbit_string_t _result_2501;
    while (1) {
      if (_M0L1iS716 < _M0L7_2abindS714) {
        uint64_t _M0L1cS718 = _M0L6outputS717 % 10ull;
        int32_t _M0L6_2atmpS1913 = _M0Lm5indexS707;
        int32_t _M0L6_2atmpS1912 = _M0L6_2atmpS1913 + _M0L7olengthS711;
        int32_t _M0L6_2atmpS1908 = _M0L6_2atmpS1912 - _M0L1iS716;
        int32_t _M0L6_2atmpS1911 = (int32_t)_M0L1cS718;
        int32_t _M0L6_2atmpS1910 = 48 + _M0L6_2atmpS1911;
        int32_t _M0L6_2atmpS1909 = _M0L6_2atmpS1910 & 0xff;
        int32_t _M0L6_2atmpS1914;
        uint64_t _M0L6_2atmpS1915;
        if (
          _M0L6_2atmpS1908 < 0
          || _M0L6_2atmpS1908 >= Moonbit_array_length(_M0L6resultS706)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS706[_M0L6_2atmpS1908] = _M0L6_2atmpS1909;
        _M0L6_2atmpS1914 = _M0L1iS716 + 1;
        _M0L6_2atmpS1915 = _M0L6outputS717 / 10ull;
        _M0L1iS716 = _M0L6_2atmpS1914;
        _M0L6outputS717 = _M0L6_2atmpS1915;
        continue;
      } else {
        _M0L6outputS715 = _M0L6outputS717;
      }
      break;
    }
    _M0L6_2atmpS1856 = _M0Lm5indexS707;
    _M0L6_2atmpS1860 = (int32_t)_M0L6outputS715;
    _M0L6_2atmpS1859 = _M0L6_2atmpS1860 % 10;
    _M0L6_2atmpS1858 = 48 + _M0L6_2atmpS1859;
    _M0L6_2atmpS1857 = _M0L6_2atmpS1858 & 0xff;
    if (
      _M0L6_2atmpS1856 < 0
      || _M0L6_2atmpS1856 >= Moonbit_array_length(_M0L6resultS706)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS706[_M0L6_2atmpS1856] = _M0L6_2atmpS1857;
    if (_M0L7olengthS711 > 1) {
      int32_t _M0L6_2atmpS1862 = _M0Lm5indexS707;
      int32_t _M0L6_2atmpS1861 = _M0L6_2atmpS1862 + 1;
      if (
        _M0L6_2atmpS1861 < 0
        || _M0L6_2atmpS1861 >= Moonbit_array_length(_M0L6resultS706)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS706[_M0L6_2atmpS1861] = 46;
    } else {
      int32_t _M0L6_2atmpS1863 = _M0Lm5indexS707;
      _M0Lm5indexS707 = _M0L6_2atmpS1863 - 1;
    }
    _M0L6_2atmpS1864 = _M0Lm5indexS707;
    _M0L6_2atmpS1865 = _M0L7olengthS711 + 1;
    _M0Lm5indexS707 = _M0L6_2atmpS1864 + _M0L6_2atmpS1865;
    _M0L6_2atmpS1866 = _M0Lm5indexS707;
    if (
      _M0L6_2atmpS1866 < 0
      || _M0L6_2atmpS1866 >= Moonbit_array_length(_M0L6resultS706)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS706[_M0L6_2atmpS1866] = 101;
    _M0L6_2atmpS1867 = _M0Lm5indexS707;
    _M0Lm5indexS707 = _M0L6_2atmpS1867 + 1;
    _M0L6_2atmpS1868 = _M0Lm3expS712;
    if (_M0L6_2atmpS1868 < 0) {
      int32_t _M0L6_2atmpS1869 = _M0Lm5indexS707;
      int32_t _M0L6_2atmpS1870;
      int32_t _M0L6_2atmpS1871;
      if (
        _M0L6_2atmpS1869 < 0
        || _M0L6_2atmpS1869 >= Moonbit_array_length(_M0L6resultS706)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS706[_M0L6_2atmpS1869] = 45;
      _M0L6_2atmpS1870 = _M0Lm5indexS707;
      _M0Lm5indexS707 = _M0L6_2atmpS1870 + 1;
      _M0L6_2atmpS1871 = _M0Lm3expS712;
      _M0Lm3expS712 = -_M0L6_2atmpS1871;
    } else {
      int32_t _M0L6_2atmpS1872 = _M0Lm5indexS707;
      int32_t _M0L6_2atmpS1873;
      if (
        _M0L6_2atmpS1872 < 0
        || _M0L6_2atmpS1872 >= Moonbit_array_length(_M0L6resultS706)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS706[_M0L6_2atmpS1872] = 43;
      _M0L6_2atmpS1873 = _M0Lm5indexS707;
      _M0Lm5indexS707 = _M0L6_2atmpS1873 + 1;
    }
    _M0L6_2atmpS1874 = _M0Lm3expS712;
    if (_M0L6_2atmpS1874 >= 100) {
      int32_t _M0L6_2atmpS1890 = _M0Lm3expS712;
      int32_t _M0L1aS720 = _M0L6_2atmpS1890 / 100;
      int32_t _M0L6_2atmpS1889 = _M0Lm3expS712;
      int32_t _M0L6_2atmpS1888 = _M0L6_2atmpS1889 / 10;
      int32_t _M0L1bS721 = _M0L6_2atmpS1888 % 10;
      int32_t _M0L6_2atmpS1887 = _M0Lm3expS712;
      int32_t _M0L1cS722 = _M0L6_2atmpS1887 % 10;
      int32_t _M0L6_2atmpS1875 = _M0Lm5indexS707;
      int32_t _M0L6_2atmpS1877 = 48 + _M0L1aS720;
      int32_t _M0L6_2atmpS1876 = _M0L6_2atmpS1877 & 0xff;
      int32_t _M0L6_2atmpS1881;
      int32_t _M0L6_2atmpS1878;
      int32_t _M0L6_2atmpS1880;
      int32_t _M0L6_2atmpS1879;
      int32_t _M0L6_2atmpS1885;
      int32_t _M0L6_2atmpS1882;
      int32_t _M0L6_2atmpS1884;
      int32_t _M0L6_2atmpS1883;
      int32_t _M0L6_2atmpS1886;
      if (
        _M0L6_2atmpS1875 < 0
        || _M0L6_2atmpS1875 >= Moonbit_array_length(_M0L6resultS706)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS706[_M0L6_2atmpS1875] = _M0L6_2atmpS1876;
      _M0L6_2atmpS1881 = _M0Lm5indexS707;
      _M0L6_2atmpS1878 = _M0L6_2atmpS1881 + 1;
      _M0L6_2atmpS1880 = 48 + _M0L1bS721;
      _M0L6_2atmpS1879 = _M0L6_2atmpS1880 & 0xff;
      if (
        _M0L6_2atmpS1878 < 0
        || _M0L6_2atmpS1878 >= Moonbit_array_length(_M0L6resultS706)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS706[_M0L6_2atmpS1878] = _M0L6_2atmpS1879;
      _M0L6_2atmpS1885 = _M0Lm5indexS707;
      _M0L6_2atmpS1882 = _M0L6_2atmpS1885 + 2;
      _M0L6_2atmpS1884 = 48 + _M0L1cS722;
      _M0L6_2atmpS1883 = _M0L6_2atmpS1884 & 0xff;
      if (
        _M0L6_2atmpS1882 < 0
        || _M0L6_2atmpS1882 >= Moonbit_array_length(_M0L6resultS706)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS706[_M0L6_2atmpS1882] = _M0L6_2atmpS1883;
      _M0L6_2atmpS1886 = _M0Lm5indexS707;
      _M0Lm5indexS707 = _M0L6_2atmpS1886 + 3;
    } else {
      int32_t _M0L6_2atmpS1891 = _M0Lm3expS712;
      if (_M0L6_2atmpS1891 >= 10) {
        int32_t _M0L6_2atmpS1901 = _M0Lm3expS712;
        int32_t _M0L1aS723 = _M0L6_2atmpS1901 / 10;
        int32_t _M0L6_2atmpS1900 = _M0Lm3expS712;
        int32_t _M0L1bS724 = _M0L6_2atmpS1900 % 10;
        int32_t _M0L6_2atmpS1892 = _M0Lm5indexS707;
        int32_t _M0L6_2atmpS1894 = 48 + _M0L1aS723;
        int32_t _M0L6_2atmpS1893 = _M0L6_2atmpS1894 & 0xff;
        int32_t _M0L6_2atmpS1898;
        int32_t _M0L6_2atmpS1895;
        int32_t _M0L6_2atmpS1897;
        int32_t _M0L6_2atmpS1896;
        int32_t _M0L6_2atmpS1899;
        if (
          _M0L6_2atmpS1892 < 0
          || _M0L6_2atmpS1892 >= Moonbit_array_length(_M0L6resultS706)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS706[_M0L6_2atmpS1892] = _M0L6_2atmpS1893;
        _M0L6_2atmpS1898 = _M0Lm5indexS707;
        _M0L6_2atmpS1895 = _M0L6_2atmpS1898 + 1;
        _M0L6_2atmpS1897 = 48 + _M0L1bS724;
        _M0L6_2atmpS1896 = _M0L6_2atmpS1897 & 0xff;
        if (
          _M0L6_2atmpS1895 < 0
          || _M0L6_2atmpS1895 >= Moonbit_array_length(_M0L6resultS706)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS706[_M0L6_2atmpS1895] = _M0L6_2atmpS1896;
        _M0L6_2atmpS1899 = _M0Lm5indexS707;
        _M0Lm5indexS707 = _M0L6_2atmpS1899 + 2;
      } else {
        int32_t _M0L6_2atmpS1902 = _M0Lm5indexS707;
        int32_t _M0L6_2atmpS1905 = _M0Lm3expS712;
        int32_t _M0L6_2atmpS1904 = 48 + _M0L6_2atmpS1905;
        int32_t _M0L6_2atmpS1903 = _M0L6_2atmpS1904 & 0xff;
        int32_t _M0L6_2atmpS1906;
        if (
          _M0L6_2atmpS1902 < 0
          || _M0L6_2atmpS1902 >= Moonbit_array_length(_M0L6resultS706)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS706[_M0L6_2atmpS1902] = _M0L6_2atmpS1903;
        _M0L6_2atmpS1906 = _M0Lm5indexS707;
        _M0Lm5indexS707 = _M0L6_2atmpS1906 + 1;
      }
    }
    _M0L6_2atmpS1907 = _M0Lm5indexS707;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2501
    = _M0FPB19string__from__bytes(_M0L6resultS706, 0, _M0L6_2atmpS1907);
    moonbit_decref_cycle_free(_M0L6resultS706);
    return _result_2501;
  } else {
    int32_t _M0L6_2atmpS1916 = _M0Lm3expS712;
    int32_t _M0L6_2atmpS1979;
    moonbit_string_t _result_2507;
    if (_M0L6_2atmpS1916 < 0) {
      int32_t _M0L6_2atmpS1917 = _M0Lm5indexS707;
      int32_t _M0L6_2atmpS1919;
      int32_t _M0L6_2atmpS1918;
      int32_t _M0L6_2atmpS1920;
      int32_t _M0L1iS725;
      int32_t _M0L6_2atmpS1935;
      int32_t _M0L6_2atmpS1937;
      int32_t _M0L6_2atmpS1936;
      int32_t _M0L7currentS727;
      int32_t _M0L1iS728;
      uint64_t _M0L6outputS729;
      if (
        _M0L6_2atmpS1917 < 0
        || _M0L6_2atmpS1917 >= Moonbit_array_length(_M0L6resultS706)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS706[_M0L6_2atmpS1917] = 48;
      _M0L6_2atmpS1919 = _M0Lm5indexS707;
      _M0L6_2atmpS1918 = _M0L6_2atmpS1919 + 1;
      if (
        _M0L6_2atmpS1918 < 0
        || _M0L6_2atmpS1918 >= Moonbit_array_length(_M0L6resultS706)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS706[_M0L6_2atmpS1918] = 46;
      _M0L6_2atmpS1920 = _M0Lm5indexS707;
      _M0Lm5indexS707 = _M0L6_2atmpS1920 + 2;
      _M0L1iS725 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1921 = _M0Lm3expS712;
        if (_M0L1iS725 > _M0L6_2atmpS1921) {
          int32_t _M0L6_2atmpS1924 = _M0Lm5indexS707;
          int32_t _M0L6_2atmpS1923 = _M0L6_2atmpS1924 - _M0L1iS725;
          int32_t _M0L6_2atmpS1922 = _M0L6_2atmpS1923 - 1;
          int32_t _M0L6_2atmpS1925;
          if (
            _M0L6_2atmpS1922 < 0
            || _M0L6_2atmpS1922 >= Moonbit_array_length(_M0L6resultS706)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS706[_M0L6_2atmpS1922] = 48;
          _M0L6_2atmpS1925 = _M0L1iS725 - 1;
          _M0L1iS725 = _M0L6_2atmpS1925;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1935 = _M0Lm5indexS707;
      _M0L6_2atmpS1937 = _M0Lm3expS712;
      _M0L6_2atmpS1936 = -1 - _M0L6_2atmpS1937;
      _M0L7currentS727 = _M0L6_2atmpS1935 + _M0L6_2atmpS1936;
      _M0L1iS728 = 0;
      _M0L6outputS729 = _M0L6outputS709;
      while (1) {
        if (_M0L1iS728 < _M0L7olengthS711) {
          int32_t _M0L6_2atmpS1932 = _M0L7currentS727 + _M0L7olengthS711;
          int32_t _M0L6_2atmpS1931 = _M0L6_2atmpS1932 - _M0L1iS728;
          int32_t _M0L6_2atmpS1926 = _M0L6_2atmpS1931 - 1;
          uint64_t _M0L6_2atmpS1930 = _M0L6outputS729 % 10ull;
          int32_t _M0L6_2atmpS1929 = (int32_t)_M0L6_2atmpS1930;
          int32_t _M0L6_2atmpS1928 = 48 + _M0L6_2atmpS1929;
          int32_t _M0L6_2atmpS1927 = _M0L6_2atmpS1928 & 0xff;
          int32_t _M0L6_2atmpS1933;
          uint64_t _M0L6_2atmpS1934;
          if (
            _M0L6_2atmpS1926 < 0
            || _M0L6_2atmpS1926 >= Moonbit_array_length(_M0L6resultS706)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS706[_M0L6_2atmpS1926] = _M0L6_2atmpS1927;
          _M0L6_2atmpS1933 = _M0L1iS728 + 1;
          _M0L6_2atmpS1934 = _M0L6outputS729 / 10ull;
          _M0L1iS728 = _M0L6_2atmpS1933;
          _M0L6outputS729 = _M0L6_2atmpS1934;
          continue;
        }
        break;
      }
      _M0Lm5indexS707 = _M0L7currentS727 + _M0L7olengthS711;
    } else {
      int32_t _M0L6_2atmpS1939 = _M0Lm3expS712;
      int32_t _M0L6_2atmpS1938 = _M0L6_2atmpS1939 + 1;
      if (_M0L6_2atmpS1938 >= _M0L7olengthS711) {
        int32_t _M0L1iS731 = 0;
        uint64_t _M0L6outputS732 = _M0L6outputS709;
        int32_t _M0L6_2atmpS1950;
        int32_t _M0L6_2atmpS1955;
        int32_t _M0L7_2abindS734;
        int32_t _M0L1iS735;
        int32_t _M0L6_2atmpS1956;
        int32_t _M0L6_2atmpS1959;
        int32_t _M0L6_2atmpS1958;
        int32_t _M0L6_2atmpS1957;
        while (1) {
          if (_M0L1iS731 < _M0L7olengthS711) {
            int32_t _M0L6_2atmpS1947 = _M0Lm5indexS707;
            int32_t _M0L6_2atmpS1946 = _M0L6_2atmpS1947 + _M0L7olengthS711;
            int32_t _M0L6_2atmpS1945 = _M0L6_2atmpS1946 - _M0L1iS731;
            int32_t _M0L6_2atmpS1940 = _M0L6_2atmpS1945 - 1;
            uint64_t _M0L6_2atmpS1944 = _M0L6outputS732 % 10ull;
            int32_t _M0L6_2atmpS1943 = (int32_t)_M0L6_2atmpS1944;
            int32_t _M0L6_2atmpS1942 = 48 + _M0L6_2atmpS1943;
            int32_t _M0L6_2atmpS1941 = _M0L6_2atmpS1942 & 0xff;
            int32_t _M0L6_2atmpS1948;
            uint64_t _M0L6_2atmpS1949;
            if (
              _M0L6_2atmpS1940 < 0
              || _M0L6_2atmpS1940 >= Moonbit_array_length(_M0L6resultS706)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS706[_M0L6_2atmpS1940] = _M0L6_2atmpS1941;
            _M0L6_2atmpS1948 = _M0L1iS731 + 1;
            _M0L6_2atmpS1949 = _M0L6outputS732 / 10ull;
            _M0L1iS731 = _M0L6_2atmpS1948;
            _M0L6outputS732 = _M0L6_2atmpS1949;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1950 = _M0Lm5indexS707;
        _M0Lm5indexS707 = _M0L6_2atmpS1950 + _M0L7olengthS711;
        _M0L6_2atmpS1955 = _M0Lm3expS712;
        _M0L7_2abindS734 = _M0L6_2atmpS1955 + 1;
        _M0L1iS735 = _M0L7olengthS711;
        while (1) {
          if (_M0L1iS735 < _M0L7_2abindS734) {
            int32_t _M0L6_2atmpS1953 = _M0Lm5indexS707;
            int32_t _M0L6_2atmpS1952 = _M0L6_2atmpS1953 + _M0L1iS735;
            int32_t _M0L6_2atmpS1951 = _M0L6_2atmpS1952 - _M0L7olengthS711;
            int32_t _M0L6_2atmpS1954;
            if (
              _M0L6_2atmpS1951 < 0
              || _M0L6_2atmpS1951 >= Moonbit_array_length(_M0L6resultS706)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS706[_M0L6_2atmpS1951] = 48;
            _M0L6_2atmpS1954 = _M0L1iS735 + 1;
            _M0L1iS735 = _M0L6_2atmpS1954;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1956 = _M0Lm5indexS707;
        _M0L6_2atmpS1959 = _M0Lm3expS712;
        _M0L6_2atmpS1958 = _M0L6_2atmpS1959 + 1;
        _M0L6_2atmpS1957 = _M0L6_2atmpS1958 - _M0L7olengthS711;
        _M0Lm5indexS707 = _M0L6_2atmpS1956 + _M0L6_2atmpS1957;
      } else {
        int32_t _M0L6_2atmpS1976 = _M0Lm5indexS707;
        int32_t _M0L6_2atmpS1975 = _M0L6_2atmpS1976 + 1;
        int32_t _M0L1iS737 = 0;
        int32_t _M0L7currentS738 = _M0L6_2atmpS1975;
        uint64_t _M0L6outputS739 = _M0L6outputS709;
        int32_t _M0L6_2atmpS1977;
        int32_t _M0L6_2atmpS1978;
        while (1) {
          if (_M0L1iS737 < _M0L7olengthS711) {
            int32_t _M0L6_2atmpS1971 = _M0L7olengthS711 - _M0L1iS737;
            int32_t _M0L6_2atmpS1969 = _M0L6_2atmpS1971 - 1;
            int32_t _M0L6_2atmpS1970 = _M0Lm3expS712;
            int32_t _M0L7currentS740;
            int32_t _M0L6_2atmpS1966;
            int32_t _M0L6_2atmpS1965;
            int32_t _M0L6_2atmpS1960;
            uint64_t _M0L6_2atmpS1964;
            int32_t _M0L6_2atmpS1963;
            int32_t _M0L6_2atmpS1962;
            int32_t _M0L6_2atmpS1961;
            int32_t _M0L6_2atmpS1967;
            uint64_t _M0L6_2atmpS1968;
            if (_M0L6_2atmpS1969 == _M0L6_2atmpS1970) {
              int32_t _M0L6_2atmpS1974 = _M0L7currentS738 + _M0L7olengthS711;
              int32_t _M0L6_2atmpS1973 = _M0L6_2atmpS1974 - _M0L1iS737;
              int32_t _M0L6_2atmpS1972 = _M0L6_2atmpS1973 - 1;
              if (
                _M0L6_2atmpS1972 < 0
                || _M0L6_2atmpS1972 >= Moonbit_array_length(_M0L6resultS706)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS706[_M0L6_2atmpS1972] = 46;
              _M0L7currentS740 = _M0L7currentS738 - 1;
            } else {
              _M0L7currentS740 = _M0L7currentS738;
            }
            _M0L6_2atmpS1966 = _M0L7currentS740 + _M0L7olengthS711;
            _M0L6_2atmpS1965 = _M0L6_2atmpS1966 - _M0L1iS737;
            _M0L6_2atmpS1960 = _M0L6_2atmpS1965 - 1;
            _M0L6_2atmpS1964 = _M0L6outputS739 % 10ull;
            _M0L6_2atmpS1963 = (int32_t)_M0L6_2atmpS1964;
            _M0L6_2atmpS1962 = 48 + _M0L6_2atmpS1963;
            _M0L6_2atmpS1961 = _M0L6_2atmpS1962 & 0xff;
            if (
              _M0L6_2atmpS1960 < 0
              || _M0L6_2atmpS1960 >= Moonbit_array_length(_M0L6resultS706)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS706[_M0L6_2atmpS1960] = _M0L6_2atmpS1961;
            _M0L6_2atmpS1967 = _M0L1iS737 + 1;
            _M0L6_2atmpS1968 = _M0L6outputS739 / 10ull;
            _M0L1iS737 = _M0L6_2atmpS1967;
            _M0L7currentS738 = _M0L7currentS740;
            _M0L6outputS739 = _M0L6_2atmpS1968;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1977 = _M0Lm5indexS707;
        _M0L6_2atmpS1978 = _M0L7olengthS711 + 1;
        _M0Lm5indexS707 = _M0L6_2atmpS1977 + _M0L6_2atmpS1978;
      }
    }
    _M0L6_2atmpS1979 = _M0Lm5indexS707;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2507
    = _M0FPB19string__from__bytes(_M0L6resultS706, 0, _M0L6_2atmpS1979);
    moonbit_decref_cycle_free(_M0L6resultS706);
    return _result_2507;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS652,
  uint32_t _M0L12ieeeExponentS651
) {
  int32_t _M0Lm2e2S649;
  uint64_t _M0Lm2m2S650;
  uint64_t _M0L6_2atmpS1853;
  uint64_t _M0L6_2atmpS1852;
  int32_t _M0L4evenS653;
  uint64_t _M0L6_2atmpS1851;
  uint64_t _M0L2mvS654;
  int32_t _M0L7mmShiftS655;
  uint64_t _M0Lm2vrS656;
  uint64_t _M0Lm2vpS657;
  uint64_t _M0Lm2vmS658;
  int32_t _M0Lm3e10S659;
  int32_t _M0Lm17vmIsTrailingZerosS660;
  int32_t _M0Lm17vrIsTrailingZerosS661;
  int32_t _M0L6_2atmpS1753;
  int32_t _M0Lm7removedS680;
  int32_t _M0Lm16lastRemovedDigitS681;
  uint64_t _M0Lm6outputS682;
  int32_t _M0L6_2atmpS1849;
  int32_t _M0L6_2atmpS1850;
  int32_t _M0L3expS705;
  uint64_t _M0L6_2atmpS1848;
  struct _M0TPB17FloatingDecimal64* _block_2513;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S649 = 0;
  _M0Lm2m2S650 = 0ull;
  if (_M0L12ieeeExponentS651 == 0u) {
    _M0Lm2e2S649 = -1076;
    _M0Lm2m2S650 = _M0L12ieeeMantissaS652;
  } else {
    int32_t _M0L6_2atmpS1752 = *(int32_t*)&_M0L12ieeeExponentS651;
    int32_t _M0L6_2atmpS1751 = _M0L6_2atmpS1752 - 1023;
    int32_t _M0L6_2atmpS1750 = _M0L6_2atmpS1751 - 52;
    _M0Lm2e2S649 = _M0L6_2atmpS1750 - 2;
    _M0Lm2m2S650 = 4503599627370496ull | _M0L12ieeeMantissaS652;
  }
  _M0L6_2atmpS1853 = _M0Lm2m2S650;
  _M0L6_2atmpS1852 = _M0L6_2atmpS1853 & 1ull;
  _M0L4evenS653 = _M0L6_2atmpS1852 == 0ull;
  _M0L6_2atmpS1851 = _M0Lm2m2S650;
  _M0L2mvS654 = 4ull * _M0L6_2atmpS1851;
  _M0L7mmShiftS655
  = _M0L12ieeeMantissaS652 != 0ull || _M0L12ieeeExponentS651 <= 1u;
  _M0Lm2vrS656 = 0ull;
  _M0Lm2vpS657 = 0ull;
  _M0Lm2vmS658 = 0ull;
  _M0Lm3e10S659 = 0;
  _M0Lm17vmIsTrailingZerosS660 = 0;
  _M0Lm17vrIsTrailingZerosS661 = 0;
  _M0L6_2atmpS1753 = _M0Lm2e2S649;
  if (_M0L6_2atmpS1753 >= 0) {
    int32_t _M0L6_2atmpS1775 = _M0Lm2e2S649;
    int32_t _M0L6_2atmpS1771;
    int32_t _M0L6_2atmpS1774;
    int32_t _M0L6_2atmpS1773;
    int32_t _M0L6_2atmpS1772;
    int32_t _M0L1qS662;
    int32_t _M0L6_2atmpS1770;
    int32_t _M0L6_2atmpS1769;
    int32_t _M0L1kS663;
    int32_t _M0L6_2atmpS1768;
    int32_t _M0L6_2atmpS1767;
    int32_t _M0L6_2atmpS1766;
    int32_t _M0L1iS664;
    struct _M0TPB8Pow5Pair _M0L4pow5S665;
    uint64_t _M0L6_2atmpS1765;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS666;
    uint64_t _M0L8_2avrOutS667;
    uint64_t _M0L8_2avpOutS668;
    uint64_t _M0L8_2avmOutS669;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1771 = _M0FPB9log10Pow2(_M0L6_2atmpS1775);
    _M0L6_2atmpS1774 = _M0Lm2e2S649;
    _M0L6_2atmpS1773 = _M0L6_2atmpS1774 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1772 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1773);
    _M0L1qS662 = _M0L6_2atmpS1771 - _M0L6_2atmpS1772;
    _M0Lm3e10S659 = _M0L1qS662;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1770 = _M0FPB8pow5bits(_M0L1qS662);
    _M0L6_2atmpS1769 = 125 + _M0L6_2atmpS1770;
    _M0L1kS663 = _M0L6_2atmpS1769 - 1;
    _M0L6_2atmpS1768 = _M0Lm2e2S649;
    _M0L6_2atmpS1767 = -_M0L6_2atmpS1768;
    _M0L6_2atmpS1766 = _M0L6_2atmpS1767 + _M0L1qS662;
    _M0L1iS664 = _M0L6_2atmpS1766 + _M0L1kS663;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S665 = _M0FPB22double__computeInvPow5(_M0L1qS662);
    _M0L6_2atmpS1765 = _M0Lm2m2S650;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS666
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1765, _M0L4pow5S665, _M0L1iS664, _M0L7mmShiftS655);
    _M0L8_2avrOutS667 = _M0L7_2abindS666.$0;
    _M0L8_2avpOutS668 = _M0L7_2abindS666.$1;
    _M0L8_2avmOutS669 = _M0L7_2abindS666.$2;
    _M0Lm2vrS656 = _M0L8_2avrOutS667;
    _M0Lm2vpS657 = _M0L8_2avpOutS668;
    _M0Lm2vmS658 = _M0L8_2avmOutS669;
    if (_M0L1qS662 <= 21) {
      int32_t _M0L6_2atmpS1761 = (int32_t)_M0L2mvS654;
      uint64_t _M0L6_2atmpS1764 = _M0L2mvS654 / 5ull;
      int32_t _M0L6_2atmpS1763 = (int32_t)_M0L6_2atmpS1764;
      int32_t _M0L6_2atmpS1762 = 5 * _M0L6_2atmpS1763;
      int32_t _M0L6mvMod5S670 = _M0L6_2atmpS1761 - _M0L6_2atmpS1762;
      if (_M0L6mvMod5S670 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS661
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS654, _M0L1qS662);
      } else if (_M0L4evenS653) {
        uint64_t _M0L6_2atmpS1755 = _M0L2mvS654 - 1ull;
        uint64_t _M0L6_2atmpS1756;
        uint64_t _M0L6_2atmpS1754;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1756 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS655);
        _M0L6_2atmpS1754 = _M0L6_2atmpS1755 - _M0L6_2atmpS1756;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS660
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1754, _M0L1qS662);
      } else {
        uint64_t _M0L6_2atmpS1757 = _M0Lm2vpS657;
        uint64_t _M0L6_2atmpS1760 = _M0L2mvS654 + 2ull;
        int32_t _M0L6_2atmpS1759;
        uint64_t _M0L6_2atmpS1758;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1759
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1760, _M0L1qS662);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1758 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1759);
        _M0Lm2vpS657 = _M0L6_2atmpS1757 - _M0L6_2atmpS1758;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1789 = _M0Lm2e2S649;
    int32_t _M0L6_2atmpS1788 = -_M0L6_2atmpS1789;
    int32_t _M0L6_2atmpS1783;
    int32_t _M0L6_2atmpS1787;
    int32_t _M0L6_2atmpS1786;
    int32_t _M0L6_2atmpS1785;
    int32_t _M0L6_2atmpS1784;
    int32_t _M0L1qS671;
    int32_t _M0L6_2atmpS1776;
    int32_t _M0L6_2atmpS1782;
    int32_t _M0L6_2atmpS1781;
    int32_t _M0L1iS672;
    int32_t _M0L6_2atmpS1780;
    int32_t _M0L1kS673;
    int32_t _M0L1jS674;
    struct _M0TPB8Pow5Pair _M0L4pow5S675;
    uint64_t _M0L6_2atmpS1779;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS676;
    uint64_t _M0L8_2avrOutS677;
    uint64_t _M0L8_2avpOutS678;
    uint64_t _M0L8_2avmOutS679;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1783 = _M0FPB9log10Pow5(_M0L6_2atmpS1788);
    _M0L6_2atmpS1787 = _M0Lm2e2S649;
    _M0L6_2atmpS1786 = -_M0L6_2atmpS1787;
    _M0L6_2atmpS1785 = _M0L6_2atmpS1786 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1784 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1785);
    _M0L1qS671 = _M0L6_2atmpS1783 - _M0L6_2atmpS1784;
    _M0L6_2atmpS1776 = _M0Lm2e2S649;
    _M0Lm3e10S659 = _M0L1qS671 + _M0L6_2atmpS1776;
    _M0L6_2atmpS1782 = _M0Lm2e2S649;
    _M0L6_2atmpS1781 = -_M0L6_2atmpS1782;
    _M0L1iS672 = _M0L6_2atmpS1781 - _M0L1qS671;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1780 = _M0FPB8pow5bits(_M0L1iS672);
    _M0L1kS673 = _M0L6_2atmpS1780 - 125;
    _M0L1jS674 = _M0L1qS671 - _M0L1kS673;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S675 = _M0FPB19double__computePow5(_M0L1iS672);
    _M0L6_2atmpS1779 = _M0Lm2m2S650;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS676
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1779, _M0L4pow5S675, _M0L1jS674, _M0L7mmShiftS655);
    _M0L8_2avrOutS677 = _M0L7_2abindS676.$0;
    _M0L8_2avpOutS678 = _M0L7_2abindS676.$1;
    _M0L8_2avmOutS679 = _M0L7_2abindS676.$2;
    _M0Lm2vrS656 = _M0L8_2avrOutS677;
    _M0Lm2vpS657 = _M0L8_2avpOutS678;
    _M0Lm2vmS658 = _M0L8_2avmOutS679;
    if (_M0L1qS671 <= 1) {
      _M0Lm17vrIsTrailingZerosS661 = 1;
      if (_M0L4evenS653) {
        int32_t _M0L6_2atmpS1777;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1777 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS655);
        _M0Lm17vmIsTrailingZerosS660 = _M0L6_2atmpS1777 == 1;
      } else {
        uint64_t _M0L6_2atmpS1778 = _M0Lm2vpS657;
        _M0Lm2vpS657 = _M0L6_2atmpS1778 - 1ull;
      }
    } else if (_M0L1qS671 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS661
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS654, _M0L1qS671);
    }
  }
  _M0Lm7removedS680 = 0;
  _M0Lm16lastRemovedDigitS681 = 0;
  _M0Lm6outputS682 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS660 || _M0Lm17vrIsTrailingZerosS661) {
    int32_t _if__result_2510;
    uint64_t _M0L6_2atmpS1819;
    uint64_t _M0L6_2atmpS1825;
    uint64_t _M0L6_2atmpS1826;
    int32_t _if__result_2511;
    int32_t _M0L6_2atmpS1822;
    int64_t _M0L6_2atmpS1821;
    uint64_t _M0L6_2atmpS1820;
    while (1) {
      uint64_t _M0L6_2atmpS1802 = _M0Lm2vpS657;
      uint64_t _M0L7vpDiv10S683 = _M0L6_2atmpS1802 / 10ull;
      uint64_t _M0L6_2atmpS1801 = _M0Lm2vmS658;
      uint64_t _M0L7vmDiv10S684 = _M0L6_2atmpS1801 / 10ull;
      uint64_t _M0L6_2atmpS1800;
      int32_t _M0L6_2atmpS1797;
      int32_t _M0L6_2atmpS1799;
      int32_t _M0L6_2atmpS1798;
      int32_t _M0L7vmMod10S686;
      uint64_t _M0L6_2atmpS1796;
      uint64_t _M0L7vrDiv10S687;
      uint64_t _M0L6_2atmpS1795;
      int32_t _M0L6_2atmpS1792;
      int32_t _M0L6_2atmpS1794;
      int32_t _M0L6_2atmpS1793;
      int32_t _M0L7vrMod10S688;
      int32_t _M0L6_2atmpS1791;
      if (_M0L7vpDiv10S683 <= _M0L7vmDiv10S684) {
        break;
      }
      _M0L6_2atmpS1800 = _M0Lm2vmS658;
      _M0L6_2atmpS1797 = (int32_t)_M0L6_2atmpS1800;
      _M0L6_2atmpS1799 = (int32_t)_M0L7vmDiv10S684;
      _M0L6_2atmpS1798 = 10 * _M0L6_2atmpS1799;
      _M0L7vmMod10S686 = _M0L6_2atmpS1797 - _M0L6_2atmpS1798;
      _M0L6_2atmpS1796 = _M0Lm2vrS656;
      _M0L7vrDiv10S687 = _M0L6_2atmpS1796 / 10ull;
      _M0L6_2atmpS1795 = _M0Lm2vrS656;
      _M0L6_2atmpS1792 = (int32_t)_M0L6_2atmpS1795;
      _M0L6_2atmpS1794 = (int32_t)_M0L7vrDiv10S687;
      _M0L6_2atmpS1793 = 10 * _M0L6_2atmpS1794;
      _M0L7vrMod10S688 = _M0L6_2atmpS1792 - _M0L6_2atmpS1793;
      _M0Lm17vmIsTrailingZerosS660
      = _M0Lm17vmIsTrailingZerosS660 && _M0L7vmMod10S686 == 0;
      if (_M0Lm17vrIsTrailingZerosS661) {
        int32_t _M0L6_2atmpS1790 = _M0Lm16lastRemovedDigitS681;
        _M0Lm17vrIsTrailingZerosS661 = _M0L6_2atmpS1790 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS661 = 0;
      }
      _M0Lm16lastRemovedDigitS681 = _M0L7vrMod10S688;
      _M0Lm2vrS656 = _M0L7vrDiv10S687;
      _M0Lm2vpS657 = _M0L7vpDiv10S683;
      _M0Lm2vmS658 = _M0L7vmDiv10S684;
      _M0L6_2atmpS1791 = _M0Lm7removedS680;
      _M0Lm7removedS680 = _M0L6_2atmpS1791 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS660) {
      while (1) {
        uint64_t _M0L6_2atmpS1815 = _M0Lm2vmS658;
        uint64_t _M0L7vmDiv10S689 = _M0L6_2atmpS1815 / 10ull;
        uint64_t _M0L6_2atmpS1814 = _M0Lm2vmS658;
        int32_t _M0L6_2atmpS1811 = (int32_t)_M0L6_2atmpS1814;
        int32_t _M0L6_2atmpS1813 = (int32_t)_M0L7vmDiv10S689;
        int32_t _M0L6_2atmpS1812 = 10 * _M0L6_2atmpS1813;
        int32_t _M0L7vmMod10S690 = _M0L6_2atmpS1811 - _M0L6_2atmpS1812;
        uint64_t _M0L6_2atmpS1810;
        uint64_t _M0L7vpDiv10S692;
        uint64_t _M0L6_2atmpS1809;
        uint64_t _M0L7vrDiv10S693;
        uint64_t _M0L6_2atmpS1808;
        int32_t _M0L6_2atmpS1805;
        int32_t _M0L6_2atmpS1807;
        int32_t _M0L6_2atmpS1806;
        int32_t _M0L7vrMod10S694;
        int32_t _M0L6_2atmpS1804;
        if (_M0L7vmMod10S690 != 0) {
          break;
        }
        _M0L6_2atmpS1810 = _M0Lm2vpS657;
        _M0L7vpDiv10S692 = _M0L6_2atmpS1810 / 10ull;
        _M0L6_2atmpS1809 = _M0Lm2vrS656;
        _M0L7vrDiv10S693 = _M0L6_2atmpS1809 / 10ull;
        _M0L6_2atmpS1808 = _M0Lm2vrS656;
        _M0L6_2atmpS1805 = (int32_t)_M0L6_2atmpS1808;
        _M0L6_2atmpS1807 = (int32_t)_M0L7vrDiv10S693;
        _M0L6_2atmpS1806 = 10 * _M0L6_2atmpS1807;
        _M0L7vrMod10S694 = _M0L6_2atmpS1805 - _M0L6_2atmpS1806;
        if (_M0Lm17vrIsTrailingZerosS661) {
          int32_t _M0L6_2atmpS1803 = _M0Lm16lastRemovedDigitS681;
          _M0Lm17vrIsTrailingZerosS661 = _M0L6_2atmpS1803 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS661 = 0;
        }
        _M0Lm16lastRemovedDigitS681 = _M0L7vrMod10S694;
        _M0Lm2vrS656 = _M0L7vrDiv10S693;
        _M0Lm2vpS657 = _M0L7vpDiv10S692;
        _M0Lm2vmS658 = _M0L7vmDiv10S689;
        _M0L6_2atmpS1804 = _M0Lm7removedS680;
        _M0Lm7removedS680 = _M0L6_2atmpS1804 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS661) {
      int32_t _M0L6_2atmpS1818 = _M0Lm16lastRemovedDigitS681;
      if (_M0L6_2atmpS1818 == 5) {
        uint64_t _M0L6_2atmpS1817 = _M0Lm2vrS656;
        uint64_t _M0L6_2atmpS1816 = _M0L6_2atmpS1817 % 2ull;
        _if__result_2510 = _M0L6_2atmpS1816 == 0ull;
      } else {
        _if__result_2510 = 0;
      }
    } else {
      _if__result_2510 = 0;
    }
    if (_if__result_2510) {
      _M0Lm16lastRemovedDigitS681 = 4;
    }
    _M0L6_2atmpS1819 = _M0Lm2vrS656;
    _M0L6_2atmpS1825 = _M0Lm2vrS656;
    _M0L6_2atmpS1826 = _M0Lm2vmS658;
    if (_M0L6_2atmpS1825 == _M0L6_2atmpS1826) {
      if (!_M0L4evenS653) {
        _if__result_2511 = 1;
      } else {
        int32_t _M0L6_2atmpS1824 = _M0Lm17vmIsTrailingZerosS660;
        _if__result_2511 = !_M0L6_2atmpS1824;
      }
    } else {
      _if__result_2511 = 0;
    }
    if (_if__result_2511) {
      _M0L6_2atmpS1822 = 1;
    } else {
      int32_t _M0L6_2atmpS1823 = _M0Lm16lastRemovedDigitS681;
      _M0L6_2atmpS1822 = _M0L6_2atmpS1823 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1821 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1822);
    _M0L6_2atmpS1820 = *(uint64_t*)&_M0L6_2atmpS1821;
    _M0Lm6outputS682 = _M0L6_2atmpS1819 + _M0L6_2atmpS1820;
  } else {
    int32_t _M0Lm7roundUpS695 = 0;
    uint64_t _M0L6_2atmpS1847 = _M0Lm2vpS657;
    uint64_t _M0L8vpDiv100S696 = _M0L6_2atmpS1847 / 100ull;
    uint64_t _M0L6_2atmpS1846 = _M0Lm2vmS658;
    uint64_t _M0L8vmDiv100S697 = _M0L6_2atmpS1846 / 100ull;
    uint64_t _M0L6_2atmpS1841;
    uint64_t _M0L6_2atmpS1844;
    uint64_t _M0L6_2atmpS1845;
    int32_t _M0L6_2atmpS1843;
    uint64_t _M0L6_2atmpS1842;
    if (_M0L8vpDiv100S696 > _M0L8vmDiv100S697) {
      uint64_t _M0L6_2atmpS1832 = _M0Lm2vrS656;
      uint64_t _M0L8vrDiv100S698 = _M0L6_2atmpS1832 / 100ull;
      uint64_t _M0L6_2atmpS1831 = _M0Lm2vrS656;
      int32_t _M0L6_2atmpS1828 = (int32_t)_M0L6_2atmpS1831;
      int32_t _M0L6_2atmpS1830 = (int32_t)_M0L8vrDiv100S698;
      int32_t _M0L6_2atmpS1829 = 100 * _M0L6_2atmpS1830;
      int32_t _M0L8vrMod100S699 = _M0L6_2atmpS1828 - _M0L6_2atmpS1829;
      int32_t _M0L6_2atmpS1827;
      _M0Lm7roundUpS695 = _M0L8vrMod100S699 >= 50;
      _M0Lm2vrS656 = _M0L8vrDiv100S698;
      _M0Lm2vpS657 = _M0L8vpDiv100S696;
      _M0Lm2vmS658 = _M0L8vmDiv100S697;
      _M0L6_2atmpS1827 = _M0Lm7removedS680;
      _M0Lm7removedS680 = _M0L6_2atmpS1827 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1840 = _M0Lm2vpS657;
      uint64_t _M0L7vpDiv10S700 = _M0L6_2atmpS1840 / 10ull;
      uint64_t _M0L6_2atmpS1839 = _M0Lm2vmS658;
      uint64_t _M0L7vmDiv10S701 = _M0L6_2atmpS1839 / 10ull;
      uint64_t _M0L6_2atmpS1838;
      uint64_t _M0L7vrDiv10S703;
      uint64_t _M0L6_2atmpS1837;
      int32_t _M0L6_2atmpS1834;
      int32_t _M0L6_2atmpS1836;
      int32_t _M0L6_2atmpS1835;
      int32_t _M0L7vrMod10S704;
      int32_t _M0L6_2atmpS1833;
      if (_M0L7vpDiv10S700 <= _M0L7vmDiv10S701) {
        break;
      }
      _M0L6_2atmpS1838 = _M0Lm2vrS656;
      _M0L7vrDiv10S703 = _M0L6_2atmpS1838 / 10ull;
      _M0L6_2atmpS1837 = _M0Lm2vrS656;
      _M0L6_2atmpS1834 = (int32_t)_M0L6_2atmpS1837;
      _M0L6_2atmpS1836 = (int32_t)_M0L7vrDiv10S703;
      _M0L6_2atmpS1835 = 10 * _M0L6_2atmpS1836;
      _M0L7vrMod10S704 = _M0L6_2atmpS1834 - _M0L6_2atmpS1835;
      _M0Lm7roundUpS695 = _M0L7vrMod10S704 >= 5;
      _M0Lm2vrS656 = _M0L7vrDiv10S703;
      _M0Lm2vpS657 = _M0L7vpDiv10S700;
      _M0Lm2vmS658 = _M0L7vmDiv10S701;
      _M0L6_2atmpS1833 = _M0Lm7removedS680;
      _M0Lm7removedS680 = _M0L6_2atmpS1833 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1841 = _M0Lm2vrS656;
    _M0L6_2atmpS1844 = _M0Lm2vrS656;
    _M0L6_2atmpS1845 = _M0Lm2vmS658;
    _M0L6_2atmpS1843
    = _M0L6_2atmpS1844 == _M0L6_2atmpS1845 || _M0Lm7roundUpS695;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1842 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1843);
    _M0Lm6outputS682 = _M0L6_2atmpS1841 + _M0L6_2atmpS1842;
  }
  _M0L6_2atmpS1849 = _M0Lm3e10S659;
  _M0L6_2atmpS1850 = _M0Lm7removedS680;
  _M0L3expS705 = _M0L6_2atmpS1849 + _M0L6_2atmpS1850;
  _M0L6_2atmpS1848 = _M0Lm6outputS682;
  _block_2513
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2513)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2513->$0 = _M0L6_2atmpS1848;
  _block_2513->$1 = _M0L3expS705;
  return _block_2513;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS648) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS648) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS647) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS647) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS646) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS646) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS645) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS645 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS645 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS645 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS645 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS645 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS645 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS645 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS645 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS645 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS645 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS645 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS645 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS645 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS645 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS645 >= 100ull) {
    return 3;
  }
  if (_M0L1vS645 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS628) {
  int32_t _M0L6_2atmpS1749;
  int32_t _M0L6_2atmpS1748;
  int32_t _M0L4baseS627;
  int32_t _M0L5base2S629;
  int32_t _M0L6offsetS630;
  int32_t _M0L6_2atmpS1747;
  uint64_t _M0L4mul0S631;
  int32_t _M0L6_2atmpS1746;
  int32_t _M0L6_2atmpS1745;
  uint64_t _M0L4mul1S632;
  uint64_t _M0L1mS633;
  struct _M0TPB7Umul128 _M0L7_2abindS634;
  uint64_t _M0L7_2alow1S635;
  uint64_t _M0L8_2ahigh1S636;
  struct _M0TPB7Umul128 _M0L7_2abindS637;
  uint64_t _M0L7_2alow0S638;
  uint64_t _M0L8_2ahigh0S639;
  uint64_t _M0L3sumS640;
  uint64_t _M0Lm5high1S641;
  int32_t _M0L6_2atmpS1743;
  int32_t _M0L6_2atmpS1744;
  int32_t _M0L5deltaS642;
  uint64_t _M0L6_2atmpS1742;
  uint64_t _M0L6_2atmpS1734;
  int32_t _M0L6_2atmpS1741;
  uint32_t _M0L6_2atmpS1738;
  int32_t _M0L6_2atmpS1740;
  int32_t _M0L6_2atmpS1739;
  uint32_t _M0L6_2atmpS1737;
  uint32_t _M0L6_2atmpS1736;
  uint64_t _M0L6_2atmpS1735;
  uint64_t _M0L1aS643;
  uint64_t _M0L6_2atmpS1733;
  uint64_t _M0L1bS644;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1749 = _M0L1iS628 + 26;
  _M0L6_2atmpS1748 = _M0L6_2atmpS1749 - 1;
  _M0L4baseS627 = _M0L6_2atmpS1748 / 26;
  _M0L5base2S629 = _M0L4baseS627 * 26;
  _M0L6offsetS630 = _M0L5base2S629 - _M0L1iS628;
  _M0L6_2atmpS1747 = _M0L4baseS627 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S631
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1747);
  _M0L6_2atmpS1746 = _M0L4baseS627 * 2;
  _M0L6_2atmpS1745 = _M0L6_2atmpS1746 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S632
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1745);
  if (_M0L6offsetS630 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S631, .$1 = _M0L4mul1S632};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS633
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS630);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS634 = _M0FPB7umul128(_M0L1mS633, _M0L4mul1S632);
  _M0L7_2alow1S635 = _M0L7_2abindS634.$0;
  _M0L8_2ahigh1S636 = _M0L7_2abindS634.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS637 = _M0FPB7umul128(_M0L1mS633, _M0L4mul0S631);
  _M0L7_2alow0S638 = _M0L7_2abindS637.$0;
  _M0L8_2ahigh0S639 = _M0L7_2abindS637.$1;
  _M0L3sumS640 = _M0L8_2ahigh0S639 + _M0L7_2alow1S635;
  _M0Lm5high1S641 = _M0L8_2ahigh1S636;
  if (_M0L3sumS640 < _M0L8_2ahigh0S639) {
    uint64_t _M0L6_2atmpS1732 = _M0Lm5high1S641;
    _M0Lm5high1S641 = _M0L6_2atmpS1732 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1743 = _M0FPB8pow5bits(_M0L5base2S629);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1744 = _M0FPB8pow5bits(_M0L1iS628);
  _M0L5deltaS642 = _M0L6_2atmpS1743 - _M0L6_2atmpS1744;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1742
  = _M0FPB13shiftright128(_M0L7_2alow0S638, _M0L3sumS640, _M0L5deltaS642);
  _M0L6_2atmpS1734 = _M0L6_2atmpS1742 + 1ull;
  _M0L6_2atmpS1741 = _M0L1iS628 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1738
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1741);
  _M0L6_2atmpS1740 = _M0L1iS628 % 16;
  _M0L6_2atmpS1739 = _M0L6_2atmpS1740 << 1;
  _M0L6_2atmpS1737 = _M0L6_2atmpS1738 >> (_M0L6_2atmpS1739 & 31);
  _M0L6_2atmpS1736 = _M0L6_2atmpS1737 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1735 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1736);
  _M0L1aS643 = _M0L6_2atmpS1734 + _M0L6_2atmpS1735;
  _M0L6_2atmpS1733 = _M0Lm5high1S641;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS644
  = _M0FPB13shiftright128(_M0L3sumS640, _M0L6_2atmpS1733, _M0L5deltaS642);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS643, .$1 = _M0L1bS644};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS610) {
  int32_t _M0L4baseS609;
  int32_t _M0L5base2S611;
  int32_t _M0L6offsetS612;
  int32_t _M0L6_2atmpS1731;
  uint64_t _M0L4mul0S613;
  int32_t _M0L6_2atmpS1730;
  int32_t _M0L6_2atmpS1729;
  uint64_t _M0L4mul1S614;
  uint64_t _M0L1mS615;
  struct _M0TPB7Umul128 _M0L7_2abindS616;
  uint64_t _M0L7_2alow1S617;
  uint64_t _M0L8_2ahigh1S618;
  struct _M0TPB7Umul128 _M0L7_2abindS619;
  uint64_t _M0L7_2alow0S620;
  uint64_t _M0L8_2ahigh0S621;
  uint64_t _M0L3sumS622;
  uint64_t _M0Lm5high1S623;
  int32_t _M0L6_2atmpS1727;
  int32_t _M0L6_2atmpS1728;
  int32_t _M0L5deltaS624;
  uint64_t _M0L6_2atmpS1719;
  int32_t _M0L6_2atmpS1726;
  uint32_t _M0L6_2atmpS1723;
  int32_t _M0L6_2atmpS1725;
  int32_t _M0L6_2atmpS1724;
  uint32_t _M0L6_2atmpS1722;
  uint32_t _M0L6_2atmpS1721;
  uint64_t _M0L6_2atmpS1720;
  uint64_t _M0L1aS625;
  uint64_t _M0L6_2atmpS1718;
  uint64_t _M0L1bS626;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS609 = _M0L1iS610 / 26;
  _M0L5base2S611 = _M0L4baseS609 * 26;
  _M0L6offsetS612 = _M0L1iS610 - _M0L5base2S611;
  _M0L6_2atmpS1731 = _M0L4baseS609 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S613
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1731);
  _M0L6_2atmpS1730 = _M0L4baseS609 * 2;
  _M0L6_2atmpS1729 = _M0L6_2atmpS1730 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S614
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1729);
  if (_M0L6offsetS612 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S613, .$1 = _M0L4mul1S614};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS615
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS612);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS616 = _M0FPB7umul128(_M0L1mS615, _M0L4mul1S614);
  _M0L7_2alow1S617 = _M0L7_2abindS616.$0;
  _M0L8_2ahigh1S618 = _M0L7_2abindS616.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS619 = _M0FPB7umul128(_M0L1mS615, _M0L4mul0S613);
  _M0L7_2alow0S620 = _M0L7_2abindS619.$0;
  _M0L8_2ahigh0S621 = _M0L7_2abindS619.$1;
  _M0L3sumS622 = _M0L8_2ahigh0S621 + _M0L7_2alow1S617;
  _M0Lm5high1S623 = _M0L8_2ahigh1S618;
  if (_M0L3sumS622 < _M0L8_2ahigh0S621) {
    uint64_t _M0L6_2atmpS1717 = _M0Lm5high1S623;
    _M0Lm5high1S623 = _M0L6_2atmpS1717 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1727 = _M0FPB8pow5bits(_M0L1iS610);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1728 = _M0FPB8pow5bits(_M0L5base2S611);
  _M0L5deltaS624 = _M0L6_2atmpS1727 - _M0L6_2atmpS1728;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1719
  = _M0FPB13shiftright128(_M0L7_2alow0S620, _M0L3sumS622, _M0L5deltaS624);
  _M0L6_2atmpS1726 = _M0L1iS610 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1723
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1726);
  _M0L6_2atmpS1725 = _M0L1iS610 % 16;
  _M0L6_2atmpS1724 = _M0L6_2atmpS1725 << 1;
  _M0L6_2atmpS1722 = _M0L6_2atmpS1723 >> (_M0L6_2atmpS1724 & 31);
  _M0L6_2atmpS1721 = _M0L6_2atmpS1722 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1720 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1721);
  _M0L1aS625 = _M0L6_2atmpS1719 + _M0L6_2atmpS1720;
  _M0L6_2atmpS1718 = _M0Lm5high1S623;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS626
  = _M0FPB13shiftright128(_M0L3sumS622, _M0L6_2atmpS1718, _M0L5deltaS624);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS625, .$1 = _M0L1bS626};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS583,
  struct _M0TPB8Pow5Pair _M0L3mulS580,
  int32_t _M0L1jS596,
  int32_t _M0L7mmShiftS598
) {
  uint64_t _M0L7_2amul0S579;
  uint64_t _M0L7_2amul1S581;
  uint64_t _M0L1mS582;
  struct _M0TPB7Umul128 _M0L7_2abindS584;
  uint64_t _M0L5_2aloS585;
  uint64_t _M0L6_2atmpS586;
  struct _M0TPB7Umul128 _M0L7_2abindS587;
  uint64_t _M0L6_2alo2S588;
  uint64_t _M0L6_2ahi2S589;
  uint64_t _M0L3midS590;
  uint64_t _M0L6_2atmpS1716;
  uint64_t _M0L2hiS591;
  uint64_t _M0L3lo2S592;
  uint64_t _M0L6_2atmpS1714;
  uint64_t _M0L6_2atmpS1715;
  uint64_t _M0L4mid2S593;
  uint64_t _M0L6_2atmpS1713;
  uint64_t _M0L3hi2S594;
  int32_t _M0L6_2atmpS1712;
  int32_t _M0L6_2atmpS1711;
  uint64_t _M0L2vpS595;
  uint64_t _M0Lm2vmS597;
  int32_t _M0L6_2atmpS1710;
  int32_t _M0L6_2atmpS1709;
  uint64_t _M0L2vrS608;
  uint64_t _M0L6_2atmpS1708;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S579 = _M0L3mulS580.$0;
  _M0L7_2amul1S581 = _M0L3mulS580.$1;
  _M0L1mS582 = _M0L1mS583 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS584 = _M0FPB7umul128(_M0L1mS582, _M0L7_2amul0S579);
  _M0L5_2aloS585 = _M0L7_2abindS584.$0;
  _M0L6_2atmpS586 = _M0L7_2abindS584.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS587 = _M0FPB7umul128(_M0L1mS582, _M0L7_2amul1S581);
  _M0L6_2alo2S588 = _M0L7_2abindS587.$0;
  _M0L6_2ahi2S589 = _M0L7_2abindS587.$1;
  _M0L3midS590 = _M0L6_2atmpS586 + _M0L6_2alo2S588;
  if (_M0L3midS590 < _M0L6_2atmpS586) {
    _M0L6_2atmpS1716 = 1ull;
  } else {
    _M0L6_2atmpS1716 = 0ull;
  }
  _M0L2hiS591 = _M0L6_2ahi2S589 + _M0L6_2atmpS1716;
  _M0L3lo2S592 = _M0L5_2aloS585 + _M0L7_2amul0S579;
  _M0L6_2atmpS1714 = _M0L3midS590 + _M0L7_2amul1S581;
  if (_M0L3lo2S592 < _M0L5_2aloS585) {
    _M0L6_2atmpS1715 = 1ull;
  } else {
    _M0L6_2atmpS1715 = 0ull;
  }
  _M0L4mid2S593 = _M0L6_2atmpS1714 + _M0L6_2atmpS1715;
  if (_M0L4mid2S593 < _M0L3midS590) {
    _M0L6_2atmpS1713 = 1ull;
  } else {
    _M0L6_2atmpS1713 = 0ull;
  }
  _M0L3hi2S594 = _M0L2hiS591 + _M0L6_2atmpS1713;
  _M0L6_2atmpS1712 = _M0L1jS596 - 64;
  _M0L6_2atmpS1711 = _M0L6_2atmpS1712 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS595
  = _M0FPB13shiftright128(_M0L4mid2S593, _M0L3hi2S594, _M0L6_2atmpS1711);
  _M0Lm2vmS597 = 0ull;
  if (_M0L7mmShiftS598) {
    uint64_t _M0L3lo3S599 = _M0L5_2aloS585 - _M0L7_2amul0S579;
    uint64_t _M0L6_2atmpS1698 = _M0L3midS590 - _M0L7_2amul1S581;
    uint64_t _M0L6_2atmpS1699;
    uint64_t _M0L4mid3S600;
    uint64_t _M0L6_2atmpS1697;
    uint64_t _M0L3hi3S601;
    int32_t _M0L6_2atmpS1696;
    int32_t _M0L6_2atmpS1695;
    if (_M0L5_2aloS585 < _M0L3lo3S599) {
      _M0L6_2atmpS1699 = 1ull;
    } else {
      _M0L6_2atmpS1699 = 0ull;
    }
    _M0L4mid3S600 = _M0L6_2atmpS1698 - _M0L6_2atmpS1699;
    if (_M0L3midS590 < _M0L4mid3S600) {
      _M0L6_2atmpS1697 = 1ull;
    } else {
      _M0L6_2atmpS1697 = 0ull;
    }
    _M0L3hi3S601 = _M0L2hiS591 - _M0L6_2atmpS1697;
    _M0L6_2atmpS1696 = _M0L1jS596 - 64;
    _M0L6_2atmpS1695 = _M0L6_2atmpS1696 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS597
    = _M0FPB13shiftright128(_M0L4mid3S600, _M0L3hi3S601, _M0L6_2atmpS1695);
  } else {
    uint64_t _M0L3lo3S602 = _M0L5_2aloS585 + _M0L5_2aloS585;
    uint64_t _M0L6_2atmpS1706 = _M0L3midS590 + _M0L3midS590;
    uint64_t _M0L6_2atmpS1707;
    uint64_t _M0L4mid3S603;
    uint64_t _M0L6_2atmpS1704;
    uint64_t _M0L6_2atmpS1705;
    uint64_t _M0L3hi3S604;
    uint64_t _M0L3lo4S605;
    uint64_t _M0L6_2atmpS1702;
    uint64_t _M0L6_2atmpS1703;
    uint64_t _M0L4mid4S606;
    uint64_t _M0L6_2atmpS1701;
    uint64_t _M0L3hi4S607;
    int32_t _M0L6_2atmpS1700;
    if (_M0L3lo3S602 < _M0L5_2aloS585) {
      _M0L6_2atmpS1707 = 1ull;
    } else {
      _M0L6_2atmpS1707 = 0ull;
    }
    _M0L4mid3S603 = _M0L6_2atmpS1706 + _M0L6_2atmpS1707;
    _M0L6_2atmpS1704 = _M0L2hiS591 + _M0L2hiS591;
    if (_M0L4mid3S603 < _M0L3midS590) {
      _M0L6_2atmpS1705 = 1ull;
    } else {
      _M0L6_2atmpS1705 = 0ull;
    }
    _M0L3hi3S604 = _M0L6_2atmpS1704 + _M0L6_2atmpS1705;
    _M0L3lo4S605 = _M0L3lo3S602 - _M0L7_2amul0S579;
    _M0L6_2atmpS1702 = _M0L4mid3S603 - _M0L7_2amul1S581;
    if (_M0L3lo3S602 < _M0L3lo4S605) {
      _M0L6_2atmpS1703 = 1ull;
    } else {
      _M0L6_2atmpS1703 = 0ull;
    }
    _M0L4mid4S606 = _M0L6_2atmpS1702 - _M0L6_2atmpS1703;
    if (_M0L4mid3S603 < _M0L4mid4S606) {
      _M0L6_2atmpS1701 = 1ull;
    } else {
      _M0L6_2atmpS1701 = 0ull;
    }
    _M0L3hi4S607 = _M0L3hi3S604 - _M0L6_2atmpS1701;
    _M0L6_2atmpS1700 = _M0L1jS596 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS597
    = _M0FPB13shiftright128(_M0L4mid4S606, _M0L3hi4S607, _M0L6_2atmpS1700);
  }
  _M0L6_2atmpS1710 = _M0L1jS596 - 64;
  _M0L6_2atmpS1709 = _M0L6_2atmpS1710 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS608
  = _M0FPB13shiftright128(_M0L3midS590, _M0L2hiS591, _M0L6_2atmpS1709);
  _M0L6_2atmpS1708 = _M0Lm2vmS597;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS608,
                                                .$1 = _M0L2vpS595,
                                                .$2 = _M0L6_2atmpS1708};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS577,
  int32_t _M0L1pS578
) {
  uint64_t _M0L6_2atmpS1694;
  uint64_t _M0L6_2atmpS1693;
  uint64_t _M0L6_2atmpS1692;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1694 = 1ull << (_M0L1pS578 & 63);
  _M0L6_2atmpS1693 = _M0L6_2atmpS1694 - 1ull;
  _M0L6_2atmpS1692 = _M0L5valueS577 & _M0L6_2atmpS1693;
  return _M0L6_2atmpS1692 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS575,
  int32_t _M0L1pS576
) {
  int32_t _M0L6_2atmpS1691;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1691 = _M0FPB10pow5Factor(_M0L5valueS575);
  return _M0L6_2atmpS1691 >= _M0L1pS576;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS570) {
  uint64_t _M0L6_2atmpS1682;
  uint64_t _M0L6_2atmpS1683;
  uint64_t _M0L6_2atmpS1684;
  uint64_t _M0L6_2atmpS1685;
  uint64_t _M0L6_2atmpS1690;
  int32_t _M0L5countS571;
  uint64_t _M0L1vS572;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1682 = _M0L5valueS570 % 5ull;
  if (_M0L6_2atmpS1682 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1683 = _M0L5valueS570 % 25ull;
  if (_M0L6_2atmpS1683 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1684 = _M0L5valueS570 % 125ull;
  if (_M0L6_2atmpS1684 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1685 = _M0L5valueS570 % 625ull;
  if (_M0L6_2atmpS1685 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1690 = _M0L5valueS570 / 625ull;
  _M0L5countS571 = 4;
  _M0L1vS572 = _M0L6_2atmpS1690;
  while (1) {
    if (_M0L1vS572 > 0ull) {
      uint64_t _M0L6_2atmpS1686 = _M0L1vS572 % 5ull;
      int32_t _M0L6_2atmpS1687;
      uint64_t _M0L6_2atmpS1688;
      if (_M0L6_2atmpS1686 != 0ull) {
        return _M0L5countS571;
      }
      _M0L6_2atmpS1687 = _M0L5countS571 + 1;
      _M0L6_2atmpS1688 = _M0L1vS572 / 5ull;
      _M0L5countS571 = _M0L6_2atmpS1687;
      _M0L1vS572 = _M0L6_2atmpS1688;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS574;
      moonbit_string_t _M0L6_2atmpS1689;
      int32_t _result_2515;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS574
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS574, (moonbit_string_t)moonbit_string_literal_19.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS574, _M0L5valueS570);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1689
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS574);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS574);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2515 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1689);
      moonbit_decref_cycle_free(_M0L6_2atmpS1689);
      return _result_2515;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS569,
  uint64_t _M0L2hiS567,
  int32_t _M0L4distS568
) {
  int32_t _M0L6_2atmpS1681;
  uint64_t _M0L6_2atmpS1679;
  uint64_t _M0L6_2atmpS1680;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1681 = 64 - _M0L4distS568;
  _M0L6_2atmpS1679 = _M0L2hiS567 << (_M0L6_2atmpS1681 & 63);
  _M0L6_2atmpS1680 = _M0L2loS569 >> (_M0L4distS568 & 63);
  return _M0L6_2atmpS1679 | _M0L6_2atmpS1680;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS557,
  uint64_t _M0L1bS560
) {
  uint64_t _M0L3aLoS556;
  uint64_t _M0L3aHiS558;
  uint64_t _M0L3bLoS559;
  uint64_t _M0L3bHiS561;
  uint64_t _M0L1xS562;
  uint64_t _M0L6_2atmpS1677;
  uint64_t _M0L6_2atmpS1678;
  uint64_t _M0L1yS563;
  uint64_t _M0L6_2atmpS1675;
  uint64_t _M0L6_2atmpS1676;
  uint64_t _M0L1zS564;
  uint64_t _M0L6_2atmpS1673;
  uint64_t _M0L6_2atmpS1674;
  uint64_t _M0L6_2atmpS1671;
  uint64_t _M0L6_2atmpS1672;
  uint64_t _M0L1wS565;
  uint64_t _M0L2loS566;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS556 = _M0L1aS557 & 4294967295ull;
  _M0L3aHiS558 = _M0L1aS557 >> 32;
  _M0L3bLoS559 = _M0L1bS560 & 4294967295ull;
  _M0L3bHiS561 = _M0L1bS560 >> 32;
  _M0L1xS562 = _M0L3aLoS556 * _M0L3bLoS559;
  _M0L6_2atmpS1677 = _M0L3aHiS558 * _M0L3bLoS559;
  _M0L6_2atmpS1678 = _M0L1xS562 >> 32;
  _M0L1yS563 = _M0L6_2atmpS1677 + _M0L6_2atmpS1678;
  _M0L6_2atmpS1675 = _M0L3aLoS556 * _M0L3bHiS561;
  _M0L6_2atmpS1676 = _M0L1yS563 & 4294967295ull;
  _M0L1zS564 = _M0L6_2atmpS1675 + _M0L6_2atmpS1676;
  _M0L6_2atmpS1673 = _M0L3aHiS558 * _M0L3bHiS561;
  _M0L6_2atmpS1674 = _M0L1yS563 >> 32;
  _M0L6_2atmpS1671 = _M0L6_2atmpS1673 + _M0L6_2atmpS1674;
  _M0L6_2atmpS1672 = _M0L1zS564 >> 32;
  _M0L1wS565 = _M0L6_2atmpS1671 + _M0L6_2atmpS1672;
  _M0L2loS566 = _M0L1aS557 * _M0L1bS560;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS566, .$1 = _M0L1wS565};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS554,
  int32_t _M0L4fromS551,
  int32_t _M0L2toS550
) {
  int32_t _M0L3lenS549;
  int32_t _M0L6_2atmpS1670;
  uint16_t* _M0L6bufferS552;
  int32_t _M0L1iS553;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS549 = _M0L2toS550 - _M0L4fromS551;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1670 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS552
  = (uint16_t*)moonbit_make_string(_M0L3lenS549, _M0L6_2atmpS1670);
  _M0L1iS553 = 0;
  while (1) {
    if (_M0L1iS553 < _M0L3lenS549) {
      int32_t _M0L6_2atmpS1668 = _M0L4fromS551 + _M0L1iS553;
      int32_t _M0L6_2atmpS1667;
      int32_t _M0L6_2atmpS1666;
      int32_t _M0L6_2atmpS1669;
      if (
        _M0L6_2atmpS1668 < 0
        || _M0L6_2atmpS1668 >= Moonbit_array_length(_M0L5bytesS554)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1667 = (int32_t)_M0L5bytesS554[_M0L6_2atmpS1668];
      _M0L6_2atmpS1666 = (uint16_t)_M0L6_2atmpS1667;
      if (
        _M0L1iS553 < 0 || _M0L1iS553 >= Moonbit_array_length(_M0L6bufferS552)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS552[_M0L1iS553] = _M0L6_2atmpS1666;
      _M0L6_2atmpS1669 = _M0L1iS553 + 1;
      _M0L1iS553 = _M0L6_2atmpS1669;
      continue;
    }
    break;
  }
  return _M0L6bufferS552;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS548) {
  int32_t _M0L6_2atmpS1665;
  uint32_t _M0L6_2atmpS1664;
  uint32_t _M0L6_2atmpS1663;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1665 = _M0L1eS548 * 78913;
  _M0L6_2atmpS1664 = *(uint32_t*)&_M0L6_2atmpS1665;
  _M0L6_2atmpS1663 = _M0L6_2atmpS1664 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1663;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS547) {
  int32_t _M0L6_2atmpS1662;
  uint32_t _M0L6_2atmpS1661;
  uint32_t _M0L6_2atmpS1660;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1662 = _M0L1eS547 * 732923;
  _M0L6_2atmpS1661 = *(uint32_t*)&_M0L6_2atmpS1662;
  _M0L6_2atmpS1660 = _M0L6_2atmpS1661 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1660;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS545,
  int32_t _M0L8exponentS546,
  int32_t _M0L8mantissaS543
) {
  moonbit_string_t _M0L1sS544;
  moonbit_string_t _result_2518;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS543) {
    return (moonbit_string_t)moonbit_string_literal_20.data;
  }
  if (_M0L4signS545) {
    _M0L1sS544 = (moonbit_string_t)moonbit_string_literal_21.data;
  } else {
    _M0L1sS544 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS546) {
    moonbit_string_t _result_2517;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2517
    = moonbit_add_string(_M0L1sS544, (moonbit_string_t)moonbit_string_literal_22.data);
    moonbit_decref_cycle_free(_M0L1sS544);
    return _result_2517;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2518
  = moonbit_add_string(_M0L1sS544, (moonbit_string_t)moonbit_string_literal_23.data);
  moonbit_decref_cycle_free(_M0L1sS544);
  return _result_2518;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS542) {
  int32_t _M0L6_2atmpS1659;
  uint32_t _M0L6_2atmpS1658;
  uint32_t _M0L6_2atmpS1657;
  int32_t _M0L6_2atmpS1656;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1659 = _M0L1eS542 * 1217359;
  _M0L6_2atmpS1658 = *(uint32_t*)&_M0L6_2atmpS1659;
  _M0L6_2atmpS1657 = _M0L6_2atmpS1658 >> 19;
  _M0L6_2atmpS1656 = *(int32_t*)&_M0L6_2atmpS1657;
  return _M0L6_2atmpS1656 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS541) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS541 != _M0L4selfS541) {
    return 0;
  } else if (_M0L4selfS541 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS541 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS541;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS540) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS540 != _M0L4selfS540) {
    return 0ll;
  } else if (_M0L4selfS540 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS540 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS540;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS536
) {
  float* _M0L6_2atmpS1652;
  struct _M0TPB5ArrayGfE* _block_2519;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1652 = (float*)moonbit_make_float_array_raw(_M0L3lenS536);
  _block_2519
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2519)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _block_2519->$0 = _M0L6_2atmpS1652;
  _block_2519->$1 = _M0L3lenS536;
  return _block_2519;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS537
) {
  uint8_t* _M0L6_2atmpS1653;
  struct _M0TPB5ArrayGbE* _block_2520;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1653 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS537);
  _block_2520
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2520)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 54, 0);
  _block_2520->$0 = _M0L6_2atmpS1653;
  _block_2520->$1 = _M0L3lenS537;
  return _block_2520;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS538
) {
  int32_t* _M0L6_2atmpS1654;
  struct _M0TPB5ArrayGiE* _block_2521;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1654 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS538);
  _block_2521
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2521)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 45, 0);
  _block_2521->$0 = _M0L6_2atmpS1654;
  _block_2521->$1 = _M0L3lenS538;
  return _block_2521;
}

struct _M0TPB5ArrayGsE* _M0MPC15array5Array20unsafe__make__uninitGsE(
  int32_t _M0L3lenS539
) {
  moonbit_string_t* _M0L6_2atmpS1655;
  struct _M0TPB5ArrayGsE* _block_2522;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1655
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L3lenS539, (moonbit_string_t)moonbit_string_literal_0.data);
  _block_2522
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_block_2522)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _block_2522->$0 = _M0L6_2atmpS1655;
  _block_2522->$1 = _M0L3lenS539;
  return _block_2522;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS532,
  int32_t _M0L5indexS533
) {
  uint64_t* _M0L6_2atmpS1650;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1650 = _M0L4selfS532;
  if (
    _M0L5indexS533 < 0
    || _M0L5indexS533 >= Moonbit_array_length(_M0L6_2atmpS1650)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1650[_M0L5indexS533];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS534,
  int32_t _M0L5indexS535
) {
  uint32_t* _M0L6_2atmpS1651;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1651 = _M0L4selfS534;
  if (
    _M0L5indexS535 < 0
    || _M0L5indexS535 >= Moonbit_array_length(_M0L6_2atmpS1651)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1651[_M0L5indexS535];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS531
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS531, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS530) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS530, 10);
}

moonbit_string_t _M0IPC14bool4BoolPB4Show10to__string(int32_t _M0L4selfS529) {
  #line 26 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L4selfS529) {
    return (moonbit_string_t)moonbit_string_literal_24.data;
  } else {
    return (moonbit_string_t)moonbit_string_literal_25.data;
  }
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS528) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS528;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS516,
  int32_t _M0L5valueS518
) {
  int32_t _M0L3lenS1622;
  int32_t* _M0L6_2atmpS1624;
  int32_t _M0L6_2atmpS1623;
  int32_t _M0L6lengthS517;
  int32_t* _M0L3bufS1627;
  int32_t _M0L6_2atmpS1628;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1622 = _M0L4selfS516->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1624 = _M0MPC15array5Array6bufferGiE(_M0L4selfS516);
  _M0L6_2atmpS1623 = Moonbit_array_length(_M0L6_2atmpS1624);
  moonbit_decref_cycle_free(_M0L6_2atmpS1624);
  if (_M0L3lenS1622 == _M0L6_2atmpS1623) {
    int32_t _M0L3lenS1626 = _M0L4selfS516->$1;
    int32_t _M0L6_2atmpS1625 = _M0L3lenS1626 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS516, _M0L6_2atmpS1625);
  }
  _M0L6lengthS517 = _M0L4selfS516->$1;
  _M0L3bufS1627 = _M0L4selfS516->$0;
  _M0L3bufS1627[_M0L6lengthS517] = _M0L5valueS518;
  _M0L6_2atmpS1628 = _M0L6lengthS517 + 1;
  _M0L4selfS516->$1 = _M0L6_2atmpS1628;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS519,
  float _M0L5valueS521
) {
  int32_t _M0L3lenS1629;
  float* _M0L6_2atmpS1631;
  int32_t _M0L6_2atmpS1630;
  int32_t _M0L6lengthS520;
  float* _M0L3bufS1634;
  int32_t _M0L6_2atmpS1635;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1629 = _M0L4selfS519->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1631 = _M0MPC15array5Array6bufferGfE(_M0L4selfS519);
  _M0L6_2atmpS1630 = Moonbit_array_length(_M0L6_2atmpS1631);
  moonbit_decref_cycle_free(_M0L6_2atmpS1631);
  if (_M0L3lenS1629 == _M0L6_2atmpS1630) {
    int32_t _M0L3lenS1633 = _M0L4selfS519->$1;
    int32_t _M0L6_2atmpS1632 = _M0L3lenS1633 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS519, _M0L6_2atmpS1632);
  }
  _M0L6lengthS520 = _M0L4selfS519->$1;
  _M0L3bufS1634 = _M0L4selfS519->$0;
  _M0L3bufS1634[_M0L6lengthS520] = _M0L5valueS521;
  _M0L6_2atmpS1635 = _M0L6lengthS520 + 1;
  _M0L4selfS519->$1 = _M0L6_2atmpS1635;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS522,
  moonbit_string_t _M0L5valueS524
) {
  int32_t _M0L3lenS1636;
  moonbit_string_t* _M0L6_2atmpS1638;
  int32_t _M0L6_2atmpS1637;
  int32_t _M0L6lengthS523;
  moonbit_string_t* _M0L3bufS1641;
  moonbit_string_t _M0L6_2aoldS2413;
  int32_t _M0L6_2atmpS1642;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1636 = _M0L4selfS522->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1638 = _M0MPC15array5Array6bufferGsE(_M0L4selfS522);
  _M0L6_2atmpS1637 = Moonbit_array_length(_M0L6_2atmpS1638);
  moonbit_decref_cycle_free(_M0L6_2atmpS1638);
  if (_M0L3lenS1636 == _M0L6_2atmpS1637) {
    int32_t _M0L3lenS1640 = _M0L4selfS522->$1;
    int32_t _M0L6_2atmpS1639 = _M0L3lenS1640 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS522, _M0L6_2atmpS1639);
  }
  _M0L6lengthS523 = _M0L4selfS522->$1;
  _M0L3bufS1641 = _M0L4selfS522->$0;
  _M0L6_2aoldS2413 = (moonbit_string_t)_M0L3bufS1641[_M0L6lengthS523];
  moonbit_decref_cycle_free(_M0L6_2aoldS2413);
  _M0L3bufS1641[_M0L6lengthS523] = _M0L5valueS524;
  _M0L6_2atmpS1642 = _M0L6lengthS523 + 1;
  _M0L4selfS522->$1 = _M0L6_2atmpS1642;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS525,
  struct _M0TUsiE* _M0L5valueS527
) {
  int32_t _M0L3lenS1643;
  struct _M0TUsiE** _M0L6_2atmpS1645;
  int32_t _M0L6_2atmpS1644;
  int32_t _M0L6lengthS526;
  struct _M0TUsiE** _M0L3bufS1648;
  struct _M0TUsiE* _M0L6_2aoldS2414;
  int32_t _M0L6_2atmpS1649;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1643 = _M0L4selfS525->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1645 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS525);
  _M0L6_2atmpS1644 = Moonbit_array_length(_M0L6_2atmpS1645);
  moonbit_decref_cycle_free(_M0L6_2atmpS1645);
  if (_M0L3lenS1643 == _M0L6_2atmpS1644) {
    int32_t _M0L3lenS1647 = _M0L4selfS525->$1;
    int32_t _M0L6_2atmpS1646 = _M0L3lenS1647 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS525, _M0L6_2atmpS1646);
  }
  _M0L6lengthS526 = _M0L4selfS525->$1;
  _M0L3bufS1648 = _M0L4selfS525->$0;
  _M0L6_2aoldS2414 = (struct _M0TUsiE*)_M0L3bufS1648[_M0L6lengthS526];
  if (_M0L6_2aoldS2414) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2414);
  }
  _M0L3bufS1648[_M0L6lengthS526] = _M0L5valueS527;
  _M0L6_2atmpS1649 = _M0L6lengthS526 + 1;
  _M0L4selfS525->$1 = _M0L6_2atmpS1649;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS501,
  int32_t _M0L8requiredS503
) {
  int32_t _M0L8old__capS500;
  int32_t _M0L3lenS1618;
  int32_t _M0L8new__capS502;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS500 = _M0MPC15array5Array8capacityGiE(_M0L4selfS501);
  _M0L3lenS1618 = _M0L4selfS501->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS502
  = _M0FPB23array__growth__capacity(_M0L8old__capS500, _M0L3lenS1618, _M0L8requiredS503);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS501, _M0L8new__capS502);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS505,
  int32_t _M0L8requiredS507
) {
  int32_t _M0L8old__capS504;
  int32_t _M0L3lenS1619;
  int32_t _M0L8new__capS506;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS504 = _M0MPC15array5Array8capacityGfE(_M0L4selfS505);
  _M0L3lenS1619 = _M0L4selfS505->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS506
  = _M0FPB23array__growth__capacity(_M0L8old__capS504, _M0L3lenS1619, _M0L8requiredS507);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS505, _M0L8new__capS506);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS509,
  int32_t _M0L8requiredS511
) {
  int32_t _M0L8old__capS508;
  int32_t _M0L3lenS1620;
  int32_t _M0L8new__capS510;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS508 = _M0MPC15array5Array8capacityGsE(_M0L4selfS509);
  _M0L3lenS1620 = _M0L4selfS509->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS510
  = _M0FPB23array__growth__capacity(_M0L8old__capS508, _M0L3lenS1620, _M0L8requiredS511);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS509, _M0L8new__capS510);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS513,
  int32_t _M0L8requiredS515
) {
  int32_t _M0L8old__capS512;
  int32_t _M0L3lenS1621;
  int32_t _M0L8new__capS514;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS512 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS513);
  _M0L3lenS1621 = _M0L4selfS513->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS514
  = _M0FPB23array__growth__capacity(_M0L8old__capS512, _M0L3lenS1621, _M0L8requiredS515);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS513, _M0L8new__capS514);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS477,
  int32_t _M0L13new__capacityS480
) {
  int32_t* _M0L8old__bufS476;
  int32_t _M0L3lenS478;
  int32_t _M0L9copy__lenS479;
  int32_t* _M0L8new__bufS481;
  int32_t* _M0L6_2aoldS2415;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS476, _M0L13new__capacityS480, _M0L9copy__lenS479, 0, 0);
  _M0L6_2aoldS2415 = _M0L4selfS477->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2415);
  _M0L4selfS477->$0 = _M0L8new__bufS481;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS483,
  int32_t _M0L13new__capacityS486
) {
  float* _M0L8old__bufS482;
  int32_t _M0L3lenS484;
  int32_t _M0L9copy__lenS485;
  float* _M0L8new__bufS487;
  float* _M0L6_2aoldS2416;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS482, _M0L13new__capacityS486, _M0L9copy__lenS485, 0, 0);
  _M0L6_2aoldS2416 = _M0L4selfS483->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2416);
  _M0L4selfS483->$0 = _M0L8new__bufS487;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS489,
  int32_t _M0L13new__capacityS492
) {
  moonbit_string_t* _M0L8old__bufS488;
  int32_t _M0L3lenS490;
  int32_t _M0L9copy__lenS491;
  moonbit_string_t* _M0L8new__bufS493;
  moonbit_string_t* _M0L6_2aoldS2417;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS488, _M0L13new__capacityS492, _M0L9copy__lenS491, 0, 0);
  _M0L6_2aoldS2417 = _M0L4selfS489->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2417);
  _M0L4selfS489->$0 = _M0L8new__bufS493;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS495,
  int32_t _M0L13new__capacityS498
) {
  struct _M0TUsiE** _M0L8old__bufS494;
  int32_t _M0L3lenS496;
  int32_t _M0L9copy__lenS497;
  struct _M0TUsiE** _M0L8new__bufS499;
  struct _M0TUsiE** _M0L6_2aoldS2418;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS494 = _M0L4selfS495->$0;
  _M0L3lenS496 = _M0L4selfS495->$1;
  if (_M0L3lenS496 < _M0L13new__capacityS498) {
    _M0L9copy__lenS497 = _M0L3lenS496;
  } else {
    _M0L9copy__lenS497 = _M0L13new__capacityS498;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS494);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS499
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS494, _M0L13new__capacityS498, _M0L9copy__lenS497, 0, 0);
  _M0L6_2aoldS2418 = _M0L4selfS495->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2418);
  _M0L4selfS495->$0 = _M0L8new__bufS499;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS472
) {
  int32_t* _M0L6_2atmpS1614;
  int32_t _result_2523;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1614 = _M0MPC15array5Array6bufferGiE(_M0L4selfS472);
  _result_2523 = Moonbit_array_length(_M0L6_2atmpS1614);
  moonbit_decref_cycle_free(_M0L6_2atmpS1614);
  return _result_2523;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS473
) {
  float* _M0L6_2atmpS1615;
  int32_t _result_2524;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1615 = _M0MPC15array5Array6bufferGfE(_M0L4selfS473);
  _result_2524 = Moonbit_array_length(_M0L6_2atmpS1615);
  moonbit_decref_cycle_free(_M0L6_2atmpS1615);
  return _result_2524;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS474
) {
  moonbit_string_t* _M0L6_2atmpS1616;
  int32_t _result_2525;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1616 = _M0MPC15array5Array6bufferGsE(_M0L4selfS474);
  _result_2525 = Moonbit_array_length(_M0L6_2atmpS1616);
  moonbit_decref_cycle_free(_M0L6_2atmpS1616);
  return _result_2525;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS475
) {
  struct _M0TUsiE** _M0L6_2atmpS1617;
  int32_t _result_2526;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1617 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS475);
  _result_2526 = Moonbit_array_length(_M0L6_2atmpS1617);
  moonbit_decref_cycle_free(_M0L6_2atmpS1617);
  return _result_2526;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS468,
  int32_t _M0L3lenS466,
  int32_t _M0L8requiredS465
) {
  int32_t _M0L5startS467;
  int32_t _M0L5spaceS469;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS465 < _M0L3lenS466) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_26.data);
  }
  if (_M0L7currentS468 == 0) {
    _M0L5startS467 = 8;
  } else {
    _M0L5startS467 = _M0L7currentS468;
  }
  _M0L5spaceS469 = _M0L5startS467;
  while (1) {
    if (_M0L5spaceS469 < _M0L8requiredS465) {
      int32_t _M0L4nextS470 = _M0L5spaceS469 * 2;
      if (_M0L4nextS470 <= _M0L5spaceS469) {
        return _M0L8requiredS465;
      }
      _M0L5spaceS469 = _M0L4nextS470;
      continue;
    } else {
      return _M0L5spaceS469;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS464) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS464->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS458) {
  float* _M0L8_2afieldS2419;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2419 = _M0L4selfS458->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2419);
  return _M0L8_2afieldS2419;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS459) {
  uint8_t* _M0L8_2afieldS2420;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2420 = _M0L4selfS459->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2420);
  return _M0L8_2afieldS2420;
}

struct _M0TP26RiantR8snn__mbt7Monitor** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt7MonitorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L4selfS460
) {
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L8_2afieldS2421;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2421 = _M0L4selfS460->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2421);
  return _M0L8_2afieldS2421;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS461) {
  int32_t* _M0L8_2afieldS2422;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2422 = _M0L4selfS461->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2422);
  return _M0L8_2afieldS2422;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS462
) {
  moonbit_string_t* _M0L8_2afieldS2423;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2423 = _M0L4selfS462->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2423);
  return _M0L8_2afieldS2423;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS463
) {
  struct _M0TUsiE** _M0L8_2afieldS2424;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2424 = _M0L4selfS463->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2424);
  return _M0L8_2afieldS2424;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS457
) {
  #line 220 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref_cycle_free(_M0L4selfS457);
  return _M0L4selfS457;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS456,
  struct _M0TPC16string10StringView _M0L3strS454
) {
  int32_t _M0L3endS1612;
  int32_t _M0L5startS1613;
  int32_t _M0L8str__lenS453;
  int32_t _M0L3lenS1611;
  int32_t _M0L8requiredS455;
  uint16_t* _M0L4dataS1604;
  int32_t _M0L6_2atmpS1603;
  int32_t _if__result_2528;
  uint16_t* _M0L4dataS1605;
  int32_t _M0L3lenS1606;
  moonbit_string_t _M0L6_2atmpS1607;
  int32_t _M0L6_2atmpS1608;
  int32_t _M0L3lenS1610;
  int32_t _M0L6_2atmpS1609;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1612 = _M0L3strS454.$2;
  _M0L5startS1613 = _M0L3strS454.$1;
  _M0L8str__lenS453 = _M0L3endS1612 - _M0L5startS1613;
  if (_M0L8str__lenS453 == 0) {
    return 0;
  }
  _M0L3lenS1611 = _M0L4selfS456->$1;
  _M0L8requiredS455 = _M0L3lenS1611 + _M0L8str__lenS453;
  _M0L4dataS1604 = _M0L4selfS456->$0;
  _M0L6_2atmpS1603 = Moonbit_array_length(_M0L4dataS1604);
  if (_M0L8requiredS455 > _M0L6_2atmpS1603) {
    _if__result_2528 = 1;
  } else {
    int32_t _M0L3lenS1602 = _M0L4selfS456->$1;
    _if__result_2528 = _M0L8requiredS455 < _M0L3lenS1602;
  }
  if (_if__result_2528) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS456, _M0L8requiredS455);
  }
  _M0L4dataS1605 = _M0L4selfS456->$0;
  _M0L3lenS1606 = _M0L4selfS456->$1;
  moonbit_incref_cycle_free(_M0L4dataS1605);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1607 = _M0MPC16string10StringView4data(_M0L3strS454);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1608 = _M0MPC16string10StringView13start__offset(_M0L3strS454);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1605, _M0L3lenS1606, _M0L6_2atmpS1607, _M0L6_2atmpS1608, _M0L8str__lenS453);
  moonbit_decref_cycle_free(_M0L4dataS1605);
  moonbit_decref_cycle_free(_M0L6_2atmpS1607);
  _M0L3lenS1610 = _M0L4selfS456->$1;
  _M0L6_2atmpS1609 = _M0L3lenS1610 + _M0L8str__lenS453;
  _M0L4selfS456->$1 = _M0L6_2atmpS1609;
  return 0;
}

moonbit_string_t _M0MPC16string6String4make(
  int32_t _M0L6lengthS448,
  int32_t _M0L5valueS449
) {
  #line 26 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L6lengthS448 >= 0) {
    int32_t _M0L6_2atmpS1599 = _M0L5valueS449;
    if (_M0L6_2atmpS1599 <= 65535) {
      #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
      return _M0FPB20unsafe__make__string(_M0L6lengthS448, _M0L5valueS449);
    } else {
      int32_t _M0L6_2atmpS1601 = 2 * _M0L6lengthS448;
      struct _M0TPB13StringBuilder* _M0L3bufS450;
      int32_t _M0L2__S451;
      moonbit_string_t _result_2530;
      #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
      _M0L3bufS450
      = _M0MPB13StringBuilder21StringBuilder_2einner(_M0L6_2atmpS1601);
      _M0L2__S451 = 0;
      while (1) {
        if (_M0L2__S451 < _M0L6lengthS448) {
          int32_t _M0L6_2atmpS1600;
          #line 33 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
          _M0IPB13StringBuilderPB6Logger11write__char(_M0L3bufS450, _M0L5valueS449);
          _M0L6_2atmpS1600 = _M0L2__S451 + 1;
          _M0L2__S451 = _M0L6_2atmpS1600;
          continue;
        }
        break;
      }
      #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
      _result_2530 = _M0MPB13StringBuilder10to__string(_M0L3bufS450);
      moonbit_decref_cycle_free(_M0L3bufS450);
      return _result_2530;
    }
  } else {
    #line 27 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
    return _M0FPC15abort5abortGsE((moonbit_string_t)moonbit_string_literal_27.data);
  }
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS445,
  int32_t _M0L5startS443,
  int32_t _M0L3endS444
) {
  int32_t _if__result_2531;
  int32_t _M0L3lenS446;
  int32_t _M0L6_2atmpS1598;
  moonbit_bytes_t _M0L5bytesS447;
  moonbit_bytes_t _M0L6_2atmpS1597;
  moonbit_string_t _result_2532;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS443 == 0) {
    int32_t _M0L6_2atmpS1596 = Moonbit_array_length(_M0L3strS445);
    _if__result_2531 = _M0L3endS444 == _M0L6_2atmpS1596;
  } else {
    _if__result_2531 = 0;
  }
  if (_if__result_2531) {
    moonbit_incref_cycle_free(_M0L3strS445);
    return _M0L3strS445;
  }
  _M0L3lenS446 = _M0L3endS444 - _M0L5startS443;
  _M0L6_2atmpS1598 = _M0L3lenS446 * 2;
  _M0L5bytesS447 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1598, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS447, 0, _M0L3strS445, _M0L5startS443, _M0L3lenS446);
  _M0L6_2atmpS1597 = _M0L5bytesS447;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2532
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1597, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1597);
  return _result_2532;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS438,
  int32_t _M0L6offsetS442,
  int64_t _M0L6lengthS440
) {
  int32_t _M0L3lenS437;
  int32_t _M0L6lengthS439;
  int32_t _if__result_2533;
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L3lenS437 = Moonbit_array_length(_M0L4selfS438);
  if (_M0L6lengthS440 == 4294967296ll) {
    _M0L6lengthS439 = _M0L3lenS437 - _M0L6offsetS442;
  } else {
    int64_t _M0L7_2aSomeS441 = _M0L6lengthS440;
    _M0L6lengthS439 = (int32_t)_M0L7_2aSomeS441;
  }
  if (_M0L6offsetS442 >= 0) {
    if (_M0L6lengthS439 >= 0) {
      int32_t _M0L6_2atmpS1595 = _M0L6offsetS442 + _M0L6lengthS439;
      _if__result_2533 = _M0L6_2atmpS1595 <= _M0L3lenS437;
    } else {
      _if__result_2533 = 0;
    }
  } else {
    _if__result_2533 = 0;
  }
  if (_if__result_2533) {
    moonbit_incref_cycle_free(_M0L4selfS438);
    #line 85 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    return _M0FPB19unsafe__sub__string(_M0L4selfS438, _M0L6offsetS442, _M0L6lengthS439);
  } else {
    #line 84 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array10FixedArray18blit__from__string(
  moonbit_bytes_t _M0L4selfS429,
  int32_t _M0L13bytes__offsetS424,
  moonbit_string_t _M0L3strS431,
  int32_t _M0L11str__offsetS427,
  int32_t _M0L6lengthS425
) {
  int32_t _M0L6_2atmpS1594;
  int32_t _M0L6_2atmpS1593;
  int32_t _M0L2e1S423;
  int32_t _M0L6_2atmpS1592;
  int32_t _M0L2e2S426;
  int32_t _M0L4len1S428;
  int32_t _M0L4len2S430;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1594 = _M0L6lengthS425 * 2;
  _M0L6_2atmpS1593 = _M0L13bytes__offsetS424 + _M0L6_2atmpS1594;
  _M0L2e1S423 = _M0L6_2atmpS1593 - 1;
  _M0L6_2atmpS1592 = _M0L11str__offsetS427 + _M0L6lengthS425;
  _M0L2e2S426 = _M0L6_2atmpS1592 - 1;
  _M0L4len1S428 = Moonbit_array_length(_M0L4selfS429);
  _M0L4len2S430 = Moonbit_array_length(_M0L3strS431);
  if (
    _M0L6lengthS425 >= 0
    && _M0L13bytes__offsetS424 >= 0
    && _M0L2e1S423 < _M0L4len1S428
    && _M0L11str__offsetS427 >= 0
    && _M0L2e2S426 < _M0L4len2S430
  ) {
    int32_t _M0L16end__str__offsetS432 =
      _M0L11str__offsetS427 + _M0L6lengthS425;
    int32_t _M0L1iS433 = _M0L11str__offsetS427;
    int32_t _M0L1jS434 = _M0L13bytes__offsetS424;
    while (1) {
      if (_M0L1iS433 < _M0L16end__str__offsetS432) {
        int32_t _M0L6_2atmpS1589 = _M0L3strS431[_M0L1iS433];
        int32_t _M0L6_2atmpS1588 = (int32_t)_M0L6_2atmpS1589;
        uint32_t _M0L1cS435 = *(uint32_t*)&_M0L6_2atmpS1588;
        uint32_t _M0L6_2atmpS1584 = _M0L1cS435 & 255u;
        int32_t _M0L6_2atmpS1583;
        int32_t _M0L6_2atmpS1585;
        uint32_t _M0L6_2atmpS1587;
        int32_t _M0L6_2atmpS1586;
        int32_t _M0L6_2atmpS1590;
        int32_t _M0L6_2atmpS1591;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1583 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1584);
        if (
          _M0L1jS434 < 0 || _M0L1jS434 >= Moonbit_array_length(_M0L4selfS429)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS429[_M0L1jS434] = _M0L6_2atmpS1583;
        _M0L6_2atmpS1585 = _M0L1jS434 + 1;
        _M0L6_2atmpS1587 = _M0L1cS435 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1586 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1587);
        if (
          _M0L6_2atmpS1585 < 0
          || _M0L6_2atmpS1585 >= Moonbit_array_length(_M0L4selfS429)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS429[_M0L6_2atmpS1585] = _M0L6_2atmpS1586;
        _M0L6_2atmpS1590 = _M0L1iS433 + 1;
        _M0L6_2atmpS1591 = _M0L1jS434 + 2;
        _M0L1iS433 = _M0L6_2atmpS1590;
        _M0L1jS434 = _M0L6_2atmpS1591;
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

int32_t _M0MPC14uint4UInt8to__byte(uint32_t _M0L4selfS422) {
  int32_t _M0L6_2atmpS1582;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1582 = *(int32_t*)&_M0L4selfS422;
  return _M0L6_2atmpS1582 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS414,
  int32_t _M0L5radixS413
) {
  uint16_t* _M0L6bufferS415;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS413 < 2 || _M0L5radixS413 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_28.data);
  }
  if (_M0L4selfS414 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_18.data;
  }
  switch (_M0L5radixS413) {
    case 10: {
      int32_t _M0L3lenS416;
      uint16_t* _M0L6bufferS417;
      #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS416 = _M0FPB12dec__count64(_M0L4selfS414);
      _M0L6bufferS417 = (uint16_t*)moonbit_make_string(_M0L3lenS416, 0);
      #line 624 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS417, _M0L4selfS414, 0, _M0L3lenS416);
      _M0L6bufferS415 = _M0L6bufferS417;
      break;
    }
    
    case 16: {
      int32_t _M0L3lenS418;
      uint16_t* _M0L6bufferS419;
      #line 628 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS418 = _M0FPB12hex__count64(_M0L4selfS414);
      _M0L6bufferS419 = (uint16_t*)moonbit_make_string(_M0L3lenS418, 0);
      #line 630 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS419, _M0L4selfS414, 0, _M0L3lenS418);
      _M0L6bufferS415 = _M0L6bufferS419;
      break;
    }
    default: {
      int32_t _M0L3lenS420;
      uint16_t* _M0L6bufferS421;
      #line 634 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS420 = _M0FPB14radix__count64(_M0L4selfS414, _M0L5radixS413);
      _M0L6bufferS421 = (uint16_t*)moonbit_make_string(_M0L3lenS420, 0);
      #line 636 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS421, _M0L4selfS414, 0, _M0L3lenS420, _M0L5radixS413);
      _M0L6bufferS415 = _M0L6bufferS421;
      break;
    }
  }
  return _M0L6bufferS415;
}

moonbit_string_t _M0MPC15int645Int6418to__string_2einner(
  int64_t _M0L4selfS397,
  int32_t _M0L5radixS396
) {
  int32_t _M0L12is__negativeS398;
  uint64_t _M0L3numS399;
  uint16_t* _M0L6bufferS400;
  #line 548 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS396 < 2 || _M0L5radixS396 > 36) {
    #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_28.data);
  }
  if (_M0L4selfS397 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_18.data;
  }
  _M0L12is__negativeS398 = _M0L4selfS397 < 0ll;
  if (_M0L12is__negativeS398) {
    int64_t _M0L6_2atmpS1581 = -_M0L4selfS397;
    _M0L3numS399 = *(uint64_t*)&_M0L6_2atmpS1581;
  } else {
    _M0L3numS399 = *(uint64_t*)&_M0L4selfS397;
  }
  switch (_M0L5radixS396) {
    case 10: {
      int32_t _M0L10digit__lenS401;
      int32_t _M0L6_2atmpS1578;
      int32_t _M0L10total__lenS402;
      uint16_t* _M0L6bufferS403;
      int32_t _M0L12digit__startS404;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS401 = _M0FPB12dec__count64(_M0L3numS399);
      if (_M0L12is__negativeS398) {
        _M0L6_2atmpS1578 = 1;
      } else {
        _M0L6_2atmpS1578 = 0;
      }
      _M0L10total__lenS402 = _M0L10digit__lenS401 + _M0L6_2atmpS1578;
      _M0L6bufferS403
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS402, 0);
      if (_M0L12is__negativeS398) {
        _M0L12digit__startS404 = 1;
      } else {
        _M0L12digit__startS404 = 0;
      }
      #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS403, _M0L3numS399, _M0L12digit__startS404, _M0L10total__lenS402);
      _M0L6bufferS400 = _M0L6bufferS403;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS405;
      int32_t _M0L6_2atmpS1579;
      int32_t _M0L10total__lenS406;
      uint16_t* _M0L6bufferS407;
      int32_t _M0L12digit__startS408;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS405 = _M0FPB12hex__count64(_M0L3numS399);
      if (_M0L12is__negativeS398) {
        _M0L6_2atmpS1579 = 1;
      } else {
        _M0L6_2atmpS1579 = 0;
      }
      _M0L10total__lenS406 = _M0L10digit__lenS405 + _M0L6_2atmpS1579;
      _M0L6bufferS407
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS406, 0);
      if (_M0L12is__negativeS398) {
        _M0L12digit__startS408 = 1;
      } else {
        _M0L12digit__startS408 = 0;
      }
      #line 585 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS407, _M0L3numS399, _M0L12digit__startS408, _M0L10total__lenS406);
      _M0L6bufferS400 = _M0L6bufferS407;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS409;
      int32_t _M0L6_2atmpS1580;
      int32_t _M0L10total__lenS410;
      uint16_t* _M0L6bufferS411;
      int32_t _M0L12digit__startS412;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS409
      = _M0FPB14radix__count64(_M0L3numS399, _M0L5radixS396);
      if (_M0L12is__negativeS398) {
        _M0L6_2atmpS1580 = 1;
      } else {
        _M0L6_2atmpS1580 = 0;
      }
      _M0L10total__lenS410 = _M0L10digit__lenS409 + _M0L6_2atmpS1580;
      _M0L6bufferS411
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS410, 0);
      if (_M0L12is__negativeS398) {
        _M0L12digit__startS412 = 1;
      } else {
        _M0L12digit__startS412 = 0;
      }
      #line 593 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS411, _M0L3numS399, _M0L12digit__startS412, _M0L10total__lenS410, _M0L5radixS396);
      _M0L6bufferS400 = _M0L6bufferS411;
      break;
    }
  }
  if (_M0L12is__negativeS398) {
    _M0L6bufferS400[0] = 45;
  }
  return _M0L6bufferS400;
}

int32_t _M0FPB22int64__to__string__dec(
  uint16_t* _M0L6bufferS382,
  uint64_t _M0L3numS394,
  int32_t _M0L12digit__startS383,
  int32_t _M0L10total__lenS395
) {
  int32_t _M0L6_2atmpS1577;
  uint64_t _M0L3numS372;
  int32_t _M0L6offsetS373;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1577 = _M0L10total__lenS395 - _M0L12digit__startS383;
  _M0L3numS372 = _M0L3numS394;
  _M0L6offsetS373 = _M0L6_2atmpS1577;
  while (1) {
    if (_M0L3numS372 >= 10000ull) {
      uint64_t _M0L1tS374 = _M0L3numS372 / 10000ull;
      uint64_t _M0L6_2atmpS1554 = _M0L3numS372 % 10000ull;
      int32_t _M0L1rS375 = (int32_t)_M0L6_2atmpS1554;
      int32_t _M0L2d1S376 = _M0L1rS375 / 100;
      int32_t _M0L2d2S377 = _M0L1rS375 % 100;
      int32_t _M0L6_2atmpS1553 = _M0L2d1S376 / 10;
      int32_t _M0L6_2atmpS1552 = 48 + _M0L6_2atmpS1553;
      int32_t _M0L6d1__hiS378 = (uint16_t)_M0L6_2atmpS1552;
      int32_t _M0L6_2atmpS1551 = _M0L2d1S376 % 10;
      int32_t _M0L6_2atmpS1550 = 48 + _M0L6_2atmpS1551;
      int32_t _M0L6d1__loS379 = (uint16_t)_M0L6_2atmpS1550;
      int32_t _M0L6_2atmpS1549 = _M0L2d2S377 / 10;
      int32_t _M0L6_2atmpS1548 = 48 + _M0L6_2atmpS1549;
      int32_t _M0L6d2__hiS380 = (uint16_t)_M0L6_2atmpS1548;
      int32_t _M0L6_2atmpS1547 = _M0L2d2S377 % 10;
      int32_t _M0L6_2atmpS1546 = 48 + _M0L6_2atmpS1547;
      int32_t _M0L6d2__loS381 = (uint16_t)_M0L6_2atmpS1546;
      int32_t _M0L6_2atmpS1538 = _M0L12digit__startS383 + _M0L6offsetS373;
      int32_t _M0L6_2atmpS1537 = _M0L6_2atmpS1538 - 4;
      int32_t _M0L6_2atmpS1540;
      int32_t _M0L6_2atmpS1539;
      int32_t _M0L6_2atmpS1542;
      int32_t _M0L6_2atmpS1541;
      int32_t _M0L6_2atmpS1544;
      int32_t _M0L6_2atmpS1543;
      int32_t _M0L6_2atmpS1545;
      _M0L6bufferS382[_M0L6_2atmpS1537] = _M0L6d1__hiS378;
      _M0L6_2atmpS1540 = _M0L12digit__startS383 + _M0L6offsetS373;
      _M0L6_2atmpS1539 = _M0L6_2atmpS1540 - 3;
      _M0L6bufferS382[_M0L6_2atmpS1539] = _M0L6d1__loS379;
      _M0L6_2atmpS1542 = _M0L12digit__startS383 + _M0L6offsetS373;
      _M0L6_2atmpS1541 = _M0L6_2atmpS1542 - 2;
      _M0L6bufferS382[_M0L6_2atmpS1541] = _M0L6d2__hiS380;
      _M0L6_2atmpS1544 = _M0L12digit__startS383 + _M0L6offsetS373;
      _M0L6_2atmpS1543 = _M0L6_2atmpS1544 - 1;
      _M0L6bufferS382[_M0L6_2atmpS1543] = _M0L6d2__loS381;
      _M0L6_2atmpS1545 = _M0L6offsetS373 - 4;
      _M0L3numS372 = _M0L1tS374;
      _M0L6offsetS373 = _M0L6_2atmpS1545;
      continue;
    } else {
      int32_t _M0L6_2atmpS1576 = (int32_t)_M0L3numS372;
      int32_t _M0L9remainingS385 = _M0L6_2atmpS1576;
      int32_t _M0L6offsetS386 = _M0L6offsetS373;
      while (1) {
        if (_M0L9remainingS385 >= 100) {
          int32_t _M0L1tS387 = _M0L9remainingS385 / 100;
          int32_t _M0L1dS388 = _M0L9remainingS385 % 100;
          int32_t _M0L6_2atmpS1563 = _M0L1dS388 / 10;
          int32_t _M0L6_2atmpS1562 = 48 + _M0L6_2atmpS1563;
          int32_t _M0L5d__hiS389 = (uint16_t)_M0L6_2atmpS1562;
          int32_t _M0L6_2atmpS1561 = _M0L1dS388 % 10;
          int32_t _M0L6_2atmpS1560 = 48 + _M0L6_2atmpS1561;
          int32_t _M0L5d__loS390 = (uint16_t)_M0L6_2atmpS1560;
          int32_t _M0L6_2atmpS1556 = _M0L12digit__startS383 + _M0L6offsetS386;
          int32_t _M0L6_2atmpS1555 = _M0L6_2atmpS1556 - 2;
          int32_t _M0L6_2atmpS1558;
          int32_t _M0L6_2atmpS1557;
          int32_t _M0L6_2atmpS1559;
          _M0L6bufferS382[_M0L6_2atmpS1555] = _M0L5d__hiS389;
          _M0L6_2atmpS1558 = _M0L12digit__startS383 + _M0L6offsetS386;
          _M0L6_2atmpS1557 = _M0L6_2atmpS1558 - 1;
          _M0L6bufferS382[_M0L6_2atmpS1557] = _M0L5d__loS390;
          _M0L6_2atmpS1559 = _M0L6offsetS386 - 2;
          _M0L9remainingS385 = _M0L1tS387;
          _M0L6offsetS386 = _M0L6_2atmpS1559;
          continue;
        } else if (_M0L9remainingS385 >= 10) {
          int32_t _M0L6_2atmpS1571 = _M0L9remainingS385 / 10;
          int32_t _M0L6_2atmpS1570 = 48 + _M0L6_2atmpS1571;
          int32_t _M0L5d__hiS392 = (uint16_t)_M0L6_2atmpS1570;
          int32_t _M0L6_2atmpS1569 = _M0L9remainingS385 % 10;
          int32_t _M0L6_2atmpS1568 = 48 + _M0L6_2atmpS1569;
          int32_t _M0L5d__loS393 = (uint16_t)_M0L6_2atmpS1568;
          int32_t _M0L6_2atmpS1565 = _M0L12digit__startS383 + _M0L6offsetS386;
          int32_t _M0L6_2atmpS1564 = _M0L6_2atmpS1565 - 2;
          int32_t _M0L6_2atmpS1567;
          int32_t _M0L6_2atmpS1566;
          _M0L6bufferS382[_M0L6_2atmpS1564] = _M0L5d__hiS392;
          _M0L6_2atmpS1567 = _M0L12digit__startS383 + _M0L6offsetS386;
          _M0L6_2atmpS1566 = _M0L6_2atmpS1567 - 1;
          _M0L6bufferS382[_M0L6_2atmpS1566] = _M0L5d__loS393;
        } else {
          int32_t _M0L6_2atmpS1575 = _M0L12digit__startS383 + _M0L6offsetS386;
          int32_t _M0L6_2atmpS1572 = _M0L6_2atmpS1575 - 1;
          int32_t _M0L6_2atmpS1574 = 48 + _M0L9remainingS385;
          int32_t _M0L6_2atmpS1573 = (uint16_t)_M0L6_2atmpS1574;
          _M0L6bufferS382[_M0L6_2atmpS1572] = _M0L6_2atmpS1573;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB26int64__to__string__generic(
  uint16_t* _M0L6bufferS362,
  uint64_t _M0L3numS366,
  int32_t _M0L12digit__startS363,
  int32_t _M0L10total__lenS365,
  int32_t _M0L5radixS356
) {
  uint64_t _M0L4baseS355;
  int32_t _M0L6_2atmpS1522;
  int32_t _M0L6_2atmpS1521;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS355 = _M0MPC13int3Int10to__uint64(_M0L5radixS356);
  _M0L6_2atmpS1522 = _M0L5radixS356 - 1;
  _M0L6_2atmpS1521 = _M0L5radixS356 & _M0L6_2atmpS1522;
  if (_M0L6_2atmpS1521 == 0) {
    int32_t _M0L5shiftS357;
    uint64_t _M0L4maskS358;
    int32_t _M0L6_2atmpS1529;
    int32_t _M0L6offsetS359;
    uint64_t _M0L1nS360;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS357 = moonbit_ctz32(_M0L5radixS356);
    _M0L4maskS358 = _M0L4baseS355 - 1ull;
    _M0L6_2atmpS1529 = _M0L10total__lenS365 - _M0L12digit__startS363;
    _M0L6offsetS359 = _M0L6_2atmpS1529;
    _M0L1nS360 = _M0L3numS366;
    while (1) {
      if (_M0L1nS360 > 0ull) {
        uint64_t _M0L6_2atmpS1528 = _M0L1nS360 & _M0L4maskS358;
        int32_t _M0L5digitS361 = (int32_t)_M0L6_2atmpS1528;
        int32_t _M0L6_2atmpS1525 = _M0L12digit__startS363 + _M0L6offsetS359;
        int32_t _M0L6_2atmpS1523 = _M0L6_2atmpS1525 - 1;
        int32_t _M0L6_2atmpS1524 =
          ((moonbit_string_t)moonbit_string_literal_29.data)[_M0L5digitS361];
        int32_t _M0L6_2atmpS1526;
        uint64_t _M0L6_2atmpS1527;
        _M0L6bufferS362[_M0L6_2atmpS1523] = _M0L6_2atmpS1524;
        _M0L6_2atmpS1526 = _M0L6offsetS359 - 1;
        _M0L6_2atmpS1527 = _M0L1nS360 >> (_M0L5shiftS357 & 63);
        _M0L6offsetS359 = _M0L6_2atmpS1526;
        _M0L1nS360 = _M0L6_2atmpS1527;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1536 = _M0L10total__lenS365 - _M0L12digit__startS363;
    int32_t _M0L6offsetS367 = _M0L6_2atmpS1536;
    uint64_t _M0L1nS368 = _M0L3numS366;
    while (1) {
      if (_M0L1nS368 > 0ull) {
        uint64_t _M0L1qS369 = _M0L1nS368 / _M0L4baseS355;
        uint64_t _M0L6_2atmpS1535 = _M0L1qS369 * _M0L4baseS355;
        uint64_t _M0L6_2atmpS1534 = _M0L1nS368 - _M0L6_2atmpS1535;
        int32_t _M0L5digitS370 = (int32_t)_M0L6_2atmpS1534;
        int32_t _M0L6_2atmpS1532 = _M0L12digit__startS363 + _M0L6offsetS367;
        int32_t _M0L6_2atmpS1530 = _M0L6_2atmpS1532 - 1;
        int32_t _M0L6_2atmpS1531 =
          ((moonbit_string_t)moonbit_string_literal_29.data)[_M0L5digitS370];
        int32_t _M0L6_2atmpS1533;
        _M0L6bufferS362[_M0L6_2atmpS1530] = _M0L6_2atmpS1531;
        _M0L6_2atmpS1533 = _M0L6offsetS367 - 1;
        _M0L6offsetS367 = _M0L6_2atmpS1533;
        _M0L1nS368 = _M0L1qS369;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB22int64__to__string__hex(
  uint16_t* _M0L6bufferS349,
  uint64_t _M0L3numS354,
  int32_t _M0L12digit__startS350,
  int32_t _M0L10total__lenS353
) {
  int32_t _M0L6_2atmpS1520;
  int32_t _M0L6offsetS344;
  uint64_t _M0L1nS345;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1520 = _M0L10total__lenS353 - _M0L12digit__startS350;
  _M0L6offsetS344 = _M0L6_2atmpS1520;
  _M0L1nS345 = _M0L3numS354;
  while (1) {
    if (_M0L6offsetS344 >= 2) {
      uint64_t _M0L6_2atmpS1517 = _M0L1nS345 & 255ull;
      int32_t _M0L9byte__valS346 = (int32_t)_M0L6_2atmpS1517;
      int32_t _M0L2hiS347 = _M0L9byte__valS346 / 16;
      int32_t _M0L2loS348 = _M0L9byte__valS346 % 16;
      int32_t _M0L6_2atmpS1511 = _M0L12digit__startS350 + _M0L6offsetS344;
      int32_t _M0L6_2atmpS1509 = _M0L6_2atmpS1511 - 2;
      int32_t _M0L6_2atmpS1510 =
        ((moonbit_string_t)moonbit_string_literal_29.data)[_M0L2hiS347];
      int32_t _M0L6_2atmpS1514;
      int32_t _M0L6_2atmpS1512;
      int32_t _M0L6_2atmpS1513;
      int32_t _M0L6_2atmpS1515;
      uint64_t _M0L6_2atmpS1516;
      _M0L6bufferS349[_M0L6_2atmpS1509] = _M0L6_2atmpS1510;
      _M0L6_2atmpS1514 = _M0L12digit__startS350 + _M0L6offsetS344;
      _M0L6_2atmpS1512 = _M0L6_2atmpS1514 - 1;
      _M0L6_2atmpS1513
      = ((moonbit_string_t)moonbit_string_literal_29.data)[
        _M0L2loS348
      ];
      _M0L6bufferS349[_M0L6_2atmpS1512] = _M0L6_2atmpS1513;
      _M0L6_2atmpS1515 = _M0L6offsetS344 - 2;
      _M0L6_2atmpS1516 = _M0L1nS345 >> 8;
      _M0L6offsetS344 = _M0L6_2atmpS1515;
      _M0L1nS345 = _M0L6_2atmpS1516;
      continue;
    } else if (_M0L6offsetS344 == 1) {
      uint64_t _M0L6_2atmpS1519 = _M0L1nS345 & 15ull;
      int32_t _M0L6nibbleS352 = (int32_t)_M0L6_2atmpS1519;
      int32_t _M0L6_2atmpS1518 =
        ((moonbit_string_t)moonbit_string_literal_29.data)[_M0L6nibbleS352];
      _M0L6bufferS349[_M0L12digit__startS350] = _M0L6_2atmpS1518;
    }
    break;
  }
  return 0;
}

int32_t _M0FPB14radix__count64(
  uint64_t _M0L5valueS338,
  int32_t _M0L5radixS340
) {
  uint64_t _M0L4baseS339;
  uint64_t _M0L3numS341;
  int32_t _M0L5countS342;
  #line 419 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS338 == 0ull) {
    return 1;
  }
  #line 424 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS339 = _M0MPC13int3Int10to__uint64(_M0L5radixS340);
  _M0L3numS341 = _M0L5valueS338;
  _M0L5countS342 = 0;
  while (1) {
    if (_M0L3numS341 > 0ull) {
      uint64_t _M0L6_2atmpS1507 = _M0L3numS341 / _M0L4baseS339;
      int32_t _M0L6_2atmpS1508 = _M0L5countS342 + 1;
      _M0L3numS341 = _M0L6_2atmpS1507;
      _M0L5countS342 = _M0L6_2atmpS1508;
      continue;
    } else {
      return _M0L5countS342;
    }
    break;
  }
}

int32_t _M0FPB12hex__count64(uint64_t _M0L5valueS336) {
  #line 407 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS336 == 0ull) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS337;
    int32_t _M0L6_2atmpS1506;
    int32_t _M0L6_2atmpS1505;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS337 = moonbit_clz64(_M0L5valueS336);
    _M0L6_2atmpS1506 = 63 - _M0L14leading__zerosS337;
    _M0L6_2atmpS1505 = _M0L6_2atmpS1506 / 4;
    return _M0L6_2atmpS1505 + 1;
  }
}

int32_t _M0FPB12dec__count64(uint64_t _M0L5valueS335) {
  #line 343 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS335 >= 10000000000ull) {
    if (_M0L5valueS335 >= 100000000000000ull) {
      if (_M0L5valueS335 >= 10000000000000000ull) {
        if (_M0L5valueS335 >= 1000000000000000000ull) {
          if (_M0L5valueS335 >= 10000000000000000000ull) {
            return 20;
          } else {
            return 19;
          }
        } else if (_M0L5valueS335 >= 100000000000000000ull) {
          return 18;
        } else {
          return 17;
        }
      } else if (_M0L5valueS335 >= 1000000000000000ull) {
        return 16;
      } else {
        return 15;
      }
    } else if (_M0L5valueS335 >= 1000000000000ull) {
      if (_M0L5valueS335 >= 10000000000000ull) {
        return 14;
      } else {
        return 13;
      }
    } else if (_M0L5valueS335 >= 100000000000ull) {
      return 12;
    } else {
      return 11;
    }
  } else if (_M0L5valueS335 >= 100000ull) {
    if (_M0L5valueS335 >= 10000000ull) {
      if (_M0L5valueS335 >= 1000000000ull) {
        return 10;
      } else if (_M0L5valueS335 >= 100000000ull) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS335 >= 1000000ull) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS335 >= 1000ull) {
    if (_M0L5valueS335 >= 10000ull) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS335 >= 100ull) {
    return 3;
  } else if (_M0L5valueS335 >= 10ull) {
    return 2;
  } else {
    return 1;
  }
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS319,
  int32_t _M0L5radixS318
) {
  int32_t _M0L12is__negativeS320;
  uint32_t _M0L3numS321;
  uint16_t* _M0L6bufferS322;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS318 < 2 || _M0L5radixS318 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_28.data);
  }
  if (_M0L4selfS319 == 0) {
    return (moonbit_string_t)moonbit_string_literal_18.data;
  }
  _M0L12is__negativeS320 = _M0L4selfS319 < 0;
  if (_M0L12is__negativeS320) {
    int32_t _M0L6_2atmpS1504 = -_M0L4selfS319;
    _M0L3numS321 = *(uint32_t*)&_M0L6_2atmpS1504;
  } else {
    _M0L3numS321 = *(uint32_t*)&_M0L4selfS319;
  }
  switch (_M0L5radixS318) {
    case 10: {
      int32_t _M0L10digit__lenS323;
      int32_t _M0L6_2atmpS1501;
      int32_t _M0L10total__lenS324;
      uint16_t* _M0L6bufferS325;
      int32_t _M0L12digit__startS326;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS323 = _M0FPB12dec__count32(_M0L3numS321);
      if (_M0L12is__negativeS320) {
        _M0L6_2atmpS1501 = 1;
      } else {
        _M0L6_2atmpS1501 = 0;
      }
      _M0L10total__lenS324 = _M0L10digit__lenS323 + _M0L6_2atmpS1501;
      _M0L6bufferS325
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS324, 0);
      if (_M0L12is__negativeS320) {
        _M0L12digit__startS326 = 1;
      } else {
        _M0L12digit__startS326 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS325, _M0L3numS321, _M0L12digit__startS326, _M0L10total__lenS324);
      _M0L6bufferS322 = _M0L6bufferS325;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS327;
      int32_t _M0L6_2atmpS1502;
      int32_t _M0L10total__lenS328;
      uint16_t* _M0L6bufferS329;
      int32_t _M0L12digit__startS330;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS327 = _M0FPB12hex__count32(_M0L3numS321);
      if (_M0L12is__negativeS320) {
        _M0L6_2atmpS1502 = 1;
      } else {
        _M0L6_2atmpS1502 = 0;
      }
      _M0L10total__lenS328 = _M0L10digit__lenS327 + _M0L6_2atmpS1502;
      _M0L6bufferS329
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS328, 0);
      if (_M0L12is__negativeS320) {
        _M0L12digit__startS330 = 1;
      } else {
        _M0L12digit__startS330 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS329, _M0L3numS321, _M0L12digit__startS330, _M0L10total__lenS328);
      _M0L6bufferS322 = _M0L6bufferS329;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS331;
      int32_t _M0L6_2atmpS1503;
      int32_t _M0L10total__lenS332;
      uint16_t* _M0L6bufferS333;
      int32_t _M0L12digit__startS334;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS331
      = _M0FPB14radix__count32(_M0L3numS321, _M0L5radixS318);
      if (_M0L12is__negativeS320) {
        _M0L6_2atmpS1503 = 1;
      } else {
        _M0L6_2atmpS1503 = 0;
      }
      _M0L10total__lenS332 = _M0L10digit__lenS331 + _M0L6_2atmpS1503;
      _M0L6bufferS333
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS332, 0);
      if (_M0L12is__negativeS320) {
        _M0L12digit__startS334 = 1;
      } else {
        _M0L12digit__startS334 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS333, _M0L3numS321, _M0L12digit__startS334, _M0L10total__lenS332, _M0L5radixS318);
      _M0L6bufferS322 = _M0L6bufferS333;
      break;
    }
  }
  if (_M0L12is__negativeS320) {
    _M0L6bufferS322[0] = 45;
  }
  return _M0L6bufferS322;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS312,
  int32_t _M0L5radixS314
) {
  uint32_t _M0L4baseS313;
  uint32_t _M0L3numS315;
  int32_t _M0L5countS316;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS312 == 0u) {
    return 1;
  }
  _M0L4baseS313 = *(uint32_t*)&_M0L5radixS314;
  _M0L3numS315 = _M0L5valueS312;
  _M0L5countS316 = 0;
  while (1) {
    if (_M0L3numS315 > 0u) {
      uint32_t _M0L6_2atmpS1499 = _M0L3numS315 / _M0L4baseS313;
      int32_t _M0L6_2atmpS1500 = _M0L5countS316 + 1;
      _M0L3numS315 = _M0L6_2atmpS1499;
      _M0L5countS316 = _M0L6_2atmpS1500;
      continue;
    } else {
      return _M0L5countS316;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS310) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS310 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS311;
    int32_t _M0L6_2atmpS1498;
    int32_t _M0L6_2atmpS1497;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS311 = moonbit_clz32(_M0L5valueS310);
    _M0L6_2atmpS1498 = 31 - _M0L14leading__zerosS311;
    _M0L6_2atmpS1497 = _M0L6_2atmpS1498 / 4;
    return _M0L6_2atmpS1497 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS309) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS309 >= 100000u) {
    if (_M0L5valueS309 >= 10000000u) {
      if (_M0L5valueS309 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS309 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS309 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS309 >= 1000u) {
    if (_M0L5valueS309 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS309 >= 100u) {
    return 3;
  } else if (_M0L5valueS309 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS295,
  uint32_t _M0L3numS307,
  int32_t _M0L12digit__startS296,
  int32_t _M0L10total__lenS308
) {
  int32_t _M0L6_2atmpS1496;
  uint32_t _M0L3numS285;
  int32_t _M0L6offsetS286;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1496 = _M0L10total__lenS308 - _M0L12digit__startS296;
  _M0L3numS285 = _M0L3numS307;
  _M0L6offsetS286 = _M0L6_2atmpS1496;
  while (1) {
    if (_M0L3numS285 >= 10000u) {
      uint32_t _M0L1tS287 = _M0L3numS285 / 10000u;
      uint32_t _M0L6_2atmpS1473 = _M0L3numS285 % 10000u;
      int32_t _M0L1rS288 = *(int32_t*)&_M0L6_2atmpS1473;
      int32_t _M0L2d1S289 = _M0L1rS288 / 100;
      int32_t _M0L2d2S290 = _M0L1rS288 % 100;
      int32_t _M0L6_2atmpS1472 = _M0L2d1S289 / 10;
      int32_t _M0L6_2atmpS1471 = 48 + _M0L6_2atmpS1472;
      int32_t _M0L6d1__hiS291 = (uint16_t)_M0L6_2atmpS1471;
      int32_t _M0L6_2atmpS1470 = _M0L2d1S289 % 10;
      int32_t _M0L6_2atmpS1469 = 48 + _M0L6_2atmpS1470;
      int32_t _M0L6d1__loS292 = (uint16_t)_M0L6_2atmpS1469;
      int32_t _M0L6_2atmpS1468 = _M0L2d2S290 / 10;
      int32_t _M0L6_2atmpS1467 = 48 + _M0L6_2atmpS1468;
      int32_t _M0L6d2__hiS293 = (uint16_t)_M0L6_2atmpS1467;
      int32_t _M0L6_2atmpS1466 = _M0L2d2S290 % 10;
      int32_t _M0L6_2atmpS1465 = 48 + _M0L6_2atmpS1466;
      int32_t _M0L6d2__loS294 = (uint16_t)_M0L6_2atmpS1465;
      int32_t _M0L6_2atmpS1457 = _M0L12digit__startS296 + _M0L6offsetS286;
      int32_t _M0L6_2atmpS1456 = _M0L6_2atmpS1457 - 4;
      int32_t _M0L6_2atmpS1459;
      int32_t _M0L6_2atmpS1458;
      int32_t _M0L6_2atmpS1461;
      int32_t _M0L6_2atmpS1460;
      int32_t _M0L6_2atmpS1463;
      int32_t _M0L6_2atmpS1462;
      int32_t _M0L6_2atmpS1464;
      _M0L6bufferS295[_M0L6_2atmpS1456] = _M0L6d1__hiS291;
      _M0L6_2atmpS1459 = _M0L12digit__startS296 + _M0L6offsetS286;
      _M0L6_2atmpS1458 = _M0L6_2atmpS1459 - 3;
      _M0L6bufferS295[_M0L6_2atmpS1458] = _M0L6d1__loS292;
      _M0L6_2atmpS1461 = _M0L12digit__startS296 + _M0L6offsetS286;
      _M0L6_2atmpS1460 = _M0L6_2atmpS1461 - 2;
      _M0L6bufferS295[_M0L6_2atmpS1460] = _M0L6d2__hiS293;
      _M0L6_2atmpS1463 = _M0L12digit__startS296 + _M0L6offsetS286;
      _M0L6_2atmpS1462 = _M0L6_2atmpS1463 - 1;
      _M0L6bufferS295[_M0L6_2atmpS1462] = _M0L6d2__loS294;
      _M0L6_2atmpS1464 = _M0L6offsetS286 - 4;
      _M0L3numS285 = _M0L1tS287;
      _M0L6offsetS286 = _M0L6_2atmpS1464;
      continue;
    } else {
      int32_t _M0L6_2atmpS1495 = *(int32_t*)&_M0L3numS285;
      int32_t _M0L9remainingS298 = _M0L6_2atmpS1495;
      int32_t _M0L6offsetS299 = _M0L6offsetS286;
      while (1) {
        if (_M0L9remainingS298 >= 100) {
          int32_t _M0L1tS300 = _M0L9remainingS298 / 100;
          int32_t _M0L1dS301 = _M0L9remainingS298 % 100;
          int32_t _M0L6_2atmpS1482 = _M0L1dS301 / 10;
          int32_t _M0L6_2atmpS1481 = 48 + _M0L6_2atmpS1482;
          int32_t _M0L5d__hiS302 = (uint16_t)_M0L6_2atmpS1481;
          int32_t _M0L6_2atmpS1480 = _M0L1dS301 % 10;
          int32_t _M0L6_2atmpS1479 = 48 + _M0L6_2atmpS1480;
          int32_t _M0L5d__loS303 = (uint16_t)_M0L6_2atmpS1479;
          int32_t _M0L6_2atmpS1475 = _M0L12digit__startS296 + _M0L6offsetS299;
          int32_t _M0L6_2atmpS1474 = _M0L6_2atmpS1475 - 2;
          int32_t _M0L6_2atmpS1477;
          int32_t _M0L6_2atmpS1476;
          int32_t _M0L6_2atmpS1478;
          _M0L6bufferS295[_M0L6_2atmpS1474] = _M0L5d__hiS302;
          _M0L6_2atmpS1477 = _M0L12digit__startS296 + _M0L6offsetS299;
          _M0L6_2atmpS1476 = _M0L6_2atmpS1477 - 1;
          _M0L6bufferS295[_M0L6_2atmpS1476] = _M0L5d__loS303;
          _M0L6_2atmpS1478 = _M0L6offsetS299 - 2;
          _M0L9remainingS298 = _M0L1tS300;
          _M0L6offsetS299 = _M0L6_2atmpS1478;
          continue;
        } else if (_M0L9remainingS298 >= 10) {
          int32_t _M0L6_2atmpS1490 = _M0L9remainingS298 / 10;
          int32_t _M0L6_2atmpS1489 = 48 + _M0L6_2atmpS1490;
          int32_t _M0L5d__hiS305 = (uint16_t)_M0L6_2atmpS1489;
          int32_t _M0L6_2atmpS1488 = _M0L9remainingS298 % 10;
          int32_t _M0L6_2atmpS1487 = 48 + _M0L6_2atmpS1488;
          int32_t _M0L5d__loS306 = (uint16_t)_M0L6_2atmpS1487;
          int32_t _M0L6_2atmpS1484 = _M0L12digit__startS296 + _M0L6offsetS299;
          int32_t _M0L6_2atmpS1483 = _M0L6_2atmpS1484 - 2;
          int32_t _M0L6_2atmpS1486;
          int32_t _M0L6_2atmpS1485;
          _M0L6bufferS295[_M0L6_2atmpS1483] = _M0L5d__hiS305;
          _M0L6_2atmpS1486 = _M0L12digit__startS296 + _M0L6offsetS299;
          _M0L6_2atmpS1485 = _M0L6_2atmpS1486 - 1;
          _M0L6bufferS295[_M0L6_2atmpS1485] = _M0L5d__loS306;
        } else {
          int32_t _M0L6_2atmpS1494 = _M0L12digit__startS296 + _M0L6offsetS299;
          int32_t _M0L6_2atmpS1491 = _M0L6_2atmpS1494 - 1;
          int32_t _M0L6_2atmpS1493 = 48 + _M0L9remainingS298;
          int32_t _M0L6_2atmpS1492 = (uint16_t)_M0L6_2atmpS1493;
          _M0L6bufferS295[_M0L6_2atmpS1491] = _M0L6_2atmpS1492;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS275,
  uint32_t _M0L3numS279,
  int32_t _M0L12digit__startS276,
  int32_t _M0L10total__lenS278,
  int32_t _M0L5radixS269
) {
  uint32_t _M0L4baseS268;
  int32_t _M0L6_2atmpS1441;
  int32_t _M0L6_2atmpS1440;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS268 = *(uint32_t*)&_M0L5radixS269;
  _M0L6_2atmpS1441 = _M0L5radixS269 - 1;
  _M0L6_2atmpS1440 = _M0L5radixS269 & _M0L6_2atmpS1441;
  if (_M0L6_2atmpS1440 == 0) {
    int32_t _M0L5shiftS270;
    uint32_t _M0L4maskS271;
    int32_t _M0L6_2atmpS1448;
    int32_t _M0L6offsetS272;
    uint32_t _M0L1nS273;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS270 = moonbit_ctz32(_M0L5radixS269);
    _M0L4maskS271 = _M0L4baseS268 - 1u;
    _M0L6_2atmpS1448 = _M0L10total__lenS278 - _M0L12digit__startS276;
    _M0L6offsetS272 = _M0L6_2atmpS1448;
    _M0L1nS273 = _M0L3numS279;
    while (1) {
      if (_M0L1nS273 > 0u) {
        uint32_t _M0L6_2atmpS1447 = _M0L1nS273 & _M0L4maskS271;
        int32_t _M0L5digitS274 = *(int32_t*)&_M0L6_2atmpS1447;
        int32_t _M0L6_2atmpS1444 = _M0L12digit__startS276 + _M0L6offsetS272;
        int32_t _M0L6_2atmpS1442 = _M0L6_2atmpS1444 - 1;
        int32_t _M0L6_2atmpS1443 =
          ((moonbit_string_t)moonbit_string_literal_29.data)[_M0L5digitS274];
        int32_t _M0L6_2atmpS1445;
        uint32_t _M0L6_2atmpS1446;
        _M0L6bufferS275[_M0L6_2atmpS1442] = _M0L6_2atmpS1443;
        _M0L6_2atmpS1445 = _M0L6offsetS272 - 1;
        _M0L6_2atmpS1446 = _M0L1nS273 >> (_M0L5shiftS270 & 31);
        _M0L6offsetS272 = _M0L6_2atmpS1445;
        _M0L1nS273 = _M0L6_2atmpS1446;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1455 = _M0L10total__lenS278 - _M0L12digit__startS276;
    int32_t _M0L6offsetS280 = _M0L6_2atmpS1455;
    uint32_t _M0L1nS281 = _M0L3numS279;
    while (1) {
      if (_M0L1nS281 > 0u) {
        uint32_t _M0L1qS282 = _M0L1nS281 / _M0L4baseS268;
        uint32_t _M0L6_2atmpS1454 = _M0L1qS282 * _M0L4baseS268;
        uint32_t _M0L6_2atmpS1453 = _M0L1nS281 - _M0L6_2atmpS1454;
        int32_t _M0L5digitS283 = *(int32_t*)&_M0L6_2atmpS1453;
        int32_t _M0L6_2atmpS1451 = _M0L12digit__startS276 + _M0L6offsetS280;
        int32_t _M0L6_2atmpS1449 = _M0L6_2atmpS1451 - 1;
        int32_t _M0L6_2atmpS1450 =
          ((moonbit_string_t)moonbit_string_literal_29.data)[_M0L5digitS283];
        int32_t _M0L6_2atmpS1452;
        _M0L6bufferS275[_M0L6_2atmpS1449] = _M0L6_2atmpS1450;
        _M0L6_2atmpS1452 = _M0L6offsetS280 - 1;
        _M0L6offsetS280 = _M0L6_2atmpS1452;
        _M0L1nS281 = _M0L1qS282;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS262,
  uint32_t _M0L3numS267,
  int32_t _M0L12digit__startS263,
  int32_t _M0L10total__lenS266
) {
  int32_t _M0L6_2atmpS1439;
  int32_t _M0L6offsetS257;
  uint32_t _M0L1nS258;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1439 = _M0L10total__lenS266 - _M0L12digit__startS263;
  _M0L6offsetS257 = _M0L6_2atmpS1439;
  _M0L1nS258 = _M0L3numS267;
  while (1) {
    if (_M0L6offsetS257 >= 2) {
      uint32_t _M0L6_2atmpS1436 = _M0L1nS258 & 255u;
      int32_t _M0L9byte__valS259 = *(int32_t*)&_M0L6_2atmpS1436;
      int32_t _M0L2hiS260 = _M0L9byte__valS259 / 16;
      int32_t _M0L2loS261 = _M0L9byte__valS259 % 16;
      int32_t _M0L6_2atmpS1430 = _M0L12digit__startS263 + _M0L6offsetS257;
      int32_t _M0L6_2atmpS1428 = _M0L6_2atmpS1430 - 2;
      int32_t _M0L6_2atmpS1429 =
        ((moonbit_string_t)moonbit_string_literal_29.data)[_M0L2hiS260];
      int32_t _M0L6_2atmpS1433;
      int32_t _M0L6_2atmpS1431;
      int32_t _M0L6_2atmpS1432;
      int32_t _M0L6_2atmpS1434;
      uint32_t _M0L6_2atmpS1435;
      _M0L6bufferS262[_M0L6_2atmpS1428] = _M0L6_2atmpS1429;
      _M0L6_2atmpS1433 = _M0L12digit__startS263 + _M0L6offsetS257;
      _M0L6_2atmpS1431 = _M0L6_2atmpS1433 - 1;
      _M0L6_2atmpS1432
      = ((moonbit_string_t)moonbit_string_literal_29.data)[
        _M0L2loS261
      ];
      _M0L6bufferS262[_M0L6_2atmpS1431] = _M0L6_2atmpS1432;
      _M0L6_2atmpS1434 = _M0L6offsetS257 - 2;
      _M0L6_2atmpS1435 = _M0L1nS258 >> 8;
      _M0L6offsetS257 = _M0L6_2atmpS1434;
      _M0L1nS258 = _M0L6_2atmpS1435;
      continue;
    } else if (_M0L6offsetS257 == 1) {
      uint32_t _M0L6_2atmpS1438 = _M0L1nS258 & 15u;
      int32_t _M0L6nibbleS265 = *(int32_t*)&_M0L6_2atmpS1438;
      int32_t _M0L6_2atmpS1437 =
        ((moonbit_string_t)moonbit_string_literal_29.data)[_M0L6nibbleS265];
      _M0L6bufferS262[_M0L12digit__startS263] = _M0L6_2atmpS1437;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS256
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS255;
  struct _M0TPB6Logger _M0L6_2atmpS1427;
  moonbit_string_t _result_2547;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS255 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS255);
  _M0L6_2atmpS1427
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS255
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS256, _M0L6_2atmpS1427);
  if (_M0L6_2atmpS1427.$1) {
    moonbit_decref(_M0L6_2atmpS1427.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2547 = _M0MPB13StringBuilder10to__string(_M0L6loggerS255);
  moonbit_decref_cycle_free(_M0L6loggerS255);
  return _result_2547;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS250,
  struct _M0TPB6Logger _M0L6loggerS249
) {
  moonbit_string_t _M0L6_2atmpS1424;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1424 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS250);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS249.$0->$method_0(_M0L6loggerS249.$1, _M0L6_2atmpS1424);
  moonbit_decref_cycle_free(_M0L6_2atmpS1424);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS252,
  struct _M0TPB6Logger _M0L6loggerS251
) {
  moonbit_string_t _M0L6_2atmpS1425;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1425 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS252);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS251.$0->$method_0(_M0L6loggerS251.$1, _M0L6_2atmpS1425);
  moonbit_decref_cycle_free(_M0L6_2atmpS1425);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS254,
  struct _M0TPB6Logger _M0L6loggerS253
) {
  moonbit_string_t _M0L6_2atmpS1426;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1426 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS254);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS253.$0->$method_0(_M0L6loggerS253.$1, _M0L6_2atmpS1426);
  moonbit_decref_cycle_free(_M0L6_2atmpS1426);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS248
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS248.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS247
) {
  moonbit_string_t _M0L8_2afieldS2425;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2425 = _M0L4selfS247.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2425);
  return _M0L8_2afieldS2425;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS243,
  moonbit_string_t _M0L5valueS244,
  int32_t _M0L5startS245,
  int32_t _M0L3lenS246
) {
  int32_t _M0L6_2atmpS1423;
  int64_t _M0L6_2atmpS1422;
  struct _M0TPC16string10StringView _M0L6_2atmpS1421;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1423 = _M0L5startS245 + _M0L3lenS246;
  _M0L6_2atmpS1422 = (int64_t)_M0L6_2atmpS1423;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1421
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS244, _M0L5startS245, _M0L6_2atmpS1422);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS243, _M0L6_2atmpS1421);
  moonbit_decref_cycle_free(_M0L6_2atmpS1421.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String21clamped__view_2einner(
  moonbit_string_t _M0L4selfS236,
  int32_t _M0L5startS238,
  int64_t _M0L3endS240
) {
  int32_t _M0L3lenS235;
  int32_t _M0Lm2loS237;
  int32_t _M0Lm2hiS239;
  int32_t _M0L6_2atmpS1405;
  int32_t _if__result_2548;
  int32_t _M0L6_2atmpS1413;
  int32_t _if__result_2549;
  int32_t _M0L6_2atmpS1415;
  int32_t _M0L6_2atmpS1416;
  #line 698 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS235 = Moonbit_array_length(_M0L4selfS236);
  if (_M0L5startS238 < 0) {
    _M0Lm2loS237 = 0;
  } else if (_M0L5startS238 > _M0L3lenS235) {
    _M0Lm2loS237 = _M0L3lenS235;
  } else {
    _M0Lm2loS237 = _M0L5startS238;
  }
  if (_M0L3endS240 == 4294967296ll) {
    _M0Lm2hiS239 = _M0L3lenS235;
  } else {
    int64_t _M0L7_2aSomeS241 = _M0L3endS240;
    int32_t _M0L4_2aeS242 = (int32_t)_M0L7_2aSomeS241;
    if (_M0L4_2aeS242 < 0) {
      _M0Lm2hiS239 = 0;
    } else if (_M0L4_2aeS242 > _M0L3lenS235) {
      _M0Lm2hiS239 = _M0L3lenS235;
    } else {
      _M0Lm2hiS239 = _M0L4_2aeS242;
    }
  }
  _M0L6_2atmpS1405 = _M0Lm2loS237;
  if (_M0L6_2atmpS1405 > 0) {
    int32_t _M0L6_2atmpS1404 = _M0Lm2loS237;
    if (_M0L6_2atmpS1404 < _M0L3lenS235) {
      int32_t _M0L6_2atmpS1403 = _M0Lm2loS237;
      int32_t _M0L6_2atmpS1402 = _M0L4selfS236[_M0L6_2atmpS1403];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1402)) {
        int32_t _M0L6_2atmpS1401 = _M0Lm2loS237;
        int32_t _M0L6_2atmpS1400 = _M0L6_2atmpS1401 - 1;
        int32_t _M0L6_2atmpS1399 = _M0L4selfS236[_M0L6_2atmpS1400];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2548
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1399);
      } else {
        _if__result_2548 = 0;
      }
    } else {
      _if__result_2548 = 0;
    }
  } else {
    _if__result_2548 = 0;
  }
  if (_if__result_2548) {
    int32_t _M0L6_2atmpS1406 = _M0Lm2loS237;
    _M0Lm2loS237 = _M0L6_2atmpS1406 + 1;
  }
  _M0L6_2atmpS1413 = _M0Lm2hiS239;
  if (_M0L6_2atmpS1413 > 0) {
    int32_t _M0L6_2atmpS1412 = _M0Lm2hiS239;
    if (_M0L6_2atmpS1412 < _M0L3lenS235) {
      int32_t _M0L6_2atmpS1411 = _M0Lm2hiS239;
      int32_t _M0L6_2atmpS1410 = _M0L4selfS236[_M0L6_2atmpS1411];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1410)) {
        int32_t _M0L6_2atmpS1409 = _M0Lm2hiS239;
        int32_t _M0L6_2atmpS1408 = _M0L6_2atmpS1409 - 1;
        int32_t _M0L6_2atmpS1407 = _M0L4selfS236[_M0L6_2atmpS1408];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2549
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1407);
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
    int32_t _M0L6_2atmpS1414 = _M0Lm2hiS239;
    _M0Lm2hiS239 = _M0L6_2atmpS1414 - 1;
  }
  _M0L6_2atmpS1415 = _M0Lm2loS237;
  _M0L6_2atmpS1416 = _M0Lm2hiS239;
  if (_M0L6_2atmpS1415 >= _M0L6_2atmpS1416) {
    int32_t _M0L6_2atmpS1417 = _M0Lm2loS237;
    int32_t _M0L6_2atmpS1418 = _M0Lm2loS237;
    moonbit_incref_cycle_free(_M0L4selfS236);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS236,
                                                 .$1 = _M0L6_2atmpS1417,
                                                 .$2 = _M0L6_2atmpS1418};
  } else {
    int32_t _M0L6_2atmpS1419 = _M0Lm2loS237;
    int32_t _M0L6_2atmpS1420 = _M0Lm2hiS239;
    moonbit_incref_cycle_free(_M0L4selfS236);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS236,
                                                 .$1 = _M0L6_2atmpS1419,
                                                 .$2 = _M0L6_2atmpS1420};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS234,
  struct _M0TPB4Show _M0L4showS233
) {
  struct _M0TPB6Logger _M0L6_2atmpS1398;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS234);
  _M0L6_2atmpS1398
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS234
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS233.$0->$method_0(_M0L4showS233.$1, _M0L6_2atmpS1398);
  if (_M0L6_2atmpS1398.$1) {
    moonbit_decref(_M0L6_2atmpS1398.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS232,
  struct _M0TPB4Show _M0L4showS231
) {
  struct _M0TPB6Logger _M0L6_2atmpS1397;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS232);
  _M0L6_2atmpS1397
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS232
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS231.$0->$method_0(_M0L4showS231.$1, _M0L6_2atmpS1397);
  if (_M0L6_2atmpS1397.$1) {
    moonbit_decref(_M0L6_2atmpS1397.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS230) {
  int64_t _M0L6_2atmpS1396;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1396 = (int64_t)_M0L4selfS230;
  return *(uint64_t*)&_M0L6_2atmpS1396;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

moonbit_string_t _M0MPC16string6String14escape_2einner(
  moonbit_string_t _M0L4selfS228,
  int32_t _M0L5quoteS229
) {
  struct _M0TPB13StringBuilder* _M0L3bufS227;
  int32_t _M0L6_2atmpS1395;
  struct _M0TPC16string10StringView _M0L6_2atmpS1393;
  struct _M0TPB6Logger _M0L6_2atmpS1394;
  moonbit_string_t _result_2550;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS227 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1395 = Moonbit_array_length(_M0L4selfS228);
  moonbit_incref_cycle_free(_M0L4selfS228);
  _M0L6_2atmpS1393
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS228, .$1 = 0, .$2 = _M0L6_2atmpS1395
  };
  moonbit_incref_cycle_free(_M0L3bufS227);
  _M0L6_2atmpS1394
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS227
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1393, _M0L6_2atmpS1394, _M0L5quoteS229);
  moonbit_decref_cycle_free(_M0L6_2atmpS1393.$0);
  if (_M0L6_2atmpS1394.$1) {
    moonbit_decref(_M0L6_2atmpS1394.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2550 = _M0MPB13StringBuilder10to__string(_M0L3bufS227);
  moonbit_decref_cycle_free(_M0L3bufS227);
  return _result_2550;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS219,
  struct _M0TPB6Logger _M0L6loggerS217,
  int32_t _M0L5quoteS216
) {
  int32_t _M0L3endS1391;
  int32_t _M0L5startS1392;
  int32_t _M0L3lenS218;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS220;
  int32_t _M0L1iS221;
  int32_t _M0L3segS222;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS216) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS217.$0->$method_3(_M0L6loggerS217.$1, 34);
  }
  _M0L3endS1391 = _M0L4selfS219.$2;
  _M0L5startS1392 = _M0L4selfS219.$1;
  _M0L3lenS218 = _M0L3endS1391 - _M0L5startS1392;
  moonbit_incref_cycle_free(_M0L4selfS219.$0);
  if (_M0L6loggerS217.$1) {
    moonbit_incref(_M0L6loggerS217.$1);
  }
  _M0L6_2aenvS220
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS220)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 71, 0);
  _M0L6_2aenvS220->$0 = _M0L4selfS219;
  _M0L6_2aenvS220->$1 = _M0L6loggerS217;
  _M0L1iS221 = 0;
  _M0L3segS222 = 0;
  _2afor_223:;
  while (1) {
    moonbit_string_t _M0L3strS1388;
    int32_t _M0L5startS1390;
    int32_t _M0L6_2atmpS1389;
    int32_t _M0L4codeS224;
    int32_t _M0L1cS226;
    int32_t _M0L6_2atmpS1372;
    int32_t _M0L6_2atmpS1373;
    int32_t _M0L6_2atmpS1374;
    if (_M0L1iS221 >= _M0L3lenS218) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS220, _M0L3segS222, _M0L1iS221);
      moonbit_decref_cycle_free(_M0L6_2aenvS220);
      break;
    }
    _M0L3strS1388 = _M0L4selfS219.$0;
    _M0L5startS1390 = _M0L4selfS219.$1;
    _M0L6_2atmpS1389 = _M0L5startS1390 + _M0L1iS221;
    _M0L4codeS224 = _M0L3strS1388[_M0L6_2atmpS1389];
    switch (_M0L4codeS224) {
      case 34: {
        _M0L1cS226 = _M0L4codeS224;
        goto join_225;
        break;
      }
      
      case 92: {
        _M0L1cS226 = _M0L4codeS224;
        goto join_225;
        break;
      }
      
      case 10: {
        int32_t _M0L6_2atmpS1375;
        int32_t _M0L6_2atmpS1376;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS220, _M0L3segS222, _M0L1iS221);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS217.$0->$method_0(_M0L6loggerS217.$1, (moonbit_string_t)moonbit_string_literal_30.data);
        _M0L6_2atmpS1375 = _M0L1iS221 + 1;
        _M0L6_2atmpS1376 = _M0L1iS221 + 1;
        _M0L1iS221 = _M0L6_2atmpS1375;
        _M0L3segS222 = _M0L6_2atmpS1376;
        goto _2afor_223;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1377;
        int32_t _M0L6_2atmpS1378;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS220, _M0L3segS222, _M0L1iS221);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS217.$0->$method_0(_M0L6loggerS217.$1, (moonbit_string_t)moonbit_string_literal_31.data);
        _M0L6_2atmpS1377 = _M0L1iS221 + 1;
        _M0L6_2atmpS1378 = _M0L1iS221 + 1;
        _M0L1iS221 = _M0L6_2atmpS1377;
        _M0L3segS222 = _M0L6_2atmpS1378;
        goto _2afor_223;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1379;
        int32_t _M0L6_2atmpS1380;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS220, _M0L3segS222, _M0L1iS221);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS217.$0->$method_0(_M0L6loggerS217.$1, (moonbit_string_t)moonbit_string_literal_32.data);
        _M0L6_2atmpS1379 = _M0L1iS221 + 1;
        _M0L6_2atmpS1380 = _M0L1iS221 + 1;
        _M0L1iS221 = _M0L6_2atmpS1379;
        _M0L3segS222 = _M0L6_2atmpS1380;
        goto _2afor_223;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1381;
        int32_t _M0L6_2atmpS1382;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS220, _M0L3segS222, _M0L1iS221);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS217.$0->$method_0(_M0L6loggerS217.$1, (moonbit_string_t)moonbit_string_literal_33.data);
        _M0L6_2atmpS1381 = _M0L1iS221 + 1;
        _M0L6_2atmpS1382 = _M0L1iS221 + 1;
        _M0L1iS221 = _M0L6_2atmpS1381;
        _M0L3segS222 = _M0L6_2atmpS1382;
        goto _2afor_223;
        break;
      }
      default: {
        if (_M0L4codeS224 < 32) {
          int32_t _M0L6_2atmpS1384;
          moonbit_string_t _M0L6_2atmpS1383;
          int32_t _M0L6_2atmpS1385;
          int32_t _M0L6_2atmpS1386;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS220, _M0L3segS222, _M0L1iS221);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS217.$0->$method_0(_M0L6loggerS217.$1, (moonbit_string_t)moonbit_string_literal_34.data);
          _M0L6_2atmpS1384 = _M0L4codeS224 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1383 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1384);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS217.$0->$method_0(_M0L6loggerS217.$1, _M0L6_2atmpS1383);
          moonbit_decref_cycle_free(_M0L6_2atmpS1383);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS217.$0->$method_0(_M0L6loggerS217.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1385 = _M0L1iS221 + 1;
          _M0L6_2atmpS1386 = _M0L1iS221 + 1;
          _M0L1iS221 = _M0L6_2atmpS1385;
          _M0L3segS222 = _M0L6_2atmpS1386;
          goto _2afor_223;
        } else {
          int32_t _M0L6_2atmpS1387 = _M0L1iS221 + 1;
          int32_t _tmp_2553 = _M0L3segS222;
          _M0L1iS221 = _M0L6_2atmpS1387;
          _M0L3segS222 = _tmp_2553;
          goto _2afor_223;
        }
        break;
      }
    }
    goto joinlet_2552;
    join_225:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS220, _M0L3segS222, _M0L1iS221);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS217.$0->$method_3(_M0L6loggerS217.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1372 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS226);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS217.$0->$method_3(_M0L6loggerS217.$1, _M0L6_2atmpS1372);
    _M0L6_2atmpS1373 = _M0L1iS221 + 1;
    _M0L6_2atmpS1374 = _M0L1iS221 + 1;
    _M0L1iS221 = _M0L6_2atmpS1373;
    _M0L3segS222 = _M0L6_2atmpS1374;
    continue;
    joinlet_2552:;
    break;
  }
  if (_M0L5quoteS216) {
    #line 202 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS217.$0->$method_3(_M0L6loggerS217.$1, 34);
  }
  return 0;
}

int32_t _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS212,
  int32_t _M0L3segS215,
  int32_t _M0L1iS214
) {
  struct _M0TPB6Logger _M0L6loggerS211;
  struct _M0TPC16string10StringView _M0L4selfS213;
  #line 153 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6loggerS211 = _M0L6_2aenvS212->$1;
  _M0L4selfS213 = _M0L6_2aenvS212->$0;
  if (_M0L1iS214 > _M0L3segS215) {
    int64_t _M0L6_2atmpS1371 = (int64_t)_M0L1iS214;
    struct _M0TPC16string10StringView _M0L6_2atmpS1370;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1370
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS213, _M0L3segS215, _M0L6_2atmpS1371);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS211.$0->$method_2(_M0L6loggerS211.$1, _M0L6_2atmpS1370);
    moonbit_decref_cycle_free(_M0L6_2atmpS1370.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS202,
  int32_t _M0L5startS204,
  int64_t _M0L3endS206
) {
  int32_t _M0L3endS1368;
  int32_t _M0L5startS1369;
  int32_t _M0L3lenS201;
  int32_t _M0Lm2loS203;
  int32_t _M0Lm2hiS205;
  moonbit_string_t _M0L3strS209;
  int32_t _M0L4baseS210;
  int32_t _M0L6_2atmpS1346;
  int32_t _if__result_2554;
  int32_t _M0L6_2atmpS1356;
  int32_t _if__result_2555;
  int32_t _M0L6_2atmpS1358;
  int32_t _M0L6_2atmpS1359;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1368 = _M0L4selfS202.$2;
  _M0L5startS1369 = _M0L4selfS202.$1;
  _M0L3lenS201 = _M0L3endS1368 - _M0L5startS1369;
  if (_M0L5startS204 < 0) {
    _M0Lm2loS203 = 0;
  } else if (_M0L5startS204 > _M0L3lenS201) {
    _M0Lm2loS203 = _M0L3lenS201;
  } else {
    _M0Lm2loS203 = _M0L5startS204;
  }
  if (_M0L3endS206 == 4294967296ll) {
    _M0Lm2hiS205 = _M0L3lenS201;
  } else {
    int64_t _M0L7_2aSomeS207 = _M0L3endS206;
    int32_t _M0L4_2aeS208 = (int32_t)_M0L7_2aSomeS207;
    if (_M0L4_2aeS208 < 0) {
      _M0Lm2hiS205 = 0;
    } else if (_M0L4_2aeS208 > _M0L3lenS201) {
      _M0Lm2hiS205 = _M0L3lenS201;
    } else {
      _M0Lm2hiS205 = _M0L4_2aeS208;
    }
  }
  _M0L3strS209 = _M0L4selfS202.$0;
  _M0L4baseS210 = _M0L4selfS202.$1;
  _M0L6_2atmpS1346 = _M0Lm2loS203;
  if (_M0L6_2atmpS1346 > 0) {
    int32_t _M0L6_2atmpS1345 = _M0Lm2loS203;
    if (_M0L6_2atmpS1345 < _M0L3lenS201) {
      int32_t _M0L6_2atmpS1344 = _M0Lm2loS203;
      int32_t _M0L6_2atmpS1343 = _M0L4baseS210 + _M0L6_2atmpS1344;
      int32_t _M0L6_2atmpS1342 = _M0L3strS209[_M0L6_2atmpS1343];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1342)) {
        int32_t _M0L6_2atmpS1341 = _M0Lm2loS203;
        int32_t _M0L6_2atmpS1340 = _M0L4baseS210 + _M0L6_2atmpS1341;
        int32_t _M0L6_2atmpS1339 = _M0L6_2atmpS1340 - 1;
        int32_t _M0L6_2atmpS1338 = _M0L3strS209[_M0L6_2atmpS1339];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2554
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1338);
      } else {
        _if__result_2554 = 0;
      }
    } else {
      _if__result_2554 = 0;
    }
  } else {
    _if__result_2554 = 0;
  }
  if (_if__result_2554) {
    int32_t _M0L6_2atmpS1347 = _M0Lm2loS203;
    _M0Lm2loS203 = _M0L6_2atmpS1347 + 1;
  }
  _M0L6_2atmpS1356 = _M0Lm2hiS205;
  if (_M0L6_2atmpS1356 > 0) {
    int32_t _M0L6_2atmpS1355 = _M0Lm2hiS205;
    if (_M0L6_2atmpS1355 < _M0L3lenS201) {
      int32_t _M0L6_2atmpS1354 = _M0Lm2hiS205;
      int32_t _M0L6_2atmpS1353 = _M0L4baseS210 + _M0L6_2atmpS1354;
      int32_t _M0L6_2atmpS1352 = _M0L3strS209[_M0L6_2atmpS1353];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1352)) {
        int32_t _M0L6_2atmpS1351 = _M0Lm2hiS205;
        int32_t _M0L6_2atmpS1350 = _M0L4baseS210 + _M0L6_2atmpS1351;
        int32_t _M0L6_2atmpS1349 = _M0L6_2atmpS1350 - 1;
        int32_t _M0L6_2atmpS1348 = _M0L3strS209[_M0L6_2atmpS1349];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2555
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1348);
      } else {
        _if__result_2555 = 0;
      }
    } else {
      _if__result_2555 = 0;
    }
  } else {
    _if__result_2555 = 0;
  }
  if (_if__result_2555) {
    int32_t _M0L6_2atmpS1357 = _M0Lm2hiS205;
    _M0Lm2hiS205 = _M0L6_2atmpS1357 - 1;
  }
  _M0L6_2atmpS1358 = _M0Lm2loS203;
  _M0L6_2atmpS1359 = _M0Lm2hiS205;
  if (_M0L6_2atmpS1358 >= _M0L6_2atmpS1359) {
    int32_t _M0L6_2atmpS1363 = _M0Lm2loS203;
    int32_t _M0L6_2atmpS1360 = _M0L4baseS210 + _M0L6_2atmpS1363;
    int32_t _M0L6_2atmpS1362 = _M0Lm2loS203;
    int32_t _M0L6_2atmpS1361 = _M0L4baseS210 + _M0L6_2atmpS1362;
    moonbit_incref_cycle_free(_M0L3strS209);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS209,
                                                 .$1 = _M0L6_2atmpS1360,
                                                 .$2 = _M0L6_2atmpS1361};
  } else {
    int32_t _M0L6_2atmpS1367 = _M0Lm2loS203;
    int32_t _M0L6_2atmpS1364 = _M0L4baseS210 + _M0L6_2atmpS1367;
    int32_t _M0L6_2atmpS1366 = _M0Lm2hiS205;
    int32_t _M0L6_2atmpS1365 = _M0L4baseS210 + _M0L6_2atmpS1366;
    moonbit_incref_cycle_free(_M0L3strS209);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS209,
                                                 .$1 = _M0L6_2atmpS1364,
                                                 .$2 = _M0L6_2atmpS1365};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS200) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS199;
  int32_t _M0L6_2atmpS1335;
  int32_t _M0L6_2atmpS1334;
  int32_t _M0L6_2atmpS1337;
  int32_t _M0L6_2atmpS1336;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1333;
  moonbit_string_t _result_2556;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS199 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1335 = _M0IPC14byte4BytePB3Div3div(_M0L1bS200, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1334
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1335);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS199, _M0L6_2atmpS1334);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1337 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS200, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1336
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1337);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS199, _M0L6_2atmpS1336);
  _M0L6_2atmpS1333 = _M0L7_2aselfS199;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2556 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1333);
  moonbit_decref_cycle_free(_M0L6_2atmpS1333);
  return _result_2556;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS198) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS198 < 10) {
    int32_t _M0L6_2atmpS1330;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1330 = _M0IPC14byte4BytePB3Add3add(_M0L1iS198, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1330);
  } else {
    int32_t _M0L6_2atmpS1332;
    int32_t _M0L6_2atmpS1331;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1332 = _M0IPC14byte4BytePB3Add3add(_M0L1iS198, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1331 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1332, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1331);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS196,
  int32_t _M0L4thatS197
) {
  int32_t _M0L6_2atmpS1328;
  int32_t _M0L6_2atmpS1329;
  int32_t _M0L6_2atmpS1327;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1328 = (int32_t)_M0L4selfS196;
  _M0L6_2atmpS1329 = (int32_t)_M0L4thatS197;
  _M0L6_2atmpS1327 = _M0L6_2atmpS1328 - _M0L6_2atmpS1329;
  return _M0L6_2atmpS1327 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS194,
  int32_t _M0L4thatS195
) {
  int32_t _M0L6_2atmpS1325;
  int32_t _M0L6_2atmpS1326;
  int32_t _M0L6_2atmpS1324;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1325 = (int32_t)_M0L4selfS194;
  _M0L6_2atmpS1326 = (int32_t)_M0L4thatS195;
  _M0L6_2atmpS1324 = _M0L6_2atmpS1325 % _M0L6_2atmpS1326;
  return _M0L6_2atmpS1324 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS192,
  int32_t _M0L4thatS193
) {
  int32_t _M0L6_2atmpS1322;
  int32_t _M0L6_2atmpS1323;
  int32_t _M0L6_2atmpS1321;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1322 = (int32_t)_M0L4selfS192;
  _M0L6_2atmpS1323 = (int32_t)_M0L4thatS193;
  _M0L6_2atmpS1321 = _M0L6_2atmpS1322 / _M0L6_2atmpS1323;
  return _M0L6_2atmpS1321 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS190,
  int32_t _M0L4thatS191
) {
  int32_t _M0L6_2atmpS1319;
  int32_t _M0L6_2atmpS1320;
  int32_t _M0L6_2atmpS1318;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1319 = (int32_t)_M0L4selfS190;
  _M0L6_2atmpS1320 = (int32_t)_M0L4thatS191;
  _M0L6_2atmpS1318 = _M0L6_2atmpS1319 + _M0L6_2atmpS1320;
  return _M0L6_2atmpS1318 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS189) {
  int32_t _M0L6_2atmpS1317;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1317 = (int32_t)_M0L4selfS189;
  return _M0L6_2atmpS1317;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS188) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS188 >= 56320 && _M0L4selfS188 <= 57343;
}

int32_t _M0MPC16uint166UInt1622is__leading__surrogate(int32_t _M0L4selfS187) {
  #line 28 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS187 >= 55296 && _M0L4selfS187 <= 56319;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS186,
  moonbit_string_t _M0L3strS184
) {
  int32_t _M0L8str__lenS183;
  int32_t _M0L3lenS1316;
  int32_t _M0L8requiredS185;
  uint16_t* _M0L4dataS1311;
  int32_t _M0L6_2atmpS1310;
  int32_t _if__result_2557;
  uint16_t* _M0L4dataS1312;
  int32_t _M0L3lenS1313;
  int32_t _M0L3lenS1315;
  int32_t _M0L6_2atmpS1314;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS183 = Moonbit_array_length(_M0L3strS184);
  if (_M0L8str__lenS183 == 0) {
    return 0;
  }
  _M0L3lenS1316 = _M0L4selfS186->$1;
  _M0L8requiredS185 = _M0L3lenS1316 + _M0L8str__lenS183;
  _M0L4dataS1311 = _M0L4selfS186->$0;
  _M0L6_2atmpS1310 = Moonbit_array_length(_M0L4dataS1311);
  if (_M0L8requiredS185 > _M0L6_2atmpS1310) {
    _if__result_2557 = 1;
  } else {
    int32_t _M0L3lenS1309 = _M0L4selfS186->$1;
    _if__result_2557 = _M0L8requiredS185 < _M0L3lenS1309;
  }
  if (_if__result_2557) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS186, _M0L8requiredS185);
  }
  _M0L4dataS1312 = _M0L4selfS186->$0;
  _M0L3lenS1313 = _M0L4selfS186->$1;
  moonbit_incref_cycle_free(_M0L4dataS1312);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1312, _M0L3lenS1313, _M0L3strS184, 0, _M0L8str__lenS183);
  moonbit_decref_cycle_free(_M0L4dataS1312);
  _M0L3lenS1315 = _M0L4selfS186->$1;
  _M0L6_2atmpS1314 = _M0L3lenS1315 + _M0L8str__lenS183;
  _M0L4selfS186->$1 = _M0L6_2atmpS1314;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS179,
  int32_t _M0L11dst__offsetS182,
  moonbit_string_t _M0L3strS180,
  int32_t _M0L11str__offsetS175,
  int32_t _M0L3lenS176
) {
  int32_t _M0L16end__str__offsetS174;
  int32_t _M0L1iS177;
  int32_t _M0L1jS178;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS174 = _M0L11str__offsetS175 + _M0L3lenS176;
  _M0L1iS177 = _M0L11str__offsetS175;
  _M0L1jS178 = _M0L11dst__offsetS182;
  while (1) {
    if (_M0L1iS177 < _M0L16end__str__offsetS174) {
      int32_t _M0L6_2atmpS1306 = _M0L3strS180[_M0L1iS177];
      int32_t _M0L6_2atmpS1307;
      int32_t _M0L6_2atmpS1308;
      _M0L4selfS179[_M0L1jS178] = _M0L6_2atmpS1306;
      _M0L6_2atmpS1307 = _M0L1iS177 + 1;
      _M0L6_2atmpS1308 = _M0L1jS178 + 1;
      _M0L1iS177 = _M0L6_2atmpS1307;
      _M0L1jS178 = _M0L6_2atmpS1308;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS172,
  int32_t _M0L2chS171
) {
  uint32_t _M0L4codeS170;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS170 = _M0MPC14char4Char8to__uint(_M0L2chS171);
  if (_M0L4codeS170 <= 65535u) {
    int32_t _M0L3lenS1277 = _M0L4selfS172->$1;
    uint16_t* _M0L4dataS1279 = _M0L4selfS172->$0;
    int32_t _M0L6_2atmpS1278 = Moonbit_array_length(_M0L4dataS1279);
    uint16_t* _M0L4dataS1282;
    int32_t _M0L3lenS1283;
    int32_t _M0L6_2atmpS1284;
    int32_t _M0L3lenS1286;
    int32_t _M0L6_2atmpS1285;
    if (_M0L3lenS1277 >= _M0L6_2atmpS1278) {
      int32_t _M0L3lenS1281 = _M0L4selfS172->$1;
      int32_t _M0L6_2atmpS1280 = _M0L3lenS1281 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS172, _M0L6_2atmpS1280);
    }
    _M0L4dataS1282 = _M0L4selfS172->$0;
    _M0L3lenS1283 = _M0L4selfS172->$1;
    moonbit_incref_cycle_free(_M0L4dataS1282);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1284 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS170);
    if (
      _M0L3lenS1283 < 0
      || _M0L3lenS1283 >= Moonbit_array_length(_M0L4dataS1282)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1282[_M0L3lenS1283] = _M0L6_2atmpS1284;
    moonbit_decref_cycle_free(_M0L4dataS1282);
    _M0L3lenS1286 = _M0L4selfS172->$1;
    _M0L6_2atmpS1285 = _M0L3lenS1286 + 1;
    _M0L4selfS172->$1 = _M0L6_2atmpS1285;
  } else if (_M0L4codeS170 <= 1114111u) {
    uint16_t* _M0L4dataS1290 = _M0L4selfS172->$0;
    int32_t _M0L6_2atmpS1288 = Moonbit_array_length(_M0L4dataS1290);
    int32_t _M0L3lenS1289 = _M0L4selfS172->$1;
    int32_t _M0L6_2atmpS1287 = _M0L6_2atmpS1288 - _M0L3lenS1289;
    uint32_t _M0L4codeS173;
    uint16_t* _M0L4dataS1293;
    int32_t _M0L3lenS1294;
    uint32_t _M0L6_2atmpS1297;
    uint32_t _M0L6_2atmpS1296;
    int32_t _M0L6_2atmpS1295;
    uint16_t* _M0L4dataS1298;
    int32_t _M0L3lenS1303;
    int32_t _M0L6_2atmpS1299;
    uint32_t _M0L6_2atmpS1302;
    uint32_t _M0L6_2atmpS1301;
    int32_t _M0L6_2atmpS1300;
    int32_t _M0L3lenS1305;
    int32_t _M0L6_2atmpS1304;
    if (_M0L6_2atmpS1287 < 2) {
      int32_t _M0L3lenS1292 = _M0L4selfS172->$1;
      int32_t _M0L6_2atmpS1291 = _M0L3lenS1292 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS172, _M0L6_2atmpS1291);
    }
    _M0L4codeS173 = _M0L4codeS170 - 65536u;
    _M0L4dataS1293 = _M0L4selfS172->$0;
    _M0L3lenS1294 = _M0L4selfS172->$1;
    _M0L6_2atmpS1297 = _M0L4codeS173 >> 10;
    _M0L6_2atmpS1296 = 55296u + _M0L6_2atmpS1297;
    moonbit_incref_cycle_free(_M0L4dataS1293);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1295 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1296);
    if (
      _M0L3lenS1294 < 0
      || _M0L3lenS1294 >= Moonbit_array_length(_M0L4dataS1293)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1293[_M0L3lenS1294] = _M0L6_2atmpS1295;
    moonbit_decref_cycle_free(_M0L4dataS1293);
    _M0L4dataS1298 = _M0L4selfS172->$0;
    _M0L3lenS1303 = _M0L4selfS172->$1;
    _M0L6_2atmpS1299 = _M0L3lenS1303 + 1;
    _M0L6_2atmpS1302 = _M0L4codeS173 & 1023u;
    _M0L6_2atmpS1301 = 56320u + _M0L6_2atmpS1302;
    moonbit_incref_cycle_free(_M0L4dataS1298);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1300 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1301);
    if (
      _M0L6_2atmpS1299 < 0
      || _M0L6_2atmpS1299 >= Moonbit_array_length(_M0L4dataS1298)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1298[_M0L6_2atmpS1299] = _M0L6_2atmpS1300;
    moonbit_decref_cycle_free(_M0L4dataS1298);
    _M0L3lenS1305 = _M0L4selfS172->$1;
    _M0L6_2atmpS1304 = _M0L3lenS1305 + 2;
    _M0L4selfS172->$1 = _M0L6_2atmpS1304;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_35.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS167,
  int32_t _M0L8requiredS168
) {
  uint16_t* _M0L4dataS1276;
  int32_t _M0L6_2atmpS1274;
  int32_t _M0L3lenS1275;
  int32_t _M0L13new__capacityS166;
  uint16_t* _M0L4dataS1271;
  int32_t _M0L6_2atmpS1272;
  int32_t _M0L3lenS1273;
  uint16_t* _M0L9new__dataS169;
  uint16_t* _M0L6_2aoldS2426;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1276 = _M0L4selfS167->$0;
  _M0L6_2atmpS1274 = Moonbit_array_length(_M0L4dataS1276);
  _M0L3lenS1275 = _M0L4selfS167->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS166
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1274, _M0L3lenS1275, _M0L8requiredS168);
  _M0L4dataS1271 = _M0L4selfS167->$0;
  moonbit_incref_cycle_free(_M0L4dataS1271);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1272 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1273 = _M0L4selfS167->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS169
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1271, _M0L13new__capacityS166, _M0L6_2atmpS1272, _M0L3lenS1273, 0, 0);
  _M0L6_2aoldS2426 = _M0L4selfS167->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2426);
  _M0L4selfS167->$0 = _M0L9new__dataS169;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS165,
  int32_t _M0L3lenS161,
  int32_t _M0L8requiredS160
) {
  int32_t _M0L5spaceS162;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS160 < _M0L3lenS161) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_36.data);
  }
  _M0L5spaceS162 = _M0L7currentS165;
  while (1) {
    if (_M0L5spaceS162 < _M0L8requiredS160) {
      int32_t _M0L4nextS163 = _M0L5spaceS162 * 2;
      if (_M0L4nextS163 <= _M0L5spaceS162) {
        return _M0L8requiredS160;
      }
      _M0L5spaceS162 = _M0L4nextS163;
      continue;
    } else {
      return _M0L5spaceS162;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS159) {
  int32_t _M0L6_2atmpS1270;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1270 = *(int32_t*)&_M0L4selfS159;
  return (uint16_t)_M0L6_2atmpS1270;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS158) {
  int32_t _M0L6_2atmpS1269;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1269 = _M0L4selfS158;
  return *(uint32_t*)&_M0L6_2atmpS1269;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS156
) {
  int32_t _M0L3lenS1260;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1260 = _M0L4selfS156->$1;
  if (_M0L3lenS1260 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1261 = _M0L4selfS156->$1;
    uint16_t* _M0L4dataS1263 = _M0L4selfS156->$0;
    int32_t _M0L6_2atmpS1262 = Moonbit_array_length(_M0L4dataS1263);
    if (_M0L3lenS1261 == _M0L6_2atmpS1262) {
      uint16_t* _M0L4dataS1264 = _M0L4selfS156->$0;
      moonbit_incref_cycle_free(_M0L4dataS1264);
      return _M0L4dataS1264;
    } else {
      uint16_t* _M0L4dataS1265 = _M0L4selfS156->$0;
      int32_t _M0L3lenS1266 = _M0L4selfS156->$1;
      int32_t _M0L6_2atmpS1267;
      int32_t _M0L3lenS1268;
      uint16_t* _M0L4dataS157;
      moonbit_incref_cycle_free(_M0L4dataS1265);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1267 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1268 = _M0L4selfS156->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS157
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1265, _M0L3lenS1266, _M0L6_2atmpS1267, _M0L3lenS1268, 0, 0);
      return _M0L4dataS157;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS153,
  int32_t _M0L13allocate__lenS149,
  int32_t _M0L4initS154,
  int32_t _M0L3lenS150,
  int32_t _M0L11src__offsetS151,
  int32_t _M0L11dst__offsetS152
) {
  int32_t _if__result_2560;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS149 >= 0) {
    if (_M0L3lenS150 >= 0) {
      if (_M0L11src__offsetS151 >= 0) {
        if (_M0L11dst__offsetS152 >= 0) {
          int32_t _M0L6_2atmpS1256 = _M0L11src__offsetS151 + _M0L3lenS150;
          int32_t _M0L6_2atmpS1257 = Moonbit_array_length(_M0L3srcS153);
          if (_M0L6_2atmpS1256 <= _M0L6_2atmpS1257) {
            int32_t _M0L6_2atmpS1255 = _M0L11dst__offsetS152 + _M0L3lenS150;
            _if__result_2560 = _M0L6_2atmpS1255 <= _M0L13allocate__lenS149;
          } else {
            _if__result_2560 = 0;
          }
        } else {
          _if__result_2560 = 0;
        }
      } else {
        _if__result_2560 = 0;
      }
    } else {
      _if__result_2560 = 0;
    }
  } else {
    _if__result_2560 = 0;
  }
  if (_if__result_2560) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS153, _M0L13allocate__lenS149, _M0L4initS154, _M0L11src__offsetS151, _M0L11dst__offsetS152, _M0L3lenS150);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS155;
    int32_t _M0L6_2atmpS1259;
    moonbit_string_t _M0L6_2atmpS1258;
    uint16_t* _result_2561;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS155
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS155, (moonbit_string_t)moonbit_string_literal_37.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS155, _M0L13allocate__lenS149);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS155, (moonbit_string_t)moonbit_string_literal_38.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS155, _M0L11src__offsetS151);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS155, (moonbit_string_t)moonbit_string_literal_39.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS155, _M0L11dst__offsetS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS155, (moonbit_string_t)moonbit_string_literal_40.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS155, _M0L3lenS150);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS155, (moonbit_string_t)moonbit_string_literal_41.data);
    _M0L6_2atmpS1259 = Moonbit_array_length(_M0L3srcS153);
    moonbit_decref_cycle_free(_M0L3srcS153);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS155, _M0L6_2atmpS1259);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1258
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS155);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS155);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2561 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1258);
    moonbit_decref_cycle_free(_M0L6_2atmpS1258);
    return _result_2561;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS146,
  int32_t _M0L13allocate__lenS143,
  int32_t _M0L4initS144,
  int32_t _M0L11src__offsetS147,
  int32_t _M0L11dst__offsetS145,
  int32_t _M0L9blit__lenS148
) {
  uint16_t* _M0L3dstS142;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS142
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS143, _M0L4initS144);
  moonbit_incref_cycle_free(_M0L3dstS142);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS142, _M0L11dst__offsetS145, _M0L3srcS146, _M0L11src__offsetS147, _M0L9blit__lenS148, sizeof(uint16_t));
  return _M0L3dstS142;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS140
) {
  int32_t _M0L7initialS139;
  uint16_t* _M0L4dataS141;
  struct _M0TPB13StringBuilder* _block_2562;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS140 < 1) {
    _M0L7initialS139 = 1;
  } else {
    int32_t _M0L6_2atmpS1254 = _M0L10size__hintS140 + 1;
    _M0L7initialS139 = _M0L6_2atmpS1254 / 2;
  }
  _M0L4dataS141 = (uint16_t*)moonbit_make_string(_M0L7initialS139, 0);
  _block_2562
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2562)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 76, 0);
  _block_2562->$0 = _M0L4dataS141;
  _block_2562->$1 = 0;
  return _block_2562;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS138) {
  int32_t _M0L6_2atmpS1253;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1253 = (int32_t)_M0L4selfS138;
  return _M0L6_2atmpS1253;
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS118,
  int32_t _M0L13allocate__lenS114,
  int32_t _M0L3lenS115,
  int32_t _M0L11src__offsetS116,
  int32_t _M0L11dst__offsetS117
) {
  int32_t _if__result_2563;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS114 >= 0) {
    if (_M0L3lenS115 >= 0) {
      if (_M0L11src__offsetS116 >= 0) {
        if (_M0L11dst__offsetS117 >= 0) {
          int32_t _M0L6_2atmpS1234 = _M0L11src__offsetS116 + _M0L3lenS115;
          int32_t _M0L6_2atmpS1235;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1235
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS118);
          if (_M0L6_2atmpS1234 <= _M0L6_2atmpS1235) {
            int32_t _M0L6_2atmpS1233 = _M0L11dst__offsetS117 + _M0L3lenS115;
            _if__result_2563 = _M0L6_2atmpS1233 <= _M0L13allocate__lenS114;
          } else {
            _if__result_2563 = 0;
          }
        } else {
          _if__result_2563 = 0;
        }
      } else {
        _if__result_2563 = 0;
      }
    } else {
      _if__result_2563 = 0;
    }
  } else {
    _if__result_2563 = 0;
  }
  if (_if__result_2563) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS118, _M0L13allocate__lenS114, _M0L11src__offsetS116, _M0L11dst__offsetS117, _M0L3lenS115);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS119;
    int32_t _M0L6_2atmpS1237;
    moonbit_string_t _M0L6_2atmpS1236;
    int32_t* _result_2564;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS119
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS119, (moonbit_string_t)moonbit_string_literal_37.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS119, _M0L13allocate__lenS114);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS119, (moonbit_string_t)moonbit_string_literal_38.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS119, _M0L11src__offsetS116);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS119, (moonbit_string_t)moonbit_string_literal_39.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS119, _M0L11dst__offsetS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS119, (moonbit_string_t)moonbit_string_literal_40.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS119, _M0L3lenS115);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS119, (moonbit_string_t)moonbit_string_literal_41.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1237 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS118);
    moonbit_decref_cycle_free(_M0L3srcS118);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS119, _M0L6_2atmpS1237);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1236
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS119);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS119);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2564
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1236);
    moonbit_decref_cycle_free(_M0L6_2atmpS1236);
    return _result_2564;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS124,
  int32_t _M0L13allocate__lenS120,
  int32_t _M0L3lenS121,
  int32_t _M0L11src__offsetS122,
  int32_t _M0L11dst__offsetS123
) {
  int32_t _if__result_2565;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS120 >= 0) {
    if (_M0L3lenS121 >= 0) {
      if (_M0L11src__offsetS122 >= 0) {
        if (_M0L11dst__offsetS123 >= 0) {
          int32_t _M0L6_2atmpS1239 = _M0L11src__offsetS122 + _M0L3lenS121;
          int32_t _M0L6_2atmpS1240;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1240
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS124);
          if (_M0L6_2atmpS1239 <= _M0L6_2atmpS1240) {
            int32_t _M0L6_2atmpS1238 = _M0L11dst__offsetS123 + _M0L3lenS121;
            _if__result_2565 = _M0L6_2atmpS1238 <= _M0L13allocate__lenS120;
          } else {
            _if__result_2565 = 0;
          }
        } else {
          _if__result_2565 = 0;
        }
      } else {
        _if__result_2565 = 0;
      }
    } else {
      _if__result_2565 = 0;
    }
  } else {
    _if__result_2565 = 0;
  }
  if (_if__result_2565) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS124, _M0L13allocate__lenS120, _M0L11src__offsetS122, _M0L11dst__offsetS123, _M0L3lenS121);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS125;
    int32_t _M0L6_2atmpS1242;
    moonbit_string_t _M0L6_2atmpS1241;
    float* _result_2566;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS125
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS125, (moonbit_string_t)moonbit_string_literal_37.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS125, _M0L13allocate__lenS120);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS125, (moonbit_string_t)moonbit_string_literal_38.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS125, _M0L11src__offsetS122);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS125, (moonbit_string_t)moonbit_string_literal_39.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS125, _M0L11dst__offsetS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS125, (moonbit_string_t)moonbit_string_literal_40.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS125, _M0L3lenS121);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS125, (moonbit_string_t)moonbit_string_literal_41.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1242 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS124);
    moonbit_decref_cycle_free(_M0L3srcS124);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS125, _M0L6_2atmpS1242);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1241
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS125);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS125);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2566
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1241);
    moonbit_decref_cycle_free(_M0L6_2atmpS1241);
    return _result_2566;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS130,
  int32_t _M0L13allocate__lenS126,
  int32_t _M0L3lenS127,
  int32_t _M0L11src__offsetS128,
  int32_t _M0L11dst__offsetS129
) {
  int32_t _if__result_2567;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS126 >= 0) {
    if (_M0L3lenS127 >= 0) {
      if (_M0L11src__offsetS128 >= 0) {
        if (_M0L11dst__offsetS129 >= 0) {
          int32_t _M0L6_2atmpS1244 = _M0L11src__offsetS128 + _M0L3lenS127;
          int32_t _M0L6_2atmpS1245;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1245
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS130);
          if (_M0L6_2atmpS1244 <= _M0L6_2atmpS1245) {
            int32_t _M0L6_2atmpS1243 = _M0L11dst__offsetS129 + _M0L3lenS127;
            _if__result_2567 = _M0L6_2atmpS1243 <= _M0L13allocate__lenS126;
          } else {
            _if__result_2567 = 0;
          }
        } else {
          _if__result_2567 = 0;
        }
      } else {
        _if__result_2567 = 0;
      }
    } else {
      _if__result_2567 = 0;
    }
  } else {
    _if__result_2567 = 0;
  }
  if (_if__result_2567) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS126, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS130, _M0L11src__offsetS128, _M0L11dst__offsetS129, _M0L3lenS127);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS131;
    int32_t _M0L6_2atmpS1247;
    moonbit_string_t _M0L6_2atmpS1246;
    moonbit_string_t* _result_2568;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS131
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS131, (moonbit_string_t)moonbit_string_literal_37.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS131, _M0L13allocate__lenS126);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS131, (moonbit_string_t)moonbit_string_literal_38.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS131, _M0L11src__offsetS128);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS131, (moonbit_string_t)moonbit_string_literal_39.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS131, _M0L11dst__offsetS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS131, (moonbit_string_t)moonbit_string_literal_40.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS131, _M0L3lenS127);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS131, (moonbit_string_t)moonbit_string_literal_41.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1247 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS130);
    moonbit_decref_cycle_free(_M0L3srcS130);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS131, _M0L6_2atmpS1247);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1246
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS131);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS131);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2568
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1246);
    moonbit_decref_cycle_free(_M0L6_2atmpS1246);
    return _result_2568;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS136,
  int32_t _M0L13allocate__lenS132,
  int32_t _M0L3lenS133,
  int32_t _M0L11src__offsetS134,
  int32_t _M0L11dst__offsetS135
) {
  int32_t _if__result_2569;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS132 >= 0) {
    if (_M0L3lenS133 >= 0) {
      if (_M0L11src__offsetS134 >= 0) {
        if (_M0L11dst__offsetS135 >= 0) {
          int32_t _M0L6_2atmpS1249 = _M0L11src__offsetS134 + _M0L3lenS133;
          int32_t _M0L6_2atmpS1250;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1250
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS136);
          if (_M0L6_2atmpS1249 <= _M0L6_2atmpS1250) {
            int32_t _M0L6_2atmpS1248 = _M0L11dst__offsetS135 + _M0L3lenS133;
            _if__result_2569 = _M0L6_2atmpS1248 <= _M0L13allocate__lenS132;
          } else {
            _if__result_2569 = 0;
          }
        } else {
          _if__result_2569 = 0;
        }
      } else {
        _if__result_2569 = 0;
      }
    } else {
      _if__result_2569 = 0;
    }
  } else {
    _if__result_2569 = 0;
  }
  if (_if__result_2569) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS132, 0, _M0L3srcS136, _M0L11src__offsetS134, _M0L11dst__offsetS135, _M0L3lenS133);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS137;
    int32_t _M0L6_2atmpS1252;
    moonbit_string_t _M0L6_2atmpS1251;
    struct _M0TUsiE** _result_2570;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS137
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS137, (moonbit_string_t)moonbit_string_literal_37.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS137, _M0L13allocate__lenS132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS137, (moonbit_string_t)moonbit_string_literal_38.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS137, _M0L11src__offsetS134);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS137, (moonbit_string_t)moonbit_string_literal_39.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS137, _M0L11dst__offsetS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS137, (moonbit_string_t)moonbit_string_literal_40.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS137, _M0L3lenS133);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS137, (moonbit_string_t)moonbit_string_literal_41.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1252 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS136);
    moonbit_decref_cycle_free(_M0L3srcS136);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS137, _M0L6_2atmpS1252);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1251
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS137);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS137);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2570
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1251);
    moonbit_decref_cycle_free(_M0L6_2atmpS1251);
    return _result_2570;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS109,
  moonbit_string_t _M0L3objS108
) {
  struct _M0TPB6Logger _M0L6_2atmpS1230;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS109);
  _M0L6_2atmpS1230
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS109
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS108, _M0L6_2atmpS1230);
  if (_M0L6_2atmpS1230.$1) {
    moonbit_decref(_M0L6_2atmpS1230.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS111,
  int32_t _M0L3objS110
) {
  struct _M0TPB6Logger _M0L6_2atmpS1231;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS111);
  _M0L6_2atmpS1231
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS111
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS110, _M0L6_2atmpS1231);
  if (_M0L6_2atmpS1231.$1) {
    moonbit_decref(_M0L6_2atmpS1231.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS113,
  uint64_t _M0L3objS112
) {
  struct _M0TPB6Logger _M0L6_2atmpS1232;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS113);
  _M0L6_2atmpS1232
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS113
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS112, _M0L6_2atmpS1232);
  if (_M0L6_2atmpS1232.$1) {
    moonbit_decref(_M0L6_2atmpS1232.$1);
  }
  return 0;
}

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t* _M0L3srcS87,
  int32_t _M0L13allocate__lenS85,
  int32_t _M0L11src__offsetS88,
  int32_t _M0L11dst__offsetS86,
  int32_t _M0L9blit__lenS89
) {
  int32_t* _M0L3dstS84;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS84
  = (int32_t*)moonbit_make_int32_array_raw(_M0L13allocate__lenS85);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L3dstS84, _M0L11dst__offsetS86, _M0L3srcS87, _M0L11src__offsetS88, _M0L9blit__lenS89);
  moonbit_decref_cycle_free(_M0L3srcS87);
  return _M0L3dstS84;
}

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS93,
  int32_t _M0L13allocate__lenS91,
  int32_t _M0L11src__offsetS94,
  int32_t _M0L11dst__offsetS92,
  int32_t _M0L9blit__lenS95
) {
  float* _M0L3dstS90;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS90 = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS91);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS90, _M0L11dst__offsetS92, _M0L3srcS93, _M0L11src__offsetS94, _M0L9blit__lenS95);
  moonbit_decref_cycle_free(_M0L3srcS93);
  return _M0L3dstS90;
}

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t* _M0L3srcS99,
  int32_t _M0L13allocate__lenS97,
  int32_t _M0L11src__offsetS100,
  int32_t _M0L11dst__offsetS98,
  int32_t _M0L9blit__lenS101
) {
  moonbit_string_t* _M0L3dstS96;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS96
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L13allocate__lenS97, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGsE(_M0L3dstS96, _M0L11dst__offsetS98, _M0L3srcS99, _M0L11src__offsetS100, _M0L9blit__lenS101);
  moonbit_decref_cycle_free(_M0L3srcS99);
  return _M0L3dstS96;
}

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE** _M0L3srcS105,
  int32_t _M0L13allocate__lenS103,
  int32_t _M0L11src__offsetS106,
  int32_t _M0L11dst__offsetS104,
  int32_t _M0L9blit__lenS107
) {
  struct _M0TUsiE** _M0L3dstS102;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS102
  = (struct _M0TUsiE**)moonbit_make_ref_array(_M0L13allocate__lenS103, 0);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGUsiEE(_M0L3dstS102, _M0L11dst__offsetS104, _M0L3srcS105, _M0L11src__offsetS106, _M0L9blit__lenS107);
  moonbit_decref_cycle_free(_M0L3srcS105);
  return _M0L3dstS102;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t* _M0L3dstS64,
  int32_t _M0L11dst__offsetS65,
  int32_t* _M0L3srcS66,
  int32_t _M0L11src__offsetS67,
  int32_t _M0L3lenS68
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS66);
  moonbit_incref_cycle_free(_M0L3dstS64);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS64, _M0L11dst__offsetS65, _M0L3srcS66, _M0L11src__offsetS67, _M0L3lenS68, sizeof(int32_t));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS69,
  int32_t _M0L11dst__offsetS70,
  float* _M0L3srcS71,
  int32_t _M0L11src__offsetS72,
  int32_t _M0L3lenS73
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS71);
  moonbit_incref_cycle_free(_M0L3dstS69);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS69, _M0L11dst__offsetS70, _M0L3srcS71, _M0L11src__offsetS72, _M0L3lenS73, sizeof(float));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS74,
  int32_t _M0L11dst__offsetS75,
  moonbit_string_t* _M0L3srcS76,
  int32_t _M0L11src__offsetS77,
  int32_t _M0L3lenS78
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS76);
  moonbit_incref_cycle_free(_M0L3dstS74);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS74, _M0L11dst__offsetS75, _M0L3srcS76, _M0L11src__offsetS77, _M0L3lenS78);
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS79,
  int32_t _M0L11dst__offsetS80,
  struct _M0TUsiE** _M0L3srcS81,
  int32_t _M0L11src__offsetS82,
  int32_t _M0L3lenS83
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS81);
  moonbit_incref_cycle_free(_M0L3dstS79);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS79, _M0L11dst__offsetS80, _M0L3srcS81, _M0L11src__offsetS82, _M0L3lenS83);
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t* _M0L3dstS19,
  int32_t _M0L11dst__offsetS21,
  uint16_t* _M0L3srcS20,
  int32_t _M0L11src__offsetS22,
  int32_t _M0L3lenS24
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS19 == _M0L3srcS20 && _M0L11dst__offsetS21 < _M0L11src__offsetS22
  ) {
    int32_t _M0L1iS23 = 0;
    while (1) {
      if (_M0L1iS23 < _M0L3lenS24) {
        int32_t _M0L6_2atmpS1185 = _M0L11dst__offsetS21 + _M0L1iS23;
        int32_t _M0L6_2atmpS1187 = _M0L11src__offsetS22 + _M0L1iS23;
        int32_t _M0L6_2atmpS1186;
        int32_t _M0L6_2atmpS1188;
        if (
          _M0L6_2atmpS1187 < 0
          || _M0L6_2atmpS1187 >= Moonbit_array_length(_M0L3srcS20)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1186 = (int32_t)_M0L3srcS20[_M0L6_2atmpS1187];
        if (
          _M0L6_2atmpS1185 < 0
          || _M0L6_2atmpS1185 >= Moonbit_array_length(_M0L3dstS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS19[_M0L6_2atmpS1185] = _M0L6_2atmpS1186;
        _M0L6_2atmpS1188 = _M0L1iS23 + 1;
        _M0L1iS23 = _M0L6_2atmpS1188;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS20);
        moonbit_decref_cycle_free(_M0L3dstS19);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1193 = _M0L3lenS24 - 1;
    int32_t _M0L1iS26 = _M0L6_2atmpS1193;
    while (1) {
      if (_M0L1iS26 >= 0) {
        int32_t _M0L6_2atmpS1189 = _M0L11dst__offsetS21 + _M0L1iS26;
        int32_t _M0L6_2atmpS1191 = _M0L11src__offsetS22 + _M0L1iS26;
        int32_t _M0L6_2atmpS1190;
        int32_t _M0L6_2atmpS1192;
        if (
          _M0L6_2atmpS1191 < 0
          || _M0L6_2atmpS1191 >= Moonbit_array_length(_M0L3srcS20)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1190 = (int32_t)_M0L3srcS20[_M0L6_2atmpS1191];
        if (
          _M0L6_2atmpS1189 < 0
          || _M0L6_2atmpS1189 >= Moonbit_array_length(_M0L3dstS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS19[_M0L6_2atmpS1189] = _M0L6_2atmpS1190;
        _M0L6_2atmpS1192 = _M0L1iS26 - 1;
        _M0L1iS26 = _M0L6_2atmpS1192;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS20);
        moonbit_decref_cycle_free(_M0L3dstS19);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS28,
  int32_t _M0L11dst__offsetS30,
  int32_t* _M0L3srcS29,
  int32_t _M0L11src__offsetS31,
  int32_t _M0L3lenS33
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS28 == _M0L3srcS29 && _M0L11dst__offsetS30 < _M0L11src__offsetS31
  ) {
    int32_t _M0L1iS32 = 0;
    while (1) {
      if (_M0L1iS32 < _M0L3lenS33) {
        int32_t _M0L6_2atmpS1194 = _M0L11dst__offsetS30 + _M0L1iS32;
        int32_t _M0L6_2atmpS1196 = _M0L11src__offsetS31 + _M0L1iS32;
        int32_t _M0L6_2atmpS1195;
        int32_t _M0L6_2atmpS1197;
        if (
          _M0L6_2atmpS1196 < 0
          || _M0L6_2atmpS1196 >= Moonbit_array_length(_M0L3srcS29)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1195 = (int32_t)_M0L3srcS29[_M0L6_2atmpS1196];
        if (
          _M0L6_2atmpS1194 < 0
          || _M0L6_2atmpS1194 >= Moonbit_array_length(_M0L3dstS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS28[_M0L6_2atmpS1194] = _M0L6_2atmpS1195;
        _M0L6_2atmpS1197 = _M0L1iS32 + 1;
        _M0L1iS32 = _M0L6_2atmpS1197;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS29);
        moonbit_decref_cycle_free(_M0L3dstS28);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1202 = _M0L3lenS33 - 1;
    int32_t _M0L1iS35 = _M0L6_2atmpS1202;
    while (1) {
      if (_M0L1iS35 >= 0) {
        int32_t _M0L6_2atmpS1198 = _M0L11dst__offsetS30 + _M0L1iS35;
        int32_t _M0L6_2atmpS1200 = _M0L11src__offsetS31 + _M0L1iS35;
        int32_t _M0L6_2atmpS1199;
        int32_t _M0L6_2atmpS1201;
        if (
          _M0L6_2atmpS1200 < 0
          || _M0L6_2atmpS1200 >= Moonbit_array_length(_M0L3srcS29)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1199 = (int32_t)_M0L3srcS29[_M0L6_2atmpS1200];
        if (
          _M0L6_2atmpS1198 < 0
          || _M0L6_2atmpS1198 >= Moonbit_array_length(_M0L3dstS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS28[_M0L6_2atmpS1198] = _M0L6_2atmpS1199;
        _M0L6_2atmpS1201 = _M0L1iS35 - 1;
        _M0L1iS35 = _M0L6_2atmpS1201;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS29);
        moonbit_decref_cycle_free(_M0L3dstS28);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS37,
  int32_t _M0L11dst__offsetS39,
  float* _M0L3srcS38,
  int32_t _M0L11src__offsetS40,
  int32_t _M0L3lenS42
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS37 == _M0L3srcS38 && _M0L11dst__offsetS39 < _M0L11src__offsetS40
  ) {
    int32_t _M0L1iS41 = 0;
    while (1) {
      if (_M0L1iS41 < _M0L3lenS42) {
        int32_t _M0L6_2atmpS1203 = _M0L11dst__offsetS39 + _M0L1iS41;
        int32_t _M0L6_2atmpS1205 = _M0L11src__offsetS40 + _M0L1iS41;
        float _M0L6_2atmpS1204;
        int32_t _M0L6_2atmpS1206;
        if (
          _M0L6_2atmpS1205 < 0
          || _M0L6_2atmpS1205 >= Moonbit_array_length(_M0L3srcS38)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1204 = (float)_M0L3srcS38[_M0L6_2atmpS1205];
        if (
          _M0L6_2atmpS1203 < 0
          || _M0L6_2atmpS1203 >= Moonbit_array_length(_M0L3dstS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS37[_M0L6_2atmpS1203] = _M0L6_2atmpS1204;
        _M0L6_2atmpS1206 = _M0L1iS41 + 1;
        _M0L1iS41 = _M0L6_2atmpS1206;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS38);
        moonbit_decref_cycle_free(_M0L3dstS37);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1211 = _M0L3lenS42 - 1;
    int32_t _M0L1iS44 = _M0L6_2atmpS1211;
    while (1) {
      if (_M0L1iS44 >= 0) {
        int32_t _M0L6_2atmpS1207 = _M0L11dst__offsetS39 + _M0L1iS44;
        int32_t _M0L6_2atmpS1209 = _M0L11src__offsetS40 + _M0L1iS44;
        float _M0L6_2atmpS1208;
        int32_t _M0L6_2atmpS1210;
        if (
          _M0L6_2atmpS1209 < 0
          || _M0L6_2atmpS1209 >= Moonbit_array_length(_M0L3srcS38)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1208 = (float)_M0L3srcS38[_M0L6_2atmpS1209];
        if (
          _M0L6_2atmpS1207 < 0
          || _M0L6_2atmpS1207 >= Moonbit_array_length(_M0L3dstS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS37[_M0L6_2atmpS1207] = _M0L6_2atmpS1208;
        _M0L6_2atmpS1210 = _M0L1iS44 - 1;
        _M0L1iS44 = _M0L6_2atmpS1210;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS38);
        moonbit_decref_cycle_free(_M0L3dstS37);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS46,
  int32_t _M0L11dst__offsetS48,
  moonbit_string_t* _M0L3srcS47,
  int32_t _M0L11src__offsetS49,
  int32_t _M0L3lenS51
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS46 == _M0L3srcS47 && _M0L11dst__offsetS48 < _M0L11src__offsetS49
  ) {
    int32_t _M0L1iS50 = 0;
    while (1) {
      if (_M0L1iS50 < _M0L3lenS51) {
        int32_t _M0L6_2atmpS1212 = _M0L11dst__offsetS48 + _M0L1iS50;
        int32_t _M0L6_2atmpS1214 = _M0L11src__offsetS49 + _M0L1iS50;
        moonbit_string_t _M0L6_2atmpS1213;
        moonbit_string_t _M0L6_2aoldS2427;
        int32_t _M0L6_2atmpS1215;
        if (
          _M0L6_2atmpS1214 < 0
          || _M0L6_2atmpS1214 >= Moonbit_array_length(_M0L3srcS47)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1213 = (moonbit_string_t)_M0L3srcS47[_M0L6_2atmpS1214];
        if (
          _M0L6_2atmpS1212 < 0
          || _M0L6_2atmpS1212 >= Moonbit_array_length(_M0L3dstS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2427 = (moonbit_string_t)_M0L3dstS46[_M0L6_2atmpS1212];
        moonbit_incref_cycle_free(_M0L6_2atmpS1213);
        moonbit_decref_cycle_free(_M0L6_2aoldS2427);
        _M0L3dstS46[_M0L6_2atmpS1212] = _M0L6_2atmpS1213;
        _M0L6_2atmpS1215 = _M0L1iS50 + 1;
        _M0L1iS50 = _M0L6_2atmpS1215;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS47);
        moonbit_decref_cycle_free(_M0L3dstS46);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1220 = _M0L3lenS51 - 1;
    int32_t _M0L1iS53 = _M0L6_2atmpS1220;
    while (1) {
      if (_M0L1iS53 >= 0) {
        int32_t _M0L6_2atmpS1216 = _M0L11dst__offsetS48 + _M0L1iS53;
        int32_t _M0L6_2atmpS1218 = _M0L11src__offsetS49 + _M0L1iS53;
        moonbit_string_t _M0L6_2atmpS1217;
        moonbit_string_t _M0L6_2aoldS2428;
        int32_t _M0L6_2atmpS1219;
        if (
          _M0L6_2atmpS1218 < 0
          || _M0L6_2atmpS1218 >= Moonbit_array_length(_M0L3srcS47)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1217 = (moonbit_string_t)_M0L3srcS47[_M0L6_2atmpS1218];
        if (
          _M0L6_2atmpS1216 < 0
          || _M0L6_2atmpS1216 >= Moonbit_array_length(_M0L3dstS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2428 = (moonbit_string_t)_M0L3dstS46[_M0L6_2atmpS1216];
        moonbit_incref_cycle_free(_M0L6_2atmpS1217);
        moonbit_decref_cycle_free(_M0L6_2aoldS2428);
        _M0L3dstS46[_M0L6_2atmpS1216] = _M0L6_2atmpS1217;
        _M0L6_2atmpS1219 = _M0L1iS53 - 1;
        _M0L1iS53 = _M0L6_2atmpS1219;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS47);
        moonbit_decref_cycle_free(_M0L3dstS46);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS55,
  int32_t _M0L11dst__offsetS57,
  struct _M0TUsiE** _M0L3srcS56,
  int32_t _M0L11src__offsetS58,
  int32_t _M0L3lenS60
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS55 == _M0L3srcS56 && _M0L11dst__offsetS57 < _M0L11src__offsetS58
  ) {
    int32_t _M0L1iS59 = 0;
    while (1) {
      if (_M0L1iS59 < _M0L3lenS60) {
        int32_t _M0L6_2atmpS1221 = _M0L11dst__offsetS57 + _M0L1iS59;
        int32_t _M0L6_2atmpS1223 = _M0L11src__offsetS58 + _M0L1iS59;
        struct _M0TUsiE* _M0L6_2atmpS1222;
        struct _M0TUsiE* _M0L6_2aoldS2429;
        int32_t _M0L6_2atmpS1224;
        if (
          _M0L6_2atmpS1223 < 0
          || _M0L6_2atmpS1223 >= Moonbit_array_length(_M0L3srcS56)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1222 = (struct _M0TUsiE*)_M0L3srcS56[_M0L6_2atmpS1223];
        if (
          _M0L6_2atmpS1221 < 0
          || _M0L6_2atmpS1221 >= Moonbit_array_length(_M0L3dstS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2429 = (struct _M0TUsiE*)_M0L3dstS55[_M0L6_2atmpS1221];
        if (_M0L6_2atmpS1222) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1222);
        }
        if (_M0L6_2aoldS2429) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2429);
        }
        _M0L3dstS55[_M0L6_2atmpS1221] = _M0L6_2atmpS1222;
        _M0L6_2atmpS1224 = _M0L1iS59 + 1;
        _M0L1iS59 = _M0L6_2atmpS1224;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS56);
        moonbit_decref_cycle_free(_M0L3dstS55);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1229 = _M0L3lenS60 - 1;
    int32_t _M0L1iS62 = _M0L6_2atmpS1229;
    while (1) {
      if (_M0L1iS62 >= 0) {
        int32_t _M0L6_2atmpS1225 = _M0L11dst__offsetS57 + _M0L1iS62;
        int32_t _M0L6_2atmpS1227 = _M0L11src__offsetS58 + _M0L1iS62;
        struct _M0TUsiE* _M0L6_2atmpS1226;
        struct _M0TUsiE* _M0L6_2aoldS2430;
        int32_t _M0L6_2atmpS1228;
        if (
          _M0L6_2atmpS1227 < 0
          || _M0L6_2atmpS1227 >= Moonbit_array_length(_M0L3srcS56)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1226 = (struct _M0TUsiE*)_M0L3srcS56[_M0L6_2atmpS1227];
        if (
          _M0L6_2atmpS1225 < 0
          || _M0L6_2atmpS1225 >= Moonbit_array_length(_M0L3dstS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2430 = (struct _M0TUsiE*)_M0L3dstS55[_M0L6_2atmpS1225];
        if (_M0L6_2atmpS1226) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1226);
        }
        if (_M0L6_2aoldS2430) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2430);
        }
        _M0L3dstS55[_M0L6_2atmpS1225] = _M0L6_2atmpS1226;
        _M0L6_2atmpS1228 = _M0L1iS62 - 1;
        _M0L1iS62 = _M0L6_2atmpS1228;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS56);
        moonbit_decref_cycle_free(_M0L3dstS55);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t* _M0L4selfS15) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS15);
}

int32_t _M0MPB18UninitializedArray6lengthGfE(float* _M0L4selfS16) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS16);
}

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t* _M0L4selfS17) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS17);
}

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(
  struct _M0TUsiE** _M0L4selfS18
) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS18);
}

int32_t _M0IPB7FailurePB4Show6output(
  void* _M0L10_2ax__6387S11,
  struct _M0TPB6Logger _M0L10_2ax__6388S14
) {
  struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS12;
  moonbit_string_t _M0L15_2a_2aarg__6389S13;
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2aFailureS12
  = (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L10_2ax__6387S11;
  _M0L15_2a_2aarg__6389S13 = _M0L10_2aFailureS12->$0;
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S14.$0->$method_0(_M0L10_2ax__6388S14.$1, (moonbit_string_t)moonbit_string_literal_42.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S14, _M0L15_2a_2aarg__6389S13);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S14.$0->$method_0(_M0L10_2ax__6388S14.$1, (moonbit_string_t)moonbit_string_literal_43.data);
  return 0;
}

int32_t _M0MPB6Logger13write__objectGsE(
  struct _M0TPB6Logger _M0L4selfS10,
  moonbit_string_t _M0L3objS9
) {
  #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 180 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS9, _M0L4selfS10);
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

moonbit_string_t _M0FPC15abort5abortGsE(moonbit_string_t _M0L3msgS2) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS2);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t _M0L3msgS3) {
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

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(
  moonbit_string_t _M0L3msgS5
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS5);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

moonbit_string_t* _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(
  moonbit_string_t _M0L3msgS6
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS6);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

struct _M0TUsiE** _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(
  moonbit_string_t _M0L3msgS7
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS7);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

int32_t _M0FPC15abort5abortGiE(moonbit_string_t _M0L3msgS8) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS8);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1154) {
  switch (Moonbit_object_tag(_M0L4_2aeS1154)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_44.data;
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_45.data;
      break;
    }
    
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_46.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1154);
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_47.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1180,
  struct _M0TPB4Show _M0L8_2aparamS1179
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1178 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1180;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1178, _M0L8_2aparamS1179);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1177,
  struct _M0TPB4Show _M0L8_2aparamS1176
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1175 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1177;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1175, _M0L8_2aparamS1176);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1174,
  int32_t _M0L8_2aparamS1173
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1172 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1174;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1172, _M0L8_2aparamS1173);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1171,
  struct _M0TPC16string10StringView _M0L8_2aparamS1170
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1169 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1171;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1169, _M0L8_2aparamS1170);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1168,
  moonbit_string_t _M0L8_2aparamS1165,
  int32_t _M0L8_2aparamS1166,
  int32_t _M0L8_2aparamS1167
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1164 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1168;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1164, _M0L8_2aparamS1165, _M0L8_2aparamS1166, _M0L8_2aparamS1167);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1163,
  moonbit_string_t _M0L8_2aparamS1162
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1161 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1163;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1161, _M0L8_2aparamS1162);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1184;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1147;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1148;
  int32_t _M0L7_2abindS1149;
  struct _M0TUsiE** _M0L7_2abindS1150;
  int32_t _M0L6_2acntS2435;
  int32_t _M0L2__S1151;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1184
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1147
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1147)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 79, 0);
  _M0L12async__testsS1147->$0 = _M0L6_2atmpS1184;
  _M0L12async__testsS1147->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1148
  = _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1149 = _M0L7_2abindS1148->$1;
  _M0L7_2abindS1150 = _M0L7_2abindS1148->$0;
  _M0L6_2acntS2435
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1148));
  if (_M0L6_2acntS2435 > 1) {
    int32_t _M0L11_2anew__cntS2436 = _M0L6_2acntS2435 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1148), _M0L11_2anew__cntS2436);
    moonbit_incref_cycle_free(_M0L7_2abindS1150);
  } else if (_M0L6_2acntS2435 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1148);
  }
  _M0L2__S1151 = 0;
  while (1) {
    if (_M0L2__S1151 < _M0L7_2abindS1149) {
      struct _M0TUsiE* _M0L3argS1152 =
        (struct _M0TUsiE*)_M0L7_2abindS1150[_M0L2__S1151];
      moonbit_string_t _M0L6_2atmpS1181 = _M0L3argS1152->$0;
      int32_t _M0L6_2atmpS1182 = _M0L3argS1152->$1;
      int32_t _M0L6_2atmpS1183;
      moonbit_incref_cycle_free(_M0L6_2atmpS1181);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples26if__neuron__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1147, _M0L6_2atmpS1181, _M0L6_2atmpS1182);
      moonbit_decref_cycle_free(_M0L6_2atmpS1181);
      _M0L6_2atmpS1183 = _M0L2__S1151 + 1;
      _M0L2__S1151 = _M0L6_2atmpS1183;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1150);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_neuron\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples26if__neuron__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples26if__neuron__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1147);
  moonbit_decref_cycle_free(_M0L12async__testsS1147);
  moonbit_flush_cycles();
  return 0;
}