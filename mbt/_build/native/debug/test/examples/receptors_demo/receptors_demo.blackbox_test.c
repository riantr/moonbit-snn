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

struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TWRPC15error5ErrorEs;

struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TPB4Show;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TP26RiantR8snn__mbt9GABAergic;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TP26RiantR8snn__mbt8Receptor;

struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c884;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TP26RiantR8snn__mbt9Receptors;

struct _M0TPB5ArrayGUsiEE;

struct _M0TPB5ArrayGsE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c889;

struct _M0TWEu;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0TP26RiantR8snn__mbt13Glutamatergic;

struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

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

struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget {
  struct _M0TPB5ArrayGiE* $0;
  struct _M0TPB5ArrayGiE* $1;
  
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

struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
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

struct _M0TP26RiantR8snn__mbt9GABAergic {
  struct _M0TP26RiantR8snn__mbt8Receptor* $0;
  struct _M0TP26RiantR8snn__mbt8Receptor* $1;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
};

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error {
  struct moonbit_result_0(* code)(
    struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error*,
    struct _M0TWuEu*,
    struct _M0TWRPC15error5ErrorEu*
  );
  
};

struct _M0TP26RiantR8snn__mbt8Receptor {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  float $5;
  float $6;
  float $7;
  int32_t $8;
  moonbit_string_t $9;
  
};

struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c884 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency {
  float $0;
  float $1;
  float $2;
  
};

struct _M0TWuEu {
  int32_t(* code)(struct _M0TWuEu*, int32_t);
  
};

struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE {
  struct _M0TP26RiantR8snn__mbt8Receptor** $0;
  int32_t $1;
  
};

struct _M0KTPB6LoggerTPB13StringBuilder {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0TP26RiantR8snn__mbt9Receptors {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* $0;
  
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

struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c889 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0TP26RiantR8snn__mbt13Glutamatergic {
  struct _M0TP26RiantR8snn__mbt8Receptor* $0;
  struct _M0TP26RiantR8snn__mbt8Receptor* $1;
  
};

struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS896(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS889(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS884(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS861(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S854(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget* _M0FP26RiantR8snn__mbt16infer__receptors(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE*
);

int32_t _M0FP26RiantR8snn__mbt18receptors__current(
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGfE*,
  struct _M0TP26RiantR8snn__mbt9Receptors*,
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency*,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0FP26RiantR8snn__mbt17receptor__current(
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGfE*,
  struct _M0TP26RiantR8snn__mbt8Receptor*,
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency*,
  struct _M0TPB5ArrayGfE*
);

float _M0FP26RiantR8snn__mbt14alpha__synapse(float, float);

float _M0FP26RiantR8snn__mbt12nmda__gating(
  float,
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency*
);

struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _M0MP26RiantR8snn__mbt21NMDAVoltageDependency4eyal(
  
);

struct _M0TP26RiantR8snn__mbt9Receptors* _M0MP26RiantR8snn__mbt9Receptors10from__pair(
  struct _M0TP26RiantR8snn__mbt13Glutamatergic*,
  struct _M0TP26RiantR8snn__mbt9GABAergic*
);

struct _M0TP26RiantR8snn__mbt8Receptor* _M0MP26RiantR8snn__mbt8Receptor4nmda(
  float,
  float,
  float,
  float
);

struct _M0TP26RiantR8snn__mbt13Glutamatergic* _M0MP26RiantR8snn__mbt13Glutamatergic6custom(
  struct _M0TP26RiantR8snn__mbt8Receptor*,
  struct _M0TP26RiantR8snn__mbt8Receptor*
);

struct _M0TP26RiantR8snn__mbt8Receptor* _M0MP26RiantR8snn__mbt8Receptor6simple(
  float,
  float,
  float,
  float,
  moonbit_string_t
);

float _M0FP26RiantR8snn__mbt13norm__synapse(float, float);

struct _M0TP26RiantR8snn__mbt9GABAergic* _M0MP26RiantR8snn__mbt9GABAergic6custom(
  struct _M0TP26RiantR8snn__mbt8Receptor*,
  struct _M0TP26RiantR8snn__mbt8Receptor*
);

#define _M0FP26RiantR8snn__mbt4logf logf

#define _M0FP26RiantR8snn__mbt4expf expf

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

struct _M0TP26RiantR8snn__mbt8Receptor* _M0MPC15array5Array2atGRP26RiantR8snn__mbt8ReceptorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE*,
  int32_t
);

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

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(uint64_t*, int32_t);

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(uint32_t*, int32_t);

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(uint64_t);

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t);

moonbit_string_t _M0IPC14bool4BoolPB4Show10to__string(int32_t);

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

int32_t _M0MPC15array5Array7reallocGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array7reallocGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  int32_t
);

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

int32_t _M0MPC15array5Array8capacityGiE(struct _M0TPB5ArrayGiE*);

int32_t _M0MPC15array5Array8capacityGsE(struct _M0TPB5ArrayGsE*);

int32_t _M0MPC15array5Array8capacityGUsiEE(struct _M0TPB5ArrayGUsiEE*);

int32_t _M0FPB23array__growth__capacity(int32_t, int32_t, int32_t);

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE*);

int32_t _M0MPC15array5Array6lengthGRP26RiantR8snn__mbt8ReceptorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE*
);

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE*);

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

struct _M0TP26RiantR8snn__mbt8Receptor** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt8ReceptorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE*
);

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

float logf(float);

float expf(float);

struct { int32_t rc; uint32_t meta; uint16_t const data[119]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 118, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 114, 101, 99, 101, 112, 116, 111, 
    114, 115, 95, 100, 101, 109, 111, 95, 98, 108, 97, 99, 107, 98, 111, 
    120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 
    84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 
    114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 46, 77, 111, 
    111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 
    114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 
    111, 114, 0
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
} const moonbit_string_literal_14 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[12]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 11, 44, 34, 
    109, 101, 115, 115, 97, 103, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[53]; 
} const moonbit_string_literal_39 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_11 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_32 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[6]; 
} const moonbit_string_literal_18 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 5, 102, 97, 
    108, 115, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[121]; 
} const moonbit_string_literal_37 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 120, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 114, 101, 99, 101, 112, 116, 111, 
    114, 115, 95, 100, 101, 109, 111, 95, 98, 108, 97, 99, 107, 98, 111, 
    120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 
    84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 
    114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 46, 77, 
    111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 
    101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 
    84, 101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_26 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_10 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 103, 97, 
    98, 97, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_17 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 116, 114, 
    117, 101, 0
  };

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

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_9 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 103, 108, 117, 0
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
} const moonbit_string_literal_12 =
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
} const moonbit_string_literal_16 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS896$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS896
  };

uint32_t const moonbit_layout_table_data[63] =
  {
    sizeof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c884)
    / 4, 1,
    offsetof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c884, $1)
    / 4
    * 2,
    sizeof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c889)
    / 4, 1,
    offsetof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c889, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGiE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGiE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget) / 4, 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget, $1) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt9Receptors) / 4, 1,
    offsetof(struct _M0TP26RiantR8snn__mbt9Receptors, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt8Receptor) / 4, 1,
    offsetof(struct _M0TP26RiantR8snn__mbt8Receptor, $9) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt13Glutamatergic) / 4, 2,
    offsetof(struct _M0TP26RiantR8snn__mbt13Glutamatergic, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt13Glutamatergic, $1) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt9GABAergic) / 4, 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9GABAergic, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9GABAergic, $1) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS1885
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS917,
  moonbit_string_t _M0L8filenameS886,
  int32_t _M0L5indexS888
) {
  struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c884* _closure_1914;
  struct _M0TWEu* _M0L13handle__startS884;
  struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c889* _closure_1915;
  struct _M0TWssbEu* _M0L14handle__resultS889;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS896;
  void* _M0L11_2atry__errS911;
  struct moonbit_result_0 _tmp_1917;
  int32_t _handle__error__result_1918;
  int32_t _M0L6_2atmpS1873;
  void* _M0L3errS912;
  moonbit_string_t _M0L4nameS914;
  struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS915;
  moonbit_string_t _M0L7_2anameS916;
  int32_t _M0L6_2acntS1904;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS886);
  _closure_1914
  = (struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c884*)moonbit_malloc(sizeof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c884));
  Moonbit_object_header(_closure_1914)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_1914->code
  = &_M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS884;
  _closure_1914->$0 = _M0L5indexS888;
  _closure_1914->$1 = _M0L8filenameS886;
  _M0L13handle__startS884 = (struct _M0TWEu*)_closure_1914;
  moonbit_incref_cycle_free(_M0L8filenameS886);
  _closure_1915
  = (struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c889*)moonbit_malloc(sizeof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c889));
  Moonbit_object_header(_closure_1915)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_1915->code
  = &_M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS889;
  _closure_1915->$0 = _M0L5indexS888;
  _closure_1915->$1 = _M0L8filenameS886;
  _M0L14handle__resultS889 = (struct _M0TWssbEu*)_closure_1915;
  _M0L17error__to__stringS896
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS896$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _tmp_1917
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS917, _M0L8filenameS886, _M0L5indexS888, _M0L13handle__startS884, _M0L14handle__resultS889, _M0L17error__to__stringS896);
  if (_tmp_1917.tag) {
    int32_t const _M0L5_2aokS1882 = _tmp_1917.data.ok;
    _handle__error__result_1918 = _M0L5_2aokS1882;
  } else {
    void* const _M0L6_2aerrS1883 = _tmp_1917.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS896);
    moonbit_decref_cycle_free(_M0L13handle__startS884);
    _M0L11_2atry__errS911 = _M0L6_2aerrS1883;
    goto join_910;
  }
  if (_handle__error__result_1918) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS896);
    moonbit_decref_cycle_free(_M0L13handle__startS884);
    _M0L6_2atmpS1873 = 1;
  } else {
    struct moonbit_result_0 _tmp_1919;
    int32_t _handle__error__result_1920;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
    _tmp_1919
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS917, _M0L8filenameS886, _M0L5indexS888, _M0L13handle__startS884, _M0L14handle__resultS889, _M0L17error__to__stringS896);
    if (_tmp_1919.tag) {
      int32_t const _M0L5_2aokS1880 = _tmp_1919.data.ok;
      _handle__error__result_1920 = _M0L5_2aokS1880;
    } else {
      void* const _M0L6_2aerrS1881 = _tmp_1919.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS896);
      moonbit_decref_cycle_free(_M0L13handle__startS884);
      _M0L11_2atry__errS911 = _M0L6_2aerrS1881;
      goto join_910;
    }
    if (_handle__error__result_1920) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS896);
      moonbit_decref_cycle_free(_M0L13handle__startS884);
      _M0L6_2atmpS1873 = 1;
    } else {
      struct moonbit_result_0 _tmp_1921;
      int32_t _handle__error__result_1922;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
      _tmp_1921
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS917, _M0L8filenameS886, _M0L5indexS888, _M0L13handle__startS884, _M0L14handle__resultS889, _M0L17error__to__stringS896);
      if (_tmp_1921.tag) {
        int32_t const _M0L5_2aokS1878 = _tmp_1921.data.ok;
        _handle__error__result_1922 = _M0L5_2aokS1878;
      } else {
        void* const _M0L6_2aerrS1879 = _tmp_1921.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS896);
        moonbit_decref_cycle_free(_M0L13handle__startS884);
        _M0L11_2atry__errS911 = _M0L6_2aerrS1879;
        goto join_910;
      }
      if (_handle__error__result_1922) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS896);
        moonbit_decref_cycle_free(_M0L13handle__startS884);
        _M0L6_2atmpS1873 = 1;
      } else {
        struct moonbit_result_0 _tmp_1923;
        int32_t _handle__error__result_1924;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
        _tmp_1923
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS917, _M0L8filenameS886, _M0L5indexS888, _M0L13handle__startS884, _M0L14handle__resultS889, _M0L17error__to__stringS896);
        if (_tmp_1923.tag) {
          int32_t const _M0L5_2aokS1876 = _tmp_1923.data.ok;
          _handle__error__result_1924 = _M0L5_2aokS1876;
        } else {
          void* const _M0L6_2aerrS1877 = _tmp_1923.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS896);
          moonbit_decref_cycle_free(_M0L13handle__startS884);
          _M0L11_2atry__errS911 = _M0L6_2aerrS1877;
          goto join_910;
        }
        if (_handle__error__result_1924) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS896);
          moonbit_decref_cycle_free(_M0L13handle__startS884);
          _M0L6_2atmpS1873 = 1;
        } else {
          struct moonbit_result_0 _tmp_1925;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
          _tmp_1925
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS917, _M0L8filenameS886, _M0L5indexS888, _M0L13handle__startS884, _M0L14handle__resultS889, _M0L17error__to__stringS896);
          moonbit_decref_cycle_free(_M0L13handle__startS884);
          moonbit_decref_cycle_free(_M0L17error__to__stringS896);
          if (_tmp_1925.tag) {
            int32_t const _M0L5_2aokS1874 = _tmp_1925.data.ok;
            _M0L6_2atmpS1873 = _M0L5_2aokS1874;
          } else {
            void* const _M0L6_2aerrS1875 = _tmp_1925.data.err;
            _M0L11_2atry__errS911 = _M0L6_2aerrS1875;
            goto join_910;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS1873) {
    void* _M0L134RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1884 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L134RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1884)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L134RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1884)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS911
    = _M0L134RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1884;
    goto join_910;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS889);
  }
  goto joinlet_1916;
  join_910:;
  _M0L3errS912 = _M0L11_2atry__errS911;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS915
  = (struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS912;
  _M0L7_2anameS916 = _M0L36_2aMoonBitTestDriverInternalSkipTestS915->$0;
  _M0L6_2acntS1904
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS915));
  if (_M0L6_2acntS1904 > 1) {
    int32_t _M0L11_2anew__cntS1905 = _M0L6_2acntS1904 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS915), _M0L11_2anew__cntS1905);
    moonbit_incref_cycle_free(_M0L7_2anameS916);
  } else if (_M0L6_2acntS1904 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS915);
  }
  _M0L4nameS914 = _M0L7_2anameS916;
  goto join_913;
  goto joinlet_1926;
  join_913:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS889(_M0L14handle__resultS889, _M0L4nameS914, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS889);
  moonbit_decref_cycle_free(_M0L4nameS914);
  joinlet_1926:;
  joinlet_1916:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS896(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS1872,
  void* _M0L3errS897
) {
  void* _M0L1eS899;
  moonbit_string_t _M0L1eS901;
  moonbit_string_t _result_1929;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS897)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS902 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS897;
      moonbit_string_t _M0L4_2aeS903 = _M0L10_2aFailureS902->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS903);
      _M0L1eS901 = _M0L4_2aeS903;
      goto join_900;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS904 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS897;
      moonbit_string_t _M0L4_2aeS905 = _M0L15_2aInspectErrorS904->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS905);
      _M0L1eS901 = _M0L4_2aeS905;
      goto join_900;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS906 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS897;
      moonbit_string_t _M0L4_2aeS907 = _M0L16_2aSnapshotErrorS906->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS907);
      _M0L1eS901 = _M0L4_2aeS907;
      goto join_900;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS908 =
        (struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS897;
      moonbit_string_t _M0L4_2aeS909 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS908->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS909);
      _M0L1eS901 = _M0L4_2aeS909;
      goto join_900;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS897);
      _M0L1eS899 = _M0L3errS897;
      goto join_898;
      break;
    }
  }
  join_900:;
  return _M0L1eS901;
  join_898:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_1929 = _M0FP15Error10to__string(_M0L1eS899);
  moonbit_decref_cycle_free(_M0L1eS899);
  return _result_1929;
}

int32_t _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS889(
  struct _M0TWssbEu* _M0L6_2aenvS1869,
  moonbit_string_t _M0L10__testnameS890,
  moonbit_string_t _M0L7messageS891,
  int32_t _M0L7skippedS892
) {
  struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c889* _M0L14_2acasted__envS1870;
  moonbit_string_t _M0L8filenameS886;
  int32_t _M0L5indexS888;
  moonbit_string_t _M0L10file__nameS893;
  moonbit_string_t _M0L7messageS894;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS895;
  moonbit_string_t _M0L6_2atmpS1871;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1870
  = (struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c889*)_M0L6_2aenvS1869;
  _M0L8filenameS886 = _M0L14_2acasted__envS1870->$1;
  _M0L5indexS888 = _M0L14_2acasted__envS1870->$0;
  if (!_M0L7skippedS892 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS893
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS886, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS894
  = _M0MPC16string6String14escape_2einner(_M0L7messageS891, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS895
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS895, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS895, _M0L10file__nameS893);
  moonbit_decref_cycle_free(_M0L10file__nameS893);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS895, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS895, _M0L5indexS888);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS895, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS895, _M0L7messageS894);
  moonbit_decref_cycle_free(_M0L7messageS894);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS895, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1871
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS895);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS895);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1871);
  moonbit_decref_cycle_free(_M0L6_2atmpS1871);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS884(
  struct _M0TWEu* _M0L6_2aenvS1866
) {
  struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c884* _M0L14_2acasted__envS1867;
  moonbit_string_t _M0L8filenameS886;
  int32_t _M0L5indexS888;
  moonbit_string_t _M0L10file__nameS885;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS887;
  moonbit_string_t _M0L6_2atmpS1868;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1867
  = (struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2freceptors__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c884*)_M0L6_2aenvS1866;
  _M0L8filenameS886 = _M0L14_2acasted__envS1867->$1;
  _M0L5indexS888 = _M0L14_2acasted__envS1867->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS885
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS886, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS887
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS887, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS887, _M0L10file__nameS885);
  moonbit_decref_cycle_free(_M0L10file__nameS885);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS887, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS887, _M0L5indexS888);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS887, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1868
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS887);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS887);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1868);
  moonbit_decref_cycle_free(_M0L6_2atmpS1868);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S854;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS861;
  struct _M0TUsiE** _M0L6_2atmpS1865;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS868;
  moonbit_string_t* _M0L9cli__argsS869;
  moonbit_string_t _M0L6_2atmpS1864;
  moonbit_string_t _M0L6_2atmpS1863;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS870;
  int32_t _M0L7_2abindS871;
  moonbit_string_t* _M0L7_2abindS872;
  int32_t _M0L6_2acntS1906;
  int32_t _M0L2__S873;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S854 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS861 = 0;
  _M0L6_2atmpS1865 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS868
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS868)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS868->$0 = _M0L6_2atmpS1865;
  _M0L16file__and__indexS868->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS869
  = _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS869)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS1864 = (moonbit_string_t)_M0L9cli__argsS869[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS1864);
  moonbit_decref_cycle_free(_M0L9cli__argsS869);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1863
  = _M0MP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS1864);
  moonbit_decref_cycle_free(_M0L6_2atmpS1864);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS870
  = _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS861(_M0L51moonbit__test__driver__internal__split__mbt__stringS861, _M0L6_2atmpS1863, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS1863);
  _M0L7_2abindS871 = _M0L10test__argsS870->$1;
  _M0L7_2abindS872 = _M0L10test__argsS870->$0;
  _M0L6_2acntS1906
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS870));
  if (_M0L6_2acntS1906 > 1) {
    int32_t _M0L11_2anew__cntS1907 = _M0L6_2acntS1906 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS870), _M0L11_2anew__cntS1907);
    moonbit_incref_cycle_free(_M0L7_2abindS872);
  } else if (_M0L6_2acntS1906 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS870);
  }
  _M0L2__S873 = 0;
  while (1) {
    if (_M0L2__S873 < _M0L7_2abindS871) {
      moonbit_string_t _M0L3argS874 =
        (moonbit_string_t)_M0L7_2abindS872[_M0L2__S873];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS875;
      moonbit_string_t _M0L4fileS876;
      moonbit_string_t _M0L5rangeS877;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS878;
      moonbit_string_t _M0L6_2atmpS1861;
      int32_t _M0L5startS879;
      moonbit_string_t _M0L6_2atmpS1860;
      int32_t _M0L3endS880;
      int32_t _M0L1iS881;
      int32_t _M0L6_2atmpS1862;
      moonbit_incref_cycle_free(_M0L3argS874);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS875
      = _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS861(_M0L51moonbit__test__driver__internal__split__mbt__stringS861, _M0L3argS874, 58);
      moonbit_decref_cycle_free(_M0L3argS874);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS876
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS875, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS877
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS875, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS875);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS878
      = _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS861(_M0L51moonbit__test__driver__internal__split__mbt__stringS861, _M0L5rangeS877, 45);
      moonbit_decref_cycle_free(_M0L5rangeS877);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1861
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS878, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS879
      = _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S854(_M0L45moonbit__test__driver__internal__parse__int__S854, _M0L6_2atmpS1861);
      moonbit_decref_cycle_free(_M0L6_2atmpS1861);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1860
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS878, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS878);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS880
      = _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S854(_M0L45moonbit__test__driver__internal__parse__int__S854, _M0L6_2atmpS1860);
      moonbit_decref_cycle_free(_M0L6_2atmpS1860);
      _M0L1iS881 = _M0L5startS879;
      while (1) {
        if (_M0L1iS881 < _M0L3endS880) {
          struct _M0TUsiE* _M0L8_2atupleS1858;
          int32_t _M0L6_2atmpS1859;
          moonbit_incref_cycle_free(_M0L4fileS876);
          _M0L8_2atupleS1858
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS1858)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS1858->$0 = _M0L4fileS876;
          _M0L8_2atupleS1858->$1 = _M0L1iS881;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS868, _M0L8_2atupleS1858);
          _M0L6_2atmpS1859 = _M0L1iS881 + 1;
          _M0L1iS881 = _M0L6_2atmpS1859;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS876);
        }
        break;
      }
      _M0L6_2atmpS1862 = _M0L2__S873 + 1;
      _M0L2__S873 = _M0L6_2atmpS1862;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS872);
    }
    break;
  }
  return _M0L16file__and__indexS868;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS861(
  int32_t _M0L6_2aenvS1839,
  moonbit_string_t _M0L1sS862,
  int32_t _M0L3sepS863
) {
  moonbit_string_t* _M0L6_2atmpS1857;
  struct _M0TPB5ArrayGsE* _M0L3resS864;
  struct _M0TPB8MutLocalGiE* _M0L1iS865;
  struct _M0TPB8MutLocalGiE* _M0L5startS866;
  int32_t _M0L3valS1852;
  int32_t _M0L6_2atmpS1853;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1857 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS864
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS864)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS864->$0 = _M0L6_2atmpS1857;
  _M0L3resS864->$1 = 0;
  _M0L1iS865
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS865)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS865->$0 = 0;
  _M0L5startS866
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS866)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS866->$0 = 0;
  while (1) {
    int32_t _M0L3valS1840 = _M0L1iS865->$0;
    int32_t _M0L6_2atmpS1841 = Moonbit_array_length(_M0L1sS862);
    if (_M0L3valS1840 < _M0L6_2atmpS1841) {
      int32_t _M0L3valS1844 = _M0L1iS865->$0;
      int32_t _M0L6_2atmpS1843;
      int32_t _M0L6_2atmpS1842;
      int32_t _M0L3valS1851;
      int32_t _M0L6_2atmpS1850;
      if (
        _M0L3valS1844 < 0
        || _M0L3valS1844 >= Moonbit_array_length(_M0L1sS862)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1843 = _M0L1sS862[_M0L3valS1844];
      _M0L6_2atmpS1842 = _M0L6_2atmpS1843;
      if (_M0L6_2atmpS1842 == _M0L3sepS863) {
        int32_t _M0L3valS1846 = _M0L5startS866->$0;
        int32_t _M0L3valS1847 = _M0L1iS865->$0;
        moonbit_string_t _M0L6_2atmpS1845;
        int32_t _M0L3valS1849;
        int32_t _M0L6_2atmpS1848;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS1845
        = _M0MPC16string6String17unsafe__substring(_M0L1sS862, _M0L3valS1846, _M0L3valS1847);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS864, _M0L6_2atmpS1845);
        _M0L3valS1849 = _M0L1iS865->$0;
        _M0L6_2atmpS1848 = _M0L3valS1849 + 1;
        _M0L5startS866->$0 = _M0L6_2atmpS1848;
      }
      _M0L3valS1851 = _M0L1iS865->$0;
      _M0L6_2atmpS1850 = _M0L3valS1851 + 1;
      _M0L1iS865->$0 = _M0L6_2atmpS1850;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS865);
    }
    break;
  }
  _M0L3valS1852 = _M0L5startS866->$0;
  _M0L6_2atmpS1853 = Moonbit_array_length(_M0L1sS862);
  if (_M0L3valS1852 < _M0L6_2atmpS1853) {
    int32_t _M0L3valS1855 = _M0L5startS866->$0;
    int32_t _M0L6_2atmpS1856;
    moonbit_string_t _M0L6_2atmpS1854;
    moonbit_decref_cycle_free(_M0L5startS866);
    _M0L6_2atmpS1856 = Moonbit_array_length(_M0L1sS862);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS1854
    = _M0MPC16string6String17unsafe__substring(_M0L1sS862, _M0L3valS1855, _M0L6_2atmpS1856);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS864, _M0L6_2atmpS1854);
  } else {
    moonbit_decref_cycle_free(_M0L5startS866);
  }
  return _M0L3resS864;
}

int32_t _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S854(
  int32_t _M0L6_2aenvS1832,
  moonbit_string_t _M0L1sS855
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS856;
  int32_t _M0L3lenS857;
  int32_t _M0L7_2abindS858;
  int32_t _M0L1iS859;
  int32_t _result_1934;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS856
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS856)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS856->$0 = 0;
  _M0L3lenS857 = Moonbit_array_length(_M0L1sS855);
  _M0L7_2abindS858 = 0;
  _M0L1iS859 = _M0L7_2abindS858;
  while (1) {
    if (_M0L1iS859 < _M0L3lenS857) {
      int32_t _M0L3valS1837 = _M0L3resS856->$0;
      int32_t _M0L6_2atmpS1834 = _M0L3valS1837 * 10;
      int32_t _M0L6_2atmpS1836;
      int32_t _M0L6_2atmpS1835;
      int32_t _M0L6_2atmpS1833;
      int32_t _M0L6_2atmpS1838;
      if (_M0L1iS859 < 0 || _M0L1iS859 >= Moonbit_array_length(_M0L1sS855)) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1836 = _M0L1sS855[_M0L1iS859];
      _M0L6_2atmpS1835 = _M0L6_2atmpS1836 - 48;
      _M0L6_2atmpS1833 = _M0L6_2atmpS1834 + _M0L6_2atmpS1835;
      _M0L3resS856->$0 = _M0L6_2atmpS1833;
      _M0L6_2atmpS1838 = _M0L1iS859 + 1;
      _M0L1iS859 = _M0L6_2atmpS1838;
      continue;
    }
    break;
  }
  _result_1934 = _M0L3resS856->$0;
  moonbit_decref_cycle_free(_M0L3resS856);
  return _result_1934;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS853
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS853);
  return _M0L4selfS853;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S823,
  moonbit_string_t _M0L12_2adiscard__S824,
  int32_t _M0L12_2adiscard__S825,
  struct _M0TWEu* _M0L12_2adiscard__S826,
  struct _M0TWssbEu* _M0L12_2adiscard__S827,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S828
) {
  struct moonbit_result_0 _result_1935;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_1935.tag = 1;
  _result_1935.data.ok = 0;
  return _result_1935;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S829,
  moonbit_string_t _M0L12_2adiscard__S830,
  int32_t _M0L12_2adiscard__S831,
  struct _M0TWEu* _M0L12_2adiscard__S832,
  struct _M0TWssbEu* _M0L12_2adiscard__S833,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S834
) {
  struct moonbit_result_0 _result_1936;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_1936.tag = 1;
  _result_1936.data.ok = 0;
  return _result_1936;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S835,
  moonbit_string_t _M0L12_2adiscard__S836,
  int32_t _M0L12_2adiscard__S837,
  struct _M0TWEu* _M0L12_2adiscard__S838,
  struct _M0TWssbEu* _M0L12_2adiscard__S839,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S840
) {
  struct moonbit_result_0 _result_1937;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_1937.tag = 1;
  _result_1937.data.ok = 0;
  return _result_1937;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S841,
  moonbit_string_t _M0L12_2adiscard__S842,
  int32_t _M0L12_2adiscard__S843,
  struct _M0TWEu* _M0L12_2adiscard__S844,
  struct _M0TWssbEu* _M0L12_2adiscard__S845,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S846
) {
  struct moonbit_result_0 _result_1938;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_1938.tag = 1;
  _result_1938.data.ok = 0;
  return _result_1938;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S847,
  moonbit_string_t _M0L12_2adiscard__S848,
  int32_t _M0L12_2adiscard__S849,
  struct _M0TWEu* _M0L12_2adiscard__S850,
  struct _M0TWssbEu* _M0L12_2adiscard__S851,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S852
) {
  struct moonbit_result_0 _result_1939;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_1939.tag = 1;
  _result_1939.data.ok = 0;
  return _result_1939;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S822
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget* _M0FP26RiantR8snn__mbt16infer__receptors(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* _M0L9receptorsS800
) {
  int32_t* _M0L6_2atmpS1831;
  struct _M0TPB5ArrayGiE* _M0L3gluS797;
  int32_t* _M0L6_2atmpS1830;
  struct _M0TPB5ArrayGiE* _M0L4gabaS798;
  struct _M0TPB8MutLocalGiE* _M0L1iS799;
  struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget* _block_1942;
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\infer_receptors.mbt"
  _M0L6_2atmpS1831 = (int32_t*)moonbit_empty_int32_array;
  _M0L3gluS797
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L3gluS797)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L3gluS797->$0 = _M0L6_2atmpS1831;
  _M0L3gluS797->$1 = 0;
  _M0L6_2atmpS1830 = (int32_t*)moonbit_empty_int32_array;
  _M0L4gabaS798
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L4gabaS798)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L4gabaS798->$0 = _M0L6_2atmpS1830;
  _M0L4gabaS798->$1 = 0;
  _M0L1iS799
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS799)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS799->$0 = 0;
  while (1) {
    int32_t _M0L3valS1819 = _M0L1iS799->$0;
    int32_t _M0L6_2atmpS1820;
    #line 21 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\infer_receptors.mbt"
    _M0L6_2atmpS1820
    = _M0MPC15array5Array6lengthGRP26RiantR8snn__mbt8ReceptorE(_M0L9receptorsS800);
    if (_M0L3valS1819 < _M0L6_2atmpS1820) {
      int32_t _M0L3valS1829 = _M0L1iS799->$0;
      struct _M0TP26RiantR8snn__mbt8Receptor* _M0L1rS801;
      moonbit_string_t _M0L6targetS1821;
      int32_t _M0L3valS1828;
      int32_t _M0L6_2atmpS1827;
      #line 22 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\infer_receptors.mbt"
      _M0L1rS801
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt8ReceptorE(_M0L9receptorsS800, _M0L3valS1829);
      _M0L6targetS1821 = _M0L1rS801->$9;
      #line 23 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\infer_receptors.mbt"
      if (
        _M0L6targetS1821 == (moonbit_string_t)moonbit_string_literal_9.data
        || Moonbit_array_length(_M0L6targetS1821)
           == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
           && 0
              == memcmp(_M0L6targetS1821, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L6targetS1821) * 2)
      ) {
        int32_t _M0L3valS1823;
        int32_t _M0L6_2atmpS1822;
        moonbit_decref_cycle_free(_M0L1rS801);
        _M0L3valS1823 = _M0L1iS799->$0;
        #line 24 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\infer_receptors.mbt"
        _M0L6_2atmpS1822
        = _M0MPC15array5Array4pushGiE(_M0L3gluS797, _M0L3valS1823);
      } else {
        moonbit_string_t _M0L6targetS1824 = _M0L1rS801->$9;
        int32_t _M0L6_2acntS1908 =
          Moonbit_rc_count(Moonbit_object_header(_M0L1rS801));
        int32_t _result_1941;
        if (_M0L6_2acntS1908 > 1) {
          int32_t _M0L11_2anew__cntS1909 = _M0L6_2acntS1908 - 1;
          Moonbit_set_rc_count(Moonbit_object_header(_M0L1rS801), _M0L11_2anew__cntS1909);
          moonbit_incref_cycle_free(_M0L6targetS1824);
        } else if (_M0L6_2acntS1908 == 1) {
          #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\infer_receptors.mbt"
          moonbit_free(_M0L1rS801);
        }
        #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\infer_receptors.mbt"
        _result_1941
        = _M0L6targetS1824
          == (moonbit_string_t)moonbit_string_literal_10.data
          || Moonbit_array_length(_M0L6targetS1824)
             == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_10.data)
             && 0
                == memcmp(_M0L6targetS1824, (moonbit_string_t)moonbit_string_literal_10.data, Moonbit_array_length(_M0L6targetS1824) * 2);
        moonbit_decref_cycle_free(_M0L6targetS1824);
        if (_result_1941) {
          int32_t _M0L3valS1826 = _M0L1iS799->$0;
          int32_t _M0L6_2atmpS1825;
          #line 26 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\infer_receptors.mbt"
          _M0L6_2atmpS1825
          = _M0MPC15array5Array4pushGiE(_M0L4gabaS798, _M0L3valS1826);
        }
      }
      _M0L3valS1828 = _M0L1iS799->$0;
      _M0L6_2atmpS1827 = _M0L3valS1828 + 1;
      _M0L1iS799->$0 = _M0L6_2atmpS1827;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS799);
    }
    break;
  }
  _block_1942
  = (struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt17ReceptorsByTarget));
  Moonbit_object_header(_block_1942)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _block_1942->$0 = _M0L3gluS797;
  _block_1942->$1 = _M0L4gabaS798;
  return _block_1942;
}

int32_t _M0FP26RiantR8snn__mbt18receptors__current(
  struct _M0TPB5ArrayGfE* _M0L9g__matrixS792,
  struct _M0TPB5ArrayGfE* _M0L1vS784,
  struct _M0TP26RiantR8snn__mbt9Receptors* _M0L2rsS786,
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _M0L9nmda__depS794,
  struct _M0TPB5ArrayGfE* _M0L3outS795
) {
  int32_t _M0L1nS783;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* _M0L3recS1818;
  int32_t _M0L6n__recS785;
  int32_t _M0L7_2abindS787;
  int32_t _M0L1kS788;
  #line 277 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  #line 284 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L1nS783 = _M0MPC15array5Array6lengthGfE(_M0L1vS784);
  _M0L3recS1818 = _M0L2rsS786->$0;
  #line 285 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L6n__recS785
  = _M0MPC15array5Array6lengthGRP26RiantR8snn__mbt8ReceptorE(_M0L3recS1818);
  _M0L7_2abindS787 = 0;
  _M0L1kS788 = _M0L7_2abindS787;
  while (1) {
    if (_M0L1kS788 < _M0L6n__recS785) {
      struct _M0TPB5ArrayGfE* _M0L6g__colS789;
      int32_t _M0L7_2abindS790;
      int32_t _M0L1iS791;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* _M0L3recS1816;
      struct _M0TP26RiantR8snn__mbt8Receptor* _M0L6_2atmpS1815;
      int32_t _M0L6_2atmpS1817;
      #line 288 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
      _M0L6g__colS789 = _M0MPC15array5Array4makeGfE(_M0L1nS783, 0x0p+0f);
      _M0L7_2abindS790 = 0;
      _M0L1iS791 = _M0L7_2abindS790;
      while (1) {
        if (_M0L1iS791 < _M0L1nS783) {
          int32_t _M0L6_2atmpS1813 = _M0L1iS791 * _M0L6n__recS785;
          int32_t _M0L6_2atmpS1812 = _M0L6_2atmpS1813 + _M0L1kS788;
          float _M0L6_2atmpS1811;
          int32_t _M0L6_2atmpS1814;
          #line 290 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
          _M0L6_2atmpS1811
          = _M0MPC15array5Array2atGfE(_M0L9g__matrixS792, _M0L6_2atmpS1812);
          #line 290 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
          _M0MPC15array5Array3setGfE(_M0L6g__colS789, _M0L1iS791, _M0L6_2atmpS1811);
          _M0L6_2atmpS1814 = _M0L1iS791 + 1;
          _M0L1iS791 = _M0L6_2atmpS1814;
          continue;
        }
        break;
      }
      _M0L3recS1816 = _M0L2rsS786->$0;
      #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
      _M0L6_2atmpS1815
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt8ReceptorE(_M0L3recS1816, _M0L1kS788);
      #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
      _M0FP26RiantR8snn__mbt17receptor__current(_M0L6g__colS789, _M0L1vS784, _M0L6_2atmpS1815, _M0L9nmda__depS794, _M0L3outS795);
      moonbit_decref_cycle_free(_M0L6g__colS789);
      moonbit_decref_cycle_free(_M0L6_2atmpS1815);
      _M0L6_2atmpS1817 = _M0L1kS788 + 1;
      _M0L1kS788 = _M0L6_2atmpS1817;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt17receptor__current(
  struct _M0TPB5ArrayGfE* _M0L1gS769,
  struct _M0TPB5ArrayGfE* _M0L1vS776,
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L1rS771,
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _M0L9nmda__depS777,
  struct _M0TPB5ArrayGfE* _M0L3outS778
) {
  int32_t _M0L1nS768;
  float _M0L4gsynS770;
  float _M0L6e__revS772;
  #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L1nS768 = _M0MPC15array5Array6lengthGfE(_M0L1gS769);
  _M0L4gsynS770 = _M0L1rS771->$4;
  _M0L6e__revS772 = _M0L1rS771->$0;
  if (_M0L1rS771->$8) {
    int32_t _M0L7_2abindS773 = 0;
    int32_t _M0L1iS774 = _M0L7_2abindS773;
    while (1) {
      if (_M0L1iS774 < _M0L1nS768) {
        float _M0L6_2atmpS1801;
        float _M0L1bS775;
        float _M0L6_2atmpS1794;
        float _M0L6_2atmpS1800;
        float _M0L6_2atmpS1797;
        float _M0L6_2atmpS1799;
        float _M0L6_2atmpS1798;
        float _M0L6_2atmpS1796;
        float _M0L6_2atmpS1795;
        float _M0L6_2atmpS1793;
        int32_t _M0L6_2atmpS1802;
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS1801 = _M0MPC15array5Array2atGfE(_M0L1vS776, _M0L1iS774);
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L1bS775
        = _M0FP26RiantR8snn__mbt12nmda__gating(_M0L6_2atmpS1801, _M0L9nmda__depS777);
        #line 265 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS1794
        = _M0MPC15array5Array2atGfE(_M0L3outS778, _M0L1iS774);
        #line 265 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS1800 = _M0MPC15array5Array2atGfE(_M0L1gS769, _M0L1iS774);
        _M0L6_2atmpS1797 = _M0L4gsynS770 * _M0L6_2atmpS1800;
        #line 265 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS1799 = _M0MPC15array5Array2atGfE(_M0L1vS776, _M0L1iS774);
        _M0L6_2atmpS1798 = _M0L6_2atmpS1799 - _M0L6e__revS772;
        _M0L6_2atmpS1796 = _M0L6_2atmpS1797 * _M0L6_2atmpS1798;
        _M0L6_2atmpS1795 = _M0L6_2atmpS1796 * _M0L1bS775;
        _M0L6_2atmpS1793 = _M0L6_2atmpS1794 + _M0L6_2atmpS1795;
        #line 265 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0MPC15array5Array3setGfE(_M0L3outS778, _M0L1iS774, _M0L6_2atmpS1793);
        _M0L6_2atmpS1802 = _M0L1iS774 + 1;
        _M0L1iS774 = _M0L6_2atmpS1802;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L7_2abindS780 = 0;
    int32_t _M0L1iS781 = _M0L7_2abindS780;
    while (1) {
      if (_M0L1iS781 < _M0L1nS768) {
        float _M0L6_2atmpS1804;
        float _M0L6_2atmpS1809;
        float _M0L6_2atmpS1806;
        float _M0L6_2atmpS1808;
        float _M0L6_2atmpS1807;
        float _M0L6_2atmpS1805;
        float _M0L6_2atmpS1803;
        int32_t _M0L6_2atmpS1810;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS1804
        = _M0MPC15array5Array2atGfE(_M0L3outS778, _M0L1iS781);
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS1809 = _M0MPC15array5Array2atGfE(_M0L1gS769, _M0L1iS781);
        _M0L6_2atmpS1806 = _M0L4gsynS770 * _M0L6_2atmpS1809;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0L6_2atmpS1808 = _M0MPC15array5Array2atGfE(_M0L1vS776, _M0L1iS781);
        _M0L6_2atmpS1807 = _M0L6_2atmpS1808 - _M0L6e__revS772;
        _M0L6_2atmpS1805 = _M0L6_2atmpS1806 * _M0L6_2atmpS1807;
        _M0L6_2atmpS1803 = _M0L6_2atmpS1804 + _M0L6_2atmpS1805;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
        _M0MPC15array5Array3setGfE(_M0L3outS778, _M0L1iS781, _M0L6_2atmpS1803);
        _M0L6_2atmpS1810 = _M0L1iS781 + 1;
        _M0L1iS781 = _M0L6_2atmpS1810;
        continue;
      }
      break;
    }
  }
  return 0;
}

float _M0FP26RiantR8snn__mbt14alpha__synapse(
  float _M0L6tau__rS767,
  float _M0L6tau__dS766
) {
  float _M0L6_2atmpS1791;
  float _M0L6_2atmpS1792;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L6_2atmpS1791 = _M0L6tau__dS766 - _M0L6tau__rS767;
  _M0L6_2atmpS1792 = _M0L6tau__dS766 * _M0L6tau__rS767;
  return _M0L6_2atmpS1791 / _M0L6_2atmpS1792;
}

float _M0FP26RiantR8snn__mbt12nmda__gating(
  float _M0L1vS763,
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _M0L4nmdaS762
) {
  float _M0L1kS1790;
  float _M0L3argS761;
  float _M0L8exp__argS764;
  float _M0L2mgS1788;
  float _M0L1bS1789;
  float _M0L6_2atmpS1787;
  float _M0L6_2atmpS1786;
  float _M0L5denomS765;
  #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L1kS1790 = _M0L4nmdaS762->$1;
  _M0L3argS761 = _M0L1kS1790 * _M0L1vS763;
  if (_M0L3argS761 < -0x1.5cp+6f) {
    _M0L8exp__argS764 = 0x0p+0f;
  } else if (_M0L3argS761 > 0x1.6p+6f) {
    _M0L8exp__argS764 = 0x1.2ced32a16a1b1p+126f;
  } else {
    #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
    _M0L8exp__argS764 = _M0FP26RiantR8snn__mbt4expf(_M0L3argS761);
  }
  _M0L2mgS1788 = _M0L4nmdaS762->$2;
  _M0L1bS1789 = _M0L4nmdaS762->$0;
  _M0L6_2atmpS1787 = _M0L2mgS1788 / _M0L1bS1789;
  _M0L6_2atmpS1786 = _M0L6_2atmpS1787 * _M0L8exp__argS764;
  _M0L5denomS765 = 0x1p+0f + _M0L6_2atmpS1786;
  return 0x1p+0f / _M0L5denomS765;
}

struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _M0MP26RiantR8snn__mbt21NMDAVoltageDependency4eyal(
  
) {
  struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency* _block_1947;
  #line 36 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _block_1947
  = (struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt21NMDAVoltageDependency));
  Moonbit_object_header(_block_1947)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1947->$0 = 0x1.ae147ae147ae1p+1f;
  _block_1947->$1 = -0x1.3b645a1cac083p-4f;
  _block_1947->$2 = 0x1p+0f;
  return _block_1947;
}

struct _M0TP26RiantR8snn__mbt9Receptors* _M0MP26RiantR8snn__mbt9Receptors10from__pair(
  struct _M0TP26RiantR8snn__mbt13Glutamatergic* _M0L3gluS759,
  struct _M0TP26RiantR8snn__mbt9GABAergic* _M0L4gabaS760
) {
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L4ampaS1782;
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L4nmdaS1783;
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L5gabaaS1784;
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L5gababS1785;
  struct _M0TP26RiantR8snn__mbt8Receptor** _M0L6_2atmpS1781;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* _M0L6_2atmpS1780;
  struct _M0TP26RiantR8snn__mbt9Receptors* _block_1948;
  #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L4ampaS1782 = _M0L3gluS759->$0;
  _M0L4nmdaS1783 = _M0L3gluS759->$1;
  _M0L5gabaaS1784 = _M0L4gabaS760->$0;
  _M0L5gababS1785 = _M0L4gabaS760->$1;
  moonbit_incref_cycle_free(_M0L4ampaS1782);
  moonbit_incref_cycle_free(_M0L4nmdaS1783);
  moonbit_incref_cycle_free(_M0L5gabaaS1784);
  moonbit_incref_cycle_free(_M0L5gababS1785);
  _M0L6_2atmpS1781
  = (struct _M0TP26RiantR8snn__mbt8Receptor**)moonbit_make_ref_array_raw(4);
  _M0L6_2atmpS1781[0] = _M0L4ampaS1782;
  _M0L6_2atmpS1781[1] = _M0L4nmdaS1783;
  _M0L6_2atmpS1781[2] = _M0L5gabaaS1784;
  _M0L6_2atmpS1781[3] = _M0L5gababS1785;
  _M0L6_2atmpS1780
  = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE));
  Moonbit_object_header(_M0L6_2atmpS1780)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 25, 0);
  _M0L6_2atmpS1780->$0 = _M0L6_2atmpS1781;
  _M0L6_2atmpS1780->$1 = 4;
  _block_1948
  = (struct _M0TP26RiantR8snn__mbt9Receptors*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9Receptors));
  Moonbit_object_header(_block_1948)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 28, 0);
  _block_1948->$0 = _M0L6_2atmpS1780;
  return _block_1948;
}

struct _M0TP26RiantR8snn__mbt8Receptor* _M0MP26RiantR8snn__mbt8Receptor4nmda(
  float _M0L6e__revS755,
  float _M0L6tau__rS756,
  float _M0L6tau__dS757,
  float _M0L2g0S758
) {
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L1rS754;
  float _M0L11_2afield__0S1771;
  float _M0L11_2afield__1S1772;
  float _M0L11_2afield__2S1773;
  float _M0L11_2afield__3S1774;
  float _M0L11_2afield__4S1775;
  float _M0L11_2afield__5S1776;
  float _M0L11_2afield__6S1777;
  float _M0L11_2afield__7S1778;
  moonbit_string_t _M0L11_2afield__9S1779;
  int32_t _M0L6_2acntS1910;
  struct _M0TP26RiantR8snn__mbt8Receptor* _block_1949;
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L1rS754
  = _M0MP26RiantR8snn__mbt8Receptor6simple(_M0L6e__revS755, _M0L6tau__rS756, _M0L6tau__dS757, _M0L2g0S758, (moonbit_string_t)moonbit_string_literal_9.data);
  _M0L11_2afield__0S1771 = _M0L1rS754->$0;
  _M0L11_2afield__1S1772 = _M0L1rS754->$1;
  _M0L11_2afield__2S1773 = _M0L1rS754->$2;
  _M0L11_2afield__3S1774 = _M0L1rS754->$3;
  _M0L11_2afield__4S1775 = _M0L1rS754->$4;
  _M0L11_2afield__5S1776 = _M0L1rS754->$5;
  _M0L11_2afield__6S1777 = _M0L1rS754->$6;
  _M0L11_2afield__7S1778 = _M0L1rS754->$7;
  _M0L11_2afield__9S1779 = _M0L1rS754->$9;
  _M0L6_2acntS1910 = Moonbit_rc_count(Moonbit_object_header(_M0L1rS754));
  if (_M0L6_2acntS1910 > 1) {
    int32_t _M0L11_2anew__cntS1911 = _M0L6_2acntS1910 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L1rS754), _M0L11_2anew__cntS1911);
    moonbit_incref_cycle_free(_M0L11_2afield__9S1779);
  } else if (_M0L6_2acntS1910 == 1) {
    #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
    moonbit_free(_M0L1rS754);
  }
  _block_1949
  = (struct _M0TP26RiantR8snn__mbt8Receptor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt8Receptor));
  Moonbit_object_header(_block_1949)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 31, 0);
  _block_1949->$0 = _M0L11_2afield__0S1771;
  _block_1949->$1 = _M0L11_2afield__1S1772;
  _block_1949->$2 = _M0L11_2afield__2S1773;
  _block_1949->$3 = _M0L11_2afield__3S1774;
  _block_1949->$4 = _M0L11_2afield__4S1775;
  _block_1949->$5 = _M0L11_2afield__5S1776;
  _block_1949->$6 = _M0L11_2afield__6S1777;
  _block_1949->$7 = _M0L11_2afield__7S1778;
  _block_1949->$8 = 1;
  _block_1949->$9 = _M0L11_2afield__9S1779;
  return _block_1949;
}

struct _M0TP26RiantR8snn__mbt13Glutamatergic* _M0MP26RiantR8snn__mbt13Glutamatergic6custom(
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L4ampaS752,
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L4nmdaS753
) {
  struct _M0TP26RiantR8snn__mbt13Glutamatergic* _block_1950;
  #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  moonbit_incref_cycle_free(_M0L4ampaS752);
  moonbit_incref_cycle_free(_M0L4nmdaS753);
  _block_1950
  = (struct _M0TP26RiantR8snn__mbt13Glutamatergic*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13Glutamatergic));
  Moonbit_object_header(_block_1950)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 34, 0);
  _block_1950->$0 = _M0L4ampaS752;
  _block_1950->$1 = _M0L4nmdaS753;
  return _block_1950;
}

struct _M0TP26RiantR8snn__mbt8Receptor* _M0MP26RiantR8snn__mbt8Receptor6simple(
  float _M0L6e__revS750,
  float _M0L6tau__rS744,
  float _M0L6tau__dS743,
  float _M0L2g0S749,
  moonbit_string_t _M0L6targetS751
) {
  float _M0L6_2atmpS1769;
  float _M0L6_2atmpS1770;
  float _M0L5alphaS742;
  float _M0L11tau__r__invS745;
  float _M0L11tau__d__invS746;
  float _M0L4normS747;
  float _M0L4gsynS748;
  struct _M0TP26RiantR8snn__mbt8Receptor* _block_1951;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L6_2atmpS1769 = _M0L6tau__dS743 - _M0L6tau__rS744;
  _M0L6_2atmpS1770 = _M0L6tau__dS743 * _M0L6tau__rS744;
  _M0L5alphaS742 = _M0L6_2atmpS1769 / _M0L6_2atmpS1770;
  if (_M0L6tau__rS744 > 0x0p+0f) {
    _M0L11tau__r__invS745 = 0x1p+0f / _M0L6tau__rS744;
  } else {
    _M0L11tau__r__invS745 = 0x0p+0f;
  }
  if (_M0L6tau__dS743 > 0x0p+0f) {
    _M0L11tau__d__invS746 = 0x1p+0f / _M0L6tau__dS743;
  } else {
    _M0L11tau__d__invS746 = 0x0p+0f;
  }
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L4normS747
  = _M0FP26RiantR8snn__mbt13norm__synapse(_M0L6tau__rS744, _M0L6tau__dS743);
  if (_M0L2g0S749 > 0x0p+0f) {
    _M0L4gsynS748 = _M0L2g0S749 * _M0L4normS747;
  } else {
    _M0L4gsynS748 = 0x0p+0f;
  }
  moonbit_incref_cycle_free(_M0L6targetS751);
  _block_1951
  = (struct _M0TP26RiantR8snn__mbt8Receptor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt8Receptor));
  Moonbit_object_header(_block_1951)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 31, 0);
  _block_1951->$0 = _M0L6e__revS750;
  _block_1951->$1 = _M0L6tau__rS744;
  _block_1951->$2 = _M0L6tau__dS743;
  _block_1951->$3 = _M0L2g0S749;
  _block_1951->$4 = _M0L4gsynS748;
  _block_1951->$5 = _M0L5alphaS742;
  _block_1951->$6 = _M0L11tau__r__invS745;
  _block_1951->$7 = _M0L11tau__d__invS746;
  _block_1951->$8 = 0;
  _block_1951->$9 = _M0L6targetS751;
  return _block_1951;
}

float _M0FP26RiantR8snn__mbt13norm__synapse(
  float _M0L6tau__rS740,
  float _M0L6tau__dS741
) {
  float _M0L6_2atmpS1767;
  float _M0L6_2atmpS1768;
  float _M0L6_2atmpS1764;
  float _M0L6_2atmpS1766;
  float _M0L6_2atmpS1765;
  float _M0L4t__pS739;
  float _M0L6_2atmpS1763;
  float _M0L6_2atmpS1762;
  float _M0L6_2atmpS1761;
  float _M0L6_2atmpS1757;
  float _M0L6_2atmpS1760;
  float _M0L6_2atmpS1759;
  float _M0L6_2atmpS1758;
  float _M0L6_2atmpS1756;
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L6_2atmpS1767 = _M0L6tau__rS740 * _M0L6tau__dS741;
  _M0L6_2atmpS1768 = _M0L6tau__dS741 - _M0L6tau__rS740;
  _M0L6_2atmpS1764 = _M0L6_2atmpS1767 / _M0L6_2atmpS1768;
  _M0L6_2atmpS1766 = _M0L6tau__dS741 / _M0L6tau__rS740;
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L6_2atmpS1765 = _M0FP26RiantR8snn__mbt4logf(_M0L6_2atmpS1766);
  _M0L4t__pS739 = _M0L6_2atmpS1764 * _M0L6_2atmpS1765;
  _M0L6_2atmpS1763 = -_M0L4t__pS739;
  _M0L6_2atmpS1762 = _M0L6_2atmpS1763 / _M0L6tau__rS740;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L6_2atmpS1761 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS1762);
  _M0L6_2atmpS1757 = -_M0L6_2atmpS1761;
  _M0L6_2atmpS1760 = -_M0L4t__pS739;
  _M0L6_2atmpS1759 = _M0L6_2atmpS1760 / _M0L6tau__dS741;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  _M0L6_2atmpS1758 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS1759);
  _M0L6_2atmpS1756 = _M0L6_2atmpS1757 + _M0L6_2atmpS1758;
  return 0x1p+0f / _M0L6_2atmpS1756;
}

struct _M0TP26RiantR8snn__mbt9GABAergic* _M0MP26RiantR8snn__mbt9GABAergic6custom(
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L5gabaaS737,
  struct _M0TP26RiantR8snn__mbt8Receptor* _M0L5gababS738
) {
  struct _M0TP26RiantR8snn__mbt9GABAergic* _block_1952;
  #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\receptor.mbt"
  moonbit_incref_cycle_free(_M0L5gabaaS737);
  moonbit_incref_cycle_free(_M0L5gababS738);
  _block_1952
  = (struct _M0TP26RiantR8snn__mbt9GABAergic*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9GABAergic));
  Moonbit_object_header(_block_1952)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 38, 0);
  _block_1952->$0 = _M0L5gabaaS737;
  _block_1952->$1 = _M0L5gababS738;
  return _block_1952;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS736) {
  double _M0L6_2atmpS1755;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1755 = (double)_M0L4selfS736;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1755);
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
      float* _M0L3bufS1753 = _M0L3arrS731->$0;
      int32_t _M0L6_2atmpS1754;
      _M0L3bufS1753[_M0L1iS733] = _M0L4elemS734;
      _M0L6_2atmpS1754 = _M0L1iS733 + 1;
      _M0L1iS733 = _M0L6_2atmpS1754;
      continue;
    }
    break;
  }
  return _M0L3arrS731;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS728,
  int32_t _M0L5indexS729,
  float _M0L5valueS730
) {
  int32_t _M0L3lenS727;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS727 = _M0L4selfS728->$1;
  if (_M0L5indexS729 >= 0 && _M0L5indexS729 < _M0L3lenS727) {
    float* _M0L6_2atmpS1752;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1752 = _M0MPC15array5Array6bufferGfE(_M0L4selfS728);
    _M0L6_2atmpS1752[_M0L5indexS729] = _M0L5valueS730;
    moonbit_decref_cycle_free(_M0L6_2atmpS1752);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS716,
  int32_t _M0L5indexS717
) {
  int32_t _M0L3lenS715;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS715 = _M0L4selfS716->$1;
  if (_M0L5indexS717 >= 0 && _M0L5indexS717 < _M0L3lenS715) {
    int32_t* _M0L6_2atmpS1748;
    int32_t _result_1954;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1748 = _M0MPC15array5Array6bufferGiE(_M0L4selfS716);
    _result_1954 = (int32_t)_M0L6_2atmpS1748[_M0L5indexS717];
    moonbit_decref_cycle_free(_M0L6_2atmpS1748);
    return _result_1954;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS719,
  int32_t _M0L5indexS720
) {
  int32_t _M0L3lenS718;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS718 = _M0L4selfS719->$1;
  if (_M0L5indexS720 >= 0 && _M0L5indexS720 < _M0L3lenS718) {
    float* _M0L6_2atmpS1749;
    float _result_1955;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1749 = _M0MPC15array5Array6bufferGfE(_M0L4selfS719);
    _result_1955 = (float)_M0L6_2atmpS1749[_M0L5indexS720];
    moonbit_decref_cycle_free(_M0L6_2atmpS1749);
    return _result_1955;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TP26RiantR8snn__mbt8Receptor* _M0MPC15array5Array2atGRP26RiantR8snn__mbt8ReceptorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* _M0L4selfS722,
  int32_t _M0L5indexS723
) {
  int32_t _M0L3lenS721;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS721 = _M0L4selfS722->$1;
  if (_M0L5indexS723 >= 0 && _M0L5indexS723 < _M0L3lenS721) {
    struct _M0TP26RiantR8snn__mbt8Receptor** _M0L6_2atmpS1750;
    struct _M0TP26RiantR8snn__mbt8Receptor* _M0L6_2atmpS1886;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1750
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt8ReceptorE(_M0L4selfS722);
    _M0L6_2atmpS1886
    = (struct _M0TP26RiantR8snn__mbt8Receptor*)_M0L6_2atmpS1750[
        _M0L5indexS723
      ];
    if (_M0L6_2atmpS1886) {
      moonbit_incref_cycle_free(_M0L6_2atmpS1886);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS1750);
    return _M0L6_2atmpS1886;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS725,
  int32_t _M0L5indexS726
) {
  int32_t _M0L3lenS724;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS724 = _M0L4selfS725->$1;
  if (_M0L5indexS726 >= 0 && _M0L5indexS726 < _M0L3lenS724) {
    moonbit_string_t* _M0L6_2atmpS1751;
    moonbit_string_t _M0L6_2atmpS1887;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1751 = _M0MPC15array5Array6bufferGsE(_M0L4selfS725);
    _M0L6_2atmpS1887 = (moonbit_string_t)_M0L6_2atmpS1751[_M0L5indexS726];
    moonbit_incref_cycle_free(_M0L6_2atmpS1887);
    moonbit_decref_cycle_free(_M0L6_2atmpS1751);
    return _M0L6_2atmpS1887;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS714) {
  moonbit_string_t _M0L6_2atmpS1747;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1747 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS714);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1747);
  moonbit_decref_cycle_free(_M0L6_2atmpS1747);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS713) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS713);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS698) {
  uint64_t _M0L4bitsS701;
  uint64_t _M0L6_2atmpS1746;
  uint64_t _M0L6_2atmpS1745;
  int32_t _M0L8ieeeSignS702;
  uint64_t _M0L12ieeeMantissaS703;
  uint64_t _M0L6_2atmpS1744;
  uint64_t _M0L6_2atmpS1743;
  int32_t _M0L12ieeeExponentS704;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS705;
  struct _M0TPB17FloatingDecimal64* _M0L1vS706;
  moonbit_string_t _result_1957;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS698 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  if (_M0L3valS698 >= -0x1p+53 && _M0L3valS698 <= 0x1p+53) {
    if (_M0L3valS698 >= -0x1p+31 && _M0L3valS698 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS699;
      double _M0L6_2atmpS1732;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS699 = _M0MPC16double6Double7to__int(_M0L3valS698);
      _M0L6_2atmpS1732 = (double)_M0L1iS699;
      if (_M0L6_2atmpS1732 == _M0L3valS698) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS699, 10);
      }
    } else {
      int64_t _M0L1iS700;
      double _M0L6_2atmpS1733;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS700 = _M0MPC16double6Double9to__int64(_M0L3valS698);
      _M0L6_2atmpS1733 = (double)_M0L1iS700;
      if (_M0L6_2atmpS1733 == _M0L3valS698) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS700, 10);
      }
    }
  }
  _M0L4bitsS701 = *(int64_t*)&_M0L3valS698;
  _M0L6_2atmpS1746 = _M0L4bitsS701 >> 63;
  _M0L6_2atmpS1745 = _M0L6_2atmpS1746 & 1ull;
  _M0L8ieeeSignS702 = _M0L6_2atmpS1745 != 0ull;
  _M0L12ieeeMantissaS703 = _M0L4bitsS701 & 4503599627370495ull;
  _M0L6_2atmpS1744 = _M0L4bitsS701 >> 52;
  _M0L6_2atmpS1743 = _M0L6_2atmpS1744 & 2047ull;
  _M0L12ieeeExponentS704 = (int32_t)_M0L6_2atmpS1743;
  if (
    _M0L12ieeeExponentS704 == 2047
    || _M0L12ieeeExponentS704 == 0 && _M0L12ieeeMantissaS703 == 0ull
  ) {
    int32_t _M0L6_2atmpS1734 = _M0L12ieeeExponentS704 != 0;
    int32_t _M0L6_2atmpS1735 = _M0L12ieeeMantissaS703 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS702, _M0L6_2atmpS1734, _M0L6_2atmpS1735);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS705
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS703, _M0L12ieeeExponentS704);
  if (_M0L7_2abindS705 == 0) {
    uint32_t _M0L6_2atmpS1736;
    if (_M0L7_2abindS705) {
      moonbit_decref_cycle_free(_M0L7_2abindS705);
    }
    _M0L6_2atmpS1736 = *(uint32_t*)&_M0L12ieeeExponentS704;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS706 = _M0FPB3d2d(_M0L12ieeeMantissaS703, _M0L6_2atmpS1736);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS707 = _M0L7_2abindS705;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS708 = _M0L7_2aSomeS707;
    struct _M0TPB17FloatingDecimal64* _M0L1xS709 = _M0L4_2afS708;
    while (1) {
      uint64_t _M0L8mantissaS1742 = _M0L1xS709->$0;
      uint64_t _M0L1qS710 = _M0L8mantissaS1742 / 10ull;
      uint64_t _M0L8mantissaS1740 = _M0L1xS709->$0;
      uint64_t _M0L6_2atmpS1741 = 10ull * _M0L1qS710;
      uint64_t _M0L1rS711 = _M0L8mantissaS1740 - _M0L6_2atmpS1741;
      int32_t _M0L8exponentS1739;
      int32_t _M0L6_2atmpS1738;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1737;
      if (_M0L1rS711 != 0ull) {
        _M0L1vS706 = _M0L1xS709;
        break;
      }
      _M0L8exponentS1739 = _M0L1xS709->$1;
      moonbit_decref_cycle_free(_M0L1xS709);
      _M0L6_2atmpS1738 = _M0L8exponentS1739 + 1;
      _M0L6_2atmpS1737
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1737)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1737->$0 = _M0L1qS710;
      _M0L6_2atmpS1737->$1 = _M0L6_2atmpS1738;
      _M0L1xS709 = _M0L6_2atmpS1737;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1957 = _M0FPB9to__chars(_M0L1vS706, _M0L8ieeeSignS702);
  moonbit_decref_cycle_free(_M0L1vS706);
  return _result_1957;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS693,
  int32_t _M0L12ieeeExponentS695
) {
  uint64_t _M0L2m2S692;
  int32_t _M0L6_2atmpS1731;
  int32_t _M0L2e2S694;
  int32_t _M0L6_2atmpS1730;
  uint64_t _M0L6_2atmpS1729;
  uint64_t _M0L4maskS696;
  uint64_t _M0L8fractionS697;
  int32_t _M0L6_2atmpS1728;
  uint64_t _M0L6_2atmpS1727;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1726;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S692 = 4503599627370496ull | _M0L12ieeeMantissaS693;
  _M0L6_2atmpS1731 = _M0L12ieeeExponentS695 - 1023;
  _M0L2e2S694 = _M0L6_2atmpS1731 - 52;
  if (_M0L2e2S694 > 0) {
    return 0;
  }
  if (_M0L2e2S694 < -52) {
    return 0;
  }
  _M0L6_2atmpS1730 = -_M0L2e2S694;
  _M0L6_2atmpS1729 = 1ull << (_M0L6_2atmpS1730 & 63);
  _M0L4maskS696 = _M0L6_2atmpS1729 - 1ull;
  _M0L8fractionS697 = _M0L2m2S692 & _M0L4maskS696;
  if (_M0L8fractionS697 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1728 = -_M0L2e2S694;
  _M0L6_2atmpS1727 = _M0L2m2S692 >> (_M0L6_2atmpS1728 & 63);
  _M0L6_2atmpS1726
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1726)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1726->$0 = _M0L6_2atmpS1727;
  _M0L6_2atmpS1726->$1 = 0;
  return _M0L6_2atmpS1726;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS660,
  int32_t _M0L4signS658
) {
  moonbit_bytes_t _M0L6resultS656;
  int32_t _M0Lm5indexS657;
  uint64_t _M0L6outputS659;
  int32_t _M0L7olengthS661;
  int32_t _M0L8exponentS1725;
  int32_t _M0L6_2atmpS1724;
  int32_t _M0Lm3expS662;
  int32_t _M0L6_2atmpS1723;
  int32_t _M0L6_2atmpS1721;
  int32_t _M0L18scientificNotationS663;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS656 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS657 = 0;
  if (_M0L4signS658) {
    int32_t _M0L6_2atmpS1595 = _M0Lm5indexS657;
    int32_t _M0L6_2atmpS1596;
    if (
      _M0L6_2atmpS1595 < 0
      || _M0L6_2atmpS1595 >= Moonbit_array_length(_M0L6resultS656)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS656[_M0L6_2atmpS1595] = 45;
    _M0L6_2atmpS1596 = _M0Lm5indexS657;
    _M0Lm5indexS657 = _M0L6_2atmpS1596 + 1;
  }
  _M0L6outputS659 = _M0L1vS660->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS661 = _M0FPB17decimal__length17(_M0L6outputS659);
  _M0L8exponentS1725 = _M0L1vS660->$1;
  _M0L6_2atmpS1724 = _M0L8exponentS1725 + _M0L7olengthS661;
  _M0Lm3expS662 = _M0L6_2atmpS1724 - 1;
  _M0L6_2atmpS1723 = _M0Lm3expS662;
  if (_M0L6_2atmpS1723 >= -6) {
    int32_t _M0L6_2atmpS1722 = _M0Lm3expS662;
    _M0L6_2atmpS1721 = _M0L6_2atmpS1722 < 21;
  } else {
    _M0L6_2atmpS1721 = 0;
  }
  _M0L18scientificNotationS663 = !_M0L6_2atmpS1721;
  if (_M0L18scientificNotationS663) {
    int32_t _M0L7_2abindS664 = _M0L7olengthS661 - 1;
    uint64_t _M0L6outputS665;
    int32_t _M0L1iS666 = 0;
    uint64_t _M0L6outputS667 = _M0L6outputS659;
    int32_t _M0L6_2atmpS1597;
    int32_t _M0L6_2atmpS1601;
    int32_t _M0L6_2atmpS1600;
    int32_t _M0L6_2atmpS1599;
    int32_t _M0L6_2atmpS1598;
    int32_t _M0L6_2atmpS1605;
    int32_t _M0L6_2atmpS1606;
    int32_t _M0L6_2atmpS1607;
    int32_t _M0L6_2atmpS1608;
    int32_t _M0L6_2atmpS1609;
    int32_t _M0L6_2atmpS1615;
    int32_t _M0L6_2atmpS1648;
    moonbit_string_t _result_1959;
    while (1) {
      if (_M0L1iS666 < _M0L7_2abindS664) {
        uint64_t _M0L1cS668 = _M0L6outputS667 % 10ull;
        int32_t _M0L6_2atmpS1654 = _M0Lm5indexS657;
        int32_t _M0L6_2atmpS1653 = _M0L6_2atmpS1654 + _M0L7olengthS661;
        int32_t _M0L6_2atmpS1649 = _M0L6_2atmpS1653 - _M0L1iS666;
        int32_t _M0L6_2atmpS1652 = (int32_t)_M0L1cS668;
        int32_t _M0L6_2atmpS1651 = 48 + _M0L6_2atmpS1652;
        int32_t _M0L6_2atmpS1650 = _M0L6_2atmpS1651 & 0xff;
        int32_t _M0L6_2atmpS1655;
        uint64_t _M0L6_2atmpS1656;
        if (
          _M0L6_2atmpS1649 < 0
          || _M0L6_2atmpS1649 >= Moonbit_array_length(_M0L6resultS656)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS656[_M0L6_2atmpS1649] = _M0L6_2atmpS1650;
        _M0L6_2atmpS1655 = _M0L1iS666 + 1;
        _M0L6_2atmpS1656 = _M0L6outputS667 / 10ull;
        _M0L1iS666 = _M0L6_2atmpS1655;
        _M0L6outputS667 = _M0L6_2atmpS1656;
        continue;
      } else {
        _M0L6outputS665 = _M0L6outputS667;
      }
      break;
    }
    _M0L6_2atmpS1597 = _M0Lm5indexS657;
    _M0L6_2atmpS1601 = (int32_t)_M0L6outputS665;
    _M0L6_2atmpS1600 = _M0L6_2atmpS1601 % 10;
    _M0L6_2atmpS1599 = 48 + _M0L6_2atmpS1600;
    _M0L6_2atmpS1598 = _M0L6_2atmpS1599 & 0xff;
    if (
      _M0L6_2atmpS1597 < 0
      || _M0L6_2atmpS1597 >= Moonbit_array_length(_M0L6resultS656)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS656[_M0L6_2atmpS1597] = _M0L6_2atmpS1598;
    if (_M0L7olengthS661 > 1) {
      int32_t _M0L6_2atmpS1603 = _M0Lm5indexS657;
      int32_t _M0L6_2atmpS1602 = _M0L6_2atmpS1603 + 1;
      if (
        _M0L6_2atmpS1602 < 0
        || _M0L6_2atmpS1602 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1602] = 46;
    } else {
      int32_t _M0L6_2atmpS1604 = _M0Lm5indexS657;
      _M0Lm5indexS657 = _M0L6_2atmpS1604 - 1;
    }
    _M0L6_2atmpS1605 = _M0Lm5indexS657;
    _M0L6_2atmpS1606 = _M0L7olengthS661 + 1;
    _M0Lm5indexS657 = _M0L6_2atmpS1605 + _M0L6_2atmpS1606;
    _M0L6_2atmpS1607 = _M0Lm5indexS657;
    if (
      _M0L6_2atmpS1607 < 0
      || _M0L6_2atmpS1607 >= Moonbit_array_length(_M0L6resultS656)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS656[_M0L6_2atmpS1607] = 101;
    _M0L6_2atmpS1608 = _M0Lm5indexS657;
    _M0Lm5indexS657 = _M0L6_2atmpS1608 + 1;
    _M0L6_2atmpS1609 = _M0Lm3expS662;
    if (_M0L6_2atmpS1609 < 0) {
      int32_t _M0L6_2atmpS1610 = _M0Lm5indexS657;
      int32_t _M0L6_2atmpS1611;
      int32_t _M0L6_2atmpS1612;
      if (
        _M0L6_2atmpS1610 < 0
        || _M0L6_2atmpS1610 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1610] = 45;
      _M0L6_2atmpS1611 = _M0Lm5indexS657;
      _M0Lm5indexS657 = _M0L6_2atmpS1611 + 1;
      _M0L6_2atmpS1612 = _M0Lm3expS662;
      _M0Lm3expS662 = -_M0L6_2atmpS1612;
    } else {
      int32_t _M0L6_2atmpS1613 = _M0Lm5indexS657;
      int32_t _M0L6_2atmpS1614;
      if (
        _M0L6_2atmpS1613 < 0
        || _M0L6_2atmpS1613 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1613] = 43;
      _M0L6_2atmpS1614 = _M0Lm5indexS657;
      _M0Lm5indexS657 = _M0L6_2atmpS1614 + 1;
    }
    _M0L6_2atmpS1615 = _M0Lm3expS662;
    if (_M0L6_2atmpS1615 >= 100) {
      int32_t _M0L6_2atmpS1631 = _M0Lm3expS662;
      int32_t _M0L1aS670 = _M0L6_2atmpS1631 / 100;
      int32_t _M0L6_2atmpS1630 = _M0Lm3expS662;
      int32_t _M0L6_2atmpS1629 = _M0L6_2atmpS1630 / 10;
      int32_t _M0L1bS671 = _M0L6_2atmpS1629 % 10;
      int32_t _M0L6_2atmpS1628 = _M0Lm3expS662;
      int32_t _M0L1cS672 = _M0L6_2atmpS1628 % 10;
      int32_t _M0L6_2atmpS1616 = _M0Lm5indexS657;
      int32_t _M0L6_2atmpS1618 = 48 + _M0L1aS670;
      int32_t _M0L6_2atmpS1617 = _M0L6_2atmpS1618 & 0xff;
      int32_t _M0L6_2atmpS1622;
      int32_t _M0L6_2atmpS1619;
      int32_t _M0L6_2atmpS1621;
      int32_t _M0L6_2atmpS1620;
      int32_t _M0L6_2atmpS1626;
      int32_t _M0L6_2atmpS1623;
      int32_t _M0L6_2atmpS1625;
      int32_t _M0L6_2atmpS1624;
      int32_t _M0L6_2atmpS1627;
      if (
        _M0L6_2atmpS1616 < 0
        || _M0L6_2atmpS1616 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1616] = _M0L6_2atmpS1617;
      _M0L6_2atmpS1622 = _M0Lm5indexS657;
      _M0L6_2atmpS1619 = _M0L6_2atmpS1622 + 1;
      _M0L6_2atmpS1621 = 48 + _M0L1bS671;
      _M0L6_2atmpS1620 = _M0L6_2atmpS1621 & 0xff;
      if (
        _M0L6_2atmpS1619 < 0
        || _M0L6_2atmpS1619 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1619] = _M0L6_2atmpS1620;
      _M0L6_2atmpS1626 = _M0Lm5indexS657;
      _M0L6_2atmpS1623 = _M0L6_2atmpS1626 + 2;
      _M0L6_2atmpS1625 = 48 + _M0L1cS672;
      _M0L6_2atmpS1624 = _M0L6_2atmpS1625 & 0xff;
      if (
        _M0L6_2atmpS1623 < 0
        || _M0L6_2atmpS1623 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1623] = _M0L6_2atmpS1624;
      _M0L6_2atmpS1627 = _M0Lm5indexS657;
      _M0Lm5indexS657 = _M0L6_2atmpS1627 + 3;
    } else {
      int32_t _M0L6_2atmpS1632 = _M0Lm3expS662;
      if (_M0L6_2atmpS1632 >= 10) {
        int32_t _M0L6_2atmpS1642 = _M0Lm3expS662;
        int32_t _M0L1aS673 = _M0L6_2atmpS1642 / 10;
        int32_t _M0L6_2atmpS1641 = _M0Lm3expS662;
        int32_t _M0L1bS674 = _M0L6_2atmpS1641 % 10;
        int32_t _M0L6_2atmpS1633 = _M0Lm5indexS657;
        int32_t _M0L6_2atmpS1635 = 48 + _M0L1aS673;
        int32_t _M0L6_2atmpS1634 = _M0L6_2atmpS1635 & 0xff;
        int32_t _M0L6_2atmpS1639;
        int32_t _M0L6_2atmpS1636;
        int32_t _M0L6_2atmpS1638;
        int32_t _M0L6_2atmpS1637;
        int32_t _M0L6_2atmpS1640;
        if (
          _M0L6_2atmpS1633 < 0
          || _M0L6_2atmpS1633 >= Moonbit_array_length(_M0L6resultS656)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS656[_M0L6_2atmpS1633] = _M0L6_2atmpS1634;
        _M0L6_2atmpS1639 = _M0Lm5indexS657;
        _M0L6_2atmpS1636 = _M0L6_2atmpS1639 + 1;
        _M0L6_2atmpS1638 = 48 + _M0L1bS674;
        _M0L6_2atmpS1637 = _M0L6_2atmpS1638 & 0xff;
        if (
          _M0L6_2atmpS1636 < 0
          || _M0L6_2atmpS1636 >= Moonbit_array_length(_M0L6resultS656)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS656[_M0L6_2atmpS1636] = _M0L6_2atmpS1637;
        _M0L6_2atmpS1640 = _M0Lm5indexS657;
        _M0Lm5indexS657 = _M0L6_2atmpS1640 + 2;
      } else {
        int32_t _M0L6_2atmpS1643 = _M0Lm5indexS657;
        int32_t _M0L6_2atmpS1646 = _M0Lm3expS662;
        int32_t _M0L6_2atmpS1645 = 48 + _M0L6_2atmpS1646;
        int32_t _M0L6_2atmpS1644 = _M0L6_2atmpS1645 & 0xff;
        int32_t _M0L6_2atmpS1647;
        if (
          _M0L6_2atmpS1643 < 0
          || _M0L6_2atmpS1643 >= Moonbit_array_length(_M0L6resultS656)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS656[_M0L6_2atmpS1643] = _M0L6_2atmpS1644;
        _M0L6_2atmpS1647 = _M0Lm5indexS657;
        _M0Lm5indexS657 = _M0L6_2atmpS1647 + 1;
      }
    }
    _M0L6_2atmpS1648 = _M0Lm5indexS657;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1959
    = _M0FPB19string__from__bytes(_M0L6resultS656, 0, _M0L6_2atmpS1648);
    moonbit_decref_cycle_free(_M0L6resultS656);
    return _result_1959;
  } else {
    int32_t _M0L6_2atmpS1657 = _M0Lm3expS662;
    int32_t _M0L6_2atmpS1720;
    moonbit_string_t _result_1965;
    if (_M0L6_2atmpS1657 < 0) {
      int32_t _M0L6_2atmpS1658 = _M0Lm5indexS657;
      int32_t _M0L6_2atmpS1660;
      int32_t _M0L6_2atmpS1659;
      int32_t _M0L6_2atmpS1661;
      int32_t _M0L1iS675;
      int32_t _M0L6_2atmpS1676;
      int32_t _M0L6_2atmpS1678;
      int32_t _M0L6_2atmpS1677;
      int32_t _M0L7currentS677;
      int32_t _M0L1iS678;
      uint64_t _M0L6outputS679;
      if (
        _M0L6_2atmpS1658 < 0
        || _M0L6_2atmpS1658 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1658] = 48;
      _M0L6_2atmpS1660 = _M0Lm5indexS657;
      _M0L6_2atmpS1659 = _M0L6_2atmpS1660 + 1;
      if (
        _M0L6_2atmpS1659 < 0
        || _M0L6_2atmpS1659 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1659] = 46;
      _M0L6_2atmpS1661 = _M0Lm5indexS657;
      _M0Lm5indexS657 = _M0L6_2atmpS1661 + 2;
      _M0L1iS675 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1662 = _M0Lm3expS662;
        if (_M0L1iS675 > _M0L6_2atmpS1662) {
          int32_t _M0L6_2atmpS1665 = _M0Lm5indexS657;
          int32_t _M0L6_2atmpS1664 = _M0L6_2atmpS1665 - _M0L1iS675;
          int32_t _M0L6_2atmpS1663 = _M0L6_2atmpS1664 - 1;
          int32_t _M0L6_2atmpS1666;
          if (
            _M0L6_2atmpS1663 < 0
            || _M0L6_2atmpS1663 >= Moonbit_array_length(_M0L6resultS656)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS656[_M0L6_2atmpS1663] = 48;
          _M0L6_2atmpS1666 = _M0L1iS675 - 1;
          _M0L1iS675 = _M0L6_2atmpS1666;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1676 = _M0Lm5indexS657;
      _M0L6_2atmpS1678 = _M0Lm3expS662;
      _M0L6_2atmpS1677 = -1 - _M0L6_2atmpS1678;
      _M0L7currentS677 = _M0L6_2atmpS1676 + _M0L6_2atmpS1677;
      _M0L1iS678 = 0;
      _M0L6outputS679 = _M0L6outputS659;
      while (1) {
        if (_M0L1iS678 < _M0L7olengthS661) {
          int32_t _M0L6_2atmpS1673 = _M0L7currentS677 + _M0L7olengthS661;
          int32_t _M0L6_2atmpS1672 = _M0L6_2atmpS1673 - _M0L1iS678;
          int32_t _M0L6_2atmpS1667 = _M0L6_2atmpS1672 - 1;
          uint64_t _M0L6_2atmpS1671 = _M0L6outputS679 % 10ull;
          int32_t _M0L6_2atmpS1670 = (int32_t)_M0L6_2atmpS1671;
          int32_t _M0L6_2atmpS1669 = 48 + _M0L6_2atmpS1670;
          int32_t _M0L6_2atmpS1668 = _M0L6_2atmpS1669 & 0xff;
          int32_t _M0L6_2atmpS1674;
          uint64_t _M0L6_2atmpS1675;
          if (
            _M0L6_2atmpS1667 < 0
            || _M0L6_2atmpS1667 >= Moonbit_array_length(_M0L6resultS656)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS656[_M0L6_2atmpS1667] = _M0L6_2atmpS1668;
          _M0L6_2atmpS1674 = _M0L1iS678 + 1;
          _M0L6_2atmpS1675 = _M0L6outputS679 / 10ull;
          _M0L1iS678 = _M0L6_2atmpS1674;
          _M0L6outputS679 = _M0L6_2atmpS1675;
          continue;
        }
        break;
      }
      _M0Lm5indexS657 = _M0L7currentS677 + _M0L7olengthS661;
    } else {
      int32_t _M0L6_2atmpS1680 = _M0Lm3expS662;
      int32_t _M0L6_2atmpS1679 = _M0L6_2atmpS1680 + 1;
      if (_M0L6_2atmpS1679 >= _M0L7olengthS661) {
        int32_t _M0L1iS681 = 0;
        uint64_t _M0L6outputS682 = _M0L6outputS659;
        int32_t _M0L6_2atmpS1691;
        int32_t _M0L6_2atmpS1696;
        int32_t _M0L7_2abindS684;
        int32_t _M0L1iS685;
        int32_t _M0L6_2atmpS1697;
        int32_t _M0L6_2atmpS1700;
        int32_t _M0L6_2atmpS1699;
        int32_t _M0L6_2atmpS1698;
        while (1) {
          if (_M0L1iS681 < _M0L7olengthS661) {
            int32_t _M0L6_2atmpS1688 = _M0Lm5indexS657;
            int32_t _M0L6_2atmpS1687 = _M0L6_2atmpS1688 + _M0L7olengthS661;
            int32_t _M0L6_2atmpS1686 = _M0L6_2atmpS1687 - _M0L1iS681;
            int32_t _M0L6_2atmpS1681 = _M0L6_2atmpS1686 - 1;
            uint64_t _M0L6_2atmpS1685 = _M0L6outputS682 % 10ull;
            int32_t _M0L6_2atmpS1684 = (int32_t)_M0L6_2atmpS1685;
            int32_t _M0L6_2atmpS1683 = 48 + _M0L6_2atmpS1684;
            int32_t _M0L6_2atmpS1682 = _M0L6_2atmpS1683 & 0xff;
            int32_t _M0L6_2atmpS1689;
            uint64_t _M0L6_2atmpS1690;
            if (
              _M0L6_2atmpS1681 < 0
              || _M0L6_2atmpS1681 >= Moonbit_array_length(_M0L6resultS656)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS656[_M0L6_2atmpS1681] = _M0L6_2atmpS1682;
            _M0L6_2atmpS1689 = _M0L1iS681 + 1;
            _M0L6_2atmpS1690 = _M0L6outputS682 / 10ull;
            _M0L1iS681 = _M0L6_2atmpS1689;
            _M0L6outputS682 = _M0L6_2atmpS1690;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1691 = _M0Lm5indexS657;
        _M0Lm5indexS657 = _M0L6_2atmpS1691 + _M0L7olengthS661;
        _M0L6_2atmpS1696 = _M0Lm3expS662;
        _M0L7_2abindS684 = _M0L6_2atmpS1696 + 1;
        _M0L1iS685 = _M0L7olengthS661;
        while (1) {
          if (_M0L1iS685 < _M0L7_2abindS684) {
            int32_t _M0L6_2atmpS1694 = _M0Lm5indexS657;
            int32_t _M0L6_2atmpS1693 = _M0L6_2atmpS1694 + _M0L1iS685;
            int32_t _M0L6_2atmpS1692 = _M0L6_2atmpS1693 - _M0L7olengthS661;
            int32_t _M0L6_2atmpS1695;
            if (
              _M0L6_2atmpS1692 < 0
              || _M0L6_2atmpS1692 >= Moonbit_array_length(_M0L6resultS656)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS656[_M0L6_2atmpS1692] = 48;
            _M0L6_2atmpS1695 = _M0L1iS685 + 1;
            _M0L1iS685 = _M0L6_2atmpS1695;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1697 = _M0Lm5indexS657;
        _M0L6_2atmpS1700 = _M0Lm3expS662;
        _M0L6_2atmpS1699 = _M0L6_2atmpS1700 + 1;
        _M0L6_2atmpS1698 = _M0L6_2atmpS1699 - _M0L7olengthS661;
        _M0Lm5indexS657 = _M0L6_2atmpS1697 + _M0L6_2atmpS1698;
      } else {
        int32_t _M0L6_2atmpS1717 = _M0Lm5indexS657;
        int32_t _M0L6_2atmpS1716 = _M0L6_2atmpS1717 + 1;
        int32_t _M0L1iS687 = 0;
        int32_t _M0L7currentS688 = _M0L6_2atmpS1716;
        uint64_t _M0L6outputS689 = _M0L6outputS659;
        int32_t _M0L6_2atmpS1718;
        int32_t _M0L6_2atmpS1719;
        while (1) {
          if (_M0L1iS687 < _M0L7olengthS661) {
            int32_t _M0L6_2atmpS1712 = _M0L7olengthS661 - _M0L1iS687;
            int32_t _M0L6_2atmpS1710 = _M0L6_2atmpS1712 - 1;
            int32_t _M0L6_2atmpS1711 = _M0Lm3expS662;
            int32_t _M0L7currentS690;
            int32_t _M0L6_2atmpS1707;
            int32_t _M0L6_2atmpS1706;
            int32_t _M0L6_2atmpS1701;
            uint64_t _M0L6_2atmpS1705;
            int32_t _M0L6_2atmpS1704;
            int32_t _M0L6_2atmpS1703;
            int32_t _M0L6_2atmpS1702;
            int32_t _M0L6_2atmpS1708;
            uint64_t _M0L6_2atmpS1709;
            if (_M0L6_2atmpS1710 == _M0L6_2atmpS1711) {
              int32_t _M0L6_2atmpS1715 = _M0L7currentS688 + _M0L7olengthS661;
              int32_t _M0L6_2atmpS1714 = _M0L6_2atmpS1715 - _M0L1iS687;
              int32_t _M0L6_2atmpS1713 = _M0L6_2atmpS1714 - 1;
              if (
                _M0L6_2atmpS1713 < 0
                || _M0L6_2atmpS1713 >= Moonbit_array_length(_M0L6resultS656)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS656[_M0L6_2atmpS1713] = 46;
              _M0L7currentS690 = _M0L7currentS688 - 1;
            } else {
              _M0L7currentS690 = _M0L7currentS688;
            }
            _M0L6_2atmpS1707 = _M0L7currentS690 + _M0L7olengthS661;
            _M0L6_2atmpS1706 = _M0L6_2atmpS1707 - _M0L1iS687;
            _M0L6_2atmpS1701 = _M0L6_2atmpS1706 - 1;
            _M0L6_2atmpS1705 = _M0L6outputS689 % 10ull;
            _M0L6_2atmpS1704 = (int32_t)_M0L6_2atmpS1705;
            _M0L6_2atmpS1703 = 48 + _M0L6_2atmpS1704;
            _M0L6_2atmpS1702 = _M0L6_2atmpS1703 & 0xff;
            if (
              _M0L6_2atmpS1701 < 0
              || _M0L6_2atmpS1701 >= Moonbit_array_length(_M0L6resultS656)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS656[_M0L6_2atmpS1701] = _M0L6_2atmpS1702;
            _M0L6_2atmpS1708 = _M0L1iS687 + 1;
            _M0L6_2atmpS1709 = _M0L6outputS689 / 10ull;
            _M0L1iS687 = _M0L6_2atmpS1708;
            _M0L7currentS688 = _M0L7currentS690;
            _M0L6outputS689 = _M0L6_2atmpS1709;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1718 = _M0Lm5indexS657;
        _M0L6_2atmpS1719 = _M0L7olengthS661 + 1;
        _M0Lm5indexS657 = _M0L6_2atmpS1718 + _M0L6_2atmpS1719;
      }
    }
    _M0L6_2atmpS1720 = _M0Lm5indexS657;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1965
    = _M0FPB19string__from__bytes(_M0L6resultS656, 0, _M0L6_2atmpS1720);
    moonbit_decref_cycle_free(_M0L6resultS656);
    return _result_1965;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS602,
  uint32_t _M0L12ieeeExponentS601
) {
  int32_t _M0Lm2e2S599;
  uint64_t _M0Lm2m2S600;
  uint64_t _M0L6_2atmpS1594;
  uint64_t _M0L6_2atmpS1593;
  int32_t _M0L4evenS603;
  uint64_t _M0L6_2atmpS1592;
  uint64_t _M0L2mvS604;
  int32_t _M0L7mmShiftS605;
  uint64_t _M0Lm2vrS606;
  uint64_t _M0Lm2vpS607;
  uint64_t _M0Lm2vmS608;
  int32_t _M0Lm3e10S609;
  int32_t _M0Lm17vmIsTrailingZerosS610;
  int32_t _M0Lm17vrIsTrailingZerosS611;
  int32_t _M0L6_2atmpS1494;
  int32_t _M0Lm7removedS630;
  int32_t _M0Lm16lastRemovedDigitS631;
  uint64_t _M0Lm6outputS632;
  int32_t _M0L6_2atmpS1590;
  int32_t _M0L6_2atmpS1591;
  int32_t _M0L3expS655;
  uint64_t _M0L6_2atmpS1589;
  struct _M0TPB17FloatingDecimal64* _block_1971;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S599 = 0;
  _M0Lm2m2S600 = 0ull;
  if (_M0L12ieeeExponentS601 == 0u) {
    _M0Lm2e2S599 = -1076;
    _M0Lm2m2S600 = _M0L12ieeeMantissaS602;
  } else {
    int32_t _M0L6_2atmpS1493 = *(int32_t*)&_M0L12ieeeExponentS601;
    int32_t _M0L6_2atmpS1492 = _M0L6_2atmpS1493 - 1023;
    int32_t _M0L6_2atmpS1491 = _M0L6_2atmpS1492 - 52;
    _M0Lm2e2S599 = _M0L6_2atmpS1491 - 2;
    _M0Lm2m2S600 = 4503599627370496ull | _M0L12ieeeMantissaS602;
  }
  _M0L6_2atmpS1594 = _M0Lm2m2S600;
  _M0L6_2atmpS1593 = _M0L6_2atmpS1594 & 1ull;
  _M0L4evenS603 = _M0L6_2atmpS1593 == 0ull;
  _M0L6_2atmpS1592 = _M0Lm2m2S600;
  _M0L2mvS604 = 4ull * _M0L6_2atmpS1592;
  _M0L7mmShiftS605
  = _M0L12ieeeMantissaS602 != 0ull || _M0L12ieeeExponentS601 <= 1u;
  _M0Lm2vrS606 = 0ull;
  _M0Lm2vpS607 = 0ull;
  _M0Lm2vmS608 = 0ull;
  _M0Lm3e10S609 = 0;
  _M0Lm17vmIsTrailingZerosS610 = 0;
  _M0Lm17vrIsTrailingZerosS611 = 0;
  _M0L6_2atmpS1494 = _M0Lm2e2S599;
  if (_M0L6_2atmpS1494 >= 0) {
    int32_t _M0L6_2atmpS1516 = _M0Lm2e2S599;
    int32_t _M0L6_2atmpS1512;
    int32_t _M0L6_2atmpS1515;
    int32_t _M0L6_2atmpS1514;
    int32_t _M0L6_2atmpS1513;
    int32_t _M0L1qS612;
    int32_t _M0L6_2atmpS1511;
    int32_t _M0L6_2atmpS1510;
    int32_t _M0L1kS613;
    int32_t _M0L6_2atmpS1509;
    int32_t _M0L6_2atmpS1508;
    int32_t _M0L6_2atmpS1507;
    int32_t _M0L1iS614;
    struct _M0TPB8Pow5Pair _M0L4pow5S615;
    uint64_t _M0L6_2atmpS1506;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS616;
    uint64_t _M0L8_2avrOutS617;
    uint64_t _M0L8_2avpOutS618;
    uint64_t _M0L8_2avmOutS619;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1512 = _M0FPB9log10Pow2(_M0L6_2atmpS1516);
    _M0L6_2atmpS1515 = _M0Lm2e2S599;
    _M0L6_2atmpS1514 = _M0L6_2atmpS1515 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1513 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1514);
    _M0L1qS612 = _M0L6_2atmpS1512 - _M0L6_2atmpS1513;
    _M0Lm3e10S609 = _M0L1qS612;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1511 = _M0FPB8pow5bits(_M0L1qS612);
    _M0L6_2atmpS1510 = 125 + _M0L6_2atmpS1511;
    _M0L1kS613 = _M0L6_2atmpS1510 - 1;
    _M0L6_2atmpS1509 = _M0Lm2e2S599;
    _M0L6_2atmpS1508 = -_M0L6_2atmpS1509;
    _M0L6_2atmpS1507 = _M0L6_2atmpS1508 + _M0L1qS612;
    _M0L1iS614 = _M0L6_2atmpS1507 + _M0L1kS613;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S615 = _M0FPB22double__computeInvPow5(_M0L1qS612);
    _M0L6_2atmpS1506 = _M0Lm2m2S600;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS616
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1506, _M0L4pow5S615, _M0L1iS614, _M0L7mmShiftS605);
    _M0L8_2avrOutS617 = _M0L7_2abindS616.$0;
    _M0L8_2avpOutS618 = _M0L7_2abindS616.$1;
    _M0L8_2avmOutS619 = _M0L7_2abindS616.$2;
    _M0Lm2vrS606 = _M0L8_2avrOutS617;
    _M0Lm2vpS607 = _M0L8_2avpOutS618;
    _M0Lm2vmS608 = _M0L8_2avmOutS619;
    if (_M0L1qS612 <= 21) {
      int32_t _M0L6_2atmpS1502 = (int32_t)_M0L2mvS604;
      uint64_t _M0L6_2atmpS1505 = _M0L2mvS604 / 5ull;
      int32_t _M0L6_2atmpS1504 = (int32_t)_M0L6_2atmpS1505;
      int32_t _M0L6_2atmpS1503 = 5 * _M0L6_2atmpS1504;
      int32_t _M0L6mvMod5S620 = _M0L6_2atmpS1502 - _M0L6_2atmpS1503;
      if (_M0L6mvMod5S620 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS611
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS604, _M0L1qS612);
      } else if (_M0L4evenS603) {
        uint64_t _M0L6_2atmpS1496 = _M0L2mvS604 - 1ull;
        uint64_t _M0L6_2atmpS1497;
        uint64_t _M0L6_2atmpS1495;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1497 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS605);
        _M0L6_2atmpS1495 = _M0L6_2atmpS1496 - _M0L6_2atmpS1497;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS610
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1495, _M0L1qS612);
      } else {
        uint64_t _M0L6_2atmpS1498 = _M0Lm2vpS607;
        uint64_t _M0L6_2atmpS1501 = _M0L2mvS604 + 2ull;
        int32_t _M0L6_2atmpS1500;
        uint64_t _M0L6_2atmpS1499;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1500
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1501, _M0L1qS612);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1499 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1500);
        _M0Lm2vpS607 = _M0L6_2atmpS1498 - _M0L6_2atmpS1499;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1530 = _M0Lm2e2S599;
    int32_t _M0L6_2atmpS1529 = -_M0L6_2atmpS1530;
    int32_t _M0L6_2atmpS1524;
    int32_t _M0L6_2atmpS1528;
    int32_t _M0L6_2atmpS1527;
    int32_t _M0L6_2atmpS1526;
    int32_t _M0L6_2atmpS1525;
    int32_t _M0L1qS621;
    int32_t _M0L6_2atmpS1517;
    int32_t _M0L6_2atmpS1523;
    int32_t _M0L6_2atmpS1522;
    int32_t _M0L1iS622;
    int32_t _M0L6_2atmpS1521;
    int32_t _M0L1kS623;
    int32_t _M0L1jS624;
    struct _M0TPB8Pow5Pair _M0L4pow5S625;
    uint64_t _M0L6_2atmpS1520;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS626;
    uint64_t _M0L8_2avrOutS627;
    uint64_t _M0L8_2avpOutS628;
    uint64_t _M0L8_2avmOutS629;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1524 = _M0FPB9log10Pow5(_M0L6_2atmpS1529);
    _M0L6_2atmpS1528 = _M0Lm2e2S599;
    _M0L6_2atmpS1527 = -_M0L6_2atmpS1528;
    _M0L6_2atmpS1526 = _M0L6_2atmpS1527 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1525 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1526);
    _M0L1qS621 = _M0L6_2atmpS1524 - _M0L6_2atmpS1525;
    _M0L6_2atmpS1517 = _M0Lm2e2S599;
    _M0Lm3e10S609 = _M0L1qS621 + _M0L6_2atmpS1517;
    _M0L6_2atmpS1523 = _M0Lm2e2S599;
    _M0L6_2atmpS1522 = -_M0L6_2atmpS1523;
    _M0L1iS622 = _M0L6_2atmpS1522 - _M0L1qS621;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1521 = _M0FPB8pow5bits(_M0L1iS622);
    _M0L1kS623 = _M0L6_2atmpS1521 - 125;
    _M0L1jS624 = _M0L1qS621 - _M0L1kS623;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S625 = _M0FPB19double__computePow5(_M0L1iS622);
    _M0L6_2atmpS1520 = _M0Lm2m2S600;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS626
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1520, _M0L4pow5S625, _M0L1jS624, _M0L7mmShiftS605);
    _M0L8_2avrOutS627 = _M0L7_2abindS626.$0;
    _M0L8_2avpOutS628 = _M0L7_2abindS626.$1;
    _M0L8_2avmOutS629 = _M0L7_2abindS626.$2;
    _M0Lm2vrS606 = _M0L8_2avrOutS627;
    _M0Lm2vpS607 = _M0L8_2avpOutS628;
    _M0Lm2vmS608 = _M0L8_2avmOutS629;
    if (_M0L1qS621 <= 1) {
      _M0Lm17vrIsTrailingZerosS611 = 1;
      if (_M0L4evenS603) {
        int32_t _M0L6_2atmpS1518;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1518 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS605);
        _M0Lm17vmIsTrailingZerosS610 = _M0L6_2atmpS1518 == 1;
      } else {
        uint64_t _M0L6_2atmpS1519 = _M0Lm2vpS607;
        _M0Lm2vpS607 = _M0L6_2atmpS1519 - 1ull;
      }
    } else if (_M0L1qS621 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS611
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS604, _M0L1qS621);
    }
  }
  _M0Lm7removedS630 = 0;
  _M0Lm16lastRemovedDigitS631 = 0;
  _M0Lm6outputS632 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS610 || _M0Lm17vrIsTrailingZerosS611) {
    int32_t _if__result_1968;
    uint64_t _M0L6_2atmpS1560;
    uint64_t _M0L6_2atmpS1566;
    uint64_t _M0L6_2atmpS1567;
    int32_t _if__result_1969;
    int32_t _M0L6_2atmpS1563;
    int64_t _M0L6_2atmpS1562;
    uint64_t _M0L6_2atmpS1561;
    while (1) {
      uint64_t _M0L6_2atmpS1543 = _M0Lm2vpS607;
      uint64_t _M0L7vpDiv10S633 = _M0L6_2atmpS1543 / 10ull;
      uint64_t _M0L6_2atmpS1542 = _M0Lm2vmS608;
      uint64_t _M0L7vmDiv10S634 = _M0L6_2atmpS1542 / 10ull;
      uint64_t _M0L6_2atmpS1541;
      int32_t _M0L6_2atmpS1538;
      int32_t _M0L6_2atmpS1540;
      int32_t _M0L6_2atmpS1539;
      int32_t _M0L7vmMod10S636;
      uint64_t _M0L6_2atmpS1537;
      uint64_t _M0L7vrDiv10S637;
      uint64_t _M0L6_2atmpS1536;
      int32_t _M0L6_2atmpS1533;
      int32_t _M0L6_2atmpS1535;
      int32_t _M0L6_2atmpS1534;
      int32_t _M0L7vrMod10S638;
      int32_t _M0L6_2atmpS1532;
      if (_M0L7vpDiv10S633 <= _M0L7vmDiv10S634) {
        break;
      }
      _M0L6_2atmpS1541 = _M0Lm2vmS608;
      _M0L6_2atmpS1538 = (int32_t)_M0L6_2atmpS1541;
      _M0L6_2atmpS1540 = (int32_t)_M0L7vmDiv10S634;
      _M0L6_2atmpS1539 = 10 * _M0L6_2atmpS1540;
      _M0L7vmMod10S636 = _M0L6_2atmpS1538 - _M0L6_2atmpS1539;
      _M0L6_2atmpS1537 = _M0Lm2vrS606;
      _M0L7vrDiv10S637 = _M0L6_2atmpS1537 / 10ull;
      _M0L6_2atmpS1536 = _M0Lm2vrS606;
      _M0L6_2atmpS1533 = (int32_t)_M0L6_2atmpS1536;
      _M0L6_2atmpS1535 = (int32_t)_M0L7vrDiv10S637;
      _M0L6_2atmpS1534 = 10 * _M0L6_2atmpS1535;
      _M0L7vrMod10S638 = _M0L6_2atmpS1533 - _M0L6_2atmpS1534;
      _M0Lm17vmIsTrailingZerosS610
      = _M0Lm17vmIsTrailingZerosS610 && _M0L7vmMod10S636 == 0;
      if (_M0Lm17vrIsTrailingZerosS611) {
        int32_t _M0L6_2atmpS1531 = _M0Lm16lastRemovedDigitS631;
        _M0Lm17vrIsTrailingZerosS611 = _M0L6_2atmpS1531 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS611 = 0;
      }
      _M0Lm16lastRemovedDigitS631 = _M0L7vrMod10S638;
      _M0Lm2vrS606 = _M0L7vrDiv10S637;
      _M0Lm2vpS607 = _M0L7vpDiv10S633;
      _M0Lm2vmS608 = _M0L7vmDiv10S634;
      _M0L6_2atmpS1532 = _M0Lm7removedS630;
      _M0Lm7removedS630 = _M0L6_2atmpS1532 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS610) {
      while (1) {
        uint64_t _M0L6_2atmpS1556 = _M0Lm2vmS608;
        uint64_t _M0L7vmDiv10S639 = _M0L6_2atmpS1556 / 10ull;
        uint64_t _M0L6_2atmpS1555 = _M0Lm2vmS608;
        int32_t _M0L6_2atmpS1552 = (int32_t)_M0L6_2atmpS1555;
        int32_t _M0L6_2atmpS1554 = (int32_t)_M0L7vmDiv10S639;
        int32_t _M0L6_2atmpS1553 = 10 * _M0L6_2atmpS1554;
        int32_t _M0L7vmMod10S640 = _M0L6_2atmpS1552 - _M0L6_2atmpS1553;
        uint64_t _M0L6_2atmpS1551;
        uint64_t _M0L7vpDiv10S642;
        uint64_t _M0L6_2atmpS1550;
        uint64_t _M0L7vrDiv10S643;
        uint64_t _M0L6_2atmpS1549;
        int32_t _M0L6_2atmpS1546;
        int32_t _M0L6_2atmpS1548;
        int32_t _M0L6_2atmpS1547;
        int32_t _M0L7vrMod10S644;
        int32_t _M0L6_2atmpS1545;
        if (_M0L7vmMod10S640 != 0) {
          break;
        }
        _M0L6_2atmpS1551 = _M0Lm2vpS607;
        _M0L7vpDiv10S642 = _M0L6_2atmpS1551 / 10ull;
        _M0L6_2atmpS1550 = _M0Lm2vrS606;
        _M0L7vrDiv10S643 = _M0L6_2atmpS1550 / 10ull;
        _M0L6_2atmpS1549 = _M0Lm2vrS606;
        _M0L6_2atmpS1546 = (int32_t)_M0L6_2atmpS1549;
        _M0L6_2atmpS1548 = (int32_t)_M0L7vrDiv10S643;
        _M0L6_2atmpS1547 = 10 * _M0L6_2atmpS1548;
        _M0L7vrMod10S644 = _M0L6_2atmpS1546 - _M0L6_2atmpS1547;
        if (_M0Lm17vrIsTrailingZerosS611) {
          int32_t _M0L6_2atmpS1544 = _M0Lm16lastRemovedDigitS631;
          _M0Lm17vrIsTrailingZerosS611 = _M0L6_2atmpS1544 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS611 = 0;
        }
        _M0Lm16lastRemovedDigitS631 = _M0L7vrMod10S644;
        _M0Lm2vrS606 = _M0L7vrDiv10S643;
        _M0Lm2vpS607 = _M0L7vpDiv10S642;
        _M0Lm2vmS608 = _M0L7vmDiv10S639;
        _M0L6_2atmpS1545 = _M0Lm7removedS630;
        _M0Lm7removedS630 = _M0L6_2atmpS1545 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS611) {
      int32_t _M0L6_2atmpS1559 = _M0Lm16lastRemovedDigitS631;
      if (_M0L6_2atmpS1559 == 5) {
        uint64_t _M0L6_2atmpS1558 = _M0Lm2vrS606;
        uint64_t _M0L6_2atmpS1557 = _M0L6_2atmpS1558 % 2ull;
        _if__result_1968 = _M0L6_2atmpS1557 == 0ull;
      } else {
        _if__result_1968 = 0;
      }
    } else {
      _if__result_1968 = 0;
    }
    if (_if__result_1968) {
      _M0Lm16lastRemovedDigitS631 = 4;
    }
    _M0L6_2atmpS1560 = _M0Lm2vrS606;
    _M0L6_2atmpS1566 = _M0Lm2vrS606;
    _M0L6_2atmpS1567 = _M0Lm2vmS608;
    if (_M0L6_2atmpS1566 == _M0L6_2atmpS1567) {
      if (!_M0L4evenS603) {
        _if__result_1969 = 1;
      } else {
        int32_t _M0L6_2atmpS1565 = _M0Lm17vmIsTrailingZerosS610;
        _if__result_1969 = !_M0L6_2atmpS1565;
      }
    } else {
      _if__result_1969 = 0;
    }
    if (_if__result_1969) {
      _M0L6_2atmpS1563 = 1;
    } else {
      int32_t _M0L6_2atmpS1564 = _M0Lm16lastRemovedDigitS631;
      _M0L6_2atmpS1563 = _M0L6_2atmpS1564 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1562 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1563);
    _M0L6_2atmpS1561 = *(uint64_t*)&_M0L6_2atmpS1562;
    _M0Lm6outputS632 = _M0L6_2atmpS1560 + _M0L6_2atmpS1561;
  } else {
    int32_t _M0Lm7roundUpS645 = 0;
    uint64_t _M0L6_2atmpS1588 = _M0Lm2vpS607;
    uint64_t _M0L8vpDiv100S646 = _M0L6_2atmpS1588 / 100ull;
    uint64_t _M0L6_2atmpS1587 = _M0Lm2vmS608;
    uint64_t _M0L8vmDiv100S647 = _M0L6_2atmpS1587 / 100ull;
    uint64_t _M0L6_2atmpS1582;
    uint64_t _M0L6_2atmpS1585;
    uint64_t _M0L6_2atmpS1586;
    int32_t _M0L6_2atmpS1584;
    uint64_t _M0L6_2atmpS1583;
    if (_M0L8vpDiv100S646 > _M0L8vmDiv100S647) {
      uint64_t _M0L6_2atmpS1573 = _M0Lm2vrS606;
      uint64_t _M0L8vrDiv100S648 = _M0L6_2atmpS1573 / 100ull;
      uint64_t _M0L6_2atmpS1572 = _M0Lm2vrS606;
      int32_t _M0L6_2atmpS1569 = (int32_t)_M0L6_2atmpS1572;
      int32_t _M0L6_2atmpS1571 = (int32_t)_M0L8vrDiv100S648;
      int32_t _M0L6_2atmpS1570 = 100 * _M0L6_2atmpS1571;
      int32_t _M0L8vrMod100S649 = _M0L6_2atmpS1569 - _M0L6_2atmpS1570;
      int32_t _M0L6_2atmpS1568;
      _M0Lm7roundUpS645 = _M0L8vrMod100S649 >= 50;
      _M0Lm2vrS606 = _M0L8vrDiv100S648;
      _M0Lm2vpS607 = _M0L8vpDiv100S646;
      _M0Lm2vmS608 = _M0L8vmDiv100S647;
      _M0L6_2atmpS1568 = _M0Lm7removedS630;
      _M0Lm7removedS630 = _M0L6_2atmpS1568 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1581 = _M0Lm2vpS607;
      uint64_t _M0L7vpDiv10S650 = _M0L6_2atmpS1581 / 10ull;
      uint64_t _M0L6_2atmpS1580 = _M0Lm2vmS608;
      uint64_t _M0L7vmDiv10S651 = _M0L6_2atmpS1580 / 10ull;
      uint64_t _M0L6_2atmpS1579;
      uint64_t _M0L7vrDiv10S653;
      uint64_t _M0L6_2atmpS1578;
      int32_t _M0L6_2atmpS1575;
      int32_t _M0L6_2atmpS1577;
      int32_t _M0L6_2atmpS1576;
      int32_t _M0L7vrMod10S654;
      int32_t _M0L6_2atmpS1574;
      if (_M0L7vpDiv10S650 <= _M0L7vmDiv10S651) {
        break;
      }
      _M0L6_2atmpS1579 = _M0Lm2vrS606;
      _M0L7vrDiv10S653 = _M0L6_2atmpS1579 / 10ull;
      _M0L6_2atmpS1578 = _M0Lm2vrS606;
      _M0L6_2atmpS1575 = (int32_t)_M0L6_2atmpS1578;
      _M0L6_2atmpS1577 = (int32_t)_M0L7vrDiv10S653;
      _M0L6_2atmpS1576 = 10 * _M0L6_2atmpS1577;
      _M0L7vrMod10S654 = _M0L6_2atmpS1575 - _M0L6_2atmpS1576;
      _M0Lm7roundUpS645 = _M0L7vrMod10S654 >= 5;
      _M0Lm2vrS606 = _M0L7vrDiv10S653;
      _M0Lm2vpS607 = _M0L7vpDiv10S650;
      _M0Lm2vmS608 = _M0L7vmDiv10S651;
      _M0L6_2atmpS1574 = _M0Lm7removedS630;
      _M0Lm7removedS630 = _M0L6_2atmpS1574 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1582 = _M0Lm2vrS606;
    _M0L6_2atmpS1585 = _M0Lm2vrS606;
    _M0L6_2atmpS1586 = _M0Lm2vmS608;
    _M0L6_2atmpS1584
    = _M0L6_2atmpS1585 == _M0L6_2atmpS1586 || _M0Lm7roundUpS645;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1583 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1584);
    _M0Lm6outputS632 = _M0L6_2atmpS1582 + _M0L6_2atmpS1583;
  }
  _M0L6_2atmpS1590 = _M0Lm3e10S609;
  _M0L6_2atmpS1591 = _M0Lm7removedS630;
  _M0L3expS655 = _M0L6_2atmpS1590 + _M0L6_2atmpS1591;
  _M0L6_2atmpS1589 = _M0Lm6outputS632;
  _block_1971
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_1971)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1971->$0 = _M0L6_2atmpS1589;
  _block_1971->$1 = _M0L3expS655;
  return _block_1971;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS598) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS598) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS597) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS597) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS596) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS596) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS595) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS595 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS595 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS595 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS595 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS595 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS595 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS595 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS595 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS595 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS595 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS595 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS595 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS595 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS595 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS595 >= 100ull) {
    return 3;
  }
  if (_M0L1vS595 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS578) {
  int32_t _M0L6_2atmpS1490;
  int32_t _M0L6_2atmpS1489;
  int32_t _M0L4baseS577;
  int32_t _M0L5base2S579;
  int32_t _M0L6offsetS580;
  int32_t _M0L6_2atmpS1488;
  uint64_t _M0L4mul0S581;
  int32_t _M0L6_2atmpS1487;
  int32_t _M0L6_2atmpS1486;
  uint64_t _M0L4mul1S582;
  uint64_t _M0L1mS583;
  struct _M0TPB7Umul128 _M0L7_2abindS584;
  uint64_t _M0L7_2alow1S585;
  uint64_t _M0L8_2ahigh1S586;
  struct _M0TPB7Umul128 _M0L7_2abindS587;
  uint64_t _M0L7_2alow0S588;
  uint64_t _M0L8_2ahigh0S589;
  uint64_t _M0L3sumS590;
  uint64_t _M0Lm5high1S591;
  int32_t _M0L6_2atmpS1484;
  int32_t _M0L6_2atmpS1485;
  int32_t _M0L5deltaS592;
  uint64_t _M0L6_2atmpS1483;
  uint64_t _M0L6_2atmpS1475;
  int32_t _M0L6_2atmpS1482;
  uint32_t _M0L6_2atmpS1479;
  int32_t _M0L6_2atmpS1481;
  int32_t _M0L6_2atmpS1480;
  uint32_t _M0L6_2atmpS1478;
  uint32_t _M0L6_2atmpS1477;
  uint64_t _M0L6_2atmpS1476;
  uint64_t _M0L1aS593;
  uint64_t _M0L6_2atmpS1474;
  uint64_t _M0L1bS594;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1490 = _M0L1iS578 + 26;
  _M0L6_2atmpS1489 = _M0L6_2atmpS1490 - 1;
  _M0L4baseS577 = _M0L6_2atmpS1489 / 26;
  _M0L5base2S579 = _M0L4baseS577 * 26;
  _M0L6offsetS580 = _M0L5base2S579 - _M0L1iS578;
  _M0L6_2atmpS1488 = _M0L4baseS577 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S581
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1488);
  _M0L6_2atmpS1487 = _M0L4baseS577 * 2;
  _M0L6_2atmpS1486 = _M0L6_2atmpS1487 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S582
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1486);
  if (_M0L6offsetS580 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S581, .$1 = _M0L4mul1S582};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS583
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS580);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS584 = _M0FPB7umul128(_M0L1mS583, _M0L4mul1S582);
  _M0L7_2alow1S585 = _M0L7_2abindS584.$0;
  _M0L8_2ahigh1S586 = _M0L7_2abindS584.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS587 = _M0FPB7umul128(_M0L1mS583, _M0L4mul0S581);
  _M0L7_2alow0S588 = _M0L7_2abindS587.$0;
  _M0L8_2ahigh0S589 = _M0L7_2abindS587.$1;
  _M0L3sumS590 = _M0L8_2ahigh0S589 + _M0L7_2alow1S585;
  _M0Lm5high1S591 = _M0L8_2ahigh1S586;
  if (_M0L3sumS590 < _M0L8_2ahigh0S589) {
    uint64_t _M0L6_2atmpS1473 = _M0Lm5high1S591;
    _M0Lm5high1S591 = _M0L6_2atmpS1473 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1484 = _M0FPB8pow5bits(_M0L5base2S579);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1485 = _M0FPB8pow5bits(_M0L1iS578);
  _M0L5deltaS592 = _M0L6_2atmpS1484 - _M0L6_2atmpS1485;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1483
  = _M0FPB13shiftright128(_M0L7_2alow0S588, _M0L3sumS590, _M0L5deltaS592);
  _M0L6_2atmpS1475 = _M0L6_2atmpS1483 + 1ull;
  _M0L6_2atmpS1482 = _M0L1iS578 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1479
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1482);
  _M0L6_2atmpS1481 = _M0L1iS578 % 16;
  _M0L6_2atmpS1480 = _M0L6_2atmpS1481 << 1;
  _M0L6_2atmpS1478 = _M0L6_2atmpS1479 >> (_M0L6_2atmpS1480 & 31);
  _M0L6_2atmpS1477 = _M0L6_2atmpS1478 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1476 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1477);
  _M0L1aS593 = _M0L6_2atmpS1475 + _M0L6_2atmpS1476;
  _M0L6_2atmpS1474 = _M0Lm5high1S591;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS594
  = _M0FPB13shiftright128(_M0L3sumS590, _M0L6_2atmpS1474, _M0L5deltaS592);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS593, .$1 = _M0L1bS594};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS560) {
  int32_t _M0L4baseS559;
  int32_t _M0L5base2S561;
  int32_t _M0L6offsetS562;
  int32_t _M0L6_2atmpS1472;
  uint64_t _M0L4mul0S563;
  int32_t _M0L6_2atmpS1471;
  int32_t _M0L6_2atmpS1470;
  uint64_t _M0L4mul1S564;
  uint64_t _M0L1mS565;
  struct _M0TPB7Umul128 _M0L7_2abindS566;
  uint64_t _M0L7_2alow1S567;
  uint64_t _M0L8_2ahigh1S568;
  struct _M0TPB7Umul128 _M0L7_2abindS569;
  uint64_t _M0L7_2alow0S570;
  uint64_t _M0L8_2ahigh0S571;
  uint64_t _M0L3sumS572;
  uint64_t _M0Lm5high1S573;
  int32_t _M0L6_2atmpS1468;
  int32_t _M0L6_2atmpS1469;
  int32_t _M0L5deltaS574;
  uint64_t _M0L6_2atmpS1460;
  int32_t _M0L6_2atmpS1467;
  uint32_t _M0L6_2atmpS1464;
  int32_t _M0L6_2atmpS1466;
  int32_t _M0L6_2atmpS1465;
  uint32_t _M0L6_2atmpS1463;
  uint32_t _M0L6_2atmpS1462;
  uint64_t _M0L6_2atmpS1461;
  uint64_t _M0L1aS575;
  uint64_t _M0L6_2atmpS1459;
  uint64_t _M0L1bS576;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS559 = _M0L1iS560 / 26;
  _M0L5base2S561 = _M0L4baseS559 * 26;
  _M0L6offsetS562 = _M0L1iS560 - _M0L5base2S561;
  _M0L6_2atmpS1472 = _M0L4baseS559 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S563
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1472);
  _M0L6_2atmpS1471 = _M0L4baseS559 * 2;
  _M0L6_2atmpS1470 = _M0L6_2atmpS1471 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S564
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1470);
  if (_M0L6offsetS562 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S563, .$1 = _M0L4mul1S564};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS565
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS562);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS566 = _M0FPB7umul128(_M0L1mS565, _M0L4mul1S564);
  _M0L7_2alow1S567 = _M0L7_2abindS566.$0;
  _M0L8_2ahigh1S568 = _M0L7_2abindS566.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS569 = _M0FPB7umul128(_M0L1mS565, _M0L4mul0S563);
  _M0L7_2alow0S570 = _M0L7_2abindS569.$0;
  _M0L8_2ahigh0S571 = _M0L7_2abindS569.$1;
  _M0L3sumS572 = _M0L8_2ahigh0S571 + _M0L7_2alow1S567;
  _M0Lm5high1S573 = _M0L8_2ahigh1S568;
  if (_M0L3sumS572 < _M0L8_2ahigh0S571) {
    uint64_t _M0L6_2atmpS1458 = _M0Lm5high1S573;
    _M0Lm5high1S573 = _M0L6_2atmpS1458 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1468 = _M0FPB8pow5bits(_M0L1iS560);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1469 = _M0FPB8pow5bits(_M0L5base2S561);
  _M0L5deltaS574 = _M0L6_2atmpS1468 - _M0L6_2atmpS1469;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1460
  = _M0FPB13shiftright128(_M0L7_2alow0S570, _M0L3sumS572, _M0L5deltaS574);
  _M0L6_2atmpS1467 = _M0L1iS560 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1464
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1467);
  _M0L6_2atmpS1466 = _M0L1iS560 % 16;
  _M0L6_2atmpS1465 = _M0L6_2atmpS1466 << 1;
  _M0L6_2atmpS1463 = _M0L6_2atmpS1464 >> (_M0L6_2atmpS1465 & 31);
  _M0L6_2atmpS1462 = _M0L6_2atmpS1463 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1461 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1462);
  _M0L1aS575 = _M0L6_2atmpS1460 + _M0L6_2atmpS1461;
  _M0L6_2atmpS1459 = _M0Lm5high1S573;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS576
  = _M0FPB13shiftright128(_M0L3sumS572, _M0L6_2atmpS1459, _M0L5deltaS574);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS575, .$1 = _M0L1bS576};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS533,
  struct _M0TPB8Pow5Pair _M0L3mulS530,
  int32_t _M0L1jS546,
  int32_t _M0L7mmShiftS548
) {
  uint64_t _M0L7_2amul0S529;
  uint64_t _M0L7_2amul1S531;
  uint64_t _M0L1mS532;
  struct _M0TPB7Umul128 _M0L7_2abindS534;
  uint64_t _M0L5_2aloS535;
  uint64_t _M0L6_2atmpS536;
  struct _M0TPB7Umul128 _M0L7_2abindS537;
  uint64_t _M0L6_2alo2S538;
  uint64_t _M0L6_2ahi2S539;
  uint64_t _M0L3midS540;
  uint64_t _M0L6_2atmpS1457;
  uint64_t _M0L2hiS541;
  uint64_t _M0L3lo2S542;
  uint64_t _M0L6_2atmpS1455;
  uint64_t _M0L6_2atmpS1456;
  uint64_t _M0L4mid2S543;
  uint64_t _M0L6_2atmpS1454;
  uint64_t _M0L3hi2S544;
  int32_t _M0L6_2atmpS1453;
  int32_t _M0L6_2atmpS1452;
  uint64_t _M0L2vpS545;
  uint64_t _M0Lm2vmS547;
  int32_t _M0L6_2atmpS1451;
  int32_t _M0L6_2atmpS1450;
  uint64_t _M0L2vrS558;
  uint64_t _M0L6_2atmpS1449;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S529 = _M0L3mulS530.$0;
  _M0L7_2amul1S531 = _M0L3mulS530.$1;
  _M0L1mS532 = _M0L1mS533 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS534 = _M0FPB7umul128(_M0L1mS532, _M0L7_2amul0S529);
  _M0L5_2aloS535 = _M0L7_2abindS534.$0;
  _M0L6_2atmpS536 = _M0L7_2abindS534.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS537 = _M0FPB7umul128(_M0L1mS532, _M0L7_2amul1S531);
  _M0L6_2alo2S538 = _M0L7_2abindS537.$0;
  _M0L6_2ahi2S539 = _M0L7_2abindS537.$1;
  _M0L3midS540 = _M0L6_2atmpS536 + _M0L6_2alo2S538;
  if (_M0L3midS540 < _M0L6_2atmpS536) {
    _M0L6_2atmpS1457 = 1ull;
  } else {
    _M0L6_2atmpS1457 = 0ull;
  }
  _M0L2hiS541 = _M0L6_2ahi2S539 + _M0L6_2atmpS1457;
  _M0L3lo2S542 = _M0L5_2aloS535 + _M0L7_2amul0S529;
  _M0L6_2atmpS1455 = _M0L3midS540 + _M0L7_2amul1S531;
  if (_M0L3lo2S542 < _M0L5_2aloS535) {
    _M0L6_2atmpS1456 = 1ull;
  } else {
    _M0L6_2atmpS1456 = 0ull;
  }
  _M0L4mid2S543 = _M0L6_2atmpS1455 + _M0L6_2atmpS1456;
  if (_M0L4mid2S543 < _M0L3midS540) {
    _M0L6_2atmpS1454 = 1ull;
  } else {
    _M0L6_2atmpS1454 = 0ull;
  }
  _M0L3hi2S544 = _M0L2hiS541 + _M0L6_2atmpS1454;
  _M0L6_2atmpS1453 = _M0L1jS546 - 64;
  _M0L6_2atmpS1452 = _M0L6_2atmpS1453 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS545
  = _M0FPB13shiftright128(_M0L4mid2S543, _M0L3hi2S544, _M0L6_2atmpS1452);
  _M0Lm2vmS547 = 0ull;
  if (_M0L7mmShiftS548) {
    uint64_t _M0L3lo3S549 = _M0L5_2aloS535 - _M0L7_2amul0S529;
    uint64_t _M0L6_2atmpS1439 = _M0L3midS540 - _M0L7_2amul1S531;
    uint64_t _M0L6_2atmpS1440;
    uint64_t _M0L4mid3S550;
    uint64_t _M0L6_2atmpS1438;
    uint64_t _M0L3hi3S551;
    int32_t _M0L6_2atmpS1437;
    int32_t _M0L6_2atmpS1436;
    if (_M0L5_2aloS535 < _M0L3lo3S549) {
      _M0L6_2atmpS1440 = 1ull;
    } else {
      _M0L6_2atmpS1440 = 0ull;
    }
    _M0L4mid3S550 = _M0L6_2atmpS1439 - _M0L6_2atmpS1440;
    if (_M0L3midS540 < _M0L4mid3S550) {
      _M0L6_2atmpS1438 = 1ull;
    } else {
      _M0L6_2atmpS1438 = 0ull;
    }
    _M0L3hi3S551 = _M0L2hiS541 - _M0L6_2atmpS1438;
    _M0L6_2atmpS1437 = _M0L1jS546 - 64;
    _M0L6_2atmpS1436 = _M0L6_2atmpS1437 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS547
    = _M0FPB13shiftright128(_M0L4mid3S550, _M0L3hi3S551, _M0L6_2atmpS1436);
  } else {
    uint64_t _M0L3lo3S552 = _M0L5_2aloS535 + _M0L5_2aloS535;
    uint64_t _M0L6_2atmpS1447 = _M0L3midS540 + _M0L3midS540;
    uint64_t _M0L6_2atmpS1448;
    uint64_t _M0L4mid3S553;
    uint64_t _M0L6_2atmpS1445;
    uint64_t _M0L6_2atmpS1446;
    uint64_t _M0L3hi3S554;
    uint64_t _M0L3lo4S555;
    uint64_t _M0L6_2atmpS1443;
    uint64_t _M0L6_2atmpS1444;
    uint64_t _M0L4mid4S556;
    uint64_t _M0L6_2atmpS1442;
    uint64_t _M0L3hi4S557;
    int32_t _M0L6_2atmpS1441;
    if (_M0L3lo3S552 < _M0L5_2aloS535) {
      _M0L6_2atmpS1448 = 1ull;
    } else {
      _M0L6_2atmpS1448 = 0ull;
    }
    _M0L4mid3S553 = _M0L6_2atmpS1447 + _M0L6_2atmpS1448;
    _M0L6_2atmpS1445 = _M0L2hiS541 + _M0L2hiS541;
    if (_M0L4mid3S553 < _M0L3midS540) {
      _M0L6_2atmpS1446 = 1ull;
    } else {
      _M0L6_2atmpS1446 = 0ull;
    }
    _M0L3hi3S554 = _M0L6_2atmpS1445 + _M0L6_2atmpS1446;
    _M0L3lo4S555 = _M0L3lo3S552 - _M0L7_2amul0S529;
    _M0L6_2atmpS1443 = _M0L4mid3S553 - _M0L7_2amul1S531;
    if (_M0L3lo3S552 < _M0L3lo4S555) {
      _M0L6_2atmpS1444 = 1ull;
    } else {
      _M0L6_2atmpS1444 = 0ull;
    }
    _M0L4mid4S556 = _M0L6_2atmpS1443 - _M0L6_2atmpS1444;
    if (_M0L4mid3S553 < _M0L4mid4S556) {
      _M0L6_2atmpS1442 = 1ull;
    } else {
      _M0L6_2atmpS1442 = 0ull;
    }
    _M0L3hi4S557 = _M0L3hi3S554 - _M0L6_2atmpS1442;
    _M0L6_2atmpS1441 = _M0L1jS546 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS547
    = _M0FPB13shiftright128(_M0L4mid4S556, _M0L3hi4S557, _M0L6_2atmpS1441);
  }
  _M0L6_2atmpS1451 = _M0L1jS546 - 64;
  _M0L6_2atmpS1450 = _M0L6_2atmpS1451 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS558
  = _M0FPB13shiftright128(_M0L3midS540, _M0L2hiS541, _M0L6_2atmpS1450);
  _M0L6_2atmpS1449 = _M0Lm2vmS547;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS558,
                                                .$1 = _M0L2vpS545,
                                                .$2 = _M0L6_2atmpS1449};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS527,
  int32_t _M0L1pS528
) {
  uint64_t _M0L6_2atmpS1435;
  uint64_t _M0L6_2atmpS1434;
  uint64_t _M0L6_2atmpS1433;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1435 = 1ull << (_M0L1pS528 & 63);
  _M0L6_2atmpS1434 = _M0L6_2atmpS1435 - 1ull;
  _M0L6_2atmpS1433 = _M0L5valueS527 & _M0L6_2atmpS1434;
  return _M0L6_2atmpS1433 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS525,
  int32_t _M0L1pS526
) {
  int32_t _M0L6_2atmpS1432;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1432 = _M0FPB10pow5Factor(_M0L5valueS525);
  return _M0L6_2atmpS1432 >= _M0L1pS526;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS520) {
  uint64_t _M0L6_2atmpS1423;
  uint64_t _M0L6_2atmpS1424;
  uint64_t _M0L6_2atmpS1425;
  uint64_t _M0L6_2atmpS1426;
  uint64_t _M0L6_2atmpS1431;
  int32_t _M0L5countS521;
  uint64_t _M0L1vS522;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1423 = _M0L5valueS520 % 5ull;
  if (_M0L6_2atmpS1423 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1424 = _M0L5valueS520 % 25ull;
  if (_M0L6_2atmpS1424 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1425 = _M0L5valueS520 % 125ull;
  if (_M0L6_2atmpS1425 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1426 = _M0L5valueS520 % 625ull;
  if (_M0L6_2atmpS1426 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1431 = _M0L5valueS520 / 625ull;
  _M0L5countS521 = 4;
  _M0L1vS522 = _M0L6_2atmpS1431;
  while (1) {
    if (_M0L1vS522 > 0ull) {
      uint64_t _M0L6_2atmpS1427 = _M0L1vS522 % 5ull;
      int32_t _M0L6_2atmpS1428;
      uint64_t _M0L6_2atmpS1429;
      if (_M0L6_2atmpS1427 != 0ull) {
        return _M0L5countS521;
      }
      _M0L6_2atmpS1428 = _M0L5countS521 + 1;
      _M0L6_2atmpS1429 = _M0L1vS522 / 5ull;
      _M0L5countS521 = _M0L6_2atmpS1428;
      _M0L1vS522 = _M0L6_2atmpS1429;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS524;
      moonbit_string_t _M0L6_2atmpS1430;
      int32_t _result_1973;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS524
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS524, (moonbit_string_t)moonbit_string_literal_12.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS524, _M0L5valueS520);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1430
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS524);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS524);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_1973 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1430);
      moonbit_decref_cycle_free(_M0L6_2atmpS1430);
      return _result_1973;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS519,
  uint64_t _M0L2hiS517,
  int32_t _M0L4distS518
) {
  int32_t _M0L6_2atmpS1422;
  uint64_t _M0L6_2atmpS1420;
  uint64_t _M0L6_2atmpS1421;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1422 = 64 - _M0L4distS518;
  _M0L6_2atmpS1420 = _M0L2hiS517 << (_M0L6_2atmpS1422 & 63);
  _M0L6_2atmpS1421 = _M0L2loS519 >> (_M0L4distS518 & 63);
  return _M0L6_2atmpS1420 | _M0L6_2atmpS1421;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS507,
  uint64_t _M0L1bS510
) {
  uint64_t _M0L3aLoS506;
  uint64_t _M0L3aHiS508;
  uint64_t _M0L3bLoS509;
  uint64_t _M0L3bHiS511;
  uint64_t _M0L1xS512;
  uint64_t _M0L6_2atmpS1418;
  uint64_t _M0L6_2atmpS1419;
  uint64_t _M0L1yS513;
  uint64_t _M0L6_2atmpS1416;
  uint64_t _M0L6_2atmpS1417;
  uint64_t _M0L1zS514;
  uint64_t _M0L6_2atmpS1414;
  uint64_t _M0L6_2atmpS1415;
  uint64_t _M0L6_2atmpS1412;
  uint64_t _M0L6_2atmpS1413;
  uint64_t _M0L1wS515;
  uint64_t _M0L2loS516;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS506 = _M0L1aS507 & 4294967295ull;
  _M0L3aHiS508 = _M0L1aS507 >> 32;
  _M0L3bLoS509 = _M0L1bS510 & 4294967295ull;
  _M0L3bHiS511 = _M0L1bS510 >> 32;
  _M0L1xS512 = _M0L3aLoS506 * _M0L3bLoS509;
  _M0L6_2atmpS1418 = _M0L3aHiS508 * _M0L3bLoS509;
  _M0L6_2atmpS1419 = _M0L1xS512 >> 32;
  _M0L1yS513 = _M0L6_2atmpS1418 + _M0L6_2atmpS1419;
  _M0L6_2atmpS1416 = _M0L3aLoS506 * _M0L3bHiS511;
  _M0L6_2atmpS1417 = _M0L1yS513 & 4294967295ull;
  _M0L1zS514 = _M0L6_2atmpS1416 + _M0L6_2atmpS1417;
  _M0L6_2atmpS1414 = _M0L3aHiS508 * _M0L3bHiS511;
  _M0L6_2atmpS1415 = _M0L1yS513 >> 32;
  _M0L6_2atmpS1412 = _M0L6_2atmpS1414 + _M0L6_2atmpS1415;
  _M0L6_2atmpS1413 = _M0L1zS514 >> 32;
  _M0L1wS515 = _M0L6_2atmpS1412 + _M0L6_2atmpS1413;
  _M0L2loS516 = _M0L1aS507 * _M0L1bS510;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS516, .$1 = _M0L1wS515};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS504,
  int32_t _M0L4fromS501,
  int32_t _M0L2toS500
) {
  int32_t _M0L3lenS499;
  int32_t _M0L6_2atmpS1411;
  uint16_t* _M0L6bufferS502;
  int32_t _M0L1iS503;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS499 = _M0L2toS500 - _M0L4fromS501;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1411 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS502
  = (uint16_t*)moonbit_make_string(_M0L3lenS499, _M0L6_2atmpS1411);
  _M0L1iS503 = 0;
  while (1) {
    if (_M0L1iS503 < _M0L3lenS499) {
      int32_t _M0L6_2atmpS1409 = _M0L4fromS501 + _M0L1iS503;
      int32_t _M0L6_2atmpS1408;
      int32_t _M0L6_2atmpS1407;
      int32_t _M0L6_2atmpS1410;
      if (
        _M0L6_2atmpS1409 < 0
        || _M0L6_2atmpS1409 >= Moonbit_array_length(_M0L5bytesS504)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1408 = (int32_t)_M0L5bytesS504[_M0L6_2atmpS1409];
      _M0L6_2atmpS1407 = (uint16_t)_M0L6_2atmpS1408;
      if (
        _M0L1iS503 < 0 || _M0L1iS503 >= Moonbit_array_length(_M0L6bufferS502)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS502[_M0L1iS503] = _M0L6_2atmpS1407;
      _M0L6_2atmpS1410 = _M0L1iS503 + 1;
      _M0L1iS503 = _M0L6_2atmpS1410;
      continue;
    }
    break;
  }
  return _M0L6bufferS502;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS498) {
  int32_t _M0L6_2atmpS1406;
  uint32_t _M0L6_2atmpS1405;
  uint32_t _M0L6_2atmpS1404;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1406 = _M0L1eS498 * 78913;
  _M0L6_2atmpS1405 = *(uint32_t*)&_M0L6_2atmpS1406;
  _M0L6_2atmpS1404 = _M0L6_2atmpS1405 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1404;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS497) {
  int32_t _M0L6_2atmpS1403;
  uint32_t _M0L6_2atmpS1402;
  uint32_t _M0L6_2atmpS1401;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1403 = _M0L1eS497 * 732923;
  _M0L6_2atmpS1402 = *(uint32_t*)&_M0L6_2atmpS1403;
  _M0L6_2atmpS1401 = _M0L6_2atmpS1402 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1401;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS495,
  int32_t _M0L8exponentS496,
  int32_t _M0L8mantissaS493
) {
  moonbit_string_t _M0L1sS494;
  moonbit_string_t _result_1976;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS493) {
    return (moonbit_string_t)moonbit_string_literal_13.data;
  }
  if (_M0L4signS495) {
    _M0L1sS494 = (moonbit_string_t)moonbit_string_literal_14.data;
  } else {
    _M0L1sS494 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS496) {
    moonbit_string_t _result_1975;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1975
    = moonbit_add_string(_M0L1sS494, (moonbit_string_t)moonbit_string_literal_15.data);
    moonbit_decref_cycle_free(_M0L1sS494);
    return _result_1975;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1976
  = moonbit_add_string(_M0L1sS494, (moonbit_string_t)moonbit_string_literal_16.data);
  moonbit_decref_cycle_free(_M0L1sS494);
  return _result_1976;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS492) {
  int32_t _M0L6_2atmpS1400;
  uint32_t _M0L6_2atmpS1399;
  uint32_t _M0L6_2atmpS1398;
  int32_t _M0L6_2atmpS1397;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1400 = _M0L1eS492 * 1217359;
  _M0L6_2atmpS1399 = *(uint32_t*)&_M0L6_2atmpS1400;
  _M0L6_2atmpS1398 = _M0L6_2atmpS1399 >> 19;
  _M0L6_2atmpS1397 = *(int32_t*)&_M0L6_2atmpS1398;
  return _M0L6_2atmpS1397 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS491) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS491 != _M0L4selfS491) {
    return 0;
  } else if (_M0L4selfS491 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS491 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS491;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS490) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS490 != _M0L4selfS490) {
    return 0ll;
  } else if (_M0L4selfS490 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS490 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS490;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS489
) {
  float* _M0L6_2atmpS1396;
  struct _M0TPB5ArrayGfE* _block_1977;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1396 = (float*)moonbit_make_float_array_raw(_M0L3lenS489);
  _block_1977
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_1977)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _block_1977->$0 = _M0L6_2atmpS1396;
  _block_1977->$1 = _M0L3lenS489;
  return _block_1977;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS485,
  int32_t _M0L5indexS486
) {
  uint64_t* _M0L6_2atmpS1394;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1394 = _M0L4selfS485;
  if (
    _M0L5indexS486 < 0
    || _M0L5indexS486 >= Moonbit_array_length(_M0L6_2atmpS1394)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1394[_M0L5indexS486];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS487,
  int32_t _M0L5indexS488
) {
  uint32_t* _M0L6_2atmpS1395;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1395 = _M0L4selfS487;
  if (
    _M0L5indexS488 < 0
    || _M0L5indexS488 >= Moonbit_array_length(_M0L6_2atmpS1395)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1395[_M0L5indexS488];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS484
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS484, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS483) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS483, 10);
}

moonbit_string_t _M0IPC14bool4BoolPB4Show10to__string(int32_t _M0L4selfS482) {
  #line 26 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L4selfS482) {
    return (moonbit_string_t)moonbit_string_literal_17.data;
  } else {
    return (moonbit_string_t)moonbit_string_literal_18.data;
  }
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS481) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS481;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS472,
  int32_t _M0L5valueS474
) {
  int32_t _M0L3lenS1373;
  int32_t* _M0L6_2atmpS1375;
  int32_t _M0L6_2atmpS1374;
  int32_t _M0L6lengthS473;
  int32_t* _M0L3bufS1378;
  int32_t _M0L6_2atmpS1379;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1373 = _M0L4selfS472->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1375 = _M0MPC15array5Array6bufferGiE(_M0L4selfS472);
  _M0L6_2atmpS1374 = Moonbit_array_length(_M0L6_2atmpS1375);
  moonbit_decref_cycle_free(_M0L6_2atmpS1375);
  if (_M0L3lenS1373 == _M0L6_2atmpS1374) {
    int32_t _M0L3lenS1377 = _M0L4selfS472->$1;
    int32_t _M0L6_2atmpS1376 = _M0L3lenS1377 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS472, _M0L6_2atmpS1376);
  }
  _M0L6lengthS473 = _M0L4selfS472->$1;
  _M0L3bufS1378 = _M0L4selfS472->$0;
  _M0L3bufS1378[_M0L6lengthS473] = _M0L5valueS474;
  _M0L6_2atmpS1379 = _M0L6lengthS473 + 1;
  _M0L4selfS472->$1 = _M0L6_2atmpS1379;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS475,
  moonbit_string_t _M0L5valueS477
) {
  int32_t _M0L3lenS1380;
  moonbit_string_t* _M0L6_2atmpS1382;
  int32_t _M0L6_2atmpS1381;
  int32_t _M0L6lengthS476;
  moonbit_string_t* _M0L3bufS1385;
  moonbit_string_t _M0L6_2aoldS1888;
  int32_t _M0L6_2atmpS1386;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1380 = _M0L4selfS475->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1382 = _M0MPC15array5Array6bufferGsE(_M0L4selfS475);
  _M0L6_2atmpS1381 = Moonbit_array_length(_M0L6_2atmpS1382);
  moonbit_decref_cycle_free(_M0L6_2atmpS1382);
  if (_M0L3lenS1380 == _M0L6_2atmpS1381) {
    int32_t _M0L3lenS1384 = _M0L4selfS475->$1;
    int32_t _M0L6_2atmpS1383 = _M0L3lenS1384 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS475, _M0L6_2atmpS1383);
  }
  _M0L6lengthS476 = _M0L4selfS475->$1;
  _M0L3bufS1385 = _M0L4selfS475->$0;
  _M0L6_2aoldS1888 = (moonbit_string_t)_M0L3bufS1385[_M0L6lengthS476];
  moonbit_decref_cycle_free(_M0L6_2aoldS1888);
  _M0L3bufS1385[_M0L6lengthS476] = _M0L5valueS477;
  _M0L6_2atmpS1386 = _M0L6lengthS476 + 1;
  _M0L4selfS475->$1 = _M0L6_2atmpS1386;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS478,
  struct _M0TUsiE* _M0L5valueS480
) {
  int32_t _M0L3lenS1387;
  struct _M0TUsiE** _M0L6_2atmpS1389;
  int32_t _M0L6_2atmpS1388;
  int32_t _M0L6lengthS479;
  struct _M0TUsiE** _M0L3bufS1392;
  struct _M0TUsiE* _M0L6_2aoldS1889;
  int32_t _M0L6_2atmpS1393;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1387 = _M0L4selfS478->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1389 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS478);
  _M0L6_2atmpS1388 = Moonbit_array_length(_M0L6_2atmpS1389);
  moonbit_decref_cycle_free(_M0L6_2atmpS1389);
  if (_M0L3lenS1387 == _M0L6_2atmpS1388) {
    int32_t _M0L3lenS1391 = _M0L4selfS478->$1;
    int32_t _M0L6_2atmpS1390 = _M0L3lenS1391 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS478, _M0L6_2atmpS1390);
  }
  _M0L6lengthS479 = _M0L4selfS478->$1;
  _M0L3bufS1392 = _M0L4selfS478->$0;
  _M0L6_2aoldS1889 = (struct _M0TUsiE*)_M0L3bufS1392[_M0L6lengthS479];
  if (_M0L6_2aoldS1889) {
    moonbit_decref_cycle_free(_M0L6_2aoldS1889);
  }
  _M0L3bufS1392[_M0L6lengthS479] = _M0L5valueS480;
  _M0L6_2atmpS1393 = _M0L6lengthS479 + 1;
  _M0L4selfS478->$1 = _M0L6_2atmpS1393;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS461,
  int32_t _M0L8requiredS463
) {
  int32_t _M0L8old__capS460;
  int32_t _M0L3lenS1370;
  int32_t _M0L8new__capS462;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS460 = _M0MPC15array5Array8capacityGiE(_M0L4selfS461);
  _M0L3lenS1370 = _M0L4selfS461->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS462
  = _M0FPB23array__growth__capacity(_M0L8old__capS460, _M0L3lenS1370, _M0L8requiredS463);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS461, _M0L8new__capS462);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS465,
  int32_t _M0L8requiredS467
) {
  int32_t _M0L8old__capS464;
  int32_t _M0L3lenS1371;
  int32_t _M0L8new__capS466;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS464 = _M0MPC15array5Array8capacityGsE(_M0L4selfS465);
  _M0L3lenS1371 = _M0L4selfS465->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS466
  = _M0FPB23array__growth__capacity(_M0L8old__capS464, _M0L3lenS1371, _M0L8requiredS467);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS465, _M0L8new__capS466);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS469,
  int32_t _M0L8requiredS471
) {
  int32_t _M0L8old__capS468;
  int32_t _M0L3lenS1372;
  int32_t _M0L8new__capS470;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS468 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS469);
  _M0L3lenS1372 = _M0L4selfS469->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS470
  = _M0FPB23array__growth__capacity(_M0L8old__capS468, _M0L3lenS1372, _M0L8requiredS471);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS469, _M0L8new__capS470);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS443,
  int32_t _M0L13new__capacityS446
) {
  int32_t* _M0L8old__bufS442;
  int32_t _M0L3lenS444;
  int32_t _M0L9copy__lenS445;
  int32_t* _M0L8new__bufS447;
  int32_t* _M0L6_2aoldS1890;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS442 = _M0L4selfS443->$0;
  _M0L3lenS444 = _M0L4selfS443->$1;
  if (_M0L3lenS444 < _M0L13new__capacityS446) {
    _M0L9copy__lenS445 = _M0L3lenS444;
  } else {
    _M0L9copy__lenS445 = _M0L13new__capacityS446;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS442);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS447
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS442, _M0L13new__capacityS446, _M0L9copy__lenS445, 0, 0);
  _M0L6_2aoldS1890 = _M0L4selfS443->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1890);
  _M0L4selfS443->$0 = _M0L8new__bufS447;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS449,
  int32_t _M0L13new__capacityS452
) {
  moonbit_string_t* _M0L8old__bufS448;
  int32_t _M0L3lenS450;
  int32_t _M0L9copy__lenS451;
  moonbit_string_t* _M0L8new__bufS453;
  moonbit_string_t* _M0L6_2aoldS1891;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS448 = _M0L4selfS449->$0;
  _M0L3lenS450 = _M0L4selfS449->$1;
  if (_M0L3lenS450 < _M0L13new__capacityS452) {
    _M0L9copy__lenS451 = _M0L3lenS450;
  } else {
    _M0L9copy__lenS451 = _M0L13new__capacityS452;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS448);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS453
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS448, _M0L13new__capacityS452, _M0L9copy__lenS451, 0, 0);
  _M0L6_2aoldS1891 = _M0L4selfS449->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1891);
  _M0L4selfS449->$0 = _M0L8new__bufS453;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS455,
  int32_t _M0L13new__capacityS458
) {
  struct _M0TUsiE** _M0L8old__bufS454;
  int32_t _M0L3lenS456;
  int32_t _M0L9copy__lenS457;
  struct _M0TUsiE** _M0L8new__bufS459;
  struct _M0TUsiE** _M0L6_2aoldS1892;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS454 = _M0L4selfS455->$0;
  _M0L3lenS456 = _M0L4selfS455->$1;
  if (_M0L3lenS456 < _M0L13new__capacityS458) {
    _M0L9copy__lenS457 = _M0L3lenS456;
  } else {
    _M0L9copy__lenS457 = _M0L13new__capacityS458;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS454);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS459
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS454, _M0L13new__capacityS458, _M0L9copy__lenS457, 0, 0);
  _M0L6_2aoldS1892 = _M0L4selfS455->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1892);
  _M0L4selfS455->$0 = _M0L8new__bufS459;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS439
) {
  int32_t* _M0L6_2atmpS1367;
  int32_t _result_1978;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1367 = _M0MPC15array5Array6bufferGiE(_M0L4selfS439);
  _result_1978 = Moonbit_array_length(_M0L6_2atmpS1367);
  moonbit_decref_cycle_free(_M0L6_2atmpS1367);
  return _result_1978;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS440
) {
  moonbit_string_t* _M0L6_2atmpS1368;
  int32_t _result_1979;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1368 = _M0MPC15array5Array6bufferGsE(_M0L4selfS440);
  _result_1979 = Moonbit_array_length(_M0L6_2atmpS1368);
  moonbit_decref_cycle_free(_M0L6_2atmpS1368);
  return _result_1979;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS441
) {
  struct _M0TUsiE** _M0L6_2atmpS1369;
  int32_t _result_1980;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1369 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS441);
  _result_1980 = Moonbit_array_length(_M0L6_2atmpS1369);
  moonbit_decref_cycle_free(_M0L6_2atmpS1369);
  return _result_1980;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS435,
  int32_t _M0L3lenS433,
  int32_t _M0L8requiredS432
) {
  int32_t _M0L5startS434;
  int32_t _M0L5spaceS436;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS432 < _M0L3lenS433) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_19.data);
  }
  if (_M0L7currentS435 == 0) {
    _M0L5startS434 = 8;
  } else {
    _M0L5startS434 = _M0L7currentS435;
  }
  _M0L5spaceS436 = _M0L5startS434;
  while (1) {
    if (_M0L5spaceS436 < _M0L8requiredS432) {
      int32_t _M0L4nextS437 = _M0L5spaceS436 * 2;
      if (_M0L4nextS437 <= _M0L5spaceS436) {
        return _M0L8requiredS432;
      }
      _M0L5spaceS436 = _M0L4nextS437;
      continue;
    } else {
      return _M0L5spaceS436;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE* _M0L4selfS429) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS429->$1;
}

int32_t _M0MPC15array5Array6lengthGRP26RiantR8snn__mbt8ReceptorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* _M0L4selfS430
) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS430->$1;
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS431) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS431->$1;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS424) {
  int32_t* _M0L8_2afieldS1893;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1893 = _M0L4selfS424->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1893);
  return _M0L8_2afieldS1893;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS425) {
  float* _M0L8_2afieldS1894;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1894 = _M0L4selfS425->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1894);
  return _M0L8_2afieldS1894;
}

struct _M0TP26RiantR8snn__mbt8Receptor** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt8ReceptorE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt8ReceptorE* _M0L4selfS426
) {
  struct _M0TP26RiantR8snn__mbt8Receptor** _M0L8_2afieldS1895;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1895 = _M0L4selfS426->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1895);
  return _M0L8_2afieldS1895;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS427
) {
  moonbit_string_t* _M0L8_2afieldS1896;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1896 = _M0L4selfS427->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1896);
  return _M0L8_2afieldS1896;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS428
) {
  struct _M0TUsiE** _M0L8_2afieldS1897;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1897 = _M0L4selfS428->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1897);
  return _M0L8_2afieldS1897;
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
  int32_t _M0L3endS1365;
  int32_t _M0L5startS1366;
  int32_t _M0L8str__lenS419;
  int32_t _M0L3lenS1364;
  int32_t _M0L8requiredS421;
  uint16_t* _M0L4dataS1357;
  int32_t _M0L6_2atmpS1356;
  int32_t _if__result_1982;
  uint16_t* _M0L4dataS1358;
  int32_t _M0L3lenS1359;
  moonbit_string_t _M0L6_2atmpS1360;
  int32_t _M0L6_2atmpS1361;
  int32_t _M0L3lenS1363;
  int32_t _M0L6_2atmpS1362;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1365 = _M0L3strS420.$2;
  _M0L5startS1366 = _M0L3strS420.$1;
  _M0L8str__lenS419 = _M0L3endS1365 - _M0L5startS1366;
  if (_M0L8str__lenS419 == 0) {
    return 0;
  }
  _M0L3lenS1364 = _M0L4selfS422->$1;
  _M0L8requiredS421 = _M0L3lenS1364 + _M0L8str__lenS419;
  _M0L4dataS1357 = _M0L4selfS422->$0;
  _M0L6_2atmpS1356 = Moonbit_array_length(_M0L4dataS1357);
  if (_M0L8requiredS421 > _M0L6_2atmpS1356) {
    _if__result_1982 = 1;
  } else {
    int32_t _M0L3lenS1355 = _M0L4selfS422->$1;
    _if__result_1982 = _M0L8requiredS421 < _M0L3lenS1355;
  }
  if (_if__result_1982) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS422, _M0L8requiredS421);
  }
  _M0L4dataS1358 = _M0L4selfS422->$0;
  _M0L3lenS1359 = _M0L4selfS422->$1;
  moonbit_incref_cycle_free(_M0L4dataS1358);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1360 = _M0MPC16string10StringView4data(_M0L3strS420);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1361 = _M0MPC16string10StringView13start__offset(_M0L3strS420);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1358, _M0L3lenS1359, _M0L6_2atmpS1360, _M0L6_2atmpS1361, _M0L8str__lenS419);
  moonbit_decref_cycle_free(_M0L4dataS1358);
  moonbit_decref_cycle_free(_M0L6_2atmpS1360);
  _M0L3lenS1363 = _M0L4selfS422->$1;
  _M0L6_2atmpS1362 = _M0L3lenS1363 + _M0L8str__lenS419;
  _M0L4selfS422->$1 = _M0L6_2atmpS1362;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS416,
  int32_t _M0L5startS414,
  int32_t _M0L3endS415
) {
  int32_t _if__result_1983;
  int32_t _M0L3lenS417;
  int32_t _M0L6_2atmpS1354;
  moonbit_bytes_t _M0L5bytesS418;
  moonbit_bytes_t _M0L6_2atmpS1353;
  moonbit_string_t _result_1984;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS414 == 0) {
    int32_t _M0L6_2atmpS1352 = Moonbit_array_length(_M0L3strS416);
    _if__result_1983 = _M0L3endS415 == _M0L6_2atmpS1352;
  } else {
    _if__result_1983 = 0;
  }
  if (_if__result_1983) {
    moonbit_incref_cycle_free(_M0L3strS416);
    return _M0L3strS416;
  }
  _M0L3lenS417 = _M0L3endS415 - _M0L5startS414;
  _M0L6_2atmpS1354 = _M0L3lenS417 * 2;
  _M0L5bytesS418 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1354, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS418, 0, _M0L3strS416, _M0L5startS414, _M0L3lenS417);
  _M0L6_2atmpS1353 = _M0L5bytesS418;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_1984
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1353, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1353);
  return _result_1984;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS409,
  int32_t _M0L6offsetS413,
  int64_t _M0L6lengthS411
) {
  int32_t _M0L3lenS408;
  int32_t _M0L6lengthS410;
  int32_t _if__result_1985;
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
      int32_t _M0L6_2atmpS1351 = _M0L6offsetS413 + _M0L6lengthS410;
      _if__result_1985 = _M0L6_2atmpS1351 <= _M0L3lenS408;
    } else {
      _if__result_1985 = 0;
    }
  } else {
    _if__result_1985 = 0;
  }
  if (_if__result_1985) {
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
  int32_t _M0L6_2atmpS1350;
  int32_t _M0L6_2atmpS1349;
  int32_t _M0L2e1S394;
  int32_t _M0L6_2atmpS1348;
  int32_t _M0L2e2S397;
  int32_t _M0L4len1S399;
  int32_t _M0L4len2S401;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1350 = _M0L6lengthS396 * 2;
  _M0L6_2atmpS1349 = _M0L13bytes__offsetS395 + _M0L6_2atmpS1350;
  _M0L2e1S394 = _M0L6_2atmpS1349 - 1;
  _M0L6_2atmpS1348 = _M0L11str__offsetS398 + _M0L6lengthS396;
  _M0L2e2S397 = _M0L6_2atmpS1348 - 1;
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
        int32_t _M0L6_2atmpS1345 = _M0L3strS402[_M0L1iS404];
        int32_t _M0L6_2atmpS1344 = (int32_t)_M0L6_2atmpS1345;
        uint32_t _M0L1cS406 = *(uint32_t*)&_M0L6_2atmpS1344;
        uint32_t _M0L6_2atmpS1340 = _M0L1cS406 & 255u;
        int32_t _M0L6_2atmpS1339;
        int32_t _M0L6_2atmpS1341;
        uint32_t _M0L6_2atmpS1343;
        int32_t _M0L6_2atmpS1342;
        int32_t _M0L6_2atmpS1346;
        int32_t _M0L6_2atmpS1347;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1339 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1340);
        if (
          _M0L1jS405 < 0 || _M0L1jS405 >= Moonbit_array_length(_M0L4selfS400)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS400[_M0L1jS405] = _M0L6_2atmpS1339;
        _M0L6_2atmpS1341 = _M0L1jS405 + 1;
        _M0L6_2atmpS1343 = _M0L1cS406 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1342 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1343);
        if (
          _M0L6_2atmpS1341 < 0
          || _M0L6_2atmpS1341 >= Moonbit_array_length(_M0L4selfS400)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS400[_M0L6_2atmpS1341] = _M0L6_2atmpS1342;
        _M0L6_2atmpS1346 = _M0L1iS404 + 1;
        _M0L6_2atmpS1347 = _M0L1jS405 + 2;
        _M0L1iS404 = _M0L6_2atmpS1346;
        _M0L1jS405 = _M0L6_2atmpS1347;
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
  int32_t _M0L6_2atmpS1338;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1338 = *(int32_t*)&_M0L4selfS393;
  return _M0L6_2atmpS1338 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS385,
  int32_t _M0L5radixS384
) {
  uint16_t* _M0L6bufferS386;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS384 < 2 || _M0L5radixS384 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_20.data);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_20.data);
  }
  if (_M0L4selfS368 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  _M0L12is__negativeS369 = _M0L4selfS368 < 0ll;
  if (_M0L12is__negativeS369) {
    int64_t _M0L6_2atmpS1337 = -_M0L4selfS368;
    _M0L3numS370 = *(uint64_t*)&_M0L6_2atmpS1337;
  } else {
    _M0L3numS370 = *(uint64_t*)&_M0L4selfS368;
  }
  switch (_M0L5radixS367) {
    case 10: {
      int32_t _M0L10digit__lenS372;
      int32_t _M0L6_2atmpS1334;
      int32_t _M0L10total__lenS373;
      uint16_t* _M0L6bufferS374;
      int32_t _M0L12digit__startS375;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS372 = _M0FPB12dec__count64(_M0L3numS370);
      if (_M0L12is__negativeS369) {
        _M0L6_2atmpS1334 = 1;
      } else {
        _M0L6_2atmpS1334 = 0;
      }
      _M0L10total__lenS373 = _M0L10digit__lenS372 + _M0L6_2atmpS1334;
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
      int32_t _M0L6_2atmpS1335;
      int32_t _M0L10total__lenS377;
      uint16_t* _M0L6bufferS378;
      int32_t _M0L12digit__startS379;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS376 = _M0FPB12hex__count64(_M0L3numS370);
      if (_M0L12is__negativeS369) {
        _M0L6_2atmpS1335 = 1;
      } else {
        _M0L6_2atmpS1335 = 0;
      }
      _M0L10total__lenS377 = _M0L10digit__lenS376 + _M0L6_2atmpS1335;
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
      int32_t _M0L6_2atmpS1336;
      int32_t _M0L10total__lenS381;
      uint16_t* _M0L6bufferS382;
      int32_t _M0L12digit__startS383;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS380
      = _M0FPB14radix__count64(_M0L3numS370, _M0L5radixS367);
      if (_M0L12is__negativeS369) {
        _M0L6_2atmpS1336 = 1;
      } else {
        _M0L6_2atmpS1336 = 0;
      }
      _M0L10total__lenS381 = _M0L10digit__lenS380 + _M0L6_2atmpS1336;
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
  int32_t _M0L6_2atmpS1333;
  uint64_t _M0L3numS343;
  int32_t _M0L6offsetS344;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1333 = _M0L10total__lenS366 - _M0L12digit__startS354;
  _M0L3numS343 = _M0L3numS365;
  _M0L6offsetS344 = _M0L6_2atmpS1333;
  while (1) {
    if (_M0L3numS343 >= 10000ull) {
      uint64_t _M0L1tS345 = _M0L3numS343 / 10000ull;
      uint64_t _M0L6_2atmpS1310 = _M0L3numS343 % 10000ull;
      int32_t _M0L1rS346 = (int32_t)_M0L6_2atmpS1310;
      int32_t _M0L2d1S347 = _M0L1rS346 / 100;
      int32_t _M0L2d2S348 = _M0L1rS346 % 100;
      int32_t _M0L6_2atmpS1309 = _M0L2d1S347 / 10;
      int32_t _M0L6_2atmpS1308 = 48 + _M0L6_2atmpS1309;
      int32_t _M0L6d1__hiS349 = (uint16_t)_M0L6_2atmpS1308;
      int32_t _M0L6_2atmpS1307 = _M0L2d1S347 % 10;
      int32_t _M0L6_2atmpS1306 = 48 + _M0L6_2atmpS1307;
      int32_t _M0L6d1__loS350 = (uint16_t)_M0L6_2atmpS1306;
      int32_t _M0L6_2atmpS1305 = _M0L2d2S348 / 10;
      int32_t _M0L6_2atmpS1304 = 48 + _M0L6_2atmpS1305;
      int32_t _M0L6d2__hiS351 = (uint16_t)_M0L6_2atmpS1304;
      int32_t _M0L6_2atmpS1303 = _M0L2d2S348 % 10;
      int32_t _M0L6_2atmpS1302 = 48 + _M0L6_2atmpS1303;
      int32_t _M0L6d2__loS352 = (uint16_t)_M0L6_2atmpS1302;
      int32_t _M0L6_2atmpS1294 = _M0L12digit__startS354 + _M0L6offsetS344;
      int32_t _M0L6_2atmpS1293 = _M0L6_2atmpS1294 - 4;
      int32_t _M0L6_2atmpS1296;
      int32_t _M0L6_2atmpS1295;
      int32_t _M0L6_2atmpS1298;
      int32_t _M0L6_2atmpS1297;
      int32_t _M0L6_2atmpS1300;
      int32_t _M0L6_2atmpS1299;
      int32_t _M0L6_2atmpS1301;
      _M0L6bufferS353[_M0L6_2atmpS1293] = _M0L6d1__hiS349;
      _M0L6_2atmpS1296 = _M0L12digit__startS354 + _M0L6offsetS344;
      _M0L6_2atmpS1295 = _M0L6_2atmpS1296 - 3;
      _M0L6bufferS353[_M0L6_2atmpS1295] = _M0L6d1__loS350;
      _M0L6_2atmpS1298 = _M0L12digit__startS354 + _M0L6offsetS344;
      _M0L6_2atmpS1297 = _M0L6_2atmpS1298 - 2;
      _M0L6bufferS353[_M0L6_2atmpS1297] = _M0L6d2__hiS351;
      _M0L6_2atmpS1300 = _M0L12digit__startS354 + _M0L6offsetS344;
      _M0L6_2atmpS1299 = _M0L6_2atmpS1300 - 1;
      _M0L6bufferS353[_M0L6_2atmpS1299] = _M0L6d2__loS352;
      _M0L6_2atmpS1301 = _M0L6offsetS344 - 4;
      _M0L3numS343 = _M0L1tS345;
      _M0L6offsetS344 = _M0L6_2atmpS1301;
      continue;
    } else {
      int32_t _M0L6_2atmpS1332 = (int32_t)_M0L3numS343;
      int32_t _M0L9remainingS356 = _M0L6_2atmpS1332;
      int32_t _M0L6offsetS357 = _M0L6offsetS344;
      while (1) {
        if (_M0L9remainingS356 >= 100) {
          int32_t _M0L1tS358 = _M0L9remainingS356 / 100;
          int32_t _M0L1dS359 = _M0L9remainingS356 % 100;
          int32_t _M0L6_2atmpS1319 = _M0L1dS359 / 10;
          int32_t _M0L6_2atmpS1318 = 48 + _M0L6_2atmpS1319;
          int32_t _M0L5d__hiS360 = (uint16_t)_M0L6_2atmpS1318;
          int32_t _M0L6_2atmpS1317 = _M0L1dS359 % 10;
          int32_t _M0L6_2atmpS1316 = 48 + _M0L6_2atmpS1317;
          int32_t _M0L5d__loS361 = (uint16_t)_M0L6_2atmpS1316;
          int32_t _M0L6_2atmpS1312 = _M0L12digit__startS354 + _M0L6offsetS357;
          int32_t _M0L6_2atmpS1311 = _M0L6_2atmpS1312 - 2;
          int32_t _M0L6_2atmpS1314;
          int32_t _M0L6_2atmpS1313;
          int32_t _M0L6_2atmpS1315;
          _M0L6bufferS353[_M0L6_2atmpS1311] = _M0L5d__hiS360;
          _M0L6_2atmpS1314 = _M0L12digit__startS354 + _M0L6offsetS357;
          _M0L6_2atmpS1313 = _M0L6_2atmpS1314 - 1;
          _M0L6bufferS353[_M0L6_2atmpS1313] = _M0L5d__loS361;
          _M0L6_2atmpS1315 = _M0L6offsetS357 - 2;
          _M0L9remainingS356 = _M0L1tS358;
          _M0L6offsetS357 = _M0L6_2atmpS1315;
          continue;
        } else if (_M0L9remainingS356 >= 10) {
          int32_t _M0L6_2atmpS1327 = _M0L9remainingS356 / 10;
          int32_t _M0L6_2atmpS1326 = 48 + _M0L6_2atmpS1327;
          int32_t _M0L5d__hiS363 = (uint16_t)_M0L6_2atmpS1326;
          int32_t _M0L6_2atmpS1325 = _M0L9remainingS356 % 10;
          int32_t _M0L6_2atmpS1324 = 48 + _M0L6_2atmpS1325;
          int32_t _M0L5d__loS364 = (uint16_t)_M0L6_2atmpS1324;
          int32_t _M0L6_2atmpS1321 = _M0L12digit__startS354 + _M0L6offsetS357;
          int32_t _M0L6_2atmpS1320 = _M0L6_2atmpS1321 - 2;
          int32_t _M0L6_2atmpS1323;
          int32_t _M0L6_2atmpS1322;
          _M0L6bufferS353[_M0L6_2atmpS1320] = _M0L5d__hiS363;
          _M0L6_2atmpS1323 = _M0L12digit__startS354 + _M0L6offsetS357;
          _M0L6_2atmpS1322 = _M0L6_2atmpS1323 - 1;
          _M0L6bufferS353[_M0L6_2atmpS1322] = _M0L5d__loS364;
        } else {
          int32_t _M0L6_2atmpS1331 = _M0L12digit__startS354 + _M0L6offsetS357;
          int32_t _M0L6_2atmpS1328 = _M0L6_2atmpS1331 - 1;
          int32_t _M0L6_2atmpS1330 = 48 + _M0L9remainingS356;
          int32_t _M0L6_2atmpS1329 = (uint16_t)_M0L6_2atmpS1330;
          _M0L6bufferS353[_M0L6_2atmpS1328] = _M0L6_2atmpS1329;
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
  int32_t _M0L6_2atmpS1278;
  int32_t _M0L6_2atmpS1277;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS326 = _M0MPC13int3Int10to__uint64(_M0L5radixS327);
  _M0L6_2atmpS1278 = _M0L5radixS327 - 1;
  _M0L6_2atmpS1277 = _M0L5radixS327 & _M0L6_2atmpS1278;
  if (_M0L6_2atmpS1277 == 0) {
    int32_t _M0L5shiftS328;
    uint64_t _M0L4maskS329;
    int32_t _M0L6_2atmpS1285;
    int32_t _M0L6offsetS330;
    uint64_t _M0L1nS331;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS328 = moonbit_ctz32(_M0L5radixS327);
    _M0L4maskS329 = _M0L4baseS326 - 1ull;
    _M0L6_2atmpS1285 = _M0L10total__lenS336 - _M0L12digit__startS334;
    _M0L6offsetS330 = _M0L6_2atmpS1285;
    _M0L1nS331 = _M0L3numS337;
    while (1) {
      if (_M0L1nS331 > 0ull) {
        uint64_t _M0L6_2atmpS1284 = _M0L1nS331 & _M0L4maskS329;
        int32_t _M0L5digitS332 = (int32_t)_M0L6_2atmpS1284;
        int32_t _M0L6_2atmpS1281 = _M0L12digit__startS334 + _M0L6offsetS330;
        int32_t _M0L6_2atmpS1279 = _M0L6_2atmpS1281 - 1;
        int32_t _M0L6_2atmpS1280 =
          ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L5digitS332];
        int32_t _M0L6_2atmpS1282;
        uint64_t _M0L6_2atmpS1283;
        _M0L6bufferS333[_M0L6_2atmpS1279] = _M0L6_2atmpS1280;
        _M0L6_2atmpS1282 = _M0L6offsetS330 - 1;
        _M0L6_2atmpS1283 = _M0L1nS331 >> (_M0L5shiftS328 & 63);
        _M0L6offsetS330 = _M0L6_2atmpS1282;
        _M0L1nS331 = _M0L6_2atmpS1283;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1292 = _M0L10total__lenS336 - _M0L12digit__startS334;
    int32_t _M0L6offsetS338 = _M0L6_2atmpS1292;
    uint64_t _M0L1nS339 = _M0L3numS337;
    while (1) {
      if (_M0L1nS339 > 0ull) {
        uint64_t _M0L1qS340 = _M0L1nS339 / _M0L4baseS326;
        uint64_t _M0L6_2atmpS1291 = _M0L1qS340 * _M0L4baseS326;
        uint64_t _M0L6_2atmpS1290 = _M0L1nS339 - _M0L6_2atmpS1291;
        int32_t _M0L5digitS341 = (int32_t)_M0L6_2atmpS1290;
        int32_t _M0L6_2atmpS1288 = _M0L12digit__startS334 + _M0L6offsetS338;
        int32_t _M0L6_2atmpS1286 = _M0L6_2atmpS1288 - 1;
        int32_t _M0L6_2atmpS1287 =
          ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L5digitS341];
        int32_t _M0L6_2atmpS1289;
        _M0L6bufferS333[_M0L6_2atmpS1286] = _M0L6_2atmpS1287;
        _M0L6_2atmpS1289 = _M0L6offsetS338 - 1;
        _M0L6offsetS338 = _M0L6_2atmpS1289;
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
  int32_t _M0L6_2atmpS1276;
  int32_t _M0L6offsetS315;
  uint64_t _M0L1nS316;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1276 = _M0L10total__lenS324 - _M0L12digit__startS321;
  _M0L6offsetS315 = _M0L6_2atmpS1276;
  _M0L1nS316 = _M0L3numS325;
  while (1) {
    if (_M0L6offsetS315 >= 2) {
      uint64_t _M0L6_2atmpS1273 = _M0L1nS316 & 255ull;
      int32_t _M0L9byte__valS317 = (int32_t)_M0L6_2atmpS1273;
      int32_t _M0L2hiS318 = _M0L9byte__valS317 / 16;
      int32_t _M0L2loS319 = _M0L9byte__valS317 % 16;
      int32_t _M0L6_2atmpS1267 = _M0L12digit__startS321 + _M0L6offsetS315;
      int32_t _M0L6_2atmpS1265 = _M0L6_2atmpS1267 - 2;
      int32_t _M0L6_2atmpS1266 =
        ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L2hiS318];
      int32_t _M0L6_2atmpS1270;
      int32_t _M0L6_2atmpS1268;
      int32_t _M0L6_2atmpS1269;
      int32_t _M0L6_2atmpS1271;
      uint64_t _M0L6_2atmpS1272;
      _M0L6bufferS320[_M0L6_2atmpS1265] = _M0L6_2atmpS1266;
      _M0L6_2atmpS1270 = _M0L12digit__startS321 + _M0L6offsetS315;
      _M0L6_2atmpS1268 = _M0L6_2atmpS1270 - 1;
      _M0L6_2atmpS1269
      = ((moonbit_string_t)moonbit_string_literal_21.data)[
        _M0L2loS319
      ];
      _M0L6bufferS320[_M0L6_2atmpS1268] = _M0L6_2atmpS1269;
      _M0L6_2atmpS1271 = _M0L6offsetS315 - 2;
      _M0L6_2atmpS1272 = _M0L1nS316 >> 8;
      _M0L6offsetS315 = _M0L6_2atmpS1271;
      _M0L1nS316 = _M0L6_2atmpS1272;
      continue;
    } else if (_M0L6offsetS315 == 1) {
      uint64_t _M0L6_2atmpS1275 = _M0L1nS316 & 15ull;
      int32_t _M0L6nibbleS323 = (int32_t)_M0L6_2atmpS1275;
      int32_t _M0L6_2atmpS1274 =
        ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L6nibbleS323];
      _M0L6bufferS320[_M0L12digit__startS321] = _M0L6_2atmpS1274;
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
      uint64_t _M0L6_2atmpS1263 = _M0L3numS312 / _M0L4baseS310;
      int32_t _M0L6_2atmpS1264 = _M0L5countS313 + 1;
      _M0L3numS312 = _M0L6_2atmpS1263;
      _M0L5countS313 = _M0L6_2atmpS1264;
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
    int32_t _M0L6_2atmpS1262;
    int32_t _M0L6_2atmpS1261;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS308 = moonbit_clz64(_M0L5valueS307);
    _M0L6_2atmpS1262 = 63 - _M0L14leading__zerosS308;
    _M0L6_2atmpS1261 = _M0L6_2atmpS1262 / 4;
    return _M0L6_2atmpS1261 + 1;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_20.data);
  }
  if (_M0L4selfS290 == 0) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  _M0L12is__negativeS291 = _M0L4selfS290 < 0;
  if (_M0L12is__negativeS291) {
    int32_t _M0L6_2atmpS1260 = -_M0L4selfS290;
    _M0L3numS292 = *(uint32_t*)&_M0L6_2atmpS1260;
  } else {
    _M0L3numS292 = *(uint32_t*)&_M0L4selfS290;
  }
  switch (_M0L5radixS289) {
    case 10: {
      int32_t _M0L10digit__lenS294;
      int32_t _M0L6_2atmpS1257;
      int32_t _M0L10total__lenS295;
      uint16_t* _M0L6bufferS296;
      int32_t _M0L12digit__startS297;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS294 = _M0FPB12dec__count32(_M0L3numS292);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1257 = 1;
      } else {
        _M0L6_2atmpS1257 = 0;
      }
      _M0L10total__lenS295 = _M0L10digit__lenS294 + _M0L6_2atmpS1257;
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
      int32_t _M0L6_2atmpS1258;
      int32_t _M0L10total__lenS299;
      uint16_t* _M0L6bufferS300;
      int32_t _M0L12digit__startS301;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS298 = _M0FPB12hex__count32(_M0L3numS292);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1258 = 1;
      } else {
        _M0L6_2atmpS1258 = 0;
      }
      _M0L10total__lenS299 = _M0L10digit__lenS298 + _M0L6_2atmpS1258;
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
      int32_t _M0L6_2atmpS1259;
      int32_t _M0L10total__lenS303;
      uint16_t* _M0L6bufferS304;
      int32_t _M0L12digit__startS305;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS302
      = _M0FPB14radix__count32(_M0L3numS292, _M0L5radixS289);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1259 = 1;
      } else {
        _M0L6_2atmpS1259 = 0;
      }
      _M0L10total__lenS303 = _M0L10digit__lenS302 + _M0L6_2atmpS1259;
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
      uint32_t _M0L6_2atmpS1255 = _M0L3numS286 / _M0L4baseS284;
      int32_t _M0L6_2atmpS1256 = _M0L5countS287 + 1;
      _M0L3numS286 = _M0L6_2atmpS1255;
      _M0L5countS287 = _M0L6_2atmpS1256;
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
    int32_t _M0L6_2atmpS1254;
    int32_t _M0L6_2atmpS1253;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS282 = moonbit_clz32(_M0L5valueS281);
    _M0L6_2atmpS1254 = 31 - _M0L14leading__zerosS282;
    _M0L6_2atmpS1253 = _M0L6_2atmpS1254 / 4;
    return _M0L6_2atmpS1253 + 1;
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
  int32_t _M0L6_2atmpS1252;
  uint32_t _M0L3numS256;
  int32_t _M0L6offsetS257;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1252 = _M0L10total__lenS279 - _M0L12digit__startS267;
  _M0L3numS256 = _M0L3numS278;
  _M0L6offsetS257 = _M0L6_2atmpS1252;
  while (1) {
    if (_M0L3numS256 >= 10000u) {
      uint32_t _M0L1tS258 = _M0L3numS256 / 10000u;
      uint32_t _M0L6_2atmpS1229 = _M0L3numS256 % 10000u;
      int32_t _M0L1rS259 = *(int32_t*)&_M0L6_2atmpS1229;
      int32_t _M0L2d1S260 = _M0L1rS259 / 100;
      int32_t _M0L2d2S261 = _M0L1rS259 % 100;
      int32_t _M0L6_2atmpS1228 = _M0L2d1S260 / 10;
      int32_t _M0L6_2atmpS1227 = 48 + _M0L6_2atmpS1228;
      int32_t _M0L6d1__hiS262 = (uint16_t)_M0L6_2atmpS1227;
      int32_t _M0L6_2atmpS1226 = _M0L2d1S260 % 10;
      int32_t _M0L6_2atmpS1225 = 48 + _M0L6_2atmpS1226;
      int32_t _M0L6d1__loS263 = (uint16_t)_M0L6_2atmpS1225;
      int32_t _M0L6_2atmpS1224 = _M0L2d2S261 / 10;
      int32_t _M0L6_2atmpS1223 = 48 + _M0L6_2atmpS1224;
      int32_t _M0L6d2__hiS264 = (uint16_t)_M0L6_2atmpS1223;
      int32_t _M0L6_2atmpS1222 = _M0L2d2S261 % 10;
      int32_t _M0L6_2atmpS1221 = 48 + _M0L6_2atmpS1222;
      int32_t _M0L6d2__loS265 = (uint16_t)_M0L6_2atmpS1221;
      int32_t _M0L6_2atmpS1213 = _M0L12digit__startS267 + _M0L6offsetS257;
      int32_t _M0L6_2atmpS1212 = _M0L6_2atmpS1213 - 4;
      int32_t _M0L6_2atmpS1215;
      int32_t _M0L6_2atmpS1214;
      int32_t _M0L6_2atmpS1217;
      int32_t _M0L6_2atmpS1216;
      int32_t _M0L6_2atmpS1219;
      int32_t _M0L6_2atmpS1218;
      int32_t _M0L6_2atmpS1220;
      _M0L6bufferS266[_M0L6_2atmpS1212] = _M0L6d1__hiS262;
      _M0L6_2atmpS1215 = _M0L12digit__startS267 + _M0L6offsetS257;
      _M0L6_2atmpS1214 = _M0L6_2atmpS1215 - 3;
      _M0L6bufferS266[_M0L6_2atmpS1214] = _M0L6d1__loS263;
      _M0L6_2atmpS1217 = _M0L12digit__startS267 + _M0L6offsetS257;
      _M0L6_2atmpS1216 = _M0L6_2atmpS1217 - 2;
      _M0L6bufferS266[_M0L6_2atmpS1216] = _M0L6d2__hiS264;
      _M0L6_2atmpS1219 = _M0L12digit__startS267 + _M0L6offsetS257;
      _M0L6_2atmpS1218 = _M0L6_2atmpS1219 - 1;
      _M0L6bufferS266[_M0L6_2atmpS1218] = _M0L6d2__loS265;
      _M0L6_2atmpS1220 = _M0L6offsetS257 - 4;
      _M0L3numS256 = _M0L1tS258;
      _M0L6offsetS257 = _M0L6_2atmpS1220;
      continue;
    } else {
      int32_t _M0L6_2atmpS1251 = *(int32_t*)&_M0L3numS256;
      int32_t _M0L9remainingS269 = _M0L6_2atmpS1251;
      int32_t _M0L6offsetS270 = _M0L6offsetS257;
      while (1) {
        if (_M0L9remainingS269 >= 100) {
          int32_t _M0L1tS271 = _M0L9remainingS269 / 100;
          int32_t _M0L1dS272 = _M0L9remainingS269 % 100;
          int32_t _M0L6_2atmpS1238 = _M0L1dS272 / 10;
          int32_t _M0L6_2atmpS1237 = 48 + _M0L6_2atmpS1238;
          int32_t _M0L5d__hiS273 = (uint16_t)_M0L6_2atmpS1237;
          int32_t _M0L6_2atmpS1236 = _M0L1dS272 % 10;
          int32_t _M0L6_2atmpS1235 = 48 + _M0L6_2atmpS1236;
          int32_t _M0L5d__loS274 = (uint16_t)_M0L6_2atmpS1235;
          int32_t _M0L6_2atmpS1231 = _M0L12digit__startS267 + _M0L6offsetS270;
          int32_t _M0L6_2atmpS1230 = _M0L6_2atmpS1231 - 2;
          int32_t _M0L6_2atmpS1233;
          int32_t _M0L6_2atmpS1232;
          int32_t _M0L6_2atmpS1234;
          _M0L6bufferS266[_M0L6_2atmpS1230] = _M0L5d__hiS273;
          _M0L6_2atmpS1233 = _M0L12digit__startS267 + _M0L6offsetS270;
          _M0L6_2atmpS1232 = _M0L6_2atmpS1233 - 1;
          _M0L6bufferS266[_M0L6_2atmpS1232] = _M0L5d__loS274;
          _M0L6_2atmpS1234 = _M0L6offsetS270 - 2;
          _M0L9remainingS269 = _M0L1tS271;
          _M0L6offsetS270 = _M0L6_2atmpS1234;
          continue;
        } else if (_M0L9remainingS269 >= 10) {
          int32_t _M0L6_2atmpS1246 = _M0L9remainingS269 / 10;
          int32_t _M0L6_2atmpS1245 = 48 + _M0L6_2atmpS1246;
          int32_t _M0L5d__hiS276 = (uint16_t)_M0L6_2atmpS1245;
          int32_t _M0L6_2atmpS1244 = _M0L9remainingS269 % 10;
          int32_t _M0L6_2atmpS1243 = 48 + _M0L6_2atmpS1244;
          int32_t _M0L5d__loS277 = (uint16_t)_M0L6_2atmpS1243;
          int32_t _M0L6_2atmpS1240 = _M0L12digit__startS267 + _M0L6offsetS270;
          int32_t _M0L6_2atmpS1239 = _M0L6_2atmpS1240 - 2;
          int32_t _M0L6_2atmpS1242;
          int32_t _M0L6_2atmpS1241;
          _M0L6bufferS266[_M0L6_2atmpS1239] = _M0L5d__hiS276;
          _M0L6_2atmpS1242 = _M0L12digit__startS267 + _M0L6offsetS270;
          _M0L6_2atmpS1241 = _M0L6_2atmpS1242 - 1;
          _M0L6bufferS266[_M0L6_2atmpS1241] = _M0L5d__loS277;
        } else {
          int32_t _M0L6_2atmpS1250 = _M0L12digit__startS267 + _M0L6offsetS270;
          int32_t _M0L6_2atmpS1247 = _M0L6_2atmpS1250 - 1;
          int32_t _M0L6_2atmpS1249 = 48 + _M0L9remainingS269;
          int32_t _M0L6_2atmpS1248 = (uint16_t)_M0L6_2atmpS1249;
          _M0L6bufferS266[_M0L6_2atmpS1247] = _M0L6_2atmpS1248;
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
  int32_t _M0L6_2atmpS1197;
  int32_t _M0L6_2atmpS1196;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS239 = *(uint32_t*)&_M0L5radixS240;
  _M0L6_2atmpS1197 = _M0L5radixS240 - 1;
  _M0L6_2atmpS1196 = _M0L5radixS240 & _M0L6_2atmpS1197;
  if (_M0L6_2atmpS1196 == 0) {
    int32_t _M0L5shiftS241;
    uint32_t _M0L4maskS242;
    int32_t _M0L6_2atmpS1204;
    int32_t _M0L6offsetS243;
    uint32_t _M0L1nS244;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS241 = moonbit_ctz32(_M0L5radixS240);
    _M0L4maskS242 = _M0L4baseS239 - 1u;
    _M0L6_2atmpS1204 = _M0L10total__lenS249 - _M0L12digit__startS247;
    _M0L6offsetS243 = _M0L6_2atmpS1204;
    _M0L1nS244 = _M0L3numS250;
    while (1) {
      if (_M0L1nS244 > 0u) {
        uint32_t _M0L6_2atmpS1203 = _M0L1nS244 & _M0L4maskS242;
        int32_t _M0L5digitS245 = *(int32_t*)&_M0L6_2atmpS1203;
        int32_t _M0L6_2atmpS1200 = _M0L12digit__startS247 + _M0L6offsetS243;
        int32_t _M0L6_2atmpS1198 = _M0L6_2atmpS1200 - 1;
        int32_t _M0L6_2atmpS1199 =
          ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L5digitS245];
        int32_t _M0L6_2atmpS1201;
        uint32_t _M0L6_2atmpS1202;
        _M0L6bufferS246[_M0L6_2atmpS1198] = _M0L6_2atmpS1199;
        _M0L6_2atmpS1201 = _M0L6offsetS243 - 1;
        _M0L6_2atmpS1202 = _M0L1nS244 >> (_M0L5shiftS241 & 31);
        _M0L6offsetS243 = _M0L6_2atmpS1201;
        _M0L1nS244 = _M0L6_2atmpS1202;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1211 = _M0L10total__lenS249 - _M0L12digit__startS247;
    int32_t _M0L6offsetS251 = _M0L6_2atmpS1211;
    uint32_t _M0L1nS252 = _M0L3numS250;
    while (1) {
      if (_M0L1nS252 > 0u) {
        uint32_t _M0L1qS253 = _M0L1nS252 / _M0L4baseS239;
        uint32_t _M0L6_2atmpS1210 = _M0L1qS253 * _M0L4baseS239;
        uint32_t _M0L6_2atmpS1209 = _M0L1nS252 - _M0L6_2atmpS1210;
        int32_t _M0L5digitS254 = *(int32_t*)&_M0L6_2atmpS1209;
        int32_t _M0L6_2atmpS1207 = _M0L12digit__startS247 + _M0L6offsetS251;
        int32_t _M0L6_2atmpS1205 = _M0L6_2atmpS1207 - 1;
        int32_t _M0L6_2atmpS1206 =
          ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L5digitS254];
        int32_t _M0L6_2atmpS1208;
        _M0L6bufferS246[_M0L6_2atmpS1205] = _M0L6_2atmpS1206;
        _M0L6_2atmpS1208 = _M0L6offsetS251 - 1;
        _M0L6offsetS251 = _M0L6_2atmpS1208;
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
  int32_t _M0L6_2atmpS1195;
  int32_t _M0L6offsetS228;
  uint32_t _M0L1nS229;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1195 = _M0L10total__lenS237 - _M0L12digit__startS234;
  _M0L6offsetS228 = _M0L6_2atmpS1195;
  _M0L1nS229 = _M0L3numS238;
  while (1) {
    if (_M0L6offsetS228 >= 2) {
      uint32_t _M0L6_2atmpS1192 = _M0L1nS229 & 255u;
      int32_t _M0L9byte__valS230 = *(int32_t*)&_M0L6_2atmpS1192;
      int32_t _M0L2hiS231 = _M0L9byte__valS230 / 16;
      int32_t _M0L2loS232 = _M0L9byte__valS230 % 16;
      int32_t _M0L6_2atmpS1186 = _M0L12digit__startS234 + _M0L6offsetS228;
      int32_t _M0L6_2atmpS1184 = _M0L6_2atmpS1186 - 2;
      int32_t _M0L6_2atmpS1185 =
        ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L2hiS231];
      int32_t _M0L6_2atmpS1189;
      int32_t _M0L6_2atmpS1187;
      int32_t _M0L6_2atmpS1188;
      int32_t _M0L6_2atmpS1190;
      uint32_t _M0L6_2atmpS1191;
      _M0L6bufferS233[_M0L6_2atmpS1184] = _M0L6_2atmpS1185;
      _M0L6_2atmpS1189 = _M0L12digit__startS234 + _M0L6offsetS228;
      _M0L6_2atmpS1187 = _M0L6_2atmpS1189 - 1;
      _M0L6_2atmpS1188
      = ((moonbit_string_t)moonbit_string_literal_21.data)[
        _M0L2loS232
      ];
      _M0L6bufferS233[_M0L6_2atmpS1187] = _M0L6_2atmpS1188;
      _M0L6_2atmpS1190 = _M0L6offsetS228 - 2;
      _M0L6_2atmpS1191 = _M0L1nS229 >> 8;
      _M0L6offsetS228 = _M0L6_2atmpS1190;
      _M0L1nS229 = _M0L6_2atmpS1191;
      continue;
    } else if (_M0L6offsetS228 == 1) {
      uint32_t _M0L6_2atmpS1194 = _M0L1nS229 & 15u;
      int32_t _M0L6nibbleS236 = *(int32_t*)&_M0L6_2atmpS1194;
      int32_t _M0L6_2atmpS1193 =
        ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L6nibbleS236];
      _M0L6bufferS233[_M0L12digit__startS234] = _M0L6_2atmpS1193;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS227
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS226;
  struct _M0TPB6Logger _M0L6_2atmpS1183;
  moonbit_string_t _result_1999;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS226);
  _M0L6_2atmpS1183
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS226
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS227, _M0L6_2atmpS1183);
  if (_M0L6_2atmpS1183.$1) {
    moonbit_decref(_M0L6_2atmpS1183.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_1999 = _M0MPB13StringBuilder10to__string(_M0L6loggerS226);
  moonbit_decref_cycle_free(_M0L6loggerS226);
  return _result_1999;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS221,
  struct _M0TPB6Logger _M0L6loggerS220
) {
  moonbit_string_t _M0L6_2atmpS1180;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1180 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS221);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS220.$0->$method_0(_M0L6loggerS220.$1, _M0L6_2atmpS1180);
  moonbit_decref_cycle_free(_M0L6_2atmpS1180);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS223,
  struct _M0TPB6Logger _M0L6loggerS222
) {
  moonbit_string_t _M0L6_2atmpS1181;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1181 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS223);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS222.$0->$method_0(_M0L6loggerS222.$1, _M0L6_2atmpS1181);
  moonbit_decref_cycle_free(_M0L6_2atmpS1181);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS225,
  struct _M0TPB6Logger _M0L6loggerS224
) {
  moonbit_string_t _M0L6_2atmpS1182;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1182 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS225);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS224.$0->$method_0(_M0L6loggerS224.$1, _M0L6_2atmpS1182);
  moonbit_decref_cycle_free(_M0L6_2atmpS1182);
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
  moonbit_string_t _M0L8_2afieldS1898;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS1898 = _M0L4selfS218.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1898);
  return _M0L8_2afieldS1898;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS214,
  moonbit_string_t _M0L5valueS215,
  int32_t _M0L5startS216,
  int32_t _M0L3lenS217
) {
  int32_t _M0L6_2atmpS1179;
  int64_t _M0L6_2atmpS1178;
  struct _M0TPC16string10StringView _M0L6_2atmpS1177;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1179 = _M0L5startS216 + _M0L3lenS217;
  _M0L6_2atmpS1178 = (int64_t)_M0L6_2atmpS1179;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1177
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS215, _M0L5startS216, _M0L6_2atmpS1178);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS214, _M0L6_2atmpS1177);
  moonbit_decref_cycle_free(_M0L6_2atmpS1177.$0);
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
  int32_t _M0L6_2atmpS1161;
  int32_t _if__result_2000;
  int32_t _M0L6_2atmpS1169;
  int32_t _if__result_2001;
  int32_t _M0L6_2atmpS1171;
  int32_t _M0L6_2atmpS1172;
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
  _M0L6_2atmpS1161 = _M0Lm2loS208;
  if (_M0L6_2atmpS1161 > 0) {
    int32_t _M0L6_2atmpS1160 = _M0Lm2loS208;
    if (_M0L6_2atmpS1160 < _M0L3lenS206) {
      int32_t _M0L6_2atmpS1159 = _M0Lm2loS208;
      int32_t _M0L6_2atmpS1158 = _M0L4selfS207[_M0L6_2atmpS1159];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1158)) {
        int32_t _M0L6_2atmpS1157 = _M0Lm2loS208;
        int32_t _M0L6_2atmpS1156 = _M0L6_2atmpS1157 - 1;
        int32_t _M0L6_2atmpS1155 = _M0L4selfS207[_M0L6_2atmpS1156];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2000
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1155);
      } else {
        _if__result_2000 = 0;
      }
    } else {
      _if__result_2000 = 0;
    }
  } else {
    _if__result_2000 = 0;
  }
  if (_if__result_2000) {
    int32_t _M0L6_2atmpS1162 = _M0Lm2loS208;
    _M0Lm2loS208 = _M0L6_2atmpS1162 + 1;
  }
  _M0L6_2atmpS1169 = _M0Lm2hiS210;
  if (_M0L6_2atmpS1169 > 0) {
    int32_t _M0L6_2atmpS1168 = _M0Lm2hiS210;
    if (_M0L6_2atmpS1168 < _M0L3lenS206) {
      int32_t _M0L6_2atmpS1167 = _M0Lm2hiS210;
      int32_t _M0L6_2atmpS1166 = _M0L4selfS207[_M0L6_2atmpS1167];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1166)) {
        int32_t _M0L6_2atmpS1165 = _M0Lm2hiS210;
        int32_t _M0L6_2atmpS1164 = _M0L6_2atmpS1165 - 1;
        int32_t _M0L6_2atmpS1163 = _M0L4selfS207[_M0L6_2atmpS1164];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2001
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1163);
      } else {
        _if__result_2001 = 0;
      }
    } else {
      _if__result_2001 = 0;
    }
  } else {
    _if__result_2001 = 0;
  }
  if (_if__result_2001) {
    int32_t _M0L6_2atmpS1170 = _M0Lm2hiS210;
    _M0Lm2hiS210 = _M0L6_2atmpS1170 - 1;
  }
  _M0L6_2atmpS1171 = _M0Lm2loS208;
  _M0L6_2atmpS1172 = _M0Lm2hiS210;
  if (_M0L6_2atmpS1171 >= _M0L6_2atmpS1172) {
    int32_t _M0L6_2atmpS1173 = _M0Lm2loS208;
    int32_t _M0L6_2atmpS1174 = _M0Lm2loS208;
    moonbit_incref_cycle_free(_M0L4selfS207);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS207,
                                                 .$1 = _M0L6_2atmpS1173,
                                                 .$2 = _M0L6_2atmpS1174};
  } else {
    int32_t _M0L6_2atmpS1175 = _M0Lm2loS208;
    int32_t _M0L6_2atmpS1176 = _M0Lm2hiS210;
    moonbit_incref_cycle_free(_M0L4selfS207);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS207,
                                                 .$1 = _M0L6_2atmpS1175,
                                                 .$2 = _M0L6_2atmpS1176};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS205,
  struct _M0TPB4Show _M0L4showS204
) {
  struct _M0TPB6Logger _M0L6_2atmpS1154;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS205);
  _M0L6_2atmpS1154
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS205
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS204.$0->$method_0(_M0L4showS204.$1, _M0L6_2atmpS1154);
  if (_M0L6_2atmpS1154.$1) {
    moonbit_decref(_M0L6_2atmpS1154.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS203,
  struct _M0TPB4Show _M0L4showS202
) {
  struct _M0TPB6Logger _M0L6_2atmpS1153;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS203);
  _M0L6_2atmpS1153
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS203
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS202.$0->$method_0(_M0L4showS202.$1, _M0L6_2atmpS1153);
  if (_M0L6_2atmpS1153.$1) {
    moonbit_decref(_M0L6_2atmpS1153.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS201) {
  int64_t _M0L6_2atmpS1152;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1152 = (int64_t)_M0L4selfS201;
  return *(uint64_t*)&_M0L6_2atmpS1152;
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
  int32_t _M0L6_2atmpS1151;
  struct _M0TPC16string10StringView _M0L6_2atmpS1149;
  struct _M0TPB6Logger _M0L6_2atmpS1150;
  moonbit_string_t _result_2002;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1151 = Moonbit_array_length(_M0L4selfS199);
  moonbit_incref_cycle_free(_M0L4selfS199);
  _M0L6_2atmpS1149
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS199, .$1 = 0, .$2 = _M0L6_2atmpS1151
  };
  moonbit_incref_cycle_free(_M0L3bufS198);
  _M0L6_2atmpS1150
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS198
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1149, _M0L6_2atmpS1150, _M0L5quoteS200);
  moonbit_decref_cycle_free(_M0L6_2atmpS1149.$0);
  if (_M0L6_2atmpS1150.$1) {
    moonbit_decref(_M0L6_2atmpS1150.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2002 = _M0MPB13StringBuilder10to__string(_M0L3bufS198);
  moonbit_decref_cycle_free(_M0L3bufS198);
  return _result_2002;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS190,
  struct _M0TPB6Logger _M0L6loggerS188,
  int32_t _M0L5quoteS187
) {
  int32_t _M0L3endS1147;
  int32_t _M0L5startS1148;
  int32_t _M0L3lenS189;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS191;
  int32_t _M0L1iS192;
  int32_t _M0L3segS193;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS187) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS188.$0->$method_3(_M0L6loggerS188.$1, 34);
  }
  _M0L3endS1147 = _M0L4selfS190.$2;
  _M0L5startS1148 = _M0L4selfS190.$1;
  _M0L3lenS189 = _M0L3endS1147 - _M0L5startS1148;
  moonbit_incref_cycle_free(_M0L4selfS190.$0);
  if (_M0L6loggerS188.$1) {
    moonbit_incref(_M0L6loggerS188.$1);
  }
  _M0L6_2aenvS191
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS191)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 52, 0);
  _M0L6_2aenvS191->$0 = _M0L4selfS190;
  _M0L6_2aenvS191->$1 = _M0L6loggerS188;
  _M0L1iS192 = 0;
  _M0L3segS193 = 0;
  _2afor_194:;
  while (1) {
    moonbit_string_t _M0L3strS1144;
    int32_t _M0L5startS1146;
    int32_t _M0L6_2atmpS1145;
    int32_t _M0L4codeS195;
    int32_t _M0L1cS197;
    int32_t _M0L6_2atmpS1128;
    int32_t _M0L6_2atmpS1129;
    int32_t _M0L6_2atmpS1130;
    if (_M0L1iS192 >= _M0L3lenS189) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
      moonbit_decref_cycle_free(_M0L6_2aenvS191);
      break;
    }
    _M0L3strS1144 = _M0L4selfS190.$0;
    _M0L5startS1146 = _M0L4selfS190.$1;
    _M0L6_2atmpS1145 = _M0L5startS1146 + _M0L1iS192;
    _M0L4codeS195 = _M0L3strS1144[_M0L6_2atmpS1145];
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
        int32_t _M0L6_2atmpS1131;
        int32_t _M0L6_2atmpS1132;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_22.data);
        _M0L6_2atmpS1131 = _M0L1iS192 + 1;
        _M0L6_2atmpS1132 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1131;
        _M0L3segS193 = _M0L6_2atmpS1132;
        goto _2afor_194;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1133;
        int32_t _M0L6_2atmpS1134;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_23.data);
        _M0L6_2atmpS1133 = _M0L1iS192 + 1;
        _M0L6_2atmpS1134 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1133;
        _M0L3segS193 = _M0L6_2atmpS1134;
        goto _2afor_194;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1135;
        int32_t _M0L6_2atmpS1136;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_24.data);
        _M0L6_2atmpS1135 = _M0L1iS192 + 1;
        _M0L6_2atmpS1136 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1135;
        _M0L3segS193 = _M0L6_2atmpS1136;
        goto _2afor_194;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1137;
        int32_t _M0L6_2atmpS1138;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_25.data);
        _M0L6_2atmpS1137 = _M0L1iS192 + 1;
        _M0L6_2atmpS1138 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1137;
        _M0L3segS193 = _M0L6_2atmpS1138;
        goto _2afor_194;
        break;
      }
      default: {
        if (_M0L4codeS195 < 32) {
          int32_t _M0L6_2atmpS1140;
          moonbit_string_t _M0L6_2atmpS1139;
          int32_t _M0L6_2atmpS1141;
          int32_t _M0L6_2atmpS1142;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_26.data);
          _M0L6_2atmpS1140 = _M0L4codeS195 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1139 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1140);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, _M0L6_2atmpS1139);
          moonbit_decref_cycle_free(_M0L6_2atmpS1139);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1141 = _M0L1iS192 + 1;
          _M0L6_2atmpS1142 = _M0L1iS192 + 1;
          _M0L1iS192 = _M0L6_2atmpS1141;
          _M0L3segS193 = _M0L6_2atmpS1142;
          goto _2afor_194;
        } else {
          int32_t _M0L6_2atmpS1143 = _M0L1iS192 + 1;
          int32_t _tmp_2005 = _M0L3segS193;
          _M0L1iS192 = _M0L6_2atmpS1143;
          _M0L3segS193 = _tmp_2005;
          goto _2afor_194;
        }
        break;
      }
    }
    goto joinlet_2004;
    join_196:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS188.$0->$method_3(_M0L6loggerS188.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1128 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS197);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS188.$0->$method_3(_M0L6loggerS188.$1, _M0L6_2atmpS1128);
    _M0L6_2atmpS1129 = _M0L1iS192 + 1;
    _M0L6_2atmpS1130 = _M0L1iS192 + 1;
    _M0L1iS192 = _M0L6_2atmpS1129;
    _M0L3segS193 = _M0L6_2atmpS1130;
    continue;
    joinlet_2004:;
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
    int64_t _M0L6_2atmpS1127 = (int64_t)_M0L1iS185;
    struct _M0TPC16string10StringView _M0L6_2atmpS1126;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1126
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS184, _M0L3segS186, _M0L6_2atmpS1127);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS182.$0->$method_2(_M0L6loggerS182.$1, _M0L6_2atmpS1126);
    moonbit_decref_cycle_free(_M0L6_2atmpS1126.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS173,
  int32_t _M0L5startS175,
  int64_t _M0L3endS177
) {
  int32_t _M0L3endS1124;
  int32_t _M0L5startS1125;
  int32_t _M0L3lenS172;
  int32_t _M0Lm2loS174;
  int32_t _M0Lm2hiS176;
  moonbit_string_t _M0L3strS180;
  int32_t _M0L4baseS181;
  int32_t _M0L6_2atmpS1102;
  int32_t _if__result_2006;
  int32_t _M0L6_2atmpS1112;
  int32_t _if__result_2007;
  int32_t _M0L6_2atmpS1114;
  int32_t _M0L6_2atmpS1115;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1124 = _M0L4selfS173.$2;
  _M0L5startS1125 = _M0L4selfS173.$1;
  _M0L3lenS172 = _M0L3endS1124 - _M0L5startS1125;
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
  _M0L6_2atmpS1102 = _M0Lm2loS174;
  if (_M0L6_2atmpS1102 > 0) {
    int32_t _M0L6_2atmpS1101 = _M0Lm2loS174;
    if (_M0L6_2atmpS1101 < _M0L3lenS172) {
      int32_t _M0L6_2atmpS1100 = _M0Lm2loS174;
      int32_t _M0L6_2atmpS1099 = _M0L4baseS181 + _M0L6_2atmpS1100;
      int32_t _M0L6_2atmpS1098 = _M0L3strS180[_M0L6_2atmpS1099];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1098)) {
        int32_t _M0L6_2atmpS1097 = _M0Lm2loS174;
        int32_t _M0L6_2atmpS1096 = _M0L4baseS181 + _M0L6_2atmpS1097;
        int32_t _M0L6_2atmpS1095 = _M0L6_2atmpS1096 - 1;
        int32_t _M0L6_2atmpS1094 = _M0L3strS180[_M0L6_2atmpS1095];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2006
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1094);
      } else {
        _if__result_2006 = 0;
      }
    } else {
      _if__result_2006 = 0;
    }
  } else {
    _if__result_2006 = 0;
  }
  if (_if__result_2006) {
    int32_t _M0L6_2atmpS1103 = _M0Lm2loS174;
    _M0Lm2loS174 = _M0L6_2atmpS1103 + 1;
  }
  _M0L6_2atmpS1112 = _M0Lm2hiS176;
  if (_M0L6_2atmpS1112 > 0) {
    int32_t _M0L6_2atmpS1111 = _M0Lm2hiS176;
    if (_M0L6_2atmpS1111 < _M0L3lenS172) {
      int32_t _M0L6_2atmpS1110 = _M0Lm2hiS176;
      int32_t _M0L6_2atmpS1109 = _M0L4baseS181 + _M0L6_2atmpS1110;
      int32_t _M0L6_2atmpS1108 = _M0L3strS180[_M0L6_2atmpS1109];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1108)) {
        int32_t _M0L6_2atmpS1107 = _M0Lm2hiS176;
        int32_t _M0L6_2atmpS1106 = _M0L4baseS181 + _M0L6_2atmpS1107;
        int32_t _M0L6_2atmpS1105 = _M0L6_2atmpS1106 - 1;
        int32_t _M0L6_2atmpS1104 = _M0L3strS180[_M0L6_2atmpS1105];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2007
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1104);
      } else {
        _if__result_2007 = 0;
      }
    } else {
      _if__result_2007 = 0;
    }
  } else {
    _if__result_2007 = 0;
  }
  if (_if__result_2007) {
    int32_t _M0L6_2atmpS1113 = _M0Lm2hiS176;
    _M0Lm2hiS176 = _M0L6_2atmpS1113 - 1;
  }
  _M0L6_2atmpS1114 = _M0Lm2loS174;
  _M0L6_2atmpS1115 = _M0Lm2hiS176;
  if (_M0L6_2atmpS1114 >= _M0L6_2atmpS1115) {
    int32_t _M0L6_2atmpS1119 = _M0Lm2loS174;
    int32_t _M0L6_2atmpS1116 = _M0L4baseS181 + _M0L6_2atmpS1119;
    int32_t _M0L6_2atmpS1118 = _M0Lm2loS174;
    int32_t _M0L6_2atmpS1117 = _M0L4baseS181 + _M0L6_2atmpS1118;
    moonbit_incref_cycle_free(_M0L3strS180);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS180,
                                                 .$1 = _M0L6_2atmpS1116,
                                                 .$2 = _M0L6_2atmpS1117};
  } else {
    int32_t _M0L6_2atmpS1123 = _M0Lm2loS174;
    int32_t _M0L6_2atmpS1120 = _M0L4baseS181 + _M0L6_2atmpS1123;
    int32_t _M0L6_2atmpS1122 = _M0Lm2hiS176;
    int32_t _M0L6_2atmpS1121 = _M0L4baseS181 + _M0L6_2atmpS1122;
    moonbit_incref_cycle_free(_M0L3strS180);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS180,
                                                 .$1 = _M0L6_2atmpS1120,
                                                 .$2 = _M0L6_2atmpS1121};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS171) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS170;
  int32_t _M0L6_2atmpS1091;
  int32_t _M0L6_2atmpS1090;
  int32_t _M0L6_2atmpS1093;
  int32_t _M0L6_2atmpS1092;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1089;
  moonbit_string_t _result_2008;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS170 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1091 = _M0IPC14byte4BytePB3Div3div(_M0L1bS171, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1090
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1091);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS170, _M0L6_2atmpS1090);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1093 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS171, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1092
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1093);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS170, _M0L6_2atmpS1092);
  _M0L6_2atmpS1089 = _M0L7_2aselfS170;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2008 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1089);
  moonbit_decref_cycle_free(_M0L6_2atmpS1089);
  return _result_2008;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS169) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS169 < 10) {
    int32_t _M0L6_2atmpS1086;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1086 = _M0IPC14byte4BytePB3Add3add(_M0L1iS169, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1086);
  } else {
    int32_t _M0L6_2atmpS1088;
    int32_t _M0L6_2atmpS1087;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1088 = _M0IPC14byte4BytePB3Add3add(_M0L1iS169, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1087 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1088, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1087);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS167,
  int32_t _M0L4thatS168
) {
  int32_t _M0L6_2atmpS1084;
  int32_t _M0L6_2atmpS1085;
  int32_t _M0L6_2atmpS1083;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1084 = (int32_t)_M0L4selfS167;
  _M0L6_2atmpS1085 = (int32_t)_M0L4thatS168;
  _M0L6_2atmpS1083 = _M0L6_2atmpS1084 - _M0L6_2atmpS1085;
  return _M0L6_2atmpS1083 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS165,
  int32_t _M0L4thatS166
) {
  int32_t _M0L6_2atmpS1081;
  int32_t _M0L6_2atmpS1082;
  int32_t _M0L6_2atmpS1080;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1081 = (int32_t)_M0L4selfS165;
  _M0L6_2atmpS1082 = (int32_t)_M0L4thatS166;
  _M0L6_2atmpS1080 = _M0L6_2atmpS1081 % _M0L6_2atmpS1082;
  return _M0L6_2atmpS1080 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS163,
  int32_t _M0L4thatS164
) {
  int32_t _M0L6_2atmpS1078;
  int32_t _M0L6_2atmpS1079;
  int32_t _M0L6_2atmpS1077;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1078 = (int32_t)_M0L4selfS163;
  _M0L6_2atmpS1079 = (int32_t)_M0L4thatS164;
  _M0L6_2atmpS1077 = _M0L6_2atmpS1078 / _M0L6_2atmpS1079;
  return _M0L6_2atmpS1077 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS161,
  int32_t _M0L4thatS162
) {
  int32_t _M0L6_2atmpS1075;
  int32_t _M0L6_2atmpS1076;
  int32_t _M0L6_2atmpS1074;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1075 = (int32_t)_M0L4selfS161;
  _M0L6_2atmpS1076 = (int32_t)_M0L4thatS162;
  _M0L6_2atmpS1074 = _M0L6_2atmpS1075 + _M0L6_2atmpS1076;
  return _M0L6_2atmpS1074 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS160) {
  int32_t _M0L6_2atmpS1073;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1073 = (int32_t)_M0L4selfS160;
  return _M0L6_2atmpS1073;
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
  int32_t _M0L3lenS1072;
  int32_t _M0L8requiredS156;
  uint16_t* _M0L4dataS1067;
  int32_t _M0L6_2atmpS1066;
  int32_t _if__result_2009;
  uint16_t* _M0L4dataS1068;
  int32_t _M0L3lenS1069;
  int32_t _M0L3lenS1071;
  int32_t _M0L6_2atmpS1070;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS154 = Moonbit_array_length(_M0L3strS155);
  if (_M0L8str__lenS154 == 0) {
    return 0;
  }
  _M0L3lenS1072 = _M0L4selfS157->$1;
  _M0L8requiredS156 = _M0L3lenS1072 + _M0L8str__lenS154;
  _M0L4dataS1067 = _M0L4selfS157->$0;
  _M0L6_2atmpS1066 = Moonbit_array_length(_M0L4dataS1067);
  if (_M0L8requiredS156 > _M0L6_2atmpS1066) {
    _if__result_2009 = 1;
  } else {
    int32_t _M0L3lenS1065 = _M0L4selfS157->$1;
    _if__result_2009 = _M0L8requiredS156 < _M0L3lenS1065;
  }
  if (_if__result_2009) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS157, _M0L8requiredS156);
  }
  _M0L4dataS1068 = _M0L4selfS157->$0;
  _M0L3lenS1069 = _M0L4selfS157->$1;
  moonbit_incref_cycle_free(_M0L4dataS1068);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1068, _M0L3lenS1069, _M0L3strS155, 0, _M0L8str__lenS154);
  moonbit_decref_cycle_free(_M0L4dataS1068);
  _M0L3lenS1071 = _M0L4selfS157->$1;
  _M0L6_2atmpS1070 = _M0L3lenS1071 + _M0L8str__lenS154;
  _M0L4selfS157->$1 = _M0L6_2atmpS1070;
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
      int32_t _M0L6_2atmpS1062 = _M0L3strS151[_M0L1iS148];
      int32_t _M0L6_2atmpS1063;
      int32_t _M0L6_2atmpS1064;
      _M0L4selfS150[_M0L1jS149] = _M0L6_2atmpS1062;
      _M0L6_2atmpS1063 = _M0L1iS148 + 1;
      _M0L6_2atmpS1064 = _M0L1jS149 + 1;
      _M0L1iS148 = _M0L6_2atmpS1063;
      _M0L1jS149 = _M0L6_2atmpS1064;
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
    int32_t _M0L3lenS1033 = _M0L4selfS143->$1;
    uint16_t* _M0L4dataS1035 = _M0L4selfS143->$0;
    int32_t _M0L6_2atmpS1034 = Moonbit_array_length(_M0L4dataS1035);
    uint16_t* _M0L4dataS1038;
    int32_t _M0L3lenS1039;
    int32_t _M0L6_2atmpS1040;
    int32_t _M0L3lenS1042;
    int32_t _M0L6_2atmpS1041;
    if (_M0L3lenS1033 >= _M0L6_2atmpS1034) {
      int32_t _M0L3lenS1037 = _M0L4selfS143->$1;
      int32_t _M0L6_2atmpS1036 = _M0L3lenS1037 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS143, _M0L6_2atmpS1036);
    }
    _M0L4dataS1038 = _M0L4selfS143->$0;
    _M0L3lenS1039 = _M0L4selfS143->$1;
    moonbit_incref_cycle_free(_M0L4dataS1038);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1040 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS141);
    if (
      _M0L3lenS1039 < 0
      || _M0L3lenS1039 >= Moonbit_array_length(_M0L4dataS1038)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1038[_M0L3lenS1039] = _M0L6_2atmpS1040;
    moonbit_decref_cycle_free(_M0L4dataS1038);
    _M0L3lenS1042 = _M0L4selfS143->$1;
    _M0L6_2atmpS1041 = _M0L3lenS1042 + 1;
    _M0L4selfS143->$1 = _M0L6_2atmpS1041;
  } else if (_M0L4codeS141 <= 1114111u) {
    uint16_t* _M0L4dataS1046 = _M0L4selfS143->$0;
    int32_t _M0L6_2atmpS1044 = Moonbit_array_length(_M0L4dataS1046);
    int32_t _M0L3lenS1045 = _M0L4selfS143->$1;
    int32_t _M0L6_2atmpS1043 = _M0L6_2atmpS1044 - _M0L3lenS1045;
    uint32_t _M0L4codeS144;
    uint16_t* _M0L4dataS1049;
    int32_t _M0L3lenS1050;
    uint32_t _M0L6_2atmpS1053;
    uint32_t _M0L6_2atmpS1052;
    int32_t _M0L6_2atmpS1051;
    uint16_t* _M0L4dataS1054;
    int32_t _M0L3lenS1059;
    int32_t _M0L6_2atmpS1055;
    uint32_t _M0L6_2atmpS1058;
    uint32_t _M0L6_2atmpS1057;
    int32_t _M0L6_2atmpS1056;
    int32_t _M0L3lenS1061;
    int32_t _M0L6_2atmpS1060;
    if (_M0L6_2atmpS1043 < 2) {
      int32_t _M0L3lenS1048 = _M0L4selfS143->$1;
      int32_t _M0L6_2atmpS1047 = _M0L3lenS1048 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS143, _M0L6_2atmpS1047);
    }
    _M0L4codeS144 = _M0L4codeS141 - 65536u;
    _M0L4dataS1049 = _M0L4selfS143->$0;
    _M0L3lenS1050 = _M0L4selfS143->$1;
    _M0L6_2atmpS1053 = _M0L4codeS144 >> 10;
    _M0L6_2atmpS1052 = 55296u + _M0L6_2atmpS1053;
    moonbit_incref_cycle_free(_M0L4dataS1049);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1051 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1052);
    if (
      _M0L3lenS1050 < 0
      || _M0L3lenS1050 >= Moonbit_array_length(_M0L4dataS1049)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1049[_M0L3lenS1050] = _M0L6_2atmpS1051;
    moonbit_decref_cycle_free(_M0L4dataS1049);
    _M0L4dataS1054 = _M0L4selfS143->$0;
    _M0L3lenS1059 = _M0L4selfS143->$1;
    _M0L6_2atmpS1055 = _M0L3lenS1059 + 1;
    _M0L6_2atmpS1058 = _M0L4codeS144 & 1023u;
    _M0L6_2atmpS1057 = 56320u + _M0L6_2atmpS1058;
    moonbit_incref_cycle_free(_M0L4dataS1054);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1056 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1057);
    if (
      _M0L6_2atmpS1055 < 0
      || _M0L6_2atmpS1055 >= Moonbit_array_length(_M0L4dataS1054)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1054[_M0L6_2atmpS1055] = _M0L6_2atmpS1056;
    moonbit_decref_cycle_free(_M0L4dataS1054);
    _M0L3lenS1061 = _M0L4selfS143->$1;
    _M0L6_2atmpS1060 = _M0L3lenS1061 + 2;
    _M0L4selfS143->$1 = _M0L6_2atmpS1060;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_27.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS138,
  int32_t _M0L8requiredS139
) {
  uint16_t* _M0L4dataS1032;
  int32_t _M0L6_2atmpS1030;
  int32_t _M0L3lenS1031;
  int32_t _M0L13new__capacityS137;
  uint16_t* _M0L4dataS1027;
  int32_t _M0L6_2atmpS1028;
  int32_t _M0L3lenS1029;
  uint16_t* _M0L9new__dataS140;
  uint16_t* _M0L6_2aoldS1899;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1032 = _M0L4selfS138->$0;
  _M0L6_2atmpS1030 = Moonbit_array_length(_M0L4dataS1032);
  _M0L3lenS1031 = _M0L4selfS138->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS137
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1030, _M0L3lenS1031, _M0L8requiredS139);
  _M0L4dataS1027 = _M0L4selfS138->$0;
  moonbit_incref_cycle_free(_M0L4dataS1027);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1028 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1029 = _M0L4selfS138->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS140
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1027, _M0L13new__capacityS137, _M0L6_2atmpS1028, _M0L3lenS1029, 0, 0);
  _M0L6_2aoldS1899 = _M0L4selfS138->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1899);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_28.data);
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
  int32_t _M0L6_2atmpS1026;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1026 = *(int32_t*)&_M0L4selfS130;
  return (uint16_t)_M0L6_2atmpS1026;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS129) {
  int32_t _M0L6_2atmpS1025;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1025 = _M0L4selfS129;
  return *(uint32_t*)&_M0L6_2atmpS1025;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS127
) {
  int32_t _M0L3lenS1016;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1016 = _M0L4selfS127->$1;
  if (_M0L3lenS1016 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1017 = _M0L4selfS127->$1;
    uint16_t* _M0L4dataS1019 = _M0L4selfS127->$0;
    int32_t _M0L6_2atmpS1018 = Moonbit_array_length(_M0L4dataS1019);
    if (_M0L3lenS1017 == _M0L6_2atmpS1018) {
      uint16_t* _M0L4dataS1020 = _M0L4selfS127->$0;
      moonbit_incref_cycle_free(_M0L4dataS1020);
      return _M0L4dataS1020;
    } else {
      uint16_t* _M0L4dataS1021 = _M0L4selfS127->$0;
      int32_t _M0L3lenS1022 = _M0L4selfS127->$1;
      int32_t _M0L6_2atmpS1023;
      int32_t _M0L3lenS1024;
      uint16_t* _M0L4dataS128;
      moonbit_incref_cycle_free(_M0L4dataS1021);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1023 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1024 = _M0L4selfS127->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS128
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1021, _M0L3lenS1022, _M0L6_2atmpS1023, _M0L3lenS1024, 0, 0);
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
  int32_t _if__result_2012;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS120 >= 0) {
    if (_M0L3lenS121 >= 0) {
      if (_M0L11src__offsetS122 >= 0) {
        if (_M0L11dst__offsetS123 >= 0) {
          int32_t _M0L6_2atmpS1012 = _M0L11src__offsetS122 + _M0L3lenS121;
          int32_t _M0L6_2atmpS1013 = Moonbit_array_length(_M0L3srcS124);
          if (_M0L6_2atmpS1012 <= _M0L6_2atmpS1013) {
            int32_t _M0L6_2atmpS1011 = _M0L11dst__offsetS123 + _M0L3lenS121;
            _if__result_2012 = _M0L6_2atmpS1011 <= _M0L13allocate__lenS120;
          } else {
            _if__result_2012 = 0;
          }
        } else {
          _if__result_2012 = 0;
        }
      } else {
        _if__result_2012 = 0;
      }
    } else {
      _if__result_2012 = 0;
    }
  } else {
    _if__result_2012 = 0;
  }
  if (_if__result_2012) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS124, _M0L13allocate__lenS120, _M0L4initS125, _M0L11src__offsetS122, _M0L11dst__offsetS123, _M0L3lenS121);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS126;
    int32_t _M0L6_2atmpS1015;
    moonbit_string_t _M0L6_2atmpS1014;
    uint16_t* _result_2013;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS126
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L13allocate__lenS120);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L11src__offsetS122);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L11dst__offsetS123);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L3lenS121);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_33.data);
    _M0L6_2atmpS1015 = Moonbit_array_length(_M0L3srcS124);
    moonbit_decref_cycle_free(_M0L3srcS124);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L6_2atmpS1015);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1014
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS126);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS126);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2013 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1014);
    moonbit_decref_cycle_free(_M0L6_2atmpS1014);
    return _result_2013;
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
  struct _M0TPB13StringBuilder* _block_2014;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS111 < 1) {
    _M0L7initialS110 = 1;
  } else {
    int32_t _M0L6_2atmpS1010 = _M0L10size__hintS111 + 1;
    _M0L7initialS110 = _M0L6_2atmpS1010 / 2;
  }
  _M0L4dataS112 = (uint16_t*)moonbit_make_string(_M0L7initialS110, 0);
  _block_2014
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2014)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
  _block_2014->$0 = _M0L4dataS112;
  _block_2014->$1 = 0;
  return _block_2014;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS109) {
  int32_t _M0L6_2atmpS1009;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1009 = (int32_t)_M0L4selfS109;
  return _M0L6_2atmpS1009;
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS95,
  int32_t _M0L13allocate__lenS91,
  int32_t _M0L3lenS92,
  int32_t _M0L11src__offsetS93,
  int32_t _M0L11dst__offsetS94
) {
  int32_t _if__result_2015;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS91 >= 0) {
    if (_M0L3lenS92 >= 0) {
      if (_M0L11src__offsetS93 >= 0) {
        if (_M0L11dst__offsetS94 >= 0) {
          int32_t _M0L6_2atmpS995 = _M0L11src__offsetS93 + _M0L3lenS92;
          int32_t _M0L6_2atmpS996;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS996 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS95);
          if (_M0L6_2atmpS995 <= _M0L6_2atmpS996) {
            int32_t _M0L6_2atmpS994 = _M0L11dst__offsetS94 + _M0L3lenS92;
            _if__result_2015 = _M0L6_2atmpS994 <= _M0L13allocate__lenS91;
          } else {
            _if__result_2015 = 0;
          }
        } else {
          _if__result_2015 = 0;
        }
      } else {
        _if__result_2015 = 0;
      }
    } else {
      _if__result_2015 = 0;
    }
  } else {
    _if__result_2015 = 0;
  }
  if (_if__result_2015) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS95, _M0L13allocate__lenS91, _M0L11src__offsetS93, _M0L11dst__offsetS94, _M0L3lenS92);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS96;
    int32_t _M0L6_2atmpS998;
    moonbit_string_t _M0L6_2atmpS997;
    int32_t* _result_2016;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS96
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L13allocate__lenS91);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L11src__offsetS93);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L11dst__offsetS94);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L3lenS92);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS998 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS95);
    moonbit_decref_cycle_free(_M0L3srcS95);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L6_2atmpS998);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS997
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS96);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS96);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2016
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS997);
    moonbit_decref_cycle_free(_M0L6_2atmpS997);
    return _result_2016;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS101,
  int32_t _M0L13allocate__lenS97,
  int32_t _M0L3lenS98,
  int32_t _M0L11src__offsetS99,
  int32_t _M0L11dst__offsetS100
) {
  int32_t _if__result_2017;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS97 >= 0) {
    if (_M0L3lenS98 >= 0) {
      if (_M0L11src__offsetS99 >= 0) {
        if (_M0L11dst__offsetS100 >= 0) {
          int32_t _M0L6_2atmpS1000 = _M0L11src__offsetS99 + _M0L3lenS98;
          int32_t _M0L6_2atmpS1001;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1001
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS101);
          if (_M0L6_2atmpS1000 <= _M0L6_2atmpS1001) {
            int32_t _M0L6_2atmpS999 = _M0L11dst__offsetS100 + _M0L3lenS98;
            _if__result_2017 = _M0L6_2atmpS999 <= _M0L13allocate__lenS97;
          } else {
            _if__result_2017 = 0;
          }
        } else {
          _if__result_2017 = 0;
        }
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
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS97, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS101, _M0L11src__offsetS99, _M0L11dst__offsetS100, _M0L3lenS98);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS102;
    int32_t _M0L6_2atmpS1003;
    moonbit_string_t _M0L6_2atmpS1002;
    moonbit_string_t* _result_2018;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS102
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L13allocate__lenS97);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L11src__offsetS99);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L11dst__offsetS100);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L3lenS98);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1003 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS101);
    moonbit_decref_cycle_free(_M0L3srcS101);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L6_2atmpS1003);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1002
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS102);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS102);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2018
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1002);
    moonbit_decref_cycle_free(_M0L6_2atmpS1002);
    return _result_2018;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS107,
  int32_t _M0L13allocate__lenS103,
  int32_t _M0L3lenS104,
  int32_t _M0L11src__offsetS105,
  int32_t _M0L11dst__offsetS106
) {
  int32_t _if__result_2019;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS103 >= 0) {
    if (_M0L3lenS104 >= 0) {
      if (_M0L11src__offsetS105 >= 0) {
        if (_M0L11dst__offsetS106 >= 0) {
          int32_t _M0L6_2atmpS1005 = _M0L11src__offsetS105 + _M0L3lenS104;
          int32_t _M0L6_2atmpS1006;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1006
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS107);
          if (_M0L6_2atmpS1005 <= _M0L6_2atmpS1006) {
            int32_t _M0L6_2atmpS1004 = _M0L11dst__offsetS106 + _M0L3lenS104;
            _if__result_2019 = _M0L6_2atmpS1004 <= _M0L13allocate__lenS103;
          } else {
            _if__result_2019 = 0;
          }
        } else {
          _if__result_2019 = 0;
        }
      } else {
        _if__result_2019 = 0;
      }
    } else {
      _if__result_2019 = 0;
    }
  } else {
    _if__result_2019 = 0;
  }
  if (_if__result_2019) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS103, 0, _M0L3srcS107, _M0L11src__offsetS105, _M0L11dst__offsetS106, _M0L3lenS104);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS108;
    int32_t _M0L6_2atmpS1008;
    moonbit_string_t _M0L6_2atmpS1007;
    struct _M0TUsiE** _result_2020;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS108
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L13allocate__lenS103);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L11src__offsetS105);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L11dst__offsetS106);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L3lenS104);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1008 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS107);
    moonbit_decref_cycle_free(_M0L3srcS107);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L6_2atmpS1008);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1007
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS108);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS108);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2020
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1007);
    moonbit_decref_cycle_free(_M0L6_2atmpS1007);
    return _result_2020;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS86,
  moonbit_string_t _M0L3objS85
) {
  struct _M0TPB6Logger _M0L6_2atmpS991;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS86);
  _M0L6_2atmpS991
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS86
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS85, _M0L6_2atmpS991);
  if (_M0L6_2atmpS991.$1) {
    moonbit_decref(_M0L6_2atmpS991.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS88,
  int32_t _M0L3objS87
) {
  struct _M0TPB6Logger _M0L6_2atmpS992;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS88);
  _M0L6_2atmpS992
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS88
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS87, _M0L6_2atmpS992);
  if (_M0L6_2atmpS992.$1) {
    moonbit_decref(_M0L6_2atmpS992.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS90,
  uint64_t _M0L3objS89
) {
  struct _M0TPB6Logger _M0L6_2atmpS993;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS90);
  _M0L6_2atmpS993
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS90
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS89, _M0L6_2atmpS993);
  if (_M0L6_2atmpS993.$1) {
    moonbit_decref(_M0L6_2atmpS993.$1);
  }
  return 0;
}

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t* _M0L3srcS70,
  int32_t _M0L13allocate__lenS68,
  int32_t _M0L11src__offsetS71,
  int32_t _M0L11dst__offsetS69,
  int32_t _M0L9blit__lenS72
) {
  int32_t* _M0L3dstS67;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS67
  = (int32_t*)moonbit_make_int32_array_raw(_M0L13allocate__lenS68);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L3dstS67, _M0L11dst__offsetS69, _M0L3srcS70, _M0L11src__offsetS71, _M0L9blit__lenS72);
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t* _M0L3dstS52,
  int32_t _M0L11dst__offsetS53,
  int32_t* _M0L3srcS54,
  int32_t _M0L11src__offsetS55,
  int32_t _M0L3lenS56
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS54);
  moonbit_incref_cycle_free(_M0L3dstS52);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS52, _M0L11dst__offsetS53, _M0L3srcS54, _M0L11src__offsetS55, _M0L3lenS56, sizeof(int32_t));
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
        int32_t _M0L6_2atmpS955 = _M0L11dst__offsetS18 + _M0L1iS20;
        int32_t _M0L6_2atmpS957 = _M0L11src__offsetS19 + _M0L1iS20;
        int32_t _M0L6_2atmpS956;
        int32_t _M0L6_2atmpS958;
        if (
          _M0L6_2atmpS957 < 0
          || _M0L6_2atmpS957 >= Moonbit_array_length(_M0L3srcS17)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS956 = (int32_t)_M0L3srcS17[_M0L6_2atmpS957];
        if (
          _M0L6_2atmpS955 < 0
          || _M0L6_2atmpS955 >= Moonbit_array_length(_M0L3dstS16)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS16[_M0L6_2atmpS955] = _M0L6_2atmpS956;
        _M0L6_2atmpS958 = _M0L1iS20 + 1;
        _M0L1iS20 = _M0L6_2atmpS958;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS17);
        moonbit_decref_cycle_free(_M0L3dstS16);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS963 = _M0L3lenS21 - 1;
    int32_t _M0L1iS23 = _M0L6_2atmpS963;
    while (1) {
      if (_M0L1iS23 >= 0) {
        int32_t _M0L6_2atmpS959 = _M0L11dst__offsetS18 + _M0L1iS23;
        int32_t _M0L6_2atmpS961 = _M0L11src__offsetS19 + _M0L1iS23;
        int32_t _M0L6_2atmpS960;
        int32_t _M0L6_2atmpS962;
        if (
          _M0L6_2atmpS961 < 0
          || _M0L6_2atmpS961 >= Moonbit_array_length(_M0L3srcS17)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS960 = (int32_t)_M0L3srcS17[_M0L6_2atmpS961];
        if (
          _M0L6_2atmpS959 < 0
          || _M0L6_2atmpS959 >= Moonbit_array_length(_M0L3dstS16)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS16[_M0L6_2atmpS959] = _M0L6_2atmpS960;
        _M0L6_2atmpS962 = _M0L1iS23 - 1;
        _M0L1iS23 = _M0L6_2atmpS962;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS25,
  int32_t _M0L11dst__offsetS27,
  int32_t* _M0L3srcS26,
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
        int32_t _M0L6_2atmpS964 = _M0L11dst__offsetS27 + _M0L1iS29;
        int32_t _M0L6_2atmpS966 = _M0L11src__offsetS28 + _M0L1iS29;
        int32_t _M0L6_2atmpS965;
        int32_t _M0L6_2atmpS967;
        if (
          _M0L6_2atmpS966 < 0
          || _M0L6_2atmpS966 >= Moonbit_array_length(_M0L3srcS26)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS965 = (int32_t)_M0L3srcS26[_M0L6_2atmpS966];
        if (
          _M0L6_2atmpS964 < 0
          || _M0L6_2atmpS964 >= Moonbit_array_length(_M0L3dstS25)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS25[_M0L6_2atmpS964] = _M0L6_2atmpS965;
        _M0L6_2atmpS967 = _M0L1iS29 + 1;
        _M0L1iS29 = _M0L6_2atmpS967;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS26);
        moonbit_decref_cycle_free(_M0L3dstS25);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS972 = _M0L3lenS30 - 1;
    int32_t _M0L1iS32 = _M0L6_2atmpS972;
    while (1) {
      if (_M0L1iS32 >= 0) {
        int32_t _M0L6_2atmpS968 = _M0L11dst__offsetS27 + _M0L1iS32;
        int32_t _M0L6_2atmpS970 = _M0L11src__offsetS28 + _M0L1iS32;
        int32_t _M0L6_2atmpS969;
        int32_t _M0L6_2atmpS971;
        if (
          _M0L6_2atmpS970 < 0
          || _M0L6_2atmpS970 >= Moonbit_array_length(_M0L3srcS26)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS969 = (int32_t)_M0L3srcS26[_M0L6_2atmpS970];
        if (
          _M0L6_2atmpS968 < 0
          || _M0L6_2atmpS968 >= Moonbit_array_length(_M0L3dstS25)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS25[_M0L6_2atmpS968] = _M0L6_2atmpS969;
        _M0L6_2atmpS971 = _M0L1iS32 - 1;
        _M0L1iS32 = _M0L6_2atmpS971;
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
        int32_t _M0L6_2atmpS973 = _M0L11dst__offsetS36 + _M0L1iS38;
        int32_t _M0L6_2atmpS975 = _M0L11src__offsetS37 + _M0L1iS38;
        moonbit_string_t _M0L6_2atmpS974;
        moonbit_string_t _M0L6_2aoldS1900;
        int32_t _M0L6_2atmpS976;
        if (
          _M0L6_2atmpS975 < 0
          || _M0L6_2atmpS975 >= Moonbit_array_length(_M0L3srcS35)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS974 = (moonbit_string_t)_M0L3srcS35[_M0L6_2atmpS975];
        if (
          _M0L6_2atmpS973 < 0
          || _M0L6_2atmpS973 >= Moonbit_array_length(_M0L3dstS34)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1900 = (moonbit_string_t)_M0L3dstS34[_M0L6_2atmpS973];
        moonbit_incref_cycle_free(_M0L6_2atmpS974);
        moonbit_decref_cycle_free(_M0L6_2aoldS1900);
        _M0L3dstS34[_M0L6_2atmpS973] = _M0L6_2atmpS974;
        _M0L6_2atmpS976 = _M0L1iS38 + 1;
        _M0L1iS38 = _M0L6_2atmpS976;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS35);
        moonbit_decref_cycle_free(_M0L3dstS34);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS981 = _M0L3lenS39 - 1;
    int32_t _M0L1iS41 = _M0L6_2atmpS981;
    while (1) {
      if (_M0L1iS41 >= 0) {
        int32_t _M0L6_2atmpS977 = _M0L11dst__offsetS36 + _M0L1iS41;
        int32_t _M0L6_2atmpS979 = _M0L11src__offsetS37 + _M0L1iS41;
        moonbit_string_t _M0L6_2atmpS978;
        moonbit_string_t _M0L6_2aoldS1901;
        int32_t _M0L6_2atmpS980;
        if (
          _M0L6_2atmpS979 < 0
          || _M0L6_2atmpS979 >= Moonbit_array_length(_M0L3srcS35)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS978 = (moonbit_string_t)_M0L3srcS35[_M0L6_2atmpS979];
        if (
          _M0L6_2atmpS977 < 0
          || _M0L6_2atmpS977 >= Moonbit_array_length(_M0L3dstS34)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1901 = (moonbit_string_t)_M0L3dstS34[_M0L6_2atmpS977];
        moonbit_incref_cycle_free(_M0L6_2atmpS978);
        moonbit_decref_cycle_free(_M0L6_2aoldS1901);
        _M0L3dstS34[_M0L6_2atmpS977] = _M0L6_2atmpS978;
        _M0L6_2atmpS980 = _M0L1iS41 - 1;
        _M0L1iS41 = _M0L6_2atmpS980;
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
        int32_t _M0L6_2atmpS982 = _M0L11dst__offsetS45 + _M0L1iS47;
        int32_t _M0L6_2atmpS984 = _M0L11src__offsetS46 + _M0L1iS47;
        struct _M0TUsiE* _M0L6_2atmpS983;
        struct _M0TUsiE* _M0L6_2aoldS1902;
        int32_t _M0L6_2atmpS985;
        if (
          _M0L6_2atmpS984 < 0
          || _M0L6_2atmpS984 >= Moonbit_array_length(_M0L3srcS44)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS983 = (struct _M0TUsiE*)_M0L3srcS44[_M0L6_2atmpS984];
        if (
          _M0L6_2atmpS982 < 0
          || _M0L6_2atmpS982 >= Moonbit_array_length(_M0L3dstS43)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1902 = (struct _M0TUsiE*)_M0L3dstS43[_M0L6_2atmpS982];
        if (_M0L6_2atmpS983) {
          moonbit_incref_cycle_free(_M0L6_2atmpS983);
        }
        if (_M0L6_2aoldS1902) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1902);
        }
        _M0L3dstS43[_M0L6_2atmpS982] = _M0L6_2atmpS983;
        _M0L6_2atmpS985 = _M0L1iS47 + 1;
        _M0L1iS47 = _M0L6_2atmpS985;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS44);
        moonbit_decref_cycle_free(_M0L3dstS43);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS990 = _M0L3lenS48 - 1;
    int32_t _M0L1iS50 = _M0L6_2atmpS990;
    while (1) {
      if (_M0L1iS50 >= 0) {
        int32_t _M0L6_2atmpS986 = _M0L11dst__offsetS45 + _M0L1iS50;
        int32_t _M0L6_2atmpS988 = _M0L11src__offsetS46 + _M0L1iS50;
        struct _M0TUsiE* _M0L6_2atmpS987;
        struct _M0TUsiE* _M0L6_2aoldS1903;
        int32_t _M0L6_2atmpS989;
        if (
          _M0L6_2atmpS988 < 0
          || _M0L6_2atmpS988 >= Moonbit_array_length(_M0L3srcS44)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS987 = (struct _M0TUsiE*)_M0L3srcS44[_M0L6_2atmpS988];
        if (
          _M0L6_2atmpS986 < 0
          || _M0L6_2atmpS986 >= Moonbit_array_length(_M0L3dstS43)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1903 = (struct _M0TUsiE*)_M0L3dstS43[_M0L6_2atmpS986];
        if (_M0L6_2atmpS987) {
          moonbit_incref_cycle_free(_M0L6_2atmpS987);
        }
        if (_M0L6_2aoldS1903) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1903);
        }
        _M0L3dstS43[_M0L6_2atmpS986] = _M0L6_2atmpS987;
        _M0L6_2atmpS989 = _M0L1iS50 - 1;
        _M0L1iS50 = _M0L6_2atmpS989;
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

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t* _M0L4selfS13) {
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
  _M0L10_2ax__6388S12.$0->$method_0(_M0L10_2ax__6388S12.$1, (moonbit_string_t)moonbit_string_literal_34.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S12, _M0L15_2a_2aarg__6389S11);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S12.$0->$method_0(_M0L10_2ax__6388S12.$1, (moonbit_string_t)moonbit_string_literal_35.data);
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS925) {
  switch (Moonbit_object_tag(_M0L4_2aeS925)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_36.data;
      break;
    }
    
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_37.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS925);
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
  void* _M0L11_2aobj__ptrS950,
  struct _M0TPB4Show _M0L8_2aparamS949
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS948 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS950;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS948, _M0L8_2aparamS949);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS947,
  struct _M0TPB4Show _M0L8_2aparamS946
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS945 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS947;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS945, _M0L8_2aparamS946);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS944,
  int32_t _M0L8_2aparamS943
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS942 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS944;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS942, _M0L8_2aparamS943);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS941,
  struct _M0TPC16string10StringView _M0L8_2aparamS940
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS939 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS941;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS939, _M0L8_2aparamS940);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS938,
  moonbit_string_t _M0L8_2aparamS935,
  int32_t _M0L8_2aparamS936,
  int32_t _M0L8_2aparamS937
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS934 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS938;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS934, _M0L8_2aparamS935, _M0L8_2aparamS936, _M0L8_2aparamS937);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS933,
  moonbit_string_t _M0L8_2aparamS932
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS931 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS933;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS931, _M0L8_2aparamS932);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS954;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS918;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS919;
  int32_t _M0L7_2abindS920;
  struct _M0TUsiE** _M0L7_2abindS921;
  int32_t _M0L6_2acntS1912;
  int32_t _M0L2__S922;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS954
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS918
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS918)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 60, 0);
  _M0L12async__testsS918->$0 = _M0L6_2atmpS954;
  _M0L12async__testsS918->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS919
  = _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS920 = _M0L7_2abindS919->$1;
  _M0L7_2abindS921 = _M0L7_2abindS919->$0;
  _M0L6_2acntS1912
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS919));
  if (_M0L6_2acntS1912 > 1) {
    int32_t _M0L11_2anew__cntS1913 = _M0L6_2acntS1912 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS919), _M0L11_2anew__cntS1913);
    moonbit_incref_cycle_free(_M0L7_2abindS921);
  } else if (_M0L6_2acntS1912 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS919);
  }
  _M0L2__S922 = 0;
  while (1) {
    if (_M0L2__S922 < _M0L7_2abindS920) {
      struct _M0TUsiE* _M0L3argS923 =
        (struct _M0TUsiE*)_M0L7_2abindS921[_M0L2__S922];
      moonbit_string_t _M0L6_2atmpS951 = _M0L3argS923->$0;
      int32_t _M0L6_2atmpS952 = _M0L3argS923->$1;
      int32_t _M0L6_2atmpS953;
      moonbit_incref_cycle_free(_M0L6_2atmpS951);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS918, _M0L6_2atmpS951, _M0L6_2atmpS952);
      moonbit_decref_cycle_free(_M0L6_2atmpS951);
      _M0L6_2atmpS953 = _M0L2__S922 + 1;
      _M0L2__S922 = _M0L6_2atmpS953;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS921);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\receptors_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples31receptors__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS918);
  moonbit_decref_cycle_free(_M0L12async__testsS918);
  moonbit_flush_cycles();
  return 0;
}