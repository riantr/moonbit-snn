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

struct _M0DTPC15error5Error136RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TP26RiantR8snn__mbt11IZParameter;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGbE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB6Logger;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0BTPB4Show;

struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0TPB5ArrayGsE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TP26RiantR8snn__mbt2IZ;

struct _M0TWEu;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c875;

struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c880;

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

struct _M0DTPC15error5Error136RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0BTPB6Logger {
  int32_t(* $method_0)(void*, moonbit_string_t);
  int32_t(* $method_1)(void*, moonbit_string_t, int32_t, int32_t);
  int32_t(* $method_2)(void*, struct _M0TPC16string10StringView);
  int32_t(* $method_3)(void*, int32_t);
  int32_t(* $method_4)(void*, struct _M0TPB4Show);
  int32_t(* $method_5)(void*, struct _M0TPB4Show);
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
};

struct _M0BTPB4Show {
  int32_t(* $method_0)(void*, struct _M0TPB6Logger);
  moonbit_string_t(* $method_1)(void*);
  
};

struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
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

struct _M0TP26RiantR8snn__mbt7Xoshiro {
  uint64_t $0;
  uint64_t $1;
  uint64_t $2;
  uint64_t $3;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c875 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c880 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS887(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS880(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS875(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS852(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S845(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

struct _M0TP26RiantR8snn__mbt2IZ* _M0MP26RiantR8snn__mbt2IZ3new(
  int32_t,
  struct _M0TP26RiantR8snn__mbt11IZParameter*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt11IZParameter* _M0MP26RiantR8snn__mbt11IZParameter2rs(
  
);

int32_t _M0FP26RiantR8snn__mbt8step__iz(
  struct _M0TP26RiantR8snn__mbt2IZ*,
  float
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro11from__state(
  uint64_t,
  uint64_t,
  uint64_t,
  uint64_t
);

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

int32_t _M0MPC15float5Float7to__int(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

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

int32_t _M0MPC15array5Array4pushGfE(struct _M0TPB5ArrayGfE*, float);

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE*,
  moonbit_string_t
);

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  struct _M0TUsiE*
);

int32_t _M0MPC15array5Array7reallocGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array7reallocGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
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

int32_t _M0MPC15array5Array8capacityGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array8capacityGsE(struct _M0TPB5ArrayGsE*);

int32_t _M0MPC15array5Array8capacityGUsiEE(struct _M0TPB5ArrayGUsiEE*);

int32_t _M0FPB23array__growth__capacity(int32_t, int32_t, int32_t);

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE*);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[121]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 120, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 105, 122, 104, 105, 107, 101, 118, 
    105, 99, 104, 95, 100, 101, 98, 117, 103, 95, 98, 108, 97, 99, 107, 
    98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 
    105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 
    116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 46, 
    77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 
    118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 
    114, 114, 111, 114, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[123]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 122, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 105, 122, 104, 105, 107, 101, 118, 
    105, 99, 104, 95, 100, 101, 98, 117, 103, 95, 98, 108, 97, 99, 107, 
    98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 
    105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 
    116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 
    46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 
    105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 
    105, 112, 84, 101, 115, 116, 0
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

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS887$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS887
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[55] =
  {
    sizeof(struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c875)
    / 4, 1,
    offsetof(struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c875, $1)
    / 4
    * 2,
    sizeof(struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c880)
    / 4, 1,
    offsetof(struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c880, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error136RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error136RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt2IZ) / 4, 8,
    offsetof(struct _M0TP26RiantR8snn__mbt2IZ, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IZ, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IZ, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IZ, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IZ, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IZ, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IZ, $7) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt2IZ, $8) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGbE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGbE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGiE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGiE, $0) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS1900
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS908,
  moonbit_string_t _M0L8filenameS877,
  int32_t _M0L5indexS879
) {
  struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c875* _closure_1924;
  struct _M0TWEu* _M0L13handle__startS875;
  struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c880* _closure_1925;
  struct _M0TWssbEu* _M0L14handle__resultS880;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS887;
  void* _M0L11_2atry__errS902;
  struct moonbit_result_0 _tmp_1927;
  int32_t _handle__error__result_1928;
  int32_t _M0L6_2atmpS1888;
  void* _M0L3errS903;
  moonbit_string_t _M0L4nameS905;
  struct _M0DTPC15error5Error136RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS906;
  moonbit_string_t _M0L7_2anameS907;
  int32_t _M0L6_2acntS1918;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS877);
  _closure_1924
  = (struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c875*)moonbit_malloc(sizeof(struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c875));
  Moonbit_object_header(_closure_1924)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_1924->code
  = &_M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS875;
  _closure_1924->$0 = _M0L5indexS879;
  _closure_1924->$1 = _M0L8filenameS877;
  _M0L13handle__startS875 = (struct _M0TWEu*)_closure_1924;
  moonbit_incref_cycle_free(_M0L8filenameS877);
  _closure_1925
  = (struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c880*)moonbit_malloc(sizeof(struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c880));
  Moonbit_object_header(_closure_1925)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_1925->code
  = &_M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS880;
  _closure_1925->$0 = _M0L5indexS879;
  _closure_1925->$1 = _M0L8filenameS877;
  _M0L14handle__resultS880 = (struct _M0TWssbEu*)_closure_1925;
  _M0L17error__to__stringS887
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS887$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _tmp_1927
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS908, _M0L8filenameS877, _M0L5indexS879, _M0L13handle__startS875, _M0L14handle__resultS880, _M0L17error__to__stringS887);
  if (_tmp_1927.tag) {
    int32_t const _M0L5_2aokS1897 = _tmp_1927.data.ok;
    _handle__error__result_1928 = _M0L5_2aokS1897;
  } else {
    void* const _M0L6_2aerrS1898 = _tmp_1927.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS887);
    moonbit_decref_cycle_free(_M0L13handle__startS875);
    _M0L11_2atry__errS902 = _M0L6_2aerrS1898;
    goto join_901;
  }
  if (_handle__error__result_1928) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS887);
    moonbit_decref_cycle_free(_M0L13handle__startS875);
    _M0L6_2atmpS1888 = 1;
  } else {
    struct moonbit_result_0 _tmp_1929;
    int32_t _handle__error__result_1930;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
    _tmp_1929
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS908, _M0L8filenameS877, _M0L5indexS879, _M0L13handle__startS875, _M0L14handle__resultS880, _M0L17error__to__stringS887);
    if (_tmp_1929.tag) {
      int32_t const _M0L5_2aokS1895 = _tmp_1929.data.ok;
      _handle__error__result_1930 = _M0L5_2aokS1895;
    } else {
      void* const _M0L6_2aerrS1896 = _tmp_1929.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS887);
      moonbit_decref_cycle_free(_M0L13handle__startS875);
      _M0L11_2atry__errS902 = _M0L6_2aerrS1896;
      goto join_901;
    }
    if (_handle__error__result_1930) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS887);
      moonbit_decref_cycle_free(_M0L13handle__startS875);
      _M0L6_2atmpS1888 = 1;
    } else {
      struct moonbit_result_0 _tmp_1931;
      int32_t _handle__error__result_1932;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
      _tmp_1931
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS908, _M0L8filenameS877, _M0L5indexS879, _M0L13handle__startS875, _M0L14handle__resultS880, _M0L17error__to__stringS887);
      if (_tmp_1931.tag) {
        int32_t const _M0L5_2aokS1893 = _tmp_1931.data.ok;
        _handle__error__result_1932 = _M0L5_2aokS1893;
      } else {
        void* const _M0L6_2aerrS1894 = _tmp_1931.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS887);
        moonbit_decref_cycle_free(_M0L13handle__startS875);
        _M0L11_2atry__errS902 = _M0L6_2aerrS1894;
        goto join_901;
      }
      if (_handle__error__result_1932) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS887);
        moonbit_decref_cycle_free(_M0L13handle__startS875);
        _M0L6_2atmpS1888 = 1;
      } else {
        struct moonbit_result_0 _tmp_1933;
        int32_t _handle__error__result_1934;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
        _tmp_1933
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS908, _M0L8filenameS877, _M0L5indexS879, _M0L13handle__startS875, _M0L14handle__resultS880, _M0L17error__to__stringS887);
        if (_tmp_1933.tag) {
          int32_t const _M0L5_2aokS1891 = _tmp_1933.data.ok;
          _handle__error__result_1934 = _M0L5_2aokS1891;
        } else {
          void* const _M0L6_2aerrS1892 = _tmp_1933.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS887);
          moonbit_decref_cycle_free(_M0L13handle__startS875);
          _M0L11_2atry__errS902 = _M0L6_2aerrS1892;
          goto join_901;
        }
        if (_handle__error__result_1934) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS887);
          moonbit_decref_cycle_free(_M0L13handle__startS875);
          _M0L6_2atmpS1888 = 1;
        } else {
          struct moonbit_result_0 _tmp_1935;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
          _tmp_1935
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS908, _M0L8filenameS877, _M0L5indexS879, _M0L13handle__startS875, _M0L14handle__resultS880, _M0L17error__to__stringS887);
          moonbit_decref_cycle_free(_M0L13handle__startS875);
          moonbit_decref_cycle_free(_M0L17error__to__stringS887);
          if (_tmp_1935.tag) {
            int32_t const _M0L5_2aokS1889 = _tmp_1935.data.ok;
            _M0L6_2atmpS1888 = _M0L5_2aokS1889;
          } else {
            void* const _M0L6_2aerrS1890 = _tmp_1935.data.err;
            _M0L11_2atry__errS902 = _M0L6_2aerrS1890;
            goto join_901;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS1888) {
    void* _M0L136RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1899 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error136RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L136RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1899)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error136RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L136RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1899)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS902
    = _M0L136RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1899;
    goto join_901;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS880);
  }
  goto joinlet_1926;
  join_901:;
  _M0L3errS903 = _M0L11_2atry__errS902;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS906
  = (struct _M0DTPC15error5Error136RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS903;
  _M0L7_2anameS907 = _M0L36_2aMoonBitTestDriverInternalSkipTestS906->$0;
  _M0L6_2acntS1918
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS906));
  if (_M0L6_2acntS1918 > 1) {
    int32_t _M0L11_2anew__cntS1919 = _M0L6_2acntS1918 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS906), _M0L11_2anew__cntS1919);
    moonbit_incref_cycle_free(_M0L7_2anameS907);
  } else if (_M0L6_2acntS1918 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS906);
  }
  _M0L4nameS905 = _M0L7_2anameS907;
  goto join_904;
  goto joinlet_1936;
  join_904:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS880(_M0L14handle__resultS880, _M0L4nameS905, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS880);
  moonbit_decref_cycle_free(_M0L4nameS905);
  joinlet_1936:;
  joinlet_1926:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS887(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS1887,
  void* _M0L3errS888
) {
  void* _M0L1eS890;
  moonbit_string_t _M0L1eS892;
  moonbit_string_t _result_1939;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS888)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS893 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS888;
      moonbit_string_t _M0L4_2aeS894 = _M0L10_2aFailureS893->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS894);
      _M0L1eS892 = _M0L4_2aeS894;
      goto join_891;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS895 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS888;
      moonbit_string_t _M0L4_2aeS896 = _M0L15_2aInspectErrorS895->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS896);
      _M0L1eS892 = _M0L4_2aeS896;
      goto join_891;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS897 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS888;
      moonbit_string_t _M0L4_2aeS898 = _M0L16_2aSnapshotErrorS897->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS898);
      _M0L1eS892 = _M0L4_2aeS898;
      goto join_891;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS899 =
        (struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS888;
      moonbit_string_t _M0L4_2aeS900 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS899->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS900);
      _M0L1eS892 = _M0L4_2aeS900;
      goto join_891;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS888);
      _M0L1eS890 = _M0L3errS888;
      goto join_889;
      break;
    }
  }
  join_891:;
  return _M0L1eS892;
  join_889:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _result_1939 = _M0FP15Error10to__string(_M0L1eS890);
  moonbit_decref_cycle_free(_M0L1eS890);
  return _result_1939;
}

int32_t _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS880(
  struct _M0TWssbEu* _M0L6_2aenvS1884,
  moonbit_string_t _M0L10__testnameS881,
  moonbit_string_t _M0L7messageS882,
  int32_t _M0L7skippedS883
) {
  struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c880* _M0L14_2acasted__envS1885;
  moonbit_string_t _M0L8filenameS877;
  int32_t _M0L5indexS879;
  moonbit_string_t _M0L10file__nameS884;
  moonbit_string_t _M0L7messageS885;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS886;
  moonbit_string_t _M0L6_2atmpS1886;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1885
  = (struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c880*)_M0L6_2aenvS1884;
  _M0L8filenameS877 = _M0L14_2acasted__envS1885->$1;
  _M0L5indexS879 = _M0L14_2acasted__envS1885->$0;
  if (!_M0L7skippedS883 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS884
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS877, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS885
  = _M0MPC16string6String14escape_2einner(_M0L7messageS882, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS886
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS886, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS886, _M0L10file__nameS884);
  moonbit_decref_cycle_free(_M0L10file__nameS884);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS886, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS886, _M0L5indexS879);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS886, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS886, _M0L7messageS885);
  moonbit_decref_cycle_free(_M0L7messageS885);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS886, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1886
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS886);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS886);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1886);
  moonbit_decref_cycle_free(_M0L6_2atmpS1886);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS875(
  struct _M0TWEu* _M0L6_2aenvS1881
) {
  struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c875* _M0L14_2acasted__envS1882;
  moonbit_string_t _M0L8filenameS877;
  int32_t _M0L5indexS879;
  moonbit_string_t _M0L10file__nameS876;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS878;
  moonbit_string_t _M0L6_2atmpS1883;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1882
  = (struct _M0R136_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__debug__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c875*)_M0L6_2aenvS1881;
  _M0L8filenameS877 = _M0L14_2acasted__envS1882->$1;
  _M0L5indexS879 = _M0L14_2acasted__envS1882->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS876
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS877, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS878
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS878, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS878, _M0L10file__nameS876);
  moonbit_decref_cycle_free(_M0L10file__nameS876);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS878, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS878, _M0L5indexS879);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS878, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1883
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS878);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS878);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1883);
  moonbit_decref_cycle_free(_M0L6_2atmpS1883);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S845;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS852;
  struct _M0TUsiE** _M0L6_2atmpS1880;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS859;
  moonbit_string_t* _M0L9cli__argsS860;
  moonbit_string_t _M0L6_2atmpS1879;
  moonbit_string_t _M0L6_2atmpS1878;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS861;
  int32_t _M0L7_2abindS862;
  moonbit_string_t* _M0L7_2abindS863;
  int32_t _M0L6_2acntS1920;
  int32_t _M0L2__S864;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S845 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS852 = 0;
  _M0L6_2atmpS1880 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS859
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS859)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS859->$0 = _M0L6_2atmpS1880;
  _M0L16file__and__indexS859->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS860
  = _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS860)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS1879 = (moonbit_string_t)_M0L9cli__argsS860[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS1879);
  moonbit_decref_cycle_free(_M0L9cli__argsS860);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1878
  = _M0MP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS1879);
  moonbit_decref_cycle_free(_M0L6_2atmpS1879);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS861
  = _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS852(_M0L51moonbit__test__driver__internal__split__mbt__stringS852, _M0L6_2atmpS1878, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS1878);
  _M0L7_2abindS862 = _M0L10test__argsS861->$1;
  _M0L7_2abindS863 = _M0L10test__argsS861->$0;
  _M0L6_2acntS1920
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS861));
  if (_M0L6_2acntS1920 > 1) {
    int32_t _M0L11_2anew__cntS1921 = _M0L6_2acntS1920 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS861), _M0L11_2anew__cntS1921);
    moonbit_incref_cycle_free(_M0L7_2abindS863);
  } else if (_M0L6_2acntS1920 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS861);
  }
  _M0L2__S864 = 0;
  while (1) {
    if (_M0L2__S864 < _M0L7_2abindS862) {
      moonbit_string_t _M0L3argS865 =
        (moonbit_string_t)_M0L7_2abindS863[_M0L2__S864];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS866;
      moonbit_string_t _M0L4fileS867;
      moonbit_string_t _M0L5rangeS868;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS869;
      moonbit_string_t _M0L6_2atmpS1876;
      int32_t _M0L5startS870;
      moonbit_string_t _M0L6_2atmpS1875;
      int32_t _M0L3endS871;
      int32_t _M0L1iS872;
      int32_t _M0L6_2atmpS1877;
      moonbit_incref_cycle_free(_M0L3argS865);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS866
      = _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS852(_M0L51moonbit__test__driver__internal__split__mbt__stringS852, _M0L3argS865, 58);
      moonbit_decref_cycle_free(_M0L3argS865);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS867
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS866, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS868
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS866, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS866);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS869
      = _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS852(_M0L51moonbit__test__driver__internal__split__mbt__stringS852, _M0L5rangeS868, 45);
      moonbit_decref_cycle_free(_M0L5rangeS868);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1876
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS869, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS870
      = _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S845(_M0L45moonbit__test__driver__internal__parse__int__S845, _M0L6_2atmpS1876);
      moonbit_decref_cycle_free(_M0L6_2atmpS1876);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1875
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS869, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS869);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS871
      = _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S845(_M0L45moonbit__test__driver__internal__parse__int__S845, _M0L6_2atmpS1875);
      moonbit_decref_cycle_free(_M0L6_2atmpS1875);
      _M0L1iS872 = _M0L5startS870;
      while (1) {
        if (_M0L1iS872 < _M0L3endS871) {
          struct _M0TUsiE* _M0L8_2atupleS1873;
          int32_t _M0L6_2atmpS1874;
          moonbit_incref_cycle_free(_M0L4fileS867);
          _M0L8_2atupleS1873
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS1873)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS1873->$0 = _M0L4fileS867;
          _M0L8_2atupleS1873->$1 = _M0L1iS872;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS859, _M0L8_2atupleS1873);
          _M0L6_2atmpS1874 = _M0L1iS872 + 1;
          _M0L1iS872 = _M0L6_2atmpS1874;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS867);
        }
        break;
      }
      _M0L6_2atmpS1877 = _M0L2__S864 + 1;
      _M0L2__S864 = _M0L6_2atmpS1877;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS863);
    }
    break;
  }
  return _M0L16file__and__indexS859;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS852(
  int32_t _M0L6_2aenvS1854,
  moonbit_string_t _M0L1sS853,
  int32_t _M0L3sepS854
) {
  moonbit_string_t* _M0L6_2atmpS1872;
  struct _M0TPB5ArrayGsE* _M0L3resS855;
  struct _M0TPB8MutLocalGiE* _M0L1iS856;
  struct _M0TPB8MutLocalGiE* _M0L5startS857;
  int32_t _M0L3valS1867;
  int32_t _M0L6_2atmpS1868;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1872 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS855
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS855)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS855->$0 = _M0L6_2atmpS1872;
  _M0L3resS855->$1 = 0;
  _M0L1iS856
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS856)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS856->$0 = 0;
  _M0L5startS857
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS857)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS857->$0 = 0;
  while (1) {
    int32_t _M0L3valS1855 = _M0L1iS856->$0;
    int32_t _M0L6_2atmpS1856 = Moonbit_array_length(_M0L1sS853);
    if (_M0L3valS1855 < _M0L6_2atmpS1856) {
      int32_t _M0L3valS1859 = _M0L1iS856->$0;
      int32_t _M0L6_2atmpS1858;
      int32_t _M0L6_2atmpS1857;
      int32_t _M0L3valS1866;
      int32_t _M0L6_2atmpS1865;
      if (
        _M0L3valS1859 < 0
        || _M0L3valS1859 >= Moonbit_array_length(_M0L1sS853)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1858 = _M0L1sS853[_M0L3valS1859];
      _M0L6_2atmpS1857 = _M0L6_2atmpS1858;
      if (_M0L6_2atmpS1857 == _M0L3sepS854) {
        int32_t _M0L3valS1861 = _M0L5startS857->$0;
        int32_t _M0L3valS1862 = _M0L1iS856->$0;
        moonbit_string_t _M0L6_2atmpS1860;
        int32_t _M0L3valS1864;
        int32_t _M0L6_2atmpS1863;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS1860
        = _M0MPC16string6String17unsafe__substring(_M0L1sS853, _M0L3valS1861, _M0L3valS1862);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS855, _M0L6_2atmpS1860);
        _M0L3valS1864 = _M0L1iS856->$0;
        _M0L6_2atmpS1863 = _M0L3valS1864 + 1;
        _M0L5startS857->$0 = _M0L6_2atmpS1863;
      }
      _M0L3valS1866 = _M0L1iS856->$0;
      _M0L6_2atmpS1865 = _M0L3valS1866 + 1;
      _M0L1iS856->$0 = _M0L6_2atmpS1865;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS856);
    }
    break;
  }
  _M0L3valS1867 = _M0L5startS857->$0;
  _M0L6_2atmpS1868 = Moonbit_array_length(_M0L1sS853);
  if (_M0L3valS1867 < _M0L6_2atmpS1868) {
    int32_t _M0L3valS1870 = _M0L5startS857->$0;
    int32_t _M0L6_2atmpS1871;
    moonbit_string_t _M0L6_2atmpS1869;
    moonbit_decref_cycle_free(_M0L5startS857);
    _M0L6_2atmpS1871 = Moonbit_array_length(_M0L1sS853);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS1869
    = _M0MPC16string6String17unsafe__substring(_M0L1sS853, _M0L3valS1870, _M0L6_2atmpS1871);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS855, _M0L6_2atmpS1869);
  } else {
    moonbit_decref_cycle_free(_M0L5startS857);
  }
  return _M0L3resS855;
}

int32_t _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S845(
  int32_t _M0L6_2aenvS1847,
  moonbit_string_t _M0L1sS846
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS847;
  int32_t _M0L3lenS848;
  int32_t _M0L7_2abindS849;
  int32_t _M0L1iS850;
  int32_t _result_1944;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS847
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS847)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS847->$0 = 0;
  _M0L3lenS848 = Moonbit_array_length(_M0L1sS846);
  _M0L7_2abindS849 = 0;
  _M0L1iS850 = _M0L7_2abindS849;
  while (1) {
    if (_M0L1iS850 < _M0L3lenS848) {
      int32_t _M0L3valS1852 = _M0L3resS847->$0;
      int32_t _M0L6_2atmpS1849 = _M0L3valS1852 * 10;
      int32_t _M0L6_2atmpS1851;
      int32_t _M0L6_2atmpS1850;
      int32_t _M0L6_2atmpS1848;
      int32_t _M0L6_2atmpS1853;
      if (_M0L1iS850 < 0 || _M0L1iS850 >= Moonbit_array_length(_M0L1sS846)) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1851 = _M0L1sS846[_M0L1iS850];
      _M0L6_2atmpS1850 = _M0L6_2atmpS1851 - 48;
      _M0L6_2atmpS1848 = _M0L6_2atmpS1849 + _M0L6_2atmpS1850;
      _M0L3resS847->$0 = _M0L6_2atmpS1848;
      _M0L6_2atmpS1853 = _M0L1iS850 + 1;
      _M0L1iS850 = _M0L6_2atmpS1853;
      continue;
    }
    break;
  }
  _result_1944 = _M0L3resS847->$0;
  moonbit_decref_cycle_free(_M0L3resS847);
  return _result_1944;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS844
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS844);
  return _M0L4selfS844;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S814,
  moonbit_string_t _M0L12_2adiscard__S815,
  int32_t _M0L12_2adiscard__S816,
  struct _M0TWEu* _M0L12_2adiscard__S817,
  struct _M0TWssbEu* _M0L12_2adiscard__S818,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S819
) {
  struct moonbit_result_0 _result_1945;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _result_1945.tag = 1;
  _result_1945.data.ok = 0;
  return _result_1945;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S820,
  moonbit_string_t _M0L12_2adiscard__S821,
  int32_t _M0L12_2adiscard__S822,
  struct _M0TWEu* _M0L12_2adiscard__S823,
  struct _M0TWssbEu* _M0L12_2adiscard__S824,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S825
) {
  struct moonbit_result_0 _result_1946;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _result_1946.tag = 1;
  _result_1946.data.ok = 0;
  return _result_1946;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S826,
  moonbit_string_t _M0L12_2adiscard__S827,
  int32_t _M0L12_2adiscard__S828,
  struct _M0TWEu* _M0L12_2adiscard__S829,
  struct _M0TWssbEu* _M0L12_2adiscard__S830,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S831
) {
  struct moonbit_result_0 _result_1947;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _result_1947.tag = 1;
  _result_1947.data.ok = 0;
  return _result_1947;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S832,
  moonbit_string_t _M0L12_2adiscard__S833,
  int32_t _M0L12_2adiscard__S834,
  struct _M0TWEu* _M0L12_2adiscard__S835,
  struct _M0TWssbEu* _M0L12_2adiscard__S836,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S837
) {
  struct moonbit_result_0 _result_1948;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _result_1948.tag = 1;
  _result_1948.data.ok = 0;
  return _result_1948;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S838,
  moonbit_string_t _M0L12_2adiscard__S839,
  int32_t _M0L12_2adiscard__S840,
  struct _M0TWEu* _M0L12_2adiscard__S841,
  struct _M0TWssbEu* _M0L12_2adiscard__S842,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S843
) {
  struct moonbit_result_0 _result_1949;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _result_1949.tag = 1;
  _result_1949.data.ok = 0;
  return _result_1949;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S813
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

struct _M0TP26RiantR8snn__mbt2IZ* _M0MP26RiantR8snn__mbt2IZ3new(
  int32_t _M0L1nS784,
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L5paramS788,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS794
) {
  struct _M0TPB5ArrayGfE* _M0L1vS783;
  struct _M0TPB5ArrayGfE* _M0L1uS785;
  int32_t _M0L7_2abindS786;
  int32_t _M0L1kS787;
  struct _M0TPB5ArrayGbE* _M0L4fireS790;
  struct _M0TPB5ArrayGfE* _M0L1iS791;
  struct _M0TPB5ArrayGfE* _M0L2geS792;
  struct _M0TPB5ArrayGfE* _M0L2giS793;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L6_2atmpS1901;
  struct _M0TPB5ArrayGiE* _M0L4tabsS795;
  struct _M0TP26RiantR8snn__mbt2IZ* _block_1951;
  #line 105 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1vS783 = _M0MPC15array5Array4makeGfE(_M0L1nS784, -0x1.04p+6f);
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1uS785 = _M0MPC15array5Array4makeGfE(_M0L1nS784, 0x0p+0f);
  _M0L7_2abindS786 = 0;
  _M0L1kS787 = _M0L7_2abindS786;
  while (1) {
    if (_M0L1kS787 < _M0L1nS784) {
      float _M0L1bS1844 = _M0L5paramS788->$1;
      float _M0L6_2atmpS1845;
      float _M0L6_2atmpS1843;
      int32_t _M0L6_2atmpS1846;
      #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1845 = _M0MPC15array5Array2atGfE(_M0L1vS783, _M0L1kS787);
      _M0L6_2atmpS1843 = _M0L1bS1844 * _M0L6_2atmpS1845;
      #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS785, _M0L1kS787, _M0L6_2atmpS1843);
      _M0L6_2atmpS1846 = _M0L1kS787 + 1;
      _M0L1kS787 = _M0L6_2atmpS1846;
      continue;
    }
    break;
  }
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L4fireS790 = _M0MPC15array5Array4makeGbE(_M0L1nS784, 0);
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1iS791 = _M0MPC15array5Array4makeGfE(_M0L1nS784, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2geS792 = _M0MPC15array5Array4makeGfE(_M0L1nS784, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2giS793 = _M0MPC15array5Array4makeGfE(_M0L1nS784, 0x0p+0f);
  _M0L6_2atmpS1901 = _M0L3rngS794;
  #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L4tabsS795 = _M0MPC15array5Array4makeGiE(_M0L1nS784, 0);
  moonbit_incref_cycle_free(_M0L5paramS788);
  _block_1951
  = (struct _M0TP26RiantR8snn__mbt2IZ*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IZ));
  Moonbit_object_header(_block_1951)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_1951->$0 = _M0L5paramS788;
  _block_1951->$1 = _M0L1nS784;
  _block_1951->$2 = _M0L1vS783;
  _block_1951->$3 = _M0L1uS785;
  _block_1951->$4 = _M0L4fireS790;
  _block_1951->$5 = _M0L1iS791;
  _block_1951->$6 = _M0L2geS792;
  _block_1951->$7 = _M0L2giS793;
  _block_1951->$8 = _M0L4tabsS795;
  _block_1951->$9 = 0;
  return _block_1951;
}

struct _M0TP26RiantR8snn__mbt11IZParameter* _M0MP26RiantR8snn__mbt11IZParameter2rs(
  
) {
  struct _M0TP26RiantR8snn__mbt11IZParameter* _block_1952;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _block_1952
  = (struct _M0TP26RiantR8snn__mbt11IZParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IZParameter));
  Moonbit_object_header(_block_1952)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1952->$0 = 0x1.47ae147ae147bp-6f;
  _block_1952->$1 = 0x1.999999999999ap-3f;
  _block_1952->$2 = -0x1.04p+6f;
  _block_1952->$3 = 0x1p+3f;
  _block_1952->$4 = 0x1.4p+2f;
  _block_1952->$5 = 0x1.4p+3f;
  _block_1952->$6 = 0x0p+0f;
  _block_1952->$7 = -0x1.4p+6f;
  return _block_1952;
}

int32_t _M0FP26RiantR8snn__mbt8step__iz(
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1pS752,
  float _M0L2dtS764
) {
  int32_t _M0L1nS751;
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L3p__S753;
  float _M0L1aS754;
  float _M0L1bS755;
  float _M0L1cS756;
  float _M0L1dS757;
  float _M0L6tau__eS758;
  float _M0L6tau__iS759;
  float _M0L4e__eS760;
  float _M0L4e__iS761;
  int32_t _M0L7_2abindS762;
  int32_t _M0L1iS763;
  int32_t _M0L7_2abindS766;
  int32_t _M0L1iS767;
  int32_t _M0L7_2abindS773;
  int32_t _M0L1iS774;
  int32_t _M0L7_2abindS777;
  int32_t _M0L1iS778;
  int32_t _M0L7_2abindS780;
  int32_t _M0L1iS781;
  #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1nS751 = _M0L1pS752->$1;
  _M0L3p__S753 = _M0L1pS752->$0;
  _M0L1aS754 = _M0L3p__S753->$0;
  _M0L1bS755 = _M0L3p__S753->$1;
  _M0L1cS756 = _M0L3p__S753->$2;
  _M0L1dS757 = _M0L3p__S753->$3;
  _M0L6tau__eS758 = _M0L3p__S753->$4;
  _M0L6tau__iS759 = _M0L3p__S753->$5;
  _M0L4e__eS760 = _M0L3p__S753->$6;
  _M0L4e__iS761 = _M0L3p__S753->$7;
  _M0L7_2abindS762 = 0;
  _M0L1iS763 = _M0L7_2abindS762;
  while (1) {
    if (_M0L1iS763 < _M0L1nS751) {
      struct _M0TPB5ArrayGfE* _M0L2geS1751 = _M0L1pS752->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS1759 = _M0L1pS752->$6;
      float _M0L6_2atmpS1753;
      struct _M0TPB5ArrayGfE* _M0L2geS1758;
      float _M0L6_2atmpS1757;
      float _M0L6_2atmpS1756;
      float _M0L6_2atmpS1755;
      float _M0L6_2atmpS1754;
      float _M0L6_2atmpS1752;
      struct _M0TPB5ArrayGfE* _M0L2giS1760;
      struct _M0TPB5ArrayGfE* _M0L2giS1768;
      float _M0L6_2atmpS1762;
      struct _M0TPB5ArrayGfE* _M0L2giS1767;
      float _M0L6_2atmpS1766;
      float _M0L6_2atmpS1765;
      float _M0L6_2atmpS1764;
      float _M0L6_2atmpS1763;
      float _M0L6_2atmpS1761;
      int32_t _M0L6_2atmpS1769;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1753 = _M0MPC15array5Array2atGfE(_M0L2geS1759, _M0L1iS763);
      _M0L2geS1758 = _M0L1pS752->$6;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1757 = _M0MPC15array5Array2atGfE(_M0L2geS1758, _M0L1iS763);
      _M0L6_2atmpS1756 = -_M0L6_2atmpS1757;
      _M0L6_2atmpS1755 = _M0L2dtS764 * _M0L6_2atmpS1756;
      _M0L6_2atmpS1754 = _M0L6_2atmpS1755 / _M0L6tau__eS758;
      _M0L6_2atmpS1752 = _M0L6_2atmpS1753 + _M0L6_2atmpS1754;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1751, _M0L1iS763, _M0L6_2atmpS1752);
      _M0L2giS1760 = _M0L1pS752->$7;
      _M0L2giS1768 = _M0L1pS752->$7;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1762 = _M0MPC15array5Array2atGfE(_M0L2giS1768, _M0L1iS763);
      _M0L2giS1767 = _M0L1pS752->$7;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1766 = _M0MPC15array5Array2atGfE(_M0L2giS1767, _M0L1iS763);
      _M0L6_2atmpS1765 = -_M0L6_2atmpS1766;
      _M0L6_2atmpS1764 = _M0L2dtS764 * _M0L6_2atmpS1765;
      _M0L6_2atmpS1763 = _M0L6_2atmpS1764 / _M0L6tau__iS759;
      _M0L6_2atmpS1761 = _M0L6_2atmpS1762 + _M0L6_2atmpS1763;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1760, _M0L1iS763, _M0L6_2atmpS1761);
      _M0L6_2atmpS1769 = _M0L1iS763 + 1;
      _M0L1iS763 = _M0L6_2atmpS1769;
      continue;
    }
    break;
  }
  _M0L7_2abindS766 = 0;
  _M0L1iS767 = _M0L7_2abindS766;
  while (1) {
    if (_M0L1iS767 < _M0L1nS751) {
      struct _M0TPB5ArrayGfE* _M0L1vS1795 = _M0L1pS752->$2;
      float _M0L1vS768;
      struct _M0TPB5ArrayGfE* _M0L1uS1794;
      float _M0L1uS769;
      struct _M0TPB5ArrayGfE* _M0L1iS1793;
      float _M0L2iiS770;
      struct _M0TPB5ArrayGfE* _M0L1vS1770;
      float _M0L6_2atmpS1773;
      float _M0L6_2atmpS1780;
      float _M0L6_2atmpS1778;
      float _M0L6_2atmpS1779;
      float _M0L6_2atmpS1777;
      float _M0L6_2atmpS1776;
      float _M0L6_2atmpS1775;
      float _M0L6_2atmpS1774;
      float _M0L6_2atmpS1772;
      float _M0L6_2atmpS1771;
      struct _M0TPB5ArrayGfE* _M0L1vS1792;
      float _M0L2v2S771;
      struct _M0TPB5ArrayGfE* _M0L1vS1781;
      float _M0L6_2atmpS1784;
      float _M0L6_2atmpS1791;
      float _M0L6_2atmpS1789;
      float _M0L6_2atmpS1790;
      float _M0L6_2atmpS1788;
      float _M0L6_2atmpS1787;
      float _M0L6_2atmpS1786;
      float _M0L6_2atmpS1785;
      float _M0L6_2atmpS1783;
      float _M0L6_2atmpS1782;
      int32_t _M0L6_2atmpS1796;
      #line 359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS768 = _M0MPC15array5Array2atGfE(_M0L1vS1795, _M0L1iS767);
      _M0L1uS1794 = _M0L1pS752->$3;
      #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1uS769 = _M0MPC15array5Array2atGfE(_M0L1uS1794, _M0L1iS767);
      _M0L1iS1793 = _M0L1pS752->$5;
      #line 361 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2iiS770 = _M0MPC15array5Array2atGfE(_M0L1iS1793, _M0L1iS767);
      _M0L1vS1770 = _M0L1pS752->$2;
      _M0L6_2atmpS1773 = 0x1p-1f * _M0L2dtS764;
      _M0L6_2atmpS1780 = 0x1.47ae147ae147bp-5f * _M0L1vS768;
      _M0L6_2atmpS1778 = _M0L6_2atmpS1780 * _M0L1vS768;
      _M0L6_2atmpS1779 = 0x1.4p+2f * _M0L1vS768;
      _M0L6_2atmpS1777 = _M0L6_2atmpS1778 + _M0L6_2atmpS1779;
      _M0L6_2atmpS1776 = _M0L6_2atmpS1777 + 0x1.18p+7f;
      _M0L6_2atmpS1775 = _M0L6_2atmpS1776 - _M0L1uS769;
      _M0L6_2atmpS1774 = _M0L6_2atmpS1775 + _M0L2iiS770;
      _M0L6_2atmpS1772 = _M0L6_2atmpS1773 * _M0L6_2atmpS1774;
      _M0L6_2atmpS1771 = _M0L1vS768 + _M0L6_2atmpS1772;
      #line 362 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1770, _M0L1iS767, _M0L6_2atmpS1771);
      _M0L1vS1792 = _M0L1pS752->$2;
      #line 363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2v2S771 = _M0MPC15array5Array2atGfE(_M0L1vS1792, _M0L1iS767);
      _M0L1vS1781 = _M0L1pS752->$2;
      _M0L6_2atmpS1784 = 0x1p-1f * _M0L2dtS764;
      _M0L6_2atmpS1791 = 0x1.47ae147ae147bp-5f * _M0L2v2S771;
      _M0L6_2atmpS1789 = _M0L6_2atmpS1791 * _M0L2v2S771;
      _M0L6_2atmpS1790 = 0x1.4p+2f * _M0L2v2S771;
      _M0L6_2atmpS1788 = _M0L6_2atmpS1789 + _M0L6_2atmpS1790;
      _M0L6_2atmpS1787 = _M0L6_2atmpS1788 + 0x1.18p+7f;
      _M0L6_2atmpS1786 = _M0L6_2atmpS1787 - _M0L1uS769;
      _M0L6_2atmpS1785 = _M0L6_2atmpS1786 + _M0L2iiS770;
      _M0L6_2atmpS1783 = _M0L6_2atmpS1784 * _M0L6_2atmpS1785;
      _M0L6_2atmpS1782 = _M0L2v2S771 + _M0L6_2atmpS1783;
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1781, _M0L1iS767, _M0L6_2atmpS1782);
      _M0L6_2atmpS1796 = _M0L1iS767 + 1;
      _M0L1iS767 = _M0L6_2atmpS1796;
      continue;
    }
    break;
  }
  _M0L7_2abindS773 = 0;
  _M0L1iS774 = _M0L7_2abindS773;
  while (1) {
    if (_M0L1iS774 < _M0L1nS751) {
      struct _M0TPB5ArrayGfE* _M0L1vS1807 = _M0L1pS752->$2;
      float _M0L1vS775;
      struct _M0TPB5ArrayGfE* _M0L1uS1797;
      struct _M0TPB5ArrayGfE* _M0L1uS1806;
      float _M0L6_2atmpS1799;
      float _M0L6_2atmpS1801;
      float _M0L6_2atmpS1803;
      struct _M0TPB5ArrayGfE* _M0L1uS1805;
      float _M0L6_2atmpS1804;
      float _M0L6_2atmpS1802;
      float _M0L6_2atmpS1800;
      float _M0L6_2atmpS1798;
      int32_t _M0L6_2atmpS1808;
      #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS775 = _M0MPC15array5Array2atGfE(_M0L1vS1807, _M0L1iS774);
      _M0L1uS1797 = _M0L1pS752->$3;
      _M0L1uS1806 = _M0L1pS752->$3;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1799 = _M0MPC15array5Array2atGfE(_M0L1uS1806, _M0L1iS774);
      _M0L6_2atmpS1801 = _M0L2dtS764 * _M0L1aS754;
      _M0L6_2atmpS1803 = _M0L1bS755 * _M0L1vS775;
      _M0L1uS1805 = _M0L1pS752->$3;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1804 = _M0MPC15array5Array2atGfE(_M0L1uS1805, _M0L1iS774);
      _M0L6_2atmpS1802 = _M0L6_2atmpS1803 - _M0L6_2atmpS1804;
      _M0L6_2atmpS1800 = _M0L6_2atmpS1801 * _M0L6_2atmpS1802;
      _M0L6_2atmpS1798 = _M0L6_2atmpS1799 + _M0L6_2atmpS1800;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS1797, _M0L1iS774, _M0L6_2atmpS1798);
      _M0L6_2atmpS1808 = _M0L1iS774 + 1;
      _M0L1iS774 = _M0L6_2atmpS1808;
      continue;
    }
    break;
  }
  _M0L7_2abindS777 = 0;
  _M0L1iS778 = _M0L7_2abindS777;
  while (1) {
    if (_M0L1iS778 < _M0L1nS751) {
      struct _M0TPB5ArrayGfE* _M0L1vS1809 = _M0L1pS752->$2;
      struct _M0TPB5ArrayGfE* _M0L1vS1826 = _M0L1pS752->$2;
      float _M0L6_2atmpS1811;
      struct _M0TPB5ArrayGfE* _M0L2geS1825;
      float _M0L6_2atmpS1821;
      struct _M0TPB5ArrayGfE* _M0L1vS1824;
      float _M0L6_2atmpS1823;
      float _M0L6_2atmpS1822;
      float _M0L6_2atmpS1814;
      struct _M0TPB5ArrayGfE* _M0L2giS1820;
      float _M0L6_2atmpS1816;
      struct _M0TPB5ArrayGfE* _M0L1vS1819;
      float _M0L6_2atmpS1818;
      float _M0L6_2atmpS1817;
      float _M0L6_2atmpS1815;
      float _M0L6_2atmpS1813;
      float _M0L6_2atmpS1812;
      float _M0L6_2atmpS1810;
      int32_t _M0L6_2atmpS1827;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1811 = _M0MPC15array5Array2atGfE(_M0L1vS1826, _M0L1iS778);
      _M0L2geS1825 = _M0L1pS752->$6;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1821 = _M0MPC15array5Array2atGfE(_M0L2geS1825, _M0L1iS778);
      _M0L1vS1824 = _M0L1pS752->$2;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1823 = _M0MPC15array5Array2atGfE(_M0L1vS1824, _M0L1iS778);
      _M0L6_2atmpS1822 = _M0L4e__eS760 - _M0L6_2atmpS1823;
      _M0L6_2atmpS1814 = _M0L6_2atmpS1821 * _M0L6_2atmpS1822;
      _M0L2giS1820 = _M0L1pS752->$7;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1816 = _M0MPC15array5Array2atGfE(_M0L2giS1820, _M0L1iS778);
      _M0L1vS1819 = _M0L1pS752->$2;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1818 = _M0MPC15array5Array2atGfE(_M0L1vS1819, _M0L1iS778);
      _M0L6_2atmpS1817 = _M0L4e__iS761 - _M0L6_2atmpS1818;
      _M0L6_2atmpS1815 = _M0L6_2atmpS1816 * _M0L6_2atmpS1817;
      _M0L6_2atmpS1813 = _M0L6_2atmpS1814 + _M0L6_2atmpS1815;
      _M0L6_2atmpS1812 = _M0L2dtS764 * _M0L6_2atmpS1813;
      _M0L6_2atmpS1810 = _M0L6_2atmpS1811 + _M0L6_2atmpS1812;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1809, _M0L1iS778, _M0L6_2atmpS1810);
      _M0L6_2atmpS1827 = _M0L1iS778 + 1;
      _M0L1iS778 = _M0L6_2atmpS1827;
      continue;
    }
    break;
  }
  _M0L7_2abindS780 = 0;
  _M0L1iS781 = _M0L7_2abindS780;
  while (1) {
    if (_M0L1iS781 < _M0L1nS751) {
      struct _M0TPB5ArrayGbE* _M0L4fireS1828 = _M0L1pS752->$4;
      struct _M0TPB5ArrayGfE* _M0L1vS1831 = _M0L1pS752->$2;
      float _M0L6_2atmpS1830;
      int32_t _M0L6_2atmpS1829;
      struct _M0TPB5ArrayGfE* _M0L1vS1832;
      struct _M0TPB5ArrayGbE* _M0L4fireS1834;
      float _M0L6_2atmpS1833;
      struct _M0TPB5ArrayGfE* _M0L1uS1836;
      struct _M0TPB5ArrayGfE* _M0L1uS1841;
      float _M0L6_2atmpS1838;
      struct _M0TPB5ArrayGbE* _M0L4fireS1840;
      float _M0L6_2atmpS1839;
      float _M0L6_2atmpS1837;
      int32_t _M0L6_2atmpS1842;
      #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1830 = _M0MPC15array5Array2atGfE(_M0L1vS1831, _M0L1iS781);
      _M0L6_2atmpS1829 = _M0L6_2atmpS1830 > 0x1.ep+4f;
      #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1828, _M0L1iS781, _M0L6_2atmpS1829);
      _M0L1vS1832 = _M0L1pS752->$2;
      _M0L4fireS1834 = _M0L1pS752->$4;
      #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1834, _M0L1iS781)) {
        _M0L6_2atmpS1833 = _M0L1cS756;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS1835 = _M0L1pS752->$2;
        #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS1833 = _M0MPC15array5Array2atGfE(_M0L1vS1835, _M0L1iS781);
      }
      #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1832, _M0L1iS781, _M0L6_2atmpS1833);
      _M0L1uS1836 = _M0L1pS752->$3;
      _M0L1uS1841 = _M0L1pS752->$3;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1838 = _M0MPC15array5Array2atGfE(_M0L1uS1841, _M0L1iS781);
      _M0L4fireS1840 = _M0L1pS752->$4;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1840, _M0L1iS781)) {
        _M0L6_2atmpS1839 = _M0L1dS757;
      } else {
        _M0L6_2atmpS1839 = 0x0p+0f;
      }
      _M0L6_2atmpS1837 = _M0L6_2atmpS1838 + _M0L6_2atmpS1839;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS1836, _M0L1iS781, _M0L6_2atmpS1837);
      _M0L6_2atmpS1842 = _M0L1iS781 + 1;
      _M0L1iS781 = _M0L6_2atmpS1842;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro11from__state(
  uint64_t _M0L2s0S747,
  uint64_t _M0L2s1S748,
  uint64_t _M0L2s2S749,
  uint64_t _M0L2s3S750
) {
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_1958;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _block_1958
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_1958)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1958->$0 = _M0L2s0S747;
  _block_1958->$1 = _M0L2s1S748;
  _block_1958->$2 = _M0L2s2S749;
  _block_1958->$3 = _M0L2s3S750;
  return _block_1958;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS746) {
  double _M0L6_2atmpS1750;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1750 = (double)_M0L4selfS746;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1750);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS745) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS745 != _M0L4selfS745) {
    return 0;
  } else if (_M0L4selfS745 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS745 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS745;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS731,
  float _M0L4elemS733
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS730;
  int32_t _M0L1iS732;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS730 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS731);
  _M0L1iS732 = 0;
  while (1) {
    if (_M0L1iS732 < _M0L3lenS731) {
      float* _M0L3bufS1744 = _M0L3arrS730->$0;
      int32_t _M0L6_2atmpS1745;
      _M0L3bufS1744[_M0L1iS732] = _M0L4elemS733;
      _M0L6_2atmpS1745 = _M0L1iS732 + 1;
      _M0L1iS732 = _M0L6_2atmpS1745;
      continue;
    }
    break;
  }
  return _M0L3arrS730;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS736,
  int32_t _M0L4elemS738
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS735;
  int32_t _M0L1iS737;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS735 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS736);
  _M0L1iS737 = 0;
  while (1) {
    if (_M0L1iS737 < _M0L3lenS736) {
      uint8_t* _M0L3bufS1746 = _M0L3arrS735->$0;
      int32_t _M0L6_2atmpS1747;
      _M0L3bufS1746[_M0L1iS737] = _M0L4elemS738;
      _M0L6_2atmpS1747 = _M0L1iS737 + 1;
      _M0L1iS737 = _M0L6_2atmpS1747;
      continue;
    }
    break;
  }
  return _M0L3arrS735;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS741,
  int32_t _M0L4elemS743
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS740;
  int32_t _M0L1iS742;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS740 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS741);
  _M0L1iS742 = 0;
  while (1) {
    if (_M0L1iS742 < _M0L3lenS741) {
      int32_t* _M0L3bufS1748 = _M0L3arrS740->$0;
      int32_t _M0L6_2atmpS1749;
      _M0L3bufS1748[_M0L1iS742] = _M0L4elemS743;
      _M0L6_2atmpS1749 = _M0L1iS742 + 1;
      _M0L1iS742 = _M0L6_2atmpS1749;
      continue;
    }
    break;
  }
  return _M0L3arrS740;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS723,
  int32_t _M0L5indexS724,
  float _M0L5valueS725
) {
  int32_t _M0L3lenS722;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS722 = _M0L4selfS723->$1;
  if (_M0L5indexS724 >= 0 && _M0L5indexS724 < _M0L3lenS722) {
    float* _M0L6_2atmpS1742;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1742 = _M0MPC15array5Array6bufferGfE(_M0L4selfS723);
    _M0L6_2atmpS1742[_M0L5indexS724] = _M0L5valueS725;
    moonbit_decref_cycle_free(_M0L6_2atmpS1742);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS727,
  int32_t _M0L5indexS728,
  int32_t _M0L5valueS729
) {
  int32_t _M0L3lenS726;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS726 = _M0L4selfS727->$1;
  if (_M0L5indexS728 >= 0 && _M0L5indexS728 < _M0L3lenS726) {
    uint8_t* _M0L6_2atmpS1743;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1743 = _M0MPC15array5Array6bufferGbE(_M0L4selfS727);
    _M0L6_2atmpS1743[_M0L5indexS728] = _M0L5valueS729;
    moonbit_decref_cycle_free(_M0L6_2atmpS1743);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS714,
  int32_t _M0L5indexS715
) {
  int32_t _M0L3lenS713;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS713 = _M0L4selfS714->$1;
  if (_M0L5indexS715 >= 0 && _M0L5indexS715 < _M0L3lenS713) {
    float* _M0L6_2atmpS1739;
    float _result_1962;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1739 = _M0MPC15array5Array6bufferGfE(_M0L4selfS714);
    _result_1962 = (float)_M0L6_2atmpS1739[_M0L5indexS715];
    moonbit_decref_cycle_free(_M0L6_2atmpS1739);
    return _result_1962;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS717,
  int32_t _M0L5indexS718
) {
  int32_t _M0L3lenS716;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS716 = _M0L4selfS717->$1;
  if (_M0L5indexS718 >= 0 && _M0L5indexS718 < _M0L3lenS716) {
    uint8_t* _M0L6_2atmpS1740;
    int32_t _result_1963;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1740 = _M0MPC15array5Array6bufferGbE(_M0L4selfS717);
    _result_1963 = (int32_t)_M0L6_2atmpS1740[_M0L5indexS718];
    moonbit_decref_cycle_free(_M0L6_2atmpS1740);
    return _result_1963;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS720,
  int32_t _M0L5indexS721
) {
  int32_t _M0L3lenS719;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS719 = _M0L4selfS720->$1;
  if (_M0L5indexS721 >= 0 && _M0L5indexS721 < _M0L3lenS719) {
    moonbit_string_t* _M0L6_2atmpS1741;
    moonbit_string_t _M0L6_2atmpS1902;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1741 = _M0MPC15array5Array6bufferGsE(_M0L4selfS720);
    _M0L6_2atmpS1902 = (moonbit_string_t)_M0L6_2atmpS1741[_M0L5indexS721];
    moonbit_incref_cycle_free(_M0L6_2atmpS1902);
    moonbit_decref_cycle_free(_M0L6_2atmpS1741);
    return _M0L6_2atmpS1902;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS712) {
  moonbit_string_t _M0L6_2atmpS1738;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1738 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS712);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1738);
  moonbit_decref_cycle_free(_M0L6_2atmpS1738);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS711) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS711);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS696) {
  uint64_t _M0L4bitsS699;
  uint64_t _M0L6_2atmpS1737;
  uint64_t _M0L6_2atmpS1736;
  int32_t _M0L8ieeeSignS700;
  uint64_t _M0L12ieeeMantissaS701;
  uint64_t _M0L6_2atmpS1735;
  uint64_t _M0L6_2atmpS1734;
  int32_t _M0L12ieeeExponentS702;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS703;
  struct _M0TPB17FloatingDecimal64* _M0L1vS704;
  moonbit_string_t _result_1965;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS696 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  if (_M0L3valS696 >= -0x1p+53 && _M0L3valS696 <= 0x1p+53) {
    if (_M0L3valS696 >= -0x1p+31 && _M0L3valS696 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS697;
      double _M0L6_2atmpS1723;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS697 = _M0MPC16double6Double7to__int(_M0L3valS696);
      _M0L6_2atmpS1723 = (double)_M0L1iS697;
      if (_M0L6_2atmpS1723 == _M0L3valS696) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS697, 10);
      }
    } else {
      int64_t _M0L1iS698;
      double _M0L6_2atmpS1724;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS698 = _M0MPC16double6Double9to__int64(_M0L3valS696);
      _M0L6_2atmpS1724 = (double)_M0L1iS698;
      if (_M0L6_2atmpS1724 == _M0L3valS696) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS698, 10);
      }
    }
  }
  _M0L4bitsS699 = *(int64_t*)&_M0L3valS696;
  _M0L6_2atmpS1737 = _M0L4bitsS699 >> 63;
  _M0L6_2atmpS1736 = _M0L6_2atmpS1737 & 1ull;
  _M0L8ieeeSignS700 = _M0L6_2atmpS1736 != 0ull;
  _M0L12ieeeMantissaS701 = _M0L4bitsS699 & 4503599627370495ull;
  _M0L6_2atmpS1735 = _M0L4bitsS699 >> 52;
  _M0L6_2atmpS1734 = _M0L6_2atmpS1735 & 2047ull;
  _M0L12ieeeExponentS702 = (int32_t)_M0L6_2atmpS1734;
  if (
    _M0L12ieeeExponentS702 == 2047
    || _M0L12ieeeExponentS702 == 0 && _M0L12ieeeMantissaS701 == 0ull
  ) {
    int32_t _M0L6_2atmpS1725 = _M0L12ieeeExponentS702 != 0;
    int32_t _M0L6_2atmpS1726 = _M0L12ieeeMantissaS701 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS700, _M0L6_2atmpS1725, _M0L6_2atmpS1726);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS703
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS701, _M0L12ieeeExponentS702);
  if (_M0L7_2abindS703 == 0) {
    uint32_t _M0L6_2atmpS1727;
    if (_M0L7_2abindS703) {
      moonbit_decref_cycle_free(_M0L7_2abindS703);
    }
    _M0L6_2atmpS1727 = *(uint32_t*)&_M0L12ieeeExponentS702;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS704 = _M0FPB3d2d(_M0L12ieeeMantissaS701, _M0L6_2atmpS1727);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS705 = _M0L7_2abindS703;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS706 = _M0L7_2aSomeS705;
    struct _M0TPB17FloatingDecimal64* _M0L1xS707 = _M0L4_2afS706;
    while (1) {
      uint64_t _M0L8mantissaS1733 = _M0L1xS707->$0;
      uint64_t _M0L1qS708 = _M0L8mantissaS1733 / 10ull;
      uint64_t _M0L8mantissaS1731 = _M0L1xS707->$0;
      uint64_t _M0L6_2atmpS1732 = 10ull * _M0L1qS708;
      uint64_t _M0L1rS709 = _M0L8mantissaS1731 - _M0L6_2atmpS1732;
      int32_t _M0L8exponentS1730;
      int32_t _M0L6_2atmpS1729;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1728;
      if (_M0L1rS709 != 0ull) {
        _M0L1vS704 = _M0L1xS707;
        break;
      }
      _M0L8exponentS1730 = _M0L1xS707->$1;
      moonbit_decref_cycle_free(_M0L1xS707);
      _M0L6_2atmpS1729 = _M0L8exponentS1730 + 1;
      _M0L6_2atmpS1728
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1728)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1728->$0 = _M0L1qS708;
      _M0L6_2atmpS1728->$1 = _M0L6_2atmpS1729;
      _M0L1xS707 = _M0L6_2atmpS1728;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1965 = _M0FPB9to__chars(_M0L1vS704, _M0L8ieeeSignS700);
  moonbit_decref_cycle_free(_M0L1vS704);
  return _result_1965;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS691,
  int32_t _M0L12ieeeExponentS693
) {
  uint64_t _M0L2m2S690;
  int32_t _M0L6_2atmpS1722;
  int32_t _M0L2e2S692;
  int32_t _M0L6_2atmpS1721;
  uint64_t _M0L6_2atmpS1720;
  uint64_t _M0L4maskS694;
  uint64_t _M0L8fractionS695;
  int32_t _M0L6_2atmpS1719;
  uint64_t _M0L6_2atmpS1718;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1717;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S690 = 4503599627370496ull | _M0L12ieeeMantissaS691;
  _M0L6_2atmpS1722 = _M0L12ieeeExponentS693 - 1023;
  _M0L2e2S692 = _M0L6_2atmpS1722 - 52;
  if (_M0L2e2S692 > 0) {
    return 0;
  }
  if (_M0L2e2S692 < -52) {
    return 0;
  }
  _M0L6_2atmpS1721 = -_M0L2e2S692;
  _M0L6_2atmpS1720 = 1ull << (_M0L6_2atmpS1721 & 63);
  _M0L4maskS694 = _M0L6_2atmpS1720 - 1ull;
  _M0L8fractionS695 = _M0L2m2S690 & _M0L4maskS694;
  if (_M0L8fractionS695 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1719 = -_M0L2e2S692;
  _M0L6_2atmpS1718 = _M0L2m2S690 >> (_M0L6_2atmpS1719 & 63);
  _M0L6_2atmpS1717
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1717)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1717->$0 = _M0L6_2atmpS1718;
  _M0L6_2atmpS1717->$1 = 0;
  return _M0L6_2atmpS1717;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS658,
  int32_t _M0L4signS656
) {
  moonbit_bytes_t _M0L6resultS654;
  int32_t _M0Lm5indexS655;
  uint64_t _M0L6outputS657;
  int32_t _M0L7olengthS659;
  int32_t _M0L8exponentS1716;
  int32_t _M0L6_2atmpS1715;
  int32_t _M0Lm3expS660;
  int32_t _M0L6_2atmpS1714;
  int32_t _M0L6_2atmpS1712;
  int32_t _M0L18scientificNotationS661;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS654 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS655 = 0;
  if (_M0L4signS656) {
    int32_t _M0L6_2atmpS1586 = _M0Lm5indexS655;
    int32_t _M0L6_2atmpS1587;
    if (
      _M0L6_2atmpS1586 < 0
      || _M0L6_2atmpS1586 >= Moonbit_array_length(_M0L6resultS654)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS654[_M0L6_2atmpS1586] = 45;
    _M0L6_2atmpS1587 = _M0Lm5indexS655;
    _M0Lm5indexS655 = _M0L6_2atmpS1587 + 1;
  }
  _M0L6outputS657 = _M0L1vS658->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS659 = _M0FPB17decimal__length17(_M0L6outputS657);
  _M0L8exponentS1716 = _M0L1vS658->$1;
  _M0L6_2atmpS1715 = _M0L8exponentS1716 + _M0L7olengthS659;
  _M0Lm3expS660 = _M0L6_2atmpS1715 - 1;
  _M0L6_2atmpS1714 = _M0Lm3expS660;
  if (_M0L6_2atmpS1714 >= -6) {
    int32_t _M0L6_2atmpS1713 = _M0Lm3expS660;
    _M0L6_2atmpS1712 = _M0L6_2atmpS1713 < 21;
  } else {
    _M0L6_2atmpS1712 = 0;
  }
  _M0L18scientificNotationS661 = !_M0L6_2atmpS1712;
  if (_M0L18scientificNotationS661) {
    int32_t _M0L7_2abindS662 = _M0L7olengthS659 - 1;
    uint64_t _M0L6outputS663;
    int32_t _M0L1iS664 = 0;
    uint64_t _M0L6outputS665 = _M0L6outputS657;
    int32_t _M0L6_2atmpS1588;
    int32_t _M0L6_2atmpS1592;
    int32_t _M0L6_2atmpS1591;
    int32_t _M0L6_2atmpS1590;
    int32_t _M0L6_2atmpS1589;
    int32_t _M0L6_2atmpS1596;
    int32_t _M0L6_2atmpS1597;
    int32_t _M0L6_2atmpS1598;
    int32_t _M0L6_2atmpS1599;
    int32_t _M0L6_2atmpS1600;
    int32_t _M0L6_2atmpS1606;
    int32_t _M0L6_2atmpS1639;
    moonbit_string_t _result_1967;
    while (1) {
      if (_M0L1iS664 < _M0L7_2abindS662) {
        uint64_t _M0L1cS666 = _M0L6outputS665 % 10ull;
        int32_t _M0L6_2atmpS1645 = _M0Lm5indexS655;
        int32_t _M0L6_2atmpS1644 = _M0L6_2atmpS1645 + _M0L7olengthS659;
        int32_t _M0L6_2atmpS1640 = _M0L6_2atmpS1644 - _M0L1iS664;
        int32_t _M0L6_2atmpS1643 = (int32_t)_M0L1cS666;
        int32_t _M0L6_2atmpS1642 = 48 + _M0L6_2atmpS1643;
        int32_t _M0L6_2atmpS1641 = _M0L6_2atmpS1642 & 0xff;
        int32_t _M0L6_2atmpS1646;
        uint64_t _M0L6_2atmpS1647;
        if (
          _M0L6_2atmpS1640 < 0
          || _M0L6_2atmpS1640 >= Moonbit_array_length(_M0L6resultS654)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS654[_M0L6_2atmpS1640] = _M0L6_2atmpS1641;
        _M0L6_2atmpS1646 = _M0L1iS664 + 1;
        _M0L6_2atmpS1647 = _M0L6outputS665 / 10ull;
        _M0L1iS664 = _M0L6_2atmpS1646;
        _M0L6outputS665 = _M0L6_2atmpS1647;
        continue;
      } else {
        _M0L6outputS663 = _M0L6outputS665;
      }
      break;
    }
    _M0L6_2atmpS1588 = _M0Lm5indexS655;
    _M0L6_2atmpS1592 = (int32_t)_M0L6outputS663;
    _M0L6_2atmpS1591 = _M0L6_2atmpS1592 % 10;
    _M0L6_2atmpS1590 = 48 + _M0L6_2atmpS1591;
    _M0L6_2atmpS1589 = _M0L6_2atmpS1590 & 0xff;
    if (
      _M0L6_2atmpS1588 < 0
      || _M0L6_2atmpS1588 >= Moonbit_array_length(_M0L6resultS654)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS654[_M0L6_2atmpS1588] = _M0L6_2atmpS1589;
    if (_M0L7olengthS659 > 1) {
      int32_t _M0L6_2atmpS1594 = _M0Lm5indexS655;
      int32_t _M0L6_2atmpS1593 = _M0L6_2atmpS1594 + 1;
      if (
        _M0L6_2atmpS1593 < 0
        || _M0L6_2atmpS1593 >= Moonbit_array_length(_M0L6resultS654)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS654[_M0L6_2atmpS1593] = 46;
    } else {
      int32_t _M0L6_2atmpS1595 = _M0Lm5indexS655;
      _M0Lm5indexS655 = _M0L6_2atmpS1595 - 1;
    }
    _M0L6_2atmpS1596 = _M0Lm5indexS655;
    _M0L6_2atmpS1597 = _M0L7olengthS659 + 1;
    _M0Lm5indexS655 = _M0L6_2atmpS1596 + _M0L6_2atmpS1597;
    _M0L6_2atmpS1598 = _M0Lm5indexS655;
    if (
      _M0L6_2atmpS1598 < 0
      || _M0L6_2atmpS1598 >= Moonbit_array_length(_M0L6resultS654)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS654[_M0L6_2atmpS1598] = 101;
    _M0L6_2atmpS1599 = _M0Lm5indexS655;
    _M0Lm5indexS655 = _M0L6_2atmpS1599 + 1;
    _M0L6_2atmpS1600 = _M0Lm3expS660;
    if (_M0L6_2atmpS1600 < 0) {
      int32_t _M0L6_2atmpS1601 = _M0Lm5indexS655;
      int32_t _M0L6_2atmpS1602;
      int32_t _M0L6_2atmpS1603;
      if (
        _M0L6_2atmpS1601 < 0
        || _M0L6_2atmpS1601 >= Moonbit_array_length(_M0L6resultS654)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS654[_M0L6_2atmpS1601] = 45;
      _M0L6_2atmpS1602 = _M0Lm5indexS655;
      _M0Lm5indexS655 = _M0L6_2atmpS1602 + 1;
      _M0L6_2atmpS1603 = _M0Lm3expS660;
      _M0Lm3expS660 = -_M0L6_2atmpS1603;
    } else {
      int32_t _M0L6_2atmpS1604 = _M0Lm5indexS655;
      int32_t _M0L6_2atmpS1605;
      if (
        _M0L6_2atmpS1604 < 0
        || _M0L6_2atmpS1604 >= Moonbit_array_length(_M0L6resultS654)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS654[_M0L6_2atmpS1604] = 43;
      _M0L6_2atmpS1605 = _M0Lm5indexS655;
      _M0Lm5indexS655 = _M0L6_2atmpS1605 + 1;
    }
    _M0L6_2atmpS1606 = _M0Lm3expS660;
    if (_M0L6_2atmpS1606 >= 100) {
      int32_t _M0L6_2atmpS1622 = _M0Lm3expS660;
      int32_t _M0L1aS668 = _M0L6_2atmpS1622 / 100;
      int32_t _M0L6_2atmpS1621 = _M0Lm3expS660;
      int32_t _M0L6_2atmpS1620 = _M0L6_2atmpS1621 / 10;
      int32_t _M0L1bS669 = _M0L6_2atmpS1620 % 10;
      int32_t _M0L6_2atmpS1619 = _M0Lm3expS660;
      int32_t _M0L1cS670 = _M0L6_2atmpS1619 % 10;
      int32_t _M0L6_2atmpS1607 = _M0Lm5indexS655;
      int32_t _M0L6_2atmpS1609 = 48 + _M0L1aS668;
      int32_t _M0L6_2atmpS1608 = _M0L6_2atmpS1609 & 0xff;
      int32_t _M0L6_2atmpS1613;
      int32_t _M0L6_2atmpS1610;
      int32_t _M0L6_2atmpS1612;
      int32_t _M0L6_2atmpS1611;
      int32_t _M0L6_2atmpS1617;
      int32_t _M0L6_2atmpS1614;
      int32_t _M0L6_2atmpS1616;
      int32_t _M0L6_2atmpS1615;
      int32_t _M0L6_2atmpS1618;
      if (
        _M0L6_2atmpS1607 < 0
        || _M0L6_2atmpS1607 >= Moonbit_array_length(_M0L6resultS654)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS654[_M0L6_2atmpS1607] = _M0L6_2atmpS1608;
      _M0L6_2atmpS1613 = _M0Lm5indexS655;
      _M0L6_2atmpS1610 = _M0L6_2atmpS1613 + 1;
      _M0L6_2atmpS1612 = 48 + _M0L1bS669;
      _M0L6_2atmpS1611 = _M0L6_2atmpS1612 & 0xff;
      if (
        _M0L6_2atmpS1610 < 0
        || _M0L6_2atmpS1610 >= Moonbit_array_length(_M0L6resultS654)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS654[_M0L6_2atmpS1610] = _M0L6_2atmpS1611;
      _M0L6_2atmpS1617 = _M0Lm5indexS655;
      _M0L6_2atmpS1614 = _M0L6_2atmpS1617 + 2;
      _M0L6_2atmpS1616 = 48 + _M0L1cS670;
      _M0L6_2atmpS1615 = _M0L6_2atmpS1616 & 0xff;
      if (
        _M0L6_2atmpS1614 < 0
        || _M0L6_2atmpS1614 >= Moonbit_array_length(_M0L6resultS654)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS654[_M0L6_2atmpS1614] = _M0L6_2atmpS1615;
      _M0L6_2atmpS1618 = _M0Lm5indexS655;
      _M0Lm5indexS655 = _M0L6_2atmpS1618 + 3;
    } else {
      int32_t _M0L6_2atmpS1623 = _M0Lm3expS660;
      if (_M0L6_2atmpS1623 >= 10) {
        int32_t _M0L6_2atmpS1633 = _M0Lm3expS660;
        int32_t _M0L1aS671 = _M0L6_2atmpS1633 / 10;
        int32_t _M0L6_2atmpS1632 = _M0Lm3expS660;
        int32_t _M0L1bS672 = _M0L6_2atmpS1632 % 10;
        int32_t _M0L6_2atmpS1624 = _M0Lm5indexS655;
        int32_t _M0L6_2atmpS1626 = 48 + _M0L1aS671;
        int32_t _M0L6_2atmpS1625 = _M0L6_2atmpS1626 & 0xff;
        int32_t _M0L6_2atmpS1630;
        int32_t _M0L6_2atmpS1627;
        int32_t _M0L6_2atmpS1629;
        int32_t _M0L6_2atmpS1628;
        int32_t _M0L6_2atmpS1631;
        if (
          _M0L6_2atmpS1624 < 0
          || _M0L6_2atmpS1624 >= Moonbit_array_length(_M0L6resultS654)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS654[_M0L6_2atmpS1624] = _M0L6_2atmpS1625;
        _M0L6_2atmpS1630 = _M0Lm5indexS655;
        _M0L6_2atmpS1627 = _M0L6_2atmpS1630 + 1;
        _M0L6_2atmpS1629 = 48 + _M0L1bS672;
        _M0L6_2atmpS1628 = _M0L6_2atmpS1629 & 0xff;
        if (
          _M0L6_2atmpS1627 < 0
          || _M0L6_2atmpS1627 >= Moonbit_array_length(_M0L6resultS654)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS654[_M0L6_2atmpS1627] = _M0L6_2atmpS1628;
        _M0L6_2atmpS1631 = _M0Lm5indexS655;
        _M0Lm5indexS655 = _M0L6_2atmpS1631 + 2;
      } else {
        int32_t _M0L6_2atmpS1634 = _M0Lm5indexS655;
        int32_t _M0L6_2atmpS1637 = _M0Lm3expS660;
        int32_t _M0L6_2atmpS1636 = 48 + _M0L6_2atmpS1637;
        int32_t _M0L6_2atmpS1635 = _M0L6_2atmpS1636 & 0xff;
        int32_t _M0L6_2atmpS1638;
        if (
          _M0L6_2atmpS1634 < 0
          || _M0L6_2atmpS1634 >= Moonbit_array_length(_M0L6resultS654)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS654[_M0L6_2atmpS1634] = _M0L6_2atmpS1635;
        _M0L6_2atmpS1638 = _M0Lm5indexS655;
        _M0Lm5indexS655 = _M0L6_2atmpS1638 + 1;
      }
    }
    _M0L6_2atmpS1639 = _M0Lm5indexS655;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1967
    = _M0FPB19string__from__bytes(_M0L6resultS654, 0, _M0L6_2atmpS1639);
    moonbit_decref_cycle_free(_M0L6resultS654);
    return _result_1967;
  } else {
    int32_t _M0L6_2atmpS1648 = _M0Lm3expS660;
    int32_t _M0L6_2atmpS1711;
    moonbit_string_t _result_1973;
    if (_M0L6_2atmpS1648 < 0) {
      int32_t _M0L6_2atmpS1649 = _M0Lm5indexS655;
      int32_t _M0L6_2atmpS1651;
      int32_t _M0L6_2atmpS1650;
      int32_t _M0L6_2atmpS1652;
      int32_t _M0L1iS673;
      int32_t _M0L6_2atmpS1667;
      int32_t _M0L6_2atmpS1669;
      int32_t _M0L6_2atmpS1668;
      int32_t _M0L7currentS675;
      int32_t _M0L1iS676;
      uint64_t _M0L6outputS677;
      if (
        _M0L6_2atmpS1649 < 0
        || _M0L6_2atmpS1649 >= Moonbit_array_length(_M0L6resultS654)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS654[_M0L6_2atmpS1649] = 48;
      _M0L6_2atmpS1651 = _M0Lm5indexS655;
      _M0L6_2atmpS1650 = _M0L6_2atmpS1651 + 1;
      if (
        _M0L6_2atmpS1650 < 0
        || _M0L6_2atmpS1650 >= Moonbit_array_length(_M0L6resultS654)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS654[_M0L6_2atmpS1650] = 46;
      _M0L6_2atmpS1652 = _M0Lm5indexS655;
      _M0Lm5indexS655 = _M0L6_2atmpS1652 + 2;
      _M0L1iS673 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1653 = _M0Lm3expS660;
        if (_M0L1iS673 > _M0L6_2atmpS1653) {
          int32_t _M0L6_2atmpS1656 = _M0Lm5indexS655;
          int32_t _M0L6_2atmpS1655 = _M0L6_2atmpS1656 - _M0L1iS673;
          int32_t _M0L6_2atmpS1654 = _M0L6_2atmpS1655 - 1;
          int32_t _M0L6_2atmpS1657;
          if (
            _M0L6_2atmpS1654 < 0
            || _M0L6_2atmpS1654 >= Moonbit_array_length(_M0L6resultS654)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS654[_M0L6_2atmpS1654] = 48;
          _M0L6_2atmpS1657 = _M0L1iS673 - 1;
          _M0L1iS673 = _M0L6_2atmpS1657;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1667 = _M0Lm5indexS655;
      _M0L6_2atmpS1669 = _M0Lm3expS660;
      _M0L6_2atmpS1668 = -1 - _M0L6_2atmpS1669;
      _M0L7currentS675 = _M0L6_2atmpS1667 + _M0L6_2atmpS1668;
      _M0L1iS676 = 0;
      _M0L6outputS677 = _M0L6outputS657;
      while (1) {
        if (_M0L1iS676 < _M0L7olengthS659) {
          int32_t _M0L6_2atmpS1664 = _M0L7currentS675 + _M0L7olengthS659;
          int32_t _M0L6_2atmpS1663 = _M0L6_2atmpS1664 - _M0L1iS676;
          int32_t _M0L6_2atmpS1658 = _M0L6_2atmpS1663 - 1;
          uint64_t _M0L6_2atmpS1662 = _M0L6outputS677 % 10ull;
          int32_t _M0L6_2atmpS1661 = (int32_t)_M0L6_2atmpS1662;
          int32_t _M0L6_2atmpS1660 = 48 + _M0L6_2atmpS1661;
          int32_t _M0L6_2atmpS1659 = _M0L6_2atmpS1660 & 0xff;
          int32_t _M0L6_2atmpS1665;
          uint64_t _M0L6_2atmpS1666;
          if (
            _M0L6_2atmpS1658 < 0
            || _M0L6_2atmpS1658 >= Moonbit_array_length(_M0L6resultS654)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS654[_M0L6_2atmpS1658] = _M0L6_2atmpS1659;
          _M0L6_2atmpS1665 = _M0L1iS676 + 1;
          _M0L6_2atmpS1666 = _M0L6outputS677 / 10ull;
          _M0L1iS676 = _M0L6_2atmpS1665;
          _M0L6outputS677 = _M0L6_2atmpS1666;
          continue;
        }
        break;
      }
      _M0Lm5indexS655 = _M0L7currentS675 + _M0L7olengthS659;
    } else {
      int32_t _M0L6_2atmpS1671 = _M0Lm3expS660;
      int32_t _M0L6_2atmpS1670 = _M0L6_2atmpS1671 + 1;
      if (_M0L6_2atmpS1670 >= _M0L7olengthS659) {
        int32_t _M0L1iS679 = 0;
        uint64_t _M0L6outputS680 = _M0L6outputS657;
        int32_t _M0L6_2atmpS1682;
        int32_t _M0L6_2atmpS1687;
        int32_t _M0L7_2abindS682;
        int32_t _M0L1iS683;
        int32_t _M0L6_2atmpS1688;
        int32_t _M0L6_2atmpS1691;
        int32_t _M0L6_2atmpS1690;
        int32_t _M0L6_2atmpS1689;
        while (1) {
          if (_M0L1iS679 < _M0L7olengthS659) {
            int32_t _M0L6_2atmpS1679 = _M0Lm5indexS655;
            int32_t _M0L6_2atmpS1678 = _M0L6_2atmpS1679 + _M0L7olengthS659;
            int32_t _M0L6_2atmpS1677 = _M0L6_2atmpS1678 - _M0L1iS679;
            int32_t _M0L6_2atmpS1672 = _M0L6_2atmpS1677 - 1;
            uint64_t _M0L6_2atmpS1676 = _M0L6outputS680 % 10ull;
            int32_t _M0L6_2atmpS1675 = (int32_t)_M0L6_2atmpS1676;
            int32_t _M0L6_2atmpS1674 = 48 + _M0L6_2atmpS1675;
            int32_t _M0L6_2atmpS1673 = _M0L6_2atmpS1674 & 0xff;
            int32_t _M0L6_2atmpS1680;
            uint64_t _M0L6_2atmpS1681;
            if (
              _M0L6_2atmpS1672 < 0
              || _M0L6_2atmpS1672 >= Moonbit_array_length(_M0L6resultS654)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS654[_M0L6_2atmpS1672] = _M0L6_2atmpS1673;
            _M0L6_2atmpS1680 = _M0L1iS679 + 1;
            _M0L6_2atmpS1681 = _M0L6outputS680 / 10ull;
            _M0L1iS679 = _M0L6_2atmpS1680;
            _M0L6outputS680 = _M0L6_2atmpS1681;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1682 = _M0Lm5indexS655;
        _M0Lm5indexS655 = _M0L6_2atmpS1682 + _M0L7olengthS659;
        _M0L6_2atmpS1687 = _M0Lm3expS660;
        _M0L7_2abindS682 = _M0L6_2atmpS1687 + 1;
        _M0L1iS683 = _M0L7olengthS659;
        while (1) {
          if (_M0L1iS683 < _M0L7_2abindS682) {
            int32_t _M0L6_2atmpS1685 = _M0Lm5indexS655;
            int32_t _M0L6_2atmpS1684 = _M0L6_2atmpS1685 + _M0L1iS683;
            int32_t _M0L6_2atmpS1683 = _M0L6_2atmpS1684 - _M0L7olengthS659;
            int32_t _M0L6_2atmpS1686;
            if (
              _M0L6_2atmpS1683 < 0
              || _M0L6_2atmpS1683 >= Moonbit_array_length(_M0L6resultS654)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS654[_M0L6_2atmpS1683] = 48;
            _M0L6_2atmpS1686 = _M0L1iS683 + 1;
            _M0L1iS683 = _M0L6_2atmpS1686;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1688 = _M0Lm5indexS655;
        _M0L6_2atmpS1691 = _M0Lm3expS660;
        _M0L6_2atmpS1690 = _M0L6_2atmpS1691 + 1;
        _M0L6_2atmpS1689 = _M0L6_2atmpS1690 - _M0L7olengthS659;
        _M0Lm5indexS655 = _M0L6_2atmpS1688 + _M0L6_2atmpS1689;
      } else {
        int32_t _M0L6_2atmpS1708 = _M0Lm5indexS655;
        int32_t _M0L6_2atmpS1707 = _M0L6_2atmpS1708 + 1;
        int32_t _M0L1iS685 = 0;
        int32_t _M0L7currentS686 = _M0L6_2atmpS1707;
        uint64_t _M0L6outputS687 = _M0L6outputS657;
        int32_t _M0L6_2atmpS1709;
        int32_t _M0L6_2atmpS1710;
        while (1) {
          if (_M0L1iS685 < _M0L7olengthS659) {
            int32_t _M0L6_2atmpS1703 = _M0L7olengthS659 - _M0L1iS685;
            int32_t _M0L6_2atmpS1701 = _M0L6_2atmpS1703 - 1;
            int32_t _M0L6_2atmpS1702 = _M0Lm3expS660;
            int32_t _M0L7currentS688;
            int32_t _M0L6_2atmpS1698;
            int32_t _M0L6_2atmpS1697;
            int32_t _M0L6_2atmpS1692;
            uint64_t _M0L6_2atmpS1696;
            int32_t _M0L6_2atmpS1695;
            int32_t _M0L6_2atmpS1694;
            int32_t _M0L6_2atmpS1693;
            int32_t _M0L6_2atmpS1699;
            uint64_t _M0L6_2atmpS1700;
            if (_M0L6_2atmpS1701 == _M0L6_2atmpS1702) {
              int32_t _M0L6_2atmpS1706 = _M0L7currentS686 + _M0L7olengthS659;
              int32_t _M0L6_2atmpS1705 = _M0L6_2atmpS1706 - _M0L1iS685;
              int32_t _M0L6_2atmpS1704 = _M0L6_2atmpS1705 - 1;
              if (
                _M0L6_2atmpS1704 < 0
                || _M0L6_2atmpS1704 >= Moonbit_array_length(_M0L6resultS654)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS654[_M0L6_2atmpS1704] = 46;
              _M0L7currentS688 = _M0L7currentS686 - 1;
            } else {
              _M0L7currentS688 = _M0L7currentS686;
            }
            _M0L6_2atmpS1698 = _M0L7currentS688 + _M0L7olengthS659;
            _M0L6_2atmpS1697 = _M0L6_2atmpS1698 - _M0L1iS685;
            _M0L6_2atmpS1692 = _M0L6_2atmpS1697 - 1;
            _M0L6_2atmpS1696 = _M0L6outputS687 % 10ull;
            _M0L6_2atmpS1695 = (int32_t)_M0L6_2atmpS1696;
            _M0L6_2atmpS1694 = 48 + _M0L6_2atmpS1695;
            _M0L6_2atmpS1693 = _M0L6_2atmpS1694 & 0xff;
            if (
              _M0L6_2atmpS1692 < 0
              || _M0L6_2atmpS1692 >= Moonbit_array_length(_M0L6resultS654)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS654[_M0L6_2atmpS1692] = _M0L6_2atmpS1693;
            _M0L6_2atmpS1699 = _M0L1iS685 + 1;
            _M0L6_2atmpS1700 = _M0L6outputS687 / 10ull;
            _M0L1iS685 = _M0L6_2atmpS1699;
            _M0L7currentS686 = _M0L7currentS688;
            _M0L6outputS687 = _M0L6_2atmpS1700;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1709 = _M0Lm5indexS655;
        _M0L6_2atmpS1710 = _M0L7olengthS659 + 1;
        _M0Lm5indexS655 = _M0L6_2atmpS1709 + _M0L6_2atmpS1710;
      }
    }
    _M0L6_2atmpS1711 = _M0Lm5indexS655;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1973
    = _M0FPB19string__from__bytes(_M0L6resultS654, 0, _M0L6_2atmpS1711);
    moonbit_decref_cycle_free(_M0L6resultS654);
    return _result_1973;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS600,
  uint32_t _M0L12ieeeExponentS599
) {
  int32_t _M0Lm2e2S597;
  uint64_t _M0Lm2m2S598;
  uint64_t _M0L6_2atmpS1585;
  uint64_t _M0L6_2atmpS1584;
  int32_t _M0L4evenS601;
  uint64_t _M0L6_2atmpS1583;
  uint64_t _M0L2mvS602;
  int32_t _M0L7mmShiftS603;
  uint64_t _M0Lm2vrS604;
  uint64_t _M0Lm2vpS605;
  uint64_t _M0Lm2vmS606;
  int32_t _M0Lm3e10S607;
  int32_t _M0Lm17vmIsTrailingZerosS608;
  int32_t _M0Lm17vrIsTrailingZerosS609;
  int32_t _M0L6_2atmpS1485;
  int32_t _M0Lm7removedS628;
  int32_t _M0Lm16lastRemovedDigitS629;
  uint64_t _M0Lm6outputS630;
  int32_t _M0L6_2atmpS1581;
  int32_t _M0L6_2atmpS1582;
  int32_t _M0L3expS653;
  uint64_t _M0L6_2atmpS1580;
  struct _M0TPB17FloatingDecimal64* _block_1979;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S597 = 0;
  _M0Lm2m2S598 = 0ull;
  if (_M0L12ieeeExponentS599 == 0u) {
    _M0Lm2e2S597 = -1076;
    _M0Lm2m2S598 = _M0L12ieeeMantissaS600;
  } else {
    int32_t _M0L6_2atmpS1484 = *(int32_t*)&_M0L12ieeeExponentS599;
    int32_t _M0L6_2atmpS1483 = _M0L6_2atmpS1484 - 1023;
    int32_t _M0L6_2atmpS1482 = _M0L6_2atmpS1483 - 52;
    _M0Lm2e2S597 = _M0L6_2atmpS1482 - 2;
    _M0Lm2m2S598 = 4503599627370496ull | _M0L12ieeeMantissaS600;
  }
  _M0L6_2atmpS1585 = _M0Lm2m2S598;
  _M0L6_2atmpS1584 = _M0L6_2atmpS1585 & 1ull;
  _M0L4evenS601 = _M0L6_2atmpS1584 == 0ull;
  _M0L6_2atmpS1583 = _M0Lm2m2S598;
  _M0L2mvS602 = 4ull * _M0L6_2atmpS1583;
  _M0L7mmShiftS603
  = _M0L12ieeeMantissaS600 != 0ull || _M0L12ieeeExponentS599 <= 1u;
  _M0Lm2vrS604 = 0ull;
  _M0Lm2vpS605 = 0ull;
  _M0Lm2vmS606 = 0ull;
  _M0Lm3e10S607 = 0;
  _M0Lm17vmIsTrailingZerosS608 = 0;
  _M0Lm17vrIsTrailingZerosS609 = 0;
  _M0L6_2atmpS1485 = _M0Lm2e2S597;
  if (_M0L6_2atmpS1485 >= 0) {
    int32_t _M0L6_2atmpS1507 = _M0Lm2e2S597;
    int32_t _M0L6_2atmpS1503;
    int32_t _M0L6_2atmpS1506;
    int32_t _M0L6_2atmpS1505;
    int32_t _M0L6_2atmpS1504;
    int32_t _M0L1qS610;
    int32_t _M0L6_2atmpS1502;
    int32_t _M0L6_2atmpS1501;
    int32_t _M0L1kS611;
    int32_t _M0L6_2atmpS1500;
    int32_t _M0L6_2atmpS1499;
    int32_t _M0L6_2atmpS1498;
    int32_t _M0L1iS612;
    struct _M0TPB8Pow5Pair _M0L4pow5S613;
    uint64_t _M0L6_2atmpS1497;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS614;
    uint64_t _M0L8_2avrOutS615;
    uint64_t _M0L8_2avpOutS616;
    uint64_t _M0L8_2avmOutS617;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1503 = _M0FPB9log10Pow2(_M0L6_2atmpS1507);
    _M0L6_2atmpS1506 = _M0Lm2e2S597;
    _M0L6_2atmpS1505 = _M0L6_2atmpS1506 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1504 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1505);
    _M0L1qS610 = _M0L6_2atmpS1503 - _M0L6_2atmpS1504;
    _M0Lm3e10S607 = _M0L1qS610;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1502 = _M0FPB8pow5bits(_M0L1qS610);
    _M0L6_2atmpS1501 = 125 + _M0L6_2atmpS1502;
    _M0L1kS611 = _M0L6_2atmpS1501 - 1;
    _M0L6_2atmpS1500 = _M0Lm2e2S597;
    _M0L6_2atmpS1499 = -_M0L6_2atmpS1500;
    _M0L6_2atmpS1498 = _M0L6_2atmpS1499 + _M0L1qS610;
    _M0L1iS612 = _M0L6_2atmpS1498 + _M0L1kS611;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S613 = _M0FPB22double__computeInvPow5(_M0L1qS610);
    _M0L6_2atmpS1497 = _M0Lm2m2S598;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS614
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1497, _M0L4pow5S613, _M0L1iS612, _M0L7mmShiftS603);
    _M0L8_2avrOutS615 = _M0L7_2abindS614.$0;
    _M0L8_2avpOutS616 = _M0L7_2abindS614.$1;
    _M0L8_2avmOutS617 = _M0L7_2abindS614.$2;
    _M0Lm2vrS604 = _M0L8_2avrOutS615;
    _M0Lm2vpS605 = _M0L8_2avpOutS616;
    _M0Lm2vmS606 = _M0L8_2avmOutS617;
    if (_M0L1qS610 <= 21) {
      int32_t _M0L6_2atmpS1493 = (int32_t)_M0L2mvS602;
      uint64_t _M0L6_2atmpS1496 = _M0L2mvS602 / 5ull;
      int32_t _M0L6_2atmpS1495 = (int32_t)_M0L6_2atmpS1496;
      int32_t _M0L6_2atmpS1494 = 5 * _M0L6_2atmpS1495;
      int32_t _M0L6mvMod5S618 = _M0L6_2atmpS1493 - _M0L6_2atmpS1494;
      if (_M0L6mvMod5S618 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS609
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS602, _M0L1qS610);
      } else if (_M0L4evenS601) {
        uint64_t _M0L6_2atmpS1487 = _M0L2mvS602 - 1ull;
        uint64_t _M0L6_2atmpS1488;
        uint64_t _M0L6_2atmpS1486;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1488 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS603);
        _M0L6_2atmpS1486 = _M0L6_2atmpS1487 - _M0L6_2atmpS1488;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS608
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1486, _M0L1qS610);
      } else {
        uint64_t _M0L6_2atmpS1489 = _M0Lm2vpS605;
        uint64_t _M0L6_2atmpS1492 = _M0L2mvS602 + 2ull;
        int32_t _M0L6_2atmpS1491;
        uint64_t _M0L6_2atmpS1490;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1491
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1492, _M0L1qS610);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1490 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1491);
        _M0Lm2vpS605 = _M0L6_2atmpS1489 - _M0L6_2atmpS1490;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1521 = _M0Lm2e2S597;
    int32_t _M0L6_2atmpS1520 = -_M0L6_2atmpS1521;
    int32_t _M0L6_2atmpS1515;
    int32_t _M0L6_2atmpS1519;
    int32_t _M0L6_2atmpS1518;
    int32_t _M0L6_2atmpS1517;
    int32_t _M0L6_2atmpS1516;
    int32_t _M0L1qS619;
    int32_t _M0L6_2atmpS1508;
    int32_t _M0L6_2atmpS1514;
    int32_t _M0L6_2atmpS1513;
    int32_t _M0L1iS620;
    int32_t _M0L6_2atmpS1512;
    int32_t _M0L1kS621;
    int32_t _M0L1jS622;
    struct _M0TPB8Pow5Pair _M0L4pow5S623;
    uint64_t _M0L6_2atmpS1511;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS624;
    uint64_t _M0L8_2avrOutS625;
    uint64_t _M0L8_2avpOutS626;
    uint64_t _M0L8_2avmOutS627;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1515 = _M0FPB9log10Pow5(_M0L6_2atmpS1520);
    _M0L6_2atmpS1519 = _M0Lm2e2S597;
    _M0L6_2atmpS1518 = -_M0L6_2atmpS1519;
    _M0L6_2atmpS1517 = _M0L6_2atmpS1518 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1516 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1517);
    _M0L1qS619 = _M0L6_2atmpS1515 - _M0L6_2atmpS1516;
    _M0L6_2atmpS1508 = _M0Lm2e2S597;
    _M0Lm3e10S607 = _M0L1qS619 + _M0L6_2atmpS1508;
    _M0L6_2atmpS1514 = _M0Lm2e2S597;
    _M0L6_2atmpS1513 = -_M0L6_2atmpS1514;
    _M0L1iS620 = _M0L6_2atmpS1513 - _M0L1qS619;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1512 = _M0FPB8pow5bits(_M0L1iS620);
    _M0L1kS621 = _M0L6_2atmpS1512 - 125;
    _M0L1jS622 = _M0L1qS619 - _M0L1kS621;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S623 = _M0FPB19double__computePow5(_M0L1iS620);
    _M0L6_2atmpS1511 = _M0Lm2m2S598;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS624
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1511, _M0L4pow5S623, _M0L1jS622, _M0L7mmShiftS603);
    _M0L8_2avrOutS625 = _M0L7_2abindS624.$0;
    _M0L8_2avpOutS626 = _M0L7_2abindS624.$1;
    _M0L8_2avmOutS627 = _M0L7_2abindS624.$2;
    _M0Lm2vrS604 = _M0L8_2avrOutS625;
    _M0Lm2vpS605 = _M0L8_2avpOutS626;
    _M0Lm2vmS606 = _M0L8_2avmOutS627;
    if (_M0L1qS619 <= 1) {
      _M0Lm17vrIsTrailingZerosS609 = 1;
      if (_M0L4evenS601) {
        int32_t _M0L6_2atmpS1509;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1509 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS603);
        _M0Lm17vmIsTrailingZerosS608 = _M0L6_2atmpS1509 == 1;
      } else {
        uint64_t _M0L6_2atmpS1510 = _M0Lm2vpS605;
        _M0Lm2vpS605 = _M0L6_2atmpS1510 - 1ull;
      }
    } else if (_M0L1qS619 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS609
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS602, _M0L1qS619);
    }
  }
  _M0Lm7removedS628 = 0;
  _M0Lm16lastRemovedDigitS629 = 0;
  _M0Lm6outputS630 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS608 || _M0Lm17vrIsTrailingZerosS609) {
    int32_t _if__result_1976;
    uint64_t _M0L6_2atmpS1551;
    uint64_t _M0L6_2atmpS1557;
    uint64_t _M0L6_2atmpS1558;
    int32_t _if__result_1977;
    int32_t _M0L6_2atmpS1554;
    int64_t _M0L6_2atmpS1553;
    uint64_t _M0L6_2atmpS1552;
    while (1) {
      uint64_t _M0L6_2atmpS1534 = _M0Lm2vpS605;
      uint64_t _M0L7vpDiv10S631 = _M0L6_2atmpS1534 / 10ull;
      uint64_t _M0L6_2atmpS1533 = _M0Lm2vmS606;
      uint64_t _M0L7vmDiv10S632 = _M0L6_2atmpS1533 / 10ull;
      uint64_t _M0L6_2atmpS1532;
      int32_t _M0L6_2atmpS1529;
      int32_t _M0L6_2atmpS1531;
      int32_t _M0L6_2atmpS1530;
      int32_t _M0L7vmMod10S634;
      uint64_t _M0L6_2atmpS1528;
      uint64_t _M0L7vrDiv10S635;
      uint64_t _M0L6_2atmpS1527;
      int32_t _M0L6_2atmpS1524;
      int32_t _M0L6_2atmpS1526;
      int32_t _M0L6_2atmpS1525;
      int32_t _M0L7vrMod10S636;
      int32_t _M0L6_2atmpS1523;
      if (_M0L7vpDiv10S631 <= _M0L7vmDiv10S632) {
        break;
      }
      _M0L6_2atmpS1532 = _M0Lm2vmS606;
      _M0L6_2atmpS1529 = (int32_t)_M0L6_2atmpS1532;
      _M0L6_2atmpS1531 = (int32_t)_M0L7vmDiv10S632;
      _M0L6_2atmpS1530 = 10 * _M0L6_2atmpS1531;
      _M0L7vmMod10S634 = _M0L6_2atmpS1529 - _M0L6_2atmpS1530;
      _M0L6_2atmpS1528 = _M0Lm2vrS604;
      _M0L7vrDiv10S635 = _M0L6_2atmpS1528 / 10ull;
      _M0L6_2atmpS1527 = _M0Lm2vrS604;
      _M0L6_2atmpS1524 = (int32_t)_M0L6_2atmpS1527;
      _M0L6_2atmpS1526 = (int32_t)_M0L7vrDiv10S635;
      _M0L6_2atmpS1525 = 10 * _M0L6_2atmpS1526;
      _M0L7vrMod10S636 = _M0L6_2atmpS1524 - _M0L6_2atmpS1525;
      _M0Lm17vmIsTrailingZerosS608
      = _M0Lm17vmIsTrailingZerosS608 && _M0L7vmMod10S634 == 0;
      if (_M0Lm17vrIsTrailingZerosS609) {
        int32_t _M0L6_2atmpS1522 = _M0Lm16lastRemovedDigitS629;
        _M0Lm17vrIsTrailingZerosS609 = _M0L6_2atmpS1522 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS609 = 0;
      }
      _M0Lm16lastRemovedDigitS629 = _M0L7vrMod10S636;
      _M0Lm2vrS604 = _M0L7vrDiv10S635;
      _M0Lm2vpS605 = _M0L7vpDiv10S631;
      _M0Lm2vmS606 = _M0L7vmDiv10S632;
      _M0L6_2atmpS1523 = _M0Lm7removedS628;
      _M0Lm7removedS628 = _M0L6_2atmpS1523 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS608) {
      while (1) {
        uint64_t _M0L6_2atmpS1547 = _M0Lm2vmS606;
        uint64_t _M0L7vmDiv10S637 = _M0L6_2atmpS1547 / 10ull;
        uint64_t _M0L6_2atmpS1546 = _M0Lm2vmS606;
        int32_t _M0L6_2atmpS1543 = (int32_t)_M0L6_2atmpS1546;
        int32_t _M0L6_2atmpS1545 = (int32_t)_M0L7vmDiv10S637;
        int32_t _M0L6_2atmpS1544 = 10 * _M0L6_2atmpS1545;
        int32_t _M0L7vmMod10S638 = _M0L6_2atmpS1543 - _M0L6_2atmpS1544;
        uint64_t _M0L6_2atmpS1542;
        uint64_t _M0L7vpDiv10S640;
        uint64_t _M0L6_2atmpS1541;
        uint64_t _M0L7vrDiv10S641;
        uint64_t _M0L6_2atmpS1540;
        int32_t _M0L6_2atmpS1537;
        int32_t _M0L6_2atmpS1539;
        int32_t _M0L6_2atmpS1538;
        int32_t _M0L7vrMod10S642;
        int32_t _M0L6_2atmpS1536;
        if (_M0L7vmMod10S638 != 0) {
          break;
        }
        _M0L6_2atmpS1542 = _M0Lm2vpS605;
        _M0L7vpDiv10S640 = _M0L6_2atmpS1542 / 10ull;
        _M0L6_2atmpS1541 = _M0Lm2vrS604;
        _M0L7vrDiv10S641 = _M0L6_2atmpS1541 / 10ull;
        _M0L6_2atmpS1540 = _M0Lm2vrS604;
        _M0L6_2atmpS1537 = (int32_t)_M0L6_2atmpS1540;
        _M0L6_2atmpS1539 = (int32_t)_M0L7vrDiv10S641;
        _M0L6_2atmpS1538 = 10 * _M0L6_2atmpS1539;
        _M0L7vrMod10S642 = _M0L6_2atmpS1537 - _M0L6_2atmpS1538;
        if (_M0Lm17vrIsTrailingZerosS609) {
          int32_t _M0L6_2atmpS1535 = _M0Lm16lastRemovedDigitS629;
          _M0Lm17vrIsTrailingZerosS609 = _M0L6_2atmpS1535 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS609 = 0;
        }
        _M0Lm16lastRemovedDigitS629 = _M0L7vrMod10S642;
        _M0Lm2vrS604 = _M0L7vrDiv10S641;
        _M0Lm2vpS605 = _M0L7vpDiv10S640;
        _M0Lm2vmS606 = _M0L7vmDiv10S637;
        _M0L6_2atmpS1536 = _M0Lm7removedS628;
        _M0Lm7removedS628 = _M0L6_2atmpS1536 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS609) {
      int32_t _M0L6_2atmpS1550 = _M0Lm16lastRemovedDigitS629;
      if (_M0L6_2atmpS1550 == 5) {
        uint64_t _M0L6_2atmpS1549 = _M0Lm2vrS604;
        uint64_t _M0L6_2atmpS1548 = _M0L6_2atmpS1549 % 2ull;
        _if__result_1976 = _M0L6_2atmpS1548 == 0ull;
      } else {
        _if__result_1976 = 0;
      }
    } else {
      _if__result_1976 = 0;
    }
    if (_if__result_1976) {
      _M0Lm16lastRemovedDigitS629 = 4;
    }
    _M0L6_2atmpS1551 = _M0Lm2vrS604;
    _M0L6_2atmpS1557 = _M0Lm2vrS604;
    _M0L6_2atmpS1558 = _M0Lm2vmS606;
    if (_M0L6_2atmpS1557 == _M0L6_2atmpS1558) {
      if (!_M0L4evenS601) {
        _if__result_1977 = 1;
      } else {
        int32_t _M0L6_2atmpS1556 = _M0Lm17vmIsTrailingZerosS608;
        _if__result_1977 = !_M0L6_2atmpS1556;
      }
    } else {
      _if__result_1977 = 0;
    }
    if (_if__result_1977) {
      _M0L6_2atmpS1554 = 1;
    } else {
      int32_t _M0L6_2atmpS1555 = _M0Lm16lastRemovedDigitS629;
      _M0L6_2atmpS1554 = _M0L6_2atmpS1555 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1553 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1554);
    _M0L6_2atmpS1552 = *(uint64_t*)&_M0L6_2atmpS1553;
    _M0Lm6outputS630 = _M0L6_2atmpS1551 + _M0L6_2atmpS1552;
  } else {
    int32_t _M0Lm7roundUpS643 = 0;
    uint64_t _M0L6_2atmpS1579 = _M0Lm2vpS605;
    uint64_t _M0L8vpDiv100S644 = _M0L6_2atmpS1579 / 100ull;
    uint64_t _M0L6_2atmpS1578 = _M0Lm2vmS606;
    uint64_t _M0L8vmDiv100S645 = _M0L6_2atmpS1578 / 100ull;
    uint64_t _M0L6_2atmpS1573;
    uint64_t _M0L6_2atmpS1576;
    uint64_t _M0L6_2atmpS1577;
    int32_t _M0L6_2atmpS1575;
    uint64_t _M0L6_2atmpS1574;
    if (_M0L8vpDiv100S644 > _M0L8vmDiv100S645) {
      uint64_t _M0L6_2atmpS1564 = _M0Lm2vrS604;
      uint64_t _M0L8vrDiv100S646 = _M0L6_2atmpS1564 / 100ull;
      uint64_t _M0L6_2atmpS1563 = _M0Lm2vrS604;
      int32_t _M0L6_2atmpS1560 = (int32_t)_M0L6_2atmpS1563;
      int32_t _M0L6_2atmpS1562 = (int32_t)_M0L8vrDiv100S646;
      int32_t _M0L6_2atmpS1561 = 100 * _M0L6_2atmpS1562;
      int32_t _M0L8vrMod100S647 = _M0L6_2atmpS1560 - _M0L6_2atmpS1561;
      int32_t _M0L6_2atmpS1559;
      _M0Lm7roundUpS643 = _M0L8vrMod100S647 >= 50;
      _M0Lm2vrS604 = _M0L8vrDiv100S646;
      _M0Lm2vpS605 = _M0L8vpDiv100S644;
      _M0Lm2vmS606 = _M0L8vmDiv100S645;
      _M0L6_2atmpS1559 = _M0Lm7removedS628;
      _M0Lm7removedS628 = _M0L6_2atmpS1559 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1572 = _M0Lm2vpS605;
      uint64_t _M0L7vpDiv10S648 = _M0L6_2atmpS1572 / 10ull;
      uint64_t _M0L6_2atmpS1571 = _M0Lm2vmS606;
      uint64_t _M0L7vmDiv10S649 = _M0L6_2atmpS1571 / 10ull;
      uint64_t _M0L6_2atmpS1570;
      uint64_t _M0L7vrDiv10S651;
      uint64_t _M0L6_2atmpS1569;
      int32_t _M0L6_2atmpS1566;
      int32_t _M0L6_2atmpS1568;
      int32_t _M0L6_2atmpS1567;
      int32_t _M0L7vrMod10S652;
      int32_t _M0L6_2atmpS1565;
      if (_M0L7vpDiv10S648 <= _M0L7vmDiv10S649) {
        break;
      }
      _M0L6_2atmpS1570 = _M0Lm2vrS604;
      _M0L7vrDiv10S651 = _M0L6_2atmpS1570 / 10ull;
      _M0L6_2atmpS1569 = _M0Lm2vrS604;
      _M0L6_2atmpS1566 = (int32_t)_M0L6_2atmpS1569;
      _M0L6_2atmpS1568 = (int32_t)_M0L7vrDiv10S651;
      _M0L6_2atmpS1567 = 10 * _M0L6_2atmpS1568;
      _M0L7vrMod10S652 = _M0L6_2atmpS1566 - _M0L6_2atmpS1567;
      _M0Lm7roundUpS643 = _M0L7vrMod10S652 >= 5;
      _M0Lm2vrS604 = _M0L7vrDiv10S651;
      _M0Lm2vpS605 = _M0L7vpDiv10S648;
      _M0Lm2vmS606 = _M0L7vmDiv10S649;
      _M0L6_2atmpS1565 = _M0Lm7removedS628;
      _M0Lm7removedS628 = _M0L6_2atmpS1565 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1573 = _M0Lm2vrS604;
    _M0L6_2atmpS1576 = _M0Lm2vrS604;
    _M0L6_2atmpS1577 = _M0Lm2vmS606;
    _M0L6_2atmpS1575
    = _M0L6_2atmpS1576 == _M0L6_2atmpS1577 || _M0Lm7roundUpS643;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1574 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1575);
    _M0Lm6outputS630 = _M0L6_2atmpS1573 + _M0L6_2atmpS1574;
  }
  _M0L6_2atmpS1581 = _M0Lm3e10S607;
  _M0L6_2atmpS1582 = _M0Lm7removedS628;
  _M0L3expS653 = _M0L6_2atmpS1581 + _M0L6_2atmpS1582;
  _M0L6_2atmpS1580 = _M0Lm6outputS630;
  _block_1979
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_1979)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1979->$0 = _M0L6_2atmpS1580;
  _block_1979->$1 = _M0L3expS653;
  return _block_1979;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS596) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS596) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS595) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS595) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS594) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS594) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS593) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS593 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS593 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS593 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS593 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS593 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS593 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS593 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS593 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS593 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS593 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS593 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS593 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS593 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS593 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS593 >= 100ull) {
    return 3;
  }
  if (_M0L1vS593 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS576) {
  int32_t _M0L6_2atmpS1481;
  int32_t _M0L6_2atmpS1480;
  int32_t _M0L4baseS575;
  int32_t _M0L5base2S577;
  int32_t _M0L6offsetS578;
  int32_t _M0L6_2atmpS1479;
  uint64_t _M0L4mul0S579;
  int32_t _M0L6_2atmpS1478;
  int32_t _M0L6_2atmpS1477;
  uint64_t _M0L4mul1S580;
  uint64_t _M0L1mS581;
  struct _M0TPB7Umul128 _M0L7_2abindS582;
  uint64_t _M0L7_2alow1S583;
  uint64_t _M0L8_2ahigh1S584;
  struct _M0TPB7Umul128 _M0L7_2abindS585;
  uint64_t _M0L7_2alow0S586;
  uint64_t _M0L8_2ahigh0S587;
  uint64_t _M0L3sumS588;
  uint64_t _M0Lm5high1S589;
  int32_t _M0L6_2atmpS1475;
  int32_t _M0L6_2atmpS1476;
  int32_t _M0L5deltaS590;
  uint64_t _M0L6_2atmpS1474;
  uint64_t _M0L6_2atmpS1466;
  int32_t _M0L6_2atmpS1473;
  uint32_t _M0L6_2atmpS1470;
  int32_t _M0L6_2atmpS1472;
  int32_t _M0L6_2atmpS1471;
  uint32_t _M0L6_2atmpS1469;
  uint32_t _M0L6_2atmpS1468;
  uint64_t _M0L6_2atmpS1467;
  uint64_t _M0L1aS591;
  uint64_t _M0L6_2atmpS1465;
  uint64_t _M0L1bS592;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1481 = _M0L1iS576 + 26;
  _M0L6_2atmpS1480 = _M0L6_2atmpS1481 - 1;
  _M0L4baseS575 = _M0L6_2atmpS1480 / 26;
  _M0L5base2S577 = _M0L4baseS575 * 26;
  _M0L6offsetS578 = _M0L5base2S577 - _M0L1iS576;
  _M0L6_2atmpS1479 = _M0L4baseS575 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S579
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1479);
  _M0L6_2atmpS1478 = _M0L4baseS575 * 2;
  _M0L6_2atmpS1477 = _M0L6_2atmpS1478 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S580
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1477);
  if (_M0L6offsetS578 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S579, .$1 = _M0L4mul1S580};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS581
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS578);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS582 = _M0FPB7umul128(_M0L1mS581, _M0L4mul1S580);
  _M0L7_2alow1S583 = _M0L7_2abindS582.$0;
  _M0L8_2ahigh1S584 = _M0L7_2abindS582.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS585 = _M0FPB7umul128(_M0L1mS581, _M0L4mul0S579);
  _M0L7_2alow0S586 = _M0L7_2abindS585.$0;
  _M0L8_2ahigh0S587 = _M0L7_2abindS585.$1;
  _M0L3sumS588 = _M0L8_2ahigh0S587 + _M0L7_2alow1S583;
  _M0Lm5high1S589 = _M0L8_2ahigh1S584;
  if (_M0L3sumS588 < _M0L8_2ahigh0S587) {
    uint64_t _M0L6_2atmpS1464 = _M0Lm5high1S589;
    _M0Lm5high1S589 = _M0L6_2atmpS1464 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1475 = _M0FPB8pow5bits(_M0L5base2S577);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1476 = _M0FPB8pow5bits(_M0L1iS576);
  _M0L5deltaS590 = _M0L6_2atmpS1475 - _M0L6_2atmpS1476;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1474
  = _M0FPB13shiftright128(_M0L7_2alow0S586, _M0L3sumS588, _M0L5deltaS590);
  _M0L6_2atmpS1466 = _M0L6_2atmpS1474 + 1ull;
  _M0L6_2atmpS1473 = _M0L1iS576 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1470
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1473);
  _M0L6_2atmpS1472 = _M0L1iS576 % 16;
  _M0L6_2atmpS1471 = _M0L6_2atmpS1472 << 1;
  _M0L6_2atmpS1469 = _M0L6_2atmpS1470 >> (_M0L6_2atmpS1471 & 31);
  _M0L6_2atmpS1468 = _M0L6_2atmpS1469 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1467 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1468);
  _M0L1aS591 = _M0L6_2atmpS1466 + _M0L6_2atmpS1467;
  _M0L6_2atmpS1465 = _M0Lm5high1S589;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS592
  = _M0FPB13shiftright128(_M0L3sumS588, _M0L6_2atmpS1465, _M0L5deltaS590);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS591, .$1 = _M0L1bS592};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS558) {
  int32_t _M0L4baseS557;
  int32_t _M0L5base2S559;
  int32_t _M0L6offsetS560;
  int32_t _M0L6_2atmpS1463;
  uint64_t _M0L4mul0S561;
  int32_t _M0L6_2atmpS1462;
  int32_t _M0L6_2atmpS1461;
  uint64_t _M0L4mul1S562;
  uint64_t _M0L1mS563;
  struct _M0TPB7Umul128 _M0L7_2abindS564;
  uint64_t _M0L7_2alow1S565;
  uint64_t _M0L8_2ahigh1S566;
  struct _M0TPB7Umul128 _M0L7_2abindS567;
  uint64_t _M0L7_2alow0S568;
  uint64_t _M0L8_2ahigh0S569;
  uint64_t _M0L3sumS570;
  uint64_t _M0Lm5high1S571;
  int32_t _M0L6_2atmpS1459;
  int32_t _M0L6_2atmpS1460;
  int32_t _M0L5deltaS572;
  uint64_t _M0L6_2atmpS1451;
  int32_t _M0L6_2atmpS1458;
  uint32_t _M0L6_2atmpS1455;
  int32_t _M0L6_2atmpS1457;
  int32_t _M0L6_2atmpS1456;
  uint32_t _M0L6_2atmpS1454;
  uint32_t _M0L6_2atmpS1453;
  uint64_t _M0L6_2atmpS1452;
  uint64_t _M0L1aS573;
  uint64_t _M0L6_2atmpS1450;
  uint64_t _M0L1bS574;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS557 = _M0L1iS558 / 26;
  _M0L5base2S559 = _M0L4baseS557 * 26;
  _M0L6offsetS560 = _M0L1iS558 - _M0L5base2S559;
  _M0L6_2atmpS1463 = _M0L4baseS557 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S561
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1463);
  _M0L6_2atmpS1462 = _M0L4baseS557 * 2;
  _M0L6_2atmpS1461 = _M0L6_2atmpS1462 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S562
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1461);
  if (_M0L6offsetS560 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S561, .$1 = _M0L4mul1S562};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS563
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS560);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS564 = _M0FPB7umul128(_M0L1mS563, _M0L4mul1S562);
  _M0L7_2alow1S565 = _M0L7_2abindS564.$0;
  _M0L8_2ahigh1S566 = _M0L7_2abindS564.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS567 = _M0FPB7umul128(_M0L1mS563, _M0L4mul0S561);
  _M0L7_2alow0S568 = _M0L7_2abindS567.$0;
  _M0L8_2ahigh0S569 = _M0L7_2abindS567.$1;
  _M0L3sumS570 = _M0L8_2ahigh0S569 + _M0L7_2alow1S565;
  _M0Lm5high1S571 = _M0L8_2ahigh1S566;
  if (_M0L3sumS570 < _M0L8_2ahigh0S569) {
    uint64_t _M0L6_2atmpS1449 = _M0Lm5high1S571;
    _M0Lm5high1S571 = _M0L6_2atmpS1449 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1459 = _M0FPB8pow5bits(_M0L1iS558);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1460 = _M0FPB8pow5bits(_M0L5base2S559);
  _M0L5deltaS572 = _M0L6_2atmpS1459 - _M0L6_2atmpS1460;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1451
  = _M0FPB13shiftright128(_M0L7_2alow0S568, _M0L3sumS570, _M0L5deltaS572);
  _M0L6_2atmpS1458 = _M0L1iS558 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1455
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1458);
  _M0L6_2atmpS1457 = _M0L1iS558 % 16;
  _M0L6_2atmpS1456 = _M0L6_2atmpS1457 << 1;
  _M0L6_2atmpS1454 = _M0L6_2atmpS1455 >> (_M0L6_2atmpS1456 & 31);
  _M0L6_2atmpS1453 = _M0L6_2atmpS1454 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1452 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1453);
  _M0L1aS573 = _M0L6_2atmpS1451 + _M0L6_2atmpS1452;
  _M0L6_2atmpS1450 = _M0Lm5high1S571;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS574
  = _M0FPB13shiftright128(_M0L3sumS570, _M0L6_2atmpS1450, _M0L5deltaS572);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS573, .$1 = _M0L1bS574};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS531,
  struct _M0TPB8Pow5Pair _M0L3mulS528,
  int32_t _M0L1jS544,
  int32_t _M0L7mmShiftS546
) {
  uint64_t _M0L7_2amul0S527;
  uint64_t _M0L7_2amul1S529;
  uint64_t _M0L1mS530;
  struct _M0TPB7Umul128 _M0L7_2abindS532;
  uint64_t _M0L5_2aloS533;
  uint64_t _M0L6_2atmpS534;
  struct _M0TPB7Umul128 _M0L7_2abindS535;
  uint64_t _M0L6_2alo2S536;
  uint64_t _M0L6_2ahi2S537;
  uint64_t _M0L3midS538;
  uint64_t _M0L6_2atmpS1448;
  uint64_t _M0L2hiS539;
  uint64_t _M0L3lo2S540;
  uint64_t _M0L6_2atmpS1446;
  uint64_t _M0L6_2atmpS1447;
  uint64_t _M0L4mid2S541;
  uint64_t _M0L6_2atmpS1445;
  uint64_t _M0L3hi2S542;
  int32_t _M0L6_2atmpS1444;
  int32_t _M0L6_2atmpS1443;
  uint64_t _M0L2vpS543;
  uint64_t _M0Lm2vmS545;
  int32_t _M0L6_2atmpS1442;
  int32_t _M0L6_2atmpS1441;
  uint64_t _M0L2vrS556;
  uint64_t _M0L6_2atmpS1440;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S527 = _M0L3mulS528.$0;
  _M0L7_2amul1S529 = _M0L3mulS528.$1;
  _M0L1mS530 = _M0L1mS531 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS532 = _M0FPB7umul128(_M0L1mS530, _M0L7_2amul0S527);
  _M0L5_2aloS533 = _M0L7_2abindS532.$0;
  _M0L6_2atmpS534 = _M0L7_2abindS532.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS535 = _M0FPB7umul128(_M0L1mS530, _M0L7_2amul1S529);
  _M0L6_2alo2S536 = _M0L7_2abindS535.$0;
  _M0L6_2ahi2S537 = _M0L7_2abindS535.$1;
  _M0L3midS538 = _M0L6_2atmpS534 + _M0L6_2alo2S536;
  if (_M0L3midS538 < _M0L6_2atmpS534) {
    _M0L6_2atmpS1448 = 1ull;
  } else {
    _M0L6_2atmpS1448 = 0ull;
  }
  _M0L2hiS539 = _M0L6_2ahi2S537 + _M0L6_2atmpS1448;
  _M0L3lo2S540 = _M0L5_2aloS533 + _M0L7_2amul0S527;
  _M0L6_2atmpS1446 = _M0L3midS538 + _M0L7_2amul1S529;
  if (_M0L3lo2S540 < _M0L5_2aloS533) {
    _M0L6_2atmpS1447 = 1ull;
  } else {
    _M0L6_2atmpS1447 = 0ull;
  }
  _M0L4mid2S541 = _M0L6_2atmpS1446 + _M0L6_2atmpS1447;
  if (_M0L4mid2S541 < _M0L3midS538) {
    _M0L6_2atmpS1445 = 1ull;
  } else {
    _M0L6_2atmpS1445 = 0ull;
  }
  _M0L3hi2S542 = _M0L2hiS539 + _M0L6_2atmpS1445;
  _M0L6_2atmpS1444 = _M0L1jS544 - 64;
  _M0L6_2atmpS1443 = _M0L6_2atmpS1444 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS543
  = _M0FPB13shiftright128(_M0L4mid2S541, _M0L3hi2S542, _M0L6_2atmpS1443);
  _M0Lm2vmS545 = 0ull;
  if (_M0L7mmShiftS546) {
    uint64_t _M0L3lo3S547 = _M0L5_2aloS533 - _M0L7_2amul0S527;
    uint64_t _M0L6_2atmpS1430 = _M0L3midS538 - _M0L7_2amul1S529;
    uint64_t _M0L6_2atmpS1431;
    uint64_t _M0L4mid3S548;
    uint64_t _M0L6_2atmpS1429;
    uint64_t _M0L3hi3S549;
    int32_t _M0L6_2atmpS1428;
    int32_t _M0L6_2atmpS1427;
    if (_M0L5_2aloS533 < _M0L3lo3S547) {
      _M0L6_2atmpS1431 = 1ull;
    } else {
      _M0L6_2atmpS1431 = 0ull;
    }
    _M0L4mid3S548 = _M0L6_2atmpS1430 - _M0L6_2atmpS1431;
    if (_M0L3midS538 < _M0L4mid3S548) {
      _M0L6_2atmpS1429 = 1ull;
    } else {
      _M0L6_2atmpS1429 = 0ull;
    }
    _M0L3hi3S549 = _M0L2hiS539 - _M0L6_2atmpS1429;
    _M0L6_2atmpS1428 = _M0L1jS544 - 64;
    _M0L6_2atmpS1427 = _M0L6_2atmpS1428 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS545
    = _M0FPB13shiftright128(_M0L4mid3S548, _M0L3hi3S549, _M0L6_2atmpS1427);
  } else {
    uint64_t _M0L3lo3S550 = _M0L5_2aloS533 + _M0L5_2aloS533;
    uint64_t _M0L6_2atmpS1438 = _M0L3midS538 + _M0L3midS538;
    uint64_t _M0L6_2atmpS1439;
    uint64_t _M0L4mid3S551;
    uint64_t _M0L6_2atmpS1436;
    uint64_t _M0L6_2atmpS1437;
    uint64_t _M0L3hi3S552;
    uint64_t _M0L3lo4S553;
    uint64_t _M0L6_2atmpS1434;
    uint64_t _M0L6_2atmpS1435;
    uint64_t _M0L4mid4S554;
    uint64_t _M0L6_2atmpS1433;
    uint64_t _M0L3hi4S555;
    int32_t _M0L6_2atmpS1432;
    if (_M0L3lo3S550 < _M0L5_2aloS533) {
      _M0L6_2atmpS1439 = 1ull;
    } else {
      _M0L6_2atmpS1439 = 0ull;
    }
    _M0L4mid3S551 = _M0L6_2atmpS1438 + _M0L6_2atmpS1439;
    _M0L6_2atmpS1436 = _M0L2hiS539 + _M0L2hiS539;
    if (_M0L4mid3S551 < _M0L3midS538) {
      _M0L6_2atmpS1437 = 1ull;
    } else {
      _M0L6_2atmpS1437 = 0ull;
    }
    _M0L3hi3S552 = _M0L6_2atmpS1436 + _M0L6_2atmpS1437;
    _M0L3lo4S553 = _M0L3lo3S550 - _M0L7_2amul0S527;
    _M0L6_2atmpS1434 = _M0L4mid3S551 - _M0L7_2amul1S529;
    if (_M0L3lo3S550 < _M0L3lo4S553) {
      _M0L6_2atmpS1435 = 1ull;
    } else {
      _M0L6_2atmpS1435 = 0ull;
    }
    _M0L4mid4S554 = _M0L6_2atmpS1434 - _M0L6_2atmpS1435;
    if (_M0L4mid3S551 < _M0L4mid4S554) {
      _M0L6_2atmpS1433 = 1ull;
    } else {
      _M0L6_2atmpS1433 = 0ull;
    }
    _M0L3hi4S555 = _M0L3hi3S552 - _M0L6_2atmpS1433;
    _M0L6_2atmpS1432 = _M0L1jS544 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS545
    = _M0FPB13shiftright128(_M0L4mid4S554, _M0L3hi4S555, _M0L6_2atmpS1432);
  }
  _M0L6_2atmpS1442 = _M0L1jS544 - 64;
  _M0L6_2atmpS1441 = _M0L6_2atmpS1442 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS556
  = _M0FPB13shiftright128(_M0L3midS538, _M0L2hiS539, _M0L6_2atmpS1441);
  _M0L6_2atmpS1440 = _M0Lm2vmS545;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS556,
                                                .$1 = _M0L2vpS543,
                                                .$2 = _M0L6_2atmpS1440};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS525,
  int32_t _M0L1pS526
) {
  uint64_t _M0L6_2atmpS1426;
  uint64_t _M0L6_2atmpS1425;
  uint64_t _M0L6_2atmpS1424;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1426 = 1ull << (_M0L1pS526 & 63);
  _M0L6_2atmpS1425 = _M0L6_2atmpS1426 - 1ull;
  _M0L6_2atmpS1424 = _M0L5valueS525 & _M0L6_2atmpS1425;
  return _M0L6_2atmpS1424 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS523,
  int32_t _M0L1pS524
) {
  int32_t _M0L6_2atmpS1423;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1423 = _M0FPB10pow5Factor(_M0L5valueS523);
  return _M0L6_2atmpS1423 >= _M0L1pS524;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS518) {
  uint64_t _M0L6_2atmpS1414;
  uint64_t _M0L6_2atmpS1415;
  uint64_t _M0L6_2atmpS1416;
  uint64_t _M0L6_2atmpS1417;
  uint64_t _M0L6_2atmpS1422;
  int32_t _M0L5countS519;
  uint64_t _M0L1vS520;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1414 = _M0L5valueS518 % 5ull;
  if (_M0L6_2atmpS1414 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1415 = _M0L5valueS518 % 25ull;
  if (_M0L6_2atmpS1415 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1416 = _M0L5valueS518 % 125ull;
  if (_M0L6_2atmpS1416 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1417 = _M0L5valueS518 % 625ull;
  if (_M0L6_2atmpS1417 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1422 = _M0L5valueS518 / 625ull;
  _M0L5countS519 = 4;
  _M0L1vS520 = _M0L6_2atmpS1422;
  while (1) {
    if (_M0L1vS520 > 0ull) {
      uint64_t _M0L6_2atmpS1418 = _M0L1vS520 % 5ull;
      int32_t _M0L6_2atmpS1419;
      uint64_t _M0L6_2atmpS1420;
      if (_M0L6_2atmpS1418 != 0ull) {
        return _M0L5countS519;
      }
      _M0L6_2atmpS1419 = _M0L5countS519 + 1;
      _M0L6_2atmpS1420 = _M0L1vS520 / 5ull;
      _M0L5countS519 = _M0L6_2atmpS1419;
      _M0L1vS520 = _M0L6_2atmpS1420;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS522;
      moonbit_string_t _M0L6_2atmpS1421;
      int32_t _result_1981;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS522
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS522, (moonbit_string_t)moonbit_string_literal_10.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS522, _M0L5valueS518);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1421
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS522);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS522);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_1981 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1421);
      moonbit_decref_cycle_free(_M0L6_2atmpS1421);
      return _result_1981;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS517,
  uint64_t _M0L2hiS515,
  int32_t _M0L4distS516
) {
  int32_t _M0L6_2atmpS1413;
  uint64_t _M0L6_2atmpS1411;
  uint64_t _M0L6_2atmpS1412;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1413 = 64 - _M0L4distS516;
  _M0L6_2atmpS1411 = _M0L2hiS515 << (_M0L6_2atmpS1413 & 63);
  _M0L6_2atmpS1412 = _M0L2loS517 >> (_M0L4distS516 & 63);
  return _M0L6_2atmpS1411 | _M0L6_2atmpS1412;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS505,
  uint64_t _M0L1bS508
) {
  uint64_t _M0L3aLoS504;
  uint64_t _M0L3aHiS506;
  uint64_t _M0L3bLoS507;
  uint64_t _M0L3bHiS509;
  uint64_t _M0L1xS510;
  uint64_t _M0L6_2atmpS1409;
  uint64_t _M0L6_2atmpS1410;
  uint64_t _M0L1yS511;
  uint64_t _M0L6_2atmpS1407;
  uint64_t _M0L6_2atmpS1408;
  uint64_t _M0L1zS512;
  uint64_t _M0L6_2atmpS1405;
  uint64_t _M0L6_2atmpS1406;
  uint64_t _M0L6_2atmpS1403;
  uint64_t _M0L6_2atmpS1404;
  uint64_t _M0L1wS513;
  uint64_t _M0L2loS514;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS504 = _M0L1aS505 & 4294967295ull;
  _M0L3aHiS506 = _M0L1aS505 >> 32;
  _M0L3bLoS507 = _M0L1bS508 & 4294967295ull;
  _M0L3bHiS509 = _M0L1bS508 >> 32;
  _M0L1xS510 = _M0L3aLoS504 * _M0L3bLoS507;
  _M0L6_2atmpS1409 = _M0L3aHiS506 * _M0L3bLoS507;
  _M0L6_2atmpS1410 = _M0L1xS510 >> 32;
  _M0L1yS511 = _M0L6_2atmpS1409 + _M0L6_2atmpS1410;
  _M0L6_2atmpS1407 = _M0L3aLoS504 * _M0L3bHiS509;
  _M0L6_2atmpS1408 = _M0L1yS511 & 4294967295ull;
  _M0L1zS512 = _M0L6_2atmpS1407 + _M0L6_2atmpS1408;
  _M0L6_2atmpS1405 = _M0L3aHiS506 * _M0L3bHiS509;
  _M0L6_2atmpS1406 = _M0L1yS511 >> 32;
  _M0L6_2atmpS1403 = _M0L6_2atmpS1405 + _M0L6_2atmpS1406;
  _M0L6_2atmpS1404 = _M0L1zS512 >> 32;
  _M0L1wS513 = _M0L6_2atmpS1403 + _M0L6_2atmpS1404;
  _M0L2loS514 = _M0L1aS505 * _M0L1bS508;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS514, .$1 = _M0L1wS513};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS502,
  int32_t _M0L4fromS499,
  int32_t _M0L2toS498
) {
  int32_t _M0L3lenS497;
  int32_t _M0L6_2atmpS1402;
  uint16_t* _M0L6bufferS500;
  int32_t _M0L1iS501;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS497 = _M0L2toS498 - _M0L4fromS499;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1402 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS500
  = (uint16_t*)moonbit_make_string(_M0L3lenS497, _M0L6_2atmpS1402);
  _M0L1iS501 = 0;
  while (1) {
    if (_M0L1iS501 < _M0L3lenS497) {
      int32_t _M0L6_2atmpS1400 = _M0L4fromS499 + _M0L1iS501;
      int32_t _M0L6_2atmpS1399;
      int32_t _M0L6_2atmpS1398;
      int32_t _M0L6_2atmpS1401;
      if (
        _M0L6_2atmpS1400 < 0
        || _M0L6_2atmpS1400 >= Moonbit_array_length(_M0L5bytesS502)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1399 = (int32_t)_M0L5bytesS502[_M0L6_2atmpS1400];
      _M0L6_2atmpS1398 = (uint16_t)_M0L6_2atmpS1399;
      if (
        _M0L1iS501 < 0 || _M0L1iS501 >= Moonbit_array_length(_M0L6bufferS500)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS500[_M0L1iS501] = _M0L6_2atmpS1398;
      _M0L6_2atmpS1401 = _M0L1iS501 + 1;
      _M0L1iS501 = _M0L6_2atmpS1401;
      continue;
    }
    break;
  }
  return _M0L6bufferS500;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS496) {
  int32_t _M0L6_2atmpS1397;
  uint32_t _M0L6_2atmpS1396;
  uint32_t _M0L6_2atmpS1395;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1397 = _M0L1eS496 * 78913;
  _M0L6_2atmpS1396 = *(uint32_t*)&_M0L6_2atmpS1397;
  _M0L6_2atmpS1395 = _M0L6_2atmpS1396 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1395;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS495) {
  int32_t _M0L6_2atmpS1394;
  uint32_t _M0L6_2atmpS1393;
  uint32_t _M0L6_2atmpS1392;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1394 = _M0L1eS495 * 732923;
  _M0L6_2atmpS1393 = *(uint32_t*)&_M0L6_2atmpS1394;
  _M0L6_2atmpS1392 = _M0L6_2atmpS1393 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1392;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS493,
  int32_t _M0L8exponentS494,
  int32_t _M0L8mantissaS491
) {
  moonbit_string_t _M0L1sS492;
  moonbit_string_t _result_1984;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS491) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  if (_M0L4signS493) {
    _M0L1sS492 = (moonbit_string_t)moonbit_string_literal_12.data;
  } else {
    _M0L1sS492 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS494) {
    moonbit_string_t _result_1983;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1983
    = moonbit_add_string(_M0L1sS492, (moonbit_string_t)moonbit_string_literal_13.data);
    moonbit_decref_cycle_free(_M0L1sS492);
    return _result_1983;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1984
  = moonbit_add_string(_M0L1sS492, (moonbit_string_t)moonbit_string_literal_14.data);
  moonbit_decref_cycle_free(_M0L1sS492);
  return _result_1984;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS490) {
  int32_t _M0L6_2atmpS1391;
  uint32_t _M0L6_2atmpS1390;
  uint32_t _M0L6_2atmpS1389;
  int32_t _M0L6_2atmpS1388;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1391 = _M0L1eS490 * 1217359;
  _M0L6_2atmpS1390 = *(uint32_t*)&_M0L6_2atmpS1391;
  _M0L6_2atmpS1389 = _M0L6_2atmpS1390 >> 19;
  _M0L6_2atmpS1388 = *(int32_t*)&_M0L6_2atmpS1389;
  return _M0L6_2atmpS1388 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS489) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS489 != _M0L4selfS489) {
    return 0;
  } else if (_M0L4selfS489 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS489 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS489;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS488) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS488 != _M0L4selfS488) {
    return 0ll;
  } else if (_M0L4selfS488 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS488 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS488;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS485
) {
  float* _M0L6_2atmpS1385;
  struct _M0TPB5ArrayGfE* _block_1985;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1385 = (float*)moonbit_make_float_array_raw(_M0L3lenS485);
  _block_1985
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_1985)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 28, 0);
  _block_1985->$0 = _M0L6_2atmpS1385;
  _block_1985->$1 = _M0L3lenS485;
  return _block_1985;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS486
) {
  uint8_t* _M0L6_2atmpS1386;
  struct _M0TPB5ArrayGbE* _block_1986;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1386 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS486);
  _block_1986
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_1986)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 31, 0);
  _block_1986->$0 = _M0L6_2atmpS1386;
  _block_1986->$1 = _M0L3lenS486;
  return _block_1986;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS487
) {
  int32_t* _M0L6_2atmpS1387;
  struct _M0TPB5ArrayGiE* _block_1987;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1387 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS487);
  _block_1987
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_1987)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 34, 0);
  _block_1987->$0 = _M0L6_2atmpS1387;
  _block_1987->$1 = _M0L3lenS487;
  return _block_1987;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS481,
  int32_t _M0L5indexS482
) {
  uint64_t* _M0L6_2atmpS1383;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1383 = _M0L4selfS481;
  if (
    _M0L5indexS482 < 0
    || _M0L5indexS482 >= Moonbit_array_length(_M0L6_2atmpS1383)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1383[_M0L5indexS482];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS483,
  int32_t _M0L5indexS484
) {
  uint32_t* _M0L6_2atmpS1384;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1384 = _M0L4selfS483;
  if (
    _M0L5indexS484 < 0
    || _M0L5indexS484 >= Moonbit_array_length(_M0L6_2atmpS1384)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1384[_M0L5indexS484];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS480
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS480, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS479) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS479, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS478) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS478;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS469,
  float _M0L5valueS471
) {
  int32_t _M0L3lenS1362;
  float* _M0L6_2atmpS1364;
  int32_t _M0L6_2atmpS1363;
  int32_t _M0L6lengthS470;
  float* _M0L3bufS1367;
  int32_t _M0L6_2atmpS1368;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1362 = _M0L4selfS469->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1364 = _M0MPC15array5Array6bufferGfE(_M0L4selfS469);
  _M0L6_2atmpS1363 = Moonbit_array_length(_M0L6_2atmpS1364);
  moonbit_decref_cycle_free(_M0L6_2atmpS1364);
  if (_M0L3lenS1362 == _M0L6_2atmpS1363) {
    int32_t _M0L3lenS1366 = _M0L4selfS469->$1;
    int32_t _M0L6_2atmpS1365 = _M0L3lenS1366 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS469, _M0L6_2atmpS1365);
  }
  _M0L6lengthS470 = _M0L4selfS469->$1;
  _M0L3bufS1367 = _M0L4selfS469->$0;
  _M0L3bufS1367[_M0L6lengthS470] = _M0L5valueS471;
  _M0L6_2atmpS1368 = _M0L6lengthS470 + 1;
  _M0L4selfS469->$1 = _M0L6_2atmpS1368;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS472,
  moonbit_string_t _M0L5valueS474
) {
  int32_t _M0L3lenS1369;
  moonbit_string_t* _M0L6_2atmpS1371;
  int32_t _M0L6_2atmpS1370;
  int32_t _M0L6lengthS473;
  moonbit_string_t* _M0L3bufS1374;
  moonbit_string_t _M0L6_2aoldS1903;
  int32_t _M0L6_2atmpS1375;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1369 = _M0L4selfS472->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1371 = _M0MPC15array5Array6bufferGsE(_M0L4selfS472);
  _M0L6_2atmpS1370 = Moonbit_array_length(_M0L6_2atmpS1371);
  moonbit_decref_cycle_free(_M0L6_2atmpS1371);
  if (_M0L3lenS1369 == _M0L6_2atmpS1370) {
    int32_t _M0L3lenS1373 = _M0L4selfS472->$1;
    int32_t _M0L6_2atmpS1372 = _M0L3lenS1373 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS472, _M0L6_2atmpS1372);
  }
  _M0L6lengthS473 = _M0L4selfS472->$1;
  _M0L3bufS1374 = _M0L4selfS472->$0;
  _M0L6_2aoldS1903 = (moonbit_string_t)_M0L3bufS1374[_M0L6lengthS473];
  moonbit_decref_cycle_free(_M0L6_2aoldS1903);
  _M0L3bufS1374[_M0L6lengthS473] = _M0L5valueS474;
  _M0L6_2atmpS1375 = _M0L6lengthS473 + 1;
  _M0L4selfS472->$1 = _M0L6_2atmpS1375;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS475,
  struct _M0TUsiE* _M0L5valueS477
) {
  int32_t _M0L3lenS1376;
  struct _M0TUsiE** _M0L6_2atmpS1378;
  int32_t _M0L6_2atmpS1377;
  int32_t _M0L6lengthS476;
  struct _M0TUsiE** _M0L3bufS1381;
  struct _M0TUsiE* _M0L6_2aoldS1904;
  int32_t _M0L6_2atmpS1382;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1376 = _M0L4selfS475->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1378 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS475);
  _M0L6_2atmpS1377 = Moonbit_array_length(_M0L6_2atmpS1378);
  moonbit_decref_cycle_free(_M0L6_2atmpS1378);
  if (_M0L3lenS1376 == _M0L6_2atmpS1377) {
    int32_t _M0L3lenS1380 = _M0L4selfS475->$1;
    int32_t _M0L6_2atmpS1379 = _M0L3lenS1380 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS475, _M0L6_2atmpS1379);
  }
  _M0L6lengthS476 = _M0L4selfS475->$1;
  _M0L3bufS1381 = _M0L4selfS475->$0;
  _M0L6_2aoldS1904 = (struct _M0TUsiE*)_M0L3bufS1381[_M0L6lengthS476];
  if (_M0L6_2aoldS1904) {
    moonbit_decref_cycle_free(_M0L6_2aoldS1904);
  }
  _M0L3bufS1381[_M0L6lengthS476] = _M0L5valueS477;
  _M0L6_2atmpS1382 = _M0L6lengthS476 + 1;
  _M0L4selfS475->$1 = _M0L6_2atmpS1382;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS458,
  int32_t _M0L8requiredS460
) {
  int32_t _M0L8old__capS457;
  int32_t _M0L3lenS1359;
  int32_t _M0L8new__capS459;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS457 = _M0MPC15array5Array8capacityGfE(_M0L4selfS458);
  _M0L3lenS1359 = _M0L4selfS458->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS459
  = _M0FPB23array__growth__capacity(_M0L8old__capS457, _M0L3lenS1359, _M0L8requiredS460);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS458, _M0L8new__capS459);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS462,
  int32_t _M0L8requiredS464
) {
  int32_t _M0L8old__capS461;
  int32_t _M0L3lenS1360;
  int32_t _M0L8new__capS463;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS461 = _M0MPC15array5Array8capacityGsE(_M0L4selfS462);
  _M0L3lenS1360 = _M0L4selfS462->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS463
  = _M0FPB23array__growth__capacity(_M0L8old__capS461, _M0L3lenS1360, _M0L8requiredS464);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS462, _M0L8new__capS463);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS466,
  int32_t _M0L8requiredS468
) {
  int32_t _M0L8old__capS465;
  int32_t _M0L3lenS1361;
  int32_t _M0L8new__capS467;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS465 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS466);
  _M0L3lenS1361 = _M0L4selfS466->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS467
  = _M0FPB23array__growth__capacity(_M0L8old__capS465, _M0L3lenS1361, _M0L8requiredS468);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS466, _M0L8new__capS467);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS440,
  int32_t _M0L13new__capacityS443
) {
  float* _M0L8old__bufS439;
  int32_t _M0L3lenS441;
  int32_t _M0L9copy__lenS442;
  float* _M0L8new__bufS444;
  float* _M0L6_2aoldS1905;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS439 = _M0L4selfS440->$0;
  _M0L3lenS441 = _M0L4selfS440->$1;
  if (_M0L3lenS441 < _M0L13new__capacityS443) {
    _M0L9copy__lenS442 = _M0L3lenS441;
  } else {
    _M0L9copy__lenS442 = _M0L13new__capacityS443;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS439);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS444
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS439, _M0L13new__capacityS443, _M0L9copy__lenS442, 0, 0);
  _M0L6_2aoldS1905 = _M0L4selfS440->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1905);
  _M0L4selfS440->$0 = _M0L8new__bufS444;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS446,
  int32_t _M0L13new__capacityS449
) {
  moonbit_string_t* _M0L8old__bufS445;
  int32_t _M0L3lenS447;
  int32_t _M0L9copy__lenS448;
  moonbit_string_t* _M0L8new__bufS450;
  moonbit_string_t* _M0L6_2aoldS1906;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS445 = _M0L4selfS446->$0;
  _M0L3lenS447 = _M0L4selfS446->$1;
  if (_M0L3lenS447 < _M0L13new__capacityS449) {
    _M0L9copy__lenS448 = _M0L3lenS447;
  } else {
    _M0L9copy__lenS448 = _M0L13new__capacityS449;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS445);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS450
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS445, _M0L13new__capacityS449, _M0L9copy__lenS448, 0, 0);
  _M0L6_2aoldS1906 = _M0L4selfS446->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1906);
  _M0L4selfS446->$0 = _M0L8new__bufS450;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS452,
  int32_t _M0L13new__capacityS455
) {
  struct _M0TUsiE** _M0L8old__bufS451;
  int32_t _M0L3lenS453;
  int32_t _M0L9copy__lenS454;
  struct _M0TUsiE** _M0L8new__bufS456;
  struct _M0TUsiE** _M0L6_2aoldS1907;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS451 = _M0L4selfS452->$0;
  _M0L3lenS453 = _M0L4selfS452->$1;
  if (_M0L3lenS453 < _M0L13new__capacityS455) {
    _M0L9copy__lenS454 = _M0L3lenS453;
  } else {
    _M0L9copy__lenS454 = _M0L13new__capacityS455;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS451);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS456
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS451, _M0L13new__capacityS455, _M0L9copy__lenS454, 0, 0);
  _M0L6_2aoldS1907 = _M0L4selfS452->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1907);
  _M0L4selfS452->$0 = _M0L8new__bufS456;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS436
) {
  float* _M0L6_2atmpS1356;
  int32_t _result_1988;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1356 = _M0MPC15array5Array6bufferGfE(_M0L4selfS436);
  _result_1988 = Moonbit_array_length(_M0L6_2atmpS1356);
  moonbit_decref_cycle_free(_M0L6_2atmpS1356);
  return _result_1988;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS437
) {
  moonbit_string_t* _M0L6_2atmpS1357;
  int32_t _result_1989;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1357 = _M0MPC15array5Array6bufferGsE(_M0L4selfS437);
  _result_1989 = Moonbit_array_length(_M0L6_2atmpS1357);
  moonbit_decref_cycle_free(_M0L6_2atmpS1357);
  return _result_1989;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS438
) {
  struct _M0TUsiE** _M0L6_2atmpS1358;
  int32_t _result_1990;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1358 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS438);
  _result_1990 = Moonbit_array_length(_M0L6_2atmpS1358);
  moonbit_decref_cycle_free(_M0L6_2atmpS1358);
  return _result_1990;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS432,
  int32_t _M0L3lenS430,
  int32_t _M0L8requiredS429
) {
  int32_t _M0L5startS431;
  int32_t _M0L5spaceS433;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS429 < _M0L3lenS430) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_15.data);
  }
  if (_M0L7currentS432 == 0) {
    _M0L5startS431 = 8;
  } else {
    _M0L5startS431 = _M0L7currentS432;
  }
  _M0L5spaceS433 = _M0L5startS431;
  while (1) {
    if (_M0L5spaceS433 < _M0L8requiredS429) {
      int32_t _M0L4nextS434 = _M0L5spaceS433 * 2;
      if (_M0L4nextS434 <= _M0L5spaceS433) {
        return _M0L8requiredS429;
      }
      _M0L5spaceS433 = _M0L4nextS434;
      continue;
    } else {
      return _M0L5spaceS433;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS428) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS428->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS424) {
  float* _M0L8_2afieldS1908;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1908 = _M0L4selfS424->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1908);
  return _M0L8_2afieldS1908;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS425) {
  uint8_t* _M0L8_2afieldS1909;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1909 = _M0L4selfS425->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1909);
  return _M0L8_2afieldS1909;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS426
) {
  moonbit_string_t* _M0L8_2afieldS1910;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1910 = _M0L4selfS426->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1910);
  return _M0L8_2afieldS1910;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS427
) {
  struct _M0TUsiE** _M0L8_2afieldS1911;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1911 = _M0L4selfS427->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1911);
  return _M0L8_2afieldS1911;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS423
) {
  #line 220 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref_cycle_free(_M0L4selfS423);
  return _M0L4selfS423;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS422,
  struct _M0TPC16string10StringView _M0L3strS420
) {
  int32_t _M0L3endS1354;
  int32_t _M0L5startS1355;
  int32_t _M0L8str__lenS419;
  int32_t _M0L3lenS1353;
  int32_t _M0L8requiredS421;
  uint16_t* _M0L4dataS1346;
  int32_t _M0L6_2atmpS1345;
  int32_t _if__result_1992;
  uint16_t* _M0L4dataS1347;
  int32_t _M0L3lenS1348;
  moonbit_string_t _M0L6_2atmpS1349;
  int32_t _M0L6_2atmpS1350;
  int32_t _M0L3lenS1352;
  int32_t _M0L6_2atmpS1351;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1354 = _M0L3strS420.$2;
  _M0L5startS1355 = _M0L3strS420.$1;
  _M0L8str__lenS419 = _M0L3endS1354 - _M0L5startS1355;
  if (_M0L8str__lenS419 == 0) {
    return 0;
  }
  _M0L3lenS1353 = _M0L4selfS422->$1;
  _M0L8requiredS421 = _M0L3lenS1353 + _M0L8str__lenS419;
  _M0L4dataS1346 = _M0L4selfS422->$0;
  _M0L6_2atmpS1345 = Moonbit_array_length(_M0L4dataS1346);
  if (_M0L8requiredS421 > _M0L6_2atmpS1345) {
    _if__result_1992 = 1;
  } else {
    int32_t _M0L3lenS1344 = _M0L4selfS422->$1;
    _if__result_1992 = _M0L8requiredS421 < _M0L3lenS1344;
  }
  if (_if__result_1992) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS422, _M0L8requiredS421);
  }
  _M0L4dataS1347 = _M0L4selfS422->$0;
  _M0L3lenS1348 = _M0L4selfS422->$1;
  moonbit_incref_cycle_free(_M0L4dataS1347);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1349 = _M0MPC16string10StringView4data(_M0L3strS420);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1350 = _M0MPC16string10StringView13start__offset(_M0L3strS420);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1347, _M0L3lenS1348, _M0L6_2atmpS1349, _M0L6_2atmpS1350, _M0L8str__lenS419);
  moonbit_decref_cycle_free(_M0L4dataS1347);
  moonbit_decref_cycle_free(_M0L6_2atmpS1349);
  _M0L3lenS1352 = _M0L4selfS422->$1;
  _M0L6_2atmpS1351 = _M0L3lenS1352 + _M0L8str__lenS419;
  _M0L4selfS422->$1 = _M0L6_2atmpS1351;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS416,
  int32_t _M0L5startS414,
  int32_t _M0L3endS415
) {
  int32_t _if__result_1993;
  int32_t _M0L3lenS417;
  int32_t _M0L6_2atmpS1343;
  moonbit_bytes_t _M0L5bytesS418;
  moonbit_bytes_t _M0L6_2atmpS1342;
  moonbit_string_t _result_1994;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS414 == 0) {
    int32_t _M0L6_2atmpS1341 = Moonbit_array_length(_M0L3strS416);
    _if__result_1993 = _M0L3endS415 == _M0L6_2atmpS1341;
  } else {
    _if__result_1993 = 0;
  }
  if (_if__result_1993) {
    moonbit_incref_cycle_free(_M0L3strS416);
    return _M0L3strS416;
  }
  _M0L3lenS417 = _M0L3endS415 - _M0L5startS414;
  _M0L6_2atmpS1343 = _M0L3lenS417 * 2;
  _M0L5bytesS418 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1343, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS418, 0, _M0L3strS416, _M0L5startS414, _M0L3lenS417);
  _M0L6_2atmpS1342 = _M0L5bytesS418;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_1994
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1342, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1342);
  return _result_1994;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS409,
  int32_t _M0L6offsetS413,
  int64_t _M0L6lengthS411
) {
  int32_t _M0L3lenS408;
  int32_t _M0L6lengthS410;
  int32_t _if__result_1995;
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L3lenS408 = Moonbit_array_length(_M0L4selfS409);
  if (_M0L6lengthS411 == 4294967296ll) {
    _M0L6lengthS410 = _M0L3lenS408 - _M0L6offsetS413;
  } else {
    int64_t _M0L7_2aSomeS412 = _M0L6lengthS411;
    _M0L6lengthS410 = (int32_t)_M0L7_2aSomeS412;
  }
  if (_M0L6offsetS413 >= 0) {
    if (_M0L6lengthS410 >= 0) {
      int32_t _M0L6_2atmpS1340 = _M0L6offsetS413 + _M0L6lengthS410;
      _if__result_1995 = _M0L6_2atmpS1340 <= _M0L3lenS408;
    } else {
      _if__result_1995 = 0;
    }
  } else {
    _if__result_1995 = 0;
  }
  if (_if__result_1995) {
    moonbit_incref_cycle_free(_M0L4selfS409);
    #line 85 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    return _M0FPB19unsafe__sub__string(_M0L4selfS409, _M0L6offsetS413, _M0L6lengthS410);
  } else {
    #line 84 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array10FixedArray18blit__from__string(
  moonbit_bytes_t _M0L4selfS400,
  int32_t _M0L13bytes__offsetS395,
  moonbit_string_t _M0L3strS402,
  int32_t _M0L11str__offsetS398,
  int32_t _M0L6lengthS396
) {
  int32_t _M0L6_2atmpS1339;
  int32_t _M0L6_2atmpS1338;
  int32_t _M0L2e1S394;
  int32_t _M0L6_2atmpS1337;
  int32_t _M0L2e2S397;
  int32_t _M0L4len1S399;
  int32_t _M0L4len2S401;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1339 = _M0L6lengthS396 * 2;
  _M0L6_2atmpS1338 = _M0L13bytes__offsetS395 + _M0L6_2atmpS1339;
  _M0L2e1S394 = _M0L6_2atmpS1338 - 1;
  _M0L6_2atmpS1337 = _M0L11str__offsetS398 + _M0L6lengthS396;
  _M0L2e2S397 = _M0L6_2atmpS1337 - 1;
  _M0L4len1S399 = Moonbit_array_length(_M0L4selfS400);
  _M0L4len2S401 = Moonbit_array_length(_M0L3strS402);
  if (
    _M0L6lengthS396 >= 0
    && _M0L13bytes__offsetS395 >= 0
    && _M0L2e1S394 < _M0L4len1S399
    && _M0L11str__offsetS398 >= 0
    && _M0L2e2S397 < _M0L4len2S401
  ) {
    int32_t _M0L16end__str__offsetS403 =
      _M0L11str__offsetS398 + _M0L6lengthS396;
    int32_t _M0L1iS404 = _M0L11str__offsetS398;
    int32_t _M0L1jS405 = _M0L13bytes__offsetS395;
    while (1) {
      if (_M0L1iS404 < _M0L16end__str__offsetS403) {
        int32_t _M0L6_2atmpS1334 = _M0L3strS402[_M0L1iS404];
        int32_t _M0L6_2atmpS1333 = (int32_t)_M0L6_2atmpS1334;
        uint32_t _M0L1cS406 = *(uint32_t*)&_M0L6_2atmpS1333;
        uint32_t _M0L6_2atmpS1329 = _M0L1cS406 & 255u;
        int32_t _M0L6_2atmpS1328;
        int32_t _M0L6_2atmpS1330;
        uint32_t _M0L6_2atmpS1332;
        int32_t _M0L6_2atmpS1331;
        int32_t _M0L6_2atmpS1335;
        int32_t _M0L6_2atmpS1336;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1328 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1329);
        if (
          _M0L1jS405 < 0 || _M0L1jS405 >= Moonbit_array_length(_M0L4selfS400)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS400[_M0L1jS405] = _M0L6_2atmpS1328;
        _M0L6_2atmpS1330 = _M0L1jS405 + 1;
        _M0L6_2atmpS1332 = _M0L1cS406 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1331 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1332);
        if (
          _M0L6_2atmpS1330 < 0
          || _M0L6_2atmpS1330 >= Moonbit_array_length(_M0L4selfS400)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS400[_M0L6_2atmpS1330] = _M0L6_2atmpS1331;
        _M0L6_2atmpS1335 = _M0L1iS404 + 1;
        _M0L6_2atmpS1336 = _M0L1jS405 + 2;
        _M0L1iS404 = _M0L6_2atmpS1335;
        _M0L1jS405 = _M0L6_2atmpS1336;
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

int32_t _M0MPC14uint4UInt8to__byte(uint32_t _M0L4selfS393) {
  int32_t _M0L6_2atmpS1327;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1327 = *(int32_t*)&_M0L4selfS393;
  return _M0L6_2atmpS1327 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS385,
  int32_t _M0L5radixS384
) {
  uint16_t* _M0L6bufferS386;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS384 < 2 || _M0L5radixS384 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_16.data);
  }
  if (_M0L4selfS385 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  switch (_M0L5radixS384) {
    case 10: {
      int32_t _M0L3lenS387;
      uint16_t* _M0L6bufferS388;
      #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS387 = _M0FPB12dec__count64(_M0L4selfS385);
      _M0L6bufferS388 = (uint16_t*)moonbit_make_string(_M0L3lenS387, 0);
      #line 624 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS388, _M0L4selfS385, 0, _M0L3lenS387);
      _M0L6bufferS386 = _M0L6bufferS388;
      break;
    }
    
    case 16: {
      int32_t _M0L3lenS389;
      uint16_t* _M0L6bufferS390;
      #line 628 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS389 = _M0FPB12hex__count64(_M0L4selfS385);
      _M0L6bufferS390 = (uint16_t*)moonbit_make_string(_M0L3lenS389, 0);
      #line 630 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS390, _M0L4selfS385, 0, _M0L3lenS389);
      _M0L6bufferS386 = _M0L6bufferS390;
      break;
    }
    default: {
      int32_t _M0L3lenS391;
      uint16_t* _M0L6bufferS392;
      #line 634 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS391 = _M0FPB14radix__count64(_M0L4selfS385, _M0L5radixS384);
      _M0L6bufferS392 = (uint16_t*)moonbit_make_string(_M0L3lenS391, 0);
      #line 636 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS392, _M0L4selfS385, 0, _M0L3lenS391, _M0L5radixS384);
      _M0L6bufferS386 = _M0L6bufferS392;
      break;
    }
  }
  return _M0L6bufferS386;
}

moonbit_string_t _M0MPC15int645Int6418to__string_2einner(
  int64_t _M0L4selfS368,
  int32_t _M0L5radixS367
) {
  int32_t _M0L12is__negativeS369;
  uint64_t _M0L3numS370;
  uint16_t* _M0L6bufferS371;
  #line 548 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS367 < 2 || _M0L5radixS367 > 36) {
    #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_16.data);
  }
  if (_M0L4selfS368 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  _M0L12is__negativeS369 = _M0L4selfS368 < 0ll;
  if (_M0L12is__negativeS369) {
    int64_t _M0L6_2atmpS1326 = -_M0L4selfS368;
    _M0L3numS370 = *(uint64_t*)&_M0L6_2atmpS1326;
  } else {
    _M0L3numS370 = *(uint64_t*)&_M0L4selfS368;
  }
  switch (_M0L5radixS367) {
    case 10: {
      int32_t _M0L10digit__lenS372;
      int32_t _M0L6_2atmpS1323;
      int32_t _M0L10total__lenS373;
      uint16_t* _M0L6bufferS374;
      int32_t _M0L12digit__startS375;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS372 = _M0FPB12dec__count64(_M0L3numS370);
      if (_M0L12is__negativeS369) {
        _M0L6_2atmpS1323 = 1;
      } else {
        _M0L6_2atmpS1323 = 0;
      }
      _M0L10total__lenS373 = _M0L10digit__lenS372 + _M0L6_2atmpS1323;
      _M0L6bufferS374
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS373, 0);
      if (_M0L12is__negativeS369) {
        _M0L12digit__startS375 = 1;
      } else {
        _M0L12digit__startS375 = 0;
      }
      #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS374, _M0L3numS370, _M0L12digit__startS375, _M0L10total__lenS373);
      _M0L6bufferS371 = _M0L6bufferS374;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS376;
      int32_t _M0L6_2atmpS1324;
      int32_t _M0L10total__lenS377;
      uint16_t* _M0L6bufferS378;
      int32_t _M0L12digit__startS379;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS376 = _M0FPB12hex__count64(_M0L3numS370);
      if (_M0L12is__negativeS369) {
        _M0L6_2atmpS1324 = 1;
      } else {
        _M0L6_2atmpS1324 = 0;
      }
      _M0L10total__lenS377 = _M0L10digit__lenS376 + _M0L6_2atmpS1324;
      _M0L6bufferS378
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS377, 0);
      if (_M0L12is__negativeS369) {
        _M0L12digit__startS379 = 1;
      } else {
        _M0L12digit__startS379 = 0;
      }
      #line 585 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS378, _M0L3numS370, _M0L12digit__startS379, _M0L10total__lenS377);
      _M0L6bufferS371 = _M0L6bufferS378;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS380;
      int32_t _M0L6_2atmpS1325;
      int32_t _M0L10total__lenS381;
      uint16_t* _M0L6bufferS382;
      int32_t _M0L12digit__startS383;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS380
      = _M0FPB14radix__count64(_M0L3numS370, _M0L5radixS367);
      if (_M0L12is__negativeS369) {
        _M0L6_2atmpS1325 = 1;
      } else {
        _M0L6_2atmpS1325 = 0;
      }
      _M0L10total__lenS381 = _M0L10digit__lenS380 + _M0L6_2atmpS1325;
      _M0L6bufferS382
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS381, 0);
      if (_M0L12is__negativeS369) {
        _M0L12digit__startS383 = 1;
      } else {
        _M0L12digit__startS383 = 0;
      }
      #line 593 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS382, _M0L3numS370, _M0L12digit__startS383, _M0L10total__lenS381, _M0L5radixS367);
      _M0L6bufferS371 = _M0L6bufferS382;
      break;
    }
  }
  if (_M0L12is__negativeS369) {
    _M0L6bufferS371[0] = 45;
  }
  return _M0L6bufferS371;
}

int32_t _M0FPB22int64__to__string__dec(
  uint16_t* _M0L6bufferS353,
  uint64_t _M0L3numS365,
  int32_t _M0L12digit__startS354,
  int32_t _M0L10total__lenS366
) {
  int32_t _M0L6_2atmpS1322;
  uint64_t _M0L3numS343;
  int32_t _M0L6offsetS344;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1322 = _M0L10total__lenS366 - _M0L12digit__startS354;
  _M0L3numS343 = _M0L3numS365;
  _M0L6offsetS344 = _M0L6_2atmpS1322;
  while (1) {
    if (_M0L3numS343 >= 10000ull) {
      uint64_t _M0L1tS345 = _M0L3numS343 / 10000ull;
      uint64_t _M0L6_2atmpS1299 = _M0L3numS343 % 10000ull;
      int32_t _M0L1rS346 = (int32_t)_M0L6_2atmpS1299;
      int32_t _M0L2d1S347 = _M0L1rS346 / 100;
      int32_t _M0L2d2S348 = _M0L1rS346 % 100;
      int32_t _M0L6_2atmpS1298 = _M0L2d1S347 / 10;
      int32_t _M0L6_2atmpS1297 = 48 + _M0L6_2atmpS1298;
      int32_t _M0L6d1__hiS349 = (uint16_t)_M0L6_2atmpS1297;
      int32_t _M0L6_2atmpS1296 = _M0L2d1S347 % 10;
      int32_t _M0L6_2atmpS1295 = 48 + _M0L6_2atmpS1296;
      int32_t _M0L6d1__loS350 = (uint16_t)_M0L6_2atmpS1295;
      int32_t _M0L6_2atmpS1294 = _M0L2d2S348 / 10;
      int32_t _M0L6_2atmpS1293 = 48 + _M0L6_2atmpS1294;
      int32_t _M0L6d2__hiS351 = (uint16_t)_M0L6_2atmpS1293;
      int32_t _M0L6_2atmpS1292 = _M0L2d2S348 % 10;
      int32_t _M0L6_2atmpS1291 = 48 + _M0L6_2atmpS1292;
      int32_t _M0L6d2__loS352 = (uint16_t)_M0L6_2atmpS1291;
      int32_t _M0L6_2atmpS1283 = _M0L12digit__startS354 + _M0L6offsetS344;
      int32_t _M0L6_2atmpS1282 = _M0L6_2atmpS1283 - 4;
      int32_t _M0L6_2atmpS1285;
      int32_t _M0L6_2atmpS1284;
      int32_t _M0L6_2atmpS1287;
      int32_t _M0L6_2atmpS1286;
      int32_t _M0L6_2atmpS1289;
      int32_t _M0L6_2atmpS1288;
      int32_t _M0L6_2atmpS1290;
      _M0L6bufferS353[_M0L6_2atmpS1282] = _M0L6d1__hiS349;
      _M0L6_2atmpS1285 = _M0L12digit__startS354 + _M0L6offsetS344;
      _M0L6_2atmpS1284 = _M0L6_2atmpS1285 - 3;
      _M0L6bufferS353[_M0L6_2atmpS1284] = _M0L6d1__loS350;
      _M0L6_2atmpS1287 = _M0L12digit__startS354 + _M0L6offsetS344;
      _M0L6_2atmpS1286 = _M0L6_2atmpS1287 - 2;
      _M0L6bufferS353[_M0L6_2atmpS1286] = _M0L6d2__hiS351;
      _M0L6_2atmpS1289 = _M0L12digit__startS354 + _M0L6offsetS344;
      _M0L6_2atmpS1288 = _M0L6_2atmpS1289 - 1;
      _M0L6bufferS353[_M0L6_2atmpS1288] = _M0L6d2__loS352;
      _M0L6_2atmpS1290 = _M0L6offsetS344 - 4;
      _M0L3numS343 = _M0L1tS345;
      _M0L6offsetS344 = _M0L6_2atmpS1290;
      continue;
    } else {
      int32_t _M0L6_2atmpS1321 = (int32_t)_M0L3numS343;
      int32_t _M0L9remainingS356 = _M0L6_2atmpS1321;
      int32_t _M0L6offsetS357 = _M0L6offsetS344;
      while (1) {
        if (_M0L9remainingS356 >= 100) {
          int32_t _M0L1tS358 = _M0L9remainingS356 / 100;
          int32_t _M0L1dS359 = _M0L9remainingS356 % 100;
          int32_t _M0L6_2atmpS1308 = _M0L1dS359 / 10;
          int32_t _M0L6_2atmpS1307 = 48 + _M0L6_2atmpS1308;
          int32_t _M0L5d__hiS360 = (uint16_t)_M0L6_2atmpS1307;
          int32_t _M0L6_2atmpS1306 = _M0L1dS359 % 10;
          int32_t _M0L6_2atmpS1305 = 48 + _M0L6_2atmpS1306;
          int32_t _M0L5d__loS361 = (uint16_t)_M0L6_2atmpS1305;
          int32_t _M0L6_2atmpS1301 = _M0L12digit__startS354 + _M0L6offsetS357;
          int32_t _M0L6_2atmpS1300 = _M0L6_2atmpS1301 - 2;
          int32_t _M0L6_2atmpS1303;
          int32_t _M0L6_2atmpS1302;
          int32_t _M0L6_2atmpS1304;
          _M0L6bufferS353[_M0L6_2atmpS1300] = _M0L5d__hiS360;
          _M0L6_2atmpS1303 = _M0L12digit__startS354 + _M0L6offsetS357;
          _M0L6_2atmpS1302 = _M0L6_2atmpS1303 - 1;
          _M0L6bufferS353[_M0L6_2atmpS1302] = _M0L5d__loS361;
          _M0L6_2atmpS1304 = _M0L6offsetS357 - 2;
          _M0L9remainingS356 = _M0L1tS358;
          _M0L6offsetS357 = _M0L6_2atmpS1304;
          continue;
        } else if (_M0L9remainingS356 >= 10) {
          int32_t _M0L6_2atmpS1316 = _M0L9remainingS356 / 10;
          int32_t _M0L6_2atmpS1315 = 48 + _M0L6_2atmpS1316;
          int32_t _M0L5d__hiS363 = (uint16_t)_M0L6_2atmpS1315;
          int32_t _M0L6_2atmpS1314 = _M0L9remainingS356 % 10;
          int32_t _M0L6_2atmpS1313 = 48 + _M0L6_2atmpS1314;
          int32_t _M0L5d__loS364 = (uint16_t)_M0L6_2atmpS1313;
          int32_t _M0L6_2atmpS1310 = _M0L12digit__startS354 + _M0L6offsetS357;
          int32_t _M0L6_2atmpS1309 = _M0L6_2atmpS1310 - 2;
          int32_t _M0L6_2atmpS1312;
          int32_t _M0L6_2atmpS1311;
          _M0L6bufferS353[_M0L6_2atmpS1309] = _M0L5d__hiS363;
          _M0L6_2atmpS1312 = _M0L12digit__startS354 + _M0L6offsetS357;
          _M0L6_2atmpS1311 = _M0L6_2atmpS1312 - 1;
          _M0L6bufferS353[_M0L6_2atmpS1311] = _M0L5d__loS364;
        } else {
          int32_t _M0L6_2atmpS1320 = _M0L12digit__startS354 + _M0L6offsetS357;
          int32_t _M0L6_2atmpS1317 = _M0L6_2atmpS1320 - 1;
          int32_t _M0L6_2atmpS1319 = 48 + _M0L9remainingS356;
          int32_t _M0L6_2atmpS1318 = (uint16_t)_M0L6_2atmpS1319;
          _M0L6bufferS353[_M0L6_2atmpS1317] = _M0L6_2atmpS1318;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB26int64__to__string__generic(
  uint16_t* _M0L6bufferS333,
  uint64_t _M0L3numS337,
  int32_t _M0L12digit__startS334,
  int32_t _M0L10total__lenS336,
  int32_t _M0L5radixS327
) {
  uint64_t _M0L4baseS326;
  int32_t _M0L6_2atmpS1267;
  int32_t _M0L6_2atmpS1266;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS326 = _M0MPC13int3Int10to__uint64(_M0L5radixS327);
  _M0L6_2atmpS1267 = _M0L5radixS327 - 1;
  _M0L6_2atmpS1266 = _M0L5radixS327 & _M0L6_2atmpS1267;
  if (_M0L6_2atmpS1266 == 0) {
    int32_t _M0L5shiftS328;
    uint64_t _M0L4maskS329;
    int32_t _M0L6_2atmpS1274;
    int32_t _M0L6offsetS330;
    uint64_t _M0L1nS331;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS328 = moonbit_ctz32(_M0L5radixS327);
    _M0L4maskS329 = _M0L4baseS326 - 1ull;
    _M0L6_2atmpS1274 = _M0L10total__lenS336 - _M0L12digit__startS334;
    _M0L6offsetS330 = _M0L6_2atmpS1274;
    _M0L1nS331 = _M0L3numS337;
    while (1) {
      if (_M0L1nS331 > 0ull) {
        uint64_t _M0L6_2atmpS1273 = _M0L1nS331 & _M0L4maskS329;
        int32_t _M0L5digitS332 = (int32_t)_M0L6_2atmpS1273;
        int32_t _M0L6_2atmpS1270 = _M0L12digit__startS334 + _M0L6offsetS330;
        int32_t _M0L6_2atmpS1268 = _M0L6_2atmpS1270 - 1;
        int32_t _M0L6_2atmpS1269 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS332];
        int32_t _M0L6_2atmpS1271;
        uint64_t _M0L6_2atmpS1272;
        _M0L6bufferS333[_M0L6_2atmpS1268] = _M0L6_2atmpS1269;
        _M0L6_2atmpS1271 = _M0L6offsetS330 - 1;
        _M0L6_2atmpS1272 = _M0L1nS331 >> (_M0L5shiftS328 & 63);
        _M0L6offsetS330 = _M0L6_2atmpS1271;
        _M0L1nS331 = _M0L6_2atmpS1272;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1281 = _M0L10total__lenS336 - _M0L12digit__startS334;
    int32_t _M0L6offsetS338 = _M0L6_2atmpS1281;
    uint64_t _M0L1nS339 = _M0L3numS337;
    while (1) {
      if (_M0L1nS339 > 0ull) {
        uint64_t _M0L1qS340 = _M0L1nS339 / _M0L4baseS326;
        uint64_t _M0L6_2atmpS1280 = _M0L1qS340 * _M0L4baseS326;
        uint64_t _M0L6_2atmpS1279 = _M0L1nS339 - _M0L6_2atmpS1280;
        int32_t _M0L5digitS341 = (int32_t)_M0L6_2atmpS1279;
        int32_t _M0L6_2atmpS1277 = _M0L12digit__startS334 + _M0L6offsetS338;
        int32_t _M0L6_2atmpS1275 = _M0L6_2atmpS1277 - 1;
        int32_t _M0L6_2atmpS1276 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS341];
        int32_t _M0L6_2atmpS1278;
        _M0L6bufferS333[_M0L6_2atmpS1275] = _M0L6_2atmpS1276;
        _M0L6_2atmpS1278 = _M0L6offsetS338 - 1;
        _M0L6offsetS338 = _M0L6_2atmpS1278;
        _M0L1nS339 = _M0L1qS340;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB22int64__to__string__hex(
  uint16_t* _M0L6bufferS320,
  uint64_t _M0L3numS325,
  int32_t _M0L12digit__startS321,
  int32_t _M0L10total__lenS324
) {
  int32_t _M0L6_2atmpS1265;
  int32_t _M0L6offsetS315;
  uint64_t _M0L1nS316;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1265 = _M0L10total__lenS324 - _M0L12digit__startS321;
  _M0L6offsetS315 = _M0L6_2atmpS1265;
  _M0L1nS316 = _M0L3numS325;
  while (1) {
    if (_M0L6offsetS315 >= 2) {
      uint64_t _M0L6_2atmpS1262 = _M0L1nS316 & 255ull;
      int32_t _M0L9byte__valS317 = (int32_t)_M0L6_2atmpS1262;
      int32_t _M0L2hiS318 = _M0L9byte__valS317 / 16;
      int32_t _M0L2loS319 = _M0L9byte__valS317 % 16;
      int32_t _M0L6_2atmpS1256 = _M0L12digit__startS321 + _M0L6offsetS315;
      int32_t _M0L6_2atmpS1254 = _M0L6_2atmpS1256 - 2;
      int32_t _M0L6_2atmpS1255 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L2hiS318];
      int32_t _M0L6_2atmpS1259;
      int32_t _M0L6_2atmpS1257;
      int32_t _M0L6_2atmpS1258;
      int32_t _M0L6_2atmpS1260;
      uint64_t _M0L6_2atmpS1261;
      _M0L6bufferS320[_M0L6_2atmpS1254] = _M0L6_2atmpS1255;
      _M0L6_2atmpS1259 = _M0L12digit__startS321 + _M0L6offsetS315;
      _M0L6_2atmpS1257 = _M0L6_2atmpS1259 - 1;
      _M0L6_2atmpS1258
      = ((moonbit_string_t)moonbit_string_literal_17.data)[
        _M0L2loS319
      ];
      _M0L6bufferS320[_M0L6_2atmpS1257] = _M0L6_2atmpS1258;
      _M0L6_2atmpS1260 = _M0L6offsetS315 - 2;
      _M0L6_2atmpS1261 = _M0L1nS316 >> 8;
      _M0L6offsetS315 = _M0L6_2atmpS1260;
      _M0L1nS316 = _M0L6_2atmpS1261;
      continue;
    } else if (_M0L6offsetS315 == 1) {
      uint64_t _M0L6_2atmpS1264 = _M0L1nS316 & 15ull;
      int32_t _M0L6nibbleS323 = (int32_t)_M0L6_2atmpS1264;
      int32_t _M0L6_2atmpS1263 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L6nibbleS323];
      _M0L6bufferS320[_M0L12digit__startS321] = _M0L6_2atmpS1263;
    }
    break;
  }
  return 0;
}

int32_t _M0FPB14radix__count64(
  uint64_t _M0L5valueS309,
  int32_t _M0L5radixS311
) {
  uint64_t _M0L4baseS310;
  uint64_t _M0L3numS312;
  int32_t _M0L5countS313;
  #line 419 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS309 == 0ull) {
    return 1;
  }
  #line 424 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS310 = _M0MPC13int3Int10to__uint64(_M0L5radixS311);
  _M0L3numS312 = _M0L5valueS309;
  _M0L5countS313 = 0;
  while (1) {
    if (_M0L3numS312 > 0ull) {
      uint64_t _M0L6_2atmpS1252 = _M0L3numS312 / _M0L4baseS310;
      int32_t _M0L6_2atmpS1253 = _M0L5countS313 + 1;
      _M0L3numS312 = _M0L6_2atmpS1252;
      _M0L5countS313 = _M0L6_2atmpS1253;
      continue;
    } else {
      return _M0L5countS313;
    }
    break;
  }
}

int32_t _M0FPB12hex__count64(uint64_t _M0L5valueS307) {
  #line 407 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS307 == 0ull) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS308;
    int32_t _M0L6_2atmpS1251;
    int32_t _M0L6_2atmpS1250;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS308 = moonbit_clz64(_M0L5valueS307);
    _M0L6_2atmpS1251 = 63 - _M0L14leading__zerosS308;
    _M0L6_2atmpS1250 = _M0L6_2atmpS1251 / 4;
    return _M0L6_2atmpS1250 + 1;
  }
}

int32_t _M0FPB12dec__count64(uint64_t _M0L5valueS306) {
  #line 343 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS306 >= 10000000000ull) {
    if (_M0L5valueS306 >= 100000000000000ull) {
      if (_M0L5valueS306 >= 10000000000000000ull) {
        if (_M0L5valueS306 >= 1000000000000000000ull) {
          if (_M0L5valueS306 >= 10000000000000000000ull) {
            return 20;
          } else {
            return 19;
          }
        } else if (_M0L5valueS306 >= 100000000000000000ull) {
          return 18;
        } else {
          return 17;
        }
      } else if (_M0L5valueS306 >= 1000000000000000ull) {
        return 16;
      } else {
        return 15;
      }
    } else if (_M0L5valueS306 >= 1000000000000ull) {
      if (_M0L5valueS306 >= 10000000000000ull) {
        return 14;
      } else {
        return 13;
      }
    } else if (_M0L5valueS306 >= 100000000000ull) {
      return 12;
    } else {
      return 11;
    }
  } else if (_M0L5valueS306 >= 100000ull) {
    if (_M0L5valueS306 >= 10000000ull) {
      if (_M0L5valueS306 >= 1000000000ull) {
        return 10;
      } else if (_M0L5valueS306 >= 100000000ull) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS306 >= 1000000ull) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS306 >= 1000ull) {
    if (_M0L5valueS306 >= 10000ull) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS306 >= 100ull) {
    return 3;
  } else if (_M0L5valueS306 >= 10ull) {
    return 2;
  } else {
    return 1;
  }
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS290,
  int32_t _M0L5radixS289
) {
  int32_t _M0L12is__negativeS291;
  uint32_t _M0L3numS292;
  uint16_t* _M0L6bufferS293;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS289 < 2 || _M0L5radixS289 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_16.data);
  }
  if (_M0L4selfS290 == 0) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  _M0L12is__negativeS291 = _M0L4selfS290 < 0;
  if (_M0L12is__negativeS291) {
    int32_t _M0L6_2atmpS1249 = -_M0L4selfS290;
    _M0L3numS292 = *(uint32_t*)&_M0L6_2atmpS1249;
  } else {
    _M0L3numS292 = *(uint32_t*)&_M0L4selfS290;
  }
  switch (_M0L5radixS289) {
    case 10: {
      int32_t _M0L10digit__lenS294;
      int32_t _M0L6_2atmpS1246;
      int32_t _M0L10total__lenS295;
      uint16_t* _M0L6bufferS296;
      int32_t _M0L12digit__startS297;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS294 = _M0FPB12dec__count32(_M0L3numS292);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1246 = 1;
      } else {
        _M0L6_2atmpS1246 = 0;
      }
      _M0L10total__lenS295 = _M0L10digit__lenS294 + _M0L6_2atmpS1246;
      _M0L6bufferS296
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS295, 0);
      if (_M0L12is__negativeS291) {
        _M0L12digit__startS297 = 1;
      } else {
        _M0L12digit__startS297 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS296, _M0L3numS292, _M0L12digit__startS297, _M0L10total__lenS295);
      _M0L6bufferS293 = _M0L6bufferS296;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS298;
      int32_t _M0L6_2atmpS1247;
      int32_t _M0L10total__lenS299;
      uint16_t* _M0L6bufferS300;
      int32_t _M0L12digit__startS301;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS298 = _M0FPB12hex__count32(_M0L3numS292);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1247 = 1;
      } else {
        _M0L6_2atmpS1247 = 0;
      }
      _M0L10total__lenS299 = _M0L10digit__lenS298 + _M0L6_2atmpS1247;
      _M0L6bufferS300
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS299, 0);
      if (_M0L12is__negativeS291) {
        _M0L12digit__startS301 = 1;
      } else {
        _M0L12digit__startS301 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS300, _M0L3numS292, _M0L12digit__startS301, _M0L10total__lenS299);
      _M0L6bufferS293 = _M0L6bufferS300;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS302;
      int32_t _M0L6_2atmpS1248;
      int32_t _M0L10total__lenS303;
      uint16_t* _M0L6bufferS304;
      int32_t _M0L12digit__startS305;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS302
      = _M0FPB14radix__count32(_M0L3numS292, _M0L5radixS289);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1248 = 1;
      } else {
        _M0L6_2atmpS1248 = 0;
      }
      _M0L10total__lenS303 = _M0L10digit__lenS302 + _M0L6_2atmpS1248;
      _M0L6bufferS304
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS303, 0);
      if (_M0L12is__negativeS291) {
        _M0L12digit__startS305 = 1;
      } else {
        _M0L12digit__startS305 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS304, _M0L3numS292, _M0L12digit__startS305, _M0L10total__lenS303, _M0L5radixS289);
      _M0L6bufferS293 = _M0L6bufferS304;
      break;
    }
  }
  if (_M0L12is__negativeS291) {
    _M0L6bufferS293[0] = 45;
  }
  return _M0L6bufferS293;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS283,
  int32_t _M0L5radixS285
) {
  uint32_t _M0L4baseS284;
  uint32_t _M0L3numS286;
  int32_t _M0L5countS287;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS283 == 0u) {
    return 1;
  }
  _M0L4baseS284 = *(uint32_t*)&_M0L5radixS285;
  _M0L3numS286 = _M0L5valueS283;
  _M0L5countS287 = 0;
  while (1) {
    if (_M0L3numS286 > 0u) {
      uint32_t _M0L6_2atmpS1244 = _M0L3numS286 / _M0L4baseS284;
      int32_t _M0L6_2atmpS1245 = _M0L5countS287 + 1;
      _M0L3numS286 = _M0L6_2atmpS1244;
      _M0L5countS287 = _M0L6_2atmpS1245;
      continue;
    } else {
      return _M0L5countS287;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS281) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS281 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS282;
    int32_t _M0L6_2atmpS1243;
    int32_t _M0L6_2atmpS1242;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS282 = moonbit_clz32(_M0L5valueS281);
    _M0L6_2atmpS1243 = 31 - _M0L14leading__zerosS282;
    _M0L6_2atmpS1242 = _M0L6_2atmpS1243 / 4;
    return _M0L6_2atmpS1242 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS280) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS280 >= 100000u) {
    if (_M0L5valueS280 >= 10000000u) {
      if (_M0L5valueS280 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS280 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS280 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS280 >= 1000u) {
    if (_M0L5valueS280 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS280 >= 100u) {
    return 3;
  } else if (_M0L5valueS280 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS266,
  uint32_t _M0L3numS278,
  int32_t _M0L12digit__startS267,
  int32_t _M0L10total__lenS279
) {
  int32_t _M0L6_2atmpS1241;
  uint32_t _M0L3numS256;
  int32_t _M0L6offsetS257;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1241 = _M0L10total__lenS279 - _M0L12digit__startS267;
  _M0L3numS256 = _M0L3numS278;
  _M0L6offsetS257 = _M0L6_2atmpS1241;
  while (1) {
    if (_M0L3numS256 >= 10000u) {
      uint32_t _M0L1tS258 = _M0L3numS256 / 10000u;
      uint32_t _M0L6_2atmpS1218 = _M0L3numS256 % 10000u;
      int32_t _M0L1rS259 = *(int32_t*)&_M0L6_2atmpS1218;
      int32_t _M0L2d1S260 = _M0L1rS259 / 100;
      int32_t _M0L2d2S261 = _M0L1rS259 % 100;
      int32_t _M0L6_2atmpS1217 = _M0L2d1S260 / 10;
      int32_t _M0L6_2atmpS1216 = 48 + _M0L6_2atmpS1217;
      int32_t _M0L6d1__hiS262 = (uint16_t)_M0L6_2atmpS1216;
      int32_t _M0L6_2atmpS1215 = _M0L2d1S260 % 10;
      int32_t _M0L6_2atmpS1214 = 48 + _M0L6_2atmpS1215;
      int32_t _M0L6d1__loS263 = (uint16_t)_M0L6_2atmpS1214;
      int32_t _M0L6_2atmpS1213 = _M0L2d2S261 / 10;
      int32_t _M0L6_2atmpS1212 = 48 + _M0L6_2atmpS1213;
      int32_t _M0L6d2__hiS264 = (uint16_t)_M0L6_2atmpS1212;
      int32_t _M0L6_2atmpS1211 = _M0L2d2S261 % 10;
      int32_t _M0L6_2atmpS1210 = 48 + _M0L6_2atmpS1211;
      int32_t _M0L6d2__loS265 = (uint16_t)_M0L6_2atmpS1210;
      int32_t _M0L6_2atmpS1202 = _M0L12digit__startS267 + _M0L6offsetS257;
      int32_t _M0L6_2atmpS1201 = _M0L6_2atmpS1202 - 4;
      int32_t _M0L6_2atmpS1204;
      int32_t _M0L6_2atmpS1203;
      int32_t _M0L6_2atmpS1206;
      int32_t _M0L6_2atmpS1205;
      int32_t _M0L6_2atmpS1208;
      int32_t _M0L6_2atmpS1207;
      int32_t _M0L6_2atmpS1209;
      _M0L6bufferS266[_M0L6_2atmpS1201] = _M0L6d1__hiS262;
      _M0L6_2atmpS1204 = _M0L12digit__startS267 + _M0L6offsetS257;
      _M0L6_2atmpS1203 = _M0L6_2atmpS1204 - 3;
      _M0L6bufferS266[_M0L6_2atmpS1203] = _M0L6d1__loS263;
      _M0L6_2atmpS1206 = _M0L12digit__startS267 + _M0L6offsetS257;
      _M0L6_2atmpS1205 = _M0L6_2atmpS1206 - 2;
      _M0L6bufferS266[_M0L6_2atmpS1205] = _M0L6d2__hiS264;
      _M0L6_2atmpS1208 = _M0L12digit__startS267 + _M0L6offsetS257;
      _M0L6_2atmpS1207 = _M0L6_2atmpS1208 - 1;
      _M0L6bufferS266[_M0L6_2atmpS1207] = _M0L6d2__loS265;
      _M0L6_2atmpS1209 = _M0L6offsetS257 - 4;
      _M0L3numS256 = _M0L1tS258;
      _M0L6offsetS257 = _M0L6_2atmpS1209;
      continue;
    } else {
      int32_t _M0L6_2atmpS1240 = *(int32_t*)&_M0L3numS256;
      int32_t _M0L9remainingS269 = _M0L6_2atmpS1240;
      int32_t _M0L6offsetS270 = _M0L6offsetS257;
      while (1) {
        if (_M0L9remainingS269 >= 100) {
          int32_t _M0L1tS271 = _M0L9remainingS269 / 100;
          int32_t _M0L1dS272 = _M0L9remainingS269 % 100;
          int32_t _M0L6_2atmpS1227 = _M0L1dS272 / 10;
          int32_t _M0L6_2atmpS1226 = 48 + _M0L6_2atmpS1227;
          int32_t _M0L5d__hiS273 = (uint16_t)_M0L6_2atmpS1226;
          int32_t _M0L6_2atmpS1225 = _M0L1dS272 % 10;
          int32_t _M0L6_2atmpS1224 = 48 + _M0L6_2atmpS1225;
          int32_t _M0L5d__loS274 = (uint16_t)_M0L6_2atmpS1224;
          int32_t _M0L6_2atmpS1220 = _M0L12digit__startS267 + _M0L6offsetS270;
          int32_t _M0L6_2atmpS1219 = _M0L6_2atmpS1220 - 2;
          int32_t _M0L6_2atmpS1222;
          int32_t _M0L6_2atmpS1221;
          int32_t _M0L6_2atmpS1223;
          _M0L6bufferS266[_M0L6_2atmpS1219] = _M0L5d__hiS273;
          _M0L6_2atmpS1222 = _M0L12digit__startS267 + _M0L6offsetS270;
          _M0L6_2atmpS1221 = _M0L6_2atmpS1222 - 1;
          _M0L6bufferS266[_M0L6_2atmpS1221] = _M0L5d__loS274;
          _M0L6_2atmpS1223 = _M0L6offsetS270 - 2;
          _M0L9remainingS269 = _M0L1tS271;
          _M0L6offsetS270 = _M0L6_2atmpS1223;
          continue;
        } else if (_M0L9remainingS269 >= 10) {
          int32_t _M0L6_2atmpS1235 = _M0L9remainingS269 / 10;
          int32_t _M0L6_2atmpS1234 = 48 + _M0L6_2atmpS1235;
          int32_t _M0L5d__hiS276 = (uint16_t)_M0L6_2atmpS1234;
          int32_t _M0L6_2atmpS1233 = _M0L9remainingS269 % 10;
          int32_t _M0L6_2atmpS1232 = 48 + _M0L6_2atmpS1233;
          int32_t _M0L5d__loS277 = (uint16_t)_M0L6_2atmpS1232;
          int32_t _M0L6_2atmpS1229 = _M0L12digit__startS267 + _M0L6offsetS270;
          int32_t _M0L6_2atmpS1228 = _M0L6_2atmpS1229 - 2;
          int32_t _M0L6_2atmpS1231;
          int32_t _M0L6_2atmpS1230;
          _M0L6bufferS266[_M0L6_2atmpS1228] = _M0L5d__hiS276;
          _M0L6_2atmpS1231 = _M0L12digit__startS267 + _M0L6offsetS270;
          _M0L6_2atmpS1230 = _M0L6_2atmpS1231 - 1;
          _M0L6bufferS266[_M0L6_2atmpS1230] = _M0L5d__loS277;
        } else {
          int32_t _M0L6_2atmpS1239 = _M0L12digit__startS267 + _M0L6offsetS270;
          int32_t _M0L6_2atmpS1236 = _M0L6_2atmpS1239 - 1;
          int32_t _M0L6_2atmpS1238 = 48 + _M0L9remainingS269;
          int32_t _M0L6_2atmpS1237 = (uint16_t)_M0L6_2atmpS1238;
          _M0L6bufferS266[_M0L6_2atmpS1236] = _M0L6_2atmpS1237;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS246,
  uint32_t _M0L3numS250,
  int32_t _M0L12digit__startS247,
  int32_t _M0L10total__lenS249,
  int32_t _M0L5radixS240
) {
  uint32_t _M0L4baseS239;
  int32_t _M0L6_2atmpS1186;
  int32_t _M0L6_2atmpS1185;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS239 = *(uint32_t*)&_M0L5radixS240;
  _M0L6_2atmpS1186 = _M0L5radixS240 - 1;
  _M0L6_2atmpS1185 = _M0L5radixS240 & _M0L6_2atmpS1186;
  if (_M0L6_2atmpS1185 == 0) {
    int32_t _M0L5shiftS241;
    uint32_t _M0L4maskS242;
    int32_t _M0L6_2atmpS1193;
    int32_t _M0L6offsetS243;
    uint32_t _M0L1nS244;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS241 = moonbit_ctz32(_M0L5radixS240);
    _M0L4maskS242 = _M0L4baseS239 - 1u;
    _M0L6_2atmpS1193 = _M0L10total__lenS249 - _M0L12digit__startS247;
    _M0L6offsetS243 = _M0L6_2atmpS1193;
    _M0L1nS244 = _M0L3numS250;
    while (1) {
      if (_M0L1nS244 > 0u) {
        uint32_t _M0L6_2atmpS1192 = _M0L1nS244 & _M0L4maskS242;
        int32_t _M0L5digitS245 = *(int32_t*)&_M0L6_2atmpS1192;
        int32_t _M0L6_2atmpS1189 = _M0L12digit__startS247 + _M0L6offsetS243;
        int32_t _M0L6_2atmpS1187 = _M0L6_2atmpS1189 - 1;
        int32_t _M0L6_2atmpS1188 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS245];
        int32_t _M0L6_2atmpS1190;
        uint32_t _M0L6_2atmpS1191;
        _M0L6bufferS246[_M0L6_2atmpS1187] = _M0L6_2atmpS1188;
        _M0L6_2atmpS1190 = _M0L6offsetS243 - 1;
        _M0L6_2atmpS1191 = _M0L1nS244 >> (_M0L5shiftS241 & 31);
        _M0L6offsetS243 = _M0L6_2atmpS1190;
        _M0L1nS244 = _M0L6_2atmpS1191;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1200 = _M0L10total__lenS249 - _M0L12digit__startS247;
    int32_t _M0L6offsetS251 = _M0L6_2atmpS1200;
    uint32_t _M0L1nS252 = _M0L3numS250;
    while (1) {
      if (_M0L1nS252 > 0u) {
        uint32_t _M0L1qS253 = _M0L1nS252 / _M0L4baseS239;
        uint32_t _M0L6_2atmpS1199 = _M0L1qS253 * _M0L4baseS239;
        uint32_t _M0L6_2atmpS1198 = _M0L1nS252 - _M0L6_2atmpS1199;
        int32_t _M0L5digitS254 = *(int32_t*)&_M0L6_2atmpS1198;
        int32_t _M0L6_2atmpS1196 = _M0L12digit__startS247 + _M0L6offsetS251;
        int32_t _M0L6_2atmpS1194 = _M0L6_2atmpS1196 - 1;
        int32_t _M0L6_2atmpS1195 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS254];
        int32_t _M0L6_2atmpS1197;
        _M0L6bufferS246[_M0L6_2atmpS1194] = _M0L6_2atmpS1195;
        _M0L6_2atmpS1197 = _M0L6offsetS251 - 1;
        _M0L6offsetS251 = _M0L6_2atmpS1197;
        _M0L1nS252 = _M0L1qS253;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS233,
  uint32_t _M0L3numS238,
  int32_t _M0L12digit__startS234,
  int32_t _M0L10total__lenS237
) {
  int32_t _M0L6_2atmpS1184;
  int32_t _M0L6offsetS228;
  uint32_t _M0L1nS229;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1184 = _M0L10total__lenS237 - _M0L12digit__startS234;
  _M0L6offsetS228 = _M0L6_2atmpS1184;
  _M0L1nS229 = _M0L3numS238;
  while (1) {
    if (_M0L6offsetS228 >= 2) {
      uint32_t _M0L6_2atmpS1181 = _M0L1nS229 & 255u;
      int32_t _M0L9byte__valS230 = *(int32_t*)&_M0L6_2atmpS1181;
      int32_t _M0L2hiS231 = _M0L9byte__valS230 / 16;
      int32_t _M0L2loS232 = _M0L9byte__valS230 % 16;
      int32_t _M0L6_2atmpS1175 = _M0L12digit__startS234 + _M0L6offsetS228;
      int32_t _M0L6_2atmpS1173 = _M0L6_2atmpS1175 - 2;
      int32_t _M0L6_2atmpS1174 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L2hiS231];
      int32_t _M0L6_2atmpS1178;
      int32_t _M0L6_2atmpS1176;
      int32_t _M0L6_2atmpS1177;
      int32_t _M0L6_2atmpS1179;
      uint32_t _M0L6_2atmpS1180;
      _M0L6bufferS233[_M0L6_2atmpS1173] = _M0L6_2atmpS1174;
      _M0L6_2atmpS1178 = _M0L12digit__startS234 + _M0L6offsetS228;
      _M0L6_2atmpS1176 = _M0L6_2atmpS1178 - 1;
      _M0L6_2atmpS1177
      = ((moonbit_string_t)moonbit_string_literal_17.data)[
        _M0L2loS232
      ];
      _M0L6bufferS233[_M0L6_2atmpS1176] = _M0L6_2atmpS1177;
      _M0L6_2atmpS1179 = _M0L6offsetS228 - 2;
      _M0L6_2atmpS1180 = _M0L1nS229 >> 8;
      _M0L6offsetS228 = _M0L6_2atmpS1179;
      _M0L1nS229 = _M0L6_2atmpS1180;
      continue;
    } else if (_M0L6offsetS228 == 1) {
      uint32_t _M0L6_2atmpS1183 = _M0L1nS229 & 15u;
      int32_t _M0L6nibbleS236 = *(int32_t*)&_M0L6_2atmpS1183;
      int32_t _M0L6_2atmpS1182 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L6nibbleS236];
      _M0L6bufferS233[_M0L12digit__startS234] = _M0L6_2atmpS1182;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS227
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS226;
  struct _M0TPB6Logger _M0L6_2atmpS1172;
  moonbit_string_t _result_2009;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS226);
  _M0L6_2atmpS1172
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS226
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS227, _M0L6_2atmpS1172);
  if (_M0L6_2atmpS1172.$1) {
    moonbit_decref(_M0L6_2atmpS1172.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2009 = _M0MPB13StringBuilder10to__string(_M0L6loggerS226);
  moonbit_decref_cycle_free(_M0L6loggerS226);
  return _result_2009;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS221,
  struct _M0TPB6Logger _M0L6loggerS220
) {
  moonbit_string_t _M0L6_2atmpS1169;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1169 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS221);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS220.$0->$method_0(_M0L6loggerS220.$1, _M0L6_2atmpS1169);
  moonbit_decref_cycle_free(_M0L6_2atmpS1169);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS223,
  struct _M0TPB6Logger _M0L6loggerS222
) {
  moonbit_string_t _M0L6_2atmpS1170;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1170 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS223);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS222.$0->$method_0(_M0L6loggerS222.$1, _M0L6_2atmpS1170);
  moonbit_decref_cycle_free(_M0L6_2atmpS1170);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS225,
  struct _M0TPB6Logger _M0L6loggerS224
) {
  moonbit_string_t _M0L6_2atmpS1171;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1171 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS225);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS224.$0->$method_0(_M0L6loggerS224.$1, _M0L6_2atmpS1171);
  moonbit_decref_cycle_free(_M0L6_2atmpS1171);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS219
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS219.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS218
) {
  moonbit_string_t _M0L8_2afieldS1912;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS1912 = _M0L4selfS218.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1912);
  return _M0L8_2afieldS1912;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS214,
  moonbit_string_t _M0L5valueS215,
  int32_t _M0L5startS216,
  int32_t _M0L3lenS217
) {
  int32_t _M0L6_2atmpS1168;
  int64_t _M0L6_2atmpS1167;
  struct _M0TPC16string10StringView _M0L6_2atmpS1166;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1168 = _M0L5startS216 + _M0L3lenS217;
  _M0L6_2atmpS1167 = (int64_t)_M0L6_2atmpS1168;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1166
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS215, _M0L5startS216, _M0L6_2atmpS1167);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS214, _M0L6_2atmpS1166);
  moonbit_decref_cycle_free(_M0L6_2atmpS1166.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String21clamped__view_2einner(
  moonbit_string_t _M0L4selfS207,
  int32_t _M0L5startS209,
  int64_t _M0L3endS211
) {
  int32_t _M0L3lenS206;
  int32_t _M0Lm2loS208;
  int32_t _M0Lm2hiS210;
  int32_t _M0L6_2atmpS1150;
  int32_t _if__result_2010;
  int32_t _M0L6_2atmpS1158;
  int32_t _if__result_2011;
  int32_t _M0L6_2atmpS1160;
  int32_t _M0L6_2atmpS1161;
  #line 698 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS206 = Moonbit_array_length(_M0L4selfS207);
  if (_M0L5startS209 < 0) {
    _M0Lm2loS208 = 0;
  } else if (_M0L5startS209 > _M0L3lenS206) {
    _M0Lm2loS208 = _M0L3lenS206;
  } else {
    _M0Lm2loS208 = _M0L5startS209;
  }
  if (_M0L3endS211 == 4294967296ll) {
    _M0Lm2hiS210 = _M0L3lenS206;
  } else {
    int64_t _M0L7_2aSomeS212 = _M0L3endS211;
    int32_t _M0L4_2aeS213 = (int32_t)_M0L7_2aSomeS212;
    if (_M0L4_2aeS213 < 0) {
      _M0Lm2hiS210 = 0;
    } else if (_M0L4_2aeS213 > _M0L3lenS206) {
      _M0Lm2hiS210 = _M0L3lenS206;
    } else {
      _M0Lm2hiS210 = _M0L4_2aeS213;
    }
  }
  _M0L6_2atmpS1150 = _M0Lm2loS208;
  if (_M0L6_2atmpS1150 > 0) {
    int32_t _M0L6_2atmpS1149 = _M0Lm2loS208;
    if (_M0L6_2atmpS1149 < _M0L3lenS206) {
      int32_t _M0L6_2atmpS1148 = _M0Lm2loS208;
      int32_t _M0L6_2atmpS1147 = _M0L4selfS207[_M0L6_2atmpS1148];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1147)) {
        int32_t _M0L6_2atmpS1146 = _M0Lm2loS208;
        int32_t _M0L6_2atmpS1145 = _M0L6_2atmpS1146 - 1;
        int32_t _M0L6_2atmpS1144 = _M0L4selfS207[_M0L6_2atmpS1145];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2010
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1144);
      } else {
        _if__result_2010 = 0;
      }
    } else {
      _if__result_2010 = 0;
    }
  } else {
    _if__result_2010 = 0;
  }
  if (_if__result_2010) {
    int32_t _M0L6_2atmpS1151 = _M0Lm2loS208;
    _M0Lm2loS208 = _M0L6_2atmpS1151 + 1;
  }
  _M0L6_2atmpS1158 = _M0Lm2hiS210;
  if (_M0L6_2atmpS1158 > 0) {
    int32_t _M0L6_2atmpS1157 = _M0Lm2hiS210;
    if (_M0L6_2atmpS1157 < _M0L3lenS206) {
      int32_t _M0L6_2atmpS1156 = _M0Lm2hiS210;
      int32_t _M0L6_2atmpS1155 = _M0L4selfS207[_M0L6_2atmpS1156];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1155)) {
        int32_t _M0L6_2atmpS1154 = _M0Lm2hiS210;
        int32_t _M0L6_2atmpS1153 = _M0L6_2atmpS1154 - 1;
        int32_t _M0L6_2atmpS1152 = _M0L4selfS207[_M0L6_2atmpS1153];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2011
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1152);
      } else {
        _if__result_2011 = 0;
      }
    } else {
      _if__result_2011 = 0;
    }
  } else {
    _if__result_2011 = 0;
  }
  if (_if__result_2011) {
    int32_t _M0L6_2atmpS1159 = _M0Lm2hiS210;
    _M0Lm2hiS210 = _M0L6_2atmpS1159 - 1;
  }
  _M0L6_2atmpS1160 = _M0Lm2loS208;
  _M0L6_2atmpS1161 = _M0Lm2hiS210;
  if (_M0L6_2atmpS1160 >= _M0L6_2atmpS1161) {
    int32_t _M0L6_2atmpS1162 = _M0Lm2loS208;
    int32_t _M0L6_2atmpS1163 = _M0Lm2loS208;
    moonbit_incref_cycle_free(_M0L4selfS207);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS207,
                                                 .$1 = _M0L6_2atmpS1162,
                                                 .$2 = _M0L6_2atmpS1163};
  } else {
    int32_t _M0L6_2atmpS1164 = _M0Lm2loS208;
    int32_t _M0L6_2atmpS1165 = _M0Lm2hiS210;
    moonbit_incref_cycle_free(_M0L4selfS207);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS207,
                                                 .$1 = _M0L6_2atmpS1164,
                                                 .$2 = _M0L6_2atmpS1165};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS205,
  struct _M0TPB4Show _M0L4showS204
) {
  struct _M0TPB6Logger _M0L6_2atmpS1143;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS205);
  _M0L6_2atmpS1143
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS205
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS204.$0->$method_0(_M0L4showS204.$1, _M0L6_2atmpS1143);
  if (_M0L6_2atmpS1143.$1) {
    moonbit_decref(_M0L6_2atmpS1143.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS203,
  struct _M0TPB4Show _M0L4showS202
) {
  struct _M0TPB6Logger _M0L6_2atmpS1142;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS203);
  _M0L6_2atmpS1142
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS203
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS202.$0->$method_0(_M0L4showS202.$1, _M0L6_2atmpS1142);
  if (_M0L6_2atmpS1142.$1) {
    moonbit_decref(_M0L6_2atmpS1142.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS201) {
  int64_t _M0L6_2atmpS1141;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1141 = (int64_t)_M0L4selfS201;
  return *(uint64_t*)&_M0L6_2atmpS1141;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

moonbit_string_t _M0MPC16string6String14escape_2einner(
  moonbit_string_t _M0L4selfS199,
  int32_t _M0L5quoteS200
) {
  struct _M0TPB13StringBuilder* _M0L3bufS198;
  int32_t _M0L6_2atmpS1140;
  struct _M0TPC16string10StringView _M0L6_2atmpS1138;
  struct _M0TPB6Logger _M0L6_2atmpS1139;
  moonbit_string_t _result_2012;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1140 = Moonbit_array_length(_M0L4selfS199);
  moonbit_incref_cycle_free(_M0L4selfS199);
  _M0L6_2atmpS1138
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS199, .$1 = 0, .$2 = _M0L6_2atmpS1140
  };
  moonbit_incref_cycle_free(_M0L3bufS198);
  _M0L6_2atmpS1139
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS198
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1138, _M0L6_2atmpS1139, _M0L5quoteS200);
  moonbit_decref_cycle_free(_M0L6_2atmpS1138.$0);
  if (_M0L6_2atmpS1139.$1) {
    moonbit_decref(_M0L6_2atmpS1139.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2012 = _M0MPB13StringBuilder10to__string(_M0L3bufS198);
  moonbit_decref_cycle_free(_M0L3bufS198);
  return _result_2012;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS190,
  struct _M0TPB6Logger _M0L6loggerS188,
  int32_t _M0L5quoteS187
) {
  int32_t _M0L3endS1136;
  int32_t _M0L5startS1137;
  int32_t _M0L3lenS189;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS191;
  int32_t _M0L1iS192;
  int32_t _M0L3segS193;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS187) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS188.$0->$method_3(_M0L6loggerS188.$1, 34);
  }
  _M0L3endS1136 = _M0L4selfS190.$2;
  _M0L5startS1137 = _M0L4selfS190.$1;
  _M0L3lenS189 = _M0L3endS1136 - _M0L5startS1137;
  moonbit_incref_cycle_free(_M0L4selfS190.$0);
  if (_M0L6loggerS188.$1) {
    moonbit_incref(_M0L6loggerS188.$1);
  }
  _M0L6_2aenvS191
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS191)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 44, 0);
  _M0L6_2aenvS191->$0 = _M0L4selfS190;
  _M0L6_2aenvS191->$1 = _M0L6loggerS188;
  _M0L1iS192 = 0;
  _M0L3segS193 = 0;
  _2afor_194:;
  while (1) {
    moonbit_string_t _M0L3strS1133;
    int32_t _M0L5startS1135;
    int32_t _M0L6_2atmpS1134;
    int32_t _M0L4codeS195;
    int32_t _M0L1cS197;
    int32_t _M0L6_2atmpS1117;
    int32_t _M0L6_2atmpS1118;
    int32_t _M0L6_2atmpS1119;
    if (_M0L1iS192 >= _M0L3lenS189) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
      moonbit_decref_cycle_free(_M0L6_2aenvS191);
      break;
    }
    _M0L3strS1133 = _M0L4selfS190.$0;
    _M0L5startS1135 = _M0L4selfS190.$1;
    _M0L6_2atmpS1134 = _M0L5startS1135 + _M0L1iS192;
    _M0L4codeS195 = _M0L3strS1133[_M0L6_2atmpS1134];
    switch (_M0L4codeS195) {
      case 34: {
        _M0L1cS197 = _M0L4codeS195;
        goto join_196;
        break;
      }
      
      case 92: {
        _M0L1cS197 = _M0L4codeS195;
        goto join_196;
        break;
      }
      
      case 10: {
        int32_t _M0L6_2atmpS1120;
        int32_t _M0L6_2atmpS1121;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_18.data);
        _M0L6_2atmpS1120 = _M0L1iS192 + 1;
        _M0L6_2atmpS1121 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1120;
        _M0L3segS193 = _M0L6_2atmpS1121;
        goto _2afor_194;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1122;
        int32_t _M0L6_2atmpS1123;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_19.data);
        _M0L6_2atmpS1122 = _M0L1iS192 + 1;
        _M0L6_2atmpS1123 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1122;
        _M0L3segS193 = _M0L6_2atmpS1123;
        goto _2afor_194;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1124;
        int32_t _M0L6_2atmpS1125;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_20.data);
        _M0L6_2atmpS1124 = _M0L1iS192 + 1;
        _M0L6_2atmpS1125 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1124;
        _M0L3segS193 = _M0L6_2atmpS1125;
        goto _2afor_194;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1126;
        int32_t _M0L6_2atmpS1127;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS1126 = _M0L1iS192 + 1;
        _M0L6_2atmpS1127 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1126;
        _M0L3segS193 = _M0L6_2atmpS1127;
        goto _2afor_194;
        break;
      }
      default: {
        if (_M0L4codeS195 < 32) {
          int32_t _M0L6_2atmpS1129;
          moonbit_string_t _M0L6_2atmpS1128;
          int32_t _M0L6_2atmpS1130;
          int32_t _M0L6_2atmpS1131;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_22.data);
          _M0L6_2atmpS1129 = _M0L4codeS195 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1128 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1129);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, _M0L6_2atmpS1128);
          moonbit_decref_cycle_free(_M0L6_2atmpS1128);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1130 = _M0L1iS192 + 1;
          _M0L6_2atmpS1131 = _M0L1iS192 + 1;
          _M0L1iS192 = _M0L6_2atmpS1130;
          _M0L3segS193 = _M0L6_2atmpS1131;
          goto _2afor_194;
        } else {
          int32_t _M0L6_2atmpS1132 = _M0L1iS192 + 1;
          int32_t _tmp_2015 = _M0L3segS193;
          _M0L1iS192 = _M0L6_2atmpS1132;
          _M0L3segS193 = _tmp_2015;
          goto _2afor_194;
        }
        break;
      }
    }
    goto joinlet_2014;
    join_196:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS188.$0->$method_3(_M0L6loggerS188.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1117 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS197);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS188.$0->$method_3(_M0L6loggerS188.$1, _M0L6_2atmpS1117);
    _M0L6_2atmpS1118 = _M0L1iS192 + 1;
    _M0L6_2atmpS1119 = _M0L1iS192 + 1;
    _M0L1iS192 = _M0L6_2atmpS1118;
    _M0L3segS193 = _M0L6_2atmpS1119;
    continue;
    joinlet_2014:;
    break;
  }
  if (_M0L5quoteS187) {
    #line 202 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS188.$0->$method_3(_M0L6loggerS188.$1, 34);
  }
  return 0;
}

int32_t _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS183,
  int32_t _M0L3segS186,
  int32_t _M0L1iS185
) {
  struct _M0TPB6Logger _M0L6loggerS182;
  struct _M0TPC16string10StringView _M0L4selfS184;
  #line 153 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6loggerS182 = _M0L6_2aenvS183->$1;
  _M0L4selfS184 = _M0L6_2aenvS183->$0;
  if (_M0L1iS185 > _M0L3segS186) {
    int64_t _M0L6_2atmpS1116 = (int64_t)_M0L1iS185;
    struct _M0TPC16string10StringView _M0L6_2atmpS1115;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1115
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS184, _M0L3segS186, _M0L6_2atmpS1116);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS182.$0->$method_2(_M0L6loggerS182.$1, _M0L6_2atmpS1115);
    moonbit_decref_cycle_free(_M0L6_2atmpS1115.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS173,
  int32_t _M0L5startS175,
  int64_t _M0L3endS177
) {
  int32_t _M0L3endS1113;
  int32_t _M0L5startS1114;
  int32_t _M0L3lenS172;
  int32_t _M0Lm2loS174;
  int32_t _M0Lm2hiS176;
  moonbit_string_t _M0L3strS180;
  int32_t _M0L4baseS181;
  int32_t _M0L6_2atmpS1091;
  int32_t _if__result_2016;
  int32_t _M0L6_2atmpS1101;
  int32_t _if__result_2017;
  int32_t _M0L6_2atmpS1103;
  int32_t _M0L6_2atmpS1104;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1113 = _M0L4selfS173.$2;
  _M0L5startS1114 = _M0L4selfS173.$1;
  _M0L3lenS172 = _M0L3endS1113 - _M0L5startS1114;
  if (_M0L5startS175 < 0) {
    _M0Lm2loS174 = 0;
  } else if (_M0L5startS175 > _M0L3lenS172) {
    _M0Lm2loS174 = _M0L3lenS172;
  } else {
    _M0Lm2loS174 = _M0L5startS175;
  }
  if (_M0L3endS177 == 4294967296ll) {
    _M0Lm2hiS176 = _M0L3lenS172;
  } else {
    int64_t _M0L7_2aSomeS178 = _M0L3endS177;
    int32_t _M0L4_2aeS179 = (int32_t)_M0L7_2aSomeS178;
    if (_M0L4_2aeS179 < 0) {
      _M0Lm2hiS176 = 0;
    } else if (_M0L4_2aeS179 > _M0L3lenS172) {
      _M0Lm2hiS176 = _M0L3lenS172;
    } else {
      _M0Lm2hiS176 = _M0L4_2aeS179;
    }
  }
  _M0L3strS180 = _M0L4selfS173.$0;
  _M0L4baseS181 = _M0L4selfS173.$1;
  _M0L6_2atmpS1091 = _M0Lm2loS174;
  if (_M0L6_2atmpS1091 > 0) {
    int32_t _M0L6_2atmpS1090 = _M0Lm2loS174;
    if (_M0L6_2atmpS1090 < _M0L3lenS172) {
      int32_t _M0L6_2atmpS1089 = _M0Lm2loS174;
      int32_t _M0L6_2atmpS1088 = _M0L4baseS181 + _M0L6_2atmpS1089;
      int32_t _M0L6_2atmpS1087 = _M0L3strS180[_M0L6_2atmpS1088];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1087)) {
        int32_t _M0L6_2atmpS1086 = _M0Lm2loS174;
        int32_t _M0L6_2atmpS1085 = _M0L4baseS181 + _M0L6_2atmpS1086;
        int32_t _M0L6_2atmpS1084 = _M0L6_2atmpS1085 - 1;
        int32_t _M0L6_2atmpS1083 = _M0L3strS180[_M0L6_2atmpS1084];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2016
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1083);
      } else {
        _if__result_2016 = 0;
      }
    } else {
      _if__result_2016 = 0;
    }
  } else {
    _if__result_2016 = 0;
  }
  if (_if__result_2016) {
    int32_t _M0L6_2atmpS1092 = _M0Lm2loS174;
    _M0Lm2loS174 = _M0L6_2atmpS1092 + 1;
  }
  _M0L6_2atmpS1101 = _M0Lm2hiS176;
  if (_M0L6_2atmpS1101 > 0) {
    int32_t _M0L6_2atmpS1100 = _M0Lm2hiS176;
    if (_M0L6_2atmpS1100 < _M0L3lenS172) {
      int32_t _M0L6_2atmpS1099 = _M0Lm2hiS176;
      int32_t _M0L6_2atmpS1098 = _M0L4baseS181 + _M0L6_2atmpS1099;
      int32_t _M0L6_2atmpS1097 = _M0L3strS180[_M0L6_2atmpS1098];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1097)) {
        int32_t _M0L6_2atmpS1096 = _M0Lm2hiS176;
        int32_t _M0L6_2atmpS1095 = _M0L4baseS181 + _M0L6_2atmpS1096;
        int32_t _M0L6_2atmpS1094 = _M0L6_2atmpS1095 - 1;
        int32_t _M0L6_2atmpS1093 = _M0L3strS180[_M0L6_2atmpS1094];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2017
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1093);
      } else {
        _if__result_2017 = 0;
      }
    } else {
      _if__result_2017 = 0;
    }
  } else {
    _if__result_2017 = 0;
  }
  if (_if__result_2017) {
    int32_t _M0L6_2atmpS1102 = _M0Lm2hiS176;
    _M0Lm2hiS176 = _M0L6_2atmpS1102 - 1;
  }
  _M0L6_2atmpS1103 = _M0Lm2loS174;
  _M0L6_2atmpS1104 = _M0Lm2hiS176;
  if (_M0L6_2atmpS1103 >= _M0L6_2atmpS1104) {
    int32_t _M0L6_2atmpS1108 = _M0Lm2loS174;
    int32_t _M0L6_2atmpS1105 = _M0L4baseS181 + _M0L6_2atmpS1108;
    int32_t _M0L6_2atmpS1107 = _M0Lm2loS174;
    int32_t _M0L6_2atmpS1106 = _M0L4baseS181 + _M0L6_2atmpS1107;
    moonbit_incref_cycle_free(_M0L3strS180);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS180,
                                                 .$1 = _M0L6_2atmpS1105,
                                                 .$2 = _M0L6_2atmpS1106};
  } else {
    int32_t _M0L6_2atmpS1112 = _M0Lm2loS174;
    int32_t _M0L6_2atmpS1109 = _M0L4baseS181 + _M0L6_2atmpS1112;
    int32_t _M0L6_2atmpS1111 = _M0Lm2hiS176;
    int32_t _M0L6_2atmpS1110 = _M0L4baseS181 + _M0L6_2atmpS1111;
    moonbit_incref_cycle_free(_M0L3strS180);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS180,
                                                 .$1 = _M0L6_2atmpS1109,
                                                 .$2 = _M0L6_2atmpS1110};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS171) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS170;
  int32_t _M0L6_2atmpS1080;
  int32_t _M0L6_2atmpS1079;
  int32_t _M0L6_2atmpS1082;
  int32_t _M0L6_2atmpS1081;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1078;
  moonbit_string_t _result_2018;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS170 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1080 = _M0IPC14byte4BytePB3Div3div(_M0L1bS171, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1079
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1080);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS170, _M0L6_2atmpS1079);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1082 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS171, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1081
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1082);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS170, _M0L6_2atmpS1081);
  _M0L6_2atmpS1078 = _M0L7_2aselfS170;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2018 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1078);
  moonbit_decref_cycle_free(_M0L6_2atmpS1078);
  return _result_2018;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS169) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS169 < 10) {
    int32_t _M0L6_2atmpS1075;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1075 = _M0IPC14byte4BytePB3Add3add(_M0L1iS169, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1075);
  } else {
    int32_t _M0L6_2atmpS1077;
    int32_t _M0L6_2atmpS1076;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1077 = _M0IPC14byte4BytePB3Add3add(_M0L1iS169, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1076 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1077, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1076);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS167,
  int32_t _M0L4thatS168
) {
  int32_t _M0L6_2atmpS1073;
  int32_t _M0L6_2atmpS1074;
  int32_t _M0L6_2atmpS1072;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1073 = (int32_t)_M0L4selfS167;
  _M0L6_2atmpS1074 = (int32_t)_M0L4thatS168;
  _M0L6_2atmpS1072 = _M0L6_2atmpS1073 - _M0L6_2atmpS1074;
  return _M0L6_2atmpS1072 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS165,
  int32_t _M0L4thatS166
) {
  int32_t _M0L6_2atmpS1070;
  int32_t _M0L6_2atmpS1071;
  int32_t _M0L6_2atmpS1069;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1070 = (int32_t)_M0L4selfS165;
  _M0L6_2atmpS1071 = (int32_t)_M0L4thatS166;
  _M0L6_2atmpS1069 = _M0L6_2atmpS1070 % _M0L6_2atmpS1071;
  return _M0L6_2atmpS1069 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS163,
  int32_t _M0L4thatS164
) {
  int32_t _M0L6_2atmpS1067;
  int32_t _M0L6_2atmpS1068;
  int32_t _M0L6_2atmpS1066;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1067 = (int32_t)_M0L4selfS163;
  _M0L6_2atmpS1068 = (int32_t)_M0L4thatS164;
  _M0L6_2atmpS1066 = _M0L6_2atmpS1067 / _M0L6_2atmpS1068;
  return _M0L6_2atmpS1066 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS161,
  int32_t _M0L4thatS162
) {
  int32_t _M0L6_2atmpS1064;
  int32_t _M0L6_2atmpS1065;
  int32_t _M0L6_2atmpS1063;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1064 = (int32_t)_M0L4selfS161;
  _M0L6_2atmpS1065 = (int32_t)_M0L4thatS162;
  _M0L6_2atmpS1063 = _M0L6_2atmpS1064 + _M0L6_2atmpS1065;
  return _M0L6_2atmpS1063 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS160) {
  int32_t _M0L6_2atmpS1062;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1062 = (int32_t)_M0L4selfS160;
  return _M0L6_2atmpS1062;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS159) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS159 >= 56320 && _M0L4selfS159 <= 57343;
}

int32_t _M0MPC16uint166UInt1622is__leading__surrogate(int32_t _M0L4selfS158) {
  #line 28 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS158 >= 55296 && _M0L4selfS158 <= 56319;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS157,
  moonbit_string_t _M0L3strS155
) {
  int32_t _M0L8str__lenS154;
  int32_t _M0L3lenS1061;
  int32_t _M0L8requiredS156;
  uint16_t* _M0L4dataS1056;
  int32_t _M0L6_2atmpS1055;
  int32_t _if__result_2019;
  uint16_t* _M0L4dataS1057;
  int32_t _M0L3lenS1058;
  int32_t _M0L3lenS1060;
  int32_t _M0L6_2atmpS1059;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS154 = Moonbit_array_length(_M0L3strS155);
  if (_M0L8str__lenS154 == 0) {
    return 0;
  }
  _M0L3lenS1061 = _M0L4selfS157->$1;
  _M0L8requiredS156 = _M0L3lenS1061 + _M0L8str__lenS154;
  _M0L4dataS1056 = _M0L4selfS157->$0;
  _M0L6_2atmpS1055 = Moonbit_array_length(_M0L4dataS1056);
  if (_M0L8requiredS156 > _M0L6_2atmpS1055) {
    _if__result_2019 = 1;
  } else {
    int32_t _M0L3lenS1054 = _M0L4selfS157->$1;
    _if__result_2019 = _M0L8requiredS156 < _M0L3lenS1054;
  }
  if (_if__result_2019) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS157, _M0L8requiredS156);
  }
  _M0L4dataS1057 = _M0L4selfS157->$0;
  _M0L3lenS1058 = _M0L4selfS157->$1;
  moonbit_incref_cycle_free(_M0L4dataS1057);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1057, _M0L3lenS1058, _M0L3strS155, 0, _M0L8str__lenS154);
  moonbit_decref_cycle_free(_M0L4dataS1057);
  _M0L3lenS1060 = _M0L4selfS157->$1;
  _M0L6_2atmpS1059 = _M0L3lenS1060 + _M0L8str__lenS154;
  _M0L4selfS157->$1 = _M0L6_2atmpS1059;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS150,
  int32_t _M0L11dst__offsetS153,
  moonbit_string_t _M0L3strS151,
  int32_t _M0L11str__offsetS146,
  int32_t _M0L3lenS147
) {
  int32_t _M0L16end__str__offsetS145;
  int32_t _M0L1iS148;
  int32_t _M0L1jS149;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS145 = _M0L11str__offsetS146 + _M0L3lenS147;
  _M0L1iS148 = _M0L11str__offsetS146;
  _M0L1jS149 = _M0L11dst__offsetS153;
  while (1) {
    if (_M0L1iS148 < _M0L16end__str__offsetS145) {
      int32_t _M0L6_2atmpS1051 = _M0L3strS151[_M0L1iS148];
      int32_t _M0L6_2atmpS1052;
      int32_t _M0L6_2atmpS1053;
      _M0L4selfS150[_M0L1jS149] = _M0L6_2atmpS1051;
      _M0L6_2atmpS1052 = _M0L1iS148 + 1;
      _M0L6_2atmpS1053 = _M0L1jS149 + 1;
      _M0L1iS148 = _M0L6_2atmpS1052;
      _M0L1jS149 = _M0L6_2atmpS1053;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS143,
  int32_t _M0L2chS142
) {
  uint32_t _M0L4codeS141;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS141 = _M0MPC14char4Char8to__uint(_M0L2chS142);
  if (_M0L4codeS141 <= 65535u) {
    int32_t _M0L3lenS1022 = _M0L4selfS143->$1;
    uint16_t* _M0L4dataS1024 = _M0L4selfS143->$0;
    int32_t _M0L6_2atmpS1023 = Moonbit_array_length(_M0L4dataS1024);
    uint16_t* _M0L4dataS1027;
    int32_t _M0L3lenS1028;
    int32_t _M0L6_2atmpS1029;
    int32_t _M0L3lenS1031;
    int32_t _M0L6_2atmpS1030;
    if (_M0L3lenS1022 >= _M0L6_2atmpS1023) {
      int32_t _M0L3lenS1026 = _M0L4selfS143->$1;
      int32_t _M0L6_2atmpS1025 = _M0L3lenS1026 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS143, _M0L6_2atmpS1025);
    }
    _M0L4dataS1027 = _M0L4selfS143->$0;
    _M0L3lenS1028 = _M0L4selfS143->$1;
    moonbit_incref_cycle_free(_M0L4dataS1027);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1029 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS141);
    if (
      _M0L3lenS1028 < 0
      || _M0L3lenS1028 >= Moonbit_array_length(_M0L4dataS1027)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1027[_M0L3lenS1028] = _M0L6_2atmpS1029;
    moonbit_decref_cycle_free(_M0L4dataS1027);
    _M0L3lenS1031 = _M0L4selfS143->$1;
    _M0L6_2atmpS1030 = _M0L3lenS1031 + 1;
    _M0L4selfS143->$1 = _M0L6_2atmpS1030;
  } else if (_M0L4codeS141 <= 1114111u) {
    uint16_t* _M0L4dataS1035 = _M0L4selfS143->$0;
    int32_t _M0L6_2atmpS1033 = Moonbit_array_length(_M0L4dataS1035);
    int32_t _M0L3lenS1034 = _M0L4selfS143->$1;
    int32_t _M0L6_2atmpS1032 = _M0L6_2atmpS1033 - _M0L3lenS1034;
    uint32_t _M0L4codeS144;
    uint16_t* _M0L4dataS1038;
    int32_t _M0L3lenS1039;
    uint32_t _M0L6_2atmpS1042;
    uint32_t _M0L6_2atmpS1041;
    int32_t _M0L6_2atmpS1040;
    uint16_t* _M0L4dataS1043;
    int32_t _M0L3lenS1048;
    int32_t _M0L6_2atmpS1044;
    uint32_t _M0L6_2atmpS1047;
    uint32_t _M0L6_2atmpS1046;
    int32_t _M0L6_2atmpS1045;
    int32_t _M0L3lenS1050;
    int32_t _M0L6_2atmpS1049;
    if (_M0L6_2atmpS1032 < 2) {
      int32_t _M0L3lenS1037 = _M0L4selfS143->$1;
      int32_t _M0L6_2atmpS1036 = _M0L3lenS1037 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS143, _M0L6_2atmpS1036);
    }
    _M0L4codeS144 = _M0L4codeS141 - 65536u;
    _M0L4dataS1038 = _M0L4selfS143->$0;
    _M0L3lenS1039 = _M0L4selfS143->$1;
    _M0L6_2atmpS1042 = _M0L4codeS144 >> 10;
    _M0L6_2atmpS1041 = 55296u + _M0L6_2atmpS1042;
    moonbit_incref_cycle_free(_M0L4dataS1038);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1040 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1041);
    if (
      _M0L3lenS1039 < 0
      || _M0L3lenS1039 >= Moonbit_array_length(_M0L4dataS1038)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1038[_M0L3lenS1039] = _M0L6_2atmpS1040;
    moonbit_decref_cycle_free(_M0L4dataS1038);
    _M0L4dataS1043 = _M0L4selfS143->$0;
    _M0L3lenS1048 = _M0L4selfS143->$1;
    _M0L6_2atmpS1044 = _M0L3lenS1048 + 1;
    _M0L6_2atmpS1047 = _M0L4codeS144 & 1023u;
    _M0L6_2atmpS1046 = 56320u + _M0L6_2atmpS1047;
    moonbit_incref_cycle_free(_M0L4dataS1043);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1045 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1046);
    if (
      _M0L6_2atmpS1044 < 0
      || _M0L6_2atmpS1044 >= Moonbit_array_length(_M0L4dataS1043)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1043[_M0L6_2atmpS1044] = _M0L6_2atmpS1045;
    moonbit_decref_cycle_free(_M0L4dataS1043);
    _M0L3lenS1050 = _M0L4selfS143->$1;
    _M0L6_2atmpS1049 = _M0L3lenS1050 + 2;
    _M0L4selfS143->$1 = _M0L6_2atmpS1049;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_23.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS138,
  int32_t _M0L8requiredS139
) {
  uint16_t* _M0L4dataS1021;
  int32_t _M0L6_2atmpS1019;
  int32_t _M0L3lenS1020;
  int32_t _M0L13new__capacityS137;
  uint16_t* _M0L4dataS1016;
  int32_t _M0L6_2atmpS1017;
  int32_t _M0L3lenS1018;
  uint16_t* _M0L9new__dataS140;
  uint16_t* _M0L6_2aoldS1913;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1021 = _M0L4selfS138->$0;
  _M0L6_2atmpS1019 = Moonbit_array_length(_M0L4dataS1021);
  _M0L3lenS1020 = _M0L4selfS138->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS137
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1019, _M0L3lenS1020, _M0L8requiredS139);
  _M0L4dataS1016 = _M0L4selfS138->$0;
  moonbit_incref_cycle_free(_M0L4dataS1016);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1017 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1018 = _M0L4selfS138->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS140
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1016, _M0L13new__capacityS137, _M0L6_2atmpS1017, _M0L3lenS1018, 0, 0);
  _M0L6_2aoldS1913 = _M0L4selfS138->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1913);
  _M0L4selfS138->$0 = _M0L9new__dataS140;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS136,
  int32_t _M0L3lenS132,
  int32_t _M0L8requiredS131
) {
  int32_t _M0L5spaceS133;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS131 < _M0L3lenS132) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_24.data);
  }
  _M0L5spaceS133 = _M0L7currentS136;
  while (1) {
    if (_M0L5spaceS133 < _M0L8requiredS131) {
      int32_t _M0L4nextS134 = _M0L5spaceS133 * 2;
      if (_M0L4nextS134 <= _M0L5spaceS133) {
        return _M0L8requiredS131;
      }
      _M0L5spaceS133 = _M0L4nextS134;
      continue;
    } else {
      return _M0L5spaceS133;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS130) {
  int32_t _M0L6_2atmpS1015;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1015 = *(int32_t*)&_M0L4selfS130;
  return (uint16_t)_M0L6_2atmpS1015;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS129) {
  int32_t _M0L6_2atmpS1014;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1014 = _M0L4selfS129;
  return *(uint32_t*)&_M0L6_2atmpS1014;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS127
) {
  int32_t _M0L3lenS1005;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1005 = _M0L4selfS127->$1;
  if (_M0L3lenS1005 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1006 = _M0L4selfS127->$1;
    uint16_t* _M0L4dataS1008 = _M0L4selfS127->$0;
    int32_t _M0L6_2atmpS1007 = Moonbit_array_length(_M0L4dataS1008);
    if (_M0L3lenS1006 == _M0L6_2atmpS1007) {
      uint16_t* _M0L4dataS1009 = _M0L4selfS127->$0;
      moonbit_incref_cycle_free(_M0L4dataS1009);
      return _M0L4dataS1009;
    } else {
      uint16_t* _M0L4dataS1010 = _M0L4selfS127->$0;
      int32_t _M0L3lenS1011 = _M0L4selfS127->$1;
      int32_t _M0L6_2atmpS1012;
      int32_t _M0L3lenS1013;
      uint16_t* _M0L4dataS128;
      moonbit_incref_cycle_free(_M0L4dataS1010);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1012 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1013 = _M0L4selfS127->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS128
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1010, _M0L3lenS1011, _M0L6_2atmpS1012, _M0L3lenS1013, 0, 0);
      return _M0L4dataS128;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS124,
  int32_t _M0L13allocate__lenS120,
  int32_t _M0L4initS125,
  int32_t _M0L3lenS121,
  int32_t _M0L11src__offsetS122,
  int32_t _M0L11dst__offsetS123
) {
  int32_t _if__result_2022;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS120 >= 0) {
    if (_M0L3lenS121 >= 0) {
      if (_M0L11src__offsetS122 >= 0) {
        if (_M0L11dst__offsetS123 >= 0) {
          int32_t _M0L6_2atmpS1001 = _M0L11src__offsetS122 + _M0L3lenS121;
          int32_t _M0L6_2atmpS1002 = Moonbit_array_length(_M0L3srcS124);
          if (_M0L6_2atmpS1001 <= _M0L6_2atmpS1002) {
            int32_t _M0L6_2atmpS1000 = _M0L11dst__offsetS123 + _M0L3lenS121;
            _if__result_2022 = _M0L6_2atmpS1000 <= _M0L13allocate__lenS120;
          } else {
            _if__result_2022 = 0;
          }
        } else {
          _if__result_2022 = 0;
        }
      } else {
        _if__result_2022 = 0;
      }
    } else {
      _if__result_2022 = 0;
    }
  } else {
    _if__result_2022 = 0;
  }
  if (_if__result_2022) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS124, _M0L13allocate__lenS120, _M0L4initS125, _M0L11src__offsetS122, _M0L11dst__offsetS123, _M0L3lenS121);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS126;
    int32_t _M0L6_2atmpS1004;
    moonbit_string_t _M0L6_2atmpS1003;
    uint16_t* _result_2023;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS126
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L13allocate__lenS120);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L11src__offsetS122);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L11dst__offsetS123);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L3lenS121);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_29.data);
    _M0L6_2atmpS1004 = Moonbit_array_length(_M0L3srcS124);
    moonbit_decref_cycle_free(_M0L3srcS124);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L6_2atmpS1004);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1003
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS126);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS126);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2023 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1003);
    moonbit_decref_cycle_free(_M0L6_2atmpS1003);
    return _result_2023;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS117,
  int32_t _M0L13allocate__lenS114,
  int32_t _M0L4initS115,
  int32_t _M0L11src__offsetS118,
  int32_t _M0L11dst__offsetS116,
  int32_t _M0L9blit__lenS119
) {
  uint16_t* _M0L3dstS113;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS113
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS114, _M0L4initS115);
  moonbit_incref_cycle_free(_M0L3dstS113);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS113, _M0L11dst__offsetS116, _M0L3srcS117, _M0L11src__offsetS118, _M0L9blit__lenS119, sizeof(uint16_t));
  return _M0L3dstS113;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS111
) {
  int32_t _M0L7initialS110;
  uint16_t* _M0L4dataS112;
  struct _M0TPB13StringBuilder* _block_2024;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS111 < 1) {
    _M0L7initialS110 = 1;
  } else {
    int32_t _M0L6_2atmpS999 = _M0L10size__hintS111 + 1;
    _M0L7initialS110 = _M0L6_2atmpS999 / 2;
  }
  _M0L4dataS112 = (uint16_t*)moonbit_make_string(_M0L7initialS110, 0);
  _block_2024
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2024)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 49, 0);
  _block_2024->$0 = _M0L4dataS112;
  _block_2024->$1 = 0;
  return _block_2024;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS109) {
  int32_t _M0L6_2atmpS998;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS998 = (int32_t)_M0L4selfS109;
  return _M0L6_2atmpS998;
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS95,
  int32_t _M0L13allocate__lenS91,
  int32_t _M0L3lenS92,
  int32_t _M0L11src__offsetS93,
  int32_t _M0L11dst__offsetS94
) {
  int32_t _if__result_2025;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS91 >= 0) {
    if (_M0L3lenS92 >= 0) {
      if (_M0L11src__offsetS93 >= 0) {
        if (_M0L11dst__offsetS94 >= 0) {
          int32_t _M0L6_2atmpS984 = _M0L11src__offsetS93 + _M0L3lenS92;
          int32_t _M0L6_2atmpS985;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS985 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS95);
          if (_M0L6_2atmpS984 <= _M0L6_2atmpS985) {
            int32_t _M0L6_2atmpS983 = _M0L11dst__offsetS94 + _M0L3lenS92;
            _if__result_2025 = _M0L6_2atmpS983 <= _M0L13allocate__lenS91;
          } else {
            _if__result_2025 = 0;
          }
        } else {
          _if__result_2025 = 0;
        }
      } else {
        _if__result_2025 = 0;
      }
    } else {
      _if__result_2025 = 0;
    }
  } else {
    _if__result_2025 = 0;
  }
  if (_if__result_2025) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS95, _M0L13allocate__lenS91, _M0L11src__offsetS93, _M0L11dst__offsetS94, _M0L3lenS92);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS96;
    int32_t _M0L6_2atmpS987;
    moonbit_string_t _M0L6_2atmpS986;
    float* _result_2026;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS96
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L13allocate__lenS91);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L11src__offsetS93);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L11dst__offsetS94);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L3lenS92);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS987 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS95);
    moonbit_decref_cycle_free(_M0L3srcS95);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L6_2atmpS987);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS986
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS96);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS96);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2026
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS986);
    moonbit_decref_cycle_free(_M0L6_2atmpS986);
    return _result_2026;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS101,
  int32_t _M0L13allocate__lenS97,
  int32_t _M0L3lenS98,
  int32_t _M0L11src__offsetS99,
  int32_t _M0L11dst__offsetS100
) {
  int32_t _if__result_2027;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS97 >= 0) {
    if (_M0L3lenS98 >= 0) {
      if (_M0L11src__offsetS99 >= 0) {
        if (_M0L11dst__offsetS100 >= 0) {
          int32_t _M0L6_2atmpS989 = _M0L11src__offsetS99 + _M0L3lenS98;
          int32_t _M0L6_2atmpS990;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS990
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS101);
          if (_M0L6_2atmpS989 <= _M0L6_2atmpS990) {
            int32_t _M0L6_2atmpS988 = _M0L11dst__offsetS100 + _M0L3lenS98;
            _if__result_2027 = _M0L6_2atmpS988 <= _M0L13allocate__lenS97;
          } else {
            _if__result_2027 = 0;
          }
        } else {
          _if__result_2027 = 0;
        }
      } else {
        _if__result_2027 = 0;
      }
    } else {
      _if__result_2027 = 0;
    }
  } else {
    _if__result_2027 = 0;
  }
  if (_if__result_2027) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS97, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS101, _M0L11src__offsetS99, _M0L11dst__offsetS100, _M0L3lenS98);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS102;
    int32_t _M0L6_2atmpS992;
    moonbit_string_t _M0L6_2atmpS991;
    moonbit_string_t* _result_2028;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS102
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L13allocate__lenS97);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L11src__offsetS99);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L11dst__offsetS100);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L3lenS98);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS992 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS101);
    moonbit_decref_cycle_free(_M0L3srcS101);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L6_2atmpS992);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS991
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS102);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS102);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2028
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS991);
    moonbit_decref_cycle_free(_M0L6_2atmpS991);
    return _result_2028;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS107,
  int32_t _M0L13allocate__lenS103,
  int32_t _M0L3lenS104,
  int32_t _M0L11src__offsetS105,
  int32_t _M0L11dst__offsetS106
) {
  int32_t _if__result_2029;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS103 >= 0) {
    if (_M0L3lenS104 >= 0) {
      if (_M0L11src__offsetS105 >= 0) {
        if (_M0L11dst__offsetS106 >= 0) {
          int32_t _M0L6_2atmpS994 = _M0L11src__offsetS105 + _M0L3lenS104;
          int32_t _M0L6_2atmpS995;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS995
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS107);
          if (_M0L6_2atmpS994 <= _M0L6_2atmpS995) {
            int32_t _M0L6_2atmpS993 = _M0L11dst__offsetS106 + _M0L3lenS104;
            _if__result_2029 = _M0L6_2atmpS993 <= _M0L13allocate__lenS103;
          } else {
            _if__result_2029 = 0;
          }
        } else {
          _if__result_2029 = 0;
        }
      } else {
        _if__result_2029 = 0;
      }
    } else {
      _if__result_2029 = 0;
    }
  } else {
    _if__result_2029 = 0;
  }
  if (_if__result_2029) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS103, 0, _M0L3srcS107, _M0L11src__offsetS105, _M0L11dst__offsetS106, _M0L3lenS104);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS108;
    int32_t _M0L6_2atmpS997;
    moonbit_string_t _M0L6_2atmpS996;
    struct _M0TUsiE** _result_2030;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS108
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L13allocate__lenS103);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L11src__offsetS105);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L11dst__offsetS106);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L3lenS104);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS997 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS107);
    moonbit_decref_cycle_free(_M0L3srcS107);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L6_2atmpS997);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS996
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS108);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS108);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2030
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS996);
    moonbit_decref_cycle_free(_M0L6_2atmpS996);
    return _result_2030;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS86,
  moonbit_string_t _M0L3objS85
) {
  struct _M0TPB6Logger _M0L6_2atmpS980;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS86);
  _M0L6_2atmpS980
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS86
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS85, _M0L6_2atmpS980);
  if (_M0L6_2atmpS980.$1) {
    moonbit_decref(_M0L6_2atmpS980.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS88,
  int32_t _M0L3objS87
) {
  struct _M0TPB6Logger _M0L6_2atmpS981;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS88);
  _M0L6_2atmpS981
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS88
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS87, _M0L6_2atmpS981);
  if (_M0L6_2atmpS981.$1) {
    moonbit_decref(_M0L6_2atmpS981.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS90,
  uint64_t _M0L3objS89
) {
  struct _M0TPB6Logger _M0L6_2atmpS982;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS90);
  _M0L6_2atmpS982
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS90
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS89, _M0L6_2atmpS982);
  if (_M0L6_2atmpS982.$1) {
    moonbit_decref(_M0L6_2atmpS982.$1);
  }
  return 0;
}

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS70,
  int32_t _M0L13allocate__lenS68,
  int32_t _M0L11src__offsetS71,
  int32_t _M0L11dst__offsetS69,
  int32_t _M0L9blit__lenS72
) {
  float* _M0L3dstS67;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS67 = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS68);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS67, _M0L11dst__offsetS69, _M0L3srcS70, _M0L11src__offsetS71, _M0L9blit__lenS72);
  moonbit_decref_cycle_free(_M0L3srcS70);
  return _M0L3dstS67;
}

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t* _M0L3srcS76,
  int32_t _M0L13allocate__lenS74,
  int32_t _M0L11src__offsetS77,
  int32_t _M0L11dst__offsetS75,
  int32_t _M0L9blit__lenS78
) {
  moonbit_string_t* _M0L3dstS73;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS73
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L13allocate__lenS74, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGsE(_M0L3dstS73, _M0L11dst__offsetS75, _M0L3srcS76, _M0L11src__offsetS77, _M0L9blit__lenS78);
  moonbit_decref_cycle_free(_M0L3srcS76);
  return _M0L3dstS73;
}

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE** _M0L3srcS82,
  int32_t _M0L13allocate__lenS80,
  int32_t _M0L11src__offsetS83,
  int32_t _M0L11dst__offsetS81,
  int32_t _M0L9blit__lenS84
) {
  struct _M0TUsiE** _M0L3dstS79;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS79
  = (struct _M0TUsiE**)moonbit_make_ref_array(_M0L13allocate__lenS80, 0);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGUsiEE(_M0L3dstS79, _M0L11dst__offsetS81, _M0L3srcS82, _M0L11src__offsetS83, _M0L9blit__lenS84);
  moonbit_decref_cycle_free(_M0L3srcS82);
  return _M0L3dstS79;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS52,
  int32_t _M0L11dst__offsetS53,
  float* _M0L3srcS54,
  int32_t _M0L11src__offsetS55,
  int32_t _M0L3lenS56
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS54);
  moonbit_incref_cycle_free(_M0L3dstS52);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS52, _M0L11dst__offsetS53, _M0L3srcS54, _M0L11src__offsetS55, _M0L3lenS56, sizeof(float));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS57,
  int32_t _M0L11dst__offsetS58,
  moonbit_string_t* _M0L3srcS59,
  int32_t _M0L11src__offsetS60,
  int32_t _M0L3lenS61
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS59);
  moonbit_incref_cycle_free(_M0L3dstS57);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS57, _M0L11dst__offsetS58, _M0L3srcS59, _M0L11src__offsetS60, _M0L3lenS61);
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS62,
  int32_t _M0L11dst__offsetS63,
  struct _M0TUsiE** _M0L3srcS64,
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t* _M0L3dstS16,
  int32_t _M0L11dst__offsetS18,
  uint16_t* _M0L3srcS17,
  int32_t _M0L11src__offsetS19,
  int32_t _M0L3lenS21
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS16 == _M0L3srcS17 && _M0L11dst__offsetS18 < _M0L11src__offsetS19
  ) {
    int32_t _M0L1iS20 = 0;
    while (1) {
      if (_M0L1iS20 < _M0L3lenS21) {
        int32_t _M0L6_2atmpS944 = _M0L11dst__offsetS18 + _M0L1iS20;
        int32_t _M0L6_2atmpS946 = _M0L11src__offsetS19 + _M0L1iS20;
        int32_t _M0L6_2atmpS945;
        int32_t _M0L6_2atmpS947;
        if (
          _M0L6_2atmpS946 < 0
          || _M0L6_2atmpS946 >= Moonbit_array_length(_M0L3srcS17)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS945 = (int32_t)_M0L3srcS17[_M0L6_2atmpS946];
        if (
          _M0L6_2atmpS944 < 0
          || _M0L6_2atmpS944 >= Moonbit_array_length(_M0L3dstS16)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS16[_M0L6_2atmpS944] = _M0L6_2atmpS945;
        _M0L6_2atmpS947 = _M0L1iS20 + 1;
        _M0L1iS20 = _M0L6_2atmpS947;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS17);
        moonbit_decref_cycle_free(_M0L3dstS16);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS952 = _M0L3lenS21 - 1;
    int32_t _M0L1iS23 = _M0L6_2atmpS952;
    while (1) {
      if (_M0L1iS23 >= 0) {
        int32_t _M0L6_2atmpS948 = _M0L11dst__offsetS18 + _M0L1iS23;
        int32_t _M0L6_2atmpS950 = _M0L11src__offsetS19 + _M0L1iS23;
        int32_t _M0L6_2atmpS949;
        int32_t _M0L6_2atmpS951;
        if (
          _M0L6_2atmpS950 < 0
          || _M0L6_2atmpS950 >= Moonbit_array_length(_M0L3srcS17)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS949 = (int32_t)_M0L3srcS17[_M0L6_2atmpS950];
        if (
          _M0L6_2atmpS948 < 0
          || _M0L6_2atmpS948 >= Moonbit_array_length(_M0L3dstS16)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS16[_M0L6_2atmpS948] = _M0L6_2atmpS949;
        _M0L6_2atmpS951 = _M0L1iS23 - 1;
        _M0L1iS23 = _M0L6_2atmpS951;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS17);
        moonbit_decref_cycle_free(_M0L3dstS16);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS25,
  int32_t _M0L11dst__offsetS27,
  float* _M0L3srcS26,
  int32_t _M0L11src__offsetS28,
  int32_t _M0L3lenS30
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS25 == _M0L3srcS26 && _M0L11dst__offsetS27 < _M0L11src__offsetS28
  ) {
    int32_t _M0L1iS29 = 0;
    while (1) {
      if (_M0L1iS29 < _M0L3lenS30) {
        int32_t _M0L6_2atmpS953 = _M0L11dst__offsetS27 + _M0L1iS29;
        int32_t _M0L6_2atmpS955 = _M0L11src__offsetS28 + _M0L1iS29;
        float _M0L6_2atmpS954;
        int32_t _M0L6_2atmpS956;
        if (
          _M0L6_2atmpS955 < 0
          || _M0L6_2atmpS955 >= Moonbit_array_length(_M0L3srcS26)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS954 = (float)_M0L3srcS26[_M0L6_2atmpS955];
        if (
          _M0L6_2atmpS953 < 0
          || _M0L6_2atmpS953 >= Moonbit_array_length(_M0L3dstS25)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS25[_M0L6_2atmpS953] = _M0L6_2atmpS954;
        _M0L6_2atmpS956 = _M0L1iS29 + 1;
        _M0L1iS29 = _M0L6_2atmpS956;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS26);
        moonbit_decref_cycle_free(_M0L3dstS25);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS961 = _M0L3lenS30 - 1;
    int32_t _M0L1iS32 = _M0L6_2atmpS961;
    while (1) {
      if (_M0L1iS32 >= 0) {
        int32_t _M0L6_2atmpS957 = _M0L11dst__offsetS27 + _M0L1iS32;
        int32_t _M0L6_2atmpS959 = _M0L11src__offsetS28 + _M0L1iS32;
        float _M0L6_2atmpS958;
        int32_t _M0L6_2atmpS960;
        if (
          _M0L6_2atmpS959 < 0
          || _M0L6_2atmpS959 >= Moonbit_array_length(_M0L3srcS26)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS958 = (float)_M0L3srcS26[_M0L6_2atmpS959];
        if (
          _M0L6_2atmpS957 < 0
          || _M0L6_2atmpS957 >= Moonbit_array_length(_M0L3dstS25)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS25[_M0L6_2atmpS957] = _M0L6_2atmpS958;
        _M0L6_2atmpS960 = _M0L1iS32 - 1;
        _M0L1iS32 = _M0L6_2atmpS960;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS26);
        moonbit_decref_cycle_free(_M0L3dstS25);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS34,
  int32_t _M0L11dst__offsetS36,
  moonbit_string_t* _M0L3srcS35,
  int32_t _M0L11src__offsetS37,
  int32_t _M0L3lenS39
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS34 == _M0L3srcS35 && _M0L11dst__offsetS36 < _M0L11src__offsetS37
  ) {
    int32_t _M0L1iS38 = 0;
    while (1) {
      if (_M0L1iS38 < _M0L3lenS39) {
        int32_t _M0L6_2atmpS962 = _M0L11dst__offsetS36 + _M0L1iS38;
        int32_t _M0L6_2atmpS964 = _M0L11src__offsetS37 + _M0L1iS38;
        moonbit_string_t _M0L6_2atmpS963;
        moonbit_string_t _M0L6_2aoldS1914;
        int32_t _M0L6_2atmpS965;
        if (
          _M0L6_2atmpS964 < 0
          || _M0L6_2atmpS964 >= Moonbit_array_length(_M0L3srcS35)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS963 = (moonbit_string_t)_M0L3srcS35[_M0L6_2atmpS964];
        if (
          _M0L6_2atmpS962 < 0
          || _M0L6_2atmpS962 >= Moonbit_array_length(_M0L3dstS34)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1914 = (moonbit_string_t)_M0L3dstS34[_M0L6_2atmpS962];
        moonbit_incref_cycle_free(_M0L6_2atmpS963);
        moonbit_decref_cycle_free(_M0L6_2aoldS1914);
        _M0L3dstS34[_M0L6_2atmpS962] = _M0L6_2atmpS963;
        _M0L6_2atmpS965 = _M0L1iS38 + 1;
        _M0L1iS38 = _M0L6_2atmpS965;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS35);
        moonbit_decref_cycle_free(_M0L3dstS34);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS970 = _M0L3lenS39 - 1;
    int32_t _M0L1iS41 = _M0L6_2atmpS970;
    while (1) {
      if (_M0L1iS41 >= 0) {
        int32_t _M0L6_2atmpS966 = _M0L11dst__offsetS36 + _M0L1iS41;
        int32_t _M0L6_2atmpS968 = _M0L11src__offsetS37 + _M0L1iS41;
        moonbit_string_t _M0L6_2atmpS967;
        moonbit_string_t _M0L6_2aoldS1915;
        int32_t _M0L6_2atmpS969;
        if (
          _M0L6_2atmpS968 < 0
          || _M0L6_2atmpS968 >= Moonbit_array_length(_M0L3srcS35)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS967 = (moonbit_string_t)_M0L3srcS35[_M0L6_2atmpS968];
        if (
          _M0L6_2atmpS966 < 0
          || _M0L6_2atmpS966 >= Moonbit_array_length(_M0L3dstS34)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1915 = (moonbit_string_t)_M0L3dstS34[_M0L6_2atmpS966];
        moonbit_incref_cycle_free(_M0L6_2atmpS967);
        moonbit_decref_cycle_free(_M0L6_2aoldS1915);
        _M0L3dstS34[_M0L6_2atmpS966] = _M0L6_2atmpS967;
        _M0L6_2atmpS969 = _M0L1iS41 - 1;
        _M0L1iS41 = _M0L6_2atmpS969;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS35);
        moonbit_decref_cycle_free(_M0L3dstS34);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS43,
  int32_t _M0L11dst__offsetS45,
  struct _M0TUsiE** _M0L3srcS44,
  int32_t _M0L11src__offsetS46,
  int32_t _M0L3lenS48
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS43 == _M0L3srcS44 && _M0L11dst__offsetS45 < _M0L11src__offsetS46
  ) {
    int32_t _M0L1iS47 = 0;
    while (1) {
      if (_M0L1iS47 < _M0L3lenS48) {
        int32_t _M0L6_2atmpS971 = _M0L11dst__offsetS45 + _M0L1iS47;
        int32_t _M0L6_2atmpS973 = _M0L11src__offsetS46 + _M0L1iS47;
        struct _M0TUsiE* _M0L6_2atmpS972;
        struct _M0TUsiE* _M0L6_2aoldS1916;
        int32_t _M0L6_2atmpS974;
        if (
          _M0L6_2atmpS973 < 0
          || _M0L6_2atmpS973 >= Moonbit_array_length(_M0L3srcS44)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS972 = (struct _M0TUsiE*)_M0L3srcS44[_M0L6_2atmpS973];
        if (
          _M0L6_2atmpS971 < 0
          || _M0L6_2atmpS971 >= Moonbit_array_length(_M0L3dstS43)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1916 = (struct _M0TUsiE*)_M0L3dstS43[_M0L6_2atmpS971];
        if (_M0L6_2atmpS972) {
          moonbit_incref_cycle_free(_M0L6_2atmpS972);
        }
        if (_M0L6_2aoldS1916) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1916);
        }
        _M0L3dstS43[_M0L6_2atmpS971] = _M0L6_2atmpS972;
        _M0L6_2atmpS974 = _M0L1iS47 + 1;
        _M0L1iS47 = _M0L6_2atmpS974;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS44);
        moonbit_decref_cycle_free(_M0L3dstS43);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS979 = _M0L3lenS48 - 1;
    int32_t _M0L1iS50 = _M0L6_2atmpS979;
    while (1) {
      if (_M0L1iS50 >= 0) {
        int32_t _M0L6_2atmpS975 = _M0L11dst__offsetS45 + _M0L1iS50;
        int32_t _M0L6_2atmpS977 = _M0L11src__offsetS46 + _M0L1iS50;
        struct _M0TUsiE* _M0L6_2atmpS976;
        struct _M0TUsiE* _M0L6_2aoldS1917;
        int32_t _M0L6_2atmpS978;
        if (
          _M0L6_2atmpS977 < 0
          || _M0L6_2atmpS977 >= Moonbit_array_length(_M0L3srcS44)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS976 = (struct _M0TUsiE*)_M0L3srcS44[_M0L6_2atmpS977];
        if (
          _M0L6_2atmpS975 < 0
          || _M0L6_2atmpS975 >= Moonbit_array_length(_M0L3dstS43)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1917 = (struct _M0TUsiE*)_M0L3dstS43[_M0L6_2atmpS975];
        if (_M0L6_2atmpS976) {
          moonbit_incref_cycle_free(_M0L6_2atmpS976);
        }
        if (_M0L6_2aoldS1917) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1917);
        }
        _M0L3dstS43[_M0L6_2atmpS975] = _M0L6_2atmpS976;
        _M0L6_2atmpS978 = _M0L1iS50 - 1;
        _M0L1iS50 = _M0L6_2atmpS978;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS44);
        moonbit_decref_cycle_free(_M0L3dstS43);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPB18UninitializedArray6lengthGfE(float* _M0L4selfS13) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS13);
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
  _M0L10_2ax__6388S12.$0->$method_0(_M0L10_2ax__6388S12.$1, (moonbit_string_t)moonbit_string_literal_30.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S12, _M0L15_2a_2aarg__6389S11);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S12.$0->$method_0(_M0L10_2ax__6388S12.$1, (moonbit_string_t)moonbit_string_literal_31.data);
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

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(
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

int32_t _M0FPC15abort5abortGiE(moonbit_string_t _M0L3msgS6) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS6);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS916) {
  switch (Moonbit_object_tag(_M0L4_2aeS916)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_32.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS916);
      break;
    }
    
    case 1: {
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
  void* _M0L11_2aobj__ptrS939,
  struct _M0TPB4Show _M0L8_2aparamS938
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS937 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS939;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS937, _M0L8_2aparamS938);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS936,
  struct _M0TPB4Show _M0L8_2aparamS935
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS934 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS936;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS934, _M0L8_2aparamS935);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS933,
  int32_t _M0L8_2aparamS932
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS931 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS933;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS931, _M0L8_2aparamS932);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS930,
  struct _M0TPC16string10StringView _M0L8_2aparamS929
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS928 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS930;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS928, _M0L8_2aparamS929);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS927,
  moonbit_string_t _M0L8_2aparamS924,
  int32_t _M0L8_2aparamS925,
  int32_t _M0L8_2aparamS926
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS923 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS927;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS923, _M0L8_2aparamS924, _M0L8_2aparamS925, _M0L8_2aparamS926);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS922,
  moonbit_string_t _M0L8_2aparamS921
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS920 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS922;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS920, _M0L8_2aparamS921);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS943;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS909;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS910;
  int32_t _M0L7_2abindS911;
  struct _M0TUsiE** _M0L7_2abindS912;
  int32_t _M0L6_2acntS1922;
  int32_t _M0L2__S913;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS943
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS909
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS909)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 52, 0);
  _M0L12async__testsS909->$0 = _M0L6_2atmpS943;
  _M0L12async__testsS909->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS910
  = _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS911 = _M0L7_2abindS910->$1;
  _M0L7_2abindS912 = _M0L7_2abindS910->$0;
  _M0L6_2acntS1922
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS910));
  if (_M0L6_2acntS1922 > 1) {
    int32_t _M0L11_2anew__cntS1923 = _M0L6_2acntS1922 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS910), _M0L11_2anew__cntS1923);
    moonbit_incref_cycle_free(_M0L7_2abindS912);
  } else if (_M0L6_2acntS1922 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS910);
  }
  _M0L2__S913 = 0;
  while (1) {
    if (_M0L2__S913 < _M0L7_2abindS911) {
      struct _M0TUsiE* _M0L3argS914 =
        (struct _M0TUsiE*)_M0L7_2abindS912[_M0L2__S913];
      moonbit_string_t _M0L6_2atmpS940 = _M0L3argS914->$0;
      int32_t _M0L6_2atmpS941 = _M0L3argS914->$1;
      int32_t _M0L6_2atmpS942;
      moonbit_incref_cycle_free(_M0L6_2atmpS940);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS909, _M0L6_2atmpS940, _M0L6_2atmpS941);
      moonbit_decref_cycle_free(_M0L6_2atmpS940);
      _M0L6_2atmpS942 = _M0L2__S913 + 1;
      _M0L2__S913 = _M0L6_2atmpS942;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS912);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_debug\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples33izhikevich__debug__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS909);
  moonbit_decref_cycle_free(_M0L12async__testsS909);
  moonbit_flush_cycles();
  return 0;
}