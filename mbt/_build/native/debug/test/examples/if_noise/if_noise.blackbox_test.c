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

struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1923;

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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25if__noise__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TP26RiantR8snn__mbt17BalancedParameter;

struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025;

struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus;

struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat;

struct _M0TUddE;

struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__;

struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TP26RiantR8snn__mbt13STDPVariables;

struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet;

struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric;

struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE;

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

struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1918;

struct _M0DTP26RiantR8snn__mbt6AnyPop12AdExSinExp__;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0DTP26RiantR8snn__mbt12STPEntryKind20MarkramSTPTimestep__;

struct _M0DTPC16option6OptionGfE4Some;

struct _M0TP26RiantR8snn__mbt20MorrisLecarParameter;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE;

struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25if__noise__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric;

struct _M0TWRPC15error5ErrorEu;

struct _M0TPB8MutLocalGiE;

struct _M0TP26RiantR8snn__mbt12STDPGerstner;

struct _M0TP26RiantR8snn__mbt11MorrisLecar;

struct _M0TP26RiantR8snn__mbt7Poisson;

struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

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

struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1923 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25if__noise__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1918 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25if__noise__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1930(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1923(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1918(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1895(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1888(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples25if__noise__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
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

struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0MP26RiantR8snn__mbt17CurrentStimulusIF11new_2einner(
  struct _M0TP26RiantR8snn__mbt2IF*,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*,
  float
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

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

int32_t _M0MPC15float5Float7to__int(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE*);

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE*);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*,
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

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

moonbit_string_t* _M0MPC15array5Array6bufferGsE(struct _M0TPB5ArrayGsE*);

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE*
);

struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*
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

int32_t _M0FPC15abort5abortGiE(moonbit_string_t);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[115]; 
} const moonbit_string_literal_37 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 114, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 105, 102, 95, 110, 111, 105, 115, 
    101, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 
    46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 
    105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 
    105, 112, 84, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 
    84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 
    114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 0
  };

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

struct { int32_t rc; uint32_t meta; uint16_t const data[113]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 112, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 105, 102, 95, 110, 111, 105, 115, 
    101, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 
    46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 
    105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 
    69, 114, 114, 111, 114, 46, 77, 111, 111, 110, 66, 105, 116, 84, 
    101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 
    110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 0
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

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1930$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1930
  };

uint32_t const moonbit_layout_table_data[99] =
  {
    sizeof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1918)
    / 4, 1,
    offsetof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1918, $1)
    / 4
    * 2,
    sizeof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1923)
    / 4, 1,
    offsetof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1923, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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
    sizeof(struct _M0TPB5ArrayGbE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGbE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF) / 4, 3,
    offsetof(struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF, $4) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGiE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGiE, $0) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS5455
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1951,
  moonbit_string_t _M0L8filenameS1920,
  int32_t _M0L5indexS1922
) {
  struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1918* _closure_5660;
  struct _M0TWEu* _M0L13handle__startS1918;
  struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1923* _closure_5661;
  struct _M0TWssbEu* _M0L14handle__resultS1923;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1930;
  void* _M0L11_2atry__errS1945;
  struct moonbit_result_0 _tmp_5663;
  int32_t _handle__error__result_5664;
  int32_t _M0L6_2atmpS5443;
  void* _M0L3errS1946;
  moonbit_string_t _M0L4nameS1948;
  struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1949;
  moonbit_string_t _M0L7_2anameS1950;
  int32_t _M0L6_2acntS5482;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1920);
  _closure_5660
  = (struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1918*)moonbit_malloc(sizeof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1918));
  Moonbit_object_header(_closure_5660)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_5660->code
  = &_M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1918;
  _closure_5660->$0 = _M0L5indexS1922;
  _closure_5660->$1 = _M0L8filenameS1920;
  _M0L13handle__startS1918 = (struct _M0TWEu*)_closure_5660;
  moonbit_incref_cycle_free(_M0L8filenameS1920);
  _closure_5661
  = (struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1923*)moonbit_malloc(sizeof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1923));
  Moonbit_object_header(_closure_5661)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_5661->code
  = &_M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1923;
  _closure_5661->$0 = _M0L5indexS1922;
  _closure_5661->$1 = _M0L8filenameS1920;
  _M0L14handle__resultS1923 = (struct _M0TWssbEu*)_closure_5661;
  _M0L17error__to__stringS1930
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1930$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _tmp_5663
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1951, _M0L8filenameS1920, _M0L5indexS1922, _M0L13handle__startS1918, _M0L14handle__resultS1923, _M0L17error__to__stringS1930);
  if (_tmp_5663.tag) {
    int32_t const _M0L5_2aokS5452 = _tmp_5663.data.ok;
    _handle__error__result_5664 = _M0L5_2aokS5452;
  } else {
    void* const _M0L6_2aerrS5453 = _tmp_5663.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1930);
    moonbit_decref_cycle_free(_M0L13handle__startS1918);
    _M0L11_2atry__errS1945 = _M0L6_2aerrS5453;
    goto join_1944;
  }
  if (_handle__error__result_5664) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1930);
    moonbit_decref_cycle_free(_M0L13handle__startS1918);
    _M0L6_2atmpS5443 = 1;
  } else {
    struct moonbit_result_0 _tmp_5665;
    int32_t _handle__error__result_5666;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
    _tmp_5665
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1951, _M0L8filenameS1920, _M0L5indexS1922, _M0L13handle__startS1918, _M0L14handle__resultS1923, _M0L17error__to__stringS1930);
    if (_tmp_5665.tag) {
      int32_t const _M0L5_2aokS5450 = _tmp_5665.data.ok;
      _handle__error__result_5666 = _M0L5_2aokS5450;
    } else {
      void* const _M0L6_2aerrS5451 = _tmp_5665.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1930);
      moonbit_decref_cycle_free(_M0L13handle__startS1918);
      _M0L11_2atry__errS1945 = _M0L6_2aerrS5451;
      goto join_1944;
    }
    if (_handle__error__result_5666) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1930);
      moonbit_decref_cycle_free(_M0L13handle__startS1918);
      _M0L6_2atmpS5443 = 1;
    } else {
      struct moonbit_result_0 _tmp_5667;
      int32_t _handle__error__result_5668;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
      _tmp_5667
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1951, _M0L8filenameS1920, _M0L5indexS1922, _M0L13handle__startS1918, _M0L14handle__resultS1923, _M0L17error__to__stringS1930);
      if (_tmp_5667.tag) {
        int32_t const _M0L5_2aokS5448 = _tmp_5667.data.ok;
        _handle__error__result_5668 = _M0L5_2aokS5448;
      } else {
        void* const _M0L6_2aerrS5449 = _tmp_5667.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1930);
        moonbit_decref_cycle_free(_M0L13handle__startS1918);
        _M0L11_2atry__errS1945 = _M0L6_2aerrS5449;
        goto join_1944;
      }
      if (_handle__error__result_5668) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1930);
        moonbit_decref_cycle_free(_M0L13handle__startS1918);
        _M0L6_2atmpS5443 = 1;
      } else {
        struct moonbit_result_0 _tmp_5669;
        int32_t _handle__error__result_5670;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
        _tmp_5669
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1951, _M0L8filenameS1920, _M0L5indexS1922, _M0L13handle__startS1918, _M0L14handle__resultS1923, _M0L17error__to__stringS1930);
        if (_tmp_5669.tag) {
          int32_t const _M0L5_2aokS5446 = _tmp_5669.data.ok;
          _handle__error__result_5670 = _M0L5_2aokS5446;
        } else {
          void* const _M0L6_2aerrS5447 = _tmp_5669.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1930);
          moonbit_decref_cycle_free(_M0L13handle__startS1918);
          _M0L11_2atry__errS1945 = _M0L6_2aerrS5447;
          goto join_1944;
        }
        if (_handle__error__result_5670) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1930);
          moonbit_decref_cycle_free(_M0L13handle__startS1918);
          _M0L6_2atmpS5443 = 1;
        } else {
          struct moonbit_result_0 _tmp_5671;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
          _tmp_5671
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1951, _M0L8filenameS1920, _M0L5indexS1922, _M0L13handle__startS1918, _M0L14handle__resultS1923, _M0L17error__to__stringS1930);
          moonbit_decref_cycle_free(_M0L13handle__startS1918);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1930);
          if (_tmp_5671.tag) {
            int32_t const _M0L5_2aokS5444 = _tmp_5671.data.ok;
            _M0L6_2atmpS5443 = _M0L5_2aokS5444;
          } else {
            void* const _M0L6_2aerrS5445 = _tmp_5671.data.err;
            _M0L11_2atry__errS1945 = _M0L6_2aerrS5445;
            goto join_1944;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS5443) {
    void* _M0L128RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5454 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L128RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5454)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L128RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5454)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1945
    = _M0L128RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5454;
    goto join_1944;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1923);
  }
  goto joinlet_5662;
  join_1944:;
  _M0L3errS1946 = _M0L11_2atry__errS1945;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1949
  = (struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1946;
  _M0L7_2anameS1950 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1949->$0;
  _M0L6_2acntS5482
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1949));
  if (_M0L6_2acntS5482 > 1) {
    int32_t _M0L11_2anew__cntS5483 = _M0L6_2acntS5482 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1949), _M0L11_2anew__cntS5483);
    moonbit_incref_cycle_free(_M0L7_2anameS1950);
  } else if (_M0L6_2acntS5482 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1949);
  }
  _M0L4nameS1948 = _M0L7_2anameS1950;
  goto join_1947;
  goto joinlet_5672;
  join_1947:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1923(_M0L14handle__resultS1923, _M0L4nameS1948, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1923);
  moonbit_decref_cycle_free(_M0L4nameS1948);
  joinlet_5672:;
  joinlet_5662:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1930(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS5442,
  void* _M0L3errS1931
) {
  void* _M0L1eS1933;
  moonbit_string_t _M0L1eS1935;
  moonbit_string_t _result_5675;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1931)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1936 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1931;
      moonbit_string_t _M0L4_2aeS1937 = _M0L10_2aFailureS1936->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1937);
      _M0L1eS1935 = _M0L4_2aeS1937;
      goto join_1934;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1938 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1931;
      moonbit_string_t _M0L4_2aeS1939 = _M0L15_2aInspectErrorS1938->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1939);
      _M0L1eS1935 = _M0L4_2aeS1939;
      goto join_1934;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1940 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1931;
      moonbit_string_t _M0L4_2aeS1941 = _M0L16_2aSnapshotErrorS1940->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1941);
      _M0L1eS1935 = _M0L4_2aeS1941;
      goto join_1934;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1942 =
        (struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1931;
      moonbit_string_t _M0L4_2aeS1943 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1942->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1943);
      _M0L1eS1935 = _M0L4_2aeS1943;
      goto join_1934;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1931);
      _M0L1eS1933 = _M0L3errS1931;
      goto join_1932;
      break;
    }
  }
  join_1934:;
  return _M0L1eS1935;
  join_1932:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _result_5675 = _M0FP15Error10to__string(_M0L1eS1933);
  moonbit_decref_cycle_free(_M0L1eS1933);
  return _result_5675;
}

int32_t _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1923(
  struct _M0TWssbEu* _M0L6_2aenvS5439,
  moonbit_string_t _M0L10__testnameS1924,
  moonbit_string_t _M0L7messageS1925,
  int32_t _M0L7skippedS1926
) {
  struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1923* _M0L14_2acasted__envS5440;
  moonbit_string_t _M0L8filenameS1920;
  int32_t _M0L5indexS1922;
  moonbit_string_t _M0L10file__nameS1927;
  moonbit_string_t _M0L7messageS1928;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1929;
  moonbit_string_t _M0L6_2atmpS5441;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS5440
  = (struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1923*)_M0L6_2aenvS5439;
  _M0L8filenameS1920 = _M0L14_2acasted__envS5440->$1;
  _M0L5indexS1922 = _M0L14_2acasted__envS5440->$0;
  if (!_M0L7skippedS1926 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1927
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1920, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1928
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1925, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1929
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1929, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1929, _M0L10file__nameS1927);
  moonbit_decref_cycle_free(_M0L10file__nameS1927);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1929, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1929, _M0L5indexS1922);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1929, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1929, _M0L7messageS1928);
  moonbit_decref_cycle_free(_M0L7messageS1928);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1929, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5441
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1929);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1929);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS5441);
  moonbit_decref_cycle_free(_M0L6_2atmpS5441);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1918(
  struct _M0TWEu* _M0L6_2aenvS5436
) {
  struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1918* _M0L14_2acasted__envS5437;
  moonbit_string_t _M0L8filenameS1920;
  int32_t _M0L5indexS1922;
  moonbit_string_t _M0L10file__nameS1919;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1921;
  moonbit_string_t _M0L6_2atmpS5438;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS5437
  = (struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2fif__noise__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1918*)_M0L6_2aenvS5436;
  _M0L8filenameS1920 = _M0L14_2acasted__envS5437->$1;
  _M0L5indexS1922 = _M0L14_2acasted__envS5437->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1919
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1920, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1921
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1921, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1921, _M0L10file__nameS1919);
  moonbit_decref_cycle_free(_M0L10file__nameS1919);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1921, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1921, _M0L5indexS1922);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1921, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5438
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1921);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1921);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS5438);
  moonbit_decref_cycle_free(_M0L6_2atmpS5438);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1888;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1895;
  struct _M0TUsiE** _M0L6_2atmpS5435;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1902;
  moonbit_string_t* _M0L9cli__argsS1903;
  moonbit_string_t _M0L6_2atmpS5434;
  moonbit_string_t _M0L6_2atmpS5433;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1904;
  int32_t _M0L7_2abindS1905;
  moonbit_string_t* _M0L7_2abindS1906;
  int32_t _M0L6_2acntS5484;
  int32_t _M0L2__S1907;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1888 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1895 = 0;
  _M0L6_2atmpS5435 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1902
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1902)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1902->$0 = _M0L6_2atmpS5435;
  _M0L16file__and__indexS1902->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1903
  = _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1903)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS5434 = (moonbit_string_t)_M0L9cli__argsS1903[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS5434);
  moonbit_decref_cycle_free(_M0L9cli__argsS1903);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5433
  = _M0MP46RiantR8snn__mbt8examples25if__noise__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS5434);
  moonbit_decref_cycle_free(_M0L6_2atmpS5434);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1904
  = _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1895(_M0L51moonbit__test__driver__internal__split__mbt__stringS1895, _M0L6_2atmpS5433, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS5433);
  _M0L7_2abindS1905 = _M0L10test__argsS1904->$1;
  _M0L7_2abindS1906 = _M0L10test__argsS1904->$0;
  _M0L6_2acntS5484
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1904));
  if (_M0L6_2acntS5484 > 1) {
    int32_t _M0L11_2anew__cntS5485 = _M0L6_2acntS5484 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1904), _M0L11_2anew__cntS5485);
    moonbit_incref_cycle_free(_M0L7_2abindS1906);
  } else if (_M0L6_2acntS5484 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1904);
  }
  _M0L2__S1907 = 0;
  while (1) {
    if (_M0L2__S1907 < _M0L7_2abindS1905) {
      moonbit_string_t _M0L3argS1908 =
        (moonbit_string_t)_M0L7_2abindS1906[_M0L2__S1907];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1909;
      moonbit_string_t _M0L4fileS1910;
      moonbit_string_t _M0L5rangeS1911;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1912;
      moonbit_string_t _M0L6_2atmpS5431;
      int32_t _M0L5startS1913;
      moonbit_string_t _M0L6_2atmpS5430;
      int32_t _M0L3endS1914;
      int32_t _M0L1iS1915;
      int32_t _M0L6_2atmpS5432;
      moonbit_incref_cycle_free(_M0L3argS1908);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1909
      = _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1895(_M0L51moonbit__test__driver__internal__split__mbt__stringS1895, _M0L3argS1908, 58);
      moonbit_decref_cycle_free(_M0L3argS1908);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1910
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1909, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1911
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1909, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1909);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1912
      = _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1895(_M0L51moonbit__test__driver__internal__split__mbt__stringS1895, _M0L5rangeS1911, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1911);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS5431
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1912, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1913
      = _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1888(_M0L45moonbit__test__driver__internal__parse__int__S1888, _M0L6_2atmpS5431);
      moonbit_decref_cycle_free(_M0L6_2atmpS5431);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS5430
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1912, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1912);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1914
      = _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1888(_M0L45moonbit__test__driver__internal__parse__int__S1888, _M0L6_2atmpS5430);
      moonbit_decref_cycle_free(_M0L6_2atmpS5430);
      _M0L1iS1915 = _M0L5startS1913;
      while (1) {
        if (_M0L1iS1915 < _M0L3endS1914) {
          struct _M0TUsiE* _M0L8_2atupleS5428;
          int32_t _M0L6_2atmpS5429;
          moonbit_incref_cycle_free(_M0L4fileS1910);
          _M0L8_2atupleS5428
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS5428)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS5428->$0 = _M0L4fileS1910;
          _M0L8_2atupleS5428->$1 = _M0L1iS1915;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1902, _M0L8_2atupleS5428);
          _M0L6_2atmpS5429 = _M0L1iS1915 + 1;
          _M0L1iS1915 = _M0L6_2atmpS5429;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1910);
        }
        break;
      }
      _M0L6_2atmpS5432 = _M0L2__S1907 + 1;
      _M0L2__S1907 = _M0L6_2atmpS5432;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1906);
    }
    break;
  }
  return _M0L16file__and__indexS1902;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1895(
  int32_t _M0L6_2aenvS5409,
  moonbit_string_t _M0L1sS1896,
  int32_t _M0L3sepS1897
) {
  moonbit_string_t* _M0L6_2atmpS5427;
  struct _M0TPB5ArrayGsE* _M0L3resS1898;
  struct _M0TPB8MutLocalGiE* _M0L1iS1899;
  struct _M0TPB8MutLocalGiE* _M0L5startS1900;
  int32_t _M0L3valS5422;
  int32_t _M0L6_2atmpS5423;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5427 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1898
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1898)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1898->$0 = _M0L6_2atmpS5427;
  _M0L3resS1898->$1 = 0;
  _M0L1iS1899
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1899)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1899->$0 = 0;
  _M0L5startS1900
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1900)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1900->$0 = 0;
  while (1) {
    int32_t _M0L3valS5410 = _M0L1iS1899->$0;
    int32_t _M0L6_2atmpS5411 = Moonbit_array_length(_M0L1sS1896);
    if (_M0L3valS5410 < _M0L6_2atmpS5411) {
      int32_t _M0L3valS5414 = _M0L1iS1899->$0;
      int32_t _M0L6_2atmpS5413;
      int32_t _M0L6_2atmpS5412;
      int32_t _M0L3valS5421;
      int32_t _M0L6_2atmpS5420;
      if (
        _M0L3valS5414 < 0
        || _M0L3valS5414 >= Moonbit_array_length(_M0L1sS1896)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS5413 = _M0L1sS1896[_M0L3valS5414];
      _M0L6_2atmpS5412 = _M0L6_2atmpS5413;
      if (_M0L6_2atmpS5412 == _M0L3sepS1897) {
        int32_t _M0L3valS5416 = _M0L5startS1900->$0;
        int32_t _M0L3valS5417 = _M0L1iS1899->$0;
        moonbit_string_t _M0L6_2atmpS5415;
        int32_t _M0L3valS5419;
        int32_t _M0L6_2atmpS5418;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS5415
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1896, _M0L3valS5416, _M0L3valS5417);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1898, _M0L6_2atmpS5415);
        _M0L3valS5419 = _M0L1iS1899->$0;
        _M0L6_2atmpS5418 = _M0L3valS5419 + 1;
        _M0L5startS1900->$0 = _M0L6_2atmpS5418;
      }
      _M0L3valS5421 = _M0L1iS1899->$0;
      _M0L6_2atmpS5420 = _M0L3valS5421 + 1;
      _M0L1iS1899->$0 = _M0L6_2atmpS5420;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1899);
    }
    break;
  }
  _M0L3valS5422 = _M0L5startS1900->$0;
  _M0L6_2atmpS5423 = Moonbit_array_length(_M0L1sS1896);
  if (_M0L3valS5422 < _M0L6_2atmpS5423) {
    int32_t _M0L3valS5425 = _M0L5startS1900->$0;
    int32_t _M0L6_2atmpS5426;
    moonbit_string_t _M0L6_2atmpS5424;
    moonbit_decref_cycle_free(_M0L5startS1900);
    _M0L6_2atmpS5426 = Moonbit_array_length(_M0L1sS1896);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS5424
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1896, _M0L3valS5425, _M0L6_2atmpS5426);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1898, _M0L6_2atmpS5424);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1900);
  }
  return _M0L3resS1898;
}

int32_t _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1888(
  int32_t _M0L6_2aenvS5402,
  moonbit_string_t _M0L1sS1889
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1890;
  int32_t _M0L3lenS1891;
  int32_t _M0L7_2abindS1892;
  int32_t _M0L1iS1893;
  int32_t _result_5680;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1890
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1890)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1890->$0 = 0;
  _M0L3lenS1891 = Moonbit_array_length(_M0L1sS1889);
  _M0L7_2abindS1892 = 0;
  _M0L1iS1893 = _M0L7_2abindS1892;
  while (1) {
    if (_M0L1iS1893 < _M0L3lenS1891) {
      int32_t _M0L3valS5407 = _M0L3resS1890->$0;
      int32_t _M0L6_2atmpS5404 = _M0L3valS5407 * 10;
      int32_t _M0L6_2atmpS5406;
      int32_t _M0L6_2atmpS5405;
      int32_t _M0L6_2atmpS5403;
      int32_t _M0L6_2atmpS5408;
      if (
        _M0L1iS1893 < 0 || _M0L1iS1893 >= Moonbit_array_length(_M0L1sS1889)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS5406 = _M0L1sS1889[_M0L1iS1893];
      _M0L6_2atmpS5405 = _M0L6_2atmpS5406 - 48;
      _M0L6_2atmpS5403 = _M0L6_2atmpS5404 + _M0L6_2atmpS5405;
      _M0L3resS1890->$0 = _M0L6_2atmpS5403;
      _M0L6_2atmpS5408 = _M0L1iS1893 + 1;
      _M0L1iS1893 = _M0L6_2atmpS5408;
      continue;
    }
    break;
  }
  _result_5680 = _M0L3resS1890->$0;
  moonbit_decref_cycle_free(_M0L3resS1890);
  return _result_5680;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples25if__noise__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1887
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1887);
  return _M0L4selfS1887;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1857,
  moonbit_string_t _M0L12_2adiscard__S1858,
  int32_t _M0L12_2adiscard__S1859,
  struct _M0TWEu* _M0L12_2adiscard__S1860,
  struct _M0TWssbEu* _M0L12_2adiscard__S1861,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1862
) {
  struct moonbit_result_0 _result_5681;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _result_5681.tag = 1;
  _result_5681.data.ok = 0;
  return _result_5681;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1863,
  moonbit_string_t _M0L12_2adiscard__S1864,
  int32_t _M0L12_2adiscard__S1865,
  struct _M0TWEu* _M0L12_2adiscard__S1866,
  struct _M0TWssbEu* _M0L12_2adiscard__S1867,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1868
) {
  struct moonbit_result_0 _result_5682;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _result_5682.tag = 1;
  _result_5682.data.ok = 0;
  return _result_5682;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1869,
  moonbit_string_t _M0L12_2adiscard__S1870,
  int32_t _M0L12_2adiscard__S1871,
  struct _M0TWEu* _M0L12_2adiscard__S1872,
  struct _M0TWssbEu* _M0L12_2adiscard__S1873,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1874
) {
  struct moonbit_result_0 _result_5683;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _result_5683.tag = 1;
  _result_5683.data.ok = 0;
  return _result_5683;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1875,
  moonbit_string_t _M0L12_2adiscard__S1876,
  int32_t _M0L12_2adiscard__S1877,
  struct _M0TWEu* _M0L12_2adiscard__S1878,
  struct _M0TWssbEu* _M0L12_2adiscard__S1879,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1880
) {
  struct moonbit_result_0 _result_5684;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _result_5684.tag = 1;
  _result_5684.data.ok = 0;
  return _result_5684;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1881,
  moonbit_string_t _M0L12_2adiscard__S1882,
  int32_t _M0L12_2adiscard__S1883,
  struct _M0TWEu* _M0L12_2adiscard__S1884,
  struct _M0TWssbEu* _M0L12_2adiscard__S1885,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1886
) {
  struct moonbit_result_0 _result_5685;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _result_5685.tag = 1;
  _result_5685.data.ok = 0;
  return _result_5685;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1856
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23heterogeneous__sim__for(
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0L1mS1846,
  float _M0L8durationS1843
) {
  float _M0L2dtS1841;
  float _M0L6_2atmpS5401;
  int32_t _M0L5stepsS1842;
  int32_t _M0L7_2abindS1844;
  int32_t _M0L2__S1845;
  #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L2dtS1841 = 0x1p-3f;
  _M0L6_2atmpS5401 = _M0L8durationS1843 / _M0L2dtS1841;
  #line 273 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L5stepsS1842 = _M0MPC15float5Float7to__int(_M0L6_2atmpS5401);
  _M0L7_2abindS1844 = 0;
  _M0L2__S1845 = _M0L7_2abindS1844;
  while (1) {
    if (_M0L2__S1845 < _M0L5stepsS1842) {
      int32_t _M0L6_2atmpS5400;
      #line 275 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt19step__heterogeneous(_M0L1mS1846, _M0L2dtS1841);
      _M0L6_2atmpS5400 = _M0L2__S1845 + 1;
      _M0L2__S1845 = _M0L6_2atmpS5400;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt19step__heterogeneous(
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0L1mS1743,
  float _M0L2dtS1748
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L7_2abindS1742;
  int32_t _M0L7_2abindS1744;
  void** _M0L7_2abindS1745;
  int32_t _M0L2__S1746;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS1750;
  int32_t _M0L7_2abindS1751;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS1752;
  int32_t _M0L2__S1753;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L7_2abindS1756;
  int32_t _M0L7_2abindS1757;
  void** _M0L7_2abindS1758;
  int32_t _M0L2__S1759;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS1777;
  int32_t _M0L7_2abindS1778;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS1779;
  int32_t _M0L2__S1780;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L7_2abindS1783;
  int32_t _M0L7_2abindS1784;
  void** _M0L7_2abindS1785;
  int32_t _M0L2__S1786;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L7_2abindS1829;
  int32_t _M0L7_2abindS1830;
  void** _M0L7_2abindS1831;
  int32_t _M0L2__S1832;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS1835;
  int32_t _M0L7_2abindS1836;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS1837;
  int32_t _M0L2__S1838;
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5399;
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L7_2abindS1742 = _M0L1mS1743->$2;
  _M0L7_2abindS1744 = _M0L7_2abindS1742->$1;
  _M0L7_2abindS1745 = _M0L7_2abindS1742->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1745);
  _M0L2__S1746 = 0;
  while (1) {
    if (_M0L2__S1746 < _M0L7_2abindS1744) {
      void* _M0L1sS1747 = (void*)_M0L7_2abindS1745[_M0L2__S1746];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5208 = _M0L1mS1743->$3;
      int32_t _M0L6_2atmpS5209;
      moonbit_incref_cycle_free(_M0L1sS1747);
      #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt14stimulate__any(_M0L1sS1747, _M0L4timeS5208, _M0L2dtS1748);
      moonbit_decref_cycle_free(_M0L1sS1747);
      _M0L6_2atmpS5209 = _M0L2__S1746 + 1;
      _M0L2__S1746 = _M0L6_2atmpS5209;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1745);
    }
    break;
  }
  _M0L7_2abindS1750 = _M0L1mS1743->$1;
  _M0L7_2abindS1751 = _M0L7_2abindS1750->$1;
  _M0L7_2abindS1752 = _M0L7_2abindS1750->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1752);
  _M0L2__S1753 = 0;
  while (1) {
    if (_M0L2__S1753 < _M0L7_2abindS1751) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1754 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS1752[
          _M0L2__S1753
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5211 = _M0L1mS1743->$3;
      float _M0L6_2atmpS5210;
      int32_t _M0L6_2atmpS5212;
      moonbit_incref_cycle_free(_M0L1cS1754);
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5210 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5211);
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt25deliver__pending__synapse(_M0L1cS1754, _M0L6_2atmpS5210);
      moonbit_decref_cycle_free(_M0L1cS1754);
      _M0L6_2atmpS5212 = _M0L2__S1753 + 1;
      _M0L2__S1753 = _M0L6_2atmpS5212;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1752);
    }
    break;
  }
  _M0L7_2abindS1756 = _M0L1mS1743->$6;
  _M0L7_2abindS1757 = _M0L7_2abindS1756->$1;
  _M0L7_2abindS1758 = _M0L7_2abindS1756->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1758);
  _M0L2__S1759 = 0;
  while (1) {
    if (_M0L2__S1759 < _M0L7_2abindS1757) {
      void* _M0L5entryS1760 = (void*)_M0L7_2abindS1758[_M0L2__S1759];
      struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _M0L1eS1762;
      struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet* _M0L1eS1765;
      struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry* _M0L1eS1768;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5229;
      int32_t _M0L11conn__indexS5230;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1769;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS5225;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter* _M0L5paramS5226;
      int32_t _M0L6_2acntS5490;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5228;
      float _M0L6_2atmpS5227;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5223;
      int32_t _M0L11conn__indexS5224;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1766;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS5219;
      struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet* _M0L5paramS5220;
      int32_t _M0L6_2acntS5488;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5222;
      float _M0L6_2atmpS5221;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5217;
      int32_t _M0L11conn__indexS5218;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1763;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS5213;
      struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0L5paramS5214;
      int32_t _M0L6_2acntS5486;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5216;
      float _M0L6_2atmpS5215;
      int32_t _M0L6_2atmpS5231;
      switch (Moonbit_object_tag(_M0L5entryS1760)) {
        case 0: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__* _M0L15_2aMarkramSTP__S1770 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__*)_M0L5entryS1760;
          struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry* _M0L4_2aeS1771 =
            _M0L15_2aMarkramSTP__S1770->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1771);
          _M0L1eS1768 = _M0L4_2aeS1771;
          goto join_1767;
          break;
        }
        
        case 1: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__* _M0L18_2aMarkramSTPHet__S1772 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__*)_M0L5entryS1760;
          struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet* _M0L4_2aeS1773 =
            _M0L18_2aMarkramSTPHet__S1772->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1773);
          _M0L1eS1765 = _M0L4_2aeS1773;
          goto join_1764;
          break;
        }
        default: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind20MarkramSTPTimestep__* _M0L23_2aMarkramSTPTimestep__S1774 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind20MarkramSTPTimestep__*)_M0L5entryS1760;
          struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _M0L4_2aeS1775 =
            _M0L23_2aMarkramSTPTimestep__S1774->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1775);
          _M0L1eS1762 = _M0L4_2aeS1775;
          goto join_1761;
          break;
        }
      }
      goto joinlet_5692;
      join_1767:;
      _M0L5connsS5229 = _M0L1mS1743->$1;
      _M0L11conn__indexS5230 = _M0L1eS1768->$0;
      #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1769
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5229, _M0L11conn__indexS5230);
      _M0L4varsS5225 = _M0L1eS1768->$1;
      _M0L5paramS5226 = _M0L1eS1768->$2;
      _M0L6_2acntS5490 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1768));
      if (_M0L6_2acntS5490 > 1) {
        int32_t _M0L11_2anew__cntS5491 = _M0L6_2acntS5490 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1768), _M0L11_2anew__cntS5491);
        moonbit_incref_cycle_free(_M0L5paramS5226);
        moonbit_incref_cycle_free(_M0L4varsS5225);
      } else if (_M0L6_2acntS5490 == 1) {
        #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1768);
      }
      _M0L4timeS5228 = _M0L1mS1743->$3;
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5227 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5228);
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt18markram__stp__step(_M0L3synS1769, _M0L4varsS5225, _M0L5paramS5226, _M0L6_2atmpS5227);
      moonbit_decref_cycle_free(_M0L3synS1769);
      moonbit_decref_cycle_free(_M0L4varsS5225);
      moonbit_decref_cycle_free(_M0L5paramS5226);
      joinlet_5692:;
      goto joinlet_5691;
      join_1764:;
      _M0L5connsS5223 = _M0L1mS1743->$1;
      _M0L11conn__indexS5224 = _M0L1eS1765->$0;
      #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1766
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5223, _M0L11conn__indexS5224);
      _M0L4varsS5219 = _M0L1eS1765->$1;
      _M0L5paramS5220 = _M0L1eS1765->$2;
      _M0L6_2acntS5488 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1765));
      if (_M0L6_2acntS5488 > 1) {
        int32_t _M0L11_2anew__cntS5489 = _M0L6_2acntS5488 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1765), _M0L11_2anew__cntS5489);
        moonbit_incref_cycle_free(_M0L5paramS5220);
        moonbit_incref_cycle_free(_M0L4varsS5219);
      } else if (_M0L6_2acntS5488 == 1) {
        #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1765);
      }
      _M0L4timeS5222 = _M0L1mS1743->$3;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5221 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5222);
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt23markram__stp__step__het(_M0L3synS1766, _M0L4varsS5219, _M0L5paramS5220, _M0L6_2atmpS5221);
      moonbit_decref_cycle_free(_M0L3synS1766);
      moonbit_decref_cycle_free(_M0L4varsS5219);
      moonbit_decref_cycle_free(_M0L5paramS5220);
      joinlet_5691:;
      goto joinlet_5690;
      join_1761:;
      _M0L5connsS5217 = _M0L1mS1743->$1;
      _M0L11conn__indexS5218 = _M0L1eS1762->$0;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1763
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5217, _M0L11conn__indexS5218);
      _M0L4varsS5213 = _M0L1eS1762->$1;
      _M0L5paramS5214 = _M0L1eS1762->$2;
      _M0L6_2acntS5486 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1762));
      if (_M0L6_2acntS5486 > 1) {
        int32_t _M0L11_2anew__cntS5487 = _M0L6_2acntS5486 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1762), _M0L11_2anew__cntS5487);
        moonbit_incref_cycle_free(_M0L5paramS5214);
        moonbit_incref_cycle_free(_M0L4varsS5213);
      } else if (_M0L6_2acntS5486 == 1) {
        #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1762);
      }
      _M0L4timeS5216 = _M0L1mS1743->$3;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5215 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5216);
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt28markram__stp__step__timestep(_M0L3synS1763, _M0L4varsS5213, _M0L5paramS5214, _M0L6_2atmpS5215, _M0L2dtS1748);
      moonbit_decref_cycle_free(_M0L3synS1763);
      moonbit_decref_cycle_free(_M0L4varsS5213);
      moonbit_decref_cycle_free(_M0L5paramS5214);
      joinlet_5690:;
      _M0L6_2atmpS5231 = _M0L2__S1759 + 1;
      _M0L2__S1759 = _M0L6_2atmpS5231;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1758);
    }
    break;
  }
  _M0L7_2abindS1777 = _M0L1mS1743->$1;
  _M0L7_2abindS1778 = _M0L7_2abindS1777->$1;
  _M0L7_2abindS1779 = _M0L7_2abindS1777->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1779);
  _M0L2__S1780 = 0;
  while (1) {
    if (_M0L2__S1780 < _M0L7_2abindS1778) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1781 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS1779[
          _M0L2__S1780
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5233 = _M0L1mS1743->$3;
      float _M0L6_2atmpS5232;
      int32_t _M0L6_2atmpS5234;
      moonbit_incref_cycle_free(_M0L1cS1781);
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5232 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5233);
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt16forward__synapse(_M0L1cS1781, _M0L6_2atmpS5232);
      moonbit_decref_cycle_free(_M0L1cS1781);
      _M0L6_2atmpS5234 = _M0L2__S1780 + 1;
      _M0L2__S1780 = _M0L6_2atmpS5234;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1779);
    }
    break;
  }
  _M0L7_2abindS1783 = _M0L1mS1743->$5;
  _M0L7_2abindS1784 = _M0L7_2abindS1783->$1;
  _M0L7_2abindS1785 = _M0L7_2abindS1783->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1785);
  _M0L2__S1786 = 0;
  while (1) {
    if (_M0L2__S1786 < _M0L7_2abindS1784) {
      void* _M0L5entryS1787 = (void*)_M0L7_2abindS1785[_M0L2__S1786];
      struct _M0TP26RiantR8snn__mbt17CaPlasticityEntry* _M0L1eS1789;
      struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric* _M0L1eS1792;
      struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0L1eS1795;
      struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0L1eS1798;
      struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025* _M0L1eS1801;
      struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric* _M0L1eS1804;
      struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat* _M0L1eS1807;
      struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0L1eS1810;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5392;
      int32_t _M0L11conn__indexS5393;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1811;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5387;
      struct _M0TPB5ArrayGfE* _M0L4valsS5374;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5386;
      struct _M0TPB5ArrayGbE* _M0L4fireS5375;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5385;
      struct _M0TPB5ArrayGbE* _M0L4fireS5376;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5384;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5377;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5383;
      int32_t _M0L6_2acntS5639;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5378;
      int32_t _M0L6_2acntS5650;
      struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS5379;
      struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS5380;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5382;
      float _M0L6_2atmpS5381;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5388;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5391;
      int32_t _M0L6_2acntS5654;
      float _M0L6_2atmpS5390;
      float _M0L6_2atmpS5389;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5372;
      int32_t _M0L11conn__indexS5373;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1808;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5367;
      struct _M0TPB5ArrayGfE* _M0L4valsS5355;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5366;
      struct _M0TPB5ArrayGbE* _M0L4fireS5356;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5365;
      struct _M0TPB5ArrayGbE* _M0L4fireS5357;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5364;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5358;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5363;
      int32_t _M0L6_2acntS5619;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5359;
      int32_t _M0L6_2acntS5630;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5360;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5361;
      struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L5paramS5362;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5368;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5371;
      int32_t _M0L6_2acntS5634;
      float _M0L6_2atmpS5370;
      float _M0L6_2atmpS5369;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5353;
      int32_t _M0L11conn__indexS5354;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1805;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5348;
      struct _M0TPB5ArrayGfE* _M0L4valsS5337;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5347;
      struct _M0TPB5ArrayGbE* _M0L4fireS5338;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5346;
      struct _M0TPB5ArrayGbE* _M0L4fireS5339;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5345;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5340;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5344;
      int32_t _M0L6_2acntS5600;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5341;
      int32_t _M0L6_2acntS5611;
      struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L4varsS5342;
      struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L5paramS5343;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5349;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5352;
      int32_t _M0L6_2acntS5615;
      float _M0L6_2atmpS5351;
      float _M0L6_2atmpS5350;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5335;
      int32_t _M0L11conn__indexS5336;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1802;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5330;
      struct _M0TPB5ArrayGfE* _M0L4valsS5317;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5329;
      struct _M0TPB5ArrayGbE* _M0L4fireS5318;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5328;
      struct _M0TPB5ArrayGbE* _M0L4fireS5319;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5327;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5320;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5326;
      int32_t _M0L6_2acntS5581;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5321;
      int32_t _M0L6_2acntS5592;
      struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS5322;
      struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L5paramS5323;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5325;
      float _M0L6_2atmpS5324;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5331;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5334;
      int32_t _M0L6_2acntS5596;
      float _M0L6_2atmpS5333;
      float _M0L6_2atmpS5332;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5315;
      int32_t _M0L11conn__indexS5316;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1799;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5310;
      struct _M0TPB5ArrayGfE* _M0L4valsS5297;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5309;
      struct _M0TPB5ArrayGbE* _M0L4fireS5298;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5308;
      struct _M0TPB5ArrayGbE* _M0L4fireS5299;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5307;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5300;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5306;
      int32_t _M0L6_2acntS5562;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5301;
      int32_t _M0L6_2acntS5573;
      struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L4varsS5302;
      struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS5303;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5305;
      float _M0L6_2atmpS5304;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5311;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5314;
      int32_t _M0L6_2acntS5577;
      float _M0L6_2atmpS5313;
      float _M0L6_2atmpS5312;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5295;
      int32_t _M0L11conn__indexS5296;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1796;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5290;
      struct _M0TPB5ArrayGfE* _M0L4valsS5275;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5289;
      struct _M0TPB5ArrayGbE* _M0L4fireS5276;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5288;
      struct _M0TPB5ArrayGbE* _M0L4fireS5277;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5287;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5278;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5286;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5279;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5285;
      int32_t _M0L6_2acntS5530;
      struct _M0TPB5ArrayGfE* _M0L1vS5280;
      int32_t _M0L6_2acntS5541;
      struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L4varsS5281;
      struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS5282;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5284;
      float _M0L6_2atmpS5283;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5291;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5294;
      int32_t _M0L6_2acntS5558;
      float _M0L6_2atmpS5293;
      float _M0L6_2atmpS5292;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5273;
      int32_t _M0L11conn__indexS5274;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1793;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5268;
      struct _M0TPB5ArrayGfE* _M0L4valsS5255;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5267;
      struct _M0TPB5ArrayGbE* _M0L4fireS5256;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5266;
      struct _M0TPB5ArrayGbE* _M0L4fireS5257;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5265;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5258;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5264;
      int32_t _M0L6_2acntS5511;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5259;
      int32_t _M0L6_2acntS5522;
      struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L4varsS5260;
      struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L5paramS5261;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5263;
      float _M0L6_2atmpS5262;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5269;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5272;
      int32_t _M0L6_2acntS5526;
      float _M0L6_2atmpS5271;
      float _M0L6_2atmpS5270;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5253;
      int32_t _M0L11conn__indexS5254;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1790;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5248;
      struct _M0TPB5ArrayGfE* _M0L4valsS5235;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5247;
      struct _M0TPB5ArrayGbE* _M0L4fireS5236;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5246;
      struct _M0TPB5ArrayGbE* _M0L4fireS5237;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5245;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5238;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5244;
      int32_t _M0L6_2acntS5492;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5239;
      int32_t _M0L6_2acntS5503;
      struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L4varsS5240;
      struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L5paramS5241;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5243;
      float _M0L6_2atmpS5242;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5249;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5252;
      int32_t _M0L6_2acntS5507;
      float _M0L6_2atmpS5251;
      float _M0L6_2atmpS5250;
      int32_t _M0L6_2atmpS5394;
      switch (Moonbit_object_tag(_M0L5entryS1787)) {
        case 0: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__* _M0L13_2aGerstner__S1812 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__*)_M0L5entryS1787;
          struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0L4_2aeS1813 =
            _M0L13_2aGerstner__S1812->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1813);
          _M0L1eS1810 = _M0L4_2aeS1813;
          goto join_1809;
          break;
        }
        
        case 1: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind12MexicanHat__* _M0L15_2aMexicanHat__S1814 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind12MexicanHat__*)_M0L5entryS1787;
          struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat* _M0L4_2aeS1815 =
            _M0L15_2aMexicanHat__S1814->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1815);
          _M0L1eS1807 = _M0L4_2aeS1815;
          goto join_1806;
          break;
        }
        
        case 2: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind15AntiSymmetric__* _M0L18_2aAntiSymmetric__S1816 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind15AntiSymmetric__*)_M0L5entryS1787;
          struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric* _M0L4_2aeS1817 =
            _M0L18_2aAntiSymmetric__S1816->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1817);
          _M0L1eS1804 = _M0L4_2aeS1817;
          goto join_1803;
          break;
        }
        
        case 3: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16Confavreux2025__* _M0L19_2aConfavreux2025__S1818 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16Confavreux2025__*)_M0L5entryS1787;
          struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025* _M0L4_2aeS1819 =
            _M0L19_2aConfavreux2025__S1818->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1819);
          _M0L1eS1801 = _M0L4_2aeS1819;
          goto join_1800;
          break;
        }
        
        case 4: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__* _M0L14_2aIstdpRate__S1820 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__*)_M0L5entryS1787;
          struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0L4_2aeS1821 =
            _M0L14_2aIstdpRate__S1820->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1821);
          _M0L1eS1798 = _M0L4_2aeS1821;
          goto join_1797;
          break;
        }
        
        case 5: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16IstdpPotential__* _M0L19_2aIstdpPotential__S1822 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16IstdpPotential__*)_M0L5entryS1787;
          struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0L4_2aeS1823 =
            _M0L19_2aIstdpPotential__S1822->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1823);
          _M0L1eS1795 = _M0L4_2aeS1823;
          goto join_1794;
          break;
        }
        
        case 6: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11Symmetric__* _M0L14_2aSymmetric__S1824 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11Symmetric__*)_M0L5entryS1787;
          struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric* _M0L4_2aeS1825 =
            _M0L14_2aSymmetric__S1824->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1825);
          _M0L1eS1792 = _M0L4_2aeS1825;
          goto join_1791;
          break;
        }
        default: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__* _M0L17_2aCaPlasticity__S1826 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__*)_M0L5entryS1787;
          struct _M0TP26RiantR8snn__mbt17CaPlasticityEntry* _M0L4_2aeS1827 =
            _M0L17_2aCaPlasticity__S1826->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1827);
          _M0L1eS1789 = _M0L4_2aeS1827;
          goto join_1788;
          break;
        }
      }
      goto joinlet_5702;
      join_1809:;
      _M0L5connsS5392 = _M0L1mS1743->$1;
      _M0L11conn__indexS5393 = _M0L1eS1810->$0;
      #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1811
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5392, _M0L11conn__indexS5393);
      _M0L6matrixS5387 = _M0L3synS1811->$4;
      _M0L4valsS5374 = _M0L6matrixS5387->$4;
      _M0L3preS5386 = _M0L3synS1811->$0;
      _M0L4fireS5375 = _M0L3preS5386->$5;
      _M0L4postS5385 = _M0L3synS1811->$1;
      _M0L4fireS5376 = _M0L4postS5385->$5;
      _M0L6matrixS5384 = _M0L3synS1811->$4;
      _M0L6colptrS5377 = _M0L6matrixS5384->$3;
      _M0L6matrixS5383 = _M0L3synS1811->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5377);
      moonbit_incref_cycle_free(_M0L4fireS5376);
      moonbit_incref_cycle_free(_M0L4fireS5375);
      moonbit_incref_cycle_free(_M0L4valsS5374);
      _M0L6_2acntS5639
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1811));
      if (_M0L6_2acntS5639 > 1) {
        int32_t _M0L11_2anew__cntS5649 = _M0L6_2acntS5639 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1811), _M0L11_2anew__cntS5649);
        moonbit_incref_cycle_free(_M0L6matrixS5383);
      } else if (_M0L6_2acntS5639 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5648 = _M0L3synS1811->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5647;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5646;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5645;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5644;
        moonbit_string_t _M0L8_2afieldS5643;
        moonbit_string_t _M0L8_2afieldS5642;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5641;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5640;
        moonbit_decref_cycle_free(_M0L8_2afieldS5648);
        _M0L8_2afieldS5647 = _M0L3synS1811->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5647);
        _M0L8_2afieldS5646 = _M0L3synS1811->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5646);
        _M0L8_2afieldS5645 = _M0L3synS1811->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5645);
        _M0L8_2afieldS5644 = _M0L3synS1811->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5644);
        _M0L8_2afieldS5643 = _M0L3synS1811->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5643);
        _M0L8_2afieldS5642 = _M0L3synS1811->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5642);
        _M0L8_2afieldS5641 = _M0L3synS1811->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5641);
        _M0L8_2afieldS5640 = _M0L3synS1811->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5640);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1811);
      }
      _M0L6rowptrS5378 = _M0L6matrixS5383->$2;
      _M0L6_2acntS5650
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5383));
      if (_M0L6_2acntS5650 > 1) {
        int32_t _M0L11_2anew__cntS5653 = _M0L6_2acntS5650 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5383), _M0L11_2anew__cntS5653);
        moonbit_incref_cycle_free(_M0L6rowptrS5378);
      } else if (_M0L6_2acntS5650 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5652 = _M0L6matrixS5383->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5651;
        moonbit_decref_cycle_free(_M0L8_2afieldS5652);
        _M0L8_2afieldS5651 = _M0L6matrixS5383->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5651);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5383);
      }
      _M0L4varsS5379 = _M0L1eS1810->$3;
      _M0L5paramS5380 = _M0L1eS1810->$4;
      _M0L6t__nowS5382 = _M0L1eS1810->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5382);
      moonbit_incref_cycle_free(_M0L5paramS5380);
      moonbit_incref_cycle_free(_M0L4varsS5379);
      #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5381 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5382, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5382);
      #line 137 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt10stdp__step(_M0L4valsS5374, _M0L4fireS5375, _M0L4fireS5376, _M0L6colptrS5377, _M0L6rowptrS5378, _M0L4varsS5379, _M0L5paramS5380, _M0L6_2atmpS5381, _M0L2dtS1748);
      moonbit_decref_cycle_free(_M0L4valsS5374);
      moonbit_decref_cycle_free(_M0L4fireS5375);
      moonbit_decref_cycle_free(_M0L4fireS5376);
      moonbit_decref_cycle_free(_M0L6colptrS5377);
      moonbit_decref_cycle_free(_M0L6rowptrS5378);
      moonbit_decref_cycle_free(_M0L4varsS5379);
      moonbit_decref_cycle_free(_M0L5paramS5380);
      _M0L6t__nowS5388 = _M0L1eS1810->$5;
      _M0L6t__nowS5391 = _M0L1eS1810->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5388);
      _M0L6_2acntS5654 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1810));
      if (_M0L6_2acntS5654 > 1) {
        int32_t _M0L11_2anew__cntS5657 = _M0L6_2acntS5654 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1810), _M0L11_2anew__cntS5657);
        moonbit_incref_cycle_free(_M0L6t__nowS5391);
      } else if (_M0L6_2acntS5654 == 1) {
        struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L8_2afieldS5656 =
          _M0L1eS1810->$4;
        struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L8_2afieldS5655;
        moonbit_decref_cycle_free(_M0L8_2afieldS5656);
        _M0L8_2afieldS5655 = _M0L1eS1810->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5655);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1810);
      }
      #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5390 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5391, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5391);
      _M0L6_2atmpS5389 = _M0L6_2atmpS5390 + _M0L2dtS1748;
      #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5388, 0, _M0L6_2atmpS5389);
      moonbit_decref_cycle_free(_M0L6t__nowS5388);
      joinlet_5702:;
      goto joinlet_5701;
      join_1806:;
      _M0L5connsS5372 = _M0L1mS1743->$1;
      _M0L11conn__indexS5373 = _M0L1eS1807->$0;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1808
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5372, _M0L11conn__indexS5373);
      _M0L6matrixS5367 = _M0L3synS1808->$4;
      _M0L4valsS5355 = _M0L6matrixS5367->$4;
      _M0L3preS5366 = _M0L3synS1808->$0;
      _M0L4fireS5356 = _M0L3preS5366->$5;
      _M0L4postS5365 = _M0L3synS1808->$1;
      _M0L4fireS5357 = _M0L4postS5365->$5;
      _M0L6matrixS5364 = _M0L3synS1808->$4;
      _M0L6colptrS5358 = _M0L6matrixS5364->$3;
      _M0L6matrixS5363 = _M0L3synS1808->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5358);
      moonbit_incref_cycle_free(_M0L4fireS5357);
      moonbit_incref_cycle_free(_M0L4fireS5356);
      moonbit_incref_cycle_free(_M0L4valsS5355);
      _M0L6_2acntS5619
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1808));
      if (_M0L6_2acntS5619 > 1) {
        int32_t _M0L11_2anew__cntS5629 = _M0L6_2acntS5619 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1808), _M0L11_2anew__cntS5629);
        moonbit_incref_cycle_free(_M0L6matrixS5363);
      } else if (_M0L6_2acntS5619 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5628 = _M0L3synS1808->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5627;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5626;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5625;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5624;
        moonbit_string_t _M0L8_2afieldS5623;
        moonbit_string_t _M0L8_2afieldS5622;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5621;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5620;
        moonbit_decref_cycle_free(_M0L8_2afieldS5628);
        _M0L8_2afieldS5627 = _M0L3synS1808->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5627);
        _M0L8_2afieldS5626 = _M0L3synS1808->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5626);
        _M0L8_2afieldS5625 = _M0L3synS1808->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5625);
        _M0L8_2afieldS5624 = _M0L3synS1808->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5624);
        _M0L8_2afieldS5623 = _M0L3synS1808->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5623);
        _M0L8_2afieldS5622 = _M0L3synS1808->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5622);
        _M0L8_2afieldS5621 = _M0L3synS1808->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5621);
        _M0L8_2afieldS5620 = _M0L3synS1808->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5620);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1808);
      }
      _M0L6rowptrS5359 = _M0L6matrixS5363->$2;
      _M0L6_2acntS5630
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5363));
      if (_M0L6_2acntS5630 > 1) {
        int32_t _M0L11_2anew__cntS5633 = _M0L6_2acntS5630 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5363), _M0L11_2anew__cntS5633);
        moonbit_incref_cycle_free(_M0L6rowptrS5359);
      } else if (_M0L6_2acntS5630 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5632 = _M0L6matrixS5363->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5631;
        moonbit_decref_cycle_free(_M0L8_2afieldS5632);
        _M0L8_2afieldS5631 = _M0L6matrixS5363->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5631);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5363);
      }
      _M0L4tpreS5360 = _M0L1eS1807->$4;
      _M0L5tpostS5361 = _M0L1eS1807->$5;
      _M0L5paramS5362 = _M0L1eS1807->$3;
      moonbit_incref_cycle_free(_M0L5paramS5362);
      moonbit_incref_cycle_free(_M0L5tpostS5361);
      moonbit_incref_cycle_free(_M0L4tpreS5360);
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt24stdp__mexican__hat__step(_M0L4valsS5355, _M0L4fireS5356, _M0L4fireS5357, _M0L6colptrS5358, _M0L6rowptrS5359, _M0L4tpreS5360, _M0L5tpostS5361, _M0L5paramS5362, _M0L2dtS1748);
      moonbit_decref_cycle_free(_M0L4valsS5355);
      moonbit_decref_cycle_free(_M0L4fireS5356);
      moonbit_decref_cycle_free(_M0L4fireS5357);
      moonbit_decref_cycle_free(_M0L6colptrS5358);
      moonbit_decref_cycle_free(_M0L6rowptrS5359);
      moonbit_decref_cycle_free(_M0L4tpreS5360);
      moonbit_decref_cycle_free(_M0L5tpostS5361);
      moonbit_decref_cycle_free(_M0L5paramS5362);
      _M0L6t__nowS5368 = _M0L1eS1807->$6;
      _M0L6t__nowS5371 = _M0L1eS1807->$6;
      moonbit_incref_cycle_free(_M0L6t__nowS5368);
      _M0L6_2acntS5634 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1807));
      if (_M0L6_2acntS5634 > 1) {
        int32_t _M0L11_2anew__cntS5638 = _M0L6_2acntS5634 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1807), _M0L11_2anew__cntS5638);
        moonbit_incref_cycle_free(_M0L6t__nowS5371);
      } else if (_M0L6_2acntS5634 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5637 = _M0L1eS1807->$5;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5636;
        struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L8_2afieldS5635;
        moonbit_decref_cycle_free(_M0L8_2afieldS5637);
        _M0L8_2afieldS5636 = _M0L1eS1807->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS5636);
        _M0L8_2afieldS5635 = _M0L1eS1807->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5635);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1807);
      }
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5370 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5371, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5371);
      _M0L6_2atmpS5369 = _M0L6_2atmpS5370 + _M0L2dtS1748;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5368, 0, _M0L6_2atmpS5369);
      moonbit_decref_cycle_free(_M0L6t__nowS5368);
      joinlet_5701:;
      goto joinlet_5700;
      join_1803:;
      _M0L5connsS5353 = _M0L1mS1743->$1;
      _M0L11conn__indexS5354 = _M0L1eS1804->$0;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1805
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5353, _M0L11conn__indexS5354);
      _M0L6matrixS5348 = _M0L3synS1805->$4;
      _M0L4valsS5337 = _M0L6matrixS5348->$4;
      _M0L3preS5347 = _M0L3synS1805->$0;
      _M0L4fireS5338 = _M0L3preS5347->$5;
      _M0L4postS5346 = _M0L3synS1805->$1;
      _M0L4fireS5339 = _M0L4postS5346->$5;
      _M0L6matrixS5345 = _M0L3synS1805->$4;
      _M0L6colptrS5340 = _M0L6matrixS5345->$3;
      _M0L6matrixS5344 = _M0L3synS1805->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5340);
      moonbit_incref_cycle_free(_M0L4fireS5339);
      moonbit_incref_cycle_free(_M0L4fireS5338);
      moonbit_incref_cycle_free(_M0L4valsS5337);
      _M0L6_2acntS5600
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1805));
      if (_M0L6_2acntS5600 > 1) {
        int32_t _M0L11_2anew__cntS5610 = _M0L6_2acntS5600 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1805), _M0L11_2anew__cntS5610);
        moonbit_incref_cycle_free(_M0L6matrixS5344);
      } else if (_M0L6_2acntS5600 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5609 = _M0L3synS1805->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5608;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5607;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5606;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5605;
        moonbit_string_t _M0L8_2afieldS5604;
        moonbit_string_t _M0L8_2afieldS5603;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5602;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5601;
        moonbit_decref_cycle_free(_M0L8_2afieldS5609);
        _M0L8_2afieldS5608 = _M0L3synS1805->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5608);
        _M0L8_2afieldS5607 = _M0L3synS1805->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5607);
        _M0L8_2afieldS5606 = _M0L3synS1805->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5606);
        _M0L8_2afieldS5605 = _M0L3synS1805->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5605);
        _M0L8_2afieldS5604 = _M0L3synS1805->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5604);
        _M0L8_2afieldS5603 = _M0L3synS1805->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5603);
        _M0L8_2afieldS5602 = _M0L3synS1805->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5602);
        _M0L8_2afieldS5601 = _M0L3synS1805->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5601);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1805);
      }
      _M0L6rowptrS5341 = _M0L6matrixS5344->$2;
      _M0L6_2acntS5611
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5344));
      if (_M0L6_2acntS5611 > 1) {
        int32_t _M0L11_2anew__cntS5614 = _M0L6_2acntS5611 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5344), _M0L11_2anew__cntS5614);
        moonbit_incref_cycle_free(_M0L6rowptrS5341);
      } else if (_M0L6_2acntS5611 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5613 = _M0L6matrixS5344->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5612;
        moonbit_decref_cycle_free(_M0L8_2afieldS5613);
        _M0L8_2afieldS5612 = _M0L6matrixS5344->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5612);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5344);
      }
      _M0L4varsS5342 = _M0L1eS1804->$4;
      _M0L5paramS5343 = _M0L1eS1804->$3;
      moonbit_incref_cycle_free(_M0L5paramS5343);
      moonbit_incref_cycle_free(_M0L4varsS5342);
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt25stdp__antisymmetric__step(_M0L4valsS5337, _M0L4fireS5338, _M0L4fireS5339, _M0L6colptrS5340, _M0L6rowptrS5341, _M0L4varsS5342, _M0L5paramS5343, _M0L2dtS1748);
      moonbit_decref_cycle_free(_M0L4valsS5337);
      moonbit_decref_cycle_free(_M0L4fireS5338);
      moonbit_decref_cycle_free(_M0L4fireS5339);
      moonbit_decref_cycle_free(_M0L6colptrS5340);
      moonbit_decref_cycle_free(_M0L6rowptrS5341);
      moonbit_decref_cycle_free(_M0L4varsS5342);
      moonbit_decref_cycle_free(_M0L5paramS5343);
      _M0L6t__nowS5349 = _M0L1eS1804->$5;
      _M0L6t__nowS5352 = _M0L1eS1804->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5349);
      _M0L6_2acntS5615 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1804));
      if (_M0L6_2acntS5615 > 1) {
        int32_t _M0L11_2anew__cntS5618 = _M0L6_2acntS5615 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1804), _M0L11_2anew__cntS5618);
        moonbit_incref_cycle_free(_M0L6t__nowS5352);
      } else if (_M0L6_2acntS5615 == 1) {
        struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L8_2afieldS5617 =
          _M0L1eS1804->$4;
        struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L8_2afieldS5616;
        moonbit_decref_cycle_free(_M0L8_2afieldS5617);
        _M0L8_2afieldS5616 = _M0L1eS1804->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5616);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1804);
      }
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5351 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5352, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5352);
      _M0L6_2atmpS5350 = _M0L6_2atmpS5351 + _M0L2dtS1748;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5349, 0, _M0L6_2atmpS5350);
      moonbit_decref_cycle_free(_M0L6t__nowS5349);
      joinlet_5700:;
      goto joinlet_5699;
      join_1800:;
      _M0L5connsS5335 = _M0L1mS1743->$1;
      _M0L11conn__indexS5336 = _M0L1eS1801->$0;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1802
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5335, _M0L11conn__indexS5336);
      _M0L6matrixS5330 = _M0L3synS1802->$4;
      _M0L4valsS5317 = _M0L6matrixS5330->$4;
      _M0L3preS5329 = _M0L3synS1802->$0;
      _M0L4fireS5318 = _M0L3preS5329->$5;
      _M0L4postS5328 = _M0L3synS1802->$1;
      _M0L4fireS5319 = _M0L4postS5328->$5;
      _M0L6matrixS5327 = _M0L3synS1802->$4;
      _M0L6colptrS5320 = _M0L6matrixS5327->$3;
      _M0L6matrixS5326 = _M0L3synS1802->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5320);
      moonbit_incref_cycle_free(_M0L4fireS5319);
      moonbit_incref_cycle_free(_M0L4fireS5318);
      moonbit_incref_cycle_free(_M0L4valsS5317);
      _M0L6_2acntS5581
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1802));
      if (_M0L6_2acntS5581 > 1) {
        int32_t _M0L11_2anew__cntS5591 = _M0L6_2acntS5581 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1802), _M0L11_2anew__cntS5591);
        moonbit_incref_cycle_free(_M0L6matrixS5326);
      } else if (_M0L6_2acntS5581 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5590 = _M0L3synS1802->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5589;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5588;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5587;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5586;
        moonbit_string_t _M0L8_2afieldS5585;
        moonbit_string_t _M0L8_2afieldS5584;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5583;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5582;
        moonbit_decref_cycle_free(_M0L8_2afieldS5590);
        _M0L8_2afieldS5589 = _M0L3synS1802->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5589);
        _M0L8_2afieldS5588 = _M0L3synS1802->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5588);
        _M0L8_2afieldS5587 = _M0L3synS1802->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5587);
        _M0L8_2afieldS5586 = _M0L3synS1802->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5586);
        _M0L8_2afieldS5585 = _M0L3synS1802->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5585);
        _M0L8_2afieldS5584 = _M0L3synS1802->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5584);
        _M0L8_2afieldS5583 = _M0L3synS1802->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5583);
        _M0L8_2afieldS5582 = _M0L3synS1802->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5582);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1802);
      }
      _M0L6rowptrS5321 = _M0L6matrixS5326->$2;
      _M0L6_2acntS5592
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5326));
      if (_M0L6_2acntS5592 > 1) {
        int32_t _M0L11_2anew__cntS5595 = _M0L6_2acntS5592 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5326), _M0L11_2anew__cntS5595);
        moonbit_incref_cycle_free(_M0L6rowptrS5321);
      } else if (_M0L6_2acntS5592 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5594 = _M0L6matrixS5326->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5593;
        moonbit_decref_cycle_free(_M0L8_2afieldS5594);
        _M0L8_2afieldS5593 = _M0L6matrixS5326->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5593);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5326);
      }
      _M0L4varsS5322 = _M0L1eS1801->$4;
      _M0L5paramS5323 = _M0L1eS1801->$3;
      _M0L6t__nowS5325 = _M0L1eS1801->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5325);
      moonbit_incref_cycle_free(_M0L5paramS5323);
      moonbit_incref_cycle_free(_M0L4varsS5322);
      #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5324 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5325, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5325);
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt22stdp__confavreux__step(_M0L4valsS5317, _M0L4fireS5318, _M0L4fireS5319, _M0L6colptrS5320, _M0L6rowptrS5321, _M0L4varsS5322, _M0L5paramS5323, _M0L6_2atmpS5324, _M0L2dtS1748);
      moonbit_decref_cycle_free(_M0L4valsS5317);
      moonbit_decref_cycle_free(_M0L4fireS5318);
      moonbit_decref_cycle_free(_M0L4fireS5319);
      moonbit_decref_cycle_free(_M0L6colptrS5320);
      moonbit_decref_cycle_free(_M0L6rowptrS5321);
      moonbit_decref_cycle_free(_M0L4varsS5322);
      moonbit_decref_cycle_free(_M0L5paramS5323);
      _M0L6t__nowS5331 = _M0L1eS1801->$5;
      _M0L6t__nowS5334 = _M0L1eS1801->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5331);
      _M0L6_2acntS5596 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1801));
      if (_M0L6_2acntS5596 > 1) {
        int32_t _M0L11_2anew__cntS5599 = _M0L6_2acntS5596 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1801), _M0L11_2anew__cntS5599);
        moonbit_incref_cycle_free(_M0L6t__nowS5334);
      } else if (_M0L6_2acntS5596 == 1) {
        struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L8_2afieldS5598 =
          _M0L1eS1801->$4;
        struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L8_2afieldS5597;
        moonbit_decref_cycle_free(_M0L8_2afieldS5598);
        _M0L8_2afieldS5597 = _M0L1eS1801->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5597);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1801);
      }
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5333 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5334, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5334);
      _M0L6_2atmpS5332 = _M0L6_2atmpS5333 + _M0L2dtS1748;
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5331, 0, _M0L6_2atmpS5332);
      moonbit_decref_cycle_free(_M0L6t__nowS5331);
      joinlet_5699:;
      goto joinlet_5698;
      join_1797:;
      _M0L5connsS5315 = _M0L1mS1743->$1;
      _M0L11conn__indexS5316 = _M0L1eS1798->$0;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1799
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5315, _M0L11conn__indexS5316);
      _M0L6matrixS5310 = _M0L3synS1799->$4;
      _M0L4valsS5297 = _M0L6matrixS5310->$4;
      _M0L3preS5309 = _M0L3synS1799->$0;
      _M0L4fireS5298 = _M0L3preS5309->$5;
      _M0L4postS5308 = _M0L3synS1799->$1;
      _M0L4fireS5299 = _M0L4postS5308->$5;
      _M0L6matrixS5307 = _M0L3synS1799->$4;
      _M0L6colptrS5300 = _M0L6matrixS5307->$3;
      _M0L6matrixS5306 = _M0L3synS1799->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5300);
      moonbit_incref_cycle_free(_M0L4fireS5299);
      moonbit_incref_cycle_free(_M0L4fireS5298);
      moonbit_incref_cycle_free(_M0L4valsS5297);
      _M0L6_2acntS5562
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1799));
      if (_M0L6_2acntS5562 > 1) {
        int32_t _M0L11_2anew__cntS5572 = _M0L6_2acntS5562 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1799), _M0L11_2anew__cntS5572);
        moonbit_incref_cycle_free(_M0L6matrixS5306);
      } else if (_M0L6_2acntS5562 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5571 = _M0L3synS1799->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5570;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5569;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5568;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5567;
        moonbit_string_t _M0L8_2afieldS5566;
        moonbit_string_t _M0L8_2afieldS5565;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5564;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5563;
        moonbit_decref_cycle_free(_M0L8_2afieldS5571);
        _M0L8_2afieldS5570 = _M0L3synS1799->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5570);
        _M0L8_2afieldS5569 = _M0L3synS1799->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5569);
        _M0L8_2afieldS5568 = _M0L3synS1799->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5568);
        _M0L8_2afieldS5567 = _M0L3synS1799->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5567);
        _M0L8_2afieldS5566 = _M0L3synS1799->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5566);
        _M0L8_2afieldS5565 = _M0L3synS1799->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5565);
        _M0L8_2afieldS5564 = _M0L3synS1799->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5564);
        _M0L8_2afieldS5563 = _M0L3synS1799->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5563);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1799);
      }
      _M0L6rowptrS5301 = _M0L6matrixS5306->$2;
      _M0L6_2acntS5573
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5306));
      if (_M0L6_2acntS5573 > 1) {
        int32_t _M0L11_2anew__cntS5576 = _M0L6_2acntS5573 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5306), _M0L11_2anew__cntS5576);
        moonbit_incref_cycle_free(_M0L6rowptrS5301);
      } else if (_M0L6_2acntS5573 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5575 = _M0L6matrixS5306->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5574;
        moonbit_decref_cycle_free(_M0L8_2afieldS5575);
        _M0L8_2afieldS5574 = _M0L6matrixS5306->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5574);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5306);
      }
      _M0L4varsS5302 = _M0L1eS1798->$4;
      _M0L5paramS5303 = _M0L1eS1798->$3;
      _M0L6t__nowS5305 = _M0L1eS1798->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5305);
      moonbit_incref_cycle_free(_M0L5paramS5303);
      moonbit_incref_cycle_free(_M0L4varsS5302);
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5304 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5305, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5305);
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt17istdp__rate__step(_M0L4valsS5297, _M0L4fireS5298, _M0L4fireS5299, _M0L6colptrS5300, _M0L6rowptrS5301, _M0L4varsS5302, _M0L5paramS5303, _M0L6_2atmpS5304, _M0L2dtS1748);
      moonbit_decref_cycle_free(_M0L4valsS5297);
      moonbit_decref_cycle_free(_M0L4fireS5298);
      moonbit_decref_cycle_free(_M0L4fireS5299);
      moonbit_decref_cycle_free(_M0L6colptrS5300);
      moonbit_decref_cycle_free(_M0L6rowptrS5301);
      moonbit_decref_cycle_free(_M0L4varsS5302);
      moonbit_decref_cycle_free(_M0L5paramS5303);
      _M0L6t__nowS5311 = _M0L1eS1798->$5;
      _M0L6t__nowS5314 = _M0L1eS1798->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5311);
      _M0L6_2acntS5577 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1798));
      if (_M0L6_2acntS5577 > 1) {
        int32_t _M0L11_2anew__cntS5580 = _M0L6_2acntS5577 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1798), _M0L11_2anew__cntS5580);
        moonbit_incref_cycle_free(_M0L6t__nowS5314);
      } else if (_M0L6_2acntS5577 == 1) {
        struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L8_2afieldS5579 =
          _M0L1eS1798->$4;
        struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L8_2afieldS5578;
        moonbit_decref_cycle_free(_M0L8_2afieldS5579);
        _M0L8_2afieldS5578 = _M0L1eS1798->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5578);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1798);
      }
      #line 207 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5313 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5314, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5314);
      _M0L6_2atmpS5312 = _M0L6_2atmpS5313 + _M0L2dtS1748;
      #line 207 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5311, 0, _M0L6_2atmpS5312);
      moonbit_decref_cycle_free(_M0L6t__nowS5311);
      joinlet_5698:;
      goto joinlet_5697;
      join_1794:;
      _M0L5connsS5295 = _M0L1mS1743->$1;
      _M0L11conn__indexS5296 = _M0L1eS1795->$0;
      #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1796
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5295, _M0L11conn__indexS5296);
      _M0L6matrixS5290 = _M0L3synS1796->$4;
      _M0L4valsS5275 = _M0L6matrixS5290->$4;
      _M0L3preS5289 = _M0L3synS1796->$0;
      _M0L4fireS5276 = _M0L3preS5289->$5;
      _M0L4postS5288 = _M0L3synS1796->$1;
      _M0L4fireS5277 = _M0L4postS5288->$5;
      _M0L6matrixS5287 = _M0L3synS1796->$4;
      _M0L6colptrS5278 = _M0L6matrixS5287->$3;
      _M0L6matrixS5286 = _M0L3synS1796->$4;
      _M0L6rowptrS5279 = _M0L6matrixS5286->$2;
      _M0L4postS5285 = _M0L3synS1796->$1;
      moonbit_incref_cycle_free(_M0L6rowptrS5279);
      moonbit_incref_cycle_free(_M0L6colptrS5278);
      moonbit_incref_cycle_free(_M0L4fireS5277);
      moonbit_incref_cycle_free(_M0L4fireS5276);
      moonbit_incref_cycle_free(_M0L4valsS5275);
      _M0L6_2acntS5530
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1796));
      if (_M0L6_2acntS5530 > 1) {
        int32_t _M0L11_2anew__cntS5540 = _M0L6_2acntS5530 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1796), _M0L11_2anew__cntS5540);
        moonbit_incref_cycle_free(_M0L4postS5285);
      } else if (_M0L6_2acntS5530 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5539 = _M0L3synS1796->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5538;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5537;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5536;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5535;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L8_2afieldS5534;
        moonbit_string_t _M0L8_2afieldS5533;
        moonbit_string_t _M0L8_2afieldS5532;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5531;
        moonbit_decref_cycle_free(_M0L8_2afieldS5539);
        _M0L8_2afieldS5538 = _M0L3synS1796->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5538);
        _M0L8_2afieldS5537 = _M0L3synS1796->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5537);
        _M0L8_2afieldS5536 = _M0L3synS1796->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5536);
        _M0L8_2afieldS5535 = _M0L3synS1796->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5535);
        _M0L8_2afieldS5534 = _M0L3synS1796->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS5534);
        _M0L8_2afieldS5533 = _M0L3synS1796->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5533);
        _M0L8_2afieldS5532 = _M0L3synS1796->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5532);
        _M0L8_2afieldS5531 = _M0L3synS1796->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5531);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1796);
      }
      _M0L1vS5280 = _M0L4postS5285->$3;
      _M0L6_2acntS5541
      = Moonbit_rc_count(Moonbit_object_header(_M0L4postS5285));
      if (_M0L6_2acntS5541 > 1) {
        int32_t _M0L11_2anew__cntS5557 = _M0L6_2acntS5541 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L4postS5285), _M0L11_2anew__cntS5557);
        moonbit_incref_cycle_free(_M0L1vS5280);
      } else if (_M0L6_2acntS5541 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5556 = _M0L4postS5285->$16;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5555;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5554;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5553;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5552;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5551;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5550;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5549;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5548;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5547;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5546;
        struct _M0TPB5ArrayGbE* _M0L8_2afieldS5545;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5544;
        struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L8_2afieldS5543;
        struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L8_2afieldS5542;
        moonbit_decref_cycle_free(_M0L8_2afieldS5556);
        _M0L8_2afieldS5555 = _M0L4postS5285->$15;
        moonbit_decref_cycle_free(_M0L8_2afieldS5555);
        _M0L8_2afieldS5554 = _M0L4postS5285->$14;
        moonbit_decref_cycle_free(_M0L8_2afieldS5554);
        _M0L8_2afieldS5553 = _M0L4postS5285->$13;
        moonbit_decref_cycle_free(_M0L8_2afieldS5553);
        _M0L8_2afieldS5552 = _M0L4postS5285->$12;
        moonbit_decref_cycle_free(_M0L8_2afieldS5552);
        _M0L8_2afieldS5551 = _M0L4postS5285->$11;
        moonbit_decref_cycle_free(_M0L8_2afieldS5551);
        _M0L8_2afieldS5550 = _M0L4postS5285->$10;
        moonbit_decref_cycle_free(_M0L8_2afieldS5550);
        _M0L8_2afieldS5549 = _M0L4postS5285->$9;
        moonbit_decref_cycle_free(_M0L8_2afieldS5549);
        _M0L8_2afieldS5548 = _M0L4postS5285->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5548);
        _M0L8_2afieldS5547 = _M0L4postS5285->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5547);
        _M0L8_2afieldS5546 = _M0L4postS5285->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5546);
        _M0L8_2afieldS5545 = _M0L4postS5285->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5545);
        _M0L8_2afieldS5544 = _M0L4postS5285->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS5544);
        _M0L8_2afieldS5543 = _M0L4postS5285->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5543);
        _M0L8_2afieldS5542 = _M0L4postS5285->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5542);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L4postS5285);
      }
      _M0L4varsS5281 = _M0L1eS1795->$4;
      _M0L5paramS5282 = _M0L1eS1795->$3;
      _M0L6t__nowS5284 = _M0L1eS1795->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5284);
      moonbit_incref_cycle_free(_M0L5paramS5282);
      moonbit_incref_cycle_free(_M0L4varsS5281);
      #line 220 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5283 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5284, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5284);
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt22istdp__potential__step(_M0L4valsS5275, _M0L4fireS5276, _M0L4fireS5277, _M0L6colptrS5278, _M0L6rowptrS5279, _M0L1vS5280, _M0L4varsS5281, _M0L5paramS5282, _M0L6_2atmpS5283, _M0L2dtS1748);
      moonbit_decref_cycle_free(_M0L4valsS5275);
      moonbit_decref_cycle_free(_M0L4fireS5276);
      moonbit_decref_cycle_free(_M0L4fireS5277);
      moonbit_decref_cycle_free(_M0L6colptrS5278);
      moonbit_decref_cycle_free(_M0L6rowptrS5279);
      moonbit_decref_cycle_free(_M0L1vS5280);
      moonbit_decref_cycle_free(_M0L4varsS5281);
      moonbit_decref_cycle_free(_M0L5paramS5282);
      _M0L6t__nowS5291 = _M0L1eS1795->$5;
      _M0L6t__nowS5294 = _M0L1eS1795->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5291);
      _M0L6_2acntS5558 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1795));
      if (_M0L6_2acntS5558 > 1) {
        int32_t _M0L11_2anew__cntS5561 = _M0L6_2acntS5558 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1795), _M0L11_2anew__cntS5561);
        moonbit_incref_cycle_free(_M0L6t__nowS5294);
      } else if (_M0L6_2acntS5558 == 1) {
        struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L8_2afieldS5560 =
          _M0L1eS1795->$4;
        struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L8_2afieldS5559;
        moonbit_decref_cycle_free(_M0L8_2afieldS5560);
        _M0L8_2afieldS5559 = _M0L1eS1795->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5559);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1795);
      }
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5293 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5294, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5294);
      _M0L6_2atmpS5292 = _M0L6_2atmpS5293 + _M0L2dtS1748;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5291, 0, _M0L6_2atmpS5292);
      moonbit_decref_cycle_free(_M0L6t__nowS5291);
      joinlet_5697:;
      goto joinlet_5696;
      join_1791:;
      _M0L5connsS5273 = _M0L1mS1743->$1;
      _M0L11conn__indexS5274 = _M0L1eS1792->$0;
      #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1793
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5273, _M0L11conn__indexS5274);
      _M0L6matrixS5268 = _M0L3synS1793->$4;
      _M0L4valsS5255 = _M0L6matrixS5268->$4;
      _M0L3preS5267 = _M0L3synS1793->$0;
      _M0L4fireS5256 = _M0L3preS5267->$5;
      _M0L4postS5266 = _M0L3synS1793->$1;
      _M0L4fireS5257 = _M0L4postS5266->$5;
      _M0L6matrixS5265 = _M0L3synS1793->$4;
      _M0L6colptrS5258 = _M0L6matrixS5265->$3;
      _M0L6matrixS5264 = _M0L3synS1793->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5258);
      moonbit_incref_cycle_free(_M0L4fireS5257);
      moonbit_incref_cycle_free(_M0L4fireS5256);
      moonbit_incref_cycle_free(_M0L4valsS5255);
      _M0L6_2acntS5511
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1793));
      if (_M0L6_2acntS5511 > 1) {
        int32_t _M0L11_2anew__cntS5521 = _M0L6_2acntS5511 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1793), _M0L11_2anew__cntS5521);
        moonbit_incref_cycle_free(_M0L6matrixS5264);
      } else if (_M0L6_2acntS5511 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5520 = _M0L3synS1793->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5519;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5518;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5517;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5516;
        moonbit_string_t _M0L8_2afieldS5515;
        moonbit_string_t _M0L8_2afieldS5514;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5513;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5512;
        moonbit_decref_cycle_free(_M0L8_2afieldS5520);
        _M0L8_2afieldS5519 = _M0L3synS1793->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5519);
        _M0L8_2afieldS5518 = _M0L3synS1793->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5518);
        _M0L8_2afieldS5517 = _M0L3synS1793->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5517);
        _M0L8_2afieldS5516 = _M0L3synS1793->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5516);
        _M0L8_2afieldS5515 = _M0L3synS1793->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5515);
        _M0L8_2afieldS5514 = _M0L3synS1793->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5514);
        _M0L8_2afieldS5513 = _M0L3synS1793->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5513);
        _M0L8_2afieldS5512 = _M0L3synS1793->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5512);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1793);
      }
      _M0L6rowptrS5259 = _M0L6matrixS5264->$2;
      _M0L6_2acntS5522
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5264));
      if (_M0L6_2acntS5522 > 1) {
        int32_t _M0L11_2anew__cntS5525 = _M0L6_2acntS5522 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5264), _M0L11_2anew__cntS5525);
        moonbit_incref_cycle_free(_M0L6rowptrS5259);
      } else if (_M0L6_2acntS5522 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5524 = _M0L6matrixS5264->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5523;
        moonbit_decref_cycle_free(_M0L8_2afieldS5524);
        _M0L8_2afieldS5523 = _M0L6matrixS5264->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5523);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5264);
      }
      _M0L4varsS5260 = _M0L1eS1792->$4;
      _M0L5paramS5261 = _M0L1eS1792->$3;
      _M0L6t__nowS5263 = _M0L1eS1792->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5263);
      moonbit_incref_cycle_free(_M0L5paramS5261);
      moonbit_incref_cycle_free(_M0L4varsS5260);
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5262 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5263, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5263);
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt21stdp__symmetric__step(_M0L4valsS5255, _M0L4fireS5256, _M0L4fireS5257, _M0L6colptrS5258, _M0L6rowptrS5259, _M0L4varsS5260, _M0L5paramS5261, _M0L6_2atmpS5262, _M0L2dtS1748);
      moonbit_decref_cycle_free(_M0L4valsS5255);
      moonbit_decref_cycle_free(_M0L4fireS5256);
      moonbit_decref_cycle_free(_M0L4fireS5257);
      moonbit_decref_cycle_free(_M0L6colptrS5258);
      moonbit_decref_cycle_free(_M0L6rowptrS5259);
      moonbit_decref_cycle_free(_M0L4varsS5260);
      moonbit_decref_cycle_free(_M0L5paramS5261);
      _M0L6t__nowS5269 = _M0L1eS1792->$5;
      _M0L6t__nowS5272 = _M0L1eS1792->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5269);
      _M0L6_2acntS5526 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1792));
      if (_M0L6_2acntS5526 > 1) {
        int32_t _M0L11_2anew__cntS5529 = _M0L6_2acntS5526 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1792), _M0L11_2anew__cntS5529);
        moonbit_incref_cycle_free(_M0L6t__nowS5272);
      } else if (_M0L6_2acntS5526 == 1) {
        struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L8_2afieldS5528 =
          _M0L1eS1792->$4;
        struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L8_2afieldS5527;
        moonbit_decref_cycle_free(_M0L8_2afieldS5528);
        _M0L8_2afieldS5527 = _M0L1eS1792->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5527);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1792);
      }
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5271 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5272, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5272);
      _M0L6_2atmpS5270 = _M0L6_2atmpS5271 + _M0L2dtS1748;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5269, 0, _M0L6_2atmpS5270);
      moonbit_decref_cycle_free(_M0L6t__nowS5269);
      joinlet_5696:;
      goto joinlet_5695;
      join_1788:;
      _M0L5connsS5253 = _M0L1mS1743->$1;
      _M0L11conn__indexS5254 = _M0L1eS1789->$0;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1790
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5253, _M0L11conn__indexS5254);
      _M0L6matrixS5248 = _M0L3synS1790->$4;
      _M0L4valsS5235 = _M0L6matrixS5248->$4;
      _M0L3preS5247 = _M0L3synS1790->$0;
      _M0L4fireS5236 = _M0L3preS5247->$5;
      _M0L4postS5246 = _M0L3synS1790->$1;
      _M0L4fireS5237 = _M0L4postS5246->$5;
      _M0L6matrixS5245 = _M0L3synS1790->$4;
      _M0L6colptrS5238 = _M0L6matrixS5245->$3;
      _M0L6matrixS5244 = _M0L3synS1790->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5238);
      moonbit_incref_cycle_free(_M0L4fireS5237);
      moonbit_incref_cycle_free(_M0L4fireS5236);
      moonbit_incref_cycle_free(_M0L4valsS5235);
      _M0L6_2acntS5492
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1790));
      if (_M0L6_2acntS5492 > 1) {
        int32_t _M0L11_2anew__cntS5502 = _M0L6_2acntS5492 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1790), _M0L11_2anew__cntS5502);
        moonbit_incref_cycle_free(_M0L6matrixS5244);
      } else if (_M0L6_2acntS5492 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5501 = _M0L3synS1790->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5500;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5499;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5498;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5497;
        moonbit_string_t _M0L8_2afieldS5496;
        moonbit_string_t _M0L8_2afieldS5495;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5494;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5493;
        moonbit_decref_cycle_free(_M0L8_2afieldS5501);
        _M0L8_2afieldS5500 = _M0L3synS1790->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5500);
        _M0L8_2afieldS5499 = _M0L3synS1790->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5499);
        _M0L8_2afieldS5498 = _M0L3synS1790->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5498);
        _M0L8_2afieldS5497 = _M0L3synS1790->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5497);
        _M0L8_2afieldS5496 = _M0L3synS1790->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5496);
        _M0L8_2afieldS5495 = _M0L3synS1790->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5495);
        _M0L8_2afieldS5494 = _M0L3synS1790->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5494);
        _M0L8_2afieldS5493 = _M0L3synS1790->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5493);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1790);
      }
      _M0L6rowptrS5239 = _M0L6matrixS5244->$2;
      _M0L6_2acntS5503
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5244));
      if (_M0L6_2acntS5503 > 1) {
        int32_t _M0L11_2anew__cntS5506 = _M0L6_2acntS5503 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5244), _M0L11_2anew__cntS5506);
        moonbit_incref_cycle_free(_M0L6rowptrS5239);
      } else if (_M0L6_2acntS5503 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5505 = _M0L6matrixS5244->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5504;
        moonbit_decref_cycle_free(_M0L8_2afieldS5505);
        _M0L8_2afieldS5504 = _M0L6matrixS5244->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5504);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5244);
      }
      _M0L4varsS5240 = _M0L1eS1789->$4;
      _M0L5paramS5241 = _M0L1eS1789->$3;
      _M0L6t__nowS5243 = _M0L1eS1789->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5243);
      moonbit_incref_cycle_free(_M0L5paramS5241);
      moonbit_incref_cycle_free(_M0L4varsS5240);
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5242 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5243, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5243);
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt20ca__plasticity__step(_M0L4valsS5235, _M0L4fireS5236, _M0L4fireS5237, _M0L6colptrS5238, _M0L6rowptrS5239, _M0L4varsS5240, _M0L5paramS5241, _M0L6_2atmpS5242, _M0L2dtS1748);
      moonbit_decref_cycle_free(_M0L4valsS5235);
      moonbit_decref_cycle_free(_M0L4fireS5236);
      moonbit_decref_cycle_free(_M0L4fireS5237);
      moonbit_decref_cycle_free(_M0L6colptrS5238);
      moonbit_decref_cycle_free(_M0L6rowptrS5239);
      moonbit_decref_cycle_free(_M0L4varsS5240);
      moonbit_decref_cycle_free(_M0L5paramS5241);
      _M0L6t__nowS5249 = _M0L1eS1789->$5;
      _M0L6t__nowS5252 = _M0L1eS1789->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5249);
      _M0L6_2acntS5507 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1789));
      if (_M0L6_2acntS5507 > 1) {
        int32_t _M0L11_2anew__cntS5510 = _M0L6_2acntS5507 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1789), _M0L11_2anew__cntS5510);
        moonbit_incref_cycle_free(_M0L6t__nowS5252);
      } else if (_M0L6_2acntS5507 == 1) {
        struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L8_2afieldS5509 =
          _M0L1eS1789->$4;
        struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L8_2afieldS5508;
        moonbit_decref_cycle_free(_M0L8_2afieldS5509);
        _M0L8_2afieldS5508 = _M0L1eS1789->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5508);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1789);
      }
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5251 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5252, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5252);
      _M0L6_2atmpS5250 = _M0L6_2atmpS5251 + _M0L2dtS1748;
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5249, 0, _M0L6_2atmpS5250);
      moonbit_decref_cycle_free(_M0L6t__nowS5249);
      joinlet_5695:;
      _M0L6_2atmpS5394 = _M0L2__S1786 + 1;
      _M0L2__S1786 = _M0L6_2atmpS5394;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1785);
    }
    break;
  }
  _M0L7_2abindS1829 = _M0L1mS1743->$0;
  _M0L7_2abindS1830 = _M0L7_2abindS1829->$1;
  _M0L7_2abindS1831 = _M0L7_2abindS1829->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1831);
  _M0L2__S1832 = 0;
  while (1) {
    if (_M0L2__S1832 < _M0L7_2abindS1830) {
      void* _M0L1pS1833 = (void*)_M0L7_2abindS1831[_M0L2__S1832];
      int32_t _M0L6_2atmpS5395;
      moonbit_incref_cycle_free(_M0L1pS1833);
      #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt14integrate__any(_M0L1pS1833, _M0L2dtS1748);
      moonbit_decref_cycle_free(_M0L1pS1833);
      _M0L6_2atmpS5395 = _M0L2__S1832 + 1;
      _M0L2__S1832 = _M0L6_2atmpS5395;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1831);
    }
    break;
  }
  _M0L7_2abindS1835 = _M0L1mS1743->$4;
  _M0L7_2abindS1836 = _M0L7_2abindS1835->$1;
  _M0L7_2abindS1837 = _M0L7_2abindS1835->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1837);
  _M0L2__S1838 = 0;
  while (1) {
    if (_M0L2__S1838 < _M0L7_2abindS1836) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L3monS1839 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS1837[
          _M0L2__S1838
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5397 = _M0L1mS1743->$3;
      float _M0L6_2atmpS5396;
      int32_t _M0L6_2atmpS5398;
      moonbit_incref_cycle_free(_M0L3monS1839);
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5396 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5397);
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L3monS1839, _M0L6_2atmpS5396);
      moonbit_decref_cycle_free(_M0L3monS1839);
      _M0L6_2atmpS5398 = _M0L2__S1838 + 1;
      _M0L2__S1838 = _M0L6_2atmpS5398;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1837);
    }
    break;
  }
  _M0L4timeS5399 = _M0L1mS1743->$3;
  #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt12update__time(_M0L4timeS5399, _M0L2dtS1748);
  return 0;
}

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0FP26RiantR8snn__mbt7compose(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L4popsS1740,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS1741,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L11stims_2eoptS1729,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L14monitors_2eoptS1732,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L10stdp_2eoptS1735,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L9stp_2eoptS1738
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L5stimsS1728;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L8monitorsS1731;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L4stdpS1734;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L3stpS1737;
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _result_5705;
  if (_M0L11stims_2eoptS1729 == 0) {
    void** _M0L6_2atmpS5207 = (void**)moonbit_empty_ref_array;
    _M0L5stimsS1728
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE));
    Moonbit_object_header(_M0L5stimsS1728)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
    _M0L5stimsS1728->$0 = _M0L6_2atmpS5207;
    _M0L5stimsS1728->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L7_2aSomeS1730 =
      _M0L11stims_2eoptS1729;
    if (_M0L7_2aSomeS1730) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1730);
    }
    _M0L5stimsS1728 = _M0L7_2aSomeS1730;
  }
  if (_M0L14monitors_2eoptS1732 == 0) {
    struct _M0TP26RiantR8snn__mbt7Monitor** _M0L6_2atmpS5206 =
      (struct _M0TP26RiantR8snn__mbt7Monitor**)moonbit_empty_ref_array;
    _M0L8monitorsS1731
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE));
    Moonbit_object_header(_M0L8monitorsS1731)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
    _M0L8monitorsS1731->$0 = _M0L6_2atmpS5206;
    _M0L8monitorsS1731->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2aSomeS1733 =
      _M0L14monitors_2eoptS1732;
    if (_M0L7_2aSomeS1733) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1733);
    }
    _M0L8monitorsS1731 = _M0L7_2aSomeS1733;
  }
  if (_M0L10stdp_2eoptS1735 == 0) {
    void** _M0L6_2atmpS5205 = (void**)moonbit_empty_ref_array;
    _M0L4stdpS1734
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE));
    Moonbit_object_header(_M0L4stdpS1734)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
    _M0L4stdpS1734->$0 = _M0L6_2atmpS5205;
    _M0L4stdpS1734->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L7_2aSomeS1736 =
      _M0L10stdp_2eoptS1735;
    if (_M0L7_2aSomeS1736) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1736);
    }
    _M0L4stdpS1734 = _M0L7_2aSomeS1736;
  }
  if (_M0L9stp_2eoptS1738 == 0) {
    void** _M0L6_2atmpS5204 = (void**)moonbit_empty_ref_array;
    _M0L3stpS1737
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE));
    Moonbit_object_header(_M0L3stpS1737)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 27, 0);
    _M0L3stpS1737->$0 = _M0L6_2atmpS5204;
    _M0L3stpS1737->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L7_2aSomeS1739 =
      _M0L9stp_2eoptS1738;
    if (_M0L7_2aSomeS1739) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1739);
    }
    _M0L3stpS1737 = _M0L7_2aSomeS1739;
  }
  _result_5705
  = _M0FP26RiantR8snn__mbt15compose_2einner(_M0L4popsS1740, _M0L5connsS1741, _M0L5stimsS1728, _M0L8monitorsS1731, _M0L4stdpS1734, _M0L3stpS1737);
  moonbit_decref_cycle_free(_M0L5stimsS1728);
  moonbit_decref_cycle_free(_M0L8monitorsS1731);
  moonbit_decref_cycle_free(_M0L4stdpS1734);
  moonbit_decref_cycle_free(_M0L3stpS1737);
  return _result_5705;
}

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0FP26RiantR8snn__mbt15compose_2einner(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L4popsS1722,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS1723,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L5stimsS1724,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L8monitorsS1725,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L4stdpS1726,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L3stpS1727
) {
  struct _M0TP26RiantR8snn__mbt4Time* _M0L6_2atmpS5203;
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _block_5706;
  #line 79 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5203 = _M0MP26RiantR8snn__mbt4Time3new();
  moonbit_incref_cycle_free(_M0L4popsS1722);
  moonbit_incref_cycle_free(_M0L5connsS1723);
  moonbit_incref_cycle_free(_M0L5stimsS1724);
  moonbit_incref_cycle_free(_M0L8monitorsS1725);
  moonbit_incref_cycle_free(_M0L4stdpS1726);
  moonbit_incref_cycle_free(_M0L3stpS1727);
  _block_5706
  = (struct _M0TP26RiantR8snn__mbt18HeterogeneousModel*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt18HeterogeneousModel));
  Moonbit_object_header(_block_5706)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 30, 0);
  _block_5706->$0 = _M0L4popsS1722;
  _block_5706->$1 = _M0L5connsS1723;
  _block_5706->$2 = _M0L5stimsS1724;
  _block_5706->$3 = _M0L6_2atmpS5203;
  _block_5706->$4 = _M0L8monitorsS1725;
  _block_5706->$5 = _M0L4stdpS1726;
  _block_5706->$6 = _M0L3stpS1727;
  return _block_5706;
}

int32_t _M0FP26RiantR8snn__mbt14stimulate__any(
  void* _M0L1sS1708,
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS1696,
  float _M0L2dtS1703
) {
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L1xS1694;
  float _M0L1wS1695;
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L1xS1698;
  struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L1xS1700;
  struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L1xS1702;
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L1xS1705;
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L1xS1707;
  float _M0L6_2atmpS5202;
  float _M0L6_2atmpS5201;
  float _M0L6_2atmpS5200;
  float _M0L6_2atmpS5199;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  switch (Moonbit_object_tag(_M0L1sS1708)) {
    case 0: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__* _M0L14_2aPoissonIF__S1709 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__*)_M0L1sS1708;
      struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L4_2axS1710 =
        _M0L14_2aPoissonIF__S1709->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1710);
      _M0L1xS1707 = _M0L4_2axS1710;
      goto join_1706;
      break;
    }
    
    case 1: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim14PoissonLayer__* _M0L17_2aPoissonLayer__S1711 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim14PoissonLayer__*)_M0L1sS1708;
      struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L4_2axS1712 =
        _M0L17_2aPoissonLayer__S1711->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1712);
      _M0L1xS1705 = _M0L4_2axS1712;
      goto join_1704;
      break;
    }
    
    case 2: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim12BalancedIF__* _M0L15_2aBalancedIF__S1713 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim12BalancedIF__*)_M0L1sS1708;
      struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L4_2axS1714 =
        _M0L15_2aBalancedIF__S1713->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1714);
      _M0L1xS1702 = _M0L4_2axS1714;
      goto join_1701;
      break;
    }
    
    case 3: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__* _M0L14_2aCurrentIF__S1715 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__*)_M0L1sS1708;
      struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L4_2axS1716 =
        _M0L14_2aCurrentIF__S1715->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1716);
      _M0L1xS1700 = _M0L4_2axS1716;
      goto join_1699;
      break;
    }
    
    case 4: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__* _M0L15_2aCurrentArr__S1717 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__*)_M0L1sS1708;
      struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L4_2axS1718 =
        _M0L15_2aCurrentArr__S1717->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1718);
      _M0L1xS1698 = _M0L4_2axS1718;
      goto join_1697;
      break;
    }
    default: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__* _M0L14_2aTimedStim__S1719 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__*)_M0L1sS1708;
      struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L4_2axS1720 =
        _M0L14_2aTimedStim__S1719->$0;
      float _M0L4_2awS1721 = _M0L14_2aTimedStim__S1719->$1;
      moonbit_incref_cycle_free(_M0L4_2axS1720);
      _M0L1xS1694 = _M0L4_2axS1720;
      _M0L1wS1695 = _M0L4_2awS1721;
      goto join_1693;
      break;
    }
  }
  goto joinlet_5712;
  join_1706:;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5202 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1696);
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt13stimulate__if(_M0L1xS1707, _M0L6_2atmpS5202, _M0L2dtS1703);
  moonbit_decref_cycle_free(_M0L1xS1707);
  joinlet_5712:;
  goto joinlet_5711;
  join_1704:;
  #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5201 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1696);
  #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt16stimulate__layer(_M0L1xS1705, _M0L6_2atmpS5201, _M0L2dtS1703);
  moonbit_decref_cycle_free(_M0L1xS1705);
  joinlet_5711:;
  goto joinlet_5710;
  join_1701:;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5200 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1696);
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt19stimulate__balanced(_M0L1xS1702, _M0L6_2atmpS5200, _M0L2dtS1703);
  moonbit_decref_cycle_free(_M0L1xS1702);
  joinlet_5710:;
  goto joinlet_5709;
  join_1699:;
  #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt22stimulate__current__if(_M0L1xS1700);
  moonbit_decref_cycle_free(_M0L1xS1700);
  joinlet_5709:;
  goto joinlet_5708;
  join_1697:;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt25stimulate__current__array(_M0L1xS1698);
  moonbit_decref_cycle_free(_M0L1xS1698);
  joinlet_5708:;
  goto joinlet_5707;
  join_1693:;
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5199 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1696);
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt20stimulate__spiketime(_M0L1xS1694, _M0L6_2atmpS5199, _M0L1wS1695);
  moonbit_decref_cycle_free(_M0L1xS1694);
  joinlet_5707:;
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22istdp__potential__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1689,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1666,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1668,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1685,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1679,
  struct _M0TPB5ArrayGfE* _M0L7v__postS1676,
  struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L4varsS1672,
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS1670,
  float _M0L6t__nowS1664,
  float _M0L2dtS1673
) {
  int32_t _M0L6n__preS1665;
  int32_t _M0L7n__postS1667;
  float _M0L6tau__yS5198;
  float _M0L11inv__tau__yS1669;
  struct _M0TPB8MutLocalGiE* _M0L1jS1671;
  struct _M0TPB8MutLocalGiE* _M0L1iS1675;
  #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6n__preS1665 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1666);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L7n__postS1667 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1668);
  _M0L6tau__yS5198 = _M0L5paramS1670->$2;
  _M0L11inv__tau__yS1669 = 0x1p+0f / _M0L6tau__yS5198;
  _M0L1jS1671
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1671)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1671->$0 = 0;
  while (1) {
    int32_t _M0L3valS5115 = _M0L1jS1671->$0;
    if (_M0L3valS5115 < _M0L6n__preS1665) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS5116 = _M0L4varsS1672->$0;
      int32_t _M0L3valS5117 = _M0L1jS1671->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5126 = _M0L4varsS1672->$0;
      int32_t _M0L3valS5127 = _M0L1jS1671->$0;
      float _M0L6_2atmpS5119;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5124;
      int32_t _M0L3valS5125;
      float _M0L6_2atmpS5123;
      float _M0L6_2atmpS5122;
      float _M0L6_2atmpS5121;
      float _M0L6_2atmpS5120;
      float _M0L6_2atmpS5118;
      int32_t _M0L3valS5128;
      int32_t _M0L3valS5136;
      int32_t _M0L6_2atmpS5135;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5119
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5126, _M0L3valS5127);
      _M0L4tpreS5124 = _M0L4varsS1672->$0;
      _M0L3valS5125 = _M0L1jS1671->$0;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5123
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5124, _M0L3valS5125);
      _M0L6_2atmpS5122 = -_M0L6_2atmpS5123;
      _M0L6_2atmpS5121 = _M0L2dtS1673 * _M0L6_2atmpS5122;
      _M0L6_2atmpS5120 = _M0L6_2atmpS5121 * _M0L11inv__tau__yS1669;
      _M0L6_2atmpS5118 = _M0L6_2atmpS5119 + _M0L6_2atmpS5120;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS5116, _M0L3valS5117, _M0L6_2atmpS5118);
      _M0L3valS5128 = _M0L1jS1671->$0;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1666, _M0L3valS5128)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS5129 = _M0L4varsS1672->$0;
        int32_t _M0L3valS5130 = _M0L1jS1671->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS5133 = _M0L4varsS1672->$0;
        int32_t _M0L3valS5134 = _M0L1jS1671->$0;
        float _M0L6_2atmpS5132;
        float _M0L6_2atmpS5131;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5132
        = _M0MPC15array5Array2atGfE(_M0L4tpreS5133, _M0L3valS5134);
        _M0L6_2atmpS5131 = _M0L6_2atmpS5132 + 0x1p+0f;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS5129, _M0L3valS5130, _M0L6_2atmpS5131);
      }
      _M0L3valS5136 = _M0L1jS1671->$0;
      _M0L6_2atmpS5135 = _M0L3valS5136 + 1;
      _M0L1jS1671->$0 = _M0L6_2atmpS5135;
      continue;
    }
    break;
  }
  _M0L1iS1675
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1675)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1675->$0 = 0;
  while (1) {
    int32_t _M0L3valS5137 = _M0L1iS1675->$0;
    if (_M0L3valS5137 < _M0L7n__postS1667) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS5138 = _M0L4varsS1672->$1;
      int32_t _M0L3valS5139 = _M0L1iS1675->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5151 = _M0L4varsS1672->$1;
      int32_t _M0L3valS5152 = _M0L1iS1675->$0;
      float _M0L6_2atmpS5141;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5149;
      int32_t _M0L3valS5150;
      float _M0L6_2atmpS5146;
      int32_t _M0L3valS5148;
      float _M0L6_2atmpS5147;
      float _M0L6_2atmpS5145;
      float _M0L6_2atmpS5144;
      float _M0L6_2atmpS5143;
      float _M0L6_2atmpS5142;
      float _M0L6_2atmpS5140;
      int32_t _M0L3valS5153;
      int32_t _M0L3valS5161;
      int32_t _M0L6_2atmpS5160;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5141
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5151, _M0L3valS5152);
      _M0L5tpostS5149 = _M0L4varsS1672->$1;
      _M0L3valS5150 = _M0L1iS1675->$0;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5146
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5149, _M0L3valS5150);
      _M0L3valS5148 = _M0L1iS1675->$0;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5147
      = _M0MPC15array5Array2atGfE(_M0L7v__postS1676, _M0L3valS5148);
      _M0L6_2atmpS5145 = _M0L6_2atmpS5146 - _M0L6_2atmpS5147;
      _M0L6_2atmpS5144 = -_M0L6_2atmpS5145;
      _M0L6_2atmpS5143 = _M0L2dtS1673 * _M0L6_2atmpS5144;
      _M0L6_2atmpS5142 = _M0L6_2atmpS5143 * _M0L11inv__tau__yS1669;
      _M0L6_2atmpS5140 = _M0L6_2atmpS5141 + _M0L6_2atmpS5142;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS5138, _M0L3valS5139, _M0L6_2atmpS5140);
      _M0L3valS5153 = _M0L1iS1675->$0;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1668, _M0L3valS5153)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS5154 = _M0L4varsS1672->$1;
        int32_t _M0L3valS5155 = _M0L1iS1675->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS5158 = _M0L4varsS1672->$1;
        int32_t _M0L3valS5159 = _M0L1iS1675->$0;
        float _M0L6_2atmpS5157;
        float _M0L6_2atmpS5156;
        #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5157
        = _M0MPC15array5Array2atGfE(_M0L5tpostS5158, _M0L3valS5159);
        _M0L6_2atmpS5156 = _M0L6_2atmpS5157 + 0x1p+0f;
        #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS5154, _M0L3valS5155, _M0L6_2atmpS5156);
      }
      _M0L3valS5161 = _M0L1iS1675->$0;
      _M0L6_2atmpS5160 = _M0L3valS5161 + 1;
      _M0L1iS1675->$0 = _M0L6_2atmpS5160;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1675);
    }
    break;
  }
  _M0L1jS1671->$0 = 0;
  while (1) {
    int32_t _M0L3valS5162 = _M0L1jS1671->$0;
    if (_M0L3valS5162 < _M0L6n__preS1665) {
      int32_t _M0L3valS5197 = _M0L1jS1671->$0;
      int32_t _M0L5startS1678;
      int32_t _M0L3valS5196;
      int32_t _M0L6_2atmpS5195;
      int32_t _M0L3endS1680;
      int32_t _M0L3valS5194;
      int32_t _M0L10pre__firedS1681;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5192;
      int32_t _M0L3valS5193;
      float _M0L7tpre__jS1682;
      struct _M0TPB8MutLocalGiE* _M0L1sS1683;
      int32_t _M0L3valS5191;
      int32_t _M0L6_2atmpS5190;
      #line 358 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L5startS1678
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1679, _M0L3valS5197);
      _M0L3valS5196 = _M0L1jS1671->$0;
      _M0L6_2atmpS5195 = _M0L3valS5196 + 1;
      #line 359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L3endS1680
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1679, _M0L6_2atmpS5195);
      _M0L3valS5194 = _M0L1jS1671->$0;
      #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L10pre__firedS1681
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1666, _M0L3valS5194);
      _M0L4tpreS5192 = _M0L4varsS1672->$0;
      _M0L3valS5193 = _M0L1jS1671->$0;
      #line 361 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L7tpre__jS1682
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5192, _M0L3valS5193);
      _M0L1sS1683
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1683)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1683->$0 = _M0L5startS1678;
      while (1) {
        int32_t _M0L3valS5163 = _M0L1sS1683->$0;
        if (_M0L3valS5163 < _M0L3endS1680) {
          int32_t _M0L3valS5189 = _M0L1sS1683->$0;
          int32_t _M0L9post__idxS1684;
          int32_t _M0L11post__firedS1686;
          struct _M0TPB5ArrayGfE* _M0L5tpostS5188;
          float _M0L8tpost__iS1687;
          int32_t _M0L3valS5178;
          float _M0L6_2atmpS5176;
          float _M0L6w__minS5177;
          int32_t _M0L3valS5183;
          float _M0L6_2atmpS5181;
          float _M0L6w__maxS5182;
          int32_t _M0L3valS5187;
          int32_t _M0L6_2atmpS5186;
          #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L9post__idxS1684
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1685, _M0L3valS5189);
          #line 365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L11post__firedS1686
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1668, _M0L9post__idxS1684);
          _M0L5tpostS5188 = _M0L4varsS1672->$1;
          #line 366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L8tpost__iS1687
          = _M0MPC15array5Array2atGfE(_M0L5tpostS5188, _M0L9post__idxS1684);
          if (_M0L10pre__firedS1681) {
            float _M0L3etaS5168 = _M0L5paramS1670->$0;
            float _M0L2v0S5170 = _M0L5paramS1670->$1;
            float _M0L6_2atmpS5169 = _M0L8tpost__iS1687 - _M0L2v0S5170;
            float _M0L2dwS1688 = _M0L3etaS5168 * _M0L6_2atmpS5169;
            int32_t _M0L3valS5164 = _M0L1sS1683->$0;
            int32_t _M0L3valS5167 = _M0L1sS1683->$0;
            float _M0L6_2atmpS5166;
            float _M0L6_2atmpS5165;
            #line 369 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5166
            = _M0MPC15array5Array2atGfE(_M0L1wS1689, _M0L3valS5167);
            _M0L6_2atmpS5165 = _M0L6_2atmpS5166 + _M0L2dwS1688;
            #line 369 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1689, _M0L3valS5164, _M0L6_2atmpS5165);
          }
          if (_M0L11post__firedS1686) {
            float _M0L3etaS5175 = _M0L5paramS1670->$0;
            float _M0L2dwS1690 = _M0L3etaS5175 * _M0L7tpre__jS1682;
            int32_t _M0L3valS5171 = _M0L1sS1683->$0;
            int32_t _M0L3valS5174 = _M0L1sS1683->$0;
            float _M0L6_2atmpS5173;
            float _M0L6_2atmpS5172;
            #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5173
            = _M0MPC15array5Array2atGfE(_M0L1wS1689, _M0L3valS5174);
            _M0L6_2atmpS5172 = _M0L6_2atmpS5173 + _M0L2dwS1690;
            #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1689, _M0L3valS5171, _M0L6_2atmpS5172);
          }
          _M0L3valS5178 = _M0L1sS1683->$0;
          #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5176
          = _M0MPC15array5Array2atGfE(_M0L1wS1689, _M0L3valS5178);
          _M0L6w__minS5177 = _M0L5paramS1670->$4;
          if (_M0L6_2atmpS5176 < _M0L6w__minS5177) {
            int32_t _M0L3valS5179 = _M0L1sS1683->$0;
            float _M0L6w__minS5180 = _M0L5paramS1670->$4;
            #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1689, _M0L3valS5179, _M0L6w__minS5180);
          }
          _M0L3valS5183 = _M0L1sS1683->$0;
          #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5181
          = _M0MPC15array5Array2atGfE(_M0L1wS1689, _M0L3valS5183);
          _M0L6w__maxS5182 = _M0L5paramS1670->$3;
          if (_M0L6_2atmpS5181 > _M0L6w__maxS5182) {
            int32_t _M0L3valS5184 = _M0L1sS1683->$0;
            float _M0L6w__maxS5185 = _M0L5paramS1670->$3;
            #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1689, _M0L3valS5184, _M0L6w__maxS5185);
          }
          _M0L3valS5187 = _M0L1sS1683->$0;
          _M0L6_2atmpS5186 = _M0L3valS5187 + 1;
          _M0L1sS1683->$0 = _M0L6_2atmpS5186;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1683);
        }
        break;
      }
      _M0L3valS5191 = _M0L1jS1671->$0;
      _M0L6_2atmpS5190 = _M0L3valS5191 + 1;
      _M0L1jS1671->$0 = _M0L6_2atmpS5190;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1671);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt17istdp__rate__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1660,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1638,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1640,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1656,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1650,
  struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L4varsS1644,
  struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS1642,
  float _M0L6t__nowS1636,
  float _M0L2dtS1645
) {
  int32_t _M0L6n__preS1637;
  int32_t _M0L7n__postS1639;
  float _M0L6tau__yS5114;
  float _M0L11inv__tau__yS1641;
  struct _M0TPB8MutLocalGiE* _M0L1jS1643;
  struct _M0TPB8MutLocalGiE* _M0L1iS1647;
  #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6n__preS1637 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1638);
  #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L7n__postS1639 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1640);
  _M0L6tau__yS5114 = _M0L5paramS1642->$2;
  _M0L11inv__tau__yS1641 = 0x1p+0f / _M0L6tau__yS5114;
  _M0L1jS1643
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1643)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1643->$0 = 0;
  while (1) {
    int32_t _M0L3valS5031 = _M0L1jS1643->$0;
    if (_M0L3valS5031 < _M0L6n__preS1637) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS5032 = _M0L4varsS1644->$0;
      int32_t _M0L3valS5033 = _M0L1jS1643->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5042 = _M0L4varsS1644->$0;
      int32_t _M0L3valS5043 = _M0L1jS1643->$0;
      float _M0L6_2atmpS5035;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5040;
      int32_t _M0L3valS5041;
      float _M0L6_2atmpS5039;
      float _M0L6_2atmpS5038;
      float _M0L6_2atmpS5037;
      float _M0L6_2atmpS5036;
      float _M0L6_2atmpS5034;
      int32_t _M0L3valS5044;
      int32_t _M0L3valS5052;
      int32_t _M0L6_2atmpS5051;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5035
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5042, _M0L3valS5043);
      _M0L4tpreS5040 = _M0L4varsS1644->$0;
      _M0L3valS5041 = _M0L1jS1643->$0;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5039
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5040, _M0L3valS5041);
      _M0L6_2atmpS5038 = -_M0L6_2atmpS5039;
      _M0L6_2atmpS5037 = _M0L2dtS1645 * _M0L6_2atmpS5038;
      _M0L6_2atmpS5036 = _M0L6_2atmpS5037 * _M0L11inv__tau__yS1641;
      _M0L6_2atmpS5034 = _M0L6_2atmpS5035 + _M0L6_2atmpS5036;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS5032, _M0L3valS5033, _M0L6_2atmpS5034);
      _M0L3valS5044 = _M0L1jS1643->$0;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1638, _M0L3valS5044)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS5045 = _M0L4varsS1644->$0;
        int32_t _M0L3valS5046 = _M0L1jS1643->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS5049 = _M0L4varsS1644->$0;
        int32_t _M0L3valS5050 = _M0L1jS1643->$0;
        float _M0L6_2atmpS5048;
        float _M0L6_2atmpS5047;
        #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5048
        = _M0MPC15array5Array2atGfE(_M0L4tpreS5049, _M0L3valS5050);
        _M0L6_2atmpS5047 = _M0L6_2atmpS5048 + 0x1p+0f;
        #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS5045, _M0L3valS5046, _M0L6_2atmpS5047);
      }
      _M0L3valS5052 = _M0L1jS1643->$0;
      _M0L6_2atmpS5051 = _M0L3valS5052 + 1;
      _M0L1jS1643->$0 = _M0L6_2atmpS5051;
      continue;
    }
    break;
  }
  _M0L1iS1647
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1647)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1647->$0 = 0;
  while (1) {
    int32_t _M0L3valS5053 = _M0L1iS1647->$0;
    if (_M0L3valS5053 < _M0L7n__postS1639) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS5054 = _M0L4varsS1644->$1;
      int32_t _M0L3valS5055 = _M0L1iS1647->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5064 = _M0L4varsS1644->$1;
      int32_t _M0L3valS5065 = _M0L1iS1647->$0;
      float _M0L6_2atmpS5057;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5062;
      int32_t _M0L3valS5063;
      float _M0L6_2atmpS5061;
      float _M0L6_2atmpS5060;
      float _M0L6_2atmpS5059;
      float _M0L6_2atmpS5058;
      float _M0L6_2atmpS5056;
      int32_t _M0L3valS5066;
      int32_t _M0L3valS5074;
      int32_t _M0L6_2atmpS5073;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5057
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5064, _M0L3valS5065);
      _M0L5tpostS5062 = _M0L4varsS1644->$1;
      _M0L3valS5063 = _M0L1iS1647->$0;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5061
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5062, _M0L3valS5063);
      _M0L6_2atmpS5060 = -_M0L6_2atmpS5061;
      _M0L6_2atmpS5059 = _M0L2dtS1645 * _M0L6_2atmpS5060;
      _M0L6_2atmpS5058 = _M0L6_2atmpS5059 * _M0L11inv__tau__yS1641;
      _M0L6_2atmpS5056 = _M0L6_2atmpS5057 + _M0L6_2atmpS5058;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS5054, _M0L3valS5055, _M0L6_2atmpS5056);
      _M0L3valS5066 = _M0L1iS1647->$0;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1640, _M0L3valS5066)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS5067 = _M0L4varsS1644->$1;
        int32_t _M0L3valS5068 = _M0L1iS1647->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS5071 = _M0L4varsS1644->$1;
        int32_t _M0L3valS5072 = _M0L1iS1647->$0;
        float _M0L6_2atmpS5070;
        float _M0L6_2atmpS5069;
        #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5070
        = _M0MPC15array5Array2atGfE(_M0L5tpostS5071, _M0L3valS5072);
        _M0L6_2atmpS5069 = _M0L6_2atmpS5070 + 0x1p+0f;
        #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS5067, _M0L3valS5068, _M0L6_2atmpS5069);
      }
      _M0L3valS5074 = _M0L1iS1647->$0;
      _M0L6_2atmpS5073 = _M0L3valS5074 + 1;
      _M0L1iS1647->$0 = _M0L6_2atmpS5073;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1647);
    }
    break;
  }
  _M0L1jS1643->$0 = 0;
  while (1) {
    int32_t _M0L3valS5075 = _M0L1jS1643->$0;
    if (_M0L3valS5075 < _M0L6n__preS1637) {
      int32_t _M0L3valS5113 = _M0L1jS1643->$0;
      int32_t _M0L5startS1649;
      int32_t _M0L3valS5112;
      int32_t _M0L6_2atmpS5111;
      int32_t _M0L3endS1651;
      int32_t _M0L3valS5110;
      int32_t _M0L10pre__firedS1652;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5108;
      int32_t _M0L3valS5109;
      float _M0L7tpre__jS1653;
      struct _M0TPB8MutLocalGiE* _M0L1sS1654;
      int32_t _M0L3valS5107;
      int32_t _M0L6_2atmpS5106;
      #line 179 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L5startS1649
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1650, _M0L3valS5113);
      _M0L3valS5112 = _M0L1jS1643->$0;
      _M0L6_2atmpS5111 = _M0L3valS5112 + 1;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L3endS1651
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1650, _M0L6_2atmpS5111);
      _M0L3valS5110 = _M0L1jS1643->$0;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L10pre__firedS1652
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1638, _M0L3valS5110);
      _M0L4tpreS5108 = _M0L4varsS1644->$0;
      _M0L3valS5109 = _M0L1jS1643->$0;
      #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L7tpre__jS1653
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5108, _M0L3valS5109);
      _M0L1sS1654
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1654)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1654->$0 = _M0L5startS1649;
      while (1) {
        int32_t _M0L3valS5076 = _M0L1sS1654->$0;
        if (_M0L3valS5076 < _M0L3endS1651) {
          int32_t _M0L3valS5105 = _M0L1sS1654->$0;
          int32_t _M0L9post__idxS1655;
          int32_t _M0L11post__firedS1657;
          struct _M0TPB5ArrayGfE* _M0L5tpostS5104;
          float _M0L8tpost__iS1658;
          int32_t _M0L3valS5094;
          float _M0L6_2atmpS5092;
          float _M0L6w__minS5093;
          int32_t _M0L3valS5099;
          float _M0L6_2atmpS5097;
          float _M0L6w__maxS5098;
          int32_t _M0L3valS5103;
          int32_t _M0L6_2atmpS5102;
          #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L9post__idxS1655
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1656, _M0L3valS5105);
          #line 186 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L11post__firedS1657
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1640, _M0L9post__idxS1655);
          _M0L5tpostS5104 = _M0L4varsS1644->$1;
          #line 187 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L8tpost__iS1658
          = _M0MPC15array5Array2atGfE(_M0L5tpostS5104, _M0L9post__idxS1655);
          if (_M0L10pre__firedS1652) {
            float _M0L3etaS5081 = _M0L5paramS1642->$0;
            float _M0L1rS5086 = _M0L5paramS1642->$1;
            float _M0L6_2atmpS5084 = 0x1p+1f * _M0L1rS5086;
            float _M0L6tau__yS5085 = _M0L5paramS1642->$2;
            float _M0L6_2atmpS5083 = _M0L6_2atmpS5084 * _M0L6tau__yS5085;
            float _M0L6_2atmpS5082 = _M0L8tpost__iS1658 - _M0L6_2atmpS5083;
            float _M0L2dwS1659 = _M0L3etaS5081 * _M0L6_2atmpS5082;
            int32_t _M0L3valS5077 = _M0L1sS1654->$0;
            int32_t _M0L3valS5080 = _M0L1sS1654->$0;
            float _M0L6_2atmpS5079;
            float _M0L6_2atmpS5078;
            #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5079
            = _M0MPC15array5Array2atGfE(_M0L1wS1660, _M0L3valS5080);
            _M0L6_2atmpS5078 = _M0L6_2atmpS5079 + _M0L2dwS1659;
            #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1660, _M0L3valS5077, _M0L6_2atmpS5078);
          }
          if (_M0L11post__firedS1657) {
            float _M0L3etaS5091 = _M0L5paramS1642->$0;
            float _M0L2dwS1661 = _M0L3etaS5091 * _M0L7tpre__jS1653;
            int32_t _M0L3valS5087 = _M0L1sS1654->$0;
            int32_t _M0L3valS5090 = _M0L1sS1654->$0;
            float _M0L6_2atmpS5089;
            float _M0L6_2atmpS5088;
            #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5089
            = _M0MPC15array5Array2atGfE(_M0L1wS1660, _M0L3valS5090);
            _M0L6_2atmpS5088 = _M0L6_2atmpS5089 + _M0L2dwS1661;
            #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1660, _M0L3valS5087, _M0L6_2atmpS5088);
          }
          _M0L3valS5094 = _M0L1sS1654->$0;
          #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5092
          = _M0MPC15array5Array2atGfE(_M0L1wS1660, _M0L3valS5094);
          _M0L6w__minS5093 = _M0L5paramS1642->$4;
          if (_M0L6_2atmpS5092 < _M0L6w__minS5093) {
            int32_t _M0L3valS5095 = _M0L1sS1654->$0;
            float _M0L6w__minS5096 = _M0L5paramS1642->$4;
            #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1660, _M0L3valS5095, _M0L6w__minS5096);
          }
          _M0L3valS5099 = _M0L1sS1654->$0;
          #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5097
          = _M0MPC15array5Array2atGfE(_M0L1wS1660, _M0L3valS5099);
          _M0L6w__maxS5098 = _M0L5paramS1642->$3;
          if (_M0L6_2atmpS5097 > _M0L6w__maxS5098) {
            int32_t _M0L3valS5100 = _M0L1sS1654->$0;
            float _M0L6w__maxS5101 = _M0L5paramS1642->$3;
            #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1660, _M0L3valS5100, _M0L6w__maxS5101);
          }
          _M0L3valS5103 = _M0L1sS1654->$0;
          _M0L6_2atmpS5102 = _M0L3valS5103 + 1;
          _M0L1sS1654->$0 = _M0L6_2atmpS5102;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1654);
        }
        break;
      }
      _M0L3valS5107 = _M0L1jS1643->$0;
      _M0L6_2atmpS5106 = _M0L3valS5107 + 1;
      _M0L1jS1643->$0 = _M0L6_2atmpS5106;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1643);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter3new(
  
) {
  float _M0L1cS1634;
  float _M0L2glS1635;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_5721;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS1634 = -0x1p+0f;
  _M0L2glS1635 = -0x1p+0f;
  _block_5721
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_5721)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5721->$0 = _M0L1cS1634;
  _block_5721->$1 = _M0L2glS1635;
  _block_5721->$2 = 0x1.ep+3f;
  _block_5721->$3 = -0x1.9p+5f;
  _block_5721->$4 = -0x1.ep+5f;
  _block_5721->$5 = -0x1.18p+6f;
  _block_5721->$6 = 0x1.eb851eb851eb8p-5f;
  _block_5721->$7 = 0x1p+1f;
  _block_5721->$8 = 0x0p+0f;
  _block_5721->$9 = 0x0p+0f;
  _block_5721->$10 = 0x0p+0f;
  return _block_5721;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS1608,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS1610,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1613
) {
  struct _M0TPB5ArrayGfE* _M0L1vS1607;
  float _M0L2vtS5029;
  float _M0L2vrS5030;
  float _M0L6spreadS1609;
  int32_t _M0L7_2abindS1611;
  int32_t _M0L1kS1612;
  struct _M0TPB5ArrayGfE* _M0L1wS1615;
  struct _M0TPB5ArrayGbE* _M0L4fireS1616;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1617;
  struct _M0TPB5ArrayGfE* _M0L1iS1618;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS1619;
  struct _M0TPB5ArrayGfE* _M0L2geS1620;
  struct _M0TPB5ArrayGfE* _M0L2giS1621;
  struct _M0TPB5ArrayGfE* _M0L2heS1622;
  struct _M0TPB5ArrayGfE* _M0L2hiS1623;
  struct _M0TPB5ArrayGfE* _M0L3gluS1624;
  struct _M0TPB5ArrayGfE* _M0L4gabaS1625;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1626;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1627;
  float _M0L4e__eS1628;
  float _M0L4e__iS1629;
  float _M0L3treS1630;
  float _M0L3tdeS1631;
  float _M0L3triS1632;
  float _M0L3tdiS1633;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS5028;
  struct _M0TP26RiantR8snn__mbt2IF* _block_5723;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS1607 = _M0MPC15array5Array4makeGfE(_M0L1nS1608, 0x0p+0f);
  _M0L2vtS5029 = _M0L5paramS1610->$3;
  _M0L2vrS5030 = _M0L5paramS1610->$4;
  _M0L6spreadS1609 = _M0L2vtS5029 - _M0L2vrS5030;
  _M0L7_2abindS1611 = 0;
  _M0L1kS1612 = _M0L7_2abindS1611;
  while (1) {
    if (_M0L1kS1612 < _M0L1nS1608) {
      float _M0L2vrS5024 = _M0L5paramS1610->$4;
      float _M0L6_2atmpS5026;
      float _M0L6_2atmpS5025;
      float _M0L6_2atmpS5023;
      int32_t _M0L6_2atmpS5027;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS5026 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1613);
      _M0L6_2atmpS5025 = _M0L6_2atmpS5026 * _M0L6spreadS1609;
      _M0L6_2atmpS5023 = _M0L2vrS5024 + _M0L6_2atmpS5025;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1607, _M0L1kS1612, _M0L6_2atmpS5023);
      _M0L6_2atmpS5027 = _M0L1kS1612 + 1;
      _M0L1kS1612 = _M0L6_2atmpS5027;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS1615 = _M0MPC15array5Array4makeGfE(_M0L1nS1608, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS1616 = _M0MPC15array5Array4makeGbE(_M0L1nS1608, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS1617 = _M0MPC15array5Array4makeGiE(_M0L1nS1608, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS1618 = _M0MPC15array5Array4makeGfE(_M0L1nS1608, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS1619 = _M0MPC15array5Array4makeGfE(_M0L1nS1608, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS1620 = _M0MPC15array5Array4makeGfE(_M0L1nS1608, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS1621 = _M0MPC15array5Array4makeGfE(_M0L1nS1608, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS1622 = _M0MPC15array5Array4makeGfE(_M0L1nS1608, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS1623 = _M0MPC15array5Array4makeGfE(_M0L1nS1608, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS1624 = _M0MPC15array5Array4makeGfE(_M0L1nS1608, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS1625 = _M0MPC15array5Array4makeGfE(_M0L1nS1608, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS1626 = _M0MPC15array5Array4makeGfE(_M0L1nS1608, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS1627 = _M0MPC15array5Array4makeGfE(_M0L1nS1608, 0x1p+0f);
  _M0L4e__eS1628 = 0x0p+0f;
  _M0L4e__iS1629 = -0x1.2cp+6f;
  _M0L3treS1630 = 0x1p+0f;
  _M0L3tdeS1631 = 0x1.8p+2f;
  _M0L3triS1632 = 0x1p-1f;
  _M0L3tdiS1633 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS5028 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS1610);
  _block_5723
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_5723)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _block_5723->$0 = _M0L5paramS1610;
  _block_5723->$1 = _M0L6_2atmpS5028;
  _block_5723->$2 = _M0L1nS1608;
  _block_5723->$3 = _M0L1vS1607;
  _block_5723->$4 = _M0L1wS1615;
  _block_5723->$5 = _M0L4fireS1616;
  _block_5723->$6 = _M0L4tabsS1617;
  _block_5723->$7 = _M0L1iS1618;
  _block_5723->$8 = _M0L9syn__currS1619;
  _block_5723->$9 = _M0L2geS1620;
  _block_5723->$10 = _M0L2giS1621;
  _block_5723->$11 = _M0L2heS1622;
  _block_5723->$12 = _M0L2hiS1623;
  _block_5723->$13 = _M0L3gluS1624;
  _block_5723->$14 = _M0L4gabaS1625;
  _block_5723->$15 = _M0L7gsyn__eS1626;
  _block_5723->$16 = _M0L7gsyn__iS1627;
  _block_5723->$17 = _M0L4e__eS1628;
  _block_5723->$18 = _M0L4e__iS1629;
  _block_5723->$19 = _M0L3treS1630;
  _block_5723->$20 = _M0L3tdeS1631;
  _block_5723->$21 = _M0L3triS1632;
  _block_5723->$22 = _M0L3tdiS1633;
  return _block_5723;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_5724;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_5724
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_5724)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5724->$0 = 0x1p+1f;
  return _block_5724;
}

int32_t _M0FP26RiantR8snn__mbt14integrate__any(
  void* _M0L1pS1588,
  float _M0L2dtS1571
) {
  struct _M0TP26RiantR8snn__mbt6HetRec* _M0L1xS1570;
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L1xS1573;
  struct _M0TP26RiantR8snn__mbt7Poisson* _M0L1xS1575;
  struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L1xS1577;
  struct _M0TP26RiantR8snn__mbt2HH* _M0L1xS1579;
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1xS1581;
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1xS1583;
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1xS1585;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1xS1587;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  switch (Moonbit_object_tag(_M0L1pS1588)) {
    case 0: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__* _M0L7_2aIF__S1589 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__*)_M0L1pS1588;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4_2axS1590 =
        _M0L7_2aIF__S1589->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1590);
      _M0L1xS1587 = _M0L4_2axS1590;
      goto join_1586;
      break;
    }
    
    case 1: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop6AdEx__* _M0L9_2aAdEx__S1591 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop6AdEx__*)_M0L1pS1588;
      struct _M0TP26RiantR8snn__mbt4AdEx* _M0L4_2axS1592 =
        _M0L9_2aAdEx__S1591->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1592);
      _M0L1xS1585 = _M0L4_2axS1592;
      goto join_1584;
      break;
    }
    
    case 2: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop12AdExSinExp__* _M0L15_2aAdExSinExp__S1593 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop12AdExSinExp__*)_M0L1pS1588;
      struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L4_2axS1594 =
        _M0L15_2aAdExSinExp__S1593->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1594);
      _M0L1xS1583 = _M0L4_2axS1594;
      goto join_1582;
      break;
    }
    
    case 3: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4IZ__* _M0L7_2aIZ__S1595 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4IZ__*)_M0L1pS1588;
      struct _M0TP26RiantR8snn__mbt2IZ* _M0L4_2axS1596 =
        _M0L7_2aIZ__S1595->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1596);
      _M0L1xS1581 = _M0L4_2axS1596;
      goto join_1580;
      break;
    }
    
    case 4: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__* _M0L7_2aHH__S1597 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__*)_M0L1pS1588;
      struct _M0TP26RiantR8snn__mbt2HH* _M0L4_2axS1598 =
        _M0L7_2aHH__S1597->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1598);
      _M0L1xS1579 = _M0L4_2axS1598;
      goto join_1578;
      break;
    }
    
    case 5: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__* _M0L7_2aML__S1599 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__*)_M0L1pS1588;
      struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L4_2axS1600 =
        _M0L7_2aML__S1599->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1600);
      _M0L1xS1577 = _M0L4_2axS1600;
      goto join_1576;
      break;
    }
    
    case 6: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__* _M0L12_2aPoisson__S1601 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__*)_M0L1pS1588;
      struct _M0TP26RiantR8snn__mbt7Poisson* _M0L4_2axS1602 =
        _M0L12_2aPoisson__S1601->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1602);
      _M0L1xS1575 = _M0L4_2axS1602;
      goto join_1574;
      break;
    }
    
    case 7: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4WC__* _M0L7_2aWC__S1603 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4WC__*)_M0L1pS1588;
      struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L4_2axS1604 =
        _M0L7_2aWC__S1603->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1604);
      _M0L1xS1573 = _M0L4_2axS1604;
      goto join_1572;
      break;
    }
    default: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__* _M0L11_2aHetRec__S1605 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__*)_M0L1pS1588;
      struct _M0TP26RiantR8snn__mbt6HetRec* _M0L4_2axS1606 =
        _M0L11_2aHetRec__S1605->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1606);
      _M0L1xS1570 = _M0L4_2axS1606;
      goto join_1569;
      break;
    }
  }
  goto joinlet_5733;
  join_1586:;
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt14step__synapses(_M0L1xS1587, _M0L2dtS1571);
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt17synaptic__current(_M0L1xS1587);
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt12step__neuron(_M0L1xS1587, _M0L2dtS1571);
  moonbit_decref_cycle_free(_M0L1xS1587);
  joinlet_5733:;
  goto joinlet_5732;
  join_1584:;
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt20adex__step__synapses(_M0L1xS1585, _M0L2dtS1571);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt23adex__synaptic__current(_M0L1xS1585);
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt10step__adex(_M0L1xS1585, _M0L2dtS1571);
  moonbit_decref_cycle_free(_M0L1xS1585);
  joinlet_5732:;
  goto joinlet_5731;
  join_1582:;
  #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(_M0L1xS1583, _M0L2dtS1571);
  #line 44 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(_M0L1xS1583);
  #line 45 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt18step__adex__sinexp(_M0L1xS1583, _M0L2dtS1571);
  moonbit_decref_cycle_free(_M0L1xS1583);
  joinlet_5731:;
  goto joinlet_5730;
  join_1580:;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__iz(_M0L1xS1581, _M0L2dtS1571);
  moonbit_decref_cycle_free(_M0L1xS1581);
  joinlet_5730:;
  goto joinlet_5729;
  join_1578:;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__hh(_M0L1xS1579, _M0L2dtS1571);
  moonbit_decref_cycle_free(_M0L1xS1579);
  joinlet_5729:;
  goto joinlet_5728;
  join_1576:;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__ml(_M0L1xS1577, _M0L2dtS1571);
  moonbit_decref_cycle_free(_M0L1xS1577);
  joinlet_5728:;
  goto joinlet_5727;
  join_1574:;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt13step__poisson(_M0L1xS1575, _M0L2dtS1571);
  moonbit_decref_cycle_free(_M0L1xS1575);
  joinlet_5727:;
  goto joinlet_5726;
  join_1572:;
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__wc(_M0L1xS1573, _M0L2dtS1571);
  moonbit_decref_cycle_free(_M0L1xS1573);
  joinlet_5726:;
  goto joinlet_5725;
  join_1569:;
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt12step__hetrec(_M0L1xS1570, _M0L2dtS1571);
  moonbit_decref_cycle_free(_M0L1xS1570);
  joinlet_5725:;
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__wc(
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L1pS1564,
  float _M0L2dtS1567
) {
  int32_t _M0L1nS1563;
  int32_t _M0L7_2abindS1565;
  int32_t _M0L1kS1566;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _M0L1nS1563 = _M0L1pS1564->$1;
  _M0L7_2abindS1565 = 0;
  _M0L1kS1566 = _M0L7_2abindS1565;
  while (1) {
    if (_M0L1kS1566 < _M0L1nS1563) {
      struct _M0TPB5ArrayGfE* _M0L1xS5003 = _M0L1pS1564->$2;
      struct _M0TPB5ArrayGfE* _M0L1xS5016 = _M0L1pS1564->$2;
      float _M0L6_2atmpS5005;
      struct _M0TPB5ArrayGfE* _M0L1xS5015;
      float _M0L6_2atmpS5014;
      float _M0L6_2atmpS5011;
      struct _M0TPB5ArrayGfE* _M0L1gS5013;
      float _M0L6_2atmpS5012;
      float _M0L6_2atmpS5008;
      struct _M0TPB5ArrayGfE* _M0L1iS5010;
      float _M0L6_2atmpS5009;
      float _M0L6_2atmpS5007;
      float _M0L6_2atmpS5006;
      float _M0L6_2atmpS5004;
      struct _M0TPB5ArrayGfE* _M0L1rS5017;
      struct _M0TPB5ArrayGfE* _M0L1xS5020;
      float _M0L6_2atmpS5019;
      float _M0L6_2atmpS5018;
      struct _M0TPB5ArrayGfE* _M0L1gS5021;
      int32_t _M0L6_2atmpS5022;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5005 = _M0MPC15array5Array2atGfE(_M0L1xS5016, _M0L1kS1566);
      _M0L1xS5015 = _M0L1pS1564->$2;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5014 = _M0MPC15array5Array2atGfE(_M0L1xS5015, _M0L1kS1566);
      _M0L6_2atmpS5011 = -_M0L6_2atmpS5014;
      _M0L1gS5013 = _M0L1pS1564->$4;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5012 = _M0MPC15array5Array2atGfE(_M0L1gS5013, _M0L1kS1566);
      _M0L6_2atmpS5008 = _M0L6_2atmpS5011 + _M0L6_2atmpS5012;
      _M0L1iS5010 = _M0L1pS1564->$5;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5009 = _M0MPC15array5Array2atGfE(_M0L1iS5010, _M0L1kS1566);
      _M0L6_2atmpS5007 = _M0L6_2atmpS5008 + _M0L6_2atmpS5009;
      _M0L6_2atmpS5006 = _M0L2dtS1567 * _M0L6_2atmpS5007;
      _M0L6_2atmpS5004 = _M0L6_2atmpS5005 + _M0L6_2atmpS5006;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS5003, _M0L1kS1566, _M0L6_2atmpS5004);
      _M0L1rS5017 = _M0L1pS1564->$3;
      _M0L1xS5020 = _M0L1pS1564->$2;
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5019 = _M0MPC15array5Array2atGfE(_M0L1xS5020, _M0L1kS1566);
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5018 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS5019);
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1rS5017, _M0L1kS1566, _M0L6_2atmpS5018);
      _M0L1gS5021 = _M0L1pS1564->$4;
      #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS5021, _M0L1kS1566, 0x0p+0f);
      _M0L6_2atmpS5022 = _M0L1kS1566 + 1;
      _M0L1kS1566 = _M0L6_2atmpS5022;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13step__poisson(
  struct _M0TP26RiantR8snn__mbt7Poisson* _M0L1pS1556,
  float _M0L2dtS1558
) {
  int32_t _M0L1nS1555;
  struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter* _M0L5paramS5002;
  float _M0L4rateS5001;
  float _M0L8rate__dtS1557;
  int32_t _M0L7_2abindS1559;
  int32_t _M0L1iS1560;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
  _M0L1nS1555 = _M0L1pS1556->$1;
  _M0L5paramS5002 = _M0L1pS1556->$0;
  _M0L4rateS5001 = _M0L5paramS5002->$0;
  _M0L8rate__dtS1557 = _M0L4rateS5001 * _M0L2dtS1558;
  _M0L7_2abindS1559 = 0;
  _M0L1iS1560 = _M0L7_2abindS1559;
  while (1) {
    if (_M0L1iS1560 < _M0L1nS1555) {
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS4999 = _M0L1pS1556->$4;
      float _M0L1uS1561;
      struct _M0TPB5ArrayGfE* _M0L9randcacheS4996;
      struct _M0TPB5ArrayGbE* _M0L4fireS4997;
      int32_t _M0L6_2atmpS4998;
      int32_t _M0L6_2atmpS5000;
      #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0L1uS1561 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS4999);
      _M0L9randcacheS4996 = _M0L1pS1556->$3;
      #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0MPC15array5Array3setGfE(_M0L9randcacheS4996, _M0L1iS1560, _M0L1uS1561);
      _M0L4fireS4997 = _M0L1pS1556->$2;
      _M0L6_2atmpS4998 = _M0L1uS1561 < _M0L8rate__dtS1557;
      #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4997, _M0L1iS1560, _M0L6_2atmpS4998);
      _M0L6_2atmpS5000 = _M0L1iS1560 + 1;
      _M0L1iS1560 = _M0L6_2atmpS5000;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__ml(
  struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L1pS1515,
  float _M0L2dtS1544
) {
  int32_t _M0L1nS1514;
  struct _M0TP26RiantR8snn__mbt20MorrisLecarParameter* _M0L3p__S1516;
  float _M0L2cmS1517;
  float _M0L2elS1518;
  float _M0L2ekS1519;
  float _M0L3ecaS1520;
  float _M0L2glS1521;
  float _M0L2gkS1522;
  float _M0L3gcaS1523;
  float _M0L6tau__eS1524;
  float _M0L6tau__iS1525;
  float _M0L2v1S1526;
  float _M0L2v2S1527;
  float _M0L2v3S1528;
  float _M0L2v4S1529;
  float _M0L3phiS1530;
  float _M0L4e__eS1531;
  float _M0L4e__iS1532;
  int32_t _M0L7_2abindS1533;
  int32_t _M0L1iS1534;
  int32_t _M0L7_2abindS1546;
  int32_t _M0L1iS1547;
  int32_t _M0L7_2abindS1549;
  int32_t _M0L1iS1550;
  int32_t _M0L7_2abindS1552;
  int32_t _M0L1iS1553;
  #line 86 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
  _M0L1nS1514 = _M0L1pS1515->$1;
  _M0L3p__S1516 = _M0L1pS1515->$0;
  _M0L2cmS1517 = _M0L3p__S1516->$0;
  _M0L2elS1518 = _M0L3p__S1516->$1;
  _M0L2ekS1519 = _M0L3p__S1516->$2;
  _M0L3ecaS1520 = _M0L3p__S1516->$3;
  _M0L2glS1521 = _M0L3p__S1516->$4;
  _M0L2gkS1522 = _M0L3p__S1516->$5;
  _M0L3gcaS1523 = _M0L3p__S1516->$6;
  _M0L6tau__eS1524 = _M0L3p__S1516->$7;
  _M0L6tau__iS1525 = _M0L3p__S1516->$8;
  _M0L2v1S1526 = _M0L3p__S1516->$9;
  _M0L2v2S1527 = _M0L3p__S1516->$10;
  _M0L2v3S1528 = _M0L3p__S1516->$11;
  _M0L2v4S1529 = _M0L3p__S1516->$12;
  _M0L3phiS1530 = _M0L3p__S1516->$13;
  _M0L4e__eS1531 = _M0L3p__S1516->$14;
  _M0L4e__iS1532 = _M0L3p__S1516->$15;
  _M0L7_2abindS1533 = 0;
  _M0L1iS1534 = _M0L7_2abindS1533;
  while (1) {
    if (_M0L1iS1534 < _M0L1nS1514) {
      struct _M0TPB5ArrayGfE* _M0L1vS4950 = _M0L1pS1515->$2;
      float _M0L1vS1535;
      struct _M0TPB5ArrayGfE* _M0L1wS4949;
      float _M0L1wS1536;
      float _M0L6_2atmpS4948;
      float _M0L6_2atmpS4947;
      float _M0L6_2atmpS4946;
      float _M0L6_2atmpS4945;
      float _M0L5m__ssS1537;
      struct _M0TPB5ArrayGfE* _M0L1iS4944;
      float _M0L6_2atmpS4941;
      float _M0L6_2atmpS4943;
      float _M0L6_2atmpS4942;
      float _M0L6_2atmpS4937;
      float _M0L6_2atmpS4940;
      float _M0L6_2atmpS4939;
      float _M0L6_2atmpS4938;
      float _M0L6_2atmpS4933;
      float _M0L6_2atmpS4936;
      float _M0L6_2atmpS4935;
      float _M0L6_2atmpS4934;
      float _M0L2dvS1538;
      float _M0L6_2atmpS4932;
      float _M0L6_2atmpS4931;
      float _M0L6_2atmpS4930;
      float _M0L6_2atmpS4929;
      float _M0L5n__ssS1539;
      float _M0L6_2atmpS4927;
      float _M0L6_2atmpS4928;
      float _M0L9cosh__argS1540;
      float _M0L6_2atmpS4924;
      float _M0L6_2atmpS4926;
      float _M0L6_2atmpS4925;
      float _M0L6_2atmpS4923;
      float _M0L9cosh__valS1541;
      float _M0L6_2atmpS4921;
      float _M0L3tauS1542;
      float _M0L6_2atmpS4920;
      float _M0L2dwS1543;
      struct _M0TPB5ArrayGfE* _M0L1vS4913;
      float _M0L6_2atmpS4916;
      float _M0L6_2atmpS4915;
      float _M0L6_2atmpS4914;
      struct _M0TPB5ArrayGfE* _M0L1wS4917;
      float _M0L6_2atmpS4919;
      float _M0L6_2atmpS4918;
      int32_t _M0L6_2atmpS4951;
      #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L1vS1535 = _M0MPC15array5Array2atGfE(_M0L1vS4950, _M0L1iS1534);
      _M0L1wS4949 = _M0L1pS1515->$3;
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L1wS1536 = _M0MPC15array5Array2atGfE(_M0L1wS4949, _M0L1iS1534);
      _M0L6_2atmpS4948 = _M0L1vS1535 - _M0L2v1S1526;
      _M0L6_2atmpS4947 = _M0L6_2atmpS4948 / _M0L2v2S1527;
      #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4946 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS4947);
      _M0L6_2atmpS4945 = 0x1p+0f + _M0L6_2atmpS4946;
      _M0L5m__ssS1537 = 0x1p-1f * _M0L6_2atmpS4945;
      _M0L1iS4944 = _M0L1pS1515->$5;
      #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4941 = _M0MPC15array5Array2atGfE(_M0L1iS4944, _M0L1iS1534);
      _M0L6_2atmpS4943 = _M0L2elS1518 - _M0L1vS1535;
      _M0L6_2atmpS4942 = _M0L2glS1521 * _M0L6_2atmpS4943;
      _M0L6_2atmpS4937 = _M0L6_2atmpS4941 + _M0L6_2atmpS4942;
      _M0L6_2atmpS4940 = _M0L3ecaS1520 - _M0L1vS1535;
      _M0L6_2atmpS4939 = _M0L3gcaS1523 * _M0L6_2atmpS4940;
      _M0L6_2atmpS4938 = _M0L6_2atmpS4939 * _M0L5m__ssS1537;
      _M0L6_2atmpS4933 = _M0L6_2atmpS4937 + _M0L6_2atmpS4938;
      _M0L6_2atmpS4936 = _M0L2ekS1519 - _M0L1vS1535;
      _M0L6_2atmpS4935 = _M0L2gkS1522 * _M0L6_2atmpS4936;
      _M0L6_2atmpS4934 = _M0L6_2atmpS4935 * _M0L1wS1536;
      _M0L2dvS1538 = _M0L6_2atmpS4933 + _M0L6_2atmpS4934;
      _M0L6_2atmpS4932 = _M0L1vS1535 - _M0L2v3S1528;
      _M0L6_2atmpS4931 = _M0L6_2atmpS4932 / _M0L2v4S1529;
      #line 112 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4930 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS4931);
      _M0L6_2atmpS4929 = 0x1p+0f + _M0L6_2atmpS4930;
      _M0L5n__ssS1539 = 0x1p-1f * _M0L6_2atmpS4929;
      _M0L6_2atmpS4927 = _M0L1vS1535 - _M0L2v3S1528;
      _M0L6_2atmpS4928 = 0x1p+1f * _M0L2v4S1529;
      _M0L9cosh__argS1540 = _M0L6_2atmpS4927 / _M0L6_2atmpS4928;
      #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4924 = _M0FP26RiantR8snn__mbt4expf(_M0L9cosh__argS1540);
      _M0L6_2atmpS4926 = -_M0L9cosh__argS1540;
      #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4925 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4926);
      _M0L6_2atmpS4923 = _M0L6_2atmpS4924 + _M0L6_2atmpS4925;
      _M0L9cosh__valS1541 = 0x1p-1f * _M0L6_2atmpS4923;
      _M0L6_2atmpS4921 = _M0L3phiS1530 * _M0L9cosh__valS1541;
      if (_M0L6_2atmpS4921 != 0x0p+0f) {
        float _M0L6_2atmpS4922 = _M0L3phiS1530 * _M0L9cosh__valS1541;
        _M0L3tauS1542 = 0x1p+0f / _M0L6_2atmpS4922;
      } else {
        _M0L3tauS1542 = 0x0p+0f;
      }
      _M0L6_2atmpS4920 = _M0L5n__ssS1539 - _M0L1wS1536;
      _M0L2dwS1543 = _M0L6_2atmpS4920 / _M0L3tauS1542;
      _M0L1vS4913 = _M0L1pS1515->$2;
      _M0L6_2atmpS4916 = _M0L2dtS1544 / _M0L2cmS1517;
      _M0L6_2atmpS4915 = _M0L6_2atmpS4916 * _M0L2dvS1538;
      _M0L6_2atmpS4914 = _M0L1vS1535 + _M0L6_2atmpS4915;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4913, _M0L1iS1534, _M0L6_2atmpS4914);
      _M0L1wS4917 = _M0L1pS1515->$3;
      _M0L6_2atmpS4919 = _M0L2dtS1544 * _M0L2dwS1543;
      _M0L6_2atmpS4918 = _M0L1wS1536 + _M0L6_2atmpS4919;
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4917, _M0L1iS1534, _M0L6_2atmpS4918);
      _M0L6_2atmpS4951 = _M0L1iS1534 + 1;
      _M0L1iS1534 = _M0L6_2atmpS4951;
      continue;
    }
    break;
  }
  _M0L7_2abindS1546 = 0;
  _M0L1iS1547 = _M0L7_2abindS1546;
  while (1) {
    if (_M0L1iS1547 < _M0L1nS1514) {
      struct _M0TPB5ArrayGfE* _M0L1vS4952 = _M0L1pS1515->$2;
      struct _M0TPB5ArrayGfE* _M0L1vS4970 = _M0L1pS1515->$2;
      float _M0L6_2atmpS4954;
      float _M0L6_2atmpS4956;
      struct _M0TPB5ArrayGfE* _M0L2geS4969;
      float _M0L6_2atmpS4965;
      struct _M0TPB5ArrayGfE* _M0L1vS4968;
      float _M0L6_2atmpS4967;
      float _M0L6_2atmpS4966;
      float _M0L6_2atmpS4958;
      struct _M0TPB5ArrayGfE* _M0L2giS4964;
      float _M0L6_2atmpS4960;
      struct _M0TPB5ArrayGfE* _M0L1vS4963;
      float _M0L6_2atmpS4962;
      float _M0L6_2atmpS4961;
      float _M0L6_2atmpS4959;
      float _M0L6_2atmpS4957;
      float _M0L6_2atmpS4955;
      float _M0L6_2atmpS4953;
      int32_t _M0L6_2atmpS4971;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4954 = _M0MPC15array5Array2atGfE(_M0L1vS4970, _M0L1iS1547);
      _M0L6_2atmpS4956 = _M0L2dtS1544 / _M0L2cmS1517;
      _M0L2geS4969 = _M0L1pS1515->$6;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4965 = _M0MPC15array5Array2atGfE(_M0L2geS4969, _M0L1iS1547);
      _M0L1vS4968 = _M0L1pS1515->$2;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4967 = _M0MPC15array5Array2atGfE(_M0L1vS4968, _M0L1iS1547);
      _M0L6_2atmpS4966 = _M0L4e__eS1531 - _M0L6_2atmpS4967;
      _M0L6_2atmpS4958 = _M0L6_2atmpS4965 * _M0L6_2atmpS4966;
      _M0L2giS4964 = _M0L1pS1515->$7;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4960 = _M0MPC15array5Array2atGfE(_M0L2giS4964, _M0L1iS1547);
      _M0L1vS4963 = _M0L1pS1515->$2;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4962 = _M0MPC15array5Array2atGfE(_M0L1vS4963, _M0L1iS1547);
      _M0L6_2atmpS4961 = _M0L4e__iS1532 - _M0L6_2atmpS4962;
      _M0L6_2atmpS4959 = _M0L6_2atmpS4960 * _M0L6_2atmpS4961;
      _M0L6_2atmpS4957 = _M0L6_2atmpS4958 + _M0L6_2atmpS4959;
      _M0L6_2atmpS4955 = _M0L6_2atmpS4956 * _M0L6_2atmpS4957;
      _M0L6_2atmpS4953 = _M0L6_2atmpS4954 + _M0L6_2atmpS4955;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4952, _M0L1iS1547, _M0L6_2atmpS4953);
      _M0L6_2atmpS4971 = _M0L1iS1547 + 1;
      _M0L1iS1547 = _M0L6_2atmpS4971;
      continue;
    }
    break;
  }
  _M0L7_2abindS1549 = 0;
  _M0L1iS1550 = _M0L7_2abindS1549;
  while (1) {
    if (_M0L1iS1550 < _M0L1nS1514) {
      struct _M0TPB5ArrayGfE* _M0L2geS4972 = _M0L1pS1515->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS4980 = _M0L1pS1515->$6;
      float _M0L6_2atmpS4974;
      struct _M0TPB5ArrayGfE* _M0L2geS4979;
      float _M0L6_2atmpS4978;
      float _M0L6_2atmpS4977;
      float _M0L6_2atmpS4976;
      float _M0L6_2atmpS4975;
      float _M0L6_2atmpS4973;
      struct _M0TPB5ArrayGfE* _M0L2giS4981;
      struct _M0TPB5ArrayGfE* _M0L2giS4989;
      float _M0L6_2atmpS4983;
      struct _M0TPB5ArrayGfE* _M0L2giS4988;
      float _M0L6_2atmpS4987;
      float _M0L6_2atmpS4986;
      float _M0L6_2atmpS4985;
      float _M0L6_2atmpS4984;
      float _M0L6_2atmpS4982;
      int32_t _M0L6_2atmpS4990;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4974 = _M0MPC15array5Array2atGfE(_M0L2geS4980, _M0L1iS1550);
      _M0L2geS4979 = _M0L1pS1515->$6;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4978 = _M0MPC15array5Array2atGfE(_M0L2geS4979, _M0L1iS1550);
      _M0L6_2atmpS4977 = -_M0L6_2atmpS4978;
      _M0L6_2atmpS4976 = _M0L6_2atmpS4977 / _M0L6tau__eS1524;
      _M0L6_2atmpS4975 = _M0L2dtS1544 * _M0L6_2atmpS4976;
      _M0L6_2atmpS4973 = _M0L6_2atmpS4974 + _M0L6_2atmpS4975;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4972, _M0L1iS1550, _M0L6_2atmpS4973);
      _M0L2giS4981 = _M0L1pS1515->$7;
      _M0L2giS4989 = _M0L1pS1515->$7;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4983 = _M0MPC15array5Array2atGfE(_M0L2giS4989, _M0L1iS1550);
      _M0L2giS4988 = _M0L1pS1515->$7;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4987 = _M0MPC15array5Array2atGfE(_M0L2giS4988, _M0L1iS1550);
      _M0L6_2atmpS4986 = -_M0L6_2atmpS4987;
      _M0L6_2atmpS4985 = _M0L6_2atmpS4986 / _M0L6tau__iS1525;
      _M0L6_2atmpS4984 = _M0L2dtS1544 * _M0L6_2atmpS4985;
      _M0L6_2atmpS4982 = _M0L6_2atmpS4983 + _M0L6_2atmpS4984;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4981, _M0L1iS1550, _M0L6_2atmpS4982);
      _M0L6_2atmpS4990 = _M0L1iS1550 + 1;
      _M0L1iS1550 = _M0L6_2atmpS4990;
      continue;
    }
    break;
  }
  _M0L7_2abindS1552 = 0;
  _M0L1iS1553 = _M0L7_2abindS1552;
  while (1) {
    if (_M0L1iS1553 < _M0L1nS1514) {
      struct _M0TPB5ArrayGbE* _M0L4fireS4991 = _M0L1pS1515->$4;
      struct _M0TPB5ArrayGfE* _M0L1vS4994 = _M0L1pS1515->$2;
      float _M0L6_2atmpS4993;
      int32_t _M0L6_2atmpS4992;
      int32_t _M0L6_2atmpS4995;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS4993 = _M0MPC15array5Array2atGfE(_M0L1vS4994, _M0L1iS1553);
      _M0L6_2atmpS4992 = _M0L6_2atmpS4993 > 0x1.4p+4f;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4991, _M0L1iS1553, _M0L6_2atmpS4992);
      _M0L6_2atmpS4995 = _M0L1iS1553 + 1;
      _M0L1iS1553 = _M0L6_2atmpS4995;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__iz(
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1pS1483,
  float _M0L2dtS1495
) {
  int32_t _M0L1nS1482;
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L3p__S1484;
  float _M0L1aS1485;
  float _M0L1bS1486;
  float _M0L1cS1487;
  float _M0L1dS1488;
  float _M0L6tau__eS1489;
  float _M0L6tau__iS1490;
  float _M0L4e__eS1491;
  float _M0L4e__iS1492;
  int32_t _M0L7_2abindS1493;
  int32_t _M0L1iS1494;
  int32_t _M0L7_2abindS1497;
  int32_t _M0L1iS1498;
  int32_t _M0L7_2abindS1504;
  int32_t _M0L1iS1505;
  int32_t _M0L7_2abindS1508;
  int32_t _M0L1iS1509;
  int32_t _M0L7_2abindS1511;
  int32_t _M0L1iS1512;
  #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1nS1482 = _M0L1pS1483->$1;
  _M0L3p__S1484 = _M0L1pS1483->$0;
  _M0L1aS1485 = _M0L3p__S1484->$0;
  _M0L1bS1486 = _M0L3p__S1484->$1;
  _M0L1cS1487 = _M0L3p__S1484->$2;
  _M0L1dS1488 = _M0L3p__S1484->$3;
  _M0L6tau__eS1489 = _M0L3p__S1484->$4;
  _M0L6tau__iS1490 = _M0L3p__S1484->$5;
  _M0L4e__eS1491 = _M0L3p__S1484->$6;
  _M0L4e__iS1492 = _M0L3p__S1484->$7;
  _M0L7_2abindS1493 = 0;
  _M0L1iS1494 = _M0L7_2abindS1493;
  while (1) {
    if (_M0L1iS1494 < _M0L1nS1482) {
      struct _M0TPB5ArrayGfE* _M0L2geS4821 = _M0L1pS1483->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS4829 = _M0L1pS1483->$6;
      float _M0L6_2atmpS4823;
      struct _M0TPB5ArrayGfE* _M0L2geS4828;
      float _M0L6_2atmpS4827;
      float _M0L6_2atmpS4826;
      float _M0L6_2atmpS4825;
      float _M0L6_2atmpS4824;
      float _M0L6_2atmpS4822;
      struct _M0TPB5ArrayGfE* _M0L2giS4830;
      struct _M0TPB5ArrayGfE* _M0L2giS4838;
      float _M0L6_2atmpS4832;
      struct _M0TPB5ArrayGfE* _M0L2giS4837;
      float _M0L6_2atmpS4836;
      float _M0L6_2atmpS4835;
      float _M0L6_2atmpS4834;
      float _M0L6_2atmpS4833;
      float _M0L6_2atmpS4831;
      int32_t _M0L6_2atmpS4839;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4823 = _M0MPC15array5Array2atGfE(_M0L2geS4829, _M0L1iS1494);
      _M0L2geS4828 = _M0L1pS1483->$6;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4827 = _M0MPC15array5Array2atGfE(_M0L2geS4828, _M0L1iS1494);
      _M0L6_2atmpS4826 = -_M0L6_2atmpS4827;
      _M0L6_2atmpS4825 = _M0L2dtS1495 * _M0L6_2atmpS4826;
      _M0L6_2atmpS4824 = _M0L6_2atmpS4825 / _M0L6tau__eS1489;
      _M0L6_2atmpS4822 = _M0L6_2atmpS4823 + _M0L6_2atmpS4824;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4821, _M0L1iS1494, _M0L6_2atmpS4822);
      _M0L2giS4830 = _M0L1pS1483->$7;
      _M0L2giS4838 = _M0L1pS1483->$7;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4832 = _M0MPC15array5Array2atGfE(_M0L2giS4838, _M0L1iS1494);
      _M0L2giS4837 = _M0L1pS1483->$7;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4836 = _M0MPC15array5Array2atGfE(_M0L2giS4837, _M0L1iS1494);
      _M0L6_2atmpS4835 = -_M0L6_2atmpS4836;
      _M0L6_2atmpS4834 = _M0L2dtS1495 * _M0L6_2atmpS4835;
      _M0L6_2atmpS4833 = _M0L6_2atmpS4834 / _M0L6tau__iS1490;
      _M0L6_2atmpS4831 = _M0L6_2atmpS4832 + _M0L6_2atmpS4833;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4830, _M0L1iS1494, _M0L6_2atmpS4831);
      _M0L6_2atmpS4839 = _M0L1iS1494 + 1;
      _M0L1iS1494 = _M0L6_2atmpS4839;
      continue;
    }
    break;
  }
  _M0L7_2abindS1497 = 0;
  _M0L1iS1498 = _M0L7_2abindS1497;
  while (1) {
    if (_M0L1iS1498 < _M0L1nS1482) {
      struct _M0TPB5ArrayGfE* _M0L1vS4865 = _M0L1pS1483->$2;
      float _M0L1vS1499;
      struct _M0TPB5ArrayGfE* _M0L1uS4864;
      float _M0L1uS1500;
      struct _M0TPB5ArrayGfE* _M0L1iS4863;
      float _M0L2iiS1501;
      struct _M0TPB5ArrayGfE* _M0L1vS4840;
      float _M0L6_2atmpS4843;
      float _M0L6_2atmpS4850;
      float _M0L6_2atmpS4848;
      float _M0L6_2atmpS4849;
      float _M0L6_2atmpS4847;
      float _M0L6_2atmpS4846;
      float _M0L6_2atmpS4845;
      float _M0L6_2atmpS4844;
      float _M0L6_2atmpS4842;
      float _M0L6_2atmpS4841;
      struct _M0TPB5ArrayGfE* _M0L1vS4862;
      float _M0L2v2S1502;
      struct _M0TPB5ArrayGfE* _M0L1vS4851;
      float _M0L6_2atmpS4854;
      float _M0L6_2atmpS4861;
      float _M0L6_2atmpS4859;
      float _M0L6_2atmpS4860;
      float _M0L6_2atmpS4858;
      float _M0L6_2atmpS4857;
      float _M0L6_2atmpS4856;
      float _M0L6_2atmpS4855;
      float _M0L6_2atmpS4853;
      float _M0L6_2atmpS4852;
      int32_t _M0L6_2atmpS4866;
      #line 359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS1499 = _M0MPC15array5Array2atGfE(_M0L1vS4865, _M0L1iS1498);
      _M0L1uS4864 = _M0L1pS1483->$3;
      #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1uS1500 = _M0MPC15array5Array2atGfE(_M0L1uS4864, _M0L1iS1498);
      _M0L1iS4863 = _M0L1pS1483->$5;
      #line 361 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2iiS1501 = _M0MPC15array5Array2atGfE(_M0L1iS4863, _M0L1iS1498);
      _M0L1vS4840 = _M0L1pS1483->$2;
      _M0L6_2atmpS4843 = 0x1p-1f * _M0L2dtS1495;
      _M0L6_2atmpS4850 = 0x1.47ae147ae147bp-5f * _M0L1vS1499;
      _M0L6_2atmpS4848 = _M0L6_2atmpS4850 * _M0L1vS1499;
      _M0L6_2atmpS4849 = 0x1.4p+2f * _M0L1vS1499;
      _M0L6_2atmpS4847 = _M0L6_2atmpS4848 + _M0L6_2atmpS4849;
      _M0L6_2atmpS4846 = _M0L6_2atmpS4847 + 0x1.18p+7f;
      _M0L6_2atmpS4845 = _M0L6_2atmpS4846 - _M0L1uS1500;
      _M0L6_2atmpS4844 = _M0L6_2atmpS4845 + _M0L2iiS1501;
      _M0L6_2atmpS4842 = _M0L6_2atmpS4843 * _M0L6_2atmpS4844;
      _M0L6_2atmpS4841 = _M0L1vS1499 + _M0L6_2atmpS4842;
      #line 362 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4840, _M0L1iS1498, _M0L6_2atmpS4841);
      _M0L1vS4862 = _M0L1pS1483->$2;
      #line 363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2v2S1502 = _M0MPC15array5Array2atGfE(_M0L1vS4862, _M0L1iS1498);
      _M0L1vS4851 = _M0L1pS1483->$2;
      _M0L6_2atmpS4854 = 0x1p-1f * _M0L2dtS1495;
      _M0L6_2atmpS4861 = 0x1.47ae147ae147bp-5f * _M0L2v2S1502;
      _M0L6_2atmpS4859 = _M0L6_2atmpS4861 * _M0L2v2S1502;
      _M0L6_2atmpS4860 = 0x1.4p+2f * _M0L2v2S1502;
      _M0L6_2atmpS4858 = _M0L6_2atmpS4859 + _M0L6_2atmpS4860;
      _M0L6_2atmpS4857 = _M0L6_2atmpS4858 + 0x1.18p+7f;
      _M0L6_2atmpS4856 = _M0L6_2atmpS4857 - _M0L1uS1500;
      _M0L6_2atmpS4855 = _M0L6_2atmpS4856 + _M0L2iiS1501;
      _M0L6_2atmpS4853 = _M0L6_2atmpS4854 * _M0L6_2atmpS4855;
      _M0L6_2atmpS4852 = _M0L2v2S1502 + _M0L6_2atmpS4853;
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4851, _M0L1iS1498, _M0L6_2atmpS4852);
      _M0L6_2atmpS4866 = _M0L1iS1498 + 1;
      _M0L1iS1498 = _M0L6_2atmpS4866;
      continue;
    }
    break;
  }
  _M0L7_2abindS1504 = 0;
  _M0L1iS1505 = _M0L7_2abindS1504;
  while (1) {
    if (_M0L1iS1505 < _M0L1nS1482) {
      struct _M0TPB5ArrayGfE* _M0L1vS4877 = _M0L1pS1483->$2;
      float _M0L1vS1506;
      struct _M0TPB5ArrayGfE* _M0L1uS4867;
      struct _M0TPB5ArrayGfE* _M0L1uS4876;
      float _M0L6_2atmpS4869;
      float _M0L6_2atmpS4871;
      float _M0L6_2atmpS4873;
      struct _M0TPB5ArrayGfE* _M0L1uS4875;
      float _M0L6_2atmpS4874;
      float _M0L6_2atmpS4872;
      float _M0L6_2atmpS4870;
      float _M0L6_2atmpS4868;
      int32_t _M0L6_2atmpS4878;
      #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS1506 = _M0MPC15array5Array2atGfE(_M0L1vS4877, _M0L1iS1505);
      _M0L1uS4867 = _M0L1pS1483->$3;
      _M0L1uS4876 = _M0L1pS1483->$3;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4869 = _M0MPC15array5Array2atGfE(_M0L1uS4876, _M0L1iS1505);
      _M0L6_2atmpS4871 = _M0L2dtS1495 * _M0L1aS1485;
      _M0L6_2atmpS4873 = _M0L1bS1486 * _M0L1vS1506;
      _M0L1uS4875 = _M0L1pS1483->$3;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4874 = _M0MPC15array5Array2atGfE(_M0L1uS4875, _M0L1iS1505);
      _M0L6_2atmpS4872 = _M0L6_2atmpS4873 - _M0L6_2atmpS4874;
      _M0L6_2atmpS4870 = _M0L6_2atmpS4871 * _M0L6_2atmpS4872;
      _M0L6_2atmpS4868 = _M0L6_2atmpS4869 + _M0L6_2atmpS4870;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS4867, _M0L1iS1505, _M0L6_2atmpS4868);
      _M0L6_2atmpS4878 = _M0L1iS1505 + 1;
      _M0L1iS1505 = _M0L6_2atmpS4878;
      continue;
    }
    break;
  }
  _M0L7_2abindS1508 = 0;
  _M0L1iS1509 = _M0L7_2abindS1508;
  while (1) {
    if (_M0L1iS1509 < _M0L1nS1482) {
      struct _M0TPB5ArrayGfE* _M0L1vS4879 = _M0L1pS1483->$2;
      struct _M0TPB5ArrayGfE* _M0L1vS4896 = _M0L1pS1483->$2;
      float _M0L6_2atmpS4881;
      struct _M0TPB5ArrayGfE* _M0L2geS4895;
      float _M0L6_2atmpS4891;
      struct _M0TPB5ArrayGfE* _M0L1vS4894;
      float _M0L6_2atmpS4893;
      float _M0L6_2atmpS4892;
      float _M0L6_2atmpS4884;
      struct _M0TPB5ArrayGfE* _M0L2giS4890;
      float _M0L6_2atmpS4886;
      struct _M0TPB5ArrayGfE* _M0L1vS4889;
      float _M0L6_2atmpS4888;
      float _M0L6_2atmpS4887;
      float _M0L6_2atmpS4885;
      float _M0L6_2atmpS4883;
      float _M0L6_2atmpS4882;
      float _M0L6_2atmpS4880;
      int32_t _M0L6_2atmpS4897;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4881 = _M0MPC15array5Array2atGfE(_M0L1vS4896, _M0L1iS1509);
      _M0L2geS4895 = _M0L1pS1483->$6;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4891 = _M0MPC15array5Array2atGfE(_M0L2geS4895, _M0L1iS1509);
      _M0L1vS4894 = _M0L1pS1483->$2;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4893 = _M0MPC15array5Array2atGfE(_M0L1vS4894, _M0L1iS1509);
      _M0L6_2atmpS4892 = _M0L4e__eS1491 - _M0L6_2atmpS4893;
      _M0L6_2atmpS4884 = _M0L6_2atmpS4891 * _M0L6_2atmpS4892;
      _M0L2giS4890 = _M0L1pS1483->$7;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4886 = _M0MPC15array5Array2atGfE(_M0L2giS4890, _M0L1iS1509);
      _M0L1vS4889 = _M0L1pS1483->$2;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4888 = _M0MPC15array5Array2atGfE(_M0L1vS4889, _M0L1iS1509);
      _M0L6_2atmpS4887 = _M0L4e__iS1492 - _M0L6_2atmpS4888;
      _M0L6_2atmpS4885 = _M0L6_2atmpS4886 * _M0L6_2atmpS4887;
      _M0L6_2atmpS4883 = _M0L6_2atmpS4884 + _M0L6_2atmpS4885;
      _M0L6_2atmpS4882 = _M0L2dtS1495 * _M0L6_2atmpS4883;
      _M0L6_2atmpS4880 = _M0L6_2atmpS4881 + _M0L6_2atmpS4882;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4879, _M0L1iS1509, _M0L6_2atmpS4880);
      _M0L6_2atmpS4897 = _M0L1iS1509 + 1;
      _M0L1iS1509 = _M0L6_2atmpS4897;
      continue;
    }
    break;
  }
  _M0L7_2abindS1511 = 0;
  _M0L1iS1512 = _M0L7_2abindS1511;
  while (1) {
    if (_M0L1iS1512 < _M0L1nS1482) {
      struct _M0TPB5ArrayGbE* _M0L4fireS4898 = _M0L1pS1483->$4;
      struct _M0TPB5ArrayGfE* _M0L1vS4901 = _M0L1pS1483->$2;
      float _M0L6_2atmpS4900;
      int32_t _M0L6_2atmpS4899;
      struct _M0TPB5ArrayGfE* _M0L1vS4902;
      struct _M0TPB5ArrayGbE* _M0L4fireS4904;
      float _M0L6_2atmpS4903;
      struct _M0TPB5ArrayGfE* _M0L1uS4906;
      struct _M0TPB5ArrayGfE* _M0L1uS4911;
      float _M0L6_2atmpS4908;
      struct _M0TPB5ArrayGbE* _M0L4fireS4910;
      float _M0L6_2atmpS4909;
      float _M0L6_2atmpS4907;
      int32_t _M0L6_2atmpS4912;
      #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4900 = _M0MPC15array5Array2atGfE(_M0L1vS4901, _M0L1iS1512);
      _M0L6_2atmpS4899 = _M0L6_2atmpS4900 > 0x1.ep+4f;
      #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4898, _M0L1iS1512, _M0L6_2atmpS4899);
      _M0L1vS4902 = _M0L1pS1483->$2;
      _M0L4fireS4904 = _M0L1pS1483->$4;
      #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4904, _M0L1iS1512)) {
        _M0L6_2atmpS4903 = _M0L1cS1487;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4905 = _M0L1pS1483->$2;
        #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS4903
        = _M0MPC15array5Array2atGfE(_M0L1vS4905, _M0L1iS1512);
      }
      #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4902, _M0L1iS1512, _M0L6_2atmpS4903);
      _M0L1uS4906 = _M0L1pS1483->$3;
      _M0L1uS4911 = _M0L1pS1483->$3;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4908 = _M0MPC15array5Array2atGfE(_M0L1uS4911, _M0L1iS1512);
      _M0L4fireS4910 = _M0L1pS1483->$4;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4910, _M0L1iS1512)) {
        _M0L6_2atmpS4909 = _M0L1dS1488;
      } else {
        _M0L6_2atmpS4909 = 0x0p+0f;
      }
      _M0L6_2atmpS4907 = _M0L6_2atmpS4908 + _M0L6_2atmpS4909;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS4906, _M0L1iS1512, _M0L6_2atmpS4907);
      _M0L6_2atmpS4912 = _M0L1iS1512 + 1;
      _M0L1iS1512 = _M0L6_2atmpS4912;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__hh(
  struct _M0TP26RiantR8snn__mbt2HH* _M0L1pS1439,
  float _M0L2dtS1465
) {
  int32_t _M0L1nS1438;
  struct _M0TP26RiantR8snn__mbt11HHParameter* _M0L3p__S1440;
  float _M0L2cmS1441;
  float _M0L2glS1442;
  float _M0L2elS1443;
  float _M0L2ekS1444;
  float _M0L2enS1445;
  float _M0L2gnS1446;
  float _M0L2gkS1447;
  float _M0L2vtS1448;
  float _M0L6tau__eS1449;
  float _M0L6tau__iS1450;
  float _M0L4e__eS1451;
  float _M0L4e__iS1452;
  int32_t _M0L7_2abindS1453;
  int32_t _M0L1iS1454;
  int32_t _M0L7_2abindS1479;
  int32_t _M0L1iS1480;
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L1nS1438 = _M0L1pS1439->$1;
  _M0L3p__S1440 = _M0L1pS1439->$0;
  _M0L2cmS1441 = _M0L3p__S1440->$0;
  _M0L2glS1442 = _M0L3p__S1440->$1;
  _M0L2elS1443 = _M0L3p__S1440->$2;
  _M0L2ekS1444 = _M0L3p__S1440->$3;
  _M0L2enS1445 = _M0L3p__S1440->$4;
  _M0L2gnS1446 = _M0L3p__S1440->$5;
  _M0L2gkS1447 = _M0L3p__S1440->$6;
  _M0L2vtS1448 = _M0L3p__S1440->$7;
  _M0L6tau__eS1449 = _M0L3p__S1440->$8;
  _M0L6tau__iS1450 = _M0L3p__S1440->$9;
  _M0L4e__eS1451 = _M0L3p__S1440->$10;
  _M0L4e__iS1452 = _M0L3p__S1440->$11;
  _M0L7_2abindS1453 = 0;
  _M0L1iS1454 = _M0L7_2abindS1453;
  while (1) {
    if (_M0L1iS1454 < _M0L1nS1438) {
      struct _M0TPB5ArrayGfE* _M0L1vS4814 = _M0L1pS1439->$2;
      float _M0L1vS1455;
      struct _M0TPB5ArrayGfE* _M0L1mS4813;
      float _M0L1mS1456;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4812;
      float _M0L2nnS1457;
      struct _M0TPB5ArrayGfE* _M0L1hS4811;
      float _M0L1hS1458;
      struct _M0TPB5ArrayGfE* _M0L2geS4810;
      float _M0L2geS1459;
      struct _M0TPB5ArrayGfE* _M0L2giS4809;
      float _M0L2giS1460;
      struct _M0TPB5ArrayGbE* _M0L4fireS4712;
      float _M0L6_2atmpS4808;
      float _M0L7am__numS1461;
      float _M0L6_2atmpS4807;
      float _M0L7bm__numS1462;
      float _M0L6_2atmpS4802;
      float _M0L6_2atmpS4801;
      float _M0L6_2atmpS4800;
      float _M0L2amS1463;
      float _M0L6_2atmpS4795;
      float _M0L6_2atmpS4794;
      float _M0L6_2atmpS4793;
      float _M0L2bmS1464;
      struct _M0TPB5ArrayGfE* _M0L1mS4713;
      float _M0L6_2atmpS4719;
      float _M0L6_2atmpS4717;
      float _M0L6_2atmpS4718;
      float _M0L6_2atmpS4716;
      float _M0L6_2atmpS4715;
      float _M0L6_2atmpS4714;
      float _M0L6_2atmpS4792;
      float _M0L7an__numS1466;
      float _M0L6_2atmpS4787;
      float _M0L6_2atmpS4786;
      float _M0L6_2atmpS4785;
      float _M0L2anS1467;
      float _M0L6_2atmpS4784;
      float _M0L6_2atmpS4783;
      float _M0L6_2atmpS4782;
      float _M0L6_2atmpS4781;
      float _M0L2bnS1468;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4720;
      float _M0L6_2atmpS4726;
      float _M0L6_2atmpS4724;
      float _M0L6_2atmpS4725;
      float _M0L6_2atmpS4723;
      float _M0L6_2atmpS4722;
      float _M0L6_2atmpS4721;
      float _M0L6_2atmpS4780;
      float _M0L6_2atmpS4779;
      float _M0L6_2atmpS4778;
      float _M0L6_2atmpS4777;
      float _M0L2ahS1469;
      float _M0L6_2atmpS4776;
      float _M0L6_2atmpS4775;
      float _M0L6_2atmpS4774;
      float _M0L6_2atmpS4773;
      float _M0L9bh__denomS1470;
      float _M0L2bhS1471;
      struct _M0TPB5ArrayGfE* _M0L1hS4727;
      float _M0L6_2atmpS4733;
      float _M0L6_2atmpS4731;
      float _M0L6_2atmpS4732;
      float _M0L6_2atmpS4730;
      float _M0L6_2atmpS4729;
      float _M0L6_2atmpS4728;
      struct _M0TPB5ArrayGfE* _M0L1mS4772;
      float _M0L6m__newS1472;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4771;
      float _M0L6n__newS1473;
      struct _M0TPB5ArrayGfE* _M0L1hS4770;
      float _M0L6h__newS1474;
      float _M0L6_2atmpS4769;
      float _M0L6_2atmpS4768;
      float _M0L3m3hS1475;
      float _M0L6_2atmpS4767;
      float _M0L6_2atmpS4766;
      float _M0L2n4S1476;
      struct _M0TPB5ArrayGfE* _M0L1iS4765;
      float _M0L6_2atmpS4762;
      float _M0L6_2atmpS4764;
      float _M0L6_2atmpS4763;
      float _M0L6_2atmpS4759;
      float _M0L6_2atmpS4761;
      float _M0L6_2atmpS4760;
      float _M0L6_2atmpS4756;
      float _M0L6_2atmpS4758;
      float _M0L6_2atmpS4757;
      float _M0L6_2atmpS4752;
      float _M0L6_2atmpS4754;
      float _M0L6_2atmpS4755;
      float _M0L6_2atmpS4753;
      float _M0L6_2atmpS4748;
      float _M0L6_2atmpS4750;
      float _M0L6_2atmpS4751;
      float _M0L6_2atmpS4749;
      float _M0L7currentS1477;
      struct _M0TPB5ArrayGfE* _M0L1vS4734;
      float _M0L6_2atmpS4737;
      float _M0L6_2atmpS4736;
      float _M0L6_2atmpS4735;
      struct _M0TPB5ArrayGfE* _M0L2geS4738;
      float _M0L6_2atmpS4742;
      float _M0L6_2atmpS4741;
      float _M0L6_2atmpS4740;
      float _M0L6_2atmpS4739;
      struct _M0TPB5ArrayGfE* _M0L2giS4743;
      float _M0L6_2atmpS4747;
      float _M0L6_2atmpS4746;
      float _M0L6_2atmpS4745;
      float _M0L6_2atmpS4744;
      int32_t _M0L6_2atmpS4815;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1vS1455 = _M0MPC15array5Array2atGfE(_M0L1vS4814, _M0L1iS1454);
      _M0L1mS4813 = _M0L1pS1439->$3;
      #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1mS1456 = _M0MPC15array5Array2atGfE(_M0L1mS4813, _M0L1iS1454);
      _M0L7n__gateS4812 = _M0L1pS1439->$4;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2nnS1457
      = _M0MPC15array5Array2atGfE(_M0L7n__gateS4812, _M0L1iS1454);
      _M0L1hS4811 = _M0L1pS1439->$5;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1hS1458 = _M0MPC15array5Array2atGfE(_M0L1hS4811, _M0L1iS1454);
      _M0L2geS4810 = _M0L1pS1439->$8;
      #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2geS1459 = _M0MPC15array5Array2atGfE(_M0L2geS4810, _M0L1iS1454);
      _M0L2giS4809 = _M0L1pS1439->$9;
      #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2giS1460 = _M0MPC15array5Array2atGfE(_M0L2giS4809, _M0L1iS1454);
      _M0L4fireS4712 = _M0L1pS1439->$6;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4712, _M0L1iS1454, 0);
      _M0L6_2atmpS4808 = 0x1.ap+3f - _M0L1vS1455;
      _M0L7am__numS1461 = _M0L6_2atmpS4808 + _M0L2vtS1448;
      _M0L6_2atmpS4807 = _M0L1vS1455 - _M0L2vtS1448;
      _M0L7bm__numS1462 = _M0L6_2atmpS4807 - 0x1.4p+5f;
      _M0L6_2atmpS4802 = _M0L7am__numS1461 / 0x1p+2f;
      #line 134 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4801 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4802);
      _M0L6_2atmpS4800 = _M0L6_2atmpS4801 - 0x1p+0f;
      if (_M0L6_2atmpS4800 != 0x0p+0f) {
        float _M0L6_2atmpS4803 = 0x1.47ae147ae147bp-2f * _M0L7am__numS1461;
        float _M0L6_2atmpS4806 = _M0L7am__numS1461 / 0x1p+2f;
        float _M0L6_2atmpS4805;
        float _M0L6_2atmpS4804;
        #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS4805 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4806);
        _M0L6_2atmpS4804 = _M0L6_2atmpS4805 - 0x1p+0f;
        _M0L2amS1463 = _M0L6_2atmpS4803 / _M0L6_2atmpS4804;
      } else {
        _M0L2amS1463 = 0x0p+0f;
      }
      _M0L6_2atmpS4795 = _M0L7bm__numS1462 / 0x1.4p+2f;
      #line 139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4794 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4795);
      _M0L6_2atmpS4793 = _M0L6_2atmpS4794 - 0x1p+0f;
      if (_M0L6_2atmpS4793 != 0x0p+0f) {
        float _M0L6_2atmpS4796 = 0x1.1eb851eb851ecp-2f * _M0L7bm__numS1462;
        float _M0L6_2atmpS4799 = _M0L7bm__numS1462 / 0x1.4p+2f;
        float _M0L6_2atmpS4798;
        float _M0L6_2atmpS4797;
        #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS4798 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4799);
        _M0L6_2atmpS4797 = _M0L6_2atmpS4798 - 0x1p+0f;
        _M0L2bmS1464 = _M0L6_2atmpS4796 / _M0L6_2atmpS4797;
      } else {
        _M0L2bmS1464 = 0x0p+0f;
      }
      _M0L1mS4713 = _M0L1pS1439->$3;
      _M0L6_2atmpS4719 = 0x1p+0f - _M0L1mS1456;
      _M0L6_2atmpS4717 = _M0L2amS1463 * _M0L6_2atmpS4719;
      _M0L6_2atmpS4718 = _M0L2bmS1464 * _M0L1mS1456;
      _M0L6_2atmpS4716 = _M0L6_2atmpS4717 - _M0L6_2atmpS4718;
      _M0L6_2atmpS4715 = _M0L2dtS1465 * _M0L6_2atmpS4716;
      _M0L6_2atmpS4714 = _M0L1mS1456 + _M0L6_2atmpS4715;
      #line 144 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1mS4713, _M0L1iS1454, _M0L6_2atmpS4714);
      _M0L6_2atmpS4792 = 0x1.ep+3f - _M0L1vS1455;
      _M0L7an__numS1466 = _M0L6_2atmpS4792 + _M0L2vtS1448;
      _M0L6_2atmpS4787 = _M0L7an__numS1466 / 0x1.4p+2f;
      #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4786 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4787);
      _M0L6_2atmpS4785 = _M0L6_2atmpS4786 - 0x1p+0f;
      if (_M0L6_2atmpS4785 != 0x0p+0f) {
        float _M0L6_2atmpS4788 = 0x1.0624dd2f1a9fcp-5f * _M0L7an__numS1466;
        float _M0L6_2atmpS4791 = _M0L7an__numS1466 / 0x1.4p+2f;
        float _M0L6_2atmpS4790;
        float _M0L6_2atmpS4789;
        #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS4790 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4791);
        _M0L6_2atmpS4789 = _M0L6_2atmpS4790 - 0x1p+0f;
        _M0L2anS1467 = _M0L6_2atmpS4788 / _M0L6_2atmpS4789;
      } else {
        _M0L2anS1467 = 0x0p+0f;
      }
      _M0L6_2atmpS4784 = 0x1.4p+3f - _M0L1vS1455;
      _M0L6_2atmpS4783 = _M0L6_2atmpS4784 + _M0L2vtS1448;
      _M0L6_2atmpS4782 = _M0L6_2atmpS4783 / 0x1.4p+5f;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4781 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4782);
      _M0L2bnS1468 = 0x1p-1f * _M0L6_2atmpS4781;
      _M0L7n__gateS4720 = _M0L1pS1439->$4;
      _M0L6_2atmpS4726 = 0x1p+0f - _M0L2nnS1457;
      _M0L6_2atmpS4724 = _M0L2anS1467 * _M0L6_2atmpS4726;
      _M0L6_2atmpS4725 = _M0L2bnS1468 * _M0L2nnS1457;
      _M0L6_2atmpS4723 = _M0L6_2atmpS4724 - _M0L6_2atmpS4725;
      _M0L6_2atmpS4722 = _M0L2dtS1465 * _M0L6_2atmpS4723;
      _M0L6_2atmpS4721 = _M0L2nnS1457 + _M0L6_2atmpS4722;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L7n__gateS4720, _M0L1iS1454, _M0L6_2atmpS4721);
      _M0L6_2atmpS4780 = 0x1.1p+4f - _M0L1vS1455;
      _M0L6_2atmpS4779 = _M0L6_2atmpS4780 + _M0L2vtS1448;
      _M0L6_2atmpS4778 = _M0L6_2atmpS4779 / 0x1.2p+4f;
      #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4777 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4778);
      _M0L2ahS1469 = 0x1.0624dd2f1a9fcp-3f * _M0L6_2atmpS4777;
      _M0L6_2atmpS4776 = 0x1.4p+5f - _M0L1vS1455;
      _M0L6_2atmpS4775 = _M0L6_2atmpS4776 + _M0L2vtS1448;
      _M0L6_2atmpS4774 = _M0L6_2atmpS4775 / 0x1.4p+2f;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4773 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4774);
      _M0L9bh__denomS1470 = 0x1p+0f + _M0L6_2atmpS4773;
      if (_M0L9bh__denomS1470 != 0x0p+0f) {
        _M0L2bhS1471 = 0x1p+2f / _M0L9bh__denomS1470;
      } else {
        _M0L2bhS1471 = 0x0p+0f;
      }
      _M0L1hS4727 = _M0L1pS1439->$5;
      _M0L6_2atmpS4733 = 0x1p+0f - _M0L1hS1458;
      _M0L6_2atmpS4731 = _M0L2ahS1469 * _M0L6_2atmpS4733;
      _M0L6_2atmpS4732 = _M0L2bhS1471 * _M0L1hS1458;
      _M0L6_2atmpS4730 = _M0L6_2atmpS4731 - _M0L6_2atmpS4732;
      _M0L6_2atmpS4729 = _M0L2dtS1465 * _M0L6_2atmpS4730;
      _M0L6_2atmpS4728 = _M0L1hS1458 + _M0L6_2atmpS4729;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1hS4727, _M0L1iS1454, _M0L6_2atmpS4728);
      _M0L1mS4772 = _M0L1pS1439->$3;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6m__newS1472 = _M0MPC15array5Array2atGfE(_M0L1mS4772, _M0L1iS1454);
      _M0L7n__gateS4771 = _M0L1pS1439->$4;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6n__newS1473
      = _M0MPC15array5Array2atGfE(_M0L7n__gateS4771, _M0L1iS1454);
      _M0L1hS4770 = _M0L1pS1439->$5;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6h__newS1474 = _M0MPC15array5Array2atGfE(_M0L1hS4770, _M0L1iS1454);
      _M0L6_2atmpS4769 = _M0L6m__newS1472 * _M0L6m__newS1472;
      _M0L6_2atmpS4768 = _M0L6_2atmpS4769 * _M0L6m__newS1472;
      _M0L3m3hS1475 = _M0L6_2atmpS4768 * _M0L6h__newS1474;
      _M0L6_2atmpS4767 = _M0L6n__newS1473 * _M0L6n__newS1473;
      _M0L6_2atmpS4766 = _M0L6_2atmpS4767 * _M0L6n__newS1473;
      _M0L2n4S1476 = _M0L6_2atmpS4766 * _M0L6n__newS1473;
      _M0L1iS4765 = _M0L1pS1439->$7;
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4762 = _M0MPC15array5Array2atGfE(_M0L1iS4765, _M0L1iS1454);
      _M0L6_2atmpS4764 = _M0L2elS1443 - _M0L1vS1455;
      _M0L6_2atmpS4763 = _M0L2glS1442 * _M0L6_2atmpS4764;
      _M0L6_2atmpS4759 = _M0L6_2atmpS4762 + _M0L6_2atmpS4763;
      _M0L6_2atmpS4761 = _M0L4e__eS1451 - _M0L1vS1455;
      _M0L6_2atmpS4760 = _M0L2geS1459 * _M0L6_2atmpS4761;
      _M0L6_2atmpS4756 = _M0L6_2atmpS4759 + _M0L6_2atmpS4760;
      _M0L6_2atmpS4758 = _M0L4e__iS1452 - _M0L1vS1455;
      _M0L6_2atmpS4757 = _M0L2giS1460 * _M0L6_2atmpS4758;
      _M0L6_2atmpS4752 = _M0L6_2atmpS4756 + _M0L6_2atmpS4757;
      _M0L6_2atmpS4754 = _M0L2gnS1446 * _M0L3m3hS1475;
      _M0L6_2atmpS4755 = _M0L2enS1445 - _M0L1vS1455;
      _M0L6_2atmpS4753 = _M0L6_2atmpS4754 * _M0L6_2atmpS4755;
      _M0L6_2atmpS4748 = _M0L6_2atmpS4752 + _M0L6_2atmpS4753;
      _M0L6_2atmpS4750 = _M0L2gkS1447 * _M0L2n4S1476;
      _M0L6_2atmpS4751 = _M0L2ekS1444 - _M0L1vS1455;
      _M0L6_2atmpS4749 = _M0L6_2atmpS4750 * _M0L6_2atmpS4751;
      _M0L7currentS1477 = _M0L6_2atmpS4748 + _M0L6_2atmpS4749;
      _M0L1vS4734 = _M0L1pS1439->$2;
      _M0L6_2atmpS4737 = _M0L2dtS1465 / _M0L2cmS1441;
      _M0L6_2atmpS4736 = _M0L6_2atmpS4737 * _M0L7currentS1477;
      _M0L6_2atmpS4735 = _M0L1vS1455 + _M0L6_2atmpS4736;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4734, _M0L1iS1454, _M0L6_2atmpS4735);
      _M0L2geS4738 = _M0L1pS1439->$8;
      _M0L6_2atmpS4742 = -_M0L2geS1459;
      _M0L6_2atmpS4741 = _M0L6_2atmpS4742 / _M0L6tau__eS1449;
      _M0L6_2atmpS4740 = _M0L2dtS1465 * _M0L6_2atmpS4741;
      _M0L6_2atmpS4739 = _M0L2geS1459 + _M0L6_2atmpS4740;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4738, _M0L1iS1454, _M0L6_2atmpS4739);
      _M0L2giS4743 = _M0L1pS1439->$9;
      _M0L6_2atmpS4747 = -_M0L2giS1460;
      _M0L6_2atmpS4746 = _M0L6_2atmpS4747 / _M0L6tau__iS1450;
      _M0L6_2atmpS4745 = _M0L2dtS1465 * _M0L6_2atmpS4746;
      _M0L6_2atmpS4744 = _M0L2giS1460 + _M0L6_2atmpS4745;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4743, _M0L1iS1454, _M0L6_2atmpS4744);
      _M0L6_2atmpS4815 = _M0L1iS1454 + 1;
      _M0L1iS1454 = _M0L6_2atmpS4815;
      continue;
    }
    break;
  }
  _M0L7_2abindS1479 = 0;
  _M0L1iS1480 = _M0L7_2abindS1479;
  while (1) {
    if (_M0L1iS1480 < _M0L1nS1438) {
      struct _M0TPB5ArrayGbE* _M0L4fireS4816 = _M0L1pS1439->$6;
      struct _M0TPB5ArrayGfE* _M0L1vS4819 = _M0L1pS1439->$2;
      float _M0L6_2atmpS4818;
      int32_t _M0L6_2atmpS4817;
      int32_t _M0L6_2atmpS4820;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4818 = _M0MPC15array5Array2atGfE(_M0L1vS4819, _M0L1iS1480);
      _M0L6_2atmpS4817 = _M0L6_2atmpS4818 > -0x1.4p+4f;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4816, _M0L1iS1480, _M0L6_2atmpS4817);
      _M0L6_2atmpS4820 = _M0L1iS1480 + 1;
      _M0L1iS1480 = _M0L6_2atmpS4820;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__hetrec(
  struct _M0TP26RiantR8snn__mbt6HetRec* _M0L1pS1409,
  float _M0L2dtS1417
) {
  int32_t _M0L1nS1408;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4711;
  int32_t _M0L2ndS1410;
  int32_t _M0L8total__dS1411;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4710;
  float _M0L9steepnessS1412;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4709;
  float _M0L6tau__mS1413;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4708;
  float _M0L9tau__rateS1414;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4707;
  float _M0L8tau__absS1415;
  float _M0L6_2atmpS4706;
  int32_t _M0L11tabs__stepsS1416;
  int32_t _M0L7_2abindS1418;
  int32_t _M0L1iS1419;
  int32_t _M0L7_2abindS1422;
  int32_t _M0L1iS1423;
  int32_t _M0L7_2abindS1432;
  int32_t _M0L1iS1433;
  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L1nS1408 = _M0L1pS1409->$1;
  _M0L5paramS4711 = _M0L1pS1409->$0;
  _M0L2ndS1410 = _M0L5paramS4711->$0;
  _M0L8total__dS1411 = _M0L1nS1408 * _M0L2ndS1410;
  _M0L5paramS4710 = _M0L1pS1409->$0;
  _M0L9steepnessS1412 = _M0L5paramS4710->$7;
  _M0L5paramS4709 = _M0L1pS1409->$0;
  _M0L6tau__mS1413 = _M0L5paramS4709->$8;
  _M0L5paramS4708 = _M0L1pS1409->$0;
  _M0L9tau__rateS1414 = _M0L5paramS4708->$9;
  _M0L5paramS4707 = _M0L1pS1409->$0;
  _M0L8tau__absS1415 = _M0L5paramS4707->$6;
  _M0L6_2atmpS4706 = _M0L8tau__absS1415 / _M0L2dtS1417;
  #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L11tabs__stepsS1416 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4706);
  _M0L7_2abindS1418 = 0;
  _M0L1iS1419 = _M0L7_2abindS1418;
  while (1) {
    if (_M0L1iS1419 < _M0L8total__dS1411) {
      struct _M0TPB5ArrayGfE* _M0L6tau__dS4634 = _M0L1pS1409->$6;
      float _M0L7tau__diS1420;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4622;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4633;
      float _M0L6_2atmpS4624;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4632;
      float _M0L6_2atmpS4631;
      float _M0L6_2atmpS4628;
      struct _M0TPB5ArrayGfE* _M0L4is__S4630;
      float _M0L6_2atmpS4629;
      float _M0L6_2atmpS4627;
      float _M0L6_2atmpS4626;
      float _M0L6_2atmpS4625;
      float _M0L6_2atmpS4623;
      int32_t _M0L6_2atmpS4635;
      #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L7tau__diS1420
      = _M0MPC15array5Array2atGfE(_M0L6tau__dS4634, _M0L1iS1419);
      _M0L4v__dS4622 = _M0L1pS1409->$2;
      _M0L4v__dS4633 = _M0L1pS1409->$2;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4624
      = _M0MPC15array5Array2atGfE(_M0L4v__dS4633, _M0L1iS1419);
      _M0L4v__dS4632 = _M0L1pS1409->$2;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4631
      = _M0MPC15array5Array2atGfE(_M0L4v__dS4632, _M0L1iS1419);
      _M0L6_2atmpS4628 = -_M0L6_2atmpS4631;
      _M0L4is__S4630 = _M0L1pS1409->$4;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4629
      = _M0MPC15array5Array2atGfE(_M0L4is__S4630, _M0L1iS1419);
      _M0L6_2atmpS4627 = _M0L6_2atmpS4628 - _M0L6_2atmpS4629;
      _M0L6_2atmpS4626 = _M0L2dtS1417 * _M0L6_2atmpS4627;
      _M0L6_2atmpS4625 = _M0L6_2atmpS4626 / _M0L7tau__diS1420;
      _M0L6_2atmpS4623 = _M0L6_2atmpS4624 + _M0L6_2atmpS4625;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__dS4622, _M0L1iS1419, _M0L6_2atmpS4623);
      _M0L6_2atmpS4635 = _M0L1iS1419 + 1;
      _M0L1iS1419 = _M0L6_2atmpS4635;
      continue;
    }
    break;
  }
  _M0L7_2abindS1422 = 0;
  _M0L1iS1423 = _M0L7_2abindS1422;
  while (1) {
    if (_M0L1iS1423 < _M0L1nS1408) {
      struct _M0TPB5ArrayGiE* _M0L6colptrS4656 = _M0L1pS1409->$11;
      int32_t _M0L5startS1424;
      struct _M0TPB5ArrayGiE* _M0L6colptrS4654;
      int32_t _M0L6_2atmpS4655;
      int32_t _M0L3endS1425;
      float _M0L16dt__over__tau__mS1426;
      struct _M0TPB8MutLocalGiE* _M0L1sS1427;
      int32_t _M0L6_2atmpS4657;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L5startS1424
      = _M0MPC15array5Array2atGiE(_M0L6colptrS4656, _M0L1iS1423);
      _M0L6colptrS4654 = _M0L1pS1409->$11;
      _M0L6_2atmpS4655 = _M0L1iS1423 + 1;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L3endS1425
      = _M0MPC15array5Array2atGiE(_M0L6colptrS4654, _M0L6_2atmpS4655);
      _M0L16dt__over__tau__mS1426 = _M0L2dtS1417 / _M0L6tau__mS1413;
      _M0L1sS1427
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1427)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1427->$0 = _M0L5startS1424;
      while (1) {
        int32_t _M0L3valS4636 = _M0L1sS1427->$0;
        if (_M0L3valS4636 < _M0L3endS1425) {
          struct _M0TPB5ArrayGiE* _M0L6i__synS4652 = _M0L1pS1409->$12;
          int32_t _M0L3valS4653 = _M0L1sS1427->$0;
          int32_t _M0L9dend__idxS1428;
          struct _M0TPB5ArrayGfE* _M0L6w__synS4650;
          int32_t _M0L3valS4651;
          float _M0L1wS1429;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4637;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4647;
          float _M0L6_2atmpS4639;
          struct _M0TPB5ArrayGfE* _M0L4v__dS4646;
          float _M0L6_2atmpS4645;
          float _M0L6_2atmpS4642;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4644;
          float _M0L6_2atmpS4643;
          float _M0L6_2atmpS4641;
          float _M0L6_2atmpS4640;
          float _M0L6_2atmpS4638;
          int32_t _M0L3valS4649;
          int32_t _M0L6_2atmpS4648;
          #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L9dend__idxS1428
          = _M0MPC15array5Array2atGiE(_M0L6i__synS4652, _M0L3valS4653);
          _M0L6w__synS4650 = _M0L1pS1409->$13;
          _M0L3valS4651 = _M0L1sS1427->$0;
          #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L1wS1429
          = _M0MPC15array5Array2atGfE(_M0L6w__synS4650, _M0L3valS4651);
          _M0L4v__sS4637 = _M0L1pS1409->$3;
          _M0L4v__sS4647 = _M0L1pS1409->$3;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4639
          = _M0MPC15array5Array2atGfE(_M0L4v__sS4647, _M0L1iS1423);
          _M0L4v__dS4646 = _M0L1pS1409->$2;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4645
          = _M0MPC15array5Array2atGfE(_M0L4v__dS4646, _M0L9dend__idxS1428);
          _M0L6_2atmpS4642 = _M0L1wS1429 * _M0L6_2atmpS4645;
          _M0L4v__sS4644 = _M0L1pS1409->$3;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4643
          = _M0MPC15array5Array2atGfE(_M0L4v__sS4644, _M0L1iS1423);
          _M0L6_2atmpS4641 = _M0L6_2atmpS4642 - _M0L6_2atmpS4643;
          _M0L6_2atmpS4640 = _M0L6_2atmpS4641 * _M0L16dt__over__tau__mS1426;
          _M0L6_2atmpS4638 = _M0L6_2atmpS4639 + _M0L6_2atmpS4640;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0MPC15array5Array3setGfE(_M0L4v__sS4637, _M0L1iS1423, _M0L6_2atmpS4638);
          _M0L3valS4649 = _M0L1sS1427->$0;
          _M0L6_2atmpS4648 = _M0L3valS4649 + 1;
          _M0L1sS1427->$0 = _M0L6_2atmpS4648;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1427);
        }
        break;
      }
      _M0L6_2atmpS4657 = _M0L1iS1423 + 1;
      _M0L1iS1423 = _M0L6_2atmpS4657;
      continue;
    }
    break;
  }
  _M0L7_2abindS1432 = 0;
  _M0L1iS1433 = _M0L7_2abindS1432;
  while (1) {
    if (_M0L1iS1433 < _M0L1nS1408) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS4659 = _M0L1pS1409->$8;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4662 = _M0L1pS1409->$8;
      int32_t _M0L6_2atmpS4661;
      int32_t _M0L6_2atmpS4660;
      struct _M0TPB5ArrayGbE* _M0L4fireS4663;
      struct _M0TPB5ArrayGfE* _M0L5traceS4664;
      struct _M0TPB5ArrayGfE* _M0L5traceS4672;
      float _M0L6_2atmpS4666;
      struct _M0TPB5ArrayGfE* _M0L5traceS4671;
      float _M0L6_2atmpS4670;
      float _M0L6_2atmpS4669;
      float _M0L6_2atmpS4668;
      float _M0L6_2atmpS4667;
      float _M0L6_2atmpS4665;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4674;
      int32_t _M0L6_2atmpS4673;
      struct _M0TPB5ArrayGfE* _M0L5traceS4675;
      struct _M0TPB5ArrayGfE* _M0L5traceS4684;
      float _M0L6_2atmpS4677;
      struct _M0TPB5ArrayGfE* _M0L4v__sS4683;
      float _M0L6_2atmpS4680;
      struct _M0TPB5ArrayGfE* _M0L5traceS4682;
      float _M0L6_2atmpS4681;
      float _M0L6_2atmpS4679;
      float _M0L6_2atmpS4678;
      float _M0L6_2atmpS4676;
      float _M0L6_2atmpS4700;
      struct _M0TPB5ArrayGfE* _M0L4v__sS4705;
      float _M0L6_2atmpS4702;
      struct _M0TPB5ArrayGfE* _M0L5traceS4704;
      float _M0L6_2atmpS4703;
      float _M0L6_2atmpS4701;
      float _M0L12sigmoid__argS1436;
      float _M0L4rateS1437;
      struct _M0TPB5ArrayGfE* _M0L9randcacheS4686;
      float _M0L6_2atmpS4685;
      int32_t _M0L6_2atmpS4658;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4661
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4662, _M0L1iS1433);
      _M0L6_2atmpS4660 = _M0L6_2atmpS4661 - 1;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4659, _M0L1iS1433, _M0L6_2atmpS4660);
      _M0L4fireS4663 = _M0L1pS1409->$7;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4663, _M0L1iS1433, 0);
      _M0L5traceS4664 = _M0L1pS1409->$9;
      _M0L5traceS4672 = _M0L1pS1409->$9;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4666
      = _M0MPC15array5Array2atGfE(_M0L5traceS4672, _M0L1iS1433);
      _M0L5traceS4671 = _M0L1pS1409->$9;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4670
      = _M0MPC15array5Array2atGfE(_M0L5traceS4671, _M0L1iS1433);
      _M0L6_2atmpS4669 = -_M0L6_2atmpS4670;
      _M0L6_2atmpS4668 = _M0L6_2atmpS4669 / _M0L9tau__rateS1414;
      _M0L6_2atmpS4667 = _M0L2dtS1417 * _M0L6_2atmpS4668;
      _M0L6_2atmpS4665 = _M0L6_2atmpS4666 + _M0L6_2atmpS4667;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L5traceS4664, _M0L1iS1433, _M0L6_2atmpS4665);
      _M0L4tabsS4674 = _M0L1pS1409->$8;
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4673
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4674, _M0L1iS1433);
      if (_M0L6_2atmpS4673 > 0) {
        goto join_1434;
      }
      _M0L5traceS4675 = _M0L1pS1409->$9;
      _M0L5traceS4684 = _M0L1pS1409->$9;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4677
      = _M0MPC15array5Array2atGfE(_M0L5traceS4684, _M0L1iS1433);
      _M0L4v__sS4683 = _M0L1pS1409->$3;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4680
      = _M0MPC15array5Array2atGfE(_M0L4v__sS4683, _M0L1iS1433);
      _M0L5traceS4682 = _M0L1pS1409->$9;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4681
      = _M0MPC15array5Array2atGfE(_M0L5traceS4682, _M0L1iS1433);
      _M0L6_2atmpS4679 = _M0L6_2atmpS4680 - _M0L6_2atmpS4681;
      _M0L6_2atmpS4678 = _M0L6_2atmpS4679 / _M0L9tau__rateS1414;
      _M0L6_2atmpS4676 = _M0L6_2atmpS4677 + _M0L6_2atmpS4678;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L5traceS4675, _M0L1iS1433, _M0L6_2atmpS4676);
      _M0L6_2atmpS4700 = -_M0L9steepnessS1412;
      _M0L4v__sS4705 = _M0L1pS1409->$3;
      #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4702
      = _M0MPC15array5Array2atGfE(_M0L4v__sS4705, _M0L1iS1433);
      _M0L5traceS4704 = _M0L1pS1409->$9;
      #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4703
      = _M0MPC15array5Array2atGfE(_M0L5traceS4704, _M0L1iS1433);
      _M0L6_2atmpS4701 = _M0L6_2atmpS4702 - _M0L6_2atmpS4703;
      _M0L12sigmoid__argS1436 = _M0L6_2atmpS4700 * _M0L6_2atmpS4701;
      if (_M0L12sigmoid__argS1436 > 0x1.6p+6f) {
        struct _M0TPB5ArrayGfE* _M0L1rS4694 = _M0L1pS1409->$5;
        float _M0L6_2atmpS4693;
        #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4693
        = _M0MPC15array5Array2atGfE(_M0L1rS4694, _M0L1iS1433);
        _M0L4rateS1437 = _M0L6_2atmpS4693 * _M0L2dtS1417;
      } else if (_M0L12sigmoid__argS1436 < -0x1.6p+6f) {
        _M0L4rateS1437 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1rS4699 = _M0L1pS1409->$5;
        float _M0L6_2atmpS4698;
        float _M0L6_2atmpS4695;
        float _M0L6_2atmpS4697;
        float _M0L6_2atmpS4696;
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4698
        = _M0MPC15array5Array2atGfE(_M0L1rS4699, _M0L1iS1433);
        _M0L6_2atmpS4695 = _M0L6_2atmpS4698 * _M0L2dtS1417;
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4697
        = _M0FP26RiantR8snn__mbt4expf(_M0L12sigmoid__argS1436);
        _M0L6_2atmpS4696 = 0x1p+0f + _M0L6_2atmpS4697;
        _M0L4rateS1437 = _M0L6_2atmpS4695 / _M0L6_2atmpS4696;
      }
      _M0L9randcacheS4686 = _M0L1pS1409->$10;
      #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4685
      = _M0MPC15array5Array2atGfE(_M0L9randcacheS4686, _M0L1iS1433);
      if (_M0L6_2atmpS4685 < _M0L4rateS1437) {
        struct _M0TPB5ArrayGbE* _M0L4fireS4687 = _M0L1pS1409->$7;
        struct _M0TPB5ArrayGiE* _M0L4tabsS4688;
        struct _M0TPB5ArrayGfE* _M0L5traceS4689;
        struct _M0TPB5ArrayGfE* _M0L5traceS4692;
        float _M0L6_2atmpS4691;
        float _M0L6_2atmpS4690;
        #line 267 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS4687, _M0L1iS1433, 1);
        _M0L4tabsS4688 = _M0L1pS1409->$8;
        #line 268 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS4688, _M0L1iS1433, _M0L11tabs__stepsS1416);
        _M0L5traceS4689 = _M0L1pS1409->$9;
        _M0L5traceS4692 = _M0L1pS1409->$9;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4691
        = _M0MPC15array5Array2atGfE(_M0L5traceS4692, _M0L1iS1433);
        _M0L6_2atmpS4690 = _M0L6_2atmpS4691 + 0x1p+0f;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGfE(_M0L5traceS4689, _M0L1iS1433, _M0L6_2atmpS4690);
      }
      goto join_1434;
      goto joinlet_5751;
      join_1434:;
      _M0L6_2atmpS4658 = _M0L1iS1433 + 1;
      _M0L1iS1433 = _M0L6_2atmpS4658;
      continue;
      joinlet_5751:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt18step__adex__sinexp(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1387,
  float _M0L2dtS1402
) {
  int32_t _M0L1nS1386;
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0L3p__S1388;
  float _M0L2tmS1389;
  float _M0L2vtS1390;
  float _M0L2vrS1391;
  float _M0L2elS1392;
  float _M0L1rS1393;
  float _M0L9dt__slopeS1394;
  float _M0L2twS1395;
  float _M0L1aS1396;
  float _M0L1bS1397;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4621;
  float _M0L2atS1398;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4620;
  float _M0L6tau__aS1399;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4619;
  float _M0L11tabs__constS1400;
  float _M0L6_2atmpS4618;
  int32_t _M0L11tabs__stepsS1401;
  int32_t _M0L7_2abindS1403;
  int32_t _M0L1iS1404;
  #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1386 = _M0L1pS1387->$2;
  _M0L3p__S1388 = _M0L1pS1387->$0;
  _M0L2tmS1389 = _M0L3p__S1388->$5;
  _M0L2vtS1390 = _M0L3p__S1388->$2;
  _M0L2vrS1391 = _M0L3p__S1388->$3;
  _M0L2elS1392 = _M0L3p__S1388->$4;
  _M0L1rS1393 = _M0L3p__S1388->$6;
  _M0L9dt__slopeS1394 = _M0L3p__S1388->$7;
  _M0L2twS1395 = _M0L3p__S1388->$8;
  _M0L1aS1396 = _M0L3p__S1388->$9;
  _M0L1bS1397 = _M0L3p__S1388->$10;
  _M0L5spikeS4621 = _M0L1pS1387->$1;
  _M0L2atS1398 = _M0L5spikeS4621->$0;
  _M0L5spikeS4620 = _M0L1pS1387->$1;
  _M0L6tau__aS1399 = _M0L5spikeS4620->$1;
  _M0L5spikeS4619 = _M0L1pS1387->$1;
  _M0L11tabs__constS1400 = _M0L5spikeS4619->$3;
  _M0L6_2atmpS4618 = _M0L11tabs__constS1400 / _M0L2dtS1402;
  #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L11tabs__stepsS1401 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4618);
  _M0L7_2abindS1403 = 0;
  _M0L1iS1404 = _M0L7_2abindS1403;
  while (1) {
    if (_M0L1iS1404 < _M0L1nS1386) {
      struct _M0TPB5ArrayGfE* _M0L1vS4531 = _M0L1pS1387->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS4533 = _M0L1pS1387->$5;
      float _M0L6_2atmpS4532;
      struct _M0TPB5ArrayGbE* _M0L4fireS4535;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4536;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4539;
      int32_t _M0L6_2atmpS4538;
      int32_t _M0L6_2atmpS4537;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4541;
      int32_t _M0L6_2atmpS4540;
      struct _M0TPB5ArrayGfE* _M0L1wS4542;
      struct _M0TPB5ArrayGfE* _M0L1wS4554;
      float _M0L6_2atmpS4544;
      struct _M0TPB5ArrayGfE* _M0L1vS4553;
      float _M0L6_2atmpS4552;
      float _M0L6_2atmpS4551;
      float _M0L6_2atmpS4548;
      struct _M0TPB5ArrayGfE* _M0L1wS4550;
      float _M0L6_2atmpS4549;
      float _M0L6_2atmpS4547;
      float _M0L6_2atmpS4546;
      float _M0L6_2atmpS4545;
      float _M0L6_2atmpS4543;
      float _M0L9exp__termS1407;
      struct _M0TPB5ArrayGfE* _M0L1vS4555;
      struct _M0TPB5ArrayGfE* _M0L1vS4577;
      float _M0L6_2atmpS4557;
      struct _M0TPB5ArrayGfE* _M0L1vS4576;
      float _M0L6_2atmpS4575;
      float _M0L6_2atmpS4574;
      float _M0L6_2atmpS4573;
      float _M0L6_2atmpS4569;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4572;
      float _M0L6_2atmpS4571;
      float _M0L6_2atmpS4570;
      float _M0L6_2atmpS4565;
      struct _M0TPB5ArrayGfE* _M0L1wS4568;
      float _M0L6_2atmpS4567;
      float _M0L6_2atmpS4566;
      float _M0L6_2atmpS4561;
      struct _M0TPB5ArrayGfE* _M0L1iS4564;
      float _M0L6_2atmpS4563;
      float _M0L6_2atmpS4562;
      float _M0L6_2atmpS4560;
      float _M0L6_2atmpS4559;
      float _M0L6_2atmpS4558;
      float _M0L6_2atmpS4556;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4578;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4586;
      float _M0L6_2atmpS4580;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4585;
      float _M0L6_2atmpS4584;
      float _M0L6_2atmpS4583;
      float _M0L6_2atmpS4582;
      float _M0L6_2atmpS4581;
      float _M0L6_2atmpS4579;
      struct _M0TPB5ArrayGbE* _M0L4fireS4587;
      struct _M0TPB5ArrayGfE* _M0L1vS4590;
      float _M0L6_2atmpS4589;
      int32_t _M0L6_2atmpS4588;
      struct _M0TPB5ArrayGfE* _M0L1vS4591;
      struct _M0TPB5ArrayGbE* _M0L4fireS4593;
      float _M0L6_2atmpS4592;
      struct _M0TPB5ArrayGfE* _M0L1wS4595;
      struct _M0TPB5ArrayGbE* _M0L4fireS4597;
      float _M0L6_2atmpS4596;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4601;
      struct _M0TPB5ArrayGbE* _M0L4fireS4603;
      float _M0L6_2atmpS4602;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4607;
      struct _M0TPB5ArrayGbE* _M0L4fireS4609;
      int32_t _M0L6_2atmpS4608;
      int32_t _M0L6_2atmpS4530;
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4533, _M0L1iS1404)) {
        _M0L6_2atmpS4532 = _M0L2vrS1391;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4534 = _M0L1pS1387->$3;
        #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4532
        = _M0MPC15array5Array2atGfE(_M0L1vS4534, _M0L1iS1404);
      }
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4531, _M0L1iS1404, _M0L6_2atmpS4532);
      _M0L4fireS4535 = _M0L1pS1387->$5;
      #line 212 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4535, _M0L1iS1404, 0);
      _M0L4tabsS4536 = _M0L1pS1387->$7;
      _M0L4tabsS4539 = _M0L1pS1387->$7;
      #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4538
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4539, _M0L1iS1404);
      _M0L6_2atmpS4537 = _M0L6_2atmpS4538 - 1;
      #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4536, _M0L1iS1404, _M0L6_2atmpS4537);
      _M0L4tabsS4541 = _M0L1pS1387->$7;
      #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4540
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4541, _M0L1iS1404);
      if (_M0L6_2atmpS4540 > 0) {
        goto join_1405;
      }
      _M0L1wS4542 = _M0L1pS1387->$4;
      _M0L1wS4554 = _M0L1pS1387->$4;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4544 = _M0MPC15array5Array2atGfE(_M0L1wS4554, _M0L1iS1404);
      _M0L1vS4553 = _M0L1pS1387->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4552 = _M0MPC15array5Array2atGfE(_M0L1vS4553, _M0L1iS1404);
      _M0L6_2atmpS4551 = _M0L6_2atmpS4552 - _M0L2elS1392;
      _M0L6_2atmpS4548 = _M0L1aS1396 * _M0L6_2atmpS4551;
      _M0L1wS4550 = _M0L1pS1387->$4;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4549 = _M0MPC15array5Array2atGfE(_M0L1wS4550, _M0L1iS1404);
      _M0L6_2atmpS4547 = _M0L6_2atmpS4548 - _M0L6_2atmpS4549;
      _M0L6_2atmpS4546 = _M0L2dtS1402 * _M0L6_2atmpS4547;
      _M0L6_2atmpS4545 = _M0L6_2atmpS4546 / _M0L2twS1395;
      _M0L6_2atmpS4543 = _M0L6_2atmpS4544 + _M0L6_2atmpS4545;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4542, _M0L1iS1404, _M0L6_2atmpS4543);
      if (_M0L9dt__slopeS1394 < 0x0p+0f) {
        _M0L9exp__termS1407 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4617 = _M0L1pS1387->$3;
        float _M0L6_2atmpS4614;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4616;
        float _M0L6_2atmpS4615;
        float _M0L6_2atmpS4613;
        float _M0L6_2atmpS4612;
        float _M0L6_2atmpS4611;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4614
        = _M0MPC15array5Array2atGfE(_M0L1vS4617, _M0L1iS1404);
        _M0L9thresholdS4616 = _M0L1pS1387->$6;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4615
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4616, _M0L1iS1404);
        _M0L6_2atmpS4613 = _M0L6_2atmpS4614 - _M0L6_2atmpS4615;
        _M0L6_2atmpS4612 = _M0L6_2atmpS4613 / _M0L9dt__slopeS1394;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4611 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4612);
        _M0L9exp__termS1407 = _M0L9dt__slopeS1394 * _M0L6_2atmpS4611;
      }
      _M0L1vS4555 = _M0L1pS1387->$3;
      _M0L1vS4577 = _M0L1pS1387->$3;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4557 = _M0MPC15array5Array2atGfE(_M0L1vS4577, _M0L1iS1404);
      _M0L1vS4576 = _M0L1pS1387->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4575 = _M0MPC15array5Array2atGfE(_M0L1vS4576, _M0L1iS1404);
      _M0L6_2atmpS4574 = _M0L6_2atmpS4575 - _M0L2elS1392;
      _M0L6_2atmpS4573 = -_M0L6_2atmpS4574;
      _M0L6_2atmpS4569 = _M0L6_2atmpS4573 + _M0L9exp__termS1407;
      _M0L9syn__currS4572 = _M0L1pS1387->$9;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4571
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS4572, _M0L1iS1404);
      _M0L6_2atmpS4570 = _M0L1rS1393 * _M0L6_2atmpS4571;
      _M0L6_2atmpS4565 = _M0L6_2atmpS4569 - _M0L6_2atmpS4570;
      _M0L1wS4568 = _M0L1pS1387->$4;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4567 = _M0MPC15array5Array2atGfE(_M0L1wS4568, _M0L1iS1404);
      _M0L6_2atmpS4566 = _M0L1rS1393 * _M0L6_2atmpS4567;
      _M0L6_2atmpS4561 = _M0L6_2atmpS4565 - _M0L6_2atmpS4566;
      _M0L1iS4564 = _M0L1pS1387->$8;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4563 = _M0MPC15array5Array2atGfE(_M0L1iS4564, _M0L1iS1404);
      _M0L6_2atmpS4562 = _M0L1rS1393 * _M0L6_2atmpS4563;
      _M0L6_2atmpS4560 = _M0L6_2atmpS4561 + _M0L6_2atmpS4562;
      _M0L6_2atmpS4559 = _M0L2dtS1402 * _M0L6_2atmpS4560;
      _M0L6_2atmpS4558 = _M0L6_2atmpS4559 / _M0L2tmS1389;
      _M0L6_2atmpS4556 = _M0L6_2atmpS4557 + _M0L6_2atmpS4558;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4555, _M0L1iS1404, _M0L6_2atmpS4556);
      _M0L9thresholdS4578 = _M0L1pS1387->$6;
      _M0L9thresholdS4586 = _M0L1pS1387->$6;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4580
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4586, _M0L1iS1404);
      _M0L9thresholdS4585 = _M0L1pS1387->$6;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4584
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4585, _M0L1iS1404);
      _M0L6_2atmpS4583 = _M0L2vtS1390 - _M0L6_2atmpS4584;
      _M0L6_2atmpS4582 = _M0L2dtS1402 * _M0L6_2atmpS4583;
      _M0L6_2atmpS4581 = _M0L6_2atmpS4582 / _M0L6tau__aS1399;
      _M0L6_2atmpS4579 = _M0L6_2atmpS4580 + _M0L6_2atmpS4581;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4578, _M0L1iS1404, _M0L6_2atmpS4579);
      _M0L4fireS4587 = _M0L1pS1387->$5;
      _M0L1vS4590 = _M0L1pS1387->$3;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4589 = _M0MPC15array5Array2atGfE(_M0L1vS4590, _M0L1iS1404);
      _M0L6_2atmpS4588 = _M0L6_2atmpS4589 >= 0x0p+0f;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4587, _M0L1iS1404, _M0L6_2atmpS4588);
      _M0L1vS4591 = _M0L1pS1387->$3;
      _M0L4fireS4593 = _M0L1pS1387->$5;
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4593, _M0L1iS1404)) {
        _M0L6_2atmpS4592 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4594 = _M0L1pS1387->$3;
        #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4592
        = _M0MPC15array5Array2atGfE(_M0L1vS4594, _M0L1iS1404);
      }
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4591, _M0L1iS1404, _M0L6_2atmpS4592);
      _M0L1wS4595 = _M0L1pS1387->$4;
      _M0L4fireS4597 = _M0L1pS1387->$5;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4597, _M0L1iS1404)) {
        struct _M0TPB5ArrayGfE* _M0L1wS4599 = _M0L1pS1387->$4;
        float _M0L6_2atmpS4598;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4598
        = _M0MPC15array5Array2atGfE(_M0L1wS4599, _M0L1iS1404);
        _M0L6_2atmpS4596 = _M0L6_2atmpS4598 + _M0L1bS1397;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS4600 = _M0L1pS1387->$4;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4596
        = _M0MPC15array5Array2atGfE(_M0L1wS4600, _M0L1iS1404);
      }
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4595, _M0L1iS1404, _M0L6_2atmpS4596);
      _M0L9thresholdS4601 = _M0L1pS1387->$6;
      _M0L4fireS4603 = _M0L1pS1387->$5;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4603, _M0L1iS1404)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4605 = _M0L1pS1387->$6;
        float _M0L6_2atmpS4604;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4604
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4605, _M0L1iS1404);
        _M0L6_2atmpS4602 = _M0L6_2atmpS4604 + _M0L2atS1398;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4606 = _M0L1pS1387->$6;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4602
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4606, _M0L1iS1404);
      }
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4601, _M0L1iS1404, _M0L6_2atmpS4602);
      _M0L4tabsS4607 = _M0L1pS1387->$7;
      _M0L4fireS4609 = _M0L1pS1387->$5;
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4609, _M0L1iS1404)) {
        _M0L6_2atmpS4608 = _M0L11tabs__stepsS1401;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4610 = _M0L1pS1387->$7;
        #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4608
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4610, _M0L1iS1404);
      }
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4607, _M0L1iS1404, _M0L6_2atmpS4608);
      goto join_1405;
      goto joinlet_5753;
      join_1405:;
      _M0L6_2atmpS4530 = _M0L1iS1404 + 1;
      _M0L1iS1404 = _M0L6_2atmpS4530;
      continue;
      joinlet_5753:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1382
) {
  int32_t _M0L1nS1381;
  int32_t _M0L7_2abindS1383;
  int32_t _M0L1iS1384;
  #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1381 = _M0L1pS1382->$2;
  _M0L7_2abindS1383 = 0;
  _M0L1iS1384 = _M0L7_2abindS1383;
  while (1) {
    if (_M0L1iS1384 < _M0L1nS1381) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4507 = _M0L1pS1382->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS4528 = _M0L1pS1382->$10;
      float _M0L6_2atmpS4523;
      struct _M0TPB5ArrayGfE* _M0L1vS4527;
      float _M0L6_2atmpS4525;
      float _M0L4e__eS4526;
      float _M0L6_2atmpS4524;
      float _M0L6_2atmpS4520;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS4522;
      float _M0L6_2atmpS4521;
      float _M0L6_2atmpS4509;
      struct _M0TPB5ArrayGfE* _M0L2giS4519;
      float _M0L6_2atmpS4514;
      struct _M0TPB5ArrayGfE* _M0L1vS4518;
      float _M0L6_2atmpS4516;
      float _M0L4e__iS4517;
      float _M0L6_2atmpS4515;
      float _M0L6_2atmpS4511;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS4513;
      float _M0L6_2atmpS4512;
      float _M0L6_2atmpS4510;
      float _M0L6_2atmpS4508;
      int32_t _M0L6_2atmpS4529;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4523 = _M0MPC15array5Array2atGfE(_M0L2geS4528, _M0L1iS1384);
      _M0L1vS4527 = _M0L1pS1382->$3;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4525 = _M0MPC15array5Array2atGfE(_M0L1vS4527, _M0L1iS1384);
      _M0L4e__eS4526 = _M0L1pS1382->$16;
      _M0L6_2atmpS4524 = _M0L6_2atmpS4525 - _M0L4e__eS4526;
      _M0L6_2atmpS4520 = _M0L6_2atmpS4523 * _M0L6_2atmpS4524;
      _M0L7gsyn__eS4522 = _M0L1pS1382->$14;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4521
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS4522, _M0L1iS1384);
      _M0L6_2atmpS4509 = _M0L6_2atmpS4520 * _M0L6_2atmpS4521;
      _M0L2giS4519 = _M0L1pS1382->$11;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4514 = _M0MPC15array5Array2atGfE(_M0L2giS4519, _M0L1iS1384);
      _M0L1vS4518 = _M0L1pS1382->$3;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4516 = _M0MPC15array5Array2atGfE(_M0L1vS4518, _M0L1iS1384);
      _M0L4e__iS4517 = _M0L1pS1382->$17;
      _M0L6_2atmpS4515 = _M0L6_2atmpS4516 - _M0L4e__iS4517;
      _M0L6_2atmpS4511 = _M0L6_2atmpS4514 * _M0L6_2atmpS4515;
      _M0L7gsyn__iS4513 = _M0L1pS1382->$15;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4512
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS4513, _M0L1iS1384);
      _M0L6_2atmpS4510 = _M0L6_2atmpS4511 * _M0L6_2atmpS4512;
      _M0L6_2atmpS4508 = _M0L6_2atmpS4509 + _M0L6_2atmpS4510;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS4507, _M0L1iS1384, _M0L6_2atmpS4508);
      _M0L6_2atmpS4529 = _M0L1iS1384 + 1;
      _M0L1iS1384 = _M0L6_2atmpS4529;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1371,
  float _M0L2dtS1376
) {
  int32_t _M0L1nS1370;
  float _M0L6tau__eS1372;
  float _M0L6tau__iS1373;
  int32_t _M0L7_2abindS1374;
  int32_t _M0L1iS1375;
  int32_t _M0L7_2abindS1378;
  int32_t _M0L1iS1379;
  #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1370 = _M0L1pS1371->$2;
  _M0L6tau__eS1372 = _M0L1pS1371->$18;
  _M0L6tau__iS1373 = _M0L1pS1371->$19;
  _M0L7_2abindS1374 = 0;
  _M0L1iS1375 = _M0L7_2abindS1374;
  while (1) {
    if (_M0L1iS1375 < _M0L1nS1370) {
      struct _M0TPB5ArrayGfE* _M0L2geS4473 = _M0L1pS1371->$10;
      struct _M0TPB5ArrayGfE* _M0L2geS4478 = _M0L1pS1371->$10;
      float _M0L6_2atmpS4475;
      struct _M0TPB5ArrayGfE* _M0L3gluS4477;
      float _M0L6_2atmpS4476;
      float _M0L6_2atmpS4474;
      struct _M0TPB5ArrayGfE* _M0L2giS4479;
      struct _M0TPB5ArrayGfE* _M0L2giS4484;
      float _M0L6_2atmpS4481;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4483;
      float _M0L6_2atmpS4482;
      float _M0L6_2atmpS4480;
      struct _M0TPB5ArrayGfE* _M0L2geS4485;
      struct _M0TPB5ArrayGfE* _M0L2geS4493;
      float _M0L6_2atmpS4487;
      struct _M0TPB5ArrayGfE* _M0L2geS4492;
      float _M0L6_2atmpS4491;
      float _M0L6_2atmpS4490;
      float _M0L6_2atmpS4489;
      float _M0L6_2atmpS4488;
      float _M0L6_2atmpS4486;
      struct _M0TPB5ArrayGfE* _M0L2giS4494;
      struct _M0TPB5ArrayGfE* _M0L2giS4502;
      float _M0L6_2atmpS4496;
      struct _M0TPB5ArrayGfE* _M0L2giS4501;
      float _M0L6_2atmpS4500;
      float _M0L6_2atmpS4499;
      float _M0L6_2atmpS4498;
      float _M0L6_2atmpS4497;
      float _M0L6_2atmpS4495;
      int32_t _M0L6_2atmpS4503;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4475 = _M0MPC15array5Array2atGfE(_M0L2geS4478, _M0L1iS1375);
      _M0L3gluS4477 = _M0L1pS1371->$12;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4476
      = _M0MPC15array5Array2atGfE(_M0L3gluS4477, _M0L1iS1375);
      _M0L6_2atmpS4474 = _M0L6_2atmpS4475 + _M0L6_2atmpS4476;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4473, _M0L1iS1375, _M0L6_2atmpS4474);
      _M0L2giS4479 = _M0L1pS1371->$11;
      _M0L2giS4484 = _M0L1pS1371->$11;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4481 = _M0MPC15array5Array2atGfE(_M0L2giS4484, _M0L1iS1375);
      _M0L4gabaS4483 = _M0L1pS1371->$13;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4482
      = _M0MPC15array5Array2atGfE(_M0L4gabaS4483, _M0L1iS1375);
      _M0L6_2atmpS4480 = _M0L6_2atmpS4481 + _M0L6_2atmpS4482;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4479, _M0L1iS1375, _M0L6_2atmpS4480);
      _M0L2geS4485 = _M0L1pS1371->$10;
      _M0L2geS4493 = _M0L1pS1371->$10;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4487 = _M0MPC15array5Array2atGfE(_M0L2geS4493, _M0L1iS1375);
      _M0L2geS4492 = _M0L1pS1371->$10;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4491 = _M0MPC15array5Array2atGfE(_M0L2geS4492, _M0L1iS1375);
      _M0L6_2atmpS4490 = -_M0L6_2atmpS4491;
      _M0L6_2atmpS4489 = _M0L6_2atmpS4490 / _M0L6tau__eS1372;
      _M0L6_2atmpS4488 = _M0L2dtS1376 * _M0L6_2atmpS4489;
      _M0L6_2atmpS4486 = _M0L6_2atmpS4487 + _M0L6_2atmpS4488;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4485, _M0L1iS1375, _M0L6_2atmpS4486);
      _M0L2giS4494 = _M0L1pS1371->$11;
      _M0L2giS4502 = _M0L1pS1371->$11;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4496 = _M0MPC15array5Array2atGfE(_M0L2giS4502, _M0L1iS1375);
      _M0L2giS4501 = _M0L1pS1371->$11;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4500 = _M0MPC15array5Array2atGfE(_M0L2giS4501, _M0L1iS1375);
      _M0L6_2atmpS4499 = -_M0L6_2atmpS4500;
      _M0L6_2atmpS4498 = _M0L6_2atmpS4499 / _M0L6tau__iS1373;
      _M0L6_2atmpS4497 = _M0L2dtS1376 * _M0L6_2atmpS4498;
      _M0L6_2atmpS4495 = _M0L6_2atmpS4496 + _M0L6_2atmpS4497;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4494, _M0L1iS1375, _M0L6_2atmpS4495);
      _M0L6_2atmpS4503 = _M0L1iS1375 + 1;
      _M0L1iS1375 = _M0L6_2atmpS4503;
      continue;
    }
    break;
  }
  _M0L7_2abindS1378 = 0;
  _M0L1iS1379 = _M0L7_2abindS1378;
  while (1) {
    if (_M0L1iS1379 < _M0L1nS1370) {
      struct _M0TPB5ArrayGfE* _M0L3gluS4504 = _M0L1pS1371->$12;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4505;
      int32_t _M0L6_2atmpS4506;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS4504, _M0L1iS1379, 0x0p+0f);
      _M0L4gabaS4505 = _M0L1pS1371->$13;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS4505, _M0L1iS1379, 0x0p+0f);
      _M0L6_2atmpS4506 = _M0L1iS1379 + 1;
      _M0L1iS1379 = _M0L6_2atmpS4506;
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
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1349,
  float _M0L2dtS1364
) {
  int32_t _M0L1nS1348;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L3p__S1350;
  float _M0L2tmS1351;
  float _M0L2vtS1352;
  float _M0L2vrS1353;
  float _M0L2elS1354;
  float _M0L1rS1355;
  float _M0L9dt__slopeS1356;
  float _M0L2twS1357;
  float _M0L1aS1358;
  float _M0L1bS1359;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4472;
  float _M0L2atS1360;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4471;
  float _M0L6tau__aS1361;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4470;
  float _M0L11tabs__constS1362;
  float _M0L6_2atmpS4469;
  int32_t _M0L11tabs__stepsS1363;
  int32_t _M0L7_2abindS1365;
  int32_t _M0L1iS1366;
  #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1348 = _M0L1pS1349->$2;
  _M0L3p__S1350 = _M0L1pS1349->$0;
  _M0L2tmS1351 = _M0L3p__S1350->$5;
  _M0L2vtS1352 = _M0L3p__S1350->$2;
  _M0L2vrS1353 = _M0L3p__S1350->$3;
  _M0L2elS1354 = _M0L3p__S1350->$4;
  _M0L1rS1355 = _M0L3p__S1350->$6;
  _M0L9dt__slopeS1356 = _M0L3p__S1350->$7;
  _M0L2twS1357 = _M0L3p__S1350->$8;
  _M0L1aS1358 = _M0L3p__S1350->$9;
  _M0L1bS1359 = _M0L3p__S1350->$10;
  _M0L5spikeS4472 = _M0L1pS1349->$1;
  _M0L2atS1360 = _M0L5spikeS4472->$0;
  _M0L5spikeS4471 = _M0L1pS1349->$1;
  _M0L6tau__aS1361 = _M0L5spikeS4471->$1;
  _M0L5spikeS4470 = _M0L1pS1349->$1;
  _M0L11tabs__constS1362 = _M0L5spikeS4470->$3;
  _M0L6_2atmpS4469 = _M0L11tabs__constS1362 / _M0L2dtS1364;
  #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L11tabs__stepsS1363 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4469);
  _M0L7_2abindS1365 = 0;
  _M0L1iS1366 = _M0L7_2abindS1365;
  while (1) {
    if (_M0L1iS1366 < _M0L1nS1348) {
      struct _M0TPB5ArrayGfE* _M0L1vS4382 = _M0L1pS1349->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS4384 = _M0L1pS1349->$5;
      float _M0L6_2atmpS4383;
      struct _M0TPB5ArrayGbE* _M0L4fireS4386;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4387;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4390;
      int32_t _M0L6_2atmpS4389;
      int32_t _M0L6_2atmpS4388;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4392;
      int32_t _M0L6_2atmpS4391;
      struct _M0TPB5ArrayGfE* _M0L1wS4393;
      struct _M0TPB5ArrayGfE* _M0L1wS4405;
      float _M0L6_2atmpS4395;
      struct _M0TPB5ArrayGfE* _M0L1vS4404;
      float _M0L6_2atmpS4403;
      float _M0L6_2atmpS4402;
      float _M0L6_2atmpS4399;
      struct _M0TPB5ArrayGfE* _M0L1wS4401;
      float _M0L6_2atmpS4400;
      float _M0L6_2atmpS4398;
      float _M0L6_2atmpS4397;
      float _M0L6_2atmpS4396;
      float _M0L6_2atmpS4394;
      float _M0L9exp__termS1369;
      struct _M0TPB5ArrayGfE* _M0L1vS4406;
      struct _M0TPB5ArrayGfE* _M0L1vS4428;
      float _M0L6_2atmpS4408;
      struct _M0TPB5ArrayGfE* _M0L1vS4427;
      float _M0L6_2atmpS4426;
      float _M0L6_2atmpS4425;
      float _M0L6_2atmpS4424;
      float _M0L6_2atmpS4420;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4423;
      float _M0L6_2atmpS4422;
      float _M0L6_2atmpS4421;
      float _M0L6_2atmpS4416;
      struct _M0TPB5ArrayGfE* _M0L1wS4419;
      float _M0L6_2atmpS4418;
      float _M0L6_2atmpS4417;
      float _M0L6_2atmpS4412;
      struct _M0TPB5ArrayGfE* _M0L1iS4415;
      float _M0L6_2atmpS4414;
      float _M0L6_2atmpS4413;
      float _M0L6_2atmpS4411;
      float _M0L6_2atmpS4410;
      float _M0L6_2atmpS4409;
      float _M0L6_2atmpS4407;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4429;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4437;
      float _M0L6_2atmpS4431;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4436;
      float _M0L6_2atmpS4435;
      float _M0L6_2atmpS4434;
      float _M0L6_2atmpS4433;
      float _M0L6_2atmpS4432;
      float _M0L6_2atmpS4430;
      struct _M0TPB5ArrayGbE* _M0L4fireS4438;
      struct _M0TPB5ArrayGfE* _M0L1vS4441;
      float _M0L6_2atmpS4440;
      int32_t _M0L6_2atmpS4439;
      struct _M0TPB5ArrayGfE* _M0L1vS4442;
      struct _M0TPB5ArrayGbE* _M0L4fireS4444;
      float _M0L6_2atmpS4443;
      struct _M0TPB5ArrayGfE* _M0L1wS4446;
      struct _M0TPB5ArrayGbE* _M0L4fireS4448;
      float _M0L6_2atmpS4447;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4452;
      struct _M0TPB5ArrayGbE* _M0L4fireS4454;
      float _M0L6_2atmpS4453;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4458;
      struct _M0TPB5ArrayGbE* _M0L4fireS4460;
      int32_t _M0L6_2atmpS4459;
      int32_t _M0L6_2atmpS4381;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4384, _M0L1iS1366)) {
        _M0L6_2atmpS4383 = _M0L2vrS1353;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4385 = _M0L1pS1349->$3;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4383
        = _M0MPC15array5Array2atGfE(_M0L1vS4385, _M0L1iS1366);
      }
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4382, _M0L1iS1366, _M0L6_2atmpS4383);
      _M0L4fireS4386 = _M0L1pS1349->$5;
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4386, _M0L1iS1366, 0);
      _M0L4tabsS4387 = _M0L1pS1349->$7;
      _M0L4tabsS4390 = _M0L1pS1349->$7;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4389
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4390, _M0L1iS1366);
      _M0L6_2atmpS4388 = _M0L6_2atmpS4389 - 1;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4387, _M0L1iS1366, _M0L6_2atmpS4388);
      _M0L4tabsS4392 = _M0L1pS1349->$7;
      #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4391
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4392, _M0L1iS1366);
      if (_M0L6_2atmpS4391 > 0) {
        goto join_1367;
      }
      _M0L1wS4393 = _M0L1pS1349->$4;
      _M0L1wS4405 = _M0L1pS1349->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4395 = _M0MPC15array5Array2atGfE(_M0L1wS4405, _M0L1iS1366);
      _M0L1vS4404 = _M0L1pS1349->$3;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4403 = _M0MPC15array5Array2atGfE(_M0L1vS4404, _M0L1iS1366);
      _M0L6_2atmpS4402 = _M0L6_2atmpS4403 - _M0L2elS1354;
      _M0L6_2atmpS4399 = _M0L1aS1358 * _M0L6_2atmpS4402;
      _M0L1wS4401 = _M0L1pS1349->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4400 = _M0MPC15array5Array2atGfE(_M0L1wS4401, _M0L1iS1366);
      _M0L6_2atmpS4398 = _M0L6_2atmpS4399 - _M0L6_2atmpS4400;
      _M0L6_2atmpS4397 = _M0L2dtS1364 * _M0L6_2atmpS4398;
      _M0L6_2atmpS4396 = _M0L6_2atmpS4397 / _M0L2twS1357;
      _M0L6_2atmpS4394 = _M0L6_2atmpS4395 + _M0L6_2atmpS4396;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4393, _M0L1iS1366, _M0L6_2atmpS4394);
      if (_M0L9dt__slopeS1356 < 0x0p+0f) {
        _M0L9exp__termS1369 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4468 = _M0L1pS1349->$3;
        float _M0L6_2atmpS4465;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4467;
        float _M0L6_2atmpS4466;
        float _M0L6_2atmpS4464;
        float _M0L6_2atmpS4463;
        float _M0L6_2atmpS4462;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4465
        = _M0MPC15array5Array2atGfE(_M0L1vS4468, _M0L1iS1366);
        _M0L9thresholdS4467 = _M0L1pS1349->$6;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4466
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4467, _M0L1iS1366);
        _M0L6_2atmpS4464 = _M0L6_2atmpS4465 - _M0L6_2atmpS4466;
        _M0L6_2atmpS4463 = _M0L6_2atmpS4464 / _M0L9dt__slopeS1356;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4462 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4463);
        _M0L9exp__termS1369 = _M0L9dt__slopeS1356 * _M0L6_2atmpS4462;
      }
      _M0L1vS4406 = _M0L1pS1349->$3;
      _M0L1vS4428 = _M0L1pS1349->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4408 = _M0MPC15array5Array2atGfE(_M0L1vS4428, _M0L1iS1366);
      _M0L1vS4427 = _M0L1pS1349->$3;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4426 = _M0MPC15array5Array2atGfE(_M0L1vS4427, _M0L1iS1366);
      _M0L6_2atmpS4425 = _M0L6_2atmpS4426 - _M0L2elS1354;
      _M0L6_2atmpS4424 = -_M0L6_2atmpS4425;
      _M0L6_2atmpS4420 = _M0L6_2atmpS4424 + _M0L9exp__termS1369;
      _M0L9syn__currS4423 = _M0L1pS1349->$9;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4422
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS4423, _M0L1iS1366);
      _M0L6_2atmpS4421 = _M0L1rS1355 * _M0L6_2atmpS4422;
      _M0L6_2atmpS4416 = _M0L6_2atmpS4420 - _M0L6_2atmpS4421;
      _M0L1wS4419 = _M0L1pS1349->$4;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4418 = _M0MPC15array5Array2atGfE(_M0L1wS4419, _M0L1iS1366);
      _M0L6_2atmpS4417 = _M0L1rS1355 * _M0L6_2atmpS4418;
      _M0L6_2atmpS4412 = _M0L6_2atmpS4416 - _M0L6_2atmpS4417;
      _M0L1iS4415 = _M0L1pS1349->$8;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4414 = _M0MPC15array5Array2atGfE(_M0L1iS4415, _M0L1iS1366);
      _M0L6_2atmpS4413 = _M0L1rS1355 * _M0L6_2atmpS4414;
      _M0L6_2atmpS4411 = _M0L6_2atmpS4412 + _M0L6_2atmpS4413;
      _M0L6_2atmpS4410 = _M0L2dtS1364 * _M0L6_2atmpS4411;
      _M0L6_2atmpS4409 = _M0L6_2atmpS4410 / _M0L2tmS1351;
      _M0L6_2atmpS4407 = _M0L6_2atmpS4408 + _M0L6_2atmpS4409;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4406, _M0L1iS1366, _M0L6_2atmpS4407);
      _M0L9thresholdS4429 = _M0L1pS1349->$6;
      _M0L9thresholdS4437 = _M0L1pS1349->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4431
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4437, _M0L1iS1366);
      _M0L9thresholdS4436 = _M0L1pS1349->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4435
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4436, _M0L1iS1366);
      _M0L6_2atmpS4434 = _M0L2vtS1352 - _M0L6_2atmpS4435;
      _M0L6_2atmpS4433 = _M0L2dtS1364 * _M0L6_2atmpS4434;
      _M0L6_2atmpS4432 = _M0L6_2atmpS4433 / _M0L6tau__aS1361;
      _M0L6_2atmpS4430 = _M0L6_2atmpS4431 + _M0L6_2atmpS4432;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4429, _M0L1iS1366, _M0L6_2atmpS4430);
      _M0L4fireS4438 = _M0L1pS1349->$5;
      _M0L1vS4441 = _M0L1pS1349->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4440 = _M0MPC15array5Array2atGfE(_M0L1vS4441, _M0L1iS1366);
      _M0L6_2atmpS4439 = _M0L6_2atmpS4440 >= 0x0p+0f;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4438, _M0L1iS1366, _M0L6_2atmpS4439);
      _M0L1vS4442 = _M0L1pS1349->$3;
      _M0L4fireS4444 = _M0L1pS1349->$5;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4444, _M0L1iS1366)) {
        _M0L6_2atmpS4443 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4445 = _M0L1pS1349->$3;
        #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4443
        = _M0MPC15array5Array2atGfE(_M0L1vS4445, _M0L1iS1366);
      }
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4442, _M0L1iS1366, _M0L6_2atmpS4443);
      _M0L1wS4446 = _M0L1pS1349->$4;
      _M0L4fireS4448 = _M0L1pS1349->$5;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4448, _M0L1iS1366)) {
        struct _M0TPB5ArrayGfE* _M0L1wS4450 = _M0L1pS1349->$4;
        float _M0L6_2atmpS4449;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4449
        = _M0MPC15array5Array2atGfE(_M0L1wS4450, _M0L1iS1366);
        _M0L6_2atmpS4447 = _M0L6_2atmpS4449 + _M0L1bS1359;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS4451 = _M0L1pS1349->$4;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4447
        = _M0MPC15array5Array2atGfE(_M0L1wS4451, _M0L1iS1366);
      }
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4446, _M0L1iS1366, _M0L6_2atmpS4447);
      _M0L9thresholdS4452 = _M0L1pS1349->$6;
      _M0L4fireS4454 = _M0L1pS1349->$5;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4454, _M0L1iS1366)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4456 = _M0L1pS1349->$6;
        float _M0L6_2atmpS4455;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4455
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4456, _M0L1iS1366);
        _M0L6_2atmpS4453 = _M0L6_2atmpS4455 + _M0L2atS1360;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4457 = _M0L1pS1349->$6;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4453
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4457, _M0L1iS1366);
      }
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4452, _M0L1iS1366, _M0L6_2atmpS4453);
      _M0L4tabsS4458 = _M0L1pS1349->$7;
      _M0L4fireS4460 = _M0L1pS1349->$5;
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4460, _M0L1iS1366)) {
        _M0L6_2atmpS4459 = _M0L11tabs__stepsS1363;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4461 = _M0L1pS1349->$7;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4459
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4461, _M0L1iS1366);
      }
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4458, _M0L1iS1366, _M0L6_2atmpS4459);
      goto join_1367;
      goto joinlet_5758;
      join_1367:;
      _M0L6_2atmpS4381 = _M0L1iS1366 + 1;
      _M0L1iS1366 = _M0L6_2atmpS4381;
      continue;
      joinlet_5758:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23adex__synaptic__current(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1344
) {
  int32_t _M0L1nS1343;
  int32_t _M0L7_2abindS1345;
  int32_t _M0L1iS1346;
  #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1343 = _M0L1pS1344->$2;
  _M0L7_2abindS1345 = 0;
  _M0L1iS1346 = _M0L7_2abindS1345;
  while (1) {
    if (_M0L1iS1346 < _M0L1nS1343) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4358 = _M0L1pS1344->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS4379 = _M0L1pS1344->$10;
      float _M0L6_2atmpS4374;
      struct _M0TPB5ArrayGfE* _M0L1vS4378;
      float _M0L6_2atmpS4376;
      float _M0L4e__eS4377;
      float _M0L6_2atmpS4375;
      float _M0L6_2atmpS4371;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS4373;
      float _M0L6_2atmpS4372;
      float _M0L6_2atmpS4360;
      struct _M0TPB5ArrayGfE* _M0L2giS4370;
      float _M0L6_2atmpS4365;
      struct _M0TPB5ArrayGfE* _M0L1vS4369;
      float _M0L6_2atmpS4367;
      float _M0L4e__iS4368;
      float _M0L6_2atmpS4366;
      float _M0L6_2atmpS4362;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS4364;
      float _M0L6_2atmpS4363;
      float _M0L6_2atmpS4361;
      float _M0L6_2atmpS4359;
      int32_t _M0L6_2atmpS4380;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4374 = _M0MPC15array5Array2atGfE(_M0L2geS4379, _M0L1iS1346);
      _M0L1vS4378 = _M0L1pS1344->$3;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4376 = _M0MPC15array5Array2atGfE(_M0L1vS4378, _M0L1iS1346);
      _M0L4e__eS4377 = _M0L1pS1344->$18;
      _M0L6_2atmpS4375 = _M0L6_2atmpS4376 - _M0L4e__eS4377;
      _M0L6_2atmpS4371 = _M0L6_2atmpS4374 * _M0L6_2atmpS4375;
      _M0L7gsyn__eS4373 = _M0L1pS1344->$16;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4372
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS4373, _M0L1iS1346);
      _M0L6_2atmpS4360 = _M0L6_2atmpS4371 * _M0L6_2atmpS4372;
      _M0L2giS4370 = _M0L1pS1344->$11;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4365 = _M0MPC15array5Array2atGfE(_M0L2giS4370, _M0L1iS1346);
      _M0L1vS4369 = _M0L1pS1344->$3;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4367 = _M0MPC15array5Array2atGfE(_M0L1vS4369, _M0L1iS1346);
      _M0L4e__iS4368 = _M0L1pS1344->$19;
      _M0L6_2atmpS4366 = _M0L6_2atmpS4367 - _M0L4e__iS4368;
      _M0L6_2atmpS4362 = _M0L6_2atmpS4365 * _M0L6_2atmpS4366;
      _M0L7gsyn__iS4364 = _M0L1pS1344->$17;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4363
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS4364, _M0L1iS1346);
      _M0L6_2atmpS4361 = _M0L6_2atmpS4362 * _M0L6_2atmpS4363;
      _M0L6_2atmpS4359 = _M0L6_2atmpS4360 + _M0L6_2atmpS4361;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS4358, _M0L1iS1346, _M0L6_2atmpS4359);
      _M0L6_2atmpS4380 = _M0L1iS1346 + 1;
      _M0L1iS1346 = _M0L6_2atmpS4380;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20adex__step__synapses(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1335,
  float _M0L2dtS1338
) {
  int32_t _M0L1nS1334;
  int32_t _M0L7_2abindS1336;
  int32_t _M0L1iS1337;
  int32_t _M0L7_2abindS1340;
  int32_t _M0L1iS1341;
  #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1334 = _M0L1pS1335->$2;
  _M0L7_2abindS1336 = 0;
  _M0L1iS1337 = _M0L7_2abindS1336;
  while (1) {
    if (_M0L1iS1337 < _M0L1nS1334) {
      struct _M0TPB5ArrayGfE* _M0L2heS4296 = _M0L1pS1335->$12;
      struct _M0TPB5ArrayGfE* _M0L2heS4301 = _M0L1pS1335->$12;
      float _M0L6_2atmpS4298;
      struct _M0TPB5ArrayGfE* _M0L3gluS4300;
      float _M0L6_2atmpS4299;
      float _M0L6_2atmpS4297;
      struct _M0TPB5ArrayGfE* _M0L2hiS4302;
      struct _M0TPB5ArrayGfE* _M0L2hiS4307;
      float _M0L6_2atmpS4304;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4306;
      float _M0L6_2atmpS4305;
      float _M0L6_2atmpS4303;
      struct _M0TPB5ArrayGfE* _M0L2geS4308;
      struct _M0TPB5ArrayGfE* _M0L2geS4320;
      float _M0L6_2atmpS4310;
      struct _M0TPB5ArrayGfE* _M0L2geS4319;
      float _M0L6_2atmpS4318;
      float _M0L6_2atmpS4316;
      float _M0L3tdeS4317;
      float _M0L6_2atmpS4313;
      struct _M0TPB5ArrayGfE* _M0L2heS4315;
      float _M0L6_2atmpS4314;
      float _M0L6_2atmpS4312;
      float _M0L6_2atmpS4311;
      float _M0L6_2atmpS4309;
      struct _M0TPB5ArrayGfE* _M0L2heS4321;
      struct _M0TPB5ArrayGfE* _M0L2heS4330;
      float _M0L6_2atmpS4323;
      struct _M0TPB5ArrayGfE* _M0L2heS4329;
      float _M0L6_2atmpS4328;
      float _M0L6_2atmpS4326;
      float _M0L3treS4327;
      float _M0L6_2atmpS4325;
      float _M0L6_2atmpS4324;
      float _M0L6_2atmpS4322;
      struct _M0TPB5ArrayGfE* _M0L2giS4331;
      struct _M0TPB5ArrayGfE* _M0L2giS4343;
      float _M0L6_2atmpS4333;
      struct _M0TPB5ArrayGfE* _M0L2giS4342;
      float _M0L6_2atmpS4341;
      float _M0L6_2atmpS4339;
      float _M0L3tdiS4340;
      float _M0L6_2atmpS4336;
      struct _M0TPB5ArrayGfE* _M0L2hiS4338;
      float _M0L6_2atmpS4337;
      float _M0L6_2atmpS4335;
      float _M0L6_2atmpS4334;
      float _M0L6_2atmpS4332;
      struct _M0TPB5ArrayGfE* _M0L2hiS4344;
      struct _M0TPB5ArrayGfE* _M0L2hiS4353;
      float _M0L6_2atmpS4346;
      struct _M0TPB5ArrayGfE* _M0L2hiS4352;
      float _M0L6_2atmpS4351;
      float _M0L6_2atmpS4349;
      float _M0L3triS4350;
      float _M0L6_2atmpS4348;
      float _M0L6_2atmpS4347;
      float _M0L6_2atmpS4345;
      int32_t _M0L6_2atmpS4354;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4298 = _M0MPC15array5Array2atGfE(_M0L2heS4301, _M0L1iS1337);
      _M0L3gluS4300 = _M0L1pS1335->$14;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4299
      = _M0MPC15array5Array2atGfE(_M0L3gluS4300, _M0L1iS1337);
      _M0L6_2atmpS4297 = _M0L6_2atmpS4298 + _M0L6_2atmpS4299;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4296, _M0L1iS1337, _M0L6_2atmpS4297);
      _M0L2hiS4302 = _M0L1pS1335->$13;
      _M0L2hiS4307 = _M0L1pS1335->$13;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4304 = _M0MPC15array5Array2atGfE(_M0L2hiS4307, _M0L1iS1337);
      _M0L4gabaS4306 = _M0L1pS1335->$15;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4305
      = _M0MPC15array5Array2atGfE(_M0L4gabaS4306, _M0L1iS1337);
      _M0L6_2atmpS4303 = _M0L6_2atmpS4304 + _M0L6_2atmpS4305;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4302, _M0L1iS1337, _M0L6_2atmpS4303);
      _M0L2geS4308 = _M0L1pS1335->$10;
      _M0L2geS4320 = _M0L1pS1335->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4310 = _M0MPC15array5Array2atGfE(_M0L2geS4320, _M0L1iS1337);
      _M0L2geS4319 = _M0L1pS1335->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4318 = _M0MPC15array5Array2atGfE(_M0L2geS4319, _M0L1iS1337);
      _M0L6_2atmpS4316 = -_M0L6_2atmpS4318;
      _M0L3tdeS4317 = _M0L1pS1335->$21;
      _M0L6_2atmpS4313 = _M0L6_2atmpS4316 / _M0L3tdeS4317;
      _M0L2heS4315 = _M0L1pS1335->$12;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4314 = _M0MPC15array5Array2atGfE(_M0L2heS4315, _M0L1iS1337);
      _M0L6_2atmpS4312 = _M0L6_2atmpS4313 + _M0L6_2atmpS4314;
      _M0L6_2atmpS4311 = _M0L2dtS1338 * _M0L6_2atmpS4312;
      _M0L6_2atmpS4309 = _M0L6_2atmpS4310 + _M0L6_2atmpS4311;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4308, _M0L1iS1337, _M0L6_2atmpS4309);
      _M0L2heS4321 = _M0L1pS1335->$12;
      _M0L2heS4330 = _M0L1pS1335->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4323 = _M0MPC15array5Array2atGfE(_M0L2heS4330, _M0L1iS1337);
      _M0L2heS4329 = _M0L1pS1335->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4328 = _M0MPC15array5Array2atGfE(_M0L2heS4329, _M0L1iS1337);
      _M0L6_2atmpS4326 = -_M0L6_2atmpS4328;
      _M0L3treS4327 = _M0L1pS1335->$20;
      _M0L6_2atmpS4325 = _M0L6_2atmpS4326 / _M0L3treS4327;
      _M0L6_2atmpS4324 = _M0L2dtS1338 * _M0L6_2atmpS4325;
      _M0L6_2atmpS4322 = _M0L6_2atmpS4323 + _M0L6_2atmpS4324;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4321, _M0L1iS1337, _M0L6_2atmpS4322);
      _M0L2giS4331 = _M0L1pS1335->$11;
      _M0L2giS4343 = _M0L1pS1335->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4333 = _M0MPC15array5Array2atGfE(_M0L2giS4343, _M0L1iS1337);
      _M0L2giS4342 = _M0L1pS1335->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4341 = _M0MPC15array5Array2atGfE(_M0L2giS4342, _M0L1iS1337);
      _M0L6_2atmpS4339 = -_M0L6_2atmpS4341;
      _M0L3tdiS4340 = _M0L1pS1335->$23;
      _M0L6_2atmpS4336 = _M0L6_2atmpS4339 / _M0L3tdiS4340;
      _M0L2hiS4338 = _M0L1pS1335->$13;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4337 = _M0MPC15array5Array2atGfE(_M0L2hiS4338, _M0L1iS1337);
      _M0L6_2atmpS4335 = _M0L6_2atmpS4336 + _M0L6_2atmpS4337;
      _M0L6_2atmpS4334 = _M0L2dtS1338 * _M0L6_2atmpS4335;
      _M0L6_2atmpS4332 = _M0L6_2atmpS4333 + _M0L6_2atmpS4334;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4331, _M0L1iS1337, _M0L6_2atmpS4332);
      _M0L2hiS4344 = _M0L1pS1335->$13;
      _M0L2hiS4353 = _M0L1pS1335->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4346 = _M0MPC15array5Array2atGfE(_M0L2hiS4353, _M0L1iS1337);
      _M0L2hiS4352 = _M0L1pS1335->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4351 = _M0MPC15array5Array2atGfE(_M0L2hiS4352, _M0L1iS1337);
      _M0L6_2atmpS4349 = -_M0L6_2atmpS4351;
      _M0L3triS4350 = _M0L1pS1335->$22;
      _M0L6_2atmpS4348 = _M0L6_2atmpS4349 / _M0L3triS4350;
      _M0L6_2atmpS4347 = _M0L2dtS1338 * _M0L6_2atmpS4348;
      _M0L6_2atmpS4345 = _M0L6_2atmpS4346 + _M0L6_2atmpS4347;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4344, _M0L1iS1337, _M0L6_2atmpS4345);
      _M0L6_2atmpS4354 = _M0L1iS1337 + 1;
      _M0L1iS1337 = _M0L6_2atmpS4354;
      continue;
    }
    break;
  }
  _M0L7_2abindS1340 = 0;
  _M0L1iS1341 = _M0L7_2abindS1340;
  while (1) {
    if (_M0L1iS1341 < _M0L1nS1334) {
      struct _M0TPB5ArrayGfE* _M0L3gluS4355 = _M0L1pS1335->$14;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4356;
      int32_t _M0L6_2atmpS4357;
      #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS4355, _M0L1iS1341, 0x0p+0f);
      _M0L4gabaS4356 = _M0L1pS1335->$15;
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS4356, _M0L1iS1341, 0x0p+0f);
      _M0L6_2atmpS4357 = _M0L1iS1341 + 1;
      _M0L1iS1341 = _M0L6_2atmpS4357;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16forward__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1310,
  float _M0L6t__nowS1321
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS4295;
  int32_t _M0L6_2atmpS4294;
  int32_t _M0L10use__delayS1309;
  struct _M0TPB5ArrayGfE* _M0L3rhoS4293;
  int32_t _M0L6_2atmpS4292;
  int32_t _M0L8use__rhoS1311;
  #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6delaysS4295 = _M0L1cS1310->$5;
  #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS4294 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS4295);
  _M0L10use__delayS1309 = _M0L6_2atmpS4294 > 0;
  _M0L3rhoS4293 = _M0L1cS1310->$6;
  #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS4292 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS4293);
  _M0L8use__rhoS1311 = _M0L6_2atmpS4292 > 0;
  if (_M0L10use__delayS1309) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4255 = _M0L1cS1310->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS4254 = _M0L3preS4255->$5;
    int32_t _M0L6n__preS1312;
    struct _M0TPB8MutLocalGiE* _M0L1jS1313;
    #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6n__preS1312 = _M0MPC15array5Array6lengthGbE(_M0L4fireS4254);
    _M0L1jS1313
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS1313)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS1313->$0 = 0;
    while (1) {
      int32_t _M0L3valS4223 = _M0L1jS1313->$0;
      if (_M0L3valS4223 < _M0L6n__preS1312) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4226 = _M0L1cS1310->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS4224 = _M0L3preS4226->$5;
        int32_t _M0L3valS4225 = _M0L1jS1313->$0;
        int32_t _M0L3valS4253;
        int32_t _M0L6_2atmpS4252;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS4224, _M0L3valS4225)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4251 =
            _M0L1cS1310->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS4249 = _M0L6matrixS4251->$2;
          int32_t _M0L3valS4250 = _M0L1jS1313->$0;
          int32_t _M0L5startS1314;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4248;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS4245;
          int32_t _M0L3valS4247;
          int32_t _M0L6_2atmpS4246;
          int32_t _M0L3endS1315;
          struct _M0TPB8MutLocalGiE* _M0L1sS1316;
          #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L5startS1314
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS4249, _M0L3valS4250);
          _M0L6matrixS4248 = _M0L1cS1310->$4;
          _M0L6rowptrS4245 = _M0L6matrixS4248->$2;
          _M0L3valS4247 = _M0L1jS1313->$0;
          _M0L6_2atmpS4246 = _M0L3valS4247 + 1;
          #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L3endS1315
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS4245, _M0L6_2atmpS4246);
          _M0L1sS1316
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS1316)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS1316->$0 = _M0L5startS1314;
          while (1) {
            int32_t _M0L3valS4227 = _M0L1sS1316->$0;
            if (_M0L3valS4227 < _M0L3endS1315) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4244 =
                _M0L1cS1310->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS4242 = _M0L6matrixS4244->$3;
              int32_t _M0L3valS4243 = _M0L1sS1316->$0;
              int32_t _M0L9post__idxS1317;
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4241;
              struct _M0TPB5ArrayGfE* _M0L4valsS4239;
              int32_t _M0L3valS4240;
              float _M0L1wS1318;
              struct _M0TPB5ArrayGfE* _M0L6delaysS4237;
              int32_t _M0L3valS4238;
              float _M0L1dS1319;
              float _M0L9w__scaledS1320;
              int32_t _M0L3valS4233;
              int32_t _M0L6_2atmpS4232;
              #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L9post__idxS1317
              = _M0MPC15array5Array2atGiE(_M0L6colptrS4242, _M0L3valS4243);
              _M0L6matrixS4241 = _M0L1cS1310->$4;
              _M0L4valsS4239 = _M0L6matrixS4241->$4;
              _M0L3valS4240 = _M0L1sS1316->$0;
              #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1wS1318
              = _M0MPC15array5Array2atGfE(_M0L4valsS4239, _M0L3valS4240);
              _M0L6delaysS4237 = _M0L1cS1310->$5;
              _M0L3valS4238 = _M0L1sS1316->$0;
              #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1dS1319
              = _M0MPC15array5Array2atGfE(_M0L6delaysS4237, _M0L3valS4238);
              if (_M0L8use__rhoS1311) {
                struct _M0TPB5ArrayGfE* _M0L3rhoS4235 = _M0L1cS1310->$6;
                int32_t _M0L3valS4236 = _M0L1sS1316->$0;
                float _M0L6_2atmpS4234;
                #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4234
                = _M0MPC15array5Array2atGfE(_M0L3rhoS4235, _M0L3valS4236);
                _M0L9w__scaledS1320 = _M0L1wS1318 * _M0L6_2atmpS4234;
              } else {
                _M0L9w__scaledS1320 = _M0L1wS1318;
              }
              if (_M0L1dS1319 == 0x0p+0f) {
                #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS1310, _M0L9post__idxS1317, _M0L9w__scaledS1320);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS4228 =
                  _M0L1cS1310->$7;
                float _M0L6_2atmpS4229 = _M0L6t__nowS1321 + _M0L1dS1319;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS4230;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4231;
                #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS4228, _M0L6_2atmpS4229);
                _M0L14pending__postsS4230 = _M0L1cS1310->$8;
                #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS4230, _M0L9post__idxS1317);
                _M0L16pending__weightsS4231 = _M0L1cS1310->$9;
                #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS4231, _M0L9w__scaledS1320);
              }
              _M0L3valS4233 = _M0L1sS1316->$0;
              _M0L6_2atmpS4232 = _M0L3valS4233 + 1;
              _M0L1sS1316->$0 = _M0L6_2atmpS4232;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1sS1316);
            }
            break;
          }
        }
        _M0L3valS4253 = _M0L1jS1313->$0;
        _M0L6_2atmpS4252 = _M0L3valS4253 + 1;
        _M0L1jS1313->$0 = _M0L6_2atmpS4252;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1jS1313);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS4289 = _M0L1cS1310->$2;
    struct _M0TPB5ArrayGfE* _M0L6targetS1324;
    #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    if (
      _M0L3symS4289 == (moonbit_string_t)moonbit_string_literal_9.data
      || Moonbit_array_length(_M0L3symS4289)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
         && 0
            == memcmp(_M0L3symS4289, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS4289) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4290 = _M0L1cS1310->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS5456 = _M0L4postS4290->$13;
      moonbit_incref_cycle_free(_M0L8_2afieldS5456);
      _M0L6targetS1324 = _M0L8_2afieldS5456;
    } else {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4291 = _M0L1cS1310->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS5457 = _M0L4postS4291->$14;
      moonbit_incref_cycle_free(_M0L8_2afieldS5457);
      _M0L6targetS1324 = _M0L8_2afieldS5457;
    }
    if (_M0L8use__rhoS1311) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4285 = _M0L1cS1310->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS4284 = _M0L3preS4285->$5;
      int32_t _M0L6n__preS1325;
      struct _M0TPB8MutLocalGiE* _M0L1jS1326;
      #line 283 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6n__preS1325 = _M0MPC15array5Array6lengthGbE(_M0L4fireS4284);
      _M0L1jS1326
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS1326)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS1326->$0 = 0;
      while (1) {
        int32_t _M0L3valS4256 = _M0L1jS1326->$0;
        if (_M0L3valS4256 < _M0L6n__preS1325) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4259 = _M0L1cS1310->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS4257 = _M0L3preS4259->$5;
          int32_t _M0L3valS4258 = _M0L1jS1326->$0;
          int32_t _M0L3valS4283;
          int32_t _M0L6_2atmpS4282;
          #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS4257, _M0L3valS4258)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4281 =
              _M0L1cS1310->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS4279 = _M0L6matrixS4281->$2;
            int32_t _M0L3valS4280 = _M0L1jS1326->$0;
            int32_t _M0L5startS1327;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4278;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS4275;
            int32_t _M0L3valS4277;
            int32_t _M0L6_2atmpS4276;
            int32_t _M0L3endS1328;
            struct _M0TPB8MutLocalGiE* _M0L1sS1329;
            #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L5startS1327
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS4279, _M0L3valS4280);
            _M0L6matrixS4278 = _M0L1cS1310->$4;
            _M0L6rowptrS4275 = _M0L6matrixS4278->$2;
            _M0L3valS4277 = _M0L1jS1326->$0;
            _M0L6_2atmpS4276 = _M0L3valS4277 + 1;
            #line 288 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L3endS1328
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS4275, _M0L6_2atmpS4276);
            _M0L1sS1329
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS1329)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS1329->$0 = _M0L5startS1327;
            while (1) {
              int32_t _M0L3valS4260 = _M0L1sS1329->$0;
              if (_M0L3valS4260 < _M0L3endS1328) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4274 =
                  _M0L1cS1310->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS4272 =
                  _M0L6matrixS4274->$3;
                int32_t _M0L3valS4273 = _M0L1sS1329->$0;
                int32_t _M0L9post__idxS1330;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4271;
                struct _M0TPB5ArrayGfE* _M0L4valsS4269;
                int32_t _M0L3valS4270;
                float _M0L6_2atmpS4265;
                struct _M0TPB5ArrayGfE* _M0L3rhoS4267;
                int32_t _M0L3valS4268;
                float _M0L6_2atmpS4266;
                float _M0L9w__scaledS1331;
                float _M0L6_2atmpS4262;
                float _M0L6_2atmpS4261;
                int32_t _M0L3valS4264;
                int32_t _M0L6_2atmpS4263;
                #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L9post__idxS1330
                = _M0MPC15array5Array2atGiE(_M0L6colptrS4272, _M0L3valS4273);
                _M0L6matrixS4271 = _M0L1cS1310->$4;
                _M0L4valsS4269 = _M0L6matrixS4271->$4;
                _M0L3valS4270 = _M0L1sS1329->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4265
                = _M0MPC15array5Array2atGfE(_M0L4valsS4269, _M0L3valS4270);
                _M0L3rhoS4267 = _M0L1cS1310->$6;
                _M0L3valS4268 = _M0L1sS1329->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4266
                = _M0MPC15array5Array2atGfE(_M0L3rhoS4267, _M0L3valS4268);
                _M0L9w__scaledS1331 = _M0L6_2atmpS4265 * _M0L6_2atmpS4266;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4262
                = _M0MPC15array5Array2atGfE(_M0L6targetS1324, _M0L9post__idxS1330);
                _M0L6_2atmpS4261 = _M0L6_2atmpS4262 + _M0L9w__scaledS1331;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array3setGfE(_M0L6targetS1324, _M0L9post__idxS1330, _M0L6_2atmpS4261);
                _M0L3valS4264 = _M0L1sS1329->$0;
                _M0L6_2atmpS4263 = _M0L3valS4264 + 1;
                _M0L1sS1329->$0 = _M0L6_2atmpS4263;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS1329);
              }
              break;
            }
          }
          _M0L3valS4283 = _M0L1jS1326->$0;
          _M0L6_2atmpS4282 = _M0L3valS4283 + 1;
          _M0L1jS1326->$0 = _M0L6_2atmpS4282;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1jS1326);
          moonbit_decref_cycle_free(_M0L6targetS1324);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4286 =
        _M0L1cS1310->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4288 = _M0L1cS1310->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS4287 = _M0L3preS4288->$5;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS4286, _M0L4fireS4287, _M0L6targetS1324);
      moonbit_decref_cycle_free(_M0L6targetS1324);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25deliver__pending__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1302,
  float _M0L6t__nowS1305
) {
  struct _M0TPB5ArrayGfE* _M0L14pending__timesS4222;
  int32_t _M0L1nS1301;
  struct _M0TPB8MutLocalGiE* _M0L4keptS1303;
  struct _M0TPB8MutLocalGiE* _M0L1kS1304;
  int32_t _M0L3valS4221;
  int32_t _M0L6_2atmpS4220;
  struct _M0TPB8MutLocalGiE* _M0L4dropS1307;
  #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L14pending__timesS4222 = _M0L1cS1302->$7;
  #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS1301 = _M0MPC15array5Array6lengthGfE(_M0L14pending__timesS4222);
  if (_M0L1nS1301 == 0) {
    return 0;
  }
  _M0L4keptS1303
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4keptS1303)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4keptS1303->$0 = 0;
  _M0L1kS1304
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS1304)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS1304->$0 = 0;
  while (1) {
    int32_t _M0L3valS4183 = _M0L1kS1304->$0;
    if (_M0L3valS4183 < _M0L1nS1301) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS4185 = _M0L1cS1302->$7;
      int32_t _M0L3valS4186 = _M0L1kS1304->$0;
      float _M0L6_2atmpS4184;
      int32_t _M0L3valS4213;
      int32_t _M0L6_2atmpS4212;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS4184
      = _M0MPC15array5Array2atGfE(_M0L14pending__timesS4185, _M0L3valS4186);
      if (_M0L6_2atmpS4184 <= _M0L6t__nowS1305) {
        struct _M0TPB5ArrayGiE* _M0L14pending__postsS4191 = _M0L1cS1302->$8;
        int32_t _M0L3valS4192 = _M0L1kS1304->$0;
        int32_t _M0L6_2atmpS4187;
        struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4189;
        int32_t _M0L3valS4190;
        float _M0L6_2atmpS4188;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS4187
        = _M0MPC15array5Array2atGiE(_M0L14pending__postsS4191, _M0L3valS4192);
        _M0L16pending__weightsS4189 = _M0L1cS1302->$9;
        _M0L3valS4190 = _M0L1kS1304->$0;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS4188
        = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS4189, _M0L3valS4190);
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS1302, _M0L6_2atmpS4187, _M0L6_2atmpS4188);
      } else {
        int32_t _M0L3valS4193 = _M0L4keptS1303->$0;
        int32_t _M0L3valS4194 = _M0L1kS1304->$0;
        int32_t _M0L3valS4211;
        int32_t _M0L6_2atmpS4210;
        if (_M0L3valS4193 != _M0L3valS4194) {
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS4195 = _M0L1cS1302->$7;
          int32_t _M0L3valS4196 = _M0L4keptS1303->$0;
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS4198 = _M0L1cS1302->$7;
          int32_t _M0L3valS4199 = _M0L1kS1304->$0;
          float _M0L6_2atmpS4197;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS4200;
          int32_t _M0L3valS4201;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS4203;
          int32_t _M0L3valS4204;
          int32_t _M0L6_2atmpS4202;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4205;
          int32_t _M0L3valS4206;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4208;
          int32_t _M0L3valS4209;
          float _M0L6_2atmpS4207;
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS4197
          = _M0MPC15array5Array2atGfE(_M0L14pending__timesS4198, _M0L3valS4199);
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L14pending__timesS4195, _M0L3valS4196, _M0L6_2atmpS4197);
          _M0L14pending__postsS4200 = _M0L1cS1302->$8;
          _M0L3valS4201 = _M0L4keptS1303->$0;
          _M0L14pending__postsS4203 = _M0L1cS1302->$8;
          _M0L3valS4204 = _M0L1kS1304->$0;
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS4202
          = _M0MPC15array5Array2atGiE(_M0L14pending__postsS4203, _M0L3valS4204);
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGiE(_M0L14pending__postsS4200, _M0L3valS4201, _M0L6_2atmpS4202);
          _M0L16pending__weightsS4205 = _M0L1cS1302->$9;
          _M0L3valS4206 = _M0L4keptS1303->$0;
          _M0L16pending__weightsS4208 = _M0L1cS1302->$9;
          _M0L3valS4209 = _M0L1kS1304->$0;
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS4207
          = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS4208, _M0L3valS4209);
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L16pending__weightsS4205, _M0L3valS4206, _M0L6_2atmpS4207);
        }
        _M0L3valS4211 = _M0L4keptS1303->$0;
        _M0L6_2atmpS4210 = _M0L3valS4211 + 1;
        _M0L4keptS1303->$0 = _M0L6_2atmpS4210;
      }
      _M0L3valS4213 = _M0L1kS1304->$0;
      _M0L6_2atmpS4212 = _M0L3valS4213 + 1;
      _M0L1kS1304->$0 = _M0L6_2atmpS4212;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS1304);
    }
    break;
  }
  _M0L3valS4221 = _M0L4keptS1303->$0;
  moonbit_decref_cycle_free(_M0L4keptS1303);
  _M0L6_2atmpS4220 = _M0L1nS1301 - _M0L3valS4221;
  _M0L4dropS1307
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4dropS1307)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4dropS1307->$0 = _M0L6_2atmpS4220;
  while (1) {
    int32_t _M0L3valS4214 = _M0L4dropS1307->$0;
    if (_M0L3valS4214 > 0) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS4215 = _M0L1cS1302->$7;
      void* _M0L6_2atmpS5459;
      struct _M0TPB5ArrayGiE* _M0L14pending__postsS4216;
      struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4217;
      void* _M0L6_2atmpS5458;
      int32_t _M0L3valS4219;
      int32_t _M0L6_2atmpS4218;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS5459
      = _M0MPC15array5Array3popGfE(_M0L14pending__timesS4215);
      moonbit_decref_cycle_free(_M0L6_2atmpS5459);
      _M0L14pending__postsS4216 = _M0L1cS1302->$8;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MPC15array5Array3popGiE(_M0L14pending__postsS4216);
      _M0L16pending__weightsS4217 = _M0L1cS1302->$9;
      #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS5458
      = _M0MPC15array5Array3popGfE(_M0L16pending__weightsS4217);
      moonbit_decref_cycle_free(_M0L6_2atmpS5458);
      _M0L3valS4219 = _M0L4dropS1307->$0;
      _M0L6_2atmpS4218 = _M0L3valS4219 - 1;
      _M0L4dropS1307->$0 = _M0L6_2atmpS4218;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4dropS1307);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13apply__weight(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1298,
  int32_t _M0L9post__idxS1299,
  float _M0L1wS1300
) {
  moonbit_string_t _M0L3symS4170;
  #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L3symS4170 = _M0L1cS1298->$2;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  if (
    _M0L3symS4170 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS4170)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS4170, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS4170) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4176 = _M0L1cS1298->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS4171 = _M0L4postS4176->$13;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4175 = _M0L1cS1298->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS4174 = _M0L4postS4175->$13;
    float _M0L6_2atmpS4173;
    float _M0L6_2atmpS4172;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS4173
    = _M0MPC15array5Array2atGfE(_M0L3gluS4174, _M0L9post__idxS1299);
    _M0L6_2atmpS4172 = _M0L6_2atmpS4173 + _M0L1wS1300;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L3gluS4171, _M0L9post__idxS1299, _M0L6_2atmpS4172);
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4182 = _M0L1cS1298->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS4177 = _M0L4postS4182->$14;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4181 = _M0L1cS1298->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS4180 = _M0L4postS4181->$14;
    float _M0L6_2atmpS4179;
    float _M0L6_2atmpS4178;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS4179
    = _M0MPC15array5Array2atGfE(_M0L4gabaS4180, _M0L9post__idxS1299);
    _M0L6_2atmpS4178 = _M0L6_2atmpS4179 + _M0L1wS1300;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L4gabaS4177, _M0L9post__idxS1299, _M0L6_2atmpS4178);
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt11record__one(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS1295,
  float _M0L1tS1297
) {
  int32_t _M0L11step__countS4156;
  int32_t _M0L6_2atmpS4155;
  int32_t _M0L11step__countS4158;
  int32_t _M0L9rec__stepS4159;
  int32_t _M0L6_2atmpS4157;
  moonbit_string_t _M0L3symS4162;
  float _M0L1vS1296;
  struct _M0TPB5ArrayGfE* _M0L4dataS4160;
  struct _M0TPB5ArrayGfE* _M0L5timesS4161;
  #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L11step__countS4156 = _M0L1mS1295->$6;
  _M0L6_2atmpS4155 = _M0L11step__countS4156 + 1;
  _M0L1mS1295->$6 = _M0L6_2atmpS4155;
  _M0L11step__countS4158 = _M0L1mS1295->$6;
  _M0L9rec__stepS4159 = _M0L1mS1295->$5;
  _M0L6_2atmpS4157 = _M0L11step__countS4158 % _M0L9rec__stepS4159;
  if (_M0L6_2atmpS4157 != 0) {
    return 0;
  }
  _M0L3symS4162 = _M0L1mS1295->$1;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS4162 == (moonbit_string_t)moonbit_string_literal_10.data
    || Moonbit_array_length(_M0L3symS4162)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_10.data)
       && 0
          == memcmp(_M0L3symS4162, (moonbit_string_t)moonbit_string_literal_10.data, Moonbit_array_length(_M0L3symS4162) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS4165 = _M0L1mS1295->$0;
    struct _M0TPB5ArrayGfE* _M0L1vS4163 = _M0L3popS4165->$3;
    int32_t _M0L6neuronS4164 = _M0L1mS1295->$4;
    #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _M0L1vS1296 = _M0MPC15array5Array2atGfE(_M0L1vS4163, _M0L6neuronS4164);
  } else {
    moonbit_string_t _M0L3symS4166 = _M0L1mS1295->$1;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    if (
      _M0L3symS4166 == (moonbit_string_t)moonbit_string_literal_11.data
      || Moonbit_array_length(_M0L3symS4166)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_11.data)
         && 0
            == memcmp(_M0L3symS4166, (moonbit_string_t)moonbit_string_literal_11.data, Moonbit_array_length(_M0L3symS4166) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS4169 = _M0L1mS1295->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS4167 = _M0L3popS4169->$5;
      int32_t _M0L6neuronS4168 = _M0L1mS1295->$4;
      #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4167, _M0L6neuronS4168)) {
        _M0L1vS1296 = 0x1p+0f;
      } else {
        _M0L1vS1296 = 0x0p+0f;
      }
    } else {
      _M0L1vS1296 = 0x0p+0f;
    }
  }
  _M0L4dataS4160 = _M0L1mS1295->$2;
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L4dataS4160, _M0L1vS1296);
  _M0L5timesS4161 = _M0L1mS1295->$3;
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L5timesS4161, _M0L1tS1297);
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor9new__fire(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS1293,
  int32_t _M0L6neuronS1294
) {
  float* _M0L6_2atmpS4154;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4151;
  float* _M0L6_2atmpS4153;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4152;
  struct _M0TP26RiantR8snn__mbt7Monitor* _block_5768;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6_2atmpS4154 = moonbit_empty_float_array;
  _M0L6_2atmpS4151
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4151)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
  _M0L6_2atmpS4151->$0 = _M0L6_2atmpS4154;
  _M0L6_2atmpS4151->$1 = 0;
  _M0L6_2atmpS4153 = moonbit_empty_float_array;
  _M0L6_2atmpS4152
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4152)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
  _M0L6_2atmpS4152->$0 = _M0L6_2atmpS4153;
  _M0L6_2atmpS4152->$1 = 0;
  moonbit_incref_cycle_free(_M0L3popS1293);
  _block_5768
  = (struct _M0TP26RiantR8snn__mbt7Monitor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Monitor));
  Moonbit_object_header(_block_5768)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 60, 0);
  _block_5768->$0 = _M0L3popS1293;
  _block_5768->$1 = (moonbit_string_t)moonbit_string_literal_11.data;
  _block_5768->$2 = _M0L6_2atmpS4151;
  _block_5768->$3 = _M0L6_2atmpS4152;
  _block_5768->$4 = _M0L6neuronS1294;
  _block_5768->$5 = 1;
  _block_5768->$6 = 0;
  return _block_5768;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1289
) {
  int32_t _M0L1nS1288;
  int32_t _M0L7_2abindS1290;
  int32_t _M0L1iS1291;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1288 = _M0L1pS1289->$2;
  _M0L7_2abindS1290 = 0;
  _M0L1iS1291 = _M0L7_2abindS1290;
  while (1) {
    if (_M0L1iS1291 < _M0L1nS1288) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4128 = _M0L1pS1289->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS4149 = _M0L1pS1289->$9;
      float _M0L6_2atmpS4144;
      struct _M0TPB5ArrayGfE* _M0L1vS4148;
      float _M0L6_2atmpS4146;
      float _M0L4e__eS4147;
      float _M0L6_2atmpS4145;
      float _M0L6_2atmpS4141;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS4143;
      float _M0L6_2atmpS4142;
      float _M0L6_2atmpS4130;
      struct _M0TPB5ArrayGfE* _M0L2giS4140;
      float _M0L6_2atmpS4135;
      struct _M0TPB5ArrayGfE* _M0L1vS4139;
      float _M0L6_2atmpS4137;
      float _M0L4e__iS4138;
      float _M0L6_2atmpS4136;
      float _M0L6_2atmpS4132;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS4134;
      float _M0L6_2atmpS4133;
      float _M0L6_2atmpS4131;
      float _M0L6_2atmpS4129;
      int32_t _M0L6_2atmpS4150;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4144 = _M0MPC15array5Array2atGfE(_M0L2geS4149, _M0L1iS1291);
      _M0L1vS4148 = _M0L1pS1289->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4146 = _M0MPC15array5Array2atGfE(_M0L1vS4148, _M0L1iS1291);
      _M0L4e__eS4147 = _M0L1pS1289->$17;
      _M0L6_2atmpS4145 = _M0L6_2atmpS4146 - _M0L4e__eS4147;
      _M0L6_2atmpS4141 = _M0L6_2atmpS4144 * _M0L6_2atmpS4145;
      _M0L7gsyn__eS4143 = _M0L1pS1289->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4142
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS4143, _M0L1iS1291);
      _M0L6_2atmpS4130 = _M0L6_2atmpS4141 * _M0L6_2atmpS4142;
      _M0L2giS4140 = _M0L1pS1289->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4135 = _M0MPC15array5Array2atGfE(_M0L2giS4140, _M0L1iS1291);
      _M0L1vS4139 = _M0L1pS1289->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4137 = _M0MPC15array5Array2atGfE(_M0L1vS4139, _M0L1iS1291);
      _M0L4e__iS4138 = _M0L1pS1289->$18;
      _M0L6_2atmpS4136 = _M0L6_2atmpS4137 - _M0L4e__iS4138;
      _M0L6_2atmpS4132 = _M0L6_2atmpS4135 * _M0L6_2atmpS4136;
      _M0L7gsyn__iS4134 = _M0L1pS1289->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4133
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS4134, _M0L1iS1291);
      _M0L6_2atmpS4131 = _M0L6_2atmpS4132 * _M0L6_2atmpS4133;
      _M0L6_2atmpS4129 = _M0L6_2atmpS4130 + _M0L6_2atmpS4131;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS4128, _M0L1iS1291, _M0L6_2atmpS4129);
      _M0L6_2atmpS4150 = _M0L1iS1291 + 1;
      _M0L1iS1291 = _M0L6_2atmpS4150;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1280,
  float _M0L2dtS1283
) {
  int32_t _M0L1nS1279;
  int32_t _M0L7_2abindS1281;
  int32_t _M0L1iS1282;
  int32_t _M0L7_2abindS1285;
  int32_t _M0L1iS1286;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1279 = _M0L1pS1280->$2;
  _M0L7_2abindS1281 = 0;
  _M0L1iS1282 = _M0L7_2abindS1281;
  while (1) {
    if (_M0L1iS1282 < _M0L1nS1279) {
      struct _M0TPB5ArrayGfE* _M0L2heS4066 = _M0L1pS1280->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS4071 = _M0L1pS1280->$11;
      float _M0L6_2atmpS4068;
      struct _M0TPB5ArrayGfE* _M0L3gluS4070;
      float _M0L6_2atmpS4069;
      float _M0L6_2atmpS4067;
      struct _M0TPB5ArrayGfE* _M0L2hiS4072;
      struct _M0TPB5ArrayGfE* _M0L2hiS4077;
      float _M0L6_2atmpS4074;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4076;
      float _M0L6_2atmpS4075;
      float _M0L6_2atmpS4073;
      struct _M0TPB5ArrayGfE* _M0L2geS4078;
      struct _M0TPB5ArrayGfE* _M0L2geS4090;
      float _M0L6_2atmpS4080;
      struct _M0TPB5ArrayGfE* _M0L2geS4089;
      float _M0L6_2atmpS4088;
      float _M0L6_2atmpS4086;
      float _M0L3tdeS4087;
      float _M0L6_2atmpS4083;
      struct _M0TPB5ArrayGfE* _M0L2heS4085;
      float _M0L6_2atmpS4084;
      float _M0L6_2atmpS4082;
      float _M0L6_2atmpS4081;
      float _M0L6_2atmpS4079;
      struct _M0TPB5ArrayGfE* _M0L2heS4091;
      struct _M0TPB5ArrayGfE* _M0L2heS4100;
      float _M0L6_2atmpS4093;
      struct _M0TPB5ArrayGfE* _M0L2heS4099;
      float _M0L6_2atmpS4098;
      float _M0L6_2atmpS4096;
      float _M0L3treS4097;
      float _M0L6_2atmpS4095;
      float _M0L6_2atmpS4094;
      float _M0L6_2atmpS4092;
      struct _M0TPB5ArrayGfE* _M0L2giS4101;
      struct _M0TPB5ArrayGfE* _M0L2giS4113;
      float _M0L6_2atmpS4103;
      struct _M0TPB5ArrayGfE* _M0L2giS4112;
      float _M0L6_2atmpS4111;
      float _M0L6_2atmpS4109;
      float _M0L3tdiS4110;
      float _M0L6_2atmpS4106;
      struct _M0TPB5ArrayGfE* _M0L2hiS4108;
      float _M0L6_2atmpS4107;
      float _M0L6_2atmpS4105;
      float _M0L6_2atmpS4104;
      float _M0L6_2atmpS4102;
      struct _M0TPB5ArrayGfE* _M0L2hiS4114;
      struct _M0TPB5ArrayGfE* _M0L2hiS4123;
      float _M0L6_2atmpS4116;
      struct _M0TPB5ArrayGfE* _M0L2hiS4122;
      float _M0L6_2atmpS4121;
      float _M0L6_2atmpS4119;
      float _M0L3triS4120;
      float _M0L6_2atmpS4118;
      float _M0L6_2atmpS4117;
      float _M0L6_2atmpS4115;
      int32_t _M0L6_2atmpS4124;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4068 = _M0MPC15array5Array2atGfE(_M0L2heS4071, _M0L1iS1282);
      _M0L3gluS4070 = _M0L1pS1280->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4069
      = _M0MPC15array5Array2atGfE(_M0L3gluS4070, _M0L1iS1282);
      _M0L6_2atmpS4067 = _M0L6_2atmpS4068 + _M0L6_2atmpS4069;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4066, _M0L1iS1282, _M0L6_2atmpS4067);
      _M0L2hiS4072 = _M0L1pS1280->$12;
      _M0L2hiS4077 = _M0L1pS1280->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4074 = _M0MPC15array5Array2atGfE(_M0L2hiS4077, _M0L1iS1282);
      _M0L4gabaS4076 = _M0L1pS1280->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4075
      = _M0MPC15array5Array2atGfE(_M0L4gabaS4076, _M0L1iS1282);
      _M0L6_2atmpS4073 = _M0L6_2atmpS4074 + _M0L6_2atmpS4075;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4072, _M0L1iS1282, _M0L6_2atmpS4073);
      _M0L2geS4078 = _M0L1pS1280->$9;
      _M0L2geS4090 = _M0L1pS1280->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4080 = _M0MPC15array5Array2atGfE(_M0L2geS4090, _M0L1iS1282);
      _M0L2geS4089 = _M0L1pS1280->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4088 = _M0MPC15array5Array2atGfE(_M0L2geS4089, _M0L1iS1282);
      _M0L6_2atmpS4086 = -_M0L6_2atmpS4088;
      _M0L3tdeS4087 = _M0L1pS1280->$20;
      _M0L6_2atmpS4083 = _M0L6_2atmpS4086 / _M0L3tdeS4087;
      _M0L2heS4085 = _M0L1pS1280->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4084 = _M0MPC15array5Array2atGfE(_M0L2heS4085, _M0L1iS1282);
      _M0L6_2atmpS4082 = _M0L6_2atmpS4083 + _M0L6_2atmpS4084;
      _M0L6_2atmpS4081 = _M0L2dtS1283 * _M0L6_2atmpS4082;
      _M0L6_2atmpS4079 = _M0L6_2atmpS4080 + _M0L6_2atmpS4081;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4078, _M0L1iS1282, _M0L6_2atmpS4079);
      _M0L2heS4091 = _M0L1pS1280->$11;
      _M0L2heS4100 = _M0L1pS1280->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4093 = _M0MPC15array5Array2atGfE(_M0L2heS4100, _M0L1iS1282);
      _M0L2heS4099 = _M0L1pS1280->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4098 = _M0MPC15array5Array2atGfE(_M0L2heS4099, _M0L1iS1282);
      _M0L6_2atmpS4096 = -_M0L6_2atmpS4098;
      _M0L3treS4097 = _M0L1pS1280->$19;
      _M0L6_2atmpS4095 = _M0L6_2atmpS4096 / _M0L3treS4097;
      _M0L6_2atmpS4094 = _M0L2dtS1283 * _M0L6_2atmpS4095;
      _M0L6_2atmpS4092 = _M0L6_2atmpS4093 + _M0L6_2atmpS4094;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4091, _M0L1iS1282, _M0L6_2atmpS4092);
      _M0L2giS4101 = _M0L1pS1280->$10;
      _M0L2giS4113 = _M0L1pS1280->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4103 = _M0MPC15array5Array2atGfE(_M0L2giS4113, _M0L1iS1282);
      _M0L2giS4112 = _M0L1pS1280->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4111 = _M0MPC15array5Array2atGfE(_M0L2giS4112, _M0L1iS1282);
      _M0L6_2atmpS4109 = -_M0L6_2atmpS4111;
      _M0L3tdiS4110 = _M0L1pS1280->$22;
      _M0L6_2atmpS4106 = _M0L6_2atmpS4109 / _M0L3tdiS4110;
      _M0L2hiS4108 = _M0L1pS1280->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4107 = _M0MPC15array5Array2atGfE(_M0L2hiS4108, _M0L1iS1282);
      _M0L6_2atmpS4105 = _M0L6_2atmpS4106 + _M0L6_2atmpS4107;
      _M0L6_2atmpS4104 = _M0L2dtS1283 * _M0L6_2atmpS4105;
      _M0L6_2atmpS4102 = _M0L6_2atmpS4103 + _M0L6_2atmpS4104;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4101, _M0L1iS1282, _M0L6_2atmpS4102);
      _M0L2hiS4114 = _M0L1pS1280->$12;
      _M0L2hiS4123 = _M0L1pS1280->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4116 = _M0MPC15array5Array2atGfE(_M0L2hiS4123, _M0L1iS1282);
      _M0L2hiS4122 = _M0L1pS1280->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4121 = _M0MPC15array5Array2atGfE(_M0L2hiS4122, _M0L1iS1282);
      _M0L6_2atmpS4119 = -_M0L6_2atmpS4121;
      _M0L3triS4120 = _M0L1pS1280->$21;
      _M0L6_2atmpS4118 = _M0L6_2atmpS4119 / _M0L3triS4120;
      _M0L6_2atmpS4117 = _M0L2dtS1283 * _M0L6_2atmpS4118;
      _M0L6_2atmpS4115 = _M0L6_2atmpS4116 + _M0L6_2atmpS4117;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4114, _M0L1iS1282, _M0L6_2atmpS4115);
      _M0L6_2atmpS4124 = _M0L1iS1282 + 1;
      _M0L1iS1282 = _M0L6_2atmpS4124;
      continue;
    }
    break;
  }
  _M0L7_2abindS1285 = 0;
  _M0L1iS1286 = _M0L7_2abindS1285;
  while (1) {
    if (_M0L1iS1286 < _M0L1nS1279) {
      struct _M0TPB5ArrayGfE* _M0L3gluS4125 = _M0L1pS1280->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4126;
      int32_t _M0L6_2atmpS4127;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS4125, _M0L1iS1286, 0x0p+0f);
      _M0L4gabaS4126 = _M0L1pS1280->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS4126, _M0L1iS1286, 0x0p+0f);
      _M0L6_2atmpS4127 = _M0L1iS1286 + 1;
      _M0L1iS1286 = _M0L6_2atmpS4127;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1265,
  float _M0L2dtS1274
) {
  int32_t _M0L1nS1264;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S1266;
  float _M0L2tmS1267;
  float _M0L2elS1268;
  float _M0L1rS1269;
  float _M0L2vtS1270;
  float _M0L2vrS1271;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS4065;
  float _M0L11tabs__constS1272;
  float _M0L6_2atmpS4064;
  int32_t _M0L11tabs__stepsS1273;
  int32_t _M0L7_2abindS1275;
  int32_t _M0L1iS1276;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1264 = _M0L1pS1265->$2;
  _M0L3p__S1266 = _M0L1pS1265->$0;
  _M0L2tmS1267 = _M0L3p__S1266->$2;
  _M0L2elS1268 = _M0L3p__S1266->$5;
  _M0L1rS1269 = _M0L3p__S1266->$6;
  _M0L2vtS1270 = _M0L3p__S1266->$3;
  _M0L2vrS1271 = _M0L3p__S1266->$4;
  _M0L5spikeS4065 = _M0L1pS1265->$1;
  _M0L11tabs__constS1272 = _M0L5spikeS4065->$0;
  _M0L6_2atmpS4064 = _M0L11tabs__constS1272 / _M0L2dtS1274;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS1273 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4064);
  _M0L7_2abindS1275 = 0;
  _M0L1iS1276 = _M0L7_2abindS1275;
  while (1) {
    if (_M0L1iS1276 < _M0L1nS1264) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS4024 = _M0L1pS1265->$6;
      int32_t _M0L6_2atmpS4023;
      struct _M0TPB5ArrayGfE* _M0L1vS4030;
      struct _M0TPB5ArrayGfE* _M0L1vS4051;
      float _M0L6_2atmpS4032;
      float _M0L6_2atmpS4034;
      struct _M0TPB5ArrayGfE* _M0L1vS4050;
      float _M0L6_2atmpS4049;
      float _M0L6_2atmpS4048;
      float _M0L6_2atmpS4040;
      struct _M0TPB5ArrayGfE* _M0L1wS4047;
      float _M0L6_2atmpS4046;
      float _M0L6_2atmpS4043;
      struct _M0TPB5ArrayGfE* _M0L1iS4045;
      float _M0L6_2atmpS4044;
      float _M0L6_2atmpS4042;
      float _M0L6_2atmpS4041;
      float _M0L6_2atmpS4036;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4039;
      float _M0L6_2atmpS4038;
      float _M0L6_2atmpS4037;
      float _M0L6_2atmpS4035;
      float _M0L6_2atmpS4033;
      float _M0L6_2atmpS4031;
      struct _M0TPB5ArrayGbE* _M0L4fireS4052;
      struct _M0TPB5ArrayGfE* _M0L1vS4055;
      float _M0L6_2atmpS4054;
      int32_t _M0L6_2atmpS4053;
      struct _M0TPB5ArrayGfE* _M0L1vS4056;
      struct _M0TPB5ArrayGbE* _M0L4fireS4058;
      float _M0L6_2atmpS4057;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4060;
      struct _M0TPB5ArrayGbE* _M0L4fireS4062;
      int32_t _M0L6_2atmpS4061;
      int32_t _M0L6_2atmpS4022;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4023
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4024, _M0L1iS1276);
      if (_M0L6_2atmpS4023 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS4025 = _M0L1pS1265->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS4026;
        struct _M0TPB5ArrayGiE* _M0L4tabsS4029;
        int32_t _M0L6_2atmpS4028;
        int32_t _M0L6_2atmpS4027;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS4025, _M0L1iS1276, 0);
        _M0L4tabsS4026 = _M0L1pS1265->$6;
        _M0L4tabsS4029 = _M0L1pS1265->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS4028
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4029, _M0L1iS1276);
        _M0L6_2atmpS4027 = _M0L6_2atmpS4028 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS4026, _M0L1iS1276, _M0L6_2atmpS4027);
        goto join_1277;
      }
      _M0L1vS4030 = _M0L1pS1265->$3;
      _M0L1vS4051 = _M0L1pS1265->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4032 = _M0MPC15array5Array2atGfE(_M0L1vS4051, _M0L1iS1276);
      _M0L6_2atmpS4034 = _M0L2dtS1274 / _M0L2tmS1267;
      _M0L1vS4050 = _M0L1pS1265->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4049 = _M0MPC15array5Array2atGfE(_M0L1vS4050, _M0L1iS1276);
      _M0L6_2atmpS4048 = _M0L6_2atmpS4049 - _M0L2elS1268;
      _M0L6_2atmpS4040 = -_M0L6_2atmpS4048;
      _M0L1wS4047 = _M0L1pS1265->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4046 = _M0MPC15array5Array2atGfE(_M0L1wS4047, _M0L1iS1276);
      _M0L6_2atmpS4043 = -_M0L6_2atmpS4046;
      _M0L1iS4045 = _M0L1pS1265->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4044 = _M0MPC15array5Array2atGfE(_M0L1iS4045, _M0L1iS1276);
      _M0L6_2atmpS4042 = _M0L6_2atmpS4043 + _M0L6_2atmpS4044;
      _M0L6_2atmpS4041 = _M0L1rS1269 * _M0L6_2atmpS4042;
      _M0L6_2atmpS4036 = _M0L6_2atmpS4040 + _M0L6_2atmpS4041;
      _M0L9syn__currS4039 = _M0L1pS1265->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4038
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS4039, _M0L1iS1276);
      _M0L6_2atmpS4037 = _M0L1rS1269 * _M0L6_2atmpS4038;
      _M0L6_2atmpS4035 = _M0L6_2atmpS4036 - _M0L6_2atmpS4037;
      _M0L6_2atmpS4033 = _M0L6_2atmpS4034 * _M0L6_2atmpS4035;
      _M0L6_2atmpS4031 = _M0L6_2atmpS4032 + _M0L6_2atmpS4033;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4030, _M0L1iS1276, _M0L6_2atmpS4031);
      _M0L4fireS4052 = _M0L1pS1265->$5;
      _M0L1vS4055 = _M0L1pS1265->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4054 = _M0MPC15array5Array2atGfE(_M0L1vS4055, _M0L1iS1276);
      _M0L6_2atmpS4053 = _M0L6_2atmpS4054 > _M0L2vtS1270;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4052, _M0L1iS1276, _M0L6_2atmpS4053);
      _M0L1vS4056 = _M0L1pS1265->$3;
      _M0L4fireS4058 = _M0L1pS1265->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4058, _M0L1iS1276)) {
        _M0L6_2atmpS4057 = _M0L2vrS1271;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4059 = _M0L1pS1265->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS4057
        = _M0MPC15array5Array2atGfE(_M0L1vS4059, _M0L1iS1276);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4056, _M0L1iS1276, _M0L6_2atmpS4057);
      _M0L4tabsS4060 = _M0L1pS1265->$6;
      _M0L4fireS4062 = _M0L1pS1265->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4062, _M0L1iS1276)) {
        _M0L6_2atmpS4061 = _M0L11tabs__stepsS1273;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4063 = _M0L1pS1265->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS4061
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4063, _M0L1iS1276);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4060, _M0L1iS1276, _M0L6_2atmpS4061);
      goto join_1277;
      goto joinlet_5773;
      join_1277:;
      _M0L6_2atmpS4022 = _M0L1iS1276 + 1;
      _M0L1iS1276 = _M0L6_2atmpS4022;
      continue;
      joinlet_5773:;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS1252,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1255,
  struct _M0TPB5ArrayGfE* _M0L7post__gS1261
) {
  int32_t _M0L4rowsS1251;
  int32_t _M0L7_2abindS1253;
  int32_t _M0L1iS1254;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS1251 = _M0L1mS1252->$0;
  _M0L7_2abindS1253 = 0;
  _M0L1iS1254 = _M0L7_2abindS1253;
  while (1) {
    if (_M0L1iS1254 < _M0L4rowsS1251) {
      int32_t _M0L6_2atmpS4021;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1255, _M0L1iS1254)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS4020 = _M0L1mS1252->$2;
        int32_t _M0L5startS1256;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS4018;
        int32_t _M0L6_2atmpS4019;
        int32_t _M0L3endS1257;
        int32_t _M0L1kS1258;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS1256
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS4020, _M0L1iS1254);
        _M0L6rowptrS4018 = _M0L1mS1252->$2;
        _M0L6_2atmpS4019 = _M0L1iS1254 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS1257
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS4018, _M0L6_2atmpS4019);
        _M0L1kS1258 = _M0L5startS1256;
        while (1) {
          if (_M0L1kS1258 < _M0L3endS1257) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS4016 = _M0L1mS1252->$3;
            int32_t _M0L9post__idxS1259;
            struct _M0TPB5ArrayGfE* _M0L4valsS4015;
            float _M0L1wS1260;
            float _M0L6_2atmpS4014;
            float _M0L6_2atmpS4013;
            int32_t _M0L6_2atmpS4017;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS1259
            = _M0MPC15array5Array2atGiE(_M0L6colptrS4016, _M0L1kS1258);
            _M0L4valsS4015 = _M0L1mS1252->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS1260
            = _M0MPC15array5Array2atGfE(_M0L4valsS4015, _M0L1kS1258);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS4014
            = _M0MPC15array5Array2atGfE(_M0L7post__gS1261, _M0L9post__idxS1259);
            _M0L6_2atmpS4013 = _M0L6_2atmpS4014 + _M0L1wS1260;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS1261, _M0L9post__idxS1259, _M0L6_2atmpS4013);
            _M0L6_2atmpS4017 = _M0L1kS1258 + 1;
            _M0L1kS1258 = _M0L6_2atmpS4017;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS4021 = _M0L1iS1254 + 1;
      _M0L1iS1254 = _M0L6_2atmpS4021;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20ca__plasticity__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1229,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1216,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1218,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1228,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1224,
  struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L4varsS1213,
  struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L5paramS1220,
  float _M0L6t__nowS1214,
  float _M0L2dtS1241
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3904;
  int32_t _M0L6_2atmpS3903;
  int32_t _if__result_5776;
  int32_t _M0L6n__preS1215;
  int32_t _M0L7n__postS1217;
  float _M0L8tau__preS4012;
  float _M0L13inv__tau__preS1219;
  float _M0L9tau__postS4011;
  float _M0L14inv__tau__postS1221;
  struct _M0TPB8MutLocalGiE* _M0L1jS1222;
  struct _M0TPB8MutLocalGiE* _M0L1kS1232;
  struct _M0TPB8MutLocalGiE* _M0L2jjS1240;
  struct _M0TPB8MutLocalGiE* _M0L2iiS1243;
  struct _M0TPB8MutLocalGiE* _M0L3jj2S1245;
  struct _M0TPB8MutLocalGiE* _M0L3ii2S1247;
  struct _M0TPB8MutLocalGiE* _M0L2s2S1249;
  #line 1495 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6activeS3904 = _M0L4varsS1213->$4;
  #line 1507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3903 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3904);
  if (_M0L6_2atmpS3903 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3902 = _M0L4varsS1213->$4;
    int32_t _M0L6_2atmpS3901;
    #line 1507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS3901 = _M0MPC15array5Array2atGbE(_M0L6activeS3902, 0);
    _if__result_5776 = !_M0L6_2atmpS3901;
  } else {
    _if__result_5776 = 0;
  }
  if (_if__result_5776) {
    return 0;
  }
  #line 1509 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1215 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1216);
  #line 1510 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1217 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1218);
  _M0L8tau__preS4012 = _M0L5paramS1220->$2;
  _M0L13inv__tau__preS1219 = 0x1p+0f / _M0L8tau__preS4012;
  _M0L9tau__postS4011 = _M0L5paramS1220->$3;
  _M0L14inv__tau__postS1221 = 0x1p+0f / _M0L9tau__postS4011;
  _M0L1jS1222
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1222)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1222->$0 = 0;
  while (1) {
    int32_t _M0L3valS3905 = _M0L1jS1222->$0;
    if (_M0L3valS3905 < _M0L6n__preS1215) {
      int32_t _M0L3valS3906 = _M0L1jS1222->$0;
      int32_t _M0L3valS3921;
      int32_t _M0L6_2atmpS3920;
      #line 1516 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1216, _M0L3valS3906)) {
        int32_t _M0L3valS3919 = _M0L1jS1222->$0;
        int32_t _M0L5startS1223;
        int32_t _M0L3valS3918;
        int32_t _M0L6_2atmpS3917;
        int32_t _M0L5end__S1225;
        struct _M0TPB8MutLocalGiE* _M0L1sS1226;
        #line 1517 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS1223
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1224, _M0L3valS3919);
        _M0L3valS3918 = _M0L1jS1222->$0;
        _M0L6_2atmpS3917 = _M0L3valS3918 + 1;
        #line 1518 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5end__S1225
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1224, _M0L6_2atmpS3917);
        _M0L1sS1226
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS1226)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS1226->$0 = _M0L5startS1223;
        while (1) {
          int32_t _M0L3valS3907 = _M0L1sS1226->$0;
          if (_M0L3valS3907 < _M0L5end__S1225) {
            int32_t _M0L3valS3916 = _M0L1sS1226->$0;
            int32_t _M0L1iS1227;
            int32_t _M0L3valS3908;
            int32_t _M0L3valS3913;
            float _M0L6_2atmpS3910;
            struct _M0TPB5ArrayGfE* _M0L5tpostS3912;
            float _M0L6_2atmpS3911;
            float _M0L6_2atmpS3909;
            int32_t _M0L3valS3915;
            int32_t _M0L6_2atmpS3914;
            #line 1521 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L1iS1227
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1228, _M0L3valS3916);
            _M0L3valS3908 = _M0L1sS1226->$0;
            _M0L3valS3913 = _M0L1sS1226->$0;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3910
            = _M0MPC15array5Array2atGfE(_M0L1wS1229, _M0L3valS3913);
            _M0L5tpostS3912 = _M0L4varsS1213->$3;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3911
            = _M0MPC15array5Array2atGfE(_M0L5tpostS3912, _M0L1iS1227);
            _M0L6_2atmpS3909 = _M0L6_2atmpS3910 + _M0L6_2atmpS3911;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1229, _M0L3valS3908, _M0L6_2atmpS3909);
            _M0L3valS3915 = _M0L1sS1226->$0;
            _M0L6_2atmpS3914 = _M0L3valS3915 + 1;
            _M0L1sS1226->$0 = _M0L6_2atmpS3914;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS1226);
          }
          break;
        }
      }
      _M0L3valS3921 = _M0L1jS1222->$0;
      _M0L6_2atmpS3920 = _M0L3valS3921 + 1;
      _M0L1jS1222->$0 = _M0L6_2atmpS3920;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1222);
    }
    break;
  }
  _M0L1kS1232
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS1232)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS1232->$0 = 0;
  while (1) {
    int32_t _M0L3valS3922 = _M0L1kS1232->$0;
    if (_M0L3valS3922 < _M0L7n__postS1217) {
      int32_t _M0L3valS3923 = _M0L1kS1232->$0;
      int32_t _M0L3valS3944;
      int32_t _M0L6_2atmpS3943;
      #line 1531 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1218, _M0L3valS3923)) {
        struct _M0TPB8MutLocalGiE* _M0L2j2S1233 =
          (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L2j2S1233)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L2j2S1233->$0 = 0;
        while (1) {
          int32_t _M0L3valS3924 = _M0L2j2S1233->$0;
          if (_M0L3valS3924 < _M0L6n__preS1215) {
            int32_t _M0L3valS3942 = _M0L2j2S1233->$0;
            int32_t _M0L5startS1234;
            int32_t _M0L3valS3941;
            int32_t _M0L6_2atmpS3940;
            int32_t _M0L5end__S1235;
            struct _M0TPB8MutLocalGiE* _M0L1sS1236;
            int32_t _M0L3valS3939;
            int32_t _M0L6_2atmpS3938;
            #line 1537 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L5startS1234
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS1224, _M0L3valS3942);
            _M0L3valS3941 = _M0L2j2S1233->$0;
            _M0L6_2atmpS3940 = _M0L3valS3941 + 1;
            #line 1538 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L5end__S1235
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS1224, _M0L6_2atmpS3940);
            _M0L1sS1236
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS1236)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS1236->$0 = _M0L5startS1234;
            while (1) {
              int32_t _M0L3valS3925 = _M0L1sS1236->$0;
              if (_M0L3valS3925 < _M0L5end__S1235) {
                int32_t _M0L3valS3928 = _M0L1sS1236->$0;
                int32_t _M0L6_2atmpS3926;
                int32_t _M0L3valS3927;
                int32_t _M0L3valS3937;
                int32_t _M0L6_2atmpS3936;
                #line 1541 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                _M0L6_2atmpS3926
                = _M0MPC15array5Array2atGiE(_M0L6colptrS1228, _M0L3valS3928);
                _M0L3valS3927 = _M0L1kS1232->$0;
                if (_M0L6_2atmpS3926 == _M0L3valS3927) {
                  int32_t _M0L3valS3929 = _M0L1sS1236->$0;
                  int32_t _M0L3valS3935 = _M0L1sS1236->$0;
                  float _M0L6_2atmpS3931;
                  struct _M0TPB5ArrayGfE* _M0L4tpreS3933;
                  int32_t _M0L3valS3934;
                  float _M0L6_2atmpS3932;
                  float _M0L6_2atmpS3930;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0L6_2atmpS3931
                  = _M0MPC15array5Array2atGfE(_M0L1wS1229, _M0L3valS3935);
                  _M0L4tpreS3933 = _M0L4varsS1213->$2;
                  _M0L3valS3934 = _M0L2j2S1233->$0;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0L6_2atmpS3932
                  = _M0MPC15array5Array2atGfE(_M0L4tpreS3933, _M0L3valS3934);
                  _M0L6_2atmpS3930 = _M0L6_2atmpS3931 + _M0L6_2atmpS3932;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0MPC15array5Array3setGfE(_M0L1wS1229, _M0L3valS3929, _M0L6_2atmpS3930);
                }
                _M0L3valS3937 = _M0L1sS1236->$0;
                _M0L6_2atmpS3936 = _M0L3valS3937 + 1;
                _M0L1sS1236->$0 = _M0L6_2atmpS3936;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS1236);
              }
              break;
            }
            _M0L3valS3939 = _M0L2j2S1233->$0;
            _M0L6_2atmpS3938 = _M0L3valS3939 + 1;
            _M0L2j2S1233->$0 = _M0L6_2atmpS3938;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L2j2S1233);
          }
          break;
        }
      }
      _M0L3valS3944 = _M0L1kS1232->$0;
      _M0L6_2atmpS3943 = _M0L3valS3944 + 1;
      _M0L1kS1232->$0 = _M0L6_2atmpS3943;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS1232);
    }
    break;
  }
  _M0L2jjS1240
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2jjS1240)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2jjS1240->$0 = 0;
  while (1) {
    int32_t _M0L3valS3945 = _M0L2jjS1240->$0;
    if (_M0L3valS3945 < _M0L6n__preS1215) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS3946 = _M0L4varsS1213->$2;
      int32_t _M0L3valS3947 = _M0L2jjS1240->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3956 = _M0L4varsS1213->$2;
      int32_t _M0L3valS3957 = _M0L2jjS1240->$0;
      float _M0L6_2atmpS3949;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3954;
      int32_t _M0L3valS3955;
      float _M0L6_2atmpS3953;
      float _M0L6_2atmpS3952;
      float _M0L6_2atmpS3951;
      float _M0L6_2atmpS3950;
      float _M0L6_2atmpS3948;
      int32_t _M0L3valS3959;
      int32_t _M0L6_2atmpS3958;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3949
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3956, _M0L3valS3957);
      _M0L4tpreS3954 = _M0L4varsS1213->$2;
      _M0L3valS3955 = _M0L2jjS1240->$0;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3953
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3954, _M0L3valS3955);
      _M0L6_2atmpS3952 = -_M0L6_2atmpS3953;
      _M0L6_2atmpS3951 = _M0L2dtS1241 * _M0L6_2atmpS3952;
      _M0L6_2atmpS3950 = _M0L6_2atmpS3951 * _M0L13inv__tau__preS1219;
      _M0L6_2atmpS3948 = _M0L6_2atmpS3949 + _M0L6_2atmpS3950;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS3946, _M0L3valS3947, _M0L6_2atmpS3948);
      _M0L3valS3959 = _M0L2jjS1240->$0;
      _M0L6_2atmpS3958 = _M0L3valS3959 + 1;
      _M0L2jjS1240->$0 = _M0L6_2atmpS3958;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2jjS1240);
    }
    break;
  }
  _M0L2iiS1243
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2iiS1243)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2iiS1243->$0 = 0;
  while (1) {
    int32_t _M0L3valS3960 = _M0L2iiS1243->$0;
    if (_M0L3valS3960 < _M0L7n__postS1217) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS3961 = _M0L4varsS1213->$3;
      int32_t _M0L3valS3962 = _M0L2iiS1243->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3971 = _M0L4varsS1213->$3;
      int32_t _M0L3valS3972 = _M0L2iiS1243->$0;
      float _M0L6_2atmpS3964;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3969;
      int32_t _M0L3valS3970;
      float _M0L6_2atmpS3968;
      float _M0L6_2atmpS3967;
      float _M0L6_2atmpS3966;
      float _M0L6_2atmpS3965;
      float _M0L6_2atmpS3963;
      int32_t _M0L3valS3974;
      int32_t _M0L6_2atmpS3973;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3964
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3971, _M0L3valS3972);
      _M0L5tpostS3969 = _M0L4varsS1213->$3;
      _M0L3valS3970 = _M0L2iiS1243->$0;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3968
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3969, _M0L3valS3970);
      _M0L6_2atmpS3967 = -_M0L6_2atmpS3968;
      _M0L6_2atmpS3966 = _M0L2dtS1241 * _M0L6_2atmpS3967;
      _M0L6_2atmpS3965 = _M0L6_2atmpS3966 * _M0L14inv__tau__postS1221;
      _M0L6_2atmpS3963 = _M0L6_2atmpS3964 + _M0L6_2atmpS3965;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS3961, _M0L3valS3962, _M0L6_2atmpS3963);
      _M0L3valS3974 = _M0L2iiS1243->$0;
      _M0L6_2atmpS3973 = _M0L3valS3974 + 1;
      _M0L2iiS1243->$0 = _M0L6_2atmpS3973;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2iiS1243);
    }
    break;
  }
  _M0L3jj2S1245
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3jj2S1245)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3jj2S1245->$0 = 0;
  while (1) {
    int32_t _M0L3valS3975 = _M0L3jj2S1245->$0;
    if (_M0L3valS3975 < _M0L6n__preS1215) {
      int32_t _M0L3valS3976 = _M0L3jj2S1245->$0;
      int32_t _M0L3valS3985;
      int32_t _M0L6_2atmpS3984;
      #line 1565 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1216, _M0L3valS3976)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS3977 = _M0L4varsS1213->$2;
        int32_t _M0L3valS3978 = _M0L3jj2S1245->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS3982 = _M0L4varsS1213->$2;
        int32_t _M0L3valS3983 = _M0L3jj2S1245->$0;
        float _M0L6_2atmpS3980;
        float _M0L6a__preS3981;
        float _M0L6_2atmpS3979;
        #line 1566 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3980
        = _M0MPC15array5Array2atGfE(_M0L4tpreS3982, _M0L3valS3983);
        _M0L6a__preS3981 = _M0L5paramS1220->$0;
        _M0L6_2atmpS3979 = _M0L6_2atmpS3980 + _M0L6a__preS3981;
        #line 1566 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS3977, _M0L3valS3978, _M0L6_2atmpS3979);
      }
      _M0L3valS3985 = _M0L3jj2S1245->$0;
      _M0L6_2atmpS3984 = _M0L3valS3985 + 1;
      _M0L3jj2S1245->$0 = _M0L6_2atmpS3984;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L3jj2S1245);
    }
    break;
  }
  _M0L3ii2S1247
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3ii2S1247)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3ii2S1247->$0 = 0;
  while (1) {
    int32_t _M0L3valS3986 = _M0L3ii2S1247->$0;
    if (_M0L3valS3986 < _M0L7n__postS1217) {
      int32_t _M0L3valS3987 = _M0L3ii2S1247->$0;
      int32_t _M0L3valS3996;
      int32_t _M0L6_2atmpS3995;
      #line 1572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1218, _M0L3valS3987)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS3988 = _M0L4varsS1213->$3;
        int32_t _M0L3valS3989 = _M0L3ii2S1247->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS3993 = _M0L4varsS1213->$3;
        int32_t _M0L3valS3994 = _M0L3ii2S1247->$0;
        float _M0L6_2atmpS3991;
        float _M0L7a__postS3992;
        float _M0L6_2atmpS3990;
        #line 1573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3991
        = _M0MPC15array5Array2atGfE(_M0L5tpostS3993, _M0L3valS3994);
        _M0L7a__postS3992 = _M0L5paramS1220->$1;
        _M0L6_2atmpS3990 = _M0L6_2atmpS3991 + _M0L7a__postS3992;
        #line 1573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS3988, _M0L3valS3989, _M0L6_2atmpS3990);
      }
      _M0L3valS3996 = _M0L3ii2S1247->$0;
      _M0L6_2atmpS3995 = _M0L3valS3996 + 1;
      _M0L3ii2S1247->$0 = _M0L6_2atmpS3995;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L3ii2S1247);
    }
    break;
  }
  _M0L2s2S1249
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S1249)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S1249->$0 = 0;
  while (1) {
    int32_t _M0L3valS3997 = _M0L2s2S1249->$0;
    int32_t _M0L6_2atmpS3998;
    #line 1579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS3998 = _M0MPC15array5Array6lengthGfE(_M0L1wS1229);
    if (_M0L3valS3997 < _M0L6_2atmpS3998) {
      int32_t _M0L3valS4001 = _M0L2s2S1249->$0;
      float _M0L6_2atmpS3999;
      float _M0L6w__minS4000;
      int32_t _M0L3valS4006;
      float _M0L6_2atmpS4004;
      float _M0L6w__maxS4005;
      int32_t _M0L3valS4010;
      int32_t _M0L6_2atmpS4009;
      #line 1580 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3999
      = _M0MPC15array5Array2atGfE(_M0L1wS1229, _M0L3valS4001);
      _M0L6w__minS4000 = _M0L5paramS1220->$5;
      if (_M0L6_2atmpS3999 < _M0L6w__minS4000) {
        int32_t _M0L3valS4002 = _M0L2s2S1249->$0;
        float _M0L6w__minS4003 = _M0L5paramS1220->$5;
        #line 1580 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1229, _M0L3valS4002, _M0L6w__minS4003);
      }
      _M0L3valS4006 = _M0L2s2S1249->$0;
      #line 1581 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4004
      = _M0MPC15array5Array2atGfE(_M0L1wS1229, _M0L3valS4006);
      _M0L6w__maxS4005 = _M0L5paramS1220->$4;
      if (_M0L6_2atmpS4004 > _M0L6w__maxS4005) {
        int32_t _M0L3valS4007 = _M0L2s2S1249->$0;
        float _M0L6w__maxS4008 = _M0L5paramS1220->$4;
        #line 1581 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1229, _M0L3valS4007, _M0L6w__maxS4008);
      }
      _M0L3valS4010 = _M0L2s2S1249->$0;
      _M0L6_2atmpS4009 = _M0L3valS4010 + 1;
      _M0L2s2S1249->$0 = _M0L6_2atmpS4009;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S1249);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt21stdp__symmetric__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1209,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1182,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1184,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1204,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1197,
  struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L4varsS1189,
  struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L5paramS1186,
  float _M0L6t__nowS1180,
  float _M0L2dtS1190
) {
  int32_t _M0L6n__preS1181;
  int32_t _M0L7n__postS1183;
  float _M0L6tau__xS3900;
  float _M0L11inv__tau__xS1185;
  float _M0L6tau__yS3899;
  float _M0L11inv__tau__yS1187;
  struct _M0TPB8MutLocalGiE* _M0L1jS1188;
  struct _M0TPB8MutLocalGiE* _M0L1iS1192;
  float _M0L4a__xS3896;
  float _M0L6tau__xS3898;
  float _M0L6_2atmpS3897;
  float _M0L7coef__xS1194;
  float _M0L4a__yS3893;
  float _M0L6tau__yS3895;
  float _M0L6_2atmpS3894;
  float _M0L7coef__yS1195;
  #line 1296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 1308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1181 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1182);
  #line 1309 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1183 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1184);
  _M0L6tau__xS3900 = _M0L5paramS1186->$2;
  _M0L11inv__tau__xS1185 = 0x1p+0f / _M0L6tau__xS3900;
  _M0L6tau__yS3899 = _M0L5paramS1186->$3;
  _M0L11inv__tau__yS1187 = 0x1p+0f / _M0L6tau__yS3899;
  _M0L1jS1188
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1188)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1188->$0 = 0;
  while (1) {
    int32_t _M0L3valS3770 = _M0L1jS1188->$0;
    if (_M0L3valS3770 < _M0L6n__preS1181) {
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3771 = _M0L4varsS1189->$0;
      int32_t _M0L3valS3772 = _M0L1jS1188->$0;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3781 = _M0L4varsS1189->$0;
      int32_t _M0L3valS3782 = _M0L1jS1188->$0;
      float _M0L6_2atmpS3774;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3779;
      int32_t _M0L3valS3780;
      float _M0L6_2atmpS3778;
      float _M0L6_2atmpS3777;
      float _M0L6_2atmpS3776;
      float _M0L6_2atmpS3775;
      float _M0L6_2atmpS3773;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3783;
      int32_t _M0L3valS3784;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3793;
      int32_t _M0L3valS3794;
      float _M0L6_2atmpS3786;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3791;
      int32_t _M0L3valS3792;
      float _M0L6_2atmpS3790;
      float _M0L6_2atmpS3789;
      float _M0L6_2atmpS3788;
      float _M0L6_2atmpS3787;
      float _M0L6_2atmpS3785;
      int32_t _M0L3valS3795;
      int32_t _M0L3valS3809;
      int32_t _M0L6_2atmpS3808;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3774
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3781, _M0L3valS3782);
      _M0L5tr__xS3779 = _M0L4varsS1189->$0;
      _M0L3valS3780 = _M0L1jS1188->$0;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3778
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3779, _M0L3valS3780);
      _M0L6_2atmpS3777 = -_M0L6_2atmpS3778;
      _M0L6_2atmpS3776 = _M0L2dtS1190 * _M0L6_2atmpS3777;
      _M0L6_2atmpS3775 = _M0L6_2atmpS3776 * _M0L11inv__tau__xS1185;
      _M0L6_2atmpS3773 = _M0L6_2atmpS3774 + _M0L6_2atmpS3775;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__xS3771, _M0L3valS3772, _M0L6_2atmpS3773);
      _M0L5tr__yS3783 = _M0L4varsS1189->$1;
      _M0L3valS3784 = _M0L1jS1188->$0;
      _M0L5tr__yS3793 = _M0L4varsS1189->$1;
      _M0L3valS3794 = _M0L1jS1188->$0;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3786
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS3793, _M0L3valS3794);
      _M0L5tr__yS3791 = _M0L4varsS1189->$1;
      _M0L3valS3792 = _M0L1jS1188->$0;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3790
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS3791, _M0L3valS3792);
      _M0L6_2atmpS3789 = -_M0L6_2atmpS3790;
      _M0L6_2atmpS3788 = _M0L2dtS1190 * _M0L6_2atmpS3789;
      _M0L6_2atmpS3787 = _M0L6_2atmpS3788 * _M0L11inv__tau__yS1187;
      _M0L6_2atmpS3785 = _M0L6_2atmpS3786 + _M0L6_2atmpS3787;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__yS3783, _M0L3valS3784, _M0L6_2atmpS3785);
      _M0L3valS3795 = _M0L1jS1188->$0;
      #line 1318 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1182, _M0L3valS3795)) {
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3796 = _M0L4varsS1189->$0;
        int32_t _M0L3valS3797 = _M0L1jS1188->$0;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3800 = _M0L4varsS1189->$0;
        int32_t _M0L3valS3801 = _M0L1jS1188->$0;
        float _M0L6_2atmpS3799;
        float _M0L6_2atmpS3798;
        struct _M0TPB5ArrayGfE* _M0L5tr__yS3802;
        int32_t _M0L3valS3803;
        struct _M0TPB5ArrayGfE* _M0L5tr__yS3806;
        int32_t _M0L3valS3807;
        float _M0L6_2atmpS3805;
        float _M0L6_2atmpS3804;
        #line 1319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3799
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3800, _M0L3valS3801);
        _M0L6_2atmpS3798 = _M0L6_2atmpS3799 + 0x1p+0f;
        #line 1319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__xS3796, _M0L3valS3797, _M0L6_2atmpS3798);
        _M0L5tr__yS3802 = _M0L4varsS1189->$1;
        _M0L3valS3803 = _M0L1jS1188->$0;
        _M0L5tr__yS3806 = _M0L4varsS1189->$1;
        _M0L3valS3807 = _M0L1jS1188->$0;
        #line 1320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3805
        = _M0MPC15array5Array2atGfE(_M0L5tr__yS3806, _M0L3valS3807);
        _M0L6_2atmpS3804 = _M0L6_2atmpS3805 + 0x1p+0f;
        #line 1320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__yS3802, _M0L3valS3803, _M0L6_2atmpS3804);
      }
      _M0L3valS3809 = _M0L1jS1188->$0;
      _M0L6_2atmpS3808 = _M0L3valS3809 + 1;
      _M0L1jS1188->$0 = _M0L6_2atmpS3808;
      continue;
    }
    break;
  }
  _M0L1iS1192
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1192)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1192->$0 = 0;
  while (1) {
    int32_t _M0L3valS3810 = _M0L1iS1192->$0;
    if (_M0L3valS3810 < _M0L7n__postS1183) {
      struct _M0TPB5ArrayGfE* _M0L5to__xS3811 = _M0L4varsS1189->$2;
      int32_t _M0L3valS3812 = _M0L1iS1192->$0;
      struct _M0TPB5ArrayGfE* _M0L5to__xS3821 = _M0L4varsS1189->$2;
      int32_t _M0L3valS3822 = _M0L1iS1192->$0;
      float _M0L6_2atmpS3814;
      struct _M0TPB5ArrayGfE* _M0L5to__xS3819;
      int32_t _M0L3valS3820;
      float _M0L6_2atmpS3818;
      float _M0L6_2atmpS3817;
      float _M0L6_2atmpS3816;
      float _M0L6_2atmpS3815;
      float _M0L6_2atmpS3813;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3823;
      int32_t _M0L3valS3824;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3833;
      int32_t _M0L3valS3834;
      float _M0L6_2atmpS3826;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3831;
      int32_t _M0L3valS3832;
      float _M0L6_2atmpS3830;
      float _M0L6_2atmpS3829;
      float _M0L6_2atmpS3828;
      float _M0L6_2atmpS3827;
      float _M0L6_2atmpS3825;
      int32_t _M0L3valS3835;
      int32_t _M0L3valS3849;
      int32_t _M0L6_2atmpS3848;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3814
      = _M0MPC15array5Array2atGfE(_M0L5to__xS3821, _M0L3valS3822);
      _M0L5to__xS3819 = _M0L4varsS1189->$2;
      _M0L3valS3820 = _M0L1iS1192->$0;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3818
      = _M0MPC15array5Array2atGfE(_M0L5to__xS3819, _M0L3valS3820);
      _M0L6_2atmpS3817 = -_M0L6_2atmpS3818;
      _M0L6_2atmpS3816 = _M0L2dtS1190 * _M0L6_2atmpS3817;
      _M0L6_2atmpS3815 = _M0L6_2atmpS3816 * _M0L11inv__tau__xS1185;
      _M0L6_2atmpS3813 = _M0L6_2atmpS3814 + _M0L6_2atmpS3815;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__xS3811, _M0L3valS3812, _M0L6_2atmpS3813);
      _M0L5to__yS3823 = _M0L4varsS1189->$3;
      _M0L3valS3824 = _M0L1iS1192->$0;
      _M0L5to__yS3833 = _M0L4varsS1189->$3;
      _M0L3valS3834 = _M0L1iS1192->$0;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3826
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3833, _M0L3valS3834);
      _M0L5to__yS3831 = _M0L4varsS1189->$3;
      _M0L3valS3832 = _M0L1iS1192->$0;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3830
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3831, _M0L3valS3832);
      _M0L6_2atmpS3829 = -_M0L6_2atmpS3830;
      _M0L6_2atmpS3828 = _M0L2dtS1190 * _M0L6_2atmpS3829;
      _M0L6_2atmpS3827 = _M0L6_2atmpS3828 * _M0L11inv__tau__yS1187;
      _M0L6_2atmpS3825 = _M0L6_2atmpS3826 + _M0L6_2atmpS3827;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__yS3823, _M0L3valS3824, _M0L6_2atmpS3825);
      _M0L3valS3835 = _M0L1iS1192->$0;
      #line 1328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1184, _M0L3valS3835)) {
        struct _M0TPB5ArrayGfE* _M0L5to__xS3836 = _M0L4varsS1189->$2;
        int32_t _M0L3valS3837 = _M0L1iS1192->$0;
        struct _M0TPB5ArrayGfE* _M0L5to__xS3840 = _M0L4varsS1189->$2;
        int32_t _M0L3valS3841 = _M0L1iS1192->$0;
        float _M0L6_2atmpS3839;
        float _M0L6_2atmpS3838;
        struct _M0TPB5ArrayGfE* _M0L5to__yS3842;
        int32_t _M0L3valS3843;
        struct _M0TPB5ArrayGfE* _M0L5to__yS3846;
        int32_t _M0L3valS3847;
        float _M0L6_2atmpS3845;
        float _M0L6_2atmpS3844;
        #line 1329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3839
        = _M0MPC15array5Array2atGfE(_M0L5to__xS3840, _M0L3valS3841);
        _M0L6_2atmpS3838 = _M0L6_2atmpS3839 + 0x1p+0f;
        #line 1329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__xS3836, _M0L3valS3837, _M0L6_2atmpS3838);
        _M0L5to__yS3842 = _M0L4varsS1189->$3;
        _M0L3valS3843 = _M0L1iS1192->$0;
        _M0L5to__yS3846 = _M0L4varsS1189->$3;
        _M0L3valS3847 = _M0L1iS1192->$0;
        #line 1330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3845
        = _M0MPC15array5Array2atGfE(_M0L5to__yS3846, _M0L3valS3847);
        _M0L6_2atmpS3844 = _M0L6_2atmpS3845 + 0x1p+0f;
        #line 1330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__yS3842, _M0L3valS3843, _M0L6_2atmpS3844);
      }
      _M0L3valS3849 = _M0L1iS1192->$0;
      _M0L6_2atmpS3848 = _M0L3valS3849 + 1;
      _M0L1iS1192->$0 = _M0L6_2atmpS3848;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1192);
    }
    break;
  }
  _M0L4a__xS3896 = _M0L5paramS1186->$0;
  _M0L6tau__xS3898 = _M0L5paramS1186->$2;
  _M0L6_2atmpS3897 = 0x1p+1f * _M0L6tau__xS3898;
  _M0L7coef__xS1194 = _M0L4a__xS3896 / _M0L6_2atmpS3897;
  _M0L4a__yS3893 = _M0L5paramS1186->$1;
  _M0L6tau__yS3895 = _M0L5paramS1186->$3;
  _M0L6_2atmpS3894 = 0x1p+1f * _M0L6tau__yS3895;
  _M0L7coef__yS1195 = _M0L4a__yS3893 / _M0L6_2atmpS3894;
  _M0L1jS1188->$0 = 0;
  while (1) {
    int32_t _M0L3valS3850 = _M0L1jS1188->$0;
    if (_M0L3valS3850 < _M0L6n__preS1181) {
      int32_t _M0L3valS3892 = _M0L1jS1188->$0;
      int32_t _M0L5startS1196;
      int32_t _M0L3valS3891;
      int32_t _M0L6_2atmpS3890;
      int32_t _M0L3endS1198;
      int32_t _M0L3valS3889;
      int32_t _M0L10pre__firedS1199;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3887;
      int32_t _M0L3valS3888;
      float _M0L8tr__x__jS1200;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3885;
      int32_t _M0L3valS3886;
      float _M0L8tr__y__jS1201;
      struct _M0TPB8MutLocalGiE* _M0L1sS1202;
      int32_t _M0L3valS3884;
      int32_t _M0L6_2atmpS3883;
      #line 1346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1196
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1197, _M0L3valS3892);
      _M0L3valS3891 = _M0L1jS1188->$0;
      _M0L6_2atmpS3890 = _M0L3valS3891 + 1;
      #line 1347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1198
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1197, _M0L6_2atmpS3890);
      _M0L3valS3889 = _M0L1jS1188->$0;
      #line 1348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L10pre__firedS1199
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1182, _M0L3valS3889);
      _M0L5tr__xS3887 = _M0L4varsS1189->$0;
      _M0L3valS3888 = _M0L1jS1188->$0;
      #line 1349 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8tr__x__jS1200
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3887, _M0L3valS3888);
      _M0L5tr__yS3885 = _M0L4varsS1189->$1;
      _M0L3valS3886 = _M0L1jS1188->$0;
      #line 1350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8tr__y__jS1201
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS3885, _M0L3valS3886);
      _M0L1sS1202
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1202)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1202->$0 = _M0L5startS1196;
      while (1) {
        int32_t _M0L3valS3851 = _M0L1sS1202->$0;
        if (_M0L3valS3851 < _M0L3endS1198) {
          int32_t _M0L3valS3882 = _M0L1sS1202->$0;
          int32_t _M0L9post__idxS1203;
          int32_t _M0L11post__firedS1205;
          struct _M0TPB5ArrayGfE* _M0L5to__xS3881;
          float _M0L8to__x__iS1206;
          struct _M0TPB5ArrayGfE* _M0L5to__yS3880;
          float _M0L8to__y__iS1207;
          int32_t _M0L3valS3870;
          float _M0L6_2atmpS3868;
          float _M0L6w__minS3869;
          int32_t _M0L3valS3875;
          float _M0L6_2atmpS3873;
          float _M0L6w__maxS3874;
          int32_t _M0L3valS3879;
          int32_t _M0L6_2atmpS3878;
          #line 1353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1203
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1204, _M0L3valS3882);
          #line 1354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1205
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1184, _M0L9post__idxS1203);
          _M0L5to__xS3881 = _M0L4varsS1189->$2;
          #line 1355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8to__x__iS1206
          = _M0MPC15array5Array2atGfE(_M0L5to__xS3881, _M0L9post__idxS1203);
          _M0L5to__yS3880 = _M0L4varsS1189->$3;
          #line 1356 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8to__y__iS1207
          = _M0MPC15array5Array2atGfE(_M0L5to__yS3880, _M0L9post__idxS1203);
          if (_M0L10pre__firedS1199) {
            float _M0L10alpha__preS3858 = _M0L5paramS1186->$4;
            float _M0L6_2atmpS3859 = _M0L7coef__xS1194 * _M0L8to__x__iS1206;
            float _M0L6_2atmpS3856 = _M0L10alpha__preS3858 + _M0L6_2atmpS3859;
            float _M0L6_2atmpS3857 = _M0L7coef__yS1195 * _M0L8to__y__iS1207;
            float _M0L2dwS1208 = _M0L6_2atmpS3856 - _M0L6_2atmpS3857;
            int32_t _M0L3valS3852 = _M0L1sS1202->$0;
            int32_t _M0L3valS3855 = _M0L1sS1202->$0;
            float _M0L6_2atmpS3854;
            float _M0L6_2atmpS3853;
            #line 1359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3854
            = _M0MPC15array5Array2atGfE(_M0L1wS1209, _M0L3valS3855);
            _M0L6_2atmpS3853 = _M0L6_2atmpS3854 + _M0L2dwS1208;
            #line 1359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1209, _M0L3valS3852, _M0L6_2atmpS3853);
          }
          if (_M0L11post__firedS1205) {
            float _M0L11alpha__postS3866 = _M0L5paramS1186->$5;
            float _M0L6_2atmpS3867 = _M0L7coef__xS1194 * _M0L8tr__x__jS1200;
            float _M0L6_2atmpS3864 =
              _M0L11alpha__postS3866 + _M0L6_2atmpS3867;
            float _M0L6_2atmpS3865 = _M0L7coef__yS1195 * _M0L8tr__y__jS1201;
            float _M0L2dwS1210 = _M0L6_2atmpS3864 - _M0L6_2atmpS3865;
            int32_t _M0L3valS3860 = _M0L1sS1202->$0;
            int32_t _M0L3valS3863 = _M0L1sS1202->$0;
            float _M0L6_2atmpS3862;
            float _M0L6_2atmpS3861;
            #line 1363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3862
            = _M0MPC15array5Array2atGfE(_M0L1wS1209, _M0L3valS3863);
            _M0L6_2atmpS3861 = _M0L6_2atmpS3862 + _M0L2dwS1210;
            #line 1363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1209, _M0L3valS3860, _M0L6_2atmpS3861);
          }
          _M0L3valS3870 = _M0L1sS1202->$0;
          #line 1365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3868
          = _M0MPC15array5Array2atGfE(_M0L1wS1209, _M0L3valS3870);
          _M0L6w__minS3869 = _M0L5paramS1186->$7;
          if (_M0L6_2atmpS3868 < _M0L6w__minS3869) {
            int32_t _M0L3valS3871 = _M0L1sS1202->$0;
            float _M0L6w__minS3872 = _M0L5paramS1186->$7;
            #line 1365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1209, _M0L3valS3871, _M0L6w__minS3872);
          }
          _M0L3valS3875 = _M0L1sS1202->$0;
          #line 1366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3873
          = _M0MPC15array5Array2atGfE(_M0L1wS1209, _M0L3valS3875);
          _M0L6w__maxS3874 = _M0L5paramS1186->$6;
          if (_M0L6_2atmpS3873 > _M0L6w__maxS3874) {
            int32_t _M0L3valS3876 = _M0L1sS1202->$0;
            float _M0L6w__maxS3877 = _M0L5paramS1186->$6;
            #line 1366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1209, _M0L3valS3876, _M0L6w__maxS3877);
          }
          _M0L3valS3879 = _M0L1sS1202->$0;
          _M0L6_2atmpS3878 = _M0L3valS3879 + 1;
          _M0L1sS1202->$0 = _M0L6_2atmpS3878;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1202);
        }
        break;
      }
      _M0L3valS3884 = _M0L1jS1188->$0;
      _M0L6_2atmpS3883 = _M0L3valS3884 + 1;
      _M0L1jS1188->$0 = _M0L6_2atmpS3883;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1188);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22stdp__confavreux__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1177,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1154,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1156,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1174,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1168,
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS1162,
  struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L5paramS1159,
  float _M0L6t__nowS1163,
  float _M0L2dtS1158
) {
  int32_t _M0L6n__preS1153;
  int32_t _M0L7n__postS1155;
  float _M0L6_2atmpS3768;
  float _M0L8tau__preS3769;
  float _M0L6_2atmpS3767;
  float _M0L10decay__preS1157;
  float _M0L6_2atmpS3765;
  float _M0L9tau__postS3766;
  float _M0L6_2atmpS3764;
  float _M0L11decay__postS1160;
  struct _M0TPB8MutLocalGiE* _M0L1jS1161;
  struct _M0TPB8MutLocalGiE* _M0L1iS1165;
  #line 1080 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 1091 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1153 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1154);
  #line 1092 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1155 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1156);
  _M0L6_2atmpS3768 = -_M0L2dtS1158;
  _M0L8tau__preS3769 = _M0L5paramS1159->$5;
  _M0L6_2atmpS3767 = _M0L6_2atmpS3768 / _M0L8tau__preS3769;
  #line 1093 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10decay__preS1157 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3767);
  _M0L6_2atmpS3765 = -_M0L2dtS1158;
  _M0L9tau__postS3766 = _M0L5paramS1159->$6;
  _M0L6_2atmpS3764 = _M0L6_2atmpS3765 / _M0L9tau__postS3766;
  #line 1094 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L11decay__postS1160 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3764);
  _M0L1jS1161
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1161)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1161->$0 = 0;
  while (1) {
    int32_t _M0L3valS3684 = _M0L1jS1161->$0;
    if (_M0L3valS3684 < _M0L6n__preS1153) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS3685 = _M0L4varsS1162->$0;
      int32_t _M0L3valS3686 = _M0L1jS1161->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3689 = _M0L4varsS1162->$0;
      int32_t _M0L3valS3690 = _M0L1jS1161->$0;
      float _M0L6_2atmpS3688;
      float _M0L6_2atmpS3687;
      int32_t _M0L3valS3691;
      int32_t _M0L3valS3701;
      int32_t _M0L6_2atmpS3700;
      #line 1098 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3688
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3689, _M0L3valS3690);
      _M0L6_2atmpS3687 = _M0L6_2atmpS3688 * _M0L10decay__preS1157;
      #line 1098 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS3685, _M0L3valS3686, _M0L6_2atmpS3687);
      _M0L3valS3691 = _M0L1jS1161->$0;
      #line 1099 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1154, _M0L3valS3691)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS3692 = _M0L4varsS1162->$0;
        int32_t _M0L3valS3693 = _M0L1jS1161->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS3696 = _M0L4varsS1162->$0;
        int32_t _M0L3valS3697 = _M0L1jS1161->$0;
        float _M0L6_2atmpS3695;
        float _M0L6_2atmpS3694;
        struct _M0TPB5ArrayGfE* _M0L9last__preS3698;
        int32_t _M0L3valS3699;
        #line 1100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3695
        = _M0MPC15array5Array2atGfE(_M0L4tpreS3696, _M0L3valS3697);
        _M0L6_2atmpS3694 = _M0L6_2atmpS3695 + 0x1p+0f;
        #line 1100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS3692, _M0L3valS3693, _M0L6_2atmpS3694);
        _M0L9last__preS3698 = _M0L4varsS1162->$2;
        _M0L3valS3699 = _M0L1jS1161->$0;
        #line 1101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L9last__preS3698, _M0L3valS3699, _M0L6t__nowS1163);
      }
      _M0L3valS3701 = _M0L1jS1161->$0;
      _M0L6_2atmpS3700 = _M0L3valS3701 + 1;
      _M0L1jS1161->$0 = _M0L6_2atmpS3700;
      continue;
    }
    break;
  }
  _M0L1iS1165
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1165)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1165->$0 = 0;
  while (1) {
    int32_t _M0L3valS3702 = _M0L1iS1165->$0;
    if (_M0L3valS3702 < _M0L7n__postS1155) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS3703 = _M0L4varsS1162->$1;
      int32_t _M0L3valS3704 = _M0L1iS1165->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3707 = _M0L4varsS1162->$1;
      int32_t _M0L3valS3708 = _M0L1iS1165->$0;
      float _M0L6_2atmpS3706;
      float _M0L6_2atmpS3705;
      int32_t _M0L3valS3709;
      int32_t _M0L3valS3719;
      int32_t _M0L6_2atmpS3718;
      #line 1107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3706
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3707, _M0L3valS3708);
      _M0L6_2atmpS3705 = _M0L6_2atmpS3706 * _M0L11decay__postS1160;
      #line 1107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS3703, _M0L3valS3704, _M0L6_2atmpS3705);
      _M0L3valS3709 = _M0L1iS1165->$0;
      #line 1108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1156, _M0L3valS3709)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS3710 = _M0L4varsS1162->$1;
        int32_t _M0L3valS3711 = _M0L1iS1165->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS3714 = _M0L4varsS1162->$1;
        int32_t _M0L3valS3715 = _M0L1iS1165->$0;
        float _M0L6_2atmpS3713;
        float _M0L6_2atmpS3712;
        struct _M0TPB5ArrayGfE* _M0L10last__postS3716;
        int32_t _M0L3valS3717;
        #line 1109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3713
        = _M0MPC15array5Array2atGfE(_M0L5tpostS3714, _M0L3valS3715);
        _M0L6_2atmpS3712 = _M0L6_2atmpS3713 + 0x1p+0f;
        #line 1109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS3710, _M0L3valS3711, _M0L6_2atmpS3712);
        _M0L10last__postS3716 = _M0L4varsS1162->$3;
        _M0L3valS3717 = _M0L1iS1165->$0;
        #line 1110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L10last__postS3716, _M0L3valS3717, _M0L6t__nowS1163);
      }
      _M0L3valS3719 = _M0L1iS1165->$0;
      _M0L6_2atmpS3718 = _M0L3valS3719 + 1;
      _M0L1iS1165->$0 = _M0L6_2atmpS3718;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1165);
    }
    break;
  }
  _M0L1jS1161->$0 = 0;
  while (1) {
    int32_t _M0L3valS3720 = _M0L1jS1161->$0;
    if (_M0L3valS3720 < _M0L6n__preS1153) {
      int32_t _M0L3valS3763 = _M0L1jS1161->$0;
      int32_t _M0L5startS1167;
      int32_t _M0L3valS3762;
      int32_t _M0L6_2atmpS3761;
      int32_t _M0L3endS1169;
      int32_t _M0L3valS3760;
      int32_t _M0L10pre__firedS1170;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3758;
      int32_t _M0L3valS3759;
      float _M0L7tpre__jS1171;
      struct _M0TPB8MutLocalGiE* _M0L1sS1172;
      int32_t _M0L3valS3757;
      int32_t _M0L6_2atmpS3756;
      #line 1120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1167
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1168, _M0L3valS3763);
      _M0L3valS3762 = _M0L1jS1161->$0;
      _M0L6_2atmpS3761 = _M0L3valS3762 + 1;
      #line 1121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1169
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1168, _M0L6_2atmpS3761);
      _M0L3valS3760 = _M0L1jS1161->$0;
      #line 1122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L10pre__firedS1170
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1154, _M0L3valS3760);
      _M0L4tpreS3758 = _M0L4varsS1162->$0;
      _M0L3valS3759 = _M0L1jS1161->$0;
      #line 1123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L7tpre__jS1171
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3758, _M0L3valS3759);
      _M0L1sS1172
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1172)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1172->$0 = _M0L5startS1167;
      while (1) {
        int32_t _M0L3valS3721 = _M0L1sS1172->$0;
        if (_M0L3valS3721 < _M0L3endS1169) {
          int32_t _M0L3valS3755 = _M0L1sS1172->$0;
          int32_t _M0L9post__idxS1173;
          int32_t _M0L11post__firedS1175;
          struct _M0TPB5ArrayGfE* _M0L5tpostS3754;
          float _M0L8tpost__iS1176;
          int32_t _M0L3valS3744;
          float _M0L6_2atmpS3742;
          float _M0L6w__minS3743;
          int32_t _M0L3valS3749;
          float _M0L6_2atmpS3747;
          float _M0L6w__maxS3748;
          int32_t _M0L3valS3753;
          int32_t _M0L6_2atmpS3752;
          #line 1126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1173
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1174, _M0L3valS3755);
          #line 1127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1175
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1156, _M0L9post__idxS1173);
          _M0L5tpostS3754 = _M0L4varsS1162->$1;
          #line 1128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8tpost__iS1176
          = _M0MPC15array5Array2atGfE(_M0L5tpostS3754, _M0L9post__idxS1173);
          if (_M0L10pre__firedS1170) {
            int32_t _M0L3valS3722 = _M0L1sS1172->$0;
            int32_t _M0L3valS3731 = _M0L1sS1172->$0;
            float _M0L6_2atmpS3724;
            float _M0L3etaS3726;
            float _M0L5kappaS3730;
            float _M0L6_2atmpS3728;
            float _M0L5alphaS3729;
            float _M0L6_2atmpS3727;
            float _M0L6_2atmpS3725;
            float _M0L6_2atmpS3723;
            #line 1131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3724
            = _M0MPC15array5Array2atGfE(_M0L1wS1177, _M0L3valS3731);
            _M0L3etaS3726 = _M0L5paramS1159->$0;
            _M0L5kappaS3730 = _M0L5paramS1159->$3;
            _M0L6_2atmpS3728 = _M0L5kappaS3730 * _M0L8tpost__iS1176;
            _M0L5alphaS3729 = _M0L5paramS1159->$1;
            _M0L6_2atmpS3727 = _M0L6_2atmpS3728 + _M0L5alphaS3729;
            _M0L6_2atmpS3725 = _M0L3etaS3726 * _M0L6_2atmpS3727;
            _M0L6_2atmpS3723 = _M0L6_2atmpS3724 + _M0L6_2atmpS3725;
            #line 1131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1177, _M0L3valS3722, _M0L6_2atmpS3723);
          }
          if (_M0L11post__firedS1175) {
            int32_t _M0L3valS3732 = _M0L1sS1172->$0;
            int32_t _M0L3valS3741 = _M0L1sS1172->$0;
            float _M0L6_2atmpS3734;
            float _M0L3etaS3736;
            float _M0L5gammaS3740;
            float _M0L6_2atmpS3738;
            float _M0L4betaS3739;
            float _M0L6_2atmpS3737;
            float _M0L6_2atmpS3735;
            float _M0L6_2atmpS3733;
            #line 1135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3734
            = _M0MPC15array5Array2atGfE(_M0L1wS1177, _M0L3valS3741);
            _M0L3etaS3736 = _M0L5paramS1159->$0;
            _M0L5gammaS3740 = _M0L5paramS1159->$4;
            _M0L6_2atmpS3738 = _M0L5gammaS3740 * _M0L7tpre__jS1171;
            _M0L4betaS3739 = _M0L5paramS1159->$2;
            _M0L6_2atmpS3737 = _M0L6_2atmpS3738 + _M0L4betaS3739;
            _M0L6_2atmpS3735 = _M0L3etaS3736 * _M0L6_2atmpS3737;
            _M0L6_2atmpS3733 = _M0L6_2atmpS3734 + _M0L6_2atmpS3735;
            #line 1135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1177, _M0L3valS3732, _M0L6_2atmpS3733);
          }
          _M0L3valS3744 = _M0L1sS1172->$0;
          #line 1138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3742
          = _M0MPC15array5Array2atGfE(_M0L1wS1177, _M0L3valS3744);
          _M0L6w__minS3743 = _M0L5paramS1159->$8;
          if (_M0L6_2atmpS3742 < _M0L6w__minS3743) {
            int32_t _M0L3valS3745 = _M0L1sS1172->$0;
            float _M0L6w__minS3746 = _M0L5paramS1159->$8;
            #line 1138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1177, _M0L3valS3745, _M0L6w__minS3746);
          }
          _M0L3valS3749 = _M0L1sS1172->$0;
          #line 1139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3747
          = _M0MPC15array5Array2atGfE(_M0L1wS1177, _M0L3valS3749);
          _M0L6w__maxS3748 = _M0L5paramS1159->$7;
          if (_M0L6_2atmpS3747 > _M0L6w__maxS3748) {
            int32_t _M0L3valS3750 = _M0L1sS1172->$0;
            float _M0L6w__maxS3751 = _M0L5paramS1159->$7;
            #line 1139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1177, _M0L3valS3750, _M0L6w__maxS3751);
          }
          _M0L3valS3753 = _M0L1sS1172->$0;
          _M0L6_2atmpS3752 = _M0L3valS3753 + 1;
          _M0L1sS1172->$0 = _M0L6_2atmpS3752;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1172);
        }
        break;
      }
      _M0L3valS3757 = _M0L1jS1161->$0;
      _M0L6_2atmpS3756 = _M0L3valS3757 + 1;
      _M0L1jS1161->$0 = _M0L6_2atmpS3756;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1161);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt10stdp__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1150,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1130,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1132,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1147,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1143,
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS1128,
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS1135,
  float _M0L6t__nowS1138,
  float _M0L2dtS1134
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3601;
  int32_t _M0L6_2atmpS3600;
  int32_t _if__result_5795;
  int32_t _M0L6n__preS1129;
  int32_t _M0L7n__postS1131;
  float _M0L6_2atmpS3682;
  float _M0L8tau__preS3683;
  float _M0L6_2atmpS3681;
  float _M0L10decay__preS1133;
  float _M0L6_2atmpS3679;
  float _M0L9tau__postS3680;
  float _M0L6_2atmpS3678;
  float _M0L11decay__postS1136;
  struct _M0TPB8MutLocalGiE* _M0L1jS1137;
  struct _M0TPB8MutLocalGiE* _M0L1iS1140;
  #line 905 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6activeS3601 = _M0L4varsS1128->$4;
  #line 917 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3600 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3601);
  if (_M0L6_2atmpS3600 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3599 = _M0L4varsS1128->$4;
    int32_t _M0L6_2atmpS3598;
    #line 917 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS3598 = _M0MPC15array5Array2atGbE(_M0L6activeS3599, 0);
    _if__result_5795 = !_M0L6_2atmpS3598;
  } else {
    _if__result_5795 = 0;
  }
  if (_if__result_5795) {
    return 0;
  }
  #line 921 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1129 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1130);
  #line 922 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1131 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1132);
  _M0L6_2atmpS3682 = -_M0L2dtS1134;
  _M0L8tau__preS3683 = _M0L5paramS1135->$2;
  _M0L6_2atmpS3681 = _M0L6_2atmpS3682 / _M0L8tau__preS3683;
  #line 923 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10decay__preS1133 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3681);
  _M0L6_2atmpS3679 = -_M0L2dtS1134;
  _M0L9tau__postS3680 = _M0L5paramS1135->$3;
  _M0L6_2atmpS3678 = _M0L6_2atmpS3679 / _M0L9tau__postS3680;
  #line 924 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L11decay__postS1136 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3678);
  _M0L1jS1137
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1137)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1137->$0 = 0;
  while (1) {
    int32_t _M0L3valS3602 = _M0L1jS1137->$0;
    if (_M0L3valS3602 < _M0L6n__preS1129) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS3603 = _M0L4varsS1128->$0;
      int32_t _M0L3valS3604 = _M0L1jS1137->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3607 = _M0L4varsS1128->$0;
      int32_t _M0L3valS3608 = _M0L1jS1137->$0;
      float _M0L6_2atmpS3606;
      float _M0L6_2atmpS3605;
      int32_t _M0L3valS3609;
      int32_t _M0L3valS3620;
      int32_t _M0L6_2atmpS3619;
      #line 927 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3606
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3607, _M0L3valS3608);
      _M0L6_2atmpS3605 = _M0L6_2atmpS3606 * _M0L10decay__preS1133;
      #line 927 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS3603, _M0L3valS3604, _M0L6_2atmpS3605);
      _M0L3valS3609 = _M0L1jS1137->$0;
      #line 928 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1130, _M0L3valS3609)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS3610 = _M0L4varsS1128->$0;
        int32_t _M0L3valS3611 = _M0L1jS1137->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS3615 = _M0L4varsS1128->$0;
        int32_t _M0L3valS3616 = _M0L1jS1137->$0;
        float _M0L6_2atmpS3613;
        float _M0L6a__preS3614;
        float _M0L6_2atmpS3612;
        struct _M0TPB5ArrayGfE* _M0L9last__preS3617;
        int32_t _M0L3valS3618;
        #line 929 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3613
        = _M0MPC15array5Array2atGfE(_M0L4tpreS3615, _M0L3valS3616);
        _M0L6a__preS3614 = _M0L5paramS1135->$0;
        _M0L6_2atmpS3612 = _M0L6_2atmpS3613 + _M0L6a__preS3614;
        #line 929 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS3610, _M0L3valS3611, _M0L6_2atmpS3612);
        _M0L9last__preS3617 = _M0L4varsS1128->$2;
        _M0L3valS3618 = _M0L1jS1137->$0;
        #line 930 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L9last__preS3617, _M0L3valS3618, _M0L6t__nowS1138);
      }
      _M0L3valS3620 = _M0L1jS1137->$0;
      _M0L6_2atmpS3619 = _M0L3valS3620 + 1;
      _M0L1jS1137->$0 = _M0L6_2atmpS3619;
      continue;
    }
    break;
  }
  _M0L1iS1140
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1140)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1140->$0 = 0;
  while (1) {
    int32_t _M0L3valS3621 = _M0L1iS1140->$0;
    if (_M0L3valS3621 < _M0L7n__postS1131) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS3622 = _M0L4varsS1128->$1;
      int32_t _M0L3valS3623 = _M0L1iS1140->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3626 = _M0L4varsS1128->$1;
      int32_t _M0L3valS3627 = _M0L1iS1140->$0;
      float _M0L6_2atmpS3625;
      float _M0L6_2atmpS3624;
      int32_t _M0L3valS3628;
      int32_t _M0L3valS3639;
      int32_t _M0L6_2atmpS3638;
      #line 936 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3625
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3626, _M0L3valS3627);
      _M0L6_2atmpS3624 = _M0L6_2atmpS3625 * _M0L11decay__postS1136;
      #line 936 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS3622, _M0L3valS3623, _M0L6_2atmpS3624);
      _M0L3valS3628 = _M0L1iS1140->$0;
      #line 937 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1132, _M0L3valS3628)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS3629 = _M0L4varsS1128->$1;
        int32_t _M0L3valS3630 = _M0L1iS1140->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS3634 = _M0L4varsS1128->$1;
        int32_t _M0L3valS3635 = _M0L1iS1140->$0;
        float _M0L6_2atmpS3632;
        float _M0L7a__postS3633;
        float _M0L6_2atmpS3631;
        struct _M0TPB5ArrayGfE* _M0L10last__postS3636;
        int32_t _M0L3valS3637;
        #line 938 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3632
        = _M0MPC15array5Array2atGfE(_M0L5tpostS3634, _M0L3valS3635);
        _M0L7a__postS3633 = _M0L5paramS1135->$1;
        _M0L6_2atmpS3631 = _M0L6_2atmpS3632 + _M0L7a__postS3633;
        #line 938 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS3629, _M0L3valS3630, _M0L6_2atmpS3631);
        _M0L10last__postS3636 = _M0L4varsS1128->$3;
        _M0L3valS3637 = _M0L1iS1140->$0;
        #line 939 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L10last__postS3636, _M0L3valS3637, _M0L6t__nowS1138);
      }
      _M0L3valS3639 = _M0L1iS1140->$0;
      _M0L6_2atmpS3638 = _M0L3valS3639 + 1;
      _M0L1iS1140->$0 = _M0L6_2atmpS3638;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1140);
    }
    break;
  }
  _M0L1jS1137->$0 = 0;
  while (1) {
    int32_t _M0L3valS3640 = _M0L1jS1137->$0;
    if (_M0L3valS3640 < _M0L6n__preS1129) {
      int32_t _M0L3valS3677 = _M0L1jS1137->$0;
      int32_t _M0L5startS1142;
      int32_t _M0L3valS3676;
      int32_t _M0L6_2atmpS3675;
      int32_t _M0L3endS1144;
      struct _M0TPB8MutLocalGiE* _M0L1sS1145;
      int32_t _M0L3valS3674;
      int32_t _M0L6_2atmpS3673;
      #line 947 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1142
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1143, _M0L3valS3677);
      _M0L3valS3676 = _M0L1jS1137->$0;
      _M0L6_2atmpS3675 = _M0L3valS3676 + 1;
      #line 948 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1144
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1143, _M0L6_2atmpS3675);
      _M0L1sS1145
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1145)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1145->$0 = _M0L5startS1142;
      while (1) {
        int32_t _M0L3valS3641 = _M0L1sS1145->$0;
        if (_M0L3valS3641 < _M0L3endS1144) {
          int32_t _M0L3valS3672 = _M0L1sS1145->$0;
          int32_t _M0L9post__idxS1146;
          int32_t _M0L3valS3671;
          int32_t _M0L10pre__firedS1148;
          int32_t _M0L11post__firedS1149;
          int32_t _M0L3valS3661;
          float _M0L6_2atmpS3659;
          float _M0L6w__minS3660;
          int32_t _M0L3valS3666;
          float _M0L6_2atmpS3664;
          float _M0L6w__maxS3665;
          int32_t _M0L3valS3670;
          int32_t _M0L6_2atmpS3669;
          #line 951 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1146
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1147, _M0L3valS3672);
          _M0L3valS3671 = _M0L1jS1137->$0;
          #line 952 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L10pre__firedS1148
          = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1130, _M0L3valS3671);
          #line 953 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1149
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1132, _M0L9post__idxS1146);
          if (_M0L10pre__firedS1148) {
            int32_t _M0L3valS3642 = _M0L1sS1145->$0;
            int32_t _M0L3valS3649 = _M0L1sS1145->$0;
            float _M0L6_2atmpS3644;
            float _M0L7a__postS3646;
            struct _M0TPB5ArrayGfE* _M0L5tpostS3648;
            float _M0L6_2atmpS3647;
            float _M0L6_2atmpS3645;
            float _M0L6_2atmpS3643;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3644
            = _M0MPC15array5Array2atGfE(_M0L1wS1150, _M0L3valS3649);
            _M0L7a__postS3646 = _M0L5paramS1135->$1;
            _M0L5tpostS3648 = _M0L4varsS1128->$1;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3647
            = _M0MPC15array5Array2atGfE(_M0L5tpostS3648, _M0L9post__idxS1146);
            _M0L6_2atmpS3645 = _M0L7a__postS3646 * _M0L6_2atmpS3647;
            _M0L6_2atmpS3643 = _M0L6_2atmpS3644 + _M0L6_2atmpS3645;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1150, _M0L3valS3642, _M0L6_2atmpS3643);
          }
          if (_M0L11post__firedS1149) {
            int32_t _M0L3valS3650 = _M0L1sS1145->$0;
            int32_t _M0L3valS3658 = _M0L1sS1145->$0;
            float _M0L6_2atmpS3652;
            float _M0L6a__preS3654;
            struct _M0TPB5ArrayGfE* _M0L4tpreS3656;
            int32_t _M0L3valS3657;
            float _M0L6_2atmpS3655;
            float _M0L6_2atmpS3653;
            float _M0L6_2atmpS3651;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3652
            = _M0MPC15array5Array2atGfE(_M0L1wS1150, _M0L3valS3658);
            _M0L6a__preS3654 = _M0L5paramS1135->$0;
            _M0L4tpreS3656 = _M0L4varsS1128->$0;
            _M0L3valS3657 = _M0L1jS1137->$0;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3655
            = _M0MPC15array5Array2atGfE(_M0L4tpreS3656, _M0L3valS3657);
            _M0L6_2atmpS3653 = _M0L6a__preS3654 * _M0L6_2atmpS3655;
            _M0L6_2atmpS3651 = _M0L6_2atmpS3652 + _M0L6_2atmpS3653;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1150, _M0L3valS3650, _M0L6_2atmpS3651);
          }
          _M0L3valS3661 = _M0L1sS1145->$0;
          #line 963 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3659
          = _M0MPC15array5Array2atGfE(_M0L1wS1150, _M0L3valS3661);
          _M0L6w__minS3660 = _M0L5paramS1135->$5;
          if (_M0L6_2atmpS3659 < _M0L6w__minS3660) {
            int32_t _M0L3valS3662 = _M0L1sS1145->$0;
            float _M0L6w__minS3663 = _M0L5paramS1135->$5;
            #line 963 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1150, _M0L3valS3662, _M0L6w__minS3663);
          }
          _M0L3valS3666 = _M0L1sS1145->$0;
          #line 964 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3664
          = _M0MPC15array5Array2atGfE(_M0L1wS1150, _M0L3valS3666);
          _M0L6w__maxS3665 = _M0L5paramS1135->$4;
          if (_M0L6_2atmpS3664 > _M0L6w__maxS3665) {
            int32_t _M0L3valS3667 = _M0L1sS1145->$0;
            float _M0L6w__maxS3668 = _M0L5paramS1135->$4;
            #line 964 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1150, _M0L3valS3667, _M0L6w__maxS3668);
          }
          _M0L3valS3670 = _M0L1sS1145->$0;
          _M0L6_2atmpS3669 = _M0L3valS3670 + 1;
          _M0L1sS1145->$0 = _M0L6_2atmpS3669;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1145);
        }
        break;
      }
      _M0L3valS3674 = _M0L1jS1137->$0;
      _M0L6_2atmpS3673 = _M0L3valS3674 + 1;
      _M0L1jS1137->$0 = _M0L6_2atmpS3673;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1137);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25stdp__antisymmetric__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1107,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1097,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1099,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1106,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1102,
  struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L4varsS1109,
  struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L5paramS1108,
  float _M0L2dtS1121
) {
  int32_t _M0L6n__preS1096;
  int32_t _M0L7n__postS1098;
  struct _M0TPB8MutLocalGiE* _M0L1jS1100;
  int32_t _M0L3nnzS1112;
  float _M0L4a__xS3596;
  float _M0L6tau__xS3597;
  float _M0L18a__x__over__tau__xS1113;
  struct _M0TPB8MutLocalGiE* _M0L2s2S1114;
  float _M0L6tau__xS3595;
  float _M0L11inv__tau__xS1118;
  float _M0L6tau__yS3594;
  float _M0L11inv__tau__yS1119;
  struct _M0TPB8MutLocalGiE* _M0L1iS1120;
  struct _M0TPB8MutLocalGiE* _M0L2s3S1126;
  #line 622 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 632 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1096 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1097);
  #line 633 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1098 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1099);
  _M0L1jS1100
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1100)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1100->$0 = 0;
  while (1) {
    int32_t _M0L3valS3494 = _M0L1jS1100->$0;
    if (_M0L3valS3494 < _M0L6n__preS1096) {
      int32_t _M0L3valS3495 = _M0L1jS1100->$0;
      int32_t _M0L3valS3516;
      int32_t _M0L6_2atmpS3515;
      #line 637 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1097, _M0L3valS3495)) {
        int32_t _M0L3valS3514 = _M0L1jS1100->$0;
        int32_t _M0L5startS1101;
        int32_t _M0L3valS3513;
        int32_t _M0L6_2atmpS3512;
        int32_t _M0L3endS1103;
        struct _M0TPB8MutLocalGiE* _M0L1sS1104;
        #line 638 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS1101
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1102, _M0L3valS3514);
        _M0L3valS3513 = _M0L1jS1100->$0;
        _M0L6_2atmpS3512 = _M0L3valS3513 + 1;
        #line 639 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3endS1103
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1102, _M0L6_2atmpS3512);
        _M0L1sS1104
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS1104)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS1104->$0 = _M0L5startS1101;
        while (1) {
          int32_t _M0L3valS3496 = _M0L1sS1104->$0;
          if (_M0L3valS3496 < _M0L3endS1103) {
            int32_t _M0L3valS3511 = _M0L1sS1104->$0;
            int32_t _M0L9post__idxS1105;
            int32_t _M0L3valS3497;
            int32_t _M0L3valS3508;
            float _M0L6_2atmpS3506;
            float _M0L10alpha__preS3507;
            float _M0L6_2atmpS3499;
            float _M0L4a__yS3504;
            float _M0L6tau__yS3505;
            float _M0L6_2atmpS3501;
            struct _M0TPB5ArrayGfE* _M0L5to__yS3503;
            float _M0L6_2atmpS3502;
            float _M0L6_2atmpS3500;
            float _M0L6_2atmpS3498;
            int32_t _M0L3valS3510;
            int32_t _M0L6_2atmpS3509;
            #line 642 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L9post__idxS1105
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1106, _M0L3valS3511);
            _M0L3valS3497 = _M0L1sS1104->$0;
            _M0L3valS3508 = _M0L1sS1104->$0;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3506
            = _M0MPC15array5Array2atGfE(_M0L1wS1107, _M0L3valS3508);
            _M0L10alpha__preS3507 = _M0L5paramS1108->$4;
            _M0L6_2atmpS3499 = _M0L6_2atmpS3506 + _M0L10alpha__preS3507;
            _M0L4a__yS3504 = _M0L5paramS1108->$1;
            _M0L6tau__yS3505 = _M0L5paramS1108->$3;
            _M0L6_2atmpS3501 = _M0L4a__yS3504 / _M0L6tau__yS3505;
            _M0L5to__yS3503 = _M0L4varsS1109->$1;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3502
            = _M0MPC15array5Array2atGfE(_M0L5to__yS3503, _M0L9post__idxS1105);
            _M0L6_2atmpS3500 = _M0L6_2atmpS3501 * _M0L6_2atmpS3502;
            _M0L6_2atmpS3498 = _M0L6_2atmpS3499 - _M0L6_2atmpS3500;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1107, _M0L3valS3497, _M0L6_2atmpS3498);
            _M0L3valS3510 = _M0L1sS1104->$0;
            _M0L6_2atmpS3509 = _M0L3valS3510 + 1;
            _M0L1sS1104->$0 = _M0L6_2atmpS3509;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS1104);
          }
          break;
        }
      }
      _M0L3valS3516 = _M0L1jS1100->$0;
      _M0L6_2atmpS3515 = _M0L3valS3516 + 1;
      _M0L1jS1100->$0 = _M0L6_2atmpS3515;
      continue;
    }
    break;
  }
  #line 650 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L3nnzS1112 = _M0MPC15array5Array6lengthGfE(_M0L1wS1107);
  _M0L4a__xS3596 = _M0L5paramS1108->$0;
  _M0L6tau__xS3597 = _M0L5paramS1108->$2;
  _M0L18a__x__over__tau__xS1113 = _M0L4a__xS3596 / _M0L6tau__xS3597;
  _M0L2s2S1114
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S1114)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S1114->$0 = 0;
  while (1) {
    int32_t _M0L3valS3517 = _M0L2s2S1114->$0;
    if (_M0L3valS3517 < _M0L3nnzS1112) {
      int32_t _M0L3valS3530 = _M0L2s2S1114->$0;
      int32_t _M0L9post__idxS1115;
      int32_t _M0L3valS3529;
      int32_t _M0L6_2atmpS3528;
      #line 654 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L9post__idxS1115
      = _M0MPC15array5Array2atGiE(_M0L6colptrS1106, _M0L3valS3530);
      #line 655 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (
        _M0MPC15array5Array2atGbE(_M0L10post__fireS1099, _M0L9post__idxS1115)
      ) {
        int32_t _M0L3valS3527 = _M0L2s2S1114->$0;
        int32_t _M0L6j__preS1116;
        int32_t _M0L3valS3518;
        int32_t _M0L3valS3526;
        float _M0L6_2atmpS3524;
        float _M0L11alpha__postS3525;
        float _M0L6_2atmpS3520;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3523;
        float _M0L6_2atmpS3522;
        float _M0L6_2atmpS3521;
        float _M0L6_2atmpS3519;
        #line 656 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6j__preS1116
        = _M0FP26RiantR8snn__mbt20find__pre__for__conn(_M0L6rowptrS1102, _M0L3valS3527);
        _M0L3valS3518 = _M0L2s2S1114->$0;
        _M0L3valS3526 = _M0L2s2S1114->$0;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3524
        = _M0MPC15array5Array2atGfE(_M0L1wS1107, _M0L3valS3526);
        _M0L11alpha__postS3525 = _M0L5paramS1108->$5;
        _M0L6_2atmpS3520 = _M0L6_2atmpS3524 + _M0L11alpha__postS3525;
        _M0L5tr__xS3523 = _M0L4varsS1109->$0;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3522
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3523, _M0L6j__preS1116);
        _M0L6_2atmpS3521 = _M0L18a__x__over__tau__xS1113 * _M0L6_2atmpS3522;
        _M0L6_2atmpS3519 = _M0L6_2atmpS3520 + _M0L6_2atmpS3521;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1107, _M0L3valS3518, _M0L6_2atmpS3519);
      }
      _M0L3valS3529 = _M0L2s2S1114->$0;
      _M0L6_2atmpS3528 = _M0L3valS3529 + 1;
      _M0L2s2S1114->$0 = _M0L6_2atmpS3528;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S1114);
    }
    break;
  }
  _M0L6tau__xS3595 = _M0L5paramS1108->$2;
  _M0L11inv__tau__xS1118 = 0x1p+0f / _M0L6tau__xS3595;
  _M0L6tau__yS3594 = _M0L5paramS1108->$3;
  _M0L11inv__tau__yS1119 = 0x1p+0f / _M0L6tau__yS3594;
  _M0L1iS1120
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1120)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1120->$0 = 0;
  while (1) {
    int32_t _M0L3valS3531 = _M0L1iS1120->$0;
    if (_M0L3valS3531 < _M0L7n__postS1098) {
      struct _M0TPB5ArrayGfE* _M0L5to__yS3532 = _M0L4varsS1109->$1;
      int32_t _M0L3valS3533 = _M0L1iS1120->$0;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3542 = _M0L4varsS1109->$1;
      int32_t _M0L3valS3543 = _M0L1iS1120->$0;
      float _M0L6_2atmpS3535;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3540;
      int32_t _M0L3valS3541;
      float _M0L6_2atmpS3539;
      float _M0L6_2atmpS3538;
      float _M0L6_2atmpS3537;
      float _M0L6_2atmpS3536;
      float _M0L6_2atmpS3534;
      int32_t _M0L3valS3545;
      int32_t _M0L6_2atmpS3544;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3535
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3542, _M0L3valS3543);
      _M0L5to__yS3540 = _M0L4varsS1109->$1;
      _M0L3valS3541 = _M0L1iS1120->$0;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3539
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3540, _M0L3valS3541);
      _M0L6_2atmpS3538 = -_M0L6_2atmpS3539;
      _M0L6_2atmpS3537 = _M0L2dtS1121 * _M0L6_2atmpS3538;
      _M0L6_2atmpS3536 = _M0L6_2atmpS3537 * _M0L11inv__tau__yS1119;
      _M0L6_2atmpS3534 = _M0L6_2atmpS3535 + _M0L6_2atmpS3536;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__yS3532, _M0L3valS3533, _M0L6_2atmpS3534);
      _M0L3valS3545 = _M0L1iS1120->$0;
      _M0L6_2atmpS3544 = _M0L3valS3545 + 1;
      _M0L1iS1120->$0 = _M0L6_2atmpS3544;
      continue;
    }
    break;
  }
  _M0L1jS1100->$0 = 0;
  while (1) {
    int32_t _M0L3valS3546 = _M0L1jS1100->$0;
    if (_M0L3valS3546 < _M0L6n__preS1096) {
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3547 = _M0L4varsS1109->$0;
      int32_t _M0L3valS3548 = _M0L1jS1100->$0;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3557 = _M0L4varsS1109->$0;
      int32_t _M0L3valS3558 = _M0L1jS1100->$0;
      float _M0L6_2atmpS3550;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3555;
      int32_t _M0L3valS3556;
      float _M0L6_2atmpS3554;
      float _M0L6_2atmpS3553;
      float _M0L6_2atmpS3552;
      float _M0L6_2atmpS3551;
      float _M0L6_2atmpS3549;
      int32_t _M0L3valS3560;
      int32_t _M0L6_2atmpS3559;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3550
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3557, _M0L3valS3558);
      _M0L5tr__xS3555 = _M0L4varsS1109->$0;
      _M0L3valS3556 = _M0L1jS1100->$0;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3554
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3555, _M0L3valS3556);
      _M0L6_2atmpS3553 = -_M0L6_2atmpS3554;
      _M0L6_2atmpS3552 = _M0L2dtS1121 * _M0L6_2atmpS3553;
      _M0L6_2atmpS3551 = _M0L6_2atmpS3552 * _M0L11inv__tau__xS1118;
      _M0L6_2atmpS3549 = _M0L6_2atmpS3550 + _M0L6_2atmpS3551;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__xS3547, _M0L3valS3548, _M0L6_2atmpS3549);
      _M0L3valS3560 = _M0L1jS1100->$0;
      _M0L6_2atmpS3559 = _M0L3valS3560 + 1;
      _M0L1jS1100->$0 = _M0L6_2atmpS3559;
      continue;
    }
    break;
  }
  _M0L1iS1120->$0 = 0;
  while (1) {
    int32_t _M0L3valS3561 = _M0L1iS1120->$0;
    if (_M0L3valS3561 < _M0L7n__postS1098) {
      int32_t _M0L3valS3562 = _M0L1iS1120->$0;
      int32_t _M0L3valS3570;
      int32_t _M0L6_2atmpS3569;
      #line 677 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1099, _M0L3valS3562)) {
        struct _M0TPB5ArrayGfE* _M0L5to__yS3563 = _M0L4varsS1109->$1;
        int32_t _M0L3valS3564 = _M0L1iS1120->$0;
        struct _M0TPB5ArrayGfE* _M0L5to__yS3567 = _M0L4varsS1109->$1;
        int32_t _M0L3valS3568 = _M0L1iS1120->$0;
        float _M0L6_2atmpS3566;
        float _M0L6_2atmpS3565;
        #line 678 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3566
        = _M0MPC15array5Array2atGfE(_M0L5to__yS3567, _M0L3valS3568);
        _M0L6_2atmpS3565 = _M0L6_2atmpS3566 + 0x1p+0f;
        #line 678 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__yS3563, _M0L3valS3564, _M0L6_2atmpS3565);
      }
      _M0L3valS3570 = _M0L1iS1120->$0;
      _M0L6_2atmpS3569 = _M0L3valS3570 + 1;
      _M0L1iS1120->$0 = _M0L6_2atmpS3569;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1120);
    }
    break;
  }
  _M0L1jS1100->$0 = 0;
  while (1) {
    int32_t _M0L3valS3571 = _M0L1jS1100->$0;
    if (_M0L3valS3571 < _M0L6n__preS1096) {
      int32_t _M0L3valS3572 = _M0L1jS1100->$0;
      int32_t _M0L3valS3580;
      int32_t _M0L6_2atmpS3579;
      #line 684 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1097, _M0L3valS3572)) {
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3573 = _M0L4varsS1109->$0;
        int32_t _M0L3valS3574 = _M0L1jS1100->$0;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3577 = _M0L4varsS1109->$0;
        int32_t _M0L3valS3578 = _M0L1jS1100->$0;
        float _M0L6_2atmpS3576;
        float _M0L6_2atmpS3575;
        #line 685 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3576
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3577, _M0L3valS3578);
        _M0L6_2atmpS3575 = _M0L6_2atmpS3576 + 0x1p+0f;
        #line 685 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__xS3573, _M0L3valS3574, _M0L6_2atmpS3575);
      }
      _M0L3valS3580 = _M0L1jS1100->$0;
      _M0L6_2atmpS3579 = _M0L3valS3580 + 1;
      _M0L1jS1100->$0 = _M0L6_2atmpS3579;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1100);
    }
    break;
  }
  _M0L2s3S1126
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s3S1126)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s3S1126->$0 = 0;
  while (1) {
    int32_t _M0L3valS3581 = _M0L2s3S1126->$0;
    if (_M0L3valS3581 < _M0L3nnzS1112) {
      int32_t _M0L3valS3584 = _M0L2s3S1126->$0;
      float _M0L6_2atmpS3582;
      float _M0L6w__minS3583;
      int32_t _M0L3valS3593;
      int32_t _M0L6_2atmpS3592;
      #line 692 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3582
      = _M0MPC15array5Array2atGfE(_M0L1wS1107, _M0L3valS3584);
      _M0L6w__minS3583 = _M0L5paramS1108->$7;
      if (_M0L6_2atmpS3582 < _M0L6w__minS3583) {
        int32_t _M0L3valS3585 = _M0L2s3S1126->$0;
        float _M0L6w__minS3586 = _M0L5paramS1108->$7;
        #line 693 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1107, _M0L3valS3585, _M0L6w__minS3586);
      } else {
        int32_t _M0L3valS3589 = _M0L2s3S1126->$0;
        float _M0L6_2atmpS3587;
        float _M0L6w__maxS3588;
        #line 694 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3587
        = _M0MPC15array5Array2atGfE(_M0L1wS1107, _M0L3valS3589);
        _M0L6w__maxS3588 = _M0L5paramS1108->$6;
        if (_M0L6_2atmpS3587 > _M0L6w__maxS3588) {
          int32_t _M0L3valS3590 = _M0L2s3S1126->$0;
          float _M0L6w__maxS3591 = _M0L5paramS1108->$6;
          #line 695 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0MPC15array5Array3setGfE(_M0L1wS1107, _M0L3valS3590, _M0L6w__maxS3591);
        }
      }
      _M0L3valS3593 = _M0L2s3S1126->$0;
      _M0L6_2atmpS3592 = _M0L3valS3593 + 1;
      _M0L2s3S1126->$0 = _M0L6_2atmpS3592;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s3S1126);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt24stdp__mexican__hat__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1082,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1058,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1060,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1077,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1073,
  struct _M0TPB5ArrayGfE* _M0L4tpreS1068,
  struct _M0TPB5ArrayGfE* _M0L5tpostS1064,
  struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L5paramS1062,
  float _M0L2dtS1065
) {
  int32_t _M0L6n__preS1057;
  int32_t _M0L7n__postS1059;
  float _M0L3tauS3493;
  float _M0L8inv__tauS1061;
  struct _M0TPB8MutLocalGiE* _M0L1iS1063;
  struct _M0TPB8MutLocalGiE* _M0L1jS1067;
  int32_t _M0L3nnzS1085;
  struct _M0TPB8MutLocalGiE* _M0L2s2S1086;
  struct _M0TPB8MutLocalGiE* _M0L2s3S1094;
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 461 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1057 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1058);
  #line 462 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1059 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1060);
  _M0L3tauS3493 = _M0L5paramS1062->$1;
  _M0L8inv__tauS1061 = 0x1p+0f / _M0L3tauS3493;
  _M0L1iS1063
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1063)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1063->$0 = 0;
  while (1) {
    int32_t _M0L3valS3407 = _M0L1iS1063->$0;
    if (_M0L3valS3407 < _M0L7n__postS1059) {
      int32_t _M0L3valS3408 = _M0L1iS1063->$0;
      int32_t _M0L3valS3416 = _M0L1iS1063->$0;
      float _M0L6_2atmpS3410;
      int32_t _M0L3valS3415;
      float _M0L6_2atmpS3414;
      float _M0L6_2atmpS3413;
      float _M0L6_2atmpS3412;
      float _M0L6_2atmpS3411;
      float _M0L6_2atmpS3409;
      int32_t _M0L3valS3418;
      int32_t _M0L6_2atmpS3417;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3410
      = _M0MPC15array5Array2atGfE(_M0L5tpostS1064, _M0L3valS3416);
      _M0L3valS3415 = _M0L1iS1063->$0;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3414
      = _M0MPC15array5Array2atGfE(_M0L5tpostS1064, _M0L3valS3415);
      _M0L6_2atmpS3413 = -_M0L6_2atmpS3414;
      _M0L6_2atmpS3412 = _M0L2dtS1065 * _M0L6_2atmpS3413;
      _M0L6_2atmpS3411 = _M0L6_2atmpS3412 * _M0L8inv__tauS1061;
      _M0L6_2atmpS3409 = _M0L6_2atmpS3410 + _M0L6_2atmpS3411;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS1064, _M0L3valS3408, _M0L6_2atmpS3409);
      _M0L3valS3418 = _M0L1iS1063->$0;
      _M0L6_2atmpS3417 = _M0L3valS3418 + 1;
      _M0L1iS1063->$0 = _M0L6_2atmpS3417;
      continue;
    }
    break;
  }
  _M0L1jS1067
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1067)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1067->$0 = 0;
  while (1) {
    int32_t _M0L3valS3419 = _M0L1jS1067->$0;
    if (_M0L3valS3419 < _M0L6n__preS1057) {
      int32_t _M0L3valS3420 = _M0L1jS1067->$0;
      int32_t _M0L3valS3428 = _M0L1jS1067->$0;
      float _M0L6_2atmpS3422;
      int32_t _M0L3valS3427;
      float _M0L6_2atmpS3426;
      float _M0L6_2atmpS3425;
      float _M0L6_2atmpS3424;
      float _M0L6_2atmpS3423;
      float _M0L6_2atmpS3421;
      int32_t _M0L3valS3430;
      int32_t _M0L6_2atmpS3429;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3422
      = _M0MPC15array5Array2atGfE(_M0L4tpreS1068, _M0L3valS3428);
      _M0L3valS3427 = _M0L1jS1067->$0;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3426
      = _M0MPC15array5Array2atGfE(_M0L4tpreS1068, _M0L3valS3427);
      _M0L6_2atmpS3425 = -_M0L6_2atmpS3426;
      _M0L6_2atmpS3424 = _M0L2dtS1065 * _M0L6_2atmpS3425;
      _M0L6_2atmpS3423 = _M0L6_2atmpS3424 * _M0L8inv__tauS1061;
      _M0L6_2atmpS3421 = _M0L6_2atmpS3422 + _M0L6_2atmpS3423;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS1068, _M0L3valS3420, _M0L6_2atmpS3421);
      _M0L3valS3430 = _M0L1jS1067->$0;
      _M0L6_2atmpS3429 = _M0L3valS3430 + 1;
      _M0L1jS1067->$0 = _M0L6_2atmpS3429;
      continue;
    }
    break;
  }
  _M0L1iS1063->$0 = 0;
  while (1) {
    int32_t _M0L3valS3431 = _M0L1iS1063->$0;
    if (_M0L3valS3431 < _M0L7n__postS1059) {
      int32_t _M0L3valS3432 = _M0L1iS1063->$0;
      int32_t _M0L3valS3438;
      int32_t _M0L6_2atmpS3437;
      #line 478 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1060, _M0L3valS3432)) {
        int32_t _M0L3valS3433 = _M0L1iS1063->$0;
        int32_t _M0L3valS3436 = _M0L1iS1063->$0;
        float _M0L6_2atmpS3435;
        float _M0L6_2atmpS3434;
        #line 479 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3435
        = _M0MPC15array5Array2atGfE(_M0L5tpostS1064, _M0L3valS3436);
        _M0L6_2atmpS3434 = _M0L6_2atmpS3435 + 0x1p+0f;
        #line 479 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS1064, _M0L3valS3433, _M0L6_2atmpS3434);
      }
      _M0L3valS3438 = _M0L1iS1063->$0;
      _M0L6_2atmpS3437 = _M0L3valS3438 + 1;
      _M0L1iS1063->$0 = _M0L6_2atmpS3437;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1063);
    }
    break;
  }
  _M0L1jS1067->$0 = 0;
  while (1) {
    int32_t _M0L3valS3439 = _M0L1jS1067->$0;
    if (_M0L3valS3439 < _M0L6n__preS1057) {
      int32_t _M0L3valS3440 = _M0L1jS1067->$0;
      int32_t _M0L3valS3446;
      int32_t _M0L6_2atmpS3445;
      #line 485 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1058, _M0L3valS3440)) {
        int32_t _M0L3valS3441 = _M0L1jS1067->$0;
        int32_t _M0L3valS3444 = _M0L1jS1067->$0;
        float _M0L6_2atmpS3443;
        float _M0L6_2atmpS3442;
        #line 486 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3443
        = _M0MPC15array5Array2atGfE(_M0L4tpreS1068, _M0L3valS3444);
        _M0L6_2atmpS3442 = _M0L6_2atmpS3443 + 0x1p+0f;
        #line 486 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS1068, _M0L3valS3441, _M0L6_2atmpS3442);
      }
      _M0L3valS3446 = _M0L1jS1067->$0;
      _M0L6_2atmpS3445 = _M0L3valS3446 + 1;
      _M0L1jS1067->$0 = _M0L6_2atmpS3445;
      continue;
    }
    break;
  }
  _M0L1jS1067->$0 = 0;
  while (1) {
    int32_t _M0L3valS3447 = _M0L1jS1067->$0;
    if (_M0L3valS3447 < _M0L6n__preS1057) {
      int32_t _M0L3valS3448 = _M0L1jS1067->$0;
      int32_t _M0L3valS3466;
      int32_t _M0L6_2atmpS3465;
      #line 493 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1058, _M0L3valS3448)) {
        int32_t _M0L3valS3464 = _M0L1jS1067->$0;
        int32_t _M0L5startS1072;
        int32_t _M0L3valS3463;
        int32_t _M0L6_2atmpS3462;
        int32_t _M0L3endS1074;
        struct _M0TPB8MutLocalGiE* _M0L1sS1075;
        #line 494 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS1072
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1073, _M0L3valS3464);
        _M0L3valS3463 = _M0L1jS1067->$0;
        _M0L6_2atmpS3462 = _M0L3valS3463 + 1;
        #line 495 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3endS1074
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1073, _M0L6_2atmpS3462);
        _M0L1sS1075
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS1075)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS1075->$0 = _M0L5startS1072;
        while (1) {
          int32_t _M0L3valS3449 = _M0L1sS1075->$0;
          if (_M0L3valS3449 < _M0L3endS1074) {
            int32_t _M0L3valS3461 = _M0L1sS1075->$0;
            int32_t _M0L9post__idxS1076;
            int32_t _M0L3valS3460;
            float _M0L6_2atmpS3458;
            float _M0L6_2atmpS3459;
            float _M0L5ratioS1078;
            float _M0L3lnxS1079;
            float _M0L1xS1080;
            float _M0L1aS3456;
            float _M0L6_2atmpS3457;
            float _M0L2dwS1081;
            int32_t _M0L3valS3450;
            int32_t _M0L3valS3453;
            float _M0L6_2atmpS3452;
            float _M0L6_2atmpS3451;
            int32_t _M0L3valS3455;
            int32_t _M0L6_2atmpS3454;
            #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L9post__idxS1076
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1077, _M0L3valS3461);
            _M0L3valS3460 = _M0L1jS1067->$0;
            #line 499 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3458
            = _M0MPC15array5Array2atGfE(_M0L4tpreS1068, _M0L3valS3460);
            #line 499 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3459
            = _M0MPC15array5Array2atGfE(_M0L5tpostS1064, _M0L9post__idxS1076);
            _M0L5ratioS1078 = _M0L6_2atmpS3458 / _M0L6_2atmpS3459;
            #line 500 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L3lnxS1079 = _M0FP26RiantR8snn__mbt4logf(_M0L5ratioS1078);
            _M0L1xS1080 = _M0L3lnxS1079 * _M0L3lnxS1079;
            _M0L1aS3456 = _M0L5paramS1062->$0;
            #line 502 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3457
            = _M0FP26RiantR8snn__mbt20mexican__hat__kernel(_M0L1xS1080);
            _M0L2dwS1081 = _M0L1aS3456 * _M0L6_2atmpS3457;
            _M0L3valS3450 = _M0L1sS1075->$0;
            _M0L3valS3453 = _M0L1sS1075->$0;
            #line 503 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3452
            = _M0MPC15array5Array2atGfE(_M0L1wS1082, _M0L3valS3453);
            _M0L6_2atmpS3451 = _M0L6_2atmpS3452 + _M0L2dwS1081;
            #line 503 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1082, _M0L3valS3450, _M0L6_2atmpS3451);
            _M0L3valS3455 = _M0L1sS1075->$0;
            _M0L6_2atmpS3454 = _M0L3valS3455 + 1;
            _M0L1sS1075->$0 = _M0L6_2atmpS3454;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS1075);
          }
          break;
        }
      }
      _M0L3valS3466 = _M0L1jS1067->$0;
      _M0L6_2atmpS3465 = _M0L3valS3466 + 1;
      _M0L1jS1067->$0 = _M0L6_2atmpS3465;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1067);
    }
    break;
  }
  #line 511 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L3nnzS1085 = _M0MPC15array5Array6lengthGfE(_M0L1wS1082);
  _M0L2s2S1086
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S1086)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S1086->$0 = 0;
  while (1) {
    int32_t _M0L3valS3467 = _M0L2s2S1086->$0;
    if (_M0L3valS3467 < _M0L3nnzS1085) {
      int32_t _M0L3valS3479 = _M0L2s2S1086->$0;
      int32_t _M0L9post__idxS1087;
      int32_t _M0L3valS3478;
      int32_t _M0L6_2atmpS3477;
      #line 514 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L9post__idxS1087
      = _M0MPC15array5Array2atGiE(_M0L6colptrS1077, _M0L3valS3479);
      #line 515 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (
        _M0MPC15array5Array2atGbE(_M0L10post__fireS1060, _M0L9post__idxS1087)
      ) {
        int32_t _M0L3valS3476 = _M0L2s2S1086->$0;
        int32_t _M0L6j__preS1088;
        float _M0L6_2atmpS3474;
        float _M0L6_2atmpS3475;
        float _M0L5ratioS1089;
        float _M0L3lnxS1090;
        float _M0L1xS1091;
        float _M0L1aS3472;
        float _M0L6_2atmpS3473;
        float _M0L2dwS1092;
        int32_t _M0L3valS3468;
        int32_t _M0L3valS3471;
        float _M0L6_2atmpS3470;
        float _M0L6_2atmpS3469;
        #line 518 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6j__preS1088
        = _M0FP26RiantR8snn__mbt20find__pre__for__conn(_M0L6rowptrS1073, _M0L3valS3476);
        #line 519 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3474
        = _M0MPC15array5Array2atGfE(_M0L4tpreS1068, _M0L6j__preS1088);
        #line 519 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3475
        = _M0MPC15array5Array2atGfE(_M0L5tpostS1064, _M0L9post__idxS1087);
        _M0L5ratioS1089 = _M0L6_2atmpS3474 / _M0L6_2atmpS3475;
        #line 520 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3lnxS1090 = _M0FP26RiantR8snn__mbt4logf(_M0L5ratioS1089);
        _M0L1xS1091 = _M0L3lnxS1090 * _M0L3lnxS1090;
        _M0L1aS3472 = _M0L5paramS1062->$0;
        #line 522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3473
        = _M0FP26RiantR8snn__mbt20mexican__hat__kernel(_M0L1xS1091);
        _M0L2dwS1092 = _M0L1aS3472 * _M0L6_2atmpS3473;
        _M0L3valS3468 = _M0L2s2S1086->$0;
        _M0L3valS3471 = _M0L2s2S1086->$0;
        #line 523 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3470
        = _M0MPC15array5Array2atGfE(_M0L1wS1082, _M0L3valS3471);
        _M0L6_2atmpS3469 = _M0L6_2atmpS3470 + _M0L2dwS1092;
        #line 523 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1082, _M0L3valS3468, _M0L6_2atmpS3469);
      }
      _M0L3valS3478 = _M0L2s2S1086->$0;
      _M0L6_2atmpS3477 = _M0L3valS3478 + 1;
      _M0L2s2S1086->$0 = _M0L6_2atmpS3477;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S1086);
    }
    break;
  }
  _M0L2s3S1094
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s3S1094)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s3S1094->$0 = 0;
  while (1) {
    int32_t _M0L3valS3480 = _M0L2s3S1094->$0;
    if (_M0L3valS3480 < _M0L3nnzS1085) {
      int32_t _M0L3valS3483 = _M0L2s3S1094->$0;
      float _M0L6_2atmpS3481;
      float _M0L6w__minS3482;
      int32_t _M0L3valS3492;
      int32_t _M0L6_2atmpS3491;
      #line 530 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3481
      = _M0MPC15array5Array2atGfE(_M0L1wS1082, _M0L3valS3483);
      _M0L6w__minS3482 = _M0L5paramS1062->$3;
      if (_M0L6_2atmpS3481 < _M0L6w__minS3482) {
        int32_t _M0L3valS3484 = _M0L2s3S1094->$0;
        float _M0L6w__minS3485 = _M0L5paramS1062->$3;
        #line 531 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1082, _M0L3valS3484, _M0L6w__minS3485);
      } else {
        int32_t _M0L3valS3488 = _M0L2s3S1094->$0;
        float _M0L6_2atmpS3486;
        float _M0L6w__maxS3487;
        #line 532 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3486
        = _M0MPC15array5Array2atGfE(_M0L1wS1082, _M0L3valS3488);
        _M0L6w__maxS3487 = _M0L5paramS1062->$2;
        if (_M0L6_2atmpS3486 > _M0L6w__maxS3487) {
          int32_t _M0L3valS3489 = _M0L2s3S1094->$0;
          float _M0L6w__maxS3490 = _M0L5paramS1062->$2;
          #line 533 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0MPC15array5Array3setGfE(_M0L1wS1082, _M0L3valS3489, _M0L6w__maxS3490);
        }
      }
      _M0L3valS3492 = _M0L2s3S1094->$0;
      _M0L6_2atmpS3491 = _M0L3valS3492 + 1;
      _M0L2s3S1094->$0 = _M0L6_2atmpS3491;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s3S1094);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20find__pre__for__conn(
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1051,
  int32_t _M0L1sS1055
) {
  int32_t _M0L6_2atmpS3406;
  int32_t _M0L1nS1050;
  struct _M0TPB8MutLocalGiE* _M0L2loS1052;
  struct _M0TPB8MutLocalGiE* _M0L2hiS1053;
  int32_t _result_5817;
  #line 542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 543 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3406 = _M0MPC15array5Array6lengthGiE(_M0L6rowptrS1051);
  _M0L1nS1050 = _M0L6_2atmpS3406 - 1;
  _M0L2loS1052
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2loS1052)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2loS1052->$0 = 0;
  _M0L2hiS1053
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2hiS1053)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2hiS1053->$0 = _M0L1nS1050;
  while (1) {
    int32_t _M0L3valS3398 = _M0L2loS1052->$0;
    int32_t _M0L3valS3399 = _M0L2hiS1053->$0;
    if (_M0L3valS3398 < _M0L3valS3399) {
      int32_t _M0L3valS3404 = _M0L2loS1052->$0;
      int32_t _M0L3valS3405 = _M0L2hiS1053->$0;
      int32_t _M0L6_2atmpS3403 = _M0L3valS3404 + _M0L3valS3405;
      int32_t _M0L6_2atmpS3402 = _M0L6_2atmpS3403 + 1;
      int32_t _M0L3midS1054 = _M0L6_2atmpS3402 / 2;
      int32_t _M0L6_2atmpS3400;
      #line 548 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3400
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1051, _M0L3midS1054);
      if (_M0L6_2atmpS3400 <= _M0L1sS1055) {
        _M0L2loS1052->$0 = _M0L3midS1054;
      } else {
        int32_t _M0L6_2atmpS3401 = _M0L3midS1054 - 1;
        _M0L2hiS1053->$0 = _M0L6_2atmpS3401;
      }
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2hiS1053);
    }
    break;
  }
  _result_5817 = _M0L2loS1052->$0;
  moonbit_decref_cycle_free(_M0L2loS1052);
  return _result_5817;
}

float _M0FP26RiantR8snn__mbt20mexican__hat__kernel(float _M0L1xS1047) {
  #line 427 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 428 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  if (_M0MPC15float5Float7is__nan(_M0L1xS1047)) {
    return 0x0p+0f;
  } else {
    float _M0L6_2atmpS3397 = -_M0L1xS1047;
    float _M0L3argS1048 = _M0L6_2atmpS3397 / 0x1.6a09e65dc27dfp+0f;
    float _M0L6_2atmpS3395 = 0x1p+0f - _M0L1xS1047;
    float _M0L6_2atmpS3396;
    float _M0L1vS1049;
    #line 432 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS3396 = _M0FP26RiantR8snn__mbt4expf(_M0L3argS1048);
    _M0L1vS1049 = _M0L6_2atmpS3395 * _M0L6_2atmpS3396;
    #line 433 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    if (_M0MPC15float5Float7is__nan(_M0L1vS1049)) {
      return 0x0p+0f;
    } else {
      return _M0L1vS1049;
    }
  }
}

int32_t _M0FP26RiantR8snn__mbt19stimulate__balanced(
  struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L1sS1014,
  float _M0L4timeS1012,
  float _M0L2dtS1023
) {
  int32_t _M0L1nS1013;
  struct _M0TP26RiantR8snn__mbt17BalancedParameter* _M0L5paramS1015;
  float _M0L3kIES1016;
  float _M0L4betaS1017;
  float _M0L3tauS1018;
  float _M0L2r0S1019;
  float _M0L1wS1020;
  float _M0L3wIES1021;
  float _M0L6_2atmpS3394;
  float _M0L11inh__lambdaS1022;
  int32_t _M0L7_2abindS1024;
  int32_t _M0L1kS1025;
  float _M0L6_2atmpS3393;
  float _M0L2ccS1029;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
  _M0L1nS1013 = _M0L1sS1014->$1;
  _M0L5paramS1015 = _M0L1sS1014->$0;
  _M0L3kIES1016 = _M0L5paramS1015->$0;
  _M0L4betaS1017 = _M0L5paramS1015->$1;
  _M0L3tauS1018 = _M0L5paramS1015->$2;
  _M0L2r0S1019 = _M0L5paramS1015->$3;
  _M0L1wS1020 = _M0L5paramS1015->$4;
  _M0L3wIES1021 = _M0L5paramS1015->$5;
  _M0L6_2atmpS3394 = _M0L2r0S1019 * _M0L3kIES1016;
  _M0L11inh__lambdaS1022 = _M0L6_2atmpS3394 * _M0L2dtS1023;
  _M0L7_2abindS1024 = 0;
  _M0L1kS1025 = _M0L7_2abindS1024;
  while (1) {
    if (_M0L1kS1025 < _M0L1nS1013) {
      struct _M0TPB5ArrayGbE* _M0L4fireS3307 = _M0L1sS1014->$4;
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3315;
      int32_t _M0L1mS1028;
      int32_t _M0L6_2atmpS3306;
      #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3307, _M0L1kS1025, 0);
      if (_M0L11inh__lambdaS1022 <= 0x0p+0f) {
        goto join_1026;
      }
      _M0L3rngS3315 = _M0L1sS1014->$7;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
      _M0L1mS1028
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3315, _M0L11inh__lambdaS1022);
      if (_M0L1mS1028 > 0) {
        struct _M0TPB5ArrayGfE* _M0L2giS3308 = _M0L1sS1014->$3;
        struct _M0TPB5ArrayGfE* _M0L2giS3314 = _M0L1sS1014->$3;
        float _M0L6_2atmpS3310;
        float _M0L6_2atmpS3313;
        float _M0L6_2atmpS3312;
        float _M0L6_2atmpS3311;
        float _M0L6_2atmpS3309;
        #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3310
        = _M0MPC15array5Array2atGfE(_M0L2giS3314, _M0L1kS1025);
        _M0L6_2atmpS3313 = (float)_M0L1mS1028;
        _M0L6_2atmpS3312 = _M0L1wS1020 * _M0L6_2atmpS3313;
        _M0L6_2atmpS3311 = _M0L6_2atmpS3312 * _M0L3wIES1021;
        _M0L6_2atmpS3309 = _M0L6_2atmpS3310 + _M0L6_2atmpS3311;
        #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L2giS3308, _M0L1kS1025, _M0L6_2atmpS3309);
      }
      goto join_1026;
      goto joinlet_5819;
      join_1026:;
      _M0L6_2atmpS3306 = _M0L1kS1025 + 1;
      _M0L1kS1025 = _M0L6_2atmpS3306;
      continue;
      joinlet_5819:;
    }
    break;
  }
  _M0L6_2atmpS3393 = _M0L2dtS1023 / _M0L3tauS1018;
  _M0L2ccS1029 = 0x1p+0f - _M0L6_2atmpS3393;
  if (_M0L5paramS1015->$6) {
    struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3353 = _M0L1sS1014->$7;
    double _M0L6_2atmpS3352;
    float _M0L6_2atmpS3351;
    float _M0L2reS1030;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3316;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3321;
    float _M0L6_2atmpS3320;
    float _M0L6_2atmpS3319;
    float _M0L6_2atmpS3318;
    float _M0L6_2atmpS3317;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3350;
    float _M0L6_2atmpS3349;
    float _M0L6_2atmpS3348;
    struct _M0TPB8MutLocalGfE* _M0L2nbS1031;
    float _M0L3valS3322;
    float _M0L3valS3323;
    float _M0L6_2atmpS3346;
    float _M0L3valS3347;
    float _M0L6_2atmpS3343;
    struct _M0TPB5ArrayGfE* _M0L1rS3345;
    float _M0L6_2atmpS3344;
    float _M0L6_2atmpS3342;
    struct _M0TPB8MutLocalGfE* _M0L5erateS1032;
    float _M0L3valS3324;
    struct _M0TPB5ArrayGfE* _M0L1rS3325;
    struct _M0TPB5ArrayGfE* _M0L1rS3332;
    float _M0L6_2atmpS3327;
    float _M0L3valS3331;
    float _M0L6_2atmpS3330;
    float _M0L6_2atmpS3329;
    float _M0L6_2atmpS3328;
    float _M0L6_2atmpS3326;
    float _M0L3valS3341;
    float _M0L11exc__lambdaS1033;
    struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3340;
    int32_t _M0L1mS1034;
    #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3352 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS3353);
    _M0L6_2atmpS3351 = (float)_M0L6_2atmpS3352;
    _M0L2reS1030 = _M0L6_2atmpS3351 - 0x1p-1f;
    _M0L5noiseS3316 = _M0L1sS1014->$6;
    _M0L5noiseS3321 = _M0L1sS1014->$6;
    #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3320 = _M0MPC15array5Array2atGfE(_M0L5noiseS3321, 0);
    _M0L6_2atmpS3319 = _M0L6_2atmpS3320 - _M0L2reS1030;
    _M0L6_2atmpS3318 = _M0L6_2atmpS3319 * _M0L2ccS1029;
    _M0L6_2atmpS3317 = _M0L6_2atmpS3318 + _M0L2reS1030;
    #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0MPC15array5Array3setGfE(_M0L5noiseS3316, 0, _M0L6_2atmpS3317);
    _M0L5noiseS3350 = _M0L1sS1014->$6;
    #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3349 = _M0MPC15array5Array2atGfE(_M0L5noiseS3350, 0);
    _M0L6_2atmpS3348 = _M0L6_2atmpS3349 * _M0L4betaS1017;
    _M0L2nbS1031
    = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
    Moonbit_object_header(_M0L2nbS1031)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L2nbS1031->$0 = _M0L6_2atmpS3348;
    _M0L3valS3322 = _M0L2nbS1031->$0;
    if (_M0L3valS3322 > 0x1p+0f) {
      _M0L2nbS1031->$0 = 0x1p+0f;
    }
    _M0L3valS3323 = _M0L2nbS1031->$0;
    if (_M0L3valS3323 < 0x0p+0f) {
      _M0L2nbS1031->$0 = 0x0p+0f;
    }
    _M0L6_2atmpS3346 = _M0L2r0S1019 / 0x1p+1f;
    _M0L3valS3347 = _M0L2nbS1031->$0;
    moonbit_decref_cycle_free(_M0L2nbS1031);
    _M0L6_2atmpS3343 = _M0L6_2atmpS3346 * _M0L3valS3347;
    _M0L1rS3345 = _M0L1sS1014->$5;
    #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3344 = _M0MPC15array5Array2atGfE(_M0L1rS3345, 0);
    _M0L6_2atmpS3342 = _M0L6_2atmpS3343 + _M0L6_2atmpS3344;
    _M0L5erateS1032
    = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
    Moonbit_object_header(_M0L5erateS1032)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L5erateS1032->$0 = _M0L6_2atmpS3342;
    _M0L3valS3324 = _M0L5erateS1032->$0;
    if (_M0L3valS3324 < 0x0p+0f) {
      _M0L5erateS1032->$0 = 0x0p+0f;
    }
    _M0L1rS3325 = _M0L1sS1014->$5;
    _M0L1rS3332 = _M0L1sS1014->$5;
    #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3327 = _M0MPC15array5Array2atGfE(_M0L1rS3332, 0);
    _M0L3valS3331 = _M0L5erateS1032->$0;
    _M0L6_2atmpS3330 = _M0L2r0S1019 - _M0L3valS3331;
    _M0L6_2atmpS3329 = _M0L6_2atmpS3330 / 0x1.9p+8f;
    _M0L6_2atmpS3328 = _M0L6_2atmpS3329 * _M0L2dtS1023;
    _M0L6_2atmpS3326 = _M0L6_2atmpS3327 + _M0L6_2atmpS3328;
    #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0MPC15array5Array3setGfE(_M0L1rS3325, 0, _M0L6_2atmpS3326);
    _M0L3valS3341 = _M0L5erateS1032->$0;
    moonbit_decref_cycle_free(_M0L5erateS1032);
    _M0L11exc__lambdaS1033 = _M0L3valS3341 * _M0L2dtS1023;
    _M0L3rngS3340 = _M0L1sS1014->$7;
    #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L1mS1034
    = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3340, _M0L11exc__lambdaS1033);
    if (_M0L1mS1034 > 0) {
      float _M0L6_2atmpS3339 = (float)_M0L1mS1034;
      float _M0L3addS1035 = _M0L1wS1020 * _M0L6_2atmpS3339;
      int32_t _M0L7_2abindS1036 = 0;
      int32_t _M0L1iS1037 = _M0L7_2abindS1036;
      while (1) {
        if (_M0L1iS1037 < _M0L1nS1013) {
          struct _M0TPB5ArrayGfE* _M0L2geS3333 = _M0L1sS1014->$2;
          struct _M0TPB5ArrayGfE* _M0L2geS3336 = _M0L1sS1014->$2;
          float _M0L6_2atmpS3335;
          float _M0L6_2atmpS3334;
          struct _M0TPB5ArrayGbE* _M0L4fireS3337;
          int32_t _M0L6_2atmpS3338;
          #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0L6_2atmpS3335
          = _M0MPC15array5Array2atGfE(_M0L2geS3336, _M0L1iS1037);
          _M0L6_2atmpS3334 = _M0L6_2atmpS3335 + _M0L3addS1035;
          #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGfE(_M0L2geS3333, _M0L1iS1037, _M0L6_2atmpS3334);
          _M0L4fireS3337 = _M0L1sS1014->$4;
          #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGbE(_M0L4fireS3337, _M0L1iS1037, 1);
          _M0L6_2atmpS3338 = _M0L1iS1037 + 1;
          _M0L1iS1037 = _M0L6_2atmpS3338;
          continue;
        }
        break;
      }
    }
  } else {
    int32_t _M0L7_2abindS1039 = 0;
    int32_t _M0L1iS1040 = _M0L7_2abindS1039;
    while (1) {
      if (_M0L1iS1040 < _M0L1nS1013) {
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3391 =
          _M0L1sS1014->$7;
        double _M0L6_2atmpS3390;
        float _M0L6_2atmpS3389;
        float _M0L2reS1041;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3354;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3359;
        float _M0L6_2atmpS3358;
        float _M0L6_2atmpS3357;
        float _M0L6_2atmpS3356;
        float _M0L6_2atmpS3355;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3388;
        float _M0L6_2atmpS3387;
        float _M0L6_2atmpS3386;
        struct _M0TPB8MutLocalGfE* _M0L2nbS1042;
        float _M0L3valS3360;
        float _M0L3valS3361;
        float _M0L6_2atmpS3384;
        float _M0L3valS3385;
        float _M0L6_2atmpS3381;
        struct _M0TPB5ArrayGfE* _M0L1rS3383;
        float _M0L6_2atmpS3382;
        float _M0L6_2atmpS3380;
        struct _M0TPB8MutLocalGfE* _M0L5erateS1043;
        float _M0L3valS3362;
        struct _M0TPB5ArrayGfE* _M0L1rS3363;
        struct _M0TPB5ArrayGfE* _M0L1rS3370;
        float _M0L6_2atmpS3365;
        float _M0L3valS3369;
        float _M0L6_2atmpS3368;
        float _M0L6_2atmpS3367;
        float _M0L6_2atmpS3366;
        float _M0L6_2atmpS3364;
        float _M0L3valS3379;
        float _M0L11exc__lambdaS1044;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3378;
        int32_t _M0L1mS1045;
        int32_t _M0L6_2atmpS3392;
        #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3390 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS3391);
        _M0L6_2atmpS3389 = (float)_M0L6_2atmpS3390;
        _M0L2reS1041 = _M0L6_2atmpS3389 - 0x1p-1f;
        _M0L5noiseS3354 = _M0L1sS1014->$6;
        _M0L5noiseS3359 = _M0L1sS1014->$6;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3358
        = _M0MPC15array5Array2atGfE(_M0L5noiseS3359, _M0L1iS1040);
        _M0L6_2atmpS3357 = _M0L6_2atmpS3358 - _M0L2reS1041;
        _M0L6_2atmpS3356 = _M0L6_2atmpS3357 * _M0L2ccS1029;
        _M0L6_2atmpS3355 = _M0L6_2atmpS3356 + _M0L2reS1041;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L5noiseS3354, _M0L1iS1040, _M0L6_2atmpS3355);
        _M0L5noiseS3388 = _M0L1sS1014->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3387
        = _M0MPC15array5Array2atGfE(_M0L5noiseS3388, _M0L1iS1040);
        _M0L6_2atmpS3386 = _M0L6_2atmpS3387 * _M0L4betaS1017;
        _M0L2nbS1042
        = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
        Moonbit_object_header(_M0L2nbS1042)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L2nbS1042->$0 = _M0L6_2atmpS3386;
        _M0L3valS3360 = _M0L2nbS1042->$0;
        if (_M0L3valS3360 > 0x1p+0f) {
          _M0L2nbS1042->$0 = 0x1p+0f;
        }
        _M0L3valS3361 = _M0L2nbS1042->$0;
        if (_M0L3valS3361 < 0x0p+0f) {
          _M0L2nbS1042->$0 = 0x0p+0f;
        }
        _M0L6_2atmpS3384 = _M0L2r0S1019 / 0x1p+1f;
        _M0L3valS3385 = _M0L2nbS1042->$0;
        moonbit_decref_cycle_free(_M0L2nbS1042);
        _M0L6_2atmpS3381 = _M0L6_2atmpS3384 * _M0L3valS3385;
        _M0L1rS3383 = _M0L1sS1014->$5;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3382
        = _M0MPC15array5Array2atGfE(_M0L1rS3383, _M0L1iS1040);
        _M0L6_2atmpS3380 = _M0L6_2atmpS3381 + _M0L6_2atmpS3382;
        _M0L5erateS1043
        = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
        Moonbit_object_header(_M0L5erateS1043)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L5erateS1043->$0 = _M0L6_2atmpS3380;
        _M0L3valS3362 = _M0L5erateS1043->$0;
        if (_M0L3valS3362 < 0x0p+0f) {
          _M0L5erateS1043->$0 = 0x0p+0f;
        }
        _M0L1rS3363 = _M0L1sS1014->$5;
        _M0L1rS3370 = _M0L1sS1014->$5;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3365
        = _M0MPC15array5Array2atGfE(_M0L1rS3370, _M0L1iS1040);
        _M0L3valS3369 = _M0L5erateS1043->$0;
        _M0L6_2atmpS3368 = _M0L2r0S1019 - _M0L3valS3369;
        _M0L6_2atmpS3367 = _M0L6_2atmpS3368 / 0x1.9p+8f;
        _M0L6_2atmpS3366 = _M0L6_2atmpS3367 * _M0L2dtS1023;
        _M0L6_2atmpS3364 = _M0L6_2atmpS3365 + _M0L6_2atmpS3366;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L1rS3363, _M0L1iS1040, _M0L6_2atmpS3364);
        _M0L3valS3379 = _M0L5erateS1043->$0;
        moonbit_decref_cycle_free(_M0L5erateS1043);
        _M0L11exc__lambdaS1044 = _M0L3valS3379 * _M0L2dtS1023;
        _M0L3rngS3378 = _M0L1sS1014->$7;
        #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L1mS1045
        = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3378, _M0L11exc__lambdaS1044);
        if (_M0L1mS1045 > 0) {
          struct _M0TPB5ArrayGfE* _M0L2geS3371 = _M0L1sS1014->$2;
          struct _M0TPB5ArrayGfE* _M0L2geS3376 = _M0L1sS1014->$2;
          float _M0L6_2atmpS3373;
          float _M0L6_2atmpS3375;
          float _M0L6_2atmpS3374;
          float _M0L6_2atmpS3372;
          struct _M0TPB5ArrayGbE* _M0L4fireS3377;
          #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0L6_2atmpS3373
          = _M0MPC15array5Array2atGfE(_M0L2geS3376, _M0L1iS1040);
          _M0L6_2atmpS3375 = (float)_M0L1mS1045;
          _M0L6_2atmpS3374 = _M0L1wS1020 * _M0L6_2atmpS3375;
          _M0L6_2atmpS3372 = _M0L6_2atmpS3373 + _M0L6_2atmpS3374;
          #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGfE(_M0L2geS3371, _M0L1iS1040, _M0L6_2atmpS3372);
          _M0L4fireS3377 = _M0L1sS1014->$4;
          #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGbE(_M0L4fireS3377, _M0L1iS1040, 1);
        }
        _M0L6_2atmpS3392 = _M0L1iS1040 + 1;
        _M0L1iS1040 = _M0L6_2atmpS3392;
        continue;
      }
      break;
    }
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS1010
) {
  struct _M0TUmmmmE* _M0L1sS1009;
  uint64_t _M0L6_2atmpS3305;
  struct _M0TUmmmmE* _M0L1tS1011;
  uint64_t _M0L6_2atmpS3301;
  uint64_t _M0L6_2atmpS3302;
  uint64_t _M0L6_2atmpS3303;
  uint64_t _M0L6_2atmpS3304;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_5822;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS1009 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS1010);
  _M0L6_2atmpS3305 = _M0L1sS1009->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS1011 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS3305);
  _M0L6_2atmpS3301 = _M0L1sS1009->$0;
  _M0L6_2atmpS3302 = _M0L1sS1009->$1;
  _M0L6_2atmpS3303 = _M0L1sS1009->$2;
  moonbit_decref_cycle_free(_M0L1sS1009);
  _M0L6_2atmpS3304 = _M0L1tS1011->$0;
  moonbit_decref_cycle_free(_M0L1tS1011);
  _block_5822
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_5822)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5822->$0 = _M0L6_2atmpS3301;
  _block_5822->$1 = _M0L6_2atmpS3302;
  _block_5822->$2 = _M0L6_2atmpS3303;
  _block_5822->$3 = _M0L6_2atmpS3304;
  return _block_5822;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(
  uint64_t _M0L4seedS1001
) {
  uint64_t _M0L2s1S1000;
  uint64_t _M0L2z1S1002;
  uint64_t _M0L2s2S1003;
  uint64_t _M0L2z2S1004;
  uint64_t _M0L2s3S1005;
  uint64_t _M0L2z3S1006;
  uint64_t _M0L2s4S1007;
  uint64_t _M0L2z4S1008;
  struct _M0TUmmmmE* _block_5823;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S1000 = _M0L4seedS1001 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S1002 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S1000);
  _M0L2s2S1003 = _M0L2s1S1000 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S1004 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S1003);
  _M0L2s3S1005 = _M0L2s2S1003 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S1006 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S1005);
  _M0L2s4S1007 = _M0L2s3S1005 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S1008 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S1007);
  _block_5823 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_5823)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5823->$0 = _M0L2z1S1002;
  _block_5823->$1 = _M0L2z2S1004;
  _block_5823->$2 = _M0L2z3S1006;
  _block_5823->$3 = _M0L2z4S1008;
  return _block_5823;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS998) {
  uint64_t _M0L6_2atmpS3300;
  uint64_t _M0L6_2atmpS3299;
  uint64_t _M0L1zS997;
  uint64_t _M0L6_2atmpS3298;
  uint64_t _M0L6_2atmpS3297;
  uint64_t _M0L1zS999;
  uint64_t _M0L6_2atmpS3296;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS3300 = _M0L1zS998 >> 30;
  _M0L6_2atmpS3299 = _M0L1zS998 ^ _M0L6_2atmpS3300;
  _M0L1zS997 = _M0L6_2atmpS3299 * 13787848793156543929ull;
  _M0L6_2atmpS3298 = _M0L1zS997 >> 27;
  _M0L6_2atmpS3297 = _M0L1zS997 ^ _M0L6_2atmpS3298;
  _M0L1zS999 = _M0L6_2atmpS3297 * 10723151780598845931ull;
  _M0L6_2atmpS3296 = _M0L1zS999 >> 31;
  return _M0L1zS999 ^ _M0L6_2atmpS3296;
}

int32_t _M0FP26RiantR8snn__mbt25stimulate__current__array(
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L1sS985
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3280;
  int32_t _M0L6_2atmpS3279;
  float _M0L12noise__sigmaS3281;
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6activeS3280 = _M0L1sS985->$1;
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6_2atmpS3279 = _M0MPC15array5Array2atGbE(_M0L6activeS3280, 0);
  if (!_M0L6_2atmpS3279) {
    return 0;
  }
  _M0L12noise__sigmaS3281 = _M0L1sS985->$4;
  if (_M0L12noise__sigmaS3281 <= 0x0p+0f) {
    int32_t _M0L7_2abindS986 = 0;
    int32_t _M0L7_2abindS987 = _M0L1sS985->$3;
    int32_t _M0L1kS988 = _M0L7_2abindS986;
    while (1) {
      if (_M0L1kS988 < _M0L7_2abindS987) {
        struct _M0TPB5ArrayGfE* _M0L1iS3282 = _M0L1sS985->$2;
        float _M0L7i__baseS3283 = _M0L1sS985->$0;
        int32_t _M0L6_2atmpS3284;
        #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3282, _M0L1kS988, _M0L7i__baseS3283);
        _M0L6_2atmpS3284 = _M0L1kS988 + 1;
        _M0L1kS988 = _M0L6_2atmpS3284;
        continue;
      }
      break;
    }
  } else {
    float _M0L5sigmaS990 = _M0L1sS985->$4;
    struct _M0TPB8MutLocalGiE* _M0L1kS991 =
      (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS991)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS991->$0 = 0;
    while (1) {
      int32_t _M0L3valS3285 = _M0L1kS991->$0;
      int32_t _M0L1nS3286 = _M0L1sS985->$3;
      if (_M0L3valS3285 < _M0L1nS3286) {
        double _M0L2z1S993;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3295 = _M0L1sS985->$5;
        struct _M0TUddE* _M0L7_2abindS994;
        double _M0L5_2az1S995;
        struct _M0TPB5ArrayGfE* _M0L1iS3287;
        int32_t _M0L3valS3288;
        float _M0L7i__baseS3290;
        float _M0L6_2atmpS3292;
        float _M0L6_2atmpS3291;
        float _M0L6_2atmpS3289;
        int32_t _M0L3valS3294;
        int32_t _M0L6_2atmpS3293;
        #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0L7_2abindS994 = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS3295);
        _M0L5_2az1S995 = _M0L7_2abindS994->$0;
        moonbit_decref_cycle_free(_M0L7_2abindS994);
        _M0L2z1S993 = _M0L5_2az1S995;
        goto join_992;
        goto joinlet_5826;
        join_992:;
        _M0L1iS3287 = _M0L1sS985->$2;
        _M0L3valS3288 = _M0L1kS991->$0;
        _M0L7i__baseS3290 = _M0L1sS985->$0;
        _M0L6_2atmpS3292 = (float)_M0L2z1S993;
        _M0L6_2atmpS3291 = _M0L5sigmaS990 * _M0L6_2atmpS3292;
        _M0L6_2atmpS3289 = _M0L7i__baseS3290 + _M0L6_2atmpS3291;
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3287, _M0L3valS3288, _M0L6_2atmpS3289);
        _M0L3valS3294 = _M0L1kS991->$0;
        _M0L6_2atmpS3293 = _M0L3valS3294 + 1;
        _M0L1kS991->$0 = _M0L6_2atmpS3293;
        joinlet_5826:;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS991);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22stimulate__current__if(
  struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L1sS972
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3264;
  int32_t _M0L6_2atmpS3263;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS973;
  int32_t _M0L1nS974;
  float _M0L12noise__sigmaS3265;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6activeS3264 = _M0L1sS972->$1;
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6_2atmpS3263 = _M0MPC15array5Array2atGbE(_M0L6activeS3264, 0);
  if (!_M0L6_2atmpS3263) {
    return 0;
  }
  _M0L3popS973 = _M0L1sS972->$2;
  _M0L1nS974 = _M0L3popS973->$2;
  _M0L12noise__sigmaS3265 = _M0L1sS972->$3;
  if (_M0L12noise__sigmaS3265 <= 0x0p+0f) {
    int32_t _M0L7_2abindS975 = 0;
    int32_t _M0L1iS976 = _M0L7_2abindS975;
    while (1) {
      if (_M0L1iS976 < _M0L1nS974) {
        struct _M0TPB5ArrayGfE* _M0L1iS3266 = _M0L3popS973->$7;
        float _M0L7i__baseS3267 = _M0L1sS972->$0;
        int32_t _M0L6_2atmpS3268;
        #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3266, _M0L1iS976, _M0L7i__baseS3267);
        _M0L6_2atmpS3268 = _M0L1iS976 + 1;
        _M0L1iS976 = _M0L6_2atmpS3268;
        continue;
      }
      break;
    }
  } else {
    float _M0L5sigmaS978 = _M0L1sS972->$3;
    struct _M0TPB8MutLocalGiE* _M0L1kS979 =
      (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS979)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS979->$0 = 0;
    while (1) {
      int32_t _M0L3valS3269 = _M0L1kS979->$0;
      if (_M0L3valS3269 < _M0L1nS974) {
        double _M0L2z1S981;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3278 = _M0L1sS972->$4;
        struct _M0TUddE* _M0L7_2abindS982;
        double _M0L5_2az1S983;
        struct _M0TPB5ArrayGfE* _M0L1iS3270;
        int32_t _M0L3valS3271;
        float _M0L7i__baseS3273;
        float _M0L6_2atmpS3275;
        float _M0L6_2atmpS3274;
        float _M0L6_2atmpS3272;
        int32_t _M0L3valS3277;
        int32_t _M0L6_2atmpS3276;
        #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0L7_2abindS982 = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS3278);
        _M0L5_2az1S983 = _M0L7_2abindS982->$0;
        moonbit_decref_cycle_free(_M0L7_2abindS982);
        _M0L2z1S981 = _M0L5_2az1S983;
        goto join_980;
        goto joinlet_5829;
        join_980:;
        _M0L1iS3270 = _M0L3popS973->$7;
        _M0L3valS3271 = _M0L1kS979->$0;
        _M0L7i__baseS3273 = _M0L1sS972->$0;
        _M0L6_2atmpS3275 = (float)_M0L2z1S981;
        _M0L6_2atmpS3274 = _M0L5sigmaS978 * _M0L6_2atmpS3275;
        _M0L6_2atmpS3272 = _M0L7i__baseS3273 + _M0L6_2atmpS3274;
        #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3270, _M0L3valS3271, _M0L6_2atmpS3272);
        _M0L3valS3277 = _M0L1kS979->$0;
        _M0L6_2atmpS3276 = _M0L3valS3277 + 1;
        _M0L1kS979->$0 = _M0L6_2atmpS3276;
        joinlet_5829:;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS979);
      }
      break;
    }
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0MP26RiantR8snn__mbt17CurrentStimulusIF11new_2einner(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS969,
  float _M0L7i__baseS968,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS971,
  float _M0L12noise__sigmaS970
) {
  uint8_t* _M0L6_2atmpS3262;
  struct _M0TPB5ArrayGbE* _M0L6_2atmpS3261;
  struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _block_5830;
  #line 32 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6_2atmpS3262 = (uint8_t*)moonbit_make_bytes_raw(1);
  _M0L6_2atmpS3262[0] = 1;
  _M0L6_2atmpS3261
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_M0L6_2atmpS3261)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 66, 0);
  _M0L6_2atmpS3261->$0 = _M0L6_2atmpS3262;
  _M0L6_2atmpS3261->$1 = 1;
  moonbit_incref_cycle_free(_M0L3popS969);
  moonbit_incref_cycle_free(_M0L3rngS971);
  _block_5830
  = (struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF));
  Moonbit_object_header(_block_5830)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 69, 0);
  _block_5830->$0 = _M0L7i__baseS968;
  _block_5830->$1 = _M0L6_2atmpS3261;
  _block_5830->$2 = _M0L3popS969;
  _block_5830->$3 = _M0L12noise__sigmaS970;
  _block_5830->$4 = _M0L3rngS971;
  return _block_5830;
}

int32_t _M0FP26RiantR8snn__mbt13stimulate__if(
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L1sS957,
  float _M0L4timeS967,
  float _M0L2dtS959
) {
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3248;
  struct _M0TPB5ArrayGbE* _M0L6activeS3247;
  int32_t _M0L6_2atmpS3246;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3260;
  float _M0L4rateS3259;
  float _M0L6lambdaS958;
  struct _M0TPB5ArrayGiE* _M0L7_2abindS960;
  int32_t _M0L7_2abindS961;
  int32_t* _M0L7_2abindS962;
  int32_t _M0L2__S963;
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L5paramS3248 = _M0L1sS957->$0;
  _M0L6activeS3247 = _M0L5paramS3248->$2;
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3246 = _M0MPC15array5Array2atGbE(_M0L6activeS3247, 0);
  if (!_M0L6_2atmpS3246) {
    return 0;
  }
  _M0L5paramS3260 = _M0L1sS957->$0;
  _M0L4rateS3259 = _M0L5paramS3260->$0;
  _M0L6lambdaS958 = _M0L4rateS3259 * _M0L2dtS959;
  if (_M0L6lambdaS958 <= 0x0p+0f) {
    return 0;
  }
  _M0L7_2abindS960 = _M0L1sS957->$1;
  _M0L7_2abindS961 = _M0L7_2abindS960->$1;
  _M0L7_2abindS962 = _M0L7_2abindS960->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS962);
  _M0L2__S963 = 0;
  while (1) {
    if (_M0L2__S963 < _M0L7_2abindS961) {
      int32_t _M0L1nS964 = (int32_t)_M0L7_2abindS962[_M0L2__S963];
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3257 = _M0L1sS957->$3;
      int32_t _M0L1kS965;
      int32_t _M0L6_2atmpS3258;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
      _M0L1kS965
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3257, _M0L6lambdaS958);
      if (_M0L1kS965 > 0) {
        struct _M0TPB5ArrayGfE* _M0L1gS3249 = _M0L1sS957->$2;
        struct _M0TPB5ArrayGfE* _M0L1gS3256 = _M0L1sS957->$2;
        float _M0L6_2atmpS3251;
        struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3255;
        float _M0L2muS3253;
        float _M0L6_2atmpS3254;
        float _M0L6_2atmpS3252;
        float _M0L6_2atmpS3250;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0L6_2atmpS3251 = _M0MPC15array5Array2atGfE(_M0L1gS3256, _M0L1nS964);
        _M0L5paramS3255 = _M0L1sS957->$0;
        _M0L2muS3253 = _M0L5paramS3255->$1;
        _M0L6_2atmpS3254 = (float)_M0L1kS965;
        _M0L6_2atmpS3252 = _M0L2muS3253 * _M0L6_2atmpS3254;
        _M0L6_2atmpS3250 = _M0L6_2atmpS3251 + _M0L6_2atmpS3252;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0MPC15array5Array3setGfE(_M0L1gS3249, _M0L1nS964, _M0L6_2atmpS3250);
      }
      _M0L6_2atmpS3258 = _M0L2__S963 + 1;
      _M0L2__S963 = _M0L6_2atmpS3258;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS962);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16stimulate__layer(
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L1sS940,
  float _M0L4timeS938,
  float _M0L2dtS943
) {
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS3245;
  int32_t _M0L6n__preS939;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3244;
  int32_t _M0L7n__postS941;
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS3243;
  float _M0L4rateS3242;
  float _M0L6lambdaS942;
  int32_t _M0L7_2abindS944;
  int32_t _M0L1iS945;
  moonbit_string_t _M0L3symS3239;
  struct _M0TPB5ArrayGfE* _M0L9g__targetS947;
  int32_t _M0L7_2abindS948;
  int32_t _M0L1iS949;
  #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  _M0L5paramS3245 = _M0L1sS940->$0;
  _M0L6n__preS939 = _M0L5paramS3245->$1;
  _M0L4postS3244 = _M0L1sS940->$1;
  _M0L7n__postS941 = _M0L4postS3244->$2;
  _M0L5paramS3243 = _M0L1sS940->$0;
  _M0L4rateS3242 = _M0L5paramS3243->$0;
  _M0L6lambdaS942 = _M0L4rateS3242 * _M0L2dtS943;
  _M0L7_2abindS944 = 0;
  _M0L1iS945 = _M0L7_2abindS944;
  while (1) {
    if (_M0L1iS945 < _M0L6n__preS939) {
      struct _M0TPB5ArrayGbE* _M0L4fireS3224 = _M0L1sS940->$3;
      int32_t _M0L6_2atmpS3225;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3224, _M0L1iS945, 0);
      _M0L6_2atmpS3225 = _M0L1iS945 + 1;
      _M0L1iS945 = _M0L6_2atmpS3225;
      continue;
    }
    break;
  }
  if (_M0L6lambdaS942 <= 0x0p+0f) {
    return 0;
  }
  _M0L3symS3239 = _M0L1sS940->$2;
  #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  if (
    _M0L3symS3239 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS3239)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS3239, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS3239) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3240 = _M0L1sS940->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5460 = _M0L4postS3240->$13;
    moonbit_incref_cycle_free(_M0L8_2afieldS5460);
    _M0L9g__targetS947 = _M0L8_2afieldS5460;
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3241 = _M0L1sS940->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5461 = _M0L4postS3241->$14;
    moonbit_incref_cycle_free(_M0L8_2afieldS5461);
    _M0L9g__targetS947 = _M0L8_2afieldS5461;
  }
  _M0L7_2abindS948 = 0;
  _M0L1iS949 = _M0L7_2abindS948;
  while (1) {
    if (_M0L1iS949 < _M0L6n__preS939) {
      struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS3229 =
        _M0L1sS940->$0;
      struct _M0TPB5ArrayGbE* _M0L6activeS3228 = _M0L5paramS3229->$2;
      int32_t _M0L6_2atmpS3227;
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3238;
      int32_t _M0L1kS952;
      int32_t _M0L6_2atmpS3226;
      moonbit_incref_cycle_free(_M0L6activeS3228);
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0L6_2atmpS3227
      = _M0MPC15array5Array2atGbE(_M0L6activeS3228, _M0L1iS949);
      moonbit_decref_cycle_free(_M0L6activeS3228);
      if (!_M0L6_2atmpS3227) {
        goto join_950;
      }
      _M0L3rngS3238 = _M0L1sS940->$6;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0L1kS952
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3238, _M0L6lambdaS942);
      if (_M0L1kS952 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS3230 = _M0L1sS940->$3;
        int32_t _M0L7_2abindS953;
        int32_t _M0L1jS954;
        #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS3230, _M0L1iS949, 1);
        _M0L7_2abindS953 = 0;
        _M0L1jS954 = _M0L7_2abindS953;
        while (1) {
          if (_M0L1jS954 < _M0L7n__postS941) {
            int32_t _M0L6_2atmpS3236 = _M0L1jS954 * _M0L6n__preS939;
            int32_t _M0L3idxS955 = _M0L6_2atmpS3236 + _M0L1iS949;
            struct _M0TPB5ArrayGbE* _M0L12connectivityS3231 = _M0L1sS940->$5;
            int32_t _M0L6_2atmpS3237;
            #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
            if (
              _M0MPC15array5Array2atGbE(_M0L12connectivityS3231, _M0L3idxS955)
            ) {
              float _M0L6_2atmpS3233;
              struct _M0TPB5ArrayGfE* _M0L7weightsS3235;
              float _M0L6_2atmpS3234;
              float _M0L6_2atmpS3232;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0L6_2atmpS3233
              = _M0MPC15array5Array2atGfE(_M0L9g__targetS947, _M0L1jS954);
              _M0L7weightsS3235 = _M0L1sS940->$4;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0L6_2atmpS3234
              = _M0MPC15array5Array2atGfE(_M0L7weightsS3235, _M0L3idxS955);
              _M0L6_2atmpS3232 = _M0L6_2atmpS3233 + _M0L6_2atmpS3234;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0MPC15array5Array3setGfE(_M0L9g__targetS947, _M0L1jS954, _M0L6_2atmpS3232);
            }
            _M0L6_2atmpS3237 = _M0L1jS954 + 1;
            _M0L1jS954 = _M0L6_2atmpS3237;
            continue;
          }
          break;
        }
      }
      goto join_950;
      goto joinlet_5834;
      join_950:;
      _M0L6_2atmpS3226 = _M0L1iS949 + 1;
      _M0L1iS949 = _M0L6_2atmpS3226;
      continue;
      joinlet_5834:;
    } else {
      moonbit_decref_cycle_free(_M0L9g__targetS947);
    }
    break;
  }
  return 0;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS933
) {
  double _M0L2u1S932;
  double _M0L8u1__safeS934;
  double _M0L2u2S935;
  double _M0L6_2atmpS3223;
  double _M0L6_2atmpS3222;
  double _M0L1rS936;
  double _M0L5thetaS937;
  double _M0L6_2atmpS3221;
  double _M0L6_2atmpS3218;
  double _M0L6_2atmpS3220;
  double _M0L6_2atmpS3219;
  struct _M0TUddE* _block_5836;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S932 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS933);
  if (_M0L2u1S932 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS934 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS934 = _M0L2u1S932;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S935 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS933);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS3223 = _M0FPC14math2ln(_M0L8u1__safeS934);
  _M0L6_2atmpS3222 = -0x1p+1 * _M0L6_2atmpS3223;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS936 = sqrt(_M0L6_2atmpS3222);
  _M0L5thetaS937 = 0x1.921fb54442d18p+2 * _M0L2u2S935;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS3221 = _M0FPC14math3cos(_M0L5thetaS937);
  _M0L6_2atmpS3218 = _M0L1rS936 * _M0L6_2atmpS3221;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS3220 = _M0FPC14math3sin(_M0L5thetaS937);
  _M0L6_2atmpS3219 = _M0L1rS936 * _M0L6_2atmpS3220;
  _block_5836 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_5836)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5836->$0 = _M0L6_2atmpS3218;
  _block_5836->$1 = _M0L6_2atmpS3219;
  return _block_5836;
}

int32_t _M0FP26RiantR8snn__mbt15sample__poisson(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS930,
  float _M0L6lambdaS924
) {
  float _M0L6_2atmpS3217;
  float _M0L6_2atmpS3216;
  double _M0L1lS925;
  struct _M0TPB8MutLocalGdE* _M0L1pS926;
  struct _M0TPB8MutLocalGiE* _M0L1kS927;
  float _M0L6_2atmpS3215;
  int32_t _M0L8ten__lamS929;
  int32_t _M0L3capS928;
  int32_t _M0L3valS3214;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  if (_M0L6lambdaS924 <= 0x0p+0f) {
    return 0;
  }
  _M0L6_2atmpS3217 = -_M0L6lambdaS924;
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3216 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3217);
  _M0L1lS925 = (double)_M0L6_2atmpS3216;
  _M0L1pS926
  = (struct _M0TPB8MutLocalGdE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGdE));
  Moonbit_object_header(_M0L1pS926)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1pS926->$0 = 0x1p+0;
  _M0L1kS927
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS927)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS927->$0 = 0;
  _M0L6_2atmpS3215 = _M0L6lambdaS924 * 0x1.4p+3f;
  #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L8ten__lamS929 = _M0MPC15float5Float7to__int(_M0L6_2atmpS3215);
  if (_M0L8ten__lamS929 > 100) {
    _M0L3capS928 = _M0L8ten__lamS929;
  } else {
    _M0L3capS928 = 100;
  }
  while (1) {
    int32_t _M0L3valS3206 = _M0L1kS927->$0;
    int32_t _M0L6_2atmpS3205 = _M0L3valS3206 + 1;
    double _M0L3valS3208;
    double _M0L6_2atmpS3209;
    double _M0L6_2atmpS3207;
    double _M0L3valS3210;
    int32_t _M0L3valS3212;
    _M0L1kS927->$0 = _M0L6_2atmpS3205;
    _M0L3valS3208 = _M0L1pS926->$0;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
    _M0L6_2atmpS3209 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS930);
    _M0L6_2atmpS3207 = _M0L3valS3208 * _M0L6_2atmpS3209;
    _M0L1pS926->$0 = _M0L6_2atmpS3207;
    _M0L3valS3210 = _M0L1pS926->$0;
    if (_M0L3valS3210 < _M0L1lS925) {
      int32_t _M0L3valS3211;
      moonbit_decref_cycle_free(_M0L1pS926);
      _M0L3valS3211 = _M0L1kS927->$0;
      moonbit_decref_cycle_free(_M0L1kS927);
      return _M0L3valS3211 - 1;
    }
    _M0L3valS3212 = _M0L1kS927->$0;
    if (_M0L3valS3212 > _M0L3capS928) {
      int32_t _M0L3valS3213;
      moonbit_decref_cycle_free(_M0L1pS926);
      _M0L3valS3213 = _M0L1kS927->$0;
      moonbit_decref_cycle_free(_M0L1kS927);
      return _M0L3valS3213 - 1;
    }
    continue;
    break;
  }
  _M0L3valS3214 = _M0L1kS927->$0;
  moonbit_decref_cycle_free(_M0L1kS927);
  return _M0L3valS3214 - 1;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS922
) {
  uint64_t _M0L1uS921;
  uint64_t _M0L4bitsS923;
  double _M0L6_2atmpS3204;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS921 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS922);
  _M0L4bitsS923 = _M0L1uS921 >> 11;
  _M0L6_2atmpS3204 = (double)_M0L4bitsS923;
  return _M0L6_2atmpS3204 * 0x1p-53;
}

int32_t _M0FP26RiantR8snn__mbt20stimulate__spiketime(
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L1sS915,
  float _M0L1tS917,
  float _M0L1wS919
) {
  struct _M0TPB8MutLocalGiE* _M0L1iS914;
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L1iS914
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS914)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS914->$0 = 0;
  while (1) {
    int32_t _M0L3valS3166 = _M0L1iS914->$0;
    int32_t _M0L1nS3167 = _M0L1sS915->$0;
    if (_M0L3valS3166 < _M0L1nS3167) {
      struct _M0TPB5ArrayGbE* _M0L4fireS3168 = _M0L1sS915->$4;
      int32_t _M0L3valS3169 = _M0L1iS914->$0;
      int32_t _M0L3valS3171;
      int32_t _M0L6_2atmpS3170;
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3168, _M0L3valS3169, 0);
      _M0L3valS3171 = _M0L1iS914->$0;
      _M0L6_2atmpS3170 = _M0L3valS3171 + 1;
      _M0L1iS914->$0 = _M0L6_2atmpS3170;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS914);
    }
    break;
  }
  while (1) {
    struct _M0TPB5ArrayGiE* _M0L11next__indexS3175 = _M0L1sS915->$3;
    int32_t _M0L6_2atmpS3174;
    int32_t _if__result_5840;
    #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
    _M0L6_2atmpS3174 = _M0MPC15array5Array2atGiE(_M0L11next__indexS3175, 0);
    if (_M0L6_2atmpS3174 >= 0) {
      struct _M0TPB5ArrayGfE* _M0L11next__spikeS3173 = _M0L1sS915->$2;
      float _M0L6_2atmpS3172;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3172 = _M0MPC15array5Array2atGfE(_M0L11next__spikeS3173, 0);
      _if__result_5840 = _M0L6_2atmpS3172 <= _M0L1tS917;
    } else {
      _if__result_5840 = 0;
    }
    if (_if__result_5840) {
      struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS3203 =
        _M0L1sS915->$1;
      struct _M0TPB5ArrayGiE* _M0L7neuronsS3200 = _M0L5paramS3203->$1;
      struct _M0TPB5ArrayGiE* _M0L11next__indexS3202 = _M0L1sS915->$3;
      int32_t _M0L6_2atmpS3201;
      int32_t _M0L1jS918;
      struct _M0TPB5ArrayGbE* _M0L4fireS3176;
      struct _M0TPB5ArrayGfE* _M0L1gS3177;
      struct _M0TPB5ArrayGfE* _M0L1gS3180;
      float _M0L6_2atmpS3179;
      float _M0L6_2atmpS3178;
      struct _M0TPB5ArrayGiE* _M0L11next__indexS3186;
      int32_t _M0L6_2atmpS3185;
      int32_t _M0L6_2atmpS3181;
      struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS3184;
      struct _M0TPB5ArrayGfE* _M0L10spiketimesS3183;
      int32_t _M0L6_2atmpS3182;
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3201 = _M0MPC15array5Array2atGiE(_M0L11next__indexS3202, 0);
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L1jS918
      = _M0MPC15array5Array2atGiE(_M0L7neuronsS3200, _M0L6_2atmpS3201);
      _M0L4fireS3176 = _M0L1sS915->$4;
      #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3176, _M0L1jS918, 1);
      _M0L1gS3177 = _M0L1sS915->$5;
      _M0L1gS3180 = _M0L1sS915->$5;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3179 = _M0MPC15array5Array2atGfE(_M0L1gS3180, _M0L1jS918);
      _M0L6_2atmpS3178 = _M0L6_2atmpS3179 + _M0L1wS919;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS3177, _M0L1jS918, _M0L6_2atmpS3178);
      _M0L11next__indexS3186 = _M0L1sS915->$3;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3185 = _M0MPC15array5Array2atGiE(_M0L11next__indexS3186, 0);
      _M0L6_2atmpS3181 = _M0L6_2atmpS3185 + 1;
      _M0L5paramS3184 = _M0L1sS915->$1;
      _M0L10spiketimesS3183 = _M0L5paramS3184->$0;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3182 = _M0MPC15array5Array6lengthGfE(_M0L10spiketimesS3183);
      if (_M0L6_2atmpS3181 < _M0L6_2atmpS3182) {
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3187 = _M0L1sS915->$3;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3190 = _M0L1sS915->$3;
        int32_t _M0L6_2atmpS3189;
        int32_t _M0L6_2atmpS3188;
        struct _M0TPB5ArrayGfE* _M0L11next__spikeS3191;
        struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS3196;
        struct _M0TPB5ArrayGfE* _M0L10spiketimesS3193;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3195;
        int32_t _M0L6_2atmpS3194;
        float _M0L6_2atmpS3192;
        #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS3189
        = _M0MPC15array5Array2atGiE(_M0L11next__indexS3190, 0);
        _M0L6_2atmpS3188 = _M0L6_2atmpS3189 + 1;
        #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGiE(_M0L11next__indexS3187, 0, _M0L6_2atmpS3188);
        _M0L11next__spikeS3191 = _M0L1sS915->$2;
        _M0L5paramS3196 = _M0L1sS915->$1;
        _M0L10spiketimesS3193 = _M0L5paramS3196->$0;
        _M0L11next__indexS3195 = _M0L1sS915->$3;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS3194
        = _M0MPC15array5Array2atGiE(_M0L11next__indexS3195, 0);
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS3192
        = _M0MPC15array5Array2atGfE(_M0L10spiketimesS3193, _M0L6_2atmpS3194);
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGfE(_M0L11next__spikeS3191, 0, _M0L6_2atmpS3192);
      } else {
        struct _M0TPB5ArrayGfE* _M0L11next__spikeS3197 = _M0L1sS915->$2;
        float _M0L6_2atmpS3198 = 0x0p+0f / (float)MOONBIT_ZERO;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3199;
        #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGfE(_M0L11next__spikeS3197, 0, _M0L6_2atmpS3198);
        _M0L11next__indexS3199 = _M0L1sS915->$3;
        #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGiE(_M0L11next__indexS3199, 0, -1);
      }
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28markram__stp__step__timestep(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS901,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS899,
  struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0L5paramS903,
  float _M0L6t__nowS898,
  float _M0L2dtS908
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3079;
  int32_t _M0L6_2atmpS3078;
  int32_t _if__result_5841;
  int32_t _M0L6n__preS900;
  struct _M0TPB5ArrayGfE* _M0L3rhoS3081;
  int32_t _M0L6_2atmpS3080;
  float _M0L11u__baselineS902;
  float _M0L6tau__fS3165;
  float _M0L11inv__tau__fS904;
  float _M0L6tau__dS3164;
  float _M0L11inv__tau__dS905;
  struct _M0TPB8MutLocalGiE* _M0L1jS906;
  #line 473 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS3079 = _M0L4varsS899->$6;
  #line 482 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3078 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3079);
  if (_M0L6_2atmpS3078 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3077 = _M0L4varsS899->$6;
    int32_t _M0L6_2atmpS3076;
    #line 482 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS3076 = _M0MPC15array5Array2atGbE(_M0L6activeS3077, 0);
    _if__result_5841 = !_M0L6_2atmpS3076;
  } else {
    _if__result_5841 = 0;
  }
  if (_if__result_5841) {
    return 0;
  }
  _M0L6n__preS900 = _M0L4varsS899->$0;
  _M0L3rhoS3081 = _M0L3synS901->$6;
  #line 487 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3080 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS3081);
  if (_M0L6_2atmpS3080 == 0) {
    return 0;
  }
  _M0L11u__baselineS902 = _M0L5paramS903->$0;
  _M0L6tau__fS3165 = _M0L5paramS903->$1;
  _M0L11inv__tau__fS904 = 0x1p+0f / _M0L6tau__fS3165;
  _M0L6tau__dS3164 = _M0L5paramS903->$2;
  _M0L11inv__tau__dS905 = 0x1p+0f / _M0L6tau__dS3164;
  _M0L1jS906
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS906)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS906->$0 = 0;
  while (1) {
    int32_t _M0L3valS3082 = _M0L1jS906->$0;
    if (_M0L3valS3082 < _M0L6n__preS900) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3085 = _M0L3synS901->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3083 = _M0L3preS3085->$5;
      int32_t _M0L3valS3084 = _M0L1jS906->$0;
      int32_t _M0L3valS3112;
      int32_t _M0L6_2atmpS3111;
      #line 496 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3083, _M0L3valS3084)) {
        struct _M0TPB5ArrayGfE* _M0L1uS3086 = _M0L4varsS899->$2;
        int32_t _M0L3valS3087 = _M0L1jS906->$0;
        struct _M0TPB5ArrayGfE* _M0L1uS3095 = _M0L4varsS899->$2;
        int32_t _M0L3valS3096 = _M0L1jS906->$0;
        float _M0L6_2atmpS3089;
        struct _M0TPB5ArrayGfE* _M0L1uS3093;
        int32_t _M0L3valS3094;
        float _M0L6_2atmpS3092;
        float _M0L6_2atmpS3091;
        float _M0L6_2atmpS3090;
        float _M0L6_2atmpS3088;
        struct _M0TPB5ArrayGfE* _M0L1xS3097;
        int32_t _M0L3valS3098;
        struct _M0TPB5ArrayGfE* _M0L1xS3109;
        int32_t _M0L3valS3110;
        float _M0L6_2atmpS3100;
        struct _M0TPB5ArrayGfE* _M0L1uS3107;
        int32_t _M0L3valS3108;
        float _M0L6_2atmpS3106;
        float _M0L6_2atmpS3102;
        struct _M0TPB5ArrayGfE* _M0L1xS3104;
        int32_t _M0L3valS3105;
        float _M0L6_2atmpS3103;
        float _M0L6_2atmpS3101;
        float _M0L6_2atmpS3099;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3089
        = _M0MPC15array5Array2atGfE(_M0L1uS3095, _M0L3valS3096);
        _M0L1uS3093 = _M0L4varsS899->$2;
        _M0L3valS3094 = _M0L1jS906->$0;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3092
        = _M0MPC15array5Array2atGfE(_M0L1uS3093, _M0L3valS3094);
        _M0L6_2atmpS3091 = 0x1p+0f - _M0L6_2atmpS3092;
        _M0L6_2atmpS3090 = _M0L11u__baselineS902 * _M0L6_2atmpS3091;
        _M0L6_2atmpS3088 = _M0L6_2atmpS3089 + _M0L6_2atmpS3090;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3086, _M0L3valS3087, _M0L6_2atmpS3088);
        _M0L1xS3097 = _M0L4varsS899->$3;
        _M0L3valS3098 = _M0L1jS906->$0;
        _M0L1xS3109 = _M0L4varsS899->$3;
        _M0L3valS3110 = _M0L1jS906->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3100
        = _M0MPC15array5Array2atGfE(_M0L1xS3109, _M0L3valS3110);
        _M0L1uS3107 = _M0L4varsS899->$2;
        _M0L3valS3108 = _M0L1jS906->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3106
        = _M0MPC15array5Array2atGfE(_M0L1uS3107, _M0L3valS3108);
        _M0L6_2atmpS3102 = -_M0L6_2atmpS3106;
        _M0L1xS3104 = _M0L4varsS899->$3;
        _M0L3valS3105 = _M0L1jS906->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3103
        = _M0MPC15array5Array2atGfE(_M0L1xS3104, _M0L3valS3105);
        _M0L6_2atmpS3101 = _M0L6_2atmpS3102 * _M0L6_2atmpS3103;
        _M0L6_2atmpS3099 = _M0L6_2atmpS3100 + _M0L6_2atmpS3101;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3097, _M0L3valS3098, _M0L6_2atmpS3099);
      }
      _M0L3valS3112 = _M0L1jS906->$0;
      _M0L6_2atmpS3111 = _M0L3valS3112 + 1;
      _M0L1jS906->$0 = _M0L6_2atmpS3111;
      continue;
    }
    break;
  }
  _M0L1jS906->$0 = 0;
  while (1) {
    int32_t _M0L3valS3113 = _M0L1jS906->$0;
    if (_M0L3valS3113 < _M0L6n__preS900) {
      struct _M0TPB5ArrayGfE* _M0L1uS3114 = _M0L4varsS899->$2;
      int32_t _M0L3valS3115 = _M0L1jS906->$0;
      struct _M0TPB5ArrayGfE* _M0L1uS3124 = _M0L4varsS899->$2;
      int32_t _M0L3valS3125 = _M0L1jS906->$0;
      float _M0L6_2atmpS3117;
      struct _M0TPB5ArrayGfE* _M0L1uS3122;
      int32_t _M0L3valS3123;
      float _M0L6_2atmpS3121;
      float _M0L6_2atmpS3120;
      float _M0L6_2atmpS3119;
      float _M0L6_2atmpS3118;
      float _M0L6_2atmpS3116;
      struct _M0TPB5ArrayGfE* _M0L1xS3126;
      int32_t _M0L3valS3127;
      struct _M0TPB5ArrayGfE* _M0L1xS3136;
      int32_t _M0L3valS3137;
      float _M0L6_2atmpS3129;
      struct _M0TPB5ArrayGfE* _M0L1xS3134;
      int32_t _M0L3valS3135;
      float _M0L6_2atmpS3133;
      float _M0L6_2atmpS3132;
      float _M0L6_2atmpS3131;
      float _M0L6_2atmpS3130;
      float _M0L6_2atmpS3128;
      struct _M0TPB5ArrayGfE* _M0L8rho__preS3138;
      int32_t _M0L3valS3139;
      struct _M0TPB5ArrayGfE* _M0L1uS3145;
      int32_t _M0L3valS3146;
      float _M0L6_2atmpS3141;
      struct _M0TPB5ArrayGfE* _M0L1xS3143;
      int32_t _M0L3valS3144;
      float _M0L6_2atmpS3142;
      float _M0L6_2atmpS3140;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3163;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS3161;
      int32_t _M0L3valS3162;
      int32_t _M0L5startS909;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3160;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS3157;
      int32_t _M0L3valS3159;
      int32_t _M0L6_2atmpS3158;
      int32_t _M0L3endS910;
      struct _M0TPB8MutLocalGiE* _M0L1sS911;
      int32_t _M0L3valS3156;
      int32_t _M0L6_2atmpS3155;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3117
      = _M0MPC15array5Array2atGfE(_M0L1uS3124, _M0L3valS3125);
      _M0L1uS3122 = _M0L4varsS899->$2;
      _M0L3valS3123 = _M0L1jS906->$0;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3121
      = _M0MPC15array5Array2atGfE(_M0L1uS3122, _M0L3valS3123);
      _M0L6_2atmpS3120 = _M0L11u__baselineS902 - _M0L6_2atmpS3121;
      _M0L6_2atmpS3119 = _M0L2dtS908 * _M0L6_2atmpS3120;
      _M0L6_2atmpS3118 = _M0L6_2atmpS3119 * _M0L11inv__tau__fS904;
      _M0L6_2atmpS3116 = _M0L6_2atmpS3117 + _M0L6_2atmpS3118;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS3114, _M0L3valS3115, _M0L6_2atmpS3116);
      _M0L1xS3126 = _M0L4varsS899->$3;
      _M0L3valS3127 = _M0L1jS906->$0;
      _M0L1xS3136 = _M0L4varsS899->$3;
      _M0L3valS3137 = _M0L1jS906->$0;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3129
      = _M0MPC15array5Array2atGfE(_M0L1xS3136, _M0L3valS3137);
      _M0L1xS3134 = _M0L4varsS899->$3;
      _M0L3valS3135 = _M0L1jS906->$0;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3133
      = _M0MPC15array5Array2atGfE(_M0L1xS3134, _M0L3valS3135);
      _M0L6_2atmpS3132 = 0x1p+0f - _M0L6_2atmpS3133;
      _M0L6_2atmpS3131 = _M0L2dtS908 * _M0L6_2atmpS3132;
      _M0L6_2atmpS3130 = _M0L6_2atmpS3131 * _M0L11inv__tau__dS905;
      _M0L6_2atmpS3128 = _M0L6_2atmpS3129 + _M0L6_2atmpS3130;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS3126, _M0L3valS3127, _M0L6_2atmpS3128);
      _M0L8rho__preS3138 = _M0L4varsS899->$4;
      _M0L3valS3139 = _M0L1jS906->$0;
      _M0L1uS3145 = _M0L4varsS899->$2;
      _M0L3valS3146 = _M0L1jS906->$0;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3141
      = _M0MPC15array5Array2atGfE(_M0L1uS3145, _M0L3valS3146);
      _M0L1xS3143 = _M0L4varsS899->$3;
      _M0L3valS3144 = _M0L1jS906->$0;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3142
      = _M0MPC15array5Array2atGfE(_M0L1xS3143, _M0L3valS3144);
      _M0L6_2atmpS3140 = _M0L6_2atmpS3141 * _M0L6_2atmpS3142;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L8rho__preS3138, _M0L3valS3139, _M0L6_2atmpS3140);
      _M0L6matrixS3163 = _M0L3synS901->$4;
      _M0L6rowptrS3161 = _M0L6matrixS3163->$2;
      _M0L3valS3162 = _M0L1jS906->$0;
      #line 509 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L5startS909
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS3161, _M0L3valS3162);
      _M0L6matrixS3160 = _M0L3synS901->$4;
      _M0L6rowptrS3157 = _M0L6matrixS3160->$2;
      _M0L3valS3159 = _M0L1jS906->$0;
      _M0L6_2atmpS3158 = _M0L3valS3159 + 1;
      #line 510 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L3endS910
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS3157, _M0L6_2atmpS3158);
      _M0L1sS911
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS911)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS911->$0 = _M0L5startS909;
      while (1) {
        int32_t _M0L3valS3147 = _M0L1sS911->$0;
        if (_M0L3valS3147 < _M0L3endS910) {
          struct _M0TPB5ArrayGfE* _M0L3rhoS3148 = _M0L3synS901->$6;
          int32_t _M0L3valS3149 = _M0L1sS911->$0;
          struct _M0TPB5ArrayGfE* _M0L8rho__preS3151 = _M0L4varsS899->$4;
          int32_t _M0L3valS3152 = _M0L1jS906->$0;
          float _M0L6_2atmpS3150;
          int32_t _M0L3valS3154;
          int32_t _M0L6_2atmpS3153;
          #line 513 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
          _M0L6_2atmpS3150
          = _M0MPC15array5Array2atGfE(_M0L8rho__preS3151, _M0L3valS3152);
          #line 513 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rhoS3148, _M0L3valS3149, _M0L6_2atmpS3150);
          _M0L3valS3154 = _M0L1sS911->$0;
          _M0L6_2atmpS3153 = _M0L3valS3154 + 1;
          _M0L1sS911->$0 = _M0L6_2atmpS3153;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS911);
        }
        break;
      }
      _M0L3valS3156 = _M0L1jS906->$0;
      _M0L6_2atmpS3155 = _M0L3valS3156 + 1;
      _M0L1jS906->$0 = _M0L6_2atmpS3155;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS906);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23markram__stp__step__het(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS882,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS880,
  struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet* _M0L5paramS885,
  float _M0L6t__nowS889
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS2987;
  int32_t _M0L6_2atmpS2986;
  int32_t _if__result_5845;
  int32_t _M0L6n__preS881;
  struct _M0TPB5ArrayGfE* _M0L3rhoS2989;
  int32_t _M0L6_2atmpS2988;
  struct _M0TPB8MutLocalGiE* _M0L1jS883;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS2987 = _M0L4varsS880->$6;
  #line 353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS2986 = _M0MPC15array5Array6lengthGbE(_M0L6activeS2987);
  if (_M0L6_2atmpS2986 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS2985 = _M0L4varsS880->$6;
    int32_t _M0L6_2atmpS2984;
    #line 353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS2984 = _M0MPC15array5Array2atGbE(_M0L6activeS2985, 0);
    _if__result_5845 = !_M0L6_2atmpS2984;
  } else {
    _if__result_5845 = 0;
  }
  if (_if__result_5845) {
    return 0;
  }
  _M0L6n__preS881 = _M0L4varsS880->$0;
  _M0L3rhoS2989 = _M0L3synS882->$6;
  #line 357 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS2988 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS2989);
  if (_M0L6_2atmpS2988 == 0) {
    return 0;
  }
  _M0L1jS883
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS883)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS883->$0 = 0;
  while (1) {
    int32_t _M0L3valS2990 = _M0L1jS883->$0;
    if (_M0L3valS2990 < _M0L6n__preS881) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2993 = _M0L3synS882->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2991 = _M0L3preS2993->$5;
      int32_t _M0L3valS2992 = _M0L1jS883->$0;
      int32_t _M0L3valS3075;
      int32_t _M0L6_2atmpS3074;
      #line 362 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2991, _M0L3valS2992)) {
        struct _M0TPB5ArrayGfE* _M0L6tau__dS3072 = _M0L5paramS885->$0;
        int32_t _M0L3valS3073 = _M0L1jS883->$0;
        float _M0L9tau__d__jS884;
        struct _M0TPB5ArrayGfE* _M0L6tau__fS3070;
        int32_t _M0L3valS3071;
        float _M0L9tau__f__jS886;
        struct _M0TPB5ArrayGfE* _M0L1uS3068;
        int32_t _M0L3valS3069;
        float _M0L14u__baseline__jS887;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS3066;
        int32_t _M0L3valS3067;
        float _M0L6_2atmpS3065;
        float _M0L7dt__preS888;
        float _M0L7dt__preS890;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS2994;
        int32_t _M0L3valS2995;
        float _M0L6_2atmpS3064;
        float _M0L6arg__fS891;
        struct _M0TPB5ArrayGfE* _M0L1uS2996;
        int32_t _M0L3valS2997;
        struct _M0TPB5ArrayGfE* _M0L1uS3003;
        int32_t _M0L3valS3004;
        float _M0L6_2atmpS3002;
        float _M0L6_2atmpS3000;
        float _M0L6_2atmpS3001;
        float _M0L6_2atmpS2999;
        float _M0L6_2atmpS2998;
        float _M0L6_2atmpS3063;
        float _M0L6arg__dS892;
        struct _M0TPB5ArrayGfE* _M0L1xS3005;
        int32_t _M0L3valS3006;
        struct _M0TPB5ArrayGfE* _M0L1xS3012;
        int32_t _M0L3valS3013;
        float _M0L6_2atmpS3011;
        float _M0L6_2atmpS3009;
        float _M0L6_2atmpS3010;
        float _M0L6_2atmpS3008;
        float _M0L6_2atmpS3007;
        struct _M0TPB5ArrayGfE* _M0L8rho__preS3014;
        int32_t _M0L3valS3015;
        struct _M0TPB5ArrayGfE* _M0L1uS3021;
        int32_t _M0L3valS3022;
        float _M0L6_2atmpS3017;
        struct _M0TPB5ArrayGfE* _M0L1xS3019;
        int32_t _M0L3valS3020;
        float _M0L6_2atmpS3018;
        float _M0L6_2atmpS3016;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3062;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3060;
        int32_t _M0L3valS3061;
        int32_t _M0L5startS893;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3059;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3056;
        int32_t _M0L3valS3058;
        int32_t _M0L6_2atmpS3057;
        int32_t _M0L3endS894;
        struct _M0TPB8MutLocalGiE* _M0L1sS895;
        struct _M0TPB5ArrayGfE* _M0L1uS3031;
        int32_t _M0L3valS3032;
        struct _M0TPB5ArrayGfE* _M0L1uS3040;
        int32_t _M0L3valS3041;
        float _M0L6_2atmpS3034;
        struct _M0TPB5ArrayGfE* _M0L1uS3038;
        int32_t _M0L3valS3039;
        float _M0L6_2atmpS3037;
        float _M0L6_2atmpS3036;
        float _M0L6_2atmpS3035;
        float _M0L6_2atmpS3033;
        struct _M0TPB5ArrayGfE* _M0L1xS3042;
        int32_t _M0L3valS3043;
        struct _M0TPB5ArrayGfE* _M0L1xS3054;
        int32_t _M0L3valS3055;
        float _M0L6_2atmpS3045;
        struct _M0TPB5ArrayGfE* _M0L1uS3052;
        int32_t _M0L3valS3053;
        float _M0L6_2atmpS3051;
        float _M0L6_2atmpS3047;
        struct _M0TPB5ArrayGfE* _M0L1xS3049;
        int32_t _M0L3valS3050;
        float _M0L6_2atmpS3048;
        float _M0L6_2atmpS3046;
        float _M0L6_2atmpS3044;
        #line 363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L9tau__d__jS884
        = _M0MPC15array5Array2atGfE(_M0L6tau__dS3072, _M0L3valS3073);
        _M0L6tau__fS3070 = _M0L5paramS885->$1;
        _M0L3valS3071 = _M0L1jS883->$0;
        #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L9tau__f__jS886
        = _M0MPC15array5Array2atGfE(_M0L6tau__fS3070, _M0L3valS3071);
        _M0L1uS3068 = _M0L5paramS885->$2;
        _M0L3valS3069 = _M0L1jS883->$0;
        #line 365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L14u__baseline__jS887
        = _M0MPC15array5Array2atGfE(_M0L1uS3068, _M0L3valS3069);
        _M0L11last__spikeS3066 = _M0L4varsS880->$5;
        _M0L3valS3067 = _M0L1jS883->$0;
        #line 366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3065
        = _M0MPC15array5Array2atGfE(_M0L11last__spikeS3066, _M0L3valS3067);
        _M0L7dt__preS888 = _M0L6t__nowS889 - _M0L6_2atmpS3065;
        if (_M0L7dt__preS888 < 0x0p+0f) {
          _M0L7dt__preS890 = 0x0p+0f;
        } else {
          _M0L7dt__preS890 = _M0L7dt__preS888;
        }
        _M0L11last__spikeS2994 = _M0L4varsS880->$5;
        _M0L3valS2995 = _M0L1jS883->$0;
        #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L11last__spikeS2994, _M0L3valS2995, _M0L6t__nowS889);
        _M0L6_2atmpS3064 = -_M0L7dt__preS890;
        _M0L6arg__fS891 = _M0L6_2atmpS3064 / _M0L9tau__f__jS886;
        _M0L1uS2996 = _M0L4varsS880->$2;
        _M0L3valS2997 = _M0L1jS883->$0;
        _M0L1uS3003 = _M0L4varsS880->$2;
        _M0L3valS3004 = _M0L1jS883->$0;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3002
        = _M0MPC15array5Array2atGfE(_M0L1uS3003, _M0L3valS3004);
        _M0L6_2atmpS3000 = _M0L14u__baseline__jS887 - _M0L6_2atmpS3002;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3001 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__fS891);
        _M0L6_2atmpS2999 = _M0L6_2atmpS3000 * _M0L6_2atmpS3001;
        _M0L6_2atmpS2998 = _M0L14u__baseline__jS887 - _M0L6_2atmpS2999;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS2996, _M0L3valS2997, _M0L6_2atmpS2998);
        _M0L6_2atmpS3063 = -_M0L7dt__preS890;
        _M0L6arg__dS892 = _M0L6_2atmpS3063 / _M0L9tau__d__jS884;
        _M0L1xS3005 = _M0L4varsS880->$3;
        _M0L3valS3006 = _M0L1jS883->$0;
        _M0L1xS3012 = _M0L4varsS880->$3;
        _M0L3valS3013 = _M0L1jS883->$0;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3011
        = _M0MPC15array5Array2atGfE(_M0L1xS3012, _M0L3valS3013);
        _M0L6_2atmpS3009 = 0x1p+0f - _M0L6_2atmpS3011;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3010 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__dS892);
        _M0L6_2atmpS3008 = _M0L6_2atmpS3009 * _M0L6_2atmpS3010;
        _M0L6_2atmpS3007 = 0x1p+0f - _M0L6_2atmpS3008;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3005, _M0L3valS3006, _M0L6_2atmpS3007);
        _M0L8rho__preS3014 = _M0L4varsS880->$4;
        _M0L3valS3015 = _M0L1jS883->$0;
        _M0L1uS3021 = _M0L4varsS880->$2;
        _M0L3valS3022 = _M0L1jS883->$0;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3017
        = _M0MPC15array5Array2atGfE(_M0L1uS3021, _M0L3valS3022);
        _M0L1xS3019 = _M0L4varsS880->$3;
        _M0L3valS3020 = _M0L1jS883->$0;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3018
        = _M0MPC15array5Array2atGfE(_M0L1xS3019, _M0L3valS3020);
        _M0L6_2atmpS3016 = _M0L6_2atmpS3017 * _M0L6_2atmpS3018;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L8rho__preS3014, _M0L3valS3015, _M0L6_2atmpS3016);
        _M0L6matrixS3062 = _M0L3synS882->$4;
        _M0L6rowptrS3060 = _M0L6matrixS3062->$2;
        _M0L3valS3061 = _M0L1jS883->$0;
        #line 374 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L5startS893
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3060, _M0L3valS3061);
        _M0L6matrixS3059 = _M0L3synS882->$4;
        _M0L6rowptrS3056 = _M0L6matrixS3059->$2;
        _M0L3valS3058 = _M0L1jS883->$0;
        _M0L6_2atmpS3057 = _M0L3valS3058 + 1;
        #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L3endS894
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3056, _M0L6_2atmpS3057);
        _M0L1sS895
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS895)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS895->$0 = _M0L5startS893;
        while (1) {
          int32_t _M0L3valS3023 = _M0L1sS895->$0;
          if (_M0L3valS3023 < _M0L3endS894) {
            struct _M0TPB5ArrayGfE* _M0L3rhoS3024 = _M0L3synS882->$6;
            int32_t _M0L3valS3025 = _M0L1sS895->$0;
            struct _M0TPB5ArrayGfE* _M0L8rho__preS3027 = _M0L4varsS880->$4;
            int32_t _M0L3valS3028 = _M0L1jS883->$0;
            float _M0L6_2atmpS3026;
            int32_t _M0L3valS3030;
            int32_t _M0L6_2atmpS3029;
            #line 378 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0L6_2atmpS3026
            = _M0MPC15array5Array2atGfE(_M0L8rho__preS3027, _M0L3valS3028);
            #line 378 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0MPC15array5Array3setGfE(_M0L3rhoS3024, _M0L3valS3025, _M0L6_2atmpS3026);
            _M0L3valS3030 = _M0L1sS895->$0;
            _M0L6_2atmpS3029 = _M0L3valS3030 + 1;
            _M0L1sS895->$0 = _M0L6_2atmpS3029;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS895);
          }
          break;
        }
        _M0L1uS3031 = _M0L4varsS880->$2;
        _M0L3valS3032 = _M0L1jS883->$0;
        _M0L1uS3040 = _M0L4varsS880->$2;
        _M0L3valS3041 = _M0L1jS883->$0;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3034
        = _M0MPC15array5Array2atGfE(_M0L1uS3040, _M0L3valS3041);
        _M0L1uS3038 = _M0L4varsS880->$2;
        _M0L3valS3039 = _M0L1jS883->$0;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3037
        = _M0MPC15array5Array2atGfE(_M0L1uS3038, _M0L3valS3039);
        _M0L6_2atmpS3036 = 0x1p+0f - _M0L6_2atmpS3037;
        _M0L6_2atmpS3035 = _M0L14u__baseline__jS887 * _M0L6_2atmpS3036;
        _M0L6_2atmpS3033 = _M0L6_2atmpS3034 + _M0L6_2atmpS3035;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3031, _M0L3valS3032, _M0L6_2atmpS3033);
        _M0L1xS3042 = _M0L4varsS880->$3;
        _M0L3valS3043 = _M0L1jS883->$0;
        _M0L1xS3054 = _M0L4varsS880->$3;
        _M0L3valS3055 = _M0L1jS883->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3045
        = _M0MPC15array5Array2atGfE(_M0L1xS3054, _M0L3valS3055);
        _M0L1uS3052 = _M0L4varsS880->$2;
        _M0L3valS3053 = _M0L1jS883->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3051
        = _M0MPC15array5Array2atGfE(_M0L1uS3052, _M0L3valS3053);
        _M0L6_2atmpS3047 = -_M0L6_2atmpS3051;
        _M0L1xS3049 = _M0L4varsS880->$3;
        _M0L3valS3050 = _M0L1jS883->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3048
        = _M0MPC15array5Array2atGfE(_M0L1xS3049, _M0L3valS3050);
        _M0L6_2atmpS3046 = _M0L6_2atmpS3047 * _M0L6_2atmpS3048;
        _M0L6_2atmpS3044 = _M0L6_2atmpS3045 + _M0L6_2atmpS3046;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3042, _M0L3valS3043, _M0L6_2atmpS3044);
      }
      _M0L3valS3075 = _M0L1jS883->$0;
      _M0L6_2atmpS3074 = _M0L3valS3075 + 1;
      _M0L1jS883->$0 = _M0L6_2atmpS3074;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS883);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt18markram__stp__step(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS864,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS862,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter* _M0L5paramS866,
  float _M0L6t__nowS871
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS2901;
  int32_t _M0L6_2atmpS2900;
  int32_t _if__result_5848;
  int32_t _M0L6n__preS863;
  struct _M0TPB5ArrayGfE* _M0L3rhoS2903;
  int32_t _M0L6_2atmpS2902;
  float _M0L6tau__fS865;
  float _M0L6tau__dS867;
  float _M0L11u__baselineS868;
  struct _M0TPB8MutLocalGiE* _M0L1jS869;
  #line 202 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS2901 = _M0L4varsS862->$6;
  #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS2900 = _M0MPC15array5Array6lengthGbE(_M0L6activeS2901);
  if (_M0L6_2atmpS2900 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS2899 = _M0L4varsS862->$6;
    int32_t _M0L6_2atmpS2898;
    #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS2898 = _M0MPC15array5Array2atGbE(_M0L6activeS2899, 0);
    _if__result_5848 = !_M0L6_2atmpS2898;
  } else {
    _if__result_5848 = 0;
  }
  if (_if__result_5848) {
    return 0;
  }
  _M0L6n__preS863 = _M0L4varsS862->$0;
  _M0L3rhoS2903 = _M0L3synS864->$6;
  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS2902 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS2903);
  if (_M0L6_2atmpS2902 == 0) {
    return 0;
  }
  _M0L6tau__fS865 = _M0L5paramS866->$1;
  _M0L6tau__dS867 = _M0L5paramS866->$0;
  _M0L11u__baselineS868 = _M0L5paramS866->$2;
  _M0L1jS869
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS869)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS869->$0 = 0;
  while (1) {
    int32_t _M0L3valS2904 = _M0L1jS869->$0;
    if (_M0L3valS2904 < _M0L6n__preS863) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2907 = _M0L3synS864->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2905 = _M0L3preS2907->$5;
      int32_t _M0L3valS2906 = _M0L1jS869->$0;
      int32_t _M0L3valS2983;
      int32_t _M0L6_2atmpS2982;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2905, _M0L3valS2906)) {
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS2980 = _M0L4varsS862->$5;
        int32_t _M0L3valS2981 = _M0L1jS869->$0;
        float _M0L6_2atmpS2979;
        float _M0L7dt__preS870;
        float _M0L7dt__preS872;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS2908;
        int32_t _M0L3valS2909;
        float _M0L6_2atmpS2978;
        float _M0L6arg__fS873;
        struct _M0TPB5ArrayGfE* _M0L1uS2910;
        int32_t _M0L3valS2911;
        struct _M0TPB5ArrayGfE* _M0L1uS2917;
        int32_t _M0L3valS2918;
        float _M0L6_2atmpS2916;
        float _M0L6_2atmpS2914;
        float _M0L6_2atmpS2915;
        float _M0L6_2atmpS2913;
        float _M0L6_2atmpS2912;
        float _M0L6_2atmpS2977;
        float _M0L6arg__dS874;
        struct _M0TPB5ArrayGfE* _M0L1xS2919;
        int32_t _M0L3valS2920;
        struct _M0TPB5ArrayGfE* _M0L1xS2926;
        int32_t _M0L3valS2927;
        float _M0L6_2atmpS2925;
        float _M0L6_2atmpS2923;
        float _M0L6_2atmpS2924;
        float _M0L6_2atmpS2922;
        float _M0L6_2atmpS2921;
        struct _M0TPB5ArrayGfE* _M0L8rho__preS2928;
        int32_t _M0L3valS2929;
        struct _M0TPB5ArrayGfE* _M0L1uS2935;
        int32_t _M0L3valS2936;
        float _M0L6_2atmpS2931;
        struct _M0TPB5ArrayGfE* _M0L1xS2933;
        int32_t _M0L3valS2934;
        float _M0L6_2atmpS2932;
        float _M0L6_2atmpS2930;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2976;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2974;
        int32_t _M0L3valS2975;
        int32_t _M0L5startS875;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2973;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2970;
        int32_t _M0L3valS2972;
        int32_t _M0L6_2atmpS2971;
        int32_t _M0L3endS876;
        struct _M0TPB8MutLocalGiE* _M0L1sS877;
        struct _M0TPB5ArrayGfE* _M0L1uS2945;
        int32_t _M0L3valS2946;
        struct _M0TPB5ArrayGfE* _M0L1uS2954;
        int32_t _M0L3valS2955;
        float _M0L6_2atmpS2948;
        struct _M0TPB5ArrayGfE* _M0L1uS2952;
        int32_t _M0L3valS2953;
        float _M0L6_2atmpS2951;
        float _M0L6_2atmpS2950;
        float _M0L6_2atmpS2949;
        float _M0L6_2atmpS2947;
        struct _M0TPB5ArrayGfE* _M0L1xS2956;
        int32_t _M0L3valS2957;
        struct _M0TPB5ArrayGfE* _M0L1xS2968;
        int32_t _M0L3valS2969;
        float _M0L6_2atmpS2959;
        struct _M0TPB5ArrayGfE* _M0L1uS2966;
        int32_t _M0L3valS2967;
        float _M0L6_2atmpS2965;
        float _M0L6_2atmpS2961;
        struct _M0TPB5ArrayGfE* _M0L1xS2963;
        int32_t _M0L3valS2964;
        float _M0L6_2atmpS2962;
        float _M0L6_2atmpS2960;
        float _M0L6_2atmpS2958;
        #line 224 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2979
        = _M0MPC15array5Array2atGfE(_M0L11last__spikeS2980, _M0L3valS2981);
        _M0L7dt__preS870 = _M0L6t__nowS871 - _M0L6_2atmpS2979;
        if (_M0L7dt__preS870 < 0x0p+0f) {
          _M0L7dt__preS872 = 0x0p+0f;
        } else {
          _M0L7dt__preS872 = _M0L7dt__preS870;
        }
        _M0L11last__spikeS2908 = _M0L4varsS862->$5;
        _M0L3valS2909 = _M0L1jS869->$0;
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L11last__spikeS2908, _M0L3valS2909, _M0L6t__nowS871);
        _M0L6_2atmpS2978 = -_M0L7dt__preS872;
        _M0L6arg__fS873 = _M0L6_2atmpS2978 / _M0L6tau__fS865;
        _M0L1uS2910 = _M0L4varsS862->$2;
        _M0L3valS2911 = _M0L1jS869->$0;
        _M0L1uS2917 = _M0L4varsS862->$2;
        _M0L3valS2918 = _M0L1jS869->$0;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2916
        = _M0MPC15array5Array2atGfE(_M0L1uS2917, _M0L3valS2918);
        _M0L6_2atmpS2914 = _M0L11u__baselineS868 - _M0L6_2atmpS2916;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2915 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__fS873);
        _M0L6_2atmpS2913 = _M0L6_2atmpS2914 * _M0L6_2atmpS2915;
        _M0L6_2atmpS2912 = _M0L11u__baselineS868 - _M0L6_2atmpS2913;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS2910, _M0L3valS2911, _M0L6_2atmpS2912);
        _M0L6_2atmpS2977 = -_M0L7dt__preS872;
        _M0L6arg__dS874 = _M0L6_2atmpS2977 / _M0L6tau__dS867;
        _M0L1xS2919 = _M0L4varsS862->$3;
        _M0L3valS2920 = _M0L1jS869->$0;
        _M0L1xS2926 = _M0L4varsS862->$3;
        _M0L3valS2927 = _M0L1jS869->$0;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2925
        = _M0MPC15array5Array2atGfE(_M0L1xS2926, _M0L3valS2927);
        _M0L6_2atmpS2923 = 0x1p+0f - _M0L6_2atmpS2925;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2924 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__dS874);
        _M0L6_2atmpS2922 = _M0L6_2atmpS2923 * _M0L6_2atmpS2924;
        _M0L6_2atmpS2921 = 0x1p+0f - _M0L6_2atmpS2922;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS2919, _M0L3valS2920, _M0L6_2atmpS2921);
        _M0L8rho__preS2928 = _M0L4varsS862->$4;
        _M0L3valS2929 = _M0L1jS869->$0;
        _M0L1uS2935 = _M0L4varsS862->$2;
        _M0L3valS2936 = _M0L1jS869->$0;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2931
        = _M0MPC15array5Array2atGfE(_M0L1uS2935, _M0L3valS2936);
        _M0L1xS2933 = _M0L4varsS862->$3;
        _M0L3valS2934 = _M0L1jS869->$0;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2932
        = _M0MPC15array5Array2atGfE(_M0L1xS2933, _M0L3valS2934);
        _M0L6_2atmpS2930 = _M0L6_2atmpS2931 * _M0L6_2atmpS2932;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L8rho__preS2928, _M0L3valS2929, _M0L6_2atmpS2930);
        _M0L6matrixS2976 = _M0L3synS864->$4;
        _M0L6rowptrS2974 = _M0L6matrixS2976->$2;
        _M0L3valS2975 = _M0L1jS869->$0;
        #line 236 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L5startS875
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2974, _M0L3valS2975);
        _M0L6matrixS2973 = _M0L3synS864->$4;
        _M0L6rowptrS2970 = _M0L6matrixS2973->$2;
        _M0L3valS2972 = _M0L1jS869->$0;
        _M0L6_2atmpS2971 = _M0L3valS2972 + 1;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L3endS876
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2970, _M0L6_2atmpS2971);
        _M0L1sS877
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS877)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS877->$0 = _M0L5startS875;
        while (1) {
          int32_t _M0L3valS2937 = _M0L1sS877->$0;
          if (_M0L3valS2937 < _M0L3endS876) {
            struct _M0TPB5ArrayGfE* _M0L3rhoS2938 = _M0L3synS864->$6;
            int32_t _M0L3valS2939 = _M0L1sS877->$0;
            struct _M0TPB5ArrayGfE* _M0L8rho__preS2941 = _M0L4varsS862->$4;
            int32_t _M0L3valS2942 = _M0L1jS869->$0;
            float _M0L6_2atmpS2940;
            int32_t _M0L3valS2944;
            int32_t _M0L6_2atmpS2943;
            #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0L6_2atmpS2940
            = _M0MPC15array5Array2atGfE(_M0L8rho__preS2941, _M0L3valS2942);
            #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0MPC15array5Array3setGfE(_M0L3rhoS2938, _M0L3valS2939, _M0L6_2atmpS2940);
            _M0L3valS2944 = _M0L1sS877->$0;
            _M0L6_2atmpS2943 = _M0L3valS2944 + 1;
            _M0L1sS877->$0 = _M0L6_2atmpS2943;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS877);
          }
          break;
        }
        _M0L1uS2945 = _M0L4varsS862->$2;
        _M0L3valS2946 = _M0L1jS869->$0;
        _M0L1uS2954 = _M0L4varsS862->$2;
        _M0L3valS2955 = _M0L1jS869->$0;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2948
        = _M0MPC15array5Array2atGfE(_M0L1uS2954, _M0L3valS2955);
        _M0L1uS2952 = _M0L4varsS862->$2;
        _M0L3valS2953 = _M0L1jS869->$0;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2951
        = _M0MPC15array5Array2atGfE(_M0L1uS2952, _M0L3valS2953);
        _M0L6_2atmpS2950 = 0x1p+0f - _M0L6_2atmpS2951;
        _M0L6_2atmpS2949 = _M0L11u__baselineS868 * _M0L6_2atmpS2950;
        _M0L6_2atmpS2947 = _M0L6_2atmpS2948 + _M0L6_2atmpS2949;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS2945, _M0L3valS2946, _M0L6_2atmpS2947);
        _M0L1xS2956 = _M0L4varsS862->$3;
        _M0L3valS2957 = _M0L1jS869->$0;
        _M0L1xS2968 = _M0L4varsS862->$3;
        _M0L3valS2969 = _M0L1jS869->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2959
        = _M0MPC15array5Array2atGfE(_M0L1xS2968, _M0L3valS2969);
        _M0L1uS2966 = _M0L4varsS862->$2;
        _M0L3valS2967 = _M0L1jS869->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2965
        = _M0MPC15array5Array2atGfE(_M0L1uS2966, _M0L3valS2967);
        _M0L6_2atmpS2961 = -_M0L6_2atmpS2965;
        _M0L1xS2963 = _M0L4varsS862->$3;
        _M0L3valS2964 = _M0L1jS869->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2962
        = _M0MPC15array5Array2atGfE(_M0L1xS2963, _M0L3valS2964);
        _M0L6_2atmpS2960 = _M0L6_2atmpS2961 * _M0L6_2atmpS2962;
        _M0L6_2atmpS2958 = _M0L6_2atmpS2959 + _M0L6_2atmpS2960;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS2956, _M0L3valS2957, _M0L6_2atmpS2958);
      }
      _M0L3valS2983 = _M0L1jS869->$0;
      _M0L6_2atmpS2982 = _M0L3valS2983 + 1;
      _M0L1jS869->$0 = _M0L6_2atmpS2982;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS869);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12update__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS860,
  float _M0L2dtS861
) {
  struct _M0TPB5ArrayGfE* _M0L1tS2890;
  struct _M0TPB5ArrayGfE* _M0L1tS2893;
  float _M0L6_2atmpS2892;
  float _M0L6_2atmpS2891;
  struct _M0TPB5ArrayGiE* _M0L2ttS2894;
  struct _M0TPB5ArrayGiE* _M0L2ttS2897;
  int32_t _M0L6_2atmpS2896;
  int32_t _M0L6_2atmpS2895;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS2890 = _M0L1tS860->$0;
  _M0L1tS2893 = _M0L1tS860->$0;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2892 = _M0MPC15array5Array2atGfE(_M0L1tS2893, 0);
  _M0L6_2atmpS2891 = _M0L6_2atmpS2892 + _M0L2dtS861;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGfE(_M0L1tS2890, 0, _M0L6_2atmpS2891);
  _M0L2ttS2894 = _M0L1tS860->$1;
  _M0L2ttS2897 = _M0L1tS860->$1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2896 = _M0MPC15array5Array2atGiE(_M0L2ttS2897, 0);
  _M0L6_2atmpS2895 = _M0L6_2atmpS2896 + 1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGiE(_M0L2ttS2894, 0, _M0L6_2atmpS2895);
  return 0;
}

float _M0FP26RiantR8snn__mbt9get__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS859
) {
  struct _M0TPB5ArrayGfE* _M0L1tS2889;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS2889 = _M0L1tS859->$0;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  return _M0MPC15array5Array2atGfE(_M0L1tS2889, 0);
}

struct _M0TP26RiantR8snn__mbt4Time* _M0MP26RiantR8snn__mbt4Time3new() {
  float* _M0L6_2atmpS2888;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2885;
  int32_t* _M0L6_2atmpS2887;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2886;
  struct _M0TP26RiantR8snn__mbt4Time* _block_5851;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2888 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS2888[0] = 0x0p+0f;
  _M0L6_2atmpS2885
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2885)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
  _M0L6_2atmpS2885->$0 = _M0L6_2atmpS2888;
  _M0L6_2atmpS2885->$1 = 1;
  _M0L6_2atmpS2887 = (int32_t*)moonbit_make_int32_array_raw(1);
  _M0L6_2atmpS2887[0] = 0;
  _M0L6_2atmpS2886
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2886)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 74, 0);
  _M0L6_2atmpS2886->$0 = _M0L6_2atmpS2887;
  _M0L6_2atmpS2886->$1 = 1;
  _block_5851
  = (struct _M0TP26RiantR8snn__mbt4Time*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt4Time));
  Moonbit_object_header(_block_5851)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 77, 0);
  _block_5851->$0 = _M0L6_2atmpS2885;
  _block_5851->$1 = _M0L6_2atmpS2886;
  _block_5851->$2 = 0x1p-3f;
  return _block_5851;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS857
) {
  uint32_t _M0L1uS856;
  uint32_t _M0L4bitsS858;
  double _M0L6_2atmpS2884;
  double _M0L6_2atmpS2883;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS856 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS857);
  _M0L4bitsS858 = _M0L1uS856 >> 8;
  _M0L6_2atmpS2884 = (double)_M0L4bitsS858;
  _M0L6_2atmpS2883 = _M0L6_2atmpS2884 * 0x1p-24;
  return (float)_M0L6_2atmpS2883;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS855
) {
  uint64_t _M0L1uS854;
  uint64_t _M0L6_2atmpS2882;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS854 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS855);
  _M0L6_2atmpS2882 = _M0L1uS854 >> 32;
  return (uint32_t)_M0L6_2atmpS2882;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS847
) {
  uint64_t _M0L2s0S846;
  uint64_t _M0L2s1S848;
  uint64_t _M0L2s2S849;
  uint64_t _M0L2s3S850;
  uint64_t _M0L3tmpS851;
  uint64_t _M0L6_2atmpS2881;
  uint64_t _M0L3resS852;
  uint64_t _M0L1tS853;
  uint64_t _M0L6_2atmpS2871;
  uint64_t _M0L6_2atmpS2872;
  uint64_t _M0L2s2S2874;
  uint64_t _M0L6_2atmpS2873;
  uint64_t _M0L2s3S2876;
  uint64_t _M0L6_2atmpS2875;
  uint64_t _M0L2s2S2878;
  uint64_t _M0L6_2atmpS2877;
  uint64_t _M0L2s3S2880;
  uint64_t _M0L6_2atmpS2879;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S846 = _M0L1rS847->$0;
  _M0L2s1S848 = _M0L1rS847->$1;
  _M0L2s2S849 = _M0L1rS847->$2;
  _M0L2s3S850 = _M0L1rS847->$3;
  _M0L3tmpS851 = _M0L2s0S846 + _M0L2s3S850;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2881 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS851, 23);
  _M0L3resS852 = _M0L6_2atmpS2881 + _M0L2s0S846;
  _M0L1tS853 = _M0L2s1S848 << 17;
  _M0L6_2atmpS2871 = _M0L2s2S849 ^ _M0L2s0S846;
  _M0L1rS847->$2 = _M0L6_2atmpS2871;
  _M0L6_2atmpS2872 = _M0L2s3S850 ^ _M0L2s1S848;
  _M0L1rS847->$3 = _M0L6_2atmpS2872;
  _M0L2s2S2874 = _M0L1rS847->$2;
  _M0L6_2atmpS2873 = _M0L2s1S848 ^ _M0L2s2S2874;
  _M0L1rS847->$1 = _M0L6_2atmpS2873;
  _M0L2s3S2876 = _M0L1rS847->$3;
  _M0L6_2atmpS2875 = _M0L2s0S846 ^ _M0L2s3S2876;
  _M0L1rS847->$0 = _M0L6_2atmpS2875;
  _M0L2s2S2878 = _M0L1rS847->$2;
  _M0L6_2atmpS2877 = _M0L2s2S2878 ^ _M0L1tS853;
  _M0L1rS847->$2 = _M0L6_2atmpS2877;
  _M0L2s3S2880 = _M0L1rS847->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2879 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S2880, 45);
  _M0L1rS847->$3 = _M0L6_2atmpS2879;
  return _M0L3resS852;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS844, int32_t _M0L1kS845) {
  uint64_t _M0L6_2atmpS2868;
  int32_t _M0L6_2atmpS2870;
  uint64_t _M0L6_2atmpS2869;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2868 = _M0L1xS844 << (_M0L1kS845 & 63);
  _M0L6_2atmpS2870 = 64 - _M0L1kS845;
  _M0L6_2atmpS2869 = _M0L1xS844 >> (_M0L6_2atmpS2870 & 63);
  return _M0L6_2atmpS2868 | _M0L6_2atmpS2869;
}

int32_t _M0MP26RiantR8snn__mbt7Monitor13count__spikes(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS837
) {
  struct _M0TPB5ArrayGfE* _M0L4dataS2867;
  int32_t _M0L1nS836;
  struct _M0TPB8MutLocalGiE* _M0L5countS838;
  struct _M0TPB8MutLocalGfE* _M0L4prevS839;
  int32_t _M0L7_2abindS840;
  int32_t _M0L1iS841;
  int32_t _result_5853;
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4dataS2867 = _M0L1mS837->$2;
  #line 18 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L1nS836 = _M0MPC15array5Array6lengthGfE(_M0L4dataS2867);
  if (_M0L1nS836 == 0) {
    return 0;
  }
  _M0L5countS838
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5countS838)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5countS838->$0 = 0;
  _M0L4prevS839
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L4prevS839)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4prevS839->$0 = 0x0p+0f;
  _M0L7_2abindS840 = 0;
  _M0L1iS841 = _M0L7_2abindS840;
  while (1) {
    if (_M0L1iS841 < _M0L1nS836) {
      struct _M0TPB5ArrayGfE* _M0L4dataS2865 = _M0L1mS837->$2;
      float _M0L3curS842;
      float _M0L3valS2862;
      int32_t _M0L6_2atmpS2866;
      #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L3curS842 = _M0MPC15array5Array2atGfE(_M0L4dataS2865, _M0L1iS841);
      _M0L3valS2862 = _M0L4prevS839->$0;
      if (_M0L3valS2862 < 0x1p-1f && _M0L3curS842 >= 0x1p-1f) {
        int32_t _M0L3valS2864 = _M0L5countS838->$0;
        int32_t _M0L6_2atmpS2863 = _M0L3valS2864 + 1;
        _M0L5countS838->$0 = _M0L6_2atmpS2863;
      }
      _M0L4prevS839->$0 = _M0L3curS842;
      _M0L6_2atmpS2866 = _M0L1iS841 + 1;
      _M0L1iS841 = _M0L6_2atmpS2866;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4prevS839);
    }
    break;
  }
  _result_5853 = _M0L5countS838->$0;
  moonbit_decref_cycle_free(_M0L5countS838);
  return _result_5853;
}

double _M0FPC14math2ln(double _M0L1xS822) {
  struct _M0TUdiE* _M0L7_2abindS823;
  double _M0L5_2af1S824;
  int32_t _M0L5_2akiS825;
  double _M0L1fS827;
  double _M0L1kS828;
  double _M0L6_2atmpS2855;
  double _M0L1sS829;
  double _M0L2s2S830;
  double _M0L2s4S831;
  double _M0L6_2atmpS2854;
  double _M0L6_2atmpS2853;
  double _M0L6_2atmpS2852;
  double _M0L6_2atmpS2851;
  double _M0L6_2atmpS2850;
  double _M0L6_2atmpS2849;
  double _M0L2t1S832;
  double _M0L6_2atmpS2848;
  double _M0L6_2atmpS2847;
  double _M0L6_2atmpS2846;
  double _M0L6_2atmpS2845;
  double _M0L2t2S833;
  double _M0L1rS834;
  double _M0L6_2atmpS2844;
  double _M0L4hfsqS835;
  double _M0L6_2atmpS2837;
  double _M0L6_2atmpS2843;
  double _M0L6_2atmpS2841;
  double _M0L6_2atmpS2842;
  double _M0L6_2atmpS2840;
  double _M0L6_2atmpS2839;
  double _M0L6_2atmpS2838;
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
    double _M0L6_2atmpS2859 = _M0L5_2af1S824 * 0x1p+1;
    double _M0L6_2atmpS2856 = _M0L6_2atmpS2859 - 0x1p+0;
    int32_t _M0L6_2atmpS2858 = _M0L5_2akiS825 - 1;
    double _M0L6_2atmpS2857 = (double)_M0L6_2atmpS2858;
    _M0L1fS827 = _M0L6_2atmpS2856;
    _M0L1kS828 = _M0L6_2atmpS2857;
    goto join_826;
  } else {
    double _M0L6_2atmpS2860 = _M0L5_2af1S824 - 0x1p+0;
    double _M0L6_2atmpS2861 = (double)_M0L5_2akiS825;
    _M0L1fS827 = _M0L6_2atmpS2860;
    _M0L1kS828 = _M0L6_2atmpS2861;
    goto join_826;
  }
  join_826:;
  _M0L6_2atmpS2855 = 0x1p+1 + _M0L1fS827;
  _M0L1sS829 = _M0L1fS827 / _M0L6_2atmpS2855;
  _M0L2s2S830 = _M0L1sS829 * _M0L1sS829;
  _M0L2s4S831 = _M0L2s2S830 * _M0L2s2S830;
  _M0L6_2atmpS2854 = _M0L2s4S831 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS2853 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS2854;
  _M0L6_2atmpS2852 = _M0L2s4S831 * _M0L6_2atmpS2853;
  _M0L6_2atmpS2851 = 0x1.2492494229359p-2 + _M0L6_2atmpS2852;
  _M0L6_2atmpS2850 = _M0L2s4S831 * _M0L6_2atmpS2851;
  _M0L6_2atmpS2849 = 0x1.5555555555593p-1 + _M0L6_2atmpS2850;
  _M0L2t1S832 = _M0L2s2S830 * _M0L6_2atmpS2849;
  _M0L6_2atmpS2848 = _M0L2s4S831 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS2847 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS2848;
  _M0L6_2atmpS2846 = _M0L2s4S831 * _M0L6_2atmpS2847;
  _M0L6_2atmpS2845 = 0x1.999999997fa04p-2 + _M0L6_2atmpS2846;
  _M0L2t2S833 = _M0L2s4S831 * _M0L6_2atmpS2845;
  _M0L1rS834 = _M0L2t1S832 + _M0L2t2S833;
  _M0L6_2atmpS2844 = 0x1p-1 * _M0L1fS827;
  _M0L4hfsqS835 = _M0L6_2atmpS2844 * _M0L1fS827;
  _M0L6_2atmpS2837 = _M0L1kS828 * 0x1.62e42feep-1;
  _M0L6_2atmpS2843 = _M0L4hfsqS835 + _M0L1rS834;
  _M0L6_2atmpS2841 = _M0L1sS829 * _M0L6_2atmpS2843;
  _M0L6_2atmpS2842 = _M0L1kS828 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS2840 = _M0L6_2atmpS2841 + _M0L6_2atmpS2842;
  _M0L6_2atmpS2839 = _M0L4hfsqS835 - _M0L6_2atmpS2840;
  _M0L6_2atmpS2838 = _M0L6_2atmpS2839 - _M0L1fS827;
  return _M0L6_2atmpS2837 - _M0L6_2atmpS2838;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS815) {
  struct _M0TUdiE* _M0L7_2abindS816;
  double _M0L10_2anorm__fS817;
  int32_t _M0L6_2aexpS818;
  uint64_t _M0L1uS819;
  uint64_t _M0L6_2atmpS2836;
  uint64_t _M0L6_2atmpS2835;
  int32_t _M0L6_2atmpS2834;
  int32_t _M0L6_2atmpS2833;
  int32_t _M0L3expS820;
  uint64_t _M0L6_2atmpS2832;
  uint64_t _M0L6_2atmpS2831;
  uint64_t _M0L6_2atmpS2830;
  double _M0L4fracS821;
  struct _M0TUdiE* _block_5856;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS815 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS815)
    || _M0MPC16double6Double7is__nan(_M0L1fS815)
  ) {
    struct _M0TUdiE* _block_5855 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_5855)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_5855->$0 = _M0L1fS815;
    _block_5855->$1 = 0;
    return _block_5855;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS816 = _M0FPC14math9normalize(_M0L1fS815);
  _M0L10_2anorm__fS817 = _M0L7_2abindS816->$0;
  _M0L6_2aexpS818 = _M0L7_2abindS816->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS816);
  _M0L1uS819 = *(int64_t*)&_M0L10_2anorm__fS817;
  _M0L6_2atmpS2836 = _M0L1uS819 >> 52;
  _M0L6_2atmpS2835 = _M0L6_2atmpS2836 & 2047ull;
  _M0L6_2atmpS2834 = (int32_t)_M0L6_2atmpS2835;
  _M0L6_2atmpS2833 = _M0L6_2aexpS818 + _M0L6_2atmpS2834;
  _M0L3expS820 = _M0L6_2atmpS2833 - 1022;
  _M0L6_2atmpS2832 = ~9218868437227405312ull;
  _M0L6_2atmpS2831 = _M0L1uS819 & _M0L6_2atmpS2832;
  _M0L6_2atmpS2830 = _M0L6_2atmpS2831 | 4602678819172646912ull;
  _M0L4fracS821 = *(double*)&_M0L6_2atmpS2830;
  _block_5856 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_5856)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5856->$0 = _M0L4fracS821;
  _block_5856->$1 = _M0L3expS820;
  return _block_5856;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS814) {
  double _M0L6_2atmpS2827;
  struct _M0TUdiE* _block_5858;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS2827 = fabs(_M0L1fS814);
  if (_M0L6_2atmpS2827 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS2829 = (double)4503599627370496ll;
    double _M0L6_2atmpS2828 = _M0L1fS814 * _M0L6_2atmpS2829;
    struct _M0TUdiE* _block_5857 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_5857)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_5857->$0 = _M0L6_2atmpS2828;
    _block_5857->$1 = -52;
    return _block_5857;
  }
  _block_5858 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_5858)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5858->$0 = _M0L1fS814;
  _block_5858->$1 = 0;
  return _block_5858;
}

int32_t _M0MPC15float5Float7is__nan(float _M0L4selfS813) {
  #line 208 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0L4selfS813 != _M0L4selfS813;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS812) {
  double _M0L6_2atmpS2826;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2826 = (double)_M0L4selfS812;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2826);
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
      float* _M0L3bufS2820 = _M0L3arrS796->$0;
      int32_t _M0L6_2atmpS2821;
      _M0L3bufS2820[_M0L1iS798] = _M0L4elemS799;
      _M0L6_2atmpS2821 = _M0L1iS798 + 1;
      _M0L1iS798 = _M0L6_2atmpS2821;
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
      uint8_t* _M0L3bufS2822 = _M0L3arrS801->$0;
      int32_t _M0L6_2atmpS2823;
      _M0L3bufS2822[_M0L1iS803] = _M0L4elemS804;
      _M0L6_2atmpS2823 = _M0L1iS803 + 1;
      _M0L1iS803 = _M0L6_2atmpS2823;
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
      int32_t* _M0L3bufS2824 = _M0L3arrS806->$0;
      int32_t _M0L6_2atmpS2825;
      _M0L3bufS2824[_M0L1iS808] = _M0L4elemS809;
      _M0L6_2atmpS2825 = _M0L1iS808 + 1;
      _M0L1iS808 = _M0L6_2atmpS2825;
      continue;
    }
    break;
  }
  return _M0L3arrS806;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS785,
  int32_t _M0L5indexS786,
  float _M0L5valueS787
) {
  int32_t _M0L3lenS784;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS784 = _M0L4selfS785->$1;
  if (_M0L5indexS786 >= 0 && _M0L5indexS786 < _M0L3lenS784) {
    float* _M0L6_2atmpS2817;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2817 = _M0MPC15array5Array6bufferGfE(_M0L4selfS785);
    _M0L6_2atmpS2817[_M0L5indexS786] = _M0L5valueS787;
    moonbit_decref_cycle_free(_M0L6_2atmpS2817);
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
    int32_t* _M0L6_2atmpS2818;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2818 = _M0MPC15array5Array6bufferGiE(_M0L4selfS789);
    _M0L6_2atmpS2818[_M0L5indexS790] = _M0L5valueS791;
    moonbit_decref_cycle_free(_M0L6_2atmpS2818);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS793,
  int32_t _M0L5indexS794,
  int32_t _M0L5valueS795
) {
  int32_t _M0L3lenS792;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS792 = _M0L4selfS793->$1;
  if (_M0L5indexS794 >= 0 && _M0L5indexS794 < _M0L3lenS792) {
    uint8_t* _M0L6_2atmpS2819;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2819 = _M0MPC15array5Array6bufferGbE(_M0L4selfS793);
    _M0L6_2atmpS2819[_M0L5indexS794] = _M0L5valueS795;
    moonbit_decref_cycle_free(_M0L6_2atmpS2819);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE* _M0L4selfS777) {
  int32_t _M0L3lenS776;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS776 = _M0L4selfS777->$1;
  if (_M0L3lenS776 == 0) {
    return (struct moonbit_object*)&moonbit_constant_constructor_0 + 1;
  } else {
    int32_t _M0L5indexS778 = _M0L3lenS776 - 1;
    float* _M0L3bufS2815 = _M0L4selfS777->$0;
    float _M0L1vS779 = (float)_M0L3bufS2815[_M0L5indexS778];
    void* _block_5862;
    _M0L4selfS777->$1 = _M0L5indexS778;
    _block_5862
    = (void*)moonbit_malloc(sizeof(struct _M0DTPC16option6OptionGfE4Some));
    Moonbit_object_header(_block_5862)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 1);
    ((struct _M0DTPC16option6OptionGfE4Some*)_block_5862)->$0 = _M0L1vS779;
    return _block_5862;
  }
}

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE* _M0L4selfS781) {
  int32_t _M0L3lenS780;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS780 = _M0L4selfS781->$1;
  if (_M0L3lenS780 == 0) {
    return 4294967296ll;
  } else {
    int32_t _M0L5indexS782 = _M0L3lenS780 - 1;
    int32_t* _M0L3bufS2816 = _M0L4selfS781->$0;
    int32_t _M0L1vS783 = (int32_t)_M0L3bufS2816[_M0L5indexS782];
    _M0L4selfS781->$1 = _M0L5indexS782;
    return (int64_t)_M0L1vS783;
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS762,
  int32_t _M0L5indexS763
) {
  int32_t _M0L3lenS761;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS761 = _M0L4selfS762->$1;
  if (_M0L5indexS763 >= 0 && _M0L5indexS763 < _M0L3lenS761) {
    float* _M0L6_2atmpS2810;
    float _result_5863;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2810 = _M0MPC15array5Array6bufferGfE(_M0L4selfS762);
    _result_5863 = (float)_M0L6_2atmpS2810[_M0L5indexS763];
    moonbit_decref_cycle_free(_M0L6_2atmpS2810);
    return _result_5863;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS765,
  int32_t _M0L5indexS766
) {
  int32_t _M0L3lenS764;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS764 = _M0L4selfS765->$1;
  if (_M0L5indexS766 >= 0 && _M0L5indexS766 < _M0L3lenS764) {
    moonbit_string_t* _M0L6_2atmpS2811;
    moonbit_string_t _M0L6_2atmpS5462;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2811 = _M0MPC15array5Array6bufferGsE(_M0L4selfS765);
    _M0L6_2atmpS5462 = (moonbit_string_t)_M0L6_2atmpS2811[_M0L5indexS766];
    moonbit_incref_cycle_free(_M0L6_2atmpS5462);
    moonbit_decref_cycle_free(_M0L6_2atmpS2811);
    return _M0L6_2atmpS5462;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L4selfS768,
  int32_t _M0L5indexS769
) {
  int32_t _M0L3lenS767;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS767 = _M0L4selfS768->$1;
  if (_M0L5indexS769 >= 0 && _M0L5indexS769 < _M0L3lenS767) {
    struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L6_2atmpS2812;
    struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L6_2atmpS5463;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2812
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L4selfS768);
    _M0L6_2atmpS5463
    = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L6_2atmpS2812[
        _M0L5indexS769
      ];
    if (_M0L6_2atmpS5463) {
      moonbit_incref_cycle_free(_M0L6_2atmpS5463);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2812);
    return _M0L6_2atmpS5463;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS771,
  int32_t _M0L5indexS772
) {
  int32_t _M0L3lenS770;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS770 = _M0L4selfS771->$1;
  if (_M0L5indexS772 >= 0 && _M0L5indexS772 < _M0L3lenS770) {
    int32_t* _M0L6_2atmpS2813;
    int32_t _result_5864;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2813 = _M0MPC15array5Array6bufferGiE(_M0L4selfS771);
    _result_5864 = (int32_t)_M0L6_2atmpS2813[_M0L5indexS772];
    moonbit_decref_cycle_free(_M0L6_2atmpS2813);
    return _result_5864;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS774,
  int32_t _M0L5indexS775
) {
  int32_t _M0L3lenS773;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS773 = _M0L4selfS774->$1;
  if (_M0L5indexS775 >= 0 && _M0L5indexS775 < _M0L3lenS773) {
    uint8_t* _M0L6_2atmpS2814;
    int32_t _result_5865;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2814 = _M0MPC15array5Array6bufferGbE(_M0L4selfS774);
    _result_5865 = (int32_t)_M0L6_2atmpS2814[_M0L5indexS775];
    moonbit_decref_cycle_free(_M0L6_2atmpS2814);
    return _result_5865;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS760) {
  moonbit_string_t _M0L6_2atmpS2809;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2809 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS760);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2809);
  moonbit_decref_cycle_free(_M0L6_2atmpS2809);
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
  uint64_t _M0L6_2atmpS2808;
  uint64_t _M0L6_2atmpS2807;
  int32_t _M0L8ieeeSignS746;
  uint64_t _M0L12ieeeMantissaS747;
  uint64_t _M0L6_2atmpS2806;
  uint64_t _M0L6_2atmpS2805;
  int32_t _M0L12ieeeExponentS748;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS749;
  struct _M0TPB17FloatingDecimal64* _M0L1vS750;
  moonbit_string_t _result_5867;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS742 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_12.data;
  }
  if (_M0L3valS742 >= -0x1p+53 && _M0L3valS742 <= 0x1p+53) {
    if (_M0L3valS742 >= -0x1p+31 && _M0L3valS742 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS743;
      double _M0L6_2atmpS2794;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS743 = _M0MPC16double6Double7to__int(_M0L3valS742);
      _M0L6_2atmpS2794 = (double)_M0L1iS743;
      if (_M0L6_2atmpS2794 == _M0L3valS742) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS743, 10);
      }
    } else {
      int64_t _M0L1iS744;
      double _M0L6_2atmpS2795;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS744 = _M0MPC16double6Double9to__int64(_M0L3valS742);
      _M0L6_2atmpS2795 = (double)_M0L1iS744;
      if (_M0L6_2atmpS2795 == _M0L3valS742) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS744, 10);
      }
    }
  }
  _M0L4bitsS745 = *(int64_t*)&_M0L3valS742;
  _M0L6_2atmpS2808 = _M0L4bitsS745 >> 63;
  _M0L6_2atmpS2807 = _M0L6_2atmpS2808 & 1ull;
  _M0L8ieeeSignS746 = _M0L6_2atmpS2807 != 0ull;
  _M0L12ieeeMantissaS747 = _M0L4bitsS745 & 4503599627370495ull;
  _M0L6_2atmpS2806 = _M0L4bitsS745 >> 52;
  _M0L6_2atmpS2805 = _M0L6_2atmpS2806 & 2047ull;
  _M0L12ieeeExponentS748 = (int32_t)_M0L6_2atmpS2805;
  if (
    _M0L12ieeeExponentS748 == 2047
    || _M0L12ieeeExponentS748 == 0 && _M0L12ieeeMantissaS747 == 0ull
  ) {
    int32_t _M0L6_2atmpS2796 = _M0L12ieeeExponentS748 != 0;
    int32_t _M0L6_2atmpS2797 = _M0L12ieeeMantissaS747 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS746, _M0L6_2atmpS2796, _M0L6_2atmpS2797);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS749
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS747, _M0L12ieeeExponentS748);
  if (_M0L7_2abindS749 == 0) {
    uint32_t _M0L6_2atmpS2798;
    if (_M0L7_2abindS749) {
      moonbit_decref_cycle_free(_M0L7_2abindS749);
    }
    _M0L6_2atmpS2798 = *(uint32_t*)&_M0L12ieeeExponentS748;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS750 = _M0FPB3d2d(_M0L12ieeeMantissaS747, _M0L6_2atmpS2798);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS751 = _M0L7_2abindS749;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS752 = _M0L7_2aSomeS751;
    struct _M0TPB17FloatingDecimal64* _M0L1xS753 = _M0L4_2afS752;
    while (1) {
      uint64_t _M0L8mantissaS2804 = _M0L1xS753->$0;
      uint64_t _M0L1qS754 = _M0L8mantissaS2804 / 10ull;
      uint64_t _M0L8mantissaS2802 = _M0L1xS753->$0;
      uint64_t _M0L6_2atmpS2803 = 10ull * _M0L1qS754;
      uint64_t _M0L1rS755 = _M0L8mantissaS2802 - _M0L6_2atmpS2803;
      int32_t _M0L8exponentS2801;
      int32_t _M0L6_2atmpS2800;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2799;
      if (_M0L1rS755 != 0ull) {
        _M0L1vS750 = _M0L1xS753;
        break;
      }
      _M0L8exponentS2801 = _M0L1xS753->$1;
      moonbit_decref_cycle_free(_M0L1xS753);
      _M0L6_2atmpS2800 = _M0L8exponentS2801 + 1;
      _M0L6_2atmpS2799
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS2799)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS2799->$0 = _M0L1qS754;
      _M0L6_2atmpS2799->$1 = _M0L6_2atmpS2800;
      _M0L1xS753 = _M0L6_2atmpS2799;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_5867 = _M0FPB9to__chars(_M0L1vS750, _M0L8ieeeSignS746);
  moonbit_decref_cycle_free(_M0L1vS750);
  return _result_5867;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS737,
  int32_t _M0L12ieeeExponentS739
) {
  uint64_t _M0L2m2S736;
  int32_t _M0L6_2atmpS2793;
  int32_t _M0L2e2S738;
  int32_t _M0L6_2atmpS2792;
  uint64_t _M0L6_2atmpS2791;
  uint64_t _M0L4maskS740;
  uint64_t _M0L8fractionS741;
  int32_t _M0L6_2atmpS2790;
  uint64_t _M0L6_2atmpS2789;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2788;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S736 = 4503599627370496ull | _M0L12ieeeMantissaS737;
  _M0L6_2atmpS2793 = _M0L12ieeeExponentS739 - 1023;
  _M0L2e2S738 = _M0L6_2atmpS2793 - 52;
  if (_M0L2e2S738 > 0) {
    return 0;
  }
  if (_M0L2e2S738 < -52) {
    return 0;
  }
  _M0L6_2atmpS2792 = -_M0L2e2S738;
  _M0L6_2atmpS2791 = 1ull << (_M0L6_2atmpS2792 & 63);
  _M0L4maskS740 = _M0L6_2atmpS2791 - 1ull;
  _M0L8fractionS741 = _M0L2m2S736 & _M0L4maskS740;
  if (_M0L8fractionS741 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2790 = -_M0L2e2S738;
  _M0L6_2atmpS2789 = _M0L2m2S736 >> (_M0L6_2atmpS2790 & 63);
  _M0L6_2atmpS2788
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS2788)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS2788->$0 = _M0L6_2atmpS2789;
  _M0L6_2atmpS2788->$1 = 0;
  return _M0L6_2atmpS2788;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS704,
  int32_t _M0L4signS702
) {
  moonbit_bytes_t _M0L6resultS700;
  int32_t _M0Lm5indexS701;
  uint64_t _M0L6outputS703;
  int32_t _M0L7olengthS705;
  int32_t _M0L8exponentS2787;
  int32_t _M0L6_2atmpS2786;
  int32_t _M0Lm3expS706;
  int32_t _M0L6_2atmpS2785;
  int32_t _M0L6_2atmpS2783;
  int32_t _M0L18scientificNotationS707;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS700 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS701 = 0;
  if (_M0L4signS702) {
    int32_t _M0L6_2atmpS2657 = _M0Lm5indexS701;
    int32_t _M0L6_2atmpS2658;
    if (
      _M0L6_2atmpS2657 < 0
      || _M0L6_2atmpS2657 >= Moonbit_array_length(_M0L6resultS700)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS700[_M0L6_2atmpS2657] = 45;
    _M0L6_2atmpS2658 = _M0Lm5indexS701;
    _M0Lm5indexS701 = _M0L6_2atmpS2658 + 1;
  }
  _M0L6outputS703 = _M0L1vS704->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS705 = _M0FPB17decimal__length17(_M0L6outputS703);
  _M0L8exponentS2787 = _M0L1vS704->$1;
  _M0L6_2atmpS2786 = _M0L8exponentS2787 + _M0L7olengthS705;
  _M0Lm3expS706 = _M0L6_2atmpS2786 - 1;
  _M0L6_2atmpS2785 = _M0Lm3expS706;
  if (_M0L6_2atmpS2785 >= -6) {
    int32_t _M0L6_2atmpS2784 = _M0Lm3expS706;
    _M0L6_2atmpS2783 = _M0L6_2atmpS2784 < 21;
  } else {
    _M0L6_2atmpS2783 = 0;
  }
  _M0L18scientificNotationS707 = !_M0L6_2atmpS2783;
  if (_M0L18scientificNotationS707) {
    int32_t _M0L7_2abindS708 = _M0L7olengthS705 - 1;
    uint64_t _M0L6outputS709;
    int32_t _M0L1iS710 = 0;
    uint64_t _M0L6outputS711 = _M0L6outputS703;
    int32_t _M0L6_2atmpS2659;
    int32_t _M0L6_2atmpS2663;
    int32_t _M0L6_2atmpS2662;
    int32_t _M0L6_2atmpS2661;
    int32_t _M0L6_2atmpS2660;
    int32_t _M0L6_2atmpS2667;
    int32_t _M0L6_2atmpS2668;
    int32_t _M0L6_2atmpS2669;
    int32_t _M0L6_2atmpS2670;
    int32_t _M0L6_2atmpS2671;
    int32_t _M0L6_2atmpS2677;
    int32_t _M0L6_2atmpS2710;
    moonbit_string_t _result_5869;
    while (1) {
      if (_M0L1iS710 < _M0L7_2abindS708) {
        uint64_t _M0L1cS712 = _M0L6outputS711 % 10ull;
        int32_t _M0L6_2atmpS2716 = _M0Lm5indexS701;
        int32_t _M0L6_2atmpS2715 = _M0L6_2atmpS2716 + _M0L7olengthS705;
        int32_t _M0L6_2atmpS2711 = _M0L6_2atmpS2715 - _M0L1iS710;
        int32_t _M0L6_2atmpS2714 = (int32_t)_M0L1cS712;
        int32_t _M0L6_2atmpS2713 = 48 + _M0L6_2atmpS2714;
        int32_t _M0L6_2atmpS2712 = _M0L6_2atmpS2713 & 0xff;
        int32_t _M0L6_2atmpS2717;
        uint64_t _M0L6_2atmpS2718;
        if (
          _M0L6_2atmpS2711 < 0
          || _M0L6_2atmpS2711 >= Moonbit_array_length(_M0L6resultS700)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS700[_M0L6_2atmpS2711] = _M0L6_2atmpS2712;
        _M0L6_2atmpS2717 = _M0L1iS710 + 1;
        _M0L6_2atmpS2718 = _M0L6outputS711 / 10ull;
        _M0L1iS710 = _M0L6_2atmpS2717;
        _M0L6outputS711 = _M0L6_2atmpS2718;
        continue;
      } else {
        _M0L6outputS709 = _M0L6outputS711;
      }
      break;
    }
    _M0L6_2atmpS2659 = _M0Lm5indexS701;
    _M0L6_2atmpS2663 = (int32_t)_M0L6outputS709;
    _M0L6_2atmpS2662 = _M0L6_2atmpS2663 % 10;
    _M0L6_2atmpS2661 = 48 + _M0L6_2atmpS2662;
    _M0L6_2atmpS2660 = _M0L6_2atmpS2661 & 0xff;
    if (
      _M0L6_2atmpS2659 < 0
      || _M0L6_2atmpS2659 >= Moonbit_array_length(_M0L6resultS700)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS700[_M0L6_2atmpS2659] = _M0L6_2atmpS2660;
    if (_M0L7olengthS705 > 1) {
      int32_t _M0L6_2atmpS2665 = _M0Lm5indexS701;
      int32_t _M0L6_2atmpS2664 = _M0L6_2atmpS2665 + 1;
      if (
        _M0L6_2atmpS2664 < 0
        || _M0L6_2atmpS2664 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2664] = 46;
    } else {
      int32_t _M0L6_2atmpS2666 = _M0Lm5indexS701;
      _M0Lm5indexS701 = _M0L6_2atmpS2666 - 1;
    }
    _M0L6_2atmpS2667 = _M0Lm5indexS701;
    _M0L6_2atmpS2668 = _M0L7olengthS705 + 1;
    _M0Lm5indexS701 = _M0L6_2atmpS2667 + _M0L6_2atmpS2668;
    _M0L6_2atmpS2669 = _M0Lm5indexS701;
    if (
      _M0L6_2atmpS2669 < 0
      || _M0L6_2atmpS2669 >= Moonbit_array_length(_M0L6resultS700)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS700[_M0L6_2atmpS2669] = 101;
    _M0L6_2atmpS2670 = _M0Lm5indexS701;
    _M0Lm5indexS701 = _M0L6_2atmpS2670 + 1;
    _M0L6_2atmpS2671 = _M0Lm3expS706;
    if (_M0L6_2atmpS2671 < 0) {
      int32_t _M0L6_2atmpS2672 = _M0Lm5indexS701;
      int32_t _M0L6_2atmpS2673;
      int32_t _M0L6_2atmpS2674;
      if (
        _M0L6_2atmpS2672 < 0
        || _M0L6_2atmpS2672 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2672] = 45;
      _M0L6_2atmpS2673 = _M0Lm5indexS701;
      _M0Lm5indexS701 = _M0L6_2atmpS2673 + 1;
      _M0L6_2atmpS2674 = _M0Lm3expS706;
      _M0Lm3expS706 = -_M0L6_2atmpS2674;
    } else {
      int32_t _M0L6_2atmpS2675 = _M0Lm5indexS701;
      int32_t _M0L6_2atmpS2676;
      if (
        _M0L6_2atmpS2675 < 0
        || _M0L6_2atmpS2675 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2675] = 43;
      _M0L6_2atmpS2676 = _M0Lm5indexS701;
      _M0Lm5indexS701 = _M0L6_2atmpS2676 + 1;
    }
    _M0L6_2atmpS2677 = _M0Lm3expS706;
    if (_M0L6_2atmpS2677 >= 100) {
      int32_t _M0L6_2atmpS2693 = _M0Lm3expS706;
      int32_t _M0L1aS714 = _M0L6_2atmpS2693 / 100;
      int32_t _M0L6_2atmpS2692 = _M0Lm3expS706;
      int32_t _M0L6_2atmpS2691 = _M0L6_2atmpS2692 / 10;
      int32_t _M0L1bS715 = _M0L6_2atmpS2691 % 10;
      int32_t _M0L6_2atmpS2690 = _M0Lm3expS706;
      int32_t _M0L1cS716 = _M0L6_2atmpS2690 % 10;
      int32_t _M0L6_2atmpS2678 = _M0Lm5indexS701;
      int32_t _M0L6_2atmpS2680 = 48 + _M0L1aS714;
      int32_t _M0L6_2atmpS2679 = _M0L6_2atmpS2680 & 0xff;
      int32_t _M0L6_2atmpS2684;
      int32_t _M0L6_2atmpS2681;
      int32_t _M0L6_2atmpS2683;
      int32_t _M0L6_2atmpS2682;
      int32_t _M0L6_2atmpS2688;
      int32_t _M0L6_2atmpS2685;
      int32_t _M0L6_2atmpS2687;
      int32_t _M0L6_2atmpS2686;
      int32_t _M0L6_2atmpS2689;
      if (
        _M0L6_2atmpS2678 < 0
        || _M0L6_2atmpS2678 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2678] = _M0L6_2atmpS2679;
      _M0L6_2atmpS2684 = _M0Lm5indexS701;
      _M0L6_2atmpS2681 = _M0L6_2atmpS2684 + 1;
      _M0L6_2atmpS2683 = 48 + _M0L1bS715;
      _M0L6_2atmpS2682 = _M0L6_2atmpS2683 & 0xff;
      if (
        _M0L6_2atmpS2681 < 0
        || _M0L6_2atmpS2681 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2681] = _M0L6_2atmpS2682;
      _M0L6_2atmpS2688 = _M0Lm5indexS701;
      _M0L6_2atmpS2685 = _M0L6_2atmpS2688 + 2;
      _M0L6_2atmpS2687 = 48 + _M0L1cS716;
      _M0L6_2atmpS2686 = _M0L6_2atmpS2687 & 0xff;
      if (
        _M0L6_2atmpS2685 < 0
        || _M0L6_2atmpS2685 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2685] = _M0L6_2atmpS2686;
      _M0L6_2atmpS2689 = _M0Lm5indexS701;
      _M0Lm5indexS701 = _M0L6_2atmpS2689 + 3;
    } else {
      int32_t _M0L6_2atmpS2694 = _M0Lm3expS706;
      if (_M0L6_2atmpS2694 >= 10) {
        int32_t _M0L6_2atmpS2704 = _M0Lm3expS706;
        int32_t _M0L1aS717 = _M0L6_2atmpS2704 / 10;
        int32_t _M0L6_2atmpS2703 = _M0Lm3expS706;
        int32_t _M0L1bS718 = _M0L6_2atmpS2703 % 10;
        int32_t _M0L6_2atmpS2695 = _M0Lm5indexS701;
        int32_t _M0L6_2atmpS2697 = 48 + _M0L1aS717;
        int32_t _M0L6_2atmpS2696 = _M0L6_2atmpS2697 & 0xff;
        int32_t _M0L6_2atmpS2701;
        int32_t _M0L6_2atmpS2698;
        int32_t _M0L6_2atmpS2700;
        int32_t _M0L6_2atmpS2699;
        int32_t _M0L6_2atmpS2702;
        if (
          _M0L6_2atmpS2695 < 0
          || _M0L6_2atmpS2695 >= Moonbit_array_length(_M0L6resultS700)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS700[_M0L6_2atmpS2695] = _M0L6_2atmpS2696;
        _M0L6_2atmpS2701 = _M0Lm5indexS701;
        _M0L6_2atmpS2698 = _M0L6_2atmpS2701 + 1;
        _M0L6_2atmpS2700 = 48 + _M0L1bS718;
        _M0L6_2atmpS2699 = _M0L6_2atmpS2700 & 0xff;
        if (
          _M0L6_2atmpS2698 < 0
          || _M0L6_2atmpS2698 >= Moonbit_array_length(_M0L6resultS700)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS700[_M0L6_2atmpS2698] = _M0L6_2atmpS2699;
        _M0L6_2atmpS2702 = _M0Lm5indexS701;
        _M0Lm5indexS701 = _M0L6_2atmpS2702 + 2;
      } else {
        int32_t _M0L6_2atmpS2705 = _M0Lm5indexS701;
        int32_t _M0L6_2atmpS2708 = _M0Lm3expS706;
        int32_t _M0L6_2atmpS2707 = 48 + _M0L6_2atmpS2708;
        int32_t _M0L6_2atmpS2706 = _M0L6_2atmpS2707 & 0xff;
        int32_t _M0L6_2atmpS2709;
        if (
          _M0L6_2atmpS2705 < 0
          || _M0L6_2atmpS2705 >= Moonbit_array_length(_M0L6resultS700)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS700[_M0L6_2atmpS2705] = _M0L6_2atmpS2706;
        _M0L6_2atmpS2709 = _M0Lm5indexS701;
        _M0Lm5indexS701 = _M0L6_2atmpS2709 + 1;
      }
    }
    _M0L6_2atmpS2710 = _M0Lm5indexS701;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_5869
    = _M0FPB19string__from__bytes(_M0L6resultS700, 0, _M0L6_2atmpS2710);
    moonbit_decref_cycle_free(_M0L6resultS700);
    return _result_5869;
  } else {
    int32_t _M0L6_2atmpS2719 = _M0Lm3expS706;
    int32_t _M0L6_2atmpS2782;
    moonbit_string_t _result_5875;
    if (_M0L6_2atmpS2719 < 0) {
      int32_t _M0L6_2atmpS2720 = _M0Lm5indexS701;
      int32_t _M0L6_2atmpS2722;
      int32_t _M0L6_2atmpS2721;
      int32_t _M0L6_2atmpS2723;
      int32_t _M0L1iS719;
      int32_t _M0L6_2atmpS2738;
      int32_t _M0L6_2atmpS2740;
      int32_t _M0L6_2atmpS2739;
      int32_t _M0L7currentS721;
      int32_t _M0L1iS722;
      uint64_t _M0L6outputS723;
      if (
        _M0L6_2atmpS2720 < 0
        || _M0L6_2atmpS2720 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2720] = 48;
      _M0L6_2atmpS2722 = _M0Lm5indexS701;
      _M0L6_2atmpS2721 = _M0L6_2atmpS2722 + 1;
      if (
        _M0L6_2atmpS2721 < 0
        || _M0L6_2atmpS2721 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2721] = 46;
      _M0L6_2atmpS2723 = _M0Lm5indexS701;
      _M0Lm5indexS701 = _M0L6_2atmpS2723 + 2;
      _M0L1iS719 = -1;
      while (1) {
        int32_t _M0L6_2atmpS2724 = _M0Lm3expS706;
        if (_M0L1iS719 > _M0L6_2atmpS2724) {
          int32_t _M0L6_2atmpS2727 = _M0Lm5indexS701;
          int32_t _M0L6_2atmpS2726 = _M0L6_2atmpS2727 - _M0L1iS719;
          int32_t _M0L6_2atmpS2725 = _M0L6_2atmpS2726 - 1;
          int32_t _M0L6_2atmpS2728;
          if (
            _M0L6_2atmpS2725 < 0
            || _M0L6_2atmpS2725 >= Moonbit_array_length(_M0L6resultS700)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS700[_M0L6_2atmpS2725] = 48;
          _M0L6_2atmpS2728 = _M0L1iS719 - 1;
          _M0L1iS719 = _M0L6_2atmpS2728;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2738 = _M0Lm5indexS701;
      _M0L6_2atmpS2740 = _M0Lm3expS706;
      _M0L6_2atmpS2739 = -1 - _M0L6_2atmpS2740;
      _M0L7currentS721 = _M0L6_2atmpS2738 + _M0L6_2atmpS2739;
      _M0L1iS722 = 0;
      _M0L6outputS723 = _M0L6outputS703;
      while (1) {
        if (_M0L1iS722 < _M0L7olengthS705) {
          int32_t _M0L6_2atmpS2735 = _M0L7currentS721 + _M0L7olengthS705;
          int32_t _M0L6_2atmpS2734 = _M0L6_2atmpS2735 - _M0L1iS722;
          int32_t _M0L6_2atmpS2729 = _M0L6_2atmpS2734 - 1;
          uint64_t _M0L6_2atmpS2733 = _M0L6outputS723 % 10ull;
          int32_t _M0L6_2atmpS2732 = (int32_t)_M0L6_2atmpS2733;
          int32_t _M0L6_2atmpS2731 = 48 + _M0L6_2atmpS2732;
          int32_t _M0L6_2atmpS2730 = _M0L6_2atmpS2731 & 0xff;
          int32_t _M0L6_2atmpS2736;
          uint64_t _M0L6_2atmpS2737;
          if (
            _M0L6_2atmpS2729 < 0
            || _M0L6_2atmpS2729 >= Moonbit_array_length(_M0L6resultS700)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS700[_M0L6_2atmpS2729] = _M0L6_2atmpS2730;
          _M0L6_2atmpS2736 = _M0L1iS722 + 1;
          _M0L6_2atmpS2737 = _M0L6outputS723 / 10ull;
          _M0L1iS722 = _M0L6_2atmpS2736;
          _M0L6outputS723 = _M0L6_2atmpS2737;
          continue;
        }
        break;
      }
      _M0Lm5indexS701 = _M0L7currentS721 + _M0L7olengthS705;
    } else {
      int32_t _M0L6_2atmpS2742 = _M0Lm3expS706;
      int32_t _M0L6_2atmpS2741 = _M0L6_2atmpS2742 + 1;
      if (_M0L6_2atmpS2741 >= _M0L7olengthS705) {
        int32_t _M0L1iS725 = 0;
        uint64_t _M0L6outputS726 = _M0L6outputS703;
        int32_t _M0L6_2atmpS2753;
        int32_t _M0L6_2atmpS2758;
        int32_t _M0L7_2abindS728;
        int32_t _M0L1iS729;
        int32_t _M0L6_2atmpS2759;
        int32_t _M0L6_2atmpS2762;
        int32_t _M0L6_2atmpS2761;
        int32_t _M0L6_2atmpS2760;
        while (1) {
          if (_M0L1iS725 < _M0L7olengthS705) {
            int32_t _M0L6_2atmpS2750 = _M0Lm5indexS701;
            int32_t _M0L6_2atmpS2749 = _M0L6_2atmpS2750 + _M0L7olengthS705;
            int32_t _M0L6_2atmpS2748 = _M0L6_2atmpS2749 - _M0L1iS725;
            int32_t _M0L6_2atmpS2743 = _M0L6_2atmpS2748 - 1;
            uint64_t _M0L6_2atmpS2747 = _M0L6outputS726 % 10ull;
            int32_t _M0L6_2atmpS2746 = (int32_t)_M0L6_2atmpS2747;
            int32_t _M0L6_2atmpS2745 = 48 + _M0L6_2atmpS2746;
            int32_t _M0L6_2atmpS2744 = _M0L6_2atmpS2745 & 0xff;
            int32_t _M0L6_2atmpS2751;
            uint64_t _M0L6_2atmpS2752;
            if (
              _M0L6_2atmpS2743 < 0
              || _M0L6_2atmpS2743 >= Moonbit_array_length(_M0L6resultS700)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS700[_M0L6_2atmpS2743] = _M0L6_2atmpS2744;
            _M0L6_2atmpS2751 = _M0L1iS725 + 1;
            _M0L6_2atmpS2752 = _M0L6outputS726 / 10ull;
            _M0L1iS725 = _M0L6_2atmpS2751;
            _M0L6outputS726 = _M0L6_2atmpS2752;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2753 = _M0Lm5indexS701;
        _M0Lm5indexS701 = _M0L6_2atmpS2753 + _M0L7olengthS705;
        _M0L6_2atmpS2758 = _M0Lm3expS706;
        _M0L7_2abindS728 = _M0L6_2atmpS2758 + 1;
        _M0L1iS729 = _M0L7olengthS705;
        while (1) {
          if (_M0L1iS729 < _M0L7_2abindS728) {
            int32_t _M0L6_2atmpS2756 = _M0Lm5indexS701;
            int32_t _M0L6_2atmpS2755 = _M0L6_2atmpS2756 + _M0L1iS729;
            int32_t _M0L6_2atmpS2754 = _M0L6_2atmpS2755 - _M0L7olengthS705;
            int32_t _M0L6_2atmpS2757;
            if (
              _M0L6_2atmpS2754 < 0
              || _M0L6_2atmpS2754 >= Moonbit_array_length(_M0L6resultS700)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS700[_M0L6_2atmpS2754] = 48;
            _M0L6_2atmpS2757 = _M0L1iS729 + 1;
            _M0L1iS729 = _M0L6_2atmpS2757;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2759 = _M0Lm5indexS701;
        _M0L6_2atmpS2762 = _M0Lm3expS706;
        _M0L6_2atmpS2761 = _M0L6_2atmpS2762 + 1;
        _M0L6_2atmpS2760 = _M0L6_2atmpS2761 - _M0L7olengthS705;
        _M0Lm5indexS701 = _M0L6_2atmpS2759 + _M0L6_2atmpS2760;
      } else {
        int32_t _M0L6_2atmpS2779 = _M0Lm5indexS701;
        int32_t _M0L6_2atmpS2778 = _M0L6_2atmpS2779 + 1;
        int32_t _M0L1iS731 = 0;
        int32_t _M0L7currentS732 = _M0L6_2atmpS2778;
        uint64_t _M0L6outputS733 = _M0L6outputS703;
        int32_t _M0L6_2atmpS2780;
        int32_t _M0L6_2atmpS2781;
        while (1) {
          if (_M0L1iS731 < _M0L7olengthS705) {
            int32_t _M0L6_2atmpS2774 = _M0L7olengthS705 - _M0L1iS731;
            int32_t _M0L6_2atmpS2772 = _M0L6_2atmpS2774 - 1;
            int32_t _M0L6_2atmpS2773 = _M0Lm3expS706;
            int32_t _M0L7currentS734;
            int32_t _M0L6_2atmpS2769;
            int32_t _M0L6_2atmpS2768;
            int32_t _M0L6_2atmpS2763;
            uint64_t _M0L6_2atmpS2767;
            int32_t _M0L6_2atmpS2766;
            int32_t _M0L6_2atmpS2765;
            int32_t _M0L6_2atmpS2764;
            int32_t _M0L6_2atmpS2770;
            uint64_t _M0L6_2atmpS2771;
            if (_M0L6_2atmpS2772 == _M0L6_2atmpS2773) {
              int32_t _M0L6_2atmpS2777 = _M0L7currentS732 + _M0L7olengthS705;
              int32_t _M0L6_2atmpS2776 = _M0L6_2atmpS2777 - _M0L1iS731;
              int32_t _M0L6_2atmpS2775 = _M0L6_2atmpS2776 - 1;
              if (
                _M0L6_2atmpS2775 < 0
                || _M0L6_2atmpS2775 >= Moonbit_array_length(_M0L6resultS700)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS700[_M0L6_2atmpS2775] = 46;
              _M0L7currentS734 = _M0L7currentS732 - 1;
            } else {
              _M0L7currentS734 = _M0L7currentS732;
            }
            _M0L6_2atmpS2769 = _M0L7currentS734 + _M0L7olengthS705;
            _M0L6_2atmpS2768 = _M0L6_2atmpS2769 - _M0L1iS731;
            _M0L6_2atmpS2763 = _M0L6_2atmpS2768 - 1;
            _M0L6_2atmpS2767 = _M0L6outputS733 % 10ull;
            _M0L6_2atmpS2766 = (int32_t)_M0L6_2atmpS2767;
            _M0L6_2atmpS2765 = 48 + _M0L6_2atmpS2766;
            _M0L6_2atmpS2764 = _M0L6_2atmpS2765 & 0xff;
            if (
              _M0L6_2atmpS2763 < 0
              || _M0L6_2atmpS2763 >= Moonbit_array_length(_M0L6resultS700)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS700[_M0L6_2atmpS2763] = _M0L6_2atmpS2764;
            _M0L6_2atmpS2770 = _M0L1iS731 + 1;
            _M0L6_2atmpS2771 = _M0L6outputS733 / 10ull;
            _M0L1iS731 = _M0L6_2atmpS2770;
            _M0L7currentS732 = _M0L7currentS734;
            _M0L6outputS733 = _M0L6_2atmpS2771;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2780 = _M0Lm5indexS701;
        _M0L6_2atmpS2781 = _M0L7olengthS705 + 1;
        _M0Lm5indexS701 = _M0L6_2atmpS2780 + _M0L6_2atmpS2781;
      }
    }
    _M0L6_2atmpS2782 = _M0Lm5indexS701;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_5875
    = _M0FPB19string__from__bytes(_M0L6resultS700, 0, _M0L6_2atmpS2782);
    moonbit_decref_cycle_free(_M0L6resultS700);
    return _result_5875;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS646,
  uint32_t _M0L12ieeeExponentS645
) {
  int32_t _M0Lm2e2S643;
  uint64_t _M0Lm2m2S644;
  uint64_t _M0L6_2atmpS2656;
  uint64_t _M0L6_2atmpS2655;
  int32_t _M0L4evenS647;
  uint64_t _M0L6_2atmpS2654;
  uint64_t _M0L2mvS648;
  int32_t _M0L7mmShiftS649;
  uint64_t _M0Lm2vrS650;
  uint64_t _M0Lm2vpS651;
  uint64_t _M0Lm2vmS652;
  int32_t _M0Lm3e10S653;
  int32_t _M0Lm17vmIsTrailingZerosS654;
  int32_t _M0Lm17vrIsTrailingZerosS655;
  int32_t _M0L6_2atmpS2556;
  int32_t _M0Lm7removedS674;
  int32_t _M0Lm16lastRemovedDigitS675;
  uint64_t _M0Lm6outputS676;
  int32_t _M0L6_2atmpS2652;
  int32_t _M0L6_2atmpS2653;
  int32_t _M0L3expS699;
  uint64_t _M0L6_2atmpS2651;
  struct _M0TPB17FloatingDecimal64* _block_5881;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S643 = 0;
  _M0Lm2m2S644 = 0ull;
  if (_M0L12ieeeExponentS645 == 0u) {
    _M0Lm2e2S643 = -1076;
    _M0Lm2m2S644 = _M0L12ieeeMantissaS646;
  } else {
    int32_t _M0L6_2atmpS2555 = *(int32_t*)&_M0L12ieeeExponentS645;
    int32_t _M0L6_2atmpS2554 = _M0L6_2atmpS2555 - 1023;
    int32_t _M0L6_2atmpS2553 = _M0L6_2atmpS2554 - 52;
    _M0Lm2e2S643 = _M0L6_2atmpS2553 - 2;
    _M0Lm2m2S644 = 4503599627370496ull | _M0L12ieeeMantissaS646;
  }
  _M0L6_2atmpS2656 = _M0Lm2m2S644;
  _M0L6_2atmpS2655 = _M0L6_2atmpS2656 & 1ull;
  _M0L4evenS647 = _M0L6_2atmpS2655 == 0ull;
  _M0L6_2atmpS2654 = _M0Lm2m2S644;
  _M0L2mvS648 = 4ull * _M0L6_2atmpS2654;
  _M0L7mmShiftS649
  = _M0L12ieeeMantissaS646 != 0ull || _M0L12ieeeExponentS645 <= 1u;
  _M0Lm2vrS650 = 0ull;
  _M0Lm2vpS651 = 0ull;
  _M0Lm2vmS652 = 0ull;
  _M0Lm3e10S653 = 0;
  _M0Lm17vmIsTrailingZerosS654 = 0;
  _M0Lm17vrIsTrailingZerosS655 = 0;
  _M0L6_2atmpS2556 = _M0Lm2e2S643;
  if (_M0L6_2atmpS2556 >= 0) {
    int32_t _M0L6_2atmpS2578 = _M0Lm2e2S643;
    int32_t _M0L6_2atmpS2574;
    int32_t _M0L6_2atmpS2577;
    int32_t _M0L6_2atmpS2576;
    int32_t _M0L6_2atmpS2575;
    int32_t _M0L1qS656;
    int32_t _M0L6_2atmpS2573;
    int32_t _M0L6_2atmpS2572;
    int32_t _M0L1kS657;
    int32_t _M0L6_2atmpS2571;
    int32_t _M0L6_2atmpS2570;
    int32_t _M0L6_2atmpS2569;
    int32_t _M0L1iS658;
    struct _M0TPB8Pow5Pair _M0L4pow5S659;
    uint64_t _M0L6_2atmpS2568;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS660;
    uint64_t _M0L8_2avrOutS661;
    uint64_t _M0L8_2avpOutS662;
    uint64_t _M0L8_2avmOutS663;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2574 = _M0FPB9log10Pow2(_M0L6_2atmpS2578);
    _M0L6_2atmpS2577 = _M0Lm2e2S643;
    _M0L6_2atmpS2576 = _M0L6_2atmpS2577 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2575 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS2576);
    _M0L1qS656 = _M0L6_2atmpS2574 - _M0L6_2atmpS2575;
    _M0Lm3e10S653 = _M0L1qS656;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2573 = _M0FPB8pow5bits(_M0L1qS656);
    _M0L6_2atmpS2572 = 125 + _M0L6_2atmpS2573;
    _M0L1kS657 = _M0L6_2atmpS2572 - 1;
    _M0L6_2atmpS2571 = _M0Lm2e2S643;
    _M0L6_2atmpS2570 = -_M0L6_2atmpS2571;
    _M0L6_2atmpS2569 = _M0L6_2atmpS2570 + _M0L1qS656;
    _M0L1iS658 = _M0L6_2atmpS2569 + _M0L1kS657;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S659 = _M0FPB22double__computeInvPow5(_M0L1qS656);
    _M0L6_2atmpS2568 = _M0Lm2m2S644;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS660
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS2568, _M0L4pow5S659, _M0L1iS658, _M0L7mmShiftS649);
    _M0L8_2avrOutS661 = _M0L7_2abindS660.$0;
    _M0L8_2avpOutS662 = _M0L7_2abindS660.$1;
    _M0L8_2avmOutS663 = _M0L7_2abindS660.$2;
    _M0Lm2vrS650 = _M0L8_2avrOutS661;
    _M0Lm2vpS651 = _M0L8_2avpOutS662;
    _M0Lm2vmS652 = _M0L8_2avmOutS663;
    if (_M0L1qS656 <= 21) {
      int32_t _M0L6_2atmpS2564 = (int32_t)_M0L2mvS648;
      uint64_t _M0L6_2atmpS2567 = _M0L2mvS648 / 5ull;
      int32_t _M0L6_2atmpS2566 = (int32_t)_M0L6_2atmpS2567;
      int32_t _M0L6_2atmpS2565 = 5 * _M0L6_2atmpS2566;
      int32_t _M0L6mvMod5S664 = _M0L6_2atmpS2564 - _M0L6_2atmpS2565;
      if (_M0L6mvMod5S664 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS655
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS648, _M0L1qS656);
      } else if (_M0L4evenS647) {
        uint64_t _M0L6_2atmpS2558 = _M0L2mvS648 - 1ull;
        uint64_t _M0L6_2atmpS2559;
        uint64_t _M0L6_2atmpS2557;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2559 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS649);
        _M0L6_2atmpS2557 = _M0L6_2atmpS2558 - _M0L6_2atmpS2559;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS654
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS2557, _M0L1qS656);
      } else {
        uint64_t _M0L6_2atmpS2560 = _M0Lm2vpS651;
        uint64_t _M0L6_2atmpS2563 = _M0L2mvS648 + 2ull;
        int32_t _M0L6_2atmpS2562;
        uint64_t _M0L6_2atmpS2561;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2562
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS2563, _M0L1qS656);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2561 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS2562);
        _M0Lm2vpS651 = _M0L6_2atmpS2560 - _M0L6_2atmpS2561;
      }
    }
  } else {
    int32_t _M0L6_2atmpS2592 = _M0Lm2e2S643;
    int32_t _M0L6_2atmpS2591 = -_M0L6_2atmpS2592;
    int32_t _M0L6_2atmpS2586;
    int32_t _M0L6_2atmpS2590;
    int32_t _M0L6_2atmpS2589;
    int32_t _M0L6_2atmpS2588;
    int32_t _M0L6_2atmpS2587;
    int32_t _M0L1qS665;
    int32_t _M0L6_2atmpS2579;
    int32_t _M0L6_2atmpS2585;
    int32_t _M0L6_2atmpS2584;
    int32_t _M0L1iS666;
    int32_t _M0L6_2atmpS2583;
    int32_t _M0L1kS667;
    int32_t _M0L1jS668;
    struct _M0TPB8Pow5Pair _M0L4pow5S669;
    uint64_t _M0L6_2atmpS2582;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS670;
    uint64_t _M0L8_2avrOutS671;
    uint64_t _M0L8_2avpOutS672;
    uint64_t _M0L8_2avmOutS673;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2586 = _M0FPB9log10Pow5(_M0L6_2atmpS2591);
    _M0L6_2atmpS2590 = _M0Lm2e2S643;
    _M0L6_2atmpS2589 = -_M0L6_2atmpS2590;
    _M0L6_2atmpS2588 = _M0L6_2atmpS2589 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2587 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS2588);
    _M0L1qS665 = _M0L6_2atmpS2586 - _M0L6_2atmpS2587;
    _M0L6_2atmpS2579 = _M0Lm2e2S643;
    _M0Lm3e10S653 = _M0L1qS665 + _M0L6_2atmpS2579;
    _M0L6_2atmpS2585 = _M0Lm2e2S643;
    _M0L6_2atmpS2584 = -_M0L6_2atmpS2585;
    _M0L1iS666 = _M0L6_2atmpS2584 - _M0L1qS665;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2583 = _M0FPB8pow5bits(_M0L1iS666);
    _M0L1kS667 = _M0L6_2atmpS2583 - 125;
    _M0L1jS668 = _M0L1qS665 - _M0L1kS667;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S669 = _M0FPB19double__computePow5(_M0L1iS666);
    _M0L6_2atmpS2582 = _M0Lm2m2S644;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS670
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS2582, _M0L4pow5S669, _M0L1jS668, _M0L7mmShiftS649);
    _M0L8_2avrOutS671 = _M0L7_2abindS670.$0;
    _M0L8_2avpOutS672 = _M0L7_2abindS670.$1;
    _M0L8_2avmOutS673 = _M0L7_2abindS670.$2;
    _M0Lm2vrS650 = _M0L8_2avrOutS671;
    _M0Lm2vpS651 = _M0L8_2avpOutS672;
    _M0Lm2vmS652 = _M0L8_2avmOutS673;
    if (_M0L1qS665 <= 1) {
      _M0Lm17vrIsTrailingZerosS655 = 1;
      if (_M0L4evenS647) {
        int32_t _M0L6_2atmpS2580;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2580 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS649);
        _M0Lm17vmIsTrailingZerosS654 = _M0L6_2atmpS2580 == 1;
      } else {
        uint64_t _M0L6_2atmpS2581 = _M0Lm2vpS651;
        _M0Lm2vpS651 = _M0L6_2atmpS2581 - 1ull;
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
    int32_t _if__result_5878;
    uint64_t _M0L6_2atmpS2622;
    uint64_t _M0L6_2atmpS2628;
    uint64_t _M0L6_2atmpS2629;
    int32_t _if__result_5879;
    int32_t _M0L6_2atmpS2625;
    int64_t _M0L6_2atmpS2624;
    uint64_t _M0L6_2atmpS2623;
    while (1) {
      uint64_t _M0L6_2atmpS2605 = _M0Lm2vpS651;
      uint64_t _M0L7vpDiv10S677 = _M0L6_2atmpS2605 / 10ull;
      uint64_t _M0L6_2atmpS2604 = _M0Lm2vmS652;
      uint64_t _M0L7vmDiv10S678 = _M0L6_2atmpS2604 / 10ull;
      uint64_t _M0L6_2atmpS2603;
      int32_t _M0L6_2atmpS2600;
      int32_t _M0L6_2atmpS2602;
      int32_t _M0L6_2atmpS2601;
      int32_t _M0L7vmMod10S680;
      uint64_t _M0L6_2atmpS2599;
      uint64_t _M0L7vrDiv10S681;
      uint64_t _M0L6_2atmpS2598;
      int32_t _M0L6_2atmpS2595;
      int32_t _M0L6_2atmpS2597;
      int32_t _M0L6_2atmpS2596;
      int32_t _M0L7vrMod10S682;
      int32_t _M0L6_2atmpS2594;
      if (_M0L7vpDiv10S677 <= _M0L7vmDiv10S678) {
        break;
      }
      _M0L6_2atmpS2603 = _M0Lm2vmS652;
      _M0L6_2atmpS2600 = (int32_t)_M0L6_2atmpS2603;
      _M0L6_2atmpS2602 = (int32_t)_M0L7vmDiv10S678;
      _M0L6_2atmpS2601 = 10 * _M0L6_2atmpS2602;
      _M0L7vmMod10S680 = _M0L6_2atmpS2600 - _M0L6_2atmpS2601;
      _M0L6_2atmpS2599 = _M0Lm2vrS650;
      _M0L7vrDiv10S681 = _M0L6_2atmpS2599 / 10ull;
      _M0L6_2atmpS2598 = _M0Lm2vrS650;
      _M0L6_2atmpS2595 = (int32_t)_M0L6_2atmpS2598;
      _M0L6_2atmpS2597 = (int32_t)_M0L7vrDiv10S681;
      _M0L6_2atmpS2596 = 10 * _M0L6_2atmpS2597;
      _M0L7vrMod10S682 = _M0L6_2atmpS2595 - _M0L6_2atmpS2596;
      _M0Lm17vmIsTrailingZerosS654
      = _M0Lm17vmIsTrailingZerosS654 && _M0L7vmMod10S680 == 0;
      if (_M0Lm17vrIsTrailingZerosS655) {
        int32_t _M0L6_2atmpS2593 = _M0Lm16lastRemovedDigitS675;
        _M0Lm17vrIsTrailingZerosS655 = _M0L6_2atmpS2593 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS655 = 0;
      }
      _M0Lm16lastRemovedDigitS675 = _M0L7vrMod10S682;
      _M0Lm2vrS650 = _M0L7vrDiv10S681;
      _M0Lm2vpS651 = _M0L7vpDiv10S677;
      _M0Lm2vmS652 = _M0L7vmDiv10S678;
      _M0L6_2atmpS2594 = _M0Lm7removedS674;
      _M0Lm7removedS674 = _M0L6_2atmpS2594 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS654) {
      while (1) {
        uint64_t _M0L6_2atmpS2618 = _M0Lm2vmS652;
        uint64_t _M0L7vmDiv10S683 = _M0L6_2atmpS2618 / 10ull;
        uint64_t _M0L6_2atmpS2617 = _M0Lm2vmS652;
        int32_t _M0L6_2atmpS2614 = (int32_t)_M0L6_2atmpS2617;
        int32_t _M0L6_2atmpS2616 = (int32_t)_M0L7vmDiv10S683;
        int32_t _M0L6_2atmpS2615 = 10 * _M0L6_2atmpS2616;
        int32_t _M0L7vmMod10S684 = _M0L6_2atmpS2614 - _M0L6_2atmpS2615;
        uint64_t _M0L6_2atmpS2613;
        uint64_t _M0L7vpDiv10S686;
        uint64_t _M0L6_2atmpS2612;
        uint64_t _M0L7vrDiv10S687;
        uint64_t _M0L6_2atmpS2611;
        int32_t _M0L6_2atmpS2608;
        int32_t _M0L6_2atmpS2610;
        int32_t _M0L6_2atmpS2609;
        int32_t _M0L7vrMod10S688;
        int32_t _M0L6_2atmpS2607;
        if (_M0L7vmMod10S684 != 0) {
          break;
        }
        _M0L6_2atmpS2613 = _M0Lm2vpS651;
        _M0L7vpDiv10S686 = _M0L6_2atmpS2613 / 10ull;
        _M0L6_2atmpS2612 = _M0Lm2vrS650;
        _M0L7vrDiv10S687 = _M0L6_2atmpS2612 / 10ull;
        _M0L6_2atmpS2611 = _M0Lm2vrS650;
        _M0L6_2atmpS2608 = (int32_t)_M0L6_2atmpS2611;
        _M0L6_2atmpS2610 = (int32_t)_M0L7vrDiv10S687;
        _M0L6_2atmpS2609 = 10 * _M0L6_2atmpS2610;
        _M0L7vrMod10S688 = _M0L6_2atmpS2608 - _M0L6_2atmpS2609;
        if (_M0Lm17vrIsTrailingZerosS655) {
          int32_t _M0L6_2atmpS2606 = _M0Lm16lastRemovedDigitS675;
          _M0Lm17vrIsTrailingZerosS655 = _M0L6_2atmpS2606 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS655 = 0;
        }
        _M0Lm16lastRemovedDigitS675 = _M0L7vrMod10S688;
        _M0Lm2vrS650 = _M0L7vrDiv10S687;
        _M0Lm2vpS651 = _M0L7vpDiv10S686;
        _M0Lm2vmS652 = _M0L7vmDiv10S683;
        _M0L6_2atmpS2607 = _M0Lm7removedS674;
        _M0Lm7removedS674 = _M0L6_2atmpS2607 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS655) {
      int32_t _M0L6_2atmpS2621 = _M0Lm16lastRemovedDigitS675;
      if (_M0L6_2atmpS2621 == 5) {
        uint64_t _M0L6_2atmpS2620 = _M0Lm2vrS650;
        uint64_t _M0L6_2atmpS2619 = _M0L6_2atmpS2620 % 2ull;
        _if__result_5878 = _M0L6_2atmpS2619 == 0ull;
      } else {
        _if__result_5878 = 0;
      }
    } else {
      _if__result_5878 = 0;
    }
    if (_if__result_5878) {
      _M0Lm16lastRemovedDigitS675 = 4;
    }
    _M0L6_2atmpS2622 = _M0Lm2vrS650;
    _M0L6_2atmpS2628 = _M0Lm2vrS650;
    _M0L6_2atmpS2629 = _M0Lm2vmS652;
    if (_M0L6_2atmpS2628 == _M0L6_2atmpS2629) {
      if (!_M0L4evenS647) {
        _if__result_5879 = 1;
      } else {
        int32_t _M0L6_2atmpS2627 = _M0Lm17vmIsTrailingZerosS654;
        _if__result_5879 = !_M0L6_2atmpS2627;
      }
    } else {
      _if__result_5879 = 0;
    }
    if (_if__result_5879) {
      _M0L6_2atmpS2625 = 1;
    } else {
      int32_t _M0L6_2atmpS2626 = _M0Lm16lastRemovedDigitS675;
      _M0L6_2atmpS2625 = _M0L6_2atmpS2626 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2624 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS2625);
    _M0L6_2atmpS2623 = *(uint64_t*)&_M0L6_2atmpS2624;
    _M0Lm6outputS676 = _M0L6_2atmpS2622 + _M0L6_2atmpS2623;
  } else {
    int32_t _M0Lm7roundUpS689 = 0;
    uint64_t _M0L6_2atmpS2650 = _M0Lm2vpS651;
    uint64_t _M0L8vpDiv100S690 = _M0L6_2atmpS2650 / 100ull;
    uint64_t _M0L6_2atmpS2649 = _M0Lm2vmS652;
    uint64_t _M0L8vmDiv100S691 = _M0L6_2atmpS2649 / 100ull;
    uint64_t _M0L6_2atmpS2644;
    uint64_t _M0L6_2atmpS2647;
    uint64_t _M0L6_2atmpS2648;
    int32_t _M0L6_2atmpS2646;
    uint64_t _M0L6_2atmpS2645;
    if (_M0L8vpDiv100S690 > _M0L8vmDiv100S691) {
      uint64_t _M0L6_2atmpS2635 = _M0Lm2vrS650;
      uint64_t _M0L8vrDiv100S692 = _M0L6_2atmpS2635 / 100ull;
      uint64_t _M0L6_2atmpS2634 = _M0Lm2vrS650;
      int32_t _M0L6_2atmpS2631 = (int32_t)_M0L6_2atmpS2634;
      int32_t _M0L6_2atmpS2633 = (int32_t)_M0L8vrDiv100S692;
      int32_t _M0L6_2atmpS2632 = 100 * _M0L6_2atmpS2633;
      int32_t _M0L8vrMod100S693 = _M0L6_2atmpS2631 - _M0L6_2atmpS2632;
      int32_t _M0L6_2atmpS2630;
      _M0Lm7roundUpS689 = _M0L8vrMod100S693 >= 50;
      _M0Lm2vrS650 = _M0L8vrDiv100S692;
      _M0Lm2vpS651 = _M0L8vpDiv100S690;
      _M0Lm2vmS652 = _M0L8vmDiv100S691;
      _M0L6_2atmpS2630 = _M0Lm7removedS674;
      _M0Lm7removedS674 = _M0L6_2atmpS2630 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS2643 = _M0Lm2vpS651;
      uint64_t _M0L7vpDiv10S694 = _M0L6_2atmpS2643 / 10ull;
      uint64_t _M0L6_2atmpS2642 = _M0Lm2vmS652;
      uint64_t _M0L7vmDiv10S695 = _M0L6_2atmpS2642 / 10ull;
      uint64_t _M0L6_2atmpS2641;
      uint64_t _M0L7vrDiv10S697;
      uint64_t _M0L6_2atmpS2640;
      int32_t _M0L6_2atmpS2637;
      int32_t _M0L6_2atmpS2639;
      int32_t _M0L6_2atmpS2638;
      int32_t _M0L7vrMod10S698;
      int32_t _M0L6_2atmpS2636;
      if (_M0L7vpDiv10S694 <= _M0L7vmDiv10S695) {
        break;
      }
      _M0L6_2atmpS2641 = _M0Lm2vrS650;
      _M0L7vrDiv10S697 = _M0L6_2atmpS2641 / 10ull;
      _M0L6_2atmpS2640 = _M0Lm2vrS650;
      _M0L6_2atmpS2637 = (int32_t)_M0L6_2atmpS2640;
      _M0L6_2atmpS2639 = (int32_t)_M0L7vrDiv10S697;
      _M0L6_2atmpS2638 = 10 * _M0L6_2atmpS2639;
      _M0L7vrMod10S698 = _M0L6_2atmpS2637 - _M0L6_2atmpS2638;
      _M0Lm7roundUpS689 = _M0L7vrMod10S698 >= 5;
      _M0Lm2vrS650 = _M0L7vrDiv10S697;
      _M0Lm2vpS651 = _M0L7vpDiv10S694;
      _M0Lm2vmS652 = _M0L7vmDiv10S695;
      _M0L6_2atmpS2636 = _M0Lm7removedS674;
      _M0Lm7removedS674 = _M0L6_2atmpS2636 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS2644 = _M0Lm2vrS650;
    _M0L6_2atmpS2647 = _M0Lm2vrS650;
    _M0L6_2atmpS2648 = _M0Lm2vmS652;
    _M0L6_2atmpS2646
    = _M0L6_2atmpS2647 == _M0L6_2atmpS2648 || _M0Lm7roundUpS689;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2645 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS2646);
    _M0Lm6outputS676 = _M0L6_2atmpS2644 + _M0L6_2atmpS2645;
  }
  _M0L6_2atmpS2652 = _M0Lm3e10S653;
  _M0L6_2atmpS2653 = _M0Lm7removedS674;
  _M0L3expS699 = _M0L6_2atmpS2652 + _M0L6_2atmpS2653;
  _M0L6_2atmpS2651 = _M0Lm6outputS676;
  _block_5881
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_5881)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5881->$0 = _M0L6_2atmpS2651;
  _block_5881->$1 = _M0L3expS699;
  return _block_5881;
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
  int32_t _M0L6_2atmpS2552;
  int32_t _M0L6_2atmpS2551;
  int32_t _M0L4baseS621;
  int32_t _M0L5base2S623;
  int32_t _M0L6offsetS624;
  int32_t _M0L6_2atmpS2550;
  uint64_t _M0L4mul0S625;
  int32_t _M0L6_2atmpS2549;
  int32_t _M0L6_2atmpS2548;
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
  int32_t _M0L6_2atmpS2546;
  int32_t _M0L6_2atmpS2547;
  int32_t _M0L5deltaS636;
  uint64_t _M0L6_2atmpS2545;
  uint64_t _M0L6_2atmpS2537;
  int32_t _M0L6_2atmpS2544;
  uint32_t _M0L6_2atmpS2541;
  int32_t _M0L6_2atmpS2543;
  int32_t _M0L6_2atmpS2542;
  uint32_t _M0L6_2atmpS2540;
  uint32_t _M0L6_2atmpS2539;
  uint64_t _M0L6_2atmpS2538;
  uint64_t _M0L1aS637;
  uint64_t _M0L6_2atmpS2536;
  uint64_t _M0L1bS638;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2552 = _M0L1iS622 + 26;
  _M0L6_2atmpS2551 = _M0L6_2atmpS2552 - 1;
  _M0L4baseS621 = _M0L6_2atmpS2551 / 26;
  _M0L5base2S623 = _M0L4baseS621 * 26;
  _M0L6offsetS624 = _M0L5base2S623 - _M0L1iS622;
  _M0L6_2atmpS2550 = _M0L4baseS621 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S625
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS2550);
  _M0L6_2atmpS2549 = _M0L4baseS621 * 2;
  _M0L6_2atmpS2548 = _M0L6_2atmpS2549 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S626
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS2548);
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
    uint64_t _M0L6_2atmpS2535 = _M0Lm5high1S635;
    _M0Lm5high1S635 = _M0L6_2atmpS2535 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2546 = _M0FPB8pow5bits(_M0L5base2S623);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2547 = _M0FPB8pow5bits(_M0L1iS622);
  _M0L5deltaS636 = _M0L6_2atmpS2546 - _M0L6_2atmpS2547;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2545
  = _M0FPB13shiftright128(_M0L7_2alow0S632, _M0L3sumS634, _M0L5deltaS636);
  _M0L6_2atmpS2537 = _M0L6_2atmpS2545 + 1ull;
  _M0L6_2atmpS2544 = _M0L1iS622 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2541
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS2544);
  _M0L6_2atmpS2543 = _M0L1iS622 % 16;
  _M0L6_2atmpS2542 = _M0L6_2atmpS2543 << 1;
  _M0L6_2atmpS2540 = _M0L6_2atmpS2541 >> (_M0L6_2atmpS2542 & 31);
  _M0L6_2atmpS2539 = _M0L6_2atmpS2540 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2538 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS2539);
  _M0L1aS637 = _M0L6_2atmpS2537 + _M0L6_2atmpS2538;
  _M0L6_2atmpS2536 = _M0Lm5high1S635;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS638
  = _M0FPB13shiftright128(_M0L3sumS634, _M0L6_2atmpS2536, _M0L5deltaS636);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS637, .$1 = _M0L1bS638};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS604) {
  int32_t _M0L4baseS603;
  int32_t _M0L5base2S605;
  int32_t _M0L6offsetS606;
  int32_t _M0L6_2atmpS2534;
  uint64_t _M0L4mul0S607;
  int32_t _M0L6_2atmpS2533;
  int32_t _M0L6_2atmpS2532;
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
  int32_t _M0L6_2atmpS2530;
  int32_t _M0L6_2atmpS2531;
  int32_t _M0L5deltaS618;
  uint64_t _M0L6_2atmpS2522;
  int32_t _M0L6_2atmpS2529;
  uint32_t _M0L6_2atmpS2526;
  int32_t _M0L6_2atmpS2528;
  int32_t _M0L6_2atmpS2527;
  uint32_t _M0L6_2atmpS2525;
  uint32_t _M0L6_2atmpS2524;
  uint64_t _M0L6_2atmpS2523;
  uint64_t _M0L1aS619;
  uint64_t _M0L6_2atmpS2521;
  uint64_t _M0L1bS620;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS603 = _M0L1iS604 / 26;
  _M0L5base2S605 = _M0L4baseS603 * 26;
  _M0L6offsetS606 = _M0L1iS604 - _M0L5base2S605;
  _M0L6_2atmpS2534 = _M0L4baseS603 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S607
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS2534);
  _M0L6_2atmpS2533 = _M0L4baseS603 * 2;
  _M0L6_2atmpS2532 = _M0L6_2atmpS2533 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S608
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS2532);
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
    uint64_t _M0L6_2atmpS2520 = _M0Lm5high1S617;
    _M0Lm5high1S617 = _M0L6_2atmpS2520 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2530 = _M0FPB8pow5bits(_M0L1iS604);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2531 = _M0FPB8pow5bits(_M0L5base2S605);
  _M0L5deltaS618 = _M0L6_2atmpS2530 - _M0L6_2atmpS2531;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2522
  = _M0FPB13shiftright128(_M0L7_2alow0S614, _M0L3sumS616, _M0L5deltaS618);
  _M0L6_2atmpS2529 = _M0L1iS604 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2526
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS2529);
  _M0L6_2atmpS2528 = _M0L1iS604 % 16;
  _M0L6_2atmpS2527 = _M0L6_2atmpS2528 << 1;
  _M0L6_2atmpS2525 = _M0L6_2atmpS2526 >> (_M0L6_2atmpS2527 & 31);
  _M0L6_2atmpS2524 = _M0L6_2atmpS2525 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2523 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS2524);
  _M0L1aS619 = _M0L6_2atmpS2522 + _M0L6_2atmpS2523;
  _M0L6_2atmpS2521 = _M0Lm5high1S617;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS620
  = _M0FPB13shiftright128(_M0L3sumS616, _M0L6_2atmpS2521, _M0L5deltaS618);
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
  uint64_t _M0L6_2atmpS2519;
  uint64_t _M0L2hiS585;
  uint64_t _M0L3lo2S586;
  uint64_t _M0L6_2atmpS2517;
  uint64_t _M0L6_2atmpS2518;
  uint64_t _M0L4mid2S587;
  uint64_t _M0L6_2atmpS2516;
  uint64_t _M0L3hi2S588;
  int32_t _M0L6_2atmpS2515;
  int32_t _M0L6_2atmpS2514;
  uint64_t _M0L2vpS589;
  uint64_t _M0Lm2vmS591;
  int32_t _M0L6_2atmpS2513;
  int32_t _M0L6_2atmpS2512;
  uint64_t _M0L2vrS602;
  uint64_t _M0L6_2atmpS2511;
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
    _M0L6_2atmpS2519 = 1ull;
  } else {
    _M0L6_2atmpS2519 = 0ull;
  }
  _M0L2hiS585 = _M0L6_2ahi2S583 + _M0L6_2atmpS2519;
  _M0L3lo2S586 = _M0L5_2aloS579 + _M0L7_2amul0S573;
  _M0L6_2atmpS2517 = _M0L3midS584 + _M0L7_2amul1S575;
  if (_M0L3lo2S586 < _M0L5_2aloS579) {
    _M0L6_2atmpS2518 = 1ull;
  } else {
    _M0L6_2atmpS2518 = 0ull;
  }
  _M0L4mid2S587 = _M0L6_2atmpS2517 + _M0L6_2atmpS2518;
  if (_M0L4mid2S587 < _M0L3midS584) {
    _M0L6_2atmpS2516 = 1ull;
  } else {
    _M0L6_2atmpS2516 = 0ull;
  }
  _M0L3hi2S588 = _M0L2hiS585 + _M0L6_2atmpS2516;
  _M0L6_2atmpS2515 = _M0L1jS590 - 64;
  _M0L6_2atmpS2514 = _M0L6_2atmpS2515 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS589
  = _M0FPB13shiftright128(_M0L4mid2S587, _M0L3hi2S588, _M0L6_2atmpS2514);
  _M0Lm2vmS591 = 0ull;
  if (_M0L7mmShiftS592) {
    uint64_t _M0L3lo3S593 = _M0L5_2aloS579 - _M0L7_2amul0S573;
    uint64_t _M0L6_2atmpS2501 = _M0L3midS584 - _M0L7_2amul1S575;
    uint64_t _M0L6_2atmpS2502;
    uint64_t _M0L4mid3S594;
    uint64_t _M0L6_2atmpS2500;
    uint64_t _M0L3hi3S595;
    int32_t _M0L6_2atmpS2499;
    int32_t _M0L6_2atmpS2498;
    if (_M0L5_2aloS579 < _M0L3lo3S593) {
      _M0L6_2atmpS2502 = 1ull;
    } else {
      _M0L6_2atmpS2502 = 0ull;
    }
    _M0L4mid3S594 = _M0L6_2atmpS2501 - _M0L6_2atmpS2502;
    if (_M0L3midS584 < _M0L4mid3S594) {
      _M0L6_2atmpS2500 = 1ull;
    } else {
      _M0L6_2atmpS2500 = 0ull;
    }
    _M0L3hi3S595 = _M0L2hiS585 - _M0L6_2atmpS2500;
    _M0L6_2atmpS2499 = _M0L1jS590 - 64;
    _M0L6_2atmpS2498 = _M0L6_2atmpS2499 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS591
    = _M0FPB13shiftright128(_M0L4mid3S594, _M0L3hi3S595, _M0L6_2atmpS2498);
  } else {
    uint64_t _M0L3lo3S596 = _M0L5_2aloS579 + _M0L5_2aloS579;
    uint64_t _M0L6_2atmpS2509 = _M0L3midS584 + _M0L3midS584;
    uint64_t _M0L6_2atmpS2510;
    uint64_t _M0L4mid3S597;
    uint64_t _M0L6_2atmpS2507;
    uint64_t _M0L6_2atmpS2508;
    uint64_t _M0L3hi3S598;
    uint64_t _M0L3lo4S599;
    uint64_t _M0L6_2atmpS2505;
    uint64_t _M0L6_2atmpS2506;
    uint64_t _M0L4mid4S600;
    uint64_t _M0L6_2atmpS2504;
    uint64_t _M0L3hi4S601;
    int32_t _M0L6_2atmpS2503;
    if (_M0L3lo3S596 < _M0L5_2aloS579) {
      _M0L6_2atmpS2510 = 1ull;
    } else {
      _M0L6_2atmpS2510 = 0ull;
    }
    _M0L4mid3S597 = _M0L6_2atmpS2509 + _M0L6_2atmpS2510;
    _M0L6_2atmpS2507 = _M0L2hiS585 + _M0L2hiS585;
    if (_M0L4mid3S597 < _M0L3midS584) {
      _M0L6_2atmpS2508 = 1ull;
    } else {
      _M0L6_2atmpS2508 = 0ull;
    }
    _M0L3hi3S598 = _M0L6_2atmpS2507 + _M0L6_2atmpS2508;
    _M0L3lo4S599 = _M0L3lo3S596 - _M0L7_2amul0S573;
    _M0L6_2atmpS2505 = _M0L4mid3S597 - _M0L7_2amul1S575;
    if (_M0L3lo3S596 < _M0L3lo4S599) {
      _M0L6_2atmpS2506 = 1ull;
    } else {
      _M0L6_2atmpS2506 = 0ull;
    }
    _M0L4mid4S600 = _M0L6_2atmpS2505 - _M0L6_2atmpS2506;
    if (_M0L4mid3S597 < _M0L4mid4S600) {
      _M0L6_2atmpS2504 = 1ull;
    } else {
      _M0L6_2atmpS2504 = 0ull;
    }
    _M0L3hi4S601 = _M0L3hi3S598 - _M0L6_2atmpS2504;
    _M0L6_2atmpS2503 = _M0L1jS590 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS591
    = _M0FPB13shiftright128(_M0L4mid4S600, _M0L3hi4S601, _M0L6_2atmpS2503);
  }
  _M0L6_2atmpS2513 = _M0L1jS590 - 64;
  _M0L6_2atmpS2512 = _M0L6_2atmpS2513 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS602
  = _M0FPB13shiftright128(_M0L3midS584, _M0L2hiS585, _M0L6_2atmpS2512);
  _M0L6_2atmpS2511 = _M0Lm2vmS591;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS602,
                                                .$1 = _M0L2vpS589,
                                                .$2 = _M0L6_2atmpS2511};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS571,
  int32_t _M0L1pS572
) {
  uint64_t _M0L6_2atmpS2497;
  uint64_t _M0L6_2atmpS2496;
  uint64_t _M0L6_2atmpS2495;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2497 = 1ull << (_M0L1pS572 & 63);
  _M0L6_2atmpS2496 = _M0L6_2atmpS2497 - 1ull;
  _M0L6_2atmpS2495 = _M0L5valueS571 & _M0L6_2atmpS2496;
  return _M0L6_2atmpS2495 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS569,
  int32_t _M0L1pS570
) {
  int32_t _M0L6_2atmpS2494;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2494 = _M0FPB10pow5Factor(_M0L5valueS569);
  return _M0L6_2atmpS2494 >= _M0L1pS570;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS564) {
  uint64_t _M0L6_2atmpS2485;
  uint64_t _M0L6_2atmpS2486;
  uint64_t _M0L6_2atmpS2487;
  uint64_t _M0L6_2atmpS2488;
  uint64_t _M0L6_2atmpS2493;
  int32_t _M0L5countS565;
  uint64_t _M0L1vS566;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2485 = _M0L5valueS564 % 5ull;
  if (_M0L6_2atmpS2485 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2486 = _M0L5valueS564 % 25ull;
  if (_M0L6_2atmpS2486 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS2487 = _M0L5valueS564 % 125ull;
  if (_M0L6_2atmpS2487 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS2488 = _M0L5valueS564 % 625ull;
  if (_M0L6_2atmpS2488 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS2493 = _M0L5valueS564 / 625ull;
  _M0L5countS565 = 4;
  _M0L1vS566 = _M0L6_2atmpS2493;
  while (1) {
    if (_M0L1vS566 > 0ull) {
      uint64_t _M0L6_2atmpS2489 = _M0L1vS566 % 5ull;
      int32_t _M0L6_2atmpS2490;
      uint64_t _M0L6_2atmpS2491;
      if (_M0L6_2atmpS2489 != 0ull) {
        return _M0L5countS565;
      }
      _M0L6_2atmpS2490 = _M0L5countS565 + 1;
      _M0L6_2atmpS2491 = _M0L1vS566 / 5ull;
      _M0L5countS565 = _M0L6_2atmpS2490;
      _M0L1vS566 = _M0L6_2atmpS2491;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS568;
      moonbit_string_t _M0L6_2atmpS2492;
      int32_t _result_5883;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS568
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS568, (moonbit_string_t)moonbit_string_literal_13.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS568, _M0L5valueS564);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS2492
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS568);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS568);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_5883 = _M0FPC15abort5abortGiE(_M0L6_2atmpS2492);
      moonbit_decref_cycle_free(_M0L6_2atmpS2492);
      return _result_5883;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS563,
  uint64_t _M0L2hiS561,
  int32_t _M0L4distS562
) {
  int32_t _M0L6_2atmpS2484;
  uint64_t _M0L6_2atmpS2482;
  uint64_t _M0L6_2atmpS2483;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2484 = 64 - _M0L4distS562;
  _M0L6_2atmpS2482 = _M0L2hiS561 << (_M0L6_2atmpS2484 & 63);
  _M0L6_2atmpS2483 = _M0L2loS563 >> (_M0L4distS562 & 63);
  return _M0L6_2atmpS2482 | _M0L6_2atmpS2483;
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
  uint64_t _M0L6_2atmpS2480;
  uint64_t _M0L6_2atmpS2481;
  uint64_t _M0L1yS557;
  uint64_t _M0L6_2atmpS2478;
  uint64_t _M0L6_2atmpS2479;
  uint64_t _M0L1zS558;
  uint64_t _M0L6_2atmpS2476;
  uint64_t _M0L6_2atmpS2477;
  uint64_t _M0L6_2atmpS2474;
  uint64_t _M0L6_2atmpS2475;
  uint64_t _M0L1wS559;
  uint64_t _M0L2loS560;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS550 = _M0L1aS551 & 4294967295ull;
  _M0L3aHiS552 = _M0L1aS551 >> 32;
  _M0L3bLoS553 = _M0L1bS554 & 4294967295ull;
  _M0L3bHiS555 = _M0L1bS554 >> 32;
  _M0L1xS556 = _M0L3aLoS550 * _M0L3bLoS553;
  _M0L6_2atmpS2480 = _M0L3aHiS552 * _M0L3bLoS553;
  _M0L6_2atmpS2481 = _M0L1xS556 >> 32;
  _M0L1yS557 = _M0L6_2atmpS2480 + _M0L6_2atmpS2481;
  _M0L6_2atmpS2478 = _M0L3aLoS550 * _M0L3bHiS555;
  _M0L6_2atmpS2479 = _M0L1yS557 & 4294967295ull;
  _M0L1zS558 = _M0L6_2atmpS2478 + _M0L6_2atmpS2479;
  _M0L6_2atmpS2476 = _M0L3aHiS552 * _M0L3bHiS555;
  _M0L6_2atmpS2477 = _M0L1yS557 >> 32;
  _M0L6_2atmpS2474 = _M0L6_2atmpS2476 + _M0L6_2atmpS2477;
  _M0L6_2atmpS2475 = _M0L1zS558 >> 32;
  _M0L1wS559 = _M0L6_2atmpS2474 + _M0L6_2atmpS2475;
  _M0L2loS560 = _M0L1aS551 * _M0L1bS554;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS560, .$1 = _M0L1wS559};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS548,
  int32_t _M0L4fromS545,
  int32_t _M0L2toS544
) {
  int32_t _M0L3lenS543;
  int32_t _M0L6_2atmpS2473;
  uint16_t* _M0L6bufferS546;
  int32_t _M0L1iS547;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS543 = _M0L2toS544 - _M0L4fromS545;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2473 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS546
  = (uint16_t*)moonbit_make_string(_M0L3lenS543, _M0L6_2atmpS2473);
  _M0L1iS547 = 0;
  while (1) {
    if (_M0L1iS547 < _M0L3lenS543) {
      int32_t _M0L6_2atmpS2471 = _M0L4fromS545 + _M0L1iS547;
      int32_t _M0L6_2atmpS2470;
      int32_t _M0L6_2atmpS2469;
      int32_t _M0L6_2atmpS2472;
      if (
        _M0L6_2atmpS2471 < 0
        || _M0L6_2atmpS2471 >= Moonbit_array_length(_M0L5bytesS548)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2470 = (int32_t)_M0L5bytesS548[_M0L6_2atmpS2471];
      _M0L6_2atmpS2469 = (uint16_t)_M0L6_2atmpS2470;
      if (
        _M0L1iS547 < 0 || _M0L1iS547 >= Moonbit_array_length(_M0L6bufferS546)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS546[_M0L1iS547] = _M0L6_2atmpS2469;
      _M0L6_2atmpS2472 = _M0L1iS547 + 1;
      _M0L1iS547 = _M0L6_2atmpS2472;
      continue;
    }
    break;
  }
  return _M0L6bufferS546;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS542) {
  int32_t _M0L6_2atmpS2468;
  uint32_t _M0L6_2atmpS2467;
  uint32_t _M0L6_2atmpS2466;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2468 = _M0L1eS542 * 78913;
  _M0L6_2atmpS2467 = *(uint32_t*)&_M0L6_2atmpS2468;
  _M0L6_2atmpS2466 = _M0L6_2atmpS2467 >> 18;
  return *(int32_t*)&_M0L6_2atmpS2466;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS541) {
  int32_t _M0L6_2atmpS2465;
  uint32_t _M0L6_2atmpS2464;
  uint32_t _M0L6_2atmpS2463;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2465 = _M0L1eS541 * 732923;
  _M0L6_2atmpS2464 = *(uint32_t*)&_M0L6_2atmpS2465;
  _M0L6_2atmpS2463 = _M0L6_2atmpS2464 >> 20;
  return *(int32_t*)&_M0L6_2atmpS2463;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS539,
  int32_t _M0L8exponentS540,
  int32_t _M0L8mantissaS537
) {
  moonbit_string_t _M0L1sS538;
  moonbit_string_t _result_5886;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS537) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  if (_M0L4signS539) {
    _M0L1sS538 = (moonbit_string_t)moonbit_string_literal_15.data;
  } else {
    _M0L1sS538 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS540) {
    moonbit_string_t _result_5885;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_5885
    = moonbit_add_string(_M0L1sS538, (moonbit_string_t)moonbit_string_literal_16.data);
    moonbit_decref_cycle_free(_M0L1sS538);
    return _result_5885;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_5886
  = moonbit_add_string(_M0L1sS538, (moonbit_string_t)moonbit_string_literal_17.data);
  moonbit_decref_cycle_free(_M0L1sS538);
  return _result_5886;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS536) {
  int32_t _M0L6_2atmpS2462;
  uint32_t _M0L6_2atmpS2461;
  uint32_t _M0L6_2atmpS2460;
  int32_t _M0L6_2atmpS2459;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2462 = _M0L1eS536 * 1217359;
  _M0L6_2atmpS2461 = *(uint32_t*)&_M0L6_2atmpS2462;
  _M0L6_2atmpS2460 = _M0L6_2atmpS2461 >> 19;
  _M0L6_2atmpS2459 = *(int32_t*)&_M0L6_2atmpS2460;
  return _M0L6_2atmpS2459 + 1;
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
  int32_t _M0L3lenS531
) {
  float* _M0L6_2atmpS2456;
  struct _M0TPB5ArrayGfE* _block_5887;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2456 = (float*)moonbit_make_float_array_raw(_M0L3lenS531);
  _block_5887
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_5887)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
  _block_5887->$0 = _M0L6_2atmpS2456;
  _block_5887->$1 = _M0L3lenS531;
  return _block_5887;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS532
) {
  uint8_t* _M0L6_2atmpS2457;
  struct _M0TPB5ArrayGbE* _block_5888;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2457 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS532);
  _block_5888
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_5888)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 66, 0);
  _block_5888->$0 = _M0L6_2atmpS2457;
  _block_5888->$1 = _M0L3lenS532;
  return _block_5888;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS533
) {
  int32_t* _M0L6_2atmpS2458;
  struct _M0TPB5ArrayGiE* _block_5889;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2458 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS533);
  _block_5889
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_5889)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 74, 0);
  _block_5889->$0 = _M0L6_2atmpS2458;
  _block_5889->$1 = _M0L3lenS533;
  return _block_5889;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS527,
  int32_t _M0L5indexS528
) {
  uint64_t* _M0L6_2atmpS2454;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS2454 = _M0L4selfS527;
  if (
    _M0L5indexS528 < 0
    || _M0L5indexS528 >= Moonbit_array_length(_M0L6_2atmpS2454)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS2454[_M0L5indexS528];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS529,
  int32_t _M0L5indexS530
) {
  uint32_t* _M0L6_2atmpS2455;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS2455 = _M0L4selfS529;
  if (
    _M0L5indexS530 < 0
    || _M0L5indexS530 >= Moonbit_array_length(_M0L6_2atmpS2455)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS2455[_M0L5indexS530];
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
  int32_t _M0L3lenS2426;
  moonbit_string_t* _M0L6_2atmpS2428;
  int32_t _M0L6_2atmpS2427;
  int32_t _M0L6lengthS513;
  moonbit_string_t* _M0L3bufS2431;
  moonbit_string_t _M0L6_2aoldS5464;
  int32_t _M0L6_2atmpS2432;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2426 = _M0L4selfS512->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2428 = _M0MPC15array5Array6bufferGsE(_M0L4selfS512);
  _M0L6_2atmpS2427 = Moonbit_array_length(_M0L6_2atmpS2428);
  moonbit_decref_cycle_free(_M0L6_2atmpS2428);
  if (_M0L3lenS2426 == _M0L6_2atmpS2427) {
    int32_t _M0L3lenS2430 = _M0L4selfS512->$1;
    int32_t _M0L6_2atmpS2429 = _M0L3lenS2430 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS512, _M0L6_2atmpS2429);
  }
  _M0L6lengthS513 = _M0L4selfS512->$1;
  _M0L3bufS2431 = _M0L4selfS512->$0;
  _M0L6_2aoldS5464 = (moonbit_string_t)_M0L3bufS2431[_M0L6lengthS513];
  moonbit_decref_cycle_free(_M0L6_2aoldS5464);
  _M0L3bufS2431[_M0L6lengthS513] = _M0L5valueS514;
  _M0L6_2atmpS2432 = _M0L6lengthS513 + 1;
  _M0L4selfS512->$1 = _M0L6_2atmpS2432;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS515,
  struct _M0TUsiE* _M0L5valueS517
) {
  int32_t _M0L3lenS2433;
  struct _M0TUsiE** _M0L6_2atmpS2435;
  int32_t _M0L6_2atmpS2434;
  int32_t _M0L6lengthS516;
  struct _M0TUsiE** _M0L3bufS2438;
  struct _M0TUsiE* _M0L6_2aoldS5465;
  int32_t _M0L6_2atmpS2439;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2433 = _M0L4selfS515->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2435 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS515);
  _M0L6_2atmpS2434 = Moonbit_array_length(_M0L6_2atmpS2435);
  moonbit_decref_cycle_free(_M0L6_2atmpS2435);
  if (_M0L3lenS2433 == _M0L6_2atmpS2434) {
    int32_t _M0L3lenS2437 = _M0L4selfS515->$1;
    int32_t _M0L6_2atmpS2436 = _M0L3lenS2437 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS515, _M0L6_2atmpS2436);
  }
  _M0L6lengthS516 = _M0L4selfS515->$1;
  _M0L3bufS2438 = _M0L4selfS515->$0;
  _M0L6_2aoldS5465 = (struct _M0TUsiE*)_M0L3bufS2438[_M0L6lengthS516];
  if (_M0L6_2aoldS5465) {
    moonbit_decref_cycle_free(_M0L6_2aoldS5465);
  }
  _M0L3bufS2438[_M0L6lengthS516] = _M0L5valueS517;
  _M0L6_2atmpS2439 = _M0L6lengthS516 + 1;
  _M0L4selfS515->$1 = _M0L6_2atmpS2439;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS518,
  float _M0L5valueS520
) {
  int32_t _M0L3lenS2440;
  float* _M0L6_2atmpS2442;
  int32_t _M0L6_2atmpS2441;
  int32_t _M0L6lengthS519;
  float* _M0L3bufS2445;
  int32_t _M0L6_2atmpS2446;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2440 = _M0L4selfS518->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2442 = _M0MPC15array5Array6bufferGfE(_M0L4selfS518);
  _M0L6_2atmpS2441 = Moonbit_array_length(_M0L6_2atmpS2442);
  moonbit_decref_cycle_free(_M0L6_2atmpS2442);
  if (_M0L3lenS2440 == _M0L6_2atmpS2441) {
    int32_t _M0L3lenS2444 = _M0L4selfS518->$1;
    int32_t _M0L6_2atmpS2443 = _M0L3lenS2444 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS518, _M0L6_2atmpS2443);
  }
  _M0L6lengthS519 = _M0L4selfS518->$1;
  _M0L3bufS2445 = _M0L4selfS518->$0;
  _M0L3bufS2445[_M0L6lengthS519] = _M0L5valueS520;
  _M0L6_2atmpS2446 = _M0L6lengthS519 + 1;
  _M0L4selfS518->$1 = _M0L6_2atmpS2446;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS521,
  int32_t _M0L5valueS523
) {
  int32_t _M0L3lenS2447;
  int32_t* _M0L6_2atmpS2449;
  int32_t _M0L6_2atmpS2448;
  int32_t _M0L6lengthS522;
  int32_t* _M0L3bufS2452;
  int32_t _M0L6_2atmpS2453;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2447 = _M0L4selfS521->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2449 = _M0MPC15array5Array6bufferGiE(_M0L4selfS521);
  _M0L6_2atmpS2448 = Moonbit_array_length(_M0L6_2atmpS2449);
  moonbit_decref_cycle_free(_M0L6_2atmpS2449);
  if (_M0L3lenS2447 == _M0L6_2atmpS2448) {
    int32_t _M0L3lenS2451 = _M0L4selfS521->$1;
    int32_t _M0L6_2atmpS2450 = _M0L3lenS2451 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS521, _M0L6_2atmpS2450);
  }
  _M0L6lengthS522 = _M0L4selfS521->$1;
  _M0L3bufS2452 = _M0L4selfS521->$0;
  _M0L3bufS2452[_M0L6lengthS522] = _M0L5valueS523;
  _M0L6_2atmpS2453 = _M0L6lengthS522 + 1;
  _M0L4selfS521->$1 = _M0L6_2atmpS2453;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS497,
  int32_t _M0L8requiredS499
) {
  int32_t _M0L8old__capS496;
  int32_t _M0L3lenS2422;
  int32_t _M0L8new__capS498;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS496 = _M0MPC15array5Array8capacityGsE(_M0L4selfS497);
  _M0L3lenS2422 = _M0L4selfS497->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS498
  = _M0FPB23array__growth__capacity(_M0L8old__capS496, _M0L3lenS2422, _M0L8requiredS499);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS497, _M0L8new__capS498);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS501,
  int32_t _M0L8requiredS503
) {
  int32_t _M0L8old__capS500;
  int32_t _M0L3lenS2423;
  int32_t _M0L8new__capS502;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS500 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS501);
  _M0L3lenS2423 = _M0L4selfS501->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS502
  = _M0FPB23array__growth__capacity(_M0L8old__capS500, _M0L3lenS2423, _M0L8requiredS503);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS501, _M0L8new__capS502);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS505,
  int32_t _M0L8requiredS507
) {
  int32_t _M0L8old__capS504;
  int32_t _M0L3lenS2424;
  int32_t _M0L8new__capS506;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS504 = _M0MPC15array5Array8capacityGfE(_M0L4selfS505);
  _M0L3lenS2424 = _M0L4selfS505->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS506
  = _M0FPB23array__growth__capacity(_M0L8old__capS504, _M0L3lenS2424, _M0L8requiredS507);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS505, _M0L8new__capS506);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS509,
  int32_t _M0L8requiredS511
) {
  int32_t _M0L8old__capS508;
  int32_t _M0L3lenS2425;
  int32_t _M0L8new__capS510;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS508 = _M0MPC15array5Array8capacityGiE(_M0L4selfS509);
  _M0L3lenS2425 = _M0L4selfS509->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS510
  = _M0FPB23array__growth__capacity(_M0L8old__capS508, _M0L3lenS2425, _M0L8requiredS511);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS509, _M0L8new__capS510);
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
  moonbit_string_t* _M0L6_2aoldS5466;
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
  _M0L6_2aoldS5466 = _M0L4selfS473->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5466);
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
  struct _M0TUsiE** _M0L6_2aoldS5467;
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
  _M0L6_2aoldS5467 = _M0L4selfS479->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5467);
  _M0L4selfS479->$0 = _M0L8new__bufS483;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS485,
  int32_t _M0L13new__capacityS488
) {
  float* _M0L8old__bufS484;
  int32_t _M0L3lenS486;
  int32_t _M0L9copy__lenS487;
  float* _M0L8new__bufS489;
  float* _M0L6_2aoldS5468;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS484, _M0L13new__capacityS488, _M0L9copy__lenS487, 0, 0);
  _M0L6_2aoldS5468 = _M0L4selfS485->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5468);
  _M0L4selfS485->$0 = _M0L8new__bufS489;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS491,
  int32_t _M0L13new__capacityS494
) {
  int32_t* _M0L8old__bufS490;
  int32_t _M0L3lenS492;
  int32_t _M0L9copy__lenS493;
  int32_t* _M0L8new__bufS495;
  int32_t* _M0L6_2aoldS5469;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS490, _M0L13new__capacityS494, _M0L9copy__lenS493, 0, 0);
  _M0L6_2aoldS5469 = _M0L4selfS491->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5469);
  _M0L4selfS491->$0 = _M0L8new__bufS495;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS468
) {
  moonbit_string_t* _M0L6_2atmpS2418;
  int32_t _result_5890;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2418 = _M0MPC15array5Array6bufferGsE(_M0L4selfS468);
  _result_5890 = Moonbit_array_length(_M0L6_2atmpS2418);
  moonbit_decref_cycle_free(_M0L6_2atmpS2418);
  return _result_5890;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS469
) {
  struct _M0TUsiE** _M0L6_2atmpS2419;
  int32_t _result_5891;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2419 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS469);
  _result_5891 = Moonbit_array_length(_M0L6_2atmpS2419);
  moonbit_decref_cycle_free(_M0L6_2atmpS2419);
  return _result_5891;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS470
) {
  float* _M0L6_2atmpS2420;
  int32_t _result_5892;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2420 = _M0MPC15array5Array6bufferGfE(_M0L4selfS470);
  _result_5892 = Moonbit_array_length(_M0L6_2atmpS2420);
  moonbit_decref_cycle_free(_M0L6_2atmpS2420);
  return _result_5892;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS471
) {
  int32_t* _M0L6_2atmpS2421;
  int32_t _result_5893;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2421 = _M0MPC15array5Array6bufferGiE(_M0L4selfS471);
  _result_5893 = Moonbit_array_length(_M0L6_2atmpS2421);
  moonbit_decref_cycle_free(_M0L6_2atmpS2421);
  return _result_5893;
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

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS458) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS458->$1;
}

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE* _M0L4selfS459) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS459->$1;
}

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE* _M0L4selfS460) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS460->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS452) {
  float* _M0L8_2afieldS5470;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5470 = _M0L4selfS452->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5470);
  return _M0L8_2afieldS5470;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS453
) {
  moonbit_string_t* _M0L8_2afieldS5471;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5471 = _M0L4selfS453->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5471);
  return _M0L8_2afieldS5471;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS454
) {
  struct _M0TUsiE** _M0L8_2afieldS5472;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5472 = _M0L4selfS454->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5472);
  return _M0L8_2afieldS5472;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L4selfS455
) {
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L8_2afieldS5473;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5473 = _M0L4selfS455->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5473);
  return _M0L8_2afieldS5473;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS456) {
  int32_t* _M0L8_2afieldS5474;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5474 = _M0L4selfS456->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5474);
  return _M0L8_2afieldS5474;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS457) {
  uint8_t* _M0L8_2afieldS5475;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5475 = _M0L4selfS457->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5475);
  return _M0L8_2afieldS5475;
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
  int32_t _M0L3endS2416;
  int32_t _M0L5startS2417;
  int32_t _M0L8str__lenS447;
  int32_t _M0L3lenS2415;
  int32_t _M0L8requiredS449;
  uint16_t* _M0L4dataS2408;
  int32_t _M0L6_2atmpS2407;
  int32_t _if__result_5895;
  uint16_t* _M0L4dataS2409;
  int32_t _M0L3lenS2410;
  moonbit_string_t _M0L6_2atmpS2411;
  int32_t _M0L6_2atmpS2412;
  int32_t _M0L3lenS2414;
  int32_t _M0L6_2atmpS2413;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS2416 = _M0L3strS448.$2;
  _M0L5startS2417 = _M0L3strS448.$1;
  _M0L8str__lenS447 = _M0L3endS2416 - _M0L5startS2417;
  if (_M0L8str__lenS447 == 0) {
    return 0;
  }
  _M0L3lenS2415 = _M0L4selfS450->$1;
  _M0L8requiredS449 = _M0L3lenS2415 + _M0L8str__lenS447;
  _M0L4dataS2408 = _M0L4selfS450->$0;
  _M0L6_2atmpS2407 = Moonbit_array_length(_M0L4dataS2408);
  if (_M0L8requiredS449 > _M0L6_2atmpS2407) {
    _if__result_5895 = 1;
  } else {
    int32_t _M0L3lenS2406 = _M0L4selfS450->$1;
    _if__result_5895 = _M0L8requiredS449 < _M0L3lenS2406;
  }
  if (_if__result_5895) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS450, _M0L8requiredS449);
  }
  _M0L4dataS2409 = _M0L4selfS450->$0;
  _M0L3lenS2410 = _M0L4selfS450->$1;
  moonbit_incref_cycle_free(_M0L4dataS2409);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2411 = _M0MPC16string10StringView4data(_M0L3strS448);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2412 = _M0MPC16string10StringView13start__offset(_M0L3strS448);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS2409, _M0L3lenS2410, _M0L6_2atmpS2411, _M0L6_2atmpS2412, _M0L8str__lenS447);
  moonbit_decref_cycle_free(_M0L4dataS2409);
  moonbit_decref_cycle_free(_M0L6_2atmpS2411);
  _M0L3lenS2414 = _M0L4selfS450->$1;
  _M0L6_2atmpS2413 = _M0L3lenS2414 + _M0L8str__lenS447;
  _M0L4selfS450->$1 = _M0L6_2atmpS2413;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS444,
  int32_t _M0L5startS442,
  int32_t _M0L3endS443
) {
  int32_t _if__result_5896;
  int32_t _M0L3lenS445;
  int32_t _M0L6_2atmpS2405;
  moonbit_bytes_t _M0L5bytesS446;
  moonbit_bytes_t _M0L6_2atmpS2404;
  moonbit_string_t _result_5897;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS442 == 0) {
    int32_t _M0L6_2atmpS2403 = Moonbit_array_length(_M0L3strS444);
    _if__result_5896 = _M0L3endS443 == _M0L6_2atmpS2403;
  } else {
    _if__result_5896 = 0;
  }
  if (_if__result_5896) {
    moonbit_incref_cycle_free(_M0L3strS444);
    return _M0L3strS444;
  }
  _M0L3lenS445 = _M0L3endS443 - _M0L5startS442;
  _M0L6_2atmpS2405 = _M0L3lenS445 * 2;
  _M0L5bytesS446 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS2405, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS446, 0, _M0L3strS444, _M0L5startS442, _M0L3lenS445);
  _M0L6_2atmpS2404 = _M0L5bytesS446;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_5897
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS2404, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS2404);
  return _result_5897;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS437,
  int32_t _M0L6offsetS441,
  int64_t _M0L6lengthS439
) {
  int32_t _M0L3lenS436;
  int32_t _M0L6lengthS438;
  int32_t _if__result_5898;
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
      int32_t _M0L6_2atmpS2402 = _M0L6offsetS441 + _M0L6lengthS438;
      _if__result_5898 = _M0L6_2atmpS2402 <= _M0L3lenS436;
    } else {
      _if__result_5898 = 0;
    }
  } else {
    _if__result_5898 = 0;
  }
  if (_if__result_5898) {
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
  int32_t _M0L6_2atmpS2401;
  int32_t _M0L6_2atmpS2400;
  int32_t _M0L2e1S422;
  int32_t _M0L6_2atmpS2399;
  int32_t _M0L2e2S425;
  int32_t _M0L4len1S427;
  int32_t _M0L4len2S429;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS2401 = _M0L6lengthS424 * 2;
  _M0L6_2atmpS2400 = _M0L13bytes__offsetS423 + _M0L6_2atmpS2401;
  _M0L2e1S422 = _M0L6_2atmpS2400 - 1;
  _M0L6_2atmpS2399 = _M0L11str__offsetS426 + _M0L6lengthS424;
  _M0L2e2S425 = _M0L6_2atmpS2399 - 1;
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
        int32_t _M0L6_2atmpS2396 = _M0L3strS430[_M0L1iS432];
        int32_t _M0L6_2atmpS2395 = (int32_t)_M0L6_2atmpS2396;
        uint32_t _M0L1cS434 = *(uint32_t*)&_M0L6_2atmpS2395;
        uint32_t _M0L6_2atmpS2391 = _M0L1cS434 & 255u;
        int32_t _M0L6_2atmpS2390;
        int32_t _M0L6_2atmpS2392;
        uint32_t _M0L6_2atmpS2394;
        int32_t _M0L6_2atmpS2393;
        int32_t _M0L6_2atmpS2397;
        int32_t _M0L6_2atmpS2398;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS2390 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS2391);
        if (
          _M0L1jS433 < 0 || _M0L1jS433 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L1jS433] = _M0L6_2atmpS2390;
        _M0L6_2atmpS2392 = _M0L1jS433 + 1;
        _M0L6_2atmpS2394 = _M0L1cS434 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS2393 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS2394);
        if (
          _M0L6_2atmpS2392 < 0
          || _M0L6_2atmpS2392 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L6_2atmpS2392] = _M0L6_2atmpS2393;
        _M0L6_2atmpS2397 = _M0L1iS432 + 1;
        _M0L6_2atmpS2398 = _M0L1jS433 + 2;
        _M0L1iS432 = _M0L6_2atmpS2397;
        _M0L1jS433 = _M0L6_2atmpS2398;
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
  int32_t _M0L6_2atmpS2389;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2389 = *(int32_t*)&_M0L4selfS421;
  return _M0L6_2atmpS2389 & 0xff;
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
    int64_t _M0L6_2atmpS2388 = -_M0L4selfS396;
    _M0L3numS398 = *(uint64_t*)&_M0L6_2atmpS2388;
  } else {
    _M0L3numS398 = *(uint64_t*)&_M0L4selfS396;
  }
  switch (_M0L5radixS395) {
    case 10: {
      int32_t _M0L10digit__lenS400;
      int32_t _M0L6_2atmpS2385;
      int32_t _M0L10total__lenS401;
      uint16_t* _M0L6bufferS402;
      int32_t _M0L12digit__startS403;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS400 = _M0FPB12dec__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS2385 = 1;
      } else {
        _M0L6_2atmpS2385 = 0;
      }
      _M0L10total__lenS401 = _M0L10digit__lenS400 + _M0L6_2atmpS2385;
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
      int32_t _M0L6_2atmpS2386;
      int32_t _M0L10total__lenS405;
      uint16_t* _M0L6bufferS406;
      int32_t _M0L12digit__startS407;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS404 = _M0FPB12hex__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS2386 = 1;
      } else {
        _M0L6_2atmpS2386 = 0;
      }
      _M0L10total__lenS405 = _M0L10digit__lenS404 + _M0L6_2atmpS2386;
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
      int32_t _M0L6_2atmpS2387;
      int32_t _M0L10total__lenS409;
      uint16_t* _M0L6bufferS410;
      int32_t _M0L12digit__startS411;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS408
      = _M0FPB14radix__count64(_M0L3numS398, _M0L5radixS395);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS2387 = 1;
      } else {
        _M0L6_2atmpS2387 = 0;
      }
      _M0L10total__lenS409 = _M0L10digit__lenS408 + _M0L6_2atmpS2387;
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
  int32_t _M0L6_2atmpS2384;
  uint64_t _M0L3numS371;
  int32_t _M0L6offsetS372;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2384 = _M0L10total__lenS394 - _M0L12digit__startS382;
  _M0L3numS371 = _M0L3numS393;
  _M0L6offsetS372 = _M0L6_2atmpS2384;
  while (1) {
    if (_M0L3numS371 >= 10000ull) {
      uint64_t _M0L1tS373 = _M0L3numS371 / 10000ull;
      uint64_t _M0L6_2atmpS2361 = _M0L3numS371 % 10000ull;
      int32_t _M0L1rS374 = (int32_t)_M0L6_2atmpS2361;
      int32_t _M0L2d1S375 = _M0L1rS374 / 100;
      int32_t _M0L2d2S376 = _M0L1rS374 % 100;
      int32_t _M0L6_2atmpS2360 = _M0L2d1S375 / 10;
      int32_t _M0L6_2atmpS2359 = 48 + _M0L6_2atmpS2360;
      int32_t _M0L6d1__hiS377 = (uint16_t)_M0L6_2atmpS2359;
      int32_t _M0L6_2atmpS2358 = _M0L2d1S375 % 10;
      int32_t _M0L6_2atmpS2357 = 48 + _M0L6_2atmpS2358;
      int32_t _M0L6d1__loS378 = (uint16_t)_M0L6_2atmpS2357;
      int32_t _M0L6_2atmpS2356 = _M0L2d2S376 / 10;
      int32_t _M0L6_2atmpS2355 = 48 + _M0L6_2atmpS2356;
      int32_t _M0L6d2__hiS379 = (uint16_t)_M0L6_2atmpS2355;
      int32_t _M0L6_2atmpS2354 = _M0L2d2S376 % 10;
      int32_t _M0L6_2atmpS2353 = 48 + _M0L6_2atmpS2354;
      int32_t _M0L6d2__loS380 = (uint16_t)_M0L6_2atmpS2353;
      int32_t _M0L6_2atmpS2345 = _M0L12digit__startS382 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS2344 = _M0L6_2atmpS2345 - 4;
      int32_t _M0L6_2atmpS2347;
      int32_t _M0L6_2atmpS2346;
      int32_t _M0L6_2atmpS2349;
      int32_t _M0L6_2atmpS2348;
      int32_t _M0L6_2atmpS2351;
      int32_t _M0L6_2atmpS2350;
      int32_t _M0L6_2atmpS2352;
      _M0L6bufferS381[_M0L6_2atmpS2344] = _M0L6d1__hiS377;
      _M0L6_2atmpS2347 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS2346 = _M0L6_2atmpS2347 - 3;
      _M0L6bufferS381[_M0L6_2atmpS2346] = _M0L6d1__loS378;
      _M0L6_2atmpS2349 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS2348 = _M0L6_2atmpS2349 - 2;
      _M0L6bufferS381[_M0L6_2atmpS2348] = _M0L6d2__hiS379;
      _M0L6_2atmpS2351 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS2350 = _M0L6_2atmpS2351 - 1;
      _M0L6bufferS381[_M0L6_2atmpS2350] = _M0L6d2__loS380;
      _M0L6_2atmpS2352 = _M0L6offsetS372 - 4;
      _M0L3numS371 = _M0L1tS373;
      _M0L6offsetS372 = _M0L6_2atmpS2352;
      continue;
    } else {
      int32_t _M0L6_2atmpS2383 = (int32_t)_M0L3numS371;
      int32_t _M0L9remainingS384 = _M0L6_2atmpS2383;
      int32_t _M0L6offsetS385 = _M0L6offsetS372;
      while (1) {
        if (_M0L9remainingS384 >= 100) {
          int32_t _M0L1tS386 = _M0L9remainingS384 / 100;
          int32_t _M0L1dS387 = _M0L9remainingS384 % 100;
          int32_t _M0L6_2atmpS2370 = _M0L1dS387 / 10;
          int32_t _M0L6_2atmpS2369 = 48 + _M0L6_2atmpS2370;
          int32_t _M0L5d__hiS388 = (uint16_t)_M0L6_2atmpS2369;
          int32_t _M0L6_2atmpS2368 = _M0L1dS387 % 10;
          int32_t _M0L6_2atmpS2367 = 48 + _M0L6_2atmpS2368;
          int32_t _M0L5d__loS389 = (uint16_t)_M0L6_2atmpS2367;
          int32_t _M0L6_2atmpS2363 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS2362 = _M0L6_2atmpS2363 - 2;
          int32_t _M0L6_2atmpS2365;
          int32_t _M0L6_2atmpS2364;
          int32_t _M0L6_2atmpS2366;
          _M0L6bufferS381[_M0L6_2atmpS2362] = _M0L5d__hiS388;
          _M0L6_2atmpS2365 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS2364 = _M0L6_2atmpS2365 - 1;
          _M0L6bufferS381[_M0L6_2atmpS2364] = _M0L5d__loS389;
          _M0L6_2atmpS2366 = _M0L6offsetS385 - 2;
          _M0L9remainingS384 = _M0L1tS386;
          _M0L6offsetS385 = _M0L6_2atmpS2366;
          continue;
        } else if (_M0L9remainingS384 >= 10) {
          int32_t _M0L6_2atmpS2378 = _M0L9remainingS384 / 10;
          int32_t _M0L6_2atmpS2377 = 48 + _M0L6_2atmpS2378;
          int32_t _M0L5d__hiS391 = (uint16_t)_M0L6_2atmpS2377;
          int32_t _M0L6_2atmpS2376 = _M0L9remainingS384 % 10;
          int32_t _M0L6_2atmpS2375 = 48 + _M0L6_2atmpS2376;
          int32_t _M0L5d__loS392 = (uint16_t)_M0L6_2atmpS2375;
          int32_t _M0L6_2atmpS2372 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS2371 = _M0L6_2atmpS2372 - 2;
          int32_t _M0L6_2atmpS2374;
          int32_t _M0L6_2atmpS2373;
          _M0L6bufferS381[_M0L6_2atmpS2371] = _M0L5d__hiS391;
          _M0L6_2atmpS2374 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS2373 = _M0L6_2atmpS2374 - 1;
          _M0L6bufferS381[_M0L6_2atmpS2373] = _M0L5d__loS392;
        } else {
          int32_t _M0L6_2atmpS2382 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS2379 = _M0L6_2atmpS2382 - 1;
          int32_t _M0L6_2atmpS2381 = 48 + _M0L9remainingS384;
          int32_t _M0L6_2atmpS2380 = (uint16_t)_M0L6_2atmpS2381;
          _M0L6bufferS381[_M0L6_2atmpS2379] = _M0L6_2atmpS2380;
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
  int32_t _M0L6_2atmpS2329;
  int32_t _M0L6_2atmpS2328;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS354 = _M0MPC13int3Int10to__uint64(_M0L5radixS355);
  _M0L6_2atmpS2329 = _M0L5radixS355 - 1;
  _M0L6_2atmpS2328 = _M0L5radixS355 & _M0L6_2atmpS2329;
  if (_M0L6_2atmpS2328 == 0) {
    int32_t _M0L5shiftS356;
    uint64_t _M0L4maskS357;
    int32_t _M0L6_2atmpS2336;
    int32_t _M0L6offsetS358;
    uint64_t _M0L1nS359;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS356 = moonbit_ctz32(_M0L5radixS355);
    _M0L4maskS357 = _M0L4baseS354 - 1ull;
    _M0L6_2atmpS2336 = _M0L10total__lenS364 - _M0L12digit__startS362;
    _M0L6offsetS358 = _M0L6_2atmpS2336;
    _M0L1nS359 = _M0L3numS365;
    while (1) {
      if (_M0L1nS359 > 0ull) {
        uint64_t _M0L6_2atmpS2335 = _M0L1nS359 & _M0L4maskS357;
        int32_t _M0L5digitS360 = (int32_t)_M0L6_2atmpS2335;
        int32_t _M0L6_2atmpS2332 = _M0L12digit__startS362 + _M0L6offsetS358;
        int32_t _M0L6_2atmpS2330 = _M0L6_2atmpS2332 - 1;
        int32_t _M0L6_2atmpS2331 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS360];
        int32_t _M0L6_2atmpS2333;
        uint64_t _M0L6_2atmpS2334;
        _M0L6bufferS361[_M0L6_2atmpS2330] = _M0L6_2atmpS2331;
        _M0L6_2atmpS2333 = _M0L6offsetS358 - 1;
        _M0L6_2atmpS2334 = _M0L1nS359 >> (_M0L5shiftS356 & 63);
        _M0L6offsetS358 = _M0L6_2atmpS2333;
        _M0L1nS359 = _M0L6_2atmpS2334;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2343 = _M0L10total__lenS364 - _M0L12digit__startS362;
    int32_t _M0L6offsetS366 = _M0L6_2atmpS2343;
    uint64_t _M0L1nS367 = _M0L3numS365;
    while (1) {
      if (_M0L1nS367 > 0ull) {
        uint64_t _M0L1qS368 = _M0L1nS367 / _M0L4baseS354;
        uint64_t _M0L6_2atmpS2342 = _M0L1qS368 * _M0L4baseS354;
        uint64_t _M0L6_2atmpS2341 = _M0L1nS367 - _M0L6_2atmpS2342;
        int32_t _M0L5digitS369 = (int32_t)_M0L6_2atmpS2341;
        int32_t _M0L6_2atmpS2339 = _M0L12digit__startS362 + _M0L6offsetS366;
        int32_t _M0L6_2atmpS2337 = _M0L6_2atmpS2339 - 1;
        int32_t _M0L6_2atmpS2338 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS369];
        int32_t _M0L6_2atmpS2340;
        _M0L6bufferS361[_M0L6_2atmpS2337] = _M0L6_2atmpS2338;
        _M0L6_2atmpS2340 = _M0L6offsetS366 - 1;
        _M0L6offsetS366 = _M0L6_2atmpS2340;
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
  int32_t _M0L6_2atmpS2327;
  int32_t _M0L6offsetS343;
  uint64_t _M0L1nS344;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2327 = _M0L10total__lenS352 - _M0L12digit__startS349;
  _M0L6offsetS343 = _M0L6_2atmpS2327;
  _M0L1nS344 = _M0L3numS353;
  while (1) {
    if (_M0L6offsetS343 >= 2) {
      uint64_t _M0L6_2atmpS2324 = _M0L1nS344 & 255ull;
      int32_t _M0L9byte__valS345 = (int32_t)_M0L6_2atmpS2324;
      int32_t _M0L2hiS346 = _M0L9byte__valS345 / 16;
      int32_t _M0L2loS347 = _M0L9byte__valS345 % 16;
      int32_t _M0L6_2atmpS2318 = _M0L12digit__startS349 + _M0L6offsetS343;
      int32_t _M0L6_2atmpS2316 = _M0L6_2atmpS2318 - 2;
      int32_t _M0L6_2atmpS2317 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L2hiS346];
      int32_t _M0L6_2atmpS2321;
      int32_t _M0L6_2atmpS2319;
      int32_t _M0L6_2atmpS2320;
      int32_t _M0L6_2atmpS2322;
      uint64_t _M0L6_2atmpS2323;
      _M0L6bufferS348[_M0L6_2atmpS2316] = _M0L6_2atmpS2317;
      _M0L6_2atmpS2321 = _M0L12digit__startS349 + _M0L6offsetS343;
      _M0L6_2atmpS2319 = _M0L6_2atmpS2321 - 1;
      _M0L6_2atmpS2320
      = ((moonbit_string_t)moonbit_string_literal_20.data)[
        _M0L2loS347
      ];
      _M0L6bufferS348[_M0L6_2atmpS2319] = _M0L6_2atmpS2320;
      _M0L6_2atmpS2322 = _M0L6offsetS343 - 2;
      _M0L6_2atmpS2323 = _M0L1nS344 >> 8;
      _M0L6offsetS343 = _M0L6_2atmpS2322;
      _M0L1nS344 = _M0L6_2atmpS2323;
      continue;
    } else if (_M0L6offsetS343 == 1) {
      uint64_t _M0L6_2atmpS2326 = _M0L1nS344 & 15ull;
      int32_t _M0L6nibbleS351 = (int32_t)_M0L6_2atmpS2326;
      int32_t _M0L6_2atmpS2325 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L6nibbleS351];
      _M0L6bufferS348[_M0L12digit__startS349] = _M0L6_2atmpS2325;
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
      uint64_t _M0L6_2atmpS2314 = _M0L3numS340 / _M0L4baseS338;
      int32_t _M0L6_2atmpS2315 = _M0L5countS341 + 1;
      _M0L3numS340 = _M0L6_2atmpS2314;
      _M0L5countS341 = _M0L6_2atmpS2315;
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
    int32_t _M0L6_2atmpS2313;
    int32_t _M0L6_2atmpS2312;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS336 = moonbit_clz64(_M0L5valueS335);
    _M0L6_2atmpS2313 = 63 - _M0L14leading__zerosS336;
    _M0L6_2atmpS2312 = _M0L6_2atmpS2313 / 4;
    return _M0L6_2atmpS2312 + 1;
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
    int32_t _M0L6_2atmpS2311 = -_M0L4selfS318;
    _M0L3numS320 = *(uint32_t*)&_M0L6_2atmpS2311;
  } else {
    _M0L3numS320 = *(uint32_t*)&_M0L4selfS318;
  }
  switch (_M0L5radixS317) {
    case 10: {
      int32_t _M0L10digit__lenS322;
      int32_t _M0L6_2atmpS2308;
      int32_t _M0L10total__lenS323;
      uint16_t* _M0L6bufferS324;
      int32_t _M0L12digit__startS325;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS322 = _M0FPB12dec__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS2308 = 1;
      } else {
        _M0L6_2atmpS2308 = 0;
      }
      _M0L10total__lenS323 = _M0L10digit__lenS322 + _M0L6_2atmpS2308;
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
      int32_t _M0L6_2atmpS2309;
      int32_t _M0L10total__lenS327;
      uint16_t* _M0L6bufferS328;
      int32_t _M0L12digit__startS329;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS326 = _M0FPB12hex__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS2309 = 1;
      } else {
        _M0L6_2atmpS2309 = 0;
      }
      _M0L10total__lenS327 = _M0L10digit__lenS326 + _M0L6_2atmpS2309;
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
      int32_t _M0L6_2atmpS2310;
      int32_t _M0L10total__lenS331;
      uint16_t* _M0L6bufferS332;
      int32_t _M0L12digit__startS333;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS330
      = _M0FPB14radix__count32(_M0L3numS320, _M0L5radixS317);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS2310 = 1;
      } else {
        _M0L6_2atmpS2310 = 0;
      }
      _M0L10total__lenS331 = _M0L10digit__lenS330 + _M0L6_2atmpS2310;
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
      uint32_t _M0L6_2atmpS2306 = _M0L3numS314 / _M0L4baseS312;
      int32_t _M0L6_2atmpS2307 = _M0L5countS315 + 1;
      _M0L3numS314 = _M0L6_2atmpS2306;
      _M0L5countS315 = _M0L6_2atmpS2307;
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
    int32_t _M0L6_2atmpS2305;
    int32_t _M0L6_2atmpS2304;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS310 = moonbit_clz32(_M0L5valueS309);
    _M0L6_2atmpS2305 = 31 - _M0L14leading__zerosS310;
    _M0L6_2atmpS2304 = _M0L6_2atmpS2305 / 4;
    return _M0L6_2atmpS2304 + 1;
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
  int32_t _M0L6_2atmpS2303;
  uint32_t _M0L3numS284;
  int32_t _M0L6offsetS285;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2303 = _M0L10total__lenS307 - _M0L12digit__startS295;
  _M0L3numS284 = _M0L3numS306;
  _M0L6offsetS285 = _M0L6_2atmpS2303;
  while (1) {
    if (_M0L3numS284 >= 10000u) {
      uint32_t _M0L1tS286 = _M0L3numS284 / 10000u;
      uint32_t _M0L6_2atmpS2280 = _M0L3numS284 % 10000u;
      int32_t _M0L1rS287 = *(int32_t*)&_M0L6_2atmpS2280;
      int32_t _M0L2d1S288 = _M0L1rS287 / 100;
      int32_t _M0L2d2S289 = _M0L1rS287 % 100;
      int32_t _M0L6_2atmpS2279 = _M0L2d1S288 / 10;
      int32_t _M0L6_2atmpS2278 = 48 + _M0L6_2atmpS2279;
      int32_t _M0L6d1__hiS290 = (uint16_t)_M0L6_2atmpS2278;
      int32_t _M0L6_2atmpS2277 = _M0L2d1S288 % 10;
      int32_t _M0L6_2atmpS2276 = 48 + _M0L6_2atmpS2277;
      int32_t _M0L6d1__loS291 = (uint16_t)_M0L6_2atmpS2276;
      int32_t _M0L6_2atmpS2275 = _M0L2d2S289 / 10;
      int32_t _M0L6_2atmpS2274 = 48 + _M0L6_2atmpS2275;
      int32_t _M0L6d2__hiS292 = (uint16_t)_M0L6_2atmpS2274;
      int32_t _M0L6_2atmpS2273 = _M0L2d2S289 % 10;
      int32_t _M0L6_2atmpS2272 = 48 + _M0L6_2atmpS2273;
      int32_t _M0L6d2__loS293 = (uint16_t)_M0L6_2atmpS2272;
      int32_t _M0L6_2atmpS2264 = _M0L12digit__startS295 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS2263 = _M0L6_2atmpS2264 - 4;
      int32_t _M0L6_2atmpS2266;
      int32_t _M0L6_2atmpS2265;
      int32_t _M0L6_2atmpS2268;
      int32_t _M0L6_2atmpS2267;
      int32_t _M0L6_2atmpS2270;
      int32_t _M0L6_2atmpS2269;
      int32_t _M0L6_2atmpS2271;
      _M0L6bufferS294[_M0L6_2atmpS2263] = _M0L6d1__hiS290;
      _M0L6_2atmpS2266 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS2265 = _M0L6_2atmpS2266 - 3;
      _M0L6bufferS294[_M0L6_2atmpS2265] = _M0L6d1__loS291;
      _M0L6_2atmpS2268 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS2267 = _M0L6_2atmpS2268 - 2;
      _M0L6bufferS294[_M0L6_2atmpS2267] = _M0L6d2__hiS292;
      _M0L6_2atmpS2270 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS2269 = _M0L6_2atmpS2270 - 1;
      _M0L6bufferS294[_M0L6_2atmpS2269] = _M0L6d2__loS293;
      _M0L6_2atmpS2271 = _M0L6offsetS285 - 4;
      _M0L3numS284 = _M0L1tS286;
      _M0L6offsetS285 = _M0L6_2atmpS2271;
      continue;
    } else {
      int32_t _M0L6_2atmpS2302 = *(int32_t*)&_M0L3numS284;
      int32_t _M0L9remainingS297 = _M0L6_2atmpS2302;
      int32_t _M0L6offsetS298 = _M0L6offsetS285;
      while (1) {
        if (_M0L9remainingS297 >= 100) {
          int32_t _M0L1tS299 = _M0L9remainingS297 / 100;
          int32_t _M0L1dS300 = _M0L9remainingS297 % 100;
          int32_t _M0L6_2atmpS2289 = _M0L1dS300 / 10;
          int32_t _M0L6_2atmpS2288 = 48 + _M0L6_2atmpS2289;
          int32_t _M0L5d__hiS301 = (uint16_t)_M0L6_2atmpS2288;
          int32_t _M0L6_2atmpS2287 = _M0L1dS300 % 10;
          int32_t _M0L6_2atmpS2286 = 48 + _M0L6_2atmpS2287;
          int32_t _M0L5d__loS302 = (uint16_t)_M0L6_2atmpS2286;
          int32_t _M0L6_2atmpS2282 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS2281 = _M0L6_2atmpS2282 - 2;
          int32_t _M0L6_2atmpS2284;
          int32_t _M0L6_2atmpS2283;
          int32_t _M0L6_2atmpS2285;
          _M0L6bufferS294[_M0L6_2atmpS2281] = _M0L5d__hiS301;
          _M0L6_2atmpS2284 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS2283 = _M0L6_2atmpS2284 - 1;
          _M0L6bufferS294[_M0L6_2atmpS2283] = _M0L5d__loS302;
          _M0L6_2atmpS2285 = _M0L6offsetS298 - 2;
          _M0L9remainingS297 = _M0L1tS299;
          _M0L6offsetS298 = _M0L6_2atmpS2285;
          continue;
        } else if (_M0L9remainingS297 >= 10) {
          int32_t _M0L6_2atmpS2297 = _M0L9remainingS297 / 10;
          int32_t _M0L6_2atmpS2296 = 48 + _M0L6_2atmpS2297;
          int32_t _M0L5d__hiS304 = (uint16_t)_M0L6_2atmpS2296;
          int32_t _M0L6_2atmpS2295 = _M0L9remainingS297 % 10;
          int32_t _M0L6_2atmpS2294 = 48 + _M0L6_2atmpS2295;
          int32_t _M0L5d__loS305 = (uint16_t)_M0L6_2atmpS2294;
          int32_t _M0L6_2atmpS2291 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS2290 = _M0L6_2atmpS2291 - 2;
          int32_t _M0L6_2atmpS2293;
          int32_t _M0L6_2atmpS2292;
          _M0L6bufferS294[_M0L6_2atmpS2290] = _M0L5d__hiS304;
          _M0L6_2atmpS2293 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS2292 = _M0L6_2atmpS2293 - 1;
          _M0L6bufferS294[_M0L6_2atmpS2292] = _M0L5d__loS305;
        } else {
          int32_t _M0L6_2atmpS2301 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS2298 = _M0L6_2atmpS2301 - 1;
          int32_t _M0L6_2atmpS2300 = 48 + _M0L9remainingS297;
          int32_t _M0L6_2atmpS2299 = (uint16_t)_M0L6_2atmpS2300;
          _M0L6bufferS294[_M0L6_2atmpS2298] = _M0L6_2atmpS2299;
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
  int32_t _M0L6_2atmpS2248;
  int32_t _M0L6_2atmpS2247;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS267 = *(uint32_t*)&_M0L5radixS268;
  _M0L6_2atmpS2248 = _M0L5radixS268 - 1;
  _M0L6_2atmpS2247 = _M0L5radixS268 & _M0L6_2atmpS2248;
  if (_M0L6_2atmpS2247 == 0) {
    int32_t _M0L5shiftS269;
    uint32_t _M0L4maskS270;
    int32_t _M0L6_2atmpS2255;
    int32_t _M0L6offsetS271;
    uint32_t _M0L1nS272;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS269 = moonbit_ctz32(_M0L5radixS268);
    _M0L4maskS270 = _M0L4baseS267 - 1u;
    _M0L6_2atmpS2255 = _M0L10total__lenS277 - _M0L12digit__startS275;
    _M0L6offsetS271 = _M0L6_2atmpS2255;
    _M0L1nS272 = _M0L3numS278;
    while (1) {
      if (_M0L1nS272 > 0u) {
        uint32_t _M0L6_2atmpS2254 = _M0L1nS272 & _M0L4maskS270;
        int32_t _M0L5digitS273 = *(int32_t*)&_M0L6_2atmpS2254;
        int32_t _M0L6_2atmpS2251 = _M0L12digit__startS275 + _M0L6offsetS271;
        int32_t _M0L6_2atmpS2249 = _M0L6_2atmpS2251 - 1;
        int32_t _M0L6_2atmpS2250 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS273];
        int32_t _M0L6_2atmpS2252;
        uint32_t _M0L6_2atmpS2253;
        _M0L6bufferS274[_M0L6_2atmpS2249] = _M0L6_2atmpS2250;
        _M0L6_2atmpS2252 = _M0L6offsetS271 - 1;
        _M0L6_2atmpS2253 = _M0L1nS272 >> (_M0L5shiftS269 & 31);
        _M0L6offsetS271 = _M0L6_2atmpS2252;
        _M0L1nS272 = _M0L6_2atmpS2253;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2262 = _M0L10total__lenS277 - _M0L12digit__startS275;
    int32_t _M0L6offsetS279 = _M0L6_2atmpS2262;
    uint32_t _M0L1nS280 = _M0L3numS278;
    while (1) {
      if (_M0L1nS280 > 0u) {
        uint32_t _M0L1qS281 = _M0L1nS280 / _M0L4baseS267;
        uint32_t _M0L6_2atmpS2261 = _M0L1qS281 * _M0L4baseS267;
        uint32_t _M0L6_2atmpS2260 = _M0L1nS280 - _M0L6_2atmpS2261;
        int32_t _M0L5digitS282 = *(int32_t*)&_M0L6_2atmpS2260;
        int32_t _M0L6_2atmpS2258 = _M0L12digit__startS275 + _M0L6offsetS279;
        int32_t _M0L6_2atmpS2256 = _M0L6_2atmpS2258 - 1;
        int32_t _M0L6_2atmpS2257 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS282];
        int32_t _M0L6_2atmpS2259;
        _M0L6bufferS274[_M0L6_2atmpS2256] = _M0L6_2atmpS2257;
        _M0L6_2atmpS2259 = _M0L6offsetS279 - 1;
        _M0L6offsetS279 = _M0L6_2atmpS2259;
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
  int32_t _M0L6_2atmpS2246;
  int32_t _M0L6offsetS256;
  uint32_t _M0L1nS257;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2246 = _M0L10total__lenS265 - _M0L12digit__startS262;
  _M0L6offsetS256 = _M0L6_2atmpS2246;
  _M0L1nS257 = _M0L3numS266;
  while (1) {
    if (_M0L6offsetS256 >= 2) {
      uint32_t _M0L6_2atmpS2243 = _M0L1nS257 & 255u;
      int32_t _M0L9byte__valS258 = *(int32_t*)&_M0L6_2atmpS2243;
      int32_t _M0L2hiS259 = _M0L9byte__valS258 / 16;
      int32_t _M0L2loS260 = _M0L9byte__valS258 % 16;
      int32_t _M0L6_2atmpS2237 = _M0L12digit__startS262 + _M0L6offsetS256;
      int32_t _M0L6_2atmpS2235 = _M0L6_2atmpS2237 - 2;
      int32_t _M0L6_2atmpS2236 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L2hiS259];
      int32_t _M0L6_2atmpS2240;
      int32_t _M0L6_2atmpS2238;
      int32_t _M0L6_2atmpS2239;
      int32_t _M0L6_2atmpS2241;
      uint32_t _M0L6_2atmpS2242;
      _M0L6bufferS261[_M0L6_2atmpS2235] = _M0L6_2atmpS2236;
      _M0L6_2atmpS2240 = _M0L12digit__startS262 + _M0L6offsetS256;
      _M0L6_2atmpS2238 = _M0L6_2atmpS2240 - 1;
      _M0L6_2atmpS2239
      = ((moonbit_string_t)moonbit_string_literal_20.data)[
        _M0L2loS260
      ];
      _M0L6bufferS261[_M0L6_2atmpS2238] = _M0L6_2atmpS2239;
      _M0L6_2atmpS2241 = _M0L6offsetS256 - 2;
      _M0L6_2atmpS2242 = _M0L1nS257 >> 8;
      _M0L6offsetS256 = _M0L6_2atmpS2241;
      _M0L1nS257 = _M0L6_2atmpS2242;
      continue;
    } else if (_M0L6offsetS256 == 1) {
      uint32_t _M0L6_2atmpS2245 = _M0L1nS257 & 15u;
      int32_t _M0L6nibbleS264 = *(int32_t*)&_M0L6_2atmpS2245;
      int32_t _M0L6_2atmpS2244 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L6nibbleS264];
      _M0L6bufferS261[_M0L12digit__startS262] = _M0L6_2atmpS2244;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS255
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS254;
  struct _M0TPB6Logger _M0L6_2atmpS2234;
  moonbit_string_t _result_5912;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS254);
  _M0L6_2atmpS2234
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS254
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS255, _M0L6_2atmpS2234);
  if (_M0L6_2atmpS2234.$1) {
    moonbit_decref(_M0L6_2atmpS2234.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_5912 = _M0MPB13StringBuilder10to__string(_M0L6loggerS254);
  moonbit_decref_cycle_free(_M0L6loggerS254);
  return _result_5912;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS249,
  struct _M0TPB6Logger _M0L6loggerS248
) {
  moonbit_string_t _M0L6_2atmpS2231;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2231 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS249);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248.$0->$method_0(_M0L6loggerS248.$1, _M0L6_2atmpS2231);
  moonbit_decref_cycle_free(_M0L6_2atmpS2231);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS2232;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2232 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS2232);
  moonbit_decref_cycle_free(_M0L6_2atmpS2232);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS2233;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2233 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS2233);
  moonbit_decref_cycle_free(_M0L6_2atmpS2233);
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
  moonbit_string_t _M0L8_2afieldS5476;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS5476 = _M0L4selfS246.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5476);
  return _M0L8_2afieldS5476;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS242,
  moonbit_string_t _M0L5valueS243,
  int32_t _M0L5startS244,
  int32_t _M0L3lenS245
) {
  int32_t _M0L6_2atmpS2230;
  int64_t _M0L6_2atmpS2229;
  struct _M0TPC16string10StringView _M0L6_2atmpS2228;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2230 = _M0L5startS244 + _M0L3lenS245;
  _M0L6_2atmpS2229 = (int64_t)_M0L6_2atmpS2230;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2228
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS243, _M0L5startS244, _M0L6_2atmpS2229);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS242, _M0L6_2atmpS2228);
  moonbit_decref_cycle_free(_M0L6_2atmpS2228.$0);
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
  int32_t _M0L6_2atmpS2212;
  int32_t _if__result_5913;
  int32_t _M0L6_2atmpS2220;
  int32_t _if__result_5914;
  int32_t _M0L6_2atmpS2222;
  int32_t _M0L6_2atmpS2223;
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
  _M0L6_2atmpS2212 = _M0Lm2loS236;
  if (_M0L6_2atmpS2212 > 0) {
    int32_t _M0L6_2atmpS2211 = _M0Lm2loS236;
    if (_M0L6_2atmpS2211 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS2210 = _M0Lm2loS236;
      int32_t _M0L6_2atmpS2209 = _M0L4selfS235[_M0L6_2atmpS2210];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2209)) {
        int32_t _M0L6_2atmpS2208 = _M0Lm2loS236;
        int32_t _M0L6_2atmpS2207 = _M0L6_2atmpS2208 - 1;
        int32_t _M0L6_2atmpS2206 = _M0L4selfS235[_M0L6_2atmpS2207];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_5913
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2206);
      } else {
        _if__result_5913 = 0;
      }
    } else {
      _if__result_5913 = 0;
    }
  } else {
    _if__result_5913 = 0;
  }
  if (_if__result_5913) {
    int32_t _M0L6_2atmpS2213 = _M0Lm2loS236;
    _M0Lm2loS236 = _M0L6_2atmpS2213 + 1;
  }
  _M0L6_2atmpS2220 = _M0Lm2hiS238;
  if (_M0L6_2atmpS2220 > 0) {
    int32_t _M0L6_2atmpS2219 = _M0Lm2hiS238;
    if (_M0L6_2atmpS2219 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS2218 = _M0Lm2hiS238;
      int32_t _M0L6_2atmpS2217 = _M0L4selfS235[_M0L6_2atmpS2218];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2217)) {
        int32_t _M0L6_2atmpS2216 = _M0Lm2hiS238;
        int32_t _M0L6_2atmpS2215 = _M0L6_2atmpS2216 - 1;
        int32_t _M0L6_2atmpS2214 = _M0L4selfS235[_M0L6_2atmpS2215];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_5914
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2214);
      } else {
        _if__result_5914 = 0;
      }
    } else {
      _if__result_5914 = 0;
    }
  } else {
    _if__result_5914 = 0;
  }
  if (_if__result_5914) {
    int32_t _M0L6_2atmpS2221 = _M0Lm2hiS238;
    _M0Lm2hiS238 = _M0L6_2atmpS2221 - 1;
  }
  _M0L6_2atmpS2222 = _M0Lm2loS236;
  _M0L6_2atmpS2223 = _M0Lm2hiS238;
  if (_M0L6_2atmpS2222 >= _M0L6_2atmpS2223) {
    int32_t _M0L6_2atmpS2224 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS2225 = _M0Lm2loS236;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS2224,
                                                 .$2 = _M0L6_2atmpS2225};
  } else {
    int32_t _M0L6_2atmpS2226 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS2227 = _M0Lm2hiS238;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS2226,
                                                 .$2 = _M0L6_2atmpS2227};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS2205;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS2205
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS2205);
  if (_M0L6_2atmpS2205.$1) {
    moonbit_decref(_M0L6_2atmpS2205.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS231,
  struct _M0TPB4Show _M0L4showS230
) {
  struct _M0TPB6Logger _M0L6_2atmpS2204;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS231);
  _M0L6_2atmpS2204
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS231
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS230.$0->$method_0(_M0L4showS230.$1, _M0L6_2atmpS2204);
  if (_M0L6_2atmpS2204.$1) {
    moonbit_decref(_M0L6_2atmpS2204.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS229) {
  int64_t _M0L6_2atmpS2203;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2203 = (int64_t)_M0L4selfS229;
  return *(uint64_t*)&_M0L6_2atmpS2203;
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
  int32_t _M0L6_2atmpS2202;
  struct _M0TPC16string10StringView _M0L6_2atmpS2200;
  struct _M0TPB6Logger _M0L6_2atmpS2201;
  moonbit_string_t _result_5915;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS2202 = Moonbit_array_length(_M0L4selfS227);
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS2200
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS227, .$1 = 0, .$2 = _M0L6_2atmpS2202
  };
  moonbit_incref_cycle_free(_M0L3bufS226);
  _M0L6_2atmpS2201
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS226
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS2200, _M0L6_2atmpS2201, _M0L5quoteS228);
  moonbit_decref_cycle_free(_M0L6_2atmpS2200.$0);
  if (_M0L6_2atmpS2201.$1) {
    moonbit_decref(_M0L6_2atmpS2201.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_5915 = _M0MPB13StringBuilder10to__string(_M0L3bufS226);
  moonbit_decref_cycle_free(_M0L3bufS226);
  return _result_5915;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS218,
  struct _M0TPB6Logger _M0L6loggerS216,
  int32_t _M0L5quoteS215
) {
  int32_t _M0L3endS2198;
  int32_t _M0L5startS2199;
  int32_t _M0L3lenS217;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS219;
  int32_t _M0L1iS220;
  int32_t _M0L3segS221;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS215) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  _M0L3endS2198 = _M0L4selfS218.$2;
  _M0L5startS2199 = _M0L4selfS218.$1;
  _M0L3lenS217 = _M0L3endS2198 - _M0L5startS2199;
  moonbit_incref_cycle_free(_M0L4selfS218.$0);
  if (_M0L6loggerS216.$1) {
    moonbit_incref(_M0L6loggerS216.$1);
  }
  _M0L6_2aenvS219
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 88, 0);
  _M0L6_2aenvS219->$0 = _M0L4selfS218;
  _M0L6_2aenvS219->$1 = _M0L6loggerS216;
  _M0L1iS220 = 0;
  _M0L3segS221 = 0;
  _2afor_222:;
  while (1) {
    moonbit_string_t _M0L3strS2195;
    int32_t _M0L5startS2197;
    int32_t _M0L6_2atmpS2196;
    int32_t _M0L4codeS223;
    int32_t _M0L1cS225;
    int32_t _M0L6_2atmpS2179;
    int32_t _M0L6_2atmpS2180;
    int32_t _M0L6_2atmpS2181;
    if (_M0L1iS220 >= _M0L3lenS217) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
      moonbit_decref_cycle_free(_M0L6_2aenvS219);
      break;
    }
    _M0L3strS2195 = _M0L4selfS218.$0;
    _M0L5startS2197 = _M0L4selfS218.$1;
    _M0L6_2atmpS2196 = _M0L5startS2197 + _M0L1iS220;
    _M0L4codeS223 = _M0L3strS2195[_M0L6_2atmpS2196];
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
        int32_t _M0L6_2atmpS2182;
        int32_t _M0L6_2atmpS2183;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS2182 = _M0L1iS220 + 1;
        _M0L6_2atmpS2183 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS2182;
        _M0L3segS221 = _M0L6_2atmpS2183;
        goto _2afor_222;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS2184;
        int32_t _M0L6_2atmpS2185;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_22.data);
        _M0L6_2atmpS2184 = _M0L1iS220 + 1;
        _M0L6_2atmpS2185 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS2184;
        _M0L3segS221 = _M0L6_2atmpS2185;
        goto _2afor_222;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS2186;
        int32_t _M0L6_2atmpS2187;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_23.data);
        _M0L6_2atmpS2186 = _M0L1iS220 + 1;
        _M0L6_2atmpS2187 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS2186;
        _M0L3segS221 = _M0L6_2atmpS2187;
        goto _2afor_222;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS2188;
        int32_t _M0L6_2atmpS2189;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_24.data);
        _M0L6_2atmpS2188 = _M0L1iS220 + 1;
        _M0L6_2atmpS2189 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS2188;
        _M0L3segS221 = _M0L6_2atmpS2189;
        goto _2afor_222;
        break;
      }
      default: {
        if (_M0L4codeS223 < 32) {
          int32_t _M0L6_2atmpS2191;
          moonbit_string_t _M0L6_2atmpS2190;
          int32_t _M0L6_2atmpS2192;
          int32_t _M0L6_2atmpS2193;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_25.data);
          _M0L6_2atmpS2191 = _M0L4codeS223 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS2190 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS2191);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, _M0L6_2atmpS2190);
          moonbit_decref_cycle_free(_M0L6_2atmpS2190);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS2192 = _M0L1iS220 + 1;
          _M0L6_2atmpS2193 = _M0L1iS220 + 1;
          _M0L1iS220 = _M0L6_2atmpS2192;
          _M0L3segS221 = _M0L6_2atmpS2193;
          goto _2afor_222;
        } else {
          int32_t _M0L6_2atmpS2194 = _M0L1iS220 + 1;
          int32_t _tmp_5918 = _M0L3segS221;
          _M0L1iS220 = _M0L6_2atmpS2194;
          _M0L3segS221 = _tmp_5918;
          goto _2afor_222;
        }
        break;
      }
    }
    goto joinlet_5917;
    join_224:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2179 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS225);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, _M0L6_2atmpS2179);
    _M0L6_2atmpS2180 = _M0L1iS220 + 1;
    _M0L6_2atmpS2181 = _M0L1iS220 + 1;
    _M0L1iS220 = _M0L6_2atmpS2180;
    _M0L3segS221 = _M0L6_2atmpS2181;
    continue;
    joinlet_5917:;
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
    int64_t _M0L6_2atmpS2178 = (int64_t)_M0L1iS213;
    struct _M0TPC16string10StringView _M0L6_2atmpS2177;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2177
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS212, _M0L3segS214, _M0L6_2atmpS2178);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS210.$0->$method_2(_M0L6loggerS210.$1, _M0L6_2atmpS2177);
    moonbit_decref_cycle_free(_M0L6_2atmpS2177.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS201,
  int32_t _M0L5startS203,
  int64_t _M0L3endS205
) {
  int32_t _M0L3endS2175;
  int32_t _M0L5startS2176;
  int32_t _M0L3lenS200;
  int32_t _M0Lm2loS202;
  int32_t _M0Lm2hiS204;
  moonbit_string_t _M0L3strS208;
  int32_t _M0L4baseS209;
  int32_t _M0L6_2atmpS2153;
  int32_t _if__result_5919;
  int32_t _M0L6_2atmpS2163;
  int32_t _if__result_5920;
  int32_t _M0L6_2atmpS2165;
  int32_t _M0L6_2atmpS2166;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS2175 = _M0L4selfS201.$2;
  _M0L5startS2176 = _M0L4selfS201.$1;
  _M0L3lenS200 = _M0L3endS2175 - _M0L5startS2176;
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
  _M0L6_2atmpS2153 = _M0Lm2loS202;
  if (_M0L6_2atmpS2153 > 0) {
    int32_t _M0L6_2atmpS2152 = _M0Lm2loS202;
    if (_M0L6_2atmpS2152 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS2151 = _M0Lm2loS202;
      int32_t _M0L6_2atmpS2150 = _M0L4baseS209 + _M0L6_2atmpS2151;
      int32_t _M0L6_2atmpS2149 = _M0L3strS208[_M0L6_2atmpS2150];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2149)) {
        int32_t _M0L6_2atmpS2148 = _M0Lm2loS202;
        int32_t _M0L6_2atmpS2147 = _M0L4baseS209 + _M0L6_2atmpS2148;
        int32_t _M0L6_2atmpS2146 = _M0L6_2atmpS2147 - 1;
        int32_t _M0L6_2atmpS2145 = _M0L3strS208[_M0L6_2atmpS2146];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_5919
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2145);
      } else {
        _if__result_5919 = 0;
      }
    } else {
      _if__result_5919 = 0;
    }
  } else {
    _if__result_5919 = 0;
  }
  if (_if__result_5919) {
    int32_t _M0L6_2atmpS2154 = _M0Lm2loS202;
    _M0Lm2loS202 = _M0L6_2atmpS2154 + 1;
  }
  _M0L6_2atmpS2163 = _M0Lm2hiS204;
  if (_M0L6_2atmpS2163 > 0) {
    int32_t _M0L6_2atmpS2162 = _M0Lm2hiS204;
    if (_M0L6_2atmpS2162 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS2161 = _M0Lm2hiS204;
      int32_t _M0L6_2atmpS2160 = _M0L4baseS209 + _M0L6_2atmpS2161;
      int32_t _M0L6_2atmpS2159 = _M0L3strS208[_M0L6_2atmpS2160];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2159)) {
        int32_t _M0L6_2atmpS2158 = _M0Lm2hiS204;
        int32_t _M0L6_2atmpS2157 = _M0L4baseS209 + _M0L6_2atmpS2158;
        int32_t _M0L6_2atmpS2156 = _M0L6_2atmpS2157 - 1;
        int32_t _M0L6_2atmpS2155 = _M0L3strS208[_M0L6_2atmpS2156];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_5920
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2155);
      } else {
        _if__result_5920 = 0;
      }
    } else {
      _if__result_5920 = 0;
    }
  } else {
    _if__result_5920 = 0;
  }
  if (_if__result_5920) {
    int32_t _M0L6_2atmpS2164 = _M0Lm2hiS204;
    _M0Lm2hiS204 = _M0L6_2atmpS2164 - 1;
  }
  _M0L6_2atmpS2165 = _M0Lm2loS202;
  _M0L6_2atmpS2166 = _M0Lm2hiS204;
  if (_M0L6_2atmpS2165 >= _M0L6_2atmpS2166) {
    int32_t _M0L6_2atmpS2170 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS2167 = _M0L4baseS209 + _M0L6_2atmpS2170;
    int32_t _M0L6_2atmpS2169 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS2168 = _M0L4baseS209 + _M0L6_2atmpS2169;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS2167,
                                                 .$2 = _M0L6_2atmpS2168};
  } else {
    int32_t _M0L6_2atmpS2174 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS2171 = _M0L4baseS209 + _M0L6_2atmpS2174;
    int32_t _M0L6_2atmpS2173 = _M0Lm2hiS204;
    int32_t _M0L6_2atmpS2172 = _M0L4baseS209 + _M0L6_2atmpS2173;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS2171,
                                                 .$2 = _M0L6_2atmpS2172};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS199) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS198;
  int32_t _M0L6_2atmpS2142;
  int32_t _M0L6_2atmpS2141;
  int32_t _M0L6_2atmpS2144;
  int32_t _M0L6_2atmpS2143;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS2140;
  moonbit_string_t _result_5921;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2142 = _M0IPC14byte4BytePB3Div3div(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2141
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS2142);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS2141);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2144 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2143
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS2144);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS2143);
  _M0L6_2atmpS2140 = _M0L7_2aselfS198;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_5921 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS2140);
  moonbit_decref_cycle_free(_M0L6_2atmpS2140);
  return _result_5921;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS197) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS197 < 10) {
    int32_t _M0L6_2atmpS2137;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2137 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS2137);
  } else {
    int32_t _M0L6_2atmpS2139;
    int32_t _M0L6_2atmpS2138;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2139 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2138 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS2139, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS2138);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS2135;
  int32_t _M0L6_2atmpS2136;
  int32_t _M0L6_2atmpS2134;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2135 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS2136 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS2134 = _M0L6_2atmpS2135 - _M0L6_2atmpS2136;
  return _M0L6_2atmpS2134 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS2132;
  int32_t _M0L6_2atmpS2133;
  int32_t _M0L6_2atmpS2131;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2132 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS2133 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS2131 = _M0L6_2atmpS2132 % _M0L6_2atmpS2133;
  return _M0L6_2atmpS2131 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS2129;
  int32_t _M0L6_2atmpS2130;
  int32_t _M0L6_2atmpS2128;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2129 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS2130 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS2128 = _M0L6_2atmpS2129 / _M0L6_2atmpS2130;
  return _M0L6_2atmpS2128 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS189,
  int32_t _M0L4thatS190
) {
  int32_t _M0L6_2atmpS2126;
  int32_t _M0L6_2atmpS2127;
  int32_t _M0L6_2atmpS2125;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2126 = (int32_t)_M0L4selfS189;
  _M0L6_2atmpS2127 = (int32_t)_M0L4thatS190;
  _M0L6_2atmpS2125 = _M0L6_2atmpS2126 + _M0L6_2atmpS2127;
  return _M0L6_2atmpS2125 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS188) {
  int32_t _M0L6_2atmpS2124;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS2124 = (int32_t)_M0L4selfS188;
  return _M0L6_2atmpS2124;
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
  int32_t _M0L3lenS2123;
  int32_t _M0L8requiredS184;
  uint16_t* _M0L4dataS2118;
  int32_t _M0L6_2atmpS2117;
  int32_t _if__result_5922;
  uint16_t* _M0L4dataS2119;
  int32_t _M0L3lenS2120;
  int32_t _M0L3lenS2122;
  int32_t _M0L6_2atmpS2121;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS182 = Moonbit_array_length(_M0L3strS183);
  if (_M0L8str__lenS182 == 0) {
    return 0;
  }
  _M0L3lenS2123 = _M0L4selfS185->$1;
  _M0L8requiredS184 = _M0L3lenS2123 + _M0L8str__lenS182;
  _M0L4dataS2118 = _M0L4selfS185->$0;
  _M0L6_2atmpS2117 = Moonbit_array_length(_M0L4dataS2118);
  if (_M0L8requiredS184 > _M0L6_2atmpS2117) {
    _if__result_5922 = 1;
  } else {
    int32_t _M0L3lenS2116 = _M0L4selfS185->$1;
    _if__result_5922 = _M0L8requiredS184 < _M0L3lenS2116;
  }
  if (_if__result_5922) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS185, _M0L8requiredS184);
  }
  _M0L4dataS2119 = _M0L4selfS185->$0;
  _M0L3lenS2120 = _M0L4selfS185->$1;
  moonbit_incref_cycle_free(_M0L4dataS2119);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS2119, _M0L3lenS2120, _M0L3strS183, 0, _M0L8str__lenS182);
  moonbit_decref_cycle_free(_M0L4dataS2119);
  _M0L3lenS2122 = _M0L4selfS185->$1;
  _M0L6_2atmpS2121 = _M0L3lenS2122 + _M0L8str__lenS182;
  _M0L4selfS185->$1 = _M0L6_2atmpS2121;
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
      int32_t _M0L6_2atmpS2113 = _M0L3strS179[_M0L1iS176];
      int32_t _M0L6_2atmpS2114;
      int32_t _M0L6_2atmpS2115;
      _M0L4selfS178[_M0L1jS177] = _M0L6_2atmpS2113;
      _M0L6_2atmpS2114 = _M0L1iS176 + 1;
      _M0L6_2atmpS2115 = _M0L1jS177 + 1;
      _M0L1iS176 = _M0L6_2atmpS2114;
      _M0L1jS177 = _M0L6_2atmpS2115;
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
    int32_t _M0L3lenS2084 = _M0L4selfS171->$1;
    uint16_t* _M0L4dataS2086 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS2085 = Moonbit_array_length(_M0L4dataS2086);
    uint16_t* _M0L4dataS2089;
    int32_t _M0L3lenS2090;
    int32_t _M0L6_2atmpS2091;
    int32_t _M0L3lenS2093;
    int32_t _M0L6_2atmpS2092;
    if (_M0L3lenS2084 >= _M0L6_2atmpS2085) {
      int32_t _M0L3lenS2088 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS2087 = _M0L3lenS2088 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS2087);
    }
    _M0L4dataS2089 = _M0L4selfS171->$0;
    _M0L3lenS2090 = _M0L4selfS171->$1;
    moonbit_incref_cycle_free(_M0L4dataS2089);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS2091 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS169);
    if (
      _M0L3lenS2090 < 0
      || _M0L3lenS2090 >= Moonbit_array_length(_M0L4dataS2089)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS2089[_M0L3lenS2090] = _M0L6_2atmpS2091;
    moonbit_decref_cycle_free(_M0L4dataS2089);
    _M0L3lenS2093 = _M0L4selfS171->$1;
    _M0L6_2atmpS2092 = _M0L3lenS2093 + 1;
    _M0L4selfS171->$1 = _M0L6_2atmpS2092;
  } else if (_M0L4codeS169 <= 1114111u) {
    uint16_t* _M0L4dataS2097 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS2095 = Moonbit_array_length(_M0L4dataS2097);
    int32_t _M0L3lenS2096 = _M0L4selfS171->$1;
    int32_t _M0L6_2atmpS2094 = _M0L6_2atmpS2095 - _M0L3lenS2096;
    uint32_t _M0L4codeS172;
    uint16_t* _M0L4dataS2100;
    int32_t _M0L3lenS2101;
    uint32_t _M0L6_2atmpS2104;
    uint32_t _M0L6_2atmpS2103;
    int32_t _M0L6_2atmpS2102;
    uint16_t* _M0L4dataS2105;
    int32_t _M0L3lenS2110;
    int32_t _M0L6_2atmpS2106;
    uint32_t _M0L6_2atmpS2109;
    uint32_t _M0L6_2atmpS2108;
    int32_t _M0L6_2atmpS2107;
    int32_t _M0L3lenS2112;
    int32_t _M0L6_2atmpS2111;
    if (_M0L6_2atmpS2094 < 2) {
      int32_t _M0L3lenS2099 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS2098 = _M0L3lenS2099 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS2098);
    }
    _M0L4codeS172 = _M0L4codeS169 - 65536u;
    _M0L4dataS2100 = _M0L4selfS171->$0;
    _M0L3lenS2101 = _M0L4selfS171->$1;
    _M0L6_2atmpS2104 = _M0L4codeS172 >> 10;
    _M0L6_2atmpS2103 = 55296u + _M0L6_2atmpS2104;
    moonbit_incref_cycle_free(_M0L4dataS2100);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS2102 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS2103);
    if (
      _M0L3lenS2101 < 0
      || _M0L3lenS2101 >= Moonbit_array_length(_M0L4dataS2100)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS2100[_M0L3lenS2101] = _M0L6_2atmpS2102;
    moonbit_decref_cycle_free(_M0L4dataS2100);
    _M0L4dataS2105 = _M0L4selfS171->$0;
    _M0L3lenS2110 = _M0L4selfS171->$1;
    _M0L6_2atmpS2106 = _M0L3lenS2110 + 1;
    _M0L6_2atmpS2109 = _M0L4codeS172 & 1023u;
    _M0L6_2atmpS2108 = 56320u + _M0L6_2atmpS2109;
    moonbit_incref_cycle_free(_M0L4dataS2105);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS2107 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS2108);
    if (
      _M0L6_2atmpS2106 < 0
      || _M0L6_2atmpS2106 >= Moonbit_array_length(_M0L4dataS2105)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS2105[_M0L6_2atmpS2106] = _M0L6_2atmpS2107;
    moonbit_decref_cycle_free(_M0L4dataS2105);
    _M0L3lenS2112 = _M0L4selfS171->$1;
    _M0L6_2atmpS2111 = _M0L3lenS2112 + 2;
    _M0L4selfS171->$1 = _M0L6_2atmpS2111;
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
  uint16_t* _M0L4dataS2083;
  int32_t _M0L6_2atmpS2081;
  int32_t _M0L3lenS2082;
  int32_t _M0L13new__capacityS165;
  uint16_t* _M0L4dataS2078;
  int32_t _M0L6_2atmpS2079;
  int32_t _M0L3lenS2080;
  uint16_t* _M0L9new__dataS168;
  uint16_t* _M0L6_2aoldS5477;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS2083 = _M0L4selfS166->$0;
  _M0L6_2atmpS2081 = Moonbit_array_length(_M0L4dataS2083);
  _M0L3lenS2082 = _M0L4selfS166->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS165
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS2081, _M0L3lenS2082, _M0L8requiredS167);
  _M0L4dataS2078 = _M0L4selfS166->$0;
  moonbit_incref_cycle_free(_M0L4dataS2078);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2079 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS2080 = _M0L4selfS166->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS168
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS2078, _M0L13new__capacityS165, _M0L6_2atmpS2079, _M0L3lenS2080, 0, 0);
  _M0L6_2aoldS5477 = _M0L4selfS166->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5477);
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
  int32_t _M0L6_2atmpS2077;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2077 = *(int32_t*)&_M0L4selfS158;
  return (uint16_t)_M0L6_2atmpS2077;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS157) {
  int32_t _M0L6_2atmpS2076;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2076 = _M0L4selfS157;
  return *(uint32_t*)&_M0L6_2atmpS2076;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS155
) {
  int32_t _M0L3lenS2067;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS2067 = _M0L4selfS155->$1;
  if (_M0L3lenS2067 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS2068 = _M0L4selfS155->$1;
    uint16_t* _M0L4dataS2070 = _M0L4selfS155->$0;
    int32_t _M0L6_2atmpS2069 = Moonbit_array_length(_M0L4dataS2070);
    if (_M0L3lenS2068 == _M0L6_2atmpS2069) {
      uint16_t* _M0L4dataS2071 = _M0L4selfS155->$0;
      moonbit_incref_cycle_free(_M0L4dataS2071);
      return _M0L4dataS2071;
    } else {
      uint16_t* _M0L4dataS2072 = _M0L4selfS155->$0;
      int32_t _M0L3lenS2073 = _M0L4selfS155->$1;
      int32_t _M0L6_2atmpS2074;
      int32_t _M0L3lenS2075;
      uint16_t* _M0L4dataS156;
      moonbit_incref_cycle_free(_M0L4dataS2072);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS2074 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS2075 = _M0L4selfS155->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS156
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS2072, _M0L3lenS2073, _M0L6_2atmpS2074, _M0L3lenS2075, 0, 0);
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
  int32_t _if__result_5925;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS148 >= 0) {
    if (_M0L3lenS149 >= 0) {
      if (_M0L11src__offsetS150 >= 0) {
        if (_M0L11dst__offsetS151 >= 0) {
          int32_t _M0L6_2atmpS2063 = _M0L11src__offsetS150 + _M0L3lenS149;
          int32_t _M0L6_2atmpS2064 = Moonbit_array_length(_M0L3srcS152);
          if (_M0L6_2atmpS2063 <= _M0L6_2atmpS2064) {
            int32_t _M0L6_2atmpS2062 = _M0L11dst__offsetS151 + _M0L3lenS149;
            _if__result_5925 = _M0L6_2atmpS2062 <= _M0L13allocate__lenS148;
          } else {
            _if__result_5925 = 0;
          }
        } else {
          _if__result_5925 = 0;
        }
      } else {
        _if__result_5925 = 0;
      }
    } else {
      _if__result_5925 = 0;
    }
  } else {
    _if__result_5925 = 0;
  }
  if (_if__result_5925) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS152, _M0L13allocate__lenS148, _M0L4initS153, _M0L11src__offsetS150, _M0L11dst__offsetS151, _M0L3lenS149);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS154;
    int32_t _M0L6_2atmpS2066;
    moonbit_string_t _M0L6_2atmpS2065;
    uint16_t* _result_5926;
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
    _M0L6_2atmpS2066 = Moonbit_array_length(_M0L3srcS152);
    moonbit_decref_cycle_free(_M0L3srcS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L6_2atmpS2066);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS2065
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS154);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS154);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_5926 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS2065);
    moonbit_decref_cycle_free(_M0L6_2atmpS2065);
    return _result_5926;
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
  struct _M0TPB13StringBuilder* _block_5927;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS139 < 1) {
    _M0L7initialS138 = 1;
  } else {
    int32_t _M0L6_2atmpS2061 = _M0L10size__hintS139 + 1;
    _M0L7initialS138 = _M0L6_2atmpS2061 / 2;
  }
  _M0L4dataS140 = (uint16_t*)moonbit_make_string(_M0L7initialS138, 0);
  _block_5927
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_5927)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 93, 0);
  _block_5927->$0 = _M0L4dataS140;
  _block_5927->$1 = 0;
  return _block_5927;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS137) {
  int32_t _M0L6_2atmpS2060;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2060 = (int32_t)_M0L4selfS137;
  return _M0L6_2atmpS2060;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS117,
  int32_t _M0L13allocate__lenS113,
  int32_t _M0L3lenS114,
  int32_t _M0L11src__offsetS115,
  int32_t _M0L11dst__offsetS116
) {
  int32_t _if__result_5928;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS113 >= 0) {
    if (_M0L3lenS114 >= 0) {
      if (_M0L11src__offsetS115 >= 0) {
        if (_M0L11dst__offsetS116 >= 0) {
          int32_t _M0L6_2atmpS2041 = _M0L11src__offsetS115 + _M0L3lenS114;
          int32_t _M0L6_2atmpS2042;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2042
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS117);
          if (_M0L6_2atmpS2041 <= _M0L6_2atmpS2042) {
            int32_t _M0L6_2atmpS2040 = _M0L11dst__offsetS116 + _M0L3lenS114;
            _if__result_5928 = _M0L6_2atmpS2040 <= _M0L13allocate__lenS113;
          } else {
            _if__result_5928 = 0;
          }
        } else {
          _if__result_5928 = 0;
        }
      } else {
        _if__result_5928 = 0;
      }
    } else {
      _if__result_5928 = 0;
    }
  } else {
    _if__result_5928 = 0;
  }
  if (_if__result_5928) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS113, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS117, _M0L11src__offsetS115, _M0L11dst__offsetS116, _M0L3lenS114);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS118;
    int32_t _M0L6_2atmpS2044;
    moonbit_string_t _M0L6_2atmpS2043;
    moonbit_string_t* _result_5929;
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
    _M0L6_2atmpS2044 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS117);
    moonbit_decref_cycle_free(_M0L3srcS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L6_2atmpS2044);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2043
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS118);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS118);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_5929
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS2043);
    moonbit_decref_cycle_free(_M0L6_2atmpS2043);
    return _result_5929;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS123,
  int32_t _M0L13allocate__lenS119,
  int32_t _M0L3lenS120,
  int32_t _M0L11src__offsetS121,
  int32_t _M0L11dst__offsetS122
) {
  int32_t _if__result_5930;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS119 >= 0) {
    if (_M0L3lenS120 >= 0) {
      if (_M0L11src__offsetS121 >= 0) {
        if (_M0L11dst__offsetS122 >= 0) {
          int32_t _M0L6_2atmpS2046 = _M0L11src__offsetS121 + _M0L3lenS120;
          int32_t _M0L6_2atmpS2047;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2047
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS123);
          if (_M0L6_2atmpS2046 <= _M0L6_2atmpS2047) {
            int32_t _M0L6_2atmpS2045 = _M0L11dst__offsetS122 + _M0L3lenS120;
            _if__result_5930 = _M0L6_2atmpS2045 <= _M0L13allocate__lenS119;
          } else {
            _if__result_5930 = 0;
          }
        } else {
          _if__result_5930 = 0;
        }
      } else {
        _if__result_5930 = 0;
      }
    } else {
      _if__result_5930 = 0;
    }
  } else {
    _if__result_5930 = 0;
  }
  if (_if__result_5930) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS119, 0, _M0L3srcS123, _M0L11src__offsetS121, _M0L11dst__offsetS122, _M0L3lenS120);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS124;
    int32_t _M0L6_2atmpS2049;
    moonbit_string_t _M0L6_2atmpS2048;
    struct _M0TUsiE** _result_5931;
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
    _M0L6_2atmpS2049 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS123);
    moonbit_decref_cycle_free(_M0L3srcS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L6_2atmpS2049);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2048
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS124);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS124);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_5931
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS2048);
    moonbit_decref_cycle_free(_M0L6_2atmpS2048);
    return _result_5931;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS129,
  int32_t _M0L13allocate__lenS125,
  int32_t _M0L3lenS126,
  int32_t _M0L11src__offsetS127,
  int32_t _M0L11dst__offsetS128
) {
  int32_t _if__result_5932;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS125 >= 0) {
    if (_M0L3lenS126 >= 0) {
      if (_M0L11src__offsetS127 >= 0) {
        if (_M0L11dst__offsetS128 >= 0) {
          int32_t _M0L6_2atmpS2051 = _M0L11src__offsetS127 + _M0L3lenS126;
          int32_t _M0L6_2atmpS2052;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2052
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS129);
          if (_M0L6_2atmpS2051 <= _M0L6_2atmpS2052) {
            int32_t _M0L6_2atmpS2050 = _M0L11dst__offsetS128 + _M0L3lenS126;
            _if__result_5932 = _M0L6_2atmpS2050 <= _M0L13allocate__lenS125;
          } else {
            _if__result_5932 = 0;
          }
        } else {
          _if__result_5932 = 0;
        }
      } else {
        _if__result_5932 = 0;
      }
    } else {
      _if__result_5932 = 0;
    }
  } else {
    _if__result_5932 = 0;
  }
  if (_if__result_5932) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS129, _M0L13allocate__lenS125, _M0L11src__offsetS127, _M0L11dst__offsetS128, _M0L3lenS126);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS130;
    int32_t _M0L6_2atmpS2054;
    moonbit_string_t _M0L6_2atmpS2053;
    float* _result_5933;
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
    _M0L6_2atmpS2054 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS129);
    moonbit_decref_cycle_free(_M0L3srcS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L6_2atmpS2054);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2053
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS130);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS130);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_5933
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS2053);
    moonbit_decref_cycle_free(_M0L6_2atmpS2053);
    return _result_5933;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS135,
  int32_t _M0L13allocate__lenS131,
  int32_t _M0L3lenS132,
  int32_t _M0L11src__offsetS133,
  int32_t _M0L11dst__offsetS134
) {
  int32_t _if__result_5934;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS131 >= 0) {
    if (_M0L3lenS132 >= 0) {
      if (_M0L11src__offsetS133 >= 0) {
        if (_M0L11dst__offsetS134 >= 0) {
          int32_t _M0L6_2atmpS2056 = _M0L11src__offsetS133 + _M0L3lenS132;
          int32_t _M0L6_2atmpS2057;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2057
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS135);
          if (_M0L6_2atmpS2056 <= _M0L6_2atmpS2057) {
            int32_t _M0L6_2atmpS2055 = _M0L11dst__offsetS134 + _M0L3lenS132;
            _if__result_5934 = _M0L6_2atmpS2055 <= _M0L13allocate__lenS131;
          } else {
            _if__result_5934 = 0;
          }
        } else {
          _if__result_5934 = 0;
        }
      } else {
        _if__result_5934 = 0;
      }
    } else {
      _if__result_5934 = 0;
    }
  } else {
    _if__result_5934 = 0;
  }
  if (_if__result_5934) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS135, _M0L13allocate__lenS131, _M0L11src__offsetS133, _M0L11dst__offsetS134, _M0L3lenS132);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS136;
    int32_t _M0L6_2atmpS2059;
    moonbit_string_t _M0L6_2atmpS2058;
    int32_t* _result_5935;
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
    _M0L6_2atmpS2059 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS135);
    moonbit_decref_cycle_free(_M0L3srcS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L6_2atmpS2059);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2058
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS136);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS136);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_5935
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS2058);
    moonbit_decref_cycle_free(_M0L6_2atmpS2058);
    return _result_5935;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L3objS107
) {
  struct _M0TPB6Logger _M0L6_2atmpS2037;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS108);
  _M0L6_2atmpS2037
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS108
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS107, _M0L6_2atmpS2037);
  if (_M0L6_2atmpS2037.$1) {
    moonbit_decref(_M0L6_2atmpS2037.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS2038;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS2038
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS109, _M0L6_2atmpS2038);
  if (_M0L6_2atmpS2038.$1) {
    moonbit_decref(_M0L6_2atmpS2038.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  uint64_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS2039;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS2039
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS111, _M0L6_2atmpS2039);
  if (_M0L6_2atmpS2039.$1) {
    moonbit_decref(_M0L6_2atmpS2039.$1);
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

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS98,
  int32_t _M0L13allocate__lenS96,
  int32_t _M0L11src__offsetS99,
  int32_t _M0L11dst__offsetS97,
  int32_t _M0L9blit__lenS100
) {
  float* _M0L3dstS95;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS95 = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS96);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS95, _M0L11dst__offsetS97, _M0L3srcS98, _M0L11src__offsetS99, _M0L9blit__lenS100);
  moonbit_decref_cycle_free(_M0L3srcS98);
  return _M0L3dstS95;
}

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t* _M0L3srcS104,
  int32_t _M0L13allocate__lenS102,
  int32_t _M0L11src__offsetS105,
  int32_t _M0L11dst__offsetS103,
  int32_t _M0L9blit__lenS106
) {
  int32_t* _M0L3dstS101;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS101
  = (int32_t*)moonbit_make_int32_array_raw(_M0L13allocate__lenS102);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L3dstS101, _M0L11dst__offsetS103, _M0L3srcS104, _M0L11src__offsetS105, _M0L9blit__lenS106);
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS73,
  int32_t _M0L11dst__offsetS74,
  float* _M0L3srcS75,
  int32_t _M0L11src__offsetS76,
  int32_t _M0L3lenS77
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS75);
  moonbit_incref_cycle_free(_M0L3dstS73);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS73, _M0L11dst__offsetS74, _M0L3srcS75, _M0L11src__offsetS76, _M0L3lenS77, sizeof(float));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t* _M0L3dstS78,
  int32_t _M0L11dst__offsetS79,
  int32_t* _M0L3srcS80,
  int32_t _M0L11src__offsetS81,
  int32_t _M0L3lenS82
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS80);
  moonbit_incref_cycle_free(_M0L3dstS78);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS78, _M0L11dst__offsetS79, _M0L3srcS80, _M0L11src__offsetS81, _M0L3lenS82, sizeof(int32_t));
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
        int32_t _M0L6_2atmpS1992 = _M0L11dst__offsetS20 + _M0L1iS22;
        int32_t _M0L6_2atmpS1994 = _M0L11src__offsetS21 + _M0L1iS22;
        int32_t _M0L6_2atmpS1993;
        int32_t _M0L6_2atmpS1995;
        if (
          _M0L6_2atmpS1994 < 0
          || _M0L6_2atmpS1994 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1993 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1994];
        if (
          _M0L6_2atmpS1992 < 0
          || _M0L6_2atmpS1992 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1992] = _M0L6_2atmpS1993;
        _M0L6_2atmpS1995 = _M0L1iS22 + 1;
        _M0L1iS22 = _M0L6_2atmpS1995;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2000 = _M0L3lenS23 - 1;
    int32_t _M0L1iS25 = _M0L6_2atmpS2000;
    while (1) {
      if (_M0L1iS25 >= 0) {
        int32_t _M0L6_2atmpS1996 = _M0L11dst__offsetS20 + _M0L1iS25;
        int32_t _M0L6_2atmpS1998 = _M0L11src__offsetS21 + _M0L1iS25;
        int32_t _M0L6_2atmpS1997;
        int32_t _M0L6_2atmpS1999;
        if (
          _M0L6_2atmpS1998 < 0
          || _M0L6_2atmpS1998 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1997 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1998];
        if (
          _M0L6_2atmpS1996 < 0
          || _M0L6_2atmpS1996 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1996] = _M0L6_2atmpS1997;
        _M0L6_2atmpS1999 = _M0L1iS25 - 1;
        _M0L1iS25 = _M0L6_2atmpS1999;
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
        int32_t _M0L6_2atmpS2001 = _M0L11dst__offsetS29 + _M0L1iS31;
        int32_t _M0L6_2atmpS2003 = _M0L11src__offsetS30 + _M0L1iS31;
        moonbit_string_t _M0L6_2atmpS2002;
        moonbit_string_t _M0L6_2aoldS5478;
        int32_t _M0L6_2atmpS2004;
        if (
          _M0L6_2atmpS2003 < 0
          || _M0L6_2atmpS2003 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2002 = (moonbit_string_t)_M0L3srcS28[_M0L6_2atmpS2003];
        if (
          _M0L6_2atmpS2001 < 0
          || _M0L6_2atmpS2001 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5478 = (moonbit_string_t)_M0L3dstS27[_M0L6_2atmpS2001];
        moonbit_incref_cycle_free(_M0L6_2atmpS2002);
        moonbit_decref_cycle_free(_M0L6_2aoldS5478);
        _M0L3dstS27[_M0L6_2atmpS2001] = _M0L6_2atmpS2002;
        _M0L6_2atmpS2004 = _M0L1iS31 + 1;
        _M0L1iS31 = _M0L6_2atmpS2004;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2009 = _M0L3lenS32 - 1;
    int32_t _M0L1iS34 = _M0L6_2atmpS2009;
    while (1) {
      if (_M0L1iS34 >= 0) {
        int32_t _M0L6_2atmpS2005 = _M0L11dst__offsetS29 + _M0L1iS34;
        int32_t _M0L6_2atmpS2007 = _M0L11src__offsetS30 + _M0L1iS34;
        moonbit_string_t _M0L6_2atmpS2006;
        moonbit_string_t _M0L6_2aoldS5479;
        int32_t _M0L6_2atmpS2008;
        if (
          _M0L6_2atmpS2007 < 0
          || _M0L6_2atmpS2007 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2006 = (moonbit_string_t)_M0L3srcS28[_M0L6_2atmpS2007];
        if (
          _M0L6_2atmpS2005 < 0
          || _M0L6_2atmpS2005 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5479 = (moonbit_string_t)_M0L3dstS27[_M0L6_2atmpS2005];
        moonbit_incref_cycle_free(_M0L6_2atmpS2006);
        moonbit_decref_cycle_free(_M0L6_2aoldS5479);
        _M0L3dstS27[_M0L6_2atmpS2005] = _M0L6_2atmpS2006;
        _M0L6_2atmpS2008 = _M0L1iS34 - 1;
        _M0L1iS34 = _M0L6_2atmpS2008;
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
        int32_t _M0L6_2atmpS2010 = _M0L11dst__offsetS38 + _M0L1iS40;
        int32_t _M0L6_2atmpS2012 = _M0L11src__offsetS39 + _M0L1iS40;
        struct _M0TUsiE* _M0L6_2atmpS2011;
        struct _M0TUsiE* _M0L6_2aoldS5480;
        int32_t _M0L6_2atmpS2013;
        if (
          _M0L6_2atmpS2012 < 0
          || _M0L6_2atmpS2012 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2011 = (struct _M0TUsiE*)_M0L3srcS37[_M0L6_2atmpS2012];
        if (
          _M0L6_2atmpS2010 < 0
          || _M0L6_2atmpS2010 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5480 = (struct _M0TUsiE*)_M0L3dstS36[_M0L6_2atmpS2010];
        if (_M0L6_2atmpS2011) {
          moonbit_incref_cycle_free(_M0L6_2atmpS2011);
        }
        if (_M0L6_2aoldS5480) {
          moonbit_decref_cycle_free(_M0L6_2aoldS5480);
        }
        _M0L3dstS36[_M0L6_2atmpS2010] = _M0L6_2atmpS2011;
        _M0L6_2atmpS2013 = _M0L1iS40 + 1;
        _M0L1iS40 = _M0L6_2atmpS2013;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2018 = _M0L3lenS41 - 1;
    int32_t _M0L1iS43 = _M0L6_2atmpS2018;
    while (1) {
      if (_M0L1iS43 >= 0) {
        int32_t _M0L6_2atmpS2014 = _M0L11dst__offsetS38 + _M0L1iS43;
        int32_t _M0L6_2atmpS2016 = _M0L11src__offsetS39 + _M0L1iS43;
        struct _M0TUsiE* _M0L6_2atmpS2015;
        struct _M0TUsiE* _M0L6_2aoldS5481;
        int32_t _M0L6_2atmpS2017;
        if (
          _M0L6_2atmpS2016 < 0
          || _M0L6_2atmpS2016 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2015 = (struct _M0TUsiE*)_M0L3srcS37[_M0L6_2atmpS2016];
        if (
          _M0L6_2atmpS2014 < 0
          || _M0L6_2atmpS2014 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5481 = (struct _M0TUsiE*)_M0L3dstS36[_M0L6_2atmpS2014];
        if (_M0L6_2atmpS2015) {
          moonbit_incref_cycle_free(_M0L6_2atmpS2015);
        }
        if (_M0L6_2aoldS5481) {
          moonbit_decref_cycle_free(_M0L6_2aoldS5481);
        }
        _M0L3dstS36[_M0L6_2atmpS2014] = _M0L6_2atmpS2015;
        _M0L6_2atmpS2017 = _M0L1iS43 - 1;
        _M0L1iS43 = _M0L6_2atmpS2017;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS45,
  int32_t _M0L11dst__offsetS47,
  float* _M0L3srcS46,
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
        int32_t _M0L6_2atmpS2019 = _M0L11dst__offsetS47 + _M0L1iS49;
        int32_t _M0L6_2atmpS2021 = _M0L11src__offsetS48 + _M0L1iS49;
        float _M0L6_2atmpS2020;
        int32_t _M0L6_2atmpS2022;
        if (
          _M0L6_2atmpS2021 < 0
          || _M0L6_2atmpS2021 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2020 = (float)_M0L3srcS46[_M0L6_2atmpS2021];
        if (
          _M0L6_2atmpS2019 < 0
          || _M0L6_2atmpS2019 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS45[_M0L6_2atmpS2019] = _M0L6_2atmpS2020;
        _M0L6_2atmpS2022 = _M0L1iS49 + 1;
        _M0L1iS49 = _M0L6_2atmpS2022;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2027 = _M0L3lenS50 - 1;
    int32_t _M0L1iS52 = _M0L6_2atmpS2027;
    while (1) {
      if (_M0L1iS52 >= 0) {
        int32_t _M0L6_2atmpS2023 = _M0L11dst__offsetS47 + _M0L1iS52;
        int32_t _M0L6_2atmpS2025 = _M0L11src__offsetS48 + _M0L1iS52;
        float _M0L6_2atmpS2024;
        int32_t _M0L6_2atmpS2026;
        if (
          _M0L6_2atmpS2025 < 0
          || _M0L6_2atmpS2025 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2024 = (float)_M0L3srcS46[_M0L6_2atmpS2025];
        if (
          _M0L6_2atmpS2023 < 0
          || _M0L6_2atmpS2023 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS45[_M0L6_2atmpS2023] = _M0L6_2atmpS2024;
        _M0L6_2atmpS2026 = _M0L1iS52 - 1;
        _M0L1iS52 = _M0L6_2atmpS2026;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS54,
  int32_t _M0L11dst__offsetS56,
  int32_t* _M0L3srcS55,
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
        int32_t _M0L6_2atmpS2028 = _M0L11dst__offsetS56 + _M0L1iS58;
        int32_t _M0L6_2atmpS2030 = _M0L11src__offsetS57 + _M0L1iS58;
        int32_t _M0L6_2atmpS2029;
        int32_t _M0L6_2atmpS2031;
        if (
          _M0L6_2atmpS2030 < 0
          || _M0L6_2atmpS2030 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2029 = (int32_t)_M0L3srcS55[_M0L6_2atmpS2030];
        if (
          _M0L6_2atmpS2028 < 0
          || _M0L6_2atmpS2028 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS2028] = _M0L6_2atmpS2029;
        _M0L6_2atmpS2031 = _M0L1iS58 + 1;
        _M0L1iS58 = _M0L6_2atmpS2031;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2036 = _M0L3lenS59 - 1;
    int32_t _M0L1iS61 = _M0L6_2atmpS2036;
    while (1) {
      if (_M0L1iS61 >= 0) {
        int32_t _M0L6_2atmpS2032 = _M0L11dst__offsetS56 + _M0L1iS61;
        int32_t _M0L6_2atmpS2034 = _M0L11src__offsetS57 + _M0L1iS61;
        int32_t _M0L6_2atmpS2033;
        int32_t _M0L6_2atmpS2035;
        if (
          _M0L6_2atmpS2034 < 0
          || _M0L6_2atmpS2034 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2033 = (int32_t)_M0L3srcS55[_M0L6_2atmpS2034];
        if (
          _M0L6_2atmpS2032 < 0
          || _M0L6_2atmpS2032 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS2032] = _M0L6_2atmpS2033;
        _M0L6_2atmpS2035 = _M0L1iS61 - 1;
        _M0L1iS61 = _M0L6_2atmpS2035;
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

int32_t _M0MPB18UninitializedArray6lengthGfE(float* _M0L4selfS16) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS16);
}

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t* _M0L4selfS17) {
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

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(
  moonbit_string_t _M0L3msgS6
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS6);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(
  moonbit_string_t _M0L3msgS7
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS7);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1959) {
  switch (Moonbit_object_tag(_M0L4_2aeS1959)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_35.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1959);
      break;
    }
    
    case 3: {
      return (moonbit_string_t)moonbit_string_literal_36.data;
      break;
    }
    
    case 1: {
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
  void* _M0L11_2aobj__ptrS1987,
  struct _M0TPB4Show _M0L8_2aparamS1986
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1985 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1987;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1985, _M0L8_2aparamS1986);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1984,
  struct _M0TPB4Show _M0L8_2aparamS1983
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1982 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1984;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1982, _M0L8_2aparamS1983);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1981,
  int32_t _M0L8_2aparamS1980
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1979 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1981;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1979, _M0L8_2aparamS1980);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1978,
  struct _M0TPC16string10StringView _M0L8_2aparamS1977
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1976 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1978;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1976, _M0L8_2aparamS1977);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1975,
  moonbit_string_t _M0L8_2aparamS1972,
  int32_t _M0L8_2aparamS1973,
  int32_t _M0L8_2aparamS1974
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1971 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1975;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1971, _M0L8_2aparamS1972, _M0L8_2aparamS1973, _M0L8_2aparamS1974);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1970,
  moonbit_string_t _M0L8_2aparamS1969
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1968 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1970;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1968, _M0L8_2aparamS1969);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_5946 = 9218868437227405311ll;
  int64_t _tmp_5947;
  int64_t _tmp_5948;
  int64_t _tmp_5949;
  int64_t _tmp_5950;
  _M0FPB18double__max__value = *(double*)&_tmp_5946;
  _tmp_5947 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_5947;
  _tmp_5948 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_5948;
  _tmp_5949 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_5949;
  _tmp_5950 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_5950;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1991;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1952;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1953;
  int32_t _M0L7_2abindS1954;
  struct _M0TUsiE** _M0L7_2abindS1955;
  int32_t _M0L6_2acntS5658;
  int32_t _M0L2__S1956;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1991
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1952
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1952)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 96, 0);
  _M0L12async__testsS1952->$0 = _M0L6_2atmpS1991;
  _M0L12async__testsS1952->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1953
  = _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1954 = _M0L7_2abindS1953->$1;
  _M0L7_2abindS1955 = _M0L7_2abindS1953->$0;
  _M0L6_2acntS5658
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1953));
  if (_M0L6_2acntS5658 > 1) {
    int32_t _M0L11_2anew__cntS5659 = _M0L6_2acntS5658 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1953), _M0L11_2anew__cntS5659);
    moonbit_incref_cycle_free(_M0L7_2abindS1955);
  } else if (_M0L6_2acntS5658 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1953);
  }
  _M0L2__S1956 = 0;
  while (1) {
    if (_M0L2__S1956 < _M0L7_2abindS1954) {
      struct _M0TUsiE* _M0L3argS1957 =
        (struct _M0TUsiE*)_M0L7_2abindS1955[_M0L2__S1956];
      moonbit_string_t _M0L6_2atmpS1988 = _M0L3argS1957->$0;
      int32_t _M0L6_2atmpS1989 = _M0L3argS1957->$1;
      int32_t _M0L6_2atmpS1990;
      moonbit_incref_cycle_free(_M0L6_2atmpS1988);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples25if__noise__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1952, _M0L6_2atmpS1988, _M0L6_2atmpS1989);
      moonbit_decref_cycle_free(_M0L6_2atmpS1988);
      _M0L6_2atmpS1990 = _M0L2__S1956 + 1;
      _M0L2__S1956 = _M0L6_2atmpS1990;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1955);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\if_noise\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples25if__noise__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples25if__noise__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1952);
  moonbit_decref_cycle_free(_M0L12async__testsS1952);
  moonbit_flush_cycles();
  return 0;
}