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
struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c869;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TPB8MutLocalGiE;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TP26RiantR8snn__mbt14SpikingSynapse;

struct _M0TWRPC15error5ErrorEs;

struct _M0TPB4Show;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TUdiE;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TPB5ArrayGbE;

struct _M0TPB5ArrayGRPB5ArrayGfEE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c874;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TPB5ArrayGsE;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0DTPC16option6OptionGfE4Some;

struct _M0TWEu;

struct _M0TPB5ArrayGiE;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0TUddE;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TWRPC15error5ErrorEu;

struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c869 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
};

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

struct _M0TP26RiantR8snn__mbt9PostSpike {
  float $0;
  
};

struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
};

struct _M0TPB5ArrayGbE {
  uint8_t* $0;
  int32_t $1;
  
};

struct _M0TPB5ArrayGRPB5ArrayGfEE {
  struct _M0TPB5ArrayGfE** $0;
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

struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c874 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0TPB5ArrayGUsiEE {
  struct _M0TUsiE** $0;
  int32_t $1;
  
};

struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
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

struct _M0DTPC16option6OptionGfE4Some {
  float $0;
  
};

struct _M0TWEu {
  int32_t(* code)(struct _M0TWEu*);
  
};

struct _M0TPB5ArrayGiE {
  int32_t* $0;
  int32_t $1;
  
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

struct _M0TUddE {
  double $0;
  double $1;
  
};

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError {
  moonbit_string_t $0;
  
};

struct _M0TWRPC15error5ErrorEu {
  int32_t(* code)(struct _M0TWRPC15error5ErrorEu*, void*);
  
};

struct moonbit_result_0 {
  int tag;
  union { int32_t ok; void* err;  } data;
  
};

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS881(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS874(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS869(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS846(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S839(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse20random__with__delays(
  struct _M0TP26RiantR8snn__mbt2IF*,
  struct _M0TP26RiantR8snn__mbt2IF*,
  moonbit_string_t,
  float,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*,
  float,
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

int32_t _M0MPC15float5Float7to__int(float);

int32_t _M0MPC15array5Array5clearGfE(struct _M0TPB5ArrayGfE*);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t,
  struct _M0TPB5ArrayGfE*
);

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE*);

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE*);

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE*,
  int32_t
);

int32_t _M0MPC15array5Array28unsafe__truncate__to__lengthGfE(
  struct _M0TPB5ArrayGfE*,
  int32_t
);

int32_t _M0FPB7printlnGsE(moonbit_string_t);

int32_t _M0MPC16double6Double7is__inf(double);

int32_t _M0MPC16double6Double7is__nan(double);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(int32_t);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(int32_t);

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t
);

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t);

int32_t _M0MPC15array5Array4pushGfE(struct _M0TPB5ArrayGfE*, float);

int32_t _M0MPC15array5Array4pushGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE*,
  moonbit_string_t
);

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  struct _M0TUsiE*
);

int32_t _M0MPC15array5Array7reallocGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array7reallocGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array7reallocGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  int32_t
);

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE*,
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

int32_t _M0MPC15array5Array8capacityGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array8capacityGiE(struct _M0TPB5ArrayGiE*);

int32_t _M0MPC15array5Array8capacityGsE(struct _M0TPB5ArrayGsE*);

int32_t _M0MPC15array5Array8capacityGUsiEE(struct _M0TPB5ArrayGUsiEE*);

int32_t _M0FPB23array__growth__capacity(int32_t, int32_t, int32_t);

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

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

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float*,
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

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float*,
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float*,
  int32_t,
  float*,
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

int32_t _M0MPB18UninitializedArray6lengthGfE(float*);

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

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(moonbit_string_t);

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(moonbit_string_t);

moonbit_string_t* _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(
  moonbit_string_t
);

struct _M0TUsiE** _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(
  moonbit_string_t
);

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
} const moonbit_string_literal_17 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_15 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_23 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_19 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[12]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 11, 44, 34, 
    109, 101, 115, 115, 97, 103, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[53]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_14 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_11 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[25]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 24, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 114, 101, 115, 117, 108, 116, 34, 
    44, 34, 102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[119]; 
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 118, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 115, 112, 105, 107, 101, 115, 121, 
    110, 97, 112, 115, 101, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 
    116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 
    115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 
    97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 46, 77, 111, 111, 
    110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 
    73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 
    115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_12 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[117]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 116, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 115, 112, 105, 107, 101, 115, 121, 
    110, 97, 112, 115, 101, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 
    116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 
    115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 
    97, 108, 74, 115, 69, 114, 114, 111, 114, 46, 77, 111, 111, 110, 
    66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 
    110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 
    0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_24 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_21 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_18 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_9 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_27 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_13 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_16 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 73, 110, 115, 112, 101, 
    99, 116, 69, 114, 114, 111, 114, 46, 73, 110, 115, 112, 101, 99, 
    116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_25 =
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
} const moonbit_string_literal_22 =
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
} const moonbit_string_literal_10 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_26 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_20 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
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
} const _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS881$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS881
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[83] =
  {
    sizeof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c869)
    / 4, 1,
    offsetof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c869, $1)
    / 4
    * 2,
    sizeof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c874)
    / 4, 1,
    offsetof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c874, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

double _M0FPB18double__max__value;

double _M0FPB18double__min__value;

double _M0FPC16double14not__a__number;

double _M0FPC16double13neg__infinity;

double _M0FPC16double13min__positive;

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS1815
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS902,
  moonbit_string_t _M0L8filenameS871,
  int32_t _M0L5indexS873
) {
  struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c869* _closure_1848;
  struct _M0TWEu* _M0L13handle__startS869;
  struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c874* _closure_1849;
  struct _M0TWssbEu* _M0L14handle__resultS874;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS881;
  void* _M0L11_2atry__errS896;
  struct moonbit_result_0 _tmp_1851;
  int32_t _handle__error__result_1852;
  int32_t _M0L6_2atmpS1803;
  void* _M0L3errS897;
  moonbit_string_t _M0L4nameS899;
  struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS900;
  moonbit_string_t _M0L7_2anameS901;
  int32_t _M0L6_2acntS1842;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS871);
  _closure_1848
  = (struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c869*)moonbit_malloc(sizeof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c869));
  Moonbit_object_header(_closure_1848)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_1848->code
  = &_M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS869;
  _closure_1848->$0 = _M0L5indexS873;
  _closure_1848->$1 = _M0L8filenameS871;
  _M0L13handle__startS869 = (struct _M0TWEu*)_closure_1848;
  moonbit_incref_cycle_free(_M0L8filenameS871);
  _closure_1849
  = (struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c874*)moonbit_malloc(sizeof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c874));
  Moonbit_object_header(_closure_1849)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_1849->code
  = &_M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS874;
  _closure_1849->$0 = _M0L5indexS873;
  _closure_1849->$1 = _M0L8filenameS871;
  _M0L14handle__resultS874 = (struct _M0TWssbEu*)_closure_1849;
  _M0L17error__to__stringS881
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS881$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _tmp_1851
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS902, _M0L8filenameS871, _M0L5indexS873, _M0L13handle__startS869, _M0L14handle__resultS874, _M0L17error__to__stringS881);
  if (_tmp_1851.tag) {
    int32_t const _M0L5_2aokS1812 = _tmp_1851.data.ok;
    _handle__error__result_1852 = _M0L5_2aokS1812;
  } else {
    void* const _M0L6_2aerrS1813 = _tmp_1851.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS881);
    moonbit_decref_cycle_free(_M0L13handle__startS869);
    _M0L11_2atry__errS896 = _M0L6_2aerrS1813;
    goto join_895;
  }
  if (_handle__error__result_1852) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS881);
    moonbit_decref_cycle_free(_M0L13handle__startS869);
    _M0L6_2atmpS1803 = 1;
  } else {
    struct moonbit_result_0 _tmp_1853;
    int32_t _handle__error__result_1854;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
    _tmp_1853
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS902, _M0L8filenameS871, _M0L5indexS873, _M0L13handle__startS869, _M0L14handle__resultS874, _M0L17error__to__stringS881);
    if (_tmp_1853.tag) {
      int32_t const _M0L5_2aokS1810 = _tmp_1853.data.ok;
      _handle__error__result_1854 = _M0L5_2aokS1810;
    } else {
      void* const _M0L6_2aerrS1811 = _tmp_1853.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS881);
      moonbit_decref_cycle_free(_M0L13handle__startS869);
      _M0L11_2atry__errS896 = _M0L6_2aerrS1811;
      goto join_895;
    }
    if (_handle__error__result_1854) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS881);
      moonbit_decref_cycle_free(_M0L13handle__startS869);
      _M0L6_2atmpS1803 = 1;
    } else {
      struct moonbit_result_0 _tmp_1855;
      int32_t _handle__error__result_1856;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
      _tmp_1855
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS902, _M0L8filenameS871, _M0L5indexS873, _M0L13handle__startS869, _M0L14handle__resultS874, _M0L17error__to__stringS881);
      if (_tmp_1855.tag) {
        int32_t const _M0L5_2aokS1808 = _tmp_1855.data.ok;
        _handle__error__result_1856 = _M0L5_2aokS1808;
      } else {
        void* const _M0L6_2aerrS1809 = _tmp_1855.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS881);
        moonbit_decref_cycle_free(_M0L13handle__startS869);
        _M0L11_2atry__errS896 = _M0L6_2aerrS1809;
        goto join_895;
      }
      if (_handle__error__result_1856) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS881);
        moonbit_decref_cycle_free(_M0L13handle__startS869);
        _M0L6_2atmpS1803 = 1;
      } else {
        struct moonbit_result_0 _tmp_1857;
        int32_t _handle__error__result_1858;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
        _tmp_1857
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS902, _M0L8filenameS871, _M0L5indexS873, _M0L13handle__startS869, _M0L14handle__resultS874, _M0L17error__to__stringS881);
        if (_tmp_1857.tag) {
          int32_t const _M0L5_2aokS1806 = _tmp_1857.data.ok;
          _handle__error__result_1858 = _M0L5_2aokS1806;
        } else {
          void* const _M0L6_2aerrS1807 = _tmp_1857.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS881);
          moonbit_decref_cycle_free(_M0L13handle__startS869);
          _M0L11_2atry__errS896 = _M0L6_2aerrS1807;
          goto join_895;
        }
        if (_handle__error__result_1858) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS881);
          moonbit_decref_cycle_free(_M0L13handle__startS869);
          _M0L6_2atmpS1803 = 1;
        } else {
          struct moonbit_result_0 _tmp_1859;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
          _tmp_1859
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS902, _M0L8filenameS871, _M0L5indexS873, _M0L13handle__startS869, _M0L14handle__resultS874, _M0L17error__to__stringS881);
          moonbit_decref_cycle_free(_M0L13handle__startS869);
          moonbit_decref_cycle_free(_M0L17error__to__stringS881);
          if (_tmp_1859.tag) {
            int32_t const _M0L5_2aokS1804 = _tmp_1859.data.ok;
            _M0L6_2atmpS1803 = _M0L5_2aokS1804;
          } else {
            void* const _M0L6_2aerrS1805 = _tmp_1859.data.err;
            _M0L11_2atry__errS896 = _M0L6_2aerrS1805;
            goto join_895;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS1803) {
    void* _M0L131RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1814 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L131RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1814)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L131RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1814)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS896
    = _M0L131RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1814;
    goto join_895;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS874);
  }
  goto joinlet_1850;
  join_895:;
  _M0L3errS897 = _M0L11_2atry__errS896;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS900
  = (struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS897;
  _M0L7_2anameS901 = _M0L36_2aMoonBitTestDriverInternalSkipTestS900->$0;
  _M0L6_2acntS1842
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS900));
  if (_M0L6_2acntS1842 > 1) {
    int32_t _M0L11_2anew__cntS1843 = _M0L6_2acntS1842 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS900), _M0L11_2anew__cntS1843);
    moonbit_incref_cycle_free(_M0L7_2anameS901);
  } else if (_M0L6_2acntS1842 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS900);
  }
  _M0L4nameS899 = _M0L7_2anameS901;
  goto join_898;
  goto joinlet_1860;
  join_898:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS874(_M0L14handle__resultS874, _M0L4nameS899, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS874);
  moonbit_decref_cycle_free(_M0L4nameS899);
  joinlet_1860:;
  joinlet_1850:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS881(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS1802,
  void* _M0L3errS882
) {
  void* _M0L1eS884;
  moonbit_string_t _M0L1eS886;
  moonbit_string_t _result_1863;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS882)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS887 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS882;
      moonbit_string_t _M0L4_2aeS888 = _M0L10_2aFailureS887->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS888);
      _M0L1eS886 = _M0L4_2aeS888;
      goto join_885;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS889 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS882;
      moonbit_string_t _M0L4_2aeS890 = _M0L15_2aInspectErrorS889->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS890);
      _M0L1eS886 = _M0L4_2aeS890;
      goto join_885;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS891 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS882;
      moonbit_string_t _M0L4_2aeS892 = _M0L16_2aSnapshotErrorS891->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS892);
      _M0L1eS886 = _M0L4_2aeS892;
      goto join_885;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS893 =
        (struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS882;
      moonbit_string_t _M0L4_2aeS894 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS893->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS894);
      _M0L1eS886 = _M0L4_2aeS894;
      goto join_885;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS882);
      _M0L1eS884 = _M0L3errS882;
      goto join_883;
      break;
    }
  }
  join_885:;
  return _M0L1eS886;
  join_883:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _result_1863 = _M0FP15Error10to__string(_M0L1eS884);
  moonbit_decref_cycle_free(_M0L1eS884);
  return _result_1863;
}

int32_t _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS874(
  struct _M0TWssbEu* _M0L6_2aenvS1799,
  moonbit_string_t _M0L10__testnameS875,
  moonbit_string_t _M0L7messageS876,
  int32_t _M0L7skippedS877
) {
  struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c874* _M0L14_2acasted__envS1800;
  moonbit_string_t _M0L8filenameS871;
  int32_t _M0L5indexS873;
  moonbit_string_t _M0L10file__nameS878;
  moonbit_string_t _M0L7messageS879;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS880;
  moonbit_string_t _M0L6_2atmpS1801;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1800
  = (struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c874*)_M0L6_2aenvS1799;
  _M0L8filenameS871 = _M0L14_2acasted__envS1800->$1;
  _M0L5indexS873 = _M0L14_2acasted__envS1800->$0;
  if (!_M0L7skippedS877 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS878
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS871, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS879
  = _M0MPC16string6String14escape_2einner(_M0L7messageS876, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS880
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS880, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS880, _M0L10file__nameS878);
  moonbit_decref_cycle_free(_M0L10file__nameS878);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS880, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS880, _M0L5indexS873);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS880, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS880, _M0L7messageS879);
  moonbit_decref_cycle_free(_M0L7messageS879);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS880, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1801
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS880);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS880);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1801);
  moonbit_decref_cycle_free(_M0L6_2atmpS1801);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS869(
  struct _M0TWEu* _M0L6_2aenvS1796
) {
  struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c869* _M0L14_2acasted__envS1797;
  moonbit_string_t _M0L8filenameS871;
  int32_t _M0L5indexS873;
  moonbit_string_t _M0L10file__nameS870;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS872;
  moonbit_string_t _M0L6_2atmpS1798;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1797
  = (struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fspikesynapse__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c869*)_M0L6_2aenvS1796;
  _M0L8filenameS871 = _M0L14_2acasted__envS1797->$1;
  _M0L5indexS873 = _M0L14_2acasted__envS1797->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS870
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS871, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS872
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS872, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS872, _M0L10file__nameS870);
  moonbit_decref_cycle_free(_M0L10file__nameS870);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS872, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS872, _M0L5indexS873);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS872, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1798
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS872);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS872);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1798);
  moonbit_decref_cycle_free(_M0L6_2atmpS1798);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S839;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS846;
  struct _M0TUsiE** _M0L6_2atmpS1795;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS853;
  moonbit_string_t* _M0L9cli__argsS854;
  moonbit_string_t _M0L6_2atmpS1794;
  moonbit_string_t _M0L6_2atmpS1793;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS855;
  int32_t _M0L7_2abindS856;
  moonbit_string_t* _M0L7_2abindS857;
  int32_t _M0L6_2acntS1844;
  int32_t _M0L2__S858;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S839 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS846 = 0;
  _M0L6_2atmpS1795 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS853
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS853)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS853->$0 = _M0L6_2atmpS1795;
  _M0L16file__and__indexS853->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS854
  = _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS854)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS1794 = (moonbit_string_t)_M0L9cli__argsS854[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS1794);
  moonbit_decref_cycle_free(_M0L9cli__argsS854);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1793
  = _M0MP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS1794);
  moonbit_decref_cycle_free(_M0L6_2atmpS1794);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS855
  = _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS846(_M0L51moonbit__test__driver__internal__split__mbt__stringS846, _M0L6_2atmpS1793, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS1793);
  _M0L7_2abindS856 = _M0L10test__argsS855->$1;
  _M0L7_2abindS857 = _M0L10test__argsS855->$0;
  _M0L6_2acntS1844
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS855));
  if (_M0L6_2acntS1844 > 1) {
    int32_t _M0L11_2anew__cntS1845 = _M0L6_2acntS1844 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS855), _M0L11_2anew__cntS1845);
    moonbit_incref_cycle_free(_M0L7_2abindS857);
  } else if (_M0L6_2acntS1844 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS855);
  }
  _M0L2__S858 = 0;
  while (1) {
    if (_M0L2__S858 < _M0L7_2abindS856) {
      moonbit_string_t _M0L3argS859 =
        (moonbit_string_t)_M0L7_2abindS857[_M0L2__S858];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS860;
      moonbit_string_t _M0L4fileS861;
      moonbit_string_t _M0L5rangeS862;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS863;
      moonbit_string_t _M0L6_2atmpS1791;
      int32_t _M0L5startS864;
      moonbit_string_t _M0L6_2atmpS1790;
      int32_t _M0L3endS865;
      int32_t _M0L1iS866;
      int32_t _M0L6_2atmpS1792;
      moonbit_incref_cycle_free(_M0L3argS859);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS860
      = _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS846(_M0L51moonbit__test__driver__internal__split__mbt__stringS846, _M0L3argS859, 58);
      moonbit_decref_cycle_free(_M0L3argS859);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS861
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS860, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS862
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS860, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS860);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS863
      = _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS846(_M0L51moonbit__test__driver__internal__split__mbt__stringS846, _M0L5rangeS862, 45);
      moonbit_decref_cycle_free(_M0L5rangeS862);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1791
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS863, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS864
      = _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S839(_M0L45moonbit__test__driver__internal__parse__int__S839, _M0L6_2atmpS1791);
      moonbit_decref_cycle_free(_M0L6_2atmpS1791);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1790
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS863, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS863);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS865
      = _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S839(_M0L45moonbit__test__driver__internal__parse__int__S839, _M0L6_2atmpS1790);
      moonbit_decref_cycle_free(_M0L6_2atmpS1790);
      _M0L1iS866 = _M0L5startS864;
      while (1) {
        if (_M0L1iS866 < _M0L3endS865) {
          struct _M0TUsiE* _M0L8_2atupleS1788;
          int32_t _M0L6_2atmpS1789;
          moonbit_incref_cycle_free(_M0L4fileS861);
          _M0L8_2atupleS1788
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS1788)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS1788->$0 = _M0L4fileS861;
          _M0L8_2atupleS1788->$1 = _M0L1iS866;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS853, _M0L8_2atupleS1788);
          _M0L6_2atmpS1789 = _M0L1iS866 + 1;
          _M0L1iS866 = _M0L6_2atmpS1789;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS861);
        }
        break;
      }
      _M0L6_2atmpS1792 = _M0L2__S858 + 1;
      _M0L2__S858 = _M0L6_2atmpS1792;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS857);
    }
    break;
  }
  return _M0L16file__and__indexS853;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS846(
  int32_t _M0L6_2aenvS1769,
  moonbit_string_t _M0L1sS847,
  int32_t _M0L3sepS848
) {
  moonbit_string_t* _M0L6_2atmpS1787;
  struct _M0TPB5ArrayGsE* _M0L3resS849;
  struct _M0TPB8MutLocalGiE* _M0L1iS850;
  struct _M0TPB8MutLocalGiE* _M0L5startS851;
  int32_t _M0L3valS1782;
  int32_t _M0L6_2atmpS1783;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1787 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS849
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS849)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS849->$0 = _M0L6_2atmpS1787;
  _M0L3resS849->$1 = 0;
  _M0L1iS850
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS850)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS850->$0 = 0;
  _M0L5startS851
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS851)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS851->$0 = 0;
  while (1) {
    int32_t _M0L3valS1770 = _M0L1iS850->$0;
    int32_t _M0L6_2atmpS1771 = Moonbit_array_length(_M0L1sS847);
    if (_M0L3valS1770 < _M0L6_2atmpS1771) {
      int32_t _M0L3valS1774 = _M0L1iS850->$0;
      int32_t _M0L6_2atmpS1773;
      int32_t _M0L6_2atmpS1772;
      int32_t _M0L3valS1781;
      int32_t _M0L6_2atmpS1780;
      if (
        _M0L3valS1774 < 0
        || _M0L3valS1774 >= Moonbit_array_length(_M0L1sS847)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1773 = _M0L1sS847[_M0L3valS1774];
      _M0L6_2atmpS1772 = _M0L6_2atmpS1773;
      if (_M0L6_2atmpS1772 == _M0L3sepS848) {
        int32_t _M0L3valS1776 = _M0L5startS851->$0;
        int32_t _M0L3valS1777 = _M0L1iS850->$0;
        moonbit_string_t _M0L6_2atmpS1775;
        int32_t _M0L3valS1779;
        int32_t _M0L6_2atmpS1778;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS1775
        = _M0MPC16string6String17unsafe__substring(_M0L1sS847, _M0L3valS1776, _M0L3valS1777);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS849, _M0L6_2atmpS1775);
        _M0L3valS1779 = _M0L1iS850->$0;
        _M0L6_2atmpS1778 = _M0L3valS1779 + 1;
        _M0L5startS851->$0 = _M0L6_2atmpS1778;
      }
      _M0L3valS1781 = _M0L1iS850->$0;
      _M0L6_2atmpS1780 = _M0L3valS1781 + 1;
      _M0L1iS850->$0 = _M0L6_2atmpS1780;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS850);
    }
    break;
  }
  _M0L3valS1782 = _M0L5startS851->$0;
  _M0L6_2atmpS1783 = Moonbit_array_length(_M0L1sS847);
  if (_M0L3valS1782 < _M0L6_2atmpS1783) {
    int32_t _M0L3valS1785 = _M0L5startS851->$0;
    int32_t _M0L6_2atmpS1786;
    moonbit_string_t _M0L6_2atmpS1784;
    moonbit_decref_cycle_free(_M0L5startS851);
    _M0L6_2atmpS1786 = Moonbit_array_length(_M0L1sS847);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS1784
    = _M0MPC16string6String17unsafe__substring(_M0L1sS847, _M0L3valS1785, _M0L6_2atmpS1786);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS849, _M0L6_2atmpS1784);
  } else {
    moonbit_decref_cycle_free(_M0L5startS851);
  }
  return _M0L3resS849;
}

int32_t _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S839(
  int32_t _M0L6_2aenvS1762,
  moonbit_string_t _M0L1sS840
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS841;
  int32_t _M0L3lenS842;
  int32_t _M0L7_2abindS843;
  int32_t _M0L1iS844;
  int32_t _result_1868;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS841
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS841)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS841->$0 = 0;
  _M0L3lenS842 = Moonbit_array_length(_M0L1sS840);
  _M0L7_2abindS843 = 0;
  _M0L1iS844 = _M0L7_2abindS843;
  while (1) {
    if (_M0L1iS844 < _M0L3lenS842) {
      int32_t _M0L3valS1767 = _M0L3resS841->$0;
      int32_t _M0L6_2atmpS1764 = _M0L3valS1767 * 10;
      int32_t _M0L6_2atmpS1766;
      int32_t _M0L6_2atmpS1765;
      int32_t _M0L6_2atmpS1763;
      int32_t _M0L6_2atmpS1768;
      if (_M0L1iS844 < 0 || _M0L1iS844 >= Moonbit_array_length(_M0L1sS840)) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1766 = _M0L1sS840[_M0L1iS844];
      _M0L6_2atmpS1765 = _M0L6_2atmpS1766 - 48;
      _M0L6_2atmpS1763 = _M0L6_2atmpS1764 + _M0L6_2atmpS1765;
      _M0L3resS841->$0 = _M0L6_2atmpS1763;
      _M0L6_2atmpS1768 = _M0L1iS844 + 1;
      _M0L1iS844 = _M0L6_2atmpS1768;
      continue;
    }
    break;
  }
  _result_1868 = _M0L3resS841->$0;
  moonbit_decref_cycle_free(_M0L3resS841);
  return _result_1868;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS838
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS838);
  return _M0L4selfS838;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S808,
  moonbit_string_t _M0L12_2adiscard__S809,
  int32_t _M0L12_2adiscard__S810,
  struct _M0TWEu* _M0L12_2adiscard__S811,
  struct _M0TWssbEu* _M0L12_2adiscard__S812,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S813
) {
  struct moonbit_result_0 _result_1869;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _result_1869.tag = 1;
  _result_1869.data.ok = 0;
  return _result_1869;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S814,
  moonbit_string_t _M0L12_2adiscard__S815,
  int32_t _M0L12_2adiscard__S816,
  struct _M0TWEu* _M0L12_2adiscard__S817,
  struct _M0TWssbEu* _M0L12_2adiscard__S818,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S819
) {
  struct moonbit_result_0 _result_1870;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _result_1870.tag = 1;
  _result_1870.data.ok = 0;
  return _result_1870;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S820,
  moonbit_string_t _M0L12_2adiscard__S821,
  int32_t _M0L12_2adiscard__S822,
  struct _M0TWEu* _M0L12_2adiscard__S823,
  struct _M0TWssbEu* _M0L12_2adiscard__S824,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S825
) {
  struct moonbit_result_0 _result_1871;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _result_1871.tag = 1;
  _result_1871.data.ok = 0;
  return _result_1871;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S826,
  moonbit_string_t _M0L12_2adiscard__S827,
  int32_t _M0L12_2adiscard__S828,
  struct _M0TWEu* _M0L12_2adiscard__S829,
  struct _M0TWssbEu* _M0L12_2adiscard__S830,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S831
) {
  struct moonbit_result_0 _result_1872;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _result_1872.tag = 1;
  _result_1872.data.ok = 0;
  return _result_1872;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S832,
  moonbit_string_t _M0L12_2adiscard__S833,
  int32_t _M0L12_2adiscard__S834,
  struct _M0TWEu* _M0L12_2adiscard__S835,
  struct _M0TWssbEu* _M0L12_2adiscard__S836,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S837
) {
  struct moonbit_result_0 _result_1873;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _result_1873.tag = 1;
  _result_1873.data.ok = 0;
  return _result_1873;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S807
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse20random__with__delays(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS776,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS777,
  moonbit_string_t _M0L3symS778,
  float _M0L2muS779,
  float _M0L5sigmaS780,
  float _M0L1pS781,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS782,
  float _M0L7d__meanS788,
  float _M0L6d__stdS789
) {
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS775;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1761;
  struct _M0TPB5ArrayGfE* _M0L4valsS1760;
  int32_t _M0L1nS783;
  struct _M0TPB5ArrayGfE* _M0L6delaysS1753;
  struct _M0TPB8MutLocalGiE* _M0L1kS784;
  #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L3synS775
  = _M0MP26RiantR8snn__mbt14SpikingSynapse6random(_M0L3preS776, _M0L4postS777, _M0L3symS778, _M0L2muS779, _M0L5sigmaS780, _M0L1pS781, _M0L3rngS782);
  _M0L6matrixS1761 = _M0L3synS775->$4;
  _M0L4valsS1760 = _M0L6matrixS1761->$4;
  moonbit_incref_cycle_free(_M0L4valsS1760);
  #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS783 = _M0MPC15array5Array6lengthGfE(_M0L4valsS1760);
  moonbit_decref_cycle_free(_M0L4valsS1760);
  _M0L6delaysS1753 = _M0L3synS775->$5;
  moonbit_incref_cycle_free(_M0L6delaysS1753);
  #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0MPC15array5Array5clearGfE(_M0L6delaysS1753);
  moonbit_decref_cycle_free(_M0L6delaysS1753);
  _M0L1kS784
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS784)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS784->$0 = 0;
  while (1) {
    int32_t _M0L3valS1754 = _M0L1kS784->$0;
    if (_M0L3valS1754 < _M0L1nS783) {
      double _M0L2z1S786;
      struct _M0TUddE* _M0L7_2abindS791;
      double _M0L5_2az1S792;
      float _M0L6_2atmpS1759;
      float _M0L6_2atmpS1758;
      float _M0L1dS787;
      float _M0L7clampedS790;
      struct _M0TPB5ArrayGfE* _M0L6delaysS1755;
      int32_t _M0L3valS1757;
      int32_t _M0L6_2atmpS1756;
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L7_2abindS791 = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS782);
      _M0L5_2az1S792 = _M0L7_2abindS791->$0;
      moonbit_decref_cycle_free(_M0L7_2abindS791);
      _M0L2z1S786 = _M0L5_2az1S792;
      goto join_785;
      goto joinlet_1875;
      join_785:;
      _M0L6_2atmpS1759 = (float)_M0L2z1S786;
      _M0L6_2atmpS1758 = _M0L6d__stdS789 * _M0L6_2atmpS1759;
      _M0L1dS787 = _M0L7d__meanS788 + _M0L6_2atmpS1758;
      if (_M0L1dS787 < 0x0p+0f) {
        _M0L7clampedS790 = 0x0p+0f;
      } else {
        _M0L7clampedS790 = _M0L1dS787;
      }
      _M0L6delaysS1755 = _M0L3synS775->$5;
      moonbit_incref_cycle_free(_M0L6delaysS1755);
      #line 212 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MPC15array5Array4pushGfE(_M0L6delaysS1755, _M0L7clampedS790);
      moonbit_decref_cycle_free(_M0L6delaysS1755);
      _M0L3valS1757 = _M0L1kS784->$0;
      _M0L6_2atmpS1756 = _M0L3valS1757 + 1;
      _M0L1kS784->$0 = _M0L6_2atmpS1756;
      joinlet_1875:;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS784);
    }
    break;
  }
  return _M0L3synS775;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse6random(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS768,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS769,
  moonbit_string_t _M0L3symS774,
  float _M0L2muS770,
  float _M0L5sigmaS771,
  float _M0L1pS772,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS773
) {
  int32_t _M0L1nS1751;
  int32_t _M0L1nS1752;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS767;
  float* _M0L6_2atmpS1750;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1741;
  float* _M0L6_2atmpS1749;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1742;
  float* _M0L6_2atmpS1748;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1743;
  int32_t* _M0L6_2atmpS1747;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS1744;
  float* _M0L6_2atmpS1746;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1745;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _block_1876;
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS1751 = _M0L3preS768->$2;
  _M0L1nS1752 = _M0L4postS769->$2;
  #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6matrixS767
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(_M0L1nS1751, _M0L1nS1752, _M0L2muS770, _M0L5sigmaS771, _M0L1pS772, _M0L3rngS773);
  _M0L6_2atmpS1750 = moonbit_empty_float_array;
  _M0L6_2atmpS1741
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1741)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1741->$0 = _M0L6_2atmpS1750;
  _M0L6_2atmpS1741->$1 = 0;
  _M0L6_2atmpS1749 = moonbit_empty_float_array;
  _M0L6_2atmpS1742
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1742)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1742->$0 = _M0L6_2atmpS1749;
  _M0L6_2atmpS1742->$1 = 0;
  _M0L6_2atmpS1748 = moonbit_empty_float_array;
  _M0L6_2atmpS1743
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1743)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1743->$0 = _M0L6_2atmpS1748;
  _M0L6_2atmpS1743->$1 = 0;
  _M0L6_2atmpS1747 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS1744
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS1744)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS1744->$0 = _M0L6_2atmpS1747;
  _M0L6_2atmpS1744->$1 = 0;
  _M0L6_2atmpS1746 = moonbit_empty_float_array;
  _M0L6_2atmpS1745
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1745)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1745->$0 = _M0L6_2atmpS1746;
  _M0L6_2atmpS1745->$1 = 0;
  moonbit_incref_cycle_free(_M0L3preS768);
  moonbit_incref_cycle_free(_M0L4postS769);
  moonbit_incref_cycle_free(_M0L3symS774);
  _block_1876
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse));
  Moonbit_object_header(_block_1876)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _block_1876->$0 = _M0L3preS768;
  _block_1876->$1 = _M0L4postS769;
  _block_1876->$2 = _M0L3symS774;
  _block_1876->$3 = (moonbit_string_t)moonbit_string_literal_0.data;
  _block_1876->$4 = _M0L6matrixS767;
  _block_1876->$5 = _M0L6_2atmpS1741;
  _block_1876->$6 = _M0L6_2atmpS1742;
  _block_1876->$7 = _M0L6_2atmpS1743;
  _block_1876->$8 = _M0L6_2atmpS1744;
  _block_1876->$9 = _M0L6_2atmpS1745;
  return _block_1876;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter6custom(
  float _M0L2tmS762,
  float _M0L2vtS763,
  float _M0L2vrS764,
  float _M0L2elS765,
  float _M0L1rS766
) {
  float _M0L1cS760;
  float _M0L2glS761;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_1877;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS760 = -0x1p+0f;
  _M0L2glS761 = -0x1p+0f;
  _block_1877
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_1877)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1877->$0 = _M0L1cS760;
  _block_1877->$1 = _M0L2glS761;
  _block_1877->$2 = _M0L2tmS762;
  _block_1877->$3 = _M0L2vtS763;
  _block_1877->$4 = _M0L2vrS764;
  _block_1877->$5 = _M0L2elS765;
  _block_1877->$6 = _M0L1rS766;
  _block_1877->$7 = 0x1p+1f;
  _block_1877->$8 = 0x0p+0f;
  _block_1877->$9 = 0x0p+0f;
  _block_1877->$10 = 0x0p+0f;
  return _block_1877;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS734,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS736,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS739
) {
  struct _M0TPB5ArrayGfE* _M0L1vS733;
  float _M0L2vtS1739;
  float _M0L2vrS1740;
  float _M0L6spreadS735;
  int32_t _M0L7_2abindS737;
  int32_t _M0L1kS738;
  struct _M0TPB5ArrayGfE* _M0L1wS741;
  struct _M0TPB5ArrayGbE* _M0L4fireS742;
  struct _M0TPB5ArrayGiE* _M0L4tabsS743;
  struct _M0TPB5ArrayGfE* _M0L1iS744;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS745;
  struct _M0TPB5ArrayGfE* _M0L2geS746;
  struct _M0TPB5ArrayGfE* _M0L2giS747;
  struct _M0TPB5ArrayGfE* _M0L2heS748;
  struct _M0TPB5ArrayGfE* _M0L2hiS749;
  struct _M0TPB5ArrayGfE* _M0L3gluS750;
  struct _M0TPB5ArrayGfE* _M0L4gabaS751;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS752;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS753;
  float _M0L4e__eS754;
  float _M0L4e__iS755;
  float _M0L3treS756;
  float _M0L3tdeS757;
  float _M0L3triS758;
  float _M0L3tdiS759;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS1738;
  struct _M0TP26RiantR8snn__mbt2IF* _block_1879;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS733 = _M0MPC15array5Array4makeGfE(_M0L1nS734, 0x0p+0f);
  _M0L2vtS1739 = _M0L5paramS736->$3;
  _M0L2vrS1740 = _M0L5paramS736->$4;
  _M0L6spreadS735 = _M0L2vtS1739 - _M0L2vrS1740;
  _M0L7_2abindS737 = 0;
  _M0L1kS738 = _M0L7_2abindS737;
  while (1) {
    if (_M0L1kS738 < _M0L1nS734) {
      float _M0L2vrS1734 = _M0L5paramS736->$4;
      float _M0L6_2atmpS1736;
      float _M0L6_2atmpS1735;
      float _M0L6_2atmpS1733;
      int32_t _M0L6_2atmpS1737;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1736 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS739);
      _M0L6_2atmpS1735 = _M0L6_2atmpS1736 * _M0L6spreadS735;
      _M0L6_2atmpS1733 = _M0L2vrS1734 + _M0L6_2atmpS1735;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS733, _M0L1kS738, _M0L6_2atmpS1733);
      _M0L6_2atmpS1737 = _M0L1kS738 + 1;
      _M0L1kS738 = _M0L6_2atmpS1737;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS741 = _M0MPC15array5Array4makeGfE(_M0L1nS734, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS742 = _M0MPC15array5Array4makeGbE(_M0L1nS734, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS743 = _M0MPC15array5Array4makeGiE(_M0L1nS734, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS744 = _M0MPC15array5Array4makeGfE(_M0L1nS734, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS745 = _M0MPC15array5Array4makeGfE(_M0L1nS734, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS746 = _M0MPC15array5Array4makeGfE(_M0L1nS734, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS747 = _M0MPC15array5Array4makeGfE(_M0L1nS734, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS748 = _M0MPC15array5Array4makeGfE(_M0L1nS734, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS749 = _M0MPC15array5Array4makeGfE(_M0L1nS734, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS750 = _M0MPC15array5Array4makeGfE(_M0L1nS734, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS751 = _M0MPC15array5Array4makeGfE(_M0L1nS734, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS752 = _M0MPC15array5Array4makeGfE(_M0L1nS734, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS753 = _M0MPC15array5Array4makeGfE(_M0L1nS734, 0x1p+0f);
  _M0L4e__eS754 = 0x0p+0f;
  _M0L4e__iS755 = -0x1.2cp+6f;
  _M0L3treS756 = 0x1p+0f;
  _M0L3tdeS757 = 0x1.8p+2f;
  _M0L3triS758 = 0x1p-1f;
  _M0L3tdiS759 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS1738 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS736);
  _block_1879
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_1879)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _block_1879->$0 = _M0L5paramS736;
  _block_1879->$1 = _M0L6_2atmpS1738;
  _block_1879->$2 = _M0L1nS734;
  _block_1879->$3 = _M0L1vS733;
  _block_1879->$4 = _M0L1wS741;
  _block_1879->$5 = _M0L4fireS742;
  _block_1879->$6 = _M0L4tabsS743;
  _block_1879->$7 = _M0L1iS744;
  _block_1879->$8 = _M0L9syn__currS745;
  _block_1879->$9 = _M0L2geS746;
  _block_1879->$10 = _M0L2giS747;
  _block_1879->$11 = _M0L2heS748;
  _block_1879->$12 = _M0L2hiS749;
  _block_1879->$13 = _M0L3gluS750;
  _block_1879->$14 = _M0L4gabaS751;
  _block_1879->$15 = _M0L7gsyn__eS752;
  _block_1879->$16 = _M0L7gsyn__iS753;
  _block_1879->$17 = _M0L4e__eS754;
  _block_1879->$18 = _M0L4e__iS755;
  _block_1879->$19 = _M0L3treS756;
  _block_1879->$20 = _M0L3tdeS757;
  _block_1879->$21 = _M0L3triS758;
  _block_1879->$22 = _M0L3tdiS759;
  return _block_1879;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_1880;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_1880
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_1880)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1880->$0 = 0x1p+1f;
  return _block_1880;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0FP26RiantR8snn__mbt16forward__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS709,
  float _M0L6t__nowS720
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS1732;
  int32_t _M0L6_2atmpS1731;
  int32_t _M0L10use__delayS708;
  struct _M0TPB5ArrayGfE* _M0L3rhoS1730;
  int32_t _M0L6_2atmpS1729;
  int32_t _M0L8use__rhoS710;
  #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6delaysS1732 = _M0L1cS709->$5;
  #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS1731 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS1732);
  _M0L10use__delayS708 = _M0L6_2atmpS1731 > 0;
  _M0L3rhoS1730 = _M0L1cS709->$6;
  #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS1729 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS1730);
  _M0L8use__rhoS710 = _M0L6_2atmpS1729 > 0;
  if (_M0L10use__delayS708) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1692 = _M0L1cS709->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS1691 = _M0L3preS1692->$5;
    int32_t _M0L6n__preS711;
    struct _M0TPB8MutLocalGiE* _M0L1jS712;
    #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6n__preS711 = _M0MPC15array5Array6lengthGbE(_M0L4fireS1691);
    _M0L1jS712
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS712)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS712->$0 = 0;
    while (1) {
      int32_t _M0L3valS1660 = _M0L1jS712->$0;
      if (_M0L3valS1660 < _M0L6n__preS711) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1663 = _M0L1cS709->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS1661 = _M0L3preS1663->$5;
        int32_t _M0L3valS1662 = _M0L1jS712->$0;
        int32_t _M0L3valS1690;
        int32_t _M0L6_2atmpS1689;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS1661, _M0L3valS1662)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1688 =
            _M0L1cS709->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS1686 = _M0L6matrixS1688->$2;
          int32_t _M0L3valS1687 = _M0L1jS712->$0;
          int32_t _M0L5startS713;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1685;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS1682;
          int32_t _M0L3valS1684;
          int32_t _M0L6_2atmpS1683;
          int32_t _M0L3endS714;
          struct _M0TPB8MutLocalGiE* _M0L1sS715;
          #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L5startS713
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS1686, _M0L3valS1687);
          _M0L6matrixS1685 = _M0L1cS709->$4;
          _M0L6rowptrS1682 = _M0L6matrixS1685->$2;
          _M0L3valS1684 = _M0L1jS712->$0;
          _M0L6_2atmpS1683 = _M0L3valS1684 + 1;
          #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L3endS714
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS1682, _M0L6_2atmpS1683);
          _M0L1sS715
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS715)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS715->$0 = _M0L5startS713;
          while (1) {
            int32_t _M0L3valS1664 = _M0L1sS715->$0;
            if (_M0L3valS1664 < _M0L3endS714) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1681 =
                _M0L1cS709->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS1679 = _M0L6matrixS1681->$3;
              int32_t _M0L3valS1680 = _M0L1sS715->$0;
              int32_t _M0L9post__idxS716;
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1678;
              struct _M0TPB5ArrayGfE* _M0L4valsS1676;
              int32_t _M0L3valS1677;
              float _M0L1wS717;
              struct _M0TPB5ArrayGfE* _M0L6delaysS1674;
              int32_t _M0L3valS1675;
              float _M0L1dS718;
              float _M0L9w__scaledS719;
              int32_t _M0L3valS1670;
              int32_t _M0L6_2atmpS1669;
              #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L9post__idxS716
              = _M0MPC15array5Array2atGiE(_M0L6colptrS1679, _M0L3valS1680);
              _M0L6matrixS1678 = _M0L1cS709->$4;
              _M0L4valsS1676 = _M0L6matrixS1678->$4;
              _M0L3valS1677 = _M0L1sS715->$0;
              #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1wS717
              = _M0MPC15array5Array2atGfE(_M0L4valsS1676, _M0L3valS1677);
              _M0L6delaysS1674 = _M0L1cS709->$5;
              _M0L3valS1675 = _M0L1sS715->$0;
              #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1dS718
              = _M0MPC15array5Array2atGfE(_M0L6delaysS1674, _M0L3valS1675);
              if (_M0L8use__rhoS710) {
                struct _M0TPB5ArrayGfE* _M0L3rhoS1672 = _M0L1cS709->$6;
                int32_t _M0L3valS1673 = _M0L1sS715->$0;
                float _M0L6_2atmpS1671;
                #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS1671
                = _M0MPC15array5Array2atGfE(_M0L3rhoS1672, _M0L3valS1673);
                _M0L9w__scaledS719 = _M0L1wS717 * _M0L6_2atmpS1671;
              } else {
                _M0L9w__scaledS719 = _M0L1wS717;
              }
              if (_M0L1dS718 == 0x0p+0f) {
                #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS709, _M0L9post__idxS716, _M0L9w__scaledS719);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS1665 =
                  _M0L1cS709->$7;
                float _M0L6_2atmpS1666 = _M0L6t__nowS720 + _M0L1dS718;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS1667;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS1668;
                #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS1665, _M0L6_2atmpS1666);
                _M0L14pending__postsS1667 = _M0L1cS709->$8;
                #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS1667, _M0L9post__idxS716);
                _M0L16pending__weightsS1668 = _M0L1cS709->$9;
                #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS1668, _M0L9w__scaledS719);
              }
              _M0L3valS1670 = _M0L1sS715->$0;
              _M0L6_2atmpS1669 = _M0L3valS1670 + 1;
              _M0L1sS715->$0 = _M0L6_2atmpS1669;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1sS715);
            }
            break;
          }
        }
        _M0L3valS1690 = _M0L1jS712->$0;
        _M0L6_2atmpS1689 = _M0L3valS1690 + 1;
        _M0L1jS712->$0 = _M0L6_2atmpS1689;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1jS712);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS1726 = _M0L1cS709->$2;
    struct _M0TPB5ArrayGfE* _M0L6targetS723;
    #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    if (
      _M0L3symS1726 == (moonbit_string_t)moonbit_string_literal_9.data
      || Moonbit_array_length(_M0L3symS1726)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
         && 0
            == memcmp(_M0L3symS1726, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS1726) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1727 = _M0L1cS709->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS1816 = _M0L4postS1727->$13;
      moonbit_incref_cycle_free(_M0L8_2afieldS1816);
      _M0L6targetS723 = _M0L8_2afieldS1816;
    } else {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1728 = _M0L1cS709->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS1817 = _M0L4postS1728->$14;
      moonbit_incref_cycle_free(_M0L8_2afieldS1817);
      _M0L6targetS723 = _M0L8_2afieldS1817;
    }
    if (_M0L8use__rhoS710) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1722 = _M0L1cS709->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS1721 = _M0L3preS1722->$5;
      int32_t _M0L6n__preS724;
      struct _M0TPB8MutLocalGiE* _M0L1jS725;
      #line 283 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6n__preS724 = _M0MPC15array5Array6lengthGbE(_M0L4fireS1721);
      _M0L1jS725
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS725)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS725->$0 = 0;
      while (1) {
        int32_t _M0L3valS1693 = _M0L1jS725->$0;
        if (_M0L3valS1693 < _M0L6n__preS724) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1696 = _M0L1cS709->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS1694 = _M0L3preS1696->$5;
          int32_t _M0L3valS1695 = _M0L1jS725->$0;
          int32_t _M0L3valS1720;
          int32_t _M0L6_2atmpS1719;
          #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS1694, _M0L3valS1695)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1718 =
              _M0L1cS709->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS1716 = _M0L6matrixS1718->$2;
            int32_t _M0L3valS1717 = _M0L1jS725->$0;
            int32_t _M0L5startS726;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1715;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS1712;
            int32_t _M0L3valS1714;
            int32_t _M0L6_2atmpS1713;
            int32_t _M0L3endS727;
            struct _M0TPB8MutLocalGiE* _M0L1sS728;
            #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L5startS726
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS1716, _M0L3valS1717);
            _M0L6matrixS1715 = _M0L1cS709->$4;
            _M0L6rowptrS1712 = _M0L6matrixS1715->$2;
            _M0L3valS1714 = _M0L1jS725->$0;
            _M0L6_2atmpS1713 = _M0L3valS1714 + 1;
            #line 288 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L3endS727
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS1712, _M0L6_2atmpS1713);
            _M0L1sS728
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS728)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS728->$0 = _M0L5startS726;
            while (1) {
              int32_t _M0L3valS1697 = _M0L1sS728->$0;
              if (_M0L3valS1697 < _M0L3endS727) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1711 =
                  _M0L1cS709->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS1709 =
                  _M0L6matrixS1711->$3;
                int32_t _M0L3valS1710 = _M0L1sS728->$0;
                int32_t _M0L9post__idxS729;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1708;
                struct _M0TPB5ArrayGfE* _M0L4valsS1706;
                int32_t _M0L3valS1707;
                float _M0L6_2atmpS1702;
                struct _M0TPB5ArrayGfE* _M0L3rhoS1704;
                int32_t _M0L3valS1705;
                float _M0L6_2atmpS1703;
                float _M0L9w__scaledS730;
                float _M0L6_2atmpS1699;
                float _M0L6_2atmpS1698;
                int32_t _M0L3valS1701;
                int32_t _M0L6_2atmpS1700;
                #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L9post__idxS729
                = _M0MPC15array5Array2atGiE(_M0L6colptrS1709, _M0L3valS1710);
                _M0L6matrixS1708 = _M0L1cS709->$4;
                _M0L4valsS1706 = _M0L6matrixS1708->$4;
                _M0L3valS1707 = _M0L1sS728->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS1702
                = _M0MPC15array5Array2atGfE(_M0L4valsS1706, _M0L3valS1707);
                _M0L3rhoS1704 = _M0L1cS709->$6;
                _M0L3valS1705 = _M0L1sS728->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS1703
                = _M0MPC15array5Array2atGfE(_M0L3rhoS1704, _M0L3valS1705);
                _M0L9w__scaledS730 = _M0L6_2atmpS1702 * _M0L6_2atmpS1703;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS1699
                = _M0MPC15array5Array2atGfE(_M0L6targetS723, _M0L9post__idxS729);
                _M0L6_2atmpS1698 = _M0L6_2atmpS1699 + _M0L9w__scaledS730;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array3setGfE(_M0L6targetS723, _M0L9post__idxS729, _M0L6_2atmpS1698);
                _M0L3valS1701 = _M0L1sS728->$0;
                _M0L6_2atmpS1700 = _M0L3valS1701 + 1;
                _M0L1sS728->$0 = _M0L6_2atmpS1700;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS728);
              }
              break;
            }
          }
          _M0L3valS1720 = _M0L1jS725->$0;
          _M0L6_2atmpS1719 = _M0L3valS1720 + 1;
          _M0L1jS725->$0 = _M0L6_2atmpS1719;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1jS725);
          moonbit_decref_cycle_free(_M0L6targetS723);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1723 =
        _M0L1cS709->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1725 = _M0L1cS709->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS1724 = _M0L3preS1725->$5;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS1723, _M0L4fireS1724, _M0L6targetS723);
      moonbit_decref_cycle_free(_M0L6targetS723);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25deliver__pending__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS701,
  float _M0L6t__nowS704
) {
  struct _M0TPB5ArrayGfE* _M0L14pending__timesS1659;
  int32_t _M0L1nS700;
  struct _M0TPB8MutLocalGiE* _M0L4keptS702;
  struct _M0TPB8MutLocalGiE* _M0L1kS703;
  int32_t _M0L3valS1658;
  int32_t _M0L6_2atmpS1657;
  struct _M0TPB8MutLocalGiE* _M0L4dropS706;
  #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L14pending__timesS1659 = _M0L1cS701->$7;
  #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS700 = _M0MPC15array5Array6lengthGfE(_M0L14pending__timesS1659);
  if (_M0L1nS700 == 0) {
    return 0;
  }
  _M0L4keptS702
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4keptS702)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4keptS702->$0 = 0;
  _M0L1kS703
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS703)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS703->$0 = 0;
  while (1) {
    int32_t _M0L3valS1620 = _M0L1kS703->$0;
    if (_M0L3valS1620 < _M0L1nS700) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS1622 = _M0L1cS701->$7;
      int32_t _M0L3valS1623 = _M0L1kS703->$0;
      float _M0L6_2atmpS1621;
      int32_t _M0L3valS1650;
      int32_t _M0L6_2atmpS1649;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS1621
      = _M0MPC15array5Array2atGfE(_M0L14pending__timesS1622, _M0L3valS1623);
      if (_M0L6_2atmpS1621 <= _M0L6t__nowS704) {
        struct _M0TPB5ArrayGiE* _M0L14pending__postsS1628 = _M0L1cS701->$8;
        int32_t _M0L3valS1629 = _M0L1kS703->$0;
        int32_t _M0L6_2atmpS1624;
        struct _M0TPB5ArrayGfE* _M0L16pending__weightsS1626;
        int32_t _M0L3valS1627;
        float _M0L6_2atmpS1625;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS1624
        = _M0MPC15array5Array2atGiE(_M0L14pending__postsS1628, _M0L3valS1629);
        _M0L16pending__weightsS1626 = _M0L1cS701->$9;
        _M0L3valS1627 = _M0L1kS703->$0;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS1625
        = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS1626, _M0L3valS1627);
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS701, _M0L6_2atmpS1624, _M0L6_2atmpS1625);
      } else {
        int32_t _M0L3valS1630 = _M0L4keptS702->$0;
        int32_t _M0L3valS1631 = _M0L1kS703->$0;
        int32_t _M0L3valS1648;
        int32_t _M0L6_2atmpS1647;
        if (_M0L3valS1630 != _M0L3valS1631) {
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS1632 = _M0L1cS701->$7;
          int32_t _M0L3valS1633 = _M0L4keptS702->$0;
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS1635 = _M0L1cS701->$7;
          int32_t _M0L3valS1636 = _M0L1kS703->$0;
          float _M0L6_2atmpS1634;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS1637;
          int32_t _M0L3valS1638;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS1640;
          int32_t _M0L3valS1641;
          int32_t _M0L6_2atmpS1639;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS1642;
          int32_t _M0L3valS1643;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS1645;
          int32_t _M0L3valS1646;
          float _M0L6_2atmpS1644;
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS1634
          = _M0MPC15array5Array2atGfE(_M0L14pending__timesS1635, _M0L3valS1636);
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L14pending__timesS1632, _M0L3valS1633, _M0L6_2atmpS1634);
          _M0L14pending__postsS1637 = _M0L1cS701->$8;
          _M0L3valS1638 = _M0L4keptS702->$0;
          _M0L14pending__postsS1640 = _M0L1cS701->$8;
          _M0L3valS1641 = _M0L1kS703->$0;
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS1639
          = _M0MPC15array5Array2atGiE(_M0L14pending__postsS1640, _M0L3valS1641);
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGiE(_M0L14pending__postsS1637, _M0L3valS1638, _M0L6_2atmpS1639);
          _M0L16pending__weightsS1642 = _M0L1cS701->$9;
          _M0L3valS1643 = _M0L4keptS702->$0;
          _M0L16pending__weightsS1645 = _M0L1cS701->$9;
          _M0L3valS1646 = _M0L1kS703->$0;
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS1644
          = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS1645, _M0L3valS1646);
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L16pending__weightsS1642, _M0L3valS1643, _M0L6_2atmpS1644);
        }
        _M0L3valS1648 = _M0L4keptS702->$0;
        _M0L6_2atmpS1647 = _M0L3valS1648 + 1;
        _M0L4keptS702->$0 = _M0L6_2atmpS1647;
      }
      _M0L3valS1650 = _M0L1kS703->$0;
      _M0L6_2atmpS1649 = _M0L3valS1650 + 1;
      _M0L1kS703->$0 = _M0L6_2atmpS1649;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS703);
    }
    break;
  }
  _M0L3valS1658 = _M0L4keptS702->$0;
  moonbit_decref_cycle_free(_M0L4keptS702);
  _M0L6_2atmpS1657 = _M0L1nS700 - _M0L3valS1658;
  _M0L4dropS706
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4dropS706)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4dropS706->$0 = _M0L6_2atmpS1657;
  while (1) {
    int32_t _M0L3valS1651 = _M0L4dropS706->$0;
    if (_M0L3valS1651 > 0) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS1652 = _M0L1cS701->$7;
      void* _M0L6_2atmpS1819;
      struct _M0TPB5ArrayGiE* _M0L14pending__postsS1653;
      struct _M0TPB5ArrayGfE* _M0L16pending__weightsS1654;
      void* _M0L6_2atmpS1818;
      int32_t _M0L3valS1656;
      int32_t _M0L6_2atmpS1655;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS1819
      = _M0MPC15array5Array3popGfE(_M0L14pending__timesS1652);
      moonbit_decref_cycle_free(_M0L6_2atmpS1819);
      _M0L14pending__postsS1653 = _M0L1cS701->$8;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MPC15array5Array3popGiE(_M0L14pending__postsS1653);
      _M0L16pending__weightsS1654 = _M0L1cS701->$9;
      #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS1818
      = _M0MPC15array5Array3popGfE(_M0L16pending__weightsS1654);
      moonbit_decref_cycle_free(_M0L6_2atmpS1818);
      _M0L3valS1656 = _M0L4dropS706->$0;
      _M0L6_2atmpS1655 = _M0L3valS1656 - 1;
      _M0L4dropS706->$0 = _M0L6_2atmpS1655;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4dropS706);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13apply__weight(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS697,
  int32_t _M0L9post__idxS698,
  float _M0L1wS699
) {
  moonbit_string_t _M0L3symS1607;
  #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L3symS1607 = _M0L1cS697->$2;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  if (
    _M0L3symS1607 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS1607)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS1607, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS1607) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1613 = _M0L1cS697->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS1608 = _M0L4postS1613->$13;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1612 = _M0L1cS697->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS1611 = _M0L4postS1612->$13;
    float _M0L6_2atmpS1610;
    float _M0L6_2atmpS1609;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS1610
    = _M0MPC15array5Array2atGfE(_M0L3gluS1611, _M0L9post__idxS698);
    _M0L6_2atmpS1609 = _M0L6_2atmpS1610 + _M0L1wS699;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L3gluS1608, _M0L9post__idxS698, _M0L6_2atmpS1609);
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1619 = _M0L1cS697->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS1614 = _M0L4postS1619->$14;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1618 = _M0L1cS697->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS1617 = _M0L4postS1618->$14;
    float _M0L6_2atmpS1616;
    float _M0L6_2atmpS1615;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS1616
    = _M0MPC15array5Array2atGfE(_M0L4gabaS1617, _M0L9post__idxS698);
    _M0L6_2atmpS1615 = _M0L6_2atmpS1616 + _M0L1wS699;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L4gabaS1614, _M0L9post__idxS698, _M0L6_2atmpS1615);
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS693
) {
  int32_t _M0L1nS692;
  int32_t _M0L7_2abindS694;
  int32_t _M0L1iS695;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS692 = _M0L1pS693->$2;
  _M0L7_2abindS694 = 0;
  _M0L1iS695 = _M0L7_2abindS694;
  while (1) {
    if (_M0L1iS695 < _M0L1nS692) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1584 = _M0L1pS693->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS1605 = _M0L1pS693->$9;
      float _M0L6_2atmpS1600;
      struct _M0TPB5ArrayGfE* _M0L1vS1604;
      float _M0L6_2atmpS1602;
      float _M0L4e__eS1603;
      float _M0L6_2atmpS1601;
      float _M0L6_2atmpS1597;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1599;
      float _M0L6_2atmpS1598;
      float _M0L6_2atmpS1586;
      struct _M0TPB5ArrayGfE* _M0L2giS1596;
      float _M0L6_2atmpS1591;
      struct _M0TPB5ArrayGfE* _M0L1vS1595;
      float _M0L6_2atmpS1593;
      float _M0L4e__iS1594;
      float _M0L6_2atmpS1592;
      float _M0L6_2atmpS1588;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1590;
      float _M0L6_2atmpS1589;
      float _M0L6_2atmpS1587;
      float _M0L6_2atmpS1585;
      int32_t _M0L6_2atmpS1606;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1600 = _M0MPC15array5Array2atGfE(_M0L2geS1605, _M0L1iS695);
      _M0L1vS1604 = _M0L1pS693->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1602 = _M0MPC15array5Array2atGfE(_M0L1vS1604, _M0L1iS695);
      _M0L4e__eS1603 = _M0L1pS693->$17;
      _M0L6_2atmpS1601 = _M0L6_2atmpS1602 - _M0L4e__eS1603;
      _M0L6_2atmpS1597 = _M0L6_2atmpS1600 * _M0L6_2atmpS1601;
      _M0L7gsyn__eS1599 = _M0L1pS693->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1598
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS1599, _M0L1iS695);
      _M0L6_2atmpS1586 = _M0L6_2atmpS1597 * _M0L6_2atmpS1598;
      _M0L2giS1596 = _M0L1pS693->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1591 = _M0MPC15array5Array2atGfE(_M0L2giS1596, _M0L1iS695);
      _M0L1vS1595 = _M0L1pS693->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1593 = _M0MPC15array5Array2atGfE(_M0L1vS1595, _M0L1iS695);
      _M0L4e__iS1594 = _M0L1pS693->$18;
      _M0L6_2atmpS1592 = _M0L6_2atmpS1593 - _M0L4e__iS1594;
      _M0L6_2atmpS1588 = _M0L6_2atmpS1591 * _M0L6_2atmpS1592;
      _M0L7gsyn__iS1590 = _M0L1pS693->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1589
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS1590, _M0L1iS695);
      _M0L6_2atmpS1587 = _M0L6_2atmpS1588 * _M0L6_2atmpS1589;
      _M0L6_2atmpS1585 = _M0L6_2atmpS1586 + _M0L6_2atmpS1587;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS1584, _M0L1iS695, _M0L6_2atmpS1585);
      _M0L6_2atmpS1606 = _M0L1iS695 + 1;
      _M0L1iS695 = _M0L6_2atmpS1606;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS684,
  float _M0L2dtS687
) {
  int32_t _M0L1nS683;
  int32_t _M0L7_2abindS685;
  int32_t _M0L1iS686;
  int32_t _M0L7_2abindS689;
  int32_t _M0L1iS690;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS683 = _M0L1pS684->$2;
  _M0L7_2abindS685 = 0;
  _M0L1iS686 = _M0L7_2abindS685;
  while (1) {
    if (_M0L1iS686 < _M0L1nS683) {
      struct _M0TPB5ArrayGfE* _M0L2heS1522 = _M0L1pS684->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS1527 = _M0L1pS684->$11;
      float _M0L6_2atmpS1524;
      struct _M0TPB5ArrayGfE* _M0L3gluS1526;
      float _M0L6_2atmpS1525;
      float _M0L6_2atmpS1523;
      struct _M0TPB5ArrayGfE* _M0L2hiS1528;
      struct _M0TPB5ArrayGfE* _M0L2hiS1533;
      float _M0L6_2atmpS1530;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1532;
      float _M0L6_2atmpS1531;
      float _M0L6_2atmpS1529;
      struct _M0TPB5ArrayGfE* _M0L2geS1534;
      struct _M0TPB5ArrayGfE* _M0L2geS1546;
      float _M0L6_2atmpS1536;
      struct _M0TPB5ArrayGfE* _M0L2geS1545;
      float _M0L6_2atmpS1544;
      float _M0L6_2atmpS1542;
      float _M0L3tdeS1543;
      float _M0L6_2atmpS1539;
      struct _M0TPB5ArrayGfE* _M0L2heS1541;
      float _M0L6_2atmpS1540;
      float _M0L6_2atmpS1538;
      float _M0L6_2atmpS1537;
      float _M0L6_2atmpS1535;
      struct _M0TPB5ArrayGfE* _M0L2heS1547;
      struct _M0TPB5ArrayGfE* _M0L2heS1556;
      float _M0L6_2atmpS1549;
      struct _M0TPB5ArrayGfE* _M0L2heS1555;
      float _M0L6_2atmpS1554;
      float _M0L6_2atmpS1552;
      float _M0L3treS1553;
      float _M0L6_2atmpS1551;
      float _M0L6_2atmpS1550;
      float _M0L6_2atmpS1548;
      struct _M0TPB5ArrayGfE* _M0L2giS1557;
      struct _M0TPB5ArrayGfE* _M0L2giS1569;
      float _M0L6_2atmpS1559;
      struct _M0TPB5ArrayGfE* _M0L2giS1568;
      float _M0L6_2atmpS1567;
      float _M0L6_2atmpS1565;
      float _M0L3tdiS1566;
      float _M0L6_2atmpS1562;
      struct _M0TPB5ArrayGfE* _M0L2hiS1564;
      float _M0L6_2atmpS1563;
      float _M0L6_2atmpS1561;
      float _M0L6_2atmpS1560;
      float _M0L6_2atmpS1558;
      struct _M0TPB5ArrayGfE* _M0L2hiS1570;
      struct _M0TPB5ArrayGfE* _M0L2hiS1579;
      float _M0L6_2atmpS1572;
      struct _M0TPB5ArrayGfE* _M0L2hiS1578;
      float _M0L6_2atmpS1577;
      float _M0L6_2atmpS1575;
      float _M0L3triS1576;
      float _M0L6_2atmpS1574;
      float _M0L6_2atmpS1573;
      float _M0L6_2atmpS1571;
      int32_t _M0L6_2atmpS1580;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1524 = _M0MPC15array5Array2atGfE(_M0L2heS1527, _M0L1iS686);
      _M0L3gluS1526 = _M0L1pS684->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1525 = _M0MPC15array5Array2atGfE(_M0L3gluS1526, _M0L1iS686);
      _M0L6_2atmpS1523 = _M0L6_2atmpS1524 + _M0L6_2atmpS1525;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1522, _M0L1iS686, _M0L6_2atmpS1523);
      _M0L2hiS1528 = _M0L1pS684->$12;
      _M0L2hiS1533 = _M0L1pS684->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1530 = _M0MPC15array5Array2atGfE(_M0L2hiS1533, _M0L1iS686);
      _M0L4gabaS1532 = _M0L1pS684->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1531
      = _M0MPC15array5Array2atGfE(_M0L4gabaS1532, _M0L1iS686);
      _M0L6_2atmpS1529 = _M0L6_2atmpS1530 + _M0L6_2atmpS1531;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1528, _M0L1iS686, _M0L6_2atmpS1529);
      _M0L2geS1534 = _M0L1pS684->$9;
      _M0L2geS1546 = _M0L1pS684->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1536 = _M0MPC15array5Array2atGfE(_M0L2geS1546, _M0L1iS686);
      _M0L2geS1545 = _M0L1pS684->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1544 = _M0MPC15array5Array2atGfE(_M0L2geS1545, _M0L1iS686);
      _M0L6_2atmpS1542 = -_M0L6_2atmpS1544;
      _M0L3tdeS1543 = _M0L1pS684->$20;
      _M0L6_2atmpS1539 = _M0L6_2atmpS1542 / _M0L3tdeS1543;
      _M0L2heS1541 = _M0L1pS684->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1540 = _M0MPC15array5Array2atGfE(_M0L2heS1541, _M0L1iS686);
      _M0L6_2atmpS1538 = _M0L6_2atmpS1539 + _M0L6_2atmpS1540;
      _M0L6_2atmpS1537 = _M0L2dtS687 * _M0L6_2atmpS1538;
      _M0L6_2atmpS1535 = _M0L6_2atmpS1536 + _M0L6_2atmpS1537;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1534, _M0L1iS686, _M0L6_2atmpS1535);
      _M0L2heS1547 = _M0L1pS684->$11;
      _M0L2heS1556 = _M0L1pS684->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1549 = _M0MPC15array5Array2atGfE(_M0L2heS1556, _M0L1iS686);
      _M0L2heS1555 = _M0L1pS684->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1554 = _M0MPC15array5Array2atGfE(_M0L2heS1555, _M0L1iS686);
      _M0L6_2atmpS1552 = -_M0L6_2atmpS1554;
      _M0L3treS1553 = _M0L1pS684->$19;
      _M0L6_2atmpS1551 = _M0L6_2atmpS1552 / _M0L3treS1553;
      _M0L6_2atmpS1550 = _M0L2dtS687 * _M0L6_2atmpS1551;
      _M0L6_2atmpS1548 = _M0L6_2atmpS1549 + _M0L6_2atmpS1550;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1547, _M0L1iS686, _M0L6_2atmpS1548);
      _M0L2giS1557 = _M0L1pS684->$10;
      _M0L2giS1569 = _M0L1pS684->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1559 = _M0MPC15array5Array2atGfE(_M0L2giS1569, _M0L1iS686);
      _M0L2giS1568 = _M0L1pS684->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1567 = _M0MPC15array5Array2atGfE(_M0L2giS1568, _M0L1iS686);
      _M0L6_2atmpS1565 = -_M0L6_2atmpS1567;
      _M0L3tdiS1566 = _M0L1pS684->$22;
      _M0L6_2atmpS1562 = _M0L6_2atmpS1565 / _M0L3tdiS1566;
      _M0L2hiS1564 = _M0L1pS684->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1563 = _M0MPC15array5Array2atGfE(_M0L2hiS1564, _M0L1iS686);
      _M0L6_2atmpS1561 = _M0L6_2atmpS1562 + _M0L6_2atmpS1563;
      _M0L6_2atmpS1560 = _M0L2dtS687 * _M0L6_2atmpS1561;
      _M0L6_2atmpS1558 = _M0L6_2atmpS1559 + _M0L6_2atmpS1560;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1557, _M0L1iS686, _M0L6_2atmpS1558);
      _M0L2hiS1570 = _M0L1pS684->$12;
      _M0L2hiS1579 = _M0L1pS684->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1572 = _M0MPC15array5Array2atGfE(_M0L2hiS1579, _M0L1iS686);
      _M0L2hiS1578 = _M0L1pS684->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1577 = _M0MPC15array5Array2atGfE(_M0L2hiS1578, _M0L1iS686);
      _M0L6_2atmpS1575 = -_M0L6_2atmpS1577;
      _M0L3triS1576 = _M0L1pS684->$21;
      _M0L6_2atmpS1574 = _M0L6_2atmpS1575 / _M0L3triS1576;
      _M0L6_2atmpS1573 = _M0L2dtS687 * _M0L6_2atmpS1574;
      _M0L6_2atmpS1571 = _M0L6_2atmpS1572 + _M0L6_2atmpS1573;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1570, _M0L1iS686, _M0L6_2atmpS1571);
      _M0L6_2atmpS1580 = _M0L1iS686 + 1;
      _M0L1iS686 = _M0L6_2atmpS1580;
      continue;
    }
    break;
  }
  _M0L7_2abindS689 = 0;
  _M0L1iS690 = _M0L7_2abindS689;
  while (1) {
    if (_M0L1iS690 < _M0L1nS683) {
      struct _M0TPB5ArrayGfE* _M0L3gluS1581 = _M0L1pS684->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS1582;
      int32_t _M0L6_2atmpS1583;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS1581, _M0L1iS690, 0x0p+0f);
      _M0L4gabaS1582 = _M0L1pS684->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS1582, _M0L1iS690, 0x0p+0f);
      _M0L6_2atmpS1583 = _M0L1iS690 + 1;
      _M0L1iS690 = _M0L6_2atmpS1583;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS669,
  float _M0L2dtS678
) {
  int32_t _M0L1nS668;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S670;
  float _M0L2tmS671;
  float _M0L2elS672;
  float _M0L1rS673;
  float _M0L2vtS674;
  float _M0L2vrS675;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS1521;
  float _M0L11tabs__constS676;
  float _M0L6_2atmpS1520;
  int32_t _M0L11tabs__stepsS677;
  int32_t _M0L7_2abindS679;
  int32_t _M0L1iS680;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS668 = _M0L1pS669->$2;
  _M0L3p__S670 = _M0L1pS669->$0;
  _M0L2tmS671 = _M0L3p__S670->$2;
  _M0L2elS672 = _M0L3p__S670->$5;
  _M0L1rS673 = _M0L3p__S670->$6;
  _M0L2vtS674 = _M0L3p__S670->$3;
  _M0L2vrS675 = _M0L3p__S670->$4;
  _M0L5spikeS1521 = _M0L1pS669->$1;
  _M0L11tabs__constS676 = _M0L5spikeS1521->$0;
  _M0L6_2atmpS1520 = _M0L11tabs__constS676 / _M0L2dtS678;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS677 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1520);
  _M0L7_2abindS679 = 0;
  _M0L1iS680 = _M0L7_2abindS679;
  while (1) {
    if (_M0L1iS680 < _M0L1nS668) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS1480 = _M0L1pS669->$6;
      int32_t _M0L6_2atmpS1479;
      struct _M0TPB5ArrayGfE* _M0L1vS1486;
      struct _M0TPB5ArrayGfE* _M0L1vS1507;
      float _M0L6_2atmpS1488;
      float _M0L6_2atmpS1490;
      struct _M0TPB5ArrayGfE* _M0L1vS1506;
      float _M0L6_2atmpS1505;
      float _M0L6_2atmpS1504;
      float _M0L6_2atmpS1496;
      struct _M0TPB5ArrayGfE* _M0L1wS1503;
      float _M0L6_2atmpS1502;
      float _M0L6_2atmpS1499;
      struct _M0TPB5ArrayGfE* _M0L1iS1501;
      float _M0L6_2atmpS1500;
      float _M0L6_2atmpS1498;
      float _M0L6_2atmpS1497;
      float _M0L6_2atmpS1492;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS1495;
      float _M0L6_2atmpS1494;
      float _M0L6_2atmpS1493;
      float _M0L6_2atmpS1491;
      float _M0L6_2atmpS1489;
      float _M0L6_2atmpS1487;
      struct _M0TPB5ArrayGbE* _M0L4fireS1508;
      struct _M0TPB5ArrayGfE* _M0L1vS1511;
      float _M0L6_2atmpS1510;
      int32_t _M0L6_2atmpS1509;
      struct _M0TPB5ArrayGfE* _M0L1vS1512;
      struct _M0TPB5ArrayGbE* _M0L4fireS1514;
      float _M0L6_2atmpS1513;
      struct _M0TPB5ArrayGiE* _M0L4tabsS1516;
      struct _M0TPB5ArrayGbE* _M0L4fireS1518;
      int32_t _M0L6_2atmpS1517;
      int32_t _M0L6_2atmpS1478;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1479
      = _M0MPC15array5Array2atGiE(_M0L4tabsS1480, _M0L1iS680);
      if (_M0L6_2atmpS1479 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS1481 = _M0L1pS669->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1482;
        struct _M0TPB5ArrayGiE* _M0L4tabsS1485;
        int32_t _M0L6_2atmpS1484;
        int32_t _M0L6_2atmpS1483;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS1481, _M0L1iS680, 0);
        _M0L4tabsS1482 = _M0L1pS669->$6;
        _M0L4tabsS1485 = _M0L1pS669->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1484
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1485, _M0L1iS680);
        _M0L6_2atmpS1483 = _M0L6_2atmpS1484 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS1482, _M0L1iS680, _M0L6_2atmpS1483);
        goto join_681;
      }
      _M0L1vS1486 = _M0L1pS669->$3;
      _M0L1vS1507 = _M0L1pS669->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1488 = _M0MPC15array5Array2atGfE(_M0L1vS1507, _M0L1iS680);
      _M0L6_2atmpS1490 = _M0L2dtS678 / _M0L2tmS671;
      _M0L1vS1506 = _M0L1pS669->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1505 = _M0MPC15array5Array2atGfE(_M0L1vS1506, _M0L1iS680);
      _M0L6_2atmpS1504 = _M0L6_2atmpS1505 - _M0L2elS672;
      _M0L6_2atmpS1496 = -_M0L6_2atmpS1504;
      _M0L1wS1503 = _M0L1pS669->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1502 = _M0MPC15array5Array2atGfE(_M0L1wS1503, _M0L1iS680);
      _M0L6_2atmpS1499 = -_M0L6_2atmpS1502;
      _M0L1iS1501 = _M0L1pS669->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1500 = _M0MPC15array5Array2atGfE(_M0L1iS1501, _M0L1iS680);
      _M0L6_2atmpS1498 = _M0L6_2atmpS1499 + _M0L6_2atmpS1500;
      _M0L6_2atmpS1497 = _M0L1rS673 * _M0L6_2atmpS1498;
      _M0L6_2atmpS1492 = _M0L6_2atmpS1496 + _M0L6_2atmpS1497;
      _M0L9syn__currS1495 = _M0L1pS669->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1494
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS1495, _M0L1iS680);
      _M0L6_2atmpS1493 = _M0L1rS673 * _M0L6_2atmpS1494;
      _M0L6_2atmpS1491 = _M0L6_2atmpS1492 - _M0L6_2atmpS1493;
      _M0L6_2atmpS1489 = _M0L6_2atmpS1490 * _M0L6_2atmpS1491;
      _M0L6_2atmpS1487 = _M0L6_2atmpS1488 + _M0L6_2atmpS1489;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1486, _M0L1iS680, _M0L6_2atmpS1487);
      _M0L4fireS1508 = _M0L1pS669->$5;
      _M0L1vS1511 = _M0L1pS669->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS1510 = _M0MPC15array5Array2atGfE(_M0L1vS1511, _M0L1iS680);
      _M0L6_2atmpS1509 = _M0L6_2atmpS1510 > _M0L2vtS674;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS1508, _M0L1iS680, _M0L6_2atmpS1509);
      _M0L1vS1512 = _M0L1pS669->$3;
      _M0L4fireS1514 = _M0L1pS669->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1514, _M0L1iS680)) {
        _M0L6_2atmpS1513 = _M0L2vrS675;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS1515 = _M0L1pS669->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1513 = _M0MPC15array5Array2atGfE(_M0L1vS1515, _M0L1iS680);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1512, _M0L1iS680, _M0L6_2atmpS1513);
      _M0L4tabsS1516 = _M0L1pS669->$6;
      _M0L4fireS1518 = _M0L1pS669->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1518, _M0L1iS680)) {
        _M0L6_2atmpS1517 = _M0L11tabs__stepsS677;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS1519 = _M0L1pS669->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS1517
        = _M0MPC15array5Array2atGiE(_M0L4tabsS1519, _M0L1iS680);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS1516, _M0L1iS680, _M0L6_2atmpS1517);
      goto join_681;
      goto joinlet_1891;
      join_681:;
      _M0L6_2atmpS1478 = _M0L1iS680 + 1;
      _M0L1iS680 = _M0L6_2atmpS1478;
      continue;
      joinlet_1891:;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS656,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS659,
  struct _M0TPB5ArrayGfE* _M0L7post__gS665
) {
  int32_t _M0L4rowsS655;
  int32_t _M0L7_2abindS657;
  int32_t _M0L1iS658;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS655 = _M0L1mS656->$0;
  _M0L7_2abindS657 = 0;
  _M0L1iS658 = _M0L7_2abindS657;
  while (1) {
    if (_M0L1iS658 < _M0L4rowsS655) {
      int32_t _M0L6_2atmpS1477;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS659, _M0L1iS658)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS1476 = _M0L1mS656->$2;
        int32_t _M0L5startS660;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS1474;
        int32_t _M0L6_2atmpS1475;
        int32_t _M0L3endS661;
        int32_t _M0L1kS662;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS660
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1476, _M0L1iS658);
        _M0L6rowptrS1474 = _M0L1mS656->$2;
        _M0L6_2atmpS1475 = _M0L1iS658 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS661
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1474, _M0L6_2atmpS1475);
        _M0L1kS662 = _M0L5startS660;
        while (1) {
          if (_M0L1kS662 < _M0L3endS661) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS1472 = _M0L1mS656->$3;
            int32_t _M0L9post__idxS663;
            struct _M0TPB5ArrayGfE* _M0L4valsS1471;
            float _M0L1wS664;
            float _M0L6_2atmpS1470;
            float _M0L6_2atmpS1469;
            int32_t _M0L6_2atmpS1473;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS663
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1472, _M0L1kS662);
            _M0L4valsS1471 = _M0L1mS656->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS664
            = _M0MPC15array5Array2atGfE(_M0L4valsS1471, _M0L1kS662);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS1470
            = _M0MPC15array5Array2atGfE(_M0L7post__gS665, _M0L9post__idxS663);
            _M0L6_2atmpS1469 = _M0L6_2atmpS1470 + _M0L1wS664;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS665, _M0L9post__idxS663, _M0L6_2atmpS1469);
            _M0L6_2atmpS1473 = _M0L1kS662 + 1;
            _M0L1kS662 = _M0L6_2atmpS1473;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS1477 = _M0L1iS658 + 1;
      _M0L1iS658 = _M0L6_2atmpS1477;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR6random(
  int32_t _M0L4rowsS649,
  int32_t _M0L4colsS650,
  float _M0L2muS651,
  float _M0L5sigmaS652,
  float _M0L1pS653,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS654
) {
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  return _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(_M0L4rowsS649, _M0L4colsS650, _M0L2muS651, _M0L5sigmaS652, _M0L1pS653, 0, _M0L3rngS654);
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR18random__with__rule(
  int32_t _M0L4rowsS563,
  int32_t _M0L4colsS567,
  float _M0L2muS573,
  float _M0L5sigmaS574,
  float _M0L1pS586,
  int32_t _M0L4ruleS580,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS576
) {
  float* _M0L6_2atmpS1468;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1467;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L5denseS562;
  int32_t _M0L7_2abindS564;
  int32_t _M0L1iS565;
  int32_t _M0L6_2atmpS1466;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS639;
  int32_t* _M0L6_2atmpS1465;
  struct _M0TPB5ArrayGiE* _M0L6colptrS640;
  float* _M0L6_2atmpS1464;
  struct _M0TPB5ArrayGfE* _M0L4valsS641;
  int32_t _M0L7_2abindS642;
  int32_t _M0L1iS643;
  int32_t _M0L6_2atmpS1463;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_1913;
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS1468 = moonbit_empty_float_array;
  _M0L6_2atmpS1467
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS1467)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS1467->$0 = _M0L6_2atmpS1468;
  _M0L6_2atmpS1467->$1 = 0;
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5denseS562
  = _M0MPC15array5Array4makeGRPB5ArrayGfEE(_M0L4rowsS563, _M0L6_2atmpS1467);
  _M0L7_2abindS564 = 0;
  _M0L1iS565 = _M0L7_2abindS564;
  while (1) {
    if (_M0L1iS565 < _M0L4rowsS563) {
      struct _M0TPB5ArrayGfE* _M0L3rowS566;
      int32_t _M0L7_2abindS568;
      int32_t _M0L1jS569;
      int32_t _M0L6_2atmpS1419;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L3rowS566 = _M0MPC15array5Array4makeGfE(_M0L4colsS567, 0x0p+0f);
      _M0L7_2abindS568 = 0;
      _M0L1jS569 = _M0L7_2abindS568;
      while (1) {
        if (_M0L1jS569 < _M0L4colsS567) {
          double _M0L2z1S571;
          struct _M0TUddE* _M0L7_2abindS575;
          double _M0L5_2az1S577;
          float _M0L6_2atmpS1417;
          float _M0L6_2atmpS1416;
          float _M0L1wS572;
          int32_t _M0L6_2atmpS1418;
          #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L7_2abindS575
          = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS576);
          _M0L5_2az1S577 = _M0L7_2abindS575->$0;
          moonbit_decref_cycle_free(_M0L7_2abindS575);
          _M0L2z1S571 = _M0L5_2az1S577;
          goto join_570;
          goto joinlet_1896;
          join_570:;
          _M0L6_2atmpS1417 = (float)_M0L2z1S571;
          _M0L6_2atmpS1416 = _M0L5sigmaS574 * _M0L6_2atmpS1417;
          _M0L1wS572 = _M0L2muS573 + _M0L6_2atmpS1416;
          #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rowS566, _M0L1jS569, _M0L1wS572);
          joinlet_1896:;
          _M0L6_2atmpS1418 = _M0L1jS569 + 1;
          _M0L1jS569 = _M0L6_2atmpS1418;
          continue;
        }
        break;
      }
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGRPB5ArrayGfEE(_M0L5denseS562, _M0L1iS565, _M0L3rowS566);
      _M0L6_2atmpS1419 = _M0L1iS565 + 1;
      _M0L1iS565 = _M0L6_2atmpS1419;
      continue;
    }
    break;
  }
  switch (_M0L4ruleS580) {
    case 0: {
      int32_t _M0L7_2abindS581 = 0;
      int32_t _M0L1iS582 = _M0L7_2abindS581;
      while (1) {
        if (_M0L1iS582 < _M0L4rowsS563) {
          int32_t _M0L7_2abindS583 = 0;
          int32_t _M0L1jS584 = _M0L7_2abindS583;
          int32_t _M0L6_2atmpS1422;
          while (1) {
            if (_M0L1jS584 < _M0L4colsS567) {
              float _M0L1uS585;
              int32_t _M0L6_2atmpS1421;
              #line 143 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
              _M0L1uS585 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS576);
              if (_M0L1uS585 >= _M0L1pS586) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1420;
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1420
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS562, _M0L1iS582);
                #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1420, _M0L1jS584, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS1420);
              }
              _M0L6_2atmpS1421 = _M0L1jS584 + 1;
              _M0L1jS584 = _M0L6_2atmpS1421;
              continue;
            }
            break;
          }
          _M0L6_2atmpS1422 = _M0L1iS582 + 1;
          _M0L1iS582 = _M0L6_2atmpS1422;
          continue;
        }
        break;
      }
      break;
    }
    
    case 1: {
      float _M0L6_2atmpS1440 = (float)_M0L4rowsS563;
      float _M0L6_2atmpS1439 = _M0L6_2atmpS1440 * _M0L1pS586;
      int32_t _M0L7n__keepS589;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS589 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1439);
      if (_M0L7n__keepS589 > 0 && _M0L7n__keepS589 <= _M0L4rowsS563) {
        int32_t _M0L7_2abindS590 = 0;
        int32_t _M0L1jS591 = _M0L7_2abindS590;
        while (1) {
          if (_M0L1jS591 < _M0L4colsS567) {
            int32_t* _M0L6_2atmpS1434 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L8pre__idxS592 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS593;
            int32_t _M0L1kS594;
            int32_t _M0L7n__dropS596;
            int32_t _M0L7_2abindS597;
            int32_t _M0L1kS598;
            int32_t _M0L7_2abindS604;
            int32_t _M0L1kS605;
            int32_t _M0L6_2atmpS1435;
            Moonbit_object_header(_M0L8pre__idxS592)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
            _M0L8pre__idxS592->$0 = _M0L6_2atmpS1434;
            _M0L8pre__idxS592->$1 = 0;
            _M0L7_2abindS593 = 0;
            _M0L1kS594 = _M0L7_2abindS593;
            while (1) {
              if (_M0L1kS594 < _M0L4rowsS563) {
                int32_t _M0L6_2atmpS1423;
                #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L8pre__idxS592, _M0L1kS594);
                _M0L6_2atmpS1423 = _M0L1kS594 + 1;
                _M0L1kS594 = _M0L6_2atmpS1423;
                continue;
              }
              break;
            }
            _M0L7n__dropS596 = _M0L4rowsS563 - _M0L7n__keepS589;
            _M0L7_2abindS597 = 0;
            _M0L1kS598 = _M0L7_2abindS597;
            while (1) {
              if (_M0L1kS598 < _M0L7n__dropS596) {
                float _M0L1uS599;
                float _M0L6_2atmpS1427;
                float _M0L6_2atmpS1429;
                float _M0L6_2atmpS1428;
                float _M0L6_2atmpS1426;
                int32_t _M0L6_2atmpS1425;
                int32_t _M0L6r__idxS600;
                int32_t _M0L10r__clampedS601;
                int32_t _M0L3tmpS602;
                int32_t _M0L6_2atmpS1424;
                int32_t _M0L6_2atmpS1430;
                #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS599 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS576);
                _M0L6_2atmpS1427 = (float)_M0L4rowsS563;
                _M0L6_2atmpS1429 = (float)_M0L1kS598;
                _M0L6_2atmpS1428 = _M0L6_2atmpS1429 * _M0L1uS599;
                _M0L6_2atmpS1426 = _M0L6_2atmpS1427 - _M0L6_2atmpS1428;
                #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1425
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS1426);
                _M0L6r__idxS600 = _M0L1kS598 + _M0L6_2atmpS1425;
                if (_M0L6r__idxS600 >= _M0L4rowsS563) {
                  _M0L10r__clampedS601 = _M0L4rowsS563 - 1;
                } else {
                  _M0L10r__clampedS601 = _M0L6r__idxS600;
                }
                #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS602
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS592, _M0L1kS598);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1424
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS592, _M0L10r__clampedS601);
                #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS592, _M0L1kS598, _M0L6_2atmpS1424);
                #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L8pre__idxS592, _M0L10r__clampedS601, _M0L3tmpS602);
                _M0L6_2atmpS1430 = _M0L1kS598 + 1;
                _M0L1kS598 = _M0L6_2atmpS1430;
                continue;
              }
              break;
            }
            _M0L7_2abindS604 = 0;
            _M0L1kS605 = _M0L7_2abindS604;
            while (1) {
              if (_M0L1kS605 < _M0L7n__dropS596) {
                int32_t _M0L6_2atmpS1432;
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1431;
                int32_t _M0L6_2atmpS1433;
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1432
                = _M0MPC15array5Array2atGiE(_M0L8pre__idxS592, _M0L1kS605);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1431
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS562, _M0L6_2atmpS1432);
                #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1431, _M0L1jS591, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS1431);
                _M0L6_2atmpS1433 = _M0L1kS605 + 1;
                _M0L1kS605 = _M0L6_2atmpS1433;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L8pre__idxS592);
              }
              break;
            }
            _M0L6_2atmpS1435 = _M0L1jS591 + 1;
            _M0L1jS591 = _M0L6_2atmpS1435;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS589 == 0) {
        int32_t _M0L7_2abindS608 = 0;
        int32_t _M0L1iS609 = _M0L7_2abindS608;
        while (1) {
          if (_M0L1iS609 < _M0L4rowsS563) {
            int32_t _M0L7_2abindS610 = 0;
            int32_t _M0L1jS611 = _M0L7_2abindS610;
            int32_t _M0L6_2atmpS1438;
            while (1) {
              if (_M0L1jS611 < _M0L4colsS567) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1436;
                int32_t _M0L6_2atmpS1437;
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1436
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS562, _M0L1iS609);
                #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1436, _M0L1jS611, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS1436);
                _M0L6_2atmpS1437 = _M0L1jS611 + 1;
                _M0L1jS611 = _M0L6_2atmpS1437;
                continue;
              }
              break;
            }
            _M0L6_2atmpS1438 = _M0L1iS609 + 1;
            _M0L1iS609 = _M0L6_2atmpS1438;
            continue;
          }
          break;
        }
      }
      break;
    }
    default: {
      float _M0L6_2atmpS1458 = (float)_M0L4colsS567;
      float _M0L6_2atmpS1457 = _M0L6_2atmpS1458 * _M0L1pS586;
      int32_t _M0L7n__keepS614;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L7n__keepS614 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1457);
      if (_M0L7n__keepS614 > 0 && _M0L7n__keepS614 <= _M0L4colsS567) {
        int32_t _M0L7_2abindS615 = 0;
        int32_t _M0L1iS616 = _M0L7_2abindS615;
        while (1) {
          if (_M0L1iS616 < _M0L4rowsS563) {
            int32_t* _M0L6_2atmpS1452 = (int32_t*)moonbit_empty_int32_array;
            struct _M0TPB5ArrayGiE* _M0L9post__idxS617 =
              (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
            int32_t _M0L7_2abindS618;
            int32_t _M0L1kS619;
            int32_t _M0L7n__dropS621;
            int32_t _M0L7_2abindS622;
            int32_t _M0L1kS623;
            int32_t _M0L7_2abindS629;
            int32_t _M0L1kS630;
            int32_t _M0L6_2atmpS1453;
            Moonbit_object_header(_M0L9post__idxS617)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
            _M0L9post__idxS617->$0 = _M0L6_2atmpS1452;
            _M0L9post__idxS617->$1 = 0;
            _M0L7_2abindS618 = 0;
            _M0L1kS619 = _M0L7_2abindS618;
            while (1) {
              if (_M0L1kS619 < _M0L4colsS567) {
                int32_t _M0L6_2atmpS1441;
                #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array4pushGiE(_M0L9post__idxS617, _M0L1kS619);
                _M0L6_2atmpS1441 = _M0L1kS619 + 1;
                _M0L1kS619 = _M0L6_2atmpS1441;
                continue;
              }
              break;
            }
            _M0L7n__dropS621 = _M0L4colsS567 - _M0L7n__keepS614;
            _M0L7_2abindS622 = 0;
            _M0L1kS623 = _M0L7_2abindS622;
            while (1) {
              if (_M0L1kS623 < _M0L7n__dropS621) {
                float _M0L1uS624;
                float _M0L6_2atmpS1445;
                float _M0L6_2atmpS1447;
                float _M0L6_2atmpS1446;
                float _M0L6_2atmpS1444;
                int32_t _M0L6_2atmpS1443;
                int32_t _M0L6r__idxS625;
                int32_t _M0L10r__clampedS626;
                int32_t _M0L3tmpS627;
                int32_t _M0L6_2atmpS1442;
                int32_t _M0L6_2atmpS1448;
                #line 200 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L1uS624 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS576);
                _M0L6_2atmpS1445 = (float)_M0L4colsS567;
                _M0L6_2atmpS1447 = (float)_M0L1kS623;
                _M0L6_2atmpS1446 = _M0L6_2atmpS1447 * _M0L1uS624;
                _M0L6_2atmpS1444 = _M0L6_2atmpS1445 - _M0L6_2atmpS1446;
                #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1443
                = _M0MPC15float5Float7to__int(_M0L6_2atmpS1444);
                _M0L6r__idxS625 = _M0L1kS623 + _M0L6_2atmpS1443;
                if (_M0L6r__idxS625 >= _M0L4colsS567) {
                  _M0L10r__clampedS626 = _M0L4colsS567 - 1;
                } else {
                  _M0L10r__clampedS626 = _M0L6r__idxS625;
                }
                #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L3tmpS627
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS617, _M0L1kS623);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1442
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS617, _M0L10r__clampedS626);
                #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS617, _M0L1kS623, _M0L6_2atmpS1442);
                #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGiE(_M0L9post__idxS617, _M0L10r__clampedS626, _M0L3tmpS627);
                _M0L6_2atmpS1448 = _M0L1kS623 + 1;
                _M0L1kS623 = _M0L6_2atmpS1448;
                continue;
              }
              break;
            }
            _M0L7_2abindS629 = 0;
            _M0L1kS630 = _M0L7_2abindS629;
            while (1) {
              if (_M0L1kS630 < _M0L7n__dropS621) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1449;
                int32_t _M0L6_2atmpS1450;
                int32_t _M0L6_2atmpS1451;
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1449
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS562, _M0L1iS616);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1450
                = _M0MPC15array5Array2atGiE(_M0L9post__idxS617, _M0L1kS630);
                #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1449, _M0L6_2atmpS1450, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS1449);
                _M0L6_2atmpS1451 = _M0L1kS630 + 1;
                _M0L1kS630 = _M0L6_2atmpS1451;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L9post__idxS617);
              }
              break;
            }
            _M0L6_2atmpS1453 = _M0L1iS616 + 1;
            _M0L1iS616 = _M0L6_2atmpS1453;
            continue;
          }
          break;
        }
      } else if (_M0L7n__keepS614 == 0) {
        int32_t _M0L7_2abindS633 = 0;
        int32_t _M0L1iS634 = _M0L7_2abindS633;
        while (1) {
          if (_M0L1iS634 < _M0L4rowsS563) {
            int32_t _M0L7_2abindS635 = 0;
            int32_t _M0L1jS636 = _M0L7_2abindS635;
            int32_t _M0L6_2atmpS1456;
            while (1) {
              if (_M0L1jS636 < _M0L4colsS567) {
                struct _M0TPB5ArrayGfE* _M0L6_2atmpS1454;
                int32_t _M0L6_2atmpS1455;
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0L6_2atmpS1454
                = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS562, _M0L1iS634);
                #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
                _M0MPC15array5Array3setGfE(_M0L6_2atmpS1454, _M0L1jS636, 0x0p+0f);
                moonbit_decref_cycle_free(_M0L6_2atmpS1454);
                _M0L6_2atmpS1455 = _M0L1jS636 + 1;
                _M0L1jS636 = _M0L6_2atmpS1455;
                continue;
              }
              break;
            }
            _M0L6_2atmpS1456 = _M0L1iS634 + 1;
            _M0L1iS634 = _M0L6_2atmpS1456;
            continue;
          }
          break;
        }
      }
      break;
    }
  }
  _M0L6_2atmpS1466 = _M0L4rowsS563 + 1;
  #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS639 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS1466, 0);
  _M0L6_2atmpS1465 = (int32_t*)moonbit_empty_int32_array;
  _M0L6colptrS640
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6colptrS640)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6colptrS640->$0 = _M0L6_2atmpS1465;
  _M0L6colptrS640->$1 = 0;
  _M0L6_2atmpS1464 = moonbit_empty_float_array;
  _M0L4valsS641
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS641)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L4valsS641->$0 = _M0L6_2atmpS1464;
  _M0L4valsS641->$1 = 0;
  _M0L7_2abindS642 = 0;
  _M0L1iS643 = _M0L7_2abindS642;
  while (1) {
    if (_M0L1iS643 < _M0L4rowsS563) {
      int32_t _M0L6_2atmpS1459;
      int32_t _M0L7_2abindS644;
      int32_t _M0L1jS645;
      int32_t _M0L6_2atmpS1462;
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS1459 = _M0MPC15array5Array6lengthGfE(_M0L4valsS641);
      #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0MPC15array5Array3setGiE(_M0L6rowptrS639, _M0L1iS643, _M0L6_2atmpS1459);
      _M0L7_2abindS644 = 0;
      _M0L1jS645 = _M0L7_2abindS644;
      while (1) {
        if (_M0L1jS645 < _M0L4colsS567) {
          struct _M0TPB5ArrayGfE* _M0L6_2atmpS1460;
          float _M0L1vS646;
          int32_t _M0L6_2atmpS1461;
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L6_2atmpS1460
          = _M0MPC15array5Array2atGRPB5ArrayGfEE(_M0L5denseS562, _M0L1iS643);
          #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
          _M0L1vS646
          = _M0MPC15array5Array2atGfE(_M0L6_2atmpS1460, _M0L1jS645);
          moonbit_decref_cycle_free(_M0L6_2atmpS1460);
          if (_M0L1vS646 != 0x0p+0f) {
            #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGiE(_M0L6colptrS640, _M0L1jS645);
            #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array4pushGfE(_M0L4valsS641, _M0L1vS646);
          }
          _M0L6_2atmpS1461 = _M0L1jS645 + 1;
          _M0L1jS645 = _M0L6_2atmpS1461;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1462 = _M0L1iS643 + 1;
      _M0L1iS643 = _M0L6_2atmpS1462;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L5denseS562);
    }
    break;
  }
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS1463 = _M0MPC15array5Array6lengthGfE(_M0L4valsS641);
  #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0MPC15array5Array3setGiE(_M0L6rowptrS639, _M0L4rowsS563, _M0L6_2atmpS1463);
  _block_1913
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_1913)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 54, 0);
  _block_1913->$0 = _M0L4rowsS563;
  _block_1913->$1 = _M0L4colsS567;
  _block_1913->$2 = _M0L6rowptrS639;
  _block_1913->$3 = _M0L6colptrS640;
  _block_1913->$4 = _M0L4valsS641;
  return _block_1913;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS560
) {
  struct _M0TUmmmmE* _M0L1sS559;
  uint64_t _M0L6_2atmpS1415;
  struct _M0TUmmmmE* _M0L1tS561;
  uint64_t _M0L6_2atmpS1411;
  uint64_t _M0L6_2atmpS1412;
  uint64_t _M0L6_2atmpS1413;
  uint64_t _M0L6_2atmpS1414;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_1914;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS559 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS560);
  _M0L6_2atmpS1415 = _M0L1sS559->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS561 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS1415);
  _M0L6_2atmpS1411 = _M0L1sS559->$0;
  _M0L6_2atmpS1412 = _M0L1sS559->$1;
  _M0L6_2atmpS1413 = _M0L1sS559->$2;
  moonbit_decref_cycle_free(_M0L1sS559);
  _M0L6_2atmpS1414 = _M0L1tS561->$0;
  moonbit_decref_cycle_free(_M0L1tS561);
  _block_1914
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_1914)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1914->$0 = _M0L6_2atmpS1411;
  _block_1914->$1 = _M0L6_2atmpS1412;
  _block_1914->$2 = _M0L6_2atmpS1413;
  _block_1914->$3 = _M0L6_2atmpS1414;
  return _block_1914;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS551) {
  uint64_t _M0L2s1S550;
  uint64_t _M0L2z1S552;
  uint64_t _M0L2s2S553;
  uint64_t _M0L2z2S554;
  uint64_t _M0L2s3S555;
  uint64_t _M0L2z3S556;
  uint64_t _M0L2s4S557;
  uint64_t _M0L2z4S558;
  struct _M0TUmmmmE* _block_1915;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S550 = _M0L4seedS551 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S552 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S550);
  _M0L2s2S553 = _M0L2s1S550 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S554 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S553);
  _M0L2s3S555 = _M0L2s2S553 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S556 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S555);
  _M0L2s4S557 = _M0L2s3S555 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S558 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S557);
  _block_1915 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_1915)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1915->$0 = _M0L2z1S552;
  _block_1915->$1 = _M0L2z2S554;
  _block_1915->$2 = _M0L2z3S556;
  _block_1915->$3 = _M0L2z4S558;
  return _block_1915;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS548) {
  uint64_t _M0L6_2atmpS1410;
  uint64_t _M0L6_2atmpS1409;
  uint64_t _M0L1zS547;
  uint64_t _M0L6_2atmpS1408;
  uint64_t _M0L6_2atmpS1407;
  uint64_t _M0L1zS549;
  uint64_t _M0L6_2atmpS1406;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1410 = _M0L1zS548 >> 30;
  _M0L6_2atmpS1409 = _M0L1zS548 ^ _M0L6_2atmpS1410;
  _M0L1zS547 = _M0L6_2atmpS1409 * 13787848793156543929ull;
  _M0L6_2atmpS1408 = _M0L1zS547 >> 27;
  _M0L6_2atmpS1407 = _M0L1zS547 ^ _M0L6_2atmpS1408;
  _M0L1zS549 = _M0L6_2atmpS1407 * 10723151780598845931ull;
  _M0L6_2atmpS1406 = _M0L1zS549 >> 31;
  return _M0L1zS549 ^ _M0L6_2atmpS1406;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS542
) {
  double _M0L2u1S541;
  double _M0L8u1__safeS543;
  double _M0L2u2S544;
  double _M0L6_2atmpS1405;
  double _M0L6_2atmpS1404;
  double _M0L1rS545;
  double _M0L5thetaS546;
  double _M0L6_2atmpS1403;
  double _M0L6_2atmpS1400;
  double _M0L6_2atmpS1402;
  double _M0L6_2atmpS1401;
  struct _M0TUddE* _block_1916;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S541 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS542);
  if (_M0L2u1S541 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS543 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS543 = _M0L2u1S541;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S544 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS542);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1405 = _M0FPC14math2ln(_M0L8u1__safeS543);
  _M0L6_2atmpS1404 = -0x1p+1 * _M0L6_2atmpS1405;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS545 = sqrt(_M0L6_2atmpS1404);
  _M0L5thetaS546 = 0x1.921fb54442d18p+2 * _M0L2u2S544;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1403 = _M0FPC14math3cos(_M0L5thetaS546);
  _M0L6_2atmpS1400 = _M0L1rS545 * _M0L6_2atmpS1403;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS1402 = _M0FPC14math3sin(_M0L5thetaS546);
  _M0L6_2atmpS1401 = _M0L1rS545 * _M0L6_2atmpS1402;
  _block_1916 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_1916)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1916->$0 = _M0L6_2atmpS1400;
  _block_1916->$1 = _M0L6_2atmpS1401;
  return _block_1916;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS539
) {
  uint64_t _M0L1uS538;
  uint64_t _M0L4bitsS540;
  double _M0L6_2atmpS1399;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS538 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS539);
  _M0L4bitsS540 = _M0L1uS538 >> 11;
  _M0L6_2atmpS1399 = (double)_M0L4bitsS540;
  return _M0L6_2atmpS1399 * 0x1p-53;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS536
) {
  uint32_t _M0L1uS535;
  uint32_t _M0L4bitsS537;
  double _M0L6_2atmpS1398;
  double _M0L6_2atmpS1397;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS535 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS536);
  _M0L4bitsS537 = _M0L1uS535 >> 8;
  _M0L6_2atmpS1398 = (double)_M0L4bitsS537;
  _M0L6_2atmpS1397 = _M0L6_2atmpS1398 * 0x1p-24;
  return (float)_M0L6_2atmpS1397;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS534
) {
  uint64_t _M0L1uS533;
  uint64_t _M0L6_2atmpS1396;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS533 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS534);
  _M0L6_2atmpS1396 = _M0L1uS533 >> 32;
  return (uint32_t)_M0L6_2atmpS1396;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS526
) {
  uint64_t _M0L2s0S525;
  uint64_t _M0L2s1S527;
  uint64_t _M0L2s2S528;
  uint64_t _M0L2s3S529;
  uint64_t _M0L3tmpS530;
  uint64_t _M0L6_2atmpS1395;
  uint64_t _M0L3resS531;
  uint64_t _M0L1tS532;
  uint64_t _M0L6_2atmpS1385;
  uint64_t _M0L6_2atmpS1386;
  uint64_t _M0L2s2S1388;
  uint64_t _M0L6_2atmpS1387;
  uint64_t _M0L2s3S1390;
  uint64_t _M0L6_2atmpS1389;
  uint64_t _M0L2s2S1392;
  uint64_t _M0L6_2atmpS1391;
  uint64_t _M0L2s3S1394;
  uint64_t _M0L6_2atmpS1393;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S525 = _M0L1rS526->$0;
  _M0L2s1S527 = _M0L1rS526->$1;
  _M0L2s2S528 = _M0L1rS526->$2;
  _M0L2s3S529 = _M0L1rS526->$3;
  _M0L3tmpS530 = _M0L2s0S525 + _M0L2s3S529;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1395 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS530, 23);
  _M0L3resS531 = _M0L6_2atmpS1395 + _M0L2s0S525;
  _M0L1tS532 = _M0L2s1S527 << 17;
  _M0L6_2atmpS1385 = _M0L2s2S528 ^ _M0L2s0S525;
  _M0L1rS526->$2 = _M0L6_2atmpS1385;
  _M0L6_2atmpS1386 = _M0L2s3S529 ^ _M0L2s1S527;
  _M0L1rS526->$3 = _M0L6_2atmpS1386;
  _M0L2s2S1388 = _M0L1rS526->$2;
  _M0L6_2atmpS1387 = _M0L2s1S527 ^ _M0L2s2S1388;
  _M0L1rS526->$1 = _M0L6_2atmpS1387;
  _M0L2s3S1390 = _M0L1rS526->$3;
  _M0L6_2atmpS1389 = _M0L2s0S525 ^ _M0L2s3S1390;
  _M0L1rS526->$0 = _M0L6_2atmpS1389;
  _M0L2s2S1392 = _M0L1rS526->$2;
  _M0L6_2atmpS1391 = _M0L2s2S1392 ^ _M0L1tS532;
  _M0L1rS526->$2 = _M0L6_2atmpS1391;
  _M0L2s3S1394 = _M0L1rS526->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1393 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S1394, 45);
  _M0L1rS526->$3 = _M0L6_2atmpS1393;
  return _M0L3resS531;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS523, int32_t _M0L1kS524) {
  uint64_t _M0L6_2atmpS1382;
  int32_t _M0L6_2atmpS1384;
  uint64_t _M0L6_2atmpS1383;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1382 = _M0L1xS523 << (_M0L1kS524 & 63);
  _M0L6_2atmpS1384 = 64 - _M0L1kS524;
  _M0L6_2atmpS1383 = _M0L1xS523 >> (_M0L6_2atmpS1384 & 63);
  return _M0L6_2atmpS1382 | _M0L6_2atmpS1383;
}

double _M0FPC14math2ln(double _M0L1xS509) {
  struct _M0TUdiE* _M0L7_2abindS510;
  double _M0L5_2af1S511;
  int32_t _M0L5_2akiS512;
  double _M0L1fS514;
  double _M0L1kS515;
  double _M0L6_2atmpS1375;
  double _M0L1sS516;
  double _M0L2s2S517;
  double _M0L2s4S518;
  double _M0L6_2atmpS1374;
  double _M0L6_2atmpS1373;
  double _M0L6_2atmpS1372;
  double _M0L6_2atmpS1371;
  double _M0L6_2atmpS1370;
  double _M0L6_2atmpS1369;
  double _M0L2t1S519;
  double _M0L6_2atmpS1368;
  double _M0L6_2atmpS1367;
  double _M0L6_2atmpS1366;
  double _M0L6_2atmpS1365;
  double _M0L2t2S520;
  double _M0L1rS521;
  double _M0L6_2atmpS1364;
  double _M0L4hfsqS522;
  double _M0L6_2atmpS1357;
  double _M0L6_2atmpS1363;
  double _M0L6_2atmpS1361;
  double _M0L6_2atmpS1362;
  double _M0L6_2atmpS1360;
  double _M0L6_2atmpS1359;
  double _M0L6_2atmpS1358;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS509 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS509)
      || _M0MPC16double6Double7is__inf(_M0L1xS509)
    ) {
      return _M0L1xS509;
    } else if (_M0L1xS509 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS510 = _M0FPC14math5frexp(_M0L1xS509);
  _M0L5_2af1S511 = _M0L7_2abindS510->$0;
  _M0L5_2akiS512 = _M0L7_2abindS510->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS510);
  if (_M0L5_2af1S511 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS1379 = _M0L5_2af1S511 * 0x1p+1;
    double _M0L6_2atmpS1376 = _M0L6_2atmpS1379 - 0x1p+0;
    int32_t _M0L6_2atmpS1378 = _M0L5_2akiS512 - 1;
    double _M0L6_2atmpS1377 = (double)_M0L6_2atmpS1378;
    _M0L1fS514 = _M0L6_2atmpS1376;
    _M0L1kS515 = _M0L6_2atmpS1377;
    goto join_513;
  } else {
    double _M0L6_2atmpS1380 = _M0L5_2af1S511 - 0x1p+0;
    double _M0L6_2atmpS1381 = (double)_M0L5_2akiS512;
    _M0L1fS514 = _M0L6_2atmpS1380;
    _M0L1kS515 = _M0L6_2atmpS1381;
    goto join_513;
  }
  join_513:;
  _M0L6_2atmpS1375 = 0x1p+1 + _M0L1fS514;
  _M0L1sS516 = _M0L1fS514 / _M0L6_2atmpS1375;
  _M0L2s2S517 = _M0L1sS516 * _M0L1sS516;
  _M0L2s4S518 = _M0L2s2S517 * _M0L2s2S517;
  _M0L6_2atmpS1374 = _M0L2s4S518 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS1373 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS1374;
  _M0L6_2atmpS1372 = _M0L2s4S518 * _M0L6_2atmpS1373;
  _M0L6_2atmpS1371 = 0x1.2492494229359p-2 + _M0L6_2atmpS1372;
  _M0L6_2atmpS1370 = _M0L2s4S518 * _M0L6_2atmpS1371;
  _M0L6_2atmpS1369 = 0x1.5555555555593p-1 + _M0L6_2atmpS1370;
  _M0L2t1S519 = _M0L2s2S517 * _M0L6_2atmpS1369;
  _M0L6_2atmpS1368 = _M0L2s4S518 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS1367 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS1368;
  _M0L6_2atmpS1366 = _M0L2s4S518 * _M0L6_2atmpS1367;
  _M0L6_2atmpS1365 = 0x1.999999997fa04p-2 + _M0L6_2atmpS1366;
  _M0L2t2S520 = _M0L2s4S518 * _M0L6_2atmpS1365;
  _M0L1rS521 = _M0L2t1S519 + _M0L2t2S520;
  _M0L6_2atmpS1364 = 0x1p-1 * _M0L1fS514;
  _M0L4hfsqS522 = _M0L6_2atmpS1364 * _M0L1fS514;
  _M0L6_2atmpS1357 = _M0L1kS515 * 0x1.62e42feep-1;
  _M0L6_2atmpS1363 = _M0L4hfsqS522 + _M0L1rS521;
  _M0L6_2atmpS1361 = _M0L1sS516 * _M0L6_2atmpS1363;
  _M0L6_2atmpS1362 = _M0L1kS515 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS1360 = _M0L6_2atmpS1361 + _M0L6_2atmpS1362;
  _M0L6_2atmpS1359 = _M0L4hfsqS522 - _M0L6_2atmpS1360;
  _M0L6_2atmpS1358 = _M0L6_2atmpS1359 - _M0L1fS514;
  return _M0L6_2atmpS1357 - _M0L6_2atmpS1358;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS502) {
  struct _M0TUdiE* _M0L7_2abindS503;
  double _M0L10_2anorm__fS504;
  int32_t _M0L6_2aexpS505;
  uint64_t _M0L1uS506;
  uint64_t _M0L6_2atmpS1356;
  uint64_t _M0L6_2atmpS1355;
  int32_t _M0L6_2atmpS1354;
  int32_t _M0L6_2atmpS1353;
  int32_t _M0L3expS507;
  uint64_t _M0L6_2atmpS1352;
  uint64_t _M0L6_2atmpS1351;
  uint64_t _M0L6_2atmpS1350;
  double _M0L4fracS508;
  struct _M0TUdiE* _block_1919;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS502 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS502)
    || _M0MPC16double6Double7is__nan(_M0L1fS502)
  ) {
    struct _M0TUdiE* _block_1918 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_1918)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_1918->$0 = _M0L1fS502;
    _block_1918->$1 = 0;
    return _block_1918;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS503 = _M0FPC14math9normalize(_M0L1fS502);
  _M0L10_2anorm__fS504 = _M0L7_2abindS503->$0;
  _M0L6_2aexpS505 = _M0L7_2abindS503->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS503);
  _M0L1uS506 = *(int64_t*)&_M0L10_2anorm__fS504;
  _M0L6_2atmpS1356 = _M0L1uS506 >> 52;
  _M0L6_2atmpS1355 = _M0L6_2atmpS1356 & 2047ull;
  _M0L6_2atmpS1354 = (int32_t)_M0L6_2atmpS1355;
  _M0L6_2atmpS1353 = _M0L6_2aexpS505 + _M0L6_2atmpS1354;
  _M0L3expS507 = _M0L6_2atmpS1353 - 1022;
  _M0L6_2atmpS1352 = ~9218868437227405312ull;
  _M0L6_2atmpS1351 = _M0L1uS506 & _M0L6_2atmpS1352;
  _M0L6_2atmpS1350 = _M0L6_2atmpS1351 | 4602678819172646912ull;
  _M0L4fracS508 = *(double*)&_M0L6_2atmpS1350;
  _block_1919 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_1919)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1919->$0 = _M0L4fracS508;
  _block_1919->$1 = _M0L3expS507;
  return _block_1919;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS501) {
  double _M0L6_2atmpS1347;
  struct _M0TUdiE* _block_1921;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS1347 = fabs(_M0L1fS501);
  if (_M0L6_2atmpS1347 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS1349 = (double)4503599627370496ll;
    double _M0L6_2atmpS1348 = _M0L1fS501 * _M0L6_2atmpS1349;
    struct _M0TUdiE* _block_1920 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_1920)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_1920->$0 = _M0L6_2atmpS1348;
    _block_1920->$1 = -52;
    return _block_1920;
  }
  _block_1921 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_1921)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1921->$0 = _M0L1fS501;
  _block_1921->$1 = 0;
  return _block_1921;
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS500) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS500 != _M0L4selfS500) {
    return 0;
  } else if (_M0L4selfS500 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS500 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS500;
  }
}

int32_t _M0MPC15array5Array5clearGfE(struct _M0TPB5ArrayGfE* _M0L4selfS499) {
  #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 579 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0MPC15array5Array28unsafe__truncate__to__lengthGfE(_M0L4selfS499, 0);
  return 0;
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS480,
  float _M0L4elemS482
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS479;
  int32_t _M0L1iS481;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS479 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS480);
  _M0L1iS481 = 0;
  while (1) {
    if (_M0L1iS481 < _M0L3lenS480) {
      float* _M0L3bufS1339 = _M0L3arrS479->$0;
      int32_t _M0L6_2atmpS1340;
      _M0L3bufS1339[_M0L1iS481] = _M0L4elemS482;
      _M0L6_2atmpS1340 = _M0L1iS481 + 1;
      _M0L1iS481 = _M0L6_2atmpS1340;
      continue;
    }
    break;
  }
  return _M0L3arrS479;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS485,
  int32_t _M0L4elemS487
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS484;
  int32_t _M0L1iS486;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS484 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS485);
  _M0L1iS486 = 0;
  while (1) {
    if (_M0L1iS486 < _M0L3lenS485) {
      uint8_t* _M0L3bufS1341 = _M0L3arrS484->$0;
      int32_t _M0L6_2atmpS1342;
      _M0L3bufS1341[_M0L1iS486] = _M0L4elemS487;
      _M0L6_2atmpS1342 = _M0L1iS486 + 1;
      _M0L1iS486 = _M0L6_2atmpS1342;
      continue;
    }
    break;
  }
  return _M0L3arrS484;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS490,
  int32_t _M0L4elemS492
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS489;
  int32_t _M0L1iS491;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS489 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS490);
  _M0L1iS491 = 0;
  while (1) {
    if (_M0L1iS491 < _M0L3lenS490) {
      int32_t* _M0L3bufS1343 = _M0L3arrS489->$0;
      int32_t _M0L6_2atmpS1344;
      _M0L3bufS1343[_M0L1iS491] = _M0L4elemS492;
      _M0L6_2atmpS1344 = _M0L1iS491 + 1;
      _M0L1iS491 = _M0L6_2atmpS1344;
      continue;
    }
    break;
  }
  return _M0L3arrS489;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array4makeGRPB5ArrayGfEE(
  int32_t _M0L3lenS495,
  struct _M0TPB5ArrayGfE* _M0L4elemS497
) {
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L3arrS494;
  int32_t _M0L1iS496;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS494
  = _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(_M0L3lenS495);
  _M0L1iS496 = 0;
  while (1) {
    if (_M0L1iS496 < _M0L3lenS495) {
      struct _M0TPB5ArrayGfE** _M0L3bufS1345 = _M0L3arrS494->$0;
      struct _M0TPB5ArrayGfE* _M0L6_2aoldS1820 =
        (struct _M0TPB5ArrayGfE*)_M0L3bufS1345[_M0L1iS496];
      int32_t _M0L6_2atmpS1346;
      moonbit_incref_cycle_free(_M0L4elemS497);
      if (_M0L6_2aoldS1820) {
        moonbit_decref_cycle_free(_M0L6_2aoldS1820);
      }
      _M0L3bufS1345[_M0L1iS496] = _M0L4elemS497;
      _M0L6_2atmpS1346 = _M0L1iS496 + 1;
      _M0L1iS496 = _M0L6_2atmpS1346;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS497);
    }
    break;
  }
  return _M0L3arrS494;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS464,
  int32_t _M0L5indexS465,
  float _M0L5valueS466
) {
  int32_t _M0L3lenS463;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS463 = _M0L4selfS464->$1;
  if (_M0L5indexS465 >= 0 && _M0L5indexS465 < _M0L3lenS463) {
    float* _M0L6_2atmpS1335;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1335 = _M0MPC15array5Array6bufferGfE(_M0L4selfS464);
    _M0L6_2atmpS1335[_M0L5indexS465] = _M0L5valueS466;
    moonbit_decref_cycle_free(_M0L6_2atmpS1335);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS468,
  int32_t _M0L5indexS469,
  int32_t _M0L5valueS470
) {
  int32_t _M0L3lenS467;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS467 = _M0L4selfS468->$1;
  if (_M0L5indexS469 >= 0 && _M0L5indexS469 < _M0L3lenS467) {
    int32_t* _M0L6_2atmpS1336;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1336 = _M0MPC15array5Array6bufferGiE(_M0L4selfS468);
    _M0L6_2atmpS1336[_M0L5indexS469] = _M0L5valueS470;
    moonbit_decref_cycle_free(_M0L6_2atmpS1336);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS472,
  int32_t _M0L5indexS473,
  int32_t _M0L5valueS474
) {
  int32_t _M0L3lenS471;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS471 = _M0L4selfS472->$1;
  if (_M0L5indexS473 >= 0 && _M0L5indexS473 < _M0L3lenS471) {
    uint8_t* _M0L6_2atmpS1337;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1337 = _M0MPC15array5Array6bufferGbE(_M0L4selfS472);
    _M0L6_2atmpS1337[_M0L5indexS473] = _M0L5valueS474;
    moonbit_decref_cycle_free(_M0L6_2atmpS1337);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS476,
  int32_t _M0L5indexS477,
  struct _M0TPB5ArrayGfE* _M0L5valueS478
) {
  int32_t _M0L3lenS475;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS475 = _M0L4selfS476->$1;
  if (_M0L5indexS477 >= 0 && _M0L5indexS477 < _M0L3lenS475) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1338;
    struct _M0TPB5ArrayGfE* _M0L6_2aoldS1821;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1338
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS476);
    _M0L6_2aoldS1821
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1338[_M0L5indexS477];
    if (_M0L6_2aoldS1821) {
      moonbit_decref_cycle_free(_M0L6_2aoldS1821);
    }
    _M0L6_2atmpS1338[_M0L5indexS477] = _M0L5valueS478;
    moonbit_decref_cycle_free(_M0L6_2atmpS1338);
  } else {
    moonbit_decref_cycle_free(_M0L5valueS478);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE* _M0L4selfS456) {
  int32_t _M0L3lenS455;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS455 = _M0L4selfS456->$1;
  if (_M0L3lenS455 == 0) {
    return (struct moonbit_object*)&moonbit_constant_constructor_0 + 1;
  } else {
    int32_t _M0L5indexS457 = _M0L3lenS455 - 1;
    float* _M0L3bufS1333 = _M0L4selfS456->$0;
    float _M0L1vS458 = (float)_M0L3bufS1333[_M0L5indexS457];
    void* _block_1926;
    _M0L4selfS456->$1 = _M0L5indexS457;
    _block_1926
    = (void*)moonbit_malloc(sizeof(struct _M0DTPC16option6OptionGfE4Some));
    Moonbit_object_header(_block_1926)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 1);
    ((struct _M0DTPC16option6OptionGfE4Some*)_block_1926)->$0 = _M0L1vS458;
    return _block_1926;
  }
}

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE* _M0L4selfS460) {
  int32_t _M0L3lenS459;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS459 = _M0L4selfS460->$1;
  if (_M0L3lenS459 == 0) {
    return 4294967296ll;
  } else {
    int32_t _M0L5indexS461 = _M0L3lenS459 - 1;
    int32_t* _M0L3bufS1334 = _M0L4selfS460->$0;
    int32_t _M0L1vS462 = (int32_t)_M0L3bufS1334[_M0L5indexS461];
    _M0L4selfS460->$1 = _M0L5indexS461;
    return (int64_t)_M0L1vS462;
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS441,
  int32_t _M0L5indexS442
) {
  int32_t _M0L3lenS440;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS440 = _M0L4selfS441->$1;
  if (_M0L5indexS442 >= 0 && _M0L5indexS442 < _M0L3lenS440) {
    uint8_t* _M0L6_2atmpS1328;
    int32_t _result_1927;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1328 = _M0MPC15array5Array6bufferGbE(_M0L4selfS441);
    _result_1927 = (int32_t)_M0L6_2atmpS1328[_M0L5indexS442];
    moonbit_decref_cycle_free(_M0L6_2atmpS1328);
    return _result_1927;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS444,
  int32_t _M0L5indexS445
) {
  int32_t _M0L3lenS443;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS443 = _M0L4selfS444->$1;
  if (_M0L5indexS445 >= 0 && _M0L5indexS445 < _M0L3lenS443) {
    float* _M0L6_2atmpS1329;
    float _result_1928;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1329 = _M0MPC15array5Array6bufferGfE(_M0L4selfS444);
    _result_1928 = (float)_M0L6_2atmpS1329[_M0L5indexS445];
    moonbit_decref_cycle_free(_M0L6_2atmpS1329);
    return _result_1928;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS447,
  int32_t _M0L5indexS448
) {
  int32_t _M0L3lenS446;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS446 = _M0L4selfS447->$1;
  if (_M0L5indexS448 >= 0 && _M0L5indexS448 < _M0L3lenS446) {
    int32_t* _M0L6_2atmpS1330;
    int32_t _result_1929;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1330 = _M0MPC15array5Array6bufferGiE(_M0L4selfS447);
    _result_1929 = (int32_t)_M0L6_2atmpS1330[_M0L5indexS448];
    moonbit_decref_cycle_free(_M0L6_2atmpS1330);
    return _result_1929;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS450,
  int32_t _M0L5indexS451
) {
  int32_t _M0L3lenS449;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS449 = _M0L4selfS450->$1;
  if (_M0L5indexS451 >= 0 && _M0L5indexS451 < _M0L3lenS449) {
    moonbit_string_t* _M0L6_2atmpS1331;
    moonbit_string_t _M0L6_2atmpS1822;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1331 = _M0MPC15array5Array6bufferGsE(_M0L4selfS450);
    _M0L6_2atmpS1822 = (moonbit_string_t)_M0L6_2atmpS1331[_M0L5indexS451];
    moonbit_incref_cycle_free(_M0L6_2atmpS1822);
    moonbit_decref_cycle_free(_M0L6_2atmpS1331);
    return _M0L6_2atmpS1822;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array2atGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS453,
  int32_t _M0L5indexS454
) {
  int32_t _M0L3lenS452;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS452 = _M0L4selfS453->$1;
  if (_M0L5indexS454 >= 0 && _M0L5indexS454 < _M0L3lenS452) {
    struct _M0TPB5ArrayGfE** _M0L6_2atmpS1332;
    struct _M0TPB5ArrayGfE* _M0L6_2atmpS1823;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1332
    = _M0MPC15array5Array6bufferGRPB5ArrayGfEE(_M0L4selfS453);
    _M0L6_2atmpS1823
    = (struct _M0TPB5ArrayGfE*)_M0L6_2atmpS1332[_M0L5indexS454];
    if (_M0L6_2atmpS1823) {
      moonbit_incref_cycle_free(_M0L6_2atmpS1823);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS1332);
    return _M0L6_2atmpS1823;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array28unsafe__truncate__to__lengthGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS439,
  int32_t _M0L8new__lenS438
) {
  int32_t _M0L3lenS1327;
  #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1327 = _M0L4selfS439->$1;
  if (_M0L8new__lenS438 <= _M0L3lenS1327) {
    _M0L4selfS439->$1 = _M0L8new__lenS438;
  } else {
    #line 180 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS437) {
  moonbit_string_t _M0L6_2atmpS1326;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1326 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS437);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1326);
  moonbit_decref_cycle_free(_M0L6_2atmpS1326);
  return 0;
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS436) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS436 > _M0FPB18double__max__value
         || _M0L4selfS436 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS435) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS435 != _M0L4selfS435;
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS431
) {
  float* _M0L6_2atmpS1322;
  struct _M0TPB5ArrayGfE* _block_1930;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1322 = (float*)moonbit_make_float_array_raw(_M0L3lenS431);
  _block_1930
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_1930)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_1930->$0 = _M0L6_2atmpS1322;
  _block_1930->$1 = _M0L3lenS431;
  return _block_1930;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS432
) {
  uint8_t* _M0L6_2atmpS1323;
  struct _M0TPB5ArrayGbE* _block_1931;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1323 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS432);
  _block_1931
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_1931)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 59, 0);
  _block_1931->$0 = _M0L6_2atmpS1323;
  _block_1931->$1 = _M0L3lenS432;
  return _block_1931;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS433
) {
  int32_t* _M0L6_2atmpS1324;
  struct _M0TPB5ArrayGiE* _block_1932;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1324 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS433);
  _block_1932
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_1932)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _block_1932->$0 = _M0L6_2atmpS1324;
  _block_1932->$1 = _M0L3lenS433;
  return _block_1932;
}

struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0MPC15array5Array20unsafe__make__uninitGRPB5ArrayGfEE(
  int32_t _M0L3lenS434
) {
  struct _M0TPB5ArrayGfE** _M0L6_2atmpS1325;
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _block_1933;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1325
  = (struct _M0TPB5ArrayGfE**)moonbit_make_ref_array(_M0L3lenS434, 0);
  _block_1933
  = (struct _M0TPB5ArrayGRPB5ArrayGfEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRPB5ArrayGfEE));
  Moonbit_object_header(_block_1933)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 62, 0);
  _block_1933->$0 = _M0L6_2atmpS1325;
  _block_1933->$1 = _M0L3lenS434;
  return _block_1933;
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS430) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS430, 10);
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS418,
  float _M0L5valueS420
) {
  int32_t _M0L3lenS1294;
  float* _M0L6_2atmpS1296;
  int32_t _M0L6_2atmpS1295;
  int32_t _M0L6lengthS419;
  float* _M0L3bufS1299;
  int32_t _M0L6_2atmpS1300;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1294 = _M0L4selfS418->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1296 = _M0MPC15array5Array6bufferGfE(_M0L4selfS418);
  _M0L6_2atmpS1295 = Moonbit_array_length(_M0L6_2atmpS1296);
  moonbit_decref_cycle_free(_M0L6_2atmpS1296);
  if (_M0L3lenS1294 == _M0L6_2atmpS1295) {
    int32_t _M0L3lenS1298 = _M0L4selfS418->$1;
    int32_t _M0L6_2atmpS1297 = _M0L3lenS1298 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS418, _M0L6_2atmpS1297);
  }
  _M0L6lengthS419 = _M0L4selfS418->$1;
  _M0L3bufS1299 = _M0L4selfS418->$0;
  _M0L3bufS1299[_M0L6lengthS419] = _M0L5valueS420;
  _M0L6_2atmpS1300 = _M0L6lengthS419 + 1;
  _M0L4selfS418->$1 = _M0L6_2atmpS1300;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS421,
  int32_t _M0L5valueS423
) {
  int32_t _M0L3lenS1301;
  int32_t* _M0L6_2atmpS1303;
  int32_t _M0L6_2atmpS1302;
  int32_t _M0L6lengthS422;
  int32_t* _M0L3bufS1306;
  int32_t _M0L6_2atmpS1307;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1301 = _M0L4selfS421->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1303 = _M0MPC15array5Array6bufferGiE(_M0L4selfS421);
  _M0L6_2atmpS1302 = Moonbit_array_length(_M0L6_2atmpS1303);
  moonbit_decref_cycle_free(_M0L6_2atmpS1303);
  if (_M0L3lenS1301 == _M0L6_2atmpS1302) {
    int32_t _M0L3lenS1305 = _M0L4selfS421->$1;
    int32_t _M0L6_2atmpS1304 = _M0L3lenS1305 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS421, _M0L6_2atmpS1304);
  }
  _M0L6lengthS422 = _M0L4selfS421->$1;
  _M0L3bufS1306 = _M0L4selfS421->$0;
  _M0L3bufS1306[_M0L6lengthS422] = _M0L5valueS423;
  _M0L6_2atmpS1307 = _M0L6lengthS422 + 1;
  _M0L4selfS421->$1 = _M0L6_2atmpS1307;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS424,
  moonbit_string_t _M0L5valueS426
) {
  int32_t _M0L3lenS1308;
  moonbit_string_t* _M0L6_2atmpS1310;
  int32_t _M0L6_2atmpS1309;
  int32_t _M0L6lengthS425;
  moonbit_string_t* _M0L3bufS1313;
  moonbit_string_t _M0L6_2aoldS1824;
  int32_t _M0L6_2atmpS1314;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1308 = _M0L4selfS424->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1310 = _M0MPC15array5Array6bufferGsE(_M0L4selfS424);
  _M0L6_2atmpS1309 = Moonbit_array_length(_M0L6_2atmpS1310);
  moonbit_decref_cycle_free(_M0L6_2atmpS1310);
  if (_M0L3lenS1308 == _M0L6_2atmpS1309) {
    int32_t _M0L3lenS1312 = _M0L4selfS424->$1;
    int32_t _M0L6_2atmpS1311 = _M0L3lenS1312 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS424, _M0L6_2atmpS1311);
  }
  _M0L6lengthS425 = _M0L4selfS424->$1;
  _M0L3bufS1313 = _M0L4selfS424->$0;
  _M0L6_2aoldS1824 = (moonbit_string_t)_M0L3bufS1313[_M0L6lengthS425];
  moonbit_decref_cycle_free(_M0L6_2aoldS1824);
  _M0L3bufS1313[_M0L6lengthS425] = _M0L5valueS426;
  _M0L6_2atmpS1314 = _M0L6lengthS425 + 1;
  _M0L4selfS424->$1 = _M0L6_2atmpS1314;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS427,
  struct _M0TUsiE* _M0L5valueS429
) {
  int32_t _M0L3lenS1315;
  struct _M0TUsiE** _M0L6_2atmpS1317;
  int32_t _M0L6_2atmpS1316;
  int32_t _M0L6lengthS428;
  struct _M0TUsiE** _M0L3bufS1320;
  struct _M0TUsiE* _M0L6_2aoldS1825;
  int32_t _M0L6_2atmpS1321;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1315 = _M0L4selfS427->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1317 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS427);
  _M0L6_2atmpS1316 = Moonbit_array_length(_M0L6_2atmpS1317);
  moonbit_decref_cycle_free(_M0L6_2atmpS1317);
  if (_M0L3lenS1315 == _M0L6_2atmpS1316) {
    int32_t _M0L3lenS1319 = _M0L4selfS427->$1;
    int32_t _M0L6_2atmpS1318 = _M0L3lenS1319 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS427, _M0L6_2atmpS1318);
  }
  _M0L6lengthS428 = _M0L4selfS427->$1;
  _M0L3bufS1320 = _M0L4selfS427->$0;
  _M0L6_2aoldS1825 = (struct _M0TUsiE*)_M0L3bufS1320[_M0L6lengthS428];
  if (_M0L6_2aoldS1825) {
    moonbit_decref_cycle_free(_M0L6_2aoldS1825);
  }
  _M0L3bufS1320[_M0L6lengthS428] = _M0L5valueS429;
  _M0L6_2atmpS1321 = _M0L6lengthS428 + 1;
  _M0L4selfS427->$1 = _M0L6_2atmpS1321;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS403,
  int32_t _M0L8requiredS405
) {
  int32_t _M0L8old__capS402;
  int32_t _M0L3lenS1290;
  int32_t _M0L8new__capS404;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS402 = _M0MPC15array5Array8capacityGfE(_M0L4selfS403);
  _M0L3lenS1290 = _M0L4selfS403->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS404
  = _M0FPB23array__growth__capacity(_M0L8old__capS402, _M0L3lenS1290, _M0L8requiredS405);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS403, _M0L8new__capS404);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS407,
  int32_t _M0L8requiredS409
) {
  int32_t _M0L8old__capS406;
  int32_t _M0L3lenS1291;
  int32_t _M0L8new__capS408;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS406 = _M0MPC15array5Array8capacityGiE(_M0L4selfS407);
  _M0L3lenS1291 = _M0L4selfS407->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS408
  = _M0FPB23array__growth__capacity(_M0L8old__capS406, _M0L3lenS1291, _M0L8requiredS409);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS407, _M0L8new__capS408);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS411,
  int32_t _M0L8requiredS413
) {
  int32_t _M0L8old__capS410;
  int32_t _M0L3lenS1292;
  int32_t _M0L8new__capS412;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS410 = _M0MPC15array5Array8capacityGsE(_M0L4selfS411);
  _M0L3lenS1292 = _M0L4selfS411->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS412
  = _M0FPB23array__growth__capacity(_M0L8old__capS410, _M0L3lenS1292, _M0L8requiredS413);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS411, _M0L8new__capS412);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS415,
  int32_t _M0L8requiredS417
) {
  int32_t _M0L8old__capS414;
  int32_t _M0L3lenS1293;
  int32_t _M0L8new__capS416;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS414 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS415);
  _M0L3lenS1293 = _M0L4selfS415->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS416
  = _M0FPB23array__growth__capacity(_M0L8old__capS414, _M0L3lenS1293, _M0L8requiredS417);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS415, _M0L8new__capS416);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS379,
  int32_t _M0L13new__capacityS382
) {
  float* _M0L8old__bufS378;
  int32_t _M0L3lenS380;
  int32_t _M0L9copy__lenS381;
  float* _M0L8new__bufS383;
  float* _M0L6_2aoldS1826;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS378 = _M0L4selfS379->$0;
  _M0L3lenS380 = _M0L4selfS379->$1;
  if (_M0L3lenS380 < _M0L13new__capacityS382) {
    _M0L9copy__lenS381 = _M0L3lenS380;
  } else {
    _M0L9copy__lenS381 = _M0L13new__capacityS382;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS378);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS383
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS378, _M0L13new__capacityS382, _M0L9copy__lenS381, 0, 0);
  _M0L6_2aoldS1826 = _M0L4selfS379->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1826);
  _M0L4selfS379->$0 = _M0L8new__bufS383;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS385,
  int32_t _M0L13new__capacityS388
) {
  int32_t* _M0L8old__bufS384;
  int32_t _M0L3lenS386;
  int32_t _M0L9copy__lenS387;
  int32_t* _M0L8new__bufS389;
  int32_t* _M0L6_2aoldS1827;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS384 = _M0L4selfS385->$0;
  _M0L3lenS386 = _M0L4selfS385->$1;
  if (_M0L3lenS386 < _M0L13new__capacityS388) {
    _M0L9copy__lenS387 = _M0L3lenS386;
  } else {
    _M0L9copy__lenS387 = _M0L13new__capacityS388;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS384);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS389
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS384, _M0L13new__capacityS388, _M0L9copy__lenS387, 0, 0);
  _M0L6_2aoldS1827 = _M0L4selfS385->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1827);
  _M0L4selfS385->$0 = _M0L8new__bufS389;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS391,
  int32_t _M0L13new__capacityS394
) {
  moonbit_string_t* _M0L8old__bufS390;
  int32_t _M0L3lenS392;
  int32_t _M0L9copy__lenS393;
  moonbit_string_t* _M0L8new__bufS395;
  moonbit_string_t* _M0L6_2aoldS1828;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS390 = _M0L4selfS391->$0;
  _M0L3lenS392 = _M0L4selfS391->$1;
  if (_M0L3lenS392 < _M0L13new__capacityS394) {
    _M0L9copy__lenS393 = _M0L3lenS392;
  } else {
    _M0L9copy__lenS393 = _M0L13new__capacityS394;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS390);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS395
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS390, _M0L13new__capacityS394, _M0L9copy__lenS393, 0, 0);
  _M0L6_2aoldS1828 = _M0L4selfS391->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1828);
  _M0L4selfS391->$0 = _M0L8new__bufS395;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS397,
  int32_t _M0L13new__capacityS400
) {
  struct _M0TUsiE** _M0L8old__bufS396;
  int32_t _M0L3lenS398;
  int32_t _M0L9copy__lenS399;
  struct _M0TUsiE** _M0L8new__bufS401;
  struct _M0TUsiE** _M0L6_2aoldS1829;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS396 = _M0L4selfS397->$0;
  _M0L3lenS398 = _M0L4selfS397->$1;
  if (_M0L3lenS398 < _M0L13new__capacityS400) {
    _M0L9copy__lenS399 = _M0L3lenS398;
  } else {
    _M0L9copy__lenS399 = _M0L13new__capacityS400;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS396);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS401
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS396, _M0L13new__capacityS400, _M0L9copy__lenS399, 0, 0);
  _M0L6_2aoldS1829 = _M0L4selfS397->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1829);
  _M0L4selfS397->$0 = _M0L8new__bufS401;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS374
) {
  float* _M0L6_2atmpS1286;
  int32_t _result_1934;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1286 = _M0MPC15array5Array6bufferGfE(_M0L4selfS374);
  _result_1934 = Moonbit_array_length(_M0L6_2atmpS1286);
  moonbit_decref_cycle_free(_M0L6_2atmpS1286);
  return _result_1934;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS375
) {
  int32_t* _M0L6_2atmpS1287;
  int32_t _result_1935;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1287 = _M0MPC15array5Array6bufferGiE(_M0L4selfS375);
  _result_1935 = Moonbit_array_length(_M0L6_2atmpS1287);
  moonbit_decref_cycle_free(_M0L6_2atmpS1287);
  return _result_1935;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS376
) {
  moonbit_string_t* _M0L6_2atmpS1288;
  int32_t _result_1936;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1288 = _M0MPC15array5Array6bufferGsE(_M0L4selfS376);
  _result_1936 = Moonbit_array_length(_M0L6_2atmpS1288);
  moonbit_decref_cycle_free(_M0L6_2atmpS1288);
  return _result_1936;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS377
) {
  struct _M0TUsiE** _M0L6_2atmpS1289;
  int32_t _result_1937;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1289 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS377);
  _result_1937 = Moonbit_array_length(_M0L6_2atmpS1289);
  moonbit_decref_cycle_free(_M0L6_2atmpS1289);
  return _result_1937;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS370,
  int32_t _M0L3lenS368,
  int32_t _M0L8requiredS367
) {
  int32_t _M0L5startS369;
  int32_t _M0L5spaceS371;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS367 < _M0L3lenS368) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_10.data);
  }
  if (_M0L7currentS370 == 0) {
    _M0L5startS369 = 8;
  } else {
    _M0L5startS369 = _M0L7currentS370;
  }
  _M0L5spaceS371 = _M0L5startS369;
  while (1) {
    if (_M0L5spaceS371 < _M0L8requiredS367) {
      int32_t _M0L4nextS372 = _M0L5spaceS371 * 2;
      if (_M0L4nextS372 <= _M0L5spaceS371) {
        return _M0L8requiredS367;
      }
      _M0L5spaceS371 = _M0L4nextS372;
      continue;
    } else {
      return _M0L5spaceS371;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS365) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS365->$1;
}

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE* _M0L4selfS366) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS366->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS359) {
  float* _M0L8_2afieldS1830;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1830 = _M0L4selfS359->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1830);
  return _M0L8_2afieldS1830;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS360) {
  uint8_t* _M0L8_2afieldS1831;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1831 = _M0L4selfS360->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1831);
  return _M0L8_2afieldS1831;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS361) {
  int32_t* _M0L8_2afieldS1832;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1832 = _M0L4selfS361->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1832);
  return _M0L8_2afieldS1832;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS362
) {
  moonbit_string_t* _M0L8_2afieldS1833;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1833 = _M0L4selfS362->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1833);
  return _M0L8_2afieldS1833;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS363
) {
  struct _M0TUsiE** _M0L8_2afieldS1834;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1834 = _M0L4selfS363->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1834);
  return _M0L8_2afieldS1834;
}

struct _M0TPB5ArrayGfE** _M0MPC15array5Array6bufferGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGRPB5ArrayGfEE* _M0L4selfS364
) {
  struct _M0TPB5ArrayGfE** _M0L8_2afieldS1835;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1835 = _M0L4selfS364->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1835);
  return _M0L8_2afieldS1835;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS358
) {
  #line 220 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref_cycle_free(_M0L4selfS358);
  return _M0L4selfS358;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS357,
  struct _M0TPC16string10StringView _M0L3strS355
) {
  int32_t _M0L3endS1284;
  int32_t _M0L5startS1285;
  int32_t _M0L8str__lenS354;
  int32_t _M0L3lenS1283;
  int32_t _M0L8requiredS356;
  uint16_t* _M0L4dataS1276;
  int32_t _M0L6_2atmpS1275;
  int32_t _if__result_1939;
  uint16_t* _M0L4dataS1277;
  int32_t _M0L3lenS1278;
  moonbit_string_t _M0L6_2atmpS1279;
  int32_t _M0L6_2atmpS1280;
  int32_t _M0L3lenS1282;
  int32_t _M0L6_2atmpS1281;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1284 = _M0L3strS355.$2;
  _M0L5startS1285 = _M0L3strS355.$1;
  _M0L8str__lenS354 = _M0L3endS1284 - _M0L5startS1285;
  if (_M0L8str__lenS354 == 0) {
    return 0;
  }
  _M0L3lenS1283 = _M0L4selfS357->$1;
  _M0L8requiredS356 = _M0L3lenS1283 + _M0L8str__lenS354;
  _M0L4dataS1276 = _M0L4selfS357->$0;
  _M0L6_2atmpS1275 = Moonbit_array_length(_M0L4dataS1276);
  if (_M0L8requiredS356 > _M0L6_2atmpS1275) {
    _if__result_1939 = 1;
  } else {
    int32_t _M0L3lenS1274 = _M0L4selfS357->$1;
    _if__result_1939 = _M0L8requiredS356 < _M0L3lenS1274;
  }
  if (_if__result_1939) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS357, _M0L8requiredS356);
  }
  _M0L4dataS1277 = _M0L4selfS357->$0;
  _M0L3lenS1278 = _M0L4selfS357->$1;
  moonbit_incref_cycle_free(_M0L4dataS1277);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1279 = _M0MPC16string10StringView4data(_M0L3strS355);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1280 = _M0MPC16string10StringView13start__offset(_M0L3strS355);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1277, _M0L3lenS1278, _M0L6_2atmpS1279, _M0L6_2atmpS1280, _M0L8str__lenS354);
  moonbit_decref_cycle_free(_M0L4dataS1277);
  moonbit_decref_cycle_free(_M0L6_2atmpS1279);
  _M0L3lenS1282 = _M0L4selfS357->$1;
  _M0L6_2atmpS1281 = _M0L3lenS1282 + _M0L8str__lenS354;
  _M0L4selfS357->$1 = _M0L6_2atmpS1281;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS351,
  int32_t _M0L5startS349,
  int32_t _M0L3endS350
) {
  int32_t _if__result_1940;
  int32_t _M0L3lenS352;
  int32_t _M0L6_2atmpS1273;
  moonbit_bytes_t _M0L5bytesS353;
  moonbit_bytes_t _M0L6_2atmpS1272;
  moonbit_string_t _result_1941;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS349 == 0) {
    int32_t _M0L6_2atmpS1271 = Moonbit_array_length(_M0L3strS351);
    _if__result_1940 = _M0L3endS350 == _M0L6_2atmpS1271;
  } else {
    _if__result_1940 = 0;
  }
  if (_if__result_1940) {
    moonbit_incref_cycle_free(_M0L3strS351);
    return _M0L3strS351;
  }
  _M0L3lenS352 = _M0L3endS350 - _M0L5startS349;
  _M0L6_2atmpS1273 = _M0L3lenS352 * 2;
  _M0L5bytesS353 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1273, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS353, 0, _M0L3strS351, _M0L5startS349, _M0L3lenS352);
  _M0L6_2atmpS1272 = _M0L5bytesS353;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_1941
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1272, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1272);
  return _result_1941;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS344,
  int32_t _M0L6offsetS348,
  int64_t _M0L6lengthS346
) {
  int32_t _M0L3lenS343;
  int32_t _M0L6lengthS345;
  int32_t _if__result_1942;
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L3lenS343 = Moonbit_array_length(_M0L4selfS344);
  if (_M0L6lengthS346 == 4294967296ll) {
    _M0L6lengthS345 = _M0L3lenS343 - _M0L6offsetS348;
  } else {
    int64_t _M0L7_2aSomeS347 = _M0L6lengthS346;
    _M0L6lengthS345 = (int32_t)_M0L7_2aSomeS347;
  }
  if (_M0L6offsetS348 >= 0) {
    if (_M0L6lengthS345 >= 0) {
      int32_t _M0L6_2atmpS1270 = _M0L6offsetS348 + _M0L6lengthS345;
      _if__result_1942 = _M0L6_2atmpS1270 <= _M0L3lenS343;
    } else {
      _if__result_1942 = 0;
    }
  } else {
    _if__result_1942 = 0;
  }
  if (_if__result_1942) {
    moonbit_incref_cycle_free(_M0L4selfS344);
    #line 85 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    return _M0FPB19unsafe__sub__string(_M0L4selfS344, _M0L6offsetS348, _M0L6lengthS345);
  } else {
    #line 84 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array10FixedArray18blit__from__string(
  moonbit_bytes_t _M0L4selfS335,
  int32_t _M0L13bytes__offsetS330,
  moonbit_string_t _M0L3strS337,
  int32_t _M0L11str__offsetS333,
  int32_t _M0L6lengthS331
) {
  int32_t _M0L6_2atmpS1269;
  int32_t _M0L6_2atmpS1268;
  int32_t _M0L2e1S329;
  int32_t _M0L6_2atmpS1267;
  int32_t _M0L2e2S332;
  int32_t _M0L4len1S334;
  int32_t _M0L4len2S336;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1269 = _M0L6lengthS331 * 2;
  _M0L6_2atmpS1268 = _M0L13bytes__offsetS330 + _M0L6_2atmpS1269;
  _M0L2e1S329 = _M0L6_2atmpS1268 - 1;
  _M0L6_2atmpS1267 = _M0L11str__offsetS333 + _M0L6lengthS331;
  _M0L2e2S332 = _M0L6_2atmpS1267 - 1;
  _M0L4len1S334 = Moonbit_array_length(_M0L4selfS335);
  _M0L4len2S336 = Moonbit_array_length(_M0L3strS337);
  if (
    _M0L6lengthS331 >= 0
    && _M0L13bytes__offsetS330 >= 0
    && _M0L2e1S329 < _M0L4len1S334
    && _M0L11str__offsetS333 >= 0
    && _M0L2e2S332 < _M0L4len2S336
  ) {
    int32_t _M0L16end__str__offsetS338 =
      _M0L11str__offsetS333 + _M0L6lengthS331;
    int32_t _M0L1iS339 = _M0L11str__offsetS333;
    int32_t _M0L1jS340 = _M0L13bytes__offsetS330;
    while (1) {
      if (_M0L1iS339 < _M0L16end__str__offsetS338) {
        int32_t _M0L6_2atmpS1264 = _M0L3strS337[_M0L1iS339];
        int32_t _M0L6_2atmpS1263 = (int32_t)_M0L6_2atmpS1264;
        uint32_t _M0L1cS341 = *(uint32_t*)&_M0L6_2atmpS1263;
        uint32_t _M0L6_2atmpS1259 = _M0L1cS341 & 255u;
        int32_t _M0L6_2atmpS1258;
        int32_t _M0L6_2atmpS1260;
        uint32_t _M0L6_2atmpS1262;
        int32_t _M0L6_2atmpS1261;
        int32_t _M0L6_2atmpS1265;
        int32_t _M0L6_2atmpS1266;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1258 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1259);
        if (
          _M0L1jS340 < 0 || _M0L1jS340 >= Moonbit_array_length(_M0L4selfS335)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS335[_M0L1jS340] = _M0L6_2atmpS1258;
        _M0L6_2atmpS1260 = _M0L1jS340 + 1;
        _M0L6_2atmpS1262 = _M0L1cS341 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1261 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1262);
        if (
          _M0L6_2atmpS1260 < 0
          || _M0L6_2atmpS1260 >= Moonbit_array_length(_M0L4selfS335)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS335[_M0L6_2atmpS1260] = _M0L6_2atmpS1261;
        _M0L6_2atmpS1265 = _M0L1iS339 + 1;
        _M0L6_2atmpS1266 = _M0L1jS340 + 2;
        _M0L1iS339 = _M0L6_2atmpS1265;
        _M0L1jS340 = _M0L6_2atmpS1266;
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

int32_t _M0MPC14uint4UInt8to__byte(uint32_t _M0L4selfS328) {
  int32_t _M0L6_2atmpS1257;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1257 = *(int32_t*)&_M0L4selfS328;
  return _M0L6_2atmpS1257 & 0xff;
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS312,
  int32_t _M0L5radixS311
) {
  int32_t _M0L12is__negativeS313;
  uint32_t _M0L3numS314;
  uint16_t* _M0L6bufferS315;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS311 < 2 || _M0L5radixS311 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_11.data);
  }
  if (_M0L4selfS312 == 0) {
    return (moonbit_string_t)moonbit_string_literal_12.data;
  }
  _M0L12is__negativeS313 = _M0L4selfS312 < 0;
  if (_M0L12is__negativeS313) {
    int32_t _M0L6_2atmpS1256 = -_M0L4selfS312;
    _M0L3numS314 = *(uint32_t*)&_M0L6_2atmpS1256;
  } else {
    _M0L3numS314 = *(uint32_t*)&_M0L4selfS312;
  }
  switch (_M0L5radixS311) {
    case 10: {
      int32_t _M0L10digit__lenS316;
      int32_t _M0L6_2atmpS1253;
      int32_t _M0L10total__lenS317;
      uint16_t* _M0L6bufferS318;
      int32_t _M0L12digit__startS319;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS316 = _M0FPB12dec__count32(_M0L3numS314);
      if (_M0L12is__negativeS313) {
        _M0L6_2atmpS1253 = 1;
      } else {
        _M0L6_2atmpS1253 = 0;
      }
      _M0L10total__lenS317 = _M0L10digit__lenS316 + _M0L6_2atmpS1253;
      _M0L6bufferS318
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS317, 0);
      if (_M0L12is__negativeS313) {
        _M0L12digit__startS319 = 1;
      } else {
        _M0L12digit__startS319 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS318, _M0L3numS314, _M0L12digit__startS319, _M0L10total__lenS317);
      _M0L6bufferS315 = _M0L6bufferS318;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS320;
      int32_t _M0L6_2atmpS1254;
      int32_t _M0L10total__lenS321;
      uint16_t* _M0L6bufferS322;
      int32_t _M0L12digit__startS323;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS320 = _M0FPB12hex__count32(_M0L3numS314);
      if (_M0L12is__negativeS313) {
        _M0L6_2atmpS1254 = 1;
      } else {
        _M0L6_2atmpS1254 = 0;
      }
      _M0L10total__lenS321 = _M0L10digit__lenS320 + _M0L6_2atmpS1254;
      _M0L6bufferS322
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS321, 0);
      if (_M0L12is__negativeS313) {
        _M0L12digit__startS323 = 1;
      } else {
        _M0L12digit__startS323 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS322, _M0L3numS314, _M0L12digit__startS323, _M0L10total__lenS321);
      _M0L6bufferS315 = _M0L6bufferS322;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS324;
      int32_t _M0L6_2atmpS1255;
      int32_t _M0L10total__lenS325;
      uint16_t* _M0L6bufferS326;
      int32_t _M0L12digit__startS327;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS324
      = _M0FPB14radix__count32(_M0L3numS314, _M0L5radixS311);
      if (_M0L12is__negativeS313) {
        _M0L6_2atmpS1255 = 1;
      } else {
        _M0L6_2atmpS1255 = 0;
      }
      _M0L10total__lenS325 = _M0L10digit__lenS324 + _M0L6_2atmpS1255;
      _M0L6bufferS326
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS325, 0);
      if (_M0L12is__negativeS313) {
        _M0L12digit__startS327 = 1;
      } else {
        _M0L12digit__startS327 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS326, _M0L3numS314, _M0L12digit__startS327, _M0L10total__lenS325, _M0L5radixS311);
      _M0L6bufferS315 = _M0L6bufferS326;
      break;
    }
  }
  if (_M0L12is__negativeS313) {
    _M0L6bufferS315[0] = 45;
  }
  return _M0L6bufferS315;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS305,
  int32_t _M0L5radixS307
) {
  uint32_t _M0L4baseS306;
  uint32_t _M0L3numS308;
  int32_t _M0L5countS309;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS305 == 0u) {
    return 1;
  }
  _M0L4baseS306 = *(uint32_t*)&_M0L5radixS307;
  _M0L3numS308 = _M0L5valueS305;
  _M0L5countS309 = 0;
  while (1) {
    if (_M0L3numS308 > 0u) {
      uint32_t _M0L6_2atmpS1251 = _M0L3numS308 / _M0L4baseS306;
      int32_t _M0L6_2atmpS1252 = _M0L5countS309 + 1;
      _M0L3numS308 = _M0L6_2atmpS1251;
      _M0L5countS309 = _M0L6_2atmpS1252;
      continue;
    } else {
      return _M0L5countS309;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS303) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS303 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS304;
    int32_t _M0L6_2atmpS1250;
    int32_t _M0L6_2atmpS1249;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS304 = moonbit_clz32(_M0L5valueS303);
    _M0L6_2atmpS1250 = 31 - _M0L14leading__zerosS304;
    _M0L6_2atmpS1249 = _M0L6_2atmpS1250 / 4;
    return _M0L6_2atmpS1249 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS302) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS302 >= 100000u) {
    if (_M0L5valueS302 >= 10000000u) {
      if (_M0L5valueS302 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS302 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS302 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS302 >= 1000u) {
    if (_M0L5valueS302 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS302 >= 100u) {
    return 3;
  } else if (_M0L5valueS302 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS288,
  uint32_t _M0L3numS300,
  int32_t _M0L12digit__startS289,
  int32_t _M0L10total__lenS301
) {
  int32_t _M0L6_2atmpS1248;
  uint32_t _M0L3numS278;
  int32_t _M0L6offsetS279;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1248 = _M0L10total__lenS301 - _M0L12digit__startS289;
  _M0L3numS278 = _M0L3numS300;
  _M0L6offsetS279 = _M0L6_2atmpS1248;
  while (1) {
    if (_M0L3numS278 >= 10000u) {
      uint32_t _M0L1tS280 = _M0L3numS278 / 10000u;
      uint32_t _M0L6_2atmpS1225 = _M0L3numS278 % 10000u;
      int32_t _M0L1rS281 = *(int32_t*)&_M0L6_2atmpS1225;
      int32_t _M0L2d1S282 = _M0L1rS281 / 100;
      int32_t _M0L2d2S283 = _M0L1rS281 % 100;
      int32_t _M0L6_2atmpS1224 = _M0L2d1S282 / 10;
      int32_t _M0L6_2atmpS1223 = 48 + _M0L6_2atmpS1224;
      int32_t _M0L6d1__hiS284 = (uint16_t)_M0L6_2atmpS1223;
      int32_t _M0L6_2atmpS1222 = _M0L2d1S282 % 10;
      int32_t _M0L6_2atmpS1221 = 48 + _M0L6_2atmpS1222;
      int32_t _M0L6d1__loS285 = (uint16_t)_M0L6_2atmpS1221;
      int32_t _M0L6_2atmpS1220 = _M0L2d2S283 / 10;
      int32_t _M0L6_2atmpS1219 = 48 + _M0L6_2atmpS1220;
      int32_t _M0L6d2__hiS286 = (uint16_t)_M0L6_2atmpS1219;
      int32_t _M0L6_2atmpS1218 = _M0L2d2S283 % 10;
      int32_t _M0L6_2atmpS1217 = 48 + _M0L6_2atmpS1218;
      int32_t _M0L6d2__loS287 = (uint16_t)_M0L6_2atmpS1217;
      int32_t _M0L6_2atmpS1209 = _M0L12digit__startS289 + _M0L6offsetS279;
      int32_t _M0L6_2atmpS1208 = _M0L6_2atmpS1209 - 4;
      int32_t _M0L6_2atmpS1211;
      int32_t _M0L6_2atmpS1210;
      int32_t _M0L6_2atmpS1213;
      int32_t _M0L6_2atmpS1212;
      int32_t _M0L6_2atmpS1215;
      int32_t _M0L6_2atmpS1214;
      int32_t _M0L6_2atmpS1216;
      _M0L6bufferS288[_M0L6_2atmpS1208] = _M0L6d1__hiS284;
      _M0L6_2atmpS1211 = _M0L12digit__startS289 + _M0L6offsetS279;
      _M0L6_2atmpS1210 = _M0L6_2atmpS1211 - 3;
      _M0L6bufferS288[_M0L6_2atmpS1210] = _M0L6d1__loS285;
      _M0L6_2atmpS1213 = _M0L12digit__startS289 + _M0L6offsetS279;
      _M0L6_2atmpS1212 = _M0L6_2atmpS1213 - 2;
      _M0L6bufferS288[_M0L6_2atmpS1212] = _M0L6d2__hiS286;
      _M0L6_2atmpS1215 = _M0L12digit__startS289 + _M0L6offsetS279;
      _M0L6_2atmpS1214 = _M0L6_2atmpS1215 - 1;
      _M0L6bufferS288[_M0L6_2atmpS1214] = _M0L6d2__loS287;
      _M0L6_2atmpS1216 = _M0L6offsetS279 - 4;
      _M0L3numS278 = _M0L1tS280;
      _M0L6offsetS279 = _M0L6_2atmpS1216;
      continue;
    } else {
      int32_t _M0L6_2atmpS1247 = *(int32_t*)&_M0L3numS278;
      int32_t _M0L9remainingS291 = _M0L6_2atmpS1247;
      int32_t _M0L6offsetS292 = _M0L6offsetS279;
      while (1) {
        if (_M0L9remainingS291 >= 100) {
          int32_t _M0L1tS293 = _M0L9remainingS291 / 100;
          int32_t _M0L1dS294 = _M0L9remainingS291 % 100;
          int32_t _M0L6_2atmpS1234 = _M0L1dS294 / 10;
          int32_t _M0L6_2atmpS1233 = 48 + _M0L6_2atmpS1234;
          int32_t _M0L5d__hiS295 = (uint16_t)_M0L6_2atmpS1233;
          int32_t _M0L6_2atmpS1232 = _M0L1dS294 % 10;
          int32_t _M0L6_2atmpS1231 = 48 + _M0L6_2atmpS1232;
          int32_t _M0L5d__loS296 = (uint16_t)_M0L6_2atmpS1231;
          int32_t _M0L6_2atmpS1227 = _M0L12digit__startS289 + _M0L6offsetS292;
          int32_t _M0L6_2atmpS1226 = _M0L6_2atmpS1227 - 2;
          int32_t _M0L6_2atmpS1229;
          int32_t _M0L6_2atmpS1228;
          int32_t _M0L6_2atmpS1230;
          _M0L6bufferS288[_M0L6_2atmpS1226] = _M0L5d__hiS295;
          _M0L6_2atmpS1229 = _M0L12digit__startS289 + _M0L6offsetS292;
          _M0L6_2atmpS1228 = _M0L6_2atmpS1229 - 1;
          _M0L6bufferS288[_M0L6_2atmpS1228] = _M0L5d__loS296;
          _M0L6_2atmpS1230 = _M0L6offsetS292 - 2;
          _M0L9remainingS291 = _M0L1tS293;
          _M0L6offsetS292 = _M0L6_2atmpS1230;
          continue;
        } else if (_M0L9remainingS291 >= 10) {
          int32_t _M0L6_2atmpS1242 = _M0L9remainingS291 / 10;
          int32_t _M0L6_2atmpS1241 = 48 + _M0L6_2atmpS1242;
          int32_t _M0L5d__hiS298 = (uint16_t)_M0L6_2atmpS1241;
          int32_t _M0L6_2atmpS1240 = _M0L9remainingS291 % 10;
          int32_t _M0L6_2atmpS1239 = 48 + _M0L6_2atmpS1240;
          int32_t _M0L5d__loS299 = (uint16_t)_M0L6_2atmpS1239;
          int32_t _M0L6_2atmpS1236 = _M0L12digit__startS289 + _M0L6offsetS292;
          int32_t _M0L6_2atmpS1235 = _M0L6_2atmpS1236 - 2;
          int32_t _M0L6_2atmpS1238;
          int32_t _M0L6_2atmpS1237;
          _M0L6bufferS288[_M0L6_2atmpS1235] = _M0L5d__hiS298;
          _M0L6_2atmpS1238 = _M0L12digit__startS289 + _M0L6offsetS292;
          _M0L6_2atmpS1237 = _M0L6_2atmpS1238 - 1;
          _M0L6bufferS288[_M0L6_2atmpS1237] = _M0L5d__loS299;
        } else {
          int32_t _M0L6_2atmpS1246 = _M0L12digit__startS289 + _M0L6offsetS292;
          int32_t _M0L6_2atmpS1243 = _M0L6_2atmpS1246 - 1;
          int32_t _M0L6_2atmpS1245 = 48 + _M0L9remainingS291;
          int32_t _M0L6_2atmpS1244 = (uint16_t)_M0L6_2atmpS1245;
          _M0L6bufferS288[_M0L6_2atmpS1243] = _M0L6_2atmpS1244;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS268,
  uint32_t _M0L3numS272,
  int32_t _M0L12digit__startS269,
  int32_t _M0L10total__lenS271,
  int32_t _M0L5radixS262
) {
  uint32_t _M0L4baseS261;
  int32_t _M0L6_2atmpS1193;
  int32_t _M0L6_2atmpS1192;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS261 = *(uint32_t*)&_M0L5radixS262;
  _M0L6_2atmpS1193 = _M0L5radixS262 - 1;
  _M0L6_2atmpS1192 = _M0L5radixS262 & _M0L6_2atmpS1193;
  if (_M0L6_2atmpS1192 == 0) {
    int32_t _M0L5shiftS263;
    uint32_t _M0L4maskS264;
    int32_t _M0L6_2atmpS1200;
    int32_t _M0L6offsetS265;
    uint32_t _M0L1nS266;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS263 = moonbit_ctz32(_M0L5radixS262);
    _M0L4maskS264 = _M0L4baseS261 - 1u;
    _M0L6_2atmpS1200 = _M0L10total__lenS271 - _M0L12digit__startS269;
    _M0L6offsetS265 = _M0L6_2atmpS1200;
    _M0L1nS266 = _M0L3numS272;
    while (1) {
      if (_M0L1nS266 > 0u) {
        uint32_t _M0L6_2atmpS1199 = _M0L1nS266 & _M0L4maskS264;
        int32_t _M0L5digitS267 = *(int32_t*)&_M0L6_2atmpS1199;
        int32_t _M0L6_2atmpS1196 = _M0L12digit__startS269 + _M0L6offsetS265;
        int32_t _M0L6_2atmpS1194 = _M0L6_2atmpS1196 - 1;
        int32_t _M0L6_2atmpS1195 =
          ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L5digitS267];
        int32_t _M0L6_2atmpS1197;
        uint32_t _M0L6_2atmpS1198;
        _M0L6bufferS268[_M0L6_2atmpS1194] = _M0L6_2atmpS1195;
        _M0L6_2atmpS1197 = _M0L6offsetS265 - 1;
        _M0L6_2atmpS1198 = _M0L1nS266 >> (_M0L5shiftS263 & 31);
        _M0L6offsetS265 = _M0L6_2atmpS1197;
        _M0L1nS266 = _M0L6_2atmpS1198;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1207 = _M0L10total__lenS271 - _M0L12digit__startS269;
    int32_t _M0L6offsetS273 = _M0L6_2atmpS1207;
    uint32_t _M0L1nS274 = _M0L3numS272;
    while (1) {
      if (_M0L1nS274 > 0u) {
        uint32_t _M0L1qS275 = _M0L1nS274 / _M0L4baseS261;
        uint32_t _M0L6_2atmpS1206 = _M0L1qS275 * _M0L4baseS261;
        uint32_t _M0L6_2atmpS1205 = _M0L1nS274 - _M0L6_2atmpS1206;
        int32_t _M0L5digitS276 = *(int32_t*)&_M0L6_2atmpS1205;
        int32_t _M0L6_2atmpS1203 = _M0L12digit__startS269 + _M0L6offsetS273;
        int32_t _M0L6_2atmpS1201 = _M0L6_2atmpS1203 - 1;
        int32_t _M0L6_2atmpS1202 =
          ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L5digitS276];
        int32_t _M0L6_2atmpS1204;
        _M0L6bufferS268[_M0L6_2atmpS1201] = _M0L6_2atmpS1202;
        _M0L6_2atmpS1204 = _M0L6offsetS273 - 1;
        _M0L6offsetS273 = _M0L6_2atmpS1204;
        _M0L1nS274 = _M0L1qS275;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS255,
  uint32_t _M0L3numS260,
  int32_t _M0L12digit__startS256,
  int32_t _M0L10total__lenS259
) {
  int32_t _M0L6_2atmpS1191;
  int32_t _M0L6offsetS250;
  uint32_t _M0L1nS251;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1191 = _M0L10total__lenS259 - _M0L12digit__startS256;
  _M0L6offsetS250 = _M0L6_2atmpS1191;
  _M0L1nS251 = _M0L3numS260;
  while (1) {
    if (_M0L6offsetS250 >= 2) {
      uint32_t _M0L6_2atmpS1188 = _M0L1nS251 & 255u;
      int32_t _M0L9byte__valS252 = *(int32_t*)&_M0L6_2atmpS1188;
      int32_t _M0L2hiS253 = _M0L9byte__valS252 / 16;
      int32_t _M0L2loS254 = _M0L9byte__valS252 % 16;
      int32_t _M0L6_2atmpS1182 = _M0L12digit__startS256 + _M0L6offsetS250;
      int32_t _M0L6_2atmpS1180 = _M0L6_2atmpS1182 - 2;
      int32_t _M0L6_2atmpS1181 =
        ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L2hiS253];
      int32_t _M0L6_2atmpS1185;
      int32_t _M0L6_2atmpS1183;
      int32_t _M0L6_2atmpS1184;
      int32_t _M0L6_2atmpS1186;
      uint32_t _M0L6_2atmpS1187;
      _M0L6bufferS255[_M0L6_2atmpS1180] = _M0L6_2atmpS1181;
      _M0L6_2atmpS1185 = _M0L12digit__startS256 + _M0L6offsetS250;
      _M0L6_2atmpS1183 = _M0L6_2atmpS1185 - 1;
      _M0L6_2atmpS1184
      = ((moonbit_string_t)moonbit_string_literal_13.data)[
        _M0L2loS254
      ];
      _M0L6bufferS255[_M0L6_2atmpS1183] = _M0L6_2atmpS1184;
      _M0L6_2atmpS1186 = _M0L6offsetS250 - 2;
      _M0L6_2atmpS1187 = _M0L1nS251 >> 8;
      _M0L6offsetS250 = _M0L6_2atmpS1186;
      _M0L1nS251 = _M0L6_2atmpS1187;
      continue;
    } else if (_M0L6offsetS250 == 1) {
      uint32_t _M0L6_2atmpS1190 = _M0L1nS251 & 15u;
      int32_t _M0L6nibbleS258 = *(int32_t*)&_M0L6_2atmpS1190;
      int32_t _M0L6_2atmpS1189 =
        ((moonbit_string_t)moonbit_string_literal_13.data)[_M0L6nibbleS258];
      _M0L6bufferS255[_M0L12digit__startS256] = _M0L6_2atmpS1189;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS249
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS248;
  struct _M0TPB6Logger _M0L6_2atmpS1179;
  moonbit_string_t _result_1950;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS248);
  _M0L6_2atmpS1179
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS248
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS249, _M0L6_2atmpS1179);
  if (_M0L6_2atmpS1179.$1) {
    moonbit_decref(_M0L6_2atmpS1179.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_1950 = _M0MPB13StringBuilder10to__string(_M0L6loggerS248);
  moonbit_decref_cycle_free(_M0L6loggerS248);
  return _result_1950;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS245,
  struct _M0TPB6Logger _M0L6loggerS244
) {
  moonbit_string_t _M0L6_2atmpS1177;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1177 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS245);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS244.$0->$method_0(_M0L6loggerS244.$1, _M0L6_2atmpS1177);
  moonbit_decref_cycle_free(_M0L6_2atmpS1177);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS247,
  struct _M0TPB6Logger _M0L6loggerS246
) {
  moonbit_string_t _M0L6_2atmpS1178;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1178 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS247);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS246.$0->$method_0(_M0L6loggerS246.$1, _M0L6_2atmpS1178);
  moonbit_decref_cycle_free(_M0L6_2atmpS1178);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS243
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS243.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS242
) {
  moonbit_string_t _M0L8_2afieldS1836;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS1836 = _M0L4selfS242.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1836);
  return _M0L8_2afieldS1836;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS238,
  moonbit_string_t _M0L5valueS239,
  int32_t _M0L5startS240,
  int32_t _M0L3lenS241
) {
  int32_t _M0L6_2atmpS1176;
  int64_t _M0L6_2atmpS1175;
  struct _M0TPC16string10StringView _M0L6_2atmpS1174;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1176 = _M0L5startS240 + _M0L3lenS241;
  _M0L6_2atmpS1175 = (int64_t)_M0L6_2atmpS1176;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1174
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS239, _M0L5startS240, _M0L6_2atmpS1175);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS238, _M0L6_2atmpS1174);
  moonbit_decref_cycle_free(_M0L6_2atmpS1174.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String21clamped__view_2einner(
  moonbit_string_t _M0L4selfS231,
  int32_t _M0L5startS233,
  int64_t _M0L3endS235
) {
  int32_t _M0L3lenS230;
  int32_t _M0Lm2loS232;
  int32_t _M0Lm2hiS234;
  int32_t _M0L6_2atmpS1158;
  int32_t _if__result_1951;
  int32_t _M0L6_2atmpS1166;
  int32_t _if__result_1952;
  int32_t _M0L6_2atmpS1168;
  int32_t _M0L6_2atmpS1169;
  #line 698 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS230 = Moonbit_array_length(_M0L4selfS231);
  if (_M0L5startS233 < 0) {
    _M0Lm2loS232 = 0;
  } else if (_M0L5startS233 > _M0L3lenS230) {
    _M0Lm2loS232 = _M0L3lenS230;
  } else {
    _M0Lm2loS232 = _M0L5startS233;
  }
  if (_M0L3endS235 == 4294967296ll) {
    _M0Lm2hiS234 = _M0L3lenS230;
  } else {
    int64_t _M0L7_2aSomeS236 = _M0L3endS235;
    int32_t _M0L4_2aeS237 = (int32_t)_M0L7_2aSomeS236;
    if (_M0L4_2aeS237 < 0) {
      _M0Lm2hiS234 = 0;
    } else if (_M0L4_2aeS237 > _M0L3lenS230) {
      _M0Lm2hiS234 = _M0L3lenS230;
    } else {
      _M0Lm2hiS234 = _M0L4_2aeS237;
    }
  }
  _M0L6_2atmpS1158 = _M0Lm2loS232;
  if (_M0L6_2atmpS1158 > 0) {
    int32_t _M0L6_2atmpS1157 = _M0Lm2loS232;
    if (_M0L6_2atmpS1157 < _M0L3lenS230) {
      int32_t _M0L6_2atmpS1156 = _M0Lm2loS232;
      int32_t _M0L6_2atmpS1155 = _M0L4selfS231[_M0L6_2atmpS1156];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1155)) {
        int32_t _M0L6_2atmpS1154 = _M0Lm2loS232;
        int32_t _M0L6_2atmpS1153 = _M0L6_2atmpS1154 - 1;
        int32_t _M0L6_2atmpS1152 = _M0L4selfS231[_M0L6_2atmpS1153];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_1951
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1152);
      } else {
        _if__result_1951 = 0;
      }
    } else {
      _if__result_1951 = 0;
    }
  } else {
    _if__result_1951 = 0;
  }
  if (_if__result_1951) {
    int32_t _M0L6_2atmpS1159 = _M0Lm2loS232;
    _M0Lm2loS232 = _M0L6_2atmpS1159 + 1;
  }
  _M0L6_2atmpS1166 = _M0Lm2hiS234;
  if (_M0L6_2atmpS1166 > 0) {
    int32_t _M0L6_2atmpS1165 = _M0Lm2hiS234;
    if (_M0L6_2atmpS1165 < _M0L3lenS230) {
      int32_t _M0L6_2atmpS1164 = _M0Lm2hiS234;
      int32_t _M0L6_2atmpS1163 = _M0L4selfS231[_M0L6_2atmpS1164];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1163)) {
        int32_t _M0L6_2atmpS1162 = _M0Lm2hiS234;
        int32_t _M0L6_2atmpS1161 = _M0L6_2atmpS1162 - 1;
        int32_t _M0L6_2atmpS1160 = _M0L4selfS231[_M0L6_2atmpS1161];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_1952
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1160);
      } else {
        _if__result_1952 = 0;
      }
    } else {
      _if__result_1952 = 0;
    }
  } else {
    _if__result_1952 = 0;
  }
  if (_if__result_1952) {
    int32_t _M0L6_2atmpS1167 = _M0Lm2hiS234;
    _M0Lm2hiS234 = _M0L6_2atmpS1167 - 1;
  }
  _M0L6_2atmpS1168 = _M0Lm2loS232;
  _M0L6_2atmpS1169 = _M0Lm2hiS234;
  if (_M0L6_2atmpS1168 >= _M0L6_2atmpS1169) {
    int32_t _M0L6_2atmpS1170 = _M0Lm2loS232;
    int32_t _M0L6_2atmpS1171 = _M0Lm2loS232;
    moonbit_incref_cycle_free(_M0L4selfS231);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS231,
                                                 .$1 = _M0L6_2atmpS1170,
                                                 .$2 = _M0L6_2atmpS1171};
  } else {
    int32_t _M0L6_2atmpS1172 = _M0Lm2loS232;
    int32_t _M0L6_2atmpS1173 = _M0Lm2hiS234;
    moonbit_incref_cycle_free(_M0L4selfS231);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS231,
                                                 .$1 = _M0L6_2atmpS1172,
                                                 .$2 = _M0L6_2atmpS1173};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS229,
  struct _M0TPB4Show _M0L4showS228
) {
  struct _M0TPB6Logger _M0L6_2atmpS1151;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS229);
  _M0L6_2atmpS1151
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS229
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS228.$0->$method_0(_M0L4showS228.$1, _M0L6_2atmpS1151);
  if (_M0L6_2atmpS1151.$1) {
    moonbit_decref(_M0L6_2atmpS1151.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS227,
  struct _M0TPB4Show _M0L4showS226
) {
  struct _M0TPB6Logger _M0L6_2atmpS1150;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS1150
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS227
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS226.$0->$method_0(_M0L4showS226.$1, _M0L6_2atmpS1150);
  if (_M0L6_2atmpS1150.$1) {
    moonbit_decref(_M0L6_2atmpS1150.$1);
  }
  return 0;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

moonbit_string_t _M0MPC16string6String14escape_2einner(
  moonbit_string_t _M0L4selfS224,
  int32_t _M0L5quoteS225
) {
  struct _M0TPB13StringBuilder* _M0L3bufS223;
  int32_t _M0L6_2atmpS1149;
  struct _M0TPC16string10StringView _M0L6_2atmpS1147;
  struct _M0TPB6Logger _M0L6_2atmpS1148;
  moonbit_string_t _result_1953;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS223 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1149 = Moonbit_array_length(_M0L4selfS224);
  moonbit_incref_cycle_free(_M0L4selfS224);
  _M0L6_2atmpS1147
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS224, .$1 = 0, .$2 = _M0L6_2atmpS1149
  };
  moonbit_incref_cycle_free(_M0L3bufS223);
  _M0L6_2atmpS1148
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS223
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1147, _M0L6_2atmpS1148, _M0L5quoteS225);
  moonbit_decref_cycle_free(_M0L6_2atmpS1147.$0);
  if (_M0L6_2atmpS1148.$1) {
    moonbit_decref(_M0L6_2atmpS1148.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_1953 = _M0MPB13StringBuilder10to__string(_M0L3bufS223);
  moonbit_decref_cycle_free(_M0L3bufS223);
  return _result_1953;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS215,
  struct _M0TPB6Logger _M0L6loggerS213,
  int32_t _M0L5quoteS212
) {
  int32_t _M0L3endS1145;
  int32_t _M0L5startS1146;
  int32_t _M0L3lenS214;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS216;
  int32_t _M0L1iS217;
  int32_t _M0L3segS218;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS212) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS213.$0->$method_3(_M0L6loggerS213.$1, 34);
  }
  _M0L3endS1145 = _M0L4selfS215.$2;
  _M0L5startS1146 = _M0L4selfS215.$1;
  _M0L3lenS214 = _M0L3endS1145 - _M0L5startS1146;
  moonbit_incref_cycle_free(_M0L4selfS215.$0);
  if (_M0L6loggerS213.$1) {
    moonbit_incref(_M0L6loggerS213.$1);
  }
  _M0L6_2aenvS216
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS216)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 72, 0);
  _M0L6_2aenvS216->$0 = _M0L4selfS215;
  _M0L6_2aenvS216->$1 = _M0L6loggerS213;
  _M0L1iS217 = 0;
  _M0L3segS218 = 0;
  _2afor_219:;
  while (1) {
    moonbit_string_t _M0L3strS1142;
    int32_t _M0L5startS1144;
    int32_t _M0L6_2atmpS1143;
    int32_t _M0L4codeS220;
    int32_t _M0L1cS222;
    int32_t _M0L6_2atmpS1126;
    int32_t _M0L6_2atmpS1127;
    int32_t _M0L6_2atmpS1128;
    if (_M0L1iS217 >= _M0L3lenS214) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
      moonbit_decref_cycle_free(_M0L6_2aenvS216);
      break;
    }
    _M0L3strS1142 = _M0L4selfS215.$0;
    _M0L5startS1144 = _M0L4selfS215.$1;
    _M0L6_2atmpS1143 = _M0L5startS1144 + _M0L1iS217;
    _M0L4codeS220 = _M0L3strS1142[_M0L6_2atmpS1143];
    switch (_M0L4codeS220) {
      case 34: {
        _M0L1cS222 = _M0L4codeS220;
        goto join_221;
        break;
      }
      
      case 92: {
        _M0L1cS222 = _M0L4codeS220;
        goto join_221;
        break;
      }
      
      case 10: {
        int32_t _M0L6_2atmpS1129;
        int32_t _M0L6_2atmpS1130;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_14.data);
        _M0L6_2atmpS1129 = _M0L1iS217 + 1;
        _M0L6_2atmpS1130 = _M0L1iS217 + 1;
        _M0L1iS217 = _M0L6_2atmpS1129;
        _M0L3segS218 = _M0L6_2atmpS1130;
        goto _2afor_219;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1131;
        int32_t _M0L6_2atmpS1132;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_15.data);
        _M0L6_2atmpS1131 = _M0L1iS217 + 1;
        _M0L6_2atmpS1132 = _M0L1iS217 + 1;
        _M0L1iS217 = _M0L6_2atmpS1131;
        _M0L3segS218 = _M0L6_2atmpS1132;
        goto _2afor_219;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1133;
        int32_t _M0L6_2atmpS1134;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_16.data);
        _M0L6_2atmpS1133 = _M0L1iS217 + 1;
        _M0L6_2atmpS1134 = _M0L1iS217 + 1;
        _M0L1iS217 = _M0L6_2atmpS1133;
        _M0L3segS218 = _M0L6_2atmpS1134;
        goto _2afor_219;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1135;
        int32_t _M0L6_2atmpS1136;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_17.data);
        _M0L6_2atmpS1135 = _M0L1iS217 + 1;
        _M0L6_2atmpS1136 = _M0L1iS217 + 1;
        _M0L1iS217 = _M0L6_2atmpS1135;
        _M0L3segS218 = _M0L6_2atmpS1136;
        goto _2afor_219;
        break;
      }
      default: {
        if (_M0L4codeS220 < 32) {
          int32_t _M0L6_2atmpS1138;
          moonbit_string_t _M0L6_2atmpS1137;
          int32_t _M0L6_2atmpS1139;
          int32_t _M0L6_2atmpS1140;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_18.data);
          _M0L6_2atmpS1138 = _M0L4codeS220 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1137 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1138);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, _M0L6_2atmpS1137);
          moonbit_decref_cycle_free(_M0L6_2atmpS1137);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS213.$0->$method_0(_M0L6loggerS213.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1139 = _M0L1iS217 + 1;
          _M0L6_2atmpS1140 = _M0L1iS217 + 1;
          _M0L1iS217 = _M0L6_2atmpS1139;
          _M0L3segS218 = _M0L6_2atmpS1140;
          goto _2afor_219;
        } else {
          int32_t _M0L6_2atmpS1141 = _M0L1iS217 + 1;
          int32_t _tmp_1956 = _M0L3segS218;
          _M0L1iS217 = _M0L6_2atmpS1141;
          _M0L3segS218 = _tmp_1956;
          goto _2afor_219;
        }
        break;
      }
    }
    goto joinlet_1955;
    join_221:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS216, _M0L3segS218, _M0L1iS217);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS213.$0->$method_3(_M0L6loggerS213.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1126 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS222);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS213.$0->$method_3(_M0L6loggerS213.$1, _M0L6_2atmpS1126);
    _M0L6_2atmpS1127 = _M0L1iS217 + 1;
    _M0L6_2atmpS1128 = _M0L1iS217 + 1;
    _M0L1iS217 = _M0L6_2atmpS1127;
    _M0L3segS218 = _M0L6_2atmpS1128;
    continue;
    joinlet_1955:;
    break;
  }
  if (_M0L5quoteS212) {
    #line 202 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS213.$0->$method_3(_M0L6loggerS213.$1, 34);
  }
  return 0;
}

int32_t _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS208,
  int32_t _M0L3segS211,
  int32_t _M0L1iS210
) {
  struct _M0TPB6Logger _M0L6loggerS207;
  struct _M0TPC16string10StringView _M0L4selfS209;
  #line 153 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6loggerS207 = _M0L6_2aenvS208->$1;
  _M0L4selfS209 = _M0L6_2aenvS208->$0;
  if (_M0L1iS210 > _M0L3segS211) {
    int64_t _M0L6_2atmpS1125 = (int64_t)_M0L1iS210;
    struct _M0TPC16string10StringView _M0L6_2atmpS1124;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1124
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS209, _M0L3segS211, _M0L6_2atmpS1125);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS207.$0->$method_2(_M0L6loggerS207.$1, _M0L6_2atmpS1124);
    moonbit_decref_cycle_free(_M0L6_2atmpS1124.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS198,
  int32_t _M0L5startS200,
  int64_t _M0L3endS202
) {
  int32_t _M0L3endS1122;
  int32_t _M0L5startS1123;
  int32_t _M0L3lenS197;
  int32_t _M0Lm2loS199;
  int32_t _M0Lm2hiS201;
  moonbit_string_t _M0L3strS205;
  int32_t _M0L4baseS206;
  int32_t _M0L6_2atmpS1100;
  int32_t _if__result_1957;
  int32_t _M0L6_2atmpS1110;
  int32_t _if__result_1958;
  int32_t _M0L6_2atmpS1112;
  int32_t _M0L6_2atmpS1113;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1122 = _M0L4selfS198.$2;
  _M0L5startS1123 = _M0L4selfS198.$1;
  _M0L3lenS197 = _M0L3endS1122 - _M0L5startS1123;
  if (_M0L5startS200 < 0) {
    _M0Lm2loS199 = 0;
  } else if (_M0L5startS200 > _M0L3lenS197) {
    _M0Lm2loS199 = _M0L3lenS197;
  } else {
    _M0Lm2loS199 = _M0L5startS200;
  }
  if (_M0L3endS202 == 4294967296ll) {
    _M0Lm2hiS201 = _M0L3lenS197;
  } else {
    int64_t _M0L7_2aSomeS203 = _M0L3endS202;
    int32_t _M0L4_2aeS204 = (int32_t)_M0L7_2aSomeS203;
    if (_M0L4_2aeS204 < 0) {
      _M0Lm2hiS201 = 0;
    } else if (_M0L4_2aeS204 > _M0L3lenS197) {
      _M0Lm2hiS201 = _M0L3lenS197;
    } else {
      _M0Lm2hiS201 = _M0L4_2aeS204;
    }
  }
  _M0L3strS205 = _M0L4selfS198.$0;
  _M0L4baseS206 = _M0L4selfS198.$1;
  _M0L6_2atmpS1100 = _M0Lm2loS199;
  if (_M0L6_2atmpS1100 > 0) {
    int32_t _M0L6_2atmpS1099 = _M0Lm2loS199;
    if (_M0L6_2atmpS1099 < _M0L3lenS197) {
      int32_t _M0L6_2atmpS1098 = _M0Lm2loS199;
      int32_t _M0L6_2atmpS1097 = _M0L4baseS206 + _M0L6_2atmpS1098;
      int32_t _M0L6_2atmpS1096 = _M0L3strS205[_M0L6_2atmpS1097];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1096)) {
        int32_t _M0L6_2atmpS1095 = _M0Lm2loS199;
        int32_t _M0L6_2atmpS1094 = _M0L4baseS206 + _M0L6_2atmpS1095;
        int32_t _M0L6_2atmpS1093 = _M0L6_2atmpS1094 - 1;
        int32_t _M0L6_2atmpS1092 = _M0L3strS205[_M0L6_2atmpS1093];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_1957
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1092);
      } else {
        _if__result_1957 = 0;
      }
    } else {
      _if__result_1957 = 0;
    }
  } else {
    _if__result_1957 = 0;
  }
  if (_if__result_1957) {
    int32_t _M0L6_2atmpS1101 = _M0Lm2loS199;
    _M0Lm2loS199 = _M0L6_2atmpS1101 + 1;
  }
  _M0L6_2atmpS1110 = _M0Lm2hiS201;
  if (_M0L6_2atmpS1110 > 0) {
    int32_t _M0L6_2atmpS1109 = _M0Lm2hiS201;
    if (_M0L6_2atmpS1109 < _M0L3lenS197) {
      int32_t _M0L6_2atmpS1108 = _M0Lm2hiS201;
      int32_t _M0L6_2atmpS1107 = _M0L4baseS206 + _M0L6_2atmpS1108;
      int32_t _M0L6_2atmpS1106 = _M0L3strS205[_M0L6_2atmpS1107];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1106)) {
        int32_t _M0L6_2atmpS1105 = _M0Lm2hiS201;
        int32_t _M0L6_2atmpS1104 = _M0L4baseS206 + _M0L6_2atmpS1105;
        int32_t _M0L6_2atmpS1103 = _M0L6_2atmpS1104 - 1;
        int32_t _M0L6_2atmpS1102 = _M0L3strS205[_M0L6_2atmpS1103];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_1958
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1102);
      } else {
        _if__result_1958 = 0;
      }
    } else {
      _if__result_1958 = 0;
    }
  } else {
    _if__result_1958 = 0;
  }
  if (_if__result_1958) {
    int32_t _M0L6_2atmpS1111 = _M0Lm2hiS201;
    _M0Lm2hiS201 = _M0L6_2atmpS1111 - 1;
  }
  _M0L6_2atmpS1112 = _M0Lm2loS199;
  _M0L6_2atmpS1113 = _M0Lm2hiS201;
  if (_M0L6_2atmpS1112 >= _M0L6_2atmpS1113) {
    int32_t _M0L6_2atmpS1117 = _M0Lm2loS199;
    int32_t _M0L6_2atmpS1114 = _M0L4baseS206 + _M0L6_2atmpS1117;
    int32_t _M0L6_2atmpS1116 = _M0Lm2loS199;
    int32_t _M0L6_2atmpS1115 = _M0L4baseS206 + _M0L6_2atmpS1116;
    moonbit_incref_cycle_free(_M0L3strS205);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS205,
                                                 .$1 = _M0L6_2atmpS1114,
                                                 .$2 = _M0L6_2atmpS1115};
  } else {
    int32_t _M0L6_2atmpS1121 = _M0Lm2loS199;
    int32_t _M0L6_2atmpS1118 = _M0L4baseS206 + _M0L6_2atmpS1121;
    int32_t _M0L6_2atmpS1120 = _M0Lm2hiS201;
    int32_t _M0L6_2atmpS1119 = _M0L4baseS206 + _M0L6_2atmpS1120;
    moonbit_incref_cycle_free(_M0L3strS205);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS205,
                                                 .$1 = _M0L6_2atmpS1118,
                                                 .$2 = _M0L6_2atmpS1119};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS196) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS195;
  int32_t _M0L6_2atmpS1089;
  int32_t _M0L6_2atmpS1088;
  int32_t _M0L6_2atmpS1091;
  int32_t _M0L6_2atmpS1090;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1087;
  moonbit_string_t _result_1959;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS195 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1089 = _M0IPC14byte4BytePB3Div3div(_M0L1bS196, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1088
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1089);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS195, _M0L6_2atmpS1088);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1091 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS196, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1090
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1091);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS195, _M0L6_2atmpS1090);
  _M0L6_2atmpS1087 = _M0L7_2aselfS195;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_1959 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1087);
  moonbit_decref_cycle_free(_M0L6_2atmpS1087);
  return _result_1959;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS194) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS194 < 10) {
    int32_t _M0L6_2atmpS1084;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1084 = _M0IPC14byte4BytePB3Add3add(_M0L1iS194, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1084);
  } else {
    int32_t _M0L6_2atmpS1086;
    int32_t _M0L6_2atmpS1085;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1086 = _M0IPC14byte4BytePB3Add3add(_M0L1iS194, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1085 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1086, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1085);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS192,
  int32_t _M0L4thatS193
) {
  int32_t _M0L6_2atmpS1082;
  int32_t _M0L6_2atmpS1083;
  int32_t _M0L6_2atmpS1081;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1082 = (int32_t)_M0L4selfS192;
  _M0L6_2atmpS1083 = (int32_t)_M0L4thatS193;
  _M0L6_2atmpS1081 = _M0L6_2atmpS1082 - _M0L6_2atmpS1083;
  return _M0L6_2atmpS1081 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS190,
  int32_t _M0L4thatS191
) {
  int32_t _M0L6_2atmpS1079;
  int32_t _M0L6_2atmpS1080;
  int32_t _M0L6_2atmpS1078;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1079 = (int32_t)_M0L4selfS190;
  _M0L6_2atmpS1080 = (int32_t)_M0L4thatS191;
  _M0L6_2atmpS1078 = _M0L6_2atmpS1079 % _M0L6_2atmpS1080;
  return _M0L6_2atmpS1078 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS188,
  int32_t _M0L4thatS189
) {
  int32_t _M0L6_2atmpS1076;
  int32_t _M0L6_2atmpS1077;
  int32_t _M0L6_2atmpS1075;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1076 = (int32_t)_M0L4selfS188;
  _M0L6_2atmpS1077 = (int32_t)_M0L4thatS189;
  _M0L6_2atmpS1075 = _M0L6_2atmpS1076 / _M0L6_2atmpS1077;
  return _M0L6_2atmpS1075 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS186,
  int32_t _M0L4thatS187
) {
  int32_t _M0L6_2atmpS1073;
  int32_t _M0L6_2atmpS1074;
  int32_t _M0L6_2atmpS1072;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1073 = (int32_t)_M0L4selfS186;
  _M0L6_2atmpS1074 = (int32_t)_M0L4thatS187;
  _M0L6_2atmpS1072 = _M0L6_2atmpS1073 + _M0L6_2atmpS1074;
  return _M0L6_2atmpS1072 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS185) {
  int32_t _M0L6_2atmpS1071;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1071 = (int32_t)_M0L4selfS185;
  return _M0L6_2atmpS1071;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS184) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS184 >= 56320 && _M0L4selfS184 <= 57343;
}

int32_t _M0MPC16uint166UInt1622is__leading__surrogate(int32_t _M0L4selfS183) {
  #line 28 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS183 >= 55296 && _M0L4selfS183 <= 56319;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS182,
  moonbit_string_t _M0L3strS180
) {
  int32_t _M0L8str__lenS179;
  int32_t _M0L3lenS1070;
  int32_t _M0L8requiredS181;
  uint16_t* _M0L4dataS1065;
  int32_t _M0L6_2atmpS1064;
  int32_t _if__result_1960;
  uint16_t* _M0L4dataS1066;
  int32_t _M0L3lenS1067;
  int32_t _M0L3lenS1069;
  int32_t _M0L6_2atmpS1068;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS179 = Moonbit_array_length(_M0L3strS180);
  if (_M0L8str__lenS179 == 0) {
    return 0;
  }
  _M0L3lenS1070 = _M0L4selfS182->$1;
  _M0L8requiredS181 = _M0L3lenS1070 + _M0L8str__lenS179;
  _M0L4dataS1065 = _M0L4selfS182->$0;
  _M0L6_2atmpS1064 = Moonbit_array_length(_M0L4dataS1065);
  if (_M0L8requiredS181 > _M0L6_2atmpS1064) {
    _if__result_1960 = 1;
  } else {
    int32_t _M0L3lenS1063 = _M0L4selfS182->$1;
    _if__result_1960 = _M0L8requiredS181 < _M0L3lenS1063;
  }
  if (_if__result_1960) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS182, _M0L8requiredS181);
  }
  _M0L4dataS1066 = _M0L4selfS182->$0;
  _M0L3lenS1067 = _M0L4selfS182->$1;
  moonbit_incref_cycle_free(_M0L4dataS1066);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1066, _M0L3lenS1067, _M0L3strS180, 0, _M0L8str__lenS179);
  moonbit_decref_cycle_free(_M0L4dataS1066);
  _M0L3lenS1069 = _M0L4selfS182->$1;
  _M0L6_2atmpS1068 = _M0L3lenS1069 + _M0L8str__lenS179;
  _M0L4selfS182->$1 = _M0L6_2atmpS1068;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS175,
  int32_t _M0L11dst__offsetS178,
  moonbit_string_t _M0L3strS176,
  int32_t _M0L11str__offsetS171,
  int32_t _M0L3lenS172
) {
  int32_t _M0L16end__str__offsetS170;
  int32_t _M0L1iS173;
  int32_t _M0L1jS174;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS170 = _M0L11str__offsetS171 + _M0L3lenS172;
  _M0L1iS173 = _M0L11str__offsetS171;
  _M0L1jS174 = _M0L11dst__offsetS178;
  while (1) {
    if (_M0L1iS173 < _M0L16end__str__offsetS170) {
      int32_t _M0L6_2atmpS1060 = _M0L3strS176[_M0L1iS173];
      int32_t _M0L6_2atmpS1061;
      int32_t _M0L6_2atmpS1062;
      _M0L4selfS175[_M0L1jS174] = _M0L6_2atmpS1060;
      _M0L6_2atmpS1061 = _M0L1iS173 + 1;
      _M0L6_2atmpS1062 = _M0L1jS174 + 1;
      _M0L1iS173 = _M0L6_2atmpS1061;
      _M0L1jS174 = _M0L6_2atmpS1062;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS168,
  int32_t _M0L2chS167
) {
  uint32_t _M0L4codeS166;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS166 = _M0MPC14char4Char8to__uint(_M0L2chS167);
  if (_M0L4codeS166 <= 65535u) {
    int32_t _M0L3lenS1031 = _M0L4selfS168->$1;
    uint16_t* _M0L4dataS1033 = _M0L4selfS168->$0;
    int32_t _M0L6_2atmpS1032 = Moonbit_array_length(_M0L4dataS1033);
    uint16_t* _M0L4dataS1036;
    int32_t _M0L3lenS1037;
    int32_t _M0L6_2atmpS1038;
    int32_t _M0L3lenS1040;
    int32_t _M0L6_2atmpS1039;
    if (_M0L3lenS1031 >= _M0L6_2atmpS1032) {
      int32_t _M0L3lenS1035 = _M0L4selfS168->$1;
      int32_t _M0L6_2atmpS1034 = _M0L3lenS1035 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS168, _M0L6_2atmpS1034);
    }
    _M0L4dataS1036 = _M0L4selfS168->$0;
    _M0L3lenS1037 = _M0L4selfS168->$1;
    moonbit_incref_cycle_free(_M0L4dataS1036);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1038 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS166);
    if (
      _M0L3lenS1037 < 0
      || _M0L3lenS1037 >= Moonbit_array_length(_M0L4dataS1036)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1036[_M0L3lenS1037] = _M0L6_2atmpS1038;
    moonbit_decref_cycle_free(_M0L4dataS1036);
    _M0L3lenS1040 = _M0L4selfS168->$1;
    _M0L6_2atmpS1039 = _M0L3lenS1040 + 1;
    _M0L4selfS168->$1 = _M0L6_2atmpS1039;
  } else if (_M0L4codeS166 <= 1114111u) {
    uint16_t* _M0L4dataS1044 = _M0L4selfS168->$0;
    int32_t _M0L6_2atmpS1042 = Moonbit_array_length(_M0L4dataS1044);
    int32_t _M0L3lenS1043 = _M0L4selfS168->$1;
    int32_t _M0L6_2atmpS1041 = _M0L6_2atmpS1042 - _M0L3lenS1043;
    uint32_t _M0L4codeS169;
    uint16_t* _M0L4dataS1047;
    int32_t _M0L3lenS1048;
    uint32_t _M0L6_2atmpS1051;
    uint32_t _M0L6_2atmpS1050;
    int32_t _M0L6_2atmpS1049;
    uint16_t* _M0L4dataS1052;
    int32_t _M0L3lenS1057;
    int32_t _M0L6_2atmpS1053;
    uint32_t _M0L6_2atmpS1056;
    uint32_t _M0L6_2atmpS1055;
    int32_t _M0L6_2atmpS1054;
    int32_t _M0L3lenS1059;
    int32_t _M0L6_2atmpS1058;
    if (_M0L6_2atmpS1041 < 2) {
      int32_t _M0L3lenS1046 = _M0L4selfS168->$1;
      int32_t _M0L6_2atmpS1045 = _M0L3lenS1046 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS168, _M0L6_2atmpS1045);
    }
    _M0L4codeS169 = _M0L4codeS166 - 65536u;
    _M0L4dataS1047 = _M0L4selfS168->$0;
    _M0L3lenS1048 = _M0L4selfS168->$1;
    _M0L6_2atmpS1051 = _M0L4codeS169 >> 10;
    _M0L6_2atmpS1050 = 55296u + _M0L6_2atmpS1051;
    moonbit_incref_cycle_free(_M0L4dataS1047);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1049 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1050);
    if (
      _M0L3lenS1048 < 0
      || _M0L3lenS1048 >= Moonbit_array_length(_M0L4dataS1047)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1047[_M0L3lenS1048] = _M0L6_2atmpS1049;
    moonbit_decref_cycle_free(_M0L4dataS1047);
    _M0L4dataS1052 = _M0L4selfS168->$0;
    _M0L3lenS1057 = _M0L4selfS168->$1;
    _M0L6_2atmpS1053 = _M0L3lenS1057 + 1;
    _M0L6_2atmpS1056 = _M0L4codeS169 & 1023u;
    _M0L6_2atmpS1055 = 56320u + _M0L6_2atmpS1056;
    moonbit_incref_cycle_free(_M0L4dataS1052);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1054 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1055);
    if (
      _M0L6_2atmpS1053 < 0
      || _M0L6_2atmpS1053 >= Moonbit_array_length(_M0L4dataS1052)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1052[_M0L6_2atmpS1053] = _M0L6_2atmpS1054;
    moonbit_decref_cycle_free(_M0L4dataS1052);
    _M0L3lenS1059 = _M0L4selfS168->$1;
    _M0L6_2atmpS1058 = _M0L3lenS1059 + 2;
    _M0L4selfS168->$1 = _M0L6_2atmpS1058;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_19.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS163,
  int32_t _M0L8requiredS164
) {
  uint16_t* _M0L4dataS1030;
  int32_t _M0L6_2atmpS1028;
  int32_t _M0L3lenS1029;
  int32_t _M0L13new__capacityS162;
  uint16_t* _M0L4dataS1025;
  int32_t _M0L6_2atmpS1026;
  int32_t _M0L3lenS1027;
  uint16_t* _M0L9new__dataS165;
  uint16_t* _M0L6_2aoldS1837;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1030 = _M0L4selfS163->$0;
  _M0L6_2atmpS1028 = Moonbit_array_length(_M0L4dataS1030);
  _M0L3lenS1029 = _M0L4selfS163->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS162
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1028, _M0L3lenS1029, _M0L8requiredS164);
  _M0L4dataS1025 = _M0L4selfS163->$0;
  moonbit_incref_cycle_free(_M0L4dataS1025);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1026 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1027 = _M0L4selfS163->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS165
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1025, _M0L13new__capacityS162, _M0L6_2atmpS1026, _M0L3lenS1027, 0, 0);
  _M0L6_2aoldS1837 = _M0L4selfS163->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1837);
  _M0L4selfS163->$0 = _M0L9new__dataS165;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS161,
  int32_t _M0L3lenS157,
  int32_t _M0L8requiredS156
) {
  int32_t _M0L5spaceS158;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS156 < _M0L3lenS157) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_20.data);
  }
  _M0L5spaceS158 = _M0L7currentS161;
  while (1) {
    if (_M0L5spaceS158 < _M0L8requiredS156) {
      int32_t _M0L4nextS159 = _M0L5spaceS158 * 2;
      if (_M0L4nextS159 <= _M0L5spaceS158) {
        return _M0L8requiredS156;
      }
      _M0L5spaceS158 = _M0L4nextS159;
      continue;
    } else {
      return _M0L5spaceS158;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS155) {
  int32_t _M0L6_2atmpS1024;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1024 = *(int32_t*)&_M0L4selfS155;
  return (uint16_t)_M0L6_2atmpS1024;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS154) {
  int32_t _M0L6_2atmpS1023;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1023 = _M0L4selfS154;
  return *(uint32_t*)&_M0L6_2atmpS1023;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS152
) {
  int32_t _M0L3lenS1014;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1014 = _M0L4selfS152->$1;
  if (_M0L3lenS1014 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1015 = _M0L4selfS152->$1;
    uint16_t* _M0L4dataS1017 = _M0L4selfS152->$0;
    int32_t _M0L6_2atmpS1016 = Moonbit_array_length(_M0L4dataS1017);
    if (_M0L3lenS1015 == _M0L6_2atmpS1016) {
      uint16_t* _M0L4dataS1018 = _M0L4selfS152->$0;
      moonbit_incref_cycle_free(_M0L4dataS1018);
      return _M0L4dataS1018;
    } else {
      uint16_t* _M0L4dataS1019 = _M0L4selfS152->$0;
      int32_t _M0L3lenS1020 = _M0L4selfS152->$1;
      int32_t _M0L6_2atmpS1021;
      int32_t _M0L3lenS1022;
      uint16_t* _M0L4dataS153;
      moonbit_incref_cycle_free(_M0L4dataS1019);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1021 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1022 = _M0L4selfS152->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS153
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1019, _M0L3lenS1020, _M0L6_2atmpS1021, _M0L3lenS1022, 0, 0);
      return _M0L4dataS153;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS149,
  int32_t _M0L13allocate__lenS145,
  int32_t _M0L4initS150,
  int32_t _M0L3lenS146,
  int32_t _M0L11src__offsetS147,
  int32_t _M0L11dst__offsetS148
) {
  int32_t _if__result_1963;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS145 >= 0) {
    if (_M0L3lenS146 >= 0) {
      if (_M0L11src__offsetS147 >= 0) {
        if (_M0L11dst__offsetS148 >= 0) {
          int32_t _M0L6_2atmpS1010 = _M0L11src__offsetS147 + _M0L3lenS146;
          int32_t _M0L6_2atmpS1011 = Moonbit_array_length(_M0L3srcS149);
          if (_M0L6_2atmpS1010 <= _M0L6_2atmpS1011) {
            int32_t _M0L6_2atmpS1009 = _M0L11dst__offsetS148 + _M0L3lenS146;
            _if__result_1963 = _M0L6_2atmpS1009 <= _M0L13allocate__lenS145;
          } else {
            _if__result_1963 = 0;
          }
        } else {
          _if__result_1963 = 0;
        }
      } else {
        _if__result_1963 = 0;
      }
    } else {
      _if__result_1963 = 0;
    }
  } else {
    _if__result_1963 = 0;
  }
  if (_if__result_1963) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS149, _M0L13allocate__lenS145, _M0L4initS150, _M0L11src__offsetS147, _M0L11dst__offsetS148, _M0L3lenS146);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS151;
    int32_t _M0L6_2atmpS1013;
    moonbit_string_t _M0L6_2atmpS1012;
    uint16_t* _result_1964;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS151
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS151, (moonbit_string_t)moonbit_string_literal_21.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS151, _M0L13allocate__lenS145);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS151, (moonbit_string_t)moonbit_string_literal_22.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS151, _M0L11src__offsetS147);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS151, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS151, _M0L11dst__offsetS148);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS151, (moonbit_string_t)moonbit_string_literal_24.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS151, _M0L3lenS146);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS151, (moonbit_string_t)moonbit_string_literal_25.data);
    _M0L6_2atmpS1013 = Moonbit_array_length(_M0L3srcS149);
    moonbit_decref_cycle_free(_M0L3srcS149);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS151, _M0L6_2atmpS1013);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1012
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS151);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS151);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_1964 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1012);
    moonbit_decref_cycle_free(_M0L6_2atmpS1012);
    return _result_1964;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS142,
  int32_t _M0L13allocate__lenS139,
  int32_t _M0L4initS140,
  int32_t _M0L11src__offsetS143,
  int32_t _M0L11dst__offsetS141,
  int32_t _M0L9blit__lenS144
) {
  uint16_t* _M0L3dstS138;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS138
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS139, _M0L4initS140);
  moonbit_incref_cycle_free(_M0L3dstS138);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS138, _M0L11dst__offsetS141, _M0L3srcS142, _M0L11src__offsetS143, _M0L9blit__lenS144, sizeof(uint16_t));
  return _M0L3dstS138;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS136
) {
  int32_t _M0L7initialS135;
  uint16_t* _M0L4dataS137;
  struct _M0TPB13StringBuilder* _block_1965;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS136 < 1) {
    _M0L7initialS135 = 1;
  } else {
    int32_t _M0L6_2atmpS1008 = _M0L10size__hintS136 + 1;
    _M0L7initialS135 = _M0L6_2atmpS1008 / 2;
  }
  _M0L4dataS137 = (uint16_t*)moonbit_make_string(_M0L7initialS135, 0);
  _block_1965
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_1965)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 77, 0);
  _block_1965->$0 = _M0L4dataS137;
  _block_1965->$1 = 0;
  return _block_1965;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS134) {
  int32_t _M0L6_2atmpS1007;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1007 = (int32_t)_M0L4selfS134;
  return _M0L6_2atmpS1007;
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS114,
  int32_t _M0L13allocate__lenS110,
  int32_t _M0L3lenS111,
  int32_t _M0L11src__offsetS112,
  int32_t _M0L11dst__offsetS113
) {
  int32_t _if__result_1966;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS110 >= 0) {
    if (_M0L3lenS111 >= 0) {
      if (_M0L11src__offsetS112 >= 0) {
        if (_M0L11dst__offsetS113 >= 0) {
          int32_t _M0L6_2atmpS988 = _M0L11src__offsetS112 + _M0L3lenS111;
          int32_t _M0L6_2atmpS989;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS989
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS114);
          if (_M0L6_2atmpS988 <= _M0L6_2atmpS989) {
            int32_t _M0L6_2atmpS987 = _M0L11dst__offsetS113 + _M0L3lenS111;
            _if__result_1966 = _M0L6_2atmpS987 <= _M0L13allocate__lenS110;
          } else {
            _if__result_1966 = 0;
          }
        } else {
          _if__result_1966 = 0;
        }
      } else {
        _if__result_1966 = 0;
      }
    } else {
      _if__result_1966 = 0;
    }
  } else {
    _if__result_1966 = 0;
  }
  if (_if__result_1966) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS114, _M0L13allocate__lenS110, _M0L11src__offsetS112, _M0L11dst__offsetS113, _M0L3lenS111);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS115;
    int32_t _M0L6_2atmpS991;
    moonbit_string_t _M0L6_2atmpS990;
    float* _result_1967;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS115
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS115, (moonbit_string_t)moonbit_string_literal_21.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS115, _M0L13allocate__lenS110);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS115, (moonbit_string_t)moonbit_string_literal_22.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS115, _M0L11src__offsetS112);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS115, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS115, _M0L11dst__offsetS113);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS115, (moonbit_string_t)moonbit_string_literal_24.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS115, _M0L3lenS111);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS115, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS991 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS114);
    moonbit_decref_cycle_free(_M0L3srcS114);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS115, _M0L6_2atmpS991);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS990
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS115);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS115);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_1967
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS990);
    moonbit_decref_cycle_free(_M0L6_2atmpS990);
    return _result_1967;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS120,
  int32_t _M0L13allocate__lenS116,
  int32_t _M0L3lenS117,
  int32_t _M0L11src__offsetS118,
  int32_t _M0L11dst__offsetS119
) {
  int32_t _if__result_1968;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS116 >= 0) {
    if (_M0L3lenS117 >= 0) {
      if (_M0L11src__offsetS118 >= 0) {
        if (_M0L11dst__offsetS119 >= 0) {
          int32_t _M0L6_2atmpS993 = _M0L11src__offsetS118 + _M0L3lenS117;
          int32_t _M0L6_2atmpS994;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS994
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS120);
          if (_M0L6_2atmpS993 <= _M0L6_2atmpS994) {
            int32_t _M0L6_2atmpS992 = _M0L11dst__offsetS119 + _M0L3lenS117;
            _if__result_1968 = _M0L6_2atmpS992 <= _M0L13allocate__lenS116;
          } else {
            _if__result_1968 = 0;
          }
        } else {
          _if__result_1968 = 0;
        }
      } else {
        _if__result_1968 = 0;
      }
    } else {
      _if__result_1968 = 0;
    }
  } else {
    _if__result_1968 = 0;
  }
  if (_if__result_1968) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS120, _M0L13allocate__lenS116, _M0L11src__offsetS118, _M0L11dst__offsetS119, _M0L3lenS117);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS121;
    int32_t _M0L6_2atmpS996;
    moonbit_string_t _M0L6_2atmpS995;
    int32_t* _result_1969;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS121
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS121, (moonbit_string_t)moonbit_string_literal_21.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L13allocate__lenS116);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS121, (moonbit_string_t)moonbit_string_literal_22.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L11src__offsetS118);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS121, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L11dst__offsetS119);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS121, (moonbit_string_t)moonbit_string_literal_24.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L3lenS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS121, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS996 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS120);
    moonbit_decref_cycle_free(_M0L3srcS120);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L6_2atmpS996);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS995
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS121);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS121);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_1969
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS995);
    moonbit_decref_cycle_free(_M0L6_2atmpS995);
    return _result_1969;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS126,
  int32_t _M0L13allocate__lenS122,
  int32_t _M0L3lenS123,
  int32_t _M0L11src__offsetS124,
  int32_t _M0L11dst__offsetS125
) {
  int32_t _if__result_1970;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS122 >= 0) {
    if (_M0L3lenS123 >= 0) {
      if (_M0L11src__offsetS124 >= 0) {
        if (_M0L11dst__offsetS125 >= 0) {
          int32_t _M0L6_2atmpS998 = _M0L11src__offsetS124 + _M0L3lenS123;
          int32_t _M0L6_2atmpS999;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS999
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS126);
          if (_M0L6_2atmpS998 <= _M0L6_2atmpS999) {
            int32_t _M0L6_2atmpS997 = _M0L11dst__offsetS125 + _M0L3lenS123;
            _if__result_1970 = _M0L6_2atmpS997 <= _M0L13allocate__lenS122;
          } else {
            _if__result_1970 = 0;
          }
        } else {
          _if__result_1970 = 0;
        }
      } else {
        _if__result_1970 = 0;
      }
    } else {
      _if__result_1970 = 0;
    }
  } else {
    _if__result_1970 = 0;
  }
  if (_if__result_1970) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS122, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS126, _M0L11src__offsetS124, _M0L11dst__offsetS125, _M0L3lenS123);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS127;
    int32_t _M0L6_2atmpS1001;
    moonbit_string_t _M0L6_2atmpS1000;
    moonbit_string_t* _result_1971;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS127
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_21.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L13allocate__lenS122);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_22.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L11src__offsetS124);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L11dst__offsetS125);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_24.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L3lenS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1001 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS126);
    moonbit_decref_cycle_free(_M0L3srcS126);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L6_2atmpS1001);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1000
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS127);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS127);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_1971
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1000);
    moonbit_decref_cycle_free(_M0L6_2atmpS1000);
    return _result_1971;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS132,
  int32_t _M0L13allocate__lenS128,
  int32_t _M0L3lenS129,
  int32_t _M0L11src__offsetS130,
  int32_t _M0L11dst__offsetS131
) {
  int32_t _if__result_1972;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS128 >= 0) {
    if (_M0L3lenS129 >= 0) {
      if (_M0L11src__offsetS130 >= 0) {
        if (_M0L11dst__offsetS131 >= 0) {
          int32_t _M0L6_2atmpS1003 = _M0L11src__offsetS130 + _M0L3lenS129;
          int32_t _M0L6_2atmpS1004;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1004
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS132);
          if (_M0L6_2atmpS1003 <= _M0L6_2atmpS1004) {
            int32_t _M0L6_2atmpS1002 = _M0L11dst__offsetS131 + _M0L3lenS129;
            _if__result_1972 = _M0L6_2atmpS1002 <= _M0L13allocate__lenS128;
          } else {
            _if__result_1972 = 0;
          }
        } else {
          _if__result_1972 = 0;
        }
      } else {
        _if__result_1972 = 0;
      }
    } else {
      _if__result_1972 = 0;
    }
  } else {
    _if__result_1972 = 0;
  }
  if (_if__result_1972) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS128, 0, _M0L3srcS132, _M0L11src__offsetS130, _M0L11dst__offsetS131, _M0L3lenS129);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS133;
    int32_t _M0L6_2atmpS1006;
    moonbit_string_t _M0L6_2atmpS1005;
    struct _M0TUsiE** _result_1973;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS133
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS133, (moonbit_string_t)moonbit_string_literal_21.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L13allocate__lenS128);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS133, (moonbit_string_t)moonbit_string_literal_22.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L11src__offsetS130);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS133, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L11dst__offsetS131);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS133, (moonbit_string_t)moonbit_string_literal_24.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L3lenS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS133, (moonbit_string_t)moonbit_string_literal_25.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1006 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS132);
    moonbit_decref_cycle_free(_M0L3srcS132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L6_2atmpS1006);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1005
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS133);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS133);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_1973
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1005);
    moonbit_decref_cycle_free(_M0L6_2atmpS1005);
    return _result_1973;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS107,
  moonbit_string_t _M0L3objS106
) {
  struct _M0TPB6Logger _M0L6_2atmpS985;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS107);
  _M0L6_2atmpS985
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS107
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS106, _M0L6_2atmpS985);
  if (_M0L6_2atmpS985.$1) {
    moonbit_decref(_M0L6_2atmpS985.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS109,
  int32_t _M0L3objS108
) {
  struct _M0TPB6Logger _M0L6_2atmpS986;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS109);
  _M0L6_2atmpS986
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS109
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS108, _M0L6_2atmpS986);
  if (_M0L6_2atmpS986.$1) {
    moonbit_decref(_M0L6_2atmpS986.$1);
  }
  return 0;
}

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS85,
  int32_t _M0L13allocate__lenS83,
  int32_t _M0L11src__offsetS86,
  int32_t _M0L11dst__offsetS84,
  int32_t _M0L9blit__lenS87
) {
  float* _M0L3dstS82;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS82 = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS83);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS82, _M0L11dst__offsetS84, _M0L3srcS85, _M0L11src__offsetS86, _M0L9blit__lenS87);
  moonbit_decref_cycle_free(_M0L3srcS85);
  return _M0L3dstS82;
}

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t* _M0L3srcS91,
  int32_t _M0L13allocate__lenS89,
  int32_t _M0L11src__offsetS92,
  int32_t _M0L11dst__offsetS90,
  int32_t _M0L9blit__lenS93
) {
  int32_t* _M0L3dstS88;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS88
  = (int32_t*)moonbit_make_int32_array_raw(_M0L13allocate__lenS89);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L3dstS88, _M0L11dst__offsetS90, _M0L3srcS91, _M0L11src__offsetS92, _M0L9blit__lenS93);
  moonbit_decref_cycle_free(_M0L3srcS91);
  return _M0L3dstS88;
}

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t* _M0L3srcS97,
  int32_t _M0L13allocate__lenS95,
  int32_t _M0L11src__offsetS98,
  int32_t _M0L11dst__offsetS96,
  int32_t _M0L9blit__lenS99
) {
  moonbit_string_t* _M0L3dstS94;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS94
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L13allocate__lenS95, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGsE(_M0L3dstS94, _M0L11dst__offsetS96, _M0L3srcS97, _M0L11src__offsetS98, _M0L9blit__lenS99);
  moonbit_decref_cycle_free(_M0L3srcS97);
  return _M0L3dstS94;
}

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE** _M0L3srcS103,
  int32_t _M0L13allocate__lenS101,
  int32_t _M0L11src__offsetS104,
  int32_t _M0L11dst__offsetS102,
  int32_t _M0L9blit__lenS105
) {
  struct _M0TUsiE** _M0L3dstS100;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS100
  = (struct _M0TUsiE**)moonbit_make_ref_array(_M0L13allocate__lenS101, 0);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGUsiEE(_M0L3dstS100, _M0L11dst__offsetS102, _M0L3srcS103, _M0L11src__offsetS104, _M0L9blit__lenS105);
  moonbit_decref_cycle_free(_M0L3srcS103);
  return _M0L3dstS100;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS62,
  int32_t _M0L11dst__offsetS63,
  float* _M0L3srcS64,
  int32_t _M0L11src__offsetS65,
  int32_t _M0L3lenS66
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS64);
  moonbit_incref_cycle_free(_M0L3dstS62);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS62, _M0L11dst__offsetS63, _M0L3srcS64, _M0L11src__offsetS65, _M0L3lenS66, sizeof(float));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t* _M0L3dstS67,
  int32_t _M0L11dst__offsetS68,
  int32_t* _M0L3srcS69,
  int32_t _M0L11src__offsetS70,
  int32_t _M0L3lenS71
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS69);
  moonbit_incref_cycle_free(_M0L3dstS67);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS67, _M0L11dst__offsetS68, _M0L3srcS69, _M0L11src__offsetS70, _M0L3lenS71, sizeof(int32_t));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS72,
  int32_t _M0L11dst__offsetS73,
  moonbit_string_t* _M0L3srcS74,
  int32_t _M0L11src__offsetS75,
  int32_t _M0L3lenS76
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS74);
  moonbit_incref_cycle_free(_M0L3dstS72);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS72, _M0L11dst__offsetS73, _M0L3srcS74, _M0L11src__offsetS75, _M0L3lenS76);
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS77,
  int32_t _M0L11dst__offsetS78,
  struct _M0TUsiE** _M0L3srcS79,
  int32_t _M0L11src__offsetS80,
  int32_t _M0L3lenS81
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS79);
  moonbit_incref_cycle_free(_M0L3dstS77);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS77, _M0L11dst__offsetS78, _M0L3srcS79, _M0L11src__offsetS80, _M0L3lenS81);
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t* _M0L3dstS17,
  int32_t _M0L11dst__offsetS19,
  uint16_t* _M0L3srcS18,
  int32_t _M0L11src__offsetS20,
  int32_t _M0L3lenS22
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS17 == _M0L3srcS18 && _M0L11dst__offsetS19 < _M0L11src__offsetS20
  ) {
    int32_t _M0L1iS21 = 0;
    while (1) {
      if (_M0L1iS21 < _M0L3lenS22) {
        int32_t _M0L6_2atmpS940 = _M0L11dst__offsetS19 + _M0L1iS21;
        int32_t _M0L6_2atmpS942 = _M0L11src__offsetS20 + _M0L1iS21;
        int32_t _M0L6_2atmpS941;
        int32_t _M0L6_2atmpS943;
        if (
          _M0L6_2atmpS942 < 0
          || _M0L6_2atmpS942 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS941 = (int32_t)_M0L3srcS18[_M0L6_2atmpS942];
        if (
          _M0L6_2atmpS940 < 0
          || _M0L6_2atmpS940 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS940] = _M0L6_2atmpS941;
        _M0L6_2atmpS943 = _M0L1iS21 + 1;
        _M0L1iS21 = _M0L6_2atmpS943;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS18);
        moonbit_decref_cycle_free(_M0L3dstS17);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS948 = _M0L3lenS22 - 1;
    int32_t _M0L1iS24 = _M0L6_2atmpS948;
    while (1) {
      if (_M0L1iS24 >= 0) {
        int32_t _M0L6_2atmpS944 = _M0L11dst__offsetS19 + _M0L1iS24;
        int32_t _M0L6_2atmpS946 = _M0L11src__offsetS20 + _M0L1iS24;
        int32_t _M0L6_2atmpS945;
        int32_t _M0L6_2atmpS947;
        if (
          _M0L6_2atmpS946 < 0
          || _M0L6_2atmpS946 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS945 = (int32_t)_M0L3srcS18[_M0L6_2atmpS946];
        if (
          _M0L6_2atmpS944 < 0
          || _M0L6_2atmpS944 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS944] = _M0L6_2atmpS945;
        _M0L6_2atmpS947 = _M0L1iS24 - 1;
        _M0L1iS24 = _M0L6_2atmpS947;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS18);
        moonbit_decref_cycle_free(_M0L3dstS17);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS26,
  int32_t _M0L11dst__offsetS28,
  float* _M0L3srcS27,
  int32_t _M0L11src__offsetS29,
  int32_t _M0L3lenS31
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS26 == _M0L3srcS27 && _M0L11dst__offsetS28 < _M0L11src__offsetS29
  ) {
    int32_t _M0L1iS30 = 0;
    while (1) {
      if (_M0L1iS30 < _M0L3lenS31) {
        int32_t _M0L6_2atmpS949 = _M0L11dst__offsetS28 + _M0L1iS30;
        int32_t _M0L6_2atmpS951 = _M0L11src__offsetS29 + _M0L1iS30;
        float _M0L6_2atmpS950;
        int32_t _M0L6_2atmpS952;
        if (
          _M0L6_2atmpS951 < 0
          || _M0L6_2atmpS951 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS950 = (float)_M0L3srcS27[_M0L6_2atmpS951];
        if (
          _M0L6_2atmpS949 < 0
          || _M0L6_2atmpS949 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS949] = _M0L6_2atmpS950;
        _M0L6_2atmpS952 = _M0L1iS30 + 1;
        _M0L1iS30 = _M0L6_2atmpS952;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS27);
        moonbit_decref_cycle_free(_M0L3dstS26);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS957 = _M0L3lenS31 - 1;
    int32_t _M0L1iS33 = _M0L6_2atmpS957;
    while (1) {
      if (_M0L1iS33 >= 0) {
        int32_t _M0L6_2atmpS953 = _M0L11dst__offsetS28 + _M0L1iS33;
        int32_t _M0L6_2atmpS955 = _M0L11src__offsetS29 + _M0L1iS33;
        float _M0L6_2atmpS954;
        int32_t _M0L6_2atmpS956;
        if (
          _M0L6_2atmpS955 < 0
          || _M0L6_2atmpS955 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS954 = (float)_M0L3srcS27[_M0L6_2atmpS955];
        if (
          _M0L6_2atmpS953 < 0
          || _M0L6_2atmpS953 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS953] = _M0L6_2atmpS954;
        _M0L6_2atmpS956 = _M0L1iS33 - 1;
        _M0L1iS33 = _M0L6_2atmpS956;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS27);
        moonbit_decref_cycle_free(_M0L3dstS26);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS35,
  int32_t _M0L11dst__offsetS37,
  int32_t* _M0L3srcS36,
  int32_t _M0L11src__offsetS38,
  int32_t _M0L3lenS40
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS35 == _M0L3srcS36 && _M0L11dst__offsetS37 < _M0L11src__offsetS38
  ) {
    int32_t _M0L1iS39 = 0;
    while (1) {
      if (_M0L1iS39 < _M0L3lenS40) {
        int32_t _M0L6_2atmpS958 = _M0L11dst__offsetS37 + _M0L1iS39;
        int32_t _M0L6_2atmpS960 = _M0L11src__offsetS38 + _M0L1iS39;
        int32_t _M0L6_2atmpS959;
        int32_t _M0L6_2atmpS961;
        if (
          _M0L6_2atmpS960 < 0
          || _M0L6_2atmpS960 >= Moonbit_array_length(_M0L3srcS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS959 = (int32_t)_M0L3srcS36[_M0L6_2atmpS960];
        if (
          _M0L6_2atmpS958 < 0
          || _M0L6_2atmpS958 >= Moonbit_array_length(_M0L3dstS35)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS35[_M0L6_2atmpS958] = _M0L6_2atmpS959;
        _M0L6_2atmpS961 = _M0L1iS39 + 1;
        _M0L1iS39 = _M0L6_2atmpS961;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS36);
        moonbit_decref_cycle_free(_M0L3dstS35);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS966 = _M0L3lenS40 - 1;
    int32_t _M0L1iS42 = _M0L6_2atmpS966;
    while (1) {
      if (_M0L1iS42 >= 0) {
        int32_t _M0L6_2atmpS962 = _M0L11dst__offsetS37 + _M0L1iS42;
        int32_t _M0L6_2atmpS964 = _M0L11src__offsetS38 + _M0L1iS42;
        int32_t _M0L6_2atmpS963;
        int32_t _M0L6_2atmpS965;
        if (
          _M0L6_2atmpS964 < 0
          || _M0L6_2atmpS964 >= Moonbit_array_length(_M0L3srcS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS963 = (int32_t)_M0L3srcS36[_M0L6_2atmpS964];
        if (
          _M0L6_2atmpS962 < 0
          || _M0L6_2atmpS962 >= Moonbit_array_length(_M0L3dstS35)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS35[_M0L6_2atmpS962] = _M0L6_2atmpS963;
        _M0L6_2atmpS965 = _M0L1iS42 - 1;
        _M0L1iS42 = _M0L6_2atmpS965;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS36);
        moonbit_decref_cycle_free(_M0L3dstS35);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS44,
  int32_t _M0L11dst__offsetS46,
  moonbit_string_t* _M0L3srcS45,
  int32_t _M0L11src__offsetS47,
  int32_t _M0L3lenS49
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS44 == _M0L3srcS45 && _M0L11dst__offsetS46 < _M0L11src__offsetS47
  ) {
    int32_t _M0L1iS48 = 0;
    while (1) {
      if (_M0L1iS48 < _M0L3lenS49) {
        int32_t _M0L6_2atmpS967 = _M0L11dst__offsetS46 + _M0L1iS48;
        int32_t _M0L6_2atmpS969 = _M0L11src__offsetS47 + _M0L1iS48;
        moonbit_string_t _M0L6_2atmpS968;
        moonbit_string_t _M0L6_2aoldS1838;
        int32_t _M0L6_2atmpS970;
        if (
          _M0L6_2atmpS969 < 0
          || _M0L6_2atmpS969 >= Moonbit_array_length(_M0L3srcS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS968 = (moonbit_string_t)_M0L3srcS45[_M0L6_2atmpS969];
        if (
          _M0L6_2atmpS967 < 0
          || _M0L6_2atmpS967 >= Moonbit_array_length(_M0L3dstS44)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1838 = (moonbit_string_t)_M0L3dstS44[_M0L6_2atmpS967];
        moonbit_incref_cycle_free(_M0L6_2atmpS968);
        moonbit_decref_cycle_free(_M0L6_2aoldS1838);
        _M0L3dstS44[_M0L6_2atmpS967] = _M0L6_2atmpS968;
        _M0L6_2atmpS970 = _M0L1iS48 + 1;
        _M0L1iS48 = _M0L6_2atmpS970;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS45);
        moonbit_decref_cycle_free(_M0L3dstS44);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS975 = _M0L3lenS49 - 1;
    int32_t _M0L1iS51 = _M0L6_2atmpS975;
    while (1) {
      if (_M0L1iS51 >= 0) {
        int32_t _M0L6_2atmpS971 = _M0L11dst__offsetS46 + _M0L1iS51;
        int32_t _M0L6_2atmpS973 = _M0L11src__offsetS47 + _M0L1iS51;
        moonbit_string_t _M0L6_2atmpS972;
        moonbit_string_t _M0L6_2aoldS1839;
        int32_t _M0L6_2atmpS974;
        if (
          _M0L6_2atmpS973 < 0
          || _M0L6_2atmpS973 >= Moonbit_array_length(_M0L3srcS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS972 = (moonbit_string_t)_M0L3srcS45[_M0L6_2atmpS973];
        if (
          _M0L6_2atmpS971 < 0
          || _M0L6_2atmpS971 >= Moonbit_array_length(_M0L3dstS44)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1839 = (moonbit_string_t)_M0L3dstS44[_M0L6_2atmpS971];
        moonbit_incref_cycle_free(_M0L6_2atmpS972);
        moonbit_decref_cycle_free(_M0L6_2aoldS1839);
        _M0L3dstS44[_M0L6_2atmpS971] = _M0L6_2atmpS972;
        _M0L6_2atmpS974 = _M0L1iS51 - 1;
        _M0L1iS51 = _M0L6_2atmpS974;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS45);
        moonbit_decref_cycle_free(_M0L3dstS44);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS53,
  int32_t _M0L11dst__offsetS55,
  struct _M0TUsiE** _M0L3srcS54,
  int32_t _M0L11src__offsetS56,
  int32_t _M0L3lenS58
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS53 == _M0L3srcS54 && _M0L11dst__offsetS55 < _M0L11src__offsetS56
  ) {
    int32_t _M0L1iS57 = 0;
    while (1) {
      if (_M0L1iS57 < _M0L3lenS58) {
        int32_t _M0L6_2atmpS976 = _M0L11dst__offsetS55 + _M0L1iS57;
        int32_t _M0L6_2atmpS978 = _M0L11src__offsetS56 + _M0L1iS57;
        struct _M0TUsiE* _M0L6_2atmpS977;
        struct _M0TUsiE* _M0L6_2aoldS1840;
        int32_t _M0L6_2atmpS979;
        if (
          _M0L6_2atmpS978 < 0
          || _M0L6_2atmpS978 >= Moonbit_array_length(_M0L3srcS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS977 = (struct _M0TUsiE*)_M0L3srcS54[_M0L6_2atmpS978];
        if (
          _M0L6_2atmpS976 < 0
          || _M0L6_2atmpS976 >= Moonbit_array_length(_M0L3dstS53)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1840 = (struct _M0TUsiE*)_M0L3dstS53[_M0L6_2atmpS976];
        if (_M0L6_2atmpS977) {
          moonbit_incref_cycle_free(_M0L6_2atmpS977);
        }
        if (_M0L6_2aoldS1840) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1840);
        }
        _M0L3dstS53[_M0L6_2atmpS976] = _M0L6_2atmpS977;
        _M0L6_2atmpS979 = _M0L1iS57 + 1;
        _M0L1iS57 = _M0L6_2atmpS979;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS54);
        moonbit_decref_cycle_free(_M0L3dstS53);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS984 = _M0L3lenS58 - 1;
    int32_t _M0L1iS60 = _M0L6_2atmpS984;
    while (1) {
      if (_M0L1iS60 >= 0) {
        int32_t _M0L6_2atmpS980 = _M0L11dst__offsetS55 + _M0L1iS60;
        int32_t _M0L6_2atmpS982 = _M0L11src__offsetS56 + _M0L1iS60;
        struct _M0TUsiE* _M0L6_2atmpS981;
        struct _M0TUsiE* _M0L6_2aoldS1841;
        int32_t _M0L6_2atmpS983;
        if (
          _M0L6_2atmpS982 < 0
          || _M0L6_2atmpS982 >= Moonbit_array_length(_M0L3srcS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS981 = (struct _M0TUsiE*)_M0L3srcS54[_M0L6_2atmpS982];
        if (
          _M0L6_2atmpS980 < 0
          || _M0L6_2atmpS980 >= Moonbit_array_length(_M0L3dstS53)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1841 = (struct _M0TUsiE*)_M0L3dstS53[_M0L6_2atmpS980];
        if (_M0L6_2atmpS981) {
          moonbit_incref_cycle_free(_M0L6_2atmpS981);
        }
        if (_M0L6_2aoldS1841) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1841);
        }
        _M0L3dstS53[_M0L6_2atmpS980] = _M0L6_2atmpS981;
        _M0L6_2atmpS983 = _M0L1iS60 - 1;
        _M0L1iS60 = _M0L6_2atmpS983;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS54);
        moonbit_decref_cycle_free(_M0L3dstS53);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPB18UninitializedArray6lengthGfE(float* _M0L4selfS13) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS13);
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
  _M0L10_2ax__6388S12.$0->$method_0(_M0L10_2ax__6388S12.$1, (moonbit_string_t)moonbit_string_literal_26.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S12, _M0L15_2a_2aarg__6389S11);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S12.$0->$method_0(_M0L10_2ax__6388S12.$1, (moonbit_string_t)moonbit_string_literal_27.data);
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

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(
  moonbit_string_t _M0L3msgS3
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS3);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS910) {
  switch (Moonbit_object_tag(_M0L4_2aeS910)) {
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_28.data;
      break;
    }
    
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_29.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS910);
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_30.data;
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_31.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS935,
  struct _M0TPB4Show _M0L8_2aparamS934
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS933 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS935;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS933, _M0L8_2aparamS934);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS932,
  struct _M0TPB4Show _M0L8_2aparamS931
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS930 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS932;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS930, _M0L8_2aparamS931);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS929,
  int32_t _M0L8_2aparamS928
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS927 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS929;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS927, _M0L8_2aparamS928);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS926,
  struct _M0TPC16string10StringView _M0L8_2aparamS925
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS924 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS926;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS924, _M0L8_2aparamS925);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS923,
  moonbit_string_t _M0L8_2aparamS920,
  int32_t _M0L8_2aparamS921,
  int32_t _M0L8_2aparamS922
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS919 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS923;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS919, _M0L8_2aparamS920, _M0L8_2aparamS921, _M0L8_2aparamS922);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS918,
  moonbit_string_t _M0L8_2aparamS917
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS916 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS918;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS916, _M0L8_2aparamS917);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_1984 = 9218868437227405311ll;
  int64_t _tmp_1985;
  int64_t _tmp_1986;
  int64_t _tmp_1987;
  int64_t _tmp_1988;
  _M0FPB18double__max__value = *(double*)&_tmp_1984;
  _tmp_1985 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_1985;
  _tmp_1986 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_1986;
  _tmp_1987 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_1987;
  _tmp_1988 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_1988;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS939;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS903;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS904;
  int32_t _M0L7_2abindS905;
  struct _M0TUsiE** _M0L7_2abindS906;
  int32_t _M0L6_2acntS1846;
  int32_t _M0L2__S907;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS939
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS903
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS903)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 80, 0);
  _M0L12async__testsS903->$0 = _M0L6_2atmpS939;
  _M0L12async__testsS903->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS904
  = _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS905 = _M0L7_2abindS904->$1;
  _M0L7_2abindS906 = _M0L7_2abindS904->$0;
  _M0L6_2acntS1846
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS904));
  if (_M0L6_2acntS1846 > 1) {
    int32_t _M0L11_2anew__cntS1847 = _M0L6_2acntS1846 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS904), _M0L11_2anew__cntS1847);
    moonbit_incref_cycle_free(_M0L7_2abindS906);
  } else if (_M0L6_2acntS1846 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS904);
  }
  _M0L2__S907 = 0;
  while (1) {
    if (_M0L2__S907 < _M0L7_2abindS905) {
      struct _M0TUsiE* _M0L3argS908 =
        (struct _M0TUsiE*)_M0L7_2abindS906[_M0L2__S907];
      moonbit_string_t _M0L6_2atmpS936 = _M0L3argS908->$0;
      int32_t _M0L6_2atmpS937 = _M0L3argS908->$1;
      int32_t _M0L6_2atmpS938;
      moonbit_incref_cycle_free(_M0L6_2atmpS936);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS903, _M0L6_2atmpS936, _M0L6_2atmpS937);
      moonbit_decref_cycle_free(_M0L6_2atmpS936);
      _M0L6_2atmpS938 = _M0L2__S907 + 1;
      _M0L2__S907 = _M0L6_2atmpS938;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS906);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\spikesynapse\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples28spikesynapse__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS903);
  moonbit_decref_cycle_free(_M0L12async__testsS903);
  moonbit_flush_cycles();
  return 0;
}