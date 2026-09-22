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

struct _M0TP26RiantR8snn__mbt11IZPostSpike;

struct _M0DTPC15error5Error138RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TWRPC15error5ErrorEs;

struct _M0TPB4Show;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TP26RiantR8snn__mbt11IZParameter;

struct _M0DTPC15error5Error140RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGbE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c904;

struct _M0TPB5ArrayGsE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TP26RiantR8snn__mbt2IZ;

struct _M0TWEu;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0R141_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c909;

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

struct _M0TP26RiantR8snn__mbt11IZPostSpike {
  int32_t $0;
  
};

struct _M0DTPC15error5Error138RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

struct _M0DTPC15error5Error140RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c904 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0R141_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c909 {
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS916(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS909(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS904(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS881(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S874(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

int32_t _M0FP26RiantR8snn__mbt25step__iz__with__postspike(
  struct _M0TP26RiantR8snn__mbt2IZ*,
  float
);

struct _M0TP26RiantR8snn__mbt2IZ* _M0MP26RiantR8snn__mbt2IZ21init__with__postspike(
  int32_t,
  struct _M0TP26RiantR8snn__mbt11IZParameter*,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt11IZPostSpike*
);

struct _M0TP26RiantR8snn__mbt11IZPostSpike* _M0MP26RiantR8snn__mbt11IZPostSpike6custom(
  int32_t
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

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t
);

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t);

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t);

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

int32_t _M0MPC15float5Float7to__int(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

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

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t,
  struct _M0TPB6Logger
);

int32_t _M0IP016_24default__implPB4Show6outputGfE(
  float,
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
} const moonbit_string_literal_33 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[125]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 124, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 105, 122, 104, 105, 107, 101, 118, 
    105, 99, 104, 95, 112, 111, 115, 116, 115, 112, 105, 107, 101, 95, 
    98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 
    111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 
    101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 
    114, 111, 114, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 
    116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 
    108, 74, 115, 69, 114, 114, 111, 114, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[127]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 126, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 105, 122, 104, 105, 107, 101, 118, 
    105, 99, 104, 95, 112, 111, 115, 116, 115, 112, 105, 107, 101, 95, 
    98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 
    111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 
    101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 
    84, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 
    115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 
    97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 0
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
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS916$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS916
  };

uint32_t const moonbit_layout_table_data[55] =
  {
    sizeof(struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c904)
    / 4, 1,
    offsetof(struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c904, $1)
    / 4
    * 2,
    sizeof(struct _M0R141_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c909)
    / 4, 1,
    offsetof(struct _M0R141_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c909, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error140RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error140RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2024
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS937,
  moonbit_string_t _M0L8filenameS906,
  int32_t _M0L5indexS908
) {
  struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c904* _closure_2048;
  struct _M0TWEu* _M0L13handle__startS904;
  struct _M0R141_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c909* _closure_2049;
  struct _M0TWssbEu* _M0L14handle__resultS909;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS916;
  void* _M0L11_2atry__errS931;
  struct moonbit_result_0 _tmp_2051;
  int32_t _handle__error__result_2052;
  int32_t _M0L6_2atmpS2012;
  void* _M0L3errS932;
  moonbit_string_t _M0L4nameS934;
  struct _M0DTPC15error5Error140RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS935;
  moonbit_string_t _M0L7_2anameS936;
  int32_t _M0L6_2acntS2042;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS906);
  _closure_2048
  = (struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c904*)moonbit_malloc(sizeof(struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c904));
  Moonbit_object_header(_closure_2048)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2048->code
  = &_M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS904;
  _closure_2048->$0 = _M0L5indexS908;
  _closure_2048->$1 = _M0L8filenameS906;
  _M0L13handle__startS904 = (struct _M0TWEu*)_closure_2048;
  moonbit_incref_cycle_free(_M0L8filenameS906);
  _closure_2049
  = (struct _M0R141_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c909*)moonbit_malloc(sizeof(struct _M0R141_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c909));
  Moonbit_object_header(_closure_2049)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2049->code
  = &_M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS909;
  _closure_2049->$0 = _M0L5indexS908;
  _closure_2049->$1 = _M0L8filenameS906;
  _M0L14handle__resultS909 = (struct _M0TWssbEu*)_closure_2049;
  _M0L17error__to__stringS916
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS916$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2051
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS937, _M0L8filenameS906, _M0L5indexS908, _M0L13handle__startS904, _M0L14handle__resultS909, _M0L17error__to__stringS916);
  if (_tmp_2051.tag) {
    int32_t const _M0L5_2aokS2021 = _tmp_2051.data.ok;
    _handle__error__result_2052 = _M0L5_2aokS2021;
  } else {
    void* const _M0L6_2aerrS2022 = _tmp_2051.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS916);
    moonbit_decref_cycle_free(_M0L13handle__startS904);
    _M0L11_2atry__errS931 = _M0L6_2aerrS2022;
    goto join_930;
  }
  if (_handle__error__result_2052) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS916);
    moonbit_decref_cycle_free(_M0L13handle__startS904);
    _M0L6_2atmpS2012 = 1;
  } else {
    struct moonbit_result_0 _tmp_2053;
    int32_t _handle__error__result_2054;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2053
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS937, _M0L8filenameS906, _M0L5indexS908, _M0L13handle__startS904, _M0L14handle__resultS909, _M0L17error__to__stringS916);
    if (_tmp_2053.tag) {
      int32_t const _M0L5_2aokS2019 = _tmp_2053.data.ok;
      _handle__error__result_2054 = _M0L5_2aokS2019;
    } else {
      void* const _M0L6_2aerrS2020 = _tmp_2053.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS916);
      moonbit_decref_cycle_free(_M0L13handle__startS904);
      _M0L11_2atry__errS931 = _M0L6_2aerrS2020;
      goto join_930;
    }
    if (_handle__error__result_2054) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS916);
      moonbit_decref_cycle_free(_M0L13handle__startS904);
      _M0L6_2atmpS2012 = 1;
    } else {
      struct moonbit_result_0 _tmp_2055;
      int32_t _handle__error__result_2056;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2055
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS937, _M0L8filenameS906, _M0L5indexS908, _M0L13handle__startS904, _M0L14handle__resultS909, _M0L17error__to__stringS916);
      if (_tmp_2055.tag) {
        int32_t const _M0L5_2aokS2017 = _tmp_2055.data.ok;
        _handle__error__result_2056 = _M0L5_2aokS2017;
      } else {
        void* const _M0L6_2aerrS2018 = _tmp_2055.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS916);
        moonbit_decref_cycle_free(_M0L13handle__startS904);
        _M0L11_2atry__errS931 = _M0L6_2aerrS2018;
        goto join_930;
      }
      if (_handle__error__result_2056) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS916);
        moonbit_decref_cycle_free(_M0L13handle__startS904);
        _M0L6_2atmpS2012 = 1;
      } else {
        struct moonbit_result_0 _tmp_2057;
        int32_t _handle__error__result_2058;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2057
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS937, _M0L8filenameS906, _M0L5indexS908, _M0L13handle__startS904, _M0L14handle__resultS909, _M0L17error__to__stringS916);
        if (_tmp_2057.tag) {
          int32_t const _M0L5_2aokS2015 = _tmp_2057.data.ok;
          _handle__error__result_2058 = _M0L5_2aokS2015;
        } else {
          void* const _M0L6_2aerrS2016 = _tmp_2057.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS916);
          moonbit_decref_cycle_free(_M0L13handle__startS904);
          _M0L11_2atry__errS931 = _M0L6_2aerrS2016;
          goto join_930;
        }
        if (_handle__error__result_2058) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS916);
          moonbit_decref_cycle_free(_M0L13handle__startS904);
          _M0L6_2atmpS2012 = 1;
        } else {
          struct moonbit_result_0 _tmp_2059;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2059
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS937, _M0L8filenameS906, _M0L5indexS908, _M0L13handle__startS904, _M0L14handle__resultS909, _M0L17error__to__stringS916);
          moonbit_decref_cycle_free(_M0L13handle__startS904);
          moonbit_decref_cycle_free(_M0L17error__to__stringS916);
          if (_tmp_2059.tag) {
            int32_t const _M0L5_2aokS2013 = _tmp_2059.data.ok;
            _M0L6_2atmpS2012 = _M0L5_2aokS2013;
          } else {
            void* const _M0L6_2aerrS2014 = _tmp_2059.data.err;
            _M0L11_2atry__errS931 = _M0L6_2aerrS2014;
            goto join_930;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2012) {
    void* _M0L140RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2023 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error140RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L140RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2023)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error140RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L140RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2023)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS931
    = _M0L140RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2023;
    goto join_930;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS909);
  }
  goto joinlet_2050;
  join_930:;
  _M0L3errS932 = _M0L11_2atry__errS931;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS935
  = (struct _M0DTPC15error5Error140RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS932;
  _M0L7_2anameS936 = _M0L36_2aMoonBitTestDriverInternalSkipTestS935->$0;
  _M0L6_2acntS2042
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS935));
  if (_M0L6_2acntS2042 > 1) {
    int32_t _M0L11_2anew__cntS2043 = _M0L6_2acntS2042 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS935), _M0L11_2anew__cntS2043);
    moonbit_incref_cycle_free(_M0L7_2anameS936);
  } else if (_M0L6_2acntS2042 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS935);
  }
  _M0L4nameS934 = _M0L7_2anameS936;
  goto join_933;
  goto joinlet_2060;
  join_933:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS909(_M0L14handle__resultS909, _M0L4nameS934, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS909);
  moonbit_decref_cycle_free(_M0L4nameS934);
  joinlet_2060:;
  joinlet_2050:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS916(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2011,
  void* _M0L3errS917
) {
  void* _M0L1eS919;
  moonbit_string_t _M0L1eS921;
  moonbit_string_t _result_2063;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS917)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS922 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS917;
      moonbit_string_t _M0L4_2aeS923 = _M0L10_2aFailureS922->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS923);
      _M0L1eS921 = _M0L4_2aeS923;
      goto join_920;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS924 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS917;
      moonbit_string_t _M0L4_2aeS925 = _M0L15_2aInspectErrorS924->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS925);
      _M0L1eS921 = _M0L4_2aeS925;
      goto join_920;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS926 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS917;
      moonbit_string_t _M0L4_2aeS927 = _M0L16_2aSnapshotErrorS926->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS927);
      _M0L1eS921 = _M0L4_2aeS927;
      goto join_920;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error138RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS928 =
        (struct _M0DTPC15error5Error138RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS917;
      moonbit_string_t _M0L4_2aeS929 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS928->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS929);
      _M0L1eS921 = _M0L4_2aeS929;
      goto join_920;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS917);
      _M0L1eS919 = _M0L3errS917;
      goto join_918;
      break;
    }
  }
  join_920:;
  return _M0L1eS921;
  join_918:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _result_2063 = _M0FP15Error10to__string(_M0L1eS919);
  moonbit_decref_cycle_free(_M0L1eS919);
  return _result_2063;
}

int32_t _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS909(
  struct _M0TWssbEu* _M0L6_2aenvS2008,
  moonbit_string_t _M0L10__testnameS910,
  moonbit_string_t _M0L7messageS911,
  int32_t _M0L7skippedS912
) {
  struct _M0R141_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c909* _M0L14_2acasted__envS2009;
  moonbit_string_t _M0L8filenameS906;
  int32_t _M0L5indexS908;
  moonbit_string_t _M0L10file__nameS913;
  moonbit_string_t _M0L7messageS914;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS915;
  moonbit_string_t _M0L6_2atmpS2010;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2009
  = (struct _M0R141_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c909*)_M0L6_2aenvS2008;
  _M0L8filenameS906 = _M0L14_2acasted__envS2009->$1;
  _M0L5indexS908 = _M0L14_2acasted__envS2009->$0;
  if (!_M0L7skippedS912 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS913
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS906, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS914
  = _M0MPC16string6String14escape_2einner(_M0L7messageS911, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS915
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS915, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS915, _M0L10file__nameS913);
  moonbit_decref_cycle_free(_M0L10file__nameS913);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS915, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS915, _M0L5indexS908);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS915, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS915, _M0L7messageS914);
  moonbit_decref_cycle_free(_M0L7messageS914);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS915, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2010
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS915);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS915);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2010);
  moonbit_decref_cycle_free(_M0L6_2atmpS2010);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS904(
  struct _M0TWEu* _M0L6_2aenvS2005
) {
  struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c904* _M0L14_2acasted__envS2006;
  moonbit_string_t _M0L8filenameS906;
  int32_t _M0L5indexS908;
  moonbit_string_t _M0L10file__nameS905;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS907;
  moonbit_string_t _M0L6_2atmpS2007;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2006
  = (struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fizhikevich__postspike__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c904*)_M0L6_2aenvS2005;
  _M0L8filenameS906 = _M0L14_2acasted__envS2006->$1;
  _M0L5indexS908 = _M0L14_2acasted__envS2006->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS905
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS906, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS907
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS907, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS907, _M0L10file__nameS905);
  moonbit_decref_cycle_free(_M0L10file__nameS905);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS907, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS907, _M0L5indexS908);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS907, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2007
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS907);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS907);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2007);
  moonbit_decref_cycle_free(_M0L6_2atmpS2007);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S874;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS881;
  struct _M0TUsiE** _M0L6_2atmpS2004;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS888;
  moonbit_string_t* _M0L9cli__argsS889;
  moonbit_string_t _M0L6_2atmpS2003;
  moonbit_string_t _M0L6_2atmpS2002;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS890;
  int32_t _M0L7_2abindS891;
  moonbit_string_t* _M0L7_2abindS892;
  int32_t _M0L6_2acntS2044;
  int32_t _M0L2__S893;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S874 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS881 = 0;
  _M0L6_2atmpS2004 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS888
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS888)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS888->$0 = _M0L6_2atmpS2004;
  _M0L16file__and__indexS888->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS889
  = _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS889)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2003 = (moonbit_string_t)_M0L9cli__argsS889[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2003);
  moonbit_decref_cycle_free(_M0L9cli__argsS889);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2002
  = _M0MP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2003);
  moonbit_decref_cycle_free(_M0L6_2atmpS2003);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS890
  = _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS881(_M0L51moonbit__test__driver__internal__split__mbt__stringS881, _M0L6_2atmpS2002, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2002);
  _M0L7_2abindS891 = _M0L10test__argsS890->$1;
  _M0L7_2abindS892 = _M0L10test__argsS890->$0;
  _M0L6_2acntS2044
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS890));
  if (_M0L6_2acntS2044 > 1) {
    int32_t _M0L11_2anew__cntS2045 = _M0L6_2acntS2044 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS890), _M0L11_2anew__cntS2045);
    moonbit_incref_cycle_free(_M0L7_2abindS892);
  } else if (_M0L6_2acntS2044 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS890);
  }
  _M0L2__S893 = 0;
  while (1) {
    if (_M0L2__S893 < _M0L7_2abindS891) {
      moonbit_string_t _M0L3argS894 =
        (moonbit_string_t)_M0L7_2abindS892[_M0L2__S893];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS895;
      moonbit_string_t _M0L4fileS896;
      moonbit_string_t _M0L5rangeS897;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS898;
      moonbit_string_t _M0L6_2atmpS2000;
      int32_t _M0L5startS899;
      moonbit_string_t _M0L6_2atmpS1999;
      int32_t _M0L3endS900;
      int32_t _M0L1iS901;
      int32_t _M0L6_2atmpS2001;
      moonbit_incref_cycle_free(_M0L3argS894);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS895
      = _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS881(_M0L51moonbit__test__driver__internal__split__mbt__stringS881, _M0L3argS894, 58);
      moonbit_decref_cycle_free(_M0L3argS894);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS896
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS895, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS897
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS895, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS895);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS898
      = _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS881(_M0L51moonbit__test__driver__internal__split__mbt__stringS881, _M0L5rangeS897, 45);
      moonbit_decref_cycle_free(_M0L5rangeS897);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2000
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS898, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS899
      = _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S874(_M0L45moonbit__test__driver__internal__parse__int__S874, _M0L6_2atmpS2000);
      moonbit_decref_cycle_free(_M0L6_2atmpS2000);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1999
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS898, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS898);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS900
      = _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S874(_M0L45moonbit__test__driver__internal__parse__int__S874, _M0L6_2atmpS1999);
      moonbit_decref_cycle_free(_M0L6_2atmpS1999);
      _M0L1iS901 = _M0L5startS899;
      while (1) {
        if (_M0L1iS901 < _M0L3endS900) {
          struct _M0TUsiE* _M0L8_2atupleS1997;
          int32_t _M0L6_2atmpS1998;
          moonbit_incref_cycle_free(_M0L4fileS896);
          _M0L8_2atupleS1997
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS1997)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS1997->$0 = _M0L4fileS896;
          _M0L8_2atupleS1997->$1 = _M0L1iS901;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS888, _M0L8_2atupleS1997);
          _M0L6_2atmpS1998 = _M0L1iS901 + 1;
          _M0L1iS901 = _M0L6_2atmpS1998;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS896);
        }
        break;
      }
      _M0L6_2atmpS2001 = _M0L2__S893 + 1;
      _M0L2__S893 = _M0L6_2atmpS2001;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS892);
    }
    break;
  }
  return _M0L16file__and__indexS888;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS881(
  int32_t _M0L6_2aenvS1978,
  moonbit_string_t _M0L1sS882,
  int32_t _M0L3sepS883
) {
  moonbit_string_t* _M0L6_2atmpS1996;
  struct _M0TPB5ArrayGsE* _M0L3resS884;
  struct _M0TPB8MutLocalGiE* _M0L1iS885;
  struct _M0TPB8MutLocalGiE* _M0L5startS886;
  int32_t _M0L3valS1991;
  int32_t _M0L6_2atmpS1992;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1996 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS884
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS884)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS884->$0 = _M0L6_2atmpS1996;
  _M0L3resS884->$1 = 0;
  _M0L1iS885
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS885)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS885->$0 = 0;
  _M0L5startS886
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS886)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS886->$0 = 0;
  while (1) {
    int32_t _M0L3valS1979 = _M0L1iS885->$0;
    int32_t _M0L6_2atmpS1980 = Moonbit_array_length(_M0L1sS882);
    if (_M0L3valS1979 < _M0L6_2atmpS1980) {
      int32_t _M0L3valS1983 = _M0L1iS885->$0;
      int32_t _M0L6_2atmpS1982;
      int32_t _M0L6_2atmpS1981;
      int32_t _M0L3valS1990;
      int32_t _M0L6_2atmpS1989;
      if (
        _M0L3valS1983 < 0
        || _M0L3valS1983 >= Moonbit_array_length(_M0L1sS882)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1982 = _M0L1sS882[_M0L3valS1983];
      _M0L6_2atmpS1981 = _M0L6_2atmpS1982;
      if (_M0L6_2atmpS1981 == _M0L3sepS883) {
        int32_t _M0L3valS1985 = _M0L5startS886->$0;
        int32_t _M0L3valS1986 = _M0L1iS885->$0;
        moonbit_string_t _M0L6_2atmpS1984;
        int32_t _M0L3valS1988;
        int32_t _M0L6_2atmpS1987;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS1984
        = _M0MPC16string6String17unsafe__substring(_M0L1sS882, _M0L3valS1985, _M0L3valS1986);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS884, _M0L6_2atmpS1984);
        _M0L3valS1988 = _M0L1iS885->$0;
        _M0L6_2atmpS1987 = _M0L3valS1988 + 1;
        _M0L5startS886->$0 = _M0L6_2atmpS1987;
      }
      _M0L3valS1990 = _M0L1iS885->$0;
      _M0L6_2atmpS1989 = _M0L3valS1990 + 1;
      _M0L1iS885->$0 = _M0L6_2atmpS1989;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS885);
    }
    break;
  }
  _M0L3valS1991 = _M0L5startS886->$0;
  _M0L6_2atmpS1992 = Moonbit_array_length(_M0L1sS882);
  if (_M0L3valS1991 < _M0L6_2atmpS1992) {
    int32_t _M0L3valS1994 = _M0L5startS886->$0;
    int32_t _M0L6_2atmpS1995;
    moonbit_string_t _M0L6_2atmpS1993;
    moonbit_decref_cycle_free(_M0L5startS886);
    _M0L6_2atmpS1995 = Moonbit_array_length(_M0L1sS882);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS1993
    = _M0MPC16string6String17unsafe__substring(_M0L1sS882, _M0L3valS1994, _M0L6_2atmpS1995);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS884, _M0L6_2atmpS1993);
  } else {
    moonbit_decref_cycle_free(_M0L5startS886);
  }
  return _M0L3resS884;
}

int32_t _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S874(
  int32_t _M0L6_2aenvS1971,
  moonbit_string_t _M0L1sS875
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS876;
  int32_t _M0L3lenS877;
  int32_t _M0L7_2abindS878;
  int32_t _M0L1iS879;
  int32_t _result_2068;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS876
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS876)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS876->$0 = 0;
  _M0L3lenS877 = Moonbit_array_length(_M0L1sS875);
  _M0L7_2abindS878 = 0;
  _M0L1iS879 = _M0L7_2abindS878;
  while (1) {
    if (_M0L1iS879 < _M0L3lenS877) {
      int32_t _M0L3valS1976 = _M0L3resS876->$0;
      int32_t _M0L6_2atmpS1973 = _M0L3valS1976 * 10;
      int32_t _M0L6_2atmpS1975;
      int32_t _M0L6_2atmpS1974;
      int32_t _M0L6_2atmpS1972;
      int32_t _M0L6_2atmpS1977;
      if (_M0L1iS879 < 0 || _M0L1iS879 >= Moonbit_array_length(_M0L1sS875)) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1975 = _M0L1sS875[_M0L1iS879];
      _M0L6_2atmpS1974 = _M0L6_2atmpS1975 - 48;
      _M0L6_2atmpS1972 = _M0L6_2atmpS1973 + _M0L6_2atmpS1974;
      _M0L3resS876->$0 = _M0L6_2atmpS1972;
      _M0L6_2atmpS1977 = _M0L1iS879 + 1;
      _M0L1iS879 = _M0L6_2atmpS1977;
      continue;
    }
    break;
  }
  _result_2068 = _M0L3resS876->$0;
  moonbit_decref_cycle_free(_M0L3resS876);
  return _result_2068;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS873
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS873);
  return _M0L4selfS873;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S843,
  moonbit_string_t _M0L12_2adiscard__S844,
  int32_t _M0L12_2adiscard__S845,
  struct _M0TWEu* _M0L12_2adiscard__S846,
  struct _M0TWssbEu* _M0L12_2adiscard__S847,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S848
) {
  struct moonbit_result_0 _result_2069;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _result_2069.tag = 1;
  _result_2069.data.ok = 0;
  return _result_2069;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S849,
  moonbit_string_t _M0L12_2adiscard__S850,
  int32_t _M0L12_2adiscard__S851,
  struct _M0TWEu* _M0L12_2adiscard__S852,
  struct _M0TWssbEu* _M0L12_2adiscard__S853,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S854
) {
  struct moonbit_result_0 _result_2070;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _result_2070.tag = 1;
  _result_2070.data.ok = 0;
  return _result_2070;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S855,
  moonbit_string_t _M0L12_2adiscard__S856,
  int32_t _M0L12_2adiscard__S857,
  struct _M0TWEu* _M0L12_2adiscard__S858,
  struct _M0TWssbEu* _M0L12_2adiscard__S859,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S860
) {
  struct moonbit_result_0 _result_2071;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _result_2071.tag = 1;
  _result_2071.data.ok = 0;
  return _result_2071;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S861,
  moonbit_string_t _M0L12_2adiscard__S862,
  int32_t _M0L12_2adiscard__S863,
  struct _M0TWEu* _M0L12_2adiscard__S864,
  struct _M0TWssbEu* _M0L12_2adiscard__S865,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S866
) {
  struct moonbit_result_0 _result_2072;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _result_2072.tag = 1;
  _result_2072.data.ok = 0;
  return _result_2072;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S867,
  moonbit_string_t _M0L12_2adiscard__S868,
  int32_t _M0L12_2adiscard__S869,
  struct _M0TWEu* _M0L12_2adiscard__S870,
  struct _M0TWssbEu* _M0L12_2adiscard__S871,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S872
) {
  struct moonbit_result_0 _result_2073;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _result_2073.tag = 1;
  _result_2073.data.ok = 0;
  return _result_2073;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S842
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25step__iz__with__postspike(
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1pS790,
  float _M0L2dtS802
) {
  int32_t _M0L1nS789;
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L3p__S791;
  float _M0L1aS792;
  float _M0L1bS793;
  float _M0L1cS794;
  float _M0L1dS795;
  float _M0L6tau__eS796;
  float _M0L6tau__iS797;
  float _M0L4e__eS798;
  float _M0L4e__iS799;
  int32_t _M0L7_2abindS800;
  int32_t _M0L1iS801;
  int32_t _M0L7_2abindS804;
  int32_t _M0L1iS805;
  int32_t _M0L7_2abindS812;
  int32_t _M0L1iS813;
  int32_t _M0L7_2abindS816;
  int32_t _M0L1iS817;
  int32_t _M0L7_2abindS819;
  int32_t _M0L1iS820;
  #line 389 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1nS789 = _M0L1pS790->$1;
  _M0L3p__S791 = _M0L1pS790->$0;
  _M0L1aS792 = _M0L3p__S791->$0;
  _M0L1bS793 = _M0L3p__S791->$1;
  _M0L1cS794 = _M0L3p__S791->$2;
  _M0L1dS795 = _M0L3p__S791->$3;
  _M0L6tau__eS796 = _M0L3p__S791->$4;
  _M0L6tau__iS797 = _M0L3p__S791->$5;
  _M0L4e__eS798 = _M0L3p__S791->$6;
  _M0L4e__iS799 = _M0L3p__S791->$7;
  _M0L7_2abindS800 = 0;
  _M0L1iS801 = _M0L7_2abindS800;
  while (1) {
    if (_M0L1iS801 < _M0L1nS789) {
      struct _M0TPB5ArrayGfE* _M0L2geS1868 = _M0L1pS790->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS1876 = _M0L1pS790->$6;
      float _M0L6_2atmpS1870;
      struct _M0TPB5ArrayGfE* _M0L2geS1875;
      float _M0L6_2atmpS1874;
      float _M0L6_2atmpS1873;
      float _M0L6_2atmpS1872;
      float _M0L6_2atmpS1871;
      float _M0L6_2atmpS1869;
      struct _M0TPB5ArrayGfE* _M0L2giS1877;
      struct _M0TPB5ArrayGfE* _M0L2giS1885;
      float _M0L6_2atmpS1879;
      struct _M0TPB5ArrayGfE* _M0L2giS1884;
      float _M0L6_2atmpS1883;
      float _M0L6_2atmpS1882;
      float _M0L6_2atmpS1881;
      float _M0L6_2atmpS1880;
      float _M0L6_2atmpS1878;
      int32_t _M0L6_2atmpS1886;
      #line 402 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1870 = _M0MPC15array5Array2atGfE(_M0L2geS1876, _M0L1iS801);
      _M0L2geS1875 = _M0L1pS790->$6;
      #line 402 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1874 = _M0MPC15array5Array2atGfE(_M0L2geS1875, _M0L1iS801);
      _M0L6_2atmpS1873 = -_M0L6_2atmpS1874;
      _M0L6_2atmpS1872 = _M0L6_2atmpS1873 / _M0L6tau__eS796;
      _M0L6_2atmpS1871 = _M0L2dtS802 * _M0L6_2atmpS1872;
      _M0L6_2atmpS1869 = _M0L6_2atmpS1870 + _M0L6_2atmpS1871;
      #line 402 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1868, _M0L1iS801, _M0L6_2atmpS1869);
      _M0L2giS1877 = _M0L1pS790->$7;
      _M0L2giS1885 = _M0L1pS790->$7;
      #line 403 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1879 = _M0MPC15array5Array2atGfE(_M0L2giS1885, _M0L1iS801);
      _M0L2giS1884 = _M0L1pS790->$7;
      #line 403 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1883 = _M0MPC15array5Array2atGfE(_M0L2giS1884, _M0L1iS801);
      _M0L6_2atmpS1882 = -_M0L6_2atmpS1883;
      _M0L6_2atmpS1881 = _M0L6_2atmpS1882 / _M0L6tau__iS797;
      _M0L6_2atmpS1880 = _M0L2dtS802 * _M0L6_2atmpS1881;
      _M0L6_2atmpS1878 = _M0L6_2atmpS1879 + _M0L6_2atmpS1880;
      #line 403 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1877, _M0L1iS801, _M0L6_2atmpS1878);
      _M0L6_2atmpS1886 = _M0L1iS801 + 1;
      _M0L1iS801 = _M0L6_2atmpS1886;
      continue;
    }
    break;
  }
  _M0L7_2abindS804 = 0;
  _M0L1iS805 = _M0L7_2abindS804;
  while (1) {
    if (_M0L1iS805 < _M0L1nS789) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS1889 = _M0L1pS790->$8;
      int32_t _M0L6_2atmpS1888;
      struct _M0TPB5ArrayGfE* _M0L1vS1919;
      float _M0L1vS808;
      struct _M0TPB5ArrayGfE* _M0L1uS1918;
      float _M0L1uS809;
      struct _M0TPB5ArrayGfE* _M0L1iS1917;
      float _M0L2iiS810;
      struct _M0TPB5ArrayGfE* _M0L1vS1894;
      float _M0L6_2atmpS1897;
      float _M0L6_2atmpS1904;
      float _M0L6_2atmpS1902;
      float _M0L6_2atmpS1903;
      float _M0L6_2atmpS1901;
      float _M0L6_2atmpS1900;
      float _M0L6_2atmpS1899;
      float _M0L6_2atmpS1898;
      float _M0L6_2atmpS1896;
      float _M0L6_2atmpS1895;
      struct _M0TPB5ArrayGfE* _M0L1vS1916;
      float _M0L2v2S811;
      struct _M0TPB5ArrayGfE* _M0L1vS1905;
      float _M0L6_2atmpS1908;
      float _M0L6_2atmpS1915;
      float _M0L6_2atmpS1913;
      float _M0L6_2atmpS1914;
      float _M0L6_2atmpS1912;
      float _M0L6_2atmpS1911;
      float _M0L6_2atmpS1910;
      float _M0L6_2atmpS1909;
      float _M0L6_2atmpS1907;
      float _M0L6_2atmpS1906;
      int32_t _M0L6_2atmpS1887;
      moonbit_incref_cycle_free(_M0L4tabsS1889);
      #line 407 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1888
      = _M0MPC15array5Array2atGiE(_M0L4tabsS1889, _M0L1iS805);
      moonbit_decref_cycle_free(_M0L4tabsS1889);
      if (_M0L6_2atmpS1888 > 0) {
        struct _M0TPB5ArrayGiE* _M0L4tabsS1890 = _M0L1pS790->$8;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1893 = _M0L1pS790->$8;
        int32_t _M0L6_2atmpS1892;
        int32_t _M0L6_2atmpS1891;
        moonbit_incref_cycle_free(_M0L4tabsS1893);
        moonbit_incref_cycle_free(_M0L4tabsS1890);
        #line 408 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS1892
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1893, _M0L1iS805);
        moonbit_decref_cycle_free(_M0L4tabsS1893);
        _M0L6_2atmpS1891 = _M0L6_2atmpS1892 - 1;
        #line 408 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS1890, _M0L1iS805, _M0L6_2atmpS1891);
        moonbit_decref_cycle_free(_M0L4tabsS1890);
        goto join_806;
      }
      _M0L1vS1919 = _M0L1pS790->$2;
      #line 411 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS808 = _M0MPC15array5Array2atGfE(_M0L1vS1919, _M0L1iS805);
      _M0L1uS1918 = _M0L1pS790->$3;
      #line 412 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1uS809 = _M0MPC15array5Array2atGfE(_M0L1uS1918, _M0L1iS805);
      _M0L1iS1917 = _M0L1pS790->$5;
      #line 413 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2iiS810 = _M0MPC15array5Array2atGfE(_M0L1iS1917, _M0L1iS805);
      _M0L1vS1894 = _M0L1pS790->$2;
      _M0L6_2atmpS1897 = 0x1p-1f * _M0L2dtS802;
      _M0L6_2atmpS1904 = 0x1.47ae147ae147bp-5f * _M0L1vS808;
      _M0L6_2atmpS1902 = _M0L6_2atmpS1904 * _M0L1vS808;
      _M0L6_2atmpS1903 = 0x1.4p+2f * _M0L1vS808;
      _M0L6_2atmpS1901 = _M0L6_2atmpS1902 + _M0L6_2atmpS1903;
      _M0L6_2atmpS1900 = _M0L6_2atmpS1901 + 0x1.18p+7f;
      _M0L6_2atmpS1899 = _M0L6_2atmpS1900 - _M0L1uS809;
      _M0L6_2atmpS1898 = _M0L6_2atmpS1899 + _M0L2iiS810;
      _M0L6_2atmpS1896 = _M0L6_2atmpS1897 * _M0L6_2atmpS1898;
      _M0L6_2atmpS1895 = _M0L1vS808 + _M0L6_2atmpS1896;
      #line 414 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1894, _M0L1iS805, _M0L6_2atmpS1895);
      _M0L1vS1916 = _M0L1pS790->$2;
      #line 415 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2v2S811 = _M0MPC15array5Array2atGfE(_M0L1vS1916, _M0L1iS805);
      _M0L1vS1905 = _M0L1pS790->$2;
      _M0L6_2atmpS1908 = 0x1p-1f * _M0L2dtS802;
      _M0L6_2atmpS1915 = 0x1.47ae147ae147bp-5f * _M0L2v2S811;
      _M0L6_2atmpS1913 = _M0L6_2atmpS1915 * _M0L2v2S811;
      _M0L6_2atmpS1914 = 0x1.4p+2f * _M0L2v2S811;
      _M0L6_2atmpS1912 = _M0L6_2atmpS1913 + _M0L6_2atmpS1914;
      _M0L6_2atmpS1911 = _M0L6_2atmpS1912 + 0x1.18p+7f;
      _M0L6_2atmpS1910 = _M0L6_2atmpS1911 - _M0L1uS809;
      _M0L6_2atmpS1909 = _M0L6_2atmpS1910 + _M0L2iiS810;
      _M0L6_2atmpS1907 = _M0L6_2atmpS1908 * _M0L6_2atmpS1909;
      _M0L6_2atmpS1906 = _M0L2v2S811 + _M0L6_2atmpS1907;
      #line 416 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1905, _M0L1iS805, _M0L6_2atmpS1906);
      goto join_806;
      goto joinlet_2076;
      join_806:;
      _M0L6_2atmpS1887 = _M0L1iS805 + 1;
      _M0L1iS805 = _M0L6_2atmpS1887;
      continue;
      joinlet_2076:;
    }
    break;
  }
  _M0L7_2abindS812 = 0;
  _M0L1iS813 = _M0L7_2abindS812;
  while (1) {
    if (_M0L1iS813 < _M0L1nS789) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS1921 = _M0L1pS790->$8;
      int32_t _M0L6_2atmpS1920;
      int32_t _M0L6_2atmpS1933;
      moonbit_incref_cycle_free(_M0L4tabsS1921);
      #line 419 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1920
      = _M0MPC15array5Array2atGiE(_M0L4tabsS1921, _M0L1iS813);
      moonbit_decref_cycle_free(_M0L4tabsS1921);
      if (_M0L6_2atmpS1920 <= 0) {
        struct _M0TPB5ArrayGfE* _M0L1vS1932 = _M0L1pS790->$2;
        float _M0L1vS814;
        struct _M0TPB5ArrayGfE* _M0L1uS1922;
        struct _M0TPB5ArrayGfE* _M0L1uS1931;
        float _M0L6_2atmpS1924;
        float _M0L6_2atmpS1926;
        float _M0L6_2atmpS1928;
        struct _M0TPB5ArrayGfE* _M0L1uS1930;
        float _M0L6_2atmpS1929;
        float _M0L6_2atmpS1927;
        float _M0L6_2atmpS1925;
        float _M0L6_2atmpS1923;
        #line 420 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L1vS814 = _M0MPC15array5Array2atGfE(_M0L1vS1932, _M0L1iS813);
        _M0L1uS1922 = _M0L1pS790->$3;
        _M0L1uS1931 = _M0L1pS790->$3;
        #line 421 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS1924 = _M0MPC15array5Array2atGfE(_M0L1uS1931, _M0L1iS813);
        _M0L6_2atmpS1926 = _M0L2dtS802 * _M0L1aS792;
        _M0L6_2atmpS1928 = _M0L1bS793 * _M0L1vS814;
        _M0L1uS1930 = _M0L1pS790->$3;
        #line 421 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS1929 = _M0MPC15array5Array2atGfE(_M0L1uS1930, _M0L1iS813);
        _M0L6_2atmpS1927 = _M0L6_2atmpS1928 - _M0L6_2atmpS1929;
        _M0L6_2atmpS1925 = _M0L6_2atmpS1926 * _M0L6_2atmpS1927;
        _M0L6_2atmpS1923 = _M0L6_2atmpS1924 + _M0L6_2atmpS1925;
        #line 421 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS1922, _M0L1iS813, _M0L6_2atmpS1923);
      }
      _M0L6_2atmpS1933 = _M0L1iS813 + 1;
      _M0L1iS813 = _M0L6_2atmpS1933;
      continue;
    }
    break;
  }
  _M0L7_2abindS816 = 0;
  _M0L1iS817 = _M0L7_2abindS816;
  while (1) {
    if (_M0L1iS817 < _M0L1nS789) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS1935 = _M0L1pS790->$8;
      int32_t _M0L6_2atmpS1934;
      int32_t _M0L6_2atmpS1954;
      moonbit_incref_cycle_free(_M0L4tabsS1935);
      #line 425 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1934
      = _M0MPC15array5Array2atGiE(_M0L4tabsS1935, _M0L1iS817);
      moonbit_decref_cycle_free(_M0L4tabsS1935);
      if (_M0L6_2atmpS1934 <= 0) {
        struct _M0TPB5ArrayGfE* _M0L1vS1936 = _M0L1pS790->$2;
        struct _M0TPB5ArrayGfE* _M0L1vS1953 = _M0L1pS790->$2;
        float _M0L6_2atmpS1938;
        struct _M0TPB5ArrayGfE* _M0L2geS1952;
        float _M0L6_2atmpS1948;
        struct _M0TPB5ArrayGfE* _M0L1vS1951;
        float _M0L6_2atmpS1950;
        float _M0L6_2atmpS1949;
        float _M0L6_2atmpS1941;
        struct _M0TPB5ArrayGfE* _M0L2giS1947;
        float _M0L6_2atmpS1943;
        struct _M0TPB5ArrayGfE* _M0L1vS1946;
        float _M0L6_2atmpS1945;
        float _M0L6_2atmpS1944;
        float _M0L6_2atmpS1942;
        float _M0L6_2atmpS1940;
        float _M0L6_2atmpS1939;
        float _M0L6_2atmpS1937;
        #line 426 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS1938 = _M0MPC15array5Array2atGfE(_M0L1vS1953, _M0L1iS817);
        _M0L2geS1952 = _M0L1pS790->$6;
        #line 426 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS1948
        = _M0MPC15array5Array2atGfE(_M0L2geS1952, _M0L1iS817);
        _M0L1vS1951 = _M0L1pS790->$2;
        #line 426 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS1950 = _M0MPC15array5Array2atGfE(_M0L1vS1951, _M0L1iS817);
        _M0L6_2atmpS1949 = _M0L4e__eS798 - _M0L6_2atmpS1950;
        _M0L6_2atmpS1941 = _M0L6_2atmpS1948 * _M0L6_2atmpS1949;
        _M0L2giS1947 = _M0L1pS790->$7;
        #line 426 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS1943
        = _M0MPC15array5Array2atGfE(_M0L2giS1947, _M0L1iS817);
        _M0L1vS1946 = _M0L1pS790->$2;
        #line 426 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS1945 = _M0MPC15array5Array2atGfE(_M0L1vS1946, _M0L1iS817);
        _M0L6_2atmpS1944 = _M0L4e__iS799 - _M0L6_2atmpS1945;
        _M0L6_2atmpS1942 = _M0L6_2atmpS1943 * _M0L6_2atmpS1944;
        _M0L6_2atmpS1940 = _M0L6_2atmpS1941 + _M0L6_2atmpS1942;
        _M0L6_2atmpS1939 = _M0L2dtS802 * _M0L6_2atmpS1940;
        _M0L6_2atmpS1937 = _M0L6_2atmpS1938 + _M0L6_2atmpS1939;
        #line 426 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0MPC15array5Array3setGfE(_M0L1vS1936, _M0L1iS817, _M0L6_2atmpS1937);
      }
      _M0L6_2atmpS1954 = _M0L1iS817 + 1;
      _M0L1iS817 = _M0L6_2atmpS1954;
      continue;
    }
    break;
  }
  _M0L7_2abindS819 = 0;
  _M0L1iS820 = _M0L7_2abindS819;
  while (1) {
    if (_M0L1iS820 < _M0L1nS789) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS1957 = _M0L1pS790->$8;
      int32_t _M0L6_2atmpS1956;
      struct _M0TPB5ArrayGbE* _M0L4fireS1959;
      struct _M0TPB5ArrayGfE* _M0L1vS1962;
      float _M0L6_2atmpS1961;
      int32_t _M0L6_2atmpS1960;
      struct _M0TPB5ArrayGbE* _M0L4fireS1963;
      int32_t _M0L6_2atmpS1955;
      moonbit_incref_cycle_free(_M0L4tabsS1957);
      #line 431 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1956
      = _M0MPC15array5Array2atGiE(_M0L4tabsS1957, _M0L1iS820);
      moonbit_decref_cycle_free(_M0L4tabsS1957);
      if (_M0L6_2atmpS1956 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS1958 = _M0L1pS790->$4;
        #line 432 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS1958, _M0L1iS820, 0);
        goto join_821;
      }
      _M0L4fireS1959 = _M0L1pS790->$4;
      _M0L1vS1962 = _M0L1pS790->$2;
      #line 435 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1961 = _M0MPC15array5Array2atGfE(_M0L1vS1962, _M0L1iS820);
      _M0L6_2atmpS1960 = _M0L6_2atmpS1961 > 0x1.ep+4f;
      #line 435 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1959, _M0L1iS820, _M0L6_2atmpS1960);
      _M0L4fireS1963 = _M0L1pS790->$4;
      #line 436 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1963, _M0L1iS820)) {
        struct _M0TPB5ArrayGfE* _M0L1vS1964 = _M0L1pS790->$2;
        struct _M0TPB5ArrayGfE* _M0L1uS1965;
        struct _M0TPB5ArrayGfE* _M0L1uS1968;
        float _M0L6_2atmpS1967;
        float _M0L6_2atmpS1966;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1969;
        int32_t _M0L11tabs__constS1970;
        #line 437 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0MPC15array5Array3setGfE(_M0L1vS1964, _M0L1iS820, _M0L1cS794);
        _M0L1uS1965 = _M0L1pS790->$3;
        _M0L1uS1968 = _M0L1pS790->$3;
        #line 438 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS1967 = _M0MPC15array5Array2atGfE(_M0L1uS1968, _M0L1iS820);
        _M0L6_2atmpS1966 = _M0L6_2atmpS1967 + _M0L1dS795;
        #line 438 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS1965, _M0L1iS820, _M0L6_2atmpS1966);
        _M0L4tabsS1969 = _M0L1pS790->$8;
        _M0L11tabs__constS1970 = _M0L1pS790->$9;
        moonbit_incref_cycle_free(_M0L4tabsS1969);
        #line 439 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS1969, _M0L1iS820, _M0L11tabs__constS1970);
        moonbit_decref_cycle_free(_M0L4tabsS1969);
      }
      goto join_821;
      goto joinlet_2080;
      join_821:;
      _M0L6_2atmpS1955 = _M0L1iS820 + 1;
      _M0L1iS820 = _M0L6_2atmpS1955;
      continue;
      joinlet_2080:;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt2IZ* _M0MP26RiantR8snn__mbt2IZ21init__with__postspike(
  int32_t _M0L1nS778,
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L5paramS787,
  float _M0L7v__initS779,
  float _M0L7u__initS781,
  struct _M0TP26RiantR8snn__mbt11IZPostSpike* _M0L9postspikeS788
) {
  struct _M0TPB5ArrayGfE* _M0L1vS777;
  struct _M0TPB5ArrayGfE* _M0L1uS780;
  struct _M0TPB5ArrayGbE* _M0L4fireS782;
  struct _M0TPB5ArrayGfE* _M0L1iS783;
  struct _M0TPB5ArrayGfE* _M0L2geS784;
  struct _M0TPB5ArrayGfE* _M0L2giS785;
  struct _M0TPB5ArrayGiE* _M0L4tabsS786;
  int32_t _M0L11tabs__constS1867;
  struct _M0TP26RiantR8snn__mbt2IZ* _block_2081;
  #line 273 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1vS777 = _M0MPC15array5Array4makeGfE(_M0L1nS778, _M0L7v__initS779);
  #line 281 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1uS780 = _M0MPC15array5Array4makeGfE(_M0L1nS778, _M0L7u__initS781);
  #line 282 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L4fireS782 = _M0MPC15array5Array4makeGbE(_M0L1nS778, 0);
  #line 283 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1iS783 = _M0MPC15array5Array4makeGfE(_M0L1nS778, 0x0p+0f);
  #line 284 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2geS784 = _M0MPC15array5Array4makeGfE(_M0L1nS778, 0x0p+0f);
  #line 285 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2giS785 = _M0MPC15array5Array4makeGfE(_M0L1nS778, 0x0p+0f);
  #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L4tabsS786 = _M0MPC15array5Array4makeGiE(_M0L1nS778, 0);
  _M0L11tabs__constS1867 = _M0L9postspikeS788->$0;
  moonbit_incref_cycle_free(_M0L5paramS787);
  _block_2081
  = (struct _M0TP26RiantR8snn__mbt2IZ*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IZ));
  Moonbit_object_header(_block_2081)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2081->$0 = _M0L5paramS787;
  _block_2081->$1 = _M0L1nS778;
  _block_2081->$2 = _M0L1vS777;
  _block_2081->$3 = _M0L1uS780;
  _block_2081->$4 = _M0L4fireS782;
  _block_2081->$5 = _M0L1iS783;
  _block_2081->$6 = _M0L2geS784;
  _block_2081->$7 = _M0L2giS785;
  _block_2081->$8 = _M0L4tabsS786;
  _block_2081->$9 = _M0L11tabs__constS1867;
  return _block_2081;
}

struct _M0TP26RiantR8snn__mbt11IZPostSpike* _M0MP26RiantR8snn__mbt11IZPostSpike6custom(
  int32_t _M0L11tabs__constS776
) {
  struct _M0TP26RiantR8snn__mbt11IZPostSpike* _block_2082;
  #line 265 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _block_2082
  = (struct _M0TP26RiantR8snn__mbt11IZPostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IZPostSpike));
  Moonbit_object_header(_block_2082)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2082->$0 = _M0L11tabs__constS776;
  return _block_2082;
}

struct _M0TP26RiantR8snn__mbt2IZ* _M0MP26RiantR8snn__mbt2IZ3new(
  int32_t _M0L1nS764,
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L5paramS768,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS774
) {
  struct _M0TPB5ArrayGfE* _M0L1vS763;
  struct _M0TPB5ArrayGfE* _M0L1uS765;
  int32_t _M0L7_2abindS766;
  int32_t _M0L1kS767;
  struct _M0TPB5ArrayGbE* _M0L4fireS770;
  struct _M0TPB5ArrayGfE* _M0L1iS771;
  struct _M0TPB5ArrayGfE* _M0L2geS772;
  struct _M0TPB5ArrayGfE* _M0L2giS773;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L6_2atmpS2025;
  struct _M0TPB5ArrayGiE* _M0L4tabsS775;
  struct _M0TP26RiantR8snn__mbt2IZ* _block_2084;
  #line 105 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1vS763 = _M0MPC15array5Array4makeGfE(_M0L1nS764, -0x1.04p+6f);
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1uS765 = _M0MPC15array5Array4makeGfE(_M0L1nS764, 0x0p+0f);
  _M0L7_2abindS766 = 0;
  _M0L1kS767 = _M0L7_2abindS766;
  while (1) {
    if (_M0L1kS767 < _M0L1nS764) {
      float _M0L1bS1864 = _M0L5paramS768->$1;
      float _M0L6_2atmpS1865;
      float _M0L6_2atmpS1863;
      int32_t _M0L6_2atmpS1866;
      #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1865 = _M0MPC15array5Array2atGfE(_M0L1vS763, _M0L1kS767);
      _M0L6_2atmpS1863 = _M0L1bS1864 * _M0L6_2atmpS1865;
      #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS765, _M0L1kS767, _M0L6_2atmpS1863);
      _M0L6_2atmpS1866 = _M0L1kS767 + 1;
      _M0L1kS767 = _M0L6_2atmpS1866;
      continue;
    }
    break;
  }
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L4fireS770 = _M0MPC15array5Array4makeGbE(_M0L1nS764, 0);
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1iS771 = _M0MPC15array5Array4makeGfE(_M0L1nS764, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2geS772 = _M0MPC15array5Array4makeGfE(_M0L1nS764, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2giS773 = _M0MPC15array5Array4makeGfE(_M0L1nS764, 0x0p+0f);
  _M0L6_2atmpS2025 = _M0L3rngS774;
  #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L4tabsS775 = _M0MPC15array5Array4makeGiE(_M0L1nS764, 0);
  moonbit_incref_cycle_free(_M0L5paramS768);
  _block_2084
  = (struct _M0TP26RiantR8snn__mbt2IZ*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IZ));
  Moonbit_object_header(_block_2084)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2084->$0 = _M0L5paramS768;
  _block_2084->$1 = _M0L1nS764;
  _block_2084->$2 = _M0L1vS763;
  _block_2084->$3 = _M0L1uS765;
  _block_2084->$4 = _M0L4fireS770;
  _block_2084->$5 = _M0L1iS771;
  _block_2084->$6 = _M0L2geS772;
  _block_2084->$7 = _M0L2giS773;
  _block_2084->$8 = _M0L4tabsS775;
  _block_2084->$9 = 0;
  return _block_2084;
}

struct _M0TP26RiantR8snn__mbt11IZParameter* _M0MP26RiantR8snn__mbt11IZParameter2rs(
  
) {
  struct _M0TP26RiantR8snn__mbt11IZParameter* _block_2085;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _block_2085
  = (struct _M0TP26RiantR8snn__mbt11IZParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IZParameter));
  Moonbit_object_header(_block_2085)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2085->$0 = 0x1.47ae147ae147bp-6f;
  _block_2085->$1 = 0x1.999999999999ap-3f;
  _block_2085->$2 = -0x1.04p+6f;
  _block_2085->$3 = 0x1p+3f;
  _block_2085->$4 = 0x1.4p+2f;
  _block_2085->$5 = 0x1.4p+3f;
  _block_2085->$6 = 0x0p+0f;
  _block_2085->$7 = -0x1.4p+6f;
  return _block_2085;
}

int32_t _M0FP26RiantR8snn__mbt8step__iz(
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1pS732,
  float _M0L2dtS744
) {
  int32_t _M0L1nS731;
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L3p__S733;
  float _M0L1aS734;
  float _M0L1bS735;
  float _M0L1cS736;
  float _M0L1dS737;
  float _M0L6tau__eS738;
  float _M0L6tau__iS739;
  float _M0L4e__eS740;
  float _M0L4e__iS741;
  int32_t _M0L7_2abindS742;
  int32_t _M0L1iS743;
  int32_t _M0L7_2abindS746;
  int32_t _M0L1iS747;
  int32_t _M0L7_2abindS753;
  int32_t _M0L1iS754;
  int32_t _M0L7_2abindS757;
  int32_t _M0L1iS758;
  int32_t _M0L7_2abindS760;
  int32_t _M0L1iS761;
  #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1nS731 = _M0L1pS732->$1;
  _M0L3p__S733 = _M0L1pS732->$0;
  _M0L1aS734 = _M0L3p__S733->$0;
  _M0L1bS735 = _M0L3p__S733->$1;
  _M0L1cS736 = _M0L3p__S733->$2;
  _M0L1dS737 = _M0L3p__S733->$3;
  _M0L6tau__eS738 = _M0L3p__S733->$4;
  _M0L6tau__iS739 = _M0L3p__S733->$5;
  _M0L4e__eS740 = _M0L3p__S733->$6;
  _M0L4e__iS741 = _M0L3p__S733->$7;
  _M0L7_2abindS742 = 0;
  _M0L1iS743 = _M0L7_2abindS742;
  while (1) {
    if (_M0L1iS743 < _M0L1nS731) {
      struct _M0TPB5ArrayGfE* _M0L2geS1771 = _M0L1pS732->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS1779 = _M0L1pS732->$6;
      float _M0L6_2atmpS1773;
      struct _M0TPB5ArrayGfE* _M0L2geS1778;
      float _M0L6_2atmpS1777;
      float _M0L6_2atmpS1776;
      float _M0L6_2atmpS1775;
      float _M0L6_2atmpS1774;
      float _M0L6_2atmpS1772;
      struct _M0TPB5ArrayGfE* _M0L2giS1780;
      struct _M0TPB5ArrayGfE* _M0L2giS1788;
      float _M0L6_2atmpS1782;
      struct _M0TPB5ArrayGfE* _M0L2giS1787;
      float _M0L6_2atmpS1786;
      float _M0L6_2atmpS1785;
      float _M0L6_2atmpS1784;
      float _M0L6_2atmpS1783;
      float _M0L6_2atmpS1781;
      int32_t _M0L6_2atmpS1789;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1773 = _M0MPC15array5Array2atGfE(_M0L2geS1779, _M0L1iS743);
      _M0L2geS1778 = _M0L1pS732->$6;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1777 = _M0MPC15array5Array2atGfE(_M0L2geS1778, _M0L1iS743);
      _M0L6_2atmpS1776 = -_M0L6_2atmpS1777;
      _M0L6_2atmpS1775 = _M0L2dtS744 * _M0L6_2atmpS1776;
      _M0L6_2atmpS1774 = _M0L6_2atmpS1775 / _M0L6tau__eS738;
      _M0L6_2atmpS1772 = _M0L6_2atmpS1773 + _M0L6_2atmpS1774;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1771, _M0L1iS743, _M0L6_2atmpS1772);
      _M0L2giS1780 = _M0L1pS732->$7;
      _M0L2giS1788 = _M0L1pS732->$7;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1782 = _M0MPC15array5Array2atGfE(_M0L2giS1788, _M0L1iS743);
      _M0L2giS1787 = _M0L1pS732->$7;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1786 = _M0MPC15array5Array2atGfE(_M0L2giS1787, _M0L1iS743);
      _M0L6_2atmpS1785 = -_M0L6_2atmpS1786;
      _M0L6_2atmpS1784 = _M0L2dtS744 * _M0L6_2atmpS1785;
      _M0L6_2atmpS1783 = _M0L6_2atmpS1784 / _M0L6tau__iS739;
      _M0L6_2atmpS1781 = _M0L6_2atmpS1782 + _M0L6_2atmpS1783;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1780, _M0L1iS743, _M0L6_2atmpS1781);
      _M0L6_2atmpS1789 = _M0L1iS743 + 1;
      _M0L1iS743 = _M0L6_2atmpS1789;
      continue;
    }
    break;
  }
  _M0L7_2abindS746 = 0;
  _M0L1iS747 = _M0L7_2abindS746;
  while (1) {
    if (_M0L1iS747 < _M0L1nS731) {
      struct _M0TPB5ArrayGfE* _M0L1vS1815 = _M0L1pS732->$2;
      float _M0L1vS748;
      struct _M0TPB5ArrayGfE* _M0L1uS1814;
      float _M0L1uS749;
      struct _M0TPB5ArrayGfE* _M0L1iS1813;
      float _M0L2iiS750;
      struct _M0TPB5ArrayGfE* _M0L1vS1790;
      float _M0L6_2atmpS1793;
      float _M0L6_2atmpS1800;
      float _M0L6_2atmpS1798;
      float _M0L6_2atmpS1799;
      float _M0L6_2atmpS1797;
      float _M0L6_2atmpS1796;
      float _M0L6_2atmpS1795;
      float _M0L6_2atmpS1794;
      float _M0L6_2atmpS1792;
      float _M0L6_2atmpS1791;
      struct _M0TPB5ArrayGfE* _M0L1vS1812;
      float _M0L2v2S751;
      struct _M0TPB5ArrayGfE* _M0L1vS1801;
      float _M0L6_2atmpS1804;
      float _M0L6_2atmpS1811;
      float _M0L6_2atmpS1809;
      float _M0L6_2atmpS1810;
      float _M0L6_2atmpS1808;
      float _M0L6_2atmpS1807;
      float _M0L6_2atmpS1806;
      float _M0L6_2atmpS1805;
      float _M0L6_2atmpS1803;
      float _M0L6_2atmpS1802;
      int32_t _M0L6_2atmpS1816;
      #line 359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS748 = _M0MPC15array5Array2atGfE(_M0L1vS1815, _M0L1iS747);
      _M0L1uS1814 = _M0L1pS732->$3;
      #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1uS749 = _M0MPC15array5Array2atGfE(_M0L1uS1814, _M0L1iS747);
      _M0L1iS1813 = _M0L1pS732->$5;
      #line 361 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2iiS750 = _M0MPC15array5Array2atGfE(_M0L1iS1813, _M0L1iS747);
      _M0L1vS1790 = _M0L1pS732->$2;
      _M0L6_2atmpS1793 = 0x1p-1f * _M0L2dtS744;
      _M0L6_2atmpS1800 = 0x1.47ae147ae147bp-5f * _M0L1vS748;
      _M0L6_2atmpS1798 = _M0L6_2atmpS1800 * _M0L1vS748;
      _M0L6_2atmpS1799 = 0x1.4p+2f * _M0L1vS748;
      _M0L6_2atmpS1797 = _M0L6_2atmpS1798 + _M0L6_2atmpS1799;
      _M0L6_2atmpS1796 = _M0L6_2atmpS1797 + 0x1.18p+7f;
      _M0L6_2atmpS1795 = _M0L6_2atmpS1796 - _M0L1uS749;
      _M0L6_2atmpS1794 = _M0L6_2atmpS1795 + _M0L2iiS750;
      _M0L6_2atmpS1792 = _M0L6_2atmpS1793 * _M0L6_2atmpS1794;
      _M0L6_2atmpS1791 = _M0L1vS748 + _M0L6_2atmpS1792;
      #line 362 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1790, _M0L1iS747, _M0L6_2atmpS1791);
      _M0L1vS1812 = _M0L1pS732->$2;
      #line 363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2v2S751 = _M0MPC15array5Array2atGfE(_M0L1vS1812, _M0L1iS747);
      _M0L1vS1801 = _M0L1pS732->$2;
      _M0L6_2atmpS1804 = 0x1p-1f * _M0L2dtS744;
      _M0L6_2atmpS1811 = 0x1.47ae147ae147bp-5f * _M0L2v2S751;
      _M0L6_2atmpS1809 = _M0L6_2atmpS1811 * _M0L2v2S751;
      _M0L6_2atmpS1810 = 0x1.4p+2f * _M0L2v2S751;
      _M0L6_2atmpS1808 = _M0L6_2atmpS1809 + _M0L6_2atmpS1810;
      _M0L6_2atmpS1807 = _M0L6_2atmpS1808 + 0x1.18p+7f;
      _M0L6_2atmpS1806 = _M0L6_2atmpS1807 - _M0L1uS749;
      _M0L6_2atmpS1805 = _M0L6_2atmpS1806 + _M0L2iiS750;
      _M0L6_2atmpS1803 = _M0L6_2atmpS1804 * _M0L6_2atmpS1805;
      _M0L6_2atmpS1802 = _M0L2v2S751 + _M0L6_2atmpS1803;
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1801, _M0L1iS747, _M0L6_2atmpS1802);
      _M0L6_2atmpS1816 = _M0L1iS747 + 1;
      _M0L1iS747 = _M0L6_2atmpS1816;
      continue;
    }
    break;
  }
  _M0L7_2abindS753 = 0;
  _M0L1iS754 = _M0L7_2abindS753;
  while (1) {
    if (_M0L1iS754 < _M0L1nS731) {
      struct _M0TPB5ArrayGfE* _M0L1vS1827 = _M0L1pS732->$2;
      float _M0L1vS755;
      struct _M0TPB5ArrayGfE* _M0L1uS1817;
      struct _M0TPB5ArrayGfE* _M0L1uS1826;
      float _M0L6_2atmpS1819;
      float _M0L6_2atmpS1821;
      float _M0L6_2atmpS1823;
      struct _M0TPB5ArrayGfE* _M0L1uS1825;
      float _M0L6_2atmpS1824;
      float _M0L6_2atmpS1822;
      float _M0L6_2atmpS1820;
      float _M0L6_2atmpS1818;
      int32_t _M0L6_2atmpS1828;
      #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS755 = _M0MPC15array5Array2atGfE(_M0L1vS1827, _M0L1iS754);
      _M0L1uS1817 = _M0L1pS732->$3;
      _M0L1uS1826 = _M0L1pS732->$3;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1819 = _M0MPC15array5Array2atGfE(_M0L1uS1826, _M0L1iS754);
      _M0L6_2atmpS1821 = _M0L2dtS744 * _M0L1aS734;
      _M0L6_2atmpS1823 = _M0L1bS735 * _M0L1vS755;
      _M0L1uS1825 = _M0L1pS732->$3;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1824 = _M0MPC15array5Array2atGfE(_M0L1uS1825, _M0L1iS754);
      _M0L6_2atmpS1822 = _M0L6_2atmpS1823 - _M0L6_2atmpS1824;
      _M0L6_2atmpS1820 = _M0L6_2atmpS1821 * _M0L6_2atmpS1822;
      _M0L6_2atmpS1818 = _M0L6_2atmpS1819 + _M0L6_2atmpS1820;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS1817, _M0L1iS754, _M0L6_2atmpS1818);
      _M0L6_2atmpS1828 = _M0L1iS754 + 1;
      _M0L1iS754 = _M0L6_2atmpS1828;
      continue;
    }
    break;
  }
  _M0L7_2abindS757 = 0;
  _M0L1iS758 = _M0L7_2abindS757;
  while (1) {
    if (_M0L1iS758 < _M0L1nS731) {
      struct _M0TPB5ArrayGfE* _M0L1vS1829 = _M0L1pS732->$2;
      struct _M0TPB5ArrayGfE* _M0L1vS1846 = _M0L1pS732->$2;
      float _M0L6_2atmpS1831;
      struct _M0TPB5ArrayGfE* _M0L2geS1845;
      float _M0L6_2atmpS1841;
      struct _M0TPB5ArrayGfE* _M0L1vS1844;
      float _M0L6_2atmpS1843;
      float _M0L6_2atmpS1842;
      float _M0L6_2atmpS1834;
      struct _M0TPB5ArrayGfE* _M0L2giS1840;
      float _M0L6_2atmpS1836;
      struct _M0TPB5ArrayGfE* _M0L1vS1839;
      float _M0L6_2atmpS1838;
      float _M0L6_2atmpS1837;
      float _M0L6_2atmpS1835;
      float _M0L6_2atmpS1833;
      float _M0L6_2atmpS1832;
      float _M0L6_2atmpS1830;
      int32_t _M0L6_2atmpS1847;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1831 = _M0MPC15array5Array2atGfE(_M0L1vS1846, _M0L1iS758);
      _M0L2geS1845 = _M0L1pS732->$6;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1841 = _M0MPC15array5Array2atGfE(_M0L2geS1845, _M0L1iS758);
      _M0L1vS1844 = _M0L1pS732->$2;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1843 = _M0MPC15array5Array2atGfE(_M0L1vS1844, _M0L1iS758);
      _M0L6_2atmpS1842 = _M0L4e__eS740 - _M0L6_2atmpS1843;
      _M0L6_2atmpS1834 = _M0L6_2atmpS1841 * _M0L6_2atmpS1842;
      _M0L2giS1840 = _M0L1pS732->$7;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1836 = _M0MPC15array5Array2atGfE(_M0L2giS1840, _M0L1iS758);
      _M0L1vS1839 = _M0L1pS732->$2;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1838 = _M0MPC15array5Array2atGfE(_M0L1vS1839, _M0L1iS758);
      _M0L6_2atmpS1837 = _M0L4e__iS741 - _M0L6_2atmpS1838;
      _M0L6_2atmpS1835 = _M0L6_2atmpS1836 * _M0L6_2atmpS1837;
      _M0L6_2atmpS1833 = _M0L6_2atmpS1834 + _M0L6_2atmpS1835;
      _M0L6_2atmpS1832 = _M0L2dtS744 * _M0L6_2atmpS1833;
      _M0L6_2atmpS1830 = _M0L6_2atmpS1831 + _M0L6_2atmpS1832;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1829, _M0L1iS758, _M0L6_2atmpS1830);
      _M0L6_2atmpS1847 = _M0L1iS758 + 1;
      _M0L1iS758 = _M0L6_2atmpS1847;
      continue;
    }
    break;
  }
  _M0L7_2abindS760 = 0;
  _M0L1iS761 = _M0L7_2abindS760;
  while (1) {
    if (_M0L1iS761 < _M0L1nS731) {
      struct _M0TPB5ArrayGbE* _M0L4fireS1848 = _M0L1pS732->$4;
      struct _M0TPB5ArrayGfE* _M0L1vS1851 = _M0L1pS732->$2;
      float _M0L6_2atmpS1850;
      int32_t _M0L6_2atmpS1849;
      struct _M0TPB5ArrayGfE* _M0L1vS1852;
      struct _M0TPB5ArrayGbE* _M0L4fireS1854;
      float _M0L6_2atmpS1853;
      struct _M0TPB5ArrayGfE* _M0L1uS1856;
      struct _M0TPB5ArrayGfE* _M0L1uS1861;
      float _M0L6_2atmpS1858;
      struct _M0TPB5ArrayGbE* _M0L4fireS1860;
      float _M0L6_2atmpS1859;
      float _M0L6_2atmpS1857;
      int32_t _M0L6_2atmpS1862;
      #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1850 = _M0MPC15array5Array2atGfE(_M0L1vS1851, _M0L1iS761);
      _M0L6_2atmpS1849 = _M0L6_2atmpS1850 > 0x1.ep+4f;
      #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1848, _M0L1iS761, _M0L6_2atmpS1849);
      _M0L1vS1852 = _M0L1pS732->$2;
      _M0L4fireS1854 = _M0L1pS732->$4;
      #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1854, _M0L1iS761)) {
        _M0L6_2atmpS1853 = _M0L1cS736;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS1855 = _M0L1pS732->$2;
        #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS1853 = _M0MPC15array5Array2atGfE(_M0L1vS1855, _M0L1iS761);
      }
      #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1852, _M0L1iS761, _M0L6_2atmpS1853);
      _M0L1uS1856 = _M0L1pS732->$3;
      _M0L1uS1861 = _M0L1pS732->$3;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS1858 = _M0MPC15array5Array2atGfE(_M0L1uS1861, _M0L1iS761);
      _M0L4fireS1860 = _M0L1pS732->$4;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1860, _M0L1iS761)) {
        _M0L6_2atmpS1859 = _M0L1dS737;
      } else {
        _M0L6_2atmpS1859 = 0x0p+0f;
      }
      _M0L6_2atmpS1857 = _M0L6_2atmpS1858 + _M0L6_2atmpS1859;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS1856, _M0L1iS761, _M0L6_2atmpS1857);
      _M0L6_2atmpS1862 = _M0L1iS761 + 1;
      _M0L1iS761 = _M0L6_2atmpS1862;
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
  uint64_t _M0L4seedS729
) {
  struct _M0TUmmmmE* _M0L1sS728;
  uint64_t _M0L6_2atmpS1770;
  struct _M0TUmmmmE* _M0L1tS730;
  uint64_t _M0L6_2atmpS1766;
  uint64_t _M0L6_2atmpS1767;
  uint64_t _M0L6_2atmpS1768;
  uint64_t _M0L6_2atmpS1769;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2091;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS728 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS729);
  _M0L6_2atmpS1770 = _M0L1sS728->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS730 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS1770);
  _M0L6_2atmpS1766 = _M0L1sS728->$0;
  _M0L6_2atmpS1767 = _M0L1sS728->$1;
  _M0L6_2atmpS1768 = _M0L1sS728->$2;
  moonbit_decref_cycle_free(_M0L1sS728);
  _M0L6_2atmpS1769 = _M0L1tS730->$0;
  moonbit_decref_cycle_free(_M0L1tS730);
  _block_2091
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2091)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2091->$0 = _M0L6_2atmpS1766;
  _block_2091->$1 = _M0L6_2atmpS1767;
  _block_2091->$2 = _M0L6_2atmpS1768;
  _block_2091->$3 = _M0L6_2atmpS1769;
  return _block_2091;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS720) {
  uint64_t _M0L2s1S719;
  uint64_t _M0L2z1S721;
  uint64_t _M0L2s2S722;
  uint64_t _M0L2z2S723;
  uint64_t _M0L2s3S724;
  uint64_t _M0L2z3S725;
  uint64_t _M0L2s4S726;
  uint64_t _M0L2z4S727;
  struct _M0TUmmmmE* _block_2092;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S719 = _M0L4seedS720 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S721 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S719);
  _M0L2s2S722 = _M0L2s1S719 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S723 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S722);
  _M0L2s3S724 = _M0L2s2S722 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S725 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S724);
  _M0L2s4S726 = _M0L2s3S724 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S727 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S726);
  _block_2092 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2092)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2092->$0 = _M0L2z1S721;
  _block_2092->$1 = _M0L2z2S723;
  _block_2092->$2 = _M0L2z3S725;
  _block_2092->$3 = _M0L2z4S727;
  return _block_2092;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS717) {
  uint64_t _M0L6_2atmpS1765;
  uint64_t _M0L6_2atmpS1764;
  uint64_t _M0L1zS716;
  uint64_t _M0L6_2atmpS1763;
  uint64_t _M0L6_2atmpS1762;
  uint64_t _M0L1zS718;
  uint64_t _M0L6_2atmpS1761;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1765 = _M0L1zS717 >> 30;
  _M0L6_2atmpS1764 = _M0L1zS717 ^ _M0L6_2atmpS1765;
  _M0L1zS716 = _M0L6_2atmpS1764 * 13787848793156543929ull;
  _M0L6_2atmpS1763 = _M0L1zS716 >> 27;
  _M0L6_2atmpS1762 = _M0L1zS716 ^ _M0L6_2atmpS1763;
  _M0L1zS718 = _M0L6_2atmpS1762 * 10723151780598845931ull;
  _M0L6_2atmpS1761 = _M0L1zS718 >> 31;
  return _M0L1zS718 ^ _M0L6_2atmpS1761;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS715) {
  double _M0L6_2atmpS1760;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1760 = (double)_M0L4selfS715;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1760);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS714) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS714 != _M0L4selfS714) {
    return 0;
  } else if (_M0L4selfS714 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS714 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS714;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS700,
  float _M0L4elemS702
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS699;
  int32_t _M0L1iS701;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS699 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS700);
  _M0L1iS701 = 0;
  while (1) {
    if (_M0L1iS701 < _M0L3lenS700) {
      float* _M0L3bufS1754 = _M0L3arrS699->$0;
      int32_t _M0L6_2atmpS1755;
      _M0L3bufS1754[_M0L1iS701] = _M0L4elemS702;
      _M0L6_2atmpS1755 = _M0L1iS701 + 1;
      _M0L1iS701 = _M0L6_2atmpS1755;
      continue;
    }
    break;
  }
  return _M0L3arrS699;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS705,
  int32_t _M0L4elemS707
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS704;
  int32_t _M0L1iS706;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS704 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS705);
  _M0L1iS706 = 0;
  while (1) {
    if (_M0L1iS706 < _M0L3lenS705) {
      uint8_t* _M0L3bufS1756 = _M0L3arrS704->$0;
      int32_t _M0L6_2atmpS1757;
      _M0L3bufS1756[_M0L1iS706] = _M0L4elemS707;
      _M0L6_2atmpS1757 = _M0L1iS706 + 1;
      _M0L1iS706 = _M0L6_2atmpS1757;
      continue;
    }
    break;
  }
  return _M0L3arrS704;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS710,
  int32_t _M0L4elemS712
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS709;
  int32_t _M0L1iS711;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS709 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS710);
  _M0L1iS711 = 0;
  while (1) {
    if (_M0L1iS711 < _M0L3lenS710) {
      int32_t* _M0L3bufS1758 = _M0L3arrS709->$0;
      int32_t _M0L6_2atmpS1759;
      _M0L3bufS1758[_M0L1iS711] = _M0L4elemS712;
      _M0L6_2atmpS1759 = _M0L1iS711 + 1;
      _M0L1iS711 = _M0L6_2atmpS1759;
      continue;
    }
    break;
  }
  return _M0L3arrS709;
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
    float* _M0L6_2atmpS1751;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1751 = _M0MPC15array5Array6bufferGfE(_M0L4selfS688);
    _M0L6_2atmpS1751[_M0L5indexS689] = _M0L5valueS690;
    moonbit_decref_cycle_free(_M0L6_2atmpS1751);
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
    uint8_t* _M0L6_2atmpS1752;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1752 = _M0MPC15array5Array6bufferGbE(_M0L4selfS692);
    _M0L6_2atmpS1752[_M0L5indexS693] = _M0L5valueS694;
    moonbit_decref_cycle_free(_M0L6_2atmpS1752);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS696,
  int32_t _M0L5indexS697,
  int32_t _M0L5valueS698
) {
  int32_t _M0L3lenS695;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS695 = _M0L4selfS696->$1;
  if (_M0L5indexS697 >= 0 && _M0L5indexS697 < _M0L3lenS695) {
    int32_t* _M0L6_2atmpS1753;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1753 = _M0MPC15array5Array6bufferGiE(_M0L4selfS696);
    _M0L6_2atmpS1753[_M0L5indexS697] = _M0L5valueS698;
    moonbit_decref_cycle_free(_M0L6_2atmpS1753);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS676,
  int32_t _M0L5indexS677
) {
  int32_t _M0L3lenS675;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS675 = _M0L4selfS676->$1;
  if (_M0L5indexS677 >= 0 && _M0L5indexS677 < _M0L3lenS675) {
    uint8_t* _M0L6_2atmpS1747;
    int32_t _result_2096;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1747 = _M0MPC15array5Array6bufferGbE(_M0L4selfS676);
    _result_2096 = (int32_t)_M0L6_2atmpS1747[_M0L5indexS677];
    moonbit_decref_cycle_free(_M0L6_2atmpS1747);
    return _result_2096;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS679,
  int32_t _M0L5indexS680
) {
  int32_t _M0L3lenS678;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS678 = _M0L4selfS679->$1;
  if (_M0L5indexS680 >= 0 && _M0L5indexS680 < _M0L3lenS678) {
    float* _M0L6_2atmpS1748;
    float _result_2097;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1748 = _M0MPC15array5Array6bufferGfE(_M0L4selfS679);
    _result_2097 = (float)_M0L6_2atmpS1748[_M0L5indexS680];
    moonbit_decref_cycle_free(_M0L6_2atmpS1748);
    return _result_2097;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS682,
  int32_t _M0L5indexS683
) {
  int32_t _M0L3lenS681;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS681 = _M0L4selfS682->$1;
  if (_M0L5indexS683 >= 0 && _M0L5indexS683 < _M0L3lenS681) {
    int32_t* _M0L6_2atmpS1749;
    int32_t _result_2098;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1749 = _M0MPC15array5Array6bufferGiE(_M0L4selfS682);
    _result_2098 = (int32_t)_M0L6_2atmpS1749[_M0L5indexS683];
    moonbit_decref_cycle_free(_M0L6_2atmpS1749);
    return _result_2098;
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
    moonbit_string_t* _M0L6_2atmpS1750;
    moonbit_string_t _M0L6_2atmpS2026;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1750 = _M0MPC15array5Array6bufferGsE(_M0L4selfS685);
    _M0L6_2atmpS2026 = (moonbit_string_t)_M0L6_2atmpS1750[_M0L5indexS686];
    moonbit_incref_cycle_free(_M0L6_2atmpS2026);
    moonbit_decref_cycle_free(_M0L6_2atmpS1750);
    return _M0L6_2atmpS2026;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS674) {
  moonbit_string_t _M0L6_2atmpS1746;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1746 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS674);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1746);
  moonbit_decref_cycle_free(_M0L6_2atmpS1746);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS673) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS673);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS658) {
  uint64_t _M0L4bitsS661;
  uint64_t _M0L6_2atmpS1745;
  uint64_t _M0L6_2atmpS1744;
  int32_t _M0L8ieeeSignS662;
  uint64_t _M0L12ieeeMantissaS663;
  uint64_t _M0L6_2atmpS1743;
  uint64_t _M0L6_2atmpS1742;
  int32_t _M0L12ieeeExponentS664;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS665;
  struct _M0TPB17FloatingDecimal64* _M0L1vS666;
  moonbit_string_t _result_2100;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS658 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  if (_M0L3valS658 >= -0x1p+53 && _M0L3valS658 <= 0x1p+53) {
    if (_M0L3valS658 >= -0x1p+31 && _M0L3valS658 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS659;
      double _M0L6_2atmpS1731;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS659 = _M0MPC16double6Double7to__int(_M0L3valS658);
      _M0L6_2atmpS1731 = (double)_M0L1iS659;
      if (_M0L6_2atmpS1731 == _M0L3valS658) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS659, 10);
      }
    } else {
      int64_t _M0L1iS660;
      double _M0L6_2atmpS1732;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS660 = _M0MPC16double6Double9to__int64(_M0L3valS658);
      _M0L6_2atmpS1732 = (double)_M0L1iS660;
      if (_M0L6_2atmpS1732 == _M0L3valS658) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS660, 10);
      }
    }
  }
  _M0L4bitsS661 = *(int64_t*)&_M0L3valS658;
  _M0L6_2atmpS1745 = _M0L4bitsS661 >> 63;
  _M0L6_2atmpS1744 = _M0L6_2atmpS1745 & 1ull;
  _M0L8ieeeSignS662 = _M0L6_2atmpS1744 != 0ull;
  _M0L12ieeeMantissaS663 = _M0L4bitsS661 & 4503599627370495ull;
  _M0L6_2atmpS1743 = _M0L4bitsS661 >> 52;
  _M0L6_2atmpS1742 = _M0L6_2atmpS1743 & 2047ull;
  _M0L12ieeeExponentS664 = (int32_t)_M0L6_2atmpS1742;
  if (
    _M0L12ieeeExponentS664 == 2047
    || _M0L12ieeeExponentS664 == 0 && _M0L12ieeeMantissaS663 == 0ull
  ) {
    int32_t _M0L6_2atmpS1733 = _M0L12ieeeExponentS664 != 0;
    int32_t _M0L6_2atmpS1734 = _M0L12ieeeMantissaS663 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS662, _M0L6_2atmpS1733, _M0L6_2atmpS1734);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS665
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS663, _M0L12ieeeExponentS664);
  if (_M0L7_2abindS665 == 0) {
    uint32_t _M0L6_2atmpS1735;
    if (_M0L7_2abindS665) {
      moonbit_decref_cycle_free(_M0L7_2abindS665);
    }
    _M0L6_2atmpS1735 = *(uint32_t*)&_M0L12ieeeExponentS664;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS666 = _M0FPB3d2d(_M0L12ieeeMantissaS663, _M0L6_2atmpS1735);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS667 = _M0L7_2abindS665;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS668 = _M0L7_2aSomeS667;
    struct _M0TPB17FloatingDecimal64* _M0L1xS669 = _M0L4_2afS668;
    while (1) {
      uint64_t _M0L8mantissaS1741 = _M0L1xS669->$0;
      uint64_t _M0L1qS670 = _M0L8mantissaS1741 / 10ull;
      uint64_t _M0L8mantissaS1739 = _M0L1xS669->$0;
      uint64_t _M0L6_2atmpS1740 = 10ull * _M0L1qS670;
      uint64_t _M0L1rS671 = _M0L8mantissaS1739 - _M0L6_2atmpS1740;
      int32_t _M0L8exponentS1738;
      int32_t _M0L6_2atmpS1737;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1736;
      if (_M0L1rS671 != 0ull) {
        _M0L1vS666 = _M0L1xS669;
        break;
      }
      _M0L8exponentS1738 = _M0L1xS669->$1;
      moonbit_decref_cycle_free(_M0L1xS669);
      _M0L6_2atmpS1737 = _M0L8exponentS1738 + 1;
      _M0L6_2atmpS1736
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1736)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1736->$0 = _M0L1qS670;
      _M0L6_2atmpS1736->$1 = _M0L6_2atmpS1737;
      _M0L1xS669 = _M0L6_2atmpS1736;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2100 = _M0FPB9to__chars(_M0L1vS666, _M0L8ieeeSignS662);
  moonbit_decref_cycle_free(_M0L1vS666);
  return _result_2100;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS653,
  int32_t _M0L12ieeeExponentS655
) {
  uint64_t _M0L2m2S652;
  int32_t _M0L6_2atmpS1730;
  int32_t _M0L2e2S654;
  int32_t _M0L6_2atmpS1729;
  uint64_t _M0L6_2atmpS1728;
  uint64_t _M0L4maskS656;
  uint64_t _M0L8fractionS657;
  int32_t _M0L6_2atmpS1727;
  uint64_t _M0L6_2atmpS1726;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1725;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S652 = 4503599627370496ull | _M0L12ieeeMantissaS653;
  _M0L6_2atmpS1730 = _M0L12ieeeExponentS655 - 1023;
  _M0L2e2S654 = _M0L6_2atmpS1730 - 52;
  if (_M0L2e2S654 > 0) {
    return 0;
  }
  if (_M0L2e2S654 < -52) {
    return 0;
  }
  _M0L6_2atmpS1729 = -_M0L2e2S654;
  _M0L6_2atmpS1728 = 1ull << (_M0L6_2atmpS1729 & 63);
  _M0L4maskS656 = _M0L6_2atmpS1728 - 1ull;
  _M0L8fractionS657 = _M0L2m2S652 & _M0L4maskS656;
  if (_M0L8fractionS657 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1727 = -_M0L2e2S654;
  _M0L6_2atmpS1726 = _M0L2m2S652 >> (_M0L6_2atmpS1727 & 63);
  _M0L6_2atmpS1725
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1725)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1725->$0 = _M0L6_2atmpS1726;
  _M0L6_2atmpS1725->$1 = 0;
  return _M0L6_2atmpS1725;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS620,
  int32_t _M0L4signS618
) {
  moonbit_bytes_t _M0L6resultS616;
  int32_t _M0Lm5indexS617;
  uint64_t _M0L6outputS619;
  int32_t _M0L7olengthS621;
  int32_t _M0L8exponentS1724;
  int32_t _M0L6_2atmpS1723;
  int32_t _M0Lm3expS622;
  int32_t _M0L6_2atmpS1722;
  int32_t _M0L6_2atmpS1720;
  int32_t _M0L18scientificNotationS623;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS616 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS617 = 0;
  if (_M0L4signS618) {
    int32_t _M0L6_2atmpS1594 = _M0Lm5indexS617;
    int32_t _M0L6_2atmpS1595;
    if (
      _M0L6_2atmpS1594 < 0
      || _M0L6_2atmpS1594 >= Moonbit_array_length(_M0L6resultS616)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS616[_M0L6_2atmpS1594] = 45;
    _M0L6_2atmpS1595 = _M0Lm5indexS617;
    _M0Lm5indexS617 = _M0L6_2atmpS1595 + 1;
  }
  _M0L6outputS619 = _M0L1vS620->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS621 = _M0FPB17decimal__length17(_M0L6outputS619);
  _M0L8exponentS1724 = _M0L1vS620->$1;
  _M0L6_2atmpS1723 = _M0L8exponentS1724 + _M0L7olengthS621;
  _M0Lm3expS622 = _M0L6_2atmpS1723 - 1;
  _M0L6_2atmpS1722 = _M0Lm3expS622;
  if (_M0L6_2atmpS1722 >= -6) {
    int32_t _M0L6_2atmpS1721 = _M0Lm3expS622;
    _M0L6_2atmpS1720 = _M0L6_2atmpS1721 < 21;
  } else {
    _M0L6_2atmpS1720 = 0;
  }
  _M0L18scientificNotationS623 = !_M0L6_2atmpS1720;
  if (_M0L18scientificNotationS623) {
    int32_t _M0L7_2abindS624 = _M0L7olengthS621 - 1;
    uint64_t _M0L6outputS625;
    int32_t _M0L1iS626 = 0;
    uint64_t _M0L6outputS627 = _M0L6outputS619;
    int32_t _M0L6_2atmpS1596;
    int32_t _M0L6_2atmpS1600;
    int32_t _M0L6_2atmpS1599;
    int32_t _M0L6_2atmpS1598;
    int32_t _M0L6_2atmpS1597;
    int32_t _M0L6_2atmpS1604;
    int32_t _M0L6_2atmpS1605;
    int32_t _M0L6_2atmpS1606;
    int32_t _M0L6_2atmpS1607;
    int32_t _M0L6_2atmpS1608;
    int32_t _M0L6_2atmpS1614;
    int32_t _M0L6_2atmpS1647;
    moonbit_string_t _result_2102;
    while (1) {
      if (_M0L1iS626 < _M0L7_2abindS624) {
        uint64_t _M0L1cS628 = _M0L6outputS627 % 10ull;
        int32_t _M0L6_2atmpS1653 = _M0Lm5indexS617;
        int32_t _M0L6_2atmpS1652 = _M0L6_2atmpS1653 + _M0L7olengthS621;
        int32_t _M0L6_2atmpS1648 = _M0L6_2atmpS1652 - _M0L1iS626;
        int32_t _M0L6_2atmpS1651 = (int32_t)_M0L1cS628;
        int32_t _M0L6_2atmpS1650 = 48 + _M0L6_2atmpS1651;
        int32_t _M0L6_2atmpS1649 = _M0L6_2atmpS1650 & 0xff;
        int32_t _M0L6_2atmpS1654;
        uint64_t _M0L6_2atmpS1655;
        if (
          _M0L6_2atmpS1648 < 0
          || _M0L6_2atmpS1648 >= Moonbit_array_length(_M0L6resultS616)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS616[_M0L6_2atmpS1648] = _M0L6_2atmpS1649;
        _M0L6_2atmpS1654 = _M0L1iS626 + 1;
        _M0L6_2atmpS1655 = _M0L6outputS627 / 10ull;
        _M0L1iS626 = _M0L6_2atmpS1654;
        _M0L6outputS627 = _M0L6_2atmpS1655;
        continue;
      } else {
        _M0L6outputS625 = _M0L6outputS627;
      }
      break;
    }
    _M0L6_2atmpS1596 = _M0Lm5indexS617;
    _M0L6_2atmpS1600 = (int32_t)_M0L6outputS625;
    _M0L6_2atmpS1599 = _M0L6_2atmpS1600 % 10;
    _M0L6_2atmpS1598 = 48 + _M0L6_2atmpS1599;
    _M0L6_2atmpS1597 = _M0L6_2atmpS1598 & 0xff;
    if (
      _M0L6_2atmpS1596 < 0
      || _M0L6_2atmpS1596 >= Moonbit_array_length(_M0L6resultS616)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS616[_M0L6_2atmpS1596] = _M0L6_2atmpS1597;
    if (_M0L7olengthS621 > 1) {
      int32_t _M0L6_2atmpS1602 = _M0Lm5indexS617;
      int32_t _M0L6_2atmpS1601 = _M0L6_2atmpS1602 + 1;
      if (
        _M0L6_2atmpS1601 < 0
        || _M0L6_2atmpS1601 >= Moonbit_array_length(_M0L6resultS616)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS616[_M0L6_2atmpS1601] = 46;
    } else {
      int32_t _M0L6_2atmpS1603 = _M0Lm5indexS617;
      _M0Lm5indexS617 = _M0L6_2atmpS1603 - 1;
    }
    _M0L6_2atmpS1604 = _M0Lm5indexS617;
    _M0L6_2atmpS1605 = _M0L7olengthS621 + 1;
    _M0Lm5indexS617 = _M0L6_2atmpS1604 + _M0L6_2atmpS1605;
    _M0L6_2atmpS1606 = _M0Lm5indexS617;
    if (
      _M0L6_2atmpS1606 < 0
      || _M0L6_2atmpS1606 >= Moonbit_array_length(_M0L6resultS616)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS616[_M0L6_2atmpS1606] = 101;
    _M0L6_2atmpS1607 = _M0Lm5indexS617;
    _M0Lm5indexS617 = _M0L6_2atmpS1607 + 1;
    _M0L6_2atmpS1608 = _M0Lm3expS622;
    if (_M0L6_2atmpS1608 < 0) {
      int32_t _M0L6_2atmpS1609 = _M0Lm5indexS617;
      int32_t _M0L6_2atmpS1610;
      int32_t _M0L6_2atmpS1611;
      if (
        _M0L6_2atmpS1609 < 0
        || _M0L6_2atmpS1609 >= Moonbit_array_length(_M0L6resultS616)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS616[_M0L6_2atmpS1609] = 45;
      _M0L6_2atmpS1610 = _M0Lm5indexS617;
      _M0Lm5indexS617 = _M0L6_2atmpS1610 + 1;
      _M0L6_2atmpS1611 = _M0Lm3expS622;
      _M0Lm3expS622 = -_M0L6_2atmpS1611;
    } else {
      int32_t _M0L6_2atmpS1612 = _M0Lm5indexS617;
      int32_t _M0L6_2atmpS1613;
      if (
        _M0L6_2atmpS1612 < 0
        || _M0L6_2atmpS1612 >= Moonbit_array_length(_M0L6resultS616)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS616[_M0L6_2atmpS1612] = 43;
      _M0L6_2atmpS1613 = _M0Lm5indexS617;
      _M0Lm5indexS617 = _M0L6_2atmpS1613 + 1;
    }
    _M0L6_2atmpS1614 = _M0Lm3expS622;
    if (_M0L6_2atmpS1614 >= 100) {
      int32_t _M0L6_2atmpS1630 = _M0Lm3expS622;
      int32_t _M0L1aS630 = _M0L6_2atmpS1630 / 100;
      int32_t _M0L6_2atmpS1629 = _M0Lm3expS622;
      int32_t _M0L6_2atmpS1628 = _M0L6_2atmpS1629 / 10;
      int32_t _M0L1bS631 = _M0L6_2atmpS1628 % 10;
      int32_t _M0L6_2atmpS1627 = _M0Lm3expS622;
      int32_t _M0L1cS632 = _M0L6_2atmpS1627 % 10;
      int32_t _M0L6_2atmpS1615 = _M0Lm5indexS617;
      int32_t _M0L6_2atmpS1617 = 48 + _M0L1aS630;
      int32_t _M0L6_2atmpS1616 = _M0L6_2atmpS1617 & 0xff;
      int32_t _M0L6_2atmpS1621;
      int32_t _M0L6_2atmpS1618;
      int32_t _M0L6_2atmpS1620;
      int32_t _M0L6_2atmpS1619;
      int32_t _M0L6_2atmpS1625;
      int32_t _M0L6_2atmpS1622;
      int32_t _M0L6_2atmpS1624;
      int32_t _M0L6_2atmpS1623;
      int32_t _M0L6_2atmpS1626;
      if (
        _M0L6_2atmpS1615 < 0
        || _M0L6_2atmpS1615 >= Moonbit_array_length(_M0L6resultS616)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS616[_M0L6_2atmpS1615] = _M0L6_2atmpS1616;
      _M0L6_2atmpS1621 = _M0Lm5indexS617;
      _M0L6_2atmpS1618 = _M0L6_2atmpS1621 + 1;
      _M0L6_2atmpS1620 = 48 + _M0L1bS631;
      _M0L6_2atmpS1619 = _M0L6_2atmpS1620 & 0xff;
      if (
        _M0L6_2atmpS1618 < 0
        || _M0L6_2atmpS1618 >= Moonbit_array_length(_M0L6resultS616)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS616[_M0L6_2atmpS1618] = _M0L6_2atmpS1619;
      _M0L6_2atmpS1625 = _M0Lm5indexS617;
      _M0L6_2atmpS1622 = _M0L6_2atmpS1625 + 2;
      _M0L6_2atmpS1624 = 48 + _M0L1cS632;
      _M0L6_2atmpS1623 = _M0L6_2atmpS1624 & 0xff;
      if (
        _M0L6_2atmpS1622 < 0
        || _M0L6_2atmpS1622 >= Moonbit_array_length(_M0L6resultS616)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS616[_M0L6_2atmpS1622] = _M0L6_2atmpS1623;
      _M0L6_2atmpS1626 = _M0Lm5indexS617;
      _M0Lm5indexS617 = _M0L6_2atmpS1626 + 3;
    } else {
      int32_t _M0L6_2atmpS1631 = _M0Lm3expS622;
      if (_M0L6_2atmpS1631 >= 10) {
        int32_t _M0L6_2atmpS1641 = _M0Lm3expS622;
        int32_t _M0L1aS633 = _M0L6_2atmpS1641 / 10;
        int32_t _M0L6_2atmpS1640 = _M0Lm3expS622;
        int32_t _M0L1bS634 = _M0L6_2atmpS1640 % 10;
        int32_t _M0L6_2atmpS1632 = _M0Lm5indexS617;
        int32_t _M0L6_2atmpS1634 = 48 + _M0L1aS633;
        int32_t _M0L6_2atmpS1633 = _M0L6_2atmpS1634 & 0xff;
        int32_t _M0L6_2atmpS1638;
        int32_t _M0L6_2atmpS1635;
        int32_t _M0L6_2atmpS1637;
        int32_t _M0L6_2atmpS1636;
        int32_t _M0L6_2atmpS1639;
        if (
          _M0L6_2atmpS1632 < 0
          || _M0L6_2atmpS1632 >= Moonbit_array_length(_M0L6resultS616)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS616[_M0L6_2atmpS1632] = _M0L6_2atmpS1633;
        _M0L6_2atmpS1638 = _M0Lm5indexS617;
        _M0L6_2atmpS1635 = _M0L6_2atmpS1638 + 1;
        _M0L6_2atmpS1637 = 48 + _M0L1bS634;
        _M0L6_2atmpS1636 = _M0L6_2atmpS1637 & 0xff;
        if (
          _M0L6_2atmpS1635 < 0
          || _M0L6_2atmpS1635 >= Moonbit_array_length(_M0L6resultS616)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS616[_M0L6_2atmpS1635] = _M0L6_2atmpS1636;
        _M0L6_2atmpS1639 = _M0Lm5indexS617;
        _M0Lm5indexS617 = _M0L6_2atmpS1639 + 2;
      } else {
        int32_t _M0L6_2atmpS1642 = _M0Lm5indexS617;
        int32_t _M0L6_2atmpS1645 = _M0Lm3expS622;
        int32_t _M0L6_2atmpS1644 = 48 + _M0L6_2atmpS1645;
        int32_t _M0L6_2atmpS1643 = _M0L6_2atmpS1644 & 0xff;
        int32_t _M0L6_2atmpS1646;
        if (
          _M0L6_2atmpS1642 < 0
          || _M0L6_2atmpS1642 >= Moonbit_array_length(_M0L6resultS616)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS616[_M0L6_2atmpS1642] = _M0L6_2atmpS1643;
        _M0L6_2atmpS1646 = _M0Lm5indexS617;
        _M0Lm5indexS617 = _M0L6_2atmpS1646 + 1;
      }
    }
    _M0L6_2atmpS1647 = _M0Lm5indexS617;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2102
    = _M0FPB19string__from__bytes(_M0L6resultS616, 0, _M0L6_2atmpS1647);
    moonbit_decref_cycle_free(_M0L6resultS616);
    return _result_2102;
  } else {
    int32_t _M0L6_2atmpS1656 = _M0Lm3expS622;
    int32_t _M0L6_2atmpS1719;
    moonbit_string_t _result_2108;
    if (_M0L6_2atmpS1656 < 0) {
      int32_t _M0L6_2atmpS1657 = _M0Lm5indexS617;
      int32_t _M0L6_2atmpS1659;
      int32_t _M0L6_2atmpS1658;
      int32_t _M0L6_2atmpS1660;
      int32_t _M0L1iS635;
      int32_t _M0L6_2atmpS1675;
      int32_t _M0L6_2atmpS1677;
      int32_t _M0L6_2atmpS1676;
      int32_t _M0L7currentS637;
      int32_t _M0L1iS638;
      uint64_t _M0L6outputS639;
      if (
        _M0L6_2atmpS1657 < 0
        || _M0L6_2atmpS1657 >= Moonbit_array_length(_M0L6resultS616)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS616[_M0L6_2atmpS1657] = 48;
      _M0L6_2atmpS1659 = _M0Lm5indexS617;
      _M0L6_2atmpS1658 = _M0L6_2atmpS1659 + 1;
      if (
        _M0L6_2atmpS1658 < 0
        || _M0L6_2atmpS1658 >= Moonbit_array_length(_M0L6resultS616)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS616[_M0L6_2atmpS1658] = 46;
      _M0L6_2atmpS1660 = _M0Lm5indexS617;
      _M0Lm5indexS617 = _M0L6_2atmpS1660 + 2;
      _M0L1iS635 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1661 = _M0Lm3expS622;
        if (_M0L1iS635 > _M0L6_2atmpS1661) {
          int32_t _M0L6_2atmpS1664 = _M0Lm5indexS617;
          int32_t _M0L6_2atmpS1663 = _M0L6_2atmpS1664 - _M0L1iS635;
          int32_t _M0L6_2atmpS1662 = _M0L6_2atmpS1663 - 1;
          int32_t _M0L6_2atmpS1665;
          if (
            _M0L6_2atmpS1662 < 0
            || _M0L6_2atmpS1662 >= Moonbit_array_length(_M0L6resultS616)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS616[_M0L6_2atmpS1662] = 48;
          _M0L6_2atmpS1665 = _M0L1iS635 - 1;
          _M0L1iS635 = _M0L6_2atmpS1665;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1675 = _M0Lm5indexS617;
      _M0L6_2atmpS1677 = _M0Lm3expS622;
      _M0L6_2atmpS1676 = -1 - _M0L6_2atmpS1677;
      _M0L7currentS637 = _M0L6_2atmpS1675 + _M0L6_2atmpS1676;
      _M0L1iS638 = 0;
      _M0L6outputS639 = _M0L6outputS619;
      while (1) {
        if (_M0L1iS638 < _M0L7olengthS621) {
          int32_t _M0L6_2atmpS1672 = _M0L7currentS637 + _M0L7olengthS621;
          int32_t _M0L6_2atmpS1671 = _M0L6_2atmpS1672 - _M0L1iS638;
          int32_t _M0L6_2atmpS1666 = _M0L6_2atmpS1671 - 1;
          uint64_t _M0L6_2atmpS1670 = _M0L6outputS639 % 10ull;
          int32_t _M0L6_2atmpS1669 = (int32_t)_M0L6_2atmpS1670;
          int32_t _M0L6_2atmpS1668 = 48 + _M0L6_2atmpS1669;
          int32_t _M0L6_2atmpS1667 = _M0L6_2atmpS1668 & 0xff;
          int32_t _M0L6_2atmpS1673;
          uint64_t _M0L6_2atmpS1674;
          if (
            _M0L6_2atmpS1666 < 0
            || _M0L6_2atmpS1666 >= Moonbit_array_length(_M0L6resultS616)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS616[_M0L6_2atmpS1666] = _M0L6_2atmpS1667;
          _M0L6_2atmpS1673 = _M0L1iS638 + 1;
          _M0L6_2atmpS1674 = _M0L6outputS639 / 10ull;
          _M0L1iS638 = _M0L6_2atmpS1673;
          _M0L6outputS639 = _M0L6_2atmpS1674;
          continue;
        }
        break;
      }
      _M0Lm5indexS617 = _M0L7currentS637 + _M0L7olengthS621;
    } else {
      int32_t _M0L6_2atmpS1679 = _M0Lm3expS622;
      int32_t _M0L6_2atmpS1678 = _M0L6_2atmpS1679 + 1;
      if (_M0L6_2atmpS1678 >= _M0L7olengthS621) {
        int32_t _M0L1iS641 = 0;
        uint64_t _M0L6outputS642 = _M0L6outputS619;
        int32_t _M0L6_2atmpS1690;
        int32_t _M0L6_2atmpS1695;
        int32_t _M0L7_2abindS644;
        int32_t _M0L1iS645;
        int32_t _M0L6_2atmpS1696;
        int32_t _M0L6_2atmpS1699;
        int32_t _M0L6_2atmpS1698;
        int32_t _M0L6_2atmpS1697;
        while (1) {
          if (_M0L1iS641 < _M0L7olengthS621) {
            int32_t _M0L6_2atmpS1687 = _M0Lm5indexS617;
            int32_t _M0L6_2atmpS1686 = _M0L6_2atmpS1687 + _M0L7olengthS621;
            int32_t _M0L6_2atmpS1685 = _M0L6_2atmpS1686 - _M0L1iS641;
            int32_t _M0L6_2atmpS1680 = _M0L6_2atmpS1685 - 1;
            uint64_t _M0L6_2atmpS1684 = _M0L6outputS642 % 10ull;
            int32_t _M0L6_2atmpS1683 = (int32_t)_M0L6_2atmpS1684;
            int32_t _M0L6_2atmpS1682 = 48 + _M0L6_2atmpS1683;
            int32_t _M0L6_2atmpS1681 = _M0L6_2atmpS1682 & 0xff;
            int32_t _M0L6_2atmpS1688;
            uint64_t _M0L6_2atmpS1689;
            if (
              _M0L6_2atmpS1680 < 0
              || _M0L6_2atmpS1680 >= Moonbit_array_length(_M0L6resultS616)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS616[_M0L6_2atmpS1680] = _M0L6_2atmpS1681;
            _M0L6_2atmpS1688 = _M0L1iS641 + 1;
            _M0L6_2atmpS1689 = _M0L6outputS642 / 10ull;
            _M0L1iS641 = _M0L6_2atmpS1688;
            _M0L6outputS642 = _M0L6_2atmpS1689;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1690 = _M0Lm5indexS617;
        _M0Lm5indexS617 = _M0L6_2atmpS1690 + _M0L7olengthS621;
        _M0L6_2atmpS1695 = _M0Lm3expS622;
        _M0L7_2abindS644 = _M0L6_2atmpS1695 + 1;
        _M0L1iS645 = _M0L7olengthS621;
        while (1) {
          if (_M0L1iS645 < _M0L7_2abindS644) {
            int32_t _M0L6_2atmpS1693 = _M0Lm5indexS617;
            int32_t _M0L6_2atmpS1692 = _M0L6_2atmpS1693 + _M0L1iS645;
            int32_t _M0L6_2atmpS1691 = _M0L6_2atmpS1692 - _M0L7olengthS621;
            int32_t _M0L6_2atmpS1694;
            if (
              _M0L6_2atmpS1691 < 0
              || _M0L6_2atmpS1691 >= Moonbit_array_length(_M0L6resultS616)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS616[_M0L6_2atmpS1691] = 48;
            _M0L6_2atmpS1694 = _M0L1iS645 + 1;
            _M0L1iS645 = _M0L6_2atmpS1694;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1696 = _M0Lm5indexS617;
        _M0L6_2atmpS1699 = _M0Lm3expS622;
        _M0L6_2atmpS1698 = _M0L6_2atmpS1699 + 1;
        _M0L6_2atmpS1697 = _M0L6_2atmpS1698 - _M0L7olengthS621;
        _M0Lm5indexS617 = _M0L6_2atmpS1696 + _M0L6_2atmpS1697;
      } else {
        int32_t _M0L6_2atmpS1716 = _M0Lm5indexS617;
        int32_t _M0L6_2atmpS1715 = _M0L6_2atmpS1716 + 1;
        int32_t _M0L1iS647 = 0;
        int32_t _M0L7currentS648 = _M0L6_2atmpS1715;
        uint64_t _M0L6outputS649 = _M0L6outputS619;
        int32_t _M0L6_2atmpS1717;
        int32_t _M0L6_2atmpS1718;
        while (1) {
          if (_M0L1iS647 < _M0L7olengthS621) {
            int32_t _M0L6_2atmpS1711 = _M0L7olengthS621 - _M0L1iS647;
            int32_t _M0L6_2atmpS1709 = _M0L6_2atmpS1711 - 1;
            int32_t _M0L6_2atmpS1710 = _M0Lm3expS622;
            int32_t _M0L7currentS650;
            int32_t _M0L6_2atmpS1706;
            int32_t _M0L6_2atmpS1705;
            int32_t _M0L6_2atmpS1700;
            uint64_t _M0L6_2atmpS1704;
            int32_t _M0L6_2atmpS1703;
            int32_t _M0L6_2atmpS1702;
            int32_t _M0L6_2atmpS1701;
            int32_t _M0L6_2atmpS1707;
            uint64_t _M0L6_2atmpS1708;
            if (_M0L6_2atmpS1709 == _M0L6_2atmpS1710) {
              int32_t _M0L6_2atmpS1714 = _M0L7currentS648 + _M0L7olengthS621;
              int32_t _M0L6_2atmpS1713 = _M0L6_2atmpS1714 - _M0L1iS647;
              int32_t _M0L6_2atmpS1712 = _M0L6_2atmpS1713 - 1;
              if (
                _M0L6_2atmpS1712 < 0
                || _M0L6_2atmpS1712 >= Moonbit_array_length(_M0L6resultS616)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS616[_M0L6_2atmpS1712] = 46;
              _M0L7currentS650 = _M0L7currentS648 - 1;
            } else {
              _M0L7currentS650 = _M0L7currentS648;
            }
            _M0L6_2atmpS1706 = _M0L7currentS650 + _M0L7olengthS621;
            _M0L6_2atmpS1705 = _M0L6_2atmpS1706 - _M0L1iS647;
            _M0L6_2atmpS1700 = _M0L6_2atmpS1705 - 1;
            _M0L6_2atmpS1704 = _M0L6outputS649 % 10ull;
            _M0L6_2atmpS1703 = (int32_t)_M0L6_2atmpS1704;
            _M0L6_2atmpS1702 = 48 + _M0L6_2atmpS1703;
            _M0L6_2atmpS1701 = _M0L6_2atmpS1702 & 0xff;
            if (
              _M0L6_2atmpS1700 < 0
              || _M0L6_2atmpS1700 >= Moonbit_array_length(_M0L6resultS616)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS616[_M0L6_2atmpS1700] = _M0L6_2atmpS1701;
            _M0L6_2atmpS1707 = _M0L1iS647 + 1;
            _M0L6_2atmpS1708 = _M0L6outputS649 / 10ull;
            _M0L1iS647 = _M0L6_2atmpS1707;
            _M0L7currentS648 = _M0L7currentS650;
            _M0L6outputS649 = _M0L6_2atmpS1708;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1717 = _M0Lm5indexS617;
        _M0L6_2atmpS1718 = _M0L7olengthS621 + 1;
        _M0Lm5indexS617 = _M0L6_2atmpS1717 + _M0L6_2atmpS1718;
      }
    }
    _M0L6_2atmpS1719 = _M0Lm5indexS617;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2108
    = _M0FPB19string__from__bytes(_M0L6resultS616, 0, _M0L6_2atmpS1719);
    moonbit_decref_cycle_free(_M0L6resultS616);
    return _result_2108;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS562,
  uint32_t _M0L12ieeeExponentS561
) {
  int32_t _M0Lm2e2S559;
  uint64_t _M0Lm2m2S560;
  uint64_t _M0L6_2atmpS1593;
  uint64_t _M0L6_2atmpS1592;
  int32_t _M0L4evenS563;
  uint64_t _M0L6_2atmpS1591;
  uint64_t _M0L2mvS564;
  int32_t _M0L7mmShiftS565;
  uint64_t _M0Lm2vrS566;
  uint64_t _M0Lm2vpS567;
  uint64_t _M0Lm2vmS568;
  int32_t _M0Lm3e10S569;
  int32_t _M0Lm17vmIsTrailingZerosS570;
  int32_t _M0Lm17vrIsTrailingZerosS571;
  int32_t _M0L6_2atmpS1493;
  int32_t _M0Lm7removedS590;
  int32_t _M0Lm16lastRemovedDigitS591;
  uint64_t _M0Lm6outputS592;
  int32_t _M0L6_2atmpS1589;
  int32_t _M0L6_2atmpS1590;
  int32_t _M0L3expS615;
  uint64_t _M0L6_2atmpS1588;
  struct _M0TPB17FloatingDecimal64* _block_2114;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S559 = 0;
  _M0Lm2m2S560 = 0ull;
  if (_M0L12ieeeExponentS561 == 0u) {
    _M0Lm2e2S559 = -1076;
    _M0Lm2m2S560 = _M0L12ieeeMantissaS562;
  } else {
    int32_t _M0L6_2atmpS1492 = *(int32_t*)&_M0L12ieeeExponentS561;
    int32_t _M0L6_2atmpS1491 = _M0L6_2atmpS1492 - 1023;
    int32_t _M0L6_2atmpS1490 = _M0L6_2atmpS1491 - 52;
    _M0Lm2e2S559 = _M0L6_2atmpS1490 - 2;
    _M0Lm2m2S560 = 4503599627370496ull | _M0L12ieeeMantissaS562;
  }
  _M0L6_2atmpS1593 = _M0Lm2m2S560;
  _M0L6_2atmpS1592 = _M0L6_2atmpS1593 & 1ull;
  _M0L4evenS563 = _M0L6_2atmpS1592 == 0ull;
  _M0L6_2atmpS1591 = _M0Lm2m2S560;
  _M0L2mvS564 = 4ull * _M0L6_2atmpS1591;
  _M0L7mmShiftS565
  = _M0L12ieeeMantissaS562 != 0ull || _M0L12ieeeExponentS561 <= 1u;
  _M0Lm2vrS566 = 0ull;
  _M0Lm2vpS567 = 0ull;
  _M0Lm2vmS568 = 0ull;
  _M0Lm3e10S569 = 0;
  _M0Lm17vmIsTrailingZerosS570 = 0;
  _M0Lm17vrIsTrailingZerosS571 = 0;
  _M0L6_2atmpS1493 = _M0Lm2e2S559;
  if (_M0L6_2atmpS1493 >= 0) {
    int32_t _M0L6_2atmpS1515 = _M0Lm2e2S559;
    int32_t _M0L6_2atmpS1511;
    int32_t _M0L6_2atmpS1514;
    int32_t _M0L6_2atmpS1513;
    int32_t _M0L6_2atmpS1512;
    int32_t _M0L1qS572;
    int32_t _M0L6_2atmpS1510;
    int32_t _M0L6_2atmpS1509;
    int32_t _M0L1kS573;
    int32_t _M0L6_2atmpS1508;
    int32_t _M0L6_2atmpS1507;
    int32_t _M0L6_2atmpS1506;
    int32_t _M0L1iS574;
    struct _M0TPB8Pow5Pair _M0L4pow5S575;
    uint64_t _M0L6_2atmpS1505;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS576;
    uint64_t _M0L8_2avrOutS577;
    uint64_t _M0L8_2avpOutS578;
    uint64_t _M0L8_2avmOutS579;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1511 = _M0FPB9log10Pow2(_M0L6_2atmpS1515);
    _M0L6_2atmpS1514 = _M0Lm2e2S559;
    _M0L6_2atmpS1513 = _M0L6_2atmpS1514 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1512 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1513);
    _M0L1qS572 = _M0L6_2atmpS1511 - _M0L6_2atmpS1512;
    _M0Lm3e10S569 = _M0L1qS572;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1510 = _M0FPB8pow5bits(_M0L1qS572);
    _M0L6_2atmpS1509 = 125 + _M0L6_2atmpS1510;
    _M0L1kS573 = _M0L6_2atmpS1509 - 1;
    _M0L6_2atmpS1508 = _M0Lm2e2S559;
    _M0L6_2atmpS1507 = -_M0L6_2atmpS1508;
    _M0L6_2atmpS1506 = _M0L6_2atmpS1507 + _M0L1qS572;
    _M0L1iS574 = _M0L6_2atmpS1506 + _M0L1kS573;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S575 = _M0FPB22double__computeInvPow5(_M0L1qS572);
    _M0L6_2atmpS1505 = _M0Lm2m2S560;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS576
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1505, _M0L4pow5S575, _M0L1iS574, _M0L7mmShiftS565);
    _M0L8_2avrOutS577 = _M0L7_2abindS576.$0;
    _M0L8_2avpOutS578 = _M0L7_2abindS576.$1;
    _M0L8_2avmOutS579 = _M0L7_2abindS576.$2;
    _M0Lm2vrS566 = _M0L8_2avrOutS577;
    _M0Lm2vpS567 = _M0L8_2avpOutS578;
    _M0Lm2vmS568 = _M0L8_2avmOutS579;
    if (_M0L1qS572 <= 21) {
      int32_t _M0L6_2atmpS1501 = (int32_t)_M0L2mvS564;
      uint64_t _M0L6_2atmpS1504 = _M0L2mvS564 / 5ull;
      int32_t _M0L6_2atmpS1503 = (int32_t)_M0L6_2atmpS1504;
      int32_t _M0L6_2atmpS1502 = 5 * _M0L6_2atmpS1503;
      int32_t _M0L6mvMod5S580 = _M0L6_2atmpS1501 - _M0L6_2atmpS1502;
      if (_M0L6mvMod5S580 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS571
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS564, _M0L1qS572);
      } else if (_M0L4evenS563) {
        uint64_t _M0L6_2atmpS1495 = _M0L2mvS564 - 1ull;
        uint64_t _M0L6_2atmpS1496;
        uint64_t _M0L6_2atmpS1494;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1496 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS565);
        _M0L6_2atmpS1494 = _M0L6_2atmpS1495 - _M0L6_2atmpS1496;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS570
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1494, _M0L1qS572);
      } else {
        uint64_t _M0L6_2atmpS1497 = _M0Lm2vpS567;
        uint64_t _M0L6_2atmpS1500 = _M0L2mvS564 + 2ull;
        int32_t _M0L6_2atmpS1499;
        uint64_t _M0L6_2atmpS1498;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1499
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1500, _M0L1qS572);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1498 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1499);
        _M0Lm2vpS567 = _M0L6_2atmpS1497 - _M0L6_2atmpS1498;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1529 = _M0Lm2e2S559;
    int32_t _M0L6_2atmpS1528 = -_M0L6_2atmpS1529;
    int32_t _M0L6_2atmpS1523;
    int32_t _M0L6_2atmpS1527;
    int32_t _M0L6_2atmpS1526;
    int32_t _M0L6_2atmpS1525;
    int32_t _M0L6_2atmpS1524;
    int32_t _M0L1qS581;
    int32_t _M0L6_2atmpS1516;
    int32_t _M0L6_2atmpS1522;
    int32_t _M0L6_2atmpS1521;
    int32_t _M0L1iS582;
    int32_t _M0L6_2atmpS1520;
    int32_t _M0L1kS583;
    int32_t _M0L1jS584;
    struct _M0TPB8Pow5Pair _M0L4pow5S585;
    uint64_t _M0L6_2atmpS1519;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS586;
    uint64_t _M0L8_2avrOutS587;
    uint64_t _M0L8_2avpOutS588;
    uint64_t _M0L8_2avmOutS589;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1523 = _M0FPB9log10Pow5(_M0L6_2atmpS1528);
    _M0L6_2atmpS1527 = _M0Lm2e2S559;
    _M0L6_2atmpS1526 = -_M0L6_2atmpS1527;
    _M0L6_2atmpS1525 = _M0L6_2atmpS1526 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1524 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1525);
    _M0L1qS581 = _M0L6_2atmpS1523 - _M0L6_2atmpS1524;
    _M0L6_2atmpS1516 = _M0Lm2e2S559;
    _M0Lm3e10S569 = _M0L1qS581 + _M0L6_2atmpS1516;
    _M0L6_2atmpS1522 = _M0Lm2e2S559;
    _M0L6_2atmpS1521 = -_M0L6_2atmpS1522;
    _M0L1iS582 = _M0L6_2atmpS1521 - _M0L1qS581;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1520 = _M0FPB8pow5bits(_M0L1iS582);
    _M0L1kS583 = _M0L6_2atmpS1520 - 125;
    _M0L1jS584 = _M0L1qS581 - _M0L1kS583;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S585 = _M0FPB19double__computePow5(_M0L1iS582);
    _M0L6_2atmpS1519 = _M0Lm2m2S560;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS586
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1519, _M0L4pow5S585, _M0L1jS584, _M0L7mmShiftS565);
    _M0L8_2avrOutS587 = _M0L7_2abindS586.$0;
    _M0L8_2avpOutS588 = _M0L7_2abindS586.$1;
    _M0L8_2avmOutS589 = _M0L7_2abindS586.$2;
    _M0Lm2vrS566 = _M0L8_2avrOutS587;
    _M0Lm2vpS567 = _M0L8_2avpOutS588;
    _M0Lm2vmS568 = _M0L8_2avmOutS589;
    if (_M0L1qS581 <= 1) {
      _M0Lm17vrIsTrailingZerosS571 = 1;
      if (_M0L4evenS563) {
        int32_t _M0L6_2atmpS1517;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1517 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS565);
        _M0Lm17vmIsTrailingZerosS570 = _M0L6_2atmpS1517 == 1;
      } else {
        uint64_t _M0L6_2atmpS1518 = _M0Lm2vpS567;
        _M0Lm2vpS567 = _M0L6_2atmpS1518 - 1ull;
      }
    } else if (_M0L1qS581 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS571
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS564, _M0L1qS581);
    }
  }
  _M0Lm7removedS590 = 0;
  _M0Lm16lastRemovedDigitS591 = 0;
  _M0Lm6outputS592 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS570 || _M0Lm17vrIsTrailingZerosS571) {
    int32_t _if__result_2111;
    uint64_t _M0L6_2atmpS1559;
    uint64_t _M0L6_2atmpS1565;
    uint64_t _M0L6_2atmpS1566;
    int32_t _if__result_2112;
    int32_t _M0L6_2atmpS1562;
    int64_t _M0L6_2atmpS1561;
    uint64_t _M0L6_2atmpS1560;
    while (1) {
      uint64_t _M0L6_2atmpS1542 = _M0Lm2vpS567;
      uint64_t _M0L7vpDiv10S593 = _M0L6_2atmpS1542 / 10ull;
      uint64_t _M0L6_2atmpS1541 = _M0Lm2vmS568;
      uint64_t _M0L7vmDiv10S594 = _M0L6_2atmpS1541 / 10ull;
      uint64_t _M0L6_2atmpS1540;
      int32_t _M0L6_2atmpS1537;
      int32_t _M0L6_2atmpS1539;
      int32_t _M0L6_2atmpS1538;
      int32_t _M0L7vmMod10S596;
      uint64_t _M0L6_2atmpS1536;
      uint64_t _M0L7vrDiv10S597;
      uint64_t _M0L6_2atmpS1535;
      int32_t _M0L6_2atmpS1532;
      int32_t _M0L6_2atmpS1534;
      int32_t _M0L6_2atmpS1533;
      int32_t _M0L7vrMod10S598;
      int32_t _M0L6_2atmpS1531;
      if (_M0L7vpDiv10S593 <= _M0L7vmDiv10S594) {
        break;
      }
      _M0L6_2atmpS1540 = _M0Lm2vmS568;
      _M0L6_2atmpS1537 = (int32_t)_M0L6_2atmpS1540;
      _M0L6_2atmpS1539 = (int32_t)_M0L7vmDiv10S594;
      _M0L6_2atmpS1538 = 10 * _M0L6_2atmpS1539;
      _M0L7vmMod10S596 = _M0L6_2atmpS1537 - _M0L6_2atmpS1538;
      _M0L6_2atmpS1536 = _M0Lm2vrS566;
      _M0L7vrDiv10S597 = _M0L6_2atmpS1536 / 10ull;
      _M0L6_2atmpS1535 = _M0Lm2vrS566;
      _M0L6_2atmpS1532 = (int32_t)_M0L6_2atmpS1535;
      _M0L6_2atmpS1534 = (int32_t)_M0L7vrDiv10S597;
      _M0L6_2atmpS1533 = 10 * _M0L6_2atmpS1534;
      _M0L7vrMod10S598 = _M0L6_2atmpS1532 - _M0L6_2atmpS1533;
      _M0Lm17vmIsTrailingZerosS570
      = _M0Lm17vmIsTrailingZerosS570 && _M0L7vmMod10S596 == 0;
      if (_M0Lm17vrIsTrailingZerosS571) {
        int32_t _M0L6_2atmpS1530 = _M0Lm16lastRemovedDigitS591;
        _M0Lm17vrIsTrailingZerosS571 = _M0L6_2atmpS1530 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS571 = 0;
      }
      _M0Lm16lastRemovedDigitS591 = _M0L7vrMod10S598;
      _M0Lm2vrS566 = _M0L7vrDiv10S597;
      _M0Lm2vpS567 = _M0L7vpDiv10S593;
      _M0Lm2vmS568 = _M0L7vmDiv10S594;
      _M0L6_2atmpS1531 = _M0Lm7removedS590;
      _M0Lm7removedS590 = _M0L6_2atmpS1531 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS570) {
      while (1) {
        uint64_t _M0L6_2atmpS1555 = _M0Lm2vmS568;
        uint64_t _M0L7vmDiv10S599 = _M0L6_2atmpS1555 / 10ull;
        uint64_t _M0L6_2atmpS1554 = _M0Lm2vmS568;
        int32_t _M0L6_2atmpS1551 = (int32_t)_M0L6_2atmpS1554;
        int32_t _M0L6_2atmpS1553 = (int32_t)_M0L7vmDiv10S599;
        int32_t _M0L6_2atmpS1552 = 10 * _M0L6_2atmpS1553;
        int32_t _M0L7vmMod10S600 = _M0L6_2atmpS1551 - _M0L6_2atmpS1552;
        uint64_t _M0L6_2atmpS1550;
        uint64_t _M0L7vpDiv10S602;
        uint64_t _M0L6_2atmpS1549;
        uint64_t _M0L7vrDiv10S603;
        uint64_t _M0L6_2atmpS1548;
        int32_t _M0L6_2atmpS1545;
        int32_t _M0L6_2atmpS1547;
        int32_t _M0L6_2atmpS1546;
        int32_t _M0L7vrMod10S604;
        int32_t _M0L6_2atmpS1544;
        if (_M0L7vmMod10S600 != 0) {
          break;
        }
        _M0L6_2atmpS1550 = _M0Lm2vpS567;
        _M0L7vpDiv10S602 = _M0L6_2atmpS1550 / 10ull;
        _M0L6_2atmpS1549 = _M0Lm2vrS566;
        _M0L7vrDiv10S603 = _M0L6_2atmpS1549 / 10ull;
        _M0L6_2atmpS1548 = _M0Lm2vrS566;
        _M0L6_2atmpS1545 = (int32_t)_M0L6_2atmpS1548;
        _M0L6_2atmpS1547 = (int32_t)_M0L7vrDiv10S603;
        _M0L6_2atmpS1546 = 10 * _M0L6_2atmpS1547;
        _M0L7vrMod10S604 = _M0L6_2atmpS1545 - _M0L6_2atmpS1546;
        if (_M0Lm17vrIsTrailingZerosS571) {
          int32_t _M0L6_2atmpS1543 = _M0Lm16lastRemovedDigitS591;
          _M0Lm17vrIsTrailingZerosS571 = _M0L6_2atmpS1543 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS571 = 0;
        }
        _M0Lm16lastRemovedDigitS591 = _M0L7vrMod10S604;
        _M0Lm2vrS566 = _M0L7vrDiv10S603;
        _M0Lm2vpS567 = _M0L7vpDiv10S602;
        _M0Lm2vmS568 = _M0L7vmDiv10S599;
        _M0L6_2atmpS1544 = _M0Lm7removedS590;
        _M0Lm7removedS590 = _M0L6_2atmpS1544 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS571) {
      int32_t _M0L6_2atmpS1558 = _M0Lm16lastRemovedDigitS591;
      if (_M0L6_2atmpS1558 == 5) {
        uint64_t _M0L6_2atmpS1557 = _M0Lm2vrS566;
        uint64_t _M0L6_2atmpS1556 = _M0L6_2atmpS1557 % 2ull;
        _if__result_2111 = _M0L6_2atmpS1556 == 0ull;
      } else {
        _if__result_2111 = 0;
      }
    } else {
      _if__result_2111 = 0;
    }
    if (_if__result_2111) {
      _M0Lm16lastRemovedDigitS591 = 4;
    }
    _M0L6_2atmpS1559 = _M0Lm2vrS566;
    _M0L6_2atmpS1565 = _M0Lm2vrS566;
    _M0L6_2atmpS1566 = _M0Lm2vmS568;
    if (_M0L6_2atmpS1565 == _M0L6_2atmpS1566) {
      if (!_M0L4evenS563) {
        _if__result_2112 = 1;
      } else {
        int32_t _M0L6_2atmpS1564 = _M0Lm17vmIsTrailingZerosS570;
        _if__result_2112 = !_M0L6_2atmpS1564;
      }
    } else {
      _if__result_2112 = 0;
    }
    if (_if__result_2112) {
      _M0L6_2atmpS1562 = 1;
    } else {
      int32_t _M0L6_2atmpS1563 = _M0Lm16lastRemovedDigitS591;
      _M0L6_2atmpS1562 = _M0L6_2atmpS1563 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1561 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1562);
    _M0L6_2atmpS1560 = *(uint64_t*)&_M0L6_2atmpS1561;
    _M0Lm6outputS592 = _M0L6_2atmpS1559 + _M0L6_2atmpS1560;
  } else {
    int32_t _M0Lm7roundUpS605 = 0;
    uint64_t _M0L6_2atmpS1587 = _M0Lm2vpS567;
    uint64_t _M0L8vpDiv100S606 = _M0L6_2atmpS1587 / 100ull;
    uint64_t _M0L6_2atmpS1586 = _M0Lm2vmS568;
    uint64_t _M0L8vmDiv100S607 = _M0L6_2atmpS1586 / 100ull;
    uint64_t _M0L6_2atmpS1581;
    uint64_t _M0L6_2atmpS1584;
    uint64_t _M0L6_2atmpS1585;
    int32_t _M0L6_2atmpS1583;
    uint64_t _M0L6_2atmpS1582;
    if (_M0L8vpDiv100S606 > _M0L8vmDiv100S607) {
      uint64_t _M0L6_2atmpS1572 = _M0Lm2vrS566;
      uint64_t _M0L8vrDiv100S608 = _M0L6_2atmpS1572 / 100ull;
      uint64_t _M0L6_2atmpS1571 = _M0Lm2vrS566;
      int32_t _M0L6_2atmpS1568 = (int32_t)_M0L6_2atmpS1571;
      int32_t _M0L6_2atmpS1570 = (int32_t)_M0L8vrDiv100S608;
      int32_t _M0L6_2atmpS1569 = 100 * _M0L6_2atmpS1570;
      int32_t _M0L8vrMod100S609 = _M0L6_2atmpS1568 - _M0L6_2atmpS1569;
      int32_t _M0L6_2atmpS1567;
      _M0Lm7roundUpS605 = _M0L8vrMod100S609 >= 50;
      _M0Lm2vrS566 = _M0L8vrDiv100S608;
      _M0Lm2vpS567 = _M0L8vpDiv100S606;
      _M0Lm2vmS568 = _M0L8vmDiv100S607;
      _M0L6_2atmpS1567 = _M0Lm7removedS590;
      _M0Lm7removedS590 = _M0L6_2atmpS1567 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1580 = _M0Lm2vpS567;
      uint64_t _M0L7vpDiv10S610 = _M0L6_2atmpS1580 / 10ull;
      uint64_t _M0L6_2atmpS1579 = _M0Lm2vmS568;
      uint64_t _M0L7vmDiv10S611 = _M0L6_2atmpS1579 / 10ull;
      uint64_t _M0L6_2atmpS1578;
      uint64_t _M0L7vrDiv10S613;
      uint64_t _M0L6_2atmpS1577;
      int32_t _M0L6_2atmpS1574;
      int32_t _M0L6_2atmpS1576;
      int32_t _M0L6_2atmpS1575;
      int32_t _M0L7vrMod10S614;
      int32_t _M0L6_2atmpS1573;
      if (_M0L7vpDiv10S610 <= _M0L7vmDiv10S611) {
        break;
      }
      _M0L6_2atmpS1578 = _M0Lm2vrS566;
      _M0L7vrDiv10S613 = _M0L6_2atmpS1578 / 10ull;
      _M0L6_2atmpS1577 = _M0Lm2vrS566;
      _M0L6_2atmpS1574 = (int32_t)_M0L6_2atmpS1577;
      _M0L6_2atmpS1576 = (int32_t)_M0L7vrDiv10S613;
      _M0L6_2atmpS1575 = 10 * _M0L6_2atmpS1576;
      _M0L7vrMod10S614 = _M0L6_2atmpS1574 - _M0L6_2atmpS1575;
      _M0Lm7roundUpS605 = _M0L7vrMod10S614 >= 5;
      _M0Lm2vrS566 = _M0L7vrDiv10S613;
      _M0Lm2vpS567 = _M0L7vpDiv10S610;
      _M0Lm2vmS568 = _M0L7vmDiv10S611;
      _M0L6_2atmpS1573 = _M0Lm7removedS590;
      _M0Lm7removedS590 = _M0L6_2atmpS1573 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1581 = _M0Lm2vrS566;
    _M0L6_2atmpS1584 = _M0Lm2vrS566;
    _M0L6_2atmpS1585 = _M0Lm2vmS568;
    _M0L6_2atmpS1583
    = _M0L6_2atmpS1584 == _M0L6_2atmpS1585 || _M0Lm7roundUpS605;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1582 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1583);
    _M0Lm6outputS592 = _M0L6_2atmpS1581 + _M0L6_2atmpS1582;
  }
  _M0L6_2atmpS1589 = _M0Lm3e10S569;
  _M0L6_2atmpS1590 = _M0Lm7removedS590;
  _M0L3expS615 = _M0L6_2atmpS1589 + _M0L6_2atmpS1590;
  _M0L6_2atmpS1588 = _M0Lm6outputS592;
  _block_2114
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2114)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2114->$0 = _M0L6_2atmpS1588;
  _block_2114->$1 = _M0L3expS615;
  return _block_2114;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS558) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS558) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS557) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS557) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS556) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS556) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS555) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS555 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS555 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS555 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS555 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS555 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS555 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS555 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS555 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS555 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS555 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS555 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS555 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS555 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS555 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS555 >= 100ull) {
    return 3;
  }
  if (_M0L1vS555 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS538) {
  int32_t _M0L6_2atmpS1489;
  int32_t _M0L6_2atmpS1488;
  int32_t _M0L4baseS537;
  int32_t _M0L5base2S539;
  int32_t _M0L6offsetS540;
  int32_t _M0L6_2atmpS1487;
  uint64_t _M0L4mul0S541;
  int32_t _M0L6_2atmpS1486;
  int32_t _M0L6_2atmpS1485;
  uint64_t _M0L4mul1S542;
  uint64_t _M0L1mS543;
  struct _M0TPB7Umul128 _M0L7_2abindS544;
  uint64_t _M0L7_2alow1S545;
  uint64_t _M0L8_2ahigh1S546;
  struct _M0TPB7Umul128 _M0L7_2abindS547;
  uint64_t _M0L7_2alow0S548;
  uint64_t _M0L8_2ahigh0S549;
  uint64_t _M0L3sumS550;
  uint64_t _M0Lm5high1S551;
  int32_t _M0L6_2atmpS1483;
  int32_t _M0L6_2atmpS1484;
  int32_t _M0L5deltaS552;
  uint64_t _M0L6_2atmpS1482;
  uint64_t _M0L6_2atmpS1474;
  int32_t _M0L6_2atmpS1481;
  uint32_t _M0L6_2atmpS1478;
  int32_t _M0L6_2atmpS1480;
  int32_t _M0L6_2atmpS1479;
  uint32_t _M0L6_2atmpS1477;
  uint32_t _M0L6_2atmpS1476;
  uint64_t _M0L6_2atmpS1475;
  uint64_t _M0L1aS553;
  uint64_t _M0L6_2atmpS1473;
  uint64_t _M0L1bS554;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1489 = _M0L1iS538 + 26;
  _M0L6_2atmpS1488 = _M0L6_2atmpS1489 - 1;
  _M0L4baseS537 = _M0L6_2atmpS1488 / 26;
  _M0L5base2S539 = _M0L4baseS537 * 26;
  _M0L6offsetS540 = _M0L5base2S539 - _M0L1iS538;
  _M0L6_2atmpS1487 = _M0L4baseS537 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S541
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1487);
  _M0L6_2atmpS1486 = _M0L4baseS537 * 2;
  _M0L6_2atmpS1485 = _M0L6_2atmpS1486 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S542
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1485);
  if (_M0L6offsetS540 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S541, .$1 = _M0L4mul1S542};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS543
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS540);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS544 = _M0FPB7umul128(_M0L1mS543, _M0L4mul1S542);
  _M0L7_2alow1S545 = _M0L7_2abindS544.$0;
  _M0L8_2ahigh1S546 = _M0L7_2abindS544.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS547 = _M0FPB7umul128(_M0L1mS543, _M0L4mul0S541);
  _M0L7_2alow0S548 = _M0L7_2abindS547.$0;
  _M0L8_2ahigh0S549 = _M0L7_2abindS547.$1;
  _M0L3sumS550 = _M0L8_2ahigh0S549 + _M0L7_2alow1S545;
  _M0Lm5high1S551 = _M0L8_2ahigh1S546;
  if (_M0L3sumS550 < _M0L8_2ahigh0S549) {
    uint64_t _M0L6_2atmpS1472 = _M0Lm5high1S551;
    _M0Lm5high1S551 = _M0L6_2atmpS1472 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1483 = _M0FPB8pow5bits(_M0L5base2S539);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1484 = _M0FPB8pow5bits(_M0L1iS538);
  _M0L5deltaS552 = _M0L6_2atmpS1483 - _M0L6_2atmpS1484;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1482
  = _M0FPB13shiftright128(_M0L7_2alow0S548, _M0L3sumS550, _M0L5deltaS552);
  _M0L6_2atmpS1474 = _M0L6_2atmpS1482 + 1ull;
  _M0L6_2atmpS1481 = _M0L1iS538 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1478
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1481);
  _M0L6_2atmpS1480 = _M0L1iS538 % 16;
  _M0L6_2atmpS1479 = _M0L6_2atmpS1480 << 1;
  _M0L6_2atmpS1477 = _M0L6_2atmpS1478 >> (_M0L6_2atmpS1479 & 31);
  _M0L6_2atmpS1476 = _M0L6_2atmpS1477 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1475 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1476);
  _M0L1aS553 = _M0L6_2atmpS1474 + _M0L6_2atmpS1475;
  _M0L6_2atmpS1473 = _M0Lm5high1S551;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS554
  = _M0FPB13shiftright128(_M0L3sumS550, _M0L6_2atmpS1473, _M0L5deltaS552);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS553, .$1 = _M0L1bS554};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS520) {
  int32_t _M0L4baseS519;
  int32_t _M0L5base2S521;
  int32_t _M0L6offsetS522;
  int32_t _M0L6_2atmpS1471;
  uint64_t _M0L4mul0S523;
  int32_t _M0L6_2atmpS1470;
  int32_t _M0L6_2atmpS1469;
  uint64_t _M0L4mul1S524;
  uint64_t _M0L1mS525;
  struct _M0TPB7Umul128 _M0L7_2abindS526;
  uint64_t _M0L7_2alow1S527;
  uint64_t _M0L8_2ahigh1S528;
  struct _M0TPB7Umul128 _M0L7_2abindS529;
  uint64_t _M0L7_2alow0S530;
  uint64_t _M0L8_2ahigh0S531;
  uint64_t _M0L3sumS532;
  uint64_t _M0Lm5high1S533;
  int32_t _M0L6_2atmpS1467;
  int32_t _M0L6_2atmpS1468;
  int32_t _M0L5deltaS534;
  uint64_t _M0L6_2atmpS1459;
  int32_t _M0L6_2atmpS1466;
  uint32_t _M0L6_2atmpS1463;
  int32_t _M0L6_2atmpS1465;
  int32_t _M0L6_2atmpS1464;
  uint32_t _M0L6_2atmpS1462;
  uint32_t _M0L6_2atmpS1461;
  uint64_t _M0L6_2atmpS1460;
  uint64_t _M0L1aS535;
  uint64_t _M0L6_2atmpS1458;
  uint64_t _M0L1bS536;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS519 = _M0L1iS520 / 26;
  _M0L5base2S521 = _M0L4baseS519 * 26;
  _M0L6offsetS522 = _M0L1iS520 - _M0L5base2S521;
  _M0L6_2atmpS1471 = _M0L4baseS519 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S523
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1471);
  _M0L6_2atmpS1470 = _M0L4baseS519 * 2;
  _M0L6_2atmpS1469 = _M0L6_2atmpS1470 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S524
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1469);
  if (_M0L6offsetS522 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S523, .$1 = _M0L4mul1S524};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS525
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS522);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS526 = _M0FPB7umul128(_M0L1mS525, _M0L4mul1S524);
  _M0L7_2alow1S527 = _M0L7_2abindS526.$0;
  _M0L8_2ahigh1S528 = _M0L7_2abindS526.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS529 = _M0FPB7umul128(_M0L1mS525, _M0L4mul0S523);
  _M0L7_2alow0S530 = _M0L7_2abindS529.$0;
  _M0L8_2ahigh0S531 = _M0L7_2abindS529.$1;
  _M0L3sumS532 = _M0L8_2ahigh0S531 + _M0L7_2alow1S527;
  _M0Lm5high1S533 = _M0L8_2ahigh1S528;
  if (_M0L3sumS532 < _M0L8_2ahigh0S531) {
    uint64_t _M0L6_2atmpS1457 = _M0Lm5high1S533;
    _M0Lm5high1S533 = _M0L6_2atmpS1457 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1467 = _M0FPB8pow5bits(_M0L1iS520);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1468 = _M0FPB8pow5bits(_M0L5base2S521);
  _M0L5deltaS534 = _M0L6_2atmpS1467 - _M0L6_2atmpS1468;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1459
  = _M0FPB13shiftright128(_M0L7_2alow0S530, _M0L3sumS532, _M0L5deltaS534);
  _M0L6_2atmpS1466 = _M0L1iS520 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1463
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1466);
  _M0L6_2atmpS1465 = _M0L1iS520 % 16;
  _M0L6_2atmpS1464 = _M0L6_2atmpS1465 << 1;
  _M0L6_2atmpS1462 = _M0L6_2atmpS1463 >> (_M0L6_2atmpS1464 & 31);
  _M0L6_2atmpS1461 = _M0L6_2atmpS1462 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1460 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1461);
  _M0L1aS535 = _M0L6_2atmpS1459 + _M0L6_2atmpS1460;
  _M0L6_2atmpS1458 = _M0Lm5high1S533;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS536
  = _M0FPB13shiftright128(_M0L3sumS532, _M0L6_2atmpS1458, _M0L5deltaS534);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS535, .$1 = _M0L1bS536};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS493,
  struct _M0TPB8Pow5Pair _M0L3mulS490,
  int32_t _M0L1jS506,
  int32_t _M0L7mmShiftS508
) {
  uint64_t _M0L7_2amul0S489;
  uint64_t _M0L7_2amul1S491;
  uint64_t _M0L1mS492;
  struct _M0TPB7Umul128 _M0L7_2abindS494;
  uint64_t _M0L5_2aloS495;
  uint64_t _M0L6_2atmpS496;
  struct _M0TPB7Umul128 _M0L7_2abindS497;
  uint64_t _M0L6_2alo2S498;
  uint64_t _M0L6_2ahi2S499;
  uint64_t _M0L3midS500;
  uint64_t _M0L6_2atmpS1456;
  uint64_t _M0L2hiS501;
  uint64_t _M0L3lo2S502;
  uint64_t _M0L6_2atmpS1454;
  uint64_t _M0L6_2atmpS1455;
  uint64_t _M0L4mid2S503;
  uint64_t _M0L6_2atmpS1453;
  uint64_t _M0L3hi2S504;
  int32_t _M0L6_2atmpS1452;
  int32_t _M0L6_2atmpS1451;
  uint64_t _M0L2vpS505;
  uint64_t _M0Lm2vmS507;
  int32_t _M0L6_2atmpS1450;
  int32_t _M0L6_2atmpS1449;
  uint64_t _M0L2vrS518;
  uint64_t _M0L6_2atmpS1448;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S489 = _M0L3mulS490.$0;
  _M0L7_2amul1S491 = _M0L3mulS490.$1;
  _M0L1mS492 = _M0L1mS493 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS494 = _M0FPB7umul128(_M0L1mS492, _M0L7_2amul0S489);
  _M0L5_2aloS495 = _M0L7_2abindS494.$0;
  _M0L6_2atmpS496 = _M0L7_2abindS494.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS497 = _M0FPB7umul128(_M0L1mS492, _M0L7_2amul1S491);
  _M0L6_2alo2S498 = _M0L7_2abindS497.$0;
  _M0L6_2ahi2S499 = _M0L7_2abindS497.$1;
  _M0L3midS500 = _M0L6_2atmpS496 + _M0L6_2alo2S498;
  if (_M0L3midS500 < _M0L6_2atmpS496) {
    _M0L6_2atmpS1456 = 1ull;
  } else {
    _M0L6_2atmpS1456 = 0ull;
  }
  _M0L2hiS501 = _M0L6_2ahi2S499 + _M0L6_2atmpS1456;
  _M0L3lo2S502 = _M0L5_2aloS495 + _M0L7_2amul0S489;
  _M0L6_2atmpS1454 = _M0L3midS500 + _M0L7_2amul1S491;
  if (_M0L3lo2S502 < _M0L5_2aloS495) {
    _M0L6_2atmpS1455 = 1ull;
  } else {
    _M0L6_2atmpS1455 = 0ull;
  }
  _M0L4mid2S503 = _M0L6_2atmpS1454 + _M0L6_2atmpS1455;
  if (_M0L4mid2S503 < _M0L3midS500) {
    _M0L6_2atmpS1453 = 1ull;
  } else {
    _M0L6_2atmpS1453 = 0ull;
  }
  _M0L3hi2S504 = _M0L2hiS501 + _M0L6_2atmpS1453;
  _M0L6_2atmpS1452 = _M0L1jS506 - 64;
  _M0L6_2atmpS1451 = _M0L6_2atmpS1452 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS505
  = _M0FPB13shiftright128(_M0L4mid2S503, _M0L3hi2S504, _M0L6_2atmpS1451);
  _M0Lm2vmS507 = 0ull;
  if (_M0L7mmShiftS508) {
    uint64_t _M0L3lo3S509 = _M0L5_2aloS495 - _M0L7_2amul0S489;
    uint64_t _M0L6_2atmpS1438 = _M0L3midS500 - _M0L7_2amul1S491;
    uint64_t _M0L6_2atmpS1439;
    uint64_t _M0L4mid3S510;
    uint64_t _M0L6_2atmpS1437;
    uint64_t _M0L3hi3S511;
    int32_t _M0L6_2atmpS1436;
    int32_t _M0L6_2atmpS1435;
    if (_M0L5_2aloS495 < _M0L3lo3S509) {
      _M0L6_2atmpS1439 = 1ull;
    } else {
      _M0L6_2atmpS1439 = 0ull;
    }
    _M0L4mid3S510 = _M0L6_2atmpS1438 - _M0L6_2atmpS1439;
    if (_M0L3midS500 < _M0L4mid3S510) {
      _M0L6_2atmpS1437 = 1ull;
    } else {
      _M0L6_2atmpS1437 = 0ull;
    }
    _M0L3hi3S511 = _M0L2hiS501 - _M0L6_2atmpS1437;
    _M0L6_2atmpS1436 = _M0L1jS506 - 64;
    _M0L6_2atmpS1435 = _M0L6_2atmpS1436 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS507
    = _M0FPB13shiftright128(_M0L4mid3S510, _M0L3hi3S511, _M0L6_2atmpS1435);
  } else {
    uint64_t _M0L3lo3S512 = _M0L5_2aloS495 + _M0L5_2aloS495;
    uint64_t _M0L6_2atmpS1446 = _M0L3midS500 + _M0L3midS500;
    uint64_t _M0L6_2atmpS1447;
    uint64_t _M0L4mid3S513;
    uint64_t _M0L6_2atmpS1444;
    uint64_t _M0L6_2atmpS1445;
    uint64_t _M0L3hi3S514;
    uint64_t _M0L3lo4S515;
    uint64_t _M0L6_2atmpS1442;
    uint64_t _M0L6_2atmpS1443;
    uint64_t _M0L4mid4S516;
    uint64_t _M0L6_2atmpS1441;
    uint64_t _M0L3hi4S517;
    int32_t _M0L6_2atmpS1440;
    if (_M0L3lo3S512 < _M0L5_2aloS495) {
      _M0L6_2atmpS1447 = 1ull;
    } else {
      _M0L6_2atmpS1447 = 0ull;
    }
    _M0L4mid3S513 = _M0L6_2atmpS1446 + _M0L6_2atmpS1447;
    _M0L6_2atmpS1444 = _M0L2hiS501 + _M0L2hiS501;
    if (_M0L4mid3S513 < _M0L3midS500) {
      _M0L6_2atmpS1445 = 1ull;
    } else {
      _M0L6_2atmpS1445 = 0ull;
    }
    _M0L3hi3S514 = _M0L6_2atmpS1444 + _M0L6_2atmpS1445;
    _M0L3lo4S515 = _M0L3lo3S512 - _M0L7_2amul0S489;
    _M0L6_2atmpS1442 = _M0L4mid3S513 - _M0L7_2amul1S491;
    if (_M0L3lo3S512 < _M0L3lo4S515) {
      _M0L6_2atmpS1443 = 1ull;
    } else {
      _M0L6_2atmpS1443 = 0ull;
    }
    _M0L4mid4S516 = _M0L6_2atmpS1442 - _M0L6_2atmpS1443;
    if (_M0L4mid3S513 < _M0L4mid4S516) {
      _M0L6_2atmpS1441 = 1ull;
    } else {
      _M0L6_2atmpS1441 = 0ull;
    }
    _M0L3hi4S517 = _M0L3hi3S514 - _M0L6_2atmpS1441;
    _M0L6_2atmpS1440 = _M0L1jS506 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS507
    = _M0FPB13shiftright128(_M0L4mid4S516, _M0L3hi4S517, _M0L6_2atmpS1440);
  }
  _M0L6_2atmpS1450 = _M0L1jS506 - 64;
  _M0L6_2atmpS1449 = _M0L6_2atmpS1450 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS518
  = _M0FPB13shiftright128(_M0L3midS500, _M0L2hiS501, _M0L6_2atmpS1449);
  _M0L6_2atmpS1448 = _M0Lm2vmS507;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS518,
                                                .$1 = _M0L2vpS505,
                                                .$2 = _M0L6_2atmpS1448};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS487,
  int32_t _M0L1pS488
) {
  uint64_t _M0L6_2atmpS1434;
  uint64_t _M0L6_2atmpS1433;
  uint64_t _M0L6_2atmpS1432;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1434 = 1ull << (_M0L1pS488 & 63);
  _M0L6_2atmpS1433 = _M0L6_2atmpS1434 - 1ull;
  _M0L6_2atmpS1432 = _M0L5valueS487 & _M0L6_2atmpS1433;
  return _M0L6_2atmpS1432 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS485,
  int32_t _M0L1pS486
) {
  int32_t _M0L6_2atmpS1431;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1431 = _M0FPB10pow5Factor(_M0L5valueS485);
  return _M0L6_2atmpS1431 >= _M0L1pS486;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS480) {
  uint64_t _M0L6_2atmpS1422;
  uint64_t _M0L6_2atmpS1423;
  uint64_t _M0L6_2atmpS1424;
  uint64_t _M0L6_2atmpS1425;
  uint64_t _M0L6_2atmpS1430;
  int32_t _M0L5countS481;
  uint64_t _M0L1vS482;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1422 = _M0L5valueS480 % 5ull;
  if (_M0L6_2atmpS1422 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1423 = _M0L5valueS480 % 25ull;
  if (_M0L6_2atmpS1423 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1424 = _M0L5valueS480 % 125ull;
  if (_M0L6_2atmpS1424 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1425 = _M0L5valueS480 % 625ull;
  if (_M0L6_2atmpS1425 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1430 = _M0L5valueS480 / 625ull;
  _M0L5countS481 = 4;
  _M0L1vS482 = _M0L6_2atmpS1430;
  while (1) {
    if (_M0L1vS482 > 0ull) {
      uint64_t _M0L6_2atmpS1426 = _M0L1vS482 % 5ull;
      int32_t _M0L6_2atmpS1427;
      uint64_t _M0L6_2atmpS1428;
      if (_M0L6_2atmpS1426 != 0ull) {
        return _M0L5countS481;
      }
      _M0L6_2atmpS1427 = _M0L5countS481 + 1;
      _M0L6_2atmpS1428 = _M0L1vS482 / 5ull;
      _M0L5countS481 = _M0L6_2atmpS1427;
      _M0L1vS482 = _M0L6_2atmpS1428;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS484;
      moonbit_string_t _M0L6_2atmpS1429;
      int32_t _result_2116;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS484
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS484, (moonbit_string_t)moonbit_string_literal_10.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS484, _M0L5valueS480);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1429
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS484);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS484);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2116 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1429);
      moonbit_decref_cycle_free(_M0L6_2atmpS1429);
      return _result_2116;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS479,
  uint64_t _M0L2hiS477,
  int32_t _M0L4distS478
) {
  int32_t _M0L6_2atmpS1421;
  uint64_t _M0L6_2atmpS1419;
  uint64_t _M0L6_2atmpS1420;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1421 = 64 - _M0L4distS478;
  _M0L6_2atmpS1419 = _M0L2hiS477 << (_M0L6_2atmpS1421 & 63);
  _M0L6_2atmpS1420 = _M0L2loS479 >> (_M0L4distS478 & 63);
  return _M0L6_2atmpS1419 | _M0L6_2atmpS1420;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS467,
  uint64_t _M0L1bS470
) {
  uint64_t _M0L3aLoS466;
  uint64_t _M0L3aHiS468;
  uint64_t _M0L3bLoS469;
  uint64_t _M0L3bHiS471;
  uint64_t _M0L1xS472;
  uint64_t _M0L6_2atmpS1417;
  uint64_t _M0L6_2atmpS1418;
  uint64_t _M0L1yS473;
  uint64_t _M0L6_2atmpS1415;
  uint64_t _M0L6_2atmpS1416;
  uint64_t _M0L1zS474;
  uint64_t _M0L6_2atmpS1413;
  uint64_t _M0L6_2atmpS1414;
  uint64_t _M0L6_2atmpS1411;
  uint64_t _M0L6_2atmpS1412;
  uint64_t _M0L1wS475;
  uint64_t _M0L2loS476;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS466 = _M0L1aS467 & 4294967295ull;
  _M0L3aHiS468 = _M0L1aS467 >> 32;
  _M0L3bLoS469 = _M0L1bS470 & 4294967295ull;
  _M0L3bHiS471 = _M0L1bS470 >> 32;
  _M0L1xS472 = _M0L3aLoS466 * _M0L3bLoS469;
  _M0L6_2atmpS1417 = _M0L3aHiS468 * _M0L3bLoS469;
  _M0L6_2atmpS1418 = _M0L1xS472 >> 32;
  _M0L1yS473 = _M0L6_2atmpS1417 + _M0L6_2atmpS1418;
  _M0L6_2atmpS1415 = _M0L3aLoS466 * _M0L3bHiS471;
  _M0L6_2atmpS1416 = _M0L1yS473 & 4294967295ull;
  _M0L1zS474 = _M0L6_2atmpS1415 + _M0L6_2atmpS1416;
  _M0L6_2atmpS1413 = _M0L3aHiS468 * _M0L3bHiS471;
  _M0L6_2atmpS1414 = _M0L1yS473 >> 32;
  _M0L6_2atmpS1411 = _M0L6_2atmpS1413 + _M0L6_2atmpS1414;
  _M0L6_2atmpS1412 = _M0L1zS474 >> 32;
  _M0L1wS475 = _M0L6_2atmpS1411 + _M0L6_2atmpS1412;
  _M0L2loS476 = _M0L1aS467 * _M0L1bS470;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS476, .$1 = _M0L1wS475};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS464,
  int32_t _M0L4fromS461,
  int32_t _M0L2toS460
) {
  int32_t _M0L3lenS459;
  int32_t _M0L6_2atmpS1410;
  uint16_t* _M0L6bufferS462;
  int32_t _M0L1iS463;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS459 = _M0L2toS460 - _M0L4fromS461;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1410 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS462
  = (uint16_t*)moonbit_make_string(_M0L3lenS459, _M0L6_2atmpS1410);
  _M0L1iS463 = 0;
  while (1) {
    if (_M0L1iS463 < _M0L3lenS459) {
      int32_t _M0L6_2atmpS1408 = _M0L4fromS461 + _M0L1iS463;
      int32_t _M0L6_2atmpS1407;
      int32_t _M0L6_2atmpS1406;
      int32_t _M0L6_2atmpS1409;
      if (
        _M0L6_2atmpS1408 < 0
        || _M0L6_2atmpS1408 >= Moonbit_array_length(_M0L5bytesS464)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1407 = (int32_t)_M0L5bytesS464[_M0L6_2atmpS1408];
      _M0L6_2atmpS1406 = (uint16_t)_M0L6_2atmpS1407;
      if (
        _M0L1iS463 < 0 || _M0L1iS463 >= Moonbit_array_length(_M0L6bufferS462)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS462[_M0L1iS463] = _M0L6_2atmpS1406;
      _M0L6_2atmpS1409 = _M0L1iS463 + 1;
      _M0L1iS463 = _M0L6_2atmpS1409;
      continue;
    }
    break;
  }
  return _M0L6bufferS462;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS458) {
  int32_t _M0L6_2atmpS1405;
  uint32_t _M0L6_2atmpS1404;
  uint32_t _M0L6_2atmpS1403;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1405 = _M0L1eS458 * 78913;
  _M0L6_2atmpS1404 = *(uint32_t*)&_M0L6_2atmpS1405;
  _M0L6_2atmpS1403 = _M0L6_2atmpS1404 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1403;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS457) {
  int32_t _M0L6_2atmpS1402;
  uint32_t _M0L6_2atmpS1401;
  uint32_t _M0L6_2atmpS1400;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1402 = _M0L1eS457 * 732923;
  _M0L6_2atmpS1401 = *(uint32_t*)&_M0L6_2atmpS1402;
  _M0L6_2atmpS1400 = _M0L6_2atmpS1401 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1400;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS455,
  int32_t _M0L8exponentS456,
  int32_t _M0L8mantissaS453
) {
  moonbit_string_t _M0L1sS454;
  moonbit_string_t _result_2119;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS453) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  if (_M0L4signS455) {
    _M0L1sS454 = (moonbit_string_t)moonbit_string_literal_12.data;
  } else {
    _M0L1sS454 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS456) {
    moonbit_string_t _result_2118;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2118
    = moonbit_add_string(_M0L1sS454, (moonbit_string_t)moonbit_string_literal_13.data);
    moonbit_decref_cycle_free(_M0L1sS454);
    return _result_2118;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2119
  = moonbit_add_string(_M0L1sS454, (moonbit_string_t)moonbit_string_literal_14.data);
  moonbit_decref_cycle_free(_M0L1sS454);
  return _result_2119;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS452) {
  int32_t _M0L6_2atmpS1399;
  uint32_t _M0L6_2atmpS1398;
  uint32_t _M0L6_2atmpS1397;
  int32_t _M0L6_2atmpS1396;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1399 = _M0L1eS452 * 1217359;
  _M0L6_2atmpS1398 = *(uint32_t*)&_M0L6_2atmpS1399;
  _M0L6_2atmpS1397 = _M0L6_2atmpS1398 >> 19;
  _M0L6_2atmpS1396 = *(int32_t*)&_M0L6_2atmpS1397;
  return _M0L6_2atmpS1396 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS451) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS451 != _M0L4selfS451) {
    return 0;
  } else if (_M0L4selfS451 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS451 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS451;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS450) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS450 != _M0L4selfS450) {
    return 0ll;
  } else if (_M0L4selfS450 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS450 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS450;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS447
) {
  float* _M0L6_2atmpS1393;
  struct _M0TPB5ArrayGfE* _block_2120;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1393 = (float*)moonbit_make_float_array_raw(_M0L3lenS447);
  _block_2120
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2120)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 28, 0);
  _block_2120->$0 = _M0L6_2atmpS1393;
  _block_2120->$1 = _M0L3lenS447;
  return _block_2120;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS448
) {
  uint8_t* _M0L6_2atmpS1394;
  struct _M0TPB5ArrayGbE* _block_2121;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1394 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS448);
  _block_2121
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2121)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 31, 0);
  _block_2121->$0 = _M0L6_2atmpS1394;
  _block_2121->$1 = _M0L3lenS448;
  return _block_2121;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS449
) {
  int32_t* _M0L6_2atmpS1395;
  struct _M0TPB5ArrayGiE* _block_2122;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1395 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS449);
  _block_2122
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2122)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 34, 0);
  _block_2122->$0 = _M0L6_2atmpS1395;
  _block_2122->$1 = _M0L3lenS449;
  return _block_2122;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS443,
  int32_t _M0L5indexS444
) {
  uint64_t* _M0L6_2atmpS1391;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1391 = _M0L4selfS443;
  if (
    _M0L5indexS444 < 0
    || _M0L5indexS444 >= Moonbit_array_length(_M0L6_2atmpS1391)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1391[_M0L5indexS444];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS445,
  int32_t _M0L5indexS446
) {
  uint32_t* _M0L6_2atmpS1392;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1392 = _M0L4selfS445;
  if (
    _M0L5indexS446 < 0
    || _M0L5indexS446 >= Moonbit_array_length(_M0L6_2atmpS1392)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1392[_M0L5indexS446];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS442
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS442, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS441) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS441, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS440) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS440;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS434,
  moonbit_string_t _M0L5valueS436
) {
  int32_t _M0L3lenS1377;
  moonbit_string_t* _M0L6_2atmpS1379;
  int32_t _M0L6_2atmpS1378;
  int32_t _M0L6lengthS435;
  moonbit_string_t* _M0L3bufS1382;
  moonbit_string_t _M0L6_2aoldS2027;
  int32_t _M0L6_2atmpS1383;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1377 = _M0L4selfS434->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1379 = _M0MPC15array5Array6bufferGsE(_M0L4selfS434);
  _M0L6_2atmpS1378 = Moonbit_array_length(_M0L6_2atmpS1379);
  moonbit_decref_cycle_free(_M0L6_2atmpS1379);
  if (_M0L3lenS1377 == _M0L6_2atmpS1378) {
    int32_t _M0L3lenS1381 = _M0L4selfS434->$1;
    int32_t _M0L6_2atmpS1380 = _M0L3lenS1381 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS434, _M0L6_2atmpS1380);
  }
  _M0L6lengthS435 = _M0L4selfS434->$1;
  _M0L3bufS1382 = _M0L4selfS434->$0;
  _M0L6_2aoldS2027 = (moonbit_string_t)_M0L3bufS1382[_M0L6lengthS435];
  moonbit_decref_cycle_free(_M0L6_2aoldS2027);
  _M0L3bufS1382[_M0L6lengthS435] = _M0L5valueS436;
  _M0L6_2atmpS1383 = _M0L6lengthS435 + 1;
  _M0L4selfS434->$1 = _M0L6_2atmpS1383;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS437,
  struct _M0TUsiE* _M0L5valueS439
) {
  int32_t _M0L3lenS1384;
  struct _M0TUsiE** _M0L6_2atmpS1386;
  int32_t _M0L6_2atmpS1385;
  int32_t _M0L6lengthS438;
  struct _M0TUsiE** _M0L3bufS1389;
  struct _M0TUsiE* _M0L6_2aoldS2028;
  int32_t _M0L6_2atmpS1390;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1384 = _M0L4selfS437->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1386 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS437);
  _M0L6_2atmpS1385 = Moonbit_array_length(_M0L6_2atmpS1386);
  moonbit_decref_cycle_free(_M0L6_2atmpS1386);
  if (_M0L3lenS1384 == _M0L6_2atmpS1385) {
    int32_t _M0L3lenS1388 = _M0L4selfS437->$1;
    int32_t _M0L6_2atmpS1387 = _M0L3lenS1388 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS437, _M0L6_2atmpS1387);
  }
  _M0L6lengthS438 = _M0L4selfS437->$1;
  _M0L3bufS1389 = _M0L4selfS437->$0;
  _M0L6_2aoldS2028 = (struct _M0TUsiE*)_M0L3bufS1389[_M0L6lengthS438];
  if (_M0L6_2aoldS2028) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2028);
  }
  _M0L3bufS1389[_M0L6lengthS438] = _M0L5valueS439;
  _M0L6_2atmpS1390 = _M0L6lengthS438 + 1;
  _M0L4selfS437->$1 = _M0L6_2atmpS1390;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS427,
  int32_t _M0L8requiredS429
) {
  int32_t _M0L8old__capS426;
  int32_t _M0L3lenS1375;
  int32_t _M0L8new__capS428;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS426 = _M0MPC15array5Array8capacityGsE(_M0L4selfS427);
  _M0L3lenS1375 = _M0L4selfS427->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS428
  = _M0FPB23array__growth__capacity(_M0L8old__capS426, _M0L3lenS1375, _M0L8requiredS429);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS427, _M0L8new__capS428);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS431,
  int32_t _M0L8requiredS433
) {
  int32_t _M0L8old__capS430;
  int32_t _M0L3lenS1376;
  int32_t _M0L8new__capS432;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS430 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS431);
  _M0L3lenS1376 = _M0L4selfS431->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS432
  = _M0FPB23array__growth__capacity(_M0L8old__capS430, _M0L3lenS1376, _M0L8requiredS433);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS431, _M0L8new__capS432);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS415,
  int32_t _M0L13new__capacityS418
) {
  moonbit_string_t* _M0L8old__bufS414;
  int32_t _M0L3lenS416;
  int32_t _M0L9copy__lenS417;
  moonbit_string_t* _M0L8new__bufS419;
  moonbit_string_t* _M0L6_2aoldS2029;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS414 = _M0L4selfS415->$0;
  _M0L3lenS416 = _M0L4selfS415->$1;
  if (_M0L3lenS416 < _M0L13new__capacityS418) {
    _M0L9copy__lenS417 = _M0L3lenS416;
  } else {
    _M0L9copy__lenS417 = _M0L13new__capacityS418;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS414);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS419
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS414, _M0L13new__capacityS418, _M0L9copy__lenS417, 0, 0);
  _M0L6_2aoldS2029 = _M0L4selfS415->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2029);
  _M0L4selfS415->$0 = _M0L8new__bufS419;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS421,
  int32_t _M0L13new__capacityS424
) {
  struct _M0TUsiE** _M0L8old__bufS420;
  int32_t _M0L3lenS422;
  int32_t _M0L9copy__lenS423;
  struct _M0TUsiE** _M0L8new__bufS425;
  struct _M0TUsiE** _M0L6_2aoldS2030;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS420 = _M0L4selfS421->$0;
  _M0L3lenS422 = _M0L4selfS421->$1;
  if (_M0L3lenS422 < _M0L13new__capacityS424) {
    _M0L9copy__lenS423 = _M0L3lenS422;
  } else {
    _M0L9copy__lenS423 = _M0L13new__capacityS424;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS420);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS425
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS420, _M0L13new__capacityS424, _M0L9copy__lenS423, 0, 0);
  _M0L6_2aoldS2030 = _M0L4selfS421->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2030);
  _M0L4selfS421->$0 = _M0L8new__bufS425;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS412
) {
  moonbit_string_t* _M0L6_2atmpS1373;
  int32_t _result_2123;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1373 = _M0MPC15array5Array6bufferGsE(_M0L4selfS412);
  _result_2123 = Moonbit_array_length(_M0L6_2atmpS1373);
  moonbit_decref_cycle_free(_M0L6_2atmpS1373);
  return _result_2123;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS413
) {
  struct _M0TUsiE** _M0L6_2atmpS1374;
  int32_t _result_2124;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1374 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS413);
  _result_2124 = Moonbit_array_length(_M0L6_2atmpS1374);
  moonbit_decref_cycle_free(_M0L6_2atmpS1374);
  return _result_2124;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS408,
  int32_t _M0L3lenS406,
  int32_t _M0L8requiredS405
) {
  int32_t _M0L5startS407;
  int32_t _M0L5spaceS409;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS405 < _M0L3lenS406) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_15.data);
  }
  if (_M0L7currentS408 == 0) {
    _M0L5startS407 = 8;
  } else {
    _M0L5startS407 = _M0L7currentS408;
  }
  _M0L5spaceS409 = _M0L5startS407;
  while (1) {
    if (_M0L5spaceS409 < _M0L8requiredS405) {
      int32_t _M0L4nextS410 = _M0L5spaceS409 * 2;
      if (_M0L4nextS410 <= _M0L5spaceS409) {
        return _M0L8requiredS405;
      }
      _M0L5spaceS409 = _M0L4nextS410;
      continue;
    } else {
      return _M0L5spaceS409;
    }
    break;
  }
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS400) {
  float* _M0L8_2afieldS2031;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2031 = _M0L4selfS400->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2031);
  return _M0L8_2afieldS2031;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS401) {
  uint8_t* _M0L8_2afieldS2032;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2032 = _M0L4selfS401->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2032);
  return _M0L8_2afieldS2032;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS402) {
  int32_t* _M0L8_2afieldS2033;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2033 = _M0L4selfS402->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2033);
  return _M0L8_2afieldS2033;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS403
) {
  moonbit_string_t* _M0L8_2afieldS2034;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2034 = _M0L4selfS403->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2034);
  return _M0L8_2afieldS2034;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS404
) {
  struct _M0TUsiE** _M0L8_2afieldS2035;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2035 = _M0L4selfS404->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2035);
  return _M0L8_2afieldS2035;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS399
) {
  #line 220 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref_cycle_free(_M0L4selfS399);
  return _M0L4selfS399;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS398,
  struct _M0TPC16string10StringView _M0L3strS396
) {
  int32_t _M0L3endS1371;
  int32_t _M0L5startS1372;
  int32_t _M0L8str__lenS395;
  int32_t _M0L3lenS1370;
  int32_t _M0L8requiredS397;
  uint16_t* _M0L4dataS1363;
  int32_t _M0L6_2atmpS1362;
  int32_t _if__result_2126;
  uint16_t* _M0L4dataS1364;
  int32_t _M0L3lenS1365;
  moonbit_string_t _M0L6_2atmpS1366;
  int32_t _M0L6_2atmpS1367;
  int32_t _M0L3lenS1369;
  int32_t _M0L6_2atmpS1368;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1371 = _M0L3strS396.$2;
  _M0L5startS1372 = _M0L3strS396.$1;
  _M0L8str__lenS395 = _M0L3endS1371 - _M0L5startS1372;
  if (_M0L8str__lenS395 == 0) {
    return 0;
  }
  _M0L3lenS1370 = _M0L4selfS398->$1;
  _M0L8requiredS397 = _M0L3lenS1370 + _M0L8str__lenS395;
  _M0L4dataS1363 = _M0L4selfS398->$0;
  _M0L6_2atmpS1362 = Moonbit_array_length(_M0L4dataS1363);
  if (_M0L8requiredS397 > _M0L6_2atmpS1362) {
    _if__result_2126 = 1;
  } else {
    int32_t _M0L3lenS1361 = _M0L4selfS398->$1;
    _if__result_2126 = _M0L8requiredS397 < _M0L3lenS1361;
  }
  if (_if__result_2126) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS398, _M0L8requiredS397);
  }
  _M0L4dataS1364 = _M0L4selfS398->$0;
  _M0L3lenS1365 = _M0L4selfS398->$1;
  moonbit_incref_cycle_free(_M0L4dataS1364);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1366 = _M0MPC16string10StringView4data(_M0L3strS396);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1367 = _M0MPC16string10StringView13start__offset(_M0L3strS396);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1364, _M0L3lenS1365, _M0L6_2atmpS1366, _M0L6_2atmpS1367, _M0L8str__lenS395);
  moonbit_decref_cycle_free(_M0L4dataS1364);
  moonbit_decref_cycle_free(_M0L6_2atmpS1366);
  _M0L3lenS1369 = _M0L4selfS398->$1;
  _M0L6_2atmpS1368 = _M0L3lenS1369 + _M0L8str__lenS395;
  _M0L4selfS398->$1 = _M0L6_2atmpS1368;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS392,
  int32_t _M0L5startS390,
  int32_t _M0L3endS391
) {
  int32_t _if__result_2127;
  int32_t _M0L3lenS393;
  int32_t _M0L6_2atmpS1360;
  moonbit_bytes_t _M0L5bytesS394;
  moonbit_bytes_t _M0L6_2atmpS1359;
  moonbit_string_t _result_2128;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS390 == 0) {
    int32_t _M0L6_2atmpS1358 = Moonbit_array_length(_M0L3strS392);
    _if__result_2127 = _M0L3endS391 == _M0L6_2atmpS1358;
  } else {
    _if__result_2127 = 0;
  }
  if (_if__result_2127) {
    moonbit_incref_cycle_free(_M0L3strS392);
    return _M0L3strS392;
  }
  _M0L3lenS393 = _M0L3endS391 - _M0L5startS390;
  _M0L6_2atmpS1360 = _M0L3lenS393 * 2;
  _M0L5bytesS394 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1360, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS394, 0, _M0L3strS392, _M0L5startS390, _M0L3lenS393);
  _M0L6_2atmpS1359 = _M0L5bytesS394;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2128
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1359, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1359);
  return _result_2128;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS385,
  int32_t _M0L6offsetS389,
  int64_t _M0L6lengthS387
) {
  int32_t _M0L3lenS384;
  int32_t _M0L6lengthS386;
  int32_t _if__result_2129;
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L3lenS384 = Moonbit_array_length(_M0L4selfS385);
  if (_M0L6lengthS387 == 4294967296ll) {
    _M0L6lengthS386 = _M0L3lenS384 - _M0L6offsetS389;
  } else {
    int64_t _M0L7_2aSomeS388 = _M0L6lengthS387;
    _M0L6lengthS386 = (int32_t)_M0L7_2aSomeS388;
  }
  if (_M0L6offsetS389 >= 0) {
    if (_M0L6lengthS386 >= 0) {
      int32_t _M0L6_2atmpS1357 = _M0L6offsetS389 + _M0L6lengthS386;
      _if__result_2129 = _M0L6_2atmpS1357 <= _M0L3lenS384;
    } else {
      _if__result_2129 = 0;
    }
  } else {
    _if__result_2129 = 0;
  }
  if (_if__result_2129) {
    moonbit_incref_cycle_free(_M0L4selfS385);
    #line 85 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    return _M0FPB19unsafe__sub__string(_M0L4selfS385, _M0L6offsetS389, _M0L6lengthS386);
  } else {
    #line 84 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array10FixedArray18blit__from__string(
  moonbit_bytes_t _M0L4selfS376,
  int32_t _M0L13bytes__offsetS371,
  moonbit_string_t _M0L3strS378,
  int32_t _M0L11str__offsetS374,
  int32_t _M0L6lengthS372
) {
  int32_t _M0L6_2atmpS1356;
  int32_t _M0L6_2atmpS1355;
  int32_t _M0L2e1S370;
  int32_t _M0L6_2atmpS1354;
  int32_t _M0L2e2S373;
  int32_t _M0L4len1S375;
  int32_t _M0L4len2S377;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1356 = _M0L6lengthS372 * 2;
  _M0L6_2atmpS1355 = _M0L13bytes__offsetS371 + _M0L6_2atmpS1356;
  _M0L2e1S370 = _M0L6_2atmpS1355 - 1;
  _M0L6_2atmpS1354 = _M0L11str__offsetS374 + _M0L6lengthS372;
  _M0L2e2S373 = _M0L6_2atmpS1354 - 1;
  _M0L4len1S375 = Moonbit_array_length(_M0L4selfS376);
  _M0L4len2S377 = Moonbit_array_length(_M0L3strS378);
  if (
    _M0L6lengthS372 >= 0
    && _M0L13bytes__offsetS371 >= 0
    && _M0L2e1S370 < _M0L4len1S375
    && _M0L11str__offsetS374 >= 0
    && _M0L2e2S373 < _M0L4len2S377
  ) {
    int32_t _M0L16end__str__offsetS379 =
      _M0L11str__offsetS374 + _M0L6lengthS372;
    int32_t _M0L1iS380 = _M0L11str__offsetS374;
    int32_t _M0L1jS381 = _M0L13bytes__offsetS371;
    while (1) {
      if (_M0L1iS380 < _M0L16end__str__offsetS379) {
        int32_t _M0L6_2atmpS1351 = _M0L3strS378[_M0L1iS380];
        int32_t _M0L6_2atmpS1350 = (int32_t)_M0L6_2atmpS1351;
        uint32_t _M0L1cS382 = *(uint32_t*)&_M0L6_2atmpS1350;
        uint32_t _M0L6_2atmpS1346 = _M0L1cS382 & 255u;
        int32_t _M0L6_2atmpS1345;
        int32_t _M0L6_2atmpS1347;
        uint32_t _M0L6_2atmpS1349;
        int32_t _M0L6_2atmpS1348;
        int32_t _M0L6_2atmpS1352;
        int32_t _M0L6_2atmpS1353;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1345 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1346);
        if (
          _M0L1jS381 < 0 || _M0L1jS381 >= Moonbit_array_length(_M0L4selfS376)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS376[_M0L1jS381] = _M0L6_2atmpS1345;
        _M0L6_2atmpS1347 = _M0L1jS381 + 1;
        _M0L6_2atmpS1349 = _M0L1cS382 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1348 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1349);
        if (
          _M0L6_2atmpS1347 < 0
          || _M0L6_2atmpS1347 >= Moonbit_array_length(_M0L4selfS376)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS376[_M0L6_2atmpS1347] = _M0L6_2atmpS1348;
        _M0L6_2atmpS1352 = _M0L1iS380 + 1;
        _M0L6_2atmpS1353 = _M0L1jS381 + 2;
        _M0L1iS380 = _M0L6_2atmpS1352;
        _M0L1jS381 = _M0L6_2atmpS1353;
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

int32_t _M0MPC14uint4UInt8to__byte(uint32_t _M0L4selfS369) {
  int32_t _M0L6_2atmpS1344;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1344 = *(int32_t*)&_M0L4selfS369;
  return _M0L6_2atmpS1344 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS361,
  int32_t _M0L5radixS360
) {
  uint16_t* _M0L6bufferS362;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS360 < 2 || _M0L5radixS360 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_16.data);
  }
  if (_M0L4selfS361 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  switch (_M0L5radixS360) {
    case 10: {
      int32_t _M0L3lenS363;
      uint16_t* _M0L6bufferS364;
      #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS363 = _M0FPB12dec__count64(_M0L4selfS361);
      _M0L6bufferS364 = (uint16_t*)moonbit_make_string(_M0L3lenS363, 0);
      #line 624 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS364, _M0L4selfS361, 0, _M0L3lenS363);
      _M0L6bufferS362 = _M0L6bufferS364;
      break;
    }
    
    case 16: {
      int32_t _M0L3lenS365;
      uint16_t* _M0L6bufferS366;
      #line 628 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS365 = _M0FPB12hex__count64(_M0L4selfS361);
      _M0L6bufferS366 = (uint16_t*)moonbit_make_string(_M0L3lenS365, 0);
      #line 630 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS366, _M0L4selfS361, 0, _M0L3lenS365);
      _M0L6bufferS362 = _M0L6bufferS366;
      break;
    }
    default: {
      int32_t _M0L3lenS367;
      uint16_t* _M0L6bufferS368;
      #line 634 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS367 = _M0FPB14radix__count64(_M0L4selfS361, _M0L5radixS360);
      _M0L6bufferS368 = (uint16_t*)moonbit_make_string(_M0L3lenS367, 0);
      #line 636 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS368, _M0L4selfS361, 0, _M0L3lenS367, _M0L5radixS360);
      _M0L6bufferS362 = _M0L6bufferS368;
      break;
    }
  }
  return _M0L6bufferS362;
}

moonbit_string_t _M0MPC15int645Int6418to__string_2einner(
  int64_t _M0L4selfS344,
  int32_t _M0L5radixS343
) {
  int32_t _M0L12is__negativeS345;
  uint64_t _M0L3numS346;
  uint16_t* _M0L6bufferS347;
  #line 548 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS343 < 2 || _M0L5radixS343 > 36) {
    #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_16.data);
  }
  if (_M0L4selfS344 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  _M0L12is__negativeS345 = _M0L4selfS344 < 0ll;
  if (_M0L12is__negativeS345) {
    int64_t _M0L6_2atmpS1343 = -_M0L4selfS344;
    _M0L3numS346 = *(uint64_t*)&_M0L6_2atmpS1343;
  } else {
    _M0L3numS346 = *(uint64_t*)&_M0L4selfS344;
  }
  switch (_M0L5radixS343) {
    case 10: {
      int32_t _M0L10digit__lenS348;
      int32_t _M0L6_2atmpS1340;
      int32_t _M0L10total__lenS349;
      uint16_t* _M0L6bufferS350;
      int32_t _M0L12digit__startS351;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS348 = _M0FPB12dec__count64(_M0L3numS346);
      if (_M0L12is__negativeS345) {
        _M0L6_2atmpS1340 = 1;
      } else {
        _M0L6_2atmpS1340 = 0;
      }
      _M0L10total__lenS349 = _M0L10digit__lenS348 + _M0L6_2atmpS1340;
      _M0L6bufferS350
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS349, 0);
      if (_M0L12is__negativeS345) {
        _M0L12digit__startS351 = 1;
      } else {
        _M0L12digit__startS351 = 0;
      }
      #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS350, _M0L3numS346, _M0L12digit__startS351, _M0L10total__lenS349);
      _M0L6bufferS347 = _M0L6bufferS350;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS352;
      int32_t _M0L6_2atmpS1341;
      int32_t _M0L10total__lenS353;
      uint16_t* _M0L6bufferS354;
      int32_t _M0L12digit__startS355;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS352 = _M0FPB12hex__count64(_M0L3numS346);
      if (_M0L12is__negativeS345) {
        _M0L6_2atmpS1341 = 1;
      } else {
        _M0L6_2atmpS1341 = 0;
      }
      _M0L10total__lenS353 = _M0L10digit__lenS352 + _M0L6_2atmpS1341;
      _M0L6bufferS354
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS353, 0);
      if (_M0L12is__negativeS345) {
        _M0L12digit__startS355 = 1;
      } else {
        _M0L12digit__startS355 = 0;
      }
      #line 585 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS354, _M0L3numS346, _M0L12digit__startS355, _M0L10total__lenS353);
      _M0L6bufferS347 = _M0L6bufferS354;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS356;
      int32_t _M0L6_2atmpS1342;
      int32_t _M0L10total__lenS357;
      uint16_t* _M0L6bufferS358;
      int32_t _M0L12digit__startS359;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS356
      = _M0FPB14radix__count64(_M0L3numS346, _M0L5radixS343);
      if (_M0L12is__negativeS345) {
        _M0L6_2atmpS1342 = 1;
      } else {
        _M0L6_2atmpS1342 = 0;
      }
      _M0L10total__lenS357 = _M0L10digit__lenS356 + _M0L6_2atmpS1342;
      _M0L6bufferS358
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS357, 0);
      if (_M0L12is__negativeS345) {
        _M0L12digit__startS359 = 1;
      } else {
        _M0L12digit__startS359 = 0;
      }
      #line 593 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS358, _M0L3numS346, _M0L12digit__startS359, _M0L10total__lenS357, _M0L5radixS343);
      _M0L6bufferS347 = _M0L6bufferS358;
      break;
    }
  }
  if (_M0L12is__negativeS345) {
    _M0L6bufferS347[0] = 45;
  }
  return _M0L6bufferS347;
}

int32_t _M0FPB22int64__to__string__dec(
  uint16_t* _M0L6bufferS329,
  uint64_t _M0L3numS341,
  int32_t _M0L12digit__startS330,
  int32_t _M0L10total__lenS342
) {
  int32_t _M0L6_2atmpS1339;
  uint64_t _M0L3numS319;
  int32_t _M0L6offsetS320;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1339 = _M0L10total__lenS342 - _M0L12digit__startS330;
  _M0L3numS319 = _M0L3numS341;
  _M0L6offsetS320 = _M0L6_2atmpS1339;
  while (1) {
    if (_M0L3numS319 >= 10000ull) {
      uint64_t _M0L1tS321 = _M0L3numS319 / 10000ull;
      uint64_t _M0L6_2atmpS1316 = _M0L3numS319 % 10000ull;
      int32_t _M0L1rS322 = (int32_t)_M0L6_2atmpS1316;
      int32_t _M0L2d1S323 = _M0L1rS322 / 100;
      int32_t _M0L2d2S324 = _M0L1rS322 % 100;
      int32_t _M0L6_2atmpS1315 = _M0L2d1S323 / 10;
      int32_t _M0L6_2atmpS1314 = 48 + _M0L6_2atmpS1315;
      int32_t _M0L6d1__hiS325 = (uint16_t)_M0L6_2atmpS1314;
      int32_t _M0L6_2atmpS1313 = _M0L2d1S323 % 10;
      int32_t _M0L6_2atmpS1312 = 48 + _M0L6_2atmpS1313;
      int32_t _M0L6d1__loS326 = (uint16_t)_M0L6_2atmpS1312;
      int32_t _M0L6_2atmpS1311 = _M0L2d2S324 / 10;
      int32_t _M0L6_2atmpS1310 = 48 + _M0L6_2atmpS1311;
      int32_t _M0L6d2__hiS327 = (uint16_t)_M0L6_2atmpS1310;
      int32_t _M0L6_2atmpS1309 = _M0L2d2S324 % 10;
      int32_t _M0L6_2atmpS1308 = 48 + _M0L6_2atmpS1309;
      int32_t _M0L6d2__loS328 = (uint16_t)_M0L6_2atmpS1308;
      int32_t _M0L6_2atmpS1300 = _M0L12digit__startS330 + _M0L6offsetS320;
      int32_t _M0L6_2atmpS1299 = _M0L6_2atmpS1300 - 4;
      int32_t _M0L6_2atmpS1302;
      int32_t _M0L6_2atmpS1301;
      int32_t _M0L6_2atmpS1304;
      int32_t _M0L6_2atmpS1303;
      int32_t _M0L6_2atmpS1306;
      int32_t _M0L6_2atmpS1305;
      int32_t _M0L6_2atmpS1307;
      _M0L6bufferS329[_M0L6_2atmpS1299] = _M0L6d1__hiS325;
      _M0L6_2atmpS1302 = _M0L12digit__startS330 + _M0L6offsetS320;
      _M0L6_2atmpS1301 = _M0L6_2atmpS1302 - 3;
      _M0L6bufferS329[_M0L6_2atmpS1301] = _M0L6d1__loS326;
      _M0L6_2atmpS1304 = _M0L12digit__startS330 + _M0L6offsetS320;
      _M0L6_2atmpS1303 = _M0L6_2atmpS1304 - 2;
      _M0L6bufferS329[_M0L6_2atmpS1303] = _M0L6d2__hiS327;
      _M0L6_2atmpS1306 = _M0L12digit__startS330 + _M0L6offsetS320;
      _M0L6_2atmpS1305 = _M0L6_2atmpS1306 - 1;
      _M0L6bufferS329[_M0L6_2atmpS1305] = _M0L6d2__loS328;
      _M0L6_2atmpS1307 = _M0L6offsetS320 - 4;
      _M0L3numS319 = _M0L1tS321;
      _M0L6offsetS320 = _M0L6_2atmpS1307;
      continue;
    } else {
      int32_t _M0L6_2atmpS1338 = (int32_t)_M0L3numS319;
      int32_t _M0L9remainingS332 = _M0L6_2atmpS1338;
      int32_t _M0L6offsetS333 = _M0L6offsetS320;
      while (1) {
        if (_M0L9remainingS332 >= 100) {
          int32_t _M0L1tS334 = _M0L9remainingS332 / 100;
          int32_t _M0L1dS335 = _M0L9remainingS332 % 100;
          int32_t _M0L6_2atmpS1325 = _M0L1dS335 / 10;
          int32_t _M0L6_2atmpS1324 = 48 + _M0L6_2atmpS1325;
          int32_t _M0L5d__hiS336 = (uint16_t)_M0L6_2atmpS1324;
          int32_t _M0L6_2atmpS1323 = _M0L1dS335 % 10;
          int32_t _M0L6_2atmpS1322 = 48 + _M0L6_2atmpS1323;
          int32_t _M0L5d__loS337 = (uint16_t)_M0L6_2atmpS1322;
          int32_t _M0L6_2atmpS1318 = _M0L12digit__startS330 + _M0L6offsetS333;
          int32_t _M0L6_2atmpS1317 = _M0L6_2atmpS1318 - 2;
          int32_t _M0L6_2atmpS1320;
          int32_t _M0L6_2atmpS1319;
          int32_t _M0L6_2atmpS1321;
          _M0L6bufferS329[_M0L6_2atmpS1317] = _M0L5d__hiS336;
          _M0L6_2atmpS1320 = _M0L12digit__startS330 + _M0L6offsetS333;
          _M0L6_2atmpS1319 = _M0L6_2atmpS1320 - 1;
          _M0L6bufferS329[_M0L6_2atmpS1319] = _M0L5d__loS337;
          _M0L6_2atmpS1321 = _M0L6offsetS333 - 2;
          _M0L9remainingS332 = _M0L1tS334;
          _M0L6offsetS333 = _M0L6_2atmpS1321;
          continue;
        } else if (_M0L9remainingS332 >= 10) {
          int32_t _M0L6_2atmpS1333 = _M0L9remainingS332 / 10;
          int32_t _M0L6_2atmpS1332 = 48 + _M0L6_2atmpS1333;
          int32_t _M0L5d__hiS339 = (uint16_t)_M0L6_2atmpS1332;
          int32_t _M0L6_2atmpS1331 = _M0L9remainingS332 % 10;
          int32_t _M0L6_2atmpS1330 = 48 + _M0L6_2atmpS1331;
          int32_t _M0L5d__loS340 = (uint16_t)_M0L6_2atmpS1330;
          int32_t _M0L6_2atmpS1327 = _M0L12digit__startS330 + _M0L6offsetS333;
          int32_t _M0L6_2atmpS1326 = _M0L6_2atmpS1327 - 2;
          int32_t _M0L6_2atmpS1329;
          int32_t _M0L6_2atmpS1328;
          _M0L6bufferS329[_M0L6_2atmpS1326] = _M0L5d__hiS339;
          _M0L6_2atmpS1329 = _M0L12digit__startS330 + _M0L6offsetS333;
          _M0L6_2atmpS1328 = _M0L6_2atmpS1329 - 1;
          _M0L6bufferS329[_M0L6_2atmpS1328] = _M0L5d__loS340;
        } else {
          int32_t _M0L6_2atmpS1337 = _M0L12digit__startS330 + _M0L6offsetS333;
          int32_t _M0L6_2atmpS1334 = _M0L6_2atmpS1337 - 1;
          int32_t _M0L6_2atmpS1336 = 48 + _M0L9remainingS332;
          int32_t _M0L6_2atmpS1335 = (uint16_t)_M0L6_2atmpS1336;
          _M0L6bufferS329[_M0L6_2atmpS1334] = _M0L6_2atmpS1335;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB26int64__to__string__generic(
  uint16_t* _M0L6bufferS309,
  uint64_t _M0L3numS313,
  int32_t _M0L12digit__startS310,
  int32_t _M0L10total__lenS312,
  int32_t _M0L5radixS303
) {
  uint64_t _M0L4baseS302;
  int32_t _M0L6_2atmpS1284;
  int32_t _M0L6_2atmpS1283;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS302 = _M0MPC13int3Int10to__uint64(_M0L5radixS303);
  _M0L6_2atmpS1284 = _M0L5radixS303 - 1;
  _M0L6_2atmpS1283 = _M0L5radixS303 & _M0L6_2atmpS1284;
  if (_M0L6_2atmpS1283 == 0) {
    int32_t _M0L5shiftS304;
    uint64_t _M0L4maskS305;
    int32_t _M0L6_2atmpS1291;
    int32_t _M0L6offsetS306;
    uint64_t _M0L1nS307;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS304 = moonbit_ctz32(_M0L5radixS303);
    _M0L4maskS305 = _M0L4baseS302 - 1ull;
    _M0L6_2atmpS1291 = _M0L10total__lenS312 - _M0L12digit__startS310;
    _M0L6offsetS306 = _M0L6_2atmpS1291;
    _M0L1nS307 = _M0L3numS313;
    while (1) {
      if (_M0L1nS307 > 0ull) {
        uint64_t _M0L6_2atmpS1290 = _M0L1nS307 & _M0L4maskS305;
        int32_t _M0L5digitS308 = (int32_t)_M0L6_2atmpS1290;
        int32_t _M0L6_2atmpS1287 = _M0L12digit__startS310 + _M0L6offsetS306;
        int32_t _M0L6_2atmpS1285 = _M0L6_2atmpS1287 - 1;
        int32_t _M0L6_2atmpS1286 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS308];
        int32_t _M0L6_2atmpS1288;
        uint64_t _M0L6_2atmpS1289;
        _M0L6bufferS309[_M0L6_2atmpS1285] = _M0L6_2atmpS1286;
        _M0L6_2atmpS1288 = _M0L6offsetS306 - 1;
        _M0L6_2atmpS1289 = _M0L1nS307 >> (_M0L5shiftS304 & 63);
        _M0L6offsetS306 = _M0L6_2atmpS1288;
        _M0L1nS307 = _M0L6_2atmpS1289;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1298 = _M0L10total__lenS312 - _M0L12digit__startS310;
    int32_t _M0L6offsetS314 = _M0L6_2atmpS1298;
    uint64_t _M0L1nS315 = _M0L3numS313;
    while (1) {
      if (_M0L1nS315 > 0ull) {
        uint64_t _M0L1qS316 = _M0L1nS315 / _M0L4baseS302;
        uint64_t _M0L6_2atmpS1297 = _M0L1qS316 * _M0L4baseS302;
        uint64_t _M0L6_2atmpS1296 = _M0L1nS315 - _M0L6_2atmpS1297;
        int32_t _M0L5digitS317 = (int32_t)_M0L6_2atmpS1296;
        int32_t _M0L6_2atmpS1294 = _M0L12digit__startS310 + _M0L6offsetS314;
        int32_t _M0L6_2atmpS1292 = _M0L6_2atmpS1294 - 1;
        int32_t _M0L6_2atmpS1293 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS317];
        int32_t _M0L6_2atmpS1295;
        _M0L6bufferS309[_M0L6_2atmpS1292] = _M0L6_2atmpS1293;
        _M0L6_2atmpS1295 = _M0L6offsetS314 - 1;
        _M0L6offsetS314 = _M0L6_2atmpS1295;
        _M0L1nS315 = _M0L1qS316;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB22int64__to__string__hex(
  uint16_t* _M0L6bufferS296,
  uint64_t _M0L3numS301,
  int32_t _M0L12digit__startS297,
  int32_t _M0L10total__lenS300
) {
  int32_t _M0L6_2atmpS1282;
  int32_t _M0L6offsetS291;
  uint64_t _M0L1nS292;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1282 = _M0L10total__lenS300 - _M0L12digit__startS297;
  _M0L6offsetS291 = _M0L6_2atmpS1282;
  _M0L1nS292 = _M0L3numS301;
  while (1) {
    if (_M0L6offsetS291 >= 2) {
      uint64_t _M0L6_2atmpS1279 = _M0L1nS292 & 255ull;
      int32_t _M0L9byte__valS293 = (int32_t)_M0L6_2atmpS1279;
      int32_t _M0L2hiS294 = _M0L9byte__valS293 / 16;
      int32_t _M0L2loS295 = _M0L9byte__valS293 % 16;
      int32_t _M0L6_2atmpS1273 = _M0L12digit__startS297 + _M0L6offsetS291;
      int32_t _M0L6_2atmpS1271 = _M0L6_2atmpS1273 - 2;
      int32_t _M0L6_2atmpS1272 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L2hiS294];
      int32_t _M0L6_2atmpS1276;
      int32_t _M0L6_2atmpS1274;
      int32_t _M0L6_2atmpS1275;
      int32_t _M0L6_2atmpS1277;
      uint64_t _M0L6_2atmpS1278;
      _M0L6bufferS296[_M0L6_2atmpS1271] = _M0L6_2atmpS1272;
      _M0L6_2atmpS1276 = _M0L12digit__startS297 + _M0L6offsetS291;
      _M0L6_2atmpS1274 = _M0L6_2atmpS1276 - 1;
      _M0L6_2atmpS1275
      = ((moonbit_string_t)moonbit_string_literal_17.data)[
        _M0L2loS295
      ];
      _M0L6bufferS296[_M0L6_2atmpS1274] = _M0L6_2atmpS1275;
      _M0L6_2atmpS1277 = _M0L6offsetS291 - 2;
      _M0L6_2atmpS1278 = _M0L1nS292 >> 8;
      _M0L6offsetS291 = _M0L6_2atmpS1277;
      _M0L1nS292 = _M0L6_2atmpS1278;
      continue;
    } else if (_M0L6offsetS291 == 1) {
      uint64_t _M0L6_2atmpS1281 = _M0L1nS292 & 15ull;
      int32_t _M0L6nibbleS299 = (int32_t)_M0L6_2atmpS1281;
      int32_t _M0L6_2atmpS1280 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L6nibbleS299];
      _M0L6bufferS296[_M0L12digit__startS297] = _M0L6_2atmpS1280;
    }
    break;
  }
  return 0;
}

int32_t _M0FPB14radix__count64(
  uint64_t _M0L5valueS285,
  int32_t _M0L5radixS287
) {
  uint64_t _M0L4baseS286;
  uint64_t _M0L3numS288;
  int32_t _M0L5countS289;
  #line 419 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS285 == 0ull) {
    return 1;
  }
  #line 424 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS286 = _M0MPC13int3Int10to__uint64(_M0L5radixS287);
  _M0L3numS288 = _M0L5valueS285;
  _M0L5countS289 = 0;
  while (1) {
    if (_M0L3numS288 > 0ull) {
      uint64_t _M0L6_2atmpS1269 = _M0L3numS288 / _M0L4baseS286;
      int32_t _M0L6_2atmpS1270 = _M0L5countS289 + 1;
      _M0L3numS288 = _M0L6_2atmpS1269;
      _M0L5countS289 = _M0L6_2atmpS1270;
      continue;
    } else {
      return _M0L5countS289;
    }
    break;
  }
}

int32_t _M0FPB12hex__count64(uint64_t _M0L5valueS283) {
  #line 407 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS283 == 0ull) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS284;
    int32_t _M0L6_2atmpS1268;
    int32_t _M0L6_2atmpS1267;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS284 = moonbit_clz64(_M0L5valueS283);
    _M0L6_2atmpS1268 = 63 - _M0L14leading__zerosS284;
    _M0L6_2atmpS1267 = _M0L6_2atmpS1268 / 4;
    return _M0L6_2atmpS1267 + 1;
  }
}

int32_t _M0FPB12dec__count64(uint64_t _M0L5valueS282) {
  #line 343 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS282 >= 10000000000ull) {
    if (_M0L5valueS282 >= 100000000000000ull) {
      if (_M0L5valueS282 >= 10000000000000000ull) {
        if (_M0L5valueS282 >= 1000000000000000000ull) {
          if (_M0L5valueS282 >= 10000000000000000000ull) {
            return 20;
          } else {
            return 19;
          }
        } else if (_M0L5valueS282 >= 100000000000000000ull) {
          return 18;
        } else {
          return 17;
        }
      } else if (_M0L5valueS282 >= 1000000000000000ull) {
        return 16;
      } else {
        return 15;
      }
    } else if (_M0L5valueS282 >= 1000000000000ull) {
      if (_M0L5valueS282 >= 10000000000000ull) {
        return 14;
      } else {
        return 13;
      }
    } else if (_M0L5valueS282 >= 100000000000ull) {
      return 12;
    } else {
      return 11;
    }
  } else if (_M0L5valueS282 >= 100000ull) {
    if (_M0L5valueS282 >= 10000000ull) {
      if (_M0L5valueS282 >= 1000000000ull) {
        return 10;
      } else if (_M0L5valueS282 >= 100000000ull) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS282 >= 1000000ull) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS282 >= 1000ull) {
    if (_M0L5valueS282 >= 10000ull) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS282 >= 100ull) {
    return 3;
  } else if (_M0L5valueS282 >= 10ull) {
    return 2;
  } else {
    return 1;
  }
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS266,
  int32_t _M0L5radixS265
) {
  int32_t _M0L12is__negativeS267;
  uint32_t _M0L3numS268;
  uint16_t* _M0L6bufferS269;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS265 < 2 || _M0L5radixS265 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_16.data);
  }
  if (_M0L4selfS266 == 0) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  _M0L12is__negativeS267 = _M0L4selfS266 < 0;
  if (_M0L12is__negativeS267) {
    int32_t _M0L6_2atmpS1266 = -_M0L4selfS266;
    _M0L3numS268 = *(uint32_t*)&_M0L6_2atmpS1266;
  } else {
    _M0L3numS268 = *(uint32_t*)&_M0L4selfS266;
  }
  switch (_M0L5radixS265) {
    case 10: {
      int32_t _M0L10digit__lenS270;
      int32_t _M0L6_2atmpS1263;
      int32_t _M0L10total__lenS271;
      uint16_t* _M0L6bufferS272;
      int32_t _M0L12digit__startS273;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS270 = _M0FPB12dec__count32(_M0L3numS268);
      if (_M0L12is__negativeS267) {
        _M0L6_2atmpS1263 = 1;
      } else {
        _M0L6_2atmpS1263 = 0;
      }
      _M0L10total__lenS271 = _M0L10digit__lenS270 + _M0L6_2atmpS1263;
      _M0L6bufferS272
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS271, 0);
      if (_M0L12is__negativeS267) {
        _M0L12digit__startS273 = 1;
      } else {
        _M0L12digit__startS273 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS272, _M0L3numS268, _M0L12digit__startS273, _M0L10total__lenS271);
      _M0L6bufferS269 = _M0L6bufferS272;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS274;
      int32_t _M0L6_2atmpS1264;
      int32_t _M0L10total__lenS275;
      uint16_t* _M0L6bufferS276;
      int32_t _M0L12digit__startS277;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS274 = _M0FPB12hex__count32(_M0L3numS268);
      if (_M0L12is__negativeS267) {
        _M0L6_2atmpS1264 = 1;
      } else {
        _M0L6_2atmpS1264 = 0;
      }
      _M0L10total__lenS275 = _M0L10digit__lenS274 + _M0L6_2atmpS1264;
      _M0L6bufferS276
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS275, 0);
      if (_M0L12is__negativeS267) {
        _M0L12digit__startS277 = 1;
      } else {
        _M0L12digit__startS277 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS276, _M0L3numS268, _M0L12digit__startS277, _M0L10total__lenS275);
      _M0L6bufferS269 = _M0L6bufferS276;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS278;
      int32_t _M0L6_2atmpS1265;
      int32_t _M0L10total__lenS279;
      uint16_t* _M0L6bufferS280;
      int32_t _M0L12digit__startS281;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS278
      = _M0FPB14radix__count32(_M0L3numS268, _M0L5radixS265);
      if (_M0L12is__negativeS267) {
        _M0L6_2atmpS1265 = 1;
      } else {
        _M0L6_2atmpS1265 = 0;
      }
      _M0L10total__lenS279 = _M0L10digit__lenS278 + _M0L6_2atmpS1265;
      _M0L6bufferS280
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS279, 0);
      if (_M0L12is__negativeS267) {
        _M0L12digit__startS281 = 1;
      } else {
        _M0L12digit__startS281 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS280, _M0L3numS268, _M0L12digit__startS281, _M0L10total__lenS279, _M0L5radixS265);
      _M0L6bufferS269 = _M0L6bufferS280;
      break;
    }
  }
  if (_M0L12is__negativeS267) {
    _M0L6bufferS269[0] = 45;
  }
  return _M0L6bufferS269;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS259,
  int32_t _M0L5radixS261
) {
  uint32_t _M0L4baseS260;
  uint32_t _M0L3numS262;
  int32_t _M0L5countS263;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS259 == 0u) {
    return 1;
  }
  _M0L4baseS260 = *(uint32_t*)&_M0L5radixS261;
  _M0L3numS262 = _M0L5valueS259;
  _M0L5countS263 = 0;
  while (1) {
    if (_M0L3numS262 > 0u) {
      uint32_t _M0L6_2atmpS1261 = _M0L3numS262 / _M0L4baseS260;
      int32_t _M0L6_2atmpS1262 = _M0L5countS263 + 1;
      _M0L3numS262 = _M0L6_2atmpS1261;
      _M0L5countS263 = _M0L6_2atmpS1262;
      continue;
    } else {
      return _M0L5countS263;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS257) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS257 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS258;
    int32_t _M0L6_2atmpS1260;
    int32_t _M0L6_2atmpS1259;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS258 = moonbit_clz32(_M0L5valueS257);
    _M0L6_2atmpS1260 = 31 - _M0L14leading__zerosS258;
    _M0L6_2atmpS1259 = _M0L6_2atmpS1260 / 4;
    return _M0L6_2atmpS1259 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS256) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS256 >= 100000u) {
    if (_M0L5valueS256 >= 10000000u) {
      if (_M0L5valueS256 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS256 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS256 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS256 >= 1000u) {
    if (_M0L5valueS256 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS256 >= 100u) {
    return 3;
  } else if (_M0L5valueS256 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS242,
  uint32_t _M0L3numS254,
  int32_t _M0L12digit__startS243,
  int32_t _M0L10total__lenS255
) {
  int32_t _M0L6_2atmpS1258;
  uint32_t _M0L3numS232;
  int32_t _M0L6offsetS233;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1258 = _M0L10total__lenS255 - _M0L12digit__startS243;
  _M0L3numS232 = _M0L3numS254;
  _M0L6offsetS233 = _M0L6_2atmpS1258;
  while (1) {
    if (_M0L3numS232 >= 10000u) {
      uint32_t _M0L1tS234 = _M0L3numS232 / 10000u;
      uint32_t _M0L6_2atmpS1235 = _M0L3numS232 % 10000u;
      int32_t _M0L1rS235 = *(int32_t*)&_M0L6_2atmpS1235;
      int32_t _M0L2d1S236 = _M0L1rS235 / 100;
      int32_t _M0L2d2S237 = _M0L1rS235 % 100;
      int32_t _M0L6_2atmpS1234 = _M0L2d1S236 / 10;
      int32_t _M0L6_2atmpS1233 = 48 + _M0L6_2atmpS1234;
      int32_t _M0L6d1__hiS238 = (uint16_t)_M0L6_2atmpS1233;
      int32_t _M0L6_2atmpS1232 = _M0L2d1S236 % 10;
      int32_t _M0L6_2atmpS1231 = 48 + _M0L6_2atmpS1232;
      int32_t _M0L6d1__loS239 = (uint16_t)_M0L6_2atmpS1231;
      int32_t _M0L6_2atmpS1230 = _M0L2d2S237 / 10;
      int32_t _M0L6_2atmpS1229 = 48 + _M0L6_2atmpS1230;
      int32_t _M0L6d2__hiS240 = (uint16_t)_M0L6_2atmpS1229;
      int32_t _M0L6_2atmpS1228 = _M0L2d2S237 % 10;
      int32_t _M0L6_2atmpS1227 = 48 + _M0L6_2atmpS1228;
      int32_t _M0L6d2__loS241 = (uint16_t)_M0L6_2atmpS1227;
      int32_t _M0L6_2atmpS1219 = _M0L12digit__startS243 + _M0L6offsetS233;
      int32_t _M0L6_2atmpS1218 = _M0L6_2atmpS1219 - 4;
      int32_t _M0L6_2atmpS1221;
      int32_t _M0L6_2atmpS1220;
      int32_t _M0L6_2atmpS1223;
      int32_t _M0L6_2atmpS1222;
      int32_t _M0L6_2atmpS1225;
      int32_t _M0L6_2atmpS1224;
      int32_t _M0L6_2atmpS1226;
      _M0L6bufferS242[_M0L6_2atmpS1218] = _M0L6d1__hiS238;
      _M0L6_2atmpS1221 = _M0L12digit__startS243 + _M0L6offsetS233;
      _M0L6_2atmpS1220 = _M0L6_2atmpS1221 - 3;
      _M0L6bufferS242[_M0L6_2atmpS1220] = _M0L6d1__loS239;
      _M0L6_2atmpS1223 = _M0L12digit__startS243 + _M0L6offsetS233;
      _M0L6_2atmpS1222 = _M0L6_2atmpS1223 - 2;
      _M0L6bufferS242[_M0L6_2atmpS1222] = _M0L6d2__hiS240;
      _M0L6_2atmpS1225 = _M0L12digit__startS243 + _M0L6offsetS233;
      _M0L6_2atmpS1224 = _M0L6_2atmpS1225 - 1;
      _M0L6bufferS242[_M0L6_2atmpS1224] = _M0L6d2__loS241;
      _M0L6_2atmpS1226 = _M0L6offsetS233 - 4;
      _M0L3numS232 = _M0L1tS234;
      _M0L6offsetS233 = _M0L6_2atmpS1226;
      continue;
    } else {
      int32_t _M0L6_2atmpS1257 = *(int32_t*)&_M0L3numS232;
      int32_t _M0L9remainingS245 = _M0L6_2atmpS1257;
      int32_t _M0L6offsetS246 = _M0L6offsetS233;
      while (1) {
        if (_M0L9remainingS245 >= 100) {
          int32_t _M0L1tS247 = _M0L9remainingS245 / 100;
          int32_t _M0L1dS248 = _M0L9remainingS245 % 100;
          int32_t _M0L6_2atmpS1244 = _M0L1dS248 / 10;
          int32_t _M0L6_2atmpS1243 = 48 + _M0L6_2atmpS1244;
          int32_t _M0L5d__hiS249 = (uint16_t)_M0L6_2atmpS1243;
          int32_t _M0L6_2atmpS1242 = _M0L1dS248 % 10;
          int32_t _M0L6_2atmpS1241 = 48 + _M0L6_2atmpS1242;
          int32_t _M0L5d__loS250 = (uint16_t)_M0L6_2atmpS1241;
          int32_t _M0L6_2atmpS1237 = _M0L12digit__startS243 + _M0L6offsetS246;
          int32_t _M0L6_2atmpS1236 = _M0L6_2atmpS1237 - 2;
          int32_t _M0L6_2atmpS1239;
          int32_t _M0L6_2atmpS1238;
          int32_t _M0L6_2atmpS1240;
          _M0L6bufferS242[_M0L6_2atmpS1236] = _M0L5d__hiS249;
          _M0L6_2atmpS1239 = _M0L12digit__startS243 + _M0L6offsetS246;
          _M0L6_2atmpS1238 = _M0L6_2atmpS1239 - 1;
          _M0L6bufferS242[_M0L6_2atmpS1238] = _M0L5d__loS250;
          _M0L6_2atmpS1240 = _M0L6offsetS246 - 2;
          _M0L9remainingS245 = _M0L1tS247;
          _M0L6offsetS246 = _M0L6_2atmpS1240;
          continue;
        } else if (_M0L9remainingS245 >= 10) {
          int32_t _M0L6_2atmpS1252 = _M0L9remainingS245 / 10;
          int32_t _M0L6_2atmpS1251 = 48 + _M0L6_2atmpS1252;
          int32_t _M0L5d__hiS252 = (uint16_t)_M0L6_2atmpS1251;
          int32_t _M0L6_2atmpS1250 = _M0L9remainingS245 % 10;
          int32_t _M0L6_2atmpS1249 = 48 + _M0L6_2atmpS1250;
          int32_t _M0L5d__loS253 = (uint16_t)_M0L6_2atmpS1249;
          int32_t _M0L6_2atmpS1246 = _M0L12digit__startS243 + _M0L6offsetS246;
          int32_t _M0L6_2atmpS1245 = _M0L6_2atmpS1246 - 2;
          int32_t _M0L6_2atmpS1248;
          int32_t _M0L6_2atmpS1247;
          _M0L6bufferS242[_M0L6_2atmpS1245] = _M0L5d__hiS252;
          _M0L6_2atmpS1248 = _M0L12digit__startS243 + _M0L6offsetS246;
          _M0L6_2atmpS1247 = _M0L6_2atmpS1248 - 1;
          _M0L6bufferS242[_M0L6_2atmpS1247] = _M0L5d__loS253;
        } else {
          int32_t _M0L6_2atmpS1256 = _M0L12digit__startS243 + _M0L6offsetS246;
          int32_t _M0L6_2atmpS1253 = _M0L6_2atmpS1256 - 1;
          int32_t _M0L6_2atmpS1255 = 48 + _M0L9remainingS245;
          int32_t _M0L6_2atmpS1254 = (uint16_t)_M0L6_2atmpS1255;
          _M0L6bufferS242[_M0L6_2atmpS1253] = _M0L6_2atmpS1254;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS222,
  uint32_t _M0L3numS226,
  int32_t _M0L12digit__startS223,
  int32_t _M0L10total__lenS225,
  int32_t _M0L5radixS216
) {
  uint32_t _M0L4baseS215;
  int32_t _M0L6_2atmpS1203;
  int32_t _M0L6_2atmpS1202;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS215 = *(uint32_t*)&_M0L5radixS216;
  _M0L6_2atmpS1203 = _M0L5radixS216 - 1;
  _M0L6_2atmpS1202 = _M0L5radixS216 & _M0L6_2atmpS1203;
  if (_M0L6_2atmpS1202 == 0) {
    int32_t _M0L5shiftS217;
    uint32_t _M0L4maskS218;
    int32_t _M0L6_2atmpS1210;
    int32_t _M0L6offsetS219;
    uint32_t _M0L1nS220;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS217 = moonbit_ctz32(_M0L5radixS216);
    _M0L4maskS218 = _M0L4baseS215 - 1u;
    _M0L6_2atmpS1210 = _M0L10total__lenS225 - _M0L12digit__startS223;
    _M0L6offsetS219 = _M0L6_2atmpS1210;
    _M0L1nS220 = _M0L3numS226;
    while (1) {
      if (_M0L1nS220 > 0u) {
        uint32_t _M0L6_2atmpS1209 = _M0L1nS220 & _M0L4maskS218;
        int32_t _M0L5digitS221 = *(int32_t*)&_M0L6_2atmpS1209;
        int32_t _M0L6_2atmpS1206 = _M0L12digit__startS223 + _M0L6offsetS219;
        int32_t _M0L6_2atmpS1204 = _M0L6_2atmpS1206 - 1;
        int32_t _M0L6_2atmpS1205 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS221];
        int32_t _M0L6_2atmpS1207;
        uint32_t _M0L6_2atmpS1208;
        _M0L6bufferS222[_M0L6_2atmpS1204] = _M0L6_2atmpS1205;
        _M0L6_2atmpS1207 = _M0L6offsetS219 - 1;
        _M0L6_2atmpS1208 = _M0L1nS220 >> (_M0L5shiftS217 & 31);
        _M0L6offsetS219 = _M0L6_2atmpS1207;
        _M0L1nS220 = _M0L6_2atmpS1208;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1217 = _M0L10total__lenS225 - _M0L12digit__startS223;
    int32_t _M0L6offsetS227 = _M0L6_2atmpS1217;
    uint32_t _M0L1nS228 = _M0L3numS226;
    while (1) {
      if (_M0L1nS228 > 0u) {
        uint32_t _M0L1qS229 = _M0L1nS228 / _M0L4baseS215;
        uint32_t _M0L6_2atmpS1216 = _M0L1qS229 * _M0L4baseS215;
        uint32_t _M0L6_2atmpS1215 = _M0L1nS228 - _M0L6_2atmpS1216;
        int32_t _M0L5digitS230 = *(int32_t*)&_M0L6_2atmpS1215;
        int32_t _M0L6_2atmpS1213 = _M0L12digit__startS223 + _M0L6offsetS227;
        int32_t _M0L6_2atmpS1211 = _M0L6_2atmpS1213 - 1;
        int32_t _M0L6_2atmpS1212 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS230];
        int32_t _M0L6_2atmpS1214;
        _M0L6bufferS222[_M0L6_2atmpS1211] = _M0L6_2atmpS1212;
        _M0L6_2atmpS1214 = _M0L6offsetS227 - 1;
        _M0L6offsetS227 = _M0L6_2atmpS1214;
        _M0L1nS228 = _M0L1qS229;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS209,
  uint32_t _M0L3numS214,
  int32_t _M0L12digit__startS210,
  int32_t _M0L10total__lenS213
) {
  int32_t _M0L6_2atmpS1201;
  int32_t _M0L6offsetS204;
  uint32_t _M0L1nS205;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1201 = _M0L10total__lenS213 - _M0L12digit__startS210;
  _M0L6offsetS204 = _M0L6_2atmpS1201;
  _M0L1nS205 = _M0L3numS214;
  while (1) {
    if (_M0L6offsetS204 >= 2) {
      uint32_t _M0L6_2atmpS1198 = _M0L1nS205 & 255u;
      int32_t _M0L9byte__valS206 = *(int32_t*)&_M0L6_2atmpS1198;
      int32_t _M0L2hiS207 = _M0L9byte__valS206 / 16;
      int32_t _M0L2loS208 = _M0L9byte__valS206 % 16;
      int32_t _M0L6_2atmpS1192 = _M0L12digit__startS210 + _M0L6offsetS204;
      int32_t _M0L6_2atmpS1190 = _M0L6_2atmpS1192 - 2;
      int32_t _M0L6_2atmpS1191 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L2hiS207];
      int32_t _M0L6_2atmpS1195;
      int32_t _M0L6_2atmpS1193;
      int32_t _M0L6_2atmpS1194;
      int32_t _M0L6_2atmpS1196;
      uint32_t _M0L6_2atmpS1197;
      _M0L6bufferS209[_M0L6_2atmpS1190] = _M0L6_2atmpS1191;
      _M0L6_2atmpS1195 = _M0L12digit__startS210 + _M0L6offsetS204;
      _M0L6_2atmpS1193 = _M0L6_2atmpS1195 - 1;
      _M0L6_2atmpS1194
      = ((moonbit_string_t)moonbit_string_literal_17.data)[
        _M0L2loS208
      ];
      _M0L6bufferS209[_M0L6_2atmpS1193] = _M0L6_2atmpS1194;
      _M0L6_2atmpS1196 = _M0L6offsetS204 - 2;
      _M0L6_2atmpS1197 = _M0L1nS205 >> 8;
      _M0L6offsetS204 = _M0L6_2atmpS1196;
      _M0L1nS205 = _M0L6_2atmpS1197;
      continue;
    } else if (_M0L6offsetS204 == 1) {
      uint32_t _M0L6_2atmpS1200 = _M0L1nS205 & 15u;
      int32_t _M0L6nibbleS212 = *(int32_t*)&_M0L6_2atmpS1200;
      int32_t _M0L6_2atmpS1199 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L6nibbleS212];
      _M0L6bufferS209[_M0L12digit__startS210] = _M0L6_2atmpS1199;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS203
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS202;
  struct _M0TPB6Logger _M0L6_2atmpS1189;
  moonbit_string_t _result_2143;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS202 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS202);
  _M0L6_2atmpS1189
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS202
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS203, _M0L6_2atmpS1189);
  if (_M0L6_2atmpS1189.$1) {
    moonbit_decref(_M0L6_2atmpS1189.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2143 = _M0MPB13StringBuilder10to__string(_M0L6loggerS202);
  moonbit_decref_cycle_free(_M0L6loggerS202);
  return _result_2143;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS195,
  struct _M0TPB6Logger _M0L6loggerS194
) {
  moonbit_string_t _M0L6_2atmpS1185;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1185 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS195);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS194.$0->$method_0(_M0L6loggerS194.$1, _M0L6_2atmpS1185);
  moonbit_decref_cycle_free(_M0L6_2atmpS1185);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGfE(
  float _M0L4selfS197,
  struct _M0TPB6Logger _M0L6loggerS196
) {
  moonbit_string_t _M0L6_2atmpS1186;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1186 = _M0IPC15float5FloatPB4Show10to__string(_M0L4selfS197);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS196.$0->$method_0(_M0L6loggerS196.$1, _M0L6_2atmpS1186);
  moonbit_decref_cycle_free(_M0L6_2atmpS1186);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS199,
  struct _M0TPB6Logger _M0L6loggerS198
) {
  moonbit_string_t _M0L6_2atmpS1187;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1187 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS199);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS198.$0->$method_0(_M0L6loggerS198.$1, _M0L6_2atmpS1187);
  moonbit_decref_cycle_free(_M0L6_2atmpS1187);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS201,
  struct _M0TPB6Logger _M0L6loggerS200
) {
  moonbit_string_t _M0L6_2atmpS1188;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1188 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS201);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS200.$0->$method_0(_M0L6loggerS200.$1, _M0L6_2atmpS1188);
  moonbit_decref_cycle_free(_M0L6_2atmpS1188);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS193
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS193.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS192
) {
  moonbit_string_t _M0L8_2afieldS2036;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2036 = _M0L4selfS192.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2036);
  return _M0L8_2afieldS2036;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS188,
  moonbit_string_t _M0L5valueS189,
  int32_t _M0L5startS190,
  int32_t _M0L3lenS191
) {
  int32_t _M0L6_2atmpS1184;
  int64_t _M0L6_2atmpS1183;
  struct _M0TPC16string10StringView _M0L6_2atmpS1182;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1184 = _M0L5startS190 + _M0L3lenS191;
  _M0L6_2atmpS1183 = (int64_t)_M0L6_2atmpS1184;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1182
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS189, _M0L5startS190, _M0L6_2atmpS1183);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS188, _M0L6_2atmpS1182);
  moonbit_decref_cycle_free(_M0L6_2atmpS1182.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String21clamped__view_2einner(
  moonbit_string_t _M0L4selfS181,
  int32_t _M0L5startS183,
  int64_t _M0L3endS185
) {
  int32_t _M0L3lenS180;
  int32_t _M0Lm2loS182;
  int32_t _M0Lm2hiS184;
  int32_t _M0L6_2atmpS1166;
  int32_t _if__result_2144;
  int32_t _M0L6_2atmpS1174;
  int32_t _if__result_2145;
  int32_t _M0L6_2atmpS1176;
  int32_t _M0L6_2atmpS1177;
  #line 698 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS180 = Moonbit_array_length(_M0L4selfS181);
  if (_M0L5startS183 < 0) {
    _M0Lm2loS182 = 0;
  } else if (_M0L5startS183 > _M0L3lenS180) {
    _M0Lm2loS182 = _M0L3lenS180;
  } else {
    _M0Lm2loS182 = _M0L5startS183;
  }
  if (_M0L3endS185 == 4294967296ll) {
    _M0Lm2hiS184 = _M0L3lenS180;
  } else {
    int64_t _M0L7_2aSomeS186 = _M0L3endS185;
    int32_t _M0L4_2aeS187 = (int32_t)_M0L7_2aSomeS186;
    if (_M0L4_2aeS187 < 0) {
      _M0Lm2hiS184 = 0;
    } else if (_M0L4_2aeS187 > _M0L3lenS180) {
      _M0Lm2hiS184 = _M0L3lenS180;
    } else {
      _M0Lm2hiS184 = _M0L4_2aeS187;
    }
  }
  _M0L6_2atmpS1166 = _M0Lm2loS182;
  if (_M0L6_2atmpS1166 > 0) {
    int32_t _M0L6_2atmpS1165 = _M0Lm2loS182;
    if (_M0L6_2atmpS1165 < _M0L3lenS180) {
      int32_t _M0L6_2atmpS1164 = _M0Lm2loS182;
      int32_t _M0L6_2atmpS1163 = _M0L4selfS181[_M0L6_2atmpS1164];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1163)) {
        int32_t _M0L6_2atmpS1162 = _M0Lm2loS182;
        int32_t _M0L6_2atmpS1161 = _M0L6_2atmpS1162 - 1;
        int32_t _M0L6_2atmpS1160 = _M0L4selfS181[_M0L6_2atmpS1161];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2144
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1160);
      } else {
        _if__result_2144 = 0;
      }
    } else {
      _if__result_2144 = 0;
    }
  } else {
    _if__result_2144 = 0;
  }
  if (_if__result_2144) {
    int32_t _M0L6_2atmpS1167 = _M0Lm2loS182;
    _M0Lm2loS182 = _M0L6_2atmpS1167 + 1;
  }
  _M0L6_2atmpS1174 = _M0Lm2hiS184;
  if (_M0L6_2atmpS1174 > 0) {
    int32_t _M0L6_2atmpS1173 = _M0Lm2hiS184;
    if (_M0L6_2atmpS1173 < _M0L3lenS180) {
      int32_t _M0L6_2atmpS1172 = _M0Lm2hiS184;
      int32_t _M0L6_2atmpS1171 = _M0L4selfS181[_M0L6_2atmpS1172];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1171)) {
        int32_t _M0L6_2atmpS1170 = _M0Lm2hiS184;
        int32_t _M0L6_2atmpS1169 = _M0L6_2atmpS1170 - 1;
        int32_t _M0L6_2atmpS1168 = _M0L4selfS181[_M0L6_2atmpS1169];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2145
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1168);
      } else {
        _if__result_2145 = 0;
      }
    } else {
      _if__result_2145 = 0;
    }
  } else {
    _if__result_2145 = 0;
  }
  if (_if__result_2145) {
    int32_t _M0L6_2atmpS1175 = _M0Lm2hiS184;
    _M0Lm2hiS184 = _M0L6_2atmpS1175 - 1;
  }
  _M0L6_2atmpS1176 = _M0Lm2loS182;
  _M0L6_2atmpS1177 = _M0Lm2hiS184;
  if (_M0L6_2atmpS1176 >= _M0L6_2atmpS1177) {
    int32_t _M0L6_2atmpS1178 = _M0Lm2loS182;
    int32_t _M0L6_2atmpS1179 = _M0Lm2loS182;
    moonbit_incref_cycle_free(_M0L4selfS181);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS181,
                                                 .$1 = _M0L6_2atmpS1178,
                                                 .$2 = _M0L6_2atmpS1179};
  } else {
    int32_t _M0L6_2atmpS1180 = _M0Lm2loS182;
    int32_t _M0L6_2atmpS1181 = _M0Lm2hiS184;
    moonbit_incref_cycle_free(_M0L4selfS181);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS181,
                                                 .$1 = _M0L6_2atmpS1180,
                                                 .$2 = _M0L6_2atmpS1181};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS179,
  struct _M0TPB4Show _M0L4showS178
) {
  struct _M0TPB6Logger _M0L6_2atmpS1159;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS179);
  _M0L6_2atmpS1159
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS179
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS178.$0->$method_0(_M0L4showS178.$1, _M0L6_2atmpS1159);
  if (_M0L6_2atmpS1159.$1) {
    moonbit_decref(_M0L6_2atmpS1159.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS177,
  struct _M0TPB4Show _M0L4showS176
) {
  struct _M0TPB6Logger _M0L6_2atmpS1158;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS177);
  _M0L6_2atmpS1158
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS177
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS176.$0->$method_0(_M0L4showS176.$1, _M0L6_2atmpS1158);
  if (_M0L6_2atmpS1158.$1) {
    moonbit_decref(_M0L6_2atmpS1158.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS175) {
  int64_t _M0L6_2atmpS1157;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1157 = (int64_t)_M0L4selfS175;
  return *(uint64_t*)&_M0L6_2atmpS1157;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

moonbit_string_t _M0MPC16string6String14escape_2einner(
  moonbit_string_t _M0L4selfS173,
  int32_t _M0L5quoteS174
) {
  struct _M0TPB13StringBuilder* _M0L3bufS172;
  int32_t _M0L6_2atmpS1156;
  struct _M0TPC16string10StringView _M0L6_2atmpS1154;
  struct _M0TPB6Logger _M0L6_2atmpS1155;
  moonbit_string_t _result_2146;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS172 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1156 = Moonbit_array_length(_M0L4selfS173);
  moonbit_incref_cycle_free(_M0L4selfS173);
  _M0L6_2atmpS1154
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS173, .$1 = 0, .$2 = _M0L6_2atmpS1156
  };
  moonbit_incref_cycle_free(_M0L3bufS172);
  _M0L6_2atmpS1155
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS172
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1154, _M0L6_2atmpS1155, _M0L5quoteS174);
  moonbit_decref_cycle_free(_M0L6_2atmpS1154.$0);
  if (_M0L6_2atmpS1155.$1) {
    moonbit_decref(_M0L6_2atmpS1155.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2146 = _M0MPB13StringBuilder10to__string(_M0L3bufS172);
  moonbit_decref_cycle_free(_M0L3bufS172);
  return _result_2146;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS164,
  struct _M0TPB6Logger _M0L6loggerS162,
  int32_t _M0L5quoteS161
) {
  int32_t _M0L3endS1152;
  int32_t _M0L5startS1153;
  int32_t _M0L3lenS163;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS165;
  int32_t _M0L1iS166;
  int32_t _M0L3segS167;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS161) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS162.$0->$method_3(_M0L6loggerS162.$1, 34);
  }
  _M0L3endS1152 = _M0L4selfS164.$2;
  _M0L5startS1153 = _M0L4selfS164.$1;
  _M0L3lenS163 = _M0L3endS1152 - _M0L5startS1153;
  moonbit_incref_cycle_free(_M0L4selfS164.$0);
  if (_M0L6loggerS162.$1) {
    moonbit_incref(_M0L6loggerS162.$1);
  }
  _M0L6_2aenvS165
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS165)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 44, 0);
  _M0L6_2aenvS165->$0 = _M0L4selfS164;
  _M0L6_2aenvS165->$1 = _M0L6loggerS162;
  _M0L1iS166 = 0;
  _M0L3segS167 = 0;
  _2afor_168:;
  while (1) {
    moonbit_string_t _M0L3strS1149;
    int32_t _M0L5startS1151;
    int32_t _M0L6_2atmpS1150;
    int32_t _M0L4codeS169;
    int32_t _M0L1cS171;
    int32_t _M0L6_2atmpS1133;
    int32_t _M0L6_2atmpS1134;
    int32_t _M0L6_2atmpS1135;
    if (_M0L1iS166 >= _M0L3lenS163) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS165, _M0L3segS167, _M0L1iS166);
      moonbit_decref_cycle_free(_M0L6_2aenvS165);
      break;
    }
    _M0L3strS1149 = _M0L4selfS164.$0;
    _M0L5startS1151 = _M0L4selfS164.$1;
    _M0L6_2atmpS1150 = _M0L5startS1151 + _M0L1iS166;
    _M0L4codeS169 = _M0L3strS1149[_M0L6_2atmpS1150];
    switch (_M0L4codeS169) {
      case 34: {
        _M0L1cS171 = _M0L4codeS169;
        goto join_170;
        break;
      }
      
      case 92: {
        _M0L1cS171 = _M0L4codeS169;
        goto join_170;
        break;
      }
      
      case 10: {
        int32_t _M0L6_2atmpS1136;
        int32_t _M0L6_2atmpS1137;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS165, _M0L3segS167, _M0L1iS166);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS162.$0->$method_0(_M0L6loggerS162.$1, (moonbit_string_t)moonbit_string_literal_18.data);
        _M0L6_2atmpS1136 = _M0L1iS166 + 1;
        _M0L6_2atmpS1137 = _M0L1iS166 + 1;
        _M0L1iS166 = _M0L6_2atmpS1136;
        _M0L3segS167 = _M0L6_2atmpS1137;
        goto _2afor_168;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1138;
        int32_t _M0L6_2atmpS1139;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS165, _M0L3segS167, _M0L1iS166);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS162.$0->$method_0(_M0L6loggerS162.$1, (moonbit_string_t)moonbit_string_literal_19.data);
        _M0L6_2atmpS1138 = _M0L1iS166 + 1;
        _M0L6_2atmpS1139 = _M0L1iS166 + 1;
        _M0L1iS166 = _M0L6_2atmpS1138;
        _M0L3segS167 = _M0L6_2atmpS1139;
        goto _2afor_168;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1140;
        int32_t _M0L6_2atmpS1141;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS165, _M0L3segS167, _M0L1iS166);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS162.$0->$method_0(_M0L6loggerS162.$1, (moonbit_string_t)moonbit_string_literal_20.data);
        _M0L6_2atmpS1140 = _M0L1iS166 + 1;
        _M0L6_2atmpS1141 = _M0L1iS166 + 1;
        _M0L1iS166 = _M0L6_2atmpS1140;
        _M0L3segS167 = _M0L6_2atmpS1141;
        goto _2afor_168;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1142;
        int32_t _M0L6_2atmpS1143;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS165, _M0L3segS167, _M0L1iS166);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS162.$0->$method_0(_M0L6loggerS162.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS1142 = _M0L1iS166 + 1;
        _M0L6_2atmpS1143 = _M0L1iS166 + 1;
        _M0L1iS166 = _M0L6_2atmpS1142;
        _M0L3segS167 = _M0L6_2atmpS1143;
        goto _2afor_168;
        break;
      }
      default: {
        if (_M0L4codeS169 < 32) {
          int32_t _M0L6_2atmpS1145;
          moonbit_string_t _M0L6_2atmpS1144;
          int32_t _M0L6_2atmpS1146;
          int32_t _M0L6_2atmpS1147;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS165, _M0L3segS167, _M0L1iS166);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS162.$0->$method_0(_M0L6loggerS162.$1, (moonbit_string_t)moonbit_string_literal_22.data);
          _M0L6_2atmpS1145 = _M0L4codeS169 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1144 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1145);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS162.$0->$method_0(_M0L6loggerS162.$1, _M0L6_2atmpS1144);
          moonbit_decref_cycle_free(_M0L6_2atmpS1144);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS162.$0->$method_0(_M0L6loggerS162.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1146 = _M0L1iS166 + 1;
          _M0L6_2atmpS1147 = _M0L1iS166 + 1;
          _M0L1iS166 = _M0L6_2atmpS1146;
          _M0L3segS167 = _M0L6_2atmpS1147;
          goto _2afor_168;
        } else {
          int32_t _M0L6_2atmpS1148 = _M0L1iS166 + 1;
          int32_t _tmp_2149 = _M0L3segS167;
          _M0L1iS166 = _M0L6_2atmpS1148;
          _M0L3segS167 = _tmp_2149;
          goto _2afor_168;
        }
        break;
      }
    }
    goto joinlet_2148;
    join_170:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS165, _M0L3segS167, _M0L1iS166);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS162.$0->$method_3(_M0L6loggerS162.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1133 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS171);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS162.$0->$method_3(_M0L6loggerS162.$1, _M0L6_2atmpS1133);
    _M0L6_2atmpS1134 = _M0L1iS166 + 1;
    _M0L6_2atmpS1135 = _M0L1iS166 + 1;
    _M0L1iS166 = _M0L6_2atmpS1134;
    _M0L3segS167 = _M0L6_2atmpS1135;
    continue;
    joinlet_2148:;
    break;
  }
  if (_M0L5quoteS161) {
    #line 202 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS162.$0->$method_3(_M0L6loggerS162.$1, 34);
  }
  return 0;
}

int32_t _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS157,
  int32_t _M0L3segS160,
  int32_t _M0L1iS159
) {
  struct _M0TPB6Logger _M0L6loggerS156;
  struct _M0TPC16string10StringView _M0L4selfS158;
  #line 153 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6loggerS156 = _M0L6_2aenvS157->$1;
  _M0L4selfS158 = _M0L6_2aenvS157->$0;
  if (_M0L1iS159 > _M0L3segS160) {
    int64_t _M0L6_2atmpS1132 = (int64_t)_M0L1iS159;
    struct _M0TPC16string10StringView _M0L6_2atmpS1131;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1131
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS158, _M0L3segS160, _M0L6_2atmpS1132);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS156.$0->$method_2(_M0L6loggerS156.$1, _M0L6_2atmpS1131);
    moonbit_decref_cycle_free(_M0L6_2atmpS1131.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS147,
  int32_t _M0L5startS149,
  int64_t _M0L3endS151
) {
  int32_t _M0L3endS1129;
  int32_t _M0L5startS1130;
  int32_t _M0L3lenS146;
  int32_t _M0Lm2loS148;
  int32_t _M0Lm2hiS150;
  moonbit_string_t _M0L3strS154;
  int32_t _M0L4baseS155;
  int32_t _M0L6_2atmpS1107;
  int32_t _if__result_2150;
  int32_t _M0L6_2atmpS1117;
  int32_t _if__result_2151;
  int32_t _M0L6_2atmpS1119;
  int32_t _M0L6_2atmpS1120;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1129 = _M0L4selfS147.$2;
  _M0L5startS1130 = _M0L4selfS147.$1;
  _M0L3lenS146 = _M0L3endS1129 - _M0L5startS1130;
  if (_M0L5startS149 < 0) {
    _M0Lm2loS148 = 0;
  } else if (_M0L5startS149 > _M0L3lenS146) {
    _M0Lm2loS148 = _M0L3lenS146;
  } else {
    _M0Lm2loS148 = _M0L5startS149;
  }
  if (_M0L3endS151 == 4294967296ll) {
    _M0Lm2hiS150 = _M0L3lenS146;
  } else {
    int64_t _M0L7_2aSomeS152 = _M0L3endS151;
    int32_t _M0L4_2aeS153 = (int32_t)_M0L7_2aSomeS152;
    if (_M0L4_2aeS153 < 0) {
      _M0Lm2hiS150 = 0;
    } else if (_M0L4_2aeS153 > _M0L3lenS146) {
      _M0Lm2hiS150 = _M0L3lenS146;
    } else {
      _M0Lm2hiS150 = _M0L4_2aeS153;
    }
  }
  _M0L3strS154 = _M0L4selfS147.$0;
  _M0L4baseS155 = _M0L4selfS147.$1;
  _M0L6_2atmpS1107 = _M0Lm2loS148;
  if (_M0L6_2atmpS1107 > 0) {
    int32_t _M0L6_2atmpS1106 = _M0Lm2loS148;
    if (_M0L6_2atmpS1106 < _M0L3lenS146) {
      int32_t _M0L6_2atmpS1105 = _M0Lm2loS148;
      int32_t _M0L6_2atmpS1104 = _M0L4baseS155 + _M0L6_2atmpS1105;
      int32_t _M0L6_2atmpS1103 = _M0L3strS154[_M0L6_2atmpS1104];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1103)) {
        int32_t _M0L6_2atmpS1102 = _M0Lm2loS148;
        int32_t _M0L6_2atmpS1101 = _M0L4baseS155 + _M0L6_2atmpS1102;
        int32_t _M0L6_2atmpS1100 = _M0L6_2atmpS1101 - 1;
        int32_t _M0L6_2atmpS1099 = _M0L3strS154[_M0L6_2atmpS1100];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2150
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1099);
      } else {
        _if__result_2150 = 0;
      }
    } else {
      _if__result_2150 = 0;
    }
  } else {
    _if__result_2150 = 0;
  }
  if (_if__result_2150) {
    int32_t _M0L6_2atmpS1108 = _M0Lm2loS148;
    _M0Lm2loS148 = _M0L6_2atmpS1108 + 1;
  }
  _M0L6_2atmpS1117 = _M0Lm2hiS150;
  if (_M0L6_2atmpS1117 > 0) {
    int32_t _M0L6_2atmpS1116 = _M0Lm2hiS150;
    if (_M0L6_2atmpS1116 < _M0L3lenS146) {
      int32_t _M0L6_2atmpS1115 = _M0Lm2hiS150;
      int32_t _M0L6_2atmpS1114 = _M0L4baseS155 + _M0L6_2atmpS1115;
      int32_t _M0L6_2atmpS1113 = _M0L3strS154[_M0L6_2atmpS1114];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1113)) {
        int32_t _M0L6_2atmpS1112 = _M0Lm2hiS150;
        int32_t _M0L6_2atmpS1111 = _M0L4baseS155 + _M0L6_2atmpS1112;
        int32_t _M0L6_2atmpS1110 = _M0L6_2atmpS1111 - 1;
        int32_t _M0L6_2atmpS1109 = _M0L3strS154[_M0L6_2atmpS1110];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2151
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1109);
      } else {
        _if__result_2151 = 0;
      }
    } else {
      _if__result_2151 = 0;
    }
  } else {
    _if__result_2151 = 0;
  }
  if (_if__result_2151) {
    int32_t _M0L6_2atmpS1118 = _M0Lm2hiS150;
    _M0Lm2hiS150 = _M0L6_2atmpS1118 - 1;
  }
  _M0L6_2atmpS1119 = _M0Lm2loS148;
  _M0L6_2atmpS1120 = _M0Lm2hiS150;
  if (_M0L6_2atmpS1119 >= _M0L6_2atmpS1120) {
    int32_t _M0L6_2atmpS1124 = _M0Lm2loS148;
    int32_t _M0L6_2atmpS1121 = _M0L4baseS155 + _M0L6_2atmpS1124;
    int32_t _M0L6_2atmpS1123 = _M0Lm2loS148;
    int32_t _M0L6_2atmpS1122 = _M0L4baseS155 + _M0L6_2atmpS1123;
    moonbit_incref_cycle_free(_M0L3strS154);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS154,
                                                 .$1 = _M0L6_2atmpS1121,
                                                 .$2 = _M0L6_2atmpS1122};
  } else {
    int32_t _M0L6_2atmpS1128 = _M0Lm2loS148;
    int32_t _M0L6_2atmpS1125 = _M0L4baseS155 + _M0L6_2atmpS1128;
    int32_t _M0L6_2atmpS1127 = _M0Lm2hiS150;
    int32_t _M0L6_2atmpS1126 = _M0L4baseS155 + _M0L6_2atmpS1127;
    moonbit_incref_cycle_free(_M0L3strS154);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS154,
                                                 .$1 = _M0L6_2atmpS1125,
                                                 .$2 = _M0L6_2atmpS1126};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS145) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS144;
  int32_t _M0L6_2atmpS1096;
  int32_t _M0L6_2atmpS1095;
  int32_t _M0L6_2atmpS1098;
  int32_t _M0L6_2atmpS1097;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1094;
  moonbit_string_t _result_2152;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS144 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1096 = _M0IPC14byte4BytePB3Div3div(_M0L1bS145, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1095
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1096);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS144, _M0L6_2atmpS1095);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1098 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS145, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1097
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1098);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS144, _M0L6_2atmpS1097);
  _M0L6_2atmpS1094 = _M0L7_2aselfS144;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2152 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1094);
  moonbit_decref_cycle_free(_M0L6_2atmpS1094);
  return _result_2152;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS143) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS143 < 10) {
    int32_t _M0L6_2atmpS1091;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1091 = _M0IPC14byte4BytePB3Add3add(_M0L1iS143, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1091);
  } else {
    int32_t _M0L6_2atmpS1093;
    int32_t _M0L6_2atmpS1092;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1093 = _M0IPC14byte4BytePB3Add3add(_M0L1iS143, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1092 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1093, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1092);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS141,
  int32_t _M0L4thatS142
) {
  int32_t _M0L6_2atmpS1089;
  int32_t _M0L6_2atmpS1090;
  int32_t _M0L6_2atmpS1088;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1089 = (int32_t)_M0L4selfS141;
  _M0L6_2atmpS1090 = (int32_t)_M0L4thatS142;
  _M0L6_2atmpS1088 = _M0L6_2atmpS1089 - _M0L6_2atmpS1090;
  return _M0L6_2atmpS1088 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS139,
  int32_t _M0L4thatS140
) {
  int32_t _M0L6_2atmpS1086;
  int32_t _M0L6_2atmpS1087;
  int32_t _M0L6_2atmpS1085;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1086 = (int32_t)_M0L4selfS139;
  _M0L6_2atmpS1087 = (int32_t)_M0L4thatS140;
  _M0L6_2atmpS1085 = _M0L6_2atmpS1086 % _M0L6_2atmpS1087;
  return _M0L6_2atmpS1085 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS137,
  int32_t _M0L4thatS138
) {
  int32_t _M0L6_2atmpS1083;
  int32_t _M0L6_2atmpS1084;
  int32_t _M0L6_2atmpS1082;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1083 = (int32_t)_M0L4selfS137;
  _M0L6_2atmpS1084 = (int32_t)_M0L4thatS138;
  _M0L6_2atmpS1082 = _M0L6_2atmpS1083 / _M0L6_2atmpS1084;
  return _M0L6_2atmpS1082 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS135,
  int32_t _M0L4thatS136
) {
  int32_t _M0L6_2atmpS1080;
  int32_t _M0L6_2atmpS1081;
  int32_t _M0L6_2atmpS1079;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1080 = (int32_t)_M0L4selfS135;
  _M0L6_2atmpS1081 = (int32_t)_M0L4thatS136;
  _M0L6_2atmpS1079 = _M0L6_2atmpS1080 + _M0L6_2atmpS1081;
  return _M0L6_2atmpS1079 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS134) {
  int32_t _M0L6_2atmpS1078;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1078 = (int32_t)_M0L4selfS134;
  return _M0L6_2atmpS1078;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS133) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS133 >= 56320 && _M0L4selfS133 <= 57343;
}

int32_t _M0MPC16uint166UInt1622is__leading__surrogate(int32_t _M0L4selfS132) {
  #line 28 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS132 >= 55296 && _M0L4selfS132 <= 56319;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS131,
  moonbit_string_t _M0L3strS129
) {
  int32_t _M0L8str__lenS128;
  int32_t _M0L3lenS1077;
  int32_t _M0L8requiredS130;
  uint16_t* _M0L4dataS1072;
  int32_t _M0L6_2atmpS1071;
  int32_t _if__result_2153;
  uint16_t* _M0L4dataS1073;
  int32_t _M0L3lenS1074;
  int32_t _M0L3lenS1076;
  int32_t _M0L6_2atmpS1075;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS128 = Moonbit_array_length(_M0L3strS129);
  if (_M0L8str__lenS128 == 0) {
    return 0;
  }
  _M0L3lenS1077 = _M0L4selfS131->$1;
  _M0L8requiredS130 = _M0L3lenS1077 + _M0L8str__lenS128;
  _M0L4dataS1072 = _M0L4selfS131->$0;
  _M0L6_2atmpS1071 = Moonbit_array_length(_M0L4dataS1072);
  if (_M0L8requiredS130 > _M0L6_2atmpS1071) {
    _if__result_2153 = 1;
  } else {
    int32_t _M0L3lenS1070 = _M0L4selfS131->$1;
    _if__result_2153 = _M0L8requiredS130 < _M0L3lenS1070;
  }
  if (_if__result_2153) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS131, _M0L8requiredS130);
  }
  _M0L4dataS1073 = _M0L4selfS131->$0;
  _M0L3lenS1074 = _M0L4selfS131->$1;
  moonbit_incref_cycle_free(_M0L4dataS1073);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1073, _M0L3lenS1074, _M0L3strS129, 0, _M0L8str__lenS128);
  moonbit_decref_cycle_free(_M0L4dataS1073);
  _M0L3lenS1076 = _M0L4selfS131->$1;
  _M0L6_2atmpS1075 = _M0L3lenS1076 + _M0L8str__lenS128;
  _M0L4selfS131->$1 = _M0L6_2atmpS1075;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS124,
  int32_t _M0L11dst__offsetS127,
  moonbit_string_t _M0L3strS125,
  int32_t _M0L11str__offsetS120,
  int32_t _M0L3lenS121
) {
  int32_t _M0L16end__str__offsetS119;
  int32_t _M0L1iS122;
  int32_t _M0L1jS123;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS119 = _M0L11str__offsetS120 + _M0L3lenS121;
  _M0L1iS122 = _M0L11str__offsetS120;
  _M0L1jS123 = _M0L11dst__offsetS127;
  while (1) {
    if (_M0L1iS122 < _M0L16end__str__offsetS119) {
      int32_t _M0L6_2atmpS1067 = _M0L3strS125[_M0L1iS122];
      int32_t _M0L6_2atmpS1068;
      int32_t _M0L6_2atmpS1069;
      _M0L4selfS124[_M0L1jS123] = _M0L6_2atmpS1067;
      _M0L6_2atmpS1068 = _M0L1iS122 + 1;
      _M0L6_2atmpS1069 = _M0L1jS123 + 1;
      _M0L1iS122 = _M0L6_2atmpS1068;
      _M0L1jS123 = _M0L6_2atmpS1069;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS117,
  int32_t _M0L2chS116
) {
  uint32_t _M0L4codeS115;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS115 = _M0MPC14char4Char8to__uint(_M0L2chS116);
  if (_M0L4codeS115 <= 65535u) {
    int32_t _M0L3lenS1038 = _M0L4selfS117->$1;
    uint16_t* _M0L4dataS1040 = _M0L4selfS117->$0;
    int32_t _M0L6_2atmpS1039 = Moonbit_array_length(_M0L4dataS1040);
    uint16_t* _M0L4dataS1043;
    int32_t _M0L3lenS1044;
    int32_t _M0L6_2atmpS1045;
    int32_t _M0L3lenS1047;
    int32_t _M0L6_2atmpS1046;
    if (_M0L3lenS1038 >= _M0L6_2atmpS1039) {
      int32_t _M0L3lenS1042 = _M0L4selfS117->$1;
      int32_t _M0L6_2atmpS1041 = _M0L3lenS1042 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS117, _M0L6_2atmpS1041);
    }
    _M0L4dataS1043 = _M0L4selfS117->$0;
    _M0L3lenS1044 = _M0L4selfS117->$1;
    moonbit_incref_cycle_free(_M0L4dataS1043);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1045 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS115);
    if (
      _M0L3lenS1044 < 0
      || _M0L3lenS1044 >= Moonbit_array_length(_M0L4dataS1043)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1043[_M0L3lenS1044] = _M0L6_2atmpS1045;
    moonbit_decref_cycle_free(_M0L4dataS1043);
    _M0L3lenS1047 = _M0L4selfS117->$1;
    _M0L6_2atmpS1046 = _M0L3lenS1047 + 1;
    _M0L4selfS117->$1 = _M0L6_2atmpS1046;
  } else if (_M0L4codeS115 <= 1114111u) {
    uint16_t* _M0L4dataS1051 = _M0L4selfS117->$0;
    int32_t _M0L6_2atmpS1049 = Moonbit_array_length(_M0L4dataS1051);
    int32_t _M0L3lenS1050 = _M0L4selfS117->$1;
    int32_t _M0L6_2atmpS1048 = _M0L6_2atmpS1049 - _M0L3lenS1050;
    uint32_t _M0L4codeS118;
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
      int32_t _M0L3lenS1053 = _M0L4selfS117->$1;
      int32_t _M0L6_2atmpS1052 = _M0L3lenS1053 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS117, _M0L6_2atmpS1052);
    }
    _M0L4codeS118 = _M0L4codeS115 - 65536u;
    _M0L4dataS1054 = _M0L4selfS117->$0;
    _M0L3lenS1055 = _M0L4selfS117->$1;
    _M0L6_2atmpS1058 = _M0L4codeS118 >> 10;
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
    _M0L4dataS1059 = _M0L4selfS117->$0;
    _M0L3lenS1064 = _M0L4selfS117->$1;
    _M0L6_2atmpS1060 = _M0L3lenS1064 + 1;
    _M0L6_2atmpS1063 = _M0L4codeS118 & 1023u;
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
    _M0L3lenS1066 = _M0L4selfS117->$1;
    _M0L6_2atmpS1065 = _M0L3lenS1066 + 2;
    _M0L4selfS117->$1 = _M0L6_2atmpS1065;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_23.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  int32_t _M0L8requiredS113
) {
  uint16_t* _M0L4dataS1037;
  int32_t _M0L6_2atmpS1035;
  int32_t _M0L3lenS1036;
  int32_t _M0L13new__capacityS111;
  uint16_t* _M0L4dataS1032;
  int32_t _M0L6_2atmpS1033;
  int32_t _M0L3lenS1034;
  uint16_t* _M0L9new__dataS114;
  uint16_t* _M0L6_2aoldS2037;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1037 = _M0L4selfS112->$0;
  _M0L6_2atmpS1035 = Moonbit_array_length(_M0L4dataS1037);
  _M0L3lenS1036 = _M0L4selfS112->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS111
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1035, _M0L3lenS1036, _M0L8requiredS113);
  _M0L4dataS1032 = _M0L4selfS112->$0;
  moonbit_incref_cycle_free(_M0L4dataS1032);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1033 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1034 = _M0L4selfS112->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS114
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1032, _M0L13new__capacityS111, _M0L6_2atmpS1033, _M0L3lenS1034, 0, 0);
  _M0L6_2aoldS2037 = _M0L4selfS112->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2037);
  _M0L4selfS112->$0 = _M0L9new__dataS114;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS110,
  int32_t _M0L3lenS106,
  int32_t _M0L8requiredS105
) {
  int32_t _M0L5spaceS107;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS105 < _M0L3lenS106) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_24.data);
  }
  _M0L5spaceS107 = _M0L7currentS110;
  while (1) {
    if (_M0L5spaceS107 < _M0L8requiredS105) {
      int32_t _M0L4nextS108 = _M0L5spaceS107 * 2;
      if (_M0L4nextS108 <= _M0L5spaceS107) {
        return _M0L8requiredS105;
      }
      _M0L5spaceS107 = _M0L4nextS108;
      continue;
    } else {
      return _M0L5spaceS107;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS104) {
  int32_t _M0L6_2atmpS1031;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1031 = *(int32_t*)&_M0L4selfS104;
  return (uint16_t)_M0L6_2atmpS1031;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS103) {
  int32_t _M0L6_2atmpS1030;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1030 = _M0L4selfS103;
  return *(uint32_t*)&_M0L6_2atmpS1030;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS101
) {
  int32_t _M0L3lenS1021;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1021 = _M0L4selfS101->$1;
  if (_M0L3lenS1021 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1022 = _M0L4selfS101->$1;
    uint16_t* _M0L4dataS1024 = _M0L4selfS101->$0;
    int32_t _M0L6_2atmpS1023 = Moonbit_array_length(_M0L4dataS1024);
    if (_M0L3lenS1022 == _M0L6_2atmpS1023) {
      uint16_t* _M0L4dataS1025 = _M0L4selfS101->$0;
      moonbit_incref_cycle_free(_M0L4dataS1025);
      return _M0L4dataS1025;
    } else {
      uint16_t* _M0L4dataS1026 = _M0L4selfS101->$0;
      int32_t _M0L3lenS1027 = _M0L4selfS101->$1;
      int32_t _M0L6_2atmpS1028;
      int32_t _M0L3lenS1029;
      uint16_t* _M0L4dataS102;
      moonbit_incref_cycle_free(_M0L4dataS1026);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1028 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1029 = _M0L4selfS101->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS102
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1026, _M0L3lenS1027, _M0L6_2atmpS1028, _M0L3lenS1029, 0, 0);
      return _M0L4dataS102;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS98,
  int32_t _M0L13allocate__lenS94,
  int32_t _M0L4initS99,
  int32_t _M0L3lenS95,
  int32_t _M0L11src__offsetS96,
  int32_t _M0L11dst__offsetS97
) {
  int32_t _if__result_2156;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS94 >= 0) {
    if (_M0L3lenS95 >= 0) {
      if (_M0L11src__offsetS96 >= 0) {
        if (_M0L11dst__offsetS97 >= 0) {
          int32_t _M0L6_2atmpS1017 = _M0L11src__offsetS96 + _M0L3lenS95;
          int32_t _M0L6_2atmpS1018 = Moonbit_array_length(_M0L3srcS98);
          if (_M0L6_2atmpS1017 <= _M0L6_2atmpS1018) {
            int32_t _M0L6_2atmpS1016 = _M0L11dst__offsetS97 + _M0L3lenS95;
            _if__result_2156 = _M0L6_2atmpS1016 <= _M0L13allocate__lenS94;
          } else {
            _if__result_2156 = 0;
          }
        } else {
          _if__result_2156 = 0;
        }
      } else {
        _if__result_2156 = 0;
      }
    } else {
      _if__result_2156 = 0;
    }
  } else {
    _if__result_2156 = 0;
  }
  if (_if__result_2156) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS98, _M0L13allocate__lenS94, _M0L4initS99, _M0L11src__offsetS96, _M0L11dst__offsetS97, _M0L3lenS95);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS100;
    int32_t _M0L6_2atmpS1020;
    moonbit_string_t _M0L6_2atmpS1019;
    uint16_t* _result_2157;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS100
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS100, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS100, _M0L13allocate__lenS94);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS100, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS100, _M0L11src__offsetS96);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS100, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS100, _M0L11dst__offsetS97);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS100, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS100, _M0L3lenS95);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS100, (moonbit_string_t)moonbit_string_literal_29.data);
    _M0L6_2atmpS1020 = Moonbit_array_length(_M0L3srcS98);
    moonbit_decref_cycle_free(_M0L3srcS98);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS100, _M0L6_2atmpS1020);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1019
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS100);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS100);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2157 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1019);
    moonbit_decref_cycle_free(_M0L6_2atmpS1019);
    return _result_2157;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS91,
  int32_t _M0L13allocate__lenS88,
  int32_t _M0L4initS89,
  int32_t _M0L11src__offsetS92,
  int32_t _M0L11dst__offsetS90,
  int32_t _M0L9blit__lenS93
) {
  uint16_t* _M0L3dstS87;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS87
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS88, _M0L4initS89);
  moonbit_incref_cycle_free(_M0L3dstS87);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS87, _M0L11dst__offsetS90, _M0L3srcS91, _M0L11src__offsetS92, _M0L9blit__lenS93, sizeof(uint16_t));
  return _M0L3dstS87;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS85
) {
  int32_t _M0L7initialS84;
  uint16_t* _M0L4dataS86;
  struct _M0TPB13StringBuilder* _block_2158;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS85 < 1) {
    _M0L7initialS84 = 1;
  } else {
    int32_t _M0L6_2atmpS1015 = _M0L10size__hintS85 + 1;
    _M0L7initialS84 = _M0L6_2atmpS1015 / 2;
  }
  _M0L4dataS86 = (uint16_t*)moonbit_make_string(_M0L7initialS84, 0);
  _block_2158
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2158)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 49, 0);
  _block_2158->$0 = _M0L4dataS86;
  _block_2158->$1 = 0;
  return _block_2158;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS83) {
  int32_t _M0L6_2atmpS1014;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1014 = (int32_t)_M0L4selfS83;
  return _M0L6_2atmpS1014;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS75,
  int32_t _M0L13allocate__lenS71,
  int32_t _M0L3lenS72,
  int32_t _M0L11src__offsetS73,
  int32_t _M0L11dst__offsetS74
) {
  int32_t _if__result_2159;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS71 >= 0) {
    if (_M0L3lenS72 >= 0) {
      if (_M0L11src__offsetS73 >= 0) {
        if (_M0L11dst__offsetS74 >= 0) {
          int32_t _M0L6_2atmpS1005 = _M0L11src__offsetS73 + _M0L3lenS72;
          int32_t _M0L6_2atmpS1006;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1006
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS75);
          if (_M0L6_2atmpS1005 <= _M0L6_2atmpS1006) {
            int32_t _M0L6_2atmpS1004 = _M0L11dst__offsetS74 + _M0L3lenS72;
            _if__result_2159 = _M0L6_2atmpS1004 <= _M0L13allocate__lenS71;
          } else {
            _if__result_2159 = 0;
          }
        } else {
          _if__result_2159 = 0;
        }
      } else {
        _if__result_2159 = 0;
      }
    } else {
      _if__result_2159 = 0;
    }
  } else {
    _if__result_2159 = 0;
  }
  if (_if__result_2159) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS71, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS75, _M0L11src__offsetS73, _M0L11dst__offsetS74, _M0L3lenS72);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS76;
    int32_t _M0L6_2atmpS1008;
    moonbit_string_t _M0L6_2atmpS1007;
    moonbit_string_t* _result_2160;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS76
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS76, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS76, _M0L13allocate__lenS71);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS76, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS76, _M0L11src__offsetS73);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS76, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS76, _M0L11dst__offsetS74);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS76, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS76, _M0L3lenS72);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS76, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1008 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS75);
    moonbit_decref_cycle_free(_M0L3srcS75);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS76, _M0L6_2atmpS1008);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1007
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS76);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS76);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2160
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1007);
    moonbit_decref_cycle_free(_M0L6_2atmpS1007);
    return _result_2160;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS81,
  int32_t _M0L13allocate__lenS77,
  int32_t _M0L3lenS78,
  int32_t _M0L11src__offsetS79,
  int32_t _M0L11dst__offsetS80
) {
  int32_t _if__result_2161;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS77 >= 0) {
    if (_M0L3lenS78 >= 0) {
      if (_M0L11src__offsetS79 >= 0) {
        if (_M0L11dst__offsetS80 >= 0) {
          int32_t _M0L6_2atmpS1010 = _M0L11src__offsetS79 + _M0L3lenS78;
          int32_t _M0L6_2atmpS1011;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1011
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS81);
          if (_M0L6_2atmpS1010 <= _M0L6_2atmpS1011) {
            int32_t _M0L6_2atmpS1009 = _M0L11dst__offsetS80 + _M0L3lenS78;
            _if__result_2161 = _M0L6_2atmpS1009 <= _M0L13allocate__lenS77;
          } else {
            _if__result_2161 = 0;
          }
        } else {
          _if__result_2161 = 0;
        }
      } else {
        _if__result_2161 = 0;
      }
    } else {
      _if__result_2161 = 0;
    }
  } else {
    _if__result_2161 = 0;
  }
  if (_if__result_2161) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS77, 0, _M0L3srcS81, _M0L11src__offsetS79, _M0L11dst__offsetS80, _M0L3lenS78);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS82;
    int32_t _M0L6_2atmpS1013;
    moonbit_string_t _M0L6_2atmpS1012;
    struct _M0TUsiE** _result_2162;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS82
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS82, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS82, _M0L13allocate__lenS77);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS82, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS82, _M0L11src__offsetS79);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS82, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS82, _M0L11dst__offsetS80);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS82, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS82, _M0L3lenS78);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS82, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1013 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS81);
    moonbit_decref_cycle_free(_M0L3srcS81);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS82, _M0L6_2atmpS1013);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1012
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS82);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS82);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2162
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1012);
    moonbit_decref_cycle_free(_M0L6_2atmpS1012);
    return _result_2162;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS64,
  int32_t _M0L3objS63
) {
  struct _M0TPB6Logger _M0L6_2atmpS1000;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS64);
  _M0L6_2atmpS1000
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS64
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS63, _M0L6_2atmpS1000);
  if (_M0L6_2atmpS1000.$1) {
    moonbit_decref(_M0L6_2atmpS1000.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGfE(
  struct _M0TPB13StringBuilder* _M0L4selfS66,
  float _M0L3objS65
) {
  struct _M0TPB6Logger _M0L6_2atmpS1001;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS66);
  _M0L6_2atmpS1001
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS66
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGfE(_M0L3objS65, _M0L6_2atmpS1001);
  if (_M0L6_2atmpS1001.$1) {
    moonbit_decref(_M0L6_2atmpS1001.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS68,
  moonbit_string_t _M0L3objS67
) {
  struct _M0TPB6Logger _M0L6_2atmpS1002;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS68);
  _M0L6_2atmpS1002
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS68
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS67, _M0L6_2atmpS1002);
  if (_M0L6_2atmpS1002.$1) {
    moonbit_decref(_M0L6_2atmpS1002.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS70,
  uint64_t _M0L3objS69
) {
  struct _M0TPB6Logger _M0L6_2atmpS1003;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS70);
  _M0L6_2atmpS1003
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS70
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS69, _M0L6_2atmpS1003);
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
        int32_t _M0L6_2atmpS973 = _M0L11dst__offsetS16 + _M0L1iS18;
        int32_t _M0L6_2atmpS975 = _M0L11src__offsetS17 + _M0L1iS18;
        int32_t _M0L6_2atmpS974;
        int32_t _M0L6_2atmpS976;
        if (
          _M0L6_2atmpS975 < 0
          || _M0L6_2atmpS975 >= Moonbit_array_length(_M0L3srcS15)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS974 = (int32_t)_M0L3srcS15[_M0L6_2atmpS975];
        if (
          _M0L6_2atmpS973 < 0
          || _M0L6_2atmpS973 >= Moonbit_array_length(_M0L3dstS14)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS14[_M0L6_2atmpS973] = _M0L6_2atmpS974;
        _M0L6_2atmpS976 = _M0L1iS18 + 1;
        _M0L1iS18 = _M0L6_2atmpS976;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS15);
        moonbit_decref_cycle_free(_M0L3dstS14);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS981 = _M0L3lenS19 - 1;
    int32_t _M0L1iS21 = _M0L6_2atmpS981;
    while (1) {
      if (_M0L1iS21 >= 0) {
        int32_t _M0L6_2atmpS977 = _M0L11dst__offsetS16 + _M0L1iS21;
        int32_t _M0L6_2atmpS979 = _M0L11src__offsetS17 + _M0L1iS21;
        int32_t _M0L6_2atmpS978;
        int32_t _M0L6_2atmpS980;
        if (
          _M0L6_2atmpS979 < 0
          || _M0L6_2atmpS979 >= Moonbit_array_length(_M0L3srcS15)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS978 = (int32_t)_M0L3srcS15[_M0L6_2atmpS979];
        if (
          _M0L6_2atmpS977 < 0
          || _M0L6_2atmpS977 >= Moonbit_array_length(_M0L3dstS14)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS14[_M0L6_2atmpS977] = _M0L6_2atmpS978;
        _M0L6_2atmpS980 = _M0L1iS21 - 1;
        _M0L1iS21 = _M0L6_2atmpS980;
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
        int32_t _M0L6_2atmpS982 = _M0L11dst__offsetS25 + _M0L1iS27;
        int32_t _M0L6_2atmpS984 = _M0L11src__offsetS26 + _M0L1iS27;
        moonbit_string_t _M0L6_2atmpS983;
        moonbit_string_t _M0L6_2aoldS2038;
        int32_t _M0L6_2atmpS985;
        if (
          _M0L6_2atmpS984 < 0
          || _M0L6_2atmpS984 >= Moonbit_array_length(_M0L3srcS24)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS983 = (moonbit_string_t)_M0L3srcS24[_M0L6_2atmpS984];
        if (
          _M0L6_2atmpS982 < 0
          || _M0L6_2atmpS982 >= Moonbit_array_length(_M0L3dstS23)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2038 = (moonbit_string_t)_M0L3dstS23[_M0L6_2atmpS982];
        moonbit_incref_cycle_free(_M0L6_2atmpS983);
        moonbit_decref_cycle_free(_M0L6_2aoldS2038);
        _M0L3dstS23[_M0L6_2atmpS982] = _M0L6_2atmpS983;
        _M0L6_2atmpS985 = _M0L1iS27 + 1;
        _M0L1iS27 = _M0L6_2atmpS985;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS24);
        moonbit_decref_cycle_free(_M0L3dstS23);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS990 = _M0L3lenS28 - 1;
    int32_t _M0L1iS30 = _M0L6_2atmpS990;
    while (1) {
      if (_M0L1iS30 >= 0) {
        int32_t _M0L6_2atmpS986 = _M0L11dst__offsetS25 + _M0L1iS30;
        int32_t _M0L6_2atmpS988 = _M0L11src__offsetS26 + _M0L1iS30;
        moonbit_string_t _M0L6_2atmpS987;
        moonbit_string_t _M0L6_2aoldS2039;
        int32_t _M0L6_2atmpS989;
        if (
          _M0L6_2atmpS988 < 0
          || _M0L6_2atmpS988 >= Moonbit_array_length(_M0L3srcS24)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS987 = (moonbit_string_t)_M0L3srcS24[_M0L6_2atmpS988];
        if (
          _M0L6_2atmpS986 < 0
          || _M0L6_2atmpS986 >= Moonbit_array_length(_M0L3dstS23)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2039 = (moonbit_string_t)_M0L3dstS23[_M0L6_2atmpS986];
        moonbit_incref_cycle_free(_M0L6_2atmpS987);
        moonbit_decref_cycle_free(_M0L6_2aoldS2039);
        _M0L3dstS23[_M0L6_2atmpS986] = _M0L6_2atmpS987;
        _M0L6_2atmpS989 = _M0L1iS30 - 1;
        _M0L1iS30 = _M0L6_2atmpS989;
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
        int32_t _M0L6_2atmpS991 = _M0L11dst__offsetS34 + _M0L1iS36;
        int32_t _M0L6_2atmpS993 = _M0L11src__offsetS35 + _M0L1iS36;
        struct _M0TUsiE* _M0L6_2atmpS992;
        struct _M0TUsiE* _M0L6_2aoldS2040;
        int32_t _M0L6_2atmpS994;
        if (
          _M0L6_2atmpS993 < 0
          || _M0L6_2atmpS993 >= Moonbit_array_length(_M0L3srcS33)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS992 = (struct _M0TUsiE*)_M0L3srcS33[_M0L6_2atmpS993];
        if (
          _M0L6_2atmpS991 < 0
          || _M0L6_2atmpS991 >= Moonbit_array_length(_M0L3dstS32)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2040 = (struct _M0TUsiE*)_M0L3dstS32[_M0L6_2atmpS991];
        if (_M0L6_2atmpS992) {
          moonbit_incref_cycle_free(_M0L6_2atmpS992);
        }
        if (_M0L6_2aoldS2040) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2040);
        }
        _M0L3dstS32[_M0L6_2atmpS991] = _M0L6_2atmpS992;
        _M0L6_2atmpS994 = _M0L1iS36 + 1;
        _M0L1iS36 = _M0L6_2atmpS994;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS33);
        moonbit_decref_cycle_free(_M0L3dstS32);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS999 = _M0L3lenS37 - 1;
    int32_t _M0L1iS39 = _M0L6_2atmpS999;
    while (1) {
      if (_M0L1iS39 >= 0) {
        int32_t _M0L6_2atmpS995 = _M0L11dst__offsetS34 + _M0L1iS39;
        int32_t _M0L6_2atmpS997 = _M0L11src__offsetS35 + _M0L1iS39;
        struct _M0TUsiE* _M0L6_2atmpS996;
        struct _M0TUsiE* _M0L6_2aoldS2041;
        int32_t _M0L6_2atmpS998;
        if (
          _M0L6_2atmpS997 < 0
          || _M0L6_2atmpS997 >= Moonbit_array_length(_M0L3srcS33)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS996 = (struct _M0TUsiE*)_M0L3srcS33[_M0L6_2atmpS997];
        if (
          _M0L6_2atmpS995 < 0
          || _M0L6_2atmpS995 >= Moonbit_array_length(_M0L3dstS32)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2041 = (struct _M0TUsiE*)_M0L3dstS32[_M0L6_2atmpS995];
        if (_M0L6_2atmpS996) {
          moonbit_incref_cycle_free(_M0L6_2atmpS996);
        }
        if (_M0L6_2aoldS2041) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2041);
        }
        _M0L3dstS32[_M0L6_2atmpS995] = _M0L6_2atmpS996;
        _M0L6_2atmpS998 = _M0L1iS39 - 1;
        _M0L1iS39 = _M0L6_2atmpS998;
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
  _M0L10_2ax__6388S11.$0->$method_0(_M0L10_2ax__6388S11.$1, (moonbit_string_t)moonbit_string_literal_30.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S11, _M0L15_2a_2aarg__6389S10);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S11.$0->$method_0(_M0L10_2ax__6388S11.$1, (moonbit_string_t)moonbit_string_literal_31.data);
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS945) {
  switch (Moonbit_object_tag(_M0L4_2aeS945)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_32.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS945);
      break;
    }
    
    case 3: {
      return (moonbit_string_t)moonbit_string_literal_33.data;
      break;
    }
    
    case 1: {
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
  void* _M0L11_2aobj__ptrS968,
  struct _M0TPB4Show _M0L8_2aparamS967
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS966 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS968;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS966, _M0L8_2aparamS967);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS965,
  struct _M0TPB4Show _M0L8_2aparamS964
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS963 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS965;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS963, _M0L8_2aparamS964);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS962,
  int32_t _M0L8_2aparamS961
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS960 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS962;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS960, _M0L8_2aparamS961);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS959,
  struct _M0TPC16string10StringView _M0L8_2aparamS958
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS957 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS959;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS957, _M0L8_2aparamS958);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS956,
  moonbit_string_t _M0L8_2aparamS953,
  int32_t _M0L8_2aparamS954,
  int32_t _M0L8_2aparamS955
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS952 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS956;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS952, _M0L8_2aparamS953, _M0L8_2aparamS954, _M0L8_2aparamS955);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS951,
  moonbit_string_t _M0L8_2aparamS950
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS949 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS951;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS949, _M0L8_2aparamS950);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS972;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS938;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS939;
  int32_t _M0L7_2abindS940;
  struct _M0TUsiE** _M0L7_2abindS941;
  int32_t _M0L6_2acntS2046;
  int32_t _M0L2__S942;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS972
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS938
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS938)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 52, 0);
  _M0L12async__testsS938->$0 = _M0L6_2atmpS972;
  _M0L12async__testsS938->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS939
  = _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS940 = _M0L7_2abindS939->$1;
  _M0L7_2abindS941 = _M0L7_2abindS939->$0;
  _M0L6_2acntS2046
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS939));
  if (_M0L6_2acntS2046 > 1) {
    int32_t _M0L11_2anew__cntS2047 = _M0L6_2acntS2046 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS939), _M0L11_2anew__cntS2047);
    moonbit_incref_cycle_free(_M0L7_2abindS941);
  } else if (_M0L6_2acntS2046 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS939);
  }
  _M0L2__S942 = 0;
  while (1) {
    if (_M0L2__S942 < _M0L7_2abindS940) {
      struct _M0TUsiE* _M0L3argS943 =
        (struct _M0TUsiE*)_M0L7_2abindS941[_M0L2__S942];
      moonbit_string_t _M0L6_2atmpS969 = _M0L3argS943->$0;
      int32_t _M0L6_2atmpS970 = _M0L3argS943->$1;
      int32_t _M0L6_2atmpS971;
      moonbit_incref_cycle_free(_M0L6_2atmpS969);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS938, _M0L6_2atmpS969, _M0L6_2atmpS970);
      moonbit_decref_cycle_free(_M0L6_2atmpS969);
      _M0L6_2atmpS971 = _M0L2__S942 + 1;
      _M0L2__S942 = _M0L6_2atmpS971;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS941);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\izhikevich_postspike\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples37izhikevich__postspike__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS938);
  moonbit_decref_cycle_free(_M0L12async__testsS938);
  moonbit_flush_cycles();
  return 0;
}