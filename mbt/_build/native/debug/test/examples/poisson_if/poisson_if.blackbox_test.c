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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__;

struct _M0TP26RiantR8snn__mbt14SpikingSynapse;

struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__;

struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE;

struct _M0TPB4Show;

struct _M0TPB8MutLocalGfE;

struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

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

struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2057;

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

struct _M0TP26RiantR8snn__mbt17BalancedParameter;

struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025;

struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus;

struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat;

struct _M0TUddE;

struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__;

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

struct _M0DTP26RiantR8snn__mbt6AnyPop12AdExSinExp__;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0DTP26RiantR8snn__mbt12STPEntryKind20MarkramSTPTimestep__;

struct _M0DTPC16option6OptionGfE4Some;

struct _M0TP26RiantR8snn__mbt20MorrisLecarParameter;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE;

struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus;

struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric;

struct _M0TWRPC15error5ErrorEu;

struct _M0TPB8MutLocalGiE;

struct _M0TP26RiantR8snn__mbt12STDPGerstner;

struct _M0TP26RiantR8snn__mbt11MorrisLecar;

struct _M0TP26RiantR8snn__mbt7Poisson;

struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

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

struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2052;

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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

struct _M0TPB4Show {
  struct _M0BTPB4Show* $0;
  void* $1;
  
};

struct _M0TPB8MutLocalGfE {
  float $0;
  
};

struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
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

struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2057 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0TPB5ArrayGRPB5ArrayGfEE {
  struct _M0TPB5ArrayGfE** $0;
  int32_t $1;
  
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

struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2052 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS2064(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS2057(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS2052(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS2029(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S2022(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples27poisson__if__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
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

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse6random(
  struct _M0TP26RiantR8snn__mbt2IF*,
  struct _M0TP26RiantR8snn__mbt2IF*,
  moonbit_string_t,
  float,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
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

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*,
  int32_t
);

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

int32_t _M0MPC15array5Array4pushGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE*,
  moonbit_string_t
);

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  struct _M0TUsiE*
);

int32_t _M0MPC15array5Array4pushGfE(struct _M0TPB5ArrayGfE*, float);

int32_t _M0MPC15array5Array7reallocGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array7reallocGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  int32_t
);

int32_t _M0MPC15array5Array7reallocGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE*,
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

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE*,
  int32_t
);

int32_t _M0MPC15array5Array8capacityGiE(struct _M0TPB5ArrayGiE*);

int32_t _M0MPC15array5Array8capacityGsE(struct _M0TPB5ArrayGsE*);

int32_t _M0MPC15array5Array8capacityGUsiEE(struct _M0TPB5ArrayGUsiEE*);

int32_t _M0MPC15array5Array8capacityGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0FPB23array__growth__capacity(int32_t, int32_t, int32_t);

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE*);

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE*);

moonbit_string_t* _M0MPC15array5Array6bufferGsE(struct _M0TPB5ArrayGsE*);

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE*
);

struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*
);

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*
);

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

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t*,
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

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t*,
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

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float*,
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

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t*);

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t*);

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(struct _M0TUsiE**);

int32_t _M0MPB18UninitializedArray6lengthGfE(float*);

int32_t _M0IPB7FailurePB4Show6output(void*, struct _M0TPB6Logger);

int32_t _M0MPB6Logger13write__objectGsE(
  struct _M0TPB6Logger,
  moonbit_string_t
);

int32_t _M0FPC15abort5abortGuE(moonbit_string_t);

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t);

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(moonbit_string_t);

moonbit_string_t* _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(
  moonbit_string_t
);

struct _M0TUsiE** _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(
  moonbit_string_t
);

int32_t _M0FPC15abort5abortGiE(moonbit_string_t);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[115]; 
} const moonbit_string_literal_37 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 114, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 112, 111, 105, 115, 115, 111, 110, 
    95, 105, 102, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 
    115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 
    68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 
    74, 115, 69, 114, 114, 111, 114, 46, 77, 111, 111, 110, 66, 105, 
    116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 
    101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[117]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 116, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 112, 111, 105, 115, 115, 111, 110, 
    95, 105, 102, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 
    115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 
    68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 
    83, 107, 105, 112, 84, 101, 115, 116, 46, 77, 111, 111, 110, 66, 
    105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 
    116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 
    0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct moonbit_object const moonbit_constant_constructor_0 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0)
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS2064$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS2064
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[123] =
  {
    sizeof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2052)
    / 4, 1,
    offsetof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2052, $1)
    / 4
    * 2,
    sizeof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2057)
    / 4, 1,
    offsetof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2057, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS5663
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS2085,
  moonbit_string_t _M0L8filenameS2054,
  int32_t _M0L5indexS2056
) {
  struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2052* _closure_5874;
  struct _M0TWEu* _M0L13handle__startS2052;
  struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2057* _closure_5875;
  struct _M0TWssbEu* _M0L14handle__resultS2057;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS2064;
  void* _M0L11_2atry__errS2079;
  struct moonbit_result_0 _tmp_5877;
  int32_t _handle__error__result_5878;
  int32_t _M0L6_2atmpS5651;
  void* _M0L3errS2080;
  moonbit_string_t _M0L4nameS2082;
  struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS2083;
  moonbit_string_t _M0L7_2anameS2084;
  int32_t _M0L6_2acntS5696;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS2054);
  _closure_5874
  = (struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2052*)moonbit_malloc(sizeof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2052));
  Moonbit_object_header(_closure_5874)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_5874->code
  = &_M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS2052;
  _closure_5874->$0 = _M0L5indexS2056;
  _closure_5874->$1 = _M0L8filenameS2054;
  _M0L13handle__startS2052 = (struct _M0TWEu*)_closure_5874;
  moonbit_incref_cycle_free(_M0L8filenameS2054);
  _closure_5875
  = (struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2057*)moonbit_malloc(sizeof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2057));
  Moonbit_object_header(_closure_5875)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_5875->code
  = &_M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS2057;
  _closure_5875->$0 = _M0L5indexS2056;
  _closure_5875->$1 = _M0L8filenameS2054;
  _M0L14handle__resultS2057 = (struct _M0TWssbEu*)_closure_5875;
  _M0L17error__to__stringS2064
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS2064$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _tmp_5877
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS2085, _M0L8filenameS2054, _M0L5indexS2056, _M0L13handle__startS2052, _M0L14handle__resultS2057, _M0L17error__to__stringS2064);
  if (_tmp_5877.tag) {
    int32_t const _M0L5_2aokS5660 = _tmp_5877.data.ok;
    _handle__error__result_5878 = _M0L5_2aokS5660;
  } else {
    void* const _M0L6_2aerrS5661 = _tmp_5877.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS2064);
    moonbit_decref_cycle_free(_M0L13handle__startS2052);
    _M0L11_2atry__errS2079 = _M0L6_2aerrS5661;
    goto join_2078;
  }
  if (_handle__error__result_5878) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS2064);
    moonbit_decref_cycle_free(_M0L13handle__startS2052);
    _M0L6_2atmpS5651 = 1;
  } else {
    struct moonbit_result_0 _tmp_5879;
    int32_t _handle__error__result_5880;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
    _tmp_5879
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS2085, _M0L8filenameS2054, _M0L5indexS2056, _M0L13handle__startS2052, _M0L14handle__resultS2057, _M0L17error__to__stringS2064);
    if (_tmp_5879.tag) {
      int32_t const _M0L5_2aokS5658 = _tmp_5879.data.ok;
      _handle__error__result_5880 = _M0L5_2aokS5658;
    } else {
      void* const _M0L6_2aerrS5659 = _tmp_5879.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS2064);
      moonbit_decref_cycle_free(_M0L13handle__startS2052);
      _M0L11_2atry__errS2079 = _M0L6_2aerrS5659;
      goto join_2078;
    }
    if (_handle__error__result_5880) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS2064);
      moonbit_decref_cycle_free(_M0L13handle__startS2052);
      _M0L6_2atmpS5651 = 1;
    } else {
      struct moonbit_result_0 _tmp_5881;
      int32_t _handle__error__result_5882;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
      _tmp_5881
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS2085, _M0L8filenameS2054, _M0L5indexS2056, _M0L13handle__startS2052, _M0L14handle__resultS2057, _M0L17error__to__stringS2064);
      if (_tmp_5881.tag) {
        int32_t const _M0L5_2aokS5656 = _tmp_5881.data.ok;
        _handle__error__result_5882 = _M0L5_2aokS5656;
      } else {
        void* const _M0L6_2aerrS5657 = _tmp_5881.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS2064);
        moonbit_decref_cycle_free(_M0L13handle__startS2052);
        _M0L11_2atry__errS2079 = _M0L6_2aerrS5657;
        goto join_2078;
      }
      if (_handle__error__result_5882) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS2064);
        moonbit_decref_cycle_free(_M0L13handle__startS2052);
        _M0L6_2atmpS5651 = 1;
      } else {
        struct moonbit_result_0 _tmp_5883;
        int32_t _handle__error__result_5884;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
        _tmp_5883
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS2085, _M0L8filenameS2054, _M0L5indexS2056, _M0L13handle__startS2052, _M0L14handle__resultS2057, _M0L17error__to__stringS2064);
        if (_tmp_5883.tag) {
          int32_t const _M0L5_2aokS5654 = _tmp_5883.data.ok;
          _handle__error__result_5884 = _M0L5_2aokS5654;
        } else {
          void* const _M0L6_2aerrS5655 = _tmp_5883.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS2064);
          moonbit_decref_cycle_free(_M0L13handle__startS2052);
          _M0L11_2atry__errS2079 = _M0L6_2aerrS5655;
          goto join_2078;
        }
        if (_handle__error__result_5884) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS2064);
          moonbit_decref_cycle_free(_M0L13handle__startS2052);
          _M0L6_2atmpS5651 = 1;
        } else {
          struct moonbit_result_0 _tmp_5885;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
          _tmp_5885
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS2085, _M0L8filenameS2054, _M0L5indexS2056, _M0L13handle__startS2052, _M0L14handle__resultS2057, _M0L17error__to__stringS2064);
          moonbit_decref_cycle_free(_M0L13handle__startS2052);
          moonbit_decref_cycle_free(_M0L17error__to__stringS2064);
          if (_tmp_5885.tag) {
            int32_t const _M0L5_2aokS5652 = _tmp_5885.data.ok;
            _M0L6_2atmpS5651 = _M0L5_2aokS5652;
          } else {
            void* const _M0L6_2aerrS5653 = _tmp_5885.data.err;
            _M0L11_2atry__errS2079 = _M0L6_2aerrS5653;
            goto join_2078;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS5651) {
    void* _M0L130RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5662 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L130RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5662)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L130RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5662)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS2079
    = _M0L130RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5662;
    goto join_2078;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS2057);
  }
  goto joinlet_5876;
  join_2078:;
  _M0L3errS2080 = _M0L11_2atry__errS2079;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS2083
  = (struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS2080;
  _M0L7_2anameS2084 = _M0L36_2aMoonBitTestDriverInternalSkipTestS2083->$0;
  _M0L6_2acntS5696
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS2083));
  if (_M0L6_2acntS5696 > 1) {
    int32_t _M0L11_2anew__cntS5697 = _M0L6_2acntS5696 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS2083), _M0L11_2anew__cntS5697);
    moonbit_incref_cycle_free(_M0L7_2anameS2084);
  } else if (_M0L6_2acntS5696 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS2083);
  }
  _M0L4nameS2082 = _M0L7_2anameS2084;
  goto join_2081;
  goto joinlet_5886;
  join_2081:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS2057(_M0L14handle__resultS2057, _M0L4nameS2082, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS2057);
  moonbit_decref_cycle_free(_M0L4nameS2082);
  joinlet_5886:;
  joinlet_5876:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS2064(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS5650,
  void* _M0L3errS2065
) {
  void* _M0L1eS2067;
  moonbit_string_t _M0L1eS2069;
  moonbit_string_t _result_5889;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS2065)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS2070 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS2065;
      moonbit_string_t _M0L4_2aeS2071 = _M0L10_2aFailureS2070->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS2071);
      _M0L1eS2069 = _M0L4_2aeS2071;
      goto join_2068;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS2072 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS2065;
      moonbit_string_t _M0L4_2aeS2073 = _M0L15_2aInspectErrorS2072->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS2073);
      _M0L1eS2069 = _M0L4_2aeS2073;
      goto join_2068;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS2074 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS2065;
      moonbit_string_t _M0L4_2aeS2075 = _M0L16_2aSnapshotErrorS2074->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS2075);
      _M0L1eS2069 = _M0L4_2aeS2075;
      goto join_2068;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS2076 =
        (struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS2065;
      moonbit_string_t _M0L4_2aeS2077 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS2076->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS2077);
      _M0L1eS2069 = _M0L4_2aeS2077;
      goto join_2068;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS2065);
      _M0L1eS2067 = _M0L3errS2065;
      goto join_2066;
      break;
    }
  }
  join_2068:;
  return _M0L1eS2069;
  join_2066:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _result_5889 = _M0FP15Error10to__string(_M0L1eS2067);
  moonbit_decref_cycle_free(_M0L1eS2067);
  return _result_5889;
}

int32_t _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS2057(
  struct _M0TWssbEu* _M0L6_2aenvS5647,
  moonbit_string_t _M0L10__testnameS2058,
  moonbit_string_t _M0L7messageS2059,
  int32_t _M0L7skippedS2060
) {
  struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2057* _M0L14_2acasted__envS5648;
  moonbit_string_t _M0L8filenameS2054;
  int32_t _M0L5indexS2056;
  moonbit_string_t _M0L10file__nameS2061;
  moonbit_string_t _M0L7messageS2062;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS2063;
  moonbit_string_t _M0L6_2atmpS5649;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS5648
  = (struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2057*)_M0L6_2aenvS5647;
  _M0L8filenameS2054 = _M0L14_2acasted__envS5648->$1;
  _M0L5indexS2056 = _M0L14_2acasted__envS5648->$0;
  if (!_M0L7skippedS2060 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS2061
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS2054, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS2062
  = _M0MPC16string6String14escape_2einner(_M0L7messageS2059, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS2063
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2063, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS2063, _M0L10file__nameS2061);
  moonbit_decref_cycle_free(_M0L10file__nameS2061);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2063, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS2063, _M0L5indexS2056);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2063, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS2063, _M0L7messageS2062);
  moonbit_decref_cycle_free(_M0L7messageS2062);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2063, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5649
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS2063);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS2063);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS5649);
  moonbit_decref_cycle_free(_M0L6_2atmpS5649);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS2052(
  struct _M0TWEu* _M0L6_2aenvS5644
) {
  struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2052* _M0L14_2acasted__envS5645;
  moonbit_string_t _M0L8filenameS2054;
  int32_t _M0L5indexS2056;
  moonbit_string_t _M0L10file__nameS2053;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS2055;
  moonbit_string_t _M0L6_2atmpS5646;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS5645
  = (struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__if__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c2052*)_M0L6_2aenvS5644;
  _M0L8filenameS2054 = _M0L14_2acasted__envS5645->$1;
  _M0L5indexS2056 = _M0L14_2acasted__envS5645->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS2053
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS2054, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS2055
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2055, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS2055, _M0L10file__nameS2053);
  moonbit_decref_cycle_free(_M0L10file__nameS2053);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2055, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS2055, _M0L5indexS2056);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2055, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5646
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS2055);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS2055);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS5646);
  moonbit_decref_cycle_free(_M0L6_2atmpS5646);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S2022;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS2029;
  struct _M0TUsiE** _M0L6_2atmpS5643;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS2036;
  moonbit_string_t* _M0L9cli__argsS2037;
  moonbit_string_t _M0L6_2atmpS5642;
  moonbit_string_t _M0L6_2atmpS5641;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS2038;
  int32_t _M0L7_2abindS2039;
  moonbit_string_t* _M0L7_2abindS2040;
  int32_t _M0L6_2acntS5698;
  int32_t _M0L2__S2041;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S2022 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS2029 = 0;
  _M0L6_2atmpS5643 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS2036
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS2036)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS2036->$0 = _M0L6_2atmpS5643;
  _M0L16file__and__indexS2036->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS2037
  = _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS2037)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS5642 = (moonbit_string_t)_M0L9cli__argsS2037[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS5642);
  moonbit_decref_cycle_free(_M0L9cli__argsS2037);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5641
  = _M0MP46RiantR8snn__mbt8examples27poisson__if__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS5642);
  moonbit_decref_cycle_free(_M0L6_2atmpS5642);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS2038
  = _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS2029(_M0L51moonbit__test__driver__internal__split__mbt__stringS2029, _M0L6_2atmpS5641, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS5641);
  _M0L7_2abindS2039 = _M0L10test__argsS2038->$1;
  _M0L7_2abindS2040 = _M0L10test__argsS2038->$0;
  _M0L6_2acntS5698
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS2038));
  if (_M0L6_2acntS5698 > 1) {
    int32_t _M0L11_2anew__cntS5699 = _M0L6_2acntS5698 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS2038), _M0L11_2anew__cntS5699);
    moonbit_incref_cycle_free(_M0L7_2abindS2040);
  } else if (_M0L6_2acntS5698 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS2038);
  }
  _M0L2__S2041 = 0;
  while (1) {
    if (_M0L2__S2041 < _M0L7_2abindS2039) {
      moonbit_string_t _M0L3argS2042 =
        (moonbit_string_t)_M0L7_2abindS2040[_M0L2__S2041];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS2043;
      moonbit_string_t _M0L4fileS2044;
      moonbit_string_t _M0L5rangeS2045;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS2046;
      moonbit_string_t _M0L6_2atmpS5639;
      int32_t _M0L5startS2047;
      moonbit_string_t _M0L6_2atmpS5638;
      int32_t _M0L3endS2048;
      int32_t _M0L1iS2049;
      int32_t _M0L6_2atmpS5640;
      moonbit_incref_cycle_free(_M0L3argS2042);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS2043
      = _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS2029(_M0L51moonbit__test__driver__internal__split__mbt__stringS2029, _M0L3argS2042, 58);
      moonbit_decref_cycle_free(_M0L3argS2042);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS2044
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS2043, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS2045
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS2043, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS2043);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS2046
      = _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS2029(_M0L51moonbit__test__driver__internal__split__mbt__stringS2029, _M0L5rangeS2045, 45);
      moonbit_decref_cycle_free(_M0L5rangeS2045);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS5639
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS2046, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS2047
      = _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S2022(_M0L45moonbit__test__driver__internal__parse__int__S2022, _M0L6_2atmpS5639);
      moonbit_decref_cycle_free(_M0L6_2atmpS5639);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS5638
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS2046, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS2046);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS2048
      = _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S2022(_M0L45moonbit__test__driver__internal__parse__int__S2022, _M0L6_2atmpS5638);
      moonbit_decref_cycle_free(_M0L6_2atmpS5638);
      _M0L1iS2049 = _M0L5startS2047;
      while (1) {
        if (_M0L1iS2049 < _M0L3endS2048) {
          struct _M0TUsiE* _M0L8_2atupleS5636;
          int32_t _M0L6_2atmpS5637;
          moonbit_incref_cycle_free(_M0L4fileS2044);
          _M0L8_2atupleS5636
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS5636)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS5636->$0 = _M0L4fileS2044;
          _M0L8_2atupleS5636->$1 = _M0L1iS2049;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS2036, _M0L8_2atupleS5636);
          _M0L6_2atmpS5637 = _M0L1iS2049 + 1;
          _M0L1iS2049 = _M0L6_2atmpS5637;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS2044);
        }
        break;
      }
      _M0L6_2atmpS5640 = _M0L2__S2041 + 1;
      _M0L2__S2041 = _M0L6_2atmpS5640;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS2040);
    }
    break;
  }
  return _M0L16file__and__indexS2036;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS2029(
  int32_t _M0L6_2aenvS5617,
  moonbit_string_t _M0L1sS2030,
  int32_t _M0L3sepS2031
) {
  moonbit_string_t* _M0L6_2atmpS5635;
  struct _M0TPB5ArrayGsE* _M0L3resS2032;
  struct _M0TPB8MutLocalGiE* _M0L1iS2033;
  struct _M0TPB8MutLocalGiE* _M0L5startS2034;
  int32_t _M0L3valS5630;
  int32_t _M0L6_2atmpS5631;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5635 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS2032
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS2032)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS2032->$0 = _M0L6_2atmpS5635;
  _M0L3resS2032->$1 = 0;
  _M0L1iS2033
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS2033)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS2033->$0 = 0;
  _M0L5startS2034
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS2034)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS2034->$0 = 0;
  while (1) {
    int32_t _M0L3valS5618 = _M0L1iS2033->$0;
    int32_t _M0L6_2atmpS5619 = Moonbit_array_length(_M0L1sS2030);
    if (_M0L3valS5618 < _M0L6_2atmpS5619) {
      int32_t _M0L3valS5622 = _M0L1iS2033->$0;
      int32_t _M0L6_2atmpS5621;
      int32_t _M0L6_2atmpS5620;
      int32_t _M0L3valS5629;
      int32_t _M0L6_2atmpS5628;
      if (
        _M0L3valS5622 < 0
        || _M0L3valS5622 >= Moonbit_array_length(_M0L1sS2030)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS5621 = _M0L1sS2030[_M0L3valS5622];
      _M0L6_2atmpS5620 = _M0L6_2atmpS5621;
      if (_M0L6_2atmpS5620 == _M0L3sepS2031) {
        int32_t _M0L3valS5624 = _M0L5startS2034->$0;
        int32_t _M0L3valS5625 = _M0L1iS2033->$0;
        moonbit_string_t _M0L6_2atmpS5623;
        int32_t _M0L3valS5627;
        int32_t _M0L6_2atmpS5626;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS5623
        = _M0MPC16string6String17unsafe__substring(_M0L1sS2030, _M0L3valS5624, _M0L3valS5625);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS2032, _M0L6_2atmpS5623);
        _M0L3valS5627 = _M0L1iS2033->$0;
        _M0L6_2atmpS5626 = _M0L3valS5627 + 1;
        _M0L5startS2034->$0 = _M0L6_2atmpS5626;
      }
      _M0L3valS5629 = _M0L1iS2033->$0;
      _M0L6_2atmpS5628 = _M0L3valS5629 + 1;
      _M0L1iS2033->$0 = _M0L6_2atmpS5628;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS2033);
    }
    break;
  }
  _M0L3valS5630 = _M0L5startS2034->$0;
  _M0L6_2atmpS5631 = Moonbit_array_length(_M0L1sS2030);
  if (_M0L3valS5630 < _M0L6_2atmpS5631) {
    int32_t _M0L3valS5633 = _M0L5startS2034->$0;
    int32_t _M0L6_2atmpS5634;
    moonbit_string_t _M0L6_2atmpS5632;
    moonbit_decref_cycle_free(_M0L5startS2034);
    _M0L6_2atmpS5634 = Moonbit_array_length(_M0L1sS2030);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS5632
    = _M0MPC16string6String17unsafe__substring(_M0L1sS2030, _M0L3valS5633, _M0L6_2atmpS5634);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS2032, _M0L6_2atmpS5632);
  } else {
    moonbit_decref_cycle_free(_M0L5startS2034);
  }
  return _M0L3resS2032;
}

int32_t _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S2022(
  int32_t _M0L6_2aenvS5610,
  moonbit_string_t _M0L1sS2023
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS2024;
  int32_t _M0L3lenS2025;
  int32_t _M0L7_2abindS2026;
  int32_t _M0L1iS2027;
  int32_t _result_5894;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS2024
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS2024)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS2024->$0 = 0;
  _M0L3lenS2025 = Moonbit_array_length(_M0L1sS2023);
  _M0L7_2abindS2026 = 0;
  _M0L1iS2027 = _M0L7_2abindS2026;
  while (1) {
    if (_M0L1iS2027 < _M0L3lenS2025) {
      int32_t _M0L3valS5615 = _M0L3resS2024->$0;
      int32_t _M0L6_2atmpS5612 = _M0L3valS5615 * 10;
      int32_t _M0L6_2atmpS5614;
      int32_t _M0L6_2atmpS5613;
      int32_t _M0L6_2atmpS5611;
      int32_t _M0L6_2atmpS5616;
      if (
        _M0L1iS2027 < 0 || _M0L1iS2027 >= Moonbit_array_length(_M0L1sS2023)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS5614 = _M0L1sS2023[_M0L1iS2027];
      _M0L6_2atmpS5613 = _M0L6_2atmpS5614 - 48;
      _M0L6_2atmpS5611 = _M0L6_2atmpS5612 + _M0L6_2atmpS5613;
      _M0L3resS2024->$0 = _M0L6_2atmpS5611;
      _M0L6_2atmpS5616 = _M0L1iS2027 + 1;
      _M0L1iS2027 = _M0L6_2atmpS5616;
      continue;
    }
    break;
  }
  _result_5894 = _M0L3resS2024->$0;
  moonbit_decref_cycle_free(_M0L3resS2024);
  return _result_5894;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples27poisson__if__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS2021
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS2021);
  return _M0L4selfS2021;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1991,
  moonbit_string_t _M0L12_2adiscard__S1992,
  int32_t _M0L12_2adiscard__S1993,
  struct _M0TWEu* _M0L12_2adiscard__S1994,
  struct _M0TWssbEu* _M0L12_2adiscard__S1995,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1996
) {
  struct moonbit_result_0 _result_5895;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _result_5895.tag = 1;
  _result_5895.data.ok = 0;
  return _result_5895;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1997,
  moonbit_string_t _M0L12_2adiscard__S1998,
  int32_t _M0L12_2adiscard__S1999,
  struct _M0TWEu* _M0L12_2adiscard__S2000,
  struct _M0TWssbEu* _M0L12_2adiscard__S2001,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S2002
) {
  struct moonbit_result_0 _result_5896;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _result_5896.tag = 1;
  _result_5896.data.ok = 0;
  return _result_5896;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S2003,
  moonbit_string_t _M0L12_2adiscard__S2004,
  int32_t _M0L12_2adiscard__S2005,
  struct _M0TWEu* _M0L12_2adiscard__S2006,
  struct _M0TWssbEu* _M0L12_2adiscard__S2007,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S2008
) {
  struct moonbit_result_0 _result_5897;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _result_5897.tag = 1;
  _result_5897.data.ok = 0;
  return _result_5897;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S2009,
  moonbit_string_t _M0L12_2adiscard__S2010,
  int32_t _M0L12_2adiscard__S2011,
  struct _M0TWEu* _M0L12_2adiscard__S2012,
  struct _M0TWssbEu* _M0L12_2adiscard__S2013,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S2014
) {
  struct moonbit_result_0 _result_5898;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _result_5898.tag = 1;
  _result_5898.data.ok = 0;
  return _result_5898;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S2015,
  moonbit_string_t _M0L12_2adiscard__S2016,
  int32_t _M0L12_2adiscard__S2017,
  struct _M0TWEu* _M0L12_2adiscard__S2018,
  struct _M0TWssbEu* _M0L12_2adiscard__S2019,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S2020
) {
  struct moonbit_result_0 _result_5899;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _result_5899.tag = 1;
  _result_5899.data.ok = 0;
  return _result_5899;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1990
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23heterogeneous__sim__for(
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0L1mS1971,
  float _M0L8durationS1968
) {
  float _M0L2dtS1966;
  float _M0L6_2atmpS5609;
  int32_t _M0L5stepsS1967;
  int32_t _M0L7_2abindS1969;
  int32_t _M0L2__S1970;
  #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L2dtS1966 = 0x1p-3f;
  _M0L6_2atmpS5609 = _M0L8durationS1968 / _M0L2dtS1966;
  #line 273 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L5stepsS1967 = _M0MPC15float5Float7to__int(_M0L6_2atmpS5609);
  _M0L7_2abindS1969 = 0;
  _M0L2__S1970 = _M0L7_2abindS1969;
  while (1) {
    if (_M0L2__S1970 < _M0L5stepsS1967) {
      int32_t _M0L6_2atmpS5608;
      #line 275 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt19step__heterogeneous(_M0L1mS1971, _M0L2dtS1966);
      _M0L6_2atmpS5608 = _M0L2__S1970 + 1;
      _M0L2__S1970 = _M0L6_2atmpS5608;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt19step__heterogeneous(
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0L1mS1868,
  float _M0L2dtS1873
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L7_2abindS1867;
  int32_t _M0L7_2abindS1869;
  void** _M0L7_2abindS1870;
  int32_t _M0L2__S1871;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS1875;
  int32_t _M0L7_2abindS1876;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS1877;
  int32_t _M0L2__S1878;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L7_2abindS1881;
  int32_t _M0L7_2abindS1882;
  void** _M0L7_2abindS1883;
  int32_t _M0L2__S1884;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS1902;
  int32_t _M0L7_2abindS1903;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS1904;
  int32_t _M0L2__S1905;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L7_2abindS1908;
  int32_t _M0L7_2abindS1909;
  void** _M0L7_2abindS1910;
  int32_t _M0L2__S1911;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L7_2abindS1954;
  int32_t _M0L7_2abindS1955;
  void** _M0L7_2abindS1956;
  int32_t _M0L2__S1957;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS1960;
  int32_t _M0L7_2abindS1961;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS1962;
  int32_t _M0L2__S1963;
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5607;
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L7_2abindS1867 = _M0L1mS1868->$2;
  _M0L7_2abindS1869 = _M0L7_2abindS1867->$1;
  _M0L7_2abindS1870 = _M0L7_2abindS1867->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1870);
  _M0L2__S1871 = 0;
  while (1) {
    if (_M0L2__S1871 < _M0L7_2abindS1869) {
      void* _M0L1sS1872 = (void*)_M0L7_2abindS1870[_M0L2__S1871];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5416 = _M0L1mS1868->$3;
      int32_t _M0L6_2atmpS5417;
      moonbit_incref_cycle_free(_M0L1sS1872);
      #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt14stimulate__any(_M0L1sS1872, _M0L4timeS5416, _M0L2dtS1873);
      moonbit_decref_cycle_free(_M0L1sS1872);
      _M0L6_2atmpS5417 = _M0L2__S1871 + 1;
      _M0L2__S1871 = _M0L6_2atmpS5417;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1870);
    }
    break;
  }
  _M0L7_2abindS1875 = _M0L1mS1868->$1;
  _M0L7_2abindS1876 = _M0L7_2abindS1875->$1;
  _M0L7_2abindS1877 = _M0L7_2abindS1875->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1877);
  _M0L2__S1878 = 0;
  while (1) {
    if (_M0L2__S1878 < _M0L7_2abindS1876) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1879 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS1877[
          _M0L2__S1878
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5419 = _M0L1mS1868->$3;
      float _M0L6_2atmpS5418;
      int32_t _M0L6_2atmpS5420;
      moonbit_incref_cycle_free(_M0L1cS1879);
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5418 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5419);
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt25deliver__pending__synapse(_M0L1cS1879, _M0L6_2atmpS5418);
      moonbit_decref_cycle_free(_M0L1cS1879);
      _M0L6_2atmpS5420 = _M0L2__S1878 + 1;
      _M0L2__S1878 = _M0L6_2atmpS5420;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1877);
    }
    break;
  }
  _M0L7_2abindS1881 = _M0L1mS1868->$6;
  _M0L7_2abindS1882 = _M0L7_2abindS1881->$1;
  _M0L7_2abindS1883 = _M0L7_2abindS1881->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1883);
  _M0L2__S1884 = 0;
  while (1) {
    if (_M0L2__S1884 < _M0L7_2abindS1882) {
      void* _M0L5entryS1885 = (void*)_M0L7_2abindS1883[_M0L2__S1884];
      struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _M0L1eS1887;
      struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet* _M0L1eS1890;
      struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry* _M0L1eS1893;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5437;
      int32_t _M0L11conn__indexS5438;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1894;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS5433;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter* _M0L5paramS5434;
      int32_t _M0L6_2acntS5704;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5436;
      float _M0L6_2atmpS5435;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5431;
      int32_t _M0L11conn__indexS5432;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1891;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS5427;
      struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet* _M0L5paramS5428;
      int32_t _M0L6_2acntS5702;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5430;
      float _M0L6_2atmpS5429;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5425;
      int32_t _M0L11conn__indexS5426;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1888;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS5421;
      struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0L5paramS5422;
      int32_t _M0L6_2acntS5700;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5424;
      float _M0L6_2atmpS5423;
      int32_t _M0L6_2atmpS5439;
      switch (Moonbit_object_tag(_M0L5entryS1885)) {
        case 0: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__* _M0L15_2aMarkramSTP__S1895 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__*)_M0L5entryS1885;
          struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry* _M0L4_2aeS1896 =
            _M0L15_2aMarkramSTP__S1895->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1896);
          _M0L1eS1893 = _M0L4_2aeS1896;
          goto join_1892;
          break;
        }
        
        case 1: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__* _M0L18_2aMarkramSTPHet__S1897 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__*)_M0L5entryS1885;
          struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet* _M0L4_2aeS1898 =
            _M0L18_2aMarkramSTPHet__S1897->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1898);
          _M0L1eS1890 = _M0L4_2aeS1898;
          goto join_1889;
          break;
        }
        default: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind20MarkramSTPTimestep__* _M0L23_2aMarkramSTPTimestep__S1899 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind20MarkramSTPTimestep__*)_M0L5entryS1885;
          struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _M0L4_2aeS1900 =
            _M0L23_2aMarkramSTPTimestep__S1899->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1900);
          _M0L1eS1887 = _M0L4_2aeS1900;
          goto join_1886;
          break;
        }
      }
      goto joinlet_5906;
      join_1892:;
      _M0L5connsS5437 = _M0L1mS1868->$1;
      _M0L11conn__indexS5438 = _M0L1eS1893->$0;
      #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1894
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5437, _M0L11conn__indexS5438);
      _M0L4varsS5433 = _M0L1eS1893->$1;
      _M0L5paramS5434 = _M0L1eS1893->$2;
      _M0L6_2acntS5704 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1893));
      if (_M0L6_2acntS5704 > 1) {
        int32_t _M0L11_2anew__cntS5705 = _M0L6_2acntS5704 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1893), _M0L11_2anew__cntS5705);
        moonbit_incref_cycle_free(_M0L5paramS5434);
        moonbit_incref_cycle_free(_M0L4varsS5433);
      } else if (_M0L6_2acntS5704 == 1) {
        #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1893);
      }
      _M0L4timeS5436 = _M0L1mS1868->$3;
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5435 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5436);
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt18markram__stp__step(_M0L3synS1894, _M0L4varsS5433, _M0L5paramS5434, _M0L6_2atmpS5435);
      moonbit_decref_cycle_free(_M0L3synS1894);
      moonbit_decref_cycle_free(_M0L4varsS5433);
      moonbit_decref_cycle_free(_M0L5paramS5434);
      joinlet_5906:;
      goto joinlet_5905;
      join_1889:;
      _M0L5connsS5431 = _M0L1mS1868->$1;
      _M0L11conn__indexS5432 = _M0L1eS1890->$0;
      #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1891
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5431, _M0L11conn__indexS5432);
      _M0L4varsS5427 = _M0L1eS1890->$1;
      _M0L5paramS5428 = _M0L1eS1890->$2;
      _M0L6_2acntS5702 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1890));
      if (_M0L6_2acntS5702 > 1) {
        int32_t _M0L11_2anew__cntS5703 = _M0L6_2acntS5702 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1890), _M0L11_2anew__cntS5703);
        moonbit_incref_cycle_free(_M0L5paramS5428);
        moonbit_incref_cycle_free(_M0L4varsS5427);
      } else if (_M0L6_2acntS5702 == 1) {
        #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1890);
      }
      _M0L4timeS5430 = _M0L1mS1868->$3;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5429 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5430);
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt23markram__stp__step__het(_M0L3synS1891, _M0L4varsS5427, _M0L5paramS5428, _M0L6_2atmpS5429);
      moonbit_decref_cycle_free(_M0L3synS1891);
      moonbit_decref_cycle_free(_M0L4varsS5427);
      moonbit_decref_cycle_free(_M0L5paramS5428);
      joinlet_5905:;
      goto joinlet_5904;
      join_1886:;
      _M0L5connsS5425 = _M0L1mS1868->$1;
      _M0L11conn__indexS5426 = _M0L1eS1887->$0;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1888
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5425, _M0L11conn__indexS5426);
      _M0L4varsS5421 = _M0L1eS1887->$1;
      _M0L5paramS5422 = _M0L1eS1887->$2;
      _M0L6_2acntS5700 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1887));
      if (_M0L6_2acntS5700 > 1) {
        int32_t _M0L11_2anew__cntS5701 = _M0L6_2acntS5700 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1887), _M0L11_2anew__cntS5701);
        moonbit_incref_cycle_free(_M0L5paramS5422);
        moonbit_incref_cycle_free(_M0L4varsS5421);
      } else if (_M0L6_2acntS5700 == 1) {
        #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1887);
      }
      _M0L4timeS5424 = _M0L1mS1868->$3;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5423 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5424);
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt28markram__stp__step__timestep(_M0L3synS1888, _M0L4varsS5421, _M0L5paramS5422, _M0L6_2atmpS5423, _M0L2dtS1873);
      moonbit_decref_cycle_free(_M0L3synS1888);
      moonbit_decref_cycle_free(_M0L4varsS5421);
      moonbit_decref_cycle_free(_M0L5paramS5422);
      joinlet_5904:;
      _M0L6_2atmpS5439 = _M0L2__S1884 + 1;
      _M0L2__S1884 = _M0L6_2atmpS5439;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1883);
    }
    break;
  }
  _M0L7_2abindS1902 = _M0L1mS1868->$1;
  _M0L7_2abindS1903 = _M0L7_2abindS1902->$1;
  _M0L7_2abindS1904 = _M0L7_2abindS1902->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1904);
  _M0L2__S1905 = 0;
  while (1) {
    if (_M0L2__S1905 < _M0L7_2abindS1903) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1906 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS1904[
          _M0L2__S1905
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5441 = _M0L1mS1868->$3;
      float _M0L6_2atmpS5440;
      int32_t _M0L6_2atmpS5442;
      moonbit_incref_cycle_free(_M0L1cS1906);
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5440 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5441);
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt16forward__synapse(_M0L1cS1906, _M0L6_2atmpS5440);
      moonbit_decref_cycle_free(_M0L1cS1906);
      _M0L6_2atmpS5442 = _M0L2__S1905 + 1;
      _M0L2__S1905 = _M0L6_2atmpS5442;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1904);
    }
    break;
  }
  _M0L7_2abindS1908 = _M0L1mS1868->$5;
  _M0L7_2abindS1909 = _M0L7_2abindS1908->$1;
  _M0L7_2abindS1910 = _M0L7_2abindS1908->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1910);
  _M0L2__S1911 = 0;
  while (1) {
    if (_M0L2__S1911 < _M0L7_2abindS1909) {
      void* _M0L5entryS1912 = (void*)_M0L7_2abindS1910[_M0L2__S1911];
      struct _M0TP26RiantR8snn__mbt17CaPlasticityEntry* _M0L1eS1914;
      struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric* _M0L1eS1917;
      struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0L1eS1920;
      struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0L1eS1923;
      struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025* _M0L1eS1926;
      struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric* _M0L1eS1929;
      struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat* _M0L1eS1932;
      struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0L1eS1935;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5600;
      int32_t _M0L11conn__indexS5601;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1936;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5595;
      struct _M0TPB5ArrayGfE* _M0L4valsS5582;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5594;
      struct _M0TPB5ArrayGbE* _M0L4fireS5583;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5593;
      struct _M0TPB5ArrayGbE* _M0L4fireS5584;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5592;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5585;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5591;
      int32_t _M0L6_2acntS5853;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5586;
      int32_t _M0L6_2acntS5864;
      struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS5587;
      struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS5588;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5590;
      float _M0L6_2atmpS5589;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5596;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5599;
      int32_t _M0L6_2acntS5868;
      float _M0L6_2atmpS5598;
      float _M0L6_2atmpS5597;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5580;
      int32_t _M0L11conn__indexS5581;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1933;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5575;
      struct _M0TPB5ArrayGfE* _M0L4valsS5563;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5574;
      struct _M0TPB5ArrayGbE* _M0L4fireS5564;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5573;
      struct _M0TPB5ArrayGbE* _M0L4fireS5565;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5572;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5566;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5571;
      int32_t _M0L6_2acntS5833;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5567;
      int32_t _M0L6_2acntS5844;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5568;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5569;
      struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L5paramS5570;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5576;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5579;
      int32_t _M0L6_2acntS5848;
      float _M0L6_2atmpS5578;
      float _M0L6_2atmpS5577;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5561;
      int32_t _M0L11conn__indexS5562;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1930;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5556;
      struct _M0TPB5ArrayGfE* _M0L4valsS5545;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5555;
      struct _M0TPB5ArrayGbE* _M0L4fireS5546;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5554;
      struct _M0TPB5ArrayGbE* _M0L4fireS5547;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5553;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5548;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5552;
      int32_t _M0L6_2acntS5814;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5549;
      int32_t _M0L6_2acntS5825;
      struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L4varsS5550;
      struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L5paramS5551;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5557;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5560;
      int32_t _M0L6_2acntS5829;
      float _M0L6_2atmpS5559;
      float _M0L6_2atmpS5558;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5543;
      int32_t _M0L11conn__indexS5544;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1927;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5538;
      struct _M0TPB5ArrayGfE* _M0L4valsS5525;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5537;
      struct _M0TPB5ArrayGbE* _M0L4fireS5526;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5536;
      struct _M0TPB5ArrayGbE* _M0L4fireS5527;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5535;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5528;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5534;
      int32_t _M0L6_2acntS5795;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5529;
      int32_t _M0L6_2acntS5806;
      struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS5530;
      struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L5paramS5531;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5533;
      float _M0L6_2atmpS5532;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5539;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5542;
      int32_t _M0L6_2acntS5810;
      float _M0L6_2atmpS5541;
      float _M0L6_2atmpS5540;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5523;
      int32_t _M0L11conn__indexS5524;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1924;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5518;
      struct _M0TPB5ArrayGfE* _M0L4valsS5505;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5517;
      struct _M0TPB5ArrayGbE* _M0L4fireS5506;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5516;
      struct _M0TPB5ArrayGbE* _M0L4fireS5507;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5515;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5508;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5514;
      int32_t _M0L6_2acntS5776;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5509;
      int32_t _M0L6_2acntS5787;
      struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L4varsS5510;
      struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS5511;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5513;
      float _M0L6_2atmpS5512;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5519;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5522;
      int32_t _M0L6_2acntS5791;
      float _M0L6_2atmpS5521;
      float _M0L6_2atmpS5520;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5503;
      int32_t _M0L11conn__indexS5504;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1921;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5498;
      struct _M0TPB5ArrayGfE* _M0L4valsS5483;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5497;
      struct _M0TPB5ArrayGbE* _M0L4fireS5484;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5496;
      struct _M0TPB5ArrayGbE* _M0L4fireS5485;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5495;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5486;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5494;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5487;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5493;
      int32_t _M0L6_2acntS5744;
      struct _M0TPB5ArrayGfE* _M0L1vS5488;
      int32_t _M0L6_2acntS5755;
      struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L4varsS5489;
      struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS5490;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5492;
      float _M0L6_2atmpS5491;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5499;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5502;
      int32_t _M0L6_2acntS5772;
      float _M0L6_2atmpS5501;
      float _M0L6_2atmpS5500;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5481;
      int32_t _M0L11conn__indexS5482;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1918;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5476;
      struct _M0TPB5ArrayGfE* _M0L4valsS5463;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5475;
      struct _M0TPB5ArrayGbE* _M0L4fireS5464;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5474;
      struct _M0TPB5ArrayGbE* _M0L4fireS5465;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5473;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5466;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5472;
      int32_t _M0L6_2acntS5725;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5467;
      int32_t _M0L6_2acntS5736;
      struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L4varsS5468;
      struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L5paramS5469;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5471;
      float _M0L6_2atmpS5470;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5477;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5480;
      int32_t _M0L6_2acntS5740;
      float _M0L6_2atmpS5479;
      float _M0L6_2atmpS5478;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5461;
      int32_t _M0L11conn__indexS5462;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1915;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5456;
      struct _M0TPB5ArrayGfE* _M0L4valsS5443;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5455;
      struct _M0TPB5ArrayGbE* _M0L4fireS5444;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5454;
      struct _M0TPB5ArrayGbE* _M0L4fireS5445;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5453;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5446;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5452;
      int32_t _M0L6_2acntS5706;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5447;
      int32_t _M0L6_2acntS5717;
      struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L4varsS5448;
      struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L5paramS5449;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5451;
      float _M0L6_2atmpS5450;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5457;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5460;
      int32_t _M0L6_2acntS5721;
      float _M0L6_2atmpS5459;
      float _M0L6_2atmpS5458;
      int32_t _M0L6_2atmpS5602;
      switch (Moonbit_object_tag(_M0L5entryS1912)) {
        case 0: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__* _M0L13_2aGerstner__S1937 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__*)_M0L5entryS1912;
          struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0L4_2aeS1938 =
            _M0L13_2aGerstner__S1937->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1938);
          _M0L1eS1935 = _M0L4_2aeS1938;
          goto join_1934;
          break;
        }
        
        case 1: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind12MexicanHat__* _M0L15_2aMexicanHat__S1939 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind12MexicanHat__*)_M0L5entryS1912;
          struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat* _M0L4_2aeS1940 =
            _M0L15_2aMexicanHat__S1939->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1940);
          _M0L1eS1932 = _M0L4_2aeS1940;
          goto join_1931;
          break;
        }
        
        case 2: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind15AntiSymmetric__* _M0L18_2aAntiSymmetric__S1941 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind15AntiSymmetric__*)_M0L5entryS1912;
          struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric* _M0L4_2aeS1942 =
            _M0L18_2aAntiSymmetric__S1941->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1942);
          _M0L1eS1929 = _M0L4_2aeS1942;
          goto join_1928;
          break;
        }
        
        case 3: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16Confavreux2025__* _M0L19_2aConfavreux2025__S1943 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16Confavreux2025__*)_M0L5entryS1912;
          struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025* _M0L4_2aeS1944 =
            _M0L19_2aConfavreux2025__S1943->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1944);
          _M0L1eS1926 = _M0L4_2aeS1944;
          goto join_1925;
          break;
        }
        
        case 4: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__* _M0L14_2aIstdpRate__S1945 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__*)_M0L5entryS1912;
          struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0L4_2aeS1946 =
            _M0L14_2aIstdpRate__S1945->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1946);
          _M0L1eS1923 = _M0L4_2aeS1946;
          goto join_1922;
          break;
        }
        
        case 5: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16IstdpPotential__* _M0L19_2aIstdpPotential__S1947 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16IstdpPotential__*)_M0L5entryS1912;
          struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0L4_2aeS1948 =
            _M0L19_2aIstdpPotential__S1947->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1948);
          _M0L1eS1920 = _M0L4_2aeS1948;
          goto join_1919;
          break;
        }
        
        case 6: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11Symmetric__* _M0L14_2aSymmetric__S1949 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11Symmetric__*)_M0L5entryS1912;
          struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric* _M0L4_2aeS1950 =
            _M0L14_2aSymmetric__S1949->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1950);
          _M0L1eS1917 = _M0L4_2aeS1950;
          goto join_1916;
          break;
        }
        default: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__* _M0L17_2aCaPlasticity__S1951 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__*)_M0L5entryS1912;
          struct _M0TP26RiantR8snn__mbt17CaPlasticityEntry* _M0L4_2aeS1952 =
            _M0L17_2aCaPlasticity__S1951->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1952);
          _M0L1eS1914 = _M0L4_2aeS1952;
          goto join_1913;
          break;
        }
      }
      goto joinlet_5916;
      join_1934:;
      _M0L5connsS5600 = _M0L1mS1868->$1;
      _M0L11conn__indexS5601 = _M0L1eS1935->$0;
      #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1936
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5600, _M0L11conn__indexS5601);
      _M0L6matrixS5595 = _M0L3synS1936->$4;
      _M0L4valsS5582 = _M0L6matrixS5595->$4;
      _M0L3preS5594 = _M0L3synS1936->$0;
      _M0L4fireS5583 = _M0L3preS5594->$5;
      _M0L4postS5593 = _M0L3synS1936->$1;
      _M0L4fireS5584 = _M0L4postS5593->$5;
      _M0L6matrixS5592 = _M0L3synS1936->$4;
      _M0L6colptrS5585 = _M0L6matrixS5592->$3;
      _M0L6matrixS5591 = _M0L3synS1936->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5585);
      moonbit_incref_cycle_free(_M0L4fireS5584);
      moonbit_incref_cycle_free(_M0L4fireS5583);
      moonbit_incref_cycle_free(_M0L4valsS5582);
      _M0L6_2acntS5853
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1936));
      if (_M0L6_2acntS5853 > 1) {
        int32_t _M0L11_2anew__cntS5863 = _M0L6_2acntS5853 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1936), _M0L11_2anew__cntS5863);
        moonbit_incref_cycle_free(_M0L6matrixS5591);
      } else if (_M0L6_2acntS5853 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5862 = _M0L3synS1936->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5861;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5860;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5859;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5858;
        moonbit_string_t _M0L8_2afieldS5857;
        moonbit_string_t _M0L8_2afieldS5856;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5855;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5854;
        moonbit_decref_cycle_free(_M0L8_2afieldS5862);
        _M0L8_2afieldS5861 = _M0L3synS1936->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5861);
        _M0L8_2afieldS5860 = _M0L3synS1936->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5860);
        _M0L8_2afieldS5859 = _M0L3synS1936->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5859);
        _M0L8_2afieldS5858 = _M0L3synS1936->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5858);
        _M0L8_2afieldS5857 = _M0L3synS1936->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5857);
        _M0L8_2afieldS5856 = _M0L3synS1936->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5856);
        _M0L8_2afieldS5855 = _M0L3synS1936->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5855);
        _M0L8_2afieldS5854 = _M0L3synS1936->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5854);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1936);
      }
      _M0L6rowptrS5586 = _M0L6matrixS5591->$2;
      _M0L6_2acntS5864
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5591));
      if (_M0L6_2acntS5864 > 1) {
        int32_t _M0L11_2anew__cntS5867 = _M0L6_2acntS5864 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5591), _M0L11_2anew__cntS5867);
        moonbit_incref_cycle_free(_M0L6rowptrS5586);
      } else if (_M0L6_2acntS5864 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5866 = _M0L6matrixS5591->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5865;
        moonbit_decref_cycle_free(_M0L8_2afieldS5866);
        _M0L8_2afieldS5865 = _M0L6matrixS5591->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5865);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5591);
      }
      _M0L4varsS5587 = _M0L1eS1935->$3;
      _M0L5paramS5588 = _M0L1eS1935->$4;
      _M0L6t__nowS5590 = _M0L1eS1935->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5590);
      moonbit_incref_cycle_free(_M0L5paramS5588);
      moonbit_incref_cycle_free(_M0L4varsS5587);
      #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5589 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5590, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5590);
      #line 137 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt10stdp__step(_M0L4valsS5582, _M0L4fireS5583, _M0L4fireS5584, _M0L6colptrS5585, _M0L6rowptrS5586, _M0L4varsS5587, _M0L5paramS5588, _M0L6_2atmpS5589, _M0L2dtS1873);
      moonbit_decref_cycle_free(_M0L4valsS5582);
      moonbit_decref_cycle_free(_M0L4fireS5583);
      moonbit_decref_cycle_free(_M0L4fireS5584);
      moonbit_decref_cycle_free(_M0L6colptrS5585);
      moonbit_decref_cycle_free(_M0L6rowptrS5586);
      moonbit_decref_cycle_free(_M0L4varsS5587);
      moonbit_decref_cycle_free(_M0L5paramS5588);
      _M0L6t__nowS5596 = _M0L1eS1935->$5;
      _M0L6t__nowS5599 = _M0L1eS1935->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5596);
      _M0L6_2acntS5868 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1935));
      if (_M0L6_2acntS5868 > 1) {
        int32_t _M0L11_2anew__cntS5871 = _M0L6_2acntS5868 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1935), _M0L11_2anew__cntS5871);
        moonbit_incref_cycle_free(_M0L6t__nowS5599);
      } else if (_M0L6_2acntS5868 == 1) {
        struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L8_2afieldS5870 =
          _M0L1eS1935->$4;
        struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L8_2afieldS5869;
        moonbit_decref_cycle_free(_M0L8_2afieldS5870);
        _M0L8_2afieldS5869 = _M0L1eS1935->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5869);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1935);
      }
      #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5598 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5599, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5599);
      _M0L6_2atmpS5597 = _M0L6_2atmpS5598 + _M0L2dtS1873;
      #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5596, 0, _M0L6_2atmpS5597);
      moonbit_decref_cycle_free(_M0L6t__nowS5596);
      joinlet_5916:;
      goto joinlet_5915;
      join_1931:;
      _M0L5connsS5580 = _M0L1mS1868->$1;
      _M0L11conn__indexS5581 = _M0L1eS1932->$0;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1933
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5580, _M0L11conn__indexS5581);
      _M0L6matrixS5575 = _M0L3synS1933->$4;
      _M0L4valsS5563 = _M0L6matrixS5575->$4;
      _M0L3preS5574 = _M0L3synS1933->$0;
      _M0L4fireS5564 = _M0L3preS5574->$5;
      _M0L4postS5573 = _M0L3synS1933->$1;
      _M0L4fireS5565 = _M0L4postS5573->$5;
      _M0L6matrixS5572 = _M0L3synS1933->$4;
      _M0L6colptrS5566 = _M0L6matrixS5572->$3;
      _M0L6matrixS5571 = _M0L3synS1933->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5566);
      moonbit_incref_cycle_free(_M0L4fireS5565);
      moonbit_incref_cycle_free(_M0L4fireS5564);
      moonbit_incref_cycle_free(_M0L4valsS5563);
      _M0L6_2acntS5833
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1933));
      if (_M0L6_2acntS5833 > 1) {
        int32_t _M0L11_2anew__cntS5843 = _M0L6_2acntS5833 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1933), _M0L11_2anew__cntS5843);
        moonbit_incref_cycle_free(_M0L6matrixS5571);
      } else if (_M0L6_2acntS5833 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5842 = _M0L3synS1933->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5841;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5840;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5839;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5838;
        moonbit_string_t _M0L8_2afieldS5837;
        moonbit_string_t _M0L8_2afieldS5836;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5835;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5834;
        moonbit_decref_cycle_free(_M0L8_2afieldS5842);
        _M0L8_2afieldS5841 = _M0L3synS1933->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5841);
        _M0L8_2afieldS5840 = _M0L3synS1933->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5840);
        _M0L8_2afieldS5839 = _M0L3synS1933->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5839);
        _M0L8_2afieldS5838 = _M0L3synS1933->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5838);
        _M0L8_2afieldS5837 = _M0L3synS1933->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5837);
        _M0L8_2afieldS5836 = _M0L3synS1933->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5836);
        _M0L8_2afieldS5835 = _M0L3synS1933->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5835);
        _M0L8_2afieldS5834 = _M0L3synS1933->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5834);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1933);
      }
      _M0L6rowptrS5567 = _M0L6matrixS5571->$2;
      _M0L6_2acntS5844
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5571));
      if (_M0L6_2acntS5844 > 1) {
        int32_t _M0L11_2anew__cntS5847 = _M0L6_2acntS5844 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5571), _M0L11_2anew__cntS5847);
        moonbit_incref_cycle_free(_M0L6rowptrS5567);
      } else if (_M0L6_2acntS5844 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5846 = _M0L6matrixS5571->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5845;
        moonbit_decref_cycle_free(_M0L8_2afieldS5846);
        _M0L8_2afieldS5845 = _M0L6matrixS5571->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5845);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5571);
      }
      _M0L4tpreS5568 = _M0L1eS1932->$4;
      _M0L5tpostS5569 = _M0L1eS1932->$5;
      _M0L5paramS5570 = _M0L1eS1932->$3;
      moonbit_incref_cycle_free(_M0L5paramS5570);
      moonbit_incref_cycle_free(_M0L5tpostS5569);
      moonbit_incref_cycle_free(_M0L4tpreS5568);
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt24stdp__mexican__hat__step(_M0L4valsS5563, _M0L4fireS5564, _M0L4fireS5565, _M0L6colptrS5566, _M0L6rowptrS5567, _M0L4tpreS5568, _M0L5tpostS5569, _M0L5paramS5570, _M0L2dtS1873);
      moonbit_decref_cycle_free(_M0L4valsS5563);
      moonbit_decref_cycle_free(_M0L4fireS5564);
      moonbit_decref_cycle_free(_M0L4fireS5565);
      moonbit_decref_cycle_free(_M0L6colptrS5566);
      moonbit_decref_cycle_free(_M0L6rowptrS5567);
      moonbit_decref_cycle_free(_M0L4tpreS5568);
      moonbit_decref_cycle_free(_M0L5tpostS5569);
      moonbit_decref_cycle_free(_M0L5paramS5570);
      _M0L6t__nowS5576 = _M0L1eS1932->$6;
      _M0L6t__nowS5579 = _M0L1eS1932->$6;
      moonbit_incref_cycle_free(_M0L6t__nowS5576);
      _M0L6_2acntS5848 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1932));
      if (_M0L6_2acntS5848 > 1) {
        int32_t _M0L11_2anew__cntS5852 = _M0L6_2acntS5848 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1932), _M0L11_2anew__cntS5852);
        moonbit_incref_cycle_free(_M0L6t__nowS5579);
      } else if (_M0L6_2acntS5848 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5851 = _M0L1eS1932->$5;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5850;
        struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L8_2afieldS5849;
        moonbit_decref_cycle_free(_M0L8_2afieldS5851);
        _M0L8_2afieldS5850 = _M0L1eS1932->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS5850);
        _M0L8_2afieldS5849 = _M0L1eS1932->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5849);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1932);
      }
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5578 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5579, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5579);
      _M0L6_2atmpS5577 = _M0L6_2atmpS5578 + _M0L2dtS1873;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5576, 0, _M0L6_2atmpS5577);
      moonbit_decref_cycle_free(_M0L6t__nowS5576);
      joinlet_5915:;
      goto joinlet_5914;
      join_1928:;
      _M0L5connsS5561 = _M0L1mS1868->$1;
      _M0L11conn__indexS5562 = _M0L1eS1929->$0;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1930
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5561, _M0L11conn__indexS5562);
      _M0L6matrixS5556 = _M0L3synS1930->$4;
      _M0L4valsS5545 = _M0L6matrixS5556->$4;
      _M0L3preS5555 = _M0L3synS1930->$0;
      _M0L4fireS5546 = _M0L3preS5555->$5;
      _M0L4postS5554 = _M0L3synS1930->$1;
      _M0L4fireS5547 = _M0L4postS5554->$5;
      _M0L6matrixS5553 = _M0L3synS1930->$4;
      _M0L6colptrS5548 = _M0L6matrixS5553->$3;
      _M0L6matrixS5552 = _M0L3synS1930->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5548);
      moonbit_incref_cycle_free(_M0L4fireS5547);
      moonbit_incref_cycle_free(_M0L4fireS5546);
      moonbit_incref_cycle_free(_M0L4valsS5545);
      _M0L6_2acntS5814
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1930));
      if (_M0L6_2acntS5814 > 1) {
        int32_t _M0L11_2anew__cntS5824 = _M0L6_2acntS5814 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1930), _M0L11_2anew__cntS5824);
        moonbit_incref_cycle_free(_M0L6matrixS5552);
      } else if (_M0L6_2acntS5814 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5823 = _M0L3synS1930->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5822;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5821;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5820;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5819;
        moonbit_string_t _M0L8_2afieldS5818;
        moonbit_string_t _M0L8_2afieldS5817;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5816;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5815;
        moonbit_decref_cycle_free(_M0L8_2afieldS5823);
        _M0L8_2afieldS5822 = _M0L3synS1930->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5822);
        _M0L8_2afieldS5821 = _M0L3synS1930->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5821);
        _M0L8_2afieldS5820 = _M0L3synS1930->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5820);
        _M0L8_2afieldS5819 = _M0L3synS1930->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5819);
        _M0L8_2afieldS5818 = _M0L3synS1930->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5818);
        _M0L8_2afieldS5817 = _M0L3synS1930->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5817);
        _M0L8_2afieldS5816 = _M0L3synS1930->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5816);
        _M0L8_2afieldS5815 = _M0L3synS1930->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5815);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1930);
      }
      _M0L6rowptrS5549 = _M0L6matrixS5552->$2;
      _M0L6_2acntS5825
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5552));
      if (_M0L6_2acntS5825 > 1) {
        int32_t _M0L11_2anew__cntS5828 = _M0L6_2acntS5825 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5552), _M0L11_2anew__cntS5828);
        moonbit_incref_cycle_free(_M0L6rowptrS5549);
      } else if (_M0L6_2acntS5825 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5827 = _M0L6matrixS5552->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5826;
        moonbit_decref_cycle_free(_M0L8_2afieldS5827);
        _M0L8_2afieldS5826 = _M0L6matrixS5552->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5826);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5552);
      }
      _M0L4varsS5550 = _M0L1eS1929->$4;
      _M0L5paramS5551 = _M0L1eS1929->$3;
      moonbit_incref_cycle_free(_M0L5paramS5551);
      moonbit_incref_cycle_free(_M0L4varsS5550);
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt25stdp__antisymmetric__step(_M0L4valsS5545, _M0L4fireS5546, _M0L4fireS5547, _M0L6colptrS5548, _M0L6rowptrS5549, _M0L4varsS5550, _M0L5paramS5551, _M0L2dtS1873);
      moonbit_decref_cycle_free(_M0L4valsS5545);
      moonbit_decref_cycle_free(_M0L4fireS5546);
      moonbit_decref_cycle_free(_M0L4fireS5547);
      moonbit_decref_cycle_free(_M0L6colptrS5548);
      moonbit_decref_cycle_free(_M0L6rowptrS5549);
      moonbit_decref_cycle_free(_M0L4varsS5550);
      moonbit_decref_cycle_free(_M0L5paramS5551);
      _M0L6t__nowS5557 = _M0L1eS1929->$5;
      _M0L6t__nowS5560 = _M0L1eS1929->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5557);
      _M0L6_2acntS5829 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1929));
      if (_M0L6_2acntS5829 > 1) {
        int32_t _M0L11_2anew__cntS5832 = _M0L6_2acntS5829 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1929), _M0L11_2anew__cntS5832);
        moonbit_incref_cycle_free(_M0L6t__nowS5560);
      } else if (_M0L6_2acntS5829 == 1) {
        struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L8_2afieldS5831 =
          _M0L1eS1929->$4;
        struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L8_2afieldS5830;
        moonbit_decref_cycle_free(_M0L8_2afieldS5831);
        _M0L8_2afieldS5830 = _M0L1eS1929->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5830);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1929);
      }
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5559 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5560, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5560);
      _M0L6_2atmpS5558 = _M0L6_2atmpS5559 + _M0L2dtS1873;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5557, 0, _M0L6_2atmpS5558);
      moonbit_decref_cycle_free(_M0L6t__nowS5557);
      joinlet_5914:;
      goto joinlet_5913;
      join_1925:;
      _M0L5connsS5543 = _M0L1mS1868->$1;
      _M0L11conn__indexS5544 = _M0L1eS1926->$0;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1927
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5543, _M0L11conn__indexS5544);
      _M0L6matrixS5538 = _M0L3synS1927->$4;
      _M0L4valsS5525 = _M0L6matrixS5538->$4;
      _M0L3preS5537 = _M0L3synS1927->$0;
      _M0L4fireS5526 = _M0L3preS5537->$5;
      _M0L4postS5536 = _M0L3synS1927->$1;
      _M0L4fireS5527 = _M0L4postS5536->$5;
      _M0L6matrixS5535 = _M0L3synS1927->$4;
      _M0L6colptrS5528 = _M0L6matrixS5535->$3;
      _M0L6matrixS5534 = _M0L3synS1927->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5528);
      moonbit_incref_cycle_free(_M0L4fireS5527);
      moonbit_incref_cycle_free(_M0L4fireS5526);
      moonbit_incref_cycle_free(_M0L4valsS5525);
      _M0L6_2acntS5795
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1927));
      if (_M0L6_2acntS5795 > 1) {
        int32_t _M0L11_2anew__cntS5805 = _M0L6_2acntS5795 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1927), _M0L11_2anew__cntS5805);
        moonbit_incref_cycle_free(_M0L6matrixS5534);
      } else if (_M0L6_2acntS5795 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5804 = _M0L3synS1927->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5803;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5802;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5801;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5800;
        moonbit_string_t _M0L8_2afieldS5799;
        moonbit_string_t _M0L8_2afieldS5798;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5797;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5796;
        moonbit_decref_cycle_free(_M0L8_2afieldS5804);
        _M0L8_2afieldS5803 = _M0L3synS1927->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5803);
        _M0L8_2afieldS5802 = _M0L3synS1927->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5802);
        _M0L8_2afieldS5801 = _M0L3synS1927->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5801);
        _M0L8_2afieldS5800 = _M0L3synS1927->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5800);
        _M0L8_2afieldS5799 = _M0L3synS1927->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5799);
        _M0L8_2afieldS5798 = _M0L3synS1927->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5798);
        _M0L8_2afieldS5797 = _M0L3synS1927->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5797);
        _M0L8_2afieldS5796 = _M0L3synS1927->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5796);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1927);
      }
      _M0L6rowptrS5529 = _M0L6matrixS5534->$2;
      _M0L6_2acntS5806
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5534));
      if (_M0L6_2acntS5806 > 1) {
        int32_t _M0L11_2anew__cntS5809 = _M0L6_2acntS5806 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5534), _M0L11_2anew__cntS5809);
        moonbit_incref_cycle_free(_M0L6rowptrS5529);
      } else if (_M0L6_2acntS5806 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5808 = _M0L6matrixS5534->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5807;
        moonbit_decref_cycle_free(_M0L8_2afieldS5808);
        _M0L8_2afieldS5807 = _M0L6matrixS5534->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5807);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5534);
      }
      _M0L4varsS5530 = _M0L1eS1926->$4;
      _M0L5paramS5531 = _M0L1eS1926->$3;
      _M0L6t__nowS5533 = _M0L1eS1926->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5533);
      moonbit_incref_cycle_free(_M0L5paramS5531);
      moonbit_incref_cycle_free(_M0L4varsS5530);
      #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5532 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5533, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5533);
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt22stdp__confavreux__step(_M0L4valsS5525, _M0L4fireS5526, _M0L4fireS5527, _M0L6colptrS5528, _M0L6rowptrS5529, _M0L4varsS5530, _M0L5paramS5531, _M0L6_2atmpS5532, _M0L2dtS1873);
      moonbit_decref_cycle_free(_M0L4valsS5525);
      moonbit_decref_cycle_free(_M0L4fireS5526);
      moonbit_decref_cycle_free(_M0L4fireS5527);
      moonbit_decref_cycle_free(_M0L6colptrS5528);
      moonbit_decref_cycle_free(_M0L6rowptrS5529);
      moonbit_decref_cycle_free(_M0L4varsS5530);
      moonbit_decref_cycle_free(_M0L5paramS5531);
      _M0L6t__nowS5539 = _M0L1eS1926->$5;
      _M0L6t__nowS5542 = _M0L1eS1926->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5539);
      _M0L6_2acntS5810 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1926));
      if (_M0L6_2acntS5810 > 1) {
        int32_t _M0L11_2anew__cntS5813 = _M0L6_2acntS5810 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1926), _M0L11_2anew__cntS5813);
        moonbit_incref_cycle_free(_M0L6t__nowS5542);
      } else if (_M0L6_2acntS5810 == 1) {
        struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L8_2afieldS5812 =
          _M0L1eS1926->$4;
        struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L8_2afieldS5811;
        moonbit_decref_cycle_free(_M0L8_2afieldS5812);
        _M0L8_2afieldS5811 = _M0L1eS1926->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5811);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1926);
      }
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5541 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5542, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5542);
      _M0L6_2atmpS5540 = _M0L6_2atmpS5541 + _M0L2dtS1873;
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5539, 0, _M0L6_2atmpS5540);
      moonbit_decref_cycle_free(_M0L6t__nowS5539);
      joinlet_5913:;
      goto joinlet_5912;
      join_1922:;
      _M0L5connsS5523 = _M0L1mS1868->$1;
      _M0L11conn__indexS5524 = _M0L1eS1923->$0;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1924
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5523, _M0L11conn__indexS5524);
      _M0L6matrixS5518 = _M0L3synS1924->$4;
      _M0L4valsS5505 = _M0L6matrixS5518->$4;
      _M0L3preS5517 = _M0L3synS1924->$0;
      _M0L4fireS5506 = _M0L3preS5517->$5;
      _M0L4postS5516 = _M0L3synS1924->$1;
      _M0L4fireS5507 = _M0L4postS5516->$5;
      _M0L6matrixS5515 = _M0L3synS1924->$4;
      _M0L6colptrS5508 = _M0L6matrixS5515->$3;
      _M0L6matrixS5514 = _M0L3synS1924->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5508);
      moonbit_incref_cycle_free(_M0L4fireS5507);
      moonbit_incref_cycle_free(_M0L4fireS5506);
      moonbit_incref_cycle_free(_M0L4valsS5505);
      _M0L6_2acntS5776
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1924));
      if (_M0L6_2acntS5776 > 1) {
        int32_t _M0L11_2anew__cntS5786 = _M0L6_2acntS5776 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1924), _M0L11_2anew__cntS5786);
        moonbit_incref_cycle_free(_M0L6matrixS5514);
      } else if (_M0L6_2acntS5776 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5785 = _M0L3synS1924->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5784;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5783;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5782;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5781;
        moonbit_string_t _M0L8_2afieldS5780;
        moonbit_string_t _M0L8_2afieldS5779;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5778;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5777;
        moonbit_decref_cycle_free(_M0L8_2afieldS5785);
        _M0L8_2afieldS5784 = _M0L3synS1924->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5784);
        _M0L8_2afieldS5783 = _M0L3synS1924->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5783);
        _M0L8_2afieldS5782 = _M0L3synS1924->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5782);
        _M0L8_2afieldS5781 = _M0L3synS1924->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5781);
        _M0L8_2afieldS5780 = _M0L3synS1924->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5780);
        _M0L8_2afieldS5779 = _M0L3synS1924->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5779);
        _M0L8_2afieldS5778 = _M0L3synS1924->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5778);
        _M0L8_2afieldS5777 = _M0L3synS1924->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5777);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1924);
      }
      _M0L6rowptrS5509 = _M0L6matrixS5514->$2;
      _M0L6_2acntS5787
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5514));
      if (_M0L6_2acntS5787 > 1) {
        int32_t _M0L11_2anew__cntS5790 = _M0L6_2acntS5787 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5514), _M0L11_2anew__cntS5790);
        moonbit_incref_cycle_free(_M0L6rowptrS5509);
      } else if (_M0L6_2acntS5787 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5789 = _M0L6matrixS5514->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5788;
        moonbit_decref_cycle_free(_M0L8_2afieldS5789);
        _M0L8_2afieldS5788 = _M0L6matrixS5514->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5788);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5514);
      }
      _M0L4varsS5510 = _M0L1eS1923->$4;
      _M0L5paramS5511 = _M0L1eS1923->$3;
      _M0L6t__nowS5513 = _M0L1eS1923->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5513);
      moonbit_incref_cycle_free(_M0L5paramS5511);
      moonbit_incref_cycle_free(_M0L4varsS5510);
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5512 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5513, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5513);
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt17istdp__rate__step(_M0L4valsS5505, _M0L4fireS5506, _M0L4fireS5507, _M0L6colptrS5508, _M0L6rowptrS5509, _M0L4varsS5510, _M0L5paramS5511, _M0L6_2atmpS5512, _M0L2dtS1873);
      moonbit_decref_cycle_free(_M0L4valsS5505);
      moonbit_decref_cycle_free(_M0L4fireS5506);
      moonbit_decref_cycle_free(_M0L4fireS5507);
      moonbit_decref_cycle_free(_M0L6colptrS5508);
      moonbit_decref_cycle_free(_M0L6rowptrS5509);
      moonbit_decref_cycle_free(_M0L4varsS5510);
      moonbit_decref_cycle_free(_M0L5paramS5511);
      _M0L6t__nowS5519 = _M0L1eS1923->$5;
      _M0L6t__nowS5522 = _M0L1eS1923->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5519);
      _M0L6_2acntS5791 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1923));
      if (_M0L6_2acntS5791 > 1) {
        int32_t _M0L11_2anew__cntS5794 = _M0L6_2acntS5791 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1923), _M0L11_2anew__cntS5794);
        moonbit_incref_cycle_free(_M0L6t__nowS5522);
      } else if (_M0L6_2acntS5791 == 1) {
        struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L8_2afieldS5793 =
          _M0L1eS1923->$4;
        struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L8_2afieldS5792;
        moonbit_decref_cycle_free(_M0L8_2afieldS5793);
        _M0L8_2afieldS5792 = _M0L1eS1923->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5792);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1923);
      }
      #line 207 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5521 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5522, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5522);
      _M0L6_2atmpS5520 = _M0L6_2atmpS5521 + _M0L2dtS1873;
      #line 207 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5519, 0, _M0L6_2atmpS5520);
      moonbit_decref_cycle_free(_M0L6t__nowS5519);
      joinlet_5912:;
      goto joinlet_5911;
      join_1919:;
      _M0L5connsS5503 = _M0L1mS1868->$1;
      _M0L11conn__indexS5504 = _M0L1eS1920->$0;
      #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1921
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5503, _M0L11conn__indexS5504);
      _M0L6matrixS5498 = _M0L3synS1921->$4;
      _M0L4valsS5483 = _M0L6matrixS5498->$4;
      _M0L3preS5497 = _M0L3synS1921->$0;
      _M0L4fireS5484 = _M0L3preS5497->$5;
      _M0L4postS5496 = _M0L3synS1921->$1;
      _M0L4fireS5485 = _M0L4postS5496->$5;
      _M0L6matrixS5495 = _M0L3synS1921->$4;
      _M0L6colptrS5486 = _M0L6matrixS5495->$3;
      _M0L6matrixS5494 = _M0L3synS1921->$4;
      _M0L6rowptrS5487 = _M0L6matrixS5494->$2;
      _M0L4postS5493 = _M0L3synS1921->$1;
      moonbit_incref_cycle_free(_M0L6rowptrS5487);
      moonbit_incref_cycle_free(_M0L6colptrS5486);
      moonbit_incref_cycle_free(_M0L4fireS5485);
      moonbit_incref_cycle_free(_M0L4fireS5484);
      moonbit_incref_cycle_free(_M0L4valsS5483);
      _M0L6_2acntS5744
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1921));
      if (_M0L6_2acntS5744 > 1) {
        int32_t _M0L11_2anew__cntS5754 = _M0L6_2acntS5744 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1921), _M0L11_2anew__cntS5754);
        moonbit_incref_cycle_free(_M0L4postS5493);
      } else if (_M0L6_2acntS5744 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5753 = _M0L3synS1921->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5752;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5751;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5750;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5749;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L8_2afieldS5748;
        moonbit_string_t _M0L8_2afieldS5747;
        moonbit_string_t _M0L8_2afieldS5746;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5745;
        moonbit_decref_cycle_free(_M0L8_2afieldS5753);
        _M0L8_2afieldS5752 = _M0L3synS1921->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5752);
        _M0L8_2afieldS5751 = _M0L3synS1921->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5751);
        _M0L8_2afieldS5750 = _M0L3synS1921->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5750);
        _M0L8_2afieldS5749 = _M0L3synS1921->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5749);
        _M0L8_2afieldS5748 = _M0L3synS1921->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS5748);
        _M0L8_2afieldS5747 = _M0L3synS1921->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5747);
        _M0L8_2afieldS5746 = _M0L3synS1921->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5746);
        _M0L8_2afieldS5745 = _M0L3synS1921->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5745);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1921);
      }
      _M0L1vS5488 = _M0L4postS5493->$3;
      _M0L6_2acntS5755
      = Moonbit_rc_count(Moonbit_object_header(_M0L4postS5493));
      if (_M0L6_2acntS5755 > 1) {
        int32_t _M0L11_2anew__cntS5771 = _M0L6_2acntS5755 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L4postS5493), _M0L11_2anew__cntS5771);
        moonbit_incref_cycle_free(_M0L1vS5488);
      } else if (_M0L6_2acntS5755 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5770 = _M0L4postS5493->$16;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5769;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5768;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5767;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5766;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5765;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5764;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5763;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5762;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5761;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5760;
        struct _M0TPB5ArrayGbE* _M0L8_2afieldS5759;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5758;
        struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L8_2afieldS5757;
        struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L8_2afieldS5756;
        moonbit_decref_cycle_free(_M0L8_2afieldS5770);
        _M0L8_2afieldS5769 = _M0L4postS5493->$15;
        moonbit_decref_cycle_free(_M0L8_2afieldS5769);
        _M0L8_2afieldS5768 = _M0L4postS5493->$14;
        moonbit_decref_cycle_free(_M0L8_2afieldS5768);
        _M0L8_2afieldS5767 = _M0L4postS5493->$13;
        moonbit_decref_cycle_free(_M0L8_2afieldS5767);
        _M0L8_2afieldS5766 = _M0L4postS5493->$12;
        moonbit_decref_cycle_free(_M0L8_2afieldS5766);
        _M0L8_2afieldS5765 = _M0L4postS5493->$11;
        moonbit_decref_cycle_free(_M0L8_2afieldS5765);
        _M0L8_2afieldS5764 = _M0L4postS5493->$10;
        moonbit_decref_cycle_free(_M0L8_2afieldS5764);
        _M0L8_2afieldS5763 = _M0L4postS5493->$9;
        moonbit_decref_cycle_free(_M0L8_2afieldS5763);
        _M0L8_2afieldS5762 = _M0L4postS5493->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5762);
        _M0L8_2afieldS5761 = _M0L4postS5493->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5761);
        _M0L8_2afieldS5760 = _M0L4postS5493->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5760);
        _M0L8_2afieldS5759 = _M0L4postS5493->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5759);
        _M0L8_2afieldS5758 = _M0L4postS5493->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS5758);
        _M0L8_2afieldS5757 = _M0L4postS5493->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5757);
        _M0L8_2afieldS5756 = _M0L4postS5493->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5756);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L4postS5493);
      }
      _M0L4varsS5489 = _M0L1eS1920->$4;
      _M0L5paramS5490 = _M0L1eS1920->$3;
      _M0L6t__nowS5492 = _M0L1eS1920->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5492);
      moonbit_incref_cycle_free(_M0L5paramS5490);
      moonbit_incref_cycle_free(_M0L4varsS5489);
      #line 220 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5491 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5492, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5492);
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt22istdp__potential__step(_M0L4valsS5483, _M0L4fireS5484, _M0L4fireS5485, _M0L6colptrS5486, _M0L6rowptrS5487, _M0L1vS5488, _M0L4varsS5489, _M0L5paramS5490, _M0L6_2atmpS5491, _M0L2dtS1873);
      moonbit_decref_cycle_free(_M0L4valsS5483);
      moonbit_decref_cycle_free(_M0L4fireS5484);
      moonbit_decref_cycle_free(_M0L4fireS5485);
      moonbit_decref_cycle_free(_M0L6colptrS5486);
      moonbit_decref_cycle_free(_M0L6rowptrS5487);
      moonbit_decref_cycle_free(_M0L1vS5488);
      moonbit_decref_cycle_free(_M0L4varsS5489);
      moonbit_decref_cycle_free(_M0L5paramS5490);
      _M0L6t__nowS5499 = _M0L1eS1920->$5;
      _M0L6t__nowS5502 = _M0L1eS1920->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5499);
      _M0L6_2acntS5772 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1920));
      if (_M0L6_2acntS5772 > 1) {
        int32_t _M0L11_2anew__cntS5775 = _M0L6_2acntS5772 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1920), _M0L11_2anew__cntS5775);
        moonbit_incref_cycle_free(_M0L6t__nowS5502);
      } else if (_M0L6_2acntS5772 == 1) {
        struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L8_2afieldS5774 =
          _M0L1eS1920->$4;
        struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L8_2afieldS5773;
        moonbit_decref_cycle_free(_M0L8_2afieldS5774);
        _M0L8_2afieldS5773 = _M0L1eS1920->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5773);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1920);
      }
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5501 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5502, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5502);
      _M0L6_2atmpS5500 = _M0L6_2atmpS5501 + _M0L2dtS1873;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5499, 0, _M0L6_2atmpS5500);
      moonbit_decref_cycle_free(_M0L6t__nowS5499);
      joinlet_5911:;
      goto joinlet_5910;
      join_1916:;
      _M0L5connsS5481 = _M0L1mS1868->$1;
      _M0L11conn__indexS5482 = _M0L1eS1917->$0;
      #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1918
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5481, _M0L11conn__indexS5482);
      _M0L6matrixS5476 = _M0L3synS1918->$4;
      _M0L4valsS5463 = _M0L6matrixS5476->$4;
      _M0L3preS5475 = _M0L3synS1918->$0;
      _M0L4fireS5464 = _M0L3preS5475->$5;
      _M0L4postS5474 = _M0L3synS1918->$1;
      _M0L4fireS5465 = _M0L4postS5474->$5;
      _M0L6matrixS5473 = _M0L3synS1918->$4;
      _M0L6colptrS5466 = _M0L6matrixS5473->$3;
      _M0L6matrixS5472 = _M0L3synS1918->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5466);
      moonbit_incref_cycle_free(_M0L4fireS5465);
      moonbit_incref_cycle_free(_M0L4fireS5464);
      moonbit_incref_cycle_free(_M0L4valsS5463);
      _M0L6_2acntS5725
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1918));
      if (_M0L6_2acntS5725 > 1) {
        int32_t _M0L11_2anew__cntS5735 = _M0L6_2acntS5725 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1918), _M0L11_2anew__cntS5735);
        moonbit_incref_cycle_free(_M0L6matrixS5472);
      } else if (_M0L6_2acntS5725 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5734 = _M0L3synS1918->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5733;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5732;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5731;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5730;
        moonbit_string_t _M0L8_2afieldS5729;
        moonbit_string_t _M0L8_2afieldS5728;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5727;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5726;
        moonbit_decref_cycle_free(_M0L8_2afieldS5734);
        _M0L8_2afieldS5733 = _M0L3synS1918->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5733);
        _M0L8_2afieldS5732 = _M0L3synS1918->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5732);
        _M0L8_2afieldS5731 = _M0L3synS1918->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5731);
        _M0L8_2afieldS5730 = _M0L3synS1918->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5730);
        _M0L8_2afieldS5729 = _M0L3synS1918->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5729);
        _M0L8_2afieldS5728 = _M0L3synS1918->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5728);
        _M0L8_2afieldS5727 = _M0L3synS1918->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5727);
        _M0L8_2afieldS5726 = _M0L3synS1918->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5726);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1918);
      }
      _M0L6rowptrS5467 = _M0L6matrixS5472->$2;
      _M0L6_2acntS5736
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5472));
      if (_M0L6_2acntS5736 > 1) {
        int32_t _M0L11_2anew__cntS5739 = _M0L6_2acntS5736 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5472), _M0L11_2anew__cntS5739);
        moonbit_incref_cycle_free(_M0L6rowptrS5467);
      } else if (_M0L6_2acntS5736 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5738 = _M0L6matrixS5472->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5737;
        moonbit_decref_cycle_free(_M0L8_2afieldS5738);
        _M0L8_2afieldS5737 = _M0L6matrixS5472->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5737);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5472);
      }
      _M0L4varsS5468 = _M0L1eS1917->$4;
      _M0L5paramS5469 = _M0L1eS1917->$3;
      _M0L6t__nowS5471 = _M0L1eS1917->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5471);
      moonbit_incref_cycle_free(_M0L5paramS5469);
      moonbit_incref_cycle_free(_M0L4varsS5468);
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5470 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5471, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5471);
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt21stdp__symmetric__step(_M0L4valsS5463, _M0L4fireS5464, _M0L4fireS5465, _M0L6colptrS5466, _M0L6rowptrS5467, _M0L4varsS5468, _M0L5paramS5469, _M0L6_2atmpS5470, _M0L2dtS1873);
      moonbit_decref_cycle_free(_M0L4valsS5463);
      moonbit_decref_cycle_free(_M0L4fireS5464);
      moonbit_decref_cycle_free(_M0L4fireS5465);
      moonbit_decref_cycle_free(_M0L6colptrS5466);
      moonbit_decref_cycle_free(_M0L6rowptrS5467);
      moonbit_decref_cycle_free(_M0L4varsS5468);
      moonbit_decref_cycle_free(_M0L5paramS5469);
      _M0L6t__nowS5477 = _M0L1eS1917->$5;
      _M0L6t__nowS5480 = _M0L1eS1917->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5477);
      _M0L6_2acntS5740 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1917));
      if (_M0L6_2acntS5740 > 1) {
        int32_t _M0L11_2anew__cntS5743 = _M0L6_2acntS5740 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1917), _M0L11_2anew__cntS5743);
        moonbit_incref_cycle_free(_M0L6t__nowS5480);
      } else if (_M0L6_2acntS5740 == 1) {
        struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L8_2afieldS5742 =
          _M0L1eS1917->$4;
        struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L8_2afieldS5741;
        moonbit_decref_cycle_free(_M0L8_2afieldS5742);
        _M0L8_2afieldS5741 = _M0L1eS1917->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5741);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1917);
      }
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5479 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5480, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5480);
      _M0L6_2atmpS5478 = _M0L6_2atmpS5479 + _M0L2dtS1873;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5477, 0, _M0L6_2atmpS5478);
      moonbit_decref_cycle_free(_M0L6t__nowS5477);
      joinlet_5910:;
      goto joinlet_5909;
      join_1913:;
      _M0L5connsS5461 = _M0L1mS1868->$1;
      _M0L11conn__indexS5462 = _M0L1eS1914->$0;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1915
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5461, _M0L11conn__indexS5462);
      _M0L6matrixS5456 = _M0L3synS1915->$4;
      _M0L4valsS5443 = _M0L6matrixS5456->$4;
      _M0L3preS5455 = _M0L3synS1915->$0;
      _M0L4fireS5444 = _M0L3preS5455->$5;
      _M0L4postS5454 = _M0L3synS1915->$1;
      _M0L4fireS5445 = _M0L4postS5454->$5;
      _M0L6matrixS5453 = _M0L3synS1915->$4;
      _M0L6colptrS5446 = _M0L6matrixS5453->$3;
      _M0L6matrixS5452 = _M0L3synS1915->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5446);
      moonbit_incref_cycle_free(_M0L4fireS5445);
      moonbit_incref_cycle_free(_M0L4fireS5444);
      moonbit_incref_cycle_free(_M0L4valsS5443);
      _M0L6_2acntS5706
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1915));
      if (_M0L6_2acntS5706 > 1) {
        int32_t _M0L11_2anew__cntS5716 = _M0L6_2acntS5706 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1915), _M0L11_2anew__cntS5716);
        moonbit_incref_cycle_free(_M0L6matrixS5452);
      } else if (_M0L6_2acntS5706 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5715 = _M0L3synS1915->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5714;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5713;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5712;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5711;
        moonbit_string_t _M0L8_2afieldS5710;
        moonbit_string_t _M0L8_2afieldS5709;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5708;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5707;
        moonbit_decref_cycle_free(_M0L8_2afieldS5715);
        _M0L8_2afieldS5714 = _M0L3synS1915->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5714);
        _M0L8_2afieldS5713 = _M0L3synS1915->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5713);
        _M0L8_2afieldS5712 = _M0L3synS1915->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5712);
        _M0L8_2afieldS5711 = _M0L3synS1915->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5711);
        _M0L8_2afieldS5710 = _M0L3synS1915->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5710);
        _M0L8_2afieldS5709 = _M0L3synS1915->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5709);
        _M0L8_2afieldS5708 = _M0L3synS1915->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5708);
        _M0L8_2afieldS5707 = _M0L3synS1915->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5707);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1915);
      }
      _M0L6rowptrS5447 = _M0L6matrixS5452->$2;
      _M0L6_2acntS5717
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5452));
      if (_M0L6_2acntS5717 > 1) {
        int32_t _M0L11_2anew__cntS5720 = _M0L6_2acntS5717 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5452), _M0L11_2anew__cntS5720);
        moonbit_incref_cycle_free(_M0L6rowptrS5447);
      } else if (_M0L6_2acntS5717 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5719 = _M0L6matrixS5452->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5718;
        moonbit_decref_cycle_free(_M0L8_2afieldS5719);
        _M0L8_2afieldS5718 = _M0L6matrixS5452->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5718);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5452);
      }
      _M0L4varsS5448 = _M0L1eS1914->$4;
      _M0L5paramS5449 = _M0L1eS1914->$3;
      _M0L6t__nowS5451 = _M0L1eS1914->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5451);
      moonbit_incref_cycle_free(_M0L5paramS5449);
      moonbit_incref_cycle_free(_M0L4varsS5448);
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5450 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5451, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5451);
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt20ca__plasticity__step(_M0L4valsS5443, _M0L4fireS5444, _M0L4fireS5445, _M0L6colptrS5446, _M0L6rowptrS5447, _M0L4varsS5448, _M0L5paramS5449, _M0L6_2atmpS5450, _M0L2dtS1873);
      moonbit_decref_cycle_free(_M0L4valsS5443);
      moonbit_decref_cycle_free(_M0L4fireS5444);
      moonbit_decref_cycle_free(_M0L4fireS5445);
      moonbit_decref_cycle_free(_M0L6colptrS5446);
      moonbit_decref_cycle_free(_M0L6rowptrS5447);
      moonbit_decref_cycle_free(_M0L4varsS5448);
      moonbit_decref_cycle_free(_M0L5paramS5449);
      _M0L6t__nowS5457 = _M0L1eS1914->$5;
      _M0L6t__nowS5460 = _M0L1eS1914->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5457);
      _M0L6_2acntS5721 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1914));
      if (_M0L6_2acntS5721 > 1) {
        int32_t _M0L11_2anew__cntS5724 = _M0L6_2acntS5721 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1914), _M0L11_2anew__cntS5724);
        moonbit_incref_cycle_free(_M0L6t__nowS5460);
      } else if (_M0L6_2acntS5721 == 1) {
        struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L8_2afieldS5723 =
          _M0L1eS1914->$4;
        struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L8_2afieldS5722;
        moonbit_decref_cycle_free(_M0L8_2afieldS5723);
        _M0L8_2afieldS5722 = _M0L1eS1914->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5722);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1914);
      }
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5459 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5460, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5460);
      _M0L6_2atmpS5458 = _M0L6_2atmpS5459 + _M0L2dtS1873;
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5457, 0, _M0L6_2atmpS5458);
      moonbit_decref_cycle_free(_M0L6t__nowS5457);
      joinlet_5909:;
      _M0L6_2atmpS5602 = _M0L2__S1911 + 1;
      _M0L2__S1911 = _M0L6_2atmpS5602;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1910);
    }
    break;
  }
  _M0L7_2abindS1954 = _M0L1mS1868->$0;
  _M0L7_2abindS1955 = _M0L7_2abindS1954->$1;
  _M0L7_2abindS1956 = _M0L7_2abindS1954->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1956);
  _M0L2__S1957 = 0;
  while (1) {
    if (_M0L2__S1957 < _M0L7_2abindS1955) {
      void* _M0L1pS1958 = (void*)_M0L7_2abindS1956[_M0L2__S1957];
      int32_t _M0L6_2atmpS5603;
      moonbit_incref_cycle_free(_M0L1pS1958);
      #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt14integrate__any(_M0L1pS1958, _M0L2dtS1873);
      moonbit_decref_cycle_free(_M0L1pS1958);
      _M0L6_2atmpS5603 = _M0L2__S1957 + 1;
      _M0L2__S1957 = _M0L6_2atmpS5603;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1956);
    }
    break;
  }
  _M0L7_2abindS1960 = _M0L1mS1868->$4;
  _M0L7_2abindS1961 = _M0L7_2abindS1960->$1;
  _M0L7_2abindS1962 = _M0L7_2abindS1960->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1962);
  _M0L2__S1963 = 0;
  while (1) {
    if (_M0L2__S1963 < _M0L7_2abindS1961) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L3monS1964 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS1962[
          _M0L2__S1963
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5605 = _M0L1mS1868->$3;
      float _M0L6_2atmpS5604;
      int32_t _M0L6_2atmpS5606;
      moonbit_incref_cycle_free(_M0L3monS1964);
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5604 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5605);
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L3monS1964, _M0L6_2atmpS5604);
      moonbit_decref_cycle_free(_M0L3monS1964);
      _M0L6_2atmpS5606 = _M0L2__S1963 + 1;
      _M0L2__S1963 = _M0L6_2atmpS5606;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1962);
    }
    break;
  }
  _M0L4timeS5607 = _M0L1mS1868->$3;
  #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt12update__time(_M0L4timeS5607, _M0L2dtS1873);
  return 0;
}

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0FP26RiantR8snn__mbt7compose(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L4popsS1865,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS1866,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L11stims_2eoptS1854,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L14monitors_2eoptS1857,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L10stdp_2eoptS1860,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L9stp_2eoptS1863
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L5stimsS1853;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L8monitorsS1856;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L4stdpS1859;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L3stpS1862;
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _result_5919;
  if (_M0L11stims_2eoptS1854 == 0) {
    void** _M0L6_2atmpS5415 = (void**)moonbit_empty_ref_array;
    _M0L5stimsS1853
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE));
    Moonbit_object_header(_M0L5stimsS1853)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
    _M0L5stimsS1853->$0 = _M0L6_2atmpS5415;
    _M0L5stimsS1853->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L7_2aSomeS1855 =
      _M0L11stims_2eoptS1854;
    if (_M0L7_2aSomeS1855) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1855);
    }
    _M0L5stimsS1853 = _M0L7_2aSomeS1855;
  }
  if (_M0L14monitors_2eoptS1857 == 0) {
    struct _M0TP26RiantR8snn__mbt7Monitor** _M0L6_2atmpS5414 =
      (struct _M0TP26RiantR8snn__mbt7Monitor**)moonbit_empty_ref_array;
    _M0L8monitorsS1856
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE));
    Moonbit_object_header(_M0L8monitorsS1856)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
    _M0L8monitorsS1856->$0 = _M0L6_2atmpS5414;
    _M0L8monitorsS1856->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2aSomeS1858 =
      _M0L14monitors_2eoptS1857;
    if (_M0L7_2aSomeS1858) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1858);
    }
    _M0L8monitorsS1856 = _M0L7_2aSomeS1858;
  }
  if (_M0L10stdp_2eoptS1860 == 0) {
    void** _M0L6_2atmpS5413 = (void**)moonbit_empty_ref_array;
    _M0L4stdpS1859
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE));
    Moonbit_object_header(_M0L4stdpS1859)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
    _M0L4stdpS1859->$0 = _M0L6_2atmpS5413;
    _M0L4stdpS1859->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L7_2aSomeS1861 =
      _M0L10stdp_2eoptS1860;
    if (_M0L7_2aSomeS1861) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1861);
    }
    _M0L4stdpS1859 = _M0L7_2aSomeS1861;
  }
  if (_M0L9stp_2eoptS1863 == 0) {
    void** _M0L6_2atmpS5412 = (void**)moonbit_empty_ref_array;
    _M0L3stpS1862
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE));
    Moonbit_object_header(_M0L3stpS1862)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 27, 0);
    _M0L3stpS1862->$0 = _M0L6_2atmpS5412;
    _M0L3stpS1862->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L7_2aSomeS1864 =
      _M0L9stp_2eoptS1863;
    if (_M0L7_2aSomeS1864) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1864);
    }
    _M0L3stpS1862 = _M0L7_2aSomeS1864;
  }
  _result_5919
  = _M0FP26RiantR8snn__mbt15compose_2einner(_M0L4popsS1865, _M0L5connsS1866, _M0L5stimsS1853, _M0L8monitorsS1856, _M0L4stdpS1859, _M0L3stpS1862);
  moonbit_decref_cycle_free(_M0L5stimsS1853);
  moonbit_decref_cycle_free(_M0L8monitorsS1856);
  moonbit_decref_cycle_free(_M0L4stdpS1859);
  moonbit_decref_cycle_free(_M0L3stpS1862);
  return _result_5919;
}

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0FP26RiantR8snn__mbt15compose_2einner(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L4popsS1847,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS1848,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L5stimsS1849,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L8monitorsS1850,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L4stdpS1851,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L3stpS1852
) {
  struct _M0TP26RiantR8snn__mbt4Time* _M0L6_2atmpS5411;
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _block_5920;
  #line 79 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5411 = _M0MP26RiantR8snn__mbt4Time3new();
  moonbit_incref_cycle_free(_M0L4popsS1847);
  moonbit_incref_cycle_free(_M0L5connsS1848);
  moonbit_incref_cycle_free(_M0L5stimsS1849);
  moonbit_incref_cycle_free(_M0L8monitorsS1850);
  moonbit_incref_cycle_free(_M0L4stdpS1851);
  moonbit_incref_cycle_free(_M0L3stpS1852);
  _block_5920
  = (struct _M0TP26RiantR8snn__mbt18HeterogeneousModel*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt18HeterogeneousModel));
  Moonbit_object_header(_block_5920)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 30, 0);
  _block_5920->$0 = _M0L4popsS1847;
  _block_5920->$1 = _M0L5connsS1848;
  _block_5920->$2 = _M0L5stimsS1849;
  _block_5920->$3 = _M0L6_2atmpS5411;
  _block_5920->$4 = _M0L8monitorsS1850;
  _block_5920->$5 = _M0L4stdpS1851;
  _block_5920->$6 = _M0L3stpS1852;
  return _block_5920;
}

int32_t _M0FP26RiantR8snn__mbt14stimulate__any(
  void* _M0L1sS1833,
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS1821,
  float _M0L2dtS1828
) {
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L1xS1819;
  float _M0L1wS1820;
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L1xS1823;
  struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L1xS1825;
  struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L1xS1827;
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L1xS1830;
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L1xS1832;
  float _M0L6_2atmpS5410;
  float _M0L6_2atmpS5409;
  float _M0L6_2atmpS5408;
  float _M0L6_2atmpS5407;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  switch (Moonbit_object_tag(_M0L1sS1833)) {
    case 0: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__* _M0L14_2aPoissonIF__S1834 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__*)_M0L1sS1833;
      struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L4_2axS1835 =
        _M0L14_2aPoissonIF__S1834->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1835);
      _M0L1xS1832 = _M0L4_2axS1835;
      goto join_1831;
      break;
    }
    
    case 1: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim14PoissonLayer__* _M0L17_2aPoissonLayer__S1836 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim14PoissonLayer__*)_M0L1sS1833;
      struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L4_2axS1837 =
        _M0L17_2aPoissonLayer__S1836->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1837);
      _M0L1xS1830 = _M0L4_2axS1837;
      goto join_1829;
      break;
    }
    
    case 2: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim12BalancedIF__* _M0L15_2aBalancedIF__S1838 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim12BalancedIF__*)_M0L1sS1833;
      struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L4_2axS1839 =
        _M0L15_2aBalancedIF__S1838->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1839);
      _M0L1xS1827 = _M0L4_2axS1839;
      goto join_1826;
      break;
    }
    
    case 3: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__* _M0L14_2aCurrentIF__S1840 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__*)_M0L1sS1833;
      struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L4_2axS1841 =
        _M0L14_2aCurrentIF__S1840->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1841);
      _M0L1xS1825 = _M0L4_2axS1841;
      goto join_1824;
      break;
    }
    
    case 4: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__* _M0L15_2aCurrentArr__S1842 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__*)_M0L1sS1833;
      struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L4_2axS1843 =
        _M0L15_2aCurrentArr__S1842->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1843);
      _M0L1xS1823 = _M0L4_2axS1843;
      goto join_1822;
      break;
    }
    default: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__* _M0L14_2aTimedStim__S1844 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__*)_M0L1sS1833;
      struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L4_2axS1845 =
        _M0L14_2aTimedStim__S1844->$0;
      float _M0L4_2awS1846 = _M0L14_2aTimedStim__S1844->$1;
      moonbit_incref_cycle_free(_M0L4_2axS1845);
      _M0L1xS1819 = _M0L4_2axS1845;
      _M0L1wS1820 = _M0L4_2awS1846;
      goto join_1818;
      break;
    }
  }
  goto joinlet_5926;
  join_1831:;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5410 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1821);
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt13stimulate__if(_M0L1xS1832, _M0L6_2atmpS5410, _M0L2dtS1828);
  moonbit_decref_cycle_free(_M0L1xS1832);
  joinlet_5926:;
  goto joinlet_5925;
  join_1829:;
  #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5409 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1821);
  #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt16stimulate__layer(_M0L1xS1830, _M0L6_2atmpS5409, _M0L2dtS1828);
  moonbit_decref_cycle_free(_M0L1xS1830);
  joinlet_5925:;
  goto joinlet_5924;
  join_1826:;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5408 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1821);
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt19stimulate__balanced(_M0L1xS1827, _M0L6_2atmpS5408, _M0L2dtS1828);
  moonbit_decref_cycle_free(_M0L1xS1827);
  joinlet_5924:;
  goto joinlet_5923;
  join_1824:;
  #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt22stimulate__current__if(_M0L1xS1825);
  moonbit_decref_cycle_free(_M0L1xS1825);
  joinlet_5923:;
  goto joinlet_5922;
  join_1822:;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt25stimulate__current__array(_M0L1xS1823);
  moonbit_decref_cycle_free(_M0L1xS1823);
  joinlet_5922:;
  goto joinlet_5921;
  join_1818:;
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5407 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1821);
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt20stimulate__spiketime(_M0L1xS1819, _M0L6_2atmpS5407, _M0L1wS1820);
  moonbit_decref_cycle_free(_M0L1xS1819);
  joinlet_5921:;
  return 0;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse6random(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1811,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1812,
  moonbit_string_t _M0L3symS1817,
  float _M0L2muS1813,
  float _M0L5sigmaS1814,
  float _M0L1pS1815,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1816
) {
  int32_t _M0L1nS5405;
  int32_t _M0L1nS5406;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1810;
  float* _M0L6_2atmpS5404;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS5395;
  float* _M0L6_2atmpS5403;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS5396;
  float* _M0L6_2atmpS5402;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS5397;
  int32_t* _M0L6_2atmpS5401;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS5398;
  float* _M0L6_2atmpS5400;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS5399;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _block_5927;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS5405 = _M0L3preS1811->$2;
  _M0L1nS5406 = _M0L4postS1812->$2;
  #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6matrixS1810
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS5405, _M0L1nS5406, _M0L2muS1813, _M0L5sigmaS1814, _M0L1pS1815, _M0L3rngS1816);
  _M0L6_2atmpS5404 = moonbit_empty_float_array;
  _M0L6_2atmpS5395
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS5395)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS5395->$0 = _M0L6_2atmpS5404;
  _M0L6_2atmpS5395->$1 = 0;
  _M0L6_2atmpS5403 = moonbit_empty_float_array;
  _M0L6_2atmpS5396
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS5396)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS5396->$0 = _M0L6_2atmpS5403;
  _M0L6_2atmpS5396->$1 = 0;
  _M0L6_2atmpS5402 = moonbit_empty_float_array;
  _M0L6_2atmpS5397
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS5397)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS5397->$0 = _M0L6_2atmpS5402;
  _M0L6_2atmpS5397->$1 = 0;
  _M0L6_2atmpS5401 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS5398
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS5398)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _M0L6_2atmpS5398->$0 = _M0L6_2atmpS5401;
  _M0L6_2atmpS5398->$1 = 0;
  _M0L6_2atmpS5400 = moonbit_empty_float_array;
  _M0L6_2atmpS5399
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS5399)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS5399->$0 = _M0L6_2atmpS5400;
  _M0L6_2atmpS5399->$1 = 0;
  moonbit_incref_cycle_free(_M0L3preS1811);
  moonbit_incref_cycle_free(_M0L4postS1812);
  moonbit_incref_cycle_free(_M0L3symS1817);
  _block_5927
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse));
  Moonbit_object_header(_block_5927)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 45, 0);
  _block_5927->$0 = _M0L3preS1811;
  _block_5927->$1 = _M0L4postS1812;
  _block_5927->$2 = _M0L3symS1817;
  _block_5927->$3 = (moonbit_string_t)moonbit_string_literal_0.data;
  _block_5927->$4 = _M0L6matrixS1810;
  _block_5927->$5 = _M0L6_2atmpS5395;
  _block_5927->$6 = _M0L6_2atmpS5396;
  _block_5927->$7 = _M0L6_2atmpS5397;
  _block_5927->$8 = _M0L6_2atmpS5398;
  _block_5927->$9 = _M0L6_2atmpS5399;
  return _block_5927;
}

int32_t _M0FP26RiantR8snn__mbt22istdp__potential__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1806,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1783,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1785,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1802,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1796,
  struct _M0TPB5ArrayGfE* _M0L7v__postS1793,
  struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L4varsS1789,
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS1787,
  float _M0L6t__nowS1781,
  float _M0L2dtS1790
) {
  int32_t _M0L6n__preS1782;
  int32_t _M0L7n__postS1784;
  float _M0L6tau__yS5394;
  float _M0L11inv__tau__yS1786;
  struct _M0TPB8MutLocalGiE* _M0L1jS1788;
  struct _M0TPB8MutLocalGiE* _M0L1iS1792;
  #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6n__preS1782 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1783);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L7n__postS1784 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1785);
  _M0L6tau__yS5394 = _M0L5paramS1787->$2;
  _M0L11inv__tau__yS1786 = 0x1p+0f / _M0L6tau__yS5394;
  _M0L1jS1788
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1788)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1788->$0 = 0;
  while (1) {
    int32_t _M0L3valS5311 = _M0L1jS1788->$0;
    if (_M0L3valS5311 < _M0L6n__preS1782) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS5312 = _M0L4varsS1789->$0;
      int32_t _M0L3valS5313 = _M0L1jS1788->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5322 = _M0L4varsS1789->$0;
      int32_t _M0L3valS5323 = _M0L1jS1788->$0;
      float _M0L6_2atmpS5315;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5320;
      int32_t _M0L3valS5321;
      float _M0L6_2atmpS5319;
      float _M0L6_2atmpS5318;
      float _M0L6_2atmpS5317;
      float _M0L6_2atmpS5316;
      float _M0L6_2atmpS5314;
      int32_t _M0L3valS5324;
      int32_t _M0L3valS5332;
      int32_t _M0L6_2atmpS5331;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5315
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5322, _M0L3valS5323);
      _M0L4tpreS5320 = _M0L4varsS1789->$0;
      _M0L3valS5321 = _M0L1jS1788->$0;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5319
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5320, _M0L3valS5321);
      _M0L6_2atmpS5318 = -_M0L6_2atmpS5319;
      _M0L6_2atmpS5317 = _M0L2dtS1790 * _M0L6_2atmpS5318;
      _M0L6_2atmpS5316 = _M0L6_2atmpS5317 * _M0L11inv__tau__yS1786;
      _M0L6_2atmpS5314 = _M0L6_2atmpS5315 + _M0L6_2atmpS5316;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS5312, _M0L3valS5313, _M0L6_2atmpS5314);
      _M0L3valS5324 = _M0L1jS1788->$0;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1783, _M0L3valS5324)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS5325 = _M0L4varsS1789->$0;
        int32_t _M0L3valS5326 = _M0L1jS1788->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS5329 = _M0L4varsS1789->$0;
        int32_t _M0L3valS5330 = _M0L1jS1788->$0;
        float _M0L6_2atmpS5328;
        float _M0L6_2atmpS5327;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5328
        = _M0MPC15array5Array2atGfE(_M0L4tpreS5329, _M0L3valS5330);
        _M0L6_2atmpS5327 = _M0L6_2atmpS5328 + 0x1p+0f;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS5325, _M0L3valS5326, _M0L6_2atmpS5327);
      }
      _M0L3valS5332 = _M0L1jS1788->$0;
      _M0L6_2atmpS5331 = _M0L3valS5332 + 1;
      _M0L1jS1788->$0 = _M0L6_2atmpS5331;
      continue;
    }
    break;
  }
  _M0L1iS1792
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1792)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1792->$0 = 0;
  while (1) {
    int32_t _M0L3valS5333 = _M0L1iS1792->$0;
    if (_M0L3valS5333 < _M0L7n__postS1784) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS5334 = _M0L4varsS1789->$1;
      int32_t _M0L3valS5335 = _M0L1iS1792->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5347 = _M0L4varsS1789->$1;
      int32_t _M0L3valS5348 = _M0L1iS1792->$0;
      float _M0L6_2atmpS5337;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5345;
      int32_t _M0L3valS5346;
      float _M0L6_2atmpS5342;
      int32_t _M0L3valS5344;
      float _M0L6_2atmpS5343;
      float _M0L6_2atmpS5341;
      float _M0L6_2atmpS5340;
      float _M0L6_2atmpS5339;
      float _M0L6_2atmpS5338;
      float _M0L6_2atmpS5336;
      int32_t _M0L3valS5349;
      int32_t _M0L3valS5357;
      int32_t _M0L6_2atmpS5356;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5337
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5347, _M0L3valS5348);
      _M0L5tpostS5345 = _M0L4varsS1789->$1;
      _M0L3valS5346 = _M0L1iS1792->$0;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5342
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5345, _M0L3valS5346);
      _M0L3valS5344 = _M0L1iS1792->$0;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5343
      = _M0MPC15array5Array2atGfE(_M0L7v__postS1793, _M0L3valS5344);
      _M0L6_2atmpS5341 = _M0L6_2atmpS5342 - _M0L6_2atmpS5343;
      _M0L6_2atmpS5340 = -_M0L6_2atmpS5341;
      _M0L6_2atmpS5339 = _M0L2dtS1790 * _M0L6_2atmpS5340;
      _M0L6_2atmpS5338 = _M0L6_2atmpS5339 * _M0L11inv__tau__yS1786;
      _M0L6_2atmpS5336 = _M0L6_2atmpS5337 + _M0L6_2atmpS5338;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS5334, _M0L3valS5335, _M0L6_2atmpS5336);
      _M0L3valS5349 = _M0L1iS1792->$0;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1785, _M0L3valS5349)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS5350 = _M0L4varsS1789->$1;
        int32_t _M0L3valS5351 = _M0L1iS1792->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS5354 = _M0L4varsS1789->$1;
        int32_t _M0L3valS5355 = _M0L1iS1792->$0;
        float _M0L6_2atmpS5353;
        float _M0L6_2atmpS5352;
        #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5353
        = _M0MPC15array5Array2atGfE(_M0L5tpostS5354, _M0L3valS5355);
        _M0L6_2atmpS5352 = _M0L6_2atmpS5353 + 0x1p+0f;
        #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS5350, _M0L3valS5351, _M0L6_2atmpS5352);
      }
      _M0L3valS5357 = _M0L1iS1792->$0;
      _M0L6_2atmpS5356 = _M0L3valS5357 + 1;
      _M0L1iS1792->$0 = _M0L6_2atmpS5356;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1792);
    }
    break;
  }
  _M0L1jS1788->$0 = 0;
  while (1) {
    int32_t _M0L3valS5358 = _M0L1jS1788->$0;
    if (_M0L3valS5358 < _M0L6n__preS1782) {
      int32_t _M0L3valS5393 = _M0L1jS1788->$0;
      int32_t _M0L5startS1795;
      int32_t _M0L3valS5392;
      int32_t _M0L6_2atmpS5391;
      int32_t _M0L3endS1797;
      int32_t _M0L3valS5390;
      int32_t _M0L10pre__firedS1798;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5388;
      int32_t _M0L3valS5389;
      float _M0L7tpre__jS1799;
      struct _M0TPB8MutLocalGiE* _M0L1sS1800;
      int32_t _M0L3valS5387;
      int32_t _M0L6_2atmpS5386;
      #line 358 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L5startS1795
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1796, _M0L3valS5393);
      _M0L3valS5392 = _M0L1jS1788->$0;
      _M0L6_2atmpS5391 = _M0L3valS5392 + 1;
      #line 359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L3endS1797
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1796, _M0L6_2atmpS5391);
      _M0L3valS5390 = _M0L1jS1788->$0;
      #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L10pre__firedS1798
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1783, _M0L3valS5390);
      _M0L4tpreS5388 = _M0L4varsS1789->$0;
      _M0L3valS5389 = _M0L1jS1788->$0;
      #line 361 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L7tpre__jS1799
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5388, _M0L3valS5389);
      _M0L1sS1800
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1800)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1800->$0 = _M0L5startS1795;
      while (1) {
        int32_t _M0L3valS5359 = _M0L1sS1800->$0;
        if (_M0L3valS5359 < _M0L3endS1797) {
          int32_t _M0L3valS5385 = _M0L1sS1800->$0;
          int32_t _M0L9post__idxS1801;
          int32_t _M0L11post__firedS1803;
          struct _M0TPB5ArrayGfE* _M0L5tpostS5384;
          float _M0L8tpost__iS1804;
          int32_t _M0L3valS5374;
          float _M0L6_2atmpS5372;
          float _M0L6w__minS5373;
          int32_t _M0L3valS5379;
          float _M0L6_2atmpS5377;
          float _M0L6w__maxS5378;
          int32_t _M0L3valS5383;
          int32_t _M0L6_2atmpS5382;
          #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L9post__idxS1801
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1802, _M0L3valS5385);
          #line 365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L11post__firedS1803
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1785, _M0L9post__idxS1801);
          _M0L5tpostS5384 = _M0L4varsS1789->$1;
          #line 366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L8tpost__iS1804
          = _M0MPC15array5Array2atGfE(_M0L5tpostS5384, _M0L9post__idxS1801);
          if (_M0L10pre__firedS1798) {
            float _M0L3etaS5364 = _M0L5paramS1787->$0;
            float _M0L2v0S5366 = _M0L5paramS1787->$1;
            float _M0L6_2atmpS5365 = _M0L8tpost__iS1804 - _M0L2v0S5366;
            float _M0L2dwS1805 = _M0L3etaS5364 * _M0L6_2atmpS5365;
            int32_t _M0L3valS5360 = _M0L1sS1800->$0;
            int32_t _M0L3valS5363 = _M0L1sS1800->$0;
            float _M0L6_2atmpS5362;
            float _M0L6_2atmpS5361;
            #line 369 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5362
            = _M0MPC15array5Array2atGfE(_M0L1wS1806, _M0L3valS5363);
            _M0L6_2atmpS5361 = _M0L6_2atmpS5362 + _M0L2dwS1805;
            #line 369 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1806, _M0L3valS5360, _M0L6_2atmpS5361);
          }
          if (_M0L11post__firedS1803) {
            float _M0L3etaS5371 = _M0L5paramS1787->$0;
            float _M0L2dwS1807 = _M0L3etaS5371 * _M0L7tpre__jS1799;
            int32_t _M0L3valS5367 = _M0L1sS1800->$0;
            int32_t _M0L3valS5370 = _M0L1sS1800->$0;
            float _M0L6_2atmpS5369;
            float _M0L6_2atmpS5368;
            #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5369
            = _M0MPC15array5Array2atGfE(_M0L1wS1806, _M0L3valS5370);
            _M0L6_2atmpS5368 = _M0L6_2atmpS5369 + _M0L2dwS1807;
            #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1806, _M0L3valS5367, _M0L6_2atmpS5368);
          }
          _M0L3valS5374 = _M0L1sS1800->$0;
          #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5372
          = _M0MPC15array5Array2atGfE(_M0L1wS1806, _M0L3valS5374);
          _M0L6w__minS5373 = _M0L5paramS1787->$4;
          if (_M0L6_2atmpS5372 < _M0L6w__minS5373) {
            int32_t _M0L3valS5375 = _M0L1sS1800->$0;
            float _M0L6w__minS5376 = _M0L5paramS1787->$4;
            #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1806, _M0L3valS5375, _M0L6w__minS5376);
          }
          _M0L3valS5379 = _M0L1sS1800->$0;
          #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5377
          = _M0MPC15array5Array2atGfE(_M0L1wS1806, _M0L3valS5379);
          _M0L6w__maxS5378 = _M0L5paramS1787->$3;
          if (_M0L6_2atmpS5377 > _M0L6w__maxS5378) {
            int32_t _M0L3valS5380 = _M0L1sS1800->$0;
            float _M0L6w__maxS5381 = _M0L5paramS1787->$3;
            #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1806, _M0L3valS5380, _M0L6w__maxS5381);
          }
          _M0L3valS5383 = _M0L1sS1800->$0;
          _M0L6_2atmpS5382 = _M0L3valS5383 + 1;
          _M0L1sS1800->$0 = _M0L6_2atmpS5382;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1800);
        }
        break;
      }
      _M0L3valS5387 = _M0L1jS1788->$0;
      _M0L6_2atmpS5386 = _M0L3valS5387 + 1;
      _M0L1jS1788->$0 = _M0L6_2atmpS5386;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1788);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt17istdp__rate__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1777,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1755,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1757,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1773,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1767,
  struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L4varsS1761,
  struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS1759,
  float _M0L6t__nowS1753,
  float _M0L2dtS1762
) {
  int32_t _M0L6n__preS1754;
  int32_t _M0L7n__postS1756;
  float _M0L6tau__yS5310;
  float _M0L11inv__tau__yS1758;
  struct _M0TPB8MutLocalGiE* _M0L1jS1760;
  struct _M0TPB8MutLocalGiE* _M0L1iS1764;
  #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6n__preS1754 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1755);
  #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L7n__postS1756 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1757);
  _M0L6tau__yS5310 = _M0L5paramS1759->$2;
  _M0L11inv__tau__yS1758 = 0x1p+0f / _M0L6tau__yS5310;
  _M0L1jS1760
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1760)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1760->$0 = 0;
  while (1) {
    int32_t _M0L3valS5227 = _M0L1jS1760->$0;
    if (_M0L3valS5227 < _M0L6n__preS1754) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS5228 = _M0L4varsS1761->$0;
      int32_t _M0L3valS5229 = _M0L1jS1760->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5238 = _M0L4varsS1761->$0;
      int32_t _M0L3valS5239 = _M0L1jS1760->$0;
      float _M0L6_2atmpS5231;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5236;
      int32_t _M0L3valS5237;
      float _M0L6_2atmpS5235;
      float _M0L6_2atmpS5234;
      float _M0L6_2atmpS5233;
      float _M0L6_2atmpS5232;
      float _M0L6_2atmpS5230;
      int32_t _M0L3valS5240;
      int32_t _M0L3valS5248;
      int32_t _M0L6_2atmpS5247;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5231
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5238, _M0L3valS5239);
      _M0L4tpreS5236 = _M0L4varsS1761->$0;
      _M0L3valS5237 = _M0L1jS1760->$0;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5235
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5236, _M0L3valS5237);
      _M0L6_2atmpS5234 = -_M0L6_2atmpS5235;
      _M0L6_2atmpS5233 = _M0L2dtS1762 * _M0L6_2atmpS5234;
      _M0L6_2atmpS5232 = _M0L6_2atmpS5233 * _M0L11inv__tau__yS1758;
      _M0L6_2atmpS5230 = _M0L6_2atmpS5231 + _M0L6_2atmpS5232;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS5228, _M0L3valS5229, _M0L6_2atmpS5230);
      _M0L3valS5240 = _M0L1jS1760->$0;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1755, _M0L3valS5240)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS5241 = _M0L4varsS1761->$0;
        int32_t _M0L3valS5242 = _M0L1jS1760->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS5245 = _M0L4varsS1761->$0;
        int32_t _M0L3valS5246 = _M0L1jS1760->$0;
        float _M0L6_2atmpS5244;
        float _M0L6_2atmpS5243;
        #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5244
        = _M0MPC15array5Array2atGfE(_M0L4tpreS5245, _M0L3valS5246);
        _M0L6_2atmpS5243 = _M0L6_2atmpS5244 + 0x1p+0f;
        #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS5241, _M0L3valS5242, _M0L6_2atmpS5243);
      }
      _M0L3valS5248 = _M0L1jS1760->$0;
      _M0L6_2atmpS5247 = _M0L3valS5248 + 1;
      _M0L1jS1760->$0 = _M0L6_2atmpS5247;
      continue;
    }
    break;
  }
  _M0L1iS1764
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1764)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1764->$0 = 0;
  while (1) {
    int32_t _M0L3valS5249 = _M0L1iS1764->$0;
    if (_M0L3valS5249 < _M0L7n__postS1756) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS5250 = _M0L4varsS1761->$1;
      int32_t _M0L3valS5251 = _M0L1iS1764->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5260 = _M0L4varsS1761->$1;
      int32_t _M0L3valS5261 = _M0L1iS1764->$0;
      float _M0L6_2atmpS5253;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5258;
      int32_t _M0L3valS5259;
      float _M0L6_2atmpS5257;
      float _M0L6_2atmpS5256;
      float _M0L6_2atmpS5255;
      float _M0L6_2atmpS5254;
      float _M0L6_2atmpS5252;
      int32_t _M0L3valS5262;
      int32_t _M0L3valS5270;
      int32_t _M0L6_2atmpS5269;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5253
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5260, _M0L3valS5261);
      _M0L5tpostS5258 = _M0L4varsS1761->$1;
      _M0L3valS5259 = _M0L1iS1764->$0;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5257
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5258, _M0L3valS5259);
      _M0L6_2atmpS5256 = -_M0L6_2atmpS5257;
      _M0L6_2atmpS5255 = _M0L2dtS1762 * _M0L6_2atmpS5256;
      _M0L6_2atmpS5254 = _M0L6_2atmpS5255 * _M0L11inv__tau__yS1758;
      _M0L6_2atmpS5252 = _M0L6_2atmpS5253 + _M0L6_2atmpS5254;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS5250, _M0L3valS5251, _M0L6_2atmpS5252);
      _M0L3valS5262 = _M0L1iS1764->$0;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1757, _M0L3valS5262)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS5263 = _M0L4varsS1761->$1;
        int32_t _M0L3valS5264 = _M0L1iS1764->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS5267 = _M0L4varsS1761->$1;
        int32_t _M0L3valS5268 = _M0L1iS1764->$0;
        float _M0L6_2atmpS5266;
        float _M0L6_2atmpS5265;
        #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5266
        = _M0MPC15array5Array2atGfE(_M0L5tpostS5267, _M0L3valS5268);
        _M0L6_2atmpS5265 = _M0L6_2atmpS5266 + 0x1p+0f;
        #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS5263, _M0L3valS5264, _M0L6_2atmpS5265);
      }
      _M0L3valS5270 = _M0L1iS1764->$0;
      _M0L6_2atmpS5269 = _M0L3valS5270 + 1;
      _M0L1iS1764->$0 = _M0L6_2atmpS5269;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1764);
    }
    break;
  }
  _M0L1jS1760->$0 = 0;
  while (1) {
    int32_t _M0L3valS5271 = _M0L1jS1760->$0;
    if (_M0L3valS5271 < _M0L6n__preS1754) {
      int32_t _M0L3valS5309 = _M0L1jS1760->$0;
      int32_t _M0L5startS1766;
      int32_t _M0L3valS5308;
      int32_t _M0L6_2atmpS5307;
      int32_t _M0L3endS1768;
      int32_t _M0L3valS5306;
      int32_t _M0L10pre__firedS1769;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5304;
      int32_t _M0L3valS5305;
      float _M0L7tpre__jS1770;
      struct _M0TPB8MutLocalGiE* _M0L1sS1771;
      int32_t _M0L3valS5303;
      int32_t _M0L6_2atmpS5302;
      #line 179 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L5startS1766
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1767, _M0L3valS5309);
      _M0L3valS5308 = _M0L1jS1760->$0;
      _M0L6_2atmpS5307 = _M0L3valS5308 + 1;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L3endS1768
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1767, _M0L6_2atmpS5307);
      _M0L3valS5306 = _M0L1jS1760->$0;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L10pre__firedS1769
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1755, _M0L3valS5306);
      _M0L4tpreS5304 = _M0L4varsS1761->$0;
      _M0L3valS5305 = _M0L1jS1760->$0;
      #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L7tpre__jS1770
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5304, _M0L3valS5305);
      _M0L1sS1771
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1771)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1771->$0 = _M0L5startS1766;
      while (1) {
        int32_t _M0L3valS5272 = _M0L1sS1771->$0;
        if (_M0L3valS5272 < _M0L3endS1768) {
          int32_t _M0L3valS5301 = _M0L1sS1771->$0;
          int32_t _M0L9post__idxS1772;
          int32_t _M0L11post__firedS1774;
          struct _M0TPB5ArrayGfE* _M0L5tpostS5300;
          float _M0L8tpost__iS1775;
          int32_t _M0L3valS5290;
          float _M0L6_2atmpS5288;
          float _M0L6w__minS5289;
          int32_t _M0L3valS5295;
          float _M0L6_2atmpS5293;
          float _M0L6w__maxS5294;
          int32_t _M0L3valS5299;
          int32_t _M0L6_2atmpS5298;
          #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L9post__idxS1772
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1773, _M0L3valS5301);
          #line 186 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L11post__firedS1774
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1757, _M0L9post__idxS1772);
          _M0L5tpostS5300 = _M0L4varsS1761->$1;
          #line 187 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L8tpost__iS1775
          = _M0MPC15array5Array2atGfE(_M0L5tpostS5300, _M0L9post__idxS1772);
          if (_M0L10pre__firedS1769) {
            float _M0L3etaS5277 = _M0L5paramS1759->$0;
            float _M0L1rS5282 = _M0L5paramS1759->$1;
            float _M0L6_2atmpS5280 = 0x1p+1f * _M0L1rS5282;
            float _M0L6tau__yS5281 = _M0L5paramS1759->$2;
            float _M0L6_2atmpS5279 = _M0L6_2atmpS5280 * _M0L6tau__yS5281;
            float _M0L6_2atmpS5278 = _M0L8tpost__iS1775 - _M0L6_2atmpS5279;
            float _M0L2dwS1776 = _M0L3etaS5277 * _M0L6_2atmpS5278;
            int32_t _M0L3valS5273 = _M0L1sS1771->$0;
            int32_t _M0L3valS5276 = _M0L1sS1771->$0;
            float _M0L6_2atmpS5275;
            float _M0L6_2atmpS5274;
            #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5275
            = _M0MPC15array5Array2atGfE(_M0L1wS1777, _M0L3valS5276);
            _M0L6_2atmpS5274 = _M0L6_2atmpS5275 + _M0L2dwS1776;
            #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1777, _M0L3valS5273, _M0L6_2atmpS5274);
          }
          if (_M0L11post__firedS1774) {
            float _M0L3etaS5287 = _M0L5paramS1759->$0;
            float _M0L2dwS1778 = _M0L3etaS5287 * _M0L7tpre__jS1770;
            int32_t _M0L3valS5283 = _M0L1sS1771->$0;
            int32_t _M0L3valS5286 = _M0L1sS1771->$0;
            float _M0L6_2atmpS5285;
            float _M0L6_2atmpS5284;
            #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5285
            = _M0MPC15array5Array2atGfE(_M0L1wS1777, _M0L3valS5286);
            _M0L6_2atmpS5284 = _M0L6_2atmpS5285 + _M0L2dwS1778;
            #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1777, _M0L3valS5283, _M0L6_2atmpS5284);
          }
          _M0L3valS5290 = _M0L1sS1771->$0;
          #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5288
          = _M0MPC15array5Array2atGfE(_M0L1wS1777, _M0L3valS5290);
          _M0L6w__minS5289 = _M0L5paramS1759->$4;
          if (_M0L6_2atmpS5288 < _M0L6w__minS5289) {
            int32_t _M0L3valS5291 = _M0L1sS1771->$0;
            float _M0L6w__minS5292 = _M0L5paramS1759->$4;
            #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1777, _M0L3valS5291, _M0L6w__minS5292);
          }
          _M0L3valS5295 = _M0L1sS1771->$0;
          #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5293
          = _M0MPC15array5Array2atGfE(_M0L1wS1777, _M0L3valS5295);
          _M0L6w__maxS5294 = _M0L5paramS1759->$3;
          if (_M0L6_2atmpS5293 > _M0L6w__maxS5294) {
            int32_t _M0L3valS5296 = _M0L1sS1771->$0;
            float _M0L6w__maxS5297 = _M0L5paramS1759->$3;
            #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1777, _M0L3valS5296, _M0L6w__maxS5297);
          }
          _M0L3valS5299 = _M0L1sS1771->$0;
          _M0L6_2atmpS5298 = _M0L3valS5299 + 1;
          _M0L1sS1771->$0 = _M0L6_2atmpS5298;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1771);
        }
        break;
      }
      _M0L3valS5303 = _M0L1jS1760->$0;
      _M0L6_2atmpS5302 = _M0L3valS5303 + 1;
      _M0L1jS1760->$0 = _M0L6_2atmpS5302;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1760);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter3new(
  
) {
  float _M0L1cS1751;
  float _M0L2glS1752;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_5936;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS1751 = -0x1p+0f;
  _M0L2glS1752 = -0x1p+0f;
  _block_5936
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_5936)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5936->$0 = _M0L1cS1751;
  _block_5936->$1 = _M0L2glS1752;
  _block_5936->$2 = 0x1.ep+3f;
  _block_5936->$3 = -0x1.9p+5f;
  _block_5936->$4 = -0x1.ep+5f;
  _block_5936->$5 = -0x1.18p+6f;
  _block_5936->$6 = 0x1.eb851eb851eb8p-5f;
  _block_5936->$7 = 0x1p+1f;
  _block_5936->$8 = 0x0p+0f;
  _block_5936->$9 = 0x0p+0f;
  _block_5936->$10 = 0x0p+0f;
  return _block_5936;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS1725,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS1727,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1730
) {
  struct _M0TPB5ArrayGfE* _M0L1vS1724;
  float _M0L2vtS5225;
  float _M0L2vrS5226;
  float _M0L6spreadS1726;
  int32_t _M0L7_2abindS1728;
  int32_t _M0L1kS1729;
  struct _M0TPB5ArrayGfE* _M0L1wS1732;
  struct _M0TPB5ArrayGbE* _M0L4fireS1733;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1734;
  struct _M0TPB5ArrayGfE* _M0L1iS1735;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS1736;
  struct _M0TPB5ArrayGfE* _M0L2geS1737;
  struct _M0TPB5ArrayGfE* _M0L2giS1738;
  struct _M0TPB5ArrayGfE* _M0L2heS1739;
  struct _M0TPB5ArrayGfE* _M0L2hiS1740;
  struct _M0TPB5ArrayGfE* _M0L3gluS1741;
  struct _M0TPB5ArrayGfE* _M0L4gabaS1742;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1743;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1744;
  float _M0L4e__eS1745;
  float _M0L4e__iS1746;
  float _M0L3treS1747;
  float _M0L3tdeS1748;
  float _M0L3triS1749;
  float _M0L3tdiS1750;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS5224;
  struct _M0TP26RiantR8snn__mbt2IF* _block_5938;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS1724 = _M0MPC15array5Array4makeGfE(_M0L1nS1725, 0x0p+0f);
  _M0L2vtS5225 = _M0L5paramS1727->$3;
  _M0L2vrS5226 = _M0L5paramS1727->$4;
  _M0L6spreadS1726 = _M0L2vtS5225 - _M0L2vrS5226;
  _M0L7_2abindS1728 = 0;
  _M0L1kS1729 = _M0L7_2abindS1728;
  while (1) {
    if (_M0L1kS1729 < _M0L1nS1725) {
      float _M0L2vrS5220 = _M0L5paramS1727->$4;
      float _M0L6_2atmpS5222;
      float _M0L6_2atmpS5221;
      float _M0L6_2atmpS5219;
      int32_t _M0L6_2atmpS5223;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS5222 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1730);
      _M0L6_2atmpS5221 = _M0L6_2atmpS5222 * _M0L6spreadS1726;
      _M0L6_2atmpS5219 = _M0L2vrS5220 + _M0L6_2atmpS5221;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1724, _M0L1kS1729, _M0L6_2atmpS5219);
      _M0L6_2atmpS5223 = _M0L1kS1729 + 1;
      _M0L1kS1729 = _M0L6_2atmpS5223;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS1732 = _M0MPC15array5Array4makeGfE(_M0L1nS1725, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS1733 = _M0MPC15array5Array4makeGbE(_M0L1nS1725, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS1734 = _M0MPC15array5Array4makeGiE(_M0L1nS1725, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS1735 = _M0MPC15array5Array4makeGfE(_M0L1nS1725, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS1736 = _M0MPC15array5Array4makeGfE(_M0L1nS1725, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS1737 = _M0MPC15array5Array4makeGfE(_M0L1nS1725, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS1738 = _M0MPC15array5Array4makeGfE(_M0L1nS1725, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS1739 = _M0MPC15array5Array4makeGfE(_M0L1nS1725, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS1740 = _M0MPC15array5Array4makeGfE(_M0L1nS1725, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS1741 = _M0MPC15array5Array4makeGfE(_M0L1nS1725, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS1742 = _M0MPC15array5Array4makeGfE(_M0L1nS1725, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS1743 = _M0MPC15array5Array4makeGfE(_M0L1nS1725, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS1744 = _M0MPC15array5Array4makeGfE(_M0L1nS1725, 0x1p+0f);
  _M0L4e__eS1745 = 0x0p+0f;
  _M0L4e__iS1746 = -0x1.2cp+6f;
  _M0L3treS1747 = 0x1p+0f;
  _M0L3tdeS1748 = 0x1.8p+2f;
  _M0L3triS1749 = 0x1p-1f;
  _M0L3tdiS1750 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS5224 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS1727);
  _block_5938
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_5938)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
  _block_5938->$0 = _M0L5paramS1727;
  _block_5938->$1 = _M0L6_2atmpS5224;
  _block_5938->$2 = _M0L1nS1725;
  _block_5938->$3 = _M0L1vS1724;
  _block_5938->$4 = _M0L1wS1732;
  _block_5938->$5 = _M0L4fireS1733;
  _block_5938->$6 = _M0L4tabsS1734;
  _block_5938->$7 = _M0L1iS1735;
  _block_5938->$8 = _M0L9syn__currS1736;
  _block_5938->$9 = _M0L2geS1737;
  _block_5938->$10 = _M0L2giS1738;
  _block_5938->$11 = _M0L2heS1739;
  _block_5938->$12 = _M0L2hiS1740;
  _block_5938->$13 = _M0L3gluS1741;
  _block_5938->$14 = _M0L4gabaS1742;
  _block_5938->$15 = _M0L7gsyn__eS1743;
  _block_5938->$16 = _M0L7gsyn__iS1744;
  _block_5938->$17 = _M0L4e__eS1745;
  _block_5938->$18 = _M0L4e__iS1746;
  _block_5938->$19 = _M0L3treS1747;
  _block_5938->$20 = _M0L3tdeS1748;
  _block_5938->$21 = _M0L3triS1749;
  _block_5938->$22 = _M0L3tdiS1750;
  return _block_5938;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_5939;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_5939
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_5939)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5939->$0 = 0x1p+1f;
  return _block_5939;
}

int32_t _M0FP26RiantR8snn__mbt14integrate__any(
  void* _M0L1pS1705,
  float _M0L2dtS1688
) {
  struct _M0TP26RiantR8snn__mbt6HetRec* _M0L1xS1687;
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L1xS1690;
  struct _M0TP26RiantR8snn__mbt7Poisson* _M0L1xS1692;
  struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L1xS1694;
  struct _M0TP26RiantR8snn__mbt2HH* _M0L1xS1696;
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1xS1698;
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1xS1700;
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1xS1702;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1xS1704;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  switch (Moonbit_object_tag(_M0L1pS1705)) {
    case 0: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__* _M0L7_2aIF__S1706 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__*)_M0L1pS1705;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4_2axS1707 =
        _M0L7_2aIF__S1706->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1707);
      _M0L1xS1704 = _M0L4_2axS1707;
      goto join_1703;
      break;
    }
    
    case 1: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop6AdEx__* _M0L9_2aAdEx__S1708 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop6AdEx__*)_M0L1pS1705;
      struct _M0TP26RiantR8snn__mbt4AdEx* _M0L4_2axS1709 =
        _M0L9_2aAdEx__S1708->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1709);
      _M0L1xS1702 = _M0L4_2axS1709;
      goto join_1701;
      break;
    }
    
    case 2: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop12AdExSinExp__* _M0L15_2aAdExSinExp__S1710 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop12AdExSinExp__*)_M0L1pS1705;
      struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L4_2axS1711 =
        _M0L15_2aAdExSinExp__S1710->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1711);
      _M0L1xS1700 = _M0L4_2axS1711;
      goto join_1699;
      break;
    }
    
    case 3: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4IZ__* _M0L7_2aIZ__S1712 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4IZ__*)_M0L1pS1705;
      struct _M0TP26RiantR8snn__mbt2IZ* _M0L4_2axS1713 =
        _M0L7_2aIZ__S1712->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1713);
      _M0L1xS1698 = _M0L4_2axS1713;
      goto join_1697;
      break;
    }
    
    case 4: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__* _M0L7_2aHH__S1714 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__*)_M0L1pS1705;
      struct _M0TP26RiantR8snn__mbt2HH* _M0L4_2axS1715 =
        _M0L7_2aHH__S1714->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1715);
      _M0L1xS1696 = _M0L4_2axS1715;
      goto join_1695;
      break;
    }
    
    case 5: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__* _M0L7_2aML__S1716 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__*)_M0L1pS1705;
      struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L4_2axS1717 =
        _M0L7_2aML__S1716->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1717);
      _M0L1xS1694 = _M0L4_2axS1717;
      goto join_1693;
      break;
    }
    
    case 6: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__* _M0L12_2aPoisson__S1718 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__*)_M0L1pS1705;
      struct _M0TP26RiantR8snn__mbt7Poisson* _M0L4_2axS1719 =
        _M0L12_2aPoisson__S1718->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1719);
      _M0L1xS1692 = _M0L4_2axS1719;
      goto join_1691;
      break;
    }
    
    case 7: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4WC__* _M0L7_2aWC__S1720 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4WC__*)_M0L1pS1705;
      struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L4_2axS1721 =
        _M0L7_2aWC__S1720->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1721);
      _M0L1xS1690 = _M0L4_2axS1721;
      goto join_1689;
      break;
    }
    default: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__* _M0L11_2aHetRec__S1722 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__*)_M0L1pS1705;
      struct _M0TP26RiantR8snn__mbt6HetRec* _M0L4_2axS1723 =
        _M0L11_2aHetRec__S1722->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1723);
      _M0L1xS1687 = _M0L4_2axS1723;
      goto join_1686;
      break;
    }
  }
  goto joinlet_5948;
  join_1703:;
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt14step__synapses(_M0L1xS1704, _M0L2dtS1688);
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt17synaptic__current(_M0L1xS1704);
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt12step__neuron(_M0L1xS1704, _M0L2dtS1688);
  moonbit_decref_cycle_free(_M0L1xS1704);
  joinlet_5948:;
  goto joinlet_5947;
  join_1701:;
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt20adex__step__synapses(_M0L1xS1702, _M0L2dtS1688);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt23adex__synaptic__current(_M0L1xS1702);
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt10step__adex(_M0L1xS1702, _M0L2dtS1688);
  moonbit_decref_cycle_free(_M0L1xS1702);
  joinlet_5947:;
  goto joinlet_5946;
  join_1699:;
  #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(_M0L1xS1700, _M0L2dtS1688);
  #line 44 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(_M0L1xS1700);
  #line 45 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt18step__adex__sinexp(_M0L1xS1700, _M0L2dtS1688);
  moonbit_decref_cycle_free(_M0L1xS1700);
  joinlet_5946:;
  goto joinlet_5945;
  join_1697:;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__iz(_M0L1xS1698, _M0L2dtS1688);
  moonbit_decref_cycle_free(_M0L1xS1698);
  joinlet_5945:;
  goto joinlet_5944;
  join_1695:;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__hh(_M0L1xS1696, _M0L2dtS1688);
  moonbit_decref_cycle_free(_M0L1xS1696);
  joinlet_5944:;
  goto joinlet_5943;
  join_1693:;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__ml(_M0L1xS1694, _M0L2dtS1688);
  moonbit_decref_cycle_free(_M0L1xS1694);
  joinlet_5943:;
  goto joinlet_5942;
  join_1691:;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt13step__poisson(_M0L1xS1692, _M0L2dtS1688);
  moonbit_decref_cycle_free(_M0L1xS1692);
  joinlet_5942:;
  goto joinlet_5941;
  join_1689:;
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__wc(_M0L1xS1690, _M0L2dtS1688);
  moonbit_decref_cycle_free(_M0L1xS1690);
  joinlet_5941:;
  goto joinlet_5940;
  join_1686:;
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt12step__hetrec(_M0L1xS1687, _M0L2dtS1688);
  moonbit_decref_cycle_free(_M0L1xS1687);
  joinlet_5940:;
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__wc(
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L1pS1681,
  float _M0L2dtS1684
) {
  int32_t _M0L1nS1680;
  int32_t _M0L7_2abindS1682;
  int32_t _M0L1kS1683;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _M0L1nS1680 = _M0L1pS1681->$1;
  _M0L7_2abindS1682 = 0;
  _M0L1kS1683 = _M0L7_2abindS1682;
  while (1) {
    if (_M0L1kS1683 < _M0L1nS1680) {
      struct _M0TPB5ArrayGfE* _M0L1xS5199 = _M0L1pS1681->$2;
      struct _M0TPB5ArrayGfE* _M0L1xS5212 = _M0L1pS1681->$2;
      float _M0L6_2atmpS5201;
      struct _M0TPB5ArrayGfE* _M0L1xS5211;
      float _M0L6_2atmpS5210;
      float _M0L6_2atmpS5207;
      struct _M0TPB5ArrayGfE* _M0L1gS5209;
      float _M0L6_2atmpS5208;
      float _M0L6_2atmpS5204;
      struct _M0TPB5ArrayGfE* _M0L1iS5206;
      float _M0L6_2atmpS5205;
      float _M0L6_2atmpS5203;
      float _M0L6_2atmpS5202;
      float _M0L6_2atmpS5200;
      struct _M0TPB5ArrayGfE* _M0L1rS5213;
      struct _M0TPB5ArrayGfE* _M0L1xS5216;
      float _M0L6_2atmpS5215;
      float _M0L6_2atmpS5214;
      struct _M0TPB5ArrayGfE* _M0L1gS5217;
      int32_t _M0L6_2atmpS5218;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5201 = _M0MPC15array5Array2atGfE(_M0L1xS5212, _M0L1kS1683);
      _M0L1xS5211 = _M0L1pS1681->$2;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5210 = _M0MPC15array5Array2atGfE(_M0L1xS5211, _M0L1kS1683);
      _M0L6_2atmpS5207 = -_M0L6_2atmpS5210;
      _M0L1gS5209 = _M0L1pS1681->$4;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5208 = _M0MPC15array5Array2atGfE(_M0L1gS5209, _M0L1kS1683);
      _M0L6_2atmpS5204 = _M0L6_2atmpS5207 + _M0L6_2atmpS5208;
      _M0L1iS5206 = _M0L1pS1681->$5;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5205 = _M0MPC15array5Array2atGfE(_M0L1iS5206, _M0L1kS1683);
      _M0L6_2atmpS5203 = _M0L6_2atmpS5204 + _M0L6_2atmpS5205;
      _M0L6_2atmpS5202 = _M0L2dtS1684 * _M0L6_2atmpS5203;
      _M0L6_2atmpS5200 = _M0L6_2atmpS5201 + _M0L6_2atmpS5202;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS5199, _M0L1kS1683, _M0L6_2atmpS5200);
      _M0L1rS5213 = _M0L1pS1681->$3;
      _M0L1xS5216 = _M0L1pS1681->$2;
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5215 = _M0MPC15array5Array2atGfE(_M0L1xS5216, _M0L1kS1683);
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5214 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS5215);
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1rS5213, _M0L1kS1683, _M0L6_2atmpS5214);
      _M0L1gS5217 = _M0L1pS1681->$4;
      #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS5217, _M0L1kS1683, 0x0p+0f);
      _M0L6_2atmpS5218 = _M0L1kS1683 + 1;
      _M0L1kS1683 = _M0L6_2atmpS5218;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13step__poisson(
  struct _M0TP26RiantR8snn__mbt7Poisson* _M0L1pS1673,
  float _M0L2dtS1675
) {
  int32_t _M0L1nS1672;
  struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter* _M0L5paramS5198;
  float _M0L4rateS5197;
  float _M0L8rate__dtS1674;
  int32_t _M0L7_2abindS1676;
  int32_t _M0L1iS1677;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
  _M0L1nS1672 = _M0L1pS1673->$1;
  _M0L5paramS5198 = _M0L1pS1673->$0;
  _M0L4rateS5197 = _M0L5paramS5198->$0;
  _M0L8rate__dtS1674 = _M0L4rateS5197 * _M0L2dtS1675;
  _M0L7_2abindS1676 = 0;
  _M0L1iS1677 = _M0L7_2abindS1676;
  while (1) {
    if (_M0L1iS1677 < _M0L1nS1672) {
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS5195 = _M0L1pS1673->$4;
      float _M0L1uS1678;
      struct _M0TPB5ArrayGfE* _M0L9randcacheS5192;
      struct _M0TPB5ArrayGbE* _M0L4fireS5193;
      int32_t _M0L6_2atmpS5194;
      int32_t _M0L6_2atmpS5196;
      #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0L1uS1678 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS5195);
      _M0L9randcacheS5192 = _M0L1pS1673->$3;
      #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0MPC15array5Array3setGfE(_M0L9randcacheS5192, _M0L1iS1677, _M0L1uS1678);
      _M0L4fireS5193 = _M0L1pS1673->$2;
      _M0L6_2atmpS5194 = _M0L1uS1678 < _M0L8rate__dtS1674;
      #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS5193, _M0L1iS1677, _M0L6_2atmpS5194);
      _M0L6_2atmpS5196 = _M0L1iS1677 + 1;
      _M0L1iS1677 = _M0L6_2atmpS5196;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__ml(
  struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L1pS1632,
  float _M0L2dtS1661
) {
  int32_t _M0L1nS1631;
  struct _M0TP26RiantR8snn__mbt20MorrisLecarParameter* _M0L3p__S1633;
  float _M0L2cmS1634;
  float _M0L2elS1635;
  float _M0L2ekS1636;
  float _M0L3ecaS1637;
  float _M0L2glS1638;
  float _M0L2gkS1639;
  float _M0L3gcaS1640;
  float _M0L6tau__eS1641;
  float _M0L6tau__iS1642;
  float _M0L2v1S1643;
  float _M0L2v2S1644;
  float _M0L2v3S1645;
  float _M0L2v4S1646;
  float _M0L3phiS1647;
  float _M0L4e__eS1648;
  float _M0L4e__iS1649;
  int32_t _M0L7_2abindS1650;
  int32_t _M0L1iS1651;
  int32_t _M0L7_2abindS1663;
  int32_t _M0L1iS1664;
  int32_t _M0L7_2abindS1666;
  int32_t _M0L1iS1667;
  int32_t _M0L7_2abindS1669;
  int32_t _M0L1iS1670;
  #line 86 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
  _M0L1nS1631 = _M0L1pS1632->$1;
  _M0L3p__S1633 = _M0L1pS1632->$0;
  _M0L2cmS1634 = _M0L3p__S1633->$0;
  _M0L2elS1635 = _M0L3p__S1633->$1;
  _M0L2ekS1636 = _M0L3p__S1633->$2;
  _M0L3ecaS1637 = _M0L3p__S1633->$3;
  _M0L2glS1638 = _M0L3p__S1633->$4;
  _M0L2gkS1639 = _M0L3p__S1633->$5;
  _M0L3gcaS1640 = _M0L3p__S1633->$6;
  _M0L6tau__eS1641 = _M0L3p__S1633->$7;
  _M0L6tau__iS1642 = _M0L3p__S1633->$8;
  _M0L2v1S1643 = _M0L3p__S1633->$9;
  _M0L2v2S1644 = _M0L3p__S1633->$10;
  _M0L2v3S1645 = _M0L3p__S1633->$11;
  _M0L2v4S1646 = _M0L3p__S1633->$12;
  _M0L3phiS1647 = _M0L3p__S1633->$13;
  _M0L4e__eS1648 = _M0L3p__S1633->$14;
  _M0L4e__iS1649 = _M0L3p__S1633->$15;
  _M0L7_2abindS1650 = 0;
  _M0L1iS1651 = _M0L7_2abindS1650;
  while (1) {
    if (_M0L1iS1651 < _M0L1nS1631) {
      struct _M0TPB5ArrayGfE* _M0L1vS5146 = _M0L1pS1632->$2;
      float _M0L1vS1652;
      struct _M0TPB5ArrayGfE* _M0L1wS5145;
      float _M0L1wS1653;
      float _M0L6_2atmpS5144;
      float _M0L6_2atmpS5143;
      float _M0L6_2atmpS5142;
      float _M0L6_2atmpS5141;
      float _M0L5m__ssS1654;
      struct _M0TPB5ArrayGfE* _M0L1iS5140;
      float _M0L6_2atmpS5137;
      float _M0L6_2atmpS5139;
      float _M0L6_2atmpS5138;
      float _M0L6_2atmpS5133;
      float _M0L6_2atmpS5136;
      float _M0L6_2atmpS5135;
      float _M0L6_2atmpS5134;
      float _M0L6_2atmpS5129;
      float _M0L6_2atmpS5132;
      float _M0L6_2atmpS5131;
      float _M0L6_2atmpS5130;
      float _M0L2dvS1655;
      float _M0L6_2atmpS5128;
      float _M0L6_2atmpS5127;
      float _M0L6_2atmpS5126;
      float _M0L6_2atmpS5125;
      float _M0L5n__ssS1656;
      float _M0L6_2atmpS5123;
      float _M0L6_2atmpS5124;
      float _M0L9cosh__argS1657;
      float _M0L6_2atmpS5120;
      float _M0L6_2atmpS5122;
      float _M0L6_2atmpS5121;
      float _M0L6_2atmpS5119;
      float _M0L9cosh__valS1658;
      float _M0L6_2atmpS5117;
      float _M0L3tauS1659;
      float _M0L6_2atmpS5116;
      float _M0L2dwS1660;
      struct _M0TPB5ArrayGfE* _M0L1vS5109;
      float _M0L6_2atmpS5112;
      float _M0L6_2atmpS5111;
      float _M0L6_2atmpS5110;
      struct _M0TPB5ArrayGfE* _M0L1wS5113;
      float _M0L6_2atmpS5115;
      float _M0L6_2atmpS5114;
      int32_t _M0L6_2atmpS5147;
      #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L1vS1652 = _M0MPC15array5Array2atGfE(_M0L1vS5146, _M0L1iS1651);
      _M0L1wS5145 = _M0L1pS1632->$3;
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L1wS1653 = _M0MPC15array5Array2atGfE(_M0L1wS5145, _M0L1iS1651);
      _M0L6_2atmpS5144 = _M0L1vS1652 - _M0L2v1S1643;
      _M0L6_2atmpS5143 = _M0L6_2atmpS5144 / _M0L2v2S1644;
      #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5142 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS5143);
      _M0L6_2atmpS5141 = 0x1p+0f + _M0L6_2atmpS5142;
      _M0L5m__ssS1654 = 0x1p-1f * _M0L6_2atmpS5141;
      _M0L1iS5140 = _M0L1pS1632->$5;
      #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5137 = _M0MPC15array5Array2atGfE(_M0L1iS5140, _M0L1iS1651);
      _M0L6_2atmpS5139 = _M0L2elS1635 - _M0L1vS1652;
      _M0L6_2atmpS5138 = _M0L2glS1638 * _M0L6_2atmpS5139;
      _M0L6_2atmpS5133 = _M0L6_2atmpS5137 + _M0L6_2atmpS5138;
      _M0L6_2atmpS5136 = _M0L3ecaS1637 - _M0L1vS1652;
      _M0L6_2atmpS5135 = _M0L3gcaS1640 * _M0L6_2atmpS5136;
      _M0L6_2atmpS5134 = _M0L6_2atmpS5135 * _M0L5m__ssS1654;
      _M0L6_2atmpS5129 = _M0L6_2atmpS5133 + _M0L6_2atmpS5134;
      _M0L6_2atmpS5132 = _M0L2ekS1636 - _M0L1vS1652;
      _M0L6_2atmpS5131 = _M0L2gkS1639 * _M0L6_2atmpS5132;
      _M0L6_2atmpS5130 = _M0L6_2atmpS5131 * _M0L1wS1653;
      _M0L2dvS1655 = _M0L6_2atmpS5129 + _M0L6_2atmpS5130;
      _M0L6_2atmpS5128 = _M0L1vS1652 - _M0L2v3S1645;
      _M0L6_2atmpS5127 = _M0L6_2atmpS5128 / _M0L2v4S1646;
      #line 112 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5126 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS5127);
      _M0L6_2atmpS5125 = 0x1p+0f + _M0L6_2atmpS5126;
      _M0L5n__ssS1656 = 0x1p-1f * _M0L6_2atmpS5125;
      _M0L6_2atmpS5123 = _M0L1vS1652 - _M0L2v3S1645;
      _M0L6_2atmpS5124 = 0x1p+1f * _M0L2v4S1646;
      _M0L9cosh__argS1657 = _M0L6_2atmpS5123 / _M0L6_2atmpS5124;
      #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5120 = _M0FP26RiantR8snn__mbt4expf(_M0L9cosh__argS1657);
      _M0L6_2atmpS5122 = -_M0L9cosh__argS1657;
      #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5121 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS5122);
      _M0L6_2atmpS5119 = _M0L6_2atmpS5120 + _M0L6_2atmpS5121;
      _M0L9cosh__valS1658 = 0x1p-1f * _M0L6_2atmpS5119;
      _M0L6_2atmpS5117 = _M0L3phiS1647 * _M0L9cosh__valS1658;
      if (_M0L6_2atmpS5117 != 0x0p+0f) {
        float _M0L6_2atmpS5118 = _M0L3phiS1647 * _M0L9cosh__valS1658;
        _M0L3tauS1659 = 0x1p+0f / _M0L6_2atmpS5118;
      } else {
        _M0L3tauS1659 = 0x0p+0f;
      }
      _M0L6_2atmpS5116 = _M0L5n__ssS1656 - _M0L1wS1653;
      _M0L2dwS1660 = _M0L6_2atmpS5116 / _M0L3tauS1659;
      _M0L1vS5109 = _M0L1pS1632->$2;
      _M0L6_2atmpS5112 = _M0L2dtS1661 / _M0L2cmS1634;
      _M0L6_2atmpS5111 = _M0L6_2atmpS5112 * _M0L2dvS1655;
      _M0L6_2atmpS5110 = _M0L1vS1652 + _M0L6_2atmpS5111;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5109, _M0L1iS1651, _M0L6_2atmpS5110);
      _M0L1wS5113 = _M0L1pS1632->$3;
      _M0L6_2atmpS5115 = _M0L2dtS1661 * _M0L2dwS1660;
      _M0L6_2atmpS5114 = _M0L1wS1653 + _M0L6_2atmpS5115;
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS5113, _M0L1iS1651, _M0L6_2atmpS5114);
      _M0L6_2atmpS5147 = _M0L1iS1651 + 1;
      _M0L1iS1651 = _M0L6_2atmpS5147;
      continue;
    }
    break;
  }
  _M0L7_2abindS1663 = 0;
  _M0L1iS1664 = _M0L7_2abindS1663;
  while (1) {
    if (_M0L1iS1664 < _M0L1nS1631) {
      struct _M0TPB5ArrayGfE* _M0L1vS5148 = _M0L1pS1632->$2;
      struct _M0TPB5ArrayGfE* _M0L1vS5166 = _M0L1pS1632->$2;
      float _M0L6_2atmpS5150;
      float _M0L6_2atmpS5152;
      struct _M0TPB5ArrayGfE* _M0L2geS5165;
      float _M0L6_2atmpS5161;
      struct _M0TPB5ArrayGfE* _M0L1vS5164;
      float _M0L6_2atmpS5163;
      float _M0L6_2atmpS5162;
      float _M0L6_2atmpS5154;
      struct _M0TPB5ArrayGfE* _M0L2giS5160;
      float _M0L6_2atmpS5156;
      struct _M0TPB5ArrayGfE* _M0L1vS5159;
      float _M0L6_2atmpS5158;
      float _M0L6_2atmpS5157;
      float _M0L6_2atmpS5155;
      float _M0L6_2atmpS5153;
      float _M0L6_2atmpS5151;
      float _M0L6_2atmpS5149;
      int32_t _M0L6_2atmpS5167;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5150 = _M0MPC15array5Array2atGfE(_M0L1vS5166, _M0L1iS1664);
      _M0L6_2atmpS5152 = _M0L2dtS1661 / _M0L2cmS1634;
      _M0L2geS5165 = _M0L1pS1632->$6;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5161 = _M0MPC15array5Array2atGfE(_M0L2geS5165, _M0L1iS1664);
      _M0L1vS5164 = _M0L1pS1632->$2;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5163 = _M0MPC15array5Array2atGfE(_M0L1vS5164, _M0L1iS1664);
      _M0L6_2atmpS5162 = _M0L4e__eS1648 - _M0L6_2atmpS5163;
      _M0L6_2atmpS5154 = _M0L6_2atmpS5161 * _M0L6_2atmpS5162;
      _M0L2giS5160 = _M0L1pS1632->$7;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5156 = _M0MPC15array5Array2atGfE(_M0L2giS5160, _M0L1iS1664);
      _M0L1vS5159 = _M0L1pS1632->$2;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5158 = _M0MPC15array5Array2atGfE(_M0L1vS5159, _M0L1iS1664);
      _M0L6_2atmpS5157 = _M0L4e__iS1649 - _M0L6_2atmpS5158;
      _M0L6_2atmpS5155 = _M0L6_2atmpS5156 * _M0L6_2atmpS5157;
      _M0L6_2atmpS5153 = _M0L6_2atmpS5154 + _M0L6_2atmpS5155;
      _M0L6_2atmpS5151 = _M0L6_2atmpS5152 * _M0L6_2atmpS5153;
      _M0L6_2atmpS5149 = _M0L6_2atmpS5150 + _M0L6_2atmpS5151;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5148, _M0L1iS1664, _M0L6_2atmpS5149);
      _M0L6_2atmpS5167 = _M0L1iS1664 + 1;
      _M0L1iS1664 = _M0L6_2atmpS5167;
      continue;
    }
    break;
  }
  _M0L7_2abindS1666 = 0;
  _M0L1iS1667 = _M0L7_2abindS1666;
  while (1) {
    if (_M0L1iS1667 < _M0L1nS1631) {
      struct _M0TPB5ArrayGfE* _M0L2geS5168 = _M0L1pS1632->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS5176 = _M0L1pS1632->$6;
      float _M0L6_2atmpS5170;
      struct _M0TPB5ArrayGfE* _M0L2geS5175;
      float _M0L6_2atmpS5174;
      float _M0L6_2atmpS5173;
      float _M0L6_2atmpS5172;
      float _M0L6_2atmpS5171;
      float _M0L6_2atmpS5169;
      struct _M0TPB5ArrayGfE* _M0L2giS5177;
      struct _M0TPB5ArrayGfE* _M0L2giS5185;
      float _M0L6_2atmpS5179;
      struct _M0TPB5ArrayGfE* _M0L2giS5184;
      float _M0L6_2atmpS5183;
      float _M0L6_2atmpS5182;
      float _M0L6_2atmpS5181;
      float _M0L6_2atmpS5180;
      float _M0L6_2atmpS5178;
      int32_t _M0L6_2atmpS5186;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5170 = _M0MPC15array5Array2atGfE(_M0L2geS5176, _M0L1iS1667);
      _M0L2geS5175 = _M0L1pS1632->$6;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5174 = _M0MPC15array5Array2atGfE(_M0L2geS5175, _M0L1iS1667);
      _M0L6_2atmpS5173 = -_M0L6_2atmpS5174;
      _M0L6_2atmpS5172 = _M0L6_2atmpS5173 / _M0L6tau__eS1641;
      _M0L6_2atmpS5171 = _M0L2dtS1661 * _M0L6_2atmpS5172;
      _M0L6_2atmpS5169 = _M0L6_2atmpS5170 + _M0L6_2atmpS5171;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS5168, _M0L1iS1667, _M0L6_2atmpS5169);
      _M0L2giS5177 = _M0L1pS1632->$7;
      _M0L2giS5185 = _M0L1pS1632->$7;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5179 = _M0MPC15array5Array2atGfE(_M0L2giS5185, _M0L1iS1667);
      _M0L2giS5184 = _M0L1pS1632->$7;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5183 = _M0MPC15array5Array2atGfE(_M0L2giS5184, _M0L1iS1667);
      _M0L6_2atmpS5182 = -_M0L6_2atmpS5183;
      _M0L6_2atmpS5181 = _M0L6_2atmpS5182 / _M0L6tau__iS1642;
      _M0L6_2atmpS5180 = _M0L2dtS1661 * _M0L6_2atmpS5181;
      _M0L6_2atmpS5178 = _M0L6_2atmpS5179 + _M0L6_2atmpS5180;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS5177, _M0L1iS1667, _M0L6_2atmpS5178);
      _M0L6_2atmpS5186 = _M0L1iS1667 + 1;
      _M0L1iS1667 = _M0L6_2atmpS5186;
      continue;
    }
    break;
  }
  _M0L7_2abindS1669 = 0;
  _M0L1iS1670 = _M0L7_2abindS1669;
  while (1) {
    if (_M0L1iS1670 < _M0L1nS1631) {
      struct _M0TPB5ArrayGbE* _M0L4fireS5187 = _M0L1pS1632->$4;
      struct _M0TPB5ArrayGfE* _M0L1vS5190 = _M0L1pS1632->$2;
      float _M0L6_2atmpS5189;
      int32_t _M0L6_2atmpS5188;
      int32_t _M0L6_2atmpS5191;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5189 = _M0MPC15array5Array2atGfE(_M0L1vS5190, _M0L1iS1670);
      _M0L6_2atmpS5188 = _M0L6_2atmpS5189 > 0x1.4p+4f;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS5187, _M0L1iS1670, _M0L6_2atmpS5188);
      _M0L6_2atmpS5191 = _M0L1iS1670 + 1;
      _M0L1iS1670 = _M0L6_2atmpS5191;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__iz(
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1pS1600,
  float _M0L2dtS1612
) {
  int32_t _M0L1nS1599;
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L3p__S1601;
  float _M0L1aS1602;
  float _M0L1bS1603;
  float _M0L1cS1604;
  float _M0L1dS1605;
  float _M0L6tau__eS1606;
  float _M0L6tau__iS1607;
  float _M0L4e__eS1608;
  float _M0L4e__iS1609;
  int32_t _M0L7_2abindS1610;
  int32_t _M0L1iS1611;
  int32_t _M0L7_2abindS1614;
  int32_t _M0L1iS1615;
  int32_t _M0L7_2abindS1621;
  int32_t _M0L1iS1622;
  int32_t _M0L7_2abindS1625;
  int32_t _M0L1iS1626;
  int32_t _M0L7_2abindS1628;
  int32_t _M0L1iS1629;
  #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1nS1599 = _M0L1pS1600->$1;
  _M0L3p__S1601 = _M0L1pS1600->$0;
  _M0L1aS1602 = _M0L3p__S1601->$0;
  _M0L1bS1603 = _M0L3p__S1601->$1;
  _M0L1cS1604 = _M0L3p__S1601->$2;
  _M0L1dS1605 = _M0L3p__S1601->$3;
  _M0L6tau__eS1606 = _M0L3p__S1601->$4;
  _M0L6tau__iS1607 = _M0L3p__S1601->$5;
  _M0L4e__eS1608 = _M0L3p__S1601->$6;
  _M0L4e__iS1609 = _M0L3p__S1601->$7;
  _M0L7_2abindS1610 = 0;
  _M0L1iS1611 = _M0L7_2abindS1610;
  while (1) {
    if (_M0L1iS1611 < _M0L1nS1599) {
      struct _M0TPB5ArrayGfE* _M0L2geS5017 = _M0L1pS1600->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS5025 = _M0L1pS1600->$6;
      float _M0L6_2atmpS5019;
      struct _M0TPB5ArrayGfE* _M0L2geS5024;
      float _M0L6_2atmpS5023;
      float _M0L6_2atmpS5022;
      float _M0L6_2atmpS5021;
      float _M0L6_2atmpS5020;
      float _M0L6_2atmpS5018;
      struct _M0TPB5ArrayGfE* _M0L2giS5026;
      struct _M0TPB5ArrayGfE* _M0L2giS5034;
      float _M0L6_2atmpS5028;
      struct _M0TPB5ArrayGfE* _M0L2giS5033;
      float _M0L6_2atmpS5032;
      float _M0L6_2atmpS5031;
      float _M0L6_2atmpS5030;
      float _M0L6_2atmpS5029;
      float _M0L6_2atmpS5027;
      int32_t _M0L6_2atmpS5035;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5019 = _M0MPC15array5Array2atGfE(_M0L2geS5025, _M0L1iS1611);
      _M0L2geS5024 = _M0L1pS1600->$6;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5023 = _M0MPC15array5Array2atGfE(_M0L2geS5024, _M0L1iS1611);
      _M0L6_2atmpS5022 = -_M0L6_2atmpS5023;
      _M0L6_2atmpS5021 = _M0L2dtS1612 * _M0L6_2atmpS5022;
      _M0L6_2atmpS5020 = _M0L6_2atmpS5021 / _M0L6tau__eS1606;
      _M0L6_2atmpS5018 = _M0L6_2atmpS5019 + _M0L6_2atmpS5020;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS5017, _M0L1iS1611, _M0L6_2atmpS5018);
      _M0L2giS5026 = _M0L1pS1600->$7;
      _M0L2giS5034 = _M0L1pS1600->$7;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5028 = _M0MPC15array5Array2atGfE(_M0L2giS5034, _M0L1iS1611);
      _M0L2giS5033 = _M0L1pS1600->$7;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5032 = _M0MPC15array5Array2atGfE(_M0L2giS5033, _M0L1iS1611);
      _M0L6_2atmpS5031 = -_M0L6_2atmpS5032;
      _M0L6_2atmpS5030 = _M0L2dtS1612 * _M0L6_2atmpS5031;
      _M0L6_2atmpS5029 = _M0L6_2atmpS5030 / _M0L6tau__iS1607;
      _M0L6_2atmpS5027 = _M0L6_2atmpS5028 + _M0L6_2atmpS5029;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS5026, _M0L1iS1611, _M0L6_2atmpS5027);
      _M0L6_2atmpS5035 = _M0L1iS1611 + 1;
      _M0L1iS1611 = _M0L6_2atmpS5035;
      continue;
    }
    break;
  }
  _M0L7_2abindS1614 = 0;
  _M0L1iS1615 = _M0L7_2abindS1614;
  while (1) {
    if (_M0L1iS1615 < _M0L1nS1599) {
      struct _M0TPB5ArrayGfE* _M0L1vS5061 = _M0L1pS1600->$2;
      float _M0L1vS1616;
      struct _M0TPB5ArrayGfE* _M0L1uS5060;
      float _M0L1uS1617;
      struct _M0TPB5ArrayGfE* _M0L1iS5059;
      float _M0L2iiS1618;
      struct _M0TPB5ArrayGfE* _M0L1vS5036;
      float _M0L6_2atmpS5039;
      float _M0L6_2atmpS5046;
      float _M0L6_2atmpS5044;
      float _M0L6_2atmpS5045;
      float _M0L6_2atmpS5043;
      float _M0L6_2atmpS5042;
      float _M0L6_2atmpS5041;
      float _M0L6_2atmpS5040;
      float _M0L6_2atmpS5038;
      float _M0L6_2atmpS5037;
      struct _M0TPB5ArrayGfE* _M0L1vS5058;
      float _M0L2v2S1619;
      struct _M0TPB5ArrayGfE* _M0L1vS5047;
      float _M0L6_2atmpS5050;
      float _M0L6_2atmpS5057;
      float _M0L6_2atmpS5055;
      float _M0L6_2atmpS5056;
      float _M0L6_2atmpS5054;
      float _M0L6_2atmpS5053;
      float _M0L6_2atmpS5052;
      float _M0L6_2atmpS5051;
      float _M0L6_2atmpS5049;
      float _M0L6_2atmpS5048;
      int32_t _M0L6_2atmpS5062;
      #line 359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS1616 = _M0MPC15array5Array2atGfE(_M0L1vS5061, _M0L1iS1615);
      _M0L1uS5060 = _M0L1pS1600->$3;
      #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1uS1617 = _M0MPC15array5Array2atGfE(_M0L1uS5060, _M0L1iS1615);
      _M0L1iS5059 = _M0L1pS1600->$5;
      #line 361 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2iiS1618 = _M0MPC15array5Array2atGfE(_M0L1iS5059, _M0L1iS1615);
      _M0L1vS5036 = _M0L1pS1600->$2;
      _M0L6_2atmpS5039 = 0x1p-1f * _M0L2dtS1612;
      _M0L6_2atmpS5046 = 0x1.47ae147ae147bp-5f * _M0L1vS1616;
      _M0L6_2atmpS5044 = _M0L6_2atmpS5046 * _M0L1vS1616;
      _M0L6_2atmpS5045 = 0x1.4p+2f * _M0L1vS1616;
      _M0L6_2atmpS5043 = _M0L6_2atmpS5044 + _M0L6_2atmpS5045;
      _M0L6_2atmpS5042 = _M0L6_2atmpS5043 + 0x1.18p+7f;
      _M0L6_2atmpS5041 = _M0L6_2atmpS5042 - _M0L1uS1617;
      _M0L6_2atmpS5040 = _M0L6_2atmpS5041 + _M0L2iiS1618;
      _M0L6_2atmpS5038 = _M0L6_2atmpS5039 * _M0L6_2atmpS5040;
      _M0L6_2atmpS5037 = _M0L1vS1616 + _M0L6_2atmpS5038;
      #line 362 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5036, _M0L1iS1615, _M0L6_2atmpS5037);
      _M0L1vS5058 = _M0L1pS1600->$2;
      #line 363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2v2S1619 = _M0MPC15array5Array2atGfE(_M0L1vS5058, _M0L1iS1615);
      _M0L1vS5047 = _M0L1pS1600->$2;
      _M0L6_2atmpS5050 = 0x1p-1f * _M0L2dtS1612;
      _M0L6_2atmpS5057 = 0x1.47ae147ae147bp-5f * _M0L2v2S1619;
      _M0L6_2atmpS5055 = _M0L6_2atmpS5057 * _M0L2v2S1619;
      _M0L6_2atmpS5056 = 0x1.4p+2f * _M0L2v2S1619;
      _M0L6_2atmpS5054 = _M0L6_2atmpS5055 + _M0L6_2atmpS5056;
      _M0L6_2atmpS5053 = _M0L6_2atmpS5054 + 0x1.18p+7f;
      _M0L6_2atmpS5052 = _M0L6_2atmpS5053 - _M0L1uS1617;
      _M0L6_2atmpS5051 = _M0L6_2atmpS5052 + _M0L2iiS1618;
      _M0L6_2atmpS5049 = _M0L6_2atmpS5050 * _M0L6_2atmpS5051;
      _M0L6_2atmpS5048 = _M0L2v2S1619 + _M0L6_2atmpS5049;
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5047, _M0L1iS1615, _M0L6_2atmpS5048);
      _M0L6_2atmpS5062 = _M0L1iS1615 + 1;
      _M0L1iS1615 = _M0L6_2atmpS5062;
      continue;
    }
    break;
  }
  _M0L7_2abindS1621 = 0;
  _M0L1iS1622 = _M0L7_2abindS1621;
  while (1) {
    if (_M0L1iS1622 < _M0L1nS1599) {
      struct _M0TPB5ArrayGfE* _M0L1vS5073 = _M0L1pS1600->$2;
      float _M0L1vS1623;
      struct _M0TPB5ArrayGfE* _M0L1uS5063;
      struct _M0TPB5ArrayGfE* _M0L1uS5072;
      float _M0L6_2atmpS5065;
      float _M0L6_2atmpS5067;
      float _M0L6_2atmpS5069;
      struct _M0TPB5ArrayGfE* _M0L1uS5071;
      float _M0L6_2atmpS5070;
      float _M0L6_2atmpS5068;
      float _M0L6_2atmpS5066;
      float _M0L6_2atmpS5064;
      int32_t _M0L6_2atmpS5074;
      #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS1623 = _M0MPC15array5Array2atGfE(_M0L1vS5073, _M0L1iS1622);
      _M0L1uS5063 = _M0L1pS1600->$3;
      _M0L1uS5072 = _M0L1pS1600->$3;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5065 = _M0MPC15array5Array2atGfE(_M0L1uS5072, _M0L1iS1622);
      _M0L6_2atmpS5067 = _M0L2dtS1612 * _M0L1aS1602;
      _M0L6_2atmpS5069 = _M0L1bS1603 * _M0L1vS1623;
      _M0L1uS5071 = _M0L1pS1600->$3;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5070 = _M0MPC15array5Array2atGfE(_M0L1uS5071, _M0L1iS1622);
      _M0L6_2atmpS5068 = _M0L6_2atmpS5069 - _M0L6_2atmpS5070;
      _M0L6_2atmpS5066 = _M0L6_2atmpS5067 * _M0L6_2atmpS5068;
      _M0L6_2atmpS5064 = _M0L6_2atmpS5065 + _M0L6_2atmpS5066;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS5063, _M0L1iS1622, _M0L6_2atmpS5064);
      _M0L6_2atmpS5074 = _M0L1iS1622 + 1;
      _M0L1iS1622 = _M0L6_2atmpS5074;
      continue;
    }
    break;
  }
  _M0L7_2abindS1625 = 0;
  _M0L1iS1626 = _M0L7_2abindS1625;
  while (1) {
    if (_M0L1iS1626 < _M0L1nS1599) {
      struct _M0TPB5ArrayGfE* _M0L1vS5075 = _M0L1pS1600->$2;
      struct _M0TPB5ArrayGfE* _M0L1vS5092 = _M0L1pS1600->$2;
      float _M0L6_2atmpS5077;
      struct _M0TPB5ArrayGfE* _M0L2geS5091;
      float _M0L6_2atmpS5087;
      struct _M0TPB5ArrayGfE* _M0L1vS5090;
      float _M0L6_2atmpS5089;
      float _M0L6_2atmpS5088;
      float _M0L6_2atmpS5080;
      struct _M0TPB5ArrayGfE* _M0L2giS5086;
      float _M0L6_2atmpS5082;
      struct _M0TPB5ArrayGfE* _M0L1vS5085;
      float _M0L6_2atmpS5084;
      float _M0L6_2atmpS5083;
      float _M0L6_2atmpS5081;
      float _M0L6_2atmpS5079;
      float _M0L6_2atmpS5078;
      float _M0L6_2atmpS5076;
      int32_t _M0L6_2atmpS5093;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5077 = _M0MPC15array5Array2atGfE(_M0L1vS5092, _M0L1iS1626);
      _M0L2geS5091 = _M0L1pS1600->$6;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5087 = _M0MPC15array5Array2atGfE(_M0L2geS5091, _M0L1iS1626);
      _M0L1vS5090 = _M0L1pS1600->$2;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5089 = _M0MPC15array5Array2atGfE(_M0L1vS5090, _M0L1iS1626);
      _M0L6_2atmpS5088 = _M0L4e__eS1608 - _M0L6_2atmpS5089;
      _M0L6_2atmpS5080 = _M0L6_2atmpS5087 * _M0L6_2atmpS5088;
      _M0L2giS5086 = _M0L1pS1600->$7;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5082 = _M0MPC15array5Array2atGfE(_M0L2giS5086, _M0L1iS1626);
      _M0L1vS5085 = _M0L1pS1600->$2;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5084 = _M0MPC15array5Array2atGfE(_M0L1vS5085, _M0L1iS1626);
      _M0L6_2atmpS5083 = _M0L4e__iS1609 - _M0L6_2atmpS5084;
      _M0L6_2atmpS5081 = _M0L6_2atmpS5082 * _M0L6_2atmpS5083;
      _M0L6_2atmpS5079 = _M0L6_2atmpS5080 + _M0L6_2atmpS5081;
      _M0L6_2atmpS5078 = _M0L2dtS1612 * _M0L6_2atmpS5079;
      _M0L6_2atmpS5076 = _M0L6_2atmpS5077 + _M0L6_2atmpS5078;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5075, _M0L1iS1626, _M0L6_2atmpS5076);
      _M0L6_2atmpS5093 = _M0L1iS1626 + 1;
      _M0L1iS1626 = _M0L6_2atmpS5093;
      continue;
    }
    break;
  }
  _M0L7_2abindS1628 = 0;
  _M0L1iS1629 = _M0L7_2abindS1628;
  while (1) {
    if (_M0L1iS1629 < _M0L1nS1599) {
      struct _M0TPB5ArrayGbE* _M0L4fireS5094 = _M0L1pS1600->$4;
      struct _M0TPB5ArrayGfE* _M0L1vS5097 = _M0L1pS1600->$2;
      float _M0L6_2atmpS5096;
      int32_t _M0L6_2atmpS5095;
      struct _M0TPB5ArrayGfE* _M0L1vS5098;
      struct _M0TPB5ArrayGbE* _M0L4fireS5100;
      float _M0L6_2atmpS5099;
      struct _M0TPB5ArrayGfE* _M0L1uS5102;
      struct _M0TPB5ArrayGfE* _M0L1uS5107;
      float _M0L6_2atmpS5104;
      struct _M0TPB5ArrayGbE* _M0L4fireS5106;
      float _M0L6_2atmpS5105;
      float _M0L6_2atmpS5103;
      int32_t _M0L6_2atmpS5108;
      #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5096 = _M0MPC15array5Array2atGfE(_M0L1vS5097, _M0L1iS1629);
      _M0L6_2atmpS5095 = _M0L6_2atmpS5096 > 0x1.ep+4f;
      #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS5094, _M0L1iS1629, _M0L6_2atmpS5095);
      _M0L1vS5098 = _M0L1pS1600->$2;
      _M0L4fireS5100 = _M0L1pS1600->$4;
      #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS5100, _M0L1iS1629)) {
        _M0L6_2atmpS5099 = _M0L1cS1604;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS5101 = _M0L1pS1600->$2;
        #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS5099
        = _M0MPC15array5Array2atGfE(_M0L1vS5101, _M0L1iS1629);
      }
      #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5098, _M0L1iS1629, _M0L6_2atmpS5099);
      _M0L1uS5102 = _M0L1pS1600->$3;
      _M0L1uS5107 = _M0L1pS1600->$3;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5104 = _M0MPC15array5Array2atGfE(_M0L1uS5107, _M0L1iS1629);
      _M0L4fireS5106 = _M0L1pS1600->$4;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS5106, _M0L1iS1629)) {
        _M0L6_2atmpS5105 = _M0L1dS1605;
      } else {
        _M0L6_2atmpS5105 = 0x0p+0f;
      }
      _M0L6_2atmpS5103 = _M0L6_2atmpS5104 + _M0L6_2atmpS5105;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS5102, _M0L1iS1629, _M0L6_2atmpS5103);
      _M0L6_2atmpS5108 = _M0L1iS1629 + 1;
      _M0L1iS1629 = _M0L6_2atmpS5108;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__hh(
  struct _M0TP26RiantR8snn__mbt2HH* _M0L1pS1556,
  float _M0L2dtS1582
) {
  int32_t _M0L1nS1555;
  struct _M0TP26RiantR8snn__mbt11HHParameter* _M0L3p__S1557;
  float _M0L2cmS1558;
  float _M0L2glS1559;
  float _M0L2elS1560;
  float _M0L2ekS1561;
  float _M0L2enS1562;
  float _M0L2gnS1563;
  float _M0L2gkS1564;
  float _M0L2vtS1565;
  float _M0L6tau__eS1566;
  float _M0L6tau__iS1567;
  float _M0L4e__eS1568;
  float _M0L4e__iS1569;
  int32_t _M0L7_2abindS1570;
  int32_t _M0L1iS1571;
  int32_t _M0L7_2abindS1596;
  int32_t _M0L1iS1597;
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L1nS1555 = _M0L1pS1556->$1;
  _M0L3p__S1557 = _M0L1pS1556->$0;
  _M0L2cmS1558 = _M0L3p__S1557->$0;
  _M0L2glS1559 = _M0L3p__S1557->$1;
  _M0L2elS1560 = _M0L3p__S1557->$2;
  _M0L2ekS1561 = _M0L3p__S1557->$3;
  _M0L2enS1562 = _M0L3p__S1557->$4;
  _M0L2gnS1563 = _M0L3p__S1557->$5;
  _M0L2gkS1564 = _M0L3p__S1557->$6;
  _M0L2vtS1565 = _M0L3p__S1557->$7;
  _M0L6tau__eS1566 = _M0L3p__S1557->$8;
  _M0L6tau__iS1567 = _M0L3p__S1557->$9;
  _M0L4e__eS1568 = _M0L3p__S1557->$10;
  _M0L4e__iS1569 = _M0L3p__S1557->$11;
  _M0L7_2abindS1570 = 0;
  _M0L1iS1571 = _M0L7_2abindS1570;
  while (1) {
    if (_M0L1iS1571 < _M0L1nS1555) {
      struct _M0TPB5ArrayGfE* _M0L1vS5010 = _M0L1pS1556->$2;
      float _M0L1vS1572;
      struct _M0TPB5ArrayGfE* _M0L1mS5009;
      float _M0L1mS1573;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS5008;
      float _M0L2nnS1574;
      struct _M0TPB5ArrayGfE* _M0L1hS5007;
      float _M0L1hS1575;
      struct _M0TPB5ArrayGfE* _M0L2geS5006;
      float _M0L2geS1576;
      struct _M0TPB5ArrayGfE* _M0L2giS5005;
      float _M0L2giS1577;
      struct _M0TPB5ArrayGbE* _M0L4fireS4908;
      float _M0L6_2atmpS5004;
      float _M0L7am__numS1578;
      float _M0L6_2atmpS5003;
      float _M0L7bm__numS1579;
      float _M0L6_2atmpS4998;
      float _M0L6_2atmpS4997;
      float _M0L6_2atmpS4996;
      float _M0L2amS1580;
      float _M0L6_2atmpS4991;
      float _M0L6_2atmpS4990;
      float _M0L6_2atmpS4989;
      float _M0L2bmS1581;
      struct _M0TPB5ArrayGfE* _M0L1mS4909;
      float _M0L6_2atmpS4915;
      float _M0L6_2atmpS4913;
      float _M0L6_2atmpS4914;
      float _M0L6_2atmpS4912;
      float _M0L6_2atmpS4911;
      float _M0L6_2atmpS4910;
      float _M0L6_2atmpS4988;
      float _M0L7an__numS1583;
      float _M0L6_2atmpS4983;
      float _M0L6_2atmpS4982;
      float _M0L6_2atmpS4981;
      float _M0L2anS1584;
      float _M0L6_2atmpS4980;
      float _M0L6_2atmpS4979;
      float _M0L6_2atmpS4978;
      float _M0L6_2atmpS4977;
      float _M0L2bnS1585;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4916;
      float _M0L6_2atmpS4922;
      float _M0L6_2atmpS4920;
      float _M0L6_2atmpS4921;
      float _M0L6_2atmpS4919;
      float _M0L6_2atmpS4918;
      float _M0L6_2atmpS4917;
      float _M0L6_2atmpS4976;
      float _M0L6_2atmpS4975;
      float _M0L6_2atmpS4974;
      float _M0L6_2atmpS4973;
      float _M0L2ahS1586;
      float _M0L6_2atmpS4972;
      float _M0L6_2atmpS4971;
      float _M0L6_2atmpS4970;
      float _M0L6_2atmpS4969;
      float _M0L9bh__denomS1587;
      float _M0L2bhS1588;
      struct _M0TPB5ArrayGfE* _M0L1hS4923;
      float _M0L6_2atmpS4929;
      float _M0L6_2atmpS4927;
      float _M0L6_2atmpS4928;
      float _M0L6_2atmpS4926;
      float _M0L6_2atmpS4925;
      float _M0L6_2atmpS4924;
      struct _M0TPB5ArrayGfE* _M0L1mS4968;
      float _M0L6m__newS1589;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4967;
      float _M0L6n__newS1590;
      struct _M0TPB5ArrayGfE* _M0L1hS4966;
      float _M0L6h__newS1591;
      float _M0L6_2atmpS4965;
      float _M0L6_2atmpS4964;
      float _M0L3m3hS1592;
      float _M0L6_2atmpS4963;
      float _M0L6_2atmpS4962;
      float _M0L2n4S1593;
      struct _M0TPB5ArrayGfE* _M0L1iS4961;
      float _M0L6_2atmpS4958;
      float _M0L6_2atmpS4960;
      float _M0L6_2atmpS4959;
      float _M0L6_2atmpS4955;
      float _M0L6_2atmpS4957;
      float _M0L6_2atmpS4956;
      float _M0L6_2atmpS4952;
      float _M0L6_2atmpS4954;
      float _M0L6_2atmpS4953;
      float _M0L6_2atmpS4948;
      float _M0L6_2atmpS4950;
      float _M0L6_2atmpS4951;
      float _M0L6_2atmpS4949;
      float _M0L6_2atmpS4944;
      float _M0L6_2atmpS4946;
      float _M0L6_2atmpS4947;
      float _M0L6_2atmpS4945;
      float _M0L7currentS1594;
      struct _M0TPB5ArrayGfE* _M0L1vS4930;
      float _M0L6_2atmpS4933;
      float _M0L6_2atmpS4932;
      float _M0L6_2atmpS4931;
      struct _M0TPB5ArrayGfE* _M0L2geS4934;
      float _M0L6_2atmpS4938;
      float _M0L6_2atmpS4937;
      float _M0L6_2atmpS4936;
      float _M0L6_2atmpS4935;
      struct _M0TPB5ArrayGfE* _M0L2giS4939;
      float _M0L6_2atmpS4943;
      float _M0L6_2atmpS4942;
      float _M0L6_2atmpS4941;
      float _M0L6_2atmpS4940;
      int32_t _M0L6_2atmpS5011;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1vS1572 = _M0MPC15array5Array2atGfE(_M0L1vS5010, _M0L1iS1571);
      _M0L1mS5009 = _M0L1pS1556->$3;
      #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1mS1573 = _M0MPC15array5Array2atGfE(_M0L1mS5009, _M0L1iS1571);
      _M0L7n__gateS5008 = _M0L1pS1556->$4;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2nnS1574
      = _M0MPC15array5Array2atGfE(_M0L7n__gateS5008, _M0L1iS1571);
      _M0L1hS5007 = _M0L1pS1556->$5;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1hS1575 = _M0MPC15array5Array2atGfE(_M0L1hS5007, _M0L1iS1571);
      _M0L2geS5006 = _M0L1pS1556->$8;
      #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2geS1576 = _M0MPC15array5Array2atGfE(_M0L2geS5006, _M0L1iS1571);
      _M0L2giS5005 = _M0L1pS1556->$9;
      #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2giS1577 = _M0MPC15array5Array2atGfE(_M0L2giS5005, _M0L1iS1571);
      _M0L4fireS4908 = _M0L1pS1556->$6;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4908, _M0L1iS1571, 0);
      _M0L6_2atmpS5004 = 0x1.ap+3f - _M0L1vS1572;
      _M0L7am__numS1578 = _M0L6_2atmpS5004 + _M0L2vtS1565;
      _M0L6_2atmpS5003 = _M0L1vS1572 - _M0L2vtS1565;
      _M0L7bm__numS1579 = _M0L6_2atmpS5003 - 0x1.4p+5f;
      _M0L6_2atmpS4998 = _M0L7am__numS1578 / 0x1p+2f;
      #line 134 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4997 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4998);
      _M0L6_2atmpS4996 = _M0L6_2atmpS4997 - 0x1p+0f;
      if (_M0L6_2atmpS4996 != 0x0p+0f) {
        float _M0L6_2atmpS4999 = 0x1.47ae147ae147bp-2f * _M0L7am__numS1578;
        float _M0L6_2atmpS5002 = _M0L7am__numS1578 / 0x1p+2f;
        float _M0L6_2atmpS5001;
        float _M0L6_2atmpS5000;
        #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS5001 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS5002);
        _M0L6_2atmpS5000 = _M0L6_2atmpS5001 - 0x1p+0f;
        _M0L2amS1580 = _M0L6_2atmpS4999 / _M0L6_2atmpS5000;
      } else {
        _M0L2amS1580 = 0x0p+0f;
      }
      _M0L6_2atmpS4991 = _M0L7bm__numS1579 / 0x1.4p+2f;
      #line 139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4990 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4991);
      _M0L6_2atmpS4989 = _M0L6_2atmpS4990 - 0x1p+0f;
      if (_M0L6_2atmpS4989 != 0x0p+0f) {
        float _M0L6_2atmpS4992 = 0x1.1eb851eb851ecp-2f * _M0L7bm__numS1579;
        float _M0L6_2atmpS4995 = _M0L7bm__numS1579 / 0x1.4p+2f;
        float _M0L6_2atmpS4994;
        float _M0L6_2atmpS4993;
        #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS4994 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4995);
        _M0L6_2atmpS4993 = _M0L6_2atmpS4994 - 0x1p+0f;
        _M0L2bmS1581 = _M0L6_2atmpS4992 / _M0L6_2atmpS4993;
      } else {
        _M0L2bmS1581 = 0x0p+0f;
      }
      _M0L1mS4909 = _M0L1pS1556->$3;
      _M0L6_2atmpS4915 = 0x1p+0f - _M0L1mS1573;
      _M0L6_2atmpS4913 = _M0L2amS1580 * _M0L6_2atmpS4915;
      _M0L6_2atmpS4914 = _M0L2bmS1581 * _M0L1mS1573;
      _M0L6_2atmpS4912 = _M0L6_2atmpS4913 - _M0L6_2atmpS4914;
      _M0L6_2atmpS4911 = _M0L2dtS1582 * _M0L6_2atmpS4912;
      _M0L6_2atmpS4910 = _M0L1mS1573 + _M0L6_2atmpS4911;
      #line 144 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1mS4909, _M0L1iS1571, _M0L6_2atmpS4910);
      _M0L6_2atmpS4988 = 0x1.ep+3f - _M0L1vS1572;
      _M0L7an__numS1583 = _M0L6_2atmpS4988 + _M0L2vtS1565;
      _M0L6_2atmpS4983 = _M0L7an__numS1583 / 0x1.4p+2f;
      #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4982 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4983);
      _M0L6_2atmpS4981 = _M0L6_2atmpS4982 - 0x1p+0f;
      if (_M0L6_2atmpS4981 != 0x0p+0f) {
        float _M0L6_2atmpS4984 = 0x1.0624dd2f1a9fcp-5f * _M0L7an__numS1583;
        float _M0L6_2atmpS4987 = _M0L7an__numS1583 / 0x1.4p+2f;
        float _M0L6_2atmpS4986;
        float _M0L6_2atmpS4985;
        #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS4986 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4987);
        _M0L6_2atmpS4985 = _M0L6_2atmpS4986 - 0x1p+0f;
        _M0L2anS1584 = _M0L6_2atmpS4984 / _M0L6_2atmpS4985;
      } else {
        _M0L2anS1584 = 0x0p+0f;
      }
      _M0L6_2atmpS4980 = 0x1.4p+3f - _M0L1vS1572;
      _M0L6_2atmpS4979 = _M0L6_2atmpS4980 + _M0L2vtS1565;
      _M0L6_2atmpS4978 = _M0L6_2atmpS4979 / 0x1.4p+5f;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4977 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4978);
      _M0L2bnS1585 = 0x1p-1f * _M0L6_2atmpS4977;
      _M0L7n__gateS4916 = _M0L1pS1556->$4;
      _M0L6_2atmpS4922 = 0x1p+0f - _M0L2nnS1574;
      _M0L6_2atmpS4920 = _M0L2anS1584 * _M0L6_2atmpS4922;
      _M0L6_2atmpS4921 = _M0L2bnS1585 * _M0L2nnS1574;
      _M0L6_2atmpS4919 = _M0L6_2atmpS4920 - _M0L6_2atmpS4921;
      _M0L6_2atmpS4918 = _M0L2dtS1582 * _M0L6_2atmpS4919;
      _M0L6_2atmpS4917 = _M0L2nnS1574 + _M0L6_2atmpS4918;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L7n__gateS4916, _M0L1iS1571, _M0L6_2atmpS4917);
      _M0L6_2atmpS4976 = 0x1.1p+4f - _M0L1vS1572;
      _M0L6_2atmpS4975 = _M0L6_2atmpS4976 + _M0L2vtS1565;
      _M0L6_2atmpS4974 = _M0L6_2atmpS4975 / 0x1.2p+4f;
      #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4973 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4974);
      _M0L2ahS1586 = 0x1.0624dd2f1a9fcp-3f * _M0L6_2atmpS4973;
      _M0L6_2atmpS4972 = 0x1.4p+5f - _M0L1vS1572;
      _M0L6_2atmpS4971 = _M0L6_2atmpS4972 + _M0L2vtS1565;
      _M0L6_2atmpS4970 = _M0L6_2atmpS4971 / 0x1.4p+2f;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4969 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4970);
      _M0L9bh__denomS1587 = 0x1p+0f + _M0L6_2atmpS4969;
      if (_M0L9bh__denomS1587 != 0x0p+0f) {
        _M0L2bhS1588 = 0x1p+2f / _M0L9bh__denomS1587;
      } else {
        _M0L2bhS1588 = 0x0p+0f;
      }
      _M0L1hS4923 = _M0L1pS1556->$5;
      _M0L6_2atmpS4929 = 0x1p+0f - _M0L1hS1575;
      _M0L6_2atmpS4927 = _M0L2ahS1586 * _M0L6_2atmpS4929;
      _M0L6_2atmpS4928 = _M0L2bhS1588 * _M0L1hS1575;
      _M0L6_2atmpS4926 = _M0L6_2atmpS4927 - _M0L6_2atmpS4928;
      _M0L6_2atmpS4925 = _M0L2dtS1582 * _M0L6_2atmpS4926;
      _M0L6_2atmpS4924 = _M0L1hS1575 + _M0L6_2atmpS4925;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1hS4923, _M0L1iS1571, _M0L6_2atmpS4924);
      _M0L1mS4968 = _M0L1pS1556->$3;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6m__newS1589 = _M0MPC15array5Array2atGfE(_M0L1mS4968, _M0L1iS1571);
      _M0L7n__gateS4967 = _M0L1pS1556->$4;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6n__newS1590
      = _M0MPC15array5Array2atGfE(_M0L7n__gateS4967, _M0L1iS1571);
      _M0L1hS4966 = _M0L1pS1556->$5;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6h__newS1591 = _M0MPC15array5Array2atGfE(_M0L1hS4966, _M0L1iS1571);
      _M0L6_2atmpS4965 = _M0L6m__newS1589 * _M0L6m__newS1589;
      _M0L6_2atmpS4964 = _M0L6_2atmpS4965 * _M0L6m__newS1589;
      _M0L3m3hS1592 = _M0L6_2atmpS4964 * _M0L6h__newS1591;
      _M0L6_2atmpS4963 = _M0L6n__newS1590 * _M0L6n__newS1590;
      _M0L6_2atmpS4962 = _M0L6_2atmpS4963 * _M0L6n__newS1590;
      _M0L2n4S1593 = _M0L6_2atmpS4962 * _M0L6n__newS1590;
      _M0L1iS4961 = _M0L1pS1556->$7;
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4958 = _M0MPC15array5Array2atGfE(_M0L1iS4961, _M0L1iS1571);
      _M0L6_2atmpS4960 = _M0L2elS1560 - _M0L1vS1572;
      _M0L6_2atmpS4959 = _M0L2glS1559 * _M0L6_2atmpS4960;
      _M0L6_2atmpS4955 = _M0L6_2atmpS4958 + _M0L6_2atmpS4959;
      _M0L6_2atmpS4957 = _M0L4e__eS1568 - _M0L1vS1572;
      _M0L6_2atmpS4956 = _M0L2geS1576 * _M0L6_2atmpS4957;
      _M0L6_2atmpS4952 = _M0L6_2atmpS4955 + _M0L6_2atmpS4956;
      _M0L6_2atmpS4954 = _M0L4e__iS1569 - _M0L1vS1572;
      _M0L6_2atmpS4953 = _M0L2giS1577 * _M0L6_2atmpS4954;
      _M0L6_2atmpS4948 = _M0L6_2atmpS4952 + _M0L6_2atmpS4953;
      _M0L6_2atmpS4950 = _M0L2gnS1563 * _M0L3m3hS1592;
      _M0L6_2atmpS4951 = _M0L2enS1562 - _M0L1vS1572;
      _M0L6_2atmpS4949 = _M0L6_2atmpS4950 * _M0L6_2atmpS4951;
      _M0L6_2atmpS4944 = _M0L6_2atmpS4948 + _M0L6_2atmpS4949;
      _M0L6_2atmpS4946 = _M0L2gkS1564 * _M0L2n4S1593;
      _M0L6_2atmpS4947 = _M0L2ekS1561 - _M0L1vS1572;
      _M0L6_2atmpS4945 = _M0L6_2atmpS4946 * _M0L6_2atmpS4947;
      _M0L7currentS1594 = _M0L6_2atmpS4944 + _M0L6_2atmpS4945;
      _M0L1vS4930 = _M0L1pS1556->$2;
      _M0L6_2atmpS4933 = _M0L2dtS1582 / _M0L2cmS1558;
      _M0L6_2atmpS4932 = _M0L6_2atmpS4933 * _M0L7currentS1594;
      _M0L6_2atmpS4931 = _M0L1vS1572 + _M0L6_2atmpS4932;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4930, _M0L1iS1571, _M0L6_2atmpS4931);
      _M0L2geS4934 = _M0L1pS1556->$8;
      _M0L6_2atmpS4938 = -_M0L2geS1576;
      _M0L6_2atmpS4937 = _M0L6_2atmpS4938 / _M0L6tau__eS1566;
      _M0L6_2atmpS4936 = _M0L2dtS1582 * _M0L6_2atmpS4937;
      _M0L6_2atmpS4935 = _M0L2geS1576 + _M0L6_2atmpS4936;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4934, _M0L1iS1571, _M0L6_2atmpS4935);
      _M0L2giS4939 = _M0L1pS1556->$9;
      _M0L6_2atmpS4943 = -_M0L2giS1577;
      _M0L6_2atmpS4942 = _M0L6_2atmpS4943 / _M0L6tau__iS1567;
      _M0L6_2atmpS4941 = _M0L2dtS1582 * _M0L6_2atmpS4942;
      _M0L6_2atmpS4940 = _M0L2giS1577 + _M0L6_2atmpS4941;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4939, _M0L1iS1571, _M0L6_2atmpS4940);
      _M0L6_2atmpS5011 = _M0L1iS1571 + 1;
      _M0L1iS1571 = _M0L6_2atmpS5011;
      continue;
    }
    break;
  }
  _M0L7_2abindS1596 = 0;
  _M0L1iS1597 = _M0L7_2abindS1596;
  while (1) {
    if (_M0L1iS1597 < _M0L1nS1555) {
      struct _M0TPB5ArrayGbE* _M0L4fireS5012 = _M0L1pS1556->$6;
      struct _M0TPB5ArrayGfE* _M0L1vS5015 = _M0L1pS1556->$2;
      float _M0L6_2atmpS5014;
      int32_t _M0L6_2atmpS5013;
      int32_t _M0L6_2atmpS5016;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS5014 = _M0MPC15array5Array2atGfE(_M0L1vS5015, _M0L1iS1597);
      _M0L6_2atmpS5013 = _M0L6_2atmpS5014 > -0x1.4p+4f;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS5012, _M0L1iS1597, _M0L6_2atmpS5013);
      _M0L6_2atmpS5016 = _M0L1iS1597 + 1;
      _M0L1iS1597 = _M0L6_2atmpS5016;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__hetrec(
  struct _M0TP26RiantR8snn__mbt6HetRec* _M0L1pS1526,
  float _M0L2dtS1534
) {
  int32_t _M0L1nS1525;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4907;
  int32_t _M0L2ndS1527;
  int32_t _M0L8total__dS1528;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4906;
  float _M0L9steepnessS1529;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4905;
  float _M0L6tau__mS1530;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4904;
  float _M0L9tau__rateS1531;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4903;
  float _M0L8tau__absS1532;
  float _M0L6_2atmpS4902;
  int32_t _M0L11tabs__stepsS1533;
  int32_t _M0L7_2abindS1535;
  int32_t _M0L1iS1536;
  int32_t _M0L7_2abindS1539;
  int32_t _M0L1iS1540;
  int32_t _M0L7_2abindS1549;
  int32_t _M0L1iS1550;
  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L1nS1525 = _M0L1pS1526->$1;
  _M0L5paramS4907 = _M0L1pS1526->$0;
  _M0L2ndS1527 = _M0L5paramS4907->$0;
  _M0L8total__dS1528 = _M0L1nS1525 * _M0L2ndS1527;
  _M0L5paramS4906 = _M0L1pS1526->$0;
  _M0L9steepnessS1529 = _M0L5paramS4906->$7;
  _M0L5paramS4905 = _M0L1pS1526->$0;
  _M0L6tau__mS1530 = _M0L5paramS4905->$8;
  _M0L5paramS4904 = _M0L1pS1526->$0;
  _M0L9tau__rateS1531 = _M0L5paramS4904->$9;
  _M0L5paramS4903 = _M0L1pS1526->$0;
  _M0L8tau__absS1532 = _M0L5paramS4903->$6;
  _M0L6_2atmpS4902 = _M0L8tau__absS1532 / _M0L2dtS1534;
  #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L11tabs__stepsS1533 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4902);
  _M0L7_2abindS1535 = 0;
  _M0L1iS1536 = _M0L7_2abindS1535;
  while (1) {
    if (_M0L1iS1536 < _M0L8total__dS1528) {
      struct _M0TPB5ArrayGfE* _M0L6tau__dS4830 = _M0L1pS1526->$6;
      float _M0L7tau__diS1537;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4818;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4829;
      float _M0L6_2atmpS4820;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4828;
      float _M0L6_2atmpS4827;
      float _M0L6_2atmpS4824;
      struct _M0TPB5ArrayGfE* _M0L4is__S4826;
      float _M0L6_2atmpS4825;
      float _M0L6_2atmpS4823;
      float _M0L6_2atmpS4822;
      float _M0L6_2atmpS4821;
      float _M0L6_2atmpS4819;
      int32_t _M0L6_2atmpS4831;
      #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L7tau__diS1537
      = _M0MPC15array5Array2atGfE(_M0L6tau__dS4830, _M0L1iS1536);
      _M0L4v__dS4818 = _M0L1pS1526->$2;
      _M0L4v__dS4829 = _M0L1pS1526->$2;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4820
      = _M0MPC15array5Array2atGfE(_M0L4v__dS4829, _M0L1iS1536);
      _M0L4v__dS4828 = _M0L1pS1526->$2;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4827
      = _M0MPC15array5Array2atGfE(_M0L4v__dS4828, _M0L1iS1536);
      _M0L6_2atmpS4824 = -_M0L6_2atmpS4827;
      _M0L4is__S4826 = _M0L1pS1526->$4;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4825
      = _M0MPC15array5Array2atGfE(_M0L4is__S4826, _M0L1iS1536);
      _M0L6_2atmpS4823 = _M0L6_2atmpS4824 - _M0L6_2atmpS4825;
      _M0L6_2atmpS4822 = _M0L2dtS1534 * _M0L6_2atmpS4823;
      _M0L6_2atmpS4821 = _M0L6_2atmpS4822 / _M0L7tau__diS1537;
      _M0L6_2atmpS4819 = _M0L6_2atmpS4820 + _M0L6_2atmpS4821;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__dS4818, _M0L1iS1536, _M0L6_2atmpS4819);
      _M0L6_2atmpS4831 = _M0L1iS1536 + 1;
      _M0L1iS1536 = _M0L6_2atmpS4831;
      continue;
    }
    break;
  }
  _M0L7_2abindS1539 = 0;
  _M0L1iS1540 = _M0L7_2abindS1539;
  while (1) {
    if (_M0L1iS1540 < _M0L1nS1525) {
      struct _M0TPB5ArrayGiE* _M0L6colptrS4852 = _M0L1pS1526->$11;
      int32_t _M0L5startS1541;
      struct _M0TPB5ArrayGiE* _M0L6colptrS4850;
      int32_t _M0L6_2atmpS4851;
      int32_t _M0L3endS1542;
      float _M0L16dt__over__tau__mS1543;
      struct _M0TPB8MutLocalGiE* _M0L1sS1544;
      int32_t _M0L6_2atmpS4853;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L5startS1541
      = _M0MPC15array5Array2atGiE(_M0L6colptrS4852, _M0L1iS1540);
      _M0L6colptrS4850 = _M0L1pS1526->$11;
      _M0L6_2atmpS4851 = _M0L1iS1540 + 1;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L3endS1542
      = _M0MPC15array5Array2atGiE(_M0L6colptrS4850, _M0L6_2atmpS4851);
      _M0L16dt__over__tau__mS1543 = _M0L2dtS1534 / _M0L6tau__mS1530;
      _M0L1sS1544
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1544)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1544->$0 = _M0L5startS1541;
      while (1) {
        int32_t _M0L3valS4832 = _M0L1sS1544->$0;
        if (_M0L3valS4832 < _M0L3endS1542) {
          struct _M0TPB5ArrayGiE* _M0L6i__synS4848 = _M0L1pS1526->$12;
          int32_t _M0L3valS4849 = _M0L1sS1544->$0;
          int32_t _M0L9dend__idxS1545;
          struct _M0TPB5ArrayGfE* _M0L6w__synS4846;
          int32_t _M0L3valS4847;
          float _M0L1wS1546;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4833;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4843;
          float _M0L6_2atmpS4835;
          struct _M0TPB5ArrayGfE* _M0L4v__dS4842;
          float _M0L6_2atmpS4841;
          float _M0L6_2atmpS4838;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4840;
          float _M0L6_2atmpS4839;
          float _M0L6_2atmpS4837;
          float _M0L6_2atmpS4836;
          float _M0L6_2atmpS4834;
          int32_t _M0L3valS4845;
          int32_t _M0L6_2atmpS4844;
          #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L9dend__idxS1545
          = _M0MPC15array5Array2atGiE(_M0L6i__synS4848, _M0L3valS4849);
          _M0L6w__synS4846 = _M0L1pS1526->$13;
          _M0L3valS4847 = _M0L1sS1544->$0;
          #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L1wS1546
          = _M0MPC15array5Array2atGfE(_M0L6w__synS4846, _M0L3valS4847);
          _M0L4v__sS4833 = _M0L1pS1526->$3;
          _M0L4v__sS4843 = _M0L1pS1526->$3;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4835
          = _M0MPC15array5Array2atGfE(_M0L4v__sS4843, _M0L1iS1540);
          _M0L4v__dS4842 = _M0L1pS1526->$2;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4841
          = _M0MPC15array5Array2atGfE(_M0L4v__dS4842, _M0L9dend__idxS1545);
          _M0L6_2atmpS4838 = _M0L1wS1546 * _M0L6_2atmpS4841;
          _M0L4v__sS4840 = _M0L1pS1526->$3;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4839
          = _M0MPC15array5Array2atGfE(_M0L4v__sS4840, _M0L1iS1540);
          _M0L6_2atmpS4837 = _M0L6_2atmpS4838 - _M0L6_2atmpS4839;
          _M0L6_2atmpS4836 = _M0L6_2atmpS4837 * _M0L16dt__over__tau__mS1543;
          _M0L6_2atmpS4834 = _M0L6_2atmpS4835 + _M0L6_2atmpS4836;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0MPC15array5Array3setGfE(_M0L4v__sS4833, _M0L1iS1540, _M0L6_2atmpS4834);
          _M0L3valS4845 = _M0L1sS1544->$0;
          _M0L6_2atmpS4844 = _M0L3valS4845 + 1;
          _M0L1sS1544->$0 = _M0L6_2atmpS4844;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1544);
        }
        break;
      }
      _M0L6_2atmpS4853 = _M0L1iS1540 + 1;
      _M0L1iS1540 = _M0L6_2atmpS4853;
      continue;
    }
    break;
  }
  _M0L7_2abindS1549 = 0;
  _M0L1iS1550 = _M0L7_2abindS1549;
  while (1) {
    if (_M0L1iS1550 < _M0L1nS1525) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS4855 = _M0L1pS1526->$8;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4858 = _M0L1pS1526->$8;
      int32_t _M0L6_2atmpS4857;
      int32_t _M0L6_2atmpS4856;
      struct _M0TPB5ArrayGbE* _M0L4fireS4859;
      struct _M0TPB5ArrayGfE* _M0L5traceS4860;
      struct _M0TPB5ArrayGfE* _M0L5traceS4868;
      float _M0L6_2atmpS4862;
      struct _M0TPB5ArrayGfE* _M0L5traceS4867;
      float _M0L6_2atmpS4866;
      float _M0L6_2atmpS4865;
      float _M0L6_2atmpS4864;
      float _M0L6_2atmpS4863;
      float _M0L6_2atmpS4861;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4870;
      int32_t _M0L6_2atmpS4869;
      struct _M0TPB5ArrayGfE* _M0L5traceS4871;
      struct _M0TPB5ArrayGfE* _M0L5traceS4880;
      float _M0L6_2atmpS4873;
      struct _M0TPB5ArrayGfE* _M0L4v__sS4879;
      float _M0L6_2atmpS4876;
      struct _M0TPB5ArrayGfE* _M0L5traceS4878;
      float _M0L6_2atmpS4877;
      float _M0L6_2atmpS4875;
      float _M0L6_2atmpS4874;
      float _M0L6_2atmpS4872;
      float _M0L6_2atmpS4896;
      struct _M0TPB5ArrayGfE* _M0L4v__sS4901;
      float _M0L6_2atmpS4898;
      struct _M0TPB5ArrayGfE* _M0L5traceS4900;
      float _M0L6_2atmpS4899;
      float _M0L6_2atmpS4897;
      float _M0L12sigmoid__argS1553;
      float _M0L4rateS1554;
      struct _M0TPB5ArrayGfE* _M0L9randcacheS4882;
      float _M0L6_2atmpS4881;
      int32_t _M0L6_2atmpS4854;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4857
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4858, _M0L1iS1550);
      _M0L6_2atmpS4856 = _M0L6_2atmpS4857 - 1;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4855, _M0L1iS1550, _M0L6_2atmpS4856);
      _M0L4fireS4859 = _M0L1pS1526->$7;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4859, _M0L1iS1550, 0);
      _M0L5traceS4860 = _M0L1pS1526->$9;
      _M0L5traceS4868 = _M0L1pS1526->$9;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4862
      = _M0MPC15array5Array2atGfE(_M0L5traceS4868, _M0L1iS1550);
      _M0L5traceS4867 = _M0L1pS1526->$9;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4866
      = _M0MPC15array5Array2atGfE(_M0L5traceS4867, _M0L1iS1550);
      _M0L6_2atmpS4865 = -_M0L6_2atmpS4866;
      _M0L6_2atmpS4864 = _M0L6_2atmpS4865 / _M0L9tau__rateS1531;
      _M0L6_2atmpS4863 = _M0L2dtS1534 * _M0L6_2atmpS4864;
      _M0L6_2atmpS4861 = _M0L6_2atmpS4862 + _M0L6_2atmpS4863;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L5traceS4860, _M0L1iS1550, _M0L6_2atmpS4861);
      _M0L4tabsS4870 = _M0L1pS1526->$8;
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4869
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4870, _M0L1iS1550);
      if (_M0L6_2atmpS4869 > 0) {
        goto join_1551;
      }
      _M0L5traceS4871 = _M0L1pS1526->$9;
      _M0L5traceS4880 = _M0L1pS1526->$9;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4873
      = _M0MPC15array5Array2atGfE(_M0L5traceS4880, _M0L1iS1550);
      _M0L4v__sS4879 = _M0L1pS1526->$3;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4876
      = _M0MPC15array5Array2atGfE(_M0L4v__sS4879, _M0L1iS1550);
      _M0L5traceS4878 = _M0L1pS1526->$9;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4877
      = _M0MPC15array5Array2atGfE(_M0L5traceS4878, _M0L1iS1550);
      _M0L6_2atmpS4875 = _M0L6_2atmpS4876 - _M0L6_2atmpS4877;
      _M0L6_2atmpS4874 = _M0L6_2atmpS4875 / _M0L9tau__rateS1531;
      _M0L6_2atmpS4872 = _M0L6_2atmpS4873 + _M0L6_2atmpS4874;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L5traceS4871, _M0L1iS1550, _M0L6_2atmpS4872);
      _M0L6_2atmpS4896 = -_M0L9steepnessS1529;
      _M0L4v__sS4901 = _M0L1pS1526->$3;
      #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4898
      = _M0MPC15array5Array2atGfE(_M0L4v__sS4901, _M0L1iS1550);
      _M0L5traceS4900 = _M0L1pS1526->$9;
      #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4899
      = _M0MPC15array5Array2atGfE(_M0L5traceS4900, _M0L1iS1550);
      _M0L6_2atmpS4897 = _M0L6_2atmpS4898 - _M0L6_2atmpS4899;
      _M0L12sigmoid__argS1553 = _M0L6_2atmpS4896 * _M0L6_2atmpS4897;
      if (_M0L12sigmoid__argS1553 > 0x1.6p+6f) {
        struct _M0TPB5ArrayGfE* _M0L1rS4890 = _M0L1pS1526->$5;
        float _M0L6_2atmpS4889;
        #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4889
        = _M0MPC15array5Array2atGfE(_M0L1rS4890, _M0L1iS1550);
        _M0L4rateS1554 = _M0L6_2atmpS4889 * _M0L2dtS1534;
      } else if (_M0L12sigmoid__argS1553 < -0x1.6p+6f) {
        _M0L4rateS1554 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1rS4895 = _M0L1pS1526->$5;
        float _M0L6_2atmpS4894;
        float _M0L6_2atmpS4891;
        float _M0L6_2atmpS4893;
        float _M0L6_2atmpS4892;
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4894
        = _M0MPC15array5Array2atGfE(_M0L1rS4895, _M0L1iS1550);
        _M0L6_2atmpS4891 = _M0L6_2atmpS4894 * _M0L2dtS1534;
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4893
        = _M0FP26RiantR8snn__mbt4expf(_M0L12sigmoid__argS1553);
        _M0L6_2atmpS4892 = 0x1p+0f + _M0L6_2atmpS4893;
        _M0L4rateS1554 = _M0L6_2atmpS4891 / _M0L6_2atmpS4892;
      }
      _M0L9randcacheS4882 = _M0L1pS1526->$10;
      #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4881
      = _M0MPC15array5Array2atGfE(_M0L9randcacheS4882, _M0L1iS1550);
      if (_M0L6_2atmpS4881 < _M0L4rateS1554) {
        struct _M0TPB5ArrayGbE* _M0L4fireS4883 = _M0L1pS1526->$7;
        struct _M0TPB5ArrayGiE* _M0L4tabsS4884;
        struct _M0TPB5ArrayGfE* _M0L5traceS4885;
        struct _M0TPB5ArrayGfE* _M0L5traceS4888;
        float _M0L6_2atmpS4887;
        float _M0L6_2atmpS4886;
        #line 267 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS4883, _M0L1iS1550, 1);
        _M0L4tabsS4884 = _M0L1pS1526->$8;
        #line 268 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS4884, _M0L1iS1550, _M0L11tabs__stepsS1533);
        _M0L5traceS4885 = _M0L1pS1526->$9;
        _M0L5traceS4888 = _M0L1pS1526->$9;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4887
        = _M0MPC15array5Array2atGfE(_M0L5traceS4888, _M0L1iS1550);
        _M0L6_2atmpS4886 = _M0L6_2atmpS4887 + 0x1p+0f;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGfE(_M0L5traceS4885, _M0L1iS1550, _M0L6_2atmpS4886);
      }
      goto join_1551;
      goto joinlet_5966;
      join_1551:;
      _M0L6_2atmpS4854 = _M0L1iS1550 + 1;
      _M0L1iS1550 = _M0L6_2atmpS4854;
      continue;
      joinlet_5966:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt18step__adex__sinexp(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1504,
  float _M0L2dtS1519
) {
  int32_t _M0L1nS1503;
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0L3p__S1505;
  float _M0L2tmS1506;
  float _M0L2vtS1507;
  float _M0L2vrS1508;
  float _M0L2elS1509;
  float _M0L1rS1510;
  float _M0L9dt__slopeS1511;
  float _M0L2twS1512;
  float _M0L1aS1513;
  float _M0L1bS1514;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4817;
  float _M0L2atS1515;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4816;
  float _M0L6tau__aS1516;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4815;
  float _M0L11tabs__constS1517;
  float _M0L6_2atmpS4814;
  int32_t _M0L11tabs__stepsS1518;
  int32_t _M0L7_2abindS1520;
  int32_t _M0L1iS1521;
  #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1503 = _M0L1pS1504->$2;
  _M0L3p__S1505 = _M0L1pS1504->$0;
  _M0L2tmS1506 = _M0L3p__S1505->$5;
  _M0L2vtS1507 = _M0L3p__S1505->$2;
  _M0L2vrS1508 = _M0L3p__S1505->$3;
  _M0L2elS1509 = _M0L3p__S1505->$4;
  _M0L1rS1510 = _M0L3p__S1505->$6;
  _M0L9dt__slopeS1511 = _M0L3p__S1505->$7;
  _M0L2twS1512 = _M0L3p__S1505->$8;
  _M0L1aS1513 = _M0L3p__S1505->$9;
  _M0L1bS1514 = _M0L3p__S1505->$10;
  _M0L5spikeS4817 = _M0L1pS1504->$1;
  _M0L2atS1515 = _M0L5spikeS4817->$0;
  _M0L5spikeS4816 = _M0L1pS1504->$1;
  _M0L6tau__aS1516 = _M0L5spikeS4816->$1;
  _M0L5spikeS4815 = _M0L1pS1504->$1;
  _M0L11tabs__constS1517 = _M0L5spikeS4815->$3;
  _M0L6_2atmpS4814 = _M0L11tabs__constS1517 / _M0L2dtS1519;
  #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L11tabs__stepsS1518 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4814);
  _M0L7_2abindS1520 = 0;
  _M0L1iS1521 = _M0L7_2abindS1520;
  while (1) {
    if (_M0L1iS1521 < _M0L1nS1503) {
      struct _M0TPB5ArrayGfE* _M0L1vS4727 = _M0L1pS1504->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS4729 = _M0L1pS1504->$5;
      float _M0L6_2atmpS4728;
      struct _M0TPB5ArrayGbE* _M0L4fireS4731;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4732;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4735;
      int32_t _M0L6_2atmpS4734;
      int32_t _M0L6_2atmpS4733;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4737;
      int32_t _M0L6_2atmpS4736;
      struct _M0TPB5ArrayGfE* _M0L1wS4738;
      struct _M0TPB5ArrayGfE* _M0L1wS4750;
      float _M0L6_2atmpS4740;
      struct _M0TPB5ArrayGfE* _M0L1vS4749;
      float _M0L6_2atmpS4748;
      float _M0L6_2atmpS4747;
      float _M0L6_2atmpS4744;
      struct _M0TPB5ArrayGfE* _M0L1wS4746;
      float _M0L6_2atmpS4745;
      float _M0L6_2atmpS4743;
      float _M0L6_2atmpS4742;
      float _M0L6_2atmpS4741;
      float _M0L6_2atmpS4739;
      float _M0L9exp__termS1524;
      struct _M0TPB5ArrayGfE* _M0L1vS4751;
      struct _M0TPB5ArrayGfE* _M0L1vS4773;
      float _M0L6_2atmpS4753;
      struct _M0TPB5ArrayGfE* _M0L1vS4772;
      float _M0L6_2atmpS4771;
      float _M0L6_2atmpS4770;
      float _M0L6_2atmpS4769;
      float _M0L6_2atmpS4765;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4768;
      float _M0L6_2atmpS4767;
      float _M0L6_2atmpS4766;
      float _M0L6_2atmpS4761;
      struct _M0TPB5ArrayGfE* _M0L1wS4764;
      float _M0L6_2atmpS4763;
      float _M0L6_2atmpS4762;
      float _M0L6_2atmpS4757;
      struct _M0TPB5ArrayGfE* _M0L1iS4760;
      float _M0L6_2atmpS4759;
      float _M0L6_2atmpS4758;
      float _M0L6_2atmpS4756;
      float _M0L6_2atmpS4755;
      float _M0L6_2atmpS4754;
      float _M0L6_2atmpS4752;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4774;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4782;
      float _M0L6_2atmpS4776;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4781;
      float _M0L6_2atmpS4780;
      float _M0L6_2atmpS4779;
      float _M0L6_2atmpS4778;
      float _M0L6_2atmpS4777;
      float _M0L6_2atmpS4775;
      struct _M0TPB5ArrayGbE* _M0L4fireS4783;
      struct _M0TPB5ArrayGfE* _M0L1vS4786;
      float _M0L6_2atmpS4785;
      int32_t _M0L6_2atmpS4784;
      struct _M0TPB5ArrayGfE* _M0L1vS4787;
      struct _M0TPB5ArrayGbE* _M0L4fireS4789;
      float _M0L6_2atmpS4788;
      struct _M0TPB5ArrayGfE* _M0L1wS4791;
      struct _M0TPB5ArrayGbE* _M0L4fireS4793;
      float _M0L6_2atmpS4792;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4797;
      struct _M0TPB5ArrayGbE* _M0L4fireS4799;
      float _M0L6_2atmpS4798;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4803;
      struct _M0TPB5ArrayGbE* _M0L4fireS4805;
      int32_t _M0L6_2atmpS4804;
      int32_t _M0L6_2atmpS4726;
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4729, _M0L1iS1521)) {
        _M0L6_2atmpS4728 = _M0L2vrS1508;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4730 = _M0L1pS1504->$3;
        #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4728
        = _M0MPC15array5Array2atGfE(_M0L1vS4730, _M0L1iS1521);
      }
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4727, _M0L1iS1521, _M0L6_2atmpS4728);
      _M0L4fireS4731 = _M0L1pS1504->$5;
      #line 212 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4731, _M0L1iS1521, 0);
      _M0L4tabsS4732 = _M0L1pS1504->$7;
      _M0L4tabsS4735 = _M0L1pS1504->$7;
      #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4734
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4735, _M0L1iS1521);
      _M0L6_2atmpS4733 = _M0L6_2atmpS4734 - 1;
      #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4732, _M0L1iS1521, _M0L6_2atmpS4733);
      _M0L4tabsS4737 = _M0L1pS1504->$7;
      #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4736
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4737, _M0L1iS1521);
      if (_M0L6_2atmpS4736 > 0) {
        goto join_1522;
      }
      _M0L1wS4738 = _M0L1pS1504->$4;
      _M0L1wS4750 = _M0L1pS1504->$4;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4740 = _M0MPC15array5Array2atGfE(_M0L1wS4750, _M0L1iS1521);
      _M0L1vS4749 = _M0L1pS1504->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4748 = _M0MPC15array5Array2atGfE(_M0L1vS4749, _M0L1iS1521);
      _M0L6_2atmpS4747 = _M0L6_2atmpS4748 - _M0L2elS1509;
      _M0L6_2atmpS4744 = _M0L1aS1513 * _M0L6_2atmpS4747;
      _M0L1wS4746 = _M0L1pS1504->$4;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4745 = _M0MPC15array5Array2atGfE(_M0L1wS4746, _M0L1iS1521);
      _M0L6_2atmpS4743 = _M0L6_2atmpS4744 - _M0L6_2atmpS4745;
      _M0L6_2atmpS4742 = _M0L2dtS1519 * _M0L6_2atmpS4743;
      _M0L6_2atmpS4741 = _M0L6_2atmpS4742 / _M0L2twS1512;
      _M0L6_2atmpS4739 = _M0L6_2atmpS4740 + _M0L6_2atmpS4741;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4738, _M0L1iS1521, _M0L6_2atmpS4739);
      if (_M0L9dt__slopeS1511 < 0x0p+0f) {
        _M0L9exp__termS1524 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4813 = _M0L1pS1504->$3;
        float _M0L6_2atmpS4810;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4812;
        float _M0L6_2atmpS4811;
        float _M0L6_2atmpS4809;
        float _M0L6_2atmpS4808;
        float _M0L6_2atmpS4807;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4810
        = _M0MPC15array5Array2atGfE(_M0L1vS4813, _M0L1iS1521);
        _M0L9thresholdS4812 = _M0L1pS1504->$6;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4811
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4812, _M0L1iS1521);
        _M0L6_2atmpS4809 = _M0L6_2atmpS4810 - _M0L6_2atmpS4811;
        _M0L6_2atmpS4808 = _M0L6_2atmpS4809 / _M0L9dt__slopeS1511;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4807 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4808);
        _M0L9exp__termS1524 = _M0L9dt__slopeS1511 * _M0L6_2atmpS4807;
      }
      _M0L1vS4751 = _M0L1pS1504->$3;
      _M0L1vS4773 = _M0L1pS1504->$3;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4753 = _M0MPC15array5Array2atGfE(_M0L1vS4773, _M0L1iS1521);
      _M0L1vS4772 = _M0L1pS1504->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4771 = _M0MPC15array5Array2atGfE(_M0L1vS4772, _M0L1iS1521);
      _M0L6_2atmpS4770 = _M0L6_2atmpS4771 - _M0L2elS1509;
      _M0L6_2atmpS4769 = -_M0L6_2atmpS4770;
      _M0L6_2atmpS4765 = _M0L6_2atmpS4769 + _M0L9exp__termS1524;
      _M0L9syn__currS4768 = _M0L1pS1504->$9;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4767
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS4768, _M0L1iS1521);
      _M0L6_2atmpS4766 = _M0L1rS1510 * _M0L6_2atmpS4767;
      _M0L6_2atmpS4761 = _M0L6_2atmpS4765 - _M0L6_2atmpS4766;
      _M0L1wS4764 = _M0L1pS1504->$4;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4763 = _M0MPC15array5Array2atGfE(_M0L1wS4764, _M0L1iS1521);
      _M0L6_2atmpS4762 = _M0L1rS1510 * _M0L6_2atmpS4763;
      _M0L6_2atmpS4757 = _M0L6_2atmpS4761 - _M0L6_2atmpS4762;
      _M0L1iS4760 = _M0L1pS1504->$8;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4759 = _M0MPC15array5Array2atGfE(_M0L1iS4760, _M0L1iS1521);
      _M0L6_2atmpS4758 = _M0L1rS1510 * _M0L6_2atmpS4759;
      _M0L6_2atmpS4756 = _M0L6_2atmpS4757 + _M0L6_2atmpS4758;
      _M0L6_2atmpS4755 = _M0L2dtS1519 * _M0L6_2atmpS4756;
      _M0L6_2atmpS4754 = _M0L6_2atmpS4755 / _M0L2tmS1506;
      _M0L6_2atmpS4752 = _M0L6_2atmpS4753 + _M0L6_2atmpS4754;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4751, _M0L1iS1521, _M0L6_2atmpS4752);
      _M0L9thresholdS4774 = _M0L1pS1504->$6;
      _M0L9thresholdS4782 = _M0L1pS1504->$6;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4776
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4782, _M0L1iS1521);
      _M0L9thresholdS4781 = _M0L1pS1504->$6;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4780
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4781, _M0L1iS1521);
      _M0L6_2atmpS4779 = _M0L2vtS1507 - _M0L6_2atmpS4780;
      _M0L6_2atmpS4778 = _M0L2dtS1519 * _M0L6_2atmpS4779;
      _M0L6_2atmpS4777 = _M0L6_2atmpS4778 / _M0L6tau__aS1516;
      _M0L6_2atmpS4775 = _M0L6_2atmpS4776 + _M0L6_2atmpS4777;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4774, _M0L1iS1521, _M0L6_2atmpS4775);
      _M0L4fireS4783 = _M0L1pS1504->$5;
      _M0L1vS4786 = _M0L1pS1504->$3;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4785 = _M0MPC15array5Array2atGfE(_M0L1vS4786, _M0L1iS1521);
      _M0L6_2atmpS4784 = _M0L6_2atmpS4785 >= 0x0p+0f;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4783, _M0L1iS1521, _M0L6_2atmpS4784);
      _M0L1vS4787 = _M0L1pS1504->$3;
      _M0L4fireS4789 = _M0L1pS1504->$5;
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4789, _M0L1iS1521)) {
        _M0L6_2atmpS4788 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4790 = _M0L1pS1504->$3;
        #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4788
        = _M0MPC15array5Array2atGfE(_M0L1vS4790, _M0L1iS1521);
      }
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4787, _M0L1iS1521, _M0L6_2atmpS4788);
      _M0L1wS4791 = _M0L1pS1504->$4;
      _M0L4fireS4793 = _M0L1pS1504->$5;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4793, _M0L1iS1521)) {
        struct _M0TPB5ArrayGfE* _M0L1wS4795 = _M0L1pS1504->$4;
        float _M0L6_2atmpS4794;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4794
        = _M0MPC15array5Array2atGfE(_M0L1wS4795, _M0L1iS1521);
        _M0L6_2atmpS4792 = _M0L6_2atmpS4794 + _M0L1bS1514;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS4796 = _M0L1pS1504->$4;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4792
        = _M0MPC15array5Array2atGfE(_M0L1wS4796, _M0L1iS1521);
      }
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4791, _M0L1iS1521, _M0L6_2atmpS4792);
      _M0L9thresholdS4797 = _M0L1pS1504->$6;
      _M0L4fireS4799 = _M0L1pS1504->$5;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4799, _M0L1iS1521)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4801 = _M0L1pS1504->$6;
        float _M0L6_2atmpS4800;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4800
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4801, _M0L1iS1521);
        _M0L6_2atmpS4798 = _M0L6_2atmpS4800 + _M0L2atS1515;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4802 = _M0L1pS1504->$6;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4798
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4802, _M0L1iS1521);
      }
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4797, _M0L1iS1521, _M0L6_2atmpS4798);
      _M0L4tabsS4803 = _M0L1pS1504->$7;
      _M0L4fireS4805 = _M0L1pS1504->$5;
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4805, _M0L1iS1521)) {
        _M0L6_2atmpS4804 = _M0L11tabs__stepsS1518;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4806 = _M0L1pS1504->$7;
        #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4804
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4806, _M0L1iS1521);
      }
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4803, _M0L1iS1521, _M0L6_2atmpS4804);
      goto join_1522;
      goto joinlet_5968;
      join_1522:;
      _M0L6_2atmpS4726 = _M0L1iS1521 + 1;
      _M0L1iS1521 = _M0L6_2atmpS4726;
      continue;
      joinlet_5968:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1499
) {
  int32_t _M0L1nS1498;
  int32_t _M0L7_2abindS1500;
  int32_t _M0L1iS1501;
  #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1498 = _M0L1pS1499->$2;
  _M0L7_2abindS1500 = 0;
  _M0L1iS1501 = _M0L7_2abindS1500;
  while (1) {
    if (_M0L1iS1501 < _M0L1nS1498) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4703 = _M0L1pS1499->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS4724 = _M0L1pS1499->$10;
      float _M0L6_2atmpS4719;
      struct _M0TPB5ArrayGfE* _M0L1vS4723;
      float _M0L6_2atmpS4721;
      float _M0L4e__eS4722;
      float _M0L6_2atmpS4720;
      float _M0L6_2atmpS4716;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS4718;
      float _M0L6_2atmpS4717;
      float _M0L6_2atmpS4705;
      struct _M0TPB5ArrayGfE* _M0L2giS4715;
      float _M0L6_2atmpS4710;
      struct _M0TPB5ArrayGfE* _M0L1vS4714;
      float _M0L6_2atmpS4712;
      float _M0L4e__iS4713;
      float _M0L6_2atmpS4711;
      float _M0L6_2atmpS4707;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS4709;
      float _M0L6_2atmpS4708;
      float _M0L6_2atmpS4706;
      float _M0L6_2atmpS4704;
      int32_t _M0L6_2atmpS4725;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4719 = _M0MPC15array5Array2atGfE(_M0L2geS4724, _M0L1iS1501);
      _M0L1vS4723 = _M0L1pS1499->$3;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4721 = _M0MPC15array5Array2atGfE(_M0L1vS4723, _M0L1iS1501);
      _M0L4e__eS4722 = _M0L1pS1499->$16;
      _M0L6_2atmpS4720 = _M0L6_2atmpS4721 - _M0L4e__eS4722;
      _M0L6_2atmpS4716 = _M0L6_2atmpS4719 * _M0L6_2atmpS4720;
      _M0L7gsyn__eS4718 = _M0L1pS1499->$14;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4717
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS4718, _M0L1iS1501);
      _M0L6_2atmpS4705 = _M0L6_2atmpS4716 * _M0L6_2atmpS4717;
      _M0L2giS4715 = _M0L1pS1499->$11;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4710 = _M0MPC15array5Array2atGfE(_M0L2giS4715, _M0L1iS1501);
      _M0L1vS4714 = _M0L1pS1499->$3;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4712 = _M0MPC15array5Array2atGfE(_M0L1vS4714, _M0L1iS1501);
      _M0L4e__iS4713 = _M0L1pS1499->$17;
      _M0L6_2atmpS4711 = _M0L6_2atmpS4712 - _M0L4e__iS4713;
      _M0L6_2atmpS4707 = _M0L6_2atmpS4710 * _M0L6_2atmpS4711;
      _M0L7gsyn__iS4709 = _M0L1pS1499->$15;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4708
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS4709, _M0L1iS1501);
      _M0L6_2atmpS4706 = _M0L6_2atmpS4707 * _M0L6_2atmpS4708;
      _M0L6_2atmpS4704 = _M0L6_2atmpS4705 + _M0L6_2atmpS4706;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS4703, _M0L1iS1501, _M0L6_2atmpS4704);
      _M0L6_2atmpS4725 = _M0L1iS1501 + 1;
      _M0L1iS1501 = _M0L6_2atmpS4725;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1488,
  float _M0L2dtS1493
) {
  int32_t _M0L1nS1487;
  float _M0L6tau__eS1489;
  float _M0L6tau__iS1490;
  int32_t _M0L7_2abindS1491;
  int32_t _M0L1iS1492;
  int32_t _M0L7_2abindS1495;
  int32_t _M0L1iS1496;
  #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1487 = _M0L1pS1488->$2;
  _M0L6tau__eS1489 = _M0L1pS1488->$18;
  _M0L6tau__iS1490 = _M0L1pS1488->$19;
  _M0L7_2abindS1491 = 0;
  _M0L1iS1492 = _M0L7_2abindS1491;
  while (1) {
    if (_M0L1iS1492 < _M0L1nS1487) {
      struct _M0TPB5ArrayGfE* _M0L2geS4669 = _M0L1pS1488->$10;
      struct _M0TPB5ArrayGfE* _M0L2geS4674 = _M0L1pS1488->$10;
      float _M0L6_2atmpS4671;
      struct _M0TPB5ArrayGfE* _M0L3gluS4673;
      float _M0L6_2atmpS4672;
      float _M0L6_2atmpS4670;
      struct _M0TPB5ArrayGfE* _M0L2giS4675;
      struct _M0TPB5ArrayGfE* _M0L2giS4680;
      float _M0L6_2atmpS4677;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4679;
      float _M0L6_2atmpS4678;
      float _M0L6_2atmpS4676;
      struct _M0TPB5ArrayGfE* _M0L2geS4681;
      struct _M0TPB5ArrayGfE* _M0L2geS4689;
      float _M0L6_2atmpS4683;
      struct _M0TPB5ArrayGfE* _M0L2geS4688;
      float _M0L6_2atmpS4687;
      float _M0L6_2atmpS4686;
      float _M0L6_2atmpS4685;
      float _M0L6_2atmpS4684;
      float _M0L6_2atmpS4682;
      struct _M0TPB5ArrayGfE* _M0L2giS4690;
      struct _M0TPB5ArrayGfE* _M0L2giS4698;
      float _M0L6_2atmpS4692;
      struct _M0TPB5ArrayGfE* _M0L2giS4697;
      float _M0L6_2atmpS4696;
      float _M0L6_2atmpS4695;
      float _M0L6_2atmpS4694;
      float _M0L6_2atmpS4693;
      float _M0L6_2atmpS4691;
      int32_t _M0L6_2atmpS4699;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4671 = _M0MPC15array5Array2atGfE(_M0L2geS4674, _M0L1iS1492);
      _M0L3gluS4673 = _M0L1pS1488->$12;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4672
      = _M0MPC15array5Array2atGfE(_M0L3gluS4673, _M0L1iS1492);
      _M0L6_2atmpS4670 = _M0L6_2atmpS4671 + _M0L6_2atmpS4672;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4669, _M0L1iS1492, _M0L6_2atmpS4670);
      _M0L2giS4675 = _M0L1pS1488->$11;
      _M0L2giS4680 = _M0L1pS1488->$11;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4677 = _M0MPC15array5Array2atGfE(_M0L2giS4680, _M0L1iS1492);
      _M0L4gabaS4679 = _M0L1pS1488->$13;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4678
      = _M0MPC15array5Array2atGfE(_M0L4gabaS4679, _M0L1iS1492);
      _M0L6_2atmpS4676 = _M0L6_2atmpS4677 + _M0L6_2atmpS4678;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4675, _M0L1iS1492, _M0L6_2atmpS4676);
      _M0L2geS4681 = _M0L1pS1488->$10;
      _M0L2geS4689 = _M0L1pS1488->$10;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4683 = _M0MPC15array5Array2atGfE(_M0L2geS4689, _M0L1iS1492);
      _M0L2geS4688 = _M0L1pS1488->$10;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4687 = _M0MPC15array5Array2atGfE(_M0L2geS4688, _M0L1iS1492);
      _M0L6_2atmpS4686 = -_M0L6_2atmpS4687;
      _M0L6_2atmpS4685 = _M0L6_2atmpS4686 / _M0L6tau__eS1489;
      _M0L6_2atmpS4684 = _M0L2dtS1493 * _M0L6_2atmpS4685;
      _M0L6_2atmpS4682 = _M0L6_2atmpS4683 + _M0L6_2atmpS4684;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4681, _M0L1iS1492, _M0L6_2atmpS4682);
      _M0L2giS4690 = _M0L1pS1488->$11;
      _M0L2giS4698 = _M0L1pS1488->$11;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4692 = _M0MPC15array5Array2atGfE(_M0L2giS4698, _M0L1iS1492);
      _M0L2giS4697 = _M0L1pS1488->$11;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4696 = _M0MPC15array5Array2atGfE(_M0L2giS4697, _M0L1iS1492);
      _M0L6_2atmpS4695 = -_M0L6_2atmpS4696;
      _M0L6_2atmpS4694 = _M0L6_2atmpS4695 / _M0L6tau__iS1490;
      _M0L6_2atmpS4693 = _M0L2dtS1493 * _M0L6_2atmpS4694;
      _M0L6_2atmpS4691 = _M0L6_2atmpS4692 + _M0L6_2atmpS4693;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4690, _M0L1iS1492, _M0L6_2atmpS4691);
      _M0L6_2atmpS4699 = _M0L1iS1492 + 1;
      _M0L1iS1492 = _M0L6_2atmpS4699;
      continue;
    }
    break;
  }
  _M0L7_2abindS1495 = 0;
  _M0L1iS1496 = _M0L7_2abindS1495;
  while (1) {
    if (_M0L1iS1496 < _M0L1nS1487) {
      struct _M0TPB5ArrayGfE* _M0L3gluS4700 = _M0L1pS1488->$12;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4701;
      int32_t _M0L6_2atmpS4702;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS4700, _M0L1iS1496, 0x0p+0f);
      _M0L4gabaS4701 = _M0L1pS1488->$13;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS4701, _M0L1iS1496, 0x0p+0f);
      _M0L6_2atmpS4702 = _M0L1iS1496 + 1;
      _M0L1iS1496 = _M0L6_2atmpS4702;
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
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1466,
  float _M0L2dtS1481
) {
  int32_t _M0L1nS1465;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L3p__S1467;
  float _M0L2tmS1468;
  float _M0L2vtS1469;
  float _M0L2vrS1470;
  float _M0L2elS1471;
  float _M0L1rS1472;
  float _M0L9dt__slopeS1473;
  float _M0L2twS1474;
  float _M0L1aS1475;
  float _M0L1bS1476;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4668;
  float _M0L2atS1477;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4667;
  float _M0L6tau__aS1478;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4666;
  float _M0L11tabs__constS1479;
  float _M0L6_2atmpS4665;
  int32_t _M0L11tabs__stepsS1480;
  int32_t _M0L7_2abindS1482;
  int32_t _M0L1iS1483;
  #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1465 = _M0L1pS1466->$2;
  _M0L3p__S1467 = _M0L1pS1466->$0;
  _M0L2tmS1468 = _M0L3p__S1467->$5;
  _M0L2vtS1469 = _M0L3p__S1467->$2;
  _M0L2vrS1470 = _M0L3p__S1467->$3;
  _M0L2elS1471 = _M0L3p__S1467->$4;
  _M0L1rS1472 = _M0L3p__S1467->$6;
  _M0L9dt__slopeS1473 = _M0L3p__S1467->$7;
  _M0L2twS1474 = _M0L3p__S1467->$8;
  _M0L1aS1475 = _M0L3p__S1467->$9;
  _M0L1bS1476 = _M0L3p__S1467->$10;
  _M0L5spikeS4668 = _M0L1pS1466->$1;
  _M0L2atS1477 = _M0L5spikeS4668->$0;
  _M0L5spikeS4667 = _M0L1pS1466->$1;
  _M0L6tau__aS1478 = _M0L5spikeS4667->$1;
  _M0L5spikeS4666 = _M0L1pS1466->$1;
  _M0L11tabs__constS1479 = _M0L5spikeS4666->$3;
  _M0L6_2atmpS4665 = _M0L11tabs__constS1479 / _M0L2dtS1481;
  #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L11tabs__stepsS1480 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4665);
  _M0L7_2abindS1482 = 0;
  _M0L1iS1483 = _M0L7_2abindS1482;
  while (1) {
    if (_M0L1iS1483 < _M0L1nS1465) {
      struct _M0TPB5ArrayGfE* _M0L1vS4578 = _M0L1pS1466->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS4580 = _M0L1pS1466->$5;
      float _M0L6_2atmpS4579;
      struct _M0TPB5ArrayGbE* _M0L4fireS4582;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4583;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4586;
      int32_t _M0L6_2atmpS4585;
      int32_t _M0L6_2atmpS4584;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4588;
      int32_t _M0L6_2atmpS4587;
      struct _M0TPB5ArrayGfE* _M0L1wS4589;
      struct _M0TPB5ArrayGfE* _M0L1wS4601;
      float _M0L6_2atmpS4591;
      struct _M0TPB5ArrayGfE* _M0L1vS4600;
      float _M0L6_2atmpS4599;
      float _M0L6_2atmpS4598;
      float _M0L6_2atmpS4595;
      struct _M0TPB5ArrayGfE* _M0L1wS4597;
      float _M0L6_2atmpS4596;
      float _M0L6_2atmpS4594;
      float _M0L6_2atmpS4593;
      float _M0L6_2atmpS4592;
      float _M0L6_2atmpS4590;
      float _M0L9exp__termS1486;
      struct _M0TPB5ArrayGfE* _M0L1vS4602;
      struct _M0TPB5ArrayGfE* _M0L1vS4624;
      float _M0L6_2atmpS4604;
      struct _M0TPB5ArrayGfE* _M0L1vS4623;
      float _M0L6_2atmpS4622;
      float _M0L6_2atmpS4621;
      float _M0L6_2atmpS4620;
      float _M0L6_2atmpS4616;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4619;
      float _M0L6_2atmpS4618;
      float _M0L6_2atmpS4617;
      float _M0L6_2atmpS4612;
      struct _M0TPB5ArrayGfE* _M0L1wS4615;
      float _M0L6_2atmpS4614;
      float _M0L6_2atmpS4613;
      float _M0L6_2atmpS4608;
      struct _M0TPB5ArrayGfE* _M0L1iS4611;
      float _M0L6_2atmpS4610;
      float _M0L6_2atmpS4609;
      float _M0L6_2atmpS4607;
      float _M0L6_2atmpS4606;
      float _M0L6_2atmpS4605;
      float _M0L6_2atmpS4603;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4625;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4633;
      float _M0L6_2atmpS4627;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4632;
      float _M0L6_2atmpS4631;
      float _M0L6_2atmpS4630;
      float _M0L6_2atmpS4629;
      float _M0L6_2atmpS4628;
      float _M0L6_2atmpS4626;
      struct _M0TPB5ArrayGbE* _M0L4fireS4634;
      struct _M0TPB5ArrayGfE* _M0L1vS4637;
      float _M0L6_2atmpS4636;
      int32_t _M0L6_2atmpS4635;
      struct _M0TPB5ArrayGfE* _M0L1vS4638;
      struct _M0TPB5ArrayGbE* _M0L4fireS4640;
      float _M0L6_2atmpS4639;
      struct _M0TPB5ArrayGfE* _M0L1wS4642;
      struct _M0TPB5ArrayGbE* _M0L4fireS4644;
      float _M0L6_2atmpS4643;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4648;
      struct _M0TPB5ArrayGbE* _M0L4fireS4650;
      float _M0L6_2atmpS4649;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4654;
      struct _M0TPB5ArrayGbE* _M0L4fireS4656;
      int32_t _M0L6_2atmpS4655;
      int32_t _M0L6_2atmpS4577;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4580, _M0L1iS1483)) {
        _M0L6_2atmpS4579 = _M0L2vrS1470;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4581 = _M0L1pS1466->$3;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4579
        = _M0MPC15array5Array2atGfE(_M0L1vS4581, _M0L1iS1483);
      }
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4578, _M0L1iS1483, _M0L6_2atmpS4579);
      _M0L4fireS4582 = _M0L1pS1466->$5;
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4582, _M0L1iS1483, 0);
      _M0L4tabsS4583 = _M0L1pS1466->$7;
      _M0L4tabsS4586 = _M0L1pS1466->$7;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4585
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4586, _M0L1iS1483);
      _M0L6_2atmpS4584 = _M0L6_2atmpS4585 - 1;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4583, _M0L1iS1483, _M0L6_2atmpS4584);
      _M0L4tabsS4588 = _M0L1pS1466->$7;
      #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4587
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4588, _M0L1iS1483);
      if (_M0L6_2atmpS4587 > 0) {
        goto join_1484;
      }
      _M0L1wS4589 = _M0L1pS1466->$4;
      _M0L1wS4601 = _M0L1pS1466->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4591 = _M0MPC15array5Array2atGfE(_M0L1wS4601, _M0L1iS1483);
      _M0L1vS4600 = _M0L1pS1466->$3;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4599 = _M0MPC15array5Array2atGfE(_M0L1vS4600, _M0L1iS1483);
      _M0L6_2atmpS4598 = _M0L6_2atmpS4599 - _M0L2elS1471;
      _M0L6_2atmpS4595 = _M0L1aS1475 * _M0L6_2atmpS4598;
      _M0L1wS4597 = _M0L1pS1466->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4596 = _M0MPC15array5Array2atGfE(_M0L1wS4597, _M0L1iS1483);
      _M0L6_2atmpS4594 = _M0L6_2atmpS4595 - _M0L6_2atmpS4596;
      _M0L6_2atmpS4593 = _M0L2dtS1481 * _M0L6_2atmpS4594;
      _M0L6_2atmpS4592 = _M0L6_2atmpS4593 / _M0L2twS1474;
      _M0L6_2atmpS4590 = _M0L6_2atmpS4591 + _M0L6_2atmpS4592;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4589, _M0L1iS1483, _M0L6_2atmpS4590);
      if (_M0L9dt__slopeS1473 < 0x0p+0f) {
        _M0L9exp__termS1486 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4664 = _M0L1pS1466->$3;
        float _M0L6_2atmpS4661;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4663;
        float _M0L6_2atmpS4662;
        float _M0L6_2atmpS4660;
        float _M0L6_2atmpS4659;
        float _M0L6_2atmpS4658;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4661
        = _M0MPC15array5Array2atGfE(_M0L1vS4664, _M0L1iS1483);
        _M0L9thresholdS4663 = _M0L1pS1466->$6;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4662
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4663, _M0L1iS1483);
        _M0L6_2atmpS4660 = _M0L6_2atmpS4661 - _M0L6_2atmpS4662;
        _M0L6_2atmpS4659 = _M0L6_2atmpS4660 / _M0L9dt__slopeS1473;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4658 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4659);
        _M0L9exp__termS1486 = _M0L9dt__slopeS1473 * _M0L6_2atmpS4658;
      }
      _M0L1vS4602 = _M0L1pS1466->$3;
      _M0L1vS4624 = _M0L1pS1466->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4604 = _M0MPC15array5Array2atGfE(_M0L1vS4624, _M0L1iS1483);
      _M0L1vS4623 = _M0L1pS1466->$3;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4622 = _M0MPC15array5Array2atGfE(_M0L1vS4623, _M0L1iS1483);
      _M0L6_2atmpS4621 = _M0L6_2atmpS4622 - _M0L2elS1471;
      _M0L6_2atmpS4620 = -_M0L6_2atmpS4621;
      _M0L6_2atmpS4616 = _M0L6_2atmpS4620 + _M0L9exp__termS1486;
      _M0L9syn__currS4619 = _M0L1pS1466->$9;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4618
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS4619, _M0L1iS1483);
      _M0L6_2atmpS4617 = _M0L1rS1472 * _M0L6_2atmpS4618;
      _M0L6_2atmpS4612 = _M0L6_2atmpS4616 - _M0L6_2atmpS4617;
      _M0L1wS4615 = _M0L1pS1466->$4;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4614 = _M0MPC15array5Array2atGfE(_M0L1wS4615, _M0L1iS1483);
      _M0L6_2atmpS4613 = _M0L1rS1472 * _M0L6_2atmpS4614;
      _M0L6_2atmpS4608 = _M0L6_2atmpS4612 - _M0L6_2atmpS4613;
      _M0L1iS4611 = _M0L1pS1466->$8;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4610 = _M0MPC15array5Array2atGfE(_M0L1iS4611, _M0L1iS1483);
      _M0L6_2atmpS4609 = _M0L1rS1472 * _M0L6_2atmpS4610;
      _M0L6_2atmpS4607 = _M0L6_2atmpS4608 + _M0L6_2atmpS4609;
      _M0L6_2atmpS4606 = _M0L2dtS1481 * _M0L6_2atmpS4607;
      _M0L6_2atmpS4605 = _M0L6_2atmpS4606 / _M0L2tmS1468;
      _M0L6_2atmpS4603 = _M0L6_2atmpS4604 + _M0L6_2atmpS4605;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4602, _M0L1iS1483, _M0L6_2atmpS4603);
      _M0L9thresholdS4625 = _M0L1pS1466->$6;
      _M0L9thresholdS4633 = _M0L1pS1466->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4627
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4633, _M0L1iS1483);
      _M0L9thresholdS4632 = _M0L1pS1466->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4631
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4632, _M0L1iS1483);
      _M0L6_2atmpS4630 = _M0L2vtS1469 - _M0L6_2atmpS4631;
      _M0L6_2atmpS4629 = _M0L2dtS1481 * _M0L6_2atmpS4630;
      _M0L6_2atmpS4628 = _M0L6_2atmpS4629 / _M0L6tau__aS1478;
      _M0L6_2atmpS4626 = _M0L6_2atmpS4627 + _M0L6_2atmpS4628;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4625, _M0L1iS1483, _M0L6_2atmpS4626);
      _M0L4fireS4634 = _M0L1pS1466->$5;
      _M0L1vS4637 = _M0L1pS1466->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4636 = _M0MPC15array5Array2atGfE(_M0L1vS4637, _M0L1iS1483);
      _M0L6_2atmpS4635 = _M0L6_2atmpS4636 >= 0x0p+0f;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4634, _M0L1iS1483, _M0L6_2atmpS4635);
      _M0L1vS4638 = _M0L1pS1466->$3;
      _M0L4fireS4640 = _M0L1pS1466->$5;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4640, _M0L1iS1483)) {
        _M0L6_2atmpS4639 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4641 = _M0L1pS1466->$3;
        #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4639
        = _M0MPC15array5Array2atGfE(_M0L1vS4641, _M0L1iS1483);
      }
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4638, _M0L1iS1483, _M0L6_2atmpS4639);
      _M0L1wS4642 = _M0L1pS1466->$4;
      _M0L4fireS4644 = _M0L1pS1466->$5;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4644, _M0L1iS1483)) {
        struct _M0TPB5ArrayGfE* _M0L1wS4646 = _M0L1pS1466->$4;
        float _M0L6_2atmpS4645;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4645
        = _M0MPC15array5Array2atGfE(_M0L1wS4646, _M0L1iS1483);
        _M0L6_2atmpS4643 = _M0L6_2atmpS4645 + _M0L1bS1476;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS4647 = _M0L1pS1466->$4;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4643
        = _M0MPC15array5Array2atGfE(_M0L1wS4647, _M0L1iS1483);
      }
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4642, _M0L1iS1483, _M0L6_2atmpS4643);
      _M0L9thresholdS4648 = _M0L1pS1466->$6;
      _M0L4fireS4650 = _M0L1pS1466->$5;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4650, _M0L1iS1483)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4652 = _M0L1pS1466->$6;
        float _M0L6_2atmpS4651;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4651
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4652, _M0L1iS1483);
        _M0L6_2atmpS4649 = _M0L6_2atmpS4651 + _M0L2atS1477;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4653 = _M0L1pS1466->$6;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4649
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4653, _M0L1iS1483);
      }
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4648, _M0L1iS1483, _M0L6_2atmpS4649);
      _M0L4tabsS4654 = _M0L1pS1466->$7;
      _M0L4fireS4656 = _M0L1pS1466->$5;
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4656, _M0L1iS1483)) {
        _M0L6_2atmpS4655 = _M0L11tabs__stepsS1480;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4657 = _M0L1pS1466->$7;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4655
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4657, _M0L1iS1483);
      }
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4654, _M0L1iS1483, _M0L6_2atmpS4655);
      goto join_1484;
      goto joinlet_5973;
      join_1484:;
      _M0L6_2atmpS4577 = _M0L1iS1483 + 1;
      _M0L1iS1483 = _M0L6_2atmpS4577;
      continue;
      joinlet_5973:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23adex__synaptic__current(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1461
) {
  int32_t _M0L1nS1460;
  int32_t _M0L7_2abindS1462;
  int32_t _M0L1iS1463;
  #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1460 = _M0L1pS1461->$2;
  _M0L7_2abindS1462 = 0;
  _M0L1iS1463 = _M0L7_2abindS1462;
  while (1) {
    if (_M0L1iS1463 < _M0L1nS1460) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4554 = _M0L1pS1461->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS4575 = _M0L1pS1461->$10;
      float _M0L6_2atmpS4570;
      struct _M0TPB5ArrayGfE* _M0L1vS4574;
      float _M0L6_2atmpS4572;
      float _M0L4e__eS4573;
      float _M0L6_2atmpS4571;
      float _M0L6_2atmpS4567;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS4569;
      float _M0L6_2atmpS4568;
      float _M0L6_2atmpS4556;
      struct _M0TPB5ArrayGfE* _M0L2giS4566;
      float _M0L6_2atmpS4561;
      struct _M0TPB5ArrayGfE* _M0L1vS4565;
      float _M0L6_2atmpS4563;
      float _M0L4e__iS4564;
      float _M0L6_2atmpS4562;
      float _M0L6_2atmpS4558;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS4560;
      float _M0L6_2atmpS4559;
      float _M0L6_2atmpS4557;
      float _M0L6_2atmpS4555;
      int32_t _M0L6_2atmpS4576;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4570 = _M0MPC15array5Array2atGfE(_M0L2geS4575, _M0L1iS1463);
      _M0L1vS4574 = _M0L1pS1461->$3;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4572 = _M0MPC15array5Array2atGfE(_M0L1vS4574, _M0L1iS1463);
      _M0L4e__eS4573 = _M0L1pS1461->$18;
      _M0L6_2atmpS4571 = _M0L6_2atmpS4572 - _M0L4e__eS4573;
      _M0L6_2atmpS4567 = _M0L6_2atmpS4570 * _M0L6_2atmpS4571;
      _M0L7gsyn__eS4569 = _M0L1pS1461->$16;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4568
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS4569, _M0L1iS1463);
      _M0L6_2atmpS4556 = _M0L6_2atmpS4567 * _M0L6_2atmpS4568;
      _M0L2giS4566 = _M0L1pS1461->$11;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4561 = _M0MPC15array5Array2atGfE(_M0L2giS4566, _M0L1iS1463);
      _M0L1vS4565 = _M0L1pS1461->$3;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4563 = _M0MPC15array5Array2atGfE(_M0L1vS4565, _M0L1iS1463);
      _M0L4e__iS4564 = _M0L1pS1461->$19;
      _M0L6_2atmpS4562 = _M0L6_2atmpS4563 - _M0L4e__iS4564;
      _M0L6_2atmpS4558 = _M0L6_2atmpS4561 * _M0L6_2atmpS4562;
      _M0L7gsyn__iS4560 = _M0L1pS1461->$17;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4559
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS4560, _M0L1iS1463);
      _M0L6_2atmpS4557 = _M0L6_2atmpS4558 * _M0L6_2atmpS4559;
      _M0L6_2atmpS4555 = _M0L6_2atmpS4556 + _M0L6_2atmpS4557;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS4554, _M0L1iS1463, _M0L6_2atmpS4555);
      _M0L6_2atmpS4576 = _M0L1iS1463 + 1;
      _M0L1iS1463 = _M0L6_2atmpS4576;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20adex__step__synapses(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1452,
  float _M0L2dtS1455
) {
  int32_t _M0L1nS1451;
  int32_t _M0L7_2abindS1453;
  int32_t _M0L1iS1454;
  int32_t _M0L7_2abindS1457;
  int32_t _M0L1iS1458;
  #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1451 = _M0L1pS1452->$2;
  _M0L7_2abindS1453 = 0;
  _M0L1iS1454 = _M0L7_2abindS1453;
  while (1) {
    if (_M0L1iS1454 < _M0L1nS1451) {
      struct _M0TPB5ArrayGfE* _M0L2heS4492 = _M0L1pS1452->$12;
      struct _M0TPB5ArrayGfE* _M0L2heS4497 = _M0L1pS1452->$12;
      float _M0L6_2atmpS4494;
      struct _M0TPB5ArrayGfE* _M0L3gluS4496;
      float _M0L6_2atmpS4495;
      float _M0L6_2atmpS4493;
      struct _M0TPB5ArrayGfE* _M0L2hiS4498;
      struct _M0TPB5ArrayGfE* _M0L2hiS4503;
      float _M0L6_2atmpS4500;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4502;
      float _M0L6_2atmpS4501;
      float _M0L6_2atmpS4499;
      struct _M0TPB5ArrayGfE* _M0L2geS4504;
      struct _M0TPB5ArrayGfE* _M0L2geS4516;
      float _M0L6_2atmpS4506;
      struct _M0TPB5ArrayGfE* _M0L2geS4515;
      float _M0L6_2atmpS4514;
      float _M0L6_2atmpS4512;
      float _M0L3tdeS4513;
      float _M0L6_2atmpS4509;
      struct _M0TPB5ArrayGfE* _M0L2heS4511;
      float _M0L6_2atmpS4510;
      float _M0L6_2atmpS4508;
      float _M0L6_2atmpS4507;
      float _M0L6_2atmpS4505;
      struct _M0TPB5ArrayGfE* _M0L2heS4517;
      struct _M0TPB5ArrayGfE* _M0L2heS4526;
      float _M0L6_2atmpS4519;
      struct _M0TPB5ArrayGfE* _M0L2heS4525;
      float _M0L6_2atmpS4524;
      float _M0L6_2atmpS4522;
      float _M0L3treS4523;
      float _M0L6_2atmpS4521;
      float _M0L6_2atmpS4520;
      float _M0L6_2atmpS4518;
      struct _M0TPB5ArrayGfE* _M0L2giS4527;
      struct _M0TPB5ArrayGfE* _M0L2giS4539;
      float _M0L6_2atmpS4529;
      struct _M0TPB5ArrayGfE* _M0L2giS4538;
      float _M0L6_2atmpS4537;
      float _M0L6_2atmpS4535;
      float _M0L3tdiS4536;
      float _M0L6_2atmpS4532;
      struct _M0TPB5ArrayGfE* _M0L2hiS4534;
      float _M0L6_2atmpS4533;
      float _M0L6_2atmpS4531;
      float _M0L6_2atmpS4530;
      float _M0L6_2atmpS4528;
      struct _M0TPB5ArrayGfE* _M0L2hiS4540;
      struct _M0TPB5ArrayGfE* _M0L2hiS4549;
      float _M0L6_2atmpS4542;
      struct _M0TPB5ArrayGfE* _M0L2hiS4548;
      float _M0L6_2atmpS4547;
      float _M0L6_2atmpS4545;
      float _M0L3triS4546;
      float _M0L6_2atmpS4544;
      float _M0L6_2atmpS4543;
      float _M0L6_2atmpS4541;
      int32_t _M0L6_2atmpS4550;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4494 = _M0MPC15array5Array2atGfE(_M0L2heS4497, _M0L1iS1454);
      _M0L3gluS4496 = _M0L1pS1452->$14;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4495
      = _M0MPC15array5Array2atGfE(_M0L3gluS4496, _M0L1iS1454);
      _M0L6_2atmpS4493 = _M0L6_2atmpS4494 + _M0L6_2atmpS4495;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4492, _M0L1iS1454, _M0L6_2atmpS4493);
      _M0L2hiS4498 = _M0L1pS1452->$13;
      _M0L2hiS4503 = _M0L1pS1452->$13;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4500 = _M0MPC15array5Array2atGfE(_M0L2hiS4503, _M0L1iS1454);
      _M0L4gabaS4502 = _M0L1pS1452->$15;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4501
      = _M0MPC15array5Array2atGfE(_M0L4gabaS4502, _M0L1iS1454);
      _M0L6_2atmpS4499 = _M0L6_2atmpS4500 + _M0L6_2atmpS4501;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4498, _M0L1iS1454, _M0L6_2atmpS4499);
      _M0L2geS4504 = _M0L1pS1452->$10;
      _M0L2geS4516 = _M0L1pS1452->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4506 = _M0MPC15array5Array2atGfE(_M0L2geS4516, _M0L1iS1454);
      _M0L2geS4515 = _M0L1pS1452->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4514 = _M0MPC15array5Array2atGfE(_M0L2geS4515, _M0L1iS1454);
      _M0L6_2atmpS4512 = -_M0L6_2atmpS4514;
      _M0L3tdeS4513 = _M0L1pS1452->$21;
      _M0L6_2atmpS4509 = _M0L6_2atmpS4512 / _M0L3tdeS4513;
      _M0L2heS4511 = _M0L1pS1452->$12;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4510 = _M0MPC15array5Array2atGfE(_M0L2heS4511, _M0L1iS1454);
      _M0L6_2atmpS4508 = _M0L6_2atmpS4509 + _M0L6_2atmpS4510;
      _M0L6_2atmpS4507 = _M0L2dtS1455 * _M0L6_2atmpS4508;
      _M0L6_2atmpS4505 = _M0L6_2atmpS4506 + _M0L6_2atmpS4507;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4504, _M0L1iS1454, _M0L6_2atmpS4505);
      _M0L2heS4517 = _M0L1pS1452->$12;
      _M0L2heS4526 = _M0L1pS1452->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4519 = _M0MPC15array5Array2atGfE(_M0L2heS4526, _M0L1iS1454);
      _M0L2heS4525 = _M0L1pS1452->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4524 = _M0MPC15array5Array2atGfE(_M0L2heS4525, _M0L1iS1454);
      _M0L6_2atmpS4522 = -_M0L6_2atmpS4524;
      _M0L3treS4523 = _M0L1pS1452->$20;
      _M0L6_2atmpS4521 = _M0L6_2atmpS4522 / _M0L3treS4523;
      _M0L6_2atmpS4520 = _M0L2dtS1455 * _M0L6_2atmpS4521;
      _M0L6_2atmpS4518 = _M0L6_2atmpS4519 + _M0L6_2atmpS4520;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4517, _M0L1iS1454, _M0L6_2atmpS4518);
      _M0L2giS4527 = _M0L1pS1452->$11;
      _M0L2giS4539 = _M0L1pS1452->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4529 = _M0MPC15array5Array2atGfE(_M0L2giS4539, _M0L1iS1454);
      _M0L2giS4538 = _M0L1pS1452->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4537 = _M0MPC15array5Array2atGfE(_M0L2giS4538, _M0L1iS1454);
      _M0L6_2atmpS4535 = -_M0L6_2atmpS4537;
      _M0L3tdiS4536 = _M0L1pS1452->$23;
      _M0L6_2atmpS4532 = _M0L6_2atmpS4535 / _M0L3tdiS4536;
      _M0L2hiS4534 = _M0L1pS1452->$13;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4533 = _M0MPC15array5Array2atGfE(_M0L2hiS4534, _M0L1iS1454);
      _M0L6_2atmpS4531 = _M0L6_2atmpS4532 + _M0L6_2atmpS4533;
      _M0L6_2atmpS4530 = _M0L2dtS1455 * _M0L6_2atmpS4531;
      _M0L6_2atmpS4528 = _M0L6_2atmpS4529 + _M0L6_2atmpS4530;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4527, _M0L1iS1454, _M0L6_2atmpS4528);
      _M0L2hiS4540 = _M0L1pS1452->$13;
      _M0L2hiS4549 = _M0L1pS1452->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4542 = _M0MPC15array5Array2atGfE(_M0L2hiS4549, _M0L1iS1454);
      _M0L2hiS4548 = _M0L1pS1452->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4547 = _M0MPC15array5Array2atGfE(_M0L2hiS4548, _M0L1iS1454);
      _M0L6_2atmpS4545 = -_M0L6_2atmpS4547;
      _M0L3triS4546 = _M0L1pS1452->$22;
      _M0L6_2atmpS4544 = _M0L6_2atmpS4545 / _M0L3triS4546;
      _M0L6_2atmpS4543 = _M0L2dtS1455 * _M0L6_2atmpS4544;
      _M0L6_2atmpS4541 = _M0L6_2atmpS4542 + _M0L6_2atmpS4543;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4540, _M0L1iS1454, _M0L6_2atmpS4541);
      _M0L6_2atmpS4550 = _M0L1iS1454 + 1;
      _M0L1iS1454 = _M0L6_2atmpS4550;
      continue;
    }
    break;
  }
  _M0L7_2abindS1457 = 0;
  _M0L1iS1458 = _M0L7_2abindS1457;
  while (1) {
    if (_M0L1iS1458 < _M0L1nS1451) {
      struct _M0TPB5ArrayGfE* _M0L3gluS4551 = _M0L1pS1452->$14;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4552;
      int32_t _M0L6_2atmpS4553;
      #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS4551, _M0L1iS1458, 0x0p+0f);
      _M0L4gabaS4552 = _M0L1pS1452->$15;
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS4552, _M0L1iS1458, 0x0p+0f);
      _M0L6_2atmpS4553 = _M0L1iS1458 + 1;
      _M0L1iS1458 = _M0L6_2atmpS4553;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16forward__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1427,
  float _M0L6t__nowS1438
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS4491;
  int32_t _M0L6_2atmpS4490;
  int32_t _M0L10use__delayS1426;
  struct _M0TPB5ArrayGfE* _M0L3rhoS4489;
  int32_t _M0L6_2atmpS4488;
  int32_t _M0L8use__rhoS1428;
  #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6delaysS4491 = _M0L1cS1427->$5;
  #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS4490 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS4491);
  _M0L10use__delayS1426 = _M0L6_2atmpS4490 > 0;
  _M0L3rhoS4489 = _M0L1cS1427->$6;
  #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS4488 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS4489);
  _M0L8use__rhoS1428 = _M0L6_2atmpS4488 > 0;
  if (_M0L10use__delayS1426) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4451 = _M0L1cS1427->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS4450 = _M0L3preS4451->$5;
    int32_t _M0L6n__preS1429;
    struct _M0TPB8MutLocalGiE* _M0L1jS1430;
    #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6n__preS1429 = _M0MPC15array5Array6lengthGbE(_M0L4fireS4450);
    _M0L1jS1430
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS1430)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS1430->$0 = 0;
    while (1) {
      int32_t _M0L3valS4419 = _M0L1jS1430->$0;
      if (_M0L3valS4419 < _M0L6n__preS1429) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4422 = _M0L1cS1427->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS4420 = _M0L3preS4422->$5;
        int32_t _M0L3valS4421 = _M0L1jS1430->$0;
        int32_t _M0L3valS4449;
        int32_t _M0L6_2atmpS4448;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS4420, _M0L3valS4421)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4447 =
            _M0L1cS1427->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS4445 = _M0L6matrixS4447->$2;
          int32_t _M0L3valS4446 = _M0L1jS1430->$0;
          int32_t _M0L5startS1431;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4444;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS4441;
          int32_t _M0L3valS4443;
          int32_t _M0L6_2atmpS4442;
          int32_t _M0L3endS1432;
          struct _M0TPB8MutLocalGiE* _M0L1sS1433;
          #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L5startS1431
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS4445, _M0L3valS4446);
          _M0L6matrixS4444 = _M0L1cS1427->$4;
          _M0L6rowptrS4441 = _M0L6matrixS4444->$2;
          _M0L3valS4443 = _M0L1jS1430->$0;
          _M0L6_2atmpS4442 = _M0L3valS4443 + 1;
          #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L3endS1432
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS4441, _M0L6_2atmpS4442);
          _M0L1sS1433
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS1433)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS1433->$0 = _M0L5startS1431;
          while (1) {
            int32_t _M0L3valS4423 = _M0L1sS1433->$0;
            if (_M0L3valS4423 < _M0L3endS1432) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4440 =
                _M0L1cS1427->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS4438 = _M0L6matrixS4440->$3;
              int32_t _M0L3valS4439 = _M0L1sS1433->$0;
              int32_t _M0L9post__idxS1434;
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4437;
              struct _M0TPB5ArrayGfE* _M0L4valsS4435;
              int32_t _M0L3valS4436;
              float _M0L1wS1435;
              struct _M0TPB5ArrayGfE* _M0L6delaysS4433;
              int32_t _M0L3valS4434;
              float _M0L1dS1436;
              float _M0L9w__scaledS1437;
              int32_t _M0L3valS4429;
              int32_t _M0L6_2atmpS4428;
              #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L9post__idxS1434
              = _M0MPC15array5Array2atGiE(_M0L6colptrS4438, _M0L3valS4439);
              _M0L6matrixS4437 = _M0L1cS1427->$4;
              _M0L4valsS4435 = _M0L6matrixS4437->$4;
              _M0L3valS4436 = _M0L1sS1433->$0;
              #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1wS1435
              = _M0MPC15array5Array2atGfE(_M0L4valsS4435, _M0L3valS4436);
              _M0L6delaysS4433 = _M0L1cS1427->$5;
              _M0L3valS4434 = _M0L1sS1433->$0;
              #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1dS1436
              = _M0MPC15array5Array2atGfE(_M0L6delaysS4433, _M0L3valS4434);
              if (_M0L8use__rhoS1428) {
                struct _M0TPB5ArrayGfE* _M0L3rhoS4431 = _M0L1cS1427->$6;
                int32_t _M0L3valS4432 = _M0L1sS1433->$0;
                float _M0L6_2atmpS4430;
                #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4430
                = _M0MPC15array5Array2atGfE(_M0L3rhoS4431, _M0L3valS4432);
                _M0L9w__scaledS1437 = _M0L1wS1435 * _M0L6_2atmpS4430;
              } else {
                _M0L9w__scaledS1437 = _M0L1wS1435;
              }
              if (_M0L1dS1436 == 0x0p+0f) {
                #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS1427, _M0L9post__idxS1434, _M0L9w__scaledS1437);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS4424 =
                  _M0L1cS1427->$7;
                float _M0L6_2atmpS4425 = _M0L6t__nowS1438 + _M0L1dS1436;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS4426;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4427;
                #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS4424, _M0L6_2atmpS4425);
                _M0L14pending__postsS4426 = _M0L1cS1427->$8;
                #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS4426, _M0L9post__idxS1434);
                _M0L16pending__weightsS4427 = _M0L1cS1427->$9;
                #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS4427, _M0L9w__scaledS1437);
              }
              _M0L3valS4429 = _M0L1sS1433->$0;
              _M0L6_2atmpS4428 = _M0L3valS4429 + 1;
              _M0L1sS1433->$0 = _M0L6_2atmpS4428;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1sS1433);
            }
            break;
          }
        }
        _M0L3valS4449 = _M0L1jS1430->$0;
        _M0L6_2atmpS4448 = _M0L3valS4449 + 1;
        _M0L1jS1430->$0 = _M0L6_2atmpS4448;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1jS1430);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS4485 = _M0L1cS1427->$2;
    struct _M0TPB5ArrayGfE* _M0L6targetS1441;
    #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    if (
      _M0L3symS4485 == (moonbit_string_t)moonbit_string_literal_9.data
      || Moonbit_array_length(_M0L3symS4485)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
         && 0
            == memcmp(_M0L3symS4485, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS4485) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4486 = _M0L1cS1427->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS5664 = _M0L4postS4486->$13;
      moonbit_incref_cycle_free(_M0L8_2afieldS5664);
      _M0L6targetS1441 = _M0L8_2afieldS5664;
    } else {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4487 = _M0L1cS1427->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS5665 = _M0L4postS4487->$14;
      moonbit_incref_cycle_free(_M0L8_2afieldS5665);
      _M0L6targetS1441 = _M0L8_2afieldS5665;
    }
    if (_M0L8use__rhoS1428) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4481 = _M0L1cS1427->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS4480 = _M0L3preS4481->$5;
      int32_t _M0L6n__preS1442;
      struct _M0TPB8MutLocalGiE* _M0L1jS1443;
      #line 283 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6n__preS1442 = _M0MPC15array5Array6lengthGbE(_M0L4fireS4480);
      _M0L1jS1443
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS1443)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS1443->$0 = 0;
      while (1) {
        int32_t _M0L3valS4452 = _M0L1jS1443->$0;
        if (_M0L3valS4452 < _M0L6n__preS1442) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4455 = _M0L1cS1427->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS4453 = _M0L3preS4455->$5;
          int32_t _M0L3valS4454 = _M0L1jS1443->$0;
          int32_t _M0L3valS4479;
          int32_t _M0L6_2atmpS4478;
          #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS4453, _M0L3valS4454)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4477 =
              _M0L1cS1427->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS4475 = _M0L6matrixS4477->$2;
            int32_t _M0L3valS4476 = _M0L1jS1443->$0;
            int32_t _M0L5startS1444;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4474;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS4471;
            int32_t _M0L3valS4473;
            int32_t _M0L6_2atmpS4472;
            int32_t _M0L3endS1445;
            struct _M0TPB8MutLocalGiE* _M0L1sS1446;
            #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L5startS1444
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS4475, _M0L3valS4476);
            _M0L6matrixS4474 = _M0L1cS1427->$4;
            _M0L6rowptrS4471 = _M0L6matrixS4474->$2;
            _M0L3valS4473 = _M0L1jS1443->$0;
            _M0L6_2atmpS4472 = _M0L3valS4473 + 1;
            #line 288 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L3endS1445
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS4471, _M0L6_2atmpS4472);
            _M0L1sS1446
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS1446)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS1446->$0 = _M0L5startS1444;
            while (1) {
              int32_t _M0L3valS4456 = _M0L1sS1446->$0;
              if (_M0L3valS4456 < _M0L3endS1445) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4470 =
                  _M0L1cS1427->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS4468 =
                  _M0L6matrixS4470->$3;
                int32_t _M0L3valS4469 = _M0L1sS1446->$0;
                int32_t _M0L9post__idxS1447;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4467;
                struct _M0TPB5ArrayGfE* _M0L4valsS4465;
                int32_t _M0L3valS4466;
                float _M0L6_2atmpS4461;
                struct _M0TPB5ArrayGfE* _M0L3rhoS4463;
                int32_t _M0L3valS4464;
                float _M0L6_2atmpS4462;
                float _M0L9w__scaledS1448;
                float _M0L6_2atmpS4458;
                float _M0L6_2atmpS4457;
                int32_t _M0L3valS4460;
                int32_t _M0L6_2atmpS4459;
                #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L9post__idxS1447
                = _M0MPC15array5Array2atGiE(_M0L6colptrS4468, _M0L3valS4469);
                _M0L6matrixS4467 = _M0L1cS1427->$4;
                _M0L4valsS4465 = _M0L6matrixS4467->$4;
                _M0L3valS4466 = _M0L1sS1446->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4461
                = _M0MPC15array5Array2atGfE(_M0L4valsS4465, _M0L3valS4466);
                _M0L3rhoS4463 = _M0L1cS1427->$6;
                _M0L3valS4464 = _M0L1sS1446->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4462
                = _M0MPC15array5Array2atGfE(_M0L3rhoS4463, _M0L3valS4464);
                _M0L9w__scaledS1448 = _M0L6_2atmpS4461 * _M0L6_2atmpS4462;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4458
                = _M0MPC15array5Array2atGfE(_M0L6targetS1441, _M0L9post__idxS1447);
                _M0L6_2atmpS4457 = _M0L6_2atmpS4458 + _M0L9w__scaledS1448;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array3setGfE(_M0L6targetS1441, _M0L9post__idxS1447, _M0L6_2atmpS4457);
                _M0L3valS4460 = _M0L1sS1446->$0;
                _M0L6_2atmpS4459 = _M0L3valS4460 + 1;
                _M0L1sS1446->$0 = _M0L6_2atmpS4459;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS1446);
              }
              break;
            }
          }
          _M0L3valS4479 = _M0L1jS1443->$0;
          _M0L6_2atmpS4478 = _M0L3valS4479 + 1;
          _M0L1jS1443->$0 = _M0L6_2atmpS4478;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1jS1443);
          moonbit_decref_cycle_free(_M0L6targetS1441);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4482 =
        _M0L1cS1427->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4484 = _M0L1cS1427->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS4483 = _M0L3preS4484->$5;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS4482, _M0L4fireS4483, _M0L6targetS1441);
      moonbit_decref_cycle_free(_M0L6targetS1441);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25deliver__pending__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1419,
  float _M0L6t__nowS1422
) {
  struct _M0TPB5ArrayGfE* _M0L14pending__timesS4418;
  int32_t _M0L1nS1418;
  struct _M0TPB8MutLocalGiE* _M0L4keptS1420;
  struct _M0TPB8MutLocalGiE* _M0L1kS1421;
  int32_t _M0L3valS4417;
  int32_t _M0L6_2atmpS4416;
  struct _M0TPB8MutLocalGiE* _M0L4dropS1424;
  #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L14pending__timesS4418 = _M0L1cS1419->$7;
  #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS1418 = _M0MPC15array5Array6lengthGfE(_M0L14pending__timesS4418);
  if (_M0L1nS1418 == 0) {
    return 0;
  }
  _M0L4keptS1420
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4keptS1420)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4keptS1420->$0 = 0;
  _M0L1kS1421
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS1421)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS1421->$0 = 0;
  while (1) {
    int32_t _M0L3valS4379 = _M0L1kS1421->$0;
    if (_M0L3valS4379 < _M0L1nS1418) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS4381 = _M0L1cS1419->$7;
      int32_t _M0L3valS4382 = _M0L1kS1421->$0;
      float _M0L6_2atmpS4380;
      int32_t _M0L3valS4409;
      int32_t _M0L6_2atmpS4408;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS4380
      = _M0MPC15array5Array2atGfE(_M0L14pending__timesS4381, _M0L3valS4382);
      if (_M0L6_2atmpS4380 <= _M0L6t__nowS1422) {
        struct _M0TPB5ArrayGiE* _M0L14pending__postsS4387 = _M0L1cS1419->$8;
        int32_t _M0L3valS4388 = _M0L1kS1421->$0;
        int32_t _M0L6_2atmpS4383;
        struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4385;
        int32_t _M0L3valS4386;
        float _M0L6_2atmpS4384;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS4383
        = _M0MPC15array5Array2atGiE(_M0L14pending__postsS4387, _M0L3valS4388);
        _M0L16pending__weightsS4385 = _M0L1cS1419->$9;
        _M0L3valS4386 = _M0L1kS1421->$0;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS4384
        = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS4385, _M0L3valS4386);
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS1419, _M0L6_2atmpS4383, _M0L6_2atmpS4384);
      } else {
        int32_t _M0L3valS4389 = _M0L4keptS1420->$0;
        int32_t _M0L3valS4390 = _M0L1kS1421->$0;
        int32_t _M0L3valS4407;
        int32_t _M0L6_2atmpS4406;
        if (_M0L3valS4389 != _M0L3valS4390) {
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS4391 = _M0L1cS1419->$7;
          int32_t _M0L3valS4392 = _M0L4keptS1420->$0;
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS4394 = _M0L1cS1419->$7;
          int32_t _M0L3valS4395 = _M0L1kS1421->$0;
          float _M0L6_2atmpS4393;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS4396;
          int32_t _M0L3valS4397;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS4399;
          int32_t _M0L3valS4400;
          int32_t _M0L6_2atmpS4398;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4401;
          int32_t _M0L3valS4402;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4404;
          int32_t _M0L3valS4405;
          float _M0L6_2atmpS4403;
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS4393
          = _M0MPC15array5Array2atGfE(_M0L14pending__timesS4394, _M0L3valS4395);
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L14pending__timesS4391, _M0L3valS4392, _M0L6_2atmpS4393);
          _M0L14pending__postsS4396 = _M0L1cS1419->$8;
          _M0L3valS4397 = _M0L4keptS1420->$0;
          _M0L14pending__postsS4399 = _M0L1cS1419->$8;
          _M0L3valS4400 = _M0L1kS1421->$0;
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS4398
          = _M0MPC15array5Array2atGiE(_M0L14pending__postsS4399, _M0L3valS4400);
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGiE(_M0L14pending__postsS4396, _M0L3valS4397, _M0L6_2atmpS4398);
          _M0L16pending__weightsS4401 = _M0L1cS1419->$9;
          _M0L3valS4402 = _M0L4keptS1420->$0;
          _M0L16pending__weightsS4404 = _M0L1cS1419->$9;
          _M0L3valS4405 = _M0L1kS1421->$0;
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS4403
          = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS4404, _M0L3valS4405);
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L16pending__weightsS4401, _M0L3valS4402, _M0L6_2atmpS4403);
        }
        _M0L3valS4407 = _M0L4keptS1420->$0;
        _M0L6_2atmpS4406 = _M0L3valS4407 + 1;
        _M0L4keptS1420->$0 = _M0L6_2atmpS4406;
      }
      _M0L3valS4409 = _M0L1kS1421->$0;
      _M0L6_2atmpS4408 = _M0L3valS4409 + 1;
      _M0L1kS1421->$0 = _M0L6_2atmpS4408;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS1421);
    }
    break;
  }
  _M0L3valS4417 = _M0L4keptS1420->$0;
  moonbit_decref_cycle_free(_M0L4keptS1420);
  _M0L6_2atmpS4416 = _M0L1nS1418 - _M0L3valS4417;
  _M0L4dropS1424
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4dropS1424)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4dropS1424->$0 = _M0L6_2atmpS4416;
  while (1) {
    int32_t _M0L3valS4410 = _M0L4dropS1424->$0;
    if (_M0L3valS4410 > 0) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS4411 = _M0L1cS1419->$7;
      void* _M0L6_2atmpS5667;
      struct _M0TPB5ArrayGiE* _M0L14pending__postsS4412;
      struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4413;
      void* _M0L6_2atmpS5666;
      int32_t _M0L3valS4415;
      int32_t _M0L6_2atmpS4414;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS5667
      = _M0MPC15array5Array3popGfE(_M0L14pending__timesS4411);
      moonbit_decref_cycle_free(_M0L6_2atmpS5667);
      _M0L14pending__postsS4412 = _M0L1cS1419->$8;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MPC15array5Array3popGiE(_M0L14pending__postsS4412);
      _M0L16pending__weightsS4413 = _M0L1cS1419->$9;
      #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS5666
      = _M0MPC15array5Array3popGfE(_M0L16pending__weightsS4413);
      moonbit_decref_cycle_free(_M0L6_2atmpS5666);
      _M0L3valS4415 = _M0L4dropS1424->$0;
      _M0L6_2atmpS4414 = _M0L3valS4415 - 1;
      _M0L4dropS1424->$0 = _M0L6_2atmpS4414;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4dropS1424);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13apply__weight(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1415,
  int32_t _M0L9post__idxS1416,
  float _M0L1wS1417
) {
  moonbit_string_t _M0L3symS4366;
  #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L3symS4366 = _M0L1cS1415->$2;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  if (
    _M0L3symS4366 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS4366)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS4366, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS4366) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4372 = _M0L1cS1415->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS4367 = _M0L4postS4372->$13;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4371 = _M0L1cS1415->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS4370 = _M0L4postS4371->$13;
    float _M0L6_2atmpS4369;
    float _M0L6_2atmpS4368;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS4369
    = _M0MPC15array5Array2atGfE(_M0L3gluS4370, _M0L9post__idxS1416);
    _M0L6_2atmpS4368 = _M0L6_2atmpS4369 + _M0L1wS1417;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L3gluS4367, _M0L9post__idxS1416, _M0L6_2atmpS4368);
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4378 = _M0L1cS1415->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS4373 = _M0L4postS4378->$14;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4377 = _M0L1cS1415->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS4376 = _M0L4postS4377->$14;
    float _M0L6_2atmpS4375;
    float _M0L6_2atmpS4374;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS4375
    = _M0MPC15array5Array2atGfE(_M0L4gabaS4376, _M0L9post__idxS1416);
    _M0L6_2atmpS4374 = _M0L6_2atmpS4375 + _M0L1wS1417;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L4gabaS4373, _M0L9post__idxS1416, _M0L6_2atmpS4374);
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt11record__one(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS1412,
  float _M0L1tS1414
) {
  int32_t _M0L11step__countS4352;
  int32_t _M0L6_2atmpS4351;
  int32_t _M0L11step__countS4354;
  int32_t _M0L9rec__stepS4355;
  int32_t _M0L6_2atmpS4353;
  moonbit_string_t _M0L3symS4358;
  float _M0L1vS1413;
  struct _M0TPB5ArrayGfE* _M0L4dataS4356;
  struct _M0TPB5ArrayGfE* _M0L5timesS4357;
  #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L11step__countS4352 = _M0L1mS1412->$6;
  _M0L6_2atmpS4351 = _M0L11step__countS4352 + 1;
  _M0L1mS1412->$6 = _M0L6_2atmpS4351;
  _M0L11step__countS4354 = _M0L1mS1412->$6;
  _M0L9rec__stepS4355 = _M0L1mS1412->$5;
  _M0L6_2atmpS4353 = _M0L11step__countS4354 % _M0L9rec__stepS4355;
  if (_M0L6_2atmpS4353 != 0) {
    return 0;
  }
  _M0L3symS4358 = _M0L1mS1412->$1;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS4358 == (moonbit_string_t)moonbit_string_literal_10.data
    || Moonbit_array_length(_M0L3symS4358)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_10.data)
       && 0
          == memcmp(_M0L3symS4358, (moonbit_string_t)moonbit_string_literal_10.data, Moonbit_array_length(_M0L3symS4358) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS4361 = _M0L1mS1412->$0;
    struct _M0TPB5ArrayGfE* _M0L1vS4359 = _M0L3popS4361->$3;
    int32_t _M0L6neuronS4360 = _M0L1mS1412->$4;
    #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _M0L1vS1413 = _M0MPC15array5Array2atGfE(_M0L1vS4359, _M0L6neuronS4360);
  } else {
    moonbit_string_t _M0L3symS4362 = _M0L1mS1412->$1;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    if (
      _M0L3symS4362 == (moonbit_string_t)moonbit_string_literal_11.data
      || Moonbit_array_length(_M0L3symS4362)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_11.data)
         && 0
            == memcmp(_M0L3symS4362, (moonbit_string_t)moonbit_string_literal_11.data, Moonbit_array_length(_M0L3symS4362) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS4365 = _M0L1mS1412->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS4363 = _M0L3popS4365->$5;
      int32_t _M0L6neuronS4364 = _M0L1mS1412->$4;
      #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4363, _M0L6neuronS4364)) {
        _M0L1vS1413 = 0x1p+0f;
      } else {
        _M0L1vS1413 = 0x0p+0f;
      }
    } else {
      _M0L1vS1413 = 0x0p+0f;
    }
  }
  _M0L4dataS4356 = _M0L1mS1412->$2;
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L4dataS4356, _M0L1vS1413);
  _M0L5timesS4357 = _M0L1mS1412->$3;
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L5timesS4357, _M0L1tS1414);
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor9new__fire(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS1410,
  int32_t _M0L6neuronS1411
) {
  float* _M0L6_2atmpS4350;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4347;
  float* _M0L6_2atmpS4349;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4348;
  struct _M0TP26RiantR8snn__mbt7Monitor* _block_5983;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6_2atmpS4350 = moonbit_empty_float_array;
  _M0L6_2atmpS4347
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4347)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS4347->$0 = _M0L6_2atmpS4350;
  _M0L6_2atmpS4347->$1 = 0;
  _M0L6_2atmpS4349 = moonbit_empty_float_array;
  _M0L6_2atmpS4348
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4348)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS4348->$0 = _M0L6_2atmpS4349;
  _M0L6_2atmpS4348->$1 = 0;
  moonbit_incref_cycle_free(_M0L3popS1410);
  _block_5983
  = (struct _M0TP26RiantR8snn__mbt7Monitor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Monitor));
  Moonbit_object_header(_block_5983)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 75, 0);
  _block_5983->$0 = _M0L3popS1410;
  _block_5983->$1 = (moonbit_string_t)moonbit_string_literal_11.data;
  _block_5983->$2 = _M0L6_2atmpS4347;
  _block_5983->$3 = _M0L6_2atmpS4348;
  _block_5983->$4 = _M0L6neuronS1411;
  _block_5983->$5 = 1;
  _block_5983->$6 = 0;
  return _block_5983;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1406
) {
  int32_t _M0L1nS1405;
  int32_t _M0L7_2abindS1407;
  int32_t _M0L1iS1408;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1405 = _M0L1pS1406->$2;
  _M0L7_2abindS1407 = 0;
  _M0L1iS1408 = _M0L7_2abindS1407;
  while (1) {
    if (_M0L1iS1408 < _M0L1nS1405) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4324 = _M0L1pS1406->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS4345 = _M0L1pS1406->$9;
      float _M0L6_2atmpS4340;
      struct _M0TPB5ArrayGfE* _M0L1vS4344;
      float _M0L6_2atmpS4342;
      float _M0L4e__eS4343;
      float _M0L6_2atmpS4341;
      float _M0L6_2atmpS4337;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS4339;
      float _M0L6_2atmpS4338;
      float _M0L6_2atmpS4326;
      struct _M0TPB5ArrayGfE* _M0L2giS4336;
      float _M0L6_2atmpS4331;
      struct _M0TPB5ArrayGfE* _M0L1vS4335;
      float _M0L6_2atmpS4333;
      float _M0L4e__iS4334;
      float _M0L6_2atmpS4332;
      float _M0L6_2atmpS4328;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS4330;
      float _M0L6_2atmpS4329;
      float _M0L6_2atmpS4327;
      float _M0L6_2atmpS4325;
      int32_t _M0L6_2atmpS4346;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4340 = _M0MPC15array5Array2atGfE(_M0L2geS4345, _M0L1iS1408);
      _M0L1vS4344 = _M0L1pS1406->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4342 = _M0MPC15array5Array2atGfE(_M0L1vS4344, _M0L1iS1408);
      _M0L4e__eS4343 = _M0L1pS1406->$17;
      _M0L6_2atmpS4341 = _M0L6_2atmpS4342 - _M0L4e__eS4343;
      _M0L6_2atmpS4337 = _M0L6_2atmpS4340 * _M0L6_2atmpS4341;
      _M0L7gsyn__eS4339 = _M0L1pS1406->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4338
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS4339, _M0L1iS1408);
      _M0L6_2atmpS4326 = _M0L6_2atmpS4337 * _M0L6_2atmpS4338;
      _M0L2giS4336 = _M0L1pS1406->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4331 = _M0MPC15array5Array2atGfE(_M0L2giS4336, _M0L1iS1408);
      _M0L1vS4335 = _M0L1pS1406->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4333 = _M0MPC15array5Array2atGfE(_M0L1vS4335, _M0L1iS1408);
      _M0L4e__iS4334 = _M0L1pS1406->$18;
      _M0L6_2atmpS4332 = _M0L6_2atmpS4333 - _M0L4e__iS4334;
      _M0L6_2atmpS4328 = _M0L6_2atmpS4331 * _M0L6_2atmpS4332;
      _M0L7gsyn__iS4330 = _M0L1pS1406->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4329
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS4330, _M0L1iS1408);
      _M0L6_2atmpS4327 = _M0L6_2atmpS4328 * _M0L6_2atmpS4329;
      _M0L6_2atmpS4325 = _M0L6_2atmpS4326 + _M0L6_2atmpS4327;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS4324, _M0L1iS1408, _M0L6_2atmpS4325);
      _M0L6_2atmpS4346 = _M0L1iS1408 + 1;
      _M0L1iS1408 = _M0L6_2atmpS4346;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1397,
  float _M0L2dtS1400
) {
  int32_t _M0L1nS1396;
  int32_t _M0L7_2abindS1398;
  int32_t _M0L1iS1399;
  int32_t _M0L7_2abindS1402;
  int32_t _M0L1iS1403;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1396 = _M0L1pS1397->$2;
  _M0L7_2abindS1398 = 0;
  _M0L1iS1399 = _M0L7_2abindS1398;
  while (1) {
    if (_M0L1iS1399 < _M0L1nS1396) {
      struct _M0TPB5ArrayGfE* _M0L2heS4262 = _M0L1pS1397->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS4267 = _M0L1pS1397->$11;
      float _M0L6_2atmpS4264;
      struct _M0TPB5ArrayGfE* _M0L3gluS4266;
      float _M0L6_2atmpS4265;
      float _M0L6_2atmpS4263;
      struct _M0TPB5ArrayGfE* _M0L2hiS4268;
      struct _M0TPB5ArrayGfE* _M0L2hiS4273;
      float _M0L6_2atmpS4270;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4272;
      float _M0L6_2atmpS4271;
      float _M0L6_2atmpS4269;
      struct _M0TPB5ArrayGfE* _M0L2geS4274;
      struct _M0TPB5ArrayGfE* _M0L2geS4286;
      float _M0L6_2atmpS4276;
      struct _M0TPB5ArrayGfE* _M0L2geS4285;
      float _M0L6_2atmpS4284;
      float _M0L6_2atmpS4282;
      float _M0L3tdeS4283;
      float _M0L6_2atmpS4279;
      struct _M0TPB5ArrayGfE* _M0L2heS4281;
      float _M0L6_2atmpS4280;
      float _M0L6_2atmpS4278;
      float _M0L6_2atmpS4277;
      float _M0L6_2atmpS4275;
      struct _M0TPB5ArrayGfE* _M0L2heS4287;
      struct _M0TPB5ArrayGfE* _M0L2heS4296;
      float _M0L6_2atmpS4289;
      struct _M0TPB5ArrayGfE* _M0L2heS4295;
      float _M0L6_2atmpS4294;
      float _M0L6_2atmpS4292;
      float _M0L3treS4293;
      float _M0L6_2atmpS4291;
      float _M0L6_2atmpS4290;
      float _M0L6_2atmpS4288;
      struct _M0TPB5ArrayGfE* _M0L2giS4297;
      struct _M0TPB5ArrayGfE* _M0L2giS4309;
      float _M0L6_2atmpS4299;
      struct _M0TPB5ArrayGfE* _M0L2giS4308;
      float _M0L6_2atmpS4307;
      float _M0L6_2atmpS4305;
      float _M0L3tdiS4306;
      float _M0L6_2atmpS4302;
      struct _M0TPB5ArrayGfE* _M0L2hiS4304;
      float _M0L6_2atmpS4303;
      float _M0L6_2atmpS4301;
      float _M0L6_2atmpS4300;
      float _M0L6_2atmpS4298;
      struct _M0TPB5ArrayGfE* _M0L2hiS4310;
      struct _M0TPB5ArrayGfE* _M0L2hiS4319;
      float _M0L6_2atmpS4312;
      struct _M0TPB5ArrayGfE* _M0L2hiS4318;
      float _M0L6_2atmpS4317;
      float _M0L6_2atmpS4315;
      float _M0L3triS4316;
      float _M0L6_2atmpS4314;
      float _M0L6_2atmpS4313;
      float _M0L6_2atmpS4311;
      int32_t _M0L6_2atmpS4320;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4264 = _M0MPC15array5Array2atGfE(_M0L2heS4267, _M0L1iS1399);
      _M0L3gluS4266 = _M0L1pS1397->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4265
      = _M0MPC15array5Array2atGfE(_M0L3gluS4266, _M0L1iS1399);
      _M0L6_2atmpS4263 = _M0L6_2atmpS4264 + _M0L6_2atmpS4265;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4262, _M0L1iS1399, _M0L6_2atmpS4263);
      _M0L2hiS4268 = _M0L1pS1397->$12;
      _M0L2hiS4273 = _M0L1pS1397->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4270 = _M0MPC15array5Array2atGfE(_M0L2hiS4273, _M0L1iS1399);
      _M0L4gabaS4272 = _M0L1pS1397->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4271
      = _M0MPC15array5Array2atGfE(_M0L4gabaS4272, _M0L1iS1399);
      _M0L6_2atmpS4269 = _M0L6_2atmpS4270 + _M0L6_2atmpS4271;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4268, _M0L1iS1399, _M0L6_2atmpS4269);
      _M0L2geS4274 = _M0L1pS1397->$9;
      _M0L2geS4286 = _M0L1pS1397->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4276 = _M0MPC15array5Array2atGfE(_M0L2geS4286, _M0L1iS1399);
      _M0L2geS4285 = _M0L1pS1397->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4284 = _M0MPC15array5Array2atGfE(_M0L2geS4285, _M0L1iS1399);
      _M0L6_2atmpS4282 = -_M0L6_2atmpS4284;
      _M0L3tdeS4283 = _M0L1pS1397->$20;
      _M0L6_2atmpS4279 = _M0L6_2atmpS4282 / _M0L3tdeS4283;
      _M0L2heS4281 = _M0L1pS1397->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4280 = _M0MPC15array5Array2atGfE(_M0L2heS4281, _M0L1iS1399);
      _M0L6_2atmpS4278 = _M0L6_2atmpS4279 + _M0L6_2atmpS4280;
      _M0L6_2atmpS4277 = _M0L2dtS1400 * _M0L6_2atmpS4278;
      _M0L6_2atmpS4275 = _M0L6_2atmpS4276 + _M0L6_2atmpS4277;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4274, _M0L1iS1399, _M0L6_2atmpS4275);
      _M0L2heS4287 = _M0L1pS1397->$11;
      _M0L2heS4296 = _M0L1pS1397->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4289 = _M0MPC15array5Array2atGfE(_M0L2heS4296, _M0L1iS1399);
      _M0L2heS4295 = _M0L1pS1397->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4294 = _M0MPC15array5Array2atGfE(_M0L2heS4295, _M0L1iS1399);
      _M0L6_2atmpS4292 = -_M0L6_2atmpS4294;
      _M0L3treS4293 = _M0L1pS1397->$19;
      _M0L6_2atmpS4291 = _M0L6_2atmpS4292 / _M0L3treS4293;
      _M0L6_2atmpS4290 = _M0L2dtS1400 * _M0L6_2atmpS4291;
      _M0L6_2atmpS4288 = _M0L6_2atmpS4289 + _M0L6_2atmpS4290;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4287, _M0L1iS1399, _M0L6_2atmpS4288);
      _M0L2giS4297 = _M0L1pS1397->$10;
      _M0L2giS4309 = _M0L1pS1397->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4299 = _M0MPC15array5Array2atGfE(_M0L2giS4309, _M0L1iS1399);
      _M0L2giS4308 = _M0L1pS1397->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4307 = _M0MPC15array5Array2atGfE(_M0L2giS4308, _M0L1iS1399);
      _M0L6_2atmpS4305 = -_M0L6_2atmpS4307;
      _M0L3tdiS4306 = _M0L1pS1397->$22;
      _M0L6_2atmpS4302 = _M0L6_2atmpS4305 / _M0L3tdiS4306;
      _M0L2hiS4304 = _M0L1pS1397->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4303 = _M0MPC15array5Array2atGfE(_M0L2hiS4304, _M0L1iS1399);
      _M0L6_2atmpS4301 = _M0L6_2atmpS4302 + _M0L6_2atmpS4303;
      _M0L6_2atmpS4300 = _M0L2dtS1400 * _M0L6_2atmpS4301;
      _M0L6_2atmpS4298 = _M0L6_2atmpS4299 + _M0L6_2atmpS4300;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4297, _M0L1iS1399, _M0L6_2atmpS4298);
      _M0L2hiS4310 = _M0L1pS1397->$12;
      _M0L2hiS4319 = _M0L1pS1397->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4312 = _M0MPC15array5Array2atGfE(_M0L2hiS4319, _M0L1iS1399);
      _M0L2hiS4318 = _M0L1pS1397->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4317 = _M0MPC15array5Array2atGfE(_M0L2hiS4318, _M0L1iS1399);
      _M0L6_2atmpS4315 = -_M0L6_2atmpS4317;
      _M0L3triS4316 = _M0L1pS1397->$21;
      _M0L6_2atmpS4314 = _M0L6_2atmpS4315 / _M0L3triS4316;
      _M0L6_2atmpS4313 = _M0L2dtS1400 * _M0L6_2atmpS4314;
      _M0L6_2atmpS4311 = _M0L6_2atmpS4312 + _M0L6_2atmpS4313;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4310, _M0L1iS1399, _M0L6_2atmpS4311);
      _M0L6_2atmpS4320 = _M0L1iS1399 + 1;
      _M0L1iS1399 = _M0L6_2atmpS4320;
      continue;
    }
    break;
  }
  _M0L7_2abindS1402 = 0;
  _M0L1iS1403 = _M0L7_2abindS1402;
  while (1) {
    if (_M0L1iS1403 < _M0L1nS1396) {
      struct _M0TPB5ArrayGfE* _M0L3gluS4321 = _M0L1pS1397->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4322;
      int32_t _M0L6_2atmpS4323;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS4321, _M0L1iS1403, 0x0p+0f);
      _M0L4gabaS4322 = _M0L1pS1397->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS4322, _M0L1iS1403, 0x0p+0f);
      _M0L6_2atmpS4323 = _M0L1iS1403 + 1;
      _M0L1iS1403 = _M0L6_2atmpS4323;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1382,
  float _M0L2dtS1391
) {
  int32_t _M0L1nS1381;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S1383;
  float _M0L2tmS1384;
  float _M0L2elS1385;
  float _M0L1rS1386;
  float _M0L2vtS1387;
  float _M0L2vrS1388;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS4261;
  float _M0L11tabs__constS1389;
  float _M0L6_2atmpS4260;
  int32_t _M0L11tabs__stepsS1390;
  int32_t _M0L7_2abindS1392;
  int32_t _M0L1iS1393;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1381 = _M0L1pS1382->$2;
  _M0L3p__S1383 = _M0L1pS1382->$0;
  _M0L2tmS1384 = _M0L3p__S1383->$2;
  _M0L2elS1385 = _M0L3p__S1383->$5;
  _M0L1rS1386 = _M0L3p__S1383->$6;
  _M0L2vtS1387 = _M0L3p__S1383->$3;
  _M0L2vrS1388 = _M0L3p__S1383->$4;
  _M0L5spikeS4261 = _M0L1pS1382->$1;
  _M0L11tabs__constS1389 = _M0L5spikeS4261->$0;
  _M0L6_2atmpS4260 = _M0L11tabs__constS1389 / _M0L2dtS1391;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS1390 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4260);
  _M0L7_2abindS1392 = 0;
  _M0L1iS1393 = _M0L7_2abindS1392;
  while (1) {
    if (_M0L1iS1393 < _M0L1nS1381) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS4220 = _M0L1pS1382->$6;
      int32_t _M0L6_2atmpS4219;
      struct _M0TPB5ArrayGfE* _M0L1vS4226;
      struct _M0TPB5ArrayGfE* _M0L1vS4247;
      float _M0L6_2atmpS4228;
      float _M0L6_2atmpS4230;
      struct _M0TPB5ArrayGfE* _M0L1vS4246;
      float _M0L6_2atmpS4245;
      float _M0L6_2atmpS4244;
      float _M0L6_2atmpS4236;
      struct _M0TPB5ArrayGfE* _M0L1wS4243;
      float _M0L6_2atmpS4242;
      float _M0L6_2atmpS4239;
      struct _M0TPB5ArrayGfE* _M0L1iS4241;
      float _M0L6_2atmpS4240;
      float _M0L6_2atmpS4238;
      float _M0L6_2atmpS4237;
      float _M0L6_2atmpS4232;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4235;
      float _M0L6_2atmpS4234;
      float _M0L6_2atmpS4233;
      float _M0L6_2atmpS4231;
      float _M0L6_2atmpS4229;
      float _M0L6_2atmpS4227;
      struct _M0TPB5ArrayGbE* _M0L4fireS4248;
      struct _M0TPB5ArrayGfE* _M0L1vS4251;
      float _M0L6_2atmpS4250;
      int32_t _M0L6_2atmpS4249;
      struct _M0TPB5ArrayGfE* _M0L1vS4252;
      struct _M0TPB5ArrayGbE* _M0L4fireS4254;
      float _M0L6_2atmpS4253;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4256;
      struct _M0TPB5ArrayGbE* _M0L4fireS4258;
      int32_t _M0L6_2atmpS4257;
      int32_t _M0L6_2atmpS4218;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4219
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4220, _M0L1iS1393);
      if (_M0L6_2atmpS4219 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS4221 = _M0L1pS1382->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS4222;
        struct _M0TPB5ArrayGiE* _M0L4tabsS4225;
        int32_t _M0L6_2atmpS4224;
        int32_t _M0L6_2atmpS4223;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS4221, _M0L1iS1393, 0);
        _M0L4tabsS4222 = _M0L1pS1382->$6;
        _M0L4tabsS4225 = _M0L1pS1382->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS4224
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4225, _M0L1iS1393);
        _M0L6_2atmpS4223 = _M0L6_2atmpS4224 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS4222, _M0L1iS1393, _M0L6_2atmpS4223);
        goto join_1394;
      }
      _M0L1vS4226 = _M0L1pS1382->$3;
      _M0L1vS4247 = _M0L1pS1382->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4228 = _M0MPC15array5Array2atGfE(_M0L1vS4247, _M0L1iS1393);
      _M0L6_2atmpS4230 = _M0L2dtS1391 / _M0L2tmS1384;
      _M0L1vS4246 = _M0L1pS1382->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4245 = _M0MPC15array5Array2atGfE(_M0L1vS4246, _M0L1iS1393);
      _M0L6_2atmpS4244 = _M0L6_2atmpS4245 - _M0L2elS1385;
      _M0L6_2atmpS4236 = -_M0L6_2atmpS4244;
      _M0L1wS4243 = _M0L1pS1382->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4242 = _M0MPC15array5Array2atGfE(_M0L1wS4243, _M0L1iS1393);
      _M0L6_2atmpS4239 = -_M0L6_2atmpS4242;
      _M0L1iS4241 = _M0L1pS1382->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4240 = _M0MPC15array5Array2atGfE(_M0L1iS4241, _M0L1iS1393);
      _M0L6_2atmpS4238 = _M0L6_2atmpS4239 + _M0L6_2atmpS4240;
      _M0L6_2atmpS4237 = _M0L1rS1386 * _M0L6_2atmpS4238;
      _M0L6_2atmpS4232 = _M0L6_2atmpS4236 + _M0L6_2atmpS4237;
      _M0L9syn__currS4235 = _M0L1pS1382->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4234
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS4235, _M0L1iS1393);
      _M0L6_2atmpS4233 = _M0L1rS1386 * _M0L6_2atmpS4234;
      _M0L6_2atmpS4231 = _M0L6_2atmpS4232 - _M0L6_2atmpS4233;
      _M0L6_2atmpS4229 = _M0L6_2atmpS4230 * _M0L6_2atmpS4231;
      _M0L6_2atmpS4227 = _M0L6_2atmpS4228 + _M0L6_2atmpS4229;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4226, _M0L1iS1393, _M0L6_2atmpS4227);
      _M0L4fireS4248 = _M0L1pS1382->$5;
      _M0L1vS4251 = _M0L1pS1382->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4250 = _M0MPC15array5Array2atGfE(_M0L1vS4251, _M0L1iS1393);
      _M0L6_2atmpS4249 = _M0L6_2atmpS4250 > _M0L2vtS1387;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4248, _M0L1iS1393, _M0L6_2atmpS4249);
      _M0L1vS4252 = _M0L1pS1382->$3;
      _M0L4fireS4254 = _M0L1pS1382->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4254, _M0L1iS1393)) {
        _M0L6_2atmpS4253 = _M0L2vrS1388;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4255 = _M0L1pS1382->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS4253
        = _M0MPC15array5Array2atGfE(_M0L1vS4255, _M0L1iS1393);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4252, _M0L1iS1393, _M0L6_2atmpS4253);
      _M0L4tabsS4256 = _M0L1pS1382->$6;
      _M0L4fireS4258 = _M0L1pS1382->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4258, _M0L1iS1393)) {
        _M0L6_2atmpS4257 = _M0L11tabs__stepsS1390;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4259 = _M0L1pS1382->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS4257
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4259, _M0L1iS1393);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4256, _M0L1iS1393, _M0L6_2atmpS4257);
      goto join_1394;
      goto joinlet_5988;
      join_1394:;
      _M0L6_2atmpS4218 = _M0L1iS1393 + 1;
      _M0L1iS1393 = _M0L6_2atmpS4218;
      continue;
      joinlet_5988:;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS1369,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1372,
  struct _M0TPB5ArrayGfE* _M0L7post__gS1378
) {
  int32_t _M0L4rowsS1368;
  int32_t _M0L7_2abindS1370;
  int32_t _M0L1iS1371;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS1368 = _M0L1mS1369->$0;
  _M0L7_2abindS1370 = 0;
  _M0L1iS1371 = _M0L7_2abindS1370;
  while (1) {
    if (_M0L1iS1371 < _M0L4rowsS1368) {
      int32_t _M0L6_2atmpS4217;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1372, _M0L1iS1371)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS4216 = _M0L1mS1369->$2;
        int32_t _M0L5startS1373;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS4214;
        int32_t _M0L6_2atmpS4215;
        int32_t _M0L3endS1374;
        int32_t _M0L1kS1375;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS1373
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS4216, _M0L1iS1371);
        _M0L6rowptrS4214 = _M0L1mS1369->$2;
        _M0L6_2atmpS4215 = _M0L1iS1371 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS1374
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS4214, _M0L6_2atmpS4215);
        _M0L1kS1375 = _M0L5startS1373;
        while (1) {
          if (_M0L1kS1375 < _M0L3endS1374) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS4212 = _M0L1mS1369->$3;
            int32_t _M0L9post__idxS1376;
            struct _M0TPB5ArrayGfE* _M0L4valsS4211;
            float _M0L1wS1377;
            float _M0L6_2atmpS4210;
            float _M0L6_2atmpS4209;
            int32_t _M0L6_2atmpS4213;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS1376
            = _M0MPC15array5Array2atGiE(_M0L6colptrS4212, _M0L1kS1375);
            _M0L4valsS4211 = _M0L1mS1369->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS1377
            = _M0MPC15array5Array2atGfE(_M0L4valsS4211, _M0L1kS1375);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS4210
            = _M0MPC15array5Array2atGfE(_M0L7post__gS1378, _M0L9post__idxS1376);
            _M0L6_2atmpS4209 = _M0L6_2atmpS4210 + _M0L1wS1377;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS1378, _M0L9post__idxS1376, _M0L6_2atmpS4209);
            _M0L6_2atmpS4213 = _M0L1kS1375 + 1;
            _M0L1kS1375 = _M0L6_2atmpS4213;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS4217 = _M0L1iS1371 + 1;
      _M0L1iS1371 = _M0L6_2atmpS4217;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS1362,
  int32_t _M0L4colsS1363,
  float _M0L2muS1364,
  float _M0L5sigmaS1365,
  float _M0L1pS1366,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1367
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS1362, _M0L4colsS1363, _M0L2muS1364, _M0L5sigmaS1365, _M0L1pS1366, 0, _M0L3rngS1367);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS1276,
  int32_t _M0L4colsS1280,
  float _M0L2muS1286,
  float _M0L5sigmaS1287,
  float _M0L1pS1299,
  int32_t _M0L4ruleS1293,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1289
) {
  float* _M0L6_2atmpS4208;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4207;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS1275;
  int32_t _M0L7_2abindS1277;
  int32_t _M0L1iS1278;
  int32_t _M0L6_2atmpS4206;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1352;
  int32_t* _M0L6_2atmpS4205;
  struct _M0TPB5ArrayGiE* _M0L6colptrS1353;
  float* _M0L6_2atmpS4204;
  struct _M0TPB5ArrayGfE* _M0L4valsS1354;
  int32_t _M0L7_2abindS1355;
  int32_t _M0L1iS1356;
  int32_t _M0L6_2atmpS4203;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_6010;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS4208 = moonbit_empty_float_array;
  _M0L6_2atmpS4207
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4207)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS4207->$0 = _M0L6_2atmpS4208;
  _M0L6_2atmpS4207->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS1275
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS1276, _M0L6_2atmpS4207);
  _M0L7_2abindS1277 = 0;
  _M0L1iS1278 = _M0L7_2abindS1277;
  while (1) {
    if (_M0L1iS1278 < _M0L4rowsS1276) {
      struct _M0TPB5ArrayGfE* _M0L3rowS1279;
      int32_t _M0L7_2abindS1281;
      int32_t _M0L1jS1282;
      int32_t _M0L6_2atmpS4159;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS1279 = _M0MPC15array5Array4makeGfE(_M0L4colsS1280, 0x0p+0f);
      _M0L7_2abindS1281 = 0;
      _M0L1jS1282 = _M0L7_2abindS1281;
      while (1) {
        if (_M0L1jS1282 < _M0L4colsS1280) {
          double _M0L2z1S1284;
          struct _M0TUddE* _M0L7_2abindS1288;
          double _M0L5_2az1S1290;
          float _M0L6_2atmpS4157;
          float _M0L6_2atmpS4156;
          float _M0L1wS1285;
          int32_t _M0L6_2atmpS4158;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS1288
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS1289);
          _M0L5_2az1S1290 = _M0L7_2abindS1288->$0;
          moonbit_decref_cycle_free(_M0L7_2abindS1288);
          _M0L2z1S1284 = _M0L5_2az1S1290;
          goto join_1283;
          goto joinlet_5993;
          join_1283:;
          _M0L6_2atmpS4157 = (float)_M0L2z1S1284;
          _M0L6_2atmpS4156 = _M0L5sigmaS1287 * _M0L6_2atmpS4157;
          _M0L1wS1285 = _M0L2muS1286 + _M0L6_2atmpS4156;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS1279, _M0L1jS1282, _M0L1wS1285);
          joinlet_5993:;
          _M0L6_2atmpS4158 = _M0L1jS1282 + 1;
          _M0L1jS1282 = _M0L6_2atmpS4158;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS1275, _M0L1iS1278, _M0L3rowS1279);
      _M0L6_2atmpS4159 = _M0L1iS1278 + 1;
      _M0L1iS1278 = _M0L6_2atmpS4159;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS1293) {
    case 0: {
      int32_t _M0L7_2abindS1294 = 0;
      int32_t _M0L1iS1295 = _M0L7_2abindS1294;
      while (1) {
        if (_M0L1iS1295 < _M0L4rowsS1276) {
          int32_t _M0L7_2abindS1296 = 0;
          int32_t _M0L1jS1297 = _M0L7_2abindS1296;
          int32_t _M0L6_2atmpS4162;
          while (1) {
            if (_M0L1jS1297 < _M0L4colsS1280) {
              float _M0L1uS1298;
              int32_t _M0L6_2atmpS4161;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS1298 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1289);
              if (_M0L1uS1298 >= _M0L1pS1299) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS4160;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4160
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1275, _M0L1iS1295);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS4160, _M0L1jS1297, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS4160);
              }
              _M0L6_2atmpS4161 = _M0L1jS1297 + 1;
              _M0L1jS1297 = _M0L6_2atmpS4161;
              continue;
            }
            break;
          }
          _M0L6_2atmpS4162 = _M0L1iS1295 + 1;
          _M0L1iS1295 = _M0L6_2atmpS4162;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS4180 = (float)_M0L4rowsS1276;
      float _M0L6_2atmpS4179 = _M0L6_2atmpS4180 * _M0L1pS1299;
      int32_t _M0L7n__keepS1302;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS1302 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4179);
      if (_M0L7n__keepS1302 > 0 && _M0L7n__keepS1302 <= _M0L4rowsS1276) {
        int32_t _M0L7_2abindS1303 = 0;
        int32_t _M0L1jS1304 = _M0L7_2abindS1303;
        while (1) {
          if (_M0L1jS1304 < _M0L4colsS1280) {
            int32_t* _M0L6_2atmpS4174 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS1305 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS1306;
            int32_t _M0L1kS1307;
            int32_t _M0L7n__dropS1309;
            int32_t _M0L7_2abindS1310;
            int32_t _M0L1kS1311;
            int32_t _M0L7_2abindS1317;
            int32_t _M0L1kS1318;
            int32_t _M0L6_2atmpS4175;
            Moonbit_object_header(_M0L8pre__idxS1305)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
            _M0L8pre__idxS1305->$0 = _M0L6_2atmpS4174;
            _M0L8pre__idxS1305->$1 = 0;
            _M0L7_2abindS1306 = 0;
            _M0L1kS1307 = _M0L7_2abindS1306;
            while (1) {
              if (_M0L1kS1307 < _M0L4rowsS1276) {
                int32_t _M0L6_2atmpS4163;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS1305, _M0L1kS1307);
                _M0L6_2atmpS4163 = _M0L1kS1307 + 1;
                _M0L1kS1307 = _M0L6_2atmpS4163;
                continue;
              }
              break;
            }
            _M0L7n__dropS1309 = _M0L4rowsS1276 - _M0L7n__keepS1302;
            _M0L7_2abindS1310 = 0;
            _M0L1kS1311 = _M0L7_2abindS1310;
            while (1) {
              if (_M0L1kS1311 < _M0L7n__dropS1309) {
                float _M0L1uS1312;
                float _M0L6_2atmpS4167;
                float _M0L6_2atmpS4169;
                float _M0L6_2atmpS4168;
                float _M0L6_2atmpS4166;
                int32_t _M0L6_2atmpS4165;
                int32_t _M0L6r__idxS1313;
                int32_t _M0L10r__clampedS1314;
                int32_t _M0L3tmpS1315;
                int32_t _M0L6_2atmpS4164;
                int32_t _M0L6_2atmpS4170;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS1312 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1289);
                _M0L6_2atmpS4167 = (float)_M0L4rowsS1276;
                _M0L6_2atmpS4169 = (float)_M0L1kS1311;
                _M0L6_2atmpS4168 = _M0L6_2atmpS4169 * _M0L1uS1312;
                _M0L6_2atmpS4166 = _M0L6_2atmpS4167 - _M0L6_2atmpS4168;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4165
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS4166);
                _M0L6r__idxS1313 = _M0L1kS1311 + _M0L6_2atmpS4165;
                if (_M0L6r__idxS1313 >= _M0L4rowsS1276) {
                  _M0L10r__clampedS1314 = _M0L4rowsS1276 - 1;
                } else {
                  _M0L10r__clampedS1314 = _M0L6r__idxS1313;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS1315
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS1305, _M0L1kS1311);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4164
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS1305, _M0L10r__clampedS1314);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS1305, _M0L1kS1311, _M0L6_2atmpS4164);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS1305, _M0L10r__clampedS1314, _M0L3tmpS1315);
                _M0L6_2atmpS4170 = _M0L1kS1311 + 1;
                _M0L1kS1311 = _M0L6_2atmpS4170;
                continue;
              }
              break;
            }
            _M0L7_2abindS1317 = 0;
            _M0L1kS1318 = _M0L7_2abindS1317;
            while (1) {
              if (_M0L1kS1318 < _M0L7n__dropS1309) {
                int32_t _M0L6_2atmpS4172;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS4171;
                int32_t _M0L6_2atmpS4173;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4172
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS1305, _M0L1kS1318);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4171
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1275, _M0L6_2atmpS4172);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS4171, _M0L1jS1304, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS4171);
                _M0L6_2atmpS4173 = _M0L1kS1318 + 1;
                _M0L1kS1318 = _M0L6_2atmpS4173;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L8pre__idxS1305);
              }
              break;
            }
            _M0L6_2atmpS4175 = _M0L1jS1304 + 1;
            _M0L1jS1304 = _M0L6_2atmpS4175;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS1302 == 0) {
        int32_t _M0L7_2abindS1321 = 0;
        int32_t _M0L1iS1322 = _M0L7_2abindS1321;
        while (1) {
          if (_M0L1iS1322 < _M0L4rowsS1276) {
            int32_t _M0L7_2abindS1323 = 0;
            int32_t _M0L1jS1324 = _M0L7_2abindS1323;
            int32_t _M0L6_2atmpS4178;
            while (1) {
              if (_M0L1jS1324 < _M0L4colsS1280) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS4176;
                int32_t _M0L6_2atmpS4177;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4176
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1275, _M0L1iS1322);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS4176, _M0L1jS1324, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS4176);
                _M0L6_2atmpS4177 = _M0L1jS1324 + 1;
                _M0L1jS1324 = _M0L6_2atmpS4177;
                continue;
              }
              break;
            }
            _M0L6_2atmpS4178 = _M0L1iS1322 + 1;
            _M0L1iS1322 = _M0L6_2atmpS4178;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS4198 = (float)_M0L4colsS1280;
      float _M0L6_2atmpS4197 = _M0L6_2atmpS4198 * _M0L1pS1299;
      int32_t _M0L7n__keepS1327;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS1327 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4197);
      if (_M0L7n__keepS1327 > 0 && _M0L7n__keepS1327 <= _M0L4colsS1280) {
        int32_t _M0L7_2abindS1328 = 0;
        int32_t _M0L1iS1329 = _M0L7_2abindS1328;
        while (1) {
          if (_M0L1iS1329 < _M0L4rowsS1276) {
            int32_t* _M0L6_2atmpS4192 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS1330 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS1331;
            int32_t _M0L1kS1332;
            int32_t _M0L7n__dropS1334;
            int32_t _M0L7_2abindS1335;
            int32_t _M0L1kS1336;
            int32_t _M0L7_2abindS1342;
            int32_t _M0L1kS1343;
            int32_t _M0L6_2atmpS4193;
            Moonbit_object_header(_M0L9post__idxS1330)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
            _M0L9post__idxS1330->$0 = _M0L6_2atmpS4192;
            _M0L9post__idxS1330->$1 = 0;
            _M0L7_2abindS1331 = 0;
            _M0L1kS1332 = _M0L7_2abindS1331;
            while (1) {
              if (_M0L1kS1332 < _M0L4colsS1280) {
                int32_t _M0L6_2atmpS4181;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS1330, _M0L1kS1332);
                _M0L6_2atmpS4181 = _M0L1kS1332 + 1;
                _M0L1kS1332 = _M0L6_2atmpS4181;
                continue;
              }
              break;
            }
            _M0L7n__dropS1334 = _M0L4colsS1280 - _M0L7n__keepS1327;
            _M0L7_2abindS1335 = 0;
            _M0L1kS1336 = _M0L7_2abindS1335;
            while (1) {
              if (_M0L1kS1336 < _M0L7n__dropS1334) {
                float _M0L1uS1337;
                float _M0L6_2atmpS4185;
                float _M0L6_2atmpS4187;
                float _M0L6_2atmpS4186;
                float _M0L6_2atmpS4184;
                int32_t _M0L6_2atmpS4183;
                int32_t _M0L6r__idxS1338;
                int32_t _M0L10r__clampedS1339;
                int32_t _M0L3tmpS1340;
                int32_t _M0L6_2atmpS4182;
                int32_t _M0L6_2atmpS4188;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS1337 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1289);
                _M0L6_2atmpS4185 = (float)_M0L4colsS1280;
                _M0L6_2atmpS4187 = (float)_M0L1kS1336;
                _M0L6_2atmpS4186 = _M0L6_2atmpS4187 * _M0L1uS1337;
                _M0L6_2atmpS4184 = _M0L6_2atmpS4185 - _M0L6_2atmpS4186;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4183
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS4184);
                _M0L6r__idxS1338 = _M0L1kS1336 + _M0L6_2atmpS4183;
                if (_M0L6r__idxS1338 >= _M0L4colsS1280) {
                  _M0L10r__clampedS1339 = _M0L4colsS1280 - 1;
                } else {
                  _M0L10r__clampedS1339 = _M0L6r__idxS1338;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS1340
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS1330, _M0L1kS1336);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4182
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS1330, _M0L10r__clampedS1339);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS1330, _M0L1kS1336, _M0L6_2atmpS4182);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS1330, _M0L10r__clampedS1339, _M0L3tmpS1340);
                _M0L6_2atmpS4188 = _M0L1kS1336 + 1;
                _M0L1kS1336 = _M0L6_2atmpS4188;
                continue;
              }
              break;
            }
            _M0L7_2abindS1342 = 0;
            _M0L1kS1343 = _M0L7_2abindS1342;
            while (1) {
              if (_M0L1kS1343 < _M0L7n__dropS1334) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS4189;
                int32_t _M0L6_2atmpS4190;
                int32_t _M0L6_2atmpS4191;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4189
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1275, _M0L1iS1329);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4190
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS1330, _M0L1kS1343);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS4189, _M0L6_2atmpS4190, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS4189);
                _M0L6_2atmpS4191 = _M0L1kS1343 + 1;
                _M0L1kS1343 = _M0L6_2atmpS4191;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L9post__idxS1330);
              }
              break;
            }
            _M0L6_2atmpS4193 = _M0L1iS1329 + 1;
            _M0L1iS1329 = _M0L6_2atmpS4193;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS1327 == 0) {
        int32_t _M0L7_2abindS1346 = 0;
        int32_t _M0L1iS1347 = _M0L7_2abindS1346;
        while (1) {
          if (_M0L1iS1347 < _M0L4rowsS1276) {
            int32_t _M0L7_2abindS1348 = 0;
            int32_t _M0L1jS1349 = _M0L7_2abindS1348;
            int32_t _M0L6_2atmpS4196;
            while (1) {
              if (_M0L1jS1349 < _M0L4colsS1280) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS4194;
                int32_t _M0L6_2atmpS4195;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS4194
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1275, _M0L1iS1347);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS4194, _M0L1jS1349, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS4194);
                _M0L6_2atmpS4195 = _M0L1jS1349 + 1;
                _M0L1jS1349 = _M0L6_2atmpS4195;
                continue;
              }
              break;
            }
            _M0L6_2atmpS4196 = _M0L1iS1347 + 1;
            _M0L1iS1347 = _M0L6_2atmpS4196;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS4206 = _M0L4rowsS1276 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS1352 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS4206, 0);
  _M0L6_2atmpS4205 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS1353
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS1353)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _M0L6colptrS1353->$0 = _M0L6_2atmpS4205;
  _M0L6colptrS1353->$1 = 0;
  _M0L6_2atmpS4204 = moonbit_empty_float_array;
  _M0L4valsS1354
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS1354)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L4valsS1354->$0 = _M0L6_2atmpS4204;
  _M0L4valsS1354->$1 = 0;
  _M0L7_2abindS1355 = 0;
  _M0L1iS1356 = _M0L7_2abindS1355;
  while (1) {
    if (_M0L1iS1356 < _M0L4rowsS1276) {
      int32_t _M0L6_2atmpS4199;
      int32_t _M0L7_2abindS1357;
      int32_t _M0L1jS1358;
      int32_t _M0L6_2atmpS4202;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS4199 = _M0MPC15array5Array6lengthGfE(_M0L4valsS1354);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS1352, _M0L1iS1356, _M0L6_2atmpS4199);
      _M0L7_2abindS1357 = 0;
      _M0L1jS1358 = _M0L7_2abindS1357;
      while (1) {
        if (_M0L1jS1358 < _M0L4colsS1280) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS4200;
          float _M0L1vS1359;
          int32_t _M0L6_2atmpS4201;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS4200
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS1275, _M0L1iS1356);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS1359
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS4200, _M0L1jS1358);
          moonbit_decref_cycle_free(_M0L6_2atmpS4200);
          if (_M0L1vS1359 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS1353, _M0L1jS1358);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS1354, _M0L1vS1359);
          }
          _M0L6_2atmpS4201 = _M0L1jS1358 + 1;
          _M0L1jS1358 = _M0L6_2atmpS4201;
          continue;
        }
        break;
      }
      _M0L6_2atmpS4202 = _M0L1iS1356 + 1;
      _M0L1iS1356 = _M0L6_2atmpS4202;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L5denseS1275);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS4203 = _M0MPC15array5Array6lengthGfE(_M0L4valsS1354);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS1352, _M0L4rowsS1276, _M0L6_2atmpS4203);
  _block_6010
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_6010)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 81, 0);
  _block_6010->$0 = _M0L4rowsS1276;
  _block_6010->$1 = _M0L4colsS1280;
  _block_6010->$2 = _M0L6rowptrS1352;
  _block_6010->$3 = _M0L6colptrS1353;
  _block_6010->$4 = _M0L4valsS1354;
  return _block_6010;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3nnz(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS1274
) {
  struct _M0TPB5ArrayGfE* _M0L4valsS4155;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4valsS4155 = _M0L1mS1274->$4;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MPC15array5Array6lengthGfE(_M0L4valsS4155);
}

int32_t _M0FP26RiantR8snn__mbt20ca__plasticity__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1252,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1239,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1241,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1251,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1247,
  struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L4varsS1236,
  struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L5paramS1243,
  float _M0L6t__nowS1237,
  float _M0L2dtS1264
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS4046;
  int32_t _M0L6_2atmpS4045;
  int32_t _if__result_6011;
  int32_t _M0L6n__preS1238;
  int32_t _M0L7n__postS1240;
  float _M0L8tau__preS4154;
  float _M0L13inv__tau__preS1242;
  float _M0L9tau__postS4153;
  float _M0L14inv__tau__postS1244;
  struct _M0TPB8MutLocalGiE* _M0L1jS1245;
  struct _M0TPB8MutLocalGiE* _M0L1kS1255;
  struct _M0TPB8MutLocalGiE* _M0L2jjS1263;
  struct _M0TPB8MutLocalGiE* _M0L2iiS1266;
  struct _M0TPB8MutLocalGiE* _M0L3jj2S1268;
  struct _M0TPB8MutLocalGiE* _M0L3ii2S1270;
  struct _M0TPB8MutLocalGiE* _M0L2s2S1272;
  #line 1495 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6activeS4046 = _M0L4varsS1236->$4;
  #line 1507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS4045 = _M0MPC15array5Array6lengthGbE(_M0L6activeS4046);
  if (_M0L6_2atmpS4045 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS4044 = _M0L4varsS1236->$4;
    int32_t _M0L6_2atmpS4043;
    #line 1507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS4043 = _M0MPC15array5Array2atGbE(_M0L6activeS4044, 0);
    _if__result_6011 = !_M0L6_2atmpS4043;
  } else {
    _if__result_6011 = 0;
  }
  if (_if__result_6011) {
    return 0;
  }
  #line 1509 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1238 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1239);
  #line 1510 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1240 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1241);
  _M0L8tau__preS4154 = _M0L5paramS1243->$2;
  _M0L13inv__tau__preS1242 = 0x1p+0f / _M0L8tau__preS4154;
  _M0L9tau__postS4153 = _M0L5paramS1243->$3;
  _M0L14inv__tau__postS1244 = 0x1p+0f / _M0L9tau__postS4153;
  _M0L1jS1245
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1245)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1245->$0 = 0;
  while (1) {
    int32_t _M0L3valS4047 = _M0L1jS1245->$0;
    if (_M0L3valS4047 < _M0L6n__preS1238) {
      int32_t _M0L3valS4048 = _M0L1jS1245->$0;
      int32_t _M0L3valS4063;
      int32_t _M0L6_2atmpS4062;
      #line 1516 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1239, _M0L3valS4048)) {
        int32_t _M0L3valS4061 = _M0L1jS1245->$0;
        int32_t _M0L5startS1246;
        int32_t _M0L3valS4060;
        int32_t _M0L6_2atmpS4059;
        int32_t _M0L5end__S1248;
        struct _M0TPB8MutLocalGiE* _M0L1sS1249;
        #line 1517 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS1246
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1247, _M0L3valS4061);
        _M0L3valS4060 = _M0L1jS1245->$0;
        _M0L6_2atmpS4059 = _M0L3valS4060 + 1;
        #line 1518 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5end__S1248
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1247, _M0L6_2atmpS4059);
        _M0L1sS1249
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS1249)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS1249->$0 = _M0L5startS1246;
        while (1) {
          int32_t _M0L3valS4049 = _M0L1sS1249->$0;
          if (_M0L3valS4049 < _M0L5end__S1248) {
            int32_t _M0L3valS4058 = _M0L1sS1249->$0;
            int32_t _M0L1iS1250;
            int32_t _M0L3valS4050;
            int32_t _M0L3valS4055;
            float _M0L6_2atmpS4052;
            struct _M0TPB5ArrayGfE* _M0L5tpostS4054;
            float _M0L6_2atmpS4053;
            float _M0L6_2atmpS4051;
            int32_t _M0L3valS4057;
            int32_t _M0L6_2atmpS4056;
            #line 1521 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L1iS1250
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1251, _M0L3valS4058);
            _M0L3valS4050 = _M0L1sS1249->$0;
            _M0L3valS4055 = _M0L1sS1249->$0;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS4052
            = _M0MPC15array5Array2atGfE(_M0L1wS1252, _M0L3valS4055);
            _M0L5tpostS4054 = _M0L4varsS1236->$3;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS4053
            = _M0MPC15array5Array2atGfE(_M0L5tpostS4054, _M0L1iS1250);
            _M0L6_2atmpS4051 = _M0L6_2atmpS4052 + _M0L6_2atmpS4053;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1252, _M0L3valS4050, _M0L6_2atmpS4051);
            _M0L3valS4057 = _M0L1sS1249->$0;
            _M0L6_2atmpS4056 = _M0L3valS4057 + 1;
            _M0L1sS1249->$0 = _M0L6_2atmpS4056;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS1249);
          }
          break;
        }
      }
      _M0L3valS4063 = _M0L1jS1245->$0;
      _M0L6_2atmpS4062 = _M0L3valS4063 + 1;
      _M0L1jS1245->$0 = _M0L6_2atmpS4062;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1245);
    }
    break;
  }
  _M0L1kS1255
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS1255)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS1255->$0 = 0;
  while (1) {
    int32_t _M0L3valS4064 = _M0L1kS1255->$0;
    if (_M0L3valS4064 < _M0L7n__postS1240) {
      int32_t _M0L3valS4065 = _M0L1kS1255->$0;
      int32_t _M0L3valS4086;
      int32_t _M0L6_2atmpS4085;
      #line 1531 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1241, _M0L3valS4065)) {
        struct _M0TPB8MutLocalGiE* _M0L2j2S1256 =
          (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L2j2S1256)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L2j2S1256->$0 = 0;
        while (1) {
          int32_t _M0L3valS4066 = _M0L2j2S1256->$0;
          if (_M0L3valS4066 < _M0L6n__preS1238) {
            int32_t _M0L3valS4084 = _M0L2j2S1256->$0;
            int32_t _M0L5startS1257;
            int32_t _M0L3valS4083;
            int32_t _M0L6_2atmpS4082;
            int32_t _M0L5end__S1258;
            struct _M0TPB8MutLocalGiE* _M0L1sS1259;
            int32_t _M0L3valS4081;
            int32_t _M0L6_2atmpS4080;
            #line 1537 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L5startS1257
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS1247, _M0L3valS4084);
            _M0L3valS4083 = _M0L2j2S1256->$0;
            _M0L6_2atmpS4082 = _M0L3valS4083 + 1;
            #line 1538 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L5end__S1258
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS1247, _M0L6_2atmpS4082);
            _M0L1sS1259
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS1259)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS1259->$0 = _M0L5startS1257;
            while (1) {
              int32_t _M0L3valS4067 = _M0L1sS1259->$0;
              if (_M0L3valS4067 < _M0L5end__S1258) {
                int32_t _M0L3valS4070 = _M0L1sS1259->$0;
                int32_t _M0L6_2atmpS4068;
                int32_t _M0L3valS4069;
                int32_t _M0L3valS4079;
                int32_t _M0L6_2atmpS4078;
                #line 1541 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                _M0L6_2atmpS4068
                = _M0MPC15array5Array2atGiE(_M0L6colptrS1251, _M0L3valS4070);
                _M0L3valS4069 = _M0L1kS1255->$0;
                if (_M0L6_2atmpS4068 == _M0L3valS4069) {
                  int32_t _M0L3valS4071 = _M0L1sS1259->$0;
                  int32_t _M0L3valS4077 = _M0L1sS1259->$0;
                  float _M0L6_2atmpS4073;
                  struct _M0TPB5ArrayGfE* _M0L4tpreS4075;
                  int32_t _M0L3valS4076;
                  float _M0L6_2atmpS4074;
                  float _M0L6_2atmpS4072;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0L6_2atmpS4073
                  = _M0MPC15array5Array2atGfE(_M0L1wS1252, _M0L3valS4077);
                  _M0L4tpreS4075 = _M0L4varsS1236->$2;
                  _M0L3valS4076 = _M0L2j2S1256->$0;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0L6_2atmpS4074
                  = _M0MPC15array5Array2atGfE(_M0L4tpreS4075, _M0L3valS4076);
                  _M0L6_2atmpS4072 = _M0L6_2atmpS4073 + _M0L6_2atmpS4074;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0MPC15array5Array3setGfE(_M0L1wS1252, _M0L3valS4071, _M0L6_2atmpS4072);
                }
                _M0L3valS4079 = _M0L1sS1259->$0;
                _M0L6_2atmpS4078 = _M0L3valS4079 + 1;
                _M0L1sS1259->$0 = _M0L6_2atmpS4078;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS1259);
              }
              break;
            }
            _M0L3valS4081 = _M0L2j2S1256->$0;
            _M0L6_2atmpS4080 = _M0L3valS4081 + 1;
            _M0L2j2S1256->$0 = _M0L6_2atmpS4080;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L2j2S1256);
          }
          break;
        }
      }
      _M0L3valS4086 = _M0L1kS1255->$0;
      _M0L6_2atmpS4085 = _M0L3valS4086 + 1;
      _M0L1kS1255->$0 = _M0L6_2atmpS4085;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS1255);
    }
    break;
  }
  _M0L2jjS1263
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2jjS1263)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2jjS1263->$0 = 0;
  while (1) {
    int32_t _M0L3valS4087 = _M0L2jjS1263->$0;
    if (_M0L3valS4087 < _M0L6n__preS1238) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS4088 = _M0L4varsS1236->$2;
      int32_t _M0L3valS4089 = _M0L2jjS1263->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4098 = _M0L4varsS1236->$2;
      int32_t _M0L3valS4099 = _M0L2jjS1263->$0;
      float _M0L6_2atmpS4091;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4096;
      int32_t _M0L3valS4097;
      float _M0L6_2atmpS4095;
      float _M0L6_2atmpS4094;
      float _M0L6_2atmpS4093;
      float _M0L6_2atmpS4092;
      float _M0L6_2atmpS4090;
      int32_t _M0L3valS4101;
      int32_t _M0L6_2atmpS4100;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4091
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4098, _M0L3valS4099);
      _M0L4tpreS4096 = _M0L4varsS1236->$2;
      _M0L3valS4097 = _M0L2jjS1263->$0;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4095
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4096, _M0L3valS4097);
      _M0L6_2atmpS4094 = -_M0L6_2atmpS4095;
      _M0L6_2atmpS4093 = _M0L2dtS1264 * _M0L6_2atmpS4094;
      _M0L6_2atmpS4092 = _M0L6_2atmpS4093 * _M0L13inv__tau__preS1242;
      _M0L6_2atmpS4090 = _M0L6_2atmpS4091 + _M0L6_2atmpS4092;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS4088, _M0L3valS4089, _M0L6_2atmpS4090);
      _M0L3valS4101 = _M0L2jjS1263->$0;
      _M0L6_2atmpS4100 = _M0L3valS4101 + 1;
      _M0L2jjS1263->$0 = _M0L6_2atmpS4100;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2jjS1263);
    }
    break;
  }
  _M0L2iiS1266
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2iiS1266)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2iiS1266->$0 = 0;
  while (1) {
    int32_t _M0L3valS4102 = _M0L2iiS1266->$0;
    if (_M0L3valS4102 < _M0L7n__postS1240) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS4103 = _M0L4varsS1236->$3;
      int32_t _M0L3valS4104 = _M0L2iiS1266->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS4113 = _M0L4varsS1236->$3;
      int32_t _M0L3valS4114 = _M0L2iiS1266->$0;
      float _M0L6_2atmpS4106;
      struct _M0TPB5ArrayGfE* _M0L5tpostS4111;
      int32_t _M0L3valS4112;
      float _M0L6_2atmpS4110;
      float _M0L6_2atmpS4109;
      float _M0L6_2atmpS4108;
      float _M0L6_2atmpS4107;
      float _M0L6_2atmpS4105;
      int32_t _M0L3valS4116;
      int32_t _M0L6_2atmpS4115;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4106
      = _M0MPC15array5Array2atGfE(_M0L5tpostS4113, _M0L3valS4114);
      _M0L5tpostS4111 = _M0L4varsS1236->$3;
      _M0L3valS4112 = _M0L2iiS1266->$0;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4110
      = _M0MPC15array5Array2atGfE(_M0L5tpostS4111, _M0L3valS4112);
      _M0L6_2atmpS4109 = -_M0L6_2atmpS4110;
      _M0L6_2atmpS4108 = _M0L2dtS1264 * _M0L6_2atmpS4109;
      _M0L6_2atmpS4107 = _M0L6_2atmpS4108 * _M0L14inv__tau__postS1244;
      _M0L6_2atmpS4105 = _M0L6_2atmpS4106 + _M0L6_2atmpS4107;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS4103, _M0L3valS4104, _M0L6_2atmpS4105);
      _M0L3valS4116 = _M0L2iiS1266->$0;
      _M0L6_2atmpS4115 = _M0L3valS4116 + 1;
      _M0L2iiS1266->$0 = _M0L6_2atmpS4115;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2iiS1266);
    }
    break;
  }
  _M0L3jj2S1268
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3jj2S1268)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3jj2S1268->$0 = 0;
  while (1) {
    int32_t _M0L3valS4117 = _M0L3jj2S1268->$0;
    if (_M0L3valS4117 < _M0L6n__preS1238) {
      int32_t _M0L3valS4118 = _M0L3jj2S1268->$0;
      int32_t _M0L3valS4127;
      int32_t _M0L6_2atmpS4126;
      #line 1565 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1239, _M0L3valS4118)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS4119 = _M0L4varsS1236->$2;
        int32_t _M0L3valS4120 = _M0L3jj2S1268->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS4124 = _M0L4varsS1236->$2;
        int32_t _M0L3valS4125 = _M0L3jj2S1268->$0;
        float _M0L6_2atmpS4122;
        float _M0L6a__preS4123;
        float _M0L6_2atmpS4121;
        #line 1566 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS4122
        = _M0MPC15array5Array2atGfE(_M0L4tpreS4124, _M0L3valS4125);
        _M0L6a__preS4123 = _M0L5paramS1243->$0;
        _M0L6_2atmpS4121 = _M0L6_2atmpS4122 + _M0L6a__preS4123;
        #line 1566 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS4119, _M0L3valS4120, _M0L6_2atmpS4121);
      }
      _M0L3valS4127 = _M0L3jj2S1268->$0;
      _M0L6_2atmpS4126 = _M0L3valS4127 + 1;
      _M0L3jj2S1268->$0 = _M0L6_2atmpS4126;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L3jj2S1268);
    }
    break;
  }
  _M0L3ii2S1270
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3ii2S1270)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3ii2S1270->$0 = 0;
  while (1) {
    int32_t _M0L3valS4128 = _M0L3ii2S1270->$0;
    if (_M0L3valS4128 < _M0L7n__postS1240) {
      int32_t _M0L3valS4129 = _M0L3ii2S1270->$0;
      int32_t _M0L3valS4138;
      int32_t _M0L6_2atmpS4137;
      #line 1572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1241, _M0L3valS4129)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS4130 = _M0L4varsS1236->$3;
        int32_t _M0L3valS4131 = _M0L3ii2S1270->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS4135 = _M0L4varsS1236->$3;
        int32_t _M0L3valS4136 = _M0L3ii2S1270->$0;
        float _M0L6_2atmpS4133;
        float _M0L7a__postS4134;
        float _M0L6_2atmpS4132;
        #line 1573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS4133
        = _M0MPC15array5Array2atGfE(_M0L5tpostS4135, _M0L3valS4136);
        _M0L7a__postS4134 = _M0L5paramS1243->$1;
        _M0L6_2atmpS4132 = _M0L6_2atmpS4133 + _M0L7a__postS4134;
        #line 1573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS4130, _M0L3valS4131, _M0L6_2atmpS4132);
      }
      _M0L3valS4138 = _M0L3ii2S1270->$0;
      _M0L6_2atmpS4137 = _M0L3valS4138 + 1;
      _M0L3ii2S1270->$0 = _M0L6_2atmpS4137;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L3ii2S1270);
    }
    break;
  }
  _M0L2s2S1272
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S1272)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S1272->$0 = 0;
  while (1) {
    int32_t _M0L3valS4139 = _M0L2s2S1272->$0;
    int32_t _M0L6_2atmpS4140;
    #line 1579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS4140 = _M0MPC15array5Array6lengthGfE(_M0L1wS1252);
    if (_M0L3valS4139 < _M0L6_2atmpS4140) {
      int32_t _M0L3valS4143 = _M0L2s2S1272->$0;
      float _M0L6_2atmpS4141;
      float _M0L6w__minS4142;
      int32_t _M0L3valS4148;
      float _M0L6_2atmpS4146;
      float _M0L6w__maxS4147;
      int32_t _M0L3valS4152;
      int32_t _M0L6_2atmpS4151;
      #line 1580 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4141
      = _M0MPC15array5Array2atGfE(_M0L1wS1252, _M0L3valS4143);
      _M0L6w__minS4142 = _M0L5paramS1243->$5;
      if (_M0L6_2atmpS4141 < _M0L6w__minS4142) {
        int32_t _M0L3valS4144 = _M0L2s2S1272->$0;
        float _M0L6w__minS4145 = _M0L5paramS1243->$5;
        #line 1580 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1252, _M0L3valS4144, _M0L6w__minS4145);
      }
      _M0L3valS4148 = _M0L2s2S1272->$0;
      #line 1581 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4146
      = _M0MPC15array5Array2atGfE(_M0L1wS1252, _M0L3valS4148);
      _M0L6w__maxS4147 = _M0L5paramS1243->$4;
      if (_M0L6_2atmpS4146 > _M0L6w__maxS4147) {
        int32_t _M0L3valS4149 = _M0L2s2S1272->$0;
        float _M0L6w__maxS4150 = _M0L5paramS1243->$4;
        #line 1581 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1252, _M0L3valS4149, _M0L6w__maxS4150);
      }
      _M0L3valS4152 = _M0L2s2S1272->$0;
      _M0L6_2atmpS4151 = _M0L3valS4152 + 1;
      _M0L2s2S1272->$0 = _M0L6_2atmpS4151;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S1272);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt21stdp__symmetric__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1232,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1205,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1207,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1227,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1220,
  struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L4varsS1212,
  struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L5paramS1209,
  float _M0L6t__nowS1203,
  float _M0L2dtS1213
) {
  int32_t _M0L6n__preS1204;
  int32_t _M0L7n__postS1206;
  float _M0L6tau__xS4042;
  float _M0L11inv__tau__xS1208;
  float _M0L6tau__yS4041;
  float _M0L11inv__tau__yS1210;
  struct _M0TPB8MutLocalGiE* _M0L1jS1211;
  struct _M0TPB8MutLocalGiE* _M0L1iS1215;
  float _M0L4a__xS4038;
  float _M0L6tau__xS4040;
  float _M0L6_2atmpS4039;
  float _M0L7coef__xS1217;
  float _M0L4a__yS4035;
  float _M0L6tau__yS4037;
  float _M0L6_2atmpS4036;
  float _M0L7coef__yS1218;
  #line 1296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 1308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1204 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1205);
  #line 1309 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1206 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1207);
  _M0L6tau__xS4042 = _M0L5paramS1209->$2;
  _M0L11inv__tau__xS1208 = 0x1p+0f / _M0L6tau__xS4042;
  _M0L6tau__yS4041 = _M0L5paramS1209->$3;
  _M0L11inv__tau__yS1210 = 0x1p+0f / _M0L6tau__yS4041;
  _M0L1jS1211
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1211)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1211->$0 = 0;
  while (1) {
    int32_t _M0L3valS3912 = _M0L1jS1211->$0;
    if (_M0L3valS3912 < _M0L6n__preS1204) {
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3913 = _M0L4varsS1212->$0;
      int32_t _M0L3valS3914 = _M0L1jS1211->$0;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3923 = _M0L4varsS1212->$0;
      int32_t _M0L3valS3924 = _M0L1jS1211->$0;
      float _M0L6_2atmpS3916;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3921;
      int32_t _M0L3valS3922;
      float _M0L6_2atmpS3920;
      float _M0L6_2atmpS3919;
      float _M0L6_2atmpS3918;
      float _M0L6_2atmpS3917;
      float _M0L6_2atmpS3915;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3925;
      int32_t _M0L3valS3926;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3935;
      int32_t _M0L3valS3936;
      float _M0L6_2atmpS3928;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3933;
      int32_t _M0L3valS3934;
      float _M0L6_2atmpS3932;
      float _M0L6_2atmpS3931;
      float _M0L6_2atmpS3930;
      float _M0L6_2atmpS3929;
      float _M0L6_2atmpS3927;
      int32_t _M0L3valS3937;
      int32_t _M0L3valS3951;
      int32_t _M0L6_2atmpS3950;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3916
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3923, _M0L3valS3924);
      _M0L5tr__xS3921 = _M0L4varsS1212->$0;
      _M0L3valS3922 = _M0L1jS1211->$0;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3920
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3921, _M0L3valS3922);
      _M0L6_2atmpS3919 = -_M0L6_2atmpS3920;
      _M0L6_2atmpS3918 = _M0L2dtS1213 * _M0L6_2atmpS3919;
      _M0L6_2atmpS3917 = _M0L6_2atmpS3918 * _M0L11inv__tau__xS1208;
      _M0L6_2atmpS3915 = _M0L6_2atmpS3916 + _M0L6_2atmpS3917;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__xS3913, _M0L3valS3914, _M0L6_2atmpS3915);
      _M0L5tr__yS3925 = _M0L4varsS1212->$1;
      _M0L3valS3926 = _M0L1jS1211->$0;
      _M0L5tr__yS3935 = _M0L4varsS1212->$1;
      _M0L3valS3936 = _M0L1jS1211->$0;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3928
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS3935, _M0L3valS3936);
      _M0L5tr__yS3933 = _M0L4varsS1212->$1;
      _M0L3valS3934 = _M0L1jS1211->$0;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3932
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS3933, _M0L3valS3934);
      _M0L6_2atmpS3931 = -_M0L6_2atmpS3932;
      _M0L6_2atmpS3930 = _M0L2dtS1213 * _M0L6_2atmpS3931;
      _M0L6_2atmpS3929 = _M0L6_2atmpS3930 * _M0L11inv__tau__yS1210;
      _M0L6_2atmpS3927 = _M0L6_2atmpS3928 + _M0L6_2atmpS3929;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__yS3925, _M0L3valS3926, _M0L6_2atmpS3927);
      _M0L3valS3937 = _M0L1jS1211->$0;
      #line 1318 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1205, _M0L3valS3937)) {
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3938 = _M0L4varsS1212->$0;
        int32_t _M0L3valS3939 = _M0L1jS1211->$0;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3942 = _M0L4varsS1212->$0;
        int32_t _M0L3valS3943 = _M0L1jS1211->$0;
        float _M0L6_2atmpS3941;
        float _M0L6_2atmpS3940;
        struct _M0TPB5ArrayGfE* _M0L5tr__yS3944;
        int32_t _M0L3valS3945;
        struct _M0TPB5ArrayGfE* _M0L5tr__yS3948;
        int32_t _M0L3valS3949;
        float _M0L6_2atmpS3947;
        float _M0L6_2atmpS3946;
        #line 1319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3941
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3942, _M0L3valS3943);
        _M0L6_2atmpS3940 = _M0L6_2atmpS3941 + 0x1p+0f;
        #line 1319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__xS3938, _M0L3valS3939, _M0L6_2atmpS3940);
        _M0L5tr__yS3944 = _M0L4varsS1212->$1;
        _M0L3valS3945 = _M0L1jS1211->$0;
        _M0L5tr__yS3948 = _M0L4varsS1212->$1;
        _M0L3valS3949 = _M0L1jS1211->$0;
        #line 1320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3947
        = _M0MPC15array5Array2atGfE(_M0L5tr__yS3948, _M0L3valS3949);
        _M0L6_2atmpS3946 = _M0L6_2atmpS3947 + 0x1p+0f;
        #line 1320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__yS3944, _M0L3valS3945, _M0L6_2atmpS3946);
      }
      _M0L3valS3951 = _M0L1jS1211->$0;
      _M0L6_2atmpS3950 = _M0L3valS3951 + 1;
      _M0L1jS1211->$0 = _M0L6_2atmpS3950;
      continue;
    }
    break;
  }
  _M0L1iS1215
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1215)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1215->$0 = 0;
  while (1) {
    int32_t _M0L3valS3952 = _M0L1iS1215->$0;
    if (_M0L3valS3952 < _M0L7n__postS1206) {
      struct _M0TPB5ArrayGfE* _M0L5to__xS3953 = _M0L4varsS1212->$2;
      int32_t _M0L3valS3954 = _M0L1iS1215->$0;
      struct _M0TPB5ArrayGfE* _M0L5to__xS3963 = _M0L4varsS1212->$2;
      int32_t _M0L3valS3964 = _M0L1iS1215->$0;
      float _M0L6_2atmpS3956;
      struct _M0TPB5ArrayGfE* _M0L5to__xS3961;
      int32_t _M0L3valS3962;
      float _M0L6_2atmpS3960;
      float _M0L6_2atmpS3959;
      float _M0L6_2atmpS3958;
      float _M0L6_2atmpS3957;
      float _M0L6_2atmpS3955;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3965;
      int32_t _M0L3valS3966;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3975;
      int32_t _M0L3valS3976;
      float _M0L6_2atmpS3968;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3973;
      int32_t _M0L3valS3974;
      float _M0L6_2atmpS3972;
      float _M0L6_2atmpS3971;
      float _M0L6_2atmpS3970;
      float _M0L6_2atmpS3969;
      float _M0L6_2atmpS3967;
      int32_t _M0L3valS3977;
      int32_t _M0L3valS3991;
      int32_t _M0L6_2atmpS3990;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3956
      = _M0MPC15array5Array2atGfE(_M0L5to__xS3963, _M0L3valS3964);
      _M0L5to__xS3961 = _M0L4varsS1212->$2;
      _M0L3valS3962 = _M0L1iS1215->$0;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3960
      = _M0MPC15array5Array2atGfE(_M0L5to__xS3961, _M0L3valS3962);
      _M0L6_2atmpS3959 = -_M0L6_2atmpS3960;
      _M0L6_2atmpS3958 = _M0L2dtS1213 * _M0L6_2atmpS3959;
      _M0L6_2atmpS3957 = _M0L6_2atmpS3958 * _M0L11inv__tau__xS1208;
      _M0L6_2atmpS3955 = _M0L6_2atmpS3956 + _M0L6_2atmpS3957;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__xS3953, _M0L3valS3954, _M0L6_2atmpS3955);
      _M0L5to__yS3965 = _M0L4varsS1212->$3;
      _M0L3valS3966 = _M0L1iS1215->$0;
      _M0L5to__yS3975 = _M0L4varsS1212->$3;
      _M0L3valS3976 = _M0L1iS1215->$0;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3968
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3975, _M0L3valS3976);
      _M0L5to__yS3973 = _M0L4varsS1212->$3;
      _M0L3valS3974 = _M0L1iS1215->$0;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3972
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3973, _M0L3valS3974);
      _M0L6_2atmpS3971 = -_M0L6_2atmpS3972;
      _M0L6_2atmpS3970 = _M0L2dtS1213 * _M0L6_2atmpS3971;
      _M0L6_2atmpS3969 = _M0L6_2atmpS3970 * _M0L11inv__tau__yS1210;
      _M0L6_2atmpS3967 = _M0L6_2atmpS3968 + _M0L6_2atmpS3969;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__yS3965, _M0L3valS3966, _M0L6_2atmpS3967);
      _M0L3valS3977 = _M0L1iS1215->$0;
      #line 1328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1207, _M0L3valS3977)) {
        struct _M0TPB5ArrayGfE* _M0L5to__xS3978 = _M0L4varsS1212->$2;
        int32_t _M0L3valS3979 = _M0L1iS1215->$0;
        struct _M0TPB5ArrayGfE* _M0L5to__xS3982 = _M0L4varsS1212->$2;
        int32_t _M0L3valS3983 = _M0L1iS1215->$0;
        float _M0L6_2atmpS3981;
        float _M0L6_2atmpS3980;
        struct _M0TPB5ArrayGfE* _M0L5to__yS3984;
        int32_t _M0L3valS3985;
        struct _M0TPB5ArrayGfE* _M0L5to__yS3988;
        int32_t _M0L3valS3989;
        float _M0L6_2atmpS3987;
        float _M0L6_2atmpS3986;
        #line 1329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3981
        = _M0MPC15array5Array2atGfE(_M0L5to__xS3982, _M0L3valS3983);
        _M0L6_2atmpS3980 = _M0L6_2atmpS3981 + 0x1p+0f;
        #line 1329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__xS3978, _M0L3valS3979, _M0L6_2atmpS3980);
        _M0L5to__yS3984 = _M0L4varsS1212->$3;
        _M0L3valS3985 = _M0L1iS1215->$0;
        _M0L5to__yS3988 = _M0L4varsS1212->$3;
        _M0L3valS3989 = _M0L1iS1215->$0;
        #line 1330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3987
        = _M0MPC15array5Array2atGfE(_M0L5to__yS3988, _M0L3valS3989);
        _M0L6_2atmpS3986 = _M0L6_2atmpS3987 + 0x1p+0f;
        #line 1330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__yS3984, _M0L3valS3985, _M0L6_2atmpS3986);
      }
      _M0L3valS3991 = _M0L1iS1215->$0;
      _M0L6_2atmpS3990 = _M0L3valS3991 + 1;
      _M0L1iS1215->$0 = _M0L6_2atmpS3990;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1215);
    }
    break;
  }
  _M0L4a__xS4038 = _M0L5paramS1209->$0;
  _M0L6tau__xS4040 = _M0L5paramS1209->$2;
  _M0L6_2atmpS4039 = 0x1p+1f * _M0L6tau__xS4040;
  _M0L7coef__xS1217 = _M0L4a__xS4038 / _M0L6_2atmpS4039;
  _M0L4a__yS4035 = _M0L5paramS1209->$1;
  _M0L6tau__yS4037 = _M0L5paramS1209->$3;
  _M0L6_2atmpS4036 = 0x1p+1f * _M0L6tau__yS4037;
  _M0L7coef__yS1218 = _M0L4a__yS4035 / _M0L6_2atmpS4036;
  _M0L1jS1211->$0 = 0;
  while (1) {
    int32_t _M0L3valS3992 = _M0L1jS1211->$0;
    if (_M0L3valS3992 < _M0L6n__preS1204) {
      int32_t _M0L3valS4034 = _M0L1jS1211->$0;
      int32_t _M0L5startS1219;
      int32_t _M0L3valS4033;
      int32_t _M0L6_2atmpS4032;
      int32_t _M0L3endS1221;
      int32_t _M0L3valS4031;
      int32_t _M0L10pre__firedS1222;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS4029;
      int32_t _M0L3valS4030;
      float _M0L8tr__x__jS1223;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS4027;
      int32_t _M0L3valS4028;
      float _M0L8tr__y__jS1224;
      struct _M0TPB8MutLocalGiE* _M0L1sS1225;
      int32_t _M0L3valS4026;
      int32_t _M0L6_2atmpS4025;
      #line 1346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1219
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1220, _M0L3valS4034);
      _M0L3valS4033 = _M0L1jS1211->$0;
      _M0L6_2atmpS4032 = _M0L3valS4033 + 1;
      #line 1347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1221
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1220, _M0L6_2atmpS4032);
      _M0L3valS4031 = _M0L1jS1211->$0;
      #line 1348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L10pre__firedS1222
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1205, _M0L3valS4031);
      _M0L5tr__xS4029 = _M0L4varsS1212->$0;
      _M0L3valS4030 = _M0L1jS1211->$0;
      #line 1349 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8tr__x__jS1223
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS4029, _M0L3valS4030);
      _M0L5tr__yS4027 = _M0L4varsS1212->$1;
      _M0L3valS4028 = _M0L1jS1211->$0;
      #line 1350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8tr__y__jS1224
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS4027, _M0L3valS4028);
      _M0L1sS1225
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1225)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1225->$0 = _M0L5startS1219;
      while (1) {
        int32_t _M0L3valS3993 = _M0L1sS1225->$0;
        if (_M0L3valS3993 < _M0L3endS1221) {
          int32_t _M0L3valS4024 = _M0L1sS1225->$0;
          int32_t _M0L9post__idxS1226;
          int32_t _M0L11post__firedS1228;
          struct _M0TPB5ArrayGfE* _M0L5to__xS4023;
          float _M0L8to__x__iS1229;
          struct _M0TPB5ArrayGfE* _M0L5to__yS4022;
          float _M0L8to__y__iS1230;
          int32_t _M0L3valS4012;
          float _M0L6_2atmpS4010;
          float _M0L6w__minS4011;
          int32_t _M0L3valS4017;
          float _M0L6_2atmpS4015;
          float _M0L6w__maxS4016;
          int32_t _M0L3valS4021;
          int32_t _M0L6_2atmpS4020;
          #line 1353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1226
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1227, _M0L3valS4024);
          #line 1354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1228
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1207, _M0L9post__idxS1226);
          _M0L5to__xS4023 = _M0L4varsS1212->$2;
          #line 1355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8to__x__iS1229
          = _M0MPC15array5Array2atGfE(_M0L5to__xS4023, _M0L9post__idxS1226);
          _M0L5to__yS4022 = _M0L4varsS1212->$3;
          #line 1356 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8to__y__iS1230
          = _M0MPC15array5Array2atGfE(_M0L5to__yS4022, _M0L9post__idxS1226);
          if (_M0L10pre__firedS1222) {
            float _M0L10alpha__preS4000 = _M0L5paramS1209->$4;
            float _M0L6_2atmpS4001 = _M0L7coef__xS1217 * _M0L8to__x__iS1229;
            float _M0L6_2atmpS3998 = _M0L10alpha__preS4000 + _M0L6_2atmpS4001;
            float _M0L6_2atmpS3999 = _M0L7coef__yS1218 * _M0L8to__y__iS1230;
            float _M0L2dwS1231 = _M0L6_2atmpS3998 - _M0L6_2atmpS3999;
            int32_t _M0L3valS3994 = _M0L1sS1225->$0;
            int32_t _M0L3valS3997 = _M0L1sS1225->$0;
            float _M0L6_2atmpS3996;
            float _M0L6_2atmpS3995;
            #line 1359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3996
            = _M0MPC15array5Array2atGfE(_M0L1wS1232, _M0L3valS3997);
            _M0L6_2atmpS3995 = _M0L6_2atmpS3996 + _M0L2dwS1231;
            #line 1359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1232, _M0L3valS3994, _M0L6_2atmpS3995);
          }
          if (_M0L11post__firedS1228) {
            float _M0L11alpha__postS4008 = _M0L5paramS1209->$5;
            float _M0L6_2atmpS4009 = _M0L7coef__xS1217 * _M0L8tr__x__jS1223;
            float _M0L6_2atmpS4006 =
              _M0L11alpha__postS4008 + _M0L6_2atmpS4009;
            float _M0L6_2atmpS4007 = _M0L7coef__yS1218 * _M0L8tr__y__jS1224;
            float _M0L2dwS1233 = _M0L6_2atmpS4006 - _M0L6_2atmpS4007;
            int32_t _M0L3valS4002 = _M0L1sS1225->$0;
            int32_t _M0L3valS4005 = _M0L1sS1225->$0;
            float _M0L6_2atmpS4004;
            float _M0L6_2atmpS4003;
            #line 1363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS4004
            = _M0MPC15array5Array2atGfE(_M0L1wS1232, _M0L3valS4005);
            _M0L6_2atmpS4003 = _M0L6_2atmpS4004 + _M0L2dwS1233;
            #line 1363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1232, _M0L3valS4002, _M0L6_2atmpS4003);
          }
          _M0L3valS4012 = _M0L1sS1225->$0;
          #line 1365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS4010
          = _M0MPC15array5Array2atGfE(_M0L1wS1232, _M0L3valS4012);
          _M0L6w__minS4011 = _M0L5paramS1209->$7;
          if (_M0L6_2atmpS4010 < _M0L6w__minS4011) {
            int32_t _M0L3valS4013 = _M0L1sS1225->$0;
            float _M0L6w__minS4014 = _M0L5paramS1209->$7;
            #line 1365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1232, _M0L3valS4013, _M0L6w__minS4014);
          }
          _M0L3valS4017 = _M0L1sS1225->$0;
          #line 1366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS4015
          = _M0MPC15array5Array2atGfE(_M0L1wS1232, _M0L3valS4017);
          _M0L6w__maxS4016 = _M0L5paramS1209->$6;
          if (_M0L6_2atmpS4015 > _M0L6w__maxS4016) {
            int32_t _M0L3valS4018 = _M0L1sS1225->$0;
            float _M0L6w__maxS4019 = _M0L5paramS1209->$6;
            #line 1366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1232, _M0L3valS4018, _M0L6w__maxS4019);
          }
          _M0L3valS4021 = _M0L1sS1225->$0;
          _M0L6_2atmpS4020 = _M0L3valS4021 + 1;
          _M0L1sS1225->$0 = _M0L6_2atmpS4020;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1225);
        }
        break;
      }
      _M0L3valS4026 = _M0L1jS1211->$0;
      _M0L6_2atmpS4025 = _M0L3valS4026 + 1;
      _M0L1jS1211->$0 = _M0L6_2atmpS4025;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1211);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22stdp__confavreux__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1200,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1177,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1179,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1197,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1191,
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS1185,
  struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L5paramS1182,
  float _M0L6t__nowS1186,
  float _M0L2dtS1181
) {
  int32_t _M0L6n__preS1176;
  int32_t _M0L7n__postS1178;
  float _M0L6_2atmpS3910;
  float _M0L8tau__preS3911;
  float _M0L6_2atmpS3909;
  float _M0L10decay__preS1180;
  float _M0L6_2atmpS3907;
  float _M0L9tau__postS3908;
  float _M0L6_2atmpS3906;
  float _M0L11decay__postS1183;
  struct _M0TPB8MutLocalGiE* _M0L1jS1184;
  struct _M0TPB8MutLocalGiE* _M0L1iS1188;
  #line 1080 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 1091 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1176 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1177);
  #line 1092 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1178 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1179);
  _M0L6_2atmpS3910 = -_M0L2dtS1181;
  _M0L8tau__preS3911 = _M0L5paramS1182->$5;
  _M0L6_2atmpS3909 = _M0L6_2atmpS3910 / _M0L8tau__preS3911;
  #line 1093 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10decay__preS1180 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3909);
  _M0L6_2atmpS3907 = -_M0L2dtS1181;
  _M0L9tau__postS3908 = _M0L5paramS1182->$6;
  _M0L6_2atmpS3906 = _M0L6_2atmpS3907 / _M0L9tau__postS3908;
  #line 1094 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L11decay__postS1183 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3906);
  _M0L1jS1184
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1184)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1184->$0 = 0;
  while (1) {
    int32_t _M0L3valS3826 = _M0L1jS1184->$0;
    if (_M0L3valS3826 < _M0L6n__preS1176) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS3827 = _M0L4varsS1185->$0;
      int32_t _M0L3valS3828 = _M0L1jS1184->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3831 = _M0L4varsS1185->$0;
      int32_t _M0L3valS3832 = _M0L1jS1184->$0;
      float _M0L6_2atmpS3830;
      float _M0L6_2atmpS3829;
      int32_t _M0L3valS3833;
      int32_t _M0L3valS3843;
      int32_t _M0L6_2atmpS3842;
      #line 1098 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3830
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3831, _M0L3valS3832);
      _M0L6_2atmpS3829 = _M0L6_2atmpS3830 * _M0L10decay__preS1180;
      #line 1098 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS3827, _M0L3valS3828, _M0L6_2atmpS3829);
      _M0L3valS3833 = _M0L1jS1184->$0;
      #line 1099 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1177, _M0L3valS3833)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS3834 = _M0L4varsS1185->$0;
        int32_t _M0L3valS3835 = _M0L1jS1184->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS3838 = _M0L4varsS1185->$0;
        int32_t _M0L3valS3839 = _M0L1jS1184->$0;
        float _M0L6_2atmpS3837;
        float _M0L6_2atmpS3836;
        struct _M0TPB5ArrayGfE* _M0L9last__preS3840;
        int32_t _M0L3valS3841;
        #line 1100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3837
        = _M0MPC15array5Array2atGfE(_M0L4tpreS3838, _M0L3valS3839);
        _M0L6_2atmpS3836 = _M0L6_2atmpS3837 + 0x1p+0f;
        #line 1100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS3834, _M0L3valS3835, _M0L6_2atmpS3836);
        _M0L9last__preS3840 = _M0L4varsS1185->$2;
        _M0L3valS3841 = _M0L1jS1184->$0;
        #line 1101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L9last__preS3840, _M0L3valS3841, _M0L6t__nowS1186);
      }
      _M0L3valS3843 = _M0L1jS1184->$0;
      _M0L6_2atmpS3842 = _M0L3valS3843 + 1;
      _M0L1jS1184->$0 = _M0L6_2atmpS3842;
      continue;
    }
    break;
  }
  _M0L1iS1188
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1188)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1188->$0 = 0;
  while (1) {
    int32_t _M0L3valS3844 = _M0L1iS1188->$0;
    if (_M0L3valS3844 < _M0L7n__postS1178) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS3845 = _M0L4varsS1185->$1;
      int32_t _M0L3valS3846 = _M0L1iS1188->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3849 = _M0L4varsS1185->$1;
      int32_t _M0L3valS3850 = _M0L1iS1188->$0;
      float _M0L6_2atmpS3848;
      float _M0L6_2atmpS3847;
      int32_t _M0L3valS3851;
      int32_t _M0L3valS3861;
      int32_t _M0L6_2atmpS3860;
      #line 1107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3848
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3849, _M0L3valS3850);
      _M0L6_2atmpS3847 = _M0L6_2atmpS3848 * _M0L11decay__postS1183;
      #line 1107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS3845, _M0L3valS3846, _M0L6_2atmpS3847);
      _M0L3valS3851 = _M0L1iS1188->$0;
      #line 1108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1179, _M0L3valS3851)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS3852 = _M0L4varsS1185->$1;
        int32_t _M0L3valS3853 = _M0L1iS1188->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS3856 = _M0L4varsS1185->$1;
        int32_t _M0L3valS3857 = _M0L1iS1188->$0;
        float _M0L6_2atmpS3855;
        float _M0L6_2atmpS3854;
        struct _M0TPB5ArrayGfE* _M0L10last__postS3858;
        int32_t _M0L3valS3859;
        #line 1109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3855
        = _M0MPC15array5Array2atGfE(_M0L5tpostS3856, _M0L3valS3857);
        _M0L6_2atmpS3854 = _M0L6_2atmpS3855 + 0x1p+0f;
        #line 1109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS3852, _M0L3valS3853, _M0L6_2atmpS3854);
        _M0L10last__postS3858 = _M0L4varsS1185->$3;
        _M0L3valS3859 = _M0L1iS1188->$0;
        #line 1110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L10last__postS3858, _M0L3valS3859, _M0L6t__nowS1186);
      }
      _M0L3valS3861 = _M0L1iS1188->$0;
      _M0L6_2atmpS3860 = _M0L3valS3861 + 1;
      _M0L1iS1188->$0 = _M0L6_2atmpS3860;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1188);
    }
    break;
  }
  _M0L1jS1184->$0 = 0;
  while (1) {
    int32_t _M0L3valS3862 = _M0L1jS1184->$0;
    if (_M0L3valS3862 < _M0L6n__preS1176) {
      int32_t _M0L3valS3905 = _M0L1jS1184->$0;
      int32_t _M0L5startS1190;
      int32_t _M0L3valS3904;
      int32_t _M0L6_2atmpS3903;
      int32_t _M0L3endS1192;
      int32_t _M0L3valS3902;
      int32_t _M0L10pre__firedS1193;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3900;
      int32_t _M0L3valS3901;
      float _M0L7tpre__jS1194;
      struct _M0TPB8MutLocalGiE* _M0L1sS1195;
      int32_t _M0L3valS3899;
      int32_t _M0L6_2atmpS3898;
      #line 1120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1190
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1191, _M0L3valS3905);
      _M0L3valS3904 = _M0L1jS1184->$0;
      _M0L6_2atmpS3903 = _M0L3valS3904 + 1;
      #line 1121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1192
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1191, _M0L6_2atmpS3903);
      _M0L3valS3902 = _M0L1jS1184->$0;
      #line 1122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L10pre__firedS1193
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1177, _M0L3valS3902);
      _M0L4tpreS3900 = _M0L4varsS1185->$0;
      _M0L3valS3901 = _M0L1jS1184->$0;
      #line 1123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L7tpre__jS1194
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3900, _M0L3valS3901);
      _M0L1sS1195
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1195)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1195->$0 = _M0L5startS1190;
      while (1) {
        int32_t _M0L3valS3863 = _M0L1sS1195->$0;
        if (_M0L3valS3863 < _M0L3endS1192) {
          int32_t _M0L3valS3897 = _M0L1sS1195->$0;
          int32_t _M0L9post__idxS1196;
          int32_t _M0L11post__firedS1198;
          struct _M0TPB5ArrayGfE* _M0L5tpostS3896;
          float _M0L8tpost__iS1199;
          int32_t _M0L3valS3886;
          float _M0L6_2atmpS3884;
          float _M0L6w__minS3885;
          int32_t _M0L3valS3891;
          float _M0L6_2atmpS3889;
          float _M0L6w__maxS3890;
          int32_t _M0L3valS3895;
          int32_t _M0L6_2atmpS3894;
          #line 1126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1196
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1197, _M0L3valS3897);
          #line 1127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1198
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1179, _M0L9post__idxS1196);
          _M0L5tpostS3896 = _M0L4varsS1185->$1;
          #line 1128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8tpost__iS1199
          = _M0MPC15array5Array2atGfE(_M0L5tpostS3896, _M0L9post__idxS1196);
          if (_M0L10pre__firedS1193) {
            int32_t _M0L3valS3864 = _M0L1sS1195->$0;
            int32_t _M0L3valS3873 = _M0L1sS1195->$0;
            float _M0L6_2atmpS3866;
            float _M0L3etaS3868;
            float _M0L5kappaS3872;
            float _M0L6_2atmpS3870;
            float _M0L5alphaS3871;
            float _M0L6_2atmpS3869;
            float _M0L6_2atmpS3867;
            float _M0L6_2atmpS3865;
            #line 1131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3866
            = _M0MPC15array5Array2atGfE(_M0L1wS1200, _M0L3valS3873);
            _M0L3etaS3868 = _M0L5paramS1182->$0;
            _M0L5kappaS3872 = _M0L5paramS1182->$3;
            _M0L6_2atmpS3870 = _M0L5kappaS3872 * _M0L8tpost__iS1199;
            _M0L5alphaS3871 = _M0L5paramS1182->$1;
            _M0L6_2atmpS3869 = _M0L6_2atmpS3870 + _M0L5alphaS3871;
            _M0L6_2atmpS3867 = _M0L3etaS3868 * _M0L6_2atmpS3869;
            _M0L6_2atmpS3865 = _M0L6_2atmpS3866 + _M0L6_2atmpS3867;
            #line 1131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1200, _M0L3valS3864, _M0L6_2atmpS3865);
          }
          if (_M0L11post__firedS1198) {
            int32_t _M0L3valS3874 = _M0L1sS1195->$0;
            int32_t _M0L3valS3883 = _M0L1sS1195->$0;
            float _M0L6_2atmpS3876;
            float _M0L3etaS3878;
            float _M0L5gammaS3882;
            float _M0L6_2atmpS3880;
            float _M0L4betaS3881;
            float _M0L6_2atmpS3879;
            float _M0L6_2atmpS3877;
            float _M0L6_2atmpS3875;
            #line 1135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3876
            = _M0MPC15array5Array2atGfE(_M0L1wS1200, _M0L3valS3883);
            _M0L3etaS3878 = _M0L5paramS1182->$0;
            _M0L5gammaS3882 = _M0L5paramS1182->$4;
            _M0L6_2atmpS3880 = _M0L5gammaS3882 * _M0L7tpre__jS1194;
            _M0L4betaS3881 = _M0L5paramS1182->$2;
            _M0L6_2atmpS3879 = _M0L6_2atmpS3880 + _M0L4betaS3881;
            _M0L6_2atmpS3877 = _M0L3etaS3878 * _M0L6_2atmpS3879;
            _M0L6_2atmpS3875 = _M0L6_2atmpS3876 + _M0L6_2atmpS3877;
            #line 1135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1200, _M0L3valS3874, _M0L6_2atmpS3875);
          }
          _M0L3valS3886 = _M0L1sS1195->$0;
          #line 1138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3884
          = _M0MPC15array5Array2atGfE(_M0L1wS1200, _M0L3valS3886);
          _M0L6w__minS3885 = _M0L5paramS1182->$8;
          if (_M0L6_2atmpS3884 < _M0L6w__minS3885) {
            int32_t _M0L3valS3887 = _M0L1sS1195->$0;
            float _M0L6w__minS3888 = _M0L5paramS1182->$8;
            #line 1138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1200, _M0L3valS3887, _M0L6w__minS3888);
          }
          _M0L3valS3891 = _M0L1sS1195->$0;
          #line 1139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3889
          = _M0MPC15array5Array2atGfE(_M0L1wS1200, _M0L3valS3891);
          _M0L6w__maxS3890 = _M0L5paramS1182->$7;
          if (_M0L6_2atmpS3889 > _M0L6w__maxS3890) {
            int32_t _M0L3valS3892 = _M0L1sS1195->$0;
            float _M0L6w__maxS3893 = _M0L5paramS1182->$7;
            #line 1139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1200, _M0L3valS3892, _M0L6w__maxS3893);
          }
          _M0L3valS3895 = _M0L1sS1195->$0;
          _M0L6_2atmpS3894 = _M0L3valS3895 + 1;
          _M0L1sS1195->$0 = _M0L6_2atmpS3894;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1195);
        }
        break;
      }
      _M0L3valS3899 = _M0L1jS1184->$0;
      _M0L6_2atmpS3898 = _M0L3valS3899 + 1;
      _M0L1jS1184->$0 = _M0L6_2atmpS3898;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1184);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt10stdp__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1173,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1153,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1155,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1170,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1166,
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS1151,
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS1158,
  float _M0L6t__nowS1161,
  float _M0L2dtS1157
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3743;
  int32_t _M0L6_2atmpS3742;
  int32_t _if__result_6030;
  int32_t _M0L6n__preS1152;
  int32_t _M0L7n__postS1154;
  float _M0L6_2atmpS3824;
  float _M0L8tau__preS3825;
  float _M0L6_2atmpS3823;
  float _M0L10decay__preS1156;
  float _M0L6_2atmpS3821;
  float _M0L9tau__postS3822;
  float _M0L6_2atmpS3820;
  float _M0L11decay__postS1159;
  struct _M0TPB8MutLocalGiE* _M0L1jS1160;
  struct _M0TPB8MutLocalGiE* _M0L1iS1163;
  #line 905 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6activeS3743 = _M0L4varsS1151->$4;
  #line 917 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3742 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3743);
  if (_M0L6_2atmpS3742 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3741 = _M0L4varsS1151->$4;
    int32_t _M0L6_2atmpS3740;
    #line 917 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS3740 = _M0MPC15array5Array2atGbE(_M0L6activeS3741, 0);
    _if__result_6030 = !_M0L6_2atmpS3740;
  } else {
    _if__result_6030 = 0;
  }
  if (_if__result_6030) {
    return 0;
  }
  #line 921 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1152 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1153);
  #line 922 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1154 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1155);
  _M0L6_2atmpS3824 = -_M0L2dtS1157;
  _M0L8tau__preS3825 = _M0L5paramS1158->$2;
  _M0L6_2atmpS3823 = _M0L6_2atmpS3824 / _M0L8tau__preS3825;
  #line 923 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10decay__preS1156 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3823);
  _M0L6_2atmpS3821 = -_M0L2dtS1157;
  _M0L9tau__postS3822 = _M0L5paramS1158->$3;
  _M0L6_2atmpS3820 = _M0L6_2atmpS3821 / _M0L9tau__postS3822;
  #line 924 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L11decay__postS1159 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3820);
  _M0L1jS1160
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1160)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1160->$0 = 0;
  while (1) {
    int32_t _M0L3valS3744 = _M0L1jS1160->$0;
    if (_M0L3valS3744 < _M0L6n__preS1152) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS3745 = _M0L4varsS1151->$0;
      int32_t _M0L3valS3746 = _M0L1jS1160->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3749 = _M0L4varsS1151->$0;
      int32_t _M0L3valS3750 = _M0L1jS1160->$0;
      float _M0L6_2atmpS3748;
      float _M0L6_2atmpS3747;
      int32_t _M0L3valS3751;
      int32_t _M0L3valS3762;
      int32_t _M0L6_2atmpS3761;
      #line 927 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3748
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3749, _M0L3valS3750);
      _M0L6_2atmpS3747 = _M0L6_2atmpS3748 * _M0L10decay__preS1156;
      #line 927 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS3745, _M0L3valS3746, _M0L6_2atmpS3747);
      _M0L3valS3751 = _M0L1jS1160->$0;
      #line 928 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1153, _M0L3valS3751)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS3752 = _M0L4varsS1151->$0;
        int32_t _M0L3valS3753 = _M0L1jS1160->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS3757 = _M0L4varsS1151->$0;
        int32_t _M0L3valS3758 = _M0L1jS1160->$0;
        float _M0L6_2atmpS3755;
        float _M0L6a__preS3756;
        float _M0L6_2atmpS3754;
        struct _M0TPB5ArrayGfE* _M0L9last__preS3759;
        int32_t _M0L3valS3760;
        #line 929 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3755
        = _M0MPC15array5Array2atGfE(_M0L4tpreS3757, _M0L3valS3758);
        _M0L6a__preS3756 = _M0L5paramS1158->$0;
        _M0L6_2atmpS3754 = _M0L6_2atmpS3755 + _M0L6a__preS3756;
        #line 929 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS3752, _M0L3valS3753, _M0L6_2atmpS3754);
        _M0L9last__preS3759 = _M0L4varsS1151->$2;
        _M0L3valS3760 = _M0L1jS1160->$0;
        #line 930 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L9last__preS3759, _M0L3valS3760, _M0L6t__nowS1161);
      }
      _M0L3valS3762 = _M0L1jS1160->$0;
      _M0L6_2atmpS3761 = _M0L3valS3762 + 1;
      _M0L1jS1160->$0 = _M0L6_2atmpS3761;
      continue;
    }
    break;
  }
  _M0L1iS1163
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1163)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1163->$0 = 0;
  while (1) {
    int32_t _M0L3valS3763 = _M0L1iS1163->$0;
    if (_M0L3valS3763 < _M0L7n__postS1154) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS3764 = _M0L4varsS1151->$1;
      int32_t _M0L3valS3765 = _M0L1iS1163->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3768 = _M0L4varsS1151->$1;
      int32_t _M0L3valS3769 = _M0L1iS1163->$0;
      float _M0L6_2atmpS3767;
      float _M0L6_2atmpS3766;
      int32_t _M0L3valS3770;
      int32_t _M0L3valS3781;
      int32_t _M0L6_2atmpS3780;
      #line 936 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3767
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3768, _M0L3valS3769);
      _M0L6_2atmpS3766 = _M0L6_2atmpS3767 * _M0L11decay__postS1159;
      #line 936 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS3764, _M0L3valS3765, _M0L6_2atmpS3766);
      _M0L3valS3770 = _M0L1iS1163->$0;
      #line 937 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1155, _M0L3valS3770)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS3771 = _M0L4varsS1151->$1;
        int32_t _M0L3valS3772 = _M0L1iS1163->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS3776 = _M0L4varsS1151->$1;
        int32_t _M0L3valS3777 = _M0L1iS1163->$0;
        float _M0L6_2atmpS3774;
        float _M0L7a__postS3775;
        float _M0L6_2atmpS3773;
        struct _M0TPB5ArrayGfE* _M0L10last__postS3778;
        int32_t _M0L3valS3779;
        #line 938 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3774
        = _M0MPC15array5Array2atGfE(_M0L5tpostS3776, _M0L3valS3777);
        _M0L7a__postS3775 = _M0L5paramS1158->$1;
        _M0L6_2atmpS3773 = _M0L6_2atmpS3774 + _M0L7a__postS3775;
        #line 938 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS3771, _M0L3valS3772, _M0L6_2atmpS3773);
        _M0L10last__postS3778 = _M0L4varsS1151->$3;
        _M0L3valS3779 = _M0L1iS1163->$0;
        #line 939 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L10last__postS3778, _M0L3valS3779, _M0L6t__nowS1161);
      }
      _M0L3valS3781 = _M0L1iS1163->$0;
      _M0L6_2atmpS3780 = _M0L3valS3781 + 1;
      _M0L1iS1163->$0 = _M0L6_2atmpS3780;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1163);
    }
    break;
  }
  _M0L1jS1160->$0 = 0;
  while (1) {
    int32_t _M0L3valS3782 = _M0L1jS1160->$0;
    if (_M0L3valS3782 < _M0L6n__preS1152) {
      int32_t _M0L3valS3819 = _M0L1jS1160->$0;
      int32_t _M0L5startS1165;
      int32_t _M0L3valS3818;
      int32_t _M0L6_2atmpS3817;
      int32_t _M0L3endS1167;
      struct _M0TPB8MutLocalGiE* _M0L1sS1168;
      int32_t _M0L3valS3816;
      int32_t _M0L6_2atmpS3815;
      #line 947 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1165
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1166, _M0L3valS3819);
      _M0L3valS3818 = _M0L1jS1160->$0;
      _M0L6_2atmpS3817 = _M0L3valS3818 + 1;
      #line 948 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1167
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1166, _M0L6_2atmpS3817);
      _M0L1sS1168
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1168)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1168->$0 = _M0L5startS1165;
      while (1) {
        int32_t _M0L3valS3783 = _M0L1sS1168->$0;
        if (_M0L3valS3783 < _M0L3endS1167) {
          int32_t _M0L3valS3814 = _M0L1sS1168->$0;
          int32_t _M0L9post__idxS1169;
          int32_t _M0L3valS3813;
          int32_t _M0L10pre__firedS1171;
          int32_t _M0L11post__firedS1172;
          int32_t _M0L3valS3803;
          float _M0L6_2atmpS3801;
          float _M0L6w__minS3802;
          int32_t _M0L3valS3808;
          float _M0L6_2atmpS3806;
          float _M0L6w__maxS3807;
          int32_t _M0L3valS3812;
          int32_t _M0L6_2atmpS3811;
          #line 951 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1169
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1170, _M0L3valS3814);
          _M0L3valS3813 = _M0L1jS1160->$0;
          #line 952 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L10pre__firedS1171
          = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1153, _M0L3valS3813);
          #line 953 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1172
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1155, _M0L9post__idxS1169);
          if (_M0L10pre__firedS1171) {
            int32_t _M0L3valS3784 = _M0L1sS1168->$0;
            int32_t _M0L3valS3791 = _M0L1sS1168->$0;
            float _M0L6_2atmpS3786;
            float _M0L7a__postS3788;
            struct _M0TPB5ArrayGfE* _M0L5tpostS3790;
            float _M0L6_2atmpS3789;
            float _M0L6_2atmpS3787;
            float _M0L6_2atmpS3785;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3786
            = _M0MPC15array5Array2atGfE(_M0L1wS1173, _M0L3valS3791);
            _M0L7a__postS3788 = _M0L5paramS1158->$1;
            _M0L5tpostS3790 = _M0L4varsS1151->$1;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3789
            = _M0MPC15array5Array2atGfE(_M0L5tpostS3790, _M0L9post__idxS1169);
            _M0L6_2atmpS3787 = _M0L7a__postS3788 * _M0L6_2atmpS3789;
            _M0L6_2atmpS3785 = _M0L6_2atmpS3786 + _M0L6_2atmpS3787;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1173, _M0L3valS3784, _M0L6_2atmpS3785);
          }
          if (_M0L11post__firedS1172) {
            int32_t _M0L3valS3792 = _M0L1sS1168->$0;
            int32_t _M0L3valS3800 = _M0L1sS1168->$0;
            float _M0L6_2atmpS3794;
            float _M0L6a__preS3796;
            struct _M0TPB5ArrayGfE* _M0L4tpreS3798;
            int32_t _M0L3valS3799;
            float _M0L6_2atmpS3797;
            float _M0L6_2atmpS3795;
            float _M0L6_2atmpS3793;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3794
            = _M0MPC15array5Array2atGfE(_M0L1wS1173, _M0L3valS3800);
            _M0L6a__preS3796 = _M0L5paramS1158->$0;
            _M0L4tpreS3798 = _M0L4varsS1151->$0;
            _M0L3valS3799 = _M0L1jS1160->$0;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3797
            = _M0MPC15array5Array2atGfE(_M0L4tpreS3798, _M0L3valS3799);
            _M0L6_2atmpS3795 = _M0L6a__preS3796 * _M0L6_2atmpS3797;
            _M0L6_2atmpS3793 = _M0L6_2atmpS3794 + _M0L6_2atmpS3795;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1173, _M0L3valS3792, _M0L6_2atmpS3793);
          }
          _M0L3valS3803 = _M0L1sS1168->$0;
          #line 963 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3801
          = _M0MPC15array5Array2atGfE(_M0L1wS1173, _M0L3valS3803);
          _M0L6w__minS3802 = _M0L5paramS1158->$5;
          if (_M0L6_2atmpS3801 < _M0L6w__minS3802) {
            int32_t _M0L3valS3804 = _M0L1sS1168->$0;
            float _M0L6w__minS3805 = _M0L5paramS1158->$5;
            #line 963 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1173, _M0L3valS3804, _M0L6w__minS3805);
          }
          _M0L3valS3808 = _M0L1sS1168->$0;
          #line 964 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3806
          = _M0MPC15array5Array2atGfE(_M0L1wS1173, _M0L3valS3808);
          _M0L6w__maxS3807 = _M0L5paramS1158->$4;
          if (_M0L6_2atmpS3806 > _M0L6w__maxS3807) {
            int32_t _M0L3valS3809 = _M0L1sS1168->$0;
            float _M0L6w__maxS3810 = _M0L5paramS1158->$4;
            #line 964 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1173, _M0L3valS3809, _M0L6w__maxS3810);
          }
          _M0L3valS3812 = _M0L1sS1168->$0;
          _M0L6_2atmpS3811 = _M0L3valS3812 + 1;
          _M0L1sS1168->$0 = _M0L6_2atmpS3811;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1168);
        }
        break;
      }
      _M0L3valS3816 = _M0L1jS1160->$0;
      _M0L6_2atmpS3815 = _M0L3valS3816 + 1;
      _M0L1jS1160->$0 = _M0L6_2atmpS3815;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1160);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25stdp__antisymmetric__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1130,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1120,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1122,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1129,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1125,
  struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L4varsS1132,
  struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L5paramS1131,
  float _M0L2dtS1144
) {
  int32_t _M0L6n__preS1119;
  int32_t _M0L7n__postS1121;
  struct _M0TPB8MutLocalGiE* _M0L1jS1123;
  int32_t _M0L3nnzS1135;
  float _M0L4a__xS3738;
  float _M0L6tau__xS3739;
  float _M0L18a__x__over__tau__xS1136;
  struct _M0TPB8MutLocalGiE* _M0L2s2S1137;
  float _M0L6tau__xS3737;
  float _M0L11inv__tau__xS1141;
  float _M0L6tau__yS3736;
  float _M0L11inv__tau__yS1142;
  struct _M0TPB8MutLocalGiE* _M0L1iS1143;
  struct _M0TPB8MutLocalGiE* _M0L2s3S1149;
  #line 622 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 632 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1119 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1120);
  #line 633 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1121 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1122);
  _M0L1jS1123
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1123)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1123->$0 = 0;
  while (1) {
    int32_t _M0L3valS3636 = _M0L1jS1123->$0;
    if (_M0L3valS3636 < _M0L6n__preS1119) {
      int32_t _M0L3valS3637 = _M0L1jS1123->$0;
      int32_t _M0L3valS3658;
      int32_t _M0L6_2atmpS3657;
      #line 637 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1120, _M0L3valS3637)) {
        int32_t _M0L3valS3656 = _M0L1jS1123->$0;
        int32_t _M0L5startS1124;
        int32_t _M0L3valS3655;
        int32_t _M0L6_2atmpS3654;
        int32_t _M0L3endS1126;
        struct _M0TPB8MutLocalGiE* _M0L1sS1127;
        #line 638 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS1124
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1125, _M0L3valS3656);
        _M0L3valS3655 = _M0L1jS1123->$0;
        _M0L6_2atmpS3654 = _M0L3valS3655 + 1;
        #line 639 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3endS1126
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1125, _M0L6_2atmpS3654);
        _M0L1sS1127
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS1127)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS1127->$0 = _M0L5startS1124;
        while (1) {
          int32_t _M0L3valS3638 = _M0L1sS1127->$0;
          if (_M0L3valS3638 < _M0L3endS1126) {
            int32_t _M0L3valS3653 = _M0L1sS1127->$0;
            int32_t _M0L9post__idxS1128;
            int32_t _M0L3valS3639;
            int32_t _M0L3valS3650;
            float _M0L6_2atmpS3648;
            float _M0L10alpha__preS3649;
            float _M0L6_2atmpS3641;
            float _M0L4a__yS3646;
            float _M0L6tau__yS3647;
            float _M0L6_2atmpS3643;
            struct _M0TPB5ArrayGfE* _M0L5to__yS3645;
            float _M0L6_2atmpS3644;
            float _M0L6_2atmpS3642;
            float _M0L6_2atmpS3640;
            int32_t _M0L3valS3652;
            int32_t _M0L6_2atmpS3651;
            #line 642 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L9post__idxS1128
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1129, _M0L3valS3653);
            _M0L3valS3639 = _M0L1sS1127->$0;
            _M0L3valS3650 = _M0L1sS1127->$0;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3648
            = _M0MPC15array5Array2atGfE(_M0L1wS1130, _M0L3valS3650);
            _M0L10alpha__preS3649 = _M0L5paramS1131->$4;
            _M0L6_2atmpS3641 = _M0L6_2atmpS3648 + _M0L10alpha__preS3649;
            _M0L4a__yS3646 = _M0L5paramS1131->$1;
            _M0L6tau__yS3647 = _M0L5paramS1131->$3;
            _M0L6_2atmpS3643 = _M0L4a__yS3646 / _M0L6tau__yS3647;
            _M0L5to__yS3645 = _M0L4varsS1132->$1;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3644
            = _M0MPC15array5Array2atGfE(_M0L5to__yS3645, _M0L9post__idxS1128);
            _M0L6_2atmpS3642 = _M0L6_2atmpS3643 * _M0L6_2atmpS3644;
            _M0L6_2atmpS3640 = _M0L6_2atmpS3641 - _M0L6_2atmpS3642;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1130, _M0L3valS3639, _M0L6_2atmpS3640);
            _M0L3valS3652 = _M0L1sS1127->$0;
            _M0L6_2atmpS3651 = _M0L3valS3652 + 1;
            _M0L1sS1127->$0 = _M0L6_2atmpS3651;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS1127);
          }
          break;
        }
      }
      _M0L3valS3658 = _M0L1jS1123->$0;
      _M0L6_2atmpS3657 = _M0L3valS3658 + 1;
      _M0L1jS1123->$0 = _M0L6_2atmpS3657;
      continue;
    }
    break;
  }
  #line 650 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L3nnzS1135 = _M0MPC15array5Array6lengthGfE(_M0L1wS1130);
  _M0L4a__xS3738 = _M0L5paramS1131->$0;
  _M0L6tau__xS3739 = _M0L5paramS1131->$2;
  _M0L18a__x__over__tau__xS1136 = _M0L4a__xS3738 / _M0L6tau__xS3739;
  _M0L2s2S1137
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S1137)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S1137->$0 = 0;
  while (1) {
    int32_t _M0L3valS3659 = _M0L2s2S1137->$0;
    if (_M0L3valS3659 < _M0L3nnzS1135) {
      int32_t _M0L3valS3672 = _M0L2s2S1137->$0;
      int32_t _M0L9post__idxS1138;
      int32_t _M0L3valS3671;
      int32_t _M0L6_2atmpS3670;
      #line 654 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L9post__idxS1138
      = _M0MPC15array5Array2atGiE(_M0L6colptrS1129, _M0L3valS3672);
      #line 655 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (
        _M0MPC15array5Array2atGbE(_M0L10post__fireS1122, _M0L9post__idxS1138)
      ) {
        int32_t _M0L3valS3669 = _M0L2s2S1137->$0;
        int32_t _M0L6j__preS1139;
        int32_t _M0L3valS3660;
        int32_t _M0L3valS3668;
        float _M0L6_2atmpS3666;
        float _M0L11alpha__postS3667;
        float _M0L6_2atmpS3662;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3665;
        float _M0L6_2atmpS3664;
        float _M0L6_2atmpS3663;
        float _M0L6_2atmpS3661;
        #line 656 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6j__preS1139
        = _M0FP26RiantR8snn__mbt20find__pre__for__conn(_M0L6rowptrS1125, _M0L3valS3669);
        _M0L3valS3660 = _M0L2s2S1137->$0;
        _M0L3valS3668 = _M0L2s2S1137->$0;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3666
        = _M0MPC15array5Array2atGfE(_M0L1wS1130, _M0L3valS3668);
        _M0L11alpha__postS3667 = _M0L5paramS1131->$5;
        _M0L6_2atmpS3662 = _M0L6_2atmpS3666 + _M0L11alpha__postS3667;
        _M0L5tr__xS3665 = _M0L4varsS1132->$0;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3664
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3665, _M0L6j__preS1139);
        _M0L6_2atmpS3663 = _M0L18a__x__over__tau__xS1136 * _M0L6_2atmpS3664;
        _M0L6_2atmpS3661 = _M0L6_2atmpS3662 + _M0L6_2atmpS3663;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1130, _M0L3valS3660, _M0L6_2atmpS3661);
      }
      _M0L3valS3671 = _M0L2s2S1137->$0;
      _M0L6_2atmpS3670 = _M0L3valS3671 + 1;
      _M0L2s2S1137->$0 = _M0L6_2atmpS3670;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S1137);
    }
    break;
  }
  _M0L6tau__xS3737 = _M0L5paramS1131->$2;
  _M0L11inv__tau__xS1141 = 0x1p+0f / _M0L6tau__xS3737;
  _M0L6tau__yS3736 = _M0L5paramS1131->$3;
  _M0L11inv__tau__yS1142 = 0x1p+0f / _M0L6tau__yS3736;
  _M0L1iS1143
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1143)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1143->$0 = 0;
  while (1) {
    int32_t _M0L3valS3673 = _M0L1iS1143->$0;
    if (_M0L3valS3673 < _M0L7n__postS1121) {
      struct _M0TPB5ArrayGfE* _M0L5to__yS3674 = _M0L4varsS1132->$1;
      int32_t _M0L3valS3675 = _M0L1iS1143->$0;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3684 = _M0L4varsS1132->$1;
      int32_t _M0L3valS3685 = _M0L1iS1143->$0;
      float _M0L6_2atmpS3677;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3682;
      int32_t _M0L3valS3683;
      float _M0L6_2atmpS3681;
      float _M0L6_2atmpS3680;
      float _M0L6_2atmpS3679;
      float _M0L6_2atmpS3678;
      float _M0L6_2atmpS3676;
      int32_t _M0L3valS3687;
      int32_t _M0L6_2atmpS3686;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3677
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3684, _M0L3valS3685);
      _M0L5to__yS3682 = _M0L4varsS1132->$1;
      _M0L3valS3683 = _M0L1iS1143->$0;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3681
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3682, _M0L3valS3683);
      _M0L6_2atmpS3680 = -_M0L6_2atmpS3681;
      _M0L6_2atmpS3679 = _M0L2dtS1144 * _M0L6_2atmpS3680;
      _M0L6_2atmpS3678 = _M0L6_2atmpS3679 * _M0L11inv__tau__yS1142;
      _M0L6_2atmpS3676 = _M0L6_2atmpS3677 + _M0L6_2atmpS3678;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__yS3674, _M0L3valS3675, _M0L6_2atmpS3676);
      _M0L3valS3687 = _M0L1iS1143->$0;
      _M0L6_2atmpS3686 = _M0L3valS3687 + 1;
      _M0L1iS1143->$0 = _M0L6_2atmpS3686;
      continue;
    }
    break;
  }
  _M0L1jS1123->$0 = 0;
  while (1) {
    int32_t _M0L3valS3688 = _M0L1jS1123->$0;
    if (_M0L3valS3688 < _M0L6n__preS1119) {
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3689 = _M0L4varsS1132->$0;
      int32_t _M0L3valS3690 = _M0L1jS1123->$0;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3699 = _M0L4varsS1132->$0;
      int32_t _M0L3valS3700 = _M0L1jS1123->$0;
      float _M0L6_2atmpS3692;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3697;
      int32_t _M0L3valS3698;
      float _M0L6_2atmpS3696;
      float _M0L6_2atmpS3695;
      float _M0L6_2atmpS3694;
      float _M0L6_2atmpS3693;
      float _M0L6_2atmpS3691;
      int32_t _M0L3valS3702;
      int32_t _M0L6_2atmpS3701;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3692
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3699, _M0L3valS3700);
      _M0L5tr__xS3697 = _M0L4varsS1132->$0;
      _M0L3valS3698 = _M0L1jS1123->$0;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3696
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3697, _M0L3valS3698);
      _M0L6_2atmpS3695 = -_M0L6_2atmpS3696;
      _M0L6_2atmpS3694 = _M0L2dtS1144 * _M0L6_2atmpS3695;
      _M0L6_2atmpS3693 = _M0L6_2atmpS3694 * _M0L11inv__tau__xS1141;
      _M0L6_2atmpS3691 = _M0L6_2atmpS3692 + _M0L6_2atmpS3693;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__xS3689, _M0L3valS3690, _M0L6_2atmpS3691);
      _M0L3valS3702 = _M0L1jS1123->$0;
      _M0L6_2atmpS3701 = _M0L3valS3702 + 1;
      _M0L1jS1123->$0 = _M0L6_2atmpS3701;
      continue;
    }
    break;
  }
  _M0L1iS1143->$0 = 0;
  while (1) {
    int32_t _M0L3valS3703 = _M0L1iS1143->$0;
    if (_M0L3valS3703 < _M0L7n__postS1121) {
      int32_t _M0L3valS3704 = _M0L1iS1143->$0;
      int32_t _M0L3valS3712;
      int32_t _M0L6_2atmpS3711;
      #line 677 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1122, _M0L3valS3704)) {
        struct _M0TPB5ArrayGfE* _M0L5to__yS3705 = _M0L4varsS1132->$1;
        int32_t _M0L3valS3706 = _M0L1iS1143->$0;
        struct _M0TPB5ArrayGfE* _M0L5to__yS3709 = _M0L4varsS1132->$1;
        int32_t _M0L3valS3710 = _M0L1iS1143->$0;
        float _M0L6_2atmpS3708;
        float _M0L6_2atmpS3707;
        #line 678 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3708
        = _M0MPC15array5Array2atGfE(_M0L5to__yS3709, _M0L3valS3710);
        _M0L6_2atmpS3707 = _M0L6_2atmpS3708 + 0x1p+0f;
        #line 678 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__yS3705, _M0L3valS3706, _M0L6_2atmpS3707);
      }
      _M0L3valS3712 = _M0L1iS1143->$0;
      _M0L6_2atmpS3711 = _M0L3valS3712 + 1;
      _M0L1iS1143->$0 = _M0L6_2atmpS3711;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1143);
    }
    break;
  }
  _M0L1jS1123->$0 = 0;
  while (1) {
    int32_t _M0L3valS3713 = _M0L1jS1123->$0;
    if (_M0L3valS3713 < _M0L6n__preS1119) {
      int32_t _M0L3valS3714 = _M0L1jS1123->$0;
      int32_t _M0L3valS3722;
      int32_t _M0L6_2atmpS3721;
      #line 684 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1120, _M0L3valS3714)) {
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3715 = _M0L4varsS1132->$0;
        int32_t _M0L3valS3716 = _M0L1jS1123->$0;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3719 = _M0L4varsS1132->$0;
        int32_t _M0L3valS3720 = _M0L1jS1123->$0;
        float _M0L6_2atmpS3718;
        float _M0L6_2atmpS3717;
        #line 685 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3718
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3719, _M0L3valS3720);
        _M0L6_2atmpS3717 = _M0L6_2atmpS3718 + 0x1p+0f;
        #line 685 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__xS3715, _M0L3valS3716, _M0L6_2atmpS3717);
      }
      _M0L3valS3722 = _M0L1jS1123->$0;
      _M0L6_2atmpS3721 = _M0L3valS3722 + 1;
      _M0L1jS1123->$0 = _M0L6_2atmpS3721;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1123);
    }
    break;
  }
  _M0L2s3S1149
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s3S1149)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s3S1149->$0 = 0;
  while (1) {
    int32_t _M0L3valS3723 = _M0L2s3S1149->$0;
    if (_M0L3valS3723 < _M0L3nnzS1135) {
      int32_t _M0L3valS3726 = _M0L2s3S1149->$0;
      float _M0L6_2atmpS3724;
      float _M0L6w__minS3725;
      int32_t _M0L3valS3735;
      int32_t _M0L6_2atmpS3734;
      #line 692 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3724
      = _M0MPC15array5Array2atGfE(_M0L1wS1130, _M0L3valS3726);
      _M0L6w__minS3725 = _M0L5paramS1131->$7;
      if (_M0L6_2atmpS3724 < _M0L6w__minS3725) {
        int32_t _M0L3valS3727 = _M0L2s3S1149->$0;
        float _M0L6w__minS3728 = _M0L5paramS1131->$7;
        #line 693 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1130, _M0L3valS3727, _M0L6w__minS3728);
      } else {
        int32_t _M0L3valS3731 = _M0L2s3S1149->$0;
        float _M0L6_2atmpS3729;
        float _M0L6w__maxS3730;
        #line 694 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3729
        = _M0MPC15array5Array2atGfE(_M0L1wS1130, _M0L3valS3731);
        _M0L6w__maxS3730 = _M0L5paramS1131->$6;
        if (_M0L6_2atmpS3729 > _M0L6w__maxS3730) {
          int32_t _M0L3valS3732 = _M0L2s3S1149->$0;
          float _M0L6w__maxS3733 = _M0L5paramS1131->$6;
          #line 695 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0MPC15array5Array3setGfE(_M0L1wS1130, _M0L3valS3732, _M0L6w__maxS3733);
        }
      }
      _M0L3valS3735 = _M0L2s3S1149->$0;
      _M0L6_2atmpS3734 = _M0L3valS3735 + 1;
      _M0L2s3S1149->$0 = _M0L6_2atmpS3734;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s3S1149);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt24stdp__mexican__hat__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1105,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1081,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1083,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1100,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1096,
  struct _M0TPB5ArrayGfE* _M0L4tpreS1091,
  struct _M0TPB5ArrayGfE* _M0L5tpostS1087,
  struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L5paramS1085,
  float _M0L2dtS1088
) {
  int32_t _M0L6n__preS1080;
  int32_t _M0L7n__postS1082;
  float _M0L3tauS3635;
  float _M0L8inv__tauS1084;
  struct _M0TPB8MutLocalGiE* _M0L1iS1086;
  struct _M0TPB8MutLocalGiE* _M0L1jS1090;
  int32_t _M0L3nnzS1108;
  struct _M0TPB8MutLocalGiE* _M0L2s2S1109;
  struct _M0TPB8MutLocalGiE* _M0L2s3S1117;
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 461 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1080 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1081);
  #line 462 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1082 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1083);
  _M0L3tauS3635 = _M0L5paramS1085->$1;
  _M0L8inv__tauS1084 = 0x1p+0f / _M0L3tauS3635;
  _M0L1iS1086
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1086)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1086->$0 = 0;
  while (1) {
    int32_t _M0L3valS3549 = _M0L1iS1086->$0;
    if (_M0L3valS3549 < _M0L7n__postS1082) {
      int32_t _M0L3valS3550 = _M0L1iS1086->$0;
      int32_t _M0L3valS3558 = _M0L1iS1086->$0;
      float _M0L6_2atmpS3552;
      int32_t _M0L3valS3557;
      float _M0L6_2atmpS3556;
      float _M0L6_2atmpS3555;
      float _M0L6_2atmpS3554;
      float _M0L6_2atmpS3553;
      float _M0L6_2atmpS3551;
      int32_t _M0L3valS3560;
      int32_t _M0L6_2atmpS3559;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3552
      = _M0MPC15array5Array2atGfE(_M0L5tpostS1087, _M0L3valS3558);
      _M0L3valS3557 = _M0L1iS1086->$0;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3556
      = _M0MPC15array5Array2atGfE(_M0L5tpostS1087, _M0L3valS3557);
      _M0L6_2atmpS3555 = -_M0L6_2atmpS3556;
      _M0L6_2atmpS3554 = _M0L2dtS1088 * _M0L6_2atmpS3555;
      _M0L6_2atmpS3553 = _M0L6_2atmpS3554 * _M0L8inv__tauS1084;
      _M0L6_2atmpS3551 = _M0L6_2atmpS3552 + _M0L6_2atmpS3553;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS1087, _M0L3valS3550, _M0L6_2atmpS3551);
      _M0L3valS3560 = _M0L1iS1086->$0;
      _M0L6_2atmpS3559 = _M0L3valS3560 + 1;
      _M0L1iS1086->$0 = _M0L6_2atmpS3559;
      continue;
    }
    break;
  }
  _M0L1jS1090
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1090)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1090->$0 = 0;
  while (1) {
    int32_t _M0L3valS3561 = _M0L1jS1090->$0;
    if (_M0L3valS3561 < _M0L6n__preS1080) {
      int32_t _M0L3valS3562 = _M0L1jS1090->$0;
      int32_t _M0L3valS3570 = _M0L1jS1090->$0;
      float _M0L6_2atmpS3564;
      int32_t _M0L3valS3569;
      float _M0L6_2atmpS3568;
      float _M0L6_2atmpS3567;
      float _M0L6_2atmpS3566;
      float _M0L6_2atmpS3565;
      float _M0L6_2atmpS3563;
      int32_t _M0L3valS3572;
      int32_t _M0L6_2atmpS3571;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3564
      = _M0MPC15array5Array2atGfE(_M0L4tpreS1091, _M0L3valS3570);
      _M0L3valS3569 = _M0L1jS1090->$0;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3568
      = _M0MPC15array5Array2atGfE(_M0L4tpreS1091, _M0L3valS3569);
      _M0L6_2atmpS3567 = -_M0L6_2atmpS3568;
      _M0L6_2atmpS3566 = _M0L2dtS1088 * _M0L6_2atmpS3567;
      _M0L6_2atmpS3565 = _M0L6_2atmpS3566 * _M0L8inv__tauS1084;
      _M0L6_2atmpS3563 = _M0L6_2atmpS3564 + _M0L6_2atmpS3565;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS1091, _M0L3valS3562, _M0L6_2atmpS3563);
      _M0L3valS3572 = _M0L1jS1090->$0;
      _M0L6_2atmpS3571 = _M0L3valS3572 + 1;
      _M0L1jS1090->$0 = _M0L6_2atmpS3571;
      continue;
    }
    break;
  }
  _M0L1iS1086->$0 = 0;
  while (1) {
    int32_t _M0L3valS3573 = _M0L1iS1086->$0;
    if (_M0L3valS3573 < _M0L7n__postS1082) {
      int32_t _M0L3valS3574 = _M0L1iS1086->$0;
      int32_t _M0L3valS3580;
      int32_t _M0L6_2atmpS3579;
      #line 478 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1083, _M0L3valS3574)) {
        int32_t _M0L3valS3575 = _M0L1iS1086->$0;
        int32_t _M0L3valS3578 = _M0L1iS1086->$0;
        float _M0L6_2atmpS3577;
        float _M0L6_2atmpS3576;
        #line 479 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3577
        = _M0MPC15array5Array2atGfE(_M0L5tpostS1087, _M0L3valS3578);
        _M0L6_2atmpS3576 = _M0L6_2atmpS3577 + 0x1p+0f;
        #line 479 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS1087, _M0L3valS3575, _M0L6_2atmpS3576);
      }
      _M0L3valS3580 = _M0L1iS1086->$0;
      _M0L6_2atmpS3579 = _M0L3valS3580 + 1;
      _M0L1iS1086->$0 = _M0L6_2atmpS3579;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1086);
    }
    break;
  }
  _M0L1jS1090->$0 = 0;
  while (1) {
    int32_t _M0L3valS3581 = _M0L1jS1090->$0;
    if (_M0L3valS3581 < _M0L6n__preS1080) {
      int32_t _M0L3valS3582 = _M0L1jS1090->$0;
      int32_t _M0L3valS3588;
      int32_t _M0L6_2atmpS3587;
      #line 485 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1081, _M0L3valS3582)) {
        int32_t _M0L3valS3583 = _M0L1jS1090->$0;
        int32_t _M0L3valS3586 = _M0L1jS1090->$0;
        float _M0L6_2atmpS3585;
        float _M0L6_2atmpS3584;
        #line 486 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3585
        = _M0MPC15array5Array2atGfE(_M0L4tpreS1091, _M0L3valS3586);
        _M0L6_2atmpS3584 = _M0L6_2atmpS3585 + 0x1p+0f;
        #line 486 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS1091, _M0L3valS3583, _M0L6_2atmpS3584);
      }
      _M0L3valS3588 = _M0L1jS1090->$0;
      _M0L6_2atmpS3587 = _M0L3valS3588 + 1;
      _M0L1jS1090->$0 = _M0L6_2atmpS3587;
      continue;
    }
    break;
  }
  _M0L1jS1090->$0 = 0;
  while (1) {
    int32_t _M0L3valS3589 = _M0L1jS1090->$0;
    if (_M0L3valS3589 < _M0L6n__preS1080) {
      int32_t _M0L3valS3590 = _M0L1jS1090->$0;
      int32_t _M0L3valS3608;
      int32_t _M0L6_2atmpS3607;
      #line 493 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1081, _M0L3valS3590)) {
        int32_t _M0L3valS3606 = _M0L1jS1090->$0;
        int32_t _M0L5startS1095;
        int32_t _M0L3valS3605;
        int32_t _M0L6_2atmpS3604;
        int32_t _M0L3endS1097;
        struct _M0TPB8MutLocalGiE* _M0L1sS1098;
        #line 494 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS1095
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1096, _M0L3valS3606);
        _M0L3valS3605 = _M0L1jS1090->$0;
        _M0L6_2atmpS3604 = _M0L3valS3605 + 1;
        #line 495 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3endS1097
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1096, _M0L6_2atmpS3604);
        _M0L1sS1098
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS1098)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS1098->$0 = _M0L5startS1095;
        while (1) {
          int32_t _M0L3valS3591 = _M0L1sS1098->$0;
          if (_M0L3valS3591 < _M0L3endS1097) {
            int32_t _M0L3valS3603 = _M0L1sS1098->$0;
            int32_t _M0L9post__idxS1099;
            int32_t _M0L3valS3602;
            float _M0L6_2atmpS3600;
            float _M0L6_2atmpS3601;
            float _M0L5ratioS1101;
            float _M0L3lnxS1102;
            float _M0L1xS1103;
            float _M0L1aS3598;
            float _M0L6_2atmpS3599;
            float _M0L2dwS1104;
            int32_t _M0L3valS3592;
            int32_t _M0L3valS3595;
            float _M0L6_2atmpS3594;
            float _M0L6_2atmpS3593;
            int32_t _M0L3valS3597;
            int32_t _M0L6_2atmpS3596;
            #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L9post__idxS1099
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1100, _M0L3valS3603);
            _M0L3valS3602 = _M0L1jS1090->$0;
            #line 499 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3600
            = _M0MPC15array5Array2atGfE(_M0L4tpreS1091, _M0L3valS3602);
            #line 499 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3601
            = _M0MPC15array5Array2atGfE(_M0L5tpostS1087, _M0L9post__idxS1099);
            _M0L5ratioS1101 = _M0L6_2atmpS3600 / _M0L6_2atmpS3601;
            #line 500 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L3lnxS1102 = _M0FP26RiantR8snn__mbt4logf(_M0L5ratioS1101);
            _M0L1xS1103 = _M0L3lnxS1102 * _M0L3lnxS1102;
            _M0L1aS3598 = _M0L5paramS1085->$0;
            #line 502 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3599
            = _M0FP26RiantR8snn__mbt20mexican__hat__kernel(_M0L1xS1103);
            _M0L2dwS1104 = _M0L1aS3598 * _M0L6_2atmpS3599;
            _M0L3valS3592 = _M0L1sS1098->$0;
            _M0L3valS3595 = _M0L1sS1098->$0;
            #line 503 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3594
            = _M0MPC15array5Array2atGfE(_M0L1wS1105, _M0L3valS3595);
            _M0L6_2atmpS3593 = _M0L6_2atmpS3594 + _M0L2dwS1104;
            #line 503 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1105, _M0L3valS3592, _M0L6_2atmpS3593);
            _M0L3valS3597 = _M0L1sS1098->$0;
            _M0L6_2atmpS3596 = _M0L3valS3597 + 1;
            _M0L1sS1098->$0 = _M0L6_2atmpS3596;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS1098);
          }
          break;
        }
      }
      _M0L3valS3608 = _M0L1jS1090->$0;
      _M0L6_2atmpS3607 = _M0L3valS3608 + 1;
      _M0L1jS1090->$0 = _M0L6_2atmpS3607;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1090);
    }
    break;
  }
  #line 511 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L3nnzS1108 = _M0MPC15array5Array6lengthGfE(_M0L1wS1105);
  _M0L2s2S1109
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S1109)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S1109->$0 = 0;
  while (1) {
    int32_t _M0L3valS3609 = _M0L2s2S1109->$0;
    if (_M0L3valS3609 < _M0L3nnzS1108) {
      int32_t _M0L3valS3621 = _M0L2s2S1109->$0;
      int32_t _M0L9post__idxS1110;
      int32_t _M0L3valS3620;
      int32_t _M0L6_2atmpS3619;
      #line 514 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L9post__idxS1110
      = _M0MPC15array5Array2atGiE(_M0L6colptrS1100, _M0L3valS3621);
      #line 515 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (
        _M0MPC15array5Array2atGbE(_M0L10post__fireS1083, _M0L9post__idxS1110)
      ) {
        int32_t _M0L3valS3618 = _M0L2s2S1109->$0;
        int32_t _M0L6j__preS1111;
        float _M0L6_2atmpS3616;
        float _M0L6_2atmpS3617;
        float _M0L5ratioS1112;
        float _M0L3lnxS1113;
        float _M0L1xS1114;
        float _M0L1aS3614;
        float _M0L6_2atmpS3615;
        float _M0L2dwS1115;
        int32_t _M0L3valS3610;
        int32_t _M0L3valS3613;
        float _M0L6_2atmpS3612;
        float _M0L6_2atmpS3611;
        #line 518 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6j__preS1111
        = _M0FP26RiantR8snn__mbt20find__pre__for__conn(_M0L6rowptrS1096, _M0L3valS3618);
        #line 519 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3616
        = _M0MPC15array5Array2atGfE(_M0L4tpreS1091, _M0L6j__preS1111);
        #line 519 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3617
        = _M0MPC15array5Array2atGfE(_M0L5tpostS1087, _M0L9post__idxS1110);
        _M0L5ratioS1112 = _M0L6_2atmpS3616 / _M0L6_2atmpS3617;
        #line 520 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3lnxS1113 = _M0FP26RiantR8snn__mbt4logf(_M0L5ratioS1112);
        _M0L1xS1114 = _M0L3lnxS1113 * _M0L3lnxS1113;
        _M0L1aS3614 = _M0L5paramS1085->$0;
        #line 522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3615
        = _M0FP26RiantR8snn__mbt20mexican__hat__kernel(_M0L1xS1114);
        _M0L2dwS1115 = _M0L1aS3614 * _M0L6_2atmpS3615;
        _M0L3valS3610 = _M0L2s2S1109->$0;
        _M0L3valS3613 = _M0L2s2S1109->$0;
        #line 523 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3612
        = _M0MPC15array5Array2atGfE(_M0L1wS1105, _M0L3valS3613);
        _M0L6_2atmpS3611 = _M0L6_2atmpS3612 + _M0L2dwS1115;
        #line 523 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1105, _M0L3valS3610, _M0L6_2atmpS3611);
      }
      _M0L3valS3620 = _M0L2s2S1109->$0;
      _M0L6_2atmpS3619 = _M0L3valS3620 + 1;
      _M0L2s2S1109->$0 = _M0L6_2atmpS3619;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S1109);
    }
    break;
  }
  _M0L2s3S1117
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s3S1117)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s3S1117->$0 = 0;
  while (1) {
    int32_t _M0L3valS3622 = _M0L2s3S1117->$0;
    if (_M0L3valS3622 < _M0L3nnzS1108) {
      int32_t _M0L3valS3625 = _M0L2s3S1117->$0;
      float _M0L6_2atmpS3623;
      float _M0L6w__minS3624;
      int32_t _M0L3valS3634;
      int32_t _M0L6_2atmpS3633;
      #line 530 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3623
      = _M0MPC15array5Array2atGfE(_M0L1wS1105, _M0L3valS3625);
      _M0L6w__minS3624 = _M0L5paramS1085->$3;
      if (_M0L6_2atmpS3623 < _M0L6w__minS3624) {
        int32_t _M0L3valS3626 = _M0L2s3S1117->$0;
        float _M0L6w__minS3627 = _M0L5paramS1085->$3;
        #line 531 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1105, _M0L3valS3626, _M0L6w__minS3627);
      } else {
        int32_t _M0L3valS3630 = _M0L2s3S1117->$0;
        float _M0L6_2atmpS3628;
        float _M0L6w__maxS3629;
        #line 532 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3628
        = _M0MPC15array5Array2atGfE(_M0L1wS1105, _M0L3valS3630);
        _M0L6w__maxS3629 = _M0L5paramS1085->$2;
        if (_M0L6_2atmpS3628 > _M0L6w__maxS3629) {
          int32_t _M0L3valS3631 = _M0L2s3S1117->$0;
          float _M0L6w__maxS3632 = _M0L5paramS1085->$2;
          #line 533 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0MPC15array5Array3setGfE(_M0L1wS1105, _M0L3valS3631, _M0L6w__maxS3632);
        }
      }
      _M0L3valS3634 = _M0L2s3S1117->$0;
      _M0L6_2atmpS3633 = _M0L3valS3634 + 1;
      _M0L2s3S1117->$0 = _M0L6_2atmpS3633;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s3S1117);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20find__pre__for__conn(
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1074,
  int32_t _M0L1sS1078
) {
  int32_t _M0L6_2atmpS3548;
  int32_t _M0L1nS1073;
  struct _M0TPB8MutLocalGiE* _M0L2loS1075;
  struct _M0TPB8MutLocalGiE* _M0L2hiS1076;
  int32_t _result_6052;
  #line 542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 543 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3548 = _M0MPC15array5Array6lengthGiE(_M0L6rowptrS1074);
  _M0L1nS1073 = _M0L6_2atmpS3548 - 1;
  _M0L2loS1075
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2loS1075)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2loS1075->$0 = 0;
  _M0L2hiS1076
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2hiS1076)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2hiS1076->$0 = _M0L1nS1073;
  while (1) {
    int32_t _M0L3valS3540 = _M0L2loS1075->$0;
    int32_t _M0L3valS3541 = _M0L2hiS1076->$0;
    if (_M0L3valS3540 < _M0L3valS3541) {
      int32_t _M0L3valS3546 = _M0L2loS1075->$0;
      int32_t _M0L3valS3547 = _M0L2hiS1076->$0;
      int32_t _M0L6_2atmpS3545 = _M0L3valS3546 + _M0L3valS3547;
      int32_t _M0L6_2atmpS3544 = _M0L6_2atmpS3545 + 1;
      int32_t _M0L3midS1077 = _M0L6_2atmpS3544 / 2;
      int32_t _M0L6_2atmpS3542;
      #line 548 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3542
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1074, _M0L3midS1077);
      if (_M0L6_2atmpS3542 <= _M0L1sS1078) {
        _M0L2loS1075->$0 = _M0L3midS1077;
      } else {
        int32_t _M0L6_2atmpS3543 = _M0L3midS1077 - 1;
        _M0L2hiS1076->$0 = _M0L6_2atmpS3543;
      }
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2hiS1076);
    }
    break;
  }
  _result_6052 = _M0L2loS1075->$0;
  moonbit_decref_cycle_free(_M0L2loS1075);
  return _result_6052;
}

float _M0FP26RiantR8snn__mbt20mexican__hat__kernel(float _M0L1xS1070) {
  #line 427 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 428 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  if (_M0MPC15float5Float7is__nan(_M0L1xS1070)) {
    return 0x0p+0f;
  } else {
    float _M0L6_2atmpS3539 = -_M0L1xS1070;
    float _M0L3argS1071 = _M0L6_2atmpS3539 / 0x1.6a09e65dc27dfp+0f;
    float _M0L6_2atmpS3537 = 0x1p+0f - _M0L1xS1070;
    float _M0L6_2atmpS3538;
    float _M0L1vS1072;
    #line 432 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS3538 = _M0FP26RiantR8snn__mbt4expf(_M0L3argS1071);
    _M0L1vS1072 = _M0L6_2atmpS3537 * _M0L6_2atmpS3538;
    #line 433 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    if (_M0MPC15float5Float7is__nan(_M0L1vS1072)) {
      return 0x0p+0f;
    } else {
      return _M0L1vS1072;
    }
  }
}

int32_t _M0FP26RiantR8snn__mbt19stimulate__balanced(
  struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L1sS1037,
  float _M0L4timeS1035,
  float _M0L2dtS1046
) {
  int32_t _M0L1nS1036;
  struct _M0TP26RiantR8snn__mbt17BalancedParameter* _M0L5paramS1038;
  float _M0L3kIES1039;
  float _M0L4betaS1040;
  float _M0L3tauS1041;
  float _M0L2r0S1042;
  float _M0L1wS1043;
  float _M0L3wIES1044;
  float _M0L6_2atmpS3536;
  float _M0L11inh__lambdaS1045;
  int32_t _M0L7_2abindS1047;
  int32_t _M0L1kS1048;
  float _M0L6_2atmpS3535;
  float _M0L2ccS1052;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
  _M0L1nS1036 = _M0L1sS1037->$1;
  _M0L5paramS1038 = _M0L1sS1037->$0;
  _M0L3kIES1039 = _M0L5paramS1038->$0;
  _M0L4betaS1040 = _M0L5paramS1038->$1;
  _M0L3tauS1041 = _M0L5paramS1038->$2;
  _M0L2r0S1042 = _M0L5paramS1038->$3;
  _M0L1wS1043 = _M0L5paramS1038->$4;
  _M0L3wIES1044 = _M0L5paramS1038->$5;
  _M0L6_2atmpS3536 = _M0L2r0S1042 * _M0L3kIES1039;
  _M0L11inh__lambdaS1045 = _M0L6_2atmpS3536 * _M0L2dtS1046;
  _M0L7_2abindS1047 = 0;
  _M0L1kS1048 = _M0L7_2abindS1047;
  while (1) {
    if (_M0L1kS1048 < _M0L1nS1036) {
      struct _M0TPB5ArrayGbE* _M0L4fireS3449 = _M0L1sS1037->$4;
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3457;
      int32_t _M0L1mS1051;
      int32_t _M0L6_2atmpS3448;
      #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3449, _M0L1kS1048, 0);
      if (_M0L11inh__lambdaS1045 <= 0x0p+0f) {
        goto join_1049;
      }
      _M0L3rngS3457 = _M0L1sS1037->$7;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
      _M0L1mS1051
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3457, _M0L11inh__lambdaS1045);
      if (_M0L1mS1051 > 0) {
        struct _M0TPB5ArrayGfE* _M0L2giS3450 = _M0L1sS1037->$3;
        struct _M0TPB5ArrayGfE* _M0L2giS3456 = _M0L1sS1037->$3;
        float _M0L6_2atmpS3452;
        float _M0L6_2atmpS3455;
        float _M0L6_2atmpS3454;
        float _M0L6_2atmpS3453;
        float _M0L6_2atmpS3451;
        #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3452
        = _M0MPC15array5Array2atGfE(_M0L2giS3456, _M0L1kS1048);
        _M0L6_2atmpS3455 = (float)_M0L1mS1051;
        _M0L6_2atmpS3454 = _M0L1wS1043 * _M0L6_2atmpS3455;
        _M0L6_2atmpS3453 = _M0L6_2atmpS3454 * _M0L3wIES1044;
        _M0L6_2atmpS3451 = _M0L6_2atmpS3452 + _M0L6_2atmpS3453;
        #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L2giS3450, _M0L1kS1048, _M0L6_2atmpS3451);
      }
      goto join_1049;
      goto joinlet_6054;
      join_1049:;
      _M0L6_2atmpS3448 = _M0L1kS1048 + 1;
      _M0L1kS1048 = _M0L6_2atmpS3448;
      continue;
      joinlet_6054:;
    }
    break;
  }
  _M0L6_2atmpS3535 = _M0L2dtS1046 / _M0L3tauS1041;
  _M0L2ccS1052 = 0x1p+0f - _M0L6_2atmpS3535;
  if (_M0L5paramS1038->$6) {
    struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3495 = _M0L1sS1037->$7;
    double _M0L6_2atmpS3494;
    float _M0L6_2atmpS3493;
    float _M0L2reS1053;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3458;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3463;
    float _M0L6_2atmpS3462;
    float _M0L6_2atmpS3461;
    float _M0L6_2atmpS3460;
    float _M0L6_2atmpS3459;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3492;
    float _M0L6_2atmpS3491;
    float _M0L6_2atmpS3490;
    struct _M0TPB8MutLocalGfE* _M0L2nbS1054;
    float _M0L3valS3464;
    float _M0L3valS3465;
    float _M0L6_2atmpS3488;
    float _M0L3valS3489;
    float _M0L6_2atmpS3485;
    struct _M0TPB5ArrayGfE* _M0L1rS3487;
    float _M0L6_2atmpS3486;
    float _M0L6_2atmpS3484;
    struct _M0TPB8MutLocalGfE* _M0L5erateS1055;
    float _M0L3valS3466;
    struct _M0TPB5ArrayGfE* _M0L1rS3467;
    struct _M0TPB5ArrayGfE* _M0L1rS3474;
    float _M0L6_2atmpS3469;
    float _M0L3valS3473;
    float _M0L6_2atmpS3472;
    float _M0L6_2atmpS3471;
    float _M0L6_2atmpS3470;
    float _M0L6_2atmpS3468;
    float _M0L3valS3483;
    float _M0L11exc__lambdaS1056;
    struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3482;
    int32_t _M0L1mS1057;
    #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3494 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS3495);
    _M0L6_2atmpS3493 = (float)_M0L6_2atmpS3494;
    _M0L2reS1053 = _M0L6_2atmpS3493 - 0x1p-1f;
    _M0L5noiseS3458 = _M0L1sS1037->$6;
    _M0L5noiseS3463 = _M0L1sS1037->$6;
    #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3462 = _M0MPC15array5Array2atGfE(_M0L5noiseS3463, 0);
    _M0L6_2atmpS3461 = _M0L6_2atmpS3462 - _M0L2reS1053;
    _M0L6_2atmpS3460 = _M0L6_2atmpS3461 * _M0L2ccS1052;
    _M0L6_2atmpS3459 = _M0L6_2atmpS3460 + _M0L2reS1053;
    #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0MPC15array5Array3setGfE(_M0L5noiseS3458, 0, _M0L6_2atmpS3459);
    _M0L5noiseS3492 = _M0L1sS1037->$6;
    #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3491 = _M0MPC15array5Array2atGfE(_M0L5noiseS3492, 0);
    _M0L6_2atmpS3490 = _M0L6_2atmpS3491 * _M0L4betaS1040;
    _M0L2nbS1054
    = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
    Moonbit_object_header(_M0L2nbS1054)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L2nbS1054->$0 = _M0L6_2atmpS3490;
    _M0L3valS3464 = _M0L2nbS1054->$0;
    if (_M0L3valS3464 > 0x1p+0f) {
      _M0L2nbS1054->$0 = 0x1p+0f;
    }
    _M0L3valS3465 = _M0L2nbS1054->$0;
    if (_M0L3valS3465 < 0x0p+0f) {
      _M0L2nbS1054->$0 = 0x0p+0f;
    }
    _M0L6_2atmpS3488 = _M0L2r0S1042 / 0x1p+1f;
    _M0L3valS3489 = _M0L2nbS1054->$0;
    moonbit_decref_cycle_free(_M0L2nbS1054);
    _M0L6_2atmpS3485 = _M0L6_2atmpS3488 * _M0L3valS3489;
    _M0L1rS3487 = _M0L1sS1037->$5;
    #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3486 = _M0MPC15array5Array2atGfE(_M0L1rS3487, 0);
    _M0L6_2atmpS3484 = _M0L6_2atmpS3485 + _M0L6_2atmpS3486;
    _M0L5erateS1055
    = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
    Moonbit_object_header(_M0L5erateS1055)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L5erateS1055->$0 = _M0L6_2atmpS3484;
    _M0L3valS3466 = _M0L5erateS1055->$0;
    if (_M0L3valS3466 < 0x0p+0f) {
      _M0L5erateS1055->$0 = 0x0p+0f;
    }
    _M0L1rS3467 = _M0L1sS1037->$5;
    _M0L1rS3474 = _M0L1sS1037->$5;
    #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3469 = _M0MPC15array5Array2atGfE(_M0L1rS3474, 0);
    _M0L3valS3473 = _M0L5erateS1055->$0;
    _M0L6_2atmpS3472 = _M0L2r0S1042 - _M0L3valS3473;
    _M0L6_2atmpS3471 = _M0L6_2atmpS3472 / 0x1.9p+8f;
    _M0L6_2atmpS3470 = _M0L6_2atmpS3471 * _M0L2dtS1046;
    _M0L6_2atmpS3468 = _M0L6_2atmpS3469 + _M0L6_2atmpS3470;
    #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0MPC15array5Array3setGfE(_M0L1rS3467, 0, _M0L6_2atmpS3468);
    _M0L3valS3483 = _M0L5erateS1055->$0;
    moonbit_decref_cycle_free(_M0L5erateS1055);
    _M0L11exc__lambdaS1056 = _M0L3valS3483 * _M0L2dtS1046;
    _M0L3rngS3482 = _M0L1sS1037->$7;
    #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L1mS1057
    = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3482, _M0L11exc__lambdaS1056);
    if (_M0L1mS1057 > 0) {
      float _M0L6_2atmpS3481 = (float)_M0L1mS1057;
      float _M0L3addS1058 = _M0L1wS1043 * _M0L6_2atmpS3481;
      int32_t _M0L7_2abindS1059 = 0;
      int32_t _M0L1iS1060 = _M0L7_2abindS1059;
      while (1) {
        if (_M0L1iS1060 < _M0L1nS1036) {
          struct _M0TPB5ArrayGfE* _M0L2geS3475 = _M0L1sS1037->$2;
          struct _M0TPB5ArrayGfE* _M0L2geS3478 = _M0L1sS1037->$2;
          float _M0L6_2atmpS3477;
          float _M0L6_2atmpS3476;
          struct _M0TPB5ArrayGbE* _M0L4fireS3479;
          int32_t _M0L6_2atmpS3480;
          #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0L6_2atmpS3477
          = _M0MPC15array5Array2atGfE(_M0L2geS3478, _M0L1iS1060);
          _M0L6_2atmpS3476 = _M0L6_2atmpS3477 + _M0L3addS1058;
          #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGfE(_M0L2geS3475, _M0L1iS1060, _M0L6_2atmpS3476);
          _M0L4fireS3479 = _M0L1sS1037->$4;
          #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGbE(_M0L4fireS3479, _M0L1iS1060, 1);
          _M0L6_2atmpS3480 = _M0L1iS1060 + 1;
          _M0L1iS1060 = _M0L6_2atmpS3480;
          continue;
        }
        break;
      }
    }
  } else {
    int32_t _M0L7_2abindS1062 = 0;
    int32_t _M0L1iS1063 = _M0L7_2abindS1062;
    while (1) {
      if (_M0L1iS1063 < _M0L1nS1036) {
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3533 =
          _M0L1sS1037->$7;
        double _M0L6_2atmpS3532;
        float _M0L6_2atmpS3531;
        float _M0L2reS1064;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3496;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3501;
        float _M0L6_2atmpS3500;
        float _M0L6_2atmpS3499;
        float _M0L6_2atmpS3498;
        float _M0L6_2atmpS3497;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3530;
        float _M0L6_2atmpS3529;
        float _M0L6_2atmpS3528;
        struct _M0TPB8MutLocalGfE* _M0L2nbS1065;
        float _M0L3valS3502;
        float _M0L3valS3503;
        float _M0L6_2atmpS3526;
        float _M0L3valS3527;
        float _M0L6_2atmpS3523;
        struct _M0TPB5ArrayGfE* _M0L1rS3525;
        float _M0L6_2atmpS3524;
        float _M0L6_2atmpS3522;
        struct _M0TPB8MutLocalGfE* _M0L5erateS1066;
        float _M0L3valS3504;
        struct _M0TPB5ArrayGfE* _M0L1rS3505;
        struct _M0TPB5ArrayGfE* _M0L1rS3512;
        float _M0L6_2atmpS3507;
        float _M0L3valS3511;
        float _M0L6_2atmpS3510;
        float _M0L6_2atmpS3509;
        float _M0L6_2atmpS3508;
        float _M0L6_2atmpS3506;
        float _M0L3valS3521;
        float _M0L11exc__lambdaS1067;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3520;
        int32_t _M0L1mS1068;
        int32_t _M0L6_2atmpS3534;
        #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3532 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS3533);
        _M0L6_2atmpS3531 = (float)_M0L6_2atmpS3532;
        _M0L2reS1064 = _M0L6_2atmpS3531 - 0x1p-1f;
        _M0L5noiseS3496 = _M0L1sS1037->$6;
        _M0L5noiseS3501 = _M0L1sS1037->$6;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3500
        = _M0MPC15array5Array2atGfE(_M0L5noiseS3501, _M0L1iS1063);
        _M0L6_2atmpS3499 = _M0L6_2atmpS3500 - _M0L2reS1064;
        _M0L6_2atmpS3498 = _M0L6_2atmpS3499 * _M0L2ccS1052;
        _M0L6_2atmpS3497 = _M0L6_2atmpS3498 + _M0L2reS1064;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L5noiseS3496, _M0L1iS1063, _M0L6_2atmpS3497);
        _M0L5noiseS3530 = _M0L1sS1037->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3529
        = _M0MPC15array5Array2atGfE(_M0L5noiseS3530, _M0L1iS1063);
        _M0L6_2atmpS3528 = _M0L6_2atmpS3529 * _M0L4betaS1040;
        _M0L2nbS1065
        = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
        Moonbit_object_header(_M0L2nbS1065)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L2nbS1065->$0 = _M0L6_2atmpS3528;
        _M0L3valS3502 = _M0L2nbS1065->$0;
        if (_M0L3valS3502 > 0x1p+0f) {
          _M0L2nbS1065->$0 = 0x1p+0f;
        }
        _M0L3valS3503 = _M0L2nbS1065->$0;
        if (_M0L3valS3503 < 0x0p+0f) {
          _M0L2nbS1065->$0 = 0x0p+0f;
        }
        _M0L6_2atmpS3526 = _M0L2r0S1042 / 0x1p+1f;
        _M0L3valS3527 = _M0L2nbS1065->$0;
        moonbit_decref_cycle_free(_M0L2nbS1065);
        _M0L6_2atmpS3523 = _M0L6_2atmpS3526 * _M0L3valS3527;
        _M0L1rS3525 = _M0L1sS1037->$5;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3524
        = _M0MPC15array5Array2atGfE(_M0L1rS3525, _M0L1iS1063);
        _M0L6_2atmpS3522 = _M0L6_2atmpS3523 + _M0L6_2atmpS3524;
        _M0L5erateS1066
        = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
        Moonbit_object_header(_M0L5erateS1066)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L5erateS1066->$0 = _M0L6_2atmpS3522;
        _M0L3valS3504 = _M0L5erateS1066->$0;
        if (_M0L3valS3504 < 0x0p+0f) {
          _M0L5erateS1066->$0 = 0x0p+0f;
        }
        _M0L1rS3505 = _M0L1sS1037->$5;
        _M0L1rS3512 = _M0L1sS1037->$5;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3507
        = _M0MPC15array5Array2atGfE(_M0L1rS3512, _M0L1iS1063);
        _M0L3valS3511 = _M0L5erateS1066->$0;
        _M0L6_2atmpS3510 = _M0L2r0S1042 - _M0L3valS3511;
        _M0L6_2atmpS3509 = _M0L6_2atmpS3510 / 0x1.9p+8f;
        _M0L6_2atmpS3508 = _M0L6_2atmpS3509 * _M0L2dtS1046;
        _M0L6_2atmpS3506 = _M0L6_2atmpS3507 + _M0L6_2atmpS3508;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L1rS3505, _M0L1iS1063, _M0L6_2atmpS3506);
        _M0L3valS3521 = _M0L5erateS1066->$0;
        moonbit_decref_cycle_free(_M0L5erateS1066);
        _M0L11exc__lambdaS1067 = _M0L3valS3521 * _M0L2dtS1046;
        _M0L3rngS3520 = _M0L1sS1037->$7;
        #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L1mS1068
        = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3520, _M0L11exc__lambdaS1067);
        if (_M0L1mS1068 > 0) {
          struct _M0TPB5ArrayGfE* _M0L2geS3513 = _M0L1sS1037->$2;
          struct _M0TPB5ArrayGfE* _M0L2geS3518 = _M0L1sS1037->$2;
          float _M0L6_2atmpS3515;
          float _M0L6_2atmpS3517;
          float _M0L6_2atmpS3516;
          float _M0L6_2atmpS3514;
          struct _M0TPB5ArrayGbE* _M0L4fireS3519;
          #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0L6_2atmpS3515
          = _M0MPC15array5Array2atGfE(_M0L2geS3518, _M0L1iS1063);
          _M0L6_2atmpS3517 = (float)_M0L1mS1068;
          _M0L6_2atmpS3516 = _M0L1wS1043 * _M0L6_2atmpS3517;
          _M0L6_2atmpS3514 = _M0L6_2atmpS3515 + _M0L6_2atmpS3516;
          #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGfE(_M0L2geS3513, _M0L1iS1063, _M0L6_2atmpS3514);
          _M0L4fireS3519 = _M0L1sS1037->$4;
          #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGbE(_M0L4fireS3519, _M0L1iS1063, 1);
        }
        _M0L6_2atmpS3534 = _M0L1iS1063 + 1;
        _M0L1iS1063 = _M0L6_2atmpS3534;
        continue;
      }
      break;
    }
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS1033
) {
  struct _M0TUmmmmE* _M0L1sS1032;
  uint64_t _M0L6_2atmpS3447;
  struct _M0TUmmmmE* _M0L1tS1034;
  uint64_t _M0L6_2atmpS3443;
  uint64_t _M0L6_2atmpS3444;
  uint64_t _M0L6_2atmpS3445;
  uint64_t _M0L6_2atmpS3446;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_6057;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS1032 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS1033);
  _M0L6_2atmpS3447 = _M0L1sS1032->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS1034 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS3447);
  _M0L6_2atmpS3443 = _M0L1sS1032->$0;
  _M0L6_2atmpS3444 = _M0L1sS1032->$1;
  _M0L6_2atmpS3445 = _M0L1sS1032->$2;
  moonbit_decref_cycle_free(_M0L1sS1032);
  _M0L6_2atmpS3446 = _M0L1tS1034->$0;
  moonbit_decref_cycle_free(_M0L1tS1034);
  _block_6057
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_6057)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6057->$0 = _M0L6_2atmpS3443;
  _block_6057->$1 = _M0L6_2atmpS3444;
  _block_6057->$2 = _M0L6_2atmpS3445;
  _block_6057->$3 = _M0L6_2atmpS3446;
  return _block_6057;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(
  uint64_t _M0L4seedS1024
) {
  uint64_t _M0L2s1S1023;
  uint64_t _M0L2z1S1025;
  uint64_t _M0L2s2S1026;
  uint64_t _M0L2z2S1027;
  uint64_t _M0L2s3S1028;
  uint64_t _M0L2z3S1029;
  uint64_t _M0L2s4S1030;
  uint64_t _M0L2z4S1031;
  struct _M0TUmmmmE* _block_6058;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S1023 = _M0L4seedS1024 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S1025 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S1023);
  _M0L2s2S1026 = _M0L2s1S1023 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S1027 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S1026);
  _M0L2s3S1028 = _M0L2s2S1026 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S1029 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S1028);
  _M0L2s4S1030 = _M0L2s3S1028 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S1031 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S1030);
  _block_6058 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_6058)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6058->$0 = _M0L2z1S1025;
  _block_6058->$1 = _M0L2z2S1027;
  _block_6058->$2 = _M0L2z3S1029;
  _block_6058->$3 = _M0L2z4S1031;
  return _block_6058;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS1021) {
  uint64_t _M0L6_2atmpS3442;
  uint64_t _M0L6_2atmpS3441;
  uint64_t _M0L1zS1020;
  uint64_t _M0L6_2atmpS3440;
  uint64_t _M0L6_2atmpS3439;
  uint64_t _M0L1zS1022;
  uint64_t _M0L6_2atmpS3438;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS3442 = _M0L1zS1021 >> 30;
  _M0L6_2atmpS3441 = _M0L1zS1021 ^ _M0L6_2atmpS3442;
  _M0L1zS1020 = _M0L6_2atmpS3441 * 13787848793156543929ull;
  _M0L6_2atmpS3440 = _M0L1zS1020 >> 27;
  _M0L6_2atmpS3439 = _M0L1zS1020 ^ _M0L6_2atmpS3440;
  _M0L1zS1022 = _M0L6_2atmpS3439 * 10723151780598845931ull;
  _M0L6_2atmpS3438 = _M0L1zS1022 >> 31;
  return _M0L1zS1022 ^ _M0L6_2atmpS3438;
}

int32_t _M0FP26RiantR8snn__mbt25stimulate__current__array(
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L1sS1008
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3422;
  int32_t _M0L6_2atmpS3421;
  float _M0L12noise__sigmaS3423;
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6activeS3422 = _M0L1sS1008->$1;
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6_2atmpS3421 = _M0MPC15array5Array2atGbE(_M0L6activeS3422, 0);
  if (!_M0L6_2atmpS3421) {
    return 0;
  }
  _M0L12noise__sigmaS3423 = _M0L1sS1008->$4;
  if (_M0L12noise__sigmaS3423 <= 0x0p+0f) {
    int32_t _M0L7_2abindS1009 = 0;
    int32_t _M0L7_2abindS1010 = _M0L1sS1008->$3;
    int32_t _M0L1kS1011 = _M0L7_2abindS1009;
    while (1) {
      if (_M0L1kS1011 < _M0L7_2abindS1010) {
        struct _M0TPB5ArrayGfE* _M0L1iS3424 = _M0L1sS1008->$2;
        float _M0L7i__baseS3425 = _M0L1sS1008->$0;
        int32_t _M0L6_2atmpS3426;
        #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3424, _M0L1kS1011, _M0L7i__baseS3425);
        _M0L6_2atmpS3426 = _M0L1kS1011 + 1;
        _M0L1kS1011 = _M0L6_2atmpS3426;
        continue;
      }
      break;
    }
  } else {
    float _M0L5sigmaS1013 = _M0L1sS1008->$4;
    struct _M0TPB8MutLocalGiE* _M0L1kS1014 =
      (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS1014)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS1014->$0 = 0;
    while (1) {
      int32_t _M0L3valS3427 = _M0L1kS1014->$0;
      int32_t _M0L1nS3428 = _M0L1sS1008->$3;
      if (_M0L3valS3427 < _M0L1nS3428) {
        double _M0L2z1S1016;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3437 =
          _M0L1sS1008->$5;
        struct _M0TUddE* _M0L7_2abindS1017;
        double _M0L5_2az1S1018;
        struct _M0TPB5ArrayGfE* _M0L1iS3429;
        int32_t _M0L3valS3430;
        float _M0L7i__baseS3432;
        float _M0L6_2atmpS3434;
        float _M0L6_2atmpS3433;
        float _M0L6_2atmpS3431;
        int32_t _M0L3valS3436;
        int32_t _M0L6_2atmpS3435;
        #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0L7_2abindS1017
        = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS3437);
        _M0L5_2az1S1018 = _M0L7_2abindS1017->$0;
        moonbit_decref_cycle_free(_M0L7_2abindS1017);
        _M0L2z1S1016 = _M0L5_2az1S1018;
        goto join_1015;
        goto joinlet_6061;
        join_1015:;
        _M0L1iS3429 = _M0L1sS1008->$2;
        _M0L3valS3430 = _M0L1kS1014->$0;
        _M0L7i__baseS3432 = _M0L1sS1008->$0;
        _M0L6_2atmpS3434 = (float)_M0L2z1S1016;
        _M0L6_2atmpS3433 = _M0L5sigmaS1013 * _M0L6_2atmpS3434;
        _M0L6_2atmpS3431 = _M0L7i__baseS3432 + _M0L6_2atmpS3433;
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3429, _M0L3valS3430, _M0L6_2atmpS3431);
        _M0L3valS3436 = _M0L1kS1014->$0;
        _M0L6_2atmpS3435 = _M0L3valS3436 + 1;
        _M0L1kS1014->$0 = _M0L6_2atmpS3435;
        joinlet_6061:;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS1014);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22stimulate__current__if(
  struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L1sS995
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3406;
  int32_t _M0L6_2atmpS3405;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS996;
  int32_t _M0L1nS997;
  float _M0L12noise__sigmaS3407;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6activeS3406 = _M0L1sS995->$1;
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6_2atmpS3405 = _M0MPC15array5Array2atGbE(_M0L6activeS3406, 0);
  if (!_M0L6_2atmpS3405) {
    return 0;
  }
  _M0L3popS996 = _M0L1sS995->$2;
  _M0L1nS997 = _M0L3popS996->$2;
  _M0L12noise__sigmaS3407 = _M0L1sS995->$3;
  if (_M0L12noise__sigmaS3407 <= 0x0p+0f) {
    int32_t _M0L7_2abindS998 = 0;
    int32_t _M0L1iS999 = _M0L7_2abindS998;
    while (1) {
      if (_M0L1iS999 < _M0L1nS997) {
        struct _M0TPB5ArrayGfE* _M0L1iS3408 = _M0L3popS996->$7;
        float _M0L7i__baseS3409 = _M0L1sS995->$0;
        int32_t _M0L6_2atmpS3410;
        #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3408, _M0L1iS999, _M0L7i__baseS3409);
        _M0L6_2atmpS3410 = _M0L1iS999 + 1;
        _M0L1iS999 = _M0L6_2atmpS3410;
        continue;
      }
      break;
    }
  } else {
    float _M0L5sigmaS1001 = _M0L1sS995->$3;
    struct _M0TPB8MutLocalGiE* _M0L1kS1002 =
      (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS1002)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS1002->$0 = 0;
    while (1) {
      int32_t _M0L3valS3411 = _M0L1kS1002->$0;
      if (_M0L3valS3411 < _M0L1nS997) {
        double _M0L2z1S1004;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3420 = _M0L1sS995->$4;
        struct _M0TUddE* _M0L7_2abindS1005;
        double _M0L5_2az1S1006;
        struct _M0TPB5ArrayGfE* _M0L1iS3412;
        int32_t _M0L3valS3413;
        float _M0L7i__baseS3415;
        float _M0L6_2atmpS3417;
        float _M0L6_2atmpS3416;
        float _M0L6_2atmpS3414;
        int32_t _M0L3valS3419;
        int32_t _M0L6_2atmpS3418;
        #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0L7_2abindS1005
        = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS3420);
        _M0L5_2az1S1006 = _M0L7_2abindS1005->$0;
        moonbit_decref_cycle_free(_M0L7_2abindS1005);
        _M0L2z1S1004 = _M0L5_2az1S1006;
        goto join_1003;
        goto joinlet_6064;
        join_1003:;
        _M0L1iS3412 = _M0L3popS996->$7;
        _M0L3valS3413 = _M0L1kS1002->$0;
        _M0L7i__baseS3415 = _M0L1sS995->$0;
        _M0L6_2atmpS3417 = (float)_M0L2z1S1004;
        _M0L6_2atmpS3416 = _M0L5sigmaS1001 * _M0L6_2atmpS3417;
        _M0L6_2atmpS3414 = _M0L7i__baseS3415 + _M0L6_2atmpS3416;
        #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3412, _M0L3valS3413, _M0L6_2atmpS3414);
        _M0L3valS3419 = _M0L1kS1002->$0;
        _M0L6_2atmpS3418 = _M0L3valS3419 + 1;
        _M0L1kS1002->$0 = _M0L6_2atmpS3418;
        joinlet_6064:;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS1002);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13stimulate__if(
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L1sS984,
  float _M0L4timeS994,
  float _M0L2dtS986
) {
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3392;
  struct _M0TPB5ArrayGbE* _M0L6activeS3391;
  int32_t _M0L6_2atmpS3390;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3404;
  float _M0L4rateS3403;
  float _M0L6lambdaS985;
  struct _M0TPB5ArrayGiE* _M0L7_2abindS987;
  int32_t _M0L7_2abindS988;
  int32_t* _M0L7_2abindS989;
  int32_t _M0L2__S990;
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L5paramS3392 = _M0L1sS984->$0;
  _M0L6activeS3391 = _M0L5paramS3392->$2;
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3390 = _M0MPC15array5Array2atGbE(_M0L6activeS3391, 0);
  if (!_M0L6_2atmpS3390) {
    return 0;
  }
  _M0L5paramS3404 = _M0L1sS984->$0;
  _M0L4rateS3403 = _M0L5paramS3404->$0;
  _M0L6lambdaS985 = _M0L4rateS3403 * _M0L2dtS986;
  if (_M0L6lambdaS985 <= 0x0p+0f) {
    return 0;
  }
  _M0L7_2abindS987 = _M0L1sS984->$1;
  _M0L7_2abindS988 = _M0L7_2abindS987->$1;
  _M0L7_2abindS989 = _M0L7_2abindS987->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS989);
  _M0L2__S990 = 0;
  while (1) {
    if (_M0L2__S990 < _M0L7_2abindS988) {
      int32_t _M0L1nS991 = (int32_t)_M0L7_2abindS989[_M0L2__S990];
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3401 = _M0L1sS984->$3;
      int32_t _M0L1kS992;
      int32_t _M0L6_2atmpS3402;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
      _M0L1kS992
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3401, _M0L6lambdaS985);
      if (_M0L1kS992 > 0) {
        struct _M0TPB5ArrayGfE* _M0L1gS3393 = _M0L1sS984->$2;
        struct _M0TPB5ArrayGfE* _M0L1gS3400 = _M0L1sS984->$2;
        float _M0L6_2atmpS3395;
        struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3399;
        float _M0L2muS3397;
        float _M0L6_2atmpS3398;
        float _M0L6_2atmpS3396;
        float _M0L6_2atmpS3394;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0L6_2atmpS3395 = _M0MPC15array5Array2atGfE(_M0L1gS3400, _M0L1nS991);
        _M0L5paramS3399 = _M0L1sS984->$0;
        _M0L2muS3397 = _M0L5paramS3399->$1;
        _M0L6_2atmpS3398 = (float)_M0L1kS992;
        _M0L6_2atmpS3396 = _M0L2muS3397 * _M0L6_2atmpS3398;
        _M0L6_2atmpS3394 = _M0L6_2atmpS3395 + _M0L6_2atmpS3396;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0MPC15array5Array3setGfE(_M0L1gS3393, _M0L1nS991, _M0L6_2atmpS3394);
      }
      _M0L6_2atmpS3402 = _M0L2__S990 + 1;
      _M0L2__S990 = _M0L6_2atmpS3402;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS989);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0MP26RiantR8snn__mbt17PoissonStimulusIF3new(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS975,
  moonbit_string_t _M0L3symS981,
  float _M0L4rateS982,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS983
) {
  int32_t _M0L1nS974;
  int32_t* _M0L6_2atmpS3389;
  struct _M0TPB5ArrayGiE* _M0L7neuronsS976;
  int32_t _M0L7_2abindS977;
  int32_t _M0L1kS978;
  struct _M0TPB5ArrayGfE* _M0L1gS980;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L6_2atmpS3388;
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _block_6067;
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L1nS974 = _M0L3popS975->$2;
  _M0L6_2atmpS3389 = (int32_t*)moonbit_empty_int32_array;
  _M0L7neuronsS976
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L7neuronsS976)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _M0L7neuronsS976->$0 = _M0L6_2atmpS3389;
  _M0L7neuronsS976->$1 = 0;
  _M0L7_2abindS977 = 0;
  _M0L1kS978 = _M0L7_2abindS977;
  while (1) {
    if (_M0L1kS978 < _M0L1nS974) {
      int32_t _M0L6_2atmpS3387;
      #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
      _M0MPC15array5Array4pushGiE(_M0L7neuronsS976, _M0L1kS978);
      _M0L6_2atmpS3387 = _M0L1kS978 + 1;
      _M0L1kS978 = _M0L6_2atmpS3387;
      continue;
    }
    break;
  }
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  if (
    _M0L3symS981 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS981)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS981, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS981) * 2)
  ) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5668 = _M0L3popS975->$13;
    moonbit_incref_cycle_free(_M0L8_2afieldS5668);
    _M0L1gS980 = _M0L8_2afieldS5668;
  } else {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5669 = _M0L3popS975->$14;
    moonbit_incref_cycle_free(_M0L8_2afieldS5669);
    _M0L1gS980 = _M0L8_2afieldS5669;
  }
  #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3388 = _M0MP26RiantR8snn__mbt12PoissonFixed3new(_M0L4rateS982);
  moonbit_incref_cycle_free(_M0L3rngS983);
  _block_6067
  = (struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF));
  Moonbit_object_header(_block_6067)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 86, 0);
  _block_6067->$0 = _M0L6_2atmpS3388;
  _block_6067->$1 = _M0L7neuronsS976;
  _block_6067->$2 = _M0L1gS980;
  _block_6067->$3 = _M0L3rngS983;
  return _block_6067;
}

int32_t _M0MP26RiantR8snn__mbt12PoissonFixed7set__mu(
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L1pS972,
  float _M0L2muS973
) {
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L1pS972->$1 = _M0L2muS973;
  return 0;
}

struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0MP26RiantR8snn__mbt12PoissonFixed3new(
  float _M0L4rateS971
) {
  uint8_t* _M0L6_2atmpS3386;
  struct _M0TPB5ArrayGbE* _M0L6_2atmpS3385;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _block_6068;
  #line 23 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3386 = (uint8_t*)moonbit_make_bytes_raw(1);
  _M0L6_2atmpS3386[0] = 1;
  _M0L6_2atmpS3385
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_M0L6_2atmpS3385)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 92, 0);
  _M0L6_2atmpS3385->$0 = _M0L6_2atmpS3386;
  _M0L6_2atmpS3385->$1 = 1;
  _block_6068
  = (struct _M0TP26RiantR8snn__mbt12PoissonFixed*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt12PoissonFixed));
  Moonbit_object_header(_block_6068)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 95, 0);
  _block_6068->$0 = _M0L4rateS971;
  _block_6068->$1 = 0x1p+0f;
  _block_6068->$2 = _M0L6_2atmpS3385;
  return _block_6068;
}

int32_t _M0FP26RiantR8snn__mbt16stimulate__layer(
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L1sS954,
  float _M0L4timeS952,
  float _M0L2dtS957
) {
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS3384;
  int32_t _M0L6n__preS953;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3383;
  int32_t _M0L7n__postS955;
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS3382;
  float _M0L4rateS3381;
  float _M0L6lambdaS956;
  int32_t _M0L7_2abindS958;
  int32_t _M0L1iS959;
  moonbit_string_t _M0L3symS3378;
  struct _M0TPB5ArrayGfE* _M0L9g__targetS961;
  int32_t _M0L7_2abindS962;
  int32_t _M0L1iS963;
  #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  _M0L5paramS3384 = _M0L1sS954->$0;
  _M0L6n__preS953 = _M0L5paramS3384->$1;
  _M0L4postS3383 = _M0L1sS954->$1;
  _M0L7n__postS955 = _M0L4postS3383->$2;
  _M0L5paramS3382 = _M0L1sS954->$0;
  _M0L4rateS3381 = _M0L5paramS3382->$0;
  _M0L6lambdaS956 = _M0L4rateS3381 * _M0L2dtS957;
  _M0L7_2abindS958 = 0;
  _M0L1iS959 = _M0L7_2abindS958;
  while (1) {
    if (_M0L1iS959 < _M0L6n__preS953) {
      struct _M0TPB5ArrayGbE* _M0L4fireS3363 = _M0L1sS954->$3;
      int32_t _M0L6_2atmpS3364;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3363, _M0L1iS959, 0);
      _M0L6_2atmpS3364 = _M0L1iS959 + 1;
      _M0L1iS959 = _M0L6_2atmpS3364;
      continue;
    }
    break;
  }
  if (_M0L6lambdaS956 <= 0x0p+0f) {
    return 0;
  }
  _M0L3symS3378 = _M0L1sS954->$2;
  #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  if (
    _M0L3symS3378 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS3378)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS3378, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS3378) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3379 = _M0L1sS954->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5670 = _M0L4postS3379->$13;
    moonbit_incref_cycle_free(_M0L8_2afieldS5670);
    _M0L9g__targetS961 = _M0L8_2afieldS5670;
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3380 = _M0L1sS954->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5671 = _M0L4postS3380->$14;
    moonbit_incref_cycle_free(_M0L8_2afieldS5671);
    _M0L9g__targetS961 = _M0L8_2afieldS5671;
  }
  _M0L7_2abindS962 = 0;
  _M0L1iS963 = _M0L7_2abindS962;
  while (1) {
    if (_M0L1iS963 < _M0L6n__preS953) {
      struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS3368 =
        _M0L1sS954->$0;
      struct _M0TPB5ArrayGbE* _M0L6activeS3367 = _M0L5paramS3368->$2;
      int32_t _M0L6_2atmpS3366;
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3377;
      int32_t _M0L1kS966;
      int32_t _M0L6_2atmpS3365;
      moonbit_incref_cycle_free(_M0L6activeS3367);
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0L6_2atmpS3366
      = _M0MPC15array5Array2atGbE(_M0L6activeS3367, _M0L1iS963);
      moonbit_decref_cycle_free(_M0L6activeS3367);
      if (!_M0L6_2atmpS3366) {
        goto join_964;
      }
      _M0L3rngS3377 = _M0L1sS954->$6;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0L1kS966
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3377, _M0L6lambdaS956);
      if (_M0L1kS966 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS3369 = _M0L1sS954->$3;
        int32_t _M0L7_2abindS967;
        int32_t _M0L1jS968;
        #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS3369, _M0L1iS963, 1);
        _M0L7_2abindS967 = 0;
        _M0L1jS968 = _M0L7_2abindS967;
        while (1) {
          if (_M0L1jS968 < _M0L7n__postS955) {
            int32_t _M0L6_2atmpS3375 = _M0L1jS968 * _M0L6n__preS953;
            int32_t _M0L3idxS969 = _M0L6_2atmpS3375 + _M0L1iS963;
            struct _M0TPB5ArrayGbE* _M0L12connectivityS3370 = _M0L1sS954->$5;
            int32_t _M0L6_2atmpS3376;
            #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
            if (
              _M0MPC15array5Array2atGbE(_M0L12connectivityS3370, _M0L3idxS969)
            ) {
              float _M0L6_2atmpS3372;
              struct _M0TPB5ArrayGfE* _M0L7weightsS3374;
              float _M0L6_2atmpS3373;
              float _M0L6_2atmpS3371;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0L6_2atmpS3372
              = _M0MPC15array5Array2atGfE(_M0L9g__targetS961, _M0L1jS968);
              _M0L7weightsS3374 = _M0L1sS954->$4;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0L6_2atmpS3373
              = _M0MPC15array5Array2atGfE(_M0L7weightsS3374, _M0L3idxS969);
              _M0L6_2atmpS3371 = _M0L6_2atmpS3372 + _M0L6_2atmpS3373;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0MPC15array5Array3setGfE(_M0L9g__targetS961, _M0L1jS968, _M0L6_2atmpS3371);
            }
            _M0L6_2atmpS3376 = _M0L1jS968 + 1;
            _M0L1jS968 = _M0L6_2atmpS3376;
            continue;
          }
          break;
        }
      }
      goto join_964;
      goto joinlet_6071;
      join_964:;
      _M0L6_2atmpS3365 = _M0L1iS963 + 1;
      _M0L1iS963 = _M0L6_2atmpS3365;
      continue;
      joinlet_6071:;
    } else {
      moonbit_decref_cycle_free(_M0L9g__targetS961);
    }
    break;
  }
  return 0;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS947
) {
  double _M0L2u1S946;
  double _M0L8u1__safeS948;
  double _M0L2u2S949;
  double _M0L6_2atmpS3362;
  double _M0L6_2atmpS3361;
  double _M0L1rS950;
  double _M0L5thetaS951;
  double _M0L6_2atmpS3360;
  double _M0L6_2atmpS3357;
  double _M0L6_2atmpS3359;
  double _M0L6_2atmpS3358;
  struct _M0TUddE* _block_6073;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S946 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS947);
  if (_M0L2u1S946 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS948 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS948 = _M0L2u1S946;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S949 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS947);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS3362 = _M0FPC14math2ln(_M0L8u1__safeS948);
  _M0L6_2atmpS3361 = -0x1p+1 * _M0L6_2atmpS3362;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS950 = sqrt(_M0L6_2atmpS3361);
  _M0L5thetaS951 = 0x1.921fb54442d18p+2 * _M0L2u2S949;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS3360 = _M0FPC14math3cos(_M0L5thetaS951);
  _M0L6_2atmpS3357 = _M0L1rS950 * _M0L6_2atmpS3360;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS3359 = _M0FPC14math3sin(_M0L5thetaS951);
  _M0L6_2atmpS3358 = _M0L1rS950 * _M0L6_2atmpS3359;
  _block_6073 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_6073)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6073->$0 = _M0L6_2atmpS3357;
  _block_6073->$1 = _M0L6_2atmpS3358;
  return _block_6073;
}

int32_t _M0FP26RiantR8snn__mbt15sample__poisson(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS944,
  float _M0L6lambdaS938
) {
  float _M0L6_2atmpS3356;
  float _M0L6_2atmpS3355;
  double _M0L1lS939;
  struct _M0TPB8MutLocalGdE* _M0L1pS940;
  struct _M0TPB8MutLocalGiE* _M0L1kS941;
  float _M0L6_2atmpS3354;
  int32_t _M0L8ten__lamS943;
  int32_t _M0L3capS942;
  int32_t _M0L3valS3353;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  if (_M0L6lambdaS938 <= 0x0p+0f) {
    return 0;
  }
  _M0L6_2atmpS3356 = -_M0L6lambdaS938;
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3355 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3356);
  _M0L1lS939 = (double)_M0L6_2atmpS3355;
  _M0L1pS940
  = (struct _M0TPB8MutLocalGdE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGdE));
  Moonbit_object_header(_M0L1pS940)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1pS940->$0 = 0x1p+0;
  _M0L1kS941
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS941)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS941->$0 = 0;
  _M0L6_2atmpS3354 = _M0L6lambdaS938 * 0x1.4p+3f;
  #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L8ten__lamS943 = _M0MPC15float5Float7to__int(_M0L6_2atmpS3354);
  if (_M0L8ten__lamS943 > 100) {
    _M0L3capS942 = _M0L8ten__lamS943;
  } else {
    _M0L3capS942 = 100;
  }
  while (1) {
    int32_t _M0L3valS3345 = _M0L1kS941->$0;
    int32_t _M0L6_2atmpS3344 = _M0L3valS3345 + 1;
    double _M0L3valS3347;
    double _M0L6_2atmpS3348;
    double _M0L6_2atmpS3346;
    double _M0L3valS3349;
    int32_t _M0L3valS3351;
    _M0L1kS941->$0 = _M0L6_2atmpS3344;
    _M0L3valS3347 = _M0L1pS940->$0;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
    _M0L6_2atmpS3348 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS944);
    _M0L6_2atmpS3346 = _M0L3valS3347 * _M0L6_2atmpS3348;
    _M0L1pS940->$0 = _M0L6_2atmpS3346;
    _M0L3valS3349 = _M0L1pS940->$0;
    if (_M0L3valS3349 < _M0L1lS939) {
      int32_t _M0L3valS3350;
      moonbit_decref_cycle_free(_M0L1pS940);
      _M0L3valS3350 = _M0L1kS941->$0;
      moonbit_decref_cycle_free(_M0L1kS941);
      return _M0L3valS3350 - 1;
    }
    _M0L3valS3351 = _M0L1kS941->$0;
    if (_M0L3valS3351 > _M0L3capS942) {
      int32_t _M0L3valS3352;
      moonbit_decref_cycle_free(_M0L1pS940);
      _M0L3valS3352 = _M0L1kS941->$0;
      moonbit_decref_cycle_free(_M0L1kS941);
      return _M0L3valS3352 - 1;
    }
    continue;
    break;
  }
  _M0L3valS3353 = _M0L1kS941->$0;
  moonbit_decref_cycle_free(_M0L1kS941);
  return _M0L3valS3353 - 1;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS936
) {
  uint64_t _M0L1uS935;
  uint64_t _M0L4bitsS937;
  double _M0L6_2atmpS3343;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS935 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS936);
  _M0L4bitsS937 = _M0L1uS935 >> 11;
  _M0L6_2atmpS3343 = (double)_M0L4bitsS937;
  return _M0L6_2atmpS3343 * 0x1p-53;
}

int32_t _M0FP26RiantR8snn__mbt20stimulate__spiketime(
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L1sS929,
  float _M0L1tS931,
  float _M0L1wS933
) {
  struct _M0TPB8MutLocalGiE* _M0L1iS928;
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L1iS928
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS928)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS928->$0 = 0;
  while (1) {
    int32_t _M0L3valS3305 = _M0L1iS928->$0;
    int32_t _M0L1nS3306 = _M0L1sS929->$0;
    if (_M0L3valS3305 < _M0L1nS3306) {
      struct _M0TPB5ArrayGbE* _M0L4fireS3307 = _M0L1sS929->$4;
      int32_t _M0L3valS3308 = _M0L1iS928->$0;
      int32_t _M0L3valS3310;
      int32_t _M0L6_2atmpS3309;
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3307, _M0L3valS3308, 0);
      _M0L3valS3310 = _M0L1iS928->$0;
      _M0L6_2atmpS3309 = _M0L3valS3310 + 1;
      _M0L1iS928->$0 = _M0L6_2atmpS3309;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS928);
    }
    break;
  }
  while (1) {
    struct _M0TPB5ArrayGiE* _M0L11next__indexS3314 = _M0L1sS929->$3;
    int32_t _M0L6_2atmpS3313;
    int32_t _if__result_6077;
    #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
    _M0L6_2atmpS3313 = _M0MPC15array5Array2atGiE(_M0L11next__indexS3314, 0);
    if (_M0L6_2atmpS3313 >= 0) {
      struct _M0TPB5ArrayGfE* _M0L11next__spikeS3312 = _M0L1sS929->$2;
      float _M0L6_2atmpS3311;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3311 = _M0MPC15array5Array2atGfE(_M0L11next__spikeS3312, 0);
      _if__result_6077 = _M0L6_2atmpS3311 <= _M0L1tS931;
    } else {
      _if__result_6077 = 0;
    }
    if (_if__result_6077) {
      struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS3342 =
        _M0L1sS929->$1;
      struct _M0TPB5ArrayGiE* _M0L7neuronsS3339 = _M0L5paramS3342->$1;
      struct _M0TPB5ArrayGiE* _M0L11next__indexS3341 = _M0L1sS929->$3;
      int32_t _M0L6_2atmpS3340;
      int32_t _M0L1jS932;
      struct _M0TPB5ArrayGbE* _M0L4fireS3315;
      struct _M0TPB5ArrayGfE* _M0L1gS3316;
      struct _M0TPB5ArrayGfE* _M0L1gS3319;
      float _M0L6_2atmpS3318;
      float _M0L6_2atmpS3317;
      struct _M0TPB5ArrayGiE* _M0L11next__indexS3325;
      int32_t _M0L6_2atmpS3324;
      int32_t _M0L6_2atmpS3320;
      struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS3323;
      struct _M0TPB5ArrayGfE* _M0L10spiketimesS3322;
      int32_t _M0L6_2atmpS3321;
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3340 = _M0MPC15array5Array2atGiE(_M0L11next__indexS3341, 0);
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L1jS932
      = _M0MPC15array5Array2atGiE(_M0L7neuronsS3339, _M0L6_2atmpS3340);
      _M0L4fireS3315 = _M0L1sS929->$4;
      #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3315, _M0L1jS932, 1);
      _M0L1gS3316 = _M0L1sS929->$5;
      _M0L1gS3319 = _M0L1sS929->$5;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3318 = _M0MPC15array5Array2atGfE(_M0L1gS3319, _M0L1jS932);
      _M0L6_2atmpS3317 = _M0L6_2atmpS3318 + _M0L1wS933;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS3316, _M0L1jS932, _M0L6_2atmpS3317);
      _M0L11next__indexS3325 = _M0L1sS929->$3;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3324 = _M0MPC15array5Array2atGiE(_M0L11next__indexS3325, 0);
      _M0L6_2atmpS3320 = _M0L6_2atmpS3324 + 1;
      _M0L5paramS3323 = _M0L1sS929->$1;
      _M0L10spiketimesS3322 = _M0L5paramS3323->$0;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3321 = _M0MPC15array5Array6lengthGfE(_M0L10spiketimesS3322);
      if (_M0L6_2atmpS3320 < _M0L6_2atmpS3321) {
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3326 = _M0L1sS929->$3;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3329 = _M0L1sS929->$3;
        int32_t _M0L6_2atmpS3328;
        int32_t _M0L6_2atmpS3327;
        struct _M0TPB5ArrayGfE* _M0L11next__spikeS3330;
        struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS3335;
        struct _M0TPB5ArrayGfE* _M0L10spiketimesS3332;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3334;
        int32_t _M0L6_2atmpS3333;
        float _M0L6_2atmpS3331;
        #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS3328
        = _M0MPC15array5Array2atGiE(_M0L11next__indexS3329, 0);
        _M0L6_2atmpS3327 = _M0L6_2atmpS3328 + 1;
        #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGiE(_M0L11next__indexS3326, 0, _M0L6_2atmpS3327);
        _M0L11next__spikeS3330 = _M0L1sS929->$2;
        _M0L5paramS3335 = _M0L1sS929->$1;
        _M0L10spiketimesS3332 = _M0L5paramS3335->$0;
        _M0L11next__indexS3334 = _M0L1sS929->$3;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS3333
        = _M0MPC15array5Array2atGiE(_M0L11next__indexS3334, 0);
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS3331
        = _M0MPC15array5Array2atGfE(_M0L10spiketimesS3332, _M0L6_2atmpS3333);
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGfE(_M0L11next__spikeS3330, 0, _M0L6_2atmpS3331);
      } else {
        struct _M0TPB5ArrayGfE* _M0L11next__spikeS3336 = _M0L1sS929->$2;
        float _M0L6_2atmpS3337 = 0x0p+0f / (float)MOONBIT_ZERO;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3338;
        #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGfE(_M0L11next__spikeS3336, 0, _M0L6_2atmpS3337);
        _M0L11next__indexS3338 = _M0L1sS929->$3;
        #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGiE(_M0L11next__indexS3338, 0, -1);
      }
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28markram__stp__step__timestep(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS915,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS913,
  struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0L5paramS917,
  float _M0L6t__nowS912,
  float _M0L2dtS922
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3218;
  int32_t _M0L6_2atmpS3217;
  int32_t _if__result_6078;
  int32_t _M0L6n__preS914;
  struct _M0TPB5ArrayGfE* _M0L3rhoS3220;
  int32_t _M0L6_2atmpS3219;
  float _M0L11u__baselineS916;
  float _M0L6tau__fS3304;
  float _M0L11inv__tau__fS918;
  float _M0L6tau__dS3303;
  float _M0L11inv__tau__dS919;
  struct _M0TPB8MutLocalGiE* _M0L1jS920;
  #line 473 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS3218 = _M0L4varsS913->$6;
  #line 482 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3217 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3218);
  if (_M0L6_2atmpS3217 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3216 = _M0L4varsS913->$6;
    int32_t _M0L6_2atmpS3215;
    #line 482 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS3215 = _M0MPC15array5Array2atGbE(_M0L6activeS3216, 0);
    _if__result_6078 = !_M0L6_2atmpS3215;
  } else {
    _if__result_6078 = 0;
  }
  if (_if__result_6078) {
    return 0;
  }
  _M0L6n__preS914 = _M0L4varsS913->$0;
  _M0L3rhoS3220 = _M0L3synS915->$6;
  #line 487 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3219 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS3220);
  if (_M0L6_2atmpS3219 == 0) {
    return 0;
  }
  _M0L11u__baselineS916 = _M0L5paramS917->$0;
  _M0L6tau__fS3304 = _M0L5paramS917->$1;
  _M0L11inv__tau__fS918 = 0x1p+0f / _M0L6tau__fS3304;
  _M0L6tau__dS3303 = _M0L5paramS917->$2;
  _M0L11inv__tau__dS919 = 0x1p+0f / _M0L6tau__dS3303;
  _M0L1jS920
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS920)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS920->$0 = 0;
  while (1) {
    int32_t _M0L3valS3221 = _M0L1jS920->$0;
    if (_M0L3valS3221 < _M0L6n__preS914) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3224 = _M0L3synS915->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3222 = _M0L3preS3224->$5;
      int32_t _M0L3valS3223 = _M0L1jS920->$0;
      int32_t _M0L3valS3251;
      int32_t _M0L6_2atmpS3250;
      #line 496 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3222, _M0L3valS3223)) {
        struct _M0TPB5ArrayGfE* _M0L1uS3225 = _M0L4varsS913->$2;
        int32_t _M0L3valS3226 = _M0L1jS920->$0;
        struct _M0TPB5ArrayGfE* _M0L1uS3234 = _M0L4varsS913->$2;
        int32_t _M0L3valS3235 = _M0L1jS920->$0;
        float _M0L6_2atmpS3228;
        struct _M0TPB5ArrayGfE* _M0L1uS3232;
        int32_t _M0L3valS3233;
        float _M0L6_2atmpS3231;
        float _M0L6_2atmpS3230;
        float _M0L6_2atmpS3229;
        float _M0L6_2atmpS3227;
        struct _M0TPB5ArrayGfE* _M0L1xS3236;
        int32_t _M0L3valS3237;
        struct _M0TPB5ArrayGfE* _M0L1xS3248;
        int32_t _M0L3valS3249;
        float _M0L6_2atmpS3239;
        struct _M0TPB5ArrayGfE* _M0L1uS3246;
        int32_t _M0L3valS3247;
        float _M0L6_2atmpS3245;
        float _M0L6_2atmpS3241;
        struct _M0TPB5ArrayGfE* _M0L1xS3243;
        int32_t _M0L3valS3244;
        float _M0L6_2atmpS3242;
        float _M0L6_2atmpS3240;
        float _M0L6_2atmpS3238;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3228
        = _M0MPC15array5Array2atGfE(_M0L1uS3234, _M0L3valS3235);
        _M0L1uS3232 = _M0L4varsS913->$2;
        _M0L3valS3233 = _M0L1jS920->$0;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3231
        = _M0MPC15array5Array2atGfE(_M0L1uS3232, _M0L3valS3233);
        _M0L6_2atmpS3230 = 0x1p+0f - _M0L6_2atmpS3231;
        _M0L6_2atmpS3229 = _M0L11u__baselineS916 * _M0L6_2atmpS3230;
        _M0L6_2atmpS3227 = _M0L6_2atmpS3228 + _M0L6_2atmpS3229;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3225, _M0L3valS3226, _M0L6_2atmpS3227);
        _M0L1xS3236 = _M0L4varsS913->$3;
        _M0L3valS3237 = _M0L1jS920->$0;
        _M0L1xS3248 = _M0L4varsS913->$3;
        _M0L3valS3249 = _M0L1jS920->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3239
        = _M0MPC15array5Array2atGfE(_M0L1xS3248, _M0L3valS3249);
        _M0L1uS3246 = _M0L4varsS913->$2;
        _M0L3valS3247 = _M0L1jS920->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3245
        = _M0MPC15array5Array2atGfE(_M0L1uS3246, _M0L3valS3247);
        _M0L6_2atmpS3241 = -_M0L6_2atmpS3245;
        _M0L1xS3243 = _M0L4varsS913->$3;
        _M0L3valS3244 = _M0L1jS920->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3242
        = _M0MPC15array5Array2atGfE(_M0L1xS3243, _M0L3valS3244);
        _M0L6_2atmpS3240 = _M0L6_2atmpS3241 * _M0L6_2atmpS3242;
        _M0L6_2atmpS3238 = _M0L6_2atmpS3239 + _M0L6_2atmpS3240;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3236, _M0L3valS3237, _M0L6_2atmpS3238);
      }
      _M0L3valS3251 = _M0L1jS920->$0;
      _M0L6_2atmpS3250 = _M0L3valS3251 + 1;
      _M0L1jS920->$0 = _M0L6_2atmpS3250;
      continue;
    }
    break;
  }
  _M0L1jS920->$0 = 0;
  while (1) {
    int32_t _M0L3valS3252 = _M0L1jS920->$0;
    if (_M0L3valS3252 < _M0L6n__preS914) {
      struct _M0TPB5ArrayGfE* _M0L1uS3253 = _M0L4varsS913->$2;
      int32_t _M0L3valS3254 = _M0L1jS920->$0;
      struct _M0TPB5ArrayGfE* _M0L1uS3263 = _M0L4varsS913->$2;
      int32_t _M0L3valS3264 = _M0L1jS920->$0;
      float _M0L6_2atmpS3256;
      struct _M0TPB5ArrayGfE* _M0L1uS3261;
      int32_t _M0L3valS3262;
      float _M0L6_2atmpS3260;
      float _M0L6_2atmpS3259;
      float _M0L6_2atmpS3258;
      float _M0L6_2atmpS3257;
      float _M0L6_2atmpS3255;
      struct _M0TPB5ArrayGfE* _M0L1xS3265;
      int32_t _M0L3valS3266;
      struct _M0TPB5ArrayGfE* _M0L1xS3275;
      int32_t _M0L3valS3276;
      float _M0L6_2atmpS3268;
      struct _M0TPB5ArrayGfE* _M0L1xS3273;
      int32_t _M0L3valS3274;
      float _M0L6_2atmpS3272;
      float _M0L6_2atmpS3271;
      float _M0L6_2atmpS3270;
      float _M0L6_2atmpS3269;
      float _M0L6_2atmpS3267;
      struct _M0TPB5ArrayGfE* _M0L8rho__preS3277;
      int32_t _M0L3valS3278;
      struct _M0TPB5ArrayGfE* _M0L1uS3284;
      int32_t _M0L3valS3285;
      float _M0L6_2atmpS3280;
      struct _M0TPB5ArrayGfE* _M0L1xS3282;
      int32_t _M0L3valS3283;
      float _M0L6_2atmpS3281;
      float _M0L6_2atmpS3279;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3302;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS3300;
      int32_t _M0L3valS3301;
      int32_t _M0L5startS923;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3299;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS3296;
      int32_t _M0L3valS3298;
      int32_t _M0L6_2atmpS3297;
      int32_t _M0L3endS924;
      struct _M0TPB8MutLocalGiE* _M0L1sS925;
      int32_t _M0L3valS3295;
      int32_t _M0L6_2atmpS3294;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3256
      = _M0MPC15array5Array2atGfE(_M0L1uS3263, _M0L3valS3264);
      _M0L1uS3261 = _M0L4varsS913->$2;
      _M0L3valS3262 = _M0L1jS920->$0;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3260
      = _M0MPC15array5Array2atGfE(_M0L1uS3261, _M0L3valS3262);
      _M0L6_2atmpS3259 = _M0L11u__baselineS916 - _M0L6_2atmpS3260;
      _M0L6_2atmpS3258 = _M0L2dtS922 * _M0L6_2atmpS3259;
      _M0L6_2atmpS3257 = _M0L6_2atmpS3258 * _M0L11inv__tau__fS918;
      _M0L6_2atmpS3255 = _M0L6_2atmpS3256 + _M0L6_2atmpS3257;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS3253, _M0L3valS3254, _M0L6_2atmpS3255);
      _M0L1xS3265 = _M0L4varsS913->$3;
      _M0L3valS3266 = _M0L1jS920->$0;
      _M0L1xS3275 = _M0L4varsS913->$3;
      _M0L3valS3276 = _M0L1jS920->$0;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3268
      = _M0MPC15array5Array2atGfE(_M0L1xS3275, _M0L3valS3276);
      _M0L1xS3273 = _M0L4varsS913->$3;
      _M0L3valS3274 = _M0L1jS920->$0;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3272
      = _M0MPC15array5Array2atGfE(_M0L1xS3273, _M0L3valS3274);
      _M0L6_2atmpS3271 = 0x1p+0f - _M0L6_2atmpS3272;
      _M0L6_2atmpS3270 = _M0L2dtS922 * _M0L6_2atmpS3271;
      _M0L6_2atmpS3269 = _M0L6_2atmpS3270 * _M0L11inv__tau__dS919;
      _M0L6_2atmpS3267 = _M0L6_2atmpS3268 + _M0L6_2atmpS3269;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS3265, _M0L3valS3266, _M0L6_2atmpS3267);
      _M0L8rho__preS3277 = _M0L4varsS913->$4;
      _M0L3valS3278 = _M0L1jS920->$0;
      _M0L1uS3284 = _M0L4varsS913->$2;
      _M0L3valS3285 = _M0L1jS920->$0;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3280
      = _M0MPC15array5Array2atGfE(_M0L1uS3284, _M0L3valS3285);
      _M0L1xS3282 = _M0L4varsS913->$3;
      _M0L3valS3283 = _M0L1jS920->$0;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3281
      = _M0MPC15array5Array2atGfE(_M0L1xS3282, _M0L3valS3283);
      _M0L6_2atmpS3279 = _M0L6_2atmpS3280 * _M0L6_2atmpS3281;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L8rho__preS3277, _M0L3valS3278, _M0L6_2atmpS3279);
      _M0L6matrixS3302 = _M0L3synS915->$4;
      _M0L6rowptrS3300 = _M0L6matrixS3302->$2;
      _M0L3valS3301 = _M0L1jS920->$0;
      #line 509 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L5startS923
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS3300, _M0L3valS3301);
      _M0L6matrixS3299 = _M0L3synS915->$4;
      _M0L6rowptrS3296 = _M0L6matrixS3299->$2;
      _M0L3valS3298 = _M0L1jS920->$0;
      _M0L6_2atmpS3297 = _M0L3valS3298 + 1;
      #line 510 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L3endS924
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS3296, _M0L6_2atmpS3297);
      _M0L1sS925
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS925)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS925->$0 = _M0L5startS923;
      while (1) {
        int32_t _M0L3valS3286 = _M0L1sS925->$0;
        if (_M0L3valS3286 < _M0L3endS924) {
          struct _M0TPB5ArrayGfE* _M0L3rhoS3287 = _M0L3synS915->$6;
          int32_t _M0L3valS3288 = _M0L1sS925->$0;
          struct _M0TPB5ArrayGfE* _M0L8rho__preS3290 = _M0L4varsS913->$4;
          int32_t _M0L3valS3291 = _M0L1jS920->$0;
          float _M0L6_2atmpS3289;
          int32_t _M0L3valS3293;
          int32_t _M0L6_2atmpS3292;
          #line 513 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
          _M0L6_2atmpS3289
          = _M0MPC15array5Array2atGfE(_M0L8rho__preS3290, _M0L3valS3291);
          #line 513 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rhoS3287, _M0L3valS3288, _M0L6_2atmpS3289);
          _M0L3valS3293 = _M0L1sS925->$0;
          _M0L6_2atmpS3292 = _M0L3valS3293 + 1;
          _M0L1sS925->$0 = _M0L6_2atmpS3292;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS925);
        }
        break;
      }
      _M0L3valS3295 = _M0L1jS920->$0;
      _M0L6_2atmpS3294 = _M0L3valS3295 + 1;
      _M0L1jS920->$0 = _M0L6_2atmpS3294;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS920);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23markram__stp__step__het(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS896,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS894,
  struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet* _M0L5paramS899,
  float _M0L6t__nowS903
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3126;
  int32_t _M0L6_2atmpS3125;
  int32_t _if__result_6082;
  int32_t _M0L6n__preS895;
  struct _M0TPB5ArrayGfE* _M0L3rhoS3128;
  int32_t _M0L6_2atmpS3127;
  struct _M0TPB8MutLocalGiE* _M0L1jS897;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS3126 = _M0L4varsS894->$6;
  #line 353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3125 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3126);
  if (_M0L6_2atmpS3125 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3124 = _M0L4varsS894->$6;
    int32_t _M0L6_2atmpS3123;
    #line 353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS3123 = _M0MPC15array5Array2atGbE(_M0L6activeS3124, 0);
    _if__result_6082 = !_M0L6_2atmpS3123;
  } else {
    _if__result_6082 = 0;
  }
  if (_if__result_6082) {
    return 0;
  }
  _M0L6n__preS895 = _M0L4varsS894->$0;
  _M0L3rhoS3128 = _M0L3synS896->$6;
  #line 357 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3127 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS3128);
  if (_M0L6_2atmpS3127 == 0) {
    return 0;
  }
  _M0L1jS897
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS897)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS897->$0 = 0;
  while (1) {
    int32_t _M0L3valS3129 = _M0L1jS897->$0;
    if (_M0L3valS3129 < _M0L6n__preS895) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3132 = _M0L3synS896->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3130 = _M0L3preS3132->$5;
      int32_t _M0L3valS3131 = _M0L1jS897->$0;
      int32_t _M0L3valS3214;
      int32_t _M0L6_2atmpS3213;
      #line 362 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3130, _M0L3valS3131)) {
        struct _M0TPB5ArrayGfE* _M0L6tau__dS3211 = _M0L5paramS899->$0;
        int32_t _M0L3valS3212 = _M0L1jS897->$0;
        float _M0L9tau__d__jS898;
        struct _M0TPB5ArrayGfE* _M0L6tau__fS3209;
        int32_t _M0L3valS3210;
        float _M0L9tau__f__jS900;
        struct _M0TPB5ArrayGfE* _M0L1uS3207;
        int32_t _M0L3valS3208;
        float _M0L14u__baseline__jS901;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS3205;
        int32_t _M0L3valS3206;
        float _M0L6_2atmpS3204;
        float _M0L7dt__preS902;
        float _M0L7dt__preS904;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS3133;
        int32_t _M0L3valS3134;
        float _M0L6_2atmpS3203;
        float _M0L6arg__fS905;
        struct _M0TPB5ArrayGfE* _M0L1uS3135;
        int32_t _M0L3valS3136;
        struct _M0TPB5ArrayGfE* _M0L1uS3142;
        int32_t _M0L3valS3143;
        float _M0L6_2atmpS3141;
        float _M0L6_2atmpS3139;
        float _M0L6_2atmpS3140;
        float _M0L6_2atmpS3138;
        float _M0L6_2atmpS3137;
        float _M0L6_2atmpS3202;
        float _M0L6arg__dS906;
        struct _M0TPB5ArrayGfE* _M0L1xS3144;
        int32_t _M0L3valS3145;
        struct _M0TPB5ArrayGfE* _M0L1xS3151;
        int32_t _M0L3valS3152;
        float _M0L6_2atmpS3150;
        float _M0L6_2atmpS3148;
        float _M0L6_2atmpS3149;
        float _M0L6_2atmpS3147;
        float _M0L6_2atmpS3146;
        struct _M0TPB5ArrayGfE* _M0L8rho__preS3153;
        int32_t _M0L3valS3154;
        struct _M0TPB5ArrayGfE* _M0L1uS3160;
        int32_t _M0L3valS3161;
        float _M0L6_2atmpS3156;
        struct _M0TPB5ArrayGfE* _M0L1xS3158;
        int32_t _M0L3valS3159;
        float _M0L6_2atmpS3157;
        float _M0L6_2atmpS3155;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3201;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3199;
        int32_t _M0L3valS3200;
        int32_t _M0L5startS907;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3198;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3195;
        int32_t _M0L3valS3197;
        int32_t _M0L6_2atmpS3196;
        int32_t _M0L3endS908;
        struct _M0TPB8MutLocalGiE* _M0L1sS909;
        struct _M0TPB5ArrayGfE* _M0L1uS3170;
        int32_t _M0L3valS3171;
        struct _M0TPB5ArrayGfE* _M0L1uS3179;
        int32_t _M0L3valS3180;
        float _M0L6_2atmpS3173;
        struct _M0TPB5ArrayGfE* _M0L1uS3177;
        int32_t _M0L3valS3178;
        float _M0L6_2atmpS3176;
        float _M0L6_2atmpS3175;
        float _M0L6_2atmpS3174;
        float _M0L6_2atmpS3172;
        struct _M0TPB5ArrayGfE* _M0L1xS3181;
        int32_t _M0L3valS3182;
        struct _M0TPB5ArrayGfE* _M0L1xS3193;
        int32_t _M0L3valS3194;
        float _M0L6_2atmpS3184;
        struct _M0TPB5ArrayGfE* _M0L1uS3191;
        int32_t _M0L3valS3192;
        float _M0L6_2atmpS3190;
        float _M0L6_2atmpS3186;
        struct _M0TPB5ArrayGfE* _M0L1xS3188;
        int32_t _M0L3valS3189;
        float _M0L6_2atmpS3187;
        float _M0L6_2atmpS3185;
        float _M0L6_2atmpS3183;
        #line 363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L9tau__d__jS898
        = _M0MPC15array5Array2atGfE(_M0L6tau__dS3211, _M0L3valS3212);
        _M0L6tau__fS3209 = _M0L5paramS899->$1;
        _M0L3valS3210 = _M0L1jS897->$0;
        #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L9tau__f__jS900
        = _M0MPC15array5Array2atGfE(_M0L6tau__fS3209, _M0L3valS3210);
        _M0L1uS3207 = _M0L5paramS899->$2;
        _M0L3valS3208 = _M0L1jS897->$0;
        #line 365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L14u__baseline__jS901
        = _M0MPC15array5Array2atGfE(_M0L1uS3207, _M0L3valS3208);
        _M0L11last__spikeS3205 = _M0L4varsS894->$5;
        _M0L3valS3206 = _M0L1jS897->$0;
        #line 366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3204
        = _M0MPC15array5Array2atGfE(_M0L11last__spikeS3205, _M0L3valS3206);
        _M0L7dt__preS902 = _M0L6t__nowS903 - _M0L6_2atmpS3204;
        if (_M0L7dt__preS902 < 0x0p+0f) {
          _M0L7dt__preS904 = 0x0p+0f;
        } else {
          _M0L7dt__preS904 = _M0L7dt__preS902;
        }
        _M0L11last__spikeS3133 = _M0L4varsS894->$5;
        _M0L3valS3134 = _M0L1jS897->$0;
        #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L11last__spikeS3133, _M0L3valS3134, _M0L6t__nowS903);
        _M0L6_2atmpS3203 = -_M0L7dt__preS904;
        _M0L6arg__fS905 = _M0L6_2atmpS3203 / _M0L9tau__f__jS900;
        _M0L1uS3135 = _M0L4varsS894->$2;
        _M0L3valS3136 = _M0L1jS897->$0;
        _M0L1uS3142 = _M0L4varsS894->$2;
        _M0L3valS3143 = _M0L1jS897->$0;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3141
        = _M0MPC15array5Array2atGfE(_M0L1uS3142, _M0L3valS3143);
        _M0L6_2atmpS3139 = _M0L14u__baseline__jS901 - _M0L6_2atmpS3141;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3140 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__fS905);
        _M0L6_2atmpS3138 = _M0L6_2atmpS3139 * _M0L6_2atmpS3140;
        _M0L6_2atmpS3137 = _M0L14u__baseline__jS901 - _M0L6_2atmpS3138;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3135, _M0L3valS3136, _M0L6_2atmpS3137);
        _M0L6_2atmpS3202 = -_M0L7dt__preS904;
        _M0L6arg__dS906 = _M0L6_2atmpS3202 / _M0L9tau__d__jS898;
        _M0L1xS3144 = _M0L4varsS894->$3;
        _M0L3valS3145 = _M0L1jS897->$0;
        _M0L1xS3151 = _M0L4varsS894->$3;
        _M0L3valS3152 = _M0L1jS897->$0;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3150
        = _M0MPC15array5Array2atGfE(_M0L1xS3151, _M0L3valS3152);
        _M0L6_2atmpS3148 = 0x1p+0f - _M0L6_2atmpS3150;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3149 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__dS906);
        _M0L6_2atmpS3147 = _M0L6_2atmpS3148 * _M0L6_2atmpS3149;
        _M0L6_2atmpS3146 = 0x1p+0f - _M0L6_2atmpS3147;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3144, _M0L3valS3145, _M0L6_2atmpS3146);
        _M0L8rho__preS3153 = _M0L4varsS894->$4;
        _M0L3valS3154 = _M0L1jS897->$0;
        _M0L1uS3160 = _M0L4varsS894->$2;
        _M0L3valS3161 = _M0L1jS897->$0;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3156
        = _M0MPC15array5Array2atGfE(_M0L1uS3160, _M0L3valS3161);
        _M0L1xS3158 = _M0L4varsS894->$3;
        _M0L3valS3159 = _M0L1jS897->$0;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3157
        = _M0MPC15array5Array2atGfE(_M0L1xS3158, _M0L3valS3159);
        _M0L6_2atmpS3155 = _M0L6_2atmpS3156 * _M0L6_2atmpS3157;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L8rho__preS3153, _M0L3valS3154, _M0L6_2atmpS3155);
        _M0L6matrixS3201 = _M0L3synS896->$4;
        _M0L6rowptrS3199 = _M0L6matrixS3201->$2;
        _M0L3valS3200 = _M0L1jS897->$0;
        #line 374 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L5startS907
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3199, _M0L3valS3200);
        _M0L6matrixS3198 = _M0L3synS896->$4;
        _M0L6rowptrS3195 = _M0L6matrixS3198->$2;
        _M0L3valS3197 = _M0L1jS897->$0;
        _M0L6_2atmpS3196 = _M0L3valS3197 + 1;
        #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L3endS908
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3195, _M0L6_2atmpS3196);
        _M0L1sS909
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS909)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS909->$0 = _M0L5startS907;
        while (1) {
          int32_t _M0L3valS3162 = _M0L1sS909->$0;
          if (_M0L3valS3162 < _M0L3endS908) {
            struct _M0TPB5ArrayGfE* _M0L3rhoS3163 = _M0L3synS896->$6;
            int32_t _M0L3valS3164 = _M0L1sS909->$0;
            struct _M0TPB5ArrayGfE* _M0L8rho__preS3166 = _M0L4varsS894->$4;
            int32_t _M0L3valS3167 = _M0L1jS897->$0;
            float _M0L6_2atmpS3165;
            int32_t _M0L3valS3169;
            int32_t _M0L6_2atmpS3168;
            #line 378 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0L6_2atmpS3165
            = _M0MPC15array5Array2atGfE(_M0L8rho__preS3166, _M0L3valS3167);
            #line 378 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0MPC15array5Array3setGfE(_M0L3rhoS3163, _M0L3valS3164, _M0L6_2atmpS3165);
            _M0L3valS3169 = _M0L1sS909->$0;
            _M0L6_2atmpS3168 = _M0L3valS3169 + 1;
            _M0L1sS909->$0 = _M0L6_2atmpS3168;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS909);
          }
          break;
        }
        _M0L1uS3170 = _M0L4varsS894->$2;
        _M0L3valS3171 = _M0L1jS897->$0;
        _M0L1uS3179 = _M0L4varsS894->$2;
        _M0L3valS3180 = _M0L1jS897->$0;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3173
        = _M0MPC15array5Array2atGfE(_M0L1uS3179, _M0L3valS3180);
        _M0L1uS3177 = _M0L4varsS894->$2;
        _M0L3valS3178 = _M0L1jS897->$0;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3176
        = _M0MPC15array5Array2atGfE(_M0L1uS3177, _M0L3valS3178);
        _M0L6_2atmpS3175 = 0x1p+0f - _M0L6_2atmpS3176;
        _M0L6_2atmpS3174 = _M0L14u__baseline__jS901 * _M0L6_2atmpS3175;
        _M0L6_2atmpS3172 = _M0L6_2atmpS3173 + _M0L6_2atmpS3174;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3170, _M0L3valS3171, _M0L6_2atmpS3172);
        _M0L1xS3181 = _M0L4varsS894->$3;
        _M0L3valS3182 = _M0L1jS897->$0;
        _M0L1xS3193 = _M0L4varsS894->$3;
        _M0L3valS3194 = _M0L1jS897->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3184
        = _M0MPC15array5Array2atGfE(_M0L1xS3193, _M0L3valS3194);
        _M0L1uS3191 = _M0L4varsS894->$2;
        _M0L3valS3192 = _M0L1jS897->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3190
        = _M0MPC15array5Array2atGfE(_M0L1uS3191, _M0L3valS3192);
        _M0L6_2atmpS3186 = -_M0L6_2atmpS3190;
        _M0L1xS3188 = _M0L4varsS894->$3;
        _M0L3valS3189 = _M0L1jS897->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3187
        = _M0MPC15array5Array2atGfE(_M0L1xS3188, _M0L3valS3189);
        _M0L6_2atmpS3185 = _M0L6_2atmpS3186 * _M0L6_2atmpS3187;
        _M0L6_2atmpS3183 = _M0L6_2atmpS3184 + _M0L6_2atmpS3185;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3181, _M0L3valS3182, _M0L6_2atmpS3183);
      }
      _M0L3valS3214 = _M0L1jS897->$0;
      _M0L6_2atmpS3213 = _M0L3valS3214 + 1;
      _M0L1jS897->$0 = _M0L6_2atmpS3213;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS897);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt18markram__stp__step(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS878,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS876,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter* _M0L5paramS880,
  float _M0L6t__nowS885
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3040;
  int32_t _M0L6_2atmpS3039;
  int32_t _if__result_6085;
  int32_t _M0L6n__preS877;
  struct _M0TPB5ArrayGfE* _M0L3rhoS3042;
  int32_t _M0L6_2atmpS3041;
  float _M0L6tau__fS879;
  float _M0L6tau__dS881;
  float _M0L11u__baselineS882;
  struct _M0TPB8MutLocalGiE* _M0L1jS883;
  #line 202 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS3040 = _M0L4varsS876->$6;
  #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3039 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3040);
  if (_M0L6_2atmpS3039 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3038 = _M0L4varsS876->$6;
    int32_t _M0L6_2atmpS3037;
    #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS3037 = _M0MPC15array5Array2atGbE(_M0L6activeS3038, 0);
    _if__result_6085 = !_M0L6_2atmpS3037;
  } else {
    _if__result_6085 = 0;
  }
  if (_if__result_6085) {
    return 0;
  }
  _M0L6n__preS877 = _M0L4varsS876->$0;
  _M0L3rhoS3042 = _M0L3synS878->$6;
  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3041 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS3042);
  if (_M0L6_2atmpS3041 == 0) {
    return 0;
  }
  _M0L6tau__fS879 = _M0L5paramS880->$1;
  _M0L6tau__dS881 = _M0L5paramS880->$0;
  _M0L11u__baselineS882 = _M0L5paramS880->$2;
  _M0L1jS883
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS883)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS883->$0 = 0;
  while (1) {
    int32_t _M0L3valS3043 = _M0L1jS883->$0;
    if (_M0L3valS3043 < _M0L6n__preS877) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3046 = _M0L3synS878->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3044 = _M0L3preS3046->$5;
      int32_t _M0L3valS3045 = _M0L1jS883->$0;
      int32_t _M0L3valS3122;
      int32_t _M0L6_2atmpS3121;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3044, _M0L3valS3045)) {
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS3119 = _M0L4varsS876->$5;
        int32_t _M0L3valS3120 = _M0L1jS883->$0;
        float _M0L6_2atmpS3118;
        float _M0L7dt__preS884;
        float _M0L7dt__preS886;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS3047;
        int32_t _M0L3valS3048;
        float _M0L6_2atmpS3117;
        float _M0L6arg__fS887;
        struct _M0TPB5ArrayGfE* _M0L1uS3049;
        int32_t _M0L3valS3050;
        struct _M0TPB5ArrayGfE* _M0L1uS3056;
        int32_t _M0L3valS3057;
        float _M0L6_2atmpS3055;
        float _M0L6_2atmpS3053;
        float _M0L6_2atmpS3054;
        float _M0L6_2atmpS3052;
        float _M0L6_2atmpS3051;
        float _M0L6_2atmpS3116;
        float _M0L6arg__dS888;
        struct _M0TPB5ArrayGfE* _M0L1xS3058;
        int32_t _M0L3valS3059;
        struct _M0TPB5ArrayGfE* _M0L1xS3065;
        int32_t _M0L3valS3066;
        float _M0L6_2atmpS3064;
        float _M0L6_2atmpS3062;
        float _M0L6_2atmpS3063;
        float _M0L6_2atmpS3061;
        float _M0L6_2atmpS3060;
        struct _M0TPB5ArrayGfE* _M0L8rho__preS3067;
        int32_t _M0L3valS3068;
        struct _M0TPB5ArrayGfE* _M0L1uS3074;
        int32_t _M0L3valS3075;
        float _M0L6_2atmpS3070;
        struct _M0TPB5ArrayGfE* _M0L1xS3072;
        int32_t _M0L3valS3073;
        float _M0L6_2atmpS3071;
        float _M0L6_2atmpS3069;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3115;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3113;
        int32_t _M0L3valS3114;
        int32_t _M0L5startS889;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3112;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3109;
        int32_t _M0L3valS3111;
        int32_t _M0L6_2atmpS3110;
        int32_t _M0L3endS890;
        struct _M0TPB8MutLocalGiE* _M0L1sS891;
        struct _M0TPB5ArrayGfE* _M0L1uS3084;
        int32_t _M0L3valS3085;
        struct _M0TPB5ArrayGfE* _M0L1uS3093;
        int32_t _M0L3valS3094;
        float _M0L6_2atmpS3087;
        struct _M0TPB5ArrayGfE* _M0L1uS3091;
        int32_t _M0L3valS3092;
        float _M0L6_2atmpS3090;
        float _M0L6_2atmpS3089;
        float _M0L6_2atmpS3088;
        float _M0L6_2atmpS3086;
        struct _M0TPB5ArrayGfE* _M0L1xS3095;
        int32_t _M0L3valS3096;
        struct _M0TPB5ArrayGfE* _M0L1xS3107;
        int32_t _M0L3valS3108;
        float _M0L6_2atmpS3098;
        struct _M0TPB5ArrayGfE* _M0L1uS3105;
        int32_t _M0L3valS3106;
        float _M0L6_2atmpS3104;
        float _M0L6_2atmpS3100;
        struct _M0TPB5ArrayGfE* _M0L1xS3102;
        int32_t _M0L3valS3103;
        float _M0L6_2atmpS3101;
        float _M0L6_2atmpS3099;
        float _M0L6_2atmpS3097;
        #line 224 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3118
        = _M0MPC15array5Array2atGfE(_M0L11last__spikeS3119, _M0L3valS3120);
        _M0L7dt__preS884 = _M0L6t__nowS885 - _M0L6_2atmpS3118;
        if (_M0L7dt__preS884 < 0x0p+0f) {
          _M0L7dt__preS886 = 0x0p+0f;
        } else {
          _M0L7dt__preS886 = _M0L7dt__preS884;
        }
        _M0L11last__spikeS3047 = _M0L4varsS876->$5;
        _M0L3valS3048 = _M0L1jS883->$0;
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L11last__spikeS3047, _M0L3valS3048, _M0L6t__nowS885);
        _M0L6_2atmpS3117 = -_M0L7dt__preS886;
        _M0L6arg__fS887 = _M0L6_2atmpS3117 / _M0L6tau__fS879;
        _M0L1uS3049 = _M0L4varsS876->$2;
        _M0L3valS3050 = _M0L1jS883->$0;
        _M0L1uS3056 = _M0L4varsS876->$2;
        _M0L3valS3057 = _M0L1jS883->$0;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3055
        = _M0MPC15array5Array2atGfE(_M0L1uS3056, _M0L3valS3057);
        _M0L6_2atmpS3053 = _M0L11u__baselineS882 - _M0L6_2atmpS3055;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3054 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__fS887);
        _M0L6_2atmpS3052 = _M0L6_2atmpS3053 * _M0L6_2atmpS3054;
        _M0L6_2atmpS3051 = _M0L11u__baselineS882 - _M0L6_2atmpS3052;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3049, _M0L3valS3050, _M0L6_2atmpS3051);
        _M0L6_2atmpS3116 = -_M0L7dt__preS886;
        _M0L6arg__dS888 = _M0L6_2atmpS3116 / _M0L6tau__dS881;
        _M0L1xS3058 = _M0L4varsS876->$3;
        _M0L3valS3059 = _M0L1jS883->$0;
        _M0L1xS3065 = _M0L4varsS876->$3;
        _M0L3valS3066 = _M0L1jS883->$0;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3064
        = _M0MPC15array5Array2atGfE(_M0L1xS3065, _M0L3valS3066);
        _M0L6_2atmpS3062 = 0x1p+0f - _M0L6_2atmpS3064;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3063 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__dS888);
        _M0L6_2atmpS3061 = _M0L6_2atmpS3062 * _M0L6_2atmpS3063;
        _M0L6_2atmpS3060 = 0x1p+0f - _M0L6_2atmpS3061;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3058, _M0L3valS3059, _M0L6_2atmpS3060);
        _M0L8rho__preS3067 = _M0L4varsS876->$4;
        _M0L3valS3068 = _M0L1jS883->$0;
        _M0L1uS3074 = _M0L4varsS876->$2;
        _M0L3valS3075 = _M0L1jS883->$0;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3070
        = _M0MPC15array5Array2atGfE(_M0L1uS3074, _M0L3valS3075);
        _M0L1xS3072 = _M0L4varsS876->$3;
        _M0L3valS3073 = _M0L1jS883->$0;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3071
        = _M0MPC15array5Array2atGfE(_M0L1xS3072, _M0L3valS3073);
        _M0L6_2atmpS3069 = _M0L6_2atmpS3070 * _M0L6_2atmpS3071;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L8rho__preS3067, _M0L3valS3068, _M0L6_2atmpS3069);
        _M0L6matrixS3115 = _M0L3synS878->$4;
        _M0L6rowptrS3113 = _M0L6matrixS3115->$2;
        _M0L3valS3114 = _M0L1jS883->$0;
        #line 236 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L5startS889
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3113, _M0L3valS3114);
        _M0L6matrixS3112 = _M0L3synS878->$4;
        _M0L6rowptrS3109 = _M0L6matrixS3112->$2;
        _M0L3valS3111 = _M0L1jS883->$0;
        _M0L6_2atmpS3110 = _M0L3valS3111 + 1;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L3endS890
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3109, _M0L6_2atmpS3110);
        _M0L1sS891
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS891)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS891->$0 = _M0L5startS889;
        while (1) {
          int32_t _M0L3valS3076 = _M0L1sS891->$0;
          if (_M0L3valS3076 < _M0L3endS890) {
            struct _M0TPB5ArrayGfE* _M0L3rhoS3077 = _M0L3synS878->$6;
            int32_t _M0L3valS3078 = _M0L1sS891->$0;
            struct _M0TPB5ArrayGfE* _M0L8rho__preS3080 = _M0L4varsS876->$4;
            int32_t _M0L3valS3081 = _M0L1jS883->$0;
            float _M0L6_2atmpS3079;
            int32_t _M0L3valS3083;
            int32_t _M0L6_2atmpS3082;
            #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0L6_2atmpS3079
            = _M0MPC15array5Array2atGfE(_M0L8rho__preS3080, _M0L3valS3081);
            #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0MPC15array5Array3setGfE(_M0L3rhoS3077, _M0L3valS3078, _M0L6_2atmpS3079);
            _M0L3valS3083 = _M0L1sS891->$0;
            _M0L6_2atmpS3082 = _M0L3valS3083 + 1;
            _M0L1sS891->$0 = _M0L6_2atmpS3082;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS891);
          }
          break;
        }
        _M0L1uS3084 = _M0L4varsS876->$2;
        _M0L3valS3085 = _M0L1jS883->$0;
        _M0L1uS3093 = _M0L4varsS876->$2;
        _M0L3valS3094 = _M0L1jS883->$0;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3087
        = _M0MPC15array5Array2atGfE(_M0L1uS3093, _M0L3valS3094);
        _M0L1uS3091 = _M0L4varsS876->$2;
        _M0L3valS3092 = _M0L1jS883->$0;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3090
        = _M0MPC15array5Array2atGfE(_M0L1uS3091, _M0L3valS3092);
        _M0L6_2atmpS3089 = 0x1p+0f - _M0L6_2atmpS3090;
        _M0L6_2atmpS3088 = _M0L11u__baselineS882 * _M0L6_2atmpS3089;
        _M0L6_2atmpS3086 = _M0L6_2atmpS3087 + _M0L6_2atmpS3088;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3084, _M0L3valS3085, _M0L6_2atmpS3086);
        _M0L1xS3095 = _M0L4varsS876->$3;
        _M0L3valS3096 = _M0L1jS883->$0;
        _M0L1xS3107 = _M0L4varsS876->$3;
        _M0L3valS3108 = _M0L1jS883->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3098
        = _M0MPC15array5Array2atGfE(_M0L1xS3107, _M0L3valS3108);
        _M0L1uS3105 = _M0L4varsS876->$2;
        _M0L3valS3106 = _M0L1jS883->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3104
        = _M0MPC15array5Array2atGfE(_M0L1uS3105, _M0L3valS3106);
        _M0L6_2atmpS3100 = -_M0L6_2atmpS3104;
        _M0L1xS3102 = _M0L4varsS876->$3;
        _M0L3valS3103 = _M0L1jS883->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3101
        = _M0MPC15array5Array2atGfE(_M0L1xS3102, _M0L3valS3103);
        _M0L6_2atmpS3099 = _M0L6_2atmpS3100 * _M0L6_2atmpS3101;
        _M0L6_2atmpS3097 = _M0L6_2atmpS3098 + _M0L6_2atmpS3099;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3095, _M0L3valS3096, _M0L6_2atmpS3097);
      }
      _M0L3valS3122 = _M0L1jS883->$0;
      _M0L6_2atmpS3121 = _M0L3valS3122 + 1;
      _M0L1jS883->$0 = _M0L6_2atmpS3121;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS883);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12update__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS874,
  float _M0L2dtS875
) {
  struct _M0TPB5ArrayGfE* _M0L1tS3029;
  struct _M0TPB5ArrayGfE* _M0L1tS3032;
  float _M0L6_2atmpS3031;
  float _M0L6_2atmpS3030;
  struct _M0TPB5ArrayGiE* _M0L2ttS3033;
  struct _M0TPB5ArrayGiE* _M0L2ttS3036;
  int32_t _M0L6_2atmpS3035;
  int32_t _M0L6_2atmpS3034;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS3029 = _M0L1tS874->$0;
  _M0L1tS3032 = _M0L1tS874->$0;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS3031 = _M0MPC15array5Array2atGfE(_M0L1tS3032, 0);
  _M0L6_2atmpS3030 = _M0L6_2atmpS3031 + _M0L2dtS875;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGfE(_M0L1tS3029, 0, _M0L6_2atmpS3030);
  _M0L2ttS3033 = _M0L1tS874->$1;
  _M0L2ttS3036 = _M0L1tS874->$1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS3035 = _M0MPC15array5Array2atGiE(_M0L2ttS3036, 0);
  _M0L6_2atmpS3034 = _M0L6_2atmpS3035 + 1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGiE(_M0L2ttS3033, 0, _M0L6_2atmpS3034);
  return 0;
}

float _M0FP26RiantR8snn__mbt9get__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS873
) {
  struct _M0TPB5ArrayGfE* _M0L1tS3028;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS3028 = _M0L1tS873->$0;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  return _M0MPC15array5Array2atGfE(_M0L1tS3028, 0);
}

struct _M0TP26RiantR8snn__mbt4Time* _M0MP26RiantR8snn__mbt4Time3new() {
  float* _M0L6_2atmpS3027;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3024;
  int32_t* _M0L6_2atmpS3026;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS3025;
  struct _M0TP26RiantR8snn__mbt4Time* _block_6088;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS3027 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS3027[0] = 0x0p+0f;
  _M0L6_2atmpS3024
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS3024)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS3024->$0 = _M0L6_2atmpS3027;
  _M0L6_2atmpS3024->$1 = 1;
  _M0L6_2atmpS3026 = (int32_t*)moonbit_make_int32_array_raw(1);
  _M0L6_2atmpS3026[0] = 0;
  _M0L6_2atmpS3025
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS3025)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _M0L6_2atmpS3025->$0 = _M0L6_2atmpS3026;
  _M0L6_2atmpS3025->$1 = 1;
  _block_6088
  = (struct _M0TP26RiantR8snn__mbt4Time*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt4Time));
  Moonbit_object_header(_block_6088)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 98, 0);
  _block_6088->$0 = _M0L6_2atmpS3024;
  _block_6088->$1 = _M0L6_2atmpS3025;
  _block_6088->$2 = 0x1p-3f;
  return _block_6088;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS871
) {
  uint32_t _M0L1uS870;
  uint32_t _M0L4bitsS872;
  double _M0L6_2atmpS3023;
  double _M0L6_2atmpS3022;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS870 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS871);
  _M0L4bitsS872 = _M0L1uS870 >> 8;
  _M0L6_2atmpS3023 = (double)_M0L4bitsS872;
  _M0L6_2atmpS3022 = _M0L6_2atmpS3023 * 0x1p-24;
  return (float)_M0L6_2atmpS3022;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS869
) {
  uint64_t _M0L1uS868;
  uint64_t _M0L6_2atmpS3021;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS868 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS869);
  _M0L6_2atmpS3021 = _M0L1uS868 >> 32;
  return (uint32_t)_M0L6_2atmpS3021;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS861
) {
  uint64_t _M0L2s0S860;
  uint64_t _M0L2s1S862;
  uint64_t _M0L2s2S863;
  uint64_t _M0L2s3S864;
  uint64_t _M0L3tmpS865;
  uint64_t _M0L6_2atmpS3020;
  uint64_t _M0L3resS866;
  uint64_t _M0L1tS867;
  uint64_t _M0L6_2atmpS3010;
  uint64_t _M0L6_2atmpS3011;
  uint64_t _M0L2s2S3013;
  uint64_t _M0L6_2atmpS3012;
  uint64_t _M0L2s3S3015;
  uint64_t _M0L6_2atmpS3014;
  uint64_t _M0L2s2S3017;
  uint64_t _M0L6_2atmpS3016;
  uint64_t _M0L2s3S3019;
  uint64_t _M0L6_2atmpS3018;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S860 = _M0L1rS861->$0;
  _M0L2s1S862 = _M0L1rS861->$1;
  _M0L2s2S863 = _M0L1rS861->$2;
  _M0L2s3S864 = _M0L1rS861->$3;
  _M0L3tmpS865 = _M0L2s0S860 + _M0L2s3S864;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS3020 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS865, 23);
  _M0L3resS866 = _M0L6_2atmpS3020 + _M0L2s0S860;
  _M0L1tS867 = _M0L2s1S862 << 17;
  _M0L6_2atmpS3010 = _M0L2s2S863 ^ _M0L2s0S860;
  _M0L1rS861->$2 = _M0L6_2atmpS3010;
  _M0L6_2atmpS3011 = _M0L2s3S864 ^ _M0L2s1S862;
  _M0L1rS861->$3 = _M0L6_2atmpS3011;
  _M0L2s2S3013 = _M0L1rS861->$2;
  _M0L6_2atmpS3012 = _M0L2s1S862 ^ _M0L2s2S3013;
  _M0L1rS861->$1 = _M0L6_2atmpS3012;
  _M0L2s3S3015 = _M0L1rS861->$3;
  _M0L6_2atmpS3014 = _M0L2s0S860 ^ _M0L2s3S3015;
  _M0L1rS861->$0 = _M0L6_2atmpS3014;
  _M0L2s2S3017 = _M0L1rS861->$2;
  _M0L6_2atmpS3016 = _M0L2s2S3017 ^ _M0L1tS867;
  _M0L1rS861->$2 = _M0L6_2atmpS3016;
  _M0L2s3S3019 = _M0L1rS861->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS3018 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S3019, 45);
  _M0L1rS861->$3 = _M0L6_2atmpS3018;
  return _M0L3resS866;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS858, int32_t _M0L1kS859) {
  uint64_t _M0L6_2atmpS3007;
  int32_t _M0L6_2atmpS3009;
  uint64_t _M0L6_2atmpS3008;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS3007 = _M0L1xS858 << (_M0L1kS859 & 63);
  _M0L6_2atmpS3009 = 64 - _M0L1kS859;
  _M0L6_2atmpS3008 = _M0L1xS858 >> (_M0L6_2atmpS3009 & 63);
  return _M0L6_2atmpS3007 | _M0L6_2atmpS3008;
}

int32_t _M0MP26RiantR8snn__mbt7Monitor13count__spikes(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS851
) {
  struct _M0TPB5ArrayGfE* _M0L4dataS3006;
  int32_t _M0L1nS850;
  struct _M0TPB8MutLocalGiE* _M0L5countS852;
  struct _M0TPB8MutLocalGfE* _M0L4prevS853;
  int32_t _M0L7_2abindS854;
  int32_t _M0L1iS855;
  int32_t _result_6090;
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4dataS3006 = _M0L1mS851->$2;
  #line 18 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L1nS850 = _M0MPC15array5Array6lengthGfE(_M0L4dataS3006);
  if (_M0L1nS850 == 0) {
    return 0;
  }
  _M0L5countS852
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5countS852)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5countS852->$0 = 0;
  _M0L4prevS853
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L4prevS853)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4prevS853->$0 = 0x0p+0f;
  _M0L7_2abindS854 = 0;
  _M0L1iS855 = _M0L7_2abindS854;
  while (1) {
    if (_M0L1iS855 < _M0L1nS850) {
      struct _M0TPB5ArrayGfE* _M0L4dataS3004 = _M0L1mS851->$2;
      float _M0L3curS856;
      float _M0L3valS3001;
      int32_t _M0L6_2atmpS3005;
      #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L3curS856 = _M0MPC15array5Array2atGfE(_M0L4dataS3004, _M0L1iS855);
      _M0L3valS3001 = _M0L4prevS853->$0;
      if (_M0L3valS3001 < 0x1p-1f && _M0L3curS856 >= 0x1p-1f) {
        int32_t _M0L3valS3003 = _M0L5countS852->$0;
        int32_t _M0L6_2atmpS3002 = _M0L3valS3003 + 1;
        _M0L5countS852->$0 = _M0L6_2atmpS3002;
      }
      _M0L4prevS853->$0 = _M0L3curS856;
      _M0L6_2atmpS3005 = _M0L1iS855 + 1;
      _M0L1iS855 = _M0L6_2atmpS3005;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4prevS853);
    }
    break;
  }
  _result_6090 = _M0L5countS852->$0;
  moonbit_decref_cycle_free(_M0L5countS852);
  return _result_6090;
}

double _M0FPC14math2ln(double _M0L1xS836) {
  struct _M0TUdiE* _M0L7_2abindS837;
  double _M0L5_2af1S838;
  int32_t _M0L5_2akiS839;
  double _M0L1fS841;
  double _M0L1kS842;
  double _M0L6_2atmpS2994;
  double _M0L1sS843;
  double _M0L2s2S844;
  double _M0L2s4S845;
  double _M0L6_2atmpS2993;
  double _M0L6_2atmpS2992;
  double _M0L6_2atmpS2991;
  double _M0L6_2atmpS2990;
  double _M0L6_2atmpS2989;
  double _M0L6_2atmpS2988;
  double _M0L2t1S846;
  double _M0L6_2atmpS2987;
  double _M0L6_2atmpS2986;
  double _M0L6_2atmpS2985;
  double _M0L6_2atmpS2984;
  double _M0L2t2S847;
  double _M0L1rS848;
  double _M0L6_2atmpS2983;
  double _M0L4hfsqS849;
  double _M0L6_2atmpS2976;
  double _M0L6_2atmpS2982;
  double _M0L6_2atmpS2980;
  double _M0L6_2atmpS2981;
  double _M0L6_2atmpS2979;
  double _M0L6_2atmpS2978;
  double _M0L6_2atmpS2977;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS836 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS836)
      || _M0MPC16double6Double7is__inf(_M0L1xS836)
    ) {
      return _M0L1xS836;
    } else if (_M0L1xS836 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS837 = _M0FPC14math5frexp(_M0L1xS836);
  _M0L5_2af1S838 = _M0L7_2abindS837->$0;
  _M0L5_2akiS839 = _M0L7_2abindS837->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS837);
  if (_M0L5_2af1S838 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS2998 = _M0L5_2af1S838 * 0x1p+1;
    double _M0L6_2atmpS2995 = _M0L6_2atmpS2998 - 0x1p+0;
    int32_t _M0L6_2atmpS2997 = _M0L5_2akiS839 - 1;
    double _M0L6_2atmpS2996 = (double)_M0L6_2atmpS2997;
    _M0L1fS841 = _M0L6_2atmpS2995;
    _M0L1kS842 = _M0L6_2atmpS2996;
    goto join_840;
  } else {
    double _M0L6_2atmpS2999 = _M0L5_2af1S838 - 0x1p+0;
    double _M0L6_2atmpS3000 = (double)_M0L5_2akiS839;
    _M0L1fS841 = _M0L6_2atmpS2999;
    _M0L1kS842 = _M0L6_2atmpS3000;
    goto join_840;
  }
  join_840:;
  _M0L6_2atmpS2994 = 0x1p+1 + _M0L1fS841;
  _M0L1sS843 = _M0L1fS841 / _M0L6_2atmpS2994;
  _M0L2s2S844 = _M0L1sS843 * _M0L1sS843;
  _M0L2s4S845 = _M0L2s2S844 * _M0L2s2S844;
  _M0L6_2atmpS2993 = _M0L2s4S845 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS2992 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS2993;
  _M0L6_2atmpS2991 = _M0L2s4S845 * _M0L6_2atmpS2992;
  _M0L6_2atmpS2990 = 0x1.2492494229359p-2 + _M0L6_2atmpS2991;
  _M0L6_2atmpS2989 = _M0L2s4S845 * _M0L6_2atmpS2990;
  _M0L6_2atmpS2988 = 0x1.5555555555593p-1 + _M0L6_2atmpS2989;
  _M0L2t1S846 = _M0L2s2S844 * _M0L6_2atmpS2988;
  _M0L6_2atmpS2987 = _M0L2s4S845 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS2986 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS2987;
  _M0L6_2atmpS2985 = _M0L2s4S845 * _M0L6_2atmpS2986;
  _M0L6_2atmpS2984 = 0x1.999999997fa04p-2 + _M0L6_2atmpS2985;
  _M0L2t2S847 = _M0L2s4S845 * _M0L6_2atmpS2984;
  _M0L1rS848 = _M0L2t1S846 + _M0L2t2S847;
  _M0L6_2atmpS2983 = 0x1p-1 * _M0L1fS841;
  _M0L4hfsqS849 = _M0L6_2atmpS2983 * _M0L1fS841;
  _M0L6_2atmpS2976 = _M0L1kS842 * 0x1.62e42feep-1;
  _M0L6_2atmpS2982 = _M0L4hfsqS849 + _M0L1rS848;
  _M0L6_2atmpS2980 = _M0L1sS843 * _M0L6_2atmpS2982;
  _M0L6_2atmpS2981 = _M0L1kS842 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS2979 = _M0L6_2atmpS2980 + _M0L6_2atmpS2981;
  _M0L6_2atmpS2978 = _M0L4hfsqS849 - _M0L6_2atmpS2979;
  _M0L6_2atmpS2977 = _M0L6_2atmpS2978 - _M0L1fS841;
  return _M0L6_2atmpS2976 - _M0L6_2atmpS2977;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS829) {
  struct _M0TUdiE* _M0L7_2abindS830;
  double _M0L10_2anorm__fS831;
  int32_t _M0L6_2aexpS832;
  uint64_t _M0L1uS833;
  uint64_t _M0L6_2atmpS2975;
  uint64_t _M0L6_2atmpS2974;
  int32_t _M0L6_2atmpS2973;
  int32_t _M0L6_2atmpS2972;
  int32_t _M0L3expS834;
  uint64_t _M0L6_2atmpS2971;
  uint64_t _M0L6_2atmpS2970;
  uint64_t _M0L6_2atmpS2969;
  double _M0L4fracS835;
  struct _M0TUdiE* _block_6093;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS829 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS829)
    || _M0MPC16double6Double7is__nan(_M0L1fS829)
  ) {
    struct _M0TUdiE* _block_6092 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_6092)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_6092->$0 = _M0L1fS829;
    _block_6092->$1 = 0;
    return _block_6092;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS830 = _M0FPC14math9normalize(_M0L1fS829);
  _M0L10_2anorm__fS831 = _M0L7_2abindS830->$0;
  _M0L6_2aexpS832 = _M0L7_2abindS830->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS830);
  _M0L1uS833 = *(int64_t*)&_M0L10_2anorm__fS831;
  _M0L6_2atmpS2975 = _M0L1uS833 >> 52;
  _M0L6_2atmpS2974 = _M0L6_2atmpS2975 & 2047ull;
  _M0L6_2atmpS2973 = (int32_t)_M0L6_2atmpS2974;
  _M0L6_2atmpS2972 = _M0L6_2aexpS832 + _M0L6_2atmpS2973;
  _M0L3expS834 = _M0L6_2atmpS2972 - 1022;
  _M0L6_2atmpS2971 = ~9218868437227405312ull;
  _M0L6_2atmpS2970 = _M0L1uS833 & _M0L6_2atmpS2971;
  _M0L6_2atmpS2969 = _M0L6_2atmpS2970 | 4602678819172646912ull;
  _M0L4fracS835 = *(double*)&_M0L6_2atmpS2969;
  _block_6093 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_6093)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6093->$0 = _M0L4fracS835;
  _block_6093->$1 = _M0L3expS834;
  return _block_6093;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS828) {
  double _M0L6_2atmpS2966;
  struct _M0TUdiE* _block_6095;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS2966 = fabs(_M0L1fS828);
  if (_M0L6_2atmpS2966 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS2968 = (double)4503599627370496ll;
    double _M0L6_2atmpS2967 = _M0L1fS828 * _M0L6_2atmpS2968;
    struct _M0TUdiE* _block_6094 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_6094)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_6094->$0 = _M0L6_2atmpS2967;
    _block_6094->$1 = -52;
    return _block_6094;
  }
  _block_6095 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_6095)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6095->$0 = _M0L1fS828;
  _block_6095->$1 = 0;
  return _block_6095;
}

int32_t _M0MPC15float5Float7is__nan(float _M0L4selfS827) {
  #line 208 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0L4selfS827 != _M0L4selfS827;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS826) {
  double _M0L6_2atmpS2965;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2965 = (double)_M0L4selfS826;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2965);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS825) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS825 != _M0L4selfS825) {
    return 0;
  } else if (_M0L4selfS825 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS825 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS825;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS806,
  float _M0L4elemS808
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS805;
  int32_t _M0L1iS807;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS805 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS806);
  _M0L1iS807 = 0;
  while (1) {
    if (_M0L1iS807 < _M0L3lenS806) {
      float* _M0L3bufS2957 = _M0L3arrS805->$0;
      int32_t _M0L6_2atmpS2958;
      _M0L3bufS2957[_M0L1iS807] = _M0L4elemS808;
      _M0L6_2atmpS2958 = _M0L1iS807 + 1;
      _M0L1iS807 = _M0L6_2atmpS2958;
      continue;
    }
    break;
  }
  return _M0L3arrS805;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS811,
  int32_t _M0L4elemS813
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS810;
  int32_t _M0L1iS812;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS810 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS811);
  _M0L1iS812 = 0;
  while (1) {
    if (_M0L1iS812 < _M0L3lenS811) {
      uint8_t* _M0L3bufS2959 = _M0L3arrS810->$0;
      int32_t _M0L6_2atmpS2960;
      _M0L3bufS2959[_M0L1iS812] = _M0L4elemS813;
      _M0L6_2atmpS2960 = _M0L1iS812 + 1;
      _M0L1iS812 = _M0L6_2atmpS2960;
      continue;
    }
    break;
  }
  return _M0L3arrS810;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS816,
  int32_t _M0L4elemS818
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS815;
  int32_t _M0L1iS817;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS815 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS816);
  _M0L1iS817 = 0;
  while (1) {
    if (_M0L1iS817 < _M0L3lenS816) {
      int32_t* _M0L3bufS2961 = _M0L3arrS815->$0;
      int32_t _M0L6_2atmpS2962;
      _M0L3bufS2961[_M0L1iS817] = _M0L4elemS818;
      _M0L6_2atmpS2962 = _M0L1iS817 + 1;
      _M0L1iS817 = _M0L6_2atmpS2962;
      continue;
    }
    break;
  }
  return _M0L3arrS815;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS821,
  struct _M0TPB5ArrayGfE* _M0L4elemS823
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS820;
  int32_t _M0L1iS822;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS820
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS821);
  _M0L1iS822 = 0;
  while (1) {
    if (_M0L1iS822 < _M0L3lenS821) {
      struct _M0TPB5ArrayGfE** _M0L3bufS2963 = _M0L3arrS820->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS5672 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS2963[_M0L1iS822];
      int32_t _M0L6_2atmpS2964;
      moonbit_incref_cycle_free(_M0L4elemS823);
      if (_M0L6_2aoldS5672) {
        moonbit_decref_cycle_free(_M0L6_2aoldS5672);
      }
      _M0L3bufS2963[_M0L1iS822] = _M0L4elemS823;
      _M0L6_2atmpS2964 = _M0L1iS822 + 1;
      _M0L1iS822 = _M0L6_2atmpS2964;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS823);
    }
    break;
  }
  return _M0L3arrS820;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS790,
  int32_t _M0L5indexS791,
  float _M0L5valueS792
) {
  int32_t _M0L3lenS789;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS789 = _M0L4selfS790->$1;
  if (_M0L5indexS791 >= 0 && _M0L5indexS791 < _M0L3lenS789) {
    float* _M0L6_2atmpS2953;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2953 = _M0MPC15array5Array6bufferGfE(_M0L4selfS790);
    _M0L6_2atmpS2953[_M0L5indexS791] = _M0L5valueS792;
    moonbit_decref_cycle_free(_M0L6_2atmpS2953);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS794,
  int32_t _M0L5indexS795,
  struct _M0TPB5ArrayGfE* _M0L5valueS796
) {
  int32_t _M0L3lenS793;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS793 = _M0L4selfS794->$1;
  if (_M0L5indexS795 >= 0 && _M0L5indexS795 < _M0L3lenS793) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2954;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS5673;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2954
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS794);
    _M0L6_2aoldS5673
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2954[_M0L5indexS795];
    if (_M0L6_2aoldS5673) {
      moonbit_decref_cycle_free(_M0L6_2aoldS5673);
    }
    _M0L6_2atmpS2954[_M0L5indexS795] = _M0L5valueS796;
    moonbit_decref_cycle_free(_M0L6_2atmpS2954);
  } else {
    moonbit_decref_cycle_free(_M0L5valueS796);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS798,
  int32_t _M0L5indexS799,
  int32_t _M0L5valueS800
) {
  int32_t _M0L3lenS797;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS797 = _M0L4selfS798->$1;
  if (_M0L5indexS799 >= 0 && _M0L5indexS799 < _M0L3lenS797) {
    int32_t* _M0L6_2atmpS2955;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2955 = _M0MPC15array5Array6bufferGiE(_M0L4selfS798);
    _M0L6_2atmpS2955[_M0L5indexS799] = _M0L5valueS800;
    moonbit_decref_cycle_free(_M0L6_2atmpS2955);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS802,
  int32_t _M0L5indexS803,
  int32_t _M0L5valueS804
) {
  int32_t _M0L3lenS801;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS801 = _M0L4selfS802->$1;
  if (_M0L5indexS803 >= 0 && _M0L5indexS803 < _M0L3lenS801) {
    uint8_t* _M0L6_2atmpS2956;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2956 = _M0MPC15array5Array6bufferGbE(_M0L4selfS802);
    _M0L6_2atmpS2956[_M0L5indexS803] = _M0L5valueS804;
    moonbit_decref_cycle_free(_M0L6_2atmpS2956);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE* _M0L4selfS782) {
  int32_t _M0L3lenS781;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS781 = _M0L4selfS782->$1;
  if (_M0L3lenS781 == 0) {
    return (struct moonbit_object*)&moonbit_constant_constructor_0 + 1;
  } else {
    int32_t _M0L5indexS783 = _M0L3lenS781 - 1;
    float* _M0L3bufS2951 = _M0L4selfS782->$0;
    float _M0L1vS784 = (float)_M0L3bufS2951[_M0L5indexS783];
    void* _block_6100;
    _M0L4selfS782->$1 = _M0L5indexS783;
    _block_6100
    = (void*)moonbit_malloc(sizeof(struct _M0DTPC16option6OptionGfE4Some));
    Moonbit_object_header(_block_6100)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 1);
    ((struct _M0DTPC16option6OptionGfE4Some*)_block_6100)->$0 = _M0L1vS784;
    return _block_6100;
  }
}

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE* _M0L4selfS786) {
  int32_t _M0L3lenS785;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS785 = _M0L4selfS786->$1;
  if (_M0L3lenS785 == 0) {
    return 4294967296ll;
  } else {
    int32_t _M0L5indexS787 = _M0L3lenS785 - 1;
    int32_t* _M0L3bufS2952 = _M0L4selfS786->$0;
    int32_t _M0L1vS788 = (int32_t)_M0L3bufS2952[_M0L5indexS787];
    _M0L4selfS786->$1 = _M0L5indexS787;
    return (int64_t)_M0L1vS788;
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS764,
  int32_t _M0L5indexS765
) {
  int32_t _M0L3lenS763;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS763 = _M0L4selfS764->$1;
  if (_M0L5indexS765 >= 0 && _M0L5indexS765 < _M0L3lenS763) {
    float* _M0L6_2atmpS2945;
    float _result_6101;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2945 = _M0MPC15array5Array6bufferGfE(_M0L4selfS764);
    _result_6101 = (float)_M0L6_2atmpS2945[_M0L5indexS765];
    moonbit_decref_cycle_free(_M0L6_2atmpS2945);
    return _result_6101;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS767,
  int32_t _M0L5indexS768
) {
  int32_t _M0L3lenS766;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS766 = _M0L4selfS767->$1;
  if (_M0L5indexS768 >= 0 && _M0L5indexS768 < _M0L3lenS766) {
    moonbit_string_t* _M0L6_2atmpS2946;
    moonbit_string_t _M0L6_2atmpS5674;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2946 = _M0MPC15array5Array6bufferGsE(_M0L4selfS767);
    _M0L6_2atmpS5674 = (moonbit_string_t)_M0L6_2atmpS2946[_M0L5indexS768];
    moonbit_incref_cycle_free(_M0L6_2atmpS5674);
    moonbit_decref_cycle_free(_M0L6_2atmpS2946);
    return _M0L6_2atmpS5674;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L4selfS770,
  int32_t _M0L5indexS771
) {
  int32_t _M0L3lenS769;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS769 = _M0L4selfS770->$1;
  if (_M0L5indexS771 >= 0 && _M0L5indexS771 < _M0L3lenS769) {
    struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L6_2atmpS2947;
    struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L6_2atmpS5675;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2947
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L4selfS770);
    _M0L6_2atmpS5675
    = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L6_2atmpS2947[
        _M0L5indexS771
      ];
    if (_M0L6_2atmpS5675) {
      moonbit_incref_cycle_free(_M0L6_2atmpS5675);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2947);
    return _M0L6_2atmpS5675;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS773,
  int32_t _M0L5indexS774
) {
  int32_t _M0L3lenS772;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS772 = _M0L4selfS773->$1;
  if (_M0L5indexS774 >= 0 && _M0L5indexS774 < _M0L3lenS772) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS2948;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS5676;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2948
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS773);
    _M0L6_2atmpS5676
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS2948[_M0L5indexS774];
    if (_M0L6_2atmpS5676) {
      moonbit_incref_cycle_free(_M0L6_2atmpS5676);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2948);
    return _M0L6_2atmpS5676;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS776,
  int32_t _M0L5indexS777
) {
  int32_t _M0L3lenS775;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS775 = _M0L4selfS776->$1;
  if (_M0L5indexS777 >= 0 && _M0L5indexS777 < _M0L3lenS775) {
    int32_t* _M0L6_2atmpS2949;
    int32_t _result_6102;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2949 = _M0MPC15array5Array6bufferGiE(_M0L4selfS776);
    _result_6102 = (int32_t)_M0L6_2atmpS2949[_M0L5indexS777];
    moonbit_decref_cycle_free(_M0L6_2atmpS2949);
    return _result_6102;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS779,
  int32_t _M0L5indexS780
) {
  int32_t _M0L3lenS778;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS778 = _M0L4selfS779->$1;
  if (_M0L5indexS780 >= 0 && _M0L5indexS780 < _M0L3lenS778) {
    uint8_t* _M0L6_2atmpS2950;
    int32_t _result_6103;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2950 = _M0MPC15array5Array6bufferGbE(_M0L4selfS779);
    _result_6103 = (int32_t)_M0L6_2atmpS2950[_M0L5indexS780];
    moonbit_decref_cycle_free(_M0L6_2atmpS2950);
    return _result_6103;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS762) {
  moonbit_string_t _M0L6_2atmpS2944;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2944 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS762);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2944);
  moonbit_decref_cycle_free(_M0L6_2atmpS2944);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS761) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS761);
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS760) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS760 > _M0FPB18double__max__value
         || _M0L4selfS760 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS759) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS759 != _M0L4selfS759;
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS744) {
  uint64_t _M0L4bitsS747;
  uint64_t _M0L6_2atmpS2943;
  uint64_t _M0L6_2atmpS2942;
  int32_t _M0L8ieeeSignS748;
  uint64_t _M0L12ieeeMantissaS749;
  uint64_t _M0L6_2atmpS2941;
  uint64_t _M0L6_2atmpS2940;
  int32_t _M0L12ieeeExponentS750;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS751;
  struct _M0TPB17FloatingDecimal64* _M0L1vS752;
  moonbit_string_t _result_6105;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS744 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_12.data;
  }
  if (_M0L3valS744 >= -0x1p+53 && _M0L3valS744 <= 0x1p+53) {
    if (_M0L3valS744 >= -0x1p+31 && _M0L3valS744 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS745;
      double _M0L6_2atmpS2929;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS745 = _M0MPC16double6Double7to__int(_M0L3valS744);
      _M0L6_2atmpS2929 = (double)_M0L1iS745;
      if (_M0L6_2atmpS2929 == _M0L3valS744) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS745, 10);
      }
    } else {
      int64_t _M0L1iS746;
      double _M0L6_2atmpS2930;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS746 = _M0MPC16double6Double9to__int64(_M0L3valS744);
      _M0L6_2atmpS2930 = (double)_M0L1iS746;
      if (_M0L6_2atmpS2930 == _M0L3valS744) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS746, 10);
      }
    }
  }
  _M0L4bitsS747 = *(int64_t*)&_M0L3valS744;
  _M0L6_2atmpS2943 = _M0L4bitsS747 >> 63;
  _M0L6_2atmpS2942 = _M0L6_2atmpS2943 & 1ull;
  _M0L8ieeeSignS748 = _M0L6_2atmpS2942 != 0ull;
  _M0L12ieeeMantissaS749 = _M0L4bitsS747 & 4503599627370495ull;
  _M0L6_2atmpS2941 = _M0L4bitsS747 >> 52;
  _M0L6_2atmpS2940 = _M0L6_2atmpS2941 & 2047ull;
  _M0L12ieeeExponentS750 = (int32_t)_M0L6_2atmpS2940;
  if (
    _M0L12ieeeExponentS750 == 2047
    || _M0L12ieeeExponentS750 == 0 && _M0L12ieeeMantissaS749 == 0ull
  ) {
    int32_t _M0L6_2atmpS2931 = _M0L12ieeeExponentS750 != 0;
    int32_t _M0L6_2atmpS2932 = _M0L12ieeeMantissaS749 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS748, _M0L6_2atmpS2931, _M0L6_2atmpS2932);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS751
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS749, _M0L12ieeeExponentS750);
  if (_M0L7_2abindS751 == 0) {
    uint32_t _M0L6_2atmpS2933;
    if (_M0L7_2abindS751) {
      moonbit_decref_cycle_free(_M0L7_2abindS751);
    }
    _M0L6_2atmpS2933 = *(uint32_t*)&_M0L12ieeeExponentS750;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS752 = _M0FPB3d2d(_M0L12ieeeMantissaS749, _M0L6_2atmpS2933);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS753 = _M0L7_2abindS751;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS754 = _M0L7_2aSomeS753;
    struct _M0TPB17FloatingDecimal64* _M0L1xS755 = _M0L4_2afS754;
    while (1) {
      uint64_t _M0L8mantissaS2939 = _M0L1xS755->$0;
      uint64_t _M0L1qS756 = _M0L8mantissaS2939 / 10ull;
      uint64_t _M0L8mantissaS2937 = _M0L1xS755->$0;
      uint64_t _M0L6_2atmpS2938 = 10ull * _M0L1qS756;
      uint64_t _M0L1rS757 = _M0L8mantissaS2937 - _M0L6_2atmpS2938;
      int32_t _M0L8exponentS2936;
      int32_t _M0L6_2atmpS2935;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2934;
      if (_M0L1rS757 != 0ull) {
        _M0L1vS752 = _M0L1xS755;
        break;
      }
      _M0L8exponentS2936 = _M0L1xS755->$1;
      moonbit_decref_cycle_free(_M0L1xS755);
      _M0L6_2atmpS2935 = _M0L8exponentS2936 + 1;
      _M0L6_2atmpS2934
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS2934)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS2934->$0 = _M0L1qS756;
      _M0L6_2atmpS2934->$1 = _M0L6_2atmpS2935;
      _M0L1xS755 = _M0L6_2atmpS2934;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_6105 = _M0FPB9to__chars(_M0L1vS752, _M0L8ieeeSignS748);
  moonbit_decref_cycle_free(_M0L1vS752);
  return _result_6105;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS739,
  int32_t _M0L12ieeeExponentS741
) {
  uint64_t _M0L2m2S738;
  int32_t _M0L6_2atmpS2928;
  int32_t _M0L2e2S740;
  int32_t _M0L6_2atmpS2927;
  uint64_t _M0L6_2atmpS2926;
  uint64_t _M0L4maskS742;
  uint64_t _M0L8fractionS743;
  int32_t _M0L6_2atmpS2925;
  uint64_t _M0L6_2atmpS2924;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2923;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S738 = 4503599627370496ull | _M0L12ieeeMantissaS739;
  _M0L6_2atmpS2928 = _M0L12ieeeExponentS741 - 1023;
  _M0L2e2S740 = _M0L6_2atmpS2928 - 52;
  if (_M0L2e2S740 > 0) {
    return 0;
  }
  if (_M0L2e2S740 < -52) {
    return 0;
  }
  _M0L6_2atmpS2927 = -_M0L2e2S740;
  _M0L6_2atmpS2926 = 1ull << (_M0L6_2atmpS2927 & 63);
  _M0L4maskS742 = _M0L6_2atmpS2926 - 1ull;
  _M0L8fractionS743 = _M0L2m2S738 & _M0L4maskS742;
  if (_M0L8fractionS743 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2925 = -_M0L2e2S740;
  _M0L6_2atmpS2924 = _M0L2m2S738 >> (_M0L6_2atmpS2925 & 63);
  _M0L6_2atmpS2923
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS2923)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS2923->$0 = _M0L6_2atmpS2924;
  _M0L6_2atmpS2923->$1 = 0;
  return _M0L6_2atmpS2923;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS706,
  int32_t _M0L4signS704
) {
  moonbit_bytes_t _M0L6resultS702;
  int32_t _M0Lm5indexS703;
  uint64_t _M0L6outputS705;
  int32_t _M0L7olengthS707;
  int32_t _M0L8exponentS2922;
  int32_t _M0L6_2atmpS2921;
  int32_t _M0Lm3expS708;
  int32_t _M0L6_2atmpS2920;
  int32_t _M0L6_2atmpS2918;
  int32_t _M0L18scientificNotationS709;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS702 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS703 = 0;
  if (_M0L4signS704) {
    int32_t _M0L6_2atmpS2792 = _M0Lm5indexS703;
    int32_t _M0L6_2atmpS2793;
    if (
      _M0L6_2atmpS2792 < 0
      || _M0L6_2atmpS2792 >= Moonbit_array_length(_M0L6resultS702)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS702[_M0L6_2atmpS2792] = 45;
    _M0L6_2atmpS2793 = _M0Lm5indexS703;
    _M0Lm5indexS703 = _M0L6_2atmpS2793 + 1;
  }
  _M0L6outputS705 = _M0L1vS706->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS707 = _M0FPB17decimal__length17(_M0L6outputS705);
  _M0L8exponentS2922 = _M0L1vS706->$1;
  _M0L6_2atmpS2921 = _M0L8exponentS2922 + _M0L7olengthS707;
  _M0Lm3expS708 = _M0L6_2atmpS2921 - 1;
  _M0L6_2atmpS2920 = _M0Lm3expS708;
  if (_M0L6_2atmpS2920 >= -6) {
    int32_t _M0L6_2atmpS2919 = _M0Lm3expS708;
    _M0L6_2atmpS2918 = _M0L6_2atmpS2919 < 21;
  } else {
    _M0L6_2atmpS2918 = 0;
  }
  _M0L18scientificNotationS709 = !_M0L6_2atmpS2918;
  if (_M0L18scientificNotationS709) {
    int32_t _M0L7_2abindS710 = _M0L7olengthS707 - 1;
    uint64_t _M0L6outputS711;
    int32_t _M0L1iS712 = 0;
    uint64_t _M0L6outputS713 = _M0L6outputS705;
    int32_t _M0L6_2atmpS2794;
    int32_t _M0L6_2atmpS2798;
    int32_t _M0L6_2atmpS2797;
    int32_t _M0L6_2atmpS2796;
    int32_t _M0L6_2atmpS2795;
    int32_t _M0L6_2atmpS2802;
    int32_t _M0L6_2atmpS2803;
    int32_t _M0L6_2atmpS2804;
    int32_t _M0L6_2atmpS2805;
    int32_t _M0L6_2atmpS2806;
    int32_t _M0L6_2atmpS2812;
    int32_t _M0L6_2atmpS2845;
    moonbit_string_t _result_6107;
    while (1) {
      if (_M0L1iS712 < _M0L7_2abindS710) {
        uint64_t _M0L1cS714 = _M0L6outputS713 % 10ull;
        int32_t _M0L6_2atmpS2851 = _M0Lm5indexS703;
        int32_t _M0L6_2atmpS2850 = _M0L6_2atmpS2851 + _M0L7olengthS707;
        int32_t _M0L6_2atmpS2846 = _M0L6_2atmpS2850 - _M0L1iS712;
        int32_t _M0L6_2atmpS2849 = (int32_t)_M0L1cS714;
        int32_t _M0L6_2atmpS2848 = 48 + _M0L6_2atmpS2849;
        int32_t _M0L6_2atmpS2847 = _M0L6_2atmpS2848 & 0xff;
        int32_t _M0L6_2atmpS2852;
        uint64_t _M0L6_2atmpS2853;
        if (
          _M0L6_2atmpS2846 < 0
          || _M0L6_2atmpS2846 >= Moonbit_array_length(_M0L6resultS702)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS702[_M0L6_2atmpS2846] = _M0L6_2atmpS2847;
        _M0L6_2atmpS2852 = _M0L1iS712 + 1;
        _M0L6_2atmpS2853 = _M0L6outputS713 / 10ull;
        _M0L1iS712 = _M0L6_2atmpS2852;
        _M0L6outputS713 = _M0L6_2atmpS2853;
        continue;
      } else {
        _M0L6outputS711 = _M0L6outputS713;
      }
      break;
    }
    _M0L6_2atmpS2794 = _M0Lm5indexS703;
    _M0L6_2atmpS2798 = (int32_t)_M0L6outputS711;
    _M0L6_2atmpS2797 = _M0L6_2atmpS2798 % 10;
    _M0L6_2atmpS2796 = 48 + _M0L6_2atmpS2797;
    _M0L6_2atmpS2795 = _M0L6_2atmpS2796 & 0xff;
    if (
      _M0L6_2atmpS2794 < 0
      || _M0L6_2atmpS2794 >= Moonbit_array_length(_M0L6resultS702)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS702[_M0L6_2atmpS2794] = _M0L6_2atmpS2795;
    if (_M0L7olengthS707 > 1) {
      int32_t _M0L6_2atmpS2800 = _M0Lm5indexS703;
      int32_t _M0L6_2atmpS2799 = _M0L6_2atmpS2800 + 1;
      if (
        _M0L6_2atmpS2799 < 0
        || _M0L6_2atmpS2799 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2799] = 46;
    } else {
      int32_t _M0L6_2atmpS2801 = _M0Lm5indexS703;
      _M0Lm5indexS703 = _M0L6_2atmpS2801 - 1;
    }
    _M0L6_2atmpS2802 = _M0Lm5indexS703;
    _M0L6_2atmpS2803 = _M0L7olengthS707 + 1;
    _M0Lm5indexS703 = _M0L6_2atmpS2802 + _M0L6_2atmpS2803;
    _M0L6_2atmpS2804 = _M0Lm5indexS703;
    if (
      _M0L6_2atmpS2804 < 0
      || _M0L6_2atmpS2804 >= Moonbit_array_length(_M0L6resultS702)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS702[_M0L6_2atmpS2804] = 101;
    _M0L6_2atmpS2805 = _M0Lm5indexS703;
    _M0Lm5indexS703 = _M0L6_2atmpS2805 + 1;
    _M0L6_2atmpS2806 = _M0Lm3expS708;
    if (_M0L6_2atmpS2806 < 0) {
      int32_t _M0L6_2atmpS2807 = _M0Lm5indexS703;
      int32_t _M0L6_2atmpS2808;
      int32_t _M0L6_2atmpS2809;
      if (
        _M0L6_2atmpS2807 < 0
        || _M0L6_2atmpS2807 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2807] = 45;
      _M0L6_2atmpS2808 = _M0Lm5indexS703;
      _M0Lm5indexS703 = _M0L6_2atmpS2808 + 1;
      _M0L6_2atmpS2809 = _M0Lm3expS708;
      _M0Lm3expS708 = -_M0L6_2atmpS2809;
    } else {
      int32_t _M0L6_2atmpS2810 = _M0Lm5indexS703;
      int32_t _M0L6_2atmpS2811;
      if (
        _M0L6_2atmpS2810 < 0
        || _M0L6_2atmpS2810 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2810] = 43;
      _M0L6_2atmpS2811 = _M0Lm5indexS703;
      _M0Lm5indexS703 = _M0L6_2atmpS2811 + 1;
    }
    _M0L6_2atmpS2812 = _M0Lm3expS708;
    if (_M0L6_2atmpS2812 >= 100) {
      int32_t _M0L6_2atmpS2828 = _M0Lm3expS708;
      int32_t _M0L1aS716 = _M0L6_2atmpS2828 / 100;
      int32_t _M0L6_2atmpS2827 = _M0Lm3expS708;
      int32_t _M0L6_2atmpS2826 = _M0L6_2atmpS2827 / 10;
      int32_t _M0L1bS717 = _M0L6_2atmpS2826 % 10;
      int32_t _M0L6_2atmpS2825 = _M0Lm3expS708;
      int32_t _M0L1cS718 = _M0L6_2atmpS2825 % 10;
      int32_t _M0L6_2atmpS2813 = _M0Lm5indexS703;
      int32_t _M0L6_2atmpS2815 = 48 + _M0L1aS716;
      int32_t _M0L6_2atmpS2814 = _M0L6_2atmpS2815 & 0xff;
      int32_t _M0L6_2atmpS2819;
      int32_t _M0L6_2atmpS2816;
      int32_t _M0L6_2atmpS2818;
      int32_t _M0L6_2atmpS2817;
      int32_t _M0L6_2atmpS2823;
      int32_t _M0L6_2atmpS2820;
      int32_t _M0L6_2atmpS2822;
      int32_t _M0L6_2atmpS2821;
      int32_t _M0L6_2atmpS2824;
      if (
        _M0L6_2atmpS2813 < 0
        || _M0L6_2atmpS2813 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2813] = _M0L6_2atmpS2814;
      _M0L6_2atmpS2819 = _M0Lm5indexS703;
      _M0L6_2atmpS2816 = _M0L6_2atmpS2819 + 1;
      _M0L6_2atmpS2818 = 48 + _M0L1bS717;
      _M0L6_2atmpS2817 = _M0L6_2atmpS2818 & 0xff;
      if (
        _M0L6_2atmpS2816 < 0
        || _M0L6_2atmpS2816 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2816] = _M0L6_2atmpS2817;
      _M0L6_2atmpS2823 = _M0Lm5indexS703;
      _M0L6_2atmpS2820 = _M0L6_2atmpS2823 + 2;
      _M0L6_2atmpS2822 = 48 + _M0L1cS718;
      _M0L6_2atmpS2821 = _M0L6_2atmpS2822 & 0xff;
      if (
        _M0L6_2atmpS2820 < 0
        || _M0L6_2atmpS2820 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2820] = _M0L6_2atmpS2821;
      _M0L6_2atmpS2824 = _M0Lm5indexS703;
      _M0Lm5indexS703 = _M0L6_2atmpS2824 + 3;
    } else {
      int32_t _M0L6_2atmpS2829 = _M0Lm3expS708;
      if (_M0L6_2atmpS2829 >= 10) {
        int32_t _M0L6_2atmpS2839 = _M0Lm3expS708;
        int32_t _M0L1aS719 = _M0L6_2atmpS2839 / 10;
        int32_t _M0L6_2atmpS2838 = _M0Lm3expS708;
        int32_t _M0L1bS720 = _M0L6_2atmpS2838 % 10;
        int32_t _M0L6_2atmpS2830 = _M0Lm5indexS703;
        int32_t _M0L6_2atmpS2832 = 48 + _M0L1aS719;
        int32_t _M0L6_2atmpS2831 = _M0L6_2atmpS2832 & 0xff;
        int32_t _M0L6_2atmpS2836;
        int32_t _M0L6_2atmpS2833;
        int32_t _M0L6_2atmpS2835;
        int32_t _M0L6_2atmpS2834;
        int32_t _M0L6_2atmpS2837;
        if (
          _M0L6_2atmpS2830 < 0
          || _M0L6_2atmpS2830 >= Moonbit_array_length(_M0L6resultS702)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS702[_M0L6_2atmpS2830] = _M0L6_2atmpS2831;
        _M0L6_2atmpS2836 = _M0Lm5indexS703;
        _M0L6_2atmpS2833 = _M0L6_2atmpS2836 + 1;
        _M0L6_2atmpS2835 = 48 + _M0L1bS720;
        _M0L6_2atmpS2834 = _M0L6_2atmpS2835 & 0xff;
        if (
          _M0L6_2atmpS2833 < 0
          || _M0L6_2atmpS2833 >= Moonbit_array_length(_M0L6resultS702)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS702[_M0L6_2atmpS2833] = _M0L6_2atmpS2834;
        _M0L6_2atmpS2837 = _M0Lm5indexS703;
        _M0Lm5indexS703 = _M0L6_2atmpS2837 + 2;
      } else {
        int32_t _M0L6_2atmpS2840 = _M0Lm5indexS703;
        int32_t _M0L6_2atmpS2843 = _M0Lm3expS708;
        int32_t _M0L6_2atmpS2842 = 48 + _M0L6_2atmpS2843;
        int32_t _M0L6_2atmpS2841 = _M0L6_2atmpS2842 & 0xff;
        int32_t _M0L6_2atmpS2844;
        if (
          _M0L6_2atmpS2840 < 0
          || _M0L6_2atmpS2840 >= Moonbit_array_length(_M0L6resultS702)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS702[_M0L6_2atmpS2840] = _M0L6_2atmpS2841;
        _M0L6_2atmpS2844 = _M0Lm5indexS703;
        _M0Lm5indexS703 = _M0L6_2atmpS2844 + 1;
      }
    }
    _M0L6_2atmpS2845 = _M0Lm5indexS703;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_6107
    = _M0FPB19string__from__bytes(_M0L6resultS702, 0, _M0L6_2atmpS2845);
    moonbit_decref_cycle_free(_M0L6resultS702);
    return _result_6107;
  } else {
    int32_t _M0L6_2atmpS2854 = _M0Lm3expS708;
    int32_t _M0L6_2atmpS2917;
    moonbit_string_t _result_6113;
    if (_M0L6_2atmpS2854 < 0) {
      int32_t _M0L6_2atmpS2855 = _M0Lm5indexS703;
      int32_t _M0L6_2atmpS2857;
      int32_t _M0L6_2atmpS2856;
      int32_t _M0L6_2atmpS2858;
      int32_t _M0L1iS721;
      int32_t _M0L6_2atmpS2873;
      int32_t _M0L6_2atmpS2875;
      int32_t _M0L6_2atmpS2874;
      int32_t _M0L7currentS723;
      int32_t _M0L1iS724;
      uint64_t _M0L6outputS725;
      if (
        _M0L6_2atmpS2855 < 0
        || _M0L6_2atmpS2855 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2855] = 48;
      _M0L6_2atmpS2857 = _M0Lm5indexS703;
      _M0L6_2atmpS2856 = _M0L6_2atmpS2857 + 1;
      if (
        _M0L6_2atmpS2856 < 0
        || _M0L6_2atmpS2856 >= Moonbit_array_length(_M0L6resultS702)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS702[_M0L6_2atmpS2856] = 46;
      _M0L6_2atmpS2858 = _M0Lm5indexS703;
      _M0Lm5indexS703 = _M0L6_2atmpS2858 + 2;
      _M0L1iS721 = -1;
      while (1) {
        int32_t _M0L6_2atmpS2859 = _M0Lm3expS708;
        if (_M0L1iS721 > _M0L6_2atmpS2859) {
          int32_t _M0L6_2atmpS2862 = _M0Lm5indexS703;
          int32_t _M0L6_2atmpS2861 = _M0L6_2atmpS2862 - _M0L1iS721;
          int32_t _M0L6_2atmpS2860 = _M0L6_2atmpS2861 - 1;
          int32_t _M0L6_2atmpS2863;
          if (
            _M0L6_2atmpS2860 < 0
            || _M0L6_2atmpS2860 >= Moonbit_array_length(_M0L6resultS702)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS702[_M0L6_2atmpS2860] = 48;
          _M0L6_2atmpS2863 = _M0L1iS721 - 1;
          _M0L1iS721 = _M0L6_2atmpS2863;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2873 = _M0Lm5indexS703;
      _M0L6_2atmpS2875 = _M0Lm3expS708;
      _M0L6_2atmpS2874 = -1 - _M0L6_2atmpS2875;
      _M0L7currentS723 = _M0L6_2atmpS2873 + _M0L6_2atmpS2874;
      _M0L1iS724 = 0;
      _M0L6outputS725 = _M0L6outputS705;
      while (1) {
        if (_M0L1iS724 < _M0L7olengthS707) {
          int32_t _M0L6_2atmpS2870 = _M0L7currentS723 + _M0L7olengthS707;
          int32_t _M0L6_2atmpS2869 = _M0L6_2atmpS2870 - _M0L1iS724;
          int32_t _M0L6_2atmpS2864 = _M0L6_2atmpS2869 - 1;
          uint64_t _M0L6_2atmpS2868 = _M0L6outputS725 % 10ull;
          int32_t _M0L6_2atmpS2867 = (int32_t)_M0L6_2atmpS2868;
          int32_t _M0L6_2atmpS2866 = 48 + _M0L6_2atmpS2867;
          int32_t _M0L6_2atmpS2865 = _M0L6_2atmpS2866 & 0xff;
          int32_t _M0L6_2atmpS2871;
          uint64_t _M0L6_2atmpS2872;
          if (
            _M0L6_2atmpS2864 < 0
            || _M0L6_2atmpS2864 >= Moonbit_array_length(_M0L6resultS702)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS702[_M0L6_2atmpS2864] = _M0L6_2atmpS2865;
          _M0L6_2atmpS2871 = _M0L1iS724 + 1;
          _M0L6_2atmpS2872 = _M0L6outputS725 / 10ull;
          _M0L1iS724 = _M0L6_2atmpS2871;
          _M0L6outputS725 = _M0L6_2atmpS2872;
          continue;
        }
        break;
      }
      _M0Lm5indexS703 = _M0L7currentS723 + _M0L7olengthS707;
    } else {
      int32_t _M0L6_2atmpS2877 = _M0Lm3expS708;
      int32_t _M0L6_2atmpS2876 = _M0L6_2atmpS2877 + 1;
      if (_M0L6_2atmpS2876 >= _M0L7olengthS707) {
        int32_t _M0L1iS727 = 0;
        uint64_t _M0L6outputS728 = _M0L6outputS705;
        int32_t _M0L6_2atmpS2888;
        int32_t _M0L6_2atmpS2893;
        int32_t _M0L7_2abindS730;
        int32_t _M0L1iS731;
        int32_t _M0L6_2atmpS2894;
        int32_t _M0L6_2atmpS2897;
        int32_t _M0L6_2atmpS2896;
        int32_t _M0L6_2atmpS2895;
        while (1) {
          if (_M0L1iS727 < _M0L7olengthS707) {
            int32_t _M0L6_2atmpS2885 = _M0Lm5indexS703;
            int32_t _M0L6_2atmpS2884 = _M0L6_2atmpS2885 + _M0L7olengthS707;
            int32_t _M0L6_2atmpS2883 = _M0L6_2atmpS2884 - _M0L1iS727;
            int32_t _M0L6_2atmpS2878 = _M0L6_2atmpS2883 - 1;
            uint64_t _M0L6_2atmpS2882 = _M0L6outputS728 % 10ull;
            int32_t _M0L6_2atmpS2881 = (int32_t)_M0L6_2atmpS2882;
            int32_t _M0L6_2atmpS2880 = 48 + _M0L6_2atmpS2881;
            int32_t _M0L6_2atmpS2879 = _M0L6_2atmpS2880 & 0xff;
            int32_t _M0L6_2atmpS2886;
            uint64_t _M0L6_2atmpS2887;
            if (
              _M0L6_2atmpS2878 < 0
              || _M0L6_2atmpS2878 >= Moonbit_array_length(_M0L6resultS702)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS702[_M0L6_2atmpS2878] = _M0L6_2atmpS2879;
            _M0L6_2atmpS2886 = _M0L1iS727 + 1;
            _M0L6_2atmpS2887 = _M0L6outputS728 / 10ull;
            _M0L1iS727 = _M0L6_2atmpS2886;
            _M0L6outputS728 = _M0L6_2atmpS2887;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2888 = _M0Lm5indexS703;
        _M0Lm5indexS703 = _M0L6_2atmpS2888 + _M0L7olengthS707;
        _M0L6_2atmpS2893 = _M0Lm3expS708;
        _M0L7_2abindS730 = _M0L6_2atmpS2893 + 1;
        _M0L1iS731 = _M0L7olengthS707;
        while (1) {
          if (_M0L1iS731 < _M0L7_2abindS730) {
            int32_t _M0L6_2atmpS2891 = _M0Lm5indexS703;
            int32_t _M0L6_2atmpS2890 = _M0L6_2atmpS2891 + _M0L1iS731;
            int32_t _M0L6_2atmpS2889 = _M0L6_2atmpS2890 - _M0L7olengthS707;
            int32_t _M0L6_2atmpS2892;
            if (
              _M0L6_2atmpS2889 < 0
              || _M0L6_2atmpS2889 >= Moonbit_array_length(_M0L6resultS702)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS702[_M0L6_2atmpS2889] = 48;
            _M0L6_2atmpS2892 = _M0L1iS731 + 1;
            _M0L1iS731 = _M0L6_2atmpS2892;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2894 = _M0Lm5indexS703;
        _M0L6_2atmpS2897 = _M0Lm3expS708;
        _M0L6_2atmpS2896 = _M0L6_2atmpS2897 + 1;
        _M0L6_2atmpS2895 = _M0L6_2atmpS2896 - _M0L7olengthS707;
        _M0Lm5indexS703 = _M0L6_2atmpS2894 + _M0L6_2atmpS2895;
      } else {
        int32_t _M0L6_2atmpS2914 = _M0Lm5indexS703;
        int32_t _M0L6_2atmpS2913 = _M0L6_2atmpS2914 + 1;
        int32_t _M0L1iS733 = 0;
        int32_t _M0L7currentS734 = _M0L6_2atmpS2913;
        uint64_t _M0L6outputS735 = _M0L6outputS705;
        int32_t _M0L6_2atmpS2915;
        int32_t _M0L6_2atmpS2916;
        while (1) {
          if (_M0L1iS733 < _M0L7olengthS707) {
            int32_t _M0L6_2atmpS2909 = _M0L7olengthS707 - _M0L1iS733;
            int32_t _M0L6_2atmpS2907 = _M0L6_2atmpS2909 - 1;
            int32_t _M0L6_2atmpS2908 = _M0Lm3expS708;
            int32_t _M0L7currentS736;
            int32_t _M0L6_2atmpS2904;
            int32_t _M0L6_2atmpS2903;
            int32_t _M0L6_2atmpS2898;
            uint64_t _M0L6_2atmpS2902;
            int32_t _M0L6_2atmpS2901;
            int32_t _M0L6_2atmpS2900;
            int32_t _M0L6_2atmpS2899;
            int32_t _M0L6_2atmpS2905;
            uint64_t _M0L6_2atmpS2906;
            if (_M0L6_2atmpS2907 == _M0L6_2atmpS2908) {
              int32_t _M0L6_2atmpS2912 = _M0L7currentS734 + _M0L7olengthS707;
              int32_t _M0L6_2atmpS2911 = _M0L6_2atmpS2912 - _M0L1iS733;
              int32_t _M0L6_2atmpS2910 = _M0L6_2atmpS2911 - 1;
              if (
                _M0L6_2atmpS2910 < 0
                || _M0L6_2atmpS2910 >= Moonbit_array_length(_M0L6resultS702)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS702[_M0L6_2atmpS2910] = 46;
              _M0L7currentS736 = _M0L7currentS734 - 1;
            } else {
              _M0L7currentS736 = _M0L7currentS734;
            }
            _M0L6_2atmpS2904 = _M0L7currentS736 + _M0L7olengthS707;
            _M0L6_2atmpS2903 = _M0L6_2atmpS2904 - _M0L1iS733;
            _M0L6_2atmpS2898 = _M0L6_2atmpS2903 - 1;
            _M0L6_2atmpS2902 = _M0L6outputS735 % 10ull;
            _M0L6_2atmpS2901 = (int32_t)_M0L6_2atmpS2902;
            _M0L6_2atmpS2900 = 48 + _M0L6_2atmpS2901;
            _M0L6_2atmpS2899 = _M0L6_2atmpS2900 & 0xff;
            if (
              _M0L6_2atmpS2898 < 0
              || _M0L6_2atmpS2898 >= Moonbit_array_length(_M0L6resultS702)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS702[_M0L6_2atmpS2898] = _M0L6_2atmpS2899;
            _M0L6_2atmpS2905 = _M0L1iS733 + 1;
            _M0L6_2atmpS2906 = _M0L6outputS735 / 10ull;
            _M0L1iS733 = _M0L6_2atmpS2905;
            _M0L7currentS734 = _M0L7currentS736;
            _M0L6outputS735 = _M0L6_2atmpS2906;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2915 = _M0Lm5indexS703;
        _M0L6_2atmpS2916 = _M0L7olengthS707 + 1;
        _M0Lm5indexS703 = _M0L6_2atmpS2915 + _M0L6_2atmpS2916;
      }
    }
    _M0L6_2atmpS2917 = _M0Lm5indexS703;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_6113
    = _M0FPB19string__from__bytes(_M0L6resultS702, 0, _M0L6_2atmpS2917);
    moonbit_decref_cycle_free(_M0L6resultS702);
    return _result_6113;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS648,
  uint32_t _M0L12ieeeExponentS647
) {
  int32_t _M0Lm2e2S645;
  uint64_t _M0Lm2m2S646;
  uint64_t _M0L6_2atmpS2791;
  uint64_t _M0L6_2atmpS2790;
  int32_t _M0L4evenS649;
  uint64_t _M0L6_2atmpS2789;
  uint64_t _M0L2mvS650;
  int32_t _M0L7mmShiftS651;
  uint64_t _M0Lm2vrS652;
  uint64_t _M0Lm2vpS653;
  uint64_t _M0Lm2vmS654;
  int32_t _M0Lm3e10S655;
  int32_t _M0Lm17vmIsTrailingZerosS656;
  int32_t _M0Lm17vrIsTrailingZerosS657;
  int32_t _M0L6_2atmpS2691;
  int32_t _M0Lm7removedS676;
  int32_t _M0Lm16lastRemovedDigitS677;
  uint64_t _M0Lm6outputS678;
  int32_t _M0L6_2atmpS2787;
  int32_t _M0L6_2atmpS2788;
  int32_t _M0L3expS701;
  uint64_t _M0L6_2atmpS2786;
  struct _M0TPB17FloatingDecimal64* _block_6119;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S645 = 0;
  _M0Lm2m2S646 = 0ull;
  if (_M0L12ieeeExponentS647 == 0u) {
    _M0Lm2e2S645 = -1076;
    _M0Lm2m2S646 = _M0L12ieeeMantissaS648;
  } else {
    int32_t _M0L6_2atmpS2690 = *(int32_t*)&_M0L12ieeeExponentS647;
    int32_t _M0L6_2atmpS2689 = _M0L6_2atmpS2690 - 1023;
    int32_t _M0L6_2atmpS2688 = _M0L6_2atmpS2689 - 52;
    _M0Lm2e2S645 = _M0L6_2atmpS2688 - 2;
    _M0Lm2m2S646 = 4503599627370496ull | _M0L12ieeeMantissaS648;
  }
  _M0L6_2atmpS2791 = _M0Lm2m2S646;
  _M0L6_2atmpS2790 = _M0L6_2atmpS2791 & 1ull;
  _M0L4evenS649 = _M0L6_2atmpS2790 == 0ull;
  _M0L6_2atmpS2789 = _M0Lm2m2S646;
  _M0L2mvS650 = 4ull * _M0L6_2atmpS2789;
  _M0L7mmShiftS651
  = _M0L12ieeeMantissaS648 != 0ull || _M0L12ieeeExponentS647 <= 1u;
  _M0Lm2vrS652 = 0ull;
  _M0Lm2vpS653 = 0ull;
  _M0Lm2vmS654 = 0ull;
  _M0Lm3e10S655 = 0;
  _M0Lm17vmIsTrailingZerosS656 = 0;
  _M0Lm17vrIsTrailingZerosS657 = 0;
  _M0L6_2atmpS2691 = _M0Lm2e2S645;
  if (_M0L6_2atmpS2691 >= 0) {
    int32_t _M0L6_2atmpS2713 = _M0Lm2e2S645;
    int32_t _M0L6_2atmpS2709;
    int32_t _M0L6_2atmpS2712;
    int32_t _M0L6_2atmpS2711;
    int32_t _M0L6_2atmpS2710;
    int32_t _M0L1qS658;
    int32_t _M0L6_2atmpS2708;
    int32_t _M0L6_2atmpS2707;
    int32_t _M0L1kS659;
    int32_t _M0L6_2atmpS2706;
    int32_t _M0L6_2atmpS2705;
    int32_t _M0L6_2atmpS2704;
    int32_t _M0L1iS660;
    struct _M0TPB8Pow5Pair _M0L4pow5S661;
    uint64_t _M0L6_2atmpS2703;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS662;
    uint64_t _M0L8_2avrOutS663;
    uint64_t _M0L8_2avpOutS664;
    uint64_t _M0L8_2avmOutS665;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2709 = _M0FPB9log10Pow2(_M0L6_2atmpS2713);
    _M0L6_2atmpS2712 = _M0Lm2e2S645;
    _M0L6_2atmpS2711 = _M0L6_2atmpS2712 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2710 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS2711);
    _M0L1qS658 = _M0L6_2atmpS2709 - _M0L6_2atmpS2710;
    _M0Lm3e10S655 = _M0L1qS658;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2708 = _M0FPB8pow5bits(_M0L1qS658);
    _M0L6_2atmpS2707 = 125 + _M0L6_2atmpS2708;
    _M0L1kS659 = _M0L6_2atmpS2707 - 1;
    _M0L6_2atmpS2706 = _M0Lm2e2S645;
    _M0L6_2atmpS2705 = -_M0L6_2atmpS2706;
    _M0L6_2atmpS2704 = _M0L6_2atmpS2705 + _M0L1qS658;
    _M0L1iS660 = _M0L6_2atmpS2704 + _M0L1kS659;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S661 = _M0FPB22double__computeInvPow5(_M0L1qS658);
    _M0L6_2atmpS2703 = _M0Lm2m2S646;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS662
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS2703, _M0L4pow5S661, _M0L1iS660, _M0L7mmShiftS651);
    _M0L8_2avrOutS663 = _M0L7_2abindS662.$0;
    _M0L8_2avpOutS664 = _M0L7_2abindS662.$1;
    _M0L8_2avmOutS665 = _M0L7_2abindS662.$2;
    _M0Lm2vrS652 = _M0L8_2avrOutS663;
    _M0Lm2vpS653 = _M0L8_2avpOutS664;
    _M0Lm2vmS654 = _M0L8_2avmOutS665;
    if (_M0L1qS658 <= 21) {
      int32_t _M0L6_2atmpS2699 = (int32_t)_M0L2mvS650;
      uint64_t _M0L6_2atmpS2702 = _M0L2mvS650 / 5ull;
      int32_t _M0L6_2atmpS2701 = (int32_t)_M0L6_2atmpS2702;
      int32_t _M0L6_2atmpS2700 = 5 * _M0L6_2atmpS2701;
      int32_t _M0L6mvMod5S666 = _M0L6_2atmpS2699 - _M0L6_2atmpS2700;
      if (_M0L6mvMod5S666 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS657
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS650, _M0L1qS658);
      } else if (_M0L4evenS649) {
        uint64_t _M0L6_2atmpS2693 = _M0L2mvS650 - 1ull;
        uint64_t _M0L6_2atmpS2694;
        uint64_t _M0L6_2atmpS2692;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2694 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS651);
        _M0L6_2atmpS2692 = _M0L6_2atmpS2693 - _M0L6_2atmpS2694;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS656
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS2692, _M0L1qS658);
      } else {
        uint64_t _M0L6_2atmpS2695 = _M0Lm2vpS653;
        uint64_t _M0L6_2atmpS2698 = _M0L2mvS650 + 2ull;
        int32_t _M0L6_2atmpS2697;
        uint64_t _M0L6_2atmpS2696;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2697
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS2698, _M0L1qS658);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2696 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS2697);
        _M0Lm2vpS653 = _M0L6_2atmpS2695 - _M0L6_2atmpS2696;
      }
    }
  } else {
    int32_t _M0L6_2atmpS2727 = _M0Lm2e2S645;
    int32_t _M0L6_2atmpS2726 = -_M0L6_2atmpS2727;
    int32_t _M0L6_2atmpS2721;
    int32_t _M0L6_2atmpS2725;
    int32_t _M0L6_2atmpS2724;
    int32_t _M0L6_2atmpS2723;
    int32_t _M0L6_2atmpS2722;
    int32_t _M0L1qS667;
    int32_t _M0L6_2atmpS2714;
    int32_t _M0L6_2atmpS2720;
    int32_t _M0L6_2atmpS2719;
    int32_t _M0L1iS668;
    int32_t _M0L6_2atmpS2718;
    int32_t _M0L1kS669;
    int32_t _M0L1jS670;
    struct _M0TPB8Pow5Pair _M0L4pow5S671;
    uint64_t _M0L6_2atmpS2717;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS672;
    uint64_t _M0L8_2avrOutS673;
    uint64_t _M0L8_2avpOutS674;
    uint64_t _M0L8_2avmOutS675;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2721 = _M0FPB9log10Pow5(_M0L6_2atmpS2726);
    _M0L6_2atmpS2725 = _M0Lm2e2S645;
    _M0L6_2atmpS2724 = -_M0L6_2atmpS2725;
    _M0L6_2atmpS2723 = _M0L6_2atmpS2724 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2722 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS2723);
    _M0L1qS667 = _M0L6_2atmpS2721 - _M0L6_2atmpS2722;
    _M0L6_2atmpS2714 = _M0Lm2e2S645;
    _M0Lm3e10S655 = _M0L1qS667 + _M0L6_2atmpS2714;
    _M0L6_2atmpS2720 = _M0Lm2e2S645;
    _M0L6_2atmpS2719 = -_M0L6_2atmpS2720;
    _M0L1iS668 = _M0L6_2atmpS2719 - _M0L1qS667;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2718 = _M0FPB8pow5bits(_M0L1iS668);
    _M0L1kS669 = _M0L6_2atmpS2718 - 125;
    _M0L1jS670 = _M0L1qS667 - _M0L1kS669;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S671 = _M0FPB19double__computePow5(_M0L1iS668);
    _M0L6_2atmpS2717 = _M0Lm2m2S646;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS672
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS2717, _M0L4pow5S671, _M0L1jS670, _M0L7mmShiftS651);
    _M0L8_2avrOutS673 = _M0L7_2abindS672.$0;
    _M0L8_2avpOutS674 = _M0L7_2abindS672.$1;
    _M0L8_2avmOutS675 = _M0L7_2abindS672.$2;
    _M0Lm2vrS652 = _M0L8_2avrOutS673;
    _M0Lm2vpS653 = _M0L8_2avpOutS674;
    _M0Lm2vmS654 = _M0L8_2avmOutS675;
    if (_M0L1qS667 <= 1) {
      _M0Lm17vrIsTrailingZerosS657 = 1;
      if (_M0L4evenS649) {
        int32_t _M0L6_2atmpS2715;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2715 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS651);
        _M0Lm17vmIsTrailingZerosS656 = _M0L6_2atmpS2715 == 1;
      } else {
        uint64_t _M0L6_2atmpS2716 = _M0Lm2vpS653;
        _M0Lm2vpS653 = _M0L6_2atmpS2716 - 1ull;
      }
    } else if (_M0L1qS667 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS657
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS650, _M0L1qS667);
    }
  }
  _M0Lm7removedS676 = 0;
  _M0Lm16lastRemovedDigitS677 = 0;
  _M0Lm6outputS678 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS656 || _M0Lm17vrIsTrailingZerosS657) {
    int32_t _if__result_6116;
    uint64_t _M0L6_2atmpS2757;
    uint64_t _M0L6_2atmpS2763;
    uint64_t _M0L6_2atmpS2764;
    int32_t _if__result_6117;
    int32_t _M0L6_2atmpS2760;
    int64_t _M0L6_2atmpS2759;
    uint64_t _M0L6_2atmpS2758;
    while (1) {
      uint64_t _M0L6_2atmpS2740 = _M0Lm2vpS653;
      uint64_t _M0L7vpDiv10S679 = _M0L6_2atmpS2740 / 10ull;
      uint64_t _M0L6_2atmpS2739 = _M0Lm2vmS654;
      uint64_t _M0L7vmDiv10S680 = _M0L6_2atmpS2739 / 10ull;
      uint64_t _M0L6_2atmpS2738;
      int32_t _M0L6_2atmpS2735;
      int32_t _M0L6_2atmpS2737;
      int32_t _M0L6_2atmpS2736;
      int32_t _M0L7vmMod10S682;
      uint64_t _M0L6_2atmpS2734;
      uint64_t _M0L7vrDiv10S683;
      uint64_t _M0L6_2atmpS2733;
      int32_t _M0L6_2atmpS2730;
      int32_t _M0L6_2atmpS2732;
      int32_t _M0L6_2atmpS2731;
      int32_t _M0L7vrMod10S684;
      int32_t _M0L6_2atmpS2729;
      if (_M0L7vpDiv10S679 <= _M0L7vmDiv10S680) {
        break;
      }
      _M0L6_2atmpS2738 = _M0Lm2vmS654;
      _M0L6_2atmpS2735 = (int32_t)_M0L6_2atmpS2738;
      _M0L6_2atmpS2737 = (int32_t)_M0L7vmDiv10S680;
      _M0L6_2atmpS2736 = 10 * _M0L6_2atmpS2737;
      _M0L7vmMod10S682 = _M0L6_2atmpS2735 - _M0L6_2atmpS2736;
      _M0L6_2atmpS2734 = _M0Lm2vrS652;
      _M0L7vrDiv10S683 = _M0L6_2atmpS2734 / 10ull;
      _M0L6_2atmpS2733 = _M0Lm2vrS652;
      _M0L6_2atmpS2730 = (int32_t)_M0L6_2atmpS2733;
      _M0L6_2atmpS2732 = (int32_t)_M0L7vrDiv10S683;
      _M0L6_2atmpS2731 = 10 * _M0L6_2atmpS2732;
      _M0L7vrMod10S684 = _M0L6_2atmpS2730 - _M0L6_2atmpS2731;
      _M0Lm17vmIsTrailingZerosS656
      = _M0Lm17vmIsTrailingZerosS656 && _M0L7vmMod10S682 == 0;
      if (_M0Lm17vrIsTrailingZerosS657) {
        int32_t _M0L6_2atmpS2728 = _M0Lm16lastRemovedDigitS677;
        _M0Lm17vrIsTrailingZerosS657 = _M0L6_2atmpS2728 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS657 = 0;
      }
      _M0Lm16lastRemovedDigitS677 = _M0L7vrMod10S684;
      _M0Lm2vrS652 = _M0L7vrDiv10S683;
      _M0Lm2vpS653 = _M0L7vpDiv10S679;
      _M0Lm2vmS654 = _M0L7vmDiv10S680;
      _M0L6_2atmpS2729 = _M0Lm7removedS676;
      _M0Lm7removedS676 = _M0L6_2atmpS2729 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS656) {
      while (1) {
        uint64_t _M0L6_2atmpS2753 = _M0Lm2vmS654;
        uint64_t _M0L7vmDiv10S685 = _M0L6_2atmpS2753 / 10ull;
        uint64_t _M0L6_2atmpS2752 = _M0Lm2vmS654;
        int32_t _M0L6_2atmpS2749 = (int32_t)_M0L6_2atmpS2752;
        int32_t _M0L6_2atmpS2751 = (int32_t)_M0L7vmDiv10S685;
        int32_t _M0L6_2atmpS2750 = 10 * _M0L6_2atmpS2751;
        int32_t _M0L7vmMod10S686 = _M0L6_2atmpS2749 - _M0L6_2atmpS2750;
        uint64_t _M0L6_2atmpS2748;
        uint64_t _M0L7vpDiv10S688;
        uint64_t _M0L6_2atmpS2747;
        uint64_t _M0L7vrDiv10S689;
        uint64_t _M0L6_2atmpS2746;
        int32_t _M0L6_2atmpS2743;
        int32_t _M0L6_2atmpS2745;
        int32_t _M0L6_2atmpS2744;
        int32_t _M0L7vrMod10S690;
        int32_t _M0L6_2atmpS2742;
        if (_M0L7vmMod10S686 != 0) {
          break;
        }
        _M0L6_2atmpS2748 = _M0Lm2vpS653;
        _M0L7vpDiv10S688 = _M0L6_2atmpS2748 / 10ull;
        _M0L6_2atmpS2747 = _M0Lm2vrS652;
        _M0L7vrDiv10S689 = _M0L6_2atmpS2747 / 10ull;
        _M0L6_2atmpS2746 = _M0Lm2vrS652;
        _M0L6_2atmpS2743 = (int32_t)_M0L6_2atmpS2746;
        _M0L6_2atmpS2745 = (int32_t)_M0L7vrDiv10S689;
        _M0L6_2atmpS2744 = 10 * _M0L6_2atmpS2745;
        _M0L7vrMod10S690 = _M0L6_2atmpS2743 - _M0L6_2atmpS2744;
        if (_M0Lm17vrIsTrailingZerosS657) {
          int32_t _M0L6_2atmpS2741 = _M0Lm16lastRemovedDigitS677;
          _M0Lm17vrIsTrailingZerosS657 = _M0L6_2atmpS2741 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS657 = 0;
        }
        _M0Lm16lastRemovedDigitS677 = _M0L7vrMod10S690;
        _M0Lm2vrS652 = _M0L7vrDiv10S689;
        _M0Lm2vpS653 = _M0L7vpDiv10S688;
        _M0Lm2vmS654 = _M0L7vmDiv10S685;
        _M0L6_2atmpS2742 = _M0Lm7removedS676;
        _M0Lm7removedS676 = _M0L6_2atmpS2742 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS657) {
      int32_t _M0L6_2atmpS2756 = _M0Lm16lastRemovedDigitS677;
      if (_M0L6_2atmpS2756 == 5) {
        uint64_t _M0L6_2atmpS2755 = _M0Lm2vrS652;
        uint64_t _M0L6_2atmpS2754 = _M0L6_2atmpS2755 % 2ull;
        _if__result_6116 = _M0L6_2atmpS2754 == 0ull;
      } else {
        _if__result_6116 = 0;
      }
    } else {
      _if__result_6116 = 0;
    }
    if (_if__result_6116) {
      _M0Lm16lastRemovedDigitS677 = 4;
    }
    _M0L6_2atmpS2757 = _M0Lm2vrS652;
    _M0L6_2atmpS2763 = _M0Lm2vrS652;
    _M0L6_2atmpS2764 = _M0Lm2vmS654;
    if (_M0L6_2atmpS2763 == _M0L6_2atmpS2764) {
      if (!_M0L4evenS649) {
        _if__result_6117 = 1;
      } else {
        int32_t _M0L6_2atmpS2762 = _M0Lm17vmIsTrailingZerosS656;
        _if__result_6117 = !_M0L6_2atmpS2762;
      }
    } else {
      _if__result_6117 = 0;
    }
    if (_if__result_6117) {
      _M0L6_2atmpS2760 = 1;
    } else {
      int32_t _M0L6_2atmpS2761 = _M0Lm16lastRemovedDigitS677;
      _M0L6_2atmpS2760 = _M0L6_2atmpS2761 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2759 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS2760);
    _M0L6_2atmpS2758 = *(uint64_t*)&_M0L6_2atmpS2759;
    _M0Lm6outputS678 = _M0L6_2atmpS2757 + _M0L6_2atmpS2758;
  } else {
    int32_t _M0Lm7roundUpS691 = 0;
    uint64_t _M0L6_2atmpS2785 = _M0Lm2vpS653;
    uint64_t _M0L8vpDiv100S692 = _M0L6_2atmpS2785 / 100ull;
    uint64_t _M0L6_2atmpS2784 = _M0Lm2vmS654;
    uint64_t _M0L8vmDiv100S693 = _M0L6_2atmpS2784 / 100ull;
    uint64_t _M0L6_2atmpS2779;
    uint64_t _M0L6_2atmpS2782;
    uint64_t _M0L6_2atmpS2783;
    int32_t _M0L6_2atmpS2781;
    uint64_t _M0L6_2atmpS2780;
    if (_M0L8vpDiv100S692 > _M0L8vmDiv100S693) {
      uint64_t _M0L6_2atmpS2770 = _M0Lm2vrS652;
      uint64_t _M0L8vrDiv100S694 = _M0L6_2atmpS2770 / 100ull;
      uint64_t _M0L6_2atmpS2769 = _M0Lm2vrS652;
      int32_t _M0L6_2atmpS2766 = (int32_t)_M0L6_2atmpS2769;
      int32_t _M0L6_2atmpS2768 = (int32_t)_M0L8vrDiv100S694;
      int32_t _M0L6_2atmpS2767 = 100 * _M0L6_2atmpS2768;
      int32_t _M0L8vrMod100S695 = _M0L6_2atmpS2766 - _M0L6_2atmpS2767;
      int32_t _M0L6_2atmpS2765;
      _M0Lm7roundUpS691 = _M0L8vrMod100S695 >= 50;
      _M0Lm2vrS652 = _M0L8vrDiv100S694;
      _M0Lm2vpS653 = _M0L8vpDiv100S692;
      _M0Lm2vmS654 = _M0L8vmDiv100S693;
      _M0L6_2atmpS2765 = _M0Lm7removedS676;
      _M0Lm7removedS676 = _M0L6_2atmpS2765 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS2778 = _M0Lm2vpS653;
      uint64_t _M0L7vpDiv10S696 = _M0L6_2atmpS2778 / 10ull;
      uint64_t _M0L6_2atmpS2777 = _M0Lm2vmS654;
      uint64_t _M0L7vmDiv10S697 = _M0L6_2atmpS2777 / 10ull;
      uint64_t _M0L6_2atmpS2776;
      uint64_t _M0L7vrDiv10S699;
      uint64_t _M0L6_2atmpS2775;
      int32_t _M0L6_2atmpS2772;
      int32_t _M0L6_2atmpS2774;
      int32_t _M0L6_2atmpS2773;
      int32_t _M0L7vrMod10S700;
      int32_t _M0L6_2atmpS2771;
      if (_M0L7vpDiv10S696 <= _M0L7vmDiv10S697) {
        break;
      }
      _M0L6_2atmpS2776 = _M0Lm2vrS652;
      _M0L7vrDiv10S699 = _M0L6_2atmpS2776 / 10ull;
      _M0L6_2atmpS2775 = _M0Lm2vrS652;
      _M0L6_2atmpS2772 = (int32_t)_M0L6_2atmpS2775;
      _M0L6_2atmpS2774 = (int32_t)_M0L7vrDiv10S699;
      _M0L6_2atmpS2773 = 10 * _M0L6_2atmpS2774;
      _M0L7vrMod10S700 = _M0L6_2atmpS2772 - _M0L6_2atmpS2773;
      _M0Lm7roundUpS691 = _M0L7vrMod10S700 >= 5;
      _M0Lm2vrS652 = _M0L7vrDiv10S699;
      _M0Lm2vpS653 = _M0L7vpDiv10S696;
      _M0Lm2vmS654 = _M0L7vmDiv10S697;
      _M0L6_2atmpS2771 = _M0Lm7removedS676;
      _M0Lm7removedS676 = _M0L6_2atmpS2771 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS2779 = _M0Lm2vrS652;
    _M0L6_2atmpS2782 = _M0Lm2vrS652;
    _M0L6_2atmpS2783 = _M0Lm2vmS654;
    _M0L6_2atmpS2781
    = _M0L6_2atmpS2782 == _M0L6_2atmpS2783 || _M0Lm7roundUpS691;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2780 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS2781);
    _M0Lm6outputS678 = _M0L6_2atmpS2779 + _M0L6_2atmpS2780;
  }
  _M0L6_2atmpS2787 = _M0Lm3e10S655;
  _M0L6_2atmpS2788 = _M0Lm7removedS676;
  _M0L3expS701 = _M0L6_2atmpS2787 + _M0L6_2atmpS2788;
  _M0L6_2atmpS2786 = _M0Lm6outputS678;
  _block_6119
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_6119)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6119->$0 = _M0L6_2atmpS2786;
  _block_6119->$1 = _M0L3expS701;
  return _block_6119;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS644) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS644) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS643) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS643) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS642) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS642) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS641) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS641 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS641 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS641 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS641 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS641 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS641 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS641 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS641 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS641 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS641 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS641 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS641 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS641 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS641 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS641 >= 100ull) {
    return 3;
  }
  if (_M0L1vS641 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS624) {
  int32_t _M0L6_2atmpS2687;
  int32_t _M0L6_2atmpS2686;
  int32_t _M0L4baseS623;
  int32_t _M0L5base2S625;
  int32_t _M0L6offsetS626;
  int32_t _M0L6_2atmpS2685;
  uint64_t _M0L4mul0S627;
  int32_t _M0L6_2atmpS2684;
  int32_t _M0L6_2atmpS2683;
  uint64_t _M0L4mul1S628;
  uint64_t _M0L1mS629;
  struct _M0TPB7Umul128 _M0L7_2abindS630;
  uint64_t _M0L7_2alow1S631;
  uint64_t _M0L8_2ahigh1S632;
  struct _M0TPB7Umul128 _M0L7_2abindS633;
  uint64_t _M0L7_2alow0S634;
  uint64_t _M0L8_2ahigh0S635;
  uint64_t _M0L3sumS636;
  uint64_t _M0Lm5high1S637;
  int32_t _M0L6_2atmpS2681;
  int32_t _M0L6_2atmpS2682;
  int32_t _M0L5deltaS638;
  uint64_t _M0L6_2atmpS2680;
  uint64_t _M0L6_2atmpS2672;
  int32_t _M0L6_2atmpS2679;
  uint32_t _M0L6_2atmpS2676;
  int32_t _M0L6_2atmpS2678;
  int32_t _M0L6_2atmpS2677;
  uint32_t _M0L6_2atmpS2675;
  uint32_t _M0L6_2atmpS2674;
  uint64_t _M0L6_2atmpS2673;
  uint64_t _M0L1aS639;
  uint64_t _M0L6_2atmpS2671;
  uint64_t _M0L1bS640;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2687 = _M0L1iS624 + 26;
  _M0L6_2atmpS2686 = _M0L6_2atmpS2687 - 1;
  _M0L4baseS623 = _M0L6_2atmpS2686 / 26;
  _M0L5base2S625 = _M0L4baseS623 * 26;
  _M0L6offsetS626 = _M0L5base2S625 - _M0L1iS624;
  _M0L6_2atmpS2685 = _M0L4baseS623 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S627
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS2685);
  _M0L6_2atmpS2684 = _M0L4baseS623 * 2;
  _M0L6_2atmpS2683 = _M0L6_2atmpS2684 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S628
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS2683);
  if (_M0L6offsetS626 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S627, .$1 = _M0L4mul1S628};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS629
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS626);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS630 = _M0FPB7umul128(_M0L1mS629, _M0L4mul1S628);
  _M0L7_2alow1S631 = _M0L7_2abindS630.$0;
  _M0L8_2ahigh1S632 = _M0L7_2abindS630.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS633 = _M0FPB7umul128(_M0L1mS629, _M0L4mul0S627);
  _M0L7_2alow0S634 = _M0L7_2abindS633.$0;
  _M0L8_2ahigh0S635 = _M0L7_2abindS633.$1;
  _M0L3sumS636 = _M0L8_2ahigh0S635 + _M0L7_2alow1S631;
  _M0Lm5high1S637 = _M0L8_2ahigh1S632;
  if (_M0L3sumS636 < _M0L8_2ahigh0S635) {
    uint64_t _M0L6_2atmpS2670 = _M0Lm5high1S637;
    _M0Lm5high1S637 = _M0L6_2atmpS2670 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2681 = _M0FPB8pow5bits(_M0L5base2S625);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2682 = _M0FPB8pow5bits(_M0L1iS624);
  _M0L5deltaS638 = _M0L6_2atmpS2681 - _M0L6_2atmpS2682;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2680
  = _M0FPB13shiftright128(_M0L7_2alow0S634, _M0L3sumS636, _M0L5deltaS638);
  _M0L6_2atmpS2672 = _M0L6_2atmpS2680 + 1ull;
  _M0L6_2atmpS2679 = _M0L1iS624 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2676
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS2679);
  _M0L6_2atmpS2678 = _M0L1iS624 % 16;
  _M0L6_2atmpS2677 = _M0L6_2atmpS2678 << 1;
  _M0L6_2atmpS2675 = _M0L6_2atmpS2676 >> (_M0L6_2atmpS2677 & 31);
  _M0L6_2atmpS2674 = _M0L6_2atmpS2675 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2673 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS2674);
  _M0L1aS639 = _M0L6_2atmpS2672 + _M0L6_2atmpS2673;
  _M0L6_2atmpS2671 = _M0Lm5high1S637;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS640
  = _M0FPB13shiftright128(_M0L3sumS636, _M0L6_2atmpS2671, _M0L5deltaS638);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS639, .$1 = _M0L1bS640};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS606) {
  int32_t _M0L4baseS605;
  int32_t _M0L5base2S607;
  int32_t _M0L6offsetS608;
  int32_t _M0L6_2atmpS2669;
  uint64_t _M0L4mul0S609;
  int32_t _M0L6_2atmpS2668;
  int32_t _M0L6_2atmpS2667;
  uint64_t _M0L4mul1S610;
  uint64_t _M0L1mS611;
  struct _M0TPB7Umul128 _M0L7_2abindS612;
  uint64_t _M0L7_2alow1S613;
  uint64_t _M0L8_2ahigh1S614;
  struct _M0TPB7Umul128 _M0L7_2abindS615;
  uint64_t _M0L7_2alow0S616;
  uint64_t _M0L8_2ahigh0S617;
  uint64_t _M0L3sumS618;
  uint64_t _M0Lm5high1S619;
  int32_t _M0L6_2atmpS2665;
  int32_t _M0L6_2atmpS2666;
  int32_t _M0L5deltaS620;
  uint64_t _M0L6_2atmpS2657;
  int32_t _M0L6_2atmpS2664;
  uint32_t _M0L6_2atmpS2661;
  int32_t _M0L6_2atmpS2663;
  int32_t _M0L6_2atmpS2662;
  uint32_t _M0L6_2atmpS2660;
  uint32_t _M0L6_2atmpS2659;
  uint64_t _M0L6_2atmpS2658;
  uint64_t _M0L1aS621;
  uint64_t _M0L6_2atmpS2656;
  uint64_t _M0L1bS622;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS605 = _M0L1iS606 / 26;
  _M0L5base2S607 = _M0L4baseS605 * 26;
  _M0L6offsetS608 = _M0L1iS606 - _M0L5base2S607;
  _M0L6_2atmpS2669 = _M0L4baseS605 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S609
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS2669);
  _M0L6_2atmpS2668 = _M0L4baseS605 * 2;
  _M0L6_2atmpS2667 = _M0L6_2atmpS2668 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S610
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS2667);
  if (_M0L6offsetS608 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S609, .$1 = _M0L4mul1S610};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS611
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS608);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS612 = _M0FPB7umul128(_M0L1mS611, _M0L4mul1S610);
  _M0L7_2alow1S613 = _M0L7_2abindS612.$0;
  _M0L8_2ahigh1S614 = _M0L7_2abindS612.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS615 = _M0FPB7umul128(_M0L1mS611, _M0L4mul0S609);
  _M0L7_2alow0S616 = _M0L7_2abindS615.$0;
  _M0L8_2ahigh0S617 = _M0L7_2abindS615.$1;
  _M0L3sumS618 = _M0L8_2ahigh0S617 + _M0L7_2alow1S613;
  _M0Lm5high1S619 = _M0L8_2ahigh1S614;
  if (_M0L3sumS618 < _M0L8_2ahigh0S617) {
    uint64_t _M0L6_2atmpS2655 = _M0Lm5high1S619;
    _M0Lm5high1S619 = _M0L6_2atmpS2655 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2665 = _M0FPB8pow5bits(_M0L1iS606);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2666 = _M0FPB8pow5bits(_M0L5base2S607);
  _M0L5deltaS620 = _M0L6_2atmpS2665 - _M0L6_2atmpS2666;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2657
  = _M0FPB13shiftright128(_M0L7_2alow0S616, _M0L3sumS618, _M0L5deltaS620);
  _M0L6_2atmpS2664 = _M0L1iS606 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2661
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS2664);
  _M0L6_2atmpS2663 = _M0L1iS606 % 16;
  _M0L6_2atmpS2662 = _M0L6_2atmpS2663 << 1;
  _M0L6_2atmpS2660 = _M0L6_2atmpS2661 >> (_M0L6_2atmpS2662 & 31);
  _M0L6_2atmpS2659 = _M0L6_2atmpS2660 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2658 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS2659);
  _M0L1aS621 = _M0L6_2atmpS2657 + _M0L6_2atmpS2658;
  _M0L6_2atmpS2656 = _M0Lm5high1S619;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS622
  = _M0FPB13shiftright128(_M0L3sumS618, _M0L6_2atmpS2656, _M0L5deltaS620);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS621, .$1 = _M0L1bS622};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS579,
  struct _M0TPB8Pow5Pair _M0L3mulS576,
  int32_t _M0L1jS592,
  int32_t _M0L7mmShiftS594
) {
  uint64_t _M0L7_2amul0S575;
  uint64_t _M0L7_2amul1S577;
  uint64_t _M0L1mS578;
  struct _M0TPB7Umul128 _M0L7_2abindS580;
  uint64_t _M0L5_2aloS581;
  uint64_t _M0L6_2atmpS582;
  struct _M0TPB7Umul128 _M0L7_2abindS583;
  uint64_t _M0L6_2alo2S584;
  uint64_t _M0L6_2ahi2S585;
  uint64_t _M0L3midS586;
  uint64_t _M0L6_2atmpS2654;
  uint64_t _M0L2hiS587;
  uint64_t _M0L3lo2S588;
  uint64_t _M0L6_2atmpS2652;
  uint64_t _M0L6_2atmpS2653;
  uint64_t _M0L4mid2S589;
  uint64_t _M0L6_2atmpS2651;
  uint64_t _M0L3hi2S590;
  int32_t _M0L6_2atmpS2650;
  int32_t _M0L6_2atmpS2649;
  uint64_t _M0L2vpS591;
  uint64_t _M0Lm2vmS593;
  int32_t _M0L6_2atmpS2648;
  int32_t _M0L6_2atmpS2647;
  uint64_t _M0L2vrS604;
  uint64_t _M0L6_2atmpS2646;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S575 = _M0L3mulS576.$0;
  _M0L7_2amul1S577 = _M0L3mulS576.$1;
  _M0L1mS578 = _M0L1mS579 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS580 = _M0FPB7umul128(_M0L1mS578, _M0L7_2amul0S575);
  _M0L5_2aloS581 = _M0L7_2abindS580.$0;
  _M0L6_2atmpS582 = _M0L7_2abindS580.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS583 = _M0FPB7umul128(_M0L1mS578, _M0L7_2amul1S577);
  _M0L6_2alo2S584 = _M0L7_2abindS583.$0;
  _M0L6_2ahi2S585 = _M0L7_2abindS583.$1;
  _M0L3midS586 = _M0L6_2atmpS582 + _M0L6_2alo2S584;
  if (_M0L3midS586 < _M0L6_2atmpS582) {
    _M0L6_2atmpS2654 = 1ull;
  } else {
    _M0L6_2atmpS2654 = 0ull;
  }
  _M0L2hiS587 = _M0L6_2ahi2S585 + _M0L6_2atmpS2654;
  _M0L3lo2S588 = _M0L5_2aloS581 + _M0L7_2amul0S575;
  _M0L6_2atmpS2652 = _M0L3midS586 + _M0L7_2amul1S577;
  if (_M0L3lo2S588 < _M0L5_2aloS581) {
    _M0L6_2atmpS2653 = 1ull;
  } else {
    _M0L6_2atmpS2653 = 0ull;
  }
  _M0L4mid2S589 = _M0L6_2atmpS2652 + _M0L6_2atmpS2653;
  if (_M0L4mid2S589 < _M0L3midS586) {
    _M0L6_2atmpS2651 = 1ull;
  } else {
    _M0L6_2atmpS2651 = 0ull;
  }
  _M0L3hi2S590 = _M0L2hiS587 + _M0L6_2atmpS2651;
  _M0L6_2atmpS2650 = _M0L1jS592 - 64;
  _M0L6_2atmpS2649 = _M0L6_2atmpS2650 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS591
  = _M0FPB13shiftright128(_M0L4mid2S589, _M0L3hi2S590, _M0L6_2atmpS2649);
  _M0Lm2vmS593 = 0ull;
  if (_M0L7mmShiftS594) {
    uint64_t _M0L3lo3S595 = _M0L5_2aloS581 - _M0L7_2amul0S575;
    uint64_t _M0L6_2atmpS2636 = _M0L3midS586 - _M0L7_2amul1S577;
    uint64_t _M0L6_2atmpS2637;
    uint64_t _M0L4mid3S596;
    uint64_t _M0L6_2atmpS2635;
    uint64_t _M0L3hi3S597;
    int32_t _M0L6_2atmpS2634;
    int32_t _M0L6_2atmpS2633;
    if (_M0L5_2aloS581 < _M0L3lo3S595) {
      _M0L6_2atmpS2637 = 1ull;
    } else {
      _M0L6_2atmpS2637 = 0ull;
    }
    _M0L4mid3S596 = _M0L6_2atmpS2636 - _M0L6_2atmpS2637;
    if (_M0L3midS586 < _M0L4mid3S596) {
      _M0L6_2atmpS2635 = 1ull;
    } else {
      _M0L6_2atmpS2635 = 0ull;
    }
    _M0L3hi3S597 = _M0L2hiS587 - _M0L6_2atmpS2635;
    _M0L6_2atmpS2634 = _M0L1jS592 - 64;
    _M0L6_2atmpS2633 = _M0L6_2atmpS2634 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS593
    = _M0FPB13shiftright128(_M0L4mid3S596, _M0L3hi3S597, _M0L6_2atmpS2633);
  } else {
    uint64_t _M0L3lo3S598 = _M0L5_2aloS581 + _M0L5_2aloS581;
    uint64_t _M0L6_2atmpS2644 = _M0L3midS586 + _M0L3midS586;
    uint64_t _M0L6_2atmpS2645;
    uint64_t _M0L4mid3S599;
    uint64_t _M0L6_2atmpS2642;
    uint64_t _M0L6_2atmpS2643;
    uint64_t _M0L3hi3S600;
    uint64_t _M0L3lo4S601;
    uint64_t _M0L6_2atmpS2640;
    uint64_t _M0L6_2atmpS2641;
    uint64_t _M0L4mid4S602;
    uint64_t _M0L6_2atmpS2639;
    uint64_t _M0L3hi4S603;
    int32_t _M0L6_2atmpS2638;
    if (_M0L3lo3S598 < _M0L5_2aloS581) {
      _M0L6_2atmpS2645 = 1ull;
    } else {
      _M0L6_2atmpS2645 = 0ull;
    }
    _M0L4mid3S599 = _M0L6_2atmpS2644 + _M0L6_2atmpS2645;
    _M0L6_2atmpS2642 = _M0L2hiS587 + _M0L2hiS587;
    if (_M0L4mid3S599 < _M0L3midS586) {
      _M0L6_2atmpS2643 = 1ull;
    } else {
      _M0L6_2atmpS2643 = 0ull;
    }
    _M0L3hi3S600 = _M0L6_2atmpS2642 + _M0L6_2atmpS2643;
    _M0L3lo4S601 = _M0L3lo3S598 - _M0L7_2amul0S575;
    _M0L6_2atmpS2640 = _M0L4mid3S599 - _M0L7_2amul1S577;
    if (_M0L3lo3S598 < _M0L3lo4S601) {
      _M0L6_2atmpS2641 = 1ull;
    } else {
      _M0L6_2atmpS2641 = 0ull;
    }
    _M0L4mid4S602 = _M0L6_2atmpS2640 - _M0L6_2atmpS2641;
    if (_M0L4mid3S599 < _M0L4mid4S602) {
      _M0L6_2atmpS2639 = 1ull;
    } else {
      _M0L6_2atmpS2639 = 0ull;
    }
    _M0L3hi4S603 = _M0L3hi3S600 - _M0L6_2atmpS2639;
    _M0L6_2atmpS2638 = _M0L1jS592 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS593
    = _M0FPB13shiftright128(_M0L4mid4S602, _M0L3hi4S603, _M0L6_2atmpS2638);
  }
  _M0L6_2atmpS2648 = _M0L1jS592 - 64;
  _M0L6_2atmpS2647 = _M0L6_2atmpS2648 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS604
  = _M0FPB13shiftright128(_M0L3midS586, _M0L2hiS587, _M0L6_2atmpS2647);
  _M0L6_2atmpS2646 = _M0Lm2vmS593;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS604,
                                                .$1 = _M0L2vpS591,
                                                .$2 = _M0L6_2atmpS2646};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS573,
  int32_t _M0L1pS574
) {
  uint64_t _M0L6_2atmpS2632;
  uint64_t _M0L6_2atmpS2631;
  uint64_t _M0L6_2atmpS2630;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2632 = 1ull << (_M0L1pS574 & 63);
  _M0L6_2atmpS2631 = _M0L6_2atmpS2632 - 1ull;
  _M0L6_2atmpS2630 = _M0L5valueS573 & _M0L6_2atmpS2631;
  return _M0L6_2atmpS2630 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS571,
  int32_t _M0L1pS572
) {
  int32_t _M0L6_2atmpS2629;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2629 = _M0FPB10pow5Factor(_M0L5valueS571);
  return _M0L6_2atmpS2629 >= _M0L1pS572;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS566) {
  uint64_t _M0L6_2atmpS2620;
  uint64_t _M0L6_2atmpS2621;
  uint64_t _M0L6_2atmpS2622;
  uint64_t _M0L6_2atmpS2623;
  uint64_t _M0L6_2atmpS2628;
  int32_t _M0L5countS567;
  uint64_t _M0L1vS568;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2620 = _M0L5valueS566 % 5ull;
  if (_M0L6_2atmpS2620 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2621 = _M0L5valueS566 % 25ull;
  if (_M0L6_2atmpS2621 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS2622 = _M0L5valueS566 % 125ull;
  if (_M0L6_2atmpS2622 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS2623 = _M0L5valueS566 % 625ull;
  if (_M0L6_2atmpS2623 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS2628 = _M0L5valueS566 / 625ull;
  _M0L5countS567 = 4;
  _M0L1vS568 = _M0L6_2atmpS2628;
  while (1) {
    if (_M0L1vS568 > 0ull) {
      uint64_t _M0L6_2atmpS2624 = _M0L1vS568 % 5ull;
      int32_t _M0L6_2atmpS2625;
      uint64_t _M0L6_2atmpS2626;
      if (_M0L6_2atmpS2624 != 0ull) {
        return _M0L5countS567;
      }
      _M0L6_2atmpS2625 = _M0L5countS567 + 1;
      _M0L6_2atmpS2626 = _M0L1vS568 / 5ull;
      _M0L5countS567 = _M0L6_2atmpS2625;
      _M0L1vS568 = _M0L6_2atmpS2626;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS570;
      moonbit_string_t _M0L6_2atmpS2627;
      int32_t _result_6121;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS570
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS570, (moonbit_string_t)moonbit_string_literal_13.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS570, _M0L5valueS566);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS2627
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS570);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS570);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_6121 = _M0FPC15abort5abortGiE(_M0L6_2atmpS2627);
      moonbit_decref_cycle_free(_M0L6_2atmpS2627);
      return _result_6121;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS565,
  uint64_t _M0L2hiS563,
  int32_t _M0L4distS564
) {
  int32_t _M0L6_2atmpS2619;
  uint64_t _M0L6_2atmpS2617;
  uint64_t _M0L6_2atmpS2618;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2619 = 64 - _M0L4distS564;
  _M0L6_2atmpS2617 = _M0L2hiS563 << (_M0L6_2atmpS2619 & 63);
  _M0L6_2atmpS2618 = _M0L2loS565 >> (_M0L4distS564 & 63);
  return _M0L6_2atmpS2617 | _M0L6_2atmpS2618;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS553,
  uint64_t _M0L1bS556
) {
  uint64_t _M0L3aLoS552;
  uint64_t _M0L3aHiS554;
  uint64_t _M0L3bLoS555;
  uint64_t _M0L3bHiS557;
  uint64_t _M0L1xS558;
  uint64_t _M0L6_2atmpS2615;
  uint64_t _M0L6_2atmpS2616;
  uint64_t _M0L1yS559;
  uint64_t _M0L6_2atmpS2613;
  uint64_t _M0L6_2atmpS2614;
  uint64_t _M0L1zS560;
  uint64_t _M0L6_2atmpS2611;
  uint64_t _M0L6_2atmpS2612;
  uint64_t _M0L6_2atmpS2609;
  uint64_t _M0L6_2atmpS2610;
  uint64_t _M0L1wS561;
  uint64_t _M0L2loS562;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS552 = _M0L1aS553 & 4294967295ull;
  _M0L3aHiS554 = _M0L1aS553 >> 32;
  _M0L3bLoS555 = _M0L1bS556 & 4294967295ull;
  _M0L3bHiS557 = _M0L1bS556 >> 32;
  _M0L1xS558 = _M0L3aLoS552 * _M0L3bLoS555;
  _M0L6_2atmpS2615 = _M0L3aHiS554 * _M0L3bLoS555;
  _M0L6_2atmpS2616 = _M0L1xS558 >> 32;
  _M0L1yS559 = _M0L6_2atmpS2615 + _M0L6_2atmpS2616;
  _M0L6_2atmpS2613 = _M0L3aLoS552 * _M0L3bHiS557;
  _M0L6_2atmpS2614 = _M0L1yS559 & 4294967295ull;
  _M0L1zS560 = _M0L6_2atmpS2613 + _M0L6_2atmpS2614;
  _M0L6_2atmpS2611 = _M0L3aHiS554 * _M0L3bHiS557;
  _M0L6_2atmpS2612 = _M0L1yS559 >> 32;
  _M0L6_2atmpS2609 = _M0L6_2atmpS2611 + _M0L6_2atmpS2612;
  _M0L6_2atmpS2610 = _M0L1zS560 >> 32;
  _M0L1wS561 = _M0L6_2atmpS2609 + _M0L6_2atmpS2610;
  _M0L2loS562 = _M0L1aS553 * _M0L1bS556;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS562, .$1 = _M0L1wS561};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS550,
  int32_t _M0L4fromS547,
  int32_t _M0L2toS546
) {
  int32_t _M0L3lenS545;
  int32_t _M0L6_2atmpS2608;
  uint16_t* _M0L6bufferS548;
  int32_t _M0L1iS549;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS545 = _M0L2toS546 - _M0L4fromS547;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2608 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS548
  = (uint16_t*)moonbit_make_string(_M0L3lenS545, _M0L6_2atmpS2608);
  _M0L1iS549 = 0;
  while (1) {
    if (_M0L1iS549 < _M0L3lenS545) {
      int32_t _M0L6_2atmpS2606 = _M0L4fromS547 + _M0L1iS549;
      int32_t _M0L6_2atmpS2605;
      int32_t _M0L6_2atmpS2604;
      int32_t _M0L6_2atmpS2607;
      if (
        _M0L6_2atmpS2606 < 0
        || _M0L6_2atmpS2606 >= Moonbit_array_length(_M0L5bytesS550)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2605 = (int32_t)_M0L5bytesS550[_M0L6_2atmpS2606];
      _M0L6_2atmpS2604 = (uint16_t)_M0L6_2atmpS2605;
      if (
        _M0L1iS549 < 0 || _M0L1iS549 >= Moonbit_array_length(_M0L6bufferS548)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS548[_M0L1iS549] = _M0L6_2atmpS2604;
      _M0L6_2atmpS2607 = _M0L1iS549 + 1;
      _M0L1iS549 = _M0L6_2atmpS2607;
      continue;
    }
    break;
  }
  return _M0L6bufferS548;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS544) {
  int32_t _M0L6_2atmpS2603;
  uint32_t _M0L6_2atmpS2602;
  uint32_t _M0L6_2atmpS2601;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2603 = _M0L1eS544 * 78913;
  _M0L6_2atmpS2602 = *(uint32_t*)&_M0L6_2atmpS2603;
  _M0L6_2atmpS2601 = _M0L6_2atmpS2602 >> 18;
  return *(int32_t*)&_M0L6_2atmpS2601;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS543) {
  int32_t _M0L6_2atmpS2600;
  uint32_t _M0L6_2atmpS2599;
  uint32_t _M0L6_2atmpS2598;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2600 = _M0L1eS543 * 732923;
  _M0L6_2atmpS2599 = *(uint32_t*)&_M0L6_2atmpS2600;
  _M0L6_2atmpS2598 = _M0L6_2atmpS2599 >> 20;
  return *(int32_t*)&_M0L6_2atmpS2598;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS541,
  int32_t _M0L8exponentS542,
  int32_t _M0L8mantissaS539
) {
  moonbit_string_t _M0L1sS540;
  moonbit_string_t _result_6124;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS539) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  if (_M0L4signS541) {
    _M0L1sS540 = (moonbit_string_t)moonbit_string_literal_15.data;
  } else {
    _M0L1sS540 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS542) {
    moonbit_string_t _result_6123;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_6123
    = moonbit_add_string(_M0L1sS540, (moonbit_string_t)moonbit_string_literal_16.data);
    moonbit_decref_cycle_free(_M0L1sS540);
    return _result_6123;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_6124
  = moonbit_add_string(_M0L1sS540, (moonbit_string_t)moonbit_string_literal_17.data);
  moonbit_decref_cycle_free(_M0L1sS540);
  return _result_6124;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS538) {
  int32_t _M0L6_2atmpS2597;
  uint32_t _M0L6_2atmpS2596;
  uint32_t _M0L6_2atmpS2595;
  int32_t _M0L6_2atmpS2594;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2597 = _M0L1eS538 * 1217359;
  _M0L6_2atmpS2596 = *(uint32_t*)&_M0L6_2atmpS2597;
  _M0L6_2atmpS2595 = _M0L6_2atmpS2596 >> 19;
  _M0L6_2atmpS2594 = *(int32_t*)&_M0L6_2atmpS2595;
  return _M0L6_2atmpS2594 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS537) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS537 != _M0L4selfS537) {
    return 0;
  } else if (_M0L4selfS537 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS537 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS537;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS536) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS536 != _M0L4selfS536) {
    return 0ll;
  } else if (_M0L4selfS536 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS536 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS536;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS532
) {
  float* _M0L6_2atmpS2590;
  struct _M0TPB5ArrayGfE* _block_6125;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2590 = (float*)moonbit_make_float_array_raw(_M0L3lenS532);
  _block_6125
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_6125)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _block_6125->$0 = _M0L6_2atmpS2590;
  _block_6125->$1 = _M0L3lenS532;
  return _block_6125;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS533
) {
  uint8_t* _M0L6_2atmpS2591;
  struct _M0TPB5ArrayGbE* _block_6126;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2591 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS533);
  _block_6126
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_6126)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 92, 0);
  _block_6126->$0 = _M0L6_2atmpS2591;
  _block_6126->$1 = _M0L3lenS533;
  return _block_6126;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS534
) {
  int32_t* _M0L6_2atmpS2592;
  struct _M0TPB5ArrayGiE* _block_6127;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2592 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS534);
  _block_6127
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_6127)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _block_6127->$0 = _M0L6_2atmpS2592;
  _block_6127->$1 = _M0L3lenS534;
  return _block_6127;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS535
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS2593;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_6128;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2593
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS535, 0);
  _block_6128
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_6128)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 102, 0);
  _block_6128->$0 = _M0L6_2atmpS2593;
  _block_6128->$1 = _M0L3lenS535;
  return _block_6128;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS528,
  int32_t _M0L5indexS529
) {
  uint64_t* _M0L6_2atmpS2588;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS2588 = _M0L4selfS528;
  if (
    _M0L5indexS529 < 0
    || _M0L5indexS529 >= Moonbit_array_length(_M0L6_2atmpS2588)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS2588[_M0L5indexS529];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS530,
  int32_t _M0L5indexS531
) {
  uint32_t* _M0L6_2atmpS2589;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS2589 = _M0L4selfS530;
  if (
    _M0L5indexS531 < 0
    || _M0L5indexS531 >= Moonbit_array_length(_M0L6_2atmpS2589)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS2589[_M0L5indexS531];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS527
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS527, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS526) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS526, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS525) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS525;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS513,
  int32_t _M0L5valueS515
) {
  int32_t _M0L3lenS2560;
  int32_t* _M0L6_2atmpS2562;
  int32_t _M0L6_2atmpS2561;
  int32_t _M0L6lengthS514;
  int32_t* _M0L3bufS2565;
  int32_t _M0L6_2atmpS2566;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2560 = _M0L4selfS513->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2562 = _M0MPC15array5Array6bufferGiE(_M0L4selfS513);
  _M0L6_2atmpS2561 = Moonbit_array_length(_M0L6_2atmpS2562);
  moonbit_decref_cycle_free(_M0L6_2atmpS2562);
  if (_M0L3lenS2560 == _M0L6_2atmpS2561) {
    int32_t _M0L3lenS2564 = _M0L4selfS513->$1;
    int32_t _M0L6_2atmpS2563 = _M0L3lenS2564 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS513, _M0L6_2atmpS2563);
  }
  _M0L6lengthS514 = _M0L4selfS513->$1;
  _M0L3bufS2565 = _M0L4selfS513->$0;
  _M0L3bufS2565[_M0L6lengthS514] = _M0L5valueS515;
  _M0L6_2atmpS2566 = _M0L6lengthS514 + 1;
  _M0L4selfS513->$1 = _M0L6_2atmpS2566;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS516,
  moonbit_string_t _M0L5valueS518
) {
  int32_t _M0L3lenS2567;
  moonbit_string_t* _M0L6_2atmpS2569;
  int32_t _M0L6_2atmpS2568;
  int32_t _M0L6lengthS517;
  moonbit_string_t* _M0L3bufS2572;
  moonbit_string_t _M0L6_2aoldS5677;
  int32_t _M0L6_2atmpS2573;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2567 = _M0L4selfS516->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2569 = _M0MPC15array5Array6bufferGsE(_M0L4selfS516);
  _M0L6_2atmpS2568 = Moonbit_array_length(_M0L6_2atmpS2569);
  moonbit_decref_cycle_free(_M0L6_2atmpS2569);
  if (_M0L3lenS2567 == _M0L6_2atmpS2568) {
    int32_t _M0L3lenS2571 = _M0L4selfS516->$1;
    int32_t _M0L6_2atmpS2570 = _M0L3lenS2571 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS516, _M0L6_2atmpS2570);
  }
  _M0L6lengthS517 = _M0L4selfS516->$1;
  _M0L3bufS2572 = _M0L4selfS516->$0;
  _M0L6_2aoldS5677 = (moonbit_string_t)_M0L3bufS2572[_M0L6lengthS517];
  moonbit_decref_cycle_free(_M0L6_2aoldS5677);
  _M0L3bufS2572[_M0L6lengthS517] = _M0L5valueS518;
  _M0L6_2atmpS2573 = _M0L6lengthS517 + 1;
  _M0L4selfS516->$1 = _M0L6_2atmpS2573;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS519,
  struct _M0TUsiE* _M0L5valueS521
) {
  int32_t _M0L3lenS2574;
  struct _M0TUsiE** _M0L6_2atmpS2576;
  int32_t _M0L6_2atmpS2575;
  int32_t _M0L6lengthS520;
  struct _M0TUsiE** _M0L3bufS2579;
  struct _M0TUsiE* _M0L6_2aoldS5678;
  int32_t _M0L6_2atmpS2580;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2574 = _M0L4selfS519->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2576 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS519);
  _M0L6_2atmpS2575 = Moonbit_array_length(_M0L6_2atmpS2576);
  moonbit_decref_cycle_free(_M0L6_2atmpS2576);
  if (_M0L3lenS2574 == _M0L6_2atmpS2575) {
    int32_t _M0L3lenS2578 = _M0L4selfS519->$1;
    int32_t _M0L6_2atmpS2577 = _M0L3lenS2578 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS519, _M0L6_2atmpS2577);
  }
  _M0L6lengthS520 = _M0L4selfS519->$1;
  _M0L3bufS2579 = _M0L4selfS519->$0;
  _M0L6_2aoldS5678 = (struct _M0TUsiE*)_M0L3bufS2579[_M0L6lengthS520];
  if (_M0L6_2aoldS5678) {
    moonbit_decref_cycle_free(_M0L6_2aoldS5678);
  }
  _M0L3bufS2579[_M0L6lengthS520] = _M0L5valueS521;
  _M0L6_2atmpS2580 = _M0L6lengthS520 + 1;
  _M0L4selfS519->$1 = _M0L6_2atmpS2580;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS522,
  float _M0L5valueS524
) {
  int32_t _M0L3lenS2581;
  float* _M0L6_2atmpS2583;
  int32_t _M0L6_2atmpS2582;
  int32_t _M0L6lengthS523;
  float* _M0L3bufS2586;
  int32_t _M0L6_2atmpS2587;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2581 = _M0L4selfS522->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2583 = _M0MPC15array5Array6bufferGfE(_M0L4selfS522);
  _M0L6_2atmpS2582 = Moonbit_array_length(_M0L6_2atmpS2583);
  moonbit_decref_cycle_free(_M0L6_2atmpS2583);
  if (_M0L3lenS2581 == _M0L6_2atmpS2582) {
    int32_t _M0L3lenS2585 = _M0L4selfS522->$1;
    int32_t _M0L6_2atmpS2584 = _M0L3lenS2585 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS522, _M0L6_2atmpS2584);
  }
  _M0L6lengthS523 = _M0L4selfS522->$1;
  _M0L3bufS2586 = _M0L4selfS522->$0;
  _M0L3bufS2586[_M0L6lengthS523] = _M0L5valueS524;
  _M0L6_2atmpS2587 = _M0L6lengthS523 + 1;
  _M0L4selfS522->$1 = _M0L6_2atmpS2587;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS498,
  int32_t _M0L8requiredS500
) {
  int32_t _M0L8old__capS497;
  int32_t _M0L3lenS2556;
  int32_t _M0L8new__capS499;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS497 = _M0MPC15array5Array8capacityGiE(_M0L4selfS498);
  _M0L3lenS2556 = _M0L4selfS498->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS499
  = _M0FPB23array__growth__capacity(_M0L8old__capS497, _M0L3lenS2556, _M0L8requiredS500);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS498, _M0L8new__capS499);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS502,
  int32_t _M0L8requiredS504
) {
  int32_t _M0L8old__capS501;
  int32_t _M0L3lenS2557;
  int32_t _M0L8new__capS503;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS501 = _M0MPC15array5Array8capacityGsE(_M0L4selfS502);
  _M0L3lenS2557 = _M0L4selfS502->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS503
  = _M0FPB23array__growth__capacity(_M0L8old__capS501, _M0L3lenS2557, _M0L8requiredS504);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS502, _M0L8new__capS503);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS506,
  int32_t _M0L8requiredS508
) {
  int32_t _M0L8old__capS505;
  int32_t _M0L3lenS2558;
  int32_t _M0L8new__capS507;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS505 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS506);
  _M0L3lenS2558 = _M0L4selfS506->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS507
  = _M0FPB23array__growth__capacity(_M0L8old__capS505, _M0L3lenS2558, _M0L8requiredS508);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS506, _M0L8new__capS507);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS510,
  int32_t _M0L8requiredS512
) {
  int32_t _M0L8old__capS509;
  int32_t _M0L3lenS2559;
  int32_t _M0L8new__capS511;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS509 = _M0MPC15array5Array8capacityGfE(_M0L4selfS510);
  _M0L3lenS2559 = _M0L4selfS510->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS511
  = _M0FPB23array__growth__capacity(_M0L8old__capS509, _M0L3lenS2559, _M0L8requiredS512);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS510, _M0L8new__capS511);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS474,
  int32_t _M0L13new__capacityS477
) {
  int32_t* _M0L8old__bufS473;
  int32_t _M0L3lenS475;
  int32_t _M0L9copy__lenS476;
  int32_t* _M0L8new__bufS478;
  int32_t* _M0L6_2aoldS5679;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS473 = _M0L4selfS474->$0;
  _M0L3lenS475 = _M0L4selfS474->$1;
  if (_M0L3lenS475 < _M0L13new__capacityS477) {
    _M0L9copy__lenS476 = _M0L3lenS475;
  } else {
    _M0L9copy__lenS476 = _M0L13new__capacityS477;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS473);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS478
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS473, _M0L13new__capacityS477, _M0L9copy__lenS476, 0, 0);
  _M0L6_2aoldS5679 = _M0L4selfS474->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5679);
  _M0L4selfS474->$0 = _M0L8new__bufS478;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS480,
  int32_t _M0L13new__capacityS483
) {
  moonbit_string_t* _M0L8old__bufS479;
  int32_t _M0L3lenS481;
  int32_t _M0L9copy__lenS482;
  moonbit_string_t* _M0L8new__bufS484;
  moonbit_string_t* _M0L6_2aoldS5680;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS479 = _M0L4selfS480->$0;
  _M0L3lenS481 = _M0L4selfS480->$1;
  if (_M0L3lenS481 < _M0L13new__capacityS483) {
    _M0L9copy__lenS482 = _M0L3lenS481;
  } else {
    _M0L9copy__lenS482 = _M0L13new__capacityS483;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS479);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS484
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS479, _M0L13new__capacityS483, _M0L9copy__lenS482, 0, 0);
  _M0L6_2aoldS5680 = _M0L4selfS480->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5680);
  _M0L4selfS480->$0 = _M0L8new__bufS484;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS486,
  int32_t _M0L13new__capacityS489
) {
  struct _M0TUsiE** _M0L8old__bufS485;
  int32_t _M0L3lenS487;
  int32_t _M0L9copy__lenS488;
  struct _M0TUsiE** _M0L8new__bufS490;
  struct _M0TUsiE** _M0L6_2aoldS5681;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS485 = _M0L4selfS486->$0;
  _M0L3lenS487 = _M0L4selfS486->$1;
  if (_M0L3lenS487 < _M0L13new__capacityS489) {
    _M0L9copy__lenS488 = _M0L3lenS487;
  } else {
    _M0L9copy__lenS488 = _M0L13new__capacityS489;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS485);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS490
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS485, _M0L13new__capacityS489, _M0L9copy__lenS488, 0, 0);
  _M0L6_2aoldS5681 = _M0L4selfS486->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5681);
  _M0L4selfS486->$0 = _M0L8new__bufS490;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS492,
  int32_t _M0L13new__capacityS495
) {
  float* _M0L8old__bufS491;
  int32_t _M0L3lenS493;
  int32_t _M0L9copy__lenS494;
  float* _M0L8new__bufS496;
  float* _M0L6_2aoldS5682;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS491 = _M0L4selfS492->$0;
  _M0L3lenS493 = _M0L4selfS492->$1;
  if (_M0L3lenS493 < _M0L13new__capacityS495) {
    _M0L9copy__lenS494 = _M0L3lenS493;
  } else {
    _M0L9copy__lenS494 = _M0L13new__capacityS495;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS491);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS496
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS491, _M0L13new__capacityS495, _M0L9copy__lenS494, 0, 0);
  _M0L6_2aoldS5682 = _M0L4selfS492->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5682);
  _M0L4selfS492->$0 = _M0L8new__bufS496;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS469
) {
  int32_t* _M0L6_2atmpS2552;
  int32_t _result_6129;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2552 = _M0MPC15array5Array6bufferGiE(_M0L4selfS469);
  _result_6129 = Moonbit_array_length(_M0L6_2atmpS2552);
  moonbit_decref_cycle_free(_M0L6_2atmpS2552);
  return _result_6129;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS470
) {
  moonbit_string_t* _M0L6_2atmpS2553;
  int32_t _result_6130;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2553 = _M0MPC15array5Array6bufferGsE(_M0L4selfS470);
  _result_6130 = Moonbit_array_length(_M0L6_2atmpS2553);
  moonbit_decref_cycle_free(_M0L6_2atmpS2553);
  return _result_6130;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS471
) {
  struct _M0TUsiE** _M0L6_2atmpS2554;
  int32_t _result_6131;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2554 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS471);
  _result_6131 = Moonbit_array_length(_M0L6_2atmpS2554);
  moonbit_decref_cycle_free(_M0L6_2atmpS2554);
  return _result_6131;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS472
) {
  float* _M0L6_2atmpS2555;
  int32_t _result_6132;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2555 = _M0MPC15array5Array6bufferGfE(_M0L4selfS472);
  _result_6132 = Moonbit_array_length(_M0L6_2atmpS2555);
  moonbit_decref_cycle_free(_M0L6_2atmpS2555);
  return _result_6132;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS465,
  int32_t _M0L3lenS463,
  int32_t _M0L8requiredS462
) {
  int32_t _M0L5startS464;
  int32_t _M0L5spaceS466;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS462 < _M0L3lenS463) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_18.data);
  }
  if (_M0L7currentS465 == 0) {
    _M0L5startS464 = 8;
  } else {
    _M0L5startS464 = _M0L7currentS465;
  }
  _M0L5spaceS466 = _M0L5startS464;
  while (1) {
    if (_M0L5spaceS466 < _M0L8requiredS462) {
      int32_t _M0L4nextS467 = _M0L5spaceS466 * 2;
      if (_M0L4nextS467 <= _M0L5spaceS466) {
        return _M0L8requiredS462;
      }
      _M0L5spaceS466 = _M0L4nextS467;
      continue;
    } else {
      return _M0L5spaceS466;
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

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE* _M0L4selfS461) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS461->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS452) {
  float* _M0L8_2afieldS5683;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5683 = _M0L4selfS452->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5683);
  return _M0L8_2afieldS5683;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS453) {
  int32_t* _M0L8_2afieldS5684;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5684 = _M0L4selfS453->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5684);
  return _M0L8_2afieldS5684;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS454
) {
  moonbit_string_t* _M0L8_2afieldS5685;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5685 = _M0L4selfS454->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5685);
  return _M0L8_2afieldS5685;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS455
) {
  struct _M0TUsiE** _M0L8_2afieldS5686;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5686 = _M0L4selfS455->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5686);
  return _M0L8_2afieldS5686;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L4selfS456
) {
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L8_2afieldS5687;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5687 = _M0L4selfS456->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5687);
  return _M0L8_2afieldS5687;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS457
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS5688;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5688 = _M0L4selfS457->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5688);
  return _M0L8_2afieldS5688;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS458) {
  uint8_t* _M0L8_2afieldS5689;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5689 = _M0L4selfS458->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5689);
  return _M0L8_2afieldS5689;
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
  int32_t _M0L3endS2550;
  int32_t _M0L5startS2551;
  int32_t _M0L8str__lenS447;
  int32_t _M0L3lenS2549;
  int32_t _M0L8requiredS449;
  uint16_t* _M0L4dataS2542;
  int32_t _M0L6_2atmpS2541;
  int32_t _if__result_6134;
  uint16_t* _M0L4dataS2543;
  int32_t _M0L3lenS2544;
  moonbit_string_t _M0L6_2atmpS2545;
  int32_t _M0L6_2atmpS2546;
  int32_t _M0L3lenS2548;
  int32_t _M0L6_2atmpS2547;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS2550 = _M0L3strS448.$2;
  _M0L5startS2551 = _M0L3strS448.$1;
  _M0L8str__lenS447 = _M0L3endS2550 - _M0L5startS2551;
  if (_M0L8str__lenS447 == 0) {
    return 0;
  }
  _M0L3lenS2549 = _M0L4selfS450->$1;
  _M0L8requiredS449 = _M0L3lenS2549 + _M0L8str__lenS447;
  _M0L4dataS2542 = _M0L4selfS450->$0;
  _M0L6_2atmpS2541 = Moonbit_array_length(_M0L4dataS2542);
  if (_M0L8requiredS449 > _M0L6_2atmpS2541) {
    _if__result_6134 = 1;
  } else {
    int32_t _M0L3lenS2540 = _M0L4selfS450->$1;
    _if__result_6134 = _M0L8requiredS449 < _M0L3lenS2540;
  }
  if (_if__result_6134) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS450, _M0L8requiredS449);
  }
  _M0L4dataS2543 = _M0L4selfS450->$0;
  _M0L3lenS2544 = _M0L4selfS450->$1;
  moonbit_incref_cycle_free(_M0L4dataS2543);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2545 = _M0MPC16string10StringView4data(_M0L3strS448);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2546 = _M0MPC16string10StringView13start__offset(_M0L3strS448);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS2543, _M0L3lenS2544, _M0L6_2atmpS2545, _M0L6_2atmpS2546, _M0L8str__lenS447);
  moonbit_decref_cycle_free(_M0L4dataS2543);
  moonbit_decref_cycle_free(_M0L6_2atmpS2545);
  _M0L3lenS2548 = _M0L4selfS450->$1;
  _M0L6_2atmpS2547 = _M0L3lenS2548 + _M0L8str__lenS447;
  _M0L4selfS450->$1 = _M0L6_2atmpS2547;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS444,
  int32_t _M0L5startS442,
  int32_t _M0L3endS443
) {
  int32_t _if__result_6135;
  int32_t _M0L3lenS445;
  int32_t _M0L6_2atmpS2539;
  moonbit_bytes_t _M0L5bytesS446;
  moonbit_bytes_t _M0L6_2atmpS2538;
  moonbit_string_t _result_6136;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS442 == 0) {
    int32_t _M0L6_2atmpS2537 = Moonbit_array_length(_M0L3strS444);
    _if__result_6135 = _M0L3endS443 == _M0L6_2atmpS2537;
  } else {
    _if__result_6135 = 0;
  }
  if (_if__result_6135) {
    moonbit_incref_cycle_free(_M0L3strS444);
    return _M0L3strS444;
  }
  _M0L3lenS445 = _M0L3endS443 - _M0L5startS442;
  _M0L6_2atmpS2539 = _M0L3lenS445 * 2;
  _M0L5bytesS446 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS2539, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS446, 0, _M0L3strS444, _M0L5startS442, _M0L3lenS445);
  _M0L6_2atmpS2538 = _M0L5bytesS446;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_6136
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS2538, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS2538);
  return _result_6136;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS437,
  int32_t _M0L6offsetS441,
  int64_t _M0L6lengthS439
) {
  int32_t _M0L3lenS436;
  int32_t _M0L6lengthS438;
  int32_t _if__result_6137;
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
      int32_t _M0L6_2atmpS2536 = _M0L6offsetS441 + _M0L6lengthS438;
      _if__result_6137 = _M0L6_2atmpS2536 <= _M0L3lenS436;
    } else {
      _if__result_6137 = 0;
    }
  } else {
    _if__result_6137 = 0;
  }
  if (_if__result_6137) {
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
  int32_t _M0L6_2atmpS2535;
  int32_t _M0L6_2atmpS2534;
  int32_t _M0L2e1S422;
  int32_t _M0L6_2atmpS2533;
  int32_t _M0L2e2S425;
  int32_t _M0L4len1S427;
  int32_t _M0L4len2S429;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS2535 = _M0L6lengthS424 * 2;
  _M0L6_2atmpS2534 = _M0L13bytes__offsetS423 + _M0L6_2atmpS2535;
  _M0L2e1S422 = _M0L6_2atmpS2534 - 1;
  _M0L6_2atmpS2533 = _M0L11str__offsetS426 + _M0L6lengthS424;
  _M0L2e2S425 = _M0L6_2atmpS2533 - 1;
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
        int32_t _M0L6_2atmpS2530 = _M0L3strS430[_M0L1iS432];
        int32_t _M0L6_2atmpS2529 = (int32_t)_M0L6_2atmpS2530;
        uint32_t _M0L1cS434 = *(uint32_t*)&_M0L6_2atmpS2529;
        uint32_t _M0L6_2atmpS2525 = _M0L1cS434 & 255u;
        int32_t _M0L6_2atmpS2524;
        int32_t _M0L6_2atmpS2526;
        uint32_t _M0L6_2atmpS2528;
        int32_t _M0L6_2atmpS2527;
        int32_t _M0L6_2atmpS2531;
        int32_t _M0L6_2atmpS2532;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS2524 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS2525);
        if (
          _M0L1jS433 < 0 || _M0L1jS433 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L1jS433] = _M0L6_2atmpS2524;
        _M0L6_2atmpS2526 = _M0L1jS433 + 1;
        _M0L6_2atmpS2528 = _M0L1cS434 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS2527 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS2528);
        if (
          _M0L6_2atmpS2526 < 0
          || _M0L6_2atmpS2526 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L6_2atmpS2526] = _M0L6_2atmpS2527;
        _M0L6_2atmpS2531 = _M0L1iS432 + 1;
        _M0L6_2atmpS2532 = _M0L1jS433 + 2;
        _M0L1iS432 = _M0L6_2atmpS2531;
        _M0L1jS433 = _M0L6_2atmpS2532;
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
  int32_t _M0L6_2atmpS2523;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2523 = *(int32_t*)&_M0L4selfS421;
  return _M0L6_2atmpS2523 & 0xff;
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
    int64_t _M0L6_2atmpS2522 = -_M0L4selfS396;
    _M0L3numS398 = *(uint64_t*)&_M0L6_2atmpS2522;
  } else {
    _M0L3numS398 = *(uint64_t*)&_M0L4selfS396;
  }
  switch (_M0L5radixS395) {
    case 10: {
      int32_t _M0L10digit__lenS400;
      int32_t _M0L6_2atmpS2519;
      int32_t _M0L10total__lenS401;
      uint16_t* _M0L6bufferS402;
      int32_t _M0L12digit__startS403;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS400 = _M0FPB12dec__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS2519 = 1;
      } else {
        _M0L6_2atmpS2519 = 0;
      }
      _M0L10total__lenS401 = _M0L10digit__lenS400 + _M0L6_2atmpS2519;
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
      int32_t _M0L6_2atmpS2520;
      int32_t _M0L10total__lenS405;
      uint16_t* _M0L6bufferS406;
      int32_t _M0L12digit__startS407;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS404 = _M0FPB12hex__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS2520 = 1;
      } else {
        _M0L6_2atmpS2520 = 0;
      }
      _M0L10total__lenS405 = _M0L10digit__lenS404 + _M0L6_2atmpS2520;
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
      int32_t _M0L6_2atmpS2521;
      int32_t _M0L10total__lenS409;
      uint16_t* _M0L6bufferS410;
      int32_t _M0L12digit__startS411;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS408
      = _M0FPB14radix__count64(_M0L3numS398, _M0L5radixS395);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS2521 = 1;
      } else {
        _M0L6_2atmpS2521 = 0;
      }
      _M0L10total__lenS409 = _M0L10digit__lenS408 + _M0L6_2atmpS2521;
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
  int32_t _M0L6_2atmpS2518;
  uint64_t _M0L3numS371;
  int32_t _M0L6offsetS372;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2518 = _M0L10total__lenS394 - _M0L12digit__startS382;
  _M0L3numS371 = _M0L3numS393;
  _M0L6offsetS372 = _M0L6_2atmpS2518;
  while (1) {
    if (_M0L3numS371 >= 10000ull) {
      uint64_t _M0L1tS373 = _M0L3numS371 / 10000ull;
      uint64_t _M0L6_2atmpS2495 = _M0L3numS371 % 10000ull;
      int32_t _M0L1rS374 = (int32_t)_M0L6_2atmpS2495;
      int32_t _M0L2d1S375 = _M0L1rS374 / 100;
      int32_t _M0L2d2S376 = _M0L1rS374 % 100;
      int32_t _M0L6_2atmpS2494 = _M0L2d1S375 / 10;
      int32_t _M0L6_2atmpS2493 = 48 + _M0L6_2atmpS2494;
      int32_t _M0L6d1__hiS377 = (uint16_t)_M0L6_2atmpS2493;
      int32_t _M0L6_2atmpS2492 = _M0L2d1S375 % 10;
      int32_t _M0L6_2atmpS2491 = 48 + _M0L6_2atmpS2492;
      int32_t _M0L6d1__loS378 = (uint16_t)_M0L6_2atmpS2491;
      int32_t _M0L6_2atmpS2490 = _M0L2d2S376 / 10;
      int32_t _M0L6_2atmpS2489 = 48 + _M0L6_2atmpS2490;
      int32_t _M0L6d2__hiS379 = (uint16_t)_M0L6_2atmpS2489;
      int32_t _M0L6_2atmpS2488 = _M0L2d2S376 % 10;
      int32_t _M0L6_2atmpS2487 = 48 + _M0L6_2atmpS2488;
      int32_t _M0L6d2__loS380 = (uint16_t)_M0L6_2atmpS2487;
      int32_t _M0L6_2atmpS2479 = _M0L12digit__startS382 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS2478 = _M0L6_2atmpS2479 - 4;
      int32_t _M0L6_2atmpS2481;
      int32_t _M0L6_2atmpS2480;
      int32_t _M0L6_2atmpS2483;
      int32_t _M0L6_2atmpS2482;
      int32_t _M0L6_2atmpS2485;
      int32_t _M0L6_2atmpS2484;
      int32_t _M0L6_2atmpS2486;
      _M0L6bufferS381[_M0L6_2atmpS2478] = _M0L6d1__hiS377;
      _M0L6_2atmpS2481 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS2480 = _M0L6_2atmpS2481 - 3;
      _M0L6bufferS381[_M0L6_2atmpS2480] = _M0L6d1__loS378;
      _M0L6_2atmpS2483 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS2482 = _M0L6_2atmpS2483 - 2;
      _M0L6bufferS381[_M0L6_2atmpS2482] = _M0L6d2__hiS379;
      _M0L6_2atmpS2485 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS2484 = _M0L6_2atmpS2485 - 1;
      _M0L6bufferS381[_M0L6_2atmpS2484] = _M0L6d2__loS380;
      _M0L6_2atmpS2486 = _M0L6offsetS372 - 4;
      _M0L3numS371 = _M0L1tS373;
      _M0L6offsetS372 = _M0L6_2atmpS2486;
      continue;
    } else {
      int32_t _M0L6_2atmpS2517 = (int32_t)_M0L3numS371;
      int32_t _M0L9remainingS384 = _M0L6_2atmpS2517;
      int32_t _M0L6offsetS385 = _M0L6offsetS372;
      while (1) {
        if (_M0L9remainingS384 >= 100) {
          int32_t _M0L1tS386 = _M0L9remainingS384 / 100;
          int32_t _M0L1dS387 = _M0L9remainingS384 % 100;
          int32_t _M0L6_2atmpS2504 = _M0L1dS387 / 10;
          int32_t _M0L6_2atmpS2503 = 48 + _M0L6_2atmpS2504;
          int32_t _M0L5d__hiS388 = (uint16_t)_M0L6_2atmpS2503;
          int32_t _M0L6_2atmpS2502 = _M0L1dS387 % 10;
          int32_t _M0L6_2atmpS2501 = 48 + _M0L6_2atmpS2502;
          int32_t _M0L5d__loS389 = (uint16_t)_M0L6_2atmpS2501;
          int32_t _M0L6_2atmpS2497 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS2496 = _M0L6_2atmpS2497 - 2;
          int32_t _M0L6_2atmpS2499;
          int32_t _M0L6_2atmpS2498;
          int32_t _M0L6_2atmpS2500;
          _M0L6bufferS381[_M0L6_2atmpS2496] = _M0L5d__hiS388;
          _M0L6_2atmpS2499 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS2498 = _M0L6_2atmpS2499 - 1;
          _M0L6bufferS381[_M0L6_2atmpS2498] = _M0L5d__loS389;
          _M0L6_2atmpS2500 = _M0L6offsetS385 - 2;
          _M0L9remainingS384 = _M0L1tS386;
          _M0L6offsetS385 = _M0L6_2atmpS2500;
          continue;
        } else if (_M0L9remainingS384 >= 10) {
          int32_t _M0L6_2atmpS2512 = _M0L9remainingS384 / 10;
          int32_t _M0L6_2atmpS2511 = 48 + _M0L6_2atmpS2512;
          int32_t _M0L5d__hiS391 = (uint16_t)_M0L6_2atmpS2511;
          int32_t _M0L6_2atmpS2510 = _M0L9remainingS384 % 10;
          int32_t _M0L6_2atmpS2509 = 48 + _M0L6_2atmpS2510;
          int32_t _M0L5d__loS392 = (uint16_t)_M0L6_2atmpS2509;
          int32_t _M0L6_2atmpS2506 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS2505 = _M0L6_2atmpS2506 - 2;
          int32_t _M0L6_2atmpS2508;
          int32_t _M0L6_2atmpS2507;
          _M0L6bufferS381[_M0L6_2atmpS2505] = _M0L5d__hiS391;
          _M0L6_2atmpS2508 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS2507 = _M0L6_2atmpS2508 - 1;
          _M0L6bufferS381[_M0L6_2atmpS2507] = _M0L5d__loS392;
        } else {
          int32_t _M0L6_2atmpS2516 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS2513 = _M0L6_2atmpS2516 - 1;
          int32_t _M0L6_2atmpS2515 = 48 + _M0L9remainingS384;
          int32_t _M0L6_2atmpS2514 = (uint16_t)_M0L6_2atmpS2515;
          _M0L6bufferS381[_M0L6_2atmpS2513] = _M0L6_2atmpS2514;
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
  int32_t _M0L6_2atmpS2463;
  int32_t _M0L6_2atmpS2462;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS354 = _M0MPC13int3Int10to__uint64(_M0L5radixS355);
  _M0L6_2atmpS2463 = _M0L5radixS355 - 1;
  _M0L6_2atmpS2462 = _M0L5radixS355 & _M0L6_2atmpS2463;
  if (_M0L6_2atmpS2462 == 0) {
    int32_t _M0L5shiftS356;
    uint64_t _M0L4maskS357;
    int32_t _M0L6_2atmpS2470;
    int32_t _M0L6offsetS358;
    uint64_t _M0L1nS359;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS356 = moonbit_ctz32(_M0L5radixS355);
    _M0L4maskS357 = _M0L4baseS354 - 1ull;
    _M0L6_2atmpS2470 = _M0L10total__lenS364 - _M0L12digit__startS362;
    _M0L6offsetS358 = _M0L6_2atmpS2470;
    _M0L1nS359 = _M0L3numS365;
    while (1) {
      if (_M0L1nS359 > 0ull) {
        uint64_t _M0L6_2atmpS2469 = _M0L1nS359 & _M0L4maskS357;
        int32_t _M0L5digitS360 = (int32_t)_M0L6_2atmpS2469;
        int32_t _M0L6_2atmpS2466 = _M0L12digit__startS362 + _M0L6offsetS358;
        int32_t _M0L6_2atmpS2464 = _M0L6_2atmpS2466 - 1;
        int32_t _M0L6_2atmpS2465 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS360];
        int32_t _M0L6_2atmpS2467;
        uint64_t _M0L6_2atmpS2468;
        _M0L6bufferS361[_M0L6_2atmpS2464] = _M0L6_2atmpS2465;
        _M0L6_2atmpS2467 = _M0L6offsetS358 - 1;
        _M0L6_2atmpS2468 = _M0L1nS359 >> (_M0L5shiftS356 & 63);
        _M0L6offsetS358 = _M0L6_2atmpS2467;
        _M0L1nS359 = _M0L6_2atmpS2468;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2477 = _M0L10total__lenS364 - _M0L12digit__startS362;
    int32_t _M0L6offsetS366 = _M0L6_2atmpS2477;
    uint64_t _M0L1nS367 = _M0L3numS365;
    while (1) {
      if (_M0L1nS367 > 0ull) {
        uint64_t _M0L1qS368 = _M0L1nS367 / _M0L4baseS354;
        uint64_t _M0L6_2atmpS2476 = _M0L1qS368 * _M0L4baseS354;
        uint64_t _M0L6_2atmpS2475 = _M0L1nS367 - _M0L6_2atmpS2476;
        int32_t _M0L5digitS369 = (int32_t)_M0L6_2atmpS2475;
        int32_t _M0L6_2atmpS2473 = _M0L12digit__startS362 + _M0L6offsetS366;
        int32_t _M0L6_2atmpS2471 = _M0L6_2atmpS2473 - 1;
        int32_t _M0L6_2atmpS2472 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS369];
        int32_t _M0L6_2atmpS2474;
        _M0L6bufferS361[_M0L6_2atmpS2471] = _M0L6_2atmpS2472;
        _M0L6_2atmpS2474 = _M0L6offsetS366 - 1;
        _M0L6offsetS366 = _M0L6_2atmpS2474;
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
  int32_t _M0L6_2atmpS2461;
  int32_t _M0L6offsetS343;
  uint64_t _M0L1nS344;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2461 = _M0L10total__lenS352 - _M0L12digit__startS349;
  _M0L6offsetS343 = _M0L6_2atmpS2461;
  _M0L1nS344 = _M0L3numS353;
  while (1) {
    if (_M0L6offsetS343 >= 2) {
      uint64_t _M0L6_2atmpS2458 = _M0L1nS344 & 255ull;
      int32_t _M0L9byte__valS345 = (int32_t)_M0L6_2atmpS2458;
      int32_t _M0L2hiS346 = _M0L9byte__valS345 / 16;
      int32_t _M0L2loS347 = _M0L9byte__valS345 % 16;
      int32_t _M0L6_2atmpS2452 = _M0L12digit__startS349 + _M0L6offsetS343;
      int32_t _M0L6_2atmpS2450 = _M0L6_2atmpS2452 - 2;
      int32_t _M0L6_2atmpS2451 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L2hiS346];
      int32_t _M0L6_2atmpS2455;
      int32_t _M0L6_2atmpS2453;
      int32_t _M0L6_2atmpS2454;
      int32_t _M0L6_2atmpS2456;
      uint64_t _M0L6_2atmpS2457;
      _M0L6bufferS348[_M0L6_2atmpS2450] = _M0L6_2atmpS2451;
      _M0L6_2atmpS2455 = _M0L12digit__startS349 + _M0L6offsetS343;
      _M0L6_2atmpS2453 = _M0L6_2atmpS2455 - 1;
      _M0L6_2atmpS2454
      = ((moonbit_string_t)moonbit_string_literal_20.data)[
        _M0L2loS347
      ];
      _M0L6bufferS348[_M0L6_2atmpS2453] = _M0L6_2atmpS2454;
      _M0L6_2atmpS2456 = _M0L6offsetS343 - 2;
      _M0L6_2atmpS2457 = _M0L1nS344 >> 8;
      _M0L6offsetS343 = _M0L6_2atmpS2456;
      _M0L1nS344 = _M0L6_2atmpS2457;
      continue;
    } else if (_M0L6offsetS343 == 1) {
      uint64_t _M0L6_2atmpS2460 = _M0L1nS344 & 15ull;
      int32_t _M0L6nibbleS351 = (int32_t)_M0L6_2atmpS2460;
      int32_t _M0L6_2atmpS2459 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L6nibbleS351];
      _M0L6bufferS348[_M0L12digit__startS349] = _M0L6_2atmpS2459;
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
      uint64_t _M0L6_2atmpS2448 = _M0L3numS340 / _M0L4baseS338;
      int32_t _M0L6_2atmpS2449 = _M0L5countS341 + 1;
      _M0L3numS340 = _M0L6_2atmpS2448;
      _M0L5countS341 = _M0L6_2atmpS2449;
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
    int32_t _M0L6_2atmpS2447;
    int32_t _M0L6_2atmpS2446;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS336 = moonbit_clz64(_M0L5valueS335);
    _M0L6_2atmpS2447 = 63 - _M0L14leading__zerosS336;
    _M0L6_2atmpS2446 = _M0L6_2atmpS2447 / 4;
    return _M0L6_2atmpS2446 + 1;
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
    int32_t _M0L6_2atmpS2445 = -_M0L4selfS318;
    _M0L3numS320 = *(uint32_t*)&_M0L6_2atmpS2445;
  } else {
    _M0L3numS320 = *(uint32_t*)&_M0L4selfS318;
  }
  switch (_M0L5radixS317) {
    case 10: {
      int32_t _M0L10digit__lenS322;
      int32_t _M0L6_2atmpS2442;
      int32_t _M0L10total__lenS323;
      uint16_t* _M0L6bufferS324;
      int32_t _M0L12digit__startS325;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS322 = _M0FPB12dec__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS2442 = 1;
      } else {
        _M0L6_2atmpS2442 = 0;
      }
      _M0L10total__lenS323 = _M0L10digit__lenS322 + _M0L6_2atmpS2442;
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
      int32_t _M0L6_2atmpS2443;
      int32_t _M0L10total__lenS327;
      uint16_t* _M0L6bufferS328;
      int32_t _M0L12digit__startS329;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS326 = _M0FPB12hex__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS2443 = 1;
      } else {
        _M0L6_2atmpS2443 = 0;
      }
      _M0L10total__lenS327 = _M0L10digit__lenS326 + _M0L6_2atmpS2443;
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
      int32_t _M0L6_2atmpS2444;
      int32_t _M0L10total__lenS331;
      uint16_t* _M0L6bufferS332;
      int32_t _M0L12digit__startS333;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS330
      = _M0FPB14radix__count32(_M0L3numS320, _M0L5radixS317);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS2444 = 1;
      } else {
        _M0L6_2atmpS2444 = 0;
      }
      _M0L10total__lenS331 = _M0L10digit__lenS330 + _M0L6_2atmpS2444;
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
      uint32_t _M0L6_2atmpS2440 = _M0L3numS314 / _M0L4baseS312;
      int32_t _M0L6_2atmpS2441 = _M0L5countS315 + 1;
      _M0L3numS314 = _M0L6_2atmpS2440;
      _M0L5countS315 = _M0L6_2atmpS2441;
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
    int32_t _M0L6_2atmpS2439;
    int32_t _M0L6_2atmpS2438;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS310 = moonbit_clz32(_M0L5valueS309);
    _M0L6_2atmpS2439 = 31 - _M0L14leading__zerosS310;
    _M0L6_2atmpS2438 = _M0L6_2atmpS2439 / 4;
    return _M0L6_2atmpS2438 + 1;
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
  int32_t _M0L6_2atmpS2437;
  uint32_t _M0L3numS284;
  int32_t _M0L6offsetS285;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2437 = _M0L10total__lenS307 - _M0L12digit__startS295;
  _M0L3numS284 = _M0L3numS306;
  _M0L6offsetS285 = _M0L6_2atmpS2437;
  while (1) {
    if (_M0L3numS284 >= 10000u) {
      uint32_t _M0L1tS286 = _M0L3numS284 / 10000u;
      uint32_t _M0L6_2atmpS2414 = _M0L3numS284 % 10000u;
      int32_t _M0L1rS287 = *(int32_t*)&_M0L6_2atmpS2414;
      int32_t _M0L2d1S288 = _M0L1rS287 / 100;
      int32_t _M0L2d2S289 = _M0L1rS287 % 100;
      int32_t _M0L6_2atmpS2413 = _M0L2d1S288 / 10;
      int32_t _M0L6_2atmpS2412 = 48 + _M0L6_2atmpS2413;
      int32_t _M0L6d1__hiS290 = (uint16_t)_M0L6_2atmpS2412;
      int32_t _M0L6_2atmpS2411 = _M0L2d1S288 % 10;
      int32_t _M0L6_2atmpS2410 = 48 + _M0L6_2atmpS2411;
      int32_t _M0L6d1__loS291 = (uint16_t)_M0L6_2atmpS2410;
      int32_t _M0L6_2atmpS2409 = _M0L2d2S289 / 10;
      int32_t _M0L6_2atmpS2408 = 48 + _M0L6_2atmpS2409;
      int32_t _M0L6d2__hiS292 = (uint16_t)_M0L6_2atmpS2408;
      int32_t _M0L6_2atmpS2407 = _M0L2d2S289 % 10;
      int32_t _M0L6_2atmpS2406 = 48 + _M0L6_2atmpS2407;
      int32_t _M0L6d2__loS293 = (uint16_t)_M0L6_2atmpS2406;
      int32_t _M0L6_2atmpS2398 = _M0L12digit__startS295 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS2397 = _M0L6_2atmpS2398 - 4;
      int32_t _M0L6_2atmpS2400;
      int32_t _M0L6_2atmpS2399;
      int32_t _M0L6_2atmpS2402;
      int32_t _M0L6_2atmpS2401;
      int32_t _M0L6_2atmpS2404;
      int32_t _M0L6_2atmpS2403;
      int32_t _M0L6_2atmpS2405;
      _M0L6bufferS294[_M0L6_2atmpS2397] = _M0L6d1__hiS290;
      _M0L6_2atmpS2400 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS2399 = _M0L6_2atmpS2400 - 3;
      _M0L6bufferS294[_M0L6_2atmpS2399] = _M0L6d1__loS291;
      _M0L6_2atmpS2402 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS2401 = _M0L6_2atmpS2402 - 2;
      _M0L6bufferS294[_M0L6_2atmpS2401] = _M0L6d2__hiS292;
      _M0L6_2atmpS2404 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS2403 = _M0L6_2atmpS2404 - 1;
      _M0L6bufferS294[_M0L6_2atmpS2403] = _M0L6d2__loS293;
      _M0L6_2atmpS2405 = _M0L6offsetS285 - 4;
      _M0L3numS284 = _M0L1tS286;
      _M0L6offsetS285 = _M0L6_2atmpS2405;
      continue;
    } else {
      int32_t _M0L6_2atmpS2436 = *(int32_t*)&_M0L3numS284;
      int32_t _M0L9remainingS297 = _M0L6_2atmpS2436;
      int32_t _M0L6offsetS298 = _M0L6offsetS285;
      while (1) {
        if (_M0L9remainingS297 >= 100) {
          int32_t _M0L1tS299 = _M0L9remainingS297 / 100;
          int32_t _M0L1dS300 = _M0L9remainingS297 % 100;
          int32_t _M0L6_2atmpS2423 = _M0L1dS300 / 10;
          int32_t _M0L6_2atmpS2422 = 48 + _M0L6_2atmpS2423;
          int32_t _M0L5d__hiS301 = (uint16_t)_M0L6_2atmpS2422;
          int32_t _M0L6_2atmpS2421 = _M0L1dS300 % 10;
          int32_t _M0L6_2atmpS2420 = 48 + _M0L6_2atmpS2421;
          int32_t _M0L5d__loS302 = (uint16_t)_M0L6_2atmpS2420;
          int32_t _M0L6_2atmpS2416 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS2415 = _M0L6_2atmpS2416 - 2;
          int32_t _M0L6_2atmpS2418;
          int32_t _M0L6_2atmpS2417;
          int32_t _M0L6_2atmpS2419;
          _M0L6bufferS294[_M0L6_2atmpS2415] = _M0L5d__hiS301;
          _M0L6_2atmpS2418 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS2417 = _M0L6_2atmpS2418 - 1;
          _M0L6bufferS294[_M0L6_2atmpS2417] = _M0L5d__loS302;
          _M0L6_2atmpS2419 = _M0L6offsetS298 - 2;
          _M0L9remainingS297 = _M0L1tS299;
          _M0L6offsetS298 = _M0L6_2atmpS2419;
          continue;
        } else if (_M0L9remainingS297 >= 10) {
          int32_t _M0L6_2atmpS2431 = _M0L9remainingS297 / 10;
          int32_t _M0L6_2atmpS2430 = 48 + _M0L6_2atmpS2431;
          int32_t _M0L5d__hiS304 = (uint16_t)_M0L6_2atmpS2430;
          int32_t _M0L6_2atmpS2429 = _M0L9remainingS297 % 10;
          int32_t _M0L6_2atmpS2428 = 48 + _M0L6_2atmpS2429;
          int32_t _M0L5d__loS305 = (uint16_t)_M0L6_2atmpS2428;
          int32_t _M0L6_2atmpS2425 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS2424 = _M0L6_2atmpS2425 - 2;
          int32_t _M0L6_2atmpS2427;
          int32_t _M0L6_2atmpS2426;
          _M0L6bufferS294[_M0L6_2atmpS2424] = _M0L5d__hiS304;
          _M0L6_2atmpS2427 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS2426 = _M0L6_2atmpS2427 - 1;
          _M0L6bufferS294[_M0L6_2atmpS2426] = _M0L5d__loS305;
        } else {
          int32_t _M0L6_2atmpS2435 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS2432 = _M0L6_2atmpS2435 - 1;
          int32_t _M0L6_2atmpS2434 = 48 + _M0L9remainingS297;
          int32_t _M0L6_2atmpS2433 = (uint16_t)_M0L6_2atmpS2434;
          _M0L6bufferS294[_M0L6_2atmpS2432] = _M0L6_2atmpS2433;
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
  int32_t _M0L6_2atmpS2382;
  int32_t _M0L6_2atmpS2381;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS267 = *(uint32_t*)&_M0L5radixS268;
  _M0L6_2atmpS2382 = _M0L5radixS268 - 1;
  _M0L6_2atmpS2381 = _M0L5radixS268 & _M0L6_2atmpS2382;
  if (_M0L6_2atmpS2381 == 0) {
    int32_t _M0L5shiftS269;
    uint32_t _M0L4maskS270;
    int32_t _M0L6_2atmpS2389;
    int32_t _M0L6offsetS271;
    uint32_t _M0L1nS272;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS269 = moonbit_ctz32(_M0L5radixS268);
    _M0L4maskS270 = _M0L4baseS267 - 1u;
    _M0L6_2atmpS2389 = _M0L10total__lenS277 - _M0L12digit__startS275;
    _M0L6offsetS271 = _M0L6_2atmpS2389;
    _M0L1nS272 = _M0L3numS278;
    while (1) {
      if (_M0L1nS272 > 0u) {
        uint32_t _M0L6_2atmpS2388 = _M0L1nS272 & _M0L4maskS270;
        int32_t _M0L5digitS273 = *(int32_t*)&_M0L6_2atmpS2388;
        int32_t _M0L6_2atmpS2385 = _M0L12digit__startS275 + _M0L6offsetS271;
        int32_t _M0L6_2atmpS2383 = _M0L6_2atmpS2385 - 1;
        int32_t _M0L6_2atmpS2384 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS273];
        int32_t _M0L6_2atmpS2386;
        uint32_t _M0L6_2atmpS2387;
        _M0L6bufferS274[_M0L6_2atmpS2383] = _M0L6_2atmpS2384;
        _M0L6_2atmpS2386 = _M0L6offsetS271 - 1;
        _M0L6_2atmpS2387 = _M0L1nS272 >> (_M0L5shiftS269 & 31);
        _M0L6offsetS271 = _M0L6_2atmpS2386;
        _M0L1nS272 = _M0L6_2atmpS2387;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2396 = _M0L10total__lenS277 - _M0L12digit__startS275;
    int32_t _M0L6offsetS279 = _M0L6_2atmpS2396;
    uint32_t _M0L1nS280 = _M0L3numS278;
    while (1) {
      if (_M0L1nS280 > 0u) {
        uint32_t _M0L1qS281 = _M0L1nS280 / _M0L4baseS267;
        uint32_t _M0L6_2atmpS2395 = _M0L1qS281 * _M0L4baseS267;
        uint32_t _M0L6_2atmpS2394 = _M0L1nS280 - _M0L6_2atmpS2395;
        int32_t _M0L5digitS282 = *(int32_t*)&_M0L6_2atmpS2394;
        int32_t _M0L6_2atmpS2392 = _M0L12digit__startS275 + _M0L6offsetS279;
        int32_t _M0L6_2atmpS2390 = _M0L6_2atmpS2392 - 1;
        int32_t _M0L6_2atmpS2391 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS282];
        int32_t _M0L6_2atmpS2393;
        _M0L6bufferS274[_M0L6_2atmpS2390] = _M0L6_2atmpS2391;
        _M0L6_2atmpS2393 = _M0L6offsetS279 - 1;
        _M0L6offsetS279 = _M0L6_2atmpS2393;
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
  int32_t _M0L6_2atmpS2380;
  int32_t _M0L6offsetS256;
  uint32_t _M0L1nS257;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2380 = _M0L10total__lenS265 - _M0L12digit__startS262;
  _M0L6offsetS256 = _M0L6_2atmpS2380;
  _M0L1nS257 = _M0L3numS266;
  while (1) {
    if (_M0L6offsetS256 >= 2) {
      uint32_t _M0L6_2atmpS2377 = _M0L1nS257 & 255u;
      int32_t _M0L9byte__valS258 = *(int32_t*)&_M0L6_2atmpS2377;
      int32_t _M0L2hiS259 = _M0L9byte__valS258 / 16;
      int32_t _M0L2loS260 = _M0L9byte__valS258 % 16;
      int32_t _M0L6_2atmpS2371 = _M0L12digit__startS262 + _M0L6offsetS256;
      int32_t _M0L6_2atmpS2369 = _M0L6_2atmpS2371 - 2;
      int32_t _M0L6_2atmpS2370 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L2hiS259];
      int32_t _M0L6_2atmpS2374;
      int32_t _M0L6_2atmpS2372;
      int32_t _M0L6_2atmpS2373;
      int32_t _M0L6_2atmpS2375;
      uint32_t _M0L6_2atmpS2376;
      _M0L6bufferS261[_M0L6_2atmpS2369] = _M0L6_2atmpS2370;
      _M0L6_2atmpS2374 = _M0L12digit__startS262 + _M0L6offsetS256;
      _M0L6_2atmpS2372 = _M0L6_2atmpS2374 - 1;
      _M0L6_2atmpS2373
      = ((moonbit_string_t)moonbit_string_literal_20.data)[
        _M0L2loS260
      ];
      _M0L6bufferS261[_M0L6_2atmpS2372] = _M0L6_2atmpS2373;
      _M0L6_2atmpS2375 = _M0L6offsetS256 - 2;
      _M0L6_2atmpS2376 = _M0L1nS257 >> 8;
      _M0L6offsetS256 = _M0L6_2atmpS2375;
      _M0L1nS257 = _M0L6_2atmpS2376;
      continue;
    } else if (_M0L6offsetS256 == 1) {
      uint32_t _M0L6_2atmpS2379 = _M0L1nS257 & 15u;
      int32_t _M0L6nibbleS264 = *(int32_t*)&_M0L6_2atmpS2379;
      int32_t _M0L6_2atmpS2378 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L6nibbleS264];
      _M0L6bufferS261[_M0L12digit__startS262] = _M0L6_2atmpS2378;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS255
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS254;
  struct _M0TPB6Logger _M0L6_2atmpS2368;
  moonbit_string_t _result_6151;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS254);
  _M0L6_2atmpS2368
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS254
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS255, _M0L6_2atmpS2368);
  if (_M0L6_2atmpS2368.$1) {
    moonbit_decref(_M0L6_2atmpS2368.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_6151 = _M0MPB13StringBuilder10to__string(_M0L6loggerS254);
  moonbit_decref_cycle_free(_M0L6loggerS254);
  return _result_6151;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS249,
  struct _M0TPB6Logger _M0L6loggerS248
) {
  moonbit_string_t _M0L6_2atmpS2365;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2365 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS249);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248.$0->$method_0(_M0L6loggerS248.$1, _M0L6_2atmpS2365);
  moonbit_decref_cycle_free(_M0L6_2atmpS2365);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS2366;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2366 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS2366);
  moonbit_decref_cycle_free(_M0L6_2atmpS2366);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS2367;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2367 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS2367);
  moonbit_decref_cycle_free(_M0L6_2atmpS2367);
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
  moonbit_string_t _M0L8_2afieldS5690;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS5690 = _M0L4selfS246.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5690);
  return _M0L8_2afieldS5690;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS242,
  moonbit_string_t _M0L5valueS243,
  int32_t _M0L5startS244,
  int32_t _M0L3lenS245
) {
  int32_t _M0L6_2atmpS2364;
  int64_t _M0L6_2atmpS2363;
  struct _M0TPC16string10StringView _M0L6_2atmpS2362;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2364 = _M0L5startS244 + _M0L3lenS245;
  _M0L6_2atmpS2363 = (int64_t)_M0L6_2atmpS2364;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2362
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS243, _M0L5startS244, _M0L6_2atmpS2363);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS242, _M0L6_2atmpS2362);
  moonbit_decref_cycle_free(_M0L6_2atmpS2362.$0);
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
  int32_t _M0L6_2atmpS2346;
  int32_t _if__result_6152;
  int32_t _M0L6_2atmpS2354;
  int32_t _if__result_6153;
  int32_t _M0L6_2atmpS2356;
  int32_t _M0L6_2atmpS2357;
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
  _M0L6_2atmpS2346 = _M0Lm2loS236;
  if (_M0L6_2atmpS2346 > 0) {
    int32_t _M0L6_2atmpS2345 = _M0Lm2loS236;
    if (_M0L6_2atmpS2345 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS2344 = _M0Lm2loS236;
      int32_t _M0L6_2atmpS2343 = _M0L4selfS235[_M0L6_2atmpS2344];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2343)) {
        int32_t _M0L6_2atmpS2342 = _M0Lm2loS236;
        int32_t _M0L6_2atmpS2341 = _M0L6_2atmpS2342 - 1;
        int32_t _M0L6_2atmpS2340 = _M0L4selfS235[_M0L6_2atmpS2341];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_6152
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2340);
      } else {
        _if__result_6152 = 0;
      }
    } else {
      _if__result_6152 = 0;
    }
  } else {
    _if__result_6152 = 0;
  }
  if (_if__result_6152) {
    int32_t _M0L6_2atmpS2347 = _M0Lm2loS236;
    _M0Lm2loS236 = _M0L6_2atmpS2347 + 1;
  }
  _M0L6_2atmpS2354 = _M0Lm2hiS238;
  if (_M0L6_2atmpS2354 > 0) {
    int32_t _M0L6_2atmpS2353 = _M0Lm2hiS238;
    if (_M0L6_2atmpS2353 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS2352 = _M0Lm2hiS238;
      int32_t _M0L6_2atmpS2351 = _M0L4selfS235[_M0L6_2atmpS2352];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2351)) {
        int32_t _M0L6_2atmpS2350 = _M0Lm2hiS238;
        int32_t _M0L6_2atmpS2349 = _M0L6_2atmpS2350 - 1;
        int32_t _M0L6_2atmpS2348 = _M0L4selfS235[_M0L6_2atmpS2349];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_6153
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2348);
      } else {
        _if__result_6153 = 0;
      }
    } else {
      _if__result_6153 = 0;
    }
  } else {
    _if__result_6153 = 0;
  }
  if (_if__result_6153) {
    int32_t _M0L6_2atmpS2355 = _M0Lm2hiS238;
    _M0Lm2hiS238 = _M0L6_2atmpS2355 - 1;
  }
  _M0L6_2atmpS2356 = _M0Lm2loS236;
  _M0L6_2atmpS2357 = _M0Lm2hiS238;
  if (_M0L6_2atmpS2356 >= _M0L6_2atmpS2357) {
    int32_t _M0L6_2atmpS2358 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS2359 = _M0Lm2loS236;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS2358,
                                                 .$2 = _M0L6_2atmpS2359};
  } else {
    int32_t _M0L6_2atmpS2360 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS2361 = _M0Lm2hiS238;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS2360,
                                                 .$2 = _M0L6_2atmpS2361};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS2339;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS2339
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS2339);
  if (_M0L6_2atmpS2339.$1) {
    moonbit_decref(_M0L6_2atmpS2339.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS231,
  struct _M0TPB4Show _M0L4showS230
) {
  struct _M0TPB6Logger _M0L6_2atmpS2338;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS231);
  _M0L6_2atmpS2338
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS231
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS230.$0->$method_0(_M0L4showS230.$1, _M0L6_2atmpS2338);
  if (_M0L6_2atmpS2338.$1) {
    moonbit_decref(_M0L6_2atmpS2338.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS229) {
  int64_t _M0L6_2atmpS2337;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2337 = (int64_t)_M0L4selfS229;
  return *(uint64_t*)&_M0L6_2atmpS2337;
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
  int32_t _M0L6_2atmpS2336;
  struct _M0TPC16string10StringView _M0L6_2atmpS2334;
  struct _M0TPB6Logger _M0L6_2atmpS2335;
  moonbit_string_t _result_6154;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS2336 = Moonbit_array_length(_M0L4selfS227);
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS2334
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS227, .$1 = 0, .$2 = _M0L6_2atmpS2336
  };
  moonbit_incref_cycle_free(_M0L3bufS226);
  _M0L6_2atmpS2335
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS226
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS2334, _M0L6_2atmpS2335, _M0L5quoteS228);
  moonbit_decref_cycle_free(_M0L6_2atmpS2334.$0);
  if (_M0L6_2atmpS2335.$1) {
    moonbit_decref(_M0L6_2atmpS2335.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_6154 = _M0MPB13StringBuilder10to__string(_M0L3bufS226);
  moonbit_decref_cycle_free(_M0L3bufS226);
  return _result_6154;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS218,
  struct _M0TPB6Logger _M0L6loggerS216,
  int32_t _M0L5quoteS215
) {
  int32_t _M0L3endS2332;
  int32_t _M0L5startS2333;
  int32_t _M0L3lenS217;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS219;
  int32_t _M0L1iS220;
  int32_t _M0L3segS221;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS215) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  _M0L3endS2332 = _M0L4selfS218.$2;
  _M0L5startS2333 = _M0L4selfS218.$1;
  _M0L3lenS217 = _M0L3endS2332 - _M0L5startS2333;
  moonbit_incref_cycle_free(_M0L4selfS218.$0);
  if (_M0L6loggerS216.$1) {
    moonbit_incref(_M0L6loggerS216.$1);
  }
  _M0L6_2aenvS219
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 112, 0);
  _M0L6_2aenvS219->$0 = _M0L4selfS218;
  _M0L6_2aenvS219->$1 = _M0L6loggerS216;
  _M0L1iS220 = 0;
  _M0L3segS221 = 0;
  _2afor_222:;
  while (1) {
    moonbit_string_t _M0L3strS2329;
    int32_t _M0L5startS2331;
    int32_t _M0L6_2atmpS2330;
    int32_t _M0L4codeS223;
    int32_t _M0L1cS225;
    int32_t _M0L6_2atmpS2313;
    int32_t _M0L6_2atmpS2314;
    int32_t _M0L6_2atmpS2315;
    if (_M0L1iS220 >= _M0L3lenS217) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
      moonbit_decref_cycle_free(_M0L6_2aenvS219);
      break;
    }
    _M0L3strS2329 = _M0L4selfS218.$0;
    _M0L5startS2331 = _M0L4selfS218.$1;
    _M0L6_2atmpS2330 = _M0L5startS2331 + _M0L1iS220;
    _M0L4codeS223 = _M0L3strS2329[_M0L6_2atmpS2330];
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
        int32_t _M0L6_2atmpS2316;
        int32_t _M0L6_2atmpS2317;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS2316 = _M0L1iS220 + 1;
        _M0L6_2atmpS2317 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS2316;
        _M0L3segS221 = _M0L6_2atmpS2317;
        goto _2afor_222;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS2318;
        int32_t _M0L6_2atmpS2319;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_22.data);
        _M0L6_2atmpS2318 = _M0L1iS220 + 1;
        _M0L6_2atmpS2319 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS2318;
        _M0L3segS221 = _M0L6_2atmpS2319;
        goto _2afor_222;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS2320;
        int32_t _M0L6_2atmpS2321;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_23.data);
        _M0L6_2atmpS2320 = _M0L1iS220 + 1;
        _M0L6_2atmpS2321 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS2320;
        _M0L3segS221 = _M0L6_2atmpS2321;
        goto _2afor_222;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS2322;
        int32_t _M0L6_2atmpS2323;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_24.data);
        _M0L6_2atmpS2322 = _M0L1iS220 + 1;
        _M0L6_2atmpS2323 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS2322;
        _M0L3segS221 = _M0L6_2atmpS2323;
        goto _2afor_222;
        break;
      }
      default: {
        if (_M0L4codeS223 < 32) {
          int32_t _M0L6_2atmpS2325;
          moonbit_string_t _M0L6_2atmpS2324;
          int32_t _M0L6_2atmpS2326;
          int32_t _M0L6_2atmpS2327;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_25.data);
          _M0L6_2atmpS2325 = _M0L4codeS223 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS2324 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS2325);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, _M0L6_2atmpS2324);
          moonbit_decref_cycle_free(_M0L6_2atmpS2324);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS2326 = _M0L1iS220 + 1;
          _M0L6_2atmpS2327 = _M0L1iS220 + 1;
          _M0L1iS220 = _M0L6_2atmpS2326;
          _M0L3segS221 = _M0L6_2atmpS2327;
          goto _2afor_222;
        } else {
          int32_t _M0L6_2atmpS2328 = _M0L1iS220 + 1;
          int32_t _tmp_6157 = _M0L3segS221;
          _M0L1iS220 = _M0L6_2atmpS2328;
          _M0L3segS221 = _tmp_6157;
          goto _2afor_222;
        }
        break;
      }
    }
    goto joinlet_6156;
    join_224:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2313 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS225);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, _M0L6_2atmpS2313);
    _M0L6_2atmpS2314 = _M0L1iS220 + 1;
    _M0L6_2atmpS2315 = _M0L1iS220 + 1;
    _M0L1iS220 = _M0L6_2atmpS2314;
    _M0L3segS221 = _M0L6_2atmpS2315;
    continue;
    joinlet_6156:;
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
    int64_t _M0L6_2atmpS2312 = (int64_t)_M0L1iS213;
    struct _M0TPC16string10StringView _M0L6_2atmpS2311;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2311
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS212, _M0L3segS214, _M0L6_2atmpS2312);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS210.$0->$method_2(_M0L6loggerS210.$1, _M0L6_2atmpS2311);
    moonbit_decref_cycle_free(_M0L6_2atmpS2311.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS201,
  int32_t _M0L5startS203,
  int64_t _M0L3endS205
) {
  int32_t _M0L3endS2309;
  int32_t _M0L5startS2310;
  int32_t _M0L3lenS200;
  int32_t _M0Lm2loS202;
  int32_t _M0Lm2hiS204;
  moonbit_string_t _M0L3strS208;
  int32_t _M0L4baseS209;
  int32_t _M0L6_2atmpS2287;
  int32_t _if__result_6158;
  int32_t _M0L6_2atmpS2297;
  int32_t _if__result_6159;
  int32_t _M0L6_2atmpS2299;
  int32_t _M0L6_2atmpS2300;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS2309 = _M0L4selfS201.$2;
  _M0L5startS2310 = _M0L4selfS201.$1;
  _M0L3lenS200 = _M0L3endS2309 - _M0L5startS2310;
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
  _M0L6_2atmpS2287 = _M0Lm2loS202;
  if (_M0L6_2atmpS2287 > 0) {
    int32_t _M0L6_2atmpS2286 = _M0Lm2loS202;
    if (_M0L6_2atmpS2286 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS2285 = _M0Lm2loS202;
      int32_t _M0L6_2atmpS2284 = _M0L4baseS209 + _M0L6_2atmpS2285;
      int32_t _M0L6_2atmpS2283 = _M0L3strS208[_M0L6_2atmpS2284];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2283)) {
        int32_t _M0L6_2atmpS2282 = _M0Lm2loS202;
        int32_t _M0L6_2atmpS2281 = _M0L4baseS209 + _M0L6_2atmpS2282;
        int32_t _M0L6_2atmpS2280 = _M0L6_2atmpS2281 - 1;
        int32_t _M0L6_2atmpS2279 = _M0L3strS208[_M0L6_2atmpS2280];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_6158
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2279);
      } else {
        _if__result_6158 = 0;
      }
    } else {
      _if__result_6158 = 0;
    }
  } else {
    _if__result_6158 = 0;
  }
  if (_if__result_6158) {
    int32_t _M0L6_2atmpS2288 = _M0Lm2loS202;
    _M0Lm2loS202 = _M0L6_2atmpS2288 + 1;
  }
  _M0L6_2atmpS2297 = _M0Lm2hiS204;
  if (_M0L6_2atmpS2297 > 0) {
    int32_t _M0L6_2atmpS2296 = _M0Lm2hiS204;
    if (_M0L6_2atmpS2296 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS2295 = _M0Lm2hiS204;
      int32_t _M0L6_2atmpS2294 = _M0L4baseS209 + _M0L6_2atmpS2295;
      int32_t _M0L6_2atmpS2293 = _M0L3strS208[_M0L6_2atmpS2294];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2293)) {
        int32_t _M0L6_2atmpS2292 = _M0Lm2hiS204;
        int32_t _M0L6_2atmpS2291 = _M0L4baseS209 + _M0L6_2atmpS2292;
        int32_t _M0L6_2atmpS2290 = _M0L6_2atmpS2291 - 1;
        int32_t _M0L6_2atmpS2289 = _M0L3strS208[_M0L6_2atmpS2290];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_6159
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2289);
      } else {
        _if__result_6159 = 0;
      }
    } else {
      _if__result_6159 = 0;
    }
  } else {
    _if__result_6159 = 0;
  }
  if (_if__result_6159) {
    int32_t _M0L6_2atmpS2298 = _M0Lm2hiS204;
    _M0Lm2hiS204 = _M0L6_2atmpS2298 - 1;
  }
  _M0L6_2atmpS2299 = _M0Lm2loS202;
  _M0L6_2atmpS2300 = _M0Lm2hiS204;
  if (_M0L6_2atmpS2299 >= _M0L6_2atmpS2300) {
    int32_t _M0L6_2atmpS2304 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS2301 = _M0L4baseS209 + _M0L6_2atmpS2304;
    int32_t _M0L6_2atmpS2303 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS2302 = _M0L4baseS209 + _M0L6_2atmpS2303;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS2301,
                                                 .$2 = _M0L6_2atmpS2302};
  } else {
    int32_t _M0L6_2atmpS2308 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS2305 = _M0L4baseS209 + _M0L6_2atmpS2308;
    int32_t _M0L6_2atmpS2307 = _M0Lm2hiS204;
    int32_t _M0L6_2atmpS2306 = _M0L4baseS209 + _M0L6_2atmpS2307;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS2305,
                                                 .$2 = _M0L6_2atmpS2306};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS199) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS198;
  int32_t _M0L6_2atmpS2276;
  int32_t _M0L6_2atmpS2275;
  int32_t _M0L6_2atmpS2278;
  int32_t _M0L6_2atmpS2277;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS2274;
  moonbit_string_t _result_6160;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2276 = _M0IPC14byte4BytePB3Div3div(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2275
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS2276);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS2275);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2278 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2277
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS2278);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS2277);
  _M0L6_2atmpS2274 = _M0L7_2aselfS198;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_6160 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS2274);
  moonbit_decref_cycle_free(_M0L6_2atmpS2274);
  return _result_6160;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS197) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS197 < 10) {
    int32_t _M0L6_2atmpS2271;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2271 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS2271);
  } else {
    int32_t _M0L6_2atmpS2273;
    int32_t _M0L6_2atmpS2272;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2273 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2272 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS2273, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS2272);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS2269;
  int32_t _M0L6_2atmpS2270;
  int32_t _M0L6_2atmpS2268;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2269 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS2270 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS2268 = _M0L6_2atmpS2269 - _M0L6_2atmpS2270;
  return _M0L6_2atmpS2268 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS2266;
  int32_t _M0L6_2atmpS2267;
  int32_t _M0L6_2atmpS2265;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2266 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS2267 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS2265 = _M0L6_2atmpS2266 % _M0L6_2atmpS2267;
  return _M0L6_2atmpS2265 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS2263;
  int32_t _M0L6_2atmpS2264;
  int32_t _M0L6_2atmpS2262;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2263 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS2264 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS2262 = _M0L6_2atmpS2263 / _M0L6_2atmpS2264;
  return _M0L6_2atmpS2262 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS189,
  int32_t _M0L4thatS190
) {
  int32_t _M0L6_2atmpS2260;
  int32_t _M0L6_2atmpS2261;
  int32_t _M0L6_2atmpS2259;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2260 = (int32_t)_M0L4selfS189;
  _M0L6_2atmpS2261 = (int32_t)_M0L4thatS190;
  _M0L6_2atmpS2259 = _M0L6_2atmpS2260 + _M0L6_2atmpS2261;
  return _M0L6_2atmpS2259 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS188) {
  int32_t _M0L6_2atmpS2258;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS2258 = (int32_t)_M0L4selfS188;
  return _M0L6_2atmpS2258;
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
  int32_t _M0L3lenS2257;
  int32_t _M0L8requiredS184;
  uint16_t* _M0L4dataS2252;
  int32_t _M0L6_2atmpS2251;
  int32_t _if__result_6161;
  uint16_t* _M0L4dataS2253;
  int32_t _M0L3lenS2254;
  int32_t _M0L3lenS2256;
  int32_t _M0L6_2atmpS2255;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS182 = Moonbit_array_length(_M0L3strS183);
  if (_M0L8str__lenS182 == 0) {
    return 0;
  }
  _M0L3lenS2257 = _M0L4selfS185->$1;
  _M0L8requiredS184 = _M0L3lenS2257 + _M0L8str__lenS182;
  _M0L4dataS2252 = _M0L4selfS185->$0;
  _M0L6_2atmpS2251 = Moonbit_array_length(_M0L4dataS2252);
  if (_M0L8requiredS184 > _M0L6_2atmpS2251) {
    _if__result_6161 = 1;
  } else {
    int32_t _M0L3lenS2250 = _M0L4selfS185->$1;
    _if__result_6161 = _M0L8requiredS184 < _M0L3lenS2250;
  }
  if (_if__result_6161) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS185, _M0L8requiredS184);
  }
  _M0L4dataS2253 = _M0L4selfS185->$0;
  _M0L3lenS2254 = _M0L4selfS185->$1;
  moonbit_incref_cycle_free(_M0L4dataS2253);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS2253, _M0L3lenS2254, _M0L3strS183, 0, _M0L8str__lenS182);
  moonbit_decref_cycle_free(_M0L4dataS2253);
  _M0L3lenS2256 = _M0L4selfS185->$1;
  _M0L6_2atmpS2255 = _M0L3lenS2256 + _M0L8str__lenS182;
  _M0L4selfS185->$1 = _M0L6_2atmpS2255;
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
      int32_t _M0L6_2atmpS2247 = _M0L3strS179[_M0L1iS176];
      int32_t _M0L6_2atmpS2248;
      int32_t _M0L6_2atmpS2249;
      _M0L4selfS178[_M0L1jS177] = _M0L6_2atmpS2247;
      _M0L6_2atmpS2248 = _M0L1iS176 + 1;
      _M0L6_2atmpS2249 = _M0L1jS177 + 1;
      _M0L1iS176 = _M0L6_2atmpS2248;
      _M0L1jS177 = _M0L6_2atmpS2249;
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
    int32_t _M0L3lenS2218 = _M0L4selfS171->$1;
    uint16_t* _M0L4dataS2220 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS2219 = Moonbit_array_length(_M0L4dataS2220);
    uint16_t* _M0L4dataS2223;
    int32_t _M0L3lenS2224;
    int32_t _M0L6_2atmpS2225;
    int32_t _M0L3lenS2227;
    int32_t _M0L6_2atmpS2226;
    if (_M0L3lenS2218 >= _M0L6_2atmpS2219) {
      int32_t _M0L3lenS2222 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS2221 = _M0L3lenS2222 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS2221);
    }
    _M0L4dataS2223 = _M0L4selfS171->$0;
    _M0L3lenS2224 = _M0L4selfS171->$1;
    moonbit_incref_cycle_free(_M0L4dataS2223);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS2225 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS169);
    if (
      _M0L3lenS2224 < 0
      || _M0L3lenS2224 >= Moonbit_array_length(_M0L4dataS2223)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS2223[_M0L3lenS2224] = _M0L6_2atmpS2225;
    moonbit_decref_cycle_free(_M0L4dataS2223);
    _M0L3lenS2227 = _M0L4selfS171->$1;
    _M0L6_2atmpS2226 = _M0L3lenS2227 + 1;
    _M0L4selfS171->$1 = _M0L6_2atmpS2226;
  } else if (_M0L4codeS169 <= 1114111u) {
    uint16_t* _M0L4dataS2231 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS2229 = Moonbit_array_length(_M0L4dataS2231);
    int32_t _M0L3lenS2230 = _M0L4selfS171->$1;
    int32_t _M0L6_2atmpS2228 = _M0L6_2atmpS2229 - _M0L3lenS2230;
    uint32_t _M0L4codeS172;
    uint16_t* _M0L4dataS2234;
    int32_t _M0L3lenS2235;
    uint32_t _M0L6_2atmpS2238;
    uint32_t _M0L6_2atmpS2237;
    int32_t _M0L6_2atmpS2236;
    uint16_t* _M0L4dataS2239;
    int32_t _M0L3lenS2244;
    int32_t _M0L6_2atmpS2240;
    uint32_t _M0L6_2atmpS2243;
    uint32_t _M0L6_2atmpS2242;
    int32_t _M0L6_2atmpS2241;
    int32_t _M0L3lenS2246;
    int32_t _M0L6_2atmpS2245;
    if (_M0L6_2atmpS2228 < 2) {
      int32_t _M0L3lenS2233 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS2232 = _M0L3lenS2233 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS2232);
    }
    _M0L4codeS172 = _M0L4codeS169 - 65536u;
    _M0L4dataS2234 = _M0L4selfS171->$0;
    _M0L3lenS2235 = _M0L4selfS171->$1;
    _M0L6_2atmpS2238 = _M0L4codeS172 >> 10;
    _M0L6_2atmpS2237 = 55296u + _M0L6_2atmpS2238;
    moonbit_incref_cycle_free(_M0L4dataS2234);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS2236 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS2237);
    if (
      _M0L3lenS2235 < 0
      || _M0L3lenS2235 >= Moonbit_array_length(_M0L4dataS2234)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS2234[_M0L3lenS2235] = _M0L6_2atmpS2236;
    moonbit_decref_cycle_free(_M0L4dataS2234);
    _M0L4dataS2239 = _M0L4selfS171->$0;
    _M0L3lenS2244 = _M0L4selfS171->$1;
    _M0L6_2atmpS2240 = _M0L3lenS2244 + 1;
    _M0L6_2atmpS2243 = _M0L4codeS172 & 1023u;
    _M0L6_2atmpS2242 = 56320u + _M0L6_2atmpS2243;
    moonbit_incref_cycle_free(_M0L4dataS2239);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS2241 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS2242);
    if (
      _M0L6_2atmpS2240 < 0
      || _M0L6_2atmpS2240 >= Moonbit_array_length(_M0L4dataS2239)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS2239[_M0L6_2atmpS2240] = _M0L6_2atmpS2241;
    moonbit_decref_cycle_free(_M0L4dataS2239);
    _M0L3lenS2246 = _M0L4selfS171->$1;
    _M0L6_2atmpS2245 = _M0L3lenS2246 + 2;
    _M0L4selfS171->$1 = _M0L6_2atmpS2245;
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
  uint16_t* _M0L4dataS2217;
  int32_t _M0L6_2atmpS2215;
  int32_t _M0L3lenS2216;
  int32_t _M0L13new__capacityS165;
  uint16_t* _M0L4dataS2212;
  int32_t _M0L6_2atmpS2213;
  int32_t _M0L3lenS2214;
  uint16_t* _M0L9new__dataS168;
  uint16_t* _M0L6_2aoldS5691;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS2217 = _M0L4selfS166->$0;
  _M0L6_2atmpS2215 = Moonbit_array_length(_M0L4dataS2217);
  _M0L3lenS2216 = _M0L4selfS166->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS165
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS2215, _M0L3lenS2216, _M0L8requiredS167);
  _M0L4dataS2212 = _M0L4selfS166->$0;
  moonbit_incref_cycle_free(_M0L4dataS2212);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2213 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS2214 = _M0L4selfS166->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS168
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS2212, _M0L13new__capacityS165, _M0L6_2atmpS2213, _M0L3lenS2214, 0, 0);
  _M0L6_2aoldS5691 = _M0L4selfS166->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5691);
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
  int32_t _M0L6_2atmpS2211;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2211 = *(int32_t*)&_M0L4selfS158;
  return (uint16_t)_M0L6_2atmpS2211;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS157) {
  int32_t _M0L6_2atmpS2210;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2210 = _M0L4selfS157;
  return *(uint32_t*)&_M0L6_2atmpS2210;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS155
) {
  int32_t _M0L3lenS2201;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS2201 = _M0L4selfS155->$1;
  if (_M0L3lenS2201 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS2202 = _M0L4selfS155->$1;
    uint16_t* _M0L4dataS2204 = _M0L4selfS155->$0;
    int32_t _M0L6_2atmpS2203 = Moonbit_array_length(_M0L4dataS2204);
    if (_M0L3lenS2202 == _M0L6_2atmpS2203) {
      uint16_t* _M0L4dataS2205 = _M0L4selfS155->$0;
      moonbit_incref_cycle_free(_M0L4dataS2205);
      return _M0L4dataS2205;
    } else {
      uint16_t* _M0L4dataS2206 = _M0L4selfS155->$0;
      int32_t _M0L3lenS2207 = _M0L4selfS155->$1;
      int32_t _M0L6_2atmpS2208;
      int32_t _M0L3lenS2209;
      uint16_t* _M0L4dataS156;
      moonbit_incref_cycle_free(_M0L4dataS2206);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS2208 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS2209 = _M0L4selfS155->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS156
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS2206, _M0L3lenS2207, _M0L6_2atmpS2208, _M0L3lenS2209, 0, 0);
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
  int32_t _if__result_6164;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS148 >= 0) {
    if (_M0L3lenS149 >= 0) {
      if (_M0L11src__offsetS150 >= 0) {
        if (_M0L11dst__offsetS151 >= 0) {
          int32_t _M0L6_2atmpS2197 = _M0L11src__offsetS150 + _M0L3lenS149;
          int32_t _M0L6_2atmpS2198 = Moonbit_array_length(_M0L3srcS152);
          if (_M0L6_2atmpS2197 <= _M0L6_2atmpS2198) {
            int32_t _M0L6_2atmpS2196 = _M0L11dst__offsetS151 + _M0L3lenS149;
            _if__result_6164 = _M0L6_2atmpS2196 <= _M0L13allocate__lenS148;
          } else {
            _if__result_6164 = 0;
          }
        } else {
          _if__result_6164 = 0;
        }
      } else {
        _if__result_6164 = 0;
      }
    } else {
      _if__result_6164 = 0;
    }
  } else {
    _if__result_6164 = 0;
  }
  if (_if__result_6164) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS152, _M0L13allocate__lenS148, _M0L4initS153, _M0L11src__offsetS150, _M0L11dst__offsetS151, _M0L3lenS149);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS154;
    int32_t _M0L6_2atmpS2200;
    moonbit_string_t _M0L6_2atmpS2199;
    uint16_t* _result_6165;
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
    _M0L6_2atmpS2200 = Moonbit_array_length(_M0L3srcS152);
    moonbit_decref_cycle_free(_M0L3srcS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L6_2atmpS2200);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS2199
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS154);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS154);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_6165 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS2199);
    moonbit_decref_cycle_free(_M0L6_2atmpS2199);
    return _result_6165;
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
  struct _M0TPB13StringBuilder* _block_6166;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS139 < 1) {
    _M0L7initialS138 = 1;
  } else {
    int32_t _M0L6_2atmpS2195 = _M0L10size__hintS139 + 1;
    _M0L7initialS138 = _M0L6_2atmpS2195 / 2;
  }
  _M0L4dataS140 = (uint16_t*)moonbit_make_string(_M0L7initialS138, 0);
  _block_6166
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_6166)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 117, 0);
  _block_6166->$0 = _M0L4dataS140;
  _block_6166->$1 = 0;
  return _block_6166;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS137) {
  int32_t _M0L6_2atmpS2194;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2194 = (int32_t)_M0L4selfS137;
  return _M0L6_2atmpS2194;
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS117,
  int32_t _M0L13allocate__lenS113,
  int32_t _M0L3lenS114,
  int32_t _M0L11src__offsetS115,
  int32_t _M0L11dst__offsetS116
) {
  int32_t _if__result_6167;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS113 >= 0) {
    if (_M0L3lenS114 >= 0) {
      if (_M0L11src__offsetS115 >= 0) {
        if (_M0L11dst__offsetS116 >= 0) {
          int32_t _M0L6_2atmpS2175 = _M0L11src__offsetS115 + _M0L3lenS114;
          int32_t _M0L6_2atmpS2176;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2176
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS117);
          if (_M0L6_2atmpS2175 <= _M0L6_2atmpS2176) {
            int32_t _M0L6_2atmpS2174 = _M0L11dst__offsetS116 + _M0L3lenS114;
            _if__result_6167 = _M0L6_2atmpS2174 <= _M0L13allocate__lenS113;
          } else {
            _if__result_6167 = 0;
          }
        } else {
          _if__result_6167 = 0;
        }
      } else {
        _if__result_6167 = 0;
      }
    } else {
      _if__result_6167 = 0;
    }
  } else {
    _if__result_6167 = 0;
  }
  if (_if__result_6167) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS117, _M0L13allocate__lenS113, _M0L11src__offsetS115, _M0L11dst__offsetS116, _M0L3lenS114);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS118;
    int32_t _M0L6_2atmpS2178;
    moonbit_string_t _M0L6_2atmpS2177;
    int32_t* _result_6168;
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
    _M0L6_2atmpS2178 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS117);
    moonbit_decref_cycle_free(_M0L3srcS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L6_2atmpS2178);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2177
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS118);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS118);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_6168
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS2177);
    moonbit_decref_cycle_free(_M0L6_2atmpS2177);
    return _result_6168;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS123,
  int32_t _M0L13allocate__lenS119,
  int32_t _M0L3lenS120,
  int32_t _M0L11src__offsetS121,
  int32_t _M0L11dst__offsetS122
) {
  int32_t _if__result_6169;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS119 >= 0) {
    if (_M0L3lenS120 >= 0) {
      if (_M0L11src__offsetS121 >= 0) {
        if (_M0L11dst__offsetS122 >= 0) {
          int32_t _M0L6_2atmpS2180 = _M0L11src__offsetS121 + _M0L3lenS120;
          int32_t _M0L6_2atmpS2181;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2181
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS123);
          if (_M0L6_2atmpS2180 <= _M0L6_2atmpS2181) {
            int32_t _M0L6_2atmpS2179 = _M0L11dst__offsetS122 + _M0L3lenS120;
            _if__result_6169 = _M0L6_2atmpS2179 <= _M0L13allocate__lenS119;
          } else {
            _if__result_6169 = 0;
          }
        } else {
          _if__result_6169 = 0;
        }
      } else {
        _if__result_6169 = 0;
      }
    } else {
      _if__result_6169 = 0;
    }
  } else {
    _if__result_6169 = 0;
  }
  if (_if__result_6169) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS119, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS123, _M0L11src__offsetS121, _M0L11dst__offsetS122, _M0L3lenS120);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS124;
    int32_t _M0L6_2atmpS2183;
    moonbit_string_t _M0L6_2atmpS2182;
    moonbit_string_t* _result_6170;
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
    _M0L6_2atmpS2183 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS123);
    moonbit_decref_cycle_free(_M0L3srcS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L6_2atmpS2183);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2182
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS124);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS124);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_6170
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS2182);
    moonbit_decref_cycle_free(_M0L6_2atmpS2182);
    return _result_6170;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS129,
  int32_t _M0L13allocate__lenS125,
  int32_t _M0L3lenS126,
  int32_t _M0L11src__offsetS127,
  int32_t _M0L11dst__offsetS128
) {
  int32_t _if__result_6171;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS125 >= 0) {
    if (_M0L3lenS126 >= 0) {
      if (_M0L11src__offsetS127 >= 0) {
        if (_M0L11dst__offsetS128 >= 0) {
          int32_t _M0L6_2atmpS2185 = _M0L11src__offsetS127 + _M0L3lenS126;
          int32_t _M0L6_2atmpS2186;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2186
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS129);
          if (_M0L6_2atmpS2185 <= _M0L6_2atmpS2186) {
            int32_t _M0L6_2atmpS2184 = _M0L11dst__offsetS128 + _M0L3lenS126;
            _if__result_6171 = _M0L6_2atmpS2184 <= _M0L13allocate__lenS125;
          } else {
            _if__result_6171 = 0;
          }
        } else {
          _if__result_6171 = 0;
        }
      } else {
        _if__result_6171 = 0;
      }
    } else {
      _if__result_6171 = 0;
    }
  } else {
    _if__result_6171 = 0;
  }
  if (_if__result_6171) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS125, 0, _M0L3srcS129, _M0L11src__offsetS127, _M0L11dst__offsetS128, _M0L3lenS126);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS130;
    int32_t _M0L6_2atmpS2188;
    moonbit_string_t _M0L6_2atmpS2187;
    struct _M0TUsiE** _result_6172;
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
    _M0L6_2atmpS2188 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS129);
    moonbit_decref_cycle_free(_M0L3srcS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L6_2atmpS2188);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2187
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS130);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS130);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_6172
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS2187);
    moonbit_decref_cycle_free(_M0L6_2atmpS2187);
    return _result_6172;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS135,
  int32_t _M0L13allocate__lenS131,
  int32_t _M0L3lenS132,
  int32_t _M0L11src__offsetS133,
  int32_t _M0L11dst__offsetS134
) {
  int32_t _if__result_6173;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS131 >= 0) {
    if (_M0L3lenS132 >= 0) {
      if (_M0L11src__offsetS133 >= 0) {
        if (_M0L11dst__offsetS134 >= 0) {
          int32_t _M0L6_2atmpS2190 = _M0L11src__offsetS133 + _M0L3lenS132;
          int32_t _M0L6_2atmpS2191;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2191
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS135);
          if (_M0L6_2atmpS2190 <= _M0L6_2atmpS2191) {
            int32_t _M0L6_2atmpS2189 = _M0L11dst__offsetS134 + _M0L3lenS132;
            _if__result_6173 = _M0L6_2atmpS2189 <= _M0L13allocate__lenS131;
          } else {
            _if__result_6173 = 0;
          }
        } else {
          _if__result_6173 = 0;
        }
      } else {
        _if__result_6173 = 0;
      }
    } else {
      _if__result_6173 = 0;
    }
  } else {
    _if__result_6173 = 0;
  }
  if (_if__result_6173) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS135, _M0L13allocate__lenS131, _M0L11src__offsetS133, _M0L11dst__offsetS134, _M0L3lenS132);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS136;
    int32_t _M0L6_2atmpS2193;
    moonbit_string_t _M0L6_2atmpS2192;
    float* _result_6174;
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
    _M0L6_2atmpS2193 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS135);
    moonbit_decref_cycle_free(_M0L3srcS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L6_2atmpS2193);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2192
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS136);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS136);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_6174
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS2192);
    moonbit_decref_cycle_free(_M0L6_2atmpS2192);
    return _result_6174;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L3objS107
) {
  struct _M0TPB6Logger _M0L6_2atmpS2171;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS108);
  _M0L6_2atmpS2171
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS108
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS107, _M0L6_2atmpS2171);
  if (_M0L6_2atmpS2171.$1) {
    moonbit_decref(_M0L6_2atmpS2171.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS2172;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS2172
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS109, _M0L6_2atmpS2172);
  if (_M0L6_2atmpS2172.$1) {
    moonbit_decref(_M0L6_2atmpS2172.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  uint64_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS2173;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS2173
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS111, _M0L6_2atmpS2173);
  if (_M0L6_2atmpS2173.$1) {
    moonbit_decref(_M0L6_2atmpS2173.$1);
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

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t* _M0L3srcS92,
  int32_t _M0L13allocate__lenS90,
  int32_t _M0L11src__offsetS93,
  int32_t _M0L11dst__offsetS91,
  int32_t _M0L9blit__lenS94
) {
  moonbit_string_t* _M0L3dstS89;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS89
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L13allocate__lenS90, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGsE(_M0L3dstS89, _M0L11dst__offsetS91, _M0L3srcS92, _M0L11src__offsetS93, _M0L9blit__lenS94);
  moonbit_decref_cycle_free(_M0L3srcS92);
  return _M0L3dstS89;
}

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE** _M0L3srcS98,
  int32_t _M0L13allocate__lenS96,
  int32_t _M0L11src__offsetS99,
  int32_t _M0L11dst__offsetS97,
  int32_t _M0L9blit__lenS100
) {
  struct _M0TUsiE** _M0L3dstS95;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS95
  = (struct _M0TUsiE**)moonbit_make_ref_array(_M0L13allocate__lenS96, 0);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGUsiEE(_M0L3dstS95, _M0L11dst__offsetS97, _M0L3srcS98, _M0L11src__offsetS99, _M0L9blit__lenS100);
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS68,
  int32_t _M0L11dst__offsetS69,
  moonbit_string_t* _M0L3srcS70,
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS73,
  int32_t _M0L11dst__offsetS74,
  struct _M0TUsiE** _M0L3srcS75,
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
        int32_t _M0L6_2atmpS2126 = _M0L11dst__offsetS20 + _M0L1iS22;
        int32_t _M0L6_2atmpS2128 = _M0L11src__offsetS21 + _M0L1iS22;
        int32_t _M0L6_2atmpS2127;
        int32_t _M0L6_2atmpS2129;
        if (
          _M0L6_2atmpS2128 < 0
          || _M0L6_2atmpS2128 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2127 = (int32_t)_M0L3srcS19[_M0L6_2atmpS2128];
        if (
          _M0L6_2atmpS2126 < 0
          || _M0L6_2atmpS2126 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS2126] = _M0L6_2atmpS2127;
        _M0L6_2atmpS2129 = _M0L1iS22 + 1;
        _M0L1iS22 = _M0L6_2atmpS2129;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2134 = _M0L3lenS23 - 1;
    int32_t _M0L1iS25 = _M0L6_2atmpS2134;
    while (1) {
      if (_M0L1iS25 >= 0) {
        int32_t _M0L6_2atmpS2130 = _M0L11dst__offsetS20 + _M0L1iS25;
        int32_t _M0L6_2atmpS2132 = _M0L11src__offsetS21 + _M0L1iS25;
        int32_t _M0L6_2atmpS2131;
        int32_t _M0L6_2atmpS2133;
        if (
          _M0L6_2atmpS2132 < 0
          || _M0L6_2atmpS2132 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2131 = (int32_t)_M0L3srcS19[_M0L6_2atmpS2132];
        if (
          _M0L6_2atmpS2130 < 0
          || _M0L6_2atmpS2130 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS2130] = _M0L6_2atmpS2131;
        _M0L6_2atmpS2133 = _M0L1iS25 - 1;
        _M0L1iS25 = _M0L6_2atmpS2133;
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
        int32_t _M0L6_2atmpS2135 = _M0L11dst__offsetS29 + _M0L1iS31;
        int32_t _M0L6_2atmpS2137 = _M0L11src__offsetS30 + _M0L1iS31;
        int32_t _M0L6_2atmpS2136;
        int32_t _M0L6_2atmpS2138;
        if (
          _M0L6_2atmpS2137 < 0
          || _M0L6_2atmpS2137 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2136 = (int32_t)_M0L3srcS28[_M0L6_2atmpS2137];
        if (
          _M0L6_2atmpS2135 < 0
          || _M0L6_2atmpS2135 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS2135] = _M0L6_2atmpS2136;
        _M0L6_2atmpS2138 = _M0L1iS31 + 1;
        _M0L1iS31 = _M0L6_2atmpS2138;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2143 = _M0L3lenS32 - 1;
    int32_t _M0L1iS34 = _M0L6_2atmpS2143;
    while (1) {
      if (_M0L1iS34 >= 0) {
        int32_t _M0L6_2atmpS2139 = _M0L11dst__offsetS29 + _M0L1iS34;
        int32_t _M0L6_2atmpS2141 = _M0L11src__offsetS30 + _M0L1iS34;
        int32_t _M0L6_2atmpS2140;
        int32_t _M0L6_2atmpS2142;
        if (
          _M0L6_2atmpS2141 < 0
          || _M0L6_2atmpS2141 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2140 = (int32_t)_M0L3srcS28[_M0L6_2atmpS2141];
        if (
          _M0L6_2atmpS2139 < 0
          || _M0L6_2atmpS2139 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS2139] = _M0L6_2atmpS2140;
        _M0L6_2atmpS2142 = _M0L1iS34 - 1;
        _M0L1iS34 = _M0L6_2atmpS2142;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS36,
  int32_t _M0L11dst__offsetS38,
  moonbit_string_t* _M0L3srcS37,
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
        int32_t _M0L6_2atmpS2144 = _M0L11dst__offsetS38 + _M0L1iS40;
        int32_t _M0L6_2atmpS2146 = _M0L11src__offsetS39 + _M0L1iS40;
        moonbit_string_t _M0L6_2atmpS2145;
        moonbit_string_t _M0L6_2aoldS5692;
        int32_t _M0L6_2atmpS2147;
        if (
          _M0L6_2atmpS2146 < 0
          || _M0L6_2atmpS2146 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2145 = (moonbit_string_t)_M0L3srcS37[_M0L6_2atmpS2146];
        if (
          _M0L6_2atmpS2144 < 0
          || _M0L6_2atmpS2144 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5692 = (moonbit_string_t)_M0L3dstS36[_M0L6_2atmpS2144];
        moonbit_incref_cycle_free(_M0L6_2atmpS2145);
        moonbit_decref_cycle_free(_M0L6_2aoldS5692);
        _M0L3dstS36[_M0L6_2atmpS2144] = _M0L6_2atmpS2145;
        _M0L6_2atmpS2147 = _M0L1iS40 + 1;
        _M0L1iS40 = _M0L6_2atmpS2147;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2152 = _M0L3lenS41 - 1;
    int32_t _M0L1iS43 = _M0L6_2atmpS2152;
    while (1) {
      if (_M0L1iS43 >= 0) {
        int32_t _M0L6_2atmpS2148 = _M0L11dst__offsetS38 + _M0L1iS43;
        int32_t _M0L6_2atmpS2150 = _M0L11src__offsetS39 + _M0L1iS43;
        moonbit_string_t _M0L6_2atmpS2149;
        moonbit_string_t _M0L6_2aoldS5693;
        int32_t _M0L6_2atmpS2151;
        if (
          _M0L6_2atmpS2150 < 0
          || _M0L6_2atmpS2150 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2149 = (moonbit_string_t)_M0L3srcS37[_M0L6_2atmpS2150];
        if (
          _M0L6_2atmpS2148 < 0
          || _M0L6_2atmpS2148 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5693 = (moonbit_string_t)_M0L3dstS36[_M0L6_2atmpS2148];
        moonbit_incref_cycle_free(_M0L6_2atmpS2149);
        moonbit_decref_cycle_free(_M0L6_2aoldS5693);
        _M0L3dstS36[_M0L6_2atmpS2148] = _M0L6_2atmpS2149;
        _M0L6_2atmpS2151 = _M0L1iS43 - 1;
        _M0L1iS43 = _M0L6_2atmpS2151;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS45,
  int32_t _M0L11dst__offsetS47,
  struct _M0TUsiE** _M0L3srcS46,
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
        int32_t _M0L6_2atmpS2153 = _M0L11dst__offsetS47 + _M0L1iS49;
        int32_t _M0L6_2atmpS2155 = _M0L11src__offsetS48 + _M0L1iS49;
        struct _M0TUsiE* _M0L6_2atmpS2154;
        struct _M0TUsiE* _M0L6_2aoldS5694;
        int32_t _M0L6_2atmpS2156;
        if (
          _M0L6_2atmpS2155 < 0
          || _M0L6_2atmpS2155 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2154 = (struct _M0TUsiE*)_M0L3srcS46[_M0L6_2atmpS2155];
        if (
          _M0L6_2atmpS2153 < 0
          || _M0L6_2atmpS2153 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5694 = (struct _M0TUsiE*)_M0L3dstS45[_M0L6_2atmpS2153];
        if (_M0L6_2atmpS2154) {
          moonbit_incref_cycle_free(_M0L6_2atmpS2154);
        }
        if (_M0L6_2aoldS5694) {
          moonbit_decref_cycle_free(_M0L6_2aoldS5694);
        }
        _M0L3dstS45[_M0L6_2atmpS2153] = _M0L6_2atmpS2154;
        _M0L6_2atmpS2156 = _M0L1iS49 + 1;
        _M0L1iS49 = _M0L6_2atmpS2156;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2161 = _M0L3lenS50 - 1;
    int32_t _M0L1iS52 = _M0L6_2atmpS2161;
    while (1) {
      if (_M0L1iS52 >= 0) {
        int32_t _M0L6_2atmpS2157 = _M0L11dst__offsetS47 + _M0L1iS52;
        int32_t _M0L6_2atmpS2159 = _M0L11src__offsetS48 + _M0L1iS52;
        struct _M0TUsiE* _M0L6_2atmpS2158;
        struct _M0TUsiE* _M0L6_2aoldS5695;
        int32_t _M0L6_2atmpS2160;
        if (
          _M0L6_2atmpS2159 < 0
          || _M0L6_2atmpS2159 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2158 = (struct _M0TUsiE*)_M0L3srcS46[_M0L6_2atmpS2159];
        if (
          _M0L6_2atmpS2157 < 0
          || _M0L6_2atmpS2157 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5695 = (struct _M0TUsiE*)_M0L3dstS45[_M0L6_2atmpS2157];
        if (_M0L6_2atmpS2158) {
          moonbit_incref_cycle_free(_M0L6_2atmpS2158);
        }
        if (_M0L6_2aoldS5695) {
          moonbit_decref_cycle_free(_M0L6_2aoldS5695);
        }
        _M0L3dstS45[_M0L6_2atmpS2157] = _M0L6_2atmpS2158;
        _M0L6_2atmpS2160 = _M0L1iS52 - 1;
        _M0L1iS52 = _M0L6_2atmpS2160;
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
        int32_t _M0L6_2atmpS2162 = _M0L11dst__offsetS56 + _M0L1iS58;
        int32_t _M0L6_2atmpS2164 = _M0L11src__offsetS57 + _M0L1iS58;
        float _M0L6_2atmpS2163;
        int32_t _M0L6_2atmpS2165;
        if (
          _M0L6_2atmpS2164 < 0
          || _M0L6_2atmpS2164 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2163 = (float)_M0L3srcS55[_M0L6_2atmpS2164];
        if (
          _M0L6_2atmpS2162 < 0
          || _M0L6_2atmpS2162 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS2162] = _M0L6_2atmpS2163;
        _M0L6_2atmpS2165 = _M0L1iS58 + 1;
        _M0L1iS58 = _M0L6_2atmpS2165;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2170 = _M0L3lenS59 - 1;
    int32_t _M0L1iS61 = _M0L6_2atmpS2170;
    while (1) {
      if (_M0L1iS61 >= 0) {
        int32_t _M0L6_2atmpS2166 = _M0L11dst__offsetS56 + _M0L1iS61;
        int32_t _M0L6_2atmpS2168 = _M0L11src__offsetS57 + _M0L1iS61;
        float _M0L6_2atmpS2167;
        int32_t _M0L6_2atmpS2169;
        if (
          _M0L6_2atmpS2168 < 0
          || _M0L6_2atmpS2168 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2167 = (float)_M0L3srcS55[_M0L6_2atmpS2168];
        if (
          _M0L6_2atmpS2166 < 0
          || _M0L6_2atmpS2166 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS54[_M0L6_2atmpS2166] = _M0L6_2atmpS2167;
        _M0L6_2atmpS2169 = _M0L1iS61 - 1;
        _M0L1iS61 = _M0L6_2atmpS2169;
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

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t* _M0L4selfS15) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS15);
}

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(
  struct _M0TUsiE** _M0L4selfS16
) {
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

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(
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

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(
  moonbit_string_t _M0L3msgS7
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS7);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS2093) {
  switch (Moonbit_object_tag(_M0L4_2aeS2093)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_35.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS2093);
      break;
    }
    
    case 3: {
      return (moonbit_string_t)moonbit_string_literal_36.data;
      break;
    }
    
    case 4: {
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
  void* _M0L11_2aobj__ptrS2121,
  struct _M0TPB4Show _M0L8_2aparamS2120
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2119 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2121;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS2119, _M0L8_2aparamS2120);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS2118,
  struct _M0TPB4Show _M0L8_2aparamS2117
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2116 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2118;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS2116, _M0L8_2aparamS2117);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS2115,
  int32_t _M0L8_2aparamS2114
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2113 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2115;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS2113, _M0L8_2aparamS2114);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS2112,
  struct _M0TPC16string10StringView _M0L8_2aparamS2111
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2110 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2112;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS2110, _M0L8_2aparamS2111);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS2109,
  moonbit_string_t _M0L8_2aparamS2106,
  int32_t _M0L8_2aparamS2107,
  int32_t _M0L8_2aparamS2108
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2105 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2109;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS2105, _M0L8_2aparamS2106, _M0L8_2aparamS2107, _M0L8_2aparamS2108);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS2104,
  moonbit_string_t _M0L8_2aparamS2103
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2102 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2104;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS2102, _M0L8_2aparamS2103);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_6185 = 9218868437227405311ll;
  int64_t _tmp_6186;
  int64_t _tmp_6187;
  int64_t _tmp_6188;
  int64_t _tmp_6189;
  _M0FPB18double__max__value = *(double*)&_tmp_6185;
  _tmp_6186 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_6186;
  _tmp_6187 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_6187;
  _tmp_6188 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_6188;
  _tmp_6189 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_6189;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS2125;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS2086;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS2087;
  int32_t _M0L7_2abindS2088;
  struct _M0TUsiE** _M0L7_2abindS2089;
  int32_t _M0L6_2acntS5872;
  int32_t _M0L2__S2090;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS2125
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS2086
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS2086)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 120, 0);
  _M0L12async__testsS2086->$0 = _M0L6_2atmpS2125;
  _M0L12async__testsS2086->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS2087
  = _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS2088 = _M0L7_2abindS2087->$1;
  _M0L7_2abindS2089 = _M0L7_2abindS2087->$0;
  _M0L6_2acntS5872
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS2087));
  if (_M0L6_2acntS5872 > 1) {
    int32_t _M0L11_2anew__cntS5873 = _M0L6_2acntS5872 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS2087), _M0L11_2anew__cntS5873);
    moonbit_incref_cycle_free(_M0L7_2abindS2089);
  } else if (_M0L6_2acntS5872 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS2087);
  }
  _M0L2__S2090 = 0;
  while (1) {
    if (_M0L2__S2090 < _M0L7_2abindS2088) {
      struct _M0TUsiE* _M0L3argS2091 =
        (struct _M0TUsiE*)_M0L7_2abindS2089[_M0L2__S2090];
      moonbit_string_t _M0L6_2atmpS2122 = _M0L3argS2091->$0;
      int32_t _M0L6_2atmpS2123 = _M0L3argS2091->$1;
      int32_t _M0L6_2atmpS2124;
      moonbit_incref_cycle_free(_M0L6_2atmpS2122);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples27poisson__if__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS2086, _M0L6_2atmpS2122, _M0L6_2atmpS2123);
      moonbit_decref_cycle_free(_M0L6_2atmpS2122);
      _M0L6_2atmpS2124 = _M0L2__S2090 + 1;
      _M0L2__S2090 = _M0L6_2atmpS2124;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS2089);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_if\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples27poisson__if__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples27poisson__if__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS2086);
  moonbit_decref_cycle_free(_M0L12async__testsS2086);
  moonbit_flush_cycles();
  return 0;
}