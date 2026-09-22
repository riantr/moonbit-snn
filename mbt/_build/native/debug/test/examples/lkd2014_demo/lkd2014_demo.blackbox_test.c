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

struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TPB8MutLocalGiE;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c945;

struct _M0TWRPC15error5ErrorEs;

struct _M0TPB4Show;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGbE;

struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c940;

struct _M0TPB5ArrayGUsiEE;

struct _M0TPB5ArrayGsE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TWEu;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus;

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

struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c945 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0TP26RiantR8snn__mbt9PostSpike {
  float $0;
  
};

struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter {
  struct _M0TPB5ArrayGfE* $0;
  struct _M0TPB5ArrayGiE* $1;
  
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

struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0KTPB6LoggerTPB13StringBuilder {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c940 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
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

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** $0;
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS952(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS945(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS940(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS917(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S910(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter6custom(
  float,
  float,
  float,
  float,
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

int32_t _M0FP26RiantR8snn__mbt20stimulate__spiketime(
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus*,
  float,
  float
);

struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0MP26RiantR8snn__mbt17SpikeTimeStimulus3new(
  struct _M0TP26RiantR8snn__mbt2IF*,
  moonbit_string_t,
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGiE*
);

struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0MP26RiantR8snn__mbt18SpikeTimeParameter3new(
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGiE*
);

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

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4copyGfE(struct _M0TPB5ArrayGfE*);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4copyGiE(struct _M0TPB5ArrayGiE*);

int32_t _M0MPC15array5Array12unsafe__blitGfE(
  struct _M0TPB5ArrayGfE*,
  int32_t,
  struct _M0TPB5ArrayGfE*,
  int32_t,
  int32_t
);

int32_t _M0MPC15array5Array12unsafe__blitGiE(
  struct _M0TPB5ArrayGiE*,
  int32_t,
  struct _M0TPB5ArrayGiE*,
  int32_t,
  int32_t
);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

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

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE*);

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
} const moonbit_string_literal_13 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_10 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[6]; 
} const moonbit_string_literal_17 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 5, 102, 97, 
    108, 115, 101, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_16 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 116, 114, 
    117, 101, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[117]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 116, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 108, 107, 100, 50, 48, 49, 52, 95, 
    100, 101, 109, 111, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 
    101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 
    116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 
    108, 74, 115, 69, 114, 114, 111, 114, 46, 77, 111, 111, 110, 66, 
    105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 
    116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[33]; 
} const moonbit_string_literal_7 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 32, 45, 45, 
    45, 45, 45, 32, 69, 78, 68, 32, 77, 79, 79, 78, 32, 84, 69, 83, 84, 
    32, 82, 69, 83, 85, 76, 84, 32, 45, 45, 45, 45, 45, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[119]; 
} const moonbit_string_literal_36 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 118, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 108, 107, 100, 50, 48, 49, 52, 95, 
    100, 101, 109, 111, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 
    101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 
    116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 
    108, 83, 107, 105, 112, 84, 101, 115, 116, 46, 77, 111, 111, 110, 
    66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 
    110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 
    116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_29 =
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
} const moonbit_string_literal_11 =
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
} const moonbit_string_literal_15 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS952$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS952
  };

uint32_t const moonbit_layout_table_data[74] =
  {
    sizeof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c940)
    / 4, 1,
    offsetof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c940, $1)
    / 4
    * 2,
    sizeof(struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c945)
    / 4, 1,
    offsetof(struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c945, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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
    sizeof(struct _M0TPB5ArrayGiE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGiE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus) / 4, 5,
    offsetof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus, $5) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter) / 4, 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter, $1) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2107
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS973,
  moonbit_string_t _M0L8filenameS942,
  int32_t _M0L5indexS944
) {
  struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c940* _closure_2132;
  struct _M0TWEu* _M0L13handle__startS940;
  struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c945* _closure_2133;
  struct _M0TWssbEu* _M0L14handle__resultS945;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS952;
  void* _M0L11_2atry__errS967;
  struct moonbit_result_0 _tmp_2135;
  int32_t _handle__error__result_2136;
  int32_t _M0L6_2atmpS2095;
  void* _M0L3errS968;
  moonbit_string_t _M0L4nameS970;
  struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS971;
  moonbit_string_t _M0L7_2anameS972;
  int32_t _M0L6_2acntS2126;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS942);
  _closure_2132
  = (struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c940*)moonbit_malloc(sizeof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c940));
  Moonbit_object_header(_closure_2132)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2132->code
  = &_M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS940;
  _closure_2132->$0 = _M0L5indexS944;
  _closure_2132->$1 = _M0L8filenameS942;
  _M0L13handle__startS940 = (struct _M0TWEu*)_closure_2132;
  moonbit_incref_cycle_free(_M0L8filenameS942);
  _closure_2133
  = (struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c945*)moonbit_malloc(sizeof(struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c945));
  Moonbit_object_header(_closure_2133)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2133->code
  = &_M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS945;
  _closure_2133->$0 = _M0L5indexS944;
  _closure_2133->$1 = _M0L8filenameS942;
  _M0L14handle__resultS945 = (struct _M0TWssbEu*)_closure_2133;
  _M0L17error__to__stringS952
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS952$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2135
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS973, _M0L8filenameS942, _M0L5indexS944, _M0L13handle__startS940, _M0L14handle__resultS945, _M0L17error__to__stringS952);
  if (_tmp_2135.tag) {
    int32_t const _M0L5_2aokS2104 = _tmp_2135.data.ok;
    _handle__error__result_2136 = _M0L5_2aokS2104;
  } else {
    void* const _M0L6_2aerrS2105 = _tmp_2135.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS952);
    moonbit_decref_cycle_free(_M0L13handle__startS940);
    _M0L11_2atry__errS967 = _M0L6_2aerrS2105;
    goto join_966;
  }
  if (_handle__error__result_2136) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS952);
    moonbit_decref_cycle_free(_M0L13handle__startS940);
    _M0L6_2atmpS2095 = 1;
  } else {
    struct moonbit_result_0 _tmp_2137;
    int32_t _handle__error__result_2138;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2137
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS973, _M0L8filenameS942, _M0L5indexS944, _M0L13handle__startS940, _M0L14handle__resultS945, _M0L17error__to__stringS952);
    if (_tmp_2137.tag) {
      int32_t const _M0L5_2aokS2102 = _tmp_2137.data.ok;
      _handle__error__result_2138 = _M0L5_2aokS2102;
    } else {
      void* const _M0L6_2aerrS2103 = _tmp_2137.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS952);
      moonbit_decref_cycle_free(_M0L13handle__startS940);
      _M0L11_2atry__errS967 = _M0L6_2aerrS2103;
      goto join_966;
    }
    if (_handle__error__result_2138) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS952);
      moonbit_decref_cycle_free(_M0L13handle__startS940);
      _M0L6_2atmpS2095 = 1;
    } else {
      struct moonbit_result_0 _tmp_2139;
      int32_t _handle__error__result_2140;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2139
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS973, _M0L8filenameS942, _M0L5indexS944, _M0L13handle__startS940, _M0L14handle__resultS945, _M0L17error__to__stringS952);
      if (_tmp_2139.tag) {
        int32_t const _M0L5_2aokS2100 = _tmp_2139.data.ok;
        _handle__error__result_2140 = _M0L5_2aokS2100;
      } else {
        void* const _M0L6_2aerrS2101 = _tmp_2139.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS952);
        moonbit_decref_cycle_free(_M0L13handle__startS940);
        _M0L11_2atry__errS967 = _M0L6_2aerrS2101;
        goto join_966;
      }
      if (_handle__error__result_2140) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS952);
        moonbit_decref_cycle_free(_M0L13handle__startS940);
        _M0L6_2atmpS2095 = 1;
      } else {
        struct moonbit_result_0 _tmp_2141;
        int32_t _handle__error__result_2142;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2141
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS973, _M0L8filenameS942, _M0L5indexS944, _M0L13handle__startS940, _M0L14handle__resultS945, _M0L17error__to__stringS952);
        if (_tmp_2141.tag) {
          int32_t const _M0L5_2aokS2098 = _tmp_2141.data.ok;
          _handle__error__result_2142 = _M0L5_2aokS2098;
        } else {
          void* const _M0L6_2aerrS2099 = _tmp_2141.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS952);
          moonbit_decref_cycle_free(_M0L13handle__startS940);
          _M0L11_2atry__errS967 = _M0L6_2aerrS2099;
          goto join_966;
        }
        if (_handle__error__result_2142) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS952);
          moonbit_decref_cycle_free(_M0L13handle__startS940);
          _M0L6_2atmpS2095 = 1;
        } else {
          struct moonbit_result_0 _tmp_2143;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2143
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS973, _M0L8filenameS942, _M0L5indexS944, _M0L13handle__startS940, _M0L14handle__resultS945, _M0L17error__to__stringS952);
          moonbit_decref_cycle_free(_M0L13handle__startS940);
          moonbit_decref_cycle_free(_M0L17error__to__stringS952);
          if (_tmp_2143.tag) {
            int32_t const _M0L5_2aokS2096 = _tmp_2143.data.ok;
            _M0L6_2atmpS2095 = _M0L5_2aokS2096;
          } else {
            void* const _M0L6_2aerrS2097 = _tmp_2143.data.err;
            _M0L11_2atry__errS967 = _M0L6_2aerrS2097;
            goto join_966;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2095) {
    void* _M0L132RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2106 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L132RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2106)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L132RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2106)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS967
    = _M0L132RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2106;
    goto join_966;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS945);
  }
  goto joinlet_2134;
  join_966:;
  _M0L3errS968 = _M0L11_2atry__errS967;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS971
  = (struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS968;
  _M0L7_2anameS972 = _M0L36_2aMoonBitTestDriverInternalSkipTestS971->$0;
  _M0L6_2acntS2126
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS971));
  if (_M0L6_2acntS2126 > 1) {
    int32_t _M0L11_2anew__cntS2127 = _M0L6_2acntS2126 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS971), _M0L11_2anew__cntS2127);
    moonbit_incref_cycle_free(_M0L7_2anameS972);
  } else if (_M0L6_2acntS2126 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS971);
  }
  _M0L4nameS970 = _M0L7_2anameS972;
  goto join_969;
  goto joinlet_2144;
  join_969:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS945(_M0L14handle__resultS945, _M0L4nameS970, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS945);
  moonbit_decref_cycle_free(_M0L4nameS970);
  joinlet_2144:;
  joinlet_2134:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS952(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2094,
  void* _M0L3errS953
) {
  void* _M0L1eS955;
  moonbit_string_t _M0L1eS957;
  moonbit_string_t _result_2147;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS953)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS958 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS953;
      moonbit_string_t _M0L4_2aeS959 = _M0L10_2aFailureS958->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS959);
      _M0L1eS957 = _M0L4_2aeS959;
      goto join_956;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS960 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS953;
      moonbit_string_t _M0L4_2aeS961 = _M0L15_2aInspectErrorS960->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS961);
      _M0L1eS957 = _M0L4_2aeS961;
      goto join_956;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS962 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS953;
      moonbit_string_t _M0L4_2aeS963 = _M0L16_2aSnapshotErrorS962->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS963);
      _M0L1eS957 = _M0L4_2aeS963;
      goto join_956;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS964 =
        (struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS953;
      moonbit_string_t _M0L4_2aeS965 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS964->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS965);
      _M0L1eS957 = _M0L4_2aeS965;
      goto join_956;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS953);
      _M0L1eS955 = _M0L3errS953;
      goto join_954;
      break;
    }
  }
  join_956:;
  return _M0L1eS957;
  join_954:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_2147 = _M0FP15Error10to__string(_M0L1eS955);
  moonbit_decref_cycle_free(_M0L1eS955);
  return _result_2147;
}

int32_t _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS945(
  struct _M0TWssbEu* _M0L6_2aenvS2091,
  moonbit_string_t _M0L10__testnameS946,
  moonbit_string_t _M0L7messageS947,
  int32_t _M0L7skippedS948
) {
  struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c945* _M0L14_2acasted__envS2092;
  moonbit_string_t _M0L8filenameS942;
  int32_t _M0L5indexS944;
  moonbit_string_t _M0L10file__nameS949;
  moonbit_string_t _M0L7messageS950;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS951;
  moonbit_string_t _M0L6_2atmpS2093;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2092
  = (struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c945*)_M0L6_2aenvS2091;
  _M0L8filenameS942 = _M0L14_2acasted__envS2092->$1;
  _M0L5indexS944 = _M0L14_2acasted__envS2092->$0;
  if (!_M0L7skippedS948 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS949
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS942, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS950
  = _M0MPC16string6String14escape_2einner(_M0L7messageS947, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS951
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS951, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS951, _M0L10file__nameS949);
  moonbit_decref_cycle_free(_M0L10file__nameS949);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS951, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS951, _M0L5indexS944);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS951, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS951, _M0L7messageS950);
  moonbit_decref_cycle_free(_M0L7messageS950);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS951, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2093
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS951);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS951);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2093);
  moonbit_decref_cycle_free(_M0L6_2atmpS2093);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS940(
  struct _M0TWEu* _M0L6_2aenvS2088
) {
  struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c940* _M0L14_2acasted__envS2089;
  moonbit_string_t _M0L8filenameS942;
  int32_t _M0L5indexS944;
  moonbit_string_t _M0L10file__nameS941;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS943;
  moonbit_string_t _M0L6_2atmpS2090;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2089
  = (struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2flkd2014__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c940*)_M0L6_2aenvS2088;
  _M0L8filenameS942 = _M0L14_2acasted__envS2089->$1;
  _M0L5indexS944 = _M0L14_2acasted__envS2089->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS941
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS942, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS943
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS943, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS943, _M0L10file__nameS941);
  moonbit_decref_cycle_free(_M0L10file__nameS941);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS943, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS943, _M0L5indexS944);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS943, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2090
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS943);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS943);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2090);
  moonbit_decref_cycle_free(_M0L6_2atmpS2090);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S910;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS917;
  struct _M0TUsiE** _M0L6_2atmpS2087;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS924;
  moonbit_string_t* _M0L9cli__argsS925;
  moonbit_string_t _M0L6_2atmpS2086;
  moonbit_string_t _M0L6_2atmpS2085;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS926;
  int32_t _M0L7_2abindS927;
  moonbit_string_t* _M0L7_2abindS928;
  int32_t _M0L6_2acntS2128;
  int32_t _M0L2__S929;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S910 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS917 = 0;
  _M0L6_2atmpS2087 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS924
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS924)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS924->$0 = _M0L6_2atmpS2087;
  _M0L16file__and__indexS924->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS925
  = _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS925)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2086 = (moonbit_string_t)_M0L9cli__argsS925[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2086);
  moonbit_decref_cycle_free(_M0L9cli__argsS925);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2085
  = _M0MP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2086);
  moonbit_decref_cycle_free(_M0L6_2atmpS2086);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS926
  = _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS917(_M0L51moonbit__test__driver__internal__split__mbt__stringS917, _M0L6_2atmpS2085, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2085);
  _M0L7_2abindS927 = _M0L10test__argsS926->$1;
  _M0L7_2abindS928 = _M0L10test__argsS926->$0;
  _M0L6_2acntS2128
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS926));
  if (_M0L6_2acntS2128 > 1) {
    int32_t _M0L11_2anew__cntS2129 = _M0L6_2acntS2128 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS926), _M0L11_2anew__cntS2129);
    moonbit_incref_cycle_free(_M0L7_2abindS928);
  } else if (_M0L6_2acntS2128 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS926);
  }
  _M0L2__S929 = 0;
  while (1) {
    if (_M0L2__S929 < _M0L7_2abindS927) {
      moonbit_string_t _M0L3argS930 =
        (moonbit_string_t)_M0L7_2abindS928[_M0L2__S929];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS931;
      moonbit_string_t _M0L4fileS932;
      moonbit_string_t _M0L5rangeS933;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS934;
      moonbit_string_t _M0L6_2atmpS2083;
      int32_t _M0L5startS935;
      moonbit_string_t _M0L6_2atmpS2082;
      int32_t _M0L3endS936;
      int32_t _M0L1iS937;
      int32_t _M0L6_2atmpS2084;
      moonbit_incref_cycle_free(_M0L3argS930);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS931
      = _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS917(_M0L51moonbit__test__driver__internal__split__mbt__stringS917, _M0L3argS930, 58);
      moonbit_decref_cycle_free(_M0L3argS930);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS932
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS931, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS933
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS931, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS931);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS934
      = _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS917(_M0L51moonbit__test__driver__internal__split__mbt__stringS917, _M0L5rangeS933, 45);
      moonbit_decref_cycle_free(_M0L5rangeS933);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2083
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS934, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS935
      = _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S910(_M0L45moonbit__test__driver__internal__parse__int__S910, _M0L6_2atmpS2083);
      moonbit_decref_cycle_free(_M0L6_2atmpS2083);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2082
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS934, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS934);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS936
      = _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S910(_M0L45moonbit__test__driver__internal__parse__int__S910, _M0L6_2atmpS2082);
      moonbit_decref_cycle_free(_M0L6_2atmpS2082);
      _M0L1iS937 = _M0L5startS935;
      while (1) {
        if (_M0L1iS937 < _M0L3endS936) {
          struct _M0TUsiE* _M0L8_2atupleS2080;
          int32_t _M0L6_2atmpS2081;
          moonbit_incref_cycle_free(_M0L4fileS932);
          _M0L8_2atupleS2080
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS2080)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS2080->$0 = _M0L4fileS932;
          _M0L8_2atupleS2080->$1 = _M0L1iS937;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS924, _M0L8_2atupleS2080);
          _M0L6_2atmpS2081 = _M0L1iS937 + 1;
          _M0L1iS937 = _M0L6_2atmpS2081;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS932);
        }
        break;
      }
      _M0L6_2atmpS2084 = _M0L2__S929 + 1;
      _M0L2__S929 = _M0L6_2atmpS2084;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS928);
    }
    break;
  }
  return _M0L16file__and__indexS924;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS917(
  int32_t _M0L6_2aenvS2061,
  moonbit_string_t _M0L1sS918,
  int32_t _M0L3sepS919
) {
  moonbit_string_t* _M0L6_2atmpS2079;
  struct _M0TPB5ArrayGsE* _M0L3resS920;
  struct _M0TPB8MutLocalGiE* _M0L1iS921;
  struct _M0TPB8MutLocalGiE* _M0L5startS922;
  int32_t _M0L3valS2074;
  int32_t _M0L6_2atmpS2075;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2079 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS920
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS920)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS920->$0 = _M0L6_2atmpS2079;
  _M0L3resS920->$1 = 0;
  _M0L1iS921
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS921)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS921->$0 = 0;
  _M0L5startS922
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS922)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS922->$0 = 0;
  while (1) {
    int32_t _M0L3valS2062 = _M0L1iS921->$0;
    int32_t _M0L6_2atmpS2063 = Moonbit_array_length(_M0L1sS918);
    if (_M0L3valS2062 < _M0L6_2atmpS2063) {
      int32_t _M0L3valS2066 = _M0L1iS921->$0;
      int32_t _M0L6_2atmpS2065;
      int32_t _M0L6_2atmpS2064;
      int32_t _M0L3valS2073;
      int32_t _M0L6_2atmpS2072;
      if (
        _M0L3valS2066 < 0
        || _M0L3valS2066 >= Moonbit_array_length(_M0L1sS918)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2065 = _M0L1sS918[_M0L3valS2066];
      _M0L6_2atmpS2064 = _M0L6_2atmpS2065;
      if (_M0L6_2atmpS2064 == _M0L3sepS919) {
        int32_t _M0L3valS2068 = _M0L5startS922->$0;
        int32_t _M0L3valS2069 = _M0L1iS921->$0;
        moonbit_string_t _M0L6_2atmpS2067;
        int32_t _M0L3valS2071;
        int32_t _M0L6_2atmpS2070;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS2067
        = _M0MPC16string6String17unsafe__substring(_M0L1sS918, _M0L3valS2068, _M0L3valS2069);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS920, _M0L6_2atmpS2067);
        _M0L3valS2071 = _M0L1iS921->$0;
        _M0L6_2atmpS2070 = _M0L3valS2071 + 1;
        _M0L5startS922->$0 = _M0L6_2atmpS2070;
      }
      _M0L3valS2073 = _M0L1iS921->$0;
      _M0L6_2atmpS2072 = _M0L3valS2073 + 1;
      _M0L1iS921->$0 = _M0L6_2atmpS2072;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS921);
    }
    break;
  }
  _M0L3valS2074 = _M0L5startS922->$0;
  _M0L6_2atmpS2075 = Moonbit_array_length(_M0L1sS918);
  if (_M0L3valS2074 < _M0L6_2atmpS2075) {
    int32_t _M0L3valS2077 = _M0L5startS922->$0;
    int32_t _M0L6_2atmpS2078;
    moonbit_string_t _M0L6_2atmpS2076;
    moonbit_decref_cycle_free(_M0L5startS922);
    _M0L6_2atmpS2078 = Moonbit_array_length(_M0L1sS918);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS2076
    = _M0MPC16string6String17unsafe__substring(_M0L1sS918, _M0L3valS2077, _M0L6_2atmpS2078);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS920, _M0L6_2atmpS2076);
  } else {
    moonbit_decref_cycle_free(_M0L5startS922);
  }
  return _M0L3resS920;
}

int32_t _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S910(
  int32_t _M0L6_2aenvS2054,
  moonbit_string_t _M0L1sS911
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS912;
  int32_t _M0L3lenS913;
  int32_t _M0L7_2abindS914;
  int32_t _M0L1iS915;
  int32_t _result_2152;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS912
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS912)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS912->$0 = 0;
  _M0L3lenS913 = Moonbit_array_length(_M0L1sS911);
  _M0L7_2abindS914 = 0;
  _M0L1iS915 = _M0L7_2abindS914;
  while (1) {
    if (_M0L1iS915 < _M0L3lenS913) {
      int32_t _M0L3valS2059 = _M0L3resS912->$0;
      int32_t _M0L6_2atmpS2056 = _M0L3valS2059 * 10;
      int32_t _M0L6_2atmpS2058;
      int32_t _M0L6_2atmpS2057;
      int32_t _M0L6_2atmpS2055;
      int32_t _M0L6_2atmpS2060;
      if (_M0L1iS915 < 0 || _M0L1iS915 >= Moonbit_array_length(_M0L1sS911)) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2058 = _M0L1sS911[_M0L1iS915];
      _M0L6_2atmpS2057 = _M0L6_2atmpS2058 - 48;
      _M0L6_2atmpS2055 = _M0L6_2atmpS2056 + _M0L6_2atmpS2057;
      _M0L3resS912->$0 = _M0L6_2atmpS2055;
      _M0L6_2atmpS2060 = _M0L1iS915 + 1;
      _M0L1iS915 = _M0L6_2atmpS2060;
      continue;
    }
    break;
  }
  _result_2152 = _M0L3resS912->$0;
  moonbit_decref_cycle_free(_M0L3resS912);
  return _result_2152;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS909
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS909);
  return _M0L4selfS909;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S879,
  moonbit_string_t _M0L12_2adiscard__S880,
  int32_t _M0L12_2adiscard__S881,
  struct _M0TWEu* _M0L12_2adiscard__S882,
  struct _M0TWssbEu* _M0L12_2adiscard__S883,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S884
) {
  struct moonbit_result_0 _result_2153;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_2153.tag = 1;
  _result_2153.data.ok = 0;
  return _result_2153;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S885,
  moonbit_string_t _M0L12_2adiscard__S886,
  int32_t _M0L12_2adiscard__S887,
  struct _M0TWEu* _M0L12_2adiscard__S888,
  struct _M0TWssbEu* _M0L12_2adiscard__S889,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S890
) {
  struct moonbit_result_0 _result_2154;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_2154.tag = 1;
  _result_2154.data.ok = 0;
  return _result_2154;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S891,
  moonbit_string_t _M0L12_2adiscard__S892,
  int32_t _M0L12_2adiscard__S893,
  struct _M0TWEu* _M0L12_2adiscard__S894,
  struct _M0TWssbEu* _M0L12_2adiscard__S895,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S896
) {
  struct moonbit_result_0 _result_2155;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_2155.tag = 1;
  _result_2155.data.ok = 0;
  return _result_2155;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S897,
  moonbit_string_t _M0L12_2adiscard__S898,
  int32_t _M0L12_2adiscard__S899,
  struct _M0TWEu* _M0L12_2adiscard__S900,
  struct _M0TWssbEu* _M0L12_2adiscard__S901,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S902
) {
  struct moonbit_result_0 _result_2156;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_2156.tag = 1;
  _result_2156.data.ok = 0;
  return _result_2156;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S903,
  moonbit_string_t _M0L12_2adiscard__S904,
  int32_t _M0L12_2adiscard__S905,
  struct _M0TWEu* _M0L12_2adiscard__S906,
  struct _M0TWssbEu* _M0L12_2adiscard__S907,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S908
) {
  struct moonbit_result_0 _result_2157;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_2157.tag = 1;
  _result_2157.data.ok = 0;
  return _result_2157;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S878
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter6custom(
  float _M0L2tmS864,
  float _M0L2vtS865,
  float _M0L2vrS866,
  float _M0L2elS867,
  float _M0L1rS868
) {
  float _M0L1cS862;
  float _M0L2glS863;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_2158;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS862 = -0x1p+0f;
  _M0L2glS863 = -0x1p+0f;
  _block_2158
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_2158)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2158->$0 = _M0L1cS862;
  _block_2158->$1 = _M0L2glS863;
  _block_2158->$2 = _M0L2tmS864;
  _block_2158->$3 = _M0L2vtS865;
  _block_2158->$4 = _M0L2vrS866;
  _block_2158->$5 = _M0L2elS867;
  _block_2158->$6 = _M0L1rS868;
  _block_2158->$7 = 0x1p+1f;
  _block_2158->$8 = 0x0p+0f;
  _block_2158->$9 = 0x0p+0f;
  _block_2158->$10 = 0x0p+0f;
  return _block_2158;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS836,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS838,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS841
) {
  struct _M0TPB5ArrayGfE* _M0L1vS835;
  float _M0L2vtS2052;
  float _M0L2vrS2053;
  float _M0L6spreadS837;
  int32_t _M0L7_2abindS839;
  int32_t _M0L1kS840;
  struct _M0TPB5ArrayGfE* _M0L1wS843;
  struct _M0TPB5ArrayGbE* _M0L4fireS844;
  struct _M0TPB5ArrayGiE* _M0L4tabsS845;
  struct _M0TPB5ArrayGfE* _M0L1iS846;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS847;
  struct _M0TPB5ArrayGfE* _M0L2geS848;
  struct _M0TPB5ArrayGfE* _M0L2giS849;
  struct _M0TPB5ArrayGfE* _M0L2heS850;
  struct _M0TPB5ArrayGfE* _M0L2hiS851;
  struct _M0TPB5ArrayGfE* _M0L3gluS852;
  struct _M0TPB5ArrayGfE* _M0L4gabaS853;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS854;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS855;
  float _M0L4e__eS856;
  float _M0L4e__iS857;
  float _M0L3treS858;
  float _M0L3tdeS859;
  float _M0L3triS860;
  float _M0L3tdiS861;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS2051;
  struct _M0TP26RiantR8snn__mbt2IF* _block_2160;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS835 = _M0MPC15array5Array4makeGfE(_M0L1nS836, 0x0p+0f);
  _M0L2vtS2052 = _M0L5paramS838->$3;
  _M0L2vrS2053 = _M0L5paramS838->$4;
  _M0L6spreadS837 = _M0L2vtS2052 - _M0L2vrS2053;
  _M0L7_2abindS839 = 0;
  _M0L1kS840 = _M0L7_2abindS839;
  while (1) {
    if (_M0L1kS840 < _M0L1nS836) {
      float _M0L2vrS2047 = _M0L5paramS838->$4;
      float _M0L6_2atmpS2049;
      float _M0L6_2atmpS2048;
      float _M0L6_2atmpS2046;
      int32_t _M0L6_2atmpS2050;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2049 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS841);
      _M0L6_2atmpS2048 = _M0L6_2atmpS2049 * _M0L6spreadS837;
      _M0L6_2atmpS2046 = _M0L2vrS2047 + _M0L6_2atmpS2048;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS835, _M0L1kS840, _M0L6_2atmpS2046);
      _M0L6_2atmpS2050 = _M0L1kS840 + 1;
      _M0L1kS840 = _M0L6_2atmpS2050;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS843 = _M0MPC15array5Array4makeGfE(_M0L1nS836, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS844 = _M0MPC15array5Array4makeGbE(_M0L1nS836, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS845 = _M0MPC15array5Array4makeGiE(_M0L1nS836, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS846 = _M0MPC15array5Array4makeGfE(_M0L1nS836, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS847 = _M0MPC15array5Array4makeGfE(_M0L1nS836, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS848 = _M0MPC15array5Array4makeGfE(_M0L1nS836, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS849 = _M0MPC15array5Array4makeGfE(_M0L1nS836, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS850 = _M0MPC15array5Array4makeGfE(_M0L1nS836, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS851 = _M0MPC15array5Array4makeGfE(_M0L1nS836, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS852 = _M0MPC15array5Array4makeGfE(_M0L1nS836, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS853 = _M0MPC15array5Array4makeGfE(_M0L1nS836, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS854 = _M0MPC15array5Array4makeGfE(_M0L1nS836, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS855 = _M0MPC15array5Array4makeGfE(_M0L1nS836, 0x1p+0f);
  _M0L4e__eS856 = 0x0p+0f;
  _M0L4e__iS857 = -0x1.2cp+6f;
  _M0L3treS858 = 0x1p+0f;
  _M0L3tdeS859 = 0x1.8p+2f;
  _M0L3triS860 = 0x1p-1f;
  _M0L3tdiS861 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS2051 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS838);
  _block_2160
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_2160)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2160->$0 = _M0L5paramS838;
  _block_2160->$1 = _M0L6_2atmpS2051;
  _block_2160->$2 = _M0L1nS836;
  _block_2160->$3 = _M0L1vS835;
  _block_2160->$4 = _M0L1wS843;
  _block_2160->$5 = _M0L4fireS844;
  _block_2160->$6 = _M0L4tabsS845;
  _block_2160->$7 = _M0L1iS846;
  _block_2160->$8 = _M0L9syn__currS847;
  _block_2160->$9 = _M0L2geS848;
  _block_2160->$10 = _M0L2giS849;
  _block_2160->$11 = _M0L2heS850;
  _block_2160->$12 = _M0L2hiS851;
  _block_2160->$13 = _M0L3gluS852;
  _block_2160->$14 = _M0L4gabaS853;
  _block_2160->$15 = _M0L7gsyn__eS854;
  _block_2160->$16 = _M0L7gsyn__iS855;
  _block_2160->$17 = _M0L4e__eS856;
  _block_2160->$18 = _M0L4e__iS857;
  _block_2160->$19 = _M0L3treS858;
  _block_2160->$20 = _M0L3tdeS859;
  _block_2160->$21 = _M0L3triS860;
  _block_2160->$22 = _M0L3tdiS861;
  return _block_2160;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_2161;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_2161
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_2161)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2161->$0 = 0x1p+1f;
  return _block_2161;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS831
) {
  int32_t _M0L1nS830;
  int32_t _M0L7_2abindS832;
  int32_t _M0L1iS833;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS830 = _M0L1pS831->$2;
  _M0L7_2abindS832 = 0;
  _M0L1iS833 = _M0L7_2abindS832;
  while (1) {
    if (_M0L1iS833 < _M0L1nS830) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2023 = _M0L1pS831->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS2044 = _M0L1pS831->$9;
      float _M0L6_2atmpS2039;
      struct _M0TPB5ArrayGfE* _M0L1vS2043;
      float _M0L6_2atmpS2041;
      float _M0L4e__eS2042;
      float _M0L6_2atmpS2040;
      float _M0L6_2atmpS2036;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS2038;
      float _M0L6_2atmpS2037;
      float _M0L6_2atmpS2025;
      struct _M0TPB5ArrayGfE* _M0L2giS2035;
      float _M0L6_2atmpS2030;
      struct _M0TPB5ArrayGfE* _M0L1vS2034;
      float _M0L6_2atmpS2032;
      float _M0L4e__iS2033;
      float _M0L6_2atmpS2031;
      float _M0L6_2atmpS2027;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS2029;
      float _M0L6_2atmpS2028;
      float _M0L6_2atmpS2026;
      float _M0L6_2atmpS2024;
      int32_t _M0L6_2atmpS2045;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2039 = _M0MPC15array5Array2atGfE(_M0L2geS2044, _M0L1iS833);
      _M0L1vS2043 = _M0L1pS831->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2041 = _M0MPC15array5Array2atGfE(_M0L1vS2043, _M0L1iS833);
      _M0L4e__eS2042 = _M0L1pS831->$17;
      _M0L6_2atmpS2040 = _M0L6_2atmpS2041 - _M0L4e__eS2042;
      _M0L6_2atmpS2036 = _M0L6_2atmpS2039 * _M0L6_2atmpS2040;
      _M0L7gsyn__eS2038 = _M0L1pS831->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2037
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS2038, _M0L1iS833);
      _M0L6_2atmpS2025 = _M0L6_2atmpS2036 * _M0L6_2atmpS2037;
      _M0L2giS2035 = _M0L1pS831->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2030 = _M0MPC15array5Array2atGfE(_M0L2giS2035, _M0L1iS833);
      _M0L1vS2034 = _M0L1pS831->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2032 = _M0MPC15array5Array2atGfE(_M0L1vS2034, _M0L1iS833);
      _M0L4e__iS2033 = _M0L1pS831->$18;
      _M0L6_2atmpS2031 = _M0L6_2atmpS2032 - _M0L4e__iS2033;
      _M0L6_2atmpS2027 = _M0L6_2atmpS2030 * _M0L6_2atmpS2031;
      _M0L7gsyn__iS2029 = _M0L1pS831->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2028
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS2029, _M0L1iS833);
      _M0L6_2atmpS2026 = _M0L6_2atmpS2027 * _M0L6_2atmpS2028;
      _M0L6_2atmpS2024 = _M0L6_2atmpS2025 + _M0L6_2atmpS2026;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS2023, _M0L1iS833, _M0L6_2atmpS2024);
      _M0L6_2atmpS2045 = _M0L1iS833 + 1;
      _M0L1iS833 = _M0L6_2atmpS2045;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS822,
  float _M0L2dtS825
) {
  int32_t _M0L1nS821;
  int32_t _M0L7_2abindS823;
  int32_t _M0L1iS824;
  int32_t _M0L7_2abindS827;
  int32_t _M0L1iS828;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS821 = _M0L1pS822->$2;
  _M0L7_2abindS823 = 0;
  _M0L1iS824 = _M0L7_2abindS823;
  while (1) {
    if (_M0L1iS824 < _M0L1nS821) {
      struct _M0TPB5ArrayGfE* _M0L2heS1961 = _M0L1pS822->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS1966 = _M0L1pS822->$11;
      float _M0L6_2atmpS1963;
      struct _M0TPB5ArrayGfE* _M0L3gluS1965;
      float _M0L6_2atmpS1964;
      float _M0L6_2atmpS1962;
      struct _M0TPB5ArrayGfE* _M0L2hiS1967;
      struct _M0TPB5ArrayGfE* _M0L2hiS1972;
      float _M0L6_2atmpS1969;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1971;
      float _M0L6_2atmpS1970;
      float _M0L6_2atmpS1968;
      struct _M0TPB5ArrayGfE* _M0L2geS1973;
      struct _M0TPB5ArrayGfE* _M0L2geS1985;
      float _M0L6_2atmpS1975;
      struct _M0TPB5ArrayGfE* _M0L2geS1984;
      float _M0L6_2atmpS1983;
      float _M0L6_2atmpS1981;
      float _M0L3tdeS1982;
      float _M0L6_2atmpS1978;
      struct _M0TPB5ArrayGfE* _M0L2heS1980;
      float _M0L6_2atmpS1979;
      float _M0L6_2atmpS1977;
      float _M0L6_2atmpS1976;
      float _M0L6_2atmpS1974;
      struct _M0TPB5ArrayGfE* _M0L2heS1986;
      struct _M0TPB5ArrayGfE* _M0L2heS1995;
      float _M0L6_2atmpS1988;
      struct _M0TPB5ArrayGfE* _M0L2heS1994;
      float _M0L6_2atmpS1993;
      float _M0L6_2atmpS1991;
      float _M0L3treS1992;
      float _M0L6_2atmpS1990;
      float _M0L6_2atmpS1989;
      float _M0L6_2atmpS1987;
      struct _M0TPB5ArrayGfE* _M0L2giS1996;
      struct _M0TPB5ArrayGfE* _M0L2giS2008;
      float _M0L6_2atmpS1998;
      struct _M0TPB5ArrayGfE* _M0L2giS2007;
      float _M0L6_2atmpS2006;
      float _M0L6_2atmpS2004;
      float _M0L3tdiS2005;
      float _M0L6_2atmpS2001;
      struct _M0TPB5ArrayGfE* _M0L2hiS2003;
      float _M0L6_2atmpS2002;
      float _M0L6_2atmpS2000;
      float _M0L6_2atmpS1999;
      float _M0L6_2atmpS1997;
      struct _M0TPB5ArrayGfE* _M0L2hiS2009;
      struct _M0TPB5ArrayGfE* _M0L2hiS2018;
      float _M0L6_2atmpS2011;
      struct _M0TPB5ArrayGfE* _M0L2hiS2017;
      float _M0L6_2atmpS2016;
      float _M0L6_2atmpS2014;
      float _M0L3triS2015;
      float _M0L6_2atmpS2013;
      float _M0L6_2atmpS2012;
      float _M0L6_2atmpS2010;
      int32_t _M0L6_2atmpS2019;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1963 = _M0MPC15array5Array2atGfE(_M0L2heS1966, _M0L1iS824);
      _M0L3gluS1965 = _M0L1pS822->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1964 = _M0MPC15array5Array2atGfE(_M0L3gluS1965, _M0L1iS824);
      _M0L6_2atmpS1962 = _M0L6_2atmpS1963 + _M0L6_2atmpS1964;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1961, _M0L1iS824, _M0L6_2atmpS1962);
      _M0L2hiS1967 = _M0L1pS822->$12;
      _M0L2hiS1972 = _M0L1pS822->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1969 = _M0MPC15array5Array2atGfE(_M0L2hiS1972, _M0L1iS824);
      _M0L4gabaS1971 = _M0L1pS822->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1970
      = _M0MPC15array5Array2atGfE(_M0L4gabaS1971, _M0L1iS824);
      _M0L6_2atmpS1968 = _M0L6_2atmpS1969 + _M0L6_2atmpS1970;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1967, _M0L1iS824, _M0L6_2atmpS1968);
      _M0L2geS1973 = _M0L1pS822->$9;
      _M0L2geS1985 = _M0L1pS822->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1975 = _M0MPC15array5Array2atGfE(_M0L2geS1985, _M0L1iS824);
      _M0L2geS1984 = _M0L1pS822->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1983 = _M0MPC15array5Array2atGfE(_M0L2geS1984, _M0L1iS824);
      _M0L6_2atmpS1981 = -_M0L6_2atmpS1983;
      _M0L3tdeS1982 = _M0L1pS822->$20;
      _M0L6_2atmpS1978 = _M0L6_2atmpS1981 / _M0L3tdeS1982;
      _M0L2heS1980 = _M0L1pS822->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1979 = _M0MPC15array5Array2atGfE(_M0L2heS1980, _M0L1iS824);
      _M0L6_2atmpS1977 = _M0L6_2atmpS1978 + _M0L6_2atmpS1979;
      _M0L6_2atmpS1976 = _M0L2dtS825 * _M0L6_2atmpS1977;
      _M0L6_2atmpS1974 = _M0L6_2atmpS1975 + _M0L6_2atmpS1976;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1973, _M0L1iS824, _M0L6_2atmpS1974);
      _M0L2heS1986 = _M0L1pS822->$11;
      _M0L2heS1995 = _M0L1pS822->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1988 = _M0MPC15array5Array2atGfE(_M0L2heS1995, _M0L1iS824);
      _M0L2heS1994 = _M0L1pS822->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1993 = _M0MPC15array5Array2atGfE(_M0L2heS1994, _M0L1iS824);
      _M0L6_2atmpS1991 = -_M0L6_2atmpS1993;
      _M0L3treS1992 = _M0L1pS822->$19;
      _M0L6_2atmpS1990 = _M0L6_2atmpS1991 / _M0L3treS1992;
      _M0L6_2atmpS1989 = _M0L2dtS825 * _M0L6_2atmpS1990;
      _M0L6_2atmpS1987 = _M0L6_2atmpS1988 + _M0L6_2atmpS1989;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1986, _M0L1iS824, _M0L6_2atmpS1987);
      _M0L2giS1996 = _M0L1pS822->$10;
      _M0L2giS2008 = _M0L1pS822->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1998 = _M0MPC15array5Array2atGfE(_M0L2giS2008, _M0L1iS824);
      _M0L2giS2007 = _M0L1pS822->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2006 = _M0MPC15array5Array2atGfE(_M0L2giS2007, _M0L1iS824);
      _M0L6_2atmpS2004 = -_M0L6_2atmpS2006;
      _M0L3tdiS2005 = _M0L1pS822->$22;
      _M0L6_2atmpS2001 = _M0L6_2atmpS2004 / _M0L3tdiS2005;
      _M0L2hiS2003 = _M0L1pS822->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2002 = _M0MPC15array5Array2atGfE(_M0L2hiS2003, _M0L1iS824);
      _M0L6_2atmpS2000 = _M0L6_2atmpS2001 + _M0L6_2atmpS2002;
      _M0L6_2atmpS1999 = _M0L2dtS825 * _M0L6_2atmpS2000;
      _M0L6_2atmpS1997 = _M0L6_2atmpS1998 + _M0L6_2atmpS1999;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1996, _M0L1iS824, _M0L6_2atmpS1997);
      _M0L2hiS2009 = _M0L1pS822->$12;
      _M0L2hiS2018 = _M0L1pS822->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2011 = _M0MPC15array5Array2atGfE(_M0L2hiS2018, _M0L1iS824);
      _M0L2hiS2017 = _M0L1pS822->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2016 = _M0MPC15array5Array2atGfE(_M0L2hiS2017, _M0L1iS824);
      _M0L6_2atmpS2014 = -_M0L6_2atmpS2016;
      _M0L3triS2015 = _M0L1pS822->$21;
      _M0L6_2atmpS2013 = _M0L6_2atmpS2014 / _M0L3triS2015;
      _M0L6_2atmpS2012 = _M0L2dtS825 * _M0L6_2atmpS2013;
      _M0L6_2atmpS2010 = _M0L6_2atmpS2011 + _M0L6_2atmpS2012;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2009, _M0L1iS824, _M0L6_2atmpS2010);
      _M0L6_2atmpS2019 = _M0L1iS824 + 1;
      _M0L1iS824 = _M0L6_2atmpS2019;
      continue;
    }
    break;
  }
  _M0L7_2abindS827 = 0;
  _M0L1iS828 = _M0L7_2abindS827;
  while (1) {
    if (_M0L1iS828 < _M0L1nS821) {
      struct _M0TPB5ArrayGfE* _M0L3gluS2020 = _M0L1pS822->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2021;
      int32_t _M0L6_2atmpS2022;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS2020, _M0L1iS828, 0x0p+0f);
      _M0L4gabaS2021 = _M0L1pS822->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS2021, _M0L1iS828, 0x0p+0f);
      _M0L6_2atmpS2022 = _M0L1iS828 + 1;
      _M0L1iS828 = _M0L6_2atmpS2022;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS807,
  float _M0L2dtS816
) {
  int32_t _M0L1nS806;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S808;
  float _M0L2tmS809;
  float _M0L2elS810;
  float _M0L1rS811;
  float _M0L2vtS812;
  float _M0L2vrS813;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS1960;
  float _M0L11tabs__constS814;
  float _M0L6_2atmpS1959;
  int32_t _M0L11tabs__stepsS815;
  int32_t _M0L7_2abindS817;
  int32_t _M0L1iS818;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS806 = _M0L1pS807->$2;
  _M0L3p__S808 = _M0L1pS807->$0;
  _M0L2tmS809 = _M0L3p__S808->$2;
  _M0L2elS810 = _M0L3p__S808->$5;
  _M0L1rS811 = _M0L3p__S808->$6;
  _M0L2vtS812 = _M0L3p__S808->$3;
  _M0L2vrS813 = _M0L3p__S808->$4;
  _M0L5spikeS1960 = _M0L1pS807->$1;
  _M0L11tabs__constS814 = _M0L5spikeS1960->$0;
  _M0L6_2atmpS1959 = _M0L11tabs__constS814 / _M0L2dtS816;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS815 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1959);
  _M0L7_2abindS817 = 0;
  _M0L1iS818 = _M0L7_2abindS817;
  while (1) {
    if (_M0L1iS818 < _M0L1nS806) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS1919 = _M0L1pS807->$6;
      int32_t _M0L6_2atmpS1918;
      struct _M0TPB5ArrayGfE* _M0L1vS1925;
      struct _M0TPB5ArrayGfE* _M0L1vS1946;
      float _M0L6_2atmpS1927;
      float _M0L6_2atmpS1929;
      struct _M0TPB5ArrayGfE* _M0L1vS1945;
      float _M0L6_2atmpS1944;
      float _M0L6_2atmpS1943;
      float _M0L6_2atmpS1935;
      struct _M0TPB5ArrayGfE* _M0L1wS1942;
      float _M0L6_2atmpS1941;
      float _M0L6_2atmpS1938;
      struct _M0TPB5ArrayGfE* _M0L1iS1940;
      float _M0L6_2atmpS1939;
      float _M0L6_2atmpS1937;
      float _M0L6_2atmpS1936;
      float _M0L6_2atmpS1931;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1934;
      float _M0L6_2atmpS1933;
      float _M0L6_2atmpS1932;
      float _M0L6_2atmpS1930;
      float _M0L6_2atmpS1928;
      float _M0L6_2atmpS1926;
      struct _M0TPB5ArrayGbE* _M0L4fireS1947;
      struct _M0TPB5ArrayGfE* _M0L1vS1950;
      float _M0L6_2atmpS1949;
      int32_t _M0L6_2atmpS1948;
      struct _M0TPB5ArrayGfE* _M0L1vS1951;
      struct _M0TPB5ArrayGbE* _M0L4fireS1953;
      float _M0L6_2atmpS1952;
      struct _M0TPB5ArrayGiE* _M0L4tabsS1955;
      struct _M0TPB5ArrayGbE* _M0L4fireS1957;
      int32_t _M0L6_2atmpS1956;
      int32_t _M0L6_2atmpS1917;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1918
      = _M0MPC15array5Array2atGiE(_M0L4tabsS1919, _M0L1iS818);
      if (_M0L6_2atmpS1918 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS1920 = _M0L1pS807->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1921;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1924;
        int32_t _M0L6_2atmpS1923;
        int32_t _M0L6_2atmpS1922;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS1920, _M0L1iS818, 0);
        _M0L4tabsS1921 = _M0L1pS807->$6;
        _M0L4tabsS1924 = _M0L1pS807->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1923
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1924, _M0L1iS818);
        _M0L6_2atmpS1922 = _M0L6_2atmpS1923 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS1921, _M0L1iS818, _M0L6_2atmpS1922);
        goto join_819;
      }
      _M0L1vS1925 = _M0L1pS807->$3;
      _M0L1vS1946 = _M0L1pS807->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1927 = _M0MPC15array5Array2atGfE(_M0L1vS1946, _M0L1iS818);
      _M0L6_2atmpS1929 = _M0L2dtS816 / _M0L2tmS809;
      _M0L1vS1945 = _M0L1pS807->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1944 = _M0MPC15array5Array2atGfE(_M0L1vS1945, _M0L1iS818);
      _M0L6_2atmpS1943 = _M0L6_2atmpS1944 - _M0L2elS810;
      _M0L6_2atmpS1935 = -_M0L6_2atmpS1943;
      _M0L1wS1942 = _M0L1pS807->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1941 = _M0MPC15array5Array2atGfE(_M0L1wS1942, _M0L1iS818);
      _M0L6_2atmpS1938 = -_M0L6_2atmpS1941;
      _M0L1iS1940 = _M0L1pS807->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1939 = _M0MPC15array5Array2atGfE(_M0L1iS1940, _M0L1iS818);
      _M0L6_2atmpS1937 = _M0L6_2atmpS1938 + _M0L6_2atmpS1939;
      _M0L6_2atmpS1936 = _M0L1rS811 * _M0L6_2atmpS1937;
      _M0L6_2atmpS1931 = _M0L6_2atmpS1935 + _M0L6_2atmpS1936;
      _M0L9syn__currS1934 = _M0L1pS807->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1933
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS1934, _M0L1iS818);
      _M0L6_2atmpS1932 = _M0L1rS811 * _M0L6_2atmpS1933;
      _M0L6_2atmpS1930 = _M0L6_2atmpS1931 - _M0L6_2atmpS1932;
      _M0L6_2atmpS1928 = _M0L6_2atmpS1929 * _M0L6_2atmpS1930;
      _M0L6_2atmpS1926 = _M0L6_2atmpS1927 + _M0L6_2atmpS1928;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1925, _M0L1iS818, _M0L6_2atmpS1926);
      _M0L4fireS1947 = _M0L1pS807->$5;
      _M0L1vS1950 = _M0L1pS807->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1949 = _M0MPC15array5Array2atGfE(_M0L1vS1950, _M0L1iS818);
      _M0L6_2atmpS1948 = _M0L6_2atmpS1949 > _M0L2vtS812;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1947, _M0L1iS818, _M0L6_2atmpS1948);
      _M0L1vS1951 = _M0L1pS807->$3;
      _M0L4fireS1953 = _M0L1pS807->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1953, _M0L1iS818)) {
        _M0L6_2atmpS1952 = _M0L2vrS813;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS1954 = _M0L1pS807->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1952 = _M0MPC15array5Array2atGfE(_M0L1vS1954, _M0L1iS818);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1951, _M0L1iS818, _M0L6_2atmpS1952);
      _M0L4tabsS1955 = _M0L1pS807->$6;
      _M0L4fireS1957 = _M0L1pS807->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1957, _M0L1iS818)) {
        _M0L6_2atmpS1956 = _M0L11tabs__stepsS815;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS1958 = _M0L1pS807->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1956
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1958, _M0L1iS818);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS1955, _M0L1iS818, _M0L6_2atmpS1956);
      goto join_819;
      goto joinlet_2166;
      join_819:;
      _M0L6_2atmpS1917 = _M0L1iS818 + 1;
      _M0L1iS818 = _M0L6_2atmpS1917;
      continue;
      joinlet_2166:;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS804
) {
  struct _M0TUmmmmE* _M0L1sS803;
  uint64_t _M0L6_2atmpS1916;
  struct _M0TUmmmmE* _M0L1tS805;
  uint64_t _M0L6_2atmpS1912;
  uint64_t _M0L6_2atmpS1913;
  uint64_t _M0L6_2atmpS1914;
  uint64_t _M0L6_2atmpS1915;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2167;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS803 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS804);
  _M0L6_2atmpS1916 = _M0L1sS803->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS805 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS1916);
  _M0L6_2atmpS1912 = _M0L1sS803->$0;
  _M0L6_2atmpS1913 = _M0L1sS803->$1;
  _M0L6_2atmpS1914 = _M0L1sS803->$2;
  moonbit_decref_cycle_free(_M0L1sS803);
  _M0L6_2atmpS1915 = _M0L1tS805->$0;
  moonbit_decref_cycle_free(_M0L1tS805);
  _block_2167
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2167)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2167->$0 = _M0L6_2atmpS1912;
  _block_2167->$1 = _M0L6_2atmpS1913;
  _block_2167->$2 = _M0L6_2atmpS1914;
  _block_2167->$3 = _M0L6_2atmpS1915;
  return _block_2167;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS795) {
  uint64_t _M0L2s1S794;
  uint64_t _M0L2z1S796;
  uint64_t _M0L2s2S797;
  uint64_t _M0L2z2S798;
  uint64_t _M0L2s3S799;
  uint64_t _M0L2z3S800;
  uint64_t _M0L2s4S801;
  uint64_t _M0L2z4S802;
  struct _M0TUmmmmE* _block_2168;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S794 = _M0L4seedS795 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S796 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S794);
  _M0L2s2S797 = _M0L2s1S794 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S798 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S797);
  _M0L2s3S799 = _M0L2s2S797 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S800 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S799);
  _M0L2s4S801 = _M0L2s3S799 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S802 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S801);
  _block_2168 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2168)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2168->$0 = _M0L2z1S796;
  _block_2168->$1 = _M0L2z2S798;
  _block_2168->$2 = _M0L2z3S800;
  _block_2168->$3 = _M0L2z4S802;
  return _block_2168;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS792) {
  uint64_t _M0L6_2atmpS1911;
  uint64_t _M0L6_2atmpS1910;
  uint64_t _M0L1zS791;
  uint64_t _M0L6_2atmpS1909;
  uint64_t _M0L6_2atmpS1908;
  uint64_t _M0L1zS793;
  uint64_t _M0L6_2atmpS1907;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1911 = _M0L1zS792 >> 30;
  _M0L6_2atmpS1910 = _M0L1zS792 ^ _M0L6_2atmpS1911;
  _M0L1zS791 = _M0L6_2atmpS1910 * 13787848793156543929ull;
  _M0L6_2atmpS1909 = _M0L1zS791 >> 27;
  _M0L6_2atmpS1908 = _M0L1zS791 ^ _M0L6_2atmpS1909;
  _M0L1zS793 = _M0L6_2atmpS1908 * 10723151780598845931ull;
  _M0L6_2atmpS1907 = _M0L1zS793 >> 31;
  return _M0L1zS793 ^ _M0L6_2atmpS1907;
}

int32_t _M0FP26RiantR8snn__mbt20stimulate__spiketime(
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L1sS785,
  float _M0L1tS787,
  float _M0L1wS789
) {
  struct _M0TPB8MutLocalGiE* _M0L1iS784;
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L1iS784
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS784)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS784->$0 = 0;
  while (1) {
    int32_t _M0L3valS1869 = _M0L1iS784->$0;
    int32_t _M0L1nS1870 = _M0L1sS785->$0;
    if (_M0L3valS1869 < _M0L1nS1870) {
      struct _M0TPB5ArrayGbE* _M0L4fireS1871 = _M0L1sS785->$4;
      int32_t _M0L3valS1872 = _M0L1iS784->$0;
      int32_t _M0L3valS1874;
      int32_t _M0L6_2atmpS1873;
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1871, _M0L3valS1872, 0);
      _M0L3valS1874 = _M0L1iS784->$0;
      _M0L6_2atmpS1873 = _M0L3valS1874 + 1;
      _M0L1iS784->$0 = _M0L6_2atmpS1873;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS784);
    }
    break;
  }
  while (1) {
    struct _M0TPB5ArrayGiE* _M0L11next__indexS1878 = _M0L1sS785->$3;
    int32_t _M0L6_2atmpS1877;
    int32_t _if__result_2171;
    #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
    _M0L6_2atmpS1877 = _M0MPC15array5Array2atGiE(_M0L11next__indexS1878, 0);
    if (_M0L6_2atmpS1877 >= 0) {
      struct _M0TPB5ArrayGfE* _M0L11next__spikeS1876 = _M0L1sS785->$2;
      float _M0L6_2atmpS1875;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS1875 = _M0MPC15array5Array2atGfE(_M0L11next__spikeS1876, 0);
      _if__result_2171 = _M0L6_2atmpS1875 <= _M0L1tS787;
    } else {
      _if__result_2171 = 0;
    }
    if (_if__result_2171) {
      struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS1906 =
        _M0L1sS785->$1;
      struct _M0TPB5ArrayGiE* _M0L7neuronsS1903 = _M0L5paramS1906->$1;
      struct _M0TPB5ArrayGiE* _M0L11next__indexS1905 = _M0L1sS785->$3;
      int32_t _M0L6_2atmpS1904;
      int32_t _M0L1jS788;
      struct _M0TPB5ArrayGbE* _M0L4fireS1879;
      struct _M0TPB5ArrayGfE* _M0L1gS1880;
      struct _M0TPB5ArrayGfE* _M0L1gS1883;
      float _M0L6_2atmpS1882;
      float _M0L6_2atmpS1881;
      struct _M0TPB5ArrayGiE* _M0L11next__indexS1889;
      int32_t _M0L6_2atmpS1888;
      int32_t _M0L6_2atmpS1884;
      struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS1887;
      struct _M0TPB5ArrayGfE* _M0L10spiketimesS1886;
      int32_t _M0L6_2atmpS1885;
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS1904 = _M0MPC15array5Array2atGiE(_M0L11next__indexS1905, 0);
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L1jS788
      = _M0MPC15array5Array2atGiE(_M0L7neuronsS1903, _M0L6_2atmpS1904);
      _M0L4fireS1879 = _M0L1sS785->$4;
      #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1879, _M0L1jS788, 1);
      _M0L1gS1880 = _M0L1sS785->$5;
      _M0L1gS1883 = _M0L1sS785->$5;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS1882 = _M0MPC15array5Array2atGfE(_M0L1gS1883, _M0L1jS788);
      _M0L6_2atmpS1881 = _M0L6_2atmpS1882 + _M0L1wS789;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS1880, _M0L1jS788, _M0L6_2atmpS1881);
      _M0L11next__indexS1889 = _M0L1sS785->$3;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS1888 = _M0MPC15array5Array2atGiE(_M0L11next__indexS1889, 0);
      _M0L6_2atmpS1884 = _M0L6_2atmpS1888 + 1;
      _M0L5paramS1887 = _M0L1sS785->$1;
      _M0L10spiketimesS1886 = _M0L5paramS1887->$0;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS1885 = _M0MPC15array5Array6lengthGfE(_M0L10spiketimesS1886);
      if (_M0L6_2atmpS1884 < _M0L6_2atmpS1885) {
        struct _M0TPB5ArrayGiE* _M0L11next__indexS1890 = _M0L1sS785->$3;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS1893 = _M0L1sS785->$3;
        int32_t _M0L6_2atmpS1892;
        int32_t _M0L6_2atmpS1891;
        struct _M0TPB5ArrayGfE* _M0L11next__spikeS1894;
        struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS1899;
        struct _M0TPB5ArrayGfE* _M0L10spiketimesS1896;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS1898;
        int32_t _M0L6_2atmpS1897;
        float _M0L6_2atmpS1895;
        #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS1892
        = _M0MPC15array5Array2atGiE(_M0L11next__indexS1893, 0);
        _M0L6_2atmpS1891 = _M0L6_2atmpS1892 + 1;
        #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGiE(_M0L11next__indexS1890, 0, _M0L6_2atmpS1891);
        _M0L11next__spikeS1894 = _M0L1sS785->$2;
        _M0L5paramS1899 = _M0L1sS785->$1;
        _M0L10spiketimesS1896 = _M0L5paramS1899->$0;
        _M0L11next__indexS1898 = _M0L1sS785->$3;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS1897
        = _M0MPC15array5Array2atGiE(_M0L11next__indexS1898, 0);
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS1895
        = _M0MPC15array5Array2atGfE(_M0L10spiketimesS1896, _M0L6_2atmpS1897);
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGfE(_M0L11next__spikeS1894, 0, _M0L6_2atmpS1895);
      } else {
        struct _M0TPB5ArrayGfE* _M0L11next__spikeS1900 = _M0L1sS785->$2;
        float _M0L6_2atmpS1901 = 0x0p+0f / (float)MOONBIT_ZERO;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS1902;
        #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGfE(_M0L11next__spikeS1900, 0, _M0L6_2atmpS1901);
        _M0L11next__indexS1902 = _M0L1sS785->$3;
        #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGiE(_M0L11next__indexS1902, 0, -1);
      }
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0MP26RiantR8snn__mbt17SpikeTimeStimulus3new(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L6e__popS775,
  moonbit_string_t _M0L3symS781,
  struct _M0TPB5ArrayGfE* _M0L10spiketimesS777,
  struct _M0TPB5ArrayGiE* _M0L7neuronsS778
) {
  int32_t _M0L1nS774;
  struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS776;
  struct _M0TPB5ArrayGbE* _M0L4fireS779;
  struct _M0TPB5ArrayGfE* _M0L1gS780;
  struct _M0TPB5ArrayGfE* _M0L10spiketimesS1863;
  int32_t _M0L6_2atmpS1862;
  struct _M0TPB5ArrayGfE* _M0L11next__spikeS782;
  struct _M0TPB5ArrayGfE* _M0L10spiketimesS1859;
  int32_t _M0L6_2atmpS1858;
  struct _M0TPB5ArrayGiE* _M0L11next__indexS783;
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _block_2172;
  #line 79 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L1nS774 = _M0L6e__popS775->$2;
  #line 86 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L5paramS776
  = _M0MP26RiantR8snn__mbt18SpikeTimeParameter3new(_M0L10spiketimesS777, _M0L7neuronsS778);
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L4fireS779 = _M0MPC15array5Array4makeGbE(_M0L1nS774, 0);
  #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  if (
    _M0L3symS781 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS781)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS781, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS781) * 2)
  ) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2108 = _M0L6e__popS775->$13;
    moonbit_incref_cycle_free(_M0L8_2afieldS2108);
    _M0L1gS780 = _M0L8_2afieldS2108;
  } else {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2109 = _M0L6e__popS775->$14;
    moonbit_incref_cycle_free(_M0L8_2afieldS2109);
    _M0L1gS780 = _M0L8_2afieldS2109;
  }
  _M0L10spiketimesS1863 = _M0L5paramS776->$0;
  moonbit_incref_cycle_free(_M0L10spiketimesS1863);
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L6_2atmpS1862 = _M0MPC15array5Array6lengthGfE(_M0L10spiketimesS1863);
  moonbit_decref_cycle_free(_M0L10spiketimesS1863);
  if (_M0L6_2atmpS1862 > 0) {
    struct _M0TPB5ArrayGfE* _M0L10spiketimesS1866 = _M0L5paramS776->$0;
    float _M0L6_2atmpS1865;
    float* _M0L6_2atmpS1864;
    moonbit_incref_cycle_free(_M0L10spiketimesS1866);
    #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
    _M0L6_2atmpS1865 = _M0MPC15array5Array2atGfE(_M0L10spiketimesS1866, 0);
    moonbit_decref_cycle_free(_M0L10spiketimesS1866);
    _M0L6_2atmpS1864 = (float*)moonbit_make_float_array_raw(1);
    _M0L6_2atmpS1864[0] = _M0L6_2atmpS1865;
    _M0L11next__spikeS782
    = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
    Moonbit_object_header(_M0L11next__spikeS782)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
    _M0L11next__spikeS782->$0 = _M0L6_2atmpS1864;
    _M0L11next__spikeS782->$1 = 1;
  } else {
    float _M0L6_2atmpS1868 = 0x0p+0f / (float)MOONBIT_ZERO;
    float* _M0L6_2atmpS1867 = (float*)moonbit_make_float_array_raw(1);
    _M0L6_2atmpS1867[0] = _M0L6_2atmpS1868;
    _M0L11next__spikeS782
    = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
    Moonbit_object_header(_M0L11next__spikeS782)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
    _M0L11next__spikeS782->$0 = _M0L6_2atmpS1867;
    _M0L11next__spikeS782->$1 = 1;
  }
  _M0L10spiketimesS1859 = _M0L5paramS776->$0;
  moonbit_incref_cycle_free(_M0L10spiketimesS1859);
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L6_2atmpS1858 = _M0MPC15array5Array6lengthGfE(_M0L10spiketimesS1859);
  moonbit_decref_cycle_free(_M0L10spiketimesS1859);
  if (_M0L6_2atmpS1858 > 0) {
    int32_t* _M0L6_2atmpS1860 = (int32_t*)moonbit_make_int32_array_raw(1);
    _M0L6_2atmpS1860[0] = 0;
    _M0L11next__indexS783
    = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
    Moonbit_object_header(_M0L11next__indexS783)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
    _M0L11next__indexS783->$0 = _M0L6_2atmpS1860;
    _M0L11next__indexS783->$1 = 1;
  } else {
    int32_t* _M0L6_2atmpS1861 = (int32_t*)moonbit_make_int32_array_raw(1);
    _M0L6_2atmpS1861[0] = -1;
    _M0L11next__indexS783
    = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
    Moonbit_object_header(_M0L11next__indexS783)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
    _M0L11next__indexS783->$0 = _M0L6_2atmpS1861;
    _M0L11next__indexS783->$1 = 1;
  }
  _block_2172
  = (struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus));
  Moonbit_object_header(_block_2172)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _block_2172->$0 = _M0L1nS774;
  _block_2172->$1 = _M0L5paramS776;
  _block_2172->$2 = _M0L11next__spikeS782;
  _block_2172->$3 = _M0L11next__indexS783;
  _block_2172->$4 = _M0L4fireS779;
  _block_2172->$5 = _M0L1gS780;
  return _block_2172;
}

struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0MP26RiantR8snn__mbt18SpikeTimeParameter3new(
  struct _M0TPB5ArrayGfE* _M0L10spiketimesS764,
  struct _M0TPB5ArrayGiE* _M0L7neuronsS767
) {
  int32_t _M0L1nS763;
  struct _M0TPB5ArrayGfE* _M0L9sorted__tS765;
  struct _M0TPB5ArrayGiE* _M0L9sorted__nS766;
  struct _M0TPB8MutLocalGiE* _M0L1iS768;
  struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _block_2176;
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L1nS763 = _M0MPC15array5Array6lengthGfE(_M0L10spiketimesS764);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L9sorted__tS765 = _M0MPC15array5Array4copyGfE(_M0L10spiketimesS764);
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L9sorted__nS766 = _M0MPC15array5Array4copyGiE(_M0L7neuronsS767);
  _M0L1iS768
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS768)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS768->$0 = 1;
  while (1) {
    int32_t _M0L3valS1836 = _M0L1iS768->$0;
    if (_M0L3valS1836 < _M0L1nS763) {
      int32_t _M0L3valS1857 = _M0L1iS768->$0;
      float _M0L6key__tS769;
      int32_t _M0L3valS1856;
      int32_t _M0L6key__nS770;
      int32_t _M0L3valS1855;
      struct _M0TPB8MutLocalGiE* _M0L1jS771;
      int32_t _M0L3valS1851;
      int32_t _M0L3valS1852;
      int32_t _M0L3valS1854;
      int32_t _M0L6_2atmpS1853;
      #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6key__tS769
      = _M0MPC15array5Array2atGfE(_M0L9sorted__tS765, _M0L3valS1857);
      _M0L3valS1856 = _M0L1iS768->$0;
      #line 44 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6key__nS770
      = _M0MPC15array5Array2atGiE(_M0L9sorted__nS766, _M0L3valS1856);
      _M0L3valS1855 = _M0L1iS768->$0;
      _M0L1jS771
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS771)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS771->$0 = _M0L3valS1855;
      while (1) {
        int32_t _M0L3valS1840 = _M0L1jS771->$0;
        int32_t _if__result_2175;
        if (_M0L3valS1840 > 0) {
          int32_t _M0L3valS1839 = _M0L1jS771->$0;
          int32_t _M0L6_2atmpS1838 = _M0L3valS1839 - 1;
          float _M0L6_2atmpS1837;
          #line 46 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
          _M0L6_2atmpS1837
          = _M0MPC15array5Array2atGfE(_M0L9sorted__tS765, _M0L6_2atmpS1838);
          _if__result_2175 = _M0L6_2atmpS1837 > _M0L6key__tS769;
        } else {
          _if__result_2175 = 0;
        }
        if (_if__result_2175) {
          int32_t _M0L3valS1841 = _M0L1jS771->$0;
          int32_t _M0L3valS1844 = _M0L1jS771->$0;
          int32_t _M0L6_2atmpS1843 = _M0L3valS1844 - 1;
          float _M0L6_2atmpS1842;
          int32_t _M0L3valS1845;
          int32_t _M0L3valS1848;
          int32_t _M0L6_2atmpS1847;
          int32_t _M0L6_2atmpS1846;
          int32_t _M0L3valS1850;
          int32_t _M0L6_2atmpS1849;
          #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
          _M0L6_2atmpS1842
          = _M0MPC15array5Array2atGfE(_M0L9sorted__tS765, _M0L6_2atmpS1843);
          #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
          _M0MPC15array5Array3setGfE(_M0L9sorted__tS765, _M0L3valS1841, _M0L6_2atmpS1842);
          _M0L3valS1845 = _M0L1jS771->$0;
          _M0L3valS1848 = _M0L1jS771->$0;
          _M0L6_2atmpS1847 = _M0L3valS1848 - 1;
          #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
          _M0L6_2atmpS1846
          = _M0MPC15array5Array2atGiE(_M0L9sorted__nS766, _M0L6_2atmpS1847);
          #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
          _M0MPC15array5Array3setGiE(_M0L9sorted__nS766, _M0L3valS1845, _M0L6_2atmpS1846);
          _M0L3valS1850 = _M0L1jS771->$0;
          _M0L6_2atmpS1849 = _M0L3valS1850 - 1;
          _M0L1jS771->$0 = _M0L6_2atmpS1849;
          continue;
        }
        break;
      }
      _M0L3valS1851 = _M0L1jS771->$0;
      #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGfE(_M0L9sorted__tS765, _M0L3valS1851, _M0L6key__tS769);
      _M0L3valS1852 = _M0L1jS771->$0;
      moonbit_decref_cycle_free(_M0L1jS771);
      #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGiE(_M0L9sorted__nS766, _M0L3valS1852, _M0L6key__nS770);
      _M0L3valS1854 = _M0L1iS768->$0;
      _M0L6_2atmpS1853 = _M0L3valS1854 + 1;
      _M0L1iS768->$0 = _M0L6_2atmpS1853;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS768);
    }
    break;
  }
  _block_2176
  = (struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter));
  Moonbit_object_header(_block_2176)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 49, 0);
  _block_2176->$0 = _M0L9sorted__tS765;
  _block_2176->$1 = _M0L9sorted__nS766;
  return _block_2176;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS761
) {
  uint32_t _M0L1uS760;
  uint32_t _M0L4bitsS762;
  double _M0L6_2atmpS1835;
  double _M0L6_2atmpS1834;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS760 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS761);
  _M0L4bitsS762 = _M0L1uS760 >> 8;
  _M0L6_2atmpS1835 = (double)_M0L4bitsS762;
  _M0L6_2atmpS1834 = _M0L6_2atmpS1835 * 0x1p-24;
  return (float)_M0L6_2atmpS1834;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS759
) {
  uint64_t _M0L1uS758;
  uint64_t _M0L6_2atmpS1833;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS758 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS759);
  _M0L6_2atmpS1833 = _M0L1uS758 >> 32;
  return (uint32_t)_M0L6_2atmpS1833;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS751
) {
  uint64_t _M0L2s0S750;
  uint64_t _M0L2s1S752;
  uint64_t _M0L2s2S753;
  uint64_t _M0L2s3S754;
  uint64_t _M0L3tmpS755;
  uint64_t _M0L6_2atmpS1832;
  uint64_t _M0L3resS756;
  uint64_t _M0L1tS757;
  uint64_t _M0L6_2atmpS1822;
  uint64_t _M0L6_2atmpS1823;
  uint64_t _M0L2s2S1825;
  uint64_t _M0L6_2atmpS1824;
  uint64_t _M0L2s3S1827;
  uint64_t _M0L6_2atmpS1826;
  uint64_t _M0L2s2S1829;
  uint64_t _M0L6_2atmpS1828;
  uint64_t _M0L2s3S1831;
  uint64_t _M0L6_2atmpS1830;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S750 = _M0L1rS751->$0;
  _M0L2s1S752 = _M0L1rS751->$1;
  _M0L2s2S753 = _M0L1rS751->$2;
  _M0L2s3S754 = _M0L1rS751->$3;
  _M0L3tmpS755 = _M0L2s0S750 + _M0L2s3S754;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1832 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS755, 23);
  _M0L3resS756 = _M0L6_2atmpS1832 + _M0L2s0S750;
  _M0L1tS757 = _M0L2s1S752 << 17;
  _M0L6_2atmpS1822 = _M0L2s2S753 ^ _M0L2s0S750;
  _M0L1rS751->$2 = _M0L6_2atmpS1822;
  _M0L6_2atmpS1823 = _M0L2s3S754 ^ _M0L2s1S752;
  _M0L1rS751->$3 = _M0L6_2atmpS1823;
  _M0L2s2S1825 = _M0L1rS751->$2;
  _M0L6_2atmpS1824 = _M0L2s1S752 ^ _M0L2s2S1825;
  _M0L1rS751->$1 = _M0L6_2atmpS1824;
  _M0L2s3S1827 = _M0L1rS751->$3;
  _M0L6_2atmpS1826 = _M0L2s0S750 ^ _M0L2s3S1827;
  _M0L1rS751->$0 = _M0L6_2atmpS1826;
  _M0L2s2S1829 = _M0L1rS751->$2;
  _M0L6_2atmpS1828 = _M0L2s2S1829 ^ _M0L1tS757;
  _M0L1rS751->$2 = _M0L6_2atmpS1828;
  _M0L2s3S1831 = _M0L1rS751->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1830 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S1831, 45);
  _M0L1rS751->$3 = _M0L6_2atmpS1830;
  return _M0L3resS756;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS748, int32_t _M0L1kS749) {
  uint64_t _M0L6_2atmpS1819;
  int32_t _M0L6_2atmpS1821;
  uint64_t _M0L6_2atmpS1820;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1819 = _M0L1xS748 << (_M0L1kS749 & 63);
  _M0L6_2atmpS1821 = 64 - _M0L1kS749;
  _M0L6_2atmpS1820 = _M0L1xS748 >> (_M0L6_2atmpS1821 & 63);
  return _M0L6_2atmpS1819 | _M0L6_2atmpS1820;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS747) {
  double _M0L6_2atmpS1818;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1818 = (double)_M0L4selfS747;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1818);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS746) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS746 != _M0L4selfS746) {
    return 0;
  } else if (_M0L4selfS746 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS746 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS746;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS732,
  float _M0L4elemS734
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS731;
  int32_t _M0L1iS733;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS731 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS732);
  _M0L1iS733 = 0;
  while (1) {
    if (_M0L1iS733 < _M0L3lenS732) {
      float* _M0L3bufS1812 = _M0L3arrS731->$0;
      int32_t _M0L6_2atmpS1813;
      _M0L3bufS1812[_M0L1iS733] = _M0L4elemS734;
      _M0L6_2atmpS1813 = _M0L1iS733 + 1;
      _M0L1iS733 = _M0L6_2atmpS1813;
      continue;
    }
    break;
  }
  return _M0L3arrS731;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS737,
  int32_t _M0L4elemS739
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS736;
  int32_t _M0L1iS738;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS736 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS737);
  _M0L1iS738 = 0;
  while (1) {
    if (_M0L1iS738 < _M0L3lenS737) {
      uint8_t* _M0L3bufS1814 = _M0L3arrS736->$0;
      int32_t _M0L6_2atmpS1815;
      _M0L3bufS1814[_M0L1iS738] = _M0L4elemS739;
      _M0L6_2atmpS1815 = _M0L1iS738 + 1;
      _M0L1iS738 = _M0L6_2atmpS1815;
      continue;
    }
    break;
  }
  return _M0L3arrS736;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS742,
  int32_t _M0L4elemS744
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS741;
  int32_t _M0L1iS743;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS741 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS742);
  _M0L1iS743 = 0;
  while (1) {
    if (_M0L1iS743 < _M0L3lenS742) {
      int32_t* _M0L3bufS1816 = _M0L3arrS741->$0;
      int32_t _M0L6_2atmpS1817;
      _M0L3bufS1816[_M0L1iS743] = _M0L4elemS744;
      _M0L6_2atmpS1817 = _M0L1iS743 + 1;
      _M0L1iS743 = _M0L6_2atmpS1817;
      continue;
    }
    break;
  }
  return _M0L3arrS741;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS720,
  int32_t _M0L5indexS721,
  float _M0L5valueS722
) {
  int32_t _M0L3lenS719;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS719 = _M0L4selfS720->$1;
  if (_M0L5indexS721 >= 0 && _M0L5indexS721 < _M0L3lenS719) {
    float* _M0L6_2atmpS1809;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1809 = _M0MPC15array5Array6bufferGfE(_M0L4selfS720);
    _M0L6_2atmpS1809[_M0L5indexS721] = _M0L5valueS722;
    moonbit_decref_cycle_free(_M0L6_2atmpS1809);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS724,
  int32_t _M0L5indexS725,
  int32_t _M0L5valueS726
) {
  int32_t _M0L3lenS723;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS723 = _M0L4selfS724->$1;
  if (_M0L5indexS725 >= 0 && _M0L5indexS725 < _M0L3lenS723) {
    uint8_t* _M0L6_2atmpS1810;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1810 = _M0MPC15array5Array6bufferGbE(_M0L4selfS724);
    _M0L6_2atmpS1810[_M0L5indexS725] = _M0L5valueS726;
    moonbit_decref_cycle_free(_M0L6_2atmpS1810);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS728,
  int32_t _M0L5indexS729,
  int32_t _M0L5valueS730
) {
  int32_t _M0L3lenS727;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS727 = _M0L4selfS728->$1;
  if (_M0L5indexS729 >= 0 && _M0L5indexS729 < _M0L3lenS727) {
    int32_t* _M0L6_2atmpS1811;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1811 = _M0MPC15array5Array6bufferGiE(_M0L4selfS728);
    _M0L6_2atmpS1811[_M0L5indexS729] = _M0L5valueS730;
    moonbit_decref_cycle_free(_M0L6_2atmpS1811);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4copyGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS714
) {
  int32_t _M0L3lenS713;
  struct _M0TPB5ArrayGfE* _M0L3arrS715;
  #line 842 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS713 = _M0L4selfS714->$1;
  if (_M0L3lenS713 == 0) {
    float* _M0L6_2atmpS1807 = moonbit_empty_float_array;
    struct _M0TPB5ArrayGfE* _block_2180 =
      (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
    Moonbit_object_header(_block_2180)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
    _block_2180->$0 = _M0L6_2atmpS1807;
    _block_2180->$1 = 0;
    return _block_2180;
  }
  #line 848 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3arrS715 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS713);
  #line 849 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array12unsafe__blitGfE(_M0L3arrS715, 0, _M0L4selfS714, 0, _M0L3lenS713);
  return _M0L3arrS715;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4copyGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS717
) {
  int32_t _M0L3lenS716;
  struct _M0TPB5ArrayGiE* _M0L3arrS718;
  #line 842 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS716 = _M0L4selfS717->$1;
  if (_M0L3lenS716 == 0) {
    int32_t* _M0L6_2atmpS1808 = (int32_t*)moonbit_empty_int32_array;
    struct _M0TPB5ArrayGiE* _block_2181 =
      (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
    Moonbit_object_header(_block_2181)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
    _block_2181->$0 = _M0L6_2atmpS1808;
    _block_2181->$1 = 0;
    return _block_2181;
  }
  #line 848 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3arrS718 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS716);
  #line 849 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array12unsafe__blitGiE(_M0L3arrS718, 0, _M0L4selfS717, 0, _M0L3lenS716);
  return _M0L3arrS718;
}

int32_t _M0MPC15array5Array12unsafe__blitGfE(
  struct _M0TPB5ArrayGfE* _M0L3dstS703,
  int32_t _M0L11dst__offsetS704,
  struct _M0TPB5ArrayGfE* _M0L3srcS705,
  int32_t _M0L11src__offsetS706,
  int32_t _M0L3lenS707
) {
  float* _M0L6_2atmpS1803;
  float* _M0L6_2atmpS1804;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  _M0L6_2atmpS1803 = _M0MPC15array5Array6bufferGfE(_M0L3dstS703);
  #line 60 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  _M0L6_2atmpS1804 = _M0MPC15array5Array6bufferGfE(_M0L3srcS705);
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L6_2atmpS1803, _M0L11dst__offsetS704, _M0L6_2atmpS1804, _M0L11src__offsetS706, _M0L3lenS707, sizeof(float));
  return 0;
}

int32_t _M0MPC15array5Array12unsafe__blitGiE(
  struct _M0TPB5ArrayGiE* _M0L3dstS708,
  int32_t _M0L11dst__offsetS709,
  struct _M0TPB5ArrayGiE* _M0L3srcS710,
  int32_t _M0L11src__offsetS711,
  int32_t _M0L3lenS712
) {
  int32_t* _M0L6_2atmpS1805;
  int32_t* _M0L6_2atmpS1806;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  _M0L6_2atmpS1805 = _M0MPC15array5Array6bufferGiE(_M0L3dstS708);
  #line 60 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  _M0L6_2atmpS1806 = _M0MPC15array5Array6bufferGiE(_M0L3srcS710);
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L6_2atmpS1805, _M0L11dst__offsetS709, _M0L6_2atmpS1806, _M0L11src__offsetS711, _M0L3lenS712, sizeof(int32_t));
  return 0;
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS692,
  int32_t _M0L5indexS693
) {
  int32_t _M0L3lenS691;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS691 = _M0L4selfS692->$1;
  if (_M0L5indexS693 >= 0 && _M0L5indexS693 < _M0L3lenS691) {
    float* _M0L6_2atmpS1799;
    float _result_2182;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1799 = _M0MPC15array5Array6bufferGfE(_M0L4selfS692);
    _result_2182 = (float)_M0L6_2atmpS1799[_M0L5indexS693];
    moonbit_decref_cycle_free(_M0L6_2atmpS1799);
    return _result_2182;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS695,
  int32_t _M0L5indexS696
) {
  int32_t _M0L3lenS694;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS694 = _M0L4selfS695->$1;
  if (_M0L5indexS696 >= 0 && _M0L5indexS696 < _M0L3lenS694) {
    uint8_t* _M0L6_2atmpS1800;
    int32_t _result_2183;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1800 = _M0MPC15array5Array6bufferGbE(_M0L4selfS695);
    _result_2183 = (int32_t)_M0L6_2atmpS1800[_M0L5indexS696];
    moonbit_decref_cycle_free(_M0L6_2atmpS1800);
    return _result_2183;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS698,
  int32_t _M0L5indexS699
) {
  int32_t _M0L3lenS697;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS697 = _M0L4selfS698->$1;
  if (_M0L5indexS699 >= 0 && _M0L5indexS699 < _M0L3lenS697) {
    int32_t* _M0L6_2atmpS1801;
    int32_t _result_2184;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1801 = _M0MPC15array5Array6bufferGiE(_M0L4selfS698);
    _result_2184 = (int32_t)_M0L6_2atmpS1801[_M0L5indexS699];
    moonbit_decref_cycle_free(_M0L6_2atmpS1801);
    return _result_2184;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS701,
  int32_t _M0L5indexS702
) {
  int32_t _M0L3lenS700;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS700 = _M0L4selfS701->$1;
  if (_M0L5indexS702 >= 0 && _M0L5indexS702 < _M0L3lenS700) {
    moonbit_string_t* _M0L6_2atmpS1802;
    moonbit_string_t _M0L6_2atmpS2110;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1802 = _M0MPC15array5Array6bufferGsE(_M0L4selfS701);
    _M0L6_2atmpS2110 = (moonbit_string_t)_M0L6_2atmpS1802[_M0L5indexS702];
    moonbit_incref_cycle_free(_M0L6_2atmpS2110);
    moonbit_decref_cycle_free(_M0L6_2atmpS1802);
    return _M0L6_2atmpS2110;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS690) {
  moonbit_string_t _M0L6_2atmpS1798;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1798 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS690);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1798);
  moonbit_decref_cycle_free(_M0L6_2atmpS1798);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS689) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS689);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS674) {
  uint64_t _M0L4bitsS677;
  uint64_t _M0L6_2atmpS1797;
  uint64_t _M0L6_2atmpS1796;
  int32_t _M0L8ieeeSignS678;
  uint64_t _M0L12ieeeMantissaS679;
  uint64_t _M0L6_2atmpS1795;
  uint64_t _M0L6_2atmpS1794;
  int32_t _M0L12ieeeExponentS680;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS681;
  struct _M0TPB17FloatingDecimal64* _M0L1vS682;
  moonbit_string_t _result_2186;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS674 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_10.data;
  }
  if (_M0L3valS674 >= -0x1p+53 && _M0L3valS674 <= 0x1p+53) {
    if (_M0L3valS674 >= -0x1p+31 && _M0L3valS674 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS675;
      double _M0L6_2atmpS1783;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS675 = _M0MPC16double6Double7to__int(_M0L3valS674);
      _M0L6_2atmpS1783 = (double)_M0L1iS675;
      if (_M0L6_2atmpS1783 == _M0L3valS674) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS675, 10);
      }
    } else {
      int64_t _M0L1iS676;
      double _M0L6_2atmpS1784;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS676 = _M0MPC16double6Double9to__int64(_M0L3valS674);
      _M0L6_2atmpS1784 = (double)_M0L1iS676;
      if (_M0L6_2atmpS1784 == _M0L3valS674) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS676, 10);
      }
    }
  }
  _M0L4bitsS677 = *(int64_t*)&_M0L3valS674;
  _M0L6_2atmpS1797 = _M0L4bitsS677 >> 63;
  _M0L6_2atmpS1796 = _M0L6_2atmpS1797 & 1ull;
  _M0L8ieeeSignS678 = _M0L6_2atmpS1796 != 0ull;
  _M0L12ieeeMantissaS679 = _M0L4bitsS677 & 4503599627370495ull;
  _M0L6_2atmpS1795 = _M0L4bitsS677 >> 52;
  _M0L6_2atmpS1794 = _M0L6_2atmpS1795 & 2047ull;
  _M0L12ieeeExponentS680 = (int32_t)_M0L6_2atmpS1794;
  if (
    _M0L12ieeeExponentS680 == 2047
    || _M0L12ieeeExponentS680 == 0 && _M0L12ieeeMantissaS679 == 0ull
  ) {
    int32_t _M0L6_2atmpS1785 = _M0L12ieeeExponentS680 != 0;
    int32_t _M0L6_2atmpS1786 = _M0L12ieeeMantissaS679 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS678, _M0L6_2atmpS1785, _M0L6_2atmpS1786);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS681
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS679, _M0L12ieeeExponentS680);
  if (_M0L7_2abindS681 == 0) {
    uint32_t _M0L6_2atmpS1787;
    if (_M0L7_2abindS681) {
      moonbit_decref_cycle_free(_M0L7_2abindS681);
    }
    _M0L6_2atmpS1787 = *(uint32_t*)&_M0L12ieeeExponentS680;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS682 = _M0FPB3d2d(_M0L12ieeeMantissaS679, _M0L6_2atmpS1787);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS683 = _M0L7_2abindS681;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS684 = _M0L7_2aSomeS683;
    struct _M0TPB17FloatingDecimal64* _M0L1xS685 = _M0L4_2afS684;
    while (1) {
      uint64_t _M0L8mantissaS1793 = _M0L1xS685->$0;
      uint64_t _M0L1qS686 = _M0L8mantissaS1793 / 10ull;
      uint64_t _M0L8mantissaS1791 = _M0L1xS685->$0;
      uint64_t _M0L6_2atmpS1792 = 10ull * _M0L1qS686;
      uint64_t _M0L1rS687 = _M0L8mantissaS1791 - _M0L6_2atmpS1792;
      int32_t _M0L8exponentS1790;
      int32_t _M0L6_2atmpS1789;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1788;
      if (_M0L1rS687 != 0ull) {
        _M0L1vS682 = _M0L1xS685;
        break;
      }
      _M0L8exponentS1790 = _M0L1xS685->$1;
      moonbit_decref_cycle_free(_M0L1xS685);
      _M0L6_2atmpS1789 = _M0L8exponentS1790 + 1;
      _M0L6_2atmpS1788
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1788)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1788->$0 = _M0L1qS686;
      _M0L6_2atmpS1788->$1 = _M0L6_2atmpS1789;
      _M0L1xS685 = _M0L6_2atmpS1788;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2186 = _M0FPB9to__chars(_M0L1vS682, _M0L8ieeeSignS678);
  moonbit_decref_cycle_free(_M0L1vS682);
  return _result_2186;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS669,
  int32_t _M0L12ieeeExponentS671
) {
  uint64_t _M0L2m2S668;
  int32_t _M0L6_2atmpS1782;
  int32_t _M0L2e2S670;
  int32_t _M0L6_2atmpS1781;
  uint64_t _M0L6_2atmpS1780;
  uint64_t _M0L4maskS672;
  uint64_t _M0L8fractionS673;
  int32_t _M0L6_2atmpS1779;
  uint64_t _M0L6_2atmpS1778;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1777;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S668 = 4503599627370496ull | _M0L12ieeeMantissaS669;
  _M0L6_2atmpS1782 = _M0L12ieeeExponentS671 - 1023;
  _M0L2e2S670 = _M0L6_2atmpS1782 - 52;
  if (_M0L2e2S670 > 0) {
    return 0;
  }
  if (_M0L2e2S670 < -52) {
    return 0;
  }
  _M0L6_2atmpS1781 = -_M0L2e2S670;
  _M0L6_2atmpS1780 = 1ull << (_M0L6_2atmpS1781 & 63);
  _M0L4maskS672 = _M0L6_2atmpS1780 - 1ull;
  _M0L8fractionS673 = _M0L2m2S668 & _M0L4maskS672;
  if (_M0L8fractionS673 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1779 = -_M0L2e2S670;
  _M0L6_2atmpS1778 = _M0L2m2S668 >> (_M0L6_2atmpS1779 & 63);
  _M0L6_2atmpS1777
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1777)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1777->$0 = _M0L6_2atmpS1778;
  _M0L6_2atmpS1777->$1 = 0;
  return _M0L6_2atmpS1777;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS636,
  int32_t _M0L4signS634
) {
  moonbit_bytes_t _M0L6resultS632;
  int32_t _M0Lm5indexS633;
  uint64_t _M0L6outputS635;
  int32_t _M0L7olengthS637;
  int32_t _M0L8exponentS1776;
  int32_t _M0L6_2atmpS1775;
  int32_t _M0Lm3expS638;
  int32_t _M0L6_2atmpS1774;
  int32_t _M0L6_2atmpS1772;
  int32_t _M0L18scientificNotationS639;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS632 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS633 = 0;
  if (_M0L4signS634) {
    int32_t _M0L6_2atmpS1646 = _M0Lm5indexS633;
    int32_t _M0L6_2atmpS1647;
    if (
      _M0L6_2atmpS1646 < 0
      || _M0L6_2atmpS1646 >= Moonbit_array_length(_M0L6resultS632)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS632[_M0L6_2atmpS1646] = 45;
    _M0L6_2atmpS1647 = _M0Lm5indexS633;
    _M0Lm5indexS633 = _M0L6_2atmpS1647 + 1;
  }
  _M0L6outputS635 = _M0L1vS636->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS637 = _M0FPB17decimal__length17(_M0L6outputS635);
  _M0L8exponentS1776 = _M0L1vS636->$1;
  _M0L6_2atmpS1775 = _M0L8exponentS1776 + _M0L7olengthS637;
  _M0Lm3expS638 = _M0L6_2atmpS1775 - 1;
  _M0L6_2atmpS1774 = _M0Lm3expS638;
  if (_M0L6_2atmpS1774 >= -6) {
    int32_t _M0L6_2atmpS1773 = _M0Lm3expS638;
    _M0L6_2atmpS1772 = _M0L6_2atmpS1773 < 21;
  } else {
    _M0L6_2atmpS1772 = 0;
  }
  _M0L18scientificNotationS639 = !_M0L6_2atmpS1772;
  if (_M0L18scientificNotationS639) {
    int32_t _M0L7_2abindS640 = _M0L7olengthS637 - 1;
    uint64_t _M0L6outputS641;
    int32_t _M0L1iS642 = 0;
    uint64_t _M0L6outputS643 = _M0L6outputS635;
    int32_t _M0L6_2atmpS1648;
    int32_t _M0L6_2atmpS1652;
    int32_t _M0L6_2atmpS1651;
    int32_t _M0L6_2atmpS1650;
    int32_t _M0L6_2atmpS1649;
    int32_t _M0L6_2atmpS1656;
    int32_t _M0L6_2atmpS1657;
    int32_t _M0L6_2atmpS1658;
    int32_t _M0L6_2atmpS1659;
    int32_t _M0L6_2atmpS1660;
    int32_t _M0L6_2atmpS1666;
    int32_t _M0L6_2atmpS1699;
    moonbit_string_t _result_2188;
    while (1) {
      if (_M0L1iS642 < _M0L7_2abindS640) {
        uint64_t _M0L1cS644 = _M0L6outputS643 % 10ull;
        int32_t _M0L6_2atmpS1705 = _M0Lm5indexS633;
        int32_t _M0L6_2atmpS1704 = _M0L6_2atmpS1705 + _M0L7olengthS637;
        int32_t _M0L6_2atmpS1700 = _M0L6_2atmpS1704 - _M0L1iS642;
        int32_t _M0L6_2atmpS1703 = (int32_t)_M0L1cS644;
        int32_t _M0L6_2atmpS1702 = 48 + _M0L6_2atmpS1703;
        int32_t _M0L6_2atmpS1701 = _M0L6_2atmpS1702 & 0xff;
        int32_t _M0L6_2atmpS1706;
        uint64_t _M0L6_2atmpS1707;
        if (
          _M0L6_2atmpS1700 < 0
          || _M0L6_2atmpS1700 >= Moonbit_array_length(_M0L6resultS632)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS632[_M0L6_2atmpS1700] = _M0L6_2atmpS1701;
        _M0L6_2atmpS1706 = _M0L1iS642 + 1;
        _M0L6_2atmpS1707 = _M0L6outputS643 / 10ull;
        _M0L1iS642 = _M0L6_2atmpS1706;
        _M0L6outputS643 = _M0L6_2atmpS1707;
        continue;
      } else {
        _M0L6outputS641 = _M0L6outputS643;
      }
      break;
    }
    _M0L6_2atmpS1648 = _M0Lm5indexS633;
    _M0L6_2atmpS1652 = (int32_t)_M0L6outputS641;
    _M0L6_2atmpS1651 = _M0L6_2atmpS1652 % 10;
    _M0L6_2atmpS1650 = 48 + _M0L6_2atmpS1651;
    _M0L6_2atmpS1649 = _M0L6_2atmpS1650 & 0xff;
    if (
      _M0L6_2atmpS1648 < 0
      || _M0L6_2atmpS1648 >= Moonbit_array_length(_M0L6resultS632)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS632[_M0L6_2atmpS1648] = _M0L6_2atmpS1649;
    if (_M0L7olengthS637 > 1) {
      int32_t _M0L6_2atmpS1654 = _M0Lm5indexS633;
      int32_t _M0L6_2atmpS1653 = _M0L6_2atmpS1654 + 1;
      if (
        _M0L6_2atmpS1653 < 0
        || _M0L6_2atmpS1653 >= Moonbit_array_length(_M0L6resultS632)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS632[_M0L6_2atmpS1653] = 46;
    } else {
      int32_t _M0L6_2atmpS1655 = _M0Lm5indexS633;
      _M0Lm5indexS633 = _M0L6_2atmpS1655 - 1;
    }
    _M0L6_2atmpS1656 = _M0Lm5indexS633;
    _M0L6_2atmpS1657 = _M0L7olengthS637 + 1;
    _M0Lm5indexS633 = _M0L6_2atmpS1656 + _M0L6_2atmpS1657;
    _M0L6_2atmpS1658 = _M0Lm5indexS633;
    if (
      _M0L6_2atmpS1658 < 0
      || _M0L6_2atmpS1658 >= Moonbit_array_length(_M0L6resultS632)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS632[_M0L6_2atmpS1658] = 101;
    _M0L6_2atmpS1659 = _M0Lm5indexS633;
    _M0Lm5indexS633 = _M0L6_2atmpS1659 + 1;
    _M0L6_2atmpS1660 = _M0Lm3expS638;
    if (_M0L6_2atmpS1660 < 0) {
      int32_t _M0L6_2atmpS1661 = _M0Lm5indexS633;
      int32_t _M0L6_2atmpS1662;
      int32_t _M0L6_2atmpS1663;
      if (
        _M0L6_2atmpS1661 < 0
        || _M0L6_2atmpS1661 >= Moonbit_array_length(_M0L6resultS632)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS632[_M0L6_2atmpS1661] = 45;
      _M0L6_2atmpS1662 = _M0Lm5indexS633;
      _M0Lm5indexS633 = _M0L6_2atmpS1662 + 1;
      _M0L6_2atmpS1663 = _M0Lm3expS638;
      _M0Lm3expS638 = -_M0L6_2atmpS1663;
    } else {
      int32_t _M0L6_2atmpS1664 = _M0Lm5indexS633;
      int32_t _M0L6_2atmpS1665;
      if (
        _M0L6_2atmpS1664 < 0
        || _M0L6_2atmpS1664 >= Moonbit_array_length(_M0L6resultS632)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS632[_M0L6_2atmpS1664] = 43;
      _M0L6_2atmpS1665 = _M0Lm5indexS633;
      _M0Lm5indexS633 = _M0L6_2atmpS1665 + 1;
    }
    _M0L6_2atmpS1666 = _M0Lm3expS638;
    if (_M0L6_2atmpS1666 >= 100) {
      int32_t _M0L6_2atmpS1682 = _M0Lm3expS638;
      int32_t _M0L1aS646 = _M0L6_2atmpS1682 / 100;
      int32_t _M0L6_2atmpS1681 = _M0Lm3expS638;
      int32_t _M0L6_2atmpS1680 = _M0L6_2atmpS1681 / 10;
      int32_t _M0L1bS647 = _M0L6_2atmpS1680 % 10;
      int32_t _M0L6_2atmpS1679 = _M0Lm3expS638;
      int32_t _M0L1cS648 = _M0L6_2atmpS1679 % 10;
      int32_t _M0L6_2atmpS1667 = _M0Lm5indexS633;
      int32_t _M0L6_2atmpS1669 = 48 + _M0L1aS646;
      int32_t _M0L6_2atmpS1668 = _M0L6_2atmpS1669 & 0xff;
      int32_t _M0L6_2atmpS1673;
      int32_t _M0L6_2atmpS1670;
      int32_t _M0L6_2atmpS1672;
      int32_t _M0L6_2atmpS1671;
      int32_t _M0L6_2atmpS1677;
      int32_t _M0L6_2atmpS1674;
      int32_t _M0L6_2atmpS1676;
      int32_t _M0L6_2atmpS1675;
      int32_t _M0L6_2atmpS1678;
      if (
        _M0L6_2atmpS1667 < 0
        || _M0L6_2atmpS1667 >= Moonbit_array_length(_M0L6resultS632)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS632[_M0L6_2atmpS1667] = _M0L6_2atmpS1668;
      _M0L6_2atmpS1673 = _M0Lm5indexS633;
      _M0L6_2atmpS1670 = _M0L6_2atmpS1673 + 1;
      _M0L6_2atmpS1672 = 48 + _M0L1bS647;
      _M0L6_2atmpS1671 = _M0L6_2atmpS1672 & 0xff;
      if (
        _M0L6_2atmpS1670 < 0
        || _M0L6_2atmpS1670 >= Moonbit_array_length(_M0L6resultS632)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS632[_M0L6_2atmpS1670] = _M0L6_2atmpS1671;
      _M0L6_2atmpS1677 = _M0Lm5indexS633;
      _M0L6_2atmpS1674 = _M0L6_2atmpS1677 + 2;
      _M0L6_2atmpS1676 = 48 + _M0L1cS648;
      _M0L6_2atmpS1675 = _M0L6_2atmpS1676 & 0xff;
      if (
        _M0L6_2atmpS1674 < 0
        || _M0L6_2atmpS1674 >= Moonbit_array_length(_M0L6resultS632)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS632[_M0L6_2atmpS1674] = _M0L6_2atmpS1675;
      _M0L6_2atmpS1678 = _M0Lm5indexS633;
      _M0Lm5indexS633 = _M0L6_2atmpS1678 + 3;
    } else {
      int32_t _M0L6_2atmpS1683 = _M0Lm3expS638;
      if (_M0L6_2atmpS1683 >= 10) {
        int32_t _M0L6_2atmpS1693 = _M0Lm3expS638;
        int32_t _M0L1aS649 = _M0L6_2atmpS1693 / 10;
        int32_t _M0L6_2atmpS1692 = _M0Lm3expS638;
        int32_t _M0L1bS650 = _M0L6_2atmpS1692 % 10;
        int32_t _M0L6_2atmpS1684 = _M0Lm5indexS633;
        int32_t _M0L6_2atmpS1686 = 48 + _M0L1aS649;
        int32_t _M0L6_2atmpS1685 = _M0L6_2atmpS1686 & 0xff;
        int32_t _M0L6_2atmpS1690;
        int32_t _M0L6_2atmpS1687;
        int32_t _M0L6_2atmpS1689;
        int32_t _M0L6_2atmpS1688;
        int32_t _M0L6_2atmpS1691;
        if (
          _M0L6_2atmpS1684 < 0
          || _M0L6_2atmpS1684 >= Moonbit_array_length(_M0L6resultS632)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS632[_M0L6_2atmpS1684] = _M0L6_2atmpS1685;
        _M0L6_2atmpS1690 = _M0Lm5indexS633;
        _M0L6_2atmpS1687 = _M0L6_2atmpS1690 + 1;
        _M0L6_2atmpS1689 = 48 + _M0L1bS650;
        _M0L6_2atmpS1688 = _M0L6_2atmpS1689 & 0xff;
        if (
          _M0L6_2atmpS1687 < 0
          || _M0L6_2atmpS1687 >= Moonbit_array_length(_M0L6resultS632)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS632[_M0L6_2atmpS1687] = _M0L6_2atmpS1688;
        _M0L6_2atmpS1691 = _M0Lm5indexS633;
        _M0Lm5indexS633 = _M0L6_2atmpS1691 + 2;
      } else {
        int32_t _M0L6_2atmpS1694 = _M0Lm5indexS633;
        int32_t _M0L6_2atmpS1697 = _M0Lm3expS638;
        int32_t _M0L6_2atmpS1696 = 48 + _M0L6_2atmpS1697;
        int32_t _M0L6_2atmpS1695 = _M0L6_2atmpS1696 & 0xff;
        int32_t _M0L6_2atmpS1698;
        if (
          _M0L6_2atmpS1694 < 0
          || _M0L6_2atmpS1694 >= Moonbit_array_length(_M0L6resultS632)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS632[_M0L6_2atmpS1694] = _M0L6_2atmpS1695;
        _M0L6_2atmpS1698 = _M0Lm5indexS633;
        _M0Lm5indexS633 = _M0L6_2atmpS1698 + 1;
      }
    }
    _M0L6_2atmpS1699 = _M0Lm5indexS633;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2188
    = _M0FPB19string__from__bytes(_M0L6resultS632, 0, _M0L6_2atmpS1699);
    moonbit_decref_cycle_free(_M0L6resultS632);
    return _result_2188;
  } else {
    int32_t _M0L6_2atmpS1708 = _M0Lm3expS638;
    int32_t _M0L6_2atmpS1771;
    moonbit_string_t _result_2194;
    if (_M0L6_2atmpS1708 < 0) {
      int32_t _M0L6_2atmpS1709 = _M0Lm5indexS633;
      int32_t _M0L6_2atmpS1711;
      int32_t _M0L6_2atmpS1710;
      int32_t _M0L6_2atmpS1712;
      int32_t _M0L1iS651;
      int32_t _M0L6_2atmpS1727;
      int32_t _M0L6_2atmpS1729;
      int32_t _M0L6_2atmpS1728;
      int32_t _M0L7currentS653;
      int32_t _M0L1iS654;
      uint64_t _M0L6outputS655;
      if (
        _M0L6_2atmpS1709 < 0
        || _M0L6_2atmpS1709 >= Moonbit_array_length(_M0L6resultS632)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS632[_M0L6_2atmpS1709] = 48;
      _M0L6_2atmpS1711 = _M0Lm5indexS633;
      _M0L6_2atmpS1710 = _M0L6_2atmpS1711 + 1;
      if (
        _M0L6_2atmpS1710 < 0
        || _M0L6_2atmpS1710 >= Moonbit_array_length(_M0L6resultS632)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS632[_M0L6_2atmpS1710] = 46;
      _M0L6_2atmpS1712 = _M0Lm5indexS633;
      _M0Lm5indexS633 = _M0L6_2atmpS1712 + 2;
      _M0L1iS651 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1713 = _M0Lm3expS638;
        if (_M0L1iS651 > _M0L6_2atmpS1713) {
          int32_t _M0L6_2atmpS1716 = _M0Lm5indexS633;
          int32_t _M0L6_2atmpS1715 = _M0L6_2atmpS1716 - _M0L1iS651;
          int32_t _M0L6_2atmpS1714 = _M0L6_2atmpS1715 - 1;
          int32_t _M0L6_2atmpS1717;
          if (
            _M0L6_2atmpS1714 < 0
            || _M0L6_2atmpS1714 >= Moonbit_array_length(_M0L6resultS632)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS632[_M0L6_2atmpS1714] = 48;
          _M0L6_2atmpS1717 = _M0L1iS651 - 1;
          _M0L1iS651 = _M0L6_2atmpS1717;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1727 = _M0Lm5indexS633;
      _M0L6_2atmpS1729 = _M0Lm3expS638;
      _M0L6_2atmpS1728 = -1 - _M0L6_2atmpS1729;
      _M0L7currentS653 = _M0L6_2atmpS1727 + _M0L6_2atmpS1728;
      _M0L1iS654 = 0;
      _M0L6outputS655 = _M0L6outputS635;
      while (1) {
        if (_M0L1iS654 < _M0L7olengthS637) {
          int32_t _M0L6_2atmpS1724 = _M0L7currentS653 + _M0L7olengthS637;
          int32_t _M0L6_2atmpS1723 = _M0L6_2atmpS1724 - _M0L1iS654;
          int32_t _M0L6_2atmpS1718 = _M0L6_2atmpS1723 - 1;
          uint64_t _M0L6_2atmpS1722 = _M0L6outputS655 % 10ull;
          int32_t _M0L6_2atmpS1721 = (int32_t)_M0L6_2atmpS1722;
          int32_t _M0L6_2atmpS1720 = 48 + _M0L6_2atmpS1721;
          int32_t _M0L6_2atmpS1719 = _M0L6_2atmpS1720 & 0xff;
          int32_t _M0L6_2atmpS1725;
          uint64_t _M0L6_2atmpS1726;
          if (
            _M0L6_2atmpS1718 < 0
            || _M0L6_2atmpS1718 >= Moonbit_array_length(_M0L6resultS632)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS632[_M0L6_2atmpS1718] = _M0L6_2atmpS1719;
          _M0L6_2atmpS1725 = _M0L1iS654 + 1;
          _M0L6_2atmpS1726 = _M0L6outputS655 / 10ull;
          _M0L1iS654 = _M0L6_2atmpS1725;
          _M0L6outputS655 = _M0L6_2atmpS1726;
          continue;
        }
        break;
      }
      _M0Lm5indexS633 = _M0L7currentS653 + _M0L7olengthS637;
    } else {
      int32_t _M0L6_2atmpS1731 = _M0Lm3expS638;
      int32_t _M0L6_2atmpS1730 = _M0L6_2atmpS1731 + 1;
      if (_M0L6_2atmpS1730 >= _M0L7olengthS637) {
        int32_t _M0L1iS657 = 0;
        uint64_t _M0L6outputS658 = _M0L6outputS635;
        int32_t _M0L6_2atmpS1742;
        int32_t _M0L6_2atmpS1747;
        int32_t _M0L7_2abindS660;
        int32_t _M0L1iS661;
        int32_t _M0L6_2atmpS1748;
        int32_t _M0L6_2atmpS1751;
        int32_t _M0L6_2atmpS1750;
        int32_t _M0L6_2atmpS1749;
        while (1) {
          if (_M0L1iS657 < _M0L7olengthS637) {
            int32_t _M0L6_2atmpS1739 = _M0Lm5indexS633;
            int32_t _M0L6_2atmpS1738 = _M0L6_2atmpS1739 + _M0L7olengthS637;
            int32_t _M0L6_2atmpS1737 = _M0L6_2atmpS1738 - _M0L1iS657;
            int32_t _M0L6_2atmpS1732 = _M0L6_2atmpS1737 - 1;
            uint64_t _M0L6_2atmpS1736 = _M0L6outputS658 % 10ull;
            int32_t _M0L6_2atmpS1735 = (int32_t)_M0L6_2atmpS1736;
            int32_t _M0L6_2atmpS1734 = 48 + _M0L6_2atmpS1735;
            int32_t _M0L6_2atmpS1733 = _M0L6_2atmpS1734 & 0xff;
            int32_t _M0L6_2atmpS1740;
            uint64_t _M0L6_2atmpS1741;
            if (
              _M0L6_2atmpS1732 < 0
              || _M0L6_2atmpS1732 >= Moonbit_array_length(_M0L6resultS632)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS632[_M0L6_2atmpS1732] = _M0L6_2atmpS1733;
            _M0L6_2atmpS1740 = _M0L1iS657 + 1;
            _M0L6_2atmpS1741 = _M0L6outputS658 / 10ull;
            _M0L1iS657 = _M0L6_2atmpS1740;
            _M0L6outputS658 = _M0L6_2atmpS1741;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1742 = _M0Lm5indexS633;
        _M0Lm5indexS633 = _M0L6_2atmpS1742 + _M0L7olengthS637;
        _M0L6_2atmpS1747 = _M0Lm3expS638;
        _M0L7_2abindS660 = _M0L6_2atmpS1747 + 1;
        _M0L1iS661 = _M0L7olengthS637;
        while (1) {
          if (_M0L1iS661 < _M0L7_2abindS660) {
            int32_t _M0L6_2atmpS1745 = _M0Lm5indexS633;
            int32_t _M0L6_2atmpS1744 = _M0L6_2atmpS1745 + _M0L1iS661;
            int32_t _M0L6_2atmpS1743 = _M0L6_2atmpS1744 - _M0L7olengthS637;
            int32_t _M0L6_2atmpS1746;
            if (
              _M0L6_2atmpS1743 < 0
              || _M0L6_2atmpS1743 >= Moonbit_array_length(_M0L6resultS632)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS632[_M0L6_2atmpS1743] = 48;
            _M0L6_2atmpS1746 = _M0L1iS661 + 1;
            _M0L1iS661 = _M0L6_2atmpS1746;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1748 = _M0Lm5indexS633;
        _M0L6_2atmpS1751 = _M0Lm3expS638;
        _M0L6_2atmpS1750 = _M0L6_2atmpS1751 + 1;
        _M0L6_2atmpS1749 = _M0L6_2atmpS1750 - _M0L7olengthS637;
        _M0Lm5indexS633 = _M0L6_2atmpS1748 + _M0L6_2atmpS1749;
      } else {
        int32_t _M0L6_2atmpS1768 = _M0Lm5indexS633;
        int32_t _M0L6_2atmpS1767 = _M0L6_2atmpS1768 + 1;
        int32_t _M0L1iS663 = 0;
        int32_t _M0L7currentS664 = _M0L6_2atmpS1767;
        uint64_t _M0L6outputS665 = _M0L6outputS635;
        int32_t _M0L6_2atmpS1769;
        int32_t _M0L6_2atmpS1770;
        while (1) {
          if (_M0L1iS663 < _M0L7olengthS637) {
            int32_t _M0L6_2atmpS1763 = _M0L7olengthS637 - _M0L1iS663;
            int32_t _M0L6_2atmpS1761 = _M0L6_2atmpS1763 - 1;
            int32_t _M0L6_2atmpS1762 = _M0Lm3expS638;
            int32_t _M0L7currentS666;
            int32_t _M0L6_2atmpS1758;
            int32_t _M0L6_2atmpS1757;
            int32_t _M0L6_2atmpS1752;
            uint64_t _M0L6_2atmpS1756;
            int32_t _M0L6_2atmpS1755;
            int32_t _M0L6_2atmpS1754;
            int32_t _M0L6_2atmpS1753;
            int32_t _M0L6_2atmpS1759;
            uint64_t _M0L6_2atmpS1760;
            if (_M0L6_2atmpS1761 == _M0L6_2atmpS1762) {
              int32_t _M0L6_2atmpS1766 = _M0L7currentS664 + _M0L7olengthS637;
              int32_t _M0L6_2atmpS1765 = _M0L6_2atmpS1766 - _M0L1iS663;
              int32_t _M0L6_2atmpS1764 = _M0L6_2atmpS1765 - 1;
              if (
                _M0L6_2atmpS1764 < 0
                || _M0L6_2atmpS1764 >= Moonbit_array_length(_M0L6resultS632)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS632[_M0L6_2atmpS1764] = 46;
              _M0L7currentS666 = _M0L7currentS664 - 1;
            } else {
              _M0L7currentS666 = _M0L7currentS664;
            }
            _M0L6_2atmpS1758 = _M0L7currentS666 + _M0L7olengthS637;
            _M0L6_2atmpS1757 = _M0L6_2atmpS1758 - _M0L1iS663;
            _M0L6_2atmpS1752 = _M0L6_2atmpS1757 - 1;
            _M0L6_2atmpS1756 = _M0L6outputS665 % 10ull;
            _M0L6_2atmpS1755 = (int32_t)_M0L6_2atmpS1756;
            _M0L6_2atmpS1754 = 48 + _M0L6_2atmpS1755;
            _M0L6_2atmpS1753 = _M0L6_2atmpS1754 & 0xff;
            if (
              _M0L6_2atmpS1752 < 0
              || _M0L6_2atmpS1752 >= Moonbit_array_length(_M0L6resultS632)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS632[_M0L6_2atmpS1752] = _M0L6_2atmpS1753;
            _M0L6_2atmpS1759 = _M0L1iS663 + 1;
            _M0L6_2atmpS1760 = _M0L6outputS665 / 10ull;
            _M0L1iS663 = _M0L6_2atmpS1759;
            _M0L7currentS664 = _M0L7currentS666;
            _M0L6outputS665 = _M0L6_2atmpS1760;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1769 = _M0Lm5indexS633;
        _M0L6_2atmpS1770 = _M0L7olengthS637 + 1;
        _M0Lm5indexS633 = _M0L6_2atmpS1769 + _M0L6_2atmpS1770;
      }
    }
    _M0L6_2atmpS1771 = _M0Lm5indexS633;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2194
    = _M0FPB19string__from__bytes(_M0L6resultS632, 0, _M0L6_2atmpS1771);
    moonbit_decref_cycle_free(_M0L6resultS632);
    return _result_2194;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS578,
  uint32_t _M0L12ieeeExponentS577
) {
  int32_t _M0Lm2e2S575;
  uint64_t _M0Lm2m2S576;
  uint64_t _M0L6_2atmpS1645;
  uint64_t _M0L6_2atmpS1644;
  int32_t _M0L4evenS579;
  uint64_t _M0L6_2atmpS1643;
  uint64_t _M0L2mvS580;
  int32_t _M0L7mmShiftS581;
  uint64_t _M0Lm2vrS582;
  uint64_t _M0Lm2vpS583;
  uint64_t _M0Lm2vmS584;
  int32_t _M0Lm3e10S585;
  int32_t _M0Lm17vmIsTrailingZerosS586;
  int32_t _M0Lm17vrIsTrailingZerosS587;
  int32_t _M0L6_2atmpS1545;
  int32_t _M0Lm7removedS606;
  int32_t _M0Lm16lastRemovedDigitS607;
  uint64_t _M0Lm6outputS608;
  int32_t _M0L6_2atmpS1641;
  int32_t _M0L6_2atmpS1642;
  int32_t _M0L3expS631;
  uint64_t _M0L6_2atmpS1640;
  struct _M0TPB17FloatingDecimal64* _block_2200;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S575 = 0;
  _M0Lm2m2S576 = 0ull;
  if (_M0L12ieeeExponentS577 == 0u) {
    _M0Lm2e2S575 = -1076;
    _M0Lm2m2S576 = _M0L12ieeeMantissaS578;
  } else {
    int32_t _M0L6_2atmpS1544 = *(int32_t*)&_M0L12ieeeExponentS577;
    int32_t _M0L6_2atmpS1543 = _M0L6_2atmpS1544 - 1023;
    int32_t _M0L6_2atmpS1542 = _M0L6_2atmpS1543 - 52;
    _M0Lm2e2S575 = _M0L6_2atmpS1542 - 2;
    _M0Lm2m2S576 = 4503599627370496ull | _M0L12ieeeMantissaS578;
  }
  _M0L6_2atmpS1645 = _M0Lm2m2S576;
  _M0L6_2atmpS1644 = _M0L6_2atmpS1645 & 1ull;
  _M0L4evenS579 = _M0L6_2atmpS1644 == 0ull;
  _M0L6_2atmpS1643 = _M0Lm2m2S576;
  _M0L2mvS580 = 4ull * _M0L6_2atmpS1643;
  _M0L7mmShiftS581
  = _M0L12ieeeMantissaS578 != 0ull || _M0L12ieeeExponentS577 <= 1u;
  _M0Lm2vrS582 = 0ull;
  _M0Lm2vpS583 = 0ull;
  _M0Lm2vmS584 = 0ull;
  _M0Lm3e10S585 = 0;
  _M0Lm17vmIsTrailingZerosS586 = 0;
  _M0Lm17vrIsTrailingZerosS587 = 0;
  _M0L6_2atmpS1545 = _M0Lm2e2S575;
  if (_M0L6_2atmpS1545 >= 0) {
    int32_t _M0L6_2atmpS1567 = _M0Lm2e2S575;
    int32_t _M0L6_2atmpS1563;
    int32_t _M0L6_2atmpS1566;
    int32_t _M0L6_2atmpS1565;
    int32_t _M0L6_2atmpS1564;
    int32_t _M0L1qS588;
    int32_t _M0L6_2atmpS1562;
    int32_t _M0L6_2atmpS1561;
    int32_t _M0L1kS589;
    int32_t _M0L6_2atmpS1560;
    int32_t _M0L6_2atmpS1559;
    int32_t _M0L6_2atmpS1558;
    int32_t _M0L1iS590;
    struct _M0TPB8Pow5Pair _M0L4pow5S591;
    uint64_t _M0L6_2atmpS1557;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS592;
    uint64_t _M0L8_2avrOutS593;
    uint64_t _M0L8_2avpOutS594;
    uint64_t _M0L8_2avmOutS595;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1563 = _M0FPB9log10Pow2(_M0L6_2atmpS1567);
    _M0L6_2atmpS1566 = _M0Lm2e2S575;
    _M0L6_2atmpS1565 = _M0L6_2atmpS1566 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1564 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1565);
    _M0L1qS588 = _M0L6_2atmpS1563 - _M0L6_2atmpS1564;
    _M0Lm3e10S585 = _M0L1qS588;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1562 = _M0FPB8pow5bits(_M0L1qS588);
    _M0L6_2atmpS1561 = 125 + _M0L6_2atmpS1562;
    _M0L1kS589 = _M0L6_2atmpS1561 - 1;
    _M0L6_2atmpS1560 = _M0Lm2e2S575;
    _M0L6_2atmpS1559 = -_M0L6_2atmpS1560;
    _M0L6_2atmpS1558 = _M0L6_2atmpS1559 + _M0L1qS588;
    _M0L1iS590 = _M0L6_2atmpS1558 + _M0L1kS589;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S591 = _M0FPB22double__computeInvPow5(_M0L1qS588);
    _M0L6_2atmpS1557 = _M0Lm2m2S576;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS592
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1557, _M0L4pow5S591, _M0L1iS590, _M0L7mmShiftS581);
    _M0L8_2avrOutS593 = _M0L7_2abindS592.$0;
    _M0L8_2avpOutS594 = _M0L7_2abindS592.$1;
    _M0L8_2avmOutS595 = _M0L7_2abindS592.$2;
    _M0Lm2vrS582 = _M0L8_2avrOutS593;
    _M0Lm2vpS583 = _M0L8_2avpOutS594;
    _M0Lm2vmS584 = _M0L8_2avmOutS595;
    if (_M0L1qS588 <= 21) {
      int32_t _M0L6_2atmpS1553 = (int32_t)_M0L2mvS580;
      uint64_t _M0L6_2atmpS1556 = _M0L2mvS580 / 5ull;
      int32_t _M0L6_2atmpS1555 = (int32_t)_M0L6_2atmpS1556;
      int32_t _M0L6_2atmpS1554 = 5 * _M0L6_2atmpS1555;
      int32_t _M0L6mvMod5S596 = _M0L6_2atmpS1553 - _M0L6_2atmpS1554;
      if (_M0L6mvMod5S596 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS587
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS580, _M0L1qS588);
      } else if (_M0L4evenS579) {
        uint64_t _M0L6_2atmpS1547 = _M0L2mvS580 - 1ull;
        uint64_t _M0L6_2atmpS1548;
        uint64_t _M0L6_2atmpS1546;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1548 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS581);
        _M0L6_2atmpS1546 = _M0L6_2atmpS1547 - _M0L6_2atmpS1548;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS586
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1546, _M0L1qS588);
      } else {
        uint64_t _M0L6_2atmpS1549 = _M0Lm2vpS583;
        uint64_t _M0L6_2atmpS1552 = _M0L2mvS580 + 2ull;
        int32_t _M0L6_2atmpS1551;
        uint64_t _M0L6_2atmpS1550;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1551
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1552, _M0L1qS588);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1550 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1551);
        _M0Lm2vpS583 = _M0L6_2atmpS1549 - _M0L6_2atmpS1550;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1581 = _M0Lm2e2S575;
    int32_t _M0L6_2atmpS1580 = -_M0L6_2atmpS1581;
    int32_t _M0L6_2atmpS1575;
    int32_t _M0L6_2atmpS1579;
    int32_t _M0L6_2atmpS1578;
    int32_t _M0L6_2atmpS1577;
    int32_t _M0L6_2atmpS1576;
    int32_t _M0L1qS597;
    int32_t _M0L6_2atmpS1568;
    int32_t _M0L6_2atmpS1574;
    int32_t _M0L6_2atmpS1573;
    int32_t _M0L1iS598;
    int32_t _M0L6_2atmpS1572;
    int32_t _M0L1kS599;
    int32_t _M0L1jS600;
    struct _M0TPB8Pow5Pair _M0L4pow5S601;
    uint64_t _M0L6_2atmpS1571;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS602;
    uint64_t _M0L8_2avrOutS603;
    uint64_t _M0L8_2avpOutS604;
    uint64_t _M0L8_2avmOutS605;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1575 = _M0FPB9log10Pow5(_M0L6_2atmpS1580);
    _M0L6_2atmpS1579 = _M0Lm2e2S575;
    _M0L6_2atmpS1578 = -_M0L6_2atmpS1579;
    _M0L6_2atmpS1577 = _M0L6_2atmpS1578 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1576 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1577);
    _M0L1qS597 = _M0L6_2atmpS1575 - _M0L6_2atmpS1576;
    _M0L6_2atmpS1568 = _M0Lm2e2S575;
    _M0Lm3e10S585 = _M0L1qS597 + _M0L6_2atmpS1568;
    _M0L6_2atmpS1574 = _M0Lm2e2S575;
    _M0L6_2atmpS1573 = -_M0L6_2atmpS1574;
    _M0L1iS598 = _M0L6_2atmpS1573 - _M0L1qS597;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1572 = _M0FPB8pow5bits(_M0L1iS598);
    _M0L1kS599 = _M0L6_2atmpS1572 - 125;
    _M0L1jS600 = _M0L1qS597 - _M0L1kS599;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S601 = _M0FPB19double__computePow5(_M0L1iS598);
    _M0L6_2atmpS1571 = _M0Lm2m2S576;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS602
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1571, _M0L4pow5S601, _M0L1jS600, _M0L7mmShiftS581);
    _M0L8_2avrOutS603 = _M0L7_2abindS602.$0;
    _M0L8_2avpOutS604 = _M0L7_2abindS602.$1;
    _M0L8_2avmOutS605 = _M0L7_2abindS602.$2;
    _M0Lm2vrS582 = _M0L8_2avrOutS603;
    _M0Lm2vpS583 = _M0L8_2avpOutS604;
    _M0Lm2vmS584 = _M0L8_2avmOutS605;
    if (_M0L1qS597 <= 1) {
      _M0Lm17vrIsTrailingZerosS587 = 1;
      if (_M0L4evenS579) {
        int32_t _M0L6_2atmpS1569;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1569 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS581);
        _M0Lm17vmIsTrailingZerosS586 = _M0L6_2atmpS1569 == 1;
      } else {
        uint64_t _M0L6_2atmpS1570 = _M0Lm2vpS583;
        _M0Lm2vpS583 = _M0L6_2atmpS1570 - 1ull;
      }
    } else if (_M0L1qS597 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS587
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS580, _M0L1qS597);
    }
  }
  _M0Lm7removedS606 = 0;
  _M0Lm16lastRemovedDigitS607 = 0;
  _M0Lm6outputS608 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS586 || _M0Lm17vrIsTrailingZerosS587) {
    int32_t _if__result_2197;
    uint64_t _M0L6_2atmpS1611;
    uint64_t _M0L6_2atmpS1617;
    uint64_t _M0L6_2atmpS1618;
    int32_t _if__result_2198;
    int32_t _M0L6_2atmpS1614;
    int64_t _M0L6_2atmpS1613;
    uint64_t _M0L6_2atmpS1612;
    while (1) {
      uint64_t _M0L6_2atmpS1594 = _M0Lm2vpS583;
      uint64_t _M0L7vpDiv10S609 = _M0L6_2atmpS1594 / 10ull;
      uint64_t _M0L6_2atmpS1593 = _M0Lm2vmS584;
      uint64_t _M0L7vmDiv10S610 = _M0L6_2atmpS1593 / 10ull;
      uint64_t _M0L6_2atmpS1592;
      int32_t _M0L6_2atmpS1589;
      int32_t _M0L6_2atmpS1591;
      int32_t _M0L6_2atmpS1590;
      int32_t _M0L7vmMod10S612;
      uint64_t _M0L6_2atmpS1588;
      uint64_t _M0L7vrDiv10S613;
      uint64_t _M0L6_2atmpS1587;
      int32_t _M0L6_2atmpS1584;
      int32_t _M0L6_2atmpS1586;
      int32_t _M0L6_2atmpS1585;
      int32_t _M0L7vrMod10S614;
      int32_t _M0L6_2atmpS1583;
      if (_M0L7vpDiv10S609 <= _M0L7vmDiv10S610) {
        break;
      }
      _M0L6_2atmpS1592 = _M0Lm2vmS584;
      _M0L6_2atmpS1589 = (int32_t)_M0L6_2atmpS1592;
      _M0L6_2atmpS1591 = (int32_t)_M0L7vmDiv10S610;
      _M0L6_2atmpS1590 = 10 * _M0L6_2atmpS1591;
      _M0L7vmMod10S612 = _M0L6_2atmpS1589 - _M0L6_2atmpS1590;
      _M0L6_2atmpS1588 = _M0Lm2vrS582;
      _M0L7vrDiv10S613 = _M0L6_2atmpS1588 / 10ull;
      _M0L6_2atmpS1587 = _M0Lm2vrS582;
      _M0L6_2atmpS1584 = (int32_t)_M0L6_2atmpS1587;
      _M0L6_2atmpS1586 = (int32_t)_M0L7vrDiv10S613;
      _M0L6_2atmpS1585 = 10 * _M0L6_2atmpS1586;
      _M0L7vrMod10S614 = _M0L6_2atmpS1584 - _M0L6_2atmpS1585;
      _M0Lm17vmIsTrailingZerosS586
      = _M0Lm17vmIsTrailingZerosS586 && _M0L7vmMod10S612 == 0;
      if (_M0Lm17vrIsTrailingZerosS587) {
        int32_t _M0L6_2atmpS1582 = _M0Lm16lastRemovedDigitS607;
        _M0Lm17vrIsTrailingZerosS587 = _M0L6_2atmpS1582 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS587 = 0;
      }
      _M0Lm16lastRemovedDigitS607 = _M0L7vrMod10S614;
      _M0Lm2vrS582 = _M0L7vrDiv10S613;
      _M0Lm2vpS583 = _M0L7vpDiv10S609;
      _M0Lm2vmS584 = _M0L7vmDiv10S610;
      _M0L6_2atmpS1583 = _M0Lm7removedS606;
      _M0Lm7removedS606 = _M0L6_2atmpS1583 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS586) {
      while (1) {
        uint64_t _M0L6_2atmpS1607 = _M0Lm2vmS584;
        uint64_t _M0L7vmDiv10S615 = _M0L6_2atmpS1607 / 10ull;
        uint64_t _M0L6_2atmpS1606 = _M0Lm2vmS584;
        int32_t _M0L6_2atmpS1603 = (int32_t)_M0L6_2atmpS1606;
        int32_t _M0L6_2atmpS1605 = (int32_t)_M0L7vmDiv10S615;
        int32_t _M0L6_2atmpS1604 = 10 * _M0L6_2atmpS1605;
        int32_t _M0L7vmMod10S616 = _M0L6_2atmpS1603 - _M0L6_2atmpS1604;
        uint64_t _M0L6_2atmpS1602;
        uint64_t _M0L7vpDiv10S618;
        uint64_t _M0L6_2atmpS1601;
        uint64_t _M0L7vrDiv10S619;
        uint64_t _M0L6_2atmpS1600;
        int32_t _M0L6_2atmpS1597;
        int32_t _M0L6_2atmpS1599;
        int32_t _M0L6_2atmpS1598;
        int32_t _M0L7vrMod10S620;
        int32_t _M0L6_2atmpS1596;
        if (_M0L7vmMod10S616 != 0) {
          break;
        }
        _M0L6_2atmpS1602 = _M0Lm2vpS583;
        _M0L7vpDiv10S618 = _M0L6_2atmpS1602 / 10ull;
        _M0L6_2atmpS1601 = _M0Lm2vrS582;
        _M0L7vrDiv10S619 = _M0L6_2atmpS1601 / 10ull;
        _M0L6_2atmpS1600 = _M0Lm2vrS582;
        _M0L6_2atmpS1597 = (int32_t)_M0L6_2atmpS1600;
        _M0L6_2atmpS1599 = (int32_t)_M0L7vrDiv10S619;
        _M0L6_2atmpS1598 = 10 * _M0L6_2atmpS1599;
        _M0L7vrMod10S620 = _M0L6_2atmpS1597 - _M0L6_2atmpS1598;
        if (_M0Lm17vrIsTrailingZerosS587) {
          int32_t _M0L6_2atmpS1595 = _M0Lm16lastRemovedDigitS607;
          _M0Lm17vrIsTrailingZerosS587 = _M0L6_2atmpS1595 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS587 = 0;
        }
        _M0Lm16lastRemovedDigitS607 = _M0L7vrMod10S620;
        _M0Lm2vrS582 = _M0L7vrDiv10S619;
        _M0Lm2vpS583 = _M0L7vpDiv10S618;
        _M0Lm2vmS584 = _M0L7vmDiv10S615;
        _M0L6_2atmpS1596 = _M0Lm7removedS606;
        _M0Lm7removedS606 = _M0L6_2atmpS1596 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS587) {
      int32_t _M0L6_2atmpS1610 = _M0Lm16lastRemovedDigitS607;
      if (_M0L6_2atmpS1610 == 5) {
        uint64_t _M0L6_2atmpS1609 = _M0Lm2vrS582;
        uint64_t _M0L6_2atmpS1608 = _M0L6_2atmpS1609 % 2ull;
        _if__result_2197 = _M0L6_2atmpS1608 == 0ull;
      } else {
        _if__result_2197 = 0;
      }
    } else {
      _if__result_2197 = 0;
    }
    if (_if__result_2197) {
      _M0Lm16lastRemovedDigitS607 = 4;
    }
    _M0L6_2atmpS1611 = _M0Lm2vrS582;
    _M0L6_2atmpS1617 = _M0Lm2vrS582;
    _M0L6_2atmpS1618 = _M0Lm2vmS584;
    if (_M0L6_2atmpS1617 == _M0L6_2atmpS1618) {
      if (!_M0L4evenS579) {
        _if__result_2198 = 1;
      } else {
        int32_t _M0L6_2atmpS1616 = _M0Lm17vmIsTrailingZerosS586;
        _if__result_2198 = !_M0L6_2atmpS1616;
      }
    } else {
      _if__result_2198 = 0;
    }
    if (_if__result_2198) {
      _M0L6_2atmpS1614 = 1;
    } else {
      int32_t _M0L6_2atmpS1615 = _M0Lm16lastRemovedDigitS607;
      _M0L6_2atmpS1614 = _M0L6_2atmpS1615 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1613 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1614);
    _M0L6_2atmpS1612 = *(uint64_t*)&_M0L6_2atmpS1613;
    _M0Lm6outputS608 = _M0L6_2atmpS1611 + _M0L6_2atmpS1612;
  } else {
    int32_t _M0Lm7roundUpS621 = 0;
    uint64_t _M0L6_2atmpS1639 = _M0Lm2vpS583;
    uint64_t _M0L8vpDiv100S622 = _M0L6_2atmpS1639 / 100ull;
    uint64_t _M0L6_2atmpS1638 = _M0Lm2vmS584;
    uint64_t _M0L8vmDiv100S623 = _M0L6_2atmpS1638 / 100ull;
    uint64_t _M0L6_2atmpS1633;
    uint64_t _M0L6_2atmpS1636;
    uint64_t _M0L6_2atmpS1637;
    int32_t _M0L6_2atmpS1635;
    uint64_t _M0L6_2atmpS1634;
    if (_M0L8vpDiv100S622 > _M0L8vmDiv100S623) {
      uint64_t _M0L6_2atmpS1624 = _M0Lm2vrS582;
      uint64_t _M0L8vrDiv100S624 = _M0L6_2atmpS1624 / 100ull;
      uint64_t _M0L6_2atmpS1623 = _M0Lm2vrS582;
      int32_t _M0L6_2atmpS1620 = (int32_t)_M0L6_2atmpS1623;
      int32_t _M0L6_2atmpS1622 = (int32_t)_M0L8vrDiv100S624;
      int32_t _M0L6_2atmpS1621 = 100 * _M0L6_2atmpS1622;
      int32_t _M0L8vrMod100S625 = _M0L6_2atmpS1620 - _M0L6_2atmpS1621;
      int32_t _M0L6_2atmpS1619;
      _M0Lm7roundUpS621 = _M0L8vrMod100S625 >= 50;
      _M0Lm2vrS582 = _M0L8vrDiv100S624;
      _M0Lm2vpS583 = _M0L8vpDiv100S622;
      _M0Lm2vmS584 = _M0L8vmDiv100S623;
      _M0L6_2atmpS1619 = _M0Lm7removedS606;
      _M0Lm7removedS606 = _M0L6_2atmpS1619 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1632 = _M0Lm2vpS583;
      uint64_t _M0L7vpDiv10S626 = _M0L6_2atmpS1632 / 10ull;
      uint64_t _M0L6_2atmpS1631 = _M0Lm2vmS584;
      uint64_t _M0L7vmDiv10S627 = _M0L6_2atmpS1631 / 10ull;
      uint64_t _M0L6_2atmpS1630;
      uint64_t _M0L7vrDiv10S629;
      uint64_t _M0L6_2atmpS1629;
      int32_t _M0L6_2atmpS1626;
      int32_t _M0L6_2atmpS1628;
      int32_t _M0L6_2atmpS1627;
      int32_t _M0L7vrMod10S630;
      int32_t _M0L6_2atmpS1625;
      if (_M0L7vpDiv10S626 <= _M0L7vmDiv10S627) {
        break;
      }
      _M0L6_2atmpS1630 = _M0Lm2vrS582;
      _M0L7vrDiv10S629 = _M0L6_2atmpS1630 / 10ull;
      _M0L6_2atmpS1629 = _M0Lm2vrS582;
      _M0L6_2atmpS1626 = (int32_t)_M0L6_2atmpS1629;
      _M0L6_2atmpS1628 = (int32_t)_M0L7vrDiv10S629;
      _M0L6_2atmpS1627 = 10 * _M0L6_2atmpS1628;
      _M0L7vrMod10S630 = _M0L6_2atmpS1626 - _M0L6_2atmpS1627;
      _M0Lm7roundUpS621 = _M0L7vrMod10S630 >= 5;
      _M0Lm2vrS582 = _M0L7vrDiv10S629;
      _M0Lm2vpS583 = _M0L7vpDiv10S626;
      _M0Lm2vmS584 = _M0L7vmDiv10S627;
      _M0L6_2atmpS1625 = _M0Lm7removedS606;
      _M0Lm7removedS606 = _M0L6_2atmpS1625 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1633 = _M0Lm2vrS582;
    _M0L6_2atmpS1636 = _M0Lm2vrS582;
    _M0L6_2atmpS1637 = _M0Lm2vmS584;
    _M0L6_2atmpS1635
    = _M0L6_2atmpS1636 == _M0L6_2atmpS1637 || _M0Lm7roundUpS621;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1634 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1635);
    _M0Lm6outputS608 = _M0L6_2atmpS1633 + _M0L6_2atmpS1634;
  }
  _M0L6_2atmpS1641 = _M0Lm3e10S585;
  _M0L6_2atmpS1642 = _M0Lm7removedS606;
  _M0L3expS631 = _M0L6_2atmpS1641 + _M0L6_2atmpS1642;
  _M0L6_2atmpS1640 = _M0Lm6outputS608;
  _block_2200
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2200)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2200->$0 = _M0L6_2atmpS1640;
  _block_2200->$1 = _M0L3expS631;
  return _block_2200;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS574) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS574) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS573) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS573) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS572) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS572) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS571) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS571 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS571 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS571 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS571 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS571 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS571 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS571 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS571 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS571 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS571 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS571 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS571 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS571 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS571 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS571 >= 100ull) {
    return 3;
  }
  if (_M0L1vS571 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS554) {
  int32_t _M0L6_2atmpS1541;
  int32_t _M0L6_2atmpS1540;
  int32_t _M0L4baseS553;
  int32_t _M0L5base2S555;
  int32_t _M0L6offsetS556;
  int32_t _M0L6_2atmpS1539;
  uint64_t _M0L4mul0S557;
  int32_t _M0L6_2atmpS1538;
  int32_t _M0L6_2atmpS1537;
  uint64_t _M0L4mul1S558;
  uint64_t _M0L1mS559;
  struct _M0TPB7Umul128 _M0L7_2abindS560;
  uint64_t _M0L7_2alow1S561;
  uint64_t _M0L8_2ahigh1S562;
  struct _M0TPB7Umul128 _M0L7_2abindS563;
  uint64_t _M0L7_2alow0S564;
  uint64_t _M0L8_2ahigh0S565;
  uint64_t _M0L3sumS566;
  uint64_t _M0Lm5high1S567;
  int32_t _M0L6_2atmpS1535;
  int32_t _M0L6_2atmpS1536;
  int32_t _M0L5deltaS568;
  uint64_t _M0L6_2atmpS1534;
  uint64_t _M0L6_2atmpS1526;
  int32_t _M0L6_2atmpS1533;
  uint32_t _M0L6_2atmpS1530;
  int32_t _M0L6_2atmpS1532;
  int32_t _M0L6_2atmpS1531;
  uint32_t _M0L6_2atmpS1529;
  uint32_t _M0L6_2atmpS1528;
  uint64_t _M0L6_2atmpS1527;
  uint64_t _M0L1aS569;
  uint64_t _M0L6_2atmpS1525;
  uint64_t _M0L1bS570;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1541 = _M0L1iS554 + 26;
  _M0L6_2atmpS1540 = _M0L6_2atmpS1541 - 1;
  _M0L4baseS553 = _M0L6_2atmpS1540 / 26;
  _M0L5base2S555 = _M0L4baseS553 * 26;
  _M0L6offsetS556 = _M0L5base2S555 - _M0L1iS554;
  _M0L6_2atmpS1539 = _M0L4baseS553 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S557
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1539);
  _M0L6_2atmpS1538 = _M0L4baseS553 * 2;
  _M0L6_2atmpS1537 = _M0L6_2atmpS1538 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S558
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1537);
  if (_M0L6offsetS556 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S557, .$1 = _M0L4mul1S558};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS559
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS556);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS560 = _M0FPB7umul128(_M0L1mS559, _M0L4mul1S558);
  _M0L7_2alow1S561 = _M0L7_2abindS560.$0;
  _M0L8_2ahigh1S562 = _M0L7_2abindS560.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS563 = _M0FPB7umul128(_M0L1mS559, _M0L4mul0S557);
  _M0L7_2alow0S564 = _M0L7_2abindS563.$0;
  _M0L8_2ahigh0S565 = _M0L7_2abindS563.$1;
  _M0L3sumS566 = _M0L8_2ahigh0S565 + _M0L7_2alow1S561;
  _M0Lm5high1S567 = _M0L8_2ahigh1S562;
  if (_M0L3sumS566 < _M0L8_2ahigh0S565) {
    uint64_t _M0L6_2atmpS1524 = _M0Lm5high1S567;
    _M0Lm5high1S567 = _M0L6_2atmpS1524 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1535 = _M0FPB8pow5bits(_M0L5base2S555);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1536 = _M0FPB8pow5bits(_M0L1iS554);
  _M0L5deltaS568 = _M0L6_2atmpS1535 - _M0L6_2atmpS1536;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1534
  = _M0FPB13shiftright128(_M0L7_2alow0S564, _M0L3sumS566, _M0L5deltaS568);
  _M0L6_2atmpS1526 = _M0L6_2atmpS1534 + 1ull;
  _M0L6_2atmpS1533 = _M0L1iS554 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1530
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1533);
  _M0L6_2atmpS1532 = _M0L1iS554 % 16;
  _M0L6_2atmpS1531 = _M0L6_2atmpS1532 << 1;
  _M0L6_2atmpS1529 = _M0L6_2atmpS1530 >> (_M0L6_2atmpS1531 & 31);
  _M0L6_2atmpS1528 = _M0L6_2atmpS1529 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1527 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1528);
  _M0L1aS569 = _M0L6_2atmpS1526 + _M0L6_2atmpS1527;
  _M0L6_2atmpS1525 = _M0Lm5high1S567;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS570
  = _M0FPB13shiftright128(_M0L3sumS566, _M0L6_2atmpS1525, _M0L5deltaS568);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS569, .$1 = _M0L1bS570};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS536) {
  int32_t _M0L4baseS535;
  int32_t _M0L5base2S537;
  int32_t _M0L6offsetS538;
  int32_t _M0L6_2atmpS1523;
  uint64_t _M0L4mul0S539;
  int32_t _M0L6_2atmpS1522;
  int32_t _M0L6_2atmpS1521;
  uint64_t _M0L4mul1S540;
  uint64_t _M0L1mS541;
  struct _M0TPB7Umul128 _M0L7_2abindS542;
  uint64_t _M0L7_2alow1S543;
  uint64_t _M0L8_2ahigh1S544;
  struct _M0TPB7Umul128 _M0L7_2abindS545;
  uint64_t _M0L7_2alow0S546;
  uint64_t _M0L8_2ahigh0S547;
  uint64_t _M0L3sumS548;
  uint64_t _M0Lm5high1S549;
  int32_t _M0L6_2atmpS1519;
  int32_t _M0L6_2atmpS1520;
  int32_t _M0L5deltaS550;
  uint64_t _M0L6_2atmpS1511;
  int32_t _M0L6_2atmpS1518;
  uint32_t _M0L6_2atmpS1515;
  int32_t _M0L6_2atmpS1517;
  int32_t _M0L6_2atmpS1516;
  uint32_t _M0L6_2atmpS1514;
  uint32_t _M0L6_2atmpS1513;
  uint64_t _M0L6_2atmpS1512;
  uint64_t _M0L1aS551;
  uint64_t _M0L6_2atmpS1510;
  uint64_t _M0L1bS552;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS535 = _M0L1iS536 / 26;
  _M0L5base2S537 = _M0L4baseS535 * 26;
  _M0L6offsetS538 = _M0L1iS536 - _M0L5base2S537;
  _M0L6_2atmpS1523 = _M0L4baseS535 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S539
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1523);
  _M0L6_2atmpS1522 = _M0L4baseS535 * 2;
  _M0L6_2atmpS1521 = _M0L6_2atmpS1522 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S540
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1521);
  if (_M0L6offsetS538 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S539, .$1 = _M0L4mul1S540};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS541
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS538);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS542 = _M0FPB7umul128(_M0L1mS541, _M0L4mul1S540);
  _M0L7_2alow1S543 = _M0L7_2abindS542.$0;
  _M0L8_2ahigh1S544 = _M0L7_2abindS542.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS545 = _M0FPB7umul128(_M0L1mS541, _M0L4mul0S539);
  _M0L7_2alow0S546 = _M0L7_2abindS545.$0;
  _M0L8_2ahigh0S547 = _M0L7_2abindS545.$1;
  _M0L3sumS548 = _M0L8_2ahigh0S547 + _M0L7_2alow1S543;
  _M0Lm5high1S549 = _M0L8_2ahigh1S544;
  if (_M0L3sumS548 < _M0L8_2ahigh0S547) {
    uint64_t _M0L6_2atmpS1509 = _M0Lm5high1S549;
    _M0Lm5high1S549 = _M0L6_2atmpS1509 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1519 = _M0FPB8pow5bits(_M0L1iS536);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1520 = _M0FPB8pow5bits(_M0L5base2S537);
  _M0L5deltaS550 = _M0L6_2atmpS1519 - _M0L6_2atmpS1520;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1511
  = _M0FPB13shiftright128(_M0L7_2alow0S546, _M0L3sumS548, _M0L5deltaS550);
  _M0L6_2atmpS1518 = _M0L1iS536 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1515
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1518);
  _M0L6_2atmpS1517 = _M0L1iS536 % 16;
  _M0L6_2atmpS1516 = _M0L6_2atmpS1517 << 1;
  _M0L6_2atmpS1514 = _M0L6_2atmpS1515 >> (_M0L6_2atmpS1516 & 31);
  _M0L6_2atmpS1513 = _M0L6_2atmpS1514 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1512 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1513);
  _M0L1aS551 = _M0L6_2atmpS1511 + _M0L6_2atmpS1512;
  _M0L6_2atmpS1510 = _M0Lm5high1S549;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS552
  = _M0FPB13shiftright128(_M0L3sumS548, _M0L6_2atmpS1510, _M0L5deltaS550);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS551, .$1 = _M0L1bS552};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS509,
  struct _M0TPB8Pow5Pair _M0L3mulS506,
  int32_t _M0L1jS522,
  int32_t _M0L7mmShiftS524
) {
  uint64_t _M0L7_2amul0S505;
  uint64_t _M0L7_2amul1S507;
  uint64_t _M0L1mS508;
  struct _M0TPB7Umul128 _M0L7_2abindS510;
  uint64_t _M0L5_2aloS511;
  uint64_t _M0L6_2atmpS512;
  struct _M0TPB7Umul128 _M0L7_2abindS513;
  uint64_t _M0L6_2alo2S514;
  uint64_t _M0L6_2ahi2S515;
  uint64_t _M0L3midS516;
  uint64_t _M0L6_2atmpS1508;
  uint64_t _M0L2hiS517;
  uint64_t _M0L3lo2S518;
  uint64_t _M0L6_2atmpS1506;
  uint64_t _M0L6_2atmpS1507;
  uint64_t _M0L4mid2S519;
  uint64_t _M0L6_2atmpS1505;
  uint64_t _M0L3hi2S520;
  int32_t _M0L6_2atmpS1504;
  int32_t _M0L6_2atmpS1503;
  uint64_t _M0L2vpS521;
  uint64_t _M0Lm2vmS523;
  int32_t _M0L6_2atmpS1502;
  int32_t _M0L6_2atmpS1501;
  uint64_t _M0L2vrS534;
  uint64_t _M0L6_2atmpS1500;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S505 = _M0L3mulS506.$0;
  _M0L7_2amul1S507 = _M0L3mulS506.$1;
  _M0L1mS508 = _M0L1mS509 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS510 = _M0FPB7umul128(_M0L1mS508, _M0L7_2amul0S505);
  _M0L5_2aloS511 = _M0L7_2abindS510.$0;
  _M0L6_2atmpS512 = _M0L7_2abindS510.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS513 = _M0FPB7umul128(_M0L1mS508, _M0L7_2amul1S507);
  _M0L6_2alo2S514 = _M0L7_2abindS513.$0;
  _M0L6_2ahi2S515 = _M0L7_2abindS513.$1;
  _M0L3midS516 = _M0L6_2atmpS512 + _M0L6_2alo2S514;
  if (_M0L3midS516 < _M0L6_2atmpS512) {
    _M0L6_2atmpS1508 = 1ull;
  } else {
    _M0L6_2atmpS1508 = 0ull;
  }
  _M0L2hiS517 = _M0L6_2ahi2S515 + _M0L6_2atmpS1508;
  _M0L3lo2S518 = _M0L5_2aloS511 + _M0L7_2amul0S505;
  _M0L6_2atmpS1506 = _M0L3midS516 + _M0L7_2amul1S507;
  if (_M0L3lo2S518 < _M0L5_2aloS511) {
    _M0L6_2atmpS1507 = 1ull;
  } else {
    _M0L6_2atmpS1507 = 0ull;
  }
  _M0L4mid2S519 = _M0L6_2atmpS1506 + _M0L6_2atmpS1507;
  if (_M0L4mid2S519 < _M0L3midS516) {
    _M0L6_2atmpS1505 = 1ull;
  } else {
    _M0L6_2atmpS1505 = 0ull;
  }
  _M0L3hi2S520 = _M0L2hiS517 + _M0L6_2atmpS1505;
  _M0L6_2atmpS1504 = _M0L1jS522 - 64;
  _M0L6_2atmpS1503 = _M0L6_2atmpS1504 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS521
  = _M0FPB13shiftright128(_M0L4mid2S519, _M0L3hi2S520, _M0L6_2atmpS1503);
  _M0Lm2vmS523 = 0ull;
  if (_M0L7mmShiftS524) {
    uint64_t _M0L3lo3S525 = _M0L5_2aloS511 - _M0L7_2amul0S505;
    uint64_t _M0L6_2atmpS1490 = _M0L3midS516 - _M0L7_2amul1S507;
    uint64_t _M0L6_2atmpS1491;
    uint64_t _M0L4mid3S526;
    uint64_t _M0L6_2atmpS1489;
    uint64_t _M0L3hi3S527;
    int32_t _M0L6_2atmpS1488;
    int32_t _M0L6_2atmpS1487;
    if (_M0L5_2aloS511 < _M0L3lo3S525) {
      _M0L6_2atmpS1491 = 1ull;
    } else {
      _M0L6_2atmpS1491 = 0ull;
    }
    _M0L4mid3S526 = _M0L6_2atmpS1490 - _M0L6_2atmpS1491;
    if (_M0L3midS516 < _M0L4mid3S526) {
      _M0L6_2atmpS1489 = 1ull;
    } else {
      _M0L6_2atmpS1489 = 0ull;
    }
    _M0L3hi3S527 = _M0L2hiS517 - _M0L6_2atmpS1489;
    _M0L6_2atmpS1488 = _M0L1jS522 - 64;
    _M0L6_2atmpS1487 = _M0L6_2atmpS1488 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS523
    = _M0FPB13shiftright128(_M0L4mid3S526, _M0L3hi3S527, _M0L6_2atmpS1487);
  } else {
    uint64_t _M0L3lo3S528 = _M0L5_2aloS511 + _M0L5_2aloS511;
    uint64_t _M0L6_2atmpS1498 = _M0L3midS516 + _M0L3midS516;
    uint64_t _M0L6_2atmpS1499;
    uint64_t _M0L4mid3S529;
    uint64_t _M0L6_2atmpS1496;
    uint64_t _M0L6_2atmpS1497;
    uint64_t _M0L3hi3S530;
    uint64_t _M0L3lo4S531;
    uint64_t _M0L6_2atmpS1494;
    uint64_t _M0L6_2atmpS1495;
    uint64_t _M0L4mid4S532;
    uint64_t _M0L6_2atmpS1493;
    uint64_t _M0L3hi4S533;
    int32_t _M0L6_2atmpS1492;
    if (_M0L3lo3S528 < _M0L5_2aloS511) {
      _M0L6_2atmpS1499 = 1ull;
    } else {
      _M0L6_2atmpS1499 = 0ull;
    }
    _M0L4mid3S529 = _M0L6_2atmpS1498 + _M0L6_2atmpS1499;
    _M0L6_2atmpS1496 = _M0L2hiS517 + _M0L2hiS517;
    if (_M0L4mid3S529 < _M0L3midS516) {
      _M0L6_2atmpS1497 = 1ull;
    } else {
      _M0L6_2atmpS1497 = 0ull;
    }
    _M0L3hi3S530 = _M0L6_2atmpS1496 + _M0L6_2atmpS1497;
    _M0L3lo4S531 = _M0L3lo3S528 - _M0L7_2amul0S505;
    _M0L6_2atmpS1494 = _M0L4mid3S529 - _M0L7_2amul1S507;
    if (_M0L3lo3S528 < _M0L3lo4S531) {
      _M0L6_2atmpS1495 = 1ull;
    } else {
      _M0L6_2atmpS1495 = 0ull;
    }
    _M0L4mid4S532 = _M0L6_2atmpS1494 - _M0L6_2atmpS1495;
    if (_M0L4mid3S529 < _M0L4mid4S532) {
      _M0L6_2atmpS1493 = 1ull;
    } else {
      _M0L6_2atmpS1493 = 0ull;
    }
    _M0L3hi4S533 = _M0L3hi3S530 - _M0L6_2atmpS1493;
    _M0L6_2atmpS1492 = _M0L1jS522 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS523
    = _M0FPB13shiftright128(_M0L4mid4S532, _M0L3hi4S533, _M0L6_2atmpS1492);
  }
  _M0L6_2atmpS1502 = _M0L1jS522 - 64;
  _M0L6_2atmpS1501 = _M0L6_2atmpS1502 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS534
  = _M0FPB13shiftright128(_M0L3midS516, _M0L2hiS517, _M0L6_2atmpS1501);
  _M0L6_2atmpS1500 = _M0Lm2vmS523;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS534,
                                                .$1 = _M0L2vpS521,
                                                .$2 = _M0L6_2atmpS1500};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS503,
  int32_t _M0L1pS504
) {
  uint64_t _M0L6_2atmpS1486;
  uint64_t _M0L6_2atmpS1485;
  uint64_t _M0L6_2atmpS1484;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1486 = 1ull << (_M0L1pS504 & 63);
  _M0L6_2atmpS1485 = _M0L6_2atmpS1486 - 1ull;
  _M0L6_2atmpS1484 = _M0L5valueS503 & _M0L6_2atmpS1485;
  return _M0L6_2atmpS1484 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS501,
  int32_t _M0L1pS502
) {
  int32_t _M0L6_2atmpS1483;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1483 = _M0FPB10pow5Factor(_M0L5valueS501);
  return _M0L6_2atmpS1483 >= _M0L1pS502;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS496) {
  uint64_t _M0L6_2atmpS1474;
  uint64_t _M0L6_2atmpS1475;
  uint64_t _M0L6_2atmpS1476;
  uint64_t _M0L6_2atmpS1477;
  uint64_t _M0L6_2atmpS1482;
  int32_t _M0L5countS497;
  uint64_t _M0L1vS498;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1474 = _M0L5valueS496 % 5ull;
  if (_M0L6_2atmpS1474 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1475 = _M0L5valueS496 % 25ull;
  if (_M0L6_2atmpS1475 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1476 = _M0L5valueS496 % 125ull;
  if (_M0L6_2atmpS1476 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1477 = _M0L5valueS496 % 625ull;
  if (_M0L6_2atmpS1477 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1482 = _M0L5valueS496 / 625ull;
  _M0L5countS497 = 4;
  _M0L1vS498 = _M0L6_2atmpS1482;
  while (1) {
    if (_M0L1vS498 > 0ull) {
      uint64_t _M0L6_2atmpS1478 = _M0L1vS498 % 5ull;
      int32_t _M0L6_2atmpS1479;
      uint64_t _M0L6_2atmpS1480;
      if (_M0L6_2atmpS1478 != 0ull) {
        return _M0L5countS497;
      }
      _M0L6_2atmpS1479 = _M0L5countS497 + 1;
      _M0L6_2atmpS1480 = _M0L1vS498 / 5ull;
      _M0L5countS497 = _M0L6_2atmpS1479;
      _M0L1vS498 = _M0L6_2atmpS1480;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS500;
      moonbit_string_t _M0L6_2atmpS1481;
      int32_t _result_2202;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS500
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS500, (moonbit_string_t)moonbit_string_literal_11.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS500, _M0L5valueS496);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1481
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS500);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS500);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2202 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1481);
      moonbit_decref_cycle_free(_M0L6_2atmpS1481);
      return _result_2202;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS495,
  uint64_t _M0L2hiS493,
  int32_t _M0L4distS494
) {
  int32_t _M0L6_2atmpS1473;
  uint64_t _M0L6_2atmpS1471;
  uint64_t _M0L6_2atmpS1472;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1473 = 64 - _M0L4distS494;
  _M0L6_2atmpS1471 = _M0L2hiS493 << (_M0L6_2atmpS1473 & 63);
  _M0L6_2atmpS1472 = _M0L2loS495 >> (_M0L4distS494 & 63);
  return _M0L6_2atmpS1471 | _M0L6_2atmpS1472;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS483,
  uint64_t _M0L1bS486
) {
  uint64_t _M0L3aLoS482;
  uint64_t _M0L3aHiS484;
  uint64_t _M0L3bLoS485;
  uint64_t _M0L3bHiS487;
  uint64_t _M0L1xS488;
  uint64_t _M0L6_2atmpS1469;
  uint64_t _M0L6_2atmpS1470;
  uint64_t _M0L1yS489;
  uint64_t _M0L6_2atmpS1467;
  uint64_t _M0L6_2atmpS1468;
  uint64_t _M0L1zS490;
  uint64_t _M0L6_2atmpS1465;
  uint64_t _M0L6_2atmpS1466;
  uint64_t _M0L6_2atmpS1463;
  uint64_t _M0L6_2atmpS1464;
  uint64_t _M0L1wS491;
  uint64_t _M0L2loS492;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS482 = _M0L1aS483 & 4294967295ull;
  _M0L3aHiS484 = _M0L1aS483 >> 32;
  _M0L3bLoS485 = _M0L1bS486 & 4294967295ull;
  _M0L3bHiS487 = _M0L1bS486 >> 32;
  _M0L1xS488 = _M0L3aLoS482 * _M0L3bLoS485;
  _M0L6_2atmpS1469 = _M0L3aHiS484 * _M0L3bLoS485;
  _M0L6_2atmpS1470 = _M0L1xS488 >> 32;
  _M0L1yS489 = _M0L6_2atmpS1469 + _M0L6_2atmpS1470;
  _M0L6_2atmpS1467 = _M0L3aLoS482 * _M0L3bHiS487;
  _M0L6_2atmpS1468 = _M0L1yS489 & 4294967295ull;
  _M0L1zS490 = _M0L6_2atmpS1467 + _M0L6_2atmpS1468;
  _M0L6_2atmpS1465 = _M0L3aHiS484 * _M0L3bHiS487;
  _M0L6_2atmpS1466 = _M0L1yS489 >> 32;
  _M0L6_2atmpS1463 = _M0L6_2atmpS1465 + _M0L6_2atmpS1466;
  _M0L6_2atmpS1464 = _M0L1zS490 >> 32;
  _M0L1wS491 = _M0L6_2atmpS1463 + _M0L6_2atmpS1464;
  _M0L2loS492 = _M0L1aS483 * _M0L1bS486;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS492, .$1 = _M0L1wS491};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS480,
  int32_t _M0L4fromS477,
  int32_t _M0L2toS476
) {
  int32_t _M0L3lenS475;
  int32_t _M0L6_2atmpS1462;
  uint16_t* _M0L6bufferS478;
  int32_t _M0L1iS479;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS475 = _M0L2toS476 - _M0L4fromS477;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1462 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS478
  = (uint16_t*)moonbit_make_string(_M0L3lenS475, _M0L6_2atmpS1462);
  _M0L1iS479 = 0;
  while (1) {
    if (_M0L1iS479 < _M0L3lenS475) {
      int32_t _M0L6_2atmpS1460 = _M0L4fromS477 + _M0L1iS479;
      int32_t _M0L6_2atmpS1459;
      int32_t _M0L6_2atmpS1458;
      int32_t _M0L6_2atmpS1461;
      if (
        _M0L6_2atmpS1460 < 0
        || _M0L6_2atmpS1460 >= Moonbit_array_length(_M0L5bytesS480)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1459 = (int32_t)_M0L5bytesS480[_M0L6_2atmpS1460];
      _M0L6_2atmpS1458 = (uint16_t)_M0L6_2atmpS1459;
      if (
        _M0L1iS479 < 0 || _M0L1iS479 >= Moonbit_array_length(_M0L6bufferS478)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS478[_M0L1iS479] = _M0L6_2atmpS1458;
      _M0L6_2atmpS1461 = _M0L1iS479 + 1;
      _M0L1iS479 = _M0L6_2atmpS1461;
      continue;
    }
    break;
  }
  return _M0L6bufferS478;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS474) {
  int32_t _M0L6_2atmpS1457;
  uint32_t _M0L6_2atmpS1456;
  uint32_t _M0L6_2atmpS1455;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1457 = _M0L1eS474 * 78913;
  _M0L6_2atmpS1456 = *(uint32_t*)&_M0L6_2atmpS1457;
  _M0L6_2atmpS1455 = _M0L6_2atmpS1456 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1455;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS473) {
  int32_t _M0L6_2atmpS1454;
  uint32_t _M0L6_2atmpS1453;
  uint32_t _M0L6_2atmpS1452;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1454 = _M0L1eS473 * 732923;
  _M0L6_2atmpS1453 = *(uint32_t*)&_M0L6_2atmpS1454;
  _M0L6_2atmpS1452 = _M0L6_2atmpS1453 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1452;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS471,
  int32_t _M0L8exponentS472,
  int32_t _M0L8mantissaS469
) {
  moonbit_string_t _M0L1sS470;
  moonbit_string_t _result_2205;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS469) {
    return (moonbit_string_t)moonbit_string_literal_12.data;
  }
  if (_M0L4signS471) {
    _M0L1sS470 = (moonbit_string_t)moonbit_string_literal_13.data;
  } else {
    _M0L1sS470 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS472) {
    moonbit_string_t _result_2204;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2204
    = moonbit_add_string(_M0L1sS470, (moonbit_string_t)moonbit_string_literal_14.data);
    moonbit_decref_cycle_free(_M0L1sS470);
    return _result_2204;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2205
  = moonbit_add_string(_M0L1sS470, (moonbit_string_t)moonbit_string_literal_15.data);
  moonbit_decref_cycle_free(_M0L1sS470);
  return _result_2205;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS468) {
  int32_t _M0L6_2atmpS1451;
  uint32_t _M0L6_2atmpS1450;
  uint32_t _M0L6_2atmpS1449;
  int32_t _M0L6_2atmpS1448;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1451 = _M0L1eS468 * 1217359;
  _M0L6_2atmpS1450 = *(uint32_t*)&_M0L6_2atmpS1451;
  _M0L6_2atmpS1449 = _M0L6_2atmpS1450 >> 19;
  _M0L6_2atmpS1448 = *(int32_t*)&_M0L6_2atmpS1449;
  return _M0L6_2atmpS1448 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS467) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS467 != _M0L4selfS467) {
    return 0;
  } else if (_M0L4selfS467 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS467 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS467;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS466) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS466 != _M0L4selfS466) {
    return 0ll;
  } else if (_M0L4selfS466 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS466 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS466;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS463
) {
  float* _M0L6_2atmpS1445;
  struct _M0TPB5ArrayGfE* _block_2206;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1445 = (float*)moonbit_make_float_array_raw(_M0L3lenS463);
  _block_2206
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2206)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _block_2206->$0 = _M0L6_2atmpS1445;
  _block_2206->$1 = _M0L3lenS463;
  return _block_2206;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS464
) {
  uint8_t* _M0L6_2atmpS1446;
  struct _M0TPB5ArrayGbE* _block_2207;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1446 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS464);
  _block_2207
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2207)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 53, 0);
  _block_2207->$0 = _M0L6_2atmpS1446;
  _block_2207->$1 = _M0L3lenS464;
  return _block_2207;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS465
) {
  int32_t* _M0L6_2atmpS1447;
  struct _M0TPB5ArrayGiE* _block_2208;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1447 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS465);
  _block_2208
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2208)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _block_2208->$0 = _M0L6_2atmpS1447;
  _block_2208->$1 = _M0L3lenS465;
  return _block_2208;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS459,
  int32_t _M0L5indexS460
) {
  uint64_t* _M0L6_2atmpS1443;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1443 = _M0L4selfS459;
  if (
    _M0L5indexS460 < 0
    || _M0L5indexS460 >= Moonbit_array_length(_M0L6_2atmpS1443)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1443[_M0L5indexS460];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS461,
  int32_t _M0L5indexS462
) {
  uint32_t* _M0L6_2atmpS1444;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1444 = _M0L4selfS461;
  if (
    _M0L5indexS462 < 0
    || _M0L5indexS462 >= Moonbit_array_length(_M0L6_2atmpS1444)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1444[_M0L5indexS462];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS458
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS458, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS457) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS457, 10);
}

moonbit_string_t _M0IPC14bool4BoolPB4Show10to__string(int32_t _M0L4selfS456) {
  #line 26 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L4selfS456) {
    return (moonbit_string_t)moonbit_string_literal_16.data;
  } else {
    return (moonbit_string_t)moonbit_string_literal_17.data;
  }
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS455) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS455;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS449,
  moonbit_string_t _M0L5valueS451
) {
  int32_t _M0L3lenS1429;
  moonbit_string_t* _M0L6_2atmpS1431;
  int32_t _M0L6_2atmpS1430;
  int32_t _M0L6lengthS450;
  moonbit_string_t* _M0L3bufS1434;
  moonbit_string_t _M0L6_2aoldS2111;
  int32_t _M0L6_2atmpS1435;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1429 = _M0L4selfS449->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1431 = _M0MPC15array5Array6bufferGsE(_M0L4selfS449);
  _M0L6_2atmpS1430 = Moonbit_array_length(_M0L6_2atmpS1431);
  moonbit_decref_cycle_free(_M0L6_2atmpS1431);
  if (_M0L3lenS1429 == _M0L6_2atmpS1430) {
    int32_t _M0L3lenS1433 = _M0L4selfS449->$1;
    int32_t _M0L6_2atmpS1432 = _M0L3lenS1433 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS449, _M0L6_2atmpS1432);
  }
  _M0L6lengthS450 = _M0L4selfS449->$1;
  _M0L3bufS1434 = _M0L4selfS449->$0;
  _M0L6_2aoldS2111 = (moonbit_string_t)_M0L3bufS1434[_M0L6lengthS450];
  moonbit_decref_cycle_free(_M0L6_2aoldS2111);
  _M0L3bufS1434[_M0L6lengthS450] = _M0L5valueS451;
  _M0L6_2atmpS1435 = _M0L6lengthS450 + 1;
  _M0L4selfS449->$1 = _M0L6_2atmpS1435;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS452,
  struct _M0TUsiE* _M0L5valueS454
) {
  int32_t _M0L3lenS1436;
  struct _M0TUsiE** _M0L6_2atmpS1438;
  int32_t _M0L6_2atmpS1437;
  int32_t _M0L6lengthS453;
  struct _M0TUsiE** _M0L3bufS1441;
  struct _M0TUsiE* _M0L6_2aoldS2112;
  int32_t _M0L6_2atmpS1442;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1436 = _M0L4selfS452->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1438 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS452);
  _M0L6_2atmpS1437 = Moonbit_array_length(_M0L6_2atmpS1438);
  moonbit_decref_cycle_free(_M0L6_2atmpS1438);
  if (_M0L3lenS1436 == _M0L6_2atmpS1437) {
    int32_t _M0L3lenS1440 = _M0L4selfS452->$1;
    int32_t _M0L6_2atmpS1439 = _M0L3lenS1440 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS452, _M0L6_2atmpS1439);
  }
  _M0L6lengthS453 = _M0L4selfS452->$1;
  _M0L3bufS1441 = _M0L4selfS452->$0;
  _M0L6_2aoldS2112 = (struct _M0TUsiE*)_M0L3bufS1441[_M0L6lengthS453];
  if (_M0L6_2aoldS2112) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2112);
  }
  _M0L3bufS1441[_M0L6lengthS453] = _M0L5valueS454;
  _M0L6_2atmpS1442 = _M0L6lengthS453 + 1;
  _M0L4selfS452->$1 = _M0L6_2atmpS1442;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS442,
  int32_t _M0L8requiredS444
) {
  int32_t _M0L8old__capS441;
  int32_t _M0L3lenS1427;
  int32_t _M0L8new__capS443;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS441 = _M0MPC15array5Array8capacityGsE(_M0L4selfS442);
  _M0L3lenS1427 = _M0L4selfS442->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS443
  = _M0FPB23array__growth__capacity(_M0L8old__capS441, _M0L3lenS1427, _M0L8requiredS444);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS442, _M0L8new__capS443);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS446,
  int32_t _M0L8requiredS448
) {
  int32_t _M0L8old__capS445;
  int32_t _M0L3lenS1428;
  int32_t _M0L8new__capS447;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS445 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS446);
  _M0L3lenS1428 = _M0L4selfS446->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS447
  = _M0FPB23array__growth__capacity(_M0L8old__capS445, _M0L3lenS1428, _M0L8requiredS448);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS446, _M0L8new__capS447);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS430,
  int32_t _M0L13new__capacityS433
) {
  moonbit_string_t* _M0L8old__bufS429;
  int32_t _M0L3lenS431;
  int32_t _M0L9copy__lenS432;
  moonbit_string_t* _M0L8new__bufS434;
  moonbit_string_t* _M0L6_2aoldS2113;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS429 = _M0L4selfS430->$0;
  _M0L3lenS431 = _M0L4selfS430->$1;
  if (_M0L3lenS431 < _M0L13new__capacityS433) {
    _M0L9copy__lenS432 = _M0L3lenS431;
  } else {
    _M0L9copy__lenS432 = _M0L13new__capacityS433;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS429);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS434
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS429, _M0L13new__capacityS433, _M0L9copy__lenS432, 0, 0);
  _M0L6_2aoldS2113 = _M0L4selfS430->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2113);
  _M0L4selfS430->$0 = _M0L8new__bufS434;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS436,
  int32_t _M0L13new__capacityS439
) {
  struct _M0TUsiE** _M0L8old__bufS435;
  int32_t _M0L3lenS437;
  int32_t _M0L9copy__lenS438;
  struct _M0TUsiE** _M0L8new__bufS440;
  struct _M0TUsiE** _M0L6_2aoldS2114;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS435 = _M0L4selfS436->$0;
  _M0L3lenS437 = _M0L4selfS436->$1;
  if (_M0L3lenS437 < _M0L13new__capacityS439) {
    _M0L9copy__lenS438 = _M0L3lenS437;
  } else {
    _M0L9copy__lenS438 = _M0L13new__capacityS439;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS435);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS440
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS435, _M0L13new__capacityS439, _M0L9copy__lenS438, 0, 0);
  _M0L6_2aoldS2114 = _M0L4selfS436->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2114);
  _M0L4selfS436->$0 = _M0L8new__bufS440;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS427
) {
  moonbit_string_t* _M0L6_2atmpS1425;
  int32_t _result_2209;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1425 = _M0MPC15array5Array6bufferGsE(_M0L4selfS427);
  _result_2209 = Moonbit_array_length(_M0L6_2atmpS1425);
  moonbit_decref_cycle_free(_M0L6_2atmpS1425);
  return _result_2209;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS428
) {
  struct _M0TUsiE** _M0L6_2atmpS1426;
  int32_t _result_2210;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1426 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS428);
  _result_2210 = Moonbit_array_length(_M0L6_2atmpS1426);
  moonbit_decref_cycle_free(_M0L6_2atmpS1426);
  return _result_2210;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS423,
  int32_t _M0L3lenS421,
  int32_t _M0L8requiredS420
) {
  int32_t _M0L5startS422;
  int32_t _M0L5spaceS424;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS420 < _M0L3lenS421) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_18.data);
  }
  if (_M0L7currentS423 == 0) {
    _M0L5startS422 = 8;
  } else {
    _M0L5startS422 = _M0L7currentS423;
  }
  _M0L5spaceS424 = _M0L5startS422;
  while (1) {
    if (_M0L5spaceS424 < _M0L8requiredS420) {
      int32_t _M0L4nextS425 = _M0L5spaceS424 * 2;
      if (_M0L4nextS425 <= _M0L5spaceS424) {
        return _M0L8requiredS420;
      }
      _M0L5spaceS424 = _M0L4nextS425;
      continue;
    } else {
      return _M0L5spaceS424;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS419) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS419->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS414) {
  float* _M0L8_2afieldS2115;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2115 = _M0L4selfS414->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2115);
  return _M0L8_2afieldS2115;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS415) {
  uint8_t* _M0L8_2afieldS2116;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2116 = _M0L4selfS415->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2116);
  return _M0L8_2afieldS2116;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS416) {
  int32_t* _M0L8_2afieldS2117;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2117 = _M0L4selfS416->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2117);
  return _M0L8_2afieldS2117;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS417
) {
  moonbit_string_t* _M0L8_2afieldS2118;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2118 = _M0L4selfS417->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2118);
  return _M0L8_2afieldS2118;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS418
) {
  struct _M0TUsiE** _M0L8_2afieldS2119;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2119 = _M0L4selfS418->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2119);
  return _M0L8_2afieldS2119;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS413
) {
  #line 220 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref_cycle_free(_M0L4selfS413);
  return _M0L4selfS413;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS412,
  struct _M0TPC16string10StringView _M0L3strS410
) {
  int32_t _M0L3endS1423;
  int32_t _M0L5startS1424;
  int32_t _M0L8str__lenS409;
  int32_t _M0L3lenS1422;
  int32_t _M0L8requiredS411;
  uint16_t* _M0L4dataS1415;
  int32_t _M0L6_2atmpS1414;
  int32_t _if__result_2212;
  uint16_t* _M0L4dataS1416;
  int32_t _M0L3lenS1417;
  moonbit_string_t _M0L6_2atmpS1418;
  int32_t _M0L6_2atmpS1419;
  int32_t _M0L3lenS1421;
  int32_t _M0L6_2atmpS1420;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1423 = _M0L3strS410.$2;
  _M0L5startS1424 = _M0L3strS410.$1;
  _M0L8str__lenS409 = _M0L3endS1423 - _M0L5startS1424;
  if (_M0L8str__lenS409 == 0) {
    return 0;
  }
  _M0L3lenS1422 = _M0L4selfS412->$1;
  _M0L8requiredS411 = _M0L3lenS1422 + _M0L8str__lenS409;
  _M0L4dataS1415 = _M0L4selfS412->$0;
  _M0L6_2atmpS1414 = Moonbit_array_length(_M0L4dataS1415);
  if (_M0L8requiredS411 > _M0L6_2atmpS1414) {
    _if__result_2212 = 1;
  } else {
    int32_t _M0L3lenS1413 = _M0L4selfS412->$1;
    _if__result_2212 = _M0L8requiredS411 < _M0L3lenS1413;
  }
  if (_if__result_2212) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS412, _M0L8requiredS411);
  }
  _M0L4dataS1416 = _M0L4selfS412->$0;
  _M0L3lenS1417 = _M0L4selfS412->$1;
  moonbit_incref_cycle_free(_M0L4dataS1416);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1418 = _M0MPC16string10StringView4data(_M0L3strS410);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1419 = _M0MPC16string10StringView13start__offset(_M0L3strS410);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1416, _M0L3lenS1417, _M0L6_2atmpS1418, _M0L6_2atmpS1419, _M0L8str__lenS409);
  moonbit_decref_cycle_free(_M0L4dataS1416);
  moonbit_decref_cycle_free(_M0L6_2atmpS1418);
  _M0L3lenS1421 = _M0L4selfS412->$1;
  _M0L6_2atmpS1420 = _M0L3lenS1421 + _M0L8str__lenS409;
  _M0L4selfS412->$1 = _M0L6_2atmpS1420;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS406,
  int32_t _M0L5startS404,
  int32_t _M0L3endS405
) {
  int32_t _if__result_2213;
  int32_t _M0L3lenS407;
  int32_t _M0L6_2atmpS1412;
  moonbit_bytes_t _M0L5bytesS408;
  moonbit_bytes_t _M0L6_2atmpS1411;
  moonbit_string_t _result_2214;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS404 == 0) {
    int32_t _M0L6_2atmpS1410 = Moonbit_array_length(_M0L3strS406);
    _if__result_2213 = _M0L3endS405 == _M0L6_2atmpS1410;
  } else {
    _if__result_2213 = 0;
  }
  if (_if__result_2213) {
    moonbit_incref_cycle_free(_M0L3strS406);
    return _M0L3strS406;
  }
  _M0L3lenS407 = _M0L3endS405 - _M0L5startS404;
  _M0L6_2atmpS1412 = _M0L3lenS407 * 2;
  _M0L5bytesS408 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1412, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS408, 0, _M0L3strS406, _M0L5startS404, _M0L3lenS407);
  _M0L6_2atmpS1411 = _M0L5bytesS408;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2214
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1411, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1411);
  return _result_2214;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS399,
  int32_t _M0L6offsetS403,
  int64_t _M0L6lengthS401
) {
  int32_t _M0L3lenS398;
  int32_t _M0L6lengthS400;
  int32_t _if__result_2215;
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L3lenS398 = Moonbit_array_length(_M0L4selfS399);
  if (_M0L6lengthS401 == 4294967296ll) {
    _M0L6lengthS400 = _M0L3lenS398 - _M0L6offsetS403;
  } else {
    int64_t _M0L7_2aSomeS402 = _M0L6lengthS401;
    _M0L6lengthS400 = (int32_t)_M0L7_2aSomeS402;
  }
  if (_M0L6offsetS403 >= 0) {
    if (_M0L6lengthS400 >= 0) {
      int32_t _M0L6_2atmpS1409 = _M0L6offsetS403 + _M0L6lengthS400;
      _if__result_2215 = _M0L6_2atmpS1409 <= _M0L3lenS398;
    } else {
      _if__result_2215 = 0;
    }
  } else {
    _if__result_2215 = 0;
  }
  if (_if__result_2215) {
    moonbit_incref_cycle_free(_M0L4selfS399);
    #line 85 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    return _M0FPB19unsafe__sub__string(_M0L4selfS399, _M0L6offsetS403, _M0L6lengthS400);
  } else {
    #line 84 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array10FixedArray18blit__from__string(
  moonbit_bytes_t _M0L4selfS390,
  int32_t _M0L13bytes__offsetS385,
  moonbit_string_t _M0L3strS392,
  int32_t _M0L11str__offsetS388,
  int32_t _M0L6lengthS386
) {
  int32_t _M0L6_2atmpS1408;
  int32_t _M0L6_2atmpS1407;
  int32_t _M0L2e1S384;
  int32_t _M0L6_2atmpS1406;
  int32_t _M0L2e2S387;
  int32_t _M0L4len1S389;
  int32_t _M0L4len2S391;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1408 = _M0L6lengthS386 * 2;
  _M0L6_2atmpS1407 = _M0L13bytes__offsetS385 + _M0L6_2atmpS1408;
  _M0L2e1S384 = _M0L6_2atmpS1407 - 1;
  _M0L6_2atmpS1406 = _M0L11str__offsetS388 + _M0L6lengthS386;
  _M0L2e2S387 = _M0L6_2atmpS1406 - 1;
  _M0L4len1S389 = Moonbit_array_length(_M0L4selfS390);
  _M0L4len2S391 = Moonbit_array_length(_M0L3strS392);
  if (
    _M0L6lengthS386 >= 0
    && _M0L13bytes__offsetS385 >= 0
    && _M0L2e1S384 < _M0L4len1S389
    && _M0L11str__offsetS388 >= 0
    && _M0L2e2S387 < _M0L4len2S391
  ) {
    int32_t _M0L16end__str__offsetS393 =
      _M0L11str__offsetS388 + _M0L6lengthS386;
    int32_t _M0L1iS394 = _M0L11str__offsetS388;
    int32_t _M0L1jS395 = _M0L13bytes__offsetS385;
    while (1) {
      if (_M0L1iS394 < _M0L16end__str__offsetS393) {
        int32_t _M0L6_2atmpS1403 = _M0L3strS392[_M0L1iS394];
        int32_t _M0L6_2atmpS1402 = (int32_t)_M0L6_2atmpS1403;
        uint32_t _M0L1cS396 = *(uint32_t*)&_M0L6_2atmpS1402;
        uint32_t _M0L6_2atmpS1398 = _M0L1cS396 & 255u;
        int32_t _M0L6_2atmpS1397;
        int32_t _M0L6_2atmpS1399;
        uint32_t _M0L6_2atmpS1401;
        int32_t _M0L6_2atmpS1400;
        int32_t _M0L6_2atmpS1404;
        int32_t _M0L6_2atmpS1405;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1397 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1398);
        if (
          _M0L1jS395 < 0 || _M0L1jS395 >= Moonbit_array_length(_M0L4selfS390)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS390[_M0L1jS395] = _M0L6_2atmpS1397;
        _M0L6_2atmpS1399 = _M0L1jS395 + 1;
        _M0L6_2atmpS1401 = _M0L1cS396 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1400 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1401);
        if (
          _M0L6_2atmpS1399 < 0
          || _M0L6_2atmpS1399 >= Moonbit_array_length(_M0L4selfS390)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS390[_M0L6_2atmpS1399] = _M0L6_2atmpS1400;
        _M0L6_2atmpS1404 = _M0L1iS394 + 1;
        _M0L6_2atmpS1405 = _M0L1jS395 + 2;
        _M0L1iS394 = _M0L6_2atmpS1404;
        _M0L1jS395 = _M0L6_2atmpS1405;
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

int32_t _M0MPC14uint4UInt8to__byte(uint32_t _M0L4selfS383) {
  int32_t _M0L6_2atmpS1396;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1396 = *(int32_t*)&_M0L4selfS383;
  return _M0L6_2atmpS1396 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS375,
  int32_t _M0L5radixS374
) {
  uint16_t* _M0L6bufferS376;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS374 < 2 || _M0L5radixS374 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_19.data);
  }
  if (_M0L4selfS375 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_10.data;
  }
  switch (_M0L5radixS374) {
    case 10: {
      int32_t _M0L3lenS377;
      uint16_t* _M0L6bufferS378;
      #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS377 = _M0FPB12dec__count64(_M0L4selfS375);
      _M0L6bufferS378 = (uint16_t*)moonbit_make_string(_M0L3lenS377, 0);
      #line 624 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS378, _M0L4selfS375, 0, _M0L3lenS377);
      _M0L6bufferS376 = _M0L6bufferS378;
      break;
    }
    
    case 16: {
      int32_t _M0L3lenS379;
      uint16_t* _M0L6bufferS380;
      #line 628 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS379 = _M0FPB12hex__count64(_M0L4selfS375);
      _M0L6bufferS380 = (uint16_t*)moonbit_make_string(_M0L3lenS379, 0);
      #line 630 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS380, _M0L4selfS375, 0, _M0L3lenS379);
      _M0L6bufferS376 = _M0L6bufferS380;
      break;
    }
    default: {
      int32_t _M0L3lenS381;
      uint16_t* _M0L6bufferS382;
      #line 634 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS381 = _M0FPB14radix__count64(_M0L4selfS375, _M0L5radixS374);
      _M0L6bufferS382 = (uint16_t*)moonbit_make_string(_M0L3lenS381, 0);
      #line 636 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS382, _M0L4selfS375, 0, _M0L3lenS381, _M0L5radixS374);
      _M0L6bufferS376 = _M0L6bufferS382;
      break;
    }
  }
  return _M0L6bufferS376;
}

moonbit_string_t _M0MPC15int645Int6418to__string_2einner(
  int64_t _M0L4selfS358,
  int32_t _M0L5radixS357
) {
  int32_t _M0L12is__negativeS359;
  uint64_t _M0L3numS360;
  uint16_t* _M0L6bufferS361;
  #line 548 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS357 < 2 || _M0L5radixS357 > 36) {
    #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_19.data);
  }
  if (_M0L4selfS358 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_10.data;
  }
  _M0L12is__negativeS359 = _M0L4selfS358 < 0ll;
  if (_M0L12is__negativeS359) {
    int64_t _M0L6_2atmpS1395 = -_M0L4selfS358;
    _M0L3numS360 = *(uint64_t*)&_M0L6_2atmpS1395;
  } else {
    _M0L3numS360 = *(uint64_t*)&_M0L4selfS358;
  }
  switch (_M0L5radixS357) {
    case 10: {
      int32_t _M0L10digit__lenS362;
      int32_t _M0L6_2atmpS1392;
      int32_t _M0L10total__lenS363;
      uint16_t* _M0L6bufferS364;
      int32_t _M0L12digit__startS365;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS362 = _M0FPB12dec__count64(_M0L3numS360);
      if (_M0L12is__negativeS359) {
        _M0L6_2atmpS1392 = 1;
      } else {
        _M0L6_2atmpS1392 = 0;
      }
      _M0L10total__lenS363 = _M0L10digit__lenS362 + _M0L6_2atmpS1392;
      _M0L6bufferS364
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS363, 0);
      if (_M0L12is__negativeS359) {
        _M0L12digit__startS365 = 1;
      } else {
        _M0L12digit__startS365 = 0;
      }
      #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS364, _M0L3numS360, _M0L12digit__startS365, _M0L10total__lenS363);
      _M0L6bufferS361 = _M0L6bufferS364;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS366;
      int32_t _M0L6_2atmpS1393;
      int32_t _M0L10total__lenS367;
      uint16_t* _M0L6bufferS368;
      int32_t _M0L12digit__startS369;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS366 = _M0FPB12hex__count64(_M0L3numS360);
      if (_M0L12is__negativeS359) {
        _M0L6_2atmpS1393 = 1;
      } else {
        _M0L6_2atmpS1393 = 0;
      }
      _M0L10total__lenS367 = _M0L10digit__lenS366 + _M0L6_2atmpS1393;
      _M0L6bufferS368
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS367, 0);
      if (_M0L12is__negativeS359) {
        _M0L12digit__startS369 = 1;
      } else {
        _M0L12digit__startS369 = 0;
      }
      #line 585 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS368, _M0L3numS360, _M0L12digit__startS369, _M0L10total__lenS367);
      _M0L6bufferS361 = _M0L6bufferS368;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS370;
      int32_t _M0L6_2atmpS1394;
      int32_t _M0L10total__lenS371;
      uint16_t* _M0L6bufferS372;
      int32_t _M0L12digit__startS373;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS370
      = _M0FPB14radix__count64(_M0L3numS360, _M0L5radixS357);
      if (_M0L12is__negativeS359) {
        _M0L6_2atmpS1394 = 1;
      } else {
        _M0L6_2atmpS1394 = 0;
      }
      _M0L10total__lenS371 = _M0L10digit__lenS370 + _M0L6_2atmpS1394;
      _M0L6bufferS372
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS371, 0);
      if (_M0L12is__negativeS359) {
        _M0L12digit__startS373 = 1;
      } else {
        _M0L12digit__startS373 = 0;
      }
      #line 593 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS372, _M0L3numS360, _M0L12digit__startS373, _M0L10total__lenS371, _M0L5radixS357);
      _M0L6bufferS361 = _M0L6bufferS372;
      break;
    }
  }
  if (_M0L12is__negativeS359) {
    _M0L6bufferS361[0] = 45;
  }
  return _M0L6bufferS361;
}

int32_t _M0FPB22int64__to__string__dec(
  uint16_t* _M0L6bufferS343,
  uint64_t _M0L3numS355,
  int32_t _M0L12digit__startS344,
  int32_t _M0L10total__lenS356
) {
  int32_t _M0L6_2atmpS1391;
  uint64_t _M0L3numS333;
  int32_t _M0L6offsetS334;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1391 = _M0L10total__lenS356 - _M0L12digit__startS344;
  _M0L3numS333 = _M0L3numS355;
  _M0L6offsetS334 = _M0L6_2atmpS1391;
  while (1) {
    if (_M0L3numS333 >= 10000ull) {
      uint64_t _M0L1tS335 = _M0L3numS333 / 10000ull;
      uint64_t _M0L6_2atmpS1368 = _M0L3numS333 % 10000ull;
      int32_t _M0L1rS336 = (int32_t)_M0L6_2atmpS1368;
      int32_t _M0L2d1S337 = _M0L1rS336 / 100;
      int32_t _M0L2d2S338 = _M0L1rS336 % 100;
      int32_t _M0L6_2atmpS1367 = _M0L2d1S337 / 10;
      int32_t _M0L6_2atmpS1366 = 48 + _M0L6_2atmpS1367;
      int32_t _M0L6d1__hiS339 = (uint16_t)_M0L6_2atmpS1366;
      int32_t _M0L6_2atmpS1365 = _M0L2d1S337 % 10;
      int32_t _M0L6_2atmpS1364 = 48 + _M0L6_2atmpS1365;
      int32_t _M0L6d1__loS340 = (uint16_t)_M0L6_2atmpS1364;
      int32_t _M0L6_2atmpS1363 = _M0L2d2S338 / 10;
      int32_t _M0L6_2atmpS1362 = 48 + _M0L6_2atmpS1363;
      int32_t _M0L6d2__hiS341 = (uint16_t)_M0L6_2atmpS1362;
      int32_t _M0L6_2atmpS1361 = _M0L2d2S338 % 10;
      int32_t _M0L6_2atmpS1360 = 48 + _M0L6_2atmpS1361;
      int32_t _M0L6d2__loS342 = (uint16_t)_M0L6_2atmpS1360;
      int32_t _M0L6_2atmpS1352 = _M0L12digit__startS344 + _M0L6offsetS334;
      int32_t _M0L6_2atmpS1351 = _M0L6_2atmpS1352 - 4;
      int32_t _M0L6_2atmpS1354;
      int32_t _M0L6_2atmpS1353;
      int32_t _M0L6_2atmpS1356;
      int32_t _M0L6_2atmpS1355;
      int32_t _M0L6_2atmpS1358;
      int32_t _M0L6_2atmpS1357;
      int32_t _M0L6_2atmpS1359;
      _M0L6bufferS343[_M0L6_2atmpS1351] = _M0L6d1__hiS339;
      _M0L6_2atmpS1354 = _M0L12digit__startS344 + _M0L6offsetS334;
      _M0L6_2atmpS1353 = _M0L6_2atmpS1354 - 3;
      _M0L6bufferS343[_M0L6_2atmpS1353] = _M0L6d1__loS340;
      _M0L6_2atmpS1356 = _M0L12digit__startS344 + _M0L6offsetS334;
      _M0L6_2atmpS1355 = _M0L6_2atmpS1356 - 2;
      _M0L6bufferS343[_M0L6_2atmpS1355] = _M0L6d2__hiS341;
      _M0L6_2atmpS1358 = _M0L12digit__startS344 + _M0L6offsetS334;
      _M0L6_2atmpS1357 = _M0L6_2atmpS1358 - 1;
      _M0L6bufferS343[_M0L6_2atmpS1357] = _M0L6d2__loS342;
      _M0L6_2atmpS1359 = _M0L6offsetS334 - 4;
      _M0L3numS333 = _M0L1tS335;
      _M0L6offsetS334 = _M0L6_2atmpS1359;
      continue;
    } else {
      int32_t _M0L6_2atmpS1390 = (int32_t)_M0L3numS333;
      int32_t _M0L9remainingS346 = _M0L6_2atmpS1390;
      int32_t _M0L6offsetS347 = _M0L6offsetS334;
      while (1) {
        if (_M0L9remainingS346 >= 100) {
          int32_t _M0L1tS348 = _M0L9remainingS346 / 100;
          int32_t _M0L1dS349 = _M0L9remainingS346 % 100;
          int32_t _M0L6_2atmpS1377 = _M0L1dS349 / 10;
          int32_t _M0L6_2atmpS1376 = 48 + _M0L6_2atmpS1377;
          int32_t _M0L5d__hiS350 = (uint16_t)_M0L6_2atmpS1376;
          int32_t _M0L6_2atmpS1375 = _M0L1dS349 % 10;
          int32_t _M0L6_2atmpS1374 = 48 + _M0L6_2atmpS1375;
          int32_t _M0L5d__loS351 = (uint16_t)_M0L6_2atmpS1374;
          int32_t _M0L6_2atmpS1370 = _M0L12digit__startS344 + _M0L6offsetS347;
          int32_t _M0L6_2atmpS1369 = _M0L6_2atmpS1370 - 2;
          int32_t _M0L6_2atmpS1372;
          int32_t _M0L6_2atmpS1371;
          int32_t _M0L6_2atmpS1373;
          _M0L6bufferS343[_M0L6_2atmpS1369] = _M0L5d__hiS350;
          _M0L6_2atmpS1372 = _M0L12digit__startS344 + _M0L6offsetS347;
          _M0L6_2atmpS1371 = _M0L6_2atmpS1372 - 1;
          _M0L6bufferS343[_M0L6_2atmpS1371] = _M0L5d__loS351;
          _M0L6_2atmpS1373 = _M0L6offsetS347 - 2;
          _M0L9remainingS346 = _M0L1tS348;
          _M0L6offsetS347 = _M0L6_2atmpS1373;
          continue;
        } else if (_M0L9remainingS346 >= 10) {
          int32_t _M0L6_2atmpS1385 = _M0L9remainingS346 / 10;
          int32_t _M0L6_2atmpS1384 = 48 + _M0L6_2atmpS1385;
          int32_t _M0L5d__hiS353 = (uint16_t)_M0L6_2atmpS1384;
          int32_t _M0L6_2atmpS1383 = _M0L9remainingS346 % 10;
          int32_t _M0L6_2atmpS1382 = 48 + _M0L6_2atmpS1383;
          int32_t _M0L5d__loS354 = (uint16_t)_M0L6_2atmpS1382;
          int32_t _M0L6_2atmpS1379 = _M0L12digit__startS344 + _M0L6offsetS347;
          int32_t _M0L6_2atmpS1378 = _M0L6_2atmpS1379 - 2;
          int32_t _M0L6_2atmpS1381;
          int32_t _M0L6_2atmpS1380;
          _M0L6bufferS343[_M0L6_2atmpS1378] = _M0L5d__hiS353;
          _M0L6_2atmpS1381 = _M0L12digit__startS344 + _M0L6offsetS347;
          _M0L6_2atmpS1380 = _M0L6_2atmpS1381 - 1;
          _M0L6bufferS343[_M0L6_2atmpS1380] = _M0L5d__loS354;
        } else {
          int32_t _M0L6_2atmpS1389 = _M0L12digit__startS344 + _M0L6offsetS347;
          int32_t _M0L6_2atmpS1386 = _M0L6_2atmpS1389 - 1;
          int32_t _M0L6_2atmpS1388 = 48 + _M0L9remainingS346;
          int32_t _M0L6_2atmpS1387 = (uint16_t)_M0L6_2atmpS1388;
          _M0L6bufferS343[_M0L6_2atmpS1386] = _M0L6_2atmpS1387;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB26int64__to__string__generic(
  uint16_t* _M0L6bufferS323,
  uint64_t _M0L3numS327,
  int32_t _M0L12digit__startS324,
  int32_t _M0L10total__lenS326,
  int32_t _M0L5radixS317
) {
  uint64_t _M0L4baseS316;
  int32_t _M0L6_2atmpS1336;
  int32_t _M0L6_2atmpS1335;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS316 = _M0MPC13int3Int10to__uint64(_M0L5radixS317);
  _M0L6_2atmpS1336 = _M0L5radixS317 - 1;
  _M0L6_2atmpS1335 = _M0L5radixS317 & _M0L6_2atmpS1336;
  if (_M0L6_2atmpS1335 == 0) {
    int32_t _M0L5shiftS318;
    uint64_t _M0L4maskS319;
    int32_t _M0L6_2atmpS1343;
    int32_t _M0L6offsetS320;
    uint64_t _M0L1nS321;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS318 = moonbit_ctz32(_M0L5radixS317);
    _M0L4maskS319 = _M0L4baseS316 - 1ull;
    _M0L6_2atmpS1343 = _M0L10total__lenS326 - _M0L12digit__startS324;
    _M0L6offsetS320 = _M0L6_2atmpS1343;
    _M0L1nS321 = _M0L3numS327;
    while (1) {
      if (_M0L1nS321 > 0ull) {
        uint64_t _M0L6_2atmpS1342 = _M0L1nS321 & _M0L4maskS319;
        int32_t _M0L5digitS322 = (int32_t)_M0L6_2atmpS1342;
        int32_t _M0L6_2atmpS1339 = _M0L12digit__startS324 + _M0L6offsetS320;
        int32_t _M0L6_2atmpS1337 = _M0L6_2atmpS1339 - 1;
        int32_t _M0L6_2atmpS1338 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS322];
        int32_t _M0L6_2atmpS1340;
        uint64_t _M0L6_2atmpS1341;
        _M0L6bufferS323[_M0L6_2atmpS1337] = _M0L6_2atmpS1338;
        _M0L6_2atmpS1340 = _M0L6offsetS320 - 1;
        _M0L6_2atmpS1341 = _M0L1nS321 >> (_M0L5shiftS318 & 63);
        _M0L6offsetS320 = _M0L6_2atmpS1340;
        _M0L1nS321 = _M0L6_2atmpS1341;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1350 = _M0L10total__lenS326 - _M0L12digit__startS324;
    int32_t _M0L6offsetS328 = _M0L6_2atmpS1350;
    uint64_t _M0L1nS329 = _M0L3numS327;
    while (1) {
      if (_M0L1nS329 > 0ull) {
        uint64_t _M0L1qS330 = _M0L1nS329 / _M0L4baseS316;
        uint64_t _M0L6_2atmpS1349 = _M0L1qS330 * _M0L4baseS316;
        uint64_t _M0L6_2atmpS1348 = _M0L1nS329 - _M0L6_2atmpS1349;
        int32_t _M0L5digitS331 = (int32_t)_M0L6_2atmpS1348;
        int32_t _M0L6_2atmpS1346 = _M0L12digit__startS324 + _M0L6offsetS328;
        int32_t _M0L6_2atmpS1344 = _M0L6_2atmpS1346 - 1;
        int32_t _M0L6_2atmpS1345 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS331];
        int32_t _M0L6_2atmpS1347;
        _M0L6bufferS323[_M0L6_2atmpS1344] = _M0L6_2atmpS1345;
        _M0L6_2atmpS1347 = _M0L6offsetS328 - 1;
        _M0L6offsetS328 = _M0L6_2atmpS1347;
        _M0L1nS329 = _M0L1qS330;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB22int64__to__string__hex(
  uint16_t* _M0L6bufferS310,
  uint64_t _M0L3numS315,
  int32_t _M0L12digit__startS311,
  int32_t _M0L10total__lenS314
) {
  int32_t _M0L6_2atmpS1334;
  int32_t _M0L6offsetS305;
  uint64_t _M0L1nS306;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1334 = _M0L10total__lenS314 - _M0L12digit__startS311;
  _M0L6offsetS305 = _M0L6_2atmpS1334;
  _M0L1nS306 = _M0L3numS315;
  while (1) {
    if (_M0L6offsetS305 >= 2) {
      uint64_t _M0L6_2atmpS1331 = _M0L1nS306 & 255ull;
      int32_t _M0L9byte__valS307 = (int32_t)_M0L6_2atmpS1331;
      int32_t _M0L2hiS308 = _M0L9byte__valS307 / 16;
      int32_t _M0L2loS309 = _M0L9byte__valS307 % 16;
      int32_t _M0L6_2atmpS1325 = _M0L12digit__startS311 + _M0L6offsetS305;
      int32_t _M0L6_2atmpS1323 = _M0L6_2atmpS1325 - 2;
      int32_t _M0L6_2atmpS1324 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L2hiS308];
      int32_t _M0L6_2atmpS1328;
      int32_t _M0L6_2atmpS1326;
      int32_t _M0L6_2atmpS1327;
      int32_t _M0L6_2atmpS1329;
      uint64_t _M0L6_2atmpS1330;
      _M0L6bufferS310[_M0L6_2atmpS1323] = _M0L6_2atmpS1324;
      _M0L6_2atmpS1328 = _M0L12digit__startS311 + _M0L6offsetS305;
      _M0L6_2atmpS1326 = _M0L6_2atmpS1328 - 1;
      _M0L6_2atmpS1327
      = ((moonbit_string_t)moonbit_string_literal_20.data)[
        _M0L2loS309
      ];
      _M0L6bufferS310[_M0L6_2atmpS1326] = _M0L6_2atmpS1327;
      _M0L6_2atmpS1329 = _M0L6offsetS305 - 2;
      _M0L6_2atmpS1330 = _M0L1nS306 >> 8;
      _M0L6offsetS305 = _M0L6_2atmpS1329;
      _M0L1nS306 = _M0L6_2atmpS1330;
      continue;
    } else if (_M0L6offsetS305 == 1) {
      uint64_t _M0L6_2atmpS1333 = _M0L1nS306 & 15ull;
      int32_t _M0L6nibbleS313 = (int32_t)_M0L6_2atmpS1333;
      int32_t _M0L6_2atmpS1332 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L6nibbleS313];
      _M0L6bufferS310[_M0L12digit__startS311] = _M0L6_2atmpS1332;
    }
    break;
  }
  return 0;
}

int32_t _M0FPB14radix__count64(
  uint64_t _M0L5valueS299,
  int32_t _M0L5radixS301
) {
  uint64_t _M0L4baseS300;
  uint64_t _M0L3numS302;
  int32_t _M0L5countS303;
  #line 419 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS299 == 0ull) {
    return 1;
  }
  #line 424 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS300 = _M0MPC13int3Int10to__uint64(_M0L5radixS301);
  _M0L3numS302 = _M0L5valueS299;
  _M0L5countS303 = 0;
  while (1) {
    if (_M0L3numS302 > 0ull) {
      uint64_t _M0L6_2atmpS1321 = _M0L3numS302 / _M0L4baseS300;
      int32_t _M0L6_2atmpS1322 = _M0L5countS303 + 1;
      _M0L3numS302 = _M0L6_2atmpS1321;
      _M0L5countS303 = _M0L6_2atmpS1322;
      continue;
    } else {
      return _M0L5countS303;
    }
    break;
  }
}

int32_t _M0FPB12hex__count64(uint64_t _M0L5valueS297) {
  #line 407 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS297 == 0ull) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS298;
    int32_t _M0L6_2atmpS1320;
    int32_t _M0L6_2atmpS1319;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS298 = moonbit_clz64(_M0L5valueS297);
    _M0L6_2atmpS1320 = 63 - _M0L14leading__zerosS298;
    _M0L6_2atmpS1319 = _M0L6_2atmpS1320 / 4;
    return _M0L6_2atmpS1319 + 1;
  }
}

int32_t _M0FPB12dec__count64(uint64_t _M0L5valueS296) {
  #line 343 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS296 >= 10000000000ull) {
    if (_M0L5valueS296 >= 100000000000000ull) {
      if (_M0L5valueS296 >= 10000000000000000ull) {
        if (_M0L5valueS296 >= 1000000000000000000ull) {
          if (_M0L5valueS296 >= 10000000000000000000ull) {
            return 20;
          } else {
            return 19;
          }
        } else if (_M0L5valueS296 >= 100000000000000000ull) {
          return 18;
        } else {
          return 17;
        }
      } else if (_M0L5valueS296 >= 1000000000000000ull) {
        return 16;
      } else {
        return 15;
      }
    } else if (_M0L5valueS296 >= 1000000000000ull) {
      if (_M0L5valueS296 >= 10000000000000ull) {
        return 14;
      } else {
        return 13;
      }
    } else if (_M0L5valueS296 >= 100000000000ull) {
      return 12;
    } else {
      return 11;
    }
  } else if (_M0L5valueS296 >= 100000ull) {
    if (_M0L5valueS296 >= 10000000ull) {
      if (_M0L5valueS296 >= 1000000000ull) {
        return 10;
      } else if (_M0L5valueS296 >= 100000000ull) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS296 >= 1000000ull) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS296 >= 1000ull) {
    if (_M0L5valueS296 >= 10000ull) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS296 >= 100ull) {
    return 3;
  } else if (_M0L5valueS296 >= 10ull) {
    return 2;
  } else {
    return 1;
  }
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS280,
  int32_t _M0L5radixS279
) {
  int32_t _M0L12is__negativeS281;
  uint32_t _M0L3numS282;
  uint16_t* _M0L6bufferS283;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS279 < 2 || _M0L5radixS279 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_19.data);
  }
  if (_M0L4selfS280 == 0) {
    return (moonbit_string_t)moonbit_string_literal_10.data;
  }
  _M0L12is__negativeS281 = _M0L4selfS280 < 0;
  if (_M0L12is__negativeS281) {
    int32_t _M0L6_2atmpS1318 = -_M0L4selfS280;
    _M0L3numS282 = *(uint32_t*)&_M0L6_2atmpS1318;
  } else {
    _M0L3numS282 = *(uint32_t*)&_M0L4selfS280;
  }
  switch (_M0L5radixS279) {
    case 10: {
      int32_t _M0L10digit__lenS284;
      int32_t _M0L6_2atmpS1315;
      int32_t _M0L10total__lenS285;
      uint16_t* _M0L6bufferS286;
      int32_t _M0L12digit__startS287;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS284 = _M0FPB12dec__count32(_M0L3numS282);
      if (_M0L12is__negativeS281) {
        _M0L6_2atmpS1315 = 1;
      } else {
        _M0L6_2atmpS1315 = 0;
      }
      _M0L10total__lenS285 = _M0L10digit__lenS284 + _M0L6_2atmpS1315;
      _M0L6bufferS286
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS285, 0);
      if (_M0L12is__negativeS281) {
        _M0L12digit__startS287 = 1;
      } else {
        _M0L12digit__startS287 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS286, _M0L3numS282, _M0L12digit__startS287, _M0L10total__lenS285);
      _M0L6bufferS283 = _M0L6bufferS286;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS288;
      int32_t _M0L6_2atmpS1316;
      int32_t _M0L10total__lenS289;
      uint16_t* _M0L6bufferS290;
      int32_t _M0L12digit__startS291;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS288 = _M0FPB12hex__count32(_M0L3numS282);
      if (_M0L12is__negativeS281) {
        _M0L6_2atmpS1316 = 1;
      } else {
        _M0L6_2atmpS1316 = 0;
      }
      _M0L10total__lenS289 = _M0L10digit__lenS288 + _M0L6_2atmpS1316;
      _M0L6bufferS290
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS289, 0);
      if (_M0L12is__negativeS281) {
        _M0L12digit__startS291 = 1;
      } else {
        _M0L12digit__startS291 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS290, _M0L3numS282, _M0L12digit__startS291, _M0L10total__lenS289);
      _M0L6bufferS283 = _M0L6bufferS290;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS292;
      int32_t _M0L6_2atmpS1317;
      int32_t _M0L10total__lenS293;
      uint16_t* _M0L6bufferS294;
      int32_t _M0L12digit__startS295;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS292
      = _M0FPB14radix__count32(_M0L3numS282, _M0L5radixS279);
      if (_M0L12is__negativeS281) {
        _M0L6_2atmpS1317 = 1;
      } else {
        _M0L6_2atmpS1317 = 0;
      }
      _M0L10total__lenS293 = _M0L10digit__lenS292 + _M0L6_2atmpS1317;
      _M0L6bufferS294
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS293, 0);
      if (_M0L12is__negativeS281) {
        _M0L12digit__startS295 = 1;
      } else {
        _M0L12digit__startS295 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS294, _M0L3numS282, _M0L12digit__startS295, _M0L10total__lenS293, _M0L5radixS279);
      _M0L6bufferS283 = _M0L6bufferS294;
      break;
    }
  }
  if (_M0L12is__negativeS281) {
    _M0L6bufferS283[0] = 45;
  }
  return _M0L6bufferS283;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS273,
  int32_t _M0L5radixS275
) {
  uint32_t _M0L4baseS274;
  uint32_t _M0L3numS276;
  int32_t _M0L5countS277;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS273 == 0u) {
    return 1;
  }
  _M0L4baseS274 = *(uint32_t*)&_M0L5radixS275;
  _M0L3numS276 = _M0L5valueS273;
  _M0L5countS277 = 0;
  while (1) {
    if (_M0L3numS276 > 0u) {
      uint32_t _M0L6_2atmpS1313 = _M0L3numS276 / _M0L4baseS274;
      int32_t _M0L6_2atmpS1314 = _M0L5countS277 + 1;
      _M0L3numS276 = _M0L6_2atmpS1313;
      _M0L5countS277 = _M0L6_2atmpS1314;
      continue;
    } else {
      return _M0L5countS277;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS271) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS271 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS272;
    int32_t _M0L6_2atmpS1312;
    int32_t _M0L6_2atmpS1311;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS272 = moonbit_clz32(_M0L5valueS271);
    _M0L6_2atmpS1312 = 31 - _M0L14leading__zerosS272;
    _M0L6_2atmpS1311 = _M0L6_2atmpS1312 / 4;
    return _M0L6_2atmpS1311 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS270) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS270 >= 100000u) {
    if (_M0L5valueS270 >= 10000000u) {
      if (_M0L5valueS270 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS270 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS270 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS270 >= 1000u) {
    if (_M0L5valueS270 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS270 >= 100u) {
    return 3;
  } else if (_M0L5valueS270 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS256,
  uint32_t _M0L3numS268,
  int32_t _M0L12digit__startS257,
  int32_t _M0L10total__lenS269
) {
  int32_t _M0L6_2atmpS1310;
  uint32_t _M0L3numS246;
  int32_t _M0L6offsetS247;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1310 = _M0L10total__lenS269 - _M0L12digit__startS257;
  _M0L3numS246 = _M0L3numS268;
  _M0L6offsetS247 = _M0L6_2atmpS1310;
  while (1) {
    if (_M0L3numS246 >= 10000u) {
      uint32_t _M0L1tS248 = _M0L3numS246 / 10000u;
      uint32_t _M0L6_2atmpS1287 = _M0L3numS246 % 10000u;
      int32_t _M0L1rS249 = *(int32_t*)&_M0L6_2atmpS1287;
      int32_t _M0L2d1S250 = _M0L1rS249 / 100;
      int32_t _M0L2d2S251 = _M0L1rS249 % 100;
      int32_t _M0L6_2atmpS1286 = _M0L2d1S250 / 10;
      int32_t _M0L6_2atmpS1285 = 48 + _M0L6_2atmpS1286;
      int32_t _M0L6d1__hiS252 = (uint16_t)_M0L6_2atmpS1285;
      int32_t _M0L6_2atmpS1284 = _M0L2d1S250 % 10;
      int32_t _M0L6_2atmpS1283 = 48 + _M0L6_2atmpS1284;
      int32_t _M0L6d1__loS253 = (uint16_t)_M0L6_2atmpS1283;
      int32_t _M0L6_2atmpS1282 = _M0L2d2S251 / 10;
      int32_t _M0L6_2atmpS1281 = 48 + _M0L6_2atmpS1282;
      int32_t _M0L6d2__hiS254 = (uint16_t)_M0L6_2atmpS1281;
      int32_t _M0L6_2atmpS1280 = _M0L2d2S251 % 10;
      int32_t _M0L6_2atmpS1279 = 48 + _M0L6_2atmpS1280;
      int32_t _M0L6d2__loS255 = (uint16_t)_M0L6_2atmpS1279;
      int32_t _M0L6_2atmpS1271 = _M0L12digit__startS257 + _M0L6offsetS247;
      int32_t _M0L6_2atmpS1270 = _M0L6_2atmpS1271 - 4;
      int32_t _M0L6_2atmpS1273;
      int32_t _M0L6_2atmpS1272;
      int32_t _M0L6_2atmpS1275;
      int32_t _M0L6_2atmpS1274;
      int32_t _M0L6_2atmpS1277;
      int32_t _M0L6_2atmpS1276;
      int32_t _M0L6_2atmpS1278;
      _M0L6bufferS256[_M0L6_2atmpS1270] = _M0L6d1__hiS252;
      _M0L6_2atmpS1273 = _M0L12digit__startS257 + _M0L6offsetS247;
      _M0L6_2atmpS1272 = _M0L6_2atmpS1273 - 3;
      _M0L6bufferS256[_M0L6_2atmpS1272] = _M0L6d1__loS253;
      _M0L6_2atmpS1275 = _M0L12digit__startS257 + _M0L6offsetS247;
      _M0L6_2atmpS1274 = _M0L6_2atmpS1275 - 2;
      _M0L6bufferS256[_M0L6_2atmpS1274] = _M0L6d2__hiS254;
      _M0L6_2atmpS1277 = _M0L12digit__startS257 + _M0L6offsetS247;
      _M0L6_2atmpS1276 = _M0L6_2atmpS1277 - 1;
      _M0L6bufferS256[_M0L6_2atmpS1276] = _M0L6d2__loS255;
      _M0L6_2atmpS1278 = _M0L6offsetS247 - 4;
      _M0L3numS246 = _M0L1tS248;
      _M0L6offsetS247 = _M0L6_2atmpS1278;
      continue;
    } else {
      int32_t _M0L6_2atmpS1309 = *(int32_t*)&_M0L3numS246;
      int32_t _M0L9remainingS259 = _M0L6_2atmpS1309;
      int32_t _M0L6offsetS260 = _M0L6offsetS247;
      while (1) {
        if (_M0L9remainingS259 >= 100) {
          int32_t _M0L1tS261 = _M0L9remainingS259 / 100;
          int32_t _M0L1dS262 = _M0L9remainingS259 % 100;
          int32_t _M0L6_2atmpS1296 = _M0L1dS262 / 10;
          int32_t _M0L6_2atmpS1295 = 48 + _M0L6_2atmpS1296;
          int32_t _M0L5d__hiS263 = (uint16_t)_M0L6_2atmpS1295;
          int32_t _M0L6_2atmpS1294 = _M0L1dS262 % 10;
          int32_t _M0L6_2atmpS1293 = 48 + _M0L6_2atmpS1294;
          int32_t _M0L5d__loS264 = (uint16_t)_M0L6_2atmpS1293;
          int32_t _M0L6_2atmpS1289 = _M0L12digit__startS257 + _M0L6offsetS260;
          int32_t _M0L6_2atmpS1288 = _M0L6_2atmpS1289 - 2;
          int32_t _M0L6_2atmpS1291;
          int32_t _M0L6_2atmpS1290;
          int32_t _M0L6_2atmpS1292;
          _M0L6bufferS256[_M0L6_2atmpS1288] = _M0L5d__hiS263;
          _M0L6_2atmpS1291 = _M0L12digit__startS257 + _M0L6offsetS260;
          _M0L6_2atmpS1290 = _M0L6_2atmpS1291 - 1;
          _M0L6bufferS256[_M0L6_2atmpS1290] = _M0L5d__loS264;
          _M0L6_2atmpS1292 = _M0L6offsetS260 - 2;
          _M0L9remainingS259 = _M0L1tS261;
          _M0L6offsetS260 = _M0L6_2atmpS1292;
          continue;
        } else if (_M0L9remainingS259 >= 10) {
          int32_t _M0L6_2atmpS1304 = _M0L9remainingS259 / 10;
          int32_t _M0L6_2atmpS1303 = 48 + _M0L6_2atmpS1304;
          int32_t _M0L5d__hiS266 = (uint16_t)_M0L6_2atmpS1303;
          int32_t _M0L6_2atmpS1302 = _M0L9remainingS259 % 10;
          int32_t _M0L6_2atmpS1301 = 48 + _M0L6_2atmpS1302;
          int32_t _M0L5d__loS267 = (uint16_t)_M0L6_2atmpS1301;
          int32_t _M0L6_2atmpS1298 = _M0L12digit__startS257 + _M0L6offsetS260;
          int32_t _M0L6_2atmpS1297 = _M0L6_2atmpS1298 - 2;
          int32_t _M0L6_2atmpS1300;
          int32_t _M0L6_2atmpS1299;
          _M0L6bufferS256[_M0L6_2atmpS1297] = _M0L5d__hiS266;
          _M0L6_2atmpS1300 = _M0L12digit__startS257 + _M0L6offsetS260;
          _M0L6_2atmpS1299 = _M0L6_2atmpS1300 - 1;
          _M0L6bufferS256[_M0L6_2atmpS1299] = _M0L5d__loS267;
        } else {
          int32_t _M0L6_2atmpS1308 = _M0L12digit__startS257 + _M0L6offsetS260;
          int32_t _M0L6_2atmpS1305 = _M0L6_2atmpS1308 - 1;
          int32_t _M0L6_2atmpS1307 = 48 + _M0L9remainingS259;
          int32_t _M0L6_2atmpS1306 = (uint16_t)_M0L6_2atmpS1307;
          _M0L6bufferS256[_M0L6_2atmpS1305] = _M0L6_2atmpS1306;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS236,
  uint32_t _M0L3numS240,
  int32_t _M0L12digit__startS237,
  int32_t _M0L10total__lenS239,
  int32_t _M0L5radixS230
) {
  uint32_t _M0L4baseS229;
  int32_t _M0L6_2atmpS1255;
  int32_t _M0L6_2atmpS1254;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS229 = *(uint32_t*)&_M0L5radixS230;
  _M0L6_2atmpS1255 = _M0L5radixS230 - 1;
  _M0L6_2atmpS1254 = _M0L5radixS230 & _M0L6_2atmpS1255;
  if (_M0L6_2atmpS1254 == 0) {
    int32_t _M0L5shiftS231;
    uint32_t _M0L4maskS232;
    int32_t _M0L6_2atmpS1262;
    int32_t _M0L6offsetS233;
    uint32_t _M0L1nS234;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS231 = moonbit_ctz32(_M0L5radixS230);
    _M0L4maskS232 = _M0L4baseS229 - 1u;
    _M0L6_2atmpS1262 = _M0L10total__lenS239 - _M0L12digit__startS237;
    _M0L6offsetS233 = _M0L6_2atmpS1262;
    _M0L1nS234 = _M0L3numS240;
    while (1) {
      if (_M0L1nS234 > 0u) {
        uint32_t _M0L6_2atmpS1261 = _M0L1nS234 & _M0L4maskS232;
        int32_t _M0L5digitS235 = *(int32_t*)&_M0L6_2atmpS1261;
        int32_t _M0L6_2atmpS1258 = _M0L12digit__startS237 + _M0L6offsetS233;
        int32_t _M0L6_2atmpS1256 = _M0L6_2atmpS1258 - 1;
        int32_t _M0L6_2atmpS1257 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS235];
        int32_t _M0L6_2atmpS1259;
        uint32_t _M0L6_2atmpS1260;
        _M0L6bufferS236[_M0L6_2atmpS1256] = _M0L6_2atmpS1257;
        _M0L6_2atmpS1259 = _M0L6offsetS233 - 1;
        _M0L6_2atmpS1260 = _M0L1nS234 >> (_M0L5shiftS231 & 31);
        _M0L6offsetS233 = _M0L6_2atmpS1259;
        _M0L1nS234 = _M0L6_2atmpS1260;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1269 = _M0L10total__lenS239 - _M0L12digit__startS237;
    int32_t _M0L6offsetS241 = _M0L6_2atmpS1269;
    uint32_t _M0L1nS242 = _M0L3numS240;
    while (1) {
      if (_M0L1nS242 > 0u) {
        uint32_t _M0L1qS243 = _M0L1nS242 / _M0L4baseS229;
        uint32_t _M0L6_2atmpS1268 = _M0L1qS243 * _M0L4baseS229;
        uint32_t _M0L6_2atmpS1267 = _M0L1nS242 - _M0L6_2atmpS1268;
        int32_t _M0L5digitS244 = *(int32_t*)&_M0L6_2atmpS1267;
        int32_t _M0L6_2atmpS1265 = _M0L12digit__startS237 + _M0L6offsetS241;
        int32_t _M0L6_2atmpS1263 = _M0L6_2atmpS1265 - 1;
        int32_t _M0L6_2atmpS1264 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS244];
        int32_t _M0L6_2atmpS1266;
        _M0L6bufferS236[_M0L6_2atmpS1263] = _M0L6_2atmpS1264;
        _M0L6_2atmpS1266 = _M0L6offsetS241 - 1;
        _M0L6offsetS241 = _M0L6_2atmpS1266;
        _M0L1nS242 = _M0L1qS243;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS223,
  uint32_t _M0L3numS228,
  int32_t _M0L12digit__startS224,
  int32_t _M0L10total__lenS227
) {
  int32_t _M0L6_2atmpS1253;
  int32_t _M0L6offsetS218;
  uint32_t _M0L1nS219;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1253 = _M0L10total__lenS227 - _M0L12digit__startS224;
  _M0L6offsetS218 = _M0L6_2atmpS1253;
  _M0L1nS219 = _M0L3numS228;
  while (1) {
    if (_M0L6offsetS218 >= 2) {
      uint32_t _M0L6_2atmpS1250 = _M0L1nS219 & 255u;
      int32_t _M0L9byte__valS220 = *(int32_t*)&_M0L6_2atmpS1250;
      int32_t _M0L2hiS221 = _M0L9byte__valS220 / 16;
      int32_t _M0L2loS222 = _M0L9byte__valS220 % 16;
      int32_t _M0L6_2atmpS1244 = _M0L12digit__startS224 + _M0L6offsetS218;
      int32_t _M0L6_2atmpS1242 = _M0L6_2atmpS1244 - 2;
      int32_t _M0L6_2atmpS1243 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L2hiS221];
      int32_t _M0L6_2atmpS1247;
      int32_t _M0L6_2atmpS1245;
      int32_t _M0L6_2atmpS1246;
      int32_t _M0L6_2atmpS1248;
      uint32_t _M0L6_2atmpS1249;
      _M0L6bufferS223[_M0L6_2atmpS1242] = _M0L6_2atmpS1243;
      _M0L6_2atmpS1247 = _M0L12digit__startS224 + _M0L6offsetS218;
      _M0L6_2atmpS1245 = _M0L6_2atmpS1247 - 1;
      _M0L6_2atmpS1246
      = ((moonbit_string_t)moonbit_string_literal_20.data)[
        _M0L2loS222
      ];
      _M0L6bufferS223[_M0L6_2atmpS1245] = _M0L6_2atmpS1246;
      _M0L6_2atmpS1248 = _M0L6offsetS218 - 2;
      _M0L6_2atmpS1249 = _M0L1nS219 >> 8;
      _M0L6offsetS218 = _M0L6_2atmpS1248;
      _M0L1nS219 = _M0L6_2atmpS1249;
      continue;
    } else if (_M0L6offsetS218 == 1) {
      uint32_t _M0L6_2atmpS1252 = _M0L1nS219 & 15u;
      int32_t _M0L6nibbleS226 = *(int32_t*)&_M0L6_2atmpS1252;
      int32_t _M0L6_2atmpS1251 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L6nibbleS226];
      _M0L6bufferS223[_M0L12digit__startS224] = _M0L6_2atmpS1251;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS217
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS216;
  struct _M0TPB6Logger _M0L6_2atmpS1241;
  moonbit_string_t _result_2229;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS216 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS216);
  _M0L6_2atmpS1241
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS216
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS217, _M0L6_2atmpS1241);
  if (_M0L6_2atmpS1241.$1) {
    moonbit_decref(_M0L6_2atmpS1241.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2229 = _M0MPB13StringBuilder10to__string(_M0L6loggerS216);
  moonbit_decref_cycle_free(_M0L6loggerS216);
  return _result_2229;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS211,
  struct _M0TPB6Logger _M0L6loggerS210
) {
  moonbit_string_t _M0L6_2atmpS1238;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1238 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS211);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS210.$0->$method_0(_M0L6loggerS210.$1, _M0L6_2atmpS1238);
  moonbit_decref_cycle_free(_M0L6_2atmpS1238);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS213,
  struct _M0TPB6Logger _M0L6loggerS212
) {
  moonbit_string_t _M0L6_2atmpS1239;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1239 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS213);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS212.$0->$method_0(_M0L6loggerS212.$1, _M0L6_2atmpS1239);
  moonbit_decref_cycle_free(_M0L6_2atmpS1239);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS215,
  struct _M0TPB6Logger _M0L6loggerS214
) {
  moonbit_string_t _M0L6_2atmpS1240;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1240 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS215);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS214.$0->$method_0(_M0L6loggerS214.$1, _M0L6_2atmpS1240);
  moonbit_decref_cycle_free(_M0L6_2atmpS1240);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS209
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS209.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS208
) {
  moonbit_string_t _M0L8_2afieldS2120;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2120 = _M0L4selfS208.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2120);
  return _M0L8_2afieldS2120;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS204,
  moonbit_string_t _M0L5valueS205,
  int32_t _M0L5startS206,
  int32_t _M0L3lenS207
) {
  int32_t _M0L6_2atmpS1237;
  int64_t _M0L6_2atmpS1236;
  struct _M0TPC16string10StringView _M0L6_2atmpS1235;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1237 = _M0L5startS206 + _M0L3lenS207;
  _M0L6_2atmpS1236 = (int64_t)_M0L6_2atmpS1237;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1235
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS205, _M0L5startS206, _M0L6_2atmpS1236);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS204, _M0L6_2atmpS1235);
  moonbit_decref_cycle_free(_M0L6_2atmpS1235.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String21clamped__view_2einner(
  moonbit_string_t _M0L4selfS197,
  int32_t _M0L5startS199,
  int64_t _M0L3endS201
) {
  int32_t _M0L3lenS196;
  int32_t _M0Lm2loS198;
  int32_t _M0Lm2hiS200;
  int32_t _M0L6_2atmpS1219;
  int32_t _if__result_2230;
  int32_t _M0L6_2atmpS1227;
  int32_t _if__result_2231;
  int32_t _M0L6_2atmpS1229;
  int32_t _M0L6_2atmpS1230;
  #line 698 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS196 = Moonbit_array_length(_M0L4selfS197);
  if (_M0L5startS199 < 0) {
    _M0Lm2loS198 = 0;
  } else if (_M0L5startS199 > _M0L3lenS196) {
    _M0Lm2loS198 = _M0L3lenS196;
  } else {
    _M0Lm2loS198 = _M0L5startS199;
  }
  if (_M0L3endS201 == 4294967296ll) {
    _M0Lm2hiS200 = _M0L3lenS196;
  } else {
    int64_t _M0L7_2aSomeS202 = _M0L3endS201;
    int32_t _M0L4_2aeS203 = (int32_t)_M0L7_2aSomeS202;
    if (_M0L4_2aeS203 < 0) {
      _M0Lm2hiS200 = 0;
    } else if (_M0L4_2aeS203 > _M0L3lenS196) {
      _M0Lm2hiS200 = _M0L3lenS196;
    } else {
      _M0Lm2hiS200 = _M0L4_2aeS203;
    }
  }
  _M0L6_2atmpS1219 = _M0Lm2loS198;
  if (_M0L6_2atmpS1219 > 0) {
    int32_t _M0L6_2atmpS1218 = _M0Lm2loS198;
    if (_M0L6_2atmpS1218 < _M0L3lenS196) {
      int32_t _M0L6_2atmpS1217 = _M0Lm2loS198;
      int32_t _M0L6_2atmpS1216 = _M0L4selfS197[_M0L6_2atmpS1217];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1216)) {
        int32_t _M0L6_2atmpS1215 = _M0Lm2loS198;
        int32_t _M0L6_2atmpS1214 = _M0L6_2atmpS1215 - 1;
        int32_t _M0L6_2atmpS1213 = _M0L4selfS197[_M0L6_2atmpS1214];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2230
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1213);
      } else {
        _if__result_2230 = 0;
      }
    } else {
      _if__result_2230 = 0;
    }
  } else {
    _if__result_2230 = 0;
  }
  if (_if__result_2230) {
    int32_t _M0L6_2atmpS1220 = _M0Lm2loS198;
    _M0Lm2loS198 = _M0L6_2atmpS1220 + 1;
  }
  _M0L6_2atmpS1227 = _M0Lm2hiS200;
  if (_M0L6_2atmpS1227 > 0) {
    int32_t _M0L6_2atmpS1226 = _M0Lm2hiS200;
    if (_M0L6_2atmpS1226 < _M0L3lenS196) {
      int32_t _M0L6_2atmpS1225 = _M0Lm2hiS200;
      int32_t _M0L6_2atmpS1224 = _M0L4selfS197[_M0L6_2atmpS1225];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1224)) {
        int32_t _M0L6_2atmpS1223 = _M0Lm2hiS200;
        int32_t _M0L6_2atmpS1222 = _M0L6_2atmpS1223 - 1;
        int32_t _M0L6_2atmpS1221 = _M0L4selfS197[_M0L6_2atmpS1222];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2231
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1221);
      } else {
        _if__result_2231 = 0;
      }
    } else {
      _if__result_2231 = 0;
    }
  } else {
    _if__result_2231 = 0;
  }
  if (_if__result_2231) {
    int32_t _M0L6_2atmpS1228 = _M0Lm2hiS200;
    _M0Lm2hiS200 = _M0L6_2atmpS1228 - 1;
  }
  _M0L6_2atmpS1229 = _M0Lm2loS198;
  _M0L6_2atmpS1230 = _M0Lm2hiS200;
  if (_M0L6_2atmpS1229 >= _M0L6_2atmpS1230) {
    int32_t _M0L6_2atmpS1231 = _M0Lm2loS198;
    int32_t _M0L6_2atmpS1232 = _M0Lm2loS198;
    moonbit_incref_cycle_free(_M0L4selfS197);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS197,
                                                 .$1 = _M0L6_2atmpS1231,
                                                 .$2 = _M0L6_2atmpS1232};
  } else {
    int32_t _M0L6_2atmpS1233 = _M0Lm2loS198;
    int32_t _M0L6_2atmpS1234 = _M0Lm2hiS200;
    moonbit_incref_cycle_free(_M0L4selfS197);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS197,
                                                 .$1 = _M0L6_2atmpS1233,
                                                 .$2 = _M0L6_2atmpS1234};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS195,
  struct _M0TPB4Show _M0L4showS194
) {
  struct _M0TPB6Logger _M0L6_2atmpS1212;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS195);
  _M0L6_2atmpS1212
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS195
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS194.$0->$method_0(_M0L4showS194.$1, _M0L6_2atmpS1212);
  if (_M0L6_2atmpS1212.$1) {
    moonbit_decref(_M0L6_2atmpS1212.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS193,
  struct _M0TPB4Show _M0L4showS192
) {
  struct _M0TPB6Logger _M0L6_2atmpS1211;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS193);
  _M0L6_2atmpS1211
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS193
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS192.$0->$method_0(_M0L4showS192.$1, _M0L6_2atmpS1211);
  if (_M0L6_2atmpS1211.$1) {
    moonbit_decref(_M0L6_2atmpS1211.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS191) {
  int64_t _M0L6_2atmpS1210;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1210 = (int64_t)_M0L4selfS191;
  return *(uint64_t*)&_M0L6_2atmpS1210;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

moonbit_string_t _M0MPC16string6String14escape_2einner(
  moonbit_string_t _M0L4selfS189,
  int32_t _M0L5quoteS190
) {
  struct _M0TPB13StringBuilder* _M0L3bufS188;
  int32_t _M0L6_2atmpS1209;
  struct _M0TPC16string10StringView _M0L6_2atmpS1207;
  struct _M0TPB6Logger _M0L6_2atmpS1208;
  moonbit_string_t _result_2232;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS188 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1209 = Moonbit_array_length(_M0L4selfS189);
  moonbit_incref_cycle_free(_M0L4selfS189);
  _M0L6_2atmpS1207
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS189, .$1 = 0, .$2 = _M0L6_2atmpS1209
  };
  moonbit_incref_cycle_free(_M0L3bufS188);
  _M0L6_2atmpS1208
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS188
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1207, _M0L6_2atmpS1208, _M0L5quoteS190);
  moonbit_decref_cycle_free(_M0L6_2atmpS1207.$0);
  if (_M0L6_2atmpS1208.$1) {
    moonbit_decref(_M0L6_2atmpS1208.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2232 = _M0MPB13StringBuilder10to__string(_M0L3bufS188);
  moonbit_decref_cycle_free(_M0L3bufS188);
  return _result_2232;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS180,
  struct _M0TPB6Logger _M0L6loggerS178,
  int32_t _M0L5quoteS177
) {
  int32_t _M0L3endS1205;
  int32_t _M0L5startS1206;
  int32_t _M0L3lenS179;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS181;
  int32_t _M0L1iS182;
  int32_t _M0L3segS183;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS177) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS178.$0->$method_3(_M0L6loggerS178.$1, 34);
  }
  _M0L3endS1205 = _M0L4selfS180.$2;
  _M0L5startS1206 = _M0L4selfS180.$1;
  _M0L3lenS179 = _M0L3endS1205 - _M0L5startS1206;
  moonbit_incref_cycle_free(_M0L4selfS180.$0);
  if (_M0L6loggerS178.$1) {
    moonbit_incref(_M0L6loggerS178.$1);
  }
  _M0L6_2aenvS181
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS181)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 63, 0);
  _M0L6_2aenvS181->$0 = _M0L4selfS180;
  _M0L6_2aenvS181->$1 = _M0L6loggerS178;
  _M0L1iS182 = 0;
  _M0L3segS183 = 0;
  _2afor_184:;
  while (1) {
    moonbit_string_t _M0L3strS1202;
    int32_t _M0L5startS1204;
    int32_t _M0L6_2atmpS1203;
    int32_t _M0L4codeS185;
    int32_t _M0L1cS187;
    int32_t _M0L6_2atmpS1186;
    int32_t _M0L6_2atmpS1187;
    int32_t _M0L6_2atmpS1188;
    if (_M0L1iS182 >= _M0L3lenS179) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS181, _M0L3segS183, _M0L1iS182);
      moonbit_decref_cycle_free(_M0L6_2aenvS181);
      break;
    }
    _M0L3strS1202 = _M0L4selfS180.$0;
    _M0L5startS1204 = _M0L4selfS180.$1;
    _M0L6_2atmpS1203 = _M0L5startS1204 + _M0L1iS182;
    _M0L4codeS185 = _M0L3strS1202[_M0L6_2atmpS1203];
    switch (_M0L4codeS185) {
      case 34: {
        _M0L1cS187 = _M0L4codeS185;
        goto join_186;
        break;
      }
      
      case 92: {
        _M0L1cS187 = _M0L4codeS185;
        goto join_186;
        break;
      }
      
      case 10: {
        int32_t _M0L6_2atmpS1189;
        int32_t _M0L6_2atmpS1190;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS181, _M0L3segS183, _M0L1iS182);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS178.$0->$method_0(_M0L6loggerS178.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS1189 = _M0L1iS182 + 1;
        _M0L6_2atmpS1190 = _M0L1iS182 + 1;
        _M0L1iS182 = _M0L6_2atmpS1189;
        _M0L3segS183 = _M0L6_2atmpS1190;
        goto _2afor_184;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1191;
        int32_t _M0L6_2atmpS1192;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS181, _M0L3segS183, _M0L1iS182);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS178.$0->$method_0(_M0L6loggerS178.$1, (moonbit_string_t)moonbit_string_literal_22.data);
        _M0L6_2atmpS1191 = _M0L1iS182 + 1;
        _M0L6_2atmpS1192 = _M0L1iS182 + 1;
        _M0L1iS182 = _M0L6_2atmpS1191;
        _M0L3segS183 = _M0L6_2atmpS1192;
        goto _2afor_184;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1193;
        int32_t _M0L6_2atmpS1194;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS181, _M0L3segS183, _M0L1iS182);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS178.$0->$method_0(_M0L6loggerS178.$1, (moonbit_string_t)moonbit_string_literal_23.data);
        _M0L6_2atmpS1193 = _M0L1iS182 + 1;
        _M0L6_2atmpS1194 = _M0L1iS182 + 1;
        _M0L1iS182 = _M0L6_2atmpS1193;
        _M0L3segS183 = _M0L6_2atmpS1194;
        goto _2afor_184;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1195;
        int32_t _M0L6_2atmpS1196;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS181, _M0L3segS183, _M0L1iS182);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS178.$0->$method_0(_M0L6loggerS178.$1, (moonbit_string_t)moonbit_string_literal_24.data);
        _M0L6_2atmpS1195 = _M0L1iS182 + 1;
        _M0L6_2atmpS1196 = _M0L1iS182 + 1;
        _M0L1iS182 = _M0L6_2atmpS1195;
        _M0L3segS183 = _M0L6_2atmpS1196;
        goto _2afor_184;
        break;
      }
      default: {
        if (_M0L4codeS185 < 32) {
          int32_t _M0L6_2atmpS1198;
          moonbit_string_t _M0L6_2atmpS1197;
          int32_t _M0L6_2atmpS1199;
          int32_t _M0L6_2atmpS1200;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS181, _M0L3segS183, _M0L1iS182);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS178.$0->$method_0(_M0L6loggerS178.$1, (moonbit_string_t)moonbit_string_literal_25.data);
          _M0L6_2atmpS1198 = _M0L4codeS185 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1197 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1198);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS178.$0->$method_0(_M0L6loggerS178.$1, _M0L6_2atmpS1197);
          moonbit_decref_cycle_free(_M0L6_2atmpS1197);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS178.$0->$method_0(_M0L6loggerS178.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1199 = _M0L1iS182 + 1;
          _M0L6_2atmpS1200 = _M0L1iS182 + 1;
          _M0L1iS182 = _M0L6_2atmpS1199;
          _M0L3segS183 = _M0L6_2atmpS1200;
          goto _2afor_184;
        } else {
          int32_t _M0L6_2atmpS1201 = _M0L1iS182 + 1;
          int32_t _tmp_2235 = _M0L3segS183;
          _M0L1iS182 = _M0L6_2atmpS1201;
          _M0L3segS183 = _tmp_2235;
          goto _2afor_184;
        }
        break;
      }
    }
    goto joinlet_2234;
    join_186:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS181, _M0L3segS183, _M0L1iS182);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS178.$0->$method_3(_M0L6loggerS178.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1186 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS187);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS178.$0->$method_3(_M0L6loggerS178.$1, _M0L6_2atmpS1186);
    _M0L6_2atmpS1187 = _M0L1iS182 + 1;
    _M0L6_2atmpS1188 = _M0L1iS182 + 1;
    _M0L1iS182 = _M0L6_2atmpS1187;
    _M0L3segS183 = _M0L6_2atmpS1188;
    continue;
    joinlet_2234:;
    break;
  }
  if (_M0L5quoteS177) {
    #line 202 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS178.$0->$method_3(_M0L6loggerS178.$1, 34);
  }
  return 0;
}

int32_t _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS173,
  int32_t _M0L3segS176,
  int32_t _M0L1iS175
) {
  struct _M0TPB6Logger _M0L6loggerS172;
  struct _M0TPC16string10StringView _M0L4selfS174;
  #line 153 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6loggerS172 = _M0L6_2aenvS173->$1;
  _M0L4selfS174 = _M0L6_2aenvS173->$0;
  if (_M0L1iS175 > _M0L3segS176) {
    int64_t _M0L6_2atmpS1185 = (int64_t)_M0L1iS175;
    struct _M0TPC16string10StringView _M0L6_2atmpS1184;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1184
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS174, _M0L3segS176, _M0L6_2atmpS1185);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS172.$0->$method_2(_M0L6loggerS172.$1, _M0L6_2atmpS1184);
    moonbit_decref_cycle_free(_M0L6_2atmpS1184.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS163,
  int32_t _M0L5startS165,
  int64_t _M0L3endS167
) {
  int32_t _M0L3endS1182;
  int32_t _M0L5startS1183;
  int32_t _M0L3lenS162;
  int32_t _M0Lm2loS164;
  int32_t _M0Lm2hiS166;
  moonbit_string_t _M0L3strS170;
  int32_t _M0L4baseS171;
  int32_t _M0L6_2atmpS1160;
  int32_t _if__result_2236;
  int32_t _M0L6_2atmpS1170;
  int32_t _if__result_2237;
  int32_t _M0L6_2atmpS1172;
  int32_t _M0L6_2atmpS1173;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1182 = _M0L4selfS163.$2;
  _M0L5startS1183 = _M0L4selfS163.$1;
  _M0L3lenS162 = _M0L3endS1182 - _M0L5startS1183;
  if (_M0L5startS165 < 0) {
    _M0Lm2loS164 = 0;
  } else if (_M0L5startS165 > _M0L3lenS162) {
    _M0Lm2loS164 = _M0L3lenS162;
  } else {
    _M0Lm2loS164 = _M0L5startS165;
  }
  if (_M0L3endS167 == 4294967296ll) {
    _M0Lm2hiS166 = _M0L3lenS162;
  } else {
    int64_t _M0L7_2aSomeS168 = _M0L3endS167;
    int32_t _M0L4_2aeS169 = (int32_t)_M0L7_2aSomeS168;
    if (_M0L4_2aeS169 < 0) {
      _M0Lm2hiS166 = 0;
    } else if (_M0L4_2aeS169 > _M0L3lenS162) {
      _M0Lm2hiS166 = _M0L3lenS162;
    } else {
      _M0Lm2hiS166 = _M0L4_2aeS169;
    }
  }
  _M0L3strS170 = _M0L4selfS163.$0;
  _M0L4baseS171 = _M0L4selfS163.$1;
  _M0L6_2atmpS1160 = _M0Lm2loS164;
  if (_M0L6_2atmpS1160 > 0) {
    int32_t _M0L6_2atmpS1159 = _M0Lm2loS164;
    if (_M0L6_2atmpS1159 < _M0L3lenS162) {
      int32_t _M0L6_2atmpS1158 = _M0Lm2loS164;
      int32_t _M0L6_2atmpS1157 = _M0L4baseS171 + _M0L6_2atmpS1158;
      int32_t _M0L6_2atmpS1156 = _M0L3strS170[_M0L6_2atmpS1157];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1156)) {
        int32_t _M0L6_2atmpS1155 = _M0Lm2loS164;
        int32_t _M0L6_2atmpS1154 = _M0L4baseS171 + _M0L6_2atmpS1155;
        int32_t _M0L6_2atmpS1153 = _M0L6_2atmpS1154 - 1;
        int32_t _M0L6_2atmpS1152 = _M0L3strS170[_M0L6_2atmpS1153];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2236
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1152);
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
    int32_t _M0L6_2atmpS1161 = _M0Lm2loS164;
    _M0Lm2loS164 = _M0L6_2atmpS1161 + 1;
  }
  _M0L6_2atmpS1170 = _M0Lm2hiS166;
  if (_M0L6_2atmpS1170 > 0) {
    int32_t _M0L6_2atmpS1169 = _M0Lm2hiS166;
    if (_M0L6_2atmpS1169 < _M0L3lenS162) {
      int32_t _M0L6_2atmpS1168 = _M0Lm2hiS166;
      int32_t _M0L6_2atmpS1167 = _M0L4baseS171 + _M0L6_2atmpS1168;
      int32_t _M0L6_2atmpS1166 = _M0L3strS170[_M0L6_2atmpS1167];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1166)) {
        int32_t _M0L6_2atmpS1165 = _M0Lm2hiS166;
        int32_t _M0L6_2atmpS1164 = _M0L4baseS171 + _M0L6_2atmpS1165;
        int32_t _M0L6_2atmpS1163 = _M0L6_2atmpS1164 - 1;
        int32_t _M0L6_2atmpS1162 = _M0L3strS170[_M0L6_2atmpS1163];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2237
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1162);
      } else {
        _if__result_2237 = 0;
      }
    } else {
      _if__result_2237 = 0;
    }
  } else {
    _if__result_2237 = 0;
  }
  if (_if__result_2237) {
    int32_t _M0L6_2atmpS1171 = _M0Lm2hiS166;
    _M0Lm2hiS166 = _M0L6_2atmpS1171 - 1;
  }
  _M0L6_2atmpS1172 = _M0Lm2loS164;
  _M0L6_2atmpS1173 = _M0Lm2hiS166;
  if (_M0L6_2atmpS1172 >= _M0L6_2atmpS1173) {
    int32_t _M0L6_2atmpS1177 = _M0Lm2loS164;
    int32_t _M0L6_2atmpS1174 = _M0L4baseS171 + _M0L6_2atmpS1177;
    int32_t _M0L6_2atmpS1176 = _M0Lm2loS164;
    int32_t _M0L6_2atmpS1175 = _M0L4baseS171 + _M0L6_2atmpS1176;
    moonbit_incref_cycle_free(_M0L3strS170);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS170,
                                                 .$1 = _M0L6_2atmpS1174,
                                                 .$2 = _M0L6_2atmpS1175};
  } else {
    int32_t _M0L6_2atmpS1181 = _M0Lm2loS164;
    int32_t _M0L6_2atmpS1178 = _M0L4baseS171 + _M0L6_2atmpS1181;
    int32_t _M0L6_2atmpS1180 = _M0Lm2hiS166;
    int32_t _M0L6_2atmpS1179 = _M0L4baseS171 + _M0L6_2atmpS1180;
    moonbit_incref_cycle_free(_M0L3strS170);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS170,
                                                 .$1 = _M0L6_2atmpS1178,
                                                 .$2 = _M0L6_2atmpS1179};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS161) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS160;
  int32_t _M0L6_2atmpS1149;
  int32_t _M0L6_2atmpS1148;
  int32_t _M0L6_2atmpS1151;
  int32_t _M0L6_2atmpS1150;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1147;
  moonbit_string_t _result_2238;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS160 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1149 = _M0IPC14byte4BytePB3Div3div(_M0L1bS161, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1148
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1149);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS160, _M0L6_2atmpS1148);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1151 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS161, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1150
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1151);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS160, _M0L6_2atmpS1150);
  _M0L6_2atmpS1147 = _M0L7_2aselfS160;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2238 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1147);
  moonbit_decref_cycle_free(_M0L6_2atmpS1147);
  return _result_2238;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS159) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS159 < 10) {
    int32_t _M0L6_2atmpS1144;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1144 = _M0IPC14byte4BytePB3Add3add(_M0L1iS159, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1144);
  } else {
    int32_t _M0L6_2atmpS1146;
    int32_t _M0L6_2atmpS1145;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1146 = _M0IPC14byte4BytePB3Add3add(_M0L1iS159, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1145 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1146, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1145);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS157,
  int32_t _M0L4thatS158
) {
  int32_t _M0L6_2atmpS1142;
  int32_t _M0L6_2atmpS1143;
  int32_t _M0L6_2atmpS1141;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1142 = (int32_t)_M0L4selfS157;
  _M0L6_2atmpS1143 = (int32_t)_M0L4thatS158;
  _M0L6_2atmpS1141 = _M0L6_2atmpS1142 - _M0L6_2atmpS1143;
  return _M0L6_2atmpS1141 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS155,
  int32_t _M0L4thatS156
) {
  int32_t _M0L6_2atmpS1139;
  int32_t _M0L6_2atmpS1140;
  int32_t _M0L6_2atmpS1138;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1139 = (int32_t)_M0L4selfS155;
  _M0L6_2atmpS1140 = (int32_t)_M0L4thatS156;
  _M0L6_2atmpS1138 = _M0L6_2atmpS1139 % _M0L6_2atmpS1140;
  return _M0L6_2atmpS1138 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS153,
  int32_t _M0L4thatS154
) {
  int32_t _M0L6_2atmpS1136;
  int32_t _M0L6_2atmpS1137;
  int32_t _M0L6_2atmpS1135;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1136 = (int32_t)_M0L4selfS153;
  _M0L6_2atmpS1137 = (int32_t)_M0L4thatS154;
  _M0L6_2atmpS1135 = _M0L6_2atmpS1136 / _M0L6_2atmpS1137;
  return _M0L6_2atmpS1135 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS151,
  int32_t _M0L4thatS152
) {
  int32_t _M0L6_2atmpS1133;
  int32_t _M0L6_2atmpS1134;
  int32_t _M0L6_2atmpS1132;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1133 = (int32_t)_M0L4selfS151;
  _M0L6_2atmpS1134 = (int32_t)_M0L4thatS152;
  _M0L6_2atmpS1132 = _M0L6_2atmpS1133 + _M0L6_2atmpS1134;
  return _M0L6_2atmpS1132 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS150) {
  int32_t _M0L6_2atmpS1131;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1131 = (int32_t)_M0L4selfS150;
  return _M0L6_2atmpS1131;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS149) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS149 >= 56320 && _M0L4selfS149 <= 57343;
}

int32_t _M0MPC16uint166UInt1622is__leading__surrogate(int32_t _M0L4selfS148) {
  #line 28 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS148 >= 55296 && _M0L4selfS148 <= 56319;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS147,
  moonbit_string_t _M0L3strS145
) {
  int32_t _M0L8str__lenS144;
  int32_t _M0L3lenS1130;
  int32_t _M0L8requiredS146;
  uint16_t* _M0L4dataS1125;
  int32_t _M0L6_2atmpS1124;
  int32_t _if__result_2239;
  uint16_t* _M0L4dataS1126;
  int32_t _M0L3lenS1127;
  int32_t _M0L3lenS1129;
  int32_t _M0L6_2atmpS1128;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS144 = Moonbit_array_length(_M0L3strS145);
  if (_M0L8str__lenS144 == 0) {
    return 0;
  }
  _M0L3lenS1130 = _M0L4selfS147->$1;
  _M0L8requiredS146 = _M0L3lenS1130 + _M0L8str__lenS144;
  _M0L4dataS1125 = _M0L4selfS147->$0;
  _M0L6_2atmpS1124 = Moonbit_array_length(_M0L4dataS1125);
  if (_M0L8requiredS146 > _M0L6_2atmpS1124) {
    _if__result_2239 = 1;
  } else {
    int32_t _M0L3lenS1123 = _M0L4selfS147->$1;
    _if__result_2239 = _M0L8requiredS146 < _M0L3lenS1123;
  }
  if (_if__result_2239) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS147, _M0L8requiredS146);
  }
  _M0L4dataS1126 = _M0L4selfS147->$0;
  _M0L3lenS1127 = _M0L4selfS147->$1;
  moonbit_incref_cycle_free(_M0L4dataS1126);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1126, _M0L3lenS1127, _M0L3strS145, 0, _M0L8str__lenS144);
  moonbit_decref_cycle_free(_M0L4dataS1126);
  _M0L3lenS1129 = _M0L4selfS147->$1;
  _M0L6_2atmpS1128 = _M0L3lenS1129 + _M0L8str__lenS144;
  _M0L4selfS147->$1 = _M0L6_2atmpS1128;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS140,
  int32_t _M0L11dst__offsetS143,
  moonbit_string_t _M0L3strS141,
  int32_t _M0L11str__offsetS136,
  int32_t _M0L3lenS137
) {
  int32_t _M0L16end__str__offsetS135;
  int32_t _M0L1iS138;
  int32_t _M0L1jS139;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS135 = _M0L11str__offsetS136 + _M0L3lenS137;
  _M0L1iS138 = _M0L11str__offsetS136;
  _M0L1jS139 = _M0L11dst__offsetS143;
  while (1) {
    if (_M0L1iS138 < _M0L16end__str__offsetS135) {
      int32_t _M0L6_2atmpS1120 = _M0L3strS141[_M0L1iS138];
      int32_t _M0L6_2atmpS1121;
      int32_t _M0L6_2atmpS1122;
      _M0L4selfS140[_M0L1jS139] = _M0L6_2atmpS1120;
      _M0L6_2atmpS1121 = _M0L1iS138 + 1;
      _M0L6_2atmpS1122 = _M0L1jS139 + 1;
      _M0L1iS138 = _M0L6_2atmpS1121;
      _M0L1jS139 = _M0L6_2atmpS1122;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS133,
  int32_t _M0L2chS132
) {
  uint32_t _M0L4codeS131;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS131 = _M0MPC14char4Char8to__uint(_M0L2chS132);
  if (_M0L4codeS131 <= 65535u) {
    int32_t _M0L3lenS1091 = _M0L4selfS133->$1;
    uint16_t* _M0L4dataS1093 = _M0L4selfS133->$0;
    int32_t _M0L6_2atmpS1092 = Moonbit_array_length(_M0L4dataS1093);
    uint16_t* _M0L4dataS1096;
    int32_t _M0L3lenS1097;
    int32_t _M0L6_2atmpS1098;
    int32_t _M0L3lenS1100;
    int32_t _M0L6_2atmpS1099;
    if (_M0L3lenS1091 >= _M0L6_2atmpS1092) {
      int32_t _M0L3lenS1095 = _M0L4selfS133->$1;
      int32_t _M0L6_2atmpS1094 = _M0L3lenS1095 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS133, _M0L6_2atmpS1094);
    }
    _M0L4dataS1096 = _M0L4selfS133->$0;
    _M0L3lenS1097 = _M0L4selfS133->$1;
    moonbit_incref_cycle_free(_M0L4dataS1096);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1098 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS131);
    if (
      _M0L3lenS1097 < 0
      || _M0L3lenS1097 >= Moonbit_array_length(_M0L4dataS1096)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1096[_M0L3lenS1097] = _M0L6_2atmpS1098;
    moonbit_decref_cycle_free(_M0L4dataS1096);
    _M0L3lenS1100 = _M0L4selfS133->$1;
    _M0L6_2atmpS1099 = _M0L3lenS1100 + 1;
    _M0L4selfS133->$1 = _M0L6_2atmpS1099;
  } else if (_M0L4codeS131 <= 1114111u) {
    uint16_t* _M0L4dataS1104 = _M0L4selfS133->$0;
    int32_t _M0L6_2atmpS1102 = Moonbit_array_length(_M0L4dataS1104);
    int32_t _M0L3lenS1103 = _M0L4selfS133->$1;
    int32_t _M0L6_2atmpS1101 = _M0L6_2atmpS1102 - _M0L3lenS1103;
    uint32_t _M0L4codeS134;
    uint16_t* _M0L4dataS1107;
    int32_t _M0L3lenS1108;
    uint32_t _M0L6_2atmpS1111;
    uint32_t _M0L6_2atmpS1110;
    int32_t _M0L6_2atmpS1109;
    uint16_t* _M0L4dataS1112;
    int32_t _M0L3lenS1117;
    int32_t _M0L6_2atmpS1113;
    uint32_t _M0L6_2atmpS1116;
    uint32_t _M0L6_2atmpS1115;
    int32_t _M0L6_2atmpS1114;
    int32_t _M0L3lenS1119;
    int32_t _M0L6_2atmpS1118;
    if (_M0L6_2atmpS1101 < 2) {
      int32_t _M0L3lenS1106 = _M0L4selfS133->$1;
      int32_t _M0L6_2atmpS1105 = _M0L3lenS1106 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS133, _M0L6_2atmpS1105);
    }
    _M0L4codeS134 = _M0L4codeS131 - 65536u;
    _M0L4dataS1107 = _M0L4selfS133->$0;
    _M0L3lenS1108 = _M0L4selfS133->$1;
    _M0L6_2atmpS1111 = _M0L4codeS134 >> 10;
    _M0L6_2atmpS1110 = 55296u + _M0L6_2atmpS1111;
    moonbit_incref_cycle_free(_M0L4dataS1107);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1109 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1110);
    if (
      _M0L3lenS1108 < 0
      || _M0L3lenS1108 >= Moonbit_array_length(_M0L4dataS1107)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1107[_M0L3lenS1108] = _M0L6_2atmpS1109;
    moonbit_decref_cycle_free(_M0L4dataS1107);
    _M0L4dataS1112 = _M0L4selfS133->$0;
    _M0L3lenS1117 = _M0L4selfS133->$1;
    _M0L6_2atmpS1113 = _M0L3lenS1117 + 1;
    _M0L6_2atmpS1116 = _M0L4codeS134 & 1023u;
    _M0L6_2atmpS1115 = 56320u + _M0L6_2atmpS1116;
    moonbit_incref_cycle_free(_M0L4dataS1112);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1114 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1115);
    if (
      _M0L6_2atmpS1113 < 0
      || _M0L6_2atmpS1113 >= Moonbit_array_length(_M0L4dataS1112)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1112[_M0L6_2atmpS1113] = _M0L6_2atmpS1114;
    moonbit_decref_cycle_free(_M0L4dataS1112);
    _M0L3lenS1119 = _M0L4selfS133->$1;
    _M0L6_2atmpS1118 = _M0L3lenS1119 + 2;
    _M0L4selfS133->$1 = _M0L6_2atmpS1118;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_26.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS128,
  int32_t _M0L8requiredS129
) {
  uint16_t* _M0L4dataS1090;
  int32_t _M0L6_2atmpS1088;
  int32_t _M0L3lenS1089;
  int32_t _M0L13new__capacityS127;
  uint16_t* _M0L4dataS1085;
  int32_t _M0L6_2atmpS1086;
  int32_t _M0L3lenS1087;
  uint16_t* _M0L9new__dataS130;
  uint16_t* _M0L6_2aoldS2121;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1090 = _M0L4selfS128->$0;
  _M0L6_2atmpS1088 = Moonbit_array_length(_M0L4dataS1090);
  _M0L3lenS1089 = _M0L4selfS128->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS127
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1088, _M0L3lenS1089, _M0L8requiredS129);
  _M0L4dataS1085 = _M0L4selfS128->$0;
  moonbit_incref_cycle_free(_M0L4dataS1085);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1086 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1087 = _M0L4selfS128->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS130
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1085, _M0L13new__capacityS127, _M0L6_2atmpS1086, _M0L3lenS1087, 0, 0);
  _M0L6_2aoldS2121 = _M0L4selfS128->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2121);
  _M0L4selfS128->$0 = _M0L9new__dataS130;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS126,
  int32_t _M0L3lenS122,
  int32_t _M0L8requiredS121
) {
  int32_t _M0L5spaceS123;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS121 < _M0L3lenS122) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_27.data);
  }
  _M0L5spaceS123 = _M0L7currentS126;
  while (1) {
    if (_M0L5spaceS123 < _M0L8requiredS121) {
      int32_t _M0L4nextS124 = _M0L5spaceS123 * 2;
      if (_M0L4nextS124 <= _M0L5spaceS123) {
        return _M0L8requiredS121;
      }
      _M0L5spaceS123 = _M0L4nextS124;
      continue;
    } else {
      return _M0L5spaceS123;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS120) {
  int32_t _M0L6_2atmpS1084;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1084 = *(int32_t*)&_M0L4selfS120;
  return (uint16_t)_M0L6_2atmpS1084;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS119) {
  int32_t _M0L6_2atmpS1083;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1083 = _M0L4selfS119;
  return *(uint32_t*)&_M0L6_2atmpS1083;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS117
) {
  int32_t _M0L3lenS1074;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1074 = _M0L4selfS117->$1;
  if (_M0L3lenS1074 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1075 = _M0L4selfS117->$1;
    uint16_t* _M0L4dataS1077 = _M0L4selfS117->$0;
    int32_t _M0L6_2atmpS1076 = Moonbit_array_length(_M0L4dataS1077);
    if (_M0L3lenS1075 == _M0L6_2atmpS1076) {
      uint16_t* _M0L4dataS1078 = _M0L4selfS117->$0;
      moonbit_incref_cycle_free(_M0L4dataS1078);
      return _M0L4dataS1078;
    } else {
      uint16_t* _M0L4dataS1079 = _M0L4selfS117->$0;
      int32_t _M0L3lenS1080 = _M0L4selfS117->$1;
      int32_t _M0L6_2atmpS1081;
      int32_t _M0L3lenS1082;
      uint16_t* _M0L4dataS118;
      moonbit_incref_cycle_free(_M0L4dataS1079);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1081 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1082 = _M0L4selfS117->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS118
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1079, _M0L3lenS1080, _M0L6_2atmpS1081, _M0L3lenS1082, 0, 0);
      return _M0L4dataS118;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS114,
  int32_t _M0L13allocate__lenS110,
  int32_t _M0L4initS115,
  int32_t _M0L3lenS111,
  int32_t _M0L11src__offsetS112,
  int32_t _M0L11dst__offsetS113
) {
  int32_t _if__result_2242;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS110 >= 0) {
    if (_M0L3lenS111 >= 0) {
      if (_M0L11src__offsetS112 >= 0) {
        if (_M0L11dst__offsetS113 >= 0) {
          int32_t _M0L6_2atmpS1070 = _M0L11src__offsetS112 + _M0L3lenS111;
          int32_t _M0L6_2atmpS1071 = Moonbit_array_length(_M0L3srcS114);
          if (_M0L6_2atmpS1070 <= _M0L6_2atmpS1071) {
            int32_t _M0L6_2atmpS1069 = _M0L11dst__offsetS113 + _M0L3lenS111;
            _if__result_2242 = _M0L6_2atmpS1069 <= _M0L13allocate__lenS110;
          } else {
            _if__result_2242 = 0;
          }
        } else {
          _if__result_2242 = 0;
        }
      } else {
        _if__result_2242 = 0;
      }
    } else {
      _if__result_2242 = 0;
    }
  } else {
    _if__result_2242 = 0;
  }
  if (_if__result_2242) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS114, _M0L13allocate__lenS110, _M0L4initS115, _M0L11src__offsetS112, _M0L11dst__offsetS113, _M0L3lenS111);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS116;
    int32_t _M0L6_2atmpS1073;
    moonbit_string_t _M0L6_2atmpS1072;
    uint16_t* _result_2243;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS116
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS116, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS116, _M0L13allocate__lenS110);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS116, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS116, _M0L11src__offsetS112);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS116, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS116, _M0L11dst__offsetS113);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS116, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS116, _M0L3lenS111);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS116, (moonbit_string_t)moonbit_string_literal_32.data);
    _M0L6_2atmpS1073 = Moonbit_array_length(_M0L3srcS114);
    moonbit_decref_cycle_free(_M0L3srcS114);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS116, _M0L6_2atmpS1073);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1072
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS116);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS116);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2243 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1072);
    moonbit_decref_cycle_free(_M0L6_2atmpS1072);
    return _result_2243;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS107,
  int32_t _M0L13allocate__lenS104,
  int32_t _M0L4initS105,
  int32_t _M0L11src__offsetS108,
  int32_t _M0L11dst__offsetS106,
  int32_t _M0L9blit__lenS109
) {
  uint16_t* _M0L3dstS103;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS103
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS104, _M0L4initS105);
  moonbit_incref_cycle_free(_M0L3dstS103);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS103, _M0L11dst__offsetS106, _M0L3srcS107, _M0L11src__offsetS108, _M0L9blit__lenS109, sizeof(uint16_t));
  return _M0L3dstS103;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS101
) {
  int32_t _M0L7initialS100;
  uint16_t* _M0L4dataS102;
  struct _M0TPB13StringBuilder* _block_2244;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS101 < 1) {
    _M0L7initialS100 = 1;
  } else {
    int32_t _M0L6_2atmpS1068 = _M0L10size__hintS101 + 1;
    _M0L7initialS100 = _M0L6_2atmpS1068 / 2;
  }
  _M0L4dataS102 = (uint16_t*)moonbit_make_string(_M0L7initialS100, 0);
  _block_2244
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2244)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 68, 0);
  _block_2244->$0 = _M0L4dataS102;
  _block_2244->$1 = 0;
  return _block_2244;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS99) {
  int32_t _M0L6_2atmpS1067;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1067 = (int32_t)_M0L4selfS99;
  return _M0L6_2atmpS1067;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS91,
  int32_t _M0L13allocate__lenS87,
  int32_t _M0L3lenS88,
  int32_t _M0L11src__offsetS89,
  int32_t _M0L11dst__offsetS90
) {
  int32_t _if__result_2245;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS87 >= 0) {
    if (_M0L3lenS88 >= 0) {
      if (_M0L11src__offsetS89 >= 0) {
        if (_M0L11dst__offsetS90 >= 0) {
          int32_t _M0L6_2atmpS1058 = _M0L11src__offsetS89 + _M0L3lenS88;
          int32_t _M0L6_2atmpS1059;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1059
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS91);
          if (_M0L6_2atmpS1058 <= _M0L6_2atmpS1059) {
            int32_t _M0L6_2atmpS1057 = _M0L11dst__offsetS90 + _M0L3lenS88;
            _if__result_2245 = _M0L6_2atmpS1057 <= _M0L13allocate__lenS87;
          } else {
            _if__result_2245 = 0;
          }
        } else {
          _if__result_2245 = 0;
        }
      } else {
        _if__result_2245 = 0;
      }
    } else {
      _if__result_2245 = 0;
    }
  } else {
    _if__result_2245 = 0;
  }
  if (_if__result_2245) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS87, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS91, _M0L11src__offsetS89, _M0L11dst__offsetS90, _M0L3lenS88);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS92;
    int32_t _M0L6_2atmpS1061;
    moonbit_string_t _M0L6_2atmpS1060;
    moonbit_string_t* _result_2246;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS92
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS92, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS92, _M0L13allocate__lenS87);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS92, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS92, _M0L11src__offsetS89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS92, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS92, _M0L11dst__offsetS90);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS92, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS92, _M0L3lenS88);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS92, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1061 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS91);
    moonbit_decref_cycle_free(_M0L3srcS91);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS92, _M0L6_2atmpS1061);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1060
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS92);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS92);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2246
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1060);
    moonbit_decref_cycle_free(_M0L6_2atmpS1060);
    return _result_2246;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS97,
  int32_t _M0L13allocate__lenS93,
  int32_t _M0L3lenS94,
  int32_t _M0L11src__offsetS95,
  int32_t _M0L11dst__offsetS96
) {
  int32_t _if__result_2247;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS93 >= 0) {
    if (_M0L3lenS94 >= 0) {
      if (_M0L11src__offsetS95 >= 0) {
        if (_M0L11dst__offsetS96 >= 0) {
          int32_t _M0L6_2atmpS1063 = _M0L11src__offsetS95 + _M0L3lenS94;
          int32_t _M0L6_2atmpS1064;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1064
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS97);
          if (_M0L6_2atmpS1063 <= _M0L6_2atmpS1064) {
            int32_t _M0L6_2atmpS1062 = _M0L11dst__offsetS96 + _M0L3lenS94;
            _if__result_2247 = _M0L6_2atmpS1062 <= _M0L13allocate__lenS93;
          } else {
            _if__result_2247 = 0;
          }
        } else {
          _if__result_2247 = 0;
        }
      } else {
        _if__result_2247 = 0;
      }
    } else {
      _if__result_2247 = 0;
    }
  } else {
    _if__result_2247 = 0;
  }
  if (_if__result_2247) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS93, 0, _M0L3srcS97, _M0L11src__offsetS95, _M0L11dst__offsetS96, _M0L3lenS94);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS98;
    int32_t _M0L6_2atmpS1066;
    moonbit_string_t _M0L6_2atmpS1065;
    struct _M0TUsiE** _result_2248;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS98
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS98, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS98, _M0L13allocate__lenS93);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS98, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS98, _M0L11src__offsetS95);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS98, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS98, _M0L11dst__offsetS96);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS98, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS98, _M0L3lenS94);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS98, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1066 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS97);
    moonbit_decref_cycle_free(_M0L3srcS97);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS98, _M0L6_2atmpS1066);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1065
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS98);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS98);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2248
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1065);
    moonbit_decref_cycle_free(_M0L6_2atmpS1065);
    return _result_2248;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS82,
  moonbit_string_t _M0L3objS81
) {
  struct _M0TPB6Logger _M0L6_2atmpS1054;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS82);
  _M0L6_2atmpS1054
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS82
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS81, _M0L6_2atmpS1054);
  if (_M0L6_2atmpS1054.$1) {
    moonbit_decref(_M0L6_2atmpS1054.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS84,
  int32_t _M0L3objS83
) {
  struct _M0TPB6Logger _M0L6_2atmpS1055;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS84);
  _M0L6_2atmpS1055
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS84
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS83, _M0L6_2atmpS1055);
  if (_M0L6_2atmpS1055.$1) {
    moonbit_decref(_M0L6_2atmpS1055.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS86,
  uint64_t _M0L3objS85
) {
  struct _M0TPB6Logger _M0L6_2atmpS1056;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS86);
  _M0L6_2atmpS1056
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS86
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS85, _M0L6_2atmpS1056);
  if (_M0L6_2atmpS1056.$1) {
    moonbit_decref(_M0L6_2atmpS1056.$1);
  }
  return 0;
}

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t* _M0L3srcS72,
  int32_t _M0L13allocate__lenS70,
  int32_t _M0L11src__offsetS73,
  int32_t _M0L11dst__offsetS71,
  int32_t _M0L9blit__lenS74
) {
  moonbit_string_t* _M0L3dstS69;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS69
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L13allocate__lenS70, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGsE(_M0L3dstS69, _M0L11dst__offsetS71, _M0L3srcS72, _M0L11src__offsetS73, _M0L9blit__lenS74);
  moonbit_decref_cycle_free(_M0L3srcS72);
  return _M0L3dstS69;
}

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE** _M0L3srcS78,
  int32_t _M0L13allocate__lenS76,
  int32_t _M0L11src__offsetS79,
  int32_t _M0L11dst__offsetS77,
  int32_t _M0L9blit__lenS80
) {
  struct _M0TUsiE** _M0L3dstS75;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS75
  = (struct _M0TUsiE**)moonbit_make_ref_array(_M0L13allocate__lenS76, 0);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGUsiEE(_M0L3dstS75, _M0L11dst__offsetS77, _M0L3srcS78, _M0L11src__offsetS79, _M0L9blit__lenS80);
  moonbit_decref_cycle_free(_M0L3srcS78);
  return _M0L3dstS75;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS59,
  int32_t _M0L11dst__offsetS60,
  moonbit_string_t* _M0L3srcS61,
  int32_t _M0L11src__offsetS62,
  int32_t _M0L3lenS63
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS61);
  moonbit_incref_cycle_free(_M0L3dstS59);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS59, _M0L11dst__offsetS60, _M0L3srcS61, _M0L11src__offsetS62, _M0L3lenS63);
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS64,
  int32_t _M0L11dst__offsetS65,
  struct _M0TUsiE** _M0L3srcS66,
  int32_t _M0L11src__offsetS67,
  int32_t _M0L3lenS68
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS66);
  moonbit_incref_cycle_free(_M0L3dstS64);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS64, _M0L11dst__offsetS65, _M0L3srcS66, _M0L11src__offsetS67, _M0L3lenS68);
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS14,
  int32_t _M0L11dst__offsetS16,
  float* _M0L3srcS15,
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
        int32_t _M0L6_2atmpS1009 = _M0L11dst__offsetS16 + _M0L1iS18;
        int32_t _M0L6_2atmpS1011 = _M0L11src__offsetS17 + _M0L1iS18;
        float _M0L6_2atmpS1010;
        int32_t _M0L6_2atmpS1012;
        if (
          _M0L6_2atmpS1011 < 0
          || _M0L6_2atmpS1011 >= Moonbit_array_length(_M0L3srcS15)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1010 = (float)_M0L3srcS15[_M0L6_2atmpS1011];
        if (
          _M0L6_2atmpS1009 < 0
          || _M0L6_2atmpS1009 >= Moonbit_array_length(_M0L3dstS14)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS14[_M0L6_2atmpS1009] = _M0L6_2atmpS1010;
        _M0L6_2atmpS1012 = _M0L1iS18 + 1;
        _M0L1iS18 = _M0L6_2atmpS1012;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS15);
        moonbit_decref_cycle_free(_M0L3dstS14);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1017 = _M0L3lenS19 - 1;
    int32_t _M0L1iS21 = _M0L6_2atmpS1017;
    while (1) {
      if (_M0L1iS21 >= 0) {
        int32_t _M0L6_2atmpS1013 = _M0L11dst__offsetS16 + _M0L1iS21;
        int32_t _M0L6_2atmpS1015 = _M0L11src__offsetS17 + _M0L1iS21;
        float _M0L6_2atmpS1014;
        int32_t _M0L6_2atmpS1016;
        if (
          _M0L6_2atmpS1015 < 0
          || _M0L6_2atmpS1015 >= Moonbit_array_length(_M0L3srcS15)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1014 = (float)_M0L3srcS15[_M0L6_2atmpS1015];
        if (
          _M0L6_2atmpS1013 < 0
          || _M0L6_2atmpS1013 >= Moonbit_array_length(_M0L3dstS14)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS14[_M0L6_2atmpS1013] = _M0L6_2atmpS1014;
        _M0L6_2atmpS1016 = _M0L1iS21 - 1;
        _M0L1iS21 = _M0L6_2atmpS1016;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS23,
  int32_t _M0L11dst__offsetS25,
  int32_t* _M0L3srcS24,
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
        int32_t _M0L6_2atmpS1018 = _M0L11dst__offsetS25 + _M0L1iS27;
        int32_t _M0L6_2atmpS1020 = _M0L11src__offsetS26 + _M0L1iS27;
        int32_t _M0L6_2atmpS1019;
        int32_t _M0L6_2atmpS1021;
        if (
          _M0L6_2atmpS1020 < 0
          || _M0L6_2atmpS1020 >= Moonbit_array_length(_M0L3srcS24)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1019 = (int32_t)_M0L3srcS24[_M0L6_2atmpS1020];
        if (
          _M0L6_2atmpS1018 < 0
          || _M0L6_2atmpS1018 >= Moonbit_array_length(_M0L3dstS23)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS23[_M0L6_2atmpS1018] = _M0L6_2atmpS1019;
        _M0L6_2atmpS1021 = _M0L1iS27 + 1;
        _M0L1iS27 = _M0L6_2atmpS1021;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS24);
        moonbit_decref_cycle_free(_M0L3dstS23);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1026 = _M0L3lenS28 - 1;
    int32_t _M0L1iS30 = _M0L6_2atmpS1026;
    while (1) {
      if (_M0L1iS30 >= 0) {
        int32_t _M0L6_2atmpS1022 = _M0L11dst__offsetS25 + _M0L1iS30;
        int32_t _M0L6_2atmpS1024 = _M0L11src__offsetS26 + _M0L1iS30;
        int32_t _M0L6_2atmpS1023;
        int32_t _M0L6_2atmpS1025;
        if (
          _M0L6_2atmpS1024 < 0
          || _M0L6_2atmpS1024 >= Moonbit_array_length(_M0L3srcS24)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1023 = (int32_t)_M0L3srcS24[_M0L6_2atmpS1024];
        if (
          _M0L6_2atmpS1022 < 0
          || _M0L6_2atmpS1022 >= Moonbit_array_length(_M0L3dstS23)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS23[_M0L6_2atmpS1022] = _M0L6_2atmpS1023;
        _M0L6_2atmpS1025 = _M0L1iS30 - 1;
        _M0L1iS30 = _M0L6_2atmpS1025;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t* _M0L3dstS32,
  int32_t _M0L11dst__offsetS34,
  uint16_t* _M0L3srcS33,
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
        int32_t _M0L6_2atmpS1027 = _M0L11dst__offsetS34 + _M0L1iS36;
        int32_t _M0L6_2atmpS1029 = _M0L11src__offsetS35 + _M0L1iS36;
        int32_t _M0L6_2atmpS1028;
        int32_t _M0L6_2atmpS1030;
        if (
          _M0L6_2atmpS1029 < 0
          || _M0L6_2atmpS1029 >= Moonbit_array_length(_M0L3srcS33)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1028 = (int32_t)_M0L3srcS33[_M0L6_2atmpS1029];
        if (
          _M0L6_2atmpS1027 < 0
          || _M0L6_2atmpS1027 >= Moonbit_array_length(_M0L3dstS32)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS32[_M0L6_2atmpS1027] = _M0L6_2atmpS1028;
        _M0L6_2atmpS1030 = _M0L1iS36 + 1;
        _M0L1iS36 = _M0L6_2atmpS1030;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS33);
        moonbit_decref_cycle_free(_M0L3dstS32);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1035 = _M0L3lenS37 - 1;
    int32_t _M0L1iS39 = _M0L6_2atmpS1035;
    while (1) {
      if (_M0L1iS39 >= 0) {
        int32_t _M0L6_2atmpS1031 = _M0L11dst__offsetS34 + _M0L1iS39;
        int32_t _M0L6_2atmpS1033 = _M0L11src__offsetS35 + _M0L1iS39;
        int32_t _M0L6_2atmpS1032;
        int32_t _M0L6_2atmpS1034;
        if (
          _M0L6_2atmpS1033 < 0
          || _M0L6_2atmpS1033 >= Moonbit_array_length(_M0L3srcS33)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1032 = (int32_t)_M0L3srcS33[_M0L6_2atmpS1033];
        if (
          _M0L6_2atmpS1031 < 0
          || _M0L6_2atmpS1031 >= Moonbit_array_length(_M0L3dstS32)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS32[_M0L6_2atmpS1031] = _M0L6_2atmpS1032;
        _M0L6_2atmpS1034 = _M0L1iS39 - 1;
        _M0L1iS39 = _M0L6_2atmpS1034;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS41,
  int32_t _M0L11dst__offsetS43,
  moonbit_string_t* _M0L3srcS42,
  int32_t _M0L11src__offsetS44,
  int32_t _M0L3lenS46
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS41 == _M0L3srcS42 && _M0L11dst__offsetS43 < _M0L11src__offsetS44
  ) {
    int32_t _M0L1iS45 = 0;
    while (1) {
      if (_M0L1iS45 < _M0L3lenS46) {
        int32_t _M0L6_2atmpS1036 = _M0L11dst__offsetS43 + _M0L1iS45;
        int32_t _M0L6_2atmpS1038 = _M0L11src__offsetS44 + _M0L1iS45;
        moonbit_string_t _M0L6_2atmpS1037;
        moonbit_string_t _M0L6_2aoldS2122;
        int32_t _M0L6_2atmpS1039;
        if (
          _M0L6_2atmpS1038 < 0
          || _M0L6_2atmpS1038 >= Moonbit_array_length(_M0L3srcS42)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1037 = (moonbit_string_t)_M0L3srcS42[_M0L6_2atmpS1038];
        if (
          _M0L6_2atmpS1036 < 0
          || _M0L6_2atmpS1036 >= Moonbit_array_length(_M0L3dstS41)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2122 = (moonbit_string_t)_M0L3dstS41[_M0L6_2atmpS1036];
        moonbit_incref_cycle_free(_M0L6_2atmpS1037);
        moonbit_decref_cycle_free(_M0L6_2aoldS2122);
        _M0L3dstS41[_M0L6_2atmpS1036] = _M0L6_2atmpS1037;
        _M0L6_2atmpS1039 = _M0L1iS45 + 1;
        _M0L1iS45 = _M0L6_2atmpS1039;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS42);
        moonbit_decref_cycle_free(_M0L3dstS41);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1044 = _M0L3lenS46 - 1;
    int32_t _M0L1iS48 = _M0L6_2atmpS1044;
    while (1) {
      if (_M0L1iS48 >= 0) {
        int32_t _M0L6_2atmpS1040 = _M0L11dst__offsetS43 + _M0L1iS48;
        int32_t _M0L6_2atmpS1042 = _M0L11src__offsetS44 + _M0L1iS48;
        moonbit_string_t _M0L6_2atmpS1041;
        moonbit_string_t _M0L6_2aoldS2123;
        int32_t _M0L6_2atmpS1043;
        if (
          _M0L6_2atmpS1042 < 0
          || _M0L6_2atmpS1042 >= Moonbit_array_length(_M0L3srcS42)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1041 = (moonbit_string_t)_M0L3srcS42[_M0L6_2atmpS1042];
        if (
          _M0L6_2atmpS1040 < 0
          || _M0L6_2atmpS1040 >= Moonbit_array_length(_M0L3dstS41)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2123 = (moonbit_string_t)_M0L3dstS41[_M0L6_2atmpS1040];
        moonbit_incref_cycle_free(_M0L6_2atmpS1041);
        moonbit_decref_cycle_free(_M0L6_2aoldS2123);
        _M0L3dstS41[_M0L6_2atmpS1040] = _M0L6_2atmpS1041;
        _M0L6_2atmpS1043 = _M0L1iS48 - 1;
        _M0L1iS48 = _M0L6_2atmpS1043;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS42);
        moonbit_decref_cycle_free(_M0L3dstS41);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS50,
  int32_t _M0L11dst__offsetS52,
  struct _M0TUsiE** _M0L3srcS51,
  int32_t _M0L11src__offsetS53,
  int32_t _M0L3lenS55
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS50 == _M0L3srcS51 && _M0L11dst__offsetS52 < _M0L11src__offsetS53
  ) {
    int32_t _M0L1iS54 = 0;
    while (1) {
      if (_M0L1iS54 < _M0L3lenS55) {
        int32_t _M0L6_2atmpS1045 = _M0L11dst__offsetS52 + _M0L1iS54;
        int32_t _M0L6_2atmpS1047 = _M0L11src__offsetS53 + _M0L1iS54;
        struct _M0TUsiE* _M0L6_2atmpS1046;
        struct _M0TUsiE* _M0L6_2aoldS2124;
        int32_t _M0L6_2atmpS1048;
        if (
          _M0L6_2atmpS1047 < 0
          || _M0L6_2atmpS1047 >= Moonbit_array_length(_M0L3srcS51)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1046 = (struct _M0TUsiE*)_M0L3srcS51[_M0L6_2atmpS1047];
        if (
          _M0L6_2atmpS1045 < 0
          || _M0L6_2atmpS1045 >= Moonbit_array_length(_M0L3dstS50)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2124 = (struct _M0TUsiE*)_M0L3dstS50[_M0L6_2atmpS1045];
        if (_M0L6_2atmpS1046) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1046);
        }
        if (_M0L6_2aoldS2124) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2124);
        }
        _M0L3dstS50[_M0L6_2atmpS1045] = _M0L6_2atmpS1046;
        _M0L6_2atmpS1048 = _M0L1iS54 + 1;
        _M0L1iS54 = _M0L6_2atmpS1048;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS51);
        moonbit_decref_cycle_free(_M0L3dstS50);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1053 = _M0L3lenS55 - 1;
    int32_t _M0L1iS57 = _M0L6_2atmpS1053;
    while (1) {
      if (_M0L1iS57 >= 0) {
        int32_t _M0L6_2atmpS1049 = _M0L11dst__offsetS52 + _M0L1iS57;
        int32_t _M0L6_2atmpS1051 = _M0L11src__offsetS53 + _M0L1iS57;
        struct _M0TUsiE* _M0L6_2atmpS1050;
        struct _M0TUsiE* _M0L6_2aoldS2125;
        int32_t _M0L6_2atmpS1052;
        if (
          _M0L6_2atmpS1051 < 0
          || _M0L6_2atmpS1051 >= Moonbit_array_length(_M0L3srcS51)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1050 = (struct _M0TUsiE*)_M0L3srcS51[_M0L6_2atmpS1051];
        if (
          _M0L6_2atmpS1049 < 0
          || _M0L6_2atmpS1049 >= Moonbit_array_length(_M0L3dstS50)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2125 = (struct _M0TUsiE*)_M0L3dstS50[_M0L6_2atmpS1049];
        if (_M0L6_2atmpS1050) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1050);
        }
        if (_M0L6_2aoldS2125) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2125);
        }
        _M0L3dstS50[_M0L6_2atmpS1049] = _M0L6_2atmpS1050;
        _M0L6_2atmpS1052 = _M0L1iS57 - 1;
        _M0L1iS57 = _M0L6_2atmpS1052;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS51);
        moonbit_decref_cycle_free(_M0L3dstS50);
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
  _M0L10_2ax__6388S11.$0->$method_0(_M0L10_2ax__6388S11.$1, (moonbit_string_t)moonbit_string_literal_33.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S11, _M0L15_2a_2aarg__6389S10);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S11.$0->$method_0(_M0L10_2ax__6388S11.$1, (moonbit_string_t)moonbit_string_literal_34.data);
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS981) {
  switch (Moonbit_object_tag(_M0L4_2aeS981)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_35.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS981);
      break;
    }
    
    case 1: {
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
  void* _M0L11_2aobj__ptrS1004,
  struct _M0TPB4Show _M0L8_2aparamS1003
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1002 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1004;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1002, _M0L8_2aparamS1003);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1001,
  struct _M0TPB4Show _M0L8_2aparamS1000
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS999 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1001;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS999, _M0L8_2aparamS1000);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS998,
  int32_t _M0L8_2aparamS997
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS996 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS998;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS996, _M0L8_2aparamS997);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS995,
  struct _M0TPC16string10StringView _M0L8_2aparamS994
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS993 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS995;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS993, _M0L8_2aparamS994);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS992,
  moonbit_string_t _M0L8_2aparamS989,
  int32_t _M0L8_2aparamS990,
  int32_t _M0L8_2aparamS991
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS988 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS992;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS988, _M0L8_2aparamS989, _M0L8_2aparamS990, _M0L8_2aparamS991);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS987,
  moonbit_string_t _M0L8_2aparamS986
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS985 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS987;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS985, _M0L8_2aparamS986);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1008;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS974;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS975;
  int32_t _M0L7_2abindS976;
  struct _M0TUsiE** _M0L7_2abindS977;
  int32_t _M0L6_2acntS2130;
  int32_t _M0L2__S978;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1008
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS974
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS974)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 71, 0);
  _M0L12async__testsS974->$0 = _M0L6_2atmpS1008;
  _M0L12async__testsS974->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS975
  = _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS976 = _M0L7_2abindS975->$1;
  _M0L7_2abindS977 = _M0L7_2abindS975->$0;
  _M0L6_2acntS2130
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS975));
  if (_M0L6_2acntS2130 > 1) {
    int32_t _M0L11_2anew__cntS2131 = _M0L6_2acntS2130 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS975), _M0L11_2anew__cntS2131);
    moonbit_incref_cycle_free(_M0L7_2abindS977);
  } else if (_M0L6_2acntS2130 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS975);
  }
  _M0L2__S978 = 0;
  while (1) {
    if (_M0L2__S978 < _M0L7_2abindS976) {
      struct _M0TUsiE* _M0L3argS979 =
        (struct _M0TUsiE*)_M0L7_2abindS977[_M0L2__S978];
      moonbit_string_t _M0L6_2atmpS1005 = _M0L3argS979->$0;
      int32_t _M0L6_2atmpS1006 = _M0L3argS979->$1;
      int32_t _M0L6_2atmpS1007;
      moonbit_incref_cycle_free(_M0L6_2atmpS1005);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS974, _M0L6_2atmpS1005, _M0L6_2atmpS1006);
      moonbit_decref_cycle_free(_M0L6_2atmpS1005);
      _M0L6_2atmpS1007 = _M0L2__S978 + 1;
      _M0L2__S978 = _M0L6_2atmpS1007;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS977);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\lkd2014_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples29lkd2014__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS974);
  moonbit_decref_cycle_free(_M0L12async__testsS974);
  moonbit_flush_cycles();
  return 0;
}