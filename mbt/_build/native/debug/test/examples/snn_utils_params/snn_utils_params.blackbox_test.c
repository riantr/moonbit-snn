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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TPB8MutLocalGiE;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TWRPC15error5ErrorEs;

struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c741;

struct _M0TPB4Show;

struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c746;

struct _M0TWssbEu;

struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB17FloatingDecimal64;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0DTPC15error5Error135RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB6Logger;

struct _M0TP26RiantR8snn__mbt10Duarte2019;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0TPB5ArrayGsE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7LKD2014;

struct _M0TWEu;

struct _M0TPB19MulShiftAll64Result;

struct _M0TP26RiantR8snn__mbt11IFParameter;

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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
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

struct _M0TWRPC15error5ErrorEs {
  moonbit_string_t(* code)(struct _M0TWRPC15error5ErrorEs*, void*);
  
};

struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c741 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0TPB4Show {
  struct _M0BTPB4Show* $0;
  void* $1;
  
};

struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c746 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0TWssbEu {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  
};

struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
};

struct _M0TUsiE {
  moonbit_string_t $0;
  int32_t $1;
  
};

struct _M0TPB13StringBuilder {
  uint16_t* $0;
  int32_t $1;
  
};

struct _M0TPB17FloatingDecimal64 {
  uint64_t $0;
  int32_t $1;
  
};

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error {
  struct moonbit_result_0(* code)(
    struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error*,
    struct _M0TWuEu*,
    struct _M0TWRPC15error5ErrorEu*
  );
  
};

struct _M0DTPC15error5Error135RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
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

struct _M0TP26RiantR8snn__mbt10Duarte2019 {
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
  float $16;
  float $17;
  float $18;
  float $19;
  float $20;
  float $21;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok {
  int32_t $0;
  
};

struct _M0TP26RiantR8snn__mbt7LKD2014 {
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
  
};

struct _M0TWEu {
  int32_t(* code)(struct _M0TWEu*);
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS753(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS746(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS741(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS718(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S711(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter6custom(
  float,
  float,
  float,
  float,
  float
);

struct _M0TP26RiantR8snn__mbt10Duarte2019* _M0MP26RiantR8snn__mbt10Duarte20193new(
  
);

struct _M0TP26RiantR8snn__mbt7LKD2014* _M0MP26RiantR8snn__mbt7LKD20143new();

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[123]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 122, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 115, 110, 110, 95, 117, 116, 105, 
    108, 115, 95, 112, 97, 114, 97, 109, 115, 95, 98, 108, 97, 99, 107, 
    98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 
    105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 
    116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 
    46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 
    105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 
    105, 112, 84, 101, 115, 116, 0
  };

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

struct { int32_t rc; uint32_t meta; uint16_t const data[121]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 120, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 115, 110, 110, 95, 117, 116, 105, 
    108, 115, 95, 112, 97, 114, 97, 109, 115, 95, 98, 108, 97, 99, 107, 
    98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 
    105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 
    116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 46, 
    77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 
    118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 
    114, 114, 111, 114, 0
  };

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

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS753$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS753
  };

uint32_t const moonbit_layout_table_data[36] =
  {
    sizeof(struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c741)
    / 4, 1,
    offsetof(struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c741, $1)
    / 4
    * 2,
    sizeof(struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c746)
    / 4, 1,
    offsetof(struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c746, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS1634
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS774,
  moonbit_string_t _M0L8filenameS743,
  int32_t _M0L5indexS745
) {
  struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c741* _closure_1654;
  struct _M0TWEu* _M0L13handle__startS741;
  struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c746* _closure_1655;
  struct _M0TWssbEu* _M0L14handle__resultS746;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS753;
  void* _M0L11_2atry__errS768;
  struct moonbit_result_0 _tmp_1657;
  int32_t _handle__error__result_1658;
  int32_t _M0L6_2atmpS1622;
  void* _M0L3errS769;
  moonbit_string_t _M0L4nameS771;
  struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS772;
  moonbit_string_t _M0L7_2anameS773;
  int32_t _M0L6_2acntS1648;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS743);
  _closure_1654
  = (struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c741*)moonbit_malloc(sizeof(struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c741));
  Moonbit_object_header(_closure_1654)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_1654->code
  = &_M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS741;
  _closure_1654->$0 = _M0L5indexS745;
  _closure_1654->$1 = _M0L8filenameS743;
  _M0L13handle__startS741 = (struct _M0TWEu*)_closure_1654;
  moonbit_incref_cycle_free(_M0L8filenameS743);
  _closure_1655
  = (struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c746*)moonbit_malloc(sizeof(struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c746));
  Moonbit_object_header(_closure_1655)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_1655->code
  = &_M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS746;
  _closure_1655->$0 = _M0L5indexS745;
  _closure_1655->$1 = _M0L8filenameS743;
  _M0L14handle__resultS746 = (struct _M0TWssbEu*)_closure_1655;
  _M0L17error__to__stringS753
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS753$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _tmp_1657
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS774, _M0L8filenameS743, _M0L5indexS745, _M0L13handle__startS741, _M0L14handle__resultS746, _M0L17error__to__stringS753);
  if (_tmp_1657.tag) {
    int32_t const _M0L5_2aokS1631 = _tmp_1657.data.ok;
    _handle__error__result_1658 = _M0L5_2aokS1631;
  } else {
    void* const _M0L6_2aerrS1632 = _tmp_1657.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS753);
    moonbit_decref_cycle_free(_M0L13handle__startS741);
    _M0L11_2atry__errS768 = _M0L6_2aerrS1632;
    goto join_767;
  }
  if (_handle__error__result_1658) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS753);
    moonbit_decref_cycle_free(_M0L13handle__startS741);
    _M0L6_2atmpS1622 = 1;
  } else {
    struct moonbit_result_0 _tmp_1659;
    int32_t _handle__error__result_1660;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
    _tmp_1659
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS774, _M0L8filenameS743, _M0L5indexS745, _M0L13handle__startS741, _M0L14handle__resultS746, _M0L17error__to__stringS753);
    if (_tmp_1659.tag) {
      int32_t const _M0L5_2aokS1629 = _tmp_1659.data.ok;
      _handle__error__result_1660 = _M0L5_2aokS1629;
    } else {
      void* const _M0L6_2aerrS1630 = _tmp_1659.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS753);
      moonbit_decref_cycle_free(_M0L13handle__startS741);
      _M0L11_2atry__errS768 = _M0L6_2aerrS1630;
      goto join_767;
    }
    if (_handle__error__result_1660) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS753);
      moonbit_decref_cycle_free(_M0L13handle__startS741);
      _M0L6_2atmpS1622 = 1;
    } else {
      struct moonbit_result_0 _tmp_1661;
      int32_t _handle__error__result_1662;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
      _tmp_1661
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS774, _M0L8filenameS743, _M0L5indexS745, _M0L13handle__startS741, _M0L14handle__resultS746, _M0L17error__to__stringS753);
      if (_tmp_1661.tag) {
        int32_t const _M0L5_2aokS1627 = _tmp_1661.data.ok;
        _handle__error__result_1662 = _M0L5_2aokS1627;
      } else {
        void* const _M0L6_2aerrS1628 = _tmp_1661.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS753);
        moonbit_decref_cycle_free(_M0L13handle__startS741);
        _M0L11_2atry__errS768 = _M0L6_2aerrS1628;
        goto join_767;
      }
      if (_handle__error__result_1662) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS753);
        moonbit_decref_cycle_free(_M0L13handle__startS741);
        _M0L6_2atmpS1622 = 1;
      } else {
        struct moonbit_result_0 _tmp_1663;
        int32_t _handle__error__result_1664;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
        _tmp_1663
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS774, _M0L8filenameS743, _M0L5indexS745, _M0L13handle__startS741, _M0L14handle__resultS746, _M0L17error__to__stringS753);
        if (_tmp_1663.tag) {
          int32_t const _M0L5_2aokS1625 = _tmp_1663.data.ok;
          _handle__error__result_1664 = _M0L5_2aokS1625;
        } else {
          void* const _M0L6_2aerrS1626 = _tmp_1663.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS753);
          moonbit_decref_cycle_free(_M0L13handle__startS741);
          _M0L11_2atry__errS768 = _M0L6_2aerrS1626;
          goto join_767;
        }
        if (_handle__error__result_1664) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS753);
          moonbit_decref_cycle_free(_M0L13handle__startS741);
          _M0L6_2atmpS1622 = 1;
        } else {
          struct moonbit_result_0 _tmp_1665;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
          _tmp_1665
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS774, _M0L8filenameS743, _M0L5indexS745, _M0L13handle__startS741, _M0L14handle__resultS746, _M0L17error__to__stringS753);
          moonbit_decref_cycle_free(_M0L13handle__startS741);
          moonbit_decref_cycle_free(_M0L17error__to__stringS753);
          if (_tmp_1665.tag) {
            int32_t const _M0L5_2aokS1623 = _tmp_1665.data.ok;
            _M0L6_2atmpS1622 = _M0L5_2aokS1623;
          } else {
            void* const _M0L6_2aerrS1624 = _tmp_1665.data.err;
            _M0L11_2atry__errS768 = _M0L6_2aerrS1624;
            goto join_767;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS1622) {
    void* _M0L137RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1633 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L137RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1633)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L137RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1633)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS768
    = _M0L137RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1633;
    goto join_767;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS746);
  }
  goto joinlet_1656;
  join_767:;
  _M0L3errS769 = _M0L11_2atry__errS768;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS772
  = (struct _M0DTPC15error5Error137RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS769;
  _M0L7_2anameS773 = _M0L36_2aMoonBitTestDriverInternalSkipTestS772->$0;
  _M0L6_2acntS1648
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS772));
  if (_M0L6_2acntS1648 > 1) {
    int32_t _M0L11_2anew__cntS1649 = _M0L6_2acntS1648 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS772), _M0L11_2anew__cntS1649);
    moonbit_incref_cycle_free(_M0L7_2anameS773);
  } else if (_M0L6_2acntS1648 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS772);
  }
  _M0L4nameS771 = _M0L7_2anameS773;
  goto join_770;
  goto joinlet_1666;
  join_770:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS746(_M0L14handle__resultS746, _M0L4nameS771, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS746);
  moonbit_decref_cycle_free(_M0L4nameS771);
  joinlet_1666:;
  joinlet_1656:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS753(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS1621,
  void* _M0L3errS754
) {
  void* _M0L1eS756;
  moonbit_string_t _M0L1eS758;
  moonbit_string_t _result_1669;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS754)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS759 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS754;
      moonbit_string_t _M0L4_2aeS760 = _M0L10_2aFailureS759->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS760);
      _M0L1eS758 = _M0L4_2aeS760;
      goto join_757;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS761 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS754;
      moonbit_string_t _M0L4_2aeS762 = _M0L15_2aInspectErrorS761->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS762);
      _M0L1eS758 = _M0L4_2aeS762;
      goto join_757;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS763 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS754;
      moonbit_string_t _M0L4_2aeS764 = _M0L16_2aSnapshotErrorS763->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS764);
      _M0L1eS758 = _M0L4_2aeS764;
      goto join_757;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error135RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS765 =
        (struct _M0DTPC15error5Error135RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS754;
      moonbit_string_t _M0L4_2aeS766 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS765->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS766);
      _M0L1eS758 = _M0L4_2aeS766;
      goto join_757;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS754);
      _M0L1eS756 = _M0L3errS754;
      goto join_755;
      break;
    }
  }
  join_757:;
  return _M0L1eS758;
  join_755:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _result_1669 = _M0FP15Error10to__string(_M0L1eS756);
  moonbit_decref_cycle_free(_M0L1eS756);
  return _result_1669;
}

int32_t _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS746(
  struct _M0TWssbEu* _M0L6_2aenvS1618,
  moonbit_string_t _M0L10__testnameS747,
  moonbit_string_t _M0L7messageS748,
  int32_t _M0L7skippedS749
) {
  struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c746* _M0L14_2acasted__envS1619;
  moonbit_string_t _M0L8filenameS743;
  int32_t _M0L5indexS745;
  moonbit_string_t _M0L10file__nameS750;
  moonbit_string_t _M0L7messageS751;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS752;
  moonbit_string_t _M0L6_2atmpS1620;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1619
  = (struct _M0R138_24RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c746*)_M0L6_2aenvS1618;
  _M0L8filenameS743 = _M0L14_2acasted__envS1619->$1;
  _M0L5indexS745 = _M0L14_2acasted__envS1619->$0;
  if (!_M0L7skippedS749 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS750
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS743, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS751
  = _M0MPC16string6String14escape_2einner(_M0L7messageS748, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS752
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS752, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS752, _M0L10file__nameS750);
  moonbit_decref_cycle_free(_M0L10file__nameS750);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS752, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS752, _M0L5indexS745);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS752, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS752, _M0L7messageS751);
  moonbit_decref_cycle_free(_M0L7messageS751);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS752, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1620
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS752);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS752);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1620);
  moonbit_decref_cycle_free(_M0L6_2atmpS1620);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS741(
  struct _M0TWEu* _M0L6_2aenvS1615
) {
  struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c741* _M0L14_2acasted__envS1616;
  moonbit_string_t _M0L8filenameS743;
  int32_t _M0L5indexS745;
  moonbit_string_t _M0L10file__nameS742;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS744;
  moonbit_string_t _M0L6_2atmpS1617;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1616
  = (struct _M0R137_24RiantR_2fsnn__mbt_2fexamples_2fsnn__utils__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c741*)_M0L6_2aenvS1615;
  _M0L8filenameS743 = _M0L14_2acasted__envS1616->$1;
  _M0L5indexS745 = _M0L14_2acasted__envS1616->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS742
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS743, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS744
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS744, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS744, _M0L10file__nameS742);
  moonbit_decref_cycle_free(_M0L10file__nameS742);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS744, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS744, _M0L5indexS745);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS744, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1617
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS744);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS744);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1617);
  moonbit_decref_cycle_free(_M0L6_2atmpS1617);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S711;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS718;
  struct _M0TUsiE** _M0L6_2atmpS1614;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS725;
  moonbit_string_t* _M0L9cli__argsS726;
  moonbit_string_t _M0L6_2atmpS1613;
  moonbit_string_t _M0L6_2atmpS1612;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS727;
  int32_t _M0L7_2abindS728;
  moonbit_string_t* _M0L7_2abindS729;
  int32_t _M0L6_2acntS1650;
  int32_t _M0L2__S730;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S711 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS718 = 0;
  _M0L6_2atmpS1614 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS725
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS725)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS725->$0 = _M0L6_2atmpS1614;
  _M0L16file__and__indexS725->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS726
  = _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS726)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS1613 = (moonbit_string_t)_M0L9cli__argsS726[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS1613);
  moonbit_decref_cycle_free(_M0L9cli__argsS726);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1612
  = _M0MP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS1613);
  moonbit_decref_cycle_free(_M0L6_2atmpS1613);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS727
  = _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS718(_M0L51moonbit__test__driver__internal__split__mbt__stringS718, _M0L6_2atmpS1612, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS1612);
  _M0L7_2abindS728 = _M0L10test__argsS727->$1;
  _M0L7_2abindS729 = _M0L10test__argsS727->$0;
  _M0L6_2acntS1650
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS727));
  if (_M0L6_2acntS1650 > 1) {
    int32_t _M0L11_2anew__cntS1651 = _M0L6_2acntS1650 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS727), _M0L11_2anew__cntS1651);
    moonbit_incref_cycle_free(_M0L7_2abindS729);
  } else if (_M0L6_2acntS1650 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS727);
  }
  _M0L2__S730 = 0;
  while (1) {
    if (_M0L2__S730 < _M0L7_2abindS728) {
      moonbit_string_t _M0L3argS731 =
        (moonbit_string_t)_M0L7_2abindS729[_M0L2__S730];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS732;
      moonbit_string_t _M0L4fileS733;
      moonbit_string_t _M0L5rangeS734;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS735;
      moonbit_string_t _M0L6_2atmpS1610;
      int32_t _M0L5startS736;
      moonbit_string_t _M0L6_2atmpS1609;
      int32_t _M0L3endS737;
      int32_t _M0L1iS738;
      int32_t _M0L6_2atmpS1611;
      moonbit_incref_cycle_free(_M0L3argS731);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS732
      = _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS718(_M0L51moonbit__test__driver__internal__split__mbt__stringS718, _M0L3argS731, 58);
      moonbit_decref_cycle_free(_M0L3argS731);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS733
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS732, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS734
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS732, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS732);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS735
      = _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS718(_M0L51moonbit__test__driver__internal__split__mbt__stringS718, _M0L5rangeS734, 45);
      moonbit_decref_cycle_free(_M0L5rangeS734);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1610
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS735, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS736
      = _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S711(_M0L45moonbit__test__driver__internal__parse__int__S711, _M0L6_2atmpS1610);
      moonbit_decref_cycle_free(_M0L6_2atmpS1610);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1609
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS735, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS735);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS737
      = _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S711(_M0L45moonbit__test__driver__internal__parse__int__S711, _M0L6_2atmpS1609);
      moonbit_decref_cycle_free(_M0L6_2atmpS1609);
      _M0L1iS738 = _M0L5startS736;
      while (1) {
        if (_M0L1iS738 < _M0L3endS737) {
          struct _M0TUsiE* _M0L8_2atupleS1607;
          int32_t _M0L6_2atmpS1608;
          moonbit_incref_cycle_free(_M0L4fileS733);
          _M0L8_2atupleS1607
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS1607)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS1607->$0 = _M0L4fileS733;
          _M0L8_2atupleS1607->$1 = _M0L1iS738;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS725, _M0L8_2atupleS1607);
          _M0L6_2atmpS1608 = _M0L1iS738 + 1;
          _M0L1iS738 = _M0L6_2atmpS1608;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS733);
        }
        break;
      }
      _M0L6_2atmpS1611 = _M0L2__S730 + 1;
      _M0L2__S730 = _M0L6_2atmpS1611;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS729);
    }
    break;
  }
  return _M0L16file__and__indexS725;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS718(
  int32_t _M0L6_2aenvS1588,
  moonbit_string_t _M0L1sS719,
  int32_t _M0L3sepS720
) {
  moonbit_string_t* _M0L6_2atmpS1606;
  struct _M0TPB5ArrayGsE* _M0L3resS721;
  struct _M0TPB8MutLocalGiE* _M0L1iS722;
  struct _M0TPB8MutLocalGiE* _M0L5startS723;
  int32_t _M0L3valS1601;
  int32_t _M0L6_2atmpS1602;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1606 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS721
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS721)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS721->$0 = _M0L6_2atmpS1606;
  _M0L3resS721->$1 = 0;
  _M0L1iS722
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS722)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS722->$0 = 0;
  _M0L5startS723
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS723)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS723->$0 = 0;
  while (1) {
    int32_t _M0L3valS1589 = _M0L1iS722->$0;
    int32_t _M0L6_2atmpS1590 = Moonbit_array_length(_M0L1sS719);
    if (_M0L3valS1589 < _M0L6_2atmpS1590) {
      int32_t _M0L3valS1593 = _M0L1iS722->$0;
      int32_t _M0L6_2atmpS1592;
      int32_t _M0L6_2atmpS1591;
      int32_t _M0L3valS1600;
      int32_t _M0L6_2atmpS1599;
      if (
        _M0L3valS1593 < 0
        || _M0L3valS1593 >= Moonbit_array_length(_M0L1sS719)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1592 = _M0L1sS719[_M0L3valS1593];
      _M0L6_2atmpS1591 = _M0L6_2atmpS1592;
      if (_M0L6_2atmpS1591 == _M0L3sepS720) {
        int32_t _M0L3valS1595 = _M0L5startS723->$0;
        int32_t _M0L3valS1596 = _M0L1iS722->$0;
        moonbit_string_t _M0L6_2atmpS1594;
        int32_t _M0L3valS1598;
        int32_t _M0L6_2atmpS1597;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS1594
        = _M0MPC16string6String17unsafe__substring(_M0L1sS719, _M0L3valS1595, _M0L3valS1596);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS721, _M0L6_2atmpS1594);
        _M0L3valS1598 = _M0L1iS722->$0;
        _M0L6_2atmpS1597 = _M0L3valS1598 + 1;
        _M0L5startS723->$0 = _M0L6_2atmpS1597;
      }
      _M0L3valS1600 = _M0L1iS722->$0;
      _M0L6_2atmpS1599 = _M0L3valS1600 + 1;
      _M0L1iS722->$0 = _M0L6_2atmpS1599;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS722);
    }
    break;
  }
  _M0L3valS1601 = _M0L5startS723->$0;
  _M0L6_2atmpS1602 = Moonbit_array_length(_M0L1sS719);
  if (_M0L3valS1601 < _M0L6_2atmpS1602) {
    int32_t _M0L3valS1604 = _M0L5startS723->$0;
    int32_t _M0L6_2atmpS1605;
    moonbit_string_t _M0L6_2atmpS1603;
    moonbit_decref_cycle_free(_M0L5startS723);
    _M0L6_2atmpS1605 = Moonbit_array_length(_M0L1sS719);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS1603
    = _M0MPC16string6String17unsafe__substring(_M0L1sS719, _M0L3valS1604, _M0L6_2atmpS1605);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS721, _M0L6_2atmpS1603);
  } else {
    moonbit_decref_cycle_free(_M0L5startS723);
  }
  return _M0L3resS721;
}

int32_t _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S711(
  int32_t _M0L6_2aenvS1581,
  moonbit_string_t _M0L1sS712
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS713;
  int32_t _M0L3lenS714;
  int32_t _M0L7_2abindS715;
  int32_t _M0L1iS716;
  int32_t _result_1674;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS713
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS713)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS713->$0 = 0;
  _M0L3lenS714 = Moonbit_array_length(_M0L1sS712);
  _M0L7_2abindS715 = 0;
  _M0L1iS716 = _M0L7_2abindS715;
  while (1) {
    if (_M0L1iS716 < _M0L3lenS714) {
      int32_t _M0L3valS1586 = _M0L3resS713->$0;
      int32_t _M0L6_2atmpS1583 = _M0L3valS1586 * 10;
      int32_t _M0L6_2atmpS1585;
      int32_t _M0L6_2atmpS1584;
      int32_t _M0L6_2atmpS1582;
      int32_t _M0L6_2atmpS1587;
      if (_M0L1iS716 < 0 || _M0L1iS716 >= Moonbit_array_length(_M0L1sS712)) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1585 = _M0L1sS712[_M0L1iS716];
      _M0L6_2atmpS1584 = _M0L6_2atmpS1585 - 48;
      _M0L6_2atmpS1582 = _M0L6_2atmpS1583 + _M0L6_2atmpS1584;
      _M0L3resS713->$0 = _M0L6_2atmpS1582;
      _M0L6_2atmpS1587 = _M0L1iS716 + 1;
      _M0L1iS716 = _M0L6_2atmpS1587;
      continue;
    }
    break;
  }
  _result_1674 = _M0L3resS713->$0;
  moonbit_decref_cycle_free(_M0L3resS713);
  return _result_1674;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS710
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS710);
  return _M0L4selfS710;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S680,
  moonbit_string_t _M0L12_2adiscard__S681,
  int32_t _M0L12_2adiscard__S682,
  struct _M0TWEu* _M0L12_2adiscard__S683,
  struct _M0TWssbEu* _M0L12_2adiscard__S684,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S685
) {
  struct moonbit_result_0 _result_1675;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _result_1675.tag = 1;
  _result_1675.data.ok = 0;
  return _result_1675;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S686,
  moonbit_string_t _M0L12_2adiscard__S687,
  int32_t _M0L12_2adiscard__S688,
  struct _M0TWEu* _M0L12_2adiscard__S689,
  struct _M0TWssbEu* _M0L12_2adiscard__S690,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S691
) {
  struct moonbit_result_0 _result_1676;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _result_1676.tag = 1;
  _result_1676.data.ok = 0;
  return _result_1676;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S692,
  moonbit_string_t _M0L12_2adiscard__S693,
  int32_t _M0L12_2adiscard__S694,
  struct _M0TWEu* _M0L12_2adiscard__S695,
  struct _M0TWssbEu* _M0L12_2adiscard__S696,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S697
) {
  struct moonbit_result_0 _result_1677;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _result_1677.tag = 1;
  _result_1677.data.ok = 0;
  return _result_1677;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S698,
  moonbit_string_t _M0L12_2adiscard__S699,
  int32_t _M0L12_2adiscard__S700,
  struct _M0TWEu* _M0L12_2adiscard__S701,
  struct _M0TWssbEu* _M0L12_2adiscard__S702,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S703
) {
  struct moonbit_result_0 _result_1678;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _result_1678.tag = 1;
  _result_1678.data.ok = 0;
  return _result_1678;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S704,
  moonbit_string_t _M0L12_2adiscard__S705,
  int32_t _M0L12_2adiscard__S706,
  struct _M0TWEu* _M0L12_2adiscard__S707,
  struct _M0TWssbEu* _M0L12_2adiscard__S708,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S709
) {
  struct moonbit_result_0 _result_1679;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _result_1679.tag = 1;
  _result_1679.data.ok = 0;
  return _result_1679;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S679
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter6custom(
  float _M0L2tmS671,
  float _M0L2vtS672,
  float _M0L2vrS673,
  float _M0L2elS674,
  float _M0L1rS675
) {
  float _M0L1cS669;
  float _M0L2glS670;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_1680;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS669 = -0x1p+0f;
  _M0L2glS670 = -0x1p+0f;
  _block_1680
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_1680)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1680->$0 = _M0L1cS669;
  _block_1680->$1 = _M0L2glS670;
  _block_1680->$2 = _M0L2tmS671;
  _block_1680->$3 = _M0L2vtS672;
  _block_1680->$4 = _M0L2vrS673;
  _block_1680->$5 = _M0L2elS674;
  _block_1680->$6 = _M0L1rS675;
  _block_1680->$7 = 0x1p+1f;
  _block_1680->$8 = 0x0p+0f;
  _block_1680->$9 = 0x0p+0f;
  _block_1680->$10 = 0x0p+0f;
  return _block_1680;
}

struct _M0TP26RiantR8snn__mbt10Duarte2019* _M0MP26RiantR8snn__mbt10Duarte20193new(
  
) {
  struct _M0TP26RiantR8snn__mbt10Duarte2019* _block_1681;
  #line 105 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\snn_utils_params.mbt"
  _block_1681
  = (struct _M0TP26RiantR8snn__mbt10Duarte2019*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt10Duarte2019));
  Moonbit_object_header(_block_1681)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1681->$0 = 0x1.570a3d70a3d71p+3f;
  _block_1681->$1 = -0x1.0151eb851eb85p+6f;
  _block_1681->$2 = -0x1.37c28f5c28f5cp+5f;
  _block_1681->$3 = -0x1.cbc28f5c28f5cp+5f;
  _block_1681->$4 = 0x1p-1f;
  _block_1681->$5 = 0x1.0a3d70a3d70a4p+0f;
  _block_1681->$6 = 0x1.ae147ae147ae1p-1f;
  _block_1681->$7 = 0x1.64f5c28f5c28fp+4f;
  _block_1681->$8 = -0x1.e8p+5f;
  _block_1681->$9 = -0x1.1333333333333p+5f;
  _block_1681->$10 = -0x1.78e147ae147aep+5f;
  _block_1681->$11 = 0x1.4cccccccccccdp+0f;
  _block_1681->$12 = 0x1.1eb851eb851ecp-1f;
  _block_1681->$13 = 0x1.2e147ae147ae1p-1f;
  _block_1681->$14 = 0x1.42p+6f;
  _block_1681->$15 = 0x1.2p+7f;
  _block_1681->$16 = 0x1.91c28f5c28f5cp+4f;
  _block_1681->$17 = -0x1.31b851eb851ecp+6f;
  _block_1681->$18 = -0x1.639999999999ap+5f;
  _block_1681->$19 = 0x1.0666666666666p+1f;
  _block_1681->$20 = 0x1.75c28f5c28f5cp-1f;
  _block_1681->$21 = 0x1.0f5c28f5c28f6p-2f;
  return _block_1681;
}

struct _M0TP26RiantR8snn__mbt7LKD2014* _M0MP26RiantR8snn__mbt7LKD20143new() {
  struct _M0TP26RiantR8snn__mbt7LKD2014* _block_1682;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\snn_utils_params.mbt"
  _block_1682
  = (struct _M0TP26RiantR8snn__mbt7LKD2014*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7LKD2014));
  Moonbit_object_header(_block_1682)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1682->$0 = 0x1.4p+4f;
  _block_1682->$1 = -0x1.ap+5f;
  _block_1682->$2 = -0x1.18p+6f;
  _block_1682->$3 = -0x1.ep+5f;
  _block_1682->$4 = 0x1.113404ea4a8c1p-4f;
  _block_1682->$5 = 0x1p+0f;
  _block_1682->$6 = -0x1.2cp+6f;
  _block_1682->$7 = 0x0p+0f;
  _block_1682->$8 = 0x1.4p+3f;
  _block_1682->$9 = 0x1.4p+4f;
  _block_1682->$10 = -0x1.fp+5f;
  _block_1682->$11 = -0x1.cbc28f5c28f5cp+5f;
  _block_1682->$12 = -0x1.ap+5f;
  return _block_1682;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS668) {
  double _M0L6_2atmpS1580;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1580 = (double)_M0L4selfS668;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1580);
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS666,
  int32_t _M0L5indexS667
) {
  int32_t _M0L3lenS665;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS665 = _M0L4selfS666->$1;
  if (_M0L5indexS667 >= 0 && _M0L5indexS667 < _M0L3lenS665) {
    moonbit_string_t* _M0L6_2atmpS1579;
    moonbit_string_t _M0L6_2atmpS1635;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1579 = _M0MPC15array5Array6bufferGsE(_M0L4selfS666);
    _M0L6_2atmpS1635 = (moonbit_string_t)_M0L6_2atmpS1579[_M0L5indexS667];
    moonbit_incref_cycle_free(_M0L6_2atmpS1635);
    moonbit_decref_cycle_free(_M0L6_2atmpS1579);
    return _M0L6_2atmpS1635;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS664) {
  moonbit_string_t _M0L6_2atmpS1578;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1578 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS664);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1578);
  moonbit_decref_cycle_free(_M0L6_2atmpS1578);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS663) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS663);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS648) {
  uint64_t _M0L4bitsS651;
  uint64_t _M0L6_2atmpS1577;
  uint64_t _M0L6_2atmpS1576;
  int32_t _M0L8ieeeSignS652;
  uint64_t _M0L12ieeeMantissaS653;
  uint64_t _M0L6_2atmpS1575;
  uint64_t _M0L6_2atmpS1574;
  int32_t _M0L12ieeeExponentS654;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS655;
  struct _M0TPB17FloatingDecimal64* _M0L1vS656;
  moonbit_string_t _result_1684;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS648 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  if (_M0L3valS648 >= -0x1p+53 && _M0L3valS648 <= 0x1p+53) {
    if (_M0L3valS648 >= -0x1p+31 && _M0L3valS648 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS649;
      double _M0L6_2atmpS1563;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS649 = _M0MPC16double6Double7to__int(_M0L3valS648);
      _M0L6_2atmpS1563 = (double)_M0L1iS649;
      if (_M0L6_2atmpS1563 == _M0L3valS648) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS649, 10);
      }
    } else {
      int64_t _M0L1iS650;
      double _M0L6_2atmpS1564;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS650 = _M0MPC16double6Double9to__int64(_M0L3valS648);
      _M0L6_2atmpS1564 = (double)_M0L1iS650;
      if (_M0L6_2atmpS1564 == _M0L3valS648) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS650, 10);
      }
    }
  }
  _M0L4bitsS651 = *(int64_t*)&_M0L3valS648;
  _M0L6_2atmpS1577 = _M0L4bitsS651 >> 63;
  _M0L6_2atmpS1576 = _M0L6_2atmpS1577 & 1ull;
  _M0L8ieeeSignS652 = _M0L6_2atmpS1576 != 0ull;
  _M0L12ieeeMantissaS653 = _M0L4bitsS651 & 4503599627370495ull;
  _M0L6_2atmpS1575 = _M0L4bitsS651 >> 52;
  _M0L6_2atmpS1574 = _M0L6_2atmpS1575 & 2047ull;
  _M0L12ieeeExponentS654 = (int32_t)_M0L6_2atmpS1574;
  if (
    _M0L12ieeeExponentS654 == 2047
    || _M0L12ieeeExponentS654 == 0 && _M0L12ieeeMantissaS653 == 0ull
  ) {
    int32_t _M0L6_2atmpS1565 = _M0L12ieeeExponentS654 != 0;
    int32_t _M0L6_2atmpS1566 = _M0L12ieeeMantissaS653 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS652, _M0L6_2atmpS1565, _M0L6_2atmpS1566);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS655
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS653, _M0L12ieeeExponentS654);
  if (_M0L7_2abindS655 == 0) {
    uint32_t _M0L6_2atmpS1567;
    if (_M0L7_2abindS655) {
      moonbit_decref_cycle_free(_M0L7_2abindS655);
    }
    _M0L6_2atmpS1567 = *(uint32_t*)&_M0L12ieeeExponentS654;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS656 = _M0FPB3d2d(_M0L12ieeeMantissaS653, _M0L6_2atmpS1567);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS657 = _M0L7_2abindS655;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS658 = _M0L7_2aSomeS657;
    struct _M0TPB17FloatingDecimal64* _M0L1xS659 = _M0L4_2afS658;
    while (1) {
      uint64_t _M0L8mantissaS1573 = _M0L1xS659->$0;
      uint64_t _M0L1qS660 = _M0L8mantissaS1573 / 10ull;
      uint64_t _M0L8mantissaS1571 = _M0L1xS659->$0;
      uint64_t _M0L6_2atmpS1572 = 10ull * _M0L1qS660;
      uint64_t _M0L1rS661 = _M0L8mantissaS1571 - _M0L6_2atmpS1572;
      int32_t _M0L8exponentS1570;
      int32_t _M0L6_2atmpS1569;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1568;
      if (_M0L1rS661 != 0ull) {
        _M0L1vS656 = _M0L1xS659;
        break;
      }
      _M0L8exponentS1570 = _M0L1xS659->$1;
      moonbit_decref_cycle_free(_M0L1xS659);
      _M0L6_2atmpS1569 = _M0L8exponentS1570 + 1;
      _M0L6_2atmpS1568
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1568)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1568->$0 = _M0L1qS660;
      _M0L6_2atmpS1568->$1 = _M0L6_2atmpS1569;
      _M0L1xS659 = _M0L6_2atmpS1568;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1684 = _M0FPB9to__chars(_M0L1vS656, _M0L8ieeeSignS652);
  moonbit_decref_cycle_free(_M0L1vS656);
  return _result_1684;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS643,
  int32_t _M0L12ieeeExponentS645
) {
  uint64_t _M0L2m2S642;
  int32_t _M0L6_2atmpS1562;
  int32_t _M0L2e2S644;
  int32_t _M0L6_2atmpS1561;
  uint64_t _M0L6_2atmpS1560;
  uint64_t _M0L4maskS646;
  uint64_t _M0L8fractionS647;
  int32_t _M0L6_2atmpS1559;
  uint64_t _M0L6_2atmpS1558;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1557;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S642 = 4503599627370496ull | _M0L12ieeeMantissaS643;
  _M0L6_2atmpS1562 = _M0L12ieeeExponentS645 - 1023;
  _M0L2e2S644 = _M0L6_2atmpS1562 - 52;
  if (_M0L2e2S644 > 0) {
    return 0;
  }
  if (_M0L2e2S644 < -52) {
    return 0;
  }
  _M0L6_2atmpS1561 = -_M0L2e2S644;
  _M0L6_2atmpS1560 = 1ull << (_M0L6_2atmpS1561 & 63);
  _M0L4maskS646 = _M0L6_2atmpS1560 - 1ull;
  _M0L8fractionS647 = _M0L2m2S642 & _M0L4maskS646;
  if (_M0L8fractionS647 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1559 = -_M0L2e2S644;
  _M0L6_2atmpS1558 = _M0L2m2S642 >> (_M0L6_2atmpS1559 & 63);
  _M0L6_2atmpS1557
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1557)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1557->$0 = _M0L6_2atmpS1558;
  _M0L6_2atmpS1557->$1 = 0;
  return _M0L6_2atmpS1557;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS610,
  int32_t _M0L4signS608
) {
  moonbit_bytes_t _M0L6resultS606;
  int32_t _M0Lm5indexS607;
  uint64_t _M0L6outputS609;
  int32_t _M0L7olengthS611;
  int32_t _M0L8exponentS1556;
  int32_t _M0L6_2atmpS1555;
  int32_t _M0Lm3expS612;
  int32_t _M0L6_2atmpS1554;
  int32_t _M0L6_2atmpS1552;
  int32_t _M0L18scientificNotationS613;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS606 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS607 = 0;
  if (_M0L4signS608) {
    int32_t _M0L6_2atmpS1426 = _M0Lm5indexS607;
    int32_t _M0L6_2atmpS1427;
    if (
      _M0L6_2atmpS1426 < 0
      || _M0L6_2atmpS1426 >= Moonbit_array_length(_M0L6resultS606)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS606[_M0L6_2atmpS1426] = 45;
    _M0L6_2atmpS1427 = _M0Lm5indexS607;
    _M0Lm5indexS607 = _M0L6_2atmpS1427 + 1;
  }
  _M0L6outputS609 = _M0L1vS610->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS611 = _M0FPB17decimal__length17(_M0L6outputS609);
  _M0L8exponentS1556 = _M0L1vS610->$1;
  _M0L6_2atmpS1555 = _M0L8exponentS1556 + _M0L7olengthS611;
  _M0Lm3expS612 = _M0L6_2atmpS1555 - 1;
  _M0L6_2atmpS1554 = _M0Lm3expS612;
  if (_M0L6_2atmpS1554 >= -6) {
    int32_t _M0L6_2atmpS1553 = _M0Lm3expS612;
    _M0L6_2atmpS1552 = _M0L6_2atmpS1553 < 21;
  } else {
    _M0L6_2atmpS1552 = 0;
  }
  _M0L18scientificNotationS613 = !_M0L6_2atmpS1552;
  if (_M0L18scientificNotationS613) {
    int32_t _M0L7_2abindS614 = _M0L7olengthS611 - 1;
    uint64_t _M0L6outputS615;
    int32_t _M0L1iS616 = 0;
    uint64_t _M0L6outputS617 = _M0L6outputS609;
    int32_t _M0L6_2atmpS1428;
    int32_t _M0L6_2atmpS1432;
    int32_t _M0L6_2atmpS1431;
    int32_t _M0L6_2atmpS1430;
    int32_t _M0L6_2atmpS1429;
    int32_t _M0L6_2atmpS1436;
    int32_t _M0L6_2atmpS1437;
    int32_t _M0L6_2atmpS1438;
    int32_t _M0L6_2atmpS1439;
    int32_t _M0L6_2atmpS1440;
    int32_t _M0L6_2atmpS1446;
    int32_t _M0L6_2atmpS1479;
    moonbit_string_t _result_1686;
    while (1) {
      if (_M0L1iS616 < _M0L7_2abindS614) {
        uint64_t _M0L1cS618 = _M0L6outputS617 % 10ull;
        int32_t _M0L6_2atmpS1485 = _M0Lm5indexS607;
        int32_t _M0L6_2atmpS1484 = _M0L6_2atmpS1485 + _M0L7olengthS611;
        int32_t _M0L6_2atmpS1480 = _M0L6_2atmpS1484 - _M0L1iS616;
        int32_t _M0L6_2atmpS1483 = (int32_t)_M0L1cS618;
        int32_t _M0L6_2atmpS1482 = 48 + _M0L6_2atmpS1483;
        int32_t _M0L6_2atmpS1481 = _M0L6_2atmpS1482 & 0xff;
        int32_t _M0L6_2atmpS1486;
        uint64_t _M0L6_2atmpS1487;
        if (
          _M0L6_2atmpS1480 < 0
          || _M0L6_2atmpS1480 >= Moonbit_array_length(_M0L6resultS606)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS606[_M0L6_2atmpS1480] = _M0L6_2atmpS1481;
        _M0L6_2atmpS1486 = _M0L1iS616 + 1;
        _M0L6_2atmpS1487 = _M0L6outputS617 / 10ull;
        _M0L1iS616 = _M0L6_2atmpS1486;
        _M0L6outputS617 = _M0L6_2atmpS1487;
        continue;
      } else {
        _M0L6outputS615 = _M0L6outputS617;
      }
      break;
    }
    _M0L6_2atmpS1428 = _M0Lm5indexS607;
    _M0L6_2atmpS1432 = (int32_t)_M0L6outputS615;
    _M0L6_2atmpS1431 = _M0L6_2atmpS1432 % 10;
    _M0L6_2atmpS1430 = 48 + _M0L6_2atmpS1431;
    _M0L6_2atmpS1429 = _M0L6_2atmpS1430 & 0xff;
    if (
      _M0L6_2atmpS1428 < 0
      || _M0L6_2atmpS1428 >= Moonbit_array_length(_M0L6resultS606)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS606[_M0L6_2atmpS1428] = _M0L6_2atmpS1429;
    if (_M0L7olengthS611 > 1) {
      int32_t _M0L6_2atmpS1434 = _M0Lm5indexS607;
      int32_t _M0L6_2atmpS1433 = _M0L6_2atmpS1434 + 1;
      if (
        _M0L6_2atmpS1433 < 0
        || _M0L6_2atmpS1433 >= Moonbit_array_length(_M0L6resultS606)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS606[_M0L6_2atmpS1433] = 46;
    } else {
      int32_t _M0L6_2atmpS1435 = _M0Lm5indexS607;
      _M0Lm5indexS607 = _M0L6_2atmpS1435 - 1;
    }
    _M0L6_2atmpS1436 = _M0Lm5indexS607;
    _M0L6_2atmpS1437 = _M0L7olengthS611 + 1;
    _M0Lm5indexS607 = _M0L6_2atmpS1436 + _M0L6_2atmpS1437;
    _M0L6_2atmpS1438 = _M0Lm5indexS607;
    if (
      _M0L6_2atmpS1438 < 0
      || _M0L6_2atmpS1438 >= Moonbit_array_length(_M0L6resultS606)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS606[_M0L6_2atmpS1438] = 101;
    _M0L6_2atmpS1439 = _M0Lm5indexS607;
    _M0Lm5indexS607 = _M0L6_2atmpS1439 + 1;
    _M0L6_2atmpS1440 = _M0Lm3expS612;
    if (_M0L6_2atmpS1440 < 0) {
      int32_t _M0L6_2atmpS1441 = _M0Lm5indexS607;
      int32_t _M0L6_2atmpS1442;
      int32_t _M0L6_2atmpS1443;
      if (
        _M0L6_2atmpS1441 < 0
        || _M0L6_2atmpS1441 >= Moonbit_array_length(_M0L6resultS606)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS606[_M0L6_2atmpS1441] = 45;
      _M0L6_2atmpS1442 = _M0Lm5indexS607;
      _M0Lm5indexS607 = _M0L6_2atmpS1442 + 1;
      _M0L6_2atmpS1443 = _M0Lm3expS612;
      _M0Lm3expS612 = -_M0L6_2atmpS1443;
    } else {
      int32_t _M0L6_2atmpS1444 = _M0Lm5indexS607;
      int32_t _M0L6_2atmpS1445;
      if (
        _M0L6_2atmpS1444 < 0
        || _M0L6_2atmpS1444 >= Moonbit_array_length(_M0L6resultS606)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS606[_M0L6_2atmpS1444] = 43;
      _M0L6_2atmpS1445 = _M0Lm5indexS607;
      _M0Lm5indexS607 = _M0L6_2atmpS1445 + 1;
    }
    _M0L6_2atmpS1446 = _M0Lm3expS612;
    if (_M0L6_2atmpS1446 >= 100) {
      int32_t _M0L6_2atmpS1462 = _M0Lm3expS612;
      int32_t _M0L1aS620 = _M0L6_2atmpS1462 / 100;
      int32_t _M0L6_2atmpS1461 = _M0Lm3expS612;
      int32_t _M0L6_2atmpS1460 = _M0L6_2atmpS1461 / 10;
      int32_t _M0L1bS621 = _M0L6_2atmpS1460 % 10;
      int32_t _M0L6_2atmpS1459 = _M0Lm3expS612;
      int32_t _M0L1cS622 = _M0L6_2atmpS1459 % 10;
      int32_t _M0L6_2atmpS1447 = _M0Lm5indexS607;
      int32_t _M0L6_2atmpS1449 = 48 + _M0L1aS620;
      int32_t _M0L6_2atmpS1448 = _M0L6_2atmpS1449 & 0xff;
      int32_t _M0L6_2atmpS1453;
      int32_t _M0L6_2atmpS1450;
      int32_t _M0L6_2atmpS1452;
      int32_t _M0L6_2atmpS1451;
      int32_t _M0L6_2atmpS1457;
      int32_t _M0L6_2atmpS1454;
      int32_t _M0L6_2atmpS1456;
      int32_t _M0L6_2atmpS1455;
      int32_t _M0L6_2atmpS1458;
      if (
        _M0L6_2atmpS1447 < 0
        || _M0L6_2atmpS1447 >= Moonbit_array_length(_M0L6resultS606)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS606[_M0L6_2atmpS1447] = _M0L6_2atmpS1448;
      _M0L6_2atmpS1453 = _M0Lm5indexS607;
      _M0L6_2atmpS1450 = _M0L6_2atmpS1453 + 1;
      _M0L6_2atmpS1452 = 48 + _M0L1bS621;
      _M0L6_2atmpS1451 = _M0L6_2atmpS1452 & 0xff;
      if (
        _M0L6_2atmpS1450 < 0
        || _M0L6_2atmpS1450 >= Moonbit_array_length(_M0L6resultS606)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS606[_M0L6_2atmpS1450] = _M0L6_2atmpS1451;
      _M0L6_2atmpS1457 = _M0Lm5indexS607;
      _M0L6_2atmpS1454 = _M0L6_2atmpS1457 + 2;
      _M0L6_2atmpS1456 = 48 + _M0L1cS622;
      _M0L6_2atmpS1455 = _M0L6_2atmpS1456 & 0xff;
      if (
        _M0L6_2atmpS1454 < 0
        || _M0L6_2atmpS1454 >= Moonbit_array_length(_M0L6resultS606)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS606[_M0L6_2atmpS1454] = _M0L6_2atmpS1455;
      _M0L6_2atmpS1458 = _M0Lm5indexS607;
      _M0Lm5indexS607 = _M0L6_2atmpS1458 + 3;
    } else {
      int32_t _M0L6_2atmpS1463 = _M0Lm3expS612;
      if (_M0L6_2atmpS1463 >= 10) {
        int32_t _M0L6_2atmpS1473 = _M0Lm3expS612;
        int32_t _M0L1aS623 = _M0L6_2atmpS1473 / 10;
        int32_t _M0L6_2atmpS1472 = _M0Lm3expS612;
        int32_t _M0L1bS624 = _M0L6_2atmpS1472 % 10;
        int32_t _M0L6_2atmpS1464 = _M0Lm5indexS607;
        int32_t _M0L6_2atmpS1466 = 48 + _M0L1aS623;
        int32_t _M0L6_2atmpS1465 = _M0L6_2atmpS1466 & 0xff;
        int32_t _M0L6_2atmpS1470;
        int32_t _M0L6_2atmpS1467;
        int32_t _M0L6_2atmpS1469;
        int32_t _M0L6_2atmpS1468;
        int32_t _M0L6_2atmpS1471;
        if (
          _M0L6_2atmpS1464 < 0
          || _M0L6_2atmpS1464 >= Moonbit_array_length(_M0L6resultS606)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS606[_M0L6_2atmpS1464] = _M0L6_2atmpS1465;
        _M0L6_2atmpS1470 = _M0Lm5indexS607;
        _M0L6_2atmpS1467 = _M0L6_2atmpS1470 + 1;
        _M0L6_2atmpS1469 = 48 + _M0L1bS624;
        _M0L6_2atmpS1468 = _M0L6_2atmpS1469 & 0xff;
        if (
          _M0L6_2atmpS1467 < 0
          || _M0L6_2atmpS1467 >= Moonbit_array_length(_M0L6resultS606)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS606[_M0L6_2atmpS1467] = _M0L6_2atmpS1468;
        _M0L6_2atmpS1471 = _M0Lm5indexS607;
        _M0Lm5indexS607 = _M0L6_2atmpS1471 + 2;
      } else {
        int32_t _M0L6_2atmpS1474 = _M0Lm5indexS607;
        int32_t _M0L6_2atmpS1477 = _M0Lm3expS612;
        int32_t _M0L6_2atmpS1476 = 48 + _M0L6_2atmpS1477;
        int32_t _M0L6_2atmpS1475 = _M0L6_2atmpS1476 & 0xff;
        int32_t _M0L6_2atmpS1478;
        if (
          _M0L6_2atmpS1474 < 0
          || _M0L6_2atmpS1474 >= Moonbit_array_length(_M0L6resultS606)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS606[_M0L6_2atmpS1474] = _M0L6_2atmpS1475;
        _M0L6_2atmpS1478 = _M0Lm5indexS607;
        _M0Lm5indexS607 = _M0L6_2atmpS1478 + 1;
      }
    }
    _M0L6_2atmpS1479 = _M0Lm5indexS607;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1686
    = _M0FPB19string__from__bytes(_M0L6resultS606, 0, _M0L6_2atmpS1479);
    moonbit_decref_cycle_free(_M0L6resultS606);
    return _result_1686;
  } else {
    int32_t _M0L6_2atmpS1488 = _M0Lm3expS612;
    int32_t _M0L6_2atmpS1551;
    moonbit_string_t _result_1692;
    if (_M0L6_2atmpS1488 < 0) {
      int32_t _M0L6_2atmpS1489 = _M0Lm5indexS607;
      int32_t _M0L6_2atmpS1491;
      int32_t _M0L6_2atmpS1490;
      int32_t _M0L6_2atmpS1492;
      int32_t _M0L1iS625;
      int32_t _M0L6_2atmpS1507;
      int32_t _M0L6_2atmpS1509;
      int32_t _M0L6_2atmpS1508;
      int32_t _M0L7currentS627;
      int32_t _M0L1iS628;
      uint64_t _M0L6outputS629;
      if (
        _M0L6_2atmpS1489 < 0
        || _M0L6_2atmpS1489 >= Moonbit_array_length(_M0L6resultS606)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS606[_M0L6_2atmpS1489] = 48;
      _M0L6_2atmpS1491 = _M0Lm5indexS607;
      _M0L6_2atmpS1490 = _M0L6_2atmpS1491 + 1;
      if (
        _M0L6_2atmpS1490 < 0
        || _M0L6_2atmpS1490 >= Moonbit_array_length(_M0L6resultS606)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS606[_M0L6_2atmpS1490] = 46;
      _M0L6_2atmpS1492 = _M0Lm5indexS607;
      _M0Lm5indexS607 = _M0L6_2atmpS1492 + 2;
      _M0L1iS625 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1493 = _M0Lm3expS612;
        if (_M0L1iS625 > _M0L6_2atmpS1493) {
          int32_t _M0L6_2atmpS1496 = _M0Lm5indexS607;
          int32_t _M0L6_2atmpS1495 = _M0L6_2atmpS1496 - _M0L1iS625;
          int32_t _M0L6_2atmpS1494 = _M0L6_2atmpS1495 - 1;
          int32_t _M0L6_2atmpS1497;
          if (
            _M0L6_2atmpS1494 < 0
            || _M0L6_2atmpS1494 >= Moonbit_array_length(_M0L6resultS606)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS606[_M0L6_2atmpS1494] = 48;
          _M0L6_2atmpS1497 = _M0L1iS625 - 1;
          _M0L1iS625 = _M0L6_2atmpS1497;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1507 = _M0Lm5indexS607;
      _M0L6_2atmpS1509 = _M0Lm3expS612;
      _M0L6_2atmpS1508 = -1 - _M0L6_2atmpS1509;
      _M0L7currentS627 = _M0L6_2atmpS1507 + _M0L6_2atmpS1508;
      _M0L1iS628 = 0;
      _M0L6outputS629 = _M0L6outputS609;
      while (1) {
        if (_M0L1iS628 < _M0L7olengthS611) {
          int32_t _M0L6_2atmpS1504 = _M0L7currentS627 + _M0L7olengthS611;
          int32_t _M0L6_2atmpS1503 = _M0L6_2atmpS1504 - _M0L1iS628;
          int32_t _M0L6_2atmpS1498 = _M0L6_2atmpS1503 - 1;
          uint64_t _M0L6_2atmpS1502 = _M0L6outputS629 % 10ull;
          int32_t _M0L6_2atmpS1501 = (int32_t)_M0L6_2atmpS1502;
          int32_t _M0L6_2atmpS1500 = 48 + _M0L6_2atmpS1501;
          int32_t _M0L6_2atmpS1499 = _M0L6_2atmpS1500 & 0xff;
          int32_t _M0L6_2atmpS1505;
          uint64_t _M0L6_2atmpS1506;
          if (
            _M0L6_2atmpS1498 < 0
            || _M0L6_2atmpS1498 >= Moonbit_array_length(_M0L6resultS606)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS606[_M0L6_2atmpS1498] = _M0L6_2atmpS1499;
          _M0L6_2atmpS1505 = _M0L1iS628 + 1;
          _M0L6_2atmpS1506 = _M0L6outputS629 / 10ull;
          _M0L1iS628 = _M0L6_2atmpS1505;
          _M0L6outputS629 = _M0L6_2atmpS1506;
          continue;
        }
        break;
      }
      _M0Lm5indexS607 = _M0L7currentS627 + _M0L7olengthS611;
    } else {
      int32_t _M0L6_2atmpS1511 = _M0Lm3expS612;
      int32_t _M0L6_2atmpS1510 = _M0L6_2atmpS1511 + 1;
      if (_M0L6_2atmpS1510 >= _M0L7olengthS611) {
        int32_t _M0L1iS631 = 0;
        uint64_t _M0L6outputS632 = _M0L6outputS609;
        int32_t _M0L6_2atmpS1522;
        int32_t _M0L6_2atmpS1527;
        int32_t _M0L7_2abindS634;
        int32_t _M0L1iS635;
        int32_t _M0L6_2atmpS1528;
        int32_t _M0L6_2atmpS1531;
        int32_t _M0L6_2atmpS1530;
        int32_t _M0L6_2atmpS1529;
        while (1) {
          if (_M0L1iS631 < _M0L7olengthS611) {
            int32_t _M0L6_2atmpS1519 = _M0Lm5indexS607;
            int32_t _M0L6_2atmpS1518 = _M0L6_2atmpS1519 + _M0L7olengthS611;
            int32_t _M0L6_2atmpS1517 = _M0L6_2atmpS1518 - _M0L1iS631;
            int32_t _M0L6_2atmpS1512 = _M0L6_2atmpS1517 - 1;
            uint64_t _M0L6_2atmpS1516 = _M0L6outputS632 % 10ull;
            int32_t _M0L6_2atmpS1515 = (int32_t)_M0L6_2atmpS1516;
            int32_t _M0L6_2atmpS1514 = 48 + _M0L6_2atmpS1515;
            int32_t _M0L6_2atmpS1513 = _M0L6_2atmpS1514 & 0xff;
            int32_t _M0L6_2atmpS1520;
            uint64_t _M0L6_2atmpS1521;
            if (
              _M0L6_2atmpS1512 < 0
              || _M0L6_2atmpS1512 >= Moonbit_array_length(_M0L6resultS606)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS606[_M0L6_2atmpS1512] = _M0L6_2atmpS1513;
            _M0L6_2atmpS1520 = _M0L1iS631 + 1;
            _M0L6_2atmpS1521 = _M0L6outputS632 / 10ull;
            _M0L1iS631 = _M0L6_2atmpS1520;
            _M0L6outputS632 = _M0L6_2atmpS1521;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1522 = _M0Lm5indexS607;
        _M0Lm5indexS607 = _M0L6_2atmpS1522 + _M0L7olengthS611;
        _M0L6_2atmpS1527 = _M0Lm3expS612;
        _M0L7_2abindS634 = _M0L6_2atmpS1527 + 1;
        _M0L1iS635 = _M0L7olengthS611;
        while (1) {
          if (_M0L1iS635 < _M0L7_2abindS634) {
            int32_t _M0L6_2atmpS1525 = _M0Lm5indexS607;
            int32_t _M0L6_2atmpS1524 = _M0L6_2atmpS1525 + _M0L1iS635;
            int32_t _M0L6_2atmpS1523 = _M0L6_2atmpS1524 - _M0L7olengthS611;
            int32_t _M0L6_2atmpS1526;
            if (
              _M0L6_2atmpS1523 < 0
              || _M0L6_2atmpS1523 >= Moonbit_array_length(_M0L6resultS606)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS606[_M0L6_2atmpS1523] = 48;
            _M0L6_2atmpS1526 = _M0L1iS635 + 1;
            _M0L1iS635 = _M0L6_2atmpS1526;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1528 = _M0Lm5indexS607;
        _M0L6_2atmpS1531 = _M0Lm3expS612;
        _M0L6_2atmpS1530 = _M0L6_2atmpS1531 + 1;
        _M0L6_2atmpS1529 = _M0L6_2atmpS1530 - _M0L7olengthS611;
        _M0Lm5indexS607 = _M0L6_2atmpS1528 + _M0L6_2atmpS1529;
      } else {
        int32_t _M0L6_2atmpS1548 = _M0Lm5indexS607;
        int32_t _M0L6_2atmpS1547 = _M0L6_2atmpS1548 + 1;
        int32_t _M0L1iS637 = 0;
        int32_t _M0L7currentS638 = _M0L6_2atmpS1547;
        uint64_t _M0L6outputS639 = _M0L6outputS609;
        int32_t _M0L6_2atmpS1549;
        int32_t _M0L6_2atmpS1550;
        while (1) {
          if (_M0L1iS637 < _M0L7olengthS611) {
            int32_t _M0L6_2atmpS1543 = _M0L7olengthS611 - _M0L1iS637;
            int32_t _M0L6_2atmpS1541 = _M0L6_2atmpS1543 - 1;
            int32_t _M0L6_2atmpS1542 = _M0Lm3expS612;
            int32_t _M0L7currentS640;
            int32_t _M0L6_2atmpS1538;
            int32_t _M0L6_2atmpS1537;
            int32_t _M0L6_2atmpS1532;
            uint64_t _M0L6_2atmpS1536;
            int32_t _M0L6_2atmpS1535;
            int32_t _M0L6_2atmpS1534;
            int32_t _M0L6_2atmpS1533;
            int32_t _M0L6_2atmpS1539;
            uint64_t _M0L6_2atmpS1540;
            if (_M0L6_2atmpS1541 == _M0L6_2atmpS1542) {
              int32_t _M0L6_2atmpS1546 = _M0L7currentS638 + _M0L7olengthS611;
              int32_t _M0L6_2atmpS1545 = _M0L6_2atmpS1546 - _M0L1iS637;
              int32_t _M0L6_2atmpS1544 = _M0L6_2atmpS1545 - 1;
              if (
                _M0L6_2atmpS1544 < 0
                || _M0L6_2atmpS1544 >= Moonbit_array_length(_M0L6resultS606)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS606[_M0L6_2atmpS1544] = 46;
              _M0L7currentS640 = _M0L7currentS638 - 1;
            } else {
              _M0L7currentS640 = _M0L7currentS638;
            }
            _M0L6_2atmpS1538 = _M0L7currentS640 + _M0L7olengthS611;
            _M0L6_2atmpS1537 = _M0L6_2atmpS1538 - _M0L1iS637;
            _M0L6_2atmpS1532 = _M0L6_2atmpS1537 - 1;
            _M0L6_2atmpS1536 = _M0L6outputS639 % 10ull;
            _M0L6_2atmpS1535 = (int32_t)_M0L6_2atmpS1536;
            _M0L6_2atmpS1534 = 48 + _M0L6_2atmpS1535;
            _M0L6_2atmpS1533 = _M0L6_2atmpS1534 & 0xff;
            if (
              _M0L6_2atmpS1532 < 0
              || _M0L6_2atmpS1532 >= Moonbit_array_length(_M0L6resultS606)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS606[_M0L6_2atmpS1532] = _M0L6_2atmpS1533;
            _M0L6_2atmpS1539 = _M0L1iS637 + 1;
            _M0L6_2atmpS1540 = _M0L6outputS639 / 10ull;
            _M0L1iS637 = _M0L6_2atmpS1539;
            _M0L7currentS638 = _M0L7currentS640;
            _M0L6outputS639 = _M0L6_2atmpS1540;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1549 = _M0Lm5indexS607;
        _M0L6_2atmpS1550 = _M0L7olengthS611 + 1;
        _M0Lm5indexS607 = _M0L6_2atmpS1549 + _M0L6_2atmpS1550;
      }
    }
    _M0L6_2atmpS1551 = _M0Lm5indexS607;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1692
    = _M0FPB19string__from__bytes(_M0L6resultS606, 0, _M0L6_2atmpS1551);
    moonbit_decref_cycle_free(_M0L6resultS606);
    return _result_1692;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS552,
  uint32_t _M0L12ieeeExponentS551
) {
  int32_t _M0Lm2e2S549;
  uint64_t _M0Lm2m2S550;
  uint64_t _M0L6_2atmpS1425;
  uint64_t _M0L6_2atmpS1424;
  int32_t _M0L4evenS553;
  uint64_t _M0L6_2atmpS1423;
  uint64_t _M0L2mvS554;
  int32_t _M0L7mmShiftS555;
  uint64_t _M0Lm2vrS556;
  uint64_t _M0Lm2vpS557;
  uint64_t _M0Lm2vmS558;
  int32_t _M0Lm3e10S559;
  int32_t _M0Lm17vmIsTrailingZerosS560;
  int32_t _M0Lm17vrIsTrailingZerosS561;
  int32_t _M0L6_2atmpS1325;
  int32_t _M0Lm7removedS580;
  int32_t _M0Lm16lastRemovedDigitS581;
  uint64_t _M0Lm6outputS582;
  int32_t _M0L6_2atmpS1421;
  int32_t _M0L6_2atmpS1422;
  int32_t _M0L3expS605;
  uint64_t _M0L6_2atmpS1420;
  struct _M0TPB17FloatingDecimal64* _block_1698;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S549 = 0;
  _M0Lm2m2S550 = 0ull;
  if (_M0L12ieeeExponentS551 == 0u) {
    _M0Lm2e2S549 = -1076;
    _M0Lm2m2S550 = _M0L12ieeeMantissaS552;
  } else {
    int32_t _M0L6_2atmpS1324 = *(int32_t*)&_M0L12ieeeExponentS551;
    int32_t _M0L6_2atmpS1323 = _M0L6_2atmpS1324 - 1023;
    int32_t _M0L6_2atmpS1322 = _M0L6_2atmpS1323 - 52;
    _M0Lm2e2S549 = _M0L6_2atmpS1322 - 2;
    _M0Lm2m2S550 = 4503599627370496ull | _M0L12ieeeMantissaS552;
  }
  _M0L6_2atmpS1425 = _M0Lm2m2S550;
  _M0L6_2atmpS1424 = _M0L6_2atmpS1425 & 1ull;
  _M0L4evenS553 = _M0L6_2atmpS1424 == 0ull;
  _M0L6_2atmpS1423 = _M0Lm2m2S550;
  _M0L2mvS554 = 4ull * _M0L6_2atmpS1423;
  _M0L7mmShiftS555
  = _M0L12ieeeMantissaS552 != 0ull || _M0L12ieeeExponentS551 <= 1u;
  _M0Lm2vrS556 = 0ull;
  _M0Lm2vpS557 = 0ull;
  _M0Lm2vmS558 = 0ull;
  _M0Lm3e10S559 = 0;
  _M0Lm17vmIsTrailingZerosS560 = 0;
  _M0Lm17vrIsTrailingZerosS561 = 0;
  _M0L6_2atmpS1325 = _M0Lm2e2S549;
  if (_M0L6_2atmpS1325 >= 0) {
    int32_t _M0L6_2atmpS1347 = _M0Lm2e2S549;
    int32_t _M0L6_2atmpS1343;
    int32_t _M0L6_2atmpS1346;
    int32_t _M0L6_2atmpS1345;
    int32_t _M0L6_2atmpS1344;
    int32_t _M0L1qS562;
    int32_t _M0L6_2atmpS1342;
    int32_t _M0L6_2atmpS1341;
    int32_t _M0L1kS563;
    int32_t _M0L6_2atmpS1340;
    int32_t _M0L6_2atmpS1339;
    int32_t _M0L6_2atmpS1338;
    int32_t _M0L1iS564;
    struct _M0TPB8Pow5Pair _M0L4pow5S565;
    uint64_t _M0L6_2atmpS1337;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS566;
    uint64_t _M0L8_2avrOutS567;
    uint64_t _M0L8_2avpOutS568;
    uint64_t _M0L8_2avmOutS569;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1343 = _M0FPB9log10Pow2(_M0L6_2atmpS1347);
    _M0L6_2atmpS1346 = _M0Lm2e2S549;
    _M0L6_2atmpS1345 = _M0L6_2atmpS1346 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1344 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1345);
    _M0L1qS562 = _M0L6_2atmpS1343 - _M0L6_2atmpS1344;
    _M0Lm3e10S559 = _M0L1qS562;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1342 = _M0FPB8pow5bits(_M0L1qS562);
    _M0L6_2atmpS1341 = 125 + _M0L6_2atmpS1342;
    _M0L1kS563 = _M0L6_2atmpS1341 - 1;
    _M0L6_2atmpS1340 = _M0Lm2e2S549;
    _M0L6_2atmpS1339 = -_M0L6_2atmpS1340;
    _M0L6_2atmpS1338 = _M0L6_2atmpS1339 + _M0L1qS562;
    _M0L1iS564 = _M0L6_2atmpS1338 + _M0L1kS563;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S565 = _M0FPB22double__computeInvPow5(_M0L1qS562);
    _M0L6_2atmpS1337 = _M0Lm2m2S550;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS566
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1337, _M0L4pow5S565, _M0L1iS564, _M0L7mmShiftS555);
    _M0L8_2avrOutS567 = _M0L7_2abindS566.$0;
    _M0L8_2avpOutS568 = _M0L7_2abindS566.$1;
    _M0L8_2avmOutS569 = _M0L7_2abindS566.$2;
    _M0Lm2vrS556 = _M0L8_2avrOutS567;
    _M0Lm2vpS557 = _M0L8_2avpOutS568;
    _M0Lm2vmS558 = _M0L8_2avmOutS569;
    if (_M0L1qS562 <= 21) {
      int32_t _M0L6_2atmpS1333 = (int32_t)_M0L2mvS554;
      uint64_t _M0L6_2atmpS1336 = _M0L2mvS554 / 5ull;
      int32_t _M0L6_2atmpS1335 = (int32_t)_M0L6_2atmpS1336;
      int32_t _M0L6_2atmpS1334 = 5 * _M0L6_2atmpS1335;
      int32_t _M0L6mvMod5S570 = _M0L6_2atmpS1333 - _M0L6_2atmpS1334;
      if (_M0L6mvMod5S570 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS561
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS554, _M0L1qS562);
      } else if (_M0L4evenS553) {
        uint64_t _M0L6_2atmpS1327 = _M0L2mvS554 - 1ull;
        uint64_t _M0L6_2atmpS1328;
        uint64_t _M0L6_2atmpS1326;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1328 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS555);
        _M0L6_2atmpS1326 = _M0L6_2atmpS1327 - _M0L6_2atmpS1328;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS560
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1326, _M0L1qS562);
      } else {
        uint64_t _M0L6_2atmpS1329 = _M0Lm2vpS557;
        uint64_t _M0L6_2atmpS1332 = _M0L2mvS554 + 2ull;
        int32_t _M0L6_2atmpS1331;
        uint64_t _M0L6_2atmpS1330;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1331
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1332, _M0L1qS562);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1330 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1331);
        _M0Lm2vpS557 = _M0L6_2atmpS1329 - _M0L6_2atmpS1330;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1361 = _M0Lm2e2S549;
    int32_t _M0L6_2atmpS1360 = -_M0L6_2atmpS1361;
    int32_t _M0L6_2atmpS1355;
    int32_t _M0L6_2atmpS1359;
    int32_t _M0L6_2atmpS1358;
    int32_t _M0L6_2atmpS1357;
    int32_t _M0L6_2atmpS1356;
    int32_t _M0L1qS571;
    int32_t _M0L6_2atmpS1348;
    int32_t _M0L6_2atmpS1354;
    int32_t _M0L6_2atmpS1353;
    int32_t _M0L1iS572;
    int32_t _M0L6_2atmpS1352;
    int32_t _M0L1kS573;
    int32_t _M0L1jS574;
    struct _M0TPB8Pow5Pair _M0L4pow5S575;
    uint64_t _M0L6_2atmpS1351;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS576;
    uint64_t _M0L8_2avrOutS577;
    uint64_t _M0L8_2avpOutS578;
    uint64_t _M0L8_2avmOutS579;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1355 = _M0FPB9log10Pow5(_M0L6_2atmpS1360);
    _M0L6_2atmpS1359 = _M0Lm2e2S549;
    _M0L6_2atmpS1358 = -_M0L6_2atmpS1359;
    _M0L6_2atmpS1357 = _M0L6_2atmpS1358 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1356 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1357);
    _M0L1qS571 = _M0L6_2atmpS1355 - _M0L6_2atmpS1356;
    _M0L6_2atmpS1348 = _M0Lm2e2S549;
    _M0Lm3e10S559 = _M0L1qS571 + _M0L6_2atmpS1348;
    _M0L6_2atmpS1354 = _M0Lm2e2S549;
    _M0L6_2atmpS1353 = -_M0L6_2atmpS1354;
    _M0L1iS572 = _M0L6_2atmpS1353 - _M0L1qS571;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1352 = _M0FPB8pow5bits(_M0L1iS572);
    _M0L1kS573 = _M0L6_2atmpS1352 - 125;
    _M0L1jS574 = _M0L1qS571 - _M0L1kS573;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S575 = _M0FPB19double__computePow5(_M0L1iS572);
    _M0L6_2atmpS1351 = _M0Lm2m2S550;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS576
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1351, _M0L4pow5S575, _M0L1jS574, _M0L7mmShiftS555);
    _M0L8_2avrOutS577 = _M0L7_2abindS576.$0;
    _M0L8_2avpOutS578 = _M0L7_2abindS576.$1;
    _M0L8_2avmOutS579 = _M0L7_2abindS576.$2;
    _M0Lm2vrS556 = _M0L8_2avrOutS577;
    _M0Lm2vpS557 = _M0L8_2avpOutS578;
    _M0Lm2vmS558 = _M0L8_2avmOutS579;
    if (_M0L1qS571 <= 1) {
      _M0Lm17vrIsTrailingZerosS561 = 1;
      if (_M0L4evenS553) {
        int32_t _M0L6_2atmpS1349;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1349 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS555);
        _M0Lm17vmIsTrailingZerosS560 = _M0L6_2atmpS1349 == 1;
      } else {
        uint64_t _M0L6_2atmpS1350 = _M0Lm2vpS557;
        _M0Lm2vpS557 = _M0L6_2atmpS1350 - 1ull;
      }
    } else if (_M0L1qS571 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS561
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS554, _M0L1qS571);
    }
  }
  _M0Lm7removedS580 = 0;
  _M0Lm16lastRemovedDigitS581 = 0;
  _M0Lm6outputS582 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS560 || _M0Lm17vrIsTrailingZerosS561) {
    int32_t _if__result_1695;
    uint64_t _M0L6_2atmpS1391;
    uint64_t _M0L6_2atmpS1397;
    uint64_t _M0L6_2atmpS1398;
    int32_t _if__result_1696;
    int32_t _M0L6_2atmpS1394;
    int64_t _M0L6_2atmpS1393;
    uint64_t _M0L6_2atmpS1392;
    while (1) {
      uint64_t _M0L6_2atmpS1374 = _M0Lm2vpS557;
      uint64_t _M0L7vpDiv10S583 = _M0L6_2atmpS1374 / 10ull;
      uint64_t _M0L6_2atmpS1373 = _M0Lm2vmS558;
      uint64_t _M0L7vmDiv10S584 = _M0L6_2atmpS1373 / 10ull;
      uint64_t _M0L6_2atmpS1372;
      int32_t _M0L6_2atmpS1369;
      int32_t _M0L6_2atmpS1371;
      int32_t _M0L6_2atmpS1370;
      int32_t _M0L7vmMod10S586;
      uint64_t _M0L6_2atmpS1368;
      uint64_t _M0L7vrDiv10S587;
      uint64_t _M0L6_2atmpS1367;
      int32_t _M0L6_2atmpS1364;
      int32_t _M0L6_2atmpS1366;
      int32_t _M0L6_2atmpS1365;
      int32_t _M0L7vrMod10S588;
      int32_t _M0L6_2atmpS1363;
      if (_M0L7vpDiv10S583 <= _M0L7vmDiv10S584) {
        break;
      }
      _M0L6_2atmpS1372 = _M0Lm2vmS558;
      _M0L6_2atmpS1369 = (int32_t)_M0L6_2atmpS1372;
      _M0L6_2atmpS1371 = (int32_t)_M0L7vmDiv10S584;
      _M0L6_2atmpS1370 = 10 * _M0L6_2atmpS1371;
      _M0L7vmMod10S586 = _M0L6_2atmpS1369 - _M0L6_2atmpS1370;
      _M0L6_2atmpS1368 = _M0Lm2vrS556;
      _M0L7vrDiv10S587 = _M0L6_2atmpS1368 / 10ull;
      _M0L6_2atmpS1367 = _M0Lm2vrS556;
      _M0L6_2atmpS1364 = (int32_t)_M0L6_2atmpS1367;
      _M0L6_2atmpS1366 = (int32_t)_M0L7vrDiv10S587;
      _M0L6_2atmpS1365 = 10 * _M0L6_2atmpS1366;
      _M0L7vrMod10S588 = _M0L6_2atmpS1364 - _M0L6_2atmpS1365;
      _M0Lm17vmIsTrailingZerosS560
      = _M0Lm17vmIsTrailingZerosS560 && _M0L7vmMod10S586 == 0;
      if (_M0Lm17vrIsTrailingZerosS561) {
        int32_t _M0L6_2atmpS1362 = _M0Lm16lastRemovedDigitS581;
        _M0Lm17vrIsTrailingZerosS561 = _M0L6_2atmpS1362 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS561 = 0;
      }
      _M0Lm16lastRemovedDigitS581 = _M0L7vrMod10S588;
      _M0Lm2vrS556 = _M0L7vrDiv10S587;
      _M0Lm2vpS557 = _M0L7vpDiv10S583;
      _M0Lm2vmS558 = _M0L7vmDiv10S584;
      _M0L6_2atmpS1363 = _M0Lm7removedS580;
      _M0Lm7removedS580 = _M0L6_2atmpS1363 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS560) {
      while (1) {
        uint64_t _M0L6_2atmpS1387 = _M0Lm2vmS558;
        uint64_t _M0L7vmDiv10S589 = _M0L6_2atmpS1387 / 10ull;
        uint64_t _M0L6_2atmpS1386 = _M0Lm2vmS558;
        int32_t _M0L6_2atmpS1383 = (int32_t)_M0L6_2atmpS1386;
        int32_t _M0L6_2atmpS1385 = (int32_t)_M0L7vmDiv10S589;
        int32_t _M0L6_2atmpS1384 = 10 * _M0L6_2atmpS1385;
        int32_t _M0L7vmMod10S590 = _M0L6_2atmpS1383 - _M0L6_2atmpS1384;
        uint64_t _M0L6_2atmpS1382;
        uint64_t _M0L7vpDiv10S592;
        uint64_t _M0L6_2atmpS1381;
        uint64_t _M0L7vrDiv10S593;
        uint64_t _M0L6_2atmpS1380;
        int32_t _M0L6_2atmpS1377;
        int32_t _M0L6_2atmpS1379;
        int32_t _M0L6_2atmpS1378;
        int32_t _M0L7vrMod10S594;
        int32_t _M0L6_2atmpS1376;
        if (_M0L7vmMod10S590 != 0) {
          break;
        }
        _M0L6_2atmpS1382 = _M0Lm2vpS557;
        _M0L7vpDiv10S592 = _M0L6_2atmpS1382 / 10ull;
        _M0L6_2atmpS1381 = _M0Lm2vrS556;
        _M0L7vrDiv10S593 = _M0L6_2atmpS1381 / 10ull;
        _M0L6_2atmpS1380 = _M0Lm2vrS556;
        _M0L6_2atmpS1377 = (int32_t)_M0L6_2atmpS1380;
        _M0L6_2atmpS1379 = (int32_t)_M0L7vrDiv10S593;
        _M0L6_2atmpS1378 = 10 * _M0L6_2atmpS1379;
        _M0L7vrMod10S594 = _M0L6_2atmpS1377 - _M0L6_2atmpS1378;
        if (_M0Lm17vrIsTrailingZerosS561) {
          int32_t _M0L6_2atmpS1375 = _M0Lm16lastRemovedDigitS581;
          _M0Lm17vrIsTrailingZerosS561 = _M0L6_2atmpS1375 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS561 = 0;
        }
        _M0Lm16lastRemovedDigitS581 = _M0L7vrMod10S594;
        _M0Lm2vrS556 = _M0L7vrDiv10S593;
        _M0Lm2vpS557 = _M0L7vpDiv10S592;
        _M0Lm2vmS558 = _M0L7vmDiv10S589;
        _M0L6_2atmpS1376 = _M0Lm7removedS580;
        _M0Lm7removedS580 = _M0L6_2atmpS1376 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS561) {
      int32_t _M0L6_2atmpS1390 = _M0Lm16lastRemovedDigitS581;
      if (_M0L6_2atmpS1390 == 5) {
        uint64_t _M0L6_2atmpS1389 = _M0Lm2vrS556;
        uint64_t _M0L6_2atmpS1388 = _M0L6_2atmpS1389 % 2ull;
        _if__result_1695 = _M0L6_2atmpS1388 == 0ull;
      } else {
        _if__result_1695 = 0;
      }
    } else {
      _if__result_1695 = 0;
    }
    if (_if__result_1695) {
      _M0Lm16lastRemovedDigitS581 = 4;
    }
    _M0L6_2atmpS1391 = _M0Lm2vrS556;
    _M0L6_2atmpS1397 = _M0Lm2vrS556;
    _M0L6_2atmpS1398 = _M0Lm2vmS558;
    if (_M0L6_2atmpS1397 == _M0L6_2atmpS1398) {
      if (!_M0L4evenS553) {
        _if__result_1696 = 1;
      } else {
        int32_t _M0L6_2atmpS1396 = _M0Lm17vmIsTrailingZerosS560;
        _if__result_1696 = !_M0L6_2atmpS1396;
      }
    } else {
      _if__result_1696 = 0;
    }
    if (_if__result_1696) {
      _M0L6_2atmpS1394 = 1;
    } else {
      int32_t _M0L6_2atmpS1395 = _M0Lm16lastRemovedDigitS581;
      _M0L6_2atmpS1394 = _M0L6_2atmpS1395 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1393 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1394);
    _M0L6_2atmpS1392 = *(uint64_t*)&_M0L6_2atmpS1393;
    _M0Lm6outputS582 = _M0L6_2atmpS1391 + _M0L6_2atmpS1392;
  } else {
    int32_t _M0Lm7roundUpS595 = 0;
    uint64_t _M0L6_2atmpS1419 = _M0Lm2vpS557;
    uint64_t _M0L8vpDiv100S596 = _M0L6_2atmpS1419 / 100ull;
    uint64_t _M0L6_2atmpS1418 = _M0Lm2vmS558;
    uint64_t _M0L8vmDiv100S597 = _M0L6_2atmpS1418 / 100ull;
    uint64_t _M0L6_2atmpS1413;
    uint64_t _M0L6_2atmpS1416;
    uint64_t _M0L6_2atmpS1417;
    int32_t _M0L6_2atmpS1415;
    uint64_t _M0L6_2atmpS1414;
    if (_M0L8vpDiv100S596 > _M0L8vmDiv100S597) {
      uint64_t _M0L6_2atmpS1404 = _M0Lm2vrS556;
      uint64_t _M0L8vrDiv100S598 = _M0L6_2atmpS1404 / 100ull;
      uint64_t _M0L6_2atmpS1403 = _M0Lm2vrS556;
      int32_t _M0L6_2atmpS1400 = (int32_t)_M0L6_2atmpS1403;
      int32_t _M0L6_2atmpS1402 = (int32_t)_M0L8vrDiv100S598;
      int32_t _M0L6_2atmpS1401 = 100 * _M0L6_2atmpS1402;
      int32_t _M0L8vrMod100S599 = _M0L6_2atmpS1400 - _M0L6_2atmpS1401;
      int32_t _M0L6_2atmpS1399;
      _M0Lm7roundUpS595 = _M0L8vrMod100S599 >= 50;
      _M0Lm2vrS556 = _M0L8vrDiv100S598;
      _M0Lm2vpS557 = _M0L8vpDiv100S596;
      _M0Lm2vmS558 = _M0L8vmDiv100S597;
      _M0L6_2atmpS1399 = _M0Lm7removedS580;
      _M0Lm7removedS580 = _M0L6_2atmpS1399 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1412 = _M0Lm2vpS557;
      uint64_t _M0L7vpDiv10S600 = _M0L6_2atmpS1412 / 10ull;
      uint64_t _M0L6_2atmpS1411 = _M0Lm2vmS558;
      uint64_t _M0L7vmDiv10S601 = _M0L6_2atmpS1411 / 10ull;
      uint64_t _M0L6_2atmpS1410;
      uint64_t _M0L7vrDiv10S603;
      uint64_t _M0L6_2atmpS1409;
      int32_t _M0L6_2atmpS1406;
      int32_t _M0L6_2atmpS1408;
      int32_t _M0L6_2atmpS1407;
      int32_t _M0L7vrMod10S604;
      int32_t _M0L6_2atmpS1405;
      if (_M0L7vpDiv10S600 <= _M0L7vmDiv10S601) {
        break;
      }
      _M0L6_2atmpS1410 = _M0Lm2vrS556;
      _M0L7vrDiv10S603 = _M0L6_2atmpS1410 / 10ull;
      _M0L6_2atmpS1409 = _M0Lm2vrS556;
      _M0L6_2atmpS1406 = (int32_t)_M0L6_2atmpS1409;
      _M0L6_2atmpS1408 = (int32_t)_M0L7vrDiv10S603;
      _M0L6_2atmpS1407 = 10 * _M0L6_2atmpS1408;
      _M0L7vrMod10S604 = _M0L6_2atmpS1406 - _M0L6_2atmpS1407;
      _M0Lm7roundUpS595 = _M0L7vrMod10S604 >= 5;
      _M0Lm2vrS556 = _M0L7vrDiv10S603;
      _M0Lm2vpS557 = _M0L7vpDiv10S600;
      _M0Lm2vmS558 = _M0L7vmDiv10S601;
      _M0L6_2atmpS1405 = _M0Lm7removedS580;
      _M0Lm7removedS580 = _M0L6_2atmpS1405 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1413 = _M0Lm2vrS556;
    _M0L6_2atmpS1416 = _M0Lm2vrS556;
    _M0L6_2atmpS1417 = _M0Lm2vmS558;
    _M0L6_2atmpS1415
    = _M0L6_2atmpS1416 == _M0L6_2atmpS1417 || _M0Lm7roundUpS595;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1414 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1415);
    _M0Lm6outputS582 = _M0L6_2atmpS1413 + _M0L6_2atmpS1414;
  }
  _M0L6_2atmpS1421 = _M0Lm3e10S559;
  _M0L6_2atmpS1422 = _M0Lm7removedS580;
  _M0L3expS605 = _M0L6_2atmpS1421 + _M0L6_2atmpS1422;
  _M0L6_2atmpS1420 = _M0Lm6outputS582;
  _block_1698
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_1698)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1698->$0 = _M0L6_2atmpS1420;
  _block_1698->$1 = _M0L3expS605;
  return _block_1698;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS548) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS548) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS547) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS547) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS546) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS546) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS545) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS545 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS545 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS545 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS545 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS545 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS545 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS545 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS545 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS545 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS545 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS545 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS545 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS545 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS545 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS545 >= 100ull) {
    return 3;
  }
  if (_M0L1vS545 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS528) {
  int32_t _M0L6_2atmpS1321;
  int32_t _M0L6_2atmpS1320;
  int32_t _M0L4baseS527;
  int32_t _M0L5base2S529;
  int32_t _M0L6offsetS530;
  int32_t _M0L6_2atmpS1319;
  uint64_t _M0L4mul0S531;
  int32_t _M0L6_2atmpS1318;
  int32_t _M0L6_2atmpS1317;
  uint64_t _M0L4mul1S532;
  uint64_t _M0L1mS533;
  struct _M0TPB7Umul128 _M0L7_2abindS534;
  uint64_t _M0L7_2alow1S535;
  uint64_t _M0L8_2ahigh1S536;
  struct _M0TPB7Umul128 _M0L7_2abindS537;
  uint64_t _M0L7_2alow0S538;
  uint64_t _M0L8_2ahigh0S539;
  uint64_t _M0L3sumS540;
  uint64_t _M0Lm5high1S541;
  int32_t _M0L6_2atmpS1315;
  int32_t _M0L6_2atmpS1316;
  int32_t _M0L5deltaS542;
  uint64_t _M0L6_2atmpS1314;
  uint64_t _M0L6_2atmpS1306;
  int32_t _M0L6_2atmpS1313;
  uint32_t _M0L6_2atmpS1310;
  int32_t _M0L6_2atmpS1312;
  int32_t _M0L6_2atmpS1311;
  uint32_t _M0L6_2atmpS1309;
  uint32_t _M0L6_2atmpS1308;
  uint64_t _M0L6_2atmpS1307;
  uint64_t _M0L1aS543;
  uint64_t _M0L6_2atmpS1305;
  uint64_t _M0L1bS544;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1321 = _M0L1iS528 + 26;
  _M0L6_2atmpS1320 = _M0L6_2atmpS1321 - 1;
  _M0L4baseS527 = _M0L6_2atmpS1320 / 26;
  _M0L5base2S529 = _M0L4baseS527 * 26;
  _M0L6offsetS530 = _M0L5base2S529 - _M0L1iS528;
  _M0L6_2atmpS1319 = _M0L4baseS527 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S531
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1319);
  _M0L6_2atmpS1318 = _M0L4baseS527 * 2;
  _M0L6_2atmpS1317 = _M0L6_2atmpS1318 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S532
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1317);
  if (_M0L6offsetS530 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S531, .$1 = _M0L4mul1S532};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS533
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS530);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS534 = _M0FPB7umul128(_M0L1mS533, _M0L4mul1S532);
  _M0L7_2alow1S535 = _M0L7_2abindS534.$0;
  _M0L8_2ahigh1S536 = _M0L7_2abindS534.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS537 = _M0FPB7umul128(_M0L1mS533, _M0L4mul0S531);
  _M0L7_2alow0S538 = _M0L7_2abindS537.$0;
  _M0L8_2ahigh0S539 = _M0L7_2abindS537.$1;
  _M0L3sumS540 = _M0L8_2ahigh0S539 + _M0L7_2alow1S535;
  _M0Lm5high1S541 = _M0L8_2ahigh1S536;
  if (_M0L3sumS540 < _M0L8_2ahigh0S539) {
    uint64_t _M0L6_2atmpS1304 = _M0Lm5high1S541;
    _M0Lm5high1S541 = _M0L6_2atmpS1304 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1315 = _M0FPB8pow5bits(_M0L5base2S529);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1316 = _M0FPB8pow5bits(_M0L1iS528);
  _M0L5deltaS542 = _M0L6_2atmpS1315 - _M0L6_2atmpS1316;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1314
  = _M0FPB13shiftright128(_M0L7_2alow0S538, _M0L3sumS540, _M0L5deltaS542);
  _M0L6_2atmpS1306 = _M0L6_2atmpS1314 + 1ull;
  _M0L6_2atmpS1313 = _M0L1iS528 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1310
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1313);
  _M0L6_2atmpS1312 = _M0L1iS528 % 16;
  _M0L6_2atmpS1311 = _M0L6_2atmpS1312 << 1;
  _M0L6_2atmpS1309 = _M0L6_2atmpS1310 >> (_M0L6_2atmpS1311 & 31);
  _M0L6_2atmpS1308 = _M0L6_2atmpS1309 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1307 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1308);
  _M0L1aS543 = _M0L6_2atmpS1306 + _M0L6_2atmpS1307;
  _M0L6_2atmpS1305 = _M0Lm5high1S541;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS544
  = _M0FPB13shiftright128(_M0L3sumS540, _M0L6_2atmpS1305, _M0L5deltaS542);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS543, .$1 = _M0L1bS544};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS510) {
  int32_t _M0L4baseS509;
  int32_t _M0L5base2S511;
  int32_t _M0L6offsetS512;
  int32_t _M0L6_2atmpS1303;
  uint64_t _M0L4mul0S513;
  int32_t _M0L6_2atmpS1302;
  int32_t _M0L6_2atmpS1301;
  uint64_t _M0L4mul1S514;
  uint64_t _M0L1mS515;
  struct _M0TPB7Umul128 _M0L7_2abindS516;
  uint64_t _M0L7_2alow1S517;
  uint64_t _M0L8_2ahigh1S518;
  struct _M0TPB7Umul128 _M0L7_2abindS519;
  uint64_t _M0L7_2alow0S520;
  uint64_t _M0L8_2ahigh0S521;
  uint64_t _M0L3sumS522;
  uint64_t _M0Lm5high1S523;
  int32_t _M0L6_2atmpS1299;
  int32_t _M0L6_2atmpS1300;
  int32_t _M0L5deltaS524;
  uint64_t _M0L6_2atmpS1291;
  int32_t _M0L6_2atmpS1298;
  uint32_t _M0L6_2atmpS1295;
  int32_t _M0L6_2atmpS1297;
  int32_t _M0L6_2atmpS1296;
  uint32_t _M0L6_2atmpS1294;
  uint32_t _M0L6_2atmpS1293;
  uint64_t _M0L6_2atmpS1292;
  uint64_t _M0L1aS525;
  uint64_t _M0L6_2atmpS1290;
  uint64_t _M0L1bS526;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS509 = _M0L1iS510 / 26;
  _M0L5base2S511 = _M0L4baseS509 * 26;
  _M0L6offsetS512 = _M0L1iS510 - _M0L5base2S511;
  _M0L6_2atmpS1303 = _M0L4baseS509 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S513
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1303);
  _M0L6_2atmpS1302 = _M0L4baseS509 * 2;
  _M0L6_2atmpS1301 = _M0L6_2atmpS1302 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S514
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1301);
  if (_M0L6offsetS512 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S513, .$1 = _M0L4mul1S514};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS515
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS512);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS516 = _M0FPB7umul128(_M0L1mS515, _M0L4mul1S514);
  _M0L7_2alow1S517 = _M0L7_2abindS516.$0;
  _M0L8_2ahigh1S518 = _M0L7_2abindS516.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS519 = _M0FPB7umul128(_M0L1mS515, _M0L4mul0S513);
  _M0L7_2alow0S520 = _M0L7_2abindS519.$0;
  _M0L8_2ahigh0S521 = _M0L7_2abindS519.$1;
  _M0L3sumS522 = _M0L8_2ahigh0S521 + _M0L7_2alow1S517;
  _M0Lm5high1S523 = _M0L8_2ahigh1S518;
  if (_M0L3sumS522 < _M0L8_2ahigh0S521) {
    uint64_t _M0L6_2atmpS1289 = _M0Lm5high1S523;
    _M0Lm5high1S523 = _M0L6_2atmpS1289 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1299 = _M0FPB8pow5bits(_M0L1iS510);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1300 = _M0FPB8pow5bits(_M0L5base2S511);
  _M0L5deltaS524 = _M0L6_2atmpS1299 - _M0L6_2atmpS1300;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1291
  = _M0FPB13shiftright128(_M0L7_2alow0S520, _M0L3sumS522, _M0L5deltaS524);
  _M0L6_2atmpS1298 = _M0L1iS510 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1295
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1298);
  _M0L6_2atmpS1297 = _M0L1iS510 % 16;
  _M0L6_2atmpS1296 = _M0L6_2atmpS1297 << 1;
  _M0L6_2atmpS1294 = _M0L6_2atmpS1295 >> (_M0L6_2atmpS1296 & 31);
  _M0L6_2atmpS1293 = _M0L6_2atmpS1294 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1292 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1293);
  _M0L1aS525 = _M0L6_2atmpS1291 + _M0L6_2atmpS1292;
  _M0L6_2atmpS1290 = _M0Lm5high1S523;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS526
  = _M0FPB13shiftright128(_M0L3sumS522, _M0L6_2atmpS1290, _M0L5deltaS524);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS525, .$1 = _M0L1bS526};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS483,
  struct _M0TPB8Pow5Pair _M0L3mulS480,
  int32_t _M0L1jS496,
  int32_t _M0L7mmShiftS498
) {
  uint64_t _M0L7_2amul0S479;
  uint64_t _M0L7_2amul1S481;
  uint64_t _M0L1mS482;
  struct _M0TPB7Umul128 _M0L7_2abindS484;
  uint64_t _M0L5_2aloS485;
  uint64_t _M0L6_2atmpS486;
  struct _M0TPB7Umul128 _M0L7_2abindS487;
  uint64_t _M0L6_2alo2S488;
  uint64_t _M0L6_2ahi2S489;
  uint64_t _M0L3midS490;
  uint64_t _M0L6_2atmpS1288;
  uint64_t _M0L2hiS491;
  uint64_t _M0L3lo2S492;
  uint64_t _M0L6_2atmpS1286;
  uint64_t _M0L6_2atmpS1287;
  uint64_t _M0L4mid2S493;
  uint64_t _M0L6_2atmpS1285;
  uint64_t _M0L3hi2S494;
  int32_t _M0L6_2atmpS1284;
  int32_t _M0L6_2atmpS1283;
  uint64_t _M0L2vpS495;
  uint64_t _M0Lm2vmS497;
  int32_t _M0L6_2atmpS1282;
  int32_t _M0L6_2atmpS1281;
  uint64_t _M0L2vrS508;
  uint64_t _M0L6_2atmpS1280;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S479 = _M0L3mulS480.$0;
  _M0L7_2amul1S481 = _M0L3mulS480.$1;
  _M0L1mS482 = _M0L1mS483 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS484 = _M0FPB7umul128(_M0L1mS482, _M0L7_2amul0S479);
  _M0L5_2aloS485 = _M0L7_2abindS484.$0;
  _M0L6_2atmpS486 = _M0L7_2abindS484.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS487 = _M0FPB7umul128(_M0L1mS482, _M0L7_2amul1S481);
  _M0L6_2alo2S488 = _M0L7_2abindS487.$0;
  _M0L6_2ahi2S489 = _M0L7_2abindS487.$1;
  _M0L3midS490 = _M0L6_2atmpS486 + _M0L6_2alo2S488;
  if (_M0L3midS490 < _M0L6_2atmpS486) {
    _M0L6_2atmpS1288 = 1ull;
  } else {
    _M0L6_2atmpS1288 = 0ull;
  }
  _M0L2hiS491 = _M0L6_2ahi2S489 + _M0L6_2atmpS1288;
  _M0L3lo2S492 = _M0L5_2aloS485 + _M0L7_2amul0S479;
  _M0L6_2atmpS1286 = _M0L3midS490 + _M0L7_2amul1S481;
  if (_M0L3lo2S492 < _M0L5_2aloS485) {
    _M0L6_2atmpS1287 = 1ull;
  } else {
    _M0L6_2atmpS1287 = 0ull;
  }
  _M0L4mid2S493 = _M0L6_2atmpS1286 + _M0L6_2atmpS1287;
  if (_M0L4mid2S493 < _M0L3midS490) {
    _M0L6_2atmpS1285 = 1ull;
  } else {
    _M0L6_2atmpS1285 = 0ull;
  }
  _M0L3hi2S494 = _M0L2hiS491 + _M0L6_2atmpS1285;
  _M0L6_2atmpS1284 = _M0L1jS496 - 64;
  _M0L6_2atmpS1283 = _M0L6_2atmpS1284 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS495
  = _M0FPB13shiftright128(_M0L4mid2S493, _M0L3hi2S494, _M0L6_2atmpS1283);
  _M0Lm2vmS497 = 0ull;
  if (_M0L7mmShiftS498) {
    uint64_t _M0L3lo3S499 = _M0L5_2aloS485 - _M0L7_2amul0S479;
    uint64_t _M0L6_2atmpS1270 = _M0L3midS490 - _M0L7_2amul1S481;
    uint64_t _M0L6_2atmpS1271;
    uint64_t _M0L4mid3S500;
    uint64_t _M0L6_2atmpS1269;
    uint64_t _M0L3hi3S501;
    int32_t _M0L6_2atmpS1268;
    int32_t _M0L6_2atmpS1267;
    if (_M0L5_2aloS485 < _M0L3lo3S499) {
      _M0L6_2atmpS1271 = 1ull;
    } else {
      _M0L6_2atmpS1271 = 0ull;
    }
    _M0L4mid3S500 = _M0L6_2atmpS1270 - _M0L6_2atmpS1271;
    if (_M0L3midS490 < _M0L4mid3S500) {
      _M0L6_2atmpS1269 = 1ull;
    } else {
      _M0L6_2atmpS1269 = 0ull;
    }
    _M0L3hi3S501 = _M0L2hiS491 - _M0L6_2atmpS1269;
    _M0L6_2atmpS1268 = _M0L1jS496 - 64;
    _M0L6_2atmpS1267 = _M0L6_2atmpS1268 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS497
    = _M0FPB13shiftright128(_M0L4mid3S500, _M0L3hi3S501, _M0L6_2atmpS1267);
  } else {
    uint64_t _M0L3lo3S502 = _M0L5_2aloS485 + _M0L5_2aloS485;
    uint64_t _M0L6_2atmpS1278 = _M0L3midS490 + _M0L3midS490;
    uint64_t _M0L6_2atmpS1279;
    uint64_t _M0L4mid3S503;
    uint64_t _M0L6_2atmpS1276;
    uint64_t _M0L6_2atmpS1277;
    uint64_t _M0L3hi3S504;
    uint64_t _M0L3lo4S505;
    uint64_t _M0L6_2atmpS1274;
    uint64_t _M0L6_2atmpS1275;
    uint64_t _M0L4mid4S506;
    uint64_t _M0L6_2atmpS1273;
    uint64_t _M0L3hi4S507;
    int32_t _M0L6_2atmpS1272;
    if (_M0L3lo3S502 < _M0L5_2aloS485) {
      _M0L6_2atmpS1279 = 1ull;
    } else {
      _M0L6_2atmpS1279 = 0ull;
    }
    _M0L4mid3S503 = _M0L6_2atmpS1278 + _M0L6_2atmpS1279;
    _M0L6_2atmpS1276 = _M0L2hiS491 + _M0L2hiS491;
    if (_M0L4mid3S503 < _M0L3midS490) {
      _M0L6_2atmpS1277 = 1ull;
    } else {
      _M0L6_2atmpS1277 = 0ull;
    }
    _M0L3hi3S504 = _M0L6_2atmpS1276 + _M0L6_2atmpS1277;
    _M0L3lo4S505 = _M0L3lo3S502 - _M0L7_2amul0S479;
    _M0L6_2atmpS1274 = _M0L4mid3S503 - _M0L7_2amul1S481;
    if (_M0L3lo3S502 < _M0L3lo4S505) {
      _M0L6_2atmpS1275 = 1ull;
    } else {
      _M0L6_2atmpS1275 = 0ull;
    }
    _M0L4mid4S506 = _M0L6_2atmpS1274 - _M0L6_2atmpS1275;
    if (_M0L4mid3S503 < _M0L4mid4S506) {
      _M0L6_2atmpS1273 = 1ull;
    } else {
      _M0L6_2atmpS1273 = 0ull;
    }
    _M0L3hi4S507 = _M0L3hi3S504 - _M0L6_2atmpS1273;
    _M0L6_2atmpS1272 = _M0L1jS496 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS497
    = _M0FPB13shiftright128(_M0L4mid4S506, _M0L3hi4S507, _M0L6_2atmpS1272);
  }
  _M0L6_2atmpS1282 = _M0L1jS496 - 64;
  _M0L6_2atmpS1281 = _M0L6_2atmpS1282 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS508
  = _M0FPB13shiftright128(_M0L3midS490, _M0L2hiS491, _M0L6_2atmpS1281);
  _M0L6_2atmpS1280 = _M0Lm2vmS497;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS508,
                                                .$1 = _M0L2vpS495,
                                                .$2 = _M0L6_2atmpS1280};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS477,
  int32_t _M0L1pS478
) {
  uint64_t _M0L6_2atmpS1266;
  uint64_t _M0L6_2atmpS1265;
  uint64_t _M0L6_2atmpS1264;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1266 = 1ull << (_M0L1pS478 & 63);
  _M0L6_2atmpS1265 = _M0L6_2atmpS1266 - 1ull;
  _M0L6_2atmpS1264 = _M0L5valueS477 & _M0L6_2atmpS1265;
  return _M0L6_2atmpS1264 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS475,
  int32_t _M0L1pS476
) {
  int32_t _M0L6_2atmpS1263;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1263 = _M0FPB10pow5Factor(_M0L5valueS475);
  return _M0L6_2atmpS1263 >= _M0L1pS476;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS470) {
  uint64_t _M0L6_2atmpS1254;
  uint64_t _M0L6_2atmpS1255;
  uint64_t _M0L6_2atmpS1256;
  uint64_t _M0L6_2atmpS1257;
  uint64_t _M0L6_2atmpS1262;
  int32_t _M0L5countS471;
  uint64_t _M0L1vS472;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1254 = _M0L5valueS470 % 5ull;
  if (_M0L6_2atmpS1254 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1255 = _M0L5valueS470 % 25ull;
  if (_M0L6_2atmpS1255 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1256 = _M0L5valueS470 % 125ull;
  if (_M0L6_2atmpS1256 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1257 = _M0L5valueS470 % 625ull;
  if (_M0L6_2atmpS1257 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1262 = _M0L5valueS470 / 625ull;
  _M0L5countS471 = 4;
  _M0L1vS472 = _M0L6_2atmpS1262;
  while (1) {
    if (_M0L1vS472 > 0ull) {
      uint64_t _M0L6_2atmpS1258 = _M0L1vS472 % 5ull;
      int32_t _M0L6_2atmpS1259;
      uint64_t _M0L6_2atmpS1260;
      if (_M0L6_2atmpS1258 != 0ull) {
        return _M0L5countS471;
      }
      _M0L6_2atmpS1259 = _M0L5countS471 + 1;
      _M0L6_2atmpS1260 = _M0L1vS472 / 5ull;
      _M0L5countS471 = _M0L6_2atmpS1259;
      _M0L1vS472 = _M0L6_2atmpS1260;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS474;
      moonbit_string_t _M0L6_2atmpS1261;
      int32_t _result_1700;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS474
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS474, (moonbit_string_t)moonbit_string_literal_10.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS474, _M0L5valueS470);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1261
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS474);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS474);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_1700 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1261);
      moonbit_decref_cycle_free(_M0L6_2atmpS1261);
      return _result_1700;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS469,
  uint64_t _M0L2hiS467,
  int32_t _M0L4distS468
) {
  int32_t _M0L6_2atmpS1253;
  uint64_t _M0L6_2atmpS1251;
  uint64_t _M0L6_2atmpS1252;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1253 = 64 - _M0L4distS468;
  _M0L6_2atmpS1251 = _M0L2hiS467 << (_M0L6_2atmpS1253 & 63);
  _M0L6_2atmpS1252 = _M0L2loS469 >> (_M0L4distS468 & 63);
  return _M0L6_2atmpS1251 | _M0L6_2atmpS1252;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS457,
  uint64_t _M0L1bS460
) {
  uint64_t _M0L3aLoS456;
  uint64_t _M0L3aHiS458;
  uint64_t _M0L3bLoS459;
  uint64_t _M0L3bHiS461;
  uint64_t _M0L1xS462;
  uint64_t _M0L6_2atmpS1249;
  uint64_t _M0L6_2atmpS1250;
  uint64_t _M0L1yS463;
  uint64_t _M0L6_2atmpS1247;
  uint64_t _M0L6_2atmpS1248;
  uint64_t _M0L1zS464;
  uint64_t _M0L6_2atmpS1245;
  uint64_t _M0L6_2atmpS1246;
  uint64_t _M0L6_2atmpS1243;
  uint64_t _M0L6_2atmpS1244;
  uint64_t _M0L1wS465;
  uint64_t _M0L2loS466;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS456 = _M0L1aS457 & 4294967295ull;
  _M0L3aHiS458 = _M0L1aS457 >> 32;
  _M0L3bLoS459 = _M0L1bS460 & 4294967295ull;
  _M0L3bHiS461 = _M0L1bS460 >> 32;
  _M0L1xS462 = _M0L3aLoS456 * _M0L3bLoS459;
  _M0L6_2atmpS1249 = _M0L3aHiS458 * _M0L3bLoS459;
  _M0L6_2atmpS1250 = _M0L1xS462 >> 32;
  _M0L1yS463 = _M0L6_2atmpS1249 + _M0L6_2atmpS1250;
  _M0L6_2atmpS1247 = _M0L3aLoS456 * _M0L3bHiS461;
  _M0L6_2atmpS1248 = _M0L1yS463 & 4294967295ull;
  _M0L1zS464 = _M0L6_2atmpS1247 + _M0L6_2atmpS1248;
  _M0L6_2atmpS1245 = _M0L3aHiS458 * _M0L3bHiS461;
  _M0L6_2atmpS1246 = _M0L1yS463 >> 32;
  _M0L6_2atmpS1243 = _M0L6_2atmpS1245 + _M0L6_2atmpS1246;
  _M0L6_2atmpS1244 = _M0L1zS464 >> 32;
  _M0L1wS465 = _M0L6_2atmpS1243 + _M0L6_2atmpS1244;
  _M0L2loS466 = _M0L1aS457 * _M0L1bS460;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS466, .$1 = _M0L1wS465};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS454,
  int32_t _M0L4fromS451,
  int32_t _M0L2toS450
) {
  int32_t _M0L3lenS449;
  int32_t _M0L6_2atmpS1242;
  uint16_t* _M0L6bufferS452;
  int32_t _M0L1iS453;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS449 = _M0L2toS450 - _M0L4fromS451;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1242 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS452
  = (uint16_t*)moonbit_make_string(_M0L3lenS449, _M0L6_2atmpS1242);
  _M0L1iS453 = 0;
  while (1) {
    if (_M0L1iS453 < _M0L3lenS449) {
      int32_t _M0L6_2atmpS1240 = _M0L4fromS451 + _M0L1iS453;
      int32_t _M0L6_2atmpS1239;
      int32_t _M0L6_2atmpS1238;
      int32_t _M0L6_2atmpS1241;
      if (
        _M0L6_2atmpS1240 < 0
        || _M0L6_2atmpS1240 >= Moonbit_array_length(_M0L5bytesS454)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1239 = (int32_t)_M0L5bytesS454[_M0L6_2atmpS1240];
      _M0L6_2atmpS1238 = (uint16_t)_M0L6_2atmpS1239;
      if (
        _M0L1iS453 < 0 || _M0L1iS453 >= Moonbit_array_length(_M0L6bufferS452)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS452[_M0L1iS453] = _M0L6_2atmpS1238;
      _M0L6_2atmpS1241 = _M0L1iS453 + 1;
      _M0L1iS453 = _M0L6_2atmpS1241;
      continue;
    }
    break;
  }
  return _M0L6bufferS452;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS448) {
  int32_t _M0L6_2atmpS1237;
  uint32_t _M0L6_2atmpS1236;
  uint32_t _M0L6_2atmpS1235;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1237 = _M0L1eS448 * 78913;
  _M0L6_2atmpS1236 = *(uint32_t*)&_M0L6_2atmpS1237;
  _M0L6_2atmpS1235 = _M0L6_2atmpS1236 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1235;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS447) {
  int32_t _M0L6_2atmpS1234;
  uint32_t _M0L6_2atmpS1233;
  uint32_t _M0L6_2atmpS1232;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1234 = _M0L1eS447 * 732923;
  _M0L6_2atmpS1233 = *(uint32_t*)&_M0L6_2atmpS1234;
  _M0L6_2atmpS1232 = _M0L6_2atmpS1233 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1232;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS445,
  int32_t _M0L8exponentS446,
  int32_t _M0L8mantissaS443
) {
  moonbit_string_t _M0L1sS444;
  moonbit_string_t _result_1703;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS443) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  if (_M0L4signS445) {
    _M0L1sS444 = (moonbit_string_t)moonbit_string_literal_12.data;
  } else {
    _M0L1sS444 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS446) {
    moonbit_string_t _result_1702;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1702
    = moonbit_add_string(_M0L1sS444, (moonbit_string_t)moonbit_string_literal_13.data);
    moonbit_decref_cycle_free(_M0L1sS444);
    return _result_1702;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1703
  = moonbit_add_string(_M0L1sS444, (moonbit_string_t)moonbit_string_literal_14.data);
  moonbit_decref_cycle_free(_M0L1sS444);
  return _result_1703;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS442) {
  int32_t _M0L6_2atmpS1231;
  uint32_t _M0L6_2atmpS1230;
  uint32_t _M0L6_2atmpS1229;
  int32_t _M0L6_2atmpS1228;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1231 = _M0L1eS442 * 1217359;
  _M0L6_2atmpS1230 = *(uint32_t*)&_M0L6_2atmpS1231;
  _M0L6_2atmpS1229 = _M0L6_2atmpS1230 >> 19;
  _M0L6_2atmpS1228 = *(int32_t*)&_M0L6_2atmpS1229;
  return _M0L6_2atmpS1228 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS441) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS441 != _M0L4selfS441) {
    return 0;
  } else if (_M0L4selfS441 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS441 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS441;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS440) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS440 != _M0L4selfS440) {
    return 0ll;
  } else if (_M0L4selfS440 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS440 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS440;
  }
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS436,
  int32_t _M0L5indexS437
) {
  uint64_t* _M0L6_2atmpS1226;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1226 = _M0L4selfS436;
  if (
    _M0L5indexS437 < 0
    || _M0L5indexS437 >= Moonbit_array_length(_M0L6_2atmpS1226)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1226[_M0L5indexS437];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS438,
  int32_t _M0L5indexS439
) {
  uint32_t* _M0L6_2atmpS1227;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1227 = _M0L4selfS438;
  if (
    _M0L5indexS439 < 0
    || _M0L5indexS439 >= Moonbit_array_length(_M0L6_2atmpS1227)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1227[_M0L5indexS439];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS435
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS435, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS434) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS434, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS433) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS433;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS427,
  moonbit_string_t _M0L5valueS429
) {
  int32_t _M0L3lenS1212;
  moonbit_string_t* _M0L6_2atmpS1214;
  int32_t _M0L6_2atmpS1213;
  int32_t _M0L6lengthS428;
  moonbit_string_t* _M0L3bufS1217;
  moonbit_string_t _M0L6_2aoldS1636;
  int32_t _M0L6_2atmpS1218;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1212 = _M0L4selfS427->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1214 = _M0MPC15array5Array6bufferGsE(_M0L4selfS427);
  _M0L6_2atmpS1213 = Moonbit_array_length(_M0L6_2atmpS1214);
  moonbit_decref_cycle_free(_M0L6_2atmpS1214);
  if (_M0L3lenS1212 == _M0L6_2atmpS1213) {
    int32_t _M0L3lenS1216 = _M0L4selfS427->$1;
    int32_t _M0L6_2atmpS1215 = _M0L3lenS1216 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS427, _M0L6_2atmpS1215);
  }
  _M0L6lengthS428 = _M0L4selfS427->$1;
  _M0L3bufS1217 = _M0L4selfS427->$0;
  _M0L6_2aoldS1636 = (moonbit_string_t)_M0L3bufS1217[_M0L6lengthS428];
  moonbit_decref_cycle_free(_M0L6_2aoldS1636);
  _M0L3bufS1217[_M0L6lengthS428] = _M0L5valueS429;
  _M0L6_2atmpS1218 = _M0L6lengthS428 + 1;
  _M0L4selfS427->$1 = _M0L6_2atmpS1218;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS430,
  struct _M0TUsiE* _M0L5valueS432
) {
  int32_t _M0L3lenS1219;
  struct _M0TUsiE** _M0L6_2atmpS1221;
  int32_t _M0L6_2atmpS1220;
  int32_t _M0L6lengthS431;
  struct _M0TUsiE** _M0L3bufS1224;
  struct _M0TUsiE* _M0L6_2aoldS1637;
  int32_t _M0L6_2atmpS1225;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1219 = _M0L4selfS430->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1221 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS430);
  _M0L6_2atmpS1220 = Moonbit_array_length(_M0L6_2atmpS1221);
  moonbit_decref_cycle_free(_M0L6_2atmpS1221);
  if (_M0L3lenS1219 == _M0L6_2atmpS1220) {
    int32_t _M0L3lenS1223 = _M0L4selfS430->$1;
    int32_t _M0L6_2atmpS1222 = _M0L3lenS1223 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS430, _M0L6_2atmpS1222);
  }
  _M0L6lengthS431 = _M0L4selfS430->$1;
  _M0L3bufS1224 = _M0L4selfS430->$0;
  _M0L6_2aoldS1637 = (struct _M0TUsiE*)_M0L3bufS1224[_M0L6lengthS431];
  if (_M0L6_2aoldS1637) {
    moonbit_decref_cycle_free(_M0L6_2aoldS1637);
  }
  _M0L3bufS1224[_M0L6lengthS431] = _M0L5valueS432;
  _M0L6_2atmpS1225 = _M0L6lengthS431 + 1;
  _M0L4selfS430->$1 = _M0L6_2atmpS1225;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS420,
  int32_t _M0L8requiredS422
) {
  int32_t _M0L8old__capS419;
  int32_t _M0L3lenS1210;
  int32_t _M0L8new__capS421;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS419 = _M0MPC15array5Array8capacityGsE(_M0L4selfS420);
  _M0L3lenS1210 = _M0L4selfS420->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS421
  = _M0FPB23array__growth__capacity(_M0L8old__capS419, _M0L3lenS1210, _M0L8requiredS422);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS420, _M0L8new__capS421);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS424,
  int32_t _M0L8requiredS426
) {
  int32_t _M0L8old__capS423;
  int32_t _M0L3lenS1211;
  int32_t _M0L8new__capS425;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS423 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS424);
  _M0L3lenS1211 = _M0L4selfS424->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS425
  = _M0FPB23array__growth__capacity(_M0L8old__capS423, _M0L3lenS1211, _M0L8requiredS426);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS424, _M0L8new__capS425);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS408,
  int32_t _M0L13new__capacityS411
) {
  moonbit_string_t* _M0L8old__bufS407;
  int32_t _M0L3lenS409;
  int32_t _M0L9copy__lenS410;
  moonbit_string_t* _M0L8new__bufS412;
  moonbit_string_t* _M0L6_2aoldS1638;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS407 = _M0L4selfS408->$0;
  _M0L3lenS409 = _M0L4selfS408->$1;
  if (_M0L3lenS409 < _M0L13new__capacityS411) {
    _M0L9copy__lenS410 = _M0L3lenS409;
  } else {
    _M0L9copy__lenS410 = _M0L13new__capacityS411;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS407);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS412
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS407, _M0L13new__capacityS411, _M0L9copy__lenS410, 0, 0);
  _M0L6_2aoldS1638 = _M0L4selfS408->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1638);
  _M0L4selfS408->$0 = _M0L8new__bufS412;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS414,
  int32_t _M0L13new__capacityS417
) {
  struct _M0TUsiE** _M0L8old__bufS413;
  int32_t _M0L3lenS415;
  int32_t _M0L9copy__lenS416;
  struct _M0TUsiE** _M0L8new__bufS418;
  struct _M0TUsiE** _M0L6_2aoldS1639;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS413 = _M0L4selfS414->$0;
  _M0L3lenS415 = _M0L4selfS414->$1;
  if (_M0L3lenS415 < _M0L13new__capacityS417) {
    _M0L9copy__lenS416 = _M0L3lenS415;
  } else {
    _M0L9copy__lenS416 = _M0L13new__capacityS417;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS413);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS418
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS413, _M0L13new__capacityS417, _M0L9copy__lenS416, 0, 0);
  _M0L6_2aoldS1639 = _M0L4selfS414->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1639);
  _M0L4selfS414->$0 = _M0L8new__bufS418;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS405
) {
  moonbit_string_t* _M0L6_2atmpS1208;
  int32_t _result_1704;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1208 = _M0MPC15array5Array6bufferGsE(_M0L4selfS405);
  _result_1704 = Moonbit_array_length(_M0L6_2atmpS1208);
  moonbit_decref_cycle_free(_M0L6_2atmpS1208);
  return _result_1704;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS406
) {
  struct _M0TUsiE** _M0L6_2atmpS1209;
  int32_t _result_1705;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1209 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS406);
  _result_1705 = Moonbit_array_length(_M0L6_2atmpS1209);
  moonbit_decref_cycle_free(_M0L6_2atmpS1209);
  return _result_1705;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS401,
  int32_t _M0L3lenS399,
  int32_t _M0L8requiredS398
) {
  int32_t _M0L5startS400;
  int32_t _M0L5spaceS402;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS398 < _M0L3lenS399) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_15.data);
  }
  if (_M0L7currentS401 == 0) {
    _M0L5startS400 = 8;
  } else {
    _M0L5startS400 = _M0L7currentS401;
  }
  _M0L5spaceS402 = _M0L5startS400;
  while (1) {
    if (_M0L5spaceS402 < _M0L8requiredS398) {
      int32_t _M0L4nextS403 = _M0L5spaceS402 * 2;
      if (_M0L4nextS403 <= _M0L5spaceS402) {
        return _M0L8requiredS398;
      }
      _M0L5spaceS402 = _M0L4nextS403;
      continue;
    } else {
      return _M0L5spaceS402;
    }
    break;
  }
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS396
) {
  moonbit_string_t* _M0L8_2afieldS1640;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1640 = _M0L4selfS396->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1640);
  return _M0L8_2afieldS1640;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS397
) {
  struct _M0TUsiE** _M0L8_2afieldS1641;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1641 = _M0L4selfS397->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1641);
  return _M0L8_2afieldS1641;
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
  int32_t _M0L3endS1206;
  int32_t _M0L5startS1207;
  int32_t _M0L8str__lenS391;
  int32_t _M0L3lenS1205;
  int32_t _M0L8requiredS393;
  uint16_t* _M0L4dataS1198;
  int32_t _M0L6_2atmpS1197;
  int32_t _if__result_1707;
  uint16_t* _M0L4dataS1199;
  int32_t _M0L3lenS1200;
  moonbit_string_t _M0L6_2atmpS1201;
  int32_t _M0L6_2atmpS1202;
  int32_t _M0L3lenS1204;
  int32_t _M0L6_2atmpS1203;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1206 = _M0L3strS392.$2;
  _M0L5startS1207 = _M0L3strS392.$1;
  _M0L8str__lenS391 = _M0L3endS1206 - _M0L5startS1207;
  if (_M0L8str__lenS391 == 0) {
    return 0;
  }
  _M0L3lenS1205 = _M0L4selfS394->$1;
  _M0L8requiredS393 = _M0L3lenS1205 + _M0L8str__lenS391;
  _M0L4dataS1198 = _M0L4selfS394->$0;
  _M0L6_2atmpS1197 = Moonbit_array_length(_M0L4dataS1198);
  if (_M0L8requiredS393 > _M0L6_2atmpS1197) {
    _if__result_1707 = 1;
  } else {
    int32_t _M0L3lenS1196 = _M0L4selfS394->$1;
    _if__result_1707 = _M0L8requiredS393 < _M0L3lenS1196;
  }
  if (_if__result_1707) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS394, _M0L8requiredS393);
  }
  _M0L4dataS1199 = _M0L4selfS394->$0;
  _M0L3lenS1200 = _M0L4selfS394->$1;
  moonbit_incref_cycle_free(_M0L4dataS1199);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1201 = _M0MPC16string10StringView4data(_M0L3strS392);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1202 = _M0MPC16string10StringView13start__offset(_M0L3strS392);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1199, _M0L3lenS1200, _M0L6_2atmpS1201, _M0L6_2atmpS1202, _M0L8str__lenS391);
  moonbit_decref_cycle_free(_M0L4dataS1199);
  moonbit_decref_cycle_free(_M0L6_2atmpS1201);
  _M0L3lenS1204 = _M0L4selfS394->$1;
  _M0L6_2atmpS1203 = _M0L3lenS1204 + _M0L8str__lenS391;
  _M0L4selfS394->$1 = _M0L6_2atmpS1203;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS388,
  int32_t _M0L5startS386,
  int32_t _M0L3endS387
) {
  int32_t _if__result_1708;
  int32_t _M0L3lenS389;
  int32_t _M0L6_2atmpS1195;
  moonbit_bytes_t _M0L5bytesS390;
  moonbit_bytes_t _M0L6_2atmpS1194;
  moonbit_string_t _result_1709;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS386 == 0) {
    int32_t _M0L6_2atmpS1193 = Moonbit_array_length(_M0L3strS388);
    _if__result_1708 = _M0L3endS387 == _M0L6_2atmpS1193;
  } else {
    _if__result_1708 = 0;
  }
  if (_if__result_1708) {
    moonbit_incref_cycle_free(_M0L3strS388);
    return _M0L3strS388;
  }
  _M0L3lenS389 = _M0L3endS387 - _M0L5startS386;
  _M0L6_2atmpS1195 = _M0L3lenS389 * 2;
  _M0L5bytesS390 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1195, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS390, 0, _M0L3strS388, _M0L5startS386, _M0L3lenS389);
  _M0L6_2atmpS1194 = _M0L5bytesS390;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_1709
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1194, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1194);
  return _result_1709;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS381,
  int32_t _M0L6offsetS385,
  int64_t _M0L6lengthS383
) {
  int32_t _M0L3lenS380;
  int32_t _M0L6lengthS382;
  int32_t _if__result_1710;
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
      int32_t _M0L6_2atmpS1192 = _M0L6offsetS385 + _M0L6lengthS382;
      _if__result_1710 = _M0L6_2atmpS1192 <= _M0L3lenS380;
    } else {
      _if__result_1710 = 0;
    }
  } else {
    _if__result_1710 = 0;
  }
  if (_if__result_1710) {
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
  int32_t _M0L6_2atmpS1191;
  int32_t _M0L6_2atmpS1190;
  int32_t _M0L2e1S366;
  int32_t _M0L6_2atmpS1189;
  int32_t _M0L2e2S369;
  int32_t _M0L4len1S371;
  int32_t _M0L4len2S373;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1191 = _M0L6lengthS368 * 2;
  _M0L6_2atmpS1190 = _M0L13bytes__offsetS367 + _M0L6_2atmpS1191;
  _M0L2e1S366 = _M0L6_2atmpS1190 - 1;
  _M0L6_2atmpS1189 = _M0L11str__offsetS370 + _M0L6lengthS368;
  _M0L2e2S369 = _M0L6_2atmpS1189 - 1;
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
        int32_t _M0L6_2atmpS1186 = _M0L3strS374[_M0L1iS376];
        int32_t _M0L6_2atmpS1185 = (int32_t)_M0L6_2atmpS1186;
        uint32_t _M0L1cS378 = *(uint32_t*)&_M0L6_2atmpS1185;
        uint32_t _M0L6_2atmpS1181 = _M0L1cS378 & 255u;
        int32_t _M0L6_2atmpS1180;
        int32_t _M0L6_2atmpS1182;
        uint32_t _M0L6_2atmpS1184;
        int32_t _M0L6_2atmpS1183;
        int32_t _M0L6_2atmpS1187;
        int32_t _M0L6_2atmpS1188;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1180 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1181);
        if (
          _M0L1jS377 < 0 || _M0L1jS377 >= Moonbit_array_length(_M0L4selfS372)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS372[_M0L1jS377] = _M0L6_2atmpS1180;
        _M0L6_2atmpS1182 = _M0L1jS377 + 1;
        _M0L6_2atmpS1184 = _M0L1cS378 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1183 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1184);
        if (
          _M0L6_2atmpS1182 < 0
          || _M0L6_2atmpS1182 >= Moonbit_array_length(_M0L4selfS372)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS372[_M0L6_2atmpS1182] = _M0L6_2atmpS1183;
        _M0L6_2atmpS1187 = _M0L1iS376 + 1;
        _M0L6_2atmpS1188 = _M0L1jS377 + 2;
        _M0L1iS376 = _M0L6_2atmpS1187;
        _M0L1jS377 = _M0L6_2atmpS1188;
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
  int32_t _M0L6_2atmpS1179;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1179 = *(int32_t*)&_M0L4selfS365;
  return _M0L6_2atmpS1179 & 0xff;
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
    int64_t _M0L6_2atmpS1178 = -_M0L4selfS340;
    _M0L3numS342 = *(uint64_t*)&_M0L6_2atmpS1178;
  } else {
    _M0L3numS342 = *(uint64_t*)&_M0L4selfS340;
  }
  switch (_M0L5radixS339) {
    case 10: {
      int32_t _M0L10digit__lenS344;
      int32_t _M0L6_2atmpS1175;
      int32_t _M0L10total__lenS345;
      uint16_t* _M0L6bufferS346;
      int32_t _M0L12digit__startS347;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS344 = _M0FPB12dec__count64(_M0L3numS342);
      if (_M0L12is__negativeS341) {
        _M0L6_2atmpS1175 = 1;
      } else {
        _M0L6_2atmpS1175 = 0;
      }
      _M0L10total__lenS345 = _M0L10digit__lenS344 + _M0L6_2atmpS1175;
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
      int32_t _M0L6_2atmpS1176;
      int32_t _M0L10total__lenS349;
      uint16_t* _M0L6bufferS350;
      int32_t _M0L12digit__startS351;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS348 = _M0FPB12hex__count64(_M0L3numS342);
      if (_M0L12is__negativeS341) {
        _M0L6_2atmpS1176 = 1;
      } else {
        _M0L6_2atmpS1176 = 0;
      }
      _M0L10total__lenS349 = _M0L10digit__lenS348 + _M0L6_2atmpS1176;
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
      int32_t _M0L6_2atmpS1177;
      int32_t _M0L10total__lenS353;
      uint16_t* _M0L6bufferS354;
      int32_t _M0L12digit__startS355;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS352
      = _M0FPB14radix__count64(_M0L3numS342, _M0L5radixS339);
      if (_M0L12is__negativeS341) {
        _M0L6_2atmpS1177 = 1;
      } else {
        _M0L6_2atmpS1177 = 0;
      }
      _M0L10total__lenS353 = _M0L10digit__lenS352 + _M0L6_2atmpS1177;
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
  int32_t _M0L6_2atmpS1174;
  uint64_t _M0L3numS315;
  int32_t _M0L6offsetS316;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1174 = _M0L10total__lenS338 - _M0L12digit__startS326;
  _M0L3numS315 = _M0L3numS337;
  _M0L6offsetS316 = _M0L6_2atmpS1174;
  while (1) {
    if (_M0L3numS315 >= 10000ull) {
      uint64_t _M0L1tS317 = _M0L3numS315 / 10000ull;
      uint64_t _M0L6_2atmpS1151 = _M0L3numS315 % 10000ull;
      int32_t _M0L1rS318 = (int32_t)_M0L6_2atmpS1151;
      int32_t _M0L2d1S319 = _M0L1rS318 / 100;
      int32_t _M0L2d2S320 = _M0L1rS318 % 100;
      int32_t _M0L6_2atmpS1150 = _M0L2d1S319 / 10;
      int32_t _M0L6_2atmpS1149 = 48 + _M0L6_2atmpS1150;
      int32_t _M0L6d1__hiS321 = (uint16_t)_M0L6_2atmpS1149;
      int32_t _M0L6_2atmpS1148 = _M0L2d1S319 % 10;
      int32_t _M0L6_2atmpS1147 = 48 + _M0L6_2atmpS1148;
      int32_t _M0L6d1__loS322 = (uint16_t)_M0L6_2atmpS1147;
      int32_t _M0L6_2atmpS1146 = _M0L2d2S320 / 10;
      int32_t _M0L6_2atmpS1145 = 48 + _M0L6_2atmpS1146;
      int32_t _M0L6d2__hiS323 = (uint16_t)_M0L6_2atmpS1145;
      int32_t _M0L6_2atmpS1144 = _M0L2d2S320 % 10;
      int32_t _M0L6_2atmpS1143 = 48 + _M0L6_2atmpS1144;
      int32_t _M0L6d2__loS324 = (uint16_t)_M0L6_2atmpS1143;
      int32_t _M0L6_2atmpS1135 = _M0L12digit__startS326 + _M0L6offsetS316;
      int32_t _M0L6_2atmpS1134 = _M0L6_2atmpS1135 - 4;
      int32_t _M0L6_2atmpS1137;
      int32_t _M0L6_2atmpS1136;
      int32_t _M0L6_2atmpS1139;
      int32_t _M0L6_2atmpS1138;
      int32_t _M0L6_2atmpS1141;
      int32_t _M0L6_2atmpS1140;
      int32_t _M0L6_2atmpS1142;
      _M0L6bufferS325[_M0L6_2atmpS1134] = _M0L6d1__hiS321;
      _M0L6_2atmpS1137 = _M0L12digit__startS326 + _M0L6offsetS316;
      _M0L6_2atmpS1136 = _M0L6_2atmpS1137 - 3;
      _M0L6bufferS325[_M0L6_2atmpS1136] = _M0L6d1__loS322;
      _M0L6_2atmpS1139 = _M0L12digit__startS326 + _M0L6offsetS316;
      _M0L6_2atmpS1138 = _M0L6_2atmpS1139 - 2;
      _M0L6bufferS325[_M0L6_2atmpS1138] = _M0L6d2__hiS323;
      _M0L6_2atmpS1141 = _M0L12digit__startS326 + _M0L6offsetS316;
      _M0L6_2atmpS1140 = _M0L6_2atmpS1141 - 1;
      _M0L6bufferS325[_M0L6_2atmpS1140] = _M0L6d2__loS324;
      _M0L6_2atmpS1142 = _M0L6offsetS316 - 4;
      _M0L3numS315 = _M0L1tS317;
      _M0L6offsetS316 = _M0L6_2atmpS1142;
      continue;
    } else {
      int32_t _M0L6_2atmpS1173 = (int32_t)_M0L3numS315;
      int32_t _M0L9remainingS328 = _M0L6_2atmpS1173;
      int32_t _M0L6offsetS329 = _M0L6offsetS316;
      while (1) {
        if (_M0L9remainingS328 >= 100) {
          int32_t _M0L1tS330 = _M0L9remainingS328 / 100;
          int32_t _M0L1dS331 = _M0L9remainingS328 % 100;
          int32_t _M0L6_2atmpS1160 = _M0L1dS331 / 10;
          int32_t _M0L6_2atmpS1159 = 48 + _M0L6_2atmpS1160;
          int32_t _M0L5d__hiS332 = (uint16_t)_M0L6_2atmpS1159;
          int32_t _M0L6_2atmpS1158 = _M0L1dS331 % 10;
          int32_t _M0L6_2atmpS1157 = 48 + _M0L6_2atmpS1158;
          int32_t _M0L5d__loS333 = (uint16_t)_M0L6_2atmpS1157;
          int32_t _M0L6_2atmpS1153 = _M0L12digit__startS326 + _M0L6offsetS329;
          int32_t _M0L6_2atmpS1152 = _M0L6_2atmpS1153 - 2;
          int32_t _M0L6_2atmpS1155;
          int32_t _M0L6_2atmpS1154;
          int32_t _M0L6_2atmpS1156;
          _M0L6bufferS325[_M0L6_2atmpS1152] = _M0L5d__hiS332;
          _M0L6_2atmpS1155 = _M0L12digit__startS326 + _M0L6offsetS329;
          _M0L6_2atmpS1154 = _M0L6_2atmpS1155 - 1;
          _M0L6bufferS325[_M0L6_2atmpS1154] = _M0L5d__loS333;
          _M0L6_2atmpS1156 = _M0L6offsetS329 - 2;
          _M0L9remainingS328 = _M0L1tS330;
          _M0L6offsetS329 = _M0L6_2atmpS1156;
          continue;
        } else if (_M0L9remainingS328 >= 10) {
          int32_t _M0L6_2atmpS1168 = _M0L9remainingS328 / 10;
          int32_t _M0L6_2atmpS1167 = 48 + _M0L6_2atmpS1168;
          int32_t _M0L5d__hiS335 = (uint16_t)_M0L6_2atmpS1167;
          int32_t _M0L6_2atmpS1166 = _M0L9remainingS328 % 10;
          int32_t _M0L6_2atmpS1165 = 48 + _M0L6_2atmpS1166;
          int32_t _M0L5d__loS336 = (uint16_t)_M0L6_2atmpS1165;
          int32_t _M0L6_2atmpS1162 = _M0L12digit__startS326 + _M0L6offsetS329;
          int32_t _M0L6_2atmpS1161 = _M0L6_2atmpS1162 - 2;
          int32_t _M0L6_2atmpS1164;
          int32_t _M0L6_2atmpS1163;
          _M0L6bufferS325[_M0L6_2atmpS1161] = _M0L5d__hiS335;
          _M0L6_2atmpS1164 = _M0L12digit__startS326 + _M0L6offsetS329;
          _M0L6_2atmpS1163 = _M0L6_2atmpS1164 - 1;
          _M0L6bufferS325[_M0L6_2atmpS1163] = _M0L5d__loS336;
        } else {
          int32_t _M0L6_2atmpS1172 = _M0L12digit__startS326 + _M0L6offsetS329;
          int32_t _M0L6_2atmpS1169 = _M0L6_2atmpS1172 - 1;
          int32_t _M0L6_2atmpS1171 = 48 + _M0L9remainingS328;
          int32_t _M0L6_2atmpS1170 = (uint16_t)_M0L6_2atmpS1171;
          _M0L6bufferS325[_M0L6_2atmpS1169] = _M0L6_2atmpS1170;
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
  int32_t _M0L6_2atmpS1119;
  int32_t _M0L6_2atmpS1118;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS298 = _M0MPC13int3Int10to__uint64(_M0L5radixS299);
  _M0L6_2atmpS1119 = _M0L5radixS299 - 1;
  _M0L6_2atmpS1118 = _M0L5radixS299 & _M0L6_2atmpS1119;
  if (_M0L6_2atmpS1118 == 0) {
    int32_t _M0L5shiftS300;
    uint64_t _M0L4maskS301;
    int32_t _M0L6_2atmpS1126;
    int32_t _M0L6offsetS302;
    uint64_t _M0L1nS303;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS300 = moonbit_ctz32(_M0L5radixS299);
    _M0L4maskS301 = _M0L4baseS298 - 1ull;
    _M0L6_2atmpS1126 = _M0L10total__lenS308 - _M0L12digit__startS306;
    _M0L6offsetS302 = _M0L6_2atmpS1126;
    _M0L1nS303 = _M0L3numS309;
    while (1) {
      if (_M0L1nS303 > 0ull) {
        uint64_t _M0L6_2atmpS1125 = _M0L1nS303 & _M0L4maskS301;
        int32_t _M0L5digitS304 = (int32_t)_M0L6_2atmpS1125;
        int32_t _M0L6_2atmpS1122 = _M0L12digit__startS306 + _M0L6offsetS302;
        int32_t _M0L6_2atmpS1120 = _M0L6_2atmpS1122 - 1;
        int32_t _M0L6_2atmpS1121 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS304];
        int32_t _M0L6_2atmpS1123;
        uint64_t _M0L6_2atmpS1124;
        _M0L6bufferS305[_M0L6_2atmpS1120] = _M0L6_2atmpS1121;
        _M0L6_2atmpS1123 = _M0L6offsetS302 - 1;
        _M0L6_2atmpS1124 = _M0L1nS303 >> (_M0L5shiftS300 & 63);
        _M0L6offsetS302 = _M0L6_2atmpS1123;
        _M0L1nS303 = _M0L6_2atmpS1124;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1133 = _M0L10total__lenS308 - _M0L12digit__startS306;
    int32_t _M0L6offsetS310 = _M0L6_2atmpS1133;
    uint64_t _M0L1nS311 = _M0L3numS309;
    while (1) {
      if (_M0L1nS311 > 0ull) {
        uint64_t _M0L1qS312 = _M0L1nS311 / _M0L4baseS298;
        uint64_t _M0L6_2atmpS1132 = _M0L1qS312 * _M0L4baseS298;
        uint64_t _M0L6_2atmpS1131 = _M0L1nS311 - _M0L6_2atmpS1132;
        int32_t _M0L5digitS313 = (int32_t)_M0L6_2atmpS1131;
        int32_t _M0L6_2atmpS1129 = _M0L12digit__startS306 + _M0L6offsetS310;
        int32_t _M0L6_2atmpS1127 = _M0L6_2atmpS1129 - 1;
        int32_t _M0L6_2atmpS1128 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS313];
        int32_t _M0L6_2atmpS1130;
        _M0L6bufferS305[_M0L6_2atmpS1127] = _M0L6_2atmpS1128;
        _M0L6_2atmpS1130 = _M0L6offsetS310 - 1;
        _M0L6offsetS310 = _M0L6_2atmpS1130;
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
  int32_t _M0L6_2atmpS1117;
  int32_t _M0L6offsetS287;
  uint64_t _M0L1nS288;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1117 = _M0L10total__lenS296 - _M0L12digit__startS293;
  _M0L6offsetS287 = _M0L6_2atmpS1117;
  _M0L1nS288 = _M0L3numS297;
  while (1) {
    if (_M0L6offsetS287 >= 2) {
      uint64_t _M0L6_2atmpS1114 = _M0L1nS288 & 255ull;
      int32_t _M0L9byte__valS289 = (int32_t)_M0L6_2atmpS1114;
      int32_t _M0L2hiS290 = _M0L9byte__valS289 / 16;
      int32_t _M0L2loS291 = _M0L9byte__valS289 % 16;
      int32_t _M0L6_2atmpS1108 = _M0L12digit__startS293 + _M0L6offsetS287;
      int32_t _M0L6_2atmpS1106 = _M0L6_2atmpS1108 - 2;
      int32_t _M0L6_2atmpS1107 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L2hiS290];
      int32_t _M0L6_2atmpS1111;
      int32_t _M0L6_2atmpS1109;
      int32_t _M0L6_2atmpS1110;
      int32_t _M0L6_2atmpS1112;
      uint64_t _M0L6_2atmpS1113;
      _M0L6bufferS292[_M0L6_2atmpS1106] = _M0L6_2atmpS1107;
      _M0L6_2atmpS1111 = _M0L12digit__startS293 + _M0L6offsetS287;
      _M0L6_2atmpS1109 = _M0L6_2atmpS1111 - 1;
      _M0L6_2atmpS1110
      = ((moonbit_string_t)moonbit_string_literal_17.data)[
        _M0L2loS291
      ];
      _M0L6bufferS292[_M0L6_2atmpS1109] = _M0L6_2atmpS1110;
      _M0L6_2atmpS1112 = _M0L6offsetS287 - 2;
      _M0L6_2atmpS1113 = _M0L1nS288 >> 8;
      _M0L6offsetS287 = _M0L6_2atmpS1112;
      _M0L1nS288 = _M0L6_2atmpS1113;
      continue;
    } else if (_M0L6offsetS287 == 1) {
      uint64_t _M0L6_2atmpS1116 = _M0L1nS288 & 15ull;
      int32_t _M0L6nibbleS295 = (int32_t)_M0L6_2atmpS1116;
      int32_t _M0L6_2atmpS1115 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L6nibbleS295];
      _M0L6bufferS292[_M0L12digit__startS293] = _M0L6_2atmpS1115;
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
      uint64_t _M0L6_2atmpS1104 = _M0L3numS284 / _M0L4baseS282;
      int32_t _M0L6_2atmpS1105 = _M0L5countS285 + 1;
      _M0L3numS284 = _M0L6_2atmpS1104;
      _M0L5countS285 = _M0L6_2atmpS1105;
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
    int32_t _M0L6_2atmpS1103;
    int32_t _M0L6_2atmpS1102;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS280 = moonbit_clz64(_M0L5valueS279);
    _M0L6_2atmpS1103 = 63 - _M0L14leading__zerosS280;
    _M0L6_2atmpS1102 = _M0L6_2atmpS1103 / 4;
    return _M0L6_2atmpS1102 + 1;
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
    int32_t _M0L6_2atmpS1101 = -_M0L4selfS262;
    _M0L3numS264 = *(uint32_t*)&_M0L6_2atmpS1101;
  } else {
    _M0L3numS264 = *(uint32_t*)&_M0L4selfS262;
  }
  switch (_M0L5radixS261) {
    case 10: {
      int32_t _M0L10digit__lenS266;
      int32_t _M0L6_2atmpS1098;
      int32_t _M0L10total__lenS267;
      uint16_t* _M0L6bufferS268;
      int32_t _M0L12digit__startS269;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS266 = _M0FPB12dec__count32(_M0L3numS264);
      if (_M0L12is__negativeS263) {
        _M0L6_2atmpS1098 = 1;
      } else {
        _M0L6_2atmpS1098 = 0;
      }
      _M0L10total__lenS267 = _M0L10digit__lenS266 + _M0L6_2atmpS1098;
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
      int32_t _M0L6_2atmpS1099;
      int32_t _M0L10total__lenS271;
      uint16_t* _M0L6bufferS272;
      int32_t _M0L12digit__startS273;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS270 = _M0FPB12hex__count32(_M0L3numS264);
      if (_M0L12is__negativeS263) {
        _M0L6_2atmpS1099 = 1;
      } else {
        _M0L6_2atmpS1099 = 0;
      }
      _M0L10total__lenS271 = _M0L10digit__lenS270 + _M0L6_2atmpS1099;
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
      int32_t _M0L6_2atmpS1100;
      int32_t _M0L10total__lenS275;
      uint16_t* _M0L6bufferS276;
      int32_t _M0L12digit__startS277;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS274
      = _M0FPB14radix__count32(_M0L3numS264, _M0L5radixS261);
      if (_M0L12is__negativeS263) {
        _M0L6_2atmpS1100 = 1;
      } else {
        _M0L6_2atmpS1100 = 0;
      }
      _M0L10total__lenS275 = _M0L10digit__lenS274 + _M0L6_2atmpS1100;
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
      uint32_t _M0L6_2atmpS1096 = _M0L3numS258 / _M0L4baseS256;
      int32_t _M0L6_2atmpS1097 = _M0L5countS259 + 1;
      _M0L3numS258 = _M0L6_2atmpS1096;
      _M0L5countS259 = _M0L6_2atmpS1097;
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
    int32_t _M0L6_2atmpS1095;
    int32_t _M0L6_2atmpS1094;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS254 = moonbit_clz32(_M0L5valueS253);
    _M0L6_2atmpS1095 = 31 - _M0L14leading__zerosS254;
    _M0L6_2atmpS1094 = _M0L6_2atmpS1095 / 4;
    return _M0L6_2atmpS1094 + 1;
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
  int32_t _M0L6_2atmpS1093;
  uint32_t _M0L3numS228;
  int32_t _M0L6offsetS229;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1093 = _M0L10total__lenS251 - _M0L12digit__startS239;
  _M0L3numS228 = _M0L3numS250;
  _M0L6offsetS229 = _M0L6_2atmpS1093;
  while (1) {
    if (_M0L3numS228 >= 10000u) {
      uint32_t _M0L1tS230 = _M0L3numS228 / 10000u;
      uint32_t _M0L6_2atmpS1070 = _M0L3numS228 % 10000u;
      int32_t _M0L1rS231 = *(int32_t*)&_M0L6_2atmpS1070;
      int32_t _M0L2d1S232 = _M0L1rS231 / 100;
      int32_t _M0L2d2S233 = _M0L1rS231 % 100;
      int32_t _M0L6_2atmpS1069 = _M0L2d1S232 / 10;
      int32_t _M0L6_2atmpS1068 = 48 + _M0L6_2atmpS1069;
      int32_t _M0L6d1__hiS234 = (uint16_t)_M0L6_2atmpS1068;
      int32_t _M0L6_2atmpS1067 = _M0L2d1S232 % 10;
      int32_t _M0L6_2atmpS1066 = 48 + _M0L6_2atmpS1067;
      int32_t _M0L6d1__loS235 = (uint16_t)_M0L6_2atmpS1066;
      int32_t _M0L6_2atmpS1065 = _M0L2d2S233 / 10;
      int32_t _M0L6_2atmpS1064 = 48 + _M0L6_2atmpS1065;
      int32_t _M0L6d2__hiS236 = (uint16_t)_M0L6_2atmpS1064;
      int32_t _M0L6_2atmpS1063 = _M0L2d2S233 % 10;
      int32_t _M0L6_2atmpS1062 = 48 + _M0L6_2atmpS1063;
      int32_t _M0L6d2__loS237 = (uint16_t)_M0L6_2atmpS1062;
      int32_t _M0L6_2atmpS1054 = _M0L12digit__startS239 + _M0L6offsetS229;
      int32_t _M0L6_2atmpS1053 = _M0L6_2atmpS1054 - 4;
      int32_t _M0L6_2atmpS1056;
      int32_t _M0L6_2atmpS1055;
      int32_t _M0L6_2atmpS1058;
      int32_t _M0L6_2atmpS1057;
      int32_t _M0L6_2atmpS1060;
      int32_t _M0L6_2atmpS1059;
      int32_t _M0L6_2atmpS1061;
      _M0L6bufferS238[_M0L6_2atmpS1053] = _M0L6d1__hiS234;
      _M0L6_2atmpS1056 = _M0L12digit__startS239 + _M0L6offsetS229;
      _M0L6_2atmpS1055 = _M0L6_2atmpS1056 - 3;
      _M0L6bufferS238[_M0L6_2atmpS1055] = _M0L6d1__loS235;
      _M0L6_2atmpS1058 = _M0L12digit__startS239 + _M0L6offsetS229;
      _M0L6_2atmpS1057 = _M0L6_2atmpS1058 - 2;
      _M0L6bufferS238[_M0L6_2atmpS1057] = _M0L6d2__hiS236;
      _M0L6_2atmpS1060 = _M0L12digit__startS239 + _M0L6offsetS229;
      _M0L6_2atmpS1059 = _M0L6_2atmpS1060 - 1;
      _M0L6bufferS238[_M0L6_2atmpS1059] = _M0L6d2__loS237;
      _M0L6_2atmpS1061 = _M0L6offsetS229 - 4;
      _M0L3numS228 = _M0L1tS230;
      _M0L6offsetS229 = _M0L6_2atmpS1061;
      continue;
    } else {
      int32_t _M0L6_2atmpS1092 = *(int32_t*)&_M0L3numS228;
      int32_t _M0L9remainingS241 = _M0L6_2atmpS1092;
      int32_t _M0L6offsetS242 = _M0L6offsetS229;
      while (1) {
        if (_M0L9remainingS241 >= 100) {
          int32_t _M0L1tS243 = _M0L9remainingS241 / 100;
          int32_t _M0L1dS244 = _M0L9remainingS241 % 100;
          int32_t _M0L6_2atmpS1079 = _M0L1dS244 / 10;
          int32_t _M0L6_2atmpS1078 = 48 + _M0L6_2atmpS1079;
          int32_t _M0L5d__hiS245 = (uint16_t)_M0L6_2atmpS1078;
          int32_t _M0L6_2atmpS1077 = _M0L1dS244 % 10;
          int32_t _M0L6_2atmpS1076 = 48 + _M0L6_2atmpS1077;
          int32_t _M0L5d__loS246 = (uint16_t)_M0L6_2atmpS1076;
          int32_t _M0L6_2atmpS1072 = _M0L12digit__startS239 + _M0L6offsetS242;
          int32_t _M0L6_2atmpS1071 = _M0L6_2atmpS1072 - 2;
          int32_t _M0L6_2atmpS1074;
          int32_t _M0L6_2atmpS1073;
          int32_t _M0L6_2atmpS1075;
          _M0L6bufferS238[_M0L6_2atmpS1071] = _M0L5d__hiS245;
          _M0L6_2atmpS1074 = _M0L12digit__startS239 + _M0L6offsetS242;
          _M0L6_2atmpS1073 = _M0L6_2atmpS1074 - 1;
          _M0L6bufferS238[_M0L6_2atmpS1073] = _M0L5d__loS246;
          _M0L6_2atmpS1075 = _M0L6offsetS242 - 2;
          _M0L9remainingS241 = _M0L1tS243;
          _M0L6offsetS242 = _M0L6_2atmpS1075;
          continue;
        } else if (_M0L9remainingS241 >= 10) {
          int32_t _M0L6_2atmpS1087 = _M0L9remainingS241 / 10;
          int32_t _M0L6_2atmpS1086 = 48 + _M0L6_2atmpS1087;
          int32_t _M0L5d__hiS248 = (uint16_t)_M0L6_2atmpS1086;
          int32_t _M0L6_2atmpS1085 = _M0L9remainingS241 % 10;
          int32_t _M0L6_2atmpS1084 = 48 + _M0L6_2atmpS1085;
          int32_t _M0L5d__loS249 = (uint16_t)_M0L6_2atmpS1084;
          int32_t _M0L6_2atmpS1081 = _M0L12digit__startS239 + _M0L6offsetS242;
          int32_t _M0L6_2atmpS1080 = _M0L6_2atmpS1081 - 2;
          int32_t _M0L6_2atmpS1083;
          int32_t _M0L6_2atmpS1082;
          _M0L6bufferS238[_M0L6_2atmpS1080] = _M0L5d__hiS248;
          _M0L6_2atmpS1083 = _M0L12digit__startS239 + _M0L6offsetS242;
          _M0L6_2atmpS1082 = _M0L6_2atmpS1083 - 1;
          _M0L6bufferS238[_M0L6_2atmpS1082] = _M0L5d__loS249;
        } else {
          int32_t _M0L6_2atmpS1091 = _M0L12digit__startS239 + _M0L6offsetS242;
          int32_t _M0L6_2atmpS1088 = _M0L6_2atmpS1091 - 1;
          int32_t _M0L6_2atmpS1090 = 48 + _M0L9remainingS241;
          int32_t _M0L6_2atmpS1089 = (uint16_t)_M0L6_2atmpS1090;
          _M0L6bufferS238[_M0L6_2atmpS1088] = _M0L6_2atmpS1089;
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
  int32_t _M0L6_2atmpS1038;
  int32_t _M0L6_2atmpS1037;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS211 = *(uint32_t*)&_M0L5radixS212;
  _M0L6_2atmpS1038 = _M0L5radixS212 - 1;
  _M0L6_2atmpS1037 = _M0L5radixS212 & _M0L6_2atmpS1038;
  if (_M0L6_2atmpS1037 == 0) {
    int32_t _M0L5shiftS213;
    uint32_t _M0L4maskS214;
    int32_t _M0L6_2atmpS1045;
    int32_t _M0L6offsetS215;
    uint32_t _M0L1nS216;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS213 = moonbit_ctz32(_M0L5radixS212);
    _M0L4maskS214 = _M0L4baseS211 - 1u;
    _M0L6_2atmpS1045 = _M0L10total__lenS221 - _M0L12digit__startS219;
    _M0L6offsetS215 = _M0L6_2atmpS1045;
    _M0L1nS216 = _M0L3numS222;
    while (1) {
      if (_M0L1nS216 > 0u) {
        uint32_t _M0L6_2atmpS1044 = _M0L1nS216 & _M0L4maskS214;
        int32_t _M0L5digitS217 = *(int32_t*)&_M0L6_2atmpS1044;
        int32_t _M0L6_2atmpS1041 = _M0L12digit__startS219 + _M0L6offsetS215;
        int32_t _M0L6_2atmpS1039 = _M0L6_2atmpS1041 - 1;
        int32_t _M0L6_2atmpS1040 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS217];
        int32_t _M0L6_2atmpS1042;
        uint32_t _M0L6_2atmpS1043;
        _M0L6bufferS218[_M0L6_2atmpS1039] = _M0L6_2atmpS1040;
        _M0L6_2atmpS1042 = _M0L6offsetS215 - 1;
        _M0L6_2atmpS1043 = _M0L1nS216 >> (_M0L5shiftS213 & 31);
        _M0L6offsetS215 = _M0L6_2atmpS1042;
        _M0L1nS216 = _M0L6_2atmpS1043;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1052 = _M0L10total__lenS221 - _M0L12digit__startS219;
    int32_t _M0L6offsetS223 = _M0L6_2atmpS1052;
    uint32_t _M0L1nS224 = _M0L3numS222;
    while (1) {
      if (_M0L1nS224 > 0u) {
        uint32_t _M0L1qS225 = _M0L1nS224 / _M0L4baseS211;
        uint32_t _M0L6_2atmpS1051 = _M0L1qS225 * _M0L4baseS211;
        uint32_t _M0L6_2atmpS1050 = _M0L1nS224 - _M0L6_2atmpS1051;
        int32_t _M0L5digitS226 = *(int32_t*)&_M0L6_2atmpS1050;
        int32_t _M0L6_2atmpS1048 = _M0L12digit__startS219 + _M0L6offsetS223;
        int32_t _M0L6_2atmpS1046 = _M0L6_2atmpS1048 - 1;
        int32_t _M0L6_2atmpS1047 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS226];
        int32_t _M0L6_2atmpS1049;
        _M0L6bufferS218[_M0L6_2atmpS1046] = _M0L6_2atmpS1047;
        _M0L6_2atmpS1049 = _M0L6offsetS223 - 1;
        _M0L6offsetS223 = _M0L6_2atmpS1049;
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
  int32_t _M0L6_2atmpS1036;
  int32_t _M0L6offsetS200;
  uint32_t _M0L1nS201;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1036 = _M0L10total__lenS209 - _M0L12digit__startS206;
  _M0L6offsetS200 = _M0L6_2atmpS1036;
  _M0L1nS201 = _M0L3numS210;
  while (1) {
    if (_M0L6offsetS200 >= 2) {
      uint32_t _M0L6_2atmpS1033 = _M0L1nS201 & 255u;
      int32_t _M0L9byte__valS202 = *(int32_t*)&_M0L6_2atmpS1033;
      int32_t _M0L2hiS203 = _M0L9byte__valS202 / 16;
      int32_t _M0L2loS204 = _M0L9byte__valS202 % 16;
      int32_t _M0L6_2atmpS1027 = _M0L12digit__startS206 + _M0L6offsetS200;
      int32_t _M0L6_2atmpS1025 = _M0L6_2atmpS1027 - 2;
      int32_t _M0L6_2atmpS1026 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L2hiS203];
      int32_t _M0L6_2atmpS1030;
      int32_t _M0L6_2atmpS1028;
      int32_t _M0L6_2atmpS1029;
      int32_t _M0L6_2atmpS1031;
      uint32_t _M0L6_2atmpS1032;
      _M0L6bufferS205[_M0L6_2atmpS1025] = _M0L6_2atmpS1026;
      _M0L6_2atmpS1030 = _M0L12digit__startS206 + _M0L6offsetS200;
      _M0L6_2atmpS1028 = _M0L6_2atmpS1030 - 1;
      _M0L6_2atmpS1029
      = ((moonbit_string_t)moonbit_string_literal_17.data)[
        _M0L2loS204
      ];
      _M0L6bufferS205[_M0L6_2atmpS1028] = _M0L6_2atmpS1029;
      _M0L6_2atmpS1031 = _M0L6offsetS200 - 2;
      _M0L6_2atmpS1032 = _M0L1nS201 >> 8;
      _M0L6offsetS200 = _M0L6_2atmpS1031;
      _M0L1nS201 = _M0L6_2atmpS1032;
      continue;
    } else if (_M0L6offsetS200 == 1) {
      uint32_t _M0L6_2atmpS1035 = _M0L1nS201 & 15u;
      int32_t _M0L6nibbleS208 = *(int32_t*)&_M0L6_2atmpS1035;
      int32_t _M0L6_2atmpS1034 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L6nibbleS208];
      _M0L6bufferS205[_M0L12digit__startS206] = _M0L6_2atmpS1034;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS199
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS198;
  struct _M0TPB6Logger _M0L6_2atmpS1024;
  moonbit_string_t _result_1724;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS198);
  _M0L6_2atmpS1024
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS198
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS199, _M0L6_2atmpS1024);
  if (_M0L6_2atmpS1024.$1) {
    moonbit_decref(_M0L6_2atmpS1024.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_1724 = _M0MPB13StringBuilder10to__string(_M0L6loggerS198);
  moonbit_decref_cycle_free(_M0L6loggerS198);
  return _result_1724;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS193,
  struct _M0TPB6Logger _M0L6loggerS192
) {
  moonbit_string_t _M0L6_2atmpS1021;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1021 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS193);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS192.$0->$method_0(_M0L6loggerS192.$1, _M0L6_2atmpS1021);
  moonbit_decref_cycle_free(_M0L6_2atmpS1021);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS195,
  struct _M0TPB6Logger _M0L6loggerS194
) {
  moonbit_string_t _M0L6_2atmpS1022;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1022 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS195);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS194.$0->$method_0(_M0L6loggerS194.$1, _M0L6_2atmpS1022);
  moonbit_decref_cycle_free(_M0L6_2atmpS1022);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS197,
  struct _M0TPB6Logger _M0L6loggerS196
) {
  moonbit_string_t _M0L6_2atmpS1023;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1023 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS197);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS196.$0->$method_0(_M0L6loggerS196.$1, _M0L6_2atmpS1023);
  moonbit_decref_cycle_free(_M0L6_2atmpS1023);
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
  moonbit_string_t _M0L8_2afieldS1642;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS1642 = _M0L4selfS190.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1642);
  return _M0L8_2afieldS1642;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS186,
  moonbit_string_t _M0L5valueS187,
  int32_t _M0L5startS188,
  int32_t _M0L3lenS189
) {
  int32_t _M0L6_2atmpS1020;
  int64_t _M0L6_2atmpS1019;
  struct _M0TPC16string10StringView _M0L6_2atmpS1018;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1020 = _M0L5startS188 + _M0L3lenS189;
  _M0L6_2atmpS1019 = (int64_t)_M0L6_2atmpS1020;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1018
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS187, _M0L5startS188, _M0L6_2atmpS1019);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS186, _M0L6_2atmpS1018);
  moonbit_decref_cycle_free(_M0L6_2atmpS1018.$0);
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
  int32_t _M0L6_2atmpS1002;
  int32_t _if__result_1725;
  int32_t _M0L6_2atmpS1010;
  int32_t _if__result_1726;
  int32_t _M0L6_2atmpS1012;
  int32_t _M0L6_2atmpS1013;
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
  _M0L6_2atmpS1002 = _M0Lm2loS180;
  if (_M0L6_2atmpS1002 > 0) {
    int32_t _M0L6_2atmpS1001 = _M0Lm2loS180;
    if (_M0L6_2atmpS1001 < _M0L3lenS178) {
      int32_t _M0L6_2atmpS1000 = _M0Lm2loS180;
      int32_t _M0L6_2atmpS999 = _M0L4selfS179[_M0L6_2atmpS1000];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS999)) {
        int32_t _M0L6_2atmpS998 = _M0Lm2loS180;
        int32_t _M0L6_2atmpS997 = _M0L6_2atmpS998 - 1;
        int32_t _M0L6_2atmpS996 = _M0L4selfS179[_M0L6_2atmpS997];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_1725
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS996);
      } else {
        _if__result_1725 = 0;
      }
    } else {
      _if__result_1725 = 0;
    }
  } else {
    _if__result_1725 = 0;
  }
  if (_if__result_1725) {
    int32_t _M0L6_2atmpS1003 = _M0Lm2loS180;
    _M0Lm2loS180 = _M0L6_2atmpS1003 + 1;
  }
  _M0L6_2atmpS1010 = _M0Lm2hiS182;
  if (_M0L6_2atmpS1010 > 0) {
    int32_t _M0L6_2atmpS1009 = _M0Lm2hiS182;
    if (_M0L6_2atmpS1009 < _M0L3lenS178) {
      int32_t _M0L6_2atmpS1008 = _M0Lm2hiS182;
      int32_t _M0L6_2atmpS1007 = _M0L4selfS179[_M0L6_2atmpS1008];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1007)) {
        int32_t _M0L6_2atmpS1006 = _M0Lm2hiS182;
        int32_t _M0L6_2atmpS1005 = _M0L6_2atmpS1006 - 1;
        int32_t _M0L6_2atmpS1004 = _M0L4selfS179[_M0L6_2atmpS1005];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_1726
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1004);
      } else {
        _if__result_1726 = 0;
      }
    } else {
      _if__result_1726 = 0;
    }
  } else {
    _if__result_1726 = 0;
  }
  if (_if__result_1726) {
    int32_t _M0L6_2atmpS1011 = _M0Lm2hiS182;
    _M0Lm2hiS182 = _M0L6_2atmpS1011 - 1;
  }
  _M0L6_2atmpS1012 = _M0Lm2loS180;
  _M0L6_2atmpS1013 = _M0Lm2hiS182;
  if (_M0L6_2atmpS1012 >= _M0L6_2atmpS1013) {
    int32_t _M0L6_2atmpS1014 = _M0Lm2loS180;
    int32_t _M0L6_2atmpS1015 = _M0Lm2loS180;
    moonbit_incref_cycle_free(_M0L4selfS179);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS179,
                                                 .$1 = _M0L6_2atmpS1014,
                                                 .$2 = _M0L6_2atmpS1015};
  } else {
    int32_t _M0L6_2atmpS1016 = _M0Lm2loS180;
    int32_t _M0L6_2atmpS1017 = _M0Lm2hiS182;
    moonbit_incref_cycle_free(_M0L4selfS179);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS179,
                                                 .$1 = _M0L6_2atmpS1016,
                                                 .$2 = _M0L6_2atmpS1017};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS177,
  struct _M0TPB4Show _M0L4showS176
) {
  struct _M0TPB6Logger _M0L6_2atmpS995;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS177);
  _M0L6_2atmpS995
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS177
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS176.$0->$method_0(_M0L4showS176.$1, _M0L6_2atmpS995);
  if (_M0L6_2atmpS995.$1) {
    moonbit_decref(_M0L6_2atmpS995.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS175,
  struct _M0TPB4Show _M0L4showS174
) {
  struct _M0TPB6Logger _M0L6_2atmpS994;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS175);
  _M0L6_2atmpS994
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS175
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS174.$0->$method_0(_M0L4showS174.$1, _M0L6_2atmpS994);
  if (_M0L6_2atmpS994.$1) {
    moonbit_decref(_M0L6_2atmpS994.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS173) {
  int64_t _M0L6_2atmpS993;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS993 = (int64_t)_M0L4selfS173;
  return *(uint64_t*)&_M0L6_2atmpS993;
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
  int32_t _M0L6_2atmpS992;
  struct _M0TPC16string10StringView _M0L6_2atmpS990;
  struct _M0TPB6Logger _M0L6_2atmpS991;
  moonbit_string_t _result_1727;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS170 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS992 = Moonbit_array_length(_M0L4selfS171);
  moonbit_incref_cycle_free(_M0L4selfS171);
  _M0L6_2atmpS990
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS171, .$1 = 0, .$2 = _M0L6_2atmpS992
  };
  moonbit_incref_cycle_free(_M0L3bufS170);
  _M0L6_2atmpS991
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS170
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS990, _M0L6_2atmpS991, _M0L5quoteS172);
  moonbit_decref_cycle_free(_M0L6_2atmpS990.$0);
  if (_M0L6_2atmpS991.$1) {
    moonbit_decref(_M0L6_2atmpS991.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_1727 = _M0MPB13StringBuilder10to__string(_M0L3bufS170);
  moonbit_decref_cycle_free(_M0L3bufS170);
  return _result_1727;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS162,
  struct _M0TPB6Logger _M0L6loggerS160,
  int32_t _M0L5quoteS159
) {
  int32_t _M0L3endS988;
  int32_t _M0L5startS989;
  int32_t _M0L3lenS161;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS163;
  int32_t _M0L1iS164;
  int32_t _M0L3segS165;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS159) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS160.$0->$method_3(_M0L6loggerS160.$1, 34);
  }
  _M0L3endS988 = _M0L4selfS162.$2;
  _M0L5startS989 = _M0L4selfS162.$1;
  _M0L3lenS161 = _M0L3endS988 - _M0L5startS989;
  moonbit_incref_cycle_free(_M0L4selfS162.$0);
  if (_M0L6loggerS160.$1) {
    moonbit_incref(_M0L6loggerS160.$1);
  }
  _M0L6_2aenvS163
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS163)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 25, 0);
  _M0L6_2aenvS163->$0 = _M0L4selfS162;
  _M0L6_2aenvS163->$1 = _M0L6loggerS160;
  _M0L1iS164 = 0;
  _M0L3segS165 = 0;
  _2afor_166:;
  while (1) {
    moonbit_string_t _M0L3strS985;
    int32_t _M0L5startS987;
    int32_t _M0L6_2atmpS986;
    int32_t _M0L4codeS167;
    int32_t _M0L1cS169;
    int32_t _M0L6_2atmpS969;
    int32_t _M0L6_2atmpS970;
    int32_t _M0L6_2atmpS971;
    if (_M0L1iS164 >= _M0L3lenS161) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
      moonbit_decref_cycle_free(_M0L6_2aenvS163);
      break;
    }
    _M0L3strS985 = _M0L4selfS162.$0;
    _M0L5startS987 = _M0L4selfS162.$1;
    _M0L6_2atmpS986 = _M0L5startS987 + _M0L1iS164;
    _M0L4codeS167 = _M0L3strS985[_M0L6_2atmpS986];
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
        int32_t _M0L6_2atmpS972;
        int32_t _M0L6_2atmpS973;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_18.data);
        _M0L6_2atmpS972 = _M0L1iS164 + 1;
        _M0L6_2atmpS973 = _M0L1iS164 + 1;
        _M0L1iS164 = _M0L6_2atmpS972;
        _M0L3segS165 = _M0L6_2atmpS973;
        goto _2afor_166;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS974;
        int32_t _M0L6_2atmpS975;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_19.data);
        _M0L6_2atmpS974 = _M0L1iS164 + 1;
        _M0L6_2atmpS975 = _M0L1iS164 + 1;
        _M0L1iS164 = _M0L6_2atmpS974;
        _M0L3segS165 = _M0L6_2atmpS975;
        goto _2afor_166;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS976;
        int32_t _M0L6_2atmpS977;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_20.data);
        _M0L6_2atmpS976 = _M0L1iS164 + 1;
        _M0L6_2atmpS977 = _M0L1iS164 + 1;
        _M0L1iS164 = _M0L6_2atmpS976;
        _M0L3segS165 = _M0L6_2atmpS977;
        goto _2afor_166;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS978;
        int32_t _M0L6_2atmpS979;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS978 = _M0L1iS164 + 1;
        _M0L6_2atmpS979 = _M0L1iS164 + 1;
        _M0L1iS164 = _M0L6_2atmpS978;
        _M0L3segS165 = _M0L6_2atmpS979;
        goto _2afor_166;
        break;
      }
      default: {
        if (_M0L4codeS167 < 32) {
          int32_t _M0L6_2atmpS981;
          moonbit_string_t _M0L6_2atmpS980;
          int32_t _M0L6_2atmpS982;
          int32_t _M0L6_2atmpS983;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_22.data);
          _M0L6_2atmpS981 = _M0L4codeS167 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS980 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS981);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, _M0L6_2atmpS980);
          moonbit_decref_cycle_free(_M0L6_2atmpS980);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS982 = _M0L1iS164 + 1;
          _M0L6_2atmpS983 = _M0L1iS164 + 1;
          _M0L1iS164 = _M0L6_2atmpS982;
          _M0L3segS165 = _M0L6_2atmpS983;
          goto _2afor_166;
        } else {
          int32_t _M0L6_2atmpS984 = _M0L1iS164 + 1;
          int32_t _tmp_1730 = _M0L3segS165;
          _M0L1iS164 = _M0L6_2atmpS984;
          _M0L3segS165 = _tmp_1730;
          goto _2afor_166;
        }
        break;
      }
    }
    goto joinlet_1729;
    join_168:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS160.$0->$method_3(_M0L6loggerS160.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS969 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS169);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS160.$0->$method_3(_M0L6loggerS160.$1, _M0L6_2atmpS969);
    _M0L6_2atmpS970 = _M0L1iS164 + 1;
    _M0L6_2atmpS971 = _M0L1iS164 + 1;
    _M0L1iS164 = _M0L6_2atmpS970;
    _M0L3segS165 = _M0L6_2atmpS971;
    continue;
    joinlet_1729:;
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
    int64_t _M0L6_2atmpS968 = (int64_t)_M0L1iS157;
    struct _M0TPC16string10StringView _M0L6_2atmpS967;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS967
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS156, _M0L3segS158, _M0L6_2atmpS968);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS154.$0->$method_2(_M0L6loggerS154.$1, _M0L6_2atmpS967);
    moonbit_decref_cycle_free(_M0L6_2atmpS967.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS145,
  int32_t _M0L5startS147,
  int64_t _M0L3endS149
) {
  int32_t _M0L3endS965;
  int32_t _M0L5startS966;
  int32_t _M0L3lenS144;
  int32_t _M0Lm2loS146;
  int32_t _M0Lm2hiS148;
  moonbit_string_t _M0L3strS152;
  int32_t _M0L4baseS153;
  int32_t _M0L6_2atmpS943;
  int32_t _if__result_1731;
  int32_t _M0L6_2atmpS953;
  int32_t _if__result_1732;
  int32_t _M0L6_2atmpS955;
  int32_t _M0L6_2atmpS956;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS965 = _M0L4selfS145.$2;
  _M0L5startS966 = _M0L4selfS145.$1;
  _M0L3lenS144 = _M0L3endS965 - _M0L5startS966;
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
  _M0L6_2atmpS943 = _M0Lm2loS146;
  if (_M0L6_2atmpS943 > 0) {
    int32_t _M0L6_2atmpS942 = _M0Lm2loS146;
    if (_M0L6_2atmpS942 < _M0L3lenS144) {
      int32_t _M0L6_2atmpS941 = _M0Lm2loS146;
      int32_t _M0L6_2atmpS940 = _M0L4baseS153 + _M0L6_2atmpS941;
      int32_t _M0L6_2atmpS939 = _M0L3strS152[_M0L6_2atmpS940];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS939)) {
        int32_t _M0L6_2atmpS938 = _M0Lm2loS146;
        int32_t _M0L6_2atmpS937 = _M0L4baseS153 + _M0L6_2atmpS938;
        int32_t _M0L6_2atmpS936 = _M0L6_2atmpS937 - 1;
        int32_t _M0L6_2atmpS935 = _M0L3strS152[_M0L6_2atmpS936];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_1731
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS935);
      } else {
        _if__result_1731 = 0;
      }
    } else {
      _if__result_1731 = 0;
    }
  } else {
    _if__result_1731 = 0;
  }
  if (_if__result_1731) {
    int32_t _M0L6_2atmpS944 = _M0Lm2loS146;
    _M0Lm2loS146 = _M0L6_2atmpS944 + 1;
  }
  _M0L6_2atmpS953 = _M0Lm2hiS148;
  if (_M0L6_2atmpS953 > 0) {
    int32_t _M0L6_2atmpS952 = _M0Lm2hiS148;
    if (_M0L6_2atmpS952 < _M0L3lenS144) {
      int32_t _M0L6_2atmpS951 = _M0Lm2hiS148;
      int32_t _M0L6_2atmpS950 = _M0L4baseS153 + _M0L6_2atmpS951;
      int32_t _M0L6_2atmpS949 = _M0L3strS152[_M0L6_2atmpS950];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS949)) {
        int32_t _M0L6_2atmpS948 = _M0Lm2hiS148;
        int32_t _M0L6_2atmpS947 = _M0L4baseS153 + _M0L6_2atmpS948;
        int32_t _M0L6_2atmpS946 = _M0L6_2atmpS947 - 1;
        int32_t _M0L6_2atmpS945 = _M0L3strS152[_M0L6_2atmpS946];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_1732
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS945);
      } else {
        _if__result_1732 = 0;
      }
    } else {
      _if__result_1732 = 0;
    }
  } else {
    _if__result_1732 = 0;
  }
  if (_if__result_1732) {
    int32_t _M0L6_2atmpS954 = _M0Lm2hiS148;
    _M0Lm2hiS148 = _M0L6_2atmpS954 - 1;
  }
  _M0L6_2atmpS955 = _M0Lm2loS146;
  _M0L6_2atmpS956 = _M0Lm2hiS148;
  if (_M0L6_2atmpS955 >= _M0L6_2atmpS956) {
    int32_t _M0L6_2atmpS960 = _M0Lm2loS146;
    int32_t _M0L6_2atmpS957 = _M0L4baseS153 + _M0L6_2atmpS960;
    int32_t _M0L6_2atmpS959 = _M0Lm2loS146;
    int32_t _M0L6_2atmpS958 = _M0L4baseS153 + _M0L6_2atmpS959;
    moonbit_incref_cycle_free(_M0L3strS152);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS152,
                                                 .$1 = _M0L6_2atmpS957,
                                                 .$2 = _M0L6_2atmpS958};
  } else {
    int32_t _M0L6_2atmpS964 = _M0Lm2loS146;
    int32_t _M0L6_2atmpS961 = _M0L4baseS153 + _M0L6_2atmpS964;
    int32_t _M0L6_2atmpS963 = _M0Lm2hiS148;
    int32_t _M0L6_2atmpS962 = _M0L4baseS153 + _M0L6_2atmpS963;
    moonbit_incref_cycle_free(_M0L3strS152);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS152,
                                                 .$1 = _M0L6_2atmpS961,
                                                 .$2 = _M0L6_2atmpS962};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS143) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS142;
  int32_t _M0L6_2atmpS932;
  int32_t _M0L6_2atmpS931;
  int32_t _M0L6_2atmpS934;
  int32_t _M0L6_2atmpS933;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS930;
  moonbit_string_t _result_1733;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS142 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS932 = _M0IPC14byte4BytePB3Div3div(_M0L1bS143, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS931
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS932);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS142, _M0L6_2atmpS931);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS934 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS143, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS933
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS934);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS142, _M0L6_2atmpS933);
  _M0L6_2atmpS930 = _M0L7_2aselfS142;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_1733 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS930);
  moonbit_decref_cycle_free(_M0L6_2atmpS930);
  return _result_1733;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS141) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS141 < 10) {
    int32_t _M0L6_2atmpS927;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS927 = _M0IPC14byte4BytePB3Add3add(_M0L1iS141, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS927);
  } else {
    int32_t _M0L6_2atmpS929;
    int32_t _M0L6_2atmpS928;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS929 = _M0IPC14byte4BytePB3Add3add(_M0L1iS141, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS928 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS929, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS928);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS139,
  int32_t _M0L4thatS140
) {
  int32_t _M0L6_2atmpS925;
  int32_t _M0L6_2atmpS926;
  int32_t _M0L6_2atmpS924;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS925 = (int32_t)_M0L4selfS139;
  _M0L6_2atmpS926 = (int32_t)_M0L4thatS140;
  _M0L6_2atmpS924 = _M0L6_2atmpS925 - _M0L6_2atmpS926;
  return _M0L6_2atmpS924 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS137,
  int32_t _M0L4thatS138
) {
  int32_t _M0L6_2atmpS922;
  int32_t _M0L6_2atmpS923;
  int32_t _M0L6_2atmpS921;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS922 = (int32_t)_M0L4selfS137;
  _M0L6_2atmpS923 = (int32_t)_M0L4thatS138;
  _M0L6_2atmpS921 = _M0L6_2atmpS922 % _M0L6_2atmpS923;
  return _M0L6_2atmpS921 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS135,
  int32_t _M0L4thatS136
) {
  int32_t _M0L6_2atmpS919;
  int32_t _M0L6_2atmpS920;
  int32_t _M0L6_2atmpS918;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS919 = (int32_t)_M0L4selfS135;
  _M0L6_2atmpS920 = (int32_t)_M0L4thatS136;
  _M0L6_2atmpS918 = _M0L6_2atmpS919 / _M0L6_2atmpS920;
  return _M0L6_2atmpS918 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS133,
  int32_t _M0L4thatS134
) {
  int32_t _M0L6_2atmpS916;
  int32_t _M0L6_2atmpS917;
  int32_t _M0L6_2atmpS915;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS916 = (int32_t)_M0L4selfS133;
  _M0L6_2atmpS917 = (int32_t)_M0L4thatS134;
  _M0L6_2atmpS915 = _M0L6_2atmpS916 + _M0L6_2atmpS917;
  return _M0L6_2atmpS915 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS132) {
  int32_t _M0L6_2atmpS914;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS914 = (int32_t)_M0L4selfS132;
  return _M0L6_2atmpS914;
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
  int32_t _M0L3lenS913;
  int32_t _M0L8requiredS128;
  uint16_t* _M0L4dataS908;
  int32_t _M0L6_2atmpS907;
  int32_t _if__result_1734;
  uint16_t* _M0L4dataS909;
  int32_t _M0L3lenS910;
  int32_t _M0L3lenS912;
  int32_t _M0L6_2atmpS911;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS126 = Moonbit_array_length(_M0L3strS127);
  if (_M0L8str__lenS126 == 0) {
    return 0;
  }
  _M0L3lenS913 = _M0L4selfS129->$1;
  _M0L8requiredS128 = _M0L3lenS913 + _M0L8str__lenS126;
  _M0L4dataS908 = _M0L4selfS129->$0;
  _M0L6_2atmpS907 = Moonbit_array_length(_M0L4dataS908);
  if (_M0L8requiredS128 > _M0L6_2atmpS907) {
    _if__result_1734 = 1;
  } else {
    int32_t _M0L3lenS906 = _M0L4selfS129->$1;
    _if__result_1734 = _M0L8requiredS128 < _M0L3lenS906;
  }
  if (_if__result_1734) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS129, _M0L8requiredS128);
  }
  _M0L4dataS909 = _M0L4selfS129->$0;
  _M0L3lenS910 = _M0L4selfS129->$1;
  moonbit_incref_cycle_free(_M0L4dataS909);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS909, _M0L3lenS910, _M0L3strS127, 0, _M0L8str__lenS126);
  moonbit_decref_cycle_free(_M0L4dataS909);
  _M0L3lenS912 = _M0L4selfS129->$1;
  _M0L6_2atmpS911 = _M0L3lenS912 + _M0L8str__lenS126;
  _M0L4selfS129->$1 = _M0L6_2atmpS911;
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
      int32_t _M0L6_2atmpS903 = _M0L3strS123[_M0L1iS120];
      int32_t _M0L6_2atmpS904;
      int32_t _M0L6_2atmpS905;
      _M0L4selfS122[_M0L1jS121] = _M0L6_2atmpS903;
      _M0L6_2atmpS904 = _M0L1iS120 + 1;
      _M0L6_2atmpS905 = _M0L1jS121 + 1;
      _M0L1iS120 = _M0L6_2atmpS904;
      _M0L1jS121 = _M0L6_2atmpS905;
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
    int32_t _M0L3lenS874 = _M0L4selfS115->$1;
    uint16_t* _M0L4dataS876 = _M0L4selfS115->$0;
    int32_t _M0L6_2atmpS875 = Moonbit_array_length(_M0L4dataS876);
    uint16_t* _M0L4dataS879;
    int32_t _M0L3lenS880;
    int32_t _M0L6_2atmpS881;
    int32_t _M0L3lenS883;
    int32_t _M0L6_2atmpS882;
    if (_M0L3lenS874 >= _M0L6_2atmpS875) {
      int32_t _M0L3lenS878 = _M0L4selfS115->$1;
      int32_t _M0L6_2atmpS877 = _M0L3lenS878 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS115, _M0L6_2atmpS877);
    }
    _M0L4dataS879 = _M0L4selfS115->$0;
    _M0L3lenS880 = _M0L4selfS115->$1;
    moonbit_incref_cycle_free(_M0L4dataS879);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS881 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS113);
    if (
      _M0L3lenS880 < 0 || _M0L3lenS880 >= Moonbit_array_length(_M0L4dataS879)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS879[_M0L3lenS880] = _M0L6_2atmpS881;
    moonbit_decref_cycle_free(_M0L4dataS879);
    _M0L3lenS883 = _M0L4selfS115->$1;
    _M0L6_2atmpS882 = _M0L3lenS883 + 1;
    _M0L4selfS115->$1 = _M0L6_2atmpS882;
  } else if (_M0L4codeS113 <= 1114111u) {
    uint16_t* _M0L4dataS887 = _M0L4selfS115->$0;
    int32_t _M0L6_2atmpS885 = Moonbit_array_length(_M0L4dataS887);
    int32_t _M0L3lenS886 = _M0L4selfS115->$1;
    int32_t _M0L6_2atmpS884 = _M0L6_2atmpS885 - _M0L3lenS886;
    uint32_t _M0L4codeS116;
    uint16_t* _M0L4dataS890;
    int32_t _M0L3lenS891;
    uint32_t _M0L6_2atmpS894;
    uint32_t _M0L6_2atmpS893;
    int32_t _M0L6_2atmpS892;
    uint16_t* _M0L4dataS895;
    int32_t _M0L3lenS900;
    int32_t _M0L6_2atmpS896;
    uint32_t _M0L6_2atmpS899;
    uint32_t _M0L6_2atmpS898;
    int32_t _M0L6_2atmpS897;
    int32_t _M0L3lenS902;
    int32_t _M0L6_2atmpS901;
    if (_M0L6_2atmpS884 < 2) {
      int32_t _M0L3lenS889 = _M0L4selfS115->$1;
      int32_t _M0L6_2atmpS888 = _M0L3lenS889 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS115, _M0L6_2atmpS888);
    }
    _M0L4codeS116 = _M0L4codeS113 - 65536u;
    _M0L4dataS890 = _M0L4selfS115->$0;
    _M0L3lenS891 = _M0L4selfS115->$1;
    _M0L6_2atmpS894 = _M0L4codeS116 >> 10;
    _M0L6_2atmpS893 = 55296u + _M0L6_2atmpS894;
    moonbit_incref_cycle_free(_M0L4dataS890);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS892 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS893);
    if (
      _M0L3lenS891 < 0 || _M0L3lenS891 >= Moonbit_array_length(_M0L4dataS890)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS890[_M0L3lenS891] = _M0L6_2atmpS892;
    moonbit_decref_cycle_free(_M0L4dataS890);
    _M0L4dataS895 = _M0L4selfS115->$0;
    _M0L3lenS900 = _M0L4selfS115->$1;
    _M0L6_2atmpS896 = _M0L3lenS900 + 1;
    _M0L6_2atmpS899 = _M0L4codeS116 & 1023u;
    _M0L6_2atmpS898 = 56320u + _M0L6_2atmpS899;
    moonbit_incref_cycle_free(_M0L4dataS895);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS897 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS898);
    if (
      _M0L6_2atmpS896 < 0
      || _M0L6_2atmpS896 >= Moonbit_array_length(_M0L4dataS895)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS895[_M0L6_2atmpS896] = _M0L6_2atmpS897;
    moonbit_decref_cycle_free(_M0L4dataS895);
    _M0L3lenS902 = _M0L4selfS115->$1;
    _M0L6_2atmpS901 = _M0L3lenS902 + 2;
    _M0L4selfS115->$1 = _M0L6_2atmpS901;
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
  uint16_t* _M0L4dataS873;
  int32_t _M0L6_2atmpS871;
  int32_t _M0L3lenS872;
  int32_t _M0L13new__capacityS109;
  uint16_t* _M0L4dataS868;
  int32_t _M0L6_2atmpS869;
  int32_t _M0L3lenS870;
  uint16_t* _M0L9new__dataS112;
  uint16_t* _M0L6_2aoldS1643;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS873 = _M0L4selfS110->$0;
  _M0L6_2atmpS871 = Moonbit_array_length(_M0L4dataS873);
  _M0L3lenS872 = _M0L4selfS110->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS109
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS871, _M0L3lenS872, _M0L8requiredS111);
  _M0L4dataS868 = _M0L4selfS110->$0;
  moonbit_incref_cycle_free(_M0L4dataS868);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS869 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS870 = _M0L4selfS110->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS112
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS868, _M0L13new__capacityS109, _M0L6_2atmpS869, _M0L3lenS870, 0, 0);
  _M0L6_2aoldS1643 = _M0L4selfS110->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1643);
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
  int32_t _M0L6_2atmpS867;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS867 = *(int32_t*)&_M0L4selfS102;
  return (uint16_t)_M0L6_2atmpS867;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS101) {
  int32_t _M0L6_2atmpS866;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS866 = _M0L4selfS101;
  return *(uint32_t*)&_M0L6_2atmpS866;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS99
) {
  int32_t _M0L3lenS857;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS857 = _M0L4selfS99->$1;
  if (_M0L3lenS857 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS858 = _M0L4selfS99->$1;
    uint16_t* _M0L4dataS860 = _M0L4selfS99->$0;
    int32_t _M0L6_2atmpS859 = Moonbit_array_length(_M0L4dataS860);
    if (_M0L3lenS858 == _M0L6_2atmpS859) {
      uint16_t* _M0L4dataS861 = _M0L4selfS99->$0;
      moonbit_incref_cycle_free(_M0L4dataS861);
      return _M0L4dataS861;
    } else {
      uint16_t* _M0L4dataS862 = _M0L4selfS99->$0;
      int32_t _M0L3lenS863 = _M0L4selfS99->$1;
      int32_t _M0L6_2atmpS864;
      int32_t _M0L3lenS865;
      uint16_t* _M0L4dataS100;
      moonbit_incref_cycle_free(_M0L4dataS862);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS864 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS865 = _M0L4selfS99->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS100
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS862, _M0L3lenS863, _M0L6_2atmpS864, _M0L3lenS865, 0, 0);
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
  int32_t _if__result_1737;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS92 >= 0) {
    if (_M0L3lenS93 >= 0) {
      if (_M0L11src__offsetS94 >= 0) {
        if (_M0L11dst__offsetS95 >= 0) {
          int32_t _M0L6_2atmpS853 = _M0L11src__offsetS94 + _M0L3lenS93;
          int32_t _M0L6_2atmpS854 = Moonbit_array_length(_M0L3srcS96);
          if (_M0L6_2atmpS853 <= _M0L6_2atmpS854) {
            int32_t _M0L6_2atmpS852 = _M0L11dst__offsetS95 + _M0L3lenS93;
            _if__result_1737 = _M0L6_2atmpS852 <= _M0L13allocate__lenS92;
          } else {
            _if__result_1737 = 0;
          }
        } else {
          _if__result_1737 = 0;
        }
      } else {
        _if__result_1737 = 0;
      }
    } else {
      _if__result_1737 = 0;
    }
  } else {
    _if__result_1737 = 0;
  }
  if (_if__result_1737) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS96, _M0L13allocate__lenS92, _M0L4initS97, _M0L11src__offsetS94, _M0L11dst__offsetS95, _M0L3lenS93);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS98;
    int32_t _M0L6_2atmpS856;
    moonbit_string_t _M0L6_2atmpS855;
    uint16_t* _result_1738;
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
    _M0L6_2atmpS856 = Moonbit_array_length(_M0L3srcS96);
    moonbit_decref_cycle_free(_M0L3srcS96);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS98, _M0L6_2atmpS856);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS855
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS98);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS98);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_1738 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS855);
    moonbit_decref_cycle_free(_M0L6_2atmpS855);
    return _result_1738;
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
  struct _M0TPB13StringBuilder* _block_1739;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS83 < 1) {
    _M0L7initialS82 = 1;
  } else {
    int32_t _M0L6_2atmpS851 = _M0L10size__hintS83 + 1;
    _M0L7initialS82 = _M0L6_2atmpS851 / 2;
  }
  _M0L4dataS84 = (uint16_t*)moonbit_make_string(_M0L7initialS82, 0);
  _block_1739
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_1739)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 30, 0);
  _block_1739->$0 = _M0L4dataS84;
  _block_1739->$1 = 0;
  return _block_1739;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS81) {
  int32_t _M0L6_2atmpS850;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS850 = (int32_t)_M0L4selfS81;
  return _M0L6_2atmpS850;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS73,
  int32_t _M0L13allocate__lenS69,
  int32_t _M0L3lenS70,
  int32_t _M0L11src__offsetS71,
  int32_t _M0L11dst__offsetS72
) {
  int32_t _if__result_1740;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS69 >= 0) {
    if (_M0L3lenS70 >= 0) {
      if (_M0L11src__offsetS71 >= 0) {
        if (_M0L11dst__offsetS72 >= 0) {
          int32_t _M0L6_2atmpS841 = _M0L11src__offsetS71 + _M0L3lenS70;
          int32_t _M0L6_2atmpS842;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS842 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS73);
          if (_M0L6_2atmpS841 <= _M0L6_2atmpS842) {
            int32_t _M0L6_2atmpS840 = _M0L11dst__offsetS72 + _M0L3lenS70;
            _if__result_1740 = _M0L6_2atmpS840 <= _M0L13allocate__lenS69;
          } else {
            _if__result_1740 = 0;
          }
        } else {
          _if__result_1740 = 0;
        }
      } else {
        _if__result_1740 = 0;
      }
    } else {
      _if__result_1740 = 0;
    }
  } else {
    _if__result_1740 = 0;
  }
  if (_if__result_1740) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS69, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS73, _M0L11src__offsetS71, _M0L11dst__offsetS72, _M0L3lenS70);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS74;
    int32_t _M0L6_2atmpS844;
    moonbit_string_t _M0L6_2atmpS843;
    moonbit_string_t* _result_1741;
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
    _M0L6_2atmpS844 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS73);
    moonbit_decref_cycle_free(_M0L3srcS73);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS74, _M0L6_2atmpS844);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS843
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS74);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS74);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_1741
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS843);
    moonbit_decref_cycle_free(_M0L6_2atmpS843);
    return _result_1741;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS79,
  int32_t _M0L13allocate__lenS75,
  int32_t _M0L3lenS76,
  int32_t _M0L11src__offsetS77,
  int32_t _M0L11dst__offsetS78
) {
  int32_t _if__result_1742;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS75 >= 0) {
    if (_M0L3lenS76 >= 0) {
      if (_M0L11src__offsetS77 >= 0) {
        if (_M0L11dst__offsetS78 >= 0) {
          int32_t _M0L6_2atmpS846 = _M0L11src__offsetS77 + _M0L3lenS76;
          int32_t _M0L6_2atmpS847;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS847
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS79);
          if (_M0L6_2atmpS846 <= _M0L6_2atmpS847) {
            int32_t _M0L6_2atmpS845 = _M0L11dst__offsetS78 + _M0L3lenS76;
            _if__result_1742 = _M0L6_2atmpS845 <= _M0L13allocate__lenS75;
          } else {
            _if__result_1742 = 0;
          }
        } else {
          _if__result_1742 = 0;
        }
      } else {
        _if__result_1742 = 0;
      }
    } else {
      _if__result_1742 = 0;
    }
  } else {
    _if__result_1742 = 0;
  }
  if (_if__result_1742) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS75, 0, _M0L3srcS79, _M0L11src__offsetS77, _M0L11dst__offsetS78, _M0L3lenS76);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS80;
    int32_t _M0L6_2atmpS849;
    moonbit_string_t _M0L6_2atmpS848;
    struct _M0TUsiE** _result_1743;
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
    _M0L6_2atmpS849 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS79);
    moonbit_decref_cycle_free(_M0L3srcS79);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS80, _M0L6_2atmpS849);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS848
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS80);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS80);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_1743
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS848);
    moonbit_decref_cycle_free(_M0L6_2atmpS848);
    return _result_1743;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS64,
  moonbit_string_t _M0L3objS63
) {
  struct _M0TPB6Logger _M0L6_2atmpS837;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS64);
  _M0L6_2atmpS837
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS64
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS63, _M0L6_2atmpS837);
  if (_M0L6_2atmpS837.$1) {
    moonbit_decref(_M0L6_2atmpS837.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS66,
  int32_t _M0L3objS65
) {
  struct _M0TPB6Logger _M0L6_2atmpS838;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS66);
  _M0L6_2atmpS838
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS66
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS65, _M0L6_2atmpS838);
  if (_M0L6_2atmpS838.$1) {
    moonbit_decref(_M0L6_2atmpS838.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS68,
  uint64_t _M0L3objS67
) {
  struct _M0TPB6Logger _M0L6_2atmpS839;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS68);
  _M0L6_2atmpS839
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS68
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS67, _M0L6_2atmpS839);
  if (_M0L6_2atmpS839.$1) {
    moonbit_decref(_M0L6_2atmpS839.$1);
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
        int32_t _M0L6_2atmpS810 = _M0L11dst__offsetS16 + _M0L1iS18;
        int32_t _M0L6_2atmpS812 = _M0L11src__offsetS17 + _M0L1iS18;
        int32_t _M0L6_2atmpS811;
        int32_t _M0L6_2atmpS813;
        if (
          _M0L6_2atmpS812 < 0
          || _M0L6_2atmpS812 >= Moonbit_array_length(_M0L3srcS15)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS811 = (int32_t)_M0L3srcS15[_M0L6_2atmpS812];
        if (
          _M0L6_2atmpS810 < 0
          || _M0L6_2atmpS810 >= Moonbit_array_length(_M0L3dstS14)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS14[_M0L6_2atmpS810] = _M0L6_2atmpS811;
        _M0L6_2atmpS813 = _M0L1iS18 + 1;
        _M0L1iS18 = _M0L6_2atmpS813;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS15);
        moonbit_decref_cycle_free(_M0L3dstS14);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS818 = _M0L3lenS19 - 1;
    int32_t _M0L1iS21 = _M0L6_2atmpS818;
    while (1) {
      if (_M0L1iS21 >= 0) {
        int32_t _M0L6_2atmpS814 = _M0L11dst__offsetS16 + _M0L1iS21;
        int32_t _M0L6_2atmpS816 = _M0L11src__offsetS17 + _M0L1iS21;
        int32_t _M0L6_2atmpS815;
        int32_t _M0L6_2atmpS817;
        if (
          _M0L6_2atmpS816 < 0
          || _M0L6_2atmpS816 >= Moonbit_array_length(_M0L3srcS15)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS815 = (int32_t)_M0L3srcS15[_M0L6_2atmpS816];
        if (
          _M0L6_2atmpS814 < 0
          || _M0L6_2atmpS814 >= Moonbit_array_length(_M0L3dstS14)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS14[_M0L6_2atmpS814] = _M0L6_2atmpS815;
        _M0L6_2atmpS817 = _M0L1iS21 - 1;
        _M0L1iS21 = _M0L6_2atmpS817;
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
        int32_t _M0L6_2atmpS819 = _M0L11dst__offsetS25 + _M0L1iS27;
        int32_t _M0L6_2atmpS821 = _M0L11src__offsetS26 + _M0L1iS27;
        moonbit_string_t _M0L6_2atmpS820;
        moonbit_string_t _M0L6_2aoldS1644;
        int32_t _M0L6_2atmpS822;
        if (
          _M0L6_2atmpS821 < 0
          || _M0L6_2atmpS821 >= Moonbit_array_length(_M0L3srcS24)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS820 = (moonbit_string_t)_M0L3srcS24[_M0L6_2atmpS821];
        if (
          _M0L6_2atmpS819 < 0
          || _M0L6_2atmpS819 >= Moonbit_array_length(_M0L3dstS23)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1644 = (moonbit_string_t)_M0L3dstS23[_M0L6_2atmpS819];
        moonbit_incref_cycle_free(_M0L6_2atmpS820);
        moonbit_decref_cycle_free(_M0L6_2aoldS1644);
        _M0L3dstS23[_M0L6_2atmpS819] = _M0L6_2atmpS820;
        _M0L6_2atmpS822 = _M0L1iS27 + 1;
        _M0L1iS27 = _M0L6_2atmpS822;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS24);
        moonbit_decref_cycle_free(_M0L3dstS23);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS827 = _M0L3lenS28 - 1;
    int32_t _M0L1iS30 = _M0L6_2atmpS827;
    while (1) {
      if (_M0L1iS30 >= 0) {
        int32_t _M0L6_2atmpS823 = _M0L11dst__offsetS25 + _M0L1iS30;
        int32_t _M0L6_2atmpS825 = _M0L11src__offsetS26 + _M0L1iS30;
        moonbit_string_t _M0L6_2atmpS824;
        moonbit_string_t _M0L6_2aoldS1645;
        int32_t _M0L6_2atmpS826;
        if (
          _M0L6_2atmpS825 < 0
          || _M0L6_2atmpS825 >= Moonbit_array_length(_M0L3srcS24)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS824 = (moonbit_string_t)_M0L3srcS24[_M0L6_2atmpS825];
        if (
          _M0L6_2atmpS823 < 0
          || _M0L6_2atmpS823 >= Moonbit_array_length(_M0L3dstS23)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1645 = (moonbit_string_t)_M0L3dstS23[_M0L6_2atmpS823];
        moonbit_incref_cycle_free(_M0L6_2atmpS824);
        moonbit_decref_cycle_free(_M0L6_2aoldS1645);
        _M0L3dstS23[_M0L6_2atmpS823] = _M0L6_2atmpS824;
        _M0L6_2atmpS826 = _M0L1iS30 - 1;
        _M0L1iS30 = _M0L6_2atmpS826;
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
        int32_t _M0L6_2atmpS828 = _M0L11dst__offsetS34 + _M0L1iS36;
        int32_t _M0L6_2atmpS830 = _M0L11src__offsetS35 + _M0L1iS36;
        struct _M0TUsiE* _M0L6_2atmpS829;
        struct _M0TUsiE* _M0L6_2aoldS1646;
        int32_t _M0L6_2atmpS831;
        if (
          _M0L6_2atmpS830 < 0
          || _M0L6_2atmpS830 >= Moonbit_array_length(_M0L3srcS33)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS829 = (struct _M0TUsiE*)_M0L3srcS33[_M0L6_2atmpS830];
        if (
          _M0L6_2atmpS828 < 0
          || _M0L6_2atmpS828 >= Moonbit_array_length(_M0L3dstS32)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1646 = (struct _M0TUsiE*)_M0L3dstS32[_M0L6_2atmpS828];
        if (_M0L6_2atmpS829) {
          moonbit_incref_cycle_free(_M0L6_2atmpS829);
        }
        if (_M0L6_2aoldS1646) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1646);
        }
        _M0L3dstS32[_M0L6_2atmpS828] = _M0L6_2atmpS829;
        _M0L6_2atmpS831 = _M0L1iS36 + 1;
        _M0L1iS36 = _M0L6_2atmpS831;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS33);
        moonbit_decref_cycle_free(_M0L3dstS32);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS836 = _M0L3lenS37 - 1;
    int32_t _M0L1iS39 = _M0L6_2atmpS836;
    while (1) {
      if (_M0L1iS39 >= 0) {
        int32_t _M0L6_2atmpS832 = _M0L11dst__offsetS34 + _M0L1iS39;
        int32_t _M0L6_2atmpS834 = _M0L11src__offsetS35 + _M0L1iS39;
        struct _M0TUsiE* _M0L6_2atmpS833;
        struct _M0TUsiE* _M0L6_2aoldS1647;
        int32_t _M0L6_2atmpS835;
        if (
          _M0L6_2atmpS834 < 0
          || _M0L6_2atmpS834 >= Moonbit_array_length(_M0L3srcS33)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS833 = (struct _M0TUsiE*)_M0L3srcS33[_M0L6_2atmpS834];
        if (
          _M0L6_2atmpS832 < 0
          || _M0L6_2atmpS832 >= Moonbit_array_length(_M0L3dstS32)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1647 = (struct _M0TUsiE*)_M0L3dstS32[_M0L6_2atmpS832];
        if (_M0L6_2atmpS833) {
          moonbit_incref_cycle_free(_M0L6_2atmpS833);
        }
        if (_M0L6_2aoldS1647) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1647);
        }
        _M0L3dstS32[_M0L6_2atmpS832] = _M0L6_2atmpS833;
        _M0L6_2atmpS835 = _M0L1iS39 - 1;
        _M0L1iS39 = _M0L6_2atmpS835;
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS782) {
  switch (Moonbit_object_tag(_M0L4_2aeS782)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_32.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS782);
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
  void* _M0L11_2aobj__ptrS805,
  struct _M0TPB4Show _M0L8_2aparamS804
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS803 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS805;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS803, _M0L8_2aparamS804);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS802,
  struct _M0TPB4Show _M0L8_2aparamS801
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS800 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS802;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS800, _M0L8_2aparamS801);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS799,
  int32_t _M0L8_2aparamS798
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS797 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS799;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS797, _M0L8_2aparamS798);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS796,
  struct _M0TPC16string10StringView _M0L8_2aparamS795
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS794 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS796;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS794, _M0L8_2aparamS795);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS793,
  moonbit_string_t _M0L8_2aparamS790,
  int32_t _M0L8_2aparamS791,
  int32_t _M0L8_2aparamS792
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS789 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS793;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS789, _M0L8_2aparamS790, _M0L8_2aparamS791, _M0L8_2aparamS792);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS788,
  moonbit_string_t _M0L8_2aparamS787
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS786 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS788;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS786, _M0L8_2aparamS787);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS809;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS775;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS776;
  int32_t _M0L7_2abindS777;
  struct _M0TUsiE** _M0L7_2abindS778;
  int32_t _M0L6_2acntS1652;
  int32_t _M0L2__S779;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS809
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS775
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS775)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 33, 0);
  _M0L12async__testsS775->$0 = _M0L6_2atmpS809;
  _M0L12async__testsS775->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS776
  = _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS777 = _M0L7_2abindS776->$1;
  _M0L7_2abindS778 = _M0L7_2abindS776->$0;
  _M0L6_2acntS1652
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS776));
  if (_M0L6_2acntS1652 > 1) {
    int32_t _M0L11_2anew__cntS1653 = _M0L6_2acntS1652 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS776), _M0L11_2anew__cntS1653);
    moonbit_incref_cycle_free(_M0L7_2abindS778);
  } else if (_M0L6_2acntS1652 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS776);
  }
  _M0L2__S779 = 0;
  while (1) {
    if (_M0L2__S779 < _M0L7_2abindS777) {
      struct _M0TUsiE* _M0L3argS780 =
        (struct _M0TUsiE*)_M0L7_2abindS778[_M0L2__S779];
      moonbit_string_t _M0L6_2atmpS806 = _M0L3argS780->$0;
      int32_t _M0L6_2atmpS807 = _M0L3argS780->$1;
      int32_t _M0L6_2atmpS808;
      moonbit_incref_cycle_free(_M0L6_2atmpS806);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS775, _M0L6_2atmpS806, _M0L6_2atmpS807);
      moonbit_decref_cycle_free(_M0L6_2atmpS806);
      _M0L6_2atmpS808 = _M0L2__S779 + 1;
      _M0L2__S779 = _M0L6_2atmpS808;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS778);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\snn_utils_params\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples34snn__utils__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS775);
  moonbit_decref_cycle_free(_M0L12async__testsS775);
  moonbit_flush_cycles();
  return 0;
}