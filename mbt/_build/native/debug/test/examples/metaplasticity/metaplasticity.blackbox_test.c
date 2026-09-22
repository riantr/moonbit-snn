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
struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TWRPC15error5ErrorEs;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TUdiE;

struct _M0BTPB6Logger;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0TP26RiantR8snn__mbt20SynapseNormalization;

struct _M0TPB6Logger;

struct _M0TP26RiantR8snn__mbt13SynapseTarget;

struct _M0TPB5ArrayGUsiEE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1187;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TWRPC15error5ErrorEu;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE;

struct _M0TPB8MutLocalGiE;

struct _M0TP26RiantR8snn__mbt14SpikingSynapse;

struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TPB4Show;

struct _M0TPB8MutLocalGfE;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGbE;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB5ArrayGsE;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1182;

struct _M0TP26RiantR8snn__mbt18MultiplicativeNorm;

struct _M0DTP26RiantR8snn__mbt9NormParam10Additive__;

struct _M0TWEu;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0TUddE;

struct _M0TP26RiantR8snn__mbt12AdditiveNorm;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0DTP26RiantR8snn__mbt9NormParam16Multiplicative__;

struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
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

struct _M0TP26RiantR8snn__mbt20SynapseNormalization {
  void* $0;
  int32_t $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE* $5;
  
};

struct _M0TPB6Logger {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0TP26RiantR8snn__mbt13SynapseTarget {
  int32_t $0;
  struct _M0TPB5ArrayGfE* $1;
  struct _M0TPB5ArrayGiE* $2;
  
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

struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1187 {
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

struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE {
  struct _M0TP26RiantR8snn__mbt13SynapseTarget** $0;
  int32_t $1;
  
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

struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
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

struct _M0TPB5ArrayGRPB5ArrayGfEE {
  struct _M0TPB5ArrayGfE** $0;
  int32_t $1;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1182 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0TP26RiantR8snn__mbt18MultiplicativeNorm {
  float $0;
  
};

struct _M0DTP26RiantR8snn__mbt9NormParam10Additive__ {
  struct _M0TP26RiantR8snn__mbt12AdditiveNorm* $0;
  
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

struct _M0TUddE {
  double $0;
  double $1;
  
};

struct _M0TP26RiantR8snn__mbt12AdditiveNorm {
  float $0;
  
};

struct _M0TPB7Umul128 {
  uint64_t $0;
  uint64_t $1;
  
};

struct _M0TPB8Pow5Pair {
  uint64_t $0;
  uint64_t $1;
  
};

struct _M0DTP26RiantR8snn__mbt9NormParam16Multiplicative__ {
  struct _M0TP26RiantR8snn__mbt18MultiplicativeNorm* $0;
  
};

struct moonbit_result_0 {
  int tag;
  union { int32_t ok; void* err;  } data;
  
};

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1194(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1187(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1182(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1159(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1152(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
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

int32_t _M0FP26RiantR8snn__mbt20metaplasticity__step(
  struct _M0TP26RiantR8snn__mbt20SynapseNormalization*
);

struct _M0TP26RiantR8snn__mbt20SynapseNormalization* _M0MP26RiantR8snn__mbt20SynapseNormalization3new(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE*,
  void*
);

struct _M0TP26RiantR8snn__mbt18MultiplicativeNorm* _M0MP26RiantR8snn__mbt18MultiplicativeNorm3new(
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

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
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

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

struct _M0TP26RiantR8snn__mbt13SynapseTarget* _M0MPC15array5Array2atGRP26RiantR8snn__mbt13SynapseTargetE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE*,
  int32_t
);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

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

int32_t _M0MPC15array5Array6lengthGRP26RiantR8snn__mbt13SynapseTargetE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE*
);

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

struct _M0TP26RiantR8snn__mbt13SynapseTarget** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt13SynapseTargetE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE*
);

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE*);

moonbit_string_t* _M0MPC15array5Array6bufferGsE(struct _M0TPB5ArrayGsE*);

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE*
);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[121]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 120, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 109, 101, 116, 97, 112, 108, 97, 
    115, 116, 105, 99, 105, 116, 121, 95, 98, 108, 97, 99, 107, 98, 111, 
    120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 
    84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 
    114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 46, 77, 
    111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 
    101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 
    84, 101, 115, 116, 0
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
} const moonbit_string_literal_35 =
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
} const moonbit_string_literal_33 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[119]; 
} const moonbit_string_literal_32 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 118, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 109, 101, 116, 97, 112, 108, 97, 
    115, 116, 105, 99, 105, 116, 121, 95, 98, 108, 97, 99, 107, 98, 111, 
    120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 
    84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 
    114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 46, 77, 111, 
    111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 
    114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 
    111, 114, 0
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

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1194$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1194
  };

uint32_t const moonbit_layout_table_data[90] =
  {
    sizeof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1182)
    / 4, 1,
    offsetof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1182, $1)
    / 4
    * 2,
    sizeof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1187)
    / 4, 1,
    offsetof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1187, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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
    sizeof(struct _M0TP26RiantR8snn__mbt20SynapseNormalization) / 4, 
    5,
    offsetof(struct _M0TP26RiantR8snn__mbt20SynapseNormalization, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt20SynapseNormalization, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt20SynapseNormalization, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt20SynapseNormalization, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt20SynapseNormalization, $5) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2359
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1215,
  moonbit_string_t _M0L8filenameS1184,
  int32_t _M0L5indexS1186
) {
  struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1182* _closure_2410;
  struct _M0TWEu* _M0L13handle__startS1182;
  struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1187* _closure_2411;
  struct _M0TWssbEu* _M0L14handle__resultS1187;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1194;
  void* _M0L11_2atry__errS1209;
  struct moonbit_result_0 _tmp_2413;
  int32_t _handle__error__result_2414;
  int32_t _M0L6_2atmpS2347;
  void* _M0L3errS1210;
  moonbit_string_t _M0L4nameS1212;
  struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1213;
  moonbit_string_t _M0L7_2anameS1214;
  int32_t _M0L6_2acntS2383;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1184);
  _closure_2410
  = (struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1182*)moonbit_malloc(sizeof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1182));
  Moonbit_object_header(_closure_2410)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2410->code
  = &_M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1182;
  _closure_2410->$0 = _M0L5indexS1186;
  _closure_2410->$1 = _M0L8filenameS1184;
  _M0L13handle__startS1182 = (struct _M0TWEu*)_closure_2410;
  moonbit_incref_cycle_free(_M0L8filenameS1184);
  _closure_2411
  = (struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1187*)moonbit_malloc(sizeof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1187));
  Moonbit_object_header(_closure_2411)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2411->code
  = &_M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1187;
  _closure_2411->$0 = _M0L5indexS1186;
  _closure_2411->$1 = _M0L8filenameS1184;
  _M0L14handle__resultS1187 = (struct _M0TWssbEu*)_closure_2411;
  _M0L17error__to__stringS1194
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1194$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2413
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1215, _M0L8filenameS1184, _M0L5indexS1186, _M0L13handle__startS1182, _M0L14handle__resultS1187, _M0L17error__to__stringS1194);
  if (_tmp_2413.tag) {
    int32_t const _M0L5_2aokS2356 = _tmp_2413.data.ok;
    _handle__error__result_2414 = _M0L5_2aokS2356;
  } else {
    void* const _M0L6_2aerrS2357 = _tmp_2413.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1194);
    moonbit_decref_cycle_free(_M0L13handle__startS1182);
    _M0L11_2atry__errS1209 = _M0L6_2aerrS2357;
    goto join_1208;
  }
  if (_handle__error__result_2414) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1194);
    moonbit_decref_cycle_free(_M0L13handle__startS1182);
    _M0L6_2atmpS2347 = 1;
  } else {
    struct moonbit_result_0 _tmp_2415;
    int32_t _handle__error__result_2416;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2415
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1215, _M0L8filenameS1184, _M0L5indexS1186, _M0L13handle__startS1182, _M0L14handle__resultS1187, _M0L17error__to__stringS1194);
    if (_tmp_2415.tag) {
      int32_t const _M0L5_2aokS2354 = _tmp_2415.data.ok;
      _handle__error__result_2416 = _M0L5_2aokS2354;
    } else {
      void* const _M0L6_2aerrS2355 = _tmp_2415.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1194);
      moonbit_decref_cycle_free(_M0L13handle__startS1182);
      _M0L11_2atry__errS1209 = _M0L6_2aerrS2355;
      goto join_1208;
    }
    if (_handle__error__result_2416) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1194);
      moonbit_decref_cycle_free(_M0L13handle__startS1182);
      _M0L6_2atmpS2347 = 1;
    } else {
      struct moonbit_result_0 _tmp_2417;
      int32_t _handle__error__result_2418;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2417
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1215, _M0L8filenameS1184, _M0L5indexS1186, _M0L13handle__startS1182, _M0L14handle__resultS1187, _M0L17error__to__stringS1194);
      if (_tmp_2417.tag) {
        int32_t const _M0L5_2aokS2352 = _tmp_2417.data.ok;
        _handle__error__result_2418 = _M0L5_2aokS2352;
      } else {
        void* const _M0L6_2aerrS2353 = _tmp_2417.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1194);
        moonbit_decref_cycle_free(_M0L13handle__startS1182);
        _M0L11_2atry__errS1209 = _M0L6_2aerrS2353;
        goto join_1208;
      }
      if (_handle__error__result_2418) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1194);
        moonbit_decref_cycle_free(_M0L13handle__startS1182);
        _M0L6_2atmpS2347 = 1;
      } else {
        struct moonbit_result_0 _tmp_2419;
        int32_t _handle__error__result_2420;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2419
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1215, _M0L8filenameS1184, _M0L5indexS1186, _M0L13handle__startS1182, _M0L14handle__resultS1187, _M0L17error__to__stringS1194);
        if (_tmp_2419.tag) {
          int32_t const _M0L5_2aokS2350 = _tmp_2419.data.ok;
          _handle__error__result_2420 = _M0L5_2aokS2350;
        } else {
          void* const _M0L6_2aerrS2351 = _tmp_2419.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1194);
          moonbit_decref_cycle_free(_M0L13handle__startS1182);
          _M0L11_2atry__errS1209 = _M0L6_2aerrS2351;
          goto join_1208;
        }
        if (_handle__error__result_2420) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1194);
          moonbit_decref_cycle_free(_M0L13handle__startS1182);
          _M0L6_2atmpS2347 = 1;
        } else {
          struct moonbit_result_0 _tmp_2421;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2421
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1215, _M0L8filenameS1184, _M0L5indexS1186, _M0L13handle__startS1182, _M0L14handle__resultS1187, _M0L17error__to__stringS1194);
          moonbit_decref_cycle_free(_M0L13handle__startS1182);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1194);
          if (_tmp_2421.tag) {
            int32_t const _M0L5_2aokS2348 = _tmp_2421.data.ok;
            _M0L6_2atmpS2347 = _M0L5_2aokS2348;
          } else {
            void* const _M0L6_2aerrS2349 = _tmp_2421.data.err;
            _M0L11_2atry__errS1209 = _M0L6_2aerrS2349;
            goto join_1208;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2347) {
    void* _M0L133RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2358 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L133RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2358)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L133RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2358)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1209
    = _M0L133RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2358;
    goto join_1208;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1187);
  }
  goto joinlet_2412;
  join_1208:;
  _M0L3errS1210 = _M0L11_2atry__errS1209;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1213
  = (struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1210;
  _M0L7_2anameS1214 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1213->$0;
  _M0L6_2acntS2383
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1213));
  if (_M0L6_2acntS2383 > 1) {
    int32_t _M0L11_2anew__cntS2384 = _M0L6_2acntS2383 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1213), _M0L11_2anew__cntS2384);
    moonbit_incref_cycle_free(_M0L7_2anameS1214);
  } else if (_M0L6_2acntS2383 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1213);
  }
  _M0L4nameS1212 = _M0L7_2anameS1214;
  goto join_1211;
  goto joinlet_2422;
  join_1211:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1187(_M0L14handle__resultS1187, _M0L4nameS1212, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1187);
  moonbit_decref_cycle_free(_M0L4nameS1212);
  joinlet_2422:;
  joinlet_2412:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1194(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2346,
  void* _M0L3errS1195
) {
  void* _M0L1eS1197;
  moonbit_string_t _M0L1eS1199;
  moonbit_string_t _result_2425;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1195)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1200 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1195;
      moonbit_string_t _M0L4_2aeS1201 = _M0L10_2aFailureS1200->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1201);
      _M0L1eS1199 = _M0L4_2aeS1201;
      goto join_1198;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1202 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1195;
      moonbit_string_t _M0L4_2aeS1203 = _M0L15_2aInspectErrorS1202->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1203);
      _M0L1eS1199 = _M0L4_2aeS1203;
      goto join_1198;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1204 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1195;
      moonbit_string_t _M0L4_2aeS1205 = _M0L16_2aSnapshotErrorS1204->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1205);
      _M0L1eS1199 = _M0L4_2aeS1205;
      goto join_1198;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1206 =
        (struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1195;
      moonbit_string_t _M0L4_2aeS1207 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1206->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1207);
      _M0L1eS1199 = _M0L4_2aeS1207;
      goto join_1198;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1195);
      _M0L1eS1197 = _M0L3errS1195;
      goto join_1196;
      break;
    }
  }
  join_1198:;
  return _M0L1eS1199;
  join_1196:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _result_2425 = _M0FP15Error10to__string(_M0L1eS1197);
  moonbit_decref_cycle_free(_M0L1eS1197);
  return _result_2425;
}

int32_t _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1187(
  struct _M0TWssbEu* _M0L6_2aenvS2343,
  moonbit_string_t _M0L10__testnameS1188,
  moonbit_string_t _M0L7messageS1189,
  int32_t _M0L7skippedS1190
) {
  struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1187* _M0L14_2acasted__envS2344;
  moonbit_string_t _M0L8filenameS1184;
  int32_t _M0L5indexS1186;
  moonbit_string_t _M0L10file__nameS1191;
  moonbit_string_t _M0L7messageS1192;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1193;
  moonbit_string_t _M0L6_2atmpS2345;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2344
  = (struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1187*)_M0L6_2aenvS2343;
  _M0L8filenameS1184 = _M0L14_2acasted__envS2344->$1;
  _M0L5indexS1186 = _M0L14_2acasted__envS2344->$0;
  if (!_M0L7skippedS1190 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1191
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1184, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1192
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1189, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1193
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1193, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1193, _M0L10file__nameS1191);
  moonbit_decref_cycle_free(_M0L10file__nameS1191);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1193, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1193, _M0L5indexS1186);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1193, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1193, _M0L7messageS1192);
  moonbit_decref_cycle_free(_M0L7messageS1192);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1193, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2345
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1193);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1193);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2345);
  moonbit_decref_cycle_free(_M0L6_2atmpS2345);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1182(
  struct _M0TWEu* _M0L6_2aenvS2340
) {
  struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1182* _M0L14_2acasted__envS2341;
  moonbit_string_t _M0L8filenameS1184;
  int32_t _M0L5indexS1186;
  moonbit_string_t _M0L10file__nameS1183;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1185;
  moonbit_string_t _M0L6_2atmpS2342;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2341
  = (struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fmetaplasticity__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1182*)_M0L6_2aenvS2340;
  _M0L8filenameS1184 = _M0L14_2acasted__envS2341->$1;
  _M0L5indexS1186 = _M0L14_2acasted__envS2341->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1183
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1184, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1185
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1185, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1185, _M0L10file__nameS1183);
  moonbit_decref_cycle_free(_M0L10file__nameS1183);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1185, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1185, _M0L5indexS1186);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1185, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2342
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1185);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1185);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2342);
  moonbit_decref_cycle_free(_M0L6_2atmpS2342);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1152;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1159;
  struct _M0TUsiE** _M0L6_2atmpS2339;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1166;
  moonbit_string_t* _M0L9cli__argsS1167;
  moonbit_string_t _M0L6_2atmpS2338;
  moonbit_string_t _M0L6_2atmpS2337;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1168;
  int32_t _M0L7_2abindS1169;
  moonbit_string_t* _M0L7_2abindS1170;
  int32_t _M0L6_2acntS2385;
  int32_t _M0L2__S1171;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1152 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1159 = 0;
  _M0L6_2atmpS2339 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1166
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1166)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1166->$0 = _M0L6_2atmpS2339;
  _M0L16file__and__indexS1166->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1167
  = _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1167)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2338 = (moonbit_string_t)_M0L9cli__argsS1167[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2338);
  moonbit_decref_cycle_free(_M0L9cli__argsS1167);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2337
  = _M0MP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2338);
  moonbit_decref_cycle_free(_M0L6_2atmpS2338);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1168
  = _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1159(_M0L51moonbit__test__driver__internal__split__mbt__stringS1159, _M0L6_2atmpS2337, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2337);
  _M0L7_2abindS1169 = _M0L10test__argsS1168->$1;
  _M0L7_2abindS1170 = _M0L10test__argsS1168->$0;
  _M0L6_2acntS2385
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1168));
  if (_M0L6_2acntS2385 > 1) {
    int32_t _M0L11_2anew__cntS2386 = _M0L6_2acntS2385 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1168), _M0L11_2anew__cntS2386);
    moonbit_incref_cycle_free(_M0L7_2abindS1170);
  } else if (_M0L6_2acntS2385 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1168);
  }
  _M0L2__S1171 = 0;
  while (1) {
    if (_M0L2__S1171 < _M0L7_2abindS1169) {
      moonbit_string_t _M0L3argS1172 =
        (moonbit_string_t)_M0L7_2abindS1170[_M0L2__S1171];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1173;
      moonbit_string_t _M0L4fileS1174;
      moonbit_string_t _M0L5rangeS1175;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1176;
      moonbit_string_t _M0L6_2atmpS2335;
      int32_t _M0L5startS1177;
      moonbit_string_t _M0L6_2atmpS2334;
      int32_t _M0L3endS1178;
      int32_t _M0L1iS1179;
      int32_t _M0L6_2atmpS2336;
      moonbit_incref_cycle_free(_M0L3argS1172);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1173
      = _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1159(_M0L51moonbit__test__driver__internal__split__mbt__stringS1159, _M0L3argS1172, 58);
      moonbit_decref_cycle_free(_M0L3argS1172);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1174
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1173, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1175
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1173, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1173);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1176
      = _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1159(_M0L51moonbit__test__driver__internal__split__mbt__stringS1159, _M0L5rangeS1175, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1175);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2335
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1176, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1177
      = _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1152(_M0L45moonbit__test__driver__internal__parse__int__S1152, _M0L6_2atmpS2335);
      moonbit_decref_cycle_free(_M0L6_2atmpS2335);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2334
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1176, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1176);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1178
      = _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1152(_M0L45moonbit__test__driver__internal__parse__int__S1152, _M0L6_2atmpS2334);
      moonbit_decref_cycle_free(_M0L6_2atmpS2334);
      _M0L1iS1179 = _M0L5startS1177;
      while (1) {
        if (_M0L1iS1179 < _M0L3endS1178) {
          struct _M0TUsiE* _M0L8_2atupleS2332;
          int32_t _M0L6_2atmpS2333;
          moonbit_incref_cycle_free(_M0L4fileS1174);
          _M0L8_2atupleS2332
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS2332)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS2332->$0 = _M0L4fileS1174;
          _M0L8_2atupleS2332->$1 = _M0L1iS1179;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1166, _M0L8_2atupleS2332);
          _M0L6_2atmpS2333 = _M0L1iS1179 + 1;
          _M0L1iS1179 = _M0L6_2atmpS2333;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1174);
        }
        break;
      }
      _M0L6_2atmpS2336 = _M0L2__S1171 + 1;
      _M0L2__S1171 = _M0L6_2atmpS2336;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1170);
    }
    break;
  }
  return _M0L16file__and__indexS1166;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1159(
  int32_t _M0L6_2aenvS2313,
  moonbit_string_t _M0L1sS1160,
  int32_t _M0L3sepS1161
) {
  moonbit_string_t* _M0L6_2atmpS2331;
  struct _M0TPB5ArrayGsE* _M0L3resS1162;
  struct _M0TPB8MutLocalGiE* _M0L1iS1163;
  struct _M0TPB8MutLocalGiE* _M0L5startS1164;
  int32_t _M0L3valS2326;
  int32_t _M0L6_2atmpS2327;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2331 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1162
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1162)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1162->$0 = _M0L6_2atmpS2331;
  _M0L3resS1162->$1 = 0;
  _M0L1iS1163
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1163)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1163->$0 = 0;
  _M0L5startS1164
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1164)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1164->$0 = 0;
  while (1) {
    int32_t _M0L3valS2314 = _M0L1iS1163->$0;
    int32_t _M0L6_2atmpS2315 = Moonbit_array_length(_M0L1sS1160);
    if (_M0L3valS2314 < _M0L6_2atmpS2315) {
      int32_t _M0L3valS2318 = _M0L1iS1163->$0;
      int32_t _M0L6_2atmpS2317;
      int32_t _M0L6_2atmpS2316;
      int32_t _M0L3valS2325;
      int32_t _M0L6_2atmpS2324;
      if (
        _M0L3valS2318 < 0
        || _M0L3valS2318 >= Moonbit_array_length(_M0L1sS1160)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2317 = _M0L1sS1160[_M0L3valS2318];
      _M0L6_2atmpS2316 = _M0L6_2atmpS2317;
      if (_M0L6_2atmpS2316 == _M0L3sepS1161) {
        int32_t _M0L3valS2320 = _M0L5startS1164->$0;
        int32_t _M0L3valS2321 = _M0L1iS1163->$0;
        moonbit_string_t _M0L6_2atmpS2319;
        int32_t _M0L3valS2323;
        int32_t _M0L6_2atmpS2322;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS2319
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1160, _M0L3valS2320, _M0L3valS2321);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1162, _M0L6_2atmpS2319);
        _M0L3valS2323 = _M0L1iS1163->$0;
        _M0L6_2atmpS2322 = _M0L3valS2323 + 1;
        _M0L5startS1164->$0 = _M0L6_2atmpS2322;
      }
      _M0L3valS2325 = _M0L1iS1163->$0;
      _M0L6_2atmpS2324 = _M0L3valS2325 + 1;
      _M0L1iS1163->$0 = _M0L6_2atmpS2324;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1163);
    }
    break;
  }
  _M0L3valS2326 = _M0L5startS1164->$0;
  _M0L6_2atmpS2327 = Moonbit_array_length(_M0L1sS1160);
  if (_M0L3valS2326 < _M0L6_2atmpS2327) {
    int32_t _M0L3valS2329 = _M0L5startS1164->$0;
    int32_t _M0L6_2atmpS2330;
    moonbit_string_t _M0L6_2atmpS2328;
    moonbit_decref_cycle_free(_M0L5startS1164);
    _M0L6_2atmpS2330 = Moonbit_array_length(_M0L1sS1160);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS2328
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1160, _M0L3valS2329, _M0L6_2atmpS2330);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1162, _M0L6_2atmpS2328);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1164);
  }
  return _M0L3resS1162;
}

int32_t _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1152(
  int32_t _M0L6_2aenvS2306,
  moonbit_string_t _M0L1sS1153
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1154;
  int32_t _M0L3lenS1155;
  int32_t _M0L7_2abindS1156;
  int32_t _M0L1iS1157;
  int32_t _result_2430;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1154
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1154)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1154->$0 = 0;
  _M0L3lenS1155 = Moonbit_array_length(_M0L1sS1153);
  _M0L7_2abindS1156 = 0;
  _M0L1iS1157 = _M0L7_2abindS1156;
  while (1) {
    if (_M0L1iS1157 < _M0L3lenS1155) {
      int32_t _M0L3valS2311 = _M0L3resS1154->$0;
      int32_t _M0L6_2atmpS2308 = _M0L3valS2311 * 10;
      int32_t _M0L6_2atmpS2310;
      int32_t _M0L6_2atmpS2309;
      int32_t _M0L6_2atmpS2307;
      int32_t _M0L6_2atmpS2312;
      if (
        _M0L1iS1157 < 0 || _M0L1iS1157 >= Moonbit_array_length(_M0L1sS1153)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2310 = _M0L1sS1153[_M0L1iS1157];
      _M0L6_2atmpS2309 = _M0L6_2atmpS2310 - 48;
      _M0L6_2atmpS2307 = _M0L6_2atmpS2308 + _M0L6_2atmpS2309;
      _M0L3resS1154->$0 = _M0L6_2atmpS2307;
      _M0L6_2atmpS2312 = _M0L1iS1157 + 1;
      _M0L1iS1157 = _M0L6_2atmpS2312;
      continue;
    }
    break;
  }
  _result_2430 = _M0L3resS1154->$0;
  moonbit_decref_cycle_free(_M0L3resS1154);
  return _result_2430;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1151
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1151);
  return _M0L4selfS1151;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1121,
  moonbit_string_t _M0L12_2adiscard__S1122,
  int32_t _M0L12_2adiscard__S1123,
  struct _M0TWEu* _M0L12_2adiscard__S1124,
  struct _M0TWssbEu* _M0L12_2adiscard__S1125,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1126
) {
  struct moonbit_result_0 _result_2431;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _result_2431.tag = 1;
  _result_2431.data.ok = 0;
  return _result_2431;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1127,
  moonbit_string_t _M0L12_2adiscard__S1128,
  int32_t _M0L12_2adiscard__S1129,
  struct _M0TWEu* _M0L12_2adiscard__S1130,
  struct _M0TWssbEu* _M0L12_2adiscard__S1131,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1132
) {
  struct moonbit_result_0 _result_2432;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _result_2432.tag = 1;
  _result_2432.data.ok = 0;
  return _result_2432;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1133,
  moonbit_string_t _M0L12_2adiscard__S1134,
  int32_t _M0L12_2adiscard__S1135,
  struct _M0TWEu* _M0L12_2adiscard__S1136,
  struct _M0TWssbEu* _M0L12_2adiscard__S1137,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1138
) {
  struct moonbit_result_0 _result_2433;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _result_2433.tag = 1;
  _result_2433.data.ok = 0;
  return _result_2433;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1139,
  moonbit_string_t _M0L12_2adiscard__S1140,
  int32_t _M0L12_2adiscard__S1141,
  struct _M0TWEu* _M0L12_2adiscard__S1142,
  struct _M0TWssbEu* _M0L12_2adiscard__S1143,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1144
) {
  struct moonbit_result_0 _result_2434;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _result_2434.tag = 1;
  _result_2434.data.ok = 0;
  return _result_2434;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1145,
  moonbit_string_t _M0L12_2adiscard__S1146,
  int32_t _M0L12_2adiscard__S1147,
  struct _M0TWEu* _M0L12_2adiscard__S1148,
  struct _M0TWssbEu* _M0L12_2adiscard__S1149,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1150
) {
  struct moonbit_result_0 _result_2435;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _result_2435.tag = 1;
  _result_2435.data.ok = 0;
  return _result_2435;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1120
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse6random(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1062,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1063,
  moonbit_string_t _M0L3symS1068,
  float _M0L2muS1064,
  float _M0L5sigmaS1065,
  float _M0L1pS1066,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1067
) {
  int32_t _M0L1nS2304;
  int32_t _M0L1nS2305;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1061;
  float* _M0L6_2atmpS2303;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2294;
  float* _M0L6_2atmpS2302;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2295;
  float* _M0L6_2atmpS2301;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2296;
  int32_t* _M0L6_2atmpS2300;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2297;
  float* _M0L6_2atmpS2299;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2298;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _block_2436;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS2304 = _M0L3preS1062->$2;
  _M0L1nS2305 = _M0L4postS1063->$2;
  #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6matrixS1061
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS2304, _M0L1nS2305, _M0L2muS1064, _M0L5sigmaS1065, _M0L1pS1066, _M0L3rngS1067);
  _M0L6_2atmpS2303 = moonbit_empty_float_array;
  _M0L6_2atmpS2294
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2294)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2294->$0 = _M0L6_2atmpS2303;
  _M0L6_2atmpS2294->$1 = 0;
  _M0L6_2atmpS2302 = moonbit_empty_float_array;
  _M0L6_2atmpS2295
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2295)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2295->$0 = _M0L6_2atmpS2302;
  _M0L6_2atmpS2295->$1 = 0;
  _M0L6_2atmpS2301 = moonbit_empty_float_array;
  _M0L6_2atmpS2296
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2296)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2296->$0 = _M0L6_2atmpS2301;
  _M0L6_2atmpS2296->$1 = 0;
  _M0L6_2atmpS2300 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS2297
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2297)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS2297->$0 = _M0L6_2atmpS2300;
  _M0L6_2atmpS2297->$1 = 0;
  _M0L6_2atmpS2299 = moonbit_empty_float_array;
  _M0L6_2atmpS2298
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2298)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2298->$0 = _M0L6_2atmpS2299;
  _M0L6_2atmpS2298->$1 = 0;
  moonbit_incref_cycle_free(_M0L3preS1062);
  moonbit_incref_cycle_free(_M0L4postS1063);
  moonbit_incref_cycle_free(_M0L3symS1068);
  _block_2436
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse));
  Moonbit_object_header(_block_2436)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _block_2436->$0 = _M0L3preS1062;
  _block_2436->$1 = _M0L4postS1063;
  _block_2436->$2 = _M0L3symS1068;
  _block_2436->$3 = (moonbit_string_t)moonbit_string_literal_0.data;
  _block_2436->$4 = _M0L6matrixS1061;
  _block_2436->$5 = _M0L6_2atmpS2294;
  _block_2436->$6 = _M0L6_2atmpS2295;
  _block_2436->$7 = _M0L6_2atmpS2296;
  _block_2436->$8 = _M0L6_2atmpS2297;
  _block_2436->$9 = _M0L6_2atmpS2298;
  return _block_2436;
}

int32_t _M0FP26RiantR8snn__mbt20metaplasticity__step(
  struct _M0TP26RiantR8snn__mbt20SynapseNormalization* _M0L4normS1015
) {
  int32_t _M0L7n__postS1014;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE* _M0L7targetsS2293;
  int32_t _M0L10n__targetsS1016;
  int32_t _M0L7_2abindS1017;
  int32_t _M0L1iS1018;
  int32_t _M0L7_2abindS1020;
  int32_t _M0L1tS1021;
  struct _M0TP26RiantR8snn__mbt12AdditiveNorm* _M0L1pS1033;
  struct _M0TP26RiantR8snn__mbt18MultiplicativeNorm* _M0L1pS1038;
  void* _M0L7_2abindS1042;
  int32_t _M0L7_2abindS1039;
  int32_t _M0L1iS1040;
  int32_t _M0L7_2abindS1034;
  int32_t _M0L1iS1035;
  int32_t _M0L7_2abindS1047;
  int32_t _M0L1tS1048;
  #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
  _M0L7n__postS1014 = _M0L4normS1015->$1;
  _M0L7targetsS2293 = _M0L4normS1015->$5;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
  _M0L10n__targetsS1016
  = _M0MPC15array5Array6lengthGRP26RiantR8snn__mbt13SynapseTargetE(_M0L7targetsS2293);
  _M0L7_2abindS1017 = 0;
  _M0L1iS1018 = _M0L7_2abindS1017;
  while (1) {
    if (_M0L1iS1018 < _M0L7n__postS1014) {
      struct _M0TPB5ArrayGfE* _M0L2w1S2235 = _M0L4normS1015->$3;
      int32_t _M0L6_2atmpS2236;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
      _M0MPC15array5Array3setGfE(_M0L2w1S2235, _M0L1iS1018, 0x0p+0f);
      _M0L6_2atmpS2236 = _M0L1iS1018 + 1;
      _M0L1iS1018 = _M0L6_2atmpS2236;
      continue;
    }
    break;
  }
  _M0L7_2abindS1020 = 0;
  _M0L1tS1021 = _M0L7_2abindS1020;
  while (1) {
    if (_M0L1tS1021 < _M0L10n__targetsS1016) {
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE* _M0L7targetsS2251 =
        _M0L4normS1015->$5;
      struct _M0TP26RiantR8snn__mbt13SynapseTarget* _M0L6_2atmpS2250;
      struct _M0TPB5ArrayGfE* _M0L4valsS1022;
      int32_t _M0L6_2acntS2387;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE* _M0L7targetsS2249;
      struct _M0TP26RiantR8snn__mbt13SynapseTarget* _M0L6_2atmpS2248;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS1023;
      int32_t _M0L6_2acntS2390;
      int32_t _M0L7_2abindS1024;
      int32_t _M0L1iS1025;
      int32_t _M0L6_2atmpS2252;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
      _M0L6_2atmpS2250
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt13SynapseTargetE(_M0L7targetsS2251, _M0L1tS1021);
      _M0L4valsS1022 = _M0L6_2atmpS2250->$1;
      _M0L6_2acntS2387
      = Moonbit_rc_count(Moonbit_object_header(_M0L6_2atmpS2250));
      if (_M0L6_2acntS2387 > 1) {
        int32_t _M0L11_2anew__cntS2389 = _M0L6_2acntS2387 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6_2atmpS2250), _M0L11_2anew__cntS2389);
        moonbit_incref_cycle_free(_M0L4valsS1022);
      } else if (_M0L6_2acntS2387 == 1) {
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS2388 = _M0L6_2atmpS2250->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS2388);
        #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
        moonbit_free(_M0L6_2atmpS2250);
      }
      _M0L7targetsS2249 = _M0L4normS1015->$5;
      #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
      _M0L6_2atmpS2248
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt13SynapseTargetE(_M0L7targetsS2249, _M0L1tS1021);
      _M0L6rowptrS1023 = _M0L6_2atmpS2248->$2;
      _M0L6_2acntS2390
      = Moonbit_rc_count(Moonbit_object_header(_M0L6_2atmpS2248));
      if (_M0L6_2acntS2390 > 1) {
        int32_t _M0L11_2anew__cntS2392 = _M0L6_2acntS2390 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6_2atmpS2248), _M0L11_2anew__cntS2392);
        moonbit_incref_cycle_free(_M0L6rowptrS1023);
      } else if (_M0L6_2acntS2390 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS2391 = _M0L6_2atmpS2248->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS2391);
        #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
        moonbit_free(_M0L6_2atmpS2248);
      }
      _M0L7_2abindS1024 = 0;
      _M0L1iS1025 = _M0L7_2abindS1024;
      while (1) {
        if (_M0L1iS1025 < _M0L7n__postS1014) {
          int32_t _M0L5startS1026;
          int32_t _M0L6_2atmpS2246;
          int32_t _M0L3endS1027;
          struct _M0TPB8MutLocalGiE* _M0L1jS1028;
          int32_t _M0L6_2atmpS2247;
          #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
          _M0L5startS1026
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS1023, _M0L1iS1025);
          _M0L6_2atmpS2246 = _M0L1iS1025 + 1;
          #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
          _M0L3endS1027
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS1023, _M0L6_2atmpS2246);
          _M0L1jS1028
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1jS1028)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1jS1028->$0 = _M0L5startS1026;
          while (1) {
            int32_t _M0L3valS2237 = _M0L1jS1028->$0;
            if (_M0L3valS2237 < _M0L3endS1027) {
              struct _M0TPB5ArrayGfE* _M0L2w1S2238 = _M0L4normS1015->$3;
              struct _M0TPB5ArrayGfE* _M0L2w1S2243 = _M0L4normS1015->$3;
              float _M0L6_2atmpS2240;
              int32_t _M0L3valS2242;
              float _M0L6_2atmpS2241;
              float _M0L6_2atmpS2239;
              int32_t _M0L3valS2245;
              int32_t _M0L6_2atmpS2244;
              #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
              _M0L6_2atmpS2240
              = _M0MPC15array5Array2atGfE(_M0L2w1S2243, _M0L1iS1025);
              _M0L3valS2242 = _M0L1jS1028->$0;
              #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
              _M0L6_2atmpS2241
              = _M0MPC15array5Array2atGfE(_M0L4valsS1022, _M0L3valS2242);
              _M0L6_2atmpS2239 = _M0L6_2atmpS2240 + _M0L6_2atmpS2241;
              #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
              _M0MPC15array5Array3setGfE(_M0L2w1S2238, _M0L1iS1025, _M0L6_2atmpS2239);
              _M0L3valS2245 = _M0L1jS1028->$0;
              _M0L6_2atmpS2244 = _M0L3valS2245 + 1;
              _M0L1jS1028->$0 = _M0L6_2atmpS2244;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1jS1028);
            }
            break;
          }
          _M0L6_2atmpS2247 = _M0L1iS1025 + 1;
          _M0L1iS1025 = _M0L6_2atmpS2247;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L6rowptrS1023);
          moonbit_decref_cycle_free(_M0L4valsS1022);
        }
        break;
      }
      _M0L6_2atmpS2252 = _M0L1tS1021 + 1;
      _M0L1tS1021 = _M0L6_2atmpS2252;
      continue;
    }
    break;
  }
  _M0L7_2abindS1042 = _M0L4normS1015->$0;
  switch (Moonbit_object_tag(_M0L7_2abindS1042)) {
    case 0: {
      struct _M0DTP26RiantR8snn__mbt9NormParam16Multiplicative__* _M0L19_2aMultiplicative__S1043 =
        (struct _M0DTP26RiantR8snn__mbt9NormParam16Multiplicative__*)_M0L7_2abindS1042;
      struct _M0TP26RiantR8snn__mbt18MultiplicativeNorm* _M0L4_2apS1044 =
        _M0L19_2aMultiplicative__S1043->$0;
      moonbit_incref_cycle_free(_M0L4_2apS1044);
      _M0L1pS1038 = _M0L4_2apS1044;
      goto join_1037;
      break;
    }
    default: {
      struct _M0DTP26RiantR8snn__mbt9NormParam10Additive__* _M0L13_2aAdditive__S1045 =
        (struct _M0DTP26RiantR8snn__mbt9NormParam10Additive__*)_M0L7_2abindS1042;
      struct _M0TP26RiantR8snn__mbt12AdditiveNorm* _M0L4_2apS1046 =
        _M0L13_2aAdditive__S1045->$0;
      moonbit_incref_cycle_free(_M0L4_2apS1046);
      _M0L1pS1033 = _M0L4_2apS1046;
      goto join_1032;
      break;
    }
  }
  goto joinlet_2442;
  join_1037:;
  moonbit_decref_cycle_free(_M0L1pS1038);
  _M0L7_2abindS1039 = 0;
  _M0L1iS1040 = _M0L7_2abindS1039;
  while (1) {
    if (_M0L1iS1040 < _M0L7n__postS1014) {
      struct _M0TPB5ArrayGfE* _M0L2w1S2261 = _M0L4normS1015->$3;
      float _M0L6_2atmpS2260;
      int32_t _M0L6_2atmpS2272;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
      _M0L6_2atmpS2260 = _M0MPC15array5Array2atGfE(_M0L2w1S2261, _M0L1iS1040);
      if (_M0L6_2atmpS2260 > 0x0p+0f) {
        struct _M0TPB5ArrayGfE* _M0L2muS2262 = _M0L4normS1015->$4;
        struct _M0TPB5ArrayGfE* _M0L2w0S2270 = _M0L4normS1015->$2;
        float _M0L6_2atmpS2267;
        struct _M0TPB5ArrayGfE* _M0L2w1S2269;
        float _M0L6_2atmpS2268;
        float _M0L6_2atmpS2264;
        struct _M0TPB5ArrayGfE* _M0L2w1S2266;
        float _M0L6_2atmpS2265;
        float _M0L6_2atmpS2263;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
        _M0L6_2atmpS2267
        = _M0MPC15array5Array2atGfE(_M0L2w0S2270, _M0L1iS1040);
        _M0L2w1S2269 = _M0L4normS1015->$3;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
        _M0L6_2atmpS2268
        = _M0MPC15array5Array2atGfE(_M0L2w1S2269, _M0L1iS1040);
        _M0L6_2atmpS2264 = _M0L6_2atmpS2267 - _M0L6_2atmpS2268;
        _M0L2w1S2266 = _M0L4normS1015->$3;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
        _M0L6_2atmpS2265
        = _M0MPC15array5Array2atGfE(_M0L2w1S2266, _M0L1iS1040);
        _M0L6_2atmpS2263 = _M0L6_2atmpS2264 / _M0L6_2atmpS2265;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
        _M0MPC15array5Array3setGfE(_M0L2muS2262, _M0L1iS1040, _M0L6_2atmpS2263);
      } else {
        struct _M0TPB5ArrayGfE* _M0L2muS2271 = _M0L4normS1015->$4;
        #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
        _M0MPC15array5Array3setGfE(_M0L2muS2271, _M0L1iS1040, 0x0p+0f);
      }
      _M0L6_2atmpS2272 = _M0L1iS1040 + 1;
      _M0L1iS1040 = _M0L6_2atmpS2272;
      continue;
    }
    break;
  }
  joinlet_2442:;
  goto joinlet_2441;
  join_1032:;
  moonbit_decref_cycle_free(_M0L1pS1033);
  _M0L7_2abindS1034 = 0;
  _M0L1iS1035 = _M0L7_2abindS1034;
  while (1) {
    if (_M0L1iS1035 < _M0L7n__postS1014) {
      struct _M0TPB5ArrayGfE* _M0L2muS2253 = _M0L4normS1015->$4;
      struct _M0TPB5ArrayGfE* _M0L2w0S2258 = _M0L4normS1015->$2;
      float _M0L6_2atmpS2255;
      struct _M0TPB5ArrayGfE* _M0L2w1S2257;
      float _M0L6_2atmpS2256;
      float _M0L6_2atmpS2254;
      int32_t _M0L6_2atmpS2259;
      #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
      _M0L6_2atmpS2255 = _M0MPC15array5Array2atGfE(_M0L2w0S2258, _M0L1iS1035);
      _M0L2w1S2257 = _M0L4normS1015->$3;
      #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
      _M0L6_2atmpS2256 = _M0MPC15array5Array2atGfE(_M0L2w1S2257, _M0L1iS1035);
      _M0L6_2atmpS2254 = _M0L6_2atmpS2255 - _M0L6_2atmpS2256;
      #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
      _M0MPC15array5Array3setGfE(_M0L2muS2253, _M0L1iS1035, _M0L6_2atmpS2254);
      _M0L6_2atmpS2259 = _M0L1iS1035 + 1;
      _M0L1iS1035 = _M0L6_2atmpS2259;
      continue;
    }
    break;
  }
  joinlet_2441:;
  _M0L7_2abindS1047 = 0;
  _M0L1tS1048 = _M0L7_2abindS1047;
  while (1) {
    if (_M0L1tS1048 < _M0L10n__targetsS1016) {
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE* _M0L7targetsS2291 =
        _M0L4normS1015->$5;
      struct _M0TP26RiantR8snn__mbt13SynapseTarget* _M0L6_2atmpS2290;
      struct _M0TPB5ArrayGfE* _M0L4valsS1049;
      int32_t _M0L6_2acntS2393;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE* _M0L7targetsS2289;
      struct _M0TP26RiantR8snn__mbt13SynapseTarget* _M0L6_2atmpS2288;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS1050;
      int32_t _M0L6_2acntS2396;
      int32_t _M0L7_2abindS1051;
      int32_t _M0L1iS1052;
      int32_t _M0L6_2atmpS2292;
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
      _M0L6_2atmpS2290
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt13SynapseTargetE(_M0L7targetsS2291, _M0L1tS1048);
      _M0L4valsS1049 = _M0L6_2atmpS2290->$1;
      _M0L6_2acntS2393
      = Moonbit_rc_count(Moonbit_object_header(_M0L6_2atmpS2290));
      if (_M0L6_2acntS2393 > 1) {
        int32_t _M0L11_2anew__cntS2395 = _M0L6_2acntS2393 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6_2atmpS2290), _M0L11_2anew__cntS2395);
        moonbit_incref_cycle_free(_M0L4valsS1049);
      } else if (_M0L6_2acntS2393 == 1) {
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS2394 = _M0L6_2atmpS2290->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS2394);
        #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
        moonbit_free(_M0L6_2atmpS2290);
      }
      _M0L7targetsS2289 = _M0L4normS1015->$5;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
      _M0L6_2atmpS2288
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt13SynapseTargetE(_M0L7targetsS2289, _M0L1tS1048);
      _M0L6rowptrS1050 = _M0L6_2atmpS2288->$2;
      _M0L6_2acntS2396
      = Moonbit_rc_count(Moonbit_object_header(_M0L6_2atmpS2288));
      if (_M0L6_2acntS2396 > 1) {
        int32_t _M0L11_2anew__cntS2398 = _M0L6_2acntS2396 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6_2atmpS2288), _M0L11_2anew__cntS2398);
        moonbit_incref_cycle_free(_M0L6rowptrS1050);
      } else if (_M0L6_2acntS2396 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS2397 = _M0L6_2atmpS2288->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS2397);
        #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
        moonbit_free(_M0L6_2atmpS2288);
      }
      _M0L7_2abindS1051 = 0;
      _M0L1iS1052 = _M0L7_2abindS1051;
      while (1) {
        if (_M0L1iS1052 < _M0L7n__postS1014) {
          int32_t _M0L5startS1053;
          int32_t _M0L6_2atmpS2286;
          int32_t _M0L3endS1054;
          struct _M0TPB5ArrayGfE* _M0L2muS2285;
          float _M0L5mu__iS1055;
          struct _M0TPB8MutLocalGiE* _M0L1jS1056;
          int32_t _M0L6_2atmpS2287;
          #line 207 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
          _M0L5startS1053
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS1050, _M0L1iS1052);
          _M0L6_2atmpS2286 = _M0L1iS1052 + 1;
          #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
          _M0L3endS1054
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS1050, _M0L6_2atmpS2286);
          _M0L2muS2285 = _M0L4normS1015->$4;
          #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
          _M0L5mu__iS1055
          = _M0MPC15array5Array2atGfE(_M0L2muS2285, _M0L1iS1052);
          _M0L1jS1056
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1jS1056)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1jS1056->$0 = _M0L5startS1053;
          while (1) {
            int32_t _M0L3valS2273 = _M0L1jS1056->$0;
            if (_M0L3valS2273 < _M0L3endS1054) {
              void* _M0L7_2abindS1057 = _M0L4normS1015->$0;
              int32_t _M0L3valS2284;
              int32_t _M0L6_2atmpS2283;
              switch (Moonbit_object_tag(_M0L7_2abindS1057)) {
                case 0: {
                  int32_t _M0L3valS2278 = _M0L1jS1056->$0;
                  int32_t _M0L3valS2282 = _M0L1jS1056->$0;
                  float _M0L6_2atmpS2280;
                  float _M0L6_2atmpS2281;
                  float _M0L6_2atmpS2279;
                  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
                  _M0L6_2atmpS2280
                  = _M0MPC15array5Array2atGfE(_M0L4valsS1049, _M0L3valS2282);
                  _M0L6_2atmpS2281 = 0x1p+0f + _M0L5mu__iS1055;
                  _M0L6_2atmpS2279 = _M0L6_2atmpS2280 * _M0L6_2atmpS2281;
                  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
                  _M0MPC15array5Array3setGfE(_M0L4valsS1049, _M0L3valS2278, _M0L6_2atmpS2279);
                  break;
                }
                default: {
                  int32_t _M0L3valS2274 = _M0L1jS1056->$0;
                  int32_t _M0L3valS2277 = _M0L1jS1056->$0;
                  float _M0L6_2atmpS2276;
                  float _M0L6_2atmpS2275;
                  #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
                  _M0L6_2atmpS2276
                  = _M0MPC15array5Array2atGfE(_M0L4valsS1049, _M0L3valS2277);
                  _M0L6_2atmpS2275 = _M0L6_2atmpS2276 + _M0L5mu__iS1055;
                  #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
                  _M0MPC15array5Array3setGfE(_M0L4valsS1049, _M0L3valS2274, _M0L6_2atmpS2275);
                  break;
                }
              }
              _M0L3valS2284 = _M0L1jS1056->$0;
              _M0L6_2atmpS2283 = _M0L3valS2284 + 1;
              _M0L1jS1056->$0 = _M0L6_2atmpS2283;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1jS1056);
            }
            break;
          }
          _M0L6_2atmpS2287 = _M0L1iS1052 + 1;
          _M0L1iS1052 = _M0L6_2atmpS2287;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L6rowptrS1050);
          moonbit_decref_cycle_free(_M0L4valsS1049);
        }
        break;
      }
      _M0L6_2atmpS2292 = _M0L1tS1048 + 1;
      _M0L1tS1048 = _M0L6_2atmpS2292;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt20SynapseNormalization* _M0MP26RiantR8snn__mbt20SynapseNormalization3new(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE* _M0L7targetsS996,
  void* _M0L5paramS1013
) {
  int32_t _M0L6_2atmpS2231;
  int32_t _M0L7n__postS995;
  struct _M0TPB5ArrayGfE* _M0L2w0S997;
  int32_t _M0L7_2abindS998;
  int32_t _M0L7_2abindS999;
  int32_t _M0L1tS1000;
  struct _M0TPB5ArrayGfE* _M0L2w1S1011;
  struct _M0TPB5ArrayGfE* _M0L2muS1012;
  struct _M0TP26RiantR8snn__mbt20SynapseNormalization* _block_2451;
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
  _M0L6_2atmpS2231
  = _M0MPC15array5Array6lengthGRP26RiantR8snn__mbt13SynapseTargetE(_M0L7targetsS996);
  if (_M0L6_2atmpS2231 > 0) {
    struct _M0TP26RiantR8snn__mbt13SynapseTarget* _M0L6_2atmpS2234;
    struct _M0TPB5ArrayGiE* _M0L6rowptrS2233;
    int32_t _M0L6_2acntS2399;
    int32_t _M0L6_2atmpS2232;
    #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
    _M0L6_2atmpS2234
    = _M0MPC15array5Array2atGRP26RiantR8snn__mbt13SynapseTargetE(_M0L7targetsS996, 0);
    _M0L6rowptrS2233 = _M0L6_2atmpS2234->$2;
    _M0L6_2acntS2399
    = Moonbit_rc_count(Moonbit_object_header(_M0L6_2atmpS2234));
    if (_M0L6_2acntS2399 > 1) {
      int32_t _M0L11_2anew__cntS2401 = _M0L6_2acntS2399 - 1;
      Moonbit_set_rc_count(Moonbit_object_header(_M0L6_2atmpS2234), _M0L11_2anew__cntS2401);
      moonbit_incref_cycle_free(_M0L6rowptrS2233);
    } else if (_M0L6_2acntS2399 == 1) {
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2400 = _M0L6_2atmpS2234->$1;
      moonbit_decref_cycle_free(_M0L8_2afieldS2400);
      #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
      moonbit_free(_M0L6_2atmpS2234);
    }
    #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
    _M0L6_2atmpS2232 = _M0MPC15array5Array6lengthGiE(_M0L6rowptrS2233);
    moonbit_decref_cycle_free(_M0L6rowptrS2233);
    _M0L7n__postS995 = _M0L6_2atmpS2232 - 1;
  } else {
    _M0L7n__postS995 = 0;
  }
  #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
  _M0L2w0S997 = _M0MPC15array5Array4makeGfE(_M0L7n__postS995, 0x0p+0f);
  _M0L7_2abindS998 = 0;
  #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
  _M0L7_2abindS999
  = _M0MPC15array5Array6lengthGRP26RiantR8snn__mbt13SynapseTargetE(_M0L7targetsS996);
  _M0L1tS1000 = _M0L7_2abindS998;
  while (1) {
    if (_M0L1tS1000 < _M0L7_2abindS999) {
      struct _M0TP26RiantR8snn__mbt13SynapseTarget* _M0L6_2atmpS2229;
      struct _M0TPB5ArrayGfE* _M0L4valsS1001;
      int32_t _M0L6_2acntS2402;
      struct _M0TP26RiantR8snn__mbt13SynapseTarget* _M0L6_2atmpS2228;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS1002;
      int32_t _M0L6_2acntS2405;
      int32_t _M0L7_2abindS1003;
      int32_t _M0L1iS1004;
      int32_t _M0L6_2atmpS2230;
      #line 137 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
      _M0L6_2atmpS2229
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt13SynapseTargetE(_M0L7targetsS996, _M0L1tS1000);
      _M0L4valsS1001 = _M0L6_2atmpS2229->$1;
      _M0L6_2acntS2402
      = Moonbit_rc_count(Moonbit_object_header(_M0L6_2atmpS2229));
      if (_M0L6_2acntS2402 > 1) {
        int32_t _M0L11_2anew__cntS2404 = _M0L6_2acntS2402 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6_2atmpS2229), _M0L11_2anew__cntS2404);
        moonbit_incref_cycle_free(_M0L4valsS1001);
      } else if (_M0L6_2acntS2402 == 1) {
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS2403 = _M0L6_2atmpS2229->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS2403);
        #line 137 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
        moonbit_free(_M0L6_2atmpS2229);
      }
      #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
      _M0L6_2atmpS2228
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt13SynapseTargetE(_M0L7targetsS996, _M0L1tS1000);
      _M0L6rowptrS1002 = _M0L6_2atmpS2228->$2;
      _M0L6_2acntS2405
      = Moonbit_rc_count(Moonbit_object_header(_M0L6_2atmpS2228));
      if (_M0L6_2acntS2405 > 1) {
        int32_t _M0L11_2anew__cntS2407 = _M0L6_2acntS2405 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6_2atmpS2228), _M0L11_2anew__cntS2407);
        moonbit_incref_cycle_free(_M0L6rowptrS1002);
      } else if (_M0L6_2acntS2405 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS2406 = _M0L6_2atmpS2228->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS2406);
        #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
        moonbit_free(_M0L6_2atmpS2228);
      }
      _M0L7_2abindS1003 = 0;
      _M0L1iS1004 = _M0L7_2abindS1003;
      while (1) {
        if (_M0L1iS1004 < _M0L7n__postS995) {
          int32_t _M0L5startS1005;
          int32_t _M0L6_2atmpS2226;
          int32_t _M0L3endS1006;
          struct _M0TPB8MutLocalGiE* _M0L1jS1007;
          int32_t _M0L6_2atmpS2227;
          #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
          _M0L5startS1005
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS1002, _M0L1iS1004);
          _M0L6_2atmpS2226 = _M0L1iS1004 + 1;
          #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
          _M0L3endS1006
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS1002, _M0L6_2atmpS2226);
          _M0L1jS1007
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1jS1007)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1jS1007->$0 = _M0L5startS1005;
          while (1) {
            int32_t _M0L3valS2219 = _M0L1jS1007->$0;
            if (_M0L3valS2219 < _M0L3endS1006) {
              float _M0L6_2atmpS2221;
              int32_t _M0L3valS2223;
              float _M0L6_2atmpS2222;
              float _M0L6_2atmpS2220;
              int32_t _M0L3valS2225;
              int32_t _M0L6_2atmpS2224;
              #line 144 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
              _M0L6_2atmpS2221
              = _M0MPC15array5Array2atGfE(_M0L2w0S997, _M0L1iS1004);
              _M0L3valS2223 = _M0L1jS1007->$0;
              #line 144 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
              _M0L6_2atmpS2222
              = _M0MPC15array5Array2atGfE(_M0L4valsS1001, _M0L3valS2223);
              _M0L6_2atmpS2220 = _M0L6_2atmpS2221 + _M0L6_2atmpS2222;
              #line 144 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
              _M0MPC15array5Array3setGfE(_M0L2w0S997, _M0L1iS1004, _M0L6_2atmpS2220);
              _M0L3valS2225 = _M0L1jS1007->$0;
              _M0L6_2atmpS2224 = _M0L3valS2225 + 1;
              _M0L1jS1007->$0 = _M0L6_2atmpS2224;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1jS1007);
            }
            break;
          }
          _M0L6_2atmpS2227 = _M0L1iS1004 + 1;
          _M0L1iS1004 = _M0L6_2atmpS2227;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L6rowptrS1002);
          moonbit_decref_cycle_free(_M0L4valsS1001);
        }
        break;
      }
      _M0L6_2atmpS2230 = _M0L1tS1000 + 1;
      _M0L1tS1000 = _M0L6_2atmpS2230;
      continue;
    }
    break;
  }
  #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
  _M0L2w1S1011 = _M0MPC15array5Array4makeGfE(_M0L7n__postS995, 0x0p+0f);
  #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
  _M0L2muS1012 = _M0MPC15array5Array4makeGfE(_M0L7n__postS995, 0x0p+0f);
  moonbit_incref_cycle_free(_M0L5paramS1013);
  moonbit_incref_cycle_free(_M0L7targetsS996);
  _block_2451
  = (struct _M0TP26RiantR8snn__mbt20SynapseNormalization*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt20SynapseNormalization));
  Moonbit_object_header(_block_2451)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _block_2451->$0 = _M0L5paramS1013;
  _block_2451->$1 = _M0L7n__postS995;
  _block_2451->$2 = _M0L2w0S997;
  _block_2451->$3 = _M0L2w1S1011;
  _block_2451->$4 = _M0L2muS1012;
  _block_2451->$5 = _M0L7targetsS996;
  return _block_2451;
}

struct _M0TP26RiantR8snn__mbt18MultiplicativeNorm* _M0MP26RiantR8snn__mbt18MultiplicativeNorm3new(
  float _M0L3tauS994
) {
  struct _M0TP26RiantR8snn__mbt18MultiplicativeNorm* _block_2452;
  #line 42 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\metaplasticity.mbt"
  _block_2452
  = (struct _M0TP26RiantR8snn__mbt18MultiplicativeNorm*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt18MultiplicativeNorm));
  Moonbit_object_header(_block_2452)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2452->$0 = _M0L3tauS994;
  return _block_2452;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter3new(
  
) {
  float _M0L1cS992;
  float _M0L2glS993;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_2453;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS992 = -0x1p+0f;
  _M0L2glS993 = -0x1p+0f;
  _block_2453
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_2453)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2453->$0 = _M0L1cS992;
  _block_2453->$1 = _M0L2glS993;
  _block_2453->$2 = 0x1.ep+3f;
  _block_2453->$3 = -0x1.9p+5f;
  _block_2453->$4 = -0x1.ep+5f;
  _block_2453->$5 = -0x1.18p+6f;
  _block_2453->$6 = 0x1.eb851eb851eb8p-5f;
  _block_2453->$7 = 0x1p+1f;
  _block_2453->$8 = 0x0p+0f;
  _block_2453->$9 = 0x0p+0f;
  _block_2453->$10 = 0x0p+0f;
  return _block_2453;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS966,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS968,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS971
) {
  struct _M0TPB5ArrayGfE* _M0L1vS965;
  float _M0L2vtS2217;
  float _M0L2vrS2218;
  float _M0L6spreadS967;
  int32_t _M0L7_2abindS969;
  int32_t _M0L1kS970;
  struct _M0TPB5ArrayGfE* _M0L1wS973;
  struct _M0TPB5ArrayGbE* _M0L4fireS974;
  struct _M0TPB5ArrayGiE* _M0L4tabsS975;
  struct _M0TPB5ArrayGfE* _M0L1iS976;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS977;
  struct _M0TPB5ArrayGfE* _M0L2geS978;
  struct _M0TPB5ArrayGfE* _M0L2giS979;
  struct _M0TPB5ArrayGfE* _M0L2heS980;
  struct _M0TPB5ArrayGfE* _M0L2hiS981;
  struct _M0TPB5ArrayGfE* _M0L3gluS982;
  struct _M0TPB5ArrayGfE* _M0L4gabaS983;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS984;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS985;
  float _M0L4e__eS986;
  float _M0L4e__iS987;
  float _M0L3treS988;
  float _M0L3tdeS989;
  float _M0L3triS990;
  float _M0L3tdiS991;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS2216;
  struct _M0TP26RiantR8snn__mbt2IF* _block_2455;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS965 = _M0MPC15array5Array4makeGfE(_M0L1nS966, 0x0p+0f);
  _M0L2vtS2217 = _M0L5paramS968->$3;
  _M0L2vrS2218 = _M0L5paramS968->$4;
  _M0L6spreadS967 = _M0L2vtS2217 - _M0L2vrS2218;
  _M0L7_2abindS969 = 0;
  _M0L1kS970 = _M0L7_2abindS969;
  while (1) {
    if (_M0L1kS970 < _M0L1nS966) {
      float _M0L2vrS2212 = _M0L5paramS968->$4;
      float _M0L6_2atmpS2214;
      float _M0L6_2atmpS2213;
      float _M0L6_2atmpS2211;
      int32_t _M0L6_2atmpS2215;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2214 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS971);
      _M0L6_2atmpS2213 = _M0L6_2atmpS2214 * _M0L6spreadS967;
      _M0L6_2atmpS2211 = _M0L2vrS2212 + _M0L6_2atmpS2213;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS965, _M0L1kS970, _M0L6_2atmpS2211);
      _M0L6_2atmpS2215 = _M0L1kS970 + 1;
      _M0L1kS970 = _M0L6_2atmpS2215;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS973 = _M0MPC15array5Array4makeGfE(_M0L1nS966, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS974 = _M0MPC15array5Array4makeGbE(_M0L1nS966, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS975 = _M0MPC15array5Array4makeGiE(_M0L1nS966, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS976 = _M0MPC15array5Array4makeGfE(_M0L1nS966, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS977 = _M0MPC15array5Array4makeGfE(_M0L1nS966, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS978 = _M0MPC15array5Array4makeGfE(_M0L1nS966, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS979 = _M0MPC15array5Array4makeGfE(_M0L1nS966, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS980 = _M0MPC15array5Array4makeGfE(_M0L1nS966, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS981 = _M0MPC15array5Array4makeGfE(_M0L1nS966, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS982 = _M0MPC15array5Array4makeGfE(_M0L1nS966, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS983 = _M0MPC15array5Array4makeGfE(_M0L1nS966, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS984 = _M0MPC15array5Array4makeGfE(_M0L1nS966, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS985 = _M0MPC15array5Array4makeGfE(_M0L1nS966, 0x1p+0f);
  _M0L4e__eS986 = 0x0p+0f;
  _M0L4e__iS987 = -0x1.2cp+6f;
  _M0L3treS988 = 0x1p+0f;
  _M0L3tdeS989 = 0x1.8p+2f;
  _M0L3triS990 = 0x1p-1f;
  _M0L3tdiS991 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS2216 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS968);
  _block_2455
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_2455)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 43, 0);
  _block_2455->$0 = _M0L5paramS968;
  _block_2455->$1 = _M0L6_2atmpS2216;
  _block_2455->$2 = _M0L1nS966;
  _block_2455->$3 = _M0L1vS965;
  _block_2455->$4 = _M0L1wS973;
  _block_2455->$5 = _M0L4fireS974;
  _block_2455->$6 = _M0L4tabsS975;
  _block_2455->$7 = _M0L1iS976;
  _block_2455->$8 = _M0L9syn__currS977;
  _block_2455->$9 = _M0L2geS978;
  _block_2455->$10 = _M0L2giS979;
  _block_2455->$11 = _M0L2heS980;
  _block_2455->$12 = _M0L2hiS981;
  _block_2455->$13 = _M0L3gluS982;
  _block_2455->$14 = _M0L4gabaS983;
  _block_2455->$15 = _M0L7gsyn__eS984;
  _block_2455->$16 = _M0L7gsyn__iS985;
  _block_2455->$17 = _M0L4e__eS986;
  _block_2455->$18 = _M0L4e__iS987;
  _block_2455->$19 = _M0L3treS988;
  _block_2455->$20 = _M0L3tdeS989;
  _block_2455->$21 = _M0L3triS990;
  _block_2455->$22 = _M0L3tdiS991;
  return _block_2455;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_2456;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_2456
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_2456)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2456->$0 = 0x1p+1f;
  return _block_2456;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS959,
  int32_t _M0L4colsS960,
  float _M0L2muS961,
  float _M0L5sigmaS962,
  float _M0L1pS963,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS964
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS959, _M0L4colsS960, _M0L2muS961, _M0L5sigmaS962, _M0L1pS963, 0, _M0L3rngS964);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS873,
  int32_t _M0L4colsS877,
  float _M0L2muS883,
  float _M0L5sigmaS884,
  float _M0L1pS896,
  int32_t _M0L4ruleS890,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS886
) {
  float* _M0L6_2atmpS2210;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2209;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS872;
  int32_t _M0L7_2abindS874;
  int32_t _M0L1iS875;
  int32_t _M0L6_2atmpS2208;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS949;
  int32_t* _M0L6_2atmpS2207;
  struct _M0TPB5ArrayGiE* _M0L6colptrS950;
  float* _M0L6_2atmpS2206;
  struct _M0TPB5ArrayGfE* _M0L4valsS951;
  int32_t _M0L7_2abindS952;
  int32_t _M0L1iS953;
  int32_t _M0L6_2atmpS2205;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_2476;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2210 = moonbit_empty_float_array;
  _M0L6_2atmpS2209
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2209)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2209->$0 = _M0L6_2atmpS2210;
  _M0L6_2atmpS2209->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS872
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS873, _M0L6_2atmpS2209);
  _M0L7_2abindS874 = 0;
  _M0L1iS875 = _M0L7_2abindS874;
  while (1) {
    if (_M0L1iS875 < _M0L4rowsS873) {
      struct _M0TPB5ArrayGfE* _M0L3rowS876;
      int32_t _M0L7_2abindS878;
      int32_t _M0L1jS879;
      int32_t _M0L6_2atmpS2161;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS876 = _M0MPC15array5Array4makeGfE(_M0L4colsS877, 0x0p+0f);
      _M0L7_2abindS878 = 0;
      _M0L1jS879 = _M0L7_2abindS878;
      while (1) {
        if (_M0L1jS879 < _M0L4colsS877) {
          double _M0L2z1S881;
          struct _M0TUddE* _M0L7_2abindS885;
          double _M0L5_2az1S887;
          float _M0L6_2atmpS2159;
          float _M0L6_2atmpS2158;
          float _M0L1wS882;
          int32_t _M0L6_2atmpS2160;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS885
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS886);
          _M0L5_2az1S887 = _M0L7_2abindS885->$0;
          moonbit_decref_cycle_free(_M0L7_2abindS885);
          _M0L2z1S881 = _M0L5_2az1S887;
          goto join_880;
          goto joinlet_2459;
          join_880:;
          _M0L6_2atmpS2159 = (float)_M0L2z1S881;
          _M0L6_2atmpS2158 = _M0L5sigmaS884 * _M0L6_2atmpS2159;
          _M0L1wS882 = _M0L2muS883 + _M0L6_2atmpS2158;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS876, _M0L1jS879, _M0L1wS882);
          joinlet_2459:;
          _M0L6_2atmpS2160 = _M0L1jS879 + 1;
          _M0L1jS879 = _M0L6_2atmpS2160;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS872, _M0L1iS875, _M0L3rowS876);
      _M0L6_2atmpS2161 = _M0L1iS875 + 1;
      _M0L1iS875 = _M0L6_2atmpS2161;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS890) {
    case 0: {
      int32_t _M0L7_2abindS891 = 0;
      int32_t _M0L1iS892 = _M0L7_2abindS891;
      while (1) {
        if (_M0L1iS892 < _M0L4rowsS873) {
          int32_t _M0L7_2abindS893 = 0;
          int32_t _M0L1jS894 = _M0L7_2abindS893;
          int32_t _M0L6_2atmpS2164;
          while (1) {
            if (_M0L1jS894 < _M0L4colsS877) {
              float _M0L1uS895;
              int32_t _M0L6_2atmpS2163;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS895 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS886);
              if (_M0L1uS895 >= _M0L1pS896) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2162;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2162
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS872, _M0L1iS892);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2162, _M0L1jS894, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2162);
              }
              _M0L6_2atmpS2163 = _M0L1jS894 + 1;
              _M0L1jS894 = _M0L6_2atmpS2163;
              continue;
            }
            break;
          }
          _M0L6_2atmpS2164 = _M0L1iS892 + 1;
          _M0L1iS892 = _M0L6_2atmpS2164;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS2182 = (float)_M0L4rowsS873;
      float _M0L6_2atmpS2181 = _M0L6_2atmpS2182 * _M0L1pS896;
      int32_t _M0L7n__keepS899;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS899 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2181);
      if (_M0L7n__keepS899 > 0 && _M0L7n__keepS899 <= _M0L4rowsS873) {
        int32_t _M0L7_2abindS900 = 0;
        int32_t _M0L1jS901 = _M0L7_2abindS900;
        while (1) {
          if (_M0L1jS901 < _M0L4colsS877) {
            int32_t* _M0L6_2atmpS2176 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS902 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS903;
            int32_t _M0L1kS904;
            int32_t _M0L7n__dropS906;
            int32_t _M0L7_2abindS907;
            int32_t _M0L1kS908;
            int32_t _M0L7_2abindS914;
            int32_t _M0L1kS915;
            int32_t _M0L6_2atmpS2177;
            Moonbit_object_header(_M0L8pre__idxS902)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
            _M0L8pre__idxS902->$0 = _M0L6_2atmpS2176;
            _M0L8pre__idxS902->$1 = 0;
            _M0L7_2abindS903 = 0;
            _M0L1kS904 = _M0L7_2abindS903;
            while (1) {
              if (_M0L1kS904 < _M0L4rowsS873) {
                int32_t _M0L6_2atmpS2165;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS902, _M0L1kS904);
                _M0L6_2atmpS2165 = _M0L1kS904 + 1;
                _M0L1kS904 = _M0L6_2atmpS2165;
                continue;
              }
              break;
            }
            _M0L7n__dropS906 = _M0L4rowsS873 - _M0L7n__keepS899;
            _M0L7_2abindS907 = 0;
            _M0L1kS908 = _M0L7_2abindS907;
            while (1) {
              if (_M0L1kS908 < _M0L7n__dropS906) {
                float _M0L1uS909;
                float _M0L6_2atmpS2169;
                float _M0L6_2atmpS2171;
                float _M0L6_2atmpS2170;
                float _M0L6_2atmpS2168;
                int32_t _M0L6_2atmpS2167;
                int32_t _M0L6r__idxS910;
                int32_t _M0L10r__clampedS911;
                int32_t _M0L3tmpS912;
                int32_t _M0L6_2atmpS2166;
                int32_t _M0L6_2atmpS2172;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS909 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS886);
                _M0L6_2atmpS2169 = (float)_M0L4rowsS873;
                _M0L6_2atmpS2171 = (float)_M0L1kS908;
                _M0L6_2atmpS2170 = _M0L6_2atmpS2171 * _M0L1uS909;
                _M0L6_2atmpS2168 = _M0L6_2atmpS2169 - _M0L6_2atmpS2170;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2167
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2168);
                _M0L6r__idxS910 = _M0L1kS908 + _M0L6_2atmpS2167;
                if (_M0L6r__idxS910 >= _M0L4rowsS873) {
                  _M0L10r__clampedS911 = _M0L4rowsS873 - 1;
                } else {
                  _M0L10r__clampedS911 = _M0L6r__idxS910;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS912
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS902, _M0L1kS908);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2166
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS902, _M0L10r__clampedS911);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS902, _M0L1kS908, _M0L6_2atmpS2166);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS902, _M0L10r__clampedS911, _M0L3tmpS912);
                _M0L6_2atmpS2172 = _M0L1kS908 + 1;
                _M0L1kS908 = _M0L6_2atmpS2172;
                continue;
              }
              break;
            }
            _M0L7_2abindS914 = 0;
            _M0L1kS915 = _M0L7_2abindS914;
            while (1) {
              if (_M0L1kS915 < _M0L7n__dropS906) {
                int32_t _M0L6_2atmpS2174;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2173;
                int32_t _M0L6_2atmpS2175;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2174
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS902, _M0L1kS915);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2173
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS872, _M0L6_2atmpS2174);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2173, _M0L1jS901, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2173);
                _M0L6_2atmpS2175 = _M0L1kS915 + 1;
                _M0L1kS915 = _M0L6_2atmpS2175;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L8pre__idxS902);
              }
              break;
            }
            _M0L6_2atmpS2177 = _M0L1jS901 + 1;
            _M0L1jS901 = _M0L6_2atmpS2177;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS899 == 0) {
        int32_t _M0L7_2abindS918 = 0;
        int32_t _M0L1iS919 = _M0L7_2abindS918;
        while (1) {
          if (_M0L1iS919 < _M0L4rowsS873) {
            int32_t _M0L7_2abindS920 = 0;
            int32_t _M0L1jS921 = _M0L7_2abindS920;
            int32_t _M0L6_2atmpS2180;
            while (1) {
              if (_M0L1jS921 < _M0L4colsS877) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2178;
                int32_t _M0L6_2atmpS2179;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2178
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS872, _M0L1iS919);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2178, _M0L1jS921, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2178);
                _M0L6_2atmpS2179 = _M0L1jS921 + 1;
                _M0L1jS921 = _M0L6_2atmpS2179;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2180 = _M0L1iS919 + 1;
            _M0L1iS919 = _M0L6_2atmpS2180;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS2200 = (float)_M0L4colsS877;
      float _M0L6_2atmpS2199 = _M0L6_2atmpS2200 * _M0L1pS896;
      int32_t _M0L7n__keepS924;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS924 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2199);
      if (_M0L7n__keepS924 > 0 && _M0L7n__keepS924 <= _M0L4colsS877) {
        int32_t _M0L7_2abindS925 = 0;
        int32_t _M0L1iS926 = _M0L7_2abindS925;
        while (1) {
          if (_M0L1iS926 < _M0L4rowsS873) {
            int32_t* _M0L6_2atmpS2194 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS927 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS928;
            int32_t _M0L1kS929;
            int32_t _M0L7n__dropS931;
            int32_t _M0L7_2abindS932;
            int32_t _M0L1kS933;
            int32_t _M0L7_2abindS939;
            int32_t _M0L1kS940;
            int32_t _M0L6_2atmpS2195;
            Moonbit_object_header(_M0L9post__idxS927)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
            _M0L9post__idxS927->$0 = _M0L6_2atmpS2194;
            _M0L9post__idxS927->$1 = 0;
            _M0L7_2abindS928 = 0;
            _M0L1kS929 = _M0L7_2abindS928;
            while (1) {
              if (_M0L1kS929 < _M0L4colsS877) {
                int32_t _M0L6_2atmpS2183;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS927, _M0L1kS929);
                _M0L6_2atmpS2183 = _M0L1kS929 + 1;
                _M0L1kS929 = _M0L6_2atmpS2183;
                continue;
              }
              break;
            }
            _M0L7n__dropS931 = _M0L4colsS877 - _M0L7n__keepS924;
            _M0L7_2abindS932 = 0;
            _M0L1kS933 = _M0L7_2abindS932;
            while (1) {
              if (_M0L1kS933 < _M0L7n__dropS931) {
                float _M0L1uS934;
                float _M0L6_2atmpS2187;
                float _M0L6_2atmpS2189;
                float _M0L6_2atmpS2188;
                float _M0L6_2atmpS2186;
                int32_t _M0L6_2atmpS2185;
                int32_t _M0L6r__idxS935;
                int32_t _M0L10r__clampedS936;
                int32_t _M0L3tmpS937;
                int32_t _M0L6_2atmpS2184;
                int32_t _M0L6_2atmpS2190;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS934 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS886);
                _M0L6_2atmpS2187 = (float)_M0L4colsS877;
                _M0L6_2atmpS2189 = (float)_M0L1kS933;
                _M0L6_2atmpS2188 = _M0L6_2atmpS2189 * _M0L1uS934;
                _M0L6_2atmpS2186 = _M0L6_2atmpS2187 - _M0L6_2atmpS2188;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2185
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2186);
                _M0L6r__idxS935 = _M0L1kS933 + _M0L6_2atmpS2185;
                if (_M0L6r__idxS935 >= _M0L4colsS877) {
                  _M0L10r__clampedS936 = _M0L4colsS877 - 1;
                } else {
                  _M0L10r__clampedS936 = _M0L6r__idxS935;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS937
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS927, _M0L1kS933);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2184
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS927, _M0L10r__clampedS936);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS927, _M0L1kS933, _M0L6_2atmpS2184);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS927, _M0L10r__clampedS936, _M0L3tmpS937);
                _M0L6_2atmpS2190 = _M0L1kS933 + 1;
                _M0L1kS933 = _M0L6_2atmpS2190;
                continue;
              }
              break;
            }
            _M0L7_2abindS939 = 0;
            _M0L1kS940 = _M0L7_2abindS939;
            while (1) {
              if (_M0L1kS940 < _M0L7n__dropS931) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2191;
                int32_t _M0L6_2atmpS2192;
                int32_t _M0L6_2atmpS2193;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2191
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS872, _M0L1iS926);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2192
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS927, _M0L1kS940);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2191, _M0L6_2atmpS2192, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2191);
                _M0L6_2atmpS2193 = _M0L1kS940 + 1;
                _M0L1kS940 = _M0L6_2atmpS2193;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L9post__idxS927);
              }
              break;
            }
            _M0L6_2atmpS2195 = _M0L1iS926 + 1;
            _M0L1iS926 = _M0L6_2atmpS2195;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS924 == 0) {
        int32_t _M0L7_2abindS943 = 0;
        int32_t _M0L1iS944 = _M0L7_2abindS943;
        while (1) {
          if (_M0L1iS944 < _M0L4rowsS873) {
            int32_t _M0L7_2abindS945 = 0;
            int32_t _M0L1jS946 = _M0L7_2abindS945;
            int32_t _M0L6_2atmpS2198;
            while (1) {
              if (_M0L1jS946 < _M0L4colsS877) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2196;
                int32_t _M0L6_2atmpS2197;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2196
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS872, _M0L1iS944);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2196, _M0L1jS946, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2196);
                _M0L6_2atmpS2197 = _M0L1jS946 + 1;
                _M0L1jS946 = _M0L6_2atmpS2197;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2198 = _M0L1iS944 + 1;
            _M0L1iS944 = _M0L6_2atmpS2198;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS2208 = _M0L4rowsS873 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS949 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS2208, 0);
  _M0L6_2atmpS2207 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS950
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS950)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6colptrS950->$0 = _M0L6_2atmpS2207;
  _M0L6colptrS950->$1 = 0;
  _M0L6_2atmpS2206 = moonbit_empty_float_array;
  _M0L4valsS951
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS951)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L4valsS951->$0 = _M0L6_2atmpS2206;
  _M0L4valsS951->$1 = 0;
  _M0L7_2abindS952 = 0;
  _M0L1iS953 = _M0L7_2abindS952;
  while (1) {
    if (_M0L1iS953 < _M0L4rowsS873) {
      int32_t _M0L6_2atmpS2201;
      int32_t _M0L7_2abindS954;
      int32_t _M0L1jS955;
      int32_t _M0L6_2atmpS2204;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS2201 = _M0MPC15array5Array6lengthGfE(_M0L4valsS951);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS949, _M0L1iS953, _M0L6_2atmpS2201);
      _M0L7_2abindS954 = 0;
      _M0L1jS955 = _M0L7_2abindS954;
      while (1) {
        if (_M0L1jS955 < _M0L4colsS877) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS2202;
          float _M0L1vS956;
          int32_t _M0L6_2atmpS2203;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS2202
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS872, _M0L1iS953);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS956
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS2202, _M0L1jS955);
          moonbit_decref_cycle_free(_M0L6_2atmpS2202);
          if (_M0L1vS956 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS950, _M0L1jS955);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS951, _M0L1vS956);
          }
          _M0L6_2atmpS2203 = _M0L1jS955 + 1;
          _M0L1jS955 = _M0L6_2atmpS2203;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2204 = _M0L1iS953 + 1;
      _M0L1iS953 = _M0L6_2atmpS2204;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L5denseS872);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2205 = _M0MPC15array5Array6lengthGfE(_M0L4valsS951);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS949, _M0L4rowsS873, _M0L6_2atmpS2205);
  _block_2476
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_2476)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 61, 0);
  _block_2476->$0 = _M0L4rowsS873;
  _block_2476->$1 = _M0L4colsS877;
  _block_2476->$2 = _M0L6rowptrS949;
  _block_2476->$3 = _M0L6colptrS950;
  _block_2476->$4 = _M0L4valsS951;
  return _block_2476;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS870
) {
  struct _M0TUmmmmE* _M0L1sS869;
  uint64_t _M0L6_2atmpS2157;
  struct _M0TUmmmmE* _M0L1tS871;
  uint64_t _M0L6_2atmpS2153;
  uint64_t _M0L6_2atmpS2154;
  uint64_t _M0L6_2atmpS2155;
  uint64_t _M0L6_2atmpS2156;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2477;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS869 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS870);
  _M0L6_2atmpS2157 = _M0L1sS869->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS871 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2157);
  _M0L6_2atmpS2153 = _M0L1sS869->$0;
  _M0L6_2atmpS2154 = _M0L1sS869->$1;
  _M0L6_2atmpS2155 = _M0L1sS869->$2;
  moonbit_decref_cycle_free(_M0L1sS869);
  _M0L6_2atmpS2156 = _M0L1tS871->$0;
  moonbit_decref_cycle_free(_M0L1tS871);
  _block_2477
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2477)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2477->$0 = _M0L6_2atmpS2153;
  _block_2477->$1 = _M0L6_2atmpS2154;
  _block_2477->$2 = _M0L6_2atmpS2155;
  _block_2477->$3 = _M0L6_2atmpS2156;
  return _block_2477;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS861) {
  uint64_t _M0L2s1S860;
  uint64_t _M0L2z1S862;
  uint64_t _M0L2s2S863;
  uint64_t _M0L2z2S864;
  uint64_t _M0L2s3S865;
  uint64_t _M0L2z3S866;
  uint64_t _M0L2s4S867;
  uint64_t _M0L2z4S868;
  struct _M0TUmmmmE* _block_2478;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S860 = _M0L4seedS861 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S862 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S860);
  _M0L2s2S863 = _M0L2s1S860 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S864 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S863);
  _M0L2s3S865 = _M0L2s2S863 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S866 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S865);
  _M0L2s4S867 = _M0L2s3S865 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S868 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S867);
  _block_2478 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2478)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2478->$0 = _M0L2z1S862;
  _block_2478->$1 = _M0L2z2S864;
  _block_2478->$2 = _M0L2z3S866;
  _block_2478->$3 = _M0L2z4S868;
  return _block_2478;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS858) {
  uint64_t _M0L6_2atmpS2152;
  uint64_t _M0L6_2atmpS2151;
  uint64_t _M0L1zS857;
  uint64_t _M0L6_2atmpS2150;
  uint64_t _M0L6_2atmpS2149;
  uint64_t _M0L1zS859;
  uint64_t _M0L6_2atmpS2148;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2152 = _M0L1zS858 >> 30;
  _M0L6_2atmpS2151 = _M0L1zS858 ^ _M0L6_2atmpS2152;
  _M0L1zS857 = _M0L6_2atmpS2151 * 13787848793156543929ull;
  _M0L6_2atmpS2150 = _M0L1zS857 >> 27;
  _M0L6_2atmpS2149 = _M0L1zS857 ^ _M0L6_2atmpS2150;
  _M0L1zS859 = _M0L6_2atmpS2149 * 10723151780598845931ull;
  _M0L6_2atmpS2148 = _M0L1zS859 >> 31;
  return _M0L1zS859 ^ _M0L6_2atmpS2148;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS852
) {
  double _M0L2u1S851;
  double _M0L8u1__safeS853;
  double _M0L2u2S854;
  double _M0L6_2atmpS2147;
  double _M0L6_2atmpS2146;
  double _M0L1rS855;
  double _M0L5thetaS856;
  double _M0L6_2atmpS2145;
  double _M0L6_2atmpS2142;
  double _M0L6_2atmpS2144;
  double _M0L6_2atmpS2143;
  struct _M0TUddE* _block_2479;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S851 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS852);
  if (_M0L2u1S851 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS853 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS853 = _M0L2u1S851;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S854 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS852);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2147 = _M0FPC14math2ln(_M0L8u1__safeS853);
  _M0L6_2atmpS2146 = -0x1p+1 * _M0L6_2atmpS2147;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS855 = sqrt(_M0L6_2atmpS2146);
  _M0L5thetaS856 = 0x1.921fb54442d18p+2 * _M0L2u2S854;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2145 = _M0FPC14math3cos(_M0L5thetaS856);
  _M0L6_2atmpS2142 = _M0L1rS855 * _M0L6_2atmpS2145;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2144 = _M0FPC14math3sin(_M0L5thetaS856);
  _M0L6_2atmpS2143 = _M0L1rS855 * _M0L6_2atmpS2144;
  _block_2479 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_2479)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2479->$0 = _M0L6_2atmpS2142;
  _block_2479->$1 = _M0L6_2atmpS2143;
  return _block_2479;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS849
) {
  uint64_t _M0L1uS848;
  uint64_t _M0L4bitsS850;
  double _M0L6_2atmpS2141;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS848 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS849);
  _M0L4bitsS850 = _M0L1uS848 >> 11;
  _M0L6_2atmpS2141 = (double)_M0L4bitsS850;
  return _M0L6_2atmpS2141 * 0x1p-53;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS846
) {
  uint32_t _M0L1uS845;
  uint32_t _M0L4bitsS847;
  double _M0L6_2atmpS2140;
  double _M0L6_2atmpS2139;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS845 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS846);
  _M0L4bitsS847 = _M0L1uS845 >> 8;
  _M0L6_2atmpS2140 = (double)_M0L4bitsS847;
  _M0L6_2atmpS2139 = _M0L6_2atmpS2140 * 0x1p-24;
  return (float)_M0L6_2atmpS2139;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS844
) {
  uint64_t _M0L1uS843;
  uint64_t _M0L6_2atmpS2138;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS843 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS844);
  _M0L6_2atmpS2138 = _M0L1uS843 >> 32;
  return (uint32_t)_M0L6_2atmpS2138;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS836
) {
  uint64_t _M0L2s0S835;
  uint64_t _M0L2s1S837;
  uint64_t _M0L2s2S838;
  uint64_t _M0L2s3S839;
  uint64_t _M0L3tmpS840;
  uint64_t _M0L6_2atmpS2137;
  uint64_t _M0L3resS841;
  uint64_t _M0L1tS842;
  uint64_t _M0L6_2atmpS2127;
  uint64_t _M0L6_2atmpS2128;
  uint64_t _M0L2s2S2130;
  uint64_t _M0L6_2atmpS2129;
  uint64_t _M0L2s3S2132;
  uint64_t _M0L6_2atmpS2131;
  uint64_t _M0L2s2S2134;
  uint64_t _M0L6_2atmpS2133;
  uint64_t _M0L2s3S2136;
  uint64_t _M0L6_2atmpS2135;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S835 = _M0L1rS836->$0;
  _M0L2s1S837 = _M0L1rS836->$1;
  _M0L2s2S838 = _M0L1rS836->$2;
  _M0L2s3S839 = _M0L1rS836->$3;
  _M0L3tmpS840 = _M0L2s0S835 + _M0L2s3S839;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2137 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS840, 23);
  _M0L3resS841 = _M0L6_2atmpS2137 + _M0L2s0S835;
  _M0L1tS842 = _M0L2s1S837 << 17;
  _M0L6_2atmpS2127 = _M0L2s2S838 ^ _M0L2s0S835;
  _M0L1rS836->$2 = _M0L6_2atmpS2127;
  _M0L6_2atmpS2128 = _M0L2s3S839 ^ _M0L2s1S837;
  _M0L1rS836->$3 = _M0L6_2atmpS2128;
  _M0L2s2S2130 = _M0L1rS836->$2;
  _M0L6_2atmpS2129 = _M0L2s1S837 ^ _M0L2s2S2130;
  _M0L1rS836->$1 = _M0L6_2atmpS2129;
  _M0L2s3S2132 = _M0L1rS836->$3;
  _M0L6_2atmpS2131 = _M0L2s0S835 ^ _M0L2s3S2132;
  _M0L1rS836->$0 = _M0L6_2atmpS2131;
  _M0L2s2S2134 = _M0L1rS836->$2;
  _M0L6_2atmpS2133 = _M0L2s2S2134 ^ _M0L1tS842;
  _M0L1rS836->$2 = _M0L6_2atmpS2133;
  _M0L2s3S2136 = _M0L1rS836->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2135 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S2136, 45);
  _M0L1rS836->$3 = _M0L6_2atmpS2135;
  return _M0L3resS841;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS833, int32_t _M0L1kS834) {
  uint64_t _M0L6_2atmpS2124;
  int32_t _M0L6_2atmpS2126;
  uint64_t _M0L6_2atmpS2125;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2124 = _M0L1xS833 << (_M0L1kS834 & 63);
  _M0L6_2atmpS2126 = 64 - _M0L1kS834;
  _M0L6_2atmpS2125 = _M0L1xS833 >> (_M0L6_2atmpS2126 & 63);
  return _M0L6_2atmpS2124 | _M0L6_2atmpS2125;
}

double _M0FPC14math2ln(double _M0L1xS819) {
  struct _M0TUdiE* _M0L7_2abindS820;
  double _M0L5_2af1S821;
  int32_t _M0L5_2akiS822;
  double _M0L1fS824;
  double _M0L1kS825;
  double _M0L6_2atmpS2117;
  double _M0L1sS826;
  double _M0L2s2S827;
  double _M0L2s4S828;
  double _M0L6_2atmpS2116;
  double _M0L6_2atmpS2115;
  double _M0L6_2atmpS2114;
  double _M0L6_2atmpS2113;
  double _M0L6_2atmpS2112;
  double _M0L6_2atmpS2111;
  double _M0L2t1S829;
  double _M0L6_2atmpS2110;
  double _M0L6_2atmpS2109;
  double _M0L6_2atmpS2108;
  double _M0L6_2atmpS2107;
  double _M0L2t2S830;
  double _M0L1rS831;
  double _M0L6_2atmpS2106;
  double _M0L4hfsqS832;
  double _M0L6_2atmpS2099;
  double _M0L6_2atmpS2105;
  double _M0L6_2atmpS2103;
  double _M0L6_2atmpS2104;
  double _M0L6_2atmpS2102;
  double _M0L6_2atmpS2101;
  double _M0L6_2atmpS2100;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS819 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS819)
      || _M0MPC16double6Double7is__inf(_M0L1xS819)
    ) {
      return _M0L1xS819;
    } else if (_M0L1xS819 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS820 = _M0FPC14math5frexp(_M0L1xS819);
  _M0L5_2af1S821 = _M0L7_2abindS820->$0;
  _M0L5_2akiS822 = _M0L7_2abindS820->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS820);
  if (_M0L5_2af1S821 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS2121 = _M0L5_2af1S821 * 0x1p+1;
    double _M0L6_2atmpS2118 = _M0L6_2atmpS2121 - 0x1p+0;
    int32_t _M0L6_2atmpS2120 = _M0L5_2akiS822 - 1;
    double _M0L6_2atmpS2119 = (double)_M0L6_2atmpS2120;
    _M0L1fS824 = _M0L6_2atmpS2118;
    _M0L1kS825 = _M0L6_2atmpS2119;
    goto join_823;
  } else {
    double _M0L6_2atmpS2122 = _M0L5_2af1S821 - 0x1p+0;
    double _M0L6_2atmpS2123 = (double)_M0L5_2akiS822;
    _M0L1fS824 = _M0L6_2atmpS2122;
    _M0L1kS825 = _M0L6_2atmpS2123;
    goto join_823;
  }
  join_823:;
  _M0L6_2atmpS2117 = 0x1p+1 + _M0L1fS824;
  _M0L1sS826 = _M0L1fS824 / _M0L6_2atmpS2117;
  _M0L2s2S827 = _M0L1sS826 * _M0L1sS826;
  _M0L2s4S828 = _M0L2s2S827 * _M0L2s2S827;
  _M0L6_2atmpS2116 = _M0L2s4S828 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS2115 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS2116;
  _M0L6_2atmpS2114 = _M0L2s4S828 * _M0L6_2atmpS2115;
  _M0L6_2atmpS2113 = 0x1.2492494229359p-2 + _M0L6_2atmpS2114;
  _M0L6_2atmpS2112 = _M0L2s4S828 * _M0L6_2atmpS2113;
  _M0L6_2atmpS2111 = 0x1.5555555555593p-1 + _M0L6_2atmpS2112;
  _M0L2t1S829 = _M0L2s2S827 * _M0L6_2atmpS2111;
  _M0L6_2atmpS2110 = _M0L2s4S828 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS2109 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS2110;
  _M0L6_2atmpS2108 = _M0L2s4S828 * _M0L6_2atmpS2109;
  _M0L6_2atmpS2107 = 0x1.999999997fa04p-2 + _M0L6_2atmpS2108;
  _M0L2t2S830 = _M0L2s4S828 * _M0L6_2atmpS2107;
  _M0L1rS831 = _M0L2t1S829 + _M0L2t2S830;
  _M0L6_2atmpS2106 = 0x1p-1 * _M0L1fS824;
  _M0L4hfsqS832 = _M0L6_2atmpS2106 * _M0L1fS824;
  _M0L6_2atmpS2099 = _M0L1kS825 * 0x1.62e42feep-1;
  _M0L6_2atmpS2105 = _M0L4hfsqS832 + _M0L1rS831;
  _M0L6_2atmpS2103 = _M0L1sS826 * _M0L6_2atmpS2105;
  _M0L6_2atmpS2104 = _M0L1kS825 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS2102 = _M0L6_2atmpS2103 + _M0L6_2atmpS2104;
  _M0L6_2atmpS2101 = _M0L4hfsqS832 - _M0L6_2atmpS2102;
  _M0L6_2atmpS2100 = _M0L6_2atmpS2101 - _M0L1fS824;
  return _M0L6_2atmpS2099 - _M0L6_2atmpS2100;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS812) {
  struct _M0TUdiE* _M0L7_2abindS813;
  double _M0L10_2anorm__fS814;
  int32_t _M0L6_2aexpS815;
  uint64_t _M0L1uS816;
  uint64_t _M0L6_2atmpS2098;
  uint64_t _M0L6_2atmpS2097;
  int32_t _M0L6_2atmpS2096;
  int32_t _M0L6_2atmpS2095;
  int32_t _M0L3expS817;
  uint64_t _M0L6_2atmpS2094;
  uint64_t _M0L6_2atmpS2093;
  uint64_t _M0L6_2atmpS2092;
  double _M0L4fracS818;
  struct _M0TUdiE* _block_2482;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS812 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS812)
    || _M0MPC16double6Double7is__nan(_M0L1fS812)
  ) {
    struct _M0TUdiE* _block_2481 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2481)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2481->$0 = _M0L1fS812;
    _block_2481->$1 = 0;
    return _block_2481;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS813 = _M0FPC14math9normalize(_M0L1fS812);
  _M0L10_2anorm__fS814 = _M0L7_2abindS813->$0;
  _M0L6_2aexpS815 = _M0L7_2abindS813->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS813);
  _M0L1uS816 = *(int64_t*)&_M0L10_2anorm__fS814;
  _M0L6_2atmpS2098 = _M0L1uS816 >> 52;
  _M0L6_2atmpS2097 = _M0L6_2atmpS2098 & 2047ull;
  _M0L6_2atmpS2096 = (int32_t)_M0L6_2atmpS2097;
  _M0L6_2atmpS2095 = _M0L6_2aexpS815 + _M0L6_2atmpS2096;
  _M0L3expS817 = _M0L6_2atmpS2095 - 1022;
  _M0L6_2atmpS2094 = ~9218868437227405312ull;
  _M0L6_2atmpS2093 = _M0L1uS816 & _M0L6_2atmpS2094;
  _M0L6_2atmpS2092 = _M0L6_2atmpS2093 | 4602678819172646912ull;
  _M0L4fracS818 = *(double*)&_M0L6_2atmpS2092;
  _block_2482 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2482)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2482->$0 = _M0L4fracS818;
  _block_2482->$1 = _M0L3expS817;
  return _block_2482;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS811) {
  double _M0L6_2atmpS2089;
  struct _M0TUdiE* _block_2484;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS2089 = fabs(_M0L1fS811);
  if (_M0L6_2atmpS2089 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS2091 = (double)4503599627370496ll;
    double _M0L6_2atmpS2090 = _M0L1fS811 * _M0L6_2atmpS2091;
    struct _M0TUdiE* _block_2483 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2483)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2483->$0 = _M0L6_2atmpS2090;
    _block_2483->$1 = -52;
    return _block_2483;
  }
  _block_2484 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2484)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2484->$0 = _M0L1fS811;
  _block_2484->$1 = 0;
  return _block_2484;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS810) {
  double _M0L6_2atmpS2088;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2088 = (double)_M0L4selfS810;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2088);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS809) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS809 != _M0L4selfS809) {
    return 0;
  } else if (_M0L4selfS809 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS809 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS809;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS790,
  float _M0L4elemS792
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS789;
  int32_t _M0L1iS791;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS789 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS790);
  _M0L1iS791 = 0;
  while (1) {
    if (_M0L1iS791 < _M0L3lenS790) {
      float* _M0L3bufS2080 = _M0L3arrS789->$0;
      int32_t _M0L6_2atmpS2081;
      _M0L3bufS2080[_M0L1iS791] = _M0L4elemS792;
      _M0L6_2atmpS2081 = _M0L1iS791 + 1;
      _M0L1iS791 = _M0L6_2atmpS2081;
      continue;
    }
    break;
  }
  return _M0L3arrS789;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS795,
  int32_t _M0L4elemS797
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS794;
  int32_t _M0L1iS796;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS794 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS795);
  _M0L1iS796 = 0;
  while (1) {
    if (_M0L1iS796 < _M0L3lenS795) {
      uint8_t* _M0L3bufS2082 = _M0L3arrS794->$0;
      int32_t _M0L6_2atmpS2083;
      _M0L3bufS2082[_M0L1iS796] = _M0L4elemS797;
      _M0L6_2atmpS2083 = _M0L1iS796 + 1;
      _M0L1iS796 = _M0L6_2atmpS2083;
      continue;
    }
    break;
  }
  return _M0L3arrS794;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS800,
  int32_t _M0L4elemS802
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS799;
  int32_t _M0L1iS801;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS799 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS800);
  _M0L1iS801 = 0;
  while (1) {
    if (_M0L1iS801 < _M0L3lenS800) {
      int32_t* _M0L3bufS2084 = _M0L3arrS799->$0;
      int32_t _M0L6_2atmpS2085;
      _M0L3bufS2084[_M0L1iS801] = _M0L4elemS802;
      _M0L6_2atmpS2085 = _M0L1iS801 + 1;
      _M0L1iS801 = _M0L6_2atmpS2085;
      continue;
    }
    break;
  }
  return _M0L3arrS799;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS805,
  struct _M0TPB5ArrayGfE* _M0L4elemS807
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS804;
  int32_t _M0L1iS806;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS804
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS805);
  _M0L1iS806 = 0;
  while (1) {
    if (_M0L1iS806 < _M0L3lenS805) {
      struct _M0TPB5ArrayGfE** _M0L3bufS2086 = _M0L3arrS804->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS2360 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS2086[_M0L1iS806];
      int32_t _M0L6_2atmpS2087;
      moonbit_incref_cycle_free(_M0L4elemS807);
      if (_M0L6_2aoldS2360) {
        moonbit_decref_cycle_free(_M0L6_2aoldS2360);
      }
      _M0L3bufS2086[_M0L1iS806] = _M0L4elemS807;
      _M0L6_2atmpS2087 = _M0L1iS806 + 1;
      _M0L1iS806 = _M0L6_2atmpS2087;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS807);
    }
    break;
  }
  return _M0L3arrS804;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS778,
  int32_t _M0L5indexS779,
  float _M0L5valueS780
) {
  int32_t _M0L3lenS777;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS777 = _M0L4selfS778->$1;
  if (_M0L5indexS779 >= 0 && _M0L5indexS779 < _M0L3lenS777) {
    float* _M0L6_2atmpS2077;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2077 = _M0MPC15array5Array6bufferGfE(_M0L4selfS778);
    _M0L6_2atmpS2077[_M0L5indexS779] = _M0L5valueS780;
    moonbit_decref_cycle_free(_M0L6_2atmpS2077);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS782,
  int32_t _M0L5indexS783,
  struct _M0TPB5ArrayGfE* _M0L5valueS784
) {
  int32_t _M0L3lenS781;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS781 = _M0L4selfS782->$1;
  if (_M0L5indexS783 >= 0 && _M0L5indexS783 < _M0L3lenS781) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2078;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS2361;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2078
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS782);
    _M0L6_2aoldS2361
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2078[_M0L5indexS783];
    if (_M0L6_2aoldS2361) {
      moonbit_decref_cycle_free(_M0L6_2aoldS2361);
    }
    _M0L6_2atmpS2078[_M0L5indexS783] = _M0L5valueS784;
    moonbit_decref_cycle_free(_M0L6_2atmpS2078);
  } else {
    moonbit_decref_cycle_free(_M0L5valueS784);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS786,
  int32_t _M0L5indexS787,
  int32_t _M0L5valueS788
) {
  int32_t _M0L3lenS785;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS785 = _M0L4selfS786->$1;
  if (_M0L5indexS787 >= 0 && _M0L5indexS787 < _M0L3lenS785) {
    int32_t* _M0L6_2atmpS2079;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2079 = _M0MPC15array5Array6bufferGiE(_M0L4selfS786);
    _M0L6_2atmpS2079[_M0L5indexS787] = _M0L5valueS788;
    moonbit_decref_cycle_free(_M0L6_2atmpS2079);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS763,
  int32_t _M0L5indexS764
) {
  int32_t _M0L3lenS762;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS762 = _M0L4selfS763->$1;
  if (_M0L5indexS764 >= 0 && _M0L5indexS764 < _M0L3lenS762) {
    float* _M0L6_2atmpS2072;
    float _result_2489;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2072 = _M0MPC15array5Array6bufferGfE(_M0L4selfS763);
    _result_2489 = (float)_M0L6_2atmpS2072[_M0L5indexS764];
    moonbit_decref_cycle_free(_M0L6_2atmpS2072);
    return _result_2489;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TP26RiantR8snn__mbt13SynapseTarget* _M0MPC15array5Array2atGRP26RiantR8snn__mbt13SynapseTargetE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE* _M0L4selfS766,
  int32_t _M0L5indexS767
) {
  int32_t _M0L3lenS765;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS765 = _M0L4selfS766->$1;
  if (_M0L5indexS767 >= 0 && _M0L5indexS767 < _M0L3lenS765) {
    struct _M0TP26RiantR8snn__mbt13SynapseTarget** _M0L6_2atmpS2073;
    struct _M0TP26RiantR8snn__mbt13SynapseTarget* _M0L6_2atmpS2362;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2073
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt13SynapseTargetE(_M0L4selfS766);
    _M0L6_2atmpS2362
    = (struct _M0TP26RiantR8snn__mbt13SynapseTarget*)_M0L6_2atmpS2073[
        _M0L5indexS767
      ];
    if (_M0L6_2atmpS2362) {
      moonbit_incref_cycle_free(_M0L6_2atmpS2362);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2073);
    return _M0L6_2atmpS2362;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS769,
  int32_t _M0L5indexS770
) {
  int32_t _M0L3lenS768;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS768 = _M0L4selfS769->$1;
  if (_M0L5indexS770 >= 0 && _M0L5indexS770 < _M0L3lenS768) {
    int32_t* _M0L6_2atmpS2074;
    int32_t _result_2490;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2074 = _M0MPC15array5Array6bufferGiE(_M0L4selfS769);
    _result_2490 = (int32_t)_M0L6_2atmpS2074[_M0L5indexS770];
    moonbit_decref_cycle_free(_M0L6_2atmpS2074);
    return _result_2490;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS772,
  int32_t _M0L5indexS773
) {
  int32_t _M0L3lenS771;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS771 = _M0L4selfS772->$1;
  if (_M0L5indexS773 >= 0 && _M0L5indexS773 < _M0L3lenS771) {
    moonbit_string_t* _M0L6_2atmpS2075;
    moonbit_string_t _M0L6_2atmpS2363;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2075 = _M0MPC15array5Array6bufferGsE(_M0L4selfS772);
    _M0L6_2atmpS2363 = (moonbit_string_t)_M0L6_2atmpS2075[_M0L5indexS773];
    moonbit_incref_cycle_free(_M0L6_2atmpS2363);
    moonbit_decref_cycle_free(_M0L6_2atmpS2075);
    return _M0L6_2atmpS2363;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS775,
  int32_t _M0L5indexS776
) {
  int32_t _M0L3lenS774;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS774 = _M0L4selfS775->$1;
  if (_M0L5indexS776 >= 0 && _M0L5indexS776 < _M0L3lenS774) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2076;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS2364;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2076
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS775);
    _M0L6_2atmpS2364
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2076[_M0L5indexS776];
    if (_M0L6_2atmpS2364) {
      moonbit_incref_cycle_free(_M0L6_2atmpS2364);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2076);
    return _M0L6_2atmpS2364;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS761) {
  moonbit_string_t _M0L6_2atmpS2071;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2071 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS761);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2071);
  moonbit_decref_cycle_free(_M0L6_2atmpS2071);
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
  uint64_t _M0L6_2atmpS2070;
  uint64_t _M0L6_2atmpS2069;
  int32_t _M0L8ieeeSignS747;
  uint64_t _M0L12ieeeMantissaS748;
  uint64_t _M0L6_2atmpS2068;
  uint64_t _M0L6_2atmpS2067;
  int32_t _M0L12ieeeExponentS749;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS750;
  struct _M0TPB17FloatingDecimal64* _M0L1vS751;
  moonbit_string_t _result_2492;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS743 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  if (_M0L3valS743 >= -0x1p+53 && _M0L3valS743 <= 0x1p+53) {
    if (_M0L3valS743 >= -0x1p+31 && _M0L3valS743 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS744;
      double _M0L6_2atmpS2056;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS744 = _M0MPC16double6Double7to__int(_M0L3valS743);
      _M0L6_2atmpS2056 = (double)_M0L1iS744;
      if (_M0L6_2atmpS2056 == _M0L3valS743) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS744, 10);
      }
    } else {
      int64_t _M0L1iS745;
      double _M0L6_2atmpS2057;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS745 = _M0MPC16double6Double9to__int64(_M0L3valS743);
      _M0L6_2atmpS2057 = (double)_M0L1iS745;
      if (_M0L6_2atmpS2057 == _M0L3valS743) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS745, 10);
      }
    }
  }
  _M0L4bitsS746 = *(int64_t*)&_M0L3valS743;
  _M0L6_2atmpS2070 = _M0L4bitsS746 >> 63;
  _M0L6_2atmpS2069 = _M0L6_2atmpS2070 & 1ull;
  _M0L8ieeeSignS747 = _M0L6_2atmpS2069 != 0ull;
  _M0L12ieeeMantissaS748 = _M0L4bitsS746 & 4503599627370495ull;
  _M0L6_2atmpS2068 = _M0L4bitsS746 >> 52;
  _M0L6_2atmpS2067 = _M0L6_2atmpS2068 & 2047ull;
  _M0L12ieeeExponentS749 = (int32_t)_M0L6_2atmpS2067;
  if (
    _M0L12ieeeExponentS749 == 2047
    || _M0L12ieeeExponentS749 == 0 && _M0L12ieeeMantissaS748 == 0ull
  ) {
    int32_t _M0L6_2atmpS2058 = _M0L12ieeeExponentS749 != 0;
    int32_t _M0L6_2atmpS2059 = _M0L12ieeeMantissaS748 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS747, _M0L6_2atmpS2058, _M0L6_2atmpS2059);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS750
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS748, _M0L12ieeeExponentS749);
  if (_M0L7_2abindS750 == 0) {
    uint32_t _M0L6_2atmpS2060;
    if (_M0L7_2abindS750) {
      moonbit_decref_cycle_free(_M0L7_2abindS750);
    }
    _M0L6_2atmpS2060 = *(uint32_t*)&_M0L12ieeeExponentS749;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS751 = _M0FPB3d2d(_M0L12ieeeMantissaS748, _M0L6_2atmpS2060);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS752 = _M0L7_2abindS750;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS753 = _M0L7_2aSomeS752;
    struct _M0TPB17FloatingDecimal64* _M0L1xS754 = _M0L4_2afS753;
    while (1) {
      uint64_t _M0L8mantissaS2066 = _M0L1xS754->$0;
      uint64_t _M0L1qS755 = _M0L8mantissaS2066 / 10ull;
      uint64_t _M0L8mantissaS2064 = _M0L1xS754->$0;
      uint64_t _M0L6_2atmpS2065 = 10ull * _M0L1qS755;
      uint64_t _M0L1rS756 = _M0L8mantissaS2064 - _M0L6_2atmpS2065;
      int32_t _M0L8exponentS2063;
      int32_t _M0L6_2atmpS2062;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2061;
      if (_M0L1rS756 != 0ull) {
        _M0L1vS751 = _M0L1xS754;
        break;
      }
      _M0L8exponentS2063 = _M0L1xS754->$1;
      moonbit_decref_cycle_free(_M0L1xS754);
      _M0L6_2atmpS2062 = _M0L8exponentS2063 + 1;
      _M0L6_2atmpS2061
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS2061)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS2061->$0 = _M0L1qS755;
      _M0L6_2atmpS2061->$1 = _M0L6_2atmpS2062;
      _M0L1xS754 = _M0L6_2atmpS2061;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2492 = _M0FPB9to__chars(_M0L1vS751, _M0L8ieeeSignS747);
  moonbit_decref_cycle_free(_M0L1vS751);
  return _result_2492;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS738,
  int32_t _M0L12ieeeExponentS740
) {
  uint64_t _M0L2m2S737;
  int32_t _M0L6_2atmpS2055;
  int32_t _M0L2e2S739;
  int32_t _M0L6_2atmpS2054;
  uint64_t _M0L6_2atmpS2053;
  uint64_t _M0L4maskS741;
  uint64_t _M0L8fractionS742;
  int32_t _M0L6_2atmpS2052;
  uint64_t _M0L6_2atmpS2051;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2050;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S737 = 4503599627370496ull | _M0L12ieeeMantissaS738;
  _M0L6_2atmpS2055 = _M0L12ieeeExponentS740 - 1023;
  _M0L2e2S739 = _M0L6_2atmpS2055 - 52;
  if (_M0L2e2S739 > 0) {
    return 0;
  }
  if (_M0L2e2S739 < -52) {
    return 0;
  }
  _M0L6_2atmpS2054 = -_M0L2e2S739;
  _M0L6_2atmpS2053 = 1ull << (_M0L6_2atmpS2054 & 63);
  _M0L4maskS741 = _M0L6_2atmpS2053 - 1ull;
  _M0L8fractionS742 = _M0L2m2S737 & _M0L4maskS741;
  if (_M0L8fractionS742 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2052 = -_M0L2e2S739;
  _M0L6_2atmpS2051 = _M0L2m2S737 >> (_M0L6_2atmpS2052 & 63);
  _M0L6_2atmpS2050
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS2050)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS2050->$0 = _M0L6_2atmpS2051;
  _M0L6_2atmpS2050->$1 = 0;
  return _M0L6_2atmpS2050;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS705,
  int32_t _M0L4signS703
) {
  moonbit_bytes_t _M0L6resultS701;
  int32_t _M0Lm5indexS702;
  uint64_t _M0L6outputS704;
  int32_t _M0L7olengthS706;
  int32_t _M0L8exponentS2049;
  int32_t _M0L6_2atmpS2048;
  int32_t _M0Lm3expS707;
  int32_t _M0L6_2atmpS2047;
  int32_t _M0L6_2atmpS2045;
  int32_t _M0L18scientificNotationS708;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS701 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS702 = 0;
  if (_M0L4signS703) {
    int32_t _M0L6_2atmpS1919 = _M0Lm5indexS702;
    int32_t _M0L6_2atmpS1920;
    if (
      _M0L6_2atmpS1919 < 0
      || _M0L6_2atmpS1919 >= Moonbit_array_length(_M0L6resultS701)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS701[_M0L6_2atmpS1919] = 45;
    _M0L6_2atmpS1920 = _M0Lm5indexS702;
    _M0Lm5indexS702 = _M0L6_2atmpS1920 + 1;
  }
  _M0L6outputS704 = _M0L1vS705->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS706 = _M0FPB17decimal__length17(_M0L6outputS704);
  _M0L8exponentS2049 = _M0L1vS705->$1;
  _M0L6_2atmpS2048 = _M0L8exponentS2049 + _M0L7olengthS706;
  _M0Lm3expS707 = _M0L6_2atmpS2048 - 1;
  _M0L6_2atmpS2047 = _M0Lm3expS707;
  if (_M0L6_2atmpS2047 >= -6) {
    int32_t _M0L6_2atmpS2046 = _M0Lm3expS707;
    _M0L6_2atmpS2045 = _M0L6_2atmpS2046 < 21;
  } else {
    _M0L6_2atmpS2045 = 0;
  }
  _M0L18scientificNotationS708 = !_M0L6_2atmpS2045;
  if (_M0L18scientificNotationS708) {
    int32_t _M0L7_2abindS709 = _M0L7olengthS706 - 1;
    uint64_t _M0L6outputS710;
    int32_t _M0L1iS711 = 0;
    uint64_t _M0L6outputS712 = _M0L6outputS704;
    int32_t _M0L6_2atmpS1921;
    int32_t _M0L6_2atmpS1925;
    int32_t _M0L6_2atmpS1924;
    int32_t _M0L6_2atmpS1923;
    int32_t _M0L6_2atmpS1922;
    int32_t _M0L6_2atmpS1929;
    int32_t _M0L6_2atmpS1930;
    int32_t _M0L6_2atmpS1931;
    int32_t _M0L6_2atmpS1932;
    int32_t _M0L6_2atmpS1933;
    int32_t _M0L6_2atmpS1939;
    int32_t _M0L6_2atmpS1972;
    moonbit_string_t _result_2494;
    while (1) {
      if (_M0L1iS711 < _M0L7_2abindS709) {
        uint64_t _M0L1cS713 = _M0L6outputS712 % 10ull;
        int32_t _M0L6_2atmpS1978 = _M0Lm5indexS702;
        int32_t _M0L6_2atmpS1977 = _M0L6_2atmpS1978 + _M0L7olengthS706;
        int32_t _M0L6_2atmpS1973 = _M0L6_2atmpS1977 - _M0L1iS711;
        int32_t _M0L6_2atmpS1976 = (int32_t)_M0L1cS713;
        int32_t _M0L6_2atmpS1975 = 48 + _M0L6_2atmpS1976;
        int32_t _M0L6_2atmpS1974 = _M0L6_2atmpS1975 & 0xff;
        int32_t _M0L6_2atmpS1979;
        uint64_t _M0L6_2atmpS1980;
        if (
          _M0L6_2atmpS1973 < 0
          || _M0L6_2atmpS1973 >= Moonbit_array_length(_M0L6resultS701)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS701[_M0L6_2atmpS1973] = _M0L6_2atmpS1974;
        _M0L6_2atmpS1979 = _M0L1iS711 + 1;
        _M0L6_2atmpS1980 = _M0L6outputS712 / 10ull;
        _M0L1iS711 = _M0L6_2atmpS1979;
        _M0L6outputS712 = _M0L6_2atmpS1980;
        continue;
      } else {
        _M0L6outputS710 = _M0L6outputS712;
      }
      break;
    }
    _M0L6_2atmpS1921 = _M0Lm5indexS702;
    _M0L6_2atmpS1925 = (int32_t)_M0L6outputS710;
    _M0L6_2atmpS1924 = _M0L6_2atmpS1925 % 10;
    _M0L6_2atmpS1923 = 48 + _M0L6_2atmpS1924;
    _M0L6_2atmpS1922 = _M0L6_2atmpS1923 & 0xff;
    if (
      _M0L6_2atmpS1921 < 0
      || _M0L6_2atmpS1921 >= Moonbit_array_length(_M0L6resultS701)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS701[_M0L6_2atmpS1921] = _M0L6_2atmpS1922;
    if (_M0L7olengthS706 > 1) {
      int32_t _M0L6_2atmpS1927 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS1926 = _M0L6_2atmpS1927 + 1;
      if (
        _M0L6_2atmpS1926 < 0
        || _M0L6_2atmpS1926 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1926] = 46;
    } else {
      int32_t _M0L6_2atmpS1928 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS1928 - 1;
    }
    _M0L6_2atmpS1929 = _M0Lm5indexS702;
    _M0L6_2atmpS1930 = _M0L7olengthS706 + 1;
    _M0Lm5indexS702 = _M0L6_2atmpS1929 + _M0L6_2atmpS1930;
    _M0L6_2atmpS1931 = _M0Lm5indexS702;
    if (
      _M0L6_2atmpS1931 < 0
      || _M0L6_2atmpS1931 >= Moonbit_array_length(_M0L6resultS701)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS701[_M0L6_2atmpS1931] = 101;
    _M0L6_2atmpS1932 = _M0Lm5indexS702;
    _M0Lm5indexS702 = _M0L6_2atmpS1932 + 1;
    _M0L6_2atmpS1933 = _M0Lm3expS707;
    if (_M0L6_2atmpS1933 < 0) {
      int32_t _M0L6_2atmpS1934 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS1935;
      int32_t _M0L6_2atmpS1936;
      if (
        _M0L6_2atmpS1934 < 0
        || _M0L6_2atmpS1934 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1934] = 45;
      _M0L6_2atmpS1935 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS1935 + 1;
      _M0L6_2atmpS1936 = _M0Lm3expS707;
      _M0Lm3expS707 = -_M0L6_2atmpS1936;
    } else {
      int32_t _M0L6_2atmpS1937 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS1938;
      if (
        _M0L6_2atmpS1937 < 0
        || _M0L6_2atmpS1937 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1937] = 43;
      _M0L6_2atmpS1938 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS1938 + 1;
    }
    _M0L6_2atmpS1939 = _M0Lm3expS707;
    if (_M0L6_2atmpS1939 >= 100) {
      int32_t _M0L6_2atmpS1955 = _M0Lm3expS707;
      int32_t _M0L1aS715 = _M0L6_2atmpS1955 / 100;
      int32_t _M0L6_2atmpS1954 = _M0Lm3expS707;
      int32_t _M0L6_2atmpS1953 = _M0L6_2atmpS1954 / 10;
      int32_t _M0L1bS716 = _M0L6_2atmpS1953 % 10;
      int32_t _M0L6_2atmpS1952 = _M0Lm3expS707;
      int32_t _M0L1cS717 = _M0L6_2atmpS1952 % 10;
      int32_t _M0L6_2atmpS1940 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS1942 = 48 + _M0L1aS715;
      int32_t _M0L6_2atmpS1941 = _M0L6_2atmpS1942 & 0xff;
      int32_t _M0L6_2atmpS1946;
      int32_t _M0L6_2atmpS1943;
      int32_t _M0L6_2atmpS1945;
      int32_t _M0L6_2atmpS1944;
      int32_t _M0L6_2atmpS1950;
      int32_t _M0L6_2atmpS1947;
      int32_t _M0L6_2atmpS1949;
      int32_t _M0L6_2atmpS1948;
      int32_t _M0L6_2atmpS1951;
      if (
        _M0L6_2atmpS1940 < 0
        || _M0L6_2atmpS1940 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1940] = _M0L6_2atmpS1941;
      _M0L6_2atmpS1946 = _M0Lm5indexS702;
      _M0L6_2atmpS1943 = _M0L6_2atmpS1946 + 1;
      _M0L6_2atmpS1945 = 48 + _M0L1bS716;
      _M0L6_2atmpS1944 = _M0L6_2atmpS1945 & 0xff;
      if (
        _M0L6_2atmpS1943 < 0
        || _M0L6_2atmpS1943 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1943] = _M0L6_2atmpS1944;
      _M0L6_2atmpS1950 = _M0Lm5indexS702;
      _M0L6_2atmpS1947 = _M0L6_2atmpS1950 + 2;
      _M0L6_2atmpS1949 = 48 + _M0L1cS717;
      _M0L6_2atmpS1948 = _M0L6_2atmpS1949 & 0xff;
      if (
        _M0L6_2atmpS1947 < 0
        || _M0L6_2atmpS1947 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1947] = _M0L6_2atmpS1948;
      _M0L6_2atmpS1951 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS1951 + 3;
    } else {
      int32_t _M0L6_2atmpS1956 = _M0Lm3expS707;
      if (_M0L6_2atmpS1956 >= 10) {
        int32_t _M0L6_2atmpS1966 = _M0Lm3expS707;
        int32_t _M0L1aS718 = _M0L6_2atmpS1966 / 10;
        int32_t _M0L6_2atmpS1965 = _M0Lm3expS707;
        int32_t _M0L1bS719 = _M0L6_2atmpS1965 % 10;
        int32_t _M0L6_2atmpS1957 = _M0Lm5indexS702;
        int32_t _M0L6_2atmpS1959 = 48 + _M0L1aS718;
        int32_t _M0L6_2atmpS1958 = _M0L6_2atmpS1959 & 0xff;
        int32_t _M0L6_2atmpS1963;
        int32_t _M0L6_2atmpS1960;
        int32_t _M0L6_2atmpS1962;
        int32_t _M0L6_2atmpS1961;
        int32_t _M0L6_2atmpS1964;
        if (
          _M0L6_2atmpS1957 < 0
          || _M0L6_2atmpS1957 >= Moonbit_array_length(_M0L6resultS701)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS701[_M0L6_2atmpS1957] = _M0L6_2atmpS1958;
        _M0L6_2atmpS1963 = _M0Lm5indexS702;
        _M0L6_2atmpS1960 = _M0L6_2atmpS1963 + 1;
        _M0L6_2atmpS1962 = 48 + _M0L1bS719;
        _M0L6_2atmpS1961 = _M0L6_2atmpS1962 & 0xff;
        if (
          _M0L6_2atmpS1960 < 0
          || _M0L6_2atmpS1960 >= Moonbit_array_length(_M0L6resultS701)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS701[_M0L6_2atmpS1960] = _M0L6_2atmpS1961;
        _M0L6_2atmpS1964 = _M0Lm5indexS702;
        _M0Lm5indexS702 = _M0L6_2atmpS1964 + 2;
      } else {
        int32_t _M0L6_2atmpS1967 = _M0Lm5indexS702;
        int32_t _M0L6_2atmpS1970 = _M0Lm3expS707;
        int32_t _M0L6_2atmpS1969 = 48 + _M0L6_2atmpS1970;
        int32_t _M0L6_2atmpS1968 = _M0L6_2atmpS1969 & 0xff;
        int32_t _M0L6_2atmpS1971;
        if (
          _M0L6_2atmpS1967 < 0
          || _M0L6_2atmpS1967 >= Moonbit_array_length(_M0L6resultS701)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS701[_M0L6_2atmpS1967] = _M0L6_2atmpS1968;
        _M0L6_2atmpS1971 = _M0Lm5indexS702;
        _M0Lm5indexS702 = _M0L6_2atmpS1971 + 1;
      }
    }
    _M0L6_2atmpS1972 = _M0Lm5indexS702;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2494
    = _M0FPB19string__from__bytes(_M0L6resultS701, 0, _M0L6_2atmpS1972);
    moonbit_decref_cycle_free(_M0L6resultS701);
    return _result_2494;
  } else {
    int32_t _M0L6_2atmpS1981 = _M0Lm3expS707;
    int32_t _M0L6_2atmpS2044;
    moonbit_string_t _result_2500;
    if (_M0L6_2atmpS1981 < 0) {
      int32_t _M0L6_2atmpS1982 = _M0Lm5indexS702;
      int32_t _M0L6_2atmpS1984;
      int32_t _M0L6_2atmpS1983;
      int32_t _M0L6_2atmpS1985;
      int32_t _M0L1iS720;
      int32_t _M0L6_2atmpS2000;
      int32_t _M0L6_2atmpS2002;
      int32_t _M0L6_2atmpS2001;
      int32_t _M0L7currentS722;
      int32_t _M0L1iS723;
      uint64_t _M0L6outputS724;
      if (
        _M0L6_2atmpS1982 < 0
        || _M0L6_2atmpS1982 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1982] = 48;
      _M0L6_2atmpS1984 = _M0Lm5indexS702;
      _M0L6_2atmpS1983 = _M0L6_2atmpS1984 + 1;
      if (
        _M0L6_2atmpS1983 < 0
        || _M0L6_2atmpS1983 >= Moonbit_array_length(_M0L6resultS701)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS701[_M0L6_2atmpS1983] = 46;
      _M0L6_2atmpS1985 = _M0Lm5indexS702;
      _M0Lm5indexS702 = _M0L6_2atmpS1985 + 2;
      _M0L1iS720 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1986 = _M0Lm3expS707;
        if (_M0L1iS720 > _M0L6_2atmpS1986) {
          int32_t _M0L6_2atmpS1989 = _M0Lm5indexS702;
          int32_t _M0L6_2atmpS1988 = _M0L6_2atmpS1989 - _M0L1iS720;
          int32_t _M0L6_2atmpS1987 = _M0L6_2atmpS1988 - 1;
          int32_t _M0L6_2atmpS1990;
          if (
            _M0L6_2atmpS1987 < 0
            || _M0L6_2atmpS1987 >= Moonbit_array_length(_M0L6resultS701)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS701[_M0L6_2atmpS1987] = 48;
          _M0L6_2atmpS1990 = _M0L1iS720 - 1;
          _M0L1iS720 = _M0L6_2atmpS1990;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2000 = _M0Lm5indexS702;
      _M0L6_2atmpS2002 = _M0Lm3expS707;
      _M0L6_2atmpS2001 = -1 - _M0L6_2atmpS2002;
      _M0L7currentS722 = _M0L6_2atmpS2000 + _M0L6_2atmpS2001;
      _M0L1iS723 = 0;
      _M0L6outputS724 = _M0L6outputS704;
      while (1) {
        if (_M0L1iS723 < _M0L7olengthS706) {
          int32_t _M0L6_2atmpS1997 = _M0L7currentS722 + _M0L7olengthS706;
          int32_t _M0L6_2atmpS1996 = _M0L6_2atmpS1997 - _M0L1iS723;
          int32_t _M0L6_2atmpS1991 = _M0L6_2atmpS1996 - 1;
          uint64_t _M0L6_2atmpS1995 = _M0L6outputS724 % 10ull;
          int32_t _M0L6_2atmpS1994 = (int32_t)_M0L6_2atmpS1995;
          int32_t _M0L6_2atmpS1993 = 48 + _M0L6_2atmpS1994;
          int32_t _M0L6_2atmpS1992 = _M0L6_2atmpS1993 & 0xff;
          int32_t _M0L6_2atmpS1998;
          uint64_t _M0L6_2atmpS1999;
          if (
            _M0L6_2atmpS1991 < 0
            || _M0L6_2atmpS1991 >= Moonbit_array_length(_M0L6resultS701)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS701[_M0L6_2atmpS1991] = _M0L6_2atmpS1992;
          _M0L6_2atmpS1998 = _M0L1iS723 + 1;
          _M0L6_2atmpS1999 = _M0L6outputS724 / 10ull;
          _M0L1iS723 = _M0L6_2atmpS1998;
          _M0L6outputS724 = _M0L6_2atmpS1999;
          continue;
        }
        break;
      }
      _M0Lm5indexS702 = _M0L7currentS722 + _M0L7olengthS706;
    } else {
      int32_t _M0L6_2atmpS2004 = _M0Lm3expS707;
      int32_t _M0L6_2atmpS2003 = _M0L6_2atmpS2004 + 1;
      if (_M0L6_2atmpS2003 >= _M0L7olengthS706) {
        int32_t _M0L1iS726 = 0;
        uint64_t _M0L6outputS727 = _M0L6outputS704;
        int32_t _M0L6_2atmpS2015;
        int32_t _M0L6_2atmpS2020;
        int32_t _M0L7_2abindS729;
        int32_t _M0L1iS730;
        int32_t _M0L6_2atmpS2021;
        int32_t _M0L6_2atmpS2024;
        int32_t _M0L6_2atmpS2023;
        int32_t _M0L6_2atmpS2022;
        while (1) {
          if (_M0L1iS726 < _M0L7olengthS706) {
            int32_t _M0L6_2atmpS2012 = _M0Lm5indexS702;
            int32_t _M0L6_2atmpS2011 = _M0L6_2atmpS2012 + _M0L7olengthS706;
            int32_t _M0L6_2atmpS2010 = _M0L6_2atmpS2011 - _M0L1iS726;
            int32_t _M0L6_2atmpS2005 = _M0L6_2atmpS2010 - 1;
            uint64_t _M0L6_2atmpS2009 = _M0L6outputS727 % 10ull;
            int32_t _M0L6_2atmpS2008 = (int32_t)_M0L6_2atmpS2009;
            int32_t _M0L6_2atmpS2007 = 48 + _M0L6_2atmpS2008;
            int32_t _M0L6_2atmpS2006 = _M0L6_2atmpS2007 & 0xff;
            int32_t _M0L6_2atmpS2013;
            uint64_t _M0L6_2atmpS2014;
            if (
              _M0L6_2atmpS2005 < 0
              || _M0L6_2atmpS2005 >= Moonbit_array_length(_M0L6resultS701)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS701[_M0L6_2atmpS2005] = _M0L6_2atmpS2006;
            _M0L6_2atmpS2013 = _M0L1iS726 + 1;
            _M0L6_2atmpS2014 = _M0L6outputS727 / 10ull;
            _M0L1iS726 = _M0L6_2atmpS2013;
            _M0L6outputS727 = _M0L6_2atmpS2014;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2015 = _M0Lm5indexS702;
        _M0Lm5indexS702 = _M0L6_2atmpS2015 + _M0L7olengthS706;
        _M0L6_2atmpS2020 = _M0Lm3expS707;
        _M0L7_2abindS729 = _M0L6_2atmpS2020 + 1;
        _M0L1iS730 = _M0L7olengthS706;
        while (1) {
          if (_M0L1iS730 < _M0L7_2abindS729) {
            int32_t _M0L6_2atmpS2018 = _M0Lm5indexS702;
            int32_t _M0L6_2atmpS2017 = _M0L6_2atmpS2018 + _M0L1iS730;
            int32_t _M0L6_2atmpS2016 = _M0L6_2atmpS2017 - _M0L7olengthS706;
            int32_t _M0L6_2atmpS2019;
            if (
              _M0L6_2atmpS2016 < 0
              || _M0L6_2atmpS2016 >= Moonbit_array_length(_M0L6resultS701)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS701[_M0L6_2atmpS2016] = 48;
            _M0L6_2atmpS2019 = _M0L1iS730 + 1;
            _M0L1iS730 = _M0L6_2atmpS2019;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2021 = _M0Lm5indexS702;
        _M0L6_2atmpS2024 = _M0Lm3expS707;
        _M0L6_2atmpS2023 = _M0L6_2atmpS2024 + 1;
        _M0L6_2atmpS2022 = _M0L6_2atmpS2023 - _M0L7olengthS706;
        _M0Lm5indexS702 = _M0L6_2atmpS2021 + _M0L6_2atmpS2022;
      } else {
        int32_t _M0L6_2atmpS2041 = _M0Lm5indexS702;
        int32_t _M0L6_2atmpS2040 = _M0L6_2atmpS2041 + 1;
        int32_t _M0L1iS732 = 0;
        int32_t _M0L7currentS733 = _M0L6_2atmpS2040;
        uint64_t _M0L6outputS734 = _M0L6outputS704;
        int32_t _M0L6_2atmpS2042;
        int32_t _M0L6_2atmpS2043;
        while (1) {
          if (_M0L1iS732 < _M0L7olengthS706) {
            int32_t _M0L6_2atmpS2036 = _M0L7olengthS706 - _M0L1iS732;
            int32_t _M0L6_2atmpS2034 = _M0L6_2atmpS2036 - 1;
            int32_t _M0L6_2atmpS2035 = _M0Lm3expS707;
            int32_t _M0L7currentS735;
            int32_t _M0L6_2atmpS2031;
            int32_t _M0L6_2atmpS2030;
            int32_t _M0L6_2atmpS2025;
            uint64_t _M0L6_2atmpS2029;
            int32_t _M0L6_2atmpS2028;
            int32_t _M0L6_2atmpS2027;
            int32_t _M0L6_2atmpS2026;
            int32_t _M0L6_2atmpS2032;
            uint64_t _M0L6_2atmpS2033;
            if (_M0L6_2atmpS2034 == _M0L6_2atmpS2035) {
              int32_t _M0L6_2atmpS2039 = _M0L7currentS733 + _M0L7olengthS706;
              int32_t _M0L6_2atmpS2038 = _M0L6_2atmpS2039 - _M0L1iS732;
              int32_t _M0L6_2atmpS2037 = _M0L6_2atmpS2038 - 1;
              if (
                _M0L6_2atmpS2037 < 0
                || _M0L6_2atmpS2037 >= Moonbit_array_length(_M0L6resultS701)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS701[_M0L6_2atmpS2037] = 46;
              _M0L7currentS735 = _M0L7currentS733 - 1;
            } else {
              _M0L7currentS735 = _M0L7currentS733;
            }
            _M0L6_2atmpS2031 = _M0L7currentS735 + _M0L7olengthS706;
            _M0L6_2atmpS2030 = _M0L6_2atmpS2031 - _M0L1iS732;
            _M0L6_2atmpS2025 = _M0L6_2atmpS2030 - 1;
            _M0L6_2atmpS2029 = _M0L6outputS734 % 10ull;
            _M0L6_2atmpS2028 = (int32_t)_M0L6_2atmpS2029;
            _M0L6_2atmpS2027 = 48 + _M0L6_2atmpS2028;
            _M0L6_2atmpS2026 = _M0L6_2atmpS2027 & 0xff;
            if (
              _M0L6_2atmpS2025 < 0
              || _M0L6_2atmpS2025 >= Moonbit_array_length(_M0L6resultS701)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS701[_M0L6_2atmpS2025] = _M0L6_2atmpS2026;
            _M0L6_2atmpS2032 = _M0L1iS732 + 1;
            _M0L6_2atmpS2033 = _M0L6outputS734 / 10ull;
            _M0L1iS732 = _M0L6_2atmpS2032;
            _M0L7currentS733 = _M0L7currentS735;
            _M0L6outputS734 = _M0L6_2atmpS2033;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2042 = _M0Lm5indexS702;
        _M0L6_2atmpS2043 = _M0L7olengthS706 + 1;
        _M0Lm5indexS702 = _M0L6_2atmpS2042 + _M0L6_2atmpS2043;
      }
    }
    _M0L6_2atmpS2044 = _M0Lm5indexS702;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2500
    = _M0FPB19string__from__bytes(_M0L6resultS701, 0, _M0L6_2atmpS2044);
    moonbit_decref_cycle_free(_M0L6resultS701);
    return _result_2500;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS647,
  uint32_t _M0L12ieeeExponentS646
) {
  int32_t _M0Lm2e2S644;
  uint64_t _M0Lm2m2S645;
  uint64_t _M0L6_2atmpS1918;
  uint64_t _M0L6_2atmpS1917;
  int32_t _M0L4evenS648;
  uint64_t _M0L6_2atmpS1916;
  uint64_t _M0L2mvS649;
  int32_t _M0L7mmShiftS650;
  uint64_t _M0Lm2vrS651;
  uint64_t _M0Lm2vpS652;
  uint64_t _M0Lm2vmS653;
  int32_t _M0Lm3e10S654;
  int32_t _M0Lm17vmIsTrailingZerosS655;
  int32_t _M0Lm17vrIsTrailingZerosS656;
  int32_t _M0L6_2atmpS1818;
  int32_t _M0Lm7removedS675;
  int32_t _M0Lm16lastRemovedDigitS676;
  uint64_t _M0Lm6outputS677;
  int32_t _M0L6_2atmpS1914;
  int32_t _M0L6_2atmpS1915;
  int32_t _M0L3expS700;
  uint64_t _M0L6_2atmpS1913;
  struct _M0TPB17FloatingDecimal64* _block_2506;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S644 = 0;
  _M0Lm2m2S645 = 0ull;
  if (_M0L12ieeeExponentS646 == 0u) {
    _M0Lm2e2S644 = -1076;
    _M0Lm2m2S645 = _M0L12ieeeMantissaS647;
  } else {
    int32_t _M0L6_2atmpS1817 = *(int32_t*)&_M0L12ieeeExponentS646;
    int32_t _M0L6_2atmpS1816 = _M0L6_2atmpS1817 - 1023;
    int32_t _M0L6_2atmpS1815 = _M0L6_2atmpS1816 - 52;
    _M0Lm2e2S644 = _M0L6_2atmpS1815 - 2;
    _M0Lm2m2S645 = 4503599627370496ull | _M0L12ieeeMantissaS647;
  }
  _M0L6_2atmpS1918 = _M0Lm2m2S645;
  _M0L6_2atmpS1917 = _M0L6_2atmpS1918 & 1ull;
  _M0L4evenS648 = _M0L6_2atmpS1917 == 0ull;
  _M0L6_2atmpS1916 = _M0Lm2m2S645;
  _M0L2mvS649 = 4ull * _M0L6_2atmpS1916;
  _M0L7mmShiftS650
  = _M0L12ieeeMantissaS647 != 0ull || _M0L12ieeeExponentS646 <= 1u;
  _M0Lm2vrS651 = 0ull;
  _M0Lm2vpS652 = 0ull;
  _M0Lm2vmS653 = 0ull;
  _M0Lm3e10S654 = 0;
  _M0Lm17vmIsTrailingZerosS655 = 0;
  _M0Lm17vrIsTrailingZerosS656 = 0;
  _M0L6_2atmpS1818 = _M0Lm2e2S644;
  if (_M0L6_2atmpS1818 >= 0) {
    int32_t _M0L6_2atmpS1840 = _M0Lm2e2S644;
    int32_t _M0L6_2atmpS1836;
    int32_t _M0L6_2atmpS1839;
    int32_t _M0L6_2atmpS1838;
    int32_t _M0L6_2atmpS1837;
    int32_t _M0L1qS657;
    int32_t _M0L6_2atmpS1835;
    int32_t _M0L6_2atmpS1834;
    int32_t _M0L1kS658;
    int32_t _M0L6_2atmpS1833;
    int32_t _M0L6_2atmpS1832;
    int32_t _M0L6_2atmpS1831;
    int32_t _M0L1iS659;
    struct _M0TPB8Pow5Pair _M0L4pow5S660;
    uint64_t _M0L6_2atmpS1830;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS661;
    uint64_t _M0L8_2avrOutS662;
    uint64_t _M0L8_2avpOutS663;
    uint64_t _M0L8_2avmOutS664;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1836 = _M0FPB9log10Pow2(_M0L6_2atmpS1840);
    _M0L6_2atmpS1839 = _M0Lm2e2S644;
    _M0L6_2atmpS1838 = _M0L6_2atmpS1839 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1837 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1838);
    _M0L1qS657 = _M0L6_2atmpS1836 - _M0L6_2atmpS1837;
    _M0Lm3e10S654 = _M0L1qS657;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1835 = _M0FPB8pow5bits(_M0L1qS657);
    _M0L6_2atmpS1834 = 125 + _M0L6_2atmpS1835;
    _M0L1kS658 = _M0L6_2atmpS1834 - 1;
    _M0L6_2atmpS1833 = _M0Lm2e2S644;
    _M0L6_2atmpS1832 = -_M0L6_2atmpS1833;
    _M0L6_2atmpS1831 = _M0L6_2atmpS1832 + _M0L1qS657;
    _M0L1iS659 = _M0L6_2atmpS1831 + _M0L1kS658;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S660 = _M0FPB22double__computeInvPow5(_M0L1qS657);
    _M0L6_2atmpS1830 = _M0Lm2m2S645;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS661
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1830, _M0L4pow5S660, _M0L1iS659, _M0L7mmShiftS650);
    _M0L8_2avrOutS662 = _M0L7_2abindS661.$0;
    _M0L8_2avpOutS663 = _M0L7_2abindS661.$1;
    _M0L8_2avmOutS664 = _M0L7_2abindS661.$2;
    _M0Lm2vrS651 = _M0L8_2avrOutS662;
    _M0Lm2vpS652 = _M0L8_2avpOutS663;
    _M0Lm2vmS653 = _M0L8_2avmOutS664;
    if (_M0L1qS657 <= 21) {
      int32_t _M0L6_2atmpS1826 = (int32_t)_M0L2mvS649;
      uint64_t _M0L6_2atmpS1829 = _M0L2mvS649 / 5ull;
      int32_t _M0L6_2atmpS1828 = (int32_t)_M0L6_2atmpS1829;
      int32_t _M0L6_2atmpS1827 = 5 * _M0L6_2atmpS1828;
      int32_t _M0L6mvMod5S665 = _M0L6_2atmpS1826 - _M0L6_2atmpS1827;
      if (_M0L6mvMod5S665 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS656
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS649, _M0L1qS657);
      } else if (_M0L4evenS648) {
        uint64_t _M0L6_2atmpS1820 = _M0L2mvS649 - 1ull;
        uint64_t _M0L6_2atmpS1821;
        uint64_t _M0L6_2atmpS1819;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1821 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS650);
        _M0L6_2atmpS1819 = _M0L6_2atmpS1820 - _M0L6_2atmpS1821;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS655
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1819, _M0L1qS657);
      } else {
        uint64_t _M0L6_2atmpS1822 = _M0Lm2vpS652;
        uint64_t _M0L6_2atmpS1825 = _M0L2mvS649 + 2ull;
        int32_t _M0L6_2atmpS1824;
        uint64_t _M0L6_2atmpS1823;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1824
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1825, _M0L1qS657);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1823 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1824);
        _M0Lm2vpS652 = _M0L6_2atmpS1822 - _M0L6_2atmpS1823;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1854 = _M0Lm2e2S644;
    int32_t _M0L6_2atmpS1853 = -_M0L6_2atmpS1854;
    int32_t _M0L6_2atmpS1848;
    int32_t _M0L6_2atmpS1852;
    int32_t _M0L6_2atmpS1851;
    int32_t _M0L6_2atmpS1850;
    int32_t _M0L6_2atmpS1849;
    int32_t _M0L1qS666;
    int32_t _M0L6_2atmpS1841;
    int32_t _M0L6_2atmpS1847;
    int32_t _M0L6_2atmpS1846;
    int32_t _M0L1iS667;
    int32_t _M0L6_2atmpS1845;
    int32_t _M0L1kS668;
    int32_t _M0L1jS669;
    struct _M0TPB8Pow5Pair _M0L4pow5S670;
    uint64_t _M0L6_2atmpS1844;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS671;
    uint64_t _M0L8_2avrOutS672;
    uint64_t _M0L8_2avpOutS673;
    uint64_t _M0L8_2avmOutS674;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1848 = _M0FPB9log10Pow5(_M0L6_2atmpS1853);
    _M0L6_2atmpS1852 = _M0Lm2e2S644;
    _M0L6_2atmpS1851 = -_M0L6_2atmpS1852;
    _M0L6_2atmpS1850 = _M0L6_2atmpS1851 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1849 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1850);
    _M0L1qS666 = _M0L6_2atmpS1848 - _M0L6_2atmpS1849;
    _M0L6_2atmpS1841 = _M0Lm2e2S644;
    _M0Lm3e10S654 = _M0L1qS666 + _M0L6_2atmpS1841;
    _M0L6_2atmpS1847 = _M0Lm2e2S644;
    _M0L6_2atmpS1846 = -_M0L6_2atmpS1847;
    _M0L1iS667 = _M0L6_2atmpS1846 - _M0L1qS666;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1845 = _M0FPB8pow5bits(_M0L1iS667);
    _M0L1kS668 = _M0L6_2atmpS1845 - 125;
    _M0L1jS669 = _M0L1qS666 - _M0L1kS668;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S670 = _M0FPB19double__computePow5(_M0L1iS667);
    _M0L6_2atmpS1844 = _M0Lm2m2S645;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS671
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1844, _M0L4pow5S670, _M0L1jS669, _M0L7mmShiftS650);
    _M0L8_2avrOutS672 = _M0L7_2abindS671.$0;
    _M0L8_2avpOutS673 = _M0L7_2abindS671.$1;
    _M0L8_2avmOutS674 = _M0L7_2abindS671.$2;
    _M0Lm2vrS651 = _M0L8_2avrOutS672;
    _M0Lm2vpS652 = _M0L8_2avpOutS673;
    _M0Lm2vmS653 = _M0L8_2avmOutS674;
    if (_M0L1qS666 <= 1) {
      _M0Lm17vrIsTrailingZerosS656 = 1;
      if (_M0L4evenS648) {
        int32_t _M0L6_2atmpS1842;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1842 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS650);
        _M0Lm17vmIsTrailingZerosS655 = _M0L6_2atmpS1842 == 1;
      } else {
        uint64_t _M0L6_2atmpS1843 = _M0Lm2vpS652;
        _M0Lm2vpS652 = _M0L6_2atmpS1843 - 1ull;
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
    int32_t _if__result_2503;
    uint64_t _M0L6_2atmpS1884;
    uint64_t _M0L6_2atmpS1890;
    uint64_t _M0L6_2atmpS1891;
    int32_t _if__result_2504;
    int32_t _M0L6_2atmpS1887;
    int64_t _M0L6_2atmpS1886;
    uint64_t _M0L6_2atmpS1885;
    while (1) {
      uint64_t _M0L6_2atmpS1867 = _M0Lm2vpS652;
      uint64_t _M0L7vpDiv10S678 = _M0L6_2atmpS1867 / 10ull;
      uint64_t _M0L6_2atmpS1866 = _M0Lm2vmS653;
      uint64_t _M0L7vmDiv10S679 = _M0L6_2atmpS1866 / 10ull;
      uint64_t _M0L6_2atmpS1865;
      int32_t _M0L6_2atmpS1862;
      int32_t _M0L6_2atmpS1864;
      int32_t _M0L6_2atmpS1863;
      int32_t _M0L7vmMod10S681;
      uint64_t _M0L6_2atmpS1861;
      uint64_t _M0L7vrDiv10S682;
      uint64_t _M0L6_2atmpS1860;
      int32_t _M0L6_2atmpS1857;
      int32_t _M0L6_2atmpS1859;
      int32_t _M0L6_2atmpS1858;
      int32_t _M0L7vrMod10S683;
      int32_t _M0L6_2atmpS1856;
      if (_M0L7vpDiv10S678 <= _M0L7vmDiv10S679) {
        break;
      }
      _M0L6_2atmpS1865 = _M0Lm2vmS653;
      _M0L6_2atmpS1862 = (int32_t)_M0L6_2atmpS1865;
      _M0L6_2atmpS1864 = (int32_t)_M0L7vmDiv10S679;
      _M0L6_2atmpS1863 = 10 * _M0L6_2atmpS1864;
      _M0L7vmMod10S681 = _M0L6_2atmpS1862 - _M0L6_2atmpS1863;
      _M0L6_2atmpS1861 = _M0Lm2vrS651;
      _M0L7vrDiv10S682 = _M0L6_2atmpS1861 / 10ull;
      _M0L6_2atmpS1860 = _M0Lm2vrS651;
      _M0L6_2atmpS1857 = (int32_t)_M0L6_2atmpS1860;
      _M0L6_2atmpS1859 = (int32_t)_M0L7vrDiv10S682;
      _M0L6_2atmpS1858 = 10 * _M0L6_2atmpS1859;
      _M0L7vrMod10S683 = _M0L6_2atmpS1857 - _M0L6_2atmpS1858;
      _M0Lm17vmIsTrailingZerosS655
      = _M0Lm17vmIsTrailingZerosS655 && _M0L7vmMod10S681 == 0;
      if (_M0Lm17vrIsTrailingZerosS656) {
        int32_t _M0L6_2atmpS1855 = _M0Lm16lastRemovedDigitS676;
        _M0Lm17vrIsTrailingZerosS656 = _M0L6_2atmpS1855 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS656 = 0;
      }
      _M0Lm16lastRemovedDigitS676 = _M0L7vrMod10S683;
      _M0Lm2vrS651 = _M0L7vrDiv10S682;
      _M0Lm2vpS652 = _M0L7vpDiv10S678;
      _M0Lm2vmS653 = _M0L7vmDiv10S679;
      _M0L6_2atmpS1856 = _M0Lm7removedS675;
      _M0Lm7removedS675 = _M0L6_2atmpS1856 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS655) {
      while (1) {
        uint64_t _M0L6_2atmpS1880 = _M0Lm2vmS653;
        uint64_t _M0L7vmDiv10S684 = _M0L6_2atmpS1880 / 10ull;
        uint64_t _M0L6_2atmpS1879 = _M0Lm2vmS653;
        int32_t _M0L6_2atmpS1876 = (int32_t)_M0L6_2atmpS1879;
        int32_t _M0L6_2atmpS1878 = (int32_t)_M0L7vmDiv10S684;
        int32_t _M0L6_2atmpS1877 = 10 * _M0L6_2atmpS1878;
        int32_t _M0L7vmMod10S685 = _M0L6_2atmpS1876 - _M0L6_2atmpS1877;
        uint64_t _M0L6_2atmpS1875;
        uint64_t _M0L7vpDiv10S687;
        uint64_t _M0L6_2atmpS1874;
        uint64_t _M0L7vrDiv10S688;
        uint64_t _M0L6_2atmpS1873;
        int32_t _M0L6_2atmpS1870;
        int32_t _M0L6_2atmpS1872;
        int32_t _M0L6_2atmpS1871;
        int32_t _M0L7vrMod10S689;
        int32_t _M0L6_2atmpS1869;
        if (_M0L7vmMod10S685 != 0) {
          break;
        }
        _M0L6_2atmpS1875 = _M0Lm2vpS652;
        _M0L7vpDiv10S687 = _M0L6_2atmpS1875 / 10ull;
        _M0L6_2atmpS1874 = _M0Lm2vrS651;
        _M0L7vrDiv10S688 = _M0L6_2atmpS1874 / 10ull;
        _M0L6_2atmpS1873 = _M0Lm2vrS651;
        _M0L6_2atmpS1870 = (int32_t)_M0L6_2atmpS1873;
        _M0L6_2atmpS1872 = (int32_t)_M0L7vrDiv10S688;
        _M0L6_2atmpS1871 = 10 * _M0L6_2atmpS1872;
        _M0L7vrMod10S689 = _M0L6_2atmpS1870 - _M0L6_2atmpS1871;
        if (_M0Lm17vrIsTrailingZerosS656) {
          int32_t _M0L6_2atmpS1868 = _M0Lm16lastRemovedDigitS676;
          _M0Lm17vrIsTrailingZerosS656 = _M0L6_2atmpS1868 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS656 = 0;
        }
        _M0Lm16lastRemovedDigitS676 = _M0L7vrMod10S689;
        _M0Lm2vrS651 = _M0L7vrDiv10S688;
        _M0Lm2vpS652 = _M0L7vpDiv10S687;
        _M0Lm2vmS653 = _M0L7vmDiv10S684;
        _M0L6_2atmpS1869 = _M0Lm7removedS675;
        _M0Lm7removedS675 = _M0L6_2atmpS1869 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS656) {
      int32_t _M0L6_2atmpS1883 = _M0Lm16lastRemovedDigitS676;
      if (_M0L6_2atmpS1883 == 5) {
        uint64_t _M0L6_2atmpS1882 = _M0Lm2vrS651;
        uint64_t _M0L6_2atmpS1881 = _M0L6_2atmpS1882 % 2ull;
        _if__result_2503 = _M0L6_2atmpS1881 == 0ull;
      } else {
        _if__result_2503 = 0;
      }
    } else {
      _if__result_2503 = 0;
    }
    if (_if__result_2503) {
      _M0Lm16lastRemovedDigitS676 = 4;
    }
    _M0L6_2atmpS1884 = _M0Lm2vrS651;
    _M0L6_2atmpS1890 = _M0Lm2vrS651;
    _M0L6_2atmpS1891 = _M0Lm2vmS653;
    if (_M0L6_2atmpS1890 == _M0L6_2atmpS1891) {
      if (!_M0L4evenS648) {
        _if__result_2504 = 1;
      } else {
        int32_t _M0L6_2atmpS1889 = _M0Lm17vmIsTrailingZerosS655;
        _if__result_2504 = !_M0L6_2atmpS1889;
      }
    } else {
      _if__result_2504 = 0;
    }
    if (_if__result_2504) {
      _M0L6_2atmpS1887 = 1;
    } else {
      int32_t _M0L6_2atmpS1888 = _M0Lm16lastRemovedDigitS676;
      _M0L6_2atmpS1887 = _M0L6_2atmpS1888 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1886 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1887);
    _M0L6_2atmpS1885 = *(uint64_t*)&_M0L6_2atmpS1886;
    _M0Lm6outputS677 = _M0L6_2atmpS1884 + _M0L6_2atmpS1885;
  } else {
    int32_t _M0Lm7roundUpS690 = 0;
    uint64_t _M0L6_2atmpS1912 = _M0Lm2vpS652;
    uint64_t _M0L8vpDiv100S691 = _M0L6_2atmpS1912 / 100ull;
    uint64_t _M0L6_2atmpS1911 = _M0Lm2vmS653;
    uint64_t _M0L8vmDiv100S692 = _M0L6_2atmpS1911 / 100ull;
    uint64_t _M0L6_2atmpS1906;
    uint64_t _M0L6_2atmpS1909;
    uint64_t _M0L6_2atmpS1910;
    int32_t _M0L6_2atmpS1908;
    uint64_t _M0L6_2atmpS1907;
    if (_M0L8vpDiv100S691 > _M0L8vmDiv100S692) {
      uint64_t _M0L6_2atmpS1897 = _M0Lm2vrS651;
      uint64_t _M0L8vrDiv100S693 = _M0L6_2atmpS1897 / 100ull;
      uint64_t _M0L6_2atmpS1896 = _M0Lm2vrS651;
      int32_t _M0L6_2atmpS1893 = (int32_t)_M0L6_2atmpS1896;
      int32_t _M0L6_2atmpS1895 = (int32_t)_M0L8vrDiv100S693;
      int32_t _M0L6_2atmpS1894 = 100 * _M0L6_2atmpS1895;
      int32_t _M0L8vrMod100S694 = _M0L6_2atmpS1893 - _M0L6_2atmpS1894;
      int32_t _M0L6_2atmpS1892;
      _M0Lm7roundUpS690 = _M0L8vrMod100S694 >= 50;
      _M0Lm2vrS651 = _M0L8vrDiv100S693;
      _M0Lm2vpS652 = _M0L8vpDiv100S691;
      _M0Lm2vmS653 = _M0L8vmDiv100S692;
      _M0L6_2atmpS1892 = _M0Lm7removedS675;
      _M0Lm7removedS675 = _M0L6_2atmpS1892 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1905 = _M0Lm2vpS652;
      uint64_t _M0L7vpDiv10S695 = _M0L6_2atmpS1905 / 10ull;
      uint64_t _M0L6_2atmpS1904 = _M0Lm2vmS653;
      uint64_t _M0L7vmDiv10S696 = _M0L6_2atmpS1904 / 10ull;
      uint64_t _M0L6_2atmpS1903;
      uint64_t _M0L7vrDiv10S698;
      uint64_t _M0L6_2atmpS1902;
      int32_t _M0L6_2atmpS1899;
      int32_t _M0L6_2atmpS1901;
      int32_t _M0L6_2atmpS1900;
      int32_t _M0L7vrMod10S699;
      int32_t _M0L6_2atmpS1898;
      if (_M0L7vpDiv10S695 <= _M0L7vmDiv10S696) {
        break;
      }
      _M0L6_2atmpS1903 = _M0Lm2vrS651;
      _M0L7vrDiv10S698 = _M0L6_2atmpS1903 / 10ull;
      _M0L6_2atmpS1902 = _M0Lm2vrS651;
      _M0L6_2atmpS1899 = (int32_t)_M0L6_2atmpS1902;
      _M0L6_2atmpS1901 = (int32_t)_M0L7vrDiv10S698;
      _M0L6_2atmpS1900 = 10 * _M0L6_2atmpS1901;
      _M0L7vrMod10S699 = _M0L6_2atmpS1899 - _M0L6_2atmpS1900;
      _M0Lm7roundUpS690 = _M0L7vrMod10S699 >= 5;
      _M0Lm2vrS651 = _M0L7vrDiv10S698;
      _M0Lm2vpS652 = _M0L7vpDiv10S695;
      _M0Lm2vmS653 = _M0L7vmDiv10S696;
      _M0L6_2atmpS1898 = _M0Lm7removedS675;
      _M0Lm7removedS675 = _M0L6_2atmpS1898 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1906 = _M0Lm2vrS651;
    _M0L6_2atmpS1909 = _M0Lm2vrS651;
    _M0L6_2atmpS1910 = _M0Lm2vmS653;
    _M0L6_2atmpS1908
    = _M0L6_2atmpS1909 == _M0L6_2atmpS1910 || _M0Lm7roundUpS690;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1907 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1908);
    _M0Lm6outputS677 = _M0L6_2atmpS1906 + _M0L6_2atmpS1907;
  }
  _M0L6_2atmpS1914 = _M0Lm3e10S654;
  _M0L6_2atmpS1915 = _M0Lm7removedS675;
  _M0L3expS700 = _M0L6_2atmpS1914 + _M0L6_2atmpS1915;
  _M0L6_2atmpS1913 = _M0Lm6outputS677;
  _block_2506
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2506)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2506->$0 = _M0L6_2atmpS1913;
  _block_2506->$1 = _M0L3expS700;
  return _block_2506;
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
  int32_t _M0L6_2atmpS1814;
  int32_t _M0L6_2atmpS1813;
  int32_t _M0L4baseS622;
  int32_t _M0L5base2S624;
  int32_t _M0L6offsetS625;
  int32_t _M0L6_2atmpS1812;
  uint64_t _M0L4mul0S626;
  int32_t _M0L6_2atmpS1811;
  int32_t _M0L6_2atmpS1810;
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
  int32_t _M0L6_2atmpS1808;
  int32_t _M0L6_2atmpS1809;
  int32_t _M0L5deltaS637;
  uint64_t _M0L6_2atmpS1807;
  uint64_t _M0L6_2atmpS1799;
  int32_t _M0L6_2atmpS1806;
  uint32_t _M0L6_2atmpS1803;
  int32_t _M0L6_2atmpS1805;
  int32_t _M0L6_2atmpS1804;
  uint32_t _M0L6_2atmpS1802;
  uint32_t _M0L6_2atmpS1801;
  uint64_t _M0L6_2atmpS1800;
  uint64_t _M0L1aS638;
  uint64_t _M0L6_2atmpS1798;
  uint64_t _M0L1bS639;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1814 = _M0L1iS623 + 26;
  _M0L6_2atmpS1813 = _M0L6_2atmpS1814 - 1;
  _M0L4baseS622 = _M0L6_2atmpS1813 / 26;
  _M0L5base2S624 = _M0L4baseS622 * 26;
  _M0L6offsetS625 = _M0L5base2S624 - _M0L1iS623;
  _M0L6_2atmpS1812 = _M0L4baseS622 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S626
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1812);
  _M0L6_2atmpS1811 = _M0L4baseS622 * 2;
  _M0L6_2atmpS1810 = _M0L6_2atmpS1811 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S627
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1810);
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
    uint64_t _M0L6_2atmpS1797 = _M0Lm5high1S636;
    _M0Lm5high1S636 = _M0L6_2atmpS1797 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1808 = _M0FPB8pow5bits(_M0L5base2S624);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1809 = _M0FPB8pow5bits(_M0L1iS623);
  _M0L5deltaS637 = _M0L6_2atmpS1808 - _M0L6_2atmpS1809;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1807
  = _M0FPB13shiftright128(_M0L7_2alow0S633, _M0L3sumS635, _M0L5deltaS637);
  _M0L6_2atmpS1799 = _M0L6_2atmpS1807 + 1ull;
  _M0L6_2atmpS1806 = _M0L1iS623 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1803
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1806);
  _M0L6_2atmpS1805 = _M0L1iS623 % 16;
  _M0L6_2atmpS1804 = _M0L6_2atmpS1805 << 1;
  _M0L6_2atmpS1802 = _M0L6_2atmpS1803 >> (_M0L6_2atmpS1804 & 31);
  _M0L6_2atmpS1801 = _M0L6_2atmpS1802 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1800 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1801);
  _M0L1aS638 = _M0L6_2atmpS1799 + _M0L6_2atmpS1800;
  _M0L6_2atmpS1798 = _M0Lm5high1S636;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS639
  = _M0FPB13shiftright128(_M0L3sumS635, _M0L6_2atmpS1798, _M0L5deltaS637);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS638, .$1 = _M0L1bS639};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS605) {
  int32_t _M0L4baseS604;
  int32_t _M0L5base2S606;
  int32_t _M0L6offsetS607;
  int32_t _M0L6_2atmpS1796;
  uint64_t _M0L4mul0S608;
  int32_t _M0L6_2atmpS1795;
  int32_t _M0L6_2atmpS1794;
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
  int32_t _M0L6_2atmpS1792;
  int32_t _M0L6_2atmpS1793;
  int32_t _M0L5deltaS619;
  uint64_t _M0L6_2atmpS1784;
  int32_t _M0L6_2atmpS1791;
  uint32_t _M0L6_2atmpS1788;
  int32_t _M0L6_2atmpS1790;
  int32_t _M0L6_2atmpS1789;
  uint32_t _M0L6_2atmpS1787;
  uint32_t _M0L6_2atmpS1786;
  uint64_t _M0L6_2atmpS1785;
  uint64_t _M0L1aS620;
  uint64_t _M0L6_2atmpS1783;
  uint64_t _M0L1bS621;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS604 = _M0L1iS605 / 26;
  _M0L5base2S606 = _M0L4baseS604 * 26;
  _M0L6offsetS607 = _M0L1iS605 - _M0L5base2S606;
  _M0L6_2atmpS1796 = _M0L4baseS604 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S608
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1796);
  _M0L6_2atmpS1795 = _M0L4baseS604 * 2;
  _M0L6_2atmpS1794 = _M0L6_2atmpS1795 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S609
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1794);
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
    uint64_t _M0L6_2atmpS1782 = _M0Lm5high1S618;
    _M0Lm5high1S618 = _M0L6_2atmpS1782 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1792 = _M0FPB8pow5bits(_M0L1iS605);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1793 = _M0FPB8pow5bits(_M0L5base2S606);
  _M0L5deltaS619 = _M0L6_2atmpS1792 - _M0L6_2atmpS1793;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1784
  = _M0FPB13shiftright128(_M0L7_2alow0S615, _M0L3sumS617, _M0L5deltaS619);
  _M0L6_2atmpS1791 = _M0L1iS605 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1788
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1791);
  _M0L6_2atmpS1790 = _M0L1iS605 % 16;
  _M0L6_2atmpS1789 = _M0L6_2atmpS1790 << 1;
  _M0L6_2atmpS1787 = _M0L6_2atmpS1788 >> (_M0L6_2atmpS1789 & 31);
  _M0L6_2atmpS1786 = _M0L6_2atmpS1787 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1785 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1786);
  _M0L1aS620 = _M0L6_2atmpS1784 + _M0L6_2atmpS1785;
  _M0L6_2atmpS1783 = _M0Lm5high1S618;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS621
  = _M0FPB13shiftright128(_M0L3sumS617, _M0L6_2atmpS1783, _M0L5deltaS619);
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
  uint64_t _M0L6_2atmpS1781;
  uint64_t _M0L2hiS586;
  uint64_t _M0L3lo2S587;
  uint64_t _M0L6_2atmpS1779;
  uint64_t _M0L6_2atmpS1780;
  uint64_t _M0L4mid2S588;
  uint64_t _M0L6_2atmpS1778;
  uint64_t _M0L3hi2S589;
  int32_t _M0L6_2atmpS1777;
  int32_t _M0L6_2atmpS1776;
  uint64_t _M0L2vpS590;
  uint64_t _M0Lm2vmS592;
  int32_t _M0L6_2atmpS1775;
  int32_t _M0L6_2atmpS1774;
  uint64_t _M0L2vrS603;
  uint64_t _M0L6_2atmpS1773;
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
    _M0L6_2atmpS1781 = 1ull;
  } else {
    _M0L6_2atmpS1781 = 0ull;
  }
  _M0L2hiS586 = _M0L6_2ahi2S584 + _M0L6_2atmpS1781;
  _M0L3lo2S587 = _M0L5_2aloS580 + _M0L7_2amul0S574;
  _M0L6_2atmpS1779 = _M0L3midS585 + _M0L7_2amul1S576;
  if (_M0L3lo2S587 < _M0L5_2aloS580) {
    _M0L6_2atmpS1780 = 1ull;
  } else {
    _M0L6_2atmpS1780 = 0ull;
  }
  _M0L4mid2S588 = _M0L6_2atmpS1779 + _M0L6_2atmpS1780;
  if (_M0L4mid2S588 < _M0L3midS585) {
    _M0L6_2atmpS1778 = 1ull;
  } else {
    _M0L6_2atmpS1778 = 0ull;
  }
  _M0L3hi2S589 = _M0L2hiS586 + _M0L6_2atmpS1778;
  _M0L6_2atmpS1777 = _M0L1jS591 - 64;
  _M0L6_2atmpS1776 = _M0L6_2atmpS1777 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS590
  = _M0FPB13shiftright128(_M0L4mid2S588, _M0L3hi2S589, _M0L6_2atmpS1776);
  _M0Lm2vmS592 = 0ull;
  if (_M0L7mmShiftS593) {
    uint64_t _M0L3lo3S594 = _M0L5_2aloS580 - _M0L7_2amul0S574;
    uint64_t _M0L6_2atmpS1763 = _M0L3midS585 - _M0L7_2amul1S576;
    uint64_t _M0L6_2atmpS1764;
    uint64_t _M0L4mid3S595;
    uint64_t _M0L6_2atmpS1762;
    uint64_t _M0L3hi3S596;
    int32_t _M0L6_2atmpS1761;
    int32_t _M0L6_2atmpS1760;
    if (_M0L5_2aloS580 < _M0L3lo3S594) {
      _M0L6_2atmpS1764 = 1ull;
    } else {
      _M0L6_2atmpS1764 = 0ull;
    }
    _M0L4mid3S595 = _M0L6_2atmpS1763 - _M0L6_2atmpS1764;
    if (_M0L3midS585 < _M0L4mid3S595) {
      _M0L6_2atmpS1762 = 1ull;
    } else {
      _M0L6_2atmpS1762 = 0ull;
    }
    _M0L3hi3S596 = _M0L2hiS586 - _M0L6_2atmpS1762;
    _M0L6_2atmpS1761 = _M0L1jS591 - 64;
    _M0L6_2atmpS1760 = _M0L6_2atmpS1761 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS592
    = _M0FPB13shiftright128(_M0L4mid3S595, _M0L3hi3S596, _M0L6_2atmpS1760);
  } else {
    uint64_t _M0L3lo3S597 = _M0L5_2aloS580 + _M0L5_2aloS580;
    uint64_t _M0L6_2atmpS1771 = _M0L3midS585 + _M0L3midS585;
    uint64_t _M0L6_2atmpS1772;
    uint64_t _M0L4mid3S598;
    uint64_t _M0L6_2atmpS1769;
    uint64_t _M0L6_2atmpS1770;
    uint64_t _M0L3hi3S599;
    uint64_t _M0L3lo4S600;
    uint64_t _M0L6_2atmpS1767;
    uint64_t _M0L6_2atmpS1768;
    uint64_t _M0L4mid4S601;
    uint64_t _M0L6_2atmpS1766;
    uint64_t _M0L3hi4S602;
    int32_t _M0L6_2atmpS1765;
    if (_M0L3lo3S597 < _M0L5_2aloS580) {
      _M0L6_2atmpS1772 = 1ull;
    } else {
      _M0L6_2atmpS1772 = 0ull;
    }
    _M0L4mid3S598 = _M0L6_2atmpS1771 + _M0L6_2atmpS1772;
    _M0L6_2atmpS1769 = _M0L2hiS586 + _M0L2hiS586;
    if (_M0L4mid3S598 < _M0L3midS585) {
      _M0L6_2atmpS1770 = 1ull;
    } else {
      _M0L6_2atmpS1770 = 0ull;
    }
    _M0L3hi3S599 = _M0L6_2atmpS1769 + _M0L6_2atmpS1770;
    _M0L3lo4S600 = _M0L3lo3S597 - _M0L7_2amul0S574;
    _M0L6_2atmpS1767 = _M0L4mid3S598 - _M0L7_2amul1S576;
    if (_M0L3lo3S597 < _M0L3lo4S600) {
      _M0L6_2atmpS1768 = 1ull;
    } else {
      _M0L6_2atmpS1768 = 0ull;
    }
    _M0L4mid4S601 = _M0L6_2atmpS1767 - _M0L6_2atmpS1768;
    if (_M0L4mid3S598 < _M0L4mid4S601) {
      _M0L6_2atmpS1766 = 1ull;
    } else {
      _M0L6_2atmpS1766 = 0ull;
    }
    _M0L3hi4S602 = _M0L3hi3S599 - _M0L6_2atmpS1766;
    _M0L6_2atmpS1765 = _M0L1jS591 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS592
    = _M0FPB13shiftright128(_M0L4mid4S601, _M0L3hi4S602, _M0L6_2atmpS1765);
  }
  _M0L6_2atmpS1775 = _M0L1jS591 - 64;
  _M0L6_2atmpS1774 = _M0L6_2atmpS1775 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS603
  = _M0FPB13shiftright128(_M0L3midS585, _M0L2hiS586, _M0L6_2atmpS1774);
  _M0L6_2atmpS1773 = _M0Lm2vmS592;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS603,
                                                .$1 = _M0L2vpS590,
                                                .$2 = _M0L6_2atmpS1773};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS572,
  int32_t _M0L1pS573
) {
  uint64_t _M0L6_2atmpS1759;
  uint64_t _M0L6_2atmpS1758;
  uint64_t _M0L6_2atmpS1757;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1759 = 1ull << (_M0L1pS573 & 63);
  _M0L6_2atmpS1758 = _M0L6_2atmpS1759 - 1ull;
  _M0L6_2atmpS1757 = _M0L5valueS572 & _M0L6_2atmpS1758;
  return _M0L6_2atmpS1757 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS570,
  int32_t _M0L1pS571
) {
  int32_t _M0L6_2atmpS1756;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1756 = _M0FPB10pow5Factor(_M0L5valueS570);
  return _M0L6_2atmpS1756 >= _M0L1pS571;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS565) {
  uint64_t _M0L6_2atmpS1747;
  uint64_t _M0L6_2atmpS1748;
  uint64_t _M0L6_2atmpS1749;
  uint64_t _M0L6_2atmpS1750;
  uint64_t _M0L6_2atmpS1755;
  int32_t _M0L5countS566;
  uint64_t _M0L1vS567;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1747 = _M0L5valueS565 % 5ull;
  if (_M0L6_2atmpS1747 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1748 = _M0L5valueS565 % 25ull;
  if (_M0L6_2atmpS1748 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1749 = _M0L5valueS565 % 125ull;
  if (_M0L6_2atmpS1749 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1750 = _M0L5valueS565 % 625ull;
  if (_M0L6_2atmpS1750 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1755 = _M0L5valueS565 / 625ull;
  _M0L5countS566 = 4;
  _M0L1vS567 = _M0L6_2atmpS1755;
  while (1) {
    if (_M0L1vS567 > 0ull) {
      uint64_t _M0L6_2atmpS1751 = _M0L1vS567 % 5ull;
      int32_t _M0L6_2atmpS1752;
      uint64_t _M0L6_2atmpS1753;
      if (_M0L6_2atmpS1751 != 0ull) {
        return _M0L5countS566;
      }
      _M0L6_2atmpS1752 = _M0L5countS566 + 1;
      _M0L6_2atmpS1753 = _M0L1vS567 / 5ull;
      _M0L5countS566 = _M0L6_2atmpS1752;
      _M0L1vS567 = _M0L6_2atmpS1753;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS569;
      moonbit_string_t _M0L6_2atmpS1754;
      int32_t _result_2508;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS569
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS569, (moonbit_string_t)moonbit_string_literal_10.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS569, _M0L5valueS565);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1754
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS569);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS569);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2508 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1754);
      moonbit_decref_cycle_free(_M0L6_2atmpS1754);
      return _result_2508;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS564,
  uint64_t _M0L2hiS562,
  int32_t _M0L4distS563
) {
  int32_t _M0L6_2atmpS1746;
  uint64_t _M0L6_2atmpS1744;
  uint64_t _M0L6_2atmpS1745;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1746 = 64 - _M0L4distS563;
  _M0L6_2atmpS1744 = _M0L2hiS562 << (_M0L6_2atmpS1746 & 63);
  _M0L6_2atmpS1745 = _M0L2loS564 >> (_M0L4distS563 & 63);
  return _M0L6_2atmpS1744 | _M0L6_2atmpS1745;
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
  uint64_t _M0L6_2atmpS1742;
  uint64_t _M0L6_2atmpS1743;
  uint64_t _M0L1yS558;
  uint64_t _M0L6_2atmpS1740;
  uint64_t _M0L6_2atmpS1741;
  uint64_t _M0L1zS559;
  uint64_t _M0L6_2atmpS1738;
  uint64_t _M0L6_2atmpS1739;
  uint64_t _M0L6_2atmpS1736;
  uint64_t _M0L6_2atmpS1737;
  uint64_t _M0L1wS560;
  uint64_t _M0L2loS561;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS551 = _M0L1aS552 & 4294967295ull;
  _M0L3aHiS553 = _M0L1aS552 >> 32;
  _M0L3bLoS554 = _M0L1bS555 & 4294967295ull;
  _M0L3bHiS556 = _M0L1bS555 >> 32;
  _M0L1xS557 = _M0L3aLoS551 * _M0L3bLoS554;
  _M0L6_2atmpS1742 = _M0L3aHiS553 * _M0L3bLoS554;
  _M0L6_2atmpS1743 = _M0L1xS557 >> 32;
  _M0L1yS558 = _M0L6_2atmpS1742 + _M0L6_2atmpS1743;
  _M0L6_2atmpS1740 = _M0L3aLoS551 * _M0L3bHiS556;
  _M0L6_2atmpS1741 = _M0L1yS558 & 4294967295ull;
  _M0L1zS559 = _M0L6_2atmpS1740 + _M0L6_2atmpS1741;
  _M0L6_2atmpS1738 = _M0L3aHiS553 * _M0L3bHiS556;
  _M0L6_2atmpS1739 = _M0L1yS558 >> 32;
  _M0L6_2atmpS1736 = _M0L6_2atmpS1738 + _M0L6_2atmpS1739;
  _M0L6_2atmpS1737 = _M0L1zS559 >> 32;
  _M0L1wS560 = _M0L6_2atmpS1736 + _M0L6_2atmpS1737;
  _M0L2loS561 = _M0L1aS552 * _M0L1bS555;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS561, .$1 = _M0L1wS560};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS549,
  int32_t _M0L4fromS546,
  int32_t _M0L2toS545
) {
  int32_t _M0L3lenS544;
  int32_t _M0L6_2atmpS1735;
  uint16_t* _M0L6bufferS547;
  int32_t _M0L1iS548;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS544 = _M0L2toS545 - _M0L4fromS546;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1735 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS547
  = (uint16_t*)moonbit_make_string(_M0L3lenS544, _M0L6_2atmpS1735);
  _M0L1iS548 = 0;
  while (1) {
    if (_M0L1iS548 < _M0L3lenS544) {
      int32_t _M0L6_2atmpS1733 = _M0L4fromS546 + _M0L1iS548;
      int32_t _M0L6_2atmpS1732;
      int32_t _M0L6_2atmpS1731;
      int32_t _M0L6_2atmpS1734;
      if (
        _M0L6_2atmpS1733 < 0
        || _M0L6_2atmpS1733 >= Moonbit_array_length(_M0L5bytesS549)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1732 = (int32_t)_M0L5bytesS549[_M0L6_2atmpS1733];
      _M0L6_2atmpS1731 = (uint16_t)_M0L6_2atmpS1732;
      if (
        _M0L1iS548 < 0 || _M0L1iS548 >= Moonbit_array_length(_M0L6bufferS547)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS547[_M0L1iS548] = _M0L6_2atmpS1731;
      _M0L6_2atmpS1734 = _M0L1iS548 + 1;
      _M0L1iS548 = _M0L6_2atmpS1734;
      continue;
    }
    break;
  }
  return _M0L6bufferS547;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS543) {
  int32_t _M0L6_2atmpS1730;
  uint32_t _M0L6_2atmpS1729;
  uint32_t _M0L6_2atmpS1728;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1730 = _M0L1eS543 * 78913;
  _M0L6_2atmpS1729 = *(uint32_t*)&_M0L6_2atmpS1730;
  _M0L6_2atmpS1728 = _M0L6_2atmpS1729 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1728;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS542) {
  int32_t _M0L6_2atmpS1727;
  uint32_t _M0L6_2atmpS1726;
  uint32_t _M0L6_2atmpS1725;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1727 = _M0L1eS542 * 732923;
  _M0L6_2atmpS1726 = *(uint32_t*)&_M0L6_2atmpS1727;
  _M0L6_2atmpS1725 = _M0L6_2atmpS1726 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1725;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS540,
  int32_t _M0L8exponentS541,
  int32_t _M0L8mantissaS538
) {
  moonbit_string_t _M0L1sS539;
  moonbit_string_t _result_2511;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS538) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  if (_M0L4signS540) {
    _M0L1sS539 = (moonbit_string_t)moonbit_string_literal_12.data;
  } else {
    _M0L1sS539 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS541) {
    moonbit_string_t _result_2510;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2510
    = moonbit_add_string(_M0L1sS539, (moonbit_string_t)moonbit_string_literal_13.data);
    moonbit_decref_cycle_free(_M0L1sS539);
    return _result_2510;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2511
  = moonbit_add_string(_M0L1sS539, (moonbit_string_t)moonbit_string_literal_14.data);
  moonbit_decref_cycle_free(_M0L1sS539);
  return _result_2511;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS537) {
  int32_t _M0L6_2atmpS1724;
  uint32_t _M0L6_2atmpS1723;
  uint32_t _M0L6_2atmpS1722;
  int32_t _M0L6_2atmpS1721;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1724 = _M0L1eS537 * 1217359;
  _M0L6_2atmpS1723 = *(uint32_t*)&_M0L6_2atmpS1724;
  _M0L6_2atmpS1722 = _M0L6_2atmpS1723 >> 19;
  _M0L6_2atmpS1721 = *(int32_t*)&_M0L6_2atmpS1722;
  return _M0L6_2atmpS1721 + 1;
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
  float* _M0L6_2atmpS1717;
  struct _M0TPB5ArrayGfE* _block_2512;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1717 = (float*)moonbit_make_float_array_raw(_M0L3lenS531);
  _block_2512
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2512)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2512->$0 = _M0L6_2atmpS1717;
  _block_2512->$1 = _M0L3lenS531;
  return _block_2512;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS532
) {
  uint8_t* _M0L6_2atmpS1718;
  struct _M0TPB5ArrayGbE* _block_2513;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1718 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS532);
  _block_2513
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2513)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 66, 0);
  _block_2513->$0 = _M0L6_2atmpS1718;
  _block_2513->$1 = _M0L3lenS532;
  return _block_2513;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS533
) {
  int32_t* _M0L6_2atmpS1719;
  struct _M0TPB5ArrayGiE* _block_2514;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1719 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS533);
  _block_2514
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2514)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _block_2514->$0 = _M0L6_2atmpS1719;
  _block_2514->$1 = _M0L3lenS533;
  return _block_2514;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS534
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS1720;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_2515;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1720
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS534, 0);
  _block_2515
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_2515)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 69, 0);
  _block_2515->$0 = _M0L6_2atmpS1720;
  _block_2515->$1 = _M0L3lenS534;
  return _block_2515;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS527,
  int32_t _M0L5indexS528
) {
  uint64_t* _M0L6_2atmpS1715;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1715 = _M0L4selfS527;
  if (
    _M0L5indexS528 < 0
    || _M0L5indexS528 >= Moonbit_array_length(_M0L6_2atmpS1715)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1715[_M0L5indexS528];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS529,
  int32_t _M0L5indexS530
) {
  uint32_t* _M0L6_2atmpS1716;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1716 = _M0L4selfS529;
  if (
    _M0L5indexS530 < 0
    || _M0L5indexS530 >= Moonbit_array_length(_M0L6_2atmpS1716)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1716[_M0L5indexS530];
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
  int32_t _M0L3lenS1687;
  moonbit_string_t* _M0L6_2atmpS1689;
  int32_t _M0L6_2atmpS1688;
  int32_t _M0L6lengthS513;
  moonbit_string_t* _M0L3bufS1692;
  moonbit_string_t _M0L6_2aoldS2365;
  int32_t _M0L6_2atmpS1693;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1687 = _M0L4selfS512->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1689 = _M0MPC15array5Array6bufferGsE(_M0L4selfS512);
  _M0L6_2atmpS1688 = Moonbit_array_length(_M0L6_2atmpS1689);
  moonbit_decref_cycle_free(_M0L6_2atmpS1689);
  if (_M0L3lenS1687 == _M0L6_2atmpS1688) {
    int32_t _M0L3lenS1691 = _M0L4selfS512->$1;
    int32_t _M0L6_2atmpS1690 = _M0L3lenS1691 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS512, _M0L6_2atmpS1690);
  }
  _M0L6lengthS513 = _M0L4selfS512->$1;
  _M0L3bufS1692 = _M0L4selfS512->$0;
  _M0L6_2aoldS2365 = (moonbit_string_t)_M0L3bufS1692[_M0L6lengthS513];
  moonbit_decref_cycle_free(_M0L6_2aoldS2365);
  _M0L3bufS1692[_M0L6lengthS513] = _M0L5valueS514;
  _M0L6_2atmpS1693 = _M0L6lengthS513 + 1;
  _M0L4selfS512->$1 = _M0L6_2atmpS1693;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS515,
  struct _M0TUsiE* _M0L5valueS517
) {
  int32_t _M0L3lenS1694;
  struct _M0TUsiE** _M0L6_2atmpS1696;
  int32_t _M0L6_2atmpS1695;
  int32_t _M0L6lengthS516;
  struct _M0TUsiE** _M0L3bufS1699;
  struct _M0TUsiE* _M0L6_2aoldS2366;
  int32_t _M0L6_2atmpS1700;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1694 = _M0L4selfS515->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1696 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS515);
  _M0L6_2atmpS1695 = Moonbit_array_length(_M0L6_2atmpS1696);
  moonbit_decref_cycle_free(_M0L6_2atmpS1696);
  if (_M0L3lenS1694 == _M0L6_2atmpS1695) {
    int32_t _M0L3lenS1698 = _M0L4selfS515->$1;
    int32_t _M0L6_2atmpS1697 = _M0L3lenS1698 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS515, _M0L6_2atmpS1697);
  }
  _M0L6lengthS516 = _M0L4selfS515->$1;
  _M0L3bufS1699 = _M0L4selfS515->$0;
  _M0L6_2aoldS2366 = (struct _M0TUsiE*)_M0L3bufS1699[_M0L6lengthS516];
  if (_M0L6_2aoldS2366) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2366);
  }
  _M0L3bufS1699[_M0L6lengthS516] = _M0L5valueS517;
  _M0L6_2atmpS1700 = _M0L6lengthS516 + 1;
  _M0L4selfS515->$1 = _M0L6_2atmpS1700;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS518,
  int32_t _M0L5valueS520
) {
  int32_t _M0L3lenS1701;
  int32_t* _M0L6_2atmpS1703;
  int32_t _M0L6_2atmpS1702;
  int32_t _M0L6lengthS519;
  int32_t* _M0L3bufS1706;
  int32_t _M0L6_2atmpS1707;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1701 = _M0L4selfS518->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1703 = _M0MPC15array5Array6bufferGiE(_M0L4selfS518);
  _M0L6_2atmpS1702 = Moonbit_array_length(_M0L6_2atmpS1703);
  moonbit_decref_cycle_free(_M0L6_2atmpS1703);
  if (_M0L3lenS1701 == _M0L6_2atmpS1702) {
    int32_t _M0L3lenS1705 = _M0L4selfS518->$1;
    int32_t _M0L6_2atmpS1704 = _M0L3lenS1705 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS518, _M0L6_2atmpS1704);
  }
  _M0L6lengthS519 = _M0L4selfS518->$1;
  _M0L3bufS1706 = _M0L4selfS518->$0;
  _M0L3bufS1706[_M0L6lengthS519] = _M0L5valueS520;
  _M0L6_2atmpS1707 = _M0L6lengthS519 + 1;
  _M0L4selfS518->$1 = _M0L6_2atmpS1707;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS521,
  float _M0L5valueS523
) {
  int32_t _M0L3lenS1708;
  float* _M0L6_2atmpS1710;
  int32_t _M0L6_2atmpS1709;
  int32_t _M0L6lengthS522;
  float* _M0L3bufS1713;
  int32_t _M0L6_2atmpS1714;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1708 = _M0L4selfS521->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1710 = _M0MPC15array5Array6bufferGfE(_M0L4selfS521);
  _M0L6_2atmpS1709 = Moonbit_array_length(_M0L6_2atmpS1710);
  moonbit_decref_cycle_free(_M0L6_2atmpS1710);
  if (_M0L3lenS1708 == _M0L6_2atmpS1709) {
    int32_t _M0L3lenS1712 = _M0L4selfS521->$1;
    int32_t _M0L6_2atmpS1711 = _M0L3lenS1712 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS521, _M0L6_2atmpS1711);
  }
  _M0L6lengthS522 = _M0L4selfS521->$1;
  _M0L3bufS1713 = _M0L4selfS521->$0;
  _M0L3bufS1713[_M0L6lengthS522] = _M0L5valueS523;
  _M0L6_2atmpS1714 = _M0L6lengthS522 + 1;
  _M0L4selfS521->$1 = _M0L6_2atmpS1714;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS497,
  int32_t _M0L8requiredS499
) {
  int32_t _M0L8old__capS496;
  int32_t _M0L3lenS1683;
  int32_t _M0L8new__capS498;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS496 = _M0MPC15array5Array8capacityGsE(_M0L4selfS497);
  _M0L3lenS1683 = _M0L4selfS497->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS498
  = _M0FPB23array__growth__capacity(_M0L8old__capS496, _M0L3lenS1683, _M0L8requiredS499);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS497, _M0L8new__capS498);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS501,
  int32_t _M0L8requiredS503
) {
  int32_t _M0L8old__capS500;
  int32_t _M0L3lenS1684;
  int32_t _M0L8new__capS502;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS500 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS501);
  _M0L3lenS1684 = _M0L4selfS501->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS502
  = _M0FPB23array__growth__capacity(_M0L8old__capS500, _M0L3lenS1684, _M0L8requiredS503);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS501, _M0L8new__capS502);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS505,
  int32_t _M0L8requiredS507
) {
  int32_t _M0L8old__capS504;
  int32_t _M0L3lenS1685;
  int32_t _M0L8new__capS506;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS504 = _M0MPC15array5Array8capacityGiE(_M0L4selfS505);
  _M0L3lenS1685 = _M0L4selfS505->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS506
  = _M0FPB23array__growth__capacity(_M0L8old__capS504, _M0L3lenS1685, _M0L8requiredS507);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS505, _M0L8new__capS506);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS509,
  int32_t _M0L8requiredS511
) {
  int32_t _M0L8old__capS508;
  int32_t _M0L3lenS1686;
  int32_t _M0L8new__capS510;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS508 = _M0MPC15array5Array8capacityGfE(_M0L4selfS509);
  _M0L3lenS1686 = _M0L4selfS509->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS510
  = _M0FPB23array__growth__capacity(_M0L8old__capS508, _M0L3lenS1686, _M0L8requiredS511);
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
  moonbit_string_t* _M0L6_2aoldS2367;
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
  _M0L6_2aoldS2367 = _M0L4selfS473->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2367);
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
  struct _M0TUsiE** _M0L6_2aoldS2368;
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
  _M0L6_2aoldS2368 = _M0L4selfS479->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2368);
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
  int32_t* _M0L6_2aoldS2369;
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
  _M0L6_2aoldS2369 = _M0L4selfS485->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2369);
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
  float* _M0L6_2aoldS2370;
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
  _M0L6_2aoldS2370 = _M0L4selfS491->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2370);
  _M0L4selfS491->$0 = _M0L8new__bufS495;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS468
) {
  moonbit_string_t* _M0L6_2atmpS1679;
  int32_t _result_2516;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1679 = _M0MPC15array5Array6bufferGsE(_M0L4selfS468);
  _result_2516 = Moonbit_array_length(_M0L6_2atmpS1679);
  moonbit_decref_cycle_free(_M0L6_2atmpS1679);
  return _result_2516;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS469
) {
  struct _M0TUsiE** _M0L6_2atmpS1680;
  int32_t _result_2517;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1680 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS469);
  _result_2517 = Moonbit_array_length(_M0L6_2atmpS1680);
  moonbit_decref_cycle_free(_M0L6_2atmpS1680);
  return _result_2517;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS470
) {
  int32_t* _M0L6_2atmpS1681;
  int32_t _result_2518;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1681 = _M0MPC15array5Array6bufferGiE(_M0L4selfS470);
  _result_2518 = Moonbit_array_length(_M0L6_2atmpS1681);
  moonbit_decref_cycle_free(_M0L6_2atmpS1681);
  return _result_2518;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS471
) {
  float* _M0L6_2atmpS1682;
  int32_t _result_2519;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1682 = _M0MPC15array5Array6bufferGfE(_M0L4selfS471);
  _result_2519 = Moonbit_array_length(_M0L6_2atmpS1682);
  moonbit_decref_cycle_free(_M0L6_2atmpS1682);
  return _result_2519;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_15.data);
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

int32_t _M0MPC15array5Array6lengthGRP26RiantR8snn__mbt13SynapseTargetE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE* _M0L4selfS459
) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS459->$1;
}

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE* _M0L4selfS460) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS460->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS452) {
  float* _M0L8_2afieldS2371;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2371 = _M0L4selfS452->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2371);
  return _M0L8_2afieldS2371;
}

struct _M0TP26RiantR8snn__mbt13SynapseTarget** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt13SynapseTargetE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13SynapseTargetE* _M0L4selfS453
) {
  struct _M0TP26RiantR8snn__mbt13SynapseTarget** _M0L8_2afieldS2372;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2372 = _M0L4selfS453->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2372);
  return _M0L8_2afieldS2372;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS454) {
  int32_t* _M0L8_2afieldS2373;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2373 = _M0L4selfS454->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2373);
  return _M0L8_2afieldS2373;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS455
) {
  moonbit_string_t* _M0L8_2afieldS2374;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2374 = _M0L4selfS455->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2374);
  return _M0L8_2afieldS2374;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS456
) {
  struct _M0TUsiE** _M0L8_2afieldS2375;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2375 = _M0L4selfS456->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2375);
  return _M0L8_2afieldS2375;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS457
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS2376;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2376 = _M0L4selfS457->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2376);
  return _M0L8_2afieldS2376;
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
  int32_t _M0L3endS1677;
  int32_t _M0L5startS1678;
  int32_t _M0L8str__lenS447;
  int32_t _M0L3lenS1676;
  int32_t _M0L8requiredS449;
  uint16_t* _M0L4dataS1669;
  int32_t _M0L6_2atmpS1668;
  int32_t _if__result_2521;
  uint16_t* _M0L4dataS1670;
  int32_t _M0L3lenS1671;
  moonbit_string_t _M0L6_2atmpS1672;
  int32_t _M0L6_2atmpS1673;
  int32_t _M0L3lenS1675;
  int32_t _M0L6_2atmpS1674;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1677 = _M0L3strS448.$2;
  _M0L5startS1678 = _M0L3strS448.$1;
  _M0L8str__lenS447 = _M0L3endS1677 - _M0L5startS1678;
  if (_M0L8str__lenS447 == 0) {
    return 0;
  }
  _M0L3lenS1676 = _M0L4selfS450->$1;
  _M0L8requiredS449 = _M0L3lenS1676 + _M0L8str__lenS447;
  _M0L4dataS1669 = _M0L4selfS450->$0;
  _M0L6_2atmpS1668 = Moonbit_array_length(_M0L4dataS1669);
  if (_M0L8requiredS449 > _M0L6_2atmpS1668) {
    _if__result_2521 = 1;
  } else {
    int32_t _M0L3lenS1667 = _M0L4selfS450->$1;
    _if__result_2521 = _M0L8requiredS449 < _M0L3lenS1667;
  }
  if (_if__result_2521) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS450, _M0L8requiredS449);
  }
  _M0L4dataS1670 = _M0L4selfS450->$0;
  _M0L3lenS1671 = _M0L4selfS450->$1;
  moonbit_incref_cycle_free(_M0L4dataS1670);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1672 = _M0MPC16string10StringView4data(_M0L3strS448);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1673 = _M0MPC16string10StringView13start__offset(_M0L3strS448);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1670, _M0L3lenS1671, _M0L6_2atmpS1672, _M0L6_2atmpS1673, _M0L8str__lenS447);
  moonbit_decref_cycle_free(_M0L4dataS1670);
  moonbit_decref_cycle_free(_M0L6_2atmpS1672);
  _M0L3lenS1675 = _M0L4selfS450->$1;
  _M0L6_2atmpS1674 = _M0L3lenS1675 + _M0L8str__lenS447;
  _M0L4selfS450->$1 = _M0L6_2atmpS1674;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS444,
  int32_t _M0L5startS442,
  int32_t _M0L3endS443
) {
  int32_t _if__result_2522;
  int32_t _M0L3lenS445;
  int32_t _M0L6_2atmpS1666;
  moonbit_bytes_t _M0L5bytesS446;
  moonbit_bytes_t _M0L6_2atmpS1665;
  moonbit_string_t _result_2523;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS442 == 0) {
    int32_t _M0L6_2atmpS1664 = Moonbit_array_length(_M0L3strS444);
    _if__result_2522 = _M0L3endS443 == _M0L6_2atmpS1664;
  } else {
    _if__result_2522 = 0;
  }
  if (_if__result_2522) {
    moonbit_incref_cycle_free(_M0L3strS444);
    return _M0L3strS444;
  }
  _M0L3lenS445 = _M0L3endS443 - _M0L5startS442;
  _M0L6_2atmpS1666 = _M0L3lenS445 * 2;
  _M0L5bytesS446 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1666, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS446, 0, _M0L3strS444, _M0L5startS442, _M0L3lenS445);
  _M0L6_2atmpS1665 = _M0L5bytesS446;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2523
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1665, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1665);
  return _result_2523;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS437,
  int32_t _M0L6offsetS441,
  int64_t _M0L6lengthS439
) {
  int32_t _M0L3lenS436;
  int32_t _M0L6lengthS438;
  int32_t _if__result_2524;
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
      int32_t _M0L6_2atmpS1663 = _M0L6offsetS441 + _M0L6lengthS438;
      _if__result_2524 = _M0L6_2atmpS1663 <= _M0L3lenS436;
    } else {
      _if__result_2524 = 0;
    }
  } else {
    _if__result_2524 = 0;
  }
  if (_if__result_2524) {
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
  int32_t _M0L6_2atmpS1662;
  int32_t _M0L6_2atmpS1661;
  int32_t _M0L2e1S422;
  int32_t _M0L6_2atmpS1660;
  int32_t _M0L2e2S425;
  int32_t _M0L4len1S427;
  int32_t _M0L4len2S429;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1662 = _M0L6lengthS424 * 2;
  _M0L6_2atmpS1661 = _M0L13bytes__offsetS423 + _M0L6_2atmpS1662;
  _M0L2e1S422 = _M0L6_2atmpS1661 - 1;
  _M0L6_2atmpS1660 = _M0L11str__offsetS426 + _M0L6lengthS424;
  _M0L2e2S425 = _M0L6_2atmpS1660 - 1;
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
        int32_t _M0L6_2atmpS1657 = _M0L3strS430[_M0L1iS432];
        int32_t _M0L6_2atmpS1656 = (int32_t)_M0L6_2atmpS1657;
        uint32_t _M0L1cS434 = *(uint32_t*)&_M0L6_2atmpS1656;
        uint32_t _M0L6_2atmpS1652 = _M0L1cS434 & 255u;
        int32_t _M0L6_2atmpS1651;
        int32_t _M0L6_2atmpS1653;
        uint32_t _M0L6_2atmpS1655;
        int32_t _M0L6_2atmpS1654;
        int32_t _M0L6_2atmpS1658;
        int32_t _M0L6_2atmpS1659;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1651 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1652);
        if (
          _M0L1jS433 < 0 || _M0L1jS433 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L1jS433] = _M0L6_2atmpS1651;
        _M0L6_2atmpS1653 = _M0L1jS433 + 1;
        _M0L6_2atmpS1655 = _M0L1cS434 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1654 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1655);
        if (
          _M0L6_2atmpS1653 < 0
          || _M0L6_2atmpS1653 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L6_2atmpS1653] = _M0L6_2atmpS1654;
        _M0L6_2atmpS1658 = _M0L1iS432 + 1;
        _M0L6_2atmpS1659 = _M0L1jS433 + 2;
        _M0L1iS432 = _M0L6_2atmpS1658;
        _M0L1jS433 = _M0L6_2atmpS1659;
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
  int32_t _M0L6_2atmpS1650;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1650 = *(int32_t*)&_M0L4selfS421;
  return _M0L6_2atmpS1650 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS413,
  int32_t _M0L5radixS412
) {
  uint16_t* _M0L6bufferS414;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS412 < 2 || _M0L5radixS412 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_16.data);
  }
  if (_M0L4selfS413 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_16.data);
  }
  if (_M0L4selfS396 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  _M0L12is__negativeS397 = _M0L4selfS396 < 0ll;
  if (_M0L12is__negativeS397) {
    int64_t _M0L6_2atmpS1649 = -_M0L4selfS396;
    _M0L3numS398 = *(uint64_t*)&_M0L6_2atmpS1649;
  } else {
    _M0L3numS398 = *(uint64_t*)&_M0L4selfS396;
  }
  switch (_M0L5radixS395) {
    case 10: {
      int32_t _M0L10digit__lenS400;
      int32_t _M0L6_2atmpS1646;
      int32_t _M0L10total__lenS401;
      uint16_t* _M0L6bufferS402;
      int32_t _M0L12digit__startS403;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS400 = _M0FPB12dec__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1646 = 1;
      } else {
        _M0L6_2atmpS1646 = 0;
      }
      _M0L10total__lenS401 = _M0L10digit__lenS400 + _M0L6_2atmpS1646;
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
      int32_t _M0L6_2atmpS1647;
      int32_t _M0L10total__lenS405;
      uint16_t* _M0L6bufferS406;
      int32_t _M0L12digit__startS407;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS404 = _M0FPB12hex__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1647 = 1;
      } else {
        _M0L6_2atmpS1647 = 0;
      }
      _M0L10total__lenS405 = _M0L10digit__lenS404 + _M0L6_2atmpS1647;
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
      int32_t _M0L6_2atmpS1648;
      int32_t _M0L10total__lenS409;
      uint16_t* _M0L6bufferS410;
      int32_t _M0L12digit__startS411;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS408
      = _M0FPB14radix__count64(_M0L3numS398, _M0L5radixS395);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1648 = 1;
      } else {
        _M0L6_2atmpS1648 = 0;
      }
      _M0L10total__lenS409 = _M0L10digit__lenS408 + _M0L6_2atmpS1648;
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
  int32_t _M0L6_2atmpS1645;
  uint64_t _M0L3numS371;
  int32_t _M0L6offsetS372;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1645 = _M0L10total__lenS394 - _M0L12digit__startS382;
  _M0L3numS371 = _M0L3numS393;
  _M0L6offsetS372 = _M0L6_2atmpS1645;
  while (1) {
    if (_M0L3numS371 >= 10000ull) {
      uint64_t _M0L1tS373 = _M0L3numS371 / 10000ull;
      uint64_t _M0L6_2atmpS1622 = _M0L3numS371 % 10000ull;
      int32_t _M0L1rS374 = (int32_t)_M0L6_2atmpS1622;
      int32_t _M0L2d1S375 = _M0L1rS374 / 100;
      int32_t _M0L2d2S376 = _M0L1rS374 % 100;
      int32_t _M0L6_2atmpS1621 = _M0L2d1S375 / 10;
      int32_t _M0L6_2atmpS1620 = 48 + _M0L6_2atmpS1621;
      int32_t _M0L6d1__hiS377 = (uint16_t)_M0L6_2atmpS1620;
      int32_t _M0L6_2atmpS1619 = _M0L2d1S375 % 10;
      int32_t _M0L6_2atmpS1618 = 48 + _M0L6_2atmpS1619;
      int32_t _M0L6d1__loS378 = (uint16_t)_M0L6_2atmpS1618;
      int32_t _M0L6_2atmpS1617 = _M0L2d2S376 / 10;
      int32_t _M0L6_2atmpS1616 = 48 + _M0L6_2atmpS1617;
      int32_t _M0L6d2__hiS379 = (uint16_t)_M0L6_2atmpS1616;
      int32_t _M0L6_2atmpS1615 = _M0L2d2S376 % 10;
      int32_t _M0L6_2atmpS1614 = 48 + _M0L6_2atmpS1615;
      int32_t _M0L6d2__loS380 = (uint16_t)_M0L6_2atmpS1614;
      int32_t _M0L6_2atmpS1606 = _M0L12digit__startS382 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS1605 = _M0L6_2atmpS1606 - 4;
      int32_t _M0L6_2atmpS1608;
      int32_t _M0L6_2atmpS1607;
      int32_t _M0L6_2atmpS1610;
      int32_t _M0L6_2atmpS1609;
      int32_t _M0L6_2atmpS1612;
      int32_t _M0L6_2atmpS1611;
      int32_t _M0L6_2atmpS1613;
      _M0L6bufferS381[_M0L6_2atmpS1605] = _M0L6d1__hiS377;
      _M0L6_2atmpS1608 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1607 = _M0L6_2atmpS1608 - 3;
      _M0L6bufferS381[_M0L6_2atmpS1607] = _M0L6d1__loS378;
      _M0L6_2atmpS1610 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1609 = _M0L6_2atmpS1610 - 2;
      _M0L6bufferS381[_M0L6_2atmpS1609] = _M0L6d2__hiS379;
      _M0L6_2atmpS1612 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1611 = _M0L6_2atmpS1612 - 1;
      _M0L6bufferS381[_M0L6_2atmpS1611] = _M0L6d2__loS380;
      _M0L6_2atmpS1613 = _M0L6offsetS372 - 4;
      _M0L3numS371 = _M0L1tS373;
      _M0L6offsetS372 = _M0L6_2atmpS1613;
      continue;
    } else {
      int32_t _M0L6_2atmpS1644 = (int32_t)_M0L3numS371;
      int32_t _M0L9remainingS384 = _M0L6_2atmpS1644;
      int32_t _M0L6offsetS385 = _M0L6offsetS372;
      while (1) {
        if (_M0L9remainingS384 >= 100) {
          int32_t _M0L1tS386 = _M0L9remainingS384 / 100;
          int32_t _M0L1dS387 = _M0L9remainingS384 % 100;
          int32_t _M0L6_2atmpS1631 = _M0L1dS387 / 10;
          int32_t _M0L6_2atmpS1630 = 48 + _M0L6_2atmpS1631;
          int32_t _M0L5d__hiS388 = (uint16_t)_M0L6_2atmpS1630;
          int32_t _M0L6_2atmpS1629 = _M0L1dS387 % 10;
          int32_t _M0L6_2atmpS1628 = 48 + _M0L6_2atmpS1629;
          int32_t _M0L5d__loS389 = (uint16_t)_M0L6_2atmpS1628;
          int32_t _M0L6_2atmpS1624 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1623 = _M0L6_2atmpS1624 - 2;
          int32_t _M0L6_2atmpS1626;
          int32_t _M0L6_2atmpS1625;
          int32_t _M0L6_2atmpS1627;
          _M0L6bufferS381[_M0L6_2atmpS1623] = _M0L5d__hiS388;
          _M0L6_2atmpS1626 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1625 = _M0L6_2atmpS1626 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1625] = _M0L5d__loS389;
          _M0L6_2atmpS1627 = _M0L6offsetS385 - 2;
          _M0L9remainingS384 = _M0L1tS386;
          _M0L6offsetS385 = _M0L6_2atmpS1627;
          continue;
        } else if (_M0L9remainingS384 >= 10) {
          int32_t _M0L6_2atmpS1639 = _M0L9remainingS384 / 10;
          int32_t _M0L6_2atmpS1638 = 48 + _M0L6_2atmpS1639;
          int32_t _M0L5d__hiS391 = (uint16_t)_M0L6_2atmpS1638;
          int32_t _M0L6_2atmpS1637 = _M0L9remainingS384 % 10;
          int32_t _M0L6_2atmpS1636 = 48 + _M0L6_2atmpS1637;
          int32_t _M0L5d__loS392 = (uint16_t)_M0L6_2atmpS1636;
          int32_t _M0L6_2atmpS1633 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1632 = _M0L6_2atmpS1633 - 2;
          int32_t _M0L6_2atmpS1635;
          int32_t _M0L6_2atmpS1634;
          _M0L6bufferS381[_M0L6_2atmpS1632] = _M0L5d__hiS391;
          _M0L6_2atmpS1635 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1634 = _M0L6_2atmpS1635 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1634] = _M0L5d__loS392;
        } else {
          int32_t _M0L6_2atmpS1643 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1640 = _M0L6_2atmpS1643 - 1;
          int32_t _M0L6_2atmpS1642 = 48 + _M0L9remainingS384;
          int32_t _M0L6_2atmpS1641 = (uint16_t)_M0L6_2atmpS1642;
          _M0L6bufferS381[_M0L6_2atmpS1640] = _M0L6_2atmpS1641;
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
  int32_t _M0L6_2atmpS1590;
  int32_t _M0L6_2atmpS1589;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS354 = _M0MPC13int3Int10to__uint64(_M0L5radixS355);
  _M0L6_2atmpS1590 = _M0L5radixS355 - 1;
  _M0L6_2atmpS1589 = _M0L5radixS355 & _M0L6_2atmpS1590;
  if (_M0L6_2atmpS1589 == 0) {
    int32_t _M0L5shiftS356;
    uint64_t _M0L4maskS357;
    int32_t _M0L6_2atmpS1597;
    int32_t _M0L6offsetS358;
    uint64_t _M0L1nS359;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS356 = moonbit_ctz32(_M0L5radixS355);
    _M0L4maskS357 = _M0L4baseS354 - 1ull;
    _M0L6_2atmpS1597 = _M0L10total__lenS364 - _M0L12digit__startS362;
    _M0L6offsetS358 = _M0L6_2atmpS1597;
    _M0L1nS359 = _M0L3numS365;
    while (1) {
      if (_M0L1nS359 > 0ull) {
        uint64_t _M0L6_2atmpS1596 = _M0L1nS359 & _M0L4maskS357;
        int32_t _M0L5digitS360 = (int32_t)_M0L6_2atmpS1596;
        int32_t _M0L6_2atmpS1593 = _M0L12digit__startS362 + _M0L6offsetS358;
        int32_t _M0L6_2atmpS1591 = _M0L6_2atmpS1593 - 1;
        int32_t _M0L6_2atmpS1592 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS360];
        int32_t _M0L6_2atmpS1594;
        uint64_t _M0L6_2atmpS1595;
        _M0L6bufferS361[_M0L6_2atmpS1591] = _M0L6_2atmpS1592;
        _M0L6_2atmpS1594 = _M0L6offsetS358 - 1;
        _M0L6_2atmpS1595 = _M0L1nS359 >> (_M0L5shiftS356 & 63);
        _M0L6offsetS358 = _M0L6_2atmpS1594;
        _M0L1nS359 = _M0L6_2atmpS1595;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1604 = _M0L10total__lenS364 - _M0L12digit__startS362;
    int32_t _M0L6offsetS366 = _M0L6_2atmpS1604;
    uint64_t _M0L1nS367 = _M0L3numS365;
    while (1) {
      if (_M0L1nS367 > 0ull) {
        uint64_t _M0L1qS368 = _M0L1nS367 / _M0L4baseS354;
        uint64_t _M0L6_2atmpS1603 = _M0L1qS368 * _M0L4baseS354;
        uint64_t _M0L6_2atmpS1602 = _M0L1nS367 - _M0L6_2atmpS1603;
        int32_t _M0L5digitS369 = (int32_t)_M0L6_2atmpS1602;
        int32_t _M0L6_2atmpS1600 = _M0L12digit__startS362 + _M0L6offsetS366;
        int32_t _M0L6_2atmpS1598 = _M0L6_2atmpS1600 - 1;
        int32_t _M0L6_2atmpS1599 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS369];
        int32_t _M0L6_2atmpS1601;
        _M0L6bufferS361[_M0L6_2atmpS1598] = _M0L6_2atmpS1599;
        _M0L6_2atmpS1601 = _M0L6offsetS366 - 1;
        _M0L6offsetS366 = _M0L6_2atmpS1601;
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
  int32_t _M0L6_2atmpS1588;
  int32_t _M0L6offsetS343;
  uint64_t _M0L1nS344;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1588 = _M0L10total__lenS352 - _M0L12digit__startS349;
  _M0L6offsetS343 = _M0L6_2atmpS1588;
  _M0L1nS344 = _M0L3numS353;
  while (1) {
    if (_M0L6offsetS343 >= 2) {
      uint64_t _M0L6_2atmpS1585 = _M0L1nS344 & 255ull;
      int32_t _M0L9byte__valS345 = (int32_t)_M0L6_2atmpS1585;
      int32_t _M0L2hiS346 = _M0L9byte__valS345 / 16;
      int32_t _M0L2loS347 = _M0L9byte__valS345 % 16;
      int32_t _M0L6_2atmpS1579 = _M0L12digit__startS349 + _M0L6offsetS343;
      int32_t _M0L6_2atmpS1577 = _M0L6_2atmpS1579 - 2;
      int32_t _M0L6_2atmpS1578 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L2hiS346];
      int32_t _M0L6_2atmpS1582;
      int32_t _M0L6_2atmpS1580;
      int32_t _M0L6_2atmpS1581;
      int32_t _M0L6_2atmpS1583;
      uint64_t _M0L6_2atmpS1584;
      _M0L6bufferS348[_M0L6_2atmpS1577] = _M0L6_2atmpS1578;
      _M0L6_2atmpS1582 = _M0L12digit__startS349 + _M0L6offsetS343;
      _M0L6_2atmpS1580 = _M0L6_2atmpS1582 - 1;
      _M0L6_2atmpS1581
      = ((moonbit_string_t)moonbit_string_literal_17.data)[
        _M0L2loS347
      ];
      _M0L6bufferS348[_M0L6_2atmpS1580] = _M0L6_2atmpS1581;
      _M0L6_2atmpS1583 = _M0L6offsetS343 - 2;
      _M0L6_2atmpS1584 = _M0L1nS344 >> 8;
      _M0L6offsetS343 = _M0L6_2atmpS1583;
      _M0L1nS344 = _M0L6_2atmpS1584;
      continue;
    } else if (_M0L6offsetS343 == 1) {
      uint64_t _M0L6_2atmpS1587 = _M0L1nS344 & 15ull;
      int32_t _M0L6nibbleS351 = (int32_t)_M0L6_2atmpS1587;
      int32_t _M0L6_2atmpS1586 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L6nibbleS351];
      _M0L6bufferS348[_M0L12digit__startS349] = _M0L6_2atmpS1586;
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
      uint64_t _M0L6_2atmpS1575 = _M0L3numS340 / _M0L4baseS338;
      int32_t _M0L6_2atmpS1576 = _M0L5countS341 + 1;
      _M0L3numS340 = _M0L6_2atmpS1575;
      _M0L5countS341 = _M0L6_2atmpS1576;
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
    int32_t _M0L6_2atmpS1574;
    int32_t _M0L6_2atmpS1573;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS336 = moonbit_clz64(_M0L5valueS335);
    _M0L6_2atmpS1574 = 63 - _M0L14leading__zerosS336;
    _M0L6_2atmpS1573 = _M0L6_2atmpS1574 / 4;
    return _M0L6_2atmpS1573 + 1;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_16.data);
  }
  if (_M0L4selfS318 == 0) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  _M0L12is__negativeS319 = _M0L4selfS318 < 0;
  if (_M0L12is__negativeS319) {
    int32_t _M0L6_2atmpS1572 = -_M0L4selfS318;
    _M0L3numS320 = *(uint32_t*)&_M0L6_2atmpS1572;
  } else {
    _M0L3numS320 = *(uint32_t*)&_M0L4selfS318;
  }
  switch (_M0L5radixS317) {
    case 10: {
      int32_t _M0L10digit__lenS322;
      int32_t _M0L6_2atmpS1569;
      int32_t _M0L10total__lenS323;
      uint16_t* _M0L6bufferS324;
      int32_t _M0L12digit__startS325;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS322 = _M0FPB12dec__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1569 = 1;
      } else {
        _M0L6_2atmpS1569 = 0;
      }
      _M0L10total__lenS323 = _M0L10digit__lenS322 + _M0L6_2atmpS1569;
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
      int32_t _M0L6_2atmpS1570;
      int32_t _M0L10total__lenS327;
      uint16_t* _M0L6bufferS328;
      int32_t _M0L12digit__startS329;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS326 = _M0FPB12hex__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1570 = 1;
      } else {
        _M0L6_2atmpS1570 = 0;
      }
      _M0L10total__lenS327 = _M0L10digit__lenS326 + _M0L6_2atmpS1570;
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
      int32_t _M0L6_2atmpS1571;
      int32_t _M0L10total__lenS331;
      uint16_t* _M0L6bufferS332;
      int32_t _M0L12digit__startS333;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS330
      = _M0FPB14radix__count32(_M0L3numS320, _M0L5radixS317);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1571 = 1;
      } else {
        _M0L6_2atmpS1571 = 0;
      }
      _M0L10total__lenS331 = _M0L10digit__lenS330 + _M0L6_2atmpS1571;
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
      uint32_t _M0L6_2atmpS1567 = _M0L3numS314 / _M0L4baseS312;
      int32_t _M0L6_2atmpS1568 = _M0L5countS315 + 1;
      _M0L3numS314 = _M0L6_2atmpS1567;
      _M0L5countS315 = _M0L6_2atmpS1568;
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
    int32_t _M0L6_2atmpS1566;
    int32_t _M0L6_2atmpS1565;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS310 = moonbit_clz32(_M0L5valueS309);
    _M0L6_2atmpS1566 = 31 - _M0L14leading__zerosS310;
    _M0L6_2atmpS1565 = _M0L6_2atmpS1566 / 4;
    return _M0L6_2atmpS1565 + 1;
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
  int32_t _M0L6_2atmpS1564;
  uint32_t _M0L3numS284;
  int32_t _M0L6offsetS285;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1564 = _M0L10total__lenS307 - _M0L12digit__startS295;
  _M0L3numS284 = _M0L3numS306;
  _M0L6offsetS285 = _M0L6_2atmpS1564;
  while (1) {
    if (_M0L3numS284 >= 10000u) {
      uint32_t _M0L1tS286 = _M0L3numS284 / 10000u;
      uint32_t _M0L6_2atmpS1541 = _M0L3numS284 % 10000u;
      int32_t _M0L1rS287 = *(int32_t*)&_M0L6_2atmpS1541;
      int32_t _M0L2d1S288 = _M0L1rS287 / 100;
      int32_t _M0L2d2S289 = _M0L1rS287 % 100;
      int32_t _M0L6_2atmpS1540 = _M0L2d1S288 / 10;
      int32_t _M0L6_2atmpS1539 = 48 + _M0L6_2atmpS1540;
      int32_t _M0L6d1__hiS290 = (uint16_t)_M0L6_2atmpS1539;
      int32_t _M0L6_2atmpS1538 = _M0L2d1S288 % 10;
      int32_t _M0L6_2atmpS1537 = 48 + _M0L6_2atmpS1538;
      int32_t _M0L6d1__loS291 = (uint16_t)_M0L6_2atmpS1537;
      int32_t _M0L6_2atmpS1536 = _M0L2d2S289 / 10;
      int32_t _M0L6_2atmpS1535 = 48 + _M0L6_2atmpS1536;
      int32_t _M0L6d2__hiS292 = (uint16_t)_M0L6_2atmpS1535;
      int32_t _M0L6_2atmpS1534 = _M0L2d2S289 % 10;
      int32_t _M0L6_2atmpS1533 = 48 + _M0L6_2atmpS1534;
      int32_t _M0L6d2__loS293 = (uint16_t)_M0L6_2atmpS1533;
      int32_t _M0L6_2atmpS1525 = _M0L12digit__startS295 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS1524 = _M0L6_2atmpS1525 - 4;
      int32_t _M0L6_2atmpS1527;
      int32_t _M0L6_2atmpS1526;
      int32_t _M0L6_2atmpS1529;
      int32_t _M0L6_2atmpS1528;
      int32_t _M0L6_2atmpS1531;
      int32_t _M0L6_2atmpS1530;
      int32_t _M0L6_2atmpS1532;
      _M0L6bufferS294[_M0L6_2atmpS1524] = _M0L6d1__hiS290;
      _M0L6_2atmpS1527 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1526 = _M0L6_2atmpS1527 - 3;
      _M0L6bufferS294[_M0L6_2atmpS1526] = _M0L6d1__loS291;
      _M0L6_2atmpS1529 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1528 = _M0L6_2atmpS1529 - 2;
      _M0L6bufferS294[_M0L6_2atmpS1528] = _M0L6d2__hiS292;
      _M0L6_2atmpS1531 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1530 = _M0L6_2atmpS1531 - 1;
      _M0L6bufferS294[_M0L6_2atmpS1530] = _M0L6d2__loS293;
      _M0L6_2atmpS1532 = _M0L6offsetS285 - 4;
      _M0L3numS284 = _M0L1tS286;
      _M0L6offsetS285 = _M0L6_2atmpS1532;
      continue;
    } else {
      int32_t _M0L6_2atmpS1563 = *(int32_t*)&_M0L3numS284;
      int32_t _M0L9remainingS297 = _M0L6_2atmpS1563;
      int32_t _M0L6offsetS298 = _M0L6offsetS285;
      while (1) {
        if (_M0L9remainingS297 >= 100) {
          int32_t _M0L1tS299 = _M0L9remainingS297 / 100;
          int32_t _M0L1dS300 = _M0L9remainingS297 % 100;
          int32_t _M0L6_2atmpS1550 = _M0L1dS300 / 10;
          int32_t _M0L6_2atmpS1549 = 48 + _M0L6_2atmpS1550;
          int32_t _M0L5d__hiS301 = (uint16_t)_M0L6_2atmpS1549;
          int32_t _M0L6_2atmpS1548 = _M0L1dS300 % 10;
          int32_t _M0L6_2atmpS1547 = 48 + _M0L6_2atmpS1548;
          int32_t _M0L5d__loS302 = (uint16_t)_M0L6_2atmpS1547;
          int32_t _M0L6_2atmpS1543 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1542 = _M0L6_2atmpS1543 - 2;
          int32_t _M0L6_2atmpS1545;
          int32_t _M0L6_2atmpS1544;
          int32_t _M0L6_2atmpS1546;
          _M0L6bufferS294[_M0L6_2atmpS1542] = _M0L5d__hiS301;
          _M0L6_2atmpS1545 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1544 = _M0L6_2atmpS1545 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1544] = _M0L5d__loS302;
          _M0L6_2atmpS1546 = _M0L6offsetS298 - 2;
          _M0L9remainingS297 = _M0L1tS299;
          _M0L6offsetS298 = _M0L6_2atmpS1546;
          continue;
        } else if (_M0L9remainingS297 >= 10) {
          int32_t _M0L6_2atmpS1558 = _M0L9remainingS297 / 10;
          int32_t _M0L6_2atmpS1557 = 48 + _M0L6_2atmpS1558;
          int32_t _M0L5d__hiS304 = (uint16_t)_M0L6_2atmpS1557;
          int32_t _M0L6_2atmpS1556 = _M0L9remainingS297 % 10;
          int32_t _M0L6_2atmpS1555 = 48 + _M0L6_2atmpS1556;
          int32_t _M0L5d__loS305 = (uint16_t)_M0L6_2atmpS1555;
          int32_t _M0L6_2atmpS1552 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1551 = _M0L6_2atmpS1552 - 2;
          int32_t _M0L6_2atmpS1554;
          int32_t _M0L6_2atmpS1553;
          _M0L6bufferS294[_M0L6_2atmpS1551] = _M0L5d__hiS304;
          _M0L6_2atmpS1554 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1553 = _M0L6_2atmpS1554 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1553] = _M0L5d__loS305;
        } else {
          int32_t _M0L6_2atmpS1562 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1559 = _M0L6_2atmpS1562 - 1;
          int32_t _M0L6_2atmpS1561 = 48 + _M0L9remainingS297;
          int32_t _M0L6_2atmpS1560 = (uint16_t)_M0L6_2atmpS1561;
          _M0L6bufferS294[_M0L6_2atmpS1559] = _M0L6_2atmpS1560;
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
  int32_t _M0L6_2atmpS1509;
  int32_t _M0L6_2atmpS1508;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS267 = *(uint32_t*)&_M0L5radixS268;
  _M0L6_2atmpS1509 = _M0L5radixS268 - 1;
  _M0L6_2atmpS1508 = _M0L5radixS268 & _M0L6_2atmpS1509;
  if (_M0L6_2atmpS1508 == 0) {
    int32_t _M0L5shiftS269;
    uint32_t _M0L4maskS270;
    int32_t _M0L6_2atmpS1516;
    int32_t _M0L6offsetS271;
    uint32_t _M0L1nS272;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS269 = moonbit_ctz32(_M0L5radixS268);
    _M0L4maskS270 = _M0L4baseS267 - 1u;
    _M0L6_2atmpS1516 = _M0L10total__lenS277 - _M0L12digit__startS275;
    _M0L6offsetS271 = _M0L6_2atmpS1516;
    _M0L1nS272 = _M0L3numS278;
    while (1) {
      if (_M0L1nS272 > 0u) {
        uint32_t _M0L6_2atmpS1515 = _M0L1nS272 & _M0L4maskS270;
        int32_t _M0L5digitS273 = *(int32_t*)&_M0L6_2atmpS1515;
        int32_t _M0L6_2atmpS1512 = _M0L12digit__startS275 + _M0L6offsetS271;
        int32_t _M0L6_2atmpS1510 = _M0L6_2atmpS1512 - 1;
        int32_t _M0L6_2atmpS1511 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS273];
        int32_t _M0L6_2atmpS1513;
        uint32_t _M0L6_2atmpS1514;
        _M0L6bufferS274[_M0L6_2atmpS1510] = _M0L6_2atmpS1511;
        _M0L6_2atmpS1513 = _M0L6offsetS271 - 1;
        _M0L6_2atmpS1514 = _M0L1nS272 >> (_M0L5shiftS269 & 31);
        _M0L6offsetS271 = _M0L6_2atmpS1513;
        _M0L1nS272 = _M0L6_2atmpS1514;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1523 = _M0L10total__lenS277 - _M0L12digit__startS275;
    int32_t _M0L6offsetS279 = _M0L6_2atmpS1523;
    uint32_t _M0L1nS280 = _M0L3numS278;
    while (1) {
      if (_M0L1nS280 > 0u) {
        uint32_t _M0L1qS281 = _M0L1nS280 / _M0L4baseS267;
        uint32_t _M0L6_2atmpS1522 = _M0L1qS281 * _M0L4baseS267;
        uint32_t _M0L6_2atmpS1521 = _M0L1nS280 - _M0L6_2atmpS1522;
        int32_t _M0L5digitS282 = *(int32_t*)&_M0L6_2atmpS1521;
        int32_t _M0L6_2atmpS1519 = _M0L12digit__startS275 + _M0L6offsetS279;
        int32_t _M0L6_2atmpS1517 = _M0L6_2atmpS1519 - 1;
        int32_t _M0L6_2atmpS1518 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS282];
        int32_t _M0L6_2atmpS1520;
        _M0L6bufferS274[_M0L6_2atmpS1517] = _M0L6_2atmpS1518;
        _M0L6_2atmpS1520 = _M0L6offsetS279 - 1;
        _M0L6offsetS279 = _M0L6_2atmpS1520;
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
  int32_t _M0L6_2atmpS1507;
  int32_t _M0L6offsetS256;
  uint32_t _M0L1nS257;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1507 = _M0L10total__lenS265 - _M0L12digit__startS262;
  _M0L6offsetS256 = _M0L6_2atmpS1507;
  _M0L1nS257 = _M0L3numS266;
  while (1) {
    if (_M0L6offsetS256 >= 2) {
      uint32_t _M0L6_2atmpS1504 = _M0L1nS257 & 255u;
      int32_t _M0L9byte__valS258 = *(int32_t*)&_M0L6_2atmpS1504;
      int32_t _M0L2hiS259 = _M0L9byte__valS258 / 16;
      int32_t _M0L2loS260 = _M0L9byte__valS258 % 16;
      int32_t _M0L6_2atmpS1498 = _M0L12digit__startS262 + _M0L6offsetS256;
      int32_t _M0L6_2atmpS1496 = _M0L6_2atmpS1498 - 2;
      int32_t _M0L6_2atmpS1497 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L2hiS259];
      int32_t _M0L6_2atmpS1501;
      int32_t _M0L6_2atmpS1499;
      int32_t _M0L6_2atmpS1500;
      int32_t _M0L6_2atmpS1502;
      uint32_t _M0L6_2atmpS1503;
      _M0L6bufferS261[_M0L6_2atmpS1496] = _M0L6_2atmpS1497;
      _M0L6_2atmpS1501 = _M0L12digit__startS262 + _M0L6offsetS256;
      _M0L6_2atmpS1499 = _M0L6_2atmpS1501 - 1;
      _M0L6_2atmpS1500
      = ((moonbit_string_t)moonbit_string_literal_17.data)[
        _M0L2loS260
      ];
      _M0L6bufferS261[_M0L6_2atmpS1499] = _M0L6_2atmpS1500;
      _M0L6_2atmpS1502 = _M0L6offsetS256 - 2;
      _M0L6_2atmpS1503 = _M0L1nS257 >> 8;
      _M0L6offsetS256 = _M0L6_2atmpS1502;
      _M0L1nS257 = _M0L6_2atmpS1503;
      continue;
    } else if (_M0L6offsetS256 == 1) {
      uint32_t _M0L6_2atmpS1506 = _M0L1nS257 & 15u;
      int32_t _M0L6nibbleS264 = *(int32_t*)&_M0L6_2atmpS1506;
      int32_t _M0L6_2atmpS1505 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L6nibbleS264];
      _M0L6bufferS261[_M0L12digit__startS262] = _M0L6_2atmpS1505;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS255
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS254;
  struct _M0TPB6Logger _M0L6_2atmpS1495;
  moonbit_string_t _result_2538;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS254);
  _M0L6_2atmpS1495
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS254
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS255, _M0L6_2atmpS1495);
  if (_M0L6_2atmpS1495.$1) {
    moonbit_decref(_M0L6_2atmpS1495.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2538 = _M0MPB13StringBuilder10to__string(_M0L6loggerS254);
  moonbit_decref_cycle_free(_M0L6loggerS254);
  return _result_2538;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS249,
  struct _M0TPB6Logger _M0L6loggerS248
) {
  moonbit_string_t _M0L6_2atmpS1492;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1492 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS249);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248.$0->$method_0(_M0L6loggerS248.$1, _M0L6_2atmpS1492);
  moonbit_decref_cycle_free(_M0L6_2atmpS1492);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS1493;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1493 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS1493);
  moonbit_decref_cycle_free(_M0L6_2atmpS1493);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS1494;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1494 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS1494);
  moonbit_decref_cycle_free(_M0L6_2atmpS1494);
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
  moonbit_string_t _M0L8_2afieldS2377;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2377 = _M0L4selfS246.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2377);
  return _M0L8_2afieldS2377;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS242,
  moonbit_string_t _M0L5valueS243,
  int32_t _M0L5startS244,
  int32_t _M0L3lenS245
) {
  int32_t _M0L6_2atmpS1491;
  int64_t _M0L6_2atmpS1490;
  struct _M0TPC16string10StringView _M0L6_2atmpS1489;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1491 = _M0L5startS244 + _M0L3lenS245;
  _M0L6_2atmpS1490 = (int64_t)_M0L6_2atmpS1491;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1489
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS243, _M0L5startS244, _M0L6_2atmpS1490);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS242, _M0L6_2atmpS1489);
  moonbit_decref_cycle_free(_M0L6_2atmpS1489.$0);
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
  int32_t _M0L6_2atmpS1473;
  int32_t _if__result_2539;
  int32_t _M0L6_2atmpS1481;
  int32_t _if__result_2540;
  int32_t _M0L6_2atmpS1483;
  int32_t _M0L6_2atmpS1484;
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
  _M0L6_2atmpS1473 = _M0Lm2loS236;
  if (_M0L6_2atmpS1473 > 0) {
    int32_t _M0L6_2atmpS1472 = _M0Lm2loS236;
    if (_M0L6_2atmpS1472 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1471 = _M0Lm2loS236;
      int32_t _M0L6_2atmpS1470 = _M0L4selfS235[_M0L6_2atmpS1471];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1470)) {
        int32_t _M0L6_2atmpS1469 = _M0Lm2loS236;
        int32_t _M0L6_2atmpS1468 = _M0L6_2atmpS1469 - 1;
        int32_t _M0L6_2atmpS1467 = _M0L4selfS235[_M0L6_2atmpS1468];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2539
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1467);
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
    int32_t _M0L6_2atmpS1474 = _M0Lm2loS236;
    _M0Lm2loS236 = _M0L6_2atmpS1474 + 1;
  }
  _M0L6_2atmpS1481 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1481 > 0) {
    int32_t _M0L6_2atmpS1480 = _M0Lm2hiS238;
    if (_M0L6_2atmpS1480 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1479 = _M0Lm2hiS238;
      int32_t _M0L6_2atmpS1478 = _M0L4selfS235[_M0L6_2atmpS1479];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1478)) {
        int32_t _M0L6_2atmpS1477 = _M0Lm2hiS238;
        int32_t _M0L6_2atmpS1476 = _M0L6_2atmpS1477 - 1;
        int32_t _M0L6_2atmpS1475 = _M0L4selfS235[_M0L6_2atmpS1476];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2540
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1475);
      } else {
        _if__result_2540 = 0;
      }
    } else {
      _if__result_2540 = 0;
    }
  } else {
    _if__result_2540 = 0;
  }
  if (_if__result_2540) {
    int32_t _M0L6_2atmpS1482 = _M0Lm2hiS238;
    _M0Lm2hiS238 = _M0L6_2atmpS1482 - 1;
  }
  _M0L6_2atmpS1483 = _M0Lm2loS236;
  _M0L6_2atmpS1484 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1483 >= _M0L6_2atmpS1484) {
    int32_t _M0L6_2atmpS1485 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1486 = _M0Lm2loS236;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1485,
                                                 .$2 = _M0L6_2atmpS1486};
  } else {
    int32_t _M0L6_2atmpS1487 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1488 = _M0Lm2hiS238;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1487,
                                                 .$2 = _M0L6_2atmpS1488};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS1466;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS1466
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS1466);
  if (_M0L6_2atmpS1466.$1) {
    moonbit_decref(_M0L6_2atmpS1466.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS231,
  struct _M0TPB4Show _M0L4showS230
) {
  struct _M0TPB6Logger _M0L6_2atmpS1465;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS231);
  _M0L6_2atmpS1465
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS231
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS230.$0->$method_0(_M0L4showS230.$1, _M0L6_2atmpS1465);
  if (_M0L6_2atmpS1465.$1) {
    moonbit_decref(_M0L6_2atmpS1465.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS229) {
  int64_t _M0L6_2atmpS1464;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1464 = (int64_t)_M0L4selfS229;
  return *(uint64_t*)&_M0L6_2atmpS1464;
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
  int32_t _M0L6_2atmpS1463;
  struct _M0TPC16string10StringView _M0L6_2atmpS1461;
  struct _M0TPB6Logger _M0L6_2atmpS1462;
  moonbit_string_t _result_2541;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1463 = Moonbit_array_length(_M0L4selfS227);
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS1461
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS227, .$1 = 0, .$2 = _M0L6_2atmpS1463
  };
  moonbit_incref_cycle_free(_M0L3bufS226);
  _M0L6_2atmpS1462
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS226
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1461, _M0L6_2atmpS1462, _M0L5quoteS228);
  moonbit_decref_cycle_free(_M0L6_2atmpS1461.$0);
  if (_M0L6_2atmpS1462.$1) {
    moonbit_decref(_M0L6_2atmpS1462.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2541 = _M0MPB13StringBuilder10to__string(_M0L3bufS226);
  moonbit_decref_cycle_free(_M0L3bufS226);
  return _result_2541;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS218,
  struct _M0TPB6Logger _M0L6loggerS216,
  int32_t _M0L5quoteS215
) {
  int32_t _M0L3endS1459;
  int32_t _M0L5startS1460;
  int32_t _M0L3lenS217;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS219;
  int32_t _M0L1iS220;
  int32_t _M0L3segS221;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS215) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  _M0L3endS1459 = _M0L4selfS218.$2;
  _M0L5startS1460 = _M0L4selfS218.$1;
  _M0L3lenS217 = _M0L3endS1459 - _M0L5startS1460;
  moonbit_incref_cycle_free(_M0L4selfS218.$0);
  if (_M0L6loggerS216.$1) {
    moonbit_incref(_M0L6loggerS216.$1);
  }
  _M0L6_2aenvS219
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 79, 0);
  _M0L6_2aenvS219->$0 = _M0L4selfS218;
  _M0L6_2aenvS219->$1 = _M0L6loggerS216;
  _M0L1iS220 = 0;
  _M0L3segS221 = 0;
  _2afor_222:;
  while (1) {
    moonbit_string_t _M0L3strS1456;
    int32_t _M0L5startS1458;
    int32_t _M0L6_2atmpS1457;
    int32_t _M0L4codeS223;
    int32_t _M0L1cS225;
    int32_t _M0L6_2atmpS1440;
    int32_t _M0L6_2atmpS1441;
    int32_t _M0L6_2atmpS1442;
    if (_M0L1iS220 >= _M0L3lenS217) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
      moonbit_decref_cycle_free(_M0L6_2aenvS219);
      break;
    }
    _M0L3strS1456 = _M0L4selfS218.$0;
    _M0L5startS1458 = _M0L4selfS218.$1;
    _M0L6_2atmpS1457 = _M0L5startS1458 + _M0L1iS220;
    _M0L4codeS223 = _M0L3strS1456[_M0L6_2atmpS1457];
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
        int32_t _M0L6_2atmpS1443;
        int32_t _M0L6_2atmpS1444;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_18.data);
        _M0L6_2atmpS1443 = _M0L1iS220 + 1;
        _M0L6_2atmpS1444 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1443;
        _M0L3segS221 = _M0L6_2atmpS1444;
        goto _2afor_222;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1445;
        int32_t _M0L6_2atmpS1446;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_19.data);
        _M0L6_2atmpS1445 = _M0L1iS220 + 1;
        _M0L6_2atmpS1446 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1445;
        _M0L3segS221 = _M0L6_2atmpS1446;
        goto _2afor_222;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1447;
        int32_t _M0L6_2atmpS1448;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_20.data);
        _M0L6_2atmpS1447 = _M0L1iS220 + 1;
        _M0L6_2atmpS1448 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1447;
        _M0L3segS221 = _M0L6_2atmpS1448;
        goto _2afor_222;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1449;
        int32_t _M0L6_2atmpS1450;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS1449 = _M0L1iS220 + 1;
        _M0L6_2atmpS1450 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1449;
        _M0L3segS221 = _M0L6_2atmpS1450;
        goto _2afor_222;
        break;
      }
      default: {
        if (_M0L4codeS223 < 32) {
          int32_t _M0L6_2atmpS1452;
          moonbit_string_t _M0L6_2atmpS1451;
          int32_t _M0L6_2atmpS1453;
          int32_t _M0L6_2atmpS1454;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_22.data);
          _M0L6_2atmpS1452 = _M0L4codeS223 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1451 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1452);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, _M0L6_2atmpS1451);
          moonbit_decref_cycle_free(_M0L6_2atmpS1451);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1453 = _M0L1iS220 + 1;
          _M0L6_2atmpS1454 = _M0L1iS220 + 1;
          _M0L1iS220 = _M0L6_2atmpS1453;
          _M0L3segS221 = _M0L6_2atmpS1454;
          goto _2afor_222;
        } else {
          int32_t _M0L6_2atmpS1455 = _M0L1iS220 + 1;
          int32_t _tmp_2544 = _M0L3segS221;
          _M0L1iS220 = _M0L6_2atmpS1455;
          _M0L3segS221 = _tmp_2544;
          goto _2afor_222;
        }
        break;
      }
    }
    goto joinlet_2543;
    join_224:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1440 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS225);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, _M0L6_2atmpS1440);
    _M0L6_2atmpS1441 = _M0L1iS220 + 1;
    _M0L6_2atmpS1442 = _M0L1iS220 + 1;
    _M0L1iS220 = _M0L6_2atmpS1441;
    _M0L3segS221 = _M0L6_2atmpS1442;
    continue;
    joinlet_2543:;
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
    int64_t _M0L6_2atmpS1439 = (int64_t)_M0L1iS213;
    struct _M0TPC16string10StringView _M0L6_2atmpS1438;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1438
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS212, _M0L3segS214, _M0L6_2atmpS1439);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS210.$0->$method_2(_M0L6loggerS210.$1, _M0L6_2atmpS1438);
    moonbit_decref_cycle_free(_M0L6_2atmpS1438.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS201,
  int32_t _M0L5startS203,
  int64_t _M0L3endS205
) {
  int32_t _M0L3endS1436;
  int32_t _M0L5startS1437;
  int32_t _M0L3lenS200;
  int32_t _M0Lm2loS202;
  int32_t _M0Lm2hiS204;
  moonbit_string_t _M0L3strS208;
  int32_t _M0L4baseS209;
  int32_t _M0L6_2atmpS1414;
  int32_t _if__result_2545;
  int32_t _M0L6_2atmpS1424;
  int32_t _if__result_2546;
  int32_t _M0L6_2atmpS1426;
  int32_t _M0L6_2atmpS1427;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1436 = _M0L4selfS201.$2;
  _M0L5startS1437 = _M0L4selfS201.$1;
  _M0L3lenS200 = _M0L3endS1436 - _M0L5startS1437;
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
  _M0L6_2atmpS1414 = _M0Lm2loS202;
  if (_M0L6_2atmpS1414 > 0) {
    int32_t _M0L6_2atmpS1413 = _M0Lm2loS202;
    if (_M0L6_2atmpS1413 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1412 = _M0Lm2loS202;
      int32_t _M0L6_2atmpS1411 = _M0L4baseS209 + _M0L6_2atmpS1412;
      int32_t _M0L6_2atmpS1410 = _M0L3strS208[_M0L6_2atmpS1411];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1410)) {
        int32_t _M0L6_2atmpS1409 = _M0Lm2loS202;
        int32_t _M0L6_2atmpS1408 = _M0L4baseS209 + _M0L6_2atmpS1409;
        int32_t _M0L6_2atmpS1407 = _M0L6_2atmpS1408 - 1;
        int32_t _M0L6_2atmpS1406 = _M0L3strS208[_M0L6_2atmpS1407];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2545
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1406);
      } else {
        _if__result_2545 = 0;
      }
    } else {
      _if__result_2545 = 0;
    }
  } else {
    _if__result_2545 = 0;
  }
  if (_if__result_2545) {
    int32_t _M0L6_2atmpS1415 = _M0Lm2loS202;
    _M0Lm2loS202 = _M0L6_2atmpS1415 + 1;
  }
  _M0L6_2atmpS1424 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1424 > 0) {
    int32_t _M0L6_2atmpS1423 = _M0Lm2hiS204;
    if (_M0L6_2atmpS1423 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1422 = _M0Lm2hiS204;
      int32_t _M0L6_2atmpS1421 = _M0L4baseS209 + _M0L6_2atmpS1422;
      int32_t _M0L6_2atmpS1420 = _M0L3strS208[_M0L6_2atmpS1421];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1420)) {
        int32_t _M0L6_2atmpS1419 = _M0Lm2hiS204;
        int32_t _M0L6_2atmpS1418 = _M0L4baseS209 + _M0L6_2atmpS1419;
        int32_t _M0L6_2atmpS1417 = _M0L6_2atmpS1418 - 1;
        int32_t _M0L6_2atmpS1416 = _M0L3strS208[_M0L6_2atmpS1417];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2546
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1416);
      } else {
        _if__result_2546 = 0;
      }
    } else {
      _if__result_2546 = 0;
    }
  } else {
    _if__result_2546 = 0;
  }
  if (_if__result_2546) {
    int32_t _M0L6_2atmpS1425 = _M0Lm2hiS204;
    _M0Lm2hiS204 = _M0L6_2atmpS1425 - 1;
  }
  _M0L6_2atmpS1426 = _M0Lm2loS202;
  _M0L6_2atmpS1427 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1426 >= _M0L6_2atmpS1427) {
    int32_t _M0L6_2atmpS1431 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1428 = _M0L4baseS209 + _M0L6_2atmpS1431;
    int32_t _M0L6_2atmpS1430 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1429 = _M0L4baseS209 + _M0L6_2atmpS1430;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1428,
                                                 .$2 = _M0L6_2atmpS1429};
  } else {
    int32_t _M0L6_2atmpS1435 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1432 = _M0L4baseS209 + _M0L6_2atmpS1435;
    int32_t _M0L6_2atmpS1434 = _M0Lm2hiS204;
    int32_t _M0L6_2atmpS1433 = _M0L4baseS209 + _M0L6_2atmpS1434;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1432,
                                                 .$2 = _M0L6_2atmpS1433};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS199) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS198;
  int32_t _M0L6_2atmpS1403;
  int32_t _M0L6_2atmpS1402;
  int32_t _M0L6_2atmpS1405;
  int32_t _M0L6_2atmpS1404;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1401;
  moonbit_string_t _result_2547;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1403 = _M0IPC14byte4BytePB3Div3div(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1402
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1403);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1402);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1405 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1404
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1405);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1404);
  _M0L6_2atmpS1401 = _M0L7_2aselfS198;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2547 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1401);
  moonbit_decref_cycle_free(_M0L6_2atmpS1401);
  return _result_2547;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS197) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS197 < 10) {
    int32_t _M0L6_2atmpS1398;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1398 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1398);
  } else {
    int32_t _M0L6_2atmpS1400;
    int32_t _M0L6_2atmpS1399;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1400 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1399 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1400, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1399);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS1396;
  int32_t _M0L6_2atmpS1397;
  int32_t _M0L6_2atmpS1395;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1396 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS1397 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS1395 = _M0L6_2atmpS1396 - _M0L6_2atmpS1397;
  return _M0L6_2atmpS1395 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS1393;
  int32_t _M0L6_2atmpS1394;
  int32_t _M0L6_2atmpS1392;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1393 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS1394 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS1392 = _M0L6_2atmpS1393 % _M0L6_2atmpS1394;
  return _M0L6_2atmpS1392 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS1390;
  int32_t _M0L6_2atmpS1391;
  int32_t _M0L6_2atmpS1389;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1390 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS1391 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS1389 = _M0L6_2atmpS1390 / _M0L6_2atmpS1391;
  return _M0L6_2atmpS1389 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS189,
  int32_t _M0L4thatS190
) {
  int32_t _M0L6_2atmpS1387;
  int32_t _M0L6_2atmpS1388;
  int32_t _M0L6_2atmpS1386;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1387 = (int32_t)_M0L4selfS189;
  _M0L6_2atmpS1388 = (int32_t)_M0L4thatS190;
  _M0L6_2atmpS1386 = _M0L6_2atmpS1387 + _M0L6_2atmpS1388;
  return _M0L6_2atmpS1386 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS188) {
  int32_t _M0L6_2atmpS1385;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1385 = (int32_t)_M0L4selfS188;
  return _M0L6_2atmpS1385;
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
  int32_t _M0L3lenS1384;
  int32_t _M0L8requiredS184;
  uint16_t* _M0L4dataS1379;
  int32_t _M0L6_2atmpS1378;
  int32_t _if__result_2548;
  uint16_t* _M0L4dataS1380;
  int32_t _M0L3lenS1381;
  int32_t _M0L3lenS1383;
  int32_t _M0L6_2atmpS1382;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS182 = Moonbit_array_length(_M0L3strS183);
  if (_M0L8str__lenS182 == 0) {
    return 0;
  }
  _M0L3lenS1384 = _M0L4selfS185->$1;
  _M0L8requiredS184 = _M0L3lenS1384 + _M0L8str__lenS182;
  _M0L4dataS1379 = _M0L4selfS185->$0;
  _M0L6_2atmpS1378 = Moonbit_array_length(_M0L4dataS1379);
  if (_M0L8requiredS184 > _M0L6_2atmpS1378) {
    _if__result_2548 = 1;
  } else {
    int32_t _M0L3lenS1377 = _M0L4selfS185->$1;
    _if__result_2548 = _M0L8requiredS184 < _M0L3lenS1377;
  }
  if (_if__result_2548) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS185, _M0L8requiredS184);
  }
  _M0L4dataS1380 = _M0L4selfS185->$0;
  _M0L3lenS1381 = _M0L4selfS185->$1;
  moonbit_incref_cycle_free(_M0L4dataS1380);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1380, _M0L3lenS1381, _M0L3strS183, 0, _M0L8str__lenS182);
  moonbit_decref_cycle_free(_M0L4dataS1380);
  _M0L3lenS1383 = _M0L4selfS185->$1;
  _M0L6_2atmpS1382 = _M0L3lenS1383 + _M0L8str__lenS182;
  _M0L4selfS185->$1 = _M0L6_2atmpS1382;
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
      int32_t _M0L6_2atmpS1374 = _M0L3strS179[_M0L1iS176];
      int32_t _M0L6_2atmpS1375;
      int32_t _M0L6_2atmpS1376;
      _M0L4selfS178[_M0L1jS177] = _M0L6_2atmpS1374;
      _M0L6_2atmpS1375 = _M0L1iS176 + 1;
      _M0L6_2atmpS1376 = _M0L1jS177 + 1;
      _M0L1iS176 = _M0L6_2atmpS1375;
      _M0L1jS177 = _M0L6_2atmpS1376;
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
    int32_t _M0L3lenS1345 = _M0L4selfS171->$1;
    uint16_t* _M0L4dataS1347 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1346 = Moonbit_array_length(_M0L4dataS1347);
    uint16_t* _M0L4dataS1350;
    int32_t _M0L3lenS1351;
    int32_t _M0L6_2atmpS1352;
    int32_t _M0L3lenS1354;
    int32_t _M0L6_2atmpS1353;
    if (_M0L3lenS1345 >= _M0L6_2atmpS1346) {
      int32_t _M0L3lenS1349 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1348 = _M0L3lenS1349 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1348);
    }
    _M0L4dataS1350 = _M0L4selfS171->$0;
    _M0L3lenS1351 = _M0L4selfS171->$1;
    moonbit_incref_cycle_free(_M0L4dataS1350);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1352 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS169);
    if (
      _M0L3lenS1351 < 0
      || _M0L3lenS1351 >= Moonbit_array_length(_M0L4dataS1350)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1350[_M0L3lenS1351] = _M0L6_2atmpS1352;
    moonbit_decref_cycle_free(_M0L4dataS1350);
    _M0L3lenS1354 = _M0L4selfS171->$1;
    _M0L6_2atmpS1353 = _M0L3lenS1354 + 1;
    _M0L4selfS171->$1 = _M0L6_2atmpS1353;
  } else if (_M0L4codeS169 <= 1114111u) {
    uint16_t* _M0L4dataS1358 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1356 = Moonbit_array_length(_M0L4dataS1358);
    int32_t _M0L3lenS1357 = _M0L4selfS171->$1;
    int32_t _M0L6_2atmpS1355 = _M0L6_2atmpS1356 - _M0L3lenS1357;
    uint32_t _M0L4codeS172;
    uint16_t* _M0L4dataS1361;
    int32_t _M0L3lenS1362;
    uint32_t _M0L6_2atmpS1365;
    uint32_t _M0L6_2atmpS1364;
    int32_t _M0L6_2atmpS1363;
    uint16_t* _M0L4dataS1366;
    int32_t _M0L3lenS1371;
    int32_t _M0L6_2atmpS1367;
    uint32_t _M0L6_2atmpS1370;
    uint32_t _M0L6_2atmpS1369;
    int32_t _M0L6_2atmpS1368;
    int32_t _M0L3lenS1373;
    int32_t _M0L6_2atmpS1372;
    if (_M0L6_2atmpS1355 < 2) {
      int32_t _M0L3lenS1360 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1359 = _M0L3lenS1360 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1359);
    }
    _M0L4codeS172 = _M0L4codeS169 - 65536u;
    _M0L4dataS1361 = _M0L4selfS171->$0;
    _M0L3lenS1362 = _M0L4selfS171->$1;
    _M0L6_2atmpS1365 = _M0L4codeS172 >> 10;
    _M0L6_2atmpS1364 = 55296u + _M0L6_2atmpS1365;
    moonbit_incref_cycle_free(_M0L4dataS1361);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1363 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1364);
    if (
      _M0L3lenS1362 < 0
      || _M0L3lenS1362 >= Moonbit_array_length(_M0L4dataS1361)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1361[_M0L3lenS1362] = _M0L6_2atmpS1363;
    moonbit_decref_cycle_free(_M0L4dataS1361);
    _M0L4dataS1366 = _M0L4selfS171->$0;
    _M0L3lenS1371 = _M0L4selfS171->$1;
    _M0L6_2atmpS1367 = _M0L3lenS1371 + 1;
    _M0L6_2atmpS1370 = _M0L4codeS172 & 1023u;
    _M0L6_2atmpS1369 = 56320u + _M0L6_2atmpS1370;
    moonbit_incref_cycle_free(_M0L4dataS1366);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1368 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1369);
    if (
      _M0L6_2atmpS1367 < 0
      || _M0L6_2atmpS1367 >= Moonbit_array_length(_M0L4dataS1366)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1366[_M0L6_2atmpS1367] = _M0L6_2atmpS1368;
    moonbit_decref_cycle_free(_M0L4dataS1366);
    _M0L3lenS1373 = _M0L4selfS171->$1;
    _M0L6_2atmpS1372 = _M0L3lenS1373 + 2;
    _M0L4selfS171->$1 = _M0L6_2atmpS1372;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_23.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS166,
  int32_t _M0L8requiredS167
) {
  uint16_t* _M0L4dataS1344;
  int32_t _M0L6_2atmpS1342;
  int32_t _M0L3lenS1343;
  int32_t _M0L13new__capacityS165;
  uint16_t* _M0L4dataS1339;
  int32_t _M0L6_2atmpS1340;
  int32_t _M0L3lenS1341;
  uint16_t* _M0L9new__dataS168;
  uint16_t* _M0L6_2aoldS2378;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1344 = _M0L4selfS166->$0;
  _M0L6_2atmpS1342 = Moonbit_array_length(_M0L4dataS1344);
  _M0L3lenS1343 = _M0L4selfS166->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS165
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1342, _M0L3lenS1343, _M0L8requiredS167);
  _M0L4dataS1339 = _M0L4selfS166->$0;
  moonbit_incref_cycle_free(_M0L4dataS1339);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1340 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1341 = _M0L4selfS166->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS168
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1339, _M0L13new__capacityS165, _M0L6_2atmpS1340, _M0L3lenS1341, 0, 0);
  _M0L6_2aoldS2378 = _M0L4selfS166->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2378);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_24.data);
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
  int32_t _M0L6_2atmpS1338;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1338 = *(int32_t*)&_M0L4selfS158;
  return (uint16_t)_M0L6_2atmpS1338;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS157) {
  int32_t _M0L6_2atmpS1337;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1337 = _M0L4selfS157;
  return *(uint32_t*)&_M0L6_2atmpS1337;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS155
) {
  int32_t _M0L3lenS1328;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1328 = _M0L4selfS155->$1;
  if (_M0L3lenS1328 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1329 = _M0L4selfS155->$1;
    uint16_t* _M0L4dataS1331 = _M0L4selfS155->$0;
    int32_t _M0L6_2atmpS1330 = Moonbit_array_length(_M0L4dataS1331);
    if (_M0L3lenS1329 == _M0L6_2atmpS1330) {
      uint16_t* _M0L4dataS1332 = _M0L4selfS155->$0;
      moonbit_incref_cycle_free(_M0L4dataS1332);
      return _M0L4dataS1332;
    } else {
      uint16_t* _M0L4dataS1333 = _M0L4selfS155->$0;
      int32_t _M0L3lenS1334 = _M0L4selfS155->$1;
      int32_t _M0L6_2atmpS1335;
      int32_t _M0L3lenS1336;
      uint16_t* _M0L4dataS156;
      moonbit_incref_cycle_free(_M0L4dataS1333);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1335 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1336 = _M0L4selfS155->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS156
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1333, _M0L3lenS1334, _M0L6_2atmpS1335, _M0L3lenS1336, 0, 0);
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
  int32_t _if__result_2551;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS148 >= 0) {
    if (_M0L3lenS149 >= 0) {
      if (_M0L11src__offsetS150 >= 0) {
        if (_M0L11dst__offsetS151 >= 0) {
          int32_t _M0L6_2atmpS1324 = _M0L11src__offsetS150 + _M0L3lenS149;
          int32_t _M0L6_2atmpS1325 = Moonbit_array_length(_M0L3srcS152);
          if (_M0L6_2atmpS1324 <= _M0L6_2atmpS1325) {
            int32_t _M0L6_2atmpS1323 = _M0L11dst__offsetS151 + _M0L3lenS149;
            _if__result_2551 = _M0L6_2atmpS1323 <= _M0L13allocate__lenS148;
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
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS152, _M0L13allocate__lenS148, _M0L4initS153, _M0L11src__offsetS150, _M0L11dst__offsetS151, _M0L3lenS149);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS154;
    int32_t _M0L6_2atmpS1327;
    moonbit_string_t _M0L6_2atmpS1326;
    uint16_t* _result_2552;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS154
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L13allocate__lenS148);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11src__offsetS150);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11dst__offsetS151);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L3lenS149);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_29.data);
    _M0L6_2atmpS1327 = Moonbit_array_length(_M0L3srcS152);
    moonbit_decref_cycle_free(_M0L3srcS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L6_2atmpS1327);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1326
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS154);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS154);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2552 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1326);
    moonbit_decref_cycle_free(_M0L6_2atmpS1326);
    return _result_2552;
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
  struct _M0TPB13StringBuilder* _block_2553;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS139 < 1) {
    _M0L7initialS138 = 1;
  } else {
    int32_t _M0L6_2atmpS1322 = _M0L10size__hintS139 + 1;
    _M0L7initialS138 = _M0L6_2atmpS1322 / 2;
  }
  _M0L4dataS140 = (uint16_t*)moonbit_make_string(_M0L7initialS138, 0);
  _block_2553
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2553)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 84, 0);
  _block_2553->$0 = _M0L4dataS140;
  _block_2553->$1 = 0;
  return _block_2553;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS137) {
  int32_t _M0L6_2atmpS1321;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1321 = (int32_t)_M0L4selfS137;
  return _M0L6_2atmpS1321;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS117,
  int32_t _M0L13allocate__lenS113,
  int32_t _M0L3lenS114,
  int32_t _M0L11src__offsetS115,
  int32_t _M0L11dst__offsetS116
) {
  int32_t _if__result_2554;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS113 >= 0) {
    if (_M0L3lenS114 >= 0) {
      if (_M0L11src__offsetS115 >= 0) {
        if (_M0L11dst__offsetS116 >= 0) {
          int32_t _M0L6_2atmpS1302 = _M0L11src__offsetS115 + _M0L3lenS114;
          int32_t _M0L6_2atmpS1303;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1303
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS117);
          if (_M0L6_2atmpS1302 <= _M0L6_2atmpS1303) {
            int32_t _M0L6_2atmpS1301 = _M0L11dst__offsetS116 + _M0L3lenS114;
            _if__result_2554 = _M0L6_2atmpS1301 <= _M0L13allocate__lenS113;
          } else {
            _if__result_2554 = 0;
          }
        } else {
          _if__result_2554 = 0;
        }
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
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS113, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS117, _M0L11src__offsetS115, _M0L11dst__offsetS116, _M0L3lenS114);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS118;
    int32_t _M0L6_2atmpS1305;
    moonbit_string_t _M0L6_2atmpS1304;
    moonbit_string_t* _result_2555;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS118
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L13allocate__lenS113);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11src__offsetS115);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11dst__offsetS116);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L3lenS114);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1305 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS117);
    moonbit_decref_cycle_free(_M0L3srcS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L6_2atmpS1305);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1304
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS118);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS118);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2555
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1304);
    moonbit_decref_cycle_free(_M0L6_2atmpS1304);
    return _result_2555;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS123,
  int32_t _M0L13allocate__lenS119,
  int32_t _M0L3lenS120,
  int32_t _M0L11src__offsetS121,
  int32_t _M0L11dst__offsetS122
) {
  int32_t _if__result_2556;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS119 >= 0) {
    if (_M0L3lenS120 >= 0) {
      if (_M0L11src__offsetS121 >= 0) {
        if (_M0L11dst__offsetS122 >= 0) {
          int32_t _M0L6_2atmpS1307 = _M0L11src__offsetS121 + _M0L3lenS120;
          int32_t _M0L6_2atmpS1308;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1308
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS123);
          if (_M0L6_2atmpS1307 <= _M0L6_2atmpS1308) {
            int32_t _M0L6_2atmpS1306 = _M0L11dst__offsetS122 + _M0L3lenS120;
            _if__result_2556 = _M0L6_2atmpS1306 <= _M0L13allocate__lenS119;
          } else {
            _if__result_2556 = 0;
          }
        } else {
          _if__result_2556 = 0;
        }
      } else {
        _if__result_2556 = 0;
      }
    } else {
      _if__result_2556 = 0;
    }
  } else {
    _if__result_2556 = 0;
  }
  if (_if__result_2556) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS119, 0, _M0L3srcS123, _M0L11src__offsetS121, _M0L11dst__offsetS122, _M0L3lenS120);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS124;
    int32_t _M0L6_2atmpS1310;
    moonbit_string_t _M0L6_2atmpS1309;
    struct _M0TUsiE** _result_2557;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS124
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L13allocate__lenS119);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11src__offsetS121);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11dst__offsetS122);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L3lenS120);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1310 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS123);
    moonbit_decref_cycle_free(_M0L3srcS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L6_2atmpS1310);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1309
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS124);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS124);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2557
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1309);
    moonbit_decref_cycle_free(_M0L6_2atmpS1309);
    return _result_2557;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS129,
  int32_t _M0L13allocate__lenS125,
  int32_t _M0L3lenS126,
  int32_t _M0L11src__offsetS127,
  int32_t _M0L11dst__offsetS128
) {
  int32_t _if__result_2558;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS125 >= 0) {
    if (_M0L3lenS126 >= 0) {
      if (_M0L11src__offsetS127 >= 0) {
        if (_M0L11dst__offsetS128 >= 0) {
          int32_t _M0L6_2atmpS1312 = _M0L11src__offsetS127 + _M0L3lenS126;
          int32_t _M0L6_2atmpS1313;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1313
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS129);
          if (_M0L6_2atmpS1312 <= _M0L6_2atmpS1313) {
            int32_t _M0L6_2atmpS1311 = _M0L11dst__offsetS128 + _M0L3lenS126;
            _if__result_2558 = _M0L6_2atmpS1311 <= _M0L13allocate__lenS125;
          } else {
            _if__result_2558 = 0;
          }
        } else {
          _if__result_2558 = 0;
        }
      } else {
        _if__result_2558 = 0;
      }
    } else {
      _if__result_2558 = 0;
    }
  } else {
    _if__result_2558 = 0;
  }
  if (_if__result_2558) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS129, _M0L13allocate__lenS125, _M0L11src__offsetS127, _M0L11dst__offsetS128, _M0L3lenS126);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS130;
    int32_t _M0L6_2atmpS1315;
    moonbit_string_t _M0L6_2atmpS1314;
    int32_t* _result_2559;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS130
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L13allocate__lenS125);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11src__offsetS127);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11dst__offsetS128);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L3lenS126);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1315 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS129);
    moonbit_decref_cycle_free(_M0L3srcS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L6_2atmpS1315);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1314
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS130);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS130);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2559
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1314);
    moonbit_decref_cycle_free(_M0L6_2atmpS1314);
    return _result_2559;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS135,
  int32_t _M0L13allocate__lenS131,
  int32_t _M0L3lenS132,
  int32_t _M0L11src__offsetS133,
  int32_t _M0L11dst__offsetS134
) {
  int32_t _if__result_2560;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS131 >= 0) {
    if (_M0L3lenS132 >= 0) {
      if (_M0L11src__offsetS133 >= 0) {
        if (_M0L11dst__offsetS134 >= 0) {
          int32_t _M0L6_2atmpS1317 = _M0L11src__offsetS133 + _M0L3lenS132;
          int32_t _M0L6_2atmpS1318;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1318
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS135);
          if (_M0L6_2atmpS1317 <= _M0L6_2atmpS1318) {
            int32_t _M0L6_2atmpS1316 = _M0L11dst__offsetS134 + _M0L3lenS132;
            _if__result_2560 = _M0L6_2atmpS1316 <= _M0L13allocate__lenS131;
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
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS135, _M0L13allocate__lenS131, _M0L11src__offsetS133, _M0L11dst__offsetS134, _M0L3lenS132);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS136;
    int32_t _M0L6_2atmpS1320;
    moonbit_string_t _M0L6_2atmpS1319;
    float* _result_2561;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS136
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L13allocate__lenS131);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11src__offsetS133);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11dst__offsetS134);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L3lenS132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1320 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS135);
    moonbit_decref_cycle_free(_M0L3srcS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L6_2atmpS1320);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1319
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS136);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS136);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2561
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1319);
    moonbit_decref_cycle_free(_M0L6_2atmpS1319);
    return _result_2561;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L3objS107
) {
  struct _M0TPB6Logger _M0L6_2atmpS1298;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS108);
  _M0L6_2atmpS1298
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS108
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS107, _M0L6_2atmpS1298);
  if (_M0L6_2atmpS1298.$1) {
    moonbit_decref(_M0L6_2atmpS1298.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS1299;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS1299
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS109, _M0L6_2atmpS1299);
  if (_M0L6_2atmpS1299.$1) {
    moonbit_decref(_M0L6_2atmpS1299.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  uint64_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS1300;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS1300
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS111, _M0L6_2atmpS1300);
  if (_M0L6_2atmpS1300.$1) {
    moonbit_decref(_M0L6_2atmpS1300.$1);
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
        int32_t _M0L6_2atmpS1253 = _M0L11dst__offsetS20 + _M0L1iS22;
        int32_t _M0L6_2atmpS1255 = _M0L11src__offsetS21 + _M0L1iS22;
        int32_t _M0L6_2atmpS1254;
        int32_t _M0L6_2atmpS1256;
        if (
          _M0L6_2atmpS1255 < 0
          || _M0L6_2atmpS1255 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1254 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1255];
        if (
          _M0L6_2atmpS1253 < 0
          || _M0L6_2atmpS1253 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1253] = _M0L6_2atmpS1254;
        _M0L6_2atmpS1256 = _M0L1iS22 + 1;
        _M0L1iS22 = _M0L6_2atmpS1256;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1261 = _M0L3lenS23 - 1;
    int32_t _M0L1iS25 = _M0L6_2atmpS1261;
    while (1) {
      if (_M0L1iS25 >= 0) {
        int32_t _M0L6_2atmpS1257 = _M0L11dst__offsetS20 + _M0L1iS25;
        int32_t _M0L6_2atmpS1259 = _M0L11src__offsetS21 + _M0L1iS25;
        int32_t _M0L6_2atmpS1258;
        int32_t _M0L6_2atmpS1260;
        if (
          _M0L6_2atmpS1259 < 0
          || _M0L6_2atmpS1259 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1258 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1259];
        if (
          _M0L6_2atmpS1257 < 0
          || _M0L6_2atmpS1257 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1257] = _M0L6_2atmpS1258;
        _M0L6_2atmpS1260 = _M0L1iS25 - 1;
        _M0L1iS25 = _M0L6_2atmpS1260;
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
        int32_t _M0L6_2atmpS1262 = _M0L11dst__offsetS29 + _M0L1iS31;
        int32_t _M0L6_2atmpS1264 = _M0L11src__offsetS30 + _M0L1iS31;
        moonbit_string_t _M0L6_2atmpS1263;
        moonbit_string_t _M0L6_2aoldS2379;
        int32_t _M0L6_2atmpS1265;
        if (
          _M0L6_2atmpS1264 < 0
          || _M0L6_2atmpS1264 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1263 = (moonbit_string_t)_M0L3srcS28[_M0L6_2atmpS1264];
        if (
          _M0L6_2atmpS1262 < 0
          || _M0L6_2atmpS1262 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2379 = (moonbit_string_t)_M0L3dstS27[_M0L6_2atmpS1262];
        moonbit_incref_cycle_free(_M0L6_2atmpS1263);
        moonbit_decref_cycle_free(_M0L6_2aoldS2379);
        _M0L3dstS27[_M0L6_2atmpS1262] = _M0L6_2atmpS1263;
        _M0L6_2atmpS1265 = _M0L1iS31 + 1;
        _M0L1iS31 = _M0L6_2atmpS1265;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1270 = _M0L3lenS32 - 1;
    int32_t _M0L1iS34 = _M0L6_2atmpS1270;
    while (1) {
      if (_M0L1iS34 >= 0) {
        int32_t _M0L6_2atmpS1266 = _M0L11dst__offsetS29 + _M0L1iS34;
        int32_t _M0L6_2atmpS1268 = _M0L11src__offsetS30 + _M0L1iS34;
        moonbit_string_t _M0L6_2atmpS1267;
        moonbit_string_t _M0L6_2aoldS2380;
        int32_t _M0L6_2atmpS1269;
        if (
          _M0L6_2atmpS1268 < 0
          || _M0L6_2atmpS1268 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1267 = (moonbit_string_t)_M0L3srcS28[_M0L6_2atmpS1268];
        if (
          _M0L6_2atmpS1266 < 0
          || _M0L6_2atmpS1266 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2380 = (moonbit_string_t)_M0L3dstS27[_M0L6_2atmpS1266];
        moonbit_incref_cycle_free(_M0L6_2atmpS1267);
        moonbit_decref_cycle_free(_M0L6_2aoldS2380);
        _M0L3dstS27[_M0L6_2atmpS1266] = _M0L6_2atmpS1267;
        _M0L6_2atmpS1269 = _M0L1iS34 - 1;
        _M0L1iS34 = _M0L6_2atmpS1269;
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
        int32_t _M0L6_2atmpS1271 = _M0L11dst__offsetS38 + _M0L1iS40;
        int32_t _M0L6_2atmpS1273 = _M0L11src__offsetS39 + _M0L1iS40;
        struct _M0TUsiE* _M0L6_2atmpS1272;
        struct _M0TUsiE* _M0L6_2aoldS2381;
        int32_t _M0L6_2atmpS1274;
        if (
          _M0L6_2atmpS1273 < 0
          || _M0L6_2atmpS1273 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1272 = (struct _M0TUsiE*)_M0L3srcS37[_M0L6_2atmpS1273];
        if (
          _M0L6_2atmpS1271 < 0
          || _M0L6_2atmpS1271 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2381 = (struct _M0TUsiE*)_M0L3dstS36[_M0L6_2atmpS1271];
        if (_M0L6_2atmpS1272) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1272);
        }
        if (_M0L6_2aoldS2381) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2381);
        }
        _M0L3dstS36[_M0L6_2atmpS1271] = _M0L6_2atmpS1272;
        _M0L6_2atmpS1274 = _M0L1iS40 + 1;
        _M0L1iS40 = _M0L6_2atmpS1274;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1279 = _M0L3lenS41 - 1;
    int32_t _M0L1iS43 = _M0L6_2atmpS1279;
    while (1) {
      if (_M0L1iS43 >= 0) {
        int32_t _M0L6_2atmpS1275 = _M0L11dst__offsetS38 + _M0L1iS43;
        int32_t _M0L6_2atmpS1277 = _M0L11src__offsetS39 + _M0L1iS43;
        struct _M0TUsiE* _M0L6_2atmpS1276;
        struct _M0TUsiE* _M0L6_2aoldS2382;
        int32_t _M0L6_2atmpS1278;
        if (
          _M0L6_2atmpS1277 < 0
          || _M0L6_2atmpS1277 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1276 = (struct _M0TUsiE*)_M0L3srcS37[_M0L6_2atmpS1277];
        if (
          _M0L6_2atmpS1275 < 0
          || _M0L6_2atmpS1275 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2382 = (struct _M0TUsiE*)_M0L3dstS36[_M0L6_2atmpS1275];
        if (_M0L6_2atmpS1276) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1276);
        }
        if (_M0L6_2aoldS2382) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2382);
        }
        _M0L3dstS36[_M0L6_2atmpS1275] = _M0L6_2atmpS1276;
        _M0L6_2atmpS1278 = _M0L1iS43 - 1;
        _M0L1iS43 = _M0L6_2atmpS1278;
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
        int32_t _M0L6_2atmpS1280 = _M0L11dst__offsetS47 + _M0L1iS49;
        int32_t _M0L6_2atmpS1282 = _M0L11src__offsetS48 + _M0L1iS49;
        int32_t _M0L6_2atmpS1281;
        int32_t _M0L6_2atmpS1283;
        if (
          _M0L6_2atmpS1282 < 0
          || _M0L6_2atmpS1282 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1281 = (int32_t)_M0L3srcS46[_M0L6_2atmpS1282];
        if (
          _M0L6_2atmpS1280 < 0
          || _M0L6_2atmpS1280 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS45[_M0L6_2atmpS1280] = _M0L6_2atmpS1281;
        _M0L6_2atmpS1283 = _M0L1iS49 + 1;
        _M0L1iS49 = _M0L6_2atmpS1283;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1288 = _M0L3lenS50 - 1;
    int32_t _M0L1iS52 = _M0L6_2atmpS1288;
    while (1) {
      if (_M0L1iS52 >= 0) {
        int32_t _M0L6_2atmpS1284 = _M0L11dst__offsetS47 + _M0L1iS52;
        int32_t _M0L6_2atmpS1286 = _M0L11src__offsetS48 + _M0L1iS52;
        int32_t _M0L6_2atmpS1285;
        int32_t _M0L6_2atmpS1287;
        if (
          _M0L6_2atmpS1286 < 0
          || _M0L6_2atmpS1286 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1285 = (int32_t)_M0L3srcS46[_M0L6_2atmpS1286];
        if (
          _M0L6_2atmpS1284 < 0
          || _M0L6_2atmpS1284 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS45[_M0L6_2atmpS1284] = _M0L6_2atmpS1285;
        _M0L6_2atmpS1287 = _M0L1iS52 - 1;
        _M0L1iS52 = _M0L6_2atmpS1287;
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
        int32_t _M0L6_2atmpS1289 = _M0L11dst__offsetS56 + _M0L1iS58;
        int32_t _M0L6_2atmpS1291 = _M0L11src__offsetS57 + _M0L1iS58;
        float _M0L6_2atmpS1290;
        int32_t _M0L6_2atmpS1292;
        if (
          _M0L6_2atmpS1291 < 0
          || _M0L6_2atmpS1291 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1290 = (float)_M0L3srcS55[_M0L6_2atmpS1291];
        if (
          _M0L6_2atmpS1289 < 0
          || _M0L6_2atmpS1289 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS1289] = _M0L6_2atmpS1290;
        _M0L6_2atmpS1292 = _M0L1iS58 + 1;
        _M0L1iS58 = _M0L6_2atmpS1292;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1297 = _M0L3lenS59 - 1;
    int32_t _M0L1iS61 = _M0L6_2atmpS1297;
    while (1) {
      if (_M0L1iS61 >= 0) {
        int32_t _M0L6_2atmpS1293 = _M0L11dst__offsetS56 + _M0L1iS61;
        int32_t _M0L6_2atmpS1295 = _M0L11src__offsetS57 + _M0L1iS61;
        float _M0L6_2atmpS1294;
        int32_t _M0L6_2atmpS1296;
        if (
          _M0L6_2atmpS1295 < 0
          || _M0L6_2atmpS1295 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1294 = (float)_M0L3srcS55[_M0L6_2atmpS1295];
        if (
          _M0L6_2atmpS1293 < 0
          || _M0L6_2atmpS1293 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS1293] = _M0L6_2atmpS1294;
        _M0L6_2atmpS1296 = _M0L1iS61 - 1;
        _M0L1iS61 = _M0L6_2atmpS1296;
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
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_30.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S13, _M0L15_2a_2aarg__6389S12);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_31.data);
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1223) {
  switch (Moonbit_object_tag(_M0L4_2aeS1223)) {
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_32.data;
      break;
    }
    
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_33.data;
      break;
    }
    
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_34.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1223);
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_35.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1248,
  struct _M0TPB4Show _M0L8_2aparamS1247
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1246 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1248;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1246, _M0L8_2aparamS1247);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1245,
  struct _M0TPB4Show _M0L8_2aparamS1244
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1243 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1245;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1243, _M0L8_2aparamS1244);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1242,
  int32_t _M0L8_2aparamS1241
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1240 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1242;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1240, _M0L8_2aparamS1241);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1239,
  struct _M0TPC16string10StringView _M0L8_2aparamS1238
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1237 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1239;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1237, _M0L8_2aparamS1238);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1236,
  moonbit_string_t _M0L8_2aparamS1233,
  int32_t _M0L8_2aparamS1234,
  int32_t _M0L8_2aparamS1235
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1232 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1236;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1232, _M0L8_2aparamS1233, _M0L8_2aparamS1234, _M0L8_2aparamS1235);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1231,
  moonbit_string_t _M0L8_2aparamS1230
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1229 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1231;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1229, _M0L8_2aparamS1230);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_2572 = 9218868437227405311ll;
  int64_t _tmp_2573;
  int64_t _tmp_2574;
  int64_t _tmp_2575;
  int64_t _tmp_2576;
  _M0FPB18double__max__value = *(double*)&_tmp_2572;
  _tmp_2573 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_2573;
  _tmp_2574 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_2574;
  _tmp_2575 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_2575;
  _tmp_2576 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_2576;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1252;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1216;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1217;
  int32_t _M0L7_2abindS1218;
  struct _M0TUsiE** _M0L7_2abindS1219;
  int32_t _M0L6_2acntS2408;
  int32_t _M0L2__S1220;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1252
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1216
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1216)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 87, 0);
  _M0L12async__testsS1216->$0 = _M0L6_2atmpS1252;
  _M0L12async__testsS1216->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1217
  = _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1218 = _M0L7_2abindS1217->$1;
  _M0L7_2abindS1219 = _M0L7_2abindS1217->$0;
  _M0L6_2acntS2408
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1217));
  if (_M0L6_2acntS2408 > 1) {
    int32_t _M0L11_2anew__cntS2409 = _M0L6_2acntS2408 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1217), _M0L11_2anew__cntS2409);
    moonbit_incref_cycle_free(_M0L7_2abindS1219);
  } else if (_M0L6_2acntS2408 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1217);
  }
  _M0L2__S1220 = 0;
  while (1) {
    if (_M0L2__S1220 < _M0L7_2abindS1218) {
      struct _M0TUsiE* _M0L3argS1221 =
        (struct _M0TUsiE*)_M0L7_2abindS1219[_M0L2__S1220];
      moonbit_string_t _M0L6_2atmpS1249 = _M0L3argS1221->$0;
      int32_t _M0L6_2atmpS1250 = _M0L3argS1221->$1;
      int32_t _M0L6_2atmpS1251;
      moonbit_incref_cycle_free(_M0L6_2atmpS1249);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1216, _M0L6_2atmpS1249, _M0L6_2atmpS1250);
      moonbit_decref_cycle_free(_M0L6_2atmpS1249);
      _M0L6_2atmpS1251 = _M0L2__S1220 + 1;
      _M0L2__S1220 = _M0L6_2atmpS1251;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1219);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\metaplasticity\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples30metaplasticity__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1216);
  moonbit_decref_cycle_free(_M0L12async__testsS1216);
  moonbit_flush_cycles();
  return 0;
}