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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples27out__degree__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

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

struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TUdiE;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples27out__degree__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0TPB5ArrayGsE;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1045;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1040;

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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples27out__degree__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples27out__degree__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1045 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0TP26RiantR8snn__mbt7Xoshiro {
  uint64_t $0;
  uint64_t $1;
  uint64_t $2;
  uint64_t $3;
  
};

struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1040 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1052(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1045(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1040(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1017(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1010(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples27out__degree__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

int32_t _M0FP46RiantR8snn__mbt8examples11out__degree9summarize(
  moonbit_string_t,
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
);

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR8row__nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*,
  int32_t
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

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t,
  struct _M0TPB5ArrayGfE*
);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t
);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

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

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t
);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(int32_t);

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

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*
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
} const moonbit_string_literal_27 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_25 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_18 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[12]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 11, 44, 34, 
    109, 101, 115, 115, 97, 103, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[53]; 
} const moonbit_string_literal_41 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[6]; 
} const moonbit_string_literal_12 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 5, 32, 115, 
    116, 100, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_24 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[11]; 
} const moonbit_string_literal_11 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 10, 32, 109, 
    101, 97, 110, 95, 111, 117, 116, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_19 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_17 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[25]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 24, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 114, 101, 115, 117, 108, 116, 34, 
    44, 34, 102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_15 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[6]; 
} const moonbit_string_literal_13 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 5, 32, 109, 
    105, 110, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_28 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[115]; 
} const moonbit_string_literal_40 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 114, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 111, 117, 116, 95, 100, 101, 103, 
    114, 101, 101, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 
    115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 
    68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 
    74, 115, 69, 114, 114, 111, 114, 46, 77, 111, 111, 110, 66, 105, 
    116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 
    101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[6]; 
} const moonbit_string_literal_14 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 5, 32, 109, 
    97, 120, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_37 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_23 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[7]; 
} const moonbit_string_literal_10 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 6, 58, 32, 
    110, 110, 122, 61, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_26 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 73, 110, 115, 112, 101, 
    99, 116, 69, 114, 114, 111, 114, 46, 73, 110, 115, 112, 101, 99, 
    116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    115, 114, 99, 46, 108, 101, 110, 103, 116, 104, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_9 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 32, 32, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[33]; 
} const moonbit_string_literal_7 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 32, 45, 45, 
    45, 45, 45, 32, 69, 78, 68, 32, 77, 79, 79, 78, 32, 84, 69, 83, 84, 
    32, 82, 69, 83, 85, 76, 84, 32, 45, 45, 45, 45, 45, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_32 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[117]; 
} const moonbit_string_literal_39 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 116, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 111, 117, 116, 95, 100, 101, 103, 
    114, 101, 101, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 
    115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 
    68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 
    83, 107, 105, 112, 84, 101, 115, 116, 46, 77, 111, 111, 110, 66, 
    105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 
    116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 
    0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_21 =
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
} const moonbit_string_literal_16 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_36 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_20 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1052$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1052
  };

uint32_t const moonbit_layout_table_data[50] =
  {
    sizeof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1040)
    / 4, 1,
    offsetof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1040, $1)
    / 4
    * 2,
    sizeof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1045)
    / 4, 1,
    offsetof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1045, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2160
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1073,
  moonbit_string_t _M0L8filenameS1042,
  int32_t _M0L5indexS1044
) {
  struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1040* _closure_2188;
  struct _M0TWEu* _M0L13handle__startS1040;
  struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1045* _closure_2189;
  struct _M0TWssbEu* _M0L14handle__resultS1045;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1052;
  void* _M0L11_2atry__errS1067;
  struct moonbit_result_0 _tmp_2191;
  int32_t _handle__error__result_2192;
  int32_t _M0L6_2atmpS2148;
  void* _M0L3errS1068;
  moonbit_string_t _M0L4nameS1070;
  struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1071;
  moonbit_string_t _M0L7_2anameS1072;
  int32_t _M0L6_2acntS2182;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1042);
  _closure_2188
  = (struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1040*)moonbit_malloc(sizeof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1040));
  Moonbit_object_header(_closure_2188)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2188->code
  = &_M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1040;
  _closure_2188->$0 = _M0L5indexS1044;
  _closure_2188->$1 = _M0L8filenameS1042;
  _M0L13handle__startS1040 = (struct _M0TWEu*)_closure_2188;
  moonbit_incref_cycle_free(_M0L8filenameS1042);
  _closure_2189
  = (struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1045*)moonbit_malloc(sizeof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1045));
  Moonbit_object_header(_closure_2189)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2189->code
  = &_M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1045;
  _closure_2189->$0 = _M0L5indexS1044;
  _closure_2189->$1 = _M0L8filenameS1042;
  _M0L14handle__resultS1045 = (struct _M0TWssbEu*)_closure_2189;
  _M0L17error__to__stringS1052
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1052$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2191
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1073, _M0L8filenameS1042, _M0L5indexS1044, _M0L13handle__startS1040, _M0L14handle__resultS1045, _M0L17error__to__stringS1052);
  if (_tmp_2191.tag) {
    int32_t const _M0L5_2aokS2157 = _tmp_2191.data.ok;
    _handle__error__result_2192 = _M0L5_2aokS2157;
  } else {
    void* const _M0L6_2aerrS2158 = _tmp_2191.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1052);
    moonbit_decref_cycle_free(_M0L13handle__startS1040);
    _M0L11_2atry__errS1067 = _M0L6_2aerrS2158;
    goto join_1066;
  }
  if (_handle__error__result_2192) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1052);
    moonbit_decref_cycle_free(_M0L13handle__startS1040);
    _M0L6_2atmpS2148 = 1;
  } else {
    struct moonbit_result_0 _tmp_2193;
    int32_t _handle__error__result_2194;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2193
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1073, _M0L8filenameS1042, _M0L5indexS1044, _M0L13handle__startS1040, _M0L14handle__resultS1045, _M0L17error__to__stringS1052);
    if (_tmp_2193.tag) {
      int32_t const _M0L5_2aokS2155 = _tmp_2193.data.ok;
      _handle__error__result_2194 = _M0L5_2aokS2155;
    } else {
      void* const _M0L6_2aerrS2156 = _tmp_2193.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1052);
      moonbit_decref_cycle_free(_M0L13handle__startS1040);
      _M0L11_2atry__errS1067 = _M0L6_2aerrS2156;
      goto join_1066;
    }
    if (_handle__error__result_2194) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1052);
      moonbit_decref_cycle_free(_M0L13handle__startS1040);
      _M0L6_2atmpS2148 = 1;
    } else {
      struct moonbit_result_0 _tmp_2195;
      int32_t _handle__error__result_2196;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2195
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1073, _M0L8filenameS1042, _M0L5indexS1044, _M0L13handle__startS1040, _M0L14handle__resultS1045, _M0L17error__to__stringS1052);
      if (_tmp_2195.tag) {
        int32_t const _M0L5_2aokS2153 = _tmp_2195.data.ok;
        _handle__error__result_2196 = _M0L5_2aokS2153;
      } else {
        void* const _M0L6_2aerrS2154 = _tmp_2195.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1052);
        moonbit_decref_cycle_free(_M0L13handle__startS1040);
        _M0L11_2atry__errS1067 = _M0L6_2aerrS2154;
        goto join_1066;
      }
      if (_handle__error__result_2196) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1052);
        moonbit_decref_cycle_free(_M0L13handle__startS1040);
        _M0L6_2atmpS2148 = 1;
      } else {
        struct moonbit_result_0 _tmp_2197;
        int32_t _handle__error__result_2198;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2197
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1073, _M0L8filenameS1042, _M0L5indexS1044, _M0L13handle__startS1040, _M0L14handle__resultS1045, _M0L17error__to__stringS1052);
        if (_tmp_2197.tag) {
          int32_t const _M0L5_2aokS2151 = _tmp_2197.data.ok;
          _handle__error__result_2198 = _M0L5_2aokS2151;
        } else {
          void* const _M0L6_2aerrS2152 = _tmp_2197.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1052);
          moonbit_decref_cycle_free(_M0L13handle__startS1040);
          _M0L11_2atry__errS1067 = _M0L6_2aerrS2152;
          goto join_1066;
        }
        if (_handle__error__result_2198) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1052);
          moonbit_decref_cycle_free(_M0L13handle__startS1040);
          _M0L6_2atmpS2148 = 1;
        } else {
          struct moonbit_result_0 _tmp_2199;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2199
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1073, _M0L8filenameS1042, _M0L5indexS1044, _M0L13handle__startS1040, _M0L14handle__resultS1045, _M0L17error__to__stringS1052);
          moonbit_decref_cycle_free(_M0L13handle__startS1040);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1052);
          if (_tmp_2199.tag) {
            int32_t const _M0L5_2aokS2149 = _tmp_2199.data.ok;
            _M0L6_2atmpS2148 = _M0L5_2aokS2149;
          } else {
            void* const _M0L6_2aerrS2150 = _tmp_2199.data.err;
            _M0L11_2atry__errS1067 = _M0L6_2aerrS2150;
            goto join_1066;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2148) {
    void* _M0L130RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2159 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L130RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2159)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L130RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2159)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1067
    = _M0L130RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2159;
    goto join_1066;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1045);
  }
  goto joinlet_2190;
  join_1066:;
  _M0L3errS1068 = _M0L11_2atry__errS1067;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1071
  = (struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1068;
  _M0L7_2anameS1072 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1071->$0;
  _M0L6_2acntS2182
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1071));
  if (_M0L6_2acntS2182 > 1) {
    int32_t _M0L11_2anew__cntS2183 = _M0L6_2acntS2182 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1071), _M0L11_2anew__cntS2183);
    moonbit_incref_cycle_free(_M0L7_2anameS1072);
  } else if (_M0L6_2acntS2182 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1071);
  }
  _M0L4nameS1070 = _M0L7_2anameS1072;
  goto join_1069;
  goto joinlet_2200;
  join_1069:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1045(_M0L14handle__resultS1045, _M0L4nameS1070, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1045);
  moonbit_decref_cycle_free(_M0L4nameS1070);
  joinlet_2200:;
  joinlet_2190:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1052(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2147,
  void* _M0L3errS1053
) {
  void* _M0L1eS1055;
  moonbit_string_t _M0L1eS1057;
  moonbit_string_t _result_2203;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1053)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1058 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1053;
      moonbit_string_t _M0L4_2aeS1059 = _M0L10_2aFailureS1058->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1059);
      _M0L1eS1057 = _M0L4_2aeS1059;
      goto join_1056;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1060 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1053;
      moonbit_string_t _M0L4_2aeS1061 = _M0L15_2aInspectErrorS1060->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1061);
      _M0L1eS1057 = _M0L4_2aeS1061;
      goto join_1056;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1062 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1053;
      moonbit_string_t _M0L4_2aeS1063 = _M0L16_2aSnapshotErrorS1062->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1063);
      _M0L1eS1057 = _M0L4_2aeS1063;
      goto join_1056;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1064 =
        (struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1053;
      moonbit_string_t _M0L4_2aeS1065 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1064->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1065);
      _M0L1eS1057 = _M0L4_2aeS1065;
      goto join_1056;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1053);
      _M0L1eS1055 = _M0L3errS1053;
      goto join_1054;
      break;
    }
  }
  join_1056:;
  return _M0L1eS1057;
  join_1054:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _result_2203 = _M0FP15Error10to__string(_M0L1eS1055);
  moonbit_decref_cycle_free(_M0L1eS1055);
  return _result_2203;
}

int32_t _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1045(
  struct _M0TWssbEu* _M0L6_2aenvS2144,
  moonbit_string_t _M0L10__testnameS1046,
  moonbit_string_t _M0L7messageS1047,
  int32_t _M0L7skippedS1048
) {
  struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1045* _M0L14_2acasted__envS2145;
  moonbit_string_t _M0L8filenameS1042;
  int32_t _M0L5indexS1044;
  moonbit_string_t _M0L10file__nameS1049;
  moonbit_string_t _M0L7messageS1050;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1051;
  moonbit_string_t _M0L6_2atmpS2146;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2145
  = (struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1045*)_M0L6_2aenvS2144;
  _M0L8filenameS1042 = _M0L14_2acasted__envS2145->$1;
  _M0L5indexS1044 = _M0L14_2acasted__envS2145->$0;
  if (!_M0L7skippedS1048 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1049
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1042, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1050
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1047, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1051
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1051, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1051, _M0L10file__nameS1049);
  moonbit_decref_cycle_free(_M0L10file__nameS1049);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1051, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1051, _M0L5indexS1044);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1051, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1051, _M0L7messageS1050);
  moonbit_decref_cycle_free(_M0L7messageS1050);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1051, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2146
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1051);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1051);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2146);
  moonbit_decref_cycle_free(_M0L6_2atmpS2146);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1040(
  struct _M0TWEu* _M0L6_2aenvS2141
) {
  struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1040* _M0L14_2acasted__envS2142;
  moonbit_string_t _M0L8filenameS1042;
  int32_t _M0L5indexS1044;
  moonbit_string_t _M0L10file__nameS1041;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1043;
  moonbit_string_t _M0L6_2atmpS2143;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2142
  = (struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fout__degree__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1040*)_M0L6_2aenvS2141;
  _M0L8filenameS1042 = _M0L14_2acasted__envS2142->$1;
  _M0L5indexS1044 = _M0L14_2acasted__envS2142->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1041
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1042, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1043
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1043, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1043, _M0L10file__nameS1041);
  moonbit_decref_cycle_free(_M0L10file__nameS1041);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1043, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1043, _M0L5indexS1044);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1043, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2143
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1043);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1043);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2143);
  moonbit_decref_cycle_free(_M0L6_2atmpS2143);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1010;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1017;
  struct _M0TUsiE** _M0L6_2atmpS2140;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1024;
  moonbit_string_t* _M0L9cli__argsS1025;
  moonbit_string_t _M0L6_2atmpS2139;
  moonbit_string_t _M0L6_2atmpS2138;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1026;
  int32_t _M0L7_2abindS1027;
  moonbit_string_t* _M0L7_2abindS1028;
  int32_t _M0L6_2acntS2184;
  int32_t _M0L2__S1029;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1010 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1017 = 0;
  _M0L6_2atmpS2140 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1024
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1024)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1024->$0 = _M0L6_2atmpS2140;
  _M0L16file__and__indexS1024->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1025
  = _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1025)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2139 = (moonbit_string_t)_M0L9cli__argsS1025[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2139);
  moonbit_decref_cycle_free(_M0L9cli__argsS1025);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2138
  = _M0MP46RiantR8snn__mbt8examples27out__degree__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2139);
  moonbit_decref_cycle_free(_M0L6_2atmpS2139);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1026
  = _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1017(_M0L51moonbit__test__driver__internal__split__mbt__stringS1017, _M0L6_2atmpS2138, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2138);
  _M0L7_2abindS1027 = _M0L10test__argsS1026->$1;
  _M0L7_2abindS1028 = _M0L10test__argsS1026->$0;
  _M0L6_2acntS2184
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1026));
  if (_M0L6_2acntS2184 > 1) {
    int32_t _M0L11_2anew__cntS2185 = _M0L6_2acntS2184 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1026), _M0L11_2anew__cntS2185);
    moonbit_incref_cycle_free(_M0L7_2abindS1028);
  } else if (_M0L6_2acntS2184 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1026);
  }
  _M0L2__S1029 = 0;
  while (1) {
    if (_M0L2__S1029 < _M0L7_2abindS1027) {
      moonbit_string_t _M0L3argS1030 =
        (moonbit_string_t)_M0L7_2abindS1028[_M0L2__S1029];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1031;
      moonbit_string_t _M0L4fileS1032;
      moonbit_string_t _M0L5rangeS1033;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1034;
      moonbit_string_t _M0L6_2atmpS2136;
      int32_t _M0L5startS1035;
      moonbit_string_t _M0L6_2atmpS2135;
      int32_t _M0L3endS1036;
      int32_t _M0L1iS1037;
      int32_t _M0L6_2atmpS2137;
      moonbit_incref_cycle_free(_M0L3argS1030);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1031
      = _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1017(_M0L51moonbit__test__driver__internal__split__mbt__stringS1017, _M0L3argS1030, 58);
      moonbit_decref_cycle_free(_M0L3argS1030);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1032
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1031, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1033
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1031, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1031);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1034
      = _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1017(_M0L51moonbit__test__driver__internal__split__mbt__stringS1017, _M0L5rangeS1033, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1033);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2136
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1034, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1035
      = _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1010(_M0L45moonbit__test__driver__internal__parse__int__S1010, _M0L6_2atmpS2136);
      moonbit_decref_cycle_free(_M0L6_2atmpS2136);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2135
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1034, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1034);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1036
      = _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1010(_M0L45moonbit__test__driver__internal__parse__int__S1010, _M0L6_2atmpS2135);
      moonbit_decref_cycle_free(_M0L6_2atmpS2135);
      _M0L1iS1037 = _M0L5startS1035;
      while (1) {
        if (_M0L1iS1037 < _M0L3endS1036) {
          struct _M0TUsiE* _M0L8_2atupleS2133;
          int32_t _M0L6_2atmpS2134;
          moonbit_incref_cycle_free(_M0L4fileS1032);
          _M0L8_2atupleS2133
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS2133)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS2133->$0 = _M0L4fileS1032;
          _M0L8_2atupleS2133->$1 = _M0L1iS1037;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1024, _M0L8_2atupleS2133);
          _M0L6_2atmpS2134 = _M0L1iS1037 + 1;
          _M0L1iS1037 = _M0L6_2atmpS2134;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1032);
        }
        break;
      }
      _M0L6_2atmpS2137 = _M0L2__S1029 + 1;
      _M0L2__S1029 = _M0L6_2atmpS2137;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1028);
    }
    break;
  }
  return _M0L16file__and__indexS1024;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1017(
  int32_t _M0L6_2aenvS2114,
  moonbit_string_t _M0L1sS1018,
  int32_t _M0L3sepS1019
) {
  moonbit_string_t* _M0L6_2atmpS2132;
  struct _M0TPB5ArrayGsE* _M0L3resS1020;
  struct _M0TPB8MutLocalGiE* _M0L1iS1021;
  struct _M0TPB8MutLocalGiE* _M0L5startS1022;
  int32_t _M0L3valS2127;
  int32_t _M0L6_2atmpS2128;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2132 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1020
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1020)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1020->$0 = _M0L6_2atmpS2132;
  _M0L3resS1020->$1 = 0;
  _M0L1iS1021
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1021)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1021->$0 = 0;
  _M0L5startS1022
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1022)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1022->$0 = 0;
  while (1) {
    int32_t _M0L3valS2115 = _M0L1iS1021->$0;
    int32_t _M0L6_2atmpS2116 = Moonbit_array_length(_M0L1sS1018);
    if (_M0L3valS2115 < _M0L6_2atmpS2116) {
      int32_t _M0L3valS2119 = _M0L1iS1021->$0;
      int32_t _M0L6_2atmpS2118;
      int32_t _M0L6_2atmpS2117;
      int32_t _M0L3valS2126;
      int32_t _M0L6_2atmpS2125;
      if (
        _M0L3valS2119 < 0
        || _M0L3valS2119 >= Moonbit_array_length(_M0L1sS1018)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2118 = _M0L1sS1018[_M0L3valS2119];
      _M0L6_2atmpS2117 = _M0L6_2atmpS2118;
      if (_M0L6_2atmpS2117 == _M0L3sepS1019) {
        int32_t _M0L3valS2121 = _M0L5startS1022->$0;
        int32_t _M0L3valS2122 = _M0L1iS1021->$0;
        moonbit_string_t _M0L6_2atmpS2120;
        int32_t _M0L3valS2124;
        int32_t _M0L6_2atmpS2123;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS2120
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1018, _M0L3valS2121, _M0L3valS2122);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1020, _M0L6_2atmpS2120);
        _M0L3valS2124 = _M0L1iS1021->$0;
        _M0L6_2atmpS2123 = _M0L3valS2124 + 1;
        _M0L5startS1022->$0 = _M0L6_2atmpS2123;
      }
      _M0L3valS2126 = _M0L1iS1021->$0;
      _M0L6_2atmpS2125 = _M0L3valS2126 + 1;
      _M0L1iS1021->$0 = _M0L6_2atmpS2125;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1021);
    }
    break;
  }
  _M0L3valS2127 = _M0L5startS1022->$0;
  _M0L6_2atmpS2128 = Moonbit_array_length(_M0L1sS1018);
  if (_M0L3valS2127 < _M0L6_2atmpS2128) {
    int32_t _M0L3valS2130 = _M0L5startS1022->$0;
    int32_t _M0L6_2atmpS2131;
    moonbit_string_t _M0L6_2atmpS2129;
    moonbit_decref_cycle_free(_M0L5startS1022);
    _M0L6_2atmpS2131 = Moonbit_array_length(_M0L1sS1018);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS2129
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1018, _M0L3valS2130, _M0L6_2atmpS2131);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1020, _M0L6_2atmpS2129);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1022);
  }
  return _M0L3resS1020;
}

int32_t _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1010(
  int32_t _M0L6_2aenvS2107,
  moonbit_string_t _M0L1sS1011
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1012;
  int32_t _M0L3lenS1013;
  int32_t _M0L7_2abindS1014;
  int32_t _M0L1iS1015;
  int32_t _result_2208;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1012
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1012)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1012->$0 = 0;
  _M0L3lenS1013 = Moonbit_array_length(_M0L1sS1011);
  _M0L7_2abindS1014 = 0;
  _M0L1iS1015 = _M0L7_2abindS1014;
  while (1) {
    if (_M0L1iS1015 < _M0L3lenS1013) {
      int32_t _M0L3valS2112 = _M0L3resS1012->$0;
      int32_t _M0L6_2atmpS2109 = _M0L3valS2112 * 10;
      int32_t _M0L6_2atmpS2111;
      int32_t _M0L6_2atmpS2110;
      int32_t _M0L6_2atmpS2108;
      int32_t _M0L6_2atmpS2113;
      if (
        _M0L1iS1015 < 0 || _M0L1iS1015 >= Moonbit_array_length(_M0L1sS1011)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2111 = _M0L1sS1011[_M0L1iS1015];
      _M0L6_2atmpS2110 = _M0L6_2atmpS2111 - 48;
      _M0L6_2atmpS2108 = _M0L6_2atmpS2109 + _M0L6_2atmpS2110;
      _M0L3resS1012->$0 = _M0L6_2atmpS2108;
      _M0L6_2atmpS2113 = _M0L1iS1015 + 1;
      _M0L1iS1015 = _M0L6_2atmpS2113;
      continue;
    }
    break;
  }
  _result_2208 = _M0L3resS1012->$0;
  moonbit_decref_cycle_free(_M0L3resS1012);
  return _result_2208;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples27out__degree__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1009
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1009);
  return _M0L4selfS1009;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S979,
  moonbit_string_t _M0L12_2adiscard__S980,
  int32_t _M0L12_2adiscard__S981,
  struct _M0TWEu* _M0L12_2adiscard__S982,
  struct _M0TWssbEu* _M0L12_2adiscard__S983,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S984
) {
  struct moonbit_result_0 _result_2209;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _result_2209.tag = 1;
  _result_2209.data.ok = 0;
  return _result_2209;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S985,
  moonbit_string_t _M0L12_2adiscard__S986,
  int32_t _M0L12_2adiscard__S987,
  struct _M0TWEu* _M0L12_2adiscard__S988,
  struct _M0TWssbEu* _M0L12_2adiscard__S989,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S990
) {
  struct moonbit_result_0 _result_2210;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _result_2210.tag = 1;
  _result_2210.data.ok = 0;
  return _result_2210;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S991,
  moonbit_string_t _M0L12_2adiscard__S992,
  int32_t _M0L12_2adiscard__S993,
  struct _M0TWEu* _M0L12_2adiscard__S994,
  struct _M0TWssbEu* _M0L12_2adiscard__S995,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S996
) {
  struct moonbit_result_0 _result_2211;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _result_2211.tag = 1;
  _result_2211.data.ok = 0;
  return _result_2211;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S997,
  moonbit_string_t _M0L12_2adiscard__S998,
  int32_t _M0L12_2adiscard__S999,
  struct _M0TWEu* _M0L12_2adiscard__S1000,
  struct _M0TWssbEu* _M0L12_2adiscard__S1001,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1002
) {
  struct moonbit_result_0 _result_2212;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _result_2212.tag = 1;
  _result_2212.data.ok = 0;
  return _result_2212;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1003,
  moonbit_string_t _M0L12_2adiscard__S1004,
  int32_t _M0L12_2adiscard__S1005,
  struct _M0TWEu* _M0L12_2adiscard__S1006,
  struct _M0TWssbEu* _M0L12_2adiscard__S1007,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1008
) {
  struct moonbit_result_0 _result_2213;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _result_2213.tag = 1;
  _result_2213.data.ok = 0;
  return _result_2213;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S978
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples11out__degree9summarize(
  moonbit_string_t _M0L5labelS968,
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS950
) {
  int32_t _M0L4rowsS949;
  struct _M0TPB8MutLocalGiE* _M0L5totalS951;
  struct _M0TPB8MutLocalGiE* _M0L2mnS952;
  struct _M0TPB8MutLocalGiE* _M0L2mxS953;
  struct _M0TPB8MutLocalGiE* _M0L11nnz__actualS954;
  int32_t _M0L7_2abindS955;
  int32_t _M0L1iS956;
  int32_t _M0L3valS2106;
  float _M0L6_2atmpS2104;
  float _M0L6_2atmpS2105;
  float _M0L4meanS959;
  struct _M0TPB8MutLocalGfE* _M0L2ssS960;
  int32_t _M0L7_2abindS961;
  int32_t _M0L1iS962;
  float _M0L3valS2102;
  float _M0L6_2atmpS2103;
  float _M0L8varianceS966;
  float _M0L3stdS967;
  moonbit_string_t _M0L6_2atmpS2101;
  moonbit_string_t _M0L6_2atmpS2098;
  int32_t _M0L3valS2100;
  moonbit_string_t _M0L6_2atmpS2099;
  moonbit_string_t _M0L6_2atmpS2097;
  moonbit_string_t _M0L6_2atmpS2095;
  moonbit_string_t _M0L6_2atmpS2096;
  moonbit_string_t _M0L6_2atmpS2094;
  moonbit_string_t _M0L6_2atmpS2092;
  moonbit_string_t _M0L6_2atmpS2093;
  moonbit_string_t _M0L6_2atmpS2091;
  moonbit_string_t _M0L6_2atmpS2088;
  int32_t _M0L3valS2090;
  moonbit_string_t _M0L6_2atmpS2089;
  moonbit_string_t _M0L6_2atmpS2087;
  moonbit_string_t _M0L6_2atmpS2084;
  int32_t _M0L3valS2086;
  moonbit_string_t _M0L6_2atmpS2085;
  moonbit_string_t _M0L6_2atmpS2083;
  #line 14 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
  _M0L4rowsS949 = _M0L1mS950->$0;
  _M0L5totalS951
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5totalS951)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5totalS951->$0 = 0;
  _M0L2mnS952
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2mnS952)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2mnS952->$0 = 1000000;
  _M0L2mxS953
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2mxS953)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2mxS953->$0 = -1;
  _M0L11nnz__actualS954
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L11nnz__actualS954)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L11nnz__actualS954->$0 = 0;
  _M0L7_2abindS955 = 0;
  _M0L1iS956 = _M0L7_2abindS955;
  while (1) {
    if (_M0L1iS956 < _M0L4rowsS949) {
      int32_t _M0L1nS957;
      int32_t _M0L3valS2072;
      int32_t _M0L6_2atmpS2071;
      int32_t _M0L3valS2073;
      int32_t _M0L3valS2074;
      int32_t _M0L3valS2076;
      int32_t _M0L6_2atmpS2075;
      int32_t _M0L6_2atmpS2077;
      #line 21 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
      _M0L1nS957
      = _M0MP26RiantR8snn__mbt15SparseMatrixCSR8row__nnz(_M0L1mS950, _M0L1iS956);
      _M0L3valS2072 = _M0L11nnz__actualS954->$0;
      _M0L6_2atmpS2071 = _M0L3valS2072 + _M0L1nS957;
      _M0L11nnz__actualS954->$0 = _M0L6_2atmpS2071;
      _M0L3valS2073 = _M0L2mnS952->$0;
      if (_M0L1nS957 < _M0L3valS2073) {
        _M0L2mnS952->$0 = _M0L1nS957;
      }
      _M0L3valS2074 = _M0L2mxS953->$0;
      if (_M0L1nS957 > _M0L3valS2074) {
        _M0L2mxS953->$0 = _M0L1nS957;
      }
      _M0L3valS2076 = _M0L5totalS951->$0;
      _M0L6_2atmpS2075 = _M0L3valS2076 + _M0L1nS957;
      _M0L5totalS951->$0 = _M0L6_2atmpS2075;
      _M0L6_2atmpS2077 = _M0L1iS956 + 1;
      _M0L1iS956 = _M0L6_2atmpS2077;
      continue;
    }
    break;
  }
  _M0L3valS2106 = _M0L5totalS951->$0;
  moonbit_decref_cycle_free(_M0L5totalS951);
  _M0L6_2atmpS2104 = (float)_M0L3valS2106;
  _M0L6_2atmpS2105 = (float)_M0L4rowsS949;
  _M0L4meanS959 = _M0L6_2atmpS2104 / _M0L6_2atmpS2105;
  _M0L2ssS960
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L2ssS960)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2ssS960->$0 = 0x0p+0f;
  _M0L7_2abindS961 = 0;
  _M0L1iS962 = _M0L7_2abindS961;
  while (1) {
    if (_M0L1iS962 < _M0L4rowsS949) {
      int32_t _M0L6_2atmpS2081;
      float _M0L1nS963;
      float _M0L1dS964;
      float _M0L3valS2079;
      float _M0L6_2atmpS2080;
      float _M0L6_2atmpS2078;
      int32_t _M0L6_2atmpS2082;
      #line 31 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
      _M0L6_2atmpS2081
      = _M0MP26RiantR8snn__mbt15SparseMatrixCSR8row__nnz(_M0L1mS950, _M0L1iS962);
      _M0L1nS963 = (float)_M0L6_2atmpS2081;
      _M0L1dS964 = _M0L1nS963 - _M0L4meanS959;
      _M0L3valS2079 = _M0L2ssS960->$0;
      _M0L6_2atmpS2080 = _M0L1dS964 * _M0L1dS964;
      _M0L6_2atmpS2078 = _M0L3valS2079 + _M0L6_2atmpS2080;
      _M0L2ssS960->$0 = _M0L6_2atmpS2078;
      _M0L6_2atmpS2082 = _M0L1iS962 + 1;
      _M0L1iS962 = _M0L6_2atmpS2082;
      continue;
    }
    break;
  }
  _M0L3valS2102 = _M0L2ssS960->$0;
  moonbit_decref_cycle_free(_M0L2ssS960);
  _M0L6_2atmpS2103 = (float)_M0L4rowsS949;
  _M0L8varianceS966 = _M0L3valS2102 / _M0L6_2atmpS2103;
  #line 36 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
  _M0L3stdS967 = sqrtf(_M0L8varianceS966);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
  _M0L6_2atmpS2101
  = moonbit_add_string((moonbit_string_t)moonbit_string_literal_9.data, _M0L5labelS968);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
  _M0L6_2atmpS2098
  = moonbit_add_string(_M0L6_2atmpS2101, (moonbit_string_t)moonbit_string_literal_10.data);
  moonbit_decref_cycle_free(_M0L6_2atmpS2101);
  _M0L3valS2100 = _M0L11nnz__actualS954->$0;
  moonbit_decref_cycle_free(_M0L11nnz__actualS954);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
  _M0L6_2atmpS2099 = _M0MPC13int3Int18to__string_2einner(_M0L3valS2100, 10);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
  _M0L6_2atmpS2097 = moonbit_add_string(_M0L6_2atmpS2098, _M0L6_2atmpS2099);
  moonbit_decref_cycle_free(_M0L6_2atmpS2099);
  moonbit_decref_cycle_free(_M0L6_2atmpS2098);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
  _M0L6_2atmpS2095
  = moonbit_add_string(_M0L6_2atmpS2097, (moonbit_string_t)moonbit_string_literal_11.data);
  moonbit_decref_cycle_free(_M0L6_2atmpS2097);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
  _M0L6_2atmpS2096 = _M0IPC15float5FloatPB4Show10to__string(_M0L4meanS959);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
  _M0L6_2atmpS2094 = moonbit_add_string(_M0L6_2atmpS2095, _M0L6_2atmpS2096);
  moonbit_decref_cycle_free(_M0L6_2atmpS2096);
  moonbit_decref_cycle_free(_M0L6_2atmpS2095);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
  _M0L6_2atmpS2092
  = moonbit_add_string(_M0L6_2atmpS2094, (moonbit_string_t)moonbit_string_literal_12.data);
  moonbit_decref_cycle_free(_M0L6_2atmpS2094);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
  _M0L6_2atmpS2093 = _M0IPC15float5FloatPB4Show10to__string(_M0L3stdS967);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
  _M0L6_2atmpS2091 = moonbit_add_string(_M0L6_2atmpS2092, _M0L6_2atmpS2093);
  moonbit_decref_cycle_free(_M0L6_2atmpS2093);
  moonbit_decref_cycle_free(_M0L6_2atmpS2092);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
  _M0L6_2atmpS2088
  = moonbit_add_string(_M0L6_2atmpS2091, (moonbit_string_t)moonbit_string_literal_13.data);
  moonbit_decref_cycle_free(_M0L6_2atmpS2091);
  _M0L3valS2090 = _M0L2mnS952->$0;
  moonbit_decref_cycle_free(_M0L2mnS952);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
  _M0L6_2atmpS2089 = _M0MPC13int3Int18to__string_2einner(_M0L3valS2090, 10);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
  _M0L6_2atmpS2087 = moonbit_add_string(_M0L6_2atmpS2088, _M0L6_2atmpS2089);
  moonbit_decref_cycle_free(_M0L6_2atmpS2089);
  moonbit_decref_cycle_free(_M0L6_2atmpS2088);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
  _M0L6_2atmpS2084
  = moonbit_add_string(_M0L6_2atmpS2087, (moonbit_string_t)moonbit_string_literal_14.data);
  moonbit_decref_cycle_free(_M0L6_2atmpS2087);
  _M0L3valS2086 = _M0L2mxS953->$0;
  moonbit_decref_cycle_free(_M0L2mxS953);
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
  _M0L6_2atmpS2085 = _M0MPC13int3Int18to__string_2einner(_M0L3valS2086, 10);
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
  _M0L6_2atmpS2083 = moonbit_add_string(_M0L6_2atmpS2084, _M0L6_2atmpS2085);
  moonbit_decref_cycle_free(_M0L6_2atmpS2085);
  moonbit_decref_cycle_free(_M0L6_2atmpS2084);
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\main.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2083);
  moonbit_decref_cycle_free(_M0L6_2atmpS2083);
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR8row__nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS948,
  int32_t _M0L1iS947
) {
  int32_t _if__result_2216;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS2069;
  int32_t _M0L6_2atmpS2070;
  int32_t _M0L6_2atmpS2066;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS2068;
  int32_t _M0L6_2atmpS2067;
  #line 334 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  if (_M0L1iS947 < 0) {
    _if__result_2216 = 1;
  } else {
    int32_t _M0L4rowsS2065 = _M0L1mS948->$0;
    _if__result_2216 = _M0L1iS947 >= _M0L4rowsS2065;
  }
  if (_if__result_2216) {
    return 0;
  }
  _M0L6rowptrS2069 = _M0L1mS948->$2;
  _M0L6_2atmpS2070 = _M0L1iS947 + 1;
  #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2066
  = _M0MPC15array5Array2atGiE(_M0L6rowptrS2069, _M0L6_2atmpS2070);
  _M0L6rowptrS2068 = _M0L1mS948->$2;
  #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2067 = _M0MPC15array5Array2atGiE(_M0L6rowptrS2068, _M0L1iS947);
  return _M0L6_2atmpS2066 - _M0L6_2atmpS2067;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS861,
  int32_t _M0L4colsS865,
  float _M0L2muS871,
  float _M0L5sigmaS872,
  float _M0L1pS884,
  int32_t _M0L4ruleS878,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS874
) {
  float* _M0L6_2atmpS2064;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2063;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS860;
  int32_t _M0L7_2abindS862;
  int32_t _M0L1iS863;
  int32_t _M0L6_2atmpS2062;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS937;
  int32_t* _M0L6_2atmpS2061;
  struct _M0TPB5ArrayGiE* _M0L6colptrS938;
  float* _M0L6_2atmpS2060;
  struct _M0TPB5ArrayGfE* _M0L4valsS939;
  int32_t _M0L7_2abindS940;
  int32_t _M0L1iS941;
  int32_t _M0L6_2atmpS2059;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_2236;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2064 = moonbit_empty_float_array;
  _M0L6_2atmpS2063
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2063)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2063->$0 = _M0L6_2atmpS2064;
  _M0L6_2atmpS2063->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS860
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS861, _M0L6_2atmpS2063);
  _M0L7_2abindS862 = 0;
  _M0L1iS863 = _M0L7_2abindS862;
  while (1) {
    if (_M0L1iS863 < _M0L4rowsS861) {
      struct _M0TPB5ArrayGfE* _M0L3rowS864;
      int32_t _M0L7_2abindS866;
      int32_t _M0L1jS867;
      int32_t _M0L6_2atmpS2015;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS864 = _M0MPC15array5Array4makeGfE(_M0L4colsS865, 0x0p+0f);
      _M0L7_2abindS866 = 0;
      _M0L1jS867 = _M0L7_2abindS866;
      while (1) {
        if (_M0L1jS867 < _M0L4colsS865) {
          double _M0L2z1S869;
          struct _M0TUddE* _M0L7_2abindS873;
          double _M0L5_2az1S875;
          float _M0L6_2atmpS2013;
          float _M0L6_2atmpS2012;
          float _M0L1wS870;
          int32_t _M0L6_2atmpS2014;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS873
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS874);
          _M0L5_2az1S875 = _M0L7_2abindS873->$0;
          moonbit_decref_cycle_free(_M0L7_2abindS873);
          _M0L2z1S869 = _M0L5_2az1S875;
          goto join_868;
          goto joinlet_2219;
          join_868:;
          _M0L6_2atmpS2013 = (float)_M0L2z1S869;
          _M0L6_2atmpS2012 = _M0L5sigmaS872 * _M0L6_2atmpS2013;
          _M0L1wS870 = _M0L2muS871 + _M0L6_2atmpS2012;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS864, _M0L1jS867, _M0L1wS870);
          joinlet_2219:;
          _M0L6_2atmpS2014 = _M0L1jS867 + 1;
          _M0L1jS867 = _M0L6_2atmpS2014;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS860, _M0L1iS863, _M0L3rowS864);
      _M0L6_2atmpS2015 = _M0L1iS863 + 1;
      _M0L1iS863 = _M0L6_2atmpS2015;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS878) {
    case 0: {
      int32_t _M0L7_2abindS879 = 0;
      int32_t _M0L1iS880 = _M0L7_2abindS879;
      while (1) {
        if (_M0L1iS880 < _M0L4rowsS861) {
          int32_t _M0L7_2abindS881 = 0;
          int32_t _M0L1jS882 = _M0L7_2abindS881;
          int32_t _M0L6_2atmpS2018;
          while (1) {
            if (_M0L1jS882 < _M0L4colsS865) {
              float _M0L1uS883;
              int32_t _M0L6_2atmpS2017;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS883 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS874);
              if (_M0L1uS883 >= _M0L1pS884) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2016;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2016
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS860, _M0L1iS880);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2016, _M0L1jS882, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2016);
              }
              _M0L6_2atmpS2017 = _M0L1jS882 + 1;
              _M0L1jS882 = _M0L6_2atmpS2017;
              continue;
            }
            break;
          }
          _M0L6_2atmpS2018 = _M0L1iS880 + 1;
          _M0L1iS880 = _M0L6_2atmpS2018;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS2036 = (float)_M0L4rowsS861;
      float _M0L6_2atmpS2035 = _M0L6_2atmpS2036 * _M0L1pS884;
      int32_t _M0L7n__keepS887;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS887 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2035);
      if (_M0L7n__keepS887 > 0 && _M0L7n__keepS887 <= _M0L4rowsS861) {
        int32_t _M0L7_2abindS888 = 0;
        int32_t _M0L1jS889 = _M0L7_2abindS888;
        while (1) {
          if (_M0L1jS889 < _M0L4colsS865) {
            int32_t* _M0L6_2atmpS2030 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS890 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS891;
            int32_t _M0L1kS892;
            int32_t _M0L7n__dropS894;
            int32_t _M0L7_2abindS895;
            int32_t _M0L1kS896;
            int32_t _M0L7_2abindS902;
            int32_t _M0L1kS903;
            int32_t _M0L6_2atmpS2031;
            Moonbit_object_header(_M0L8pre__idxS890)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
            _M0L8pre__idxS890->$0 = _M0L6_2atmpS2030;
            _M0L8pre__idxS890->$1 = 0;
            _M0L7_2abindS891 = 0;
            _M0L1kS892 = _M0L7_2abindS891;
            while (1) {
              if (_M0L1kS892 < _M0L4rowsS861) {
                int32_t _M0L6_2atmpS2019;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS890, _M0L1kS892);
                _M0L6_2atmpS2019 = _M0L1kS892 + 1;
                _M0L1kS892 = _M0L6_2atmpS2019;
                continue;
              }
              break;
            }
            _M0L7n__dropS894 = _M0L4rowsS861 - _M0L7n__keepS887;
            _M0L7_2abindS895 = 0;
            _M0L1kS896 = _M0L7_2abindS895;
            while (1) {
              if (_M0L1kS896 < _M0L7n__dropS894) {
                float _M0L1uS897;
                float _M0L6_2atmpS2023;
                float _M0L6_2atmpS2025;
                float _M0L6_2atmpS2024;
                float _M0L6_2atmpS2022;
                int32_t _M0L6_2atmpS2021;
                int32_t _M0L6r__idxS898;
                int32_t _M0L10r__clampedS899;
                int32_t _M0L3tmpS900;
                int32_t _M0L6_2atmpS2020;
                int32_t _M0L6_2atmpS2026;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS897 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS874);
                _M0L6_2atmpS2023 = (float)_M0L4rowsS861;
                _M0L6_2atmpS2025 = (float)_M0L1kS896;
                _M0L6_2atmpS2024 = _M0L6_2atmpS2025 * _M0L1uS897;
                _M0L6_2atmpS2022 = _M0L6_2atmpS2023 - _M0L6_2atmpS2024;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2021
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2022);
                _M0L6r__idxS898 = _M0L1kS896 + _M0L6_2atmpS2021;
                if (_M0L6r__idxS898 >= _M0L4rowsS861) {
                  _M0L10r__clampedS899 = _M0L4rowsS861 - 1;
                } else {
                  _M0L10r__clampedS899 = _M0L6r__idxS898;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS900
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS890, _M0L1kS896);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2020
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS890, _M0L10r__clampedS899);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS890, _M0L1kS896, _M0L6_2atmpS2020);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS890, _M0L10r__clampedS899, _M0L3tmpS900);
                _M0L6_2atmpS2026 = _M0L1kS896 + 1;
                _M0L1kS896 = _M0L6_2atmpS2026;
                continue;
              }
              break;
            }
            _M0L7_2abindS902 = 0;
            _M0L1kS903 = _M0L7_2abindS902;
            while (1) {
              if (_M0L1kS903 < _M0L7n__dropS894) {
                int32_t _M0L6_2atmpS2028;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2027;
                int32_t _M0L6_2atmpS2029;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2028
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS890, _M0L1kS903);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2027
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS860, _M0L6_2atmpS2028);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2027, _M0L1jS889, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2027);
                _M0L6_2atmpS2029 = _M0L1kS903 + 1;
                _M0L1kS903 = _M0L6_2atmpS2029;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L8pre__idxS890);
              }
              break;
            }
            _M0L6_2atmpS2031 = _M0L1jS889 + 1;
            _M0L1jS889 = _M0L6_2atmpS2031;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS887 == 0) {
        int32_t _M0L7_2abindS906 = 0;
        int32_t _M0L1iS907 = _M0L7_2abindS906;
        while (1) {
          if (_M0L1iS907 < _M0L4rowsS861) {
            int32_t _M0L7_2abindS908 = 0;
            int32_t _M0L1jS909 = _M0L7_2abindS908;
            int32_t _M0L6_2atmpS2034;
            while (1) {
              if (_M0L1jS909 < _M0L4colsS865) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2032;
                int32_t _M0L6_2atmpS2033;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2032
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS860, _M0L1iS907);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2032, _M0L1jS909, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2032);
                _M0L6_2atmpS2033 = _M0L1jS909 + 1;
                _M0L1jS909 = _M0L6_2atmpS2033;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2034 = _M0L1iS907 + 1;
            _M0L1iS907 = _M0L6_2atmpS2034;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS2054 = (float)_M0L4colsS865;
      float _M0L6_2atmpS2053 = _M0L6_2atmpS2054 * _M0L1pS884;
      int32_t _M0L7n__keepS912;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS912 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2053);
      if (_M0L7n__keepS912 > 0 && _M0L7n__keepS912 <= _M0L4colsS865) {
        int32_t _M0L7_2abindS913 = 0;
        int32_t _M0L1iS914 = _M0L7_2abindS913;
        while (1) {
          if (_M0L1iS914 < _M0L4rowsS861) {
            int32_t* _M0L6_2atmpS2048 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS915 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS916;
            int32_t _M0L1kS917;
            int32_t _M0L7n__dropS919;
            int32_t _M0L7_2abindS920;
            int32_t _M0L1kS921;
            int32_t _M0L7_2abindS927;
            int32_t _M0L1kS928;
            int32_t _M0L6_2atmpS2049;
            Moonbit_object_header(_M0L9post__idxS915)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
            _M0L9post__idxS915->$0 = _M0L6_2atmpS2048;
            _M0L9post__idxS915->$1 = 0;
            _M0L7_2abindS916 = 0;
            _M0L1kS917 = _M0L7_2abindS916;
            while (1) {
              if (_M0L1kS917 < _M0L4colsS865) {
                int32_t _M0L6_2atmpS2037;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS915, _M0L1kS917);
                _M0L6_2atmpS2037 = _M0L1kS917 + 1;
                _M0L1kS917 = _M0L6_2atmpS2037;
                continue;
              }
              break;
            }
            _M0L7n__dropS919 = _M0L4colsS865 - _M0L7n__keepS912;
            _M0L7_2abindS920 = 0;
            _M0L1kS921 = _M0L7_2abindS920;
            while (1) {
              if (_M0L1kS921 < _M0L7n__dropS919) {
                float _M0L1uS922;
                float _M0L6_2atmpS2041;
                float _M0L6_2atmpS2043;
                float _M0L6_2atmpS2042;
                float _M0L6_2atmpS2040;
                int32_t _M0L6_2atmpS2039;
                int32_t _M0L6r__idxS923;
                int32_t _M0L10r__clampedS924;
                int32_t _M0L3tmpS925;
                int32_t _M0L6_2atmpS2038;
                int32_t _M0L6_2atmpS2044;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS922 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS874);
                _M0L6_2atmpS2041 = (float)_M0L4colsS865;
                _M0L6_2atmpS2043 = (float)_M0L1kS921;
                _M0L6_2atmpS2042 = _M0L6_2atmpS2043 * _M0L1uS922;
                _M0L6_2atmpS2040 = _M0L6_2atmpS2041 - _M0L6_2atmpS2042;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2039
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS2040);
                _M0L6r__idxS923 = _M0L1kS921 + _M0L6_2atmpS2039;
                if (_M0L6r__idxS923 >= _M0L4colsS865) {
                  _M0L10r__clampedS924 = _M0L4colsS865 - 1;
                } else {
                  _M0L10r__clampedS924 = _M0L6r__idxS923;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS925
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS915, _M0L1kS921);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2038
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS915, _M0L10r__clampedS924);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS915, _M0L1kS921, _M0L6_2atmpS2038);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS915, _M0L10r__clampedS924, _M0L3tmpS925);
                _M0L6_2atmpS2044 = _M0L1kS921 + 1;
                _M0L1kS921 = _M0L6_2atmpS2044;
                continue;
              }
              break;
            }
            _M0L7_2abindS927 = 0;
            _M0L1kS928 = _M0L7_2abindS927;
            while (1) {
              if (_M0L1kS928 < _M0L7n__dropS919) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2045;
                int32_t _M0L6_2atmpS2046;
                int32_t _M0L6_2atmpS2047;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2045
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS860, _M0L1iS914);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2046
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS915, _M0L1kS928);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2045, _M0L6_2atmpS2046, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2045);
                _M0L6_2atmpS2047 = _M0L1kS928 + 1;
                _M0L1kS928 = _M0L6_2atmpS2047;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L9post__idxS915);
              }
              break;
            }
            _M0L6_2atmpS2049 = _M0L1iS914 + 1;
            _M0L1iS914 = _M0L6_2atmpS2049;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS912 == 0) {
        int32_t _M0L7_2abindS931 = 0;
        int32_t _M0L1iS932 = _M0L7_2abindS931;
        while (1) {
          if (_M0L1iS932 < _M0L4rowsS861) {
            int32_t _M0L7_2abindS933 = 0;
            int32_t _M0L1jS934 = _M0L7_2abindS933;
            int32_t _M0L6_2atmpS2052;
            while (1) {
              if (_M0L1jS934 < _M0L4colsS865) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS2050;
                int32_t _M0L6_2atmpS2051;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS2050
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS860, _M0L1iS932);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS2050, _M0L1jS934, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS2050);
                _M0L6_2atmpS2051 = _M0L1jS934 + 1;
                _M0L1jS934 = _M0L6_2atmpS2051;
                continue;
              }
              break;
            }
            _M0L6_2atmpS2052 = _M0L1iS932 + 1;
            _M0L1iS932 = _M0L6_2atmpS2052;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS2062 = _M0L4rowsS861 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS937 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS2062, 0);
  _M0L6_2atmpS2061 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS938
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS938)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6colptrS938->$0 = _M0L6_2atmpS2061;
  _M0L6colptrS938->$1 = 0;
  _M0L6_2atmpS2060 = moonbit_empty_float_array;
  _M0L4valsS939
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS939)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L4valsS939->$0 = _M0L6_2atmpS2060;
  _M0L4valsS939->$1 = 0;
  _M0L7_2abindS940 = 0;
  _M0L1iS941 = _M0L7_2abindS940;
  while (1) {
    if (_M0L1iS941 < _M0L4rowsS861) {
      int32_t _M0L6_2atmpS2055;
      int32_t _M0L7_2abindS942;
      int32_t _M0L1jS943;
      int32_t _M0L6_2atmpS2058;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS2055 = _M0MPC15array5Array6lengthGfE(_M0L4valsS939);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS937, _M0L1iS941, _M0L6_2atmpS2055);
      _M0L7_2abindS942 = 0;
      _M0L1jS943 = _M0L7_2abindS942;
      while (1) {
        if (_M0L1jS943 < _M0L4colsS865) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS2056;
          float _M0L1vS944;
          int32_t _M0L6_2atmpS2057;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS2056
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS860, _M0L1iS941);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS944
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS2056, _M0L1jS943);
          moonbit_decref_cycle_free(_M0L6_2atmpS2056);
          if (_M0L1vS944 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS938, _M0L1jS943);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS939, _M0L1vS944);
          }
          _M0L6_2atmpS2057 = _M0L1jS943 + 1;
          _M0L1jS943 = _M0L6_2atmpS2057;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2058 = _M0L1iS941 + 1;
      _M0L1iS941 = _M0L6_2atmpS2058;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L5denseS860);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2059 = _M0MPC15array5Array6lengthGfE(_M0L4valsS939);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS937, _M0L4rowsS861, _M0L6_2atmpS2059);
  _block_2236
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_2236)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _block_2236->$0 = _M0L4rowsS861;
  _block_2236->$1 = _M0L4colsS865;
  _block_2236->$2 = _M0L6rowptrS937;
  _block_2236->$3 = _M0L6colptrS938;
  _block_2236->$4 = _M0L4valsS939;
  return _block_2236;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS858
) {
  struct _M0TUmmmmE* _M0L1sS857;
  uint64_t _M0L6_2atmpS2011;
  struct _M0TUmmmmE* _M0L1tS859;
  uint64_t _M0L6_2atmpS2007;
  uint64_t _M0L6_2atmpS2008;
  uint64_t _M0L6_2atmpS2009;
  uint64_t _M0L6_2atmpS2010;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2237;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS857 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS858);
  _M0L6_2atmpS2011 = _M0L1sS857->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS859 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2011);
  _M0L6_2atmpS2007 = _M0L1sS857->$0;
  _M0L6_2atmpS2008 = _M0L1sS857->$1;
  _M0L6_2atmpS2009 = _M0L1sS857->$2;
  moonbit_decref_cycle_free(_M0L1sS857);
  _M0L6_2atmpS2010 = _M0L1tS859->$0;
  moonbit_decref_cycle_free(_M0L1tS859);
  _block_2237
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2237)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2237->$0 = _M0L6_2atmpS2007;
  _block_2237->$1 = _M0L6_2atmpS2008;
  _block_2237->$2 = _M0L6_2atmpS2009;
  _block_2237->$3 = _M0L6_2atmpS2010;
  return _block_2237;
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
  struct _M0TUmmmmE* _block_2238;
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
  _block_2238 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2238)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2238->$0 = _M0L2z1S850;
  _block_2238->$1 = _M0L2z2S852;
  _block_2238->$2 = _M0L2z3S854;
  _block_2238->$3 = _M0L2z4S856;
  return _block_2238;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS846) {
  uint64_t _M0L6_2atmpS2006;
  uint64_t _M0L6_2atmpS2005;
  uint64_t _M0L1zS845;
  uint64_t _M0L6_2atmpS2004;
  uint64_t _M0L6_2atmpS2003;
  uint64_t _M0L1zS847;
  uint64_t _M0L6_2atmpS2002;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2006 = _M0L1zS846 >> 30;
  _M0L6_2atmpS2005 = _M0L1zS846 ^ _M0L6_2atmpS2006;
  _M0L1zS845 = _M0L6_2atmpS2005 * 13787848793156543929ull;
  _M0L6_2atmpS2004 = _M0L1zS845 >> 27;
  _M0L6_2atmpS2003 = _M0L1zS845 ^ _M0L6_2atmpS2004;
  _M0L1zS847 = _M0L6_2atmpS2003 * 10723151780598845931ull;
  _M0L6_2atmpS2002 = _M0L1zS847 >> 31;
  return _M0L1zS847 ^ _M0L6_2atmpS2002;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS840
) {
  double _M0L2u1S839;
  double _M0L8u1__safeS841;
  double _M0L2u2S842;
  double _M0L6_2atmpS2001;
  double _M0L6_2atmpS2000;
  double _M0L1rS843;
  double _M0L5thetaS844;
  double _M0L6_2atmpS1999;
  double _M0L6_2atmpS1996;
  double _M0L6_2atmpS1998;
  double _M0L6_2atmpS1997;
  struct _M0TUddE* _block_2239;
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
  _M0L6_2atmpS2001 = _M0FPC14math2ln(_M0L8u1__safeS841);
  _M0L6_2atmpS2000 = -0x1p+1 * _M0L6_2atmpS2001;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS843 = sqrt(_M0L6_2atmpS2000);
  _M0L5thetaS844 = 0x1.921fb54442d18p+2 * _M0L2u2S842;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1999 = _M0FPC14math3cos(_M0L5thetaS844);
  _M0L6_2atmpS1996 = _M0L1rS843 * _M0L6_2atmpS1999;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1998 = _M0FPC14math3sin(_M0L5thetaS844);
  _M0L6_2atmpS1997 = _M0L1rS843 * _M0L6_2atmpS1998;
  _block_2239 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_2239)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2239->$0 = _M0L6_2atmpS1996;
  _block_2239->$1 = _M0L6_2atmpS1997;
  return _block_2239;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS837
) {
  uint64_t _M0L1uS836;
  uint64_t _M0L4bitsS838;
  double _M0L6_2atmpS1995;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS836 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS837);
  _M0L4bitsS838 = _M0L1uS836 >> 11;
  _M0L6_2atmpS1995 = (double)_M0L4bitsS838;
  return _M0L6_2atmpS1995 * 0x1p-53;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS834
) {
  uint32_t _M0L1uS833;
  uint32_t _M0L4bitsS835;
  double _M0L6_2atmpS1994;
  double _M0L6_2atmpS1993;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS833 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS834);
  _M0L4bitsS835 = _M0L1uS833 >> 8;
  _M0L6_2atmpS1994 = (double)_M0L4bitsS835;
  _M0L6_2atmpS1993 = _M0L6_2atmpS1994 * 0x1p-24;
  return (float)_M0L6_2atmpS1993;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS832
) {
  uint64_t _M0L1uS831;
  uint64_t _M0L6_2atmpS1992;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS831 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS832);
  _M0L6_2atmpS1992 = _M0L1uS831 >> 32;
  return (uint32_t)_M0L6_2atmpS1992;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS824
) {
  uint64_t _M0L2s0S823;
  uint64_t _M0L2s1S825;
  uint64_t _M0L2s2S826;
  uint64_t _M0L2s3S827;
  uint64_t _M0L3tmpS828;
  uint64_t _M0L6_2atmpS1991;
  uint64_t _M0L3resS829;
  uint64_t _M0L1tS830;
  uint64_t _M0L6_2atmpS1981;
  uint64_t _M0L6_2atmpS1982;
  uint64_t _M0L2s2S1984;
  uint64_t _M0L6_2atmpS1983;
  uint64_t _M0L2s3S1986;
  uint64_t _M0L6_2atmpS1985;
  uint64_t _M0L2s2S1988;
  uint64_t _M0L6_2atmpS1987;
  uint64_t _M0L2s3S1990;
  uint64_t _M0L6_2atmpS1989;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S823 = _M0L1rS824->$0;
  _M0L2s1S825 = _M0L1rS824->$1;
  _M0L2s2S826 = _M0L1rS824->$2;
  _M0L2s3S827 = _M0L1rS824->$3;
  _M0L3tmpS828 = _M0L2s0S823 + _M0L2s3S827;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1991 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS828, 23);
  _M0L3resS829 = _M0L6_2atmpS1991 + _M0L2s0S823;
  _M0L1tS830 = _M0L2s1S825 << 17;
  _M0L6_2atmpS1981 = _M0L2s2S826 ^ _M0L2s0S823;
  _M0L1rS824->$2 = _M0L6_2atmpS1981;
  _M0L6_2atmpS1982 = _M0L2s3S827 ^ _M0L2s1S825;
  _M0L1rS824->$3 = _M0L6_2atmpS1982;
  _M0L2s2S1984 = _M0L1rS824->$2;
  _M0L6_2atmpS1983 = _M0L2s1S825 ^ _M0L2s2S1984;
  _M0L1rS824->$1 = _M0L6_2atmpS1983;
  _M0L2s3S1986 = _M0L1rS824->$3;
  _M0L6_2atmpS1985 = _M0L2s0S823 ^ _M0L2s3S1986;
  _M0L1rS824->$0 = _M0L6_2atmpS1985;
  _M0L2s2S1988 = _M0L1rS824->$2;
  _M0L6_2atmpS1987 = _M0L2s2S1988 ^ _M0L1tS830;
  _M0L1rS824->$2 = _M0L6_2atmpS1987;
  _M0L2s3S1990 = _M0L1rS824->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1989 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S1990, 45);
  _M0L1rS824->$3 = _M0L6_2atmpS1989;
  return _M0L3resS829;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS821, int32_t _M0L1kS822) {
  uint64_t _M0L6_2atmpS1978;
  int32_t _M0L6_2atmpS1980;
  uint64_t _M0L6_2atmpS1979;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1978 = _M0L1xS821 << (_M0L1kS822 & 63);
  _M0L6_2atmpS1980 = 64 - _M0L1kS822;
  _M0L6_2atmpS1979 = _M0L1xS821 >> (_M0L6_2atmpS1980 & 63);
  return _M0L6_2atmpS1978 | _M0L6_2atmpS1979;
}

double _M0FPC14math2ln(double _M0L1xS807) {
  struct _M0TUdiE* _M0L7_2abindS808;
  double _M0L5_2af1S809;
  int32_t _M0L5_2akiS810;
  double _M0L1fS812;
  double _M0L1kS813;
  double _M0L6_2atmpS1971;
  double _M0L1sS814;
  double _M0L2s2S815;
  double _M0L2s4S816;
  double _M0L6_2atmpS1970;
  double _M0L6_2atmpS1969;
  double _M0L6_2atmpS1968;
  double _M0L6_2atmpS1967;
  double _M0L6_2atmpS1966;
  double _M0L6_2atmpS1965;
  double _M0L2t1S817;
  double _M0L6_2atmpS1964;
  double _M0L6_2atmpS1963;
  double _M0L6_2atmpS1962;
  double _M0L6_2atmpS1961;
  double _M0L2t2S818;
  double _M0L1rS819;
  double _M0L6_2atmpS1960;
  double _M0L4hfsqS820;
  double _M0L6_2atmpS1953;
  double _M0L6_2atmpS1959;
  double _M0L6_2atmpS1957;
  double _M0L6_2atmpS1958;
  double _M0L6_2atmpS1956;
  double _M0L6_2atmpS1955;
  double _M0L6_2atmpS1954;
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
    double _M0L6_2atmpS1975 = _M0L5_2af1S809 * 0x1p+1;
    double _M0L6_2atmpS1972 = _M0L6_2atmpS1975 - 0x1p+0;
    int32_t _M0L6_2atmpS1974 = _M0L5_2akiS810 - 1;
    double _M0L6_2atmpS1973 = (double)_M0L6_2atmpS1974;
    _M0L1fS812 = _M0L6_2atmpS1972;
    _M0L1kS813 = _M0L6_2atmpS1973;
    goto join_811;
  } else {
    double _M0L6_2atmpS1976 = _M0L5_2af1S809 - 0x1p+0;
    double _M0L6_2atmpS1977 = (double)_M0L5_2akiS810;
    _M0L1fS812 = _M0L6_2atmpS1976;
    _M0L1kS813 = _M0L6_2atmpS1977;
    goto join_811;
  }
  join_811:;
  _M0L6_2atmpS1971 = 0x1p+1 + _M0L1fS812;
  _M0L1sS814 = _M0L1fS812 / _M0L6_2atmpS1971;
  _M0L2s2S815 = _M0L1sS814 * _M0L1sS814;
  _M0L2s4S816 = _M0L2s2S815 * _M0L2s2S815;
  _M0L6_2atmpS1970 = _M0L2s4S816 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS1969 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS1970;
  _M0L6_2atmpS1968 = _M0L2s4S816 * _M0L6_2atmpS1969;
  _M0L6_2atmpS1967 = 0x1.2492494229359p-2 + _M0L6_2atmpS1968;
  _M0L6_2atmpS1966 = _M0L2s4S816 * _M0L6_2atmpS1967;
  _M0L6_2atmpS1965 = 0x1.5555555555593p-1 + _M0L6_2atmpS1966;
  _M0L2t1S817 = _M0L2s2S815 * _M0L6_2atmpS1965;
  _M0L6_2atmpS1964 = _M0L2s4S816 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS1963 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS1964;
  _M0L6_2atmpS1962 = _M0L2s4S816 * _M0L6_2atmpS1963;
  _M0L6_2atmpS1961 = 0x1.999999997fa04p-2 + _M0L6_2atmpS1962;
  _M0L2t2S818 = _M0L2s4S816 * _M0L6_2atmpS1961;
  _M0L1rS819 = _M0L2t1S817 + _M0L2t2S818;
  _M0L6_2atmpS1960 = 0x1p-1 * _M0L1fS812;
  _M0L4hfsqS820 = _M0L6_2atmpS1960 * _M0L1fS812;
  _M0L6_2atmpS1953 = _M0L1kS813 * 0x1.62e42feep-1;
  _M0L6_2atmpS1959 = _M0L4hfsqS820 + _M0L1rS819;
  _M0L6_2atmpS1957 = _M0L1sS814 * _M0L6_2atmpS1959;
  _M0L6_2atmpS1958 = _M0L1kS813 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS1956 = _M0L6_2atmpS1957 + _M0L6_2atmpS1958;
  _M0L6_2atmpS1955 = _M0L4hfsqS820 - _M0L6_2atmpS1956;
  _M0L6_2atmpS1954 = _M0L6_2atmpS1955 - _M0L1fS812;
  return _M0L6_2atmpS1953 - _M0L6_2atmpS1954;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS800) {
  struct _M0TUdiE* _M0L7_2abindS801;
  double _M0L10_2anorm__fS802;
  int32_t _M0L6_2aexpS803;
  uint64_t _M0L1uS804;
  uint64_t _M0L6_2atmpS1952;
  uint64_t _M0L6_2atmpS1951;
  int32_t _M0L6_2atmpS1950;
  int32_t _M0L6_2atmpS1949;
  int32_t _M0L3expS805;
  uint64_t _M0L6_2atmpS1948;
  uint64_t _M0L6_2atmpS1947;
  uint64_t _M0L6_2atmpS1946;
  double _M0L4fracS806;
  struct _M0TUdiE* _block_2242;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS800 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS800)
    || _M0MPC16double6Double7is__nan(_M0L1fS800)
  ) {
    struct _M0TUdiE* _block_2241 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2241)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2241->$0 = _M0L1fS800;
    _block_2241->$1 = 0;
    return _block_2241;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS801 = _M0FPC14math9normalize(_M0L1fS800);
  _M0L10_2anorm__fS802 = _M0L7_2abindS801->$0;
  _M0L6_2aexpS803 = _M0L7_2abindS801->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS801);
  _M0L1uS804 = *(int64_t*)&_M0L10_2anorm__fS802;
  _M0L6_2atmpS1952 = _M0L1uS804 >> 52;
  _M0L6_2atmpS1951 = _M0L6_2atmpS1952 & 2047ull;
  _M0L6_2atmpS1950 = (int32_t)_M0L6_2atmpS1951;
  _M0L6_2atmpS1949 = _M0L6_2aexpS803 + _M0L6_2atmpS1950;
  _M0L3expS805 = _M0L6_2atmpS1949 - 1022;
  _M0L6_2atmpS1948 = ~9218868437227405312ull;
  _M0L6_2atmpS1947 = _M0L1uS804 & _M0L6_2atmpS1948;
  _M0L6_2atmpS1946 = _M0L6_2atmpS1947 | 4602678819172646912ull;
  _M0L4fracS806 = *(double*)&_M0L6_2atmpS1946;
  _block_2242 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2242)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2242->$0 = _M0L4fracS806;
  _block_2242->$1 = _M0L3expS805;
  return _block_2242;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS799) {
  double _M0L6_2atmpS1943;
  struct _M0TUdiE* _block_2244;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS1943 = fabs(_M0L1fS799);
  if (_M0L6_2atmpS1943 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS1945 = (double)4503599627370496ll;
    double _M0L6_2atmpS1944 = _M0L1fS799 * _M0L6_2atmpS1945;
    struct _M0TUdiE* _block_2243 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2243)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2243->$0 = _M0L6_2atmpS1944;
    _block_2243->$1 = -52;
    return _block_2243;
  }
  _block_2244 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2244)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2244->$0 = _M0L1fS799;
  _block_2244->$1 = 0;
  return _block_2244;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS798) {
  double _M0L6_2atmpS1942;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1942 = (double)_M0L4selfS798;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1942);
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

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS783,
  struct _M0TPB5ArrayGfE* _M0L4elemS785
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS782;
  int32_t _M0L1iS784;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS782
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS783);
  _M0L1iS784 = 0;
  while (1) {
    if (_M0L1iS784 < _M0L3lenS783) {
      struct _M0TPB5ArrayGfE** _M0L3bufS1936 = _M0L3arrS782->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS2161 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS1936[_M0L1iS784];
      int32_t _M0L6_2atmpS1937;
      moonbit_incref_cycle_free(_M0L4elemS785);
      if (_M0L6_2aoldS2161) {
        moonbit_decref_cycle_free(_M0L6_2aoldS2161);
      }
      _M0L3bufS1936[_M0L1iS784] = _M0L4elemS785;
      _M0L6_2atmpS1937 = _M0L1iS784 + 1;
      _M0L1iS784 = _M0L6_2atmpS1937;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS785);
    }
    break;
  }
  return _M0L3arrS782;
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS788,
  float _M0L4elemS790
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS787;
  int32_t _M0L1iS789;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS787 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS788);
  _M0L1iS789 = 0;
  while (1) {
    if (_M0L1iS789 < _M0L3lenS788) {
      float* _M0L3bufS1938 = _M0L3arrS787->$0;
      int32_t _M0L6_2atmpS1939;
      _M0L3bufS1938[_M0L1iS789] = _M0L4elemS790;
      _M0L6_2atmpS1939 = _M0L1iS789 + 1;
      _M0L1iS789 = _M0L6_2atmpS1939;
      continue;
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
      int32_t* _M0L3bufS1940 = _M0L3arrS792->$0;
      int32_t _M0L6_2atmpS1941;
      _M0L3bufS1940[_M0L1iS794] = _M0L4elemS795;
      _M0L6_2atmpS1941 = _M0L1iS794 + 1;
      _M0L1iS794 = _M0L6_2atmpS1941;
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
    float* _M0L6_2atmpS1933;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1933 = _M0MPC15array5Array6bufferGfE(_M0L4selfS771);
    _M0L6_2atmpS1933[_M0L5indexS772] = _M0L5valueS773;
    moonbit_decref_cycle_free(_M0L6_2atmpS1933);
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
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1934;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS2162;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1934
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS775);
    _M0L6_2aoldS2162
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1934[_M0L5indexS776];
    if (_M0L6_2aoldS2162) {
      moonbit_decref_cycle_free(_M0L6_2aoldS2162);
    }
    _M0L6_2atmpS1934[_M0L5indexS776] = _M0L5valueS777;
    moonbit_decref_cycle_free(_M0L6_2atmpS1934);
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
    int32_t* _M0L6_2atmpS1935;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1935 = _M0MPC15array5Array6bufferGiE(_M0L4selfS779);
    _M0L6_2atmpS1935[_M0L5indexS780] = _M0L5valueS781;
    moonbit_decref_cycle_free(_M0L6_2atmpS1935);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS759,
  int32_t _M0L5indexS760
) {
  int32_t _M0L3lenS758;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS758 = _M0L4selfS759->$1;
  if (_M0L5indexS760 >= 0 && _M0L5indexS760 < _M0L3lenS758) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1929;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS2163;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1929
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS759);
    _M0L6_2atmpS2163
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1929[_M0L5indexS760];
    if (_M0L6_2atmpS2163) {
      moonbit_incref_cycle_free(_M0L6_2atmpS2163);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS1929);
    return _M0L6_2atmpS2163;
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
    int32_t* _M0L6_2atmpS1930;
    int32_t _result_2248;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1930 = _M0MPC15array5Array6bufferGiE(_M0L4selfS762);
    _result_2248 = (int32_t)_M0L6_2atmpS1930[_M0L5indexS763];
    moonbit_decref_cycle_free(_M0L6_2atmpS1930);
    return _result_2248;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS765,
  int32_t _M0L5indexS766
) {
  int32_t _M0L3lenS764;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS764 = _M0L4selfS765->$1;
  if (_M0L5indexS766 >= 0 && _M0L5indexS766 < _M0L3lenS764) {
    float* _M0L6_2atmpS1931;
    float _result_2249;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1931 = _M0MPC15array5Array6bufferGfE(_M0L4selfS765);
    _result_2249 = (float)_M0L6_2atmpS1931[_M0L5indexS766];
    moonbit_decref_cycle_free(_M0L6_2atmpS1931);
    return _result_2249;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS768,
  int32_t _M0L5indexS769
) {
  int32_t _M0L3lenS767;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS767 = _M0L4selfS768->$1;
  if (_M0L5indexS769 >= 0 && _M0L5indexS769 < _M0L3lenS767) {
    moonbit_string_t* _M0L6_2atmpS1932;
    moonbit_string_t _M0L6_2atmpS2164;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1932 = _M0MPC15array5Array6bufferGsE(_M0L4selfS768);
    _M0L6_2atmpS2164 = (moonbit_string_t)_M0L6_2atmpS1932[_M0L5indexS769];
    moonbit_incref_cycle_free(_M0L6_2atmpS2164);
    moonbit_decref_cycle_free(_M0L6_2atmpS1932);
    return _M0L6_2atmpS2164;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS757) {
  moonbit_string_t _M0L6_2atmpS1928;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1928 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS757);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1928);
  moonbit_decref_cycle_free(_M0L6_2atmpS1928);
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
  uint64_t _M0L6_2atmpS1927;
  uint64_t _M0L6_2atmpS1926;
  int32_t _M0L8ieeeSignS743;
  uint64_t _M0L12ieeeMantissaS744;
  uint64_t _M0L6_2atmpS1925;
  uint64_t _M0L6_2atmpS1924;
  int32_t _M0L12ieeeExponentS745;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS746;
  struct _M0TPB17FloatingDecimal64* _M0L1vS747;
  moonbit_string_t _result_2251;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS739 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_15.data;
  }
  if (_M0L3valS739 >= -0x1p+53 && _M0L3valS739 <= 0x1p+53) {
    if (_M0L3valS739 >= -0x1p+31 && _M0L3valS739 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS740;
      double _M0L6_2atmpS1913;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS740 = _M0MPC16double6Double7to__int(_M0L3valS739);
      _M0L6_2atmpS1913 = (double)_M0L1iS740;
      if (_M0L6_2atmpS1913 == _M0L3valS739) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS740, 10);
      }
    } else {
      int64_t _M0L1iS741;
      double _M0L6_2atmpS1914;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS741 = _M0MPC16double6Double9to__int64(_M0L3valS739);
      _M0L6_2atmpS1914 = (double)_M0L1iS741;
      if (_M0L6_2atmpS1914 == _M0L3valS739) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS741, 10);
      }
    }
  }
  _M0L4bitsS742 = *(int64_t*)&_M0L3valS739;
  _M0L6_2atmpS1927 = _M0L4bitsS742 >> 63;
  _M0L6_2atmpS1926 = _M0L6_2atmpS1927 & 1ull;
  _M0L8ieeeSignS743 = _M0L6_2atmpS1926 != 0ull;
  _M0L12ieeeMantissaS744 = _M0L4bitsS742 & 4503599627370495ull;
  _M0L6_2atmpS1925 = _M0L4bitsS742 >> 52;
  _M0L6_2atmpS1924 = _M0L6_2atmpS1925 & 2047ull;
  _M0L12ieeeExponentS745 = (int32_t)_M0L6_2atmpS1924;
  if (
    _M0L12ieeeExponentS745 == 2047
    || _M0L12ieeeExponentS745 == 0 && _M0L12ieeeMantissaS744 == 0ull
  ) {
    int32_t _M0L6_2atmpS1915 = _M0L12ieeeExponentS745 != 0;
    int32_t _M0L6_2atmpS1916 = _M0L12ieeeMantissaS744 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS743, _M0L6_2atmpS1915, _M0L6_2atmpS1916);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS746
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS744, _M0L12ieeeExponentS745);
  if (_M0L7_2abindS746 == 0) {
    uint32_t _M0L6_2atmpS1917;
    if (_M0L7_2abindS746) {
      moonbit_decref_cycle_free(_M0L7_2abindS746);
    }
    _M0L6_2atmpS1917 = *(uint32_t*)&_M0L12ieeeExponentS745;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS747 = _M0FPB3d2d(_M0L12ieeeMantissaS744, _M0L6_2atmpS1917);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS748 = _M0L7_2abindS746;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS749 = _M0L7_2aSomeS748;
    struct _M0TPB17FloatingDecimal64* _M0L1xS750 = _M0L4_2afS749;
    while (1) {
      uint64_t _M0L8mantissaS1923 = _M0L1xS750->$0;
      uint64_t _M0L1qS751 = _M0L8mantissaS1923 / 10ull;
      uint64_t _M0L8mantissaS1921 = _M0L1xS750->$0;
      uint64_t _M0L6_2atmpS1922 = 10ull * _M0L1qS751;
      uint64_t _M0L1rS752 = _M0L8mantissaS1921 - _M0L6_2atmpS1922;
      int32_t _M0L8exponentS1920;
      int32_t _M0L6_2atmpS1919;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1918;
      if (_M0L1rS752 != 0ull) {
        _M0L1vS747 = _M0L1xS750;
        break;
      }
      _M0L8exponentS1920 = _M0L1xS750->$1;
      moonbit_decref_cycle_free(_M0L1xS750);
      _M0L6_2atmpS1919 = _M0L8exponentS1920 + 1;
      _M0L6_2atmpS1918
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1918)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1918->$0 = _M0L1qS751;
      _M0L6_2atmpS1918->$1 = _M0L6_2atmpS1919;
      _M0L1xS750 = _M0L6_2atmpS1918;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2251 = _M0FPB9to__chars(_M0L1vS747, _M0L8ieeeSignS743);
  moonbit_decref_cycle_free(_M0L1vS747);
  return _result_2251;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS734,
  int32_t _M0L12ieeeExponentS736
) {
  uint64_t _M0L2m2S733;
  int32_t _M0L6_2atmpS1912;
  int32_t _M0L2e2S735;
  int32_t _M0L6_2atmpS1911;
  uint64_t _M0L6_2atmpS1910;
  uint64_t _M0L4maskS737;
  uint64_t _M0L8fractionS738;
  int32_t _M0L6_2atmpS1909;
  uint64_t _M0L6_2atmpS1908;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1907;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S733 = 4503599627370496ull | _M0L12ieeeMantissaS734;
  _M0L6_2atmpS1912 = _M0L12ieeeExponentS736 - 1023;
  _M0L2e2S735 = _M0L6_2atmpS1912 - 52;
  if (_M0L2e2S735 > 0) {
    return 0;
  }
  if (_M0L2e2S735 < -52) {
    return 0;
  }
  _M0L6_2atmpS1911 = -_M0L2e2S735;
  _M0L6_2atmpS1910 = 1ull << (_M0L6_2atmpS1911 & 63);
  _M0L4maskS737 = _M0L6_2atmpS1910 - 1ull;
  _M0L8fractionS738 = _M0L2m2S733 & _M0L4maskS737;
  if (_M0L8fractionS738 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1909 = -_M0L2e2S735;
  _M0L6_2atmpS1908 = _M0L2m2S733 >> (_M0L6_2atmpS1909 & 63);
  _M0L6_2atmpS1907
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1907)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1907->$0 = _M0L6_2atmpS1908;
  _M0L6_2atmpS1907->$1 = 0;
  return _M0L6_2atmpS1907;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS701,
  int32_t _M0L4signS699
) {
  moonbit_bytes_t _M0L6resultS697;
  int32_t _M0Lm5indexS698;
  uint64_t _M0L6outputS700;
  int32_t _M0L7olengthS702;
  int32_t _M0L8exponentS1906;
  int32_t _M0L6_2atmpS1905;
  int32_t _M0Lm3expS703;
  int32_t _M0L6_2atmpS1904;
  int32_t _M0L6_2atmpS1902;
  int32_t _M0L18scientificNotationS704;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS697 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS698 = 0;
  if (_M0L4signS699) {
    int32_t _M0L6_2atmpS1776 = _M0Lm5indexS698;
    int32_t _M0L6_2atmpS1777;
    if (
      _M0L6_2atmpS1776 < 0
      || _M0L6_2atmpS1776 >= Moonbit_array_length(_M0L6resultS697)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS697[_M0L6_2atmpS1776] = 45;
    _M0L6_2atmpS1777 = _M0Lm5indexS698;
    _M0Lm5indexS698 = _M0L6_2atmpS1777 + 1;
  }
  _M0L6outputS700 = _M0L1vS701->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS702 = _M0FPB17decimal__length17(_M0L6outputS700);
  _M0L8exponentS1906 = _M0L1vS701->$1;
  _M0L6_2atmpS1905 = _M0L8exponentS1906 + _M0L7olengthS702;
  _M0Lm3expS703 = _M0L6_2atmpS1905 - 1;
  _M0L6_2atmpS1904 = _M0Lm3expS703;
  if (_M0L6_2atmpS1904 >= -6) {
    int32_t _M0L6_2atmpS1903 = _M0Lm3expS703;
    _M0L6_2atmpS1902 = _M0L6_2atmpS1903 < 21;
  } else {
    _M0L6_2atmpS1902 = 0;
  }
  _M0L18scientificNotationS704 = !_M0L6_2atmpS1902;
  if (_M0L18scientificNotationS704) {
    int32_t _M0L7_2abindS705 = _M0L7olengthS702 - 1;
    uint64_t _M0L6outputS706;
    int32_t _M0L1iS707 = 0;
    uint64_t _M0L6outputS708 = _M0L6outputS700;
    int32_t _M0L6_2atmpS1778;
    int32_t _M0L6_2atmpS1782;
    int32_t _M0L6_2atmpS1781;
    int32_t _M0L6_2atmpS1780;
    int32_t _M0L6_2atmpS1779;
    int32_t _M0L6_2atmpS1786;
    int32_t _M0L6_2atmpS1787;
    int32_t _M0L6_2atmpS1788;
    int32_t _M0L6_2atmpS1789;
    int32_t _M0L6_2atmpS1790;
    int32_t _M0L6_2atmpS1796;
    int32_t _M0L6_2atmpS1829;
    moonbit_string_t _result_2253;
    while (1) {
      if (_M0L1iS707 < _M0L7_2abindS705) {
        uint64_t _M0L1cS709 = _M0L6outputS708 % 10ull;
        int32_t _M0L6_2atmpS1835 = _M0Lm5indexS698;
        int32_t _M0L6_2atmpS1834 = _M0L6_2atmpS1835 + _M0L7olengthS702;
        int32_t _M0L6_2atmpS1830 = _M0L6_2atmpS1834 - _M0L1iS707;
        int32_t _M0L6_2atmpS1833 = (int32_t)_M0L1cS709;
        int32_t _M0L6_2atmpS1832 = 48 + _M0L6_2atmpS1833;
        int32_t _M0L6_2atmpS1831 = _M0L6_2atmpS1832 & 0xff;
        int32_t _M0L6_2atmpS1836;
        uint64_t _M0L6_2atmpS1837;
        if (
          _M0L6_2atmpS1830 < 0
          || _M0L6_2atmpS1830 >= Moonbit_array_length(_M0L6resultS697)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS697[_M0L6_2atmpS1830] = _M0L6_2atmpS1831;
        _M0L6_2atmpS1836 = _M0L1iS707 + 1;
        _M0L6_2atmpS1837 = _M0L6outputS708 / 10ull;
        _M0L1iS707 = _M0L6_2atmpS1836;
        _M0L6outputS708 = _M0L6_2atmpS1837;
        continue;
      } else {
        _M0L6outputS706 = _M0L6outputS708;
      }
      break;
    }
    _M0L6_2atmpS1778 = _M0Lm5indexS698;
    _M0L6_2atmpS1782 = (int32_t)_M0L6outputS706;
    _M0L6_2atmpS1781 = _M0L6_2atmpS1782 % 10;
    _M0L6_2atmpS1780 = 48 + _M0L6_2atmpS1781;
    _M0L6_2atmpS1779 = _M0L6_2atmpS1780 & 0xff;
    if (
      _M0L6_2atmpS1778 < 0
      || _M0L6_2atmpS1778 >= Moonbit_array_length(_M0L6resultS697)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS697[_M0L6_2atmpS1778] = _M0L6_2atmpS1779;
    if (_M0L7olengthS702 > 1) {
      int32_t _M0L6_2atmpS1784 = _M0Lm5indexS698;
      int32_t _M0L6_2atmpS1783 = _M0L6_2atmpS1784 + 1;
      if (
        _M0L6_2atmpS1783 < 0
        || _M0L6_2atmpS1783 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1783] = 46;
    } else {
      int32_t _M0L6_2atmpS1785 = _M0Lm5indexS698;
      _M0Lm5indexS698 = _M0L6_2atmpS1785 - 1;
    }
    _M0L6_2atmpS1786 = _M0Lm5indexS698;
    _M0L6_2atmpS1787 = _M0L7olengthS702 + 1;
    _M0Lm5indexS698 = _M0L6_2atmpS1786 + _M0L6_2atmpS1787;
    _M0L6_2atmpS1788 = _M0Lm5indexS698;
    if (
      _M0L6_2atmpS1788 < 0
      || _M0L6_2atmpS1788 >= Moonbit_array_length(_M0L6resultS697)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS697[_M0L6_2atmpS1788] = 101;
    _M0L6_2atmpS1789 = _M0Lm5indexS698;
    _M0Lm5indexS698 = _M0L6_2atmpS1789 + 1;
    _M0L6_2atmpS1790 = _M0Lm3expS703;
    if (_M0L6_2atmpS1790 < 0) {
      int32_t _M0L6_2atmpS1791 = _M0Lm5indexS698;
      int32_t _M0L6_2atmpS1792;
      int32_t _M0L6_2atmpS1793;
      if (
        _M0L6_2atmpS1791 < 0
        || _M0L6_2atmpS1791 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1791] = 45;
      _M0L6_2atmpS1792 = _M0Lm5indexS698;
      _M0Lm5indexS698 = _M0L6_2atmpS1792 + 1;
      _M0L6_2atmpS1793 = _M0Lm3expS703;
      _M0Lm3expS703 = -_M0L6_2atmpS1793;
    } else {
      int32_t _M0L6_2atmpS1794 = _M0Lm5indexS698;
      int32_t _M0L6_2atmpS1795;
      if (
        _M0L6_2atmpS1794 < 0
        || _M0L6_2atmpS1794 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1794] = 43;
      _M0L6_2atmpS1795 = _M0Lm5indexS698;
      _M0Lm5indexS698 = _M0L6_2atmpS1795 + 1;
    }
    _M0L6_2atmpS1796 = _M0Lm3expS703;
    if (_M0L6_2atmpS1796 >= 100) {
      int32_t _M0L6_2atmpS1812 = _M0Lm3expS703;
      int32_t _M0L1aS711 = _M0L6_2atmpS1812 / 100;
      int32_t _M0L6_2atmpS1811 = _M0Lm3expS703;
      int32_t _M0L6_2atmpS1810 = _M0L6_2atmpS1811 / 10;
      int32_t _M0L1bS712 = _M0L6_2atmpS1810 % 10;
      int32_t _M0L6_2atmpS1809 = _M0Lm3expS703;
      int32_t _M0L1cS713 = _M0L6_2atmpS1809 % 10;
      int32_t _M0L6_2atmpS1797 = _M0Lm5indexS698;
      int32_t _M0L6_2atmpS1799 = 48 + _M0L1aS711;
      int32_t _M0L6_2atmpS1798 = _M0L6_2atmpS1799 & 0xff;
      int32_t _M0L6_2atmpS1803;
      int32_t _M0L6_2atmpS1800;
      int32_t _M0L6_2atmpS1802;
      int32_t _M0L6_2atmpS1801;
      int32_t _M0L6_2atmpS1807;
      int32_t _M0L6_2atmpS1804;
      int32_t _M0L6_2atmpS1806;
      int32_t _M0L6_2atmpS1805;
      int32_t _M0L6_2atmpS1808;
      if (
        _M0L6_2atmpS1797 < 0
        || _M0L6_2atmpS1797 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1797] = _M0L6_2atmpS1798;
      _M0L6_2atmpS1803 = _M0Lm5indexS698;
      _M0L6_2atmpS1800 = _M0L6_2atmpS1803 + 1;
      _M0L6_2atmpS1802 = 48 + _M0L1bS712;
      _M0L6_2atmpS1801 = _M0L6_2atmpS1802 & 0xff;
      if (
        _M0L6_2atmpS1800 < 0
        || _M0L6_2atmpS1800 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1800] = _M0L6_2atmpS1801;
      _M0L6_2atmpS1807 = _M0Lm5indexS698;
      _M0L6_2atmpS1804 = _M0L6_2atmpS1807 + 2;
      _M0L6_2atmpS1806 = 48 + _M0L1cS713;
      _M0L6_2atmpS1805 = _M0L6_2atmpS1806 & 0xff;
      if (
        _M0L6_2atmpS1804 < 0
        || _M0L6_2atmpS1804 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1804] = _M0L6_2atmpS1805;
      _M0L6_2atmpS1808 = _M0Lm5indexS698;
      _M0Lm5indexS698 = _M0L6_2atmpS1808 + 3;
    } else {
      int32_t _M0L6_2atmpS1813 = _M0Lm3expS703;
      if (_M0L6_2atmpS1813 >= 10) {
        int32_t _M0L6_2atmpS1823 = _M0Lm3expS703;
        int32_t _M0L1aS714 = _M0L6_2atmpS1823 / 10;
        int32_t _M0L6_2atmpS1822 = _M0Lm3expS703;
        int32_t _M0L1bS715 = _M0L6_2atmpS1822 % 10;
        int32_t _M0L6_2atmpS1814 = _M0Lm5indexS698;
        int32_t _M0L6_2atmpS1816 = 48 + _M0L1aS714;
        int32_t _M0L6_2atmpS1815 = _M0L6_2atmpS1816 & 0xff;
        int32_t _M0L6_2atmpS1820;
        int32_t _M0L6_2atmpS1817;
        int32_t _M0L6_2atmpS1819;
        int32_t _M0L6_2atmpS1818;
        int32_t _M0L6_2atmpS1821;
        if (
          _M0L6_2atmpS1814 < 0
          || _M0L6_2atmpS1814 >= Moonbit_array_length(_M0L6resultS697)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS697[_M0L6_2atmpS1814] = _M0L6_2atmpS1815;
        _M0L6_2atmpS1820 = _M0Lm5indexS698;
        _M0L6_2atmpS1817 = _M0L6_2atmpS1820 + 1;
        _M0L6_2atmpS1819 = 48 + _M0L1bS715;
        _M0L6_2atmpS1818 = _M0L6_2atmpS1819 & 0xff;
        if (
          _M0L6_2atmpS1817 < 0
          || _M0L6_2atmpS1817 >= Moonbit_array_length(_M0L6resultS697)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS697[_M0L6_2atmpS1817] = _M0L6_2atmpS1818;
        _M0L6_2atmpS1821 = _M0Lm5indexS698;
        _M0Lm5indexS698 = _M0L6_2atmpS1821 + 2;
      } else {
        int32_t _M0L6_2atmpS1824 = _M0Lm5indexS698;
        int32_t _M0L6_2atmpS1827 = _M0Lm3expS703;
        int32_t _M0L6_2atmpS1826 = 48 + _M0L6_2atmpS1827;
        int32_t _M0L6_2atmpS1825 = _M0L6_2atmpS1826 & 0xff;
        int32_t _M0L6_2atmpS1828;
        if (
          _M0L6_2atmpS1824 < 0
          || _M0L6_2atmpS1824 >= Moonbit_array_length(_M0L6resultS697)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS697[_M0L6_2atmpS1824] = _M0L6_2atmpS1825;
        _M0L6_2atmpS1828 = _M0Lm5indexS698;
        _M0Lm5indexS698 = _M0L6_2atmpS1828 + 1;
      }
    }
    _M0L6_2atmpS1829 = _M0Lm5indexS698;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2253
    = _M0FPB19string__from__bytes(_M0L6resultS697, 0, _M0L6_2atmpS1829);
    moonbit_decref_cycle_free(_M0L6resultS697);
    return _result_2253;
  } else {
    int32_t _M0L6_2atmpS1838 = _M0Lm3expS703;
    int32_t _M0L6_2atmpS1901;
    moonbit_string_t _result_2259;
    if (_M0L6_2atmpS1838 < 0) {
      int32_t _M0L6_2atmpS1839 = _M0Lm5indexS698;
      int32_t _M0L6_2atmpS1841;
      int32_t _M0L6_2atmpS1840;
      int32_t _M0L6_2atmpS1842;
      int32_t _M0L1iS716;
      int32_t _M0L6_2atmpS1857;
      int32_t _M0L6_2atmpS1859;
      int32_t _M0L6_2atmpS1858;
      int32_t _M0L7currentS718;
      int32_t _M0L1iS719;
      uint64_t _M0L6outputS720;
      if (
        _M0L6_2atmpS1839 < 0
        || _M0L6_2atmpS1839 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1839] = 48;
      _M0L6_2atmpS1841 = _M0Lm5indexS698;
      _M0L6_2atmpS1840 = _M0L6_2atmpS1841 + 1;
      if (
        _M0L6_2atmpS1840 < 0
        || _M0L6_2atmpS1840 >= Moonbit_array_length(_M0L6resultS697)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS697[_M0L6_2atmpS1840] = 46;
      _M0L6_2atmpS1842 = _M0Lm5indexS698;
      _M0Lm5indexS698 = _M0L6_2atmpS1842 + 2;
      _M0L1iS716 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1843 = _M0Lm3expS703;
        if (_M0L1iS716 > _M0L6_2atmpS1843) {
          int32_t _M0L6_2atmpS1846 = _M0Lm5indexS698;
          int32_t _M0L6_2atmpS1845 = _M0L6_2atmpS1846 - _M0L1iS716;
          int32_t _M0L6_2atmpS1844 = _M0L6_2atmpS1845 - 1;
          int32_t _M0L6_2atmpS1847;
          if (
            _M0L6_2atmpS1844 < 0
            || _M0L6_2atmpS1844 >= Moonbit_array_length(_M0L6resultS697)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS697[_M0L6_2atmpS1844] = 48;
          _M0L6_2atmpS1847 = _M0L1iS716 - 1;
          _M0L1iS716 = _M0L6_2atmpS1847;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1857 = _M0Lm5indexS698;
      _M0L6_2atmpS1859 = _M0Lm3expS703;
      _M0L6_2atmpS1858 = -1 - _M0L6_2atmpS1859;
      _M0L7currentS718 = _M0L6_2atmpS1857 + _M0L6_2atmpS1858;
      _M0L1iS719 = 0;
      _M0L6outputS720 = _M0L6outputS700;
      while (1) {
        if (_M0L1iS719 < _M0L7olengthS702) {
          int32_t _M0L6_2atmpS1854 = _M0L7currentS718 + _M0L7olengthS702;
          int32_t _M0L6_2atmpS1853 = _M0L6_2atmpS1854 - _M0L1iS719;
          int32_t _M0L6_2atmpS1848 = _M0L6_2atmpS1853 - 1;
          uint64_t _M0L6_2atmpS1852 = _M0L6outputS720 % 10ull;
          int32_t _M0L6_2atmpS1851 = (int32_t)_M0L6_2atmpS1852;
          int32_t _M0L6_2atmpS1850 = 48 + _M0L6_2atmpS1851;
          int32_t _M0L6_2atmpS1849 = _M0L6_2atmpS1850 & 0xff;
          int32_t _M0L6_2atmpS1855;
          uint64_t _M0L6_2atmpS1856;
          if (
            _M0L6_2atmpS1848 < 0
            || _M0L6_2atmpS1848 >= Moonbit_array_length(_M0L6resultS697)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS697[_M0L6_2atmpS1848] = _M0L6_2atmpS1849;
          _M0L6_2atmpS1855 = _M0L1iS719 + 1;
          _M0L6_2atmpS1856 = _M0L6outputS720 / 10ull;
          _M0L1iS719 = _M0L6_2atmpS1855;
          _M0L6outputS720 = _M0L6_2atmpS1856;
          continue;
        }
        break;
      }
      _M0Lm5indexS698 = _M0L7currentS718 + _M0L7olengthS702;
    } else {
      int32_t _M0L6_2atmpS1861 = _M0Lm3expS703;
      int32_t _M0L6_2atmpS1860 = _M0L6_2atmpS1861 + 1;
      if (_M0L6_2atmpS1860 >= _M0L7olengthS702) {
        int32_t _M0L1iS722 = 0;
        uint64_t _M0L6outputS723 = _M0L6outputS700;
        int32_t _M0L6_2atmpS1872;
        int32_t _M0L6_2atmpS1877;
        int32_t _M0L7_2abindS725;
        int32_t _M0L1iS726;
        int32_t _M0L6_2atmpS1878;
        int32_t _M0L6_2atmpS1881;
        int32_t _M0L6_2atmpS1880;
        int32_t _M0L6_2atmpS1879;
        while (1) {
          if (_M0L1iS722 < _M0L7olengthS702) {
            int32_t _M0L6_2atmpS1869 = _M0Lm5indexS698;
            int32_t _M0L6_2atmpS1868 = _M0L6_2atmpS1869 + _M0L7olengthS702;
            int32_t _M0L6_2atmpS1867 = _M0L6_2atmpS1868 - _M0L1iS722;
            int32_t _M0L6_2atmpS1862 = _M0L6_2atmpS1867 - 1;
            uint64_t _M0L6_2atmpS1866 = _M0L6outputS723 % 10ull;
            int32_t _M0L6_2atmpS1865 = (int32_t)_M0L6_2atmpS1866;
            int32_t _M0L6_2atmpS1864 = 48 + _M0L6_2atmpS1865;
            int32_t _M0L6_2atmpS1863 = _M0L6_2atmpS1864 & 0xff;
            int32_t _M0L6_2atmpS1870;
            uint64_t _M0L6_2atmpS1871;
            if (
              _M0L6_2atmpS1862 < 0
              || _M0L6_2atmpS1862 >= Moonbit_array_length(_M0L6resultS697)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS697[_M0L6_2atmpS1862] = _M0L6_2atmpS1863;
            _M0L6_2atmpS1870 = _M0L1iS722 + 1;
            _M0L6_2atmpS1871 = _M0L6outputS723 / 10ull;
            _M0L1iS722 = _M0L6_2atmpS1870;
            _M0L6outputS723 = _M0L6_2atmpS1871;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1872 = _M0Lm5indexS698;
        _M0Lm5indexS698 = _M0L6_2atmpS1872 + _M0L7olengthS702;
        _M0L6_2atmpS1877 = _M0Lm3expS703;
        _M0L7_2abindS725 = _M0L6_2atmpS1877 + 1;
        _M0L1iS726 = _M0L7olengthS702;
        while (1) {
          if (_M0L1iS726 < _M0L7_2abindS725) {
            int32_t _M0L6_2atmpS1875 = _M0Lm5indexS698;
            int32_t _M0L6_2atmpS1874 = _M0L6_2atmpS1875 + _M0L1iS726;
            int32_t _M0L6_2atmpS1873 = _M0L6_2atmpS1874 - _M0L7olengthS702;
            int32_t _M0L6_2atmpS1876;
            if (
              _M0L6_2atmpS1873 < 0
              || _M0L6_2atmpS1873 >= Moonbit_array_length(_M0L6resultS697)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS697[_M0L6_2atmpS1873] = 48;
            _M0L6_2atmpS1876 = _M0L1iS726 + 1;
            _M0L1iS726 = _M0L6_2atmpS1876;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1878 = _M0Lm5indexS698;
        _M0L6_2atmpS1881 = _M0Lm3expS703;
        _M0L6_2atmpS1880 = _M0L6_2atmpS1881 + 1;
        _M0L6_2atmpS1879 = _M0L6_2atmpS1880 - _M0L7olengthS702;
        _M0Lm5indexS698 = _M0L6_2atmpS1878 + _M0L6_2atmpS1879;
      } else {
        int32_t _M0L6_2atmpS1898 = _M0Lm5indexS698;
        int32_t _M0L6_2atmpS1897 = _M0L6_2atmpS1898 + 1;
        int32_t _M0L1iS728 = 0;
        int32_t _M0L7currentS729 = _M0L6_2atmpS1897;
        uint64_t _M0L6outputS730 = _M0L6outputS700;
        int32_t _M0L6_2atmpS1899;
        int32_t _M0L6_2atmpS1900;
        while (1) {
          if (_M0L1iS728 < _M0L7olengthS702) {
            int32_t _M0L6_2atmpS1893 = _M0L7olengthS702 - _M0L1iS728;
            int32_t _M0L6_2atmpS1891 = _M0L6_2atmpS1893 - 1;
            int32_t _M0L6_2atmpS1892 = _M0Lm3expS703;
            int32_t _M0L7currentS731;
            int32_t _M0L6_2atmpS1888;
            int32_t _M0L6_2atmpS1887;
            int32_t _M0L6_2atmpS1882;
            uint64_t _M0L6_2atmpS1886;
            int32_t _M0L6_2atmpS1885;
            int32_t _M0L6_2atmpS1884;
            int32_t _M0L6_2atmpS1883;
            int32_t _M0L6_2atmpS1889;
            uint64_t _M0L6_2atmpS1890;
            if (_M0L6_2atmpS1891 == _M0L6_2atmpS1892) {
              int32_t _M0L6_2atmpS1896 = _M0L7currentS729 + _M0L7olengthS702;
              int32_t _M0L6_2atmpS1895 = _M0L6_2atmpS1896 - _M0L1iS728;
              int32_t _M0L6_2atmpS1894 = _M0L6_2atmpS1895 - 1;
              if (
                _M0L6_2atmpS1894 < 0
                || _M0L6_2atmpS1894 >= Moonbit_array_length(_M0L6resultS697)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS697[_M0L6_2atmpS1894] = 46;
              _M0L7currentS731 = _M0L7currentS729 - 1;
            } else {
              _M0L7currentS731 = _M0L7currentS729;
            }
            _M0L6_2atmpS1888 = _M0L7currentS731 + _M0L7olengthS702;
            _M0L6_2atmpS1887 = _M0L6_2atmpS1888 - _M0L1iS728;
            _M0L6_2atmpS1882 = _M0L6_2atmpS1887 - 1;
            _M0L6_2atmpS1886 = _M0L6outputS730 % 10ull;
            _M0L6_2atmpS1885 = (int32_t)_M0L6_2atmpS1886;
            _M0L6_2atmpS1884 = 48 + _M0L6_2atmpS1885;
            _M0L6_2atmpS1883 = _M0L6_2atmpS1884 & 0xff;
            if (
              _M0L6_2atmpS1882 < 0
              || _M0L6_2atmpS1882 >= Moonbit_array_length(_M0L6resultS697)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS697[_M0L6_2atmpS1882] = _M0L6_2atmpS1883;
            _M0L6_2atmpS1889 = _M0L1iS728 + 1;
            _M0L6_2atmpS1890 = _M0L6outputS730 / 10ull;
            _M0L1iS728 = _M0L6_2atmpS1889;
            _M0L7currentS729 = _M0L7currentS731;
            _M0L6outputS730 = _M0L6_2atmpS1890;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1899 = _M0Lm5indexS698;
        _M0L6_2atmpS1900 = _M0L7olengthS702 + 1;
        _M0Lm5indexS698 = _M0L6_2atmpS1899 + _M0L6_2atmpS1900;
      }
    }
    _M0L6_2atmpS1901 = _M0Lm5indexS698;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2259
    = _M0FPB19string__from__bytes(_M0L6resultS697, 0, _M0L6_2atmpS1901);
    moonbit_decref_cycle_free(_M0L6resultS697);
    return _result_2259;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS643,
  uint32_t _M0L12ieeeExponentS642
) {
  int32_t _M0Lm2e2S640;
  uint64_t _M0Lm2m2S641;
  uint64_t _M0L6_2atmpS1775;
  uint64_t _M0L6_2atmpS1774;
  int32_t _M0L4evenS644;
  uint64_t _M0L6_2atmpS1773;
  uint64_t _M0L2mvS645;
  int32_t _M0L7mmShiftS646;
  uint64_t _M0Lm2vrS647;
  uint64_t _M0Lm2vpS648;
  uint64_t _M0Lm2vmS649;
  int32_t _M0Lm3e10S650;
  int32_t _M0Lm17vmIsTrailingZerosS651;
  int32_t _M0Lm17vrIsTrailingZerosS652;
  int32_t _M0L6_2atmpS1675;
  int32_t _M0Lm7removedS671;
  int32_t _M0Lm16lastRemovedDigitS672;
  uint64_t _M0Lm6outputS673;
  int32_t _M0L6_2atmpS1771;
  int32_t _M0L6_2atmpS1772;
  int32_t _M0L3expS696;
  uint64_t _M0L6_2atmpS1770;
  struct _M0TPB17FloatingDecimal64* _block_2265;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S640 = 0;
  _M0Lm2m2S641 = 0ull;
  if (_M0L12ieeeExponentS642 == 0u) {
    _M0Lm2e2S640 = -1076;
    _M0Lm2m2S641 = _M0L12ieeeMantissaS643;
  } else {
    int32_t _M0L6_2atmpS1674 = *(int32_t*)&_M0L12ieeeExponentS642;
    int32_t _M0L6_2atmpS1673 = _M0L6_2atmpS1674 - 1023;
    int32_t _M0L6_2atmpS1672 = _M0L6_2atmpS1673 - 52;
    _M0Lm2e2S640 = _M0L6_2atmpS1672 - 2;
    _M0Lm2m2S641 = 4503599627370496ull | _M0L12ieeeMantissaS643;
  }
  _M0L6_2atmpS1775 = _M0Lm2m2S641;
  _M0L6_2atmpS1774 = _M0L6_2atmpS1775 & 1ull;
  _M0L4evenS644 = _M0L6_2atmpS1774 == 0ull;
  _M0L6_2atmpS1773 = _M0Lm2m2S641;
  _M0L2mvS645 = 4ull * _M0L6_2atmpS1773;
  _M0L7mmShiftS646
  = _M0L12ieeeMantissaS643 != 0ull || _M0L12ieeeExponentS642 <= 1u;
  _M0Lm2vrS647 = 0ull;
  _M0Lm2vpS648 = 0ull;
  _M0Lm2vmS649 = 0ull;
  _M0Lm3e10S650 = 0;
  _M0Lm17vmIsTrailingZerosS651 = 0;
  _M0Lm17vrIsTrailingZerosS652 = 0;
  _M0L6_2atmpS1675 = _M0Lm2e2S640;
  if (_M0L6_2atmpS1675 >= 0) {
    int32_t _M0L6_2atmpS1697 = _M0Lm2e2S640;
    int32_t _M0L6_2atmpS1693;
    int32_t _M0L6_2atmpS1696;
    int32_t _M0L6_2atmpS1695;
    int32_t _M0L6_2atmpS1694;
    int32_t _M0L1qS653;
    int32_t _M0L6_2atmpS1692;
    int32_t _M0L6_2atmpS1691;
    int32_t _M0L1kS654;
    int32_t _M0L6_2atmpS1690;
    int32_t _M0L6_2atmpS1689;
    int32_t _M0L6_2atmpS1688;
    int32_t _M0L1iS655;
    struct _M0TPB8Pow5Pair _M0L4pow5S656;
    uint64_t _M0L6_2atmpS1687;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS657;
    uint64_t _M0L8_2avrOutS658;
    uint64_t _M0L8_2avpOutS659;
    uint64_t _M0L8_2avmOutS660;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1693 = _M0FPB9log10Pow2(_M0L6_2atmpS1697);
    _M0L6_2atmpS1696 = _M0Lm2e2S640;
    _M0L6_2atmpS1695 = _M0L6_2atmpS1696 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1694 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1695);
    _M0L1qS653 = _M0L6_2atmpS1693 - _M0L6_2atmpS1694;
    _M0Lm3e10S650 = _M0L1qS653;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1692 = _M0FPB8pow5bits(_M0L1qS653);
    _M0L6_2atmpS1691 = 125 + _M0L6_2atmpS1692;
    _M0L1kS654 = _M0L6_2atmpS1691 - 1;
    _M0L6_2atmpS1690 = _M0Lm2e2S640;
    _M0L6_2atmpS1689 = -_M0L6_2atmpS1690;
    _M0L6_2atmpS1688 = _M0L6_2atmpS1689 + _M0L1qS653;
    _M0L1iS655 = _M0L6_2atmpS1688 + _M0L1kS654;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S656 = _M0FPB22double__computeInvPow5(_M0L1qS653);
    _M0L6_2atmpS1687 = _M0Lm2m2S641;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS657
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1687, _M0L4pow5S656, _M0L1iS655, _M0L7mmShiftS646);
    _M0L8_2avrOutS658 = _M0L7_2abindS657.$0;
    _M0L8_2avpOutS659 = _M0L7_2abindS657.$1;
    _M0L8_2avmOutS660 = _M0L7_2abindS657.$2;
    _M0Lm2vrS647 = _M0L8_2avrOutS658;
    _M0Lm2vpS648 = _M0L8_2avpOutS659;
    _M0Lm2vmS649 = _M0L8_2avmOutS660;
    if (_M0L1qS653 <= 21) {
      int32_t _M0L6_2atmpS1683 = (int32_t)_M0L2mvS645;
      uint64_t _M0L6_2atmpS1686 = _M0L2mvS645 / 5ull;
      int32_t _M0L6_2atmpS1685 = (int32_t)_M0L6_2atmpS1686;
      int32_t _M0L6_2atmpS1684 = 5 * _M0L6_2atmpS1685;
      int32_t _M0L6mvMod5S661 = _M0L6_2atmpS1683 - _M0L6_2atmpS1684;
      if (_M0L6mvMod5S661 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS652
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS645, _M0L1qS653);
      } else if (_M0L4evenS644) {
        uint64_t _M0L6_2atmpS1677 = _M0L2mvS645 - 1ull;
        uint64_t _M0L6_2atmpS1678;
        uint64_t _M0L6_2atmpS1676;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1678 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS646);
        _M0L6_2atmpS1676 = _M0L6_2atmpS1677 - _M0L6_2atmpS1678;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS651
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1676, _M0L1qS653);
      } else {
        uint64_t _M0L6_2atmpS1679 = _M0Lm2vpS648;
        uint64_t _M0L6_2atmpS1682 = _M0L2mvS645 + 2ull;
        int32_t _M0L6_2atmpS1681;
        uint64_t _M0L6_2atmpS1680;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1681
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1682, _M0L1qS653);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1680 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1681);
        _M0Lm2vpS648 = _M0L6_2atmpS1679 - _M0L6_2atmpS1680;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1711 = _M0Lm2e2S640;
    int32_t _M0L6_2atmpS1710 = -_M0L6_2atmpS1711;
    int32_t _M0L6_2atmpS1705;
    int32_t _M0L6_2atmpS1709;
    int32_t _M0L6_2atmpS1708;
    int32_t _M0L6_2atmpS1707;
    int32_t _M0L6_2atmpS1706;
    int32_t _M0L1qS662;
    int32_t _M0L6_2atmpS1698;
    int32_t _M0L6_2atmpS1704;
    int32_t _M0L6_2atmpS1703;
    int32_t _M0L1iS663;
    int32_t _M0L6_2atmpS1702;
    int32_t _M0L1kS664;
    int32_t _M0L1jS665;
    struct _M0TPB8Pow5Pair _M0L4pow5S666;
    uint64_t _M0L6_2atmpS1701;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS667;
    uint64_t _M0L8_2avrOutS668;
    uint64_t _M0L8_2avpOutS669;
    uint64_t _M0L8_2avmOutS670;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1705 = _M0FPB9log10Pow5(_M0L6_2atmpS1710);
    _M0L6_2atmpS1709 = _M0Lm2e2S640;
    _M0L6_2atmpS1708 = -_M0L6_2atmpS1709;
    _M0L6_2atmpS1707 = _M0L6_2atmpS1708 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1706 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1707);
    _M0L1qS662 = _M0L6_2atmpS1705 - _M0L6_2atmpS1706;
    _M0L6_2atmpS1698 = _M0Lm2e2S640;
    _M0Lm3e10S650 = _M0L1qS662 + _M0L6_2atmpS1698;
    _M0L6_2atmpS1704 = _M0Lm2e2S640;
    _M0L6_2atmpS1703 = -_M0L6_2atmpS1704;
    _M0L1iS663 = _M0L6_2atmpS1703 - _M0L1qS662;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1702 = _M0FPB8pow5bits(_M0L1iS663);
    _M0L1kS664 = _M0L6_2atmpS1702 - 125;
    _M0L1jS665 = _M0L1qS662 - _M0L1kS664;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S666 = _M0FPB19double__computePow5(_M0L1iS663);
    _M0L6_2atmpS1701 = _M0Lm2m2S641;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS667
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1701, _M0L4pow5S666, _M0L1jS665, _M0L7mmShiftS646);
    _M0L8_2avrOutS668 = _M0L7_2abindS667.$0;
    _M0L8_2avpOutS669 = _M0L7_2abindS667.$1;
    _M0L8_2avmOutS670 = _M0L7_2abindS667.$2;
    _M0Lm2vrS647 = _M0L8_2avrOutS668;
    _M0Lm2vpS648 = _M0L8_2avpOutS669;
    _M0Lm2vmS649 = _M0L8_2avmOutS670;
    if (_M0L1qS662 <= 1) {
      _M0Lm17vrIsTrailingZerosS652 = 1;
      if (_M0L4evenS644) {
        int32_t _M0L6_2atmpS1699;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1699 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS646);
        _M0Lm17vmIsTrailingZerosS651 = _M0L6_2atmpS1699 == 1;
      } else {
        uint64_t _M0L6_2atmpS1700 = _M0Lm2vpS648;
        _M0Lm2vpS648 = _M0L6_2atmpS1700 - 1ull;
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
    int32_t _if__result_2262;
    uint64_t _M0L6_2atmpS1741;
    uint64_t _M0L6_2atmpS1747;
    uint64_t _M0L6_2atmpS1748;
    int32_t _if__result_2263;
    int32_t _M0L6_2atmpS1744;
    int64_t _M0L6_2atmpS1743;
    uint64_t _M0L6_2atmpS1742;
    while (1) {
      uint64_t _M0L6_2atmpS1724 = _M0Lm2vpS648;
      uint64_t _M0L7vpDiv10S674 = _M0L6_2atmpS1724 / 10ull;
      uint64_t _M0L6_2atmpS1723 = _M0Lm2vmS649;
      uint64_t _M0L7vmDiv10S675 = _M0L6_2atmpS1723 / 10ull;
      uint64_t _M0L6_2atmpS1722;
      int32_t _M0L6_2atmpS1719;
      int32_t _M0L6_2atmpS1721;
      int32_t _M0L6_2atmpS1720;
      int32_t _M0L7vmMod10S677;
      uint64_t _M0L6_2atmpS1718;
      uint64_t _M0L7vrDiv10S678;
      uint64_t _M0L6_2atmpS1717;
      int32_t _M0L6_2atmpS1714;
      int32_t _M0L6_2atmpS1716;
      int32_t _M0L6_2atmpS1715;
      int32_t _M0L7vrMod10S679;
      int32_t _M0L6_2atmpS1713;
      if (_M0L7vpDiv10S674 <= _M0L7vmDiv10S675) {
        break;
      }
      _M0L6_2atmpS1722 = _M0Lm2vmS649;
      _M0L6_2atmpS1719 = (int32_t)_M0L6_2atmpS1722;
      _M0L6_2atmpS1721 = (int32_t)_M0L7vmDiv10S675;
      _M0L6_2atmpS1720 = 10 * _M0L6_2atmpS1721;
      _M0L7vmMod10S677 = _M0L6_2atmpS1719 - _M0L6_2atmpS1720;
      _M0L6_2atmpS1718 = _M0Lm2vrS647;
      _M0L7vrDiv10S678 = _M0L6_2atmpS1718 / 10ull;
      _M0L6_2atmpS1717 = _M0Lm2vrS647;
      _M0L6_2atmpS1714 = (int32_t)_M0L6_2atmpS1717;
      _M0L6_2atmpS1716 = (int32_t)_M0L7vrDiv10S678;
      _M0L6_2atmpS1715 = 10 * _M0L6_2atmpS1716;
      _M0L7vrMod10S679 = _M0L6_2atmpS1714 - _M0L6_2atmpS1715;
      _M0Lm17vmIsTrailingZerosS651
      = _M0Lm17vmIsTrailingZerosS651 && _M0L7vmMod10S677 == 0;
      if (_M0Lm17vrIsTrailingZerosS652) {
        int32_t _M0L6_2atmpS1712 = _M0Lm16lastRemovedDigitS672;
        _M0Lm17vrIsTrailingZerosS652 = _M0L6_2atmpS1712 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS652 = 0;
      }
      _M0Lm16lastRemovedDigitS672 = _M0L7vrMod10S679;
      _M0Lm2vrS647 = _M0L7vrDiv10S678;
      _M0Lm2vpS648 = _M0L7vpDiv10S674;
      _M0Lm2vmS649 = _M0L7vmDiv10S675;
      _M0L6_2atmpS1713 = _M0Lm7removedS671;
      _M0Lm7removedS671 = _M0L6_2atmpS1713 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS651) {
      while (1) {
        uint64_t _M0L6_2atmpS1737 = _M0Lm2vmS649;
        uint64_t _M0L7vmDiv10S680 = _M0L6_2atmpS1737 / 10ull;
        uint64_t _M0L6_2atmpS1736 = _M0Lm2vmS649;
        int32_t _M0L6_2atmpS1733 = (int32_t)_M0L6_2atmpS1736;
        int32_t _M0L6_2atmpS1735 = (int32_t)_M0L7vmDiv10S680;
        int32_t _M0L6_2atmpS1734 = 10 * _M0L6_2atmpS1735;
        int32_t _M0L7vmMod10S681 = _M0L6_2atmpS1733 - _M0L6_2atmpS1734;
        uint64_t _M0L6_2atmpS1732;
        uint64_t _M0L7vpDiv10S683;
        uint64_t _M0L6_2atmpS1731;
        uint64_t _M0L7vrDiv10S684;
        uint64_t _M0L6_2atmpS1730;
        int32_t _M0L6_2atmpS1727;
        int32_t _M0L6_2atmpS1729;
        int32_t _M0L6_2atmpS1728;
        int32_t _M0L7vrMod10S685;
        int32_t _M0L6_2atmpS1726;
        if (_M0L7vmMod10S681 != 0) {
          break;
        }
        _M0L6_2atmpS1732 = _M0Lm2vpS648;
        _M0L7vpDiv10S683 = _M0L6_2atmpS1732 / 10ull;
        _M0L6_2atmpS1731 = _M0Lm2vrS647;
        _M0L7vrDiv10S684 = _M0L6_2atmpS1731 / 10ull;
        _M0L6_2atmpS1730 = _M0Lm2vrS647;
        _M0L6_2atmpS1727 = (int32_t)_M0L6_2atmpS1730;
        _M0L6_2atmpS1729 = (int32_t)_M0L7vrDiv10S684;
        _M0L6_2atmpS1728 = 10 * _M0L6_2atmpS1729;
        _M0L7vrMod10S685 = _M0L6_2atmpS1727 - _M0L6_2atmpS1728;
        if (_M0Lm17vrIsTrailingZerosS652) {
          int32_t _M0L6_2atmpS1725 = _M0Lm16lastRemovedDigitS672;
          _M0Lm17vrIsTrailingZerosS652 = _M0L6_2atmpS1725 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS652 = 0;
        }
        _M0Lm16lastRemovedDigitS672 = _M0L7vrMod10S685;
        _M0Lm2vrS647 = _M0L7vrDiv10S684;
        _M0Lm2vpS648 = _M0L7vpDiv10S683;
        _M0Lm2vmS649 = _M0L7vmDiv10S680;
        _M0L6_2atmpS1726 = _M0Lm7removedS671;
        _M0Lm7removedS671 = _M0L6_2atmpS1726 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS652) {
      int32_t _M0L6_2atmpS1740 = _M0Lm16lastRemovedDigitS672;
      if (_M0L6_2atmpS1740 == 5) {
        uint64_t _M0L6_2atmpS1739 = _M0Lm2vrS647;
        uint64_t _M0L6_2atmpS1738 = _M0L6_2atmpS1739 % 2ull;
        _if__result_2262 = _M0L6_2atmpS1738 == 0ull;
      } else {
        _if__result_2262 = 0;
      }
    } else {
      _if__result_2262 = 0;
    }
    if (_if__result_2262) {
      _M0Lm16lastRemovedDigitS672 = 4;
    }
    _M0L6_2atmpS1741 = _M0Lm2vrS647;
    _M0L6_2atmpS1747 = _M0Lm2vrS647;
    _M0L6_2atmpS1748 = _M0Lm2vmS649;
    if (_M0L6_2atmpS1747 == _M0L6_2atmpS1748) {
      if (!_M0L4evenS644) {
        _if__result_2263 = 1;
      } else {
        int32_t _M0L6_2atmpS1746 = _M0Lm17vmIsTrailingZerosS651;
        _if__result_2263 = !_M0L6_2atmpS1746;
      }
    } else {
      _if__result_2263 = 0;
    }
    if (_if__result_2263) {
      _M0L6_2atmpS1744 = 1;
    } else {
      int32_t _M0L6_2atmpS1745 = _M0Lm16lastRemovedDigitS672;
      _M0L6_2atmpS1744 = _M0L6_2atmpS1745 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1743 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1744);
    _M0L6_2atmpS1742 = *(uint64_t*)&_M0L6_2atmpS1743;
    _M0Lm6outputS673 = _M0L6_2atmpS1741 + _M0L6_2atmpS1742;
  } else {
    int32_t _M0Lm7roundUpS686 = 0;
    uint64_t _M0L6_2atmpS1769 = _M0Lm2vpS648;
    uint64_t _M0L8vpDiv100S687 = _M0L6_2atmpS1769 / 100ull;
    uint64_t _M0L6_2atmpS1768 = _M0Lm2vmS649;
    uint64_t _M0L8vmDiv100S688 = _M0L6_2atmpS1768 / 100ull;
    uint64_t _M0L6_2atmpS1763;
    uint64_t _M0L6_2atmpS1766;
    uint64_t _M0L6_2atmpS1767;
    int32_t _M0L6_2atmpS1765;
    uint64_t _M0L6_2atmpS1764;
    if (_M0L8vpDiv100S687 > _M0L8vmDiv100S688) {
      uint64_t _M0L6_2atmpS1754 = _M0Lm2vrS647;
      uint64_t _M0L8vrDiv100S689 = _M0L6_2atmpS1754 / 100ull;
      uint64_t _M0L6_2atmpS1753 = _M0Lm2vrS647;
      int32_t _M0L6_2atmpS1750 = (int32_t)_M0L6_2atmpS1753;
      int32_t _M0L6_2atmpS1752 = (int32_t)_M0L8vrDiv100S689;
      int32_t _M0L6_2atmpS1751 = 100 * _M0L6_2atmpS1752;
      int32_t _M0L8vrMod100S690 = _M0L6_2atmpS1750 - _M0L6_2atmpS1751;
      int32_t _M0L6_2atmpS1749;
      _M0Lm7roundUpS686 = _M0L8vrMod100S690 >= 50;
      _M0Lm2vrS647 = _M0L8vrDiv100S689;
      _M0Lm2vpS648 = _M0L8vpDiv100S687;
      _M0Lm2vmS649 = _M0L8vmDiv100S688;
      _M0L6_2atmpS1749 = _M0Lm7removedS671;
      _M0Lm7removedS671 = _M0L6_2atmpS1749 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1762 = _M0Lm2vpS648;
      uint64_t _M0L7vpDiv10S691 = _M0L6_2atmpS1762 / 10ull;
      uint64_t _M0L6_2atmpS1761 = _M0Lm2vmS649;
      uint64_t _M0L7vmDiv10S692 = _M0L6_2atmpS1761 / 10ull;
      uint64_t _M0L6_2atmpS1760;
      uint64_t _M0L7vrDiv10S694;
      uint64_t _M0L6_2atmpS1759;
      int32_t _M0L6_2atmpS1756;
      int32_t _M0L6_2atmpS1758;
      int32_t _M0L6_2atmpS1757;
      int32_t _M0L7vrMod10S695;
      int32_t _M0L6_2atmpS1755;
      if (_M0L7vpDiv10S691 <= _M0L7vmDiv10S692) {
        break;
      }
      _M0L6_2atmpS1760 = _M0Lm2vrS647;
      _M0L7vrDiv10S694 = _M0L6_2atmpS1760 / 10ull;
      _M0L6_2atmpS1759 = _M0Lm2vrS647;
      _M0L6_2atmpS1756 = (int32_t)_M0L6_2atmpS1759;
      _M0L6_2atmpS1758 = (int32_t)_M0L7vrDiv10S694;
      _M0L6_2atmpS1757 = 10 * _M0L6_2atmpS1758;
      _M0L7vrMod10S695 = _M0L6_2atmpS1756 - _M0L6_2atmpS1757;
      _M0Lm7roundUpS686 = _M0L7vrMod10S695 >= 5;
      _M0Lm2vrS647 = _M0L7vrDiv10S694;
      _M0Lm2vpS648 = _M0L7vpDiv10S691;
      _M0Lm2vmS649 = _M0L7vmDiv10S692;
      _M0L6_2atmpS1755 = _M0Lm7removedS671;
      _M0Lm7removedS671 = _M0L6_2atmpS1755 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1763 = _M0Lm2vrS647;
    _M0L6_2atmpS1766 = _M0Lm2vrS647;
    _M0L6_2atmpS1767 = _M0Lm2vmS649;
    _M0L6_2atmpS1765
    = _M0L6_2atmpS1766 == _M0L6_2atmpS1767 || _M0Lm7roundUpS686;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1764 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1765);
    _M0Lm6outputS673 = _M0L6_2atmpS1763 + _M0L6_2atmpS1764;
  }
  _M0L6_2atmpS1771 = _M0Lm3e10S650;
  _M0L6_2atmpS1772 = _M0Lm7removedS671;
  _M0L3expS696 = _M0L6_2atmpS1771 + _M0L6_2atmpS1772;
  _M0L6_2atmpS1770 = _M0Lm6outputS673;
  _block_2265
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2265)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2265->$0 = _M0L6_2atmpS1770;
  _block_2265->$1 = _M0L3expS696;
  return _block_2265;
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
  int32_t _M0L6_2atmpS1671;
  int32_t _M0L6_2atmpS1670;
  int32_t _M0L4baseS618;
  int32_t _M0L5base2S620;
  int32_t _M0L6offsetS621;
  int32_t _M0L6_2atmpS1669;
  uint64_t _M0L4mul0S622;
  int32_t _M0L6_2atmpS1668;
  int32_t _M0L6_2atmpS1667;
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
  int32_t _M0L6_2atmpS1665;
  int32_t _M0L6_2atmpS1666;
  int32_t _M0L5deltaS633;
  uint64_t _M0L6_2atmpS1664;
  uint64_t _M0L6_2atmpS1656;
  int32_t _M0L6_2atmpS1663;
  uint32_t _M0L6_2atmpS1660;
  int32_t _M0L6_2atmpS1662;
  int32_t _M0L6_2atmpS1661;
  uint32_t _M0L6_2atmpS1659;
  uint32_t _M0L6_2atmpS1658;
  uint64_t _M0L6_2atmpS1657;
  uint64_t _M0L1aS634;
  uint64_t _M0L6_2atmpS1655;
  uint64_t _M0L1bS635;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1671 = _M0L1iS619 + 26;
  _M0L6_2atmpS1670 = _M0L6_2atmpS1671 - 1;
  _M0L4baseS618 = _M0L6_2atmpS1670 / 26;
  _M0L5base2S620 = _M0L4baseS618 * 26;
  _M0L6offsetS621 = _M0L5base2S620 - _M0L1iS619;
  _M0L6_2atmpS1669 = _M0L4baseS618 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S622
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1669);
  _M0L6_2atmpS1668 = _M0L4baseS618 * 2;
  _M0L6_2atmpS1667 = _M0L6_2atmpS1668 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S623
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1667);
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
    uint64_t _M0L6_2atmpS1654 = _M0Lm5high1S632;
    _M0Lm5high1S632 = _M0L6_2atmpS1654 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1665 = _M0FPB8pow5bits(_M0L5base2S620);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1666 = _M0FPB8pow5bits(_M0L1iS619);
  _M0L5deltaS633 = _M0L6_2atmpS1665 - _M0L6_2atmpS1666;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1664
  = _M0FPB13shiftright128(_M0L7_2alow0S629, _M0L3sumS631, _M0L5deltaS633);
  _M0L6_2atmpS1656 = _M0L6_2atmpS1664 + 1ull;
  _M0L6_2atmpS1663 = _M0L1iS619 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1660
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1663);
  _M0L6_2atmpS1662 = _M0L1iS619 % 16;
  _M0L6_2atmpS1661 = _M0L6_2atmpS1662 << 1;
  _M0L6_2atmpS1659 = _M0L6_2atmpS1660 >> (_M0L6_2atmpS1661 & 31);
  _M0L6_2atmpS1658 = _M0L6_2atmpS1659 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1657 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1658);
  _M0L1aS634 = _M0L6_2atmpS1656 + _M0L6_2atmpS1657;
  _M0L6_2atmpS1655 = _M0Lm5high1S632;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS635
  = _M0FPB13shiftright128(_M0L3sumS631, _M0L6_2atmpS1655, _M0L5deltaS633);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS634, .$1 = _M0L1bS635};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS601) {
  int32_t _M0L4baseS600;
  int32_t _M0L5base2S602;
  int32_t _M0L6offsetS603;
  int32_t _M0L6_2atmpS1653;
  uint64_t _M0L4mul0S604;
  int32_t _M0L6_2atmpS1652;
  int32_t _M0L6_2atmpS1651;
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
  int32_t _M0L6_2atmpS1649;
  int32_t _M0L6_2atmpS1650;
  int32_t _M0L5deltaS615;
  uint64_t _M0L6_2atmpS1641;
  int32_t _M0L6_2atmpS1648;
  uint32_t _M0L6_2atmpS1645;
  int32_t _M0L6_2atmpS1647;
  int32_t _M0L6_2atmpS1646;
  uint32_t _M0L6_2atmpS1644;
  uint32_t _M0L6_2atmpS1643;
  uint64_t _M0L6_2atmpS1642;
  uint64_t _M0L1aS616;
  uint64_t _M0L6_2atmpS1640;
  uint64_t _M0L1bS617;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS600 = _M0L1iS601 / 26;
  _M0L5base2S602 = _M0L4baseS600 * 26;
  _M0L6offsetS603 = _M0L1iS601 - _M0L5base2S602;
  _M0L6_2atmpS1653 = _M0L4baseS600 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S604
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1653);
  _M0L6_2atmpS1652 = _M0L4baseS600 * 2;
  _M0L6_2atmpS1651 = _M0L6_2atmpS1652 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S605
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1651);
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
    uint64_t _M0L6_2atmpS1639 = _M0Lm5high1S614;
    _M0Lm5high1S614 = _M0L6_2atmpS1639 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1649 = _M0FPB8pow5bits(_M0L1iS601);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1650 = _M0FPB8pow5bits(_M0L5base2S602);
  _M0L5deltaS615 = _M0L6_2atmpS1649 - _M0L6_2atmpS1650;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1641
  = _M0FPB13shiftright128(_M0L7_2alow0S611, _M0L3sumS613, _M0L5deltaS615);
  _M0L6_2atmpS1648 = _M0L1iS601 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1645
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1648);
  _M0L6_2atmpS1647 = _M0L1iS601 % 16;
  _M0L6_2atmpS1646 = _M0L6_2atmpS1647 << 1;
  _M0L6_2atmpS1644 = _M0L6_2atmpS1645 >> (_M0L6_2atmpS1646 & 31);
  _M0L6_2atmpS1643 = _M0L6_2atmpS1644 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1642 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1643);
  _M0L1aS616 = _M0L6_2atmpS1641 + _M0L6_2atmpS1642;
  _M0L6_2atmpS1640 = _M0Lm5high1S614;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS617
  = _M0FPB13shiftright128(_M0L3sumS613, _M0L6_2atmpS1640, _M0L5deltaS615);
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
  uint64_t _M0L6_2atmpS1638;
  uint64_t _M0L2hiS582;
  uint64_t _M0L3lo2S583;
  uint64_t _M0L6_2atmpS1636;
  uint64_t _M0L6_2atmpS1637;
  uint64_t _M0L4mid2S584;
  uint64_t _M0L6_2atmpS1635;
  uint64_t _M0L3hi2S585;
  int32_t _M0L6_2atmpS1634;
  int32_t _M0L6_2atmpS1633;
  uint64_t _M0L2vpS586;
  uint64_t _M0Lm2vmS588;
  int32_t _M0L6_2atmpS1632;
  int32_t _M0L6_2atmpS1631;
  uint64_t _M0L2vrS599;
  uint64_t _M0L6_2atmpS1630;
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
    _M0L6_2atmpS1638 = 1ull;
  } else {
    _M0L6_2atmpS1638 = 0ull;
  }
  _M0L2hiS582 = _M0L6_2ahi2S580 + _M0L6_2atmpS1638;
  _M0L3lo2S583 = _M0L5_2aloS576 + _M0L7_2amul0S570;
  _M0L6_2atmpS1636 = _M0L3midS581 + _M0L7_2amul1S572;
  if (_M0L3lo2S583 < _M0L5_2aloS576) {
    _M0L6_2atmpS1637 = 1ull;
  } else {
    _M0L6_2atmpS1637 = 0ull;
  }
  _M0L4mid2S584 = _M0L6_2atmpS1636 + _M0L6_2atmpS1637;
  if (_M0L4mid2S584 < _M0L3midS581) {
    _M0L6_2atmpS1635 = 1ull;
  } else {
    _M0L6_2atmpS1635 = 0ull;
  }
  _M0L3hi2S585 = _M0L2hiS582 + _M0L6_2atmpS1635;
  _M0L6_2atmpS1634 = _M0L1jS587 - 64;
  _M0L6_2atmpS1633 = _M0L6_2atmpS1634 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS586
  = _M0FPB13shiftright128(_M0L4mid2S584, _M0L3hi2S585, _M0L6_2atmpS1633);
  _M0Lm2vmS588 = 0ull;
  if (_M0L7mmShiftS589) {
    uint64_t _M0L3lo3S590 = _M0L5_2aloS576 - _M0L7_2amul0S570;
    uint64_t _M0L6_2atmpS1620 = _M0L3midS581 - _M0L7_2amul1S572;
    uint64_t _M0L6_2atmpS1621;
    uint64_t _M0L4mid3S591;
    uint64_t _M0L6_2atmpS1619;
    uint64_t _M0L3hi3S592;
    int32_t _M0L6_2atmpS1618;
    int32_t _M0L6_2atmpS1617;
    if (_M0L5_2aloS576 < _M0L3lo3S590) {
      _M0L6_2atmpS1621 = 1ull;
    } else {
      _M0L6_2atmpS1621 = 0ull;
    }
    _M0L4mid3S591 = _M0L6_2atmpS1620 - _M0L6_2atmpS1621;
    if (_M0L3midS581 < _M0L4mid3S591) {
      _M0L6_2atmpS1619 = 1ull;
    } else {
      _M0L6_2atmpS1619 = 0ull;
    }
    _M0L3hi3S592 = _M0L2hiS582 - _M0L6_2atmpS1619;
    _M0L6_2atmpS1618 = _M0L1jS587 - 64;
    _M0L6_2atmpS1617 = _M0L6_2atmpS1618 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS588
    = _M0FPB13shiftright128(_M0L4mid3S591, _M0L3hi3S592, _M0L6_2atmpS1617);
  } else {
    uint64_t _M0L3lo3S593 = _M0L5_2aloS576 + _M0L5_2aloS576;
    uint64_t _M0L6_2atmpS1628 = _M0L3midS581 + _M0L3midS581;
    uint64_t _M0L6_2atmpS1629;
    uint64_t _M0L4mid3S594;
    uint64_t _M0L6_2atmpS1626;
    uint64_t _M0L6_2atmpS1627;
    uint64_t _M0L3hi3S595;
    uint64_t _M0L3lo4S596;
    uint64_t _M0L6_2atmpS1624;
    uint64_t _M0L6_2atmpS1625;
    uint64_t _M0L4mid4S597;
    uint64_t _M0L6_2atmpS1623;
    uint64_t _M0L3hi4S598;
    int32_t _M0L6_2atmpS1622;
    if (_M0L3lo3S593 < _M0L5_2aloS576) {
      _M0L6_2atmpS1629 = 1ull;
    } else {
      _M0L6_2atmpS1629 = 0ull;
    }
    _M0L4mid3S594 = _M0L6_2atmpS1628 + _M0L6_2atmpS1629;
    _M0L6_2atmpS1626 = _M0L2hiS582 + _M0L2hiS582;
    if (_M0L4mid3S594 < _M0L3midS581) {
      _M0L6_2atmpS1627 = 1ull;
    } else {
      _M0L6_2atmpS1627 = 0ull;
    }
    _M0L3hi3S595 = _M0L6_2atmpS1626 + _M0L6_2atmpS1627;
    _M0L3lo4S596 = _M0L3lo3S593 - _M0L7_2amul0S570;
    _M0L6_2atmpS1624 = _M0L4mid3S594 - _M0L7_2amul1S572;
    if (_M0L3lo3S593 < _M0L3lo4S596) {
      _M0L6_2atmpS1625 = 1ull;
    } else {
      _M0L6_2atmpS1625 = 0ull;
    }
    _M0L4mid4S597 = _M0L6_2atmpS1624 - _M0L6_2atmpS1625;
    if (_M0L4mid3S594 < _M0L4mid4S597) {
      _M0L6_2atmpS1623 = 1ull;
    } else {
      _M0L6_2atmpS1623 = 0ull;
    }
    _M0L3hi4S598 = _M0L3hi3S595 - _M0L6_2atmpS1623;
    _M0L6_2atmpS1622 = _M0L1jS587 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS588
    = _M0FPB13shiftright128(_M0L4mid4S597, _M0L3hi4S598, _M0L6_2atmpS1622);
  }
  _M0L6_2atmpS1632 = _M0L1jS587 - 64;
  _M0L6_2atmpS1631 = _M0L6_2atmpS1632 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS599
  = _M0FPB13shiftright128(_M0L3midS581, _M0L2hiS582, _M0L6_2atmpS1631);
  _M0L6_2atmpS1630 = _M0Lm2vmS588;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS599,
                                                .$1 = _M0L2vpS586,
                                                .$2 = _M0L6_2atmpS1630};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS568,
  int32_t _M0L1pS569
) {
  uint64_t _M0L6_2atmpS1616;
  uint64_t _M0L6_2atmpS1615;
  uint64_t _M0L6_2atmpS1614;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1616 = 1ull << (_M0L1pS569 & 63);
  _M0L6_2atmpS1615 = _M0L6_2atmpS1616 - 1ull;
  _M0L6_2atmpS1614 = _M0L5valueS568 & _M0L6_2atmpS1615;
  return _M0L6_2atmpS1614 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS566,
  int32_t _M0L1pS567
) {
  int32_t _M0L6_2atmpS1613;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1613 = _M0FPB10pow5Factor(_M0L5valueS566);
  return _M0L6_2atmpS1613 >= _M0L1pS567;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS561) {
  uint64_t _M0L6_2atmpS1604;
  uint64_t _M0L6_2atmpS1605;
  uint64_t _M0L6_2atmpS1606;
  uint64_t _M0L6_2atmpS1607;
  uint64_t _M0L6_2atmpS1612;
  int32_t _M0L5countS562;
  uint64_t _M0L1vS563;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1604 = _M0L5valueS561 % 5ull;
  if (_M0L6_2atmpS1604 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1605 = _M0L5valueS561 % 25ull;
  if (_M0L6_2atmpS1605 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1606 = _M0L5valueS561 % 125ull;
  if (_M0L6_2atmpS1606 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1607 = _M0L5valueS561 % 625ull;
  if (_M0L6_2atmpS1607 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1612 = _M0L5valueS561 / 625ull;
  _M0L5countS562 = 4;
  _M0L1vS563 = _M0L6_2atmpS1612;
  while (1) {
    if (_M0L1vS563 > 0ull) {
      uint64_t _M0L6_2atmpS1608 = _M0L1vS563 % 5ull;
      int32_t _M0L6_2atmpS1609;
      uint64_t _M0L6_2atmpS1610;
      if (_M0L6_2atmpS1608 != 0ull) {
        return _M0L5countS562;
      }
      _M0L6_2atmpS1609 = _M0L5countS562 + 1;
      _M0L6_2atmpS1610 = _M0L1vS563 / 5ull;
      _M0L5countS562 = _M0L6_2atmpS1609;
      _M0L1vS563 = _M0L6_2atmpS1610;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS565;
      moonbit_string_t _M0L6_2atmpS1611;
      int32_t _result_2267;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS565
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS565, (moonbit_string_t)moonbit_string_literal_16.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS565, _M0L5valueS561);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1611
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS565);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS565);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2267 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1611);
      moonbit_decref_cycle_free(_M0L6_2atmpS1611);
      return _result_2267;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS560,
  uint64_t _M0L2hiS558,
  int32_t _M0L4distS559
) {
  int32_t _M0L6_2atmpS1603;
  uint64_t _M0L6_2atmpS1601;
  uint64_t _M0L6_2atmpS1602;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1603 = 64 - _M0L4distS559;
  _M0L6_2atmpS1601 = _M0L2hiS558 << (_M0L6_2atmpS1603 & 63);
  _M0L6_2atmpS1602 = _M0L2loS560 >> (_M0L4distS559 & 63);
  return _M0L6_2atmpS1601 | _M0L6_2atmpS1602;
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
  uint64_t _M0L6_2atmpS1599;
  uint64_t _M0L6_2atmpS1600;
  uint64_t _M0L1yS554;
  uint64_t _M0L6_2atmpS1597;
  uint64_t _M0L6_2atmpS1598;
  uint64_t _M0L1zS555;
  uint64_t _M0L6_2atmpS1595;
  uint64_t _M0L6_2atmpS1596;
  uint64_t _M0L6_2atmpS1593;
  uint64_t _M0L6_2atmpS1594;
  uint64_t _M0L1wS556;
  uint64_t _M0L2loS557;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS547 = _M0L1aS548 & 4294967295ull;
  _M0L3aHiS549 = _M0L1aS548 >> 32;
  _M0L3bLoS550 = _M0L1bS551 & 4294967295ull;
  _M0L3bHiS552 = _M0L1bS551 >> 32;
  _M0L1xS553 = _M0L3aLoS547 * _M0L3bLoS550;
  _M0L6_2atmpS1599 = _M0L3aHiS549 * _M0L3bLoS550;
  _M0L6_2atmpS1600 = _M0L1xS553 >> 32;
  _M0L1yS554 = _M0L6_2atmpS1599 + _M0L6_2atmpS1600;
  _M0L6_2atmpS1597 = _M0L3aLoS547 * _M0L3bHiS552;
  _M0L6_2atmpS1598 = _M0L1yS554 & 4294967295ull;
  _M0L1zS555 = _M0L6_2atmpS1597 + _M0L6_2atmpS1598;
  _M0L6_2atmpS1595 = _M0L3aHiS549 * _M0L3bHiS552;
  _M0L6_2atmpS1596 = _M0L1yS554 >> 32;
  _M0L6_2atmpS1593 = _M0L6_2atmpS1595 + _M0L6_2atmpS1596;
  _M0L6_2atmpS1594 = _M0L1zS555 >> 32;
  _M0L1wS556 = _M0L6_2atmpS1593 + _M0L6_2atmpS1594;
  _M0L2loS557 = _M0L1aS548 * _M0L1bS551;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS557, .$1 = _M0L1wS556};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS545,
  int32_t _M0L4fromS542,
  int32_t _M0L2toS541
) {
  int32_t _M0L3lenS540;
  int32_t _M0L6_2atmpS1592;
  uint16_t* _M0L6bufferS543;
  int32_t _M0L1iS544;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS540 = _M0L2toS541 - _M0L4fromS542;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1592 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS543
  = (uint16_t*)moonbit_make_string(_M0L3lenS540, _M0L6_2atmpS1592);
  _M0L1iS544 = 0;
  while (1) {
    if (_M0L1iS544 < _M0L3lenS540) {
      int32_t _M0L6_2atmpS1590 = _M0L4fromS542 + _M0L1iS544;
      int32_t _M0L6_2atmpS1589;
      int32_t _M0L6_2atmpS1588;
      int32_t _M0L6_2atmpS1591;
      if (
        _M0L6_2atmpS1590 < 0
        || _M0L6_2atmpS1590 >= Moonbit_array_length(_M0L5bytesS545)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1589 = (int32_t)_M0L5bytesS545[_M0L6_2atmpS1590];
      _M0L6_2atmpS1588 = (uint16_t)_M0L6_2atmpS1589;
      if (
        _M0L1iS544 < 0 || _M0L1iS544 >= Moonbit_array_length(_M0L6bufferS543)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS543[_M0L1iS544] = _M0L6_2atmpS1588;
      _M0L6_2atmpS1591 = _M0L1iS544 + 1;
      _M0L1iS544 = _M0L6_2atmpS1591;
      continue;
    }
    break;
  }
  return _M0L6bufferS543;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS539) {
  int32_t _M0L6_2atmpS1587;
  uint32_t _M0L6_2atmpS1586;
  uint32_t _M0L6_2atmpS1585;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1587 = _M0L1eS539 * 78913;
  _M0L6_2atmpS1586 = *(uint32_t*)&_M0L6_2atmpS1587;
  _M0L6_2atmpS1585 = _M0L6_2atmpS1586 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1585;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS538) {
  int32_t _M0L6_2atmpS1584;
  uint32_t _M0L6_2atmpS1583;
  uint32_t _M0L6_2atmpS1582;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1584 = _M0L1eS538 * 732923;
  _M0L6_2atmpS1583 = *(uint32_t*)&_M0L6_2atmpS1584;
  _M0L6_2atmpS1582 = _M0L6_2atmpS1583 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1582;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS536,
  int32_t _M0L8exponentS537,
  int32_t _M0L8mantissaS534
) {
  moonbit_string_t _M0L1sS535;
  moonbit_string_t _result_2270;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS534) {
    return (moonbit_string_t)moonbit_string_literal_17.data;
  }
  if (_M0L4signS536) {
    _M0L1sS535 = (moonbit_string_t)moonbit_string_literal_18.data;
  } else {
    _M0L1sS535 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS537) {
    moonbit_string_t _result_2269;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2269
    = moonbit_add_string(_M0L1sS535, (moonbit_string_t)moonbit_string_literal_19.data);
    moonbit_decref_cycle_free(_M0L1sS535);
    return _result_2269;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2270
  = moonbit_add_string(_M0L1sS535, (moonbit_string_t)moonbit_string_literal_20.data);
  moonbit_decref_cycle_free(_M0L1sS535);
  return _result_2270;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS533) {
  int32_t _M0L6_2atmpS1581;
  uint32_t _M0L6_2atmpS1580;
  uint32_t _M0L6_2atmpS1579;
  int32_t _M0L6_2atmpS1578;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1581 = _M0L1eS533 * 1217359;
  _M0L6_2atmpS1580 = *(uint32_t*)&_M0L6_2atmpS1581;
  _M0L6_2atmpS1579 = _M0L6_2atmpS1580 >> 19;
  _M0L6_2atmpS1578 = *(int32_t*)&_M0L6_2atmpS1579;
  return _M0L6_2atmpS1578 + 1;
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

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS528
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS1575;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_2271;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1575
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS528, 0);
  _block_2271
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_2271)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 29, 0);
  _block_2271->$0 = _M0L6_2atmpS1575;
  _block_2271->$1 = _M0L3lenS528;
  return _block_2271;
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS529
) {
  float* _M0L6_2atmpS1576;
  struct _M0TPB5ArrayGfE* _block_2272;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1576 = (float*)moonbit_make_float_array_raw(_M0L3lenS529);
  _block_2272
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2272)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2272->$0 = _M0L6_2atmpS1576;
  _block_2272->$1 = _M0L3lenS529;
  return _block_2272;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS530
) {
  int32_t* _M0L6_2atmpS1577;
  struct _M0TPB5ArrayGiE* _block_2273;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1577 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS530);
  _block_2273
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2273)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _block_2273->$0 = _M0L6_2atmpS1577;
  _block_2273->$1 = _M0L3lenS530;
  return _block_2273;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS524,
  int32_t _M0L5indexS525
) {
  uint64_t* _M0L6_2atmpS1573;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1573 = _M0L4selfS524;
  if (
    _M0L5indexS525 < 0
    || _M0L5indexS525 >= Moonbit_array_length(_M0L6_2atmpS1573)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1573[_M0L5indexS525];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS526,
  int32_t _M0L5indexS527
) {
  uint32_t* _M0L6_2atmpS1574;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1574 = _M0L4selfS526;
  if (
    _M0L5indexS527 < 0
    || _M0L5indexS527 >= Moonbit_array_length(_M0L6_2atmpS1574)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1574[_M0L5indexS527];
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
  int32_t _M0L3lenS1545;
  int32_t* _M0L6_2atmpS1547;
  int32_t _M0L6_2atmpS1546;
  int32_t _M0L6lengthS510;
  int32_t* _M0L3bufS1550;
  int32_t _M0L6_2atmpS1551;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1545 = _M0L4selfS509->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1547 = _M0MPC15array5Array6bufferGiE(_M0L4selfS509);
  _M0L6_2atmpS1546 = Moonbit_array_length(_M0L6_2atmpS1547);
  moonbit_decref_cycle_free(_M0L6_2atmpS1547);
  if (_M0L3lenS1545 == _M0L6_2atmpS1546) {
    int32_t _M0L3lenS1549 = _M0L4selfS509->$1;
    int32_t _M0L6_2atmpS1548 = _M0L3lenS1549 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS509, _M0L6_2atmpS1548);
  }
  _M0L6lengthS510 = _M0L4selfS509->$1;
  _M0L3bufS1550 = _M0L4selfS509->$0;
  _M0L3bufS1550[_M0L6lengthS510] = _M0L5valueS511;
  _M0L6_2atmpS1551 = _M0L6lengthS510 + 1;
  _M0L4selfS509->$1 = _M0L6_2atmpS1551;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS512,
  float _M0L5valueS514
) {
  int32_t _M0L3lenS1552;
  float* _M0L6_2atmpS1554;
  int32_t _M0L6_2atmpS1553;
  int32_t _M0L6lengthS513;
  float* _M0L3bufS1557;
  int32_t _M0L6_2atmpS1558;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1552 = _M0L4selfS512->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1554 = _M0MPC15array5Array6bufferGfE(_M0L4selfS512);
  _M0L6_2atmpS1553 = Moonbit_array_length(_M0L6_2atmpS1554);
  moonbit_decref_cycle_free(_M0L6_2atmpS1554);
  if (_M0L3lenS1552 == _M0L6_2atmpS1553) {
    int32_t _M0L3lenS1556 = _M0L4selfS512->$1;
    int32_t _M0L6_2atmpS1555 = _M0L3lenS1556 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS512, _M0L6_2atmpS1555);
  }
  _M0L6lengthS513 = _M0L4selfS512->$1;
  _M0L3bufS1557 = _M0L4selfS512->$0;
  _M0L3bufS1557[_M0L6lengthS513] = _M0L5valueS514;
  _M0L6_2atmpS1558 = _M0L6lengthS513 + 1;
  _M0L4selfS512->$1 = _M0L6_2atmpS1558;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS515,
  moonbit_string_t _M0L5valueS517
) {
  int32_t _M0L3lenS1559;
  moonbit_string_t* _M0L6_2atmpS1561;
  int32_t _M0L6_2atmpS1560;
  int32_t _M0L6lengthS516;
  moonbit_string_t* _M0L3bufS1564;
  moonbit_string_t _M0L6_2aoldS2165;
  int32_t _M0L6_2atmpS1565;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1559 = _M0L4selfS515->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1561 = _M0MPC15array5Array6bufferGsE(_M0L4selfS515);
  _M0L6_2atmpS1560 = Moonbit_array_length(_M0L6_2atmpS1561);
  moonbit_decref_cycle_free(_M0L6_2atmpS1561);
  if (_M0L3lenS1559 == _M0L6_2atmpS1560) {
    int32_t _M0L3lenS1563 = _M0L4selfS515->$1;
    int32_t _M0L6_2atmpS1562 = _M0L3lenS1563 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS515, _M0L6_2atmpS1562);
  }
  _M0L6lengthS516 = _M0L4selfS515->$1;
  _M0L3bufS1564 = _M0L4selfS515->$0;
  _M0L6_2aoldS2165 = (moonbit_string_t)_M0L3bufS1564[_M0L6lengthS516];
  moonbit_decref_cycle_free(_M0L6_2aoldS2165);
  _M0L3bufS1564[_M0L6lengthS516] = _M0L5valueS517;
  _M0L6_2atmpS1565 = _M0L6lengthS516 + 1;
  _M0L4selfS515->$1 = _M0L6_2atmpS1565;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS518,
  struct _M0TUsiE* _M0L5valueS520
) {
  int32_t _M0L3lenS1566;
  struct _M0TUsiE** _M0L6_2atmpS1568;
  int32_t _M0L6_2atmpS1567;
  int32_t _M0L6lengthS519;
  struct _M0TUsiE** _M0L3bufS1571;
  struct _M0TUsiE* _M0L6_2aoldS2166;
  int32_t _M0L6_2atmpS1572;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1566 = _M0L4selfS518->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1568 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS518);
  _M0L6_2atmpS1567 = Moonbit_array_length(_M0L6_2atmpS1568);
  moonbit_decref_cycle_free(_M0L6_2atmpS1568);
  if (_M0L3lenS1566 == _M0L6_2atmpS1567) {
    int32_t _M0L3lenS1570 = _M0L4selfS518->$1;
    int32_t _M0L6_2atmpS1569 = _M0L3lenS1570 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS518, _M0L6_2atmpS1569);
  }
  _M0L6lengthS519 = _M0L4selfS518->$1;
  _M0L3bufS1571 = _M0L4selfS518->$0;
  _M0L6_2aoldS2166 = (struct _M0TUsiE*)_M0L3bufS1571[_M0L6lengthS519];
  if (_M0L6_2aoldS2166) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2166);
  }
  _M0L3bufS1571[_M0L6lengthS519] = _M0L5valueS520;
  _M0L6_2atmpS1572 = _M0L6lengthS519 + 1;
  _M0L4selfS518->$1 = _M0L6_2atmpS1572;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS494,
  int32_t _M0L8requiredS496
) {
  int32_t _M0L8old__capS493;
  int32_t _M0L3lenS1541;
  int32_t _M0L8new__capS495;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS493 = _M0MPC15array5Array8capacityGiE(_M0L4selfS494);
  _M0L3lenS1541 = _M0L4selfS494->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS495
  = _M0FPB23array__growth__capacity(_M0L8old__capS493, _M0L3lenS1541, _M0L8requiredS496);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS494, _M0L8new__capS495);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS498,
  int32_t _M0L8requiredS500
) {
  int32_t _M0L8old__capS497;
  int32_t _M0L3lenS1542;
  int32_t _M0L8new__capS499;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS497 = _M0MPC15array5Array8capacityGfE(_M0L4selfS498);
  _M0L3lenS1542 = _M0L4selfS498->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS499
  = _M0FPB23array__growth__capacity(_M0L8old__capS497, _M0L3lenS1542, _M0L8requiredS500);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS498, _M0L8new__capS499);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS502,
  int32_t _M0L8requiredS504
) {
  int32_t _M0L8old__capS501;
  int32_t _M0L3lenS1543;
  int32_t _M0L8new__capS503;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS501 = _M0MPC15array5Array8capacityGsE(_M0L4selfS502);
  _M0L3lenS1543 = _M0L4selfS502->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS503
  = _M0FPB23array__growth__capacity(_M0L8old__capS501, _M0L3lenS1543, _M0L8requiredS504);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS502, _M0L8new__capS503);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS506,
  int32_t _M0L8requiredS508
) {
  int32_t _M0L8old__capS505;
  int32_t _M0L3lenS1544;
  int32_t _M0L8new__capS507;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS505 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS506);
  _M0L3lenS1544 = _M0L4selfS506->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS507
  = _M0FPB23array__growth__capacity(_M0L8old__capS505, _M0L3lenS1544, _M0L8requiredS508);
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
  int32_t* _M0L6_2aoldS2167;
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
  _M0L6_2aoldS2167 = _M0L4selfS470->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2167);
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
  float* _M0L6_2aoldS2168;
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
  _M0L6_2aoldS2168 = _M0L4selfS476->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2168);
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
  moonbit_string_t* _M0L6_2aoldS2169;
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
  _M0L6_2aoldS2169 = _M0L4selfS482->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2169);
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
  struct _M0TUsiE** _M0L6_2aoldS2170;
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
  _M0L6_2aoldS2170 = _M0L4selfS488->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2170);
  _M0L4selfS488->$0 = _M0L8new__bufS492;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS465
) {
  int32_t* _M0L6_2atmpS1537;
  int32_t _result_2274;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1537 = _M0MPC15array5Array6bufferGiE(_M0L4selfS465);
  _result_2274 = Moonbit_array_length(_M0L6_2atmpS1537);
  moonbit_decref_cycle_free(_M0L6_2atmpS1537);
  return _result_2274;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS466
) {
  float* _M0L6_2atmpS1538;
  int32_t _result_2275;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1538 = _M0MPC15array5Array6bufferGfE(_M0L4selfS466);
  _result_2275 = Moonbit_array_length(_M0L6_2atmpS1538);
  moonbit_decref_cycle_free(_M0L6_2atmpS1538);
  return _result_2275;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS467
) {
  moonbit_string_t* _M0L6_2atmpS1539;
  int32_t _result_2276;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1539 = _M0MPC15array5Array6bufferGsE(_M0L4selfS467);
  _result_2276 = Moonbit_array_length(_M0L6_2atmpS1539);
  moonbit_decref_cycle_free(_M0L6_2atmpS1539);
  return _result_2276;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS468
) {
  struct _M0TUsiE** _M0L6_2atmpS1540;
  int32_t _result_2277;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1540 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS468);
  _result_2277 = Moonbit_array_length(_M0L6_2atmpS1540);
  moonbit_decref_cycle_free(_M0L6_2atmpS1540);
  return _result_2277;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_21.data);
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
  float* _M0L8_2afieldS2171;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2171 = _M0L4selfS452->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2171);
  return _M0L8_2afieldS2171;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS453
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS2172;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2172 = _M0L4selfS453->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2172);
  return _M0L8_2afieldS2172;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS454) {
  int32_t* _M0L8_2afieldS2173;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2173 = _M0L4selfS454->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2173);
  return _M0L8_2afieldS2173;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS455
) {
  moonbit_string_t* _M0L8_2afieldS2174;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2174 = _M0L4selfS455->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2174);
  return _M0L8_2afieldS2174;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS456
) {
  struct _M0TUsiE** _M0L8_2afieldS2175;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2175 = _M0L4selfS456->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2175);
  return _M0L8_2afieldS2175;
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
  int32_t _M0L3endS1535;
  int32_t _M0L5startS1536;
  int32_t _M0L8str__lenS447;
  int32_t _M0L3lenS1534;
  int32_t _M0L8requiredS449;
  uint16_t* _M0L4dataS1527;
  int32_t _M0L6_2atmpS1526;
  int32_t _if__result_2279;
  uint16_t* _M0L4dataS1528;
  int32_t _M0L3lenS1529;
  moonbit_string_t _M0L6_2atmpS1530;
  int32_t _M0L6_2atmpS1531;
  int32_t _M0L3lenS1533;
  int32_t _M0L6_2atmpS1532;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1535 = _M0L3strS448.$2;
  _M0L5startS1536 = _M0L3strS448.$1;
  _M0L8str__lenS447 = _M0L3endS1535 - _M0L5startS1536;
  if (_M0L8str__lenS447 == 0) {
    return 0;
  }
  _M0L3lenS1534 = _M0L4selfS450->$1;
  _M0L8requiredS449 = _M0L3lenS1534 + _M0L8str__lenS447;
  _M0L4dataS1527 = _M0L4selfS450->$0;
  _M0L6_2atmpS1526 = Moonbit_array_length(_M0L4dataS1527);
  if (_M0L8requiredS449 > _M0L6_2atmpS1526) {
    _if__result_2279 = 1;
  } else {
    int32_t _M0L3lenS1525 = _M0L4selfS450->$1;
    _if__result_2279 = _M0L8requiredS449 < _M0L3lenS1525;
  }
  if (_if__result_2279) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS450, _M0L8requiredS449);
  }
  _M0L4dataS1528 = _M0L4selfS450->$0;
  _M0L3lenS1529 = _M0L4selfS450->$1;
  moonbit_incref_cycle_free(_M0L4dataS1528);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1530 = _M0MPC16string10StringView4data(_M0L3strS448);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1531 = _M0MPC16string10StringView13start__offset(_M0L3strS448);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1528, _M0L3lenS1529, _M0L6_2atmpS1530, _M0L6_2atmpS1531, _M0L8str__lenS447);
  moonbit_decref_cycle_free(_M0L4dataS1528);
  moonbit_decref_cycle_free(_M0L6_2atmpS1530);
  _M0L3lenS1533 = _M0L4selfS450->$1;
  _M0L6_2atmpS1532 = _M0L3lenS1533 + _M0L8str__lenS447;
  _M0L4selfS450->$1 = _M0L6_2atmpS1532;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS444,
  int32_t _M0L5startS442,
  int32_t _M0L3endS443
) {
  int32_t _if__result_2280;
  int32_t _M0L3lenS445;
  int32_t _M0L6_2atmpS1524;
  moonbit_bytes_t _M0L5bytesS446;
  moonbit_bytes_t _M0L6_2atmpS1523;
  moonbit_string_t _result_2281;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS442 == 0) {
    int32_t _M0L6_2atmpS1522 = Moonbit_array_length(_M0L3strS444);
    _if__result_2280 = _M0L3endS443 == _M0L6_2atmpS1522;
  } else {
    _if__result_2280 = 0;
  }
  if (_if__result_2280) {
    moonbit_incref_cycle_free(_M0L3strS444);
    return _M0L3strS444;
  }
  _M0L3lenS445 = _M0L3endS443 - _M0L5startS442;
  _M0L6_2atmpS1524 = _M0L3lenS445 * 2;
  _M0L5bytesS446 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1524, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS446, 0, _M0L3strS444, _M0L5startS442, _M0L3lenS445);
  _M0L6_2atmpS1523 = _M0L5bytesS446;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2281
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1523, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1523);
  return _result_2281;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS437,
  int32_t _M0L6offsetS441,
  int64_t _M0L6lengthS439
) {
  int32_t _M0L3lenS436;
  int32_t _M0L6lengthS438;
  int32_t _if__result_2282;
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
      int32_t _M0L6_2atmpS1521 = _M0L6offsetS441 + _M0L6lengthS438;
      _if__result_2282 = _M0L6_2atmpS1521 <= _M0L3lenS436;
    } else {
      _if__result_2282 = 0;
    }
  } else {
    _if__result_2282 = 0;
  }
  if (_if__result_2282) {
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
  int32_t _M0L6_2atmpS1520;
  int32_t _M0L6_2atmpS1519;
  int32_t _M0L2e1S422;
  int32_t _M0L6_2atmpS1518;
  int32_t _M0L2e2S425;
  int32_t _M0L4len1S427;
  int32_t _M0L4len2S429;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1520 = _M0L6lengthS424 * 2;
  _M0L6_2atmpS1519 = _M0L13bytes__offsetS423 + _M0L6_2atmpS1520;
  _M0L2e1S422 = _M0L6_2atmpS1519 - 1;
  _M0L6_2atmpS1518 = _M0L11str__offsetS426 + _M0L6lengthS424;
  _M0L2e2S425 = _M0L6_2atmpS1518 - 1;
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
        int32_t _M0L6_2atmpS1515 = _M0L3strS430[_M0L1iS432];
        int32_t _M0L6_2atmpS1514 = (int32_t)_M0L6_2atmpS1515;
        uint32_t _M0L1cS434 = *(uint32_t*)&_M0L6_2atmpS1514;
        uint32_t _M0L6_2atmpS1510 = _M0L1cS434 & 255u;
        int32_t _M0L6_2atmpS1509;
        int32_t _M0L6_2atmpS1511;
        uint32_t _M0L6_2atmpS1513;
        int32_t _M0L6_2atmpS1512;
        int32_t _M0L6_2atmpS1516;
        int32_t _M0L6_2atmpS1517;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1509 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1510);
        if (
          _M0L1jS433 < 0 || _M0L1jS433 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L1jS433] = _M0L6_2atmpS1509;
        _M0L6_2atmpS1511 = _M0L1jS433 + 1;
        _M0L6_2atmpS1513 = _M0L1cS434 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1512 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1513);
        if (
          _M0L6_2atmpS1511 < 0
          || _M0L6_2atmpS1511 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L6_2atmpS1511] = _M0L6_2atmpS1512;
        _M0L6_2atmpS1516 = _M0L1iS432 + 1;
        _M0L6_2atmpS1517 = _M0L1jS433 + 2;
        _M0L1iS432 = _M0L6_2atmpS1516;
        _M0L1jS433 = _M0L6_2atmpS1517;
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
  int32_t _M0L6_2atmpS1508;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1508 = *(int32_t*)&_M0L4selfS421;
  return _M0L6_2atmpS1508 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS413,
  int32_t _M0L5radixS412
) {
  uint16_t* _M0L6bufferS414;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS412 < 2 || _M0L5radixS412 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_22.data);
  }
  if (_M0L4selfS413 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_15.data;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_22.data);
  }
  if (_M0L4selfS396 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_15.data;
  }
  _M0L12is__negativeS397 = _M0L4selfS396 < 0ll;
  if (_M0L12is__negativeS397) {
    int64_t _M0L6_2atmpS1507 = -_M0L4selfS396;
    _M0L3numS398 = *(uint64_t*)&_M0L6_2atmpS1507;
  } else {
    _M0L3numS398 = *(uint64_t*)&_M0L4selfS396;
  }
  switch (_M0L5radixS395) {
    case 10: {
      int32_t _M0L10digit__lenS400;
      int32_t _M0L6_2atmpS1504;
      int32_t _M0L10total__lenS401;
      uint16_t* _M0L6bufferS402;
      int32_t _M0L12digit__startS403;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS400 = _M0FPB12dec__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1504 = 1;
      } else {
        _M0L6_2atmpS1504 = 0;
      }
      _M0L10total__lenS401 = _M0L10digit__lenS400 + _M0L6_2atmpS1504;
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
      int32_t _M0L6_2atmpS1505;
      int32_t _M0L10total__lenS405;
      uint16_t* _M0L6bufferS406;
      int32_t _M0L12digit__startS407;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS404 = _M0FPB12hex__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1505 = 1;
      } else {
        _M0L6_2atmpS1505 = 0;
      }
      _M0L10total__lenS405 = _M0L10digit__lenS404 + _M0L6_2atmpS1505;
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
      int32_t _M0L6_2atmpS1506;
      int32_t _M0L10total__lenS409;
      uint16_t* _M0L6bufferS410;
      int32_t _M0L12digit__startS411;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS408
      = _M0FPB14radix__count64(_M0L3numS398, _M0L5radixS395);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1506 = 1;
      } else {
        _M0L6_2atmpS1506 = 0;
      }
      _M0L10total__lenS409 = _M0L10digit__lenS408 + _M0L6_2atmpS1506;
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
  int32_t _M0L6_2atmpS1503;
  uint64_t _M0L3numS371;
  int32_t _M0L6offsetS372;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1503 = _M0L10total__lenS394 - _M0L12digit__startS382;
  _M0L3numS371 = _M0L3numS393;
  _M0L6offsetS372 = _M0L6_2atmpS1503;
  while (1) {
    if (_M0L3numS371 >= 10000ull) {
      uint64_t _M0L1tS373 = _M0L3numS371 / 10000ull;
      uint64_t _M0L6_2atmpS1480 = _M0L3numS371 % 10000ull;
      int32_t _M0L1rS374 = (int32_t)_M0L6_2atmpS1480;
      int32_t _M0L2d1S375 = _M0L1rS374 / 100;
      int32_t _M0L2d2S376 = _M0L1rS374 % 100;
      int32_t _M0L6_2atmpS1479 = _M0L2d1S375 / 10;
      int32_t _M0L6_2atmpS1478 = 48 + _M0L6_2atmpS1479;
      int32_t _M0L6d1__hiS377 = (uint16_t)_M0L6_2atmpS1478;
      int32_t _M0L6_2atmpS1477 = _M0L2d1S375 % 10;
      int32_t _M0L6_2atmpS1476 = 48 + _M0L6_2atmpS1477;
      int32_t _M0L6d1__loS378 = (uint16_t)_M0L6_2atmpS1476;
      int32_t _M0L6_2atmpS1475 = _M0L2d2S376 / 10;
      int32_t _M0L6_2atmpS1474 = 48 + _M0L6_2atmpS1475;
      int32_t _M0L6d2__hiS379 = (uint16_t)_M0L6_2atmpS1474;
      int32_t _M0L6_2atmpS1473 = _M0L2d2S376 % 10;
      int32_t _M0L6_2atmpS1472 = 48 + _M0L6_2atmpS1473;
      int32_t _M0L6d2__loS380 = (uint16_t)_M0L6_2atmpS1472;
      int32_t _M0L6_2atmpS1464 = _M0L12digit__startS382 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS1463 = _M0L6_2atmpS1464 - 4;
      int32_t _M0L6_2atmpS1466;
      int32_t _M0L6_2atmpS1465;
      int32_t _M0L6_2atmpS1468;
      int32_t _M0L6_2atmpS1467;
      int32_t _M0L6_2atmpS1470;
      int32_t _M0L6_2atmpS1469;
      int32_t _M0L6_2atmpS1471;
      _M0L6bufferS381[_M0L6_2atmpS1463] = _M0L6d1__hiS377;
      _M0L6_2atmpS1466 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1465 = _M0L6_2atmpS1466 - 3;
      _M0L6bufferS381[_M0L6_2atmpS1465] = _M0L6d1__loS378;
      _M0L6_2atmpS1468 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1467 = _M0L6_2atmpS1468 - 2;
      _M0L6bufferS381[_M0L6_2atmpS1467] = _M0L6d2__hiS379;
      _M0L6_2atmpS1470 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1469 = _M0L6_2atmpS1470 - 1;
      _M0L6bufferS381[_M0L6_2atmpS1469] = _M0L6d2__loS380;
      _M0L6_2atmpS1471 = _M0L6offsetS372 - 4;
      _M0L3numS371 = _M0L1tS373;
      _M0L6offsetS372 = _M0L6_2atmpS1471;
      continue;
    } else {
      int32_t _M0L6_2atmpS1502 = (int32_t)_M0L3numS371;
      int32_t _M0L9remainingS384 = _M0L6_2atmpS1502;
      int32_t _M0L6offsetS385 = _M0L6offsetS372;
      while (1) {
        if (_M0L9remainingS384 >= 100) {
          int32_t _M0L1tS386 = _M0L9remainingS384 / 100;
          int32_t _M0L1dS387 = _M0L9remainingS384 % 100;
          int32_t _M0L6_2atmpS1489 = _M0L1dS387 / 10;
          int32_t _M0L6_2atmpS1488 = 48 + _M0L6_2atmpS1489;
          int32_t _M0L5d__hiS388 = (uint16_t)_M0L6_2atmpS1488;
          int32_t _M0L6_2atmpS1487 = _M0L1dS387 % 10;
          int32_t _M0L6_2atmpS1486 = 48 + _M0L6_2atmpS1487;
          int32_t _M0L5d__loS389 = (uint16_t)_M0L6_2atmpS1486;
          int32_t _M0L6_2atmpS1482 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1481 = _M0L6_2atmpS1482 - 2;
          int32_t _M0L6_2atmpS1484;
          int32_t _M0L6_2atmpS1483;
          int32_t _M0L6_2atmpS1485;
          _M0L6bufferS381[_M0L6_2atmpS1481] = _M0L5d__hiS388;
          _M0L6_2atmpS1484 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1483 = _M0L6_2atmpS1484 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1483] = _M0L5d__loS389;
          _M0L6_2atmpS1485 = _M0L6offsetS385 - 2;
          _M0L9remainingS384 = _M0L1tS386;
          _M0L6offsetS385 = _M0L6_2atmpS1485;
          continue;
        } else if (_M0L9remainingS384 >= 10) {
          int32_t _M0L6_2atmpS1497 = _M0L9remainingS384 / 10;
          int32_t _M0L6_2atmpS1496 = 48 + _M0L6_2atmpS1497;
          int32_t _M0L5d__hiS391 = (uint16_t)_M0L6_2atmpS1496;
          int32_t _M0L6_2atmpS1495 = _M0L9remainingS384 % 10;
          int32_t _M0L6_2atmpS1494 = 48 + _M0L6_2atmpS1495;
          int32_t _M0L5d__loS392 = (uint16_t)_M0L6_2atmpS1494;
          int32_t _M0L6_2atmpS1491 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1490 = _M0L6_2atmpS1491 - 2;
          int32_t _M0L6_2atmpS1493;
          int32_t _M0L6_2atmpS1492;
          _M0L6bufferS381[_M0L6_2atmpS1490] = _M0L5d__hiS391;
          _M0L6_2atmpS1493 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1492 = _M0L6_2atmpS1493 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1492] = _M0L5d__loS392;
        } else {
          int32_t _M0L6_2atmpS1501 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1498 = _M0L6_2atmpS1501 - 1;
          int32_t _M0L6_2atmpS1500 = 48 + _M0L9remainingS384;
          int32_t _M0L6_2atmpS1499 = (uint16_t)_M0L6_2atmpS1500;
          _M0L6bufferS381[_M0L6_2atmpS1498] = _M0L6_2atmpS1499;
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
  int32_t _M0L6_2atmpS1448;
  int32_t _M0L6_2atmpS1447;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS354 = _M0MPC13int3Int10to__uint64(_M0L5radixS355);
  _M0L6_2atmpS1448 = _M0L5radixS355 - 1;
  _M0L6_2atmpS1447 = _M0L5radixS355 & _M0L6_2atmpS1448;
  if (_M0L6_2atmpS1447 == 0) {
    int32_t _M0L5shiftS356;
    uint64_t _M0L4maskS357;
    int32_t _M0L6_2atmpS1455;
    int32_t _M0L6offsetS358;
    uint64_t _M0L1nS359;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS356 = moonbit_ctz32(_M0L5radixS355);
    _M0L4maskS357 = _M0L4baseS354 - 1ull;
    _M0L6_2atmpS1455 = _M0L10total__lenS364 - _M0L12digit__startS362;
    _M0L6offsetS358 = _M0L6_2atmpS1455;
    _M0L1nS359 = _M0L3numS365;
    while (1) {
      if (_M0L1nS359 > 0ull) {
        uint64_t _M0L6_2atmpS1454 = _M0L1nS359 & _M0L4maskS357;
        int32_t _M0L5digitS360 = (int32_t)_M0L6_2atmpS1454;
        int32_t _M0L6_2atmpS1451 = _M0L12digit__startS362 + _M0L6offsetS358;
        int32_t _M0L6_2atmpS1449 = _M0L6_2atmpS1451 - 1;
        int32_t _M0L6_2atmpS1450 =
          ((moonbit_string_t)moonbit_string_literal_23.data)[_M0L5digitS360];
        int32_t _M0L6_2atmpS1452;
        uint64_t _M0L6_2atmpS1453;
        _M0L6bufferS361[_M0L6_2atmpS1449] = _M0L6_2atmpS1450;
        _M0L6_2atmpS1452 = _M0L6offsetS358 - 1;
        _M0L6_2atmpS1453 = _M0L1nS359 >> (_M0L5shiftS356 & 63);
        _M0L6offsetS358 = _M0L6_2atmpS1452;
        _M0L1nS359 = _M0L6_2atmpS1453;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1462 = _M0L10total__lenS364 - _M0L12digit__startS362;
    int32_t _M0L6offsetS366 = _M0L6_2atmpS1462;
    uint64_t _M0L1nS367 = _M0L3numS365;
    while (1) {
      if (_M0L1nS367 > 0ull) {
        uint64_t _M0L1qS368 = _M0L1nS367 / _M0L4baseS354;
        uint64_t _M0L6_2atmpS1461 = _M0L1qS368 * _M0L4baseS354;
        uint64_t _M0L6_2atmpS1460 = _M0L1nS367 - _M0L6_2atmpS1461;
        int32_t _M0L5digitS369 = (int32_t)_M0L6_2atmpS1460;
        int32_t _M0L6_2atmpS1458 = _M0L12digit__startS362 + _M0L6offsetS366;
        int32_t _M0L6_2atmpS1456 = _M0L6_2atmpS1458 - 1;
        int32_t _M0L6_2atmpS1457 =
          ((moonbit_string_t)moonbit_string_literal_23.data)[_M0L5digitS369];
        int32_t _M0L6_2atmpS1459;
        _M0L6bufferS361[_M0L6_2atmpS1456] = _M0L6_2atmpS1457;
        _M0L6_2atmpS1459 = _M0L6offsetS366 - 1;
        _M0L6offsetS366 = _M0L6_2atmpS1459;
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
  int32_t _M0L6_2atmpS1446;
  int32_t _M0L6offsetS343;
  uint64_t _M0L1nS344;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1446 = _M0L10total__lenS352 - _M0L12digit__startS349;
  _M0L6offsetS343 = _M0L6_2atmpS1446;
  _M0L1nS344 = _M0L3numS353;
  while (1) {
    if (_M0L6offsetS343 >= 2) {
      uint64_t _M0L6_2atmpS1443 = _M0L1nS344 & 255ull;
      int32_t _M0L9byte__valS345 = (int32_t)_M0L6_2atmpS1443;
      int32_t _M0L2hiS346 = _M0L9byte__valS345 / 16;
      int32_t _M0L2loS347 = _M0L9byte__valS345 % 16;
      int32_t _M0L6_2atmpS1437 = _M0L12digit__startS349 + _M0L6offsetS343;
      int32_t _M0L6_2atmpS1435 = _M0L6_2atmpS1437 - 2;
      int32_t _M0L6_2atmpS1436 =
        ((moonbit_string_t)moonbit_string_literal_23.data)[_M0L2hiS346];
      int32_t _M0L6_2atmpS1440;
      int32_t _M0L6_2atmpS1438;
      int32_t _M0L6_2atmpS1439;
      int32_t _M0L6_2atmpS1441;
      uint64_t _M0L6_2atmpS1442;
      _M0L6bufferS348[_M0L6_2atmpS1435] = _M0L6_2atmpS1436;
      _M0L6_2atmpS1440 = _M0L12digit__startS349 + _M0L6offsetS343;
      _M0L6_2atmpS1438 = _M0L6_2atmpS1440 - 1;
      _M0L6_2atmpS1439
      = ((moonbit_string_t)moonbit_string_literal_23.data)[
        _M0L2loS347
      ];
      _M0L6bufferS348[_M0L6_2atmpS1438] = _M0L6_2atmpS1439;
      _M0L6_2atmpS1441 = _M0L6offsetS343 - 2;
      _M0L6_2atmpS1442 = _M0L1nS344 >> 8;
      _M0L6offsetS343 = _M0L6_2atmpS1441;
      _M0L1nS344 = _M0L6_2atmpS1442;
      continue;
    } else if (_M0L6offsetS343 == 1) {
      uint64_t _M0L6_2atmpS1445 = _M0L1nS344 & 15ull;
      int32_t _M0L6nibbleS351 = (int32_t)_M0L6_2atmpS1445;
      int32_t _M0L6_2atmpS1444 =
        ((moonbit_string_t)moonbit_string_literal_23.data)[_M0L6nibbleS351];
      _M0L6bufferS348[_M0L12digit__startS349] = _M0L6_2atmpS1444;
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
      uint64_t _M0L6_2atmpS1433 = _M0L3numS340 / _M0L4baseS338;
      int32_t _M0L6_2atmpS1434 = _M0L5countS341 + 1;
      _M0L3numS340 = _M0L6_2atmpS1433;
      _M0L5countS341 = _M0L6_2atmpS1434;
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
    int32_t _M0L6_2atmpS1432;
    int32_t _M0L6_2atmpS1431;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS336 = moonbit_clz64(_M0L5valueS335);
    _M0L6_2atmpS1432 = 63 - _M0L14leading__zerosS336;
    _M0L6_2atmpS1431 = _M0L6_2atmpS1432 / 4;
    return _M0L6_2atmpS1431 + 1;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_22.data);
  }
  if (_M0L4selfS318 == 0) {
    return (moonbit_string_t)moonbit_string_literal_15.data;
  }
  _M0L12is__negativeS319 = _M0L4selfS318 < 0;
  if (_M0L12is__negativeS319) {
    int32_t _M0L6_2atmpS1430 = -_M0L4selfS318;
    _M0L3numS320 = *(uint32_t*)&_M0L6_2atmpS1430;
  } else {
    _M0L3numS320 = *(uint32_t*)&_M0L4selfS318;
  }
  switch (_M0L5radixS317) {
    case 10: {
      int32_t _M0L10digit__lenS322;
      int32_t _M0L6_2atmpS1427;
      int32_t _M0L10total__lenS323;
      uint16_t* _M0L6bufferS324;
      int32_t _M0L12digit__startS325;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS322 = _M0FPB12dec__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1427 = 1;
      } else {
        _M0L6_2atmpS1427 = 0;
      }
      _M0L10total__lenS323 = _M0L10digit__lenS322 + _M0L6_2atmpS1427;
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
      int32_t _M0L6_2atmpS1428;
      int32_t _M0L10total__lenS327;
      uint16_t* _M0L6bufferS328;
      int32_t _M0L12digit__startS329;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS326 = _M0FPB12hex__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1428 = 1;
      } else {
        _M0L6_2atmpS1428 = 0;
      }
      _M0L10total__lenS327 = _M0L10digit__lenS326 + _M0L6_2atmpS1428;
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
      int32_t _M0L6_2atmpS1429;
      int32_t _M0L10total__lenS331;
      uint16_t* _M0L6bufferS332;
      int32_t _M0L12digit__startS333;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS330
      = _M0FPB14radix__count32(_M0L3numS320, _M0L5radixS317);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1429 = 1;
      } else {
        _M0L6_2atmpS1429 = 0;
      }
      _M0L10total__lenS331 = _M0L10digit__lenS330 + _M0L6_2atmpS1429;
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
      uint32_t _M0L6_2atmpS1425 = _M0L3numS314 / _M0L4baseS312;
      int32_t _M0L6_2atmpS1426 = _M0L5countS315 + 1;
      _M0L3numS314 = _M0L6_2atmpS1425;
      _M0L5countS315 = _M0L6_2atmpS1426;
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
    int32_t _M0L6_2atmpS1424;
    int32_t _M0L6_2atmpS1423;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS310 = moonbit_clz32(_M0L5valueS309);
    _M0L6_2atmpS1424 = 31 - _M0L14leading__zerosS310;
    _M0L6_2atmpS1423 = _M0L6_2atmpS1424 / 4;
    return _M0L6_2atmpS1423 + 1;
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
  int32_t _M0L6_2atmpS1422;
  uint32_t _M0L3numS284;
  int32_t _M0L6offsetS285;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1422 = _M0L10total__lenS307 - _M0L12digit__startS295;
  _M0L3numS284 = _M0L3numS306;
  _M0L6offsetS285 = _M0L6_2atmpS1422;
  while (1) {
    if (_M0L3numS284 >= 10000u) {
      uint32_t _M0L1tS286 = _M0L3numS284 / 10000u;
      uint32_t _M0L6_2atmpS1399 = _M0L3numS284 % 10000u;
      int32_t _M0L1rS287 = *(int32_t*)&_M0L6_2atmpS1399;
      int32_t _M0L2d1S288 = _M0L1rS287 / 100;
      int32_t _M0L2d2S289 = _M0L1rS287 % 100;
      int32_t _M0L6_2atmpS1398 = _M0L2d1S288 / 10;
      int32_t _M0L6_2atmpS1397 = 48 + _M0L6_2atmpS1398;
      int32_t _M0L6d1__hiS290 = (uint16_t)_M0L6_2atmpS1397;
      int32_t _M0L6_2atmpS1396 = _M0L2d1S288 % 10;
      int32_t _M0L6_2atmpS1395 = 48 + _M0L6_2atmpS1396;
      int32_t _M0L6d1__loS291 = (uint16_t)_M0L6_2atmpS1395;
      int32_t _M0L6_2atmpS1394 = _M0L2d2S289 / 10;
      int32_t _M0L6_2atmpS1393 = 48 + _M0L6_2atmpS1394;
      int32_t _M0L6d2__hiS292 = (uint16_t)_M0L6_2atmpS1393;
      int32_t _M0L6_2atmpS1392 = _M0L2d2S289 % 10;
      int32_t _M0L6_2atmpS1391 = 48 + _M0L6_2atmpS1392;
      int32_t _M0L6d2__loS293 = (uint16_t)_M0L6_2atmpS1391;
      int32_t _M0L6_2atmpS1383 = _M0L12digit__startS295 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS1382 = _M0L6_2atmpS1383 - 4;
      int32_t _M0L6_2atmpS1385;
      int32_t _M0L6_2atmpS1384;
      int32_t _M0L6_2atmpS1387;
      int32_t _M0L6_2atmpS1386;
      int32_t _M0L6_2atmpS1389;
      int32_t _M0L6_2atmpS1388;
      int32_t _M0L6_2atmpS1390;
      _M0L6bufferS294[_M0L6_2atmpS1382] = _M0L6d1__hiS290;
      _M0L6_2atmpS1385 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1384 = _M0L6_2atmpS1385 - 3;
      _M0L6bufferS294[_M0L6_2atmpS1384] = _M0L6d1__loS291;
      _M0L6_2atmpS1387 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1386 = _M0L6_2atmpS1387 - 2;
      _M0L6bufferS294[_M0L6_2atmpS1386] = _M0L6d2__hiS292;
      _M0L6_2atmpS1389 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1388 = _M0L6_2atmpS1389 - 1;
      _M0L6bufferS294[_M0L6_2atmpS1388] = _M0L6d2__loS293;
      _M0L6_2atmpS1390 = _M0L6offsetS285 - 4;
      _M0L3numS284 = _M0L1tS286;
      _M0L6offsetS285 = _M0L6_2atmpS1390;
      continue;
    } else {
      int32_t _M0L6_2atmpS1421 = *(int32_t*)&_M0L3numS284;
      int32_t _M0L9remainingS297 = _M0L6_2atmpS1421;
      int32_t _M0L6offsetS298 = _M0L6offsetS285;
      while (1) {
        if (_M0L9remainingS297 >= 100) {
          int32_t _M0L1tS299 = _M0L9remainingS297 / 100;
          int32_t _M0L1dS300 = _M0L9remainingS297 % 100;
          int32_t _M0L6_2atmpS1408 = _M0L1dS300 / 10;
          int32_t _M0L6_2atmpS1407 = 48 + _M0L6_2atmpS1408;
          int32_t _M0L5d__hiS301 = (uint16_t)_M0L6_2atmpS1407;
          int32_t _M0L6_2atmpS1406 = _M0L1dS300 % 10;
          int32_t _M0L6_2atmpS1405 = 48 + _M0L6_2atmpS1406;
          int32_t _M0L5d__loS302 = (uint16_t)_M0L6_2atmpS1405;
          int32_t _M0L6_2atmpS1401 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1400 = _M0L6_2atmpS1401 - 2;
          int32_t _M0L6_2atmpS1403;
          int32_t _M0L6_2atmpS1402;
          int32_t _M0L6_2atmpS1404;
          _M0L6bufferS294[_M0L6_2atmpS1400] = _M0L5d__hiS301;
          _M0L6_2atmpS1403 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1402 = _M0L6_2atmpS1403 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1402] = _M0L5d__loS302;
          _M0L6_2atmpS1404 = _M0L6offsetS298 - 2;
          _M0L9remainingS297 = _M0L1tS299;
          _M0L6offsetS298 = _M0L6_2atmpS1404;
          continue;
        } else if (_M0L9remainingS297 >= 10) {
          int32_t _M0L6_2atmpS1416 = _M0L9remainingS297 / 10;
          int32_t _M0L6_2atmpS1415 = 48 + _M0L6_2atmpS1416;
          int32_t _M0L5d__hiS304 = (uint16_t)_M0L6_2atmpS1415;
          int32_t _M0L6_2atmpS1414 = _M0L9remainingS297 % 10;
          int32_t _M0L6_2atmpS1413 = 48 + _M0L6_2atmpS1414;
          int32_t _M0L5d__loS305 = (uint16_t)_M0L6_2atmpS1413;
          int32_t _M0L6_2atmpS1410 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1409 = _M0L6_2atmpS1410 - 2;
          int32_t _M0L6_2atmpS1412;
          int32_t _M0L6_2atmpS1411;
          _M0L6bufferS294[_M0L6_2atmpS1409] = _M0L5d__hiS304;
          _M0L6_2atmpS1412 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1411 = _M0L6_2atmpS1412 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1411] = _M0L5d__loS305;
        } else {
          int32_t _M0L6_2atmpS1420 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1417 = _M0L6_2atmpS1420 - 1;
          int32_t _M0L6_2atmpS1419 = 48 + _M0L9remainingS297;
          int32_t _M0L6_2atmpS1418 = (uint16_t)_M0L6_2atmpS1419;
          _M0L6bufferS294[_M0L6_2atmpS1417] = _M0L6_2atmpS1418;
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
  int32_t _M0L6_2atmpS1367;
  int32_t _M0L6_2atmpS1366;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS267 = *(uint32_t*)&_M0L5radixS268;
  _M0L6_2atmpS1367 = _M0L5radixS268 - 1;
  _M0L6_2atmpS1366 = _M0L5radixS268 & _M0L6_2atmpS1367;
  if (_M0L6_2atmpS1366 == 0) {
    int32_t _M0L5shiftS269;
    uint32_t _M0L4maskS270;
    int32_t _M0L6_2atmpS1374;
    int32_t _M0L6offsetS271;
    uint32_t _M0L1nS272;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS269 = moonbit_ctz32(_M0L5radixS268);
    _M0L4maskS270 = _M0L4baseS267 - 1u;
    _M0L6_2atmpS1374 = _M0L10total__lenS277 - _M0L12digit__startS275;
    _M0L6offsetS271 = _M0L6_2atmpS1374;
    _M0L1nS272 = _M0L3numS278;
    while (1) {
      if (_M0L1nS272 > 0u) {
        uint32_t _M0L6_2atmpS1373 = _M0L1nS272 & _M0L4maskS270;
        int32_t _M0L5digitS273 = *(int32_t*)&_M0L6_2atmpS1373;
        int32_t _M0L6_2atmpS1370 = _M0L12digit__startS275 + _M0L6offsetS271;
        int32_t _M0L6_2atmpS1368 = _M0L6_2atmpS1370 - 1;
        int32_t _M0L6_2atmpS1369 =
          ((moonbit_string_t)moonbit_string_literal_23.data)[_M0L5digitS273];
        int32_t _M0L6_2atmpS1371;
        uint32_t _M0L6_2atmpS1372;
        _M0L6bufferS274[_M0L6_2atmpS1368] = _M0L6_2atmpS1369;
        _M0L6_2atmpS1371 = _M0L6offsetS271 - 1;
        _M0L6_2atmpS1372 = _M0L1nS272 >> (_M0L5shiftS269 & 31);
        _M0L6offsetS271 = _M0L6_2atmpS1371;
        _M0L1nS272 = _M0L6_2atmpS1372;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1381 = _M0L10total__lenS277 - _M0L12digit__startS275;
    int32_t _M0L6offsetS279 = _M0L6_2atmpS1381;
    uint32_t _M0L1nS280 = _M0L3numS278;
    while (1) {
      if (_M0L1nS280 > 0u) {
        uint32_t _M0L1qS281 = _M0L1nS280 / _M0L4baseS267;
        uint32_t _M0L6_2atmpS1380 = _M0L1qS281 * _M0L4baseS267;
        uint32_t _M0L6_2atmpS1379 = _M0L1nS280 - _M0L6_2atmpS1380;
        int32_t _M0L5digitS282 = *(int32_t*)&_M0L6_2atmpS1379;
        int32_t _M0L6_2atmpS1377 = _M0L12digit__startS275 + _M0L6offsetS279;
        int32_t _M0L6_2atmpS1375 = _M0L6_2atmpS1377 - 1;
        int32_t _M0L6_2atmpS1376 =
          ((moonbit_string_t)moonbit_string_literal_23.data)[_M0L5digitS282];
        int32_t _M0L6_2atmpS1378;
        _M0L6bufferS274[_M0L6_2atmpS1375] = _M0L6_2atmpS1376;
        _M0L6_2atmpS1378 = _M0L6offsetS279 - 1;
        _M0L6offsetS279 = _M0L6_2atmpS1378;
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
  int32_t _M0L6_2atmpS1365;
  int32_t _M0L6offsetS256;
  uint32_t _M0L1nS257;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1365 = _M0L10total__lenS265 - _M0L12digit__startS262;
  _M0L6offsetS256 = _M0L6_2atmpS1365;
  _M0L1nS257 = _M0L3numS266;
  while (1) {
    if (_M0L6offsetS256 >= 2) {
      uint32_t _M0L6_2atmpS1362 = _M0L1nS257 & 255u;
      int32_t _M0L9byte__valS258 = *(int32_t*)&_M0L6_2atmpS1362;
      int32_t _M0L2hiS259 = _M0L9byte__valS258 / 16;
      int32_t _M0L2loS260 = _M0L9byte__valS258 % 16;
      int32_t _M0L6_2atmpS1356 = _M0L12digit__startS262 + _M0L6offsetS256;
      int32_t _M0L6_2atmpS1354 = _M0L6_2atmpS1356 - 2;
      int32_t _M0L6_2atmpS1355 =
        ((moonbit_string_t)moonbit_string_literal_23.data)[_M0L2hiS259];
      int32_t _M0L6_2atmpS1359;
      int32_t _M0L6_2atmpS1357;
      int32_t _M0L6_2atmpS1358;
      int32_t _M0L6_2atmpS1360;
      uint32_t _M0L6_2atmpS1361;
      _M0L6bufferS261[_M0L6_2atmpS1354] = _M0L6_2atmpS1355;
      _M0L6_2atmpS1359 = _M0L12digit__startS262 + _M0L6offsetS256;
      _M0L6_2atmpS1357 = _M0L6_2atmpS1359 - 1;
      _M0L6_2atmpS1358
      = ((moonbit_string_t)moonbit_string_literal_23.data)[
        _M0L2loS260
      ];
      _M0L6bufferS261[_M0L6_2atmpS1357] = _M0L6_2atmpS1358;
      _M0L6_2atmpS1360 = _M0L6offsetS256 - 2;
      _M0L6_2atmpS1361 = _M0L1nS257 >> 8;
      _M0L6offsetS256 = _M0L6_2atmpS1360;
      _M0L1nS257 = _M0L6_2atmpS1361;
      continue;
    } else if (_M0L6offsetS256 == 1) {
      uint32_t _M0L6_2atmpS1364 = _M0L1nS257 & 15u;
      int32_t _M0L6nibbleS264 = *(int32_t*)&_M0L6_2atmpS1364;
      int32_t _M0L6_2atmpS1363 =
        ((moonbit_string_t)moonbit_string_literal_23.data)[_M0L6nibbleS264];
      _M0L6bufferS261[_M0L12digit__startS262] = _M0L6_2atmpS1363;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS255
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS254;
  struct _M0TPB6Logger _M0L6_2atmpS1353;
  moonbit_string_t _result_2296;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS254);
  _M0L6_2atmpS1353
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS254
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS255, _M0L6_2atmpS1353);
  if (_M0L6_2atmpS1353.$1) {
    moonbit_decref(_M0L6_2atmpS1353.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2296 = _M0MPB13StringBuilder10to__string(_M0L6loggerS254);
  moonbit_decref_cycle_free(_M0L6loggerS254);
  return _result_2296;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS249,
  struct _M0TPB6Logger _M0L6loggerS248
) {
  moonbit_string_t _M0L6_2atmpS1350;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1350 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS249);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248.$0->$method_0(_M0L6loggerS248.$1, _M0L6_2atmpS1350);
  moonbit_decref_cycle_free(_M0L6_2atmpS1350);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS1351;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1351 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS1351);
  moonbit_decref_cycle_free(_M0L6_2atmpS1351);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS1352;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1352 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS1352);
  moonbit_decref_cycle_free(_M0L6_2atmpS1352);
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
  moonbit_string_t _M0L8_2afieldS2176;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2176 = _M0L4selfS246.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2176);
  return _M0L8_2afieldS2176;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS242,
  moonbit_string_t _M0L5valueS243,
  int32_t _M0L5startS244,
  int32_t _M0L3lenS245
) {
  int32_t _M0L6_2atmpS1349;
  int64_t _M0L6_2atmpS1348;
  struct _M0TPC16string10StringView _M0L6_2atmpS1347;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1349 = _M0L5startS244 + _M0L3lenS245;
  _M0L6_2atmpS1348 = (int64_t)_M0L6_2atmpS1349;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1347
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS243, _M0L5startS244, _M0L6_2atmpS1348);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS242, _M0L6_2atmpS1347);
  moonbit_decref_cycle_free(_M0L6_2atmpS1347.$0);
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
  int32_t _M0L6_2atmpS1331;
  int32_t _if__result_2297;
  int32_t _M0L6_2atmpS1339;
  int32_t _if__result_2298;
  int32_t _M0L6_2atmpS1341;
  int32_t _M0L6_2atmpS1342;
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
  _M0L6_2atmpS1331 = _M0Lm2loS236;
  if (_M0L6_2atmpS1331 > 0) {
    int32_t _M0L6_2atmpS1330 = _M0Lm2loS236;
    if (_M0L6_2atmpS1330 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1329 = _M0Lm2loS236;
      int32_t _M0L6_2atmpS1328 = _M0L4selfS235[_M0L6_2atmpS1329];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1328)) {
        int32_t _M0L6_2atmpS1327 = _M0Lm2loS236;
        int32_t _M0L6_2atmpS1326 = _M0L6_2atmpS1327 - 1;
        int32_t _M0L6_2atmpS1325 = _M0L4selfS235[_M0L6_2atmpS1326];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2297
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1325);
      } else {
        _if__result_2297 = 0;
      }
    } else {
      _if__result_2297 = 0;
    }
  } else {
    _if__result_2297 = 0;
  }
  if (_if__result_2297) {
    int32_t _M0L6_2atmpS1332 = _M0Lm2loS236;
    _M0Lm2loS236 = _M0L6_2atmpS1332 + 1;
  }
  _M0L6_2atmpS1339 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1339 > 0) {
    int32_t _M0L6_2atmpS1338 = _M0Lm2hiS238;
    if (_M0L6_2atmpS1338 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1337 = _M0Lm2hiS238;
      int32_t _M0L6_2atmpS1336 = _M0L4selfS235[_M0L6_2atmpS1337];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1336)) {
        int32_t _M0L6_2atmpS1335 = _M0Lm2hiS238;
        int32_t _M0L6_2atmpS1334 = _M0L6_2atmpS1335 - 1;
        int32_t _M0L6_2atmpS1333 = _M0L4selfS235[_M0L6_2atmpS1334];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2298
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1333);
      } else {
        _if__result_2298 = 0;
      }
    } else {
      _if__result_2298 = 0;
    }
  } else {
    _if__result_2298 = 0;
  }
  if (_if__result_2298) {
    int32_t _M0L6_2atmpS1340 = _M0Lm2hiS238;
    _M0Lm2hiS238 = _M0L6_2atmpS1340 - 1;
  }
  _M0L6_2atmpS1341 = _M0Lm2loS236;
  _M0L6_2atmpS1342 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1341 >= _M0L6_2atmpS1342) {
    int32_t _M0L6_2atmpS1343 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1344 = _M0Lm2loS236;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1343,
                                                 .$2 = _M0L6_2atmpS1344};
  } else {
    int32_t _M0L6_2atmpS1345 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1346 = _M0Lm2hiS238;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1345,
                                                 .$2 = _M0L6_2atmpS1346};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS1324;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS1324
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS1324);
  if (_M0L6_2atmpS1324.$1) {
    moonbit_decref(_M0L6_2atmpS1324.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS231,
  struct _M0TPB4Show _M0L4showS230
) {
  struct _M0TPB6Logger _M0L6_2atmpS1323;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS231);
  _M0L6_2atmpS1323
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS231
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS230.$0->$method_0(_M0L4showS230.$1, _M0L6_2atmpS1323);
  if (_M0L6_2atmpS1323.$1) {
    moonbit_decref(_M0L6_2atmpS1323.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS229) {
  int64_t _M0L6_2atmpS1322;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1322 = (int64_t)_M0L4selfS229;
  return *(uint64_t*)&_M0L6_2atmpS1322;
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
  int32_t _M0L6_2atmpS1321;
  struct _M0TPC16string10StringView _M0L6_2atmpS1319;
  struct _M0TPB6Logger _M0L6_2atmpS1320;
  moonbit_string_t _result_2299;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1321 = Moonbit_array_length(_M0L4selfS227);
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS1319
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS227, .$1 = 0, .$2 = _M0L6_2atmpS1321
  };
  moonbit_incref_cycle_free(_M0L3bufS226);
  _M0L6_2atmpS1320
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS226
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1319, _M0L6_2atmpS1320, _M0L5quoteS228);
  moonbit_decref_cycle_free(_M0L6_2atmpS1319.$0);
  if (_M0L6_2atmpS1320.$1) {
    moonbit_decref(_M0L6_2atmpS1320.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2299 = _M0MPB13StringBuilder10to__string(_M0L3bufS226);
  moonbit_decref_cycle_free(_M0L3bufS226);
  return _result_2299;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS218,
  struct _M0TPB6Logger _M0L6loggerS216,
  int32_t _M0L5quoteS215
) {
  int32_t _M0L3endS1317;
  int32_t _M0L5startS1318;
  int32_t _M0L3lenS217;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS219;
  int32_t _M0L1iS220;
  int32_t _M0L3segS221;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS215) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  _M0L3endS1317 = _M0L4selfS218.$2;
  _M0L5startS1318 = _M0L4selfS218.$1;
  _M0L3lenS217 = _M0L3endS1317 - _M0L5startS1318;
  moonbit_incref_cycle_free(_M0L4selfS218.$0);
  if (_M0L6loggerS216.$1) {
    moonbit_incref(_M0L6loggerS216.$1);
  }
  _M0L6_2aenvS219
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2aenvS219->$0 = _M0L4selfS218;
  _M0L6_2aenvS219->$1 = _M0L6loggerS216;
  _M0L1iS220 = 0;
  _M0L3segS221 = 0;
  _2afor_222:;
  while (1) {
    moonbit_string_t _M0L3strS1314;
    int32_t _M0L5startS1316;
    int32_t _M0L6_2atmpS1315;
    int32_t _M0L4codeS223;
    int32_t _M0L1cS225;
    int32_t _M0L6_2atmpS1298;
    int32_t _M0L6_2atmpS1299;
    int32_t _M0L6_2atmpS1300;
    if (_M0L1iS220 >= _M0L3lenS217) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
      moonbit_decref_cycle_free(_M0L6_2aenvS219);
      break;
    }
    _M0L3strS1314 = _M0L4selfS218.$0;
    _M0L5startS1316 = _M0L4selfS218.$1;
    _M0L6_2atmpS1315 = _M0L5startS1316 + _M0L1iS220;
    _M0L4codeS223 = _M0L3strS1314[_M0L6_2atmpS1315];
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
        int32_t _M0L6_2atmpS1301;
        int32_t _M0L6_2atmpS1302;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_24.data);
        _M0L6_2atmpS1301 = _M0L1iS220 + 1;
        _M0L6_2atmpS1302 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1301;
        _M0L3segS221 = _M0L6_2atmpS1302;
        goto _2afor_222;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1303;
        int32_t _M0L6_2atmpS1304;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_25.data);
        _M0L6_2atmpS1303 = _M0L1iS220 + 1;
        _M0L6_2atmpS1304 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1303;
        _M0L3segS221 = _M0L6_2atmpS1304;
        goto _2afor_222;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1305;
        int32_t _M0L6_2atmpS1306;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_26.data);
        _M0L6_2atmpS1305 = _M0L1iS220 + 1;
        _M0L6_2atmpS1306 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1305;
        _M0L3segS221 = _M0L6_2atmpS1306;
        goto _2afor_222;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1307;
        int32_t _M0L6_2atmpS1308;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_27.data);
        _M0L6_2atmpS1307 = _M0L1iS220 + 1;
        _M0L6_2atmpS1308 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1307;
        _M0L3segS221 = _M0L6_2atmpS1308;
        goto _2afor_222;
        break;
      }
      default: {
        if (_M0L4codeS223 < 32) {
          int32_t _M0L6_2atmpS1310;
          moonbit_string_t _M0L6_2atmpS1309;
          int32_t _M0L6_2atmpS1311;
          int32_t _M0L6_2atmpS1312;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_28.data);
          _M0L6_2atmpS1310 = _M0L4codeS223 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1309 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1310);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, _M0L6_2atmpS1309);
          moonbit_decref_cycle_free(_M0L6_2atmpS1309);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1311 = _M0L1iS220 + 1;
          _M0L6_2atmpS1312 = _M0L1iS220 + 1;
          _M0L1iS220 = _M0L6_2atmpS1311;
          _M0L3segS221 = _M0L6_2atmpS1312;
          goto _2afor_222;
        } else {
          int32_t _M0L6_2atmpS1313 = _M0L1iS220 + 1;
          int32_t _tmp_2302 = _M0L3segS221;
          _M0L1iS220 = _M0L6_2atmpS1313;
          _M0L3segS221 = _tmp_2302;
          goto _2afor_222;
        }
        break;
      }
    }
    goto joinlet_2301;
    join_224:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1298 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS225);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, _M0L6_2atmpS1298);
    _M0L6_2atmpS1299 = _M0L1iS220 + 1;
    _M0L6_2atmpS1300 = _M0L1iS220 + 1;
    _M0L1iS220 = _M0L6_2atmpS1299;
    _M0L3segS221 = _M0L6_2atmpS1300;
    continue;
    joinlet_2301:;
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
    int64_t _M0L6_2atmpS1297 = (int64_t)_M0L1iS213;
    struct _M0TPC16string10StringView _M0L6_2atmpS1296;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1296
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS212, _M0L3segS214, _M0L6_2atmpS1297);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS210.$0->$method_2(_M0L6loggerS210.$1, _M0L6_2atmpS1296);
    moonbit_decref_cycle_free(_M0L6_2atmpS1296.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS201,
  int32_t _M0L5startS203,
  int64_t _M0L3endS205
) {
  int32_t _M0L3endS1294;
  int32_t _M0L5startS1295;
  int32_t _M0L3lenS200;
  int32_t _M0Lm2loS202;
  int32_t _M0Lm2hiS204;
  moonbit_string_t _M0L3strS208;
  int32_t _M0L4baseS209;
  int32_t _M0L6_2atmpS1272;
  int32_t _if__result_2303;
  int32_t _M0L6_2atmpS1282;
  int32_t _if__result_2304;
  int32_t _M0L6_2atmpS1284;
  int32_t _M0L6_2atmpS1285;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1294 = _M0L4selfS201.$2;
  _M0L5startS1295 = _M0L4selfS201.$1;
  _M0L3lenS200 = _M0L3endS1294 - _M0L5startS1295;
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
  _M0L6_2atmpS1272 = _M0Lm2loS202;
  if (_M0L6_2atmpS1272 > 0) {
    int32_t _M0L6_2atmpS1271 = _M0Lm2loS202;
    if (_M0L6_2atmpS1271 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1270 = _M0Lm2loS202;
      int32_t _M0L6_2atmpS1269 = _M0L4baseS209 + _M0L6_2atmpS1270;
      int32_t _M0L6_2atmpS1268 = _M0L3strS208[_M0L6_2atmpS1269];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1268)) {
        int32_t _M0L6_2atmpS1267 = _M0Lm2loS202;
        int32_t _M0L6_2atmpS1266 = _M0L4baseS209 + _M0L6_2atmpS1267;
        int32_t _M0L6_2atmpS1265 = _M0L6_2atmpS1266 - 1;
        int32_t _M0L6_2atmpS1264 = _M0L3strS208[_M0L6_2atmpS1265];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2303
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1264);
      } else {
        _if__result_2303 = 0;
      }
    } else {
      _if__result_2303 = 0;
    }
  } else {
    _if__result_2303 = 0;
  }
  if (_if__result_2303) {
    int32_t _M0L6_2atmpS1273 = _M0Lm2loS202;
    _M0Lm2loS202 = _M0L6_2atmpS1273 + 1;
  }
  _M0L6_2atmpS1282 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1282 > 0) {
    int32_t _M0L6_2atmpS1281 = _M0Lm2hiS204;
    if (_M0L6_2atmpS1281 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1280 = _M0Lm2hiS204;
      int32_t _M0L6_2atmpS1279 = _M0L4baseS209 + _M0L6_2atmpS1280;
      int32_t _M0L6_2atmpS1278 = _M0L3strS208[_M0L6_2atmpS1279];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1278)) {
        int32_t _M0L6_2atmpS1277 = _M0Lm2hiS204;
        int32_t _M0L6_2atmpS1276 = _M0L4baseS209 + _M0L6_2atmpS1277;
        int32_t _M0L6_2atmpS1275 = _M0L6_2atmpS1276 - 1;
        int32_t _M0L6_2atmpS1274 = _M0L3strS208[_M0L6_2atmpS1275];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2304
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1274);
      } else {
        _if__result_2304 = 0;
      }
    } else {
      _if__result_2304 = 0;
    }
  } else {
    _if__result_2304 = 0;
  }
  if (_if__result_2304) {
    int32_t _M0L6_2atmpS1283 = _M0Lm2hiS204;
    _M0Lm2hiS204 = _M0L6_2atmpS1283 - 1;
  }
  _M0L6_2atmpS1284 = _M0Lm2loS202;
  _M0L6_2atmpS1285 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1284 >= _M0L6_2atmpS1285) {
    int32_t _M0L6_2atmpS1289 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1286 = _M0L4baseS209 + _M0L6_2atmpS1289;
    int32_t _M0L6_2atmpS1288 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1287 = _M0L4baseS209 + _M0L6_2atmpS1288;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1286,
                                                 .$2 = _M0L6_2atmpS1287};
  } else {
    int32_t _M0L6_2atmpS1293 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1290 = _M0L4baseS209 + _M0L6_2atmpS1293;
    int32_t _M0L6_2atmpS1292 = _M0Lm2hiS204;
    int32_t _M0L6_2atmpS1291 = _M0L4baseS209 + _M0L6_2atmpS1292;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1290,
                                                 .$2 = _M0L6_2atmpS1291};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS199) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS198;
  int32_t _M0L6_2atmpS1261;
  int32_t _M0L6_2atmpS1260;
  int32_t _M0L6_2atmpS1263;
  int32_t _M0L6_2atmpS1262;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1259;
  moonbit_string_t _result_2305;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1261 = _M0IPC14byte4BytePB3Div3div(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1260
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1261);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1260);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1263 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1262
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1263);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1262);
  _M0L6_2atmpS1259 = _M0L7_2aselfS198;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2305 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1259);
  moonbit_decref_cycle_free(_M0L6_2atmpS1259);
  return _result_2305;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS197) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS197 < 10) {
    int32_t _M0L6_2atmpS1256;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1256 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1256);
  } else {
    int32_t _M0L6_2atmpS1258;
    int32_t _M0L6_2atmpS1257;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1258 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1257 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1258, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1257);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS1254;
  int32_t _M0L6_2atmpS1255;
  int32_t _M0L6_2atmpS1253;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1254 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS1255 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS1253 = _M0L6_2atmpS1254 - _M0L6_2atmpS1255;
  return _M0L6_2atmpS1253 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS1251;
  int32_t _M0L6_2atmpS1252;
  int32_t _M0L6_2atmpS1250;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1251 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS1252 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS1250 = _M0L6_2atmpS1251 % _M0L6_2atmpS1252;
  return _M0L6_2atmpS1250 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS1248;
  int32_t _M0L6_2atmpS1249;
  int32_t _M0L6_2atmpS1247;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1248 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS1249 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS1247 = _M0L6_2atmpS1248 / _M0L6_2atmpS1249;
  return _M0L6_2atmpS1247 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS189,
  int32_t _M0L4thatS190
) {
  int32_t _M0L6_2atmpS1245;
  int32_t _M0L6_2atmpS1246;
  int32_t _M0L6_2atmpS1244;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1245 = (int32_t)_M0L4selfS189;
  _M0L6_2atmpS1246 = (int32_t)_M0L4thatS190;
  _M0L6_2atmpS1244 = _M0L6_2atmpS1245 + _M0L6_2atmpS1246;
  return _M0L6_2atmpS1244 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS188) {
  int32_t _M0L6_2atmpS1243;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1243 = (int32_t)_M0L4selfS188;
  return _M0L6_2atmpS1243;
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
  int32_t _M0L3lenS1242;
  int32_t _M0L8requiredS184;
  uint16_t* _M0L4dataS1237;
  int32_t _M0L6_2atmpS1236;
  int32_t _if__result_2306;
  uint16_t* _M0L4dataS1238;
  int32_t _M0L3lenS1239;
  int32_t _M0L3lenS1241;
  int32_t _M0L6_2atmpS1240;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS182 = Moonbit_array_length(_M0L3strS183);
  if (_M0L8str__lenS182 == 0) {
    return 0;
  }
  _M0L3lenS1242 = _M0L4selfS185->$1;
  _M0L8requiredS184 = _M0L3lenS1242 + _M0L8str__lenS182;
  _M0L4dataS1237 = _M0L4selfS185->$0;
  _M0L6_2atmpS1236 = Moonbit_array_length(_M0L4dataS1237);
  if (_M0L8requiredS184 > _M0L6_2atmpS1236) {
    _if__result_2306 = 1;
  } else {
    int32_t _M0L3lenS1235 = _M0L4selfS185->$1;
    _if__result_2306 = _M0L8requiredS184 < _M0L3lenS1235;
  }
  if (_if__result_2306) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS185, _M0L8requiredS184);
  }
  _M0L4dataS1238 = _M0L4selfS185->$0;
  _M0L3lenS1239 = _M0L4selfS185->$1;
  moonbit_incref_cycle_free(_M0L4dataS1238);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1238, _M0L3lenS1239, _M0L3strS183, 0, _M0L8str__lenS182);
  moonbit_decref_cycle_free(_M0L4dataS1238);
  _M0L3lenS1241 = _M0L4selfS185->$1;
  _M0L6_2atmpS1240 = _M0L3lenS1241 + _M0L8str__lenS182;
  _M0L4selfS185->$1 = _M0L6_2atmpS1240;
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
      int32_t _M0L6_2atmpS1232 = _M0L3strS179[_M0L1iS176];
      int32_t _M0L6_2atmpS1233;
      int32_t _M0L6_2atmpS1234;
      _M0L4selfS178[_M0L1jS177] = _M0L6_2atmpS1232;
      _M0L6_2atmpS1233 = _M0L1iS176 + 1;
      _M0L6_2atmpS1234 = _M0L1jS177 + 1;
      _M0L1iS176 = _M0L6_2atmpS1233;
      _M0L1jS177 = _M0L6_2atmpS1234;
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
    int32_t _M0L3lenS1203 = _M0L4selfS171->$1;
    uint16_t* _M0L4dataS1205 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1204 = Moonbit_array_length(_M0L4dataS1205);
    uint16_t* _M0L4dataS1208;
    int32_t _M0L3lenS1209;
    int32_t _M0L6_2atmpS1210;
    int32_t _M0L3lenS1212;
    int32_t _M0L6_2atmpS1211;
    if (_M0L3lenS1203 >= _M0L6_2atmpS1204) {
      int32_t _M0L3lenS1207 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1206 = _M0L3lenS1207 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1206);
    }
    _M0L4dataS1208 = _M0L4selfS171->$0;
    _M0L3lenS1209 = _M0L4selfS171->$1;
    moonbit_incref_cycle_free(_M0L4dataS1208);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1210 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS169);
    if (
      _M0L3lenS1209 < 0
      || _M0L3lenS1209 >= Moonbit_array_length(_M0L4dataS1208)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1208[_M0L3lenS1209] = _M0L6_2atmpS1210;
    moonbit_decref_cycle_free(_M0L4dataS1208);
    _M0L3lenS1212 = _M0L4selfS171->$1;
    _M0L6_2atmpS1211 = _M0L3lenS1212 + 1;
    _M0L4selfS171->$1 = _M0L6_2atmpS1211;
  } else if (_M0L4codeS169 <= 1114111u) {
    uint16_t* _M0L4dataS1216 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1214 = Moonbit_array_length(_M0L4dataS1216);
    int32_t _M0L3lenS1215 = _M0L4selfS171->$1;
    int32_t _M0L6_2atmpS1213 = _M0L6_2atmpS1214 - _M0L3lenS1215;
    uint32_t _M0L4codeS172;
    uint16_t* _M0L4dataS1219;
    int32_t _M0L3lenS1220;
    uint32_t _M0L6_2atmpS1223;
    uint32_t _M0L6_2atmpS1222;
    int32_t _M0L6_2atmpS1221;
    uint16_t* _M0L4dataS1224;
    int32_t _M0L3lenS1229;
    int32_t _M0L6_2atmpS1225;
    uint32_t _M0L6_2atmpS1228;
    uint32_t _M0L6_2atmpS1227;
    int32_t _M0L6_2atmpS1226;
    int32_t _M0L3lenS1231;
    int32_t _M0L6_2atmpS1230;
    if (_M0L6_2atmpS1213 < 2) {
      int32_t _M0L3lenS1218 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1217 = _M0L3lenS1218 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1217);
    }
    _M0L4codeS172 = _M0L4codeS169 - 65536u;
    _M0L4dataS1219 = _M0L4selfS171->$0;
    _M0L3lenS1220 = _M0L4selfS171->$1;
    _M0L6_2atmpS1223 = _M0L4codeS172 >> 10;
    _M0L6_2atmpS1222 = 55296u + _M0L6_2atmpS1223;
    moonbit_incref_cycle_free(_M0L4dataS1219);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1221 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1222);
    if (
      _M0L3lenS1220 < 0
      || _M0L3lenS1220 >= Moonbit_array_length(_M0L4dataS1219)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1219[_M0L3lenS1220] = _M0L6_2atmpS1221;
    moonbit_decref_cycle_free(_M0L4dataS1219);
    _M0L4dataS1224 = _M0L4selfS171->$0;
    _M0L3lenS1229 = _M0L4selfS171->$1;
    _M0L6_2atmpS1225 = _M0L3lenS1229 + 1;
    _M0L6_2atmpS1228 = _M0L4codeS172 & 1023u;
    _M0L6_2atmpS1227 = 56320u + _M0L6_2atmpS1228;
    moonbit_incref_cycle_free(_M0L4dataS1224);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1226 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1227);
    if (
      _M0L6_2atmpS1225 < 0
      || _M0L6_2atmpS1225 >= Moonbit_array_length(_M0L4dataS1224)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1224[_M0L6_2atmpS1225] = _M0L6_2atmpS1226;
    moonbit_decref_cycle_free(_M0L4dataS1224);
    _M0L3lenS1231 = _M0L4selfS171->$1;
    _M0L6_2atmpS1230 = _M0L3lenS1231 + 2;
    _M0L4selfS171->$1 = _M0L6_2atmpS1230;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_29.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS166,
  int32_t _M0L8requiredS167
) {
  uint16_t* _M0L4dataS1202;
  int32_t _M0L6_2atmpS1200;
  int32_t _M0L3lenS1201;
  int32_t _M0L13new__capacityS165;
  uint16_t* _M0L4dataS1197;
  int32_t _M0L6_2atmpS1198;
  int32_t _M0L3lenS1199;
  uint16_t* _M0L9new__dataS168;
  uint16_t* _M0L6_2aoldS2177;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1202 = _M0L4selfS166->$0;
  _M0L6_2atmpS1200 = Moonbit_array_length(_M0L4dataS1202);
  _M0L3lenS1201 = _M0L4selfS166->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS165
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1200, _M0L3lenS1201, _M0L8requiredS167);
  _M0L4dataS1197 = _M0L4selfS166->$0;
  moonbit_incref_cycle_free(_M0L4dataS1197);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1198 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1199 = _M0L4selfS166->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS168
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1197, _M0L13new__capacityS165, _M0L6_2atmpS1198, _M0L3lenS1199, 0, 0);
  _M0L6_2aoldS2177 = _M0L4selfS166->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2177);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_30.data);
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
  int32_t _M0L6_2atmpS1196;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1196 = *(int32_t*)&_M0L4selfS158;
  return (uint16_t)_M0L6_2atmpS1196;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS157) {
  int32_t _M0L6_2atmpS1195;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1195 = _M0L4selfS157;
  return *(uint32_t*)&_M0L6_2atmpS1195;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS155
) {
  int32_t _M0L3lenS1186;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1186 = _M0L4selfS155->$1;
  if (_M0L3lenS1186 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1187 = _M0L4selfS155->$1;
    uint16_t* _M0L4dataS1189 = _M0L4selfS155->$0;
    int32_t _M0L6_2atmpS1188 = Moonbit_array_length(_M0L4dataS1189);
    if (_M0L3lenS1187 == _M0L6_2atmpS1188) {
      uint16_t* _M0L4dataS1190 = _M0L4selfS155->$0;
      moonbit_incref_cycle_free(_M0L4dataS1190);
      return _M0L4dataS1190;
    } else {
      uint16_t* _M0L4dataS1191 = _M0L4selfS155->$0;
      int32_t _M0L3lenS1192 = _M0L4selfS155->$1;
      int32_t _M0L6_2atmpS1193;
      int32_t _M0L3lenS1194;
      uint16_t* _M0L4dataS156;
      moonbit_incref_cycle_free(_M0L4dataS1191);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1193 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1194 = _M0L4selfS155->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS156
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1191, _M0L3lenS1192, _M0L6_2atmpS1193, _M0L3lenS1194, 0, 0);
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
  int32_t _if__result_2309;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS148 >= 0) {
    if (_M0L3lenS149 >= 0) {
      if (_M0L11src__offsetS150 >= 0) {
        if (_M0L11dst__offsetS151 >= 0) {
          int32_t _M0L6_2atmpS1182 = _M0L11src__offsetS150 + _M0L3lenS149;
          int32_t _M0L6_2atmpS1183 = Moonbit_array_length(_M0L3srcS152);
          if (_M0L6_2atmpS1182 <= _M0L6_2atmpS1183) {
            int32_t _M0L6_2atmpS1181 = _M0L11dst__offsetS151 + _M0L3lenS149;
            _if__result_2309 = _M0L6_2atmpS1181 <= _M0L13allocate__lenS148;
          } else {
            _if__result_2309 = 0;
          }
        } else {
          _if__result_2309 = 0;
        }
      } else {
        _if__result_2309 = 0;
      }
    } else {
      _if__result_2309 = 0;
    }
  } else {
    _if__result_2309 = 0;
  }
  if (_if__result_2309) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS152, _M0L13allocate__lenS148, _M0L4initS153, _M0L11src__offsetS150, _M0L11dst__offsetS151, _M0L3lenS149);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS154;
    int32_t _M0L6_2atmpS1185;
    moonbit_string_t _M0L6_2atmpS1184;
    uint16_t* _result_2310;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS154
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L13allocate__lenS148);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11src__offsetS150);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11dst__offsetS151);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L3lenS149);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_35.data);
    _M0L6_2atmpS1185 = Moonbit_array_length(_M0L3srcS152);
    moonbit_decref_cycle_free(_M0L3srcS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L6_2atmpS1185);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1184
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS154);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS154);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2310 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1184);
    moonbit_decref_cycle_free(_M0L6_2atmpS1184);
    return _result_2310;
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
  struct _M0TPB13StringBuilder* _block_2311;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS139 < 1) {
    _M0L7initialS138 = 1;
  } else {
    int32_t _M0L6_2atmpS1180 = _M0L10size__hintS139 + 1;
    _M0L7initialS138 = _M0L6_2atmpS1180 / 2;
  }
  _M0L4dataS140 = (uint16_t*)moonbit_make_string(_M0L7initialS138, 0);
  _block_2311
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2311)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 44, 0);
  _block_2311->$0 = _M0L4dataS140;
  _block_2311->$1 = 0;
  return _block_2311;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS137) {
  int32_t _M0L6_2atmpS1179;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1179 = (int32_t)_M0L4selfS137;
  return _M0L6_2atmpS1179;
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS117,
  int32_t _M0L13allocate__lenS113,
  int32_t _M0L3lenS114,
  int32_t _M0L11src__offsetS115,
  int32_t _M0L11dst__offsetS116
) {
  int32_t _if__result_2312;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS113 >= 0) {
    if (_M0L3lenS114 >= 0) {
      if (_M0L11src__offsetS115 >= 0) {
        if (_M0L11dst__offsetS116 >= 0) {
          int32_t _M0L6_2atmpS1160 = _M0L11src__offsetS115 + _M0L3lenS114;
          int32_t _M0L6_2atmpS1161;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1161
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS117);
          if (_M0L6_2atmpS1160 <= _M0L6_2atmpS1161) {
            int32_t _M0L6_2atmpS1159 = _M0L11dst__offsetS116 + _M0L3lenS114;
            _if__result_2312 = _M0L6_2atmpS1159 <= _M0L13allocate__lenS113;
          } else {
            _if__result_2312 = 0;
          }
        } else {
          _if__result_2312 = 0;
        }
      } else {
        _if__result_2312 = 0;
      }
    } else {
      _if__result_2312 = 0;
    }
  } else {
    _if__result_2312 = 0;
  }
  if (_if__result_2312) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS117, _M0L13allocate__lenS113, _M0L11src__offsetS115, _M0L11dst__offsetS116, _M0L3lenS114);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS118;
    int32_t _M0L6_2atmpS1163;
    moonbit_string_t _M0L6_2atmpS1162;
    int32_t* _result_2313;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS118
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L13allocate__lenS113);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11src__offsetS115);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11dst__offsetS116);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L3lenS114);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1163 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS117);
    moonbit_decref_cycle_free(_M0L3srcS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L6_2atmpS1163);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1162
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS118);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS118);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2313
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1162);
    moonbit_decref_cycle_free(_M0L6_2atmpS1162);
    return _result_2313;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS123,
  int32_t _M0L13allocate__lenS119,
  int32_t _M0L3lenS120,
  int32_t _M0L11src__offsetS121,
  int32_t _M0L11dst__offsetS122
) {
  int32_t _if__result_2314;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS119 >= 0) {
    if (_M0L3lenS120 >= 0) {
      if (_M0L11src__offsetS121 >= 0) {
        if (_M0L11dst__offsetS122 >= 0) {
          int32_t _M0L6_2atmpS1165 = _M0L11src__offsetS121 + _M0L3lenS120;
          int32_t _M0L6_2atmpS1166;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1166
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS123);
          if (_M0L6_2atmpS1165 <= _M0L6_2atmpS1166) {
            int32_t _M0L6_2atmpS1164 = _M0L11dst__offsetS122 + _M0L3lenS120;
            _if__result_2314 = _M0L6_2atmpS1164 <= _M0L13allocate__lenS119;
          } else {
            _if__result_2314 = 0;
          }
        } else {
          _if__result_2314 = 0;
        }
      } else {
        _if__result_2314 = 0;
      }
    } else {
      _if__result_2314 = 0;
    }
  } else {
    _if__result_2314 = 0;
  }
  if (_if__result_2314) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS123, _M0L13allocate__lenS119, _M0L11src__offsetS121, _M0L11dst__offsetS122, _M0L3lenS120);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS124;
    int32_t _M0L6_2atmpS1168;
    moonbit_string_t _M0L6_2atmpS1167;
    float* _result_2315;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS124
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L13allocate__lenS119);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11src__offsetS121);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11dst__offsetS122);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L3lenS120);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1168 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS123);
    moonbit_decref_cycle_free(_M0L3srcS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L6_2atmpS1168);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1167
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS124);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS124);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2315
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1167);
    moonbit_decref_cycle_free(_M0L6_2atmpS1167);
    return _result_2315;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS129,
  int32_t _M0L13allocate__lenS125,
  int32_t _M0L3lenS126,
  int32_t _M0L11src__offsetS127,
  int32_t _M0L11dst__offsetS128
) {
  int32_t _if__result_2316;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS125 >= 0) {
    if (_M0L3lenS126 >= 0) {
      if (_M0L11src__offsetS127 >= 0) {
        if (_M0L11dst__offsetS128 >= 0) {
          int32_t _M0L6_2atmpS1170 = _M0L11src__offsetS127 + _M0L3lenS126;
          int32_t _M0L6_2atmpS1171;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1171
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS129);
          if (_M0L6_2atmpS1170 <= _M0L6_2atmpS1171) {
            int32_t _M0L6_2atmpS1169 = _M0L11dst__offsetS128 + _M0L3lenS126;
            _if__result_2316 = _M0L6_2atmpS1169 <= _M0L13allocate__lenS125;
          } else {
            _if__result_2316 = 0;
          }
        } else {
          _if__result_2316 = 0;
        }
      } else {
        _if__result_2316 = 0;
      }
    } else {
      _if__result_2316 = 0;
    }
  } else {
    _if__result_2316 = 0;
  }
  if (_if__result_2316) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS125, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS129, _M0L11src__offsetS127, _M0L11dst__offsetS128, _M0L3lenS126);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS130;
    int32_t _M0L6_2atmpS1173;
    moonbit_string_t _M0L6_2atmpS1172;
    moonbit_string_t* _result_2317;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS130
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L13allocate__lenS125);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11src__offsetS127);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11dst__offsetS128);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L3lenS126);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1173 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS129);
    moonbit_decref_cycle_free(_M0L3srcS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L6_2atmpS1173);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1172
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS130);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS130);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2317
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1172);
    moonbit_decref_cycle_free(_M0L6_2atmpS1172);
    return _result_2317;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS135,
  int32_t _M0L13allocate__lenS131,
  int32_t _M0L3lenS132,
  int32_t _M0L11src__offsetS133,
  int32_t _M0L11dst__offsetS134
) {
  int32_t _if__result_2318;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS131 >= 0) {
    if (_M0L3lenS132 >= 0) {
      if (_M0L11src__offsetS133 >= 0) {
        if (_M0L11dst__offsetS134 >= 0) {
          int32_t _M0L6_2atmpS1175 = _M0L11src__offsetS133 + _M0L3lenS132;
          int32_t _M0L6_2atmpS1176;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1176
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS135);
          if (_M0L6_2atmpS1175 <= _M0L6_2atmpS1176) {
            int32_t _M0L6_2atmpS1174 = _M0L11dst__offsetS134 + _M0L3lenS132;
            _if__result_2318 = _M0L6_2atmpS1174 <= _M0L13allocate__lenS131;
          } else {
            _if__result_2318 = 0;
          }
        } else {
          _if__result_2318 = 0;
        }
      } else {
        _if__result_2318 = 0;
      }
    } else {
      _if__result_2318 = 0;
    }
  } else {
    _if__result_2318 = 0;
  }
  if (_if__result_2318) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS131, 0, _M0L3srcS135, _M0L11src__offsetS133, _M0L11dst__offsetS134, _M0L3lenS132);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS136;
    int32_t _M0L6_2atmpS1178;
    moonbit_string_t _M0L6_2atmpS1177;
    struct _M0TUsiE** _result_2319;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS136
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L13allocate__lenS131);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11src__offsetS133);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11dst__offsetS134);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L3lenS132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1178 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS135);
    moonbit_decref_cycle_free(_M0L3srcS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L6_2atmpS1178);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1177
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS136);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS136);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2319
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1177);
    moonbit_decref_cycle_free(_M0L6_2atmpS1177);
    return _result_2319;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L3objS107
) {
  struct _M0TPB6Logger _M0L6_2atmpS1156;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS108);
  _M0L6_2atmpS1156
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS108
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS107, _M0L6_2atmpS1156);
  if (_M0L6_2atmpS1156.$1) {
    moonbit_decref(_M0L6_2atmpS1156.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS1157;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS1157
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS109, _M0L6_2atmpS1157);
  if (_M0L6_2atmpS1157.$1) {
    moonbit_decref(_M0L6_2atmpS1157.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  uint64_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS1158;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS1158
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS111, _M0L6_2atmpS1158);
  if (_M0L6_2atmpS1158.$1) {
    moonbit_decref(_M0L6_2atmpS1158.$1);
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
        int32_t _M0L6_2atmpS1111 = _M0L11dst__offsetS20 + _M0L1iS22;
        int32_t _M0L6_2atmpS1113 = _M0L11src__offsetS21 + _M0L1iS22;
        int32_t _M0L6_2atmpS1112;
        int32_t _M0L6_2atmpS1114;
        if (
          _M0L6_2atmpS1113 < 0
          || _M0L6_2atmpS1113 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1112 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1113];
        if (
          _M0L6_2atmpS1111 < 0
          || _M0L6_2atmpS1111 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1111] = _M0L6_2atmpS1112;
        _M0L6_2atmpS1114 = _M0L1iS22 + 1;
        _M0L1iS22 = _M0L6_2atmpS1114;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1119 = _M0L3lenS23 - 1;
    int32_t _M0L1iS25 = _M0L6_2atmpS1119;
    while (1) {
      if (_M0L1iS25 >= 0) {
        int32_t _M0L6_2atmpS1115 = _M0L11dst__offsetS20 + _M0L1iS25;
        int32_t _M0L6_2atmpS1117 = _M0L11src__offsetS21 + _M0L1iS25;
        int32_t _M0L6_2atmpS1116;
        int32_t _M0L6_2atmpS1118;
        if (
          _M0L6_2atmpS1117 < 0
          || _M0L6_2atmpS1117 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1116 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1117];
        if (
          _M0L6_2atmpS1115 < 0
          || _M0L6_2atmpS1115 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1115] = _M0L6_2atmpS1116;
        _M0L6_2atmpS1118 = _M0L1iS25 - 1;
        _M0L1iS25 = _M0L6_2atmpS1118;
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
        int32_t _M0L6_2atmpS1120 = _M0L11dst__offsetS29 + _M0L1iS31;
        int32_t _M0L6_2atmpS1122 = _M0L11src__offsetS30 + _M0L1iS31;
        int32_t _M0L6_2atmpS1121;
        int32_t _M0L6_2atmpS1123;
        if (
          _M0L6_2atmpS1122 < 0
          || _M0L6_2atmpS1122 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1121 = (int32_t)_M0L3srcS28[_M0L6_2atmpS1122];
        if (
          _M0L6_2atmpS1120 < 0
          || _M0L6_2atmpS1120 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS1120] = _M0L6_2atmpS1121;
        _M0L6_2atmpS1123 = _M0L1iS31 + 1;
        _M0L1iS31 = _M0L6_2atmpS1123;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1128 = _M0L3lenS32 - 1;
    int32_t _M0L1iS34 = _M0L6_2atmpS1128;
    while (1) {
      if (_M0L1iS34 >= 0) {
        int32_t _M0L6_2atmpS1124 = _M0L11dst__offsetS29 + _M0L1iS34;
        int32_t _M0L6_2atmpS1126 = _M0L11src__offsetS30 + _M0L1iS34;
        int32_t _M0L6_2atmpS1125;
        int32_t _M0L6_2atmpS1127;
        if (
          _M0L6_2atmpS1126 < 0
          || _M0L6_2atmpS1126 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1125 = (int32_t)_M0L3srcS28[_M0L6_2atmpS1126];
        if (
          _M0L6_2atmpS1124 < 0
          || _M0L6_2atmpS1124 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS1124] = _M0L6_2atmpS1125;
        _M0L6_2atmpS1127 = _M0L1iS34 - 1;
        _M0L1iS34 = _M0L6_2atmpS1127;
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
        int32_t _M0L6_2atmpS1129 = _M0L11dst__offsetS38 + _M0L1iS40;
        int32_t _M0L6_2atmpS1131 = _M0L11src__offsetS39 + _M0L1iS40;
        float _M0L6_2atmpS1130;
        int32_t _M0L6_2atmpS1132;
        if (
          _M0L6_2atmpS1131 < 0
          || _M0L6_2atmpS1131 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1130 = (float)_M0L3srcS37[_M0L6_2atmpS1131];
        if (
          _M0L6_2atmpS1129 < 0
          || _M0L6_2atmpS1129 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS36[_M0L6_2atmpS1129] = _M0L6_2atmpS1130;
        _M0L6_2atmpS1132 = _M0L1iS40 + 1;
        _M0L1iS40 = _M0L6_2atmpS1132;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1137 = _M0L3lenS41 - 1;
    int32_t _M0L1iS43 = _M0L6_2atmpS1137;
    while (1) {
      if (_M0L1iS43 >= 0) {
        int32_t _M0L6_2atmpS1133 = _M0L11dst__offsetS38 + _M0L1iS43;
        int32_t _M0L6_2atmpS1135 = _M0L11src__offsetS39 + _M0L1iS43;
        float _M0L6_2atmpS1134;
        int32_t _M0L6_2atmpS1136;
        if (
          _M0L6_2atmpS1135 < 0
          || _M0L6_2atmpS1135 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1134 = (float)_M0L3srcS37[_M0L6_2atmpS1135];
        if (
          _M0L6_2atmpS1133 < 0
          || _M0L6_2atmpS1133 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS36[_M0L6_2atmpS1133] = _M0L6_2atmpS1134;
        _M0L6_2atmpS1136 = _M0L1iS43 - 1;
        _M0L1iS43 = _M0L6_2atmpS1136;
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
        int32_t _M0L6_2atmpS1138 = _M0L11dst__offsetS47 + _M0L1iS49;
        int32_t _M0L6_2atmpS1140 = _M0L11src__offsetS48 + _M0L1iS49;
        moonbit_string_t _M0L6_2atmpS1139;
        moonbit_string_t _M0L6_2aoldS2178;
        int32_t _M0L6_2atmpS1141;
        if (
          _M0L6_2atmpS1140 < 0
          || _M0L6_2atmpS1140 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1139 = (moonbit_string_t)_M0L3srcS46[_M0L6_2atmpS1140];
        if (
          _M0L6_2atmpS1138 < 0
          || _M0L6_2atmpS1138 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2178 = (moonbit_string_t)_M0L3dstS45[_M0L6_2atmpS1138];
        moonbit_incref_cycle_free(_M0L6_2atmpS1139);
        moonbit_decref_cycle_free(_M0L6_2aoldS2178);
        _M0L3dstS45[_M0L6_2atmpS1138] = _M0L6_2atmpS1139;
        _M0L6_2atmpS1141 = _M0L1iS49 + 1;
        _M0L1iS49 = _M0L6_2atmpS1141;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1146 = _M0L3lenS50 - 1;
    int32_t _M0L1iS52 = _M0L6_2atmpS1146;
    while (1) {
      if (_M0L1iS52 >= 0) {
        int32_t _M0L6_2atmpS1142 = _M0L11dst__offsetS47 + _M0L1iS52;
        int32_t _M0L6_2atmpS1144 = _M0L11src__offsetS48 + _M0L1iS52;
        moonbit_string_t _M0L6_2atmpS1143;
        moonbit_string_t _M0L6_2aoldS2179;
        int32_t _M0L6_2atmpS1145;
        if (
          _M0L6_2atmpS1144 < 0
          || _M0L6_2atmpS1144 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1143 = (moonbit_string_t)_M0L3srcS46[_M0L6_2atmpS1144];
        if (
          _M0L6_2atmpS1142 < 0
          || _M0L6_2atmpS1142 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2179 = (moonbit_string_t)_M0L3dstS45[_M0L6_2atmpS1142];
        moonbit_incref_cycle_free(_M0L6_2atmpS1143);
        moonbit_decref_cycle_free(_M0L6_2aoldS2179);
        _M0L3dstS45[_M0L6_2atmpS1142] = _M0L6_2atmpS1143;
        _M0L6_2atmpS1145 = _M0L1iS52 - 1;
        _M0L1iS52 = _M0L6_2atmpS1145;
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
        int32_t _M0L6_2atmpS1147 = _M0L11dst__offsetS56 + _M0L1iS58;
        int32_t _M0L6_2atmpS1149 = _M0L11src__offsetS57 + _M0L1iS58;
        struct _M0TUsiE* _M0L6_2atmpS1148;
        struct _M0TUsiE* _M0L6_2aoldS2180;
        int32_t _M0L6_2atmpS1150;
        if (
          _M0L6_2atmpS1149 < 0
          || _M0L6_2atmpS1149 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1148 = (struct _M0TUsiE*)_M0L3srcS55[_M0L6_2atmpS1149];
        if (
          _M0L6_2atmpS1147 < 0
          || _M0L6_2atmpS1147 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2180 = (struct _M0TUsiE*)_M0L3dstS54[_M0L6_2atmpS1147];
        if (_M0L6_2atmpS1148) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1148);
        }
        if (_M0L6_2aoldS2180) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2180);
        }
        _M0L3dstS54[_M0L6_2atmpS1147] = _M0L6_2atmpS1148;
        _M0L6_2atmpS1150 = _M0L1iS58 + 1;
        _M0L1iS58 = _M0L6_2atmpS1150;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1155 = _M0L3lenS59 - 1;
    int32_t _M0L1iS61 = _M0L6_2atmpS1155;
    while (1) {
      if (_M0L1iS61 >= 0) {
        int32_t _M0L6_2atmpS1151 = _M0L11dst__offsetS56 + _M0L1iS61;
        int32_t _M0L6_2atmpS1153 = _M0L11src__offsetS57 + _M0L1iS61;
        struct _M0TUsiE* _M0L6_2atmpS1152;
        struct _M0TUsiE* _M0L6_2aoldS2181;
        int32_t _M0L6_2atmpS1154;
        if (
          _M0L6_2atmpS1153 < 0
          || _M0L6_2atmpS1153 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1152 = (struct _M0TUsiE*)_M0L3srcS55[_M0L6_2atmpS1153];
        if (
          _M0L6_2atmpS1151 < 0
          || _M0L6_2atmpS1151 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2181 = (struct _M0TUsiE*)_M0L3dstS54[_M0L6_2atmpS1151];
        if (_M0L6_2atmpS1152) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1152);
        }
        if (_M0L6_2aoldS2181) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2181);
        }
        _M0L3dstS54[_M0L6_2atmpS1151] = _M0L6_2atmpS1152;
        _M0L6_2atmpS1154 = _M0L1iS61 - 1;
        _M0L1iS61 = _M0L6_2atmpS1154;
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
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_36.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S13, _M0L15_2a_2aarg__6389S12);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_37.data);
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1081) {
  switch (Moonbit_object_tag(_M0L4_2aeS1081)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_38.data;
      break;
    }
    
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_39.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1081);
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_40.data;
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_41.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1106,
  struct _M0TPB4Show _M0L8_2aparamS1105
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1104 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1106;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1104, _M0L8_2aparamS1105);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1103,
  struct _M0TPB4Show _M0L8_2aparamS1102
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1101 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1103;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1101, _M0L8_2aparamS1102);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1100,
  int32_t _M0L8_2aparamS1099
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1098 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1100;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1098, _M0L8_2aparamS1099);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1097,
  struct _M0TPC16string10StringView _M0L8_2aparamS1096
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1095 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1097;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1095, _M0L8_2aparamS1096);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1094,
  moonbit_string_t _M0L8_2aparamS1091,
  int32_t _M0L8_2aparamS1092,
  int32_t _M0L8_2aparamS1093
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1090 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1094;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1090, _M0L8_2aparamS1091, _M0L8_2aparamS1092, _M0L8_2aparamS1093);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1089,
  moonbit_string_t _M0L8_2aparamS1088
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1087 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1089;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1087, _M0L8_2aparamS1088);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_2330 = 9218868437227405311ll;
  int64_t _tmp_2331;
  int64_t _tmp_2332;
  int64_t _tmp_2333;
  int64_t _tmp_2334;
  _M0FPB18double__max__value = *(double*)&_tmp_2330;
  _tmp_2331 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_2331;
  _tmp_2332 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_2332;
  _tmp_2333 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_2333;
  _tmp_2334 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_2334;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1110;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1074;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1075;
  int32_t _M0L7_2abindS1076;
  struct _M0TUsiE** _M0L7_2abindS1077;
  int32_t _M0L6_2acntS2186;
  int32_t _M0L2__S1078;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1110
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1074
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1074)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 47, 0);
  _M0L12async__testsS1074->$0 = _M0L6_2atmpS1110;
  _M0L12async__testsS1074->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1075
  = _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1076 = _M0L7_2abindS1075->$1;
  _M0L7_2abindS1077 = _M0L7_2abindS1075->$0;
  _M0L6_2acntS2186
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1075));
  if (_M0L6_2acntS2186 > 1) {
    int32_t _M0L11_2anew__cntS2187 = _M0L6_2acntS2186 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1075), _M0L11_2anew__cntS2187);
    moonbit_incref_cycle_free(_M0L7_2abindS1077);
  } else if (_M0L6_2acntS2186 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1075);
  }
  _M0L2__S1078 = 0;
  while (1) {
    if (_M0L2__S1078 < _M0L7_2abindS1076) {
      struct _M0TUsiE* _M0L3argS1079 =
        (struct _M0TUsiE*)_M0L7_2abindS1077[_M0L2__S1078];
      moonbit_string_t _M0L6_2atmpS1107 = _M0L3argS1079->$0;
      int32_t _M0L6_2atmpS1108 = _M0L3argS1079->$1;
      int32_t _M0L6_2atmpS1109;
      moonbit_incref_cycle_free(_M0L6_2atmpS1107);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples27out__degree__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1074, _M0L6_2atmpS1107, _M0L6_2atmpS1108);
      moonbit_decref_cycle_free(_M0L6_2atmpS1107);
      _M0L6_2atmpS1109 = _M0L2__S1078 + 1;
      _M0L2__S1078 = _M0L6_2atmpS1109;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1077);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\out_degree\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples27out__degree__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples27out__degree__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1074);
  moonbit_decref_cycle_free(_M0L12async__testsS1074);
  moonbit_flush_cycles();
  return 0;
}