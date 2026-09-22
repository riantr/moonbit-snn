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

struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1083;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TP26RiantR8snn__mbt11WCParameter;

struct _M0TP26RiantR8snn__mbt11RateSynapse;

struct _M0TWRPC15error5ErrorEs;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TPB4Show;

struct _M0TPB8MutLocalGfE;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1078;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TUdiE;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

struct _M0TP26RiantR8snn__mbt11WilsonCowan;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0TPB5ArrayGsE;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TWEu;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0TUddE;

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

struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1083 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure {
  moonbit_string_t $0;
  
};

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError {
  moonbit_string_t $0;
  
};

struct _M0TP26RiantR8snn__mbt11WCParameter {
  float $0;
  
};

struct _M0TP26RiantR8snn__mbt11RateSynapse {
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* $0;
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* $1;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* $2;
  
};

struct _M0TWRPC15error5ErrorEs {
  moonbit_string_t(* code)(struct _M0TWRPC15error5ErrorEs*, void*);
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
};

struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
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

struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1078 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

struct _M0TPB5ArrayGRPB5ArrayGfEE {
  struct _M0TPB5ArrayGfE** $0;
  int32_t $1;
  
};

struct _M0TP26RiantR8snn__mbt11WilsonCowan {
  struct _M0TP26RiantR8snn__mbt11WCParameter* $0;
  int32_t $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGfE* $5;
  
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

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR {
  int32_t $0;
  int32_t $1;
  struct _M0TPB5ArrayGiE* $2;
  struct _M0TPB5ArrayGiE* $3;
  struct _M0TPB5ArrayGfE* $4;
  
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

struct _M0TUddE {
  double $0;
  double $1;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1090(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1083(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1078(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1055(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1048(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples25oja__rule__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

struct _M0TP26RiantR8snn__mbt11RateSynapse* _M0MP26RiantR8snn__mbt11RateSynapse3new(
  struct _M0TP26RiantR8snn__mbt11WilsonCowan*,
  struct _M0TP26RiantR8snn__mbt11WilsonCowan*,
  float,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0MP26RiantR8snn__mbt11WilsonCowan3new(
  int32_t,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt11WCParameter* _M0MP26RiantR8snn__mbt11WCParameter3new(
  
);

int32_t _M0FP26RiantR8snn__mbt8step__wc(
  struct _M0TP26RiantR8snn__mbt11WilsonCowan*,
  float
);

#define _M0FP26RiantR8snn__mbt5tanhf tanhf

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
);

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR13forward__rate(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*,
  struct _M0TPB5ArrayGfE*,
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

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t,
  struct _M0TPB5ArrayGfE*
);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

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

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t
);

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

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

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

float tanhf(float);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[115]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 114, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 111, 106, 97, 95, 114, 117, 108, 
    101, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 
    46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 
    105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 
    105, 112, 84, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 
    84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 
    114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[113]; 
} const moonbit_string_literal_32 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 112, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 111, 106, 97, 95, 114, 117, 108, 
    101, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 
    46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 
    105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 
    69, 114, 114, 111, 114, 46, 77, 111, 111, 110, 66, 105, 116, 84, 
    101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 
    110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 0
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
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1090$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1090
  };

uint32_t const moonbit_layout_table_data[62] =
  {
    sizeof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1078)
    / 4, 1,
    offsetof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1078, $1)
    / 4
    * 2,
    sizeof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1083)
    / 4, 1,
    offsetof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1083, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt11RateSynapse) / 4, 3,
    offsetof(struct _M0TP26RiantR8snn__mbt11RateSynapse, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt11RateSynapse, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt11RateSynapse, $2) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt11WilsonCowan) / 4, 5,
    offsetof(struct _M0TP26RiantR8snn__mbt11WilsonCowan, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt11WilsonCowan, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt11WilsonCowan, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt11WilsonCowan, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt11WilsonCowan, $5) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGiE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGiE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR) / 4, 3,
    offsetof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR, $4) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2198
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1111,
  moonbit_string_t _M0L8filenameS1080,
  int32_t _M0L5indexS1082
) {
  struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1078* _closure_2226;
  struct _M0TWEu* _M0L13handle__startS1078;
  struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1083* _closure_2227;
  struct _M0TWssbEu* _M0L14handle__resultS1083;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1090;
  void* _M0L11_2atry__errS1105;
  struct moonbit_result_0 _tmp_2229;
  int32_t _handle__error__result_2230;
  int32_t _M0L6_2atmpS2186;
  void* _M0L3errS1106;
  moonbit_string_t _M0L4nameS1108;
  struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1109;
  moonbit_string_t _M0L7_2anameS1110;
  int32_t _M0L6_2acntS2220;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1080);
  _closure_2226
  = (struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1078*)moonbit_malloc(sizeof(struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1078));
  Moonbit_object_header(_closure_2226)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2226->code
  = &_M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1078;
  _closure_2226->$0 = _M0L5indexS1082;
  _closure_2226->$1 = _M0L8filenameS1080;
  _M0L13handle__startS1078 = (struct _M0TWEu*)_closure_2226;
  moonbit_incref_cycle_free(_M0L8filenameS1080);
  _closure_2227
  = (struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1083*)moonbit_malloc(sizeof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1083));
  Moonbit_object_header(_closure_2227)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2227->code
  = &_M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1083;
  _closure_2227->$0 = _M0L5indexS1082;
  _closure_2227->$1 = _M0L8filenameS1080;
  _M0L14handle__resultS1083 = (struct _M0TWssbEu*)_closure_2227;
  _M0L17error__to__stringS1090
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1090$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2229
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1111, _M0L8filenameS1080, _M0L5indexS1082, _M0L13handle__startS1078, _M0L14handle__resultS1083, _M0L17error__to__stringS1090);
  if (_tmp_2229.tag) {
    int32_t const _M0L5_2aokS2195 = _tmp_2229.data.ok;
    _handle__error__result_2230 = _M0L5_2aokS2195;
  } else {
    void* const _M0L6_2aerrS2196 = _tmp_2229.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1090);
    moonbit_decref_cycle_free(_M0L13handle__startS1078);
    _M0L11_2atry__errS1105 = _M0L6_2aerrS2196;
    goto join_1104;
  }
  if (_handle__error__result_2230) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1090);
    moonbit_decref_cycle_free(_M0L13handle__startS1078);
    _M0L6_2atmpS2186 = 1;
  } else {
    struct moonbit_result_0 _tmp_2231;
    int32_t _handle__error__result_2232;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2231
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1111, _M0L8filenameS1080, _M0L5indexS1082, _M0L13handle__startS1078, _M0L14handle__resultS1083, _M0L17error__to__stringS1090);
    if (_tmp_2231.tag) {
      int32_t const _M0L5_2aokS2193 = _tmp_2231.data.ok;
      _handle__error__result_2232 = _M0L5_2aokS2193;
    } else {
      void* const _M0L6_2aerrS2194 = _tmp_2231.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1090);
      moonbit_decref_cycle_free(_M0L13handle__startS1078);
      _M0L11_2atry__errS1105 = _M0L6_2aerrS2194;
      goto join_1104;
    }
    if (_handle__error__result_2232) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1090);
      moonbit_decref_cycle_free(_M0L13handle__startS1078);
      _M0L6_2atmpS2186 = 1;
    } else {
      struct moonbit_result_0 _tmp_2233;
      int32_t _handle__error__result_2234;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2233
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1111, _M0L8filenameS1080, _M0L5indexS1082, _M0L13handle__startS1078, _M0L14handle__resultS1083, _M0L17error__to__stringS1090);
      if (_tmp_2233.tag) {
        int32_t const _M0L5_2aokS2191 = _tmp_2233.data.ok;
        _handle__error__result_2234 = _M0L5_2aokS2191;
      } else {
        void* const _M0L6_2aerrS2192 = _tmp_2233.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1090);
        moonbit_decref_cycle_free(_M0L13handle__startS1078);
        _M0L11_2atry__errS1105 = _M0L6_2aerrS2192;
        goto join_1104;
      }
      if (_handle__error__result_2234) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1090);
        moonbit_decref_cycle_free(_M0L13handle__startS1078);
        _M0L6_2atmpS2186 = 1;
      } else {
        struct moonbit_result_0 _tmp_2235;
        int32_t _handle__error__result_2236;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2235
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1111, _M0L8filenameS1080, _M0L5indexS1082, _M0L13handle__startS1078, _M0L14handle__resultS1083, _M0L17error__to__stringS1090);
        if (_tmp_2235.tag) {
          int32_t const _M0L5_2aokS2189 = _tmp_2235.data.ok;
          _handle__error__result_2236 = _M0L5_2aokS2189;
        } else {
          void* const _M0L6_2aerrS2190 = _tmp_2235.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1090);
          moonbit_decref_cycle_free(_M0L13handle__startS1078);
          _M0L11_2atry__errS1105 = _M0L6_2aerrS2190;
          goto join_1104;
        }
        if (_handle__error__result_2236) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1090);
          moonbit_decref_cycle_free(_M0L13handle__startS1078);
          _M0L6_2atmpS2186 = 1;
        } else {
          struct moonbit_result_0 _tmp_2237;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2237
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1111, _M0L8filenameS1080, _M0L5indexS1082, _M0L13handle__startS1078, _M0L14handle__resultS1083, _M0L17error__to__stringS1090);
          moonbit_decref_cycle_free(_M0L13handle__startS1078);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1090);
          if (_tmp_2237.tag) {
            int32_t const _M0L5_2aokS2187 = _tmp_2237.data.ok;
            _M0L6_2atmpS2186 = _M0L5_2aokS2187;
          } else {
            void* const _M0L6_2aerrS2188 = _tmp_2237.data.err;
            _M0L11_2atry__errS1105 = _M0L6_2aerrS2188;
            goto join_1104;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2186) {
    void* _M0L128RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2197 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L128RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2197)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L128RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2197)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1105
    = _M0L128RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2197;
    goto join_1104;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1083);
  }
  goto joinlet_2228;
  join_1104:;
  _M0L3errS1106 = _M0L11_2atry__errS1105;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1109
  = (struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1106;
  _M0L7_2anameS1110 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1109->$0;
  _M0L6_2acntS2220
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1109));
  if (_M0L6_2acntS2220 > 1) {
    int32_t _M0L11_2anew__cntS2221 = _M0L6_2acntS2220 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1109), _M0L11_2anew__cntS2221);
    moonbit_incref_cycle_free(_M0L7_2anameS1110);
  } else if (_M0L6_2acntS2220 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1109);
  }
  _M0L4nameS1108 = _M0L7_2anameS1110;
  goto join_1107;
  goto joinlet_2238;
  join_1107:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1083(_M0L14handle__resultS1083, _M0L4nameS1108, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1083);
  moonbit_decref_cycle_free(_M0L4nameS1108);
  joinlet_2238:;
  joinlet_2228:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1090(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2185,
  void* _M0L3errS1091
) {
  void* _M0L1eS1093;
  moonbit_string_t _M0L1eS1095;
  moonbit_string_t _result_2241;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1091)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1096 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1091;
      moonbit_string_t _M0L4_2aeS1097 = _M0L10_2aFailureS1096->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1097);
      _M0L1eS1095 = _M0L4_2aeS1097;
      goto join_1094;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1098 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1091;
      moonbit_string_t _M0L4_2aeS1099 = _M0L15_2aInspectErrorS1098->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1099);
      _M0L1eS1095 = _M0L4_2aeS1099;
      goto join_1094;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1100 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1091;
      moonbit_string_t _M0L4_2aeS1101 = _M0L16_2aSnapshotErrorS1100->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1101);
      _M0L1eS1095 = _M0L4_2aeS1101;
      goto join_1094;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1102 =
        (struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1091;
      moonbit_string_t _M0L4_2aeS1103 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1102->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1103);
      _M0L1eS1095 = _M0L4_2aeS1103;
      goto join_1094;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1091);
      _M0L1eS1093 = _M0L3errS1091;
      goto join_1092;
      break;
    }
  }
  join_1094:;
  return _M0L1eS1095;
  join_1092:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _result_2241 = _M0FP15Error10to__string(_M0L1eS1093);
  moonbit_decref_cycle_free(_M0L1eS1093);
  return _result_2241;
}

int32_t _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1083(
  struct _M0TWssbEu* _M0L6_2aenvS2182,
  moonbit_string_t _M0L10__testnameS1084,
  moonbit_string_t _M0L7messageS1085,
  int32_t _M0L7skippedS1086
) {
  struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1083* _M0L14_2acasted__envS2183;
  moonbit_string_t _M0L8filenameS1080;
  int32_t _M0L5indexS1082;
  moonbit_string_t _M0L10file__nameS1087;
  moonbit_string_t _M0L7messageS1088;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1089;
  moonbit_string_t _M0L6_2atmpS2184;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2183
  = (struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1083*)_M0L6_2aenvS2182;
  _M0L8filenameS1080 = _M0L14_2acasted__envS2183->$1;
  _M0L5indexS1082 = _M0L14_2acasted__envS2183->$0;
  if (!_M0L7skippedS1086 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1087
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1080, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1088
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1085, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1089
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1089, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1089, _M0L10file__nameS1087);
  moonbit_decref_cycle_free(_M0L10file__nameS1087);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1089, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1089, _M0L5indexS1082);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1089, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1089, _M0L7messageS1088);
  moonbit_decref_cycle_free(_M0L7messageS1088);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1089, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2184
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1089);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1089);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2184);
  moonbit_decref_cycle_free(_M0L6_2atmpS2184);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1078(
  struct _M0TWEu* _M0L6_2aenvS2179
) {
  struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1078* _M0L14_2acasted__envS2180;
  moonbit_string_t _M0L8filenameS1080;
  int32_t _M0L5indexS1082;
  moonbit_string_t _M0L10file__nameS1079;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1081;
  moonbit_string_t _M0L6_2atmpS2181;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2180
  = (struct _M0R129_24RiantR_2fsnn__mbt_2fexamples_2foja__rule__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1078*)_M0L6_2aenvS2179;
  _M0L8filenameS1080 = _M0L14_2acasted__envS2180->$1;
  _M0L5indexS1082 = _M0L14_2acasted__envS2180->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1079
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1080, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1081
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1081, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1081, _M0L10file__nameS1079);
  moonbit_decref_cycle_free(_M0L10file__nameS1079);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1081, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1081, _M0L5indexS1082);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1081, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2181
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1081);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1081);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2181);
  moonbit_decref_cycle_free(_M0L6_2atmpS2181);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1048;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1055;
  struct _M0TUsiE** _M0L6_2atmpS2178;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1062;
  moonbit_string_t* _M0L9cli__argsS1063;
  moonbit_string_t _M0L6_2atmpS2177;
  moonbit_string_t _M0L6_2atmpS2176;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1064;
  int32_t _M0L7_2abindS1065;
  moonbit_string_t* _M0L7_2abindS1066;
  int32_t _M0L6_2acntS2222;
  int32_t _M0L2__S1067;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1048 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1055 = 0;
  _M0L6_2atmpS2178 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1062
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1062)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1062->$0 = _M0L6_2atmpS2178;
  _M0L16file__and__indexS1062->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1063
  = _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1063)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2177 = (moonbit_string_t)_M0L9cli__argsS1063[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2177);
  moonbit_decref_cycle_free(_M0L9cli__argsS1063);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2176
  = _M0MP46RiantR8snn__mbt8examples25oja__rule__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2177);
  moonbit_decref_cycle_free(_M0L6_2atmpS2177);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1064
  = _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1055(_M0L51moonbit__test__driver__internal__split__mbt__stringS1055, _M0L6_2atmpS2176, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2176);
  _M0L7_2abindS1065 = _M0L10test__argsS1064->$1;
  _M0L7_2abindS1066 = _M0L10test__argsS1064->$0;
  _M0L6_2acntS2222
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1064));
  if (_M0L6_2acntS2222 > 1) {
    int32_t _M0L11_2anew__cntS2223 = _M0L6_2acntS2222 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1064), _M0L11_2anew__cntS2223);
    moonbit_incref_cycle_free(_M0L7_2abindS1066);
  } else if (_M0L6_2acntS2222 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1064);
  }
  _M0L2__S1067 = 0;
  while (1) {
    if (_M0L2__S1067 < _M0L7_2abindS1065) {
      moonbit_string_t _M0L3argS1068 =
        (moonbit_string_t)_M0L7_2abindS1066[_M0L2__S1067];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1069;
      moonbit_string_t _M0L4fileS1070;
      moonbit_string_t _M0L5rangeS1071;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1072;
      moonbit_string_t _M0L6_2atmpS2174;
      int32_t _M0L5startS1073;
      moonbit_string_t _M0L6_2atmpS2173;
      int32_t _M0L3endS1074;
      int32_t _M0L1iS1075;
      int32_t _M0L6_2atmpS2175;
      moonbit_incref_cycle_free(_M0L3argS1068);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1069
      = _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1055(_M0L51moonbit__test__driver__internal__split__mbt__stringS1055, _M0L3argS1068, 58);
      moonbit_decref_cycle_free(_M0L3argS1068);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1070
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1069, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1071
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1069, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1069);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1072
      = _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1055(_M0L51moonbit__test__driver__internal__split__mbt__stringS1055, _M0L5rangeS1071, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1071);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2174
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1072, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1073
      = _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1048(_M0L45moonbit__test__driver__internal__parse__int__S1048, _M0L6_2atmpS2174);
      moonbit_decref_cycle_free(_M0L6_2atmpS2174);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2173
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1072, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1072);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1074
      = _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1048(_M0L45moonbit__test__driver__internal__parse__int__S1048, _M0L6_2atmpS2173);
      moonbit_decref_cycle_free(_M0L6_2atmpS2173);
      _M0L1iS1075 = _M0L5startS1073;
      while (1) {
        if (_M0L1iS1075 < _M0L3endS1074) {
          struct _M0TUsiE* _M0L8_2atupleS2171;
          int32_t _M0L6_2atmpS2172;
          moonbit_incref_cycle_free(_M0L4fileS1070);
          _M0L8_2atupleS2171
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS2171)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS2171->$0 = _M0L4fileS1070;
          _M0L8_2atupleS2171->$1 = _M0L1iS1075;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1062, _M0L8_2atupleS2171);
          _M0L6_2atmpS2172 = _M0L1iS1075 + 1;
          _M0L1iS1075 = _M0L6_2atmpS2172;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1070);
        }
        break;
      }
      _M0L6_2atmpS2175 = _M0L2__S1067 + 1;
      _M0L2__S1067 = _M0L6_2atmpS2175;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1066);
    }
    break;
  }
  return _M0L16file__and__indexS1062;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1055(
  int32_t _M0L6_2aenvS2152,
  moonbit_string_t _M0L1sS1056,
  int32_t _M0L3sepS1057
) {
  moonbit_string_t* _M0L6_2atmpS2170;
  struct _M0TPB5ArrayGsE* _M0L3resS1058;
  struct _M0TPB8MutLocalGiE* _M0L1iS1059;
  struct _M0TPB8MutLocalGiE* _M0L5startS1060;
  int32_t _M0L3valS2165;
  int32_t _M0L6_2atmpS2166;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2170 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1058
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1058)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1058->$0 = _M0L6_2atmpS2170;
  _M0L3resS1058->$1 = 0;
  _M0L1iS1059
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1059)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1059->$0 = 0;
  _M0L5startS1060
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1060)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1060->$0 = 0;
  while (1) {
    int32_t _M0L3valS2153 = _M0L1iS1059->$0;
    int32_t _M0L6_2atmpS2154 = Moonbit_array_length(_M0L1sS1056);
    if (_M0L3valS2153 < _M0L6_2atmpS2154) {
      int32_t _M0L3valS2157 = _M0L1iS1059->$0;
      int32_t _M0L6_2atmpS2156;
      int32_t _M0L6_2atmpS2155;
      int32_t _M0L3valS2164;
      int32_t _M0L6_2atmpS2163;
      if (
        _M0L3valS2157 < 0
        || _M0L3valS2157 >= Moonbit_array_length(_M0L1sS1056)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2156 = _M0L1sS1056[_M0L3valS2157];
      _M0L6_2atmpS2155 = _M0L6_2atmpS2156;
      if (_M0L6_2atmpS2155 == _M0L3sepS1057) {
        int32_t _M0L3valS2159 = _M0L5startS1060->$0;
        int32_t _M0L3valS2160 = _M0L1iS1059->$0;
        moonbit_string_t _M0L6_2atmpS2158;
        int32_t _M0L3valS2162;
        int32_t _M0L6_2atmpS2161;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS2158
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1056, _M0L3valS2159, _M0L3valS2160);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1058, _M0L6_2atmpS2158);
        _M0L3valS2162 = _M0L1iS1059->$0;
        _M0L6_2atmpS2161 = _M0L3valS2162 + 1;
        _M0L5startS1060->$0 = _M0L6_2atmpS2161;
      }
      _M0L3valS2164 = _M0L1iS1059->$0;
      _M0L6_2atmpS2163 = _M0L3valS2164 + 1;
      _M0L1iS1059->$0 = _M0L6_2atmpS2163;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1059);
    }
    break;
  }
  _M0L3valS2165 = _M0L5startS1060->$0;
  _M0L6_2atmpS2166 = Moonbit_array_length(_M0L1sS1056);
  if (_M0L3valS2165 < _M0L6_2atmpS2166) {
    int32_t _M0L3valS2168 = _M0L5startS1060->$0;
    int32_t _M0L6_2atmpS2169;
    moonbit_string_t _M0L6_2atmpS2167;
    moonbit_decref_cycle_free(_M0L5startS1060);
    _M0L6_2atmpS2169 = Moonbit_array_length(_M0L1sS1056);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS2167
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1056, _M0L3valS2168, _M0L6_2atmpS2169);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1058, _M0L6_2atmpS2167);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1060);
  }
  return _M0L3resS1058;
}

int32_t _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1048(
  int32_t _M0L6_2aenvS2145,
  moonbit_string_t _M0L1sS1049
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1050;
  int32_t _M0L3lenS1051;
  int32_t _M0L7_2abindS1052;
  int32_t _M0L1iS1053;
  int32_t _result_2246;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1050
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1050)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1050->$0 = 0;
  _M0L3lenS1051 = Moonbit_array_length(_M0L1sS1049);
  _M0L7_2abindS1052 = 0;
  _M0L1iS1053 = _M0L7_2abindS1052;
  while (1) {
    if (_M0L1iS1053 < _M0L3lenS1051) {
      int32_t _M0L3valS2150 = _M0L3resS1050->$0;
      int32_t _M0L6_2atmpS2147 = _M0L3valS2150 * 10;
      int32_t _M0L6_2atmpS2149;
      int32_t _M0L6_2atmpS2148;
      int32_t _M0L6_2atmpS2146;
      int32_t _M0L6_2atmpS2151;
      if (
        _M0L1iS1053 < 0 || _M0L1iS1053 >= Moonbit_array_length(_M0L1sS1049)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2149 = _M0L1sS1049[_M0L1iS1053];
      _M0L6_2atmpS2148 = _M0L6_2atmpS2149 - 48;
      _M0L6_2atmpS2146 = _M0L6_2atmpS2147 + _M0L6_2atmpS2148;
      _M0L3resS1050->$0 = _M0L6_2atmpS2146;
      _M0L6_2atmpS2151 = _M0L1iS1053 + 1;
      _M0L1iS1053 = _M0L6_2atmpS2151;
      continue;
    }
    break;
  }
  _result_2246 = _M0L3resS1050->$0;
  moonbit_decref_cycle_free(_M0L3resS1050);
  return _result_2246;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples25oja__rule__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1047
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1047);
  return _M0L4selfS1047;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1017,
  moonbit_string_t _M0L12_2adiscard__S1018,
  int32_t _M0L12_2adiscard__S1019,
  struct _M0TWEu* _M0L12_2adiscard__S1020,
  struct _M0TWssbEu* _M0L12_2adiscard__S1021,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1022
) {
  struct moonbit_result_0 _result_2247;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _result_2247.tag = 1;
  _result_2247.data.ok = 0;
  return _result_2247;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1023,
  moonbit_string_t _M0L12_2adiscard__S1024,
  int32_t _M0L12_2adiscard__S1025,
  struct _M0TWEu* _M0L12_2adiscard__S1026,
  struct _M0TWssbEu* _M0L12_2adiscard__S1027,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1028
) {
  struct moonbit_result_0 _result_2248;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _result_2248.tag = 1;
  _result_2248.data.ok = 0;
  return _result_2248;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1029,
  moonbit_string_t _M0L12_2adiscard__S1030,
  int32_t _M0L12_2adiscard__S1031,
  struct _M0TWEu* _M0L12_2adiscard__S1032,
  struct _M0TWssbEu* _M0L12_2adiscard__S1033,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1034
) {
  struct moonbit_result_0 _result_2249;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _result_2249.tag = 1;
  _result_2249.data.ok = 0;
  return _result_2249;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1035,
  moonbit_string_t _M0L12_2adiscard__S1036,
  int32_t _M0L12_2adiscard__S1037,
  struct _M0TWEu* _M0L12_2adiscard__S1038,
  struct _M0TWssbEu* _M0L12_2adiscard__S1039,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1040
) {
  struct moonbit_result_0 _result_2250;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _result_2250.tag = 1;
  _result_2250.data.ok = 0;
  return _result_2250;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1041,
  moonbit_string_t _M0L12_2adiscard__S1042,
  int32_t _M0L12_2adiscard__S1043,
  struct _M0TWEu* _M0L12_2adiscard__S1044,
  struct _M0TWssbEu* _M0L12_2adiscard__S1045,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1046
) {
  struct moonbit_result_0 _result_2251;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _result_2251.tag = 1;
  _result_2251.data.ok = 0;
  return _result_2251;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1016
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

struct _M0TP26RiantR8snn__mbt11RateSynapse* _M0MP26RiantR8snn__mbt11RateSynapse3new(
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L3preS989,
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L4postS997,
  float _M0L2muS993,
  float _M0L5sigmaS995,
  float _M0L1pS991,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS998
) {
  int32_t _M0L1nS2144;
  float _M0L6n__preS988;
  float _M0L6_2atmpS2143;
  float _M0L5denomS990;
  float _M0L11weight__stdS992;
  float _M0L13weight__sigmaS994;
  int32_t _M0L1nS2141;
  int32_t _M0L1nS2142;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS996;
  struct _M0TP26RiantR8snn__mbt11RateSynapse* _block_2252;
  #line 36 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_rate.mbt"
  _M0L1nS2144 = _M0L3preS989->$1;
  _M0L6n__preS988 = (float)_M0L1nS2144;
  _M0L6_2atmpS2143 = _M0L6n__preS988 * _M0L1pS991;
  #line 46 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_rate.mbt"
  _M0L5denomS990 = sqrtf(_M0L6_2atmpS2143);
  if (_M0L5denomS990 > 0x0p+0f) {
    _M0L11weight__stdS992 = _M0L2muS993 / _M0L5denomS990;
  } else {
    _M0L11weight__stdS992 = _M0L2muS993;
  }
  if (_M0L5denomS990 > 0x0p+0f) {
    _M0L13weight__sigmaS994 = _M0L5sigmaS995 / _M0L5denomS990;
  } else {
    _M0L13weight__sigmaS994 = _M0L5sigmaS995;
  }
  _M0L1nS2141 = _M0L3preS989->$1;
  _M0L1nS2142 = _M0L4postS997->$1;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_rate.mbt"
  _M0L6matrixS996
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS2141, _M0L1nS2142, 0x0p+0f, _M0L11weight__stdS992, _M0L1pS991, _M0L3rngS998);
  moonbit_incref_cycle_free(_M0L3preS989);
  moonbit_incref_cycle_free(_M0L4postS997);
  _block_2252
  = (struct _M0TP26RiantR8snn__mbt11RateSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11RateSynapse));
  Moonbit_object_header(_block_2252)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2252->$0 = _M0L3preS989;
  _block_2252->$1 = _M0L4postS997;
  _block_2252->$2 = _M0L6matrixS996;
  return _block_2252;
}

struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0MP26RiantR8snn__mbt11WilsonCowan3new(
  int32_t _M0L1nS976,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS983
) {
  struct _M0TPB5ArrayGfE* _M0L1xS975;
  struct _M0TPB5ArrayGfE* _M0L1rS977;
  int32_t _M0L7_2abindS978;
  int32_t _M0L1kS979;
  struct _M0TPB5ArrayGfE* _M0L1gS986;
  struct _M0TPB5ArrayGfE* _M0L1iS987;
  struct _M0TP26RiantR8snn__mbt11WCParameter* _M0L6_2atmpS2140;
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _block_2255;
  #line 44 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  #line 45 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _M0L1xS975 = _M0MPC15array5Array4makeGfE(_M0L1nS976, 0x0p+0f);
  #line 46 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _M0L1rS977 = _M0MPC15array5Array4makeGfE(_M0L1nS976, 0x0p+0f);
  _M0L7_2abindS978 = 0;
  _M0L1kS979 = _M0L7_2abindS978;
  while (1) {
    if (_M0L1kS979 < _M0L1nS976) {
      double _M0L2z1S981;
      struct _M0TUddE* _M0L7_2abindS982;
      double _M0L5_2az1S984;
      float _M0L6_2atmpS2136;
      float _M0L6_2atmpS2135;
      float _M0L6_2atmpS2138;
      float _M0L6_2atmpS2137;
      int32_t _M0L6_2atmpS2139;
      #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L7_2abindS982 = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS983);
      _M0L5_2az1S984 = _M0L7_2abindS982->$0;
      moonbit_decref_cycle_free(_M0L7_2abindS982);
      _M0L2z1S981 = _M0L5_2az1S984;
      goto join_980;
      goto joinlet_2254;
      join_980:;
      _M0L6_2atmpS2136 = (float)_M0L2z1S981;
      _M0L6_2atmpS2135 = 0x1p-1f * _M0L6_2atmpS2136;
      #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS975, _M0L1kS979, _M0L6_2atmpS2135);
      #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS2138 = _M0MPC15array5Array2atGfE(_M0L1xS975, _M0L1kS979);
      #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS2137 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS2138);
      #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1rS977, _M0L1kS979, _M0L6_2atmpS2137);
      joinlet_2254:;
      _M0L6_2atmpS2139 = _M0L1kS979 + 1;
      _M0L1kS979 = _M0L6_2atmpS2139;
      continue;
    }
    break;
  }
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _M0L1gS986 = _M0MPC15array5Array4makeGfE(_M0L1nS976, 0x0p+0f);
  #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _M0L1iS987 = _M0MPC15array5Array4makeGfE(_M0L1nS976, 0x0p+0f);
  #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _M0L6_2atmpS2140 = _M0MP26RiantR8snn__mbt11WCParameter3new();
  _block_2255
  = (struct _M0TP26RiantR8snn__mbt11WilsonCowan*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11WilsonCowan));
  Moonbit_object_header(_block_2255)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 23, 0);
  _block_2255->$0 = _M0L6_2atmpS2140;
  _block_2255->$1 = _M0L1nS976;
  _block_2255->$2 = _M0L1xS975;
  _block_2255->$3 = _M0L1rS977;
  _block_2255->$4 = _M0L1gS986;
  _block_2255->$5 = _M0L1iS987;
  return _block_2255;
}

struct _M0TP26RiantR8snn__mbt11WCParameter* _M0MP26RiantR8snn__mbt11WCParameter3new(
  
) {
  struct _M0TP26RiantR8snn__mbt11WCParameter* _block_2256;
  #line 19 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _block_2256
  = (struct _M0TP26RiantR8snn__mbt11WCParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11WCParameter));
  Moonbit_object_header(_block_2256)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2256->$0 = 0x0p+0f;
  return _block_2256;
}

int32_t _M0FP26RiantR8snn__mbt8step__wc(
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L1pS970,
  float _M0L2dtS973
) {
  int32_t _M0L1nS969;
  int32_t _M0L7_2abindS971;
  int32_t _M0L1kS972;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _M0L1nS969 = _M0L1pS970->$1;
  _M0L7_2abindS971 = 0;
  _M0L1kS972 = _M0L7_2abindS971;
  while (1) {
    if (_M0L1kS972 < _M0L1nS969) {
      struct _M0TPB5ArrayGfE* _M0L1xS2115 = _M0L1pS970->$2;
      struct _M0TPB5ArrayGfE* _M0L1xS2128 = _M0L1pS970->$2;
      float _M0L6_2atmpS2117;
      struct _M0TPB5ArrayGfE* _M0L1xS2127;
      float _M0L6_2atmpS2126;
      float _M0L6_2atmpS2123;
      struct _M0TPB5ArrayGfE* _M0L1gS2125;
      float _M0L6_2atmpS2124;
      float _M0L6_2atmpS2120;
      struct _M0TPB5ArrayGfE* _M0L1iS2122;
      float _M0L6_2atmpS2121;
      float _M0L6_2atmpS2119;
      float _M0L6_2atmpS2118;
      float _M0L6_2atmpS2116;
      struct _M0TPB5ArrayGfE* _M0L1rS2129;
      struct _M0TPB5ArrayGfE* _M0L1xS2132;
      float _M0L6_2atmpS2131;
      float _M0L6_2atmpS2130;
      struct _M0TPB5ArrayGfE* _M0L1gS2133;
      int32_t _M0L6_2atmpS2134;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS2117 = _M0MPC15array5Array2atGfE(_M0L1xS2128, _M0L1kS972);
      _M0L1xS2127 = _M0L1pS970->$2;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS2126 = _M0MPC15array5Array2atGfE(_M0L1xS2127, _M0L1kS972);
      _M0L6_2atmpS2123 = -_M0L6_2atmpS2126;
      _M0L1gS2125 = _M0L1pS970->$4;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS2124 = _M0MPC15array5Array2atGfE(_M0L1gS2125, _M0L1kS972);
      _M0L6_2atmpS2120 = _M0L6_2atmpS2123 + _M0L6_2atmpS2124;
      _M0L1iS2122 = _M0L1pS970->$5;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS2121 = _M0MPC15array5Array2atGfE(_M0L1iS2122, _M0L1kS972);
      _M0L6_2atmpS2119 = _M0L6_2atmpS2120 + _M0L6_2atmpS2121;
      _M0L6_2atmpS2118 = _M0L2dtS973 * _M0L6_2atmpS2119;
      _M0L6_2atmpS2116 = _M0L6_2atmpS2117 + _M0L6_2atmpS2118;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS2115, _M0L1kS972, _M0L6_2atmpS2116);
      _M0L1rS2129 = _M0L1pS970->$3;
      _M0L1xS2132 = _M0L1pS970->$2;
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS2131 = _M0MPC15array5Array2atGfE(_M0L1xS2132, _M0L1kS972);
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS2130 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS2131);
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1rS2129, _M0L1kS972, _M0L6_2atmpS2130);
      _M0L1gS2133 = _M0L1pS970->$4;
      #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS2133, _M0L1kS972, 0x0p+0f);
      _M0L6_2atmpS2134 = _M0L1kS972 + 1;
      _M0L1kS972 = _M0L6_2atmpS2134;
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

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR13forward__rate(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS955,
  struct _M0TPB5ArrayGfE* _M0L9pre__rateS961,
  struct _M0TPB5ArrayGfE* _M0L7post__gS967
) {
  int32_t _M0L4rowsS954;
  int32_t _M0L7_2abindS956;
  int32_t _M0L1iS957;
  #line 311 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS954 = _M0L1mS955->$0;
  _M0L7_2abindS956 = 0;
  _M0L1iS957 = _M0L7_2abindS956;
  while (1) {
    if (_M0L1iS957 < _M0L4rowsS954) {
      float _M0L4r__iS960;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS2114;
      int32_t _M0L5startS962;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS2112;
      int32_t _M0L6_2atmpS2113;
      int32_t _M0L3endS963;
      int32_t _M0L1kS964;
      int32_t _M0L6_2atmpS2105;
      #line 318 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L4r__iS960
      = _M0MPC15array5Array2atGfE(_M0L9pre__rateS961, _M0L1iS957);
      if (_M0L4r__iS960 == 0x0p+0f) {
        goto join_958;
      }
      _M0L6rowptrS2114 = _M0L1mS955->$2;
      #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L5startS962
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS2114, _M0L1iS957);
      _M0L6rowptrS2112 = _M0L1mS955->$2;
      _M0L6_2atmpS2113 = _M0L1iS957 + 1;
      #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3endS963
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS2112, _M0L6_2atmpS2113);
      _M0L1kS964 = _M0L5startS962;
      while (1) {
        if (_M0L1kS964 < _M0L3endS963) {
          struct _M0TPB5ArrayGiE* _M0L6colptrS2110 = _M0L1mS955->$3;
          int32_t _M0L9post__idxS965;
          struct _M0TPB5ArrayGfE* _M0L4valsS2109;
          float _M0L1wS966;
          float _M0L6_2atmpS2107;
          float _M0L6_2atmpS2108;
          float _M0L6_2atmpS2106;
          int32_t _M0L6_2atmpS2111;
          #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L9post__idxS965
          = _M0MPC15array5Array2atGiE(_M0L6colptrS2110, _M0L1kS964);
          _M0L4valsS2109 = _M0L1mS955->$4;
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1wS966 = _M0MPC15array5Array2atGfE(_M0L4valsS2109, _M0L1kS964);
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS2107
          = _M0MPC15array5Array2atGfE(_M0L7post__gS967, _M0L9post__idxS965);
          _M0L6_2atmpS2108 = _M0L1wS966 * _M0L4r__iS960;
          _M0L6_2atmpS2106 = _M0L6_2atmpS2107 + _M0L6_2atmpS2108;
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L7post__gS967, _M0L9post__idxS965, _M0L6_2atmpS2106);
          _M0L6_2atmpS2111 = _M0L1kS964 + 1;
          _M0L1kS964 = _M0L6_2atmpS2111;
          continue;
        }
        break;
      }
      goto join_958;
      goto joinlet_2259;
      join_958:;
      _M0L6_2atmpS2105 = _M0L1iS957 + 1;
      _M0L1iS957 = _M0L6_2atmpS2105;
      continue;
      joinlet_2259:;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS948,
  int32_t _M0L4colsS949,
  float _M0L2muS950,
  float _M0L5sigmaS951,
  float _M0L1pS952,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS953
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS948, _M0L4colsS949, _M0L2muS950, _M0L5sigmaS951, _M0L1pS952, 0, _M0L3rngS953);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS862,
  int32_t _M0L4colsS866,
  float _M0L2muS872,
  float _M0L5sigmaS873,
  float _M0L1pS885,
  int32_t _M0L4ruleS879,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS875
) {
  float* _M0L6_2atmpS2104;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2103;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS861;
  int32_t _M0L7_2abindS863;
  int32_t _M0L1iS864;
  int32_t _M0L6_2atmpS2102;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS938;
  int32_t* _M0L6_2atmpS2101;
  struct _M0TPB5ArrayGiE* _M0L6colptrS939;
  float* _M0L6_2atmpS2100;
  struct _M0TPB5ArrayGfE* _M0L4valsS940;
  int32_t _M0L7_2abindS941;
  int32_t _M0L1iS942;
  int32_t _M0L6_2atmpS2099;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_2280;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2104 = moonbit_empty_float_array;
  _M0L6_2atmpS2103
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2103)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 30, 0);
  _M0L6_2atmpS2103->$0 = _M0L6_2atmpS2104;
  _M0L6_2atmpS2103->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS861
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS862, _M0L6_2atmpS2103);
  _M0L7_2abindS863 = 0;
  _M0L1iS864 = _M0L7_2abindS863;
  while (1) {
    if (_M0L1iS864 < _M0L4rowsS862) {
      struct _M0TPB5ArrayGfE* _M0L3rowS865;
      int32_t _M0L7_2abindS867;
      int32_t _M0L1jS868;
      int32_t _M0L6_2atmpS2055;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS865 = _M0MPC15array5Array4makeGfE(_M0L4colsS866, 0x0p+0f);
      _M0L7_2abindS867 = 0;
      _M0L1jS868 = _M0L7_2abindS867;
      while (1) {
        if (_M0L1jS868 < _M0L4colsS866) {
          double _M0L2z1S870;
          struct _M0TUddE* _M0L7_2abindS874;
          double _M0L5_2az1S876;
          float _M0L6_2atmpS2053;
          float _M0L6_2atmpS2052;
          float _M0L1wS871;
          int32_t _M0L6_2atmpS2054;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS874
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS875);
          _M0L5_2az1S876 = _M0L7_2abindS874->$0;
          moonbit_decref_cycle_free(_M0L7_2abindS874);
          _M0L2z1S870 = _M0L5_2az1S876;
          goto join_869;
          goto joinlet_2263;
          join_869:;
          _M0L6_2atmpS2053 = (float)_M0L2z1S870;
          _M0L6_2atmpS2052 = _M0L5sigmaS873 * _M0L6_2atmpS2053;
          _M0L1wS871 = _M0L2muS872 + _M0L6_2atmpS2052;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS865, _M0L1jS868, _M0L1wS871);
          joinlet_2263:;
          _M0L6_2atmpS2054 = _M0L1jS868 + 1;
          _M0L1jS868 = _M0L6_2atmpS2054;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS861, _M0L1iS864, _M0L3rowS865);
      _M0L6_2atmpS2055 = _M0L1iS864 + 1;
      _M0L1iS864 = _M0L6_2atmpS2055;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS879) {
    case 0: {
      int32_t _M0L7_2abindS880 = 0;
      int32_t _M0L1iS881 = _M0L7_2abindS880;
      while (1) {
        if (_M0L1iS881 < _M0L4rowsS862) {
          int32_t _M0L7_2abindS882 = 0;
          int32_t _M0L1jS883 = _M0L7_2abindS882;
          int32_t _M0L6_2atmpS2058;
          while (1) {
            if (_M0L1jS883 < _M0L4colsS866) {
              float _M0L1uS884;
              int32_t _M0L6_2atmpS2057;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS884 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS875);
              if (_M0L1uS884 >= _M0L1pS885) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2056;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2056
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS861, _M0L1iS881);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2056, _M0L1jS883, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2056);
              }
              _M0L6_2atmpS2057 = _M0L1jS883 + 1;
              _M0L1jS883 = _M0L6_2atmpS2057;
              continue;
            }
            break;
          }
          _M0L6_2atmpS2058 = _M0L1iS881 + 1;
          _M0L1iS881 = _M0L6_2atmpS2058;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS2076 = (float)_M0L4rowsS862;
      float _M0L6_2atmpS2075 = _M0L6_2atmpS2076 * _M0L1pS885;
      int32_t _M0L7n__keepS888;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS888 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2075);
      if (_M0L7n__keepS888 > 0 && _M0L7n__keepS888 <= _M0L4rowsS862) {
        int32_t _M0L7_2abindS889 = 0;
        int32_t _M0L1jS890 = _M0L7_2abindS889;
        while (1) {
          if (_M0L1jS890 < _M0L4colsS866) {
            int32_t* _M0L6_2atmpS2070 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS891 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS892;
            int32_t _M0L1kS893;
            int32_t _M0L7n__dropS895;
            int32_t _M0L7_2abindS896;
            int32_t _M0L1kS897;
            int32_t _M0L7_2abindS903;
            int32_t _M0L1kS904;
            int32_t _M0L6_2atmpS2071;
            Moonbit_object_header(_M0L8pre__idxS891)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 33, 0);
            _M0L8pre__idxS891->$0 = _M0L6_2atmpS2070;
            _M0L8pre__idxS891->$1 = 0;
            _M0L7_2abindS892 = 0;
            _M0L1kS893 = _M0L7_2abindS892;
            while (1) {
              if (_M0L1kS893 < _M0L4rowsS862) {
                int32_t _M0L6_2atmpS2059;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS891, _M0L1kS893);
                _M0L6_2atmpS2059 = _M0L1kS893 + 1;
                _M0L1kS893 = _M0L6_2atmpS2059;
                continue;
              }
              break;
            }
            _M0L7n__dropS895 = _M0L4rowsS862 - _M0L7n__keepS888;
            _M0L7_2abindS896 = 0;
            _M0L1kS897 = _M0L7_2abindS896;
            while (1) {
              if (_M0L1kS897 < _M0L7n__dropS895) {
                float _M0L1uS898;
                float _M0L6_2atmpS2063;
                float _M0L6_2atmpS2065;
                float _M0L6_2atmpS2064;
                float _M0L6_2atmpS2062;
                int32_t _M0L6_2atmpS2061;
                int32_t _M0L6r__idxS899;
                int32_t _M0L10r__clampedS900;
                int32_t _M0L3tmpS901;
                int32_t _M0L6_2atmpS2060;
                int32_t _M0L6_2atmpS2066;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS898 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS875);
                _M0L6_2atmpS2063 = (float)_M0L4rowsS862;
                _M0L6_2atmpS2065 = (float)_M0L1kS897;
                _M0L6_2atmpS2064 = _M0L6_2atmpS2065 * _M0L1uS898;
                _M0L6_2atmpS2062 = _M0L6_2atmpS2063 - _M0L6_2atmpS2064;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2061
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2062);
                _M0L6r__idxS899 = _M0L1kS897 + _M0L6_2atmpS2061;
                if (_M0L6r__idxS899 >= _M0L4rowsS862) {
                  _M0L10r__clampedS900 = _M0L4rowsS862 - 1;
                } else {
                  _M0L10r__clampedS900 = _M0L6r__idxS899;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS901
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS891, _M0L1kS897);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2060
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS891, _M0L10r__clampedS900);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS891, _M0L1kS897, _M0L6_2atmpS2060);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS891, _M0L10r__clampedS900, _M0L3tmpS901);
                _M0L6_2atmpS2066 = _M0L1kS897 + 1;
                _M0L1kS897 = _M0L6_2atmpS2066;
                continue;
              }
              break;
            }
            _M0L7_2abindS903 = 0;
            _M0L1kS904 = _M0L7_2abindS903;
            while (1) {
              if (_M0L1kS904 < _M0L7n__dropS895) {
                int32_t _M0L6_2atmpS2068;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2067;
                int32_t _M0L6_2atmpS2069;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2068
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS891, _M0L1kS904);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2067
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS861, _M0L6_2atmpS2068);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2067, _M0L1jS890, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2067);
                _M0L6_2atmpS2069 = _M0L1kS904 + 1;
                _M0L1kS904 = _M0L6_2atmpS2069;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L8pre__idxS891);
              }
              break;
            }
            _M0L6_2atmpS2071 = _M0L1jS890 + 1;
            _M0L1jS890 = _M0L6_2atmpS2071;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS888 == 0) {
        int32_t _M0L7_2abindS907 = 0;
        int32_t _M0L1iS908 = _M0L7_2abindS907;
        while (1) {
          if (_M0L1iS908 < _M0L4rowsS862) {
            int32_t _M0L7_2abindS909 = 0;
            int32_t _M0L1jS910 = _M0L7_2abindS909;
            int32_t _M0L6_2atmpS2074;
            while (1) {
              if (_M0L1jS910 < _M0L4colsS866) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2072;
                int32_t _M0L6_2atmpS2073;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2072
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS861, _M0L1iS908);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2072, _M0L1jS910, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2072);
                _M0L6_2atmpS2073 = _M0L1jS910 + 1;
                _M0L1jS910 = _M0L6_2atmpS2073;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2074 = _M0L1iS908 + 1;
            _M0L1iS908 = _M0L6_2atmpS2074;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS2094 = (float)_M0L4colsS866;
      float _M0L6_2atmpS2093 = _M0L6_2atmpS2094 * _M0L1pS885;
      int32_t _M0L7n__keepS913;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS913 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2093);
      if (_M0L7n__keepS913 > 0 && _M0L7n__keepS913 <= _M0L4colsS866) {
        int32_t _M0L7_2abindS914 = 0;
        int32_t _M0L1iS915 = _M0L7_2abindS914;
        while (1) {
          if (_M0L1iS915 < _M0L4rowsS862) {
            int32_t* _M0L6_2atmpS2088 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS916 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS917;
            int32_t _M0L1kS918;
            int32_t _M0L7n__dropS920;
            int32_t _M0L7_2abindS921;
            int32_t _M0L1kS922;
            int32_t _M0L7_2abindS928;
            int32_t _M0L1kS929;
            int32_t _M0L6_2atmpS2089;
            Moonbit_object_header(_M0L9post__idxS916)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 33, 0);
            _M0L9post__idxS916->$0 = _M0L6_2atmpS2088;
            _M0L9post__idxS916->$1 = 0;
            _M0L7_2abindS917 = 0;
            _M0L1kS918 = _M0L7_2abindS917;
            while (1) {
              if (_M0L1kS918 < _M0L4colsS866) {
                int32_t _M0L6_2atmpS2077;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS916, _M0L1kS918);
                _M0L6_2atmpS2077 = _M0L1kS918 + 1;
                _M0L1kS918 = _M0L6_2atmpS2077;
                continue;
              }
              break;
            }
            _M0L7n__dropS920 = _M0L4colsS866 - _M0L7n__keepS913;
            _M0L7_2abindS921 = 0;
            _M0L1kS922 = _M0L7_2abindS921;
            while (1) {
              if (_M0L1kS922 < _M0L7n__dropS920) {
                float _M0L1uS923;
                float _M0L6_2atmpS2081;
                float _M0L6_2atmpS2083;
                float _M0L6_2atmpS2082;
                float _M0L6_2atmpS2080;
                int32_t _M0L6_2atmpS2079;
                int32_t _M0L6r__idxS924;
                int32_t _M0L10r__clampedS925;
                int32_t _M0L3tmpS926;
                int32_t _M0L6_2atmpS2078;
                int32_t _M0L6_2atmpS2084;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS923 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS875);
                _M0L6_2atmpS2081 = (float)_M0L4colsS866;
                _M0L6_2atmpS2083 = (float)_M0L1kS922;
                _M0L6_2atmpS2082 = _M0L6_2atmpS2083 * _M0L1uS923;
                _M0L6_2atmpS2080 = _M0L6_2atmpS2081 - _M0L6_2atmpS2082;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2079
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2080);
                _M0L6r__idxS924 = _M0L1kS922 + _M0L6_2atmpS2079;
                if (_M0L6r__idxS924 >= _M0L4colsS866) {
                  _M0L10r__clampedS925 = _M0L4colsS866 - 1;
                } else {
                  _M0L10r__clampedS925 = _M0L6r__idxS924;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS926
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS916, _M0L1kS922);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2078
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS916, _M0L10r__clampedS925);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS916, _M0L1kS922, _M0L6_2atmpS2078);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS916, _M0L10r__clampedS925, _M0L3tmpS926);
                _M0L6_2atmpS2084 = _M0L1kS922 + 1;
                _M0L1kS922 = _M0L6_2atmpS2084;
                continue;
              }
              break;
            }
            _M0L7_2abindS928 = 0;
            _M0L1kS929 = _M0L7_2abindS928;
            while (1) {
              if (_M0L1kS929 < _M0L7n__dropS920) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2085;
                int32_t _M0L6_2atmpS2086;
                int32_t _M0L6_2atmpS2087;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2085
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS861, _M0L1iS915);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2086
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS916, _M0L1kS929);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2085, _M0L6_2atmpS2086, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2085);
                _M0L6_2atmpS2087 = _M0L1kS929 + 1;
                _M0L1kS929 = _M0L6_2atmpS2087;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L9post__idxS916);
              }
              break;
            }
            _M0L6_2atmpS2089 = _M0L1iS915 + 1;
            _M0L1iS915 = _M0L6_2atmpS2089;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS913 == 0) {
        int32_t _M0L7_2abindS932 = 0;
        int32_t _M0L1iS933 = _M0L7_2abindS932;
        while (1) {
          if (_M0L1iS933 < _M0L4rowsS862) {
            int32_t _M0L7_2abindS934 = 0;
            int32_t _M0L1jS935 = _M0L7_2abindS934;
            int32_t _M0L6_2atmpS2092;
            while (1) {
              if (_M0L1jS935 < _M0L4colsS866) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2090;
                int32_t _M0L6_2atmpS2091;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2090
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS861, _M0L1iS933);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2090, _M0L1jS935, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2090);
                _M0L6_2atmpS2091 = _M0L1jS935 + 1;
                _M0L1jS935 = _M0L6_2atmpS2091;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2092 = _M0L1iS933 + 1;
            _M0L1iS933 = _M0L6_2atmpS2092;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS2102 = _M0L4rowsS862 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS938 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS2102, 0);
  _M0L6_2atmpS2101 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS939
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS939)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 33, 0);
  _M0L6colptrS939->$0 = _M0L6_2atmpS2101;
  _M0L6colptrS939->$1 = 0;
  _M0L6_2atmpS2100 = moonbit_empty_float_array;
  _M0L4valsS940
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS940)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 30, 0);
  _M0L4valsS940->$0 = _M0L6_2atmpS2100;
  _M0L4valsS940->$1 = 0;
  _M0L7_2abindS941 = 0;
  _M0L1iS942 = _M0L7_2abindS941;
  while (1) {
    if (_M0L1iS942 < _M0L4rowsS862) {
      int32_t _M0L6_2atmpS2095;
      int32_t _M0L7_2abindS943;
      int32_t _M0L1jS944;
      int32_t _M0L6_2atmpS2098;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS2095 = _M0MPC15array5Array6lengthGfE(_M0L4valsS940);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS938, _M0L1iS942, _M0L6_2atmpS2095);
      _M0L7_2abindS943 = 0;
      _M0L1jS944 = _M0L7_2abindS943;
      while (1) {
        if (_M0L1jS944 < _M0L4colsS866) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS2096;
          float _M0L1vS945;
          int32_t _M0L6_2atmpS2097;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS2096
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS861, _M0L1iS942);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS945
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS2096, _M0L1jS944);
          moonbit_decref_cycle_free(_M0L6_2atmpS2096);
          if (_M0L1vS945 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS939, _M0L1jS944);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS940, _M0L1vS945);
          }
          _M0L6_2atmpS2097 = _M0L1jS944 + 1;
          _M0L1jS944 = _M0L6_2atmpS2097;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2098 = _M0L1iS942 + 1;
      _M0L1iS942 = _M0L6_2atmpS2098;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L5denseS861);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2099 = _M0MPC15array5Array6lengthGfE(_M0L4valsS940);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS938, _M0L4rowsS862, _M0L6_2atmpS2099);
  _block_2280
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_2280)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _block_2280->$0 = _M0L4rowsS862;
  _block_2280->$1 = _M0L4colsS866;
  _block_2280->$2 = _M0L6rowptrS938;
  _block_2280->$3 = _M0L6colptrS939;
  _block_2280->$4 = _M0L4valsS940;
  return _block_2280;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS860
) {
  struct _M0TPB5ArrayGfE* _M0L4valsS2051;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4valsS2051 = _M0L1mS860->$4;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MPC15array5Array6lengthGfE(_M0L4valsS2051);
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS858
) {
  struct _M0TUmmmmE* _M0L1sS857;
  uint64_t _M0L6_2atmpS2050;
  struct _M0TUmmmmE* _M0L1tS859;
  uint64_t _M0L6_2atmpS2046;
  uint64_t _M0L6_2atmpS2047;
  uint64_t _M0L6_2atmpS2048;
  uint64_t _M0L6_2atmpS2049;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2281;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS857 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS858);
  _M0L6_2atmpS2050 = _M0L1sS857->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS859 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2050);
  _M0L6_2atmpS2046 = _M0L1sS857->$0;
  _M0L6_2atmpS2047 = _M0L1sS857->$1;
  _M0L6_2atmpS2048 = _M0L1sS857->$2;
  moonbit_decref_cycle_free(_M0L1sS857);
  _M0L6_2atmpS2049 = _M0L1tS859->$0;
  moonbit_decref_cycle_free(_M0L1tS859);
  _block_2281
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2281)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2281->$0 = _M0L6_2atmpS2046;
  _block_2281->$1 = _M0L6_2atmpS2047;
  _block_2281->$2 = _M0L6_2atmpS2048;
  _block_2281->$3 = _M0L6_2atmpS2049;
  return _block_2281;
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
  struct _M0TUmmmmE* _block_2282;
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
  _block_2282 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2282)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2282->$0 = _M0L2z1S850;
  _block_2282->$1 = _M0L2z2S852;
  _block_2282->$2 = _M0L2z3S854;
  _block_2282->$3 = _M0L2z4S856;
  return _block_2282;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS846) {
  uint64_t _M0L6_2atmpS2045;
  uint64_t _M0L6_2atmpS2044;
  uint64_t _M0L1zS845;
  uint64_t _M0L6_2atmpS2043;
  uint64_t _M0L6_2atmpS2042;
  uint64_t _M0L1zS847;
  uint64_t _M0L6_2atmpS2041;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2045 = _M0L1zS846 >> 30;
  _M0L6_2atmpS2044 = _M0L1zS846 ^ _M0L6_2atmpS2045;
  _M0L1zS845 = _M0L6_2atmpS2044 * 13787848793156543929ull;
  _M0L6_2atmpS2043 = _M0L1zS845 >> 27;
  _M0L6_2atmpS2042 = _M0L1zS845 ^ _M0L6_2atmpS2043;
  _M0L1zS847 = _M0L6_2atmpS2042 * 10723151780598845931ull;
  _M0L6_2atmpS2041 = _M0L1zS847 >> 31;
  return _M0L1zS847 ^ _M0L6_2atmpS2041;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS840
) {
  double _M0L2u1S839;
  double _M0L8u1__safeS841;
  double _M0L2u2S842;
  double _M0L6_2atmpS2040;
  double _M0L6_2atmpS2039;
  double _M0L1rS843;
  double _M0L5thetaS844;
  double _M0L6_2atmpS2038;
  double _M0L6_2atmpS2035;
  double _M0L6_2atmpS2037;
  double _M0L6_2atmpS2036;
  struct _M0TUddE* _block_2283;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S839 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS840);
  if (_M0L2u1S839 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS841 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS841 = _M0L2u1S839;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S842 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS840);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2040 = _M0FPC14math2ln(_M0L8u1__safeS841);
  _M0L6_2atmpS2039 = -0x1p+1 * _M0L6_2atmpS2040;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS843 = sqrt(_M0L6_2atmpS2039);
  _M0L5thetaS844 = 0x1.921fb54442d18p+2 * _M0L2u2S842;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2038 = _M0FPC14math3cos(_M0L5thetaS844);
  _M0L6_2atmpS2035 = _M0L1rS843 * _M0L6_2atmpS2038;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS2037 = _M0FPC14math3sin(_M0L5thetaS844);
  _M0L6_2atmpS2036 = _M0L1rS843 * _M0L6_2atmpS2037;
  _block_2283 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_2283)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2283->$0 = _M0L6_2atmpS2035;
  _block_2283->$1 = _M0L6_2atmpS2036;
  return _block_2283;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS837
) {
  uint64_t _M0L1uS836;
  uint64_t _M0L4bitsS838;
  double _M0L6_2atmpS2034;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS836 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS837);
  _M0L4bitsS838 = _M0L1uS836 >> 11;
  _M0L6_2atmpS2034 = (double)_M0L4bitsS838;
  return _M0L6_2atmpS2034 * 0x1p-53;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS834
) {
  uint32_t _M0L1uS833;
  uint32_t _M0L4bitsS835;
  double _M0L6_2atmpS2033;
  double _M0L6_2atmpS2032;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS833 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS834);
  _M0L4bitsS835 = _M0L1uS833 >> 8;
  _M0L6_2atmpS2033 = (double)_M0L4bitsS835;
  _M0L6_2atmpS2032 = _M0L6_2atmpS2033 * 0x1p-24;
  return (float)_M0L6_2atmpS2032;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS832
) {
  uint64_t _M0L1uS831;
  uint64_t _M0L6_2atmpS2031;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS831 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS832);
  _M0L6_2atmpS2031 = _M0L1uS831 >> 32;
  return (uint32_t)_M0L6_2atmpS2031;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS824
) {
  uint64_t _M0L2s0S823;
  uint64_t _M0L2s1S825;
  uint64_t _M0L2s2S826;
  uint64_t _M0L2s3S827;
  uint64_t _M0L3tmpS828;
  uint64_t _M0L6_2atmpS2030;
  uint64_t _M0L3resS829;
  uint64_t _M0L1tS830;
  uint64_t _M0L6_2atmpS2020;
  uint64_t _M0L6_2atmpS2021;
  uint64_t _M0L2s2S2023;
  uint64_t _M0L6_2atmpS2022;
  uint64_t _M0L2s3S2025;
  uint64_t _M0L6_2atmpS2024;
  uint64_t _M0L2s2S2027;
  uint64_t _M0L6_2atmpS2026;
  uint64_t _M0L2s3S2029;
  uint64_t _M0L6_2atmpS2028;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S823 = _M0L1rS824->$0;
  _M0L2s1S825 = _M0L1rS824->$1;
  _M0L2s2S826 = _M0L1rS824->$2;
  _M0L2s3S827 = _M0L1rS824->$3;
  _M0L3tmpS828 = _M0L2s0S823 + _M0L2s3S827;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2030 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS828, 23);
  _M0L3resS829 = _M0L6_2atmpS2030 + _M0L2s0S823;
  _M0L1tS830 = _M0L2s1S825 << 17;
  _M0L6_2atmpS2020 = _M0L2s2S826 ^ _M0L2s0S823;
  _M0L1rS824->$2 = _M0L6_2atmpS2020;
  _M0L6_2atmpS2021 = _M0L2s3S827 ^ _M0L2s1S825;
  _M0L1rS824->$3 = _M0L6_2atmpS2021;
  _M0L2s2S2023 = _M0L1rS824->$2;
  _M0L6_2atmpS2022 = _M0L2s1S825 ^ _M0L2s2S2023;
  _M0L1rS824->$1 = _M0L6_2atmpS2022;
  _M0L2s3S2025 = _M0L1rS824->$3;
  _M0L6_2atmpS2024 = _M0L2s0S823 ^ _M0L2s3S2025;
  _M0L1rS824->$0 = _M0L6_2atmpS2024;
  _M0L2s2S2027 = _M0L1rS824->$2;
  _M0L6_2atmpS2026 = _M0L2s2S2027 ^ _M0L1tS830;
  _M0L1rS824->$2 = _M0L6_2atmpS2026;
  _M0L2s3S2029 = _M0L1rS824->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2028 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S2029, 45);
  _M0L1rS824->$3 = _M0L6_2atmpS2028;
  return _M0L3resS829;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS821, int32_t _M0L1kS822) {
  uint64_t _M0L6_2atmpS2017;
  int32_t _M0L6_2atmpS2019;
  uint64_t _M0L6_2atmpS2018;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2017 = _M0L1xS821 << (_M0L1kS822 & 63);
  _M0L6_2atmpS2019 = 64 - _M0L1kS822;
  _M0L6_2atmpS2018 = _M0L1xS821 >> (_M0L6_2atmpS2019 & 63);
  return _M0L6_2atmpS2017 | _M0L6_2atmpS2018;
}

double _M0FPC14math2ln(double _M0L1xS807) {
  struct _M0TUdiE* _M0L7_2abindS808;
  double _M0L5_2af1S809;
  int32_t _M0L5_2akiS810;
  double _M0L1fS812;
  double _M0L1kS813;
  double _M0L6_2atmpS2010;
  double _M0L1sS814;
  double _M0L2s2S815;
  double _M0L2s4S816;
  double _M0L6_2atmpS2009;
  double _M0L6_2atmpS2008;
  double _M0L6_2atmpS2007;
  double _M0L6_2atmpS2006;
  double _M0L6_2atmpS2005;
  double _M0L6_2atmpS2004;
  double _M0L2t1S817;
  double _M0L6_2atmpS2003;
  double _M0L6_2atmpS2002;
  double _M0L6_2atmpS2001;
  double _M0L6_2atmpS2000;
  double _M0L2t2S818;
  double _M0L1rS819;
  double _M0L6_2atmpS1999;
  double _M0L4hfsqS820;
  double _M0L6_2atmpS1992;
  double _M0L6_2atmpS1998;
  double _M0L6_2atmpS1996;
  double _M0L6_2atmpS1997;
  double _M0L6_2atmpS1995;
  double _M0L6_2atmpS1994;
  double _M0L6_2atmpS1993;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS807 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS807)
      || _M0MPC16double6Double7is__inf(_M0L1xS807)
    ) {
      return _M0L1xS807;
    } else if (_M0L1xS807 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS808 = _M0FPC14math5frexp(_M0L1xS807);
  _M0L5_2af1S809 = _M0L7_2abindS808->$0;
  _M0L5_2akiS810 = _M0L7_2abindS808->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS808);
  if (_M0L5_2af1S809 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS2014 = _M0L5_2af1S809 * 0x1p+1;
    double _M0L6_2atmpS2011 = _M0L6_2atmpS2014 - 0x1p+0;
    int32_t _M0L6_2atmpS2013 = _M0L5_2akiS810 - 1;
    double _M0L6_2atmpS2012 = (double)_M0L6_2atmpS2013;
    _M0L1fS812 = _M0L6_2atmpS2011;
    _M0L1kS813 = _M0L6_2atmpS2012;
    goto join_811;
  } else {
    double _M0L6_2atmpS2015 = _M0L5_2af1S809 - 0x1p+0;
    double _M0L6_2atmpS2016 = (double)_M0L5_2akiS810;
    _M0L1fS812 = _M0L6_2atmpS2015;
    _M0L1kS813 = _M0L6_2atmpS2016;
    goto join_811;
  }
  join_811:;
  _M0L6_2atmpS2010 = 0x1p+1 + _M0L1fS812;
  _M0L1sS814 = _M0L1fS812 / _M0L6_2atmpS2010;
  _M0L2s2S815 = _M0L1sS814 * _M0L1sS814;
  _M0L2s4S816 = _M0L2s2S815 * _M0L2s2S815;
  _M0L6_2atmpS2009 = _M0L2s4S816 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS2008 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS2009;
  _M0L6_2atmpS2007 = _M0L2s4S816 * _M0L6_2atmpS2008;
  _M0L6_2atmpS2006 = 0x1.2492494229359p-2 + _M0L6_2atmpS2007;
  _M0L6_2atmpS2005 = _M0L2s4S816 * _M0L6_2atmpS2006;
  _M0L6_2atmpS2004 = 0x1.5555555555593p-1 + _M0L6_2atmpS2005;
  _M0L2t1S817 = _M0L2s2S815 * _M0L6_2atmpS2004;
  _M0L6_2atmpS2003 = _M0L2s4S816 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS2002 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS2003;
  _M0L6_2atmpS2001 = _M0L2s4S816 * _M0L6_2atmpS2002;
  _M0L6_2atmpS2000 = 0x1.999999997fa04p-2 + _M0L6_2atmpS2001;
  _M0L2t2S818 = _M0L2s4S816 * _M0L6_2atmpS2000;
  _M0L1rS819 = _M0L2t1S817 + _M0L2t2S818;
  _M0L6_2atmpS1999 = 0x1p-1 * _M0L1fS812;
  _M0L4hfsqS820 = _M0L6_2atmpS1999 * _M0L1fS812;
  _M0L6_2atmpS1992 = _M0L1kS813 * 0x1.62e42feep-1;
  _M0L6_2atmpS1998 = _M0L4hfsqS820 + _M0L1rS819;
  _M0L6_2atmpS1996 = _M0L1sS814 * _M0L6_2atmpS1998;
  _M0L6_2atmpS1997 = _M0L1kS813 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS1995 = _M0L6_2atmpS1996 + _M0L6_2atmpS1997;
  _M0L6_2atmpS1994 = _M0L4hfsqS820 - _M0L6_2atmpS1995;
  _M0L6_2atmpS1993 = _M0L6_2atmpS1994 - _M0L1fS812;
  return _M0L6_2atmpS1992 - _M0L6_2atmpS1993;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS800) {
  struct _M0TUdiE* _M0L7_2abindS801;
  double _M0L10_2anorm__fS802;
  int32_t _M0L6_2aexpS803;
  uint64_t _M0L1uS804;
  uint64_t _M0L6_2atmpS1991;
  uint64_t _M0L6_2atmpS1990;
  int32_t _M0L6_2atmpS1989;
  int32_t _M0L6_2atmpS1988;
  int32_t _M0L3expS805;
  uint64_t _M0L6_2atmpS1987;
  uint64_t _M0L6_2atmpS1986;
  uint64_t _M0L6_2atmpS1985;
  double _M0L4fracS806;
  struct _M0TUdiE* _block_2286;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS800 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS800)
    || _M0MPC16double6Double7is__nan(_M0L1fS800)
  ) {
    struct _M0TUdiE* _block_2285 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2285)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2285->$0 = _M0L1fS800;
    _block_2285->$1 = 0;
    return _block_2285;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS801 = _M0FPC14math9normalize(_M0L1fS800);
  _M0L10_2anorm__fS802 = _M0L7_2abindS801->$0;
  _M0L6_2aexpS803 = _M0L7_2abindS801->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS801);
  _M0L1uS804 = *(int64_t*)&_M0L10_2anorm__fS802;
  _M0L6_2atmpS1991 = _M0L1uS804 >> 52;
  _M0L6_2atmpS1990 = _M0L6_2atmpS1991 & 2047ull;
  _M0L6_2atmpS1989 = (int32_t)_M0L6_2atmpS1990;
  _M0L6_2atmpS1988 = _M0L6_2aexpS803 + _M0L6_2atmpS1989;
  _M0L3expS805 = _M0L6_2atmpS1988 - 1022;
  _M0L6_2atmpS1987 = ~9218868437227405312ull;
  _M0L6_2atmpS1986 = _M0L1uS804 & _M0L6_2atmpS1987;
  _M0L6_2atmpS1985 = _M0L6_2atmpS1986 | 4602678819172646912ull;
  _M0L4fracS806 = *(double*)&_M0L6_2atmpS1985;
  _block_2286 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2286)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2286->$0 = _M0L4fracS806;
  _block_2286->$1 = _M0L3expS805;
  return _block_2286;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS799) {
  double _M0L6_2atmpS1982;
  struct _M0TUdiE* _block_2288;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS1982 = fabs(_M0L1fS799);
  if (_M0L6_2atmpS1982 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS1984 = (double)4503599627370496ll;
    double _M0L6_2atmpS1983 = _M0L1fS799 * _M0L6_2atmpS1984;
    struct _M0TUdiE* _block_2287 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2287)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2287->$0 = _M0L6_2atmpS1983;
    _block_2287->$1 = -52;
    return _block_2287;
  }
  _block_2288 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2288)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2288->$0 = _M0L1fS799;
  _block_2288->$1 = 0;
  return _block_2288;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS798) {
  double _M0L6_2atmpS1981;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1981 = (double)_M0L4selfS798;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1981);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS797) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS797 != _M0L4selfS797) {
    return 0;
  } else if (_M0L4selfS797 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS797 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS797;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS783,
  float _M0L4elemS785
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS782;
  int32_t _M0L1iS784;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS782 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS783);
  _M0L1iS784 = 0;
  while (1) {
    if (_M0L1iS784 < _M0L3lenS783) {
      float* _M0L3bufS1975 = _M0L3arrS782->$0;
      int32_t _M0L6_2atmpS1976;
      _M0L3bufS1975[_M0L1iS784] = _M0L4elemS785;
      _M0L6_2atmpS1976 = _M0L1iS784 + 1;
      _M0L1iS784 = _M0L6_2atmpS1976;
      continue;
    }
    break;
  }
  return _M0L3arrS782;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS788,
  struct _M0TPB5ArrayGfE* _M0L4elemS790
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS787;
  int32_t _M0L1iS789;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS787
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS788);
  _M0L1iS789 = 0;
  while (1) {
    if (_M0L1iS789 < _M0L3lenS788) {
      struct _M0TPB5ArrayGfE** _M0L3bufS1977 = _M0L3arrS787->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS2199 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS1977[_M0L1iS789];
      int32_t _M0L6_2atmpS1978;
      moonbit_incref_cycle_free(_M0L4elemS790);
      if (_M0L6_2aoldS2199) {
        moonbit_decref_cycle_free(_M0L6_2aoldS2199);
      }
      _M0L3bufS1977[_M0L1iS789] = _M0L4elemS790;
      _M0L6_2atmpS1978 = _M0L1iS789 + 1;
      _M0L1iS789 = _M0L6_2atmpS1978;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS790);
    }
    break;
  }
  return _M0L3arrS787;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS793,
  int32_t _M0L4elemS795
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS792;
  int32_t _M0L1iS794;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS792 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS793);
  _M0L1iS794 = 0;
  while (1) {
    if (_M0L1iS794 < _M0L3lenS793) {
      int32_t* _M0L3bufS1979 = _M0L3arrS792->$0;
      int32_t _M0L6_2atmpS1980;
      _M0L3bufS1979[_M0L1iS794] = _M0L4elemS795;
      _M0L6_2atmpS1980 = _M0L1iS794 + 1;
      _M0L1iS794 = _M0L6_2atmpS1980;
      continue;
    }
    break;
  }
  return _M0L3arrS792;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS771,
  int32_t _M0L5indexS772,
  float _M0L5valueS773
) {
  int32_t _M0L3lenS770;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS770 = _M0L4selfS771->$1;
  if (_M0L5indexS772 >= 0 && _M0L5indexS772 < _M0L3lenS770) {
    float* _M0L6_2atmpS1972;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1972 = _M0MPC15array5Array6bufferGfE(_M0L4selfS771);
    _M0L6_2atmpS1972[_M0L5indexS772] = _M0L5valueS773;
    moonbit_decref_cycle_free(_M0L6_2atmpS1972);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS775,
  int32_t _M0L5indexS776,
  struct _M0TPB5ArrayGfE* _M0L5valueS777
) {
  int32_t _M0L3lenS774;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS774 = _M0L4selfS775->$1;
  if (_M0L5indexS776 >= 0 && _M0L5indexS776 < _M0L3lenS774) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1973;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS2200;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1973
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS775);
    _M0L6_2aoldS2200
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1973[_M0L5indexS776];
    if (_M0L6_2aoldS2200) {
      moonbit_decref_cycle_free(_M0L6_2aoldS2200);
    }
    _M0L6_2atmpS1973[_M0L5indexS776] = _M0L5valueS777;
    moonbit_decref_cycle_free(_M0L6_2atmpS1973);
  } else {
    moonbit_decref_cycle_free(_M0L5valueS777);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS779,
  int32_t _M0L5indexS780,
  int32_t _M0L5valueS781
) {
  int32_t _M0L3lenS778;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS778 = _M0L4selfS779->$1;
  if (_M0L5indexS780 >= 0 && _M0L5indexS780 < _M0L3lenS778) {
    int32_t* _M0L6_2atmpS1974;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1974 = _M0MPC15array5Array6bufferGiE(_M0L4selfS779);
    _M0L6_2atmpS1974[_M0L5indexS780] = _M0L5valueS781;
    moonbit_decref_cycle_free(_M0L6_2atmpS1974);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS759,
  int32_t _M0L5indexS760
) {
  int32_t _M0L3lenS758;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS758 = _M0L4selfS759->$1;
  if (_M0L5indexS760 >= 0 && _M0L5indexS760 < _M0L3lenS758) {
    float* _M0L6_2atmpS1968;
    float _result_2292;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1968 = _M0MPC15array5Array6bufferGfE(_M0L4selfS759);
    _result_2292 = (float)_M0L6_2atmpS1968[_M0L5indexS760];
    moonbit_decref_cycle_free(_M0L6_2atmpS1968);
    return _result_2292;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS762,
  int32_t _M0L5indexS763
) {
  int32_t _M0L3lenS761;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS761 = _M0L4selfS762->$1;
  if (_M0L5indexS763 >= 0 && _M0L5indexS763 < _M0L3lenS761) {
    int32_t* _M0L6_2atmpS1969;
    int32_t _result_2293;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1969 = _M0MPC15array5Array6bufferGiE(_M0L4selfS762);
    _result_2293 = (int32_t)_M0L6_2atmpS1969[_M0L5indexS763];
    moonbit_decref_cycle_free(_M0L6_2atmpS1969);
    return _result_2293;
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
    moonbit_string_t* _M0L6_2atmpS1970;
    moonbit_string_t _M0L6_2atmpS2201;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1970 = _M0MPC15array5Array6bufferGsE(_M0L4selfS765);
    _M0L6_2atmpS2201 = (moonbit_string_t)_M0L6_2atmpS1970[_M0L5indexS766];
    moonbit_incref_cycle_free(_M0L6_2atmpS2201);
    moonbit_decref_cycle_free(_M0L6_2atmpS1970);
    return _M0L6_2atmpS2201;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS768,
  int32_t _M0L5indexS769
) {
  int32_t _M0L3lenS767;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS767 = _M0L4selfS768->$1;
  if (_M0L5indexS769 >= 0 && _M0L5indexS769 < _M0L3lenS767) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1971;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS2202;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1971
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS768);
    _M0L6_2atmpS2202
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1971[_M0L5indexS769];
    if (_M0L6_2atmpS2202) {
      moonbit_incref_cycle_free(_M0L6_2atmpS2202);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS1971);
    return _M0L6_2atmpS2202;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS757) {
  moonbit_string_t _M0L6_2atmpS1967;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1967 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS757);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1967);
  moonbit_decref_cycle_free(_M0L6_2atmpS1967);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS756) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS756);
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS755) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS755 > _M0FPB18double__max__value
         || _M0L4selfS755 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS754) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS754 != _M0L4selfS754;
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS739) {
  uint64_t _M0L4bitsS742;
  uint64_t _M0L6_2atmpS1966;
  uint64_t _M0L6_2atmpS1965;
  int32_t _M0L8ieeeSignS743;
  uint64_t _M0L12ieeeMantissaS744;
  uint64_t _M0L6_2atmpS1964;
  uint64_t _M0L6_2atmpS1963;
  int32_t _M0L12ieeeExponentS745;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS746;
  struct _M0TPB17FloatingDecimal64* _M0L1vS747;
  moonbit_string_t _result_2295;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS739 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  if (_M0L3valS739 >= -0x1p+53 && _M0L3valS739 <= 0x1p+53) {
    if (_M0L3valS739 >= -0x1p+31 && _M0L3valS739 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS740;
      double _M0L6_2atmpS1952;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS740 = _M0MPC16double6Double7to__int(_M0L3valS739);
      _M0L6_2atmpS1952 = (double)_M0L1iS740;
      if (_M0L6_2atmpS1952 == _M0L3valS739) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS740, 10);
      }
    } else {
      int64_t _M0L1iS741;
      double _M0L6_2atmpS1953;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS741 = _M0MPC16double6Double9to__int64(_M0L3valS739);
      _M0L6_2atmpS1953 = (double)_M0L1iS741;
      if (_M0L6_2atmpS1953 == _M0L3valS739) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS741, 10);
      }
    }
  }
  _M0L4bitsS742 = *(int64_t*)&_M0L3valS739;
  _M0L6_2atmpS1966 = _M0L4bitsS742 >> 63;
  _M0L6_2atmpS1965 = _M0L6_2atmpS1966 & 1ull;
  _M0L8ieeeSignS743 = _M0L6_2atmpS1965 != 0ull;
  _M0L12ieeeMantissaS744 = _M0L4bitsS742 & 4503599627370495ull;
  _M0L6_2atmpS1964 = _M0L4bitsS742 >> 52;
  _M0L6_2atmpS1963 = _M0L6_2atmpS1964 & 2047ull;
  _M0L12ieeeExponentS745 = (int32_t)_M0L6_2atmpS1963;
  if (
    _M0L12ieeeExponentS745 == 2047
    || _M0L12ieeeExponentS745 == 0 && _M0L12ieeeMantissaS744 == 0ull
  ) {
    int32_t _M0L6_2atmpS1954 = _M0L12ieeeExponentS745 != 0;
    int32_t _M0L6_2atmpS1955 = _M0L12ieeeMantissaS744 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS743, _M0L6_2atmpS1954, _M0L6_2atmpS1955);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS746
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS744, _M0L12ieeeExponentS745);
  if (_M0L7_2abindS746 == 0) {
    uint32_t _M0L6_2atmpS1956;
    if (_M0L7_2abindS746) {
      moonbit_decref_cycle_free(_M0L7_2abindS746);
    }
    _M0L6_2atmpS1956 = *(uint32_t*)&_M0L12ieeeExponentS745;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS747 = _M0FPB3d2d(_M0L12ieeeMantissaS744, _M0L6_2atmpS1956);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS748 = _M0L7_2abindS746;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS749 = _M0L7_2aSomeS748;
    struct _M0TPB17FloatingDecimal64* _M0L1xS750 = _M0L4_2afS749;
    while (1) {
      uint64_t _M0L8mantissaS1962 = _M0L1xS750->$0;
      uint64_t _M0L1qS751 = _M0L8mantissaS1962 / 10ull;
      uint64_t _M0L8mantissaS1960 = _M0L1xS750->$0;
      uint64_t _M0L6_2atmpS1961 = 10ull * _M0L1qS751;
      uint64_t _M0L1rS752 = _M0L8mantissaS1960 - _M0L6_2atmpS1961;
      int32_t _M0L8exponentS1959;
      int32_t _M0L6_2atmpS1958;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1957;
      if (_M0L1rS752 != 0ull) {
        _M0L1vS747 = _M0L1xS750;
        break;
      }
      _M0L8exponentS1959 = _M0L1xS750->$1;
      moonbit_decref_cycle_free(_M0L1xS750);
      _M0L6_2atmpS1958 = _M0L8exponentS1959 + 1;
      _M0L6_2atmpS1957
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1957)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1957->$0 = _M0L1qS751;
      _M0L6_2atmpS1957->$1 = _M0L6_2atmpS1958;
      _M0L1xS750 = _M0L6_2atmpS1957;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2295 = _M0FPB9to__chars(_M0L1vS747, _M0L8ieeeSignS743);
  moonbit_decref_cycle_free(_M0L1vS747);
  return _result_2295;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS734,
  int32_t _M0L12ieeeExponentS736
) {
  uint64_t _M0L2m2S733;
  int32_t _M0L6_2atmpS1951;
  int32_t _M0L2e2S735;
  int32_t _M0L6_2atmpS1950;
  uint64_t _M0L6_2atmpS1949;
  uint64_t _M0L4maskS737;
  uint64_t _M0L8fractionS738;
  int32_t _M0L6_2atmpS1948;
  uint64_t _M0L6_2atmpS1947;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1946;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S733 = 4503599627370496ull | _M0L12ieeeMantissaS734;
  _M0L6_2atmpS1951 = _M0L12ieeeExponentS736 - 1023;
  _M0L2e2S735 = _M0L6_2atmpS1951 - 52;
  if (_M0L2e2S735 > 0) {
    return 0;
  }
  if (_M0L2e2S735 < -52) {
    return 0;
  }
  _M0L6_2atmpS1950 = -_M0L2e2S735;
  _M0L6_2atmpS1949 = 1ull << (_M0L6_2atmpS1950 & 63);
  _M0L4maskS737 = _M0L6_2atmpS1949 - 1ull;
  _M0L8fractionS738 = _M0L2m2S733 & _M0L4maskS737;
  if (_M0L8fractionS738 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1948 = -_M0L2e2S735;
  _M0L6_2atmpS1947 = _M0L2m2S733 >> (_M0L6_2atmpS1948 & 63);
  _M0L6_2atmpS1946
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1946)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1946->$0 = _M0L6_2atmpS1947;
  _M0L6_2atmpS1946->$1 = 0;
  return _M0L6_2atmpS1946;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS701,
  int32_t _M0L4signS699
) {
  moonbit_bytes_t _M0L6resultS697;
  int32_t _M0Lm5indexS698;
  uint64_t _M0L6outputS700;
  int32_t _M0L7olengthS702;
  int32_t _M0L8exponentS1945;
  int32_t _M0L6_2atmpS1944;
  int32_t _M0Lm3expS703;
  int32_t _M0L6_2atmpS1943;
  int32_t _M0L6_2atmpS1941;
  int32_t _M0L18scientificNotationS704;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS697 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS698 = 0;
  if (_M0L4signS699) {
    int32_t _M0L6_2atmpS1815 = _M0Lm5indexS698;
    int32_t _M0L6_2atmpS1816;
    if (
      _M0L6_2atmpS1815 < 0
      || _M0L6_2atmpS1815 >= Moonbit_array_length(_M0L6resultS697)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS697[_M0L6_2atmpS1815] = 45;
    _M0L6_2atmpS1816 = _M0Lm5indexS698;
    _M0Lm5indexS698 = _M0L6_2atmpS1816 + 1;
  }
  _M0L6outputS700 = _M0L1vS701->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS702 = _M0FPB17decimal__length17(_M0L6outputS700);
  _M0L8exponentS1945 = _M0L1vS701->$1;
  _M0L6_2atmpS1944 = _M0L8exponentS1945 + _M0L7olengthS702;
  _M0Lm3expS703 = _M0L6_2atmpS1944 - 1;
  _M0L6_2atmpS1943 = _M0Lm3expS703;
  if (_M0L6_2atmpS1943 >= -6) {
    int32_t _M0L6_2atmpS1942 = _M0Lm3expS703;
    _M0L6_2atmpS1941 = _M0L6_2atmpS1942 < 21;
  } else {
    _M0L6_2atmpS1941 = 0;
  }
  _M0L18scientificNotationS704 = !_M0L6_2atmpS1941;
  if (_M0L18scientificNotationS704) {
    int32_t _M0L7_2abindS705 = _M0L7olengthS702 - 1;
    uint64_t _M0L6outputS706;
    int32_t _M0L1iS707 = 0;
    uint64_t _M0L6outputS708 = _M0L6outputS700;
    int32_t _M0L6_2atmpS1817;
    int32_t _M0L6_2atmpS1821;
    int32_t _M0L6_2atmpS1820;
    int32_t _M0L6_2atmpS1819;
    int32_t _M0L6_2atmpS1818;
    int32_t _M0L6_2atmpS1825;
    int32_t _M0L6_2atmpS1826;
    int32_t _M0L6_2atmpS1827;
    int32_t _M0L6_2atmpS1828;
    int32_t _M0L6_2atmpS1829;
    int32_t _M0L6_2atmpS1835;
    int32_t _M0L6_2atmpS1868;
    moonbit_string_t _result_2297;
    while (1) {
      if (_M0L1iS707 < _M0L7_2abindS705) {
        uint64_t _M0L1cS709 = _M0L6outputS708 % 10ull;
        int32_t _M0L6_2atmpS1874 = _M0Lm5indexS698;
        int32_t _M0L6_2atmpS1873 = _M0L6_2atmpS1874 + _M0L7olengthS702;
        int32_t _M0L6_2atmpS1869 = _M0L6_2atmpS1873 - _M0L1iS707;
        int32_t _M0L6_2atmpS1872 = (int32_t)_M0L1cS709;
        int32_t _M0L6_2atmpS1871 = 48 + _M0L6_2atmpS1872;
        int32_t _M0L6_2atmpS1870 = _M0L6_2atmpS1871 & 0xff;
        int32_t _M0L6_2atmpS1875;
        uint64_t _M0L6_2atmpS1876;
        if (
          _M0L6_2atmpS1869 < 0
          || _M0L6_2atmpS1869 >= Moonbit_array_length(_M0L6resultS697)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS697[_M0L6_2atmpS1869] = _M0L6_2atmpS1870;
        _M0L6_2atmpS1875 = _M0L1iS707 + 1;
        _M0L6_2atmpS1876 = _M0L6outputS708 / 10ull;
        _M0L1iS707 = _M0L6_2atmpS1875;
        _M0L6outputS708 = _M0L6_2atmpS1876;
        continue;
      } else {
        _M0L6outputS706 = _M0L6outputS708;
      }
      break;
    }
    _M0L6_2atmpS1817 = _M0Lm5indexS698;
    _M0L6_2atmpS1821 = (int32_t)_M0L6outputS706;
    _M0L6_2atmpS1820 = _M0L6_2atmpS1821 % 10;
    _M0L6_2atmpS1819 = 48 + _M0L6_2atmpS1820;
    _M0L6_2atmpS1818 = _M0L6_2atmpS1819 & 0xff;
    if (
      _M0L6_2atmpS1817 < 0
      || _M0L6_2atmpS1817 >= Moonbit_array_length(_M0L6resultS697)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS697[_M0L6_2atmpS1817] = _M0L6_2atmpS1818;
    if (_M0L7olengthS702 > 1) {
      int32_t _M0L6_2atmpS1823 = _M0Lm5indexS698;
      int32_t _M0L6_2atmpS1822 = _M0L6_2atmpS1823 + 1;
      if (
        _M0L6_2atmpS1822 < 0
        || _M0L6_2atmpS1822 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1822] = 46;
    } else {
      int32_t _M0L6_2atmpS1824 = _M0Lm5indexS698;
      _M0Lm5indexS698 = _M0L6_2atmpS1824 - 1;
    }
    _M0L6_2atmpS1825 = _M0Lm5indexS698;
    _M0L6_2atmpS1826 = _M0L7olengthS702 + 1;
    _M0Lm5indexS698 = _M0L6_2atmpS1825 + _M0L6_2atmpS1826;
    _M0L6_2atmpS1827 = _M0Lm5indexS698;
    if (
      _M0L6_2atmpS1827 < 0
      || _M0L6_2atmpS1827 >= Moonbit_array_length(_M0L6resultS697)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS697[_M0L6_2atmpS1827] = 101;
    _M0L6_2atmpS1828 = _M0Lm5indexS698;
    _M0Lm5indexS698 = _M0L6_2atmpS1828 + 1;
    _M0L6_2atmpS1829 = _M0Lm3expS703;
    if (_M0L6_2atmpS1829 < 0) {
      int32_t _M0L6_2atmpS1830 = _M0Lm5indexS698;
      int32_t _M0L6_2atmpS1831;
      int32_t _M0L6_2atmpS1832;
      if (
        _M0L6_2atmpS1830 < 0
        || _M0L6_2atmpS1830 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1830] = 45;
      _M0L6_2atmpS1831 = _M0Lm5indexS698;
      _M0Lm5indexS698 = _M0L6_2atmpS1831 + 1;
      _M0L6_2atmpS1832 = _M0Lm3expS703;
      _M0Lm3expS703 = -_M0L6_2atmpS1832;
    } else {
      int32_t _M0L6_2atmpS1833 = _M0Lm5indexS698;
      int32_t _M0L6_2atmpS1834;
      if (
        _M0L6_2atmpS1833 < 0
        || _M0L6_2atmpS1833 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1833] = 43;
      _M0L6_2atmpS1834 = _M0Lm5indexS698;
      _M0Lm5indexS698 = _M0L6_2atmpS1834 + 1;
    }
    _M0L6_2atmpS1835 = _M0Lm3expS703;
    if (_M0L6_2atmpS1835 >= 100) {
      int32_t _M0L6_2atmpS1851 = _M0Lm3expS703;
      int32_t _M0L1aS711 = _M0L6_2atmpS1851 / 100;
      int32_t _M0L6_2atmpS1850 = _M0Lm3expS703;
      int32_t _M0L6_2atmpS1849 = _M0L6_2atmpS1850 / 10;
      int32_t _M0L1bS712 = _M0L6_2atmpS1849 % 10;
      int32_t _M0L6_2atmpS1848 = _M0Lm3expS703;
      int32_t _M0L1cS713 = _M0L6_2atmpS1848 % 10;
      int32_t _M0L6_2atmpS1836 = _M0Lm5indexS698;
      int32_t _M0L6_2atmpS1838 = 48 + _M0L1aS711;
      int32_t _M0L6_2atmpS1837 = _M0L6_2atmpS1838 & 0xff;
      int32_t _M0L6_2atmpS1842;
      int32_t _M0L6_2atmpS1839;
      int32_t _M0L6_2atmpS1841;
      int32_t _M0L6_2atmpS1840;
      int32_t _M0L6_2atmpS1846;
      int32_t _M0L6_2atmpS1843;
      int32_t _M0L6_2atmpS1845;
      int32_t _M0L6_2atmpS1844;
      int32_t _M0L6_2atmpS1847;
      if (
        _M0L6_2atmpS1836 < 0
        || _M0L6_2atmpS1836 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1836] = _M0L6_2atmpS1837;
      _M0L6_2atmpS1842 = _M0Lm5indexS698;
      _M0L6_2atmpS1839 = _M0L6_2atmpS1842 + 1;
      _M0L6_2atmpS1841 = 48 + _M0L1bS712;
      _M0L6_2atmpS1840 = _M0L6_2atmpS1841 & 0xff;
      if (
        _M0L6_2atmpS1839 < 0
        || _M0L6_2atmpS1839 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1839] = _M0L6_2atmpS1840;
      _M0L6_2atmpS1846 = _M0Lm5indexS698;
      _M0L6_2atmpS1843 = _M0L6_2atmpS1846 + 2;
      _M0L6_2atmpS1845 = 48 + _M0L1cS713;
      _M0L6_2atmpS1844 = _M0L6_2atmpS1845 & 0xff;
      if (
        _M0L6_2atmpS1843 < 0
        || _M0L6_2atmpS1843 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1843] = _M0L6_2atmpS1844;
      _M0L6_2atmpS1847 = _M0Lm5indexS698;
      _M0Lm5indexS698 = _M0L6_2atmpS1847 + 3;
    } else {
      int32_t _M0L6_2atmpS1852 = _M0Lm3expS703;
      if (_M0L6_2atmpS1852 >= 10) {
        int32_t _M0L6_2atmpS1862 = _M0Lm3expS703;
        int32_t _M0L1aS714 = _M0L6_2atmpS1862 / 10;
        int32_t _M0L6_2atmpS1861 = _M0Lm3expS703;
        int32_t _M0L1bS715 = _M0L6_2atmpS1861 % 10;
        int32_t _M0L6_2atmpS1853 = _M0Lm5indexS698;
        int32_t _M0L6_2atmpS1855 = 48 + _M0L1aS714;
        int32_t _M0L6_2atmpS1854 = _M0L6_2atmpS1855 & 0xff;
        int32_t _M0L6_2atmpS1859;
        int32_t _M0L6_2atmpS1856;
        int32_t _M0L6_2atmpS1858;
        int32_t _M0L6_2atmpS1857;
        int32_t _M0L6_2atmpS1860;
        if (
          _M0L6_2atmpS1853 < 0
          || _M0L6_2atmpS1853 >= Moonbit_array_length(_M0L6resultS697)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS697[_M0L6_2atmpS1853] = _M0L6_2atmpS1854;
        _M0L6_2atmpS1859 = _M0Lm5indexS698;
        _M0L6_2atmpS1856 = _M0L6_2atmpS1859 + 1;
        _M0L6_2atmpS1858 = 48 + _M0L1bS715;
        _M0L6_2atmpS1857 = _M0L6_2atmpS1858 & 0xff;
        if (
          _M0L6_2atmpS1856 < 0
          || _M0L6_2atmpS1856 >= Moonbit_array_length(_M0L6resultS697)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS697[_M0L6_2atmpS1856] = _M0L6_2atmpS1857;
        _M0L6_2atmpS1860 = _M0Lm5indexS698;
        _M0Lm5indexS698 = _M0L6_2atmpS1860 + 2;
      } else {
        int32_t _M0L6_2atmpS1863 = _M0Lm5indexS698;
        int32_t _M0L6_2atmpS1866 = _M0Lm3expS703;
        int32_t _M0L6_2atmpS1865 = 48 + _M0L6_2atmpS1866;
        int32_t _M0L6_2atmpS1864 = _M0L6_2atmpS1865 & 0xff;
        int32_t _M0L6_2atmpS1867;
        if (
          _M0L6_2atmpS1863 < 0
          || _M0L6_2atmpS1863 >= Moonbit_array_length(_M0L6resultS697)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS697[_M0L6_2atmpS1863] = _M0L6_2atmpS1864;
        _M0L6_2atmpS1867 = _M0Lm5indexS698;
        _M0Lm5indexS698 = _M0L6_2atmpS1867 + 1;
      }
    }
    _M0L6_2atmpS1868 = _M0Lm5indexS698;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2297
    = _M0FPB19string__from__bytes(_M0L6resultS697, 0, _M0L6_2atmpS1868);
    moonbit_decref_cycle_free(_M0L6resultS697);
    return _result_2297;
  } else {
    int32_t _M0L6_2atmpS1877 = _M0Lm3expS703;
    int32_t _M0L6_2atmpS1940;
    moonbit_string_t _result_2303;
    if (_M0L6_2atmpS1877 < 0) {
      int32_t _M0L6_2atmpS1878 = _M0Lm5indexS698;
      int32_t _M0L6_2atmpS1880;
      int32_t _M0L6_2atmpS1879;
      int32_t _M0L6_2atmpS1881;
      int32_t _M0L1iS716;
      int32_t _M0L6_2atmpS1896;
      int32_t _M0L6_2atmpS1898;
      int32_t _M0L6_2atmpS1897;
      int32_t _M0L7currentS718;
      int32_t _M0L1iS719;
      uint64_t _M0L6outputS720;
      if (
        _M0L6_2atmpS1878 < 0
        || _M0L6_2atmpS1878 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1878] = 48;
      _M0L6_2atmpS1880 = _M0Lm5indexS698;
      _M0L6_2atmpS1879 = _M0L6_2atmpS1880 + 1;
      if (
        _M0L6_2atmpS1879 < 0
        || _M0L6_2atmpS1879 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1879] = 46;
      _M0L6_2atmpS1881 = _M0Lm5indexS698;
      _M0Lm5indexS698 = _M0L6_2atmpS1881 + 2;
      _M0L1iS716 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1882 = _M0Lm3expS703;
        if (_M0L1iS716 > _M0L6_2atmpS1882) {
          int32_t _M0L6_2atmpS1885 = _M0Lm5indexS698;
          int32_t _M0L6_2atmpS1884 = _M0L6_2atmpS1885 - _M0L1iS716;
          int32_t _M0L6_2atmpS1883 = _M0L6_2atmpS1884 - 1;
          int32_t _M0L6_2atmpS1886;
          if (
            _M0L6_2atmpS1883 < 0
            || _M0L6_2atmpS1883 >= Moonbit_array_length(_M0L6resultS697)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS697[_M0L6_2atmpS1883] = 48;
          _M0L6_2atmpS1886 = _M0L1iS716 - 1;
          _M0L1iS716 = _M0L6_2atmpS1886;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1896 = _M0Lm5indexS698;
      _M0L6_2atmpS1898 = _M0Lm3expS703;
      _M0L6_2atmpS1897 = -1 - _M0L6_2atmpS1898;
      _M0L7currentS718 = _M0L6_2atmpS1896 + _M0L6_2atmpS1897;
      _M0L1iS719 = 0;
      _M0L6outputS720 = _M0L6outputS700;
      while (1) {
        if (_M0L1iS719 < _M0L7olengthS702) {
          int32_t _M0L6_2atmpS1893 = _M0L7currentS718 + _M0L7olengthS702;
          int32_t _M0L6_2atmpS1892 = _M0L6_2atmpS1893 - _M0L1iS719;
          int32_t _M0L6_2atmpS1887 = _M0L6_2atmpS1892 - 1;
          uint64_t _M0L6_2atmpS1891 = _M0L6outputS720 % 10ull;
          int32_t _M0L6_2atmpS1890 = (int32_t)_M0L6_2atmpS1891;
          int32_t _M0L6_2atmpS1889 = 48 + _M0L6_2atmpS1890;
          int32_t _M0L6_2atmpS1888 = _M0L6_2atmpS1889 & 0xff;
          int32_t _M0L6_2atmpS1894;
          uint64_t _M0L6_2atmpS1895;
          if (
            _M0L6_2atmpS1887 < 0
            || _M0L6_2atmpS1887 >= Moonbit_array_length(_M0L6resultS697)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS697[_M0L6_2atmpS1887] = _M0L6_2atmpS1888;
          _M0L6_2atmpS1894 = _M0L1iS719 + 1;
          _M0L6_2atmpS1895 = _M0L6outputS720 / 10ull;
          _M0L1iS719 = _M0L6_2atmpS1894;
          _M0L6outputS720 = _M0L6_2atmpS1895;
          continue;
        }
        break;
      }
      _M0Lm5indexS698 = _M0L7currentS718 + _M0L7olengthS702;
    } else {
      int32_t _M0L6_2atmpS1900 = _M0Lm3expS703;
      int32_t _M0L6_2atmpS1899 = _M0L6_2atmpS1900 + 1;
      if (_M0L6_2atmpS1899 >= _M0L7olengthS702) {
        int32_t _M0L1iS722 = 0;
        uint64_t _M0L6outputS723 = _M0L6outputS700;
        int32_t _M0L6_2atmpS1911;
        int32_t _M0L6_2atmpS1916;
        int32_t _M0L7_2abindS725;
        int32_t _M0L1iS726;
        int32_t _M0L6_2atmpS1917;
        int32_t _M0L6_2atmpS1920;
        int32_t _M0L6_2atmpS1919;
        int32_t _M0L6_2atmpS1918;
        while (1) {
          if (_M0L1iS722 < _M0L7olengthS702) {
            int32_t _M0L6_2atmpS1908 = _M0Lm5indexS698;
            int32_t _M0L6_2atmpS1907 = _M0L6_2atmpS1908 + _M0L7olengthS702;
            int32_t _M0L6_2atmpS1906 = _M0L6_2atmpS1907 - _M0L1iS722;
            int32_t _M0L6_2atmpS1901 = _M0L6_2atmpS1906 - 1;
            uint64_t _M0L6_2atmpS1905 = _M0L6outputS723 % 10ull;
            int32_t _M0L6_2atmpS1904 = (int32_t)_M0L6_2atmpS1905;
            int32_t _M0L6_2atmpS1903 = 48 + _M0L6_2atmpS1904;
            int32_t _M0L6_2atmpS1902 = _M0L6_2atmpS1903 & 0xff;
            int32_t _M0L6_2atmpS1909;
            uint64_t _M0L6_2atmpS1910;
            if (
              _M0L6_2atmpS1901 < 0
              || _M0L6_2atmpS1901 >= Moonbit_array_length(_M0L6resultS697)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS697[_M0L6_2atmpS1901] = _M0L6_2atmpS1902;
            _M0L6_2atmpS1909 = _M0L1iS722 + 1;
            _M0L6_2atmpS1910 = _M0L6outputS723 / 10ull;
            _M0L1iS722 = _M0L6_2atmpS1909;
            _M0L6outputS723 = _M0L6_2atmpS1910;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1911 = _M0Lm5indexS698;
        _M0Lm5indexS698 = _M0L6_2atmpS1911 + _M0L7olengthS702;
        _M0L6_2atmpS1916 = _M0Lm3expS703;
        _M0L7_2abindS725 = _M0L6_2atmpS1916 + 1;
        _M0L1iS726 = _M0L7olengthS702;
        while (1) {
          if (_M0L1iS726 < _M0L7_2abindS725) {
            int32_t _M0L6_2atmpS1914 = _M0Lm5indexS698;
            int32_t _M0L6_2atmpS1913 = _M0L6_2atmpS1914 + _M0L1iS726;
            int32_t _M0L6_2atmpS1912 = _M0L6_2atmpS1913 - _M0L7olengthS702;
            int32_t _M0L6_2atmpS1915;
            if (
              _M0L6_2atmpS1912 < 0
              || _M0L6_2atmpS1912 >= Moonbit_array_length(_M0L6resultS697)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS697[_M0L6_2atmpS1912] = 48;
            _M0L6_2atmpS1915 = _M0L1iS726 + 1;
            _M0L1iS726 = _M0L6_2atmpS1915;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1917 = _M0Lm5indexS698;
        _M0L6_2atmpS1920 = _M0Lm3expS703;
        _M0L6_2atmpS1919 = _M0L6_2atmpS1920 + 1;
        _M0L6_2atmpS1918 = _M0L6_2atmpS1919 - _M0L7olengthS702;
        _M0Lm5indexS698 = _M0L6_2atmpS1917 + _M0L6_2atmpS1918;
      } else {
        int32_t _M0L6_2atmpS1937 = _M0Lm5indexS698;
        int32_t _M0L6_2atmpS1936 = _M0L6_2atmpS1937 + 1;
        int32_t _M0L1iS728 = 0;
        int32_t _M0L7currentS729 = _M0L6_2atmpS1936;
        uint64_t _M0L6outputS730 = _M0L6outputS700;
        int32_t _M0L6_2atmpS1938;
        int32_t _M0L6_2atmpS1939;
        while (1) {
          if (_M0L1iS728 < _M0L7olengthS702) {
            int32_t _M0L6_2atmpS1932 = _M0L7olengthS702 - _M0L1iS728;
            int32_t _M0L6_2atmpS1930 = _M0L6_2atmpS1932 - 1;
            int32_t _M0L6_2atmpS1931 = _M0Lm3expS703;
            int32_t _M0L7currentS731;
            int32_t _M0L6_2atmpS1927;
            int32_t _M0L6_2atmpS1926;
            int32_t _M0L6_2atmpS1921;
            uint64_t _M0L6_2atmpS1925;
            int32_t _M0L6_2atmpS1924;
            int32_t _M0L6_2atmpS1923;
            int32_t _M0L6_2atmpS1922;
            int32_t _M0L6_2atmpS1928;
            uint64_t _M0L6_2atmpS1929;
            if (_M0L6_2atmpS1930 == _M0L6_2atmpS1931) {
              int32_t _M0L6_2atmpS1935 = _M0L7currentS729 + _M0L7olengthS702;
              int32_t _M0L6_2atmpS1934 = _M0L6_2atmpS1935 - _M0L1iS728;
              int32_t _M0L6_2atmpS1933 = _M0L6_2atmpS1934 - 1;
              if (
                _M0L6_2atmpS1933 < 0
                || _M0L6_2atmpS1933 >= Moonbit_array_length(_M0L6resultS697)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS697[_M0L6_2atmpS1933] = 46;
              _M0L7currentS731 = _M0L7currentS729 - 1;
            } else {
              _M0L7currentS731 = _M0L7currentS729;
            }
            _M0L6_2atmpS1927 = _M0L7currentS731 + _M0L7olengthS702;
            _M0L6_2atmpS1926 = _M0L6_2atmpS1927 - _M0L1iS728;
            _M0L6_2atmpS1921 = _M0L6_2atmpS1926 - 1;
            _M0L6_2atmpS1925 = _M0L6outputS730 % 10ull;
            _M0L6_2atmpS1924 = (int32_t)_M0L6_2atmpS1925;
            _M0L6_2atmpS1923 = 48 + _M0L6_2atmpS1924;
            _M0L6_2atmpS1922 = _M0L6_2atmpS1923 & 0xff;
            if (
              _M0L6_2atmpS1921 < 0
              || _M0L6_2atmpS1921 >= Moonbit_array_length(_M0L6resultS697)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS697[_M0L6_2atmpS1921] = _M0L6_2atmpS1922;
            _M0L6_2atmpS1928 = _M0L1iS728 + 1;
            _M0L6_2atmpS1929 = _M0L6outputS730 / 10ull;
            _M0L1iS728 = _M0L6_2atmpS1928;
            _M0L7currentS729 = _M0L7currentS731;
            _M0L6outputS730 = _M0L6_2atmpS1929;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1938 = _M0Lm5indexS698;
        _M0L6_2atmpS1939 = _M0L7olengthS702 + 1;
        _M0Lm5indexS698 = _M0L6_2atmpS1938 + _M0L6_2atmpS1939;
      }
    }
    _M0L6_2atmpS1940 = _M0Lm5indexS698;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2303
    = _M0FPB19string__from__bytes(_M0L6resultS697, 0, _M0L6_2atmpS1940);
    moonbit_decref_cycle_free(_M0L6resultS697);
    return _result_2303;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS643,
  uint32_t _M0L12ieeeExponentS642
) {
  int32_t _M0Lm2e2S640;
  uint64_t _M0Lm2m2S641;
  uint64_t _M0L6_2atmpS1814;
  uint64_t _M0L6_2atmpS1813;
  int32_t _M0L4evenS644;
  uint64_t _M0L6_2atmpS1812;
  uint64_t _M0L2mvS645;
  int32_t _M0L7mmShiftS646;
  uint64_t _M0Lm2vrS647;
  uint64_t _M0Lm2vpS648;
  uint64_t _M0Lm2vmS649;
  int32_t _M0Lm3e10S650;
  int32_t _M0Lm17vmIsTrailingZerosS651;
  int32_t _M0Lm17vrIsTrailingZerosS652;
  int32_t _M0L6_2atmpS1714;
  int32_t _M0Lm7removedS671;
  int32_t _M0Lm16lastRemovedDigitS672;
  uint64_t _M0Lm6outputS673;
  int32_t _M0L6_2atmpS1810;
  int32_t _M0L6_2atmpS1811;
  int32_t _M0L3expS696;
  uint64_t _M0L6_2atmpS1809;
  struct _M0TPB17FloatingDecimal64* _block_2309;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S640 = 0;
  _M0Lm2m2S641 = 0ull;
  if (_M0L12ieeeExponentS642 == 0u) {
    _M0Lm2e2S640 = -1076;
    _M0Lm2m2S641 = _M0L12ieeeMantissaS643;
  } else {
    int32_t _M0L6_2atmpS1713 = *(int32_t*)&_M0L12ieeeExponentS642;
    int32_t _M0L6_2atmpS1712 = _M0L6_2atmpS1713 - 1023;
    int32_t _M0L6_2atmpS1711 = _M0L6_2atmpS1712 - 52;
    _M0Lm2e2S640 = _M0L6_2atmpS1711 - 2;
    _M0Lm2m2S641 = 4503599627370496ull | _M0L12ieeeMantissaS643;
  }
  _M0L6_2atmpS1814 = _M0Lm2m2S641;
  _M0L6_2atmpS1813 = _M0L6_2atmpS1814 & 1ull;
  _M0L4evenS644 = _M0L6_2atmpS1813 == 0ull;
  _M0L6_2atmpS1812 = _M0Lm2m2S641;
  _M0L2mvS645 = 4ull * _M0L6_2atmpS1812;
  _M0L7mmShiftS646
  = _M0L12ieeeMantissaS643 != 0ull || _M0L12ieeeExponentS642 <= 1u;
  _M0Lm2vrS647 = 0ull;
  _M0Lm2vpS648 = 0ull;
  _M0Lm2vmS649 = 0ull;
  _M0Lm3e10S650 = 0;
  _M0Lm17vmIsTrailingZerosS651 = 0;
  _M0Lm17vrIsTrailingZerosS652 = 0;
  _M0L6_2atmpS1714 = _M0Lm2e2S640;
  if (_M0L6_2atmpS1714 >= 0) {
    int32_t _M0L6_2atmpS1736 = _M0Lm2e2S640;
    int32_t _M0L6_2atmpS1732;
    int32_t _M0L6_2atmpS1735;
    int32_t _M0L6_2atmpS1734;
    int32_t _M0L6_2atmpS1733;
    int32_t _M0L1qS653;
    int32_t _M0L6_2atmpS1731;
    int32_t _M0L6_2atmpS1730;
    int32_t _M0L1kS654;
    int32_t _M0L6_2atmpS1729;
    int32_t _M0L6_2atmpS1728;
    int32_t _M0L6_2atmpS1727;
    int32_t _M0L1iS655;
    struct _M0TPB8Pow5Pair _M0L4pow5S656;
    uint64_t _M0L6_2atmpS1726;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS657;
    uint64_t _M0L8_2avrOutS658;
    uint64_t _M0L8_2avpOutS659;
    uint64_t _M0L8_2avmOutS660;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1732 = _M0FPB9log10Pow2(_M0L6_2atmpS1736);
    _M0L6_2atmpS1735 = _M0Lm2e2S640;
    _M0L6_2atmpS1734 = _M0L6_2atmpS1735 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1733 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1734);
    _M0L1qS653 = _M0L6_2atmpS1732 - _M0L6_2atmpS1733;
    _M0Lm3e10S650 = _M0L1qS653;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1731 = _M0FPB8pow5bits(_M0L1qS653);
    _M0L6_2atmpS1730 = 125 + _M0L6_2atmpS1731;
    _M0L1kS654 = _M0L6_2atmpS1730 - 1;
    _M0L6_2atmpS1729 = _M0Lm2e2S640;
    _M0L6_2atmpS1728 = -_M0L6_2atmpS1729;
    _M0L6_2atmpS1727 = _M0L6_2atmpS1728 + _M0L1qS653;
    _M0L1iS655 = _M0L6_2atmpS1727 + _M0L1kS654;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S656 = _M0FPB22double__computeInvPow5(_M0L1qS653);
    _M0L6_2atmpS1726 = _M0Lm2m2S641;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS657
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1726, _M0L4pow5S656, _M0L1iS655, _M0L7mmShiftS646);
    _M0L8_2avrOutS658 = _M0L7_2abindS657.$0;
    _M0L8_2avpOutS659 = _M0L7_2abindS657.$1;
    _M0L8_2avmOutS660 = _M0L7_2abindS657.$2;
    _M0Lm2vrS647 = _M0L8_2avrOutS658;
    _M0Lm2vpS648 = _M0L8_2avpOutS659;
    _M0Lm2vmS649 = _M0L8_2avmOutS660;
    if (_M0L1qS653 <= 21) {
      int32_t _M0L6_2atmpS1722 = (int32_t)_M0L2mvS645;
      uint64_t _M0L6_2atmpS1725 = _M0L2mvS645 / 5ull;
      int32_t _M0L6_2atmpS1724 = (int32_t)_M0L6_2atmpS1725;
      int32_t _M0L6_2atmpS1723 = 5 * _M0L6_2atmpS1724;
      int32_t _M0L6mvMod5S661 = _M0L6_2atmpS1722 - _M0L6_2atmpS1723;
      if (_M0L6mvMod5S661 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS652
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS645, _M0L1qS653);
      } else if (_M0L4evenS644) {
        uint64_t _M0L6_2atmpS1716 = _M0L2mvS645 - 1ull;
        uint64_t _M0L6_2atmpS1717;
        uint64_t _M0L6_2atmpS1715;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1717 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS646);
        _M0L6_2atmpS1715 = _M0L6_2atmpS1716 - _M0L6_2atmpS1717;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS651
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1715, _M0L1qS653);
      } else {
        uint64_t _M0L6_2atmpS1718 = _M0Lm2vpS648;
        uint64_t _M0L6_2atmpS1721 = _M0L2mvS645 + 2ull;
        int32_t _M0L6_2atmpS1720;
        uint64_t _M0L6_2atmpS1719;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1720
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1721, _M0L1qS653);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1719 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1720);
        _M0Lm2vpS648 = _M0L6_2atmpS1718 - _M0L6_2atmpS1719;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1750 = _M0Lm2e2S640;
    int32_t _M0L6_2atmpS1749 = -_M0L6_2atmpS1750;
    int32_t _M0L6_2atmpS1744;
    int32_t _M0L6_2atmpS1748;
    int32_t _M0L6_2atmpS1747;
    int32_t _M0L6_2atmpS1746;
    int32_t _M0L6_2atmpS1745;
    int32_t _M0L1qS662;
    int32_t _M0L6_2atmpS1737;
    int32_t _M0L6_2atmpS1743;
    int32_t _M0L6_2atmpS1742;
    int32_t _M0L1iS663;
    int32_t _M0L6_2atmpS1741;
    int32_t _M0L1kS664;
    int32_t _M0L1jS665;
    struct _M0TPB8Pow5Pair _M0L4pow5S666;
    uint64_t _M0L6_2atmpS1740;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS667;
    uint64_t _M0L8_2avrOutS668;
    uint64_t _M0L8_2avpOutS669;
    uint64_t _M0L8_2avmOutS670;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1744 = _M0FPB9log10Pow5(_M0L6_2atmpS1749);
    _M0L6_2atmpS1748 = _M0Lm2e2S640;
    _M0L6_2atmpS1747 = -_M0L6_2atmpS1748;
    _M0L6_2atmpS1746 = _M0L6_2atmpS1747 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1745 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1746);
    _M0L1qS662 = _M0L6_2atmpS1744 - _M0L6_2atmpS1745;
    _M0L6_2atmpS1737 = _M0Lm2e2S640;
    _M0Lm3e10S650 = _M0L1qS662 + _M0L6_2atmpS1737;
    _M0L6_2atmpS1743 = _M0Lm2e2S640;
    _M0L6_2atmpS1742 = -_M0L6_2atmpS1743;
    _M0L1iS663 = _M0L6_2atmpS1742 - _M0L1qS662;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1741 = _M0FPB8pow5bits(_M0L1iS663);
    _M0L1kS664 = _M0L6_2atmpS1741 - 125;
    _M0L1jS665 = _M0L1qS662 - _M0L1kS664;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S666 = _M0FPB19double__computePow5(_M0L1iS663);
    _M0L6_2atmpS1740 = _M0Lm2m2S641;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS667
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1740, _M0L4pow5S666, _M0L1jS665, _M0L7mmShiftS646);
    _M0L8_2avrOutS668 = _M0L7_2abindS667.$0;
    _M0L8_2avpOutS669 = _M0L7_2abindS667.$1;
    _M0L8_2avmOutS670 = _M0L7_2abindS667.$2;
    _M0Lm2vrS647 = _M0L8_2avrOutS668;
    _M0Lm2vpS648 = _M0L8_2avpOutS669;
    _M0Lm2vmS649 = _M0L8_2avmOutS670;
    if (_M0L1qS662 <= 1) {
      _M0Lm17vrIsTrailingZerosS652 = 1;
      if (_M0L4evenS644) {
        int32_t _M0L6_2atmpS1738;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1738 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS646);
        _M0Lm17vmIsTrailingZerosS651 = _M0L6_2atmpS1738 == 1;
      } else {
        uint64_t _M0L6_2atmpS1739 = _M0Lm2vpS648;
        _M0Lm2vpS648 = _M0L6_2atmpS1739 - 1ull;
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
    int32_t _if__result_2306;
    uint64_t _M0L6_2atmpS1780;
    uint64_t _M0L6_2atmpS1786;
    uint64_t _M0L6_2atmpS1787;
    int32_t _if__result_2307;
    int32_t _M0L6_2atmpS1783;
    int64_t _M0L6_2atmpS1782;
    uint64_t _M0L6_2atmpS1781;
    while (1) {
      uint64_t _M0L6_2atmpS1763 = _M0Lm2vpS648;
      uint64_t _M0L7vpDiv10S674 = _M0L6_2atmpS1763 / 10ull;
      uint64_t _M0L6_2atmpS1762 = _M0Lm2vmS649;
      uint64_t _M0L7vmDiv10S675 = _M0L6_2atmpS1762 / 10ull;
      uint64_t _M0L6_2atmpS1761;
      int32_t _M0L6_2atmpS1758;
      int32_t _M0L6_2atmpS1760;
      int32_t _M0L6_2atmpS1759;
      int32_t _M0L7vmMod10S677;
      uint64_t _M0L6_2atmpS1757;
      uint64_t _M0L7vrDiv10S678;
      uint64_t _M0L6_2atmpS1756;
      int32_t _M0L6_2atmpS1753;
      int32_t _M0L6_2atmpS1755;
      int32_t _M0L6_2atmpS1754;
      int32_t _M0L7vrMod10S679;
      int32_t _M0L6_2atmpS1752;
      if (_M0L7vpDiv10S674 <= _M0L7vmDiv10S675) {
        break;
      }
      _M0L6_2atmpS1761 = _M0Lm2vmS649;
      _M0L6_2atmpS1758 = (int32_t)_M0L6_2atmpS1761;
      _M0L6_2atmpS1760 = (int32_t)_M0L7vmDiv10S675;
      _M0L6_2atmpS1759 = 10 * _M0L6_2atmpS1760;
      _M0L7vmMod10S677 = _M0L6_2atmpS1758 - _M0L6_2atmpS1759;
      _M0L6_2atmpS1757 = _M0Lm2vrS647;
      _M0L7vrDiv10S678 = _M0L6_2atmpS1757 / 10ull;
      _M0L6_2atmpS1756 = _M0Lm2vrS647;
      _M0L6_2atmpS1753 = (int32_t)_M0L6_2atmpS1756;
      _M0L6_2atmpS1755 = (int32_t)_M0L7vrDiv10S678;
      _M0L6_2atmpS1754 = 10 * _M0L6_2atmpS1755;
      _M0L7vrMod10S679 = _M0L6_2atmpS1753 - _M0L6_2atmpS1754;
      _M0Lm17vmIsTrailingZerosS651
      = _M0Lm17vmIsTrailingZerosS651 && _M0L7vmMod10S677 == 0;
      if (_M0Lm17vrIsTrailingZerosS652) {
        int32_t _M0L6_2atmpS1751 = _M0Lm16lastRemovedDigitS672;
        _M0Lm17vrIsTrailingZerosS652 = _M0L6_2atmpS1751 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS652 = 0;
      }
      _M0Lm16lastRemovedDigitS672 = _M0L7vrMod10S679;
      _M0Lm2vrS647 = _M0L7vrDiv10S678;
      _M0Lm2vpS648 = _M0L7vpDiv10S674;
      _M0Lm2vmS649 = _M0L7vmDiv10S675;
      _M0L6_2atmpS1752 = _M0Lm7removedS671;
      _M0Lm7removedS671 = _M0L6_2atmpS1752 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS651) {
      while (1) {
        uint64_t _M0L6_2atmpS1776 = _M0Lm2vmS649;
        uint64_t _M0L7vmDiv10S680 = _M0L6_2atmpS1776 / 10ull;
        uint64_t _M0L6_2atmpS1775 = _M0Lm2vmS649;
        int32_t _M0L6_2atmpS1772 = (int32_t)_M0L6_2atmpS1775;
        int32_t _M0L6_2atmpS1774 = (int32_t)_M0L7vmDiv10S680;
        int32_t _M0L6_2atmpS1773 = 10 * _M0L6_2atmpS1774;
        int32_t _M0L7vmMod10S681 = _M0L6_2atmpS1772 - _M0L6_2atmpS1773;
        uint64_t _M0L6_2atmpS1771;
        uint64_t _M0L7vpDiv10S683;
        uint64_t _M0L6_2atmpS1770;
        uint64_t _M0L7vrDiv10S684;
        uint64_t _M0L6_2atmpS1769;
        int32_t _M0L6_2atmpS1766;
        int32_t _M0L6_2atmpS1768;
        int32_t _M0L6_2atmpS1767;
        int32_t _M0L7vrMod10S685;
        int32_t _M0L6_2atmpS1765;
        if (_M0L7vmMod10S681 != 0) {
          break;
        }
        _M0L6_2atmpS1771 = _M0Lm2vpS648;
        _M0L7vpDiv10S683 = _M0L6_2atmpS1771 / 10ull;
        _M0L6_2atmpS1770 = _M0Lm2vrS647;
        _M0L7vrDiv10S684 = _M0L6_2atmpS1770 / 10ull;
        _M0L6_2atmpS1769 = _M0Lm2vrS647;
        _M0L6_2atmpS1766 = (int32_t)_M0L6_2atmpS1769;
        _M0L6_2atmpS1768 = (int32_t)_M0L7vrDiv10S684;
        _M0L6_2atmpS1767 = 10 * _M0L6_2atmpS1768;
        _M0L7vrMod10S685 = _M0L6_2atmpS1766 - _M0L6_2atmpS1767;
        if (_M0Lm17vrIsTrailingZerosS652) {
          int32_t _M0L6_2atmpS1764 = _M0Lm16lastRemovedDigitS672;
          _M0Lm17vrIsTrailingZerosS652 = _M0L6_2atmpS1764 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS652 = 0;
        }
        _M0Lm16lastRemovedDigitS672 = _M0L7vrMod10S685;
        _M0Lm2vrS647 = _M0L7vrDiv10S684;
        _M0Lm2vpS648 = _M0L7vpDiv10S683;
        _M0Lm2vmS649 = _M0L7vmDiv10S680;
        _M0L6_2atmpS1765 = _M0Lm7removedS671;
        _M0Lm7removedS671 = _M0L6_2atmpS1765 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS652) {
      int32_t _M0L6_2atmpS1779 = _M0Lm16lastRemovedDigitS672;
      if (_M0L6_2atmpS1779 == 5) {
        uint64_t _M0L6_2atmpS1778 = _M0Lm2vrS647;
        uint64_t _M0L6_2atmpS1777 = _M0L6_2atmpS1778 % 2ull;
        _if__result_2306 = _M0L6_2atmpS1777 == 0ull;
      } else {
        _if__result_2306 = 0;
      }
    } else {
      _if__result_2306 = 0;
    }
    if (_if__result_2306) {
      _M0Lm16lastRemovedDigitS672 = 4;
    }
    _M0L6_2atmpS1780 = _M0Lm2vrS647;
    _M0L6_2atmpS1786 = _M0Lm2vrS647;
    _M0L6_2atmpS1787 = _M0Lm2vmS649;
    if (_M0L6_2atmpS1786 == _M0L6_2atmpS1787) {
      if (!_M0L4evenS644) {
        _if__result_2307 = 1;
      } else {
        int32_t _M0L6_2atmpS1785 = _M0Lm17vmIsTrailingZerosS651;
        _if__result_2307 = !_M0L6_2atmpS1785;
      }
    } else {
      _if__result_2307 = 0;
    }
    if (_if__result_2307) {
      _M0L6_2atmpS1783 = 1;
    } else {
      int32_t _M0L6_2atmpS1784 = _M0Lm16lastRemovedDigitS672;
      _M0L6_2atmpS1783 = _M0L6_2atmpS1784 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1782 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1783);
    _M0L6_2atmpS1781 = *(uint64_t*)&_M0L6_2atmpS1782;
    _M0Lm6outputS673 = _M0L6_2atmpS1780 + _M0L6_2atmpS1781;
  } else {
    int32_t _M0Lm7roundUpS686 = 0;
    uint64_t _M0L6_2atmpS1808 = _M0Lm2vpS648;
    uint64_t _M0L8vpDiv100S687 = _M0L6_2atmpS1808 / 100ull;
    uint64_t _M0L6_2atmpS1807 = _M0Lm2vmS649;
    uint64_t _M0L8vmDiv100S688 = _M0L6_2atmpS1807 / 100ull;
    uint64_t _M0L6_2atmpS1802;
    uint64_t _M0L6_2atmpS1805;
    uint64_t _M0L6_2atmpS1806;
    int32_t _M0L6_2atmpS1804;
    uint64_t _M0L6_2atmpS1803;
    if (_M0L8vpDiv100S687 > _M0L8vmDiv100S688) {
      uint64_t _M0L6_2atmpS1793 = _M0Lm2vrS647;
      uint64_t _M0L8vrDiv100S689 = _M0L6_2atmpS1793 / 100ull;
      uint64_t _M0L6_2atmpS1792 = _M0Lm2vrS647;
      int32_t _M0L6_2atmpS1789 = (int32_t)_M0L6_2atmpS1792;
      int32_t _M0L6_2atmpS1791 = (int32_t)_M0L8vrDiv100S689;
      int32_t _M0L6_2atmpS1790 = 100 * _M0L6_2atmpS1791;
      int32_t _M0L8vrMod100S690 = _M0L6_2atmpS1789 - _M0L6_2atmpS1790;
      int32_t _M0L6_2atmpS1788;
      _M0Lm7roundUpS686 = _M0L8vrMod100S690 >= 50;
      _M0Lm2vrS647 = _M0L8vrDiv100S689;
      _M0Lm2vpS648 = _M0L8vpDiv100S687;
      _M0Lm2vmS649 = _M0L8vmDiv100S688;
      _M0L6_2atmpS1788 = _M0Lm7removedS671;
      _M0Lm7removedS671 = _M0L6_2atmpS1788 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1801 = _M0Lm2vpS648;
      uint64_t _M0L7vpDiv10S691 = _M0L6_2atmpS1801 / 10ull;
      uint64_t _M0L6_2atmpS1800 = _M0Lm2vmS649;
      uint64_t _M0L7vmDiv10S692 = _M0L6_2atmpS1800 / 10ull;
      uint64_t _M0L6_2atmpS1799;
      uint64_t _M0L7vrDiv10S694;
      uint64_t _M0L6_2atmpS1798;
      int32_t _M0L6_2atmpS1795;
      int32_t _M0L6_2atmpS1797;
      int32_t _M0L6_2atmpS1796;
      int32_t _M0L7vrMod10S695;
      int32_t _M0L6_2atmpS1794;
      if (_M0L7vpDiv10S691 <= _M0L7vmDiv10S692) {
        break;
      }
      _M0L6_2atmpS1799 = _M0Lm2vrS647;
      _M0L7vrDiv10S694 = _M0L6_2atmpS1799 / 10ull;
      _M0L6_2atmpS1798 = _M0Lm2vrS647;
      _M0L6_2atmpS1795 = (int32_t)_M0L6_2atmpS1798;
      _M0L6_2atmpS1797 = (int32_t)_M0L7vrDiv10S694;
      _M0L6_2atmpS1796 = 10 * _M0L6_2atmpS1797;
      _M0L7vrMod10S695 = _M0L6_2atmpS1795 - _M0L6_2atmpS1796;
      _M0Lm7roundUpS686 = _M0L7vrMod10S695 >= 5;
      _M0Lm2vrS647 = _M0L7vrDiv10S694;
      _M0Lm2vpS648 = _M0L7vpDiv10S691;
      _M0Lm2vmS649 = _M0L7vmDiv10S692;
      _M0L6_2atmpS1794 = _M0Lm7removedS671;
      _M0Lm7removedS671 = _M0L6_2atmpS1794 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1802 = _M0Lm2vrS647;
    _M0L6_2atmpS1805 = _M0Lm2vrS647;
    _M0L6_2atmpS1806 = _M0Lm2vmS649;
    _M0L6_2atmpS1804
    = _M0L6_2atmpS1805 == _M0L6_2atmpS1806 || _M0Lm7roundUpS686;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1803 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1804);
    _M0Lm6outputS673 = _M0L6_2atmpS1802 + _M0L6_2atmpS1803;
  }
  _M0L6_2atmpS1810 = _M0Lm3e10S650;
  _M0L6_2atmpS1811 = _M0Lm7removedS671;
  _M0L3expS696 = _M0L6_2atmpS1810 + _M0L6_2atmpS1811;
  _M0L6_2atmpS1809 = _M0Lm6outputS673;
  _block_2309
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2309)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2309->$0 = _M0L6_2atmpS1809;
  _block_2309->$1 = _M0L3expS696;
  return _block_2309;
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
  int32_t _M0L6_2atmpS1710;
  int32_t _M0L6_2atmpS1709;
  int32_t _M0L4baseS618;
  int32_t _M0L5base2S620;
  int32_t _M0L6offsetS621;
  int32_t _M0L6_2atmpS1708;
  uint64_t _M0L4mul0S622;
  int32_t _M0L6_2atmpS1707;
  int32_t _M0L6_2atmpS1706;
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
  int32_t _M0L6_2atmpS1704;
  int32_t _M0L6_2atmpS1705;
  int32_t _M0L5deltaS633;
  uint64_t _M0L6_2atmpS1703;
  uint64_t _M0L6_2atmpS1695;
  int32_t _M0L6_2atmpS1702;
  uint32_t _M0L6_2atmpS1699;
  int32_t _M0L6_2atmpS1701;
  int32_t _M0L6_2atmpS1700;
  uint32_t _M0L6_2atmpS1698;
  uint32_t _M0L6_2atmpS1697;
  uint64_t _M0L6_2atmpS1696;
  uint64_t _M0L1aS634;
  uint64_t _M0L6_2atmpS1694;
  uint64_t _M0L1bS635;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1710 = _M0L1iS619 + 26;
  _M0L6_2atmpS1709 = _M0L6_2atmpS1710 - 1;
  _M0L4baseS618 = _M0L6_2atmpS1709 / 26;
  _M0L5base2S620 = _M0L4baseS618 * 26;
  _M0L6offsetS621 = _M0L5base2S620 - _M0L1iS619;
  _M0L6_2atmpS1708 = _M0L4baseS618 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S622
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1708);
  _M0L6_2atmpS1707 = _M0L4baseS618 * 2;
  _M0L6_2atmpS1706 = _M0L6_2atmpS1707 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S623
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1706);
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
    uint64_t _M0L6_2atmpS1693 = _M0Lm5high1S632;
    _M0Lm5high1S632 = _M0L6_2atmpS1693 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1704 = _M0FPB8pow5bits(_M0L5base2S620);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1705 = _M0FPB8pow5bits(_M0L1iS619);
  _M0L5deltaS633 = _M0L6_2atmpS1704 - _M0L6_2atmpS1705;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1703
  = _M0FPB13shiftright128(_M0L7_2alow0S629, _M0L3sumS631, _M0L5deltaS633);
  _M0L6_2atmpS1695 = _M0L6_2atmpS1703 + 1ull;
  _M0L6_2atmpS1702 = _M0L1iS619 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1699
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1702);
  _M0L6_2atmpS1701 = _M0L1iS619 % 16;
  _M0L6_2atmpS1700 = _M0L6_2atmpS1701 << 1;
  _M0L6_2atmpS1698 = _M0L6_2atmpS1699 >> (_M0L6_2atmpS1700 & 31);
  _M0L6_2atmpS1697 = _M0L6_2atmpS1698 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1696 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1697);
  _M0L1aS634 = _M0L6_2atmpS1695 + _M0L6_2atmpS1696;
  _M0L6_2atmpS1694 = _M0Lm5high1S632;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS635
  = _M0FPB13shiftright128(_M0L3sumS631, _M0L6_2atmpS1694, _M0L5deltaS633);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS634, .$1 = _M0L1bS635};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS601) {
  int32_t _M0L4baseS600;
  int32_t _M0L5base2S602;
  int32_t _M0L6offsetS603;
  int32_t _M0L6_2atmpS1692;
  uint64_t _M0L4mul0S604;
  int32_t _M0L6_2atmpS1691;
  int32_t _M0L6_2atmpS1690;
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
  int32_t _M0L6_2atmpS1688;
  int32_t _M0L6_2atmpS1689;
  int32_t _M0L5deltaS615;
  uint64_t _M0L6_2atmpS1680;
  int32_t _M0L6_2atmpS1687;
  uint32_t _M0L6_2atmpS1684;
  int32_t _M0L6_2atmpS1686;
  int32_t _M0L6_2atmpS1685;
  uint32_t _M0L6_2atmpS1683;
  uint32_t _M0L6_2atmpS1682;
  uint64_t _M0L6_2atmpS1681;
  uint64_t _M0L1aS616;
  uint64_t _M0L6_2atmpS1679;
  uint64_t _M0L1bS617;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS600 = _M0L1iS601 / 26;
  _M0L5base2S602 = _M0L4baseS600 * 26;
  _M0L6offsetS603 = _M0L1iS601 - _M0L5base2S602;
  _M0L6_2atmpS1692 = _M0L4baseS600 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S604
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1692);
  _M0L6_2atmpS1691 = _M0L4baseS600 * 2;
  _M0L6_2atmpS1690 = _M0L6_2atmpS1691 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S605
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1690);
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
    uint64_t _M0L6_2atmpS1678 = _M0Lm5high1S614;
    _M0Lm5high1S614 = _M0L6_2atmpS1678 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1688 = _M0FPB8pow5bits(_M0L1iS601);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1689 = _M0FPB8pow5bits(_M0L5base2S602);
  _M0L5deltaS615 = _M0L6_2atmpS1688 - _M0L6_2atmpS1689;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1680
  = _M0FPB13shiftright128(_M0L7_2alow0S611, _M0L3sumS613, _M0L5deltaS615);
  _M0L6_2atmpS1687 = _M0L1iS601 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1684
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1687);
  _M0L6_2atmpS1686 = _M0L1iS601 % 16;
  _M0L6_2atmpS1685 = _M0L6_2atmpS1686 << 1;
  _M0L6_2atmpS1683 = _M0L6_2atmpS1684 >> (_M0L6_2atmpS1685 & 31);
  _M0L6_2atmpS1682 = _M0L6_2atmpS1683 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1681 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1682);
  _M0L1aS616 = _M0L6_2atmpS1680 + _M0L6_2atmpS1681;
  _M0L6_2atmpS1679 = _M0Lm5high1S614;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS617
  = _M0FPB13shiftright128(_M0L3sumS613, _M0L6_2atmpS1679, _M0L5deltaS615);
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
  uint64_t _M0L6_2atmpS1677;
  uint64_t _M0L2hiS582;
  uint64_t _M0L3lo2S583;
  uint64_t _M0L6_2atmpS1675;
  uint64_t _M0L6_2atmpS1676;
  uint64_t _M0L4mid2S584;
  uint64_t _M0L6_2atmpS1674;
  uint64_t _M0L3hi2S585;
  int32_t _M0L6_2atmpS1673;
  int32_t _M0L6_2atmpS1672;
  uint64_t _M0L2vpS586;
  uint64_t _M0Lm2vmS588;
  int32_t _M0L6_2atmpS1671;
  int32_t _M0L6_2atmpS1670;
  uint64_t _M0L2vrS599;
  uint64_t _M0L6_2atmpS1669;
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
    _M0L6_2atmpS1677 = 1ull;
  } else {
    _M0L6_2atmpS1677 = 0ull;
  }
  _M0L2hiS582 = _M0L6_2ahi2S580 + _M0L6_2atmpS1677;
  _M0L3lo2S583 = _M0L5_2aloS576 + _M0L7_2amul0S570;
  _M0L6_2atmpS1675 = _M0L3midS581 + _M0L7_2amul1S572;
  if (_M0L3lo2S583 < _M0L5_2aloS576) {
    _M0L6_2atmpS1676 = 1ull;
  } else {
    _M0L6_2atmpS1676 = 0ull;
  }
  _M0L4mid2S584 = _M0L6_2atmpS1675 + _M0L6_2atmpS1676;
  if (_M0L4mid2S584 < _M0L3midS581) {
    _M0L6_2atmpS1674 = 1ull;
  } else {
    _M0L6_2atmpS1674 = 0ull;
  }
  _M0L3hi2S585 = _M0L2hiS582 + _M0L6_2atmpS1674;
  _M0L6_2atmpS1673 = _M0L1jS587 - 64;
  _M0L6_2atmpS1672 = _M0L6_2atmpS1673 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS586
  = _M0FPB13shiftright128(_M0L4mid2S584, _M0L3hi2S585, _M0L6_2atmpS1672);
  _M0Lm2vmS588 = 0ull;
  if (_M0L7mmShiftS589) {
    uint64_t _M0L3lo3S590 = _M0L5_2aloS576 - _M0L7_2amul0S570;
    uint64_t _M0L6_2atmpS1659 = _M0L3midS581 - _M0L7_2amul1S572;
    uint64_t _M0L6_2atmpS1660;
    uint64_t _M0L4mid3S591;
    uint64_t _M0L6_2atmpS1658;
    uint64_t _M0L3hi3S592;
    int32_t _M0L6_2atmpS1657;
    int32_t _M0L6_2atmpS1656;
    if (_M0L5_2aloS576 < _M0L3lo3S590) {
      _M0L6_2atmpS1660 = 1ull;
    } else {
      _M0L6_2atmpS1660 = 0ull;
    }
    _M0L4mid3S591 = _M0L6_2atmpS1659 - _M0L6_2atmpS1660;
    if (_M0L3midS581 < _M0L4mid3S591) {
      _M0L6_2atmpS1658 = 1ull;
    } else {
      _M0L6_2atmpS1658 = 0ull;
    }
    _M0L3hi3S592 = _M0L2hiS582 - _M0L6_2atmpS1658;
    _M0L6_2atmpS1657 = _M0L1jS587 - 64;
    _M0L6_2atmpS1656 = _M0L6_2atmpS1657 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS588
    = _M0FPB13shiftright128(_M0L4mid3S591, _M0L3hi3S592, _M0L6_2atmpS1656);
  } else {
    uint64_t _M0L3lo3S593 = _M0L5_2aloS576 + _M0L5_2aloS576;
    uint64_t _M0L6_2atmpS1667 = _M0L3midS581 + _M0L3midS581;
    uint64_t _M0L6_2atmpS1668;
    uint64_t _M0L4mid3S594;
    uint64_t _M0L6_2atmpS1665;
    uint64_t _M0L6_2atmpS1666;
    uint64_t _M0L3hi3S595;
    uint64_t _M0L3lo4S596;
    uint64_t _M0L6_2atmpS1663;
    uint64_t _M0L6_2atmpS1664;
    uint64_t _M0L4mid4S597;
    uint64_t _M0L6_2atmpS1662;
    uint64_t _M0L3hi4S598;
    int32_t _M0L6_2atmpS1661;
    if (_M0L3lo3S593 < _M0L5_2aloS576) {
      _M0L6_2atmpS1668 = 1ull;
    } else {
      _M0L6_2atmpS1668 = 0ull;
    }
    _M0L4mid3S594 = _M0L6_2atmpS1667 + _M0L6_2atmpS1668;
    _M0L6_2atmpS1665 = _M0L2hiS582 + _M0L2hiS582;
    if (_M0L4mid3S594 < _M0L3midS581) {
      _M0L6_2atmpS1666 = 1ull;
    } else {
      _M0L6_2atmpS1666 = 0ull;
    }
    _M0L3hi3S595 = _M0L6_2atmpS1665 + _M0L6_2atmpS1666;
    _M0L3lo4S596 = _M0L3lo3S593 - _M0L7_2amul0S570;
    _M0L6_2atmpS1663 = _M0L4mid3S594 - _M0L7_2amul1S572;
    if (_M0L3lo3S593 < _M0L3lo4S596) {
      _M0L6_2atmpS1664 = 1ull;
    } else {
      _M0L6_2atmpS1664 = 0ull;
    }
    _M0L4mid4S597 = _M0L6_2atmpS1663 - _M0L6_2atmpS1664;
    if (_M0L4mid3S594 < _M0L4mid4S597) {
      _M0L6_2atmpS1662 = 1ull;
    } else {
      _M0L6_2atmpS1662 = 0ull;
    }
    _M0L3hi4S598 = _M0L3hi3S595 - _M0L6_2atmpS1662;
    _M0L6_2atmpS1661 = _M0L1jS587 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS588
    = _M0FPB13shiftright128(_M0L4mid4S597, _M0L3hi4S598, _M0L6_2atmpS1661);
  }
  _M0L6_2atmpS1671 = _M0L1jS587 - 64;
  _M0L6_2atmpS1670 = _M0L6_2atmpS1671 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS599
  = _M0FPB13shiftright128(_M0L3midS581, _M0L2hiS582, _M0L6_2atmpS1670);
  _M0L6_2atmpS1669 = _M0Lm2vmS588;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS599,
                                                .$1 = _M0L2vpS586,
                                                .$2 = _M0L6_2atmpS1669};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS568,
  int32_t _M0L1pS569
) {
  uint64_t _M0L6_2atmpS1655;
  uint64_t _M0L6_2atmpS1654;
  uint64_t _M0L6_2atmpS1653;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1655 = 1ull << (_M0L1pS569 & 63);
  _M0L6_2atmpS1654 = _M0L6_2atmpS1655 - 1ull;
  _M0L6_2atmpS1653 = _M0L5valueS568 & _M0L6_2atmpS1654;
  return _M0L6_2atmpS1653 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS566,
  int32_t _M0L1pS567
) {
  int32_t _M0L6_2atmpS1652;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1652 = _M0FPB10pow5Factor(_M0L5valueS566);
  return _M0L6_2atmpS1652 >= _M0L1pS567;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS561) {
  uint64_t _M0L6_2atmpS1643;
  uint64_t _M0L6_2atmpS1644;
  uint64_t _M0L6_2atmpS1645;
  uint64_t _M0L6_2atmpS1646;
  uint64_t _M0L6_2atmpS1651;
  int32_t _M0L5countS562;
  uint64_t _M0L1vS563;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1643 = _M0L5valueS561 % 5ull;
  if (_M0L6_2atmpS1643 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1644 = _M0L5valueS561 % 25ull;
  if (_M0L6_2atmpS1644 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1645 = _M0L5valueS561 % 125ull;
  if (_M0L6_2atmpS1645 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1646 = _M0L5valueS561 % 625ull;
  if (_M0L6_2atmpS1646 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1651 = _M0L5valueS561 / 625ull;
  _M0L5countS562 = 4;
  _M0L1vS563 = _M0L6_2atmpS1651;
  while (1) {
    if (_M0L1vS563 > 0ull) {
      uint64_t _M0L6_2atmpS1647 = _M0L1vS563 % 5ull;
      int32_t _M0L6_2atmpS1648;
      uint64_t _M0L6_2atmpS1649;
      if (_M0L6_2atmpS1647 != 0ull) {
        return _M0L5countS562;
      }
      _M0L6_2atmpS1648 = _M0L5countS562 + 1;
      _M0L6_2atmpS1649 = _M0L1vS563 / 5ull;
      _M0L5countS562 = _M0L6_2atmpS1648;
      _M0L1vS563 = _M0L6_2atmpS1649;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS565;
      moonbit_string_t _M0L6_2atmpS1650;
      int32_t _result_2311;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS565
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS565, (moonbit_string_t)moonbit_string_literal_10.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS565, _M0L5valueS561);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1650
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS565);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS565);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2311 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1650);
      moonbit_decref_cycle_free(_M0L6_2atmpS1650);
      return _result_2311;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS560,
  uint64_t _M0L2hiS558,
  int32_t _M0L4distS559
) {
  int32_t _M0L6_2atmpS1642;
  uint64_t _M0L6_2atmpS1640;
  uint64_t _M0L6_2atmpS1641;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1642 = 64 - _M0L4distS559;
  _M0L6_2atmpS1640 = _M0L2hiS558 << (_M0L6_2atmpS1642 & 63);
  _M0L6_2atmpS1641 = _M0L2loS560 >> (_M0L4distS559 & 63);
  return _M0L6_2atmpS1640 | _M0L6_2atmpS1641;
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
  uint64_t _M0L6_2atmpS1638;
  uint64_t _M0L6_2atmpS1639;
  uint64_t _M0L1yS554;
  uint64_t _M0L6_2atmpS1636;
  uint64_t _M0L6_2atmpS1637;
  uint64_t _M0L1zS555;
  uint64_t _M0L6_2atmpS1634;
  uint64_t _M0L6_2atmpS1635;
  uint64_t _M0L6_2atmpS1632;
  uint64_t _M0L6_2atmpS1633;
  uint64_t _M0L1wS556;
  uint64_t _M0L2loS557;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS547 = _M0L1aS548 & 4294967295ull;
  _M0L3aHiS549 = _M0L1aS548 >> 32;
  _M0L3bLoS550 = _M0L1bS551 & 4294967295ull;
  _M0L3bHiS552 = _M0L1bS551 >> 32;
  _M0L1xS553 = _M0L3aLoS547 * _M0L3bLoS550;
  _M0L6_2atmpS1638 = _M0L3aHiS549 * _M0L3bLoS550;
  _M0L6_2atmpS1639 = _M0L1xS553 >> 32;
  _M0L1yS554 = _M0L6_2atmpS1638 + _M0L6_2atmpS1639;
  _M0L6_2atmpS1636 = _M0L3aLoS547 * _M0L3bHiS552;
  _M0L6_2atmpS1637 = _M0L1yS554 & 4294967295ull;
  _M0L1zS555 = _M0L6_2atmpS1636 + _M0L6_2atmpS1637;
  _M0L6_2atmpS1634 = _M0L3aHiS549 * _M0L3bHiS552;
  _M0L6_2atmpS1635 = _M0L1yS554 >> 32;
  _M0L6_2atmpS1632 = _M0L6_2atmpS1634 + _M0L6_2atmpS1635;
  _M0L6_2atmpS1633 = _M0L1zS555 >> 32;
  _M0L1wS556 = _M0L6_2atmpS1632 + _M0L6_2atmpS1633;
  _M0L2loS557 = _M0L1aS548 * _M0L1bS551;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS557, .$1 = _M0L1wS556};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS545,
  int32_t _M0L4fromS542,
  int32_t _M0L2toS541
) {
  int32_t _M0L3lenS540;
  int32_t _M0L6_2atmpS1631;
  uint16_t* _M0L6bufferS543;
  int32_t _M0L1iS544;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS540 = _M0L2toS541 - _M0L4fromS542;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1631 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS543
  = (uint16_t*)moonbit_make_string(_M0L3lenS540, _M0L6_2atmpS1631);
  _M0L1iS544 = 0;
  while (1) {
    if (_M0L1iS544 < _M0L3lenS540) {
      int32_t _M0L6_2atmpS1629 = _M0L4fromS542 + _M0L1iS544;
      int32_t _M0L6_2atmpS1628;
      int32_t _M0L6_2atmpS1627;
      int32_t _M0L6_2atmpS1630;
      if (
        _M0L6_2atmpS1629 < 0
        || _M0L6_2atmpS1629 >= Moonbit_array_length(_M0L5bytesS545)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1628 = (int32_t)_M0L5bytesS545[_M0L6_2atmpS1629];
      _M0L6_2atmpS1627 = (uint16_t)_M0L6_2atmpS1628;
      if (
        _M0L1iS544 < 0 || _M0L1iS544 >= Moonbit_array_length(_M0L6bufferS543)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS543[_M0L1iS544] = _M0L6_2atmpS1627;
      _M0L6_2atmpS1630 = _M0L1iS544 + 1;
      _M0L1iS544 = _M0L6_2atmpS1630;
      continue;
    }
    break;
  }
  return _M0L6bufferS543;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS539) {
  int32_t _M0L6_2atmpS1626;
  uint32_t _M0L6_2atmpS1625;
  uint32_t _M0L6_2atmpS1624;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1626 = _M0L1eS539 * 78913;
  _M0L6_2atmpS1625 = *(uint32_t*)&_M0L6_2atmpS1626;
  _M0L6_2atmpS1624 = _M0L6_2atmpS1625 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1624;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS538) {
  int32_t _M0L6_2atmpS1623;
  uint32_t _M0L6_2atmpS1622;
  uint32_t _M0L6_2atmpS1621;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1623 = _M0L1eS538 * 732923;
  _M0L6_2atmpS1622 = *(uint32_t*)&_M0L6_2atmpS1623;
  _M0L6_2atmpS1621 = _M0L6_2atmpS1622 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1621;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS536,
  int32_t _M0L8exponentS537,
  int32_t _M0L8mantissaS534
) {
  moonbit_string_t _M0L1sS535;
  moonbit_string_t _result_2314;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS534) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  if (_M0L4signS536) {
    _M0L1sS535 = (moonbit_string_t)moonbit_string_literal_12.data;
  } else {
    _M0L1sS535 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS537) {
    moonbit_string_t _result_2313;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2313
    = moonbit_add_string(_M0L1sS535, (moonbit_string_t)moonbit_string_literal_13.data);
    moonbit_decref_cycle_free(_M0L1sS535);
    return _result_2313;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2314
  = moonbit_add_string(_M0L1sS535, (moonbit_string_t)moonbit_string_literal_14.data);
  moonbit_decref_cycle_free(_M0L1sS535);
  return _result_2314;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS533) {
  int32_t _M0L6_2atmpS1620;
  uint32_t _M0L6_2atmpS1619;
  uint32_t _M0L6_2atmpS1618;
  int32_t _M0L6_2atmpS1617;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1620 = _M0L1eS533 * 1217359;
  _M0L6_2atmpS1619 = *(uint32_t*)&_M0L6_2atmpS1620;
  _M0L6_2atmpS1618 = _M0L6_2atmpS1619 >> 19;
  _M0L6_2atmpS1617 = *(int32_t*)&_M0L6_2atmpS1618;
  return _M0L6_2atmpS1617 + 1;
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
  float* _M0L6_2atmpS1614;
  struct _M0TPB5ArrayGfE* _block_2315;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1614 = (float*)moonbit_make_float_array_raw(_M0L3lenS528);
  _block_2315
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2315)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 30, 0);
  _block_2315->$0 = _M0L6_2atmpS1614;
  _block_2315->$1 = _M0L3lenS528;
  return _block_2315;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS529
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS1615;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_2316;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1615
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS529, 0);
  _block_2316
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_2316)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 41, 0);
  _block_2316->$0 = _M0L6_2atmpS1615;
  _block_2316->$1 = _M0L3lenS529;
  return _block_2316;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS530
) {
  int32_t* _M0L6_2atmpS1616;
  struct _M0TPB5ArrayGiE* _block_2317;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1616 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS530);
  _block_2317
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2317)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 33, 0);
  _block_2317->$0 = _M0L6_2atmpS1616;
  _block_2317->$1 = _M0L3lenS530;
  return _block_2317;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS524,
  int32_t _M0L5indexS525
) {
  uint64_t* _M0L6_2atmpS1612;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1612 = _M0L4selfS524;
  if (
    _M0L5indexS525 < 0
    || _M0L5indexS525 >= Moonbit_array_length(_M0L6_2atmpS1612)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1612[_M0L5indexS525];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS526,
  int32_t _M0L5indexS527
) {
  uint32_t* _M0L6_2atmpS1613;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1613 = _M0L4selfS526;
  if (
    _M0L5indexS527 < 0
    || _M0L5indexS527 >= Moonbit_array_length(_M0L6_2atmpS1613)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1613[_M0L5indexS527];
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

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS509,
  moonbit_string_t _M0L5valueS511
) {
  int32_t _M0L3lenS1584;
  moonbit_string_t* _M0L6_2atmpS1586;
  int32_t _M0L6_2atmpS1585;
  int32_t _M0L6lengthS510;
  moonbit_string_t* _M0L3bufS1589;
  moonbit_string_t _M0L6_2aoldS2203;
  int32_t _M0L6_2atmpS1590;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1584 = _M0L4selfS509->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1586 = _M0MPC15array5Array6bufferGsE(_M0L4selfS509);
  _M0L6_2atmpS1585 = Moonbit_array_length(_M0L6_2atmpS1586);
  moonbit_decref_cycle_free(_M0L6_2atmpS1586);
  if (_M0L3lenS1584 == _M0L6_2atmpS1585) {
    int32_t _M0L3lenS1588 = _M0L4selfS509->$1;
    int32_t _M0L6_2atmpS1587 = _M0L3lenS1588 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS509, _M0L6_2atmpS1587);
  }
  _M0L6lengthS510 = _M0L4selfS509->$1;
  _M0L3bufS1589 = _M0L4selfS509->$0;
  _M0L6_2aoldS2203 = (moonbit_string_t)_M0L3bufS1589[_M0L6lengthS510];
  moonbit_decref_cycle_free(_M0L6_2aoldS2203);
  _M0L3bufS1589[_M0L6lengthS510] = _M0L5valueS511;
  _M0L6_2atmpS1590 = _M0L6lengthS510 + 1;
  _M0L4selfS509->$1 = _M0L6_2atmpS1590;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS512,
  struct _M0TUsiE* _M0L5valueS514
) {
  int32_t _M0L3lenS1591;
  struct _M0TUsiE** _M0L6_2atmpS1593;
  int32_t _M0L6_2atmpS1592;
  int32_t _M0L6lengthS513;
  struct _M0TUsiE** _M0L3bufS1596;
  struct _M0TUsiE* _M0L6_2aoldS2204;
  int32_t _M0L6_2atmpS1597;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1591 = _M0L4selfS512->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1593 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS512);
  _M0L6_2atmpS1592 = Moonbit_array_length(_M0L6_2atmpS1593);
  moonbit_decref_cycle_free(_M0L6_2atmpS1593);
  if (_M0L3lenS1591 == _M0L6_2atmpS1592) {
    int32_t _M0L3lenS1595 = _M0L4selfS512->$1;
    int32_t _M0L6_2atmpS1594 = _M0L3lenS1595 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS512, _M0L6_2atmpS1594);
  }
  _M0L6lengthS513 = _M0L4selfS512->$1;
  _M0L3bufS1596 = _M0L4selfS512->$0;
  _M0L6_2aoldS2204 = (struct _M0TUsiE*)_M0L3bufS1596[_M0L6lengthS513];
  if (_M0L6_2aoldS2204) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2204);
  }
  _M0L3bufS1596[_M0L6lengthS513] = _M0L5valueS514;
  _M0L6_2atmpS1597 = _M0L6lengthS513 + 1;
  _M0L4selfS512->$1 = _M0L6_2atmpS1597;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS515,
  int32_t _M0L5valueS517
) {
  int32_t _M0L3lenS1598;
  int32_t* _M0L6_2atmpS1600;
  int32_t _M0L6_2atmpS1599;
  int32_t _M0L6lengthS516;
  int32_t* _M0L3bufS1603;
  int32_t _M0L6_2atmpS1604;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1598 = _M0L4selfS515->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1600 = _M0MPC15array5Array6bufferGiE(_M0L4selfS515);
  _M0L6_2atmpS1599 = Moonbit_array_length(_M0L6_2atmpS1600);
  moonbit_decref_cycle_free(_M0L6_2atmpS1600);
  if (_M0L3lenS1598 == _M0L6_2atmpS1599) {
    int32_t _M0L3lenS1602 = _M0L4selfS515->$1;
    int32_t _M0L6_2atmpS1601 = _M0L3lenS1602 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS515, _M0L6_2atmpS1601);
  }
  _M0L6lengthS516 = _M0L4selfS515->$1;
  _M0L3bufS1603 = _M0L4selfS515->$0;
  _M0L3bufS1603[_M0L6lengthS516] = _M0L5valueS517;
  _M0L6_2atmpS1604 = _M0L6lengthS516 + 1;
  _M0L4selfS515->$1 = _M0L6_2atmpS1604;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS518,
  float _M0L5valueS520
) {
  int32_t _M0L3lenS1605;
  float* _M0L6_2atmpS1607;
  int32_t _M0L6_2atmpS1606;
  int32_t _M0L6lengthS519;
  float* _M0L3bufS1610;
  int32_t _M0L6_2atmpS1611;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1605 = _M0L4selfS518->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1607 = _M0MPC15array5Array6bufferGfE(_M0L4selfS518);
  _M0L6_2atmpS1606 = Moonbit_array_length(_M0L6_2atmpS1607);
  moonbit_decref_cycle_free(_M0L6_2atmpS1607);
  if (_M0L3lenS1605 == _M0L6_2atmpS1606) {
    int32_t _M0L3lenS1609 = _M0L4selfS518->$1;
    int32_t _M0L6_2atmpS1608 = _M0L3lenS1609 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS518, _M0L6_2atmpS1608);
  }
  _M0L6lengthS519 = _M0L4selfS518->$1;
  _M0L3bufS1610 = _M0L4selfS518->$0;
  _M0L3bufS1610[_M0L6lengthS519] = _M0L5valueS520;
  _M0L6_2atmpS1611 = _M0L6lengthS519 + 1;
  _M0L4selfS518->$1 = _M0L6_2atmpS1611;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS494,
  int32_t _M0L8requiredS496
) {
  int32_t _M0L8old__capS493;
  int32_t _M0L3lenS1580;
  int32_t _M0L8new__capS495;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS493 = _M0MPC15array5Array8capacityGsE(_M0L4selfS494);
  _M0L3lenS1580 = _M0L4selfS494->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS495
  = _M0FPB23array__growth__capacity(_M0L8old__capS493, _M0L3lenS1580, _M0L8requiredS496);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS494, _M0L8new__capS495);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS498,
  int32_t _M0L8requiredS500
) {
  int32_t _M0L8old__capS497;
  int32_t _M0L3lenS1581;
  int32_t _M0L8new__capS499;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS497 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS498);
  _M0L3lenS1581 = _M0L4selfS498->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS499
  = _M0FPB23array__growth__capacity(_M0L8old__capS497, _M0L3lenS1581, _M0L8requiredS500);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS498, _M0L8new__capS499);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS502,
  int32_t _M0L8requiredS504
) {
  int32_t _M0L8old__capS501;
  int32_t _M0L3lenS1582;
  int32_t _M0L8new__capS503;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS501 = _M0MPC15array5Array8capacityGiE(_M0L4selfS502);
  _M0L3lenS1582 = _M0L4selfS502->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS503
  = _M0FPB23array__growth__capacity(_M0L8old__capS501, _M0L3lenS1582, _M0L8requiredS504);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS502, _M0L8new__capS503);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS506,
  int32_t _M0L8requiredS508
) {
  int32_t _M0L8old__capS505;
  int32_t _M0L3lenS1583;
  int32_t _M0L8new__capS507;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS505 = _M0MPC15array5Array8capacityGfE(_M0L4selfS506);
  _M0L3lenS1583 = _M0L4selfS506->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS507
  = _M0FPB23array__growth__capacity(_M0L8old__capS505, _M0L3lenS1583, _M0L8requiredS508);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS506, _M0L8new__capS507);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS470,
  int32_t _M0L13new__capacityS473
) {
  moonbit_string_t* _M0L8old__bufS469;
  int32_t _M0L3lenS471;
  int32_t _M0L9copy__lenS472;
  moonbit_string_t* _M0L8new__bufS474;
  moonbit_string_t* _M0L6_2aoldS2205;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS469, _M0L13new__capacityS473, _M0L9copy__lenS472, 0, 0);
  _M0L6_2aoldS2205 = _M0L4selfS470->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2205);
  _M0L4selfS470->$0 = _M0L8new__bufS474;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS476,
  int32_t _M0L13new__capacityS479
) {
  struct _M0TUsiE** _M0L8old__bufS475;
  int32_t _M0L3lenS477;
  int32_t _M0L9copy__lenS478;
  struct _M0TUsiE** _M0L8new__bufS480;
  struct _M0TUsiE** _M0L6_2aoldS2206;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS475, _M0L13new__capacityS479, _M0L9copy__lenS478, 0, 0);
  _M0L6_2aoldS2206 = _M0L4selfS476->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2206);
  _M0L4selfS476->$0 = _M0L8new__bufS480;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS482,
  int32_t _M0L13new__capacityS485
) {
  int32_t* _M0L8old__bufS481;
  int32_t _M0L3lenS483;
  int32_t _M0L9copy__lenS484;
  int32_t* _M0L8new__bufS486;
  int32_t* _M0L6_2aoldS2207;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS481, _M0L13new__capacityS485, _M0L9copy__lenS484, 0, 0);
  _M0L6_2aoldS2207 = _M0L4selfS482->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2207);
  _M0L4selfS482->$0 = _M0L8new__bufS486;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS488,
  int32_t _M0L13new__capacityS491
) {
  float* _M0L8old__bufS487;
  int32_t _M0L3lenS489;
  int32_t _M0L9copy__lenS490;
  float* _M0L8new__bufS492;
  float* _M0L6_2aoldS2208;
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
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS487, _M0L13new__capacityS491, _M0L9copy__lenS490, 0, 0);
  _M0L6_2aoldS2208 = _M0L4selfS488->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2208);
  _M0L4selfS488->$0 = _M0L8new__bufS492;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS465
) {
  moonbit_string_t* _M0L6_2atmpS1576;
  int32_t _result_2318;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1576 = _M0MPC15array5Array6bufferGsE(_M0L4selfS465);
  _result_2318 = Moonbit_array_length(_M0L6_2atmpS1576);
  moonbit_decref_cycle_free(_M0L6_2atmpS1576);
  return _result_2318;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS466
) {
  struct _M0TUsiE** _M0L6_2atmpS1577;
  int32_t _result_2319;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1577 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS466);
  _result_2319 = Moonbit_array_length(_M0L6_2atmpS1577);
  moonbit_decref_cycle_free(_M0L6_2atmpS1577);
  return _result_2319;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS467
) {
  int32_t* _M0L6_2atmpS1578;
  int32_t _result_2320;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1578 = _M0MPC15array5Array6bufferGiE(_M0L4selfS467);
  _result_2320 = Moonbit_array_length(_M0L6_2atmpS1578);
  moonbit_decref_cycle_free(_M0L6_2atmpS1578);
  return _result_2320;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS468
) {
  float* _M0L6_2atmpS1579;
  int32_t _result_2321;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1579 = _M0MPC15array5Array6bufferGfE(_M0L4selfS468);
  _result_2321 = Moonbit_array_length(_M0L6_2atmpS1579);
  moonbit_decref_cycle_free(_M0L6_2atmpS1579);
  return _result_2321;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_15.data);
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
  float* _M0L8_2afieldS2209;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2209 = _M0L4selfS452->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2209);
  return _M0L8_2afieldS2209;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS453) {
  int32_t* _M0L8_2afieldS2210;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2210 = _M0L4selfS453->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2210);
  return _M0L8_2afieldS2210;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS454
) {
  moonbit_string_t* _M0L8_2afieldS2211;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2211 = _M0L4selfS454->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2211);
  return _M0L8_2afieldS2211;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS455
) {
  struct _M0TUsiE** _M0L8_2afieldS2212;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2212 = _M0L4selfS455->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2212);
  return _M0L8_2afieldS2212;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS456
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS2213;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2213 = _M0L4selfS456->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2213);
  return _M0L8_2afieldS2213;
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
  int32_t _M0L3endS1574;
  int32_t _M0L5startS1575;
  int32_t _M0L8str__lenS447;
  int32_t _M0L3lenS1573;
  int32_t _M0L8requiredS449;
  uint16_t* _M0L4dataS1566;
  int32_t _M0L6_2atmpS1565;
  int32_t _if__result_2323;
  uint16_t* _M0L4dataS1567;
  int32_t _M0L3lenS1568;
  moonbit_string_t _M0L6_2atmpS1569;
  int32_t _M0L6_2atmpS1570;
  int32_t _M0L3lenS1572;
  int32_t _M0L6_2atmpS1571;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1574 = _M0L3strS448.$2;
  _M0L5startS1575 = _M0L3strS448.$1;
  _M0L8str__lenS447 = _M0L3endS1574 - _M0L5startS1575;
  if (_M0L8str__lenS447 == 0) {
    return 0;
  }
  _M0L3lenS1573 = _M0L4selfS450->$1;
  _M0L8requiredS449 = _M0L3lenS1573 + _M0L8str__lenS447;
  _M0L4dataS1566 = _M0L4selfS450->$0;
  _M0L6_2atmpS1565 = Moonbit_array_length(_M0L4dataS1566);
  if (_M0L8requiredS449 > _M0L6_2atmpS1565) {
    _if__result_2323 = 1;
  } else {
    int32_t _M0L3lenS1564 = _M0L4selfS450->$1;
    _if__result_2323 = _M0L8requiredS449 < _M0L3lenS1564;
  }
  if (_if__result_2323) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS450, _M0L8requiredS449);
  }
  _M0L4dataS1567 = _M0L4selfS450->$0;
  _M0L3lenS1568 = _M0L4selfS450->$1;
  moonbit_incref_cycle_free(_M0L4dataS1567);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1569 = _M0MPC16string10StringView4data(_M0L3strS448);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1570 = _M0MPC16string10StringView13start__offset(_M0L3strS448);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1567, _M0L3lenS1568, _M0L6_2atmpS1569, _M0L6_2atmpS1570, _M0L8str__lenS447);
  moonbit_decref_cycle_free(_M0L4dataS1567);
  moonbit_decref_cycle_free(_M0L6_2atmpS1569);
  _M0L3lenS1572 = _M0L4selfS450->$1;
  _M0L6_2atmpS1571 = _M0L3lenS1572 + _M0L8str__lenS447;
  _M0L4selfS450->$1 = _M0L6_2atmpS1571;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS444,
  int32_t _M0L5startS442,
  int32_t _M0L3endS443
) {
  int32_t _if__result_2324;
  int32_t _M0L3lenS445;
  int32_t _M0L6_2atmpS1563;
  moonbit_bytes_t _M0L5bytesS446;
  moonbit_bytes_t _M0L6_2atmpS1562;
  moonbit_string_t _result_2325;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS442 == 0) {
    int32_t _M0L6_2atmpS1561 = Moonbit_array_length(_M0L3strS444);
    _if__result_2324 = _M0L3endS443 == _M0L6_2atmpS1561;
  } else {
    _if__result_2324 = 0;
  }
  if (_if__result_2324) {
    moonbit_incref_cycle_free(_M0L3strS444);
    return _M0L3strS444;
  }
  _M0L3lenS445 = _M0L3endS443 - _M0L5startS442;
  _M0L6_2atmpS1563 = _M0L3lenS445 * 2;
  _M0L5bytesS446 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1563, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS446, 0, _M0L3strS444, _M0L5startS442, _M0L3lenS445);
  _M0L6_2atmpS1562 = _M0L5bytesS446;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2325
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1562, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1562);
  return _result_2325;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS437,
  int32_t _M0L6offsetS441,
  int64_t _M0L6lengthS439
) {
  int32_t _M0L3lenS436;
  int32_t _M0L6lengthS438;
  int32_t _if__result_2326;
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
      int32_t _M0L6_2atmpS1560 = _M0L6offsetS441 + _M0L6lengthS438;
      _if__result_2326 = _M0L6_2atmpS1560 <= _M0L3lenS436;
    } else {
      _if__result_2326 = 0;
    }
  } else {
    _if__result_2326 = 0;
  }
  if (_if__result_2326) {
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
  int32_t _M0L6_2atmpS1559;
  int32_t _M0L6_2atmpS1558;
  int32_t _M0L2e1S422;
  int32_t _M0L6_2atmpS1557;
  int32_t _M0L2e2S425;
  int32_t _M0L4len1S427;
  int32_t _M0L4len2S429;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1559 = _M0L6lengthS424 * 2;
  _M0L6_2atmpS1558 = _M0L13bytes__offsetS423 + _M0L6_2atmpS1559;
  _M0L2e1S422 = _M0L6_2atmpS1558 - 1;
  _M0L6_2atmpS1557 = _M0L11str__offsetS426 + _M0L6lengthS424;
  _M0L2e2S425 = _M0L6_2atmpS1557 - 1;
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
        int32_t _M0L6_2atmpS1554 = _M0L3strS430[_M0L1iS432];
        int32_t _M0L6_2atmpS1553 = (int32_t)_M0L6_2atmpS1554;
        uint32_t _M0L1cS434 = *(uint32_t*)&_M0L6_2atmpS1553;
        uint32_t _M0L6_2atmpS1549 = _M0L1cS434 & 255u;
        int32_t _M0L6_2atmpS1548;
        int32_t _M0L6_2atmpS1550;
        uint32_t _M0L6_2atmpS1552;
        int32_t _M0L6_2atmpS1551;
        int32_t _M0L6_2atmpS1555;
        int32_t _M0L6_2atmpS1556;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1548 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1549);
        if (
          _M0L1jS433 < 0 || _M0L1jS433 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L1jS433] = _M0L6_2atmpS1548;
        _M0L6_2atmpS1550 = _M0L1jS433 + 1;
        _M0L6_2atmpS1552 = _M0L1cS434 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1551 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1552);
        if (
          _M0L6_2atmpS1550 < 0
          || _M0L6_2atmpS1550 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L6_2atmpS1550] = _M0L6_2atmpS1551;
        _M0L6_2atmpS1555 = _M0L1iS432 + 1;
        _M0L6_2atmpS1556 = _M0L1jS433 + 2;
        _M0L1iS432 = _M0L6_2atmpS1555;
        _M0L1jS433 = _M0L6_2atmpS1556;
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
  int32_t _M0L6_2atmpS1547;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1547 = *(int32_t*)&_M0L4selfS421;
  return _M0L6_2atmpS1547 & 0xff;
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
    int64_t _M0L6_2atmpS1546 = -_M0L4selfS396;
    _M0L3numS398 = *(uint64_t*)&_M0L6_2atmpS1546;
  } else {
    _M0L3numS398 = *(uint64_t*)&_M0L4selfS396;
  }
  switch (_M0L5radixS395) {
    case 10: {
      int32_t _M0L10digit__lenS400;
      int32_t _M0L6_2atmpS1543;
      int32_t _M0L10total__lenS401;
      uint16_t* _M0L6bufferS402;
      int32_t _M0L12digit__startS403;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS400 = _M0FPB12dec__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1543 = 1;
      } else {
        _M0L6_2atmpS1543 = 0;
      }
      _M0L10total__lenS401 = _M0L10digit__lenS400 + _M0L6_2atmpS1543;
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
      int32_t _M0L6_2atmpS1544;
      int32_t _M0L10total__lenS405;
      uint16_t* _M0L6bufferS406;
      int32_t _M0L12digit__startS407;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS404 = _M0FPB12hex__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1544 = 1;
      } else {
        _M0L6_2atmpS1544 = 0;
      }
      _M0L10total__lenS405 = _M0L10digit__lenS404 + _M0L6_2atmpS1544;
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
      int32_t _M0L6_2atmpS1545;
      int32_t _M0L10total__lenS409;
      uint16_t* _M0L6bufferS410;
      int32_t _M0L12digit__startS411;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS408
      = _M0FPB14radix__count64(_M0L3numS398, _M0L5radixS395);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1545 = 1;
      } else {
        _M0L6_2atmpS1545 = 0;
      }
      _M0L10total__lenS409 = _M0L10digit__lenS408 + _M0L6_2atmpS1545;
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
  int32_t _M0L6_2atmpS1542;
  uint64_t _M0L3numS371;
  int32_t _M0L6offsetS372;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1542 = _M0L10total__lenS394 - _M0L12digit__startS382;
  _M0L3numS371 = _M0L3numS393;
  _M0L6offsetS372 = _M0L6_2atmpS1542;
  while (1) {
    if (_M0L3numS371 >= 10000ull) {
      uint64_t _M0L1tS373 = _M0L3numS371 / 10000ull;
      uint64_t _M0L6_2atmpS1519 = _M0L3numS371 % 10000ull;
      int32_t _M0L1rS374 = (int32_t)_M0L6_2atmpS1519;
      int32_t _M0L2d1S375 = _M0L1rS374 / 100;
      int32_t _M0L2d2S376 = _M0L1rS374 % 100;
      int32_t _M0L6_2atmpS1518 = _M0L2d1S375 / 10;
      int32_t _M0L6_2atmpS1517 = 48 + _M0L6_2atmpS1518;
      int32_t _M0L6d1__hiS377 = (uint16_t)_M0L6_2atmpS1517;
      int32_t _M0L6_2atmpS1516 = _M0L2d1S375 % 10;
      int32_t _M0L6_2atmpS1515 = 48 + _M0L6_2atmpS1516;
      int32_t _M0L6d1__loS378 = (uint16_t)_M0L6_2atmpS1515;
      int32_t _M0L6_2atmpS1514 = _M0L2d2S376 / 10;
      int32_t _M0L6_2atmpS1513 = 48 + _M0L6_2atmpS1514;
      int32_t _M0L6d2__hiS379 = (uint16_t)_M0L6_2atmpS1513;
      int32_t _M0L6_2atmpS1512 = _M0L2d2S376 % 10;
      int32_t _M0L6_2atmpS1511 = 48 + _M0L6_2atmpS1512;
      int32_t _M0L6d2__loS380 = (uint16_t)_M0L6_2atmpS1511;
      int32_t _M0L6_2atmpS1503 = _M0L12digit__startS382 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS1502 = _M0L6_2atmpS1503 - 4;
      int32_t _M0L6_2atmpS1505;
      int32_t _M0L6_2atmpS1504;
      int32_t _M0L6_2atmpS1507;
      int32_t _M0L6_2atmpS1506;
      int32_t _M0L6_2atmpS1509;
      int32_t _M0L6_2atmpS1508;
      int32_t _M0L6_2atmpS1510;
      _M0L6bufferS381[_M0L6_2atmpS1502] = _M0L6d1__hiS377;
      _M0L6_2atmpS1505 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1504 = _M0L6_2atmpS1505 - 3;
      _M0L6bufferS381[_M0L6_2atmpS1504] = _M0L6d1__loS378;
      _M0L6_2atmpS1507 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1506 = _M0L6_2atmpS1507 - 2;
      _M0L6bufferS381[_M0L6_2atmpS1506] = _M0L6d2__hiS379;
      _M0L6_2atmpS1509 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1508 = _M0L6_2atmpS1509 - 1;
      _M0L6bufferS381[_M0L6_2atmpS1508] = _M0L6d2__loS380;
      _M0L6_2atmpS1510 = _M0L6offsetS372 - 4;
      _M0L3numS371 = _M0L1tS373;
      _M0L6offsetS372 = _M0L6_2atmpS1510;
      continue;
    } else {
      int32_t _M0L6_2atmpS1541 = (int32_t)_M0L3numS371;
      int32_t _M0L9remainingS384 = _M0L6_2atmpS1541;
      int32_t _M0L6offsetS385 = _M0L6offsetS372;
      while (1) {
        if (_M0L9remainingS384 >= 100) {
          int32_t _M0L1tS386 = _M0L9remainingS384 / 100;
          int32_t _M0L1dS387 = _M0L9remainingS384 % 100;
          int32_t _M0L6_2atmpS1528 = _M0L1dS387 / 10;
          int32_t _M0L6_2atmpS1527 = 48 + _M0L6_2atmpS1528;
          int32_t _M0L5d__hiS388 = (uint16_t)_M0L6_2atmpS1527;
          int32_t _M0L6_2atmpS1526 = _M0L1dS387 % 10;
          int32_t _M0L6_2atmpS1525 = 48 + _M0L6_2atmpS1526;
          int32_t _M0L5d__loS389 = (uint16_t)_M0L6_2atmpS1525;
          int32_t _M0L6_2atmpS1521 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1520 = _M0L6_2atmpS1521 - 2;
          int32_t _M0L6_2atmpS1523;
          int32_t _M0L6_2atmpS1522;
          int32_t _M0L6_2atmpS1524;
          _M0L6bufferS381[_M0L6_2atmpS1520] = _M0L5d__hiS388;
          _M0L6_2atmpS1523 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1522 = _M0L6_2atmpS1523 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1522] = _M0L5d__loS389;
          _M0L6_2atmpS1524 = _M0L6offsetS385 - 2;
          _M0L9remainingS384 = _M0L1tS386;
          _M0L6offsetS385 = _M0L6_2atmpS1524;
          continue;
        } else if (_M0L9remainingS384 >= 10) {
          int32_t _M0L6_2atmpS1536 = _M0L9remainingS384 / 10;
          int32_t _M0L6_2atmpS1535 = 48 + _M0L6_2atmpS1536;
          int32_t _M0L5d__hiS391 = (uint16_t)_M0L6_2atmpS1535;
          int32_t _M0L6_2atmpS1534 = _M0L9remainingS384 % 10;
          int32_t _M0L6_2atmpS1533 = 48 + _M0L6_2atmpS1534;
          int32_t _M0L5d__loS392 = (uint16_t)_M0L6_2atmpS1533;
          int32_t _M0L6_2atmpS1530 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1529 = _M0L6_2atmpS1530 - 2;
          int32_t _M0L6_2atmpS1532;
          int32_t _M0L6_2atmpS1531;
          _M0L6bufferS381[_M0L6_2atmpS1529] = _M0L5d__hiS391;
          _M0L6_2atmpS1532 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1531 = _M0L6_2atmpS1532 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1531] = _M0L5d__loS392;
        } else {
          int32_t _M0L6_2atmpS1540 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1537 = _M0L6_2atmpS1540 - 1;
          int32_t _M0L6_2atmpS1539 = 48 + _M0L9remainingS384;
          int32_t _M0L6_2atmpS1538 = (uint16_t)_M0L6_2atmpS1539;
          _M0L6bufferS381[_M0L6_2atmpS1537] = _M0L6_2atmpS1538;
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
  int32_t _M0L6_2atmpS1487;
  int32_t _M0L6_2atmpS1486;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS354 = _M0MPC13int3Int10to__uint64(_M0L5radixS355);
  _M0L6_2atmpS1487 = _M0L5radixS355 - 1;
  _M0L6_2atmpS1486 = _M0L5radixS355 & _M0L6_2atmpS1487;
  if (_M0L6_2atmpS1486 == 0) {
    int32_t _M0L5shiftS356;
    uint64_t _M0L4maskS357;
    int32_t _M0L6_2atmpS1494;
    int32_t _M0L6offsetS358;
    uint64_t _M0L1nS359;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS356 = moonbit_ctz32(_M0L5radixS355);
    _M0L4maskS357 = _M0L4baseS354 - 1ull;
    _M0L6_2atmpS1494 = _M0L10total__lenS364 - _M0L12digit__startS362;
    _M0L6offsetS358 = _M0L6_2atmpS1494;
    _M0L1nS359 = _M0L3numS365;
    while (1) {
      if (_M0L1nS359 > 0ull) {
        uint64_t _M0L6_2atmpS1493 = _M0L1nS359 & _M0L4maskS357;
        int32_t _M0L5digitS360 = (int32_t)_M0L6_2atmpS1493;
        int32_t _M0L6_2atmpS1490 = _M0L12digit__startS362 + _M0L6offsetS358;
        int32_t _M0L6_2atmpS1488 = _M0L6_2atmpS1490 - 1;
        int32_t _M0L6_2atmpS1489 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS360];
        int32_t _M0L6_2atmpS1491;
        uint64_t _M0L6_2atmpS1492;
        _M0L6bufferS361[_M0L6_2atmpS1488] = _M0L6_2atmpS1489;
        _M0L6_2atmpS1491 = _M0L6offsetS358 - 1;
        _M0L6_2atmpS1492 = _M0L1nS359 >> (_M0L5shiftS356 & 63);
        _M0L6offsetS358 = _M0L6_2atmpS1491;
        _M0L1nS359 = _M0L6_2atmpS1492;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1501 = _M0L10total__lenS364 - _M0L12digit__startS362;
    int32_t _M0L6offsetS366 = _M0L6_2atmpS1501;
    uint64_t _M0L1nS367 = _M0L3numS365;
    while (1) {
      if (_M0L1nS367 > 0ull) {
        uint64_t _M0L1qS368 = _M0L1nS367 / _M0L4baseS354;
        uint64_t _M0L6_2atmpS1500 = _M0L1qS368 * _M0L4baseS354;
        uint64_t _M0L6_2atmpS1499 = _M0L1nS367 - _M0L6_2atmpS1500;
        int32_t _M0L5digitS369 = (int32_t)_M0L6_2atmpS1499;
        int32_t _M0L6_2atmpS1497 = _M0L12digit__startS362 + _M0L6offsetS366;
        int32_t _M0L6_2atmpS1495 = _M0L6_2atmpS1497 - 1;
        int32_t _M0L6_2atmpS1496 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS369];
        int32_t _M0L6_2atmpS1498;
        _M0L6bufferS361[_M0L6_2atmpS1495] = _M0L6_2atmpS1496;
        _M0L6_2atmpS1498 = _M0L6offsetS366 - 1;
        _M0L6offsetS366 = _M0L6_2atmpS1498;
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
  int32_t _M0L6_2atmpS1485;
  int32_t _M0L6offsetS343;
  uint64_t _M0L1nS344;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1485 = _M0L10total__lenS352 - _M0L12digit__startS349;
  _M0L6offsetS343 = _M0L6_2atmpS1485;
  _M0L1nS344 = _M0L3numS353;
  while (1) {
    if (_M0L6offsetS343 >= 2) {
      uint64_t _M0L6_2atmpS1482 = _M0L1nS344 & 255ull;
      int32_t _M0L9byte__valS345 = (int32_t)_M0L6_2atmpS1482;
      int32_t _M0L2hiS346 = _M0L9byte__valS345 / 16;
      int32_t _M0L2loS347 = _M0L9byte__valS345 % 16;
      int32_t _M0L6_2atmpS1476 = _M0L12digit__startS349 + _M0L6offsetS343;
      int32_t _M0L6_2atmpS1474 = _M0L6_2atmpS1476 - 2;
      int32_t _M0L6_2atmpS1475 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L2hiS346];
      int32_t _M0L6_2atmpS1479;
      int32_t _M0L6_2atmpS1477;
      int32_t _M0L6_2atmpS1478;
      int32_t _M0L6_2atmpS1480;
      uint64_t _M0L6_2atmpS1481;
      _M0L6bufferS348[_M0L6_2atmpS1474] = _M0L6_2atmpS1475;
      _M0L6_2atmpS1479 = _M0L12digit__startS349 + _M0L6offsetS343;
      _M0L6_2atmpS1477 = _M0L6_2atmpS1479 - 1;
      _M0L6_2atmpS1478
      = ((moonbit_string_t)moonbit_string_literal_17.data)[
        _M0L2loS347
      ];
      _M0L6bufferS348[_M0L6_2atmpS1477] = _M0L6_2atmpS1478;
      _M0L6_2atmpS1480 = _M0L6offsetS343 - 2;
      _M0L6_2atmpS1481 = _M0L1nS344 >> 8;
      _M0L6offsetS343 = _M0L6_2atmpS1480;
      _M0L1nS344 = _M0L6_2atmpS1481;
      continue;
    } else if (_M0L6offsetS343 == 1) {
      uint64_t _M0L6_2atmpS1484 = _M0L1nS344 & 15ull;
      int32_t _M0L6nibbleS351 = (int32_t)_M0L6_2atmpS1484;
      int32_t _M0L6_2atmpS1483 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L6nibbleS351];
      _M0L6bufferS348[_M0L12digit__startS349] = _M0L6_2atmpS1483;
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
      uint64_t _M0L6_2atmpS1472 = _M0L3numS340 / _M0L4baseS338;
      int32_t _M0L6_2atmpS1473 = _M0L5countS341 + 1;
      _M0L3numS340 = _M0L6_2atmpS1472;
      _M0L5countS341 = _M0L6_2atmpS1473;
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
    int32_t _M0L6_2atmpS1471;
    int32_t _M0L6_2atmpS1470;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS336 = moonbit_clz64(_M0L5valueS335);
    _M0L6_2atmpS1471 = 63 - _M0L14leading__zerosS336;
    _M0L6_2atmpS1470 = _M0L6_2atmpS1471 / 4;
    return _M0L6_2atmpS1470 + 1;
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
    int32_t _M0L6_2atmpS1469 = -_M0L4selfS318;
    _M0L3numS320 = *(uint32_t*)&_M0L6_2atmpS1469;
  } else {
    _M0L3numS320 = *(uint32_t*)&_M0L4selfS318;
  }
  switch (_M0L5radixS317) {
    case 10: {
      int32_t _M0L10digit__lenS322;
      int32_t _M0L6_2atmpS1466;
      int32_t _M0L10total__lenS323;
      uint16_t* _M0L6bufferS324;
      int32_t _M0L12digit__startS325;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS322 = _M0FPB12dec__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1466 = 1;
      } else {
        _M0L6_2atmpS1466 = 0;
      }
      _M0L10total__lenS323 = _M0L10digit__lenS322 + _M0L6_2atmpS1466;
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
      int32_t _M0L6_2atmpS1467;
      int32_t _M0L10total__lenS327;
      uint16_t* _M0L6bufferS328;
      int32_t _M0L12digit__startS329;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS326 = _M0FPB12hex__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1467 = 1;
      } else {
        _M0L6_2atmpS1467 = 0;
      }
      _M0L10total__lenS327 = _M0L10digit__lenS326 + _M0L6_2atmpS1467;
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
      int32_t _M0L6_2atmpS1468;
      int32_t _M0L10total__lenS331;
      uint16_t* _M0L6bufferS332;
      int32_t _M0L12digit__startS333;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS330
      = _M0FPB14radix__count32(_M0L3numS320, _M0L5radixS317);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1468 = 1;
      } else {
        _M0L6_2atmpS1468 = 0;
      }
      _M0L10total__lenS331 = _M0L10digit__lenS330 + _M0L6_2atmpS1468;
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
      uint32_t _M0L6_2atmpS1464 = _M0L3numS314 / _M0L4baseS312;
      int32_t _M0L6_2atmpS1465 = _M0L5countS315 + 1;
      _M0L3numS314 = _M0L6_2atmpS1464;
      _M0L5countS315 = _M0L6_2atmpS1465;
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
    int32_t _M0L6_2atmpS1463;
    int32_t _M0L6_2atmpS1462;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS310 = moonbit_clz32(_M0L5valueS309);
    _M0L6_2atmpS1463 = 31 - _M0L14leading__zerosS310;
    _M0L6_2atmpS1462 = _M0L6_2atmpS1463 / 4;
    return _M0L6_2atmpS1462 + 1;
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
  int32_t _M0L6_2atmpS1461;
  uint32_t _M0L3numS284;
  int32_t _M0L6offsetS285;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1461 = _M0L10total__lenS307 - _M0L12digit__startS295;
  _M0L3numS284 = _M0L3numS306;
  _M0L6offsetS285 = _M0L6_2atmpS1461;
  while (1) {
    if (_M0L3numS284 >= 10000u) {
      uint32_t _M0L1tS286 = _M0L3numS284 / 10000u;
      uint32_t _M0L6_2atmpS1438 = _M0L3numS284 % 10000u;
      int32_t _M0L1rS287 = *(int32_t*)&_M0L6_2atmpS1438;
      int32_t _M0L2d1S288 = _M0L1rS287 / 100;
      int32_t _M0L2d2S289 = _M0L1rS287 % 100;
      int32_t _M0L6_2atmpS1437 = _M0L2d1S288 / 10;
      int32_t _M0L6_2atmpS1436 = 48 + _M0L6_2atmpS1437;
      int32_t _M0L6d1__hiS290 = (uint16_t)_M0L6_2atmpS1436;
      int32_t _M0L6_2atmpS1435 = _M0L2d1S288 % 10;
      int32_t _M0L6_2atmpS1434 = 48 + _M0L6_2atmpS1435;
      int32_t _M0L6d1__loS291 = (uint16_t)_M0L6_2atmpS1434;
      int32_t _M0L6_2atmpS1433 = _M0L2d2S289 / 10;
      int32_t _M0L6_2atmpS1432 = 48 + _M0L6_2atmpS1433;
      int32_t _M0L6d2__hiS292 = (uint16_t)_M0L6_2atmpS1432;
      int32_t _M0L6_2atmpS1431 = _M0L2d2S289 % 10;
      int32_t _M0L6_2atmpS1430 = 48 + _M0L6_2atmpS1431;
      int32_t _M0L6d2__loS293 = (uint16_t)_M0L6_2atmpS1430;
      int32_t _M0L6_2atmpS1422 = _M0L12digit__startS295 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS1421 = _M0L6_2atmpS1422 - 4;
      int32_t _M0L6_2atmpS1424;
      int32_t _M0L6_2atmpS1423;
      int32_t _M0L6_2atmpS1426;
      int32_t _M0L6_2atmpS1425;
      int32_t _M0L6_2atmpS1428;
      int32_t _M0L6_2atmpS1427;
      int32_t _M0L6_2atmpS1429;
      _M0L6bufferS294[_M0L6_2atmpS1421] = _M0L6d1__hiS290;
      _M0L6_2atmpS1424 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1423 = _M0L6_2atmpS1424 - 3;
      _M0L6bufferS294[_M0L6_2atmpS1423] = _M0L6d1__loS291;
      _M0L6_2atmpS1426 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1425 = _M0L6_2atmpS1426 - 2;
      _M0L6bufferS294[_M0L6_2atmpS1425] = _M0L6d2__hiS292;
      _M0L6_2atmpS1428 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1427 = _M0L6_2atmpS1428 - 1;
      _M0L6bufferS294[_M0L6_2atmpS1427] = _M0L6d2__loS293;
      _M0L6_2atmpS1429 = _M0L6offsetS285 - 4;
      _M0L3numS284 = _M0L1tS286;
      _M0L6offsetS285 = _M0L6_2atmpS1429;
      continue;
    } else {
      int32_t _M0L6_2atmpS1460 = *(int32_t*)&_M0L3numS284;
      int32_t _M0L9remainingS297 = _M0L6_2atmpS1460;
      int32_t _M0L6offsetS298 = _M0L6offsetS285;
      while (1) {
        if (_M0L9remainingS297 >= 100) {
          int32_t _M0L1tS299 = _M0L9remainingS297 / 100;
          int32_t _M0L1dS300 = _M0L9remainingS297 % 100;
          int32_t _M0L6_2atmpS1447 = _M0L1dS300 / 10;
          int32_t _M0L6_2atmpS1446 = 48 + _M0L6_2atmpS1447;
          int32_t _M0L5d__hiS301 = (uint16_t)_M0L6_2atmpS1446;
          int32_t _M0L6_2atmpS1445 = _M0L1dS300 % 10;
          int32_t _M0L6_2atmpS1444 = 48 + _M0L6_2atmpS1445;
          int32_t _M0L5d__loS302 = (uint16_t)_M0L6_2atmpS1444;
          int32_t _M0L6_2atmpS1440 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1439 = _M0L6_2atmpS1440 - 2;
          int32_t _M0L6_2atmpS1442;
          int32_t _M0L6_2atmpS1441;
          int32_t _M0L6_2atmpS1443;
          _M0L6bufferS294[_M0L6_2atmpS1439] = _M0L5d__hiS301;
          _M0L6_2atmpS1442 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1441 = _M0L6_2atmpS1442 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1441] = _M0L5d__loS302;
          _M0L6_2atmpS1443 = _M0L6offsetS298 - 2;
          _M0L9remainingS297 = _M0L1tS299;
          _M0L6offsetS298 = _M0L6_2atmpS1443;
          continue;
        } else if (_M0L9remainingS297 >= 10) {
          int32_t _M0L6_2atmpS1455 = _M0L9remainingS297 / 10;
          int32_t _M0L6_2atmpS1454 = 48 + _M0L6_2atmpS1455;
          int32_t _M0L5d__hiS304 = (uint16_t)_M0L6_2atmpS1454;
          int32_t _M0L6_2atmpS1453 = _M0L9remainingS297 % 10;
          int32_t _M0L6_2atmpS1452 = 48 + _M0L6_2atmpS1453;
          int32_t _M0L5d__loS305 = (uint16_t)_M0L6_2atmpS1452;
          int32_t _M0L6_2atmpS1449 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1448 = _M0L6_2atmpS1449 - 2;
          int32_t _M0L6_2atmpS1451;
          int32_t _M0L6_2atmpS1450;
          _M0L6bufferS294[_M0L6_2atmpS1448] = _M0L5d__hiS304;
          _M0L6_2atmpS1451 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1450 = _M0L6_2atmpS1451 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1450] = _M0L5d__loS305;
        } else {
          int32_t _M0L6_2atmpS1459 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1456 = _M0L6_2atmpS1459 - 1;
          int32_t _M0L6_2atmpS1458 = 48 + _M0L9remainingS297;
          int32_t _M0L6_2atmpS1457 = (uint16_t)_M0L6_2atmpS1458;
          _M0L6bufferS294[_M0L6_2atmpS1456] = _M0L6_2atmpS1457;
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
  int32_t _M0L6_2atmpS1406;
  int32_t _M0L6_2atmpS1405;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS267 = *(uint32_t*)&_M0L5radixS268;
  _M0L6_2atmpS1406 = _M0L5radixS268 - 1;
  _M0L6_2atmpS1405 = _M0L5radixS268 & _M0L6_2atmpS1406;
  if (_M0L6_2atmpS1405 == 0) {
    int32_t _M0L5shiftS269;
    uint32_t _M0L4maskS270;
    int32_t _M0L6_2atmpS1413;
    int32_t _M0L6offsetS271;
    uint32_t _M0L1nS272;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS269 = moonbit_ctz32(_M0L5radixS268);
    _M0L4maskS270 = _M0L4baseS267 - 1u;
    _M0L6_2atmpS1413 = _M0L10total__lenS277 - _M0L12digit__startS275;
    _M0L6offsetS271 = _M0L6_2atmpS1413;
    _M0L1nS272 = _M0L3numS278;
    while (1) {
      if (_M0L1nS272 > 0u) {
        uint32_t _M0L6_2atmpS1412 = _M0L1nS272 & _M0L4maskS270;
        int32_t _M0L5digitS273 = *(int32_t*)&_M0L6_2atmpS1412;
        int32_t _M0L6_2atmpS1409 = _M0L12digit__startS275 + _M0L6offsetS271;
        int32_t _M0L6_2atmpS1407 = _M0L6_2atmpS1409 - 1;
        int32_t _M0L6_2atmpS1408 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS273];
        int32_t _M0L6_2atmpS1410;
        uint32_t _M0L6_2atmpS1411;
        _M0L6bufferS274[_M0L6_2atmpS1407] = _M0L6_2atmpS1408;
        _M0L6_2atmpS1410 = _M0L6offsetS271 - 1;
        _M0L6_2atmpS1411 = _M0L1nS272 >> (_M0L5shiftS269 & 31);
        _M0L6offsetS271 = _M0L6_2atmpS1410;
        _M0L1nS272 = _M0L6_2atmpS1411;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1420 = _M0L10total__lenS277 - _M0L12digit__startS275;
    int32_t _M0L6offsetS279 = _M0L6_2atmpS1420;
    uint32_t _M0L1nS280 = _M0L3numS278;
    while (1) {
      if (_M0L1nS280 > 0u) {
        uint32_t _M0L1qS281 = _M0L1nS280 / _M0L4baseS267;
        uint32_t _M0L6_2atmpS1419 = _M0L1qS281 * _M0L4baseS267;
        uint32_t _M0L6_2atmpS1418 = _M0L1nS280 - _M0L6_2atmpS1419;
        int32_t _M0L5digitS282 = *(int32_t*)&_M0L6_2atmpS1418;
        int32_t _M0L6_2atmpS1416 = _M0L12digit__startS275 + _M0L6offsetS279;
        int32_t _M0L6_2atmpS1414 = _M0L6_2atmpS1416 - 1;
        int32_t _M0L6_2atmpS1415 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS282];
        int32_t _M0L6_2atmpS1417;
        _M0L6bufferS274[_M0L6_2atmpS1414] = _M0L6_2atmpS1415;
        _M0L6_2atmpS1417 = _M0L6offsetS279 - 1;
        _M0L6offsetS279 = _M0L6_2atmpS1417;
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
  int32_t _M0L6_2atmpS1404;
  int32_t _M0L6offsetS256;
  uint32_t _M0L1nS257;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1404 = _M0L10total__lenS265 - _M0L12digit__startS262;
  _M0L6offsetS256 = _M0L6_2atmpS1404;
  _M0L1nS257 = _M0L3numS266;
  while (1) {
    if (_M0L6offsetS256 >= 2) {
      uint32_t _M0L6_2atmpS1401 = _M0L1nS257 & 255u;
      int32_t _M0L9byte__valS258 = *(int32_t*)&_M0L6_2atmpS1401;
      int32_t _M0L2hiS259 = _M0L9byte__valS258 / 16;
      int32_t _M0L2loS260 = _M0L9byte__valS258 % 16;
      int32_t _M0L6_2atmpS1395 = _M0L12digit__startS262 + _M0L6offsetS256;
      int32_t _M0L6_2atmpS1393 = _M0L6_2atmpS1395 - 2;
      int32_t _M0L6_2atmpS1394 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L2hiS259];
      int32_t _M0L6_2atmpS1398;
      int32_t _M0L6_2atmpS1396;
      int32_t _M0L6_2atmpS1397;
      int32_t _M0L6_2atmpS1399;
      uint32_t _M0L6_2atmpS1400;
      _M0L6bufferS261[_M0L6_2atmpS1393] = _M0L6_2atmpS1394;
      _M0L6_2atmpS1398 = _M0L12digit__startS262 + _M0L6offsetS256;
      _M0L6_2atmpS1396 = _M0L6_2atmpS1398 - 1;
      _M0L6_2atmpS1397
      = ((moonbit_string_t)moonbit_string_literal_17.data)[
        _M0L2loS260
      ];
      _M0L6bufferS261[_M0L6_2atmpS1396] = _M0L6_2atmpS1397;
      _M0L6_2atmpS1399 = _M0L6offsetS256 - 2;
      _M0L6_2atmpS1400 = _M0L1nS257 >> 8;
      _M0L6offsetS256 = _M0L6_2atmpS1399;
      _M0L1nS257 = _M0L6_2atmpS1400;
      continue;
    } else if (_M0L6offsetS256 == 1) {
      uint32_t _M0L6_2atmpS1403 = _M0L1nS257 & 15u;
      int32_t _M0L6nibbleS264 = *(int32_t*)&_M0L6_2atmpS1403;
      int32_t _M0L6_2atmpS1402 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L6nibbleS264];
      _M0L6bufferS261[_M0L12digit__startS262] = _M0L6_2atmpS1402;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS255
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS254;
  struct _M0TPB6Logger _M0L6_2atmpS1392;
  moonbit_string_t _result_2340;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS254);
  _M0L6_2atmpS1392
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS254
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS255, _M0L6_2atmpS1392);
  if (_M0L6_2atmpS1392.$1) {
    moonbit_decref(_M0L6_2atmpS1392.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2340 = _M0MPB13StringBuilder10to__string(_M0L6loggerS254);
  moonbit_decref_cycle_free(_M0L6loggerS254);
  return _result_2340;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS249,
  struct _M0TPB6Logger _M0L6loggerS248
) {
  moonbit_string_t _M0L6_2atmpS1389;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1389 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS249);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248.$0->$method_0(_M0L6loggerS248.$1, _M0L6_2atmpS1389);
  moonbit_decref_cycle_free(_M0L6_2atmpS1389);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS1390;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1390 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS1390);
  moonbit_decref_cycle_free(_M0L6_2atmpS1390);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS1391;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1391 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS1391);
  moonbit_decref_cycle_free(_M0L6_2atmpS1391);
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
  moonbit_string_t _M0L8_2afieldS2214;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2214 = _M0L4selfS246.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2214);
  return _M0L8_2afieldS2214;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS242,
  moonbit_string_t _M0L5valueS243,
  int32_t _M0L5startS244,
  int32_t _M0L3lenS245
) {
  int32_t _M0L6_2atmpS1388;
  int64_t _M0L6_2atmpS1387;
  struct _M0TPC16string10StringView _M0L6_2atmpS1386;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1388 = _M0L5startS244 + _M0L3lenS245;
  _M0L6_2atmpS1387 = (int64_t)_M0L6_2atmpS1388;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1386
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS243, _M0L5startS244, _M0L6_2atmpS1387);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS242, _M0L6_2atmpS1386);
  moonbit_decref_cycle_free(_M0L6_2atmpS1386.$0);
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
  int32_t _M0L6_2atmpS1370;
  int32_t _if__result_2341;
  int32_t _M0L6_2atmpS1378;
  int32_t _if__result_2342;
  int32_t _M0L6_2atmpS1380;
  int32_t _M0L6_2atmpS1381;
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
  _M0L6_2atmpS1370 = _M0Lm2loS236;
  if (_M0L6_2atmpS1370 > 0) {
    int32_t _M0L6_2atmpS1369 = _M0Lm2loS236;
    if (_M0L6_2atmpS1369 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1368 = _M0Lm2loS236;
      int32_t _M0L6_2atmpS1367 = _M0L4selfS235[_M0L6_2atmpS1368];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1367)) {
        int32_t _M0L6_2atmpS1366 = _M0Lm2loS236;
        int32_t _M0L6_2atmpS1365 = _M0L6_2atmpS1366 - 1;
        int32_t _M0L6_2atmpS1364 = _M0L4selfS235[_M0L6_2atmpS1365];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2341
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1364);
      } else {
        _if__result_2341 = 0;
      }
    } else {
      _if__result_2341 = 0;
    }
  } else {
    _if__result_2341 = 0;
  }
  if (_if__result_2341) {
    int32_t _M0L6_2atmpS1371 = _M0Lm2loS236;
    _M0Lm2loS236 = _M0L6_2atmpS1371 + 1;
  }
  _M0L6_2atmpS1378 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1378 > 0) {
    int32_t _M0L6_2atmpS1377 = _M0Lm2hiS238;
    if (_M0L6_2atmpS1377 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1376 = _M0Lm2hiS238;
      int32_t _M0L6_2atmpS1375 = _M0L4selfS235[_M0L6_2atmpS1376];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1375)) {
        int32_t _M0L6_2atmpS1374 = _M0Lm2hiS238;
        int32_t _M0L6_2atmpS1373 = _M0L6_2atmpS1374 - 1;
        int32_t _M0L6_2atmpS1372 = _M0L4selfS235[_M0L6_2atmpS1373];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2342
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1372);
      } else {
        _if__result_2342 = 0;
      }
    } else {
      _if__result_2342 = 0;
    }
  } else {
    _if__result_2342 = 0;
  }
  if (_if__result_2342) {
    int32_t _M0L6_2atmpS1379 = _M0Lm2hiS238;
    _M0Lm2hiS238 = _M0L6_2atmpS1379 - 1;
  }
  _M0L6_2atmpS1380 = _M0Lm2loS236;
  _M0L6_2atmpS1381 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1380 >= _M0L6_2atmpS1381) {
    int32_t _M0L6_2atmpS1382 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1383 = _M0Lm2loS236;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1382,
                                                 .$2 = _M0L6_2atmpS1383};
  } else {
    int32_t _M0L6_2atmpS1384 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1385 = _M0Lm2hiS238;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1384,
                                                 .$2 = _M0L6_2atmpS1385};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS1363;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS1363
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS1363);
  if (_M0L6_2atmpS1363.$1) {
    moonbit_decref(_M0L6_2atmpS1363.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS231,
  struct _M0TPB4Show _M0L4showS230
) {
  struct _M0TPB6Logger _M0L6_2atmpS1362;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS231);
  _M0L6_2atmpS1362
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS231
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS230.$0->$method_0(_M0L4showS230.$1, _M0L6_2atmpS1362);
  if (_M0L6_2atmpS1362.$1) {
    moonbit_decref(_M0L6_2atmpS1362.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS229) {
  int64_t _M0L6_2atmpS1361;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1361 = (int64_t)_M0L4selfS229;
  return *(uint64_t*)&_M0L6_2atmpS1361;
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
  int32_t _M0L6_2atmpS1360;
  struct _M0TPC16string10StringView _M0L6_2atmpS1358;
  struct _M0TPB6Logger _M0L6_2atmpS1359;
  moonbit_string_t _result_2343;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1360 = Moonbit_array_length(_M0L4selfS227);
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS1358
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS227, .$1 = 0, .$2 = _M0L6_2atmpS1360
  };
  moonbit_incref_cycle_free(_M0L3bufS226);
  _M0L6_2atmpS1359
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS226
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1358, _M0L6_2atmpS1359, _M0L5quoteS228);
  moonbit_decref_cycle_free(_M0L6_2atmpS1358.$0);
  if (_M0L6_2atmpS1359.$1) {
    moonbit_decref(_M0L6_2atmpS1359.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2343 = _M0MPB13StringBuilder10to__string(_M0L3bufS226);
  moonbit_decref_cycle_free(_M0L3bufS226);
  return _result_2343;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS218,
  struct _M0TPB6Logger _M0L6loggerS216,
  int32_t _M0L5quoteS215
) {
  int32_t _M0L3endS1356;
  int32_t _M0L5startS1357;
  int32_t _M0L3lenS217;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS219;
  int32_t _M0L1iS220;
  int32_t _M0L3segS221;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS215) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  _M0L3endS1356 = _M0L4selfS218.$2;
  _M0L5startS1357 = _M0L4selfS218.$1;
  _M0L3lenS217 = _M0L3endS1356 - _M0L5startS1357;
  moonbit_incref_cycle_free(_M0L4selfS218.$0);
  if (_M0L6loggerS216.$1) {
    moonbit_incref(_M0L6loggerS216.$1);
  }
  _M0L6_2aenvS219
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 51, 0);
  _M0L6_2aenvS219->$0 = _M0L4selfS218;
  _M0L6_2aenvS219->$1 = _M0L6loggerS216;
  _M0L1iS220 = 0;
  _M0L3segS221 = 0;
  _2afor_222:;
  while (1) {
    moonbit_string_t _M0L3strS1353;
    int32_t _M0L5startS1355;
    int32_t _M0L6_2atmpS1354;
    int32_t _M0L4codeS223;
    int32_t _M0L1cS225;
    int32_t _M0L6_2atmpS1337;
    int32_t _M0L6_2atmpS1338;
    int32_t _M0L6_2atmpS1339;
    if (_M0L1iS220 >= _M0L3lenS217) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
      moonbit_decref_cycle_free(_M0L6_2aenvS219);
      break;
    }
    _M0L3strS1353 = _M0L4selfS218.$0;
    _M0L5startS1355 = _M0L4selfS218.$1;
    _M0L6_2atmpS1354 = _M0L5startS1355 + _M0L1iS220;
    _M0L4codeS223 = _M0L3strS1353[_M0L6_2atmpS1354];
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
        int32_t _M0L6_2atmpS1340;
        int32_t _M0L6_2atmpS1341;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_18.data);
        _M0L6_2atmpS1340 = _M0L1iS220 + 1;
        _M0L6_2atmpS1341 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1340;
        _M0L3segS221 = _M0L6_2atmpS1341;
        goto _2afor_222;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1342;
        int32_t _M0L6_2atmpS1343;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_19.data);
        _M0L6_2atmpS1342 = _M0L1iS220 + 1;
        _M0L6_2atmpS1343 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1342;
        _M0L3segS221 = _M0L6_2atmpS1343;
        goto _2afor_222;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1344;
        int32_t _M0L6_2atmpS1345;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_20.data);
        _M0L6_2atmpS1344 = _M0L1iS220 + 1;
        _M0L6_2atmpS1345 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1344;
        _M0L3segS221 = _M0L6_2atmpS1345;
        goto _2afor_222;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1346;
        int32_t _M0L6_2atmpS1347;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS1346 = _M0L1iS220 + 1;
        _M0L6_2atmpS1347 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1346;
        _M0L3segS221 = _M0L6_2atmpS1347;
        goto _2afor_222;
        break;
      }
      default: {
        if (_M0L4codeS223 < 32) {
          int32_t _M0L6_2atmpS1349;
          moonbit_string_t _M0L6_2atmpS1348;
          int32_t _M0L6_2atmpS1350;
          int32_t _M0L6_2atmpS1351;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_22.data);
          _M0L6_2atmpS1349 = _M0L4codeS223 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1348 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1349);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, _M0L6_2atmpS1348);
          moonbit_decref_cycle_free(_M0L6_2atmpS1348);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1350 = _M0L1iS220 + 1;
          _M0L6_2atmpS1351 = _M0L1iS220 + 1;
          _M0L1iS220 = _M0L6_2atmpS1350;
          _M0L3segS221 = _M0L6_2atmpS1351;
          goto _2afor_222;
        } else {
          int32_t _M0L6_2atmpS1352 = _M0L1iS220 + 1;
          int32_t _tmp_2346 = _M0L3segS221;
          _M0L1iS220 = _M0L6_2atmpS1352;
          _M0L3segS221 = _tmp_2346;
          goto _2afor_222;
        }
        break;
      }
    }
    goto joinlet_2345;
    join_224:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1337 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS225);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, _M0L6_2atmpS1337);
    _M0L6_2atmpS1338 = _M0L1iS220 + 1;
    _M0L6_2atmpS1339 = _M0L1iS220 + 1;
    _M0L1iS220 = _M0L6_2atmpS1338;
    _M0L3segS221 = _M0L6_2atmpS1339;
    continue;
    joinlet_2345:;
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
    int64_t _M0L6_2atmpS1336 = (int64_t)_M0L1iS213;
    struct _M0TPC16string10StringView _M0L6_2atmpS1335;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1335
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS212, _M0L3segS214, _M0L6_2atmpS1336);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS210.$0->$method_2(_M0L6loggerS210.$1, _M0L6_2atmpS1335);
    moonbit_decref_cycle_free(_M0L6_2atmpS1335.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS201,
  int32_t _M0L5startS203,
  int64_t _M0L3endS205
) {
  int32_t _M0L3endS1333;
  int32_t _M0L5startS1334;
  int32_t _M0L3lenS200;
  int32_t _M0Lm2loS202;
  int32_t _M0Lm2hiS204;
  moonbit_string_t _M0L3strS208;
  int32_t _M0L4baseS209;
  int32_t _M0L6_2atmpS1311;
  int32_t _if__result_2347;
  int32_t _M0L6_2atmpS1321;
  int32_t _if__result_2348;
  int32_t _M0L6_2atmpS1323;
  int32_t _M0L6_2atmpS1324;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1333 = _M0L4selfS201.$2;
  _M0L5startS1334 = _M0L4selfS201.$1;
  _M0L3lenS200 = _M0L3endS1333 - _M0L5startS1334;
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
  _M0L6_2atmpS1311 = _M0Lm2loS202;
  if (_M0L6_2atmpS1311 > 0) {
    int32_t _M0L6_2atmpS1310 = _M0Lm2loS202;
    if (_M0L6_2atmpS1310 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1309 = _M0Lm2loS202;
      int32_t _M0L6_2atmpS1308 = _M0L4baseS209 + _M0L6_2atmpS1309;
      int32_t _M0L6_2atmpS1307 = _M0L3strS208[_M0L6_2atmpS1308];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1307)) {
        int32_t _M0L6_2atmpS1306 = _M0Lm2loS202;
        int32_t _M0L6_2atmpS1305 = _M0L4baseS209 + _M0L6_2atmpS1306;
        int32_t _M0L6_2atmpS1304 = _M0L6_2atmpS1305 - 1;
        int32_t _M0L6_2atmpS1303 = _M0L3strS208[_M0L6_2atmpS1304];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2347
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1303);
      } else {
        _if__result_2347 = 0;
      }
    } else {
      _if__result_2347 = 0;
    }
  } else {
    _if__result_2347 = 0;
  }
  if (_if__result_2347) {
    int32_t _M0L6_2atmpS1312 = _M0Lm2loS202;
    _M0Lm2loS202 = _M0L6_2atmpS1312 + 1;
  }
  _M0L6_2atmpS1321 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1321 > 0) {
    int32_t _M0L6_2atmpS1320 = _M0Lm2hiS204;
    if (_M0L6_2atmpS1320 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1319 = _M0Lm2hiS204;
      int32_t _M0L6_2atmpS1318 = _M0L4baseS209 + _M0L6_2atmpS1319;
      int32_t _M0L6_2atmpS1317 = _M0L3strS208[_M0L6_2atmpS1318];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1317)) {
        int32_t _M0L6_2atmpS1316 = _M0Lm2hiS204;
        int32_t _M0L6_2atmpS1315 = _M0L4baseS209 + _M0L6_2atmpS1316;
        int32_t _M0L6_2atmpS1314 = _M0L6_2atmpS1315 - 1;
        int32_t _M0L6_2atmpS1313 = _M0L3strS208[_M0L6_2atmpS1314];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2348
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1313);
      } else {
        _if__result_2348 = 0;
      }
    } else {
      _if__result_2348 = 0;
    }
  } else {
    _if__result_2348 = 0;
  }
  if (_if__result_2348) {
    int32_t _M0L6_2atmpS1322 = _M0Lm2hiS204;
    _M0Lm2hiS204 = _M0L6_2atmpS1322 - 1;
  }
  _M0L6_2atmpS1323 = _M0Lm2loS202;
  _M0L6_2atmpS1324 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1323 >= _M0L6_2atmpS1324) {
    int32_t _M0L6_2atmpS1328 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1325 = _M0L4baseS209 + _M0L6_2atmpS1328;
    int32_t _M0L6_2atmpS1327 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1326 = _M0L4baseS209 + _M0L6_2atmpS1327;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1325,
                                                 .$2 = _M0L6_2atmpS1326};
  } else {
    int32_t _M0L6_2atmpS1332 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1329 = _M0L4baseS209 + _M0L6_2atmpS1332;
    int32_t _M0L6_2atmpS1331 = _M0Lm2hiS204;
    int32_t _M0L6_2atmpS1330 = _M0L4baseS209 + _M0L6_2atmpS1331;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1329,
                                                 .$2 = _M0L6_2atmpS1330};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS199) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS198;
  int32_t _M0L6_2atmpS1300;
  int32_t _M0L6_2atmpS1299;
  int32_t _M0L6_2atmpS1302;
  int32_t _M0L6_2atmpS1301;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1298;
  moonbit_string_t _result_2349;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1300 = _M0IPC14byte4BytePB3Div3div(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1299
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1300);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1299);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1302 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1301
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1302);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1301);
  _M0L6_2atmpS1298 = _M0L7_2aselfS198;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2349 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1298);
  moonbit_decref_cycle_free(_M0L6_2atmpS1298);
  return _result_2349;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS197) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS197 < 10) {
    int32_t _M0L6_2atmpS1295;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1295 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1295);
  } else {
    int32_t _M0L6_2atmpS1297;
    int32_t _M0L6_2atmpS1296;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1297 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1296 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1297, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1296);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS1293;
  int32_t _M0L6_2atmpS1294;
  int32_t _M0L6_2atmpS1292;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1293 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS1294 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS1292 = _M0L6_2atmpS1293 - _M0L6_2atmpS1294;
  return _M0L6_2atmpS1292 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS1290;
  int32_t _M0L6_2atmpS1291;
  int32_t _M0L6_2atmpS1289;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1290 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS1291 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS1289 = _M0L6_2atmpS1290 % _M0L6_2atmpS1291;
  return _M0L6_2atmpS1289 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS1287;
  int32_t _M0L6_2atmpS1288;
  int32_t _M0L6_2atmpS1286;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1287 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS1288 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS1286 = _M0L6_2atmpS1287 / _M0L6_2atmpS1288;
  return _M0L6_2atmpS1286 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS189,
  int32_t _M0L4thatS190
) {
  int32_t _M0L6_2atmpS1284;
  int32_t _M0L6_2atmpS1285;
  int32_t _M0L6_2atmpS1283;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1284 = (int32_t)_M0L4selfS189;
  _M0L6_2atmpS1285 = (int32_t)_M0L4thatS190;
  _M0L6_2atmpS1283 = _M0L6_2atmpS1284 + _M0L6_2atmpS1285;
  return _M0L6_2atmpS1283 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS188) {
  int32_t _M0L6_2atmpS1282;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1282 = (int32_t)_M0L4selfS188;
  return _M0L6_2atmpS1282;
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
  int32_t _M0L3lenS1281;
  int32_t _M0L8requiredS184;
  uint16_t* _M0L4dataS1276;
  int32_t _M0L6_2atmpS1275;
  int32_t _if__result_2350;
  uint16_t* _M0L4dataS1277;
  int32_t _M0L3lenS1278;
  int32_t _M0L3lenS1280;
  int32_t _M0L6_2atmpS1279;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS182 = Moonbit_array_length(_M0L3strS183);
  if (_M0L8str__lenS182 == 0) {
    return 0;
  }
  _M0L3lenS1281 = _M0L4selfS185->$1;
  _M0L8requiredS184 = _M0L3lenS1281 + _M0L8str__lenS182;
  _M0L4dataS1276 = _M0L4selfS185->$0;
  _M0L6_2atmpS1275 = Moonbit_array_length(_M0L4dataS1276);
  if (_M0L8requiredS184 > _M0L6_2atmpS1275) {
    _if__result_2350 = 1;
  } else {
    int32_t _M0L3lenS1274 = _M0L4selfS185->$1;
    _if__result_2350 = _M0L8requiredS184 < _M0L3lenS1274;
  }
  if (_if__result_2350) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS185, _M0L8requiredS184);
  }
  _M0L4dataS1277 = _M0L4selfS185->$0;
  _M0L3lenS1278 = _M0L4selfS185->$1;
  moonbit_incref_cycle_free(_M0L4dataS1277);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1277, _M0L3lenS1278, _M0L3strS183, 0, _M0L8str__lenS182);
  moonbit_decref_cycle_free(_M0L4dataS1277);
  _M0L3lenS1280 = _M0L4selfS185->$1;
  _M0L6_2atmpS1279 = _M0L3lenS1280 + _M0L8str__lenS182;
  _M0L4selfS185->$1 = _M0L6_2atmpS1279;
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
      int32_t _M0L6_2atmpS1271 = _M0L3strS179[_M0L1iS176];
      int32_t _M0L6_2atmpS1272;
      int32_t _M0L6_2atmpS1273;
      _M0L4selfS178[_M0L1jS177] = _M0L6_2atmpS1271;
      _M0L6_2atmpS1272 = _M0L1iS176 + 1;
      _M0L6_2atmpS1273 = _M0L1jS177 + 1;
      _M0L1iS176 = _M0L6_2atmpS1272;
      _M0L1jS177 = _M0L6_2atmpS1273;
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
    int32_t _M0L3lenS1242 = _M0L4selfS171->$1;
    uint16_t* _M0L4dataS1244 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1243 = Moonbit_array_length(_M0L4dataS1244);
    uint16_t* _M0L4dataS1247;
    int32_t _M0L3lenS1248;
    int32_t _M0L6_2atmpS1249;
    int32_t _M0L3lenS1251;
    int32_t _M0L6_2atmpS1250;
    if (_M0L3lenS1242 >= _M0L6_2atmpS1243) {
      int32_t _M0L3lenS1246 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1245 = _M0L3lenS1246 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1245);
    }
    _M0L4dataS1247 = _M0L4selfS171->$0;
    _M0L3lenS1248 = _M0L4selfS171->$1;
    moonbit_incref_cycle_free(_M0L4dataS1247);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1249 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS169);
    if (
      _M0L3lenS1248 < 0
      || _M0L3lenS1248 >= Moonbit_array_length(_M0L4dataS1247)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1247[_M0L3lenS1248] = _M0L6_2atmpS1249;
    moonbit_decref_cycle_free(_M0L4dataS1247);
    _M0L3lenS1251 = _M0L4selfS171->$1;
    _M0L6_2atmpS1250 = _M0L3lenS1251 + 1;
    _M0L4selfS171->$1 = _M0L6_2atmpS1250;
  } else if (_M0L4codeS169 <= 1114111u) {
    uint16_t* _M0L4dataS1255 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1253 = Moonbit_array_length(_M0L4dataS1255);
    int32_t _M0L3lenS1254 = _M0L4selfS171->$1;
    int32_t _M0L6_2atmpS1252 = _M0L6_2atmpS1253 - _M0L3lenS1254;
    uint32_t _M0L4codeS172;
    uint16_t* _M0L4dataS1258;
    int32_t _M0L3lenS1259;
    uint32_t _M0L6_2atmpS1262;
    uint32_t _M0L6_2atmpS1261;
    int32_t _M0L6_2atmpS1260;
    uint16_t* _M0L4dataS1263;
    int32_t _M0L3lenS1268;
    int32_t _M0L6_2atmpS1264;
    uint32_t _M0L6_2atmpS1267;
    uint32_t _M0L6_2atmpS1266;
    int32_t _M0L6_2atmpS1265;
    int32_t _M0L3lenS1270;
    int32_t _M0L6_2atmpS1269;
    if (_M0L6_2atmpS1252 < 2) {
      int32_t _M0L3lenS1257 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1256 = _M0L3lenS1257 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1256);
    }
    _M0L4codeS172 = _M0L4codeS169 - 65536u;
    _M0L4dataS1258 = _M0L4selfS171->$0;
    _M0L3lenS1259 = _M0L4selfS171->$1;
    _M0L6_2atmpS1262 = _M0L4codeS172 >> 10;
    _M0L6_2atmpS1261 = 55296u + _M0L6_2atmpS1262;
    moonbit_incref_cycle_free(_M0L4dataS1258);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1260 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1261);
    if (
      _M0L3lenS1259 < 0
      || _M0L3lenS1259 >= Moonbit_array_length(_M0L4dataS1258)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1258[_M0L3lenS1259] = _M0L6_2atmpS1260;
    moonbit_decref_cycle_free(_M0L4dataS1258);
    _M0L4dataS1263 = _M0L4selfS171->$0;
    _M0L3lenS1268 = _M0L4selfS171->$1;
    _M0L6_2atmpS1264 = _M0L3lenS1268 + 1;
    _M0L6_2atmpS1267 = _M0L4codeS172 & 1023u;
    _M0L6_2atmpS1266 = 56320u + _M0L6_2atmpS1267;
    moonbit_incref_cycle_free(_M0L4dataS1263);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1265 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1266);
    if (
      _M0L6_2atmpS1264 < 0
      || _M0L6_2atmpS1264 >= Moonbit_array_length(_M0L4dataS1263)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1263[_M0L6_2atmpS1264] = _M0L6_2atmpS1265;
    moonbit_decref_cycle_free(_M0L4dataS1263);
    _M0L3lenS1270 = _M0L4selfS171->$1;
    _M0L6_2atmpS1269 = _M0L3lenS1270 + 2;
    _M0L4selfS171->$1 = _M0L6_2atmpS1269;
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
  uint16_t* _M0L4dataS1241;
  int32_t _M0L6_2atmpS1239;
  int32_t _M0L3lenS1240;
  int32_t _M0L13new__capacityS165;
  uint16_t* _M0L4dataS1236;
  int32_t _M0L6_2atmpS1237;
  int32_t _M0L3lenS1238;
  uint16_t* _M0L9new__dataS168;
  uint16_t* _M0L6_2aoldS2215;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1241 = _M0L4selfS166->$0;
  _M0L6_2atmpS1239 = Moonbit_array_length(_M0L4dataS1241);
  _M0L3lenS1240 = _M0L4selfS166->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS165
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1239, _M0L3lenS1240, _M0L8requiredS167);
  _M0L4dataS1236 = _M0L4selfS166->$0;
  moonbit_incref_cycle_free(_M0L4dataS1236);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1237 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1238 = _M0L4selfS166->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS168
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1236, _M0L13new__capacityS165, _M0L6_2atmpS1237, _M0L3lenS1238, 0, 0);
  _M0L6_2aoldS2215 = _M0L4selfS166->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2215);
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
  int32_t _M0L6_2atmpS1235;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1235 = *(int32_t*)&_M0L4selfS158;
  return (uint16_t)_M0L6_2atmpS1235;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS157) {
  int32_t _M0L6_2atmpS1234;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1234 = _M0L4selfS157;
  return *(uint32_t*)&_M0L6_2atmpS1234;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS155
) {
  int32_t _M0L3lenS1225;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1225 = _M0L4selfS155->$1;
  if (_M0L3lenS1225 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1226 = _M0L4selfS155->$1;
    uint16_t* _M0L4dataS1228 = _M0L4selfS155->$0;
    int32_t _M0L6_2atmpS1227 = Moonbit_array_length(_M0L4dataS1228);
    if (_M0L3lenS1226 == _M0L6_2atmpS1227) {
      uint16_t* _M0L4dataS1229 = _M0L4selfS155->$0;
      moonbit_incref_cycle_free(_M0L4dataS1229);
      return _M0L4dataS1229;
    } else {
      uint16_t* _M0L4dataS1230 = _M0L4selfS155->$0;
      int32_t _M0L3lenS1231 = _M0L4selfS155->$1;
      int32_t _M0L6_2atmpS1232;
      int32_t _M0L3lenS1233;
      uint16_t* _M0L4dataS156;
      moonbit_incref_cycle_free(_M0L4dataS1230);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1232 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1233 = _M0L4selfS155->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS156
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1230, _M0L3lenS1231, _M0L6_2atmpS1232, _M0L3lenS1233, 0, 0);
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
  int32_t _if__result_2353;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS148 >= 0) {
    if (_M0L3lenS149 >= 0) {
      if (_M0L11src__offsetS150 >= 0) {
        if (_M0L11dst__offsetS151 >= 0) {
          int32_t _M0L6_2atmpS1221 = _M0L11src__offsetS150 + _M0L3lenS149;
          int32_t _M0L6_2atmpS1222 = Moonbit_array_length(_M0L3srcS152);
          if (_M0L6_2atmpS1221 <= _M0L6_2atmpS1222) {
            int32_t _M0L6_2atmpS1220 = _M0L11dst__offsetS151 + _M0L3lenS149;
            _if__result_2353 = _M0L6_2atmpS1220 <= _M0L13allocate__lenS148;
          } else {
            _if__result_2353 = 0;
          }
        } else {
          _if__result_2353 = 0;
        }
      } else {
        _if__result_2353 = 0;
      }
    } else {
      _if__result_2353 = 0;
    }
  } else {
    _if__result_2353 = 0;
  }
  if (_if__result_2353) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS152, _M0L13allocate__lenS148, _M0L4initS153, _M0L11src__offsetS150, _M0L11dst__offsetS151, _M0L3lenS149);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS154;
    int32_t _M0L6_2atmpS1224;
    moonbit_string_t _M0L6_2atmpS1223;
    uint16_t* _result_2354;
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
    _M0L6_2atmpS1224 = Moonbit_array_length(_M0L3srcS152);
    moonbit_decref_cycle_free(_M0L3srcS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L6_2atmpS1224);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1223
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS154);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS154);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2354 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1223);
    moonbit_decref_cycle_free(_M0L6_2atmpS1223);
    return _result_2354;
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
  struct _M0TPB13StringBuilder* _block_2355;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS139 < 1) {
    _M0L7initialS138 = 1;
  } else {
    int32_t _M0L6_2atmpS1219 = _M0L10size__hintS139 + 1;
    _M0L7initialS138 = _M0L6_2atmpS1219 / 2;
  }
  _M0L4dataS140 = (uint16_t*)moonbit_make_string(_M0L7initialS138, 0);
  _block_2355
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2355)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 56, 0);
  _block_2355->$0 = _M0L4dataS140;
  _block_2355->$1 = 0;
  return _block_2355;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS137) {
  int32_t _M0L6_2atmpS1218;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1218 = (int32_t)_M0L4selfS137;
  return _M0L6_2atmpS1218;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS117,
  int32_t _M0L13allocate__lenS113,
  int32_t _M0L3lenS114,
  int32_t _M0L11src__offsetS115,
  int32_t _M0L11dst__offsetS116
) {
  int32_t _if__result_2356;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS113 >= 0) {
    if (_M0L3lenS114 >= 0) {
      if (_M0L11src__offsetS115 >= 0) {
        if (_M0L11dst__offsetS116 >= 0) {
          int32_t _M0L6_2atmpS1199 = _M0L11src__offsetS115 + _M0L3lenS114;
          int32_t _M0L6_2atmpS1200;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1200
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS117);
          if (_M0L6_2atmpS1199 <= _M0L6_2atmpS1200) {
            int32_t _M0L6_2atmpS1198 = _M0L11dst__offsetS116 + _M0L3lenS114;
            _if__result_2356 = _M0L6_2atmpS1198 <= _M0L13allocate__lenS113;
          } else {
            _if__result_2356 = 0;
          }
        } else {
          _if__result_2356 = 0;
        }
      } else {
        _if__result_2356 = 0;
      }
    } else {
      _if__result_2356 = 0;
    }
  } else {
    _if__result_2356 = 0;
  }
  if (_if__result_2356) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS113, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS117, _M0L11src__offsetS115, _M0L11dst__offsetS116, _M0L3lenS114);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS118;
    int32_t _M0L6_2atmpS1202;
    moonbit_string_t _M0L6_2atmpS1201;
    moonbit_string_t* _result_2357;
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
    _M0L6_2atmpS1202 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS117);
    moonbit_decref_cycle_free(_M0L3srcS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L6_2atmpS1202);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1201
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS118);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS118);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2357
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1201);
    moonbit_decref_cycle_free(_M0L6_2atmpS1201);
    return _result_2357;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS123,
  int32_t _M0L13allocate__lenS119,
  int32_t _M0L3lenS120,
  int32_t _M0L11src__offsetS121,
  int32_t _M0L11dst__offsetS122
) {
  int32_t _if__result_2358;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS119 >= 0) {
    if (_M0L3lenS120 >= 0) {
      if (_M0L11src__offsetS121 >= 0) {
        if (_M0L11dst__offsetS122 >= 0) {
          int32_t _M0L6_2atmpS1204 = _M0L11src__offsetS121 + _M0L3lenS120;
          int32_t _M0L6_2atmpS1205;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1205
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS123);
          if (_M0L6_2atmpS1204 <= _M0L6_2atmpS1205) {
            int32_t _M0L6_2atmpS1203 = _M0L11dst__offsetS122 + _M0L3lenS120;
            _if__result_2358 = _M0L6_2atmpS1203 <= _M0L13allocate__lenS119;
          } else {
            _if__result_2358 = 0;
          }
        } else {
          _if__result_2358 = 0;
        }
      } else {
        _if__result_2358 = 0;
      }
    } else {
      _if__result_2358 = 0;
    }
  } else {
    _if__result_2358 = 0;
  }
  if (_if__result_2358) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS119, 0, _M0L3srcS123, _M0L11src__offsetS121, _M0L11dst__offsetS122, _M0L3lenS120);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS124;
    int32_t _M0L6_2atmpS1207;
    moonbit_string_t _M0L6_2atmpS1206;
    struct _M0TUsiE** _result_2359;
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
    _M0L6_2atmpS1207 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS123);
    moonbit_decref_cycle_free(_M0L3srcS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L6_2atmpS1207);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1206
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS124);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS124);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2359
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1206);
    moonbit_decref_cycle_free(_M0L6_2atmpS1206);
    return _result_2359;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS129,
  int32_t _M0L13allocate__lenS125,
  int32_t _M0L3lenS126,
  int32_t _M0L11src__offsetS127,
  int32_t _M0L11dst__offsetS128
) {
  int32_t _if__result_2360;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS125 >= 0) {
    if (_M0L3lenS126 >= 0) {
      if (_M0L11src__offsetS127 >= 0) {
        if (_M0L11dst__offsetS128 >= 0) {
          int32_t _M0L6_2atmpS1209 = _M0L11src__offsetS127 + _M0L3lenS126;
          int32_t _M0L6_2atmpS1210;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1210
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS129);
          if (_M0L6_2atmpS1209 <= _M0L6_2atmpS1210) {
            int32_t _M0L6_2atmpS1208 = _M0L11dst__offsetS128 + _M0L3lenS126;
            _if__result_2360 = _M0L6_2atmpS1208 <= _M0L13allocate__lenS125;
          } else {
            _if__result_2360 = 0;
          }
        } else {
          _if__result_2360 = 0;
        }
      } else {
        _if__result_2360 = 0;
      }
    } else {
      _if__result_2360 = 0;
    }
  } else {
    _if__result_2360 = 0;
  }
  if (_if__result_2360) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS129, _M0L13allocate__lenS125, _M0L11src__offsetS127, _M0L11dst__offsetS128, _M0L3lenS126);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS130;
    int32_t _M0L6_2atmpS1212;
    moonbit_string_t _M0L6_2atmpS1211;
    int32_t* _result_2361;
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
    _M0L6_2atmpS1212 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS129);
    moonbit_decref_cycle_free(_M0L3srcS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L6_2atmpS1212);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1211
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS130);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS130);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2361
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1211);
    moonbit_decref_cycle_free(_M0L6_2atmpS1211);
    return _result_2361;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS135,
  int32_t _M0L13allocate__lenS131,
  int32_t _M0L3lenS132,
  int32_t _M0L11src__offsetS133,
  int32_t _M0L11dst__offsetS134
) {
  int32_t _if__result_2362;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS131 >= 0) {
    if (_M0L3lenS132 >= 0) {
      if (_M0L11src__offsetS133 >= 0) {
        if (_M0L11dst__offsetS134 >= 0) {
          int32_t _M0L6_2atmpS1214 = _M0L11src__offsetS133 + _M0L3lenS132;
          int32_t _M0L6_2atmpS1215;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1215
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS135);
          if (_M0L6_2atmpS1214 <= _M0L6_2atmpS1215) {
            int32_t _M0L6_2atmpS1213 = _M0L11dst__offsetS134 + _M0L3lenS132;
            _if__result_2362 = _M0L6_2atmpS1213 <= _M0L13allocate__lenS131;
          } else {
            _if__result_2362 = 0;
          }
        } else {
          _if__result_2362 = 0;
        }
      } else {
        _if__result_2362 = 0;
      }
    } else {
      _if__result_2362 = 0;
    }
  } else {
    _if__result_2362 = 0;
  }
  if (_if__result_2362) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS135, _M0L13allocate__lenS131, _M0L11src__offsetS133, _M0L11dst__offsetS134, _M0L3lenS132);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS136;
    int32_t _M0L6_2atmpS1217;
    moonbit_string_t _M0L6_2atmpS1216;
    float* _result_2363;
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
    _M0L6_2atmpS1217 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS135);
    moonbit_decref_cycle_free(_M0L3srcS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L6_2atmpS1217);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1216
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS136);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS136);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2363
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1216);
    moonbit_decref_cycle_free(_M0L6_2atmpS1216);
    return _result_2363;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L3objS107
) {
  struct _M0TPB6Logger _M0L6_2atmpS1195;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS108);
  _M0L6_2atmpS1195
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS108
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS107, _M0L6_2atmpS1195);
  if (_M0L6_2atmpS1195.$1) {
    moonbit_decref(_M0L6_2atmpS1195.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS1196;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS1196
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS109, _M0L6_2atmpS1196);
  if (_M0L6_2atmpS1196.$1) {
    moonbit_decref(_M0L6_2atmpS1196.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  uint64_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS1197;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS1197
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS111, _M0L6_2atmpS1197);
  if (_M0L6_2atmpS1197.$1) {
    moonbit_decref(_M0L6_2atmpS1197.$1);
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
        int32_t _M0L6_2atmpS1150 = _M0L11dst__offsetS20 + _M0L1iS22;
        int32_t _M0L6_2atmpS1152 = _M0L11src__offsetS21 + _M0L1iS22;
        int32_t _M0L6_2atmpS1151;
        int32_t _M0L6_2atmpS1153;
        if (
          _M0L6_2atmpS1152 < 0
          || _M0L6_2atmpS1152 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1151 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1152];
        if (
          _M0L6_2atmpS1150 < 0
          || _M0L6_2atmpS1150 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1150] = _M0L6_2atmpS1151;
        _M0L6_2atmpS1153 = _M0L1iS22 + 1;
        _M0L1iS22 = _M0L6_2atmpS1153;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1158 = _M0L3lenS23 - 1;
    int32_t _M0L1iS25 = _M0L6_2atmpS1158;
    while (1) {
      if (_M0L1iS25 >= 0) {
        int32_t _M0L6_2atmpS1154 = _M0L11dst__offsetS20 + _M0L1iS25;
        int32_t _M0L6_2atmpS1156 = _M0L11src__offsetS21 + _M0L1iS25;
        int32_t _M0L6_2atmpS1155;
        int32_t _M0L6_2atmpS1157;
        if (
          _M0L6_2atmpS1156 < 0
          || _M0L6_2atmpS1156 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1155 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1156];
        if (
          _M0L6_2atmpS1154 < 0
          || _M0L6_2atmpS1154 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1154] = _M0L6_2atmpS1155;
        _M0L6_2atmpS1157 = _M0L1iS25 - 1;
        _M0L1iS25 = _M0L6_2atmpS1157;
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
        int32_t _M0L6_2atmpS1159 = _M0L11dst__offsetS29 + _M0L1iS31;
        int32_t _M0L6_2atmpS1161 = _M0L11src__offsetS30 + _M0L1iS31;
        moonbit_string_t _M0L6_2atmpS1160;
        moonbit_string_t _M0L6_2aoldS2216;
        int32_t _M0L6_2atmpS1162;
        if (
          _M0L6_2atmpS1161 < 0
          || _M0L6_2atmpS1161 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1160 = (moonbit_string_t)_M0L3srcS28[_M0L6_2atmpS1161];
        if (
          _M0L6_2atmpS1159 < 0
          || _M0L6_2atmpS1159 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2216 = (moonbit_string_t)_M0L3dstS27[_M0L6_2atmpS1159];
        moonbit_incref_cycle_free(_M0L6_2atmpS1160);
        moonbit_decref_cycle_free(_M0L6_2aoldS2216);
        _M0L3dstS27[_M0L6_2atmpS1159] = _M0L6_2atmpS1160;
        _M0L6_2atmpS1162 = _M0L1iS31 + 1;
        _M0L1iS31 = _M0L6_2atmpS1162;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1167 = _M0L3lenS32 - 1;
    int32_t _M0L1iS34 = _M0L6_2atmpS1167;
    while (1) {
      if (_M0L1iS34 >= 0) {
        int32_t _M0L6_2atmpS1163 = _M0L11dst__offsetS29 + _M0L1iS34;
        int32_t _M0L6_2atmpS1165 = _M0L11src__offsetS30 + _M0L1iS34;
        moonbit_string_t _M0L6_2atmpS1164;
        moonbit_string_t _M0L6_2aoldS2217;
        int32_t _M0L6_2atmpS1166;
        if (
          _M0L6_2atmpS1165 < 0
          || _M0L6_2atmpS1165 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1164 = (moonbit_string_t)_M0L3srcS28[_M0L6_2atmpS1165];
        if (
          _M0L6_2atmpS1163 < 0
          || _M0L6_2atmpS1163 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2217 = (moonbit_string_t)_M0L3dstS27[_M0L6_2atmpS1163];
        moonbit_incref_cycle_free(_M0L6_2atmpS1164);
        moonbit_decref_cycle_free(_M0L6_2aoldS2217);
        _M0L3dstS27[_M0L6_2atmpS1163] = _M0L6_2atmpS1164;
        _M0L6_2atmpS1166 = _M0L1iS34 - 1;
        _M0L1iS34 = _M0L6_2atmpS1166;
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
        int32_t _M0L6_2atmpS1168 = _M0L11dst__offsetS38 + _M0L1iS40;
        int32_t _M0L6_2atmpS1170 = _M0L11src__offsetS39 + _M0L1iS40;
        struct _M0TUsiE* _M0L6_2atmpS1169;
        struct _M0TUsiE* _M0L6_2aoldS2218;
        int32_t _M0L6_2atmpS1171;
        if (
          _M0L6_2atmpS1170 < 0
          || _M0L6_2atmpS1170 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1169 = (struct _M0TUsiE*)_M0L3srcS37[_M0L6_2atmpS1170];
        if (
          _M0L6_2atmpS1168 < 0
          || _M0L6_2atmpS1168 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2218 = (struct _M0TUsiE*)_M0L3dstS36[_M0L6_2atmpS1168];
        if (_M0L6_2atmpS1169) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1169);
        }
        if (_M0L6_2aoldS2218) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2218);
        }
        _M0L3dstS36[_M0L6_2atmpS1168] = _M0L6_2atmpS1169;
        _M0L6_2atmpS1171 = _M0L1iS40 + 1;
        _M0L1iS40 = _M0L6_2atmpS1171;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1176 = _M0L3lenS41 - 1;
    int32_t _M0L1iS43 = _M0L6_2atmpS1176;
    while (1) {
      if (_M0L1iS43 >= 0) {
        int32_t _M0L6_2atmpS1172 = _M0L11dst__offsetS38 + _M0L1iS43;
        int32_t _M0L6_2atmpS1174 = _M0L11src__offsetS39 + _M0L1iS43;
        struct _M0TUsiE* _M0L6_2atmpS1173;
        struct _M0TUsiE* _M0L6_2aoldS2219;
        int32_t _M0L6_2atmpS1175;
        if (
          _M0L6_2atmpS1174 < 0
          || _M0L6_2atmpS1174 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1173 = (struct _M0TUsiE*)_M0L3srcS37[_M0L6_2atmpS1174];
        if (
          _M0L6_2atmpS1172 < 0
          || _M0L6_2atmpS1172 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2219 = (struct _M0TUsiE*)_M0L3dstS36[_M0L6_2atmpS1172];
        if (_M0L6_2atmpS1173) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1173);
        }
        if (_M0L6_2aoldS2219) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2219);
        }
        _M0L3dstS36[_M0L6_2atmpS1172] = _M0L6_2atmpS1173;
        _M0L6_2atmpS1175 = _M0L1iS43 - 1;
        _M0L1iS43 = _M0L6_2atmpS1175;
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
        int32_t _M0L6_2atmpS1177 = _M0L11dst__offsetS47 + _M0L1iS49;
        int32_t _M0L6_2atmpS1179 = _M0L11src__offsetS48 + _M0L1iS49;
        int32_t _M0L6_2atmpS1178;
        int32_t _M0L6_2atmpS1180;
        if (
          _M0L6_2atmpS1179 < 0
          || _M0L6_2atmpS1179 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1178 = (int32_t)_M0L3srcS46[_M0L6_2atmpS1179];
        if (
          _M0L6_2atmpS1177 < 0
          || _M0L6_2atmpS1177 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS45[_M0L6_2atmpS1177] = _M0L6_2atmpS1178;
        _M0L6_2atmpS1180 = _M0L1iS49 + 1;
        _M0L1iS49 = _M0L6_2atmpS1180;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1185 = _M0L3lenS50 - 1;
    int32_t _M0L1iS52 = _M0L6_2atmpS1185;
    while (1) {
      if (_M0L1iS52 >= 0) {
        int32_t _M0L6_2atmpS1181 = _M0L11dst__offsetS47 + _M0L1iS52;
        int32_t _M0L6_2atmpS1183 = _M0L11src__offsetS48 + _M0L1iS52;
        int32_t _M0L6_2atmpS1182;
        int32_t _M0L6_2atmpS1184;
        if (
          _M0L6_2atmpS1183 < 0
          || _M0L6_2atmpS1183 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1182 = (int32_t)_M0L3srcS46[_M0L6_2atmpS1183];
        if (
          _M0L6_2atmpS1181 < 0
          || _M0L6_2atmpS1181 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS45[_M0L6_2atmpS1181] = _M0L6_2atmpS1182;
        _M0L6_2atmpS1184 = _M0L1iS52 - 1;
        _M0L1iS52 = _M0L6_2atmpS1184;
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
        int32_t _M0L6_2atmpS1186 = _M0L11dst__offsetS56 + _M0L1iS58;
        int32_t _M0L6_2atmpS1188 = _M0L11src__offsetS57 + _M0L1iS58;
        float _M0L6_2atmpS1187;
        int32_t _M0L6_2atmpS1189;
        if (
          _M0L6_2atmpS1188 < 0
          || _M0L6_2atmpS1188 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1187 = (float)_M0L3srcS55[_M0L6_2atmpS1188];
        if (
          _M0L6_2atmpS1186 < 0
          || _M0L6_2atmpS1186 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS1186] = _M0L6_2atmpS1187;
        _M0L6_2atmpS1189 = _M0L1iS58 + 1;
        _M0L1iS58 = _M0L6_2atmpS1189;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1194 = _M0L3lenS59 - 1;
    int32_t _M0L1iS61 = _M0L6_2atmpS1194;
    while (1) {
      if (_M0L1iS61 >= 0) {
        int32_t _M0L6_2atmpS1190 = _M0L11dst__offsetS56 + _M0L1iS61;
        int32_t _M0L6_2atmpS1192 = _M0L11src__offsetS57 + _M0L1iS61;
        float _M0L6_2atmpS1191;
        int32_t _M0L6_2atmpS1193;
        if (
          _M0L6_2atmpS1192 < 0
          || _M0L6_2atmpS1192 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1191 = (float)_M0L3srcS55[_M0L6_2atmpS1192];
        if (
          _M0L6_2atmpS1190 < 0
          || _M0L6_2atmpS1190 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS1190] = _M0L6_2atmpS1191;
        _M0L6_2atmpS1193 = _M0L1iS61 - 1;
        _M0L1iS61 = _M0L6_2atmpS1193;
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1119) {
  switch (Moonbit_object_tag(_M0L4_2aeS1119)) {
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_32.data;
      break;
    }
    
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_33.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1119);
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
  void* _M0L11_2aobj__ptrS1145,
  struct _M0TPB4Show _M0L8_2aparamS1144
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1143 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1145;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1143, _M0L8_2aparamS1144);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1142,
  struct _M0TPB4Show _M0L8_2aparamS1141
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1140 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1142;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1140, _M0L8_2aparamS1141);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1139,
  int32_t _M0L8_2aparamS1138
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1137 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1139;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1137, _M0L8_2aparamS1138);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1136,
  struct _M0TPC16string10StringView _M0L8_2aparamS1135
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1134 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1136;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1134, _M0L8_2aparamS1135);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1133,
  moonbit_string_t _M0L8_2aparamS1130,
  int32_t _M0L8_2aparamS1131,
  int32_t _M0L8_2aparamS1132
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1129 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1133;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1129, _M0L8_2aparamS1130, _M0L8_2aparamS1131, _M0L8_2aparamS1132);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1128,
  moonbit_string_t _M0L8_2aparamS1127
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1126 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1128;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1126, _M0L8_2aparamS1127);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_2374 = 9218868437227405311ll;
  int64_t _tmp_2375;
  int64_t _tmp_2376;
  int64_t _tmp_2377;
  int64_t _tmp_2378;
  _M0FPB18double__max__value = *(double*)&_tmp_2374;
  _tmp_2375 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_2375;
  _tmp_2376 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_2376;
  _tmp_2377 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_2377;
  _tmp_2378 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_2378;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1149;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1112;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1113;
  int32_t _M0L7_2abindS1114;
  struct _M0TUsiE** _M0L7_2abindS1115;
  int32_t _M0L6_2acntS2224;
  int32_t _M0L2__S1116;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1149
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1112
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1112)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 59, 0);
  _M0L12async__testsS1112->$0 = _M0L6_2atmpS1149;
  _M0L12async__testsS1112->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1113
  = _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1114 = _M0L7_2abindS1113->$1;
  _M0L7_2abindS1115 = _M0L7_2abindS1113->$0;
  _M0L6_2acntS2224
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1113));
  if (_M0L6_2acntS2224 > 1) {
    int32_t _M0L11_2anew__cntS2225 = _M0L6_2acntS2224 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1113), _M0L11_2anew__cntS2225);
    moonbit_incref_cycle_free(_M0L7_2abindS1115);
  } else if (_M0L6_2acntS2224 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1113);
  }
  _M0L2__S1116 = 0;
  while (1) {
    if (_M0L2__S1116 < _M0L7_2abindS1114) {
      struct _M0TUsiE* _M0L3argS1117 =
        (struct _M0TUsiE*)_M0L7_2abindS1115[_M0L2__S1116];
      moonbit_string_t _M0L6_2atmpS1146 = _M0L3argS1117->$0;
      int32_t _M0L6_2atmpS1147 = _M0L3argS1117->$1;
      int32_t _M0L6_2atmpS1148;
      moonbit_incref_cycle_free(_M0L6_2atmpS1146);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples25oja__rule__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1112, _M0L6_2atmpS1146, _M0L6_2atmpS1147);
      moonbit_decref_cycle_free(_M0L6_2atmpS1146);
      _M0L6_2atmpS1148 = _M0L2__S1116 + 1;
      _M0L2__S1116 = _M0L6_2atmpS1148;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1115);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\oja_rule\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples25oja__rule__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples25oja__rule__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1112);
  moonbit_decref_cycle_free(_M0L12async__testsS1112);
  moonbit_flush_cycles();
  return 0;
}