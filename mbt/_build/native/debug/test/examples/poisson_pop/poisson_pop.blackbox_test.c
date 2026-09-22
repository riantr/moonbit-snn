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

struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TWRPC15error5ErrorEs;

struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TPB4Show;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TP26RiantR8snn__mbt7Poisson;

struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c814;

struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c819;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGbE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0TPB5ArrayGsE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TWEu;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

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

struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
};

struct _M0TWRPC15error5ErrorEs {
  moonbit_string_t(* code)(struct _M0TWRPC15error5ErrorEs*, void*);
  
};

struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
};

struct _M0TP26RiantR8snn__mbt7Poisson {
  struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter* $0;
  int32_t $1;
  struct _M0TPB5ArrayGbE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* $4;
  
};

struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c814 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c819 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0TWuEu {
  int32_t(* code)(struct _M0TWuEu*, int32_t);
  
};

struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter {
  float $0;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
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

struct _M0TPB19MulShiftAll64Result {
  uint64_t $0;
  uint64_t $1;
  uint64_t $2;
  
};

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** $0;
  int32_t $1;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS826(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS819(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS814(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS791(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S784(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

struct _M0TP26RiantR8snn__mbt7Poisson* _M0MP26RiantR8snn__mbt7Poisson3new(
  int32_t,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter* _M0MP26RiantR8snn__mbt20PoissonHomoParameter3new(
  float
);

int32_t _M0FP26RiantR8snn__mbt13step__poisson(
  struct _M0TP26RiantR8snn__mbt7Poisson*,
  float
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t
);

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t);

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t);

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

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

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

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(int32_t);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(int32_t);

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

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_9 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[116]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 115, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 112, 111, 105, 115, 115, 111, 110, 
    95, 112, 111, 112, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 
    101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 
    116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 
    108, 74, 115, 69, 114, 114, 111, 114, 46, 77, 111, 111, 110, 66, 
    105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 
    116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 0
  };

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

struct { int32_t rc; uint32_t meta; uint16_t const data[118]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 117, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 112, 111, 105, 115, 115, 111, 110, 
    95, 112, 111, 112, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 
    101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 
    116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 
    108, 83, 107, 105, 112, 84, 101, 115, 116, 46, 77, 111, 111, 110, 
    66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 
    110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 
    116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS826$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS826
  };

uint32_t const moonbit_layout_table_data[48] =
  {
    sizeof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c814)
    / 4, 1,
    offsetof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c814, $1)
    / 4
    * 2,
    sizeof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c819)
    / 4, 1,
    offsetof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c819, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt7Poisson) / 4, 4,
    offsetof(struct _M0TP26RiantR8snn__mbt7Poisson, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt7Poisson, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt7Poisson, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt7Poisson, $4) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGbE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGbE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

float _M0FP26RiantR8snn__mbt2hz = 0x1.0624dd2f1a9fcp-10f;

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS1753
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS847,
  moonbit_string_t _M0L8filenameS816,
  int32_t _M0L5indexS818
) {
  struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c814* _closure_1775;
  struct _M0TWEu* _M0L13handle__startS814;
  struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c819* _closure_1776;
  struct _M0TWssbEu* _M0L14handle__resultS819;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS826;
  void* _M0L11_2atry__errS841;
  struct moonbit_result_0 _tmp_1778;
  int32_t _handle__error__result_1779;
  int32_t _M0L6_2atmpS1741;
  void* _M0L3errS842;
  moonbit_string_t _M0L4nameS844;
  struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS845;
  moonbit_string_t _M0L7_2anameS846;
  int32_t _M0L6_2acntS1769;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS816);
  _closure_1775
  = (struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c814*)moonbit_malloc(sizeof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c814));
  Moonbit_object_header(_closure_1775)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_1775->code
  = &_M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS814;
  _closure_1775->$0 = _M0L5indexS818;
  _closure_1775->$1 = _M0L8filenameS816;
  _M0L13handle__startS814 = (struct _M0TWEu*)_closure_1775;
  moonbit_incref_cycle_free(_M0L8filenameS816);
  _closure_1776
  = (struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c819*)moonbit_malloc(sizeof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c819));
  Moonbit_object_header(_closure_1776)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_1776->code
  = &_M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS819;
  _closure_1776->$0 = _M0L5indexS818;
  _closure_1776->$1 = _M0L8filenameS816;
  _M0L14handle__resultS819 = (struct _M0TWssbEu*)_closure_1776;
  _M0L17error__to__stringS826
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS826$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _tmp_1778
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS847, _M0L8filenameS816, _M0L5indexS818, _M0L13handle__startS814, _M0L14handle__resultS819, _M0L17error__to__stringS826);
  if (_tmp_1778.tag) {
    int32_t const _M0L5_2aokS1750 = _tmp_1778.data.ok;
    _handle__error__result_1779 = _M0L5_2aokS1750;
  } else {
    void* const _M0L6_2aerrS1751 = _tmp_1778.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS826);
    moonbit_decref_cycle_free(_M0L13handle__startS814);
    _M0L11_2atry__errS841 = _M0L6_2aerrS1751;
    goto join_840;
  }
  if (_handle__error__result_1779) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS826);
    moonbit_decref_cycle_free(_M0L13handle__startS814);
    _M0L6_2atmpS1741 = 1;
  } else {
    struct moonbit_result_0 _tmp_1780;
    int32_t _handle__error__result_1781;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
    _tmp_1780
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS847, _M0L8filenameS816, _M0L5indexS818, _M0L13handle__startS814, _M0L14handle__resultS819, _M0L17error__to__stringS826);
    if (_tmp_1780.tag) {
      int32_t const _M0L5_2aokS1748 = _tmp_1780.data.ok;
      _handle__error__result_1781 = _M0L5_2aokS1748;
    } else {
      void* const _M0L6_2aerrS1749 = _tmp_1780.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS826);
      moonbit_decref_cycle_free(_M0L13handle__startS814);
      _M0L11_2atry__errS841 = _M0L6_2aerrS1749;
      goto join_840;
    }
    if (_handle__error__result_1781) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS826);
      moonbit_decref_cycle_free(_M0L13handle__startS814);
      _M0L6_2atmpS1741 = 1;
    } else {
      struct moonbit_result_0 _tmp_1782;
      int32_t _handle__error__result_1783;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
      _tmp_1782
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS847, _M0L8filenameS816, _M0L5indexS818, _M0L13handle__startS814, _M0L14handle__resultS819, _M0L17error__to__stringS826);
      if (_tmp_1782.tag) {
        int32_t const _M0L5_2aokS1746 = _tmp_1782.data.ok;
        _handle__error__result_1783 = _M0L5_2aokS1746;
      } else {
        void* const _M0L6_2aerrS1747 = _tmp_1782.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS826);
        moonbit_decref_cycle_free(_M0L13handle__startS814);
        _M0L11_2atry__errS841 = _M0L6_2aerrS1747;
        goto join_840;
      }
      if (_handle__error__result_1783) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS826);
        moonbit_decref_cycle_free(_M0L13handle__startS814);
        _M0L6_2atmpS1741 = 1;
      } else {
        struct moonbit_result_0 _tmp_1784;
        int32_t _handle__error__result_1785;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
        _tmp_1784
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS847, _M0L8filenameS816, _M0L5indexS818, _M0L13handle__startS814, _M0L14handle__resultS819, _M0L17error__to__stringS826);
        if (_tmp_1784.tag) {
          int32_t const _M0L5_2aokS1744 = _tmp_1784.data.ok;
          _handle__error__result_1785 = _M0L5_2aokS1744;
        } else {
          void* const _M0L6_2aerrS1745 = _tmp_1784.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS826);
          moonbit_decref_cycle_free(_M0L13handle__startS814);
          _M0L11_2atry__errS841 = _M0L6_2aerrS1745;
          goto join_840;
        }
        if (_handle__error__result_1785) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS826);
          moonbit_decref_cycle_free(_M0L13handle__startS814);
          _M0L6_2atmpS1741 = 1;
        } else {
          struct moonbit_result_0 _tmp_1786;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
          _tmp_1786
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS847, _M0L8filenameS816, _M0L5indexS818, _M0L13handle__startS814, _M0L14handle__resultS819, _M0L17error__to__stringS826);
          moonbit_decref_cycle_free(_M0L13handle__startS814);
          moonbit_decref_cycle_free(_M0L17error__to__stringS826);
          if (_tmp_1786.tag) {
            int32_t const _M0L5_2aokS1742 = _tmp_1786.data.ok;
            _M0L6_2atmpS1741 = _M0L5_2aokS1742;
          } else {
            void* const _M0L6_2aerrS1743 = _tmp_1786.data.err;
            _M0L11_2atry__errS841 = _M0L6_2aerrS1743;
            goto join_840;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS1741) {
    void* _M0L131RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1752 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L131RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1752)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L131RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1752)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS841
    = _M0L131RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1752;
    goto join_840;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS819);
  }
  goto joinlet_1777;
  join_840:;
  _M0L3errS842 = _M0L11_2atry__errS841;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS845
  = (struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS842;
  _M0L7_2anameS846 = _M0L36_2aMoonBitTestDriverInternalSkipTestS845->$0;
  _M0L6_2acntS1769
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS845));
  if (_M0L6_2acntS1769 > 1) {
    int32_t _M0L11_2anew__cntS1770 = _M0L6_2acntS1769 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS845), _M0L11_2anew__cntS1770);
    moonbit_incref_cycle_free(_M0L7_2anameS846);
  } else if (_M0L6_2acntS1769 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS845);
  }
  _M0L4nameS844 = _M0L7_2anameS846;
  goto join_843;
  goto joinlet_1787;
  join_843:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS819(_M0L14handle__resultS819, _M0L4nameS844, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS819);
  moonbit_decref_cycle_free(_M0L4nameS844);
  joinlet_1787:;
  joinlet_1777:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS826(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS1740,
  void* _M0L3errS827
) {
  void* _M0L1eS829;
  moonbit_string_t _M0L1eS831;
  moonbit_string_t _result_1790;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS827)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS832 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS827;
      moonbit_string_t _M0L4_2aeS833 = _M0L10_2aFailureS832->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS833);
      _M0L1eS831 = _M0L4_2aeS833;
      goto join_830;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS834 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS827;
      moonbit_string_t _M0L4_2aeS835 = _M0L15_2aInspectErrorS834->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS835);
      _M0L1eS831 = _M0L4_2aeS835;
      goto join_830;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS836 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS827;
      moonbit_string_t _M0L4_2aeS837 = _M0L16_2aSnapshotErrorS836->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS837);
      _M0L1eS831 = _M0L4_2aeS837;
      goto join_830;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS838 =
        (struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS827;
      moonbit_string_t _M0L4_2aeS839 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS838->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS839);
      _M0L1eS831 = _M0L4_2aeS839;
      goto join_830;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS827);
      _M0L1eS829 = _M0L3errS827;
      goto join_828;
      break;
    }
  }
  join_830:;
  return _M0L1eS831;
  join_828:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _result_1790 = _M0FP15Error10to__string(_M0L1eS829);
  moonbit_decref_cycle_free(_M0L1eS829);
  return _result_1790;
}

int32_t _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS819(
  struct _M0TWssbEu* _M0L6_2aenvS1737,
  moonbit_string_t _M0L10__testnameS820,
  moonbit_string_t _M0L7messageS821,
  int32_t _M0L7skippedS822
) {
  struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c819* _M0L14_2acasted__envS1738;
  moonbit_string_t _M0L8filenameS816;
  int32_t _M0L5indexS818;
  moonbit_string_t _M0L10file__nameS823;
  moonbit_string_t _M0L7messageS824;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS825;
  moonbit_string_t _M0L6_2atmpS1739;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1738
  = (struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c819*)_M0L6_2aenvS1737;
  _M0L8filenameS816 = _M0L14_2acasted__envS1738->$1;
  _M0L5indexS818 = _M0L14_2acasted__envS1738->$0;
  if (!_M0L7skippedS822 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS823
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS816, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS824
  = _M0MPC16string6String14escape_2einner(_M0L7messageS821, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS825
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS825, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS825, _M0L10file__nameS823);
  moonbit_decref_cycle_free(_M0L10file__nameS823);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS825, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS825, _M0L5indexS818);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS825, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS825, _M0L7messageS824);
  moonbit_decref_cycle_free(_M0L7messageS824);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS825, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1739
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS825);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS825);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1739);
  moonbit_decref_cycle_free(_M0L6_2atmpS1739);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS814(
  struct _M0TWEu* _M0L6_2aenvS1734
) {
  struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c814* _M0L14_2acasted__envS1735;
  moonbit_string_t _M0L8filenameS816;
  int32_t _M0L5indexS818;
  moonbit_string_t _M0L10file__nameS815;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS817;
  moonbit_string_t _M0L6_2atmpS1736;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1735
  = (struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fpoisson__pop__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c814*)_M0L6_2aenvS1734;
  _M0L8filenameS816 = _M0L14_2acasted__envS1735->$1;
  _M0L5indexS818 = _M0L14_2acasted__envS1735->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS815
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS816, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS817
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS817, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS817, _M0L10file__nameS815);
  moonbit_decref_cycle_free(_M0L10file__nameS815);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS817, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS817, _M0L5indexS818);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS817, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1736
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS817);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS817);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1736);
  moonbit_decref_cycle_free(_M0L6_2atmpS1736);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S784;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS791;
  struct _M0TUsiE** _M0L6_2atmpS1733;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS798;
  moonbit_string_t* _M0L9cli__argsS799;
  moonbit_string_t _M0L6_2atmpS1732;
  moonbit_string_t _M0L6_2atmpS1731;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS800;
  int32_t _M0L7_2abindS801;
  moonbit_string_t* _M0L7_2abindS802;
  int32_t _M0L6_2acntS1771;
  int32_t _M0L2__S803;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S784 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS791 = 0;
  _M0L6_2atmpS1733 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS798
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS798)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS798->$0 = _M0L6_2atmpS1733;
  _M0L16file__and__indexS798->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS799
  = _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS799)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS1732 = (moonbit_string_t)_M0L9cli__argsS799[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS1732);
  moonbit_decref_cycle_free(_M0L9cli__argsS799);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1731
  = _M0MP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS1732);
  moonbit_decref_cycle_free(_M0L6_2atmpS1732);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS800
  = _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS791(_M0L51moonbit__test__driver__internal__split__mbt__stringS791, _M0L6_2atmpS1731, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS1731);
  _M0L7_2abindS801 = _M0L10test__argsS800->$1;
  _M0L7_2abindS802 = _M0L10test__argsS800->$0;
  _M0L6_2acntS1771
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS800));
  if (_M0L6_2acntS1771 > 1) {
    int32_t _M0L11_2anew__cntS1772 = _M0L6_2acntS1771 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS800), _M0L11_2anew__cntS1772);
    moonbit_incref_cycle_free(_M0L7_2abindS802);
  } else if (_M0L6_2acntS1771 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS800);
  }
  _M0L2__S803 = 0;
  while (1) {
    if (_M0L2__S803 < _M0L7_2abindS801) {
      moonbit_string_t _M0L3argS804 =
        (moonbit_string_t)_M0L7_2abindS802[_M0L2__S803];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS805;
      moonbit_string_t _M0L4fileS806;
      moonbit_string_t _M0L5rangeS807;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS808;
      moonbit_string_t _M0L6_2atmpS1729;
      int32_t _M0L5startS809;
      moonbit_string_t _M0L6_2atmpS1728;
      int32_t _M0L3endS810;
      int32_t _M0L1iS811;
      int32_t _M0L6_2atmpS1730;
      moonbit_incref_cycle_free(_M0L3argS804);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS805
      = _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS791(_M0L51moonbit__test__driver__internal__split__mbt__stringS791, _M0L3argS804, 58);
      moonbit_decref_cycle_free(_M0L3argS804);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS806
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS805, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS807
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS805, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS805);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS808
      = _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS791(_M0L51moonbit__test__driver__internal__split__mbt__stringS791, _M0L5rangeS807, 45);
      moonbit_decref_cycle_free(_M0L5rangeS807);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1729
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS808, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS809
      = _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S784(_M0L45moonbit__test__driver__internal__parse__int__S784, _M0L6_2atmpS1729);
      moonbit_decref_cycle_free(_M0L6_2atmpS1729);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1728
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS808, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS808);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS810
      = _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S784(_M0L45moonbit__test__driver__internal__parse__int__S784, _M0L6_2atmpS1728);
      moonbit_decref_cycle_free(_M0L6_2atmpS1728);
      _M0L1iS811 = _M0L5startS809;
      while (1) {
        if (_M0L1iS811 < _M0L3endS810) {
          struct _M0TUsiE* _M0L8_2atupleS1726;
          int32_t _M0L6_2atmpS1727;
          moonbit_incref_cycle_free(_M0L4fileS806);
          _M0L8_2atupleS1726
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS1726)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS1726->$0 = _M0L4fileS806;
          _M0L8_2atupleS1726->$1 = _M0L1iS811;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS798, _M0L8_2atupleS1726);
          _M0L6_2atmpS1727 = _M0L1iS811 + 1;
          _M0L1iS811 = _M0L6_2atmpS1727;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS806);
        }
        break;
      }
      _M0L6_2atmpS1730 = _M0L2__S803 + 1;
      _M0L2__S803 = _M0L6_2atmpS1730;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS802);
    }
    break;
  }
  return _M0L16file__and__indexS798;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS791(
  int32_t _M0L6_2aenvS1707,
  moonbit_string_t _M0L1sS792,
  int32_t _M0L3sepS793
) {
  moonbit_string_t* _M0L6_2atmpS1725;
  struct _M0TPB5ArrayGsE* _M0L3resS794;
  struct _M0TPB8MutLocalGiE* _M0L1iS795;
  struct _M0TPB8MutLocalGiE* _M0L5startS796;
  int32_t _M0L3valS1720;
  int32_t _M0L6_2atmpS1721;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1725 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS794
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS794)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS794->$0 = _M0L6_2atmpS1725;
  _M0L3resS794->$1 = 0;
  _M0L1iS795
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS795)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS795->$0 = 0;
  _M0L5startS796
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS796)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS796->$0 = 0;
  while (1) {
    int32_t _M0L3valS1708 = _M0L1iS795->$0;
    int32_t _M0L6_2atmpS1709 = Moonbit_array_length(_M0L1sS792);
    if (_M0L3valS1708 < _M0L6_2atmpS1709) {
      int32_t _M0L3valS1712 = _M0L1iS795->$0;
      int32_t _M0L6_2atmpS1711;
      int32_t _M0L6_2atmpS1710;
      int32_t _M0L3valS1719;
      int32_t _M0L6_2atmpS1718;
      if (
        _M0L3valS1712 < 0
        || _M0L3valS1712 >= Moonbit_array_length(_M0L1sS792)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1711 = _M0L1sS792[_M0L3valS1712];
      _M0L6_2atmpS1710 = _M0L6_2atmpS1711;
      if (_M0L6_2atmpS1710 == _M0L3sepS793) {
        int32_t _M0L3valS1714 = _M0L5startS796->$0;
        int32_t _M0L3valS1715 = _M0L1iS795->$0;
        moonbit_string_t _M0L6_2atmpS1713;
        int32_t _M0L3valS1717;
        int32_t _M0L6_2atmpS1716;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS1713
        = _M0MPC16string6String17unsafe__substring(_M0L1sS792, _M0L3valS1714, _M0L3valS1715);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS794, _M0L6_2atmpS1713);
        _M0L3valS1717 = _M0L1iS795->$0;
        _M0L6_2atmpS1716 = _M0L3valS1717 + 1;
        _M0L5startS796->$0 = _M0L6_2atmpS1716;
      }
      _M0L3valS1719 = _M0L1iS795->$0;
      _M0L6_2atmpS1718 = _M0L3valS1719 + 1;
      _M0L1iS795->$0 = _M0L6_2atmpS1718;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS795);
    }
    break;
  }
  _M0L3valS1720 = _M0L5startS796->$0;
  _M0L6_2atmpS1721 = Moonbit_array_length(_M0L1sS792);
  if (_M0L3valS1720 < _M0L6_2atmpS1721) {
    int32_t _M0L3valS1723 = _M0L5startS796->$0;
    int32_t _M0L6_2atmpS1724;
    moonbit_string_t _M0L6_2atmpS1722;
    moonbit_decref_cycle_free(_M0L5startS796);
    _M0L6_2atmpS1724 = Moonbit_array_length(_M0L1sS792);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS1722
    = _M0MPC16string6String17unsafe__substring(_M0L1sS792, _M0L3valS1723, _M0L6_2atmpS1724);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS794, _M0L6_2atmpS1722);
  } else {
    moonbit_decref_cycle_free(_M0L5startS796);
  }
  return _M0L3resS794;
}

int32_t _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S784(
  int32_t _M0L6_2aenvS1700,
  moonbit_string_t _M0L1sS785
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS786;
  int32_t _M0L3lenS787;
  int32_t _M0L7_2abindS788;
  int32_t _M0L1iS789;
  int32_t _result_1795;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS786
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS786)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS786->$0 = 0;
  _M0L3lenS787 = Moonbit_array_length(_M0L1sS785);
  _M0L7_2abindS788 = 0;
  _M0L1iS789 = _M0L7_2abindS788;
  while (1) {
    if (_M0L1iS789 < _M0L3lenS787) {
      int32_t _M0L3valS1705 = _M0L3resS786->$0;
      int32_t _M0L6_2atmpS1702 = _M0L3valS1705 * 10;
      int32_t _M0L6_2atmpS1704;
      int32_t _M0L6_2atmpS1703;
      int32_t _M0L6_2atmpS1701;
      int32_t _M0L6_2atmpS1706;
      if (_M0L1iS789 < 0 || _M0L1iS789 >= Moonbit_array_length(_M0L1sS785)) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1704 = _M0L1sS785[_M0L1iS789];
      _M0L6_2atmpS1703 = _M0L6_2atmpS1704 - 48;
      _M0L6_2atmpS1701 = _M0L6_2atmpS1702 + _M0L6_2atmpS1703;
      _M0L3resS786->$0 = _M0L6_2atmpS1701;
      _M0L6_2atmpS1706 = _M0L1iS789 + 1;
      _M0L1iS789 = _M0L6_2atmpS1706;
      continue;
    }
    break;
  }
  _result_1795 = _M0L3resS786->$0;
  moonbit_decref_cycle_free(_M0L3resS786);
  return _result_1795;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS783
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS783);
  return _M0L4selfS783;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S753,
  moonbit_string_t _M0L12_2adiscard__S754,
  int32_t _M0L12_2adiscard__S755,
  struct _M0TWEu* _M0L12_2adiscard__S756,
  struct _M0TWssbEu* _M0L12_2adiscard__S757,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S758
) {
  struct moonbit_result_0 _result_1796;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _result_1796.tag = 1;
  _result_1796.data.ok = 0;
  return _result_1796;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S759,
  moonbit_string_t _M0L12_2adiscard__S760,
  int32_t _M0L12_2adiscard__S761,
  struct _M0TWEu* _M0L12_2adiscard__S762,
  struct _M0TWssbEu* _M0L12_2adiscard__S763,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S764
) {
  struct moonbit_result_0 _result_1797;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _result_1797.tag = 1;
  _result_1797.data.ok = 0;
  return _result_1797;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S765,
  moonbit_string_t _M0L12_2adiscard__S766,
  int32_t _M0L12_2adiscard__S767,
  struct _M0TWEu* _M0L12_2adiscard__S768,
  struct _M0TWssbEu* _M0L12_2adiscard__S769,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S770
) {
  struct moonbit_result_0 _result_1798;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _result_1798.tag = 1;
  _result_1798.data.ok = 0;
  return _result_1798;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S771,
  moonbit_string_t _M0L12_2adiscard__S772,
  int32_t _M0L12_2adiscard__S773,
  struct _M0TWEu* _M0L12_2adiscard__S774,
  struct _M0TWssbEu* _M0L12_2adiscard__S775,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S776
) {
  struct moonbit_result_0 _result_1799;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _result_1799.tag = 1;
  _result_1799.data.ok = 0;
  return _result_1799;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S777,
  moonbit_string_t _M0L12_2adiscard__S778,
  int32_t _M0L12_2adiscard__S779,
  struct _M0TWEu* _M0L12_2adiscard__S780,
  struct _M0TWssbEu* _M0L12_2adiscard__S781,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S782
) {
  struct moonbit_result_0 _result_1800;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _result_1800.tag = 1;
  _result_1800.data.ok = 0;
  return _result_1800;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S752
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Poisson* _M0MP26RiantR8snn__mbt7Poisson3new(
  int32_t _M0L1nS735,
  float _M0L4rateS734,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS736
) {
  struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter* _M0L6_2atmpS1697;
  struct _M0TPB5ArrayGbE* _M0L6_2atmpS1698;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1699;
  struct _M0TP26RiantR8snn__mbt7Poisson* _block_1801;
  #line 37 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
  _M0L6_2atmpS1697
  = _M0MP26RiantR8snn__mbt20PoissonHomoParameter3new(_M0L4rateS734);
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
  _M0L6_2atmpS1698 = _M0MPC15array5Array4makeGbE(_M0L1nS735, 0);
  #line 41 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
  _M0L6_2atmpS1699 = _M0MPC15array5Array4makeGfE(_M0L1nS735, 0x0p+0f);
  moonbit_incref_cycle_free(_M0L3rngS736);
  _block_1801
  = (struct _M0TP26RiantR8snn__mbt7Poisson*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Poisson));
  Moonbit_object_header(_block_1801)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_1801->$0 = _M0L6_2atmpS1697;
  _block_1801->$1 = _M0L1nS735;
  _block_1801->$2 = _M0L6_2atmpS1698;
  _block_1801->$3 = _M0L6_2atmpS1699;
  _block_1801->$4 = _M0L3rngS736;
  return _block_1801;
}

struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter* _M0MP26RiantR8snn__mbt20PoissonHomoParameter3new(
  float _M0L4rateS733
) {
  struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter* _block_1802;
  #line 20 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
  _block_1802
  = (struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter));
  Moonbit_object_header(_block_1802)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1802->$0 = _M0L4rateS733;
  return _block_1802;
}

int32_t _M0FP26RiantR8snn__mbt13step__poisson(
  struct _M0TP26RiantR8snn__mbt7Poisson* _M0L1pS726,
  float _M0L2dtS728
) {
  int32_t _M0L1nS725;
  struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter* _M0L5paramS1696;
  float _M0L4rateS1695;
  float _M0L8rate__dtS727;
  int32_t _M0L7_2abindS729;
  int32_t _M0L1iS730;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
  _M0L1nS725 = _M0L1pS726->$1;
  _M0L5paramS1696 = _M0L1pS726->$0;
  _M0L4rateS1695 = _M0L5paramS1696->$0;
  _M0L8rate__dtS727 = _M0L4rateS1695 * _M0L2dtS728;
  _M0L7_2abindS729 = 0;
  _M0L1iS730 = _M0L7_2abindS729;
  while (1) {
    if (_M0L1iS730 < _M0L1nS725) {
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1693 = _M0L1pS726->$4;
      float _M0L1uS731;
      struct _M0TPB5ArrayGfE* _M0L9randcacheS1690;
      struct _M0TPB5ArrayGbE* _M0L4fireS1691;
      int32_t _M0L6_2atmpS1692;
      int32_t _M0L6_2atmpS1694;
      #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0L1uS731 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1693);
      _M0L9randcacheS1690 = _M0L1pS726->$3;
      #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0MPC15array5Array3setGfE(_M0L9randcacheS1690, _M0L1iS730, _M0L1uS731);
      _M0L4fireS1691 = _M0L1pS726->$2;
      _M0L6_2atmpS1692 = _M0L1uS731 < _M0L8rate__dtS727;
      #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1691, _M0L1iS730, _M0L6_2atmpS1692);
      _M0L6_2atmpS1694 = _M0L1iS730 + 1;
      _M0L1iS730 = _M0L6_2atmpS1694;
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
  uint64_t _M0L4seedS723
) {
  struct _M0TUmmmmE* _M0L1sS722;
  uint64_t _M0L6_2atmpS1689;
  struct _M0TUmmmmE* _M0L1tS724;
  uint64_t _M0L6_2atmpS1685;
  uint64_t _M0L6_2atmpS1686;
  uint64_t _M0L6_2atmpS1687;
  uint64_t _M0L6_2atmpS1688;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_1804;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS722 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS723);
  _M0L6_2atmpS1689 = _M0L1sS722->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS724 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS1689);
  _M0L6_2atmpS1685 = _M0L1sS722->$0;
  _M0L6_2atmpS1686 = _M0L1sS722->$1;
  _M0L6_2atmpS1687 = _M0L1sS722->$2;
  moonbit_decref_cycle_free(_M0L1sS722);
  _M0L6_2atmpS1688 = _M0L1tS724->$0;
  moonbit_decref_cycle_free(_M0L1tS724);
  _block_1804
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_1804)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1804->$0 = _M0L6_2atmpS1685;
  _block_1804->$1 = _M0L6_2atmpS1686;
  _block_1804->$2 = _M0L6_2atmpS1687;
  _block_1804->$3 = _M0L6_2atmpS1688;
  return _block_1804;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS714) {
  uint64_t _M0L2s1S713;
  uint64_t _M0L2z1S715;
  uint64_t _M0L2s2S716;
  uint64_t _M0L2z2S717;
  uint64_t _M0L2s3S718;
  uint64_t _M0L2z3S719;
  uint64_t _M0L2s4S720;
  uint64_t _M0L2z4S721;
  struct _M0TUmmmmE* _block_1805;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S713 = _M0L4seedS714 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S715 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S713);
  _M0L2s2S716 = _M0L2s1S713 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S717 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S716);
  _M0L2s3S718 = _M0L2s2S716 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S719 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S718);
  _M0L2s4S720 = _M0L2s3S718 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S721 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S720);
  _block_1805 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_1805)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1805->$0 = _M0L2z1S715;
  _block_1805->$1 = _M0L2z2S717;
  _block_1805->$2 = _M0L2z3S719;
  _block_1805->$3 = _M0L2z4S721;
  return _block_1805;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS711) {
  uint64_t _M0L6_2atmpS1684;
  uint64_t _M0L6_2atmpS1683;
  uint64_t _M0L1zS710;
  uint64_t _M0L6_2atmpS1682;
  uint64_t _M0L6_2atmpS1681;
  uint64_t _M0L1zS712;
  uint64_t _M0L6_2atmpS1680;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1684 = _M0L1zS711 >> 30;
  _M0L6_2atmpS1683 = _M0L1zS711 ^ _M0L6_2atmpS1684;
  _M0L1zS710 = _M0L6_2atmpS1683 * 13787848793156543929ull;
  _M0L6_2atmpS1682 = _M0L1zS710 >> 27;
  _M0L6_2atmpS1681 = _M0L1zS710 ^ _M0L6_2atmpS1682;
  _M0L1zS712 = _M0L6_2atmpS1681 * 10723151780598845931ull;
  _M0L6_2atmpS1680 = _M0L1zS712 >> 31;
  return _M0L1zS712 ^ _M0L6_2atmpS1680;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS708
) {
  uint32_t _M0L1uS707;
  uint32_t _M0L4bitsS709;
  double _M0L6_2atmpS1679;
  double _M0L6_2atmpS1678;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS707 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS708);
  _M0L4bitsS709 = _M0L1uS707 >> 8;
  _M0L6_2atmpS1679 = (double)_M0L4bitsS709;
  _M0L6_2atmpS1678 = _M0L6_2atmpS1679 * 0x1p-24;
  return (float)_M0L6_2atmpS1678;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS706
) {
  uint64_t _M0L1uS705;
  uint64_t _M0L6_2atmpS1677;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS705 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS706);
  _M0L6_2atmpS1677 = _M0L1uS705 >> 32;
  return (uint32_t)_M0L6_2atmpS1677;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS698
) {
  uint64_t _M0L2s0S697;
  uint64_t _M0L2s1S699;
  uint64_t _M0L2s2S700;
  uint64_t _M0L2s3S701;
  uint64_t _M0L3tmpS702;
  uint64_t _M0L6_2atmpS1676;
  uint64_t _M0L3resS703;
  uint64_t _M0L1tS704;
  uint64_t _M0L6_2atmpS1666;
  uint64_t _M0L6_2atmpS1667;
  uint64_t _M0L2s2S1669;
  uint64_t _M0L6_2atmpS1668;
  uint64_t _M0L2s3S1671;
  uint64_t _M0L6_2atmpS1670;
  uint64_t _M0L2s2S1673;
  uint64_t _M0L6_2atmpS1672;
  uint64_t _M0L2s3S1675;
  uint64_t _M0L6_2atmpS1674;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S697 = _M0L1rS698->$0;
  _M0L2s1S699 = _M0L1rS698->$1;
  _M0L2s2S700 = _M0L1rS698->$2;
  _M0L2s3S701 = _M0L1rS698->$3;
  _M0L3tmpS702 = _M0L2s0S697 + _M0L2s3S701;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1676 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS702, 23);
  _M0L3resS703 = _M0L6_2atmpS1676 + _M0L2s0S697;
  _M0L1tS704 = _M0L2s1S699 << 17;
  _M0L6_2atmpS1666 = _M0L2s2S700 ^ _M0L2s0S697;
  _M0L1rS698->$2 = _M0L6_2atmpS1666;
  _M0L6_2atmpS1667 = _M0L2s3S701 ^ _M0L2s1S699;
  _M0L1rS698->$3 = _M0L6_2atmpS1667;
  _M0L2s2S1669 = _M0L1rS698->$2;
  _M0L6_2atmpS1668 = _M0L2s1S699 ^ _M0L2s2S1669;
  _M0L1rS698->$1 = _M0L6_2atmpS1668;
  _M0L2s3S1671 = _M0L1rS698->$3;
  _M0L6_2atmpS1670 = _M0L2s0S697 ^ _M0L2s3S1671;
  _M0L1rS698->$0 = _M0L6_2atmpS1670;
  _M0L2s2S1673 = _M0L1rS698->$2;
  _M0L6_2atmpS1672 = _M0L2s2S1673 ^ _M0L1tS704;
  _M0L1rS698->$2 = _M0L6_2atmpS1672;
  _M0L2s3S1675 = _M0L1rS698->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1674 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S1675, 45);
  _M0L1rS698->$3 = _M0L6_2atmpS1674;
  return _M0L3resS703;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS695, int32_t _M0L1kS696) {
  uint64_t _M0L6_2atmpS1663;
  int32_t _M0L6_2atmpS1665;
  uint64_t _M0L6_2atmpS1664;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1663 = _M0L1xS695 << (_M0L1kS696 & 63);
  _M0L6_2atmpS1665 = 64 - _M0L1kS696;
  _M0L6_2atmpS1664 = _M0L1xS695 >> (_M0L6_2atmpS1665 & 63);
  return _M0L6_2atmpS1663 | _M0L6_2atmpS1664;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS694) {
  double _M0L6_2atmpS1662;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1662 = (double)_M0L4selfS694;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1662);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS693) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS693 != _M0L4selfS693) {
    return 0;
  } else if (_M0L4selfS693 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS693 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS693;
  }
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS684,
  int32_t _M0L4elemS686
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS683;
  int32_t _M0L1iS685;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS683 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS684);
  _M0L1iS685 = 0;
  while (1) {
    if (_M0L1iS685 < _M0L3lenS684) {
      uint8_t* _M0L3bufS1658 = _M0L3arrS683->$0;
      int32_t _M0L6_2atmpS1659;
      _M0L3bufS1658[_M0L1iS685] = _M0L4elemS686;
      _M0L6_2atmpS1659 = _M0L1iS685 + 1;
      _M0L1iS685 = _M0L6_2atmpS1659;
      continue;
    }
    break;
  }
  return _M0L3arrS683;
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS689,
  float _M0L4elemS691
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS688;
  int32_t _M0L1iS690;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS688 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS689);
  _M0L1iS690 = 0;
  while (1) {
    if (_M0L1iS690 < _M0L3lenS689) {
      float* _M0L3bufS1660 = _M0L3arrS688->$0;
      int32_t _M0L6_2atmpS1661;
      _M0L3bufS1660[_M0L1iS690] = _M0L4elemS691;
      _M0L6_2atmpS1661 = _M0L1iS690 + 1;
      _M0L1iS690 = _M0L6_2atmpS1661;
      continue;
    }
    break;
  }
  return _M0L3arrS688;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS676,
  int32_t _M0L5indexS677,
  float _M0L5valueS678
) {
  int32_t _M0L3lenS675;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS675 = _M0L4selfS676->$1;
  if (_M0L5indexS677 >= 0 && _M0L5indexS677 < _M0L3lenS675) {
    float* _M0L6_2atmpS1656;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1656 = _M0MPC15array5Array6bufferGfE(_M0L4selfS676);
    _M0L6_2atmpS1656[_M0L5indexS677] = _M0L5valueS678;
    moonbit_decref_cycle_free(_M0L6_2atmpS1656);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS680,
  int32_t _M0L5indexS681,
  int32_t _M0L5valueS682
) {
  int32_t _M0L3lenS679;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS679 = _M0L4selfS680->$1;
  if (_M0L5indexS681 >= 0 && _M0L5indexS681 < _M0L3lenS679) {
    uint8_t* _M0L6_2atmpS1657;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1657 = _M0MPC15array5Array6bufferGbE(_M0L4selfS680);
    _M0L6_2atmpS1657[_M0L5indexS681] = _M0L5valueS682;
    moonbit_decref_cycle_free(_M0L6_2atmpS1657);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS670,
  int32_t _M0L5indexS671
) {
  int32_t _M0L3lenS669;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS669 = _M0L4selfS670->$1;
  if (_M0L5indexS671 >= 0 && _M0L5indexS671 < _M0L3lenS669) {
    uint8_t* _M0L6_2atmpS1654;
    int32_t _result_1808;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1654 = _M0MPC15array5Array6bufferGbE(_M0L4selfS670);
    _result_1808 = (int32_t)_M0L6_2atmpS1654[_M0L5indexS671];
    moonbit_decref_cycle_free(_M0L6_2atmpS1654);
    return _result_1808;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS673,
  int32_t _M0L5indexS674
) {
  int32_t _M0L3lenS672;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS672 = _M0L4selfS673->$1;
  if (_M0L5indexS674 >= 0 && _M0L5indexS674 < _M0L3lenS672) {
    moonbit_string_t* _M0L6_2atmpS1655;
    moonbit_string_t _M0L6_2atmpS1754;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1655 = _M0MPC15array5Array6bufferGsE(_M0L4selfS673);
    _M0L6_2atmpS1754 = (moonbit_string_t)_M0L6_2atmpS1655[_M0L5indexS674];
    moonbit_incref_cycle_free(_M0L6_2atmpS1754);
    moonbit_decref_cycle_free(_M0L6_2atmpS1655);
    return _M0L6_2atmpS1754;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS668) {
  moonbit_string_t _M0L6_2atmpS1653;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1653 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS668);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1653);
  moonbit_decref_cycle_free(_M0L6_2atmpS1653);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS667) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS667);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS652) {
  uint64_t _M0L4bitsS655;
  uint64_t _M0L6_2atmpS1652;
  uint64_t _M0L6_2atmpS1651;
  int32_t _M0L8ieeeSignS656;
  uint64_t _M0L12ieeeMantissaS657;
  uint64_t _M0L6_2atmpS1650;
  uint64_t _M0L6_2atmpS1649;
  int32_t _M0L12ieeeExponentS658;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS659;
  struct _M0TPB17FloatingDecimal64* _M0L1vS660;
  moonbit_string_t _result_1810;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS652 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  if (_M0L3valS652 >= -0x1p+53 && _M0L3valS652 <= 0x1p+53) {
    if (_M0L3valS652 >= -0x1p+31 && _M0L3valS652 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS653;
      double _M0L6_2atmpS1638;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS653 = _M0MPC16double6Double7to__int(_M0L3valS652);
      _M0L6_2atmpS1638 = (double)_M0L1iS653;
      if (_M0L6_2atmpS1638 == _M0L3valS652) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS653, 10);
      }
    } else {
      int64_t _M0L1iS654;
      double _M0L6_2atmpS1639;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS654 = _M0MPC16double6Double9to__int64(_M0L3valS652);
      _M0L6_2atmpS1639 = (double)_M0L1iS654;
      if (_M0L6_2atmpS1639 == _M0L3valS652) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS654, 10);
      }
    }
  }
  _M0L4bitsS655 = *(int64_t*)&_M0L3valS652;
  _M0L6_2atmpS1652 = _M0L4bitsS655 >> 63;
  _M0L6_2atmpS1651 = _M0L6_2atmpS1652 & 1ull;
  _M0L8ieeeSignS656 = _M0L6_2atmpS1651 != 0ull;
  _M0L12ieeeMantissaS657 = _M0L4bitsS655 & 4503599627370495ull;
  _M0L6_2atmpS1650 = _M0L4bitsS655 >> 52;
  _M0L6_2atmpS1649 = _M0L6_2atmpS1650 & 2047ull;
  _M0L12ieeeExponentS658 = (int32_t)_M0L6_2atmpS1649;
  if (
    _M0L12ieeeExponentS658 == 2047
    || _M0L12ieeeExponentS658 == 0 && _M0L12ieeeMantissaS657 == 0ull
  ) {
    int32_t _M0L6_2atmpS1640 = _M0L12ieeeExponentS658 != 0;
    int32_t _M0L6_2atmpS1641 = _M0L12ieeeMantissaS657 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS656, _M0L6_2atmpS1640, _M0L6_2atmpS1641);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS659
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS657, _M0L12ieeeExponentS658);
  if (_M0L7_2abindS659 == 0) {
    uint32_t _M0L6_2atmpS1642;
    if (_M0L7_2abindS659) {
      moonbit_decref_cycle_free(_M0L7_2abindS659);
    }
    _M0L6_2atmpS1642 = *(uint32_t*)&_M0L12ieeeExponentS658;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS660 = _M0FPB3d2d(_M0L12ieeeMantissaS657, _M0L6_2atmpS1642);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS661 = _M0L7_2abindS659;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS662 = _M0L7_2aSomeS661;
    struct _M0TPB17FloatingDecimal64* _M0L1xS663 = _M0L4_2afS662;
    while (1) {
      uint64_t _M0L8mantissaS1648 = _M0L1xS663->$0;
      uint64_t _M0L1qS664 = _M0L8mantissaS1648 / 10ull;
      uint64_t _M0L8mantissaS1646 = _M0L1xS663->$0;
      uint64_t _M0L6_2atmpS1647 = 10ull * _M0L1qS664;
      uint64_t _M0L1rS665 = _M0L8mantissaS1646 - _M0L6_2atmpS1647;
      int32_t _M0L8exponentS1645;
      int32_t _M0L6_2atmpS1644;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1643;
      if (_M0L1rS665 != 0ull) {
        _M0L1vS660 = _M0L1xS663;
        break;
      }
      _M0L8exponentS1645 = _M0L1xS663->$1;
      moonbit_decref_cycle_free(_M0L1xS663);
      _M0L6_2atmpS1644 = _M0L8exponentS1645 + 1;
      _M0L6_2atmpS1643
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1643)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1643->$0 = _M0L1qS664;
      _M0L6_2atmpS1643->$1 = _M0L6_2atmpS1644;
      _M0L1xS663 = _M0L6_2atmpS1643;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1810 = _M0FPB9to__chars(_M0L1vS660, _M0L8ieeeSignS656);
  moonbit_decref_cycle_free(_M0L1vS660);
  return _result_1810;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS647,
  int32_t _M0L12ieeeExponentS649
) {
  uint64_t _M0L2m2S646;
  int32_t _M0L6_2atmpS1637;
  int32_t _M0L2e2S648;
  int32_t _M0L6_2atmpS1636;
  uint64_t _M0L6_2atmpS1635;
  uint64_t _M0L4maskS650;
  uint64_t _M0L8fractionS651;
  int32_t _M0L6_2atmpS1634;
  uint64_t _M0L6_2atmpS1633;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1632;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S646 = 4503599627370496ull | _M0L12ieeeMantissaS647;
  _M0L6_2atmpS1637 = _M0L12ieeeExponentS649 - 1023;
  _M0L2e2S648 = _M0L6_2atmpS1637 - 52;
  if (_M0L2e2S648 > 0) {
    return 0;
  }
  if (_M0L2e2S648 < -52) {
    return 0;
  }
  _M0L6_2atmpS1636 = -_M0L2e2S648;
  _M0L6_2atmpS1635 = 1ull << (_M0L6_2atmpS1636 & 63);
  _M0L4maskS650 = _M0L6_2atmpS1635 - 1ull;
  _M0L8fractionS651 = _M0L2m2S646 & _M0L4maskS650;
  if (_M0L8fractionS651 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1634 = -_M0L2e2S648;
  _M0L6_2atmpS1633 = _M0L2m2S646 >> (_M0L6_2atmpS1634 & 63);
  _M0L6_2atmpS1632
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1632)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1632->$0 = _M0L6_2atmpS1633;
  _M0L6_2atmpS1632->$1 = 0;
  return _M0L6_2atmpS1632;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS614,
  int32_t _M0L4signS612
) {
  moonbit_bytes_t _M0L6resultS610;
  int32_t _M0Lm5indexS611;
  uint64_t _M0L6outputS613;
  int32_t _M0L7olengthS615;
  int32_t _M0L8exponentS1631;
  int32_t _M0L6_2atmpS1630;
  int32_t _M0Lm3expS616;
  int32_t _M0L6_2atmpS1629;
  int32_t _M0L6_2atmpS1627;
  int32_t _M0L18scientificNotationS617;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS610 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS611 = 0;
  if (_M0L4signS612) {
    int32_t _M0L6_2atmpS1501 = _M0Lm5indexS611;
    int32_t _M0L6_2atmpS1502;
    if (
      _M0L6_2atmpS1501 < 0
      || _M0L6_2atmpS1501 >= Moonbit_array_length(_M0L6resultS610)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS610[_M0L6_2atmpS1501] = 45;
    _M0L6_2atmpS1502 = _M0Lm5indexS611;
    _M0Lm5indexS611 = _M0L6_2atmpS1502 + 1;
  }
  _M0L6outputS613 = _M0L1vS614->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS615 = _M0FPB17decimal__length17(_M0L6outputS613);
  _M0L8exponentS1631 = _M0L1vS614->$1;
  _M0L6_2atmpS1630 = _M0L8exponentS1631 + _M0L7olengthS615;
  _M0Lm3expS616 = _M0L6_2atmpS1630 - 1;
  _M0L6_2atmpS1629 = _M0Lm3expS616;
  if (_M0L6_2atmpS1629 >= -6) {
    int32_t _M0L6_2atmpS1628 = _M0Lm3expS616;
    _M0L6_2atmpS1627 = _M0L6_2atmpS1628 < 21;
  } else {
    _M0L6_2atmpS1627 = 0;
  }
  _M0L18scientificNotationS617 = !_M0L6_2atmpS1627;
  if (_M0L18scientificNotationS617) {
    int32_t _M0L7_2abindS618 = _M0L7olengthS615 - 1;
    uint64_t _M0L6outputS619;
    int32_t _M0L1iS620 = 0;
    uint64_t _M0L6outputS621 = _M0L6outputS613;
    int32_t _M0L6_2atmpS1503;
    int32_t _M0L6_2atmpS1507;
    int32_t _M0L6_2atmpS1506;
    int32_t _M0L6_2atmpS1505;
    int32_t _M0L6_2atmpS1504;
    int32_t _M0L6_2atmpS1511;
    int32_t _M0L6_2atmpS1512;
    int32_t _M0L6_2atmpS1513;
    int32_t _M0L6_2atmpS1514;
    int32_t _M0L6_2atmpS1515;
    int32_t _M0L6_2atmpS1521;
    int32_t _M0L6_2atmpS1554;
    moonbit_string_t _result_1812;
    while (1) {
      if (_M0L1iS620 < _M0L7_2abindS618) {
        uint64_t _M0L1cS622 = _M0L6outputS621 % 10ull;
        int32_t _M0L6_2atmpS1560 = _M0Lm5indexS611;
        int32_t _M0L6_2atmpS1559 = _M0L6_2atmpS1560 + _M0L7olengthS615;
        int32_t _M0L6_2atmpS1555 = _M0L6_2atmpS1559 - _M0L1iS620;
        int32_t _M0L6_2atmpS1558 = (int32_t)_M0L1cS622;
        int32_t _M0L6_2atmpS1557 = 48 + _M0L6_2atmpS1558;
        int32_t _M0L6_2atmpS1556 = _M0L6_2atmpS1557 & 0xff;
        int32_t _M0L6_2atmpS1561;
        uint64_t _M0L6_2atmpS1562;
        if (
          _M0L6_2atmpS1555 < 0
          || _M0L6_2atmpS1555 >= Moonbit_array_length(_M0L6resultS610)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS610[_M0L6_2atmpS1555] = _M0L6_2atmpS1556;
        _M0L6_2atmpS1561 = _M0L1iS620 + 1;
        _M0L6_2atmpS1562 = _M0L6outputS621 / 10ull;
        _M0L1iS620 = _M0L6_2atmpS1561;
        _M0L6outputS621 = _M0L6_2atmpS1562;
        continue;
      } else {
        _M0L6outputS619 = _M0L6outputS621;
      }
      break;
    }
    _M0L6_2atmpS1503 = _M0Lm5indexS611;
    _M0L6_2atmpS1507 = (int32_t)_M0L6outputS619;
    _M0L6_2atmpS1506 = _M0L6_2atmpS1507 % 10;
    _M0L6_2atmpS1505 = 48 + _M0L6_2atmpS1506;
    _M0L6_2atmpS1504 = _M0L6_2atmpS1505 & 0xff;
    if (
      _M0L6_2atmpS1503 < 0
      || _M0L6_2atmpS1503 >= Moonbit_array_length(_M0L6resultS610)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS610[_M0L6_2atmpS1503] = _M0L6_2atmpS1504;
    if (_M0L7olengthS615 > 1) {
      int32_t _M0L6_2atmpS1509 = _M0Lm5indexS611;
      int32_t _M0L6_2atmpS1508 = _M0L6_2atmpS1509 + 1;
      if (
        _M0L6_2atmpS1508 < 0
        || _M0L6_2atmpS1508 >= Moonbit_array_length(_M0L6resultS610)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS610[_M0L6_2atmpS1508] = 46;
    } else {
      int32_t _M0L6_2atmpS1510 = _M0Lm5indexS611;
      _M0Lm5indexS611 = _M0L6_2atmpS1510 - 1;
    }
    _M0L6_2atmpS1511 = _M0Lm5indexS611;
    _M0L6_2atmpS1512 = _M0L7olengthS615 + 1;
    _M0Lm5indexS611 = _M0L6_2atmpS1511 + _M0L6_2atmpS1512;
    _M0L6_2atmpS1513 = _M0Lm5indexS611;
    if (
      _M0L6_2atmpS1513 < 0
      || _M0L6_2atmpS1513 >= Moonbit_array_length(_M0L6resultS610)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS610[_M0L6_2atmpS1513] = 101;
    _M0L6_2atmpS1514 = _M0Lm5indexS611;
    _M0Lm5indexS611 = _M0L6_2atmpS1514 + 1;
    _M0L6_2atmpS1515 = _M0Lm3expS616;
    if (_M0L6_2atmpS1515 < 0) {
      int32_t _M0L6_2atmpS1516 = _M0Lm5indexS611;
      int32_t _M0L6_2atmpS1517;
      int32_t _M0L6_2atmpS1518;
      if (
        _M0L6_2atmpS1516 < 0
        || _M0L6_2atmpS1516 >= Moonbit_array_length(_M0L6resultS610)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS610[_M0L6_2atmpS1516] = 45;
      _M0L6_2atmpS1517 = _M0Lm5indexS611;
      _M0Lm5indexS611 = _M0L6_2atmpS1517 + 1;
      _M0L6_2atmpS1518 = _M0Lm3expS616;
      _M0Lm3expS616 = -_M0L6_2atmpS1518;
    } else {
      int32_t _M0L6_2atmpS1519 = _M0Lm5indexS611;
      int32_t _M0L6_2atmpS1520;
      if (
        _M0L6_2atmpS1519 < 0
        || _M0L6_2atmpS1519 >= Moonbit_array_length(_M0L6resultS610)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS610[_M0L6_2atmpS1519] = 43;
      _M0L6_2atmpS1520 = _M0Lm5indexS611;
      _M0Lm5indexS611 = _M0L6_2atmpS1520 + 1;
    }
    _M0L6_2atmpS1521 = _M0Lm3expS616;
    if (_M0L6_2atmpS1521 >= 100) {
      int32_t _M0L6_2atmpS1537 = _M0Lm3expS616;
      int32_t _M0L1aS624 = _M0L6_2atmpS1537 / 100;
      int32_t _M0L6_2atmpS1536 = _M0Lm3expS616;
      int32_t _M0L6_2atmpS1535 = _M0L6_2atmpS1536 / 10;
      int32_t _M0L1bS625 = _M0L6_2atmpS1535 % 10;
      int32_t _M0L6_2atmpS1534 = _M0Lm3expS616;
      int32_t _M0L1cS626 = _M0L6_2atmpS1534 % 10;
      int32_t _M0L6_2atmpS1522 = _M0Lm5indexS611;
      int32_t _M0L6_2atmpS1524 = 48 + _M0L1aS624;
      int32_t _M0L6_2atmpS1523 = _M0L6_2atmpS1524 & 0xff;
      int32_t _M0L6_2atmpS1528;
      int32_t _M0L6_2atmpS1525;
      int32_t _M0L6_2atmpS1527;
      int32_t _M0L6_2atmpS1526;
      int32_t _M0L6_2atmpS1532;
      int32_t _M0L6_2atmpS1529;
      int32_t _M0L6_2atmpS1531;
      int32_t _M0L6_2atmpS1530;
      int32_t _M0L6_2atmpS1533;
      if (
        _M0L6_2atmpS1522 < 0
        || _M0L6_2atmpS1522 >= Moonbit_array_length(_M0L6resultS610)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS610[_M0L6_2atmpS1522] = _M0L6_2atmpS1523;
      _M0L6_2atmpS1528 = _M0Lm5indexS611;
      _M0L6_2atmpS1525 = _M0L6_2atmpS1528 + 1;
      _M0L6_2atmpS1527 = 48 + _M0L1bS625;
      _M0L6_2atmpS1526 = _M0L6_2atmpS1527 & 0xff;
      if (
        _M0L6_2atmpS1525 < 0
        || _M0L6_2atmpS1525 >= Moonbit_array_length(_M0L6resultS610)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS610[_M0L6_2atmpS1525] = _M0L6_2atmpS1526;
      _M0L6_2atmpS1532 = _M0Lm5indexS611;
      _M0L6_2atmpS1529 = _M0L6_2atmpS1532 + 2;
      _M0L6_2atmpS1531 = 48 + _M0L1cS626;
      _M0L6_2atmpS1530 = _M0L6_2atmpS1531 & 0xff;
      if (
        _M0L6_2atmpS1529 < 0
        || _M0L6_2atmpS1529 >= Moonbit_array_length(_M0L6resultS610)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS610[_M0L6_2atmpS1529] = _M0L6_2atmpS1530;
      _M0L6_2atmpS1533 = _M0Lm5indexS611;
      _M0Lm5indexS611 = _M0L6_2atmpS1533 + 3;
    } else {
      int32_t _M0L6_2atmpS1538 = _M0Lm3expS616;
      if (_M0L6_2atmpS1538 >= 10) {
        int32_t _M0L6_2atmpS1548 = _M0Lm3expS616;
        int32_t _M0L1aS627 = _M0L6_2atmpS1548 / 10;
        int32_t _M0L6_2atmpS1547 = _M0Lm3expS616;
        int32_t _M0L1bS628 = _M0L6_2atmpS1547 % 10;
        int32_t _M0L6_2atmpS1539 = _M0Lm5indexS611;
        int32_t _M0L6_2atmpS1541 = 48 + _M0L1aS627;
        int32_t _M0L6_2atmpS1540 = _M0L6_2atmpS1541 & 0xff;
        int32_t _M0L6_2atmpS1545;
        int32_t _M0L6_2atmpS1542;
        int32_t _M0L6_2atmpS1544;
        int32_t _M0L6_2atmpS1543;
        int32_t _M0L6_2atmpS1546;
        if (
          _M0L6_2atmpS1539 < 0
          || _M0L6_2atmpS1539 >= Moonbit_array_length(_M0L6resultS610)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS610[_M0L6_2atmpS1539] = _M0L6_2atmpS1540;
        _M0L6_2atmpS1545 = _M0Lm5indexS611;
        _M0L6_2atmpS1542 = _M0L6_2atmpS1545 + 1;
        _M0L6_2atmpS1544 = 48 + _M0L1bS628;
        _M0L6_2atmpS1543 = _M0L6_2atmpS1544 & 0xff;
        if (
          _M0L6_2atmpS1542 < 0
          || _M0L6_2atmpS1542 >= Moonbit_array_length(_M0L6resultS610)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS610[_M0L6_2atmpS1542] = _M0L6_2atmpS1543;
        _M0L6_2atmpS1546 = _M0Lm5indexS611;
        _M0Lm5indexS611 = _M0L6_2atmpS1546 + 2;
      } else {
        int32_t _M0L6_2atmpS1549 = _M0Lm5indexS611;
        int32_t _M0L6_2atmpS1552 = _M0Lm3expS616;
        int32_t _M0L6_2atmpS1551 = 48 + _M0L6_2atmpS1552;
        int32_t _M0L6_2atmpS1550 = _M0L6_2atmpS1551 & 0xff;
        int32_t _M0L6_2atmpS1553;
        if (
          _M0L6_2atmpS1549 < 0
          || _M0L6_2atmpS1549 >= Moonbit_array_length(_M0L6resultS610)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS610[_M0L6_2atmpS1549] = _M0L6_2atmpS1550;
        _M0L6_2atmpS1553 = _M0Lm5indexS611;
        _M0Lm5indexS611 = _M0L6_2atmpS1553 + 1;
      }
    }
    _M0L6_2atmpS1554 = _M0Lm5indexS611;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1812
    = _M0FPB19string__from__bytes(_M0L6resultS610, 0, _M0L6_2atmpS1554);
    moonbit_decref_cycle_free(_M0L6resultS610);
    return _result_1812;
  } else {
    int32_t _M0L6_2atmpS1563 = _M0Lm3expS616;
    int32_t _M0L6_2atmpS1626;
    moonbit_string_t _result_1818;
    if (_M0L6_2atmpS1563 < 0) {
      int32_t _M0L6_2atmpS1564 = _M0Lm5indexS611;
      int32_t _M0L6_2atmpS1566;
      int32_t _M0L6_2atmpS1565;
      int32_t _M0L6_2atmpS1567;
      int32_t _M0L1iS629;
      int32_t _M0L6_2atmpS1582;
      int32_t _M0L6_2atmpS1584;
      int32_t _M0L6_2atmpS1583;
      int32_t _M0L7currentS631;
      int32_t _M0L1iS632;
      uint64_t _M0L6outputS633;
      if (
        _M0L6_2atmpS1564 < 0
        || _M0L6_2atmpS1564 >= Moonbit_array_length(_M0L6resultS610)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS610[_M0L6_2atmpS1564] = 48;
      _M0L6_2atmpS1566 = _M0Lm5indexS611;
      _M0L6_2atmpS1565 = _M0L6_2atmpS1566 + 1;
      if (
        _M0L6_2atmpS1565 < 0
        || _M0L6_2atmpS1565 >= Moonbit_array_length(_M0L6resultS610)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS610[_M0L6_2atmpS1565] = 46;
      _M0L6_2atmpS1567 = _M0Lm5indexS611;
      _M0Lm5indexS611 = _M0L6_2atmpS1567 + 2;
      _M0L1iS629 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1568 = _M0Lm3expS616;
        if (_M0L1iS629 > _M0L6_2atmpS1568) {
          int32_t _M0L6_2atmpS1571 = _M0Lm5indexS611;
          int32_t _M0L6_2atmpS1570 = _M0L6_2atmpS1571 - _M0L1iS629;
          int32_t _M0L6_2atmpS1569 = _M0L6_2atmpS1570 - 1;
          int32_t _M0L6_2atmpS1572;
          if (
            _M0L6_2atmpS1569 < 0
            || _M0L6_2atmpS1569 >= Moonbit_array_length(_M0L6resultS610)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS610[_M0L6_2atmpS1569] = 48;
          _M0L6_2atmpS1572 = _M0L1iS629 - 1;
          _M0L1iS629 = _M0L6_2atmpS1572;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1582 = _M0Lm5indexS611;
      _M0L6_2atmpS1584 = _M0Lm3expS616;
      _M0L6_2atmpS1583 = -1 - _M0L6_2atmpS1584;
      _M0L7currentS631 = _M0L6_2atmpS1582 + _M0L6_2atmpS1583;
      _M0L1iS632 = 0;
      _M0L6outputS633 = _M0L6outputS613;
      while (1) {
        if (_M0L1iS632 < _M0L7olengthS615) {
          int32_t _M0L6_2atmpS1579 = _M0L7currentS631 + _M0L7olengthS615;
          int32_t _M0L6_2atmpS1578 = _M0L6_2atmpS1579 - _M0L1iS632;
          int32_t _M0L6_2atmpS1573 = _M0L6_2atmpS1578 - 1;
          uint64_t _M0L6_2atmpS1577 = _M0L6outputS633 % 10ull;
          int32_t _M0L6_2atmpS1576 = (int32_t)_M0L6_2atmpS1577;
          int32_t _M0L6_2atmpS1575 = 48 + _M0L6_2atmpS1576;
          int32_t _M0L6_2atmpS1574 = _M0L6_2atmpS1575 & 0xff;
          int32_t _M0L6_2atmpS1580;
          uint64_t _M0L6_2atmpS1581;
          if (
            _M0L6_2atmpS1573 < 0
            || _M0L6_2atmpS1573 >= Moonbit_array_length(_M0L6resultS610)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS610[_M0L6_2atmpS1573] = _M0L6_2atmpS1574;
          _M0L6_2atmpS1580 = _M0L1iS632 + 1;
          _M0L6_2atmpS1581 = _M0L6outputS633 / 10ull;
          _M0L1iS632 = _M0L6_2atmpS1580;
          _M0L6outputS633 = _M0L6_2atmpS1581;
          continue;
        }
        break;
      }
      _M0Lm5indexS611 = _M0L7currentS631 + _M0L7olengthS615;
    } else {
      int32_t _M0L6_2atmpS1586 = _M0Lm3expS616;
      int32_t _M0L6_2atmpS1585 = _M0L6_2atmpS1586 + 1;
      if (_M0L6_2atmpS1585 >= _M0L7olengthS615) {
        int32_t _M0L1iS635 = 0;
        uint64_t _M0L6outputS636 = _M0L6outputS613;
        int32_t _M0L6_2atmpS1597;
        int32_t _M0L6_2atmpS1602;
        int32_t _M0L7_2abindS638;
        int32_t _M0L1iS639;
        int32_t _M0L6_2atmpS1603;
        int32_t _M0L6_2atmpS1606;
        int32_t _M0L6_2atmpS1605;
        int32_t _M0L6_2atmpS1604;
        while (1) {
          if (_M0L1iS635 < _M0L7olengthS615) {
            int32_t _M0L6_2atmpS1594 = _M0Lm5indexS611;
            int32_t _M0L6_2atmpS1593 = _M0L6_2atmpS1594 + _M0L7olengthS615;
            int32_t _M0L6_2atmpS1592 = _M0L6_2atmpS1593 - _M0L1iS635;
            int32_t _M0L6_2atmpS1587 = _M0L6_2atmpS1592 - 1;
            uint64_t _M0L6_2atmpS1591 = _M0L6outputS636 % 10ull;
            int32_t _M0L6_2atmpS1590 = (int32_t)_M0L6_2atmpS1591;
            int32_t _M0L6_2atmpS1589 = 48 + _M0L6_2atmpS1590;
            int32_t _M0L6_2atmpS1588 = _M0L6_2atmpS1589 & 0xff;
            int32_t _M0L6_2atmpS1595;
            uint64_t _M0L6_2atmpS1596;
            if (
              _M0L6_2atmpS1587 < 0
              || _M0L6_2atmpS1587 >= Moonbit_array_length(_M0L6resultS610)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS610[_M0L6_2atmpS1587] = _M0L6_2atmpS1588;
            _M0L6_2atmpS1595 = _M0L1iS635 + 1;
            _M0L6_2atmpS1596 = _M0L6outputS636 / 10ull;
            _M0L1iS635 = _M0L6_2atmpS1595;
            _M0L6outputS636 = _M0L6_2atmpS1596;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1597 = _M0Lm5indexS611;
        _M0Lm5indexS611 = _M0L6_2atmpS1597 + _M0L7olengthS615;
        _M0L6_2atmpS1602 = _M0Lm3expS616;
        _M0L7_2abindS638 = _M0L6_2atmpS1602 + 1;
        _M0L1iS639 = _M0L7olengthS615;
        while (1) {
          if (_M0L1iS639 < _M0L7_2abindS638) {
            int32_t _M0L6_2atmpS1600 = _M0Lm5indexS611;
            int32_t _M0L6_2atmpS1599 = _M0L6_2atmpS1600 + _M0L1iS639;
            int32_t _M0L6_2atmpS1598 = _M0L6_2atmpS1599 - _M0L7olengthS615;
            int32_t _M0L6_2atmpS1601;
            if (
              _M0L6_2atmpS1598 < 0
              || _M0L6_2atmpS1598 >= Moonbit_array_length(_M0L6resultS610)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS610[_M0L6_2atmpS1598] = 48;
            _M0L6_2atmpS1601 = _M0L1iS639 + 1;
            _M0L1iS639 = _M0L6_2atmpS1601;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1603 = _M0Lm5indexS611;
        _M0L6_2atmpS1606 = _M0Lm3expS616;
        _M0L6_2atmpS1605 = _M0L6_2atmpS1606 + 1;
        _M0L6_2atmpS1604 = _M0L6_2atmpS1605 - _M0L7olengthS615;
        _M0Lm5indexS611 = _M0L6_2atmpS1603 + _M0L6_2atmpS1604;
      } else {
        int32_t _M0L6_2atmpS1623 = _M0Lm5indexS611;
        int32_t _M0L6_2atmpS1622 = _M0L6_2atmpS1623 + 1;
        int32_t _M0L1iS641 = 0;
        int32_t _M0L7currentS642 = _M0L6_2atmpS1622;
        uint64_t _M0L6outputS643 = _M0L6outputS613;
        int32_t _M0L6_2atmpS1624;
        int32_t _M0L6_2atmpS1625;
        while (1) {
          if (_M0L1iS641 < _M0L7olengthS615) {
            int32_t _M0L6_2atmpS1618 = _M0L7olengthS615 - _M0L1iS641;
            int32_t _M0L6_2atmpS1616 = _M0L6_2atmpS1618 - 1;
            int32_t _M0L6_2atmpS1617 = _M0Lm3expS616;
            int32_t _M0L7currentS644;
            int32_t _M0L6_2atmpS1613;
            int32_t _M0L6_2atmpS1612;
            int32_t _M0L6_2atmpS1607;
            uint64_t _M0L6_2atmpS1611;
            int32_t _M0L6_2atmpS1610;
            int32_t _M0L6_2atmpS1609;
            int32_t _M0L6_2atmpS1608;
            int32_t _M0L6_2atmpS1614;
            uint64_t _M0L6_2atmpS1615;
            if (_M0L6_2atmpS1616 == _M0L6_2atmpS1617) {
              int32_t _M0L6_2atmpS1621 = _M0L7currentS642 + _M0L7olengthS615;
              int32_t _M0L6_2atmpS1620 = _M0L6_2atmpS1621 - _M0L1iS641;
              int32_t _M0L6_2atmpS1619 = _M0L6_2atmpS1620 - 1;
              if (
                _M0L6_2atmpS1619 < 0
                || _M0L6_2atmpS1619 >= Moonbit_array_length(_M0L6resultS610)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS610[_M0L6_2atmpS1619] = 46;
              _M0L7currentS644 = _M0L7currentS642 - 1;
            } else {
              _M0L7currentS644 = _M0L7currentS642;
            }
            _M0L6_2atmpS1613 = _M0L7currentS644 + _M0L7olengthS615;
            _M0L6_2atmpS1612 = _M0L6_2atmpS1613 - _M0L1iS641;
            _M0L6_2atmpS1607 = _M0L6_2atmpS1612 - 1;
            _M0L6_2atmpS1611 = _M0L6outputS643 % 10ull;
            _M0L6_2atmpS1610 = (int32_t)_M0L6_2atmpS1611;
            _M0L6_2atmpS1609 = 48 + _M0L6_2atmpS1610;
            _M0L6_2atmpS1608 = _M0L6_2atmpS1609 & 0xff;
            if (
              _M0L6_2atmpS1607 < 0
              || _M0L6_2atmpS1607 >= Moonbit_array_length(_M0L6resultS610)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS610[_M0L6_2atmpS1607] = _M0L6_2atmpS1608;
            _M0L6_2atmpS1614 = _M0L1iS641 + 1;
            _M0L6_2atmpS1615 = _M0L6outputS643 / 10ull;
            _M0L1iS641 = _M0L6_2atmpS1614;
            _M0L7currentS642 = _M0L7currentS644;
            _M0L6outputS643 = _M0L6_2atmpS1615;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1624 = _M0Lm5indexS611;
        _M0L6_2atmpS1625 = _M0L7olengthS615 + 1;
        _M0Lm5indexS611 = _M0L6_2atmpS1624 + _M0L6_2atmpS1625;
      }
    }
    _M0L6_2atmpS1626 = _M0Lm5indexS611;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1818
    = _M0FPB19string__from__bytes(_M0L6resultS610, 0, _M0L6_2atmpS1626);
    moonbit_decref_cycle_free(_M0L6resultS610);
    return _result_1818;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS556,
  uint32_t _M0L12ieeeExponentS555
) {
  int32_t _M0Lm2e2S553;
  uint64_t _M0Lm2m2S554;
  uint64_t _M0L6_2atmpS1500;
  uint64_t _M0L6_2atmpS1499;
  int32_t _M0L4evenS557;
  uint64_t _M0L6_2atmpS1498;
  uint64_t _M0L2mvS558;
  int32_t _M0L7mmShiftS559;
  uint64_t _M0Lm2vrS560;
  uint64_t _M0Lm2vpS561;
  uint64_t _M0Lm2vmS562;
  int32_t _M0Lm3e10S563;
  int32_t _M0Lm17vmIsTrailingZerosS564;
  int32_t _M0Lm17vrIsTrailingZerosS565;
  int32_t _M0L6_2atmpS1400;
  int32_t _M0Lm7removedS584;
  int32_t _M0Lm16lastRemovedDigitS585;
  uint64_t _M0Lm6outputS586;
  int32_t _M0L6_2atmpS1496;
  int32_t _M0L6_2atmpS1497;
  int32_t _M0L3expS609;
  uint64_t _M0L6_2atmpS1495;
  struct _M0TPB17FloatingDecimal64* _block_1824;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S553 = 0;
  _M0Lm2m2S554 = 0ull;
  if (_M0L12ieeeExponentS555 == 0u) {
    _M0Lm2e2S553 = -1076;
    _M0Lm2m2S554 = _M0L12ieeeMantissaS556;
  } else {
    int32_t _M0L6_2atmpS1399 = *(int32_t*)&_M0L12ieeeExponentS555;
    int32_t _M0L6_2atmpS1398 = _M0L6_2atmpS1399 - 1023;
    int32_t _M0L6_2atmpS1397 = _M0L6_2atmpS1398 - 52;
    _M0Lm2e2S553 = _M0L6_2atmpS1397 - 2;
    _M0Lm2m2S554 = 4503599627370496ull | _M0L12ieeeMantissaS556;
  }
  _M0L6_2atmpS1500 = _M0Lm2m2S554;
  _M0L6_2atmpS1499 = _M0L6_2atmpS1500 & 1ull;
  _M0L4evenS557 = _M0L6_2atmpS1499 == 0ull;
  _M0L6_2atmpS1498 = _M0Lm2m2S554;
  _M0L2mvS558 = 4ull * _M0L6_2atmpS1498;
  _M0L7mmShiftS559
  = _M0L12ieeeMantissaS556 != 0ull || _M0L12ieeeExponentS555 <= 1u;
  _M0Lm2vrS560 = 0ull;
  _M0Lm2vpS561 = 0ull;
  _M0Lm2vmS562 = 0ull;
  _M0Lm3e10S563 = 0;
  _M0Lm17vmIsTrailingZerosS564 = 0;
  _M0Lm17vrIsTrailingZerosS565 = 0;
  _M0L6_2atmpS1400 = _M0Lm2e2S553;
  if (_M0L6_2atmpS1400 >= 0) {
    int32_t _M0L6_2atmpS1422 = _M0Lm2e2S553;
    int32_t _M0L6_2atmpS1418;
    int32_t _M0L6_2atmpS1421;
    int32_t _M0L6_2atmpS1420;
    int32_t _M0L6_2atmpS1419;
    int32_t _M0L1qS566;
    int32_t _M0L6_2atmpS1417;
    int32_t _M0L6_2atmpS1416;
    int32_t _M0L1kS567;
    int32_t _M0L6_2atmpS1415;
    int32_t _M0L6_2atmpS1414;
    int32_t _M0L6_2atmpS1413;
    int32_t _M0L1iS568;
    struct _M0TPB8Pow5Pair _M0L4pow5S569;
    uint64_t _M0L6_2atmpS1412;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS570;
    uint64_t _M0L8_2avrOutS571;
    uint64_t _M0L8_2avpOutS572;
    uint64_t _M0L8_2avmOutS573;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1418 = _M0FPB9log10Pow2(_M0L6_2atmpS1422);
    _M0L6_2atmpS1421 = _M0Lm2e2S553;
    _M0L6_2atmpS1420 = _M0L6_2atmpS1421 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1419 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1420);
    _M0L1qS566 = _M0L6_2atmpS1418 - _M0L6_2atmpS1419;
    _M0Lm3e10S563 = _M0L1qS566;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1417 = _M0FPB8pow5bits(_M0L1qS566);
    _M0L6_2atmpS1416 = 125 + _M0L6_2atmpS1417;
    _M0L1kS567 = _M0L6_2atmpS1416 - 1;
    _M0L6_2atmpS1415 = _M0Lm2e2S553;
    _M0L6_2atmpS1414 = -_M0L6_2atmpS1415;
    _M0L6_2atmpS1413 = _M0L6_2atmpS1414 + _M0L1qS566;
    _M0L1iS568 = _M0L6_2atmpS1413 + _M0L1kS567;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S569 = _M0FPB22double__computeInvPow5(_M0L1qS566);
    _M0L6_2atmpS1412 = _M0Lm2m2S554;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS570
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1412, _M0L4pow5S569, _M0L1iS568, _M0L7mmShiftS559);
    _M0L8_2avrOutS571 = _M0L7_2abindS570.$0;
    _M0L8_2avpOutS572 = _M0L7_2abindS570.$1;
    _M0L8_2avmOutS573 = _M0L7_2abindS570.$2;
    _M0Lm2vrS560 = _M0L8_2avrOutS571;
    _M0Lm2vpS561 = _M0L8_2avpOutS572;
    _M0Lm2vmS562 = _M0L8_2avmOutS573;
    if (_M0L1qS566 <= 21) {
      int32_t _M0L6_2atmpS1408 = (int32_t)_M0L2mvS558;
      uint64_t _M0L6_2atmpS1411 = _M0L2mvS558 / 5ull;
      int32_t _M0L6_2atmpS1410 = (int32_t)_M0L6_2atmpS1411;
      int32_t _M0L6_2atmpS1409 = 5 * _M0L6_2atmpS1410;
      int32_t _M0L6mvMod5S574 = _M0L6_2atmpS1408 - _M0L6_2atmpS1409;
      if (_M0L6mvMod5S574 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS565
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS558, _M0L1qS566);
      } else if (_M0L4evenS557) {
        uint64_t _M0L6_2atmpS1402 = _M0L2mvS558 - 1ull;
        uint64_t _M0L6_2atmpS1403;
        uint64_t _M0L6_2atmpS1401;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1403 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS559);
        _M0L6_2atmpS1401 = _M0L6_2atmpS1402 - _M0L6_2atmpS1403;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS564
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1401, _M0L1qS566);
      } else {
        uint64_t _M0L6_2atmpS1404 = _M0Lm2vpS561;
        uint64_t _M0L6_2atmpS1407 = _M0L2mvS558 + 2ull;
        int32_t _M0L6_2atmpS1406;
        uint64_t _M0L6_2atmpS1405;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1406
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1407, _M0L1qS566);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1405 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1406);
        _M0Lm2vpS561 = _M0L6_2atmpS1404 - _M0L6_2atmpS1405;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1436 = _M0Lm2e2S553;
    int32_t _M0L6_2atmpS1435 = -_M0L6_2atmpS1436;
    int32_t _M0L6_2atmpS1430;
    int32_t _M0L6_2atmpS1434;
    int32_t _M0L6_2atmpS1433;
    int32_t _M0L6_2atmpS1432;
    int32_t _M0L6_2atmpS1431;
    int32_t _M0L1qS575;
    int32_t _M0L6_2atmpS1423;
    int32_t _M0L6_2atmpS1429;
    int32_t _M0L6_2atmpS1428;
    int32_t _M0L1iS576;
    int32_t _M0L6_2atmpS1427;
    int32_t _M0L1kS577;
    int32_t _M0L1jS578;
    struct _M0TPB8Pow5Pair _M0L4pow5S579;
    uint64_t _M0L6_2atmpS1426;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS580;
    uint64_t _M0L8_2avrOutS581;
    uint64_t _M0L8_2avpOutS582;
    uint64_t _M0L8_2avmOutS583;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1430 = _M0FPB9log10Pow5(_M0L6_2atmpS1435);
    _M0L6_2atmpS1434 = _M0Lm2e2S553;
    _M0L6_2atmpS1433 = -_M0L6_2atmpS1434;
    _M0L6_2atmpS1432 = _M0L6_2atmpS1433 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1431 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1432);
    _M0L1qS575 = _M0L6_2atmpS1430 - _M0L6_2atmpS1431;
    _M0L6_2atmpS1423 = _M0Lm2e2S553;
    _M0Lm3e10S563 = _M0L1qS575 + _M0L6_2atmpS1423;
    _M0L6_2atmpS1429 = _M0Lm2e2S553;
    _M0L6_2atmpS1428 = -_M0L6_2atmpS1429;
    _M0L1iS576 = _M0L6_2atmpS1428 - _M0L1qS575;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1427 = _M0FPB8pow5bits(_M0L1iS576);
    _M0L1kS577 = _M0L6_2atmpS1427 - 125;
    _M0L1jS578 = _M0L1qS575 - _M0L1kS577;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S579 = _M0FPB19double__computePow5(_M0L1iS576);
    _M0L6_2atmpS1426 = _M0Lm2m2S554;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS580
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1426, _M0L4pow5S579, _M0L1jS578, _M0L7mmShiftS559);
    _M0L8_2avrOutS581 = _M0L7_2abindS580.$0;
    _M0L8_2avpOutS582 = _M0L7_2abindS580.$1;
    _M0L8_2avmOutS583 = _M0L7_2abindS580.$2;
    _M0Lm2vrS560 = _M0L8_2avrOutS581;
    _M0Lm2vpS561 = _M0L8_2avpOutS582;
    _M0Lm2vmS562 = _M0L8_2avmOutS583;
    if (_M0L1qS575 <= 1) {
      _M0Lm17vrIsTrailingZerosS565 = 1;
      if (_M0L4evenS557) {
        int32_t _M0L6_2atmpS1424;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1424 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS559);
        _M0Lm17vmIsTrailingZerosS564 = _M0L6_2atmpS1424 == 1;
      } else {
        uint64_t _M0L6_2atmpS1425 = _M0Lm2vpS561;
        _M0Lm2vpS561 = _M0L6_2atmpS1425 - 1ull;
      }
    } else if (_M0L1qS575 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS565
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS558, _M0L1qS575);
    }
  }
  _M0Lm7removedS584 = 0;
  _M0Lm16lastRemovedDigitS585 = 0;
  _M0Lm6outputS586 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS564 || _M0Lm17vrIsTrailingZerosS565) {
    int32_t _if__result_1821;
    uint64_t _M0L6_2atmpS1466;
    uint64_t _M0L6_2atmpS1472;
    uint64_t _M0L6_2atmpS1473;
    int32_t _if__result_1822;
    int32_t _M0L6_2atmpS1469;
    int64_t _M0L6_2atmpS1468;
    uint64_t _M0L6_2atmpS1467;
    while (1) {
      uint64_t _M0L6_2atmpS1449 = _M0Lm2vpS561;
      uint64_t _M0L7vpDiv10S587 = _M0L6_2atmpS1449 / 10ull;
      uint64_t _M0L6_2atmpS1448 = _M0Lm2vmS562;
      uint64_t _M0L7vmDiv10S588 = _M0L6_2atmpS1448 / 10ull;
      uint64_t _M0L6_2atmpS1447;
      int32_t _M0L6_2atmpS1444;
      int32_t _M0L6_2atmpS1446;
      int32_t _M0L6_2atmpS1445;
      int32_t _M0L7vmMod10S590;
      uint64_t _M0L6_2atmpS1443;
      uint64_t _M0L7vrDiv10S591;
      uint64_t _M0L6_2atmpS1442;
      int32_t _M0L6_2atmpS1439;
      int32_t _M0L6_2atmpS1441;
      int32_t _M0L6_2atmpS1440;
      int32_t _M0L7vrMod10S592;
      int32_t _M0L6_2atmpS1438;
      if (_M0L7vpDiv10S587 <= _M0L7vmDiv10S588) {
        break;
      }
      _M0L6_2atmpS1447 = _M0Lm2vmS562;
      _M0L6_2atmpS1444 = (int32_t)_M0L6_2atmpS1447;
      _M0L6_2atmpS1446 = (int32_t)_M0L7vmDiv10S588;
      _M0L6_2atmpS1445 = 10 * _M0L6_2atmpS1446;
      _M0L7vmMod10S590 = _M0L6_2atmpS1444 - _M0L6_2atmpS1445;
      _M0L6_2atmpS1443 = _M0Lm2vrS560;
      _M0L7vrDiv10S591 = _M0L6_2atmpS1443 / 10ull;
      _M0L6_2atmpS1442 = _M0Lm2vrS560;
      _M0L6_2atmpS1439 = (int32_t)_M0L6_2atmpS1442;
      _M0L6_2atmpS1441 = (int32_t)_M0L7vrDiv10S591;
      _M0L6_2atmpS1440 = 10 * _M0L6_2atmpS1441;
      _M0L7vrMod10S592 = _M0L6_2atmpS1439 - _M0L6_2atmpS1440;
      _M0Lm17vmIsTrailingZerosS564
      = _M0Lm17vmIsTrailingZerosS564 && _M0L7vmMod10S590 == 0;
      if (_M0Lm17vrIsTrailingZerosS565) {
        int32_t _M0L6_2atmpS1437 = _M0Lm16lastRemovedDigitS585;
        _M0Lm17vrIsTrailingZerosS565 = _M0L6_2atmpS1437 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS565 = 0;
      }
      _M0Lm16lastRemovedDigitS585 = _M0L7vrMod10S592;
      _M0Lm2vrS560 = _M0L7vrDiv10S591;
      _M0Lm2vpS561 = _M0L7vpDiv10S587;
      _M0Lm2vmS562 = _M0L7vmDiv10S588;
      _M0L6_2atmpS1438 = _M0Lm7removedS584;
      _M0Lm7removedS584 = _M0L6_2atmpS1438 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS564) {
      while (1) {
        uint64_t _M0L6_2atmpS1462 = _M0Lm2vmS562;
        uint64_t _M0L7vmDiv10S593 = _M0L6_2atmpS1462 / 10ull;
        uint64_t _M0L6_2atmpS1461 = _M0Lm2vmS562;
        int32_t _M0L6_2atmpS1458 = (int32_t)_M0L6_2atmpS1461;
        int32_t _M0L6_2atmpS1460 = (int32_t)_M0L7vmDiv10S593;
        int32_t _M0L6_2atmpS1459 = 10 * _M0L6_2atmpS1460;
        int32_t _M0L7vmMod10S594 = _M0L6_2atmpS1458 - _M0L6_2atmpS1459;
        uint64_t _M0L6_2atmpS1457;
        uint64_t _M0L7vpDiv10S596;
        uint64_t _M0L6_2atmpS1456;
        uint64_t _M0L7vrDiv10S597;
        uint64_t _M0L6_2atmpS1455;
        int32_t _M0L6_2atmpS1452;
        int32_t _M0L6_2atmpS1454;
        int32_t _M0L6_2atmpS1453;
        int32_t _M0L7vrMod10S598;
        int32_t _M0L6_2atmpS1451;
        if (_M0L7vmMod10S594 != 0) {
          break;
        }
        _M0L6_2atmpS1457 = _M0Lm2vpS561;
        _M0L7vpDiv10S596 = _M0L6_2atmpS1457 / 10ull;
        _M0L6_2atmpS1456 = _M0Lm2vrS560;
        _M0L7vrDiv10S597 = _M0L6_2atmpS1456 / 10ull;
        _M0L6_2atmpS1455 = _M0Lm2vrS560;
        _M0L6_2atmpS1452 = (int32_t)_M0L6_2atmpS1455;
        _M0L6_2atmpS1454 = (int32_t)_M0L7vrDiv10S597;
        _M0L6_2atmpS1453 = 10 * _M0L6_2atmpS1454;
        _M0L7vrMod10S598 = _M0L6_2atmpS1452 - _M0L6_2atmpS1453;
        if (_M0Lm17vrIsTrailingZerosS565) {
          int32_t _M0L6_2atmpS1450 = _M0Lm16lastRemovedDigitS585;
          _M0Lm17vrIsTrailingZerosS565 = _M0L6_2atmpS1450 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS565 = 0;
        }
        _M0Lm16lastRemovedDigitS585 = _M0L7vrMod10S598;
        _M0Lm2vrS560 = _M0L7vrDiv10S597;
        _M0Lm2vpS561 = _M0L7vpDiv10S596;
        _M0Lm2vmS562 = _M0L7vmDiv10S593;
        _M0L6_2atmpS1451 = _M0Lm7removedS584;
        _M0Lm7removedS584 = _M0L6_2atmpS1451 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS565) {
      int32_t _M0L6_2atmpS1465 = _M0Lm16lastRemovedDigitS585;
      if (_M0L6_2atmpS1465 == 5) {
        uint64_t _M0L6_2atmpS1464 = _M0Lm2vrS560;
        uint64_t _M0L6_2atmpS1463 = _M0L6_2atmpS1464 % 2ull;
        _if__result_1821 = _M0L6_2atmpS1463 == 0ull;
      } else {
        _if__result_1821 = 0;
      }
    } else {
      _if__result_1821 = 0;
    }
    if (_if__result_1821) {
      _M0Lm16lastRemovedDigitS585 = 4;
    }
    _M0L6_2atmpS1466 = _M0Lm2vrS560;
    _M0L6_2atmpS1472 = _M0Lm2vrS560;
    _M0L6_2atmpS1473 = _M0Lm2vmS562;
    if (_M0L6_2atmpS1472 == _M0L6_2atmpS1473) {
      if (!_M0L4evenS557) {
        _if__result_1822 = 1;
      } else {
        int32_t _M0L6_2atmpS1471 = _M0Lm17vmIsTrailingZerosS564;
        _if__result_1822 = !_M0L6_2atmpS1471;
      }
    } else {
      _if__result_1822 = 0;
    }
    if (_if__result_1822) {
      _M0L6_2atmpS1469 = 1;
    } else {
      int32_t _M0L6_2atmpS1470 = _M0Lm16lastRemovedDigitS585;
      _M0L6_2atmpS1469 = _M0L6_2atmpS1470 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1468 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1469);
    _M0L6_2atmpS1467 = *(uint64_t*)&_M0L6_2atmpS1468;
    _M0Lm6outputS586 = _M0L6_2atmpS1466 + _M0L6_2atmpS1467;
  } else {
    int32_t _M0Lm7roundUpS599 = 0;
    uint64_t _M0L6_2atmpS1494 = _M0Lm2vpS561;
    uint64_t _M0L8vpDiv100S600 = _M0L6_2atmpS1494 / 100ull;
    uint64_t _M0L6_2atmpS1493 = _M0Lm2vmS562;
    uint64_t _M0L8vmDiv100S601 = _M0L6_2atmpS1493 / 100ull;
    uint64_t _M0L6_2atmpS1488;
    uint64_t _M0L6_2atmpS1491;
    uint64_t _M0L6_2atmpS1492;
    int32_t _M0L6_2atmpS1490;
    uint64_t _M0L6_2atmpS1489;
    if (_M0L8vpDiv100S600 > _M0L8vmDiv100S601) {
      uint64_t _M0L6_2atmpS1479 = _M0Lm2vrS560;
      uint64_t _M0L8vrDiv100S602 = _M0L6_2atmpS1479 / 100ull;
      uint64_t _M0L6_2atmpS1478 = _M0Lm2vrS560;
      int32_t _M0L6_2atmpS1475 = (int32_t)_M0L6_2atmpS1478;
      int32_t _M0L6_2atmpS1477 = (int32_t)_M0L8vrDiv100S602;
      int32_t _M0L6_2atmpS1476 = 100 * _M0L6_2atmpS1477;
      int32_t _M0L8vrMod100S603 = _M0L6_2atmpS1475 - _M0L6_2atmpS1476;
      int32_t _M0L6_2atmpS1474;
      _M0Lm7roundUpS599 = _M0L8vrMod100S603 >= 50;
      _M0Lm2vrS560 = _M0L8vrDiv100S602;
      _M0Lm2vpS561 = _M0L8vpDiv100S600;
      _M0Lm2vmS562 = _M0L8vmDiv100S601;
      _M0L6_2atmpS1474 = _M0Lm7removedS584;
      _M0Lm7removedS584 = _M0L6_2atmpS1474 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1487 = _M0Lm2vpS561;
      uint64_t _M0L7vpDiv10S604 = _M0L6_2atmpS1487 / 10ull;
      uint64_t _M0L6_2atmpS1486 = _M0Lm2vmS562;
      uint64_t _M0L7vmDiv10S605 = _M0L6_2atmpS1486 / 10ull;
      uint64_t _M0L6_2atmpS1485;
      uint64_t _M0L7vrDiv10S607;
      uint64_t _M0L6_2atmpS1484;
      int32_t _M0L6_2atmpS1481;
      int32_t _M0L6_2atmpS1483;
      int32_t _M0L6_2atmpS1482;
      int32_t _M0L7vrMod10S608;
      int32_t _M0L6_2atmpS1480;
      if (_M0L7vpDiv10S604 <= _M0L7vmDiv10S605) {
        break;
      }
      _M0L6_2atmpS1485 = _M0Lm2vrS560;
      _M0L7vrDiv10S607 = _M0L6_2atmpS1485 / 10ull;
      _M0L6_2atmpS1484 = _M0Lm2vrS560;
      _M0L6_2atmpS1481 = (int32_t)_M0L6_2atmpS1484;
      _M0L6_2atmpS1483 = (int32_t)_M0L7vrDiv10S607;
      _M0L6_2atmpS1482 = 10 * _M0L6_2atmpS1483;
      _M0L7vrMod10S608 = _M0L6_2atmpS1481 - _M0L6_2atmpS1482;
      _M0Lm7roundUpS599 = _M0L7vrMod10S608 >= 5;
      _M0Lm2vrS560 = _M0L7vrDiv10S607;
      _M0Lm2vpS561 = _M0L7vpDiv10S604;
      _M0Lm2vmS562 = _M0L7vmDiv10S605;
      _M0L6_2atmpS1480 = _M0Lm7removedS584;
      _M0Lm7removedS584 = _M0L6_2atmpS1480 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1488 = _M0Lm2vrS560;
    _M0L6_2atmpS1491 = _M0Lm2vrS560;
    _M0L6_2atmpS1492 = _M0Lm2vmS562;
    _M0L6_2atmpS1490
    = _M0L6_2atmpS1491 == _M0L6_2atmpS1492 || _M0Lm7roundUpS599;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1489 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1490);
    _M0Lm6outputS586 = _M0L6_2atmpS1488 + _M0L6_2atmpS1489;
  }
  _M0L6_2atmpS1496 = _M0Lm3e10S563;
  _M0L6_2atmpS1497 = _M0Lm7removedS584;
  _M0L3expS609 = _M0L6_2atmpS1496 + _M0L6_2atmpS1497;
  _M0L6_2atmpS1495 = _M0Lm6outputS586;
  _block_1824
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_1824)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1824->$0 = _M0L6_2atmpS1495;
  _block_1824->$1 = _M0L3expS609;
  return _block_1824;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS552) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS552) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS551) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS551) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS550) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS550) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS549) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS549 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS549 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS549 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS549 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS549 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS549 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS549 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS549 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS549 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS549 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS549 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS549 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS549 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS549 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS549 >= 100ull) {
    return 3;
  }
  if (_M0L1vS549 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS532) {
  int32_t _M0L6_2atmpS1396;
  int32_t _M0L6_2atmpS1395;
  int32_t _M0L4baseS531;
  int32_t _M0L5base2S533;
  int32_t _M0L6offsetS534;
  int32_t _M0L6_2atmpS1394;
  uint64_t _M0L4mul0S535;
  int32_t _M0L6_2atmpS1393;
  int32_t _M0L6_2atmpS1392;
  uint64_t _M0L4mul1S536;
  uint64_t _M0L1mS537;
  struct _M0TPB7Umul128 _M0L7_2abindS538;
  uint64_t _M0L7_2alow1S539;
  uint64_t _M0L8_2ahigh1S540;
  struct _M0TPB7Umul128 _M0L7_2abindS541;
  uint64_t _M0L7_2alow0S542;
  uint64_t _M0L8_2ahigh0S543;
  uint64_t _M0L3sumS544;
  uint64_t _M0Lm5high1S545;
  int32_t _M0L6_2atmpS1390;
  int32_t _M0L6_2atmpS1391;
  int32_t _M0L5deltaS546;
  uint64_t _M0L6_2atmpS1389;
  uint64_t _M0L6_2atmpS1381;
  int32_t _M0L6_2atmpS1388;
  uint32_t _M0L6_2atmpS1385;
  int32_t _M0L6_2atmpS1387;
  int32_t _M0L6_2atmpS1386;
  uint32_t _M0L6_2atmpS1384;
  uint32_t _M0L6_2atmpS1383;
  uint64_t _M0L6_2atmpS1382;
  uint64_t _M0L1aS547;
  uint64_t _M0L6_2atmpS1380;
  uint64_t _M0L1bS548;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1396 = _M0L1iS532 + 26;
  _M0L6_2atmpS1395 = _M0L6_2atmpS1396 - 1;
  _M0L4baseS531 = _M0L6_2atmpS1395 / 26;
  _M0L5base2S533 = _M0L4baseS531 * 26;
  _M0L6offsetS534 = _M0L5base2S533 - _M0L1iS532;
  _M0L6_2atmpS1394 = _M0L4baseS531 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S535
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1394);
  _M0L6_2atmpS1393 = _M0L4baseS531 * 2;
  _M0L6_2atmpS1392 = _M0L6_2atmpS1393 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S536
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1392);
  if (_M0L6offsetS534 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S535, .$1 = _M0L4mul1S536};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS537
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS534);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS538 = _M0FPB7umul128(_M0L1mS537, _M0L4mul1S536);
  _M0L7_2alow1S539 = _M0L7_2abindS538.$0;
  _M0L8_2ahigh1S540 = _M0L7_2abindS538.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS541 = _M0FPB7umul128(_M0L1mS537, _M0L4mul0S535);
  _M0L7_2alow0S542 = _M0L7_2abindS541.$0;
  _M0L8_2ahigh0S543 = _M0L7_2abindS541.$1;
  _M0L3sumS544 = _M0L8_2ahigh0S543 + _M0L7_2alow1S539;
  _M0Lm5high1S545 = _M0L8_2ahigh1S540;
  if (_M0L3sumS544 < _M0L8_2ahigh0S543) {
    uint64_t _M0L6_2atmpS1379 = _M0Lm5high1S545;
    _M0Lm5high1S545 = _M0L6_2atmpS1379 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1390 = _M0FPB8pow5bits(_M0L5base2S533);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1391 = _M0FPB8pow5bits(_M0L1iS532);
  _M0L5deltaS546 = _M0L6_2atmpS1390 - _M0L6_2atmpS1391;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1389
  = _M0FPB13shiftright128(_M0L7_2alow0S542, _M0L3sumS544, _M0L5deltaS546);
  _M0L6_2atmpS1381 = _M0L6_2atmpS1389 + 1ull;
  _M0L6_2atmpS1388 = _M0L1iS532 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1385
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1388);
  _M0L6_2atmpS1387 = _M0L1iS532 % 16;
  _M0L6_2atmpS1386 = _M0L6_2atmpS1387 << 1;
  _M0L6_2atmpS1384 = _M0L6_2atmpS1385 >> (_M0L6_2atmpS1386 & 31);
  _M0L6_2atmpS1383 = _M0L6_2atmpS1384 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1382 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1383);
  _M0L1aS547 = _M0L6_2atmpS1381 + _M0L6_2atmpS1382;
  _M0L6_2atmpS1380 = _M0Lm5high1S545;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS548
  = _M0FPB13shiftright128(_M0L3sumS544, _M0L6_2atmpS1380, _M0L5deltaS546);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS547, .$1 = _M0L1bS548};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS514) {
  int32_t _M0L4baseS513;
  int32_t _M0L5base2S515;
  int32_t _M0L6offsetS516;
  int32_t _M0L6_2atmpS1378;
  uint64_t _M0L4mul0S517;
  int32_t _M0L6_2atmpS1377;
  int32_t _M0L6_2atmpS1376;
  uint64_t _M0L4mul1S518;
  uint64_t _M0L1mS519;
  struct _M0TPB7Umul128 _M0L7_2abindS520;
  uint64_t _M0L7_2alow1S521;
  uint64_t _M0L8_2ahigh1S522;
  struct _M0TPB7Umul128 _M0L7_2abindS523;
  uint64_t _M0L7_2alow0S524;
  uint64_t _M0L8_2ahigh0S525;
  uint64_t _M0L3sumS526;
  uint64_t _M0Lm5high1S527;
  int32_t _M0L6_2atmpS1374;
  int32_t _M0L6_2atmpS1375;
  int32_t _M0L5deltaS528;
  uint64_t _M0L6_2atmpS1366;
  int32_t _M0L6_2atmpS1373;
  uint32_t _M0L6_2atmpS1370;
  int32_t _M0L6_2atmpS1372;
  int32_t _M0L6_2atmpS1371;
  uint32_t _M0L6_2atmpS1369;
  uint32_t _M0L6_2atmpS1368;
  uint64_t _M0L6_2atmpS1367;
  uint64_t _M0L1aS529;
  uint64_t _M0L6_2atmpS1365;
  uint64_t _M0L1bS530;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS513 = _M0L1iS514 / 26;
  _M0L5base2S515 = _M0L4baseS513 * 26;
  _M0L6offsetS516 = _M0L1iS514 - _M0L5base2S515;
  _M0L6_2atmpS1378 = _M0L4baseS513 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S517
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1378);
  _M0L6_2atmpS1377 = _M0L4baseS513 * 2;
  _M0L6_2atmpS1376 = _M0L6_2atmpS1377 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S518
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1376);
  if (_M0L6offsetS516 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S517, .$1 = _M0L4mul1S518};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS519
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS516);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS520 = _M0FPB7umul128(_M0L1mS519, _M0L4mul1S518);
  _M0L7_2alow1S521 = _M0L7_2abindS520.$0;
  _M0L8_2ahigh1S522 = _M0L7_2abindS520.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS523 = _M0FPB7umul128(_M0L1mS519, _M0L4mul0S517);
  _M0L7_2alow0S524 = _M0L7_2abindS523.$0;
  _M0L8_2ahigh0S525 = _M0L7_2abindS523.$1;
  _M0L3sumS526 = _M0L8_2ahigh0S525 + _M0L7_2alow1S521;
  _M0Lm5high1S527 = _M0L8_2ahigh1S522;
  if (_M0L3sumS526 < _M0L8_2ahigh0S525) {
    uint64_t _M0L6_2atmpS1364 = _M0Lm5high1S527;
    _M0Lm5high1S527 = _M0L6_2atmpS1364 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1374 = _M0FPB8pow5bits(_M0L1iS514);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1375 = _M0FPB8pow5bits(_M0L5base2S515);
  _M0L5deltaS528 = _M0L6_2atmpS1374 - _M0L6_2atmpS1375;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1366
  = _M0FPB13shiftright128(_M0L7_2alow0S524, _M0L3sumS526, _M0L5deltaS528);
  _M0L6_2atmpS1373 = _M0L1iS514 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1370
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1373);
  _M0L6_2atmpS1372 = _M0L1iS514 % 16;
  _M0L6_2atmpS1371 = _M0L6_2atmpS1372 << 1;
  _M0L6_2atmpS1369 = _M0L6_2atmpS1370 >> (_M0L6_2atmpS1371 & 31);
  _M0L6_2atmpS1368 = _M0L6_2atmpS1369 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1367 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1368);
  _M0L1aS529 = _M0L6_2atmpS1366 + _M0L6_2atmpS1367;
  _M0L6_2atmpS1365 = _M0Lm5high1S527;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS530
  = _M0FPB13shiftright128(_M0L3sumS526, _M0L6_2atmpS1365, _M0L5deltaS528);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS529, .$1 = _M0L1bS530};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS487,
  struct _M0TPB8Pow5Pair _M0L3mulS484,
  int32_t _M0L1jS500,
  int32_t _M0L7mmShiftS502
) {
  uint64_t _M0L7_2amul0S483;
  uint64_t _M0L7_2amul1S485;
  uint64_t _M0L1mS486;
  struct _M0TPB7Umul128 _M0L7_2abindS488;
  uint64_t _M0L5_2aloS489;
  uint64_t _M0L6_2atmpS490;
  struct _M0TPB7Umul128 _M0L7_2abindS491;
  uint64_t _M0L6_2alo2S492;
  uint64_t _M0L6_2ahi2S493;
  uint64_t _M0L3midS494;
  uint64_t _M0L6_2atmpS1363;
  uint64_t _M0L2hiS495;
  uint64_t _M0L3lo2S496;
  uint64_t _M0L6_2atmpS1361;
  uint64_t _M0L6_2atmpS1362;
  uint64_t _M0L4mid2S497;
  uint64_t _M0L6_2atmpS1360;
  uint64_t _M0L3hi2S498;
  int32_t _M0L6_2atmpS1359;
  int32_t _M0L6_2atmpS1358;
  uint64_t _M0L2vpS499;
  uint64_t _M0Lm2vmS501;
  int32_t _M0L6_2atmpS1357;
  int32_t _M0L6_2atmpS1356;
  uint64_t _M0L2vrS512;
  uint64_t _M0L6_2atmpS1355;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S483 = _M0L3mulS484.$0;
  _M0L7_2amul1S485 = _M0L3mulS484.$1;
  _M0L1mS486 = _M0L1mS487 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS488 = _M0FPB7umul128(_M0L1mS486, _M0L7_2amul0S483);
  _M0L5_2aloS489 = _M0L7_2abindS488.$0;
  _M0L6_2atmpS490 = _M0L7_2abindS488.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS491 = _M0FPB7umul128(_M0L1mS486, _M0L7_2amul1S485);
  _M0L6_2alo2S492 = _M0L7_2abindS491.$0;
  _M0L6_2ahi2S493 = _M0L7_2abindS491.$1;
  _M0L3midS494 = _M0L6_2atmpS490 + _M0L6_2alo2S492;
  if (_M0L3midS494 < _M0L6_2atmpS490) {
    _M0L6_2atmpS1363 = 1ull;
  } else {
    _M0L6_2atmpS1363 = 0ull;
  }
  _M0L2hiS495 = _M0L6_2ahi2S493 + _M0L6_2atmpS1363;
  _M0L3lo2S496 = _M0L5_2aloS489 + _M0L7_2amul0S483;
  _M0L6_2atmpS1361 = _M0L3midS494 + _M0L7_2amul1S485;
  if (_M0L3lo2S496 < _M0L5_2aloS489) {
    _M0L6_2atmpS1362 = 1ull;
  } else {
    _M0L6_2atmpS1362 = 0ull;
  }
  _M0L4mid2S497 = _M0L6_2atmpS1361 + _M0L6_2atmpS1362;
  if (_M0L4mid2S497 < _M0L3midS494) {
    _M0L6_2atmpS1360 = 1ull;
  } else {
    _M0L6_2atmpS1360 = 0ull;
  }
  _M0L3hi2S498 = _M0L2hiS495 + _M0L6_2atmpS1360;
  _M0L6_2atmpS1359 = _M0L1jS500 - 64;
  _M0L6_2atmpS1358 = _M0L6_2atmpS1359 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS499
  = _M0FPB13shiftright128(_M0L4mid2S497, _M0L3hi2S498, _M0L6_2atmpS1358);
  _M0Lm2vmS501 = 0ull;
  if (_M0L7mmShiftS502) {
    uint64_t _M0L3lo3S503 = _M0L5_2aloS489 - _M0L7_2amul0S483;
    uint64_t _M0L6_2atmpS1345 = _M0L3midS494 - _M0L7_2amul1S485;
    uint64_t _M0L6_2atmpS1346;
    uint64_t _M0L4mid3S504;
    uint64_t _M0L6_2atmpS1344;
    uint64_t _M0L3hi3S505;
    int32_t _M0L6_2atmpS1343;
    int32_t _M0L6_2atmpS1342;
    if (_M0L5_2aloS489 < _M0L3lo3S503) {
      _M0L6_2atmpS1346 = 1ull;
    } else {
      _M0L6_2atmpS1346 = 0ull;
    }
    _M0L4mid3S504 = _M0L6_2atmpS1345 - _M0L6_2atmpS1346;
    if (_M0L3midS494 < _M0L4mid3S504) {
      _M0L6_2atmpS1344 = 1ull;
    } else {
      _M0L6_2atmpS1344 = 0ull;
    }
    _M0L3hi3S505 = _M0L2hiS495 - _M0L6_2atmpS1344;
    _M0L6_2atmpS1343 = _M0L1jS500 - 64;
    _M0L6_2atmpS1342 = _M0L6_2atmpS1343 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS501
    = _M0FPB13shiftright128(_M0L4mid3S504, _M0L3hi3S505, _M0L6_2atmpS1342);
  } else {
    uint64_t _M0L3lo3S506 = _M0L5_2aloS489 + _M0L5_2aloS489;
    uint64_t _M0L6_2atmpS1353 = _M0L3midS494 + _M0L3midS494;
    uint64_t _M0L6_2atmpS1354;
    uint64_t _M0L4mid3S507;
    uint64_t _M0L6_2atmpS1351;
    uint64_t _M0L6_2atmpS1352;
    uint64_t _M0L3hi3S508;
    uint64_t _M0L3lo4S509;
    uint64_t _M0L6_2atmpS1349;
    uint64_t _M0L6_2atmpS1350;
    uint64_t _M0L4mid4S510;
    uint64_t _M0L6_2atmpS1348;
    uint64_t _M0L3hi4S511;
    int32_t _M0L6_2atmpS1347;
    if (_M0L3lo3S506 < _M0L5_2aloS489) {
      _M0L6_2atmpS1354 = 1ull;
    } else {
      _M0L6_2atmpS1354 = 0ull;
    }
    _M0L4mid3S507 = _M0L6_2atmpS1353 + _M0L6_2atmpS1354;
    _M0L6_2atmpS1351 = _M0L2hiS495 + _M0L2hiS495;
    if (_M0L4mid3S507 < _M0L3midS494) {
      _M0L6_2atmpS1352 = 1ull;
    } else {
      _M0L6_2atmpS1352 = 0ull;
    }
    _M0L3hi3S508 = _M0L6_2atmpS1351 + _M0L6_2atmpS1352;
    _M0L3lo4S509 = _M0L3lo3S506 - _M0L7_2amul0S483;
    _M0L6_2atmpS1349 = _M0L4mid3S507 - _M0L7_2amul1S485;
    if (_M0L3lo3S506 < _M0L3lo4S509) {
      _M0L6_2atmpS1350 = 1ull;
    } else {
      _M0L6_2atmpS1350 = 0ull;
    }
    _M0L4mid4S510 = _M0L6_2atmpS1349 - _M0L6_2atmpS1350;
    if (_M0L4mid3S507 < _M0L4mid4S510) {
      _M0L6_2atmpS1348 = 1ull;
    } else {
      _M0L6_2atmpS1348 = 0ull;
    }
    _M0L3hi4S511 = _M0L3hi3S508 - _M0L6_2atmpS1348;
    _M0L6_2atmpS1347 = _M0L1jS500 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS501
    = _M0FPB13shiftright128(_M0L4mid4S510, _M0L3hi4S511, _M0L6_2atmpS1347);
  }
  _M0L6_2atmpS1357 = _M0L1jS500 - 64;
  _M0L6_2atmpS1356 = _M0L6_2atmpS1357 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS512
  = _M0FPB13shiftright128(_M0L3midS494, _M0L2hiS495, _M0L6_2atmpS1356);
  _M0L6_2atmpS1355 = _M0Lm2vmS501;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS512,
                                                .$1 = _M0L2vpS499,
                                                .$2 = _M0L6_2atmpS1355};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS481,
  int32_t _M0L1pS482
) {
  uint64_t _M0L6_2atmpS1341;
  uint64_t _M0L6_2atmpS1340;
  uint64_t _M0L6_2atmpS1339;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1341 = 1ull << (_M0L1pS482 & 63);
  _M0L6_2atmpS1340 = _M0L6_2atmpS1341 - 1ull;
  _M0L6_2atmpS1339 = _M0L5valueS481 & _M0L6_2atmpS1340;
  return _M0L6_2atmpS1339 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS479,
  int32_t _M0L1pS480
) {
  int32_t _M0L6_2atmpS1338;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1338 = _M0FPB10pow5Factor(_M0L5valueS479);
  return _M0L6_2atmpS1338 >= _M0L1pS480;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS474) {
  uint64_t _M0L6_2atmpS1329;
  uint64_t _M0L6_2atmpS1330;
  uint64_t _M0L6_2atmpS1331;
  uint64_t _M0L6_2atmpS1332;
  uint64_t _M0L6_2atmpS1337;
  int32_t _M0L5countS475;
  uint64_t _M0L1vS476;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1329 = _M0L5valueS474 % 5ull;
  if (_M0L6_2atmpS1329 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1330 = _M0L5valueS474 % 25ull;
  if (_M0L6_2atmpS1330 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1331 = _M0L5valueS474 % 125ull;
  if (_M0L6_2atmpS1331 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1332 = _M0L5valueS474 % 625ull;
  if (_M0L6_2atmpS1332 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1337 = _M0L5valueS474 / 625ull;
  _M0L5countS475 = 4;
  _M0L1vS476 = _M0L6_2atmpS1337;
  while (1) {
    if (_M0L1vS476 > 0ull) {
      uint64_t _M0L6_2atmpS1333 = _M0L1vS476 % 5ull;
      int32_t _M0L6_2atmpS1334;
      uint64_t _M0L6_2atmpS1335;
      if (_M0L6_2atmpS1333 != 0ull) {
        return _M0L5countS475;
      }
      _M0L6_2atmpS1334 = _M0L5countS475 + 1;
      _M0L6_2atmpS1335 = _M0L1vS476 / 5ull;
      _M0L5countS475 = _M0L6_2atmpS1334;
      _M0L1vS476 = _M0L6_2atmpS1335;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS478;
      moonbit_string_t _M0L6_2atmpS1336;
      int32_t _result_1826;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS478
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS478, (moonbit_string_t)moonbit_string_literal_10.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS478, _M0L5valueS474);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1336
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS478);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS478);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_1826 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1336);
      moonbit_decref_cycle_free(_M0L6_2atmpS1336);
      return _result_1826;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS473,
  uint64_t _M0L2hiS471,
  int32_t _M0L4distS472
) {
  int32_t _M0L6_2atmpS1328;
  uint64_t _M0L6_2atmpS1326;
  uint64_t _M0L6_2atmpS1327;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1328 = 64 - _M0L4distS472;
  _M0L6_2atmpS1326 = _M0L2hiS471 << (_M0L6_2atmpS1328 & 63);
  _M0L6_2atmpS1327 = _M0L2loS473 >> (_M0L4distS472 & 63);
  return _M0L6_2atmpS1326 | _M0L6_2atmpS1327;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS461,
  uint64_t _M0L1bS464
) {
  uint64_t _M0L3aLoS460;
  uint64_t _M0L3aHiS462;
  uint64_t _M0L3bLoS463;
  uint64_t _M0L3bHiS465;
  uint64_t _M0L1xS466;
  uint64_t _M0L6_2atmpS1324;
  uint64_t _M0L6_2atmpS1325;
  uint64_t _M0L1yS467;
  uint64_t _M0L6_2atmpS1322;
  uint64_t _M0L6_2atmpS1323;
  uint64_t _M0L1zS468;
  uint64_t _M0L6_2atmpS1320;
  uint64_t _M0L6_2atmpS1321;
  uint64_t _M0L6_2atmpS1318;
  uint64_t _M0L6_2atmpS1319;
  uint64_t _M0L1wS469;
  uint64_t _M0L2loS470;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS460 = _M0L1aS461 & 4294967295ull;
  _M0L3aHiS462 = _M0L1aS461 >> 32;
  _M0L3bLoS463 = _M0L1bS464 & 4294967295ull;
  _M0L3bHiS465 = _M0L1bS464 >> 32;
  _M0L1xS466 = _M0L3aLoS460 * _M0L3bLoS463;
  _M0L6_2atmpS1324 = _M0L3aHiS462 * _M0L3bLoS463;
  _M0L6_2atmpS1325 = _M0L1xS466 >> 32;
  _M0L1yS467 = _M0L6_2atmpS1324 + _M0L6_2atmpS1325;
  _M0L6_2atmpS1322 = _M0L3aLoS460 * _M0L3bHiS465;
  _M0L6_2atmpS1323 = _M0L1yS467 & 4294967295ull;
  _M0L1zS468 = _M0L6_2atmpS1322 + _M0L6_2atmpS1323;
  _M0L6_2atmpS1320 = _M0L3aHiS462 * _M0L3bHiS465;
  _M0L6_2atmpS1321 = _M0L1yS467 >> 32;
  _M0L6_2atmpS1318 = _M0L6_2atmpS1320 + _M0L6_2atmpS1321;
  _M0L6_2atmpS1319 = _M0L1zS468 >> 32;
  _M0L1wS469 = _M0L6_2atmpS1318 + _M0L6_2atmpS1319;
  _M0L2loS470 = _M0L1aS461 * _M0L1bS464;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS470, .$1 = _M0L1wS469};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS458,
  int32_t _M0L4fromS455,
  int32_t _M0L2toS454
) {
  int32_t _M0L3lenS453;
  int32_t _M0L6_2atmpS1317;
  uint16_t* _M0L6bufferS456;
  int32_t _M0L1iS457;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS453 = _M0L2toS454 - _M0L4fromS455;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1317 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS456
  = (uint16_t*)moonbit_make_string(_M0L3lenS453, _M0L6_2atmpS1317);
  _M0L1iS457 = 0;
  while (1) {
    if (_M0L1iS457 < _M0L3lenS453) {
      int32_t _M0L6_2atmpS1315 = _M0L4fromS455 + _M0L1iS457;
      int32_t _M0L6_2atmpS1314;
      int32_t _M0L6_2atmpS1313;
      int32_t _M0L6_2atmpS1316;
      if (
        _M0L6_2atmpS1315 < 0
        || _M0L6_2atmpS1315 >= Moonbit_array_length(_M0L5bytesS458)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1314 = (int32_t)_M0L5bytesS458[_M0L6_2atmpS1315];
      _M0L6_2atmpS1313 = (uint16_t)_M0L6_2atmpS1314;
      if (
        _M0L1iS457 < 0 || _M0L1iS457 >= Moonbit_array_length(_M0L6bufferS456)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS456[_M0L1iS457] = _M0L6_2atmpS1313;
      _M0L6_2atmpS1316 = _M0L1iS457 + 1;
      _M0L1iS457 = _M0L6_2atmpS1316;
      continue;
    }
    break;
  }
  return _M0L6bufferS456;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS452) {
  int32_t _M0L6_2atmpS1312;
  uint32_t _M0L6_2atmpS1311;
  uint32_t _M0L6_2atmpS1310;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1312 = _M0L1eS452 * 78913;
  _M0L6_2atmpS1311 = *(uint32_t*)&_M0L6_2atmpS1312;
  _M0L6_2atmpS1310 = _M0L6_2atmpS1311 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1310;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS451) {
  int32_t _M0L6_2atmpS1309;
  uint32_t _M0L6_2atmpS1308;
  uint32_t _M0L6_2atmpS1307;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1309 = _M0L1eS451 * 732923;
  _M0L6_2atmpS1308 = *(uint32_t*)&_M0L6_2atmpS1309;
  _M0L6_2atmpS1307 = _M0L6_2atmpS1308 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1307;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS449,
  int32_t _M0L8exponentS450,
  int32_t _M0L8mantissaS447
) {
  moonbit_string_t _M0L1sS448;
  moonbit_string_t _result_1829;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS447) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  if (_M0L4signS449) {
    _M0L1sS448 = (moonbit_string_t)moonbit_string_literal_12.data;
  } else {
    _M0L1sS448 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS450) {
    moonbit_string_t _result_1828;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1828
    = moonbit_add_string(_M0L1sS448, (moonbit_string_t)moonbit_string_literal_13.data);
    moonbit_decref_cycle_free(_M0L1sS448);
    return _result_1828;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1829
  = moonbit_add_string(_M0L1sS448, (moonbit_string_t)moonbit_string_literal_14.data);
  moonbit_decref_cycle_free(_M0L1sS448);
  return _result_1829;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS446) {
  int32_t _M0L6_2atmpS1306;
  uint32_t _M0L6_2atmpS1305;
  uint32_t _M0L6_2atmpS1304;
  int32_t _M0L6_2atmpS1303;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1306 = _M0L1eS446 * 1217359;
  _M0L6_2atmpS1305 = *(uint32_t*)&_M0L6_2atmpS1306;
  _M0L6_2atmpS1304 = _M0L6_2atmpS1305 >> 19;
  _M0L6_2atmpS1303 = *(int32_t*)&_M0L6_2atmpS1304;
  return _M0L6_2atmpS1303 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS445) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS445 != _M0L4selfS445) {
    return 0;
  } else if (_M0L4selfS445 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS445 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS445;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS444) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS444 != _M0L4selfS444) {
    return 0ll;
  } else if (_M0L4selfS444 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS444 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS444;
  }
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS442
) {
  uint8_t* _M0L6_2atmpS1301;
  struct _M0TPB5ArrayGbE* _block_1830;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1301 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS442);
  _block_1830
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_1830)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _block_1830->$0 = _M0L6_2atmpS1301;
  _block_1830->$1 = _M0L3lenS442;
  return _block_1830;
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS443
) {
  float* _M0L6_2atmpS1302;
  struct _M0TPB5ArrayGfE* _block_1831;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1302 = (float*)moonbit_make_float_array_raw(_M0L3lenS443);
  _block_1831
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_1831)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 27, 0);
  _block_1831->$0 = _M0L6_2atmpS1302;
  _block_1831->$1 = _M0L3lenS443;
  return _block_1831;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS438,
  int32_t _M0L5indexS439
) {
  uint64_t* _M0L6_2atmpS1299;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1299 = _M0L4selfS438;
  if (
    _M0L5indexS439 < 0
    || _M0L5indexS439 >= Moonbit_array_length(_M0L6_2atmpS1299)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1299[_M0L5indexS439];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS440,
  int32_t _M0L5indexS441
) {
  uint32_t* _M0L6_2atmpS1300;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1300 = _M0L4selfS440;
  if (
    _M0L5indexS441 < 0
    || _M0L5indexS441 >= Moonbit_array_length(_M0L6_2atmpS1300)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1300[_M0L5indexS441];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS437
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS437, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS436) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS436, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS435) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS435;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS429,
  moonbit_string_t _M0L5valueS431
) {
  int32_t _M0L3lenS1285;
  moonbit_string_t* _M0L6_2atmpS1287;
  int32_t _M0L6_2atmpS1286;
  int32_t _M0L6lengthS430;
  moonbit_string_t* _M0L3bufS1290;
  moonbit_string_t _M0L6_2aoldS1755;
  int32_t _M0L6_2atmpS1291;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1285 = _M0L4selfS429->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1287 = _M0MPC15array5Array6bufferGsE(_M0L4selfS429);
  _M0L6_2atmpS1286 = Moonbit_array_length(_M0L6_2atmpS1287);
  moonbit_decref_cycle_free(_M0L6_2atmpS1287);
  if (_M0L3lenS1285 == _M0L6_2atmpS1286) {
    int32_t _M0L3lenS1289 = _M0L4selfS429->$1;
    int32_t _M0L6_2atmpS1288 = _M0L3lenS1289 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS429, _M0L6_2atmpS1288);
  }
  _M0L6lengthS430 = _M0L4selfS429->$1;
  _M0L3bufS1290 = _M0L4selfS429->$0;
  _M0L6_2aoldS1755 = (moonbit_string_t)_M0L3bufS1290[_M0L6lengthS430];
  moonbit_decref_cycle_free(_M0L6_2aoldS1755);
  _M0L3bufS1290[_M0L6lengthS430] = _M0L5valueS431;
  _M0L6_2atmpS1291 = _M0L6lengthS430 + 1;
  _M0L4selfS429->$1 = _M0L6_2atmpS1291;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS432,
  struct _M0TUsiE* _M0L5valueS434
) {
  int32_t _M0L3lenS1292;
  struct _M0TUsiE** _M0L6_2atmpS1294;
  int32_t _M0L6_2atmpS1293;
  int32_t _M0L6lengthS433;
  struct _M0TUsiE** _M0L3bufS1297;
  struct _M0TUsiE* _M0L6_2aoldS1756;
  int32_t _M0L6_2atmpS1298;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1292 = _M0L4selfS432->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1294 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS432);
  _M0L6_2atmpS1293 = Moonbit_array_length(_M0L6_2atmpS1294);
  moonbit_decref_cycle_free(_M0L6_2atmpS1294);
  if (_M0L3lenS1292 == _M0L6_2atmpS1293) {
    int32_t _M0L3lenS1296 = _M0L4selfS432->$1;
    int32_t _M0L6_2atmpS1295 = _M0L3lenS1296 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS432, _M0L6_2atmpS1295);
  }
  _M0L6lengthS433 = _M0L4selfS432->$1;
  _M0L3bufS1297 = _M0L4selfS432->$0;
  _M0L6_2aoldS1756 = (struct _M0TUsiE*)_M0L3bufS1297[_M0L6lengthS433];
  if (_M0L6_2aoldS1756) {
    moonbit_decref_cycle_free(_M0L6_2aoldS1756);
  }
  _M0L3bufS1297[_M0L6lengthS433] = _M0L5valueS434;
  _M0L6_2atmpS1298 = _M0L6lengthS433 + 1;
  _M0L4selfS432->$1 = _M0L6_2atmpS1298;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS422,
  int32_t _M0L8requiredS424
) {
  int32_t _M0L8old__capS421;
  int32_t _M0L3lenS1283;
  int32_t _M0L8new__capS423;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS421 = _M0MPC15array5Array8capacityGsE(_M0L4selfS422);
  _M0L3lenS1283 = _M0L4selfS422->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS423
  = _M0FPB23array__growth__capacity(_M0L8old__capS421, _M0L3lenS1283, _M0L8requiredS424);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS422, _M0L8new__capS423);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS426,
  int32_t _M0L8requiredS428
) {
  int32_t _M0L8old__capS425;
  int32_t _M0L3lenS1284;
  int32_t _M0L8new__capS427;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS425 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS426);
  _M0L3lenS1284 = _M0L4selfS426->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS427
  = _M0FPB23array__growth__capacity(_M0L8old__capS425, _M0L3lenS1284, _M0L8requiredS428);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS426, _M0L8new__capS427);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS410,
  int32_t _M0L13new__capacityS413
) {
  moonbit_string_t* _M0L8old__bufS409;
  int32_t _M0L3lenS411;
  int32_t _M0L9copy__lenS412;
  moonbit_string_t* _M0L8new__bufS414;
  moonbit_string_t* _M0L6_2aoldS1757;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS409 = _M0L4selfS410->$0;
  _M0L3lenS411 = _M0L4selfS410->$1;
  if (_M0L3lenS411 < _M0L13new__capacityS413) {
    _M0L9copy__lenS412 = _M0L3lenS411;
  } else {
    _M0L9copy__lenS412 = _M0L13new__capacityS413;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS409);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS414
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS409, _M0L13new__capacityS413, _M0L9copy__lenS412, 0, 0);
  _M0L6_2aoldS1757 = _M0L4selfS410->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1757);
  _M0L4selfS410->$0 = _M0L8new__bufS414;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS416,
  int32_t _M0L13new__capacityS419
) {
  struct _M0TUsiE** _M0L8old__bufS415;
  int32_t _M0L3lenS417;
  int32_t _M0L9copy__lenS418;
  struct _M0TUsiE** _M0L8new__bufS420;
  struct _M0TUsiE** _M0L6_2aoldS1758;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS415 = _M0L4selfS416->$0;
  _M0L3lenS417 = _M0L4selfS416->$1;
  if (_M0L3lenS417 < _M0L13new__capacityS419) {
    _M0L9copy__lenS418 = _M0L3lenS417;
  } else {
    _M0L9copy__lenS418 = _M0L13new__capacityS419;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS415);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS420
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS415, _M0L13new__capacityS419, _M0L9copy__lenS418, 0, 0);
  _M0L6_2aoldS1758 = _M0L4selfS416->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1758);
  _M0L4selfS416->$0 = _M0L8new__bufS420;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS407
) {
  moonbit_string_t* _M0L6_2atmpS1281;
  int32_t _result_1832;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1281 = _M0MPC15array5Array6bufferGsE(_M0L4selfS407);
  _result_1832 = Moonbit_array_length(_M0L6_2atmpS1281);
  moonbit_decref_cycle_free(_M0L6_2atmpS1281);
  return _result_1832;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS408
) {
  struct _M0TUsiE** _M0L6_2atmpS1282;
  int32_t _result_1833;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1282 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS408);
  _result_1833 = Moonbit_array_length(_M0L6_2atmpS1282);
  moonbit_decref_cycle_free(_M0L6_2atmpS1282);
  return _result_1833;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS403,
  int32_t _M0L3lenS401,
  int32_t _M0L8requiredS400
) {
  int32_t _M0L5startS402;
  int32_t _M0L5spaceS404;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS400 < _M0L3lenS401) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_15.data);
  }
  if (_M0L7currentS403 == 0) {
    _M0L5startS402 = 8;
  } else {
    _M0L5startS402 = _M0L7currentS403;
  }
  _M0L5spaceS404 = _M0L5startS402;
  while (1) {
    if (_M0L5spaceS404 < _M0L8requiredS400) {
      int32_t _M0L4nextS405 = _M0L5spaceS404 * 2;
      if (_M0L4nextS405 <= _M0L5spaceS404) {
        return _M0L8requiredS400;
      }
      _M0L5spaceS404 = _M0L4nextS405;
      continue;
    } else {
      return _M0L5spaceS404;
    }
    break;
  }
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS396) {
  uint8_t* _M0L8_2afieldS1759;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1759 = _M0L4selfS396->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1759);
  return _M0L8_2afieldS1759;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS397) {
  float* _M0L8_2afieldS1760;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1760 = _M0L4selfS397->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1760);
  return _M0L8_2afieldS1760;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS398
) {
  moonbit_string_t* _M0L8_2afieldS1761;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1761 = _M0L4selfS398->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1761);
  return _M0L8_2afieldS1761;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS399
) {
  struct _M0TUsiE** _M0L8_2afieldS1762;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1762 = _M0L4selfS399->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1762);
  return _M0L8_2afieldS1762;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS395
) {
  #line 220 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref_cycle_free(_M0L4selfS395);
  return _M0L4selfS395;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS394,
  struct _M0TPC16string10StringView _M0L3strS392
) {
  int32_t _M0L3endS1279;
  int32_t _M0L5startS1280;
  int32_t _M0L8str__lenS391;
  int32_t _M0L3lenS1278;
  int32_t _M0L8requiredS393;
  uint16_t* _M0L4dataS1271;
  int32_t _M0L6_2atmpS1270;
  int32_t _if__result_1835;
  uint16_t* _M0L4dataS1272;
  int32_t _M0L3lenS1273;
  moonbit_string_t _M0L6_2atmpS1274;
  int32_t _M0L6_2atmpS1275;
  int32_t _M0L3lenS1277;
  int32_t _M0L6_2atmpS1276;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1279 = _M0L3strS392.$2;
  _M0L5startS1280 = _M0L3strS392.$1;
  _M0L8str__lenS391 = _M0L3endS1279 - _M0L5startS1280;
  if (_M0L8str__lenS391 == 0) {
    return 0;
  }
  _M0L3lenS1278 = _M0L4selfS394->$1;
  _M0L8requiredS393 = _M0L3lenS1278 + _M0L8str__lenS391;
  _M0L4dataS1271 = _M0L4selfS394->$0;
  _M0L6_2atmpS1270 = Moonbit_array_length(_M0L4dataS1271);
  if (_M0L8requiredS393 > _M0L6_2atmpS1270) {
    _if__result_1835 = 1;
  } else {
    int32_t _M0L3lenS1269 = _M0L4selfS394->$1;
    _if__result_1835 = _M0L8requiredS393 < _M0L3lenS1269;
  }
  if (_if__result_1835) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS394, _M0L8requiredS393);
  }
  _M0L4dataS1272 = _M0L4selfS394->$0;
  _M0L3lenS1273 = _M0L4selfS394->$1;
  moonbit_incref_cycle_free(_M0L4dataS1272);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1274 = _M0MPC16string10StringView4data(_M0L3strS392);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1275 = _M0MPC16string10StringView13start__offset(_M0L3strS392);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1272, _M0L3lenS1273, _M0L6_2atmpS1274, _M0L6_2atmpS1275, _M0L8str__lenS391);
  moonbit_decref_cycle_free(_M0L4dataS1272);
  moonbit_decref_cycle_free(_M0L6_2atmpS1274);
  _M0L3lenS1277 = _M0L4selfS394->$1;
  _M0L6_2atmpS1276 = _M0L3lenS1277 + _M0L8str__lenS391;
  _M0L4selfS394->$1 = _M0L6_2atmpS1276;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS388,
  int32_t _M0L5startS386,
  int32_t _M0L3endS387
) {
  int32_t _if__result_1836;
  int32_t _M0L3lenS389;
  int32_t _M0L6_2atmpS1268;
  moonbit_bytes_t _M0L5bytesS390;
  moonbit_bytes_t _M0L6_2atmpS1267;
  moonbit_string_t _result_1837;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS386 == 0) {
    int32_t _M0L6_2atmpS1266 = Moonbit_array_length(_M0L3strS388);
    _if__result_1836 = _M0L3endS387 == _M0L6_2atmpS1266;
  } else {
    _if__result_1836 = 0;
  }
  if (_if__result_1836) {
    moonbit_incref_cycle_free(_M0L3strS388);
    return _M0L3strS388;
  }
  _M0L3lenS389 = _M0L3endS387 - _M0L5startS386;
  _M0L6_2atmpS1268 = _M0L3lenS389 * 2;
  _M0L5bytesS390 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1268, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS390, 0, _M0L3strS388, _M0L5startS386, _M0L3lenS389);
  _M0L6_2atmpS1267 = _M0L5bytesS390;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_1837
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1267, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1267);
  return _result_1837;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS381,
  int32_t _M0L6offsetS385,
  int64_t _M0L6lengthS383
) {
  int32_t _M0L3lenS380;
  int32_t _M0L6lengthS382;
  int32_t _if__result_1838;
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L3lenS380 = Moonbit_array_length(_M0L4selfS381);
  if (_M0L6lengthS383 == 4294967296ll) {
    _M0L6lengthS382 = _M0L3lenS380 - _M0L6offsetS385;
  } else {
    int64_t _M0L7_2aSomeS384 = _M0L6lengthS383;
    _M0L6lengthS382 = (int32_t)_M0L7_2aSomeS384;
  }
  if (_M0L6offsetS385 >= 0) {
    if (_M0L6lengthS382 >= 0) {
      int32_t _M0L6_2atmpS1265 = _M0L6offsetS385 + _M0L6lengthS382;
      _if__result_1838 = _M0L6_2atmpS1265 <= _M0L3lenS380;
    } else {
      _if__result_1838 = 0;
    }
  } else {
    _if__result_1838 = 0;
  }
  if (_if__result_1838) {
    moonbit_incref_cycle_free(_M0L4selfS381);
    #line 85 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    return _M0FPB19unsafe__sub__string(_M0L4selfS381, _M0L6offsetS385, _M0L6lengthS382);
  } else {
    #line 84 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array10FixedArray18blit__from__string(
  moonbit_bytes_t _M0L4selfS372,
  int32_t _M0L13bytes__offsetS367,
  moonbit_string_t _M0L3strS374,
  int32_t _M0L11str__offsetS370,
  int32_t _M0L6lengthS368
) {
  int32_t _M0L6_2atmpS1264;
  int32_t _M0L6_2atmpS1263;
  int32_t _M0L2e1S366;
  int32_t _M0L6_2atmpS1262;
  int32_t _M0L2e2S369;
  int32_t _M0L4len1S371;
  int32_t _M0L4len2S373;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1264 = _M0L6lengthS368 * 2;
  _M0L6_2atmpS1263 = _M0L13bytes__offsetS367 + _M0L6_2atmpS1264;
  _M0L2e1S366 = _M0L6_2atmpS1263 - 1;
  _M0L6_2atmpS1262 = _M0L11str__offsetS370 + _M0L6lengthS368;
  _M0L2e2S369 = _M0L6_2atmpS1262 - 1;
  _M0L4len1S371 = Moonbit_array_length(_M0L4selfS372);
  _M0L4len2S373 = Moonbit_array_length(_M0L3strS374);
  if (
    _M0L6lengthS368 >= 0
    && _M0L13bytes__offsetS367 >= 0
    && _M0L2e1S366 < _M0L4len1S371
    && _M0L11str__offsetS370 >= 0
    && _M0L2e2S369 < _M0L4len2S373
  ) {
    int32_t _M0L16end__str__offsetS375 =
      _M0L11str__offsetS370 + _M0L6lengthS368;
    int32_t _M0L1iS376 = _M0L11str__offsetS370;
    int32_t _M0L1jS377 = _M0L13bytes__offsetS367;
    while (1) {
      if (_M0L1iS376 < _M0L16end__str__offsetS375) {
        int32_t _M0L6_2atmpS1259 = _M0L3strS374[_M0L1iS376];
        int32_t _M0L6_2atmpS1258 = (int32_t)_M0L6_2atmpS1259;
        uint32_t _M0L1cS378 = *(uint32_t*)&_M0L6_2atmpS1258;
        uint32_t _M0L6_2atmpS1254 = _M0L1cS378 & 255u;
        int32_t _M0L6_2atmpS1253;
        int32_t _M0L6_2atmpS1255;
        uint32_t _M0L6_2atmpS1257;
        int32_t _M0L6_2atmpS1256;
        int32_t _M0L6_2atmpS1260;
        int32_t _M0L6_2atmpS1261;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1253 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1254);
        if (
          _M0L1jS377 < 0 || _M0L1jS377 >= Moonbit_array_length(_M0L4selfS372)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS372[_M0L1jS377] = _M0L6_2atmpS1253;
        _M0L6_2atmpS1255 = _M0L1jS377 + 1;
        _M0L6_2atmpS1257 = _M0L1cS378 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1256 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1257);
        if (
          _M0L6_2atmpS1255 < 0
          || _M0L6_2atmpS1255 >= Moonbit_array_length(_M0L4selfS372)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS372[_M0L6_2atmpS1255] = _M0L6_2atmpS1256;
        _M0L6_2atmpS1260 = _M0L1iS376 + 1;
        _M0L6_2atmpS1261 = _M0L1jS377 + 2;
        _M0L1iS376 = _M0L6_2atmpS1260;
        _M0L1jS377 = _M0L6_2atmpS1261;
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

int32_t _M0MPC14uint4UInt8to__byte(uint32_t _M0L4selfS365) {
  int32_t _M0L6_2atmpS1252;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1252 = *(int32_t*)&_M0L4selfS365;
  return _M0L6_2atmpS1252 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS357,
  int32_t _M0L5radixS356
) {
  uint16_t* _M0L6bufferS358;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS356 < 2 || _M0L5radixS356 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_16.data);
  }
  if (_M0L4selfS357 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  switch (_M0L5radixS356) {
    case 10: {
      int32_t _M0L3lenS359;
      uint16_t* _M0L6bufferS360;
      #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS359 = _M0FPB12dec__count64(_M0L4selfS357);
      _M0L6bufferS360 = (uint16_t*)moonbit_make_string(_M0L3lenS359, 0);
      #line 624 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS360, _M0L4selfS357, 0, _M0L3lenS359);
      _M0L6bufferS358 = _M0L6bufferS360;
      break;
    }
    
    case 16: {
      int32_t _M0L3lenS361;
      uint16_t* _M0L6bufferS362;
      #line 628 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS361 = _M0FPB12hex__count64(_M0L4selfS357);
      _M0L6bufferS362 = (uint16_t*)moonbit_make_string(_M0L3lenS361, 0);
      #line 630 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS362, _M0L4selfS357, 0, _M0L3lenS361);
      _M0L6bufferS358 = _M0L6bufferS362;
      break;
    }
    default: {
      int32_t _M0L3lenS363;
      uint16_t* _M0L6bufferS364;
      #line 634 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS363 = _M0FPB14radix__count64(_M0L4selfS357, _M0L5radixS356);
      _M0L6bufferS364 = (uint16_t*)moonbit_make_string(_M0L3lenS363, 0);
      #line 636 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS364, _M0L4selfS357, 0, _M0L3lenS363, _M0L5radixS356);
      _M0L6bufferS358 = _M0L6bufferS364;
      break;
    }
  }
  return _M0L6bufferS358;
}

moonbit_string_t _M0MPC15int645Int6418to__string_2einner(
  int64_t _M0L4selfS340,
  int32_t _M0L5radixS339
) {
  int32_t _M0L12is__negativeS341;
  uint64_t _M0L3numS342;
  uint16_t* _M0L6bufferS343;
  #line 548 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS339 < 2 || _M0L5radixS339 > 36) {
    #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_16.data);
  }
  if (_M0L4selfS340 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  _M0L12is__negativeS341 = _M0L4selfS340 < 0ll;
  if (_M0L12is__negativeS341) {
    int64_t _M0L6_2atmpS1251 = -_M0L4selfS340;
    _M0L3numS342 = *(uint64_t*)&_M0L6_2atmpS1251;
  } else {
    _M0L3numS342 = *(uint64_t*)&_M0L4selfS340;
  }
  switch (_M0L5radixS339) {
    case 10: {
      int32_t _M0L10digit__lenS344;
      int32_t _M0L6_2atmpS1248;
      int32_t _M0L10total__lenS345;
      uint16_t* _M0L6bufferS346;
      int32_t _M0L12digit__startS347;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS344 = _M0FPB12dec__count64(_M0L3numS342);
      if (_M0L12is__negativeS341) {
        _M0L6_2atmpS1248 = 1;
      } else {
        _M0L6_2atmpS1248 = 0;
      }
      _M0L10total__lenS345 = _M0L10digit__lenS344 + _M0L6_2atmpS1248;
      _M0L6bufferS346
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS345, 0);
      if (_M0L12is__negativeS341) {
        _M0L12digit__startS347 = 1;
      } else {
        _M0L12digit__startS347 = 0;
      }
      #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS346, _M0L3numS342, _M0L12digit__startS347, _M0L10total__lenS345);
      _M0L6bufferS343 = _M0L6bufferS346;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS348;
      int32_t _M0L6_2atmpS1249;
      int32_t _M0L10total__lenS349;
      uint16_t* _M0L6bufferS350;
      int32_t _M0L12digit__startS351;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS348 = _M0FPB12hex__count64(_M0L3numS342);
      if (_M0L12is__negativeS341) {
        _M0L6_2atmpS1249 = 1;
      } else {
        _M0L6_2atmpS1249 = 0;
      }
      _M0L10total__lenS349 = _M0L10digit__lenS348 + _M0L6_2atmpS1249;
      _M0L6bufferS350
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS349, 0);
      if (_M0L12is__negativeS341) {
        _M0L12digit__startS351 = 1;
      } else {
        _M0L12digit__startS351 = 0;
      }
      #line 585 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS350, _M0L3numS342, _M0L12digit__startS351, _M0L10total__lenS349);
      _M0L6bufferS343 = _M0L6bufferS350;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS352;
      int32_t _M0L6_2atmpS1250;
      int32_t _M0L10total__lenS353;
      uint16_t* _M0L6bufferS354;
      int32_t _M0L12digit__startS355;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS352
      = _M0FPB14radix__count64(_M0L3numS342, _M0L5radixS339);
      if (_M0L12is__negativeS341) {
        _M0L6_2atmpS1250 = 1;
      } else {
        _M0L6_2atmpS1250 = 0;
      }
      _M0L10total__lenS353 = _M0L10digit__lenS352 + _M0L6_2atmpS1250;
      _M0L6bufferS354
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS353, 0);
      if (_M0L12is__negativeS341) {
        _M0L12digit__startS355 = 1;
      } else {
        _M0L12digit__startS355 = 0;
      }
      #line 593 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS354, _M0L3numS342, _M0L12digit__startS355, _M0L10total__lenS353, _M0L5radixS339);
      _M0L6bufferS343 = _M0L6bufferS354;
      break;
    }
  }
  if (_M0L12is__negativeS341) {
    _M0L6bufferS343[0] = 45;
  }
  return _M0L6bufferS343;
}

int32_t _M0FPB22int64__to__string__dec(
  uint16_t* _M0L6bufferS325,
  uint64_t _M0L3numS337,
  int32_t _M0L12digit__startS326,
  int32_t _M0L10total__lenS338
) {
  int32_t _M0L6_2atmpS1247;
  uint64_t _M0L3numS315;
  int32_t _M0L6offsetS316;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1247 = _M0L10total__lenS338 - _M0L12digit__startS326;
  _M0L3numS315 = _M0L3numS337;
  _M0L6offsetS316 = _M0L6_2atmpS1247;
  while (1) {
    if (_M0L3numS315 >= 10000ull) {
      uint64_t _M0L1tS317 = _M0L3numS315 / 10000ull;
      uint64_t _M0L6_2atmpS1224 = _M0L3numS315 % 10000ull;
      int32_t _M0L1rS318 = (int32_t)_M0L6_2atmpS1224;
      int32_t _M0L2d1S319 = _M0L1rS318 / 100;
      int32_t _M0L2d2S320 = _M0L1rS318 % 100;
      int32_t _M0L6_2atmpS1223 = _M0L2d1S319 / 10;
      int32_t _M0L6_2atmpS1222 = 48 + _M0L6_2atmpS1223;
      int32_t _M0L6d1__hiS321 = (uint16_t)_M0L6_2atmpS1222;
      int32_t _M0L6_2atmpS1221 = _M0L2d1S319 % 10;
      int32_t _M0L6_2atmpS1220 = 48 + _M0L6_2atmpS1221;
      int32_t _M0L6d1__loS322 = (uint16_t)_M0L6_2atmpS1220;
      int32_t _M0L6_2atmpS1219 = _M0L2d2S320 / 10;
      int32_t _M0L6_2atmpS1218 = 48 + _M0L6_2atmpS1219;
      int32_t _M0L6d2__hiS323 = (uint16_t)_M0L6_2atmpS1218;
      int32_t _M0L6_2atmpS1217 = _M0L2d2S320 % 10;
      int32_t _M0L6_2atmpS1216 = 48 + _M0L6_2atmpS1217;
      int32_t _M0L6d2__loS324 = (uint16_t)_M0L6_2atmpS1216;
      int32_t _M0L6_2atmpS1208 = _M0L12digit__startS326 + _M0L6offsetS316;
      int32_t _M0L6_2atmpS1207 = _M0L6_2atmpS1208 - 4;
      int32_t _M0L6_2atmpS1210;
      int32_t _M0L6_2atmpS1209;
      int32_t _M0L6_2atmpS1212;
      int32_t _M0L6_2atmpS1211;
      int32_t _M0L6_2atmpS1214;
      int32_t _M0L6_2atmpS1213;
      int32_t _M0L6_2atmpS1215;
      _M0L6bufferS325[_M0L6_2atmpS1207] = _M0L6d1__hiS321;
      _M0L6_2atmpS1210 = _M0L12digit__startS326 + _M0L6offsetS316;
      _M0L6_2atmpS1209 = _M0L6_2atmpS1210 - 3;
      _M0L6bufferS325[_M0L6_2atmpS1209] = _M0L6d1__loS322;
      _M0L6_2atmpS1212 = _M0L12digit__startS326 + _M0L6offsetS316;
      _M0L6_2atmpS1211 = _M0L6_2atmpS1212 - 2;
      _M0L6bufferS325[_M0L6_2atmpS1211] = _M0L6d2__hiS323;
      _M0L6_2atmpS1214 = _M0L12digit__startS326 + _M0L6offsetS316;
      _M0L6_2atmpS1213 = _M0L6_2atmpS1214 - 1;
      _M0L6bufferS325[_M0L6_2atmpS1213] = _M0L6d2__loS324;
      _M0L6_2atmpS1215 = _M0L6offsetS316 - 4;
      _M0L3numS315 = _M0L1tS317;
      _M0L6offsetS316 = _M0L6_2atmpS1215;
      continue;
    } else {
      int32_t _M0L6_2atmpS1246 = (int32_t)_M0L3numS315;
      int32_t _M0L9remainingS328 = _M0L6_2atmpS1246;
      int32_t _M0L6offsetS329 = _M0L6offsetS316;
      while (1) {
        if (_M0L9remainingS328 >= 100) {
          int32_t _M0L1tS330 = _M0L9remainingS328 / 100;
          int32_t _M0L1dS331 = _M0L9remainingS328 % 100;
          int32_t _M0L6_2atmpS1233 = _M0L1dS331 / 10;
          int32_t _M0L6_2atmpS1232 = 48 + _M0L6_2atmpS1233;
          int32_t _M0L5d__hiS332 = (uint16_t)_M0L6_2atmpS1232;
          int32_t _M0L6_2atmpS1231 = _M0L1dS331 % 10;
          int32_t _M0L6_2atmpS1230 = 48 + _M0L6_2atmpS1231;
          int32_t _M0L5d__loS333 = (uint16_t)_M0L6_2atmpS1230;
          int32_t _M0L6_2atmpS1226 = _M0L12digit__startS326 + _M0L6offsetS329;
          int32_t _M0L6_2atmpS1225 = _M0L6_2atmpS1226 - 2;
          int32_t _M0L6_2atmpS1228;
          int32_t _M0L6_2atmpS1227;
          int32_t _M0L6_2atmpS1229;
          _M0L6bufferS325[_M0L6_2atmpS1225] = _M0L5d__hiS332;
          _M0L6_2atmpS1228 = _M0L12digit__startS326 + _M0L6offsetS329;
          _M0L6_2atmpS1227 = _M0L6_2atmpS1228 - 1;
          _M0L6bufferS325[_M0L6_2atmpS1227] = _M0L5d__loS333;
          _M0L6_2atmpS1229 = _M0L6offsetS329 - 2;
          _M0L9remainingS328 = _M0L1tS330;
          _M0L6offsetS329 = _M0L6_2atmpS1229;
          continue;
        } else if (_M0L9remainingS328 >= 10) {
          int32_t _M0L6_2atmpS1241 = _M0L9remainingS328 / 10;
          int32_t _M0L6_2atmpS1240 = 48 + _M0L6_2atmpS1241;
          int32_t _M0L5d__hiS335 = (uint16_t)_M0L6_2atmpS1240;
          int32_t _M0L6_2atmpS1239 = _M0L9remainingS328 % 10;
          int32_t _M0L6_2atmpS1238 = 48 + _M0L6_2atmpS1239;
          int32_t _M0L5d__loS336 = (uint16_t)_M0L6_2atmpS1238;
          int32_t _M0L6_2atmpS1235 = _M0L12digit__startS326 + _M0L6offsetS329;
          int32_t _M0L6_2atmpS1234 = _M0L6_2atmpS1235 - 2;
          int32_t _M0L6_2atmpS1237;
          int32_t _M0L6_2atmpS1236;
          _M0L6bufferS325[_M0L6_2atmpS1234] = _M0L5d__hiS335;
          _M0L6_2atmpS1237 = _M0L12digit__startS326 + _M0L6offsetS329;
          _M0L6_2atmpS1236 = _M0L6_2atmpS1237 - 1;
          _M0L6bufferS325[_M0L6_2atmpS1236] = _M0L5d__loS336;
        } else {
          int32_t _M0L6_2atmpS1245 = _M0L12digit__startS326 + _M0L6offsetS329;
          int32_t _M0L6_2atmpS1242 = _M0L6_2atmpS1245 - 1;
          int32_t _M0L6_2atmpS1244 = 48 + _M0L9remainingS328;
          int32_t _M0L6_2atmpS1243 = (uint16_t)_M0L6_2atmpS1244;
          _M0L6bufferS325[_M0L6_2atmpS1242] = _M0L6_2atmpS1243;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB26int64__to__string__generic(
  uint16_t* _M0L6bufferS305,
  uint64_t _M0L3numS309,
  int32_t _M0L12digit__startS306,
  int32_t _M0L10total__lenS308,
  int32_t _M0L5radixS299
) {
  uint64_t _M0L4baseS298;
  int32_t _M0L6_2atmpS1192;
  int32_t _M0L6_2atmpS1191;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS298 = _M0MPC13int3Int10to__uint64(_M0L5radixS299);
  _M0L6_2atmpS1192 = _M0L5radixS299 - 1;
  _M0L6_2atmpS1191 = _M0L5radixS299 & _M0L6_2atmpS1192;
  if (_M0L6_2atmpS1191 == 0) {
    int32_t _M0L5shiftS300;
    uint64_t _M0L4maskS301;
    int32_t _M0L6_2atmpS1199;
    int32_t _M0L6offsetS302;
    uint64_t _M0L1nS303;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS300 = moonbit_ctz32(_M0L5radixS299);
    _M0L4maskS301 = _M0L4baseS298 - 1ull;
    _M0L6_2atmpS1199 = _M0L10total__lenS308 - _M0L12digit__startS306;
    _M0L6offsetS302 = _M0L6_2atmpS1199;
    _M0L1nS303 = _M0L3numS309;
    while (1) {
      if (_M0L1nS303 > 0ull) {
        uint64_t _M0L6_2atmpS1198 = _M0L1nS303 & _M0L4maskS301;
        int32_t _M0L5digitS304 = (int32_t)_M0L6_2atmpS1198;
        int32_t _M0L6_2atmpS1195 = _M0L12digit__startS306 + _M0L6offsetS302;
        int32_t _M0L6_2atmpS1193 = _M0L6_2atmpS1195 - 1;
        int32_t _M0L6_2atmpS1194 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS304];
        int32_t _M0L6_2atmpS1196;
        uint64_t _M0L6_2atmpS1197;
        _M0L6bufferS305[_M0L6_2atmpS1193] = _M0L6_2atmpS1194;
        _M0L6_2atmpS1196 = _M0L6offsetS302 - 1;
        _M0L6_2atmpS1197 = _M0L1nS303 >> (_M0L5shiftS300 & 63);
        _M0L6offsetS302 = _M0L6_2atmpS1196;
        _M0L1nS303 = _M0L6_2atmpS1197;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1206 = _M0L10total__lenS308 - _M0L12digit__startS306;
    int32_t _M0L6offsetS310 = _M0L6_2atmpS1206;
    uint64_t _M0L1nS311 = _M0L3numS309;
    while (1) {
      if (_M0L1nS311 > 0ull) {
        uint64_t _M0L1qS312 = _M0L1nS311 / _M0L4baseS298;
        uint64_t _M0L6_2atmpS1205 = _M0L1qS312 * _M0L4baseS298;
        uint64_t _M0L6_2atmpS1204 = _M0L1nS311 - _M0L6_2atmpS1205;
        int32_t _M0L5digitS313 = (int32_t)_M0L6_2atmpS1204;
        int32_t _M0L6_2atmpS1202 = _M0L12digit__startS306 + _M0L6offsetS310;
        int32_t _M0L6_2atmpS1200 = _M0L6_2atmpS1202 - 1;
        int32_t _M0L6_2atmpS1201 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS313];
        int32_t _M0L6_2atmpS1203;
        _M0L6bufferS305[_M0L6_2atmpS1200] = _M0L6_2atmpS1201;
        _M0L6_2atmpS1203 = _M0L6offsetS310 - 1;
        _M0L6offsetS310 = _M0L6_2atmpS1203;
        _M0L1nS311 = _M0L1qS312;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB22int64__to__string__hex(
  uint16_t* _M0L6bufferS292,
  uint64_t _M0L3numS297,
  int32_t _M0L12digit__startS293,
  int32_t _M0L10total__lenS296
) {
  int32_t _M0L6_2atmpS1190;
  int32_t _M0L6offsetS287;
  uint64_t _M0L1nS288;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1190 = _M0L10total__lenS296 - _M0L12digit__startS293;
  _M0L6offsetS287 = _M0L6_2atmpS1190;
  _M0L1nS288 = _M0L3numS297;
  while (1) {
    if (_M0L6offsetS287 >= 2) {
      uint64_t _M0L6_2atmpS1187 = _M0L1nS288 & 255ull;
      int32_t _M0L9byte__valS289 = (int32_t)_M0L6_2atmpS1187;
      int32_t _M0L2hiS290 = _M0L9byte__valS289 / 16;
      int32_t _M0L2loS291 = _M0L9byte__valS289 % 16;
      int32_t _M0L6_2atmpS1181 = _M0L12digit__startS293 + _M0L6offsetS287;
      int32_t _M0L6_2atmpS1179 = _M0L6_2atmpS1181 - 2;
      int32_t _M0L6_2atmpS1180 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L2hiS290];
      int32_t _M0L6_2atmpS1184;
      int32_t _M0L6_2atmpS1182;
      int32_t _M0L6_2atmpS1183;
      int32_t _M0L6_2atmpS1185;
      uint64_t _M0L6_2atmpS1186;
      _M0L6bufferS292[_M0L6_2atmpS1179] = _M0L6_2atmpS1180;
      _M0L6_2atmpS1184 = _M0L12digit__startS293 + _M0L6offsetS287;
      _M0L6_2atmpS1182 = _M0L6_2atmpS1184 - 1;
      _M0L6_2atmpS1183
      = ((moonbit_string_t)moonbit_string_literal_17.data)[
        _M0L2loS291
      ];
      _M0L6bufferS292[_M0L6_2atmpS1182] = _M0L6_2atmpS1183;
      _M0L6_2atmpS1185 = _M0L6offsetS287 - 2;
      _M0L6_2atmpS1186 = _M0L1nS288 >> 8;
      _M0L6offsetS287 = _M0L6_2atmpS1185;
      _M0L1nS288 = _M0L6_2atmpS1186;
      continue;
    } else if (_M0L6offsetS287 == 1) {
      uint64_t _M0L6_2atmpS1189 = _M0L1nS288 & 15ull;
      int32_t _M0L6nibbleS295 = (int32_t)_M0L6_2atmpS1189;
      int32_t _M0L6_2atmpS1188 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L6nibbleS295];
      _M0L6bufferS292[_M0L12digit__startS293] = _M0L6_2atmpS1188;
    }
    break;
  }
  return 0;
}

int32_t _M0FPB14radix__count64(
  uint64_t _M0L5valueS281,
  int32_t _M0L5radixS283
) {
  uint64_t _M0L4baseS282;
  uint64_t _M0L3numS284;
  int32_t _M0L5countS285;
  #line 419 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS281 == 0ull) {
    return 1;
  }
  #line 424 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS282 = _M0MPC13int3Int10to__uint64(_M0L5radixS283);
  _M0L3numS284 = _M0L5valueS281;
  _M0L5countS285 = 0;
  while (1) {
    if (_M0L3numS284 > 0ull) {
      uint64_t _M0L6_2atmpS1177 = _M0L3numS284 / _M0L4baseS282;
      int32_t _M0L6_2atmpS1178 = _M0L5countS285 + 1;
      _M0L3numS284 = _M0L6_2atmpS1177;
      _M0L5countS285 = _M0L6_2atmpS1178;
      continue;
    } else {
      return _M0L5countS285;
    }
    break;
  }
}

int32_t _M0FPB12hex__count64(uint64_t _M0L5valueS279) {
  #line 407 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS279 == 0ull) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS280;
    int32_t _M0L6_2atmpS1176;
    int32_t _M0L6_2atmpS1175;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS280 = moonbit_clz64(_M0L5valueS279);
    _M0L6_2atmpS1176 = 63 - _M0L14leading__zerosS280;
    _M0L6_2atmpS1175 = _M0L6_2atmpS1176 / 4;
    return _M0L6_2atmpS1175 + 1;
  }
}

int32_t _M0FPB12dec__count64(uint64_t _M0L5valueS278) {
  #line 343 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS278 >= 10000000000ull) {
    if (_M0L5valueS278 >= 100000000000000ull) {
      if (_M0L5valueS278 >= 10000000000000000ull) {
        if (_M0L5valueS278 >= 1000000000000000000ull) {
          if (_M0L5valueS278 >= 10000000000000000000ull) {
            return 20;
          } else {
            return 19;
          }
        } else if (_M0L5valueS278 >= 100000000000000000ull) {
          return 18;
        } else {
          return 17;
        }
      } else if (_M0L5valueS278 >= 1000000000000000ull) {
        return 16;
      } else {
        return 15;
      }
    } else if (_M0L5valueS278 >= 1000000000000ull) {
      if (_M0L5valueS278 >= 10000000000000ull) {
        return 14;
      } else {
        return 13;
      }
    } else if (_M0L5valueS278 >= 100000000000ull) {
      return 12;
    } else {
      return 11;
    }
  } else if (_M0L5valueS278 >= 100000ull) {
    if (_M0L5valueS278 >= 10000000ull) {
      if (_M0L5valueS278 >= 1000000000ull) {
        return 10;
      } else if (_M0L5valueS278 >= 100000000ull) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS278 >= 1000000ull) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS278 >= 1000ull) {
    if (_M0L5valueS278 >= 10000ull) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS278 >= 100ull) {
    return 3;
  } else if (_M0L5valueS278 >= 10ull) {
    return 2;
  } else {
    return 1;
  }
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS262,
  int32_t _M0L5radixS261
) {
  int32_t _M0L12is__negativeS263;
  uint32_t _M0L3numS264;
  uint16_t* _M0L6bufferS265;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS261 < 2 || _M0L5radixS261 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_16.data);
  }
  if (_M0L4selfS262 == 0) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  _M0L12is__negativeS263 = _M0L4selfS262 < 0;
  if (_M0L12is__negativeS263) {
    int32_t _M0L6_2atmpS1174 = -_M0L4selfS262;
    _M0L3numS264 = *(uint32_t*)&_M0L6_2atmpS1174;
  } else {
    _M0L3numS264 = *(uint32_t*)&_M0L4selfS262;
  }
  switch (_M0L5radixS261) {
    case 10: {
      int32_t _M0L10digit__lenS266;
      int32_t _M0L6_2atmpS1171;
      int32_t _M0L10total__lenS267;
      uint16_t* _M0L6bufferS268;
      int32_t _M0L12digit__startS269;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS266 = _M0FPB12dec__count32(_M0L3numS264);
      if (_M0L12is__negativeS263) {
        _M0L6_2atmpS1171 = 1;
      } else {
        _M0L6_2atmpS1171 = 0;
      }
      _M0L10total__lenS267 = _M0L10digit__lenS266 + _M0L6_2atmpS1171;
      _M0L6bufferS268
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS267, 0);
      if (_M0L12is__negativeS263) {
        _M0L12digit__startS269 = 1;
      } else {
        _M0L12digit__startS269 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS268, _M0L3numS264, _M0L12digit__startS269, _M0L10total__lenS267);
      _M0L6bufferS265 = _M0L6bufferS268;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS270;
      int32_t _M0L6_2atmpS1172;
      int32_t _M0L10total__lenS271;
      uint16_t* _M0L6bufferS272;
      int32_t _M0L12digit__startS273;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS270 = _M0FPB12hex__count32(_M0L3numS264);
      if (_M0L12is__negativeS263) {
        _M0L6_2atmpS1172 = 1;
      } else {
        _M0L6_2atmpS1172 = 0;
      }
      _M0L10total__lenS271 = _M0L10digit__lenS270 + _M0L6_2atmpS1172;
      _M0L6bufferS272
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS271, 0);
      if (_M0L12is__negativeS263) {
        _M0L12digit__startS273 = 1;
      } else {
        _M0L12digit__startS273 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS272, _M0L3numS264, _M0L12digit__startS273, _M0L10total__lenS271);
      _M0L6bufferS265 = _M0L6bufferS272;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS274;
      int32_t _M0L6_2atmpS1173;
      int32_t _M0L10total__lenS275;
      uint16_t* _M0L6bufferS276;
      int32_t _M0L12digit__startS277;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS274
      = _M0FPB14radix__count32(_M0L3numS264, _M0L5radixS261);
      if (_M0L12is__negativeS263) {
        _M0L6_2atmpS1173 = 1;
      } else {
        _M0L6_2atmpS1173 = 0;
      }
      _M0L10total__lenS275 = _M0L10digit__lenS274 + _M0L6_2atmpS1173;
      _M0L6bufferS276
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS275, 0);
      if (_M0L12is__negativeS263) {
        _M0L12digit__startS277 = 1;
      } else {
        _M0L12digit__startS277 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS276, _M0L3numS264, _M0L12digit__startS277, _M0L10total__lenS275, _M0L5radixS261);
      _M0L6bufferS265 = _M0L6bufferS276;
      break;
    }
  }
  if (_M0L12is__negativeS263) {
    _M0L6bufferS265[0] = 45;
  }
  return _M0L6bufferS265;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS255,
  int32_t _M0L5radixS257
) {
  uint32_t _M0L4baseS256;
  uint32_t _M0L3numS258;
  int32_t _M0L5countS259;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS255 == 0u) {
    return 1;
  }
  _M0L4baseS256 = *(uint32_t*)&_M0L5radixS257;
  _M0L3numS258 = _M0L5valueS255;
  _M0L5countS259 = 0;
  while (1) {
    if (_M0L3numS258 > 0u) {
      uint32_t _M0L6_2atmpS1169 = _M0L3numS258 / _M0L4baseS256;
      int32_t _M0L6_2atmpS1170 = _M0L5countS259 + 1;
      _M0L3numS258 = _M0L6_2atmpS1169;
      _M0L5countS259 = _M0L6_2atmpS1170;
      continue;
    } else {
      return _M0L5countS259;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS253) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS253 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS254;
    int32_t _M0L6_2atmpS1168;
    int32_t _M0L6_2atmpS1167;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS254 = moonbit_clz32(_M0L5valueS253);
    _M0L6_2atmpS1168 = 31 - _M0L14leading__zerosS254;
    _M0L6_2atmpS1167 = _M0L6_2atmpS1168 / 4;
    return _M0L6_2atmpS1167 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS252) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS252 >= 100000u) {
    if (_M0L5valueS252 >= 10000000u) {
      if (_M0L5valueS252 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS252 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS252 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS252 >= 1000u) {
    if (_M0L5valueS252 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS252 >= 100u) {
    return 3;
  } else if (_M0L5valueS252 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS238,
  uint32_t _M0L3numS250,
  int32_t _M0L12digit__startS239,
  int32_t _M0L10total__lenS251
) {
  int32_t _M0L6_2atmpS1166;
  uint32_t _M0L3numS228;
  int32_t _M0L6offsetS229;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1166 = _M0L10total__lenS251 - _M0L12digit__startS239;
  _M0L3numS228 = _M0L3numS250;
  _M0L6offsetS229 = _M0L6_2atmpS1166;
  while (1) {
    if (_M0L3numS228 >= 10000u) {
      uint32_t _M0L1tS230 = _M0L3numS228 / 10000u;
      uint32_t _M0L6_2atmpS1143 = _M0L3numS228 % 10000u;
      int32_t _M0L1rS231 = *(int32_t*)&_M0L6_2atmpS1143;
      int32_t _M0L2d1S232 = _M0L1rS231 / 100;
      int32_t _M0L2d2S233 = _M0L1rS231 % 100;
      int32_t _M0L6_2atmpS1142 = _M0L2d1S232 / 10;
      int32_t _M0L6_2atmpS1141 = 48 + _M0L6_2atmpS1142;
      int32_t _M0L6d1__hiS234 = (uint16_t)_M0L6_2atmpS1141;
      int32_t _M0L6_2atmpS1140 = _M0L2d1S232 % 10;
      int32_t _M0L6_2atmpS1139 = 48 + _M0L6_2atmpS1140;
      int32_t _M0L6d1__loS235 = (uint16_t)_M0L6_2atmpS1139;
      int32_t _M0L6_2atmpS1138 = _M0L2d2S233 / 10;
      int32_t _M0L6_2atmpS1137 = 48 + _M0L6_2atmpS1138;
      int32_t _M0L6d2__hiS236 = (uint16_t)_M0L6_2atmpS1137;
      int32_t _M0L6_2atmpS1136 = _M0L2d2S233 % 10;
      int32_t _M0L6_2atmpS1135 = 48 + _M0L6_2atmpS1136;
      int32_t _M0L6d2__loS237 = (uint16_t)_M0L6_2atmpS1135;
      int32_t _M0L6_2atmpS1127 = _M0L12digit__startS239 + _M0L6offsetS229;
      int32_t _M0L6_2atmpS1126 = _M0L6_2atmpS1127 - 4;
      int32_t _M0L6_2atmpS1129;
      int32_t _M0L6_2atmpS1128;
      int32_t _M0L6_2atmpS1131;
      int32_t _M0L6_2atmpS1130;
      int32_t _M0L6_2atmpS1133;
      int32_t _M0L6_2atmpS1132;
      int32_t _M0L6_2atmpS1134;
      _M0L6bufferS238[_M0L6_2atmpS1126] = _M0L6d1__hiS234;
      _M0L6_2atmpS1129 = _M0L12digit__startS239 + _M0L6offsetS229;
      _M0L6_2atmpS1128 = _M0L6_2atmpS1129 - 3;
      _M0L6bufferS238[_M0L6_2atmpS1128] = _M0L6d1__loS235;
      _M0L6_2atmpS1131 = _M0L12digit__startS239 + _M0L6offsetS229;
      _M0L6_2atmpS1130 = _M0L6_2atmpS1131 - 2;
      _M0L6bufferS238[_M0L6_2atmpS1130] = _M0L6d2__hiS236;
      _M0L6_2atmpS1133 = _M0L12digit__startS239 + _M0L6offsetS229;
      _M0L6_2atmpS1132 = _M0L6_2atmpS1133 - 1;
      _M0L6bufferS238[_M0L6_2atmpS1132] = _M0L6d2__loS237;
      _M0L6_2atmpS1134 = _M0L6offsetS229 - 4;
      _M0L3numS228 = _M0L1tS230;
      _M0L6offsetS229 = _M0L6_2atmpS1134;
      continue;
    } else {
      int32_t _M0L6_2atmpS1165 = *(int32_t*)&_M0L3numS228;
      int32_t _M0L9remainingS241 = _M0L6_2atmpS1165;
      int32_t _M0L6offsetS242 = _M0L6offsetS229;
      while (1) {
        if (_M0L9remainingS241 >= 100) {
          int32_t _M0L1tS243 = _M0L9remainingS241 / 100;
          int32_t _M0L1dS244 = _M0L9remainingS241 % 100;
          int32_t _M0L6_2atmpS1152 = _M0L1dS244 / 10;
          int32_t _M0L6_2atmpS1151 = 48 + _M0L6_2atmpS1152;
          int32_t _M0L5d__hiS245 = (uint16_t)_M0L6_2atmpS1151;
          int32_t _M0L6_2atmpS1150 = _M0L1dS244 % 10;
          int32_t _M0L6_2atmpS1149 = 48 + _M0L6_2atmpS1150;
          int32_t _M0L5d__loS246 = (uint16_t)_M0L6_2atmpS1149;
          int32_t _M0L6_2atmpS1145 = _M0L12digit__startS239 + _M0L6offsetS242;
          int32_t _M0L6_2atmpS1144 = _M0L6_2atmpS1145 - 2;
          int32_t _M0L6_2atmpS1147;
          int32_t _M0L6_2atmpS1146;
          int32_t _M0L6_2atmpS1148;
          _M0L6bufferS238[_M0L6_2atmpS1144] = _M0L5d__hiS245;
          _M0L6_2atmpS1147 = _M0L12digit__startS239 + _M0L6offsetS242;
          _M0L6_2atmpS1146 = _M0L6_2atmpS1147 - 1;
          _M0L6bufferS238[_M0L6_2atmpS1146] = _M0L5d__loS246;
          _M0L6_2atmpS1148 = _M0L6offsetS242 - 2;
          _M0L9remainingS241 = _M0L1tS243;
          _M0L6offsetS242 = _M0L6_2atmpS1148;
          continue;
        } else if (_M0L9remainingS241 >= 10) {
          int32_t _M0L6_2atmpS1160 = _M0L9remainingS241 / 10;
          int32_t _M0L6_2atmpS1159 = 48 + _M0L6_2atmpS1160;
          int32_t _M0L5d__hiS248 = (uint16_t)_M0L6_2atmpS1159;
          int32_t _M0L6_2atmpS1158 = _M0L9remainingS241 % 10;
          int32_t _M0L6_2atmpS1157 = 48 + _M0L6_2atmpS1158;
          int32_t _M0L5d__loS249 = (uint16_t)_M0L6_2atmpS1157;
          int32_t _M0L6_2atmpS1154 = _M0L12digit__startS239 + _M0L6offsetS242;
          int32_t _M0L6_2atmpS1153 = _M0L6_2atmpS1154 - 2;
          int32_t _M0L6_2atmpS1156;
          int32_t _M0L6_2atmpS1155;
          _M0L6bufferS238[_M0L6_2atmpS1153] = _M0L5d__hiS248;
          _M0L6_2atmpS1156 = _M0L12digit__startS239 + _M0L6offsetS242;
          _M0L6_2atmpS1155 = _M0L6_2atmpS1156 - 1;
          _M0L6bufferS238[_M0L6_2atmpS1155] = _M0L5d__loS249;
        } else {
          int32_t _M0L6_2atmpS1164 = _M0L12digit__startS239 + _M0L6offsetS242;
          int32_t _M0L6_2atmpS1161 = _M0L6_2atmpS1164 - 1;
          int32_t _M0L6_2atmpS1163 = 48 + _M0L9remainingS241;
          int32_t _M0L6_2atmpS1162 = (uint16_t)_M0L6_2atmpS1163;
          _M0L6bufferS238[_M0L6_2atmpS1161] = _M0L6_2atmpS1162;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS218,
  uint32_t _M0L3numS222,
  int32_t _M0L12digit__startS219,
  int32_t _M0L10total__lenS221,
  int32_t _M0L5radixS212
) {
  uint32_t _M0L4baseS211;
  int32_t _M0L6_2atmpS1111;
  int32_t _M0L6_2atmpS1110;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS211 = *(uint32_t*)&_M0L5radixS212;
  _M0L6_2atmpS1111 = _M0L5radixS212 - 1;
  _M0L6_2atmpS1110 = _M0L5radixS212 & _M0L6_2atmpS1111;
  if (_M0L6_2atmpS1110 == 0) {
    int32_t _M0L5shiftS213;
    uint32_t _M0L4maskS214;
    int32_t _M0L6_2atmpS1118;
    int32_t _M0L6offsetS215;
    uint32_t _M0L1nS216;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS213 = moonbit_ctz32(_M0L5radixS212);
    _M0L4maskS214 = _M0L4baseS211 - 1u;
    _M0L6_2atmpS1118 = _M0L10total__lenS221 - _M0L12digit__startS219;
    _M0L6offsetS215 = _M0L6_2atmpS1118;
    _M0L1nS216 = _M0L3numS222;
    while (1) {
      if (_M0L1nS216 > 0u) {
        uint32_t _M0L6_2atmpS1117 = _M0L1nS216 & _M0L4maskS214;
        int32_t _M0L5digitS217 = *(int32_t*)&_M0L6_2atmpS1117;
        int32_t _M0L6_2atmpS1114 = _M0L12digit__startS219 + _M0L6offsetS215;
        int32_t _M0L6_2atmpS1112 = _M0L6_2atmpS1114 - 1;
        int32_t _M0L6_2atmpS1113 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS217];
        int32_t _M0L6_2atmpS1115;
        uint32_t _M0L6_2atmpS1116;
        _M0L6bufferS218[_M0L6_2atmpS1112] = _M0L6_2atmpS1113;
        _M0L6_2atmpS1115 = _M0L6offsetS215 - 1;
        _M0L6_2atmpS1116 = _M0L1nS216 >> (_M0L5shiftS213 & 31);
        _M0L6offsetS215 = _M0L6_2atmpS1115;
        _M0L1nS216 = _M0L6_2atmpS1116;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1125 = _M0L10total__lenS221 - _M0L12digit__startS219;
    int32_t _M0L6offsetS223 = _M0L6_2atmpS1125;
    uint32_t _M0L1nS224 = _M0L3numS222;
    while (1) {
      if (_M0L1nS224 > 0u) {
        uint32_t _M0L1qS225 = _M0L1nS224 / _M0L4baseS211;
        uint32_t _M0L6_2atmpS1124 = _M0L1qS225 * _M0L4baseS211;
        uint32_t _M0L6_2atmpS1123 = _M0L1nS224 - _M0L6_2atmpS1124;
        int32_t _M0L5digitS226 = *(int32_t*)&_M0L6_2atmpS1123;
        int32_t _M0L6_2atmpS1121 = _M0L12digit__startS219 + _M0L6offsetS223;
        int32_t _M0L6_2atmpS1119 = _M0L6_2atmpS1121 - 1;
        int32_t _M0L6_2atmpS1120 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS226];
        int32_t _M0L6_2atmpS1122;
        _M0L6bufferS218[_M0L6_2atmpS1119] = _M0L6_2atmpS1120;
        _M0L6_2atmpS1122 = _M0L6offsetS223 - 1;
        _M0L6offsetS223 = _M0L6_2atmpS1122;
        _M0L1nS224 = _M0L1qS225;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS205,
  uint32_t _M0L3numS210,
  int32_t _M0L12digit__startS206,
  int32_t _M0L10total__lenS209
) {
  int32_t _M0L6_2atmpS1109;
  int32_t _M0L6offsetS200;
  uint32_t _M0L1nS201;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1109 = _M0L10total__lenS209 - _M0L12digit__startS206;
  _M0L6offsetS200 = _M0L6_2atmpS1109;
  _M0L1nS201 = _M0L3numS210;
  while (1) {
    if (_M0L6offsetS200 >= 2) {
      uint32_t _M0L6_2atmpS1106 = _M0L1nS201 & 255u;
      int32_t _M0L9byte__valS202 = *(int32_t*)&_M0L6_2atmpS1106;
      int32_t _M0L2hiS203 = _M0L9byte__valS202 / 16;
      int32_t _M0L2loS204 = _M0L9byte__valS202 % 16;
      int32_t _M0L6_2atmpS1100 = _M0L12digit__startS206 + _M0L6offsetS200;
      int32_t _M0L6_2atmpS1098 = _M0L6_2atmpS1100 - 2;
      int32_t _M0L6_2atmpS1099 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L2hiS203];
      int32_t _M0L6_2atmpS1103;
      int32_t _M0L6_2atmpS1101;
      int32_t _M0L6_2atmpS1102;
      int32_t _M0L6_2atmpS1104;
      uint32_t _M0L6_2atmpS1105;
      _M0L6bufferS205[_M0L6_2atmpS1098] = _M0L6_2atmpS1099;
      _M0L6_2atmpS1103 = _M0L12digit__startS206 + _M0L6offsetS200;
      _M0L6_2atmpS1101 = _M0L6_2atmpS1103 - 1;
      _M0L6_2atmpS1102
      = ((moonbit_string_t)moonbit_string_literal_17.data)[
        _M0L2loS204
      ];
      _M0L6bufferS205[_M0L6_2atmpS1101] = _M0L6_2atmpS1102;
      _M0L6_2atmpS1104 = _M0L6offsetS200 - 2;
      _M0L6_2atmpS1105 = _M0L1nS201 >> 8;
      _M0L6offsetS200 = _M0L6_2atmpS1104;
      _M0L1nS201 = _M0L6_2atmpS1105;
      continue;
    } else if (_M0L6offsetS200 == 1) {
      uint32_t _M0L6_2atmpS1108 = _M0L1nS201 & 15u;
      int32_t _M0L6nibbleS208 = *(int32_t*)&_M0L6_2atmpS1108;
      int32_t _M0L6_2atmpS1107 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L6nibbleS208];
      _M0L6bufferS205[_M0L12digit__startS206] = _M0L6_2atmpS1107;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS199
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS198;
  struct _M0TPB6Logger _M0L6_2atmpS1097;
  moonbit_string_t _result_1852;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS198);
  _M0L6_2atmpS1097
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS198
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS199, _M0L6_2atmpS1097);
  if (_M0L6_2atmpS1097.$1) {
    moonbit_decref(_M0L6_2atmpS1097.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_1852 = _M0MPB13StringBuilder10to__string(_M0L6loggerS198);
  moonbit_decref_cycle_free(_M0L6loggerS198);
  return _result_1852;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS193,
  struct _M0TPB6Logger _M0L6loggerS192
) {
  moonbit_string_t _M0L6_2atmpS1094;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1094 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS193);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS192.$0->$method_0(_M0L6loggerS192.$1, _M0L6_2atmpS1094);
  moonbit_decref_cycle_free(_M0L6_2atmpS1094);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS195,
  struct _M0TPB6Logger _M0L6loggerS194
) {
  moonbit_string_t _M0L6_2atmpS1095;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1095 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS195);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS194.$0->$method_0(_M0L6loggerS194.$1, _M0L6_2atmpS1095);
  moonbit_decref_cycle_free(_M0L6_2atmpS1095);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS197,
  struct _M0TPB6Logger _M0L6loggerS196
) {
  moonbit_string_t _M0L6_2atmpS1096;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1096 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS197);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS196.$0->$method_0(_M0L6loggerS196.$1, _M0L6_2atmpS1096);
  moonbit_decref_cycle_free(_M0L6_2atmpS1096);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS191
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS191.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS190
) {
  moonbit_string_t _M0L8_2afieldS1763;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS1763 = _M0L4selfS190.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1763);
  return _M0L8_2afieldS1763;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS186,
  moonbit_string_t _M0L5valueS187,
  int32_t _M0L5startS188,
  int32_t _M0L3lenS189
) {
  int32_t _M0L6_2atmpS1093;
  int64_t _M0L6_2atmpS1092;
  struct _M0TPC16string10StringView _M0L6_2atmpS1091;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1093 = _M0L5startS188 + _M0L3lenS189;
  _M0L6_2atmpS1092 = (int64_t)_M0L6_2atmpS1093;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1091
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS187, _M0L5startS188, _M0L6_2atmpS1092);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS186, _M0L6_2atmpS1091);
  moonbit_decref_cycle_free(_M0L6_2atmpS1091.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String21clamped__view_2einner(
  moonbit_string_t _M0L4selfS179,
  int32_t _M0L5startS181,
  int64_t _M0L3endS183
) {
  int32_t _M0L3lenS178;
  int32_t _M0Lm2loS180;
  int32_t _M0Lm2hiS182;
  int32_t _M0L6_2atmpS1075;
  int32_t _if__result_1853;
  int32_t _M0L6_2atmpS1083;
  int32_t _if__result_1854;
  int32_t _M0L6_2atmpS1085;
  int32_t _M0L6_2atmpS1086;
  #line 698 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS178 = Moonbit_array_length(_M0L4selfS179);
  if (_M0L5startS181 < 0) {
    _M0Lm2loS180 = 0;
  } else if (_M0L5startS181 > _M0L3lenS178) {
    _M0Lm2loS180 = _M0L3lenS178;
  } else {
    _M0Lm2loS180 = _M0L5startS181;
  }
  if (_M0L3endS183 == 4294967296ll) {
    _M0Lm2hiS182 = _M0L3lenS178;
  } else {
    int64_t _M0L7_2aSomeS184 = _M0L3endS183;
    int32_t _M0L4_2aeS185 = (int32_t)_M0L7_2aSomeS184;
    if (_M0L4_2aeS185 < 0) {
      _M0Lm2hiS182 = 0;
    } else if (_M0L4_2aeS185 > _M0L3lenS178) {
      _M0Lm2hiS182 = _M0L3lenS178;
    } else {
      _M0Lm2hiS182 = _M0L4_2aeS185;
    }
  }
  _M0L6_2atmpS1075 = _M0Lm2loS180;
  if (_M0L6_2atmpS1075 > 0) {
    int32_t _M0L6_2atmpS1074 = _M0Lm2loS180;
    if (_M0L6_2atmpS1074 < _M0L3lenS178) {
      int32_t _M0L6_2atmpS1073 = _M0Lm2loS180;
      int32_t _M0L6_2atmpS1072 = _M0L4selfS179[_M0L6_2atmpS1073];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1072)) {
        int32_t _M0L6_2atmpS1071 = _M0Lm2loS180;
        int32_t _M0L6_2atmpS1070 = _M0L6_2atmpS1071 - 1;
        int32_t _M0L6_2atmpS1069 = _M0L4selfS179[_M0L6_2atmpS1070];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_1853
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1069);
      } else {
        _if__result_1853 = 0;
      }
    } else {
      _if__result_1853 = 0;
    }
  } else {
    _if__result_1853 = 0;
  }
  if (_if__result_1853) {
    int32_t _M0L6_2atmpS1076 = _M0Lm2loS180;
    _M0Lm2loS180 = _M0L6_2atmpS1076 + 1;
  }
  _M0L6_2atmpS1083 = _M0Lm2hiS182;
  if (_M0L6_2atmpS1083 > 0) {
    int32_t _M0L6_2atmpS1082 = _M0Lm2hiS182;
    if (_M0L6_2atmpS1082 < _M0L3lenS178) {
      int32_t _M0L6_2atmpS1081 = _M0Lm2hiS182;
      int32_t _M0L6_2atmpS1080 = _M0L4selfS179[_M0L6_2atmpS1081];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1080)) {
        int32_t _M0L6_2atmpS1079 = _M0Lm2hiS182;
        int32_t _M0L6_2atmpS1078 = _M0L6_2atmpS1079 - 1;
        int32_t _M0L6_2atmpS1077 = _M0L4selfS179[_M0L6_2atmpS1078];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_1854
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1077);
      } else {
        _if__result_1854 = 0;
      }
    } else {
      _if__result_1854 = 0;
    }
  } else {
    _if__result_1854 = 0;
  }
  if (_if__result_1854) {
    int32_t _M0L6_2atmpS1084 = _M0Lm2hiS182;
    _M0Lm2hiS182 = _M0L6_2atmpS1084 - 1;
  }
  _M0L6_2atmpS1085 = _M0Lm2loS180;
  _M0L6_2atmpS1086 = _M0Lm2hiS182;
  if (_M0L6_2atmpS1085 >= _M0L6_2atmpS1086) {
    int32_t _M0L6_2atmpS1087 = _M0Lm2loS180;
    int32_t _M0L6_2atmpS1088 = _M0Lm2loS180;
    moonbit_incref_cycle_free(_M0L4selfS179);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS179,
                                                 .$1 = _M0L6_2atmpS1087,
                                                 .$2 = _M0L6_2atmpS1088};
  } else {
    int32_t _M0L6_2atmpS1089 = _M0Lm2loS180;
    int32_t _M0L6_2atmpS1090 = _M0Lm2hiS182;
    moonbit_incref_cycle_free(_M0L4selfS179);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS179,
                                                 .$1 = _M0L6_2atmpS1089,
                                                 .$2 = _M0L6_2atmpS1090};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS177,
  struct _M0TPB4Show _M0L4showS176
) {
  struct _M0TPB6Logger _M0L6_2atmpS1068;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS177);
  _M0L6_2atmpS1068
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS177
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS176.$0->$method_0(_M0L4showS176.$1, _M0L6_2atmpS1068);
  if (_M0L6_2atmpS1068.$1) {
    moonbit_decref(_M0L6_2atmpS1068.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS175,
  struct _M0TPB4Show _M0L4showS174
) {
  struct _M0TPB6Logger _M0L6_2atmpS1067;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS175);
  _M0L6_2atmpS1067
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS175
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS174.$0->$method_0(_M0L4showS174.$1, _M0L6_2atmpS1067);
  if (_M0L6_2atmpS1067.$1) {
    moonbit_decref(_M0L6_2atmpS1067.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS173) {
  int64_t _M0L6_2atmpS1066;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1066 = (int64_t)_M0L4selfS173;
  return *(uint64_t*)&_M0L6_2atmpS1066;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

moonbit_string_t _M0MPC16string6String14escape_2einner(
  moonbit_string_t _M0L4selfS171,
  int32_t _M0L5quoteS172
) {
  struct _M0TPB13StringBuilder* _M0L3bufS170;
  int32_t _M0L6_2atmpS1065;
  struct _M0TPC16string10StringView _M0L6_2atmpS1063;
  struct _M0TPB6Logger _M0L6_2atmpS1064;
  moonbit_string_t _result_1855;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS170 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1065 = Moonbit_array_length(_M0L4selfS171);
  moonbit_incref_cycle_free(_M0L4selfS171);
  _M0L6_2atmpS1063
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS171, .$1 = 0, .$2 = _M0L6_2atmpS1065
  };
  moonbit_incref_cycle_free(_M0L3bufS170);
  _M0L6_2atmpS1064
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS170
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1063, _M0L6_2atmpS1064, _M0L5quoteS172);
  moonbit_decref_cycle_free(_M0L6_2atmpS1063.$0);
  if (_M0L6_2atmpS1064.$1) {
    moonbit_decref(_M0L6_2atmpS1064.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_1855 = _M0MPB13StringBuilder10to__string(_M0L3bufS170);
  moonbit_decref_cycle_free(_M0L3bufS170);
  return _result_1855;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS162,
  struct _M0TPB6Logger _M0L6loggerS160,
  int32_t _M0L5quoteS159
) {
  int32_t _M0L3endS1061;
  int32_t _M0L5startS1062;
  int32_t _M0L3lenS161;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS163;
  int32_t _M0L1iS164;
  int32_t _M0L3segS165;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS159) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS160.$0->$method_3(_M0L6loggerS160.$1, 34);
  }
  _M0L3endS1061 = _M0L4selfS162.$2;
  _M0L5startS1062 = _M0L4selfS162.$1;
  _M0L3lenS161 = _M0L3endS1061 - _M0L5startS1062;
  moonbit_incref_cycle_free(_M0L4selfS162.$0);
  if (_M0L6loggerS160.$1) {
    moonbit_incref(_M0L6loggerS160.$1);
  }
  _M0L6_2aenvS163
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS163)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 37, 0);
  _M0L6_2aenvS163->$0 = _M0L4selfS162;
  _M0L6_2aenvS163->$1 = _M0L6loggerS160;
  _M0L1iS164 = 0;
  _M0L3segS165 = 0;
  _2afor_166:;
  while (1) {
    moonbit_string_t _M0L3strS1058;
    int32_t _M0L5startS1060;
    int32_t _M0L6_2atmpS1059;
    int32_t _M0L4codeS167;
    int32_t _M0L1cS169;
    int32_t _M0L6_2atmpS1042;
    int32_t _M0L6_2atmpS1043;
    int32_t _M0L6_2atmpS1044;
    if (_M0L1iS164 >= _M0L3lenS161) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
      moonbit_decref_cycle_free(_M0L6_2aenvS163);
      break;
    }
    _M0L3strS1058 = _M0L4selfS162.$0;
    _M0L5startS1060 = _M0L4selfS162.$1;
    _M0L6_2atmpS1059 = _M0L5startS1060 + _M0L1iS164;
    _M0L4codeS167 = _M0L3strS1058[_M0L6_2atmpS1059];
    switch (_M0L4codeS167) {
      case 34: {
        _M0L1cS169 = _M0L4codeS167;
        goto join_168;
        break;
      }
      
      case 92: {
        _M0L1cS169 = _M0L4codeS167;
        goto join_168;
        break;
      }
      
      case 10: {
        int32_t _M0L6_2atmpS1045;
        int32_t _M0L6_2atmpS1046;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_18.data);
        _M0L6_2atmpS1045 = _M0L1iS164 + 1;
        _M0L6_2atmpS1046 = _M0L1iS164 + 1;
        _M0L1iS164 = _M0L6_2atmpS1045;
        _M0L3segS165 = _M0L6_2atmpS1046;
        goto _2afor_166;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1047;
        int32_t _M0L6_2atmpS1048;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_19.data);
        _M0L6_2atmpS1047 = _M0L1iS164 + 1;
        _M0L6_2atmpS1048 = _M0L1iS164 + 1;
        _M0L1iS164 = _M0L6_2atmpS1047;
        _M0L3segS165 = _M0L6_2atmpS1048;
        goto _2afor_166;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1049;
        int32_t _M0L6_2atmpS1050;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_20.data);
        _M0L6_2atmpS1049 = _M0L1iS164 + 1;
        _M0L6_2atmpS1050 = _M0L1iS164 + 1;
        _M0L1iS164 = _M0L6_2atmpS1049;
        _M0L3segS165 = _M0L6_2atmpS1050;
        goto _2afor_166;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1051;
        int32_t _M0L6_2atmpS1052;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS1051 = _M0L1iS164 + 1;
        _M0L6_2atmpS1052 = _M0L1iS164 + 1;
        _M0L1iS164 = _M0L6_2atmpS1051;
        _M0L3segS165 = _M0L6_2atmpS1052;
        goto _2afor_166;
        break;
      }
      default: {
        if (_M0L4codeS167 < 32) {
          int32_t _M0L6_2atmpS1054;
          moonbit_string_t _M0L6_2atmpS1053;
          int32_t _M0L6_2atmpS1055;
          int32_t _M0L6_2atmpS1056;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_22.data);
          _M0L6_2atmpS1054 = _M0L4codeS167 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1053 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1054);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, _M0L6_2atmpS1053);
          moonbit_decref_cycle_free(_M0L6_2atmpS1053);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1055 = _M0L1iS164 + 1;
          _M0L6_2atmpS1056 = _M0L1iS164 + 1;
          _M0L1iS164 = _M0L6_2atmpS1055;
          _M0L3segS165 = _M0L6_2atmpS1056;
          goto _2afor_166;
        } else {
          int32_t _M0L6_2atmpS1057 = _M0L1iS164 + 1;
          int32_t _tmp_1858 = _M0L3segS165;
          _M0L1iS164 = _M0L6_2atmpS1057;
          _M0L3segS165 = _tmp_1858;
          goto _2afor_166;
        }
        break;
      }
    }
    goto joinlet_1857;
    join_168:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS160.$0->$method_3(_M0L6loggerS160.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1042 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS169);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS160.$0->$method_3(_M0L6loggerS160.$1, _M0L6_2atmpS1042);
    _M0L6_2atmpS1043 = _M0L1iS164 + 1;
    _M0L6_2atmpS1044 = _M0L1iS164 + 1;
    _M0L1iS164 = _M0L6_2atmpS1043;
    _M0L3segS165 = _M0L6_2atmpS1044;
    continue;
    joinlet_1857:;
    break;
  }
  if (_M0L5quoteS159) {
    #line 202 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS160.$0->$method_3(_M0L6loggerS160.$1, 34);
  }
  return 0;
}

int32_t _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS155,
  int32_t _M0L3segS158,
  int32_t _M0L1iS157
) {
  struct _M0TPB6Logger _M0L6loggerS154;
  struct _M0TPC16string10StringView _M0L4selfS156;
  #line 153 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6loggerS154 = _M0L6_2aenvS155->$1;
  _M0L4selfS156 = _M0L6_2aenvS155->$0;
  if (_M0L1iS157 > _M0L3segS158) {
    int64_t _M0L6_2atmpS1041 = (int64_t)_M0L1iS157;
    struct _M0TPC16string10StringView _M0L6_2atmpS1040;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1040
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS156, _M0L3segS158, _M0L6_2atmpS1041);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS154.$0->$method_2(_M0L6loggerS154.$1, _M0L6_2atmpS1040);
    moonbit_decref_cycle_free(_M0L6_2atmpS1040.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS145,
  int32_t _M0L5startS147,
  int64_t _M0L3endS149
) {
  int32_t _M0L3endS1038;
  int32_t _M0L5startS1039;
  int32_t _M0L3lenS144;
  int32_t _M0Lm2loS146;
  int32_t _M0Lm2hiS148;
  moonbit_string_t _M0L3strS152;
  int32_t _M0L4baseS153;
  int32_t _M0L6_2atmpS1016;
  int32_t _if__result_1859;
  int32_t _M0L6_2atmpS1026;
  int32_t _if__result_1860;
  int32_t _M0L6_2atmpS1028;
  int32_t _M0L6_2atmpS1029;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1038 = _M0L4selfS145.$2;
  _M0L5startS1039 = _M0L4selfS145.$1;
  _M0L3lenS144 = _M0L3endS1038 - _M0L5startS1039;
  if (_M0L5startS147 < 0) {
    _M0Lm2loS146 = 0;
  } else if (_M0L5startS147 > _M0L3lenS144) {
    _M0Lm2loS146 = _M0L3lenS144;
  } else {
    _M0Lm2loS146 = _M0L5startS147;
  }
  if (_M0L3endS149 == 4294967296ll) {
    _M0Lm2hiS148 = _M0L3lenS144;
  } else {
    int64_t _M0L7_2aSomeS150 = _M0L3endS149;
    int32_t _M0L4_2aeS151 = (int32_t)_M0L7_2aSomeS150;
    if (_M0L4_2aeS151 < 0) {
      _M0Lm2hiS148 = 0;
    } else if (_M0L4_2aeS151 > _M0L3lenS144) {
      _M0Lm2hiS148 = _M0L3lenS144;
    } else {
      _M0Lm2hiS148 = _M0L4_2aeS151;
    }
  }
  _M0L3strS152 = _M0L4selfS145.$0;
  _M0L4baseS153 = _M0L4selfS145.$1;
  _M0L6_2atmpS1016 = _M0Lm2loS146;
  if (_M0L6_2atmpS1016 > 0) {
    int32_t _M0L6_2atmpS1015 = _M0Lm2loS146;
    if (_M0L6_2atmpS1015 < _M0L3lenS144) {
      int32_t _M0L6_2atmpS1014 = _M0Lm2loS146;
      int32_t _M0L6_2atmpS1013 = _M0L4baseS153 + _M0L6_2atmpS1014;
      int32_t _M0L6_2atmpS1012 = _M0L3strS152[_M0L6_2atmpS1013];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1012)) {
        int32_t _M0L6_2atmpS1011 = _M0Lm2loS146;
        int32_t _M0L6_2atmpS1010 = _M0L4baseS153 + _M0L6_2atmpS1011;
        int32_t _M0L6_2atmpS1009 = _M0L6_2atmpS1010 - 1;
        int32_t _M0L6_2atmpS1008 = _M0L3strS152[_M0L6_2atmpS1009];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_1859
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1008);
      } else {
        _if__result_1859 = 0;
      }
    } else {
      _if__result_1859 = 0;
    }
  } else {
    _if__result_1859 = 0;
  }
  if (_if__result_1859) {
    int32_t _M0L6_2atmpS1017 = _M0Lm2loS146;
    _M0Lm2loS146 = _M0L6_2atmpS1017 + 1;
  }
  _M0L6_2atmpS1026 = _M0Lm2hiS148;
  if (_M0L6_2atmpS1026 > 0) {
    int32_t _M0L6_2atmpS1025 = _M0Lm2hiS148;
    if (_M0L6_2atmpS1025 < _M0L3lenS144) {
      int32_t _M0L6_2atmpS1024 = _M0Lm2hiS148;
      int32_t _M0L6_2atmpS1023 = _M0L4baseS153 + _M0L6_2atmpS1024;
      int32_t _M0L6_2atmpS1022 = _M0L3strS152[_M0L6_2atmpS1023];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1022)) {
        int32_t _M0L6_2atmpS1021 = _M0Lm2hiS148;
        int32_t _M0L6_2atmpS1020 = _M0L4baseS153 + _M0L6_2atmpS1021;
        int32_t _M0L6_2atmpS1019 = _M0L6_2atmpS1020 - 1;
        int32_t _M0L6_2atmpS1018 = _M0L3strS152[_M0L6_2atmpS1019];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_1860
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1018);
      } else {
        _if__result_1860 = 0;
      }
    } else {
      _if__result_1860 = 0;
    }
  } else {
    _if__result_1860 = 0;
  }
  if (_if__result_1860) {
    int32_t _M0L6_2atmpS1027 = _M0Lm2hiS148;
    _M0Lm2hiS148 = _M0L6_2atmpS1027 - 1;
  }
  _M0L6_2atmpS1028 = _M0Lm2loS146;
  _M0L6_2atmpS1029 = _M0Lm2hiS148;
  if (_M0L6_2atmpS1028 >= _M0L6_2atmpS1029) {
    int32_t _M0L6_2atmpS1033 = _M0Lm2loS146;
    int32_t _M0L6_2atmpS1030 = _M0L4baseS153 + _M0L6_2atmpS1033;
    int32_t _M0L6_2atmpS1032 = _M0Lm2loS146;
    int32_t _M0L6_2atmpS1031 = _M0L4baseS153 + _M0L6_2atmpS1032;
    moonbit_incref_cycle_free(_M0L3strS152);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS152,
                                                 .$1 = _M0L6_2atmpS1030,
                                                 .$2 = _M0L6_2atmpS1031};
  } else {
    int32_t _M0L6_2atmpS1037 = _M0Lm2loS146;
    int32_t _M0L6_2atmpS1034 = _M0L4baseS153 + _M0L6_2atmpS1037;
    int32_t _M0L6_2atmpS1036 = _M0Lm2hiS148;
    int32_t _M0L6_2atmpS1035 = _M0L4baseS153 + _M0L6_2atmpS1036;
    moonbit_incref_cycle_free(_M0L3strS152);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS152,
                                                 .$1 = _M0L6_2atmpS1034,
                                                 .$2 = _M0L6_2atmpS1035};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS143) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS142;
  int32_t _M0L6_2atmpS1005;
  int32_t _M0L6_2atmpS1004;
  int32_t _M0L6_2atmpS1007;
  int32_t _M0L6_2atmpS1006;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1003;
  moonbit_string_t _result_1861;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS142 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1005 = _M0IPC14byte4BytePB3Div3div(_M0L1bS143, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1004
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1005);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS142, _M0L6_2atmpS1004);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1007 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS143, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1006
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1007);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS142, _M0L6_2atmpS1006);
  _M0L6_2atmpS1003 = _M0L7_2aselfS142;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_1861 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1003);
  moonbit_decref_cycle_free(_M0L6_2atmpS1003);
  return _result_1861;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS141) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS141 < 10) {
    int32_t _M0L6_2atmpS1000;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1000 = _M0IPC14byte4BytePB3Add3add(_M0L1iS141, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1000);
  } else {
    int32_t _M0L6_2atmpS1002;
    int32_t _M0L6_2atmpS1001;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1002 = _M0IPC14byte4BytePB3Add3add(_M0L1iS141, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1001 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1002, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1001);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS139,
  int32_t _M0L4thatS140
) {
  int32_t _M0L6_2atmpS998;
  int32_t _M0L6_2atmpS999;
  int32_t _M0L6_2atmpS997;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS998 = (int32_t)_M0L4selfS139;
  _M0L6_2atmpS999 = (int32_t)_M0L4thatS140;
  _M0L6_2atmpS997 = _M0L6_2atmpS998 - _M0L6_2atmpS999;
  return _M0L6_2atmpS997 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS137,
  int32_t _M0L4thatS138
) {
  int32_t _M0L6_2atmpS995;
  int32_t _M0L6_2atmpS996;
  int32_t _M0L6_2atmpS994;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS995 = (int32_t)_M0L4selfS137;
  _M0L6_2atmpS996 = (int32_t)_M0L4thatS138;
  _M0L6_2atmpS994 = _M0L6_2atmpS995 % _M0L6_2atmpS996;
  return _M0L6_2atmpS994 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS135,
  int32_t _M0L4thatS136
) {
  int32_t _M0L6_2atmpS992;
  int32_t _M0L6_2atmpS993;
  int32_t _M0L6_2atmpS991;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS992 = (int32_t)_M0L4selfS135;
  _M0L6_2atmpS993 = (int32_t)_M0L4thatS136;
  _M0L6_2atmpS991 = _M0L6_2atmpS992 / _M0L6_2atmpS993;
  return _M0L6_2atmpS991 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS133,
  int32_t _M0L4thatS134
) {
  int32_t _M0L6_2atmpS989;
  int32_t _M0L6_2atmpS990;
  int32_t _M0L6_2atmpS988;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS989 = (int32_t)_M0L4selfS133;
  _M0L6_2atmpS990 = (int32_t)_M0L4thatS134;
  _M0L6_2atmpS988 = _M0L6_2atmpS989 + _M0L6_2atmpS990;
  return _M0L6_2atmpS988 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS132) {
  int32_t _M0L6_2atmpS987;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS987 = (int32_t)_M0L4selfS132;
  return _M0L6_2atmpS987;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS131) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS131 >= 56320 && _M0L4selfS131 <= 57343;
}

int32_t _M0MPC16uint166UInt1622is__leading__surrogate(int32_t _M0L4selfS130) {
  #line 28 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS130 >= 55296 && _M0L4selfS130 <= 56319;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS129,
  moonbit_string_t _M0L3strS127
) {
  int32_t _M0L8str__lenS126;
  int32_t _M0L3lenS986;
  int32_t _M0L8requiredS128;
  uint16_t* _M0L4dataS981;
  int32_t _M0L6_2atmpS980;
  int32_t _if__result_1862;
  uint16_t* _M0L4dataS982;
  int32_t _M0L3lenS983;
  int32_t _M0L3lenS985;
  int32_t _M0L6_2atmpS984;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS126 = Moonbit_array_length(_M0L3strS127);
  if (_M0L8str__lenS126 == 0) {
    return 0;
  }
  _M0L3lenS986 = _M0L4selfS129->$1;
  _M0L8requiredS128 = _M0L3lenS986 + _M0L8str__lenS126;
  _M0L4dataS981 = _M0L4selfS129->$0;
  _M0L6_2atmpS980 = Moonbit_array_length(_M0L4dataS981);
  if (_M0L8requiredS128 > _M0L6_2atmpS980) {
    _if__result_1862 = 1;
  } else {
    int32_t _M0L3lenS979 = _M0L4selfS129->$1;
    _if__result_1862 = _M0L8requiredS128 < _M0L3lenS979;
  }
  if (_if__result_1862) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS129, _M0L8requiredS128);
  }
  _M0L4dataS982 = _M0L4selfS129->$0;
  _M0L3lenS983 = _M0L4selfS129->$1;
  moonbit_incref_cycle_free(_M0L4dataS982);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS982, _M0L3lenS983, _M0L3strS127, 0, _M0L8str__lenS126);
  moonbit_decref_cycle_free(_M0L4dataS982);
  _M0L3lenS985 = _M0L4selfS129->$1;
  _M0L6_2atmpS984 = _M0L3lenS985 + _M0L8str__lenS126;
  _M0L4selfS129->$1 = _M0L6_2atmpS984;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS122,
  int32_t _M0L11dst__offsetS125,
  moonbit_string_t _M0L3strS123,
  int32_t _M0L11str__offsetS118,
  int32_t _M0L3lenS119
) {
  int32_t _M0L16end__str__offsetS117;
  int32_t _M0L1iS120;
  int32_t _M0L1jS121;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS117 = _M0L11str__offsetS118 + _M0L3lenS119;
  _M0L1iS120 = _M0L11str__offsetS118;
  _M0L1jS121 = _M0L11dst__offsetS125;
  while (1) {
    if (_M0L1iS120 < _M0L16end__str__offsetS117) {
      int32_t _M0L6_2atmpS976 = _M0L3strS123[_M0L1iS120];
      int32_t _M0L6_2atmpS977;
      int32_t _M0L6_2atmpS978;
      _M0L4selfS122[_M0L1jS121] = _M0L6_2atmpS976;
      _M0L6_2atmpS977 = _M0L1iS120 + 1;
      _M0L6_2atmpS978 = _M0L1jS121 + 1;
      _M0L1iS120 = _M0L6_2atmpS977;
      _M0L1jS121 = _M0L6_2atmpS978;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS115,
  int32_t _M0L2chS114
) {
  uint32_t _M0L4codeS113;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS113 = _M0MPC14char4Char8to__uint(_M0L2chS114);
  if (_M0L4codeS113 <= 65535u) {
    int32_t _M0L3lenS947 = _M0L4selfS115->$1;
    uint16_t* _M0L4dataS949 = _M0L4selfS115->$0;
    int32_t _M0L6_2atmpS948 = Moonbit_array_length(_M0L4dataS949);
    uint16_t* _M0L4dataS952;
    int32_t _M0L3lenS953;
    int32_t _M0L6_2atmpS954;
    int32_t _M0L3lenS956;
    int32_t _M0L6_2atmpS955;
    if (_M0L3lenS947 >= _M0L6_2atmpS948) {
      int32_t _M0L3lenS951 = _M0L4selfS115->$1;
      int32_t _M0L6_2atmpS950 = _M0L3lenS951 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS115, _M0L6_2atmpS950);
    }
    _M0L4dataS952 = _M0L4selfS115->$0;
    _M0L3lenS953 = _M0L4selfS115->$1;
    moonbit_incref_cycle_free(_M0L4dataS952);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS954 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS113);
    if (
      _M0L3lenS953 < 0 || _M0L3lenS953 >= Moonbit_array_length(_M0L4dataS952)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS952[_M0L3lenS953] = _M0L6_2atmpS954;
    moonbit_decref_cycle_free(_M0L4dataS952);
    _M0L3lenS956 = _M0L4selfS115->$1;
    _M0L6_2atmpS955 = _M0L3lenS956 + 1;
    _M0L4selfS115->$1 = _M0L6_2atmpS955;
  } else if (_M0L4codeS113 <= 1114111u) {
    uint16_t* _M0L4dataS960 = _M0L4selfS115->$0;
    int32_t _M0L6_2atmpS958 = Moonbit_array_length(_M0L4dataS960);
    int32_t _M0L3lenS959 = _M0L4selfS115->$1;
    int32_t _M0L6_2atmpS957 = _M0L6_2atmpS958 - _M0L3lenS959;
    uint32_t _M0L4codeS116;
    uint16_t* _M0L4dataS963;
    int32_t _M0L3lenS964;
    uint32_t _M0L6_2atmpS967;
    uint32_t _M0L6_2atmpS966;
    int32_t _M0L6_2atmpS965;
    uint16_t* _M0L4dataS968;
    int32_t _M0L3lenS973;
    int32_t _M0L6_2atmpS969;
    uint32_t _M0L6_2atmpS972;
    uint32_t _M0L6_2atmpS971;
    int32_t _M0L6_2atmpS970;
    int32_t _M0L3lenS975;
    int32_t _M0L6_2atmpS974;
    if (_M0L6_2atmpS957 < 2) {
      int32_t _M0L3lenS962 = _M0L4selfS115->$1;
      int32_t _M0L6_2atmpS961 = _M0L3lenS962 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS115, _M0L6_2atmpS961);
    }
    _M0L4codeS116 = _M0L4codeS113 - 65536u;
    _M0L4dataS963 = _M0L4selfS115->$0;
    _M0L3lenS964 = _M0L4selfS115->$1;
    _M0L6_2atmpS967 = _M0L4codeS116 >> 10;
    _M0L6_2atmpS966 = 55296u + _M0L6_2atmpS967;
    moonbit_incref_cycle_free(_M0L4dataS963);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS965 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS966);
    if (
      _M0L3lenS964 < 0 || _M0L3lenS964 >= Moonbit_array_length(_M0L4dataS963)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS963[_M0L3lenS964] = _M0L6_2atmpS965;
    moonbit_decref_cycle_free(_M0L4dataS963);
    _M0L4dataS968 = _M0L4selfS115->$0;
    _M0L3lenS973 = _M0L4selfS115->$1;
    _M0L6_2atmpS969 = _M0L3lenS973 + 1;
    _M0L6_2atmpS972 = _M0L4codeS116 & 1023u;
    _M0L6_2atmpS971 = 56320u + _M0L6_2atmpS972;
    moonbit_incref_cycle_free(_M0L4dataS968);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS970 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS971);
    if (
      _M0L6_2atmpS969 < 0
      || _M0L6_2atmpS969 >= Moonbit_array_length(_M0L4dataS968)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS968[_M0L6_2atmpS969] = _M0L6_2atmpS970;
    moonbit_decref_cycle_free(_M0L4dataS968);
    _M0L3lenS975 = _M0L4selfS115->$1;
    _M0L6_2atmpS974 = _M0L3lenS975 + 2;
    _M0L4selfS115->$1 = _M0L6_2atmpS974;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_23.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L8requiredS111
) {
  uint16_t* _M0L4dataS946;
  int32_t _M0L6_2atmpS944;
  int32_t _M0L3lenS945;
  int32_t _M0L13new__capacityS109;
  uint16_t* _M0L4dataS941;
  int32_t _M0L6_2atmpS942;
  int32_t _M0L3lenS943;
  uint16_t* _M0L9new__dataS112;
  uint16_t* _M0L6_2aoldS1764;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS946 = _M0L4selfS110->$0;
  _M0L6_2atmpS944 = Moonbit_array_length(_M0L4dataS946);
  _M0L3lenS945 = _M0L4selfS110->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS109
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS944, _M0L3lenS945, _M0L8requiredS111);
  _M0L4dataS941 = _M0L4selfS110->$0;
  moonbit_incref_cycle_free(_M0L4dataS941);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS942 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS943 = _M0L4selfS110->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS112
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS941, _M0L13new__capacityS109, _M0L6_2atmpS942, _M0L3lenS943, 0, 0);
  _M0L6_2aoldS1764 = _M0L4selfS110->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1764);
  _M0L4selfS110->$0 = _M0L9new__dataS112;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS108,
  int32_t _M0L3lenS104,
  int32_t _M0L8requiredS103
) {
  int32_t _M0L5spaceS105;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS103 < _M0L3lenS104) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_24.data);
  }
  _M0L5spaceS105 = _M0L7currentS108;
  while (1) {
    if (_M0L5spaceS105 < _M0L8requiredS103) {
      int32_t _M0L4nextS106 = _M0L5spaceS105 * 2;
      if (_M0L4nextS106 <= _M0L5spaceS105) {
        return _M0L8requiredS103;
      }
      _M0L5spaceS105 = _M0L4nextS106;
      continue;
    } else {
      return _M0L5spaceS105;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS102) {
  int32_t _M0L6_2atmpS940;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS940 = *(int32_t*)&_M0L4selfS102;
  return (uint16_t)_M0L6_2atmpS940;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS101) {
  int32_t _M0L6_2atmpS939;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS939 = _M0L4selfS101;
  return *(uint32_t*)&_M0L6_2atmpS939;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS99
) {
  int32_t _M0L3lenS930;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS930 = _M0L4selfS99->$1;
  if (_M0L3lenS930 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS931 = _M0L4selfS99->$1;
    uint16_t* _M0L4dataS933 = _M0L4selfS99->$0;
    int32_t _M0L6_2atmpS932 = Moonbit_array_length(_M0L4dataS933);
    if (_M0L3lenS931 == _M0L6_2atmpS932) {
      uint16_t* _M0L4dataS934 = _M0L4selfS99->$0;
      moonbit_incref_cycle_free(_M0L4dataS934);
      return _M0L4dataS934;
    } else {
      uint16_t* _M0L4dataS935 = _M0L4selfS99->$0;
      int32_t _M0L3lenS936 = _M0L4selfS99->$1;
      int32_t _M0L6_2atmpS937;
      int32_t _M0L3lenS938;
      uint16_t* _M0L4dataS100;
      moonbit_incref_cycle_free(_M0L4dataS935);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS937 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS938 = _M0L4selfS99->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS100
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS935, _M0L3lenS936, _M0L6_2atmpS937, _M0L3lenS938, 0, 0);
      return _M0L4dataS100;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS96,
  int32_t _M0L13allocate__lenS92,
  int32_t _M0L4initS97,
  int32_t _M0L3lenS93,
  int32_t _M0L11src__offsetS94,
  int32_t _M0L11dst__offsetS95
) {
  int32_t _if__result_1865;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS92 >= 0) {
    if (_M0L3lenS93 >= 0) {
      if (_M0L11src__offsetS94 >= 0) {
        if (_M0L11dst__offsetS95 >= 0) {
          int32_t _M0L6_2atmpS926 = _M0L11src__offsetS94 + _M0L3lenS93;
          int32_t _M0L6_2atmpS927 = Moonbit_array_length(_M0L3srcS96);
          if (_M0L6_2atmpS926 <= _M0L6_2atmpS927) {
            int32_t _M0L6_2atmpS925 = _M0L11dst__offsetS95 + _M0L3lenS93;
            _if__result_1865 = _M0L6_2atmpS925 <= _M0L13allocate__lenS92;
          } else {
            _if__result_1865 = 0;
          }
        } else {
          _if__result_1865 = 0;
        }
      } else {
        _if__result_1865 = 0;
      }
    } else {
      _if__result_1865 = 0;
    }
  } else {
    _if__result_1865 = 0;
  }
  if (_if__result_1865) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS96, _M0L13allocate__lenS92, _M0L4initS97, _M0L11src__offsetS94, _M0L11dst__offsetS95, _M0L3lenS93);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS98;
    int32_t _M0L6_2atmpS929;
    moonbit_string_t _M0L6_2atmpS928;
    uint16_t* _result_1866;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS98
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS98, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS98, _M0L13allocate__lenS92);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS98, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS98, _M0L11src__offsetS94);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS98, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS98, _M0L11dst__offsetS95);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS98, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS98, _M0L3lenS93);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS98, (moonbit_string_t)moonbit_string_literal_29.data);
    _M0L6_2atmpS929 = Moonbit_array_length(_M0L3srcS96);
    moonbit_decref_cycle_free(_M0L3srcS96);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS98, _M0L6_2atmpS929);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS928
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS98);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS98);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_1866 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS928);
    moonbit_decref_cycle_free(_M0L6_2atmpS928);
    return _result_1866;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS89,
  int32_t _M0L13allocate__lenS86,
  int32_t _M0L4initS87,
  int32_t _M0L11src__offsetS90,
  int32_t _M0L11dst__offsetS88,
  int32_t _M0L9blit__lenS91
) {
  uint16_t* _M0L3dstS85;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS85
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS86, _M0L4initS87);
  moonbit_incref_cycle_free(_M0L3dstS85);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS85, _M0L11dst__offsetS88, _M0L3srcS89, _M0L11src__offsetS90, _M0L9blit__lenS91, sizeof(uint16_t));
  return _M0L3dstS85;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS83
) {
  int32_t _M0L7initialS82;
  uint16_t* _M0L4dataS84;
  struct _M0TPB13StringBuilder* _block_1867;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS83 < 1) {
    _M0L7initialS82 = 1;
  } else {
    int32_t _M0L6_2atmpS924 = _M0L10size__hintS83 + 1;
    _M0L7initialS82 = _M0L6_2atmpS924 / 2;
  }
  _M0L4dataS84 = (uint16_t*)moonbit_make_string(_M0L7initialS82, 0);
  _block_1867
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_1867)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _block_1867->$0 = _M0L4dataS84;
  _block_1867->$1 = 0;
  return _block_1867;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS81) {
  int32_t _M0L6_2atmpS923;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS923 = (int32_t)_M0L4selfS81;
  return _M0L6_2atmpS923;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS73,
  int32_t _M0L13allocate__lenS69,
  int32_t _M0L3lenS70,
  int32_t _M0L11src__offsetS71,
  int32_t _M0L11dst__offsetS72
) {
  int32_t _if__result_1868;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS69 >= 0) {
    if (_M0L3lenS70 >= 0) {
      if (_M0L11src__offsetS71 >= 0) {
        if (_M0L11dst__offsetS72 >= 0) {
          int32_t _M0L6_2atmpS914 = _M0L11src__offsetS71 + _M0L3lenS70;
          int32_t _M0L6_2atmpS915;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS915 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS73);
          if (_M0L6_2atmpS914 <= _M0L6_2atmpS915) {
            int32_t _M0L6_2atmpS913 = _M0L11dst__offsetS72 + _M0L3lenS70;
            _if__result_1868 = _M0L6_2atmpS913 <= _M0L13allocate__lenS69;
          } else {
            _if__result_1868 = 0;
          }
        } else {
          _if__result_1868 = 0;
        }
      } else {
        _if__result_1868 = 0;
      }
    } else {
      _if__result_1868 = 0;
    }
  } else {
    _if__result_1868 = 0;
  }
  if (_if__result_1868) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS69, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS73, _M0L11src__offsetS71, _M0L11dst__offsetS72, _M0L3lenS70);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS74;
    int32_t _M0L6_2atmpS917;
    moonbit_string_t _M0L6_2atmpS916;
    moonbit_string_t* _result_1869;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS74
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS74, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS74, _M0L13allocate__lenS69);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS74, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS74, _M0L11src__offsetS71);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS74, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS74, _M0L11dst__offsetS72);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS74, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS74, _M0L3lenS70);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS74, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS917 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS73);
    moonbit_decref_cycle_free(_M0L3srcS73);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS74, _M0L6_2atmpS917);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS916
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS74);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS74);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_1869
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS916);
    moonbit_decref_cycle_free(_M0L6_2atmpS916);
    return _result_1869;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS79,
  int32_t _M0L13allocate__lenS75,
  int32_t _M0L3lenS76,
  int32_t _M0L11src__offsetS77,
  int32_t _M0L11dst__offsetS78
) {
  int32_t _if__result_1870;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS75 >= 0) {
    if (_M0L3lenS76 >= 0) {
      if (_M0L11src__offsetS77 >= 0) {
        if (_M0L11dst__offsetS78 >= 0) {
          int32_t _M0L6_2atmpS919 = _M0L11src__offsetS77 + _M0L3lenS76;
          int32_t _M0L6_2atmpS920;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS920
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS79);
          if (_M0L6_2atmpS919 <= _M0L6_2atmpS920) {
            int32_t _M0L6_2atmpS918 = _M0L11dst__offsetS78 + _M0L3lenS76;
            _if__result_1870 = _M0L6_2atmpS918 <= _M0L13allocate__lenS75;
          } else {
            _if__result_1870 = 0;
          }
        } else {
          _if__result_1870 = 0;
        }
      } else {
        _if__result_1870 = 0;
      }
    } else {
      _if__result_1870 = 0;
    }
  } else {
    _if__result_1870 = 0;
  }
  if (_if__result_1870) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS75, 0, _M0L3srcS79, _M0L11src__offsetS77, _M0L11dst__offsetS78, _M0L3lenS76);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS80;
    int32_t _M0L6_2atmpS922;
    moonbit_string_t _M0L6_2atmpS921;
    struct _M0TUsiE** _result_1871;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS80
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS80, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS80, _M0L13allocate__lenS75);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS80, (moonbit_string_t)moonbit_string_literal_26.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS80, _M0L11src__offsetS77);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS80, (moonbit_string_t)moonbit_string_literal_27.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS80, _M0L11dst__offsetS78);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS80, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS80, _M0L3lenS76);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS80, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS922 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS79);
    moonbit_decref_cycle_free(_M0L3srcS79);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS80, _M0L6_2atmpS922);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS921
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS80);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS80);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_1871
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS921);
    moonbit_decref_cycle_free(_M0L6_2atmpS921);
    return _result_1871;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS64,
  moonbit_string_t _M0L3objS63
) {
  struct _M0TPB6Logger _M0L6_2atmpS910;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS64);
  _M0L6_2atmpS910
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS64
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS63, _M0L6_2atmpS910);
  if (_M0L6_2atmpS910.$1) {
    moonbit_decref(_M0L6_2atmpS910.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS66,
  int32_t _M0L3objS65
) {
  struct _M0TPB6Logger _M0L6_2atmpS911;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS66);
  _M0L6_2atmpS911
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS66
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS65, _M0L6_2atmpS911);
  if (_M0L6_2atmpS911.$1) {
    moonbit_decref(_M0L6_2atmpS911.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS68,
  uint64_t _M0L3objS67
) {
  struct _M0TPB6Logger _M0L6_2atmpS912;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS68);
  _M0L6_2atmpS912
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS68
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS67, _M0L6_2atmpS912);
  if (_M0L6_2atmpS912.$1) {
    moonbit_decref(_M0L6_2atmpS912.$1);
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
        int32_t _M0L6_2atmpS883 = _M0L11dst__offsetS16 + _M0L1iS18;
        int32_t _M0L6_2atmpS885 = _M0L11src__offsetS17 + _M0L1iS18;
        int32_t _M0L6_2atmpS884;
        int32_t _M0L6_2atmpS886;
        if (
          _M0L6_2atmpS885 < 0
          || _M0L6_2atmpS885 >= Moonbit_array_length(_M0L3srcS15)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS884 = (int32_t)_M0L3srcS15[_M0L6_2atmpS885];
        if (
          _M0L6_2atmpS883 < 0
          || _M0L6_2atmpS883 >= Moonbit_array_length(_M0L3dstS14)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS14[_M0L6_2atmpS883] = _M0L6_2atmpS884;
        _M0L6_2atmpS886 = _M0L1iS18 + 1;
        _M0L1iS18 = _M0L6_2atmpS886;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS15);
        moonbit_decref_cycle_free(_M0L3dstS14);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS891 = _M0L3lenS19 - 1;
    int32_t _M0L1iS21 = _M0L6_2atmpS891;
    while (1) {
      if (_M0L1iS21 >= 0) {
        int32_t _M0L6_2atmpS887 = _M0L11dst__offsetS16 + _M0L1iS21;
        int32_t _M0L6_2atmpS889 = _M0L11src__offsetS17 + _M0L1iS21;
        int32_t _M0L6_2atmpS888;
        int32_t _M0L6_2atmpS890;
        if (
          _M0L6_2atmpS889 < 0
          || _M0L6_2atmpS889 >= Moonbit_array_length(_M0L3srcS15)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS888 = (int32_t)_M0L3srcS15[_M0L6_2atmpS889];
        if (
          _M0L6_2atmpS887 < 0
          || _M0L6_2atmpS887 >= Moonbit_array_length(_M0L3dstS14)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS14[_M0L6_2atmpS887] = _M0L6_2atmpS888;
        _M0L6_2atmpS890 = _M0L1iS21 - 1;
        _M0L1iS21 = _M0L6_2atmpS890;
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
        int32_t _M0L6_2atmpS892 = _M0L11dst__offsetS25 + _M0L1iS27;
        int32_t _M0L6_2atmpS894 = _M0L11src__offsetS26 + _M0L1iS27;
        moonbit_string_t _M0L6_2atmpS893;
        moonbit_string_t _M0L6_2aoldS1765;
        int32_t _M0L6_2atmpS895;
        if (
          _M0L6_2atmpS894 < 0
          || _M0L6_2atmpS894 >= Moonbit_array_length(_M0L3srcS24)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS893 = (moonbit_string_t)_M0L3srcS24[_M0L6_2atmpS894];
        if (
          _M0L6_2atmpS892 < 0
          || _M0L6_2atmpS892 >= Moonbit_array_length(_M0L3dstS23)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1765 = (moonbit_string_t)_M0L3dstS23[_M0L6_2atmpS892];
        moonbit_incref_cycle_free(_M0L6_2atmpS893);
        moonbit_decref_cycle_free(_M0L6_2aoldS1765);
        _M0L3dstS23[_M0L6_2atmpS892] = _M0L6_2atmpS893;
        _M0L6_2atmpS895 = _M0L1iS27 + 1;
        _M0L1iS27 = _M0L6_2atmpS895;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS24);
        moonbit_decref_cycle_free(_M0L3dstS23);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS900 = _M0L3lenS28 - 1;
    int32_t _M0L1iS30 = _M0L6_2atmpS900;
    while (1) {
      if (_M0L1iS30 >= 0) {
        int32_t _M0L6_2atmpS896 = _M0L11dst__offsetS25 + _M0L1iS30;
        int32_t _M0L6_2atmpS898 = _M0L11src__offsetS26 + _M0L1iS30;
        moonbit_string_t _M0L6_2atmpS897;
        moonbit_string_t _M0L6_2aoldS1766;
        int32_t _M0L6_2atmpS899;
        if (
          _M0L6_2atmpS898 < 0
          || _M0L6_2atmpS898 >= Moonbit_array_length(_M0L3srcS24)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS897 = (moonbit_string_t)_M0L3srcS24[_M0L6_2atmpS898];
        if (
          _M0L6_2atmpS896 < 0
          || _M0L6_2atmpS896 >= Moonbit_array_length(_M0L3dstS23)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1766 = (moonbit_string_t)_M0L3dstS23[_M0L6_2atmpS896];
        moonbit_incref_cycle_free(_M0L6_2atmpS897);
        moonbit_decref_cycle_free(_M0L6_2aoldS1766);
        _M0L3dstS23[_M0L6_2atmpS896] = _M0L6_2atmpS897;
        _M0L6_2atmpS899 = _M0L1iS30 - 1;
        _M0L1iS30 = _M0L6_2atmpS899;
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
        int32_t _M0L6_2atmpS901 = _M0L11dst__offsetS34 + _M0L1iS36;
        int32_t _M0L6_2atmpS903 = _M0L11src__offsetS35 + _M0L1iS36;
        struct _M0TUsiE* _M0L6_2atmpS902;
        struct _M0TUsiE* _M0L6_2aoldS1767;
        int32_t _M0L6_2atmpS904;
        if (
          _M0L6_2atmpS903 < 0
          || _M0L6_2atmpS903 >= Moonbit_array_length(_M0L3srcS33)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS902 = (struct _M0TUsiE*)_M0L3srcS33[_M0L6_2atmpS903];
        if (
          _M0L6_2atmpS901 < 0
          || _M0L6_2atmpS901 >= Moonbit_array_length(_M0L3dstS32)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1767 = (struct _M0TUsiE*)_M0L3dstS32[_M0L6_2atmpS901];
        if (_M0L6_2atmpS902) {
          moonbit_incref_cycle_free(_M0L6_2atmpS902);
        }
        if (_M0L6_2aoldS1767) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1767);
        }
        _M0L3dstS32[_M0L6_2atmpS901] = _M0L6_2atmpS902;
        _M0L6_2atmpS904 = _M0L1iS36 + 1;
        _M0L1iS36 = _M0L6_2atmpS904;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS33);
        moonbit_decref_cycle_free(_M0L3dstS32);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS909 = _M0L3lenS37 - 1;
    int32_t _M0L1iS39 = _M0L6_2atmpS909;
    while (1) {
      if (_M0L1iS39 >= 0) {
        int32_t _M0L6_2atmpS905 = _M0L11dst__offsetS34 + _M0L1iS39;
        int32_t _M0L6_2atmpS907 = _M0L11src__offsetS35 + _M0L1iS39;
        struct _M0TUsiE* _M0L6_2atmpS906;
        struct _M0TUsiE* _M0L6_2aoldS1768;
        int32_t _M0L6_2atmpS908;
        if (
          _M0L6_2atmpS907 < 0
          || _M0L6_2atmpS907 >= Moonbit_array_length(_M0L3srcS33)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS906 = (struct _M0TUsiE*)_M0L3srcS33[_M0L6_2atmpS907];
        if (
          _M0L6_2atmpS905 < 0
          || _M0L6_2atmpS905 >= Moonbit_array_length(_M0L3dstS32)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1768 = (struct _M0TUsiE*)_M0L3dstS32[_M0L6_2atmpS905];
        if (_M0L6_2atmpS906) {
          moonbit_incref_cycle_free(_M0L6_2atmpS906);
        }
        if (_M0L6_2aoldS1768) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1768);
        }
        _M0L3dstS32[_M0L6_2atmpS905] = _M0L6_2atmpS906;
        _M0L6_2atmpS908 = _M0L1iS39 - 1;
        _M0L1iS39 = _M0L6_2atmpS908;
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS855) {
  switch (Moonbit_object_tag(_M0L4_2aeS855)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_32.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS855);
      break;
    }
    
    case 4: {
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
  void* _M0L11_2aobj__ptrS878,
  struct _M0TPB4Show _M0L8_2aparamS877
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS876 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS878;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS876, _M0L8_2aparamS877);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS875,
  struct _M0TPB4Show _M0L8_2aparamS874
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS873 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS875;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS873, _M0L8_2aparamS874);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS872,
  int32_t _M0L8_2aparamS871
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS870 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS872;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS870, _M0L8_2aparamS871);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS869,
  struct _M0TPC16string10StringView _M0L8_2aparamS868
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS867 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS869;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS867, _M0L8_2aparamS868);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS866,
  moonbit_string_t _M0L8_2aparamS863,
  int32_t _M0L8_2aparamS864,
  int32_t _M0L8_2aparamS865
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS862 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS866;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS862, _M0L8_2aparamS863, _M0L8_2aparamS864, _M0L8_2aparamS865);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS861,
  moonbit_string_t _M0L8_2aparamS860
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS859 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS861;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS859, _M0L8_2aparamS860);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS882;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS848;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS849;
  int32_t _M0L7_2abindS850;
  struct _M0TUsiE** _M0L7_2abindS851;
  int32_t _M0L6_2acntS1773;
  int32_t _M0L2__S852;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS882
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS848
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS848)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 45, 0);
  _M0L12async__testsS848->$0 = _M0L6_2atmpS882;
  _M0L12async__testsS848->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS849
  = _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS850 = _M0L7_2abindS849->$1;
  _M0L7_2abindS851 = _M0L7_2abindS849->$0;
  _M0L6_2acntS1773
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS849));
  if (_M0L6_2acntS1773 > 1) {
    int32_t _M0L11_2anew__cntS1774 = _M0L6_2acntS1773 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS849), _M0L11_2anew__cntS1774);
    moonbit_incref_cycle_free(_M0L7_2abindS851);
  } else if (_M0L6_2acntS1773 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS849);
  }
  _M0L2__S852 = 0;
  while (1) {
    if (_M0L2__S852 < _M0L7_2abindS850) {
      struct _M0TUsiE* _M0L3argS853 =
        (struct _M0TUsiE*)_M0L7_2abindS851[_M0L2__S852];
      moonbit_string_t _M0L6_2atmpS879 = _M0L3argS853->$0;
      int32_t _M0L6_2atmpS880 = _M0L3argS853->$1;
      int32_t _M0L6_2atmpS881;
      moonbit_incref_cycle_free(_M0L6_2atmpS879);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS848, _M0L6_2atmpS879, _M0L6_2atmpS880);
      moonbit_decref_cycle_free(_M0L6_2atmpS879);
      _M0L6_2atmpS881 = _M0L2__S852 + 1;
      _M0L2__S852 = _M0L6_2atmpS881;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS851);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\poisson_pop\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples28poisson__pop__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS848);
  moonbit_decref_cycle_free(_M0L12async__testsS848);
  moonbit_flush_cycles();
  return 0;
}