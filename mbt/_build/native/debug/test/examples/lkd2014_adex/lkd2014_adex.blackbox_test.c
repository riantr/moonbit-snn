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

struct _M0TP26RiantR8snn__mbt13AdExPostSpike;

struct _M0TWRPC15error5ErrorEs;

struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF;

struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1135;

struct _M0BTPB6Logger;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0TPB6Logger;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TPB5ArrayGUsiEE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TWRPC15error5ErrorEu;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TPB8MutLocalGiE;

struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp;

struct _M0TPB4Show;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0TP26RiantR8snn__mbt10AdExSinExp;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter;

struct _M0TPB5ArrayGbE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0TPB8MutLocalGdE;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TP26RiantR8snn__mbt12PoissonFixed;

struct _M0TPB5ArrayGsE;

struct _M0TWEu;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1130;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure {
  moonbit_string_t $0;
  
};

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError {
  moonbit_string_t $0;
  
};

struct _M0TP26RiantR8snn__mbt13AdExPostSpike {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  
};

struct _M0TWRPC15error5ErrorEs {
  moonbit_string_t(* code)(struct _M0TWRPC15error5ErrorEs*, void*);
  
};

struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF {
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* $0;
  struct _M0TPB5ArrayGiE* $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* $3;
  
};

struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1135 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0BTPB6Logger {
  int32_t(* $method_0)(void*, moonbit_string_t);
  int32_t(* $method_1)(void*, moonbit_string_t, int32_t, int32_t);
  int32_t(* $method_2)(void*, struct _M0TPC16string10StringView);
  int32_t(* $method_3)(void*, int32_t);
  int32_t(* $method_4)(void*, struct _M0TPB4Show);
  int32_t(* $method_5)(void*, struct _M0TPB4Show);
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
};

struct _M0TPB5ArrayGUsiEE {
  struct _M0TUsiE** $0;
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

struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp {
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* $0;
  moonbit_string_t $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  int32_t $4;
  
};

struct _M0TPB4Show {
  struct _M0BTPB4Show* $0;
  void* $1;
  
};

struct _M0TP26RiantR8snn__mbt9PostSpike {
  float $0;
  
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

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error {
  struct moonbit_result_0(* code)(
    struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error*,
    struct _M0TWuEu*,
    struct _M0TWRPC15error5ErrorEu*
  );
  
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

struct _M0TWuEu {
  int32_t(* code)(struct _M0TWuEu*, int32_t);
  
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

struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1130 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1142(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1135(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1130(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1107(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1100(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

int32_t _M0FP26RiantR8snn__mbt19record__one__sinexp(
  struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp*,
  float
);

struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp* _M0MP26RiantR8snn__mbt17MonitorAdExSinExp9new__fire(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp*,
  int32_t
);

struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0MP26RiantR8snn__mbt10AdExSinExp3new(
  int32_t,
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0MP26RiantR8snn__mbt10AdExSinExp16new__with__spike(
  int32_t,
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter*,
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0MP26RiantR8snn__mbt19AdExSinExpParameter9lkd__adex(
  
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

struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0MP26RiantR8snn__mbt13AdExPostSpike3new(
  
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

float _M0FP26RiantR8snn__mbt21monitor__firing__rate(struct _M0TPB5ArrayGfE*);

int32_t _M0FP26RiantR8snn__mbt22monitor__count__spikes(
  struct _M0TPB5ArrayGfE*
);

int32_t _M0FP26RiantR8snn__mbt10count__nnz(struct _M0TPB5ArrayGfE*);

int32_t _M0FP26RiantR8snn__mbt18simulate__step__if(
  struct _M0TP26RiantR8snn__mbt2IF*,
  float
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

int32_t _M0FP26RiantR8snn__mbt20route__pre__to__post(
  struct _M0TP26RiantR8snn__mbt2IF*,
  struct _M0TP26RiantR8snn__mbt10AdExSinExp*,
  struct _M0TPB5ArrayGfE*,
  int32_t,
  int32_t
);

struct _M0TPB5ArrayGfE* _M0FP26RiantR8snn__mbt21make__random__weights(
  int32_t,
  int32_t,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
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

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE*);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_25 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_23 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_16 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[117]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 116, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 108, 107, 100, 50, 48, 49, 52, 95, 
    97, 100, 101, 120, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 
    101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 
    116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 
    108, 74, 115, 69, 114, 114, 111, 114, 46, 77, 111, 111, 110, 66, 
    105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 
    116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 0
  };

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
} const moonbit_string_literal_22 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_20 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_17 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_15 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_11 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 119, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[25]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 24, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 114, 101, 115, 117, 108, 116, 34, 
    44, 34, 102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[119]; 
} const moonbit_string_literal_39 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 118, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 108, 107, 100, 50, 48, 49, 52, 95, 
    97, 100, 101, 120, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 
    101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 
    116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 
    108, 83, 107, 105, 112, 84, 101, 115, 116, 46, 77, 111, 111, 110, 
    66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 
    110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 
    116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_13 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_32 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_26 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_12 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_35 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_21 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_24 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_10 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 102, 105, 
    114, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_36 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 73, 110, 115, 112, 101, 
    99, 116, 69, 114, 114, 111, 114, 46, 73, 110, 115, 112, 101, 99, 
    116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_33 =
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
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

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
} const moonbit_string_literal_19 =
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
} const moonbit_string_literal_14 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_18 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1142$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1142
  };

uint32_t const moonbit_layout_table_data[95] =
  {
    sizeof(struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1130)
    / 4, 1,
    offsetof(struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1130, $1)
    / 4
    * 2,
    sizeof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1135)
    / 4, 1,
    offsetof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1135, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp) / 4, 4,
    offsetof(struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp, $3) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt10AdExSinExp) / 4, 15,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $7) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $8) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $9) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $10) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $11) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $12) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $13) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $14) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt10AdExSinExp, $15) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2485
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1163,
  moonbit_string_t _M0L8filenameS1132,
  int32_t _M0L5indexS1134
) {
  struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1130* _closure_2512;
  struct _M0TWEu* _M0L13handle__startS1130;
  struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1135* _closure_2513;
  struct _M0TWssbEu* _M0L14handle__resultS1135;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1142;
  void* _M0L11_2atry__errS1157;
  struct moonbit_result_0 _tmp_2515;
  int32_t _handle__error__result_2516;
  int32_t _M0L6_2atmpS2473;
  void* _M0L3errS1158;
  moonbit_string_t _M0L4nameS1160;
  struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1161;
  moonbit_string_t _M0L7_2anameS1162;
  int32_t _M0L6_2acntS2506;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1132);
  _closure_2512
  = (struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1130*)moonbit_malloc(sizeof(struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1130));
  Moonbit_object_header(_closure_2512)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2512->code
  = &_M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1130;
  _closure_2512->$0 = _M0L5indexS1134;
  _closure_2512->$1 = _M0L8filenameS1132;
  _M0L13handle__startS1130 = (struct _M0TWEu*)_closure_2512;
  moonbit_incref_cycle_free(_M0L8filenameS1132);
  _closure_2513
  = (struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1135*)moonbit_malloc(sizeof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1135));
  Moonbit_object_header(_closure_2513)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2513->code
  = &_M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1135;
  _closure_2513->$0 = _M0L5indexS1134;
  _closure_2513->$1 = _M0L8filenameS1132;
  _M0L14handle__resultS1135 = (struct _M0TWssbEu*)_closure_2513;
  _M0L17error__to__stringS1142
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1142$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2515
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1163, _M0L8filenameS1132, _M0L5indexS1134, _M0L13handle__startS1130, _M0L14handle__resultS1135, _M0L17error__to__stringS1142);
  if (_tmp_2515.tag) {
    int32_t const _M0L5_2aokS2482 = _tmp_2515.data.ok;
    _handle__error__result_2516 = _M0L5_2aokS2482;
  } else {
    void* const _M0L6_2aerrS2483 = _tmp_2515.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1142);
    moonbit_decref_cycle_free(_M0L13handle__startS1130);
    _M0L11_2atry__errS1157 = _M0L6_2aerrS2483;
    goto join_1156;
  }
  if (_handle__error__result_2516) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1142);
    moonbit_decref_cycle_free(_M0L13handle__startS1130);
    _M0L6_2atmpS2473 = 1;
  } else {
    struct moonbit_result_0 _tmp_2517;
    int32_t _handle__error__result_2518;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2517
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1163, _M0L8filenameS1132, _M0L5indexS1134, _M0L13handle__startS1130, _M0L14handle__resultS1135, _M0L17error__to__stringS1142);
    if (_tmp_2517.tag) {
      int32_t const _M0L5_2aokS2480 = _tmp_2517.data.ok;
      _handle__error__result_2518 = _M0L5_2aokS2480;
    } else {
      void* const _M0L6_2aerrS2481 = _tmp_2517.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1142);
      moonbit_decref_cycle_free(_M0L13handle__startS1130);
      _M0L11_2atry__errS1157 = _M0L6_2aerrS2481;
      goto join_1156;
    }
    if (_handle__error__result_2518) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1142);
      moonbit_decref_cycle_free(_M0L13handle__startS1130);
      _M0L6_2atmpS2473 = 1;
    } else {
      struct moonbit_result_0 _tmp_2519;
      int32_t _handle__error__result_2520;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2519
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1163, _M0L8filenameS1132, _M0L5indexS1134, _M0L13handle__startS1130, _M0L14handle__resultS1135, _M0L17error__to__stringS1142);
      if (_tmp_2519.tag) {
        int32_t const _M0L5_2aokS2478 = _tmp_2519.data.ok;
        _handle__error__result_2520 = _M0L5_2aokS2478;
      } else {
        void* const _M0L6_2aerrS2479 = _tmp_2519.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1142);
        moonbit_decref_cycle_free(_M0L13handle__startS1130);
        _M0L11_2atry__errS1157 = _M0L6_2aerrS2479;
        goto join_1156;
      }
      if (_handle__error__result_2520) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1142);
        moonbit_decref_cycle_free(_M0L13handle__startS1130);
        _M0L6_2atmpS2473 = 1;
      } else {
        struct moonbit_result_0 _tmp_2521;
        int32_t _handle__error__result_2522;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2521
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1163, _M0L8filenameS1132, _M0L5indexS1134, _M0L13handle__startS1130, _M0L14handle__resultS1135, _M0L17error__to__stringS1142);
        if (_tmp_2521.tag) {
          int32_t const _M0L5_2aokS2476 = _tmp_2521.data.ok;
          _handle__error__result_2522 = _M0L5_2aokS2476;
        } else {
          void* const _M0L6_2aerrS2477 = _tmp_2521.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1142);
          moonbit_decref_cycle_free(_M0L13handle__startS1130);
          _M0L11_2atry__errS1157 = _M0L6_2aerrS2477;
          goto join_1156;
        }
        if (_handle__error__result_2522) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1142);
          moonbit_decref_cycle_free(_M0L13handle__startS1130);
          _M0L6_2atmpS2473 = 1;
        } else {
          struct moonbit_result_0 _tmp_2523;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2523
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1163, _M0L8filenameS1132, _M0L5indexS1134, _M0L13handle__startS1130, _M0L14handle__resultS1135, _M0L17error__to__stringS1142);
          moonbit_decref_cycle_free(_M0L13handle__startS1130);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1142);
          if (_tmp_2523.tag) {
            int32_t const _M0L5_2aokS2474 = _tmp_2523.data.ok;
            _M0L6_2atmpS2473 = _M0L5_2aokS2474;
          } else {
            void* const _M0L6_2aerrS2475 = _tmp_2523.data.err;
            _M0L11_2atry__errS1157 = _M0L6_2aerrS2475;
            goto join_1156;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2473) {
    void* _M0L132RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2484 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L132RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2484)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L132RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2484)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1157
    = _M0L132RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2484;
    goto join_1156;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1135);
  }
  goto joinlet_2514;
  join_1156:;
  _M0L3errS1158 = _M0L11_2atry__errS1157;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1161
  = (struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1158;
  _M0L7_2anameS1162 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1161->$0;
  _M0L6_2acntS2506
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1161));
  if (_M0L6_2acntS2506 > 1) {
    int32_t _M0L11_2anew__cntS2507 = _M0L6_2acntS2506 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1161), _M0L11_2anew__cntS2507);
    moonbit_incref_cycle_free(_M0L7_2anameS1162);
  } else if (_M0L6_2acntS2506 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1161);
  }
  _M0L4nameS1160 = _M0L7_2anameS1162;
  goto join_1159;
  goto joinlet_2524;
  join_1159:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1135(_M0L14handle__resultS1135, _M0L4nameS1160, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1135);
  moonbit_decref_cycle_free(_M0L4nameS1160);
  joinlet_2524:;
  joinlet_2514:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1142(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2472,
  void* _M0L3errS1143
) {
  void* _M0L1eS1145;
  moonbit_string_t _M0L1eS1147;
  moonbit_string_t _result_2527;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1143)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1148 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1143;
      moonbit_string_t _M0L4_2aeS1149 = _M0L10_2aFailureS1148->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1149);
      _M0L1eS1147 = _M0L4_2aeS1149;
      goto join_1146;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1150 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1143;
      moonbit_string_t _M0L4_2aeS1151 = _M0L15_2aInspectErrorS1150->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1151);
      _M0L1eS1147 = _M0L4_2aeS1151;
      goto join_1146;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1152 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1143;
      moonbit_string_t _M0L4_2aeS1153 = _M0L16_2aSnapshotErrorS1152->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1153);
      _M0L1eS1147 = _M0L4_2aeS1153;
      goto join_1146;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1154 =
        (struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1143;
      moonbit_string_t _M0L4_2aeS1155 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1154->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1155);
      _M0L1eS1147 = _M0L4_2aeS1155;
      goto join_1146;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1143);
      _M0L1eS1145 = _M0L3errS1143;
      goto join_1144;
      break;
    }
  }
  join_1146:;
  return _M0L1eS1147;
  join_1144:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _result_2527 = _M0FP15Error10to__string(_M0L1eS1145);
  moonbit_decref_cycle_free(_M0L1eS1145);
  return _result_2527;
}

int32_t _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1135(
  struct _M0TWssbEu* _M0L6_2aenvS2469,
  moonbit_string_t _M0L10__testnameS1136,
  moonbit_string_t _M0L7messageS1137,
  int32_t _M0L7skippedS1138
) {
  struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1135* _M0L14_2acasted__envS2470;
  moonbit_string_t _M0L8filenameS1132;
  int32_t _M0L5indexS1134;
  moonbit_string_t _M0L10file__nameS1139;
  moonbit_string_t _M0L7messageS1140;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1141;
  moonbit_string_t _M0L6_2atmpS2471;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2470
  = (struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1135*)_M0L6_2aenvS2469;
  _M0L8filenameS1132 = _M0L14_2acasted__envS2470->$1;
  _M0L5indexS1134 = _M0L14_2acasted__envS2470->$0;
  if (!_M0L7skippedS1138 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1139
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1132, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1140
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1137, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1141
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1141, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1141, _M0L10file__nameS1139);
  moonbit_decref_cycle_free(_M0L10file__nameS1139);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1141, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1141, _M0L5indexS1134);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1141, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1141, _M0L7messageS1140);
  moonbit_decref_cycle_free(_M0L7messageS1140);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1141, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2471
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1141);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1141);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2471);
  moonbit_decref_cycle_free(_M0L6_2atmpS2471);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1130(
  struct _M0TWEu* _M0L6_2aenvS2466
) {
  struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1130* _M0L14_2acasted__envS2467;
  moonbit_string_t _M0L8filenameS1132;
  int32_t _M0L5indexS1134;
  moonbit_string_t _M0L10file__nameS1131;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1133;
  moonbit_string_t _M0L6_2atmpS2468;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2467
  = (struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__adex__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1130*)_M0L6_2aenvS2466;
  _M0L8filenameS1132 = _M0L14_2acasted__envS2467->$1;
  _M0L5indexS1134 = _M0L14_2acasted__envS2467->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1131
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1132, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1133
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1133, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1133, _M0L10file__nameS1131);
  moonbit_decref_cycle_free(_M0L10file__nameS1131);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1133, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1133, _M0L5indexS1134);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1133, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2468
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1133);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1133);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2468);
  moonbit_decref_cycle_free(_M0L6_2atmpS2468);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1100;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1107;
  struct _M0TUsiE** _M0L6_2atmpS2465;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1114;
  moonbit_string_t* _M0L9cli__argsS1115;
  moonbit_string_t _M0L6_2atmpS2464;
  moonbit_string_t _M0L6_2atmpS2463;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1116;
  int32_t _M0L7_2abindS1117;
  moonbit_string_t* _M0L7_2abindS1118;
  int32_t _M0L6_2acntS2508;
  int32_t _M0L2__S1119;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1100 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1107 = 0;
  _M0L6_2atmpS2465 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1114
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1114)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1114->$0 = _M0L6_2atmpS2465;
  _M0L16file__and__indexS1114->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1115
  = _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1115)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2464 = (moonbit_string_t)_M0L9cli__argsS1115[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2464);
  moonbit_decref_cycle_free(_M0L9cli__argsS1115);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2463
  = _M0MP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2464);
  moonbit_decref_cycle_free(_M0L6_2atmpS2464);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1116
  = _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1107(_M0L51moonbit__test__driver__internal__split__mbt__stringS1107, _M0L6_2atmpS2463, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2463);
  _M0L7_2abindS1117 = _M0L10test__argsS1116->$1;
  _M0L7_2abindS1118 = _M0L10test__argsS1116->$0;
  _M0L6_2acntS2508
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1116));
  if (_M0L6_2acntS2508 > 1) {
    int32_t _M0L11_2anew__cntS2509 = _M0L6_2acntS2508 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1116), _M0L11_2anew__cntS2509);
    moonbit_incref_cycle_free(_M0L7_2abindS1118);
  } else if (_M0L6_2acntS2508 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1116);
  }
  _M0L2__S1119 = 0;
  while (1) {
    if (_M0L2__S1119 < _M0L7_2abindS1117) {
      moonbit_string_t _M0L3argS1120 =
        (moonbit_string_t)_M0L7_2abindS1118[_M0L2__S1119];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1121;
      moonbit_string_t _M0L4fileS1122;
      moonbit_string_t _M0L5rangeS1123;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1124;
      moonbit_string_t _M0L6_2atmpS2461;
      int32_t _M0L5startS1125;
      moonbit_string_t _M0L6_2atmpS2460;
      int32_t _M0L3endS1126;
      int32_t _M0L1iS1127;
      int32_t _M0L6_2atmpS2462;
      moonbit_incref_cycle_free(_M0L3argS1120);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1121
      = _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1107(_M0L51moonbit__test__driver__internal__split__mbt__stringS1107, _M0L3argS1120, 58);
      moonbit_decref_cycle_free(_M0L3argS1120);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1122
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1121, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1123
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1121, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1121);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1124
      = _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1107(_M0L51moonbit__test__driver__internal__split__mbt__stringS1107, _M0L5rangeS1123, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1123);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2461
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1124, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1125
      = _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1100(_M0L45moonbit__test__driver__internal__parse__int__S1100, _M0L6_2atmpS2461);
      moonbit_decref_cycle_free(_M0L6_2atmpS2461);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2460
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1124, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1124);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1126
      = _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1100(_M0L45moonbit__test__driver__internal__parse__int__S1100, _M0L6_2atmpS2460);
      moonbit_decref_cycle_free(_M0L6_2atmpS2460);
      _M0L1iS1127 = _M0L5startS1125;
      while (1) {
        if (_M0L1iS1127 < _M0L3endS1126) {
          struct _M0TUsiE* _M0L8_2atupleS2458;
          int32_t _M0L6_2atmpS2459;
          moonbit_incref_cycle_free(_M0L4fileS1122);
          _M0L8_2atupleS2458
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS2458)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS2458->$0 = _M0L4fileS1122;
          _M0L8_2atupleS2458->$1 = _M0L1iS1127;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1114, _M0L8_2atupleS2458);
          _M0L6_2atmpS2459 = _M0L1iS1127 + 1;
          _M0L1iS1127 = _M0L6_2atmpS2459;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1122);
        }
        break;
      }
      _M0L6_2atmpS2462 = _M0L2__S1119 + 1;
      _M0L2__S1119 = _M0L6_2atmpS2462;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1118);
    }
    break;
  }
  return _M0L16file__and__indexS1114;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1107(
  int32_t _M0L6_2aenvS2439,
  moonbit_string_t _M0L1sS1108,
  int32_t _M0L3sepS1109
) {
  moonbit_string_t* _M0L6_2atmpS2457;
  struct _M0TPB5ArrayGsE* _M0L3resS1110;
  struct _M0TPB8MutLocalGiE* _M0L1iS1111;
  struct _M0TPB8MutLocalGiE* _M0L5startS1112;
  int32_t _M0L3valS2452;
  int32_t _M0L6_2atmpS2453;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2457 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1110
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1110)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1110->$0 = _M0L6_2atmpS2457;
  _M0L3resS1110->$1 = 0;
  _M0L1iS1111
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1111)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1111->$0 = 0;
  _M0L5startS1112
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1112)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1112->$0 = 0;
  while (1) {
    int32_t _M0L3valS2440 = _M0L1iS1111->$0;
    int32_t _M0L6_2atmpS2441 = Moonbit_array_length(_M0L1sS1108);
    if (_M0L3valS2440 < _M0L6_2atmpS2441) {
      int32_t _M0L3valS2444 = _M0L1iS1111->$0;
      int32_t _M0L6_2atmpS2443;
      int32_t _M0L6_2atmpS2442;
      int32_t _M0L3valS2451;
      int32_t _M0L6_2atmpS2450;
      if (
        _M0L3valS2444 < 0
        || _M0L3valS2444 >= Moonbit_array_length(_M0L1sS1108)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2443 = _M0L1sS1108[_M0L3valS2444];
      _M0L6_2atmpS2442 = _M0L6_2atmpS2443;
      if (_M0L6_2atmpS2442 == _M0L3sepS1109) {
        int32_t _M0L3valS2446 = _M0L5startS1112->$0;
        int32_t _M0L3valS2447 = _M0L1iS1111->$0;
        moonbit_string_t _M0L6_2atmpS2445;
        int32_t _M0L3valS2449;
        int32_t _M0L6_2atmpS2448;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS2445
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1108, _M0L3valS2446, _M0L3valS2447);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1110, _M0L6_2atmpS2445);
        _M0L3valS2449 = _M0L1iS1111->$0;
        _M0L6_2atmpS2448 = _M0L3valS2449 + 1;
        _M0L5startS1112->$0 = _M0L6_2atmpS2448;
      }
      _M0L3valS2451 = _M0L1iS1111->$0;
      _M0L6_2atmpS2450 = _M0L3valS2451 + 1;
      _M0L1iS1111->$0 = _M0L6_2atmpS2450;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1111);
    }
    break;
  }
  _M0L3valS2452 = _M0L5startS1112->$0;
  _M0L6_2atmpS2453 = Moonbit_array_length(_M0L1sS1108);
  if (_M0L3valS2452 < _M0L6_2atmpS2453) {
    int32_t _M0L3valS2455 = _M0L5startS1112->$0;
    int32_t _M0L6_2atmpS2456;
    moonbit_string_t _M0L6_2atmpS2454;
    moonbit_decref_cycle_free(_M0L5startS1112);
    _M0L6_2atmpS2456 = Moonbit_array_length(_M0L1sS1108);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS2454
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1108, _M0L3valS2455, _M0L6_2atmpS2456);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1110, _M0L6_2atmpS2454);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1112);
  }
  return _M0L3resS1110;
}

int32_t _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1100(
  int32_t _M0L6_2aenvS2432,
  moonbit_string_t _M0L1sS1101
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1102;
  int32_t _M0L3lenS1103;
  int32_t _M0L7_2abindS1104;
  int32_t _M0L1iS1105;
  int32_t _result_2532;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1102
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1102)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1102->$0 = 0;
  _M0L3lenS1103 = Moonbit_array_length(_M0L1sS1101);
  _M0L7_2abindS1104 = 0;
  _M0L1iS1105 = _M0L7_2abindS1104;
  while (1) {
    if (_M0L1iS1105 < _M0L3lenS1103) {
      int32_t _M0L3valS2437 = _M0L3resS1102->$0;
      int32_t _M0L6_2atmpS2434 = _M0L3valS2437 * 10;
      int32_t _M0L6_2atmpS2436;
      int32_t _M0L6_2atmpS2435;
      int32_t _M0L6_2atmpS2433;
      int32_t _M0L6_2atmpS2438;
      if (
        _M0L1iS1105 < 0 || _M0L1iS1105 >= Moonbit_array_length(_M0L1sS1101)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2436 = _M0L1sS1101[_M0L1iS1105];
      _M0L6_2atmpS2435 = _M0L6_2atmpS2436 - 48;
      _M0L6_2atmpS2433 = _M0L6_2atmpS2434 + _M0L6_2atmpS2435;
      _M0L3resS1102->$0 = _M0L6_2atmpS2433;
      _M0L6_2atmpS2438 = _M0L1iS1105 + 1;
      _M0L1iS1105 = _M0L6_2atmpS2438;
      continue;
    }
    break;
  }
  _result_2532 = _M0L3resS1102->$0;
  moonbit_decref_cycle_free(_M0L3resS1102);
  return _result_2532;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1099
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1099);
  return _M0L4selfS1099;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1069,
  moonbit_string_t _M0L12_2adiscard__S1070,
  int32_t _M0L12_2adiscard__S1071,
  struct _M0TWEu* _M0L12_2adiscard__S1072,
  struct _M0TWssbEu* _M0L12_2adiscard__S1073,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1074
) {
  struct moonbit_result_0 _result_2533;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _result_2533.tag = 1;
  _result_2533.data.ok = 0;
  return _result_2533;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1075,
  moonbit_string_t _M0L12_2adiscard__S1076,
  int32_t _M0L12_2adiscard__S1077,
  struct _M0TWEu* _M0L12_2adiscard__S1078,
  struct _M0TWssbEu* _M0L12_2adiscard__S1079,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1080
) {
  struct moonbit_result_0 _result_2534;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _result_2534.tag = 1;
  _result_2534.data.ok = 0;
  return _result_2534;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1081,
  moonbit_string_t _M0L12_2adiscard__S1082,
  int32_t _M0L12_2adiscard__S1083,
  struct _M0TWEu* _M0L12_2adiscard__S1084,
  struct _M0TWssbEu* _M0L12_2adiscard__S1085,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1086
) {
  struct moonbit_result_0 _result_2535;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _result_2535.tag = 1;
  _result_2535.data.ok = 0;
  return _result_2535;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1087,
  moonbit_string_t _M0L12_2adiscard__S1088,
  int32_t _M0L12_2adiscard__S1089,
  struct _M0TWEu* _M0L12_2adiscard__S1090,
  struct _M0TWssbEu* _M0L12_2adiscard__S1091,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1092
) {
  struct moonbit_result_0 _result_2536;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _result_2536.tag = 1;
  _result_2536.data.ok = 0;
  return _result_2536;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1093,
  moonbit_string_t _M0L12_2adiscard__S1094,
  int32_t _M0L12_2adiscard__S1095,
  struct _M0TWEu* _M0L12_2adiscard__S1096,
  struct _M0TWssbEu* _M0L12_2adiscard__S1097,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1098
) {
  struct moonbit_result_0 _result_2537;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _result_2537.tag = 1;
  _result_2537.data.ok = 0;
  return _result_2537;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1068
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt19record__one__sinexp(
  struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp* _M0L1mS1028,
  float _M0L1tS1029
) {
  moonbit_string_t _M0L3symS2420;
  float _M0L1vS1027;
  struct _M0TPB5ArrayGfE* _M0L4dataS2418;
  struct _M0TPB5ArrayGfE* _M0L5timesS2419;
  #line 276 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L3symS2420 = _M0L1mS1028->$1;
  #line 277 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  if (
    _M0L3symS2420 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS2420)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS2420, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS2420) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L3popS2423 =
      _M0L1mS1028->$0;
    struct _M0TPB5ArrayGfE* _M0L1vS2421 = _M0L3popS2423->$3;
    int32_t _M0L6neuronS2422 = _M0L1mS1028->$4;
    #line 278 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
    _M0L1vS1027 = _M0MPC15array5Array2atGfE(_M0L1vS2421, _M0L6neuronS2422);
  } else {
    moonbit_string_t _M0L3symS2424 = _M0L1mS1028->$1;
    #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
    if (
      _M0L3symS2424 == (moonbit_string_t)moonbit_string_literal_10.data
      || Moonbit_array_length(_M0L3symS2424)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_10.data)
         && 0
            == memcmp(_M0L3symS2424, (moonbit_string_t)moonbit_string_literal_10.data, Moonbit_array_length(_M0L3symS2424) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L3popS2427 =
        _M0L1mS1028->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2425 = _M0L3popS2427->$5;
      int32_t _M0L6neuronS2426 = _M0L1mS1028->$4;
      #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2425, _M0L6neuronS2426)) {
        _M0L1vS1027 = 0x1p+0f;
      } else {
        _M0L1vS1027 = 0x0p+0f;
      }
    } else {
      moonbit_string_t _M0L3symS2428 = _M0L1mS1028->$1;
      #line 281 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (
        _M0L3symS2428 == (moonbit_string_t)moonbit_string_literal_11.data
        || Moonbit_array_length(_M0L3symS2428)
           == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_11.data)
           && 0
              == memcmp(_M0L3symS2428, (moonbit_string_t)moonbit_string_literal_11.data, Moonbit_array_length(_M0L3symS2428) * 2)
      ) {
        struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L3popS2431 =
          _M0L1mS1028->$0;
        struct _M0TPB5ArrayGfE* _M0L1wS2429 = _M0L3popS2431->$4;
        int32_t _M0L6neuronS2430 = _M0L1mS1028->$4;
        #line 282 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L1vS1027
        = _M0MPC15array5Array2atGfE(_M0L1wS2429, _M0L6neuronS2430);
      } else {
        _M0L1vS1027 = 0x0p+0f;
      }
    }
  }
  _M0L4dataS2418 = _M0L1mS1028->$2;
  #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0MPC15array5Array4pushGfE(_M0L4dataS2418, _M0L1vS1027);
  _M0L5timesS2419 = _M0L1mS1028->$3;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0MPC15array5Array4pushGfE(_M0L5timesS2419, _M0L1tS1029);
  return 0;
}

struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp* _M0MP26RiantR8snn__mbt17MonitorAdExSinExp9new__fire(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L3popS1025,
  int32_t _M0L6neuronS1026
) {
  float* _M0L6_2atmpS2417;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2414;
  float* _M0L6_2atmpS2416;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2415;
  struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp* _block_2538;
  #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L6_2atmpS2417 = moonbit_empty_float_array;
  _M0L6_2atmpS2414
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2414)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2414->$0 = _M0L6_2atmpS2417;
  _M0L6_2atmpS2414->$1 = 0;
  _M0L6_2atmpS2416 = moonbit_empty_float_array;
  _M0L6_2atmpS2415
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2415)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2415->$0 = _M0L6_2atmpS2416;
  _M0L6_2atmpS2415->$1 = 0;
  moonbit_incref_cycle_free(_M0L3popS1025);
  _block_2538
  = (struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt17MonitorAdExSinExp));
  Moonbit_object_header(_block_2538)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _block_2538->$0 = _M0L3popS1025;
  _block_2538->$1 = (moonbit_string_t)moonbit_string_literal_10.data;
  _block_2538->$2 = _M0L6_2atmpS2414;
  _block_2538->$3 = _M0L6_2atmpS2415;
  _block_2538->$4 = _M0L6neuronS1026;
  return _block_2538;
}

struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0MP26RiantR8snn__mbt10AdExSinExp3new(
  int32_t _M0L1nS1022,
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0L5paramS1023,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1024
) {
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L6_2atmpS2413;
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _result_2539;
  #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L6_2atmpS2413 = _M0MP26RiantR8snn__mbt13AdExPostSpike3new();
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _result_2539
  = _M0MP26RiantR8snn__mbt10AdExSinExp16new__with__spike(_M0L1nS1022, _M0L5paramS1023, _M0L6_2atmpS2413, _M0L3rngS1024);
  moonbit_decref_cycle_free(_M0L6_2atmpS2413);
  return _result_2539;
}

struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0MP26RiantR8snn__mbt10AdExSinExp16new__with__spike(
  int32_t _M0L1nS1002,
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0L5paramS1004,
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS1021,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1007
) {
  struct _M0TPB5ArrayGfE* _M0L1vS1001;
  float _M0L2vtS2411;
  float _M0L2vrS2412;
  float _M0L6spreadS1003;
  int32_t _M0L7_2abindS1005;
  int32_t _M0L1kS1006;
  struct _M0TPB5ArrayGfE* _M0L1wS1009;
  struct _M0TPB5ArrayGbE* _M0L4fireS1010;
  float _M0L2vtS2410;
  struct _M0TPB5ArrayGfE* _M0L9thresholdS1011;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1012;
  struct _M0TPB5ArrayGfE* _M0L1iS1013;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS1014;
  struct _M0TPB5ArrayGfE* _M0L2geS1015;
  struct _M0TPB5ArrayGfE* _M0L2giS1016;
  struct _M0TPB5ArrayGfE* _M0L3gluS1017;
  struct _M0TPB5ArrayGfE* _M0L4gabaS1018;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1019;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1020;
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _block_2541;
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1vS1001 = _M0MPC15array5Array4makeGfE(_M0L1nS1002, 0x0p+0f);
  _M0L2vtS2411 = _M0L5paramS1004->$2;
  _M0L2vrS2412 = _M0L5paramS1004->$3;
  _M0L6spreadS1003 = _M0L2vtS2411 - _M0L2vrS2412;
  _M0L7_2abindS1005 = 0;
  _M0L1kS1006 = _M0L7_2abindS1005;
  while (1) {
    if (_M0L1kS1006 < _M0L1nS1002) {
      float _M0L2vrS2406 = _M0L5paramS1004->$3;
      float _M0L6_2atmpS2408;
      float _M0L6_2atmpS2407;
      float _M0L6_2atmpS2405;
      int32_t _M0L6_2atmpS2409;
      #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2408 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1007);
      _M0L6_2atmpS2407 = _M0L6_2atmpS2408 * _M0L6spreadS1003;
      _M0L6_2atmpS2405 = _M0L2vrS2406 + _M0L6_2atmpS2407;
      #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1001, _M0L1kS1006, _M0L6_2atmpS2405);
      _M0L6_2atmpS2409 = _M0L1kS1006 + 1;
      _M0L1kS1006 = _M0L6_2atmpS2409;
      continue;
    }
    break;
  }
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1wS1009 = _M0MPC15array5Array4makeGfE(_M0L1nS1002, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L4fireS1010 = _M0MPC15array5Array4makeGbE(_M0L1nS1002, 0);
  _M0L2vtS2410 = _M0L5paramS1004->$2;
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L9thresholdS1011
  = _M0MPC15array5Array4makeGfE(_M0L1nS1002, _M0L2vtS2410);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L4tabsS1012 = _M0MPC15array5Array4makeGiE(_M0L1nS1002, 1);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1iS1013 = _M0MPC15array5Array4makeGfE(_M0L1nS1002, 0x0p+0f);
  #line 132 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L9syn__currS1014 = _M0MPC15array5Array4makeGfE(_M0L1nS1002, 0x0p+0f);
  #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L2geS1015 = _M0MPC15array5Array4makeGfE(_M0L1nS1002, 0x0p+0f);
  #line 134 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L2giS1016 = _M0MPC15array5Array4makeGfE(_M0L1nS1002, 0x0p+0f);
  #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L3gluS1017 = _M0MPC15array5Array4makeGfE(_M0L1nS1002, 0x0p+0f);
  #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L4gabaS1018 = _M0MPC15array5Array4makeGfE(_M0L1nS1002, 0x0p+0f);
  #line 137 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L7gsyn__eS1019 = _M0MPC15array5Array4makeGfE(_M0L1nS1002, 0x1p+0f);
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L7gsyn__iS1020 = _M0MPC15array5Array4makeGfE(_M0L1nS1002, 0x1p+0f);
  moonbit_incref_cycle_free(_M0L5paramS1004);
  moonbit_incref_cycle_free(_M0L5spikeS1021);
  _block_2541
  = (struct _M0TP26RiantR8snn__mbt10AdExSinExp*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt10AdExSinExp));
  Moonbit_object_header(_block_2541)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 27, 0);
  _block_2541->$0 = _M0L5paramS1004;
  _block_2541->$1 = _M0L5spikeS1021;
  _block_2541->$2 = _M0L1nS1002;
  _block_2541->$3 = _M0L1vS1001;
  _block_2541->$4 = _M0L1wS1009;
  _block_2541->$5 = _M0L4fireS1010;
  _block_2541->$6 = _M0L9thresholdS1011;
  _block_2541->$7 = _M0L4tabsS1012;
  _block_2541->$8 = _M0L1iS1013;
  _block_2541->$9 = _M0L9syn__currS1014;
  _block_2541->$10 = _M0L2geS1015;
  _block_2541->$11 = _M0L2giS1016;
  _block_2541->$12 = _M0L3gluS1017;
  _block_2541->$13 = _M0L4gabaS1018;
  _block_2541->$14 = _M0L7gsyn__eS1019;
  _block_2541->$15 = _M0L7gsyn__iS1020;
  _block_2541->$16 = 0x0p+0f;
  _block_2541->$17 = -0x1.2cp+6f;
  _block_2541->$18 = 0x1.8p+2f;
  _block_2541->$19 = 0x1p+1f;
  return _block_2541;
}

struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0MP26RiantR8snn__mbt19AdExSinExpParameter9lkd__adex(
  
) {
  float _M0L1cS997;
  float _M0L2glS998;
  float _M0L2tmS999;
  float _M0L1rS1000;
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _block_2542;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1cS997 = 0x1.2cp+8f;
  _M0L2glS998 = 0x1.ep+3f;
  _M0L2tmS999 = _M0L1cS997 / _M0L2glS998;
  _M0L1rS1000 = 0x1p+0f / _M0L2glS998;
  _block_2542
  = (struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter));
  Moonbit_object_header(_block_2542)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2542->$0 = _M0L1cS997;
  _block_2542->$1 = _M0L2glS998;
  _block_2542->$2 = -0x1.ap+5f;
  _block_2542->$3 = -0x1.ep+5f;
  _block_2542->$4 = -0x1.18p+6f;
  _block_2542->$5 = _M0L2tmS999;
  _block_2542->$6 = _M0L1rS1000;
  _block_2542->$7 = 0x1p+1f;
  _block_2542->$8 = 0x1.2p+7f;
  _block_2542->$9 = 0x1p+2f;
  _block_2542->$10 = 0x1.42p+6f;
  return _block_2542;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter3new(
  
) {
  float _M0L1cS995;
  float _M0L2glS996;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_2543;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS995 = -0x1p+0f;
  _M0L2glS996 = -0x1p+0f;
  _block_2543
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_2543)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2543->$0 = _M0L1cS995;
  _block_2543->$1 = _M0L2glS996;
  _block_2543->$2 = 0x1.ep+3f;
  _block_2543->$3 = -0x1.9p+5f;
  _block_2543->$4 = -0x1.ep+5f;
  _block_2543->$5 = -0x1.18p+6f;
  _block_2543->$6 = 0x1.eb851eb851eb8p-5f;
  _block_2543->$7 = 0x1p+1f;
  _block_2543->$8 = 0x0p+0f;
  _block_2543->$9 = 0x0p+0f;
  _block_2543->$10 = 0x0p+0f;
  return _block_2543;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS969,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS971,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS974
) {
  struct _M0TPB5ArrayGfE* _M0L1vS968;
  float _M0L2vtS2403;
  float _M0L2vrS2404;
  float _M0L6spreadS970;
  int32_t _M0L7_2abindS972;
  int32_t _M0L1kS973;
  struct _M0TPB5ArrayGfE* _M0L1wS976;
  struct _M0TPB5ArrayGbE* _M0L4fireS977;
  struct _M0TPB5ArrayGiE* _M0L4tabsS978;
  struct _M0TPB5ArrayGfE* _M0L1iS979;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS980;
  struct _M0TPB5ArrayGfE* _M0L2geS981;
  struct _M0TPB5ArrayGfE* _M0L2giS982;
  struct _M0TPB5ArrayGfE* _M0L2heS983;
  struct _M0TPB5ArrayGfE* _M0L2hiS984;
  struct _M0TPB5ArrayGfE* _M0L3gluS985;
  struct _M0TPB5ArrayGfE* _M0L4gabaS986;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS987;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS988;
  float _M0L4e__eS989;
  float _M0L4e__iS990;
  float _M0L3treS991;
  float _M0L3tdeS992;
  float _M0L3triS993;
  float _M0L3tdiS994;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS2402;
  struct _M0TP26RiantR8snn__mbt2IF* _block_2545;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS968 = _M0MPC15array5Array4makeGfE(_M0L1nS969, 0x0p+0f);
  _M0L2vtS2403 = _M0L5paramS971->$3;
  _M0L2vrS2404 = _M0L5paramS971->$4;
  _M0L6spreadS970 = _M0L2vtS2403 - _M0L2vrS2404;
  _M0L7_2abindS972 = 0;
  _M0L1kS973 = _M0L7_2abindS972;
  while (1) {
    if (_M0L1kS973 < _M0L1nS969) {
      float _M0L2vrS2398 = _M0L5paramS971->$4;
      float _M0L6_2atmpS2400;
      float _M0L6_2atmpS2399;
      float _M0L6_2atmpS2397;
      int32_t _M0L6_2atmpS2401;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2400 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS974);
      _M0L6_2atmpS2399 = _M0L6_2atmpS2400 * _M0L6spreadS970;
      _M0L6_2atmpS2397 = _M0L2vrS2398 + _M0L6_2atmpS2399;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS968, _M0L1kS973, _M0L6_2atmpS2397);
      _M0L6_2atmpS2401 = _M0L1kS973 + 1;
      _M0L1kS973 = _M0L6_2atmpS2401;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS976 = _M0MPC15array5Array4makeGfE(_M0L1nS969, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS977 = _M0MPC15array5Array4makeGbE(_M0L1nS969, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS978 = _M0MPC15array5Array4makeGiE(_M0L1nS969, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS979 = _M0MPC15array5Array4makeGfE(_M0L1nS969, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS980 = _M0MPC15array5Array4makeGfE(_M0L1nS969, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS981 = _M0MPC15array5Array4makeGfE(_M0L1nS969, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS982 = _M0MPC15array5Array4makeGfE(_M0L1nS969, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS983 = _M0MPC15array5Array4makeGfE(_M0L1nS969, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS984 = _M0MPC15array5Array4makeGfE(_M0L1nS969, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS985 = _M0MPC15array5Array4makeGfE(_M0L1nS969, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS986 = _M0MPC15array5Array4makeGfE(_M0L1nS969, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS987 = _M0MPC15array5Array4makeGfE(_M0L1nS969, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS988 = _M0MPC15array5Array4makeGfE(_M0L1nS969, 0x1p+0f);
  _M0L4e__eS989 = 0x0p+0f;
  _M0L4e__iS990 = -0x1.2cp+6f;
  _M0L3treS991 = 0x1p+0f;
  _M0L3tdeS992 = 0x1.8p+2f;
  _M0L3triS993 = 0x1p-1f;
  _M0L3tdiS994 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS2402 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS971);
  _block_2545
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_2545)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 44, 0);
  _block_2545->$0 = _M0L5paramS971;
  _block_2545->$1 = _M0L6_2atmpS2402;
  _block_2545->$2 = _M0L1nS969;
  _block_2545->$3 = _M0L1vS968;
  _block_2545->$4 = _M0L1wS976;
  _block_2545->$5 = _M0L4fireS977;
  _block_2545->$6 = _M0L4tabsS978;
  _block_2545->$7 = _M0L1iS979;
  _block_2545->$8 = _M0L9syn__currS980;
  _block_2545->$9 = _M0L2geS981;
  _block_2545->$10 = _M0L2giS982;
  _block_2545->$11 = _M0L2heS983;
  _block_2545->$12 = _M0L2hiS984;
  _block_2545->$13 = _M0L3gluS985;
  _block_2545->$14 = _M0L4gabaS986;
  _block_2545->$15 = _M0L7gsyn__eS987;
  _block_2545->$16 = _M0L7gsyn__iS988;
  _block_2545->$17 = _M0L4e__eS989;
  _block_2545->$18 = _M0L4e__iS990;
  _block_2545->$19 = _M0L3treS991;
  _block_2545->$20 = _M0L3tdeS992;
  _block_2545->$21 = _M0L3triS993;
  _block_2545->$22 = _M0L3tdiS994;
  return _block_2545;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_2546;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_2546
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_2546)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2546->$0 = 0x1p+1f;
  return _block_2546;
}

struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0MP26RiantR8snn__mbt13AdExPostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _block_2547;
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _block_2547
  = (struct _M0TP26RiantR8snn__mbt13AdExPostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExPostSpike));
  Moonbit_object_header(_block_2547)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2547->$0 = 0x0p+0f;
  _block_2547->$1 = 0x1.4p+3f;
  _block_2547->$2 = 0x1.4p+3f;
  _block_2547->$3 = 0x1p+0f;
  _block_2547->$4 = 0x1p+0f;
  return _block_2547;
}

int32_t _M0FP26RiantR8snn__mbt18step__adex__sinexp(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS947,
  float _M0L2dtS962
) {
  int32_t _M0L1nS946;
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0L3p__S948;
  float _M0L2tmS949;
  float _M0L2vtS950;
  float _M0L2vrS951;
  float _M0L2elS952;
  float _M0L1rS953;
  float _M0L9dt__slopeS954;
  float _M0L2twS955;
  float _M0L1aS956;
  float _M0L1bS957;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS2396;
  float _M0L2atS958;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS2395;
  float _M0L6tau__aS959;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS2394;
  float _M0L11tabs__constS960;
  float _M0L6_2atmpS2393;
  int32_t _M0L11tabs__stepsS961;
  int32_t _M0L7_2abindS963;
  int32_t _M0L1iS964;
  #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS946 = _M0L1pS947->$2;
  _M0L3p__S948 = _M0L1pS947->$0;
  _M0L2tmS949 = _M0L3p__S948->$5;
  _M0L2vtS950 = _M0L3p__S948->$2;
  _M0L2vrS951 = _M0L3p__S948->$3;
  _M0L2elS952 = _M0L3p__S948->$4;
  _M0L1rS953 = _M0L3p__S948->$6;
  _M0L9dt__slopeS954 = _M0L3p__S948->$7;
  _M0L2twS955 = _M0L3p__S948->$8;
  _M0L1aS956 = _M0L3p__S948->$9;
  _M0L1bS957 = _M0L3p__S948->$10;
  _M0L5spikeS2396 = _M0L1pS947->$1;
  _M0L2atS958 = _M0L5spikeS2396->$0;
  _M0L5spikeS2395 = _M0L1pS947->$1;
  _M0L6tau__aS959 = _M0L5spikeS2395->$1;
  _M0L5spikeS2394 = _M0L1pS947->$1;
  _M0L11tabs__constS960 = _M0L5spikeS2394->$3;
  _M0L6_2atmpS2393 = _M0L11tabs__constS960 / _M0L2dtS962;
  #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L11tabs__stepsS961 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2393);
  _M0L7_2abindS963 = 0;
  _M0L1iS964 = _M0L7_2abindS963;
  while (1) {
    if (_M0L1iS964 < _M0L1nS946) {
      struct _M0TPB5ArrayGfE* _M0L1vS2306 = _M0L1pS947->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS2308 = _M0L1pS947->$5;
      float _M0L6_2atmpS2307;
      struct _M0TPB5ArrayGbE* _M0L4fireS2310;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2311;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2314;
      int32_t _M0L6_2atmpS2313;
      int32_t _M0L6_2atmpS2312;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2316;
      int32_t _M0L6_2atmpS2315;
      struct _M0TPB5ArrayGfE* _M0L1wS2317;
      struct _M0TPB5ArrayGfE* _M0L1wS2329;
      float _M0L6_2atmpS2319;
      struct _M0TPB5ArrayGfE* _M0L1vS2328;
      float _M0L6_2atmpS2327;
      float _M0L6_2atmpS2326;
      float _M0L6_2atmpS2323;
      struct _M0TPB5ArrayGfE* _M0L1wS2325;
      float _M0L6_2atmpS2324;
      float _M0L6_2atmpS2322;
      float _M0L6_2atmpS2321;
      float _M0L6_2atmpS2320;
      float _M0L6_2atmpS2318;
      float _M0L9exp__termS967;
      struct _M0TPB5ArrayGfE* _M0L1vS2330;
      struct _M0TPB5ArrayGfE* _M0L1vS2352;
      float _M0L6_2atmpS2332;
      struct _M0TPB5ArrayGfE* _M0L1vS2351;
      float _M0L6_2atmpS2350;
      float _M0L6_2atmpS2349;
      float _M0L6_2atmpS2348;
      float _M0L6_2atmpS2344;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2347;
      float _M0L6_2atmpS2346;
      float _M0L6_2atmpS2345;
      float _M0L6_2atmpS2340;
      struct _M0TPB5ArrayGfE* _M0L1wS2343;
      float _M0L6_2atmpS2342;
      float _M0L6_2atmpS2341;
      float _M0L6_2atmpS2336;
      struct _M0TPB5ArrayGfE* _M0L1iS2339;
      float _M0L6_2atmpS2338;
      float _M0L6_2atmpS2337;
      float _M0L6_2atmpS2335;
      float _M0L6_2atmpS2334;
      float _M0L6_2atmpS2333;
      float _M0L6_2atmpS2331;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2353;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2361;
      float _M0L6_2atmpS2355;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2360;
      float _M0L6_2atmpS2359;
      float _M0L6_2atmpS2358;
      float _M0L6_2atmpS2357;
      float _M0L6_2atmpS2356;
      float _M0L6_2atmpS2354;
      struct _M0TPB5ArrayGbE* _M0L4fireS2362;
      struct _M0TPB5ArrayGfE* _M0L1vS2365;
      float _M0L6_2atmpS2364;
      int32_t _M0L6_2atmpS2363;
      struct _M0TPB5ArrayGfE* _M0L1vS2366;
      struct _M0TPB5ArrayGbE* _M0L4fireS2368;
      float _M0L6_2atmpS2367;
      struct _M0TPB5ArrayGfE* _M0L1wS2370;
      struct _M0TPB5ArrayGbE* _M0L4fireS2372;
      float _M0L6_2atmpS2371;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2376;
      struct _M0TPB5ArrayGbE* _M0L4fireS2378;
      float _M0L6_2atmpS2377;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2382;
      struct _M0TPB5ArrayGbE* _M0L4fireS2384;
      int32_t _M0L6_2atmpS2383;
      int32_t _M0L6_2atmpS2305;
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2308, _M0L1iS964)) {
        _M0L6_2atmpS2307 = _M0L2vrS951;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS2309 = _M0L1pS947->$3;
        #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS2307 = _M0MPC15array5Array2atGfE(_M0L1vS2309, _M0L1iS964);
      }
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2306, _M0L1iS964, _M0L6_2atmpS2307);
      _M0L4fireS2310 = _M0L1pS947->$5;
      #line 212 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2310, _M0L1iS964, 0);
      _M0L4tabsS2311 = _M0L1pS947->$7;
      _M0L4tabsS2314 = _M0L1pS947->$7;
      #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2313
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2314, _M0L1iS964);
      _M0L6_2atmpS2312 = _M0L6_2atmpS2313 - 1;
      #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2311, _M0L1iS964, _M0L6_2atmpS2312);
      _M0L4tabsS2316 = _M0L1pS947->$7;
      #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2315
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2316, _M0L1iS964);
      if (_M0L6_2atmpS2315 > 0) {
        goto join_965;
      }
      _M0L1wS2317 = _M0L1pS947->$4;
      _M0L1wS2329 = _M0L1pS947->$4;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2319 = _M0MPC15array5Array2atGfE(_M0L1wS2329, _M0L1iS964);
      _M0L1vS2328 = _M0L1pS947->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2327 = _M0MPC15array5Array2atGfE(_M0L1vS2328, _M0L1iS964);
      _M0L6_2atmpS2326 = _M0L6_2atmpS2327 - _M0L2elS952;
      _M0L6_2atmpS2323 = _M0L1aS956 * _M0L6_2atmpS2326;
      _M0L1wS2325 = _M0L1pS947->$4;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2324 = _M0MPC15array5Array2atGfE(_M0L1wS2325, _M0L1iS964);
      _M0L6_2atmpS2322 = _M0L6_2atmpS2323 - _M0L6_2atmpS2324;
      _M0L6_2atmpS2321 = _M0L2dtS962 * _M0L6_2atmpS2322;
      _M0L6_2atmpS2320 = _M0L6_2atmpS2321 / _M0L2twS955;
      _M0L6_2atmpS2318 = _M0L6_2atmpS2319 + _M0L6_2atmpS2320;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS2317, _M0L1iS964, _M0L6_2atmpS2318);
      if (_M0L9dt__slopeS954 < 0x0p+0f) {
        _M0L9exp__termS967 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS2392 = _M0L1pS947->$3;
        float _M0L6_2atmpS2389;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2391;
        float _M0L6_2atmpS2390;
        float _M0L6_2atmpS2388;
        float _M0L6_2atmpS2387;
        float _M0L6_2atmpS2386;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS2389 = _M0MPC15array5Array2atGfE(_M0L1vS2392, _M0L1iS964);
        _M0L9thresholdS2391 = _M0L1pS947->$6;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS2390
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2391, _M0L1iS964);
        _M0L6_2atmpS2388 = _M0L6_2atmpS2389 - _M0L6_2atmpS2390;
        _M0L6_2atmpS2387 = _M0L6_2atmpS2388 / _M0L9dt__slopeS954;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS2386 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2387);
        _M0L9exp__termS967 = _M0L9dt__slopeS954 * _M0L6_2atmpS2386;
      }
      _M0L1vS2330 = _M0L1pS947->$3;
      _M0L1vS2352 = _M0L1pS947->$3;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2332 = _M0MPC15array5Array2atGfE(_M0L1vS2352, _M0L1iS964);
      _M0L1vS2351 = _M0L1pS947->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2350 = _M0MPC15array5Array2atGfE(_M0L1vS2351, _M0L1iS964);
      _M0L6_2atmpS2349 = _M0L6_2atmpS2350 - _M0L2elS952;
      _M0L6_2atmpS2348 = -_M0L6_2atmpS2349;
      _M0L6_2atmpS2344 = _M0L6_2atmpS2348 + _M0L9exp__termS967;
      _M0L9syn__currS2347 = _M0L1pS947->$9;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2346
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS2347, _M0L1iS964);
      _M0L6_2atmpS2345 = _M0L1rS953 * _M0L6_2atmpS2346;
      _M0L6_2atmpS2340 = _M0L6_2atmpS2344 - _M0L6_2atmpS2345;
      _M0L1wS2343 = _M0L1pS947->$4;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2342 = _M0MPC15array5Array2atGfE(_M0L1wS2343, _M0L1iS964);
      _M0L6_2atmpS2341 = _M0L1rS953 * _M0L6_2atmpS2342;
      _M0L6_2atmpS2336 = _M0L6_2atmpS2340 - _M0L6_2atmpS2341;
      _M0L1iS2339 = _M0L1pS947->$8;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2338 = _M0MPC15array5Array2atGfE(_M0L1iS2339, _M0L1iS964);
      _M0L6_2atmpS2337 = _M0L1rS953 * _M0L6_2atmpS2338;
      _M0L6_2atmpS2335 = _M0L6_2atmpS2336 + _M0L6_2atmpS2337;
      _M0L6_2atmpS2334 = _M0L2dtS962 * _M0L6_2atmpS2335;
      _M0L6_2atmpS2333 = _M0L6_2atmpS2334 / _M0L2tmS949;
      _M0L6_2atmpS2331 = _M0L6_2atmpS2332 + _M0L6_2atmpS2333;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2330, _M0L1iS964, _M0L6_2atmpS2331);
      _M0L9thresholdS2353 = _M0L1pS947->$6;
      _M0L9thresholdS2361 = _M0L1pS947->$6;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2355
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS2361, _M0L1iS964);
      _M0L9thresholdS2360 = _M0L1pS947->$6;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2359
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS2360, _M0L1iS964);
      _M0L6_2atmpS2358 = _M0L2vtS950 - _M0L6_2atmpS2359;
      _M0L6_2atmpS2357 = _M0L2dtS962 * _M0L6_2atmpS2358;
      _M0L6_2atmpS2356 = _M0L6_2atmpS2357 / _M0L6tau__aS959;
      _M0L6_2atmpS2354 = _M0L6_2atmpS2355 + _M0L6_2atmpS2356;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS2353, _M0L1iS964, _M0L6_2atmpS2354);
      _M0L4fireS2362 = _M0L1pS947->$5;
      _M0L1vS2365 = _M0L1pS947->$3;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2364 = _M0MPC15array5Array2atGfE(_M0L1vS2365, _M0L1iS964);
      _M0L6_2atmpS2363 = _M0L6_2atmpS2364 >= 0x0p+0f;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2362, _M0L1iS964, _M0L6_2atmpS2363);
      _M0L1vS2366 = _M0L1pS947->$3;
      _M0L4fireS2368 = _M0L1pS947->$5;
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2368, _M0L1iS964)) {
        _M0L6_2atmpS2367 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS2369 = _M0L1pS947->$3;
        #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS2367 = _M0MPC15array5Array2atGfE(_M0L1vS2369, _M0L1iS964);
      }
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2366, _M0L1iS964, _M0L6_2atmpS2367);
      _M0L1wS2370 = _M0L1pS947->$4;
      _M0L4fireS2372 = _M0L1pS947->$5;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2372, _M0L1iS964)) {
        struct _M0TPB5ArrayGfE* _M0L1wS2374 = _M0L1pS947->$4;
        float _M0L6_2atmpS2373;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS2373 = _M0MPC15array5Array2atGfE(_M0L1wS2374, _M0L1iS964);
        _M0L6_2atmpS2371 = _M0L6_2atmpS2373 + _M0L1bS957;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS2375 = _M0L1pS947->$4;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS2371 = _M0MPC15array5Array2atGfE(_M0L1wS2375, _M0L1iS964);
      }
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS2370, _M0L1iS964, _M0L6_2atmpS2371);
      _M0L9thresholdS2376 = _M0L1pS947->$6;
      _M0L4fireS2378 = _M0L1pS947->$5;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2378, _M0L1iS964)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2380 = _M0L1pS947->$6;
        float _M0L6_2atmpS2379;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS2379
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2380, _M0L1iS964);
        _M0L6_2atmpS2377 = _M0L6_2atmpS2379 + _M0L2atS958;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2381 = _M0L1pS947->$6;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS2377
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2381, _M0L1iS964);
      }
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS2376, _M0L1iS964, _M0L6_2atmpS2377);
      _M0L4tabsS2382 = _M0L1pS947->$7;
      _M0L4fireS2384 = _M0L1pS947->$5;
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2384, _M0L1iS964)) {
        _M0L6_2atmpS2383 = _M0L11tabs__stepsS961;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS2385 = _M0L1pS947->$7;
        #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS2383
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2385, _M0L1iS964);
      }
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2382, _M0L1iS964, _M0L6_2atmpS2383);
      goto join_965;
      goto joinlet_2549;
      join_965:;
      _M0L6_2atmpS2305 = _M0L1iS964 + 1;
      _M0L1iS964 = _M0L6_2atmpS2305;
      continue;
      joinlet_2549:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS942
) {
  int32_t _M0L1nS941;
  int32_t _M0L7_2abindS943;
  int32_t _M0L1iS944;
  #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS941 = _M0L1pS942->$2;
  _M0L7_2abindS943 = 0;
  _M0L1iS944 = _M0L7_2abindS943;
  while (1) {
    if (_M0L1iS944 < _M0L1nS941) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2282 = _M0L1pS942->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS2303 = _M0L1pS942->$10;
      float _M0L6_2atmpS2298;
      struct _M0TPB5ArrayGfE* _M0L1vS2302;
      float _M0L6_2atmpS2300;
      float _M0L4e__eS2301;
      float _M0L6_2atmpS2299;
      float _M0L6_2atmpS2295;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS2297;
      float _M0L6_2atmpS2296;
      float _M0L6_2atmpS2284;
      struct _M0TPB5ArrayGfE* _M0L2giS2294;
      float _M0L6_2atmpS2289;
      struct _M0TPB5ArrayGfE* _M0L1vS2293;
      float _M0L6_2atmpS2291;
      float _M0L4e__iS2292;
      float _M0L6_2atmpS2290;
      float _M0L6_2atmpS2286;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS2288;
      float _M0L6_2atmpS2287;
      float _M0L6_2atmpS2285;
      float _M0L6_2atmpS2283;
      int32_t _M0L6_2atmpS2304;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2298 = _M0MPC15array5Array2atGfE(_M0L2geS2303, _M0L1iS944);
      _M0L1vS2302 = _M0L1pS942->$3;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2300 = _M0MPC15array5Array2atGfE(_M0L1vS2302, _M0L1iS944);
      _M0L4e__eS2301 = _M0L1pS942->$16;
      _M0L6_2atmpS2299 = _M0L6_2atmpS2300 - _M0L4e__eS2301;
      _M0L6_2atmpS2295 = _M0L6_2atmpS2298 * _M0L6_2atmpS2299;
      _M0L7gsyn__eS2297 = _M0L1pS942->$14;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2296
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS2297, _M0L1iS944);
      _M0L6_2atmpS2284 = _M0L6_2atmpS2295 * _M0L6_2atmpS2296;
      _M0L2giS2294 = _M0L1pS942->$11;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2289 = _M0MPC15array5Array2atGfE(_M0L2giS2294, _M0L1iS944);
      _M0L1vS2293 = _M0L1pS942->$3;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2291 = _M0MPC15array5Array2atGfE(_M0L1vS2293, _M0L1iS944);
      _M0L4e__iS2292 = _M0L1pS942->$17;
      _M0L6_2atmpS2290 = _M0L6_2atmpS2291 - _M0L4e__iS2292;
      _M0L6_2atmpS2286 = _M0L6_2atmpS2289 * _M0L6_2atmpS2290;
      _M0L7gsyn__iS2288 = _M0L1pS942->$15;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2287
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS2288, _M0L1iS944);
      _M0L6_2atmpS2285 = _M0L6_2atmpS2286 * _M0L6_2atmpS2287;
      _M0L6_2atmpS2283 = _M0L6_2atmpS2284 + _M0L6_2atmpS2285;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS2282, _M0L1iS944, _M0L6_2atmpS2283);
      _M0L6_2atmpS2304 = _M0L1iS944 + 1;
      _M0L1iS944 = _M0L6_2atmpS2304;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS931,
  float _M0L2dtS936
) {
  int32_t _M0L1nS930;
  float _M0L6tau__eS932;
  float _M0L6tau__iS933;
  int32_t _M0L7_2abindS934;
  int32_t _M0L1iS935;
  int32_t _M0L7_2abindS938;
  int32_t _M0L1iS939;
  #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS930 = _M0L1pS931->$2;
  _M0L6tau__eS932 = _M0L1pS931->$18;
  _M0L6tau__iS933 = _M0L1pS931->$19;
  _M0L7_2abindS934 = 0;
  _M0L1iS935 = _M0L7_2abindS934;
  while (1) {
    if (_M0L1iS935 < _M0L1nS930) {
      struct _M0TPB5ArrayGfE* _M0L2geS2248 = _M0L1pS931->$10;
      struct _M0TPB5ArrayGfE* _M0L2geS2253 = _M0L1pS931->$10;
      float _M0L6_2atmpS2250;
      struct _M0TPB5ArrayGfE* _M0L3gluS2252;
      float _M0L6_2atmpS2251;
      float _M0L6_2atmpS2249;
      struct _M0TPB5ArrayGfE* _M0L2giS2254;
      struct _M0TPB5ArrayGfE* _M0L2giS2259;
      float _M0L6_2atmpS2256;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2258;
      float _M0L6_2atmpS2257;
      float _M0L6_2atmpS2255;
      struct _M0TPB5ArrayGfE* _M0L2geS2260;
      struct _M0TPB5ArrayGfE* _M0L2geS2268;
      float _M0L6_2atmpS2262;
      struct _M0TPB5ArrayGfE* _M0L2geS2267;
      float _M0L6_2atmpS2266;
      float _M0L6_2atmpS2265;
      float _M0L6_2atmpS2264;
      float _M0L6_2atmpS2263;
      float _M0L6_2atmpS2261;
      struct _M0TPB5ArrayGfE* _M0L2giS2269;
      struct _M0TPB5ArrayGfE* _M0L2giS2277;
      float _M0L6_2atmpS2271;
      struct _M0TPB5ArrayGfE* _M0L2giS2276;
      float _M0L6_2atmpS2275;
      float _M0L6_2atmpS2274;
      float _M0L6_2atmpS2273;
      float _M0L6_2atmpS2272;
      float _M0L6_2atmpS2270;
      int32_t _M0L6_2atmpS2278;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2250 = _M0MPC15array5Array2atGfE(_M0L2geS2253, _M0L1iS935);
      _M0L3gluS2252 = _M0L1pS931->$12;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2251 = _M0MPC15array5Array2atGfE(_M0L3gluS2252, _M0L1iS935);
      _M0L6_2atmpS2249 = _M0L6_2atmpS2250 + _M0L6_2atmpS2251;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS2248, _M0L1iS935, _M0L6_2atmpS2249);
      _M0L2giS2254 = _M0L1pS931->$11;
      _M0L2giS2259 = _M0L1pS931->$11;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2256 = _M0MPC15array5Array2atGfE(_M0L2giS2259, _M0L1iS935);
      _M0L4gabaS2258 = _M0L1pS931->$13;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2257
      = _M0MPC15array5Array2atGfE(_M0L4gabaS2258, _M0L1iS935);
      _M0L6_2atmpS2255 = _M0L6_2atmpS2256 + _M0L6_2atmpS2257;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS2254, _M0L1iS935, _M0L6_2atmpS2255);
      _M0L2geS2260 = _M0L1pS931->$10;
      _M0L2geS2268 = _M0L1pS931->$10;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2262 = _M0MPC15array5Array2atGfE(_M0L2geS2268, _M0L1iS935);
      _M0L2geS2267 = _M0L1pS931->$10;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2266 = _M0MPC15array5Array2atGfE(_M0L2geS2267, _M0L1iS935);
      _M0L6_2atmpS2265 = -_M0L6_2atmpS2266;
      _M0L6_2atmpS2264 = _M0L6_2atmpS2265 / _M0L6tau__eS932;
      _M0L6_2atmpS2263 = _M0L2dtS936 * _M0L6_2atmpS2264;
      _M0L6_2atmpS2261 = _M0L6_2atmpS2262 + _M0L6_2atmpS2263;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS2260, _M0L1iS935, _M0L6_2atmpS2261);
      _M0L2giS2269 = _M0L1pS931->$11;
      _M0L2giS2277 = _M0L1pS931->$11;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2271 = _M0MPC15array5Array2atGfE(_M0L2giS2277, _M0L1iS935);
      _M0L2giS2276 = _M0L1pS931->$11;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS2275 = _M0MPC15array5Array2atGfE(_M0L2giS2276, _M0L1iS935);
      _M0L6_2atmpS2274 = -_M0L6_2atmpS2275;
      _M0L6_2atmpS2273 = _M0L6_2atmpS2274 / _M0L6tau__iS933;
      _M0L6_2atmpS2272 = _M0L2dtS936 * _M0L6_2atmpS2273;
      _M0L6_2atmpS2270 = _M0L6_2atmpS2271 + _M0L6_2atmpS2272;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS2269, _M0L1iS935, _M0L6_2atmpS2270);
      _M0L6_2atmpS2278 = _M0L1iS935 + 1;
      _M0L1iS935 = _M0L6_2atmpS2278;
      continue;
    }
    break;
  }
  _M0L7_2abindS938 = 0;
  _M0L1iS939 = _M0L7_2abindS938;
  while (1) {
    if (_M0L1iS939 < _M0L1nS930) {
      struct _M0TPB5ArrayGfE* _M0L3gluS2279 = _M0L1pS931->$12;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2280;
      int32_t _M0L6_2atmpS2281;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS2279, _M0L1iS939, 0x0p+0f);
      _M0L4gabaS2280 = _M0L1pS931->$13;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS2280, _M0L1iS939, 0x0p+0f);
      _M0L6_2atmpS2281 = _M0L1iS939 + 1;
      _M0L1iS939 = _M0L6_2atmpS2281;
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

float _M0FP26RiantR8snn__mbt21monitor__firing__rate(
  struct _M0TPB5ArrayGfE* _M0L4dataS928
) {
  int32_t _M0L1nS927;
  int32_t _M0L5countS929;
  float _M0L6_2atmpS2247;
  float _M0L6_2atmpS2245;
  float _M0L6_2atmpS2246;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0L1nS927 = _M0MPC15array5Array6lengthGfE(_M0L4dataS928);
  if (_M0L1nS927 == 0) {
    return 0x0p+0f;
  }
  #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0L5countS929
  = _M0FP26RiantR8snn__mbt22monitor__count__spikes(_M0L4dataS928);
  _M0L6_2atmpS2247 = (float)_M0L5countS929;
  _M0L6_2atmpS2245 = _M0L6_2atmpS2247 * 0x1.f4p+12f;
  _M0L6_2atmpS2246 = (float)_M0L1nS927;
  return _M0L6_2atmpS2245 / _M0L6_2atmpS2246;
}

int32_t _M0FP26RiantR8snn__mbt22monitor__count__spikes(
  struct _M0TPB5ArrayGfE* _M0L4dataS924
) {
  struct _M0TPB8MutLocalGiE* _M0L1nS921;
  int32_t _M0L7_2abindS922;
  int32_t _M0L7_2abindS923;
  int32_t _M0L1kS925;
  int32_t _result_2554;
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0L1nS921
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1nS921)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1nS921->$0 = 0;
  _M0L7_2abindS922 = 0;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0L7_2abindS923 = _M0MPC15array5Array6lengthGfE(_M0L4dataS924);
  _M0L1kS925 = _M0L7_2abindS922;
  while (1) {
    if (_M0L1kS925 < _M0L7_2abindS923) {
      float _M0L6_2atmpS2241;
      int32_t _M0L6_2atmpS2244;
      #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
      _M0L6_2atmpS2241 = _M0MPC15array5Array2atGfE(_M0L4dataS924, _M0L1kS925);
      if (_M0L6_2atmpS2241 > 0x0p+0f) {
        int32_t _M0L3valS2243 = _M0L1nS921->$0;
        int32_t _M0L6_2atmpS2242 = _M0L3valS2243 + 1;
        _M0L1nS921->$0 = _M0L6_2atmpS2242;
      }
      _M0L6_2atmpS2244 = _M0L1kS925 + 1;
      _M0L1kS925 = _M0L6_2atmpS2244;
      continue;
    }
    break;
  }
  _result_2554 = _M0L1nS921->$0;
  moonbit_decref_cycle_free(_M0L1nS921);
  return _result_2554;
}

int32_t _M0FP26RiantR8snn__mbt10count__nnz(
  struct _M0TPB5ArrayGfE* _M0L3arrS918
) {
  struct _M0TPB8MutLocalGiE* _M0L1nS915;
  int32_t _M0L7_2abindS916;
  int32_t _M0L7_2abindS917;
  int32_t _M0L1kS919;
  int32_t _result_2556;
  #line 80 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0L1nS915
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1nS915)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1nS915->$0 = 0;
  _M0L7_2abindS916 = 0;
  #line 82 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0L7_2abindS917 = _M0MPC15array5Array6lengthGfE(_M0L3arrS918);
  _M0L1kS919 = _M0L7_2abindS916;
  while (1) {
    if (_M0L1kS919 < _M0L7_2abindS917) {
      float _M0L6_2atmpS2237;
      int32_t _M0L6_2atmpS2240;
      #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
      _M0L6_2atmpS2237 = _M0MPC15array5Array2atGfE(_M0L3arrS918, _M0L1kS919);
      if (_M0L6_2atmpS2237 != 0x0p+0f) {
        int32_t _M0L3valS2239 = _M0L1nS915->$0;
        int32_t _M0L6_2atmpS2238 = _M0L3valS2239 + 1;
        _M0L1nS915->$0 = _M0L6_2atmpS2238;
      }
      _M0L6_2atmpS2240 = _M0L1kS919 + 1;
      _M0L1kS919 = _M0L6_2atmpS2240;
      continue;
    }
    break;
  }
  _result_2556 = _M0L1nS915->$0;
  moonbit_decref_cycle_free(_M0L1nS915);
  return _result_2556;
}

int32_t _M0FP26RiantR8snn__mbt18simulate__step__if(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS913,
  float _M0L2dtS914
) {
  #line 72 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0FP26RiantR8snn__mbt14step__synapses(_M0L3popS913, _M0L2dtS914);
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0FP26RiantR8snn__mbt17synaptic__current(_M0L3popS913);
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0FP26RiantR8snn__mbt12step__neuron(_M0L3popS913, _M0L2dtS914);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS909
) {
  int32_t _M0L1nS908;
  int32_t _M0L7_2abindS910;
  int32_t _M0L1iS911;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS908 = _M0L1pS909->$2;
  _M0L7_2abindS910 = 0;
  _M0L1iS911 = _M0L7_2abindS910;
  while (1) {
    if (_M0L1iS911 < _M0L1nS908) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2214 = _M0L1pS909->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS2235 = _M0L1pS909->$9;
      float _M0L6_2atmpS2230;
      struct _M0TPB5ArrayGfE* _M0L1vS2234;
      float _M0L6_2atmpS2232;
      float _M0L4e__eS2233;
      float _M0L6_2atmpS2231;
      float _M0L6_2atmpS2227;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS2229;
      float _M0L6_2atmpS2228;
      float _M0L6_2atmpS2216;
      struct _M0TPB5ArrayGfE* _M0L2giS2226;
      float _M0L6_2atmpS2221;
      struct _M0TPB5ArrayGfE* _M0L1vS2225;
      float _M0L6_2atmpS2223;
      float _M0L4e__iS2224;
      float _M0L6_2atmpS2222;
      float _M0L6_2atmpS2218;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS2220;
      float _M0L6_2atmpS2219;
      float _M0L6_2atmpS2217;
      float _M0L6_2atmpS2215;
      int32_t _M0L6_2atmpS2236;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2230 = _M0MPC15array5Array2atGfE(_M0L2geS2235, _M0L1iS911);
      _M0L1vS2234 = _M0L1pS909->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2232 = _M0MPC15array5Array2atGfE(_M0L1vS2234, _M0L1iS911);
      _M0L4e__eS2233 = _M0L1pS909->$17;
      _M0L6_2atmpS2231 = _M0L6_2atmpS2232 - _M0L4e__eS2233;
      _M0L6_2atmpS2227 = _M0L6_2atmpS2230 * _M0L6_2atmpS2231;
      _M0L7gsyn__eS2229 = _M0L1pS909->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2228
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS2229, _M0L1iS911);
      _M0L6_2atmpS2216 = _M0L6_2atmpS2227 * _M0L6_2atmpS2228;
      _M0L2giS2226 = _M0L1pS909->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2221 = _M0MPC15array5Array2atGfE(_M0L2giS2226, _M0L1iS911);
      _M0L1vS2225 = _M0L1pS909->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2223 = _M0MPC15array5Array2atGfE(_M0L1vS2225, _M0L1iS911);
      _M0L4e__iS2224 = _M0L1pS909->$18;
      _M0L6_2atmpS2222 = _M0L6_2atmpS2223 - _M0L4e__iS2224;
      _M0L6_2atmpS2218 = _M0L6_2atmpS2221 * _M0L6_2atmpS2222;
      _M0L7gsyn__iS2220 = _M0L1pS909->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2219
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS2220, _M0L1iS911);
      _M0L6_2atmpS2217 = _M0L6_2atmpS2218 * _M0L6_2atmpS2219;
      _M0L6_2atmpS2215 = _M0L6_2atmpS2216 + _M0L6_2atmpS2217;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS2214, _M0L1iS911, _M0L6_2atmpS2215);
      _M0L6_2atmpS2236 = _M0L1iS911 + 1;
      _M0L1iS911 = _M0L6_2atmpS2236;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS900,
  float _M0L2dtS903
) {
  int32_t _M0L1nS899;
  int32_t _M0L7_2abindS901;
  int32_t _M0L1iS902;
  int32_t _M0L7_2abindS905;
  int32_t _M0L1iS906;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS899 = _M0L1pS900->$2;
  _M0L7_2abindS901 = 0;
  _M0L1iS902 = _M0L7_2abindS901;
  while (1) {
    if (_M0L1iS902 < _M0L1nS899) {
      struct _M0TPB5ArrayGfE* _M0L2heS2152 = _M0L1pS900->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS2157 = _M0L1pS900->$11;
      float _M0L6_2atmpS2154;
      struct _M0TPB5ArrayGfE* _M0L3gluS2156;
      float _M0L6_2atmpS2155;
      float _M0L6_2atmpS2153;
      struct _M0TPB5ArrayGfE* _M0L2hiS2158;
      struct _M0TPB5ArrayGfE* _M0L2hiS2163;
      float _M0L6_2atmpS2160;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2162;
      float _M0L6_2atmpS2161;
      float _M0L6_2atmpS2159;
      struct _M0TPB5ArrayGfE* _M0L2geS2164;
      struct _M0TPB5ArrayGfE* _M0L2geS2176;
      float _M0L6_2atmpS2166;
      struct _M0TPB5ArrayGfE* _M0L2geS2175;
      float _M0L6_2atmpS2174;
      float _M0L6_2atmpS2172;
      float _M0L3tdeS2173;
      float _M0L6_2atmpS2169;
      struct _M0TPB5ArrayGfE* _M0L2heS2171;
      float _M0L6_2atmpS2170;
      float _M0L6_2atmpS2168;
      float _M0L6_2atmpS2167;
      float _M0L6_2atmpS2165;
      struct _M0TPB5ArrayGfE* _M0L2heS2177;
      struct _M0TPB5ArrayGfE* _M0L2heS2186;
      float _M0L6_2atmpS2179;
      struct _M0TPB5ArrayGfE* _M0L2heS2185;
      float _M0L6_2atmpS2184;
      float _M0L6_2atmpS2182;
      float _M0L3treS2183;
      float _M0L6_2atmpS2181;
      float _M0L6_2atmpS2180;
      float _M0L6_2atmpS2178;
      struct _M0TPB5ArrayGfE* _M0L2giS2187;
      struct _M0TPB5ArrayGfE* _M0L2giS2199;
      float _M0L6_2atmpS2189;
      struct _M0TPB5ArrayGfE* _M0L2giS2198;
      float _M0L6_2atmpS2197;
      float _M0L6_2atmpS2195;
      float _M0L3tdiS2196;
      float _M0L6_2atmpS2192;
      struct _M0TPB5ArrayGfE* _M0L2hiS2194;
      float _M0L6_2atmpS2193;
      float _M0L6_2atmpS2191;
      float _M0L6_2atmpS2190;
      float _M0L6_2atmpS2188;
      struct _M0TPB5ArrayGfE* _M0L2hiS2200;
      struct _M0TPB5ArrayGfE* _M0L2hiS2209;
      float _M0L6_2atmpS2202;
      struct _M0TPB5ArrayGfE* _M0L2hiS2208;
      float _M0L6_2atmpS2207;
      float _M0L6_2atmpS2205;
      float _M0L3triS2206;
      float _M0L6_2atmpS2204;
      float _M0L6_2atmpS2203;
      float _M0L6_2atmpS2201;
      int32_t _M0L6_2atmpS2210;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2154 = _M0MPC15array5Array2atGfE(_M0L2heS2157, _M0L1iS902);
      _M0L3gluS2156 = _M0L1pS900->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2155 = _M0MPC15array5Array2atGfE(_M0L3gluS2156, _M0L1iS902);
      _M0L6_2atmpS2153 = _M0L6_2atmpS2154 + _M0L6_2atmpS2155;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS2152, _M0L1iS902, _M0L6_2atmpS2153);
      _M0L2hiS2158 = _M0L1pS900->$12;
      _M0L2hiS2163 = _M0L1pS900->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2160 = _M0MPC15array5Array2atGfE(_M0L2hiS2163, _M0L1iS902);
      _M0L4gabaS2162 = _M0L1pS900->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2161
      = _M0MPC15array5Array2atGfE(_M0L4gabaS2162, _M0L1iS902);
      _M0L6_2atmpS2159 = _M0L6_2atmpS2160 + _M0L6_2atmpS2161;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2158, _M0L1iS902, _M0L6_2atmpS2159);
      _M0L2geS2164 = _M0L1pS900->$9;
      _M0L2geS2176 = _M0L1pS900->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2166 = _M0MPC15array5Array2atGfE(_M0L2geS2176, _M0L1iS902);
      _M0L2geS2175 = _M0L1pS900->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2174 = _M0MPC15array5Array2atGfE(_M0L2geS2175, _M0L1iS902);
      _M0L6_2atmpS2172 = -_M0L6_2atmpS2174;
      _M0L3tdeS2173 = _M0L1pS900->$20;
      _M0L6_2atmpS2169 = _M0L6_2atmpS2172 / _M0L3tdeS2173;
      _M0L2heS2171 = _M0L1pS900->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2170 = _M0MPC15array5Array2atGfE(_M0L2heS2171, _M0L1iS902);
      _M0L6_2atmpS2168 = _M0L6_2atmpS2169 + _M0L6_2atmpS2170;
      _M0L6_2atmpS2167 = _M0L2dtS903 * _M0L6_2atmpS2168;
      _M0L6_2atmpS2165 = _M0L6_2atmpS2166 + _M0L6_2atmpS2167;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS2164, _M0L1iS902, _M0L6_2atmpS2165);
      _M0L2heS2177 = _M0L1pS900->$11;
      _M0L2heS2186 = _M0L1pS900->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2179 = _M0MPC15array5Array2atGfE(_M0L2heS2186, _M0L1iS902);
      _M0L2heS2185 = _M0L1pS900->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2184 = _M0MPC15array5Array2atGfE(_M0L2heS2185, _M0L1iS902);
      _M0L6_2atmpS2182 = -_M0L6_2atmpS2184;
      _M0L3treS2183 = _M0L1pS900->$19;
      _M0L6_2atmpS2181 = _M0L6_2atmpS2182 / _M0L3treS2183;
      _M0L6_2atmpS2180 = _M0L2dtS903 * _M0L6_2atmpS2181;
      _M0L6_2atmpS2178 = _M0L6_2atmpS2179 + _M0L6_2atmpS2180;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS2177, _M0L1iS902, _M0L6_2atmpS2178);
      _M0L2giS2187 = _M0L1pS900->$10;
      _M0L2giS2199 = _M0L1pS900->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2189 = _M0MPC15array5Array2atGfE(_M0L2giS2199, _M0L1iS902);
      _M0L2giS2198 = _M0L1pS900->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2197 = _M0MPC15array5Array2atGfE(_M0L2giS2198, _M0L1iS902);
      _M0L6_2atmpS2195 = -_M0L6_2atmpS2197;
      _M0L3tdiS2196 = _M0L1pS900->$22;
      _M0L6_2atmpS2192 = _M0L6_2atmpS2195 / _M0L3tdiS2196;
      _M0L2hiS2194 = _M0L1pS900->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2193 = _M0MPC15array5Array2atGfE(_M0L2hiS2194, _M0L1iS902);
      _M0L6_2atmpS2191 = _M0L6_2atmpS2192 + _M0L6_2atmpS2193;
      _M0L6_2atmpS2190 = _M0L2dtS903 * _M0L6_2atmpS2191;
      _M0L6_2atmpS2188 = _M0L6_2atmpS2189 + _M0L6_2atmpS2190;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS2187, _M0L1iS902, _M0L6_2atmpS2188);
      _M0L2hiS2200 = _M0L1pS900->$12;
      _M0L2hiS2209 = _M0L1pS900->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2202 = _M0MPC15array5Array2atGfE(_M0L2hiS2209, _M0L1iS902);
      _M0L2hiS2208 = _M0L1pS900->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2207 = _M0MPC15array5Array2atGfE(_M0L2hiS2208, _M0L1iS902);
      _M0L6_2atmpS2205 = -_M0L6_2atmpS2207;
      _M0L3triS2206 = _M0L1pS900->$21;
      _M0L6_2atmpS2204 = _M0L6_2atmpS2205 / _M0L3triS2206;
      _M0L6_2atmpS2203 = _M0L2dtS903 * _M0L6_2atmpS2204;
      _M0L6_2atmpS2201 = _M0L6_2atmpS2202 + _M0L6_2atmpS2203;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2200, _M0L1iS902, _M0L6_2atmpS2201);
      _M0L6_2atmpS2210 = _M0L1iS902 + 1;
      _M0L1iS902 = _M0L6_2atmpS2210;
      continue;
    }
    break;
  }
  _M0L7_2abindS905 = 0;
  _M0L1iS906 = _M0L7_2abindS905;
  while (1) {
    if (_M0L1iS906 < _M0L1nS899) {
      struct _M0TPB5ArrayGfE* _M0L3gluS2211 = _M0L1pS900->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2212;
      int32_t _M0L6_2atmpS2213;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS2211, _M0L1iS906, 0x0p+0f);
      _M0L4gabaS2212 = _M0L1pS900->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS2212, _M0L1iS906, 0x0p+0f);
      _M0L6_2atmpS2213 = _M0L1iS906 + 1;
      _M0L1iS906 = _M0L6_2atmpS2213;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS885,
  float _M0L2dtS894
) {
  int32_t _M0L1nS884;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S886;
  float _M0L2tmS887;
  float _M0L2elS888;
  float _M0L1rS889;
  float _M0L2vtS890;
  float _M0L2vrS891;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS2151;
  float _M0L11tabs__constS892;
  float _M0L6_2atmpS2150;
  int32_t _M0L11tabs__stepsS893;
  int32_t _M0L7_2abindS895;
  int32_t _M0L1iS896;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS884 = _M0L1pS885->$2;
  _M0L3p__S886 = _M0L1pS885->$0;
  _M0L2tmS887 = _M0L3p__S886->$2;
  _M0L2elS888 = _M0L3p__S886->$5;
  _M0L1rS889 = _M0L3p__S886->$6;
  _M0L2vtS890 = _M0L3p__S886->$3;
  _M0L2vrS891 = _M0L3p__S886->$4;
  _M0L5spikeS2151 = _M0L1pS885->$1;
  _M0L11tabs__constS892 = _M0L5spikeS2151->$0;
  _M0L6_2atmpS2150 = _M0L11tabs__constS892 / _M0L2dtS894;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS893 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2150);
  _M0L7_2abindS895 = 0;
  _M0L1iS896 = _M0L7_2abindS895;
  while (1) {
    if (_M0L1iS896 < _M0L1nS884) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS2110 = _M0L1pS885->$6;
      int32_t _M0L6_2atmpS2109;
      struct _M0TPB5ArrayGfE* _M0L1vS2116;
      struct _M0TPB5ArrayGfE* _M0L1vS2137;
      float _M0L6_2atmpS2118;
      float _M0L6_2atmpS2120;
      struct _M0TPB5ArrayGfE* _M0L1vS2136;
      float _M0L6_2atmpS2135;
      float _M0L6_2atmpS2134;
      float _M0L6_2atmpS2126;
      struct _M0TPB5ArrayGfE* _M0L1wS2133;
      float _M0L6_2atmpS2132;
      float _M0L6_2atmpS2129;
      struct _M0TPB5ArrayGfE* _M0L1iS2131;
      float _M0L6_2atmpS2130;
      float _M0L6_2atmpS2128;
      float _M0L6_2atmpS2127;
      float _M0L6_2atmpS2122;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2125;
      float _M0L6_2atmpS2124;
      float _M0L6_2atmpS2123;
      float _M0L6_2atmpS2121;
      float _M0L6_2atmpS2119;
      float _M0L6_2atmpS2117;
      struct _M0TPB5ArrayGbE* _M0L4fireS2138;
      struct _M0TPB5ArrayGfE* _M0L1vS2141;
      float _M0L6_2atmpS2140;
      int32_t _M0L6_2atmpS2139;
      struct _M0TPB5ArrayGfE* _M0L1vS2142;
      struct _M0TPB5ArrayGbE* _M0L4fireS2144;
      float _M0L6_2atmpS2143;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2146;
      struct _M0TPB5ArrayGbE* _M0L4fireS2148;
      int32_t _M0L6_2atmpS2147;
      int32_t _M0L6_2atmpS2108;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2109
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2110, _M0L1iS896);
      if (_M0L6_2atmpS2109 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS2111 = _M0L1pS885->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2112;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2115;
        int32_t _M0L6_2atmpS2114;
        int32_t _M0L6_2atmpS2113;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS2111, _M0L1iS896, 0);
        _M0L4tabsS2112 = _M0L1pS885->$6;
        _M0L4tabsS2115 = _M0L1pS885->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2114
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2115, _M0L1iS896);
        _M0L6_2atmpS2113 = _M0L6_2atmpS2114 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS2112, _M0L1iS896, _M0L6_2atmpS2113);
        goto join_897;
      }
      _M0L1vS2116 = _M0L1pS885->$3;
      _M0L1vS2137 = _M0L1pS885->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2118 = _M0MPC15array5Array2atGfE(_M0L1vS2137, _M0L1iS896);
      _M0L6_2atmpS2120 = _M0L2dtS894 / _M0L2tmS887;
      _M0L1vS2136 = _M0L1pS885->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2135 = _M0MPC15array5Array2atGfE(_M0L1vS2136, _M0L1iS896);
      _M0L6_2atmpS2134 = _M0L6_2atmpS2135 - _M0L2elS888;
      _M0L6_2atmpS2126 = -_M0L6_2atmpS2134;
      _M0L1wS2133 = _M0L1pS885->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2132 = _M0MPC15array5Array2atGfE(_M0L1wS2133, _M0L1iS896);
      _M0L6_2atmpS2129 = -_M0L6_2atmpS2132;
      _M0L1iS2131 = _M0L1pS885->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2130 = _M0MPC15array5Array2atGfE(_M0L1iS2131, _M0L1iS896);
      _M0L6_2atmpS2128 = _M0L6_2atmpS2129 + _M0L6_2atmpS2130;
      _M0L6_2atmpS2127 = _M0L1rS889 * _M0L6_2atmpS2128;
      _M0L6_2atmpS2122 = _M0L6_2atmpS2126 + _M0L6_2atmpS2127;
      _M0L9syn__currS2125 = _M0L1pS885->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2124
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS2125, _M0L1iS896);
      _M0L6_2atmpS2123 = _M0L1rS889 * _M0L6_2atmpS2124;
      _M0L6_2atmpS2121 = _M0L6_2atmpS2122 - _M0L6_2atmpS2123;
      _M0L6_2atmpS2119 = _M0L6_2atmpS2120 * _M0L6_2atmpS2121;
      _M0L6_2atmpS2117 = _M0L6_2atmpS2118 + _M0L6_2atmpS2119;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2116, _M0L1iS896, _M0L6_2atmpS2117);
      _M0L4fireS2138 = _M0L1pS885->$5;
      _M0L1vS2141 = _M0L1pS885->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2140 = _M0MPC15array5Array2atGfE(_M0L1vS2141, _M0L1iS896);
      _M0L6_2atmpS2139 = _M0L6_2atmpS2140 > _M0L2vtS890;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2138, _M0L1iS896, _M0L6_2atmpS2139);
      _M0L1vS2142 = _M0L1pS885->$3;
      _M0L4fireS2144 = _M0L1pS885->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2144, _M0L1iS896)) {
        _M0L6_2atmpS2143 = _M0L2vrS891;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS2145 = _M0L1pS885->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2143 = _M0MPC15array5Array2atGfE(_M0L1vS2145, _M0L1iS896);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2142, _M0L1iS896, _M0L6_2atmpS2143);
      _M0L4tabsS2146 = _M0L1pS885->$6;
      _M0L4fireS2148 = _M0L1pS885->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2148, _M0L1iS896)) {
        _M0L6_2atmpS2147 = _M0L11tabs__stepsS893;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS2149 = _M0L1pS885->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2147
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2149, _M0L1iS896);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2146, _M0L1iS896, _M0L6_2atmpS2147);
      goto join_897;
      goto joinlet_2561;
      join_897:;
      _M0L6_2atmpS2108 = _M0L1iS896 + 1;
      _M0L1iS896 = _M0L6_2atmpS2108;
      continue;
      joinlet_2561:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20route__pre__to__post(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS871,
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L4postS873,
  struct _M0TPB5ArrayGfE* _M0L7weightsS879,
  int32_t _M0L3excS880,
  int32_t _M0L3inhS881
) {
  int32_t _M0L6n__preS870;
  int32_t _M0L7n__postS872;
  int32_t _M0L7_2abindS874;
  int32_t _M0L1iS875;
  #line 42 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0L6n__preS870 = _M0L3preS871->$2;
  _M0L7n__postS872 = _M0L4postS873->$2;
  _M0L7_2abindS874 = 0;
  _M0L1iS875 = _M0L7_2abindS874;
  while (1) {
    if (_M0L1iS875 < _M0L6n__preS870) {
      struct _M0TPB5ArrayGbE* _M0L4fireS2095 = _M0L3preS871->$5;
      int32_t _M0L6_2atmpS2107;
      #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2095, _M0L1iS875)) {
        int32_t _M0L7_2abindS876 = 0;
        int32_t _M0L1jS877 = _M0L7_2abindS876;
        while (1) {
          if (_M0L1jS877 < _M0L7n__postS872) {
            int32_t _M0L6_2atmpS2105 = _M0L1iS875 * _M0L7n__postS872;
            int32_t _M0L6_2atmpS2104 = _M0L6_2atmpS2105 + _M0L1jS877;
            float _M0L1wS878;
            int32_t _M0L6_2atmpS2106;
            #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
            _M0L1wS878
            = _M0MPC15array5Array2atGfE(_M0L7weightsS879, _M0L6_2atmpS2104);
            if (_M0L1wS878 != 0x0p+0f) {
              if (_M0L3excS880) {
                struct _M0TPB5ArrayGfE* _M0L3gluS2096 = _M0L4postS873->$12;
                struct _M0TPB5ArrayGfE* _M0L3gluS2099 = _M0L4postS873->$12;
                float _M0L6_2atmpS2098;
                float _M0L6_2atmpS2097;
                #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
                _M0L6_2atmpS2098
                = _M0MPC15array5Array2atGfE(_M0L3gluS2099, _M0L1jS877);
                _M0L6_2atmpS2097 = _M0L6_2atmpS2098 + _M0L1wS878;
                #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
                _M0MPC15array5Array3setGfE(_M0L3gluS2096, _M0L1jS877, _M0L6_2atmpS2097);
              }
              if (_M0L3inhS881) {
                struct _M0TPB5ArrayGfE* _M0L4gabaS2100 = _M0L4postS873->$13;
                struct _M0TPB5ArrayGfE* _M0L4gabaS2103 = _M0L4postS873->$13;
                float _M0L6_2atmpS2102;
                float _M0L6_2atmpS2101;
                #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
                _M0L6_2atmpS2102
                = _M0MPC15array5Array2atGfE(_M0L4gabaS2103, _M0L1jS877);
                _M0L6_2atmpS2101 = _M0L6_2atmpS2102 + _M0L1wS878;
                #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
                _M0MPC15array5Array3setGfE(_M0L4gabaS2100, _M0L1jS877, _M0L6_2atmpS2101);
              }
            }
            _M0L6_2atmpS2106 = _M0L1jS877 + 1;
            _M0L1jS877 = _M0L6_2atmpS2106;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS2107 = _M0L1iS875 + 1;
      _M0L1iS875 = _M0L6_2atmpS2107;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TPB5ArrayGfE* _M0FP26RiantR8snn__mbt21make__random__weights(
  int32_t _M0L6n__preS861,
  int32_t _M0L7n__postS862,
  float _M0L2muS868,
  float _M0L1pS867,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS866
) {
  int32_t _M0L5totalS860;
  struct _M0TPB5ArrayGfE* _M0L3arrS863;
  int32_t _M0L7_2abindS864;
  int32_t _M0L1kS865;
  #line 20 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0L5totalS860 = _M0L6n__preS861 * _M0L7n__postS862;
  #line 28 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
  _M0L3arrS863 = _M0MPC15array5Array4makeGfE(_M0L5totalS860, 0x0p+0f);
  _M0L7_2abindS864 = 0;
  _M0L1kS865 = _M0L7_2abindS864;
  while (1) {
    if (_M0L1kS865 < _M0L5totalS860) {
      float _M0L6_2atmpS2093;
      int32_t _M0L6_2atmpS2094;
      #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
      _M0L6_2atmpS2093 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS866);
      if (_M0L6_2atmpS2093 < _M0L1pS867) {
        #line 31 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sinexp_helpers.mbt"
        _M0MPC15array5Array3setGfE(_M0L3arrS863, _M0L1kS865, _M0L2muS868);
      }
      _M0L6_2atmpS2094 = _M0L1kS865 + 1;
      _M0L1kS865 = _M0L6_2atmpS2094;
      continue;
    }
    break;
  }
  return _M0L3arrS863;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS858
) {
  struct _M0TUmmmmE* _M0L1sS857;
  uint64_t _M0L6_2atmpS2092;
  struct _M0TUmmmmE* _M0L1tS859;
  uint64_t _M0L6_2atmpS2088;
  uint64_t _M0L6_2atmpS2089;
  uint64_t _M0L6_2atmpS2090;
  uint64_t _M0L6_2atmpS2091;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2565;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS857 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS858);
  _M0L6_2atmpS2092 = _M0L1sS857->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS859 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2092);
  _M0L6_2atmpS2088 = _M0L1sS857->$0;
  _M0L6_2atmpS2089 = _M0L1sS857->$1;
  _M0L6_2atmpS2090 = _M0L1sS857->$2;
  moonbit_decref_cycle_free(_M0L1sS857);
  _M0L6_2atmpS2091 = _M0L1tS859->$0;
  moonbit_decref_cycle_free(_M0L1tS859);
  _block_2565
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2565)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2565->$0 = _M0L6_2atmpS2088;
  _block_2565->$1 = _M0L6_2atmpS2089;
  _block_2565->$2 = _M0L6_2atmpS2090;
  _block_2565->$3 = _M0L6_2atmpS2091;
  return _block_2565;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS849) {
  uint64_t _M0L2s1S848;
  uint64_t _M0L2z1S850;
  uint64_t _M0L2s2S851;
  uint64_t _M0L2z2S852;
  uint64_t _M0L2s3S853;
  uint64_t _M0L2z3S854;
  uint64_t _M0L2s4S855;
  uint64_t _M0L2z4S856;
  struct _M0TUmmmmE* _block_2566;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S848 = _M0L4seedS849 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S850 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S848);
  _M0L2s2S851 = _M0L2s1S848 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S852 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S851);
  _M0L2s3S853 = _M0L2s2S851 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S854 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S853);
  _M0L2s4S855 = _M0L2s3S853 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S856 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S855);
  _block_2566 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2566)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2566->$0 = _M0L2z1S850;
  _block_2566->$1 = _M0L2z2S852;
  _block_2566->$2 = _M0L2z3S854;
  _block_2566->$3 = _M0L2z4S856;
  return _block_2566;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS846) {
  uint64_t _M0L6_2atmpS2087;
  uint64_t _M0L6_2atmpS2086;
  uint64_t _M0L1zS845;
  uint64_t _M0L6_2atmpS2085;
  uint64_t _M0L6_2atmpS2084;
  uint64_t _M0L1zS847;
  uint64_t _M0L6_2atmpS2083;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2087 = _M0L1zS846 >> 30;
  _M0L6_2atmpS2086 = _M0L1zS846 ^ _M0L6_2atmpS2087;
  _M0L1zS845 = _M0L6_2atmpS2086 * 13787848793156543929ull;
  _M0L6_2atmpS2085 = _M0L1zS845 >> 27;
  _M0L6_2atmpS2084 = _M0L1zS845 ^ _M0L6_2atmpS2085;
  _M0L1zS847 = _M0L6_2atmpS2084 * 10723151780598845931ull;
  _M0L6_2atmpS2083 = _M0L1zS847 >> 31;
  return _M0L1zS847 ^ _M0L6_2atmpS2083;
}

int32_t _M0FP26RiantR8snn__mbt13stimulate__if(
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L1sS834,
  float _M0L4timeS844,
  float _M0L2dtS836
) {
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS2070;
  struct _M0TPB5ArrayGbE* _M0L6activeS2069;
  int32_t _M0L6_2atmpS2068;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS2082;
  float _M0L4rateS2081;
  float _M0L6lambdaS835;
  struct _M0TPB5ArrayGiE* _M0L7_2abindS837;
  int32_t _M0L7_2abindS838;
  int32_t* _M0L7_2abindS839;
  int32_t _M0L2__S840;
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L5paramS2070 = _M0L1sS834->$0;
  _M0L6activeS2069 = _M0L5paramS2070->$2;
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS2068 = _M0MPC15array5Array2atGbE(_M0L6activeS2069, 0);
  if (!_M0L6_2atmpS2068) {
    return 0;
  }
  _M0L5paramS2082 = _M0L1sS834->$0;
  _M0L4rateS2081 = _M0L5paramS2082->$0;
  _M0L6lambdaS835 = _M0L4rateS2081 * _M0L2dtS836;
  if (_M0L6lambdaS835 <= 0x0p+0f) {
    return 0;
  }
  _M0L7_2abindS837 = _M0L1sS834->$1;
  _M0L7_2abindS838 = _M0L7_2abindS837->$1;
  _M0L7_2abindS839 = _M0L7_2abindS837->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS839);
  _M0L2__S840 = 0;
  while (1) {
    if (_M0L2__S840 < _M0L7_2abindS838) {
      int32_t _M0L1nS841 = (int32_t)_M0L7_2abindS839[_M0L2__S840];
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS2079 = _M0L1sS834->$3;
      int32_t _M0L1kS842;
      int32_t _M0L6_2atmpS2080;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
      _M0L1kS842
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS2079, _M0L6lambdaS835);
      if (_M0L1kS842 > 0) {
        struct _M0TPB5ArrayGfE* _M0L1gS2071 = _M0L1sS834->$2;
        struct _M0TPB5ArrayGfE* _M0L1gS2078 = _M0L1sS834->$2;
        float _M0L6_2atmpS2073;
        struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS2077;
        float _M0L2muS2075;
        float _M0L6_2atmpS2076;
        float _M0L6_2atmpS2074;
        float _M0L6_2atmpS2072;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0L6_2atmpS2073 = _M0MPC15array5Array2atGfE(_M0L1gS2078, _M0L1nS841);
        _M0L5paramS2077 = _M0L1sS834->$0;
        _M0L2muS2075 = _M0L5paramS2077->$1;
        _M0L6_2atmpS2076 = (float)_M0L1kS842;
        _M0L6_2atmpS2074 = _M0L2muS2075 * _M0L6_2atmpS2076;
        _M0L6_2atmpS2072 = _M0L6_2atmpS2073 + _M0L6_2atmpS2074;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0MPC15array5Array3setGfE(_M0L1gS2071, _M0L1nS841, _M0L6_2atmpS2072);
      }
      _M0L6_2atmpS2080 = _M0L2__S840 + 1;
      _M0L2__S840 = _M0L6_2atmpS2080;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS839);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0MP26RiantR8snn__mbt17PoissonStimulusIF3new(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS825,
  moonbit_string_t _M0L3symS831,
  float _M0L4rateS832,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS833
) {
  int32_t _M0L1nS824;
  int32_t* _M0L6_2atmpS2067;
  struct _M0TPB5ArrayGiE* _M0L7neuronsS826;
  int32_t _M0L7_2abindS827;
  int32_t _M0L1kS828;
  struct _M0TPB5ArrayGfE* _M0L1gS830;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L6_2atmpS2066;
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _block_2569;
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L1nS824 = _M0L3popS825->$2;
  _M0L6_2atmpS2067 = (int32_t*)moonbit_empty_int32_array;
  _M0L7neuronsS826
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L7neuronsS826)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 62, 0);
  _M0L7neuronsS826->$0 = _M0L6_2atmpS2067;
  _M0L7neuronsS826->$1 = 0;
  _M0L7_2abindS827 = 0;
  _M0L1kS828 = _M0L7_2abindS827;
  while (1) {
    if (_M0L1kS828 < _M0L1nS824) {
      int32_t _M0L6_2atmpS2065;
      #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
      _M0MPC15array5Array4pushGiE(_M0L7neuronsS826, _M0L1kS828);
      _M0L6_2atmpS2065 = _M0L1kS828 + 1;
      _M0L1kS828 = _M0L6_2atmpS2065;
      continue;
    }
    break;
  }
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  if (
    _M0L3symS831 == (moonbit_string_t)moonbit_string_literal_12.data
    || Moonbit_array_length(_M0L3symS831)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_12.data)
       && 0
          == memcmp(_M0L3symS831, (moonbit_string_t)moonbit_string_literal_12.data, Moonbit_array_length(_M0L3symS831) * 2)
  ) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2486 = _M0L3popS825->$13;
    moonbit_incref_cycle_free(_M0L8_2afieldS2486);
    _M0L1gS830 = _M0L8_2afieldS2486;
  } else {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2487 = _M0L3popS825->$14;
    moonbit_incref_cycle_free(_M0L8_2afieldS2487);
    _M0L1gS830 = _M0L8_2afieldS2487;
  }
  #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS2066 = _M0MP26RiantR8snn__mbt12PoissonFixed3new(_M0L4rateS832);
  moonbit_incref_cycle_free(_M0L3rngS833);
  _block_2569
  = (struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF));
  Moonbit_object_header(_block_2569)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 65, 0);
  _block_2569->$0 = _M0L6_2atmpS2066;
  _block_2569->$1 = _M0L7neuronsS826;
  _block_2569->$2 = _M0L1gS830;
  _block_2569->$3 = _M0L3rngS833;
  return _block_2569;
}

struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0MP26RiantR8snn__mbt12PoissonFixed3new(
  float _M0L4rateS823
) {
  uint8_t* _M0L6_2atmpS2064;
  struct _M0TPB5ArrayGbE* _M0L6_2atmpS2063;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _block_2570;
  #line 23 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS2064 = (uint8_t*)moonbit_make_bytes_raw(1);
  _M0L6_2atmpS2064[0] = 1;
  _M0L6_2atmpS2063
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_M0L6_2atmpS2063)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 71, 0);
  _M0L6_2atmpS2063->$0 = _M0L6_2atmpS2064;
  _M0L6_2atmpS2063->$1 = 1;
  _block_2570
  = (struct _M0TP26RiantR8snn__mbt12PoissonFixed*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt12PoissonFixed));
  Moonbit_object_header(_block_2570)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 74, 0);
  _block_2570->$0 = _M0L4rateS823;
  _block_2570->$1 = 0x1p+0f;
  _block_2570->$2 = _M0L6_2atmpS2063;
  return _block_2570;
}

int32_t _M0FP26RiantR8snn__mbt15sample__poisson(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS821,
  float _M0L6lambdaS815
) {
  float _M0L6_2atmpS2062;
  float _M0L6_2atmpS2061;
  double _M0L1lS816;
  struct _M0TPB8MutLocalGdE* _M0L1pS817;
  struct _M0TPB8MutLocalGiE* _M0L1kS818;
  float _M0L6_2atmpS2060;
  int32_t _M0L8ten__lamS820;
  int32_t _M0L3capS819;
  int32_t _M0L3valS2059;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  if (_M0L6lambdaS815 <= 0x0p+0f) {
    return 0;
  }
  _M0L6_2atmpS2062 = -_M0L6lambdaS815;
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS2061 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2062);
  _M0L1lS816 = (double)_M0L6_2atmpS2061;
  _M0L1pS817
  = (struct _M0TPB8MutLocalGdE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGdE));
  Moonbit_object_header(_M0L1pS817)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1pS817->$0 = 0x1p+0;
  _M0L1kS818
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS818)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS818->$0 = 0;
  _M0L6_2atmpS2060 = _M0L6lambdaS815 * 0x1.4p+3f;
  #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L8ten__lamS820 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2060);
  if (_M0L8ten__lamS820 > 100) {
    _M0L3capS819 = _M0L8ten__lamS820;
  } else {
    _M0L3capS819 = 100;
  }
  while (1) {
    int32_t _M0L3valS2051 = _M0L1kS818->$0;
    int32_t _M0L6_2atmpS2050 = _M0L3valS2051 + 1;
    double _M0L3valS2053;
    double _M0L6_2atmpS2054;
    double _M0L6_2atmpS2052;
    double _M0L3valS2055;
    int32_t _M0L3valS2057;
    _M0L1kS818->$0 = _M0L6_2atmpS2050;
    _M0L3valS2053 = _M0L1pS817->$0;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
    _M0L6_2atmpS2054 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS821);
    _M0L6_2atmpS2052 = _M0L3valS2053 * _M0L6_2atmpS2054;
    _M0L1pS817->$0 = _M0L6_2atmpS2052;
    _M0L3valS2055 = _M0L1pS817->$0;
    if (_M0L3valS2055 < _M0L1lS816) {
      int32_t _M0L3valS2056;
      moonbit_decref_cycle_free(_M0L1pS817);
      _M0L3valS2056 = _M0L1kS818->$0;
      moonbit_decref_cycle_free(_M0L1kS818);
      return _M0L3valS2056 - 1;
    }
    _M0L3valS2057 = _M0L1kS818->$0;
    if (_M0L3valS2057 > _M0L3capS819) {
      int32_t _M0L3valS2058;
      moonbit_decref_cycle_free(_M0L1pS817);
      _M0L3valS2058 = _M0L1kS818->$0;
      moonbit_decref_cycle_free(_M0L1kS818);
      return _M0L3valS2058 - 1;
    }
    continue;
    break;
  }
  _M0L3valS2059 = _M0L1kS818->$0;
  moonbit_decref_cycle_free(_M0L1kS818);
  return _M0L3valS2059 - 1;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS813
) {
  uint64_t _M0L1uS812;
  uint64_t _M0L4bitsS814;
  double _M0L6_2atmpS2049;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS812 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS813);
  _M0L4bitsS814 = _M0L1uS812 >> 11;
  _M0L6_2atmpS2049 = (double)_M0L4bitsS814;
  return _M0L6_2atmpS2049 * 0x1p-53;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS810
) {
  uint32_t _M0L1uS809;
  uint32_t _M0L4bitsS811;
  double _M0L6_2atmpS2048;
  double _M0L6_2atmpS2047;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS809 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS810);
  _M0L4bitsS811 = _M0L1uS809 >> 8;
  _M0L6_2atmpS2048 = (double)_M0L4bitsS811;
  _M0L6_2atmpS2047 = _M0L6_2atmpS2048 * 0x1p-24;
  return (float)_M0L6_2atmpS2047;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS808
) {
  uint64_t _M0L1uS807;
  uint64_t _M0L6_2atmpS2046;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS807 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS808);
  _M0L6_2atmpS2046 = _M0L1uS807 >> 32;
  return (uint32_t)_M0L6_2atmpS2046;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS800
) {
  uint64_t _M0L2s0S799;
  uint64_t _M0L2s1S801;
  uint64_t _M0L2s2S802;
  uint64_t _M0L2s3S803;
  uint64_t _M0L3tmpS804;
  uint64_t _M0L6_2atmpS2045;
  uint64_t _M0L3resS805;
  uint64_t _M0L1tS806;
  uint64_t _M0L6_2atmpS2035;
  uint64_t _M0L6_2atmpS2036;
  uint64_t _M0L2s2S2038;
  uint64_t _M0L6_2atmpS2037;
  uint64_t _M0L2s3S2040;
  uint64_t _M0L6_2atmpS2039;
  uint64_t _M0L2s2S2042;
  uint64_t _M0L6_2atmpS2041;
  uint64_t _M0L2s3S2044;
  uint64_t _M0L6_2atmpS2043;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S799 = _M0L1rS800->$0;
  _M0L2s1S801 = _M0L1rS800->$1;
  _M0L2s2S802 = _M0L1rS800->$2;
  _M0L2s3S803 = _M0L1rS800->$3;
  _M0L3tmpS804 = _M0L2s0S799 + _M0L2s3S803;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2045 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS804, 23);
  _M0L3resS805 = _M0L6_2atmpS2045 + _M0L2s0S799;
  _M0L1tS806 = _M0L2s1S801 << 17;
  _M0L6_2atmpS2035 = _M0L2s2S802 ^ _M0L2s0S799;
  _M0L1rS800->$2 = _M0L6_2atmpS2035;
  _M0L6_2atmpS2036 = _M0L2s3S803 ^ _M0L2s1S801;
  _M0L1rS800->$3 = _M0L6_2atmpS2036;
  _M0L2s2S2038 = _M0L1rS800->$2;
  _M0L6_2atmpS2037 = _M0L2s1S801 ^ _M0L2s2S2038;
  _M0L1rS800->$1 = _M0L6_2atmpS2037;
  _M0L2s3S2040 = _M0L1rS800->$3;
  _M0L6_2atmpS2039 = _M0L2s0S799 ^ _M0L2s3S2040;
  _M0L1rS800->$0 = _M0L6_2atmpS2039;
  _M0L2s2S2042 = _M0L1rS800->$2;
  _M0L6_2atmpS2041 = _M0L2s2S2042 ^ _M0L1tS806;
  _M0L1rS800->$2 = _M0L6_2atmpS2041;
  _M0L2s3S2044 = _M0L1rS800->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2043 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S2044, 45);
  _M0L1rS800->$3 = _M0L6_2atmpS2043;
  return _M0L3resS805;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS797, int32_t _M0L1kS798) {
  uint64_t _M0L6_2atmpS2032;
  int32_t _M0L6_2atmpS2034;
  uint64_t _M0L6_2atmpS2033;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2032 = _M0L1xS797 << (_M0L1kS798 & 63);
  _M0L6_2atmpS2034 = 64 - _M0L1kS798;
  _M0L6_2atmpS2033 = _M0L1xS797 >> (_M0L6_2atmpS2034 & 63);
  return _M0L6_2atmpS2032 | _M0L6_2atmpS2033;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS796) {
  double _M0L6_2atmpS2031;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2031 = (double)_M0L4selfS796;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2031);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS795) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS795 != _M0L4selfS795) {
    return 0;
  } else if (_M0L4selfS795 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS795 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS795;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS781,
  float _M0L4elemS783
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS780;
  int32_t _M0L1iS782;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS780 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS781);
  _M0L1iS782 = 0;
  while (1) {
    if (_M0L1iS782 < _M0L3lenS781) {
      float* _M0L3bufS2025 = _M0L3arrS780->$0;
      int32_t _M0L6_2atmpS2026;
      _M0L3bufS2025[_M0L1iS782] = _M0L4elemS783;
      _M0L6_2atmpS2026 = _M0L1iS782 + 1;
      _M0L1iS782 = _M0L6_2atmpS2026;
      continue;
    }
    break;
  }
  return _M0L3arrS780;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS786,
  int32_t _M0L4elemS788
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS785;
  int32_t _M0L1iS787;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS785 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS786);
  _M0L1iS787 = 0;
  while (1) {
    if (_M0L1iS787 < _M0L3lenS786) {
      uint8_t* _M0L3bufS2027 = _M0L3arrS785->$0;
      int32_t _M0L6_2atmpS2028;
      _M0L3bufS2027[_M0L1iS787] = _M0L4elemS788;
      _M0L6_2atmpS2028 = _M0L1iS787 + 1;
      _M0L1iS787 = _M0L6_2atmpS2028;
      continue;
    }
    break;
  }
  return _M0L3arrS785;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS791,
  int32_t _M0L4elemS793
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS790;
  int32_t _M0L1iS792;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS790 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS791);
  _M0L1iS792 = 0;
  while (1) {
    if (_M0L1iS792 < _M0L3lenS791) {
      int32_t* _M0L3bufS2029 = _M0L3arrS790->$0;
      int32_t _M0L6_2atmpS2030;
      _M0L3bufS2029[_M0L1iS792] = _M0L4elemS793;
      _M0L6_2atmpS2030 = _M0L1iS792 + 1;
      _M0L1iS792 = _M0L6_2atmpS2030;
      continue;
    }
    break;
  }
  return _M0L3arrS790;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS769,
  int32_t _M0L5indexS770,
  float _M0L5valueS771
) {
  int32_t _M0L3lenS768;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS768 = _M0L4selfS769->$1;
  if (_M0L5indexS770 >= 0 && _M0L5indexS770 < _M0L3lenS768) {
    float* _M0L6_2atmpS2022;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2022 = _M0MPC15array5Array6bufferGfE(_M0L4selfS769);
    _M0L6_2atmpS2022[_M0L5indexS770] = _M0L5valueS771;
    moonbit_decref_cycle_free(_M0L6_2atmpS2022);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS773,
  int32_t _M0L5indexS774,
  int32_t _M0L5valueS775
) {
  int32_t _M0L3lenS772;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS772 = _M0L4selfS773->$1;
  if (_M0L5indexS774 >= 0 && _M0L5indexS774 < _M0L3lenS772) {
    uint8_t* _M0L6_2atmpS2023;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2023 = _M0MPC15array5Array6bufferGbE(_M0L4selfS773);
    _M0L6_2atmpS2023[_M0L5indexS774] = _M0L5valueS775;
    moonbit_decref_cycle_free(_M0L6_2atmpS2023);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS777,
  int32_t _M0L5indexS778,
  int32_t _M0L5valueS779
) {
  int32_t _M0L3lenS776;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS776 = _M0L4selfS777->$1;
  if (_M0L5indexS778 >= 0 && _M0L5indexS778 < _M0L3lenS776) {
    int32_t* _M0L6_2atmpS2024;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2024 = _M0MPC15array5Array6bufferGiE(_M0L4selfS777);
    _M0L6_2atmpS2024[_M0L5indexS778] = _M0L5valueS779;
    moonbit_decref_cycle_free(_M0L6_2atmpS2024);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS757,
  int32_t _M0L5indexS758
) {
  int32_t _M0L3lenS756;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS756 = _M0L4selfS757->$1;
  if (_M0L5indexS758 >= 0 && _M0L5indexS758 < _M0L3lenS756) {
    uint8_t* _M0L6_2atmpS2018;
    int32_t _result_2575;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2018 = _M0MPC15array5Array6bufferGbE(_M0L4selfS757);
    _result_2575 = (int32_t)_M0L6_2atmpS2018[_M0L5indexS758];
    moonbit_decref_cycle_free(_M0L6_2atmpS2018);
    return _result_2575;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS760,
  int32_t _M0L5indexS761
) {
  int32_t _M0L3lenS759;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS759 = _M0L4selfS760->$1;
  if (_M0L5indexS761 >= 0 && _M0L5indexS761 < _M0L3lenS759) {
    float* _M0L6_2atmpS2019;
    float _result_2576;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2019 = _M0MPC15array5Array6bufferGfE(_M0L4selfS760);
    _result_2576 = (float)_M0L6_2atmpS2019[_M0L5indexS761];
    moonbit_decref_cycle_free(_M0L6_2atmpS2019);
    return _result_2576;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS763,
  int32_t _M0L5indexS764
) {
  int32_t _M0L3lenS762;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS762 = _M0L4selfS763->$1;
  if (_M0L5indexS764 >= 0 && _M0L5indexS764 < _M0L3lenS762) {
    int32_t* _M0L6_2atmpS2020;
    int32_t _result_2577;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2020 = _M0MPC15array5Array6bufferGiE(_M0L4selfS763);
    _result_2577 = (int32_t)_M0L6_2atmpS2020[_M0L5indexS764];
    moonbit_decref_cycle_free(_M0L6_2atmpS2020);
    return _result_2577;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS766,
  int32_t _M0L5indexS767
) {
  int32_t _M0L3lenS765;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS765 = _M0L4selfS766->$1;
  if (_M0L5indexS767 >= 0 && _M0L5indexS767 < _M0L3lenS765) {
    moonbit_string_t* _M0L6_2atmpS2021;
    moonbit_string_t _M0L6_2atmpS2488;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2021 = _M0MPC15array5Array6bufferGsE(_M0L4selfS766);
    _M0L6_2atmpS2488 = (moonbit_string_t)_M0L6_2atmpS2021[_M0L5indexS767];
    moonbit_incref_cycle_free(_M0L6_2atmpS2488);
    moonbit_decref_cycle_free(_M0L6_2atmpS2021);
    return _M0L6_2atmpS2488;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS755) {
  moonbit_string_t _M0L6_2atmpS2017;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2017 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS755);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2017);
  moonbit_decref_cycle_free(_M0L6_2atmpS2017);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS754) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS754);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS739) {
  uint64_t _M0L4bitsS742;
  uint64_t _M0L6_2atmpS2016;
  uint64_t _M0L6_2atmpS2015;
  int32_t _M0L8ieeeSignS743;
  uint64_t _M0L12ieeeMantissaS744;
  uint64_t _M0L6_2atmpS2014;
  uint64_t _M0L6_2atmpS2013;
  int32_t _M0L12ieeeExponentS745;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS746;
  struct _M0TPB17FloatingDecimal64* _M0L1vS747;
  moonbit_string_t _result_2579;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS739 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_13.data;
  }
  if (_M0L3valS739 >= -0x1p+53 && _M0L3valS739 <= 0x1p+53) {
    if (_M0L3valS739 >= -0x1p+31 && _M0L3valS739 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS740;
      double _M0L6_2atmpS2002;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS740 = _M0MPC16double6Double7to__int(_M0L3valS739);
      _M0L6_2atmpS2002 = (double)_M0L1iS740;
      if (_M0L6_2atmpS2002 == _M0L3valS739) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS740, 10);
      }
    } else {
      int64_t _M0L1iS741;
      double _M0L6_2atmpS2003;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS741 = _M0MPC16double6Double9to__int64(_M0L3valS739);
      _M0L6_2atmpS2003 = (double)_M0L1iS741;
      if (_M0L6_2atmpS2003 == _M0L3valS739) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS741, 10);
      }
    }
  }
  _M0L4bitsS742 = *(int64_t*)&_M0L3valS739;
  _M0L6_2atmpS2016 = _M0L4bitsS742 >> 63;
  _M0L6_2atmpS2015 = _M0L6_2atmpS2016 & 1ull;
  _M0L8ieeeSignS743 = _M0L6_2atmpS2015 != 0ull;
  _M0L12ieeeMantissaS744 = _M0L4bitsS742 & 4503599627370495ull;
  _M0L6_2atmpS2014 = _M0L4bitsS742 >> 52;
  _M0L6_2atmpS2013 = _M0L6_2atmpS2014 & 2047ull;
  _M0L12ieeeExponentS745 = (int32_t)_M0L6_2atmpS2013;
  if (
    _M0L12ieeeExponentS745 == 2047
    || _M0L12ieeeExponentS745 == 0 && _M0L12ieeeMantissaS744 == 0ull
  ) {
    int32_t _M0L6_2atmpS2004 = _M0L12ieeeExponentS745 != 0;
    int32_t _M0L6_2atmpS2005 = _M0L12ieeeMantissaS744 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS743, _M0L6_2atmpS2004, _M0L6_2atmpS2005);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS746
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS744, _M0L12ieeeExponentS745);
  if (_M0L7_2abindS746 == 0) {
    uint32_t _M0L6_2atmpS2006;
    if (_M0L7_2abindS746) {
      moonbit_decref_cycle_free(_M0L7_2abindS746);
    }
    _M0L6_2atmpS2006 = *(uint32_t*)&_M0L12ieeeExponentS745;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS747 = _M0FPB3d2d(_M0L12ieeeMantissaS744, _M0L6_2atmpS2006);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS748 = _M0L7_2abindS746;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS749 = _M0L7_2aSomeS748;
    struct _M0TPB17FloatingDecimal64* _M0L1xS750 = _M0L4_2afS749;
    while (1) {
      uint64_t _M0L8mantissaS2012 = _M0L1xS750->$0;
      uint64_t _M0L1qS751 = _M0L8mantissaS2012 / 10ull;
      uint64_t _M0L8mantissaS2010 = _M0L1xS750->$0;
      uint64_t _M0L6_2atmpS2011 = 10ull * _M0L1qS751;
      uint64_t _M0L1rS752 = _M0L8mantissaS2010 - _M0L6_2atmpS2011;
      int32_t _M0L8exponentS2009;
      int32_t _M0L6_2atmpS2008;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2007;
      if (_M0L1rS752 != 0ull) {
        _M0L1vS747 = _M0L1xS750;
        break;
      }
      _M0L8exponentS2009 = _M0L1xS750->$1;
      moonbit_decref_cycle_free(_M0L1xS750);
      _M0L6_2atmpS2008 = _M0L8exponentS2009 + 1;
      _M0L6_2atmpS2007
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS2007)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS2007->$0 = _M0L1qS751;
      _M0L6_2atmpS2007->$1 = _M0L6_2atmpS2008;
      _M0L1xS750 = _M0L6_2atmpS2007;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2579 = _M0FPB9to__chars(_M0L1vS747, _M0L8ieeeSignS743);
  moonbit_decref_cycle_free(_M0L1vS747);
  return _result_2579;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS734,
  int32_t _M0L12ieeeExponentS736
) {
  uint64_t _M0L2m2S733;
  int32_t _M0L6_2atmpS2001;
  int32_t _M0L2e2S735;
  int32_t _M0L6_2atmpS2000;
  uint64_t _M0L6_2atmpS1999;
  uint64_t _M0L4maskS737;
  uint64_t _M0L8fractionS738;
  int32_t _M0L6_2atmpS1998;
  uint64_t _M0L6_2atmpS1997;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1996;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S733 = 4503599627370496ull | _M0L12ieeeMantissaS734;
  _M0L6_2atmpS2001 = _M0L12ieeeExponentS736 - 1023;
  _M0L2e2S735 = _M0L6_2atmpS2001 - 52;
  if (_M0L2e2S735 > 0) {
    return 0;
  }
  if (_M0L2e2S735 < -52) {
    return 0;
  }
  _M0L6_2atmpS2000 = -_M0L2e2S735;
  _M0L6_2atmpS1999 = 1ull << (_M0L6_2atmpS2000 & 63);
  _M0L4maskS737 = _M0L6_2atmpS1999 - 1ull;
  _M0L8fractionS738 = _M0L2m2S733 & _M0L4maskS737;
  if (_M0L8fractionS738 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1998 = -_M0L2e2S735;
  _M0L6_2atmpS1997 = _M0L2m2S733 >> (_M0L6_2atmpS1998 & 63);
  _M0L6_2atmpS1996
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1996)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1996->$0 = _M0L6_2atmpS1997;
  _M0L6_2atmpS1996->$1 = 0;
  return _M0L6_2atmpS1996;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS701,
  int32_t _M0L4signS699
) {
  moonbit_bytes_t _M0L6resultS697;
  int32_t _M0Lm5indexS698;
  uint64_t _M0L6outputS700;
  int32_t _M0L7olengthS702;
  int32_t _M0L8exponentS1995;
  int32_t _M0L6_2atmpS1994;
  int32_t _M0Lm3expS703;
  int32_t _M0L6_2atmpS1993;
  int32_t _M0L6_2atmpS1991;
  int32_t _M0L18scientificNotationS704;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS697 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS698 = 0;
  if (_M0L4signS699) {
    int32_t _M0L6_2atmpS1865 = _M0Lm5indexS698;
    int32_t _M0L6_2atmpS1866;
    if (
      _M0L6_2atmpS1865 < 0
      || _M0L6_2atmpS1865 >= Moonbit_array_length(_M0L6resultS697)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS697[_M0L6_2atmpS1865] = 45;
    _M0L6_2atmpS1866 = _M0Lm5indexS698;
    _M0Lm5indexS698 = _M0L6_2atmpS1866 + 1;
  }
  _M0L6outputS700 = _M0L1vS701->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS702 = _M0FPB17decimal__length17(_M0L6outputS700);
  _M0L8exponentS1995 = _M0L1vS701->$1;
  _M0L6_2atmpS1994 = _M0L8exponentS1995 + _M0L7olengthS702;
  _M0Lm3expS703 = _M0L6_2atmpS1994 - 1;
  _M0L6_2atmpS1993 = _M0Lm3expS703;
  if (_M0L6_2atmpS1993 >= -6) {
    int32_t _M0L6_2atmpS1992 = _M0Lm3expS703;
    _M0L6_2atmpS1991 = _M0L6_2atmpS1992 < 21;
  } else {
    _M0L6_2atmpS1991 = 0;
  }
  _M0L18scientificNotationS704 = !_M0L6_2atmpS1991;
  if (_M0L18scientificNotationS704) {
    int32_t _M0L7_2abindS705 = _M0L7olengthS702 - 1;
    uint64_t _M0L6outputS706;
    int32_t _M0L1iS707 = 0;
    uint64_t _M0L6outputS708 = _M0L6outputS700;
    int32_t _M0L6_2atmpS1867;
    int32_t _M0L6_2atmpS1871;
    int32_t _M0L6_2atmpS1870;
    int32_t _M0L6_2atmpS1869;
    int32_t _M0L6_2atmpS1868;
    int32_t _M0L6_2atmpS1875;
    int32_t _M0L6_2atmpS1876;
    int32_t _M0L6_2atmpS1877;
    int32_t _M0L6_2atmpS1878;
    int32_t _M0L6_2atmpS1879;
    int32_t _M0L6_2atmpS1885;
    int32_t _M0L6_2atmpS1918;
    moonbit_string_t _result_2581;
    while (1) {
      if (_M0L1iS707 < _M0L7_2abindS705) {
        uint64_t _M0L1cS709 = _M0L6outputS708 % 10ull;
        int32_t _M0L6_2atmpS1924 = _M0Lm5indexS698;
        int32_t _M0L6_2atmpS1923 = _M0L6_2atmpS1924 + _M0L7olengthS702;
        int32_t _M0L6_2atmpS1919 = _M0L6_2atmpS1923 - _M0L1iS707;
        int32_t _M0L6_2atmpS1922 = (int32_t)_M0L1cS709;
        int32_t _M0L6_2atmpS1921 = 48 + _M0L6_2atmpS1922;
        int32_t _M0L6_2atmpS1920 = _M0L6_2atmpS1921 & 0xff;
        int32_t _M0L6_2atmpS1925;
        uint64_t _M0L6_2atmpS1926;
        if (
          _M0L6_2atmpS1919 < 0
          || _M0L6_2atmpS1919 >= Moonbit_array_length(_M0L6resultS697)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS697[_M0L6_2atmpS1919] = _M0L6_2atmpS1920;
        _M0L6_2atmpS1925 = _M0L1iS707 + 1;
        _M0L6_2atmpS1926 = _M0L6outputS708 / 10ull;
        _M0L1iS707 = _M0L6_2atmpS1925;
        _M0L6outputS708 = _M0L6_2atmpS1926;
        continue;
      } else {
        _M0L6outputS706 = _M0L6outputS708;
      }
      break;
    }
    _M0L6_2atmpS1867 = _M0Lm5indexS698;
    _M0L6_2atmpS1871 = (int32_t)_M0L6outputS706;
    _M0L6_2atmpS1870 = _M0L6_2atmpS1871 % 10;
    _M0L6_2atmpS1869 = 48 + _M0L6_2atmpS1870;
    _M0L6_2atmpS1868 = _M0L6_2atmpS1869 & 0xff;
    if (
      _M0L6_2atmpS1867 < 0
      || _M0L6_2atmpS1867 >= Moonbit_array_length(_M0L6resultS697)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS697[_M0L6_2atmpS1867] = _M0L6_2atmpS1868;
    if (_M0L7olengthS702 > 1) {
      int32_t _M0L6_2atmpS1873 = _M0Lm5indexS698;
      int32_t _M0L6_2atmpS1872 = _M0L6_2atmpS1873 + 1;
      if (
        _M0L6_2atmpS1872 < 0
        || _M0L6_2atmpS1872 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1872] = 46;
    } else {
      int32_t _M0L6_2atmpS1874 = _M0Lm5indexS698;
      _M0Lm5indexS698 = _M0L6_2atmpS1874 - 1;
    }
    _M0L6_2atmpS1875 = _M0Lm5indexS698;
    _M0L6_2atmpS1876 = _M0L7olengthS702 + 1;
    _M0Lm5indexS698 = _M0L6_2atmpS1875 + _M0L6_2atmpS1876;
    _M0L6_2atmpS1877 = _M0Lm5indexS698;
    if (
      _M0L6_2atmpS1877 < 0
      || _M0L6_2atmpS1877 >= Moonbit_array_length(_M0L6resultS697)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS697[_M0L6_2atmpS1877] = 101;
    _M0L6_2atmpS1878 = _M0Lm5indexS698;
    _M0Lm5indexS698 = _M0L6_2atmpS1878 + 1;
    _M0L6_2atmpS1879 = _M0Lm3expS703;
    if (_M0L6_2atmpS1879 < 0) {
      int32_t _M0L6_2atmpS1880 = _M0Lm5indexS698;
      int32_t _M0L6_2atmpS1881;
      int32_t _M0L6_2atmpS1882;
      if (
        _M0L6_2atmpS1880 < 0
        || _M0L6_2atmpS1880 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1880] = 45;
      _M0L6_2atmpS1881 = _M0Lm5indexS698;
      _M0Lm5indexS698 = _M0L6_2atmpS1881 + 1;
      _M0L6_2atmpS1882 = _M0Lm3expS703;
      _M0Lm3expS703 = -_M0L6_2atmpS1882;
    } else {
      int32_t _M0L6_2atmpS1883 = _M0Lm5indexS698;
      int32_t _M0L6_2atmpS1884;
      if (
        _M0L6_2atmpS1883 < 0
        || _M0L6_2atmpS1883 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1883] = 43;
      _M0L6_2atmpS1884 = _M0Lm5indexS698;
      _M0Lm5indexS698 = _M0L6_2atmpS1884 + 1;
    }
    _M0L6_2atmpS1885 = _M0Lm3expS703;
    if (_M0L6_2atmpS1885 >= 100) {
      int32_t _M0L6_2atmpS1901 = _M0Lm3expS703;
      int32_t _M0L1aS711 = _M0L6_2atmpS1901 / 100;
      int32_t _M0L6_2atmpS1900 = _M0Lm3expS703;
      int32_t _M0L6_2atmpS1899 = _M0L6_2atmpS1900 / 10;
      int32_t _M0L1bS712 = _M0L6_2atmpS1899 % 10;
      int32_t _M0L6_2atmpS1898 = _M0Lm3expS703;
      int32_t _M0L1cS713 = _M0L6_2atmpS1898 % 10;
      int32_t _M0L6_2atmpS1886 = _M0Lm5indexS698;
      int32_t _M0L6_2atmpS1888 = 48 + _M0L1aS711;
      int32_t _M0L6_2atmpS1887 = _M0L6_2atmpS1888 & 0xff;
      int32_t _M0L6_2atmpS1892;
      int32_t _M0L6_2atmpS1889;
      int32_t _M0L6_2atmpS1891;
      int32_t _M0L6_2atmpS1890;
      int32_t _M0L6_2atmpS1896;
      int32_t _M0L6_2atmpS1893;
      int32_t _M0L6_2atmpS1895;
      int32_t _M0L6_2atmpS1894;
      int32_t _M0L6_2atmpS1897;
      if (
        _M0L6_2atmpS1886 < 0
        || _M0L6_2atmpS1886 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1886] = _M0L6_2atmpS1887;
      _M0L6_2atmpS1892 = _M0Lm5indexS698;
      _M0L6_2atmpS1889 = _M0L6_2atmpS1892 + 1;
      _M0L6_2atmpS1891 = 48 + _M0L1bS712;
      _M0L6_2atmpS1890 = _M0L6_2atmpS1891 & 0xff;
      if (
        _M0L6_2atmpS1889 < 0
        || _M0L6_2atmpS1889 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1889] = _M0L6_2atmpS1890;
      _M0L6_2atmpS1896 = _M0Lm5indexS698;
      _M0L6_2atmpS1893 = _M0L6_2atmpS1896 + 2;
      _M0L6_2atmpS1895 = 48 + _M0L1cS713;
      _M0L6_2atmpS1894 = _M0L6_2atmpS1895 & 0xff;
      if (
        _M0L6_2atmpS1893 < 0
        || _M0L6_2atmpS1893 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1893] = _M0L6_2atmpS1894;
      _M0L6_2atmpS1897 = _M0Lm5indexS698;
      _M0Lm5indexS698 = _M0L6_2atmpS1897 + 3;
    } else {
      int32_t _M0L6_2atmpS1902 = _M0Lm3expS703;
      if (_M0L6_2atmpS1902 >= 10) {
        int32_t _M0L6_2atmpS1912 = _M0Lm3expS703;
        int32_t _M0L1aS714 = _M0L6_2atmpS1912 / 10;
        int32_t _M0L6_2atmpS1911 = _M0Lm3expS703;
        int32_t _M0L1bS715 = _M0L6_2atmpS1911 % 10;
        int32_t _M0L6_2atmpS1903 = _M0Lm5indexS698;
        int32_t _M0L6_2atmpS1905 = 48 + _M0L1aS714;
        int32_t _M0L6_2atmpS1904 = _M0L6_2atmpS1905 & 0xff;
        int32_t _M0L6_2atmpS1909;
        int32_t _M0L6_2atmpS1906;
        int32_t _M0L6_2atmpS1908;
        int32_t _M0L6_2atmpS1907;
        int32_t _M0L6_2atmpS1910;
        if (
          _M0L6_2atmpS1903 < 0
          || _M0L6_2atmpS1903 >= Moonbit_array_length(_M0L6resultS697)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS697[_M0L6_2atmpS1903] = _M0L6_2atmpS1904;
        _M0L6_2atmpS1909 = _M0Lm5indexS698;
        _M0L6_2atmpS1906 = _M0L6_2atmpS1909 + 1;
        _M0L6_2atmpS1908 = 48 + _M0L1bS715;
        _M0L6_2atmpS1907 = _M0L6_2atmpS1908 & 0xff;
        if (
          _M0L6_2atmpS1906 < 0
          || _M0L6_2atmpS1906 >= Moonbit_array_length(_M0L6resultS697)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS697[_M0L6_2atmpS1906] = _M0L6_2atmpS1907;
        _M0L6_2atmpS1910 = _M0Lm5indexS698;
        _M0Lm5indexS698 = _M0L6_2atmpS1910 + 2;
      } else {
        int32_t _M0L6_2atmpS1913 = _M0Lm5indexS698;
        int32_t _M0L6_2atmpS1916 = _M0Lm3expS703;
        int32_t _M0L6_2atmpS1915 = 48 + _M0L6_2atmpS1916;
        int32_t _M0L6_2atmpS1914 = _M0L6_2atmpS1915 & 0xff;
        int32_t _M0L6_2atmpS1917;
        if (
          _M0L6_2atmpS1913 < 0
          || _M0L6_2atmpS1913 >= Moonbit_array_length(_M0L6resultS697)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS697[_M0L6_2atmpS1913] = _M0L6_2atmpS1914;
        _M0L6_2atmpS1917 = _M0Lm5indexS698;
        _M0Lm5indexS698 = _M0L6_2atmpS1917 + 1;
      }
    }
    _M0L6_2atmpS1918 = _M0Lm5indexS698;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2581
    = _M0FPB19string__from__bytes(_M0L6resultS697, 0, _M0L6_2atmpS1918);
    moonbit_decref_cycle_free(_M0L6resultS697);
    return _result_2581;
  } else {
    int32_t _M0L6_2atmpS1927 = _M0Lm3expS703;
    int32_t _M0L6_2atmpS1990;
    moonbit_string_t _result_2587;
    if (_M0L6_2atmpS1927 < 0) {
      int32_t _M0L6_2atmpS1928 = _M0Lm5indexS698;
      int32_t _M0L6_2atmpS1930;
      int32_t _M0L6_2atmpS1929;
      int32_t _M0L6_2atmpS1931;
      int32_t _M0L1iS716;
      int32_t _M0L6_2atmpS1946;
      int32_t _M0L6_2atmpS1948;
      int32_t _M0L6_2atmpS1947;
      int32_t _M0L7currentS718;
      int32_t _M0L1iS719;
      uint64_t _M0L6outputS720;
      if (
        _M0L6_2atmpS1928 < 0
        || _M0L6_2atmpS1928 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1928] = 48;
      _M0L6_2atmpS1930 = _M0Lm5indexS698;
      _M0L6_2atmpS1929 = _M0L6_2atmpS1930 + 1;
      if (
        _M0L6_2atmpS1929 < 0
        || _M0L6_2atmpS1929 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1929] = 46;
      _M0L6_2atmpS1931 = _M0Lm5indexS698;
      _M0Lm5indexS698 = _M0L6_2atmpS1931 + 2;
      _M0L1iS716 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1932 = _M0Lm3expS703;
        if (_M0L1iS716 > _M0L6_2atmpS1932) {
          int32_t _M0L6_2atmpS1935 = _M0Lm5indexS698;
          int32_t _M0L6_2atmpS1934 = _M0L6_2atmpS1935 - _M0L1iS716;
          int32_t _M0L6_2atmpS1933 = _M0L6_2atmpS1934 - 1;
          int32_t _M0L6_2atmpS1936;
          if (
            _M0L6_2atmpS1933 < 0
            || _M0L6_2atmpS1933 >= Moonbit_array_length(_M0L6resultS697)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS697[_M0L6_2atmpS1933] = 48;
          _M0L6_2atmpS1936 = _M0L1iS716 - 1;
          _M0L1iS716 = _M0L6_2atmpS1936;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1946 = _M0Lm5indexS698;
      _M0L6_2atmpS1948 = _M0Lm3expS703;
      _M0L6_2atmpS1947 = -1 - _M0L6_2atmpS1948;
      _M0L7currentS718 = _M0L6_2atmpS1946 + _M0L6_2atmpS1947;
      _M0L1iS719 = 0;
      _M0L6outputS720 = _M0L6outputS700;
      while (1) {
        if (_M0L1iS719 < _M0L7olengthS702) {
          int32_t _M0L6_2atmpS1943 = _M0L7currentS718 + _M0L7olengthS702;
          int32_t _M0L6_2atmpS1942 = _M0L6_2atmpS1943 - _M0L1iS719;
          int32_t _M0L6_2atmpS1937 = _M0L6_2atmpS1942 - 1;
          uint64_t _M0L6_2atmpS1941 = _M0L6outputS720 % 10ull;
          int32_t _M0L6_2atmpS1940 = (int32_t)_M0L6_2atmpS1941;
          int32_t _M0L6_2atmpS1939 = 48 + _M0L6_2atmpS1940;
          int32_t _M0L6_2atmpS1938 = _M0L6_2atmpS1939 & 0xff;
          int32_t _M0L6_2atmpS1944;
          uint64_t _M0L6_2atmpS1945;
          if (
            _M0L6_2atmpS1937 < 0
            || _M0L6_2atmpS1937 >= Moonbit_array_length(_M0L6resultS697)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS697[_M0L6_2atmpS1937] = _M0L6_2atmpS1938;
          _M0L6_2atmpS1944 = _M0L1iS719 + 1;
          _M0L6_2atmpS1945 = _M0L6outputS720 / 10ull;
          _M0L1iS719 = _M0L6_2atmpS1944;
          _M0L6outputS720 = _M0L6_2atmpS1945;
          continue;
        }
        break;
      }
      _M0Lm5indexS698 = _M0L7currentS718 + _M0L7olengthS702;
    } else {
      int32_t _M0L6_2atmpS1950 = _M0Lm3expS703;
      int32_t _M0L6_2atmpS1949 = _M0L6_2atmpS1950 + 1;
      if (_M0L6_2atmpS1949 >= _M0L7olengthS702) {
        int32_t _M0L1iS722 = 0;
        uint64_t _M0L6outputS723 = _M0L6outputS700;
        int32_t _M0L6_2atmpS1961;
        int32_t _M0L6_2atmpS1966;
        int32_t _M0L7_2abindS725;
        int32_t _M0L1iS726;
        int32_t _M0L6_2atmpS1967;
        int32_t _M0L6_2atmpS1970;
        int32_t _M0L6_2atmpS1969;
        int32_t _M0L6_2atmpS1968;
        while (1) {
          if (_M0L1iS722 < _M0L7olengthS702) {
            int32_t _M0L6_2atmpS1958 = _M0Lm5indexS698;
            int32_t _M0L6_2atmpS1957 = _M0L6_2atmpS1958 + _M0L7olengthS702;
            int32_t _M0L6_2atmpS1956 = _M0L6_2atmpS1957 - _M0L1iS722;
            int32_t _M0L6_2atmpS1951 = _M0L6_2atmpS1956 - 1;
            uint64_t _M0L6_2atmpS1955 = _M0L6outputS723 % 10ull;
            int32_t _M0L6_2atmpS1954 = (int32_t)_M0L6_2atmpS1955;
            int32_t _M0L6_2atmpS1953 = 48 + _M0L6_2atmpS1954;
            int32_t _M0L6_2atmpS1952 = _M0L6_2atmpS1953 & 0xff;
            int32_t _M0L6_2atmpS1959;
            uint64_t _M0L6_2atmpS1960;
            if (
              _M0L6_2atmpS1951 < 0
              || _M0L6_2atmpS1951 >= Moonbit_array_length(_M0L6resultS697)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS697[_M0L6_2atmpS1951] = _M0L6_2atmpS1952;
            _M0L6_2atmpS1959 = _M0L1iS722 + 1;
            _M0L6_2atmpS1960 = _M0L6outputS723 / 10ull;
            _M0L1iS722 = _M0L6_2atmpS1959;
            _M0L6outputS723 = _M0L6_2atmpS1960;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1961 = _M0Lm5indexS698;
        _M0Lm5indexS698 = _M0L6_2atmpS1961 + _M0L7olengthS702;
        _M0L6_2atmpS1966 = _M0Lm3expS703;
        _M0L7_2abindS725 = _M0L6_2atmpS1966 + 1;
        _M0L1iS726 = _M0L7olengthS702;
        while (1) {
          if (_M0L1iS726 < _M0L7_2abindS725) {
            int32_t _M0L6_2atmpS1964 = _M0Lm5indexS698;
            int32_t _M0L6_2atmpS1963 = _M0L6_2atmpS1964 + _M0L1iS726;
            int32_t _M0L6_2atmpS1962 = _M0L6_2atmpS1963 - _M0L7olengthS702;
            int32_t _M0L6_2atmpS1965;
            if (
              _M0L6_2atmpS1962 < 0
              || _M0L6_2atmpS1962 >= Moonbit_array_length(_M0L6resultS697)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS697[_M0L6_2atmpS1962] = 48;
            _M0L6_2atmpS1965 = _M0L1iS726 + 1;
            _M0L1iS726 = _M0L6_2atmpS1965;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1967 = _M0Lm5indexS698;
        _M0L6_2atmpS1970 = _M0Lm3expS703;
        _M0L6_2atmpS1969 = _M0L6_2atmpS1970 + 1;
        _M0L6_2atmpS1968 = _M0L6_2atmpS1969 - _M0L7olengthS702;
        _M0Lm5indexS698 = _M0L6_2atmpS1967 + _M0L6_2atmpS1968;
      } else {
        int32_t _M0L6_2atmpS1987 = _M0Lm5indexS698;
        int32_t _M0L6_2atmpS1986 = _M0L6_2atmpS1987 + 1;
        int32_t _M0L1iS728 = 0;
        int32_t _M0L7currentS729 = _M0L6_2atmpS1986;
        uint64_t _M0L6outputS730 = _M0L6outputS700;
        int32_t _M0L6_2atmpS1988;
        int32_t _M0L6_2atmpS1989;
        while (1) {
          if (_M0L1iS728 < _M0L7olengthS702) {
            int32_t _M0L6_2atmpS1982 = _M0L7olengthS702 - _M0L1iS728;
            int32_t _M0L6_2atmpS1980 = _M0L6_2atmpS1982 - 1;
            int32_t _M0L6_2atmpS1981 = _M0Lm3expS703;
            int32_t _M0L7currentS731;
            int32_t _M0L6_2atmpS1977;
            int32_t _M0L6_2atmpS1976;
            int32_t _M0L6_2atmpS1971;
            uint64_t _M0L6_2atmpS1975;
            int32_t _M0L6_2atmpS1974;
            int32_t _M0L6_2atmpS1973;
            int32_t _M0L6_2atmpS1972;
            int32_t _M0L6_2atmpS1978;
            uint64_t _M0L6_2atmpS1979;
            if (_M0L6_2atmpS1980 == _M0L6_2atmpS1981) {
              int32_t _M0L6_2atmpS1985 = _M0L7currentS729 + _M0L7olengthS702;
              int32_t _M0L6_2atmpS1984 = _M0L6_2atmpS1985 - _M0L1iS728;
              int32_t _M0L6_2atmpS1983 = _M0L6_2atmpS1984 - 1;
              if (
                _M0L6_2atmpS1983 < 0
                || _M0L6_2atmpS1983 >= Moonbit_array_length(_M0L6resultS697)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS697[_M0L6_2atmpS1983] = 46;
              _M0L7currentS731 = _M0L7currentS729 - 1;
            } else {
              _M0L7currentS731 = _M0L7currentS729;
            }
            _M0L6_2atmpS1977 = _M0L7currentS731 + _M0L7olengthS702;
            _M0L6_2atmpS1976 = _M0L6_2atmpS1977 - _M0L1iS728;
            _M0L6_2atmpS1971 = _M0L6_2atmpS1976 - 1;
            _M0L6_2atmpS1975 = _M0L6outputS730 % 10ull;
            _M0L6_2atmpS1974 = (int32_t)_M0L6_2atmpS1975;
            _M0L6_2atmpS1973 = 48 + _M0L6_2atmpS1974;
            _M0L6_2atmpS1972 = _M0L6_2atmpS1973 & 0xff;
            if (
              _M0L6_2atmpS1971 < 0
              || _M0L6_2atmpS1971 >= Moonbit_array_length(_M0L6resultS697)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS697[_M0L6_2atmpS1971] = _M0L6_2atmpS1972;
            _M0L6_2atmpS1978 = _M0L1iS728 + 1;
            _M0L6_2atmpS1979 = _M0L6outputS730 / 10ull;
            _M0L1iS728 = _M0L6_2atmpS1978;
            _M0L7currentS729 = _M0L7currentS731;
            _M0L6outputS730 = _M0L6_2atmpS1979;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1988 = _M0Lm5indexS698;
        _M0L6_2atmpS1989 = _M0L7olengthS702 + 1;
        _M0Lm5indexS698 = _M0L6_2atmpS1988 + _M0L6_2atmpS1989;
      }
    }
    _M0L6_2atmpS1990 = _M0Lm5indexS698;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2587
    = _M0FPB19string__from__bytes(_M0L6resultS697, 0, _M0L6_2atmpS1990);
    moonbit_decref_cycle_free(_M0L6resultS697);
    return _result_2587;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS643,
  uint32_t _M0L12ieeeExponentS642
) {
  int32_t _M0Lm2e2S640;
  uint64_t _M0Lm2m2S641;
  uint64_t _M0L6_2atmpS1864;
  uint64_t _M0L6_2atmpS1863;
  int32_t _M0L4evenS644;
  uint64_t _M0L6_2atmpS1862;
  uint64_t _M0L2mvS645;
  int32_t _M0L7mmShiftS646;
  uint64_t _M0Lm2vrS647;
  uint64_t _M0Lm2vpS648;
  uint64_t _M0Lm2vmS649;
  int32_t _M0Lm3e10S650;
  int32_t _M0Lm17vmIsTrailingZerosS651;
  int32_t _M0Lm17vrIsTrailingZerosS652;
  int32_t _M0L6_2atmpS1764;
  int32_t _M0Lm7removedS671;
  int32_t _M0Lm16lastRemovedDigitS672;
  uint64_t _M0Lm6outputS673;
  int32_t _M0L6_2atmpS1860;
  int32_t _M0L6_2atmpS1861;
  int32_t _M0L3expS696;
  uint64_t _M0L6_2atmpS1859;
  struct _M0TPB17FloatingDecimal64* _block_2593;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S640 = 0;
  _M0Lm2m2S641 = 0ull;
  if (_M0L12ieeeExponentS642 == 0u) {
    _M0Lm2e2S640 = -1076;
    _M0Lm2m2S641 = _M0L12ieeeMantissaS643;
  } else {
    int32_t _M0L6_2atmpS1763 = *(int32_t*)&_M0L12ieeeExponentS642;
    int32_t _M0L6_2atmpS1762 = _M0L6_2atmpS1763 - 1023;
    int32_t _M0L6_2atmpS1761 = _M0L6_2atmpS1762 - 52;
    _M0Lm2e2S640 = _M0L6_2atmpS1761 - 2;
    _M0Lm2m2S641 = 4503599627370496ull | _M0L12ieeeMantissaS643;
  }
  _M0L6_2atmpS1864 = _M0Lm2m2S641;
  _M0L6_2atmpS1863 = _M0L6_2atmpS1864 & 1ull;
  _M0L4evenS644 = _M0L6_2atmpS1863 == 0ull;
  _M0L6_2atmpS1862 = _M0Lm2m2S641;
  _M0L2mvS645 = 4ull * _M0L6_2atmpS1862;
  _M0L7mmShiftS646
  = _M0L12ieeeMantissaS643 != 0ull || _M0L12ieeeExponentS642 <= 1u;
  _M0Lm2vrS647 = 0ull;
  _M0Lm2vpS648 = 0ull;
  _M0Lm2vmS649 = 0ull;
  _M0Lm3e10S650 = 0;
  _M0Lm17vmIsTrailingZerosS651 = 0;
  _M0Lm17vrIsTrailingZerosS652 = 0;
  _M0L6_2atmpS1764 = _M0Lm2e2S640;
  if (_M0L6_2atmpS1764 >= 0) {
    int32_t _M0L6_2atmpS1786 = _M0Lm2e2S640;
    int32_t _M0L6_2atmpS1782;
    int32_t _M0L6_2atmpS1785;
    int32_t _M0L6_2atmpS1784;
    int32_t _M0L6_2atmpS1783;
    int32_t _M0L1qS653;
    int32_t _M0L6_2atmpS1781;
    int32_t _M0L6_2atmpS1780;
    int32_t _M0L1kS654;
    int32_t _M0L6_2atmpS1779;
    int32_t _M0L6_2atmpS1778;
    int32_t _M0L6_2atmpS1777;
    int32_t _M0L1iS655;
    struct _M0TPB8Pow5Pair _M0L4pow5S656;
    uint64_t _M0L6_2atmpS1776;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS657;
    uint64_t _M0L8_2avrOutS658;
    uint64_t _M0L8_2avpOutS659;
    uint64_t _M0L8_2avmOutS660;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1782 = _M0FPB9log10Pow2(_M0L6_2atmpS1786);
    _M0L6_2atmpS1785 = _M0Lm2e2S640;
    _M0L6_2atmpS1784 = _M0L6_2atmpS1785 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1783 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1784);
    _M0L1qS653 = _M0L6_2atmpS1782 - _M0L6_2atmpS1783;
    _M0Lm3e10S650 = _M0L1qS653;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1781 = _M0FPB8pow5bits(_M0L1qS653);
    _M0L6_2atmpS1780 = 125 + _M0L6_2atmpS1781;
    _M0L1kS654 = _M0L6_2atmpS1780 - 1;
    _M0L6_2atmpS1779 = _M0Lm2e2S640;
    _M0L6_2atmpS1778 = -_M0L6_2atmpS1779;
    _M0L6_2atmpS1777 = _M0L6_2atmpS1778 + _M0L1qS653;
    _M0L1iS655 = _M0L6_2atmpS1777 + _M0L1kS654;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S656 = _M0FPB22double__computeInvPow5(_M0L1qS653);
    _M0L6_2atmpS1776 = _M0Lm2m2S641;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS657
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1776, _M0L4pow5S656, _M0L1iS655, _M0L7mmShiftS646);
    _M0L8_2avrOutS658 = _M0L7_2abindS657.$0;
    _M0L8_2avpOutS659 = _M0L7_2abindS657.$1;
    _M0L8_2avmOutS660 = _M0L7_2abindS657.$2;
    _M0Lm2vrS647 = _M0L8_2avrOutS658;
    _M0Lm2vpS648 = _M0L8_2avpOutS659;
    _M0Lm2vmS649 = _M0L8_2avmOutS660;
    if (_M0L1qS653 <= 21) {
      int32_t _M0L6_2atmpS1772 = (int32_t)_M0L2mvS645;
      uint64_t _M0L6_2atmpS1775 = _M0L2mvS645 / 5ull;
      int32_t _M0L6_2atmpS1774 = (int32_t)_M0L6_2atmpS1775;
      int32_t _M0L6_2atmpS1773 = 5 * _M0L6_2atmpS1774;
      int32_t _M0L6mvMod5S661 = _M0L6_2atmpS1772 - _M0L6_2atmpS1773;
      if (_M0L6mvMod5S661 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS652
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS645, _M0L1qS653);
      } else if (_M0L4evenS644) {
        uint64_t _M0L6_2atmpS1766 = _M0L2mvS645 - 1ull;
        uint64_t _M0L6_2atmpS1767;
        uint64_t _M0L6_2atmpS1765;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1767 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS646);
        _M0L6_2atmpS1765 = _M0L6_2atmpS1766 - _M0L6_2atmpS1767;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS651
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1765, _M0L1qS653);
      } else {
        uint64_t _M0L6_2atmpS1768 = _M0Lm2vpS648;
        uint64_t _M0L6_2atmpS1771 = _M0L2mvS645 + 2ull;
        int32_t _M0L6_2atmpS1770;
        uint64_t _M0L6_2atmpS1769;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1770
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1771, _M0L1qS653);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1769 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1770);
        _M0Lm2vpS648 = _M0L6_2atmpS1768 - _M0L6_2atmpS1769;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1800 = _M0Lm2e2S640;
    int32_t _M0L6_2atmpS1799 = -_M0L6_2atmpS1800;
    int32_t _M0L6_2atmpS1794;
    int32_t _M0L6_2atmpS1798;
    int32_t _M0L6_2atmpS1797;
    int32_t _M0L6_2atmpS1796;
    int32_t _M0L6_2atmpS1795;
    int32_t _M0L1qS662;
    int32_t _M0L6_2atmpS1787;
    int32_t _M0L6_2atmpS1793;
    int32_t _M0L6_2atmpS1792;
    int32_t _M0L1iS663;
    int32_t _M0L6_2atmpS1791;
    int32_t _M0L1kS664;
    int32_t _M0L1jS665;
    struct _M0TPB8Pow5Pair _M0L4pow5S666;
    uint64_t _M0L6_2atmpS1790;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS667;
    uint64_t _M0L8_2avrOutS668;
    uint64_t _M0L8_2avpOutS669;
    uint64_t _M0L8_2avmOutS670;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1794 = _M0FPB9log10Pow5(_M0L6_2atmpS1799);
    _M0L6_2atmpS1798 = _M0Lm2e2S640;
    _M0L6_2atmpS1797 = -_M0L6_2atmpS1798;
    _M0L6_2atmpS1796 = _M0L6_2atmpS1797 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1795 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1796);
    _M0L1qS662 = _M0L6_2atmpS1794 - _M0L6_2atmpS1795;
    _M0L6_2atmpS1787 = _M0Lm2e2S640;
    _M0Lm3e10S650 = _M0L1qS662 + _M0L6_2atmpS1787;
    _M0L6_2atmpS1793 = _M0Lm2e2S640;
    _M0L6_2atmpS1792 = -_M0L6_2atmpS1793;
    _M0L1iS663 = _M0L6_2atmpS1792 - _M0L1qS662;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1791 = _M0FPB8pow5bits(_M0L1iS663);
    _M0L1kS664 = _M0L6_2atmpS1791 - 125;
    _M0L1jS665 = _M0L1qS662 - _M0L1kS664;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S666 = _M0FPB19double__computePow5(_M0L1iS663);
    _M0L6_2atmpS1790 = _M0Lm2m2S641;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS667
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1790, _M0L4pow5S666, _M0L1jS665, _M0L7mmShiftS646);
    _M0L8_2avrOutS668 = _M0L7_2abindS667.$0;
    _M0L8_2avpOutS669 = _M0L7_2abindS667.$1;
    _M0L8_2avmOutS670 = _M0L7_2abindS667.$2;
    _M0Lm2vrS647 = _M0L8_2avrOutS668;
    _M0Lm2vpS648 = _M0L8_2avpOutS669;
    _M0Lm2vmS649 = _M0L8_2avmOutS670;
    if (_M0L1qS662 <= 1) {
      _M0Lm17vrIsTrailingZerosS652 = 1;
      if (_M0L4evenS644) {
        int32_t _M0L6_2atmpS1788;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1788 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS646);
        _M0Lm17vmIsTrailingZerosS651 = _M0L6_2atmpS1788 == 1;
      } else {
        uint64_t _M0L6_2atmpS1789 = _M0Lm2vpS648;
        _M0Lm2vpS648 = _M0L6_2atmpS1789 - 1ull;
      }
    } else if (_M0L1qS662 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS652
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS645, _M0L1qS662);
    }
  }
  _M0Lm7removedS671 = 0;
  _M0Lm16lastRemovedDigitS672 = 0;
  _M0Lm6outputS673 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS651 || _M0Lm17vrIsTrailingZerosS652) {
    int32_t _if__result_2590;
    uint64_t _M0L6_2atmpS1830;
    uint64_t _M0L6_2atmpS1836;
    uint64_t _M0L6_2atmpS1837;
    int32_t _if__result_2591;
    int32_t _M0L6_2atmpS1833;
    int64_t _M0L6_2atmpS1832;
    uint64_t _M0L6_2atmpS1831;
    while (1) {
      uint64_t _M0L6_2atmpS1813 = _M0Lm2vpS648;
      uint64_t _M0L7vpDiv10S674 = _M0L6_2atmpS1813 / 10ull;
      uint64_t _M0L6_2atmpS1812 = _M0Lm2vmS649;
      uint64_t _M0L7vmDiv10S675 = _M0L6_2atmpS1812 / 10ull;
      uint64_t _M0L6_2atmpS1811;
      int32_t _M0L6_2atmpS1808;
      int32_t _M0L6_2atmpS1810;
      int32_t _M0L6_2atmpS1809;
      int32_t _M0L7vmMod10S677;
      uint64_t _M0L6_2atmpS1807;
      uint64_t _M0L7vrDiv10S678;
      uint64_t _M0L6_2atmpS1806;
      int32_t _M0L6_2atmpS1803;
      int32_t _M0L6_2atmpS1805;
      int32_t _M0L6_2atmpS1804;
      int32_t _M0L7vrMod10S679;
      int32_t _M0L6_2atmpS1802;
      if (_M0L7vpDiv10S674 <= _M0L7vmDiv10S675) {
        break;
      }
      _M0L6_2atmpS1811 = _M0Lm2vmS649;
      _M0L6_2atmpS1808 = (int32_t)_M0L6_2atmpS1811;
      _M0L6_2atmpS1810 = (int32_t)_M0L7vmDiv10S675;
      _M0L6_2atmpS1809 = 10 * _M0L6_2atmpS1810;
      _M0L7vmMod10S677 = _M0L6_2atmpS1808 - _M0L6_2atmpS1809;
      _M0L6_2atmpS1807 = _M0Lm2vrS647;
      _M0L7vrDiv10S678 = _M0L6_2atmpS1807 / 10ull;
      _M0L6_2atmpS1806 = _M0Lm2vrS647;
      _M0L6_2atmpS1803 = (int32_t)_M0L6_2atmpS1806;
      _M0L6_2atmpS1805 = (int32_t)_M0L7vrDiv10S678;
      _M0L6_2atmpS1804 = 10 * _M0L6_2atmpS1805;
      _M0L7vrMod10S679 = _M0L6_2atmpS1803 - _M0L6_2atmpS1804;
      _M0Lm17vmIsTrailingZerosS651
      = _M0Lm17vmIsTrailingZerosS651 && _M0L7vmMod10S677 == 0;
      if (_M0Lm17vrIsTrailingZerosS652) {
        int32_t _M0L6_2atmpS1801 = _M0Lm16lastRemovedDigitS672;
        _M0Lm17vrIsTrailingZerosS652 = _M0L6_2atmpS1801 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS652 = 0;
      }
      _M0Lm16lastRemovedDigitS672 = _M0L7vrMod10S679;
      _M0Lm2vrS647 = _M0L7vrDiv10S678;
      _M0Lm2vpS648 = _M0L7vpDiv10S674;
      _M0Lm2vmS649 = _M0L7vmDiv10S675;
      _M0L6_2atmpS1802 = _M0Lm7removedS671;
      _M0Lm7removedS671 = _M0L6_2atmpS1802 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS651) {
      while (1) {
        uint64_t _M0L6_2atmpS1826 = _M0Lm2vmS649;
        uint64_t _M0L7vmDiv10S680 = _M0L6_2atmpS1826 / 10ull;
        uint64_t _M0L6_2atmpS1825 = _M0Lm2vmS649;
        int32_t _M0L6_2atmpS1822 = (int32_t)_M0L6_2atmpS1825;
        int32_t _M0L6_2atmpS1824 = (int32_t)_M0L7vmDiv10S680;
        int32_t _M0L6_2atmpS1823 = 10 * _M0L6_2atmpS1824;
        int32_t _M0L7vmMod10S681 = _M0L6_2atmpS1822 - _M0L6_2atmpS1823;
        uint64_t _M0L6_2atmpS1821;
        uint64_t _M0L7vpDiv10S683;
        uint64_t _M0L6_2atmpS1820;
        uint64_t _M0L7vrDiv10S684;
        uint64_t _M0L6_2atmpS1819;
        int32_t _M0L6_2atmpS1816;
        int32_t _M0L6_2atmpS1818;
        int32_t _M0L6_2atmpS1817;
        int32_t _M0L7vrMod10S685;
        int32_t _M0L6_2atmpS1815;
        if (_M0L7vmMod10S681 != 0) {
          break;
        }
        _M0L6_2atmpS1821 = _M0Lm2vpS648;
        _M0L7vpDiv10S683 = _M0L6_2atmpS1821 / 10ull;
        _M0L6_2atmpS1820 = _M0Lm2vrS647;
        _M0L7vrDiv10S684 = _M0L6_2atmpS1820 / 10ull;
        _M0L6_2atmpS1819 = _M0Lm2vrS647;
        _M0L6_2atmpS1816 = (int32_t)_M0L6_2atmpS1819;
        _M0L6_2atmpS1818 = (int32_t)_M0L7vrDiv10S684;
        _M0L6_2atmpS1817 = 10 * _M0L6_2atmpS1818;
        _M0L7vrMod10S685 = _M0L6_2atmpS1816 - _M0L6_2atmpS1817;
        if (_M0Lm17vrIsTrailingZerosS652) {
          int32_t _M0L6_2atmpS1814 = _M0Lm16lastRemovedDigitS672;
          _M0Lm17vrIsTrailingZerosS652 = _M0L6_2atmpS1814 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS652 = 0;
        }
        _M0Lm16lastRemovedDigitS672 = _M0L7vrMod10S685;
        _M0Lm2vrS647 = _M0L7vrDiv10S684;
        _M0Lm2vpS648 = _M0L7vpDiv10S683;
        _M0Lm2vmS649 = _M0L7vmDiv10S680;
        _M0L6_2atmpS1815 = _M0Lm7removedS671;
        _M0Lm7removedS671 = _M0L6_2atmpS1815 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS652) {
      int32_t _M0L6_2atmpS1829 = _M0Lm16lastRemovedDigitS672;
      if (_M0L6_2atmpS1829 == 5) {
        uint64_t _M0L6_2atmpS1828 = _M0Lm2vrS647;
        uint64_t _M0L6_2atmpS1827 = _M0L6_2atmpS1828 % 2ull;
        _if__result_2590 = _M0L6_2atmpS1827 == 0ull;
      } else {
        _if__result_2590 = 0;
      }
    } else {
      _if__result_2590 = 0;
    }
    if (_if__result_2590) {
      _M0Lm16lastRemovedDigitS672 = 4;
    }
    _M0L6_2atmpS1830 = _M0Lm2vrS647;
    _M0L6_2atmpS1836 = _M0Lm2vrS647;
    _M0L6_2atmpS1837 = _M0Lm2vmS649;
    if (_M0L6_2atmpS1836 == _M0L6_2atmpS1837) {
      if (!_M0L4evenS644) {
        _if__result_2591 = 1;
      } else {
        int32_t _M0L6_2atmpS1835 = _M0Lm17vmIsTrailingZerosS651;
        _if__result_2591 = !_M0L6_2atmpS1835;
      }
    } else {
      _if__result_2591 = 0;
    }
    if (_if__result_2591) {
      _M0L6_2atmpS1833 = 1;
    } else {
      int32_t _M0L6_2atmpS1834 = _M0Lm16lastRemovedDigitS672;
      _M0L6_2atmpS1833 = _M0L6_2atmpS1834 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1832 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1833);
    _M0L6_2atmpS1831 = *(uint64_t*)&_M0L6_2atmpS1832;
    _M0Lm6outputS673 = _M0L6_2atmpS1830 + _M0L6_2atmpS1831;
  } else {
    int32_t _M0Lm7roundUpS686 = 0;
    uint64_t _M0L6_2atmpS1858 = _M0Lm2vpS648;
    uint64_t _M0L8vpDiv100S687 = _M0L6_2atmpS1858 / 100ull;
    uint64_t _M0L6_2atmpS1857 = _M0Lm2vmS649;
    uint64_t _M0L8vmDiv100S688 = _M0L6_2atmpS1857 / 100ull;
    uint64_t _M0L6_2atmpS1852;
    uint64_t _M0L6_2atmpS1855;
    uint64_t _M0L6_2atmpS1856;
    int32_t _M0L6_2atmpS1854;
    uint64_t _M0L6_2atmpS1853;
    if (_M0L8vpDiv100S687 > _M0L8vmDiv100S688) {
      uint64_t _M0L6_2atmpS1843 = _M0Lm2vrS647;
      uint64_t _M0L8vrDiv100S689 = _M0L6_2atmpS1843 / 100ull;
      uint64_t _M0L6_2atmpS1842 = _M0Lm2vrS647;
      int32_t _M0L6_2atmpS1839 = (int32_t)_M0L6_2atmpS1842;
      int32_t _M0L6_2atmpS1841 = (int32_t)_M0L8vrDiv100S689;
      int32_t _M0L6_2atmpS1840 = 100 * _M0L6_2atmpS1841;
      int32_t _M0L8vrMod100S690 = _M0L6_2atmpS1839 - _M0L6_2atmpS1840;
      int32_t _M0L6_2atmpS1838;
      _M0Lm7roundUpS686 = _M0L8vrMod100S690 >= 50;
      _M0Lm2vrS647 = _M0L8vrDiv100S689;
      _M0Lm2vpS648 = _M0L8vpDiv100S687;
      _M0Lm2vmS649 = _M0L8vmDiv100S688;
      _M0L6_2atmpS1838 = _M0Lm7removedS671;
      _M0Lm7removedS671 = _M0L6_2atmpS1838 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1851 = _M0Lm2vpS648;
      uint64_t _M0L7vpDiv10S691 = _M0L6_2atmpS1851 / 10ull;
      uint64_t _M0L6_2atmpS1850 = _M0Lm2vmS649;
      uint64_t _M0L7vmDiv10S692 = _M0L6_2atmpS1850 / 10ull;
      uint64_t _M0L6_2atmpS1849;
      uint64_t _M0L7vrDiv10S694;
      uint64_t _M0L6_2atmpS1848;
      int32_t _M0L6_2atmpS1845;
      int32_t _M0L6_2atmpS1847;
      int32_t _M0L6_2atmpS1846;
      int32_t _M0L7vrMod10S695;
      int32_t _M0L6_2atmpS1844;
      if (_M0L7vpDiv10S691 <= _M0L7vmDiv10S692) {
        break;
      }
      _M0L6_2atmpS1849 = _M0Lm2vrS647;
      _M0L7vrDiv10S694 = _M0L6_2atmpS1849 / 10ull;
      _M0L6_2atmpS1848 = _M0Lm2vrS647;
      _M0L6_2atmpS1845 = (int32_t)_M0L6_2atmpS1848;
      _M0L6_2atmpS1847 = (int32_t)_M0L7vrDiv10S694;
      _M0L6_2atmpS1846 = 10 * _M0L6_2atmpS1847;
      _M0L7vrMod10S695 = _M0L6_2atmpS1845 - _M0L6_2atmpS1846;
      _M0Lm7roundUpS686 = _M0L7vrMod10S695 >= 5;
      _M0Lm2vrS647 = _M0L7vrDiv10S694;
      _M0Lm2vpS648 = _M0L7vpDiv10S691;
      _M0Lm2vmS649 = _M0L7vmDiv10S692;
      _M0L6_2atmpS1844 = _M0Lm7removedS671;
      _M0Lm7removedS671 = _M0L6_2atmpS1844 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1852 = _M0Lm2vrS647;
    _M0L6_2atmpS1855 = _M0Lm2vrS647;
    _M0L6_2atmpS1856 = _M0Lm2vmS649;
    _M0L6_2atmpS1854
    = _M0L6_2atmpS1855 == _M0L6_2atmpS1856 || _M0Lm7roundUpS686;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1853 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1854);
    _M0Lm6outputS673 = _M0L6_2atmpS1852 + _M0L6_2atmpS1853;
  }
  _M0L6_2atmpS1860 = _M0Lm3e10S650;
  _M0L6_2atmpS1861 = _M0Lm7removedS671;
  _M0L3expS696 = _M0L6_2atmpS1860 + _M0L6_2atmpS1861;
  _M0L6_2atmpS1859 = _M0Lm6outputS673;
  _block_2593
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2593)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2593->$0 = _M0L6_2atmpS1859;
  _block_2593->$1 = _M0L3expS696;
  return _block_2593;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS639) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS639) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS638) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS638) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS637) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS637) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS636) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS636 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS636 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS636 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS636 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS636 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS636 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS636 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS636 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS636 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS636 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS636 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS636 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS636 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS636 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS636 >= 100ull) {
    return 3;
  }
  if (_M0L1vS636 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS619) {
  int32_t _M0L6_2atmpS1760;
  int32_t _M0L6_2atmpS1759;
  int32_t _M0L4baseS618;
  int32_t _M0L5base2S620;
  int32_t _M0L6offsetS621;
  int32_t _M0L6_2atmpS1758;
  uint64_t _M0L4mul0S622;
  int32_t _M0L6_2atmpS1757;
  int32_t _M0L6_2atmpS1756;
  uint64_t _M0L4mul1S623;
  uint64_t _M0L1mS624;
  struct _M0TPB7Umul128 _M0L7_2abindS625;
  uint64_t _M0L7_2alow1S626;
  uint64_t _M0L8_2ahigh1S627;
  struct _M0TPB7Umul128 _M0L7_2abindS628;
  uint64_t _M0L7_2alow0S629;
  uint64_t _M0L8_2ahigh0S630;
  uint64_t _M0L3sumS631;
  uint64_t _M0Lm5high1S632;
  int32_t _M0L6_2atmpS1754;
  int32_t _M0L6_2atmpS1755;
  int32_t _M0L5deltaS633;
  uint64_t _M0L6_2atmpS1753;
  uint64_t _M0L6_2atmpS1745;
  int32_t _M0L6_2atmpS1752;
  uint32_t _M0L6_2atmpS1749;
  int32_t _M0L6_2atmpS1751;
  int32_t _M0L6_2atmpS1750;
  uint32_t _M0L6_2atmpS1748;
  uint32_t _M0L6_2atmpS1747;
  uint64_t _M0L6_2atmpS1746;
  uint64_t _M0L1aS634;
  uint64_t _M0L6_2atmpS1744;
  uint64_t _M0L1bS635;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1760 = _M0L1iS619 + 26;
  _M0L6_2atmpS1759 = _M0L6_2atmpS1760 - 1;
  _M0L4baseS618 = _M0L6_2atmpS1759 / 26;
  _M0L5base2S620 = _M0L4baseS618 * 26;
  _M0L6offsetS621 = _M0L5base2S620 - _M0L1iS619;
  _M0L6_2atmpS1758 = _M0L4baseS618 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S622
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1758);
  _M0L6_2atmpS1757 = _M0L4baseS618 * 2;
  _M0L6_2atmpS1756 = _M0L6_2atmpS1757 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S623
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1756);
  if (_M0L6offsetS621 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S622, .$1 = _M0L4mul1S623};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS624
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS621);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS625 = _M0FPB7umul128(_M0L1mS624, _M0L4mul1S623);
  _M0L7_2alow1S626 = _M0L7_2abindS625.$0;
  _M0L8_2ahigh1S627 = _M0L7_2abindS625.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS628 = _M0FPB7umul128(_M0L1mS624, _M0L4mul0S622);
  _M0L7_2alow0S629 = _M0L7_2abindS628.$0;
  _M0L8_2ahigh0S630 = _M0L7_2abindS628.$1;
  _M0L3sumS631 = _M0L8_2ahigh0S630 + _M0L7_2alow1S626;
  _M0Lm5high1S632 = _M0L8_2ahigh1S627;
  if (_M0L3sumS631 < _M0L8_2ahigh0S630) {
    uint64_t _M0L6_2atmpS1743 = _M0Lm5high1S632;
    _M0Lm5high1S632 = _M0L6_2atmpS1743 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1754 = _M0FPB8pow5bits(_M0L5base2S620);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1755 = _M0FPB8pow5bits(_M0L1iS619);
  _M0L5deltaS633 = _M0L6_2atmpS1754 - _M0L6_2atmpS1755;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1753
  = _M0FPB13shiftright128(_M0L7_2alow0S629, _M0L3sumS631, _M0L5deltaS633);
  _M0L6_2atmpS1745 = _M0L6_2atmpS1753 + 1ull;
  _M0L6_2atmpS1752 = _M0L1iS619 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1749
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1752);
  _M0L6_2atmpS1751 = _M0L1iS619 % 16;
  _M0L6_2atmpS1750 = _M0L6_2atmpS1751 << 1;
  _M0L6_2atmpS1748 = _M0L6_2atmpS1749 >> (_M0L6_2atmpS1750 & 31);
  _M0L6_2atmpS1747 = _M0L6_2atmpS1748 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1746 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1747);
  _M0L1aS634 = _M0L6_2atmpS1745 + _M0L6_2atmpS1746;
  _M0L6_2atmpS1744 = _M0Lm5high1S632;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS635
  = _M0FPB13shiftright128(_M0L3sumS631, _M0L6_2atmpS1744, _M0L5deltaS633);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS634, .$1 = _M0L1bS635};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS601) {
  int32_t _M0L4baseS600;
  int32_t _M0L5base2S602;
  int32_t _M0L6offsetS603;
  int32_t _M0L6_2atmpS1742;
  uint64_t _M0L4mul0S604;
  int32_t _M0L6_2atmpS1741;
  int32_t _M0L6_2atmpS1740;
  uint64_t _M0L4mul1S605;
  uint64_t _M0L1mS606;
  struct _M0TPB7Umul128 _M0L7_2abindS607;
  uint64_t _M0L7_2alow1S608;
  uint64_t _M0L8_2ahigh1S609;
  struct _M0TPB7Umul128 _M0L7_2abindS610;
  uint64_t _M0L7_2alow0S611;
  uint64_t _M0L8_2ahigh0S612;
  uint64_t _M0L3sumS613;
  uint64_t _M0Lm5high1S614;
  int32_t _M0L6_2atmpS1738;
  int32_t _M0L6_2atmpS1739;
  int32_t _M0L5deltaS615;
  uint64_t _M0L6_2atmpS1730;
  int32_t _M0L6_2atmpS1737;
  uint32_t _M0L6_2atmpS1734;
  int32_t _M0L6_2atmpS1736;
  int32_t _M0L6_2atmpS1735;
  uint32_t _M0L6_2atmpS1733;
  uint32_t _M0L6_2atmpS1732;
  uint64_t _M0L6_2atmpS1731;
  uint64_t _M0L1aS616;
  uint64_t _M0L6_2atmpS1729;
  uint64_t _M0L1bS617;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS600 = _M0L1iS601 / 26;
  _M0L5base2S602 = _M0L4baseS600 * 26;
  _M0L6offsetS603 = _M0L1iS601 - _M0L5base2S602;
  _M0L6_2atmpS1742 = _M0L4baseS600 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S604
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1742);
  _M0L6_2atmpS1741 = _M0L4baseS600 * 2;
  _M0L6_2atmpS1740 = _M0L6_2atmpS1741 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S605
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1740);
  if (_M0L6offsetS603 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S604, .$1 = _M0L4mul1S605};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS606
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS603);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS607 = _M0FPB7umul128(_M0L1mS606, _M0L4mul1S605);
  _M0L7_2alow1S608 = _M0L7_2abindS607.$0;
  _M0L8_2ahigh1S609 = _M0L7_2abindS607.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS610 = _M0FPB7umul128(_M0L1mS606, _M0L4mul0S604);
  _M0L7_2alow0S611 = _M0L7_2abindS610.$0;
  _M0L8_2ahigh0S612 = _M0L7_2abindS610.$1;
  _M0L3sumS613 = _M0L8_2ahigh0S612 + _M0L7_2alow1S608;
  _M0Lm5high1S614 = _M0L8_2ahigh1S609;
  if (_M0L3sumS613 < _M0L8_2ahigh0S612) {
    uint64_t _M0L6_2atmpS1728 = _M0Lm5high1S614;
    _M0Lm5high1S614 = _M0L6_2atmpS1728 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1738 = _M0FPB8pow5bits(_M0L1iS601);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1739 = _M0FPB8pow5bits(_M0L5base2S602);
  _M0L5deltaS615 = _M0L6_2atmpS1738 - _M0L6_2atmpS1739;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1730
  = _M0FPB13shiftright128(_M0L7_2alow0S611, _M0L3sumS613, _M0L5deltaS615);
  _M0L6_2atmpS1737 = _M0L1iS601 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1734
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1737);
  _M0L6_2atmpS1736 = _M0L1iS601 % 16;
  _M0L6_2atmpS1735 = _M0L6_2atmpS1736 << 1;
  _M0L6_2atmpS1733 = _M0L6_2atmpS1734 >> (_M0L6_2atmpS1735 & 31);
  _M0L6_2atmpS1732 = _M0L6_2atmpS1733 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1731 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1732);
  _M0L1aS616 = _M0L6_2atmpS1730 + _M0L6_2atmpS1731;
  _M0L6_2atmpS1729 = _M0Lm5high1S614;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS617
  = _M0FPB13shiftright128(_M0L3sumS613, _M0L6_2atmpS1729, _M0L5deltaS615);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS616, .$1 = _M0L1bS617};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS574,
  struct _M0TPB8Pow5Pair _M0L3mulS571,
  int32_t _M0L1jS587,
  int32_t _M0L7mmShiftS589
) {
  uint64_t _M0L7_2amul0S570;
  uint64_t _M0L7_2amul1S572;
  uint64_t _M0L1mS573;
  struct _M0TPB7Umul128 _M0L7_2abindS575;
  uint64_t _M0L5_2aloS576;
  uint64_t _M0L6_2atmpS577;
  struct _M0TPB7Umul128 _M0L7_2abindS578;
  uint64_t _M0L6_2alo2S579;
  uint64_t _M0L6_2ahi2S580;
  uint64_t _M0L3midS581;
  uint64_t _M0L6_2atmpS1727;
  uint64_t _M0L2hiS582;
  uint64_t _M0L3lo2S583;
  uint64_t _M0L6_2atmpS1725;
  uint64_t _M0L6_2atmpS1726;
  uint64_t _M0L4mid2S584;
  uint64_t _M0L6_2atmpS1724;
  uint64_t _M0L3hi2S585;
  int32_t _M0L6_2atmpS1723;
  int32_t _M0L6_2atmpS1722;
  uint64_t _M0L2vpS586;
  uint64_t _M0Lm2vmS588;
  int32_t _M0L6_2atmpS1721;
  int32_t _M0L6_2atmpS1720;
  uint64_t _M0L2vrS599;
  uint64_t _M0L6_2atmpS1719;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S570 = _M0L3mulS571.$0;
  _M0L7_2amul1S572 = _M0L3mulS571.$1;
  _M0L1mS573 = _M0L1mS574 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS575 = _M0FPB7umul128(_M0L1mS573, _M0L7_2amul0S570);
  _M0L5_2aloS576 = _M0L7_2abindS575.$0;
  _M0L6_2atmpS577 = _M0L7_2abindS575.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS578 = _M0FPB7umul128(_M0L1mS573, _M0L7_2amul1S572);
  _M0L6_2alo2S579 = _M0L7_2abindS578.$0;
  _M0L6_2ahi2S580 = _M0L7_2abindS578.$1;
  _M0L3midS581 = _M0L6_2atmpS577 + _M0L6_2alo2S579;
  if (_M0L3midS581 < _M0L6_2atmpS577) {
    _M0L6_2atmpS1727 = 1ull;
  } else {
    _M0L6_2atmpS1727 = 0ull;
  }
  _M0L2hiS582 = _M0L6_2ahi2S580 + _M0L6_2atmpS1727;
  _M0L3lo2S583 = _M0L5_2aloS576 + _M0L7_2amul0S570;
  _M0L6_2atmpS1725 = _M0L3midS581 + _M0L7_2amul1S572;
  if (_M0L3lo2S583 < _M0L5_2aloS576) {
    _M0L6_2atmpS1726 = 1ull;
  } else {
    _M0L6_2atmpS1726 = 0ull;
  }
  _M0L4mid2S584 = _M0L6_2atmpS1725 + _M0L6_2atmpS1726;
  if (_M0L4mid2S584 < _M0L3midS581) {
    _M0L6_2atmpS1724 = 1ull;
  } else {
    _M0L6_2atmpS1724 = 0ull;
  }
  _M0L3hi2S585 = _M0L2hiS582 + _M0L6_2atmpS1724;
  _M0L6_2atmpS1723 = _M0L1jS587 - 64;
  _M0L6_2atmpS1722 = _M0L6_2atmpS1723 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS586
  = _M0FPB13shiftright128(_M0L4mid2S584, _M0L3hi2S585, _M0L6_2atmpS1722);
  _M0Lm2vmS588 = 0ull;
  if (_M0L7mmShiftS589) {
    uint64_t _M0L3lo3S590 = _M0L5_2aloS576 - _M0L7_2amul0S570;
    uint64_t _M0L6_2atmpS1709 = _M0L3midS581 - _M0L7_2amul1S572;
    uint64_t _M0L6_2atmpS1710;
    uint64_t _M0L4mid3S591;
    uint64_t _M0L6_2atmpS1708;
    uint64_t _M0L3hi3S592;
    int32_t _M0L6_2atmpS1707;
    int32_t _M0L6_2atmpS1706;
    if (_M0L5_2aloS576 < _M0L3lo3S590) {
      _M0L6_2atmpS1710 = 1ull;
    } else {
      _M0L6_2atmpS1710 = 0ull;
    }
    _M0L4mid3S591 = _M0L6_2atmpS1709 - _M0L6_2atmpS1710;
    if (_M0L3midS581 < _M0L4mid3S591) {
      _M0L6_2atmpS1708 = 1ull;
    } else {
      _M0L6_2atmpS1708 = 0ull;
    }
    _M0L3hi3S592 = _M0L2hiS582 - _M0L6_2atmpS1708;
    _M0L6_2atmpS1707 = _M0L1jS587 - 64;
    _M0L6_2atmpS1706 = _M0L6_2atmpS1707 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS588
    = _M0FPB13shiftright128(_M0L4mid3S591, _M0L3hi3S592, _M0L6_2atmpS1706);
  } else {
    uint64_t _M0L3lo3S593 = _M0L5_2aloS576 + _M0L5_2aloS576;
    uint64_t _M0L6_2atmpS1717 = _M0L3midS581 + _M0L3midS581;
    uint64_t _M0L6_2atmpS1718;
    uint64_t _M0L4mid3S594;
    uint64_t _M0L6_2atmpS1715;
    uint64_t _M0L6_2atmpS1716;
    uint64_t _M0L3hi3S595;
    uint64_t _M0L3lo4S596;
    uint64_t _M0L6_2atmpS1713;
    uint64_t _M0L6_2atmpS1714;
    uint64_t _M0L4mid4S597;
    uint64_t _M0L6_2atmpS1712;
    uint64_t _M0L3hi4S598;
    int32_t _M0L6_2atmpS1711;
    if (_M0L3lo3S593 < _M0L5_2aloS576) {
      _M0L6_2atmpS1718 = 1ull;
    } else {
      _M0L6_2atmpS1718 = 0ull;
    }
    _M0L4mid3S594 = _M0L6_2atmpS1717 + _M0L6_2atmpS1718;
    _M0L6_2atmpS1715 = _M0L2hiS582 + _M0L2hiS582;
    if (_M0L4mid3S594 < _M0L3midS581) {
      _M0L6_2atmpS1716 = 1ull;
    } else {
      _M0L6_2atmpS1716 = 0ull;
    }
    _M0L3hi3S595 = _M0L6_2atmpS1715 + _M0L6_2atmpS1716;
    _M0L3lo4S596 = _M0L3lo3S593 - _M0L7_2amul0S570;
    _M0L6_2atmpS1713 = _M0L4mid3S594 - _M0L7_2amul1S572;
    if (_M0L3lo3S593 < _M0L3lo4S596) {
      _M0L6_2atmpS1714 = 1ull;
    } else {
      _M0L6_2atmpS1714 = 0ull;
    }
    _M0L4mid4S597 = _M0L6_2atmpS1713 - _M0L6_2atmpS1714;
    if (_M0L4mid3S594 < _M0L4mid4S597) {
      _M0L6_2atmpS1712 = 1ull;
    } else {
      _M0L6_2atmpS1712 = 0ull;
    }
    _M0L3hi4S598 = _M0L3hi3S595 - _M0L6_2atmpS1712;
    _M0L6_2atmpS1711 = _M0L1jS587 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS588
    = _M0FPB13shiftright128(_M0L4mid4S597, _M0L3hi4S598, _M0L6_2atmpS1711);
  }
  _M0L6_2atmpS1721 = _M0L1jS587 - 64;
  _M0L6_2atmpS1720 = _M0L6_2atmpS1721 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS599
  = _M0FPB13shiftright128(_M0L3midS581, _M0L2hiS582, _M0L6_2atmpS1720);
  _M0L6_2atmpS1719 = _M0Lm2vmS588;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS599,
                                                .$1 = _M0L2vpS586,
                                                .$2 = _M0L6_2atmpS1719};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS568,
  int32_t _M0L1pS569
) {
  uint64_t _M0L6_2atmpS1705;
  uint64_t _M0L6_2atmpS1704;
  uint64_t _M0L6_2atmpS1703;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1705 = 1ull << (_M0L1pS569 & 63);
  _M0L6_2atmpS1704 = _M0L6_2atmpS1705 - 1ull;
  _M0L6_2atmpS1703 = _M0L5valueS568 & _M0L6_2atmpS1704;
  return _M0L6_2atmpS1703 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS566,
  int32_t _M0L1pS567
) {
  int32_t _M0L6_2atmpS1702;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1702 = _M0FPB10pow5Factor(_M0L5valueS566);
  return _M0L6_2atmpS1702 >= _M0L1pS567;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS561) {
  uint64_t _M0L6_2atmpS1693;
  uint64_t _M0L6_2atmpS1694;
  uint64_t _M0L6_2atmpS1695;
  uint64_t _M0L6_2atmpS1696;
  uint64_t _M0L6_2atmpS1701;
  int32_t _M0L5countS562;
  uint64_t _M0L1vS563;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1693 = _M0L5valueS561 % 5ull;
  if (_M0L6_2atmpS1693 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1694 = _M0L5valueS561 % 25ull;
  if (_M0L6_2atmpS1694 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1695 = _M0L5valueS561 % 125ull;
  if (_M0L6_2atmpS1695 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1696 = _M0L5valueS561 % 625ull;
  if (_M0L6_2atmpS1696 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1701 = _M0L5valueS561 / 625ull;
  _M0L5countS562 = 4;
  _M0L1vS563 = _M0L6_2atmpS1701;
  while (1) {
    if (_M0L1vS563 > 0ull) {
      uint64_t _M0L6_2atmpS1697 = _M0L1vS563 % 5ull;
      int32_t _M0L6_2atmpS1698;
      uint64_t _M0L6_2atmpS1699;
      if (_M0L6_2atmpS1697 != 0ull) {
        return _M0L5countS562;
      }
      _M0L6_2atmpS1698 = _M0L5countS562 + 1;
      _M0L6_2atmpS1699 = _M0L1vS563 / 5ull;
      _M0L5countS562 = _M0L6_2atmpS1698;
      _M0L1vS563 = _M0L6_2atmpS1699;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS565;
      moonbit_string_t _M0L6_2atmpS1700;
      int32_t _result_2595;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS565
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS565, (moonbit_string_t)moonbit_string_literal_14.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS565, _M0L5valueS561);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1700
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS565);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS565);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2595 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1700);
      moonbit_decref_cycle_free(_M0L6_2atmpS1700);
      return _result_2595;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS560,
  uint64_t _M0L2hiS558,
  int32_t _M0L4distS559
) {
  int32_t _M0L6_2atmpS1692;
  uint64_t _M0L6_2atmpS1690;
  uint64_t _M0L6_2atmpS1691;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1692 = 64 - _M0L4distS559;
  _M0L6_2atmpS1690 = _M0L2hiS558 << (_M0L6_2atmpS1692 & 63);
  _M0L6_2atmpS1691 = _M0L2loS560 >> (_M0L4distS559 & 63);
  return _M0L6_2atmpS1690 | _M0L6_2atmpS1691;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS548,
  uint64_t _M0L1bS551
) {
  uint64_t _M0L3aLoS547;
  uint64_t _M0L3aHiS549;
  uint64_t _M0L3bLoS550;
  uint64_t _M0L3bHiS552;
  uint64_t _M0L1xS553;
  uint64_t _M0L6_2atmpS1688;
  uint64_t _M0L6_2atmpS1689;
  uint64_t _M0L1yS554;
  uint64_t _M0L6_2atmpS1686;
  uint64_t _M0L6_2atmpS1687;
  uint64_t _M0L1zS555;
  uint64_t _M0L6_2atmpS1684;
  uint64_t _M0L6_2atmpS1685;
  uint64_t _M0L6_2atmpS1682;
  uint64_t _M0L6_2atmpS1683;
  uint64_t _M0L1wS556;
  uint64_t _M0L2loS557;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS547 = _M0L1aS548 & 4294967295ull;
  _M0L3aHiS549 = _M0L1aS548 >> 32;
  _M0L3bLoS550 = _M0L1bS551 & 4294967295ull;
  _M0L3bHiS552 = _M0L1bS551 >> 32;
  _M0L1xS553 = _M0L3aLoS547 * _M0L3bLoS550;
  _M0L6_2atmpS1688 = _M0L3aHiS549 * _M0L3bLoS550;
  _M0L6_2atmpS1689 = _M0L1xS553 >> 32;
  _M0L1yS554 = _M0L6_2atmpS1688 + _M0L6_2atmpS1689;
  _M0L6_2atmpS1686 = _M0L3aLoS547 * _M0L3bHiS552;
  _M0L6_2atmpS1687 = _M0L1yS554 & 4294967295ull;
  _M0L1zS555 = _M0L6_2atmpS1686 + _M0L6_2atmpS1687;
  _M0L6_2atmpS1684 = _M0L3aHiS549 * _M0L3bHiS552;
  _M0L6_2atmpS1685 = _M0L1yS554 >> 32;
  _M0L6_2atmpS1682 = _M0L6_2atmpS1684 + _M0L6_2atmpS1685;
  _M0L6_2atmpS1683 = _M0L1zS555 >> 32;
  _M0L1wS556 = _M0L6_2atmpS1682 + _M0L6_2atmpS1683;
  _M0L2loS557 = _M0L1aS548 * _M0L1bS551;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS557, .$1 = _M0L1wS556};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS545,
  int32_t _M0L4fromS542,
  int32_t _M0L2toS541
) {
  int32_t _M0L3lenS540;
  int32_t _M0L6_2atmpS1681;
  uint16_t* _M0L6bufferS543;
  int32_t _M0L1iS544;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS540 = _M0L2toS541 - _M0L4fromS542;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1681 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS543
  = (uint16_t*)moonbit_make_string(_M0L3lenS540, _M0L6_2atmpS1681);
  _M0L1iS544 = 0;
  while (1) {
    if (_M0L1iS544 < _M0L3lenS540) {
      int32_t _M0L6_2atmpS1679 = _M0L4fromS542 + _M0L1iS544;
      int32_t _M0L6_2atmpS1678;
      int32_t _M0L6_2atmpS1677;
      int32_t _M0L6_2atmpS1680;
      if (
        _M0L6_2atmpS1679 < 0
        || _M0L6_2atmpS1679 >= Moonbit_array_length(_M0L5bytesS545)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1678 = (int32_t)_M0L5bytesS545[_M0L6_2atmpS1679];
      _M0L6_2atmpS1677 = (uint16_t)_M0L6_2atmpS1678;
      if (
        _M0L1iS544 < 0 || _M0L1iS544 >= Moonbit_array_length(_M0L6bufferS543)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS543[_M0L1iS544] = _M0L6_2atmpS1677;
      _M0L6_2atmpS1680 = _M0L1iS544 + 1;
      _M0L1iS544 = _M0L6_2atmpS1680;
      continue;
    }
    break;
  }
  return _M0L6bufferS543;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS539) {
  int32_t _M0L6_2atmpS1676;
  uint32_t _M0L6_2atmpS1675;
  uint32_t _M0L6_2atmpS1674;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1676 = _M0L1eS539 * 78913;
  _M0L6_2atmpS1675 = *(uint32_t*)&_M0L6_2atmpS1676;
  _M0L6_2atmpS1674 = _M0L6_2atmpS1675 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1674;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS538) {
  int32_t _M0L6_2atmpS1673;
  uint32_t _M0L6_2atmpS1672;
  uint32_t _M0L6_2atmpS1671;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1673 = _M0L1eS538 * 732923;
  _M0L6_2atmpS1672 = *(uint32_t*)&_M0L6_2atmpS1673;
  _M0L6_2atmpS1671 = _M0L6_2atmpS1672 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1671;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS536,
  int32_t _M0L8exponentS537,
  int32_t _M0L8mantissaS534
) {
  moonbit_string_t _M0L1sS535;
  moonbit_string_t _result_2598;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS534) {
    return (moonbit_string_t)moonbit_string_literal_15.data;
  }
  if (_M0L4signS536) {
    _M0L1sS535 = (moonbit_string_t)moonbit_string_literal_16.data;
  } else {
    _M0L1sS535 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS537) {
    moonbit_string_t _result_2597;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2597
    = moonbit_add_string(_M0L1sS535, (moonbit_string_t)moonbit_string_literal_17.data);
    moonbit_decref_cycle_free(_M0L1sS535);
    return _result_2597;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2598
  = moonbit_add_string(_M0L1sS535, (moonbit_string_t)moonbit_string_literal_18.data);
  moonbit_decref_cycle_free(_M0L1sS535);
  return _result_2598;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS533) {
  int32_t _M0L6_2atmpS1670;
  uint32_t _M0L6_2atmpS1669;
  uint32_t _M0L6_2atmpS1668;
  int32_t _M0L6_2atmpS1667;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1670 = _M0L1eS533 * 1217359;
  _M0L6_2atmpS1669 = *(uint32_t*)&_M0L6_2atmpS1670;
  _M0L6_2atmpS1668 = _M0L6_2atmpS1669 >> 19;
  _M0L6_2atmpS1667 = *(int32_t*)&_M0L6_2atmpS1668;
  return _M0L6_2atmpS1667 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS532) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS532 != _M0L4selfS532) {
    return 0;
  } else if (_M0L4selfS532 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS532 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS532;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS531) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS531 != _M0L4selfS531) {
    return 0ll;
  } else if (_M0L4selfS531 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS531 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS531;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS528
) {
  float* _M0L6_2atmpS1664;
  struct _M0TPB5ArrayGfE* _block_2599;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1664 = (float*)moonbit_make_float_array_raw(_M0L3lenS528);
  _block_2599
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2599)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2599->$0 = _M0L6_2atmpS1664;
  _block_2599->$1 = _M0L3lenS528;
  return _block_2599;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS529
) {
  uint8_t* _M0L6_2atmpS1665;
  struct _M0TPB5ArrayGbE* _block_2600;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1665 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS529);
  _block_2600
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2600)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 71, 0);
  _block_2600->$0 = _M0L6_2atmpS1665;
  _block_2600->$1 = _M0L3lenS529;
  return _block_2600;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS530
) {
  int32_t* _M0L6_2atmpS1666;
  struct _M0TPB5ArrayGiE* _block_2601;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1666 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS530);
  _block_2601
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2601)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 62, 0);
  _block_2601->$0 = _M0L6_2atmpS1666;
  _block_2601->$1 = _M0L3lenS530;
  return _block_2601;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS524,
  int32_t _M0L5indexS525
) {
  uint64_t* _M0L6_2atmpS1662;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1662 = _M0L4selfS524;
  if (
    _M0L5indexS525 < 0
    || _M0L5indexS525 >= Moonbit_array_length(_M0L6_2atmpS1662)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1662[_M0L5indexS525];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS526,
  int32_t _M0L5indexS527
) {
  uint32_t* _M0L6_2atmpS1663;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1663 = _M0L4selfS526;
  if (
    _M0L5indexS527 < 0
    || _M0L5indexS527 >= Moonbit_array_length(_M0L6_2atmpS1663)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1663[_M0L5indexS527];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS523
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS523, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS522) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS522, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS521) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS521;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS509,
  int32_t _M0L5valueS511
) {
  int32_t _M0L3lenS1634;
  int32_t* _M0L6_2atmpS1636;
  int32_t _M0L6_2atmpS1635;
  int32_t _M0L6lengthS510;
  int32_t* _M0L3bufS1639;
  int32_t _M0L6_2atmpS1640;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1634 = _M0L4selfS509->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1636 = _M0MPC15array5Array6bufferGiE(_M0L4selfS509);
  _M0L6_2atmpS1635 = Moonbit_array_length(_M0L6_2atmpS1636);
  moonbit_decref_cycle_free(_M0L6_2atmpS1636);
  if (_M0L3lenS1634 == _M0L6_2atmpS1635) {
    int32_t _M0L3lenS1638 = _M0L4selfS509->$1;
    int32_t _M0L6_2atmpS1637 = _M0L3lenS1638 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS509, _M0L6_2atmpS1637);
  }
  _M0L6lengthS510 = _M0L4selfS509->$1;
  _M0L3bufS1639 = _M0L4selfS509->$0;
  _M0L3bufS1639[_M0L6lengthS510] = _M0L5valueS511;
  _M0L6_2atmpS1640 = _M0L6lengthS510 + 1;
  _M0L4selfS509->$1 = _M0L6_2atmpS1640;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS512,
  float _M0L5valueS514
) {
  int32_t _M0L3lenS1641;
  float* _M0L6_2atmpS1643;
  int32_t _M0L6_2atmpS1642;
  int32_t _M0L6lengthS513;
  float* _M0L3bufS1646;
  int32_t _M0L6_2atmpS1647;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1641 = _M0L4selfS512->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1643 = _M0MPC15array5Array6bufferGfE(_M0L4selfS512);
  _M0L6_2atmpS1642 = Moonbit_array_length(_M0L6_2atmpS1643);
  moonbit_decref_cycle_free(_M0L6_2atmpS1643);
  if (_M0L3lenS1641 == _M0L6_2atmpS1642) {
    int32_t _M0L3lenS1645 = _M0L4selfS512->$1;
    int32_t _M0L6_2atmpS1644 = _M0L3lenS1645 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS512, _M0L6_2atmpS1644);
  }
  _M0L6lengthS513 = _M0L4selfS512->$1;
  _M0L3bufS1646 = _M0L4selfS512->$0;
  _M0L3bufS1646[_M0L6lengthS513] = _M0L5valueS514;
  _M0L6_2atmpS1647 = _M0L6lengthS513 + 1;
  _M0L4selfS512->$1 = _M0L6_2atmpS1647;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS515,
  moonbit_string_t _M0L5valueS517
) {
  int32_t _M0L3lenS1648;
  moonbit_string_t* _M0L6_2atmpS1650;
  int32_t _M0L6_2atmpS1649;
  int32_t _M0L6lengthS516;
  moonbit_string_t* _M0L3bufS1653;
  moonbit_string_t _M0L6_2aoldS2489;
  int32_t _M0L6_2atmpS1654;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1648 = _M0L4selfS515->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1650 = _M0MPC15array5Array6bufferGsE(_M0L4selfS515);
  _M0L6_2atmpS1649 = Moonbit_array_length(_M0L6_2atmpS1650);
  moonbit_decref_cycle_free(_M0L6_2atmpS1650);
  if (_M0L3lenS1648 == _M0L6_2atmpS1649) {
    int32_t _M0L3lenS1652 = _M0L4selfS515->$1;
    int32_t _M0L6_2atmpS1651 = _M0L3lenS1652 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS515, _M0L6_2atmpS1651);
  }
  _M0L6lengthS516 = _M0L4selfS515->$1;
  _M0L3bufS1653 = _M0L4selfS515->$0;
  _M0L6_2aoldS2489 = (moonbit_string_t)_M0L3bufS1653[_M0L6lengthS516];
  moonbit_decref_cycle_free(_M0L6_2aoldS2489);
  _M0L3bufS1653[_M0L6lengthS516] = _M0L5valueS517;
  _M0L6_2atmpS1654 = _M0L6lengthS516 + 1;
  _M0L4selfS515->$1 = _M0L6_2atmpS1654;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS518,
  struct _M0TUsiE* _M0L5valueS520
) {
  int32_t _M0L3lenS1655;
  struct _M0TUsiE** _M0L6_2atmpS1657;
  int32_t _M0L6_2atmpS1656;
  int32_t _M0L6lengthS519;
  struct _M0TUsiE** _M0L3bufS1660;
  struct _M0TUsiE* _M0L6_2aoldS2490;
  int32_t _M0L6_2atmpS1661;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1655 = _M0L4selfS518->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1657 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS518);
  _M0L6_2atmpS1656 = Moonbit_array_length(_M0L6_2atmpS1657);
  moonbit_decref_cycle_free(_M0L6_2atmpS1657);
  if (_M0L3lenS1655 == _M0L6_2atmpS1656) {
    int32_t _M0L3lenS1659 = _M0L4selfS518->$1;
    int32_t _M0L6_2atmpS1658 = _M0L3lenS1659 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS518, _M0L6_2atmpS1658);
  }
  _M0L6lengthS519 = _M0L4selfS518->$1;
  _M0L3bufS1660 = _M0L4selfS518->$0;
  _M0L6_2aoldS2490 = (struct _M0TUsiE*)_M0L3bufS1660[_M0L6lengthS519];
  if (_M0L6_2aoldS2490) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2490);
  }
  _M0L3bufS1660[_M0L6lengthS519] = _M0L5valueS520;
  _M0L6_2atmpS1661 = _M0L6lengthS519 + 1;
  _M0L4selfS518->$1 = _M0L6_2atmpS1661;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS494,
  int32_t _M0L8requiredS496
) {
  int32_t _M0L8old__capS493;
  int32_t _M0L3lenS1630;
  int32_t _M0L8new__capS495;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS493 = _M0MPC15array5Array8capacityGiE(_M0L4selfS494);
  _M0L3lenS1630 = _M0L4selfS494->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS495
  = _M0FPB23array__growth__capacity(_M0L8old__capS493, _M0L3lenS1630, _M0L8requiredS496);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS494, _M0L8new__capS495);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS498,
  int32_t _M0L8requiredS500
) {
  int32_t _M0L8old__capS497;
  int32_t _M0L3lenS1631;
  int32_t _M0L8new__capS499;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS497 = _M0MPC15array5Array8capacityGfE(_M0L4selfS498);
  _M0L3lenS1631 = _M0L4selfS498->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS499
  = _M0FPB23array__growth__capacity(_M0L8old__capS497, _M0L3lenS1631, _M0L8requiredS500);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS498, _M0L8new__capS499);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS502,
  int32_t _M0L8requiredS504
) {
  int32_t _M0L8old__capS501;
  int32_t _M0L3lenS1632;
  int32_t _M0L8new__capS503;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS501 = _M0MPC15array5Array8capacityGsE(_M0L4selfS502);
  _M0L3lenS1632 = _M0L4selfS502->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS503
  = _M0FPB23array__growth__capacity(_M0L8old__capS501, _M0L3lenS1632, _M0L8requiredS504);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS502, _M0L8new__capS503);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS506,
  int32_t _M0L8requiredS508
) {
  int32_t _M0L8old__capS505;
  int32_t _M0L3lenS1633;
  int32_t _M0L8new__capS507;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS505 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS506);
  _M0L3lenS1633 = _M0L4selfS506->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS507
  = _M0FPB23array__growth__capacity(_M0L8old__capS505, _M0L3lenS1633, _M0L8requiredS508);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS506, _M0L8new__capS507);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS470,
  int32_t _M0L13new__capacityS473
) {
  int32_t* _M0L8old__bufS469;
  int32_t _M0L3lenS471;
  int32_t _M0L9copy__lenS472;
  int32_t* _M0L8new__bufS474;
  int32_t* _M0L6_2aoldS2491;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS469 = _M0L4selfS470->$0;
  _M0L3lenS471 = _M0L4selfS470->$1;
  if (_M0L3lenS471 < _M0L13new__capacityS473) {
    _M0L9copy__lenS472 = _M0L3lenS471;
  } else {
    _M0L9copy__lenS472 = _M0L13new__capacityS473;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS469);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS474
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS469, _M0L13new__capacityS473, _M0L9copy__lenS472, 0, 0);
  _M0L6_2aoldS2491 = _M0L4selfS470->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2491);
  _M0L4selfS470->$0 = _M0L8new__bufS474;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS476,
  int32_t _M0L13new__capacityS479
) {
  float* _M0L8old__bufS475;
  int32_t _M0L3lenS477;
  int32_t _M0L9copy__lenS478;
  float* _M0L8new__bufS480;
  float* _M0L6_2aoldS2492;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS475 = _M0L4selfS476->$0;
  _M0L3lenS477 = _M0L4selfS476->$1;
  if (_M0L3lenS477 < _M0L13new__capacityS479) {
    _M0L9copy__lenS478 = _M0L3lenS477;
  } else {
    _M0L9copy__lenS478 = _M0L13new__capacityS479;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS475);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS480
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS475, _M0L13new__capacityS479, _M0L9copy__lenS478, 0, 0);
  _M0L6_2aoldS2492 = _M0L4selfS476->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2492);
  _M0L4selfS476->$0 = _M0L8new__bufS480;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS482,
  int32_t _M0L13new__capacityS485
) {
  moonbit_string_t* _M0L8old__bufS481;
  int32_t _M0L3lenS483;
  int32_t _M0L9copy__lenS484;
  moonbit_string_t* _M0L8new__bufS486;
  moonbit_string_t* _M0L6_2aoldS2493;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS481 = _M0L4selfS482->$0;
  _M0L3lenS483 = _M0L4selfS482->$1;
  if (_M0L3lenS483 < _M0L13new__capacityS485) {
    _M0L9copy__lenS484 = _M0L3lenS483;
  } else {
    _M0L9copy__lenS484 = _M0L13new__capacityS485;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS481);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS486
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS481, _M0L13new__capacityS485, _M0L9copy__lenS484, 0, 0);
  _M0L6_2aoldS2493 = _M0L4selfS482->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2493);
  _M0L4selfS482->$0 = _M0L8new__bufS486;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS488,
  int32_t _M0L13new__capacityS491
) {
  struct _M0TUsiE** _M0L8old__bufS487;
  int32_t _M0L3lenS489;
  int32_t _M0L9copy__lenS490;
  struct _M0TUsiE** _M0L8new__bufS492;
  struct _M0TUsiE** _M0L6_2aoldS2494;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS487 = _M0L4selfS488->$0;
  _M0L3lenS489 = _M0L4selfS488->$1;
  if (_M0L3lenS489 < _M0L13new__capacityS491) {
    _M0L9copy__lenS490 = _M0L3lenS489;
  } else {
    _M0L9copy__lenS490 = _M0L13new__capacityS491;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS487);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS492
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS487, _M0L13new__capacityS491, _M0L9copy__lenS490, 0, 0);
  _M0L6_2aoldS2494 = _M0L4selfS488->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2494);
  _M0L4selfS488->$0 = _M0L8new__bufS492;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS465
) {
  int32_t* _M0L6_2atmpS1626;
  int32_t _result_2602;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1626 = _M0MPC15array5Array6bufferGiE(_M0L4selfS465);
  _result_2602 = Moonbit_array_length(_M0L6_2atmpS1626);
  moonbit_decref_cycle_free(_M0L6_2atmpS1626);
  return _result_2602;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS466
) {
  float* _M0L6_2atmpS1627;
  int32_t _result_2603;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1627 = _M0MPC15array5Array6bufferGfE(_M0L4selfS466);
  _result_2603 = Moonbit_array_length(_M0L6_2atmpS1627);
  moonbit_decref_cycle_free(_M0L6_2atmpS1627);
  return _result_2603;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS467
) {
  moonbit_string_t* _M0L6_2atmpS1628;
  int32_t _result_2604;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1628 = _M0MPC15array5Array6bufferGsE(_M0L4selfS467);
  _result_2604 = Moonbit_array_length(_M0L6_2atmpS1628);
  moonbit_decref_cycle_free(_M0L6_2atmpS1628);
  return _result_2604;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS468
) {
  struct _M0TUsiE** _M0L6_2atmpS1629;
  int32_t _result_2605;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1629 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS468);
  _result_2605 = Moonbit_array_length(_M0L6_2atmpS1629);
  moonbit_decref_cycle_free(_M0L6_2atmpS1629);
  return _result_2605;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS461,
  int32_t _M0L3lenS459,
  int32_t _M0L8requiredS458
) {
  int32_t _M0L5startS460;
  int32_t _M0L5spaceS462;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS458 < _M0L3lenS459) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_19.data);
  }
  if (_M0L7currentS461 == 0) {
    _M0L5startS460 = 8;
  } else {
    _M0L5startS460 = _M0L7currentS461;
  }
  _M0L5spaceS462 = _M0L5startS460;
  while (1) {
    if (_M0L5spaceS462 < _M0L8requiredS458) {
      int32_t _M0L4nextS463 = _M0L5spaceS462 * 2;
      if (_M0L4nextS463 <= _M0L5spaceS462) {
        return _M0L8requiredS458;
      }
      _M0L5spaceS462 = _M0L4nextS463;
      continue;
    } else {
      return _M0L5spaceS462;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS457) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS457->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS452) {
  float* _M0L8_2afieldS2495;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2495 = _M0L4selfS452->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2495);
  return _M0L8_2afieldS2495;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS453) {
  int32_t* _M0L8_2afieldS2496;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2496 = _M0L4selfS453->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2496);
  return _M0L8_2afieldS2496;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS454) {
  uint8_t* _M0L8_2afieldS2497;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2497 = _M0L4selfS454->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2497);
  return _M0L8_2afieldS2497;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS455
) {
  moonbit_string_t* _M0L8_2afieldS2498;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2498 = _M0L4selfS455->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2498);
  return _M0L8_2afieldS2498;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS456
) {
  struct _M0TUsiE** _M0L8_2afieldS2499;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2499 = _M0L4selfS456->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2499);
  return _M0L8_2afieldS2499;
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
  int32_t _M0L3endS1624;
  int32_t _M0L5startS1625;
  int32_t _M0L8str__lenS447;
  int32_t _M0L3lenS1623;
  int32_t _M0L8requiredS449;
  uint16_t* _M0L4dataS1616;
  int32_t _M0L6_2atmpS1615;
  int32_t _if__result_2607;
  uint16_t* _M0L4dataS1617;
  int32_t _M0L3lenS1618;
  moonbit_string_t _M0L6_2atmpS1619;
  int32_t _M0L6_2atmpS1620;
  int32_t _M0L3lenS1622;
  int32_t _M0L6_2atmpS1621;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1624 = _M0L3strS448.$2;
  _M0L5startS1625 = _M0L3strS448.$1;
  _M0L8str__lenS447 = _M0L3endS1624 - _M0L5startS1625;
  if (_M0L8str__lenS447 == 0) {
    return 0;
  }
  _M0L3lenS1623 = _M0L4selfS450->$1;
  _M0L8requiredS449 = _M0L3lenS1623 + _M0L8str__lenS447;
  _M0L4dataS1616 = _M0L4selfS450->$0;
  _M0L6_2atmpS1615 = Moonbit_array_length(_M0L4dataS1616);
  if (_M0L8requiredS449 > _M0L6_2atmpS1615) {
    _if__result_2607 = 1;
  } else {
    int32_t _M0L3lenS1614 = _M0L4selfS450->$1;
    _if__result_2607 = _M0L8requiredS449 < _M0L3lenS1614;
  }
  if (_if__result_2607) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS450, _M0L8requiredS449);
  }
  _M0L4dataS1617 = _M0L4selfS450->$0;
  _M0L3lenS1618 = _M0L4selfS450->$1;
  moonbit_incref_cycle_free(_M0L4dataS1617);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1619 = _M0MPC16string10StringView4data(_M0L3strS448);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1620 = _M0MPC16string10StringView13start__offset(_M0L3strS448);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1617, _M0L3lenS1618, _M0L6_2atmpS1619, _M0L6_2atmpS1620, _M0L8str__lenS447);
  moonbit_decref_cycle_free(_M0L4dataS1617);
  moonbit_decref_cycle_free(_M0L6_2atmpS1619);
  _M0L3lenS1622 = _M0L4selfS450->$1;
  _M0L6_2atmpS1621 = _M0L3lenS1622 + _M0L8str__lenS447;
  _M0L4selfS450->$1 = _M0L6_2atmpS1621;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS444,
  int32_t _M0L5startS442,
  int32_t _M0L3endS443
) {
  int32_t _if__result_2608;
  int32_t _M0L3lenS445;
  int32_t _M0L6_2atmpS1613;
  moonbit_bytes_t _M0L5bytesS446;
  moonbit_bytes_t _M0L6_2atmpS1612;
  moonbit_string_t _result_2609;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS442 == 0) {
    int32_t _M0L6_2atmpS1611 = Moonbit_array_length(_M0L3strS444);
    _if__result_2608 = _M0L3endS443 == _M0L6_2atmpS1611;
  } else {
    _if__result_2608 = 0;
  }
  if (_if__result_2608) {
    moonbit_incref_cycle_free(_M0L3strS444);
    return _M0L3strS444;
  }
  _M0L3lenS445 = _M0L3endS443 - _M0L5startS442;
  _M0L6_2atmpS1613 = _M0L3lenS445 * 2;
  _M0L5bytesS446 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1613, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS446, 0, _M0L3strS444, _M0L5startS442, _M0L3lenS445);
  _M0L6_2atmpS1612 = _M0L5bytesS446;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2609
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1612, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1612);
  return _result_2609;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS437,
  int32_t _M0L6offsetS441,
  int64_t _M0L6lengthS439
) {
  int32_t _M0L3lenS436;
  int32_t _M0L6lengthS438;
  int32_t _if__result_2610;
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
      int32_t _M0L6_2atmpS1610 = _M0L6offsetS441 + _M0L6lengthS438;
      _if__result_2610 = _M0L6_2atmpS1610 <= _M0L3lenS436;
    } else {
      _if__result_2610 = 0;
    }
  } else {
    _if__result_2610 = 0;
  }
  if (_if__result_2610) {
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
  int32_t _M0L6_2atmpS1609;
  int32_t _M0L6_2atmpS1608;
  int32_t _M0L2e1S422;
  int32_t _M0L6_2atmpS1607;
  int32_t _M0L2e2S425;
  int32_t _M0L4len1S427;
  int32_t _M0L4len2S429;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1609 = _M0L6lengthS424 * 2;
  _M0L6_2atmpS1608 = _M0L13bytes__offsetS423 + _M0L6_2atmpS1609;
  _M0L2e1S422 = _M0L6_2atmpS1608 - 1;
  _M0L6_2atmpS1607 = _M0L11str__offsetS426 + _M0L6lengthS424;
  _M0L2e2S425 = _M0L6_2atmpS1607 - 1;
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
        int32_t _M0L6_2atmpS1604 = _M0L3strS430[_M0L1iS432];
        int32_t _M0L6_2atmpS1603 = (int32_t)_M0L6_2atmpS1604;
        uint32_t _M0L1cS434 = *(uint32_t*)&_M0L6_2atmpS1603;
        uint32_t _M0L6_2atmpS1599 = _M0L1cS434 & 255u;
        int32_t _M0L6_2atmpS1598;
        int32_t _M0L6_2atmpS1600;
        uint32_t _M0L6_2atmpS1602;
        int32_t _M0L6_2atmpS1601;
        int32_t _M0L6_2atmpS1605;
        int32_t _M0L6_2atmpS1606;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1598 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1599);
        if (
          _M0L1jS433 < 0 || _M0L1jS433 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L1jS433] = _M0L6_2atmpS1598;
        _M0L6_2atmpS1600 = _M0L1jS433 + 1;
        _M0L6_2atmpS1602 = _M0L1cS434 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1601 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1602);
        if (
          _M0L6_2atmpS1600 < 0
          || _M0L6_2atmpS1600 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L6_2atmpS1600] = _M0L6_2atmpS1601;
        _M0L6_2atmpS1605 = _M0L1iS432 + 1;
        _M0L6_2atmpS1606 = _M0L1jS433 + 2;
        _M0L1iS432 = _M0L6_2atmpS1605;
        _M0L1jS433 = _M0L6_2atmpS1606;
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
  int32_t _M0L6_2atmpS1597;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1597 = *(int32_t*)&_M0L4selfS421;
  return _M0L6_2atmpS1597 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS413,
  int32_t _M0L5radixS412
) {
  uint16_t* _M0L6bufferS414;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS412 < 2 || _M0L5radixS412 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_20.data);
  }
  if (_M0L4selfS413 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_13.data;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_20.data);
  }
  if (_M0L4selfS396 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_13.data;
  }
  _M0L12is__negativeS397 = _M0L4selfS396 < 0ll;
  if (_M0L12is__negativeS397) {
    int64_t _M0L6_2atmpS1596 = -_M0L4selfS396;
    _M0L3numS398 = *(uint64_t*)&_M0L6_2atmpS1596;
  } else {
    _M0L3numS398 = *(uint64_t*)&_M0L4selfS396;
  }
  switch (_M0L5radixS395) {
    case 10: {
      int32_t _M0L10digit__lenS400;
      int32_t _M0L6_2atmpS1593;
      int32_t _M0L10total__lenS401;
      uint16_t* _M0L6bufferS402;
      int32_t _M0L12digit__startS403;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS400 = _M0FPB12dec__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1593 = 1;
      } else {
        _M0L6_2atmpS1593 = 0;
      }
      _M0L10total__lenS401 = _M0L10digit__lenS400 + _M0L6_2atmpS1593;
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
      int32_t _M0L6_2atmpS1594;
      int32_t _M0L10total__lenS405;
      uint16_t* _M0L6bufferS406;
      int32_t _M0L12digit__startS407;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS404 = _M0FPB12hex__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1594 = 1;
      } else {
        _M0L6_2atmpS1594 = 0;
      }
      _M0L10total__lenS405 = _M0L10digit__lenS404 + _M0L6_2atmpS1594;
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
      int32_t _M0L6_2atmpS1595;
      int32_t _M0L10total__lenS409;
      uint16_t* _M0L6bufferS410;
      int32_t _M0L12digit__startS411;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS408
      = _M0FPB14radix__count64(_M0L3numS398, _M0L5radixS395);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1595 = 1;
      } else {
        _M0L6_2atmpS1595 = 0;
      }
      _M0L10total__lenS409 = _M0L10digit__lenS408 + _M0L6_2atmpS1595;
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
  int32_t _M0L6_2atmpS1592;
  uint64_t _M0L3numS371;
  int32_t _M0L6offsetS372;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1592 = _M0L10total__lenS394 - _M0L12digit__startS382;
  _M0L3numS371 = _M0L3numS393;
  _M0L6offsetS372 = _M0L6_2atmpS1592;
  while (1) {
    if (_M0L3numS371 >= 10000ull) {
      uint64_t _M0L1tS373 = _M0L3numS371 / 10000ull;
      uint64_t _M0L6_2atmpS1569 = _M0L3numS371 % 10000ull;
      int32_t _M0L1rS374 = (int32_t)_M0L6_2atmpS1569;
      int32_t _M0L2d1S375 = _M0L1rS374 / 100;
      int32_t _M0L2d2S376 = _M0L1rS374 % 100;
      int32_t _M0L6_2atmpS1568 = _M0L2d1S375 / 10;
      int32_t _M0L6_2atmpS1567 = 48 + _M0L6_2atmpS1568;
      int32_t _M0L6d1__hiS377 = (uint16_t)_M0L6_2atmpS1567;
      int32_t _M0L6_2atmpS1566 = _M0L2d1S375 % 10;
      int32_t _M0L6_2atmpS1565 = 48 + _M0L6_2atmpS1566;
      int32_t _M0L6d1__loS378 = (uint16_t)_M0L6_2atmpS1565;
      int32_t _M0L6_2atmpS1564 = _M0L2d2S376 / 10;
      int32_t _M0L6_2atmpS1563 = 48 + _M0L6_2atmpS1564;
      int32_t _M0L6d2__hiS379 = (uint16_t)_M0L6_2atmpS1563;
      int32_t _M0L6_2atmpS1562 = _M0L2d2S376 % 10;
      int32_t _M0L6_2atmpS1561 = 48 + _M0L6_2atmpS1562;
      int32_t _M0L6d2__loS380 = (uint16_t)_M0L6_2atmpS1561;
      int32_t _M0L6_2atmpS1553 = _M0L12digit__startS382 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS1552 = _M0L6_2atmpS1553 - 4;
      int32_t _M0L6_2atmpS1555;
      int32_t _M0L6_2atmpS1554;
      int32_t _M0L6_2atmpS1557;
      int32_t _M0L6_2atmpS1556;
      int32_t _M0L6_2atmpS1559;
      int32_t _M0L6_2atmpS1558;
      int32_t _M0L6_2atmpS1560;
      _M0L6bufferS381[_M0L6_2atmpS1552] = _M0L6d1__hiS377;
      _M0L6_2atmpS1555 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1554 = _M0L6_2atmpS1555 - 3;
      _M0L6bufferS381[_M0L6_2atmpS1554] = _M0L6d1__loS378;
      _M0L6_2atmpS1557 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1556 = _M0L6_2atmpS1557 - 2;
      _M0L6bufferS381[_M0L6_2atmpS1556] = _M0L6d2__hiS379;
      _M0L6_2atmpS1559 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1558 = _M0L6_2atmpS1559 - 1;
      _M0L6bufferS381[_M0L6_2atmpS1558] = _M0L6d2__loS380;
      _M0L6_2atmpS1560 = _M0L6offsetS372 - 4;
      _M0L3numS371 = _M0L1tS373;
      _M0L6offsetS372 = _M0L6_2atmpS1560;
      continue;
    } else {
      int32_t _M0L6_2atmpS1591 = (int32_t)_M0L3numS371;
      int32_t _M0L9remainingS384 = _M0L6_2atmpS1591;
      int32_t _M0L6offsetS385 = _M0L6offsetS372;
      while (1) {
        if (_M0L9remainingS384 >= 100) {
          int32_t _M0L1tS386 = _M0L9remainingS384 / 100;
          int32_t _M0L1dS387 = _M0L9remainingS384 % 100;
          int32_t _M0L6_2atmpS1578 = _M0L1dS387 / 10;
          int32_t _M0L6_2atmpS1577 = 48 + _M0L6_2atmpS1578;
          int32_t _M0L5d__hiS388 = (uint16_t)_M0L6_2atmpS1577;
          int32_t _M0L6_2atmpS1576 = _M0L1dS387 % 10;
          int32_t _M0L6_2atmpS1575 = 48 + _M0L6_2atmpS1576;
          int32_t _M0L5d__loS389 = (uint16_t)_M0L6_2atmpS1575;
          int32_t _M0L6_2atmpS1571 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1570 = _M0L6_2atmpS1571 - 2;
          int32_t _M0L6_2atmpS1573;
          int32_t _M0L6_2atmpS1572;
          int32_t _M0L6_2atmpS1574;
          _M0L6bufferS381[_M0L6_2atmpS1570] = _M0L5d__hiS388;
          _M0L6_2atmpS1573 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1572 = _M0L6_2atmpS1573 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1572] = _M0L5d__loS389;
          _M0L6_2atmpS1574 = _M0L6offsetS385 - 2;
          _M0L9remainingS384 = _M0L1tS386;
          _M0L6offsetS385 = _M0L6_2atmpS1574;
          continue;
        } else if (_M0L9remainingS384 >= 10) {
          int32_t _M0L6_2atmpS1586 = _M0L9remainingS384 / 10;
          int32_t _M0L6_2atmpS1585 = 48 + _M0L6_2atmpS1586;
          int32_t _M0L5d__hiS391 = (uint16_t)_M0L6_2atmpS1585;
          int32_t _M0L6_2atmpS1584 = _M0L9remainingS384 % 10;
          int32_t _M0L6_2atmpS1583 = 48 + _M0L6_2atmpS1584;
          int32_t _M0L5d__loS392 = (uint16_t)_M0L6_2atmpS1583;
          int32_t _M0L6_2atmpS1580 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1579 = _M0L6_2atmpS1580 - 2;
          int32_t _M0L6_2atmpS1582;
          int32_t _M0L6_2atmpS1581;
          _M0L6bufferS381[_M0L6_2atmpS1579] = _M0L5d__hiS391;
          _M0L6_2atmpS1582 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1581 = _M0L6_2atmpS1582 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1581] = _M0L5d__loS392;
        } else {
          int32_t _M0L6_2atmpS1590 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1587 = _M0L6_2atmpS1590 - 1;
          int32_t _M0L6_2atmpS1589 = 48 + _M0L9remainingS384;
          int32_t _M0L6_2atmpS1588 = (uint16_t)_M0L6_2atmpS1589;
          _M0L6bufferS381[_M0L6_2atmpS1587] = _M0L6_2atmpS1588;
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
  int32_t _M0L6_2atmpS1537;
  int32_t _M0L6_2atmpS1536;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS354 = _M0MPC13int3Int10to__uint64(_M0L5radixS355);
  _M0L6_2atmpS1537 = _M0L5radixS355 - 1;
  _M0L6_2atmpS1536 = _M0L5radixS355 & _M0L6_2atmpS1537;
  if (_M0L6_2atmpS1536 == 0) {
    int32_t _M0L5shiftS356;
    uint64_t _M0L4maskS357;
    int32_t _M0L6_2atmpS1544;
    int32_t _M0L6offsetS358;
    uint64_t _M0L1nS359;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS356 = moonbit_ctz32(_M0L5radixS355);
    _M0L4maskS357 = _M0L4baseS354 - 1ull;
    _M0L6_2atmpS1544 = _M0L10total__lenS364 - _M0L12digit__startS362;
    _M0L6offsetS358 = _M0L6_2atmpS1544;
    _M0L1nS359 = _M0L3numS365;
    while (1) {
      if (_M0L1nS359 > 0ull) {
        uint64_t _M0L6_2atmpS1543 = _M0L1nS359 & _M0L4maskS357;
        int32_t _M0L5digitS360 = (int32_t)_M0L6_2atmpS1543;
        int32_t _M0L6_2atmpS1540 = _M0L12digit__startS362 + _M0L6offsetS358;
        int32_t _M0L6_2atmpS1538 = _M0L6_2atmpS1540 - 1;
        int32_t _M0L6_2atmpS1539 =
          ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L5digitS360];
        int32_t _M0L6_2atmpS1541;
        uint64_t _M0L6_2atmpS1542;
        _M0L6bufferS361[_M0L6_2atmpS1538] = _M0L6_2atmpS1539;
        _M0L6_2atmpS1541 = _M0L6offsetS358 - 1;
        _M0L6_2atmpS1542 = _M0L1nS359 >> (_M0L5shiftS356 & 63);
        _M0L6offsetS358 = _M0L6_2atmpS1541;
        _M0L1nS359 = _M0L6_2atmpS1542;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1551 = _M0L10total__lenS364 - _M0L12digit__startS362;
    int32_t _M0L6offsetS366 = _M0L6_2atmpS1551;
    uint64_t _M0L1nS367 = _M0L3numS365;
    while (1) {
      if (_M0L1nS367 > 0ull) {
        uint64_t _M0L1qS368 = _M0L1nS367 / _M0L4baseS354;
        uint64_t _M0L6_2atmpS1550 = _M0L1qS368 * _M0L4baseS354;
        uint64_t _M0L6_2atmpS1549 = _M0L1nS367 - _M0L6_2atmpS1550;
        int32_t _M0L5digitS369 = (int32_t)_M0L6_2atmpS1549;
        int32_t _M0L6_2atmpS1547 = _M0L12digit__startS362 + _M0L6offsetS366;
        int32_t _M0L6_2atmpS1545 = _M0L6_2atmpS1547 - 1;
        int32_t _M0L6_2atmpS1546 =
          ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L5digitS369];
        int32_t _M0L6_2atmpS1548;
        _M0L6bufferS361[_M0L6_2atmpS1545] = _M0L6_2atmpS1546;
        _M0L6_2atmpS1548 = _M0L6offsetS366 - 1;
        _M0L6offsetS366 = _M0L6_2atmpS1548;
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
  int32_t _M0L6_2atmpS1535;
  int32_t _M0L6offsetS343;
  uint64_t _M0L1nS344;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1535 = _M0L10total__lenS352 - _M0L12digit__startS349;
  _M0L6offsetS343 = _M0L6_2atmpS1535;
  _M0L1nS344 = _M0L3numS353;
  while (1) {
    if (_M0L6offsetS343 >= 2) {
      uint64_t _M0L6_2atmpS1532 = _M0L1nS344 & 255ull;
      int32_t _M0L9byte__valS345 = (int32_t)_M0L6_2atmpS1532;
      int32_t _M0L2hiS346 = _M0L9byte__valS345 / 16;
      int32_t _M0L2loS347 = _M0L9byte__valS345 % 16;
      int32_t _M0L6_2atmpS1526 = _M0L12digit__startS349 + _M0L6offsetS343;
      int32_t _M0L6_2atmpS1524 = _M0L6_2atmpS1526 - 2;
      int32_t _M0L6_2atmpS1525 =
        ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L2hiS346];
      int32_t _M0L6_2atmpS1529;
      int32_t _M0L6_2atmpS1527;
      int32_t _M0L6_2atmpS1528;
      int32_t _M0L6_2atmpS1530;
      uint64_t _M0L6_2atmpS1531;
      _M0L6bufferS348[_M0L6_2atmpS1524] = _M0L6_2atmpS1525;
      _M0L6_2atmpS1529 = _M0L12digit__startS349 + _M0L6offsetS343;
      _M0L6_2atmpS1527 = _M0L6_2atmpS1529 - 1;
      _M0L6_2atmpS1528
      = ((moonbit_string_t)moonbit_string_literal_21.data)[
        _M0L2loS347
      ];
      _M0L6bufferS348[_M0L6_2atmpS1527] = _M0L6_2atmpS1528;
      _M0L6_2atmpS1530 = _M0L6offsetS343 - 2;
      _M0L6_2atmpS1531 = _M0L1nS344 >> 8;
      _M0L6offsetS343 = _M0L6_2atmpS1530;
      _M0L1nS344 = _M0L6_2atmpS1531;
      continue;
    } else if (_M0L6offsetS343 == 1) {
      uint64_t _M0L6_2atmpS1534 = _M0L1nS344 & 15ull;
      int32_t _M0L6nibbleS351 = (int32_t)_M0L6_2atmpS1534;
      int32_t _M0L6_2atmpS1533 =
        ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L6nibbleS351];
      _M0L6bufferS348[_M0L12digit__startS349] = _M0L6_2atmpS1533;
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
      uint64_t _M0L6_2atmpS1522 = _M0L3numS340 / _M0L4baseS338;
      int32_t _M0L6_2atmpS1523 = _M0L5countS341 + 1;
      _M0L3numS340 = _M0L6_2atmpS1522;
      _M0L5countS341 = _M0L6_2atmpS1523;
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
    int32_t _M0L6_2atmpS1521;
    int32_t _M0L6_2atmpS1520;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS336 = moonbit_clz64(_M0L5valueS335);
    _M0L6_2atmpS1521 = 63 - _M0L14leading__zerosS336;
    _M0L6_2atmpS1520 = _M0L6_2atmpS1521 / 4;
    return _M0L6_2atmpS1520 + 1;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_20.data);
  }
  if (_M0L4selfS318 == 0) {
    return (moonbit_string_t)moonbit_string_literal_13.data;
  }
  _M0L12is__negativeS319 = _M0L4selfS318 < 0;
  if (_M0L12is__negativeS319) {
    int32_t _M0L6_2atmpS1519 = -_M0L4selfS318;
    _M0L3numS320 = *(uint32_t*)&_M0L6_2atmpS1519;
  } else {
    _M0L3numS320 = *(uint32_t*)&_M0L4selfS318;
  }
  switch (_M0L5radixS317) {
    case 10: {
      int32_t _M0L10digit__lenS322;
      int32_t _M0L6_2atmpS1516;
      int32_t _M0L10total__lenS323;
      uint16_t* _M0L6bufferS324;
      int32_t _M0L12digit__startS325;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS322 = _M0FPB12dec__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1516 = 1;
      } else {
        _M0L6_2atmpS1516 = 0;
      }
      _M0L10total__lenS323 = _M0L10digit__lenS322 + _M0L6_2atmpS1516;
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
      int32_t _M0L6_2atmpS1517;
      int32_t _M0L10total__lenS327;
      uint16_t* _M0L6bufferS328;
      int32_t _M0L12digit__startS329;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS326 = _M0FPB12hex__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1517 = 1;
      } else {
        _M0L6_2atmpS1517 = 0;
      }
      _M0L10total__lenS327 = _M0L10digit__lenS326 + _M0L6_2atmpS1517;
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
      int32_t _M0L6_2atmpS1518;
      int32_t _M0L10total__lenS331;
      uint16_t* _M0L6bufferS332;
      int32_t _M0L12digit__startS333;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS330
      = _M0FPB14radix__count32(_M0L3numS320, _M0L5radixS317);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1518 = 1;
      } else {
        _M0L6_2atmpS1518 = 0;
      }
      _M0L10total__lenS331 = _M0L10digit__lenS330 + _M0L6_2atmpS1518;
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
      uint32_t _M0L6_2atmpS1514 = _M0L3numS314 / _M0L4baseS312;
      int32_t _M0L6_2atmpS1515 = _M0L5countS315 + 1;
      _M0L3numS314 = _M0L6_2atmpS1514;
      _M0L5countS315 = _M0L6_2atmpS1515;
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
    int32_t _M0L6_2atmpS1513;
    int32_t _M0L6_2atmpS1512;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS310 = moonbit_clz32(_M0L5valueS309);
    _M0L6_2atmpS1513 = 31 - _M0L14leading__zerosS310;
    _M0L6_2atmpS1512 = _M0L6_2atmpS1513 / 4;
    return _M0L6_2atmpS1512 + 1;
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
  int32_t _M0L6_2atmpS1511;
  uint32_t _M0L3numS284;
  int32_t _M0L6offsetS285;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1511 = _M0L10total__lenS307 - _M0L12digit__startS295;
  _M0L3numS284 = _M0L3numS306;
  _M0L6offsetS285 = _M0L6_2atmpS1511;
  while (1) {
    if (_M0L3numS284 >= 10000u) {
      uint32_t _M0L1tS286 = _M0L3numS284 / 10000u;
      uint32_t _M0L6_2atmpS1488 = _M0L3numS284 % 10000u;
      int32_t _M0L1rS287 = *(int32_t*)&_M0L6_2atmpS1488;
      int32_t _M0L2d1S288 = _M0L1rS287 / 100;
      int32_t _M0L2d2S289 = _M0L1rS287 % 100;
      int32_t _M0L6_2atmpS1487 = _M0L2d1S288 / 10;
      int32_t _M0L6_2atmpS1486 = 48 + _M0L6_2atmpS1487;
      int32_t _M0L6d1__hiS290 = (uint16_t)_M0L6_2atmpS1486;
      int32_t _M0L6_2atmpS1485 = _M0L2d1S288 % 10;
      int32_t _M0L6_2atmpS1484 = 48 + _M0L6_2atmpS1485;
      int32_t _M0L6d1__loS291 = (uint16_t)_M0L6_2atmpS1484;
      int32_t _M0L6_2atmpS1483 = _M0L2d2S289 / 10;
      int32_t _M0L6_2atmpS1482 = 48 + _M0L6_2atmpS1483;
      int32_t _M0L6d2__hiS292 = (uint16_t)_M0L6_2atmpS1482;
      int32_t _M0L6_2atmpS1481 = _M0L2d2S289 % 10;
      int32_t _M0L6_2atmpS1480 = 48 + _M0L6_2atmpS1481;
      int32_t _M0L6d2__loS293 = (uint16_t)_M0L6_2atmpS1480;
      int32_t _M0L6_2atmpS1472 = _M0L12digit__startS295 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS1471 = _M0L6_2atmpS1472 - 4;
      int32_t _M0L6_2atmpS1474;
      int32_t _M0L6_2atmpS1473;
      int32_t _M0L6_2atmpS1476;
      int32_t _M0L6_2atmpS1475;
      int32_t _M0L6_2atmpS1478;
      int32_t _M0L6_2atmpS1477;
      int32_t _M0L6_2atmpS1479;
      _M0L6bufferS294[_M0L6_2atmpS1471] = _M0L6d1__hiS290;
      _M0L6_2atmpS1474 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1473 = _M0L6_2atmpS1474 - 3;
      _M0L6bufferS294[_M0L6_2atmpS1473] = _M0L6d1__loS291;
      _M0L6_2atmpS1476 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1475 = _M0L6_2atmpS1476 - 2;
      _M0L6bufferS294[_M0L6_2atmpS1475] = _M0L6d2__hiS292;
      _M0L6_2atmpS1478 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1477 = _M0L6_2atmpS1478 - 1;
      _M0L6bufferS294[_M0L6_2atmpS1477] = _M0L6d2__loS293;
      _M0L6_2atmpS1479 = _M0L6offsetS285 - 4;
      _M0L3numS284 = _M0L1tS286;
      _M0L6offsetS285 = _M0L6_2atmpS1479;
      continue;
    } else {
      int32_t _M0L6_2atmpS1510 = *(int32_t*)&_M0L3numS284;
      int32_t _M0L9remainingS297 = _M0L6_2atmpS1510;
      int32_t _M0L6offsetS298 = _M0L6offsetS285;
      while (1) {
        if (_M0L9remainingS297 >= 100) {
          int32_t _M0L1tS299 = _M0L9remainingS297 / 100;
          int32_t _M0L1dS300 = _M0L9remainingS297 % 100;
          int32_t _M0L6_2atmpS1497 = _M0L1dS300 / 10;
          int32_t _M0L6_2atmpS1496 = 48 + _M0L6_2atmpS1497;
          int32_t _M0L5d__hiS301 = (uint16_t)_M0L6_2atmpS1496;
          int32_t _M0L6_2atmpS1495 = _M0L1dS300 % 10;
          int32_t _M0L6_2atmpS1494 = 48 + _M0L6_2atmpS1495;
          int32_t _M0L5d__loS302 = (uint16_t)_M0L6_2atmpS1494;
          int32_t _M0L6_2atmpS1490 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1489 = _M0L6_2atmpS1490 - 2;
          int32_t _M0L6_2atmpS1492;
          int32_t _M0L6_2atmpS1491;
          int32_t _M0L6_2atmpS1493;
          _M0L6bufferS294[_M0L6_2atmpS1489] = _M0L5d__hiS301;
          _M0L6_2atmpS1492 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1491 = _M0L6_2atmpS1492 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1491] = _M0L5d__loS302;
          _M0L6_2atmpS1493 = _M0L6offsetS298 - 2;
          _M0L9remainingS297 = _M0L1tS299;
          _M0L6offsetS298 = _M0L6_2atmpS1493;
          continue;
        } else if (_M0L9remainingS297 >= 10) {
          int32_t _M0L6_2atmpS1505 = _M0L9remainingS297 / 10;
          int32_t _M0L6_2atmpS1504 = 48 + _M0L6_2atmpS1505;
          int32_t _M0L5d__hiS304 = (uint16_t)_M0L6_2atmpS1504;
          int32_t _M0L6_2atmpS1503 = _M0L9remainingS297 % 10;
          int32_t _M0L6_2atmpS1502 = 48 + _M0L6_2atmpS1503;
          int32_t _M0L5d__loS305 = (uint16_t)_M0L6_2atmpS1502;
          int32_t _M0L6_2atmpS1499 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1498 = _M0L6_2atmpS1499 - 2;
          int32_t _M0L6_2atmpS1501;
          int32_t _M0L6_2atmpS1500;
          _M0L6bufferS294[_M0L6_2atmpS1498] = _M0L5d__hiS304;
          _M0L6_2atmpS1501 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1500 = _M0L6_2atmpS1501 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1500] = _M0L5d__loS305;
        } else {
          int32_t _M0L6_2atmpS1509 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1506 = _M0L6_2atmpS1509 - 1;
          int32_t _M0L6_2atmpS1508 = 48 + _M0L9remainingS297;
          int32_t _M0L6_2atmpS1507 = (uint16_t)_M0L6_2atmpS1508;
          _M0L6bufferS294[_M0L6_2atmpS1506] = _M0L6_2atmpS1507;
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
  int32_t _M0L6_2atmpS1456;
  int32_t _M0L6_2atmpS1455;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS267 = *(uint32_t*)&_M0L5radixS268;
  _M0L6_2atmpS1456 = _M0L5radixS268 - 1;
  _M0L6_2atmpS1455 = _M0L5radixS268 & _M0L6_2atmpS1456;
  if (_M0L6_2atmpS1455 == 0) {
    int32_t _M0L5shiftS269;
    uint32_t _M0L4maskS270;
    int32_t _M0L6_2atmpS1463;
    int32_t _M0L6offsetS271;
    uint32_t _M0L1nS272;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS269 = moonbit_ctz32(_M0L5radixS268);
    _M0L4maskS270 = _M0L4baseS267 - 1u;
    _M0L6_2atmpS1463 = _M0L10total__lenS277 - _M0L12digit__startS275;
    _M0L6offsetS271 = _M0L6_2atmpS1463;
    _M0L1nS272 = _M0L3numS278;
    while (1) {
      if (_M0L1nS272 > 0u) {
        uint32_t _M0L6_2atmpS1462 = _M0L1nS272 & _M0L4maskS270;
        int32_t _M0L5digitS273 = *(int32_t*)&_M0L6_2atmpS1462;
        int32_t _M0L6_2atmpS1459 = _M0L12digit__startS275 + _M0L6offsetS271;
        int32_t _M0L6_2atmpS1457 = _M0L6_2atmpS1459 - 1;
        int32_t _M0L6_2atmpS1458 =
          ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L5digitS273];
        int32_t _M0L6_2atmpS1460;
        uint32_t _M0L6_2atmpS1461;
        _M0L6bufferS274[_M0L6_2atmpS1457] = _M0L6_2atmpS1458;
        _M0L6_2atmpS1460 = _M0L6offsetS271 - 1;
        _M0L6_2atmpS1461 = _M0L1nS272 >> (_M0L5shiftS269 & 31);
        _M0L6offsetS271 = _M0L6_2atmpS1460;
        _M0L1nS272 = _M0L6_2atmpS1461;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1470 = _M0L10total__lenS277 - _M0L12digit__startS275;
    int32_t _M0L6offsetS279 = _M0L6_2atmpS1470;
    uint32_t _M0L1nS280 = _M0L3numS278;
    while (1) {
      if (_M0L1nS280 > 0u) {
        uint32_t _M0L1qS281 = _M0L1nS280 / _M0L4baseS267;
        uint32_t _M0L6_2atmpS1469 = _M0L1qS281 * _M0L4baseS267;
        uint32_t _M0L6_2atmpS1468 = _M0L1nS280 - _M0L6_2atmpS1469;
        int32_t _M0L5digitS282 = *(int32_t*)&_M0L6_2atmpS1468;
        int32_t _M0L6_2atmpS1466 = _M0L12digit__startS275 + _M0L6offsetS279;
        int32_t _M0L6_2atmpS1464 = _M0L6_2atmpS1466 - 1;
        int32_t _M0L6_2atmpS1465 =
          ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L5digitS282];
        int32_t _M0L6_2atmpS1467;
        _M0L6bufferS274[_M0L6_2atmpS1464] = _M0L6_2atmpS1465;
        _M0L6_2atmpS1467 = _M0L6offsetS279 - 1;
        _M0L6offsetS279 = _M0L6_2atmpS1467;
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
  int32_t _M0L6_2atmpS1454;
  int32_t _M0L6offsetS256;
  uint32_t _M0L1nS257;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1454 = _M0L10total__lenS265 - _M0L12digit__startS262;
  _M0L6offsetS256 = _M0L6_2atmpS1454;
  _M0L1nS257 = _M0L3numS266;
  while (1) {
    if (_M0L6offsetS256 >= 2) {
      uint32_t _M0L6_2atmpS1451 = _M0L1nS257 & 255u;
      int32_t _M0L9byte__valS258 = *(int32_t*)&_M0L6_2atmpS1451;
      int32_t _M0L2hiS259 = _M0L9byte__valS258 / 16;
      int32_t _M0L2loS260 = _M0L9byte__valS258 % 16;
      int32_t _M0L6_2atmpS1445 = _M0L12digit__startS262 + _M0L6offsetS256;
      int32_t _M0L6_2atmpS1443 = _M0L6_2atmpS1445 - 2;
      int32_t _M0L6_2atmpS1444 =
        ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L2hiS259];
      int32_t _M0L6_2atmpS1448;
      int32_t _M0L6_2atmpS1446;
      int32_t _M0L6_2atmpS1447;
      int32_t _M0L6_2atmpS1449;
      uint32_t _M0L6_2atmpS1450;
      _M0L6bufferS261[_M0L6_2atmpS1443] = _M0L6_2atmpS1444;
      _M0L6_2atmpS1448 = _M0L12digit__startS262 + _M0L6offsetS256;
      _M0L6_2atmpS1446 = _M0L6_2atmpS1448 - 1;
      _M0L6_2atmpS1447
      = ((moonbit_string_t)moonbit_string_literal_21.data)[
        _M0L2loS260
      ];
      _M0L6bufferS261[_M0L6_2atmpS1446] = _M0L6_2atmpS1447;
      _M0L6_2atmpS1449 = _M0L6offsetS256 - 2;
      _M0L6_2atmpS1450 = _M0L1nS257 >> 8;
      _M0L6offsetS256 = _M0L6_2atmpS1449;
      _M0L1nS257 = _M0L6_2atmpS1450;
      continue;
    } else if (_M0L6offsetS256 == 1) {
      uint32_t _M0L6_2atmpS1453 = _M0L1nS257 & 15u;
      int32_t _M0L6nibbleS264 = *(int32_t*)&_M0L6_2atmpS1453;
      int32_t _M0L6_2atmpS1452 =
        ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L6nibbleS264];
      _M0L6bufferS261[_M0L12digit__startS262] = _M0L6_2atmpS1452;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS255
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS254;
  struct _M0TPB6Logger _M0L6_2atmpS1442;
  moonbit_string_t _result_2624;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS254);
  _M0L6_2atmpS1442
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS254
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS255, _M0L6_2atmpS1442);
  if (_M0L6_2atmpS1442.$1) {
    moonbit_decref(_M0L6_2atmpS1442.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2624 = _M0MPB13StringBuilder10to__string(_M0L6loggerS254);
  moonbit_decref_cycle_free(_M0L6loggerS254);
  return _result_2624;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS249,
  struct _M0TPB6Logger _M0L6loggerS248
) {
  moonbit_string_t _M0L6_2atmpS1439;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1439 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS249);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248.$0->$method_0(_M0L6loggerS248.$1, _M0L6_2atmpS1439);
  moonbit_decref_cycle_free(_M0L6_2atmpS1439);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS1440;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1440 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS1440);
  moonbit_decref_cycle_free(_M0L6_2atmpS1440);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS1441;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1441 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS1441);
  moonbit_decref_cycle_free(_M0L6_2atmpS1441);
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
  moonbit_string_t _M0L8_2afieldS2500;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2500 = _M0L4selfS246.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2500);
  return _M0L8_2afieldS2500;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS242,
  moonbit_string_t _M0L5valueS243,
  int32_t _M0L5startS244,
  int32_t _M0L3lenS245
) {
  int32_t _M0L6_2atmpS1438;
  int64_t _M0L6_2atmpS1437;
  struct _M0TPC16string10StringView _M0L6_2atmpS1436;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1438 = _M0L5startS244 + _M0L3lenS245;
  _M0L6_2atmpS1437 = (int64_t)_M0L6_2atmpS1438;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1436
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS243, _M0L5startS244, _M0L6_2atmpS1437);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS242, _M0L6_2atmpS1436);
  moonbit_decref_cycle_free(_M0L6_2atmpS1436.$0);
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
  int32_t _M0L6_2atmpS1420;
  int32_t _if__result_2625;
  int32_t _M0L6_2atmpS1428;
  int32_t _if__result_2626;
  int32_t _M0L6_2atmpS1430;
  int32_t _M0L6_2atmpS1431;
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
  _M0L6_2atmpS1420 = _M0Lm2loS236;
  if (_M0L6_2atmpS1420 > 0) {
    int32_t _M0L6_2atmpS1419 = _M0Lm2loS236;
    if (_M0L6_2atmpS1419 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1418 = _M0Lm2loS236;
      int32_t _M0L6_2atmpS1417 = _M0L4selfS235[_M0L6_2atmpS1418];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1417)) {
        int32_t _M0L6_2atmpS1416 = _M0Lm2loS236;
        int32_t _M0L6_2atmpS1415 = _M0L6_2atmpS1416 - 1;
        int32_t _M0L6_2atmpS1414 = _M0L4selfS235[_M0L6_2atmpS1415];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2625
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1414);
      } else {
        _if__result_2625 = 0;
      }
    } else {
      _if__result_2625 = 0;
    }
  } else {
    _if__result_2625 = 0;
  }
  if (_if__result_2625) {
    int32_t _M0L6_2atmpS1421 = _M0Lm2loS236;
    _M0Lm2loS236 = _M0L6_2atmpS1421 + 1;
  }
  _M0L6_2atmpS1428 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1428 > 0) {
    int32_t _M0L6_2atmpS1427 = _M0Lm2hiS238;
    if (_M0L6_2atmpS1427 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1426 = _M0Lm2hiS238;
      int32_t _M0L6_2atmpS1425 = _M0L4selfS235[_M0L6_2atmpS1426];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1425)) {
        int32_t _M0L6_2atmpS1424 = _M0Lm2hiS238;
        int32_t _M0L6_2atmpS1423 = _M0L6_2atmpS1424 - 1;
        int32_t _M0L6_2atmpS1422 = _M0L4selfS235[_M0L6_2atmpS1423];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2626
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1422);
      } else {
        _if__result_2626 = 0;
      }
    } else {
      _if__result_2626 = 0;
    }
  } else {
    _if__result_2626 = 0;
  }
  if (_if__result_2626) {
    int32_t _M0L6_2atmpS1429 = _M0Lm2hiS238;
    _M0Lm2hiS238 = _M0L6_2atmpS1429 - 1;
  }
  _M0L6_2atmpS1430 = _M0Lm2loS236;
  _M0L6_2atmpS1431 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1430 >= _M0L6_2atmpS1431) {
    int32_t _M0L6_2atmpS1432 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1433 = _M0Lm2loS236;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1432,
                                                 .$2 = _M0L6_2atmpS1433};
  } else {
    int32_t _M0L6_2atmpS1434 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1435 = _M0Lm2hiS238;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1434,
                                                 .$2 = _M0L6_2atmpS1435};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS1413;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS1413
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS1413);
  if (_M0L6_2atmpS1413.$1) {
    moonbit_decref(_M0L6_2atmpS1413.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS231,
  struct _M0TPB4Show _M0L4showS230
) {
  struct _M0TPB6Logger _M0L6_2atmpS1412;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS231);
  _M0L6_2atmpS1412
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS231
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS230.$0->$method_0(_M0L4showS230.$1, _M0L6_2atmpS1412);
  if (_M0L6_2atmpS1412.$1) {
    moonbit_decref(_M0L6_2atmpS1412.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS229) {
  int64_t _M0L6_2atmpS1411;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1411 = (int64_t)_M0L4selfS229;
  return *(uint64_t*)&_M0L6_2atmpS1411;
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
  int32_t _M0L6_2atmpS1410;
  struct _M0TPC16string10StringView _M0L6_2atmpS1408;
  struct _M0TPB6Logger _M0L6_2atmpS1409;
  moonbit_string_t _result_2627;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1410 = Moonbit_array_length(_M0L4selfS227);
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS1408
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS227, .$1 = 0, .$2 = _M0L6_2atmpS1410
  };
  moonbit_incref_cycle_free(_M0L3bufS226);
  _M0L6_2atmpS1409
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS226
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1408, _M0L6_2atmpS1409, _M0L5quoteS228);
  moonbit_decref_cycle_free(_M0L6_2atmpS1408.$0);
  if (_M0L6_2atmpS1409.$1) {
    moonbit_decref(_M0L6_2atmpS1409.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2627 = _M0MPB13StringBuilder10to__string(_M0L3bufS226);
  moonbit_decref_cycle_free(_M0L3bufS226);
  return _result_2627;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS218,
  struct _M0TPB6Logger _M0L6loggerS216,
  int32_t _M0L5quoteS215
) {
  int32_t _M0L3endS1406;
  int32_t _M0L5startS1407;
  int32_t _M0L3lenS217;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS219;
  int32_t _M0L1iS220;
  int32_t _M0L3segS221;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS215) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  _M0L3endS1406 = _M0L4selfS218.$2;
  _M0L5startS1407 = _M0L4selfS218.$1;
  _M0L3lenS217 = _M0L3endS1406 - _M0L5startS1407;
  moonbit_incref_cycle_free(_M0L4selfS218.$0);
  if (_M0L6loggerS216.$1) {
    moonbit_incref(_M0L6loggerS216.$1);
  }
  _M0L6_2aenvS219
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 84, 0);
  _M0L6_2aenvS219->$0 = _M0L4selfS218;
  _M0L6_2aenvS219->$1 = _M0L6loggerS216;
  _M0L1iS220 = 0;
  _M0L3segS221 = 0;
  _2afor_222:;
  while (1) {
    moonbit_string_t _M0L3strS1403;
    int32_t _M0L5startS1405;
    int32_t _M0L6_2atmpS1404;
    int32_t _M0L4codeS223;
    int32_t _M0L1cS225;
    int32_t _M0L6_2atmpS1387;
    int32_t _M0L6_2atmpS1388;
    int32_t _M0L6_2atmpS1389;
    if (_M0L1iS220 >= _M0L3lenS217) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
      moonbit_decref_cycle_free(_M0L6_2aenvS219);
      break;
    }
    _M0L3strS1403 = _M0L4selfS218.$0;
    _M0L5startS1405 = _M0L4selfS218.$1;
    _M0L6_2atmpS1404 = _M0L5startS1405 + _M0L1iS220;
    _M0L4codeS223 = _M0L3strS1403[_M0L6_2atmpS1404];
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
        int32_t _M0L6_2atmpS1390;
        int32_t _M0L6_2atmpS1391;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_22.data);
        _M0L6_2atmpS1390 = _M0L1iS220 + 1;
        _M0L6_2atmpS1391 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1390;
        _M0L3segS221 = _M0L6_2atmpS1391;
        goto _2afor_222;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1392;
        int32_t _M0L6_2atmpS1393;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_23.data);
        _M0L6_2atmpS1392 = _M0L1iS220 + 1;
        _M0L6_2atmpS1393 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1392;
        _M0L3segS221 = _M0L6_2atmpS1393;
        goto _2afor_222;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1394;
        int32_t _M0L6_2atmpS1395;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_24.data);
        _M0L6_2atmpS1394 = _M0L1iS220 + 1;
        _M0L6_2atmpS1395 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1394;
        _M0L3segS221 = _M0L6_2atmpS1395;
        goto _2afor_222;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1396;
        int32_t _M0L6_2atmpS1397;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_25.data);
        _M0L6_2atmpS1396 = _M0L1iS220 + 1;
        _M0L6_2atmpS1397 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1396;
        _M0L3segS221 = _M0L6_2atmpS1397;
        goto _2afor_222;
        break;
      }
      default: {
        if (_M0L4codeS223 < 32) {
          int32_t _M0L6_2atmpS1399;
          moonbit_string_t _M0L6_2atmpS1398;
          int32_t _M0L6_2atmpS1400;
          int32_t _M0L6_2atmpS1401;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_26.data);
          _M0L6_2atmpS1399 = _M0L4codeS223 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1398 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1399);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, _M0L6_2atmpS1398);
          moonbit_decref_cycle_free(_M0L6_2atmpS1398);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1400 = _M0L1iS220 + 1;
          _M0L6_2atmpS1401 = _M0L1iS220 + 1;
          _M0L1iS220 = _M0L6_2atmpS1400;
          _M0L3segS221 = _M0L6_2atmpS1401;
          goto _2afor_222;
        } else {
          int32_t _M0L6_2atmpS1402 = _M0L1iS220 + 1;
          int32_t _tmp_2630 = _M0L3segS221;
          _M0L1iS220 = _M0L6_2atmpS1402;
          _M0L3segS221 = _tmp_2630;
          goto _2afor_222;
        }
        break;
      }
    }
    goto joinlet_2629;
    join_224:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1387 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS225);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, _M0L6_2atmpS1387);
    _M0L6_2atmpS1388 = _M0L1iS220 + 1;
    _M0L6_2atmpS1389 = _M0L1iS220 + 1;
    _M0L1iS220 = _M0L6_2atmpS1388;
    _M0L3segS221 = _M0L6_2atmpS1389;
    continue;
    joinlet_2629:;
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
    int64_t _M0L6_2atmpS1386 = (int64_t)_M0L1iS213;
    struct _M0TPC16string10StringView _M0L6_2atmpS1385;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1385
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS212, _M0L3segS214, _M0L6_2atmpS1386);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS210.$0->$method_2(_M0L6loggerS210.$1, _M0L6_2atmpS1385);
    moonbit_decref_cycle_free(_M0L6_2atmpS1385.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS201,
  int32_t _M0L5startS203,
  int64_t _M0L3endS205
) {
  int32_t _M0L3endS1383;
  int32_t _M0L5startS1384;
  int32_t _M0L3lenS200;
  int32_t _M0Lm2loS202;
  int32_t _M0Lm2hiS204;
  moonbit_string_t _M0L3strS208;
  int32_t _M0L4baseS209;
  int32_t _M0L6_2atmpS1361;
  int32_t _if__result_2631;
  int32_t _M0L6_2atmpS1371;
  int32_t _if__result_2632;
  int32_t _M0L6_2atmpS1373;
  int32_t _M0L6_2atmpS1374;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1383 = _M0L4selfS201.$2;
  _M0L5startS1384 = _M0L4selfS201.$1;
  _M0L3lenS200 = _M0L3endS1383 - _M0L5startS1384;
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
  _M0L6_2atmpS1361 = _M0Lm2loS202;
  if (_M0L6_2atmpS1361 > 0) {
    int32_t _M0L6_2atmpS1360 = _M0Lm2loS202;
    if (_M0L6_2atmpS1360 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1359 = _M0Lm2loS202;
      int32_t _M0L6_2atmpS1358 = _M0L4baseS209 + _M0L6_2atmpS1359;
      int32_t _M0L6_2atmpS1357 = _M0L3strS208[_M0L6_2atmpS1358];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1357)) {
        int32_t _M0L6_2atmpS1356 = _M0Lm2loS202;
        int32_t _M0L6_2atmpS1355 = _M0L4baseS209 + _M0L6_2atmpS1356;
        int32_t _M0L6_2atmpS1354 = _M0L6_2atmpS1355 - 1;
        int32_t _M0L6_2atmpS1353 = _M0L3strS208[_M0L6_2atmpS1354];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2631
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1353);
      } else {
        _if__result_2631 = 0;
      }
    } else {
      _if__result_2631 = 0;
    }
  } else {
    _if__result_2631 = 0;
  }
  if (_if__result_2631) {
    int32_t _M0L6_2atmpS1362 = _M0Lm2loS202;
    _M0Lm2loS202 = _M0L6_2atmpS1362 + 1;
  }
  _M0L6_2atmpS1371 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1371 > 0) {
    int32_t _M0L6_2atmpS1370 = _M0Lm2hiS204;
    if (_M0L6_2atmpS1370 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1369 = _M0Lm2hiS204;
      int32_t _M0L6_2atmpS1368 = _M0L4baseS209 + _M0L6_2atmpS1369;
      int32_t _M0L6_2atmpS1367 = _M0L3strS208[_M0L6_2atmpS1368];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1367)) {
        int32_t _M0L6_2atmpS1366 = _M0Lm2hiS204;
        int32_t _M0L6_2atmpS1365 = _M0L4baseS209 + _M0L6_2atmpS1366;
        int32_t _M0L6_2atmpS1364 = _M0L6_2atmpS1365 - 1;
        int32_t _M0L6_2atmpS1363 = _M0L3strS208[_M0L6_2atmpS1364];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2632
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1363);
      } else {
        _if__result_2632 = 0;
      }
    } else {
      _if__result_2632 = 0;
    }
  } else {
    _if__result_2632 = 0;
  }
  if (_if__result_2632) {
    int32_t _M0L6_2atmpS1372 = _M0Lm2hiS204;
    _M0Lm2hiS204 = _M0L6_2atmpS1372 - 1;
  }
  _M0L6_2atmpS1373 = _M0Lm2loS202;
  _M0L6_2atmpS1374 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1373 >= _M0L6_2atmpS1374) {
    int32_t _M0L6_2atmpS1378 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1375 = _M0L4baseS209 + _M0L6_2atmpS1378;
    int32_t _M0L6_2atmpS1377 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1376 = _M0L4baseS209 + _M0L6_2atmpS1377;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1375,
                                                 .$2 = _M0L6_2atmpS1376};
  } else {
    int32_t _M0L6_2atmpS1382 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1379 = _M0L4baseS209 + _M0L6_2atmpS1382;
    int32_t _M0L6_2atmpS1381 = _M0Lm2hiS204;
    int32_t _M0L6_2atmpS1380 = _M0L4baseS209 + _M0L6_2atmpS1381;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1379,
                                                 .$2 = _M0L6_2atmpS1380};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS199) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS198;
  int32_t _M0L6_2atmpS1350;
  int32_t _M0L6_2atmpS1349;
  int32_t _M0L6_2atmpS1352;
  int32_t _M0L6_2atmpS1351;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1348;
  moonbit_string_t _result_2633;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1350 = _M0IPC14byte4BytePB3Div3div(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1349
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1350);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1349);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1352 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1351
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1352);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1351);
  _M0L6_2atmpS1348 = _M0L7_2aselfS198;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2633 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1348);
  moonbit_decref_cycle_free(_M0L6_2atmpS1348);
  return _result_2633;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS197) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS197 < 10) {
    int32_t _M0L6_2atmpS1345;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1345 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1345);
  } else {
    int32_t _M0L6_2atmpS1347;
    int32_t _M0L6_2atmpS1346;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1347 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1346 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1347, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1346);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS1343;
  int32_t _M0L6_2atmpS1344;
  int32_t _M0L6_2atmpS1342;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1343 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS1344 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS1342 = _M0L6_2atmpS1343 - _M0L6_2atmpS1344;
  return _M0L6_2atmpS1342 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS1340;
  int32_t _M0L6_2atmpS1341;
  int32_t _M0L6_2atmpS1339;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1340 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS1341 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS1339 = _M0L6_2atmpS1340 % _M0L6_2atmpS1341;
  return _M0L6_2atmpS1339 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS1337;
  int32_t _M0L6_2atmpS1338;
  int32_t _M0L6_2atmpS1336;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1337 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS1338 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS1336 = _M0L6_2atmpS1337 / _M0L6_2atmpS1338;
  return _M0L6_2atmpS1336 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS189,
  int32_t _M0L4thatS190
) {
  int32_t _M0L6_2atmpS1334;
  int32_t _M0L6_2atmpS1335;
  int32_t _M0L6_2atmpS1333;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1334 = (int32_t)_M0L4selfS189;
  _M0L6_2atmpS1335 = (int32_t)_M0L4thatS190;
  _M0L6_2atmpS1333 = _M0L6_2atmpS1334 + _M0L6_2atmpS1335;
  return _M0L6_2atmpS1333 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS188) {
  int32_t _M0L6_2atmpS1332;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1332 = (int32_t)_M0L4selfS188;
  return _M0L6_2atmpS1332;
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
  int32_t _M0L3lenS1331;
  int32_t _M0L8requiredS184;
  uint16_t* _M0L4dataS1326;
  int32_t _M0L6_2atmpS1325;
  int32_t _if__result_2634;
  uint16_t* _M0L4dataS1327;
  int32_t _M0L3lenS1328;
  int32_t _M0L3lenS1330;
  int32_t _M0L6_2atmpS1329;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS182 = Moonbit_array_length(_M0L3strS183);
  if (_M0L8str__lenS182 == 0) {
    return 0;
  }
  _M0L3lenS1331 = _M0L4selfS185->$1;
  _M0L8requiredS184 = _M0L3lenS1331 + _M0L8str__lenS182;
  _M0L4dataS1326 = _M0L4selfS185->$0;
  _M0L6_2atmpS1325 = Moonbit_array_length(_M0L4dataS1326);
  if (_M0L8requiredS184 > _M0L6_2atmpS1325) {
    _if__result_2634 = 1;
  } else {
    int32_t _M0L3lenS1324 = _M0L4selfS185->$1;
    _if__result_2634 = _M0L8requiredS184 < _M0L3lenS1324;
  }
  if (_if__result_2634) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS185, _M0L8requiredS184);
  }
  _M0L4dataS1327 = _M0L4selfS185->$0;
  _M0L3lenS1328 = _M0L4selfS185->$1;
  moonbit_incref_cycle_free(_M0L4dataS1327);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1327, _M0L3lenS1328, _M0L3strS183, 0, _M0L8str__lenS182);
  moonbit_decref_cycle_free(_M0L4dataS1327);
  _M0L3lenS1330 = _M0L4selfS185->$1;
  _M0L6_2atmpS1329 = _M0L3lenS1330 + _M0L8str__lenS182;
  _M0L4selfS185->$1 = _M0L6_2atmpS1329;
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
      int32_t _M0L6_2atmpS1321 = _M0L3strS179[_M0L1iS176];
      int32_t _M0L6_2atmpS1322;
      int32_t _M0L6_2atmpS1323;
      _M0L4selfS178[_M0L1jS177] = _M0L6_2atmpS1321;
      _M0L6_2atmpS1322 = _M0L1iS176 + 1;
      _M0L6_2atmpS1323 = _M0L1jS177 + 1;
      _M0L1iS176 = _M0L6_2atmpS1322;
      _M0L1jS177 = _M0L6_2atmpS1323;
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
    int32_t _M0L3lenS1292 = _M0L4selfS171->$1;
    uint16_t* _M0L4dataS1294 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1293 = Moonbit_array_length(_M0L4dataS1294);
    uint16_t* _M0L4dataS1297;
    int32_t _M0L3lenS1298;
    int32_t _M0L6_2atmpS1299;
    int32_t _M0L3lenS1301;
    int32_t _M0L6_2atmpS1300;
    if (_M0L3lenS1292 >= _M0L6_2atmpS1293) {
      int32_t _M0L3lenS1296 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1295 = _M0L3lenS1296 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1295);
    }
    _M0L4dataS1297 = _M0L4selfS171->$0;
    _M0L3lenS1298 = _M0L4selfS171->$1;
    moonbit_incref_cycle_free(_M0L4dataS1297);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1299 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS169);
    if (
      _M0L3lenS1298 < 0
      || _M0L3lenS1298 >= Moonbit_array_length(_M0L4dataS1297)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1297[_M0L3lenS1298] = _M0L6_2atmpS1299;
    moonbit_decref_cycle_free(_M0L4dataS1297);
    _M0L3lenS1301 = _M0L4selfS171->$1;
    _M0L6_2atmpS1300 = _M0L3lenS1301 + 1;
    _M0L4selfS171->$1 = _M0L6_2atmpS1300;
  } else if (_M0L4codeS169 <= 1114111u) {
    uint16_t* _M0L4dataS1305 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1303 = Moonbit_array_length(_M0L4dataS1305);
    int32_t _M0L3lenS1304 = _M0L4selfS171->$1;
    int32_t _M0L6_2atmpS1302 = _M0L6_2atmpS1303 - _M0L3lenS1304;
    uint32_t _M0L4codeS172;
    uint16_t* _M0L4dataS1308;
    int32_t _M0L3lenS1309;
    uint32_t _M0L6_2atmpS1312;
    uint32_t _M0L6_2atmpS1311;
    int32_t _M0L6_2atmpS1310;
    uint16_t* _M0L4dataS1313;
    int32_t _M0L3lenS1318;
    int32_t _M0L6_2atmpS1314;
    uint32_t _M0L6_2atmpS1317;
    uint32_t _M0L6_2atmpS1316;
    int32_t _M0L6_2atmpS1315;
    int32_t _M0L3lenS1320;
    int32_t _M0L6_2atmpS1319;
    if (_M0L6_2atmpS1302 < 2) {
      int32_t _M0L3lenS1307 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1306 = _M0L3lenS1307 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1306);
    }
    _M0L4codeS172 = _M0L4codeS169 - 65536u;
    _M0L4dataS1308 = _M0L4selfS171->$0;
    _M0L3lenS1309 = _M0L4selfS171->$1;
    _M0L6_2atmpS1312 = _M0L4codeS172 >> 10;
    _M0L6_2atmpS1311 = 55296u + _M0L6_2atmpS1312;
    moonbit_incref_cycle_free(_M0L4dataS1308);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1310 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1311);
    if (
      _M0L3lenS1309 < 0
      || _M0L3lenS1309 >= Moonbit_array_length(_M0L4dataS1308)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1308[_M0L3lenS1309] = _M0L6_2atmpS1310;
    moonbit_decref_cycle_free(_M0L4dataS1308);
    _M0L4dataS1313 = _M0L4selfS171->$0;
    _M0L3lenS1318 = _M0L4selfS171->$1;
    _M0L6_2atmpS1314 = _M0L3lenS1318 + 1;
    _M0L6_2atmpS1317 = _M0L4codeS172 & 1023u;
    _M0L6_2atmpS1316 = 56320u + _M0L6_2atmpS1317;
    moonbit_incref_cycle_free(_M0L4dataS1313);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1315 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1316);
    if (
      _M0L6_2atmpS1314 < 0
      || _M0L6_2atmpS1314 >= Moonbit_array_length(_M0L4dataS1313)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1313[_M0L6_2atmpS1314] = _M0L6_2atmpS1315;
    moonbit_decref_cycle_free(_M0L4dataS1313);
    _M0L3lenS1320 = _M0L4selfS171->$1;
    _M0L6_2atmpS1319 = _M0L3lenS1320 + 2;
    _M0L4selfS171->$1 = _M0L6_2atmpS1319;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_27.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS166,
  int32_t _M0L8requiredS167
) {
  uint16_t* _M0L4dataS1291;
  int32_t _M0L6_2atmpS1289;
  int32_t _M0L3lenS1290;
  int32_t _M0L13new__capacityS165;
  uint16_t* _M0L4dataS1286;
  int32_t _M0L6_2atmpS1287;
  int32_t _M0L3lenS1288;
  uint16_t* _M0L9new__dataS168;
  uint16_t* _M0L6_2aoldS2501;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1291 = _M0L4selfS166->$0;
  _M0L6_2atmpS1289 = Moonbit_array_length(_M0L4dataS1291);
  _M0L3lenS1290 = _M0L4selfS166->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS165
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1289, _M0L3lenS1290, _M0L8requiredS167);
  _M0L4dataS1286 = _M0L4selfS166->$0;
  moonbit_incref_cycle_free(_M0L4dataS1286);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1287 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1288 = _M0L4selfS166->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS168
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1286, _M0L13new__capacityS165, _M0L6_2atmpS1287, _M0L3lenS1288, 0, 0);
  _M0L6_2aoldS2501 = _M0L4selfS166->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2501);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_28.data);
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
  int32_t _M0L6_2atmpS1285;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1285 = *(int32_t*)&_M0L4selfS158;
  return (uint16_t)_M0L6_2atmpS1285;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS157) {
  int32_t _M0L6_2atmpS1284;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1284 = _M0L4selfS157;
  return *(uint32_t*)&_M0L6_2atmpS1284;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS155
) {
  int32_t _M0L3lenS1275;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1275 = _M0L4selfS155->$1;
  if (_M0L3lenS1275 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1276 = _M0L4selfS155->$1;
    uint16_t* _M0L4dataS1278 = _M0L4selfS155->$0;
    int32_t _M0L6_2atmpS1277 = Moonbit_array_length(_M0L4dataS1278);
    if (_M0L3lenS1276 == _M0L6_2atmpS1277) {
      uint16_t* _M0L4dataS1279 = _M0L4selfS155->$0;
      moonbit_incref_cycle_free(_M0L4dataS1279);
      return _M0L4dataS1279;
    } else {
      uint16_t* _M0L4dataS1280 = _M0L4selfS155->$0;
      int32_t _M0L3lenS1281 = _M0L4selfS155->$1;
      int32_t _M0L6_2atmpS1282;
      int32_t _M0L3lenS1283;
      uint16_t* _M0L4dataS156;
      moonbit_incref_cycle_free(_M0L4dataS1280);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1282 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1283 = _M0L4selfS155->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS156
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1280, _M0L3lenS1281, _M0L6_2atmpS1282, _M0L3lenS1283, 0, 0);
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
  int32_t _if__result_2637;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS148 >= 0) {
    if (_M0L3lenS149 >= 0) {
      if (_M0L11src__offsetS150 >= 0) {
        if (_M0L11dst__offsetS151 >= 0) {
          int32_t _M0L6_2atmpS1271 = _M0L11src__offsetS150 + _M0L3lenS149;
          int32_t _M0L6_2atmpS1272 = Moonbit_array_length(_M0L3srcS152);
          if (_M0L6_2atmpS1271 <= _M0L6_2atmpS1272) {
            int32_t _M0L6_2atmpS1270 = _M0L11dst__offsetS151 + _M0L3lenS149;
            _if__result_2637 = _M0L6_2atmpS1270 <= _M0L13allocate__lenS148;
          } else {
            _if__result_2637 = 0;
          }
        } else {
          _if__result_2637 = 0;
        }
      } else {
        _if__result_2637 = 0;
      }
    } else {
      _if__result_2637 = 0;
    }
  } else {
    _if__result_2637 = 0;
  }
  if (_if__result_2637) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS152, _M0L13allocate__lenS148, _M0L4initS153, _M0L11src__offsetS150, _M0L11dst__offsetS151, _M0L3lenS149);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS154;
    int32_t _M0L6_2atmpS1274;
    moonbit_string_t _M0L6_2atmpS1273;
    uint16_t* _result_2638;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS154
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L13allocate__lenS148);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11src__offsetS150);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11dst__offsetS151);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L3lenS149);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_33.data);
    _M0L6_2atmpS1274 = Moonbit_array_length(_M0L3srcS152);
    moonbit_decref_cycle_free(_M0L3srcS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L6_2atmpS1274);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1273
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS154);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS154);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2638 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1273);
    moonbit_decref_cycle_free(_M0L6_2atmpS1273);
    return _result_2638;
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
  struct _M0TPB13StringBuilder* _block_2639;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS139 < 1) {
    _M0L7initialS138 = 1;
  } else {
    int32_t _M0L6_2atmpS1269 = _M0L10size__hintS139 + 1;
    _M0L7initialS138 = _M0L6_2atmpS1269 / 2;
  }
  _M0L4dataS140 = (uint16_t*)moonbit_make_string(_M0L7initialS138, 0);
  _block_2639
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2639)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 89, 0);
  _block_2639->$0 = _M0L4dataS140;
  _block_2639->$1 = 0;
  return _block_2639;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS137) {
  int32_t _M0L6_2atmpS1268;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1268 = (int32_t)_M0L4selfS137;
  return _M0L6_2atmpS1268;
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS117,
  int32_t _M0L13allocate__lenS113,
  int32_t _M0L3lenS114,
  int32_t _M0L11src__offsetS115,
  int32_t _M0L11dst__offsetS116
) {
  int32_t _if__result_2640;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS113 >= 0) {
    if (_M0L3lenS114 >= 0) {
      if (_M0L11src__offsetS115 >= 0) {
        if (_M0L11dst__offsetS116 >= 0) {
          int32_t _M0L6_2atmpS1249 = _M0L11src__offsetS115 + _M0L3lenS114;
          int32_t _M0L6_2atmpS1250;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1250
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS117);
          if (_M0L6_2atmpS1249 <= _M0L6_2atmpS1250) {
            int32_t _M0L6_2atmpS1248 = _M0L11dst__offsetS116 + _M0L3lenS114;
            _if__result_2640 = _M0L6_2atmpS1248 <= _M0L13allocate__lenS113;
          } else {
            _if__result_2640 = 0;
          }
        } else {
          _if__result_2640 = 0;
        }
      } else {
        _if__result_2640 = 0;
      }
    } else {
      _if__result_2640 = 0;
    }
  } else {
    _if__result_2640 = 0;
  }
  if (_if__result_2640) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS117, _M0L13allocate__lenS113, _M0L11src__offsetS115, _M0L11dst__offsetS116, _M0L3lenS114);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS118;
    int32_t _M0L6_2atmpS1252;
    moonbit_string_t _M0L6_2atmpS1251;
    int32_t* _result_2641;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS118
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L13allocate__lenS113);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11src__offsetS115);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11dst__offsetS116);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L3lenS114);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1252 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS117);
    moonbit_decref_cycle_free(_M0L3srcS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L6_2atmpS1252);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1251
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS118);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS118);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2641
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1251);
    moonbit_decref_cycle_free(_M0L6_2atmpS1251);
    return _result_2641;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS123,
  int32_t _M0L13allocate__lenS119,
  int32_t _M0L3lenS120,
  int32_t _M0L11src__offsetS121,
  int32_t _M0L11dst__offsetS122
) {
  int32_t _if__result_2642;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS119 >= 0) {
    if (_M0L3lenS120 >= 0) {
      if (_M0L11src__offsetS121 >= 0) {
        if (_M0L11dst__offsetS122 >= 0) {
          int32_t _M0L6_2atmpS1254 = _M0L11src__offsetS121 + _M0L3lenS120;
          int32_t _M0L6_2atmpS1255;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1255
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS123);
          if (_M0L6_2atmpS1254 <= _M0L6_2atmpS1255) {
            int32_t _M0L6_2atmpS1253 = _M0L11dst__offsetS122 + _M0L3lenS120;
            _if__result_2642 = _M0L6_2atmpS1253 <= _M0L13allocate__lenS119;
          } else {
            _if__result_2642 = 0;
          }
        } else {
          _if__result_2642 = 0;
        }
      } else {
        _if__result_2642 = 0;
      }
    } else {
      _if__result_2642 = 0;
    }
  } else {
    _if__result_2642 = 0;
  }
  if (_if__result_2642) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS123, _M0L13allocate__lenS119, _M0L11src__offsetS121, _M0L11dst__offsetS122, _M0L3lenS120);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS124;
    int32_t _M0L6_2atmpS1257;
    moonbit_string_t _M0L6_2atmpS1256;
    float* _result_2643;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS124
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L13allocate__lenS119);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11src__offsetS121);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11dst__offsetS122);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L3lenS120);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1257 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS123);
    moonbit_decref_cycle_free(_M0L3srcS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L6_2atmpS1257);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1256
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS124);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS124);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2643
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1256);
    moonbit_decref_cycle_free(_M0L6_2atmpS1256);
    return _result_2643;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS129,
  int32_t _M0L13allocate__lenS125,
  int32_t _M0L3lenS126,
  int32_t _M0L11src__offsetS127,
  int32_t _M0L11dst__offsetS128
) {
  int32_t _if__result_2644;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS125 >= 0) {
    if (_M0L3lenS126 >= 0) {
      if (_M0L11src__offsetS127 >= 0) {
        if (_M0L11dst__offsetS128 >= 0) {
          int32_t _M0L6_2atmpS1259 = _M0L11src__offsetS127 + _M0L3lenS126;
          int32_t _M0L6_2atmpS1260;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1260
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS129);
          if (_M0L6_2atmpS1259 <= _M0L6_2atmpS1260) {
            int32_t _M0L6_2atmpS1258 = _M0L11dst__offsetS128 + _M0L3lenS126;
            _if__result_2644 = _M0L6_2atmpS1258 <= _M0L13allocate__lenS125;
          } else {
            _if__result_2644 = 0;
          }
        } else {
          _if__result_2644 = 0;
        }
      } else {
        _if__result_2644 = 0;
      }
    } else {
      _if__result_2644 = 0;
    }
  } else {
    _if__result_2644 = 0;
  }
  if (_if__result_2644) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS125, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS129, _M0L11src__offsetS127, _M0L11dst__offsetS128, _M0L3lenS126);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS130;
    int32_t _M0L6_2atmpS1262;
    moonbit_string_t _M0L6_2atmpS1261;
    moonbit_string_t* _result_2645;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS130
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L13allocate__lenS125);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11src__offsetS127);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11dst__offsetS128);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L3lenS126);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1262 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS129);
    moonbit_decref_cycle_free(_M0L3srcS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L6_2atmpS1262);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1261
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS130);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS130);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2645
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1261);
    moonbit_decref_cycle_free(_M0L6_2atmpS1261);
    return _result_2645;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS135,
  int32_t _M0L13allocate__lenS131,
  int32_t _M0L3lenS132,
  int32_t _M0L11src__offsetS133,
  int32_t _M0L11dst__offsetS134
) {
  int32_t _if__result_2646;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS131 >= 0) {
    if (_M0L3lenS132 >= 0) {
      if (_M0L11src__offsetS133 >= 0) {
        if (_M0L11dst__offsetS134 >= 0) {
          int32_t _M0L6_2atmpS1264 = _M0L11src__offsetS133 + _M0L3lenS132;
          int32_t _M0L6_2atmpS1265;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1265
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS135);
          if (_M0L6_2atmpS1264 <= _M0L6_2atmpS1265) {
            int32_t _M0L6_2atmpS1263 = _M0L11dst__offsetS134 + _M0L3lenS132;
            _if__result_2646 = _M0L6_2atmpS1263 <= _M0L13allocate__lenS131;
          } else {
            _if__result_2646 = 0;
          }
        } else {
          _if__result_2646 = 0;
        }
      } else {
        _if__result_2646 = 0;
      }
    } else {
      _if__result_2646 = 0;
    }
  } else {
    _if__result_2646 = 0;
  }
  if (_if__result_2646) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS131, 0, _M0L3srcS135, _M0L11src__offsetS133, _M0L11dst__offsetS134, _M0L3lenS132);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS136;
    int32_t _M0L6_2atmpS1267;
    moonbit_string_t _M0L6_2atmpS1266;
    struct _M0TUsiE** _result_2647;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS136
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L13allocate__lenS131);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11src__offsetS133);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11dst__offsetS134);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L3lenS132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1267 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS135);
    moonbit_decref_cycle_free(_M0L3srcS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L6_2atmpS1267);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1266
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS136);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS136);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2647
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1266);
    moonbit_decref_cycle_free(_M0L6_2atmpS1266);
    return _result_2647;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L3objS107
) {
  struct _M0TPB6Logger _M0L6_2atmpS1245;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS108);
  _M0L6_2atmpS1245
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS108
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS107, _M0L6_2atmpS1245);
  if (_M0L6_2atmpS1245.$1) {
    moonbit_decref(_M0L6_2atmpS1245.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS1246;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS1246
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS109, _M0L6_2atmpS1246);
  if (_M0L6_2atmpS1246.$1) {
    moonbit_decref(_M0L6_2atmpS1246.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  uint64_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS1247;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS1247
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS111, _M0L6_2atmpS1247);
  if (_M0L6_2atmpS1247.$1) {
    moonbit_decref(_M0L6_2atmpS1247.$1);
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
        int32_t _M0L6_2atmpS1200 = _M0L11dst__offsetS20 + _M0L1iS22;
        int32_t _M0L6_2atmpS1202 = _M0L11src__offsetS21 + _M0L1iS22;
        int32_t _M0L6_2atmpS1201;
        int32_t _M0L6_2atmpS1203;
        if (
          _M0L6_2atmpS1202 < 0
          || _M0L6_2atmpS1202 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1201 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1202];
        if (
          _M0L6_2atmpS1200 < 0
          || _M0L6_2atmpS1200 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1200] = _M0L6_2atmpS1201;
        _M0L6_2atmpS1203 = _M0L1iS22 + 1;
        _M0L1iS22 = _M0L6_2atmpS1203;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1208 = _M0L3lenS23 - 1;
    int32_t _M0L1iS25 = _M0L6_2atmpS1208;
    while (1) {
      if (_M0L1iS25 >= 0) {
        int32_t _M0L6_2atmpS1204 = _M0L11dst__offsetS20 + _M0L1iS25;
        int32_t _M0L6_2atmpS1206 = _M0L11src__offsetS21 + _M0L1iS25;
        int32_t _M0L6_2atmpS1205;
        int32_t _M0L6_2atmpS1207;
        if (
          _M0L6_2atmpS1206 < 0
          || _M0L6_2atmpS1206 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1205 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1206];
        if (
          _M0L6_2atmpS1204 < 0
          || _M0L6_2atmpS1204 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1204] = _M0L6_2atmpS1205;
        _M0L6_2atmpS1207 = _M0L1iS25 - 1;
        _M0L1iS25 = _M0L6_2atmpS1207;
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
        int32_t _M0L6_2atmpS1209 = _M0L11dst__offsetS29 + _M0L1iS31;
        int32_t _M0L6_2atmpS1211 = _M0L11src__offsetS30 + _M0L1iS31;
        int32_t _M0L6_2atmpS1210;
        int32_t _M0L6_2atmpS1212;
        if (
          _M0L6_2atmpS1211 < 0
          || _M0L6_2atmpS1211 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1210 = (int32_t)_M0L3srcS28[_M0L6_2atmpS1211];
        if (
          _M0L6_2atmpS1209 < 0
          || _M0L6_2atmpS1209 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS1209] = _M0L6_2atmpS1210;
        _M0L6_2atmpS1212 = _M0L1iS31 + 1;
        _M0L1iS31 = _M0L6_2atmpS1212;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1217 = _M0L3lenS32 - 1;
    int32_t _M0L1iS34 = _M0L6_2atmpS1217;
    while (1) {
      if (_M0L1iS34 >= 0) {
        int32_t _M0L6_2atmpS1213 = _M0L11dst__offsetS29 + _M0L1iS34;
        int32_t _M0L6_2atmpS1215 = _M0L11src__offsetS30 + _M0L1iS34;
        int32_t _M0L6_2atmpS1214;
        int32_t _M0L6_2atmpS1216;
        if (
          _M0L6_2atmpS1215 < 0
          || _M0L6_2atmpS1215 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1214 = (int32_t)_M0L3srcS28[_M0L6_2atmpS1215];
        if (
          _M0L6_2atmpS1213 < 0
          || _M0L6_2atmpS1213 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS1213] = _M0L6_2atmpS1214;
        _M0L6_2atmpS1216 = _M0L1iS34 - 1;
        _M0L1iS34 = _M0L6_2atmpS1216;
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
        int32_t _M0L6_2atmpS1218 = _M0L11dst__offsetS38 + _M0L1iS40;
        int32_t _M0L6_2atmpS1220 = _M0L11src__offsetS39 + _M0L1iS40;
        float _M0L6_2atmpS1219;
        int32_t _M0L6_2atmpS1221;
        if (
          _M0L6_2atmpS1220 < 0
          || _M0L6_2atmpS1220 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1219 = (float)_M0L3srcS37[_M0L6_2atmpS1220];
        if (
          _M0L6_2atmpS1218 < 0
          || _M0L6_2atmpS1218 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS36[_M0L6_2atmpS1218] = _M0L6_2atmpS1219;
        _M0L6_2atmpS1221 = _M0L1iS40 + 1;
        _M0L1iS40 = _M0L6_2atmpS1221;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1226 = _M0L3lenS41 - 1;
    int32_t _M0L1iS43 = _M0L6_2atmpS1226;
    while (1) {
      if (_M0L1iS43 >= 0) {
        int32_t _M0L6_2atmpS1222 = _M0L11dst__offsetS38 + _M0L1iS43;
        int32_t _M0L6_2atmpS1224 = _M0L11src__offsetS39 + _M0L1iS43;
        float _M0L6_2atmpS1223;
        int32_t _M0L6_2atmpS1225;
        if (
          _M0L6_2atmpS1224 < 0
          || _M0L6_2atmpS1224 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1223 = (float)_M0L3srcS37[_M0L6_2atmpS1224];
        if (
          _M0L6_2atmpS1222 < 0
          || _M0L6_2atmpS1222 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS36[_M0L6_2atmpS1222] = _M0L6_2atmpS1223;
        _M0L6_2atmpS1225 = _M0L1iS43 - 1;
        _M0L1iS43 = _M0L6_2atmpS1225;
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
        int32_t _M0L6_2atmpS1227 = _M0L11dst__offsetS47 + _M0L1iS49;
        int32_t _M0L6_2atmpS1229 = _M0L11src__offsetS48 + _M0L1iS49;
        moonbit_string_t _M0L6_2atmpS1228;
        moonbit_string_t _M0L6_2aoldS2502;
        int32_t _M0L6_2atmpS1230;
        if (
          _M0L6_2atmpS1229 < 0
          || _M0L6_2atmpS1229 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1228 = (moonbit_string_t)_M0L3srcS46[_M0L6_2atmpS1229];
        if (
          _M0L6_2atmpS1227 < 0
          || _M0L6_2atmpS1227 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2502 = (moonbit_string_t)_M0L3dstS45[_M0L6_2atmpS1227];
        moonbit_incref_cycle_free(_M0L6_2atmpS1228);
        moonbit_decref_cycle_free(_M0L6_2aoldS2502);
        _M0L3dstS45[_M0L6_2atmpS1227] = _M0L6_2atmpS1228;
        _M0L6_2atmpS1230 = _M0L1iS49 + 1;
        _M0L1iS49 = _M0L6_2atmpS1230;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1235 = _M0L3lenS50 - 1;
    int32_t _M0L1iS52 = _M0L6_2atmpS1235;
    while (1) {
      if (_M0L1iS52 >= 0) {
        int32_t _M0L6_2atmpS1231 = _M0L11dst__offsetS47 + _M0L1iS52;
        int32_t _M0L6_2atmpS1233 = _M0L11src__offsetS48 + _M0L1iS52;
        moonbit_string_t _M0L6_2atmpS1232;
        moonbit_string_t _M0L6_2aoldS2503;
        int32_t _M0L6_2atmpS1234;
        if (
          _M0L6_2atmpS1233 < 0
          || _M0L6_2atmpS1233 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1232 = (moonbit_string_t)_M0L3srcS46[_M0L6_2atmpS1233];
        if (
          _M0L6_2atmpS1231 < 0
          || _M0L6_2atmpS1231 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2503 = (moonbit_string_t)_M0L3dstS45[_M0L6_2atmpS1231];
        moonbit_incref_cycle_free(_M0L6_2atmpS1232);
        moonbit_decref_cycle_free(_M0L6_2aoldS2503);
        _M0L3dstS45[_M0L6_2atmpS1231] = _M0L6_2atmpS1232;
        _M0L6_2atmpS1234 = _M0L1iS52 - 1;
        _M0L1iS52 = _M0L6_2atmpS1234;
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
        int32_t _M0L6_2atmpS1236 = _M0L11dst__offsetS56 + _M0L1iS58;
        int32_t _M0L6_2atmpS1238 = _M0L11src__offsetS57 + _M0L1iS58;
        struct _M0TUsiE* _M0L6_2atmpS1237;
        struct _M0TUsiE* _M0L6_2aoldS2504;
        int32_t _M0L6_2atmpS1239;
        if (
          _M0L6_2atmpS1238 < 0
          || _M0L6_2atmpS1238 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1237 = (struct _M0TUsiE*)_M0L3srcS55[_M0L6_2atmpS1238];
        if (
          _M0L6_2atmpS1236 < 0
          || _M0L6_2atmpS1236 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2504 = (struct _M0TUsiE*)_M0L3dstS54[_M0L6_2atmpS1236];
        if (_M0L6_2atmpS1237) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1237);
        }
        if (_M0L6_2aoldS2504) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2504);
        }
        _M0L3dstS54[_M0L6_2atmpS1236] = _M0L6_2atmpS1237;
        _M0L6_2atmpS1239 = _M0L1iS58 + 1;
        _M0L1iS58 = _M0L6_2atmpS1239;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1244 = _M0L3lenS59 - 1;
    int32_t _M0L1iS61 = _M0L6_2atmpS1244;
    while (1) {
      if (_M0L1iS61 >= 0) {
        int32_t _M0L6_2atmpS1240 = _M0L11dst__offsetS56 + _M0L1iS61;
        int32_t _M0L6_2atmpS1242 = _M0L11src__offsetS57 + _M0L1iS61;
        struct _M0TUsiE* _M0L6_2atmpS1241;
        struct _M0TUsiE* _M0L6_2aoldS2505;
        int32_t _M0L6_2atmpS1243;
        if (
          _M0L6_2atmpS1242 < 0
          || _M0L6_2atmpS1242 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1241 = (struct _M0TUsiE*)_M0L3srcS55[_M0L6_2atmpS1242];
        if (
          _M0L6_2atmpS1240 < 0
          || _M0L6_2atmpS1240 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2505 = (struct _M0TUsiE*)_M0L3dstS54[_M0L6_2atmpS1240];
        if (_M0L6_2atmpS1241) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1241);
        }
        if (_M0L6_2aoldS2505) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2505);
        }
        _M0L3dstS54[_M0L6_2atmpS1240] = _M0L6_2atmpS1241;
        _M0L6_2atmpS1243 = _M0L1iS61 - 1;
        _M0L1iS61 = _M0L6_2atmpS1243;
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
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_34.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S13, _M0L15_2a_2aarg__6389S12);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_35.data);
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1171) {
  switch (Moonbit_object_tag(_M0L4_2aeS1171)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_36.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1171);
      break;
    }
    
    case 3: {
      return (moonbit_string_t)moonbit_string_literal_37.data;
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_38.data;
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_39.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1195,
  struct _M0TPB4Show _M0L8_2aparamS1194
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1193 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1195;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1193, _M0L8_2aparamS1194);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1192,
  struct _M0TPB4Show _M0L8_2aparamS1191
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1190 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1192;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1190, _M0L8_2aparamS1191);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1189,
  int32_t _M0L8_2aparamS1188
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1187 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1189;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1187, _M0L8_2aparamS1188);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1186,
  struct _M0TPC16string10StringView _M0L8_2aparamS1185
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1184 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1186;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1184, _M0L8_2aparamS1185);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1183,
  moonbit_string_t _M0L8_2aparamS1180,
  int32_t _M0L8_2aparamS1181,
  int32_t _M0L8_2aparamS1182
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1179 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1183;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1179, _M0L8_2aparamS1180, _M0L8_2aparamS1181, _M0L8_2aparamS1182);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1178,
  moonbit_string_t _M0L8_2aparamS1177
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1176 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1178;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1176, _M0L8_2aparamS1177);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1199;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1164;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1165;
  int32_t _M0L7_2abindS1166;
  struct _M0TUsiE** _M0L7_2abindS1167;
  int32_t _M0L6_2acntS2510;
  int32_t _M0L2__S1168;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1199
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1164
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1164)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 92, 0);
  _M0L12async__testsS1164->$0 = _M0L6_2atmpS1199;
  _M0L12async__testsS1164->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1165
  = _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1166 = _M0L7_2abindS1165->$1;
  _M0L7_2abindS1167 = _M0L7_2abindS1165->$0;
  _M0L6_2acntS2510
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1165));
  if (_M0L6_2acntS2510 > 1) {
    int32_t _M0L11_2anew__cntS2511 = _M0L6_2acntS2510 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1165), _M0L11_2anew__cntS2511);
    moonbit_incref_cycle_free(_M0L7_2abindS1167);
  } else if (_M0L6_2acntS2510 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1165);
  }
  _M0L2__S1168 = 0;
  while (1) {
    if (_M0L2__S1168 < _M0L7_2abindS1166) {
      struct _M0TUsiE* _M0L3argS1169 =
        (struct _M0TUsiE*)_M0L7_2abindS1167[_M0L2__S1168];
      moonbit_string_t _M0L6_2atmpS1196 = _M0L3argS1169->$0;
      int32_t _M0L6_2atmpS1197 = _M0L3argS1169->$1;
      int32_t _M0L6_2atmpS1198;
      moonbit_incref_cycle_free(_M0L6_2atmpS1196);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1164, _M0L6_2atmpS1196, _M0L6_2atmpS1197);
      moonbit_decref_cycle_free(_M0L6_2atmpS1196);
      _M0L6_2atmpS1198 = _M0L2__S1168 + 1;
      _M0L2__S1168 = _M0L6_2atmpS1198;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1167);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_adex\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples29lkd2014__adex__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1164);
  moonbit_decref_cycle_free(_M0L12async__testsS1164);
  moonbit_flush_cycles();
  return 0;
}