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

struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1036;

struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1031;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TWRPC15error5ErrorEs;

struct _M0TP26RiantR8snn__mbt12PoissonLayer;

struct _M0TPB4Show;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TUdiE;

struct _M0TPB5ArrayGbE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB6Logger;

struct _M0TPB8MutLocalGdE;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TPB5ArrayGUsiEE;

struct _M0TPB5ArrayGsE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TWEu;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus;

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

struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1036 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1031 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
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

struct _M0TUmmmmE {
  uint64_t $0;
  uint64_t $1;
  uint64_t $2;
  uint64_t $3;
  
};

struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus {
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* $0;
  struct _M0TP26RiantR8snn__mbt2IF* $1;
  moonbit_string_t $2;
  struct _M0TPB5ArrayGbE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGbE* $5;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* $6;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1043(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1036(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1031(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1008(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1001(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
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

int32_t _M0FP26RiantR8snn__mbt16stimulate__layer(
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus*,
  float,
  float
);

struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0MP26RiantR8snn__mbt20PoissonLayerStimulus3new(
  struct _M0TP26RiantR8snn__mbt12PoissonLayer*,
  struct _M0TP26RiantR8snn__mbt2IF*,
  moonbit_string_t,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0MP26RiantR8snn__mbt12PoissonLayer10with__conn(
  float,
  int32_t,
  struct _M0TPB5ArrayGbE*,
  float,
  float,
  float,
  moonbit_string_t,
  moonbit_string_t
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

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

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

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(int32_t);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(int32_t);

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(uint64_t*, int32_t);

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(uint32_t*, int32_t);

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(uint64_t);

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t);

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t);

int32_t _M0MPC15array5Array4pushGbE(struct _M0TPB5ArrayGbE*, int32_t);

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE*,
  moonbit_string_t
);

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  struct _M0TUsiE*
);

int32_t _M0MPC15array5Array7reallocGbE(struct _M0TPB5ArrayGbE*, int32_t);

int32_t _M0MPC15array5Array7reallocGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  int32_t
);

int32_t _M0MPC15array5Array14resize__bufferGbE(
  struct _M0TPB5ArrayGbE*,
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

int32_t _M0MPC15array5Array8capacityGbE(struct _M0TPB5ArrayGbE*);

int32_t _M0MPC15array5Array8capacityGsE(struct _M0TPB5ArrayGsE*);

int32_t _M0MPC15array5Array8capacityGUsiEE(struct _M0TPB5ArrayGUsiEE*);

int32_t _M0FPB23array__growth__capacity(int32_t, int32_t, int32_t);

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE*);

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

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

uint8_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGbE(
  uint8_t*,
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

uint8_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGbE(
  uint8_t*,
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGbE(
  uint8_t*,
  int32_t,
  uint8_t*,
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGbEE(
  uint8_t*,
  int32_t,
  uint8_t*,
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

int32_t _M0MPB18UninitializedArray6lengthGbE(uint8_t*);

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t*);

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(struct _M0TUsiE**);

int32_t _M0IPB7FailurePB4Show6output(void*, struct _M0TPB6Logger);

int32_t _M0MPB6Logger13write__objectGsE(
  struct _M0TPB6Logger,
  moonbit_string_t
);

int32_t _M0FPC15abort5abortGuE(moonbit_string_t);

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t);

uint8_t* _M0FPC15abort5abortGRPB18UninitializedArrayGbEE(moonbit_string_t);

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
} const moonbit_string_literal_23 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_21 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_25 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_14 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[118]; 
} const moonbit_string_literal_36 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 117, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 112, 111, 105, 115, 115, 111, 110, 
    95, 108, 97, 121, 101, 114, 95, 98, 108, 97, 99, 107, 98, 111, 120, 
    95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 
    101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 
    110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 46, 77, 111, 111, 
    110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 
    73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 
    114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_20 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_18 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_15 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_13 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[25]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 24, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 114, 101, 115, 117, 108, 116, 34, 
    44, 34, 102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[7]; 
} const moonbit_string_literal_10 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 6, 78, 111, 
    114, 109, 97, 108, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_11 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_24 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_9 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[120]; 
} const moonbit_string_literal_37 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 119, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 112, 111, 105, 115, 115, 111, 110, 
    95, 108, 97, 121, 101, 114, 95, 98, 108, 97, 99, 107, 98, 111, 120, 
    95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 
    101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 
    110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 46, 77, 111, 
    111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 
    114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 
    101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_33 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_19 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_22 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 73, 110, 115, 112, 101, 
    99, 116, 69, 114, 114, 111, 114, 46, 73, 110, 115, 112, 101, 99, 
    116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_31 =
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
} const moonbit_string_literal_28 =
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
} const moonbit_string_literal_17 =
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
} const moonbit_string_literal_12 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_32 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_26 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_16 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1043$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1043
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[77] =
  {
    sizeof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1031)
    / 4, 1,
    offsetof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1031, $1)
    / 4
    * 2,
    sizeof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1036)
    / 4, 1,
    offsetof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1036, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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
    sizeof(struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus) / 4, 
    7,
    offsetof(struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus, $6) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt12PoissonLayer) / 4, 3,
    offsetof(struct _M0TP26RiantR8snn__mbt12PoissonLayer, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12PoissonLayer, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12PoissonLayer, $7) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

float _M0FP26RiantR8snn__mbt2hz = 0x1.0624dd2f1a9fcp-10f;

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2216
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1064,
  moonbit_string_t _M0L8filenameS1033,
  int32_t _M0L5indexS1035
) {
  struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1031* _closure_2242;
  struct _M0TWEu* _M0L13handle__startS1031;
  struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1036* _closure_2243;
  struct _M0TWssbEu* _M0L14handle__resultS1036;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1043;
  void* _M0L11_2atry__errS1058;
  struct moonbit_result_0 _tmp_2245;
  int32_t _handle__error__result_2246;
  int32_t _M0L6_2atmpS2204;
  void* _M0L3errS1059;
  moonbit_string_t _M0L4nameS1061;
  struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1062;
  moonbit_string_t _M0L7_2anameS1063;
  int32_t _M0L6_2acntS2236;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1033);
  _closure_2242
  = (struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1031*)moonbit_malloc(sizeof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1031));
  Moonbit_object_header(_closure_2242)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2242->code
  = &_M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1031;
  _closure_2242->$0 = _M0L5indexS1035;
  _closure_2242->$1 = _M0L8filenameS1033;
  _M0L13handle__startS1031 = (struct _M0TWEu*)_closure_2242;
  moonbit_incref_cycle_free(_M0L8filenameS1033);
  _closure_2243
  = (struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1036*)moonbit_malloc(sizeof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1036));
  Moonbit_object_header(_closure_2243)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2243->code
  = &_M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1036;
  _closure_2243->$0 = _M0L5indexS1035;
  _closure_2243->$1 = _M0L8filenameS1033;
  _M0L14handle__resultS1036 = (struct _M0TWssbEu*)_closure_2243;
  _M0L17error__to__stringS1043
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1043$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2245
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1064, _M0L8filenameS1033, _M0L5indexS1035, _M0L13handle__startS1031, _M0L14handle__resultS1036, _M0L17error__to__stringS1043);
  if (_tmp_2245.tag) {
    int32_t const _M0L5_2aokS2213 = _tmp_2245.data.ok;
    _handle__error__result_2246 = _M0L5_2aokS2213;
  } else {
    void* const _M0L6_2aerrS2214 = _tmp_2245.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1043);
    moonbit_decref_cycle_free(_M0L13handle__startS1031);
    _M0L11_2atry__errS1058 = _M0L6_2aerrS2214;
    goto join_1057;
  }
  if (_handle__error__result_2246) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1043);
    moonbit_decref_cycle_free(_M0L13handle__startS1031);
    _M0L6_2atmpS2204 = 1;
  } else {
    struct moonbit_result_0 _tmp_2247;
    int32_t _handle__error__result_2248;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2247
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1064, _M0L8filenameS1033, _M0L5indexS1035, _M0L13handle__startS1031, _M0L14handle__resultS1036, _M0L17error__to__stringS1043);
    if (_tmp_2247.tag) {
      int32_t const _M0L5_2aokS2211 = _tmp_2247.data.ok;
      _handle__error__result_2248 = _M0L5_2aokS2211;
    } else {
      void* const _M0L6_2aerrS2212 = _tmp_2247.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1043);
      moonbit_decref_cycle_free(_M0L13handle__startS1031);
      _M0L11_2atry__errS1058 = _M0L6_2aerrS2212;
      goto join_1057;
    }
    if (_handle__error__result_2248) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1043);
      moonbit_decref_cycle_free(_M0L13handle__startS1031);
      _M0L6_2atmpS2204 = 1;
    } else {
      struct moonbit_result_0 _tmp_2249;
      int32_t _handle__error__result_2250;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2249
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1064, _M0L8filenameS1033, _M0L5indexS1035, _M0L13handle__startS1031, _M0L14handle__resultS1036, _M0L17error__to__stringS1043);
      if (_tmp_2249.tag) {
        int32_t const _M0L5_2aokS2209 = _tmp_2249.data.ok;
        _handle__error__result_2250 = _M0L5_2aokS2209;
      } else {
        void* const _M0L6_2aerrS2210 = _tmp_2249.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1043);
        moonbit_decref_cycle_free(_M0L13handle__startS1031);
        _M0L11_2atry__errS1058 = _M0L6_2aerrS2210;
        goto join_1057;
      }
      if (_handle__error__result_2250) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1043);
        moonbit_decref_cycle_free(_M0L13handle__startS1031);
        _M0L6_2atmpS2204 = 1;
      } else {
        struct moonbit_result_0 _tmp_2251;
        int32_t _handle__error__result_2252;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2251
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1064, _M0L8filenameS1033, _M0L5indexS1035, _M0L13handle__startS1031, _M0L14handle__resultS1036, _M0L17error__to__stringS1043);
        if (_tmp_2251.tag) {
          int32_t const _M0L5_2aokS2207 = _tmp_2251.data.ok;
          _handle__error__result_2252 = _M0L5_2aokS2207;
        } else {
          void* const _M0L6_2aerrS2208 = _tmp_2251.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1043);
          moonbit_decref_cycle_free(_M0L13handle__startS1031);
          _M0L11_2atry__errS1058 = _M0L6_2aerrS2208;
          goto join_1057;
        }
        if (_handle__error__result_2252) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1043);
          moonbit_decref_cycle_free(_M0L13handle__startS1031);
          _M0L6_2atmpS2204 = 1;
        } else {
          struct moonbit_result_0 _tmp_2253;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2253
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1064, _M0L8filenameS1033, _M0L5indexS1035, _M0L13handle__startS1031, _M0L14handle__resultS1036, _M0L17error__to__stringS1043);
          moonbit_decref_cycle_free(_M0L13handle__startS1031);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1043);
          if (_tmp_2253.tag) {
            int32_t const _M0L5_2aokS2205 = _tmp_2253.data.ok;
            _M0L6_2atmpS2204 = _M0L5_2aokS2205;
          } else {
            void* const _M0L6_2aerrS2206 = _tmp_2253.data.err;
            _M0L11_2atry__errS1058 = _M0L6_2aerrS2206;
            goto join_1057;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2204) {
    void* _M0L133RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2215 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L133RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2215)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L133RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2215)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1058
    = _M0L133RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2215;
    goto join_1057;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1036);
  }
  goto joinlet_2244;
  join_1057:;
  _M0L3errS1059 = _M0L11_2atry__errS1058;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1062
  = (struct _M0DTPC15error5Error133RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1059;
  _M0L7_2anameS1063 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1062->$0;
  _M0L6_2acntS2236
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1062));
  if (_M0L6_2acntS2236 > 1) {
    int32_t _M0L11_2anew__cntS2237 = _M0L6_2acntS2236 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1062), _M0L11_2anew__cntS2237);
    moonbit_incref_cycle_free(_M0L7_2anameS1063);
  } else if (_M0L6_2acntS2236 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1062);
  }
  _M0L4nameS1061 = _M0L7_2anameS1063;
  goto join_1060;
  goto joinlet_2254;
  join_1060:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1036(_M0L14handle__resultS1036, _M0L4nameS1061, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1036);
  moonbit_decref_cycle_free(_M0L4nameS1061);
  joinlet_2254:;
  joinlet_2244:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1043(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2203,
  void* _M0L3errS1044
) {
  void* _M0L1eS1046;
  moonbit_string_t _M0L1eS1048;
  moonbit_string_t _result_2257;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1044)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1049 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1044;
      moonbit_string_t _M0L4_2aeS1050 = _M0L10_2aFailureS1049->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1050);
      _M0L1eS1048 = _M0L4_2aeS1050;
      goto join_1047;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1051 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1044;
      moonbit_string_t _M0L4_2aeS1052 = _M0L15_2aInspectErrorS1051->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1052);
      _M0L1eS1048 = _M0L4_2aeS1052;
      goto join_1047;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1053 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1044;
      moonbit_string_t _M0L4_2aeS1054 = _M0L16_2aSnapshotErrorS1053->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1054);
      _M0L1eS1048 = _M0L4_2aeS1054;
      goto join_1047;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1055 =
        (struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1044;
      moonbit_string_t _M0L4_2aeS1056 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1055->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1056);
      _M0L1eS1048 = _M0L4_2aeS1056;
      goto join_1047;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1044);
      _M0L1eS1046 = _M0L3errS1044;
      goto join_1045;
      break;
    }
  }
  join_1047:;
  return _M0L1eS1048;
  join_1045:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _result_2257 = _M0FP15Error10to__string(_M0L1eS1046);
  moonbit_decref_cycle_free(_M0L1eS1046);
  return _result_2257;
}

int32_t _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1036(
  struct _M0TWssbEu* _M0L6_2aenvS2200,
  moonbit_string_t _M0L10__testnameS1037,
  moonbit_string_t _M0L7messageS1038,
  int32_t _M0L7skippedS1039
) {
  struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1036* _M0L14_2acasted__envS2201;
  moonbit_string_t _M0L8filenameS1033;
  int32_t _M0L5indexS1035;
  moonbit_string_t _M0L10file__nameS1040;
  moonbit_string_t _M0L7messageS1041;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1042;
  moonbit_string_t _M0L6_2atmpS2202;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2201
  = (struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1036*)_M0L6_2aenvS2200;
  _M0L8filenameS1033 = _M0L14_2acasted__envS2201->$1;
  _M0L5indexS1035 = _M0L14_2acasted__envS2201->$0;
  if (!_M0L7skippedS1039 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1040
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1033, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1041
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1038, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1042
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1042, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1042, _M0L10file__nameS1040);
  moonbit_decref_cycle_free(_M0L10file__nameS1040);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1042, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1042, _M0L5indexS1035);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1042, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1042, _M0L7messageS1041);
  moonbit_decref_cycle_free(_M0L7messageS1041);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1042, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2202
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1042);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1042);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2202);
  moonbit_decref_cycle_free(_M0L6_2atmpS2202);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1031(
  struct _M0TWEu* _M0L6_2aenvS2197
) {
  struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1031* _M0L14_2acasted__envS2198;
  moonbit_string_t _M0L8filenameS1033;
  int32_t _M0L5indexS1035;
  moonbit_string_t _M0L10file__nameS1032;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1034;
  moonbit_string_t _M0L6_2atmpS2199;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2198
  = (struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__layer__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1031*)_M0L6_2aenvS2197;
  _M0L8filenameS1033 = _M0L14_2acasted__envS2198->$1;
  _M0L5indexS1035 = _M0L14_2acasted__envS2198->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1032
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1033, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1034
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1034, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1034, _M0L10file__nameS1032);
  moonbit_decref_cycle_free(_M0L10file__nameS1032);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1034, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1034, _M0L5indexS1035);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1034, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2199
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1034);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1034);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2199);
  moonbit_decref_cycle_free(_M0L6_2atmpS2199);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1001;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1008;
  struct _M0TUsiE** _M0L6_2atmpS2196;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1015;
  moonbit_string_t* _M0L9cli__argsS1016;
  moonbit_string_t _M0L6_2atmpS2195;
  moonbit_string_t _M0L6_2atmpS2194;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1017;
  int32_t _M0L7_2abindS1018;
  moonbit_string_t* _M0L7_2abindS1019;
  int32_t _M0L6_2acntS2238;
  int32_t _M0L2__S1020;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1001 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1008 = 0;
  _M0L6_2atmpS2196 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1015
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1015)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1015->$0 = _M0L6_2atmpS2196;
  _M0L16file__and__indexS1015->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1016
  = _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1016)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2195 = (moonbit_string_t)_M0L9cli__argsS1016[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2195);
  moonbit_decref_cycle_free(_M0L9cli__argsS1016);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2194
  = _M0MP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2195);
  moonbit_decref_cycle_free(_M0L6_2atmpS2195);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1017
  = _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1008(_M0L51moonbit__test__driver__internal__split__mbt__stringS1008, _M0L6_2atmpS2194, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2194);
  _M0L7_2abindS1018 = _M0L10test__argsS1017->$1;
  _M0L7_2abindS1019 = _M0L10test__argsS1017->$0;
  _M0L6_2acntS2238
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1017));
  if (_M0L6_2acntS2238 > 1) {
    int32_t _M0L11_2anew__cntS2239 = _M0L6_2acntS2238 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1017), _M0L11_2anew__cntS2239);
    moonbit_incref_cycle_free(_M0L7_2abindS1019);
  } else if (_M0L6_2acntS2238 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1017);
  }
  _M0L2__S1020 = 0;
  while (1) {
    if (_M0L2__S1020 < _M0L7_2abindS1018) {
      moonbit_string_t _M0L3argS1021 =
        (moonbit_string_t)_M0L7_2abindS1019[_M0L2__S1020];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1022;
      moonbit_string_t _M0L4fileS1023;
      moonbit_string_t _M0L5rangeS1024;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1025;
      moonbit_string_t _M0L6_2atmpS2192;
      int32_t _M0L5startS1026;
      moonbit_string_t _M0L6_2atmpS2191;
      int32_t _M0L3endS1027;
      int32_t _M0L1iS1028;
      int32_t _M0L6_2atmpS2193;
      moonbit_incref_cycle_free(_M0L3argS1021);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1022
      = _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1008(_M0L51moonbit__test__driver__internal__split__mbt__stringS1008, _M0L3argS1021, 58);
      moonbit_decref_cycle_free(_M0L3argS1021);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1023
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1022, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1024
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1022, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1022);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1025
      = _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1008(_M0L51moonbit__test__driver__internal__split__mbt__stringS1008, _M0L5rangeS1024, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1024);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2192
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1025, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1026
      = _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1001(_M0L45moonbit__test__driver__internal__parse__int__S1001, _M0L6_2atmpS2192);
      moonbit_decref_cycle_free(_M0L6_2atmpS2192);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2191
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1025, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1025);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1027
      = _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1001(_M0L45moonbit__test__driver__internal__parse__int__S1001, _M0L6_2atmpS2191);
      moonbit_decref_cycle_free(_M0L6_2atmpS2191);
      _M0L1iS1028 = _M0L5startS1026;
      while (1) {
        if (_M0L1iS1028 < _M0L3endS1027) {
          struct _M0TUsiE* _M0L8_2atupleS2189;
          int32_t _M0L6_2atmpS2190;
          moonbit_incref_cycle_free(_M0L4fileS1023);
          _M0L8_2atupleS2189
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS2189)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS2189->$0 = _M0L4fileS1023;
          _M0L8_2atupleS2189->$1 = _M0L1iS1028;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1015, _M0L8_2atupleS2189);
          _M0L6_2atmpS2190 = _M0L1iS1028 + 1;
          _M0L1iS1028 = _M0L6_2atmpS2190;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1023);
        }
        break;
      }
      _M0L6_2atmpS2193 = _M0L2__S1020 + 1;
      _M0L2__S1020 = _M0L6_2atmpS2193;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1019);
    }
    break;
  }
  return _M0L16file__and__indexS1015;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1008(
  int32_t _M0L6_2aenvS2170,
  moonbit_string_t _M0L1sS1009,
  int32_t _M0L3sepS1010
) {
  moonbit_string_t* _M0L6_2atmpS2188;
  struct _M0TPB5ArrayGsE* _M0L3resS1011;
  struct _M0TPB8MutLocalGiE* _M0L1iS1012;
  struct _M0TPB8MutLocalGiE* _M0L5startS1013;
  int32_t _M0L3valS2183;
  int32_t _M0L6_2atmpS2184;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2188 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1011
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1011)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1011->$0 = _M0L6_2atmpS2188;
  _M0L3resS1011->$1 = 0;
  _M0L1iS1012
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1012)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1012->$0 = 0;
  _M0L5startS1013
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1013)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1013->$0 = 0;
  while (1) {
    int32_t _M0L3valS2171 = _M0L1iS1012->$0;
    int32_t _M0L6_2atmpS2172 = Moonbit_array_length(_M0L1sS1009);
    if (_M0L3valS2171 < _M0L6_2atmpS2172) {
      int32_t _M0L3valS2175 = _M0L1iS1012->$0;
      int32_t _M0L6_2atmpS2174;
      int32_t _M0L6_2atmpS2173;
      int32_t _M0L3valS2182;
      int32_t _M0L6_2atmpS2181;
      if (
        _M0L3valS2175 < 0
        || _M0L3valS2175 >= Moonbit_array_length(_M0L1sS1009)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2174 = _M0L1sS1009[_M0L3valS2175];
      _M0L6_2atmpS2173 = _M0L6_2atmpS2174;
      if (_M0L6_2atmpS2173 == _M0L3sepS1010) {
        int32_t _M0L3valS2177 = _M0L5startS1013->$0;
        int32_t _M0L3valS2178 = _M0L1iS1012->$0;
        moonbit_string_t _M0L6_2atmpS2176;
        int32_t _M0L3valS2180;
        int32_t _M0L6_2atmpS2179;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS2176
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1009, _M0L3valS2177, _M0L3valS2178);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1011, _M0L6_2atmpS2176);
        _M0L3valS2180 = _M0L1iS1012->$0;
        _M0L6_2atmpS2179 = _M0L3valS2180 + 1;
        _M0L5startS1013->$0 = _M0L6_2atmpS2179;
      }
      _M0L3valS2182 = _M0L1iS1012->$0;
      _M0L6_2atmpS2181 = _M0L3valS2182 + 1;
      _M0L1iS1012->$0 = _M0L6_2atmpS2181;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1012);
    }
    break;
  }
  _M0L3valS2183 = _M0L5startS1013->$0;
  _M0L6_2atmpS2184 = Moonbit_array_length(_M0L1sS1009);
  if (_M0L3valS2183 < _M0L6_2atmpS2184) {
    int32_t _M0L3valS2186 = _M0L5startS1013->$0;
    int32_t _M0L6_2atmpS2187;
    moonbit_string_t _M0L6_2atmpS2185;
    moonbit_decref_cycle_free(_M0L5startS1013);
    _M0L6_2atmpS2187 = Moonbit_array_length(_M0L1sS1009);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS2185
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1009, _M0L3valS2186, _M0L6_2atmpS2187);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1011, _M0L6_2atmpS2185);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1013);
  }
  return _M0L3resS1011;
}

int32_t _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1001(
  int32_t _M0L6_2aenvS2163,
  moonbit_string_t _M0L1sS1002
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1003;
  int32_t _M0L3lenS1004;
  int32_t _M0L7_2abindS1005;
  int32_t _M0L1iS1006;
  int32_t _result_2262;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1003
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1003)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1003->$0 = 0;
  _M0L3lenS1004 = Moonbit_array_length(_M0L1sS1002);
  _M0L7_2abindS1005 = 0;
  _M0L1iS1006 = _M0L7_2abindS1005;
  while (1) {
    if (_M0L1iS1006 < _M0L3lenS1004) {
      int32_t _M0L3valS2168 = _M0L3resS1003->$0;
      int32_t _M0L6_2atmpS2165 = _M0L3valS2168 * 10;
      int32_t _M0L6_2atmpS2167;
      int32_t _M0L6_2atmpS2166;
      int32_t _M0L6_2atmpS2164;
      int32_t _M0L6_2atmpS2169;
      if (
        _M0L1iS1006 < 0 || _M0L1iS1006 >= Moonbit_array_length(_M0L1sS1002)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2167 = _M0L1sS1002[_M0L1iS1006];
      _M0L6_2atmpS2166 = _M0L6_2atmpS2167 - 48;
      _M0L6_2atmpS2164 = _M0L6_2atmpS2165 + _M0L6_2atmpS2166;
      _M0L3resS1003->$0 = _M0L6_2atmpS2164;
      _M0L6_2atmpS2169 = _M0L1iS1006 + 1;
      _M0L1iS1006 = _M0L6_2atmpS2169;
      continue;
    }
    break;
  }
  _result_2262 = _M0L3resS1003->$0;
  moonbit_decref_cycle_free(_M0L3resS1003);
  return _result_2262;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1000
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1000);
  return _M0L4selfS1000;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S970,
  moonbit_string_t _M0L12_2adiscard__S971,
  int32_t _M0L12_2adiscard__S972,
  struct _M0TWEu* _M0L12_2adiscard__S973,
  struct _M0TWssbEu* _M0L12_2adiscard__S974,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S975
) {
  struct moonbit_result_0 _result_2263;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _result_2263.tag = 1;
  _result_2263.data.ok = 0;
  return _result_2263;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S976,
  moonbit_string_t _M0L12_2adiscard__S977,
  int32_t _M0L12_2adiscard__S978,
  struct _M0TWEu* _M0L12_2adiscard__S979,
  struct _M0TWssbEu* _M0L12_2adiscard__S980,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S981
) {
  struct moonbit_result_0 _result_2264;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _result_2264.tag = 1;
  _result_2264.data.ok = 0;
  return _result_2264;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S982,
  moonbit_string_t _M0L12_2adiscard__S983,
  int32_t _M0L12_2adiscard__S984,
  struct _M0TWEu* _M0L12_2adiscard__S985,
  struct _M0TWssbEu* _M0L12_2adiscard__S986,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S987
) {
  struct moonbit_result_0 _result_2265;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _result_2265.tag = 1;
  _result_2265.data.ok = 0;
  return _result_2265;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S988,
  moonbit_string_t _M0L12_2adiscard__S989,
  int32_t _M0L12_2adiscard__S990,
  struct _M0TWEu* _M0L12_2adiscard__S991,
  struct _M0TWssbEu* _M0L12_2adiscard__S992,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S993
) {
  struct moonbit_result_0 _result_2266;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _result_2266.tag = 1;
  _result_2266.data.ok = 0;
  return _result_2266;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S994,
  moonbit_string_t _M0L12_2adiscard__S995,
  int32_t _M0L12_2adiscard__S996,
  struct _M0TWEu* _M0L12_2adiscard__S997,
  struct _M0TWssbEu* _M0L12_2adiscard__S998,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S999
) {
  struct moonbit_result_0 _result_2267;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _result_2267.tag = 1;
  _result_2267.data.ok = 0;
  return _result_2267;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S969
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter6custom(
  float _M0L2tmS935,
  float _M0L2vtS936,
  float _M0L2vrS937,
  float _M0L2elS938,
  float _M0L1rS939
) {
  float _M0L1cS933;
  float _M0L2glS934;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_2268;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS933 = -0x1p+0f;
  _M0L2glS934 = -0x1p+0f;
  _block_2268
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_2268)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2268->$0 = _M0L1cS933;
  _block_2268->$1 = _M0L2glS934;
  _block_2268->$2 = _M0L2tmS935;
  _block_2268->$3 = _M0L2vtS936;
  _block_2268->$4 = _M0L2vrS937;
  _block_2268->$5 = _M0L2elS938;
  _block_2268->$6 = _M0L1rS939;
  _block_2268->$7 = 0x1p+1f;
  _block_2268->$8 = 0x0p+0f;
  _block_2268->$9 = 0x0p+0f;
  _block_2268->$10 = 0x0p+0f;
  return _block_2268;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS907,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS909,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS912
) {
  struct _M0TPB5ArrayGfE* _M0L1vS906;
  float _M0L2vtS2161;
  float _M0L2vrS2162;
  float _M0L6spreadS908;
  int32_t _M0L7_2abindS910;
  int32_t _M0L1kS911;
  struct _M0TPB5ArrayGfE* _M0L1wS914;
  struct _M0TPB5ArrayGbE* _M0L4fireS915;
  struct _M0TPB5ArrayGiE* _M0L4tabsS916;
  struct _M0TPB5ArrayGfE* _M0L1iS917;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS918;
  struct _M0TPB5ArrayGfE* _M0L2geS919;
  struct _M0TPB5ArrayGfE* _M0L2giS920;
  struct _M0TPB5ArrayGfE* _M0L2heS921;
  struct _M0TPB5ArrayGfE* _M0L2hiS922;
  struct _M0TPB5ArrayGfE* _M0L3gluS923;
  struct _M0TPB5ArrayGfE* _M0L4gabaS924;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS925;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS926;
  float _M0L4e__eS927;
  float _M0L4e__iS928;
  float _M0L3treS929;
  float _M0L3tdeS930;
  float _M0L3triS931;
  float _M0L3tdiS932;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS2160;
  struct _M0TP26RiantR8snn__mbt2IF* _block_2270;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS906 = _M0MPC15array5Array4makeGfE(_M0L1nS907, 0x0p+0f);
  _M0L2vtS2161 = _M0L5paramS909->$3;
  _M0L2vrS2162 = _M0L5paramS909->$4;
  _M0L6spreadS908 = _M0L2vtS2161 - _M0L2vrS2162;
  _M0L7_2abindS910 = 0;
  _M0L1kS911 = _M0L7_2abindS910;
  while (1) {
    if (_M0L1kS911 < _M0L1nS907) {
      float _M0L2vrS2156 = _M0L5paramS909->$4;
      float _M0L6_2atmpS2158;
      float _M0L6_2atmpS2157;
      float _M0L6_2atmpS2155;
      int32_t _M0L6_2atmpS2159;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2158 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS912);
      _M0L6_2atmpS2157 = _M0L6_2atmpS2158 * _M0L6spreadS908;
      _M0L6_2atmpS2155 = _M0L2vrS2156 + _M0L6_2atmpS2157;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS906, _M0L1kS911, _M0L6_2atmpS2155);
      _M0L6_2atmpS2159 = _M0L1kS911 + 1;
      _M0L1kS911 = _M0L6_2atmpS2159;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS914 = _M0MPC15array5Array4makeGfE(_M0L1nS907, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS915 = _M0MPC15array5Array4makeGbE(_M0L1nS907, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS916 = _M0MPC15array5Array4makeGiE(_M0L1nS907, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS917 = _M0MPC15array5Array4makeGfE(_M0L1nS907, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS918 = _M0MPC15array5Array4makeGfE(_M0L1nS907, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS919 = _M0MPC15array5Array4makeGfE(_M0L1nS907, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS920 = _M0MPC15array5Array4makeGfE(_M0L1nS907, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS921 = _M0MPC15array5Array4makeGfE(_M0L1nS907, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS922 = _M0MPC15array5Array4makeGfE(_M0L1nS907, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS923 = _M0MPC15array5Array4makeGfE(_M0L1nS907, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS924 = _M0MPC15array5Array4makeGfE(_M0L1nS907, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS925 = _M0MPC15array5Array4makeGfE(_M0L1nS907, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS926 = _M0MPC15array5Array4makeGfE(_M0L1nS907, 0x1p+0f);
  _M0L4e__eS927 = 0x0p+0f;
  _M0L4e__iS928 = -0x1.2cp+6f;
  _M0L3treS929 = 0x1p+0f;
  _M0L3tdeS930 = 0x1.8p+2f;
  _M0L3triS931 = 0x1p-1f;
  _M0L3tdiS932 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS2160 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS909);
  _block_2270
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_2270)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2270->$0 = _M0L5paramS909;
  _block_2270->$1 = _M0L6_2atmpS2160;
  _block_2270->$2 = _M0L1nS907;
  _block_2270->$3 = _M0L1vS906;
  _block_2270->$4 = _M0L1wS914;
  _block_2270->$5 = _M0L4fireS915;
  _block_2270->$6 = _M0L4tabsS916;
  _block_2270->$7 = _M0L1iS917;
  _block_2270->$8 = _M0L9syn__currS918;
  _block_2270->$9 = _M0L2geS919;
  _block_2270->$10 = _M0L2giS920;
  _block_2270->$11 = _M0L2heS921;
  _block_2270->$12 = _M0L2hiS922;
  _block_2270->$13 = _M0L3gluS923;
  _block_2270->$14 = _M0L4gabaS924;
  _block_2270->$15 = _M0L7gsyn__eS925;
  _block_2270->$16 = _M0L7gsyn__iS926;
  _block_2270->$17 = _M0L4e__eS927;
  _block_2270->$18 = _M0L4e__iS928;
  _block_2270->$19 = _M0L3treS929;
  _block_2270->$20 = _M0L3tdeS930;
  _block_2270->$21 = _M0L3triS931;
  _block_2270->$22 = _M0L3tdiS932;
  return _block_2270;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_2271;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_2271
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_2271)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2271->$0 = 0x1p+1f;
  return _block_2271;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS902
) {
  int32_t _M0L1nS901;
  int32_t _M0L7_2abindS903;
  int32_t _M0L1iS904;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS901 = _M0L1pS902->$2;
  _M0L7_2abindS903 = 0;
  _M0L1iS904 = _M0L7_2abindS903;
  while (1) {
    if (_M0L1iS904 < _M0L1nS901) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2132 = _M0L1pS902->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS2153 = _M0L1pS902->$9;
      float _M0L6_2atmpS2148;
      struct _M0TPB5ArrayGfE* _M0L1vS2152;
      float _M0L6_2atmpS2150;
      float _M0L4e__eS2151;
      float _M0L6_2atmpS2149;
      float _M0L6_2atmpS2145;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS2147;
      float _M0L6_2atmpS2146;
      float _M0L6_2atmpS2134;
      struct _M0TPB5ArrayGfE* _M0L2giS2144;
      float _M0L6_2atmpS2139;
      struct _M0TPB5ArrayGfE* _M0L1vS2143;
      float _M0L6_2atmpS2141;
      float _M0L4e__iS2142;
      float _M0L6_2atmpS2140;
      float _M0L6_2atmpS2136;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS2138;
      float _M0L6_2atmpS2137;
      float _M0L6_2atmpS2135;
      float _M0L6_2atmpS2133;
      int32_t _M0L6_2atmpS2154;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2148 = _M0MPC15array5Array2atGfE(_M0L2geS2153, _M0L1iS904);
      _M0L1vS2152 = _M0L1pS902->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2150 = _M0MPC15array5Array2atGfE(_M0L1vS2152, _M0L1iS904);
      _M0L4e__eS2151 = _M0L1pS902->$17;
      _M0L6_2atmpS2149 = _M0L6_2atmpS2150 - _M0L4e__eS2151;
      _M0L6_2atmpS2145 = _M0L6_2atmpS2148 * _M0L6_2atmpS2149;
      _M0L7gsyn__eS2147 = _M0L1pS902->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2146
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS2147, _M0L1iS904);
      _M0L6_2atmpS2134 = _M0L6_2atmpS2145 * _M0L6_2atmpS2146;
      _M0L2giS2144 = _M0L1pS902->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2139 = _M0MPC15array5Array2atGfE(_M0L2giS2144, _M0L1iS904);
      _M0L1vS2143 = _M0L1pS902->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2141 = _M0MPC15array5Array2atGfE(_M0L1vS2143, _M0L1iS904);
      _M0L4e__iS2142 = _M0L1pS902->$18;
      _M0L6_2atmpS2140 = _M0L6_2atmpS2141 - _M0L4e__iS2142;
      _M0L6_2atmpS2136 = _M0L6_2atmpS2139 * _M0L6_2atmpS2140;
      _M0L7gsyn__iS2138 = _M0L1pS902->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2137
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS2138, _M0L1iS904);
      _M0L6_2atmpS2135 = _M0L6_2atmpS2136 * _M0L6_2atmpS2137;
      _M0L6_2atmpS2133 = _M0L6_2atmpS2134 + _M0L6_2atmpS2135;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS2132, _M0L1iS904, _M0L6_2atmpS2133);
      _M0L6_2atmpS2154 = _M0L1iS904 + 1;
      _M0L1iS904 = _M0L6_2atmpS2154;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS893,
  float _M0L2dtS896
) {
  int32_t _M0L1nS892;
  int32_t _M0L7_2abindS894;
  int32_t _M0L1iS895;
  int32_t _M0L7_2abindS898;
  int32_t _M0L1iS899;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS892 = _M0L1pS893->$2;
  _M0L7_2abindS894 = 0;
  _M0L1iS895 = _M0L7_2abindS894;
  while (1) {
    if (_M0L1iS895 < _M0L1nS892) {
      struct _M0TPB5ArrayGfE* _M0L2heS2070 = _M0L1pS893->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS2075 = _M0L1pS893->$11;
      float _M0L6_2atmpS2072;
      struct _M0TPB5ArrayGfE* _M0L3gluS2074;
      float _M0L6_2atmpS2073;
      float _M0L6_2atmpS2071;
      struct _M0TPB5ArrayGfE* _M0L2hiS2076;
      struct _M0TPB5ArrayGfE* _M0L2hiS2081;
      float _M0L6_2atmpS2078;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2080;
      float _M0L6_2atmpS2079;
      float _M0L6_2atmpS2077;
      struct _M0TPB5ArrayGfE* _M0L2geS2082;
      struct _M0TPB5ArrayGfE* _M0L2geS2094;
      float _M0L6_2atmpS2084;
      struct _M0TPB5ArrayGfE* _M0L2geS2093;
      float _M0L6_2atmpS2092;
      float _M0L6_2atmpS2090;
      float _M0L3tdeS2091;
      float _M0L6_2atmpS2087;
      struct _M0TPB5ArrayGfE* _M0L2heS2089;
      float _M0L6_2atmpS2088;
      float _M0L6_2atmpS2086;
      float _M0L6_2atmpS2085;
      float _M0L6_2atmpS2083;
      struct _M0TPB5ArrayGfE* _M0L2heS2095;
      struct _M0TPB5ArrayGfE* _M0L2heS2104;
      float _M0L6_2atmpS2097;
      struct _M0TPB5ArrayGfE* _M0L2heS2103;
      float _M0L6_2atmpS2102;
      float _M0L6_2atmpS2100;
      float _M0L3treS2101;
      float _M0L6_2atmpS2099;
      float _M0L6_2atmpS2098;
      float _M0L6_2atmpS2096;
      struct _M0TPB5ArrayGfE* _M0L2giS2105;
      struct _M0TPB5ArrayGfE* _M0L2giS2117;
      float _M0L6_2atmpS2107;
      struct _M0TPB5ArrayGfE* _M0L2giS2116;
      float _M0L6_2atmpS2115;
      float _M0L6_2atmpS2113;
      float _M0L3tdiS2114;
      float _M0L6_2atmpS2110;
      struct _M0TPB5ArrayGfE* _M0L2hiS2112;
      float _M0L6_2atmpS2111;
      float _M0L6_2atmpS2109;
      float _M0L6_2atmpS2108;
      float _M0L6_2atmpS2106;
      struct _M0TPB5ArrayGfE* _M0L2hiS2118;
      struct _M0TPB5ArrayGfE* _M0L2hiS2127;
      float _M0L6_2atmpS2120;
      struct _M0TPB5ArrayGfE* _M0L2hiS2126;
      float _M0L6_2atmpS2125;
      float _M0L6_2atmpS2123;
      float _M0L3triS2124;
      float _M0L6_2atmpS2122;
      float _M0L6_2atmpS2121;
      float _M0L6_2atmpS2119;
      int32_t _M0L6_2atmpS2128;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2072 = _M0MPC15array5Array2atGfE(_M0L2heS2075, _M0L1iS895);
      _M0L3gluS2074 = _M0L1pS893->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2073 = _M0MPC15array5Array2atGfE(_M0L3gluS2074, _M0L1iS895);
      _M0L6_2atmpS2071 = _M0L6_2atmpS2072 + _M0L6_2atmpS2073;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS2070, _M0L1iS895, _M0L6_2atmpS2071);
      _M0L2hiS2076 = _M0L1pS893->$12;
      _M0L2hiS2081 = _M0L1pS893->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2078 = _M0MPC15array5Array2atGfE(_M0L2hiS2081, _M0L1iS895);
      _M0L4gabaS2080 = _M0L1pS893->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2079
      = _M0MPC15array5Array2atGfE(_M0L4gabaS2080, _M0L1iS895);
      _M0L6_2atmpS2077 = _M0L6_2atmpS2078 + _M0L6_2atmpS2079;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2076, _M0L1iS895, _M0L6_2atmpS2077);
      _M0L2geS2082 = _M0L1pS893->$9;
      _M0L2geS2094 = _M0L1pS893->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2084 = _M0MPC15array5Array2atGfE(_M0L2geS2094, _M0L1iS895);
      _M0L2geS2093 = _M0L1pS893->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2092 = _M0MPC15array5Array2atGfE(_M0L2geS2093, _M0L1iS895);
      _M0L6_2atmpS2090 = -_M0L6_2atmpS2092;
      _M0L3tdeS2091 = _M0L1pS893->$20;
      _M0L6_2atmpS2087 = _M0L6_2atmpS2090 / _M0L3tdeS2091;
      _M0L2heS2089 = _M0L1pS893->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2088 = _M0MPC15array5Array2atGfE(_M0L2heS2089, _M0L1iS895);
      _M0L6_2atmpS2086 = _M0L6_2atmpS2087 + _M0L6_2atmpS2088;
      _M0L6_2atmpS2085 = _M0L2dtS896 * _M0L6_2atmpS2086;
      _M0L6_2atmpS2083 = _M0L6_2atmpS2084 + _M0L6_2atmpS2085;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS2082, _M0L1iS895, _M0L6_2atmpS2083);
      _M0L2heS2095 = _M0L1pS893->$11;
      _M0L2heS2104 = _M0L1pS893->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2097 = _M0MPC15array5Array2atGfE(_M0L2heS2104, _M0L1iS895);
      _M0L2heS2103 = _M0L1pS893->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2102 = _M0MPC15array5Array2atGfE(_M0L2heS2103, _M0L1iS895);
      _M0L6_2atmpS2100 = -_M0L6_2atmpS2102;
      _M0L3treS2101 = _M0L1pS893->$19;
      _M0L6_2atmpS2099 = _M0L6_2atmpS2100 / _M0L3treS2101;
      _M0L6_2atmpS2098 = _M0L2dtS896 * _M0L6_2atmpS2099;
      _M0L6_2atmpS2096 = _M0L6_2atmpS2097 + _M0L6_2atmpS2098;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS2095, _M0L1iS895, _M0L6_2atmpS2096);
      _M0L2giS2105 = _M0L1pS893->$10;
      _M0L2giS2117 = _M0L1pS893->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2107 = _M0MPC15array5Array2atGfE(_M0L2giS2117, _M0L1iS895);
      _M0L2giS2116 = _M0L1pS893->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2115 = _M0MPC15array5Array2atGfE(_M0L2giS2116, _M0L1iS895);
      _M0L6_2atmpS2113 = -_M0L6_2atmpS2115;
      _M0L3tdiS2114 = _M0L1pS893->$22;
      _M0L6_2atmpS2110 = _M0L6_2atmpS2113 / _M0L3tdiS2114;
      _M0L2hiS2112 = _M0L1pS893->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2111 = _M0MPC15array5Array2atGfE(_M0L2hiS2112, _M0L1iS895);
      _M0L6_2atmpS2109 = _M0L6_2atmpS2110 + _M0L6_2atmpS2111;
      _M0L6_2atmpS2108 = _M0L2dtS896 * _M0L6_2atmpS2109;
      _M0L6_2atmpS2106 = _M0L6_2atmpS2107 + _M0L6_2atmpS2108;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS2105, _M0L1iS895, _M0L6_2atmpS2106);
      _M0L2hiS2118 = _M0L1pS893->$12;
      _M0L2hiS2127 = _M0L1pS893->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2120 = _M0MPC15array5Array2atGfE(_M0L2hiS2127, _M0L1iS895);
      _M0L2hiS2126 = _M0L1pS893->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2125 = _M0MPC15array5Array2atGfE(_M0L2hiS2126, _M0L1iS895);
      _M0L6_2atmpS2123 = -_M0L6_2atmpS2125;
      _M0L3triS2124 = _M0L1pS893->$21;
      _M0L6_2atmpS2122 = _M0L6_2atmpS2123 / _M0L3triS2124;
      _M0L6_2atmpS2121 = _M0L2dtS896 * _M0L6_2atmpS2122;
      _M0L6_2atmpS2119 = _M0L6_2atmpS2120 + _M0L6_2atmpS2121;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2118, _M0L1iS895, _M0L6_2atmpS2119);
      _M0L6_2atmpS2128 = _M0L1iS895 + 1;
      _M0L1iS895 = _M0L6_2atmpS2128;
      continue;
    }
    break;
  }
  _M0L7_2abindS898 = 0;
  _M0L1iS899 = _M0L7_2abindS898;
  while (1) {
    if (_M0L1iS899 < _M0L1nS892) {
      struct _M0TPB5ArrayGfE* _M0L3gluS2129 = _M0L1pS893->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2130;
      int32_t _M0L6_2atmpS2131;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS2129, _M0L1iS899, 0x0p+0f);
      _M0L4gabaS2130 = _M0L1pS893->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS2130, _M0L1iS899, 0x0p+0f);
      _M0L6_2atmpS2131 = _M0L1iS899 + 1;
      _M0L1iS899 = _M0L6_2atmpS2131;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS878,
  float _M0L2dtS887
) {
  int32_t _M0L1nS877;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S879;
  float _M0L2tmS880;
  float _M0L2elS881;
  float _M0L1rS882;
  float _M0L2vtS883;
  float _M0L2vrS884;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS2069;
  float _M0L11tabs__constS885;
  float _M0L6_2atmpS2068;
  int32_t _M0L11tabs__stepsS886;
  int32_t _M0L7_2abindS888;
  int32_t _M0L1iS889;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS877 = _M0L1pS878->$2;
  _M0L3p__S879 = _M0L1pS878->$0;
  _M0L2tmS880 = _M0L3p__S879->$2;
  _M0L2elS881 = _M0L3p__S879->$5;
  _M0L1rS882 = _M0L3p__S879->$6;
  _M0L2vtS883 = _M0L3p__S879->$3;
  _M0L2vrS884 = _M0L3p__S879->$4;
  _M0L5spikeS2069 = _M0L1pS878->$1;
  _M0L11tabs__constS885 = _M0L5spikeS2069->$0;
  _M0L6_2atmpS2068 = _M0L11tabs__constS885 / _M0L2dtS887;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS886 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2068);
  _M0L7_2abindS888 = 0;
  _M0L1iS889 = _M0L7_2abindS888;
  while (1) {
    if (_M0L1iS889 < _M0L1nS877) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS2028 = _M0L1pS878->$6;
      int32_t _M0L6_2atmpS2027;
      struct _M0TPB5ArrayGfE* _M0L1vS2034;
      struct _M0TPB5ArrayGfE* _M0L1vS2055;
      float _M0L6_2atmpS2036;
      float _M0L6_2atmpS2038;
      struct _M0TPB5ArrayGfE* _M0L1vS2054;
      float _M0L6_2atmpS2053;
      float _M0L6_2atmpS2052;
      float _M0L6_2atmpS2044;
      struct _M0TPB5ArrayGfE* _M0L1wS2051;
      float _M0L6_2atmpS2050;
      float _M0L6_2atmpS2047;
      struct _M0TPB5ArrayGfE* _M0L1iS2049;
      float _M0L6_2atmpS2048;
      float _M0L6_2atmpS2046;
      float _M0L6_2atmpS2045;
      float _M0L6_2atmpS2040;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2043;
      float _M0L6_2atmpS2042;
      float _M0L6_2atmpS2041;
      float _M0L6_2atmpS2039;
      float _M0L6_2atmpS2037;
      float _M0L6_2atmpS2035;
      struct _M0TPB5ArrayGbE* _M0L4fireS2056;
      struct _M0TPB5ArrayGfE* _M0L1vS2059;
      float _M0L6_2atmpS2058;
      int32_t _M0L6_2atmpS2057;
      struct _M0TPB5ArrayGfE* _M0L1vS2060;
      struct _M0TPB5ArrayGbE* _M0L4fireS2062;
      float _M0L6_2atmpS2061;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2064;
      struct _M0TPB5ArrayGbE* _M0L4fireS2066;
      int32_t _M0L6_2atmpS2065;
      int32_t _M0L6_2atmpS2026;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2027
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2028, _M0L1iS889);
      if (_M0L6_2atmpS2027 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS2029 = _M0L1pS878->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2030;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2033;
        int32_t _M0L6_2atmpS2032;
        int32_t _M0L6_2atmpS2031;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS2029, _M0L1iS889, 0);
        _M0L4tabsS2030 = _M0L1pS878->$6;
        _M0L4tabsS2033 = _M0L1pS878->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2032
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2033, _M0L1iS889);
        _M0L6_2atmpS2031 = _M0L6_2atmpS2032 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS2030, _M0L1iS889, _M0L6_2atmpS2031);
        goto join_890;
      }
      _M0L1vS2034 = _M0L1pS878->$3;
      _M0L1vS2055 = _M0L1pS878->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2036 = _M0MPC15array5Array2atGfE(_M0L1vS2055, _M0L1iS889);
      _M0L6_2atmpS2038 = _M0L2dtS887 / _M0L2tmS880;
      _M0L1vS2054 = _M0L1pS878->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2053 = _M0MPC15array5Array2atGfE(_M0L1vS2054, _M0L1iS889);
      _M0L6_2atmpS2052 = _M0L6_2atmpS2053 - _M0L2elS881;
      _M0L6_2atmpS2044 = -_M0L6_2atmpS2052;
      _M0L1wS2051 = _M0L1pS878->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2050 = _M0MPC15array5Array2atGfE(_M0L1wS2051, _M0L1iS889);
      _M0L6_2atmpS2047 = -_M0L6_2atmpS2050;
      _M0L1iS2049 = _M0L1pS878->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2048 = _M0MPC15array5Array2atGfE(_M0L1iS2049, _M0L1iS889);
      _M0L6_2atmpS2046 = _M0L6_2atmpS2047 + _M0L6_2atmpS2048;
      _M0L6_2atmpS2045 = _M0L1rS882 * _M0L6_2atmpS2046;
      _M0L6_2atmpS2040 = _M0L6_2atmpS2044 + _M0L6_2atmpS2045;
      _M0L9syn__currS2043 = _M0L1pS878->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2042
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS2043, _M0L1iS889);
      _M0L6_2atmpS2041 = _M0L1rS882 * _M0L6_2atmpS2042;
      _M0L6_2atmpS2039 = _M0L6_2atmpS2040 - _M0L6_2atmpS2041;
      _M0L6_2atmpS2037 = _M0L6_2atmpS2038 * _M0L6_2atmpS2039;
      _M0L6_2atmpS2035 = _M0L6_2atmpS2036 + _M0L6_2atmpS2037;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2034, _M0L1iS889, _M0L6_2atmpS2035);
      _M0L4fireS2056 = _M0L1pS878->$5;
      _M0L1vS2059 = _M0L1pS878->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2058 = _M0MPC15array5Array2atGfE(_M0L1vS2059, _M0L1iS889);
      _M0L6_2atmpS2057 = _M0L6_2atmpS2058 > _M0L2vtS883;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2056, _M0L1iS889, _M0L6_2atmpS2057);
      _M0L1vS2060 = _M0L1pS878->$3;
      _M0L4fireS2062 = _M0L1pS878->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2062, _M0L1iS889)) {
        _M0L6_2atmpS2061 = _M0L2vrS884;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS2063 = _M0L1pS878->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2061 = _M0MPC15array5Array2atGfE(_M0L1vS2063, _M0L1iS889);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2060, _M0L1iS889, _M0L6_2atmpS2061);
      _M0L4tabsS2064 = _M0L1pS878->$6;
      _M0L4fireS2066 = _M0L1pS878->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2066, _M0L1iS889)) {
        _M0L6_2atmpS2065 = _M0L11tabs__stepsS886;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS2067 = _M0L1pS878->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2065
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2067, _M0L1iS889);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2064, _M0L1iS889, _M0L6_2atmpS2065);
      goto join_890;
      goto joinlet_2276;
      join_890:;
      _M0L6_2atmpS2026 = _M0L1iS889 + 1;
      _M0L1iS889 = _M0L6_2atmpS2026;
      continue;
      joinlet_2276:;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS875
) {
  struct _M0TUmmmmE* _M0L1sS874;
  uint64_t _M0L6_2atmpS2025;
  struct _M0TUmmmmE* _M0L1tS876;
  uint64_t _M0L6_2atmpS2021;
  uint64_t _M0L6_2atmpS2022;
  uint64_t _M0L6_2atmpS2023;
  uint64_t _M0L6_2atmpS2024;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2277;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS874 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS875);
  _M0L6_2atmpS2025 = _M0L1sS874->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS876 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2025);
  _M0L6_2atmpS2021 = _M0L1sS874->$0;
  _M0L6_2atmpS2022 = _M0L1sS874->$1;
  _M0L6_2atmpS2023 = _M0L1sS874->$2;
  moonbit_decref_cycle_free(_M0L1sS874);
  _M0L6_2atmpS2024 = _M0L1tS876->$0;
  moonbit_decref_cycle_free(_M0L1tS876);
  _block_2277
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2277)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2277->$0 = _M0L6_2atmpS2021;
  _block_2277->$1 = _M0L6_2atmpS2022;
  _block_2277->$2 = _M0L6_2atmpS2023;
  _block_2277->$3 = _M0L6_2atmpS2024;
  return _block_2277;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS866) {
  uint64_t _M0L2s1S865;
  uint64_t _M0L2z1S867;
  uint64_t _M0L2s2S868;
  uint64_t _M0L2z2S869;
  uint64_t _M0L2s3S870;
  uint64_t _M0L2z3S871;
  uint64_t _M0L2s4S872;
  uint64_t _M0L2z4S873;
  struct _M0TUmmmmE* _block_2278;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S865 = _M0L4seedS866 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S867 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S865);
  _M0L2s2S868 = _M0L2s1S865 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S869 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S868);
  _M0L2s3S870 = _M0L2s2S868 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S871 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S870);
  _M0L2s4S872 = _M0L2s3S870 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S873 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S872);
  _block_2278 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2278)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2278->$0 = _M0L2z1S867;
  _block_2278->$1 = _M0L2z2S869;
  _block_2278->$2 = _M0L2z3S871;
  _block_2278->$3 = _M0L2z4S873;
  return _block_2278;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS863) {
  uint64_t _M0L6_2atmpS2020;
  uint64_t _M0L6_2atmpS2019;
  uint64_t _M0L1zS862;
  uint64_t _M0L6_2atmpS2018;
  uint64_t _M0L6_2atmpS2017;
  uint64_t _M0L1zS864;
  uint64_t _M0L6_2atmpS2016;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2020 = _M0L1zS863 >> 30;
  _M0L6_2atmpS2019 = _M0L1zS863 ^ _M0L6_2atmpS2020;
  _M0L1zS862 = _M0L6_2atmpS2019 * 13787848793156543929ull;
  _M0L6_2atmpS2018 = _M0L1zS862 >> 27;
  _M0L6_2atmpS2017 = _M0L1zS862 ^ _M0L6_2atmpS2018;
  _M0L1zS864 = _M0L6_2atmpS2017 * 10723151780598845931ull;
  _M0L6_2atmpS2016 = _M0L1zS864 >> 31;
  return _M0L1zS864 ^ _M0L6_2atmpS2016;
}

int32_t _M0FP26RiantR8snn__mbt16stimulate__layer(
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L1sS845,
  float _M0L4timeS843,
  float _M0L2dtS848
) {
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS2015;
  int32_t _M0L6n__preS844;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2014;
  int32_t _M0L7n__postS846;
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS2013;
  float _M0L4rateS2012;
  float _M0L6lambdaS847;
  int32_t _M0L7_2abindS849;
  int32_t _M0L1iS850;
  moonbit_string_t _M0L3symS2009;
  struct _M0TPB5ArrayGfE* _M0L9g__targetS852;
  int32_t _M0L7_2abindS853;
  int32_t _M0L1iS854;
  #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  _M0L5paramS2015 = _M0L1sS845->$0;
  _M0L6n__preS844 = _M0L5paramS2015->$1;
  _M0L4postS2014 = _M0L1sS845->$1;
  _M0L7n__postS846 = _M0L4postS2014->$2;
  _M0L5paramS2013 = _M0L1sS845->$0;
  _M0L4rateS2012 = _M0L5paramS2013->$0;
  _M0L6lambdaS847 = _M0L4rateS2012 * _M0L2dtS848;
  _M0L7_2abindS849 = 0;
  _M0L1iS850 = _M0L7_2abindS849;
  while (1) {
    if (_M0L1iS850 < _M0L6n__preS844) {
      struct _M0TPB5ArrayGbE* _M0L4fireS1994 = _M0L1sS845->$3;
      int32_t _M0L6_2atmpS1995;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1994, _M0L1iS850, 0);
      _M0L6_2atmpS1995 = _M0L1iS850 + 1;
      _M0L1iS850 = _M0L6_2atmpS1995;
      continue;
    }
    break;
  }
  if (_M0L6lambdaS847 <= 0x0p+0f) {
    return 0;
  }
  _M0L3symS2009 = _M0L1sS845->$2;
  #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  if (
    _M0L3symS2009 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS2009)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS2009, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS2009) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2010 = _M0L1sS845->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2217 = _M0L4postS2010->$13;
    moonbit_incref_cycle_free(_M0L8_2afieldS2217);
    _M0L9g__targetS852 = _M0L8_2afieldS2217;
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2011 = _M0L1sS845->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS2218 = _M0L4postS2011->$14;
    moonbit_incref_cycle_free(_M0L8_2afieldS2218);
    _M0L9g__targetS852 = _M0L8_2afieldS2218;
  }
  _M0L7_2abindS853 = 0;
  _M0L1iS854 = _M0L7_2abindS853;
  while (1) {
    if (_M0L1iS854 < _M0L6n__preS844) {
      struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS1999 =
        _M0L1sS845->$0;
      struct _M0TPB5ArrayGbE* _M0L6activeS1998 = _M0L5paramS1999->$2;
      int32_t _M0L6_2atmpS1997;
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS2008;
      int32_t _M0L1kS857;
      int32_t _M0L6_2atmpS1996;
      moonbit_incref_cycle_free(_M0L6activeS1998);
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0L6_2atmpS1997
      = _M0MPC15array5Array2atGbE(_M0L6activeS1998, _M0L1iS854);
      moonbit_decref_cycle_free(_M0L6activeS1998);
      if (!_M0L6_2atmpS1997) {
        goto join_855;
      }
      _M0L3rngS2008 = _M0L1sS845->$6;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0L1kS857
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS2008, _M0L6lambdaS847);
      if (_M0L1kS857 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS2000 = _M0L1sS845->$3;
        int32_t _M0L7_2abindS858;
        int32_t _M0L1jS859;
        #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS2000, _M0L1iS854, 1);
        _M0L7_2abindS858 = 0;
        _M0L1jS859 = _M0L7_2abindS858;
        while (1) {
          if (_M0L1jS859 < _M0L7n__postS846) {
            int32_t _M0L6_2atmpS2006 = _M0L1jS859 * _M0L6n__preS844;
            int32_t _M0L3idxS860 = _M0L6_2atmpS2006 + _M0L1iS854;
            struct _M0TPB5ArrayGbE* _M0L12connectivityS2001 = _M0L1sS845->$5;
            int32_t _M0L6_2atmpS2007;
            #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
            if (
              _M0MPC15array5Array2atGbE(_M0L12connectivityS2001, _M0L3idxS860)
            ) {
              float _M0L6_2atmpS2003;
              struct _M0TPB5ArrayGfE* _M0L7weightsS2005;
              float _M0L6_2atmpS2004;
              float _M0L6_2atmpS2002;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0L6_2atmpS2003
              = _M0MPC15array5Array2atGfE(_M0L9g__targetS852, _M0L1jS859);
              _M0L7weightsS2005 = _M0L1sS845->$4;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0L6_2atmpS2004
              = _M0MPC15array5Array2atGfE(_M0L7weightsS2005, _M0L3idxS860);
              _M0L6_2atmpS2002 = _M0L6_2atmpS2003 + _M0L6_2atmpS2004;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0MPC15array5Array3setGfE(_M0L9g__targetS852, _M0L1jS859, _M0L6_2atmpS2002);
            }
            _M0L6_2atmpS2007 = _M0L1jS859 + 1;
            _M0L1jS859 = _M0L6_2atmpS2007;
            continue;
          }
          break;
        }
      }
      goto join_855;
      goto joinlet_2281;
      join_855:;
      _M0L6_2atmpS1996 = _M0L1iS854 + 1;
      _M0L1iS854 = _M0L6_2atmpS1996;
      continue;
      joinlet_2281:;
    } else {
      moonbit_decref_cycle_free(_M0L9g__targetS852);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0MP26RiantR8snn__mbt20PoissonLayerStimulus3new(
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS820,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS822,
  moonbit_string_t _M0L3symS842,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS835
) {
  int32_t _M0L6n__preS819;
  int32_t _M0L7n__postS821;
  struct _M0TPB5ArrayGbE* _M0L4fireS823;
  int32_t _M0L6_2atmpS1993;
  struct _M0TPB5ArrayGfE* _M0L7weightsS824;
  int32_t _M0L6_2atmpS1992;
  struct _M0TPB5ArrayGbE* _M0L12connectivityS825;
  float _M0L7p__connS826;
  float _M0L2muS827;
  float _M0L5sigmaS828;
  moonbit_string_t _M0L4distS1991;
  int32_t _M0L11use__normalS829;
  int32_t _M0L7_2abindS830;
  int32_t _M0L1iS831;
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _block_2286;
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  _M0L6n__preS819 = _M0L5paramS820->$1;
  _M0L7n__postS821 = _M0L4postS822->$2;
  #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  _M0L4fireS823 = _M0MPC15array5Array4makeGbE(_M0L6n__preS819, 0);
  _M0L6_2atmpS1993 = _M0L7n__postS821 * _M0L6n__preS819;
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  _M0L7weightsS824 = _M0MPC15array5Array4makeGfE(_M0L6_2atmpS1993, 0x0p+0f);
  _M0L6_2atmpS1992 = _M0L7n__postS821 * _M0L6n__preS819;
  #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  _M0L12connectivityS825 = _M0MPC15array5Array4makeGbE(_M0L6_2atmpS1992, 0);
  _M0L7p__connS826 = _M0L5paramS820->$5;
  _M0L2muS827 = _M0L5paramS820->$3;
  _M0L5sigmaS828 = _M0L5paramS820->$4;
  _M0L4distS1991 = _M0L5paramS820->$6;
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  _M0L11use__normalS829
  = _M0L4distS1991 == (moonbit_string_t)moonbit_string_literal_10.data
    || Moonbit_array_length(_M0L4distS1991)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_10.data)
       && 0
          == memcmp(_M0L4distS1991, (moonbit_string_t)moonbit_string_literal_10.data, Moonbit_array_length(_M0L4distS1991) * 2);
  _M0L7_2abindS830 = 0;
  _M0L1iS831 = _M0L7_2abindS830;
  while (1) {
    if (_M0L1iS831 < _M0L6n__preS819) {
      int32_t _M0L7_2abindS832 = 0;
      int32_t _M0L1jS833 = _M0L7_2abindS832;
      int32_t _M0L6_2atmpS1990;
      while (1) {
        if (_M0L1jS833 < _M0L7n__postS821) {
          int32_t _M0L6_2atmpS1988 = _M0L1jS833 * _M0L6n__preS819;
          int32_t _M0L3idxS834 = _M0L6_2atmpS1988 + _M0L1iS831;
          float _M0L6_2atmpS1984;
          int32_t _M0L6_2atmpS1989;
          #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
          _M0L6_2atmpS1984 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS835);
          if (_M0L6_2atmpS1984 < _M0L7p__connS826) {
            float _M0L6_2atmpS1985;
            #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
            _M0MPC15array5Array3setGbE(_M0L12connectivityS825, _M0L3idxS834, 1);
            if (_M0L11use__normalS829) {
              double _M0L1zS837;
              struct _M0TUddE* _M0L7_2abindS838;
              double _M0L4_2azS839;
              float _M0L6_2atmpS1987;
              float _M0L6_2atmpS1986;
              #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0L7_2abindS838
              = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS835);
              _M0L4_2azS839 = _M0L7_2abindS838->$0;
              moonbit_decref_cycle_free(_M0L7_2abindS838);
              _M0L1zS837 = _M0L4_2azS839;
              goto join_836;
              goto joinlet_2285;
              join_836:;
              _M0L6_2atmpS1987 = (float)_M0L1zS837;
              _M0L6_2atmpS1986 = _M0L6_2atmpS1987 * _M0L5sigmaS828;
              _M0L6_2atmpS1985 = _M0L2muS827 + _M0L6_2atmpS1986;
              joinlet_2285:;
            } else {
              _M0L6_2atmpS1985 = _M0L2muS827;
            }
            #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
            _M0MPC15array5Array3setGfE(_M0L7weightsS824, _M0L3idxS834, _M0L6_2atmpS1985);
          }
          _M0L6_2atmpS1989 = _M0L1jS833 + 1;
          _M0L1jS833 = _M0L6_2atmpS1989;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1990 = _M0L1iS831 + 1;
      _M0L1iS831 = _M0L6_2atmpS1990;
      continue;
    }
    break;
  }
  moonbit_incref_cycle_free(_M0L5paramS820);
  moonbit_incref_cycle_free(_M0L4postS822);
  moonbit_incref_cycle_free(_M0L3symS842);
  moonbit_incref_cycle_free(_M0L3rngS835);
  _block_2286
  = (struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus));
  Moonbit_object_header(_block_2286)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _block_2286->$0 = _M0L5paramS820;
  _block_2286->$1 = _M0L4postS822;
  _block_2286->$2 = _M0L3symS842;
  _block_2286->$3 = _M0L4fireS823;
  _block_2286->$4 = _M0L7weightsS824;
  _block_2286->$5 = _M0L12connectivityS825;
  _block_2286->$6 = _M0L3rngS835;
  return _block_2286;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS814
) {
  double _M0L2u1S813;
  double _M0L8u1__safeS815;
  double _M0L2u2S816;
  double _M0L6_2atmpS1983;
  double _M0L6_2atmpS1982;
  double _M0L1rS817;
  double _M0L5thetaS818;
  double _M0L6_2atmpS1981;
  double _M0L6_2atmpS1978;
  double _M0L6_2atmpS1980;
  double _M0L6_2atmpS1979;
  struct _M0TUddE* _block_2287;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S813 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS814);
  if (_M0L2u1S813 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS815 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS815 = _M0L2u1S813;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S816 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS814);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1983 = _M0FPC14math2ln(_M0L8u1__safeS815);
  _M0L6_2atmpS1982 = -0x1p+1 * _M0L6_2atmpS1983;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS817 = sqrt(_M0L6_2atmpS1982);
  _M0L5thetaS818 = 0x1.921fb54442d18p+2 * _M0L2u2S816;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1981 = _M0FPC14math3cos(_M0L5thetaS818);
  _M0L6_2atmpS1978 = _M0L1rS817 * _M0L6_2atmpS1981;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1980 = _M0FPC14math3sin(_M0L5thetaS818);
  _M0L6_2atmpS1979 = _M0L1rS817 * _M0L6_2atmpS1980;
  _block_2287 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_2287)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2287->$0 = _M0L6_2atmpS1978;
  _block_2287->$1 = _M0L6_2atmpS1979;
  return _block_2287;
}

struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0MP26RiantR8snn__mbt12PoissonLayer10with__conn(
  float _M0L4rateS805,
  int32_t _M0L10n__sourcesS806,
  struct _M0TPB5ArrayGbE* _M0L6activeS807,
  float _M0L2muS808,
  float _M0L5sigmaS809,
  float _M0L1pS810,
  moonbit_string_t _M0L4distS811,
  moonbit_string_t _M0L4ruleS812
) {
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _block_2288;
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  moonbit_incref_cycle_free(_M0L6activeS807);
  moonbit_incref_cycle_free(_M0L4distS811);
  moonbit_incref_cycle_free(_M0L4ruleS812);
  _block_2288
  = (struct _M0TP26RiantR8snn__mbt12PoissonLayer*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt12PoissonLayer));
  Moonbit_object_header(_block_2288)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 45, 0);
  _block_2288->$0 = _M0L4rateS805;
  _block_2288->$1 = _M0L10n__sourcesS806;
  _block_2288->$2 = _M0L6activeS807;
  _block_2288->$3 = _M0L2muS808;
  _block_2288->$4 = _M0L5sigmaS809;
  _block_2288->$5 = _M0L1pS810;
  _block_2288->$6 = _M0L4distS811;
  _block_2288->$7 = _M0L4ruleS812;
  return _block_2288;
}

int32_t _M0FP26RiantR8snn__mbt15sample__poisson(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS803,
  float _M0L6lambdaS797
) {
  float _M0L6_2atmpS1977;
  float _M0L6_2atmpS1976;
  double _M0L1lS798;
  struct _M0TPB8MutLocalGdE* _M0L1pS799;
  struct _M0TPB8MutLocalGiE* _M0L1kS800;
  float _M0L6_2atmpS1975;
  int32_t _M0L8ten__lamS802;
  int32_t _M0L3capS801;
  int32_t _M0L3valS1974;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  if (_M0L6lambdaS797 <= 0x0p+0f) {
    return 0;
  }
  _M0L6_2atmpS1977 = -_M0L6lambdaS797;
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS1976 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS1977);
  _M0L1lS798 = (double)_M0L6_2atmpS1976;
  _M0L1pS799
  = (struct _M0TPB8MutLocalGdE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGdE));
  Moonbit_object_header(_M0L1pS799)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1pS799->$0 = 0x1p+0;
  _M0L1kS800
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS800)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS800->$0 = 0;
  _M0L6_2atmpS1975 = _M0L6lambdaS797 * 0x1.4p+3f;
  #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L8ten__lamS802 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1975);
  if (_M0L8ten__lamS802 > 100) {
    _M0L3capS801 = _M0L8ten__lamS802;
  } else {
    _M0L3capS801 = 100;
  }
  while (1) {
    int32_t _M0L3valS1966 = _M0L1kS800->$0;
    int32_t _M0L6_2atmpS1965 = _M0L3valS1966 + 1;
    double _M0L3valS1968;
    double _M0L6_2atmpS1969;
    double _M0L6_2atmpS1967;
    double _M0L3valS1970;
    int32_t _M0L3valS1972;
    _M0L1kS800->$0 = _M0L6_2atmpS1965;
    _M0L3valS1968 = _M0L1pS799->$0;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
    _M0L6_2atmpS1969 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS803);
    _M0L6_2atmpS1967 = _M0L3valS1968 * _M0L6_2atmpS1969;
    _M0L1pS799->$0 = _M0L6_2atmpS1967;
    _M0L3valS1970 = _M0L1pS799->$0;
    if (_M0L3valS1970 < _M0L1lS798) {
      int32_t _M0L3valS1971;
      moonbit_decref_cycle_free(_M0L1pS799);
      _M0L3valS1971 = _M0L1kS800->$0;
      moonbit_decref_cycle_free(_M0L1kS800);
      return _M0L3valS1971 - 1;
    }
    _M0L3valS1972 = _M0L1kS800->$0;
    if (_M0L3valS1972 > _M0L3capS801) {
      int32_t _M0L3valS1973;
      moonbit_decref_cycle_free(_M0L1pS799);
      _M0L3valS1973 = _M0L1kS800->$0;
      moonbit_decref_cycle_free(_M0L1kS800);
      return _M0L3valS1973 - 1;
    }
    continue;
    break;
  }
  _M0L3valS1974 = _M0L1kS800->$0;
  moonbit_decref_cycle_free(_M0L1kS800);
  return _M0L3valS1974 - 1;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS795
) {
  uint64_t _M0L1uS794;
  uint64_t _M0L4bitsS796;
  double _M0L6_2atmpS1964;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS794 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS795);
  _M0L4bitsS796 = _M0L1uS794 >> 11;
  _M0L6_2atmpS1964 = (double)_M0L4bitsS796;
  return _M0L6_2atmpS1964 * 0x1p-53;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS792
) {
  uint32_t _M0L1uS791;
  uint32_t _M0L4bitsS793;
  double _M0L6_2atmpS1963;
  double _M0L6_2atmpS1962;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS791 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS792);
  _M0L4bitsS793 = _M0L1uS791 >> 8;
  _M0L6_2atmpS1963 = (double)_M0L4bitsS793;
  _M0L6_2atmpS1962 = _M0L6_2atmpS1963 * 0x1p-24;
  return (float)_M0L6_2atmpS1962;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS790
) {
  uint64_t _M0L1uS789;
  uint64_t _M0L6_2atmpS1961;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS789 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS790);
  _M0L6_2atmpS1961 = _M0L1uS789 >> 32;
  return (uint32_t)_M0L6_2atmpS1961;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS782
) {
  uint64_t _M0L2s0S781;
  uint64_t _M0L2s1S783;
  uint64_t _M0L2s2S784;
  uint64_t _M0L2s3S785;
  uint64_t _M0L3tmpS786;
  uint64_t _M0L6_2atmpS1960;
  uint64_t _M0L3resS787;
  uint64_t _M0L1tS788;
  uint64_t _M0L6_2atmpS1950;
  uint64_t _M0L6_2atmpS1951;
  uint64_t _M0L2s2S1953;
  uint64_t _M0L6_2atmpS1952;
  uint64_t _M0L2s3S1955;
  uint64_t _M0L6_2atmpS1954;
  uint64_t _M0L2s2S1957;
  uint64_t _M0L6_2atmpS1956;
  uint64_t _M0L2s3S1959;
  uint64_t _M0L6_2atmpS1958;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S781 = _M0L1rS782->$0;
  _M0L2s1S783 = _M0L1rS782->$1;
  _M0L2s2S784 = _M0L1rS782->$2;
  _M0L2s3S785 = _M0L1rS782->$3;
  _M0L3tmpS786 = _M0L2s0S781 + _M0L2s3S785;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1960 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS786, 23);
  _M0L3resS787 = _M0L6_2atmpS1960 + _M0L2s0S781;
  _M0L1tS788 = _M0L2s1S783 << 17;
  _M0L6_2atmpS1950 = _M0L2s2S784 ^ _M0L2s0S781;
  _M0L1rS782->$2 = _M0L6_2atmpS1950;
  _M0L6_2atmpS1951 = _M0L2s3S785 ^ _M0L2s1S783;
  _M0L1rS782->$3 = _M0L6_2atmpS1951;
  _M0L2s2S1953 = _M0L1rS782->$2;
  _M0L6_2atmpS1952 = _M0L2s1S783 ^ _M0L2s2S1953;
  _M0L1rS782->$1 = _M0L6_2atmpS1952;
  _M0L2s3S1955 = _M0L1rS782->$3;
  _M0L6_2atmpS1954 = _M0L2s0S781 ^ _M0L2s3S1955;
  _M0L1rS782->$0 = _M0L6_2atmpS1954;
  _M0L2s2S1957 = _M0L1rS782->$2;
  _M0L6_2atmpS1956 = _M0L2s2S1957 ^ _M0L1tS788;
  _M0L1rS782->$2 = _M0L6_2atmpS1956;
  _M0L2s3S1959 = _M0L1rS782->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1958 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S1959, 45);
  _M0L1rS782->$3 = _M0L6_2atmpS1958;
  return _M0L3resS787;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS779, int32_t _M0L1kS780) {
  uint64_t _M0L6_2atmpS1947;
  int32_t _M0L6_2atmpS1949;
  uint64_t _M0L6_2atmpS1948;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1947 = _M0L1xS779 << (_M0L1kS780 & 63);
  _M0L6_2atmpS1949 = 64 - _M0L1kS780;
  _M0L6_2atmpS1948 = _M0L1xS779 >> (_M0L6_2atmpS1949 & 63);
  return _M0L6_2atmpS1947 | _M0L6_2atmpS1948;
}

double _M0FPC14math2ln(double _M0L1xS765) {
  struct _M0TUdiE* _M0L7_2abindS766;
  double _M0L5_2af1S767;
  int32_t _M0L5_2akiS768;
  double _M0L1fS770;
  double _M0L1kS771;
  double _M0L6_2atmpS1940;
  double _M0L1sS772;
  double _M0L2s2S773;
  double _M0L2s4S774;
  double _M0L6_2atmpS1939;
  double _M0L6_2atmpS1938;
  double _M0L6_2atmpS1937;
  double _M0L6_2atmpS1936;
  double _M0L6_2atmpS1935;
  double _M0L6_2atmpS1934;
  double _M0L2t1S775;
  double _M0L6_2atmpS1933;
  double _M0L6_2atmpS1932;
  double _M0L6_2atmpS1931;
  double _M0L6_2atmpS1930;
  double _M0L2t2S776;
  double _M0L1rS777;
  double _M0L6_2atmpS1929;
  double _M0L4hfsqS778;
  double _M0L6_2atmpS1922;
  double _M0L6_2atmpS1928;
  double _M0L6_2atmpS1926;
  double _M0L6_2atmpS1927;
  double _M0L6_2atmpS1925;
  double _M0L6_2atmpS1924;
  double _M0L6_2atmpS1923;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS765 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS765)
      || _M0MPC16double6Double7is__inf(_M0L1xS765)
    ) {
      return _M0L1xS765;
    } else if (_M0L1xS765 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS766 = _M0FPC14math5frexp(_M0L1xS765);
  _M0L5_2af1S767 = _M0L7_2abindS766->$0;
  _M0L5_2akiS768 = _M0L7_2abindS766->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS766);
  if (_M0L5_2af1S767 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS1944 = _M0L5_2af1S767 * 0x1p+1;
    double _M0L6_2atmpS1941 = _M0L6_2atmpS1944 - 0x1p+0;
    int32_t _M0L6_2atmpS1943 = _M0L5_2akiS768 - 1;
    double _M0L6_2atmpS1942 = (double)_M0L6_2atmpS1943;
    _M0L1fS770 = _M0L6_2atmpS1941;
    _M0L1kS771 = _M0L6_2atmpS1942;
    goto join_769;
  } else {
    double _M0L6_2atmpS1945 = _M0L5_2af1S767 - 0x1p+0;
    double _M0L6_2atmpS1946 = (double)_M0L5_2akiS768;
    _M0L1fS770 = _M0L6_2atmpS1945;
    _M0L1kS771 = _M0L6_2atmpS1946;
    goto join_769;
  }
  join_769:;
  _M0L6_2atmpS1940 = 0x1p+1 + _M0L1fS770;
  _M0L1sS772 = _M0L1fS770 / _M0L6_2atmpS1940;
  _M0L2s2S773 = _M0L1sS772 * _M0L1sS772;
  _M0L2s4S774 = _M0L2s2S773 * _M0L2s2S773;
  _M0L6_2atmpS1939 = _M0L2s4S774 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS1938 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS1939;
  _M0L6_2atmpS1937 = _M0L2s4S774 * _M0L6_2atmpS1938;
  _M0L6_2atmpS1936 = 0x1.2492494229359p-2 + _M0L6_2atmpS1937;
  _M0L6_2atmpS1935 = _M0L2s4S774 * _M0L6_2atmpS1936;
  _M0L6_2atmpS1934 = 0x1.5555555555593p-1 + _M0L6_2atmpS1935;
  _M0L2t1S775 = _M0L2s2S773 * _M0L6_2atmpS1934;
  _M0L6_2atmpS1933 = _M0L2s4S774 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS1932 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS1933;
  _M0L6_2atmpS1931 = _M0L2s4S774 * _M0L6_2atmpS1932;
  _M0L6_2atmpS1930 = 0x1.999999997fa04p-2 + _M0L6_2atmpS1931;
  _M0L2t2S776 = _M0L2s4S774 * _M0L6_2atmpS1930;
  _M0L1rS777 = _M0L2t1S775 + _M0L2t2S776;
  _M0L6_2atmpS1929 = 0x1p-1 * _M0L1fS770;
  _M0L4hfsqS778 = _M0L6_2atmpS1929 * _M0L1fS770;
  _M0L6_2atmpS1922 = _M0L1kS771 * 0x1.62e42feep-1;
  _M0L6_2atmpS1928 = _M0L4hfsqS778 + _M0L1rS777;
  _M0L6_2atmpS1926 = _M0L1sS772 * _M0L6_2atmpS1928;
  _M0L6_2atmpS1927 = _M0L1kS771 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS1925 = _M0L6_2atmpS1926 + _M0L6_2atmpS1927;
  _M0L6_2atmpS1924 = _M0L4hfsqS778 - _M0L6_2atmpS1925;
  _M0L6_2atmpS1923 = _M0L6_2atmpS1924 - _M0L1fS770;
  return _M0L6_2atmpS1922 - _M0L6_2atmpS1923;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS758) {
  struct _M0TUdiE* _M0L7_2abindS759;
  double _M0L10_2anorm__fS760;
  int32_t _M0L6_2aexpS761;
  uint64_t _M0L1uS762;
  uint64_t _M0L6_2atmpS1921;
  uint64_t _M0L6_2atmpS1920;
  int32_t _M0L6_2atmpS1919;
  int32_t _M0L6_2atmpS1918;
  int32_t _M0L3expS763;
  uint64_t _M0L6_2atmpS1917;
  uint64_t _M0L6_2atmpS1916;
  uint64_t _M0L6_2atmpS1915;
  double _M0L4fracS764;
  struct _M0TUdiE* _block_2292;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS758 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS758)
    || _M0MPC16double6Double7is__nan(_M0L1fS758)
  ) {
    struct _M0TUdiE* _block_2291 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2291)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2291->$0 = _M0L1fS758;
    _block_2291->$1 = 0;
    return _block_2291;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS759 = _M0FPC14math9normalize(_M0L1fS758);
  _M0L10_2anorm__fS760 = _M0L7_2abindS759->$0;
  _M0L6_2aexpS761 = _M0L7_2abindS759->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS759);
  _M0L1uS762 = *(int64_t*)&_M0L10_2anorm__fS760;
  _M0L6_2atmpS1921 = _M0L1uS762 >> 52;
  _M0L6_2atmpS1920 = _M0L6_2atmpS1921 & 2047ull;
  _M0L6_2atmpS1919 = (int32_t)_M0L6_2atmpS1920;
  _M0L6_2atmpS1918 = _M0L6_2aexpS761 + _M0L6_2atmpS1919;
  _M0L3expS763 = _M0L6_2atmpS1918 - 1022;
  _M0L6_2atmpS1917 = ~9218868437227405312ull;
  _M0L6_2atmpS1916 = _M0L1uS762 & _M0L6_2atmpS1917;
  _M0L6_2atmpS1915 = _M0L6_2atmpS1916 | 4602678819172646912ull;
  _M0L4fracS764 = *(double*)&_M0L6_2atmpS1915;
  _block_2292 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2292)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2292->$0 = _M0L4fracS764;
  _block_2292->$1 = _M0L3expS763;
  return _block_2292;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS757) {
  double _M0L6_2atmpS1912;
  struct _M0TUdiE* _block_2294;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS1912 = fabs(_M0L1fS757);
  if (_M0L6_2atmpS1912 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS1914 = (double)4503599627370496ll;
    double _M0L6_2atmpS1913 = _M0L1fS757 * _M0L6_2atmpS1914;
    struct _M0TUdiE* _block_2293 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_2293)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_2293->$0 = _M0L6_2atmpS1913;
    _block_2293->$1 = -52;
    return _block_2293;
  }
  _block_2294 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_2294)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2294->$0 = _M0L1fS757;
  _block_2294->$1 = 0;
  return _block_2294;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS756) {
  double _M0L6_2atmpS1911;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1911 = (double)_M0L4selfS756;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1911);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS755) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS755 != _M0L4selfS755) {
    return 0;
  } else if (_M0L4selfS755 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS755 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS755;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS741,
  float _M0L4elemS743
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS740;
  int32_t _M0L1iS742;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS740 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS741);
  _M0L1iS742 = 0;
  while (1) {
    if (_M0L1iS742 < _M0L3lenS741) {
      float* _M0L3bufS1905 = _M0L3arrS740->$0;
      int32_t _M0L6_2atmpS1906;
      _M0L3bufS1905[_M0L1iS742] = _M0L4elemS743;
      _M0L6_2atmpS1906 = _M0L1iS742 + 1;
      _M0L1iS742 = _M0L6_2atmpS1906;
      continue;
    }
    break;
  }
  return _M0L3arrS740;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS746,
  int32_t _M0L4elemS748
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS745;
  int32_t _M0L1iS747;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS745 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS746);
  _M0L1iS747 = 0;
  while (1) {
    if (_M0L1iS747 < _M0L3lenS746) {
      uint8_t* _M0L3bufS1907 = _M0L3arrS745->$0;
      int32_t _M0L6_2atmpS1908;
      _M0L3bufS1907[_M0L1iS747] = _M0L4elemS748;
      _M0L6_2atmpS1908 = _M0L1iS747 + 1;
      _M0L1iS747 = _M0L6_2atmpS1908;
      continue;
    }
    break;
  }
  return _M0L3arrS745;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS751,
  int32_t _M0L4elemS753
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS750;
  int32_t _M0L1iS752;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS750 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS751);
  _M0L1iS752 = 0;
  while (1) {
    if (_M0L1iS752 < _M0L3lenS751) {
      int32_t* _M0L3bufS1909 = _M0L3arrS750->$0;
      int32_t _M0L6_2atmpS1910;
      _M0L3bufS1909[_M0L1iS752] = _M0L4elemS753;
      _M0L6_2atmpS1910 = _M0L1iS752 + 1;
      _M0L1iS752 = _M0L6_2atmpS1910;
      continue;
    }
    break;
  }
  return _M0L3arrS750;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS729,
  int32_t _M0L5indexS730,
  float _M0L5valueS731
) {
  int32_t _M0L3lenS728;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS728 = _M0L4selfS729->$1;
  if (_M0L5indexS730 >= 0 && _M0L5indexS730 < _M0L3lenS728) {
    float* _M0L6_2atmpS1902;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1902 = _M0MPC15array5Array6bufferGfE(_M0L4selfS729);
    _M0L6_2atmpS1902[_M0L5indexS730] = _M0L5valueS731;
    moonbit_decref_cycle_free(_M0L6_2atmpS1902);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS733,
  int32_t _M0L5indexS734,
  int32_t _M0L5valueS735
) {
  int32_t _M0L3lenS732;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS732 = _M0L4selfS733->$1;
  if (_M0L5indexS734 >= 0 && _M0L5indexS734 < _M0L3lenS732) {
    uint8_t* _M0L6_2atmpS1903;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1903 = _M0MPC15array5Array6bufferGbE(_M0L4selfS733);
    _M0L6_2atmpS1903[_M0L5indexS734] = _M0L5valueS735;
    moonbit_decref_cycle_free(_M0L6_2atmpS1903);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS737,
  int32_t _M0L5indexS738,
  int32_t _M0L5valueS739
) {
  int32_t _M0L3lenS736;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS736 = _M0L4selfS737->$1;
  if (_M0L5indexS738 >= 0 && _M0L5indexS738 < _M0L3lenS736) {
    int32_t* _M0L6_2atmpS1904;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1904 = _M0MPC15array5Array6bufferGiE(_M0L4selfS737);
    _M0L6_2atmpS1904[_M0L5indexS738] = _M0L5valueS739;
    moonbit_decref_cycle_free(_M0L6_2atmpS1904);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS717,
  int32_t _M0L5indexS718
) {
  int32_t _M0L3lenS716;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS716 = _M0L4selfS717->$1;
  if (_M0L5indexS718 >= 0 && _M0L5indexS718 < _M0L3lenS716) {
    uint8_t* _M0L6_2atmpS1898;
    int32_t _result_2298;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1898 = _M0MPC15array5Array6bufferGbE(_M0L4selfS717);
    _result_2298 = (int32_t)_M0L6_2atmpS1898[_M0L5indexS718];
    moonbit_decref_cycle_free(_M0L6_2atmpS1898);
    return _result_2298;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS720,
  int32_t _M0L5indexS721
) {
  int32_t _M0L3lenS719;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS719 = _M0L4selfS720->$1;
  if (_M0L5indexS721 >= 0 && _M0L5indexS721 < _M0L3lenS719) {
    float* _M0L6_2atmpS1899;
    float _result_2299;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1899 = _M0MPC15array5Array6bufferGfE(_M0L4selfS720);
    _result_2299 = (float)_M0L6_2atmpS1899[_M0L5indexS721];
    moonbit_decref_cycle_free(_M0L6_2atmpS1899);
    return _result_2299;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS723,
  int32_t _M0L5indexS724
) {
  int32_t _M0L3lenS722;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS722 = _M0L4selfS723->$1;
  if (_M0L5indexS724 >= 0 && _M0L5indexS724 < _M0L3lenS722) {
    int32_t* _M0L6_2atmpS1900;
    int32_t _result_2300;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1900 = _M0MPC15array5Array6bufferGiE(_M0L4selfS723);
    _result_2300 = (int32_t)_M0L6_2atmpS1900[_M0L5indexS724];
    moonbit_decref_cycle_free(_M0L6_2atmpS1900);
    return _result_2300;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS726,
  int32_t _M0L5indexS727
) {
  int32_t _M0L3lenS725;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS725 = _M0L4selfS726->$1;
  if (_M0L5indexS727 >= 0 && _M0L5indexS727 < _M0L3lenS725) {
    moonbit_string_t* _M0L6_2atmpS1901;
    moonbit_string_t _M0L6_2atmpS2219;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1901 = _M0MPC15array5Array6bufferGsE(_M0L4selfS726);
    _M0L6_2atmpS2219 = (moonbit_string_t)_M0L6_2atmpS1901[_M0L5indexS727];
    moonbit_incref_cycle_free(_M0L6_2atmpS2219);
    moonbit_decref_cycle_free(_M0L6_2atmpS1901);
    return _M0L6_2atmpS2219;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS715) {
  moonbit_string_t _M0L6_2atmpS1897;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1897 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS715);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1897);
  moonbit_decref_cycle_free(_M0L6_2atmpS1897);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS714) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS714);
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS713) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS713 > _M0FPB18double__max__value
         || _M0L4selfS713 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS712) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS712 != _M0L4selfS712;
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS697) {
  uint64_t _M0L4bitsS700;
  uint64_t _M0L6_2atmpS1896;
  uint64_t _M0L6_2atmpS1895;
  int32_t _M0L8ieeeSignS701;
  uint64_t _M0L12ieeeMantissaS702;
  uint64_t _M0L6_2atmpS1894;
  uint64_t _M0L6_2atmpS1893;
  int32_t _M0L12ieeeExponentS703;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS704;
  struct _M0TPB17FloatingDecimal64* _M0L1vS705;
  moonbit_string_t _result_2302;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS697 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  if (_M0L3valS697 >= -0x1p+53 && _M0L3valS697 <= 0x1p+53) {
    if (_M0L3valS697 >= -0x1p+31 && _M0L3valS697 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS698;
      double _M0L6_2atmpS1882;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS698 = _M0MPC16double6Double7to__int(_M0L3valS697);
      _M0L6_2atmpS1882 = (double)_M0L1iS698;
      if (_M0L6_2atmpS1882 == _M0L3valS697) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS698, 10);
      }
    } else {
      int64_t _M0L1iS699;
      double _M0L6_2atmpS1883;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS699 = _M0MPC16double6Double9to__int64(_M0L3valS697);
      _M0L6_2atmpS1883 = (double)_M0L1iS699;
      if (_M0L6_2atmpS1883 == _M0L3valS697) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS699, 10);
      }
    }
  }
  _M0L4bitsS700 = *(int64_t*)&_M0L3valS697;
  _M0L6_2atmpS1896 = _M0L4bitsS700 >> 63;
  _M0L6_2atmpS1895 = _M0L6_2atmpS1896 & 1ull;
  _M0L8ieeeSignS701 = _M0L6_2atmpS1895 != 0ull;
  _M0L12ieeeMantissaS702 = _M0L4bitsS700 & 4503599627370495ull;
  _M0L6_2atmpS1894 = _M0L4bitsS700 >> 52;
  _M0L6_2atmpS1893 = _M0L6_2atmpS1894 & 2047ull;
  _M0L12ieeeExponentS703 = (int32_t)_M0L6_2atmpS1893;
  if (
    _M0L12ieeeExponentS703 == 2047
    || _M0L12ieeeExponentS703 == 0 && _M0L12ieeeMantissaS702 == 0ull
  ) {
    int32_t _M0L6_2atmpS1884 = _M0L12ieeeExponentS703 != 0;
    int32_t _M0L6_2atmpS1885 = _M0L12ieeeMantissaS702 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS701, _M0L6_2atmpS1884, _M0L6_2atmpS1885);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS704
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS702, _M0L12ieeeExponentS703);
  if (_M0L7_2abindS704 == 0) {
    uint32_t _M0L6_2atmpS1886;
    if (_M0L7_2abindS704) {
      moonbit_decref_cycle_free(_M0L7_2abindS704);
    }
    _M0L6_2atmpS1886 = *(uint32_t*)&_M0L12ieeeExponentS703;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS705 = _M0FPB3d2d(_M0L12ieeeMantissaS702, _M0L6_2atmpS1886);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS706 = _M0L7_2abindS704;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS707 = _M0L7_2aSomeS706;
    struct _M0TPB17FloatingDecimal64* _M0L1xS708 = _M0L4_2afS707;
    while (1) {
      uint64_t _M0L8mantissaS1892 = _M0L1xS708->$0;
      uint64_t _M0L1qS709 = _M0L8mantissaS1892 / 10ull;
      uint64_t _M0L8mantissaS1890 = _M0L1xS708->$0;
      uint64_t _M0L6_2atmpS1891 = 10ull * _M0L1qS709;
      uint64_t _M0L1rS710 = _M0L8mantissaS1890 - _M0L6_2atmpS1891;
      int32_t _M0L8exponentS1889;
      int32_t _M0L6_2atmpS1888;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1887;
      if (_M0L1rS710 != 0ull) {
        _M0L1vS705 = _M0L1xS708;
        break;
      }
      _M0L8exponentS1889 = _M0L1xS708->$1;
      moonbit_decref_cycle_free(_M0L1xS708);
      _M0L6_2atmpS1888 = _M0L8exponentS1889 + 1;
      _M0L6_2atmpS1887
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1887)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1887->$0 = _M0L1qS709;
      _M0L6_2atmpS1887->$1 = _M0L6_2atmpS1888;
      _M0L1xS708 = _M0L6_2atmpS1887;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2302 = _M0FPB9to__chars(_M0L1vS705, _M0L8ieeeSignS701);
  moonbit_decref_cycle_free(_M0L1vS705);
  return _result_2302;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS692,
  int32_t _M0L12ieeeExponentS694
) {
  uint64_t _M0L2m2S691;
  int32_t _M0L6_2atmpS1881;
  int32_t _M0L2e2S693;
  int32_t _M0L6_2atmpS1880;
  uint64_t _M0L6_2atmpS1879;
  uint64_t _M0L4maskS695;
  uint64_t _M0L8fractionS696;
  int32_t _M0L6_2atmpS1878;
  uint64_t _M0L6_2atmpS1877;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1876;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S691 = 4503599627370496ull | _M0L12ieeeMantissaS692;
  _M0L6_2atmpS1881 = _M0L12ieeeExponentS694 - 1023;
  _M0L2e2S693 = _M0L6_2atmpS1881 - 52;
  if (_M0L2e2S693 > 0) {
    return 0;
  }
  if (_M0L2e2S693 < -52) {
    return 0;
  }
  _M0L6_2atmpS1880 = -_M0L2e2S693;
  _M0L6_2atmpS1879 = 1ull << (_M0L6_2atmpS1880 & 63);
  _M0L4maskS695 = _M0L6_2atmpS1879 - 1ull;
  _M0L8fractionS696 = _M0L2m2S691 & _M0L4maskS695;
  if (_M0L8fractionS696 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1878 = -_M0L2e2S693;
  _M0L6_2atmpS1877 = _M0L2m2S691 >> (_M0L6_2atmpS1878 & 63);
  _M0L6_2atmpS1876
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1876)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1876->$0 = _M0L6_2atmpS1877;
  _M0L6_2atmpS1876->$1 = 0;
  return _M0L6_2atmpS1876;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS659,
  int32_t _M0L4signS657
) {
  moonbit_bytes_t _M0L6resultS655;
  int32_t _M0Lm5indexS656;
  uint64_t _M0L6outputS658;
  int32_t _M0L7olengthS660;
  int32_t _M0L8exponentS1875;
  int32_t _M0L6_2atmpS1874;
  int32_t _M0Lm3expS661;
  int32_t _M0L6_2atmpS1873;
  int32_t _M0L6_2atmpS1871;
  int32_t _M0L18scientificNotationS662;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS655 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS656 = 0;
  if (_M0L4signS657) {
    int32_t _M0L6_2atmpS1745 = _M0Lm5indexS656;
    int32_t _M0L6_2atmpS1746;
    if (
      _M0L6_2atmpS1745 < 0
      || _M0L6_2atmpS1745 >= Moonbit_array_length(_M0L6resultS655)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS655[_M0L6_2atmpS1745] = 45;
    _M0L6_2atmpS1746 = _M0Lm5indexS656;
    _M0Lm5indexS656 = _M0L6_2atmpS1746 + 1;
  }
  _M0L6outputS658 = _M0L1vS659->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS660 = _M0FPB17decimal__length17(_M0L6outputS658);
  _M0L8exponentS1875 = _M0L1vS659->$1;
  _M0L6_2atmpS1874 = _M0L8exponentS1875 + _M0L7olengthS660;
  _M0Lm3expS661 = _M0L6_2atmpS1874 - 1;
  _M0L6_2atmpS1873 = _M0Lm3expS661;
  if (_M0L6_2atmpS1873 >= -6) {
    int32_t _M0L6_2atmpS1872 = _M0Lm3expS661;
    _M0L6_2atmpS1871 = _M0L6_2atmpS1872 < 21;
  } else {
    _M0L6_2atmpS1871 = 0;
  }
  _M0L18scientificNotationS662 = !_M0L6_2atmpS1871;
  if (_M0L18scientificNotationS662) {
    int32_t _M0L7_2abindS663 = _M0L7olengthS660 - 1;
    uint64_t _M0L6outputS664;
    int32_t _M0L1iS665 = 0;
    uint64_t _M0L6outputS666 = _M0L6outputS658;
    int32_t _M0L6_2atmpS1747;
    int32_t _M0L6_2atmpS1751;
    int32_t _M0L6_2atmpS1750;
    int32_t _M0L6_2atmpS1749;
    int32_t _M0L6_2atmpS1748;
    int32_t _M0L6_2atmpS1755;
    int32_t _M0L6_2atmpS1756;
    int32_t _M0L6_2atmpS1757;
    int32_t _M0L6_2atmpS1758;
    int32_t _M0L6_2atmpS1759;
    int32_t _M0L6_2atmpS1765;
    int32_t _M0L6_2atmpS1798;
    moonbit_string_t _result_2304;
    while (1) {
      if (_M0L1iS665 < _M0L7_2abindS663) {
        uint64_t _M0L1cS667 = _M0L6outputS666 % 10ull;
        int32_t _M0L6_2atmpS1804 = _M0Lm5indexS656;
        int32_t _M0L6_2atmpS1803 = _M0L6_2atmpS1804 + _M0L7olengthS660;
        int32_t _M0L6_2atmpS1799 = _M0L6_2atmpS1803 - _M0L1iS665;
        int32_t _M0L6_2atmpS1802 = (int32_t)_M0L1cS667;
        int32_t _M0L6_2atmpS1801 = 48 + _M0L6_2atmpS1802;
        int32_t _M0L6_2atmpS1800 = _M0L6_2atmpS1801 & 0xff;
        int32_t _M0L6_2atmpS1805;
        uint64_t _M0L6_2atmpS1806;
        if (
          _M0L6_2atmpS1799 < 0
          || _M0L6_2atmpS1799 >= Moonbit_array_length(_M0L6resultS655)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS655[_M0L6_2atmpS1799] = _M0L6_2atmpS1800;
        _M0L6_2atmpS1805 = _M0L1iS665 + 1;
        _M0L6_2atmpS1806 = _M0L6outputS666 / 10ull;
        _M0L1iS665 = _M0L6_2atmpS1805;
        _M0L6outputS666 = _M0L6_2atmpS1806;
        continue;
      } else {
        _M0L6outputS664 = _M0L6outputS666;
      }
      break;
    }
    _M0L6_2atmpS1747 = _M0Lm5indexS656;
    _M0L6_2atmpS1751 = (int32_t)_M0L6outputS664;
    _M0L6_2atmpS1750 = _M0L6_2atmpS1751 % 10;
    _M0L6_2atmpS1749 = 48 + _M0L6_2atmpS1750;
    _M0L6_2atmpS1748 = _M0L6_2atmpS1749 & 0xff;
    if (
      _M0L6_2atmpS1747 < 0
      || _M0L6_2atmpS1747 >= Moonbit_array_length(_M0L6resultS655)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS655[_M0L6_2atmpS1747] = _M0L6_2atmpS1748;
    if (_M0L7olengthS660 > 1) {
      int32_t _M0L6_2atmpS1753 = _M0Lm5indexS656;
      int32_t _M0L6_2atmpS1752 = _M0L6_2atmpS1753 + 1;
      if (
        _M0L6_2atmpS1752 < 0
        || _M0L6_2atmpS1752 >= Moonbit_array_length(_M0L6resultS655)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS655[_M0L6_2atmpS1752] = 46;
    } else {
      int32_t _M0L6_2atmpS1754 = _M0Lm5indexS656;
      _M0Lm5indexS656 = _M0L6_2atmpS1754 - 1;
    }
    _M0L6_2atmpS1755 = _M0Lm5indexS656;
    _M0L6_2atmpS1756 = _M0L7olengthS660 + 1;
    _M0Lm5indexS656 = _M0L6_2atmpS1755 + _M0L6_2atmpS1756;
    _M0L6_2atmpS1757 = _M0Lm5indexS656;
    if (
      _M0L6_2atmpS1757 < 0
      || _M0L6_2atmpS1757 >= Moonbit_array_length(_M0L6resultS655)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS655[_M0L6_2atmpS1757] = 101;
    _M0L6_2atmpS1758 = _M0Lm5indexS656;
    _M0Lm5indexS656 = _M0L6_2atmpS1758 + 1;
    _M0L6_2atmpS1759 = _M0Lm3expS661;
    if (_M0L6_2atmpS1759 < 0) {
      int32_t _M0L6_2atmpS1760 = _M0Lm5indexS656;
      int32_t _M0L6_2atmpS1761;
      int32_t _M0L6_2atmpS1762;
      if (
        _M0L6_2atmpS1760 < 0
        || _M0L6_2atmpS1760 >= Moonbit_array_length(_M0L6resultS655)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS655[_M0L6_2atmpS1760] = 45;
      _M0L6_2atmpS1761 = _M0Lm5indexS656;
      _M0Lm5indexS656 = _M0L6_2atmpS1761 + 1;
      _M0L6_2atmpS1762 = _M0Lm3expS661;
      _M0Lm3expS661 = -_M0L6_2atmpS1762;
    } else {
      int32_t _M0L6_2atmpS1763 = _M0Lm5indexS656;
      int32_t _M0L6_2atmpS1764;
      if (
        _M0L6_2atmpS1763 < 0
        || _M0L6_2atmpS1763 >= Moonbit_array_length(_M0L6resultS655)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS655[_M0L6_2atmpS1763] = 43;
      _M0L6_2atmpS1764 = _M0Lm5indexS656;
      _M0Lm5indexS656 = _M0L6_2atmpS1764 + 1;
    }
    _M0L6_2atmpS1765 = _M0Lm3expS661;
    if (_M0L6_2atmpS1765 >= 100) {
      int32_t _M0L6_2atmpS1781 = _M0Lm3expS661;
      int32_t _M0L1aS669 = _M0L6_2atmpS1781 / 100;
      int32_t _M0L6_2atmpS1780 = _M0Lm3expS661;
      int32_t _M0L6_2atmpS1779 = _M0L6_2atmpS1780 / 10;
      int32_t _M0L1bS670 = _M0L6_2atmpS1779 % 10;
      int32_t _M0L6_2atmpS1778 = _M0Lm3expS661;
      int32_t _M0L1cS671 = _M0L6_2atmpS1778 % 10;
      int32_t _M0L6_2atmpS1766 = _M0Lm5indexS656;
      int32_t _M0L6_2atmpS1768 = 48 + _M0L1aS669;
      int32_t _M0L6_2atmpS1767 = _M0L6_2atmpS1768 & 0xff;
      int32_t _M0L6_2atmpS1772;
      int32_t _M0L6_2atmpS1769;
      int32_t _M0L6_2atmpS1771;
      int32_t _M0L6_2atmpS1770;
      int32_t _M0L6_2atmpS1776;
      int32_t _M0L6_2atmpS1773;
      int32_t _M0L6_2atmpS1775;
      int32_t _M0L6_2atmpS1774;
      int32_t _M0L6_2atmpS1777;
      if (
        _M0L6_2atmpS1766 < 0
        || _M0L6_2atmpS1766 >= Moonbit_array_length(_M0L6resultS655)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS655[_M0L6_2atmpS1766] = _M0L6_2atmpS1767;
      _M0L6_2atmpS1772 = _M0Lm5indexS656;
      _M0L6_2atmpS1769 = _M0L6_2atmpS1772 + 1;
      _M0L6_2atmpS1771 = 48 + _M0L1bS670;
      _M0L6_2atmpS1770 = _M0L6_2atmpS1771 & 0xff;
      if (
        _M0L6_2atmpS1769 < 0
        || _M0L6_2atmpS1769 >= Moonbit_array_length(_M0L6resultS655)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS655[_M0L6_2atmpS1769] = _M0L6_2atmpS1770;
      _M0L6_2atmpS1776 = _M0Lm5indexS656;
      _M0L6_2atmpS1773 = _M0L6_2atmpS1776 + 2;
      _M0L6_2atmpS1775 = 48 + _M0L1cS671;
      _M0L6_2atmpS1774 = _M0L6_2atmpS1775 & 0xff;
      if (
        _M0L6_2atmpS1773 < 0
        || _M0L6_2atmpS1773 >= Moonbit_array_length(_M0L6resultS655)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS655[_M0L6_2atmpS1773] = _M0L6_2atmpS1774;
      _M0L6_2atmpS1777 = _M0Lm5indexS656;
      _M0Lm5indexS656 = _M0L6_2atmpS1777 + 3;
    } else {
      int32_t _M0L6_2atmpS1782 = _M0Lm3expS661;
      if (_M0L6_2atmpS1782 >= 10) {
        int32_t _M0L6_2atmpS1792 = _M0Lm3expS661;
        int32_t _M0L1aS672 = _M0L6_2atmpS1792 / 10;
        int32_t _M0L6_2atmpS1791 = _M0Lm3expS661;
        int32_t _M0L1bS673 = _M0L6_2atmpS1791 % 10;
        int32_t _M0L6_2atmpS1783 = _M0Lm5indexS656;
        int32_t _M0L6_2atmpS1785 = 48 + _M0L1aS672;
        int32_t _M0L6_2atmpS1784 = _M0L6_2atmpS1785 & 0xff;
        int32_t _M0L6_2atmpS1789;
        int32_t _M0L6_2atmpS1786;
        int32_t _M0L6_2atmpS1788;
        int32_t _M0L6_2atmpS1787;
        int32_t _M0L6_2atmpS1790;
        if (
          _M0L6_2atmpS1783 < 0
          || _M0L6_2atmpS1783 >= Moonbit_array_length(_M0L6resultS655)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS655[_M0L6_2atmpS1783] = _M0L6_2atmpS1784;
        _M0L6_2atmpS1789 = _M0Lm5indexS656;
        _M0L6_2atmpS1786 = _M0L6_2atmpS1789 + 1;
        _M0L6_2atmpS1788 = 48 + _M0L1bS673;
        _M0L6_2atmpS1787 = _M0L6_2atmpS1788 & 0xff;
        if (
          _M0L6_2atmpS1786 < 0
          || _M0L6_2atmpS1786 >= Moonbit_array_length(_M0L6resultS655)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS655[_M0L6_2atmpS1786] = _M0L6_2atmpS1787;
        _M0L6_2atmpS1790 = _M0Lm5indexS656;
        _M0Lm5indexS656 = _M0L6_2atmpS1790 + 2;
      } else {
        int32_t _M0L6_2atmpS1793 = _M0Lm5indexS656;
        int32_t _M0L6_2atmpS1796 = _M0Lm3expS661;
        int32_t _M0L6_2atmpS1795 = 48 + _M0L6_2atmpS1796;
        int32_t _M0L6_2atmpS1794 = _M0L6_2atmpS1795 & 0xff;
        int32_t _M0L6_2atmpS1797;
        if (
          _M0L6_2atmpS1793 < 0
          || _M0L6_2atmpS1793 >= Moonbit_array_length(_M0L6resultS655)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS655[_M0L6_2atmpS1793] = _M0L6_2atmpS1794;
        _M0L6_2atmpS1797 = _M0Lm5indexS656;
        _M0Lm5indexS656 = _M0L6_2atmpS1797 + 1;
      }
    }
    _M0L6_2atmpS1798 = _M0Lm5indexS656;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2304
    = _M0FPB19string__from__bytes(_M0L6resultS655, 0, _M0L6_2atmpS1798);
    moonbit_decref_cycle_free(_M0L6resultS655);
    return _result_2304;
  } else {
    int32_t _M0L6_2atmpS1807 = _M0Lm3expS661;
    int32_t _M0L6_2atmpS1870;
    moonbit_string_t _result_2310;
    if (_M0L6_2atmpS1807 < 0) {
      int32_t _M0L6_2atmpS1808 = _M0Lm5indexS656;
      int32_t _M0L6_2atmpS1810;
      int32_t _M0L6_2atmpS1809;
      int32_t _M0L6_2atmpS1811;
      int32_t _M0L1iS674;
      int32_t _M0L6_2atmpS1826;
      int32_t _M0L6_2atmpS1828;
      int32_t _M0L6_2atmpS1827;
      int32_t _M0L7currentS676;
      int32_t _M0L1iS677;
      uint64_t _M0L6outputS678;
      if (
        _M0L6_2atmpS1808 < 0
        || _M0L6_2atmpS1808 >= Moonbit_array_length(_M0L6resultS655)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS655[_M0L6_2atmpS1808] = 48;
      _M0L6_2atmpS1810 = _M0Lm5indexS656;
      _M0L6_2atmpS1809 = _M0L6_2atmpS1810 + 1;
      if (
        _M0L6_2atmpS1809 < 0
        || _M0L6_2atmpS1809 >= Moonbit_array_length(_M0L6resultS655)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS655[_M0L6_2atmpS1809] = 46;
      _M0L6_2atmpS1811 = _M0Lm5indexS656;
      _M0Lm5indexS656 = _M0L6_2atmpS1811 + 2;
      _M0L1iS674 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1812 = _M0Lm3expS661;
        if (_M0L1iS674 > _M0L6_2atmpS1812) {
          int32_t _M0L6_2atmpS1815 = _M0Lm5indexS656;
          int32_t _M0L6_2atmpS1814 = _M0L6_2atmpS1815 - _M0L1iS674;
          int32_t _M0L6_2atmpS1813 = _M0L6_2atmpS1814 - 1;
          int32_t _M0L6_2atmpS1816;
          if (
            _M0L6_2atmpS1813 < 0
            || _M0L6_2atmpS1813 >= Moonbit_array_length(_M0L6resultS655)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS655[_M0L6_2atmpS1813] = 48;
          _M0L6_2atmpS1816 = _M0L1iS674 - 1;
          _M0L1iS674 = _M0L6_2atmpS1816;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1826 = _M0Lm5indexS656;
      _M0L6_2atmpS1828 = _M0Lm3expS661;
      _M0L6_2atmpS1827 = -1 - _M0L6_2atmpS1828;
      _M0L7currentS676 = _M0L6_2atmpS1826 + _M0L6_2atmpS1827;
      _M0L1iS677 = 0;
      _M0L6outputS678 = _M0L6outputS658;
      while (1) {
        if (_M0L1iS677 < _M0L7olengthS660) {
          int32_t _M0L6_2atmpS1823 = _M0L7currentS676 + _M0L7olengthS660;
          int32_t _M0L6_2atmpS1822 = _M0L6_2atmpS1823 - _M0L1iS677;
          int32_t _M0L6_2atmpS1817 = _M0L6_2atmpS1822 - 1;
          uint64_t _M0L6_2atmpS1821 = _M0L6outputS678 % 10ull;
          int32_t _M0L6_2atmpS1820 = (int32_t)_M0L6_2atmpS1821;
          int32_t _M0L6_2atmpS1819 = 48 + _M0L6_2atmpS1820;
          int32_t _M0L6_2atmpS1818 = _M0L6_2atmpS1819 & 0xff;
          int32_t _M0L6_2atmpS1824;
          uint64_t _M0L6_2atmpS1825;
          if (
            _M0L6_2atmpS1817 < 0
            || _M0L6_2atmpS1817 >= Moonbit_array_length(_M0L6resultS655)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS655[_M0L6_2atmpS1817] = _M0L6_2atmpS1818;
          _M0L6_2atmpS1824 = _M0L1iS677 + 1;
          _M0L6_2atmpS1825 = _M0L6outputS678 / 10ull;
          _M0L1iS677 = _M0L6_2atmpS1824;
          _M0L6outputS678 = _M0L6_2atmpS1825;
          continue;
        }
        break;
      }
      _M0Lm5indexS656 = _M0L7currentS676 + _M0L7olengthS660;
    } else {
      int32_t _M0L6_2atmpS1830 = _M0Lm3expS661;
      int32_t _M0L6_2atmpS1829 = _M0L6_2atmpS1830 + 1;
      if (_M0L6_2atmpS1829 >= _M0L7olengthS660) {
        int32_t _M0L1iS680 = 0;
        uint64_t _M0L6outputS681 = _M0L6outputS658;
        int32_t _M0L6_2atmpS1841;
        int32_t _M0L6_2atmpS1846;
        int32_t _M0L7_2abindS683;
        int32_t _M0L1iS684;
        int32_t _M0L6_2atmpS1847;
        int32_t _M0L6_2atmpS1850;
        int32_t _M0L6_2atmpS1849;
        int32_t _M0L6_2atmpS1848;
        while (1) {
          if (_M0L1iS680 < _M0L7olengthS660) {
            int32_t _M0L6_2atmpS1838 = _M0Lm5indexS656;
            int32_t _M0L6_2atmpS1837 = _M0L6_2atmpS1838 + _M0L7olengthS660;
            int32_t _M0L6_2atmpS1836 = _M0L6_2atmpS1837 - _M0L1iS680;
            int32_t _M0L6_2atmpS1831 = _M0L6_2atmpS1836 - 1;
            uint64_t _M0L6_2atmpS1835 = _M0L6outputS681 % 10ull;
            int32_t _M0L6_2atmpS1834 = (int32_t)_M0L6_2atmpS1835;
            int32_t _M0L6_2atmpS1833 = 48 + _M0L6_2atmpS1834;
            int32_t _M0L6_2atmpS1832 = _M0L6_2atmpS1833 & 0xff;
            int32_t _M0L6_2atmpS1839;
            uint64_t _M0L6_2atmpS1840;
            if (
              _M0L6_2atmpS1831 < 0
              || _M0L6_2atmpS1831 >= Moonbit_array_length(_M0L6resultS655)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS655[_M0L6_2atmpS1831] = _M0L6_2atmpS1832;
            _M0L6_2atmpS1839 = _M0L1iS680 + 1;
            _M0L6_2atmpS1840 = _M0L6outputS681 / 10ull;
            _M0L1iS680 = _M0L6_2atmpS1839;
            _M0L6outputS681 = _M0L6_2atmpS1840;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1841 = _M0Lm5indexS656;
        _M0Lm5indexS656 = _M0L6_2atmpS1841 + _M0L7olengthS660;
        _M0L6_2atmpS1846 = _M0Lm3expS661;
        _M0L7_2abindS683 = _M0L6_2atmpS1846 + 1;
        _M0L1iS684 = _M0L7olengthS660;
        while (1) {
          if (_M0L1iS684 < _M0L7_2abindS683) {
            int32_t _M0L6_2atmpS1844 = _M0Lm5indexS656;
            int32_t _M0L6_2atmpS1843 = _M0L6_2atmpS1844 + _M0L1iS684;
            int32_t _M0L6_2atmpS1842 = _M0L6_2atmpS1843 - _M0L7olengthS660;
            int32_t _M0L6_2atmpS1845;
            if (
              _M0L6_2atmpS1842 < 0
              || _M0L6_2atmpS1842 >= Moonbit_array_length(_M0L6resultS655)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS655[_M0L6_2atmpS1842] = 48;
            _M0L6_2atmpS1845 = _M0L1iS684 + 1;
            _M0L1iS684 = _M0L6_2atmpS1845;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1847 = _M0Lm5indexS656;
        _M0L6_2atmpS1850 = _M0Lm3expS661;
        _M0L6_2atmpS1849 = _M0L6_2atmpS1850 + 1;
        _M0L6_2atmpS1848 = _M0L6_2atmpS1849 - _M0L7olengthS660;
        _M0Lm5indexS656 = _M0L6_2atmpS1847 + _M0L6_2atmpS1848;
      } else {
        int32_t _M0L6_2atmpS1867 = _M0Lm5indexS656;
        int32_t _M0L6_2atmpS1866 = _M0L6_2atmpS1867 + 1;
        int32_t _M0L1iS686 = 0;
        int32_t _M0L7currentS687 = _M0L6_2atmpS1866;
        uint64_t _M0L6outputS688 = _M0L6outputS658;
        int32_t _M0L6_2atmpS1868;
        int32_t _M0L6_2atmpS1869;
        while (1) {
          if (_M0L1iS686 < _M0L7olengthS660) {
            int32_t _M0L6_2atmpS1862 = _M0L7olengthS660 - _M0L1iS686;
            int32_t _M0L6_2atmpS1860 = _M0L6_2atmpS1862 - 1;
            int32_t _M0L6_2atmpS1861 = _M0Lm3expS661;
            int32_t _M0L7currentS689;
            int32_t _M0L6_2atmpS1857;
            int32_t _M0L6_2atmpS1856;
            int32_t _M0L6_2atmpS1851;
            uint64_t _M0L6_2atmpS1855;
            int32_t _M0L6_2atmpS1854;
            int32_t _M0L6_2atmpS1853;
            int32_t _M0L6_2atmpS1852;
            int32_t _M0L6_2atmpS1858;
            uint64_t _M0L6_2atmpS1859;
            if (_M0L6_2atmpS1860 == _M0L6_2atmpS1861) {
              int32_t _M0L6_2atmpS1865 = _M0L7currentS687 + _M0L7olengthS660;
              int32_t _M0L6_2atmpS1864 = _M0L6_2atmpS1865 - _M0L1iS686;
              int32_t _M0L6_2atmpS1863 = _M0L6_2atmpS1864 - 1;
              if (
                _M0L6_2atmpS1863 < 0
                || _M0L6_2atmpS1863 >= Moonbit_array_length(_M0L6resultS655)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS655[_M0L6_2atmpS1863] = 46;
              _M0L7currentS689 = _M0L7currentS687 - 1;
            } else {
              _M0L7currentS689 = _M0L7currentS687;
            }
            _M0L6_2atmpS1857 = _M0L7currentS689 + _M0L7olengthS660;
            _M0L6_2atmpS1856 = _M0L6_2atmpS1857 - _M0L1iS686;
            _M0L6_2atmpS1851 = _M0L6_2atmpS1856 - 1;
            _M0L6_2atmpS1855 = _M0L6outputS688 % 10ull;
            _M0L6_2atmpS1854 = (int32_t)_M0L6_2atmpS1855;
            _M0L6_2atmpS1853 = 48 + _M0L6_2atmpS1854;
            _M0L6_2atmpS1852 = _M0L6_2atmpS1853 & 0xff;
            if (
              _M0L6_2atmpS1851 < 0
              || _M0L6_2atmpS1851 >= Moonbit_array_length(_M0L6resultS655)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS655[_M0L6_2atmpS1851] = _M0L6_2atmpS1852;
            _M0L6_2atmpS1858 = _M0L1iS686 + 1;
            _M0L6_2atmpS1859 = _M0L6outputS688 / 10ull;
            _M0L1iS686 = _M0L6_2atmpS1858;
            _M0L7currentS687 = _M0L7currentS689;
            _M0L6outputS688 = _M0L6_2atmpS1859;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1868 = _M0Lm5indexS656;
        _M0L6_2atmpS1869 = _M0L7olengthS660 + 1;
        _M0Lm5indexS656 = _M0L6_2atmpS1868 + _M0L6_2atmpS1869;
      }
    }
    _M0L6_2atmpS1870 = _M0Lm5indexS656;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2310
    = _M0FPB19string__from__bytes(_M0L6resultS655, 0, _M0L6_2atmpS1870);
    moonbit_decref_cycle_free(_M0L6resultS655);
    return _result_2310;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS601,
  uint32_t _M0L12ieeeExponentS600
) {
  int32_t _M0Lm2e2S598;
  uint64_t _M0Lm2m2S599;
  uint64_t _M0L6_2atmpS1744;
  uint64_t _M0L6_2atmpS1743;
  int32_t _M0L4evenS602;
  uint64_t _M0L6_2atmpS1742;
  uint64_t _M0L2mvS603;
  int32_t _M0L7mmShiftS604;
  uint64_t _M0Lm2vrS605;
  uint64_t _M0Lm2vpS606;
  uint64_t _M0Lm2vmS607;
  int32_t _M0Lm3e10S608;
  int32_t _M0Lm17vmIsTrailingZerosS609;
  int32_t _M0Lm17vrIsTrailingZerosS610;
  int32_t _M0L6_2atmpS1644;
  int32_t _M0Lm7removedS629;
  int32_t _M0Lm16lastRemovedDigitS630;
  uint64_t _M0Lm6outputS631;
  int32_t _M0L6_2atmpS1740;
  int32_t _M0L6_2atmpS1741;
  int32_t _M0L3expS654;
  uint64_t _M0L6_2atmpS1739;
  struct _M0TPB17FloatingDecimal64* _block_2316;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S598 = 0;
  _M0Lm2m2S599 = 0ull;
  if (_M0L12ieeeExponentS600 == 0u) {
    _M0Lm2e2S598 = -1076;
    _M0Lm2m2S599 = _M0L12ieeeMantissaS601;
  } else {
    int32_t _M0L6_2atmpS1643 = *(int32_t*)&_M0L12ieeeExponentS600;
    int32_t _M0L6_2atmpS1642 = _M0L6_2atmpS1643 - 1023;
    int32_t _M0L6_2atmpS1641 = _M0L6_2atmpS1642 - 52;
    _M0Lm2e2S598 = _M0L6_2atmpS1641 - 2;
    _M0Lm2m2S599 = 4503599627370496ull | _M0L12ieeeMantissaS601;
  }
  _M0L6_2atmpS1744 = _M0Lm2m2S599;
  _M0L6_2atmpS1743 = _M0L6_2atmpS1744 & 1ull;
  _M0L4evenS602 = _M0L6_2atmpS1743 == 0ull;
  _M0L6_2atmpS1742 = _M0Lm2m2S599;
  _M0L2mvS603 = 4ull * _M0L6_2atmpS1742;
  _M0L7mmShiftS604
  = _M0L12ieeeMantissaS601 != 0ull || _M0L12ieeeExponentS600 <= 1u;
  _M0Lm2vrS605 = 0ull;
  _M0Lm2vpS606 = 0ull;
  _M0Lm2vmS607 = 0ull;
  _M0Lm3e10S608 = 0;
  _M0Lm17vmIsTrailingZerosS609 = 0;
  _M0Lm17vrIsTrailingZerosS610 = 0;
  _M0L6_2atmpS1644 = _M0Lm2e2S598;
  if (_M0L6_2atmpS1644 >= 0) {
    int32_t _M0L6_2atmpS1666 = _M0Lm2e2S598;
    int32_t _M0L6_2atmpS1662;
    int32_t _M0L6_2atmpS1665;
    int32_t _M0L6_2atmpS1664;
    int32_t _M0L6_2atmpS1663;
    int32_t _M0L1qS611;
    int32_t _M0L6_2atmpS1661;
    int32_t _M0L6_2atmpS1660;
    int32_t _M0L1kS612;
    int32_t _M0L6_2atmpS1659;
    int32_t _M0L6_2atmpS1658;
    int32_t _M0L6_2atmpS1657;
    int32_t _M0L1iS613;
    struct _M0TPB8Pow5Pair _M0L4pow5S614;
    uint64_t _M0L6_2atmpS1656;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS615;
    uint64_t _M0L8_2avrOutS616;
    uint64_t _M0L8_2avpOutS617;
    uint64_t _M0L8_2avmOutS618;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1662 = _M0FPB9log10Pow2(_M0L6_2atmpS1666);
    _M0L6_2atmpS1665 = _M0Lm2e2S598;
    _M0L6_2atmpS1664 = _M0L6_2atmpS1665 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1663 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1664);
    _M0L1qS611 = _M0L6_2atmpS1662 - _M0L6_2atmpS1663;
    _M0Lm3e10S608 = _M0L1qS611;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1661 = _M0FPB8pow5bits(_M0L1qS611);
    _M0L6_2atmpS1660 = 125 + _M0L6_2atmpS1661;
    _M0L1kS612 = _M0L6_2atmpS1660 - 1;
    _M0L6_2atmpS1659 = _M0Lm2e2S598;
    _M0L6_2atmpS1658 = -_M0L6_2atmpS1659;
    _M0L6_2atmpS1657 = _M0L6_2atmpS1658 + _M0L1qS611;
    _M0L1iS613 = _M0L6_2atmpS1657 + _M0L1kS612;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S614 = _M0FPB22double__computeInvPow5(_M0L1qS611);
    _M0L6_2atmpS1656 = _M0Lm2m2S599;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS615
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1656, _M0L4pow5S614, _M0L1iS613, _M0L7mmShiftS604);
    _M0L8_2avrOutS616 = _M0L7_2abindS615.$0;
    _M0L8_2avpOutS617 = _M0L7_2abindS615.$1;
    _M0L8_2avmOutS618 = _M0L7_2abindS615.$2;
    _M0Lm2vrS605 = _M0L8_2avrOutS616;
    _M0Lm2vpS606 = _M0L8_2avpOutS617;
    _M0Lm2vmS607 = _M0L8_2avmOutS618;
    if (_M0L1qS611 <= 21) {
      int32_t _M0L6_2atmpS1652 = (int32_t)_M0L2mvS603;
      uint64_t _M0L6_2atmpS1655 = _M0L2mvS603 / 5ull;
      int32_t _M0L6_2atmpS1654 = (int32_t)_M0L6_2atmpS1655;
      int32_t _M0L6_2atmpS1653 = 5 * _M0L6_2atmpS1654;
      int32_t _M0L6mvMod5S619 = _M0L6_2atmpS1652 - _M0L6_2atmpS1653;
      if (_M0L6mvMod5S619 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS610
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS603, _M0L1qS611);
      } else if (_M0L4evenS602) {
        uint64_t _M0L6_2atmpS1646 = _M0L2mvS603 - 1ull;
        uint64_t _M0L6_2atmpS1647;
        uint64_t _M0L6_2atmpS1645;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1647 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS604);
        _M0L6_2atmpS1645 = _M0L6_2atmpS1646 - _M0L6_2atmpS1647;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS609
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1645, _M0L1qS611);
      } else {
        uint64_t _M0L6_2atmpS1648 = _M0Lm2vpS606;
        uint64_t _M0L6_2atmpS1651 = _M0L2mvS603 + 2ull;
        int32_t _M0L6_2atmpS1650;
        uint64_t _M0L6_2atmpS1649;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1650
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1651, _M0L1qS611);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1649 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1650);
        _M0Lm2vpS606 = _M0L6_2atmpS1648 - _M0L6_2atmpS1649;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1680 = _M0Lm2e2S598;
    int32_t _M0L6_2atmpS1679 = -_M0L6_2atmpS1680;
    int32_t _M0L6_2atmpS1674;
    int32_t _M0L6_2atmpS1678;
    int32_t _M0L6_2atmpS1677;
    int32_t _M0L6_2atmpS1676;
    int32_t _M0L6_2atmpS1675;
    int32_t _M0L1qS620;
    int32_t _M0L6_2atmpS1667;
    int32_t _M0L6_2atmpS1673;
    int32_t _M0L6_2atmpS1672;
    int32_t _M0L1iS621;
    int32_t _M0L6_2atmpS1671;
    int32_t _M0L1kS622;
    int32_t _M0L1jS623;
    struct _M0TPB8Pow5Pair _M0L4pow5S624;
    uint64_t _M0L6_2atmpS1670;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS625;
    uint64_t _M0L8_2avrOutS626;
    uint64_t _M0L8_2avpOutS627;
    uint64_t _M0L8_2avmOutS628;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1674 = _M0FPB9log10Pow5(_M0L6_2atmpS1679);
    _M0L6_2atmpS1678 = _M0Lm2e2S598;
    _M0L6_2atmpS1677 = -_M0L6_2atmpS1678;
    _M0L6_2atmpS1676 = _M0L6_2atmpS1677 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1675 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1676);
    _M0L1qS620 = _M0L6_2atmpS1674 - _M0L6_2atmpS1675;
    _M0L6_2atmpS1667 = _M0Lm2e2S598;
    _M0Lm3e10S608 = _M0L1qS620 + _M0L6_2atmpS1667;
    _M0L6_2atmpS1673 = _M0Lm2e2S598;
    _M0L6_2atmpS1672 = -_M0L6_2atmpS1673;
    _M0L1iS621 = _M0L6_2atmpS1672 - _M0L1qS620;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1671 = _M0FPB8pow5bits(_M0L1iS621);
    _M0L1kS622 = _M0L6_2atmpS1671 - 125;
    _M0L1jS623 = _M0L1qS620 - _M0L1kS622;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S624 = _M0FPB19double__computePow5(_M0L1iS621);
    _M0L6_2atmpS1670 = _M0Lm2m2S599;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS625
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1670, _M0L4pow5S624, _M0L1jS623, _M0L7mmShiftS604);
    _M0L8_2avrOutS626 = _M0L7_2abindS625.$0;
    _M0L8_2avpOutS627 = _M0L7_2abindS625.$1;
    _M0L8_2avmOutS628 = _M0L7_2abindS625.$2;
    _M0Lm2vrS605 = _M0L8_2avrOutS626;
    _M0Lm2vpS606 = _M0L8_2avpOutS627;
    _M0Lm2vmS607 = _M0L8_2avmOutS628;
    if (_M0L1qS620 <= 1) {
      _M0Lm17vrIsTrailingZerosS610 = 1;
      if (_M0L4evenS602) {
        int32_t _M0L6_2atmpS1668;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1668 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS604);
        _M0Lm17vmIsTrailingZerosS609 = _M0L6_2atmpS1668 == 1;
      } else {
        uint64_t _M0L6_2atmpS1669 = _M0Lm2vpS606;
        _M0Lm2vpS606 = _M0L6_2atmpS1669 - 1ull;
      }
    } else if (_M0L1qS620 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS610
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS603, _M0L1qS620);
    }
  }
  _M0Lm7removedS629 = 0;
  _M0Lm16lastRemovedDigitS630 = 0;
  _M0Lm6outputS631 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS609 || _M0Lm17vrIsTrailingZerosS610) {
    int32_t _if__result_2313;
    uint64_t _M0L6_2atmpS1710;
    uint64_t _M0L6_2atmpS1716;
    uint64_t _M0L6_2atmpS1717;
    int32_t _if__result_2314;
    int32_t _M0L6_2atmpS1713;
    int64_t _M0L6_2atmpS1712;
    uint64_t _M0L6_2atmpS1711;
    while (1) {
      uint64_t _M0L6_2atmpS1693 = _M0Lm2vpS606;
      uint64_t _M0L7vpDiv10S632 = _M0L6_2atmpS1693 / 10ull;
      uint64_t _M0L6_2atmpS1692 = _M0Lm2vmS607;
      uint64_t _M0L7vmDiv10S633 = _M0L6_2atmpS1692 / 10ull;
      uint64_t _M0L6_2atmpS1691;
      int32_t _M0L6_2atmpS1688;
      int32_t _M0L6_2atmpS1690;
      int32_t _M0L6_2atmpS1689;
      int32_t _M0L7vmMod10S635;
      uint64_t _M0L6_2atmpS1687;
      uint64_t _M0L7vrDiv10S636;
      uint64_t _M0L6_2atmpS1686;
      int32_t _M0L6_2atmpS1683;
      int32_t _M0L6_2atmpS1685;
      int32_t _M0L6_2atmpS1684;
      int32_t _M0L7vrMod10S637;
      int32_t _M0L6_2atmpS1682;
      if (_M0L7vpDiv10S632 <= _M0L7vmDiv10S633) {
        break;
      }
      _M0L6_2atmpS1691 = _M0Lm2vmS607;
      _M0L6_2atmpS1688 = (int32_t)_M0L6_2atmpS1691;
      _M0L6_2atmpS1690 = (int32_t)_M0L7vmDiv10S633;
      _M0L6_2atmpS1689 = 10 * _M0L6_2atmpS1690;
      _M0L7vmMod10S635 = _M0L6_2atmpS1688 - _M0L6_2atmpS1689;
      _M0L6_2atmpS1687 = _M0Lm2vrS605;
      _M0L7vrDiv10S636 = _M0L6_2atmpS1687 / 10ull;
      _M0L6_2atmpS1686 = _M0Lm2vrS605;
      _M0L6_2atmpS1683 = (int32_t)_M0L6_2atmpS1686;
      _M0L6_2atmpS1685 = (int32_t)_M0L7vrDiv10S636;
      _M0L6_2atmpS1684 = 10 * _M0L6_2atmpS1685;
      _M0L7vrMod10S637 = _M0L6_2atmpS1683 - _M0L6_2atmpS1684;
      _M0Lm17vmIsTrailingZerosS609
      = _M0Lm17vmIsTrailingZerosS609 && _M0L7vmMod10S635 == 0;
      if (_M0Lm17vrIsTrailingZerosS610) {
        int32_t _M0L6_2atmpS1681 = _M0Lm16lastRemovedDigitS630;
        _M0Lm17vrIsTrailingZerosS610 = _M0L6_2atmpS1681 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS610 = 0;
      }
      _M0Lm16lastRemovedDigitS630 = _M0L7vrMod10S637;
      _M0Lm2vrS605 = _M0L7vrDiv10S636;
      _M0Lm2vpS606 = _M0L7vpDiv10S632;
      _M0Lm2vmS607 = _M0L7vmDiv10S633;
      _M0L6_2atmpS1682 = _M0Lm7removedS629;
      _M0Lm7removedS629 = _M0L6_2atmpS1682 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS609) {
      while (1) {
        uint64_t _M0L6_2atmpS1706 = _M0Lm2vmS607;
        uint64_t _M0L7vmDiv10S638 = _M0L6_2atmpS1706 / 10ull;
        uint64_t _M0L6_2atmpS1705 = _M0Lm2vmS607;
        int32_t _M0L6_2atmpS1702 = (int32_t)_M0L6_2atmpS1705;
        int32_t _M0L6_2atmpS1704 = (int32_t)_M0L7vmDiv10S638;
        int32_t _M0L6_2atmpS1703 = 10 * _M0L6_2atmpS1704;
        int32_t _M0L7vmMod10S639 = _M0L6_2atmpS1702 - _M0L6_2atmpS1703;
        uint64_t _M0L6_2atmpS1701;
        uint64_t _M0L7vpDiv10S641;
        uint64_t _M0L6_2atmpS1700;
        uint64_t _M0L7vrDiv10S642;
        uint64_t _M0L6_2atmpS1699;
        int32_t _M0L6_2atmpS1696;
        int32_t _M0L6_2atmpS1698;
        int32_t _M0L6_2atmpS1697;
        int32_t _M0L7vrMod10S643;
        int32_t _M0L6_2atmpS1695;
        if (_M0L7vmMod10S639 != 0) {
          break;
        }
        _M0L6_2atmpS1701 = _M0Lm2vpS606;
        _M0L7vpDiv10S641 = _M0L6_2atmpS1701 / 10ull;
        _M0L6_2atmpS1700 = _M0Lm2vrS605;
        _M0L7vrDiv10S642 = _M0L6_2atmpS1700 / 10ull;
        _M0L6_2atmpS1699 = _M0Lm2vrS605;
        _M0L6_2atmpS1696 = (int32_t)_M0L6_2atmpS1699;
        _M0L6_2atmpS1698 = (int32_t)_M0L7vrDiv10S642;
        _M0L6_2atmpS1697 = 10 * _M0L6_2atmpS1698;
        _M0L7vrMod10S643 = _M0L6_2atmpS1696 - _M0L6_2atmpS1697;
        if (_M0Lm17vrIsTrailingZerosS610) {
          int32_t _M0L6_2atmpS1694 = _M0Lm16lastRemovedDigitS630;
          _M0Lm17vrIsTrailingZerosS610 = _M0L6_2atmpS1694 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS610 = 0;
        }
        _M0Lm16lastRemovedDigitS630 = _M0L7vrMod10S643;
        _M0Lm2vrS605 = _M0L7vrDiv10S642;
        _M0Lm2vpS606 = _M0L7vpDiv10S641;
        _M0Lm2vmS607 = _M0L7vmDiv10S638;
        _M0L6_2atmpS1695 = _M0Lm7removedS629;
        _M0Lm7removedS629 = _M0L6_2atmpS1695 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS610) {
      int32_t _M0L6_2atmpS1709 = _M0Lm16lastRemovedDigitS630;
      if (_M0L6_2atmpS1709 == 5) {
        uint64_t _M0L6_2atmpS1708 = _M0Lm2vrS605;
        uint64_t _M0L6_2atmpS1707 = _M0L6_2atmpS1708 % 2ull;
        _if__result_2313 = _M0L6_2atmpS1707 == 0ull;
      } else {
        _if__result_2313 = 0;
      }
    } else {
      _if__result_2313 = 0;
    }
    if (_if__result_2313) {
      _M0Lm16lastRemovedDigitS630 = 4;
    }
    _M0L6_2atmpS1710 = _M0Lm2vrS605;
    _M0L6_2atmpS1716 = _M0Lm2vrS605;
    _M0L6_2atmpS1717 = _M0Lm2vmS607;
    if (_M0L6_2atmpS1716 == _M0L6_2atmpS1717) {
      if (!_M0L4evenS602) {
        _if__result_2314 = 1;
      } else {
        int32_t _M0L6_2atmpS1715 = _M0Lm17vmIsTrailingZerosS609;
        _if__result_2314 = !_M0L6_2atmpS1715;
      }
    } else {
      _if__result_2314 = 0;
    }
    if (_if__result_2314) {
      _M0L6_2atmpS1713 = 1;
    } else {
      int32_t _M0L6_2atmpS1714 = _M0Lm16lastRemovedDigitS630;
      _M0L6_2atmpS1713 = _M0L6_2atmpS1714 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1712 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1713);
    _M0L6_2atmpS1711 = *(uint64_t*)&_M0L6_2atmpS1712;
    _M0Lm6outputS631 = _M0L6_2atmpS1710 + _M0L6_2atmpS1711;
  } else {
    int32_t _M0Lm7roundUpS644 = 0;
    uint64_t _M0L6_2atmpS1738 = _M0Lm2vpS606;
    uint64_t _M0L8vpDiv100S645 = _M0L6_2atmpS1738 / 100ull;
    uint64_t _M0L6_2atmpS1737 = _M0Lm2vmS607;
    uint64_t _M0L8vmDiv100S646 = _M0L6_2atmpS1737 / 100ull;
    uint64_t _M0L6_2atmpS1732;
    uint64_t _M0L6_2atmpS1735;
    uint64_t _M0L6_2atmpS1736;
    int32_t _M0L6_2atmpS1734;
    uint64_t _M0L6_2atmpS1733;
    if (_M0L8vpDiv100S645 > _M0L8vmDiv100S646) {
      uint64_t _M0L6_2atmpS1723 = _M0Lm2vrS605;
      uint64_t _M0L8vrDiv100S647 = _M0L6_2atmpS1723 / 100ull;
      uint64_t _M0L6_2atmpS1722 = _M0Lm2vrS605;
      int32_t _M0L6_2atmpS1719 = (int32_t)_M0L6_2atmpS1722;
      int32_t _M0L6_2atmpS1721 = (int32_t)_M0L8vrDiv100S647;
      int32_t _M0L6_2atmpS1720 = 100 * _M0L6_2atmpS1721;
      int32_t _M0L8vrMod100S648 = _M0L6_2atmpS1719 - _M0L6_2atmpS1720;
      int32_t _M0L6_2atmpS1718;
      _M0Lm7roundUpS644 = _M0L8vrMod100S648 >= 50;
      _M0Lm2vrS605 = _M0L8vrDiv100S647;
      _M0Lm2vpS606 = _M0L8vpDiv100S645;
      _M0Lm2vmS607 = _M0L8vmDiv100S646;
      _M0L6_2atmpS1718 = _M0Lm7removedS629;
      _M0Lm7removedS629 = _M0L6_2atmpS1718 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1731 = _M0Lm2vpS606;
      uint64_t _M0L7vpDiv10S649 = _M0L6_2atmpS1731 / 10ull;
      uint64_t _M0L6_2atmpS1730 = _M0Lm2vmS607;
      uint64_t _M0L7vmDiv10S650 = _M0L6_2atmpS1730 / 10ull;
      uint64_t _M0L6_2atmpS1729;
      uint64_t _M0L7vrDiv10S652;
      uint64_t _M0L6_2atmpS1728;
      int32_t _M0L6_2atmpS1725;
      int32_t _M0L6_2atmpS1727;
      int32_t _M0L6_2atmpS1726;
      int32_t _M0L7vrMod10S653;
      int32_t _M0L6_2atmpS1724;
      if (_M0L7vpDiv10S649 <= _M0L7vmDiv10S650) {
        break;
      }
      _M0L6_2atmpS1729 = _M0Lm2vrS605;
      _M0L7vrDiv10S652 = _M0L6_2atmpS1729 / 10ull;
      _M0L6_2atmpS1728 = _M0Lm2vrS605;
      _M0L6_2atmpS1725 = (int32_t)_M0L6_2atmpS1728;
      _M0L6_2atmpS1727 = (int32_t)_M0L7vrDiv10S652;
      _M0L6_2atmpS1726 = 10 * _M0L6_2atmpS1727;
      _M0L7vrMod10S653 = _M0L6_2atmpS1725 - _M0L6_2atmpS1726;
      _M0Lm7roundUpS644 = _M0L7vrMod10S653 >= 5;
      _M0Lm2vrS605 = _M0L7vrDiv10S652;
      _M0Lm2vpS606 = _M0L7vpDiv10S649;
      _M0Lm2vmS607 = _M0L7vmDiv10S650;
      _M0L6_2atmpS1724 = _M0Lm7removedS629;
      _M0Lm7removedS629 = _M0L6_2atmpS1724 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1732 = _M0Lm2vrS605;
    _M0L6_2atmpS1735 = _M0Lm2vrS605;
    _M0L6_2atmpS1736 = _M0Lm2vmS607;
    _M0L6_2atmpS1734
    = _M0L6_2atmpS1735 == _M0L6_2atmpS1736 || _M0Lm7roundUpS644;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1733 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1734);
    _M0Lm6outputS631 = _M0L6_2atmpS1732 + _M0L6_2atmpS1733;
  }
  _M0L6_2atmpS1740 = _M0Lm3e10S608;
  _M0L6_2atmpS1741 = _M0Lm7removedS629;
  _M0L3expS654 = _M0L6_2atmpS1740 + _M0L6_2atmpS1741;
  _M0L6_2atmpS1739 = _M0Lm6outputS631;
  _block_2316
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2316)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2316->$0 = _M0L6_2atmpS1739;
  _block_2316->$1 = _M0L3expS654;
  return _block_2316;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS597) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS597) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS596) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS596) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS595) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS595) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS594) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS594 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS594 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS594 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS594 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS594 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS594 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS594 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS594 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS594 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS594 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS594 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS594 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS594 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS594 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS594 >= 100ull) {
    return 3;
  }
  if (_M0L1vS594 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS577) {
  int32_t _M0L6_2atmpS1640;
  int32_t _M0L6_2atmpS1639;
  int32_t _M0L4baseS576;
  int32_t _M0L5base2S578;
  int32_t _M0L6offsetS579;
  int32_t _M0L6_2atmpS1638;
  uint64_t _M0L4mul0S580;
  int32_t _M0L6_2atmpS1637;
  int32_t _M0L6_2atmpS1636;
  uint64_t _M0L4mul1S581;
  uint64_t _M0L1mS582;
  struct _M0TPB7Umul128 _M0L7_2abindS583;
  uint64_t _M0L7_2alow1S584;
  uint64_t _M0L8_2ahigh1S585;
  struct _M0TPB7Umul128 _M0L7_2abindS586;
  uint64_t _M0L7_2alow0S587;
  uint64_t _M0L8_2ahigh0S588;
  uint64_t _M0L3sumS589;
  uint64_t _M0Lm5high1S590;
  int32_t _M0L6_2atmpS1634;
  int32_t _M0L6_2atmpS1635;
  int32_t _M0L5deltaS591;
  uint64_t _M0L6_2atmpS1633;
  uint64_t _M0L6_2atmpS1625;
  int32_t _M0L6_2atmpS1632;
  uint32_t _M0L6_2atmpS1629;
  int32_t _M0L6_2atmpS1631;
  int32_t _M0L6_2atmpS1630;
  uint32_t _M0L6_2atmpS1628;
  uint32_t _M0L6_2atmpS1627;
  uint64_t _M0L6_2atmpS1626;
  uint64_t _M0L1aS592;
  uint64_t _M0L6_2atmpS1624;
  uint64_t _M0L1bS593;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1640 = _M0L1iS577 + 26;
  _M0L6_2atmpS1639 = _M0L6_2atmpS1640 - 1;
  _M0L4baseS576 = _M0L6_2atmpS1639 / 26;
  _M0L5base2S578 = _M0L4baseS576 * 26;
  _M0L6offsetS579 = _M0L5base2S578 - _M0L1iS577;
  _M0L6_2atmpS1638 = _M0L4baseS576 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S580
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1638);
  _M0L6_2atmpS1637 = _M0L4baseS576 * 2;
  _M0L6_2atmpS1636 = _M0L6_2atmpS1637 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S581
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1636);
  if (_M0L6offsetS579 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S580, .$1 = _M0L4mul1S581};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS582
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS579);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS583 = _M0FPB7umul128(_M0L1mS582, _M0L4mul1S581);
  _M0L7_2alow1S584 = _M0L7_2abindS583.$0;
  _M0L8_2ahigh1S585 = _M0L7_2abindS583.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS586 = _M0FPB7umul128(_M0L1mS582, _M0L4mul0S580);
  _M0L7_2alow0S587 = _M0L7_2abindS586.$0;
  _M0L8_2ahigh0S588 = _M0L7_2abindS586.$1;
  _M0L3sumS589 = _M0L8_2ahigh0S588 + _M0L7_2alow1S584;
  _M0Lm5high1S590 = _M0L8_2ahigh1S585;
  if (_M0L3sumS589 < _M0L8_2ahigh0S588) {
    uint64_t _M0L6_2atmpS1623 = _M0Lm5high1S590;
    _M0Lm5high1S590 = _M0L6_2atmpS1623 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1634 = _M0FPB8pow5bits(_M0L5base2S578);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1635 = _M0FPB8pow5bits(_M0L1iS577);
  _M0L5deltaS591 = _M0L6_2atmpS1634 - _M0L6_2atmpS1635;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1633
  = _M0FPB13shiftright128(_M0L7_2alow0S587, _M0L3sumS589, _M0L5deltaS591);
  _M0L6_2atmpS1625 = _M0L6_2atmpS1633 + 1ull;
  _M0L6_2atmpS1632 = _M0L1iS577 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1629
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1632);
  _M0L6_2atmpS1631 = _M0L1iS577 % 16;
  _M0L6_2atmpS1630 = _M0L6_2atmpS1631 << 1;
  _M0L6_2atmpS1628 = _M0L6_2atmpS1629 >> (_M0L6_2atmpS1630 & 31);
  _M0L6_2atmpS1627 = _M0L6_2atmpS1628 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1626 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1627);
  _M0L1aS592 = _M0L6_2atmpS1625 + _M0L6_2atmpS1626;
  _M0L6_2atmpS1624 = _M0Lm5high1S590;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS593
  = _M0FPB13shiftright128(_M0L3sumS589, _M0L6_2atmpS1624, _M0L5deltaS591);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS592, .$1 = _M0L1bS593};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS559) {
  int32_t _M0L4baseS558;
  int32_t _M0L5base2S560;
  int32_t _M0L6offsetS561;
  int32_t _M0L6_2atmpS1622;
  uint64_t _M0L4mul0S562;
  int32_t _M0L6_2atmpS1621;
  int32_t _M0L6_2atmpS1620;
  uint64_t _M0L4mul1S563;
  uint64_t _M0L1mS564;
  struct _M0TPB7Umul128 _M0L7_2abindS565;
  uint64_t _M0L7_2alow1S566;
  uint64_t _M0L8_2ahigh1S567;
  struct _M0TPB7Umul128 _M0L7_2abindS568;
  uint64_t _M0L7_2alow0S569;
  uint64_t _M0L8_2ahigh0S570;
  uint64_t _M0L3sumS571;
  uint64_t _M0Lm5high1S572;
  int32_t _M0L6_2atmpS1618;
  int32_t _M0L6_2atmpS1619;
  int32_t _M0L5deltaS573;
  uint64_t _M0L6_2atmpS1610;
  int32_t _M0L6_2atmpS1617;
  uint32_t _M0L6_2atmpS1614;
  int32_t _M0L6_2atmpS1616;
  int32_t _M0L6_2atmpS1615;
  uint32_t _M0L6_2atmpS1613;
  uint32_t _M0L6_2atmpS1612;
  uint64_t _M0L6_2atmpS1611;
  uint64_t _M0L1aS574;
  uint64_t _M0L6_2atmpS1609;
  uint64_t _M0L1bS575;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS558 = _M0L1iS559 / 26;
  _M0L5base2S560 = _M0L4baseS558 * 26;
  _M0L6offsetS561 = _M0L1iS559 - _M0L5base2S560;
  _M0L6_2atmpS1622 = _M0L4baseS558 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S562
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1622);
  _M0L6_2atmpS1621 = _M0L4baseS558 * 2;
  _M0L6_2atmpS1620 = _M0L6_2atmpS1621 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S563
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1620);
  if (_M0L6offsetS561 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S562, .$1 = _M0L4mul1S563};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS564
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS561);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS565 = _M0FPB7umul128(_M0L1mS564, _M0L4mul1S563);
  _M0L7_2alow1S566 = _M0L7_2abindS565.$0;
  _M0L8_2ahigh1S567 = _M0L7_2abindS565.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS568 = _M0FPB7umul128(_M0L1mS564, _M0L4mul0S562);
  _M0L7_2alow0S569 = _M0L7_2abindS568.$0;
  _M0L8_2ahigh0S570 = _M0L7_2abindS568.$1;
  _M0L3sumS571 = _M0L8_2ahigh0S570 + _M0L7_2alow1S566;
  _M0Lm5high1S572 = _M0L8_2ahigh1S567;
  if (_M0L3sumS571 < _M0L8_2ahigh0S570) {
    uint64_t _M0L6_2atmpS1608 = _M0Lm5high1S572;
    _M0Lm5high1S572 = _M0L6_2atmpS1608 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1618 = _M0FPB8pow5bits(_M0L1iS559);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1619 = _M0FPB8pow5bits(_M0L5base2S560);
  _M0L5deltaS573 = _M0L6_2atmpS1618 - _M0L6_2atmpS1619;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1610
  = _M0FPB13shiftright128(_M0L7_2alow0S569, _M0L3sumS571, _M0L5deltaS573);
  _M0L6_2atmpS1617 = _M0L1iS559 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1614
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1617);
  _M0L6_2atmpS1616 = _M0L1iS559 % 16;
  _M0L6_2atmpS1615 = _M0L6_2atmpS1616 << 1;
  _M0L6_2atmpS1613 = _M0L6_2atmpS1614 >> (_M0L6_2atmpS1615 & 31);
  _M0L6_2atmpS1612 = _M0L6_2atmpS1613 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1611 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1612);
  _M0L1aS574 = _M0L6_2atmpS1610 + _M0L6_2atmpS1611;
  _M0L6_2atmpS1609 = _M0Lm5high1S572;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS575
  = _M0FPB13shiftright128(_M0L3sumS571, _M0L6_2atmpS1609, _M0L5deltaS573);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS574, .$1 = _M0L1bS575};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS532,
  struct _M0TPB8Pow5Pair _M0L3mulS529,
  int32_t _M0L1jS545,
  int32_t _M0L7mmShiftS547
) {
  uint64_t _M0L7_2amul0S528;
  uint64_t _M0L7_2amul1S530;
  uint64_t _M0L1mS531;
  struct _M0TPB7Umul128 _M0L7_2abindS533;
  uint64_t _M0L5_2aloS534;
  uint64_t _M0L6_2atmpS535;
  struct _M0TPB7Umul128 _M0L7_2abindS536;
  uint64_t _M0L6_2alo2S537;
  uint64_t _M0L6_2ahi2S538;
  uint64_t _M0L3midS539;
  uint64_t _M0L6_2atmpS1607;
  uint64_t _M0L2hiS540;
  uint64_t _M0L3lo2S541;
  uint64_t _M0L6_2atmpS1605;
  uint64_t _M0L6_2atmpS1606;
  uint64_t _M0L4mid2S542;
  uint64_t _M0L6_2atmpS1604;
  uint64_t _M0L3hi2S543;
  int32_t _M0L6_2atmpS1603;
  int32_t _M0L6_2atmpS1602;
  uint64_t _M0L2vpS544;
  uint64_t _M0Lm2vmS546;
  int32_t _M0L6_2atmpS1601;
  int32_t _M0L6_2atmpS1600;
  uint64_t _M0L2vrS557;
  uint64_t _M0L6_2atmpS1599;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S528 = _M0L3mulS529.$0;
  _M0L7_2amul1S530 = _M0L3mulS529.$1;
  _M0L1mS531 = _M0L1mS532 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS533 = _M0FPB7umul128(_M0L1mS531, _M0L7_2amul0S528);
  _M0L5_2aloS534 = _M0L7_2abindS533.$0;
  _M0L6_2atmpS535 = _M0L7_2abindS533.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS536 = _M0FPB7umul128(_M0L1mS531, _M0L7_2amul1S530);
  _M0L6_2alo2S537 = _M0L7_2abindS536.$0;
  _M0L6_2ahi2S538 = _M0L7_2abindS536.$1;
  _M0L3midS539 = _M0L6_2atmpS535 + _M0L6_2alo2S537;
  if (_M0L3midS539 < _M0L6_2atmpS535) {
    _M0L6_2atmpS1607 = 1ull;
  } else {
    _M0L6_2atmpS1607 = 0ull;
  }
  _M0L2hiS540 = _M0L6_2ahi2S538 + _M0L6_2atmpS1607;
  _M0L3lo2S541 = _M0L5_2aloS534 + _M0L7_2amul0S528;
  _M0L6_2atmpS1605 = _M0L3midS539 + _M0L7_2amul1S530;
  if (_M0L3lo2S541 < _M0L5_2aloS534) {
    _M0L6_2atmpS1606 = 1ull;
  } else {
    _M0L6_2atmpS1606 = 0ull;
  }
  _M0L4mid2S542 = _M0L6_2atmpS1605 + _M0L6_2atmpS1606;
  if (_M0L4mid2S542 < _M0L3midS539) {
    _M0L6_2atmpS1604 = 1ull;
  } else {
    _M0L6_2atmpS1604 = 0ull;
  }
  _M0L3hi2S543 = _M0L2hiS540 + _M0L6_2atmpS1604;
  _M0L6_2atmpS1603 = _M0L1jS545 - 64;
  _M0L6_2atmpS1602 = _M0L6_2atmpS1603 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS544
  = _M0FPB13shiftright128(_M0L4mid2S542, _M0L3hi2S543, _M0L6_2atmpS1602);
  _M0Lm2vmS546 = 0ull;
  if (_M0L7mmShiftS547) {
    uint64_t _M0L3lo3S548 = _M0L5_2aloS534 - _M0L7_2amul0S528;
    uint64_t _M0L6_2atmpS1589 = _M0L3midS539 - _M0L7_2amul1S530;
    uint64_t _M0L6_2atmpS1590;
    uint64_t _M0L4mid3S549;
    uint64_t _M0L6_2atmpS1588;
    uint64_t _M0L3hi3S550;
    int32_t _M0L6_2atmpS1587;
    int32_t _M0L6_2atmpS1586;
    if (_M0L5_2aloS534 < _M0L3lo3S548) {
      _M0L6_2atmpS1590 = 1ull;
    } else {
      _M0L6_2atmpS1590 = 0ull;
    }
    _M0L4mid3S549 = _M0L6_2atmpS1589 - _M0L6_2atmpS1590;
    if (_M0L3midS539 < _M0L4mid3S549) {
      _M0L6_2atmpS1588 = 1ull;
    } else {
      _M0L6_2atmpS1588 = 0ull;
    }
    _M0L3hi3S550 = _M0L2hiS540 - _M0L6_2atmpS1588;
    _M0L6_2atmpS1587 = _M0L1jS545 - 64;
    _M0L6_2atmpS1586 = _M0L6_2atmpS1587 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS546
    = _M0FPB13shiftright128(_M0L4mid3S549, _M0L3hi3S550, _M0L6_2atmpS1586);
  } else {
    uint64_t _M0L3lo3S551 = _M0L5_2aloS534 + _M0L5_2aloS534;
    uint64_t _M0L6_2atmpS1597 = _M0L3midS539 + _M0L3midS539;
    uint64_t _M0L6_2atmpS1598;
    uint64_t _M0L4mid3S552;
    uint64_t _M0L6_2atmpS1595;
    uint64_t _M0L6_2atmpS1596;
    uint64_t _M0L3hi3S553;
    uint64_t _M0L3lo4S554;
    uint64_t _M0L6_2atmpS1593;
    uint64_t _M0L6_2atmpS1594;
    uint64_t _M0L4mid4S555;
    uint64_t _M0L6_2atmpS1592;
    uint64_t _M0L3hi4S556;
    int32_t _M0L6_2atmpS1591;
    if (_M0L3lo3S551 < _M0L5_2aloS534) {
      _M0L6_2atmpS1598 = 1ull;
    } else {
      _M0L6_2atmpS1598 = 0ull;
    }
    _M0L4mid3S552 = _M0L6_2atmpS1597 + _M0L6_2atmpS1598;
    _M0L6_2atmpS1595 = _M0L2hiS540 + _M0L2hiS540;
    if (_M0L4mid3S552 < _M0L3midS539) {
      _M0L6_2atmpS1596 = 1ull;
    } else {
      _M0L6_2atmpS1596 = 0ull;
    }
    _M0L3hi3S553 = _M0L6_2atmpS1595 + _M0L6_2atmpS1596;
    _M0L3lo4S554 = _M0L3lo3S551 - _M0L7_2amul0S528;
    _M0L6_2atmpS1593 = _M0L4mid3S552 - _M0L7_2amul1S530;
    if (_M0L3lo3S551 < _M0L3lo4S554) {
      _M0L6_2atmpS1594 = 1ull;
    } else {
      _M0L6_2atmpS1594 = 0ull;
    }
    _M0L4mid4S555 = _M0L6_2atmpS1593 - _M0L6_2atmpS1594;
    if (_M0L4mid3S552 < _M0L4mid4S555) {
      _M0L6_2atmpS1592 = 1ull;
    } else {
      _M0L6_2atmpS1592 = 0ull;
    }
    _M0L3hi4S556 = _M0L3hi3S553 - _M0L6_2atmpS1592;
    _M0L6_2atmpS1591 = _M0L1jS545 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS546
    = _M0FPB13shiftright128(_M0L4mid4S555, _M0L3hi4S556, _M0L6_2atmpS1591);
  }
  _M0L6_2atmpS1601 = _M0L1jS545 - 64;
  _M0L6_2atmpS1600 = _M0L6_2atmpS1601 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS557
  = _M0FPB13shiftright128(_M0L3midS539, _M0L2hiS540, _M0L6_2atmpS1600);
  _M0L6_2atmpS1599 = _M0Lm2vmS546;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS557,
                                                .$1 = _M0L2vpS544,
                                                .$2 = _M0L6_2atmpS1599};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS526,
  int32_t _M0L1pS527
) {
  uint64_t _M0L6_2atmpS1585;
  uint64_t _M0L6_2atmpS1584;
  uint64_t _M0L6_2atmpS1583;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1585 = 1ull << (_M0L1pS527 & 63);
  _M0L6_2atmpS1584 = _M0L6_2atmpS1585 - 1ull;
  _M0L6_2atmpS1583 = _M0L5valueS526 & _M0L6_2atmpS1584;
  return _M0L6_2atmpS1583 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS524,
  int32_t _M0L1pS525
) {
  int32_t _M0L6_2atmpS1582;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1582 = _M0FPB10pow5Factor(_M0L5valueS524);
  return _M0L6_2atmpS1582 >= _M0L1pS525;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS519) {
  uint64_t _M0L6_2atmpS1573;
  uint64_t _M0L6_2atmpS1574;
  uint64_t _M0L6_2atmpS1575;
  uint64_t _M0L6_2atmpS1576;
  uint64_t _M0L6_2atmpS1581;
  int32_t _M0L5countS520;
  uint64_t _M0L1vS521;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1573 = _M0L5valueS519 % 5ull;
  if (_M0L6_2atmpS1573 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1574 = _M0L5valueS519 % 25ull;
  if (_M0L6_2atmpS1574 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1575 = _M0L5valueS519 % 125ull;
  if (_M0L6_2atmpS1575 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1576 = _M0L5valueS519 % 625ull;
  if (_M0L6_2atmpS1576 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1581 = _M0L5valueS519 / 625ull;
  _M0L5countS520 = 4;
  _M0L1vS521 = _M0L6_2atmpS1581;
  while (1) {
    if (_M0L1vS521 > 0ull) {
      uint64_t _M0L6_2atmpS1577 = _M0L1vS521 % 5ull;
      int32_t _M0L6_2atmpS1578;
      uint64_t _M0L6_2atmpS1579;
      if (_M0L6_2atmpS1577 != 0ull) {
        return _M0L5countS520;
      }
      _M0L6_2atmpS1578 = _M0L5countS520 + 1;
      _M0L6_2atmpS1579 = _M0L1vS521 / 5ull;
      _M0L5countS520 = _M0L6_2atmpS1578;
      _M0L1vS521 = _M0L6_2atmpS1579;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS523;
      moonbit_string_t _M0L6_2atmpS1580;
      int32_t _result_2318;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS523
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS523, (moonbit_string_t)moonbit_string_literal_12.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS523, _M0L5valueS519);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1580
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS523);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS523);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2318 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1580);
      moonbit_decref_cycle_free(_M0L6_2atmpS1580);
      return _result_2318;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS518,
  uint64_t _M0L2hiS516,
  int32_t _M0L4distS517
) {
  int32_t _M0L6_2atmpS1572;
  uint64_t _M0L6_2atmpS1570;
  uint64_t _M0L6_2atmpS1571;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1572 = 64 - _M0L4distS517;
  _M0L6_2atmpS1570 = _M0L2hiS516 << (_M0L6_2atmpS1572 & 63);
  _M0L6_2atmpS1571 = _M0L2loS518 >> (_M0L4distS517 & 63);
  return _M0L6_2atmpS1570 | _M0L6_2atmpS1571;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS506,
  uint64_t _M0L1bS509
) {
  uint64_t _M0L3aLoS505;
  uint64_t _M0L3aHiS507;
  uint64_t _M0L3bLoS508;
  uint64_t _M0L3bHiS510;
  uint64_t _M0L1xS511;
  uint64_t _M0L6_2atmpS1568;
  uint64_t _M0L6_2atmpS1569;
  uint64_t _M0L1yS512;
  uint64_t _M0L6_2atmpS1566;
  uint64_t _M0L6_2atmpS1567;
  uint64_t _M0L1zS513;
  uint64_t _M0L6_2atmpS1564;
  uint64_t _M0L6_2atmpS1565;
  uint64_t _M0L6_2atmpS1562;
  uint64_t _M0L6_2atmpS1563;
  uint64_t _M0L1wS514;
  uint64_t _M0L2loS515;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS505 = _M0L1aS506 & 4294967295ull;
  _M0L3aHiS507 = _M0L1aS506 >> 32;
  _M0L3bLoS508 = _M0L1bS509 & 4294967295ull;
  _M0L3bHiS510 = _M0L1bS509 >> 32;
  _M0L1xS511 = _M0L3aLoS505 * _M0L3bLoS508;
  _M0L6_2atmpS1568 = _M0L3aHiS507 * _M0L3bLoS508;
  _M0L6_2atmpS1569 = _M0L1xS511 >> 32;
  _M0L1yS512 = _M0L6_2atmpS1568 + _M0L6_2atmpS1569;
  _M0L6_2atmpS1566 = _M0L3aLoS505 * _M0L3bHiS510;
  _M0L6_2atmpS1567 = _M0L1yS512 & 4294967295ull;
  _M0L1zS513 = _M0L6_2atmpS1566 + _M0L6_2atmpS1567;
  _M0L6_2atmpS1564 = _M0L3aHiS507 * _M0L3bHiS510;
  _M0L6_2atmpS1565 = _M0L1yS512 >> 32;
  _M0L6_2atmpS1562 = _M0L6_2atmpS1564 + _M0L6_2atmpS1565;
  _M0L6_2atmpS1563 = _M0L1zS513 >> 32;
  _M0L1wS514 = _M0L6_2atmpS1562 + _M0L6_2atmpS1563;
  _M0L2loS515 = _M0L1aS506 * _M0L1bS509;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS515, .$1 = _M0L1wS514};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS503,
  int32_t _M0L4fromS500,
  int32_t _M0L2toS499
) {
  int32_t _M0L3lenS498;
  int32_t _M0L6_2atmpS1561;
  uint16_t* _M0L6bufferS501;
  int32_t _M0L1iS502;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS498 = _M0L2toS499 - _M0L4fromS500;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1561 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS501
  = (uint16_t*)moonbit_make_string(_M0L3lenS498, _M0L6_2atmpS1561);
  _M0L1iS502 = 0;
  while (1) {
    if (_M0L1iS502 < _M0L3lenS498) {
      int32_t _M0L6_2atmpS1559 = _M0L4fromS500 + _M0L1iS502;
      int32_t _M0L6_2atmpS1558;
      int32_t _M0L6_2atmpS1557;
      int32_t _M0L6_2atmpS1560;
      if (
        _M0L6_2atmpS1559 < 0
        || _M0L6_2atmpS1559 >= Moonbit_array_length(_M0L5bytesS503)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1558 = (int32_t)_M0L5bytesS503[_M0L6_2atmpS1559];
      _M0L6_2atmpS1557 = (uint16_t)_M0L6_2atmpS1558;
      if (
        _M0L1iS502 < 0 || _M0L1iS502 >= Moonbit_array_length(_M0L6bufferS501)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS501[_M0L1iS502] = _M0L6_2atmpS1557;
      _M0L6_2atmpS1560 = _M0L1iS502 + 1;
      _M0L1iS502 = _M0L6_2atmpS1560;
      continue;
    }
    break;
  }
  return _M0L6bufferS501;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS497) {
  int32_t _M0L6_2atmpS1556;
  uint32_t _M0L6_2atmpS1555;
  uint32_t _M0L6_2atmpS1554;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1556 = _M0L1eS497 * 78913;
  _M0L6_2atmpS1555 = *(uint32_t*)&_M0L6_2atmpS1556;
  _M0L6_2atmpS1554 = _M0L6_2atmpS1555 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1554;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS496) {
  int32_t _M0L6_2atmpS1553;
  uint32_t _M0L6_2atmpS1552;
  uint32_t _M0L6_2atmpS1551;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1553 = _M0L1eS496 * 732923;
  _M0L6_2atmpS1552 = *(uint32_t*)&_M0L6_2atmpS1553;
  _M0L6_2atmpS1551 = _M0L6_2atmpS1552 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1551;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS494,
  int32_t _M0L8exponentS495,
  int32_t _M0L8mantissaS492
) {
  moonbit_string_t _M0L1sS493;
  moonbit_string_t _result_2321;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS492) {
    return (moonbit_string_t)moonbit_string_literal_13.data;
  }
  if (_M0L4signS494) {
    _M0L1sS493 = (moonbit_string_t)moonbit_string_literal_14.data;
  } else {
    _M0L1sS493 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS495) {
    moonbit_string_t _result_2320;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2320
    = moonbit_add_string(_M0L1sS493, (moonbit_string_t)moonbit_string_literal_15.data);
    moonbit_decref_cycle_free(_M0L1sS493);
    return _result_2320;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2321
  = moonbit_add_string(_M0L1sS493, (moonbit_string_t)moonbit_string_literal_16.data);
  moonbit_decref_cycle_free(_M0L1sS493);
  return _result_2321;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS491) {
  int32_t _M0L6_2atmpS1550;
  uint32_t _M0L6_2atmpS1549;
  uint32_t _M0L6_2atmpS1548;
  int32_t _M0L6_2atmpS1547;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1550 = _M0L1eS491 * 1217359;
  _M0L6_2atmpS1549 = *(uint32_t*)&_M0L6_2atmpS1550;
  _M0L6_2atmpS1548 = _M0L6_2atmpS1549 >> 19;
  _M0L6_2atmpS1547 = *(int32_t*)&_M0L6_2atmpS1548;
  return _M0L6_2atmpS1547 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS490) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS490 != _M0L4selfS490) {
    return 0;
  } else if (_M0L4selfS490 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS490 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS490;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS489) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS489 != _M0L4selfS489) {
    return 0ll;
  } else if (_M0L4selfS489 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS489 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS489;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS486
) {
  float* _M0L6_2atmpS1544;
  struct _M0TPB5ArrayGfE* _block_2322;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1544 = (float*)moonbit_make_float_array_raw(_M0L3lenS486);
  _block_2322
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2322)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 50, 0);
  _block_2322->$0 = _M0L6_2atmpS1544;
  _block_2322->$1 = _M0L3lenS486;
  return _block_2322;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS487
) {
  uint8_t* _M0L6_2atmpS1545;
  struct _M0TPB5ArrayGbE* _block_2323;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1545 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS487);
  _block_2323
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2323)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 53, 0);
  _block_2323->$0 = _M0L6_2atmpS1545;
  _block_2323->$1 = _M0L3lenS487;
  return _block_2323;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS488
) {
  int32_t* _M0L6_2atmpS1546;
  struct _M0TPB5ArrayGiE* _block_2324;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1546 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS488);
  _block_2324
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2324)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 56, 0);
  _block_2324->$0 = _M0L6_2atmpS1546;
  _block_2324->$1 = _M0L3lenS488;
  return _block_2324;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS482,
  int32_t _M0L5indexS483
) {
  uint64_t* _M0L6_2atmpS1542;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1542 = _M0L4selfS482;
  if (
    _M0L5indexS483 < 0
    || _M0L5indexS483 >= Moonbit_array_length(_M0L6_2atmpS1542)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1542[_M0L5indexS483];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS484,
  int32_t _M0L5indexS485
) {
  uint32_t* _M0L6_2atmpS1543;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1543 = _M0L4selfS484;
  if (
    _M0L5indexS485 < 0
    || _M0L5indexS485 >= Moonbit_array_length(_M0L6_2atmpS1543)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1543[_M0L5indexS485];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS481
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS481, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS480) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS480, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS479) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS479;
}

int32_t _M0MPC15array5Array4pushGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS470,
  int32_t _M0L5valueS472
) {
  int32_t _M0L3lenS1521;
  uint8_t* _M0L6_2atmpS1523;
  int32_t _M0L6_2atmpS1522;
  int32_t _M0L6lengthS471;
  uint8_t* _M0L3bufS1526;
  int32_t _M0L6_2atmpS1527;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1521 = _M0L4selfS470->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1523 = _M0MPC15array5Array6bufferGbE(_M0L4selfS470);
  _M0L6_2atmpS1522 = Moonbit_array_length(_M0L6_2atmpS1523);
  moonbit_decref_cycle_free(_M0L6_2atmpS1523);
  if (_M0L3lenS1521 == _M0L6_2atmpS1522) {
    int32_t _M0L3lenS1525 = _M0L4selfS470->$1;
    int32_t _M0L6_2atmpS1524 = _M0L3lenS1525 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGbE(_M0L4selfS470, _M0L6_2atmpS1524);
  }
  _M0L6lengthS471 = _M0L4selfS470->$1;
  _M0L3bufS1526 = _M0L4selfS470->$0;
  _M0L3bufS1526[_M0L6lengthS471] = _M0L5valueS472;
  _M0L6_2atmpS1527 = _M0L6lengthS471 + 1;
  _M0L4selfS470->$1 = _M0L6_2atmpS1527;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS473,
  moonbit_string_t _M0L5valueS475
) {
  int32_t _M0L3lenS1528;
  moonbit_string_t* _M0L6_2atmpS1530;
  int32_t _M0L6_2atmpS1529;
  int32_t _M0L6lengthS474;
  moonbit_string_t* _M0L3bufS1533;
  moonbit_string_t _M0L6_2aoldS2220;
  int32_t _M0L6_2atmpS1534;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1528 = _M0L4selfS473->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1530 = _M0MPC15array5Array6bufferGsE(_M0L4selfS473);
  _M0L6_2atmpS1529 = Moonbit_array_length(_M0L6_2atmpS1530);
  moonbit_decref_cycle_free(_M0L6_2atmpS1530);
  if (_M0L3lenS1528 == _M0L6_2atmpS1529) {
    int32_t _M0L3lenS1532 = _M0L4selfS473->$1;
    int32_t _M0L6_2atmpS1531 = _M0L3lenS1532 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS473, _M0L6_2atmpS1531);
  }
  _M0L6lengthS474 = _M0L4selfS473->$1;
  _M0L3bufS1533 = _M0L4selfS473->$0;
  _M0L6_2aoldS2220 = (moonbit_string_t)_M0L3bufS1533[_M0L6lengthS474];
  moonbit_decref_cycle_free(_M0L6_2aoldS2220);
  _M0L3bufS1533[_M0L6lengthS474] = _M0L5valueS475;
  _M0L6_2atmpS1534 = _M0L6lengthS474 + 1;
  _M0L4selfS473->$1 = _M0L6_2atmpS1534;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS476,
  struct _M0TUsiE* _M0L5valueS478
) {
  int32_t _M0L3lenS1535;
  struct _M0TUsiE** _M0L6_2atmpS1537;
  int32_t _M0L6_2atmpS1536;
  int32_t _M0L6lengthS477;
  struct _M0TUsiE** _M0L3bufS1540;
  struct _M0TUsiE* _M0L6_2aoldS2221;
  int32_t _M0L6_2atmpS1541;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1535 = _M0L4selfS476->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1537 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS476);
  _M0L6_2atmpS1536 = Moonbit_array_length(_M0L6_2atmpS1537);
  moonbit_decref_cycle_free(_M0L6_2atmpS1537);
  if (_M0L3lenS1535 == _M0L6_2atmpS1536) {
    int32_t _M0L3lenS1539 = _M0L4selfS476->$1;
    int32_t _M0L6_2atmpS1538 = _M0L3lenS1539 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS476, _M0L6_2atmpS1538);
  }
  _M0L6lengthS477 = _M0L4selfS476->$1;
  _M0L3bufS1540 = _M0L4selfS476->$0;
  _M0L6_2aoldS2221 = (struct _M0TUsiE*)_M0L3bufS1540[_M0L6lengthS477];
  if (_M0L6_2aoldS2221) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2221);
  }
  _M0L3bufS1540[_M0L6lengthS477] = _M0L5valueS478;
  _M0L6_2atmpS1541 = _M0L6lengthS477 + 1;
  _M0L4selfS476->$1 = _M0L6_2atmpS1541;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS459,
  int32_t _M0L8requiredS461
) {
  int32_t _M0L8old__capS458;
  int32_t _M0L3lenS1518;
  int32_t _M0L8new__capS460;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS458 = _M0MPC15array5Array8capacityGbE(_M0L4selfS459);
  _M0L3lenS1518 = _M0L4selfS459->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS460
  = _M0FPB23array__growth__capacity(_M0L8old__capS458, _M0L3lenS1518, _M0L8requiredS461);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGbE(_M0L4selfS459, _M0L8new__capS460);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS463,
  int32_t _M0L8requiredS465
) {
  int32_t _M0L8old__capS462;
  int32_t _M0L3lenS1519;
  int32_t _M0L8new__capS464;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS462 = _M0MPC15array5Array8capacityGsE(_M0L4selfS463);
  _M0L3lenS1519 = _M0L4selfS463->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS464
  = _M0FPB23array__growth__capacity(_M0L8old__capS462, _M0L3lenS1519, _M0L8requiredS465);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS463, _M0L8new__capS464);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS467,
  int32_t _M0L8requiredS469
) {
  int32_t _M0L8old__capS466;
  int32_t _M0L3lenS1520;
  int32_t _M0L8new__capS468;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS466 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS467);
  _M0L3lenS1520 = _M0L4selfS467->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS468
  = _M0FPB23array__growth__capacity(_M0L8old__capS466, _M0L3lenS1520, _M0L8requiredS469);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS467, _M0L8new__capS468);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS441,
  int32_t _M0L13new__capacityS444
) {
  uint8_t* _M0L8old__bufS440;
  int32_t _M0L3lenS442;
  int32_t _M0L9copy__lenS443;
  uint8_t* _M0L8new__bufS445;
  uint8_t* _M0L6_2aoldS2222;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS440 = _M0L4selfS441->$0;
  _M0L3lenS442 = _M0L4selfS441->$1;
  if (_M0L3lenS442 < _M0L13new__capacityS444) {
    _M0L9copy__lenS443 = _M0L3lenS442;
  } else {
    _M0L9copy__lenS443 = _M0L13new__capacityS444;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS440);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS445
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGbE(_M0L8old__bufS440, _M0L13new__capacityS444, _M0L9copy__lenS443, 0, 0);
  _M0L6_2aoldS2222 = _M0L4selfS441->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2222);
  _M0L4selfS441->$0 = _M0L8new__bufS445;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS447,
  int32_t _M0L13new__capacityS450
) {
  moonbit_string_t* _M0L8old__bufS446;
  int32_t _M0L3lenS448;
  int32_t _M0L9copy__lenS449;
  moonbit_string_t* _M0L8new__bufS451;
  moonbit_string_t* _M0L6_2aoldS2223;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS446 = _M0L4selfS447->$0;
  _M0L3lenS448 = _M0L4selfS447->$1;
  if (_M0L3lenS448 < _M0L13new__capacityS450) {
    _M0L9copy__lenS449 = _M0L3lenS448;
  } else {
    _M0L9copy__lenS449 = _M0L13new__capacityS450;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS446);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS451
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS446, _M0L13new__capacityS450, _M0L9copy__lenS449, 0, 0);
  _M0L6_2aoldS2223 = _M0L4selfS447->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2223);
  _M0L4selfS447->$0 = _M0L8new__bufS451;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS453,
  int32_t _M0L13new__capacityS456
) {
  struct _M0TUsiE** _M0L8old__bufS452;
  int32_t _M0L3lenS454;
  int32_t _M0L9copy__lenS455;
  struct _M0TUsiE** _M0L8new__bufS457;
  struct _M0TUsiE** _M0L6_2aoldS2224;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS452 = _M0L4selfS453->$0;
  _M0L3lenS454 = _M0L4selfS453->$1;
  if (_M0L3lenS454 < _M0L13new__capacityS456) {
    _M0L9copy__lenS455 = _M0L3lenS454;
  } else {
    _M0L9copy__lenS455 = _M0L13new__capacityS456;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS452);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS457
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS452, _M0L13new__capacityS456, _M0L9copy__lenS455, 0, 0);
  _M0L6_2aoldS2224 = _M0L4selfS453->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2224);
  _M0L4selfS453->$0 = _M0L8new__bufS457;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS437
) {
  uint8_t* _M0L6_2atmpS1515;
  int32_t _result_2325;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1515 = _M0MPC15array5Array6bufferGbE(_M0L4selfS437);
  _result_2325 = Moonbit_array_length(_M0L6_2atmpS1515);
  moonbit_decref_cycle_free(_M0L6_2atmpS1515);
  return _result_2325;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS438
) {
  moonbit_string_t* _M0L6_2atmpS1516;
  int32_t _result_2326;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1516 = _M0MPC15array5Array6bufferGsE(_M0L4selfS438);
  _result_2326 = Moonbit_array_length(_M0L6_2atmpS1516);
  moonbit_decref_cycle_free(_M0L6_2atmpS1516);
  return _result_2326;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS439
) {
  struct _M0TUsiE** _M0L6_2atmpS1517;
  int32_t _result_2327;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1517 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS439);
  _result_2327 = Moonbit_array_length(_M0L6_2atmpS1517);
  moonbit_decref_cycle_free(_M0L6_2atmpS1517);
  return _result_2327;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS433,
  int32_t _M0L3lenS431,
  int32_t _M0L8requiredS430
) {
  int32_t _M0L5startS432;
  int32_t _M0L5spaceS434;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS430 < _M0L3lenS431) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_17.data);
  }
  if (_M0L7currentS433 == 0) {
    _M0L5startS432 = 8;
  } else {
    _M0L5startS432 = _M0L7currentS433;
  }
  _M0L5spaceS434 = _M0L5startS432;
  while (1) {
    if (_M0L5spaceS434 < _M0L8requiredS430) {
      int32_t _M0L4nextS435 = _M0L5spaceS434 * 2;
      if (_M0L4nextS435 <= _M0L5spaceS434) {
        return _M0L8requiredS430;
      }
      _M0L5spaceS434 = _M0L4nextS435;
      continue;
    } else {
      return _M0L5spaceS434;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE* _M0L4selfS429) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS429->$1;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS424) {
  uint8_t* _M0L8_2afieldS2225;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2225 = _M0L4selfS424->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2225);
  return _M0L8_2afieldS2225;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS425) {
  float* _M0L8_2afieldS2226;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2226 = _M0L4selfS425->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2226);
  return _M0L8_2afieldS2226;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS426) {
  int32_t* _M0L8_2afieldS2227;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2227 = _M0L4selfS426->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2227);
  return _M0L8_2afieldS2227;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS427
) {
  moonbit_string_t* _M0L8_2afieldS2228;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2228 = _M0L4selfS427->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2228);
  return _M0L8_2afieldS2228;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS428
) {
  struct _M0TUsiE** _M0L8_2afieldS2229;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2229 = _M0L4selfS428->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2229);
  return _M0L8_2afieldS2229;
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
  int32_t _M0L3endS1513;
  int32_t _M0L5startS1514;
  int32_t _M0L8str__lenS419;
  int32_t _M0L3lenS1512;
  int32_t _M0L8requiredS421;
  uint16_t* _M0L4dataS1505;
  int32_t _M0L6_2atmpS1504;
  int32_t _if__result_2329;
  uint16_t* _M0L4dataS1506;
  int32_t _M0L3lenS1507;
  moonbit_string_t _M0L6_2atmpS1508;
  int32_t _M0L6_2atmpS1509;
  int32_t _M0L3lenS1511;
  int32_t _M0L6_2atmpS1510;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1513 = _M0L3strS420.$2;
  _M0L5startS1514 = _M0L3strS420.$1;
  _M0L8str__lenS419 = _M0L3endS1513 - _M0L5startS1514;
  if (_M0L8str__lenS419 == 0) {
    return 0;
  }
  _M0L3lenS1512 = _M0L4selfS422->$1;
  _M0L8requiredS421 = _M0L3lenS1512 + _M0L8str__lenS419;
  _M0L4dataS1505 = _M0L4selfS422->$0;
  _M0L6_2atmpS1504 = Moonbit_array_length(_M0L4dataS1505);
  if (_M0L8requiredS421 > _M0L6_2atmpS1504) {
    _if__result_2329 = 1;
  } else {
    int32_t _M0L3lenS1503 = _M0L4selfS422->$1;
    _if__result_2329 = _M0L8requiredS421 < _M0L3lenS1503;
  }
  if (_if__result_2329) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS422, _M0L8requiredS421);
  }
  _M0L4dataS1506 = _M0L4selfS422->$0;
  _M0L3lenS1507 = _M0L4selfS422->$1;
  moonbit_incref_cycle_free(_M0L4dataS1506);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1508 = _M0MPC16string10StringView4data(_M0L3strS420);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1509 = _M0MPC16string10StringView13start__offset(_M0L3strS420);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1506, _M0L3lenS1507, _M0L6_2atmpS1508, _M0L6_2atmpS1509, _M0L8str__lenS419);
  moonbit_decref_cycle_free(_M0L4dataS1506);
  moonbit_decref_cycle_free(_M0L6_2atmpS1508);
  _M0L3lenS1511 = _M0L4selfS422->$1;
  _M0L6_2atmpS1510 = _M0L3lenS1511 + _M0L8str__lenS419;
  _M0L4selfS422->$1 = _M0L6_2atmpS1510;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS416,
  int32_t _M0L5startS414,
  int32_t _M0L3endS415
) {
  int32_t _if__result_2330;
  int32_t _M0L3lenS417;
  int32_t _M0L6_2atmpS1502;
  moonbit_bytes_t _M0L5bytesS418;
  moonbit_bytes_t _M0L6_2atmpS1501;
  moonbit_string_t _result_2331;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS414 == 0) {
    int32_t _M0L6_2atmpS1500 = Moonbit_array_length(_M0L3strS416);
    _if__result_2330 = _M0L3endS415 == _M0L6_2atmpS1500;
  } else {
    _if__result_2330 = 0;
  }
  if (_if__result_2330) {
    moonbit_incref_cycle_free(_M0L3strS416);
    return _M0L3strS416;
  }
  _M0L3lenS417 = _M0L3endS415 - _M0L5startS414;
  _M0L6_2atmpS1502 = _M0L3lenS417 * 2;
  _M0L5bytesS418 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1502, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS418, 0, _M0L3strS416, _M0L5startS414, _M0L3lenS417);
  _M0L6_2atmpS1501 = _M0L5bytesS418;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2331
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1501, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1501);
  return _result_2331;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS409,
  int32_t _M0L6offsetS413,
  int64_t _M0L6lengthS411
) {
  int32_t _M0L3lenS408;
  int32_t _M0L6lengthS410;
  int32_t _if__result_2332;
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
      int32_t _M0L6_2atmpS1499 = _M0L6offsetS413 + _M0L6lengthS410;
      _if__result_2332 = _M0L6_2atmpS1499 <= _M0L3lenS408;
    } else {
      _if__result_2332 = 0;
    }
  } else {
    _if__result_2332 = 0;
  }
  if (_if__result_2332) {
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
  int32_t _M0L6_2atmpS1498;
  int32_t _M0L6_2atmpS1497;
  int32_t _M0L2e1S394;
  int32_t _M0L6_2atmpS1496;
  int32_t _M0L2e2S397;
  int32_t _M0L4len1S399;
  int32_t _M0L4len2S401;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1498 = _M0L6lengthS396 * 2;
  _M0L6_2atmpS1497 = _M0L13bytes__offsetS395 + _M0L6_2atmpS1498;
  _M0L2e1S394 = _M0L6_2atmpS1497 - 1;
  _M0L6_2atmpS1496 = _M0L11str__offsetS398 + _M0L6lengthS396;
  _M0L2e2S397 = _M0L6_2atmpS1496 - 1;
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
        int32_t _M0L6_2atmpS1493 = _M0L3strS402[_M0L1iS404];
        int32_t _M0L6_2atmpS1492 = (int32_t)_M0L6_2atmpS1493;
        uint32_t _M0L1cS406 = *(uint32_t*)&_M0L6_2atmpS1492;
        uint32_t _M0L6_2atmpS1488 = _M0L1cS406 & 255u;
        int32_t _M0L6_2atmpS1487;
        int32_t _M0L6_2atmpS1489;
        uint32_t _M0L6_2atmpS1491;
        int32_t _M0L6_2atmpS1490;
        int32_t _M0L6_2atmpS1494;
        int32_t _M0L6_2atmpS1495;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1487 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1488);
        if (
          _M0L1jS405 < 0 || _M0L1jS405 >= Moonbit_array_length(_M0L4selfS400)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS400[_M0L1jS405] = _M0L6_2atmpS1487;
        _M0L6_2atmpS1489 = _M0L1jS405 + 1;
        _M0L6_2atmpS1491 = _M0L1cS406 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1490 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1491);
        if (
          _M0L6_2atmpS1489 < 0
          || _M0L6_2atmpS1489 >= Moonbit_array_length(_M0L4selfS400)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS400[_M0L6_2atmpS1489] = _M0L6_2atmpS1490;
        _M0L6_2atmpS1494 = _M0L1iS404 + 1;
        _M0L6_2atmpS1495 = _M0L1jS405 + 2;
        _M0L1iS404 = _M0L6_2atmpS1494;
        _M0L1jS405 = _M0L6_2atmpS1495;
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
  int32_t _M0L6_2atmpS1486;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1486 = *(int32_t*)&_M0L4selfS393;
  return _M0L6_2atmpS1486 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS385,
  int32_t _M0L5radixS384
) {
  uint16_t* _M0L6bufferS386;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS384 < 2 || _M0L5radixS384 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_18.data);
  }
  if (_M0L4selfS385 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_18.data);
  }
  if (_M0L4selfS368 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  _M0L12is__negativeS369 = _M0L4selfS368 < 0ll;
  if (_M0L12is__negativeS369) {
    int64_t _M0L6_2atmpS1485 = -_M0L4selfS368;
    _M0L3numS370 = *(uint64_t*)&_M0L6_2atmpS1485;
  } else {
    _M0L3numS370 = *(uint64_t*)&_M0L4selfS368;
  }
  switch (_M0L5radixS367) {
    case 10: {
      int32_t _M0L10digit__lenS372;
      int32_t _M0L6_2atmpS1482;
      int32_t _M0L10total__lenS373;
      uint16_t* _M0L6bufferS374;
      int32_t _M0L12digit__startS375;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS372 = _M0FPB12dec__count64(_M0L3numS370);
      if (_M0L12is__negativeS369) {
        _M0L6_2atmpS1482 = 1;
      } else {
        _M0L6_2atmpS1482 = 0;
      }
      _M0L10total__lenS373 = _M0L10digit__lenS372 + _M0L6_2atmpS1482;
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
      int32_t _M0L6_2atmpS1483;
      int32_t _M0L10total__lenS377;
      uint16_t* _M0L6bufferS378;
      int32_t _M0L12digit__startS379;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS376 = _M0FPB12hex__count64(_M0L3numS370);
      if (_M0L12is__negativeS369) {
        _M0L6_2atmpS1483 = 1;
      } else {
        _M0L6_2atmpS1483 = 0;
      }
      _M0L10total__lenS377 = _M0L10digit__lenS376 + _M0L6_2atmpS1483;
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
      int32_t _M0L6_2atmpS1484;
      int32_t _M0L10total__lenS381;
      uint16_t* _M0L6bufferS382;
      int32_t _M0L12digit__startS383;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS380
      = _M0FPB14radix__count64(_M0L3numS370, _M0L5radixS367);
      if (_M0L12is__negativeS369) {
        _M0L6_2atmpS1484 = 1;
      } else {
        _M0L6_2atmpS1484 = 0;
      }
      _M0L10total__lenS381 = _M0L10digit__lenS380 + _M0L6_2atmpS1484;
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
  int32_t _M0L6_2atmpS1481;
  uint64_t _M0L3numS343;
  int32_t _M0L6offsetS344;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1481 = _M0L10total__lenS366 - _M0L12digit__startS354;
  _M0L3numS343 = _M0L3numS365;
  _M0L6offsetS344 = _M0L6_2atmpS1481;
  while (1) {
    if (_M0L3numS343 >= 10000ull) {
      uint64_t _M0L1tS345 = _M0L3numS343 / 10000ull;
      uint64_t _M0L6_2atmpS1458 = _M0L3numS343 % 10000ull;
      int32_t _M0L1rS346 = (int32_t)_M0L6_2atmpS1458;
      int32_t _M0L2d1S347 = _M0L1rS346 / 100;
      int32_t _M0L2d2S348 = _M0L1rS346 % 100;
      int32_t _M0L6_2atmpS1457 = _M0L2d1S347 / 10;
      int32_t _M0L6_2atmpS1456 = 48 + _M0L6_2atmpS1457;
      int32_t _M0L6d1__hiS349 = (uint16_t)_M0L6_2atmpS1456;
      int32_t _M0L6_2atmpS1455 = _M0L2d1S347 % 10;
      int32_t _M0L6_2atmpS1454 = 48 + _M0L6_2atmpS1455;
      int32_t _M0L6d1__loS350 = (uint16_t)_M0L6_2atmpS1454;
      int32_t _M0L6_2atmpS1453 = _M0L2d2S348 / 10;
      int32_t _M0L6_2atmpS1452 = 48 + _M0L6_2atmpS1453;
      int32_t _M0L6d2__hiS351 = (uint16_t)_M0L6_2atmpS1452;
      int32_t _M0L6_2atmpS1451 = _M0L2d2S348 % 10;
      int32_t _M0L6_2atmpS1450 = 48 + _M0L6_2atmpS1451;
      int32_t _M0L6d2__loS352 = (uint16_t)_M0L6_2atmpS1450;
      int32_t _M0L6_2atmpS1442 = _M0L12digit__startS354 + _M0L6offsetS344;
      int32_t _M0L6_2atmpS1441 = _M0L6_2atmpS1442 - 4;
      int32_t _M0L6_2atmpS1444;
      int32_t _M0L6_2atmpS1443;
      int32_t _M0L6_2atmpS1446;
      int32_t _M0L6_2atmpS1445;
      int32_t _M0L6_2atmpS1448;
      int32_t _M0L6_2atmpS1447;
      int32_t _M0L6_2atmpS1449;
      _M0L6bufferS353[_M0L6_2atmpS1441] = _M0L6d1__hiS349;
      _M0L6_2atmpS1444 = _M0L12digit__startS354 + _M0L6offsetS344;
      _M0L6_2atmpS1443 = _M0L6_2atmpS1444 - 3;
      _M0L6bufferS353[_M0L6_2atmpS1443] = _M0L6d1__loS350;
      _M0L6_2atmpS1446 = _M0L12digit__startS354 + _M0L6offsetS344;
      _M0L6_2atmpS1445 = _M0L6_2atmpS1446 - 2;
      _M0L6bufferS353[_M0L6_2atmpS1445] = _M0L6d2__hiS351;
      _M0L6_2atmpS1448 = _M0L12digit__startS354 + _M0L6offsetS344;
      _M0L6_2atmpS1447 = _M0L6_2atmpS1448 - 1;
      _M0L6bufferS353[_M0L6_2atmpS1447] = _M0L6d2__loS352;
      _M0L6_2atmpS1449 = _M0L6offsetS344 - 4;
      _M0L3numS343 = _M0L1tS345;
      _M0L6offsetS344 = _M0L6_2atmpS1449;
      continue;
    } else {
      int32_t _M0L6_2atmpS1480 = (int32_t)_M0L3numS343;
      int32_t _M0L9remainingS356 = _M0L6_2atmpS1480;
      int32_t _M0L6offsetS357 = _M0L6offsetS344;
      while (1) {
        if (_M0L9remainingS356 >= 100) {
          int32_t _M0L1tS358 = _M0L9remainingS356 / 100;
          int32_t _M0L1dS359 = _M0L9remainingS356 % 100;
          int32_t _M0L6_2atmpS1467 = _M0L1dS359 / 10;
          int32_t _M0L6_2atmpS1466 = 48 + _M0L6_2atmpS1467;
          int32_t _M0L5d__hiS360 = (uint16_t)_M0L6_2atmpS1466;
          int32_t _M0L6_2atmpS1465 = _M0L1dS359 % 10;
          int32_t _M0L6_2atmpS1464 = 48 + _M0L6_2atmpS1465;
          int32_t _M0L5d__loS361 = (uint16_t)_M0L6_2atmpS1464;
          int32_t _M0L6_2atmpS1460 = _M0L12digit__startS354 + _M0L6offsetS357;
          int32_t _M0L6_2atmpS1459 = _M0L6_2atmpS1460 - 2;
          int32_t _M0L6_2atmpS1462;
          int32_t _M0L6_2atmpS1461;
          int32_t _M0L6_2atmpS1463;
          _M0L6bufferS353[_M0L6_2atmpS1459] = _M0L5d__hiS360;
          _M0L6_2atmpS1462 = _M0L12digit__startS354 + _M0L6offsetS357;
          _M0L6_2atmpS1461 = _M0L6_2atmpS1462 - 1;
          _M0L6bufferS353[_M0L6_2atmpS1461] = _M0L5d__loS361;
          _M0L6_2atmpS1463 = _M0L6offsetS357 - 2;
          _M0L9remainingS356 = _M0L1tS358;
          _M0L6offsetS357 = _M0L6_2atmpS1463;
          continue;
        } else if (_M0L9remainingS356 >= 10) {
          int32_t _M0L6_2atmpS1475 = _M0L9remainingS356 / 10;
          int32_t _M0L6_2atmpS1474 = 48 + _M0L6_2atmpS1475;
          int32_t _M0L5d__hiS363 = (uint16_t)_M0L6_2atmpS1474;
          int32_t _M0L6_2atmpS1473 = _M0L9remainingS356 % 10;
          int32_t _M0L6_2atmpS1472 = 48 + _M0L6_2atmpS1473;
          int32_t _M0L5d__loS364 = (uint16_t)_M0L6_2atmpS1472;
          int32_t _M0L6_2atmpS1469 = _M0L12digit__startS354 + _M0L6offsetS357;
          int32_t _M0L6_2atmpS1468 = _M0L6_2atmpS1469 - 2;
          int32_t _M0L6_2atmpS1471;
          int32_t _M0L6_2atmpS1470;
          _M0L6bufferS353[_M0L6_2atmpS1468] = _M0L5d__hiS363;
          _M0L6_2atmpS1471 = _M0L12digit__startS354 + _M0L6offsetS357;
          _M0L6_2atmpS1470 = _M0L6_2atmpS1471 - 1;
          _M0L6bufferS353[_M0L6_2atmpS1470] = _M0L5d__loS364;
        } else {
          int32_t _M0L6_2atmpS1479 = _M0L12digit__startS354 + _M0L6offsetS357;
          int32_t _M0L6_2atmpS1476 = _M0L6_2atmpS1479 - 1;
          int32_t _M0L6_2atmpS1478 = 48 + _M0L9remainingS356;
          int32_t _M0L6_2atmpS1477 = (uint16_t)_M0L6_2atmpS1478;
          _M0L6bufferS353[_M0L6_2atmpS1476] = _M0L6_2atmpS1477;
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
  int32_t _M0L6_2atmpS1426;
  int32_t _M0L6_2atmpS1425;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS326 = _M0MPC13int3Int10to__uint64(_M0L5radixS327);
  _M0L6_2atmpS1426 = _M0L5radixS327 - 1;
  _M0L6_2atmpS1425 = _M0L5radixS327 & _M0L6_2atmpS1426;
  if (_M0L6_2atmpS1425 == 0) {
    int32_t _M0L5shiftS328;
    uint64_t _M0L4maskS329;
    int32_t _M0L6_2atmpS1433;
    int32_t _M0L6offsetS330;
    uint64_t _M0L1nS331;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS328 = moonbit_ctz32(_M0L5radixS327);
    _M0L4maskS329 = _M0L4baseS326 - 1ull;
    _M0L6_2atmpS1433 = _M0L10total__lenS336 - _M0L12digit__startS334;
    _M0L6offsetS330 = _M0L6_2atmpS1433;
    _M0L1nS331 = _M0L3numS337;
    while (1) {
      if (_M0L1nS331 > 0ull) {
        uint64_t _M0L6_2atmpS1432 = _M0L1nS331 & _M0L4maskS329;
        int32_t _M0L5digitS332 = (int32_t)_M0L6_2atmpS1432;
        int32_t _M0L6_2atmpS1429 = _M0L12digit__startS334 + _M0L6offsetS330;
        int32_t _M0L6_2atmpS1427 = _M0L6_2atmpS1429 - 1;
        int32_t _M0L6_2atmpS1428 =
          ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L5digitS332];
        int32_t _M0L6_2atmpS1430;
        uint64_t _M0L6_2atmpS1431;
        _M0L6bufferS333[_M0L6_2atmpS1427] = _M0L6_2atmpS1428;
        _M0L6_2atmpS1430 = _M0L6offsetS330 - 1;
        _M0L6_2atmpS1431 = _M0L1nS331 >> (_M0L5shiftS328 & 63);
        _M0L6offsetS330 = _M0L6_2atmpS1430;
        _M0L1nS331 = _M0L6_2atmpS1431;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1440 = _M0L10total__lenS336 - _M0L12digit__startS334;
    int32_t _M0L6offsetS338 = _M0L6_2atmpS1440;
    uint64_t _M0L1nS339 = _M0L3numS337;
    while (1) {
      if (_M0L1nS339 > 0ull) {
        uint64_t _M0L1qS340 = _M0L1nS339 / _M0L4baseS326;
        uint64_t _M0L6_2atmpS1439 = _M0L1qS340 * _M0L4baseS326;
        uint64_t _M0L6_2atmpS1438 = _M0L1nS339 - _M0L6_2atmpS1439;
        int32_t _M0L5digitS341 = (int32_t)_M0L6_2atmpS1438;
        int32_t _M0L6_2atmpS1436 = _M0L12digit__startS334 + _M0L6offsetS338;
        int32_t _M0L6_2atmpS1434 = _M0L6_2atmpS1436 - 1;
        int32_t _M0L6_2atmpS1435 =
          ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L5digitS341];
        int32_t _M0L6_2atmpS1437;
        _M0L6bufferS333[_M0L6_2atmpS1434] = _M0L6_2atmpS1435;
        _M0L6_2atmpS1437 = _M0L6offsetS338 - 1;
        _M0L6offsetS338 = _M0L6_2atmpS1437;
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
  int32_t _M0L6_2atmpS1424;
  int32_t _M0L6offsetS315;
  uint64_t _M0L1nS316;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1424 = _M0L10total__lenS324 - _M0L12digit__startS321;
  _M0L6offsetS315 = _M0L6_2atmpS1424;
  _M0L1nS316 = _M0L3numS325;
  while (1) {
    if (_M0L6offsetS315 >= 2) {
      uint64_t _M0L6_2atmpS1421 = _M0L1nS316 & 255ull;
      int32_t _M0L9byte__valS317 = (int32_t)_M0L6_2atmpS1421;
      int32_t _M0L2hiS318 = _M0L9byte__valS317 / 16;
      int32_t _M0L2loS319 = _M0L9byte__valS317 % 16;
      int32_t _M0L6_2atmpS1415 = _M0L12digit__startS321 + _M0L6offsetS315;
      int32_t _M0L6_2atmpS1413 = _M0L6_2atmpS1415 - 2;
      int32_t _M0L6_2atmpS1414 =
        ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L2hiS318];
      int32_t _M0L6_2atmpS1418;
      int32_t _M0L6_2atmpS1416;
      int32_t _M0L6_2atmpS1417;
      int32_t _M0L6_2atmpS1419;
      uint64_t _M0L6_2atmpS1420;
      _M0L6bufferS320[_M0L6_2atmpS1413] = _M0L6_2atmpS1414;
      _M0L6_2atmpS1418 = _M0L12digit__startS321 + _M0L6offsetS315;
      _M0L6_2atmpS1416 = _M0L6_2atmpS1418 - 1;
      _M0L6_2atmpS1417
      = ((moonbit_string_t)moonbit_string_literal_19.data)[
        _M0L2loS319
      ];
      _M0L6bufferS320[_M0L6_2atmpS1416] = _M0L6_2atmpS1417;
      _M0L6_2atmpS1419 = _M0L6offsetS315 - 2;
      _M0L6_2atmpS1420 = _M0L1nS316 >> 8;
      _M0L6offsetS315 = _M0L6_2atmpS1419;
      _M0L1nS316 = _M0L6_2atmpS1420;
      continue;
    } else if (_M0L6offsetS315 == 1) {
      uint64_t _M0L6_2atmpS1423 = _M0L1nS316 & 15ull;
      int32_t _M0L6nibbleS323 = (int32_t)_M0L6_2atmpS1423;
      int32_t _M0L6_2atmpS1422 =
        ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L6nibbleS323];
      _M0L6bufferS320[_M0L12digit__startS321] = _M0L6_2atmpS1422;
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
      uint64_t _M0L6_2atmpS1411 = _M0L3numS312 / _M0L4baseS310;
      int32_t _M0L6_2atmpS1412 = _M0L5countS313 + 1;
      _M0L3numS312 = _M0L6_2atmpS1411;
      _M0L5countS313 = _M0L6_2atmpS1412;
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
    int32_t _M0L6_2atmpS1410;
    int32_t _M0L6_2atmpS1409;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS308 = moonbit_clz64(_M0L5valueS307);
    _M0L6_2atmpS1410 = 63 - _M0L14leading__zerosS308;
    _M0L6_2atmpS1409 = _M0L6_2atmpS1410 / 4;
    return _M0L6_2atmpS1409 + 1;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_18.data);
  }
  if (_M0L4selfS290 == 0) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  _M0L12is__negativeS291 = _M0L4selfS290 < 0;
  if (_M0L12is__negativeS291) {
    int32_t _M0L6_2atmpS1408 = -_M0L4selfS290;
    _M0L3numS292 = *(uint32_t*)&_M0L6_2atmpS1408;
  } else {
    _M0L3numS292 = *(uint32_t*)&_M0L4selfS290;
  }
  switch (_M0L5radixS289) {
    case 10: {
      int32_t _M0L10digit__lenS294;
      int32_t _M0L6_2atmpS1405;
      int32_t _M0L10total__lenS295;
      uint16_t* _M0L6bufferS296;
      int32_t _M0L12digit__startS297;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS294 = _M0FPB12dec__count32(_M0L3numS292);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1405 = 1;
      } else {
        _M0L6_2atmpS1405 = 0;
      }
      _M0L10total__lenS295 = _M0L10digit__lenS294 + _M0L6_2atmpS1405;
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
      int32_t _M0L6_2atmpS1406;
      int32_t _M0L10total__lenS299;
      uint16_t* _M0L6bufferS300;
      int32_t _M0L12digit__startS301;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS298 = _M0FPB12hex__count32(_M0L3numS292);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1406 = 1;
      } else {
        _M0L6_2atmpS1406 = 0;
      }
      _M0L10total__lenS299 = _M0L10digit__lenS298 + _M0L6_2atmpS1406;
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
      int32_t _M0L6_2atmpS1407;
      int32_t _M0L10total__lenS303;
      uint16_t* _M0L6bufferS304;
      int32_t _M0L12digit__startS305;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS302
      = _M0FPB14radix__count32(_M0L3numS292, _M0L5radixS289);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1407 = 1;
      } else {
        _M0L6_2atmpS1407 = 0;
      }
      _M0L10total__lenS303 = _M0L10digit__lenS302 + _M0L6_2atmpS1407;
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
      uint32_t _M0L6_2atmpS1403 = _M0L3numS286 / _M0L4baseS284;
      int32_t _M0L6_2atmpS1404 = _M0L5countS287 + 1;
      _M0L3numS286 = _M0L6_2atmpS1403;
      _M0L5countS287 = _M0L6_2atmpS1404;
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
    int32_t _M0L6_2atmpS1402;
    int32_t _M0L6_2atmpS1401;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS282 = moonbit_clz32(_M0L5valueS281);
    _M0L6_2atmpS1402 = 31 - _M0L14leading__zerosS282;
    _M0L6_2atmpS1401 = _M0L6_2atmpS1402 / 4;
    return _M0L6_2atmpS1401 + 1;
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
  int32_t _M0L6_2atmpS1400;
  uint32_t _M0L3numS256;
  int32_t _M0L6offsetS257;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1400 = _M0L10total__lenS279 - _M0L12digit__startS267;
  _M0L3numS256 = _M0L3numS278;
  _M0L6offsetS257 = _M0L6_2atmpS1400;
  while (1) {
    if (_M0L3numS256 >= 10000u) {
      uint32_t _M0L1tS258 = _M0L3numS256 / 10000u;
      uint32_t _M0L6_2atmpS1377 = _M0L3numS256 % 10000u;
      int32_t _M0L1rS259 = *(int32_t*)&_M0L6_2atmpS1377;
      int32_t _M0L2d1S260 = _M0L1rS259 / 100;
      int32_t _M0L2d2S261 = _M0L1rS259 % 100;
      int32_t _M0L6_2atmpS1376 = _M0L2d1S260 / 10;
      int32_t _M0L6_2atmpS1375 = 48 + _M0L6_2atmpS1376;
      int32_t _M0L6d1__hiS262 = (uint16_t)_M0L6_2atmpS1375;
      int32_t _M0L6_2atmpS1374 = _M0L2d1S260 % 10;
      int32_t _M0L6_2atmpS1373 = 48 + _M0L6_2atmpS1374;
      int32_t _M0L6d1__loS263 = (uint16_t)_M0L6_2atmpS1373;
      int32_t _M0L6_2atmpS1372 = _M0L2d2S261 / 10;
      int32_t _M0L6_2atmpS1371 = 48 + _M0L6_2atmpS1372;
      int32_t _M0L6d2__hiS264 = (uint16_t)_M0L6_2atmpS1371;
      int32_t _M0L6_2atmpS1370 = _M0L2d2S261 % 10;
      int32_t _M0L6_2atmpS1369 = 48 + _M0L6_2atmpS1370;
      int32_t _M0L6d2__loS265 = (uint16_t)_M0L6_2atmpS1369;
      int32_t _M0L6_2atmpS1361 = _M0L12digit__startS267 + _M0L6offsetS257;
      int32_t _M0L6_2atmpS1360 = _M0L6_2atmpS1361 - 4;
      int32_t _M0L6_2atmpS1363;
      int32_t _M0L6_2atmpS1362;
      int32_t _M0L6_2atmpS1365;
      int32_t _M0L6_2atmpS1364;
      int32_t _M0L6_2atmpS1367;
      int32_t _M0L6_2atmpS1366;
      int32_t _M0L6_2atmpS1368;
      _M0L6bufferS266[_M0L6_2atmpS1360] = _M0L6d1__hiS262;
      _M0L6_2atmpS1363 = _M0L12digit__startS267 + _M0L6offsetS257;
      _M0L6_2atmpS1362 = _M0L6_2atmpS1363 - 3;
      _M0L6bufferS266[_M0L6_2atmpS1362] = _M0L6d1__loS263;
      _M0L6_2atmpS1365 = _M0L12digit__startS267 + _M0L6offsetS257;
      _M0L6_2atmpS1364 = _M0L6_2atmpS1365 - 2;
      _M0L6bufferS266[_M0L6_2atmpS1364] = _M0L6d2__hiS264;
      _M0L6_2atmpS1367 = _M0L12digit__startS267 + _M0L6offsetS257;
      _M0L6_2atmpS1366 = _M0L6_2atmpS1367 - 1;
      _M0L6bufferS266[_M0L6_2atmpS1366] = _M0L6d2__loS265;
      _M0L6_2atmpS1368 = _M0L6offsetS257 - 4;
      _M0L3numS256 = _M0L1tS258;
      _M0L6offsetS257 = _M0L6_2atmpS1368;
      continue;
    } else {
      int32_t _M0L6_2atmpS1399 = *(int32_t*)&_M0L3numS256;
      int32_t _M0L9remainingS269 = _M0L6_2atmpS1399;
      int32_t _M0L6offsetS270 = _M0L6offsetS257;
      while (1) {
        if (_M0L9remainingS269 >= 100) {
          int32_t _M0L1tS271 = _M0L9remainingS269 / 100;
          int32_t _M0L1dS272 = _M0L9remainingS269 % 100;
          int32_t _M0L6_2atmpS1386 = _M0L1dS272 / 10;
          int32_t _M0L6_2atmpS1385 = 48 + _M0L6_2atmpS1386;
          int32_t _M0L5d__hiS273 = (uint16_t)_M0L6_2atmpS1385;
          int32_t _M0L6_2atmpS1384 = _M0L1dS272 % 10;
          int32_t _M0L6_2atmpS1383 = 48 + _M0L6_2atmpS1384;
          int32_t _M0L5d__loS274 = (uint16_t)_M0L6_2atmpS1383;
          int32_t _M0L6_2atmpS1379 = _M0L12digit__startS267 + _M0L6offsetS270;
          int32_t _M0L6_2atmpS1378 = _M0L6_2atmpS1379 - 2;
          int32_t _M0L6_2atmpS1381;
          int32_t _M0L6_2atmpS1380;
          int32_t _M0L6_2atmpS1382;
          _M0L6bufferS266[_M0L6_2atmpS1378] = _M0L5d__hiS273;
          _M0L6_2atmpS1381 = _M0L12digit__startS267 + _M0L6offsetS270;
          _M0L6_2atmpS1380 = _M0L6_2atmpS1381 - 1;
          _M0L6bufferS266[_M0L6_2atmpS1380] = _M0L5d__loS274;
          _M0L6_2atmpS1382 = _M0L6offsetS270 - 2;
          _M0L9remainingS269 = _M0L1tS271;
          _M0L6offsetS270 = _M0L6_2atmpS1382;
          continue;
        } else if (_M0L9remainingS269 >= 10) {
          int32_t _M0L6_2atmpS1394 = _M0L9remainingS269 / 10;
          int32_t _M0L6_2atmpS1393 = 48 + _M0L6_2atmpS1394;
          int32_t _M0L5d__hiS276 = (uint16_t)_M0L6_2atmpS1393;
          int32_t _M0L6_2atmpS1392 = _M0L9remainingS269 % 10;
          int32_t _M0L6_2atmpS1391 = 48 + _M0L6_2atmpS1392;
          int32_t _M0L5d__loS277 = (uint16_t)_M0L6_2atmpS1391;
          int32_t _M0L6_2atmpS1388 = _M0L12digit__startS267 + _M0L6offsetS270;
          int32_t _M0L6_2atmpS1387 = _M0L6_2atmpS1388 - 2;
          int32_t _M0L6_2atmpS1390;
          int32_t _M0L6_2atmpS1389;
          _M0L6bufferS266[_M0L6_2atmpS1387] = _M0L5d__hiS276;
          _M0L6_2atmpS1390 = _M0L12digit__startS267 + _M0L6offsetS270;
          _M0L6_2atmpS1389 = _M0L6_2atmpS1390 - 1;
          _M0L6bufferS266[_M0L6_2atmpS1389] = _M0L5d__loS277;
        } else {
          int32_t _M0L6_2atmpS1398 = _M0L12digit__startS267 + _M0L6offsetS270;
          int32_t _M0L6_2atmpS1395 = _M0L6_2atmpS1398 - 1;
          int32_t _M0L6_2atmpS1397 = 48 + _M0L9remainingS269;
          int32_t _M0L6_2atmpS1396 = (uint16_t)_M0L6_2atmpS1397;
          _M0L6bufferS266[_M0L6_2atmpS1395] = _M0L6_2atmpS1396;
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
  int32_t _M0L6_2atmpS1345;
  int32_t _M0L6_2atmpS1344;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS239 = *(uint32_t*)&_M0L5radixS240;
  _M0L6_2atmpS1345 = _M0L5radixS240 - 1;
  _M0L6_2atmpS1344 = _M0L5radixS240 & _M0L6_2atmpS1345;
  if (_M0L6_2atmpS1344 == 0) {
    int32_t _M0L5shiftS241;
    uint32_t _M0L4maskS242;
    int32_t _M0L6_2atmpS1352;
    int32_t _M0L6offsetS243;
    uint32_t _M0L1nS244;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS241 = moonbit_ctz32(_M0L5radixS240);
    _M0L4maskS242 = _M0L4baseS239 - 1u;
    _M0L6_2atmpS1352 = _M0L10total__lenS249 - _M0L12digit__startS247;
    _M0L6offsetS243 = _M0L6_2atmpS1352;
    _M0L1nS244 = _M0L3numS250;
    while (1) {
      if (_M0L1nS244 > 0u) {
        uint32_t _M0L6_2atmpS1351 = _M0L1nS244 & _M0L4maskS242;
        int32_t _M0L5digitS245 = *(int32_t*)&_M0L6_2atmpS1351;
        int32_t _M0L6_2atmpS1348 = _M0L12digit__startS247 + _M0L6offsetS243;
        int32_t _M0L6_2atmpS1346 = _M0L6_2atmpS1348 - 1;
        int32_t _M0L6_2atmpS1347 =
          ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L5digitS245];
        int32_t _M0L6_2atmpS1349;
        uint32_t _M0L6_2atmpS1350;
        _M0L6bufferS246[_M0L6_2atmpS1346] = _M0L6_2atmpS1347;
        _M0L6_2atmpS1349 = _M0L6offsetS243 - 1;
        _M0L6_2atmpS1350 = _M0L1nS244 >> (_M0L5shiftS241 & 31);
        _M0L6offsetS243 = _M0L6_2atmpS1349;
        _M0L1nS244 = _M0L6_2atmpS1350;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1359 = _M0L10total__lenS249 - _M0L12digit__startS247;
    int32_t _M0L6offsetS251 = _M0L6_2atmpS1359;
    uint32_t _M0L1nS252 = _M0L3numS250;
    while (1) {
      if (_M0L1nS252 > 0u) {
        uint32_t _M0L1qS253 = _M0L1nS252 / _M0L4baseS239;
        uint32_t _M0L6_2atmpS1358 = _M0L1qS253 * _M0L4baseS239;
        uint32_t _M0L6_2atmpS1357 = _M0L1nS252 - _M0L6_2atmpS1358;
        int32_t _M0L5digitS254 = *(int32_t*)&_M0L6_2atmpS1357;
        int32_t _M0L6_2atmpS1355 = _M0L12digit__startS247 + _M0L6offsetS251;
        int32_t _M0L6_2atmpS1353 = _M0L6_2atmpS1355 - 1;
        int32_t _M0L6_2atmpS1354 =
          ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L5digitS254];
        int32_t _M0L6_2atmpS1356;
        _M0L6bufferS246[_M0L6_2atmpS1353] = _M0L6_2atmpS1354;
        _M0L6_2atmpS1356 = _M0L6offsetS251 - 1;
        _M0L6offsetS251 = _M0L6_2atmpS1356;
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
  int32_t _M0L6_2atmpS1343;
  int32_t _M0L6offsetS228;
  uint32_t _M0L1nS229;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1343 = _M0L10total__lenS237 - _M0L12digit__startS234;
  _M0L6offsetS228 = _M0L6_2atmpS1343;
  _M0L1nS229 = _M0L3numS238;
  while (1) {
    if (_M0L6offsetS228 >= 2) {
      uint32_t _M0L6_2atmpS1340 = _M0L1nS229 & 255u;
      int32_t _M0L9byte__valS230 = *(int32_t*)&_M0L6_2atmpS1340;
      int32_t _M0L2hiS231 = _M0L9byte__valS230 / 16;
      int32_t _M0L2loS232 = _M0L9byte__valS230 % 16;
      int32_t _M0L6_2atmpS1334 = _M0L12digit__startS234 + _M0L6offsetS228;
      int32_t _M0L6_2atmpS1332 = _M0L6_2atmpS1334 - 2;
      int32_t _M0L6_2atmpS1333 =
        ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L2hiS231];
      int32_t _M0L6_2atmpS1337;
      int32_t _M0L6_2atmpS1335;
      int32_t _M0L6_2atmpS1336;
      int32_t _M0L6_2atmpS1338;
      uint32_t _M0L6_2atmpS1339;
      _M0L6bufferS233[_M0L6_2atmpS1332] = _M0L6_2atmpS1333;
      _M0L6_2atmpS1337 = _M0L12digit__startS234 + _M0L6offsetS228;
      _M0L6_2atmpS1335 = _M0L6_2atmpS1337 - 1;
      _M0L6_2atmpS1336
      = ((moonbit_string_t)moonbit_string_literal_19.data)[
        _M0L2loS232
      ];
      _M0L6bufferS233[_M0L6_2atmpS1335] = _M0L6_2atmpS1336;
      _M0L6_2atmpS1338 = _M0L6offsetS228 - 2;
      _M0L6_2atmpS1339 = _M0L1nS229 >> 8;
      _M0L6offsetS228 = _M0L6_2atmpS1338;
      _M0L1nS229 = _M0L6_2atmpS1339;
      continue;
    } else if (_M0L6offsetS228 == 1) {
      uint32_t _M0L6_2atmpS1342 = _M0L1nS229 & 15u;
      int32_t _M0L6nibbleS236 = *(int32_t*)&_M0L6_2atmpS1342;
      int32_t _M0L6_2atmpS1341 =
        ((moonbit_string_t)moonbit_string_literal_19.data)[_M0L6nibbleS236];
      _M0L6bufferS233[_M0L12digit__startS234] = _M0L6_2atmpS1341;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS227
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS226;
  struct _M0TPB6Logger _M0L6_2atmpS1331;
  moonbit_string_t _result_2346;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS226);
  _M0L6_2atmpS1331
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS226
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS227, _M0L6_2atmpS1331);
  if (_M0L6_2atmpS1331.$1) {
    moonbit_decref(_M0L6_2atmpS1331.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2346 = _M0MPB13StringBuilder10to__string(_M0L6loggerS226);
  moonbit_decref_cycle_free(_M0L6loggerS226);
  return _result_2346;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS221,
  struct _M0TPB6Logger _M0L6loggerS220
) {
  moonbit_string_t _M0L6_2atmpS1328;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1328 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS221);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS220.$0->$method_0(_M0L6loggerS220.$1, _M0L6_2atmpS1328);
  moonbit_decref_cycle_free(_M0L6_2atmpS1328);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS223,
  struct _M0TPB6Logger _M0L6loggerS222
) {
  moonbit_string_t _M0L6_2atmpS1329;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1329 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS223);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS222.$0->$method_0(_M0L6loggerS222.$1, _M0L6_2atmpS1329);
  moonbit_decref_cycle_free(_M0L6_2atmpS1329);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS225,
  struct _M0TPB6Logger _M0L6loggerS224
) {
  moonbit_string_t _M0L6_2atmpS1330;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1330 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS225);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS224.$0->$method_0(_M0L6loggerS224.$1, _M0L6_2atmpS1330);
  moonbit_decref_cycle_free(_M0L6_2atmpS1330);
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
  moonbit_string_t _M0L8_2afieldS2230;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2230 = _M0L4selfS218.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2230);
  return _M0L8_2afieldS2230;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS214,
  moonbit_string_t _M0L5valueS215,
  int32_t _M0L5startS216,
  int32_t _M0L3lenS217
) {
  int32_t _M0L6_2atmpS1327;
  int64_t _M0L6_2atmpS1326;
  struct _M0TPC16string10StringView _M0L6_2atmpS1325;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1327 = _M0L5startS216 + _M0L3lenS217;
  _M0L6_2atmpS1326 = (int64_t)_M0L6_2atmpS1327;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1325
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS215, _M0L5startS216, _M0L6_2atmpS1326);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS214, _M0L6_2atmpS1325);
  moonbit_decref_cycle_free(_M0L6_2atmpS1325.$0);
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
  int32_t _M0L6_2atmpS1309;
  int32_t _if__result_2347;
  int32_t _M0L6_2atmpS1317;
  int32_t _if__result_2348;
  int32_t _M0L6_2atmpS1319;
  int32_t _M0L6_2atmpS1320;
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
  _M0L6_2atmpS1309 = _M0Lm2loS208;
  if (_M0L6_2atmpS1309 > 0) {
    int32_t _M0L6_2atmpS1308 = _M0Lm2loS208;
    if (_M0L6_2atmpS1308 < _M0L3lenS206) {
      int32_t _M0L6_2atmpS1307 = _M0Lm2loS208;
      int32_t _M0L6_2atmpS1306 = _M0L4selfS207[_M0L6_2atmpS1307];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1306)) {
        int32_t _M0L6_2atmpS1305 = _M0Lm2loS208;
        int32_t _M0L6_2atmpS1304 = _M0L6_2atmpS1305 - 1;
        int32_t _M0L6_2atmpS1303 = _M0L4selfS207[_M0L6_2atmpS1304];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
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
    int32_t _M0L6_2atmpS1310 = _M0Lm2loS208;
    _M0Lm2loS208 = _M0L6_2atmpS1310 + 1;
  }
  _M0L6_2atmpS1317 = _M0Lm2hiS210;
  if (_M0L6_2atmpS1317 > 0) {
    int32_t _M0L6_2atmpS1316 = _M0Lm2hiS210;
    if (_M0L6_2atmpS1316 < _M0L3lenS206) {
      int32_t _M0L6_2atmpS1315 = _M0Lm2hiS210;
      int32_t _M0L6_2atmpS1314 = _M0L4selfS207[_M0L6_2atmpS1315];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1314)) {
        int32_t _M0L6_2atmpS1313 = _M0Lm2hiS210;
        int32_t _M0L6_2atmpS1312 = _M0L6_2atmpS1313 - 1;
        int32_t _M0L6_2atmpS1311 = _M0L4selfS207[_M0L6_2atmpS1312];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2348
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1311);
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
    int32_t _M0L6_2atmpS1318 = _M0Lm2hiS210;
    _M0Lm2hiS210 = _M0L6_2atmpS1318 - 1;
  }
  _M0L6_2atmpS1319 = _M0Lm2loS208;
  _M0L6_2atmpS1320 = _M0Lm2hiS210;
  if (_M0L6_2atmpS1319 >= _M0L6_2atmpS1320) {
    int32_t _M0L6_2atmpS1321 = _M0Lm2loS208;
    int32_t _M0L6_2atmpS1322 = _M0Lm2loS208;
    moonbit_incref_cycle_free(_M0L4selfS207);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS207,
                                                 .$1 = _M0L6_2atmpS1321,
                                                 .$2 = _M0L6_2atmpS1322};
  } else {
    int32_t _M0L6_2atmpS1323 = _M0Lm2loS208;
    int32_t _M0L6_2atmpS1324 = _M0Lm2hiS210;
    moonbit_incref_cycle_free(_M0L4selfS207);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS207,
                                                 .$1 = _M0L6_2atmpS1323,
                                                 .$2 = _M0L6_2atmpS1324};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS205,
  struct _M0TPB4Show _M0L4showS204
) {
  struct _M0TPB6Logger _M0L6_2atmpS1302;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS205);
  _M0L6_2atmpS1302
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS205
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS204.$0->$method_0(_M0L4showS204.$1, _M0L6_2atmpS1302);
  if (_M0L6_2atmpS1302.$1) {
    moonbit_decref(_M0L6_2atmpS1302.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS203,
  struct _M0TPB4Show _M0L4showS202
) {
  struct _M0TPB6Logger _M0L6_2atmpS1301;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS203);
  _M0L6_2atmpS1301
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS203
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS202.$0->$method_0(_M0L4showS202.$1, _M0L6_2atmpS1301);
  if (_M0L6_2atmpS1301.$1) {
    moonbit_decref(_M0L6_2atmpS1301.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS201) {
  int64_t _M0L6_2atmpS1300;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1300 = (int64_t)_M0L4selfS201;
  return *(uint64_t*)&_M0L6_2atmpS1300;
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
  int32_t _M0L6_2atmpS1299;
  struct _M0TPC16string10StringView _M0L6_2atmpS1297;
  struct _M0TPB6Logger _M0L6_2atmpS1298;
  moonbit_string_t _result_2349;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1299 = Moonbit_array_length(_M0L4selfS199);
  moonbit_incref_cycle_free(_M0L4selfS199);
  _M0L6_2atmpS1297
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS199, .$1 = 0, .$2 = _M0L6_2atmpS1299
  };
  moonbit_incref_cycle_free(_M0L3bufS198);
  _M0L6_2atmpS1298
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS198
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1297, _M0L6_2atmpS1298, _M0L5quoteS200);
  moonbit_decref_cycle_free(_M0L6_2atmpS1297.$0);
  if (_M0L6_2atmpS1298.$1) {
    moonbit_decref(_M0L6_2atmpS1298.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2349 = _M0MPB13StringBuilder10to__string(_M0L3bufS198);
  moonbit_decref_cycle_free(_M0L3bufS198);
  return _result_2349;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS190,
  struct _M0TPB6Logger _M0L6loggerS188,
  int32_t _M0L5quoteS187
) {
  int32_t _M0L3endS1295;
  int32_t _M0L5startS1296;
  int32_t _M0L3lenS189;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS191;
  int32_t _M0L1iS192;
  int32_t _M0L3segS193;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS187) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS188.$0->$method_3(_M0L6loggerS188.$1, 34);
  }
  _M0L3endS1295 = _M0L4selfS190.$2;
  _M0L5startS1296 = _M0L4selfS190.$1;
  _M0L3lenS189 = _M0L3endS1295 - _M0L5startS1296;
  moonbit_incref_cycle_free(_M0L4selfS190.$0);
  if (_M0L6loggerS188.$1) {
    moonbit_incref(_M0L6loggerS188.$1);
  }
  _M0L6_2aenvS191
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS191)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 66, 0);
  _M0L6_2aenvS191->$0 = _M0L4selfS190;
  _M0L6_2aenvS191->$1 = _M0L6loggerS188;
  _M0L1iS192 = 0;
  _M0L3segS193 = 0;
  _2afor_194:;
  while (1) {
    moonbit_string_t _M0L3strS1292;
    int32_t _M0L5startS1294;
    int32_t _M0L6_2atmpS1293;
    int32_t _M0L4codeS195;
    int32_t _M0L1cS197;
    int32_t _M0L6_2atmpS1276;
    int32_t _M0L6_2atmpS1277;
    int32_t _M0L6_2atmpS1278;
    if (_M0L1iS192 >= _M0L3lenS189) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
      moonbit_decref_cycle_free(_M0L6_2aenvS191);
      break;
    }
    _M0L3strS1292 = _M0L4selfS190.$0;
    _M0L5startS1294 = _M0L4selfS190.$1;
    _M0L6_2atmpS1293 = _M0L5startS1294 + _M0L1iS192;
    _M0L4codeS195 = _M0L3strS1292[_M0L6_2atmpS1293];
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
        int32_t _M0L6_2atmpS1279;
        int32_t _M0L6_2atmpS1280;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_20.data);
        _M0L6_2atmpS1279 = _M0L1iS192 + 1;
        _M0L6_2atmpS1280 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1279;
        _M0L3segS193 = _M0L6_2atmpS1280;
        goto _2afor_194;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1281;
        int32_t _M0L6_2atmpS1282;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS1281 = _M0L1iS192 + 1;
        _M0L6_2atmpS1282 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1281;
        _M0L3segS193 = _M0L6_2atmpS1282;
        goto _2afor_194;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1283;
        int32_t _M0L6_2atmpS1284;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_22.data);
        _M0L6_2atmpS1283 = _M0L1iS192 + 1;
        _M0L6_2atmpS1284 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1283;
        _M0L3segS193 = _M0L6_2atmpS1284;
        goto _2afor_194;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1285;
        int32_t _M0L6_2atmpS1286;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_23.data);
        _M0L6_2atmpS1285 = _M0L1iS192 + 1;
        _M0L6_2atmpS1286 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1285;
        _M0L3segS193 = _M0L6_2atmpS1286;
        goto _2afor_194;
        break;
      }
      default: {
        if (_M0L4codeS195 < 32) {
          int32_t _M0L6_2atmpS1288;
          moonbit_string_t _M0L6_2atmpS1287;
          int32_t _M0L6_2atmpS1289;
          int32_t _M0L6_2atmpS1290;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_24.data);
          _M0L6_2atmpS1288 = _M0L4codeS195 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1287 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1288);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, _M0L6_2atmpS1287);
          moonbit_decref_cycle_free(_M0L6_2atmpS1287);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1289 = _M0L1iS192 + 1;
          _M0L6_2atmpS1290 = _M0L1iS192 + 1;
          _M0L1iS192 = _M0L6_2atmpS1289;
          _M0L3segS193 = _M0L6_2atmpS1290;
          goto _2afor_194;
        } else {
          int32_t _M0L6_2atmpS1291 = _M0L1iS192 + 1;
          int32_t _tmp_2352 = _M0L3segS193;
          _M0L1iS192 = _M0L6_2atmpS1291;
          _M0L3segS193 = _tmp_2352;
          goto _2afor_194;
        }
        break;
      }
    }
    goto joinlet_2351;
    join_196:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS188.$0->$method_3(_M0L6loggerS188.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1276 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS197);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS188.$0->$method_3(_M0L6loggerS188.$1, _M0L6_2atmpS1276);
    _M0L6_2atmpS1277 = _M0L1iS192 + 1;
    _M0L6_2atmpS1278 = _M0L1iS192 + 1;
    _M0L1iS192 = _M0L6_2atmpS1277;
    _M0L3segS193 = _M0L6_2atmpS1278;
    continue;
    joinlet_2351:;
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
    int64_t _M0L6_2atmpS1275 = (int64_t)_M0L1iS185;
    struct _M0TPC16string10StringView _M0L6_2atmpS1274;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1274
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS184, _M0L3segS186, _M0L6_2atmpS1275);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS182.$0->$method_2(_M0L6loggerS182.$1, _M0L6_2atmpS1274);
    moonbit_decref_cycle_free(_M0L6_2atmpS1274.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS173,
  int32_t _M0L5startS175,
  int64_t _M0L3endS177
) {
  int32_t _M0L3endS1272;
  int32_t _M0L5startS1273;
  int32_t _M0L3lenS172;
  int32_t _M0Lm2loS174;
  int32_t _M0Lm2hiS176;
  moonbit_string_t _M0L3strS180;
  int32_t _M0L4baseS181;
  int32_t _M0L6_2atmpS1250;
  int32_t _if__result_2353;
  int32_t _M0L6_2atmpS1260;
  int32_t _if__result_2354;
  int32_t _M0L6_2atmpS1262;
  int32_t _M0L6_2atmpS1263;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1272 = _M0L4selfS173.$2;
  _M0L5startS1273 = _M0L4selfS173.$1;
  _M0L3lenS172 = _M0L3endS1272 - _M0L5startS1273;
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
  _M0L6_2atmpS1250 = _M0Lm2loS174;
  if (_M0L6_2atmpS1250 > 0) {
    int32_t _M0L6_2atmpS1249 = _M0Lm2loS174;
    if (_M0L6_2atmpS1249 < _M0L3lenS172) {
      int32_t _M0L6_2atmpS1248 = _M0Lm2loS174;
      int32_t _M0L6_2atmpS1247 = _M0L4baseS181 + _M0L6_2atmpS1248;
      int32_t _M0L6_2atmpS1246 = _M0L3strS180[_M0L6_2atmpS1247];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1246)) {
        int32_t _M0L6_2atmpS1245 = _M0Lm2loS174;
        int32_t _M0L6_2atmpS1244 = _M0L4baseS181 + _M0L6_2atmpS1245;
        int32_t _M0L6_2atmpS1243 = _M0L6_2atmpS1244 - 1;
        int32_t _M0L6_2atmpS1242 = _M0L3strS180[_M0L6_2atmpS1243];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2353
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1242);
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
    int32_t _M0L6_2atmpS1251 = _M0Lm2loS174;
    _M0Lm2loS174 = _M0L6_2atmpS1251 + 1;
  }
  _M0L6_2atmpS1260 = _M0Lm2hiS176;
  if (_M0L6_2atmpS1260 > 0) {
    int32_t _M0L6_2atmpS1259 = _M0Lm2hiS176;
    if (_M0L6_2atmpS1259 < _M0L3lenS172) {
      int32_t _M0L6_2atmpS1258 = _M0Lm2hiS176;
      int32_t _M0L6_2atmpS1257 = _M0L4baseS181 + _M0L6_2atmpS1258;
      int32_t _M0L6_2atmpS1256 = _M0L3strS180[_M0L6_2atmpS1257];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1256)) {
        int32_t _M0L6_2atmpS1255 = _M0Lm2hiS176;
        int32_t _M0L6_2atmpS1254 = _M0L4baseS181 + _M0L6_2atmpS1255;
        int32_t _M0L6_2atmpS1253 = _M0L6_2atmpS1254 - 1;
        int32_t _M0L6_2atmpS1252 = _M0L3strS180[_M0L6_2atmpS1253];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2354
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1252);
      } else {
        _if__result_2354 = 0;
      }
    } else {
      _if__result_2354 = 0;
    }
  } else {
    _if__result_2354 = 0;
  }
  if (_if__result_2354) {
    int32_t _M0L6_2atmpS1261 = _M0Lm2hiS176;
    _M0Lm2hiS176 = _M0L6_2atmpS1261 - 1;
  }
  _M0L6_2atmpS1262 = _M0Lm2loS174;
  _M0L6_2atmpS1263 = _M0Lm2hiS176;
  if (_M0L6_2atmpS1262 >= _M0L6_2atmpS1263) {
    int32_t _M0L6_2atmpS1267 = _M0Lm2loS174;
    int32_t _M0L6_2atmpS1264 = _M0L4baseS181 + _M0L6_2atmpS1267;
    int32_t _M0L6_2atmpS1266 = _M0Lm2loS174;
    int32_t _M0L6_2atmpS1265 = _M0L4baseS181 + _M0L6_2atmpS1266;
    moonbit_incref_cycle_free(_M0L3strS180);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS180,
                                                 .$1 = _M0L6_2atmpS1264,
                                                 .$2 = _M0L6_2atmpS1265};
  } else {
    int32_t _M0L6_2atmpS1271 = _M0Lm2loS174;
    int32_t _M0L6_2atmpS1268 = _M0L4baseS181 + _M0L6_2atmpS1271;
    int32_t _M0L6_2atmpS1270 = _M0Lm2hiS176;
    int32_t _M0L6_2atmpS1269 = _M0L4baseS181 + _M0L6_2atmpS1270;
    moonbit_incref_cycle_free(_M0L3strS180);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS180,
                                                 .$1 = _M0L6_2atmpS1268,
                                                 .$2 = _M0L6_2atmpS1269};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS171) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS170;
  int32_t _M0L6_2atmpS1239;
  int32_t _M0L6_2atmpS1238;
  int32_t _M0L6_2atmpS1241;
  int32_t _M0L6_2atmpS1240;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1237;
  moonbit_string_t _result_2355;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS170 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1239 = _M0IPC14byte4BytePB3Div3div(_M0L1bS171, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1238
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1239);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS170, _M0L6_2atmpS1238);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1241 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS171, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1240
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1241);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS170, _M0L6_2atmpS1240);
  _M0L6_2atmpS1237 = _M0L7_2aselfS170;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2355 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1237);
  moonbit_decref_cycle_free(_M0L6_2atmpS1237);
  return _result_2355;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS169) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS169 < 10) {
    int32_t _M0L6_2atmpS1234;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1234 = _M0IPC14byte4BytePB3Add3add(_M0L1iS169, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1234);
  } else {
    int32_t _M0L6_2atmpS1236;
    int32_t _M0L6_2atmpS1235;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1236 = _M0IPC14byte4BytePB3Add3add(_M0L1iS169, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1235 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1236, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1235);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS167,
  int32_t _M0L4thatS168
) {
  int32_t _M0L6_2atmpS1232;
  int32_t _M0L6_2atmpS1233;
  int32_t _M0L6_2atmpS1231;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1232 = (int32_t)_M0L4selfS167;
  _M0L6_2atmpS1233 = (int32_t)_M0L4thatS168;
  _M0L6_2atmpS1231 = _M0L6_2atmpS1232 - _M0L6_2atmpS1233;
  return _M0L6_2atmpS1231 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS165,
  int32_t _M0L4thatS166
) {
  int32_t _M0L6_2atmpS1229;
  int32_t _M0L6_2atmpS1230;
  int32_t _M0L6_2atmpS1228;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1229 = (int32_t)_M0L4selfS165;
  _M0L6_2atmpS1230 = (int32_t)_M0L4thatS166;
  _M0L6_2atmpS1228 = _M0L6_2atmpS1229 % _M0L6_2atmpS1230;
  return _M0L6_2atmpS1228 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS163,
  int32_t _M0L4thatS164
) {
  int32_t _M0L6_2atmpS1226;
  int32_t _M0L6_2atmpS1227;
  int32_t _M0L6_2atmpS1225;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1226 = (int32_t)_M0L4selfS163;
  _M0L6_2atmpS1227 = (int32_t)_M0L4thatS164;
  _M0L6_2atmpS1225 = _M0L6_2atmpS1226 / _M0L6_2atmpS1227;
  return _M0L6_2atmpS1225 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS161,
  int32_t _M0L4thatS162
) {
  int32_t _M0L6_2atmpS1223;
  int32_t _M0L6_2atmpS1224;
  int32_t _M0L6_2atmpS1222;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1223 = (int32_t)_M0L4selfS161;
  _M0L6_2atmpS1224 = (int32_t)_M0L4thatS162;
  _M0L6_2atmpS1222 = _M0L6_2atmpS1223 + _M0L6_2atmpS1224;
  return _M0L6_2atmpS1222 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS160) {
  int32_t _M0L6_2atmpS1221;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1221 = (int32_t)_M0L4selfS160;
  return _M0L6_2atmpS1221;
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
  int32_t _M0L3lenS1220;
  int32_t _M0L8requiredS156;
  uint16_t* _M0L4dataS1215;
  int32_t _M0L6_2atmpS1214;
  int32_t _if__result_2356;
  uint16_t* _M0L4dataS1216;
  int32_t _M0L3lenS1217;
  int32_t _M0L3lenS1219;
  int32_t _M0L6_2atmpS1218;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS154 = Moonbit_array_length(_M0L3strS155);
  if (_M0L8str__lenS154 == 0) {
    return 0;
  }
  _M0L3lenS1220 = _M0L4selfS157->$1;
  _M0L8requiredS156 = _M0L3lenS1220 + _M0L8str__lenS154;
  _M0L4dataS1215 = _M0L4selfS157->$0;
  _M0L6_2atmpS1214 = Moonbit_array_length(_M0L4dataS1215);
  if (_M0L8requiredS156 > _M0L6_2atmpS1214) {
    _if__result_2356 = 1;
  } else {
    int32_t _M0L3lenS1213 = _M0L4selfS157->$1;
    _if__result_2356 = _M0L8requiredS156 < _M0L3lenS1213;
  }
  if (_if__result_2356) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS157, _M0L8requiredS156);
  }
  _M0L4dataS1216 = _M0L4selfS157->$0;
  _M0L3lenS1217 = _M0L4selfS157->$1;
  moonbit_incref_cycle_free(_M0L4dataS1216);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1216, _M0L3lenS1217, _M0L3strS155, 0, _M0L8str__lenS154);
  moonbit_decref_cycle_free(_M0L4dataS1216);
  _M0L3lenS1219 = _M0L4selfS157->$1;
  _M0L6_2atmpS1218 = _M0L3lenS1219 + _M0L8str__lenS154;
  _M0L4selfS157->$1 = _M0L6_2atmpS1218;
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
      int32_t _M0L6_2atmpS1210 = _M0L3strS151[_M0L1iS148];
      int32_t _M0L6_2atmpS1211;
      int32_t _M0L6_2atmpS1212;
      _M0L4selfS150[_M0L1jS149] = _M0L6_2atmpS1210;
      _M0L6_2atmpS1211 = _M0L1iS148 + 1;
      _M0L6_2atmpS1212 = _M0L1jS149 + 1;
      _M0L1iS148 = _M0L6_2atmpS1211;
      _M0L1jS149 = _M0L6_2atmpS1212;
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
    int32_t _M0L3lenS1181 = _M0L4selfS143->$1;
    uint16_t* _M0L4dataS1183 = _M0L4selfS143->$0;
    int32_t _M0L6_2atmpS1182 = Moonbit_array_length(_M0L4dataS1183);
    uint16_t* _M0L4dataS1186;
    int32_t _M0L3lenS1187;
    int32_t _M0L6_2atmpS1188;
    int32_t _M0L3lenS1190;
    int32_t _M0L6_2atmpS1189;
    if (_M0L3lenS1181 >= _M0L6_2atmpS1182) {
      int32_t _M0L3lenS1185 = _M0L4selfS143->$1;
      int32_t _M0L6_2atmpS1184 = _M0L3lenS1185 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS143, _M0L6_2atmpS1184);
    }
    _M0L4dataS1186 = _M0L4selfS143->$0;
    _M0L3lenS1187 = _M0L4selfS143->$1;
    moonbit_incref_cycle_free(_M0L4dataS1186);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1188 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS141);
    if (
      _M0L3lenS1187 < 0
      || _M0L3lenS1187 >= Moonbit_array_length(_M0L4dataS1186)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1186[_M0L3lenS1187] = _M0L6_2atmpS1188;
    moonbit_decref_cycle_free(_M0L4dataS1186);
    _M0L3lenS1190 = _M0L4selfS143->$1;
    _M0L6_2atmpS1189 = _M0L3lenS1190 + 1;
    _M0L4selfS143->$1 = _M0L6_2atmpS1189;
  } else if (_M0L4codeS141 <= 1114111u) {
    uint16_t* _M0L4dataS1194 = _M0L4selfS143->$0;
    int32_t _M0L6_2atmpS1192 = Moonbit_array_length(_M0L4dataS1194);
    int32_t _M0L3lenS1193 = _M0L4selfS143->$1;
    int32_t _M0L6_2atmpS1191 = _M0L6_2atmpS1192 - _M0L3lenS1193;
    uint32_t _M0L4codeS144;
    uint16_t* _M0L4dataS1197;
    int32_t _M0L3lenS1198;
    uint32_t _M0L6_2atmpS1201;
    uint32_t _M0L6_2atmpS1200;
    int32_t _M0L6_2atmpS1199;
    uint16_t* _M0L4dataS1202;
    int32_t _M0L3lenS1207;
    int32_t _M0L6_2atmpS1203;
    uint32_t _M0L6_2atmpS1206;
    uint32_t _M0L6_2atmpS1205;
    int32_t _M0L6_2atmpS1204;
    int32_t _M0L3lenS1209;
    int32_t _M0L6_2atmpS1208;
    if (_M0L6_2atmpS1191 < 2) {
      int32_t _M0L3lenS1196 = _M0L4selfS143->$1;
      int32_t _M0L6_2atmpS1195 = _M0L3lenS1196 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS143, _M0L6_2atmpS1195);
    }
    _M0L4codeS144 = _M0L4codeS141 - 65536u;
    _M0L4dataS1197 = _M0L4selfS143->$0;
    _M0L3lenS1198 = _M0L4selfS143->$1;
    _M0L6_2atmpS1201 = _M0L4codeS144 >> 10;
    _M0L6_2atmpS1200 = 55296u + _M0L6_2atmpS1201;
    moonbit_incref_cycle_free(_M0L4dataS1197);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1199 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1200);
    if (
      _M0L3lenS1198 < 0
      || _M0L3lenS1198 >= Moonbit_array_length(_M0L4dataS1197)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1197[_M0L3lenS1198] = _M0L6_2atmpS1199;
    moonbit_decref_cycle_free(_M0L4dataS1197);
    _M0L4dataS1202 = _M0L4selfS143->$0;
    _M0L3lenS1207 = _M0L4selfS143->$1;
    _M0L6_2atmpS1203 = _M0L3lenS1207 + 1;
    _M0L6_2atmpS1206 = _M0L4codeS144 & 1023u;
    _M0L6_2atmpS1205 = 56320u + _M0L6_2atmpS1206;
    moonbit_incref_cycle_free(_M0L4dataS1202);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1204 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1205);
    if (
      _M0L6_2atmpS1203 < 0
      || _M0L6_2atmpS1203 >= Moonbit_array_length(_M0L4dataS1202)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1202[_M0L6_2atmpS1203] = _M0L6_2atmpS1204;
    moonbit_decref_cycle_free(_M0L4dataS1202);
    _M0L3lenS1209 = _M0L4selfS143->$1;
    _M0L6_2atmpS1208 = _M0L3lenS1209 + 2;
    _M0L4selfS143->$1 = _M0L6_2atmpS1208;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_25.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS138,
  int32_t _M0L8requiredS139
) {
  uint16_t* _M0L4dataS1180;
  int32_t _M0L6_2atmpS1178;
  int32_t _M0L3lenS1179;
  int32_t _M0L13new__capacityS137;
  uint16_t* _M0L4dataS1175;
  int32_t _M0L6_2atmpS1176;
  int32_t _M0L3lenS1177;
  uint16_t* _M0L9new__dataS140;
  uint16_t* _M0L6_2aoldS2231;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1180 = _M0L4selfS138->$0;
  _M0L6_2atmpS1178 = Moonbit_array_length(_M0L4dataS1180);
  _M0L3lenS1179 = _M0L4selfS138->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS137
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1178, _M0L3lenS1179, _M0L8requiredS139);
  _M0L4dataS1175 = _M0L4selfS138->$0;
  moonbit_incref_cycle_free(_M0L4dataS1175);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1176 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1177 = _M0L4selfS138->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS140
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1175, _M0L13new__capacityS137, _M0L6_2atmpS1176, _M0L3lenS1177, 0, 0);
  _M0L6_2aoldS2231 = _M0L4selfS138->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2231);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_26.data);
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
  int32_t _M0L6_2atmpS1174;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1174 = *(int32_t*)&_M0L4selfS130;
  return (uint16_t)_M0L6_2atmpS1174;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS129) {
  int32_t _M0L6_2atmpS1173;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1173 = _M0L4selfS129;
  return *(uint32_t*)&_M0L6_2atmpS1173;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS127
) {
  int32_t _M0L3lenS1164;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1164 = _M0L4selfS127->$1;
  if (_M0L3lenS1164 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1165 = _M0L4selfS127->$1;
    uint16_t* _M0L4dataS1167 = _M0L4selfS127->$0;
    int32_t _M0L6_2atmpS1166 = Moonbit_array_length(_M0L4dataS1167);
    if (_M0L3lenS1165 == _M0L6_2atmpS1166) {
      uint16_t* _M0L4dataS1168 = _M0L4selfS127->$0;
      moonbit_incref_cycle_free(_M0L4dataS1168);
      return _M0L4dataS1168;
    } else {
      uint16_t* _M0L4dataS1169 = _M0L4selfS127->$0;
      int32_t _M0L3lenS1170 = _M0L4selfS127->$1;
      int32_t _M0L6_2atmpS1171;
      int32_t _M0L3lenS1172;
      uint16_t* _M0L4dataS128;
      moonbit_incref_cycle_free(_M0L4dataS1169);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1171 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1172 = _M0L4selfS127->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS128
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1169, _M0L3lenS1170, _M0L6_2atmpS1171, _M0L3lenS1172, 0, 0);
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
  int32_t _if__result_2359;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS120 >= 0) {
    if (_M0L3lenS121 >= 0) {
      if (_M0L11src__offsetS122 >= 0) {
        if (_M0L11dst__offsetS123 >= 0) {
          int32_t _M0L6_2atmpS1160 = _M0L11src__offsetS122 + _M0L3lenS121;
          int32_t _M0L6_2atmpS1161 = Moonbit_array_length(_M0L3srcS124);
          if (_M0L6_2atmpS1160 <= _M0L6_2atmpS1161) {
            int32_t _M0L6_2atmpS1159 = _M0L11dst__offsetS123 + _M0L3lenS121;
            _if__result_2359 = _M0L6_2atmpS1159 <= _M0L13allocate__lenS120;
          } else {
            _if__result_2359 = 0;
          }
        } else {
          _if__result_2359 = 0;
        }
      } else {
        _if__result_2359 = 0;
      }
    } else {
      _if__result_2359 = 0;
    }
  } else {
    _if__result_2359 = 0;
  }
  if (_if__result_2359) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS124, _M0L13allocate__lenS120, _M0L4initS125, _M0L11src__offsetS122, _M0L11dst__offsetS123, _M0L3lenS121);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS126;
    int32_t _M0L6_2atmpS1163;
    moonbit_string_t _M0L6_2atmpS1162;
    uint16_t* _result_2360;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS126
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L13allocate__lenS120);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L11src__offsetS122);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L11dst__offsetS123);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L3lenS121);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_31.data);
    _M0L6_2atmpS1163 = Moonbit_array_length(_M0L3srcS124);
    moonbit_decref_cycle_free(_M0L3srcS124);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L6_2atmpS1163);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1162
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS126);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS126);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2360 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1162);
    moonbit_decref_cycle_free(_M0L6_2atmpS1162);
    return _result_2360;
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
  struct _M0TPB13StringBuilder* _block_2361;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS111 < 1) {
    _M0L7initialS110 = 1;
  } else {
    int32_t _M0L6_2atmpS1158 = _M0L10size__hintS111 + 1;
    _M0L7initialS110 = _M0L6_2atmpS1158 / 2;
  }
  _M0L4dataS112 = (uint16_t*)moonbit_make_string(_M0L7initialS110, 0);
  _block_2361
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2361)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 71, 0);
  _block_2361->$0 = _M0L4dataS112;
  _block_2361->$1 = 0;
  return _block_2361;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS109) {
  int32_t _M0L6_2atmpS1157;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1157 = (int32_t)_M0L4selfS109;
  return _M0L6_2atmpS1157;
}

uint8_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGbE(
  uint8_t* _M0L3srcS95,
  int32_t _M0L13allocate__lenS91,
  int32_t _M0L3lenS92,
  int32_t _M0L11src__offsetS93,
  int32_t _M0L11dst__offsetS94
) {
  int32_t _if__result_2362;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS91 >= 0) {
    if (_M0L3lenS92 >= 0) {
      if (_M0L11src__offsetS93 >= 0) {
        if (_M0L11dst__offsetS94 >= 0) {
          int32_t _M0L6_2atmpS1143 = _M0L11src__offsetS93 + _M0L3lenS92;
          int32_t _M0L6_2atmpS1144;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1144
          = _M0MPB18UninitializedArray6lengthGbE(_M0L3srcS95);
          if (_M0L6_2atmpS1143 <= _M0L6_2atmpS1144) {
            int32_t _M0L6_2atmpS1142 = _M0L11dst__offsetS94 + _M0L3lenS92;
            _if__result_2362 = _M0L6_2atmpS1142 <= _M0L13allocate__lenS91;
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
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGbE(_M0L3srcS95, _M0L13allocate__lenS91, _M0L11src__offsetS93, _M0L11dst__offsetS94, _M0L3lenS92);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS96;
    int32_t _M0L6_2atmpS1146;
    moonbit_string_t _M0L6_2atmpS1145;
    uint8_t* _result_2363;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS96
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L13allocate__lenS91);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L11src__offsetS93);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L11dst__offsetS94);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L3lenS92);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1146 = _M0MPB18UninitializedArray6lengthGbE(_M0L3srcS95);
    moonbit_decref_cycle_free(_M0L3srcS95);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L6_2atmpS1146);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1145
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS96);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS96);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2363
    = _M0FPC15abort5abortGRPB18UninitializedArrayGbEE(_M0L6_2atmpS1145);
    moonbit_decref_cycle_free(_M0L6_2atmpS1145);
    return _result_2363;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS101,
  int32_t _M0L13allocate__lenS97,
  int32_t _M0L3lenS98,
  int32_t _M0L11src__offsetS99,
  int32_t _M0L11dst__offsetS100
) {
  int32_t _if__result_2364;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS97 >= 0) {
    if (_M0L3lenS98 >= 0) {
      if (_M0L11src__offsetS99 >= 0) {
        if (_M0L11dst__offsetS100 >= 0) {
          int32_t _M0L6_2atmpS1148 = _M0L11src__offsetS99 + _M0L3lenS98;
          int32_t _M0L6_2atmpS1149;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1149
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS101);
          if (_M0L6_2atmpS1148 <= _M0L6_2atmpS1149) {
            int32_t _M0L6_2atmpS1147 = _M0L11dst__offsetS100 + _M0L3lenS98;
            _if__result_2364 = _M0L6_2atmpS1147 <= _M0L13allocate__lenS97;
          } else {
            _if__result_2364 = 0;
          }
        } else {
          _if__result_2364 = 0;
        }
      } else {
        _if__result_2364 = 0;
      }
    } else {
      _if__result_2364 = 0;
    }
  } else {
    _if__result_2364 = 0;
  }
  if (_if__result_2364) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS97, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS101, _M0L11src__offsetS99, _M0L11dst__offsetS100, _M0L3lenS98);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS102;
    int32_t _M0L6_2atmpS1151;
    moonbit_string_t _M0L6_2atmpS1150;
    moonbit_string_t* _result_2365;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS102
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L13allocate__lenS97);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L11src__offsetS99);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L11dst__offsetS100);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L3lenS98);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1151 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS101);
    moonbit_decref_cycle_free(_M0L3srcS101);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L6_2atmpS1151);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1150
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS102);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS102);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2365
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1150);
    moonbit_decref_cycle_free(_M0L6_2atmpS1150);
    return _result_2365;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS107,
  int32_t _M0L13allocate__lenS103,
  int32_t _M0L3lenS104,
  int32_t _M0L11src__offsetS105,
  int32_t _M0L11dst__offsetS106
) {
  int32_t _if__result_2366;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS103 >= 0) {
    if (_M0L3lenS104 >= 0) {
      if (_M0L11src__offsetS105 >= 0) {
        if (_M0L11dst__offsetS106 >= 0) {
          int32_t _M0L6_2atmpS1153 = _M0L11src__offsetS105 + _M0L3lenS104;
          int32_t _M0L6_2atmpS1154;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1154
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS107);
          if (_M0L6_2atmpS1153 <= _M0L6_2atmpS1154) {
            int32_t _M0L6_2atmpS1152 = _M0L11dst__offsetS106 + _M0L3lenS104;
            _if__result_2366 = _M0L6_2atmpS1152 <= _M0L13allocate__lenS103;
          } else {
            _if__result_2366 = 0;
          }
        } else {
          _if__result_2366 = 0;
        }
      } else {
        _if__result_2366 = 0;
      }
    } else {
      _if__result_2366 = 0;
    }
  } else {
    _if__result_2366 = 0;
  }
  if (_if__result_2366) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS103, 0, _M0L3srcS107, _M0L11src__offsetS105, _M0L11dst__offsetS106, _M0L3lenS104);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS108;
    int32_t _M0L6_2atmpS1156;
    moonbit_string_t _M0L6_2atmpS1155;
    struct _M0TUsiE** _result_2367;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS108
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L13allocate__lenS103);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L11src__offsetS105);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L11dst__offsetS106);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L3lenS104);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1156 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS107);
    moonbit_decref_cycle_free(_M0L3srcS107);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L6_2atmpS1156);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1155
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS108);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS108);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2367
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1155);
    moonbit_decref_cycle_free(_M0L6_2atmpS1155);
    return _result_2367;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS86,
  moonbit_string_t _M0L3objS85
) {
  struct _M0TPB6Logger _M0L6_2atmpS1139;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS86);
  _M0L6_2atmpS1139
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS86
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS85, _M0L6_2atmpS1139);
  if (_M0L6_2atmpS1139.$1) {
    moonbit_decref(_M0L6_2atmpS1139.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS88,
  int32_t _M0L3objS87
) {
  struct _M0TPB6Logger _M0L6_2atmpS1140;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS88);
  _M0L6_2atmpS1140
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS88
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS87, _M0L6_2atmpS1140);
  if (_M0L6_2atmpS1140.$1) {
    moonbit_decref(_M0L6_2atmpS1140.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS90,
  uint64_t _M0L3objS89
) {
  struct _M0TPB6Logger _M0L6_2atmpS1141;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS90);
  _M0L6_2atmpS1141
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS90
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS89, _M0L6_2atmpS1141);
  if (_M0L6_2atmpS1141.$1) {
    moonbit_decref(_M0L6_2atmpS1141.$1);
  }
  return 0;
}

uint8_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGbE(
  uint8_t* _M0L3srcS70,
  int32_t _M0L13allocate__lenS68,
  int32_t _M0L11src__offsetS71,
  int32_t _M0L11dst__offsetS69,
  int32_t _M0L9blit__lenS72
) {
  uint8_t* _M0L3dstS67;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS67 = (uint8_t*)moonbit_make_bytes_raw(_M0L13allocate__lenS68);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGbE(_M0L3dstS67, _M0L11dst__offsetS69, _M0L3srcS70, _M0L11src__offsetS71, _M0L9blit__lenS72);
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGbE(
  uint8_t* _M0L3dstS52,
  int32_t _M0L11dst__offsetS53,
  uint8_t* _M0L3srcS54,
  int32_t _M0L11src__offsetS55,
  int32_t _M0L3lenS56
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS54);
  moonbit_incref_cycle_free(_M0L3dstS52);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS52, _M0L11dst__offsetS53, _M0L3srcS54, _M0L11src__offsetS55, _M0L3lenS56, sizeof(uint8_t));
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
        int32_t _M0L6_2atmpS1103 = _M0L11dst__offsetS18 + _M0L1iS20;
        int32_t _M0L6_2atmpS1105 = _M0L11src__offsetS19 + _M0L1iS20;
        int32_t _M0L6_2atmpS1104;
        int32_t _M0L6_2atmpS1106;
        if (
          _M0L6_2atmpS1105 < 0
          || _M0L6_2atmpS1105 >= Moonbit_array_length(_M0L3srcS17)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1104 = (int32_t)_M0L3srcS17[_M0L6_2atmpS1105];
        if (
          _M0L6_2atmpS1103 < 0
          || _M0L6_2atmpS1103 >= Moonbit_array_length(_M0L3dstS16)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS16[_M0L6_2atmpS1103] = _M0L6_2atmpS1104;
        _M0L6_2atmpS1106 = _M0L1iS20 + 1;
        _M0L1iS20 = _M0L6_2atmpS1106;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS17);
        moonbit_decref_cycle_free(_M0L3dstS16);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1111 = _M0L3lenS21 - 1;
    int32_t _M0L1iS23 = _M0L6_2atmpS1111;
    while (1) {
      if (_M0L1iS23 >= 0) {
        int32_t _M0L6_2atmpS1107 = _M0L11dst__offsetS18 + _M0L1iS23;
        int32_t _M0L6_2atmpS1109 = _M0L11src__offsetS19 + _M0L1iS23;
        int32_t _M0L6_2atmpS1108;
        int32_t _M0L6_2atmpS1110;
        if (
          _M0L6_2atmpS1109 < 0
          || _M0L6_2atmpS1109 >= Moonbit_array_length(_M0L3srcS17)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1108 = (int32_t)_M0L3srcS17[_M0L6_2atmpS1109];
        if (
          _M0L6_2atmpS1107 < 0
          || _M0L6_2atmpS1107 >= Moonbit_array_length(_M0L3dstS16)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS16[_M0L6_2atmpS1107] = _M0L6_2atmpS1108;
        _M0L6_2atmpS1110 = _M0L1iS23 - 1;
        _M0L1iS23 = _M0L6_2atmpS1110;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGbEE(
  uint8_t* _M0L3dstS25,
  int32_t _M0L11dst__offsetS27,
  uint8_t* _M0L3srcS26,
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
        int32_t _M0L6_2atmpS1112 = _M0L11dst__offsetS27 + _M0L1iS29;
        int32_t _M0L6_2atmpS1114 = _M0L11src__offsetS28 + _M0L1iS29;
        int32_t _M0L6_2atmpS1113;
        int32_t _M0L6_2atmpS1115;
        if (
          _M0L6_2atmpS1114 < 0
          || _M0L6_2atmpS1114 >= Moonbit_array_length(_M0L3srcS26)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1113 = (int32_t)_M0L3srcS26[_M0L6_2atmpS1114];
        if (
          _M0L6_2atmpS1112 < 0
          || _M0L6_2atmpS1112 >= Moonbit_array_length(_M0L3dstS25)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS25[_M0L6_2atmpS1112] = _M0L6_2atmpS1113;
        _M0L6_2atmpS1115 = _M0L1iS29 + 1;
        _M0L1iS29 = _M0L6_2atmpS1115;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS26);
        moonbit_decref_cycle_free(_M0L3dstS25);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1120 = _M0L3lenS30 - 1;
    int32_t _M0L1iS32 = _M0L6_2atmpS1120;
    while (1) {
      if (_M0L1iS32 >= 0) {
        int32_t _M0L6_2atmpS1116 = _M0L11dst__offsetS27 + _M0L1iS32;
        int32_t _M0L6_2atmpS1118 = _M0L11src__offsetS28 + _M0L1iS32;
        int32_t _M0L6_2atmpS1117;
        int32_t _M0L6_2atmpS1119;
        if (
          _M0L6_2atmpS1118 < 0
          || _M0L6_2atmpS1118 >= Moonbit_array_length(_M0L3srcS26)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1117 = (int32_t)_M0L3srcS26[_M0L6_2atmpS1118];
        if (
          _M0L6_2atmpS1116 < 0
          || _M0L6_2atmpS1116 >= Moonbit_array_length(_M0L3dstS25)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS25[_M0L6_2atmpS1116] = _M0L6_2atmpS1117;
        _M0L6_2atmpS1119 = _M0L1iS32 - 1;
        _M0L1iS32 = _M0L6_2atmpS1119;
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
        int32_t _M0L6_2atmpS1121 = _M0L11dst__offsetS36 + _M0L1iS38;
        int32_t _M0L6_2atmpS1123 = _M0L11src__offsetS37 + _M0L1iS38;
        moonbit_string_t _M0L6_2atmpS1122;
        moonbit_string_t _M0L6_2aoldS2232;
        int32_t _M0L6_2atmpS1124;
        if (
          _M0L6_2atmpS1123 < 0
          || _M0L6_2atmpS1123 >= Moonbit_array_length(_M0L3srcS35)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1122 = (moonbit_string_t)_M0L3srcS35[_M0L6_2atmpS1123];
        if (
          _M0L6_2atmpS1121 < 0
          || _M0L6_2atmpS1121 >= Moonbit_array_length(_M0L3dstS34)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2232 = (moonbit_string_t)_M0L3dstS34[_M0L6_2atmpS1121];
        moonbit_incref_cycle_free(_M0L6_2atmpS1122);
        moonbit_decref_cycle_free(_M0L6_2aoldS2232);
        _M0L3dstS34[_M0L6_2atmpS1121] = _M0L6_2atmpS1122;
        _M0L6_2atmpS1124 = _M0L1iS38 + 1;
        _M0L1iS38 = _M0L6_2atmpS1124;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS35);
        moonbit_decref_cycle_free(_M0L3dstS34);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1129 = _M0L3lenS39 - 1;
    int32_t _M0L1iS41 = _M0L6_2atmpS1129;
    while (1) {
      if (_M0L1iS41 >= 0) {
        int32_t _M0L6_2atmpS1125 = _M0L11dst__offsetS36 + _M0L1iS41;
        int32_t _M0L6_2atmpS1127 = _M0L11src__offsetS37 + _M0L1iS41;
        moonbit_string_t _M0L6_2atmpS1126;
        moonbit_string_t _M0L6_2aoldS2233;
        int32_t _M0L6_2atmpS1128;
        if (
          _M0L6_2atmpS1127 < 0
          || _M0L6_2atmpS1127 >= Moonbit_array_length(_M0L3srcS35)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1126 = (moonbit_string_t)_M0L3srcS35[_M0L6_2atmpS1127];
        if (
          _M0L6_2atmpS1125 < 0
          || _M0L6_2atmpS1125 >= Moonbit_array_length(_M0L3dstS34)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2233 = (moonbit_string_t)_M0L3dstS34[_M0L6_2atmpS1125];
        moonbit_incref_cycle_free(_M0L6_2atmpS1126);
        moonbit_decref_cycle_free(_M0L6_2aoldS2233);
        _M0L3dstS34[_M0L6_2atmpS1125] = _M0L6_2atmpS1126;
        _M0L6_2atmpS1128 = _M0L1iS41 - 1;
        _M0L1iS41 = _M0L6_2atmpS1128;
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
        int32_t _M0L6_2atmpS1130 = _M0L11dst__offsetS45 + _M0L1iS47;
        int32_t _M0L6_2atmpS1132 = _M0L11src__offsetS46 + _M0L1iS47;
        struct _M0TUsiE* _M0L6_2atmpS1131;
        struct _M0TUsiE* _M0L6_2aoldS2234;
        int32_t _M0L6_2atmpS1133;
        if (
          _M0L6_2atmpS1132 < 0
          || _M0L6_2atmpS1132 >= Moonbit_array_length(_M0L3srcS44)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1131 = (struct _M0TUsiE*)_M0L3srcS44[_M0L6_2atmpS1132];
        if (
          _M0L6_2atmpS1130 < 0
          || _M0L6_2atmpS1130 >= Moonbit_array_length(_M0L3dstS43)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2234 = (struct _M0TUsiE*)_M0L3dstS43[_M0L6_2atmpS1130];
        if (_M0L6_2atmpS1131) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1131);
        }
        if (_M0L6_2aoldS2234) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2234);
        }
        _M0L3dstS43[_M0L6_2atmpS1130] = _M0L6_2atmpS1131;
        _M0L6_2atmpS1133 = _M0L1iS47 + 1;
        _M0L1iS47 = _M0L6_2atmpS1133;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS44);
        moonbit_decref_cycle_free(_M0L3dstS43);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1138 = _M0L3lenS48 - 1;
    int32_t _M0L1iS50 = _M0L6_2atmpS1138;
    while (1) {
      if (_M0L1iS50 >= 0) {
        int32_t _M0L6_2atmpS1134 = _M0L11dst__offsetS45 + _M0L1iS50;
        int32_t _M0L6_2atmpS1136 = _M0L11src__offsetS46 + _M0L1iS50;
        struct _M0TUsiE* _M0L6_2atmpS1135;
        struct _M0TUsiE* _M0L6_2aoldS2235;
        int32_t _M0L6_2atmpS1137;
        if (
          _M0L6_2atmpS1136 < 0
          || _M0L6_2atmpS1136 >= Moonbit_array_length(_M0L3srcS44)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1135 = (struct _M0TUsiE*)_M0L3srcS44[_M0L6_2atmpS1136];
        if (
          _M0L6_2atmpS1134 < 0
          || _M0L6_2atmpS1134 >= Moonbit_array_length(_M0L3dstS43)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2235 = (struct _M0TUsiE*)_M0L3dstS43[_M0L6_2atmpS1134];
        if (_M0L6_2atmpS1135) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1135);
        }
        if (_M0L6_2aoldS2235) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2235);
        }
        _M0L3dstS43[_M0L6_2atmpS1134] = _M0L6_2atmpS1135;
        _M0L6_2atmpS1137 = _M0L1iS50 - 1;
        _M0L1iS50 = _M0L6_2atmpS1137;
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

int32_t _M0MPB18UninitializedArray6lengthGbE(uint8_t* _M0L4selfS13) {
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
  _M0L10_2ax__6388S12.$0->$method_0(_M0L10_2ax__6388S12.$1, (moonbit_string_t)moonbit_string_literal_32.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S12, _M0L15_2a_2aarg__6389S11);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S12.$0->$method_0(_M0L10_2ax__6388S12.$1, (moonbit_string_t)moonbit_string_literal_33.data);
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

uint8_t* _M0FPC15abort5abortGRPB18UninitializedArrayGbEE(
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1072) {
  switch (Moonbit_object_tag(_M0L4_2aeS1072)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_34.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1072);
      break;
    }
    
    case 3: {
      return (moonbit_string_t)moonbit_string_literal_35.data;
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_36.data;
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_37.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1098,
  struct _M0TPB4Show _M0L8_2aparamS1097
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1096 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1098;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1096, _M0L8_2aparamS1097);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1095,
  struct _M0TPB4Show _M0L8_2aparamS1094
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1093 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1095;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1093, _M0L8_2aparamS1094);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1092,
  int32_t _M0L8_2aparamS1091
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1090 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1092;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1090, _M0L8_2aparamS1091);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1089,
  struct _M0TPC16string10StringView _M0L8_2aparamS1088
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1087 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1089;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1087, _M0L8_2aparamS1088);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1086,
  moonbit_string_t _M0L8_2aparamS1083,
  int32_t _M0L8_2aparamS1084,
  int32_t _M0L8_2aparamS1085
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1082 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1086;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1082, _M0L8_2aparamS1083, _M0L8_2aparamS1084, _M0L8_2aparamS1085);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1081,
  moonbit_string_t _M0L8_2aparamS1080
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1079 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1081;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1079, _M0L8_2aparamS1080);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_2376 = 9218868437227405311ll;
  int64_t _tmp_2377;
  int64_t _tmp_2378;
  int64_t _tmp_2379;
  int64_t _tmp_2380;
  _M0FPB18double__max__value = *(double*)&_tmp_2376;
  _tmp_2377 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_2377;
  _tmp_2378 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_2378;
  _tmp_2379 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_2379;
  _tmp_2380 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_2380;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1102;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1065;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1066;
  int32_t _M0L7_2abindS1067;
  struct _M0TUsiE** _M0L7_2abindS1068;
  int32_t _M0L6_2acntS2240;
  int32_t _M0L2__S1069;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1102
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1065
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1065)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 74, 0);
  _M0L12async__testsS1065->$0 = _M0L6_2atmpS1102;
  _M0L12async__testsS1065->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1066
  = _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1067 = _M0L7_2abindS1066->$1;
  _M0L7_2abindS1068 = _M0L7_2abindS1066->$0;
  _M0L6_2acntS2240
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1066));
  if (_M0L6_2acntS2240 > 1) {
    int32_t _M0L11_2anew__cntS2241 = _M0L6_2acntS2240 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1066), _M0L11_2anew__cntS2241);
    moonbit_incref_cycle_free(_M0L7_2abindS1068);
  } else if (_M0L6_2acntS2240 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1066);
  }
  _M0L2__S1069 = 0;
  while (1) {
    if (_M0L2__S1069 < _M0L7_2abindS1067) {
      struct _M0TUsiE* _M0L3argS1070 =
        (struct _M0TUsiE*)_M0L7_2abindS1068[_M0L2__S1069];
      moonbit_string_t _M0L6_2atmpS1099 = _M0L3argS1070->$0;
      int32_t _M0L6_2atmpS1100 = _M0L3argS1070->$1;
      int32_t _M0L6_2atmpS1101;
      moonbit_incref_cycle_free(_M0L6_2atmpS1099);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1065, _M0L6_2atmpS1099, _M0L6_2atmpS1100);
      moonbit_decref_cycle_free(_M0L6_2atmpS1099);
      _M0L6_2atmpS1101 = _M0L2__S1069 + 1;
      _M0L2__S1069 = _M0L6_2atmpS1101;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1068);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_layer\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples30poisson__layer__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1065);
  moonbit_decref_cycle_free(_M0L12async__testsS1065);
  moonbit_flush_cycles();
  return 0;
}