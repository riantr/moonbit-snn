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

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables;

struct _M0TUmmmmE;

struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TP26RiantR8snn__mbt4Time;

struct _M0DTPC15error5Error136RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__;

struct _M0TP26RiantR8snn__mbt11HHParameter;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__;

struct _M0TP26RiantR8snn__mbt14SpikingSynapse;

struct _M0TWERPC16option6OptionGfE;

struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__;

struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE;

struct _M0TPB4Show;

struct _M0TPB8MutLocalGfE;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind12MexicanHat__;

struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0DTP26RiantR8snn__mbt6AnyPop6AdEx__;

struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter;

struct _M0TP26RiantR8snn__mbt10AdExSinExp;

struct _M0TP26RiantR8snn__mbt13STDPSymmetric;

struct _M0TPB5ArrayGbE;

struct _M0DTPC15error5Error138RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__;

struct _M0TP26RiantR8snn__mbt11WilsonCowan;

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

struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2001;

struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric;

struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet;

struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1996;

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

struct _M0TPB4IterGfE;

struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep;

struct _M0BTPB6Logger;

struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind15AntiSymmetric__;

struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep;

struct _M0TP26RiantR8snn__mbt7Monitor;

struct _M0TP26RiantR8snn__mbt6HetRec;

struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

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

struct _M0TPB9ArrayViewGfE;

struct _M0TPB8MutLocalGiE;

struct _M0TP26RiantR8snn__mbt12STDPGerstner;

struct _M0TP26RiantR8snn__mbt11MorrisLecar;

struct _M0TP26RiantR8snn__mbt7Poisson;

struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0R58ArrayView_3a_3aiter_7c_5bFloat_5d_7c_2eanon__u2538__l885__;

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

struct _M0TPB8MutLocalGbE;

struct _M0TP26RiantR8snn__mbt12PoissonFixed;

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

struct _M0DTPC15error5Error136RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
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

struct _M0TWERPC16option6OptionGfE {
  void*(* code)(struct _M0TWERPC16option6OptionGfE*);
  
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

struct _M0DTPC15error5Error138RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2001 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
};

struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet {
  struct _M0TPB5ArrayGfE* $0;
  struct _M0TPB5ArrayGfE* $1;
  struct _M0TPB5ArrayGfE* $2;
  float $3;
  float $4;
  
};

struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1996 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0TPB4IterGfE {
  struct _M0TWERPC16option6OptionGfE* $0;
  int64_t $1;
  
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

struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

struct _M0TPB9ArrayViewGfE {
  float* $0;
  int32_t $1;
  int32_t $2;
  
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

struct _M0R58ArrayView_3a_3aiter_7c_5bFloat_5d_7c_2eanon__u2538__l885__ {
  void*(* code)(struct _M0TWERPC16option6OptionGfE*);
  struct _M0TPB9ArrayViewGfE $0;
  int32_t $1;
  struct _M0TPB8MutLocalGiE* $2;
  
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

struct _M0TPB8MutLocalGbE {
  int32_t $0;
  
};

struct _M0TP26RiantR8snn__mbt12PoissonFixed {
  float $0;
  float $1;
  struct _M0TPB5ArrayGbE* $2;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS2008(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS2001(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1996(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1973(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1966(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
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

int32_t _M0FP26RiantR8snn__mbt16spiking__connect(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse*,
  int32_t,
  int32_t,
  float
);

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse3new(
  struct _M0TP26RiantR8snn__mbt2IF*,
  struct _M0TP26RiantR8snn__mbt2IF*,
  moonbit_string_t
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

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3set(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*,
  int32_t,
  int32_t,
  float
);

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR5empty(
  int32_t,
  int32_t
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

struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0MP26RiantR8snn__mbt9STDPEntry3new(
  int32_t,
  int32_t,
  int32_t
);

struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0MP26RiantR8snn__mbt13STDPVariables3new(
  int32_t,
  int32_t
);

struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0MP26RiantR8snn__mbt12STDPGerstner3new(
  
);

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

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array6insertGiE(
  struct _M0TPB5ArrayGiE*,
  int32_t,
  int32_t
);

int32_t _M0MPC15array5Array6insertGfE(
  struct _M0TPB5ArrayGfE*,
  int32_t,
  float
);

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE*);

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE*);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*,
  int32_t
);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

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

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(uint64_t*, int32_t);

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(uint32_t*, int32_t);

int32_t _M0IPC15array5ArrayPB4Show6outputGfE(
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB6Logger
);

struct _M0TPB4IterGfE* _M0MPC15array5Array4iterGfE(struct _M0TPB5ArrayGfE*);

struct _M0TPB4IterGfE* _M0MPC15array9ArrayView4iterGfE(
  struct _M0TPB9ArrayViewGfE
);

void* _M0MPC15array9ArrayView4iterGfEC2538l885(
  struct _M0TWERPC16option6OptionGfE*
);

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

int32_t _M0MPC15array5Array4pushGfE(struct _M0TPB5ArrayGfE*, float);

int32_t _M0MPC15array5Array4pushGiE(struct _M0TPB5ArrayGiE*, int32_t);

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

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE*);

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*
);

moonbit_string_t* _M0MPC15array5Array6bufferGsE(struct _M0TPB5ArrayGsE*);

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE*
);

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE*);

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

struct _M0TPB4IterGfE* _M0MPB4Iter3newGfE(
  struct _M0TWERPC16option6OptionGfE*,
  int64_t
);

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

int32_t _M0MPB6Logger19write__iter_2einnerGfE(
  struct _M0TPB6Logger,
  struct _M0TPB4IterGfE*,
  moonbit_string_t,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

void* _M0MPB4Iter4nextGfE(struct _M0TPB4IterGfE*);

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGfE*
);

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

int32_t _M0IP016_24default__implPB4Show6outputGfE(
  float,
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

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t*);

int32_t _M0MPB18UninitializedArray6lengthGfE(float*);

int32_t _M0IPB7FailurePB4Show6output(void*, struct _M0TPB6Logger);

int32_t _M0MPB6Logger13write__objectGfE(struct _M0TPB6Logger, float);

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

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(moonbit_string_t);

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(moonbit_string_t);

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
} const moonbit_string_literal_29 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_27 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_22 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 44, 32, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_17 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[12]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 11, 44, 34, 
    109, 101, 115, 115, 97, 103, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[53]; 
} const moonbit_string_literal_43 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_26 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_24 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_18 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_16 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[25]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 24, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 114, 101, 115, 117, 108, 116, 34, 
    44, 34, 102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[124]; 
} const moonbit_string_literal_41 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 123, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 115, 116, 100, 112, 95, 99, 111, 
    109, 112, 111, 115, 101, 95, 100, 101, 109, 111, 95, 98, 108, 97, 
    99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 
    110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 
    73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 
    115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 
    68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 
    83, 107, 105, 112, 84, 101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_14 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_36 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_30 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_9 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[122]; 
} const moonbit_string_literal_42 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 121, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 115, 116, 100, 112, 95, 99, 111, 
    109, 112, 111, 115, 101, 95, 100, 101, 109, 111, 95, 98, 108, 97, 
    99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 
    110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 
    73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 
    114, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 
    114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 
    115, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_21 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 93, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_39 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_20 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 91, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_25 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_28 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[43]; 
} const moonbit_string_literal_12 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 42, 105, 110, 
    100, 101, 120, 32, 111, 117, 116, 32, 111, 102, 32, 98, 111, 117, 
    110, 100, 115, 58, 32, 116, 104, 101, 32, 108, 101, 110, 32, 105, 
    115, 32, 102, 114, 111, 109, 32, 48, 32, 116, 111, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_11 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 102, 105, 
    114, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_40 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 73, 110, 115, 112, 101, 
    99, 116, 69, 114, 114, 111, 114, 46, 73, 110, 115, 112, 101, 99, 
    116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_37 =
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
} const moonbit_string_literal_34 =
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
} const moonbit_string_literal_23 =
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
} const moonbit_string_literal_15 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_13 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 98, 
    117, 116, 32, 116, 104, 101, 32, 105, 110, 100, 101, 120, 32, 105, 
    115, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_32 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_19 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct moonbit_object const moonbit_constant_constructor_0 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0)
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS2008$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS2008
  };

uint32_t const moonbit_layout_table_data[127] =
  {
    sizeof(struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1996)
    / 4, 1,
    offsetof(struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1996, $1)
    / 4
    * 2,
    sizeof(struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2001)
    / 4, 1,
    offsetof(struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2001, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error138RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error138RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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
    sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR) / 4, 3,
    offsetof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR, $4) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt9STDPEntry) / 4, 3,
    offsetof(struct _M0TP26RiantR8snn__mbt9STDPEntry, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9STDPEntry, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9STDPEntry, $5) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGbE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGbE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt13STDPVariables) / 4, 5,
    offsetof(struct _M0TP26RiantR8snn__mbt13STDPVariables, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt13STDPVariables, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt13STDPVariables, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt13STDPVariables, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt13STDPVariables, $4) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt4Time) / 4, 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4Time, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4Time, $1) / 4 * 2,
    sizeof(struct _M0TPB9ArrayViewGfE) / 4, 1,
    offsetof(struct _M0TPB9ArrayViewGfE, $0) / 4 * 2,
    sizeof(struct _M0R58ArrayView_3a_3aiter_7c_5bFloat_5d_7c_2eanon__u2538__l885__)
    / 4, 2,
    (offsetof(struct _M0R58ArrayView_3a_3aiter_7c_5bFloat_5d_7c_2eanon__u2538__l885__, $0)
     + offsetof(struct _M0TPB9ArrayViewGfE, $0))
    / 4
    * 2,
    offsetof(struct _M0R58ArrayView_3a_3aiter_7c_5bFloat_5d_7c_2eanon__u2538__l885__, $2)
    / 4
    * 2, sizeof(struct _M0TPB4IterGfE) / 4, 1,
    offsetof(struct _M0TPB4IterGfE, $0) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int64_t _M0MPB4Iter4nextN6constrS10984GfE = 0ll;

int64_t _M0MPB4Iter4nextN6constrS10985GfE = 0ll;

int64_t _M0MPB4Iter3newN6constrS10992GfE = 0ll;

double _M0FPC16double14not__a__number;

double _M0FPC16double13neg__infinity;

double _M0FPC16double13min__positive;

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS5615
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS2029,
  moonbit_string_t _M0L8filenameS1998,
  int32_t _M0L5indexS2000
) {
  struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1996* _closure_5820;
  struct _M0TWEu* _M0L13handle__startS1996;
  struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2001* _closure_5821;
  struct _M0TWssbEu* _M0L14handle__resultS2001;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS2008;
  void* _M0L11_2atry__errS2023;
  struct moonbit_result_0 _tmp_5823;
  int32_t _handle__error__result_5824;
  int32_t _M0L6_2atmpS5603;
  void* _M0L3errS2024;
  moonbit_string_t _M0L4nameS2026;
  struct _M0DTPC15error5Error138RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS2027;
  moonbit_string_t _M0L7_2anameS2028;
  int32_t _M0L6_2acntS5642;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1998);
  _closure_5820
  = (struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1996*)moonbit_malloc(sizeof(struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1996));
  Moonbit_object_header(_closure_5820)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_5820->code
  = &_M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1996;
  _closure_5820->$0 = _M0L5indexS2000;
  _closure_5820->$1 = _M0L8filenameS1998;
  _M0L13handle__startS1996 = (struct _M0TWEu*)_closure_5820;
  moonbit_incref_cycle_free(_M0L8filenameS1998);
  _closure_5821
  = (struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2001*)moonbit_malloc(sizeof(struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2001));
  Moonbit_object_header(_closure_5821)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_5821->code
  = &_M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS2001;
  _closure_5821->$0 = _M0L5indexS2000;
  _closure_5821->$1 = _M0L8filenameS1998;
  _M0L14handle__resultS2001 = (struct _M0TWssbEu*)_closure_5821;
  _M0L17error__to__stringS2008
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS2008$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _tmp_5823
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS2029, _M0L8filenameS1998, _M0L5indexS2000, _M0L13handle__startS1996, _M0L14handle__resultS2001, _M0L17error__to__stringS2008);
  if (_tmp_5823.tag) {
    int32_t const _M0L5_2aokS5612 = _tmp_5823.data.ok;
    _handle__error__result_5824 = _M0L5_2aokS5612;
  } else {
    void* const _M0L6_2aerrS5613 = _tmp_5823.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS2008);
    moonbit_decref_cycle_free(_M0L13handle__startS1996);
    _M0L11_2atry__errS2023 = _M0L6_2aerrS5613;
    goto join_2022;
  }
  if (_handle__error__result_5824) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS2008);
    moonbit_decref_cycle_free(_M0L13handle__startS1996);
    _M0L6_2atmpS5603 = 1;
  } else {
    struct moonbit_result_0 _tmp_5825;
    int32_t _handle__error__result_5826;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
    _tmp_5825
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS2029, _M0L8filenameS1998, _M0L5indexS2000, _M0L13handle__startS1996, _M0L14handle__resultS2001, _M0L17error__to__stringS2008);
    if (_tmp_5825.tag) {
      int32_t const _M0L5_2aokS5610 = _tmp_5825.data.ok;
      _handle__error__result_5826 = _M0L5_2aokS5610;
    } else {
      void* const _M0L6_2aerrS5611 = _tmp_5825.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS2008);
      moonbit_decref_cycle_free(_M0L13handle__startS1996);
      _M0L11_2atry__errS2023 = _M0L6_2aerrS5611;
      goto join_2022;
    }
    if (_handle__error__result_5826) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS2008);
      moonbit_decref_cycle_free(_M0L13handle__startS1996);
      _M0L6_2atmpS5603 = 1;
    } else {
      struct moonbit_result_0 _tmp_5827;
      int32_t _handle__error__result_5828;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
      _tmp_5827
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS2029, _M0L8filenameS1998, _M0L5indexS2000, _M0L13handle__startS1996, _M0L14handle__resultS2001, _M0L17error__to__stringS2008);
      if (_tmp_5827.tag) {
        int32_t const _M0L5_2aokS5608 = _tmp_5827.data.ok;
        _handle__error__result_5828 = _M0L5_2aokS5608;
      } else {
        void* const _M0L6_2aerrS5609 = _tmp_5827.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS2008);
        moonbit_decref_cycle_free(_M0L13handle__startS1996);
        _M0L11_2atry__errS2023 = _M0L6_2aerrS5609;
        goto join_2022;
      }
      if (_handle__error__result_5828) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS2008);
        moonbit_decref_cycle_free(_M0L13handle__startS1996);
        _M0L6_2atmpS5603 = 1;
      } else {
        struct moonbit_result_0 _tmp_5829;
        int32_t _handle__error__result_5830;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
        _tmp_5829
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS2029, _M0L8filenameS1998, _M0L5indexS2000, _M0L13handle__startS1996, _M0L14handle__resultS2001, _M0L17error__to__stringS2008);
        if (_tmp_5829.tag) {
          int32_t const _M0L5_2aokS5606 = _tmp_5829.data.ok;
          _handle__error__result_5830 = _M0L5_2aokS5606;
        } else {
          void* const _M0L6_2aerrS5607 = _tmp_5829.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS2008);
          moonbit_decref_cycle_free(_M0L13handle__startS1996);
          _M0L11_2atry__errS2023 = _M0L6_2aerrS5607;
          goto join_2022;
        }
        if (_handle__error__result_5830) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS2008);
          moonbit_decref_cycle_free(_M0L13handle__startS1996);
          _M0L6_2atmpS5603 = 1;
        } else {
          struct moonbit_result_0 _tmp_5831;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
          _tmp_5831
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS2029, _M0L8filenameS1998, _M0L5indexS2000, _M0L13handle__startS1996, _M0L14handle__resultS2001, _M0L17error__to__stringS2008);
          moonbit_decref_cycle_free(_M0L13handle__startS1996);
          moonbit_decref_cycle_free(_M0L17error__to__stringS2008);
          if (_tmp_5831.tag) {
            int32_t const _M0L5_2aokS5604 = _tmp_5831.data.ok;
            _M0L6_2atmpS5603 = _M0L5_2aokS5604;
          } else {
            void* const _M0L6_2aerrS5605 = _tmp_5831.data.err;
            _M0L11_2atry__errS2023 = _M0L6_2aerrS5605;
            goto join_2022;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS5603) {
    void* _M0L138RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5614 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error138RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L138RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5614)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error138RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L138RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5614)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS2023
    = _M0L138RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5614;
    goto join_2022;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS2001);
  }
  goto joinlet_5822;
  join_2022:;
  _M0L3errS2024 = _M0L11_2atry__errS2023;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS2027
  = (struct _M0DTPC15error5Error138RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS2024;
  _M0L7_2anameS2028 = _M0L36_2aMoonBitTestDriverInternalSkipTestS2027->$0;
  _M0L6_2acntS5642
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS2027));
  if (_M0L6_2acntS5642 > 1) {
    int32_t _M0L11_2anew__cntS5643 = _M0L6_2acntS5642 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS2027), _M0L11_2anew__cntS5643);
    moonbit_incref_cycle_free(_M0L7_2anameS2028);
  } else if (_M0L6_2acntS5642 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS2027);
  }
  _M0L4nameS2026 = _M0L7_2anameS2028;
  goto join_2025;
  goto joinlet_5832;
  join_2025:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS2001(_M0L14handle__resultS2001, _M0L4nameS2026, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS2001);
  moonbit_decref_cycle_free(_M0L4nameS2026);
  joinlet_5832:;
  joinlet_5822:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS2008(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS5602,
  void* _M0L3errS2009
) {
  void* _M0L1eS2011;
  moonbit_string_t _M0L1eS2013;
  moonbit_string_t _result_5835;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS2009)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS2014 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS2009;
      moonbit_string_t _M0L4_2aeS2015 = _M0L10_2aFailureS2014->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS2015);
      _M0L1eS2013 = _M0L4_2aeS2015;
      goto join_2012;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS2016 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS2009;
      moonbit_string_t _M0L4_2aeS2017 = _M0L15_2aInspectErrorS2016->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS2017);
      _M0L1eS2013 = _M0L4_2aeS2017;
      goto join_2012;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS2018 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS2009;
      moonbit_string_t _M0L4_2aeS2019 = _M0L16_2aSnapshotErrorS2018->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS2019);
      _M0L1eS2013 = _M0L4_2aeS2019;
      goto join_2012;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error136RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS2020 =
        (struct _M0DTPC15error5Error136RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS2009;
      moonbit_string_t _M0L4_2aeS2021 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS2020->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS2021);
      _M0L1eS2013 = _M0L4_2aeS2021;
      goto join_2012;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS2009);
      _M0L1eS2011 = _M0L3errS2009;
      goto join_2010;
      break;
    }
  }
  join_2012:;
  return _M0L1eS2013;
  join_2010:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_5835 = _M0FP15Error10to__string(_M0L1eS2011);
  moonbit_decref_cycle_free(_M0L1eS2011);
  return _result_5835;
}

int32_t _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS2001(
  struct _M0TWssbEu* _M0L6_2aenvS5599,
  moonbit_string_t _M0L10__testnameS2002,
  moonbit_string_t _M0L7messageS2003,
  int32_t _M0L7skippedS2004
) {
  struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2001* _M0L14_2acasted__envS5600;
  moonbit_string_t _M0L8filenameS1998;
  int32_t _M0L5indexS2000;
  moonbit_string_t _M0L10file__nameS2005;
  moonbit_string_t _M0L7messageS2006;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS2007;
  moonbit_string_t _M0L6_2atmpS5601;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS5600
  = (struct _M0R140_24RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c2001*)_M0L6_2aenvS5599;
  _M0L8filenameS1998 = _M0L14_2acasted__envS5600->$1;
  _M0L5indexS2000 = _M0L14_2acasted__envS5600->$0;
  if (!_M0L7skippedS2004 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS2005
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1998, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS2006
  = _M0MPC16string6String14escape_2einner(_M0L7messageS2003, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS2007
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2007, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS2007, _M0L10file__nameS2005);
  moonbit_decref_cycle_free(_M0L10file__nameS2005);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2007, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS2007, _M0L5indexS2000);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2007, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS2007, _M0L7messageS2006);
  moonbit_decref_cycle_free(_M0L7messageS2006);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS2007, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5601
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS2007);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS2007);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS5601);
  moonbit_decref_cycle_free(_M0L6_2atmpS5601);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1996(
  struct _M0TWEu* _M0L6_2aenvS5596
) {
  struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1996* _M0L14_2acasted__envS5597;
  moonbit_string_t _M0L8filenameS1998;
  int32_t _M0L5indexS2000;
  moonbit_string_t _M0L10file__nameS1997;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1999;
  moonbit_string_t _M0L6_2atmpS5598;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS5597
  = (struct _M0R139_24RiantR_2fsnn__mbt_2fexamples_2fstdp__compose__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1996*)_M0L6_2aenvS5596;
  _M0L8filenameS1998 = _M0L14_2acasted__envS5597->$1;
  _M0L5indexS2000 = _M0L14_2acasted__envS5597->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1997
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1998, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1999
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1999, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1999, _M0L10file__nameS1997);
  moonbit_decref_cycle_free(_M0L10file__nameS1997);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1999, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1999, _M0L5indexS2000);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1999, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5598
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1999);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1999);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS5598);
  moonbit_decref_cycle_free(_M0L6_2atmpS5598);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1966;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1973;
  struct _M0TUsiE** _M0L6_2atmpS5595;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1980;
  moonbit_string_t* _M0L9cli__argsS1981;
  moonbit_string_t _M0L6_2atmpS5594;
  moonbit_string_t _M0L6_2atmpS5593;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1982;
  int32_t _M0L7_2abindS1983;
  moonbit_string_t* _M0L7_2abindS1984;
  int32_t _M0L6_2acntS5644;
  int32_t _M0L2__S1985;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1966 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1973 = 0;
  _M0L6_2atmpS5595 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1980
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1980)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1980->$0 = _M0L6_2atmpS5595;
  _M0L16file__and__indexS1980->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1981
  = _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1981)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS5594 = (moonbit_string_t)_M0L9cli__argsS1981[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS5594);
  moonbit_decref_cycle_free(_M0L9cli__argsS1981);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5593
  = _M0MP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS5594);
  moonbit_decref_cycle_free(_M0L6_2atmpS5594);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1982
  = _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1973(_M0L51moonbit__test__driver__internal__split__mbt__stringS1973, _M0L6_2atmpS5593, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS5593);
  _M0L7_2abindS1983 = _M0L10test__argsS1982->$1;
  _M0L7_2abindS1984 = _M0L10test__argsS1982->$0;
  _M0L6_2acntS5644
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1982));
  if (_M0L6_2acntS5644 > 1) {
    int32_t _M0L11_2anew__cntS5645 = _M0L6_2acntS5644 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1982), _M0L11_2anew__cntS5645);
    moonbit_incref_cycle_free(_M0L7_2abindS1984);
  } else if (_M0L6_2acntS5644 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1982);
  }
  _M0L2__S1985 = 0;
  while (1) {
    if (_M0L2__S1985 < _M0L7_2abindS1983) {
      moonbit_string_t _M0L3argS1986 =
        (moonbit_string_t)_M0L7_2abindS1984[_M0L2__S1985];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1987;
      moonbit_string_t _M0L4fileS1988;
      moonbit_string_t _M0L5rangeS1989;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1990;
      moonbit_string_t _M0L6_2atmpS5591;
      int32_t _M0L5startS1991;
      moonbit_string_t _M0L6_2atmpS5590;
      int32_t _M0L3endS1992;
      int32_t _M0L1iS1993;
      int32_t _M0L6_2atmpS5592;
      moonbit_incref_cycle_free(_M0L3argS1986);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1987
      = _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1973(_M0L51moonbit__test__driver__internal__split__mbt__stringS1973, _M0L3argS1986, 58);
      moonbit_decref_cycle_free(_M0L3argS1986);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1988
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1987, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1989
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1987, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1987);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1990
      = _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1973(_M0L51moonbit__test__driver__internal__split__mbt__stringS1973, _M0L5rangeS1989, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1989);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS5591
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1990, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1991
      = _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1966(_M0L45moonbit__test__driver__internal__parse__int__S1966, _M0L6_2atmpS5591);
      moonbit_decref_cycle_free(_M0L6_2atmpS5591);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS5590
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1990, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1990);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1992
      = _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1966(_M0L45moonbit__test__driver__internal__parse__int__S1966, _M0L6_2atmpS5590);
      moonbit_decref_cycle_free(_M0L6_2atmpS5590);
      _M0L1iS1993 = _M0L5startS1991;
      while (1) {
        if (_M0L1iS1993 < _M0L3endS1992) {
          struct _M0TUsiE* _M0L8_2atupleS5588;
          int32_t _M0L6_2atmpS5589;
          moonbit_incref_cycle_free(_M0L4fileS1988);
          _M0L8_2atupleS5588
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS5588)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS5588->$0 = _M0L4fileS1988;
          _M0L8_2atupleS5588->$1 = _M0L1iS1993;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1980, _M0L8_2atupleS5588);
          _M0L6_2atmpS5589 = _M0L1iS1993 + 1;
          _M0L1iS1993 = _M0L6_2atmpS5589;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1988);
        }
        break;
      }
      _M0L6_2atmpS5592 = _M0L2__S1985 + 1;
      _M0L2__S1985 = _M0L6_2atmpS5592;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1984);
    }
    break;
  }
  return _M0L16file__and__indexS1980;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1973(
  int32_t _M0L6_2aenvS5569,
  moonbit_string_t _M0L1sS1974,
  int32_t _M0L3sepS1975
) {
  moonbit_string_t* _M0L6_2atmpS5587;
  struct _M0TPB5ArrayGsE* _M0L3resS1976;
  struct _M0TPB8MutLocalGiE* _M0L1iS1977;
  struct _M0TPB8MutLocalGiE* _M0L5startS1978;
  int32_t _M0L3valS5582;
  int32_t _M0L6_2atmpS5583;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5587 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1976
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1976)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1976->$0 = _M0L6_2atmpS5587;
  _M0L3resS1976->$1 = 0;
  _M0L1iS1977
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1977)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1977->$0 = 0;
  _M0L5startS1978
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1978)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1978->$0 = 0;
  while (1) {
    int32_t _M0L3valS5570 = _M0L1iS1977->$0;
    int32_t _M0L6_2atmpS5571 = Moonbit_array_length(_M0L1sS1974);
    if (_M0L3valS5570 < _M0L6_2atmpS5571) {
      int32_t _M0L3valS5574 = _M0L1iS1977->$0;
      int32_t _M0L6_2atmpS5573;
      int32_t _M0L6_2atmpS5572;
      int32_t _M0L3valS5581;
      int32_t _M0L6_2atmpS5580;
      if (
        _M0L3valS5574 < 0
        || _M0L3valS5574 >= Moonbit_array_length(_M0L1sS1974)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS5573 = _M0L1sS1974[_M0L3valS5574];
      _M0L6_2atmpS5572 = _M0L6_2atmpS5573;
      if (_M0L6_2atmpS5572 == _M0L3sepS1975) {
        int32_t _M0L3valS5576 = _M0L5startS1978->$0;
        int32_t _M0L3valS5577 = _M0L1iS1977->$0;
        moonbit_string_t _M0L6_2atmpS5575;
        int32_t _M0L3valS5579;
        int32_t _M0L6_2atmpS5578;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS5575
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1974, _M0L3valS5576, _M0L3valS5577);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1976, _M0L6_2atmpS5575);
        _M0L3valS5579 = _M0L1iS1977->$0;
        _M0L6_2atmpS5578 = _M0L3valS5579 + 1;
        _M0L5startS1978->$0 = _M0L6_2atmpS5578;
      }
      _M0L3valS5581 = _M0L1iS1977->$0;
      _M0L6_2atmpS5580 = _M0L3valS5581 + 1;
      _M0L1iS1977->$0 = _M0L6_2atmpS5580;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1977);
    }
    break;
  }
  _M0L3valS5582 = _M0L5startS1978->$0;
  _M0L6_2atmpS5583 = Moonbit_array_length(_M0L1sS1974);
  if (_M0L3valS5582 < _M0L6_2atmpS5583) {
    int32_t _M0L3valS5585 = _M0L5startS1978->$0;
    int32_t _M0L6_2atmpS5586;
    moonbit_string_t _M0L6_2atmpS5584;
    moonbit_decref_cycle_free(_M0L5startS1978);
    _M0L6_2atmpS5586 = Moonbit_array_length(_M0L1sS1974);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS5584
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1974, _M0L3valS5585, _M0L6_2atmpS5586);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1976, _M0L6_2atmpS5584);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1978);
  }
  return _M0L3resS1976;
}

int32_t _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1966(
  int32_t _M0L6_2aenvS5562,
  moonbit_string_t _M0L1sS1967
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1968;
  int32_t _M0L3lenS1969;
  int32_t _M0L7_2abindS1970;
  int32_t _M0L1iS1971;
  int32_t _result_5840;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1968
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1968)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1968->$0 = 0;
  _M0L3lenS1969 = Moonbit_array_length(_M0L1sS1967);
  _M0L7_2abindS1970 = 0;
  _M0L1iS1971 = _M0L7_2abindS1970;
  while (1) {
    if (_M0L1iS1971 < _M0L3lenS1969) {
      int32_t _M0L3valS5567 = _M0L3resS1968->$0;
      int32_t _M0L6_2atmpS5564 = _M0L3valS5567 * 10;
      int32_t _M0L6_2atmpS5566;
      int32_t _M0L6_2atmpS5565;
      int32_t _M0L6_2atmpS5563;
      int32_t _M0L6_2atmpS5568;
      if (
        _M0L1iS1971 < 0 || _M0L1iS1971 >= Moonbit_array_length(_M0L1sS1967)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS5566 = _M0L1sS1967[_M0L1iS1971];
      _M0L6_2atmpS5565 = _M0L6_2atmpS5566 - 48;
      _M0L6_2atmpS5563 = _M0L6_2atmpS5564 + _M0L6_2atmpS5565;
      _M0L3resS1968->$0 = _M0L6_2atmpS5563;
      _M0L6_2atmpS5568 = _M0L1iS1971 + 1;
      _M0L1iS1971 = _M0L6_2atmpS5568;
      continue;
    }
    break;
  }
  _result_5840 = _M0L3resS1968->$0;
  moonbit_decref_cycle_free(_M0L3resS1968);
  return _result_5840;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1965
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1965);
  return _M0L4selfS1965;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1935,
  moonbit_string_t _M0L12_2adiscard__S1936,
  int32_t _M0L12_2adiscard__S1937,
  struct _M0TWEu* _M0L12_2adiscard__S1938,
  struct _M0TWssbEu* _M0L12_2adiscard__S1939,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1940
) {
  struct moonbit_result_0 _result_5841;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_5841.tag = 1;
  _result_5841.data.ok = 0;
  return _result_5841;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1941,
  moonbit_string_t _M0L12_2adiscard__S1942,
  int32_t _M0L12_2adiscard__S1943,
  struct _M0TWEu* _M0L12_2adiscard__S1944,
  struct _M0TWssbEu* _M0L12_2adiscard__S1945,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1946
) {
  struct moonbit_result_0 _result_5842;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_5842.tag = 1;
  _result_5842.data.ok = 0;
  return _result_5842;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1947,
  moonbit_string_t _M0L12_2adiscard__S1948,
  int32_t _M0L12_2adiscard__S1949,
  struct _M0TWEu* _M0L12_2adiscard__S1950,
  struct _M0TWssbEu* _M0L12_2adiscard__S1951,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1952
) {
  struct moonbit_result_0 _result_5843;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_5843.tag = 1;
  _result_5843.data.ok = 0;
  return _result_5843;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1953,
  moonbit_string_t _M0L12_2adiscard__S1954,
  int32_t _M0L12_2adiscard__S1955,
  struct _M0TWEu* _M0L12_2adiscard__S1956,
  struct _M0TWssbEu* _M0L12_2adiscard__S1957,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1958
) {
  struct moonbit_result_0 _result_5844;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_5844.tag = 1;
  _result_5844.data.ok = 0;
  return _result_5844;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1959,
  moonbit_string_t _M0L12_2adiscard__S1960,
  int32_t _M0L12_2adiscard__S1961,
  struct _M0TWEu* _M0L12_2adiscard__S1962,
  struct _M0TWssbEu* _M0L12_2adiscard__S1963,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1964
) {
  struct moonbit_result_0 _result_5845;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_5845.tag = 1;
  _result_5845.data.ok = 0;
  return _result_5845;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1934
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt19step__heterogeneous(
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0L1mS1812,
  float _M0L2dtS1817
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L7_2abindS1811;
  int32_t _M0L7_2abindS1813;
  void** _M0L7_2abindS1814;
  int32_t _M0L2__S1815;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS1819;
  int32_t _M0L7_2abindS1820;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS1821;
  int32_t _M0L2__S1822;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L7_2abindS1825;
  int32_t _M0L7_2abindS1826;
  void** _M0L7_2abindS1827;
  int32_t _M0L2__S1828;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS1846;
  int32_t _M0L7_2abindS1847;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS1848;
  int32_t _M0L2__S1849;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L7_2abindS1852;
  int32_t _M0L7_2abindS1853;
  void** _M0L7_2abindS1854;
  int32_t _M0L2__S1855;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L7_2abindS1898;
  int32_t _M0L7_2abindS1899;
  void** _M0L7_2abindS1900;
  int32_t _M0L2__S1901;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS1904;
  int32_t _M0L7_2abindS1905;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS1906;
  int32_t _M0L2__S1907;
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5561;
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L7_2abindS1811 = _M0L1mS1812->$2;
  _M0L7_2abindS1813 = _M0L7_2abindS1811->$1;
  _M0L7_2abindS1814 = _M0L7_2abindS1811->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1814);
  _M0L2__S1815 = 0;
  while (1) {
    if (_M0L2__S1815 < _M0L7_2abindS1813) {
      void* _M0L1sS1816 = (void*)_M0L7_2abindS1814[_M0L2__S1815];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5370 = _M0L1mS1812->$3;
      int32_t _M0L6_2atmpS5371;
      moonbit_incref_cycle_free(_M0L1sS1816);
      #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt14stimulate__any(_M0L1sS1816, _M0L4timeS5370, _M0L2dtS1817);
      moonbit_decref_cycle_free(_M0L1sS1816);
      _M0L6_2atmpS5371 = _M0L2__S1815 + 1;
      _M0L2__S1815 = _M0L6_2atmpS5371;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1814);
    }
    break;
  }
  _M0L7_2abindS1819 = _M0L1mS1812->$1;
  _M0L7_2abindS1820 = _M0L7_2abindS1819->$1;
  _M0L7_2abindS1821 = _M0L7_2abindS1819->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1821);
  _M0L2__S1822 = 0;
  while (1) {
    if (_M0L2__S1822 < _M0L7_2abindS1820) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1823 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS1821[
          _M0L2__S1822
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5373 = _M0L1mS1812->$3;
      float _M0L6_2atmpS5372;
      int32_t _M0L6_2atmpS5374;
      moonbit_incref_cycle_free(_M0L1cS1823);
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5372 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5373);
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt25deliver__pending__synapse(_M0L1cS1823, _M0L6_2atmpS5372);
      moonbit_decref_cycle_free(_M0L1cS1823);
      _M0L6_2atmpS5374 = _M0L2__S1822 + 1;
      _M0L2__S1822 = _M0L6_2atmpS5374;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1821);
    }
    break;
  }
  _M0L7_2abindS1825 = _M0L1mS1812->$6;
  _M0L7_2abindS1826 = _M0L7_2abindS1825->$1;
  _M0L7_2abindS1827 = _M0L7_2abindS1825->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1827);
  _M0L2__S1828 = 0;
  while (1) {
    if (_M0L2__S1828 < _M0L7_2abindS1826) {
      void* _M0L5entryS1829 = (void*)_M0L7_2abindS1827[_M0L2__S1828];
      struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _M0L1eS1831;
      struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet* _M0L1eS1834;
      struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry* _M0L1eS1837;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5391;
      int32_t _M0L11conn__indexS5392;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1838;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS5387;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter* _M0L5paramS5388;
      int32_t _M0L6_2acntS5650;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5390;
      float _M0L6_2atmpS5389;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5385;
      int32_t _M0L11conn__indexS5386;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1835;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS5381;
      struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet* _M0L5paramS5382;
      int32_t _M0L6_2acntS5648;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5384;
      float _M0L6_2atmpS5383;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5379;
      int32_t _M0L11conn__indexS5380;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1832;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS5375;
      struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0L5paramS5376;
      int32_t _M0L6_2acntS5646;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5378;
      float _M0L6_2atmpS5377;
      int32_t _M0L6_2atmpS5393;
      switch (Moonbit_object_tag(_M0L5entryS1829)) {
        case 0: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__* _M0L15_2aMarkramSTP__S1839 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__*)_M0L5entryS1829;
          struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry* _M0L4_2aeS1840 =
            _M0L15_2aMarkramSTP__S1839->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1840);
          _M0L1eS1837 = _M0L4_2aeS1840;
          goto join_1836;
          break;
        }
        
        case 1: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__* _M0L18_2aMarkramSTPHet__S1841 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__*)_M0L5entryS1829;
          struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet* _M0L4_2aeS1842 =
            _M0L18_2aMarkramSTPHet__S1841->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1842);
          _M0L1eS1834 = _M0L4_2aeS1842;
          goto join_1833;
          break;
        }
        default: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind20MarkramSTPTimestep__* _M0L23_2aMarkramSTPTimestep__S1843 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind20MarkramSTPTimestep__*)_M0L5entryS1829;
          struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _M0L4_2aeS1844 =
            _M0L23_2aMarkramSTPTimestep__S1843->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1844);
          _M0L1eS1831 = _M0L4_2aeS1844;
          goto join_1830;
          break;
        }
      }
      goto joinlet_5851;
      join_1836:;
      _M0L5connsS5391 = _M0L1mS1812->$1;
      _M0L11conn__indexS5392 = _M0L1eS1837->$0;
      #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1838
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5391, _M0L11conn__indexS5392);
      _M0L4varsS5387 = _M0L1eS1837->$1;
      _M0L5paramS5388 = _M0L1eS1837->$2;
      _M0L6_2acntS5650 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1837));
      if (_M0L6_2acntS5650 > 1) {
        int32_t _M0L11_2anew__cntS5651 = _M0L6_2acntS5650 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1837), _M0L11_2anew__cntS5651);
        moonbit_incref_cycle_free(_M0L5paramS5388);
        moonbit_incref_cycle_free(_M0L4varsS5387);
      } else if (_M0L6_2acntS5650 == 1) {
        #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1837);
      }
      _M0L4timeS5390 = _M0L1mS1812->$3;
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5389 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5390);
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt18markram__stp__step(_M0L3synS1838, _M0L4varsS5387, _M0L5paramS5388, _M0L6_2atmpS5389);
      moonbit_decref_cycle_free(_M0L3synS1838);
      moonbit_decref_cycle_free(_M0L4varsS5387);
      moonbit_decref_cycle_free(_M0L5paramS5388);
      joinlet_5851:;
      goto joinlet_5850;
      join_1833:;
      _M0L5connsS5385 = _M0L1mS1812->$1;
      _M0L11conn__indexS5386 = _M0L1eS1834->$0;
      #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1835
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5385, _M0L11conn__indexS5386);
      _M0L4varsS5381 = _M0L1eS1834->$1;
      _M0L5paramS5382 = _M0L1eS1834->$2;
      _M0L6_2acntS5648 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1834));
      if (_M0L6_2acntS5648 > 1) {
        int32_t _M0L11_2anew__cntS5649 = _M0L6_2acntS5648 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1834), _M0L11_2anew__cntS5649);
        moonbit_incref_cycle_free(_M0L5paramS5382);
        moonbit_incref_cycle_free(_M0L4varsS5381);
      } else if (_M0L6_2acntS5648 == 1) {
        #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1834);
      }
      _M0L4timeS5384 = _M0L1mS1812->$3;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5383 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5384);
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt23markram__stp__step__het(_M0L3synS1835, _M0L4varsS5381, _M0L5paramS5382, _M0L6_2atmpS5383);
      moonbit_decref_cycle_free(_M0L3synS1835);
      moonbit_decref_cycle_free(_M0L4varsS5381);
      moonbit_decref_cycle_free(_M0L5paramS5382);
      joinlet_5850:;
      goto joinlet_5849;
      join_1830:;
      _M0L5connsS5379 = _M0L1mS1812->$1;
      _M0L11conn__indexS5380 = _M0L1eS1831->$0;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1832
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5379, _M0L11conn__indexS5380);
      _M0L4varsS5375 = _M0L1eS1831->$1;
      _M0L5paramS5376 = _M0L1eS1831->$2;
      _M0L6_2acntS5646 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1831));
      if (_M0L6_2acntS5646 > 1) {
        int32_t _M0L11_2anew__cntS5647 = _M0L6_2acntS5646 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1831), _M0L11_2anew__cntS5647);
        moonbit_incref_cycle_free(_M0L5paramS5376);
        moonbit_incref_cycle_free(_M0L4varsS5375);
      } else if (_M0L6_2acntS5646 == 1) {
        #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1831);
      }
      _M0L4timeS5378 = _M0L1mS1812->$3;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5377 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5378);
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt28markram__stp__step__timestep(_M0L3synS1832, _M0L4varsS5375, _M0L5paramS5376, _M0L6_2atmpS5377, _M0L2dtS1817);
      moonbit_decref_cycle_free(_M0L3synS1832);
      moonbit_decref_cycle_free(_M0L4varsS5375);
      moonbit_decref_cycle_free(_M0L5paramS5376);
      joinlet_5849:;
      _M0L6_2atmpS5393 = _M0L2__S1828 + 1;
      _M0L2__S1828 = _M0L6_2atmpS5393;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1827);
    }
    break;
  }
  _M0L7_2abindS1846 = _M0L1mS1812->$1;
  _M0L7_2abindS1847 = _M0L7_2abindS1846->$1;
  _M0L7_2abindS1848 = _M0L7_2abindS1846->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1848);
  _M0L2__S1849 = 0;
  while (1) {
    if (_M0L2__S1849 < _M0L7_2abindS1847) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1850 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS1848[
          _M0L2__S1849
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5395 = _M0L1mS1812->$3;
      float _M0L6_2atmpS5394;
      int32_t _M0L6_2atmpS5396;
      moonbit_incref_cycle_free(_M0L1cS1850);
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5394 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5395);
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt16forward__synapse(_M0L1cS1850, _M0L6_2atmpS5394);
      moonbit_decref_cycle_free(_M0L1cS1850);
      _M0L6_2atmpS5396 = _M0L2__S1849 + 1;
      _M0L2__S1849 = _M0L6_2atmpS5396;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1848);
    }
    break;
  }
  _M0L7_2abindS1852 = _M0L1mS1812->$5;
  _M0L7_2abindS1853 = _M0L7_2abindS1852->$1;
  _M0L7_2abindS1854 = _M0L7_2abindS1852->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1854);
  _M0L2__S1855 = 0;
  while (1) {
    if (_M0L2__S1855 < _M0L7_2abindS1853) {
      void* _M0L5entryS1856 = (void*)_M0L7_2abindS1854[_M0L2__S1855];
      struct _M0TP26RiantR8snn__mbt17CaPlasticityEntry* _M0L1eS1858;
      struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric* _M0L1eS1861;
      struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0L1eS1864;
      struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0L1eS1867;
      struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025* _M0L1eS1870;
      struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric* _M0L1eS1873;
      struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat* _M0L1eS1876;
      struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0L1eS1879;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5554;
      int32_t _M0L11conn__indexS5555;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1880;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5549;
      struct _M0TPB5ArrayGfE* _M0L4valsS5536;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5548;
      struct _M0TPB5ArrayGbE* _M0L4fireS5537;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5547;
      struct _M0TPB5ArrayGbE* _M0L4fireS5538;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5546;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5539;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5545;
      int32_t _M0L6_2acntS5799;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5540;
      int32_t _M0L6_2acntS5810;
      struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS5541;
      struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS5542;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5544;
      float _M0L6_2atmpS5543;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5550;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5553;
      int32_t _M0L6_2acntS5814;
      float _M0L6_2atmpS5552;
      float _M0L6_2atmpS5551;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5534;
      int32_t _M0L11conn__indexS5535;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1877;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5529;
      struct _M0TPB5ArrayGfE* _M0L4valsS5517;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5528;
      struct _M0TPB5ArrayGbE* _M0L4fireS5518;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5527;
      struct _M0TPB5ArrayGbE* _M0L4fireS5519;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5526;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5520;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5525;
      int32_t _M0L6_2acntS5779;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5521;
      int32_t _M0L6_2acntS5790;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5522;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5523;
      struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L5paramS5524;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5530;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5533;
      int32_t _M0L6_2acntS5794;
      float _M0L6_2atmpS5532;
      float _M0L6_2atmpS5531;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5515;
      int32_t _M0L11conn__indexS5516;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1874;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5510;
      struct _M0TPB5ArrayGfE* _M0L4valsS5499;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5509;
      struct _M0TPB5ArrayGbE* _M0L4fireS5500;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5508;
      struct _M0TPB5ArrayGbE* _M0L4fireS5501;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5507;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5502;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5506;
      int32_t _M0L6_2acntS5760;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5503;
      int32_t _M0L6_2acntS5771;
      struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L4varsS5504;
      struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L5paramS5505;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5511;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5514;
      int32_t _M0L6_2acntS5775;
      float _M0L6_2atmpS5513;
      float _M0L6_2atmpS5512;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5497;
      int32_t _M0L11conn__indexS5498;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1871;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5492;
      struct _M0TPB5ArrayGfE* _M0L4valsS5479;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5491;
      struct _M0TPB5ArrayGbE* _M0L4fireS5480;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5490;
      struct _M0TPB5ArrayGbE* _M0L4fireS5481;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5489;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5482;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5488;
      int32_t _M0L6_2acntS5741;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5483;
      int32_t _M0L6_2acntS5752;
      struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS5484;
      struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L5paramS5485;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5487;
      float _M0L6_2atmpS5486;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5493;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5496;
      int32_t _M0L6_2acntS5756;
      float _M0L6_2atmpS5495;
      float _M0L6_2atmpS5494;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5477;
      int32_t _M0L11conn__indexS5478;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1868;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5472;
      struct _M0TPB5ArrayGfE* _M0L4valsS5459;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5471;
      struct _M0TPB5ArrayGbE* _M0L4fireS5460;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5470;
      struct _M0TPB5ArrayGbE* _M0L4fireS5461;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5469;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5462;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5468;
      int32_t _M0L6_2acntS5722;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5463;
      int32_t _M0L6_2acntS5733;
      struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L4varsS5464;
      struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS5465;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5467;
      float _M0L6_2atmpS5466;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5473;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5476;
      int32_t _M0L6_2acntS5737;
      float _M0L6_2atmpS5475;
      float _M0L6_2atmpS5474;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5457;
      int32_t _M0L11conn__indexS5458;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1865;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5452;
      struct _M0TPB5ArrayGfE* _M0L4valsS5437;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5451;
      struct _M0TPB5ArrayGbE* _M0L4fireS5438;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5450;
      struct _M0TPB5ArrayGbE* _M0L4fireS5439;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5449;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5440;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5448;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5441;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5447;
      int32_t _M0L6_2acntS5690;
      struct _M0TPB5ArrayGfE* _M0L1vS5442;
      int32_t _M0L6_2acntS5701;
      struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L4varsS5443;
      struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS5444;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5446;
      float _M0L6_2atmpS5445;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5453;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5456;
      int32_t _M0L6_2acntS5718;
      float _M0L6_2atmpS5455;
      float _M0L6_2atmpS5454;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5435;
      int32_t _M0L11conn__indexS5436;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1862;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5430;
      struct _M0TPB5ArrayGfE* _M0L4valsS5417;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5429;
      struct _M0TPB5ArrayGbE* _M0L4fireS5418;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5428;
      struct _M0TPB5ArrayGbE* _M0L4fireS5419;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5427;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5420;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5426;
      int32_t _M0L6_2acntS5671;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5421;
      int32_t _M0L6_2acntS5682;
      struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L4varsS5422;
      struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L5paramS5423;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5425;
      float _M0L6_2atmpS5424;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5431;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5434;
      int32_t _M0L6_2acntS5686;
      float _M0L6_2atmpS5433;
      float _M0L6_2atmpS5432;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5415;
      int32_t _M0L11conn__indexS5416;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1859;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5410;
      struct _M0TPB5ArrayGfE* _M0L4valsS5397;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5409;
      struct _M0TPB5ArrayGbE* _M0L4fireS5398;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5408;
      struct _M0TPB5ArrayGbE* _M0L4fireS5399;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5407;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5400;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5406;
      int32_t _M0L6_2acntS5652;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5401;
      int32_t _M0L6_2acntS5663;
      struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L4varsS5402;
      struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L5paramS5403;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5405;
      float _M0L6_2atmpS5404;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5411;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5414;
      int32_t _M0L6_2acntS5667;
      float _M0L6_2atmpS5413;
      float _M0L6_2atmpS5412;
      int32_t _M0L6_2atmpS5556;
      switch (Moonbit_object_tag(_M0L5entryS1856)) {
        case 0: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__* _M0L13_2aGerstner__S1881 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__*)_M0L5entryS1856;
          struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0L4_2aeS1882 =
            _M0L13_2aGerstner__S1881->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1882);
          _M0L1eS1879 = _M0L4_2aeS1882;
          goto join_1878;
          break;
        }
        
        case 1: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind12MexicanHat__* _M0L15_2aMexicanHat__S1883 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind12MexicanHat__*)_M0L5entryS1856;
          struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat* _M0L4_2aeS1884 =
            _M0L15_2aMexicanHat__S1883->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1884);
          _M0L1eS1876 = _M0L4_2aeS1884;
          goto join_1875;
          break;
        }
        
        case 2: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind15AntiSymmetric__* _M0L18_2aAntiSymmetric__S1885 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind15AntiSymmetric__*)_M0L5entryS1856;
          struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric* _M0L4_2aeS1886 =
            _M0L18_2aAntiSymmetric__S1885->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1886);
          _M0L1eS1873 = _M0L4_2aeS1886;
          goto join_1872;
          break;
        }
        
        case 3: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16Confavreux2025__* _M0L19_2aConfavreux2025__S1887 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16Confavreux2025__*)_M0L5entryS1856;
          struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025* _M0L4_2aeS1888 =
            _M0L19_2aConfavreux2025__S1887->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1888);
          _M0L1eS1870 = _M0L4_2aeS1888;
          goto join_1869;
          break;
        }
        
        case 4: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__* _M0L14_2aIstdpRate__S1889 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__*)_M0L5entryS1856;
          struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0L4_2aeS1890 =
            _M0L14_2aIstdpRate__S1889->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1890);
          _M0L1eS1867 = _M0L4_2aeS1890;
          goto join_1866;
          break;
        }
        
        case 5: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16IstdpPotential__* _M0L19_2aIstdpPotential__S1891 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16IstdpPotential__*)_M0L5entryS1856;
          struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0L4_2aeS1892 =
            _M0L19_2aIstdpPotential__S1891->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1892);
          _M0L1eS1864 = _M0L4_2aeS1892;
          goto join_1863;
          break;
        }
        
        case 6: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11Symmetric__* _M0L14_2aSymmetric__S1893 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11Symmetric__*)_M0L5entryS1856;
          struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric* _M0L4_2aeS1894 =
            _M0L14_2aSymmetric__S1893->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1894);
          _M0L1eS1861 = _M0L4_2aeS1894;
          goto join_1860;
          break;
        }
        default: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__* _M0L17_2aCaPlasticity__S1895 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__*)_M0L5entryS1856;
          struct _M0TP26RiantR8snn__mbt17CaPlasticityEntry* _M0L4_2aeS1896 =
            _M0L17_2aCaPlasticity__S1895->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1896);
          _M0L1eS1858 = _M0L4_2aeS1896;
          goto join_1857;
          break;
        }
      }
      goto joinlet_5861;
      join_1878:;
      _M0L5connsS5554 = _M0L1mS1812->$1;
      _M0L11conn__indexS5555 = _M0L1eS1879->$0;
      #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1880
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5554, _M0L11conn__indexS5555);
      _M0L6matrixS5549 = _M0L3synS1880->$4;
      _M0L4valsS5536 = _M0L6matrixS5549->$4;
      _M0L3preS5548 = _M0L3synS1880->$0;
      _M0L4fireS5537 = _M0L3preS5548->$5;
      _M0L4postS5547 = _M0L3synS1880->$1;
      _M0L4fireS5538 = _M0L4postS5547->$5;
      _M0L6matrixS5546 = _M0L3synS1880->$4;
      _M0L6colptrS5539 = _M0L6matrixS5546->$3;
      _M0L6matrixS5545 = _M0L3synS1880->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5539);
      moonbit_incref_cycle_free(_M0L4fireS5538);
      moonbit_incref_cycle_free(_M0L4fireS5537);
      moonbit_incref_cycle_free(_M0L4valsS5536);
      _M0L6_2acntS5799
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1880));
      if (_M0L6_2acntS5799 > 1) {
        int32_t _M0L11_2anew__cntS5809 = _M0L6_2acntS5799 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1880), _M0L11_2anew__cntS5809);
        moonbit_incref_cycle_free(_M0L6matrixS5545);
      } else if (_M0L6_2acntS5799 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5808 = _M0L3synS1880->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5807;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5806;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5805;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5804;
        moonbit_string_t _M0L8_2afieldS5803;
        moonbit_string_t _M0L8_2afieldS5802;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5801;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5800;
        moonbit_decref_cycle_free(_M0L8_2afieldS5808);
        _M0L8_2afieldS5807 = _M0L3synS1880->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5807);
        _M0L8_2afieldS5806 = _M0L3synS1880->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5806);
        _M0L8_2afieldS5805 = _M0L3synS1880->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5805);
        _M0L8_2afieldS5804 = _M0L3synS1880->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5804);
        _M0L8_2afieldS5803 = _M0L3synS1880->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5803);
        _M0L8_2afieldS5802 = _M0L3synS1880->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5802);
        _M0L8_2afieldS5801 = _M0L3synS1880->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5801);
        _M0L8_2afieldS5800 = _M0L3synS1880->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5800);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1880);
      }
      _M0L6rowptrS5540 = _M0L6matrixS5545->$2;
      _M0L6_2acntS5810
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5545));
      if (_M0L6_2acntS5810 > 1) {
        int32_t _M0L11_2anew__cntS5813 = _M0L6_2acntS5810 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5545), _M0L11_2anew__cntS5813);
        moonbit_incref_cycle_free(_M0L6rowptrS5540);
      } else if (_M0L6_2acntS5810 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5812 = _M0L6matrixS5545->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5811;
        moonbit_decref_cycle_free(_M0L8_2afieldS5812);
        _M0L8_2afieldS5811 = _M0L6matrixS5545->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5811);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5545);
      }
      _M0L4varsS5541 = _M0L1eS1879->$3;
      _M0L5paramS5542 = _M0L1eS1879->$4;
      _M0L6t__nowS5544 = _M0L1eS1879->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5544);
      moonbit_incref_cycle_free(_M0L5paramS5542);
      moonbit_incref_cycle_free(_M0L4varsS5541);
      #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5543 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5544, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5544);
      #line 137 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt10stdp__step(_M0L4valsS5536, _M0L4fireS5537, _M0L4fireS5538, _M0L6colptrS5539, _M0L6rowptrS5540, _M0L4varsS5541, _M0L5paramS5542, _M0L6_2atmpS5543, _M0L2dtS1817);
      moonbit_decref_cycle_free(_M0L4valsS5536);
      moonbit_decref_cycle_free(_M0L4fireS5537);
      moonbit_decref_cycle_free(_M0L4fireS5538);
      moonbit_decref_cycle_free(_M0L6colptrS5539);
      moonbit_decref_cycle_free(_M0L6rowptrS5540);
      moonbit_decref_cycle_free(_M0L4varsS5541);
      moonbit_decref_cycle_free(_M0L5paramS5542);
      _M0L6t__nowS5550 = _M0L1eS1879->$5;
      _M0L6t__nowS5553 = _M0L1eS1879->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5550);
      _M0L6_2acntS5814 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1879));
      if (_M0L6_2acntS5814 > 1) {
        int32_t _M0L11_2anew__cntS5817 = _M0L6_2acntS5814 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1879), _M0L11_2anew__cntS5817);
        moonbit_incref_cycle_free(_M0L6t__nowS5553);
      } else if (_M0L6_2acntS5814 == 1) {
        struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L8_2afieldS5816 =
          _M0L1eS1879->$4;
        struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L8_2afieldS5815;
        moonbit_decref_cycle_free(_M0L8_2afieldS5816);
        _M0L8_2afieldS5815 = _M0L1eS1879->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5815);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1879);
      }
      #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5552 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5553, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5553);
      _M0L6_2atmpS5551 = _M0L6_2atmpS5552 + _M0L2dtS1817;
      #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5550, 0, _M0L6_2atmpS5551);
      moonbit_decref_cycle_free(_M0L6t__nowS5550);
      joinlet_5861:;
      goto joinlet_5860;
      join_1875:;
      _M0L5connsS5534 = _M0L1mS1812->$1;
      _M0L11conn__indexS5535 = _M0L1eS1876->$0;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1877
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5534, _M0L11conn__indexS5535);
      _M0L6matrixS5529 = _M0L3synS1877->$4;
      _M0L4valsS5517 = _M0L6matrixS5529->$4;
      _M0L3preS5528 = _M0L3synS1877->$0;
      _M0L4fireS5518 = _M0L3preS5528->$5;
      _M0L4postS5527 = _M0L3synS1877->$1;
      _M0L4fireS5519 = _M0L4postS5527->$5;
      _M0L6matrixS5526 = _M0L3synS1877->$4;
      _M0L6colptrS5520 = _M0L6matrixS5526->$3;
      _M0L6matrixS5525 = _M0L3synS1877->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5520);
      moonbit_incref_cycle_free(_M0L4fireS5519);
      moonbit_incref_cycle_free(_M0L4fireS5518);
      moonbit_incref_cycle_free(_M0L4valsS5517);
      _M0L6_2acntS5779
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1877));
      if (_M0L6_2acntS5779 > 1) {
        int32_t _M0L11_2anew__cntS5789 = _M0L6_2acntS5779 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1877), _M0L11_2anew__cntS5789);
        moonbit_incref_cycle_free(_M0L6matrixS5525);
      } else if (_M0L6_2acntS5779 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5788 = _M0L3synS1877->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5787;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5786;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5785;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5784;
        moonbit_string_t _M0L8_2afieldS5783;
        moonbit_string_t _M0L8_2afieldS5782;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5781;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5780;
        moonbit_decref_cycle_free(_M0L8_2afieldS5788);
        _M0L8_2afieldS5787 = _M0L3synS1877->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5787);
        _M0L8_2afieldS5786 = _M0L3synS1877->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5786);
        _M0L8_2afieldS5785 = _M0L3synS1877->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5785);
        _M0L8_2afieldS5784 = _M0L3synS1877->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5784);
        _M0L8_2afieldS5783 = _M0L3synS1877->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5783);
        _M0L8_2afieldS5782 = _M0L3synS1877->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5782);
        _M0L8_2afieldS5781 = _M0L3synS1877->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5781);
        _M0L8_2afieldS5780 = _M0L3synS1877->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5780);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1877);
      }
      _M0L6rowptrS5521 = _M0L6matrixS5525->$2;
      _M0L6_2acntS5790
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5525));
      if (_M0L6_2acntS5790 > 1) {
        int32_t _M0L11_2anew__cntS5793 = _M0L6_2acntS5790 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5525), _M0L11_2anew__cntS5793);
        moonbit_incref_cycle_free(_M0L6rowptrS5521);
      } else if (_M0L6_2acntS5790 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5792 = _M0L6matrixS5525->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5791;
        moonbit_decref_cycle_free(_M0L8_2afieldS5792);
        _M0L8_2afieldS5791 = _M0L6matrixS5525->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5791);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5525);
      }
      _M0L4tpreS5522 = _M0L1eS1876->$4;
      _M0L5tpostS5523 = _M0L1eS1876->$5;
      _M0L5paramS5524 = _M0L1eS1876->$3;
      moonbit_incref_cycle_free(_M0L5paramS5524);
      moonbit_incref_cycle_free(_M0L5tpostS5523);
      moonbit_incref_cycle_free(_M0L4tpreS5522);
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt24stdp__mexican__hat__step(_M0L4valsS5517, _M0L4fireS5518, _M0L4fireS5519, _M0L6colptrS5520, _M0L6rowptrS5521, _M0L4tpreS5522, _M0L5tpostS5523, _M0L5paramS5524, _M0L2dtS1817);
      moonbit_decref_cycle_free(_M0L4valsS5517);
      moonbit_decref_cycle_free(_M0L4fireS5518);
      moonbit_decref_cycle_free(_M0L4fireS5519);
      moonbit_decref_cycle_free(_M0L6colptrS5520);
      moonbit_decref_cycle_free(_M0L6rowptrS5521);
      moonbit_decref_cycle_free(_M0L4tpreS5522);
      moonbit_decref_cycle_free(_M0L5tpostS5523);
      moonbit_decref_cycle_free(_M0L5paramS5524);
      _M0L6t__nowS5530 = _M0L1eS1876->$6;
      _M0L6t__nowS5533 = _M0L1eS1876->$6;
      moonbit_incref_cycle_free(_M0L6t__nowS5530);
      _M0L6_2acntS5794 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1876));
      if (_M0L6_2acntS5794 > 1) {
        int32_t _M0L11_2anew__cntS5798 = _M0L6_2acntS5794 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1876), _M0L11_2anew__cntS5798);
        moonbit_incref_cycle_free(_M0L6t__nowS5533);
      } else if (_M0L6_2acntS5794 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5797 = _M0L1eS1876->$5;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5796;
        struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L8_2afieldS5795;
        moonbit_decref_cycle_free(_M0L8_2afieldS5797);
        _M0L8_2afieldS5796 = _M0L1eS1876->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS5796);
        _M0L8_2afieldS5795 = _M0L1eS1876->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5795);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1876);
      }
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5532 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5533, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5533);
      _M0L6_2atmpS5531 = _M0L6_2atmpS5532 + _M0L2dtS1817;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5530, 0, _M0L6_2atmpS5531);
      moonbit_decref_cycle_free(_M0L6t__nowS5530);
      joinlet_5860:;
      goto joinlet_5859;
      join_1872:;
      _M0L5connsS5515 = _M0L1mS1812->$1;
      _M0L11conn__indexS5516 = _M0L1eS1873->$0;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1874
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5515, _M0L11conn__indexS5516);
      _M0L6matrixS5510 = _M0L3synS1874->$4;
      _M0L4valsS5499 = _M0L6matrixS5510->$4;
      _M0L3preS5509 = _M0L3synS1874->$0;
      _M0L4fireS5500 = _M0L3preS5509->$5;
      _M0L4postS5508 = _M0L3synS1874->$1;
      _M0L4fireS5501 = _M0L4postS5508->$5;
      _M0L6matrixS5507 = _M0L3synS1874->$4;
      _M0L6colptrS5502 = _M0L6matrixS5507->$3;
      _M0L6matrixS5506 = _M0L3synS1874->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5502);
      moonbit_incref_cycle_free(_M0L4fireS5501);
      moonbit_incref_cycle_free(_M0L4fireS5500);
      moonbit_incref_cycle_free(_M0L4valsS5499);
      _M0L6_2acntS5760
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1874));
      if (_M0L6_2acntS5760 > 1) {
        int32_t _M0L11_2anew__cntS5770 = _M0L6_2acntS5760 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1874), _M0L11_2anew__cntS5770);
        moonbit_incref_cycle_free(_M0L6matrixS5506);
      } else if (_M0L6_2acntS5760 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5769 = _M0L3synS1874->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5768;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5767;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5766;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5765;
        moonbit_string_t _M0L8_2afieldS5764;
        moonbit_string_t _M0L8_2afieldS5763;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5762;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5761;
        moonbit_decref_cycle_free(_M0L8_2afieldS5769);
        _M0L8_2afieldS5768 = _M0L3synS1874->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5768);
        _M0L8_2afieldS5767 = _M0L3synS1874->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5767);
        _M0L8_2afieldS5766 = _M0L3synS1874->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5766);
        _M0L8_2afieldS5765 = _M0L3synS1874->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5765);
        _M0L8_2afieldS5764 = _M0L3synS1874->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5764);
        _M0L8_2afieldS5763 = _M0L3synS1874->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5763);
        _M0L8_2afieldS5762 = _M0L3synS1874->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5762);
        _M0L8_2afieldS5761 = _M0L3synS1874->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5761);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1874);
      }
      _M0L6rowptrS5503 = _M0L6matrixS5506->$2;
      _M0L6_2acntS5771
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5506));
      if (_M0L6_2acntS5771 > 1) {
        int32_t _M0L11_2anew__cntS5774 = _M0L6_2acntS5771 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5506), _M0L11_2anew__cntS5774);
        moonbit_incref_cycle_free(_M0L6rowptrS5503);
      } else if (_M0L6_2acntS5771 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5773 = _M0L6matrixS5506->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5772;
        moonbit_decref_cycle_free(_M0L8_2afieldS5773);
        _M0L8_2afieldS5772 = _M0L6matrixS5506->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5772);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5506);
      }
      _M0L4varsS5504 = _M0L1eS1873->$4;
      _M0L5paramS5505 = _M0L1eS1873->$3;
      moonbit_incref_cycle_free(_M0L5paramS5505);
      moonbit_incref_cycle_free(_M0L4varsS5504);
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt25stdp__antisymmetric__step(_M0L4valsS5499, _M0L4fireS5500, _M0L4fireS5501, _M0L6colptrS5502, _M0L6rowptrS5503, _M0L4varsS5504, _M0L5paramS5505, _M0L2dtS1817);
      moonbit_decref_cycle_free(_M0L4valsS5499);
      moonbit_decref_cycle_free(_M0L4fireS5500);
      moonbit_decref_cycle_free(_M0L4fireS5501);
      moonbit_decref_cycle_free(_M0L6colptrS5502);
      moonbit_decref_cycle_free(_M0L6rowptrS5503);
      moonbit_decref_cycle_free(_M0L4varsS5504);
      moonbit_decref_cycle_free(_M0L5paramS5505);
      _M0L6t__nowS5511 = _M0L1eS1873->$5;
      _M0L6t__nowS5514 = _M0L1eS1873->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5511);
      _M0L6_2acntS5775 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1873));
      if (_M0L6_2acntS5775 > 1) {
        int32_t _M0L11_2anew__cntS5778 = _M0L6_2acntS5775 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1873), _M0L11_2anew__cntS5778);
        moonbit_incref_cycle_free(_M0L6t__nowS5514);
      } else if (_M0L6_2acntS5775 == 1) {
        struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L8_2afieldS5777 =
          _M0L1eS1873->$4;
        struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L8_2afieldS5776;
        moonbit_decref_cycle_free(_M0L8_2afieldS5777);
        _M0L8_2afieldS5776 = _M0L1eS1873->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5776);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1873);
      }
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5513 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5514, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5514);
      _M0L6_2atmpS5512 = _M0L6_2atmpS5513 + _M0L2dtS1817;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5511, 0, _M0L6_2atmpS5512);
      moonbit_decref_cycle_free(_M0L6t__nowS5511);
      joinlet_5859:;
      goto joinlet_5858;
      join_1869:;
      _M0L5connsS5497 = _M0L1mS1812->$1;
      _M0L11conn__indexS5498 = _M0L1eS1870->$0;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1871
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5497, _M0L11conn__indexS5498);
      _M0L6matrixS5492 = _M0L3synS1871->$4;
      _M0L4valsS5479 = _M0L6matrixS5492->$4;
      _M0L3preS5491 = _M0L3synS1871->$0;
      _M0L4fireS5480 = _M0L3preS5491->$5;
      _M0L4postS5490 = _M0L3synS1871->$1;
      _M0L4fireS5481 = _M0L4postS5490->$5;
      _M0L6matrixS5489 = _M0L3synS1871->$4;
      _M0L6colptrS5482 = _M0L6matrixS5489->$3;
      _M0L6matrixS5488 = _M0L3synS1871->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5482);
      moonbit_incref_cycle_free(_M0L4fireS5481);
      moonbit_incref_cycle_free(_M0L4fireS5480);
      moonbit_incref_cycle_free(_M0L4valsS5479);
      _M0L6_2acntS5741
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1871));
      if (_M0L6_2acntS5741 > 1) {
        int32_t _M0L11_2anew__cntS5751 = _M0L6_2acntS5741 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1871), _M0L11_2anew__cntS5751);
        moonbit_incref_cycle_free(_M0L6matrixS5488);
      } else if (_M0L6_2acntS5741 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5750 = _M0L3synS1871->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5749;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5748;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5747;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5746;
        moonbit_string_t _M0L8_2afieldS5745;
        moonbit_string_t _M0L8_2afieldS5744;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5743;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5742;
        moonbit_decref_cycle_free(_M0L8_2afieldS5750);
        _M0L8_2afieldS5749 = _M0L3synS1871->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5749);
        _M0L8_2afieldS5748 = _M0L3synS1871->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5748);
        _M0L8_2afieldS5747 = _M0L3synS1871->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5747);
        _M0L8_2afieldS5746 = _M0L3synS1871->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5746);
        _M0L8_2afieldS5745 = _M0L3synS1871->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5745);
        _M0L8_2afieldS5744 = _M0L3synS1871->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5744);
        _M0L8_2afieldS5743 = _M0L3synS1871->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5743);
        _M0L8_2afieldS5742 = _M0L3synS1871->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5742);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1871);
      }
      _M0L6rowptrS5483 = _M0L6matrixS5488->$2;
      _M0L6_2acntS5752
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5488));
      if (_M0L6_2acntS5752 > 1) {
        int32_t _M0L11_2anew__cntS5755 = _M0L6_2acntS5752 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5488), _M0L11_2anew__cntS5755);
        moonbit_incref_cycle_free(_M0L6rowptrS5483);
      } else if (_M0L6_2acntS5752 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5754 = _M0L6matrixS5488->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5753;
        moonbit_decref_cycle_free(_M0L8_2afieldS5754);
        _M0L8_2afieldS5753 = _M0L6matrixS5488->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5753);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5488);
      }
      _M0L4varsS5484 = _M0L1eS1870->$4;
      _M0L5paramS5485 = _M0L1eS1870->$3;
      _M0L6t__nowS5487 = _M0L1eS1870->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5487);
      moonbit_incref_cycle_free(_M0L5paramS5485);
      moonbit_incref_cycle_free(_M0L4varsS5484);
      #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5486 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5487, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5487);
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt22stdp__confavreux__step(_M0L4valsS5479, _M0L4fireS5480, _M0L4fireS5481, _M0L6colptrS5482, _M0L6rowptrS5483, _M0L4varsS5484, _M0L5paramS5485, _M0L6_2atmpS5486, _M0L2dtS1817);
      moonbit_decref_cycle_free(_M0L4valsS5479);
      moonbit_decref_cycle_free(_M0L4fireS5480);
      moonbit_decref_cycle_free(_M0L4fireS5481);
      moonbit_decref_cycle_free(_M0L6colptrS5482);
      moonbit_decref_cycle_free(_M0L6rowptrS5483);
      moonbit_decref_cycle_free(_M0L4varsS5484);
      moonbit_decref_cycle_free(_M0L5paramS5485);
      _M0L6t__nowS5493 = _M0L1eS1870->$5;
      _M0L6t__nowS5496 = _M0L1eS1870->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5493);
      _M0L6_2acntS5756 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1870));
      if (_M0L6_2acntS5756 > 1) {
        int32_t _M0L11_2anew__cntS5759 = _M0L6_2acntS5756 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1870), _M0L11_2anew__cntS5759);
        moonbit_incref_cycle_free(_M0L6t__nowS5496);
      } else if (_M0L6_2acntS5756 == 1) {
        struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L8_2afieldS5758 =
          _M0L1eS1870->$4;
        struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L8_2afieldS5757;
        moonbit_decref_cycle_free(_M0L8_2afieldS5758);
        _M0L8_2afieldS5757 = _M0L1eS1870->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5757);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1870);
      }
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5495 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5496, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5496);
      _M0L6_2atmpS5494 = _M0L6_2atmpS5495 + _M0L2dtS1817;
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5493, 0, _M0L6_2atmpS5494);
      moonbit_decref_cycle_free(_M0L6t__nowS5493);
      joinlet_5858:;
      goto joinlet_5857;
      join_1866:;
      _M0L5connsS5477 = _M0L1mS1812->$1;
      _M0L11conn__indexS5478 = _M0L1eS1867->$0;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1868
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5477, _M0L11conn__indexS5478);
      _M0L6matrixS5472 = _M0L3synS1868->$4;
      _M0L4valsS5459 = _M0L6matrixS5472->$4;
      _M0L3preS5471 = _M0L3synS1868->$0;
      _M0L4fireS5460 = _M0L3preS5471->$5;
      _M0L4postS5470 = _M0L3synS1868->$1;
      _M0L4fireS5461 = _M0L4postS5470->$5;
      _M0L6matrixS5469 = _M0L3synS1868->$4;
      _M0L6colptrS5462 = _M0L6matrixS5469->$3;
      _M0L6matrixS5468 = _M0L3synS1868->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5462);
      moonbit_incref_cycle_free(_M0L4fireS5461);
      moonbit_incref_cycle_free(_M0L4fireS5460);
      moonbit_incref_cycle_free(_M0L4valsS5459);
      _M0L6_2acntS5722
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1868));
      if (_M0L6_2acntS5722 > 1) {
        int32_t _M0L11_2anew__cntS5732 = _M0L6_2acntS5722 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1868), _M0L11_2anew__cntS5732);
        moonbit_incref_cycle_free(_M0L6matrixS5468);
      } else if (_M0L6_2acntS5722 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5731 = _M0L3synS1868->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5730;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5729;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5728;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5727;
        moonbit_string_t _M0L8_2afieldS5726;
        moonbit_string_t _M0L8_2afieldS5725;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5724;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5723;
        moonbit_decref_cycle_free(_M0L8_2afieldS5731);
        _M0L8_2afieldS5730 = _M0L3synS1868->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5730);
        _M0L8_2afieldS5729 = _M0L3synS1868->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5729);
        _M0L8_2afieldS5728 = _M0L3synS1868->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5728);
        _M0L8_2afieldS5727 = _M0L3synS1868->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5727);
        _M0L8_2afieldS5726 = _M0L3synS1868->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5726);
        _M0L8_2afieldS5725 = _M0L3synS1868->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5725);
        _M0L8_2afieldS5724 = _M0L3synS1868->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5724);
        _M0L8_2afieldS5723 = _M0L3synS1868->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5723);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1868);
      }
      _M0L6rowptrS5463 = _M0L6matrixS5468->$2;
      _M0L6_2acntS5733
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5468));
      if (_M0L6_2acntS5733 > 1) {
        int32_t _M0L11_2anew__cntS5736 = _M0L6_2acntS5733 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5468), _M0L11_2anew__cntS5736);
        moonbit_incref_cycle_free(_M0L6rowptrS5463);
      } else if (_M0L6_2acntS5733 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5735 = _M0L6matrixS5468->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5734;
        moonbit_decref_cycle_free(_M0L8_2afieldS5735);
        _M0L8_2afieldS5734 = _M0L6matrixS5468->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5734);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5468);
      }
      _M0L4varsS5464 = _M0L1eS1867->$4;
      _M0L5paramS5465 = _M0L1eS1867->$3;
      _M0L6t__nowS5467 = _M0L1eS1867->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5467);
      moonbit_incref_cycle_free(_M0L5paramS5465);
      moonbit_incref_cycle_free(_M0L4varsS5464);
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5466 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5467, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5467);
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt17istdp__rate__step(_M0L4valsS5459, _M0L4fireS5460, _M0L4fireS5461, _M0L6colptrS5462, _M0L6rowptrS5463, _M0L4varsS5464, _M0L5paramS5465, _M0L6_2atmpS5466, _M0L2dtS1817);
      moonbit_decref_cycle_free(_M0L4valsS5459);
      moonbit_decref_cycle_free(_M0L4fireS5460);
      moonbit_decref_cycle_free(_M0L4fireS5461);
      moonbit_decref_cycle_free(_M0L6colptrS5462);
      moonbit_decref_cycle_free(_M0L6rowptrS5463);
      moonbit_decref_cycle_free(_M0L4varsS5464);
      moonbit_decref_cycle_free(_M0L5paramS5465);
      _M0L6t__nowS5473 = _M0L1eS1867->$5;
      _M0L6t__nowS5476 = _M0L1eS1867->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5473);
      _M0L6_2acntS5737 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1867));
      if (_M0L6_2acntS5737 > 1) {
        int32_t _M0L11_2anew__cntS5740 = _M0L6_2acntS5737 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1867), _M0L11_2anew__cntS5740);
        moonbit_incref_cycle_free(_M0L6t__nowS5476);
      } else if (_M0L6_2acntS5737 == 1) {
        struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L8_2afieldS5739 =
          _M0L1eS1867->$4;
        struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L8_2afieldS5738;
        moonbit_decref_cycle_free(_M0L8_2afieldS5739);
        _M0L8_2afieldS5738 = _M0L1eS1867->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5738);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1867);
      }
      #line 207 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5475 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5476, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5476);
      _M0L6_2atmpS5474 = _M0L6_2atmpS5475 + _M0L2dtS1817;
      #line 207 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5473, 0, _M0L6_2atmpS5474);
      moonbit_decref_cycle_free(_M0L6t__nowS5473);
      joinlet_5857:;
      goto joinlet_5856;
      join_1863:;
      _M0L5connsS5457 = _M0L1mS1812->$1;
      _M0L11conn__indexS5458 = _M0L1eS1864->$0;
      #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1865
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5457, _M0L11conn__indexS5458);
      _M0L6matrixS5452 = _M0L3synS1865->$4;
      _M0L4valsS5437 = _M0L6matrixS5452->$4;
      _M0L3preS5451 = _M0L3synS1865->$0;
      _M0L4fireS5438 = _M0L3preS5451->$5;
      _M0L4postS5450 = _M0L3synS1865->$1;
      _M0L4fireS5439 = _M0L4postS5450->$5;
      _M0L6matrixS5449 = _M0L3synS1865->$4;
      _M0L6colptrS5440 = _M0L6matrixS5449->$3;
      _M0L6matrixS5448 = _M0L3synS1865->$4;
      _M0L6rowptrS5441 = _M0L6matrixS5448->$2;
      _M0L4postS5447 = _M0L3synS1865->$1;
      moonbit_incref_cycle_free(_M0L6rowptrS5441);
      moonbit_incref_cycle_free(_M0L6colptrS5440);
      moonbit_incref_cycle_free(_M0L4fireS5439);
      moonbit_incref_cycle_free(_M0L4fireS5438);
      moonbit_incref_cycle_free(_M0L4valsS5437);
      _M0L6_2acntS5690
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1865));
      if (_M0L6_2acntS5690 > 1) {
        int32_t _M0L11_2anew__cntS5700 = _M0L6_2acntS5690 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1865), _M0L11_2anew__cntS5700);
        moonbit_incref_cycle_free(_M0L4postS5447);
      } else if (_M0L6_2acntS5690 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5699 = _M0L3synS1865->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5698;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5697;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5696;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5695;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L8_2afieldS5694;
        moonbit_string_t _M0L8_2afieldS5693;
        moonbit_string_t _M0L8_2afieldS5692;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5691;
        moonbit_decref_cycle_free(_M0L8_2afieldS5699);
        _M0L8_2afieldS5698 = _M0L3synS1865->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5698);
        _M0L8_2afieldS5697 = _M0L3synS1865->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5697);
        _M0L8_2afieldS5696 = _M0L3synS1865->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5696);
        _M0L8_2afieldS5695 = _M0L3synS1865->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5695);
        _M0L8_2afieldS5694 = _M0L3synS1865->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS5694);
        _M0L8_2afieldS5693 = _M0L3synS1865->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5693);
        _M0L8_2afieldS5692 = _M0L3synS1865->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5692);
        _M0L8_2afieldS5691 = _M0L3synS1865->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5691);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1865);
      }
      _M0L1vS5442 = _M0L4postS5447->$3;
      _M0L6_2acntS5701
      = Moonbit_rc_count(Moonbit_object_header(_M0L4postS5447));
      if (_M0L6_2acntS5701 > 1) {
        int32_t _M0L11_2anew__cntS5717 = _M0L6_2acntS5701 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L4postS5447), _M0L11_2anew__cntS5717);
        moonbit_incref_cycle_free(_M0L1vS5442);
      } else if (_M0L6_2acntS5701 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5716 = _M0L4postS5447->$16;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5715;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5714;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5713;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5712;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5711;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5710;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5709;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5708;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5707;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5706;
        struct _M0TPB5ArrayGbE* _M0L8_2afieldS5705;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5704;
        struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L8_2afieldS5703;
        struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L8_2afieldS5702;
        moonbit_decref_cycle_free(_M0L8_2afieldS5716);
        _M0L8_2afieldS5715 = _M0L4postS5447->$15;
        moonbit_decref_cycle_free(_M0L8_2afieldS5715);
        _M0L8_2afieldS5714 = _M0L4postS5447->$14;
        moonbit_decref_cycle_free(_M0L8_2afieldS5714);
        _M0L8_2afieldS5713 = _M0L4postS5447->$13;
        moonbit_decref_cycle_free(_M0L8_2afieldS5713);
        _M0L8_2afieldS5712 = _M0L4postS5447->$12;
        moonbit_decref_cycle_free(_M0L8_2afieldS5712);
        _M0L8_2afieldS5711 = _M0L4postS5447->$11;
        moonbit_decref_cycle_free(_M0L8_2afieldS5711);
        _M0L8_2afieldS5710 = _M0L4postS5447->$10;
        moonbit_decref_cycle_free(_M0L8_2afieldS5710);
        _M0L8_2afieldS5709 = _M0L4postS5447->$9;
        moonbit_decref_cycle_free(_M0L8_2afieldS5709);
        _M0L8_2afieldS5708 = _M0L4postS5447->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5708);
        _M0L8_2afieldS5707 = _M0L4postS5447->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5707);
        _M0L8_2afieldS5706 = _M0L4postS5447->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5706);
        _M0L8_2afieldS5705 = _M0L4postS5447->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5705);
        _M0L8_2afieldS5704 = _M0L4postS5447->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS5704);
        _M0L8_2afieldS5703 = _M0L4postS5447->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5703);
        _M0L8_2afieldS5702 = _M0L4postS5447->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5702);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L4postS5447);
      }
      _M0L4varsS5443 = _M0L1eS1864->$4;
      _M0L5paramS5444 = _M0L1eS1864->$3;
      _M0L6t__nowS5446 = _M0L1eS1864->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5446);
      moonbit_incref_cycle_free(_M0L5paramS5444);
      moonbit_incref_cycle_free(_M0L4varsS5443);
      #line 220 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5445 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5446, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5446);
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt22istdp__potential__step(_M0L4valsS5437, _M0L4fireS5438, _M0L4fireS5439, _M0L6colptrS5440, _M0L6rowptrS5441, _M0L1vS5442, _M0L4varsS5443, _M0L5paramS5444, _M0L6_2atmpS5445, _M0L2dtS1817);
      moonbit_decref_cycle_free(_M0L4valsS5437);
      moonbit_decref_cycle_free(_M0L4fireS5438);
      moonbit_decref_cycle_free(_M0L4fireS5439);
      moonbit_decref_cycle_free(_M0L6colptrS5440);
      moonbit_decref_cycle_free(_M0L6rowptrS5441);
      moonbit_decref_cycle_free(_M0L1vS5442);
      moonbit_decref_cycle_free(_M0L4varsS5443);
      moonbit_decref_cycle_free(_M0L5paramS5444);
      _M0L6t__nowS5453 = _M0L1eS1864->$5;
      _M0L6t__nowS5456 = _M0L1eS1864->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5453);
      _M0L6_2acntS5718 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1864));
      if (_M0L6_2acntS5718 > 1) {
        int32_t _M0L11_2anew__cntS5721 = _M0L6_2acntS5718 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1864), _M0L11_2anew__cntS5721);
        moonbit_incref_cycle_free(_M0L6t__nowS5456);
      } else if (_M0L6_2acntS5718 == 1) {
        struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L8_2afieldS5720 =
          _M0L1eS1864->$4;
        struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L8_2afieldS5719;
        moonbit_decref_cycle_free(_M0L8_2afieldS5720);
        _M0L8_2afieldS5719 = _M0L1eS1864->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5719);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1864);
      }
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5455 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5456, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5456);
      _M0L6_2atmpS5454 = _M0L6_2atmpS5455 + _M0L2dtS1817;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5453, 0, _M0L6_2atmpS5454);
      moonbit_decref_cycle_free(_M0L6t__nowS5453);
      joinlet_5856:;
      goto joinlet_5855;
      join_1860:;
      _M0L5connsS5435 = _M0L1mS1812->$1;
      _M0L11conn__indexS5436 = _M0L1eS1861->$0;
      #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1862
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5435, _M0L11conn__indexS5436);
      _M0L6matrixS5430 = _M0L3synS1862->$4;
      _M0L4valsS5417 = _M0L6matrixS5430->$4;
      _M0L3preS5429 = _M0L3synS1862->$0;
      _M0L4fireS5418 = _M0L3preS5429->$5;
      _M0L4postS5428 = _M0L3synS1862->$1;
      _M0L4fireS5419 = _M0L4postS5428->$5;
      _M0L6matrixS5427 = _M0L3synS1862->$4;
      _M0L6colptrS5420 = _M0L6matrixS5427->$3;
      _M0L6matrixS5426 = _M0L3synS1862->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5420);
      moonbit_incref_cycle_free(_M0L4fireS5419);
      moonbit_incref_cycle_free(_M0L4fireS5418);
      moonbit_incref_cycle_free(_M0L4valsS5417);
      _M0L6_2acntS5671
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1862));
      if (_M0L6_2acntS5671 > 1) {
        int32_t _M0L11_2anew__cntS5681 = _M0L6_2acntS5671 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1862), _M0L11_2anew__cntS5681);
        moonbit_incref_cycle_free(_M0L6matrixS5426);
      } else if (_M0L6_2acntS5671 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5680 = _M0L3synS1862->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5679;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5678;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5677;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5676;
        moonbit_string_t _M0L8_2afieldS5675;
        moonbit_string_t _M0L8_2afieldS5674;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5673;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5672;
        moonbit_decref_cycle_free(_M0L8_2afieldS5680);
        _M0L8_2afieldS5679 = _M0L3synS1862->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5679);
        _M0L8_2afieldS5678 = _M0L3synS1862->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5678);
        _M0L8_2afieldS5677 = _M0L3synS1862->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5677);
        _M0L8_2afieldS5676 = _M0L3synS1862->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5676);
        _M0L8_2afieldS5675 = _M0L3synS1862->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5675);
        _M0L8_2afieldS5674 = _M0L3synS1862->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5674);
        _M0L8_2afieldS5673 = _M0L3synS1862->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5673);
        _M0L8_2afieldS5672 = _M0L3synS1862->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5672);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1862);
      }
      _M0L6rowptrS5421 = _M0L6matrixS5426->$2;
      _M0L6_2acntS5682
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5426));
      if (_M0L6_2acntS5682 > 1) {
        int32_t _M0L11_2anew__cntS5685 = _M0L6_2acntS5682 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5426), _M0L11_2anew__cntS5685);
        moonbit_incref_cycle_free(_M0L6rowptrS5421);
      } else if (_M0L6_2acntS5682 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5684 = _M0L6matrixS5426->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5683;
        moonbit_decref_cycle_free(_M0L8_2afieldS5684);
        _M0L8_2afieldS5683 = _M0L6matrixS5426->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5683);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5426);
      }
      _M0L4varsS5422 = _M0L1eS1861->$4;
      _M0L5paramS5423 = _M0L1eS1861->$3;
      _M0L6t__nowS5425 = _M0L1eS1861->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5425);
      moonbit_incref_cycle_free(_M0L5paramS5423);
      moonbit_incref_cycle_free(_M0L4varsS5422);
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5424 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5425, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5425);
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt21stdp__symmetric__step(_M0L4valsS5417, _M0L4fireS5418, _M0L4fireS5419, _M0L6colptrS5420, _M0L6rowptrS5421, _M0L4varsS5422, _M0L5paramS5423, _M0L6_2atmpS5424, _M0L2dtS1817);
      moonbit_decref_cycle_free(_M0L4valsS5417);
      moonbit_decref_cycle_free(_M0L4fireS5418);
      moonbit_decref_cycle_free(_M0L4fireS5419);
      moonbit_decref_cycle_free(_M0L6colptrS5420);
      moonbit_decref_cycle_free(_M0L6rowptrS5421);
      moonbit_decref_cycle_free(_M0L4varsS5422);
      moonbit_decref_cycle_free(_M0L5paramS5423);
      _M0L6t__nowS5431 = _M0L1eS1861->$5;
      _M0L6t__nowS5434 = _M0L1eS1861->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5431);
      _M0L6_2acntS5686 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1861));
      if (_M0L6_2acntS5686 > 1) {
        int32_t _M0L11_2anew__cntS5689 = _M0L6_2acntS5686 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1861), _M0L11_2anew__cntS5689);
        moonbit_incref_cycle_free(_M0L6t__nowS5434);
      } else if (_M0L6_2acntS5686 == 1) {
        struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L8_2afieldS5688 =
          _M0L1eS1861->$4;
        struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L8_2afieldS5687;
        moonbit_decref_cycle_free(_M0L8_2afieldS5688);
        _M0L8_2afieldS5687 = _M0L1eS1861->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5687);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1861);
      }
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5433 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5434, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5434);
      _M0L6_2atmpS5432 = _M0L6_2atmpS5433 + _M0L2dtS1817;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5431, 0, _M0L6_2atmpS5432);
      moonbit_decref_cycle_free(_M0L6t__nowS5431);
      joinlet_5855:;
      goto joinlet_5854;
      join_1857:;
      _M0L5connsS5415 = _M0L1mS1812->$1;
      _M0L11conn__indexS5416 = _M0L1eS1858->$0;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1859
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5415, _M0L11conn__indexS5416);
      _M0L6matrixS5410 = _M0L3synS1859->$4;
      _M0L4valsS5397 = _M0L6matrixS5410->$4;
      _M0L3preS5409 = _M0L3synS1859->$0;
      _M0L4fireS5398 = _M0L3preS5409->$5;
      _M0L4postS5408 = _M0L3synS1859->$1;
      _M0L4fireS5399 = _M0L4postS5408->$5;
      _M0L6matrixS5407 = _M0L3synS1859->$4;
      _M0L6colptrS5400 = _M0L6matrixS5407->$3;
      _M0L6matrixS5406 = _M0L3synS1859->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5400);
      moonbit_incref_cycle_free(_M0L4fireS5399);
      moonbit_incref_cycle_free(_M0L4fireS5398);
      moonbit_incref_cycle_free(_M0L4valsS5397);
      _M0L6_2acntS5652
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1859));
      if (_M0L6_2acntS5652 > 1) {
        int32_t _M0L11_2anew__cntS5662 = _M0L6_2acntS5652 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1859), _M0L11_2anew__cntS5662);
        moonbit_incref_cycle_free(_M0L6matrixS5406);
      } else if (_M0L6_2acntS5652 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5661 = _M0L3synS1859->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5660;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5659;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5658;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5657;
        moonbit_string_t _M0L8_2afieldS5656;
        moonbit_string_t _M0L8_2afieldS5655;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5654;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5653;
        moonbit_decref_cycle_free(_M0L8_2afieldS5661);
        _M0L8_2afieldS5660 = _M0L3synS1859->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5660);
        _M0L8_2afieldS5659 = _M0L3synS1859->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5659);
        _M0L8_2afieldS5658 = _M0L3synS1859->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5658);
        _M0L8_2afieldS5657 = _M0L3synS1859->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5657);
        _M0L8_2afieldS5656 = _M0L3synS1859->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5656);
        _M0L8_2afieldS5655 = _M0L3synS1859->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5655);
        _M0L8_2afieldS5654 = _M0L3synS1859->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5654);
        _M0L8_2afieldS5653 = _M0L3synS1859->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5653);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1859);
      }
      _M0L6rowptrS5401 = _M0L6matrixS5406->$2;
      _M0L6_2acntS5663
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5406));
      if (_M0L6_2acntS5663 > 1) {
        int32_t _M0L11_2anew__cntS5666 = _M0L6_2acntS5663 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5406), _M0L11_2anew__cntS5666);
        moonbit_incref_cycle_free(_M0L6rowptrS5401);
      } else if (_M0L6_2acntS5663 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5665 = _M0L6matrixS5406->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5664;
        moonbit_decref_cycle_free(_M0L8_2afieldS5665);
        _M0L8_2afieldS5664 = _M0L6matrixS5406->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5664);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5406);
      }
      _M0L4varsS5402 = _M0L1eS1858->$4;
      _M0L5paramS5403 = _M0L1eS1858->$3;
      _M0L6t__nowS5405 = _M0L1eS1858->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5405);
      moonbit_incref_cycle_free(_M0L5paramS5403);
      moonbit_incref_cycle_free(_M0L4varsS5402);
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5404 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5405, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5405);
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt20ca__plasticity__step(_M0L4valsS5397, _M0L4fireS5398, _M0L4fireS5399, _M0L6colptrS5400, _M0L6rowptrS5401, _M0L4varsS5402, _M0L5paramS5403, _M0L6_2atmpS5404, _M0L2dtS1817);
      moonbit_decref_cycle_free(_M0L4valsS5397);
      moonbit_decref_cycle_free(_M0L4fireS5398);
      moonbit_decref_cycle_free(_M0L4fireS5399);
      moonbit_decref_cycle_free(_M0L6colptrS5400);
      moonbit_decref_cycle_free(_M0L6rowptrS5401);
      moonbit_decref_cycle_free(_M0L4varsS5402);
      moonbit_decref_cycle_free(_M0L5paramS5403);
      _M0L6t__nowS5411 = _M0L1eS1858->$5;
      _M0L6t__nowS5414 = _M0L1eS1858->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5411);
      _M0L6_2acntS5667 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1858));
      if (_M0L6_2acntS5667 > 1) {
        int32_t _M0L11_2anew__cntS5670 = _M0L6_2acntS5667 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1858), _M0L11_2anew__cntS5670);
        moonbit_incref_cycle_free(_M0L6t__nowS5414);
      } else if (_M0L6_2acntS5667 == 1) {
        struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L8_2afieldS5669 =
          _M0L1eS1858->$4;
        struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L8_2afieldS5668;
        moonbit_decref_cycle_free(_M0L8_2afieldS5669);
        _M0L8_2afieldS5668 = _M0L1eS1858->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5668);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1858);
      }
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5413 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5414, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5414);
      _M0L6_2atmpS5412 = _M0L6_2atmpS5413 + _M0L2dtS1817;
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5411, 0, _M0L6_2atmpS5412);
      moonbit_decref_cycle_free(_M0L6t__nowS5411);
      joinlet_5854:;
      _M0L6_2atmpS5556 = _M0L2__S1855 + 1;
      _M0L2__S1855 = _M0L6_2atmpS5556;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1854);
    }
    break;
  }
  _M0L7_2abindS1898 = _M0L1mS1812->$0;
  _M0L7_2abindS1899 = _M0L7_2abindS1898->$1;
  _M0L7_2abindS1900 = _M0L7_2abindS1898->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1900);
  _M0L2__S1901 = 0;
  while (1) {
    if (_M0L2__S1901 < _M0L7_2abindS1899) {
      void* _M0L1pS1902 = (void*)_M0L7_2abindS1900[_M0L2__S1901];
      int32_t _M0L6_2atmpS5557;
      moonbit_incref_cycle_free(_M0L1pS1902);
      #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt14integrate__any(_M0L1pS1902, _M0L2dtS1817);
      moonbit_decref_cycle_free(_M0L1pS1902);
      _M0L6_2atmpS5557 = _M0L2__S1901 + 1;
      _M0L2__S1901 = _M0L6_2atmpS5557;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1900);
    }
    break;
  }
  _M0L7_2abindS1904 = _M0L1mS1812->$4;
  _M0L7_2abindS1905 = _M0L7_2abindS1904->$1;
  _M0L7_2abindS1906 = _M0L7_2abindS1904->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1906);
  _M0L2__S1907 = 0;
  while (1) {
    if (_M0L2__S1907 < _M0L7_2abindS1905) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L3monS1908 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS1906[
          _M0L2__S1907
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5559 = _M0L1mS1812->$3;
      float _M0L6_2atmpS5558;
      int32_t _M0L6_2atmpS5560;
      moonbit_incref_cycle_free(_M0L3monS1908);
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5558 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5559);
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L3monS1908, _M0L6_2atmpS5558);
      moonbit_decref_cycle_free(_M0L3monS1908);
      _M0L6_2atmpS5560 = _M0L2__S1907 + 1;
      _M0L2__S1907 = _M0L6_2atmpS5560;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1906);
    }
    break;
  }
  _M0L4timeS5561 = _M0L1mS1812->$3;
  #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt12update__time(_M0L4timeS5561, _M0L2dtS1817);
  return 0;
}

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0FP26RiantR8snn__mbt7compose(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L4popsS1809,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS1810,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L11stims_2eoptS1798,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L14monitors_2eoptS1801,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L10stdp_2eoptS1804,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L9stp_2eoptS1807
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L5stimsS1797;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L8monitorsS1800;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L4stdpS1803;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L3stpS1806;
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _result_5864;
  if (_M0L11stims_2eoptS1798 == 0) {
    void** _M0L6_2atmpS5369 = (void**)moonbit_empty_ref_array;
    _M0L5stimsS1797
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE));
    Moonbit_object_header(_M0L5stimsS1797)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
    _M0L5stimsS1797->$0 = _M0L6_2atmpS5369;
    _M0L5stimsS1797->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L7_2aSomeS1799 =
      _M0L11stims_2eoptS1798;
    if (_M0L7_2aSomeS1799) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1799);
    }
    _M0L5stimsS1797 = _M0L7_2aSomeS1799;
  }
  if (_M0L14monitors_2eoptS1801 == 0) {
    struct _M0TP26RiantR8snn__mbt7Monitor** _M0L6_2atmpS5368 =
      (struct _M0TP26RiantR8snn__mbt7Monitor**)moonbit_empty_ref_array;
    _M0L8monitorsS1800
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE));
    Moonbit_object_header(_M0L8monitorsS1800)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
    _M0L8monitorsS1800->$0 = _M0L6_2atmpS5368;
    _M0L8monitorsS1800->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2aSomeS1802 =
      _M0L14monitors_2eoptS1801;
    if (_M0L7_2aSomeS1802) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1802);
    }
    _M0L8monitorsS1800 = _M0L7_2aSomeS1802;
  }
  if (_M0L10stdp_2eoptS1804 == 0) {
    void** _M0L6_2atmpS5367 = (void**)moonbit_empty_ref_array;
    _M0L4stdpS1803
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE));
    Moonbit_object_header(_M0L4stdpS1803)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
    _M0L4stdpS1803->$0 = _M0L6_2atmpS5367;
    _M0L4stdpS1803->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L7_2aSomeS1805 =
      _M0L10stdp_2eoptS1804;
    if (_M0L7_2aSomeS1805) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1805);
    }
    _M0L4stdpS1803 = _M0L7_2aSomeS1805;
  }
  if (_M0L9stp_2eoptS1807 == 0) {
    void** _M0L6_2atmpS5366 = (void**)moonbit_empty_ref_array;
    _M0L3stpS1806
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE));
    Moonbit_object_header(_M0L3stpS1806)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 27, 0);
    _M0L3stpS1806->$0 = _M0L6_2atmpS5366;
    _M0L3stpS1806->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L7_2aSomeS1808 =
      _M0L9stp_2eoptS1807;
    if (_M0L7_2aSomeS1808) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1808);
    }
    _M0L3stpS1806 = _M0L7_2aSomeS1808;
  }
  _result_5864
  = _M0FP26RiantR8snn__mbt15compose_2einner(_M0L4popsS1809, _M0L5connsS1810, _M0L5stimsS1797, _M0L8monitorsS1800, _M0L4stdpS1803, _M0L3stpS1806);
  moonbit_decref_cycle_free(_M0L5stimsS1797);
  moonbit_decref_cycle_free(_M0L8monitorsS1800);
  moonbit_decref_cycle_free(_M0L4stdpS1803);
  moonbit_decref_cycle_free(_M0L3stpS1806);
  return _result_5864;
}

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0FP26RiantR8snn__mbt15compose_2einner(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L4popsS1791,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS1792,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L5stimsS1793,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L8monitorsS1794,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L4stdpS1795,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L3stpS1796
) {
  struct _M0TP26RiantR8snn__mbt4Time* _M0L6_2atmpS5365;
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _block_5865;
  #line 79 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5365 = _M0MP26RiantR8snn__mbt4Time3new();
  moonbit_incref_cycle_free(_M0L4popsS1791);
  moonbit_incref_cycle_free(_M0L5connsS1792);
  moonbit_incref_cycle_free(_M0L5stimsS1793);
  moonbit_incref_cycle_free(_M0L8monitorsS1794);
  moonbit_incref_cycle_free(_M0L4stdpS1795);
  moonbit_incref_cycle_free(_M0L3stpS1796);
  _block_5865
  = (struct _M0TP26RiantR8snn__mbt18HeterogeneousModel*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt18HeterogeneousModel));
  Moonbit_object_header(_block_5865)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 30, 0);
  _block_5865->$0 = _M0L4popsS1791;
  _block_5865->$1 = _M0L5connsS1792;
  _block_5865->$2 = _M0L5stimsS1793;
  _block_5865->$3 = _M0L6_2atmpS5365;
  _block_5865->$4 = _M0L8monitorsS1794;
  _block_5865->$5 = _M0L4stdpS1795;
  _block_5865->$6 = _M0L3stpS1796;
  return _block_5865;
}

int32_t _M0FP26RiantR8snn__mbt14stimulate__any(
  void* _M0L1sS1777,
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS1765,
  float _M0L2dtS1772
) {
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L1xS1763;
  float _M0L1wS1764;
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L1xS1767;
  struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L1xS1769;
  struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L1xS1771;
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L1xS1774;
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L1xS1776;
  float _M0L6_2atmpS5364;
  float _M0L6_2atmpS5363;
  float _M0L6_2atmpS5362;
  float _M0L6_2atmpS5361;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  switch (Moonbit_object_tag(_M0L1sS1777)) {
    case 0: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__* _M0L14_2aPoissonIF__S1778 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__*)_M0L1sS1777;
      struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L4_2axS1779 =
        _M0L14_2aPoissonIF__S1778->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1779);
      _M0L1xS1776 = _M0L4_2axS1779;
      goto join_1775;
      break;
    }
    
    case 1: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim14PoissonLayer__* _M0L17_2aPoissonLayer__S1780 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim14PoissonLayer__*)_M0L1sS1777;
      struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L4_2axS1781 =
        _M0L17_2aPoissonLayer__S1780->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1781);
      _M0L1xS1774 = _M0L4_2axS1781;
      goto join_1773;
      break;
    }
    
    case 2: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim12BalancedIF__* _M0L15_2aBalancedIF__S1782 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim12BalancedIF__*)_M0L1sS1777;
      struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L4_2axS1783 =
        _M0L15_2aBalancedIF__S1782->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1783);
      _M0L1xS1771 = _M0L4_2axS1783;
      goto join_1770;
      break;
    }
    
    case 3: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__* _M0L14_2aCurrentIF__S1784 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__*)_M0L1sS1777;
      struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L4_2axS1785 =
        _M0L14_2aCurrentIF__S1784->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1785);
      _M0L1xS1769 = _M0L4_2axS1785;
      goto join_1768;
      break;
    }
    
    case 4: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__* _M0L15_2aCurrentArr__S1786 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__*)_M0L1sS1777;
      struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L4_2axS1787 =
        _M0L15_2aCurrentArr__S1786->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1787);
      _M0L1xS1767 = _M0L4_2axS1787;
      goto join_1766;
      break;
    }
    default: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__* _M0L14_2aTimedStim__S1788 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__*)_M0L1sS1777;
      struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L4_2axS1789 =
        _M0L14_2aTimedStim__S1788->$0;
      float _M0L4_2awS1790 = _M0L14_2aTimedStim__S1788->$1;
      moonbit_incref_cycle_free(_M0L4_2axS1789);
      _M0L1xS1763 = _M0L4_2axS1789;
      _M0L1wS1764 = _M0L4_2awS1790;
      goto join_1762;
      break;
    }
  }
  goto joinlet_5871;
  join_1775:;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5364 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1765);
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt13stimulate__if(_M0L1xS1776, _M0L6_2atmpS5364, _M0L2dtS1772);
  moonbit_decref_cycle_free(_M0L1xS1776);
  joinlet_5871:;
  goto joinlet_5870;
  join_1773:;
  #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5363 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1765);
  #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt16stimulate__layer(_M0L1xS1774, _M0L6_2atmpS5363, _M0L2dtS1772);
  moonbit_decref_cycle_free(_M0L1xS1774);
  joinlet_5870:;
  goto joinlet_5869;
  join_1770:;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5362 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1765);
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt19stimulate__balanced(_M0L1xS1771, _M0L6_2atmpS5362, _M0L2dtS1772);
  moonbit_decref_cycle_free(_M0L1xS1771);
  joinlet_5869:;
  goto joinlet_5868;
  join_1768:;
  #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt22stimulate__current__if(_M0L1xS1769);
  moonbit_decref_cycle_free(_M0L1xS1769);
  joinlet_5868:;
  goto joinlet_5867;
  join_1766:;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt25stimulate__current__array(_M0L1xS1767);
  moonbit_decref_cycle_free(_M0L1xS1767);
  joinlet_5867:;
  goto joinlet_5866;
  join_1762:;
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5361 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1765);
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt20stimulate__spiketime(_M0L1xS1763, _M0L6_2atmpS5361, _M0L1wS1764);
  moonbit_decref_cycle_free(_M0L1xS1763);
  joinlet_5866:;
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16spiking__connect(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1760,
  int32_t _M0L3preS1757,
  int32_t _M0L4postS1759,
  float _M0L1wS1761
) {
  int32_t _M0L8pre__idxS1756;
  int32_t _M0L9post__idxS1758;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5360;
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L8pre__idxS1756 = _M0L3preS1757 - 1;
  _M0L9post__idxS1758 = _M0L4postS1759 - 1;
  _M0L6matrixS5360 = _M0L1cS1760->$4;
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0MP26RiantR8snn__mbt15SparseMatrixCSR3set(_M0L6matrixS5360, _M0L8pre__idxS1756, _M0L9post__idxS1758, _M0L1wS1761);
  return 0;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse3new(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1753,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1754,
  moonbit_string_t _M0L3symS1755
) {
  int32_t _M0L1nS5358;
  int32_t _M0L1nS5359;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1752;
  float* _M0L6_2atmpS5357;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS5348;
  float* _M0L6_2atmpS5356;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS5349;
  float* _M0L6_2atmpS5355;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS5350;
  int32_t* _M0L6_2atmpS5354;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS5351;
  float* _M0L6_2atmpS5353;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS5352;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _block_5872;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS5358 = _M0L3preS1753->$2;
  _M0L1nS5359 = _M0L4postS1754->$2;
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6matrixS1752
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR5empty(_M0L1nS5358, _M0L1nS5359);
  _M0L6_2atmpS5357 = moonbit_empty_float_array;
  _M0L6_2atmpS5348
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS5348)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS5348->$0 = _M0L6_2atmpS5357;
  _M0L6_2atmpS5348->$1 = 0;
  _M0L6_2atmpS5356 = moonbit_empty_float_array;
  _M0L6_2atmpS5349
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS5349)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS5349->$0 = _M0L6_2atmpS5356;
  _M0L6_2atmpS5349->$1 = 0;
  _M0L6_2atmpS5355 = moonbit_empty_float_array;
  _M0L6_2atmpS5350
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS5350)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS5350->$0 = _M0L6_2atmpS5355;
  _M0L6_2atmpS5350->$1 = 0;
  _M0L6_2atmpS5354 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS5351
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS5351)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _M0L6_2atmpS5351->$0 = _M0L6_2atmpS5354;
  _M0L6_2atmpS5351->$1 = 0;
  _M0L6_2atmpS5353 = moonbit_empty_float_array;
  _M0L6_2atmpS5352
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS5352)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS5352->$0 = _M0L6_2atmpS5353;
  _M0L6_2atmpS5352->$1 = 0;
  moonbit_incref_cycle_free(_M0L3preS1753);
  moonbit_incref_cycle_free(_M0L4postS1754);
  moonbit_incref_cycle_free(_M0L3symS1755);
  _block_5872
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse));
  Moonbit_object_header(_block_5872)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 45, 0);
  _block_5872->$0 = _M0L3preS1753;
  _block_5872->$1 = _M0L4postS1754;
  _block_5872->$2 = _M0L3symS1755;
  _block_5872->$3 = (moonbit_string_t)moonbit_string_literal_0.data;
  _block_5872->$4 = _M0L6matrixS1752;
  _block_5872->$5 = _M0L6_2atmpS5348;
  _block_5872->$6 = _M0L6_2atmpS5349;
  _block_5872->$7 = _M0L6_2atmpS5350;
  _block_5872->$8 = _M0L6_2atmpS5351;
  _block_5872->$9 = _M0L6_2atmpS5352;
  return _block_5872;
}

int32_t _M0FP26RiantR8snn__mbt22istdp__potential__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1748,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1725,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1727,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1744,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1738,
  struct _M0TPB5ArrayGfE* _M0L7v__postS1735,
  struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L4varsS1731,
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS1729,
  float _M0L6t__nowS1723,
  float _M0L2dtS1732
) {
  int32_t _M0L6n__preS1724;
  int32_t _M0L7n__postS1726;
  float _M0L6tau__yS5347;
  float _M0L11inv__tau__yS1728;
  struct _M0TPB8MutLocalGiE* _M0L1jS1730;
  struct _M0TPB8MutLocalGiE* _M0L1iS1734;
  #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6n__preS1724 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1725);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L7n__postS1726 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1727);
  _M0L6tau__yS5347 = _M0L5paramS1729->$2;
  _M0L11inv__tau__yS1728 = 0x1p+0f / _M0L6tau__yS5347;
  _M0L1jS1730
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1730)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1730->$0 = 0;
  while (1) {
    int32_t _M0L3valS5264 = _M0L1jS1730->$0;
    if (_M0L3valS5264 < _M0L6n__preS1724) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS5265 = _M0L4varsS1731->$0;
      int32_t _M0L3valS5266 = _M0L1jS1730->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5275 = _M0L4varsS1731->$0;
      int32_t _M0L3valS5276 = _M0L1jS1730->$0;
      float _M0L6_2atmpS5268;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5273;
      int32_t _M0L3valS5274;
      float _M0L6_2atmpS5272;
      float _M0L6_2atmpS5271;
      float _M0L6_2atmpS5270;
      float _M0L6_2atmpS5269;
      float _M0L6_2atmpS5267;
      int32_t _M0L3valS5277;
      int32_t _M0L3valS5285;
      int32_t _M0L6_2atmpS5284;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5268
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5275, _M0L3valS5276);
      _M0L4tpreS5273 = _M0L4varsS1731->$0;
      _M0L3valS5274 = _M0L1jS1730->$0;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5272
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5273, _M0L3valS5274);
      _M0L6_2atmpS5271 = -_M0L6_2atmpS5272;
      _M0L6_2atmpS5270 = _M0L2dtS1732 * _M0L6_2atmpS5271;
      _M0L6_2atmpS5269 = _M0L6_2atmpS5270 * _M0L11inv__tau__yS1728;
      _M0L6_2atmpS5267 = _M0L6_2atmpS5268 + _M0L6_2atmpS5269;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS5265, _M0L3valS5266, _M0L6_2atmpS5267);
      _M0L3valS5277 = _M0L1jS1730->$0;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1725, _M0L3valS5277)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS5278 = _M0L4varsS1731->$0;
        int32_t _M0L3valS5279 = _M0L1jS1730->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS5282 = _M0L4varsS1731->$0;
        int32_t _M0L3valS5283 = _M0L1jS1730->$0;
        float _M0L6_2atmpS5281;
        float _M0L6_2atmpS5280;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5281
        = _M0MPC15array5Array2atGfE(_M0L4tpreS5282, _M0L3valS5283);
        _M0L6_2atmpS5280 = _M0L6_2atmpS5281 + 0x1p+0f;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS5278, _M0L3valS5279, _M0L6_2atmpS5280);
      }
      _M0L3valS5285 = _M0L1jS1730->$0;
      _M0L6_2atmpS5284 = _M0L3valS5285 + 1;
      _M0L1jS1730->$0 = _M0L6_2atmpS5284;
      continue;
    }
    break;
  }
  _M0L1iS1734
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1734)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1734->$0 = 0;
  while (1) {
    int32_t _M0L3valS5286 = _M0L1iS1734->$0;
    if (_M0L3valS5286 < _M0L7n__postS1726) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS5287 = _M0L4varsS1731->$1;
      int32_t _M0L3valS5288 = _M0L1iS1734->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5300 = _M0L4varsS1731->$1;
      int32_t _M0L3valS5301 = _M0L1iS1734->$0;
      float _M0L6_2atmpS5290;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5298;
      int32_t _M0L3valS5299;
      float _M0L6_2atmpS5295;
      int32_t _M0L3valS5297;
      float _M0L6_2atmpS5296;
      float _M0L6_2atmpS5294;
      float _M0L6_2atmpS5293;
      float _M0L6_2atmpS5292;
      float _M0L6_2atmpS5291;
      float _M0L6_2atmpS5289;
      int32_t _M0L3valS5302;
      int32_t _M0L3valS5310;
      int32_t _M0L6_2atmpS5309;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5290
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5300, _M0L3valS5301);
      _M0L5tpostS5298 = _M0L4varsS1731->$1;
      _M0L3valS5299 = _M0L1iS1734->$0;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5295
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5298, _M0L3valS5299);
      _M0L3valS5297 = _M0L1iS1734->$0;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5296
      = _M0MPC15array5Array2atGfE(_M0L7v__postS1735, _M0L3valS5297);
      _M0L6_2atmpS5294 = _M0L6_2atmpS5295 - _M0L6_2atmpS5296;
      _M0L6_2atmpS5293 = -_M0L6_2atmpS5294;
      _M0L6_2atmpS5292 = _M0L2dtS1732 * _M0L6_2atmpS5293;
      _M0L6_2atmpS5291 = _M0L6_2atmpS5292 * _M0L11inv__tau__yS1728;
      _M0L6_2atmpS5289 = _M0L6_2atmpS5290 + _M0L6_2atmpS5291;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS5287, _M0L3valS5288, _M0L6_2atmpS5289);
      _M0L3valS5302 = _M0L1iS1734->$0;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1727, _M0L3valS5302)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS5303 = _M0L4varsS1731->$1;
        int32_t _M0L3valS5304 = _M0L1iS1734->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS5307 = _M0L4varsS1731->$1;
        int32_t _M0L3valS5308 = _M0L1iS1734->$0;
        float _M0L6_2atmpS5306;
        float _M0L6_2atmpS5305;
        #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5306
        = _M0MPC15array5Array2atGfE(_M0L5tpostS5307, _M0L3valS5308);
        _M0L6_2atmpS5305 = _M0L6_2atmpS5306 + 0x1p+0f;
        #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS5303, _M0L3valS5304, _M0L6_2atmpS5305);
      }
      _M0L3valS5310 = _M0L1iS1734->$0;
      _M0L6_2atmpS5309 = _M0L3valS5310 + 1;
      _M0L1iS1734->$0 = _M0L6_2atmpS5309;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1734);
    }
    break;
  }
  _M0L1jS1730->$0 = 0;
  while (1) {
    int32_t _M0L3valS5311 = _M0L1jS1730->$0;
    if (_M0L3valS5311 < _M0L6n__preS1724) {
      int32_t _M0L3valS5346 = _M0L1jS1730->$0;
      int32_t _M0L5startS1737;
      int32_t _M0L3valS5345;
      int32_t _M0L6_2atmpS5344;
      int32_t _M0L3endS1739;
      int32_t _M0L3valS5343;
      int32_t _M0L10pre__firedS1740;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5341;
      int32_t _M0L3valS5342;
      float _M0L7tpre__jS1741;
      struct _M0TPB8MutLocalGiE* _M0L1sS1742;
      int32_t _M0L3valS5340;
      int32_t _M0L6_2atmpS5339;
      #line 358 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L5startS1737
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1738, _M0L3valS5346);
      _M0L3valS5345 = _M0L1jS1730->$0;
      _M0L6_2atmpS5344 = _M0L3valS5345 + 1;
      #line 359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L3endS1739
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1738, _M0L6_2atmpS5344);
      _M0L3valS5343 = _M0L1jS1730->$0;
      #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L10pre__firedS1740
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1725, _M0L3valS5343);
      _M0L4tpreS5341 = _M0L4varsS1731->$0;
      _M0L3valS5342 = _M0L1jS1730->$0;
      #line 361 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L7tpre__jS1741
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5341, _M0L3valS5342);
      _M0L1sS1742
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1742)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1742->$0 = _M0L5startS1737;
      while (1) {
        int32_t _M0L3valS5312 = _M0L1sS1742->$0;
        if (_M0L3valS5312 < _M0L3endS1739) {
          int32_t _M0L3valS5338 = _M0L1sS1742->$0;
          int32_t _M0L9post__idxS1743;
          int32_t _M0L11post__firedS1745;
          struct _M0TPB5ArrayGfE* _M0L5tpostS5337;
          float _M0L8tpost__iS1746;
          int32_t _M0L3valS5327;
          float _M0L6_2atmpS5325;
          float _M0L6w__minS5326;
          int32_t _M0L3valS5332;
          float _M0L6_2atmpS5330;
          float _M0L6w__maxS5331;
          int32_t _M0L3valS5336;
          int32_t _M0L6_2atmpS5335;
          #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L9post__idxS1743
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1744, _M0L3valS5338);
          #line 365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L11post__firedS1745
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1727, _M0L9post__idxS1743);
          _M0L5tpostS5337 = _M0L4varsS1731->$1;
          #line 366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L8tpost__iS1746
          = _M0MPC15array5Array2atGfE(_M0L5tpostS5337, _M0L9post__idxS1743);
          if (_M0L10pre__firedS1740) {
            float _M0L3etaS5317 = _M0L5paramS1729->$0;
            float _M0L2v0S5319 = _M0L5paramS1729->$1;
            float _M0L6_2atmpS5318 = _M0L8tpost__iS1746 - _M0L2v0S5319;
            float _M0L2dwS1747 = _M0L3etaS5317 * _M0L6_2atmpS5318;
            int32_t _M0L3valS5313 = _M0L1sS1742->$0;
            int32_t _M0L3valS5316 = _M0L1sS1742->$0;
            float _M0L6_2atmpS5315;
            float _M0L6_2atmpS5314;
            #line 369 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5315
            = _M0MPC15array5Array2atGfE(_M0L1wS1748, _M0L3valS5316);
            _M0L6_2atmpS5314 = _M0L6_2atmpS5315 + _M0L2dwS1747;
            #line 369 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1748, _M0L3valS5313, _M0L6_2atmpS5314);
          }
          if (_M0L11post__firedS1745) {
            float _M0L3etaS5324 = _M0L5paramS1729->$0;
            float _M0L2dwS1749 = _M0L3etaS5324 * _M0L7tpre__jS1741;
            int32_t _M0L3valS5320 = _M0L1sS1742->$0;
            int32_t _M0L3valS5323 = _M0L1sS1742->$0;
            float _M0L6_2atmpS5322;
            float _M0L6_2atmpS5321;
            #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5322
            = _M0MPC15array5Array2atGfE(_M0L1wS1748, _M0L3valS5323);
            _M0L6_2atmpS5321 = _M0L6_2atmpS5322 + _M0L2dwS1749;
            #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1748, _M0L3valS5320, _M0L6_2atmpS5321);
          }
          _M0L3valS5327 = _M0L1sS1742->$0;
          #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5325
          = _M0MPC15array5Array2atGfE(_M0L1wS1748, _M0L3valS5327);
          _M0L6w__minS5326 = _M0L5paramS1729->$4;
          if (_M0L6_2atmpS5325 < _M0L6w__minS5326) {
            int32_t _M0L3valS5328 = _M0L1sS1742->$0;
            float _M0L6w__minS5329 = _M0L5paramS1729->$4;
            #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1748, _M0L3valS5328, _M0L6w__minS5329);
          }
          _M0L3valS5332 = _M0L1sS1742->$0;
          #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5330
          = _M0MPC15array5Array2atGfE(_M0L1wS1748, _M0L3valS5332);
          _M0L6w__maxS5331 = _M0L5paramS1729->$3;
          if (_M0L6_2atmpS5330 > _M0L6w__maxS5331) {
            int32_t _M0L3valS5333 = _M0L1sS1742->$0;
            float _M0L6w__maxS5334 = _M0L5paramS1729->$3;
            #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1748, _M0L3valS5333, _M0L6w__maxS5334);
          }
          _M0L3valS5336 = _M0L1sS1742->$0;
          _M0L6_2atmpS5335 = _M0L3valS5336 + 1;
          _M0L1sS1742->$0 = _M0L6_2atmpS5335;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1742);
        }
        break;
      }
      _M0L3valS5340 = _M0L1jS1730->$0;
      _M0L6_2atmpS5339 = _M0L3valS5340 + 1;
      _M0L1jS1730->$0 = _M0L6_2atmpS5339;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1730);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt17istdp__rate__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1719,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1697,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1699,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1715,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1709,
  struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L4varsS1703,
  struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS1701,
  float _M0L6t__nowS1695,
  float _M0L2dtS1704
) {
  int32_t _M0L6n__preS1696;
  int32_t _M0L7n__postS1698;
  float _M0L6tau__yS5263;
  float _M0L11inv__tau__yS1700;
  struct _M0TPB8MutLocalGiE* _M0L1jS1702;
  struct _M0TPB8MutLocalGiE* _M0L1iS1706;
  #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6n__preS1696 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1697);
  #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L7n__postS1698 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1699);
  _M0L6tau__yS5263 = _M0L5paramS1701->$2;
  _M0L11inv__tau__yS1700 = 0x1p+0f / _M0L6tau__yS5263;
  _M0L1jS1702
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1702)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1702->$0 = 0;
  while (1) {
    int32_t _M0L3valS5180 = _M0L1jS1702->$0;
    if (_M0L3valS5180 < _M0L6n__preS1696) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS5181 = _M0L4varsS1703->$0;
      int32_t _M0L3valS5182 = _M0L1jS1702->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5191 = _M0L4varsS1703->$0;
      int32_t _M0L3valS5192 = _M0L1jS1702->$0;
      float _M0L6_2atmpS5184;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5189;
      int32_t _M0L3valS5190;
      float _M0L6_2atmpS5188;
      float _M0L6_2atmpS5187;
      float _M0L6_2atmpS5186;
      float _M0L6_2atmpS5185;
      float _M0L6_2atmpS5183;
      int32_t _M0L3valS5193;
      int32_t _M0L3valS5201;
      int32_t _M0L6_2atmpS5200;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5184
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5191, _M0L3valS5192);
      _M0L4tpreS5189 = _M0L4varsS1703->$0;
      _M0L3valS5190 = _M0L1jS1702->$0;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5188
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5189, _M0L3valS5190);
      _M0L6_2atmpS5187 = -_M0L6_2atmpS5188;
      _M0L6_2atmpS5186 = _M0L2dtS1704 * _M0L6_2atmpS5187;
      _M0L6_2atmpS5185 = _M0L6_2atmpS5186 * _M0L11inv__tau__yS1700;
      _M0L6_2atmpS5183 = _M0L6_2atmpS5184 + _M0L6_2atmpS5185;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS5181, _M0L3valS5182, _M0L6_2atmpS5183);
      _M0L3valS5193 = _M0L1jS1702->$0;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1697, _M0L3valS5193)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS5194 = _M0L4varsS1703->$0;
        int32_t _M0L3valS5195 = _M0L1jS1702->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS5198 = _M0L4varsS1703->$0;
        int32_t _M0L3valS5199 = _M0L1jS1702->$0;
        float _M0L6_2atmpS5197;
        float _M0L6_2atmpS5196;
        #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5197
        = _M0MPC15array5Array2atGfE(_M0L4tpreS5198, _M0L3valS5199);
        _M0L6_2atmpS5196 = _M0L6_2atmpS5197 + 0x1p+0f;
        #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS5194, _M0L3valS5195, _M0L6_2atmpS5196);
      }
      _M0L3valS5201 = _M0L1jS1702->$0;
      _M0L6_2atmpS5200 = _M0L3valS5201 + 1;
      _M0L1jS1702->$0 = _M0L6_2atmpS5200;
      continue;
    }
    break;
  }
  _M0L1iS1706
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1706)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1706->$0 = 0;
  while (1) {
    int32_t _M0L3valS5202 = _M0L1iS1706->$0;
    if (_M0L3valS5202 < _M0L7n__postS1698) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS5203 = _M0L4varsS1703->$1;
      int32_t _M0L3valS5204 = _M0L1iS1706->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5213 = _M0L4varsS1703->$1;
      int32_t _M0L3valS5214 = _M0L1iS1706->$0;
      float _M0L6_2atmpS5206;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5211;
      int32_t _M0L3valS5212;
      float _M0L6_2atmpS5210;
      float _M0L6_2atmpS5209;
      float _M0L6_2atmpS5208;
      float _M0L6_2atmpS5207;
      float _M0L6_2atmpS5205;
      int32_t _M0L3valS5215;
      int32_t _M0L3valS5223;
      int32_t _M0L6_2atmpS5222;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5206
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5213, _M0L3valS5214);
      _M0L5tpostS5211 = _M0L4varsS1703->$1;
      _M0L3valS5212 = _M0L1iS1706->$0;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5210
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5211, _M0L3valS5212);
      _M0L6_2atmpS5209 = -_M0L6_2atmpS5210;
      _M0L6_2atmpS5208 = _M0L2dtS1704 * _M0L6_2atmpS5209;
      _M0L6_2atmpS5207 = _M0L6_2atmpS5208 * _M0L11inv__tau__yS1700;
      _M0L6_2atmpS5205 = _M0L6_2atmpS5206 + _M0L6_2atmpS5207;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS5203, _M0L3valS5204, _M0L6_2atmpS5205);
      _M0L3valS5215 = _M0L1iS1706->$0;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1699, _M0L3valS5215)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS5216 = _M0L4varsS1703->$1;
        int32_t _M0L3valS5217 = _M0L1iS1706->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS5220 = _M0L4varsS1703->$1;
        int32_t _M0L3valS5221 = _M0L1iS1706->$0;
        float _M0L6_2atmpS5219;
        float _M0L6_2atmpS5218;
        #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5219
        = _M0MPC15array5Array2atGfE(_M0L5tpostS5220, _M0L3valS5221);
        _M0L6_2atmpS5218 = _M0L6_2atmpS5219 + 0x1p+0f;
        #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS5216, _M0L3valS5217, _M0L6_2atmpS5218);
      }
      _M0L3valS5223 = _M0L1iS1706->$0;
      _M0L6_2atmpS5222 = _M0L3valS5223 + 1;
      _M0L1iS1706->$0 = _M0L6_2atmpS5222;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1706);
    }
    break;
  }
  _M0L1jS1702->$0 = 0;
  while (1) {
    int32_t _M0L3valS5224 = _M0L1jS1702->$0;
    if (_M0L3valS5224 < _M0L6n__preS1696) {
      int32_t _M0L3valS5262 = _M0L1jS1702->$0;
      int32_t _M0L5startS1708;
      int32_t _M0L3valS5261;
      int32_t _M0L6_2atmpS5260;
      int32_t _M0L3endS1710;
      int32_t _M0L3valS5259;
      int32_t _M0L10pre__firedS1711;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5257;
      int32_t _M0L3valS5258;
      float _M0L7tpre__jS1712;
      struct _M0TPB8MutLocalGiE* _M0L1sS1713;
      int32_t _M0L3valS5256;
      int32_t _M0L6_2atmpS5255;
      #line 179 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L5startS1708
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1709, _M0L3valS5262);
      _M0L3valS5261 = _M0L1jS1702->$0;
      _M0L6_2atmpS5260 = _M0L3valS5261 + 1;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L3endS1710
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1709, _M0L6_2atmpS5260);
      _M0L3valS5259 = _M0L1jS1702->$0;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L10pre__firedS1711
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1697, _M0L3valS5259);
      _M0L4tpreS5257 = _M0L4varsS1703->$0;
      _M0L3valS5258 = _M0L1jS1702->$0;
      #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L7tpre__jS1712
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5257, _M0L3valS5258);
      _M0L1sS1713
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1713)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1713->$0 = _M0L5startS1708;
      while (1) {
        int32_t _M0L3valS5225 = _M0L1sS1713->$0;
        if (_M0L3valS5225 < _M0L3endS1710) {
          int32_t _M0L3valS5254 = _M0L1sS1713->$0;
          int32_t _M0L9post__idxS1714;
          int32_t _M0L11post__firedS1716;
          struct _M0TPB5ArrayGfE* _M0L5tpostS5253;
          float _M0L8tpost__iS1717;
          int32_t _M0L3valS5243;
          float _M0L6_2atmpS5241;
          float _M0L6w__minS5242;
          int32_t _M0L3valS5248;
          float _M0L6_2atmpS5246;
          float _M0L6w__maxS5247;
          int32_t _M0L3valS5252;
          int32_t _M0L6_2atmpS5251;
          #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L9post__idxS1714
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1715, _M0L3valS5254);
          #line 186 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L11post__firedS1716
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1699, _M0L9post__idxS1714);
          _M0L5tpostS5253 = _M0L4varsS1703->$1;
          #line 187 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L8tpost__iS1717
          = _M0MPC15array5Array2atGfE(_M0L5tpostS5253, _M0L9post__idxS1714);
          if (_M0L10pre__firedS1711) {
            float _M0L3etaS5230 = _M0L5paramS1701->$0;
            float _M0L1rS5235 = _M0L5paramS1701->$1;
            float _M0L6_2atmpS5233 = 0x1p+1f * _M0L1rS5235;
            float _M0L6tau__yS5234 = _M0L5paramS1701->$2;
            float _M0L6_2atmpS5232 = _M0L6_2atmpS5233 * _M0L6tau__yS5234;
            float _M0L6_2atmpS5231 = _M0L8tpost__iS1717 - _M0L6_2atmpS5232;
            float _M0L2dwS1718 = _M0L3etaS5230 * _M0L6_2atmpS5231;
            int32_t _M0L3valS5226 = _M0L1sS1713->$0;
            int32_t _M0L3valS5229 = _M0L1sS1713->$0;
            float _M0L6_2atmpS5228;
            float _M0L6_2atmpS5227;
            #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5228
            = _M0MPC15array5Array2atGfE(_M0L1wS1719, _M0L3valS5229);
            _M0L6_2atmpS5227 = _M0L6_2atmpS5228 + _M0L2dwS1718;
            #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1719, _M0L3valS5226, _M0L6_2atmpS5227);
          }
          if (_M0L11post__firedS1716) {
            float _M0L3etaS5240 = _M0L5paramS1701->$0;
            float _M0L2dwS1720 = _M0L3etaS5240 * _M0L7tpre__jS1712;
            int32_t _M0L3valS5236 = _M0L1sS1713->$0;
            int32_t _M0L3valS5239 = _M0L1sS1713->$0;
            float _M0L6_2atmpS5238;
            float _M0L6_2atmpS5237;
            #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5238
            = _M0MPC15array5Array2atGfE(_M0L1wS1719, _M0L3valS5239);
            _M0L6_2atmpS5237 = _M0L6_2atmpS5238 + _M0L2dwS1720;
            #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1719, _M0L3valS5236, _M0L6_2atmpS5237);
          }
          _M0L3valS5243 = _M0L1sS1713->$0;
          #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5241
          = _M0MPC15array5Array2atGfE(_M0L1wS1719, _M0L3valS5243);
          _M0L6w__minS5242 = _M0L5paramS1701->$4;
          if (_M0L6_2atmpS5241 < _M0L6w__minS5242) {
            int32_t _M0L3valS5244 = _M0L1sS1713->$0;
            float _M0L6w__minS5245 = _M0L5paramS1701->$4;
            #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1719, _M0L3valS5244, _M0L6w__minS5245);
          }
          _M0L3valS5248 = _M0L1sS1713->$0;
          #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5246
          = _M0MPC15array5Array2atGfE(_M0L1wS1719, _M0L3valS5248);
          _M0L6w__maxS5247 = _M0L5paramS1701->$3;
          if (_M0L6_2atmpS5246 > _M0L6w__maxS5247) {
            int32_t _M0L3valS5249 = _M0L1sS1713->$0;
            float _M0L6w__maxS5250 = _M0L5paramS1701->$3;
            #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1719, _M0L3valS5249, _M0L6w__maxS5250);
          }
          _M0L3valS5252 = _M0L1sS1713->$0;
          _M0L6_2atmpS5251 = _M0L3valS5252 + 1;
          _M0L1sS1713->$0 = _M0L6_2atmpS5251;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1713);
        }
        break;
      }
      _M0L3valS5256 = _M0L1jS1702->$0;
      _M0L6_2atmpS5255 = _M0L3valS5256 + 1;
      _M0L1jS1702->$0 = _M0L6_2atmpS5255;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1702);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter3new(
  
) {
  float _M0L1cS1693;
  float _M0L2glS1694;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_5881;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS1693 = -0x1p+0f;
  _M0L2glS1694 = -0x1p+0f;
  _block_5881
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_5881)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5881->$0 = _M0L1cS1693;
  _block_5881->$1 = _M0L2glS1694;
  _block_5881->$2 = 0x1.ep+3f;
  _block_5881->$3 = -0x1.9p+5f;
  _block_5881->$4 = -0x1.ep+5f;
  _block_5881->$5 = -0x1.18p+6f;
  _block_5881->$6 = 0x1.eb851eb851eb8p-5f;
  _block_5881->$7 = 0x1p+1f;
  _block_5881->$8 = 0x0p+0f;
  _block_5881->$9 = 0x0p+0f;
  _block_5881->$10 = 0x0p+0f;
  return _block_5881;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS1667,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS1669,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1672
) {
  struct _M0TPB5ArrayGfE* _M0L1vS1666;
  float _M0L2vtS5178;
  float _M0L2vrS5179;
  float _M0L6spreadS1668;
  int32_t _M0L7_2abindS1670;
  int32_t _M0L1kS1671;
  struct _M0TPB5ArrayGfE* _M0L1wS1674;
  struct _M0TPB5ArrayGbE* _M0L4fireS1675;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1676;
  struct _M0TPB5ArrayGfE* _M0L1iS1677;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS1678;
  struct _M0TPB5ArrayGfE* _M0L2geS1679;
  struct _M0TPB5ArrayGfE* _M0L2giS1680;
  struct _M0TPB5ArrayGfE* _M0L2heS1681;
  struct _M0TPB5ArrayGfE* _M0L2hiS1682;
  struct _M0TPB5ArrayGfE* _M0L3gluS1683;
  struct _M0TPB5ArrayGfE* _M0L4gabaS1684;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1685;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1686;
  float _M0L4e__eS1687;
  float _M0L4e__iS1688;
  float _M0L3treS1689;
  float _M0L3tdeS1690;
  float _M0L3triS1691;
  float _M0L3tdiS1692;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS5177;
  struct _M0TP26RiantR8snn__mbt2IF* _block_5883;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS1666 = _M0MPC15array5Array4makeGfE(_M0L1nS1667, 0x0p+0f);
  _M0L2vtS5178 = _M0L5paramS1669->$3;
  _M0L2vrS5179 = _M0L5paramS1669->$4;
  _M0L6spreadS1668 = _M0L2vtS5178 - _M0L2vrS5179;
  _M0L7_2abindS1670 = 0;
  _M0L1kS1671 = _M0L7_2abindS1670;
  while (1) {
    if (_M0L1kS1671 < _M0L1nS1667) {
      float _M0L2vrS5173 = _M0L5paramS1669->$4;
      float _M0L6_2atmpS5175;
      float _M0L6_2atmpS5174;
      float _M0L6_2atmpS5172;
      int32_t _M0L6_2atmpS5176;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS5175 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1672);
      _M0L6_2atmpS5174 = _M0L6_2atmpS5175 * _M0L6spreadS1668;
      _M0L6_2atmpS5172 = _M0L2vrS5173 + _M0L6_2atmpS5174;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1666, _M0L1kS1671, _M0L6_2atmpS5172);
      _M0L6_2atmpS5176 = _M0L1kS1671 + 1;
      _M0L1kS1671 = _M0L6_2atmpS5176;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS1674 = _M0MPC15array5Array4makeGfE(_M0L1nS1667, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS1675 = _M0MPC15array5Array4makeGbE(_M0L1nS1667, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS1676 = _M0MPC15array5Array4makeGiE(_M0L1nS1667, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS1677 = _M0MPC15array5Array4makeGfE(_M0L1nS1667, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS1678 = _M0MPC15array5Array4makeGfE(_M0L1nS1667, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS1679 = _M0MPC15array5Array4makeGfE(_M0L1nS1667, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS1680 = _M0MPC15array5Array4makeGfE(_M0L1nS1667, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS1681 = _M0MPC15array5Array4makeGfE(_M0L1nS1667, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS1682 = _M0MPC15array5Array4makeGfE(_M0L1nS1667, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS1683 = _M0MPC15array5Array4makeGfE(_M0L1nS1667, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS1684 = _M0MPC15array5Array4makeGfE(_M0L1nS1667, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS1685 = _M0MPC15array5Array4makeGfE(_M0L1nS1667, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS1686 = _M0MPC15array5Array4makeGfE(_M0L1nS1667, 0x1p+0f);
  _M0L4e__eS1687 = 0x0p+0f;
  _M0L4e__iS1688 = -0x1.2cp+6f;
  _M0L3treS1689 = 0x1p+0f;
  _M0L3tdeS1690 = 0x1.8p+2f;
  _M0L3triS1691 = 0x1p-1f;
  _M0L3tdiS1692 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS5177 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS1669);
  _block_5883
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_5883)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
  _block_5883->$0 = _M0L5paramS1669;
  _block_5883->$1 = _M0L6_2atmpS5177;
  _block_5883->$2 = _M0L1nS1667;
  _block_5883->$3 = _M0L1vS1666;
  _block_5883->$4 = _M0L1wS1674;
  _block_5883->$5 = _M0L4fireS1675;
  _block_5883->$6 = _M0L4tabsS1676;
  _block_5883->$7 = _M0L1iS1677;
  _block_5883->$8 = _M0L9syn__currS1678;
  _block_5883->$9 = _M0L2geS1679;
  _block_5883->$10 = _M0L2giS1680;
  _block_5883->$11 = _M0L2heS1681;
  _block_5883->$12 = _M0L2hiS1682;
  _block_5883->$13 = _M0L3gluS1683;
  _block_5883->$14 = _M0L4gabaS1684;
  _block_5883->$15 = _M0L7gsyn__eS1685;
  _block_5883->$16 = _M0L7gsyn__iS1686;
  _block_5883->$17 = _M0L4e__eS1687;
  _block_5883->$18 = _M0L4e__iS1688;
  _block_5883->$19 = _M0L3treS1689;
  _block_5883->$20 = _M0L3tdeS1690;
  _block_5883->$21 = _M0L3triS1691;
  _block_5883->$22 = _M0L3tdiS1692;
  return _block_5883;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_5884;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_5884
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_5884)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5884->$0 = 0x1p+1f;
  return _block_5884;
}

int32_t _M0FP26RiantR8snn__mbt14integrate__any(
  void* _M0L1pS1647,
  float _M0L2dtS1630
) {
  struct _M0TP26RiantR8snn__mbt6HetRec* _M0L1xS1629;
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L1xS1632;
  struct _M0TP26RiantR8snn__mbt7Poisson* _M0L1xS1634;
  struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L1xS1636;
  struct _M0TP26RiantR8snn__mbt2HH* _M0L1xS1638;
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1xS1640;
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1xS1642;
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1xS1644;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1xS1646;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  switch (Moonbit_object_tag(_M0L1pS1647)) {
    case 0: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__* _M0L7_2aIF__S1648 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__*)_M0L1pS1647;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4_2axS1649 =
        _M0L7_2aIF__S1648->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1649);
      _M0L1xS1646 = _M0L4_2axS1649;
      goto join_1645;
      break;
    }
    
    case 1: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop6AdEx__* _M0L9_2aAdEx__S1650 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop6AdEx__*)_M0L1pS1647;
      struct _M0TP26RiantR8snn__mbt4AdEx* _M0L4_2axS1651 =
        _M0L9_2aAdEx__S1650->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1651);
      _M0L1xS1644 = _M0L4_2axS1651;
      goto join_1643;
      break;
    }
    
    case 2: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop12AdExSinExp__* _M0L15_2aAdExSinExp__S1652 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop12AdExSinExp__*)_M0L1pS1647;
      struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L4_2axS1653 =
        _M0L15_2aAdExSinExp__S1652->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1653);
      _M0L1xS1642 = _M0L4_2axS1653;
      goto join_1641;
      break;
    }
    
    case 3: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4IZ__* _M0L7_2aIZ__S1654 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4IZ__*)_M0L1pS1647;
      struct _M0TP26RiantR8snn__mbt2IZ* _M0L4_2axS1655 =
        _M0L7_2aIZ__S1654->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1655);
      _M0L1xS1640 = _M0L4_2axS1655;
      goto join_1639;
      break;
    }
    
    case 4: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__* _M0L7_2aHH__S1656 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__*)_M0L1pS1647;
      struct _M0TP26RiantR8snn__mbt2HH* _M0L4_2axS1657 =
        _M0L7_2aHH__S1656->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1657);
      _M0L1xS1638 = _M0L4_2axS1657;
      goto join_1637;
      break;
    }
    
    case 5: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__* _M0L7_2aML__S1658 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__*)_M0L1pS1647;
      struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L4_2axS1659 =
        _M0L7_2aML__S1658->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1659);
      _M0L1xS1636 = _M0L4_2axS1659;
      goto join_1635;
      break;
    }
    
    case 6: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__* _M0L12_2aPoisson__S1660 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__*)_M0L1pS1647;
      struct _M0TP26RiantR8snn__mbt7Poisson* _M0L4_2axS1661 =
        _M0L12_2aPoisson__S1660->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1661);
      _M0L1xS1634 = _M0L4_2axS1661;
      goto join_1633;
      break;
    }
    
    case 7: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4WC__* _M0L7_2aWC__S1662 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4WC__*)_M0L1pS1647;
      struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L4_2axS1663 =
        _M0L7_2aWC__S1662->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1663);
      _M0L1xS1632 = _M0L4_2axS1663;
      goto join_1631;
      break;
    }
    default: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__* _M0L11_2aHetRec__S1664 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__*)_M0L1pS1647;
      struct _M0TP26RiantR8snn__mbt6HetRec* _M0L4_2axS1665 =
        _M0L11_2aHetRec__S1664->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1665);
      _M0L1xS1629 = _M0L4_2axS1665;
      goto join_1628;
      break;
    }
  }
  goto joinlet_5893;
  join_1645:;
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt14step__synapses(_M0L1xS1646, _M0L2dtS1630);
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt17synaptic__current(_M0L1xS1646);
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt12step__neuron(_M0L1xS1646, _M0L2dtS1630);
  moonbit_decref_cycle_free(_M0L1xS1646);
  joinlet_5893:;
  goto joinlet_5892;
  join_1643:;
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt20adex__step__synapses(_M0L1xS1644, _M0L2dtS1630);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt23adex__synaptic__current(_M0L1xS1644);
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt10step__adex(_M0L1xS1644, _M0L2dtS1630);
  moonbit_decref_cycle_free(_M0L1xS1644);
  joinlet_5892:;
  goto joinlet_5891;
  join_1641:;
  #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(_M0L1xS1642, _M0L2dtS1630);
  #line 44 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(_M0L1xS1642);
  #line 45 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt18step__adex__sinexp(_M0L1xS1642, _M0L2dtS1630);
  moonbit_decref_cycle_free(_M0L1xS1642);
  joinlet_5891:;
  goto joinlet_5890;
  join_1639:;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__iz(_M0L1xS1640, _M0L2dtS1630);
  moonbit_decref_cycle_free(_M0L1xS1640);
  joinlet_5890:;
  goto joinlet_5889;
  join_1637:;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__hh(_M0L1xS1638, _M0L2dtS1630);
  moonbit_decref_cycle_free(_M0L1xS1638);
  joinlet_5889:;
  goto joinlet_5888;
  join_1635:;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__ml(_M0L1xS1636, _M0L2dtS1630);
  moonbit_decref_cycle_free(_M0L1xS1636);
  joinlet_5888:;
  goto joinlet_5887;
  join_1633:;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt13step__poisson(_M0L1xS1634, _M0L2dtS1630);
  moonbit_decref_cycle_free(_M0L1xS1634);
  joinlet_5887:;
  goto joinlet_5886;
  join_1631:;
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__wc(_M0L1xS1632, _M0L2dtS1630);
  moonbit_decref_cycle_free(_M0L1xS1632);
  joinlet_5886:;
  goto joinlet_5885;
  join_1628:;
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt12step__hetrec(_M0L1xS1629, _M0L2dtS1630);
  moonbit_decref_cycle_free(_M0L1xS1629);
  joinlet_5885:;
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__wc(
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L1pS1623,
  float _M0L2dtS1626
) {
  int32_t _M0L1nS1622;
  int32_t _M0L7_2abindS1624;
  int32_t _M0L1kS1625;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _M0L1nS1622 = _M0L1pS1623->$1;
  _M0L7_2abindS1624 = 0;
  _M0L1kS1625 = _M0L7_2abindS1624;
  while (1) {
    if (_M0L1kS1625 < _M0L1nS1622) {
      struct _M0TPB5ArrayGfE* _M0L1xS5152 = _M0L1pS1623->$2;
      struct _M0TPB5ArrayGfE* _M0L1xS5165 = _M0L1pS1623->$2;
      float _M0L6_2atmpS5154;
      struct _M0TPB5ArrayGfE* _M0L1xS5164;
      float _M0L6_2atmpS5163;
      float _M0L6_2atmpS5160;
      struct _M0TPB5ArrayGfE* _M0L1gS5162;
      float _M0L6_2atmpS5161;
      float _M0L6_2atmpS5157;
      struct _M0TPB5ArrayGfE* _M0L1iS5159;
      float _M0L6_2atmpS5158;
      float _M0L6_2atmpS5156;
      float _M0L6_2atmpS5155;
      float _M0L6_2atmpS5153;
      struct _M0TPB5ArrayGfE* _M0L1rS5166;
      struct _M0TPB5ArrayGfE* _M0L1xS5169;
      float _M0L6_2atmpS5168;
      float _M0L6_2atmpS5167;
      struct _M0TPB5ArrayGfE* _M0L1gS5170;
      int32_t _M0L6_2atmpS5171;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5154 = _M0MPC15array5Array2atGfE(_M0L1xS5165, _M0L1kS1625);
      _M0L1xS5164 = _M0L1pS1623->$2;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5163 = _M0MPC15array5Array2atGfE(_M0L1xS5164, _M0L1kS1625);
      _M0L6_2atmpS5160 = -_M0L6_2atmpS5163;
      _M0L1gS5162 = _M0L1pS1623->$4;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5161 = _M0MPC15array5Array2atGfE(_M0L1gS5162, _M0L1kS1625);
      _M0L6_2atmpS5157 = _M0L6_2atmpS5160 + _M0L6_2atmpS5161;
      _M0L1iS5159 = _M0L1pS1623->$5;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5158 = _M0MPC15array5Array2atGfE(_M0L1iS5159, _M0L1kS1625);
      _M0L6_2atmpS5156 = _M0L6_2atmpS5157 + _M0L6_2atmpS5158;
      _M0L6_2atmpS5155 = _M0L2dtS1626 * _M0L6_2atmpS5156;
      _M0L6_2atmpS5153 = _M0L6_2atmpS5154 + _M0L6_2atmpS5155;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS5152, _M0L1kS1625, _M0L6_2atmpS5153);
      _M0L1rS5166 = _M0L1pS1623->$3;
      _M0L1xS5169 = _M0L1pS1623->$2;
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5168 = _M0MPC15array5Array2atGfE(_M0L1xS5169, _M0L1kS1625);
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5167 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS5168);
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1rS5166, _M0L1kS1625, _M0L6_2atmpS5167);
      _M0L1gS5170 = _M0L1pS1623->$4;
      #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS5170, _M0L1kS1625, 0x0p+0f);
      _M0L6_2atmpS5171 = _M0L1kS1625 + 1;
      _M0L1kS1625 = _M0L6_2atmpS5171;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13step__poisson(
  struct _M0TP26RiantR8snn__mbt7Poisson* _M0L1pS1615,
  float _M0L2dtS1617
) {
  int32_t _M0L1nS1614;
  struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter* _M0L5paramS5151;
  float _M0L4rateS5150;
  float _M0L8rate__dtS1616;
  int32_t _M0L7_2abindS1618;
  int32_t _M0L1iS1619;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
  _M0L1nS1614 = _M0L1pS1615->$1;
  _M0L5paramS5151 = _M0L1pS1615->$0;
  _M0L4rateS5150 = _M0L5paramS5151->$0;
  _M0L8rate__dtS1616 = _M0L4rateS5150 * _M0L2dtS1617;
  _M0L7_2abindS1618 = 0;
  _M0L1iS1619 = _M0L7_2abindS1618;
  while (1) {
    if (_M0L1iS1619 < _M0L1nS1614) {
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS5148 = _M0L1pS1615->$4;
      float _M0L1uS1620;
      struct _M0TPB5ArrayGfE* _M0L9randcacheS5145;
      struct _M0TPB5ArrayGbE* _M0L4fireS5146;
      int32_t _M0L6_2atmpS5147;
      int32_t _M0L6_2atmpS5149;
      #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0L1uS1620 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS5148);
      _M0L9randcacheS5145 = _M0L1pS1615->$3;
      #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0MPC15array5Array3setGfE(_M0L9randcacheS5145, _M0L1iS1619, _M0L1uS1620);
      _M0L4fireS5146 = _M0L1pS1615->$2;
      _M0L6_2atmpS5147 = _M0L1uS1620 < _M0L8rate__dtS1616;
      #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS5146, _M0L1iS1619, _M0L6_2atmpS5147);
      _M0L6_2atmpS5149 = _M0L1iS1619 + 1;
      _M0L1iS1619 = _M0L6_2atmpS5149;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__ml(
  struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L1pS1574,
  float _M0L2dtS1603
) {
  int32_t _M0L1nS1573;
  struct _M0TP26RiantR8snn__mbt20MorrisLecarParameter* _M0L3p__S1575;
  float _M0L2cmS1576;
  float _M0L2elS1577;
  float _M0L2ekS1578;
  float _M0L3ecaS1579;
  float _M0L2glS1580;
  float _M0L2gkS1581;
  float _M0L3gcaS1582;
  float _M0L6tau__eS1583;
  float _M0L6tau__iS1584;
  float _M0L2v1S1585;
  float _M0L2v2S1586;
  float _M0L2v3S1587;
  float _M0L2v4S1588;
  float _M0L3phiS1589;
  float _M0L4e__eS1590;
  float _M0L4e__iS1591;
  int32_t _M0L7_2abindS1592;
  int32_t _M0L1iS1593;
  int32_t _M0L7_2abindS1605;
  int32_t _M0L1iS1606;
  int32_t _M0L7_2abindS1608;
  int32_t _M0L1iS1609;
  int32_t _M0L7_2abindS1611;
  int32_t _M0L1iS1612;
  #line 86 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
  _M0L1nS1573 = _M0L1pS1574->$1;
  _M0L3p__S1575 = _M0L1pS1574->$0;
  _M0L2cmS1576 = _M0L3p__S1575->$0;
  _M0L2elS1577 = _M0L3p__S1575->$1;
  _M0L2ekS1578 = _M0L3p__S1575->$2;
  _M0L3ecaS1579 = _M0L3p__S1575->$3;
  _M0L2glS1580 = _M0L3p__S1575->$4;
  _M0L2gkS1581 = _M0L3p__S1575->$5;
  _M0L3gcaS1582 = _M0L3p__S1575->$6;
  _M0L6tau__eS1583 = _M0L3p__S1575->$7;
  _M0L6tau__iS1584 = _M0L3p__S1575->$8;
  _M0L2v1S1585 = _M0L3p__S1575->$9;
  _M0L2v2S1586 = _M0L3p__S1575->$10;
  _M0L2v3S1587 = _M0L3p__S1575->$11;
  _M0L2v4S1588 = _M0L3p__S1575->$12;
  _M0L3phiS1589 = _M0L3p__S1575->$13;
  _M0L4e__eS1590 = _M0L3p__S1575->$14;
  _M0L4e__iS1591 = _M0L3p__S1575->$15;
  _M0L7_2abindS1592 = 0;
  _M0L1iS1593 = _M0L7_2abindS1592;
  while (1) {
    if (_M0L1iS1593 < _M0L1nS1573) {
      struct _M0TPB5ArrayGfE* _M0L1vS5099 = _M0L1pS1574->$2;
      float _M0L1vS1594;
      struct _M0TPB5ArrayGfE* _M0L1wS5098;
      float _M0L1wS1595;
      float _M0L6_2atmpS5097;
      float _M0L6_2atmpS5096;
      float _M0L6_2atmpS5095;
      float _M0L6_2atmpS5094;
      float _M0L5m__ssS1596;
      struct _M0TPB5ArrayGfE* _M0L1iS5093;
      float _M0L6_2atmpS5090;
      float _M0L6_2atmpS5092;
      float _M0L6_2atmpS5091;
      float _M0L6_2atmpS5086;
      float _M0L6_2atmpS5089;
      float _M0L6_2atmpS5088;
      float _M0L6_2atmpS5087;
      float _M0L6_2atmpS5082;
      float _M0L6_2atmpS5085;
      float _M0L6_2atmpS5084;
      float _M0L6_2atmpS5083;
      float _M0L2dvS1597;
      float _M0L6_2atmpS5081;
      float _M0L6_2atmpS5080;
      float _M0L6_2atmpS5079;
      float _M0L6_2atmpS5078;
      float _M0L5n__ssS1598;
      float _M0L6_2atmpS5076;
      float _M0L6_2atmpS5077;
      float _M0L9cosh__argS1599;
      float _M0L6_2atmpS5073;
      float _M0L6_2atmpS5075;
      float _M0L6_2atmpS5074;
      float _M0L6_2atmpS5072;
      float _M0L9cosh__valS1600;
      float _M0L6_2atmpS5070;
      float _M0L3tauS1601;
      float _M0L6_2atmpS5069;
      float _M0L2dwS1602;
      struct _M0TPB5ArrayGfE* _M0L1vS5062;
      float _M0L6_2atmpS5065;
      float _M0L6_2atmpS5064;
      float _M0L6_2atmpS5063;
      struct _M0TPB5ArrayGfE* _M0L1wS5066;
      float _M0L6_2atmpS5068;
      float _M0L6_2atmpS5067;
      int32_t _M0L6_2atmpS5100;
      #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L1vS1594 = _M0MPC15array5Array2atGfE(_M0L1vS5099, _M0L1iS1593);
      _M0L1wS5098 = _M0L1pS1574->$3;
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L1wS1595 = _M0MPC15array5Array2atGfE(_M0L1wS5098, _M0L1iS1593);
      _M0L6_2atmpS5097 = _M0L1vS1594 - _M0L2v1S1585;
      _M0L6_2atmpS5096 = _M0L6_2atmpS5097 / _M0L2v2S1586;
      #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5095 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS5096);
      _M0L6_2atmpS5094 = 0x1p+0f + _M0L6_2atmpS5095;
      _M0L5m__ssS1596 = 0x1p-1f * _M0L6_2atmpS5094;
      _M0L1iS5093 = _M0L1pS1574->$5;
      #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5090 = _M0MPC15array5Array2atGfE(_M0L1iS5093, _M0L1iS1593);
      _M0L6_2atmpS5092 = _M0L2elS1577 - _M0L1vS1594;
      _M0L6_2atmpS5091 = _M0L2glS1580 * _M0L6_2atmpS5092;
      _M0L6_2atmpS5086 = _M0L6_2atmpS5090 + _M0L6_2atmpS5091;
      _M0L6_2atmpS5089 = _M0L3ecaS1579 - _M0L1vS1594;
      _M0L6_2atmpS5088 = _M0L3gcaS1582 * _M0L6_2atmpS5089;
      _M0L6_2atmpS5087 = _M0L6_2atmpS5088 * _M0L5m__ssS1596;
      _M0L6_2atmpS5082 = _M0L6_2atmpS5086 + _M0L6_2atmpS5087;
      _M0L6_2atmpS5085 = _M0L2ekS1578 - _M0L1vS1594;
      _M0L6_2atmpS5084 = _M0L2gkS1581 * _M0L6_2atmpS5085;
      _M0L6_2atmpS5083 = _M0L6_2atmpS5084 * _M0L1wS1595;
      _M0L2dvS1597 = _M0L6_2atmpS5082 + _M0L6_2atmpS5083;
      _M0L6_2atmpS5081 = _M0L1vS1594 - _M0L2v3S1587;
      _M0L6_2atmpS5080 = _M0L6_2atmpS5081 / _M0L2v4S1588;
      #line 112 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5079 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS5080);
      _M0L6_2atmpS5078 = 0x1p+0f + _M0L6_2atmpS5079;
      _M0L5n__ssS1598 = 0x1p-1f * _M0L6_2atmpS5078;
      _M0L6_2atmpS5076 = _M0L1vS1594 - _M0L2v3S1587;
      _M0L6_2atmpS5077 = 0x1p+1f * _M0L2v4S1588;
      _M0L9cosh__argS1599 = _M0L6_2atmpS5076 / _M0L6_2atmpS5077;
      #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5073 = _M0FP26RiantR8snn__mbt4expf(_M0L9cosh__argS1599);
      _M0L6_2atmpS5075 = -_M0L9cosh__argS1599;
      #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5074 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS5075);
      _M0L6_2atmpS5072 = _M0L6_2atmpS5073 + _M0L6_2atmpS5074;
      _M0L9cosh__valS1600 = 0x1p-1f * _M0L6_2atmpS5072;
      _M0L6_2atmpS5070 = _M0L3phiS1589 * _M0L9cosh__valS1600;
      if (_M0L6_2atmpS5070 != 0x0p+0f) {
        float _M0L6_2atmpS5071 = _M0L3phiS1589 * _M0L9cosh__valS1600;
        _M0L3tauS1601 = 0x1p+0f / _M0L6_2atmpS5071;
      } else {
        _M0L3tauS1601 = 0x0p+0f;
      }
      _M0L6_2atmpS5069 = _M0L5n__ssS1598 - _M0L1wS1595;
      _M0L2dwS1602 = _M0L6_2atmpS5069 / _M0L3tauS1601;
      _M0L1vS5062 = _M0L1pS1574->$2;
      _M0L6_2atmpS5065 = _M0L2dtS1603 / _M0L2cmS1576;
      _M0L6_2atmpS5064 = _M0L6_2atmpS5065 * _M0L2dvS1597;
      _M0L6_2atmpS5063 = _M0L1vS1594 + _M0L6_2atmpS5064;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5062, _M0L1iS1593, _M0L6_2atmpS5063);
      _M0L1wS5066 = _M0L1pS1574->$3;
      _M0L6_2atmpS5068 = _M0L2dtS1603 * _M0L2dwS1602;
      _M0L6_2atmpS5067 = _M0L1wS1595 + _M0L6_2atmpS5068;
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS5066, _M0L1iS1593, _M0L6_2atmpS5067);
      _M0L6_2atmpS5100 = _M0L1iS1593 + 1;
      _M0L1iS1593 = _M0L6_2atmpS5100;
      continue;
    }
    break;
  }
  _M0L7_2abindS1605 = 0;
  _M0L1iS1606 = _M0L7_2abindS1605;
  while (1) {
    if (_M0L1iS1606 < _M0L1nS1573) {
      struct _M0TPB5ArrayGfE* _M0L1vS5101 = _M0L1pS1574->$2;
      struct _M0TPB5ArrayGfE* _M0L1vS5119 = _M0L1pS1574->$2;
      float _M0L6_2atmpS5103;
      float _M0L6_2atmpS5105;
      struct _M0TPB5ArrayGfE* _M0L2geS5118;
      float _M0L6_2atmpS5114;
      struct _M0TPB5ArrayGfE* _M0L1vS5117;
      float _M0L6_2atmpS5116;
      float _M0L6_2atmpS5115;
      float _M0L6_2atmpS5107;
      struct _M0TPB5ArrayGfE* _M0L2giS5113;
      float _M0L6_2atmpS5109;
      struct _M0TPB5ArrayGfE* _M0L1vS5112;
      float _M0L6_2atmpS5111;
      float _M0L6_2atmpS5110;
      float _M0L6_2atmpS5108;
      float _M0L6_2atmpS5106;
      float _M0L6_2atmpS5104;
      float _M0L6_2atmpS5102;
      int32_t _M0L6_2atmpS5120;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5103 = _M0MPC15array5Array2atGfE(_M0L1vS5119, _M0L1iS1606);
      _M0L6_2atmpS5105 = _M0L2dtS1603 / _M0L2cmS1576;
      _M0L2geS5118 = _M0L1pS1574->$6;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5114 = _M0MPC15array5Array2atGfE(_M0L2geS5118, _M0L1iS1606);
      _M0L1vS5117 = _M0L1pS1574->$2;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5116 = _M0MPC15array5Array2atGfE(_M0L1vS5117, _M0L1iS1606);
      _M0L6_2atmpS5115 = _M0L4e__eS1590 - _M0L6_2atmpS5116;
      _M0L6_2atmpS5107 = _M0L6_2atmpS5114 * _M0L6_2atmpS5115;
      _M0L2giS5113 = _M0L1pS1574->$7;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5109 = _M0MPC15array5Array2atGfE(_M0L2giS5113, _M0L1iS1606);
      _M0L1vS5112 = _M0L1pS1574->$2;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5111 = _M0MPC15array5Array2atGfE(_M0L1vS5112, _M0L1iS1606);
      _M0L6_2atmpS5110 = _M0L4e__iS1591 - _M0L6_2atmpS5111;
      _M0L6_2atmpS5108 = _M0L6_2atmpS5109 * _M0L6_2atmpS5110;
      _M0L6_2atmpS5106 = _M0L6_2atmpS5107 + _M0L6_2atmpS5108;
      _M0L6_2atmpS5104 = _M0L6_2atmpS5105 * _M0L6_2atmpS5106;
      _M0L6_2atmpS5102 = _M0L6_2atmpS5103 + _M0L6_2atmpS5104;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5101, _M0L1iS1606, _M0L6_2atmpS5102);
      _M0L6_2atmpS5120 = _M0L1iS1606 + 1;
      _M0L1iS1606 = _M0L6_2atmpS5120;
      continue;
    }
    break;
  }
  _M0L7_2abindS1608 = 0;
  _M0L1iS1609 = _M0L7_2abindS1608;
  while (1) {
    if (_M0L1iS1609 < _M0L1nS1573) {
      struct _M0TPB5ArrayGfE* _M0L2geS5121 = _M0L1pS1574->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS5129 = _M0L1pS1574->$6;
      float _M0L6_2atmpS5123;
      struct _M0TPB5ArrayGfE* _M0L2geS5128;
      float _M0L6_2atmpS5127;
      float _M0L6_2atmpS5126;
      float _M0L6_2atmpS5125;
      float _M0L6_2atmpS5124;
      float _M0L6_2atmpS5122;
      struct _M0TPB5ArrayGfE* _M0L2giS5130;
      struct _M0TPB5ArrayGfE* _M0L2giS5138;
      float _M0L6_2atmpS5132;
      struct _M0TPB5ArrayGfE* _M0L2giS5137;
      float _M0L6_2atmpS5136;
      float _M0L6_2atmpS5135;
      float _M0L6_2atmpS5134;
      float _M0L6_2atmpS5133;
      float _M0L6_2atmpS5131;
      int32_t _M0L6_2atmpS5139;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5123 = _M0MPC15array5Array2atGfE(_M0L2geS5129, _M0L1iS1609);
      _M0L2geS5128 = _M0L1pS1574->$6;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5127 = _M0MPC15array5Array2atGfE(_M0L2geS5128, _M0L1iS1609);
      _M0L6_2atmpS5126 = -_M0L6_2atmpS5127;
      _M0L6_2atmpS5125 = _M0L6_2atmpS5126 / _M0L6tau__eS1583;
      _M0L6_2atmpS5124 = _M0L2dtS1603 * _M0L6_2atmpS5125;
      _M0L6_2atmpS5122 = _M0L6_2atmpS5123 + _M0L6_2atmpS5124;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS5121, _M0L1iS1609, _M0L6_2atmpS5122);
      _M0L2giS5130 = _M0L1pS1574->$7;
      _M0L2giS5138 = _M0L1pS1574->$7;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5132 = _M0MPC15array5Array2atGfE(_M0L2giS5138, _M0L1iS1609);
      _M0L2giS5137 = _M0L1pS1574->$7;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5136 = _M0MPC15array5Array2atGfE(_M0L2giS5137, _M0L1iS1609);
      _M0L6_2atmpS5135 = -_M0L6_2atmpS5136;
      _M0L6_2atmpS5134 = _M0L6_2atmpS5135 / _M0L6tau__iS1584;
      _M0L6_2atmpS5133 = _M0L2dtS1603 * _M0L6_2atmpS5134;
      _M0L6_2atmpS5131 = _M0L6_2atmpS5132 + _M0L6_2atmpS5133;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS5130, _M0L1iS1609, _M0L6_2atmpS5131);
      _M0L6_2atmpS5139 = _M0L1iS1609 + 1;
      _M0L1iS1609 = _M0L6_2atmpS5139;
      continue;
    }
    break;
  }
  _M0L7_2abindS1611 = 0;
  _M0L1iS1612 = _M0L7_2abindS1611;
  while (1) {
    if (_M0L1iS1612 < _M0L1nS1573) {
      struct _M0TPB5ArrayGbE* _M0L4fireS5140 = _M0L1pS1574->$4;
      struct _M0TPB5ArrayGfE* _M0L1vS5143 = _M0L1pS1574->$2;
      float _M0L6_2atmpS5142;
      int32_t _M0L6_2atmpS5141;
      int32_t _M0L6_2atmpS5144;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5142 = _M0MPC15array5Array2atGfE(_M0L1vS5143, _M0L1iS1612);
      _M0L6_2atmpS5141 = _M0L6_2atmpS5142 > 0x1.4p+4f;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS5140, _M0L1iS1612, _M0L6_2atmpS5141);
      _M0L6_2atmpS5144 = _M0L1iS1612 + 1;
      _M0L1iS1612 = _M0L6_2atmpS5144;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__iz(
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1pS1542,
  float _M0L2dtS1554
) {
  int32_t _M0L1nS1541;
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L3p__S1543;
  float _M0L1aS1544;
  float _M0L1bS1545;
  float _M0L1cS1546;
  float _M0L1dS1547;
  float _M0L6tau__eS1548;
  float _M0L6tau__iS1549;
  float _M0L4e__eS1550;
  float _M0L4e__iS1551;
  int32_t _M0L7_2abindS1552;
  int32_t _M0L1iS1553;
  int32_t _M0L7_2abindS1556;
  int32_t _M0L1iS1557;
  int32_t _M0L7_2abindS1563;
  int32_t _M0L1iS1564;
  int32_t _M0L7_2abindS1567;
  int32_t _M0L1iS1568;
  int32_t _M0L7_2abindS1570;
  int32_t _M0L1iS1571;
  #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1nS1541 = _M0L1pS1542->$1;
  _M0L3p__S1543 = _M0L1pS1542->$0;
  _M0L1aS1544 = _M0L3p__S1543->$0;
  _M0L1bS1545 = _M0L3p__S1543->$1;
  _M0L1cS1546 = _M0L3p__S1543->$2;
  _M0L1dS1547 = _M0L3p__S1543->$3;
  _M0L6tau__eS1548 = _M0L3p__S1543->$4;
  _M0L6tau__iS1549 = _M0L3p__S1543->$5;
  _M0L4e__eS1550 = _M0L3p__S1543->$6;
  _M0L4e__iS1551 = _M0L3p__S1543->$7;
  _M0L7_2abindS1552 = 0;
  _M0L1iS1553 = _M0L7_2abindS1552;
  while (1) {
    if (_M0L1iS1553 < _M0L1nS1541) {
      struct _M0TPB5ArrayGfE* _M0L2geS4970 = _M0L1pS1542->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS4978 = _M0L1pS1542->$6;
      float _M0L6_2atmpS4972;
      struct _M0TPB5ArrayGfE* _M0L2geS4977;
      float _M0L6_2atmpS4976;
      float _M0L6_2atmpS4975;
      float _M0L6_2atmpS4974;
      float _M0L6_2atmpS4973;
      float _M0L6_2atmpS4971;
      struct _M0TPB5ArrayGfE* _M0L2giS4979;
      struct _M0TPB5ArrayGfE* _M0L2giS4987;
      float _M0L6_2atmpS4981;
      struct _M0TPB5ArrayGfE* _M0L2giS4986;
      float _M0L6_2atmpS4985;
      float _M0L6_2atmpS4984;
      float _M0L6_2atmpS4983;
      float _M0L6_2atmpS4982;
      float _M0L6_2atmpS4980;
      int32_t _M0L6_2atmpS4988;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4972 = _M0MPC15array5Array2atGfE(_M0L2geS4978, _M0L1iS1553);
      _M0L2geS4977 = _M0L1pS1542->$6;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4976 = _M0MPC15array5Array2atGfE(_M0L2geS4977, _M0L1iS1553);
      _M0L6_2atmpS4975 = -_M0L6_2atmpS4976;
      _M0L6_2atmpS4974 = _M0L2dtS1554 * _M0L6_2atmpS4975;
      _M0L6_2atmpS4973 = _M0L6_2atmpS4974 / _M0L6tau__eS1548;
      _M0L6_2atmpS4971 = _M0L6_2atmpS4972 + _M0L6_2atmpS4973;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4970, _M0L1iS1553, _M0L6_2atmpS4971);
      _M0L2giS4979 = _M0L1pS1542->$7;
      _M0L2giS4987 = _M0L1pS1542->$7;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4981 = _M0MPC15array5Array2atGfE(_M0L2giS4987, _M0L1iS1553);
      _M0L2giS4986 = _M0L1pS1542->$7;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4985 = _M0MPC15array5Array2atGfE(_M0L2giS4986, _M0L1iS1553);
      _M0L6_2atmpS4984 = -_M0L6_2atmpS4985;
      _M0L6_2atmpS4983 = _M0L2dtS1554 * _M0L6_2atmpS4984;
      _M0L6_2atmpS4982 = _M0L6_2atmpS4983 / _M0L6tau__iS1549;
      _M0L6_2atmpS4980 = _M0L6_2atmpS4981 + _M0L6_2atmpS4982;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4979, _M0L1iS1553, _M0L6_2atmpS4980);
      _M0L6_2atmpS4988 = _M0L1iS1553 + 1;
      _M0L1iS1553 = _M0L6_2atmpS4988;
      continue;
    }
    break;
  }
  _M0L7_2abindS1556 = 0;
  _M0L1iS1557 = _M0L7_2abindS1556;
  while (1) {
    if (_M0L1iS1557 < _M0L1nS1541) {
      struct _M0TPB5ArrayGfE* _M0L1vS5014 = _M0L1pS1542->$2;
      float _M0L1vS1558;
      struct _M0TPB5ArrayGfE* _M0L1uS5013;
      float _M0L1uS1559;
      struct _M0TPB5ArrayGfE* _M0L1iS5012;
      float _M0L2iiS1560;
      struct _M0TPB5ArrayGfE* _M0L1vS4989;
      float _M0L6_2atmpS4992;
      float _M0L6_2atmpS4999;
      float _M0L6_2atmpS4997;
      float _M0L6_2atmpS4998;
      float _M0L6_2atmpS4996;
      float _M0L6_2atmpS4995;
      float _M0L6_2atmpS4994;
      float _M0L6_2atmpS4993;
      float _M0L6_2atmpS4991;
      float _M0L6_2atmpS4990;
      struct _M0TPB5ArrayGfE* _M0L1vS5011;
      float _M0L2v2S1561;
      struct _M0TPB5ArrayGfE* _M0L1vS5000;
      float _M0L6_2atmpS5003;
      float _M0L6_2atmpS5010;
      float _M0L6_2atmpS5008;
      float _M0L6_2atmpS5009;
      float _M0L6_2atmpS5007;
      float _M0L6_2atmpS5006;
      float _M0L6_2atmpS5005;
      float _M0L6_2atmpS5004;
      float _M0L6_2atmpS5002;
      float _M0L6_2atmpS5001;
      int32_t _M0L6_2atmpS5015;
      #line 359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS1558 = _M0MPC15array5Array2atGfE(_M0L1vS5014, _M0L1iS1557);
      _M0L1uS5013 = _M0L1pS1542->$3;
      #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1uS1559 = _M0MPC15array5Array2atGfE(_M0L1uS5013, _M0L1iS1557);
      _M0L1iS5012 = _M0L1pS1542->$5;
      #line 361 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2iiS1560 = _M0MPC15array5Array2atGfE(_M0L1iS5012, _M0L1iS1557);
      _M0L1vS4989 = _M0L1pS1542->$2;
      _M0L6_2atmpS4992 = 0x1p-1f * _M0L2dtS1554;
      _M0L6_2atmpS4999 = 0x1.47ae147ae147bp-5f * _M0L1vS1558;
      _M0L6_2atmpS4997 = _M0L6_2atmpS4999 * _M0L1vS1558;
      _M0L6_2atmpS4998 = 0x1.4p+2f * _M0L1vS1558;
      _M0L6_2atmpS4996 = _M0L6_2atmpS4997 + _M0L6_2atmpS4998;
      _M0L6_2atmpS4995 = _M0L6_2atmpS4996 + 0x1.18p+7f;
      _M0L6_2atmpS4994 = _M0L6_2atmpS4995 - _M0L1uS1559;
      _M0L6_2atmpS4993 = _M0L6_2atmpS4994 + _M0L2iiS1560;
      _M0L6_2atmpS4991 = _M0L6_2atmpS4992 * _M0L6_2atmpS4993;
      _M0L6_2atmpS4990 = _M0L1vS1558 + _M0L6_2atmpS4991;
      #line 362 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4989, _M0L1iS1557, _M0L6_2atmpS4990);
      _M0L1vS5011 = _M0L1pS1542->$2;
      #line 363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2v2S1561 = _M0MPC15array5Array2atGfE(_M0L1vS5011, _M0L1iS1557);
      _M0L1vS5000 = _M0L1pS1542->$2;
      _M0L6_2atmpS5003 = 0x1p-1f * _M0L2dtS1554;
      _M0L6_2atmpS5010 = 0x1.47ae147ae147bp-5f * _M0L2v2S1561;
      _M0L6_2atmpS5008 = _M0L6_2atmpS5010 * _M0L2v2S1561;
      _M0L6_2atmpS5009 = 0x1.4p+2f * _M0L2v2S1561;
      _M0L6_2atmpS5007 = _M0L6_2atmpS5008 + _M0L6_2atmpS5009;
      _M0L6_2atmpS5006 = _M0L6_2atmpS5007 + 0x1.18p+7f;
      _M0L6_2atmpS5005 = _M0L6_2atmpS5006 - _M0L1uS1559;
      _M0L6_2atmpS5004 = _M0L6_2atmpS5005 + _M0L2iiS1560;
      _M0L6_2atmpS5002 = _M0L6_2atmpS5003 * _M0L6_2atmpS5004;
      _M0L6_2atmpS5001 = _M0L2v2S1561 + _M0L6_2atmpS5002;
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5000, _M0L1iS1557, _M0L6_2atmpS5001);
      _M0L6_2atmpS5015 = _M0L1iS1557 + 1;
      _M0L1iS1557 = _M0L6_2atmpS5015;
      continue;
    }
    break;
  }
  _M0L7_2abindS1563 = 0;
  _M0L1iS1564 = _M0L7_2abindS1563;
  while (1) {
    if (_M0L1iS1564 < _M0L1nS1541) {
      struct _M0TPB5ArrayGfE* _M0L1vS5026 = _M0L1pS1542->$2;
      float _M0L1vS1565;
      struct _M0TPB5ArrayGfE* _M0L1uS5016;
      struct _M0TPB5ArrayGfE* _M0L1uS5025;
      float _M0L6_2atmpS5018;
      float _M0L6_2atmpS5020;
      float _M0L6_2atmpS5022;
      struct _M0TPB5ArrayGfE* _M0L1uS5024;
      float _M0L6_2atmpS5023;
      float _M0L6_2atmpS5021;
      float _M0L6_2atmpS5019;
      float _M0L6_2atmpS5017;
      int32_t _M0L6_2atmpS5027;
      #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS1565 = _M0MPC15array5Array2atGfE(_M0L1vS5026, _M0L1iS1564);
      _M0L1uS5016 = _M0L1pS1542->$3;
      _M0L1uS5025 = _M0L1pS1542->$3;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5018 = _M0MPC15array5Array2atGfE(_M0L1uS5025, _M0L1iS1564);
      _M0L6_2atmpS5020 = _M0L2dtS1554 * _M0L1aS1544;
      _M0L6_2atmpS5022 = _M0L1bS1545 * _M0L1vS1565;
      _M0L1uS5024 = _M0L1pS1542->$3;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5023 = _M0MPC15array5Array2atGfE(_M0L1uS5024, _M0L1iS1564);
      _M0L6_2atmpS5021 = _M0L6_2atmpS5022 - _M0L6_2atmpS5023;
      _M0L6_2atmpS5019 = _M0L6_2atmpS5020 * _M0L6_2atmpS5021;
      _M0L6_2atmpS5017 = _M0L6_2atmpS5018 + _M0L6_2atmpS5019;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS5016, _M0L1iS1564, _M0L6_2atmpS5017);
      _M0L6_2atmpS5027 = _M0L1iS1564 + 1;
      _M0L1iS1564 = _M0L6_2atmpS5027;
      continue;
    }
    break;
  }
  _M0L7_2abindS1567 = 0;
  _M0L1iS1568 = _M0L7_2abindS1567;
  while (1) {
    if (_M0L1iS1568 < _M0L1nS1541) {
      struct _M0TPB5ArrayGfE* _M0L1vS5028 = _M0L1pS1542->$2;
      struct _M0TPB5ArrayGfE* _M0L1vS5045 = _M0L1pS1542->$2;
      float _M0L6_2atmpS5030;
      struct _M0TPB5ArrayGfE* _M0L2geS5044;
      float _M0L6_2atmpS5040;
      struct _M0TPB5ArrayGfE* _M0L1vS5043;
      float _M0L6_2atmpS5042;
      float _M0L6_2atmpS5041;
      float _M0L6_2atmpS5033;
      struct _M0TPB5ArrayGfE* _M0L2giS5039;
      float _M0L6_2atmpS5035;
      struct _M0TPB5ArrayGfE* _M0L1vS5038;
      float _M0L6_2atmpS5037;
      float _M0L6_2atmpS5036;
      float _M0L6_2atmpS5034;
      float _M0L6_2atmpS5032;
      float _M0L6_2atmpS5031;
      float _M0L6_2atmpS5029;
      int32_t _M0L6_2atmpS5046;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5030 = _M0MPC15array5Array2atGfE(_M0L1vS5045, _M0L1iS1568);
      _M0L2geS5044 = _M0L1pS1542->$6;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5040 = _M0MPC15array5Array2atGfE(_M0L2geS5044, _M0L1iS1568);
      _M0L1vS5043 = _M0L1pS1542->$2;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5042 = _M0MPC15array5Array2atGfE(_M0L1vS5043, _M0L1iS1568);
      _M0L6_2atmpS5041 = _M0L4e__eS1550 - _M0L6_2atmpS5042;
      _M0L6_2atmpS5033 = _M0L6_2atmpS5040 * _M0L6_2atmpS5041;
      _M0L2giS5039 = _M0L1pS1542->$7;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5035 = _M0MPC15array5Array2atGfE(_M0L2giS5039, _M0L1iS1568);
      _M0L1vS5038 = _M0L1pS1542->$2;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5037 = _M0MPC15array5Array2atGfE(_M0L1vS5038, _M0L1iS1568);
      _M0L6_2atmpS5036 = _M0L4e__iS1551 - _M0L6_2atmpS5037;
      _M0L6_2atmpS5034 = _M0L6_2atmpS5035 * _M0L6_2atmpS5036;
      _M0L6_2atmpS5032 = _M0L6_2atmpS5033 + _M0L6_2atmpS5034;
      _M0L6_2atmpS5031 = _M0L2dtS1554 * _M0L6_2atmpS5032;
      _M0L6_2atmpS5029 = _M0L6_2atmpS5030 + _M0L6_2atmpS5031;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5028, _M0L1iS1568, _M0L6_2atmpS5029);
      _M0L6_2atmpS5046 = _M0L1iS1568 + 1;
      _M0L1iS1568 = _M0L6_2atmpS5046;
      continue;
    }
    break;
  }
  _M0L7_2abindS1570 = 0;
  _M0L1iS1571 = _M0L7_2abindS1570;
  while (1) {
    if (_M0L1iS1571 < _M0L1nS1541) {
      struct _M0TPB5ArrayGbE* _M0L4fireS5047 = _M0L1pS1542->$4;
      struct _M0TPB5ArrayGfE* _M0L1vS5050 = _M0L1pS1542->$2;
      float _M0L6_2atmpS5049;
      int32_t _M0L6_2atmpS5048;
      struct _M0TPB5ArrayGfE* _M0L1vS5051;
      struct _M0TPB5ArrayGbE* _M0L4fireS5053;
      float _M0L6_2atmpS5052;
      struct _M0TPB5ArrayGfE* _M0L1uS5055;
      struct _M0TPB5ArrayGfE* _M0L1uS5060;
      float _M0L6_2atmpS5057;
      struct _M0TPB5ArrayGbE* _M0L4fireS5059;
      float _M0L6_2atmpS5058;
      float _M0L6_2atmpS5056;
      int32_t _M0L6_2atmpS5061;
      #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5049 = _M0MPC15array5Array2atGfE(_M0L1vS5050, _M0L1iS1571);
      _M0L6_2atmpS5048 = _M0L6_2atmpS5049 > 0x1.ep+4f;
      #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS5047, _M0L1iS1571, _M0L6_2atmpS5048);
      _M0L1vS5051 = _M0L1pS1542->$2;
      _M0L4fireS5053 = _M0L1pS1542->$4;
      #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS5053, _M0L1iS1571)) {
        _M0L6_2atmpS5052 = _M0L1cS1546;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS5054 = _M0L1pS1542->$2;
        #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS5052
        = _M0MPC15array5Array2atGfE(_M0L1vS5054, _M0L1iS1571);
      }
      #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5051, _M0L1iS1571, _M0L6_2atmpS5052);
      _M0L1uS5055 = _M0L1pS1542->$3;
      _M0L1uS5060 = _M0L1pS1542->$3;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS5057 = _M0MPC15array5Array2atGfE(_M0L1uS5060, _M0L1iS1571);
      _M0L4fireS5059 = _M0L1pS1542->$4;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS5059, _M0L1iS1571)) {
        _M0L6_2atmpS5058 = _M0L1dS1547;
      } else {
        _M0L6_2atmpS5058 = 0x0p+0f;
      }
      _M0L6_2atmpS5056 = _M0L6_2atmpS5057 + _M0L6_2atmpS5058;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS5055, _M0L1iS1571, _M0L6_2atmpS5056);
      _M0L6_2atmpS5061 = _M0L1iS1571 + 1;
      _M0L1iS1571 = _M0L6_2atmpS5061;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__hh(
  struct _M0TP26RiantR8snn__mbt2HH* _M0L1pS1498,
  float _M0L2dtS1524
) {
  int32_t _M0L1nS1497;
  struct _M0TP26RiantR8snn__mbt11HHParameter* _M0L3p__S1499;
  float _M0L2cmS1500;
  float _M0L2glS1501;
  float _M0L2elS1502;
  float _M0L2ekS1503;
  float _M0L2enS1504;
  float _M0L2gnS1505;
  float _M0L2gkS1506;
  float _M0L2vtS1507;
  float _M0L6tau__eS1508;
  float _M0L6tau__iS1509;
  float _M0L4e__eS1510;
  float _M0L4e__iS1511;
  int32_t _M0L7_2abindS1512;
  int32_t _M0L1iS1513;
  int32_t _M0L7_2abindS1538;
  int32_t _M0L1iS1539;
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L1nS1497 = _M0L1pS1498->$1;
  _M0L3p__S1499 = _M0L1pS1498->$0;
  _M0L2cmS1500 = _M0L3p__S1499->$0;
  _M0L2glS1501 = _M0L3p__S1499->$1;
  _M0L2elS1502 = _M0L3p__S1499->$2;
  _M0L2ekS1503 = _M0L3p__S1499->$3;
  _M0L2enS1504 = _M0L3p__S1499->$4;
  _M0L2gnS1505 = _M0L3p__S1499->$5;
  _M0L2gkS1506 = _M0L3p__S1499->$6;
  _M0L2vtS1507 = _M0L3p__S1499->$7;
  _M0L6tau__eS1508 = _M0L3p__S1499->$8;
  _M0L6tau__iS1509 = _M0L3p__S1499->$9;
  _M0L4e__eS1510 = _M0L3p__S1499->$10;
  _M0L4e__iS1511 = _M0L3p__S1499->$11;
  _M0L7_2abindS1512 = 0;
  _M0L1iS1513 = _M0L7_2abindS1512;
  while (1) {
    if (_M0L1iS1513 < _M0L1nS1497) {
      struct _M0TPB5ArrayGfE* _M0L1vS4963 = _M0L1pS1498->$2;
      float _M0L1vS1514;
      struct _M0TPB5ArrayGfE* _M0L1mS4962;
      float _M0L1mS1515;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4961;
      float _M0L2nnS1516;
      struct _M0TPB5ArrayGfE* _M0L1hS4960;
      float _M0L1hS1517;
      struct _M0TPB5ArrayGfE* _M0L2geS4959;
      float _M0L2geS1518;
      struct _M0TPB5ArrayGfE* _M0L2giS4958;
      float _M0L2giS1519;
      struct _M0TPB5ArrayGbE* _M0L4fireS4861;
      float _M0L6_2atmpS4957;
      float _M0L7am__numS1520;
      float _M0L6_2atmpS4956;
      float _M0L7bm__numS1521;
      float _M0L6_2atmpS4951;
      float _M0L6_2atmpS4950;
      float _M0L6_2atmpS4949;
      float _M0L2amS1522;
      float _M0L6_2atmpS4944;
      float _M0L6_2atmpS4943;
      float _M0L6_2atmpS4942;
      float _M0L2bmS1523;
      struct _M0TPB5ArrayGfE* _M0L1mS4862;
      float _M0L6_2atmpS4868;
      float _M0L6_2atmpS4866;
      float _M0L6_2atmpS4867;
      float _M0L6_2atmpS4865;
      float _M0L6_2atmpS4864;
      float _M0L6_2atmpS4863;
      float _M0L6_2atmpS4941;
      float _M0L7an__numS1525;
      float _M0L6_2atmpS4936;
      float _M0L6_2atmpS4935;
      float _M0L6_2atmpS4934;
      float _M0L2anS1526;
      float _M0L6_2atmpS4933;
      float _M0L6_2atmpS4932;
      float _M0L6_2atmpS4931;
      float _M0L6_2atmpS4930;
      float _M0L2bnS1527;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4869;
      float _M0L6_2atmpS4875;
      float _M0L6_2atmpS4873;
      float _M0L6_2atmpS4874;
      float _M0L6_2atmpS4872;
      float _M0L6_2atmpS4871;
      float _M0L6_2atmpS4870;
      float _M0L6_2atmpS4929;
      float _M0L6_2atmpS4928;
      float _M0L6_2atmpS4927;
      float _M0L6_2atmpS4926;
      float _M0L2ahS1528;
      float _M0L6_2atmpS4925;
      float _M0L6_2atmpS4924;
      float _M0L6_2atmpS4923;
      float _M0L6_2atmpS4922;
      float _M0L9bh__denomS1529;
      float _M0L2bhS1530;
      struct _M0TPB5ArrayGfE* _M0L1hS4876;
      float _M0L6_2atmpS4882;
      float _M0L6_2atmpS4880;
      float _M0L6_2atmpS4881;
      float _M0L6_2atmpS4879;
      float _M0L6_2atmpS4878;
      float _M0L6_2atmpS4877;
      struct _M0TPB5ArrayGfE* _M0L1mS4921;
      float _M0L6m__newS1531;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4920;
      float _M0L6n__newS1532;
      struct _M0TPB5ArrayGfE* _M0L1hS4919;
      float _M0L6h__newS1533;
      float _M0L6_2atmpS4918;
      float _M0L6_2atmpS4917;
      float _M0L3m3hS1534;
      float _M0L6_2atmpS4916;
      float _M0L6_2atmpS4915;
      float _M0L2n4S1535;
      struct _M0TPB5ArrayGfE* _M0L1iS4914;
      float _M0L6_2atmpS4911;
      float _M0L6_2atmpS4913;
      float _M0L6_2atmpS4912;
      float _M0L6_2atmpS4908;
      float _M0L6_2atmpS4910;
      float _M0L6_2atmpS4909;
      float _M0L6_2atmpS4905;
      float _M0L6_2atmpS4907;
      float _M0L6_2atmpS4906;
      float _M0L6_2atmpS4901;
      float _M0L6_2atmpS4903;
      float _M0L6_2atmpS4904;
      float _M0L6_2atmpS4902;
      float _M0L6_2atmpS4897;
      float _M0L6_2atmpS4899;
      float _M0L6_2atmpS4900;
      float _M0L6_2atmpS4898;
      float _M0L7currentS1536;
      struct _M0TPB5ArrayGfE* _M0L1vS4883;
      float _M0L6_2atmpS4886;
      float _M0L6_2atmpS4885;
      float _M0L6_2atmpS4884;
      struct _M0TPB5ArrayGfE* _M0L2geS4887;
      float _M0L6_2atmpS4891;
      float _M0L6_2atmpS4890;
      float _M0L6_2atmpS4889;
      float _M0L6_2atmpS4888;
      struct _M0TPB5ArrayGfE* _M0L2giS4892;
      float _M0L6_2atmpS4896;
      float _M0L6_2atmpS4895;
      float _M0L6_2atmpS4894;
      float _M0L6_2atmpS4893;
      int32_t _M0L6_2atmpS4964;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1vS1514 = _M0MPC15array5Array2atGfE(_M0L1vS4963, _M0L1iS1513);
      _M0L1mS4962 = _M0L1pS1498->$3;
      #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1mS1515 = _M0MPC15array5Array2atGfE(_M0L1mS4962, _M0L1iS1513);
      _M0L7n__gateS4961 = _M0L1pS1498->$4;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2nnS1516
      = _M0MPC15array5Array2atGfE(_M0L7n__gateS4961, _M0L1iS1513);
      _M0L1hS4960 = _M0L1pS1498->$5;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1hS1517 = _M0MPC15array5Array2atGfE(_M0L1hS4960, _M0L1iS1513);
      _M0L2geS4959 = _M0L1pS1498->$8;
      #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2geS1518 = _M0MPC15array5Array2atGfE(_M0L2geS4959, _M0L1iS1513);
      _M0L2giS4958 = _M0L1pS1498->$9;
      #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2giS1519 = _M0MPC15array5Array2atGfE(_M0L2giS4958, _M0L1iS1513);
      _M0L4fireS4861 = _M0L1pS1498->$6;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4861, _M0L1iS1513, 0);
      _M0L6_2atmpS4957 = 0x1.ap+3f - _M0L1vS1514;
      _M0L7am__numS1520 = _M0L6_2atmpS4957 + _M0L2vtS1507;
      _M0L6_2atmpS4956 = _M0L1vS1514 - _M0L2vtS1507;
      _M0L7bm__numS1521 = _M0L6_2atmpS4956 - 0x1.4p+5f;
      _M0L6_2atmpS4951 = _M0L7am__numS1520 / 0x1p+2f;
      #line 134 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4950 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4951);
      _M0L6_2atmpS4949 = _M0L6_2atmpS4950 - 0x1p+0f;
      if (_M0L6_2atmpS4949 != 0x0p+0f) {
        float _M0L6_2atmpS4952 = 0x1.47ae147ae147bp-2f * _M0L7am__numS1520;
        float _M0L6_2atmpS4955 = _M0L7am__numS1520 / 0x1p+2f;
        float _M0L6_2atmpS4954;
        float _M0L6_2atmpS4953;
        #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS4954 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4955);
        _M0L6_2atmpS4953 = _M0L6_2atmpS4954 - 0x1p+0f;
        _M0L2amS1522 = _M0L6_2atmpS4952 / _M0L6_2atmpS4953;
      } else {
        _M0L2amS1522 = 0x0p+0f;
      }
      _M0L6_2atmpS4944 = _M0L7bm__numS1521 / 0x1.4p+2f;
      #line 139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4943 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4944);
      _M0L6_2atmpS4942 = _M0L6_2atmpS4943 - 0x1p+0f;
      if (_M0L6_2atmpS4942 != 0x0p+0f) {
        float _M0L6_2atmpS4945 = 0x1.1eb851eb851ecp-2f * _M0L7bm__numS1521;
        float _M0L6_2atmpS4948 = _M0L7bm__numS1521 / 0x1.4p+2f;
        float _M0L6_2atmpS4947;
        float _M0L6_2atmpS4946;
        #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS4947 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4948);
        _M0L6_2atmpS4946 = _M0L6_2atmpS4947 - 0x1p+0f;
        _M0L2bmS1523 = _M0L6_2atmpS4945 / _M0L6_2atmpS4946;
      } else {
        _M0L2bmS1523 = 0x0p+0f;
      }
      _M0L1mS4862 = _M0L1pS1498->$3;
      _M0L6_2atmpS4868 = 0x1p+0f - _M0L1mS1515;
      _M0L6_2atmpS4866 = _M0L2amS1522 * _M0L6_2atmpS4868;
      _M0L6_2atmpS4867 = _M0L2bmS1523 * _M0L1mS1515;
      _M0L6_2atmpS4865 = _M0L6_2atmpS4866 - _M0L6_2atmpS4867;
      _M0L6_2atmpS4864 = _M0L2dtS1524 * _M0L6_2atmpS4865;
      _M0L6_2atmpS4863 = _M0L1mS1515 + _M0L6_2atmpS4864;
      #line 144 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1mS4862, _M0L1iS1513, _M0L6_2atmpS4863);
      _M0L6_2atmpS4941 = 0x1.ep+3f - _M0L1vS1514;
      _M0L7an__numS1525 = _M0L6_2atmpS4941 + _M0L2vtS1507;
      _M0L6_2atmpS4936 = _M0L7an__numS1525 / 0x1.4p+2f;
      #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4935 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4936);
      _M0L6_2atmpS4934 = _M0L6_2atmpS4935 - 0x1p+0f;
      if (_M0L6_2atmpS4934 != 0x0p+0f) {
        float _M0L6_2atmpS4937 = 0x1.0624dd2f1a9fcp-5f * _M0L7an__numS1525;
        float _M0L6_2atmpS4940 = _M0L7an__numS1525 / 0x1.4p+2f;
        float _M0L6_2atmpS4939;
        float _M0L6_2atmpS4938;
        #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS4939 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4940);
        _M0L6_2atmpS4938 = _M0L6_2atmpS4939 - 0x1p+0f;
        _M0L2anS1526 = _M0L6_2atmpS4937 / _M0L6_2atmpS4938;
      } else {
        _M0L2anS1526 = 0x0p+0f;
      }
      _M0L6_2atmpS4933 = 0x1.4p+3f - _M0L1vS1514;
      _M0L6_2atmpS4932 = _M0L6_2atmpS4933 + _M0L2vtS1507;
      _M0L6_2atmpS4931 = _M0L6_2atmpS4932 / 0x1.4p+5f;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4930 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4931);
      _M0L2bnS1527 = 0x1p-1f * _M0L6_2atmpS4930;
      _M0L7n__gateS4869 = _M0L1pS1498->$4;
      _M0L6_2atmpS4875 = 0x1p+0f - _M0L2nnS1516;
      _M0L6_2atmpS4873 = _M0L2anS1526 * _M0L6_2atmpS4875;
      _M0L6_2atmpS4874 = _M0L2bnS1527 * _M0L2nnS1516;
      _M0L6_2atmpS4872 = _M0L6_2atmpS4873 - _M0L6_2atmpS4874;
      _M0L6_2atmpS4871 = _M0L2dtS1524 * _M0L6_2atmpS4872;
      _M0L6_2atmpS4870 = _M0L2nnS1516 + _M0L6_2atmpS4871;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L7n__gateS4869, _M0L1iS1513, _M0L6_2atmpS4870);
      _M0L6_2atmpS4929 = 0x1.1p+4f - _M0L1vS1514;
      _M0L6_2atmpS4928 = _M0L6_2atmpS4929 + _M0L2vtS1507;
      _M0L6_2atmpS4927 = _M0L6_2atmpS4928 / 0x1.2p+4f;
      #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4926 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4927);
      _M0L2ahS1528 = 0x1.0624dd2f1a9fcp-3f * _M0L6_2atmpS4926;
      _M0L6_2atmpS4925 = 0x1.4p+5f - _M0L1vS1514;
      _M0L6_2atmpS4924 = _M0L6_2atmpS4925 + _M0L2vtS1507;
      _M0L6_2atmpS4923 = _M0L6_2atmpS4924 / 0x1.4p+2f;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4922 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4923);
      _M0L9bh__denomS1529 = 0x1p+0f + _M0L6_2atmpS4922;
      if (_M0L9bh__denomS1529 != 0x0p+0f) {
        _M0L2bhS1530 = 0x1p+2f / _M0L9bh__denomS1529;
      } else {
        _M0L2bhS1530 = 0x0p+0f;
      }
      _M0L1hS4876 = _M0L1pS1498->$5;
      _M0L6_2atmpS4882 = 0x1p+0f - _M0L1hS1517;
      _M0L6_2atmpS4880 = _M0L2ahS1528 * _M0L6_2atmpS4882;
      _M0L6_2atmpS4881 = _M0L2bhS1530 * _M0L1hS1517;
      _M0L6_2atmpS4879 = _M0L6_2atmpS4880 - _M0L6_2atmpS4881;
      _M0L6_2atmpS4878 = _M0L2dtS1524 * _M0L6_2atmpS4879;
      _M0L6_2atmpS4877 = _M0L1hS1517 + _M0L6_2atmpS4878;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1hS4876, _M0L1iS1513, _M0L6_2atmpS4877);
      _M0L1mS4921 = _M0L1pS1498->$3;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6m__newS1531 = _M0MPC15array5Array2atGfE(_M0L1mS4921, _M0L1iS1513);
      _M0L7n__gateS4920 = _M0L1pS1498->$4;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6n__newS1532
      = _M0MPC15array5Array2atGfE(_M0L7n__gateS4920, _M0L1iS1513);
      _M0L1hS4919 = _M0L1pS1498->$5;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6h__newS1533 = _M0MPC15array5Array2atGfE(_M0L1hS4919, _M0L1iS1513);
      _M0L6_2atmpS4918 = _M0L6m__newS1531 * _M0L6m__newS1531;
      _M0L6_2atmpS4917 = _M0L6_2atmpS4918 * _M0L6m__newS1531;
      _M0L3m3hS1534 = _M0L6_2atmpS4917 * _M0L6h__newS1533;
      _M0L6_2atmpS4916 = _M0L6n__newS1532 * _M0L6n__newS1532;
      _M0L6_2atmpS4915 = _M0L6_2atmpS4916 * _M0L6n__newS1532;
      _M0L2n4S1535 = _M0L6_2atmpS4915 * _M0L6n__newS1532;
      _M0L1iS4914 = _M0L1pS1498->$7;
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4911 = _M0MPC15array5Array2atGfE(_M0L1iS4914, _M0L1iS1513);
      _M0L6_2atmpS4913 = _M0L2elS1502 - _M0L1vS1514;
      _M0L6_2atmpS4912 = _M0L2glS1501 * _M0L6_2atmpS4913;
      _M0L6_2atmpS4908 = _M0L6_2atmpS4911 + _M0L6_2atmpS4912;
      _M0L6_2atmpS4910 = _M0L4e__eS1510 - _M0L1vS1514;
      _M0L6_2atmpS4909 = _M0L2geS1518 * _M0L6_2atmpS4910;
      _M0L6_2atmpS4905 = _M0L6_2atmpS4908 + _M0L6_2atmpS4909;
      _M0L6_2atmpS4907 = _M0L4e__iS1511 - _M0L1vS1514;
      _M0L6_2atmpS4906 = _M0L2giS1519 * _M0L6_2atmpS4907;
      _M0L6_2atmpS4901 = _M0L6_2atmpS4905 + _M0L6_2atmpS4906;
      _M0L6_2atmpS4903 = _M0L2gnS1505 * _M0L3m3hS1534;
      _M0L6_2atmpS4904 = _M0L2enS1504 - _M0L1vS1514;
      _M0L6_2atmpS4902 = _M0L6_2atmpS4903 * _M0L6_2atmpS4904;
      _M0L6_2atmpS4897 = _M0L6_2atmpS4901 + _M0L6_2atmpS4902;
      _M0L6_2atmpS4899 = _M0L2gkS1506 * _M0L2n4S1535;
      _M0L6_2atmpS4900 = _M0L2ekS1503 - _M0L1vS1514;
      _M0L6_2atmpS4898 = _M0L6_2atmpS4899 * _M0L6_2atmpS4900;
      _M0L7currentS1536 = _M0L6_2atmpS4897 + _M0L6_2atmpS4898;
      _M0L1vS4883 = _M0L1pS1498->$2;
      _M0L6_2atmpS4886 = _M0L2dtS1524 / _M0L2cmS1500;
      _M0L6_2atmpS4885 = _M0L6_2atmpS4886 * _M0L7currentS1536;
      _M0L6_2atmpS4884 = _M0L1vS1514 + _M0L6_2atmpS4885;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4883, _M0L1iS1513, _M0L6_2atmpS4884);
      _M0L2geS4887 = _M0L1pS1498->$8;
      _M0L6_2atmpS4891 = -_M0L2geS1518;
      _M0L6_2atmpS4890 = _M0L6_2atmpS4891 / _M0L6tau__eS1508;
      _M0L6_2atmpS4889 = _M0L2dtS1524 * _M0L6_2atmpS4890;
      _M0L6_2atmpS4888 = _M0L2geS1518 + _M0L6_2atmpS4889;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4887, _M0L1iS1513, _M0L6_2atmpS4888);
      _M0L2giS4892 = _M0L1pS1498->$9;
      _M0L6_2atmpS4896 = -_M0L2giS1519;
      _M0L6_2atmpS4895 = _M0L6_2atmpS4896 / _M0L6tau__iS1509;
      _M0L6_2atmpS4894 = _M0L2dtS1524 * _M0L6_2atmpS4895;
      _M0L6_2atmpS4893 = _M0L2giS1519 + _M0L6_2atmpS4894;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4892, _M0L1iS1513, _M0L6_2atmpS4893);
      _M0L6_2atmpS4964 = _M0L1iS1513 + 1;
      _M0L1iS1513 = _M0L6_2atmpS4964;
      continue;
    }
    break;
  }
  _M0L7_2abindS1538 = 0;
  _M0L1iS1539 = _M0L7_2abindS1538;
  while (1) {
    if (_M0L1iS1539 < _M0L1nS1497) {
      struct _M0TPB5ArrayGbE* _M0L4fireS4965 = _M0L1pS1498->$6;
      struct _M0TPB5ArrayGfE* _M0L1vS4968 = _M0L1pS1498->$2;
      float _M0L6_2atmpS4967;
      int32_t _M0L6_2atmpS4966;
      int32_t _M0L6_2atmpS4969;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4967 = _M0MPC15array5Array2atGfE(_M0L1vS4968, _M0L1iS1539);
      _M0L6_2atmpS4966 = _M0L6_2atmpS4967 > -0x1.4p+4f;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4965, _M0L1iS1539, _M0L6_2atmpS4966);
      _M0L6_2atmpS4969 = _M0L1iS1539 + 1;
      _M0L1iS1539 = _M0L6_2atmpS4969;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__hetrec(
  struct _M0TP26RiantR8snn__mbt6HetRec* _M0L1pS1468,
  float _M0L2dtS1476
) {
  int32_t _M0L1nS1467;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4860;
  int32_t _M0L2ndS1469;
  int32_t _M0L8total__dS1470;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4859;
  float _M0L9steepnessS1471;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4858;
  float _M0L6tau__mS1472;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4857;
  float _M0L9tau__rateS1473;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4856;
  float _M0L8tau__absS1474;
  float _M0L6_2atmpS4855;
  int32_t _M0L11tabs__stepsS1475;
  int32_t _M0L7_2abindS1477;
  int32_t _M0L1iS1478;
  int32_t _M0L7_2abindS1481;
  int32_t _M0L1iS1482;
  int32_t _M0L7_2abindS1491;
  int32_t _M0L1iS1492;
  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L1nS1467 = _M0L1pS1468->$1;
  _M0L5paramS4860 = _M0L1pS1468->$0;
  _M0L2ndS1469 = _M0L5paramS4860->$0;
  _M0L8total__dS1470 = _M0L1nS1467 * _M0L2ndS1469;
  _M0L5paramS4859 = _M0L1pS1468->$0;
  _M0L9steepnessS1471 = _M0L5paramS4859->$7;
  _M0L5paramS4858 = _M0L1pS1468->$0;
  _M0L6tau__mS1472 = _M0L5paramS4858->$8;
  _M0L5paramS4857 = _M0L1pS1468->$0;
  _M0L9tau__rateS1473 = _M0L5paramS4857->$9;
  _M0L5paramS4856 = _M0L1pS1468->$0;
  _M0L8tau__absS1474 = _M0L5paramS4856->$6;
  _M0L6_2atmpS4855 = _M0L8tau__absS1474 / _M0L2dtS1476;
  #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L11tabs__stepsS1475 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4855);
  _M0L7_2abindS1477 = 0;
  _M0L1iS1478 = _M0L7_2abindS1477;
  while (1) {
    if (_M0L1iS1478 < _M0L8total__dS1470) {
      struct _M0TPB5ArrayGfE* _M0L6tau__dS4783 = _M0L1pS1468->$6;
      float _M0L7tau__diS1479;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4771;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4782;
      float _M0L6_2atmpS4773;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4781;
      float _M0L6_2atmpS4780;
      float _M0L6_2atmpS4777;
      struct _M0TPB5ArrayGfE* _M0L4is__S4779;
      float _M0L6_2atmpS4778;
      float _M0L6_2atmpS4776;
      float _M0L6_2atmpS4775;
      float _M0L6_2atmpS4774;
      float _M0L6_2atmpS4772;
      int32_t _M0L6_2atmpS4784;
      #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L7tau__diS1479
      = _M0MPC15array5Array2atGfE(_M0L6tau__dS4783, _M0L1iS1478);
      _M0L4v__dS4771 = _M0L1pS1468->$2;
      _M0L4v__dS4782 = _M0L1pS1468->$2;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4773
      = _M0MPC15array5Array2atGfE(_M0L4v__dS4782, _M0L1iS1478);
      _M0L4v__dS4781 = _M0L1pS1468->$2;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4780
      = _M0MPC15array5Array2atGfE(_M0L4v__dS4781, _M0L1iS1478);
      _M0L6_2atmpS4777 = -_M0L6_2atmpS4780;
      _M0L4is__S4779 = _M0L1pS1468->$4;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4778
      = _M0MPC15array5Array2atGfE(_M0L4is__S4779, _M0L1iS1478);
      _M0L6_2atmpS4776 = _M0L6_2atmpS4777 - _M0L6_2atmpS4778;
      _M0L6_2atmpS4775 = _M0L2dtS1476 * _M0L6_2atmpS4776;
      _M0L6_2atmpS4774 = _M0L6_2atmpS4775 / _M0L7tau__diS1479;
      _M0L6_2atmpS4772 = _M0L6_2atmpS4773 + _M0L6_2atmpS4774;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__dS4771, _M0L1iS1478, _M0L6_2atmpS4772);
      _M0L6_2atmpS4784 = _M0L1iS1478 + 1;
      _M0L1iS1478 = _M0L6_2atmpS4784;
      continue;
    }
    break;
  }
  _M0L7_2abindS1481 = 0;
  _M0L1iS1482 = _M0L7_2abindS1481;
  while (1) {
    if (_M0L1iS1482 < _M0L1nS1467) {
      struct _M0TPB5ArrayGiE* _M0L6colptrS4805 = _M0L1pS1468->$11;
      int32_t _M0L5startS1483;
      struct _M0TPB5ArrayGiE* _M0L6colptrS4803;
      int32_t _M0L6_2atmpS4804;
      int32_t _M0L3endS1484;
      float _M0L16dt__over__tau__mS1485;
      struct _M0TPB8MutLocalGiE* _M0L1sS1486;
      int32_t _M0L6_2atmpS4806;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L5startS1483
      = _M0MPC15array5Array2atGiE(_M0L6colptrS4805, _M0L1iS1482);
      _M0L6colptrS4803 = _M0L1pS1468->$11;
      _M0L6_2atmpS4804 = _M0L1iS1482 + 1;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L3endS1484
      = _M0MPC15array5Array2atGiE(_M0L6colptrS4803, _M0L6_2atmpS4804);
      _M0L16dt__over__tau__mS1485 = _M0L2dtS1476 / _M0L6tau__mS1472;
      _M0L1sS1486
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1486)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1486->$0 = _M0L5startS1483;
      while (1) {
        int32_t _M0L3valS4785 = _M0L1sS1486->$0;
        if (_M0L3valS4785 < _M0L3endS1484) {
          struct _M0TPB5ArrayGiE* _M0L6i__synS4801 = _M0L1pS1468->$12;
          int32_t _M0L3valS4802 = _M0L1sS1486->$0;
          int32_t _M0L9dend__idxS1487;
          struct _M0TPB5ArrayGfE* _M0L6w__synS4799;
          int32_t _M0L3valS4800;
          float _M0L1wS1488;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4786;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4796;
          float _M0L6_2atmpS4788;
          struct _M0TPB5ArrayGfE* _M0L4v__dS4795;
          float _M0L6_2atmpS4794;
          float _M0L6_2atmpS4791;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4793;
          float _M0L6_2atmpS4792;
          float _M0L6_2atmpS4790;
          float _M0L6_2atmpS4789;
          float _M0L6_2atmpS4787;
          int32_t _M0L3valS4798;
          int32_t _M0L6_2atmpS4797;
          #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L9dend__idxS1487
          = _M0MPC15array5Array2atGiE(_M0L6i__synS4801, _M0L3valS4802);
          _M0L6w__synS4799 = _M0L1pS1468->$13;
          _M0L3valS4800 = _M0L1sS1486->$0;
          #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L1wS1488
          = _M0MPC15array5Array2atGfE(_M0L6w__synS4799, _M0L3valS4800);
          _M0L4v__sS4786 = _M0L1pS1468->$3;
          _M0L4v__sS4796 = _M0L1pS1468->$3;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4788
          = _M0MPC15array5Array2atGfE(_M0L4v__sS4796, _M0L1iS1482);
          _M0L4v__dS4795 = _M0L1pS1468->$2;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4794
          = _M0MPC15array5Array2atGfE(_M0L4v__dS4795, _M0L9dend__idxS1487);
          _M0L6_2atmpS4791 = _M0L1wS1488 * _M0L6_2atmpS4794;
          _M0L4v__sS4793 = _M0L1pS1468->$3;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4792
          = _M0MPC15array5Array2atGfE(_M0L4v__sS4793, _M0L1iS1482);
          _M0L6_2atmpS4790 = _M0L6_2atmpS4791 - _M0L6_2atmpS4792;
          _M0L6_2atmpS4789 = _M0L6_2atmpS4790 * _M0L16dt__over__tau__mS1485;
          _M0L6_2atmpS4787 = _M0L6_2atmpS4788 + _M0L6_2atmpS4789;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0MPC15array5Array3setGfE(_M0L4v__sS4786, _M0L1iS1482, _M0L6_2atmpS4787);
          _M0L3valS4798 = _M0L1sS1486->$0;
          _M0L6_2atmpS4797 = _M0L3valS4798 + 1;
          _M0L1sS1486->$0 = _M0L6_2atmpS4797;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1486);
        }
        break;
      }
      _M0L6_2atmpS4806 = _M0L1iS1482 + 1;
      _M0L1iS1482 = _M0L6_2atmpS4806;
      continue;
    }
    break;
  }
  _M0L7_2abindS1491 = 0;
  _M0L1iS1492 = _M0L7_2abindS1491;
  while (1) {
    if (_M0L1iS1492 < _M0L1nS1467) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS4808 = _M0L1pS1468->$8;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4811 = _M0L1pS1468->$8;
      int32_t _M0L6_2atmpS4810;
      int32_t _M0L6_2atmpS4809;
      struct _M0TPB5ArrayGbE* _M0L4fireS4812;
      struct _M0TPB5ArrayGfE* _M0L5traceS4813;
      struct _M0TPB5ArrayGfE* _M0L5traceS4821;
      float _M0L6_2atmpS4815;
      struct _M0TPB5ArrayGfE* _M0L5traceS4820;
      float _M0L6_2atmpS4819;
      float _M0L6_2atmpS4818;
      float _M0L6_2atmpS4817;
      float _M0L6_2atmpS4816;
      float _M0L6_2atmpS4814;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4823;
      int32_t _M0L6_2atmpS4822;
      struct _M0TPB5ArrayGfE* _M0L5traceS4824;
      struct _M0TPB5ArrayGfE* _M0L5traceS4833;
      float _M0L6_2atmpS4826;
      struct _M0TPB5ArrayGfE* _M0L4v__sS4832;
      float _M0L6_2atmpS4829;
      struct _M0TPB5ArrayGfE* _M0L5traceS4831;
      float _M0L6_2atmpS4830;
      float _M0L6_2atmpS4828;
      float _M0L6_2atmpS4827;
      float _M0L6_2atmpS4825;
      float _M0L6_2atmpS4849;
      struct _M0TPB5ArrayGfE* _M0L4v__sS4854;
      float _M0L6_2atmpS4851;
      struct _M0TPB5ArrayGfE* _M0L5traceS4853;
      float _M0L6_2atmpS4852;
      float _M0L6_2atmpS4850;
      float _M0L12sigmoid__argS1495;
      float _M0L4rateS1496;
      struct _M0TPB5ArrayGfE* _M0L9randcacheS4835;
      float _M0L6_2atmpS4834;
      int32_t _M0L6_2atmpS4807;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4810
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4811, _M0L1iS1492);
      _M0L6_2atmpS4809 = _M0L6_2atmpS4810 - 1;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4808, _M0L1iS1492, _M0L6_2atmpS4809);
      _M0L4fireS4812 = _M0L1pS1468->$7;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4812, _M0L1iS1492, 0);
      _M0L5traceS4813 = _M0L1pS1468->$9;
      _M0L5traceS4821 = _M0L1pS1468->$9;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4815
      = _M0MPC15array5Array2atGfE(_M0L5traceS4821, _M0L1iS1492);
      _M0L5traceS4820 = _M0L1pS1468->$9;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4819
      = _M0MPC15array5Array2atGfE(_M0L5traceS4820, _M0L1iS1492);
      _M0L6_2atmpS4818 = -_M0L6_2atmpS4819;
      _M0L6_2atmpS4817 = _M0L6_2atmpS4818 / _M0L9tau__rateS1473;
      _M0L6_2atmpS4816 = _M0L2dtS1476 * _M0L6_2atmpS4817;
      _M0L6_2atmpS4814 = _M0L6_2atmpS4815 + _M0L6_2atmpS4816;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L5traceS4813, _M0L1iS1492, _M0L6_2atmpS4814);
      _M0L4tabsS4823 = _M0L1pS1468->$8;
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4822
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4823, _M0L1iS1492);
      if (_M0L6_2atmpS4822 > 0) {
        goto join_1493;
      }
      _M0L5traceS4824 = _M0L1pS1468->$9;
      _M0L5traceS4833 = _M0L1pS1468->$9;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4826
      = _M0MPC15array5Array2atGfE(_M0L5traceS4833, _M0L1iS1492);
      _M0L4v__sS4832 = _M0L1pS1468->$3;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4829
      = _M0MPC15array5Array2atGfE(_M0L4v__sS4832, _M0L1iS1492);
      _M0L5traceS4831 = _M0L1pS1468->$9;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4830
      = _M0MPC15array5Array2atGfE(_M0L5traceS4831, _M0L1iS1492);
      _M0L6_2atmpS4828 = _M0L6_2atmpS4829 - _M0L6_2atmpS4830;
      _M0L6_2atmpS4827 = _M0L6_2atmpS4828 / _M0L9tau__rateS1473;
      _M0L6_2atmpS4825 = _M0L6_2atmpS4826 + _M0L6_2atmpS4827;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L5traceS4824, _M0L1iS1492, _M0L6_2atmpS4825);
      _M0L6_2atmpS4849 = -_M0L9steepnessS1471;
      _M0L4v__sS4854 = _M0L1pS1468->$3;
      #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4851
      = _M0MPC15array5Array2atGfE(_M0L4v__sS4854, _M0L1iS1492);
      _M0L5traceS4853 = _M0L1pS1468->$9;
      #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4852
      = _M0MPC15array5Array2atGfE(_M0L5traceS4853, _M0L1iS1492);
      _M0L6_2atmpS4850 = _M0L6_2atmpS4851 - _M0L6_2atmpS4852;
      _M0L12sigmoid__argS1495 = _M0L6_2atmpS4849 * _M0L6_2atmpS4850;
      if (_M0L12sigmoid__argS1495 > 0x1.6p+6f) {
        struct _M0TPB5ArrayGfE* _M0L1rS4843 = _M0L1pS1468->$5;
        float _M0L6_2atmpS4842;
        #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4842
        = _M0MPC15array5Array2atGfE(_M0L1rS4843, _M0L1iS1492);
        _M0L4rateS1496 = _M0L6_2atmpS4842 * _M0L2dtS1476;
      } else if (_M0L12sigmoid__argS1495 < -0x1.6p+6f) {
        _M0L4rateS1496 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1rS4848 = _M0L1pS1468->$5;
        float _M0L6_2atmpS4847;
        float _M0L6_2atmpS4844;
        float _M0L6_2atmpS4846;
        float _M0L6_2atmpS4845;
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4847
        = _M0MPC15array5Array2atGfE(_M0L1rS4848, _M0L1iS1492);
        _M0L6_2atmpS4844 = _M0L6_2atmpS4847 * _M0L2dtS1476;
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4846
        = _M0FP26RiantR8snn__mbt4expf(_M0L12sigmoid__argS1495);
        _M0L6_2atmpS4845 = 0x1p+0f + _M0L6_2atmpS4846;
        _M0L4rateS1496 = _M0L6_2atmpS4844 / _M0L6_2atmpS4845;
      }
      _M0L9randcacheS4835 = _M0L1pS1468->$10;
      #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4834
      = _M0MPC15array5Array2atGfE(_M0L9randcacheS4835, _M0L1iS1492);
      if (_M0L6_2atmpS4834 < _M0L4rateS1496) {
        struct _M0TPB5ArrayGbE* _M0L4fireS4836 = _M0L1pS1468->$7;
        struct _M0TPB5ArrayGiE* _M0L4tabsS4837;
        struct _M0TPB5ArrayGfE* _M0L5traceS4838;
        struct _M0TPB5ArrayGfE* _M0L5traceS4841;
        float _M0L6_2atmpS4840;
        float _M0L6_2atmpS4839;
        #line 267 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS4836, _M0L1iS1492, 1);
        _M0L4tabsS4837 = _M0L1pS1468->$8;
        #line 268 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS4837, _M0L1iS1492, _M0L11tabs__stepsS1475);
        _M0L5traceS4838 = _M0L1pS1468->$9;
        _M0L5traceS4841 = _M0L1pS1468->$9;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4840
        = _M0MPC15array5Array2atGfE(_M0L5traceS4841, _M0L1iS1492);
        _M0L6_2atmpS4839 = _M0L6_2atmpS4840 + 0x1p+0f;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGfE(_M0L5traceS4838, _M0L1iS1492, _M0L6_2atmpS4839);
      }
      goto join_1493;
      goto joinlet_5911;
      join_1493:;
      _M0L6_2atmpS4807 = _M0L1iS1492 + 1;
      _M0L1iS1492 = _M0L6_2atmpS4807;
      continue;
      joinlet_5911:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt18step__adex__sinexp(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1446,
  float _M0L2dtS1461
) {
  int32_t _M0L1nS1445;
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0L3p__S1447;
  float _M0L2tmS1448;
  float _M0L2vtS1449;
  float _M0L2vrS1450;
  float _M0L2elS1451;
  float _M0L1rS1452;
  float _M0L9dt__slopeS1453;
  float _M0L2twS1454;
  float _M0L1aS1455;
  float _M0L1bS1456;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4770;
  float _M0L2atS1457;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4769;
  float _M0L6tau__aS1458;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4768;
  float _M0L11tabs__constS1459;
  float _M0L6_2atmpS4767;
  int32_t _M0L11tabs__stepsS1460;
  int32_t _M0L7_2abindS1462;
  int32_t _M0L1iS1463;
  #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1445 = _M0L1pS1446->$2;
  _M0L3p__S1447 = _M0L1pS1446->$0;
  _M0L2tmS1448 = _M0L3p__S1447->$5;
  _M0L2vtS1449 = _M0L3p__S1447->$2;
  _M0L2vrS1450 = _M0L3p__S1447->$3;
  _M0L2elS1451 = _M0L3p__S1447->$4;
  _M0L1rS1452 = _M0L3p__S1447->$6;
  _M0L9dt__slopeS1453 = _M0L3p__S1447->$7;
  _M0L2twS1454 = _M0L3p__S1447->$8;
  _M0L1aS1455 = _M0L3p__S1447->$9;
  _M0L1bS1456 = _M0L3p__S1447->$10;
  _M0L5spikeS4770 = _M0L1pS1446->$1;
  _M0L2atS1457 = _M0L5spikeS4770->$0;
  _M0L5spikeS4769 = _M0L1pS1446->$1;
  _M0L6tau__aS1458 = _M0L5spikeS4769->$1;
  _M0L5spikeS4768 = _M0L1pS1446->$1;
  _M0L11tabs__constS1459 = _M0L5spikeS4768->$3;
  _M0L6_2atmpS4767 = _M0L11tabs__constS1459 / _M0L2dtS1461;
  #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L11tabs__stepsS1460 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4767);
  _M0L7_2abindS1462 = 0;
  _M0L1iS1463 = _M0L7_2abindS1462;
  while (1) {
    if (_M0L1iS1463 < _M0L1nS1445) {
      struct _M0TPB5ArrayGfE* _M0L1vS4680 = _M0L1pS1446->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS4682 = _M0L1pS1446->$5;
      float _M0L6_2atmpS4681;
      struct _M0TPB5ArrayGbE* _M0L4fireS4684;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4685;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4688;
      int32_t _M0L6_2atmpS4687;
      int32_t _M0L6_2atmpS4686;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4690;
      int32_t _M0L6_2atmpS4689;
      struct _M0TPB5ArrayGfE* _M0L1wS4691;
      struct _M0TPB5ArrayGfE* _M0L1wS4703;
      float _M0L6_2atmpS4693;
      struct _M0TPB5ArrayGfE* _M0L1vS4702;
      float _M0L6_2atmpS4701;
      float _M0L6_2atmpS4700;
      float _M0L6_2atmpS4697;
      struct _M0TPB5ArrayGfE* _M0L1wS4699;
      float _M0L6_2atmpS4698;
      float _M0L6_2atmpS4696;
      float _M0L6_2atmpS4695;
      float _M0L6_2atmpS4694;
      float _M0L6_2atmpS4692;
      float _M0L9exp__termS1466;
      struct _M0TPB5ArrayGfE* _M0L1vS4704;
      struct _M0TPB5ArrayGfE* _M0L1vS4726;
      float _M0L6_2atmpS4706;
      struct _M0TPB5ArrayGfE* _M0L1vS4725;
      float _M0L6_2atmpS4724;
      float _M0L6_2atmpS4723;
      float _M0L6_2atmpS4722;
      float _M0L6_2atmpS4718;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4721;
      float _M0L6_2atmpS4720;
      float _M0L6_2atmpS4719;
      float _M0L6_2atmpS4714;
      struct _M0TPB5ArrayGfE* _M0L1wS4717;
      float _M0L6_2atmpS4716;
      float _M0L6_2atmpS4715;
      float _M0L6_2atmpS4710;
      struct _M0TPB5ArrayGfE* _M0L1iS4713;
      float _M0L6_2atmpS4712;
      float _M0L6_2atmpS4711;
      float _M0L6_2atmpS4709;
      float _M0L6_2atmpS4708;
      float _M0L6_2atmpS4707;
      float _M0L6_2atmpS4705;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4727;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4735;
      float _M0L6_2atmpS4729;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4734;
      float _M0L6_2atmpS4733;
      float _M0L6_2atmpS4732;
      float _M0L6_2atmpS4731;
      float _M0L6_2atmpS4730;
      float _M0L6_2atmpS4728;
      struct _M0TPB5ArrayGbE* _M0L4fireS4736;
      struct _M0TPB5ArrayGfE* _M0L1vS4739;
      float _M0L6_2atmpS4738;
      int32_t _M0L6_2atmpS4737;
      struct _M0TPB5ArrayGfE* _M0L1vS4740;
      struct _M0TPB5ArrayGbE* _M0L4fireS4742;
      float _M0L6_2atmpS4741;
      struct _M0TPB5ArrayGfE* _M0L1wS4744;
      struct _M0TPB5ArrayGbE* _M0L4fireS4746;
      float _M0L6_2atmpS4745;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4750;
      struct _M0TPB5ArrayGbE* _M0L4fireS4752;
      float _M0L6_2atmpS4751;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4756;
      struct _M0TPB5ArrayGbE* _M0L4fireS4758;
      int32_t _M0L6_2atmpS4757;
      int32_t _M0L6_2atmpS4679;
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4682, _M0L1iS1463)) {
        _M0L6_2atmpS4681 = _M0L2vrS1450;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4683 = _M0L1pS1446->$3;
        #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4681
        = _M0MPC15array5Array2atGfE(_M0L1vS4683, _M0L1iS1463);
      }
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4680, _M0L1iS1463, _M0L6_2atmpS4681);
      _M0L4fireS4684 = _M0L1pS1446->$5;
      #line 212 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4684, _M0L1iS1463, 0);
      _M0L4tabsS4685 = _M0L1pS1446->$7;
      _M0L4tabsS4688 = _M0L1pS1446->$7;
      #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4687
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4688, _M0L1iS1463);
      _M0L6_2atmpS4686 = _M0L6_2atmpS4687 - 1;
      #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4685, _M0L1iS1463, _M0L6_2atmpS4686);
      _M0L4tabsS4690 = _M0L1pS1446->$7;
      #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4689
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4690, _M0L1iS1463);
      if (_M0L6_2atmpS4689 > 0) {
        goto join_1464;
      }
      _M0L1wS4691 = _M0L1pS1446->$4;
      _M0L1wS4703 = _M0L1pS1446->$4;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4693 = _M0MPC15array5Array2atGfE(_M0L1wS4703, _M0L1iS1463);
      _M0L1vS4702 = _M0L1pS1446->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4701 = _M0MPC15array5Array2atGfE(_M0L1vS4702, _M0L1iS1463);
      _M0L6_2atmpS4700 = _M0L6_2atmpS4701 - _M0L2elS1451;
      _M0L6_2atmpS4697 = _M0L1aS1455 * _M0L6_2atmpS4700;
      _M0L1wS4699 = _M0L1pS1446->$4;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4698 = _M0MPC15array5Array2atGfE(_M0L1wS4699, _M0L1iS1463);
      _M0L6_2atmpS4696 = _M0L6_2atmpS4697 - _M0L6_2atmpS4698;
      _M0L6_2atmpS4695 = _M0L2dtS1461 * _M0L6_2atmpS4696;
      _M0L6_2atmpS4694 = _M0L6_2atmpS4695 / _M0L2twS1454;
      _M0L6_2atmpS4692 = _M0L6_2atmpS4693 + _M0L6_2atmpS4694;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4691, _M0L1iS1463, _M0L6_2atmpS4692);
      if (_M0L9dt__slopeS1453 < 0x0p+0f) {
        _M0L9exp__termS1466 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4766 = _M0L1pS1446->$3;
        float _M0L6_2atmpS4763;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4765;
        float _M0L6_2atmpS4764;
        float _M0L6_2atmpS4762;
        float _M0L6_2atmpS4761;
        float _M0L6_2atmpS4760;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4763
        = _M0MPC15array5Array2atGfE(_M0L1vS4766, _M0L1iS1463);
        _M0L9thresholdS4765 = _M0L1pS1446->$6;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4764
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4765, _M0L1iS1463);
        _M0L6_2atmpS4762 = _M0L6_2atmpS4763 - _M0L6_2atmpS4764;
        _M0L6_2atmpS4761 = _M0L6_2atmpS4762 / _M0L9dt__slopeS1453;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4760 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4761);
        _M0L9exp__termS1466 = _M0L9dt__slopeS1453 * _M0L6_2atmpS4760;
      }
      _M0L1vS4704 = _M0L1pS1446->$3;
      _M0L1vS4726 = _M0L1pS1446->$3;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4706 = _M0MPC15array5Array2atGfE(_M0L1vS4726, _M0L1iS1463);
      _M0L1vS4725 = _M0L1pS1446->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4724 = _M0MPC15array5Array2atGfE(_M0L1vS4725, _M0L1iS1463);
      _M0L6_2atmpS4723 = _M0L6_2atmpS4724 - _M0L2elS1451;
      _M0L6_2atmpS4722 = -_M0L6_2atmpS4723;
      _M0L6_2atmpS4718 = _M0L6_2atmpS4722 + _M0L9exp__termS1466;
      _M0L9syn__currS4721 = _M0L1pS1446->$9;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4720
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS4721, _M0L1iS1463);
      _M0L6_2atmpS4719 = _M0L1rS1452 * _M0L6_2atmpS4720;
      _M0L6_2atmpS4714 = _M0L6_2atmpS4718 - _M0L6_2atmpS4719;
      _M0L1wS4717 = _M0L1pS1446->$4;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4716 = _M0MPC15array5Array2atGfE(_M0L1wS4717, _M0L1iS1463);
      _M0L6_2atmpS4715 = _M0L1rS1452 * _M0L6_2atmpS4716;
      _M0L6_2atmpS4710 = _M0L6_2atmpS4714 - _M0L6_2atmpS4715;
      _M0L1iS4713 = _M0L1pS1446->$8;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4712 = _M0MPC15array5Array2atGfE(_M0L1iS4713, _M0L1iS1463);
      _M0L6_2atmpS4711 = _M0L1rS1452 * _M0L6_2atmpS4712;
      _M0L6_2atmpS4709 = _M0L6_2atmpS4710 + _M0L6_2atmpS4711;
      _M0L6_2atmpS4708 = _M0L2dtS1461 * _M0L6_2atmpS4709;
      _M0L6_2atmpS4707 = _M0L6_2atmpS4708 / _M0L2tmS1448;
      _M0L6_2atmpS4705 = _M0L6_2atmpS4706 + _M0L6_2atmpS4707;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4704, _M0L1iS1463, _M0L6_2atmpS4705);
      _M0L9thresholdS4727 = _M0L1pS1446->$6;
      _M0L9thresholdS4735 = _M0L1pS1446->$6;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4729
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4735, _M0L1iS1463);
      _M0L9thresholdS4734 = _M0L1pS1446->$6;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4733
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4734, _M0L1iS1463);
      _M0L6_2atmpS4732 = _M0L2vtS1449 - _M0L6_2atmpS4733;
      _M0L6_2atmpS4731 = _M0L2dtS1461 * _M0L6_2atmpS4732;
      _M0L6_2atmpS4730 = _M0L6_2atmpS4731 / _M0L6tau__aS1458;
      _M0L6_2atmpS4728 = _M0L6_2atmpS4729 + _M0L6_2atmpS4730;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4727, _M0L1iS1463, _M0L6_2atmpS4728);
      _M0L4fireS4736 = _M0L1pS1446->$5;
      _M0L1vS4739 = _M0L1pS1446->$3;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4738 = _M0MPC15array5Array2atGfE(_M0L1vS4739, _M0L1iS1463);
      _M0L6_2atmpS4737 = _M0L6_2atmpS4738 >= 0x0p+0f;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4736, _M0L1iS1463, _M0L6_2atmpS4737);
      _M0L1vS4740 = _M0L1pS1446->$3;
      _M0L4fireS4742 = _M0L1pS1446->$5;
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4742, _M0L1iS1463)) {
        _M0L6_2atmpS4741 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4743 = _M0L1pS1446->$3;
        #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4741
        = _M0MPC15array5Array2atGfE(_M0L1vS4743, _M0L1iS1463);
      }
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4740, _M0L1iS1463, _M0L6_2atmpS4741);
      _M0L1wS4744 = _M0L1pS1446->$4;
      _M0L4fireS4746 = _M0L1pS1446->$5;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4746, _M0L1iS1463)) {
        struct _M0TPB5ArrayGfE* _M0L1wS4748 = _M0L1pS1446->$4;
        float _M0L6_2atmpS4747;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4747
        = _M0MPC15array5Array2atGfE(_M0L1wS4748, _M0L1iS1463);
        _M0L6_2atmpS4745 = _M0L6_2atmpS4747 + _M0L1bS1456;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS4749 = _M0L1pS1446->$4;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4745
        = _M0MPC15array5Array2atGfE(_M0L1wS4749, _M0L1iS1463);
      }
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4744, _M0L1iS1463, _M0L6_2atmpS4745);
      _M0L9thresholdS4750 = _M0L1pS1446->$6;
      _M0L4fireS4752 = _M0L1pS1446->$5;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4752, _M0L1iS1463)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4754 = _M0L1pS1446->$6;
        float _M0L6_2atmpS4753;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4753
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4754, _M0L1iS1463);
        _M0L6_2atmpS4751 = _M0L6_2atmpS4753 + _M0L2atS1457;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4755 = _M0L1pS1446->$6;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4751
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4755, _M0L1iS1463);
      }
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4750, _M0L1iS1463, _M0L6_2atmpS4751);
      _M0L4tabsS4756 = _M0L1pS1446->$7;
      _M0L4fireS4758 = _M0L1pS1446->$5;
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4758, _M0L1iS1463)) {
        _M0L6_2atmpS4757 = _M0L11tabs__stepsS1460;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4759 = _M0L1pS1446->$7;
        #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4757
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4759, _M0L1iS1463);
      }
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4756, _M0L1iS1463, _M0L6_2atmpS4757);
      goto join_1464;
      goto joinlet_5913;
      join_1464:;
      _M0L6_2atmpS4679 = _M0L1iS1463 + 1;
      _M0L1iS1463 = _M0L6_2atmpS4679;
      continue;
      joinlet_5913:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1441
) {
  int32_t _M0L1nS1440;
  int32_t _M0L7_2abindS1442;
  int32_t _M0L1iS1443;
  #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1440 = _M0L1pS1441->$2;
  _M0L7_2abindS1442 = 0;
  _M0L1iS1443 = _M0L7_2abindS1442;
  while (1) {
    if (_M0L1iS1443 < _M0L1nS1440) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4656 = _M0L1pS1441->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS4677 = _M0L1pS1441->$10;
      float _M0L6_2atmpS4672;
      struct _M0TPB5ArrayGfE* _M0L1vS4676;
      float _M0L6_2atmpS4674;
      float _M0L4e__eS4675;
      float _M0L6_2atmpS4673;
      float _M0L6_2atmpS4669;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS4671;
      float _M0L6_2atmpS4670;
      float _M0L6_2atmpS4658;
      struct _M0TPB5ArrayGfE* _M0L2giS4668;
      float _M0L6_2atmpS4663;
      struct _M0TPB5ArrayGfE* _M0L1vS4667;
      float _M0L6_2atmpS4665;
      float _M0L4e__iS4666;
      float _M0L6_2atmpS4664;
      float _M0L6_2atmpS4660;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS4662;
      float _M0L6_2atmpS4661;
      float _M0L6_2atmpS4659;
      float _M0L6_2atmpS4657;
      int32_t _M0L6_2atmpS4678;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4672 = _M0MPC15array5Array2atGfE(_M0L2geS4677, _M0L1iS1443);
      _M0L1vS4676 = _M0L1pS1441->$3;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4674 = _M0MPC15array5Array2atGfE(_M0L1vS4676, _M0L1iS1443);
      _M0L4e__eS4675 = _M0L1pS1441->$16;
      _M0L6_2atmpS4673 = _M0L6_2atmpS4674 - _M0L4e__eS4675;
      _M0L6_2atmpS4669 = _M0L6_2atmpS4672 * _M0L6_2atmpS4673;
      _M0L7gsyn__eS4671 = _M0L1pS1441->$14;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4670
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS4671, _M0L1iS1443);
      _M0L6_2atmpS4658 = _M0L6_2atmpS4669 * _M0L6_2atmpS4670;
      _M0L2giS4668 = _M0L1pS1441->$11;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4663 = _M0MPC15array5Array2atGfE(_M0L2giS4668, _M0L1iS1443);
      _M0L1vS4667 = _M0L1pS1441->$3;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4665 = _M0MPC15array5Array2atGfE(_M0L1vS4667, _M0L1iS1443);
      _M0L4e__iS4666 = _M0L1pS1441->$17;
      _M0L6_2atmpS4664 = _M0L6_2atmpS4665 - _M0L4e__iS4666;
      _M0L6_2atmpS4660 = _M0L6_2atmpS4663 * _M0L6_2atmpS4664;
      _M0L7gsyn__iS4662 = _M0L1pS1441->$15;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4661
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS4662, _M0L1iS1443);
      _M0L6_2atmpS4659 = _M0L6_2atmpS4660 * _M0L6_2atmpS4661;
      _M0L6_2atmpS4657 = _M0L6_2atmpS4658 + _M0L6_2atmpS4659;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS4656, _M0L1iS1443, _M0L6_2atmpS4657);
      _M0L6_2atmpS4678 = _M0L1iS1443 + 1;
      _M0L1iS1443 = _M0L6_2atmpS4678;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1430,
  float _M0L2dtS1435
) {
  int32_t _M0L1nS1429;
  float _M0L6tau__eS1431;
  float _M0L6tau__iS1432;
  int32_t _M0L7_2abindS1433;
  int32_t _M0L1iS1434;
  int32_t _M0L7_2abindS1437;
  int32_t _M0L1iS1438;
  #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1429 = _M0L1pS1430->$2;
  _M0L6tau__eS1431 = _M0L1pS1430->$18;
  _M0L6tau__iS1432 = _M0L1pS1430->$19;
  _M0L7_2abindS1433 = 0;
  _M0L1iS1434 = _M0L7_2abindS1433;
  while (1) {
    if (_M0L1iS1434 < _M0L1nS1429) {
      struct _M0TPB5ArrayGfE* _M0L2geS4622 = _M0L1pS1430->$10;
      struct _M0TPB5ArrayGfE* _M0L2geS4627 = _M0L1pS1430->$10;
      float _M0L6_2atmpS4624;
      struct _M0TPB5ArrayGfE* _M0L3gluS4626;
      float _M0L6_2atmpS4625;
      float _M0L6_2atmpS4623;
      struct _M0TPB5ArrayGfE* _M0L2giS4628;
      struct _M0TPB5ArrayGfE* _M0L2giS4633;
      float _M0L6_2atmpS4630;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4632;
      float _M0L6_2atmpS4631;
      float _M0L6_2atmpS4629;
      struct _M0TPB5ArrayGfE* _M0L2geS4634;
      struct _M0TPB5ArrayGfE* _M0L2geS4642;
      float _M0L6_2atmpS4636;
      struct _M0TPB5ArrayGfE* _M0L2geS4641;
      float _M0L6_2atmpS4640;
      float _M0L6_2atmpS4639;
      float _M0L6_2atmpS4638;
      float _M0L6_2atmpS4637;
      float _M0L6_2atmpS4635;
      struct _M0TPB5ArrayGfE* _M0L2giS4643;
      struct _M0TPB5ArrayGfE* _M0L2giS4651;
      float _M0L6_2atmpS4645;
      struct _M0TPB5ArrayGfE* _M0L2giS4650;
      float _M0L6_2atmpS4649;
      float _M0L6_2atmpS4648;
      float _M0L6_2atmpS4647;
      float _M0L6_2atmpS4646;
      float _M0L6_2atmpS4644;
      int32_t _M0L6_2atmpS4652;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4624 = _M0MPC15array5Array2atGfE(_M0L2geS4627, _M0L1iS1434);
      _M0L3gluS4626 = _M0L1pS1430->$12;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4625
      = _M0MPC15array5Array2atGfE(_M0L3gluS4626, _M0L1iS1434);
      _M0L6_2atmpS4623 = _M0L6_2atmpS4624 + _M0L6_2atmpS4625;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4622, _M0L1iS1434, _M0L6_2atmpS4623);
      _M0L2giS4628 = _M0L1pS1430->$11;
      _M0L2giS4633 = _M0L1pS1430->$11;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4630 = _M0MPC15array5Array2atGfE(_M0L2giS4633, _M0L1iS1434);
      _M0L4gabaS4632 = _M0L1pS1430->$13;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4631
      = _M0MPC15array5Array2atGfE(_M0L4gabaS4632, _M0L1iS1434);
      _M0L6_2atmpS4629 = _M0L6_2atmpS4630 + _M0L6_2atmpS4631;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4628, _M0L1iS1434, _M0L6_2atmpS4629);
      _M0L2geS4634 = _M0L1pS1430->$10;
      _M0L2geS4642 = _M0L1pS1430->$10;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4636 = _M0MPC15array5Array2atGfE(_M0L2geS4642, _M0L1iS1434);
      _M0L2geS4641 = _M0L1pS1430->$10;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4640 = _M0MPC15array5Array2atGfE(_M0L2geS4641, _M0L1iS1434);
      _M0L6_2atmpS4639 = -_M0L6_2atmpS4640;
      _M0L6_2atmpS4638 = _M0L6_2atmpS4639 / _M0L6tau__eS1431;
      _M0L6_2atmpS4637 = _M0L2dtS1435 * _M0L6_2atmpS4638;
      _M0L6_2atmpS4635 = _M0L6_2atmpS4636 + _M0L6_2atmpS4637;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4634, _M0L1iS1434, _M0L6_2atmpS4635);
      _M0L2giS4643 = _M0L1pS1430->$11;
      _M0L2giS4651 = _M0L1pS1430->$11;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4645 = _M0MPC15array5Array2atGfE(_M0L2giS4651, _M0L1iS1434);
      _M0L2giS4650 = _M0L1pS1430->$11;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4649 = _M0MPC15array5Array2atGfE(_M0L2giS4650, _M0L1iS1434);
      _M0L6_2atmpS4648 = -_M0L6_2atmpS4649;
      _M0L6_2atmpS4647 = _M0L6_2atmpS4648 / _M0L6tau__iS1432;
      _M0L6_2atmpS4646 = _M0L2dtS1435 * _M0L6_2atmpS4647;
      _M0L6_2atmpS4644 = _M0L6_2atmpS4645 + _M0L6_2atmpS4646;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4643, _M0L1iS1434, _M0L6_2atmpS4644);
      _M0L6_2atmpS4652 = _M0L1iS1434 + 1;
      _M0L1iS1434 = _M0L6_2atmpS4652;
      continue;
    }
    break;
  }
  _M0L7_2abindS1437 = 0;
  _M0L1iS1438 = _M0L7_2abindS1437;
  while (1) {
    if (_M0L1iS1438 < _M0L1nS1429) {
      struct _M0TPB5ArrayGfE* _M0L3gluS4653 = _M0L1pS1430->$12;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4654;
      int32_t _M0L6_2atmpS4655;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS4653, _M0L1iS1438, 0x0p+0f);
      _M0L4gabaS4654 = _M0L1pS1430->$13;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS4654, _M0L1iS1438, 0x0p+0f);
      _M0L6_2atmpS4655 = _M0L1iS1438 + 1;
      _M0L1iS1438 = _M0L6_2atmpS4655;
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
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1408,
  float _M0L2dtS1423
) {
  int32_t _M0L1nS1407;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L3p__S1409;
  float _M0L2tmS1410;
  float _M0L2vtS1411;
  float _M0L2vrS1412;
  float _M0L2elS1413;
  float _M0L1rS1414;
  float _M0L9dt__slopeS1415;
  float _M0L2twS1416;
  float _M0L1aS1417;
  float _M0L1bS1418;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4621;
  float _M0L2atS1419;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4620;
  float _M0L6tau__aS1420;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4619;
  float _M0L11tabs__constS1421;
  float _M0L6_2atmpS4618;
  int32_t _M0L11tabs__stepsS1422;
  int32_t _M0L7_2abindS1424;
  int32_t _M0L1iS1425;
  #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1407 = _M0L1pS1408->$2;
  _M0L3p__S1409 = _M0L1pS1408->$0;
  _M0L2tmS1410 = _M0L3p__S1409->$5;
  _M0L2vtS1411 = _M0L3p__S1409->$2;
  _M0L2vrS1412 = _M0L3p__S1409->$3;
  _M0L2elS1413 = _M0L3p__S1409->$4;
  _M0L1rS1414 = _M0L3p__S1409->$6;
  _M0L9dt__slopeS1415 = _M0L3p__S1409->$7;
  _M0L2twS1416 = _M0L3p__S1409->$8;
  _M0L1aS1417 = _M0L3p__S1409->$9;
  _M0L1bS1418 = _M0L3p__S1409->$10;
  _M0L5spikeS4621 = _M0L1pS1408->$1;
  _M0L2atS1419 = _M0L5spikeS4621->$0;
  _M0L5spikeS4620 = _M0L1pS1408->$1;
  _M0L6tau__aS1420 = _M0L5spikeS4620->$1;
  _M0L5spikeS4619 = _M0L1pS1408->$1;
  _M0L11tabs__constS1421 = _M0L5spikeS4619->$3;
  _M0L6_2atmpS4618 = _M0L11tabs__constS1421 / _M0L2dtS1423;
  #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L11tabs__stepsS1422 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4618);
  _M0L7_2abindS1424 = 0;
  _M0L1iS1425 = _M0L7_2abindS1424;
  while (1) {
    if (_M0L1iS1425 < _M0L1nS1407) {
      struct _M0TPB5ArrayGfE* _M0L1vS4531 = _M0L1pS1408->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS4533 = _M0L1pS1408->$5;
      float _M0L6_2atmpS4532;
      struct _M0TPB5ArrayGbE* _M0L4fireS4535;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4536;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4539;
      int32_t _M0L6_2atmpS4538;
      int32_t _M0L6_2atmpS4537;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4541;
      int32_t _M0L6_2atmpS4540;
      struct _M0TPB5ArrayGfE* _M0L1wS4542;
      struct _M0TPB5ArrayGfE* _M0L1wS4554;
      float _M0L6_2atmpS4544;
      struct _M0TPB5ArrayGfE* _M0L1vS4553;
      float _M0L6_2atmpS4552;
      float _M0L6_2atmpS4551;
      float _M0L6_2atmpS4548;
      struct _M0TPB5ArrayGfE* _M0L1wS4550;
      float _M0L6_2atmpS4549;
      float _M0L6_2atmpS4547;
      float _M0L6_2atmpS4546;
      float _M0L6_2atmpS4545;
      float _M0L6_2atmpS4543;
      float _M0L9exp__termS1428;
      struct _M0TPB5ArrayGfE* _M0L1vS4555;
      struct _M0TPB5ArrayGfE* _M0L1vS4577;
      float _M0L6_2atmpS4557;
      struct _M0TPB5ArrayGfE* _M0L1vS4576;
      float _M0L6_2atmpS4575;
      float _M0L6_2atmpS4574;
      float _M0L6_2atmpS4573;
      float _M0L6_2atmpS4569;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4572;
      float _M0L6_2atmpS4571;
      float _M0L6_2atmpS4570;
      float _M0L6_2atmpS4565;
      struct _M0TPB5ArrayGfE* _M0L1wS4568;
      float _M0L6_2atmpS4567;
      float _M0L6_2atmpS4566;
      float _M0L6_2atmpS4561;
      struct _M0TPB5ArrayGfE* _M0L1iS4564;
      float _M0L6_2atmpS4563;
      float _M0L6_2atmpS4562;
      float _M0L6_2atmpS4560;
      float _M0L6_2atmpS4559;
      float _M0L6_2atmpS4558;
      float _M0L6_2atmpS4556;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4578;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4586;
      float _M0L6_2atmpS4580;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4585;
      float _M0L6_2atmpS4584;
      float _M0L6_2atmpS4583;
      float _M0L6_2atmpS4582;
      float _M0L6_2atmpS4581;
      float _M0L6_2atmpS4579;
      struct _M0TPB5ArrayGbE* _M0L4fireS4587;
      struct _M0TPB5ArrayGfE* _M0L1vS4590;
      float _M0L6_2atmpS4589;
      int32_t _M0L6_2atmpS4588;
      struct _M0TPB5ArrayGfE* _M0L1vS4591;
      struct _M0TPB5ArrayGbE* _M0L4fireS4593;
      float _M0L6_2atmpS4592;
      struct _M0TPB5ArrayGfE* _M0L1wS4595;
      struct _M0TPB5ArrayGbE* _M0L4fireS4597;
      float _M0L6_2atmpS4596;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4601;
      struct _M0TPB5ArrayGbE* _M0L4fireS4603;
      float _M0L6_2atmpS4602;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4607;
      struct _M0TPB5ArrayGbE* _M0L4fireS4609;
      int32_t _M0L6_2atmpS4608;
      int32_t _M0L6_2atmpS4530;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4533, _M0L1iS1425)) {
        _M0L6_2atmpS4532 = _M0L2vrS1412;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4534 = _M0L1pS1408->$3;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4532
        = _M0MPC15array5Array2atGfE(_M0L1vS4534, _M0L1iS1425);
      }
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4531, _M0L1iS1425, _M0L6_2atmpS4532);
      _M0L4fireS4535 = _M0L1pS1408->$5;
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4535, _M0L1iS1425, 0);
      _M0L4tabsS4536 = _M0L1pS1408->$7;
      _M0L4tabsS4539 = _M0L1pS1408->$7;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4538
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4539, _M0L1iS1425);
      _M0L6_2atmpS4537 = _M0L6_2atmpS4538 - 1;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4536, _M0L1iS1425, _M0L6_2atmpS4537);
      _M0L4tabsS4541 = _M0L1pS1408->$7;
      #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4540
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4541, _M0L1iS1425);
      if (_M0L6_2atmpS4540 > 0) {
        goto join_1426;
      }
      _M0L1wS4542 = _M0L1pS1408->$4;
      _M0L1wS4554 = _M0L1pS1408->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4544 = _M0MPC15array5Array2atGfE(_M0L1wS4554, _M0L1iS1425);
      _M0L1vS4553 = _M0L1pS1408->$3;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4552 = _M0MPC15array5Array2atGfE(_M0L1vS4553, _M0L1iS1425);
      _M0L6_2atmpS4551 = _M0L6_2atmpS4552 - _M0L2elS1413;
      _M0L6_2atmpS4548 = _M0L1aS1417 * _M0L6_2atmpS4551;
      _M0L1wS4550 = _M0L1pS1408->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4549 = _M0MPC15array5Array2atGfE(_M0L1wS4550, _M0L1iS1425);
      _M0L6_2atmpS4547 = _M0L6_2atmpS4548 - _M0L6_2atmpS4549;
      _M0L6_2atmpS4546 = _M0L2dtS1423 * _M0L6_2atmpS4547;
      _M0L6_2atmpS4545 = _M0L6_2atmpS4546 / _M0L2twS1416;
      _M0L6_2atmpS4543 = _M0L6_2atmpS4544 + _M0L6_2atmpS4545;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4542, _M0L1iS1425, _M0L6_2atmpS4543);
      if (_M0L9dt__slopeS1415 < 0x0p+0f) {
        _M0L9exp__termS1428 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4617 = _M0L1pS1408->$3;
        float _M0L6_2atmpS4614;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4616;
        float _M0L6_2atmpS4615;
        float _M0L6_2atmpS4613;
        float _M0L6_2atmpS4612;
        float _M0L6_2atmpS4611;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4614
        = _M0MPC15array5Array2atGfE(_M0L1vS4617, _M0L1iS1425);
        _M0L9thresholdS4616 = _M0L1pS1408->$6;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4615
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4616, _M0L1iS1425);
        _M0L6_2atmpS4613 = _M0L6_2atmpS4614 - _M0L6_2atmpS4615;
        _M0L6_2atmpS4612 = _M0L6_2atmpS4613 / _M0L9dt__slopeS1415;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4611 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4612);
        _M0L9exp__termS1428 = _M0L9dt__slopeS1415 * _M0L6_2atmpS4611;
      }
      _M0L1vS4555 = _M0L1pS1408->$3;
      _M0L1vS4577 = _M0L1pS1408->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4557 = _M0MPC15array5Array2atGfE(_M0L1vS4577, _M0L1iS1425);
      _M0L1vS4576 = _M0L1pS1408->$3;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4575 = _M0MPC15array5Array2atGfE(_M0L1vS4576, _M0L1iS1425);
      _M0L6_2atmpS4574 = _M0L6_2atmpS4575 - _M0L2elS1413;
      _M0L6_2atmpS4573 = -_M0L6_2atmpS4574;
      _M0L6_2atmpS4569 = _M0L6_2atmpS4573 + _M0L9exp__termS1428;
      _M0L9syn__currS4572 = _M0L1pS1408->$9;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4571
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS4572, _M0L1iS1425);
      _M0L6_2atmpS4570 = _M0L1rS1414 * _M0L6_2atmpS4571;
      _M0L6_2atmpS4565 = _M0L6_2atmpS4569 - _M0L6_2atmpS4570;
      _M0L1wS4568 = _M0L1pS1408->$4;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4567 = _M0MPC15array5Array2atGfE(_M0L1wS4568, _M0L1iS1425);
      _M0L6_2atmpS4566 = _M0L1rS1414 * _M0L6_2atmpS4567;
      _M0L6_2atmpS4561 = _M0L6_2atmpS4565 - _M0L6_2atmpS4566;
      _M0L1iS4564 = _M0L1pS1408->$8;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4563 = _M0MPC15array5Array2atGfE(_M0L1iS4564, _M0L1iS1425);
      _M0L6_2atmpS4562 = _M0L1rS1414 * _M0L6_2atmpS4563;
      _M0L6_2atmpS4560 = _M0L6_2atmpS4561 + _M0L6_2atmpS4562;
      _M0L6_2atmpS4559 = _M0L2dtS1423 * _M0L6_2atmpS4560;
      _M0L6_2atmpS4558 = _M0L6_2atmpS4559 / _M0L2tmS1410;
      _M0L6_2atmpS4556 = _M0L6_2atmpS4557 + _M0L6_2atmpS4558;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4555, _M0L1iS1425, _M0L6_2atmpS4556);
      _M0L9thresholdS4578 = _M0L1pS1408->$6;
      _M0L9thresholdS4586 = _M0L1pS1408->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4580
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4586, _M0L1iS1425);
      _M0L9thresholdS4585 = _M0L1pS1408->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4584
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4585, _M0L1iS1425);
      _M0L6_2atmpS4583 = _M0L2vtS1411 - _M0L6_2atmpS4584;
      _M0L6_2atmpS4582 = _M0L2dtS1423 * _M0L6_2atmpS4583;
      _M0L6_2atmpS4581 = _M0L6_2atmpS4582 / _M0L6tau__aS1420;
      _M0L6_2atmpS4579 = _M0L6_2atmpS4580 + _M0L6_2atmpS4581;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4578, _M0L1iS1425, _M0L6_2atmpS4579);
      _M0L4fireS4587 = _M0L1pS1408->$5;
      _M0L1vS4590 = _M0L1pS1408->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4589 = _M0MPC15array5Array2atGfE(_M0L1vS4590, _M0L1iS1425);
      _M0L6_2atmpS4588 = _M0L6_2atmpS4589 >= 0x0p+0f;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4587, _M0L1iS1425, _M0L6_2atmpS4588);
      _M0L1vS4591 = _M0L1pS1408->$3;
      _M0L4fireS4593 = _M0L1pS1408->$5;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4593, _M0L1iS1425)) {
        _M0L6_2atmpS4592 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4594 = _M0L1pS1408->$3;
        #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4592
        = _M0MPC15array5Array2atGfE(_M0L1vS4594, _M0L1iS1425);
      }
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4591, _M0L1iS1425, _M0L6_2atmpS4592);
      _M0L1wS4595 = _M0L1pS1408->$4;
      _M0L4fireS4597 = _M0L1pS1408->$5;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4597, _M0L1iS1425)) {
        struct _M0TPB5ArrayGfE* _M0L1wS4599 = _M0L1pS1408->$4;
        float _M0L6_2atmpS4598;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4598
        = _M0MPC15array5Array2atGfE(_M0L1wS4599, _M0L1iS1425);
        _M0L6_2atmpS4596 = _M0L6_2atmpS4598 + _M0L1bS1418;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS4600 = _M0L1pS1408->$4;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4596
        = _M0MPC15array5Array2atGfE(_M0L1wS4600, _M0L1iS1425);
      }
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4595, _M0L1iS1425, _M0L6_2atmpS4596);
      _M0L9thresholdS4601 = _M0L1pS1408->$6;
      _M0L4fireS4603 = _M0L1pS1408->$5;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4603, _M0L1iS1425)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4605 = _M0L1pS1408->$6;
        float _M0L6_2atmpS4604;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4604
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4605, _M0L1iS1425);
        _M0L6_2atmpS4602 = _M0L6_2atmpS4604 + _M0L2atS1419;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4606 = _M0L1pS1408->$6;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4602
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4606, _M0L1iS1425);
      }
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4601, _M0L1iS1425, _M0L6_2atmpS4602);
      _M0L4tabsS4607 = _M0L1pS1408->$7;
      _M0L4fireS4609 = _M0L1pS1408->$5;
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4609, _M0L1iS1425)) {
        _M0L6_2atmpS4608 = _M0L11tabs__stepsS1422;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4610 = _M0L1pS1408->$7;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4608
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4610, _M0L1iS1425);
      }
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4607, _M0L1iS1425, _M0L6_2atmpS4608);
      goto join_1426;
      goto joinlet_5918;
      join_1426:;
      _M0L6_2atmpS4530 = _M0L1iS1425 + 1;
      _M0L1iS1425 = _M0L6_2atmpS4530;
      continue;
      joinlet_5918:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23adex__synaptic__current(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1403
) {
  int32_t _M0L1nS1402;
  int32_t _M0L7_2abindS1404;
  int32_t _M0L1iS1405;
  #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1402 = _M0L1pS1403->$2;
  _M0L7_2abindS1404 = 0;
  _M0L1iS1405 = _M0L7_2abindS1404;
  while (1) {
    if (_M0L1iS1405 < _M0L1nS1402) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4507 = _M0L1pS1403->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS4528 = _M0L1pS1403->$10;
      float _M0L6_2atmpS4523;
      struct _M0TPB5ArrayGfE* _M0L1vS4527;
      float _M0L6_2atmpS4525;
      float _M0L4e__eS4526;
      float _M0L6_2atmpS4524;
      float _M0L6_2atmpS4520;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS4522;
      float _M0L6_2atmpS4521;
      float _M0L6_2atmpS4509;
      struct _M0TPB5ArrayGfE* _M0L2giS4519;
      float _M0L6_2atmpS4514;
      struct _M0TPB5ArrayGfE* _M0L1vS4518;
      float _M0L6_2atmpS4516;
      float _M0L4e__iS4517;
      float _M0L6_2atmpS4515;
      float _M0L6_2atmpS4511;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS4513;
      float _M0L6_2atmpS4512;
      float _M0L6_2atmpS4510;
      float _M0L6_2atmpS4508;
      int32_t _M0L6_2atmpS4529;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4523 = _M0MPC15array5Array2atGfE(_M0L2geS4528, _M0L1iS1405);
      _M0L1vS4527 = _M0L1pS1403->$3;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4525 = _M0MPC15array5Array2atGfE(_M0L1vS4527, _M0L1iS1405);
      _M0L4e__eS4526 = _M0L1pS1403->$18;
      _M0L6_2atmpS4524 = _M0L6_2atmpS4525 - _M0L4e__eS4526;
      _M0L6_2atmpS4520 = _M0L6_2atmpS4523 * _M0L6_2atmpS4524;
      _M0L7gsyn__eS4522 = _M0L1pS1403->$16;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4521
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS4522, _M0L1iS1405);
      _M0L6_2atmpS4509 = _M0L6_2atmpS4520 * _M0L6_2atmpS4521;
      _M0L2giS4519 = _M0L1pS1403->$11;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4514 = _M0MPC15array5Array2atGfE(_M0L2giS4519, _M0L1iS1405);
      _M0L1vS4518 = _M0L1pS1403->$3;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4516 = _M0MPC15array5Array2atGfE(_M0L1vS4518, _M0L1iS1405);
      _M0L4e__iS4517 = _M0L1pS1403->$19;
      _M0L6_2atmpS4515 = _M0L6_2atmpS4516 - _M0L4e__iS4517;
      _M0L6_2atmpS4511 = _M0L6_2atmpS4514 * _M0L6_2atmpS4515;
      _M0L7gsyn__iS4513 = _M0L1pS1403->$17;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4512
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS4513, _M0L1iS1405);
      _M0L6_2atmpS4510 = _M0L6_2atmpS4511 * _M0L6_2atmpS4512;
      _M0L6_2atmpS4508 = _M0L6_2atmpS4509 + _M0L6_2atmpS4510;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS4507, _M0L1iS1405, _M0L6_2atmpS4508);
      _M0L6_2atmpS4529 = _M0L1iS1405 + 1;
      _M0L1iS1405 = _M0L6_2atmpS4529;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20adex__step__synapses(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1394,
  float _M0L2dtS1397
) {
  int32_t _M0L1nS1393;
  int32_t _M0L7_2abindS1395;
  int32_t _M0L1iS1396;
  int32_t _M0L7_2abindS1399;
  int32_t _M0L1iS1400;
  #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1393 = _M0L1pS1394->$2;
  _M0L7_2abindS1395 = 0;
  _M0L1iS1396 = _M0L7_2abindS1395;
  while (1) {
    if (_M0L1iS1396 < _M0L1nS1393) {
      struct _M0TPB5ArrayGfE* _M0L2heS4445 = _M0L1pS1394->$12;
      struct _M0TPB5ArrayGfE* _M0L2heS4450 = _M0L1pS1394->$12;
      float _M0L6_2atmpS4447;
      struct _M0TPB5ArrayGfE* _M0L3gluS4449;
      float _M0L6_2atmpS4448;
      float _M0L6_2atmpS4446;
      struct _M0TPB5ArrayGfE* _M0L2hiS4451;
      struct _M0TPB5ArrayGfE* _M0L2hiS4456;
      float _M0L6_2atmpS4453;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4455;
      float _M0L6_2atmpS4454;
      float _M0L6_2atmpS4452;
      struct _M0TPB5ArrayGfE* _M0L2geS4457;
      struct _M0TPB5ArrayGfE* _M0L2geS4469;
      float _M0L6_2atmpS4459;
      struct _M0TPB5ArrayGfE* _M0L2geS4468;
      float _M0L6_2atmpS4467;
      float _M0L6_2atmpS4465;
      float _M0L3tdeS4466;
      float _M0L6_2atmpS4462;
      struct _M0TPB5ArrayGfE* _M0L2heS4464;
      float _M0L6_2atmpS4463;
      float _M0L6_2atmpS4461;
      float _M0L6_2atmpS4460;
      float _M0L6_2atmpS4458;
      struct _M0TPB5ArrayGfE* _M0L2heS4470;
      struct _M0TPB5ArrayGfE* _M0L2heS4479;
      float _M0L6_2atmpS4472;
      struct _M0TPB5ArrayGfE* _M0L2heS4478;
      float _M0L6_2atmpS4477;
      float _M0L6_2atmpS4475;
      float _M0L3treS4476;
      float _M0L6_2atmpS4474;
      float _M0L6_2atmpS4473;
      float _M0L6_2atmpS4471;
      struct _M0TPB5ArrayGfE* _M0L2giS4480;
      struct _M0TPB5ArrayGfE* _M0L2giS4492;
      float _M0L6_2atmpS4482;
      struct _M0TPB5ArrayGfE* _M0L2giS4491;
      float _M0L6_2atmpS4490;
      float _M0L6_2atmpS4488;
      float _M0L3tdiS4489;
      float _M0L6_2atmpS4485;
      struct _M0TPB5ArrayGfE* _M0L2hiS4487;
      float _M0L6_2atmpS4486;
      float _M0L6_2atmpS4484;
      float _M0L6_2atmpS4483;
      float _M0L6_2atmpS4481;
      struct _M0TPB5ArrayGfE* _M0L2hiS4493;
      struct _M0TPB5ArrayGfE* _M0L2hiS4502;
      float _M0L6_2atmpS4495;
      struct _M0TPB5ArrayGfE* _M0L2hiS4501;
      float _M0L6_2atmpS4500;
      float _M0L6_2atmpS4498;
      float _M0L3triS4499;
      float _M0L6_2atmpS4497;
      float _M0L6_2atmpS4496;
      float _M0L6_2atmpS4494;
      int32_t _M0L6_2atmpS4503;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4447 = _M0MPC15array5Array2atGfE(_M0L2heS4450, _M0L1iS1396);
      _M0L3gluS4449 = _M0L1pS1394->$14;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4448
      = _M0MPC15array5Array2atGfE(_M0L3gluS4449, _M0L1iS1396);
      _M0L6_2atmpS4446 = _M0L6_2atmpS4447 + _M0L6_2atmpS4448;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4445, _M0L1iS1396, _M0L6_2atmpS4446);
      _M0L2hiS4451 = _M0L1pS1394->$13;
      _M0L2hiS4456 = _M0L1pS1394->$13;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4453 = _M0MPC15array5Array2atGfE(_M0L2hiS4456, _M0L1iS1396);
      _M0L4gabaS4455 = _M0L1pS1394->$15;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4454
      = _M0MPC15array5Array2atGfE(_M0L4gabaS4455, _M0L1iS1396);
      _M0L6_2atmpS4452 = _M0L6_2atmpS4453 + _M0L6_2atmpS4454;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4451, _M0L1iS1396, _M0L6_2atmpS4452);
      _M0L2geS4457 = _M0L1pS1394->$10;
      _M0L2geS4469 = _M0L1pS1394->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4459 = _M0MPC15array5Array2atGfE(_M0L2geS4469, _M0L1iS1396);
      _M0L2geS4468 = _M0L1pS1394->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4467 = _M0MPC15array5Array2atGfE(_M0L2geS4468, _M0L1iS1396);
      _M0L6_2atmpS4465 = -_M0L6_2atmpS4467;
      _M0L3tdeS4466 = _M0L1pS1394->$21;
      _M0L6_2atmpS4462 = _M0L6_2atmpS4465 / _M0L3tdeS4466;
      _M0L2heS4464 = _M0L1pS1394->$12;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4463 = _M0MPC15array5Array2atGfE(_M0L2heS4464, _M0L1iS1396);
      _M0L6_2atmpS4461 = _M0L6_2atmpS4462 + _M0L6_2atmpS4463;
      _M0L6_2atmpS4460 = _M0L2dtS1397 * _M0L6_2atmpS4461;
      _M0L6_2atmpS4458 = _M0L6_2atmpS4459 + _M0L6_2atmpS4460;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4457, _M0L1iS1396, _M0L6_2atmpS4458);
      _M0L2heS4470 = _M0L1pS1394->$12;
      _M0L2heS4479 = _M0L1pS1394->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4472 = _M0MPC15array5Array2atGfE(_M0L2heS4479, _M0L1iS1396);
      _M0L2heS4478 = _M0L1pS1394->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4477 = _M0MPC15array5Array2atGfE(_M0L2heS4478, _M0L1iS1396);
      _M0L6_2atmpS4475 = -_M0L6_2atmpS4477;
      _M0L3treS4476 = _M0L1pS1394->$20;
      _M0L6_2atmpS4474 = _M0L6_2atmpS4475 / _M0L3treS4476;
      _M0L6_2atmpS4473 = _M0L2dtS1397 * _M0L6_2atmpS4474;
      _M0L6_2atmpS4471 = _M0L6_2atmpS4472 + _M0L6_2atmpS4473;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4470, _M0L1iS1396, _M0L6_2atmpS4471);
      _M0L2giS4480 = _M0L1pS1394->$11;
      _M0L2giS4492 = _M0L1pS1394->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4482 = _M0MPC15array5Array2atGfE(_M0L2giS4492, _M0L1iS1396);
      _M0L2giS4491 = _M0L1pS1394->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4490 = _M0MPC15array5Array2atGfE(_M0L2giS4491, _M0L1iS1396);
      _M0L6_2atmpS4488 = -_M0L6_2atmpS4490;
      _M0L3tdiS4489 = _M0L1pS1394->$23;
      _M0L6_2atmpS4485 = _M0L6_2atmpS4488 / _M0L3tdiS4489;
      _M0L2hiS4487 = _M0L1pS1394->$13;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4486 = _M0MPC15array5Array2atGfE(_M0L2hiS4487, _M0L1iS1396);
      _M0L6_2atmpS4484 = _M0L6_2atmpS4485 + _M0L6_2atmpS4486;
      _M0L6_2atmpS4483 = _M0L2dtS1397 * _M0L6_2atmpS4484;
      _M0L6_2atmpS4481 = _M0L6_2atmpS4482 + _M0L6_2atmpS4483;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4480, _M0L1iS1396, _M0L6_2atmpS4481);
      _M0L2hiS4493 = _M0L1pS1394->$13;
      _M0L2hiS4502 = _M0L1pS1394->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4495 = _M0MPC15array5Array2atGfE(_M0L2hiS4502, _M0L1iS1396);
      _M0L2hiS4501 = _M0L1pS1394->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4500 = _M0MPC15array5Array2atGfE(_M0L2hiS4501, _M0L1iS1396);
      _M0L6_2atmpS4498 = -_M0L6_2atmpS4500;
      _M0L3triS4499 = _M0L1pS1394->$22;
      _M0L6_2atmpS4497 = _M0L6_2atmpS4498 / _M0L3triS4499;
      _M0L6_2atmpS4496 = _M0L2dtS1397 * _M0L6_2atmpS4497;
      _M0L6_2atmpS4494 = _M0L6_2atmpS4495 + _M0L6_2atmpS4496;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4493, _M0L1iS1396, _M0L6_2atmpS4494);
      _M0L6_2atmpS4503 = _M0L1iS1396 + 1;
      _M0L1iS1396 = _M0L6_2atmpS4503;
      continue;
    }
    break;
  }
  _M0L7_2abindS1399 = 0;
  _M0L1iS1400 = _M0L7_2abindS1399;
  while (1) {
    if (_M0L1iS1400 < _M0L1nS1393) {
      struct _M0TPB5ArrayGfE* _M0L3gluS4504 = _M0L1pS1394->$14;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4505;
      int32_t _M0L6_2atmpS4506;
      #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS4504, _M0L1iS1400, 0x0p+0f);
      _M0L4gabaS4505 = _M0L1pS1394->$15;
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS4505, _M0L1iS1400, 0x0p+0f);
      _M0L6_2atmpS4506 = _M0L1iS1400 + 1;
      _M0L1iS1400 = _M0L6_2atmpS4506;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16forward__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1369,
  float _M0L6t__nowS1380
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS4444;
  int32_t _M0L6_2atmpS4443;
  int32_t _M0L10use__delayS1368;
  struct _M0TPB5ArrayGfE* _M0L3rhoS4442;
  int32_t _M0L6_2atmpS4441;
  int32_t _M0L8use__rhoS1370;
  #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6delaysS4444 = _M0L1cS1369->$5;
  #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS4443 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS4444);
  _M0L10use__delayS1368 = _M0L6_2atmpS4443 > 0;
  _M0L3rhoS4442 = _M0L1cS1369->$6;
  #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS4441 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS4442);
  _M0L8use__rhoS1370 = _M0L6_2atmpS4441 > 0;
  if (_M0L10use__delayS1368) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4404 = _M0L1cS1369->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS4403 = _M0L3preS4404->$5;
    int32_t _M0L6n__preS1371;
    struct _M0TPB8MutLocalGiE* _M0L1jS1372;
    #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6n__preS1371 = _M0MPC15array5Array6lengthGbE(_M0L4fireS4403);
    _M0L1jS1372
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS1372)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS1372->$0 = 0;
    while (1) {
      int32_t _M0L3valS4372 = _M0L1jS1372->$0;
      if (_M0L3valS4372 < _M0L6n__preS1371) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4375 = _M0L1cS1369->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS4373 = _M0L3preS4375->$5;
        int32_t _M0L3valS4374 = _M0L1jS1372->$0;
        int32_t _M0L3valS4402;
        int32_t _M0L6_2atmpS4401;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS4373, _M0L3valS4374)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4400 =
            _M0L1cS1369->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS4398 = _M0L6matrixS4400->$2;
          int32_t _M0L3valS4399 = _M0L1jS1372->$0;
          int32_t _M0L5startS1373;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4397;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS4394;
          int32_t _M0L3valS4396;
          int32_t _M0L6_2atmpS4395;
          int32_t _M0L3endS1374;
          struct _M0TPB8MutLocalGiE* _M0L1sS1375;
          #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L5startS1373
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS4398, _M0L3valS4399);
          _M0L6matrixS4397 = _M0L1cS1369->$4;
          _M0L6rowptrS4394 = _M0L6matrixS4397->$2;
          _M0L3valS4396 = _M0L1jS1372->$0;
          _M0L6_2atmpS4395 = _M0L3valS4396 + 1;
          #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L3endS1374
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS4394, _M0L6_2atmpS4395);
          _M0L1sS1375
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS1375)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS1375->$0 = _M0L5startS1373;
          while (1) {
            int32_t _M0L3valS4376 = _M0L1sS1375->$0;
            if (_M0L3valS4376 < _M0L3endS1374) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4393 =
                _M0L1cS1369->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS4391 = _M0L6matrixS4393->$3;
              int32_t _M0L3valS4392 = _M0L1sS1375->$0;
              int32_t _M0L9post__idxS1376;
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4390;
              struct _M0TPB5ArrayGfE* _M0L4valsS4388;
              int32_t _M0L3valS4389;
              float _M0L1wS1377;
              struct _M0TPB5ArrayGfE* _M0L6delaysS4386;
              int32_t _M0L3valS4387;
              float _M0L1dS1378;
              float _M0L9w__scaledS1379;
              int32_t _M0L3valS4382;
              int32_t _M0L6_2atmpS4381;
              #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L9post__idxS1376
              = _M0MPC15array5Array2atGiE(_M0L6colptrS4391, _M0L3valS4392);
              _M0L6matrixS4390 = _M0L1cS1369->$4;
              _M0L4valsS4388 = _M0L6matrixS4390->$4;
              _M0L3valS4389 = _M0L1sS1375->$0;
              #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1wS1377
              = _M0MPC15array5Array2atGfE(_M0L4valsS4388, _M0L3valS4389);
              _M0L6delaysS4386 = _M0L1cS1369->$5;
              _M0L3valS4387 = _M0L1sS1375->$0;
              #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1dS1378
              = _M0MPC15array5Array2atGfE(_M0L6delaysS4386, _M0L3valS4387);
              if (_M0L8use__rhoS1370) {
                struct _M0TPB5ArrayGfE* _M0L3rhoS4384 = _M0L1cS1369->$6;
                int32_t _M0L3valS4385 = _M0L1sS1375->$0;
                float _M0L6_2atmpS4383;
                #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4383
                = _M0MPC15array5Array2atGfE(_M0L3rhoS4384, _M0L3valS4385);
                _M0L9w__scaledS1379 = _M0L1wS1377 * _M0L6_2atmpS4383;
              } else {
                _M0L9w__scaledS1379 = _M0L1wS1377;
              }
              if (_M0L1dS1378 == 0x0p+0f) {
                #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS1369, _M0L9post__idxS1376, _M0L9w__scaledS1379);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS4377 =
                  _M0L1cS1369->$7;
                float _M0L6_2atmpS4378 = _M0L6t__nowS1380 + _M0L1dS1378;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS4379;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4380;
                #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS4377, _M0L6_2atmpS4378);
                _M0L14pending__postsS4379 = _M0L1cS1369->$8;
                #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS4379, _M0L9post__idxS1376);
                _M0L16pending__weightsS4380 = _M0L1cS1369->$9;
                #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS4380, _M0L9w__scaledS1379);
              }
              _M0L3valS4382 = _M0L1sS1375->$0;
              _M0L6_2atmpS4381 = _M0L3valS4382 + 1;
              _M0L1sS1375->$0 = _M0L6_2atmpS4381;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1sS1375);
            }
            break;
          }
        }
        _M0L3valS4402 = _M0L1jS1372->$0;
        _M0L6_2atmpS4401 = _M0L3valS4402 + 1;
        _M0L1jS1372->$0 = _M0L6_2atmpS4401;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1jS1372);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS4438 = _M0L1cS1369->$2;
    struct _M0TPB5ArrayGfE* _M0L6targetS1383;
    #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    if (
      _M0L3symS4438 == (moonbit_string_t)moonbit_string_literal_9.data
      || Moonbit_array_length(_M0L3symS4438)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
         && 0
            == memcmp(_M0L3symS4438, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS4438) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4439 = _M0L1cS1369->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS5616 = _M0L4postS4439->$13;
      moonbit_incref_cycle_free(_M0L8_2afieldS5616);
      _M0L6targetS1383 = _M0L8_2afieldS5616;
    } else {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4440 = _M0L1cS1369->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS5617 = _M0L4postS4440->$14;
      moonbit_incref_cycle_free(_M0L8_2afieldS5617);
      _M0L6targetS1383 = _M0L8_2afieldS5617;
    }
    if (_M0L8use__rhoS1370) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4434 = _M0L1cS1369->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS4433 = _M0L3preS4434->$5;
      int32_t _M0L6n__preS1384;
      struct _M0TPB8MutLocalGiE* _M0L1jS1385;
      #line 283 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6n__preS1384 = _M0MPC15array5Array6lengthGbE(_M0L4fireS4433);
      _M0L1jS1385
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS1385)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS1385->$0 = 0;
      while (1) {
        int32_t _M0L3valS4405 = _M0L1jS1385->$0;
        if (_M0L3valS4405 < _M0L6n__preS1384) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4408 = _M0L1cS1369->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS4406 = _M0L3preS4408->$5;
          int32_t _M0L3valS4407 = _M0L1jS1385->$0;
          int32_t _M0L3valS4432;
          int32_t _M0L6_2atmpS4431;
          #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS4406, _M0L3valS4407)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4430 =
              _M0L1cS1369->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS4428 = _M0L6matrixS4430->$2;
            int32_t _M0L3valS4429 = _M0L1jS1385->$0;
            int32_t _M0L5startS1386;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4427;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS4424;
            int32_t _M0L3valS4426;
            int32_t _M0L6_2atmpS4425;
            int32_t _M0L3endS1387;
            struct _M0TPB8MutLocalGiE* _M0L1sS1388;
            #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L5startS1386
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS4428, _M0L3valS4429);
            _M0L6matrixS4427 = _M0L1cS1369->$4;
            _M0L6rowptrS4424 = _M0L6matrixS4427->$2;
            _M0L3valS4426 = _M0L1jS1385->$0;
            _M0L6_2atmpS4425 = _M0L3valS4426 + 1;
            #line 288 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L3endS1387
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS4424, _M0L6_2atmpS4425);
            _M0L1sS1388
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS1388)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS1388->$0 = _M0L5startS1386;
            while (1) {
              int32_t _M0L3valS4409 = _M0L1sS1388->$0;
              if (_M0L3valS4409 < _M0L3endS1387) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4423 =
                  _M0L1cS1369->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS4421 =
                  _M0L6matrixS4423->$3;
                int32_t _M0L3valS4422 = _M0L1sS1388->$0;
                int32_t _M0L9post__idxS1389;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4420;
                struct _M0TPB5ArrayGfE* _M0L4valsS4418;
                int32_t _M0L3valS4419;
                float _M0L6_2atmpS4414;
                struct _M0TPB5ArrayGfE* _M0L3rhoS4416;
                int32_t _M0L3valS4417;
                float _M0L6_2atmpS4415;
                float _M0L9w__scaledS1390;
                float _M0L6_2atmpS4411;
                float _M0L6_2atmpS4410;
                int32_t _M0L3valS4413;
                int32_t _M0L6_2atmpS4412;
                #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L9post__idxS1389
                = _M0MPC15array5Array2atGiE(_M0L6colptrS4421, _M0L3valS4422);
                _M0L6matrixS4420 = _M0L1cS1369->$4;
                _M0L4valsS4418 = _M0L6matrixS4420->$4;
                _M0L3valS4419 = _M0L1sS1388->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4414
                = _M0MPC15array5Array2atGfE(_M0L4valsS4418, _M0L3valS4419);
                _M0L3rhoS4416 = _M0L1cS1369->$6;
                _M0L3valS4417 = _M0L1sS1388->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4415
                = _M0MPC15array5Array2atGfE(_M0L3rhoS4416, _M0L3valS4417);
                _M0L9w__scaledS1390 = _M0L6_2atmpS4414 * _M0L6_2atmpS4415;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4411
                = _M0MPC15array5Array2atGfE(_M0L6targetS1383, _M0L9post__idxS1389);
                _M0L6_2atmpS4410 = _M0L6_2atmpS4411 + _M0L9w__scaledS1390;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array3setGfE(_M0L6targetS1383, _M0L9post__idxS1389, _M0L6_2atmpS4410);
                _M0L3valS4413 = _M0L1sS1388->$0;
                _M0L6_2atmpS4412 = _M0L3valS4413 + 1;
                _M0L1sS1388->$0 = _M0L6_2atmpS4412;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS1388);
              }
              break;
            }
          }
          _M0L3valS4432 = _M0L1jS1385->$0;
          _M0L6_2atmpS4431 = _M0L3valS4432 + 1;
          _M0L1jS1385->$0 = _M0L6_2atmpS4431;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1jS1385);
          moonbit_decref_cycle_free(_M0L6targetS1383);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4435 =
        _M0L1cS1369->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4437 = _M0L1cS1369->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS4436 = _M0L3preS4437->$5;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS4435, _M0L4fireS4436, _M0L6targetS1383);
      moonbit_decref_cycle_free(_M0L6targetS1383);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25deliver__pending__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1361,
  float _M0L6t__nowS1364
) {
  struct _M0TPB5ArrayGfE* _M0L14pending__timesS4371;
  int32_t _M0L1nS1360;
  struct _M0TPB8MutLocalGiE* _M0L4keptS1362;
  struct _M0TPB8MutLocalGiE* _M0L1kS1363;
  int32_t _M0L3valS4370;
  int32_t _M0L6_2atmpS4369;
  struct _M0TPB8MutLocalGiE* _M0L4dropS1366;
  #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L14pending__timesS4371 = _M0L1cS1361->$7;
  #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS1360 = _M0MPC15array5Array6lengthGfE(_M0L14pending__timesS4371);
  if (_M0L1nS1360 == 0) {
    return 0;
  }
  _M0L4keptS1362
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4keptS1362)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4keptS1362->$0 = 0;
  _M0L1kS1363
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS1363)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS1363->$0 = 0;
  while (1) {
    int32_t _M0L3valS4332 = _M0L1kS1363->$0;
    if (_M0L3valS4332 < _M0L1nS1360) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS4334 = _M0L1cS1361->$7;
      int32_t _M0L3valS4335 = _M0L1kS1363->$0;
      float _M0L6_2atmpS4333;
      int32_t _M0L3valS4362;
      int32_t _M0L6_2atmpS4361;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS4333
      = _M0MPC15array5Array2atGfE(_M0L14pending__timesS4334, _M0L3valS4335);
      if (_M0L6_2atmpS4333 <= _M0L6t__nowS1364) {
        struct _M0TPB5ArrayGiE* _M0L14pending__postsS4340 = _M0L1cS1361->$8;
        int32_t _M0L3valS4341 = _M0L1kS1363->$0;
        int32_t _M0L6_2atmpS4336;
        struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4338;
        int32_t _M0L3valS4339;
        float _M0L6_2atmpS4337;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS4336
        = _M0MPC15array5Array2atGiE(_M0L14pending__postsS4340, _M0L3valS4341);
        _M0L16pending__weightsS4338 = _M0L1cS1361->$9;
        _M0L3valS4339 = _M0L1kS1363->$0;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS4337
        = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS4338, _M0L3valS4339);
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS1361, _M0L6_2atmpS4336, _M0L6_2atmpS4337);
      } else {
        int32_t _M0L3valS4342 = _M0L4keptS1362->$0;
        int32_t _M0L3valS4343 = _M0L1kS1363->$0;
        int32_t _M0L3valS4360;
        int32_t _M0L6_2atmpS4359;
        if (_M0L3valS4342 != _M0L3valS4343) {
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS4344 = _M0L1cS1361->$7;
          int32_t _M0L3valS4345 = _M0L4keptS1362->$0;
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS4347 = _M0L1cS1361->$7;
          int32_t _M0L3valS4348 = _M0L1kS1363->$0;
          float _M0L6_2atmpS4346;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS4349;
          int32_t _M0L3valS4350;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS4352;
          int32_t _M0L3valS4353;
          int32_t _M0L6_2atmpS4351;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4354;
          int32_t _M0L3valS4355;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4357;
          int32_t _M0L3valS4358;
          float _M0L6_2atmpS4356;
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS4346
          = _M0MPC15array5Array2atGfE(_M0L14pending__timesS4347, _M0L3valS4348);
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L14pending__timesS4344, _M0L3valS4345, _M0L6_2atmpS4346);
          _M0L14pending__postsS4349 = _M0L1cS1361->$8;
          _M0L3valS4350 = _M0L4keptS1362->$0;
          _M0L14pending__postsS4352 = _M0L1cS1361->$8;
          _M0L3valS4353 = _M0L1kS1363->$0;
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS4351
          = _M0MPC15array5Array2atGiE(_M0L14pending__postsS4352, _M0L3valS4353);
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGiE(_M0L14pending__postsS4349, _M0L3valS4350, _M0L6_2atmpS4351);
          _M0L16pending__weightsS4354 = _M0L1cS1361->$9;
          _M0L3valS4355 = _M0L4keptS1362->$0;
          _M0L16pending__weightsS4357 = _M0L1cS1361->$9;
          _M0L3valS4358 = _M0L1kS1363->$0;
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS4356
          = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS4357, _M0L3valS4358);
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L16pending__weightsS4354, _M0L3valS4355, _M0L6_2atmpS4356);
        }
        _M0L3valS4360 = _M0L4keptS1362->$0;
        _M0L6_2atmpS4359 = _M0L3valS4360 + 1;
        _M0L4keptS1362->$0 = _M0L6_2atmpS4359;
      }
      _M0L3valS4362 = _M0L1kS1363->$0;
      _M0L6_2atmpS4361 = _M0L3valS4362 + 1;
      _M0L1kS1363->$0 = _M0L6_2atmpS4361;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS1363);
    }
    break;
  }
  _M0L3valS4370 = _M0L4keptS1362->$0;
  moonbit_decref_cycle_free(_M0L4keptS1362);
  _M0L6_2atmpS4369 = _M0L1nS1360 - _M0L3valS4370;
  _M0L4dropS1366
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4dropS1366)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4dropS1366->$0 = _M0L6_2atmpS4369;
  while (1) {
    int32_t _M0L3valS4363 = _M0L4dropS1366->$0;
    if (_M0L3valS4363 > 0) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS4364 = _M0L1cS1361->$7;
      void* _M0L6_2atmpS5619;
      struct _M0TPB5ArrayGiE* _M0L14pending__postsS4365;
      struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4366;
      void* _M0L6_2atmpS5618;
      int32_t _M0L3valS4368;
      int32_t _M0L6_2atmpS4367;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS5619
      = _M0MPC15array5Array3popGfE(_M0L14pending__timesS4364);
      moonbit_decref_cycle_free(_M0L6_2atmpS5619);
      _M0L14pending__postsS4365 = _M0L1cS1361->$8;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MPC15array5Array3popGiE(_M0L14pending__postsS4365);
      _M0L16pending__weightsS4366 = _M0L1cS1361->$9;
      #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS5618
      = _M0MPC15array5Array3popGfE(_M0L16pending__weightsS4366);
      moonbit_decref_cycle_free(_M0L6_2atmpS5618);
      _M0L3valS4368 = _M0L4dropS1366->$0;
      _M0L6_2atmpS4367 = _M0L3valS4368 - 1;
      _M0L4dropS1366->$0 = _M0L6_2atmpS4367;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4dropS1366);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13apply__weight(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1357,
  int32_t _M0L9post__idxS1358,
  float _M0L1wS1359
) {
  moonbit_string_t _M0L3symS4319;
  #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L3symS4319 = _M0L1cS1357->$2;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  if (
    _M0L3symS4319 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS4319)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS4319, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS4319) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4325 = _M0L1cS1357->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS4320 = _M0L4postS4325->$13;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4324 = _M0L1cS1357->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS4323 = _M0L4postS4324->$13;
    float _M0L6_2atmpS4322;
    float _M0L6_2atmpS4321;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS4322
    = _M0MPC15array5Array2atGfE(_M0L3gluS4323, _M0L9post__idxS1358);
    _M0L6_2atmpS4321 = _M0L6_2atmpS4322 + _M0L1wS1359;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L3gluS4320, _M0L9post__idxS1358, _M0L6_2atmpS4321);
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4331 = _M0L1cS1357->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS4326 = _M0L4postS4331->$14;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4330 = _M0L1cS1357->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS4329 = _M0L4postS4330->$14;
    float _M0L6_2atmpS4328;
    float _M0L6_2atmpS4327;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS4328
    = _M0MPC15array5Array2atGfE(_M0L4gabaS4329, _M0L9post__idxS1358);
    _M0L6_2atmpS4327 = _M0L6_2atmpS4328 + _M0L1wS1359;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L4gabaS4326, _M0L9post__idxS1358, _M0L6_2atmpS4327);
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt11record__one(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS1354,
  float _M0L1tS1356
) {
  int32_t _M0L11step__countS4305;
  int32_t _M0L6_2atmpS4304;
  int32_t _M0L11step__countS4307;
  int32_t _M0L9rec__stepS4308;
  int32_t _M0L6_2atmpS4306;
  moonbit_string_t _M0L3symS4311;
  float _M0L1vS1355;
  struct _M0TPB5ArrayGfE* _M0L4dataS4309;
  struct _M0TPB5ArrayGfE* _M0L5timesS4310;
  #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L11step__countS4305 = _M0L1mS1354->$6;
  _M0L6_2atmpS4304 = _M0L11step__countS4305 + 1;
  _M0L1mS1354->$6 = _M0L6_2atmpS4304;
  _M0L11step__countS4307 = _M0L1mS1354->$6;
  _M0L9rec__stepS4308 = _M0L1mS1354->$5;
  _M0L6_2atmpS4306 = _M0L11step__countS4307 % _M0L9rec__stepS4308;
  if (_M0L6_2atmpS4306 != 0) {
    return 0;
  }
  _M0L3symS4311 = _M0L1mS1354->$1;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS4311 == (moonbit_string_t)moonbit_string_literal_10.data
    || Moonbit_array_length(_M0L3symS4311)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_10.data)
       && 0
          == memcmp(_M0L3symS4311, (moonbit_string_t)moonbit_string_literal_10.data, Moonbit_array_length(_M0L3symS4311) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS4314 = _M0L1mS1354->$0;
    struct _M0TPB5ArrayGfE* _M0L1vS4312 = _M0L3popS4314->$3;
    int32_t _M0L6neuronS4313 = _M0L1mS1354->$4;
    #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _M0L1vS1355 = _M0MPC15array5Array2atGfE(_M0L1vS4312, _M0L6neuronS4313);
  } else {
    moonbit_string_t _M0L3symS4315 = _M0L1mS1354->$1;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    if (
      _M0L3symS4315 == (moonbit_string_t)moonbit_string_literal_11.data
      || Moonbit_array_length(_M0L3symS4315)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_11.data)
         && 0
            == memcmp(_M0L3symS4315, (moonbit_string_t)moonbit_string_literal_11.data, Moonbit_array_length(_M0L3symS4315) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS4318 = _M0L1mS1354->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS4316 = _M0L3popS4318->$5;
      int32_t _M0L6neuronS4317 = _M0L1mS1354->$4;
      #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4316, _M0L6neuronS4317)) {
        _M0L1vS1355 = 0x1p+0f;
      } else {
        _M0L1vS1355 = 0x0p+0f;
      }
    } else {
      _M0L1vS1355 = 0x0p+0f;
    }
  }
  _M0L4dataS4309 = _M0L1mS1354->$2;
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L4dataS4309, _M0L1vS1355);
  _M0L5timesS4310 = _M0L1mS1354->$3;
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L5timesS4310, _M0L1tS1356);
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1350
) {
  int32_t _M0L1nS1349;
  int32_t _M0L7_2abindS1351;
  int32_t _M0L1iS1352;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1349 = _M0L1pS1350->$2;
  _M0L7_2abindS1351 = 0;
  _M0L1iS1352 = _M0L7_2abindS1351;
  while (1) {
    if (_M0L1iS1352 < _M0L1nS1349) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4281 = _M0L1pS1350->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS4302 = _M0L1pS1350->$9;
      float _M0L6_2atmpS4297;
      struct _M0TPB5ArrayGfE* _M0L1vS4301;
      float _M0L6_2atmpS4299;
      float _M0L4e__eS4300;
      float _M0L6_2atmpS4298;
      float _M0L6_2atmpS4294;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS4296;
      float _M0L6_2atmpS4295;
      float _M0L6_2atmpS4283;
      struct _M0TPB5ArrayGfE* _M0L2giS4293;
      float _M0L6_2atmpS4288;
      struct _M0TPB5ArrayGfE* _M0L1vS4292;
      float _M0L6_2atmpS4290;
      float _M0L4e__iS4291;
      float _M0L6_2atmpS4289;
      float _M0L6_2atmpS4285;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS4287;
      float _M0L6_2atmpS4286;
      float _M0L6_2atmpS4284;
      float _M0L6_2atmpS4282;
      int32_t _M0L6_2atmpS4303;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4297 = _M0MPC15array5Array2atGfE(_M0L2geS4302, _M0L1iS1352);
      _M0L1vS4301 = _M0L1pS1350->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4299 = _M0MPC15array5Array2atGfE(_M0L1vS4301, _M0L1iS1352);
      _M0L4e__eS4300 = _M0L1pS1350->$17;
      _M0L6_2atmpS4298 = _M0L6_2atmpS4299 - _M0L4e__eS4300;
      _M0L6_2atmpS4294 = _M0L6_2atmpS4297 * _M0L6_2atmpS4298;
      _M0L7gsyn__eS4296 = _M0L1pS1350->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4295
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS4296, _M0L1iS1352);
      _M0L6_2atmpS4283 = _M0L6_2atmpS4294 * _M0L6_2atmpS4295;
      _M0L2giS4293 = _M0L1pS1350->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4288 = _M0MPC15array5Array2atGfE(_M0L2giS4293, _M0L1iS1352);
      _M0L1vS4292 = _M0L1pS1350->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4290 = _M0MPC15array5Array2atGfE(_M0L1vS4292, _M0L1iS1352);
      _M0L4e__iS4291 = _M0L1pS1350->$18;
      _M0L6_2atmpS4289 = _M0L6_2atmpS4290 - _M0L4e__iS4291;
      _M0L6_2atmpS4285 = _M0L6_2atmpS4288 * _M0L6_2atmpS4289;
      _M0L7gsyn__iS4287 = _M0L1pS1350->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4286
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS4287, _M0L1iS1352);
      _M0L6_2atmpS4284 = _M0L6_2atmpS4285 * _M0L6_2atmpS4286;
      _M0L6_2atmpS4282 = _M0L6_2atmpS4283 + _M0L6_2atmpS4284;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS4281, _M0L1iS1352, _M0L6_2atmpS4282);
      _M0L6_2atmpS4303 = _M0L1iS1352 + 1;
      _M0L1iS1352 = _M0L6_2atmpS4303;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1341,
  float _M0L2dtS1344
) {
  int32_t _M0L1nS1340;
  int32_t _M0L7_2abindS1342;
  int32_t _M0L1iS1343;
  int32_t _M0L7_2abindS1346;
  int32_t _M0L1iS1347;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1340 = _M0L1pS1341->$2;
  _M0L7_2abindS1342 = 0;
  _M0L1iS1343 = _M0L7_2abindS1342;
  while (1) {
    if (_M0L1iS1343 < _M0L1nS1340) {
      struct _M0TPB5ArrayGfE* _M0L2heS4219 = _M0L1pS1341->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS4224 = _M0L1pS1341->$11;
      float _M0L6_2atmpS4221;
      struct _M0TPB5ArrayGfE* _M0L3gluS4223;
      float _M0L6_2atmpS4222;
      float _M0L6_2atmpS4220;
      struct _M0TPB5ArrayGfE* _M0L2hiS4225;
      struct _M0TPB5ArrayGfE* _M0L2hiS4230;
      float _M0L6_2atmpS4227;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4229;
      float _M0L6_2atmpS4228;
      float _M0L6_2atmpS4226;
      struct _M0TPB5ArrayGfE* _M0L2geS4231;
      struct _M0TPB5ArrayGfE* _M0L2geS4243;
      float _M0L6_2atmpS4233;
      struct _M0TPB5ArrayGfE* _M0L2geS4242;
      float _M0L6_2atmpS4241;
      float _M0L6_2atmpS4239;
      float _M0L3tdeS4240;
      float _M0L6_2atmpS4236;
      struct _M0TPB5ArrayGfE* _M0L2heS4238;
      float _M0L6_2atmpS4237;
      float _M0L6_2atmpS4235;
      float _M0L6_2atmpS4234;
      float _M0L6_2atmpS4232;
      struct _M0TPB5ArrayGfE* _M0L2heS4244;
      struct _M0TPB5ArrayGfE* _M0L2heS4253;
      float _M0L6_2atmpS4246;
      struct _M0TPB5ArrayGfE* _M0L2heS4252;
      float _M0L6_2atmpS4251;
      float _M0L6_2atmpS4249;
      float _M0L3treS4250;
      float _M0L6_2atmpS4248;
      float _M0L6_2atmpS4247;
      float _M0L6_2atmpS4245;
      struct _M0TPB5ArrayGfE* _M0L2giS4254;
      struct _M0TPB5ArrayGfE* _M0L2giS4266;
      float _M0L6_2atmpS4256;
      struct _M0TPB5ArrayGfE* _M0L2giS4265;
      float _M0L6_2atmpS4264;
      float _M0L6_2atmpS4262;
      float _M0L3tdiS4263;
      float _M0L6_2atmpS4259;
      struct _M0TPB5ArrayGfE* _M0L2hiS4261;
      float _M0L6_2atmpS4260;
      float _M0L6_2atmpS4258;
      float _M0L6_2atmpS4257;
      float _M0L6_2atmpS4255;
      struct _M0TPB5ArrayGfE* _M0L2hiS4267;
      struct _M0TPB5ArrayGfE* _M0L2hiS4276;
      float _M0L6_2atmpS4269;
      struct _M0TPB5ArrayGfE* _M0L2hiS4275;
      float _M0L6_2atmpS4274;
      float _M0L6_2atmpS4272;
      float _M0L3triS4273;
      float _M0L6_2atmpS4271;
      float _M0L6_2atmpS4270;
      float _M0L6_2atmpS4268;
      int32_t _M0L6_2atmpS4277;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4221 = _M0MPC15array5Array2atGfE(_M0L2heS4224, _M0L1iS1343);
      _M0L3gluS4223 = _M0L1pS1341->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4222
      = _M0MPC15array5Array2atGfE(_M0L3gluS4223, _M0L1iS1343);
      _M0L6_2atmpS4220 = _M0L6_2atmpS4221 + _M0L6_2atmpS4222;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4219, _M0L1iS1343, _M0L6_2atmpS4220);
      _M0L2hiS4225 = _M0L1pS1341->$12;
      _M0L2hiS4230 = _M0L1pS1341->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4227 = _M0MPC15array5Array2atGfE(_M0L2hiS4230, _M0L1iS1343);
      _M0L4gabaS4229 = _M0L1pS1341->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4228
      = _M0MPC15array5Array2atGfE(_M0L4gabaS4229, _M0L1iS1343);
      _M0L6_2atmpS4226 = _M0L6_2atmpS4227 + _M0L6_2atmpS4228;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4225, _M0L1iS1343, _M0L6_2atmpS4226);
      _M0L2geS4231 = _M0L1pS1341->$9;
      _M0L2geS4243 = _M0L1pS1341->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4233 = _M0MPC15array5Array2atGfE(_M0L2geS4243, _M0L1iS1343);
      _M0L2geS4242 = _M0L1pS1341->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4241 = _M0MPC15array5Array2atGfE(_M0L2geS4242, _M0L1iS1343);
      _M0L6_2atmpS4239 = -_M0L6_2atmpS4241;
      _M0L3tdeS4240 = _M0L1pS1341->$20;
      _M0L6_2atmpS4236 = _M0L6_2atmpS4239 / _M0L3tdeS4240;
      _M0L2heS4238 = _M0L1pS1341->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4237 = _M0MPC15array5Array2atGfE(_M0L2heS4238, _M0L1iS1343);
      _M0L6_2atmpS4235 = _M0L6_2atmpS4236 + _M0L6_2atmpS4237;
      _M0L6_2atmpS4234 = _M0L2dtS1344 * _M0L6_2atmpS4235;
      _M0L6_2atmpS4232 = _M0L6_2atmpS4233 + _M0L6_2atmpS4234;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4231, _M0L1iS1343, _M0L6_2atmpS4232);
      _M0L2heS4244 = _M0L1pS1341->$11;
      _M0L2heS4253 = _M0L1pS1341->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4246 = _M0MPC15array5Array2atGfE(_M0L2heS4253, _M0L1iS1343);
      _M0L2heS4252 = _M0L1pS1341->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4251 = _M0MPC15array5Array2atGfE(_M0L2heS4252, _M0L1iS1343);
      _M0L6_2atmpS4249 = -_M0L6_2atmpS4251;
      _M0L3treS4250 = _M0L1pS1341->$19;
      _M0L6_2atmpS4248 = _M0L6_2atmpS4249 / _M0L3treS4250;
      _M0L6_2atmpS4247 = _M0L2dtS1344 * _M0L6_2atmpS4248;
      _M0L6_2atmpS4245 = _M0L6_2atmpS4246 + _M0L6_2atmpS4247;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4244, _M0L1iS1343, _M0L6_2atmpS4245);
      _M0L2giS4254 = _M0L1pS1341->$10;
      _M0L2giS4266 = _M0L1pS1341->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4256 = _M0MPC15array5Array2atGfE(_M0L2giS4266, _M0L1iS1343);
      _M0L2giS4265 = _M0L1pS1341->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4264 = _M0MPC15array5Array2atGfE(_M0L2giS4265, _M0L1iS1343);
      _M0L6_2atmpS4262 = -_M0L6_2atmpS4264;
      _M0L3tdiS4263 = _M0L1pS1341->$22;
      _M0L6_2atmpS4259 = _M0L6_2atmpS4262 / _M0L3tdiS4263;
      _M0L2hiS4261 = _M0L1pS1341->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4260 = _M0MPC15array5Array2atGfE(_M0L2hiS4261, _M0L1iS1343);
      _M0L6_2atmpS4258 = _M0L6_2atmpS4259 + _M0L6_2atmpS4260;
      _M0L6_2atmpS4257 = _M0L2dtS1344 * _M0L6_2atmpS4258;
      _M0L6_2atmpS4255 = _M0L6_2atmpS4256 + _M0L6_2atmpS4257;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4254, _M0L1iS1343, _M0L6_2atmpS4255);
      _M0L2hiS4267 = _M0L1pS1341->$12;
      _M0L2hiS4276 = _M0L1pS1341->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4269 = _M0MPC15array5Array2atGfE(_M0L2hiS4276, _M0L1iS1343);
      _M0L2hiS4275 = _M0L1pS1341->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4274 = _M0MPC15array5Array2atGfE(_M0L2hiS4275, _M0L1iS1343);
      _M0L6_2atmpS4272 = -_M0L6_2atmpS4274;
      _M0L3triS4273 = _M0L1pS1341->$21;
      _M0L6_2atmpS4271 = _M0L6_2atmpS4272 / _M0L3triS4273;
      _M0L6_2atmpS4270 = _M0L2dtS1344 * _M0L6_2atmpS4271;
      _M0L6_2atmpS4268 = _M0L6_2atmpS4269 + _M0L6_2atmpS4270;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4267, _M0L1iS1343, _M0L6_2atmpS4268);
      _M0L6_2atmpS4277 = _M0L1iS1343 + 1;
      _M0L1iS1343 = _M0L6_2atmpS4277;
      continue;
    }
    break;
  }
  _M0L7_2abindS1346 = 0;
  _M0L1iS1347 = _M0L7_2abindS1346;
  while (1) {
    if (_M0L1iS1347 < _M0L1nS1340) {
      struct _M0TPB5ArrayGfE* _M0L3gluS4278 = _M0L1pS1341->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4279;
      int32_t _M0L6_2atmpS4280;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS4278, _M0L1iS1347, 0x0p+0f);
      _M0L4gabaS4279 = _M0L1pS1341->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS4279, _M0L1iS1347, 0x0p+0f);
      _M0L6_2atmpS4280 = _M0L1iS1347 + 1;
      _M0L1iS1347 = _M0L6_2atmpS4280;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1326,
  float _M0L2dtS1335
) {
  int32_t _M0L1nS1325;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S1327;
  float _M0L2tmS1328;
  float _M0L2elS1329;
  float _M0L1rS1330;
  float _M0L2vtS1331;
  float _M0L2vrS1332;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS4218;
  float _M0L11tabs__constS1333;
  float _M0L6_2atmpS4217;
  int32_t _M0L11tabs__stepsS1334;
  int32_t _M0L7_2abindS1336;
  int32_t _M0L1iS1337;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1325 = _M0L1pS1326->$2;
  _M0L3p__S1327 = _M0L1pS1326->$0;
  _M0L2tmS1328 = _M0L3p__S1327->$2;
  _M0L2elS1329 = _M0L3p__S1327->$5;
  _M0L1rS1330 = _M0L3p__S1327->$6;
  _M0L2vtS1331 = _M0L3p__S1327->$3;
  _M0L2vrS1332 = _M0L3p__S1327->$4;
  _M0L5spikeS4218 = _M0L1pS1326->$1;
  _M0L11tabs__constS1333 = _M0L5spikeS4218->$0;
  _M0L6_2atmpS4217 = _M0L11tabs__constS1333 / _M0L2dtS1335;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS1334 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4217);
  _M0L7_2abindS1336 = 0;
  _M0L1iS1337 = _M0L7_2abindS1336;
  while (1) {
    if (_M0L1iS1337 < _M0L1nS1325) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS4177 = _M0L1pS1326->$6;
      int32_t _M0L6_2atmpS4176;
      struct _M0TPB5ArrayGfE* _M0L1vS4183;
      struct _M0TPB5ArrayGfE* _M0L1vS4204;
      float _M0L6_2atmpS4185;
      float _M0L6_2atmpS4187;
      struct _M0TPB5ArrayGfE* _M0L1vS4203;
      float _M0L6_2atmpS4202;
      float _M0L6_2atmpS4201;
      float _M0L6_2atmpS4193;
      struct _M0TPB5ArrayGfE* _M0L1wS4200;
      float _M0L6_2atmpS4199;
      float _M0L6_2atmpS4196;
      struct _M0TPB5ArrayGfE* _M0L1iS4198;
      float _M0L6_2atmpS4197;
      float _M0L6_2atmpS4195;
      float _M0L6_2atmpS4194;
      float _M0L6_2atmpS4189;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4192;
      float _M0L6_2atmpS4191;
      float _M0L6_2atmpS4190;
      float _M0L6_2atmpS4188;
      float _M0L6_2atmpS4186;
      float _M0L6_2atmpS4184;
      struct _M0TPB5ArrayGbE* _M0L4fireS4205;
      struct _M0TPB5ArrayGfE* _M0L1vS4208;
      float _M0L6_2atmpS4207;
      int32_t _M0L6_2atmpS4206;
      struct _M0TPB5ArrayGfE* _M0L1vS4209;
      struct _M0TPB5ArrayGbE* _M0L4fireS4211;
      float _M0L6_2atmpS4210;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4213;
      struct _M0TPB5ArrayGbE* _M0L4fireS4215;
      int32_t _M0L6_2atmpS4214;
      int32_t _M0L6_2atmpS4175;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4176
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4177, _M0L1iS1337);
      if (_M0L6_2atmpS4176 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS4178 = _M0L1pS1326->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS4179;
        struct _M0TPB5ArrayGiE* _M0L4tabsS4182;
        int32_t _M0L6_2atmpS4181;
        int32_t _M0L6_2atmpS4180;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS4178, _M0L1iS1337, 0);
        _M0L4tabsS4179 = _M0L1pS1326->$6;
        _M0L4tabsS4182 = _M0L1pS1326->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS4181
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4182, _M0L1iS1337);
        _M0L6_2atmpS4180 = _M0L6_2atmpS4181 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS4179, _M0L1iS1337, _M0L6_2atmpS4180);
        goto join_1338;
      }
      _M0L1vS4183 = _M0L1pS1326->$3;
      _M0L1vS4204 = _M0L1pS1326->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4185 = _M0MPC15array5Array2atGfE(_M0L1vS4204, _M0L1iS1337);
      _M0L6_2atmpS4187 = _M0L2dtS1335 / _M0L2tmS1328;
      _M0L1vS4203 = _M0L1pS1326->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4202 = _M0MPC15array5Array2atGfE(_M0L1vS4203, _M0L1iS1337);
      _M0L6_2atmpS4201 = _M0L6_2atmpS4202 - _M0L2elS1329;
      _M0L6_2atmpS4193 = -_M0L6_2atmpS4201;
      _M0L1wS4200 = _M0L1pS1326->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4199 = _M0MPC15array5Array2atGfE(_M0L1wS4200, _M0L1iS1337);
      _M0L6_2atmpS4196 = -_M0L6_2atmpS4199;
      _M0L1iS4198 = _M0L1pS1326->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4197 = _M0MPC15array5Array2atGfE(_M0L1iS4198, _M0L1iS1337);
      _M0L6_2atmpS4195 = _M0L6_2atmpS4196 + _M0L6_2atmpS4197;
      _M0L6_2atmpS4194 = _M0L1rS1330 * _M0L6_2atmpS4195;
      _M0L6_2atmpS4189 = _M0L6_2atmpS4193 + _M0L6_2atmpS4194;
      _M0L9syn__currS4192 = _M0L1pS1326->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4191
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS4192, _M0L1iS1337);
      _M0L6_2atmpS4190 = _M0L1rS1330 * _M0L6_2atmpS4191;
      _M0L6_2atmpS4188 = _M0L6_2atmpS4189 - _M0L6_2atmpS4190;
      _M0L6_2atmpS4186 = _M0L6_2atmpS4187 * _M0L6_2atmpS4188;
      _M0L6_2atmpS4184 = _M0L6_2atmpS4185 + _M0L6_2atmpS4186;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4183, _M0L1iS1337, _M0L6_2atmpS4184);
      _M0L4fireS4205 = _M0L1pS1326->$5;
      _M0L1vS4208 = _M0L1pS1326->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4207 = _M0MPC15array5Array2atGfE(_M0L1vS4208, _M0L1iS1337);
      _M0L6_2atmpS4206 = _M0L6_2atmpS4207 > _M0L2vtS1331;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4205, _M0L1iS1337, _M0L6_2atmpS4206);
      _M0L1vS4209 = _M0L1pS1326->$3;
      _M0L4fireS4211 = _M0L1pS1326->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4211, _M0L1iS1337)) {
        _M0L6_2atmpS4210 = _M0L2vrS1332;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4212 = _M0L1pS1326->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS4210
        = _M0MPC15array5Array2atGfE(_M0L1vS4212, _M0L1iS1337);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4209, _M0L1iS1337, _M0L6_2atmpS4210);
      _M0L4tabsS4213 = _M0L1pS1326->$6;
      _M0L4fireS4215 = _M0L1pS1326->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4215, _M0L1iS1337)) {
        _M0L6_2atmpS4214 = _M0L11tabs__stepsS1334;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4216 = _M0L1pS1326->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS4214
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4216, _M0L1iS1337);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4213, _M0L1iS1337, _M0L6_2atmpS4214);
      goto join_1338;
      goto joinlet_5932;
      join_1338:;
      _M0L6_2atmpS4175 = _M0L1iS1337 + 1;
      _M0L1iS1337 = _M0L6_2atmpS4175;
      continue;
      joinlet_5932:;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS1313,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1316,
  struct _M0TPB5ArrayGfE* _M0L7post__gS1322
) {
  int32_t _M0L4rowsS1312;
  int32_t _M0L7_2abindS1314;
  int32_t _M0L1iS1315;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS1312 = _M0L1mS1313->$0;
  _M0L7_2abindS1314 = 0;
  _M0L1iS1315 = _M0L7_2abindS1314;
  while (1) {
    if (_M0L1iS1315 < _M0L4rowsS1312) {
      int32_t _M0L6_2atmpS4174;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1316, _M0L1iS1315)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS4173 = _M0L1mS1313->$2;
        int32_t _M0L5startS1317;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS4171;
        int32_t _M0L6_2atmpS4172;
        int32_t _M0L3endS1318;
        int32_t _M0L1kS1319;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS1317
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS4173, _M0L1iS1315);
        _M0L6rowptrS4171 = _M0L1mS1313->$2;
        _M0L6_2atmpS4172 = _M0L1iS1315 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS1318
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS4171, _M0L6_2atmpS4172);
        _M0L1kS1319 = _M0L5startS1317;
        while (1) {
          if (_M0L1kS1319 < _M0L3endS1318) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS4169 = _M0L1mS1313->$3;
            int32_t _M0L9post__idxS1320;
            struct _M0TPB5ArrayGfE* _M0L4valsS4168;
            float _M0L1wS1321;
            float _M0L6_2atmpS4167;
            float _M0L6_2atmpS4166;
            int32_t _M0L6_2atmpS4170;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS1320
            = _M0MPC15array5Array2atGiE(_M0L6colptrS4169, _M0L1kS1319);
            _M0L4valsS4168 = _M0L1mS1313->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS1321
            = _M0MPC15array5Array2atGfE(_M0L4valsS4168, _M0L1kS1319);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS4167
            = _M0MPC15array5Array2atGfE(_M0L7post__gS1322, _M0L9post__idxS1320);
            _M0L6_2atmpS4166 = _M0L6_2atmpS4167 + _M0L1wS1321;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS1322, _M0L9post__idxS1320, _M0L6_2atmpS4166);
            _M0L6_2atmpS4170 = _M0L1kS1319 + 1;
            _M0L1kS1319 = _M0L6_2atmpS4170;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS4174 = _M0L1iS1315 + 1;
      _M0L1iS1315 = _M0L6_2atmpS4174;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3set(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS1299,
  int32_t _M0L1iS1298,
  int32_t _M0L1jS1304,
  float _M0L1vS1305
) {
  int32_t _if__result_5935;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS4165;
  int32_t _M0L5startS1300;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS4163;
  int32_t _M0L6_2atmpS4164;
  int32_t _M0L3endS1301;
  struct _M0TPB8MutLocalGbE* _M0L5foundS1302;
  int32_t _M0L1kS1303;
  int32_t _M0L3valS4155;
  #line 258 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  if (_M0L1iS1298 < 0) {
    _if__result_5935 = 1;
  } else {
    int32_t _M0L4rowsS4150 = _M0L1mS1299->$0;
    _if__result_5935 = _M0L1iS1298 >= _M0L4rowsS4150;
  }
  if (_if__result_5935) {
    return 0;
  }
  _M0L6rowptrS4165 = _M0L1mS1299->$2;
  #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5startS1300 = _M0MPC15array5Array2atGiE(_M0L6rowptrS4165, _M0L1iS1298);
  _M0L6rowptrS4163 = _M0L1mS1299->$2;
  _M0L6_2atmpS4164 = _M0L1iS1298 + 1;
  #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L3endS1301
  = _M0MPC15array5Array2atGiE(_M0L6rowptrS4163, _M0L6_2atmpS4164);
  _M0L5foundS1302
  = (struct _M0TPB8MutLocalGbE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGbE));
  Moonbit_object_header(_M0L5foundS1302)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5foundS1302->$0 = 0;
  _M0L1kS1303 = _M0L5startS1300;
  while (1) {
    if (_M0L1kS1303 < _M0L3endS1301) {
      struct _M0TPB5ArrayGiE* _M0L6colptrS4152 = _M0L1mS1299->$3;
      int32_t _M0L6_2atmpS4151;
      int32_t _M0L6_2atmpS4154;
      #line 267 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS4151
      = _M0MPC15array5Array2atGiE(_M0L6colptrS4152, _M0L1kS1303);
      if (_M0L6_2atmpS4151 == _M0L1jS1304) {
        struct _M0TPB5ArrayGfE* _M0L4valsS4153 = _M0L1mS1299->$4;
        #line 268 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0MPC15array5Array3setGfE(_M0L4valsS4153, _M0L1kS1303, _M0L1vS1305);
        _M0L5foundS1302->$0 = 1;
        break;
      }
      _M0L6_2atmpS4154 = _M0L1kS1303 + 1;
      _M0L1kS1303 = _M0L6_2atmpS4154;
      continue;
    }
    break;
  }
  _M0L3valS4155 = _M0L5foundS1302->$0;
  moonbit_decref_cycle_free(_M0L5foundS1302);
  if (!_M0L3valS4155) {
    struct _M0TPB5ArrayGiE* _M0L6colptrS4156 = _M0L1mS1299->$3;
    struct _M0TPB5ArrayGfE* _M0L4valsS4157;
    int32_t _M0L7n__rowsS1307;
    int32_t _M0L7_2abindS1308;
    int32_t _M0L7_2abindS1309;
    int32_t _M0L1rS1310;
    #line 275 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
    _M0MPC15array5Array6insertGiE(_M0L6colptrS4156, _M0L3endS1301, _M0L1jS1304);
    _M0L4valsS4157 = _M0L1mS1299->$4;
    #line 276 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
    _M0MPC15array5Array6insertGfE(_M0L4valsS4157, _M0L3endS1301, _M0L1vS1305);
    _M0L7n__rowsS1307 = _M0L1mS1299->$0;
    _M0L7_2abindS1308 = _M0L1iS1298 + 1;
    _M0L7_2abindS1309 = _M0L7n__rowsS1307 + 1;
    _M0L1rS1310 = _M0L7_2abindS1308;
    while (1) {
      if (_M0L1rS1310 < _M0L7_2abindS1309) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS4158 = _M0L1mS1299->$2;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS4161 = _M0L1mS1299->$2;
        int32_t _M0L6_2atmpS4160;
        int32_t _M0L6_2atmpS4159;
        int32_t _M0L6_2atmpS4162;
        #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L6_2atmpS4160
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS4161, _M0L1rS1310);
        _M0L6_2atmpS4159 = _M0L6_2atmpS4160 + 1;
        #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0MPC15array5Array3setGiE(_M0L6rowptrS4158, _M0L1rS1310, _M0L6_2atmpS4159);
        _M0L6_2atmpS4162 = _M0L1rS1310 + 1;
        _M0L1rS1310 = _M0L6_2atmpS4162;
        continue;
      }
      break;
    }
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR5empty(
  int32_t _M0L4rowsS1296,
  int32_t _M0L4colsS1297
) {
  int32_t _M0L6_2atmpS4149;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1295;
  int32_t* _M0L6_2atmpS4148;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS4145;
  float* _M0L6_2atmpS4147;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4146;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_5938;
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS4149 = _M0L4rowsS1296 + 1;
  #line 41 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS1295 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS4149, 0);
  _M0L6_2atmpS4148 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS4145
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS4145)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _M0L6_2atmpS4145->$0 = _M0L6_2atmpS4148;
  _M0L6_2atmpS4145->$1 = 0;
  _M0L6_2atmpS4147 = moonbit_empty_float_array;
  _M0L6_2atmpS4146
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4146)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS4146->$0 = _M0L6_2atmpS4147;
  _M0L6_2atmpS4146->$1 = 0;
  _block_5938
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_5938)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 75, 0);
  _block_5938->$0 = _M0L4rowsS1296;
  _block_5938->$1 = _M0L4colsS1297;
  _block_5938->$2 = _M0L6rowptrS1295;
  _block_5938->$3 = _M0L6_2atmpS4145;
  _block_5938->$4 = _M0L6_2atmpS4146;
  return _block_5938;
}

int32_t _M0FP26RiantR8snn__mbt20ca__plasticity__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1273,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1260,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1262,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1272,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1268,
  struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L4varsS1257,
  struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L5paramS1264,
  float _M0L6t__nowS1258,
  float _M0L2dtS1285
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS4036;
  int32_t _M0L6_2atmpS4035;
  int32_t _if__result_5939;
  int32_t _M0L6n__preS1259;
  int32_t _M0L7n__postS1261;
  float _M0L8tau__preS4144;
  float _M0L13inv__tau__preS1263;
  float _M0L9tau__postS4143;
  float _M0L14inv__tau__postS1265;
  struct _M0TPB8MutLocalGiE* _M0L1jS1266;
  struct _M0TPB8MutLocalGiE* _M0L1kS1276;
  struct _M0TPB8MutLocalGiE* _M0L2jjS1284;
  struct _M0TPB8MutLocalGiE* _M0L2iiS1287;
  struct _M0TPB8MutLocalGiE* _M0L3jj2S1289;
  struct _M0TPB8MutLocalGiE* _M0L3ii2S1291;
  struct _M0TPB8MutLocalGiE* _M0L2s2S1293;
  #line 1495 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6activeS4036 = _M0L4varsS1257->$4;
  #line 1507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS4035 = _M0MPC15array5Array6lengthGbE(_M0L6activeS4036);
  if (_M0L6_2atmpS4035 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS4034 = _M0L4varsS1257->$4;
    int32_t _M0L6_2atmpS4033;
    #line 1507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS4033 = _M0MPC15array5Array2atGbE(_M0L6activeS4034, 0);
    _if__result_5939 = !_M0L6_2atmpS4033;
  } else {
    _if__result_5939 = 0;
  }
  if (_if__result_5939) {
    return 0;
  }
  #line 1509 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1259 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1260);
  #line 1510 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1261 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1262);
  _M0L8tau__preS4144 = _M0L5paramS1264->$2;
  _M0L13inv__tau__preS1263 = 0x1p+0f / _M0L8tau__preS4144;
  _M0L9tau__postS4143 = _M0L5paramS1264->$3;
  _M0L14inv__tau__postS1265 = 0x1p+0f / _M0L9tau__postS4143;
  _M0L1jS1266
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1266)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1266->$0 = 0;
  while (1) {
    int32_t _M0L3valS4037 = _M0L1jS1266->$0;
    if (_M0L3valS4037 < _M0L6n__preS1259) {
      int32_t _M0L3valS4038 = _M0L1jS1266->$0;
      int32_t _M0L3valS4053;
      int32_t _M0L6_2atmpS4052;
      #line 1516 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1260, _M0L3valS4038)) {
        int32_t _M0L3valS4051 = _M0L1jS1266->$0;
        int32_t _M0L5startS1267;
        int32_t _M0L3valS4050;
        int32_t _M0L6_2atmpS4049;
        int32_t _M0L5end__S1269;
        struct _M0TPB8MutLocalGiE* _M0L1sS1270;
        #line 1517 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS1267
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1268, _M0L3valS4051);
        _M0L3valS4050 = _M0L1jS1266->$0;
        _M0L6_2atmpS4049 = _M0L3valS4050 + 1;
        #line 1518 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5end__S1269
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1268, _M0L6_2atmpS4049);
        _M0L1sS1270
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS1270)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS1270->$0 = _M0L5startS1267;
        while (1) {
          int32_t _M0L3valS4039 = _M0L1sS1270->$0;
          if (_M0L3valS4039 < _M0L5end__S1269) {
            int32_t _M0L3valS4048 = _M0L1sS1270->$0;
            int32_t _M0L1iS1271;
            int32_t _M0L3valS4040;
            int32_t _M0L3valS4045;
            float _M0L6_2atmpS4042;
            struct _M0TPB5ArrayGfE* _M0L5tpostS4044;
            float _M0L6_2atmpS4043;
            float _M0L6_2atmpS4041;
            int32_t _M0L3valS4047;
            int32_t _M0L6_2atmpS4046;
            #line 1521 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L1iS1271
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1272, _M0L3valS4048);
            _M0L3valS4040 = _M0L1sS1270->$0;
            _M0L3valS4045 = _M0L1sS1270->$0;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS4042
            = _M0MPC15array5Array2atGfE(_M0L1wS1273, _M0L3valS4045);
            _M0L5tpostS4044 = _M0L4varsS1257->$3;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS4043
            = _M0MPC15array5Array2atGfE(_M0L5tpostS4044, _M0L1iS1271);
            _M0L6_2atmpS4041 = _M0L6_2atmpS4042 + _M0L6_2atmpS4043;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1273, _M0L3valS4040, _M0L6_2atmpS4041);
            _M0L3valS4047 = _M0L1sS1270->$0;
            _M0L6_2atmpS4046 = _M0L3valS4047 + 1;
            _M0L1sS1270->$0 = _M0L6_2atmpS4046;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS1270);
          }
          break;
        }
      }
      _M0L3valS4053 = _M0L1jS1266->$0;
      _M0L6_2atmpS4052 = _M0L3valS4053 + 1;
      _M0L1jS1266->$0 = _M0L6_2atmpS4052;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1266);
    }
    break;
  }
  _M0L1kS1276
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS1276)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS1276->$0 = 0;
  while (1) {
    int32_t _M0L3valS4054 = _M0L1kS1276->$0;
    if (_M0L3valS4054 < _M0L7n__postS1261) {
      int32_t _M0L3valS4055 = _M0L1kS1276->$0;
      int32_t _M0L3valS4076;
      int32_t _M0L6_2atmpS4075;
      #line 1531 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1262, _M0L3valS4055)) {
        struct _M0TPB8MutLocalGiE* _M0L2j2S1277 =
          (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L2j2S1277)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L2j2S1277->$0 = 0;
        while (1) {
          int32_t _M0L3valS4056 = _M0L2j2S1277->$0;
          if (_M0L3valS4056 < _M0L6n__preS1259) {
            int32_t _M0L3valS4074 = _M0L2j2S1277->$0;
            int32_t _M0L5startS1278;
            int32_t _M0L3valS4073;
            int32_t _M0L6_2atmpS4072;
            int32_t _M0L5end__S1279;
            struct _M0TPB8MutLocalGiE* _M0L1sS1280;
            int32_t _M0L3valS4071;
            int32_t _M0L6_2atmpS4070;
            #line 1537 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L5startS1278
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS1268, _M0L3valS4074);
            _M0L3valS4073 = _M0L2j2S1277->$0;
            _M0L6_2atmpS4072 = _M0L3valS4073 + 1;
            #line 1538 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L5end__S1279
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS1268, _M0L6_2atmpS4072);
            _M0L1sS1280
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS1280)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS1280->$0 = _M0L5startS1278;
            while (1) {
              int32_t _M0L3valS4057 = _M0L1sS1280->$0;
              if (_M0L3valS4057 < _M0L5end__S1279) {
                int32_t _M0L3valS4060 = _M0L1sS1280->$0;
                int32_t _M0L6_2atmpS4058;
                int32_t _M0L3valS4059;
                int32_t _M0L3valS4069;
                int32_t _M0L6_2atmpS4068;
                #line 1541 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                _M0L6_2atmpS4058
                = _M0MPC15array5Array2atGiE(_M0L6colptrS1272, _M0L3valS4060);
                _M0L3valS4059 = _M0L1kS1276->$0;
                if (_M0L6_2atmpS4058 == _M0L3valS4059) {
                  int32_t _M0L3valS4061 = _M0L1sS1280->$0;
                  int32_t _M0L3valS4067 = _M0L1sS1280->$0;
                  float _M0L6_2atmpS4063;
                  struct _M0TPB5ArrayGfE* _M0L4tpreS4065;
                  int32_t _M0L3valS4066;
                  float _M0L6_2atmpS4064;
                  float _M0L6_2atmpS4062;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0L6_2atmpS4063
                  = _M0MPC15array5Array2atGfE(_M0L1wS1273, _M0L3valS4067);
                  _M0L4tpreS4065 = _M0L4varsS1257->$2;
                  _M0L3valS4066 = _M0L2j2S1277->$0;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0L6_2atmpS4064
                  = _M0MPC15array5Array2atGfE(_M0L4tpreS4065, _M0L3valS4066);
                  _M0L6_2atmpS4062 = _M0L6_2atmpS4063 + _M0L6_2atmpS4064;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0MPC15array5Array3setGfE(_M0L1wS1273, _M0L3valS4061, _M0L6_2atmpS4062);
                }
                _M0L3valS4069 = _M0L1sS1280->$0;
                _M0L6_2atmpS4068 = _M0L3valS4069 + 1;
                _M0L1sS1280->$0 = _M0L6_2atmpS4068;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS1280);
              }
              break;
            }
            _M0L3valS4071 = _M0L2j2S1277->$0;
            _M0L6_2atmpS4070 = _M0L3valS4071 + 1;
            _M0L2j2S1277->$0 = _M0L6_2atmpS4070;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L2j2S1277);
          }
          break;
        }
      }
      _M0L3valS4076 = _M0L1kS1276->$0;
      _M0L6_2atmpS4075 = _M0L3valS4076 + 1;
      _M0L1kS1276->$0 = _M0L6_2atmpS4075;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS1276);
    }
    break;
  }
  _M0L2jjS1284
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2jjS1284)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2jjS1284->$0 = 0;
  while (1) {
    int32_t _M0L3valS4077 = _M0L2jjS1284->$0;
    if (_M0L3valS4077 < _M0L6n__preS1259) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS4078 = _M0L4varsS1257->$2;
      int32_t _M0L3valS4079 = _M0L2jjS1284->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4088 = _M0L4varsS1257->$2;
      int32_t _M0L3valS4089 = _M0L2jjS1284->$0;
      float _M0L6_2atmpS4081;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4086;
      int32_t _M0L3valS4087;
      float _M0L6_2atmpS4085;
      float _M0L6_2atmpS4084;
      float _M0L6_2atmpS4083;
      float _M0L6_2atmpS4082;
      float _M0L6_2atmpS4080;
      int32_t _M0L3valS4091;
      int32_t _M0L6_2atmpS4090;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4081
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4088, _M0L3valS4089);
      _M0L4tpreS4086 = _M0L4varsS1257->$2;
      _M0L3valS4087 = _M0L2jjS1284->$0;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4085
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4086, _M0L3valS4087);
      _M0L6_2atmpS4084 = -_M0L6_2atmpS4085;
      _M0L6_2atmpS4083 = _M0L2dtS1285 * _M0L6_2atmpS4084;
      _M0L6_2atmpS4082 = _M0L6_2atmpS4083 * _M0L13inv__tau__preS1263;
      _M0L6_2atmpS4080 = _M0L6_2atmpS4081 + _M0L6_2atmpS4082;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS4078, _M0L3valS4079, _M0L6_2atmpS4080);
      _M0L3valS4091 = _M0L2jjS1284->$0;
      _M0L6_2atmpS4090 = _M0L3valS4091 + 1;
      _M0L2jjS1284->$0 = _M0L6_2atmpS4090;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2jjS1284);
    }
    break;
  }
  _M0L2iiS1287
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2iiS1287)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2iiS1287->$0 = 0;
  while (1) {
    int32_t _M0L3valS4092 = _M0L2iiS1287->$0;
    if (_M0L3valS4092 < _M0L7n__postS1261) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS4093 = _M0L4varsS1257->$3;
      int32_t _M0L3valS4094 = _M0L2iiS1287->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS4103 = _M0L4varsS1257->$3;
      int32_t _M0L3valS4104 = _M0L2iiS1287->$0;
      float _M0L6_2atmpS4096;
      struct _M0TPB5ArrayGfE* _M0L5tpostS4101;
      int32_t _M0L3valS4102;
      float _M0L6_2atmpS4100;
      float _M0L6_2atmpS4099;
      float _M0L6_2atmpS4098;
      float _M0L6_2atmpS4097;
      float _M0L6_2atmpS4095;
      int32_t _M0L3valS4106;
      int32_t _M0L6_2atmpS4105;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4096
      = _M0MPC15array5Array2atGfE(_M0L5tpostS4103, _M0L3valS4104);
      _M0L5tpostS4101 = _M0L4varsS1257->$3;
      _M0L3valS4102 = _M0L2iiS1287->$0;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4100
      = _M0MPC15array5Array2atGfE(_M0L5tpostS4101, _M0L3valS4102);
      _M0L6_2atmpS4099 = -_M0L6_2atmpS4100;
      _M0L6_2atmpS4098 = _M0L2dtS1285 * _M0L6_2atmpS4099;
      _M0L6_2atmpS4097 = _M0L6_2atmpS4098 * _M0L14inv__tau__postS1265;
      _M0L6_2atmpS4095 = _M0L6_2atmpS4096 + _M0L6_2atmpS4097;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS4093, _M0L3valS4094, _M0L6_2atmpS4095);
      _M0L3valS4106 = _M0L2iiS1287->$0;
      _M0L6_2atmpS4105 = _M0L3valS4106 + 1;
      _M0L2iiS1287->$0 = _M0L6_2atmpS4105;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2iiS1287);
    }
    break;
  }
  _M0L3jj2S1289
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3jj2S1289)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3jj2S1289->$0 = 0;
  while (1) {
    int32_t _M0L3valS4107 = _M0L3jj2S1289->$0;
    if (_M0L3valS4107 < _M0L6n__preS1259) {
      int32_t _M0L3valS4108 = _M0L3jj2S1289->$0;
      int32_t _M0L3valS4117;
      int32_t _M0L6_2atmpS4116;
      #line 1565 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1260, _M0L3valS4108)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS4109 = _M0L4varsS1257->$2;
        int32_t _M0L3valS4110 = _M0L3jj2S1289->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS4114 = _M0L4varsS1257->$2;
        int32_t _M0L3valS4115 = _M0L3jj2S1289->$0;
        float _M0L6_2atmpS4112;
        float _M0L6a__preS4113;
        float _M0L6_2atmpS4111;
        #line 1566 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS4112
        = _M0MPC15array5Array2atGfE(_M0L4tpreS4114, _M0L3valS4115);
        _M0L6a__preS4113 = _M0L5paramS1264->$0;
        _M0L6_2atmpS4111 = _M0L6_2atmpS4112 + _M0L6a__preS4113;
        #line 1566 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS4109, _M0L3valS4110, _M0L6_2atmpS4111);
      }
      _M0L3valS4117 = _M0L3jj2S1289->$0;
      _M0L6_2atmpS4116 = _M0L3valS4117 + 1;
      _M0L3jj2S1289->$0 = _M0L6_2atmpS4116;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L3jj2S1289);
    }
    break;
  }
  _M0L3ii2S1291
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3ii2S1291)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3ii2S1291->$0 = 0;
  while (1) {
    int32_t _M0L3valS4118 = _M0L3ii2S1291->$0;
    if (_M0L3valS4118 < _M0L7n__postS1261) {
      int32_t _M0L3valS4119 = _M0L3ii2S1291->$0;
      int32_t _M0L3valS4128;
      int32_t _M0L6_2atmpS4127;
      #line 1572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1262, _M0L3valS4119)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS4120 = _M0L4varsS1257->$3;
        int32_t _M0L3valS4121 = _M0L3ii2S1291->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS4125 = _M0L4varsS1257->$3;
        int32_t _M0L3valS4126 = _M0L3ii2S1291->$0;
        float _M0L6_2atmpS4123;
        float _M0L7a__postS4124;
        float _M0L6_2atmpS4122;
        #line 1573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS4123
        = _M0MPC15array5Array2atGfE(_M0L5tpostS4125, _M0L3valS4126);
        _M0L7a__postS4124 = _M0L5paramS1264->$1;
        _M0L6_2atmpS4122 = _M0L6_2atmpS4123 + _M0L7a__postS4124;
        #line 1573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS4120, _M0L3valS4121, _M0L6_2atmpS4122);
      }
      _M0L3valS4128 = _M0L3ii2S1291->$0;
      _M0L6_2atmpS4127 = _M0L3valS4128 + 1;
      _M0L3ii2S1291->$0 = _M0L6_2atmpS4127;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L3ii2S1291);
    }
    break;
  }
  _M0L2s2S1293
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S1293)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S1293->$0 = 0;
  while (1) {
    int32_t _M0L3valS4129 = _M0L2s2S1293->$0;
    int32_t _M0L6_2atmpS4130;
    #line 1579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS4130 = _M0MPC15array5Array6lengthGfE(_M0L1wS1273);
    if (_M0L3valS4129 < _M0L6_2atmpS4130) {
      int32_t _M0L3valS4133 = _M0L2s2S1293->$0;
      float _M0L6_2atmpS4131;
      float _M0L6w__minS4132;
      int32_t _M0L3valS4138;
      float _M0L6_2atmpS4136;
      float _M0L6w__maxS4137;
      int32_t _M0L3valS4142;
      int32_t _M0L6_2atmpS4141;
      #line 1580 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4131
      = _M0MPC15array5Array2atGfE(_M0L1wS1273, _M0L3valS4133);
      _M0L6w__minS4132 = _M0L5paramS1264->$5;
      if (_M0L6_2atmpS4131 < _M0L6w__minS4132) {
        int32_t _M0L3valS4134 = _M0L2s2S1293->$0;
        float _M0L6w__minS4135 = _M0L5paramS1264->$5;
        #line 1580 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1273, _M0L3valS4134, _M0L6w__minS4135);
      }
      _M0L3valS4138 = _M0L2s2S1293->$0;
      #line 1581 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4136
      = _M0MPC15array5Array2atGfE(_M0L1wS1273, _M0L3valS4138);
      _M0L6w__maxS4137 = _M0L5paramS1264->$4;
      if (_M0L6_2atmpS4136 > _M0L6w__maxS4137) {
        int32_t _M0L3valS4139 = _M0L2s2S1293->$0;
        float _M0L6w__maxS4140 = _M0L5paramS1264->$4;
        #line 1581 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1273, _M0L3valS4139, _M0L6w__maxS4140);
      }
      _M0L3valS4142 = _M0L2s2S1293->$0;
      _M0L6_2atmpS4141 = _M0L3valS4142 + 1;
      _M0L2s2S1293->$0 = _M0L6_2atmpS4141;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S1293);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt21stdp__symmetric__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1253,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1226,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1228,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1248,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1241,
  struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L4varsS1233,
  struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L5paramS1230,
  float _M0L6t__nowS1224,
  float _M0L2dtS1234
) {
  int32_t _M0L6n__preS1225;
  int32_t _M0L7n__postS1227;
  float _M0L6tau__xS4032;
  float _M0L11inv__tau__xS1229;
  float _M0L6tau__yS4031;
  float _M0L11inv__tau__yS1231;
  struct _M0TPB8MutLocalGiE* _M0L1jS1232;
  struct _M0TPB8MutLocalGiE* _M0L1iS1236;
  float _M0L4a__xS4028;
  float _M0L6tau__xS4030;
  float _M0L6_2atmpS4029;
  float _M0L7coef__xS1238;
  float _M0L4a__yS4025;
  float _M0L6tau__yS4027;
  float _M0L6_2atmpS4026;
  float _M0L7coef__yS1239;
  #line 1296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 1308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1225 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1226);
  #line 1309 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1227 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1228);
  _M0L6tau__xS4032 = _M0L5paramS1230->$2;
  _M0L11inv__tau__xS1229 = 0x1p+0f / _M0L6tau__xS4032;
  _M0L6tau__yS4031 = _M0L5paramS1230->$3;
  _M0L11inv__tau__yS1231 = 0x1p+0f / _M0L6tau__yS4031;
  _M0L1jS1232
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1232)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1232->$0 = 0;
  while (1) {
    int32_t _M0L3valS3902 = _M0L1jS1232->$0;
    if (_M0L3valS3902 < _M0L6n__preS1225) {
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3903 = _M0L4varsS1233->$0;
      int32_t _M0L3valS3904 = _M0L1jS1232->$0;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3913 = _M0L4varsS1233->$0;
      int32_t _M0L3valS3914 = _M0L1jS1232->$0;
      float _M0L6_2atmpS3906;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3911;
      int32_t _M0L3valS3912;
      float _M0L6_2atmpS3910;
      float _M0L6_2atmpS3909;
      float _M0L6_2atmpS3908;
      float _M0L6_2atmpS3907;
      float _M0L6_2atmpS3905;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3915;
      int32_t _M0L3valS3916;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3925;
      int32_t _M0L3valS3926;
      float _M0L6_2atmpS3918;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3923;
      int32_t _M0L3valS3924;
      float _M0L6_2atmpS3922;
      float _M0L6_2atmpS3921;
      float _M0L6_2atmpS3920;
      float _M0L6_2atmpS3919;
      float _M0L6_2atmpS3917;
      int32_t _M0L3valS3927;
      int32_t _M0L3valS3941;
      int32_t _M0L6_2atmpS3940;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3906
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3913, _M0L3valS3914);
      _M0L5tr__xS3911 = _M0L4varsS1233->$0;
      _M0L3valS3912 = _M0L1jS1232->$0;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3910
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3911, _M0L3valS3912);
      _M0L6_2atmpS3909 = -_M0L6_2atmpS3910;
      _M0L6_2atmpS3908 = _M0L2dtS1234 * _M0L6_2atmpS3909;
      _M0L6_2atmpS3907 = _M0L6_2atmpS3908 * _M0L11inv__tau__xS1229;
      _M0L6_2atmpS3905 = _M0L6_2atmpS3906 + _M0L6_2atmpS3907;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__xS3903, _M0L3valS3904, _M0L6_2atmpS3905);
      _M0L5tr__yS3915 = _M0L4varsS1233->$1;
      _M0L3valS3916 = _M0L1jS1232->$0;
      _M0L5tr__yS3925 = _M0L4varsS1233->$1;
      _M0L3valS3926 = _M0L1jS1232->$0;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3918
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS3925, _M0L3valS3926);
      _M0L5tr__yS3923 = _M0L4varsS1233->$1;
      _M0L3valS3924 = _M0L1jS1232->$0;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3922
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS3923, _M0L3valS3924);
      _M0L6_2atmpS3921 = -_M0L6_2atmpS3922;
      _M0L6_2atmpS3920 = _M0L2dtS1234 * _M0L6_2atmpS3921;
      _M0L6_2atmpS3919 = _M0L6_2atmpS3920 * _M0L11inv__tau__yS1231;
      _M0L6_2atmpS3917 = _M0L6_2atmpS3918 + _M0L6_2atmpS3919;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__yS3915, _M0L3valS3916, _M0L6_2atmpS3917);
      _M0L3valS3927 = _M0L1jS1232->$0;
      #line 1318 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1226, _M0L3valS3927)) {
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3928 = _M0L4varsS1233->$0;
        int32_t _M0L3valS3929 = _M0L1jS1232->$0;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3932 = _M0L4varsS1233->$0;
        int32_t _M0L3valS3933 = _M0L1jS1232->$0;
        float _M0L6_2atmpS3931;
        float _M0L6_2atmpS3930;
        struct _M0TPB5ArrayGfE* _M0L5tr__yS3934;
        int32_t _M0L3valS3935;
        struct _M0TPB5ArrayGfE* _M0L5tr__yS3938;
        int32_t _M0L3valS3939;
        float _M0L6_2atmpS3937;
        float _M0L6_2atmpS3936;
        #line 1319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3931
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3932, _M0L3valS3933);
        _M0L6_2atmpS3930 = _M0L6_2atmpS3931 + 0x1p+0f;
        #line 1319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__xS3928, _M0L3valS3929, _M0L6_2atmpS3930);
        _M0L5tr__yS3934 = _M0L4varsS1233->$1;
        _M0L3valS3935 = _M0L1jS1232->$0;
        _M0L5tr__yS3938 = _M0L4varsS1233->$1;
        _M0L3valS3939 = _M0L1jS1232->$0;
        #line 1320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3937
        = _M0MPC15array5Array2atGfE(_M0L5tr__yS3938, _M0L3valS3939);
        _M0L6_2atmpS3936 = _M0L6_2atmpS3937 + 0x1p+0f;
        #line 1320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__yS3934, _M0L3valS3935, _M0L6_2atmpS3936);
      }
      _M0L3valS3941 = _M0L1jS1232->$0;
      _M0L6_2atmpS3940 = _M0L3valS3941 + 1;
      _M0L1jS1232->$0 = _M0L6_2atmpS3940;
      continue;
    }
    break;
  }
  _M0L1iS1236
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1236)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1236->$0 = 0;
  while (1) {
    int32_t _M0L3valS3942 = _M0L1iS1236->$0;
    if (_M0L3valS3942 < _M0L7n__postS1227) {
      struct _M0TPB5ArrayGfE* _M0L5to__xS3943 = _M0L4varsS1233->$2;
      int32_t _M0L3valS3944 = _M0L1iS1236->$0;
      struct _M0TPB5ArrayGfE* _M0L5to__xS3953 = _M0L4varsS1233->$2;
      int32_t _M0L3valS3954 = _M0L1iS1236->$0;
      float _M0L6_2atmpS3946;
      struct _M0TPB5ArrayGfE* _M0L5to__xS3951;
      int32_t _M0L3valS3952;
      float _M0L6_2atmpS3950;
      float _M0L6_2atmpS3949;
      float _M0L6_2atmpS3948;
      float _M0L6_2atmpS3947;
      float _M0L6_2atmpS3945;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3955;
      int32_t _M0L3valS3956;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3965;
      int32_t _M0L3valS3966;
      float _M0L6_2atmpS3958;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3963;
      int32_t _M0L3valS3964;
      float _M0L6_2atmpS3962;
      float _M0L6_2atmpS3961;
      float _M0L6_2atmpS3960;
      float _M0L6_2atmpS3959;
      float _M0L6_2atmpS3957;
      int32_t _M0L3valS3967;
      int32_t _M0L3valS3981;
      int32_t _M0L6_2atmpS3980;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3946
      = _M0MPC15array5Array2atGfE(_M0L5to__xS3953, _M0L3valS3954);
      _M0L5to__xS3951 = _M0L4varsS1233->$2;
      _M0L3valS3952 = _M0L1iS1236->$0;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3950
      = _M0MPC15array5Array2atGfE(_M0L5to__xS3951, _M0L3valS3952);
      _M0L6_2atmpS3949 = -_M0L6_2atmpS3950;
      _M0L6_2atmpS3948 = _M0L2dtS1234 * _M0L6_2atmpS3949;
      _M0L6_2atmpS3947 = _M0L6_2atmpS3948 * _M0L11inv__tau__xS1229;
      _M0L6_2atmpS3945 = _M0L6_2atmpS3946 + _M0L6_2atmpS3947;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__xS3943, _M0L3valS3944, _M0L6_2atmpS3945);
      _M0L5to__yS3955 = _M0L4varsS1233->$3;
      _M0L3valS3956 = _M0L1iS1236->$0;
      _M0L5to__yS3965 = _M0L4varsS1233->$3;
      _M0L3valS3966 = _M0L1iS1236->$0;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3958
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3965, _M0L3valS3966);
      _M0L5to__yS3963 = _M0L4varsS1233->$3;
      _M0L3valS3964 = _M0L1iS1236->$0;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3962
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3963, _M0L3valS3964);
      _M0L6_2atmpS3961 = -_M0L6_2atmpS3962;
      _M0L6_2atmpS3960 = _M0L2dtS1234 * _M0L6_2atmpS3961;
      _M0L6_2atmpS3959 = _M0L6_2atmpS3960 * _M0L11inv__tau__yS1231;
      _M0L6_2atmpS3957 = _M0L6_2atmpS3958 + _M0L6_2atmpS3959;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__yS3955, _M0L3valS3956, _M0L6_2atmpS3957);
      _M0L3valS3967 = _M0L1iS1236->$0;
      #line 1328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1228, _M0L3valS3967)) {
        struct _M0TPB5ArrayGfE* _M0L5to__xS3968 = _M0L4varsS1233->$2;
        int32_t _M0L3valS3969 = _M0L1iS1236->$0;
        struct _M0TPB5ArrayGfE* _M0L5to__xS3972 = _M0L4varsS1233->$2;
        int32_t _M0L3valS3973 = _M0L1iS1236->$0;
        float _M0L6_2atmpS3971;
        float _M0L6_2atmpS3970;
        struct _M0TPB5ArrayGfE* _M0L5to__yS3974;
        int32_t _M0L3valS3975;
        struct _M0TPB5ArrayGfE* _M0L5to__yS3978;
        int32_t _M0L3valS3979;
        float _M0L6_2atmpS3977;
        float _M0L6_2atmpS3976;
        #line 1329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3971
        = _M0MPC15array5Array2atGfE(_M0L5to__xS3972, _M0L3valS3973);
        _M0L6_2atmpS3970 = _M0L6_2atmpS3971 + 0x1p+0f;
        #line 1329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__xS3968, _M0L3valS3969, _M0L6_2atmpS3970);
        _M0L5to__yS3974 = _M0L4varsS1233->$3;
        _M0L3valS3975 = _M0L1iS1236->$0;
        _M0L5to__yS3978 = _M0L4varsS1233->$3;
        _M0L3valS3979 = _M0L1iS1236->$0;
        #line 1330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3977
        = _M0MPC15array5Array2atGfE(_M0L5to__yS3978, _M0L3valS3979);
        _M0L6_2atmpS3976 = _M0L6_2atmpS3977 + 0x1p+0f;
        #line 1330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__yS3974, _M0L3valS3975, _M0L6_2atmpS3976);
      }
      _M0L3valS3981 = _M0L1iS1236->$0;
      _M0L6_2atmpS3980 = _M0L3valS3981 + 1;
      _M0L1iS1236->$0 = _M0L6_2atmpS3980;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1236);
    }
    break;
  }
  _M0L4a__xS4028 = _M0L5paramS1230->$0;
  _M0L6tau__xS4030 = _M0L5paramS1230->$2;
  _M0L6_2atmpS4029 = 0x1p+1f * _M0L6tau__xS4030;
  _M0L7coef__xS1238 = _M0L4a__xS4028 / _M0L6_2atmpS4029;
  _M0L4a__yS4025 = _M0L5paramS1230->$1;
  _M0L6tau__yS4027 = _M0L5paramS1230->$3;
  _M0L6_2atmpS4026 = 0x1p+1f * _M0L6tau__yS4027;
  _M0L7coef__yS1239 = _M0L4a__yS4025 / _M0L6_2atmpS4026;
  _M0L1jS1232->$0 = 0;
  while (1) {
    int32_t _M0L3valS3982 = _M0L1jS1232->$0;
    if (_M0L3valS3982 < _M0L6n__preS1225) {
      int32_t _M0L3valS4024 = _M0L1jS1232->$0;
      int32_t _M0L5startS1240;
      int32_t _M0L3valS4023;
      int32_t _M0L6_2atmpS4022;
      int32_t _M0L3endS1242;
      int32_t _M0L3valS4021;
      int32_t _M0L10pre__firedS1243;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS4019;
      int32_t _M0L3valS4020;
      float _M0L8tr__x__jS1244;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS4017;
      int32_t _M0L3valS4018;
      float _M0L8tr__y__jS1245;
      struct _M0TPB8MutLocalGiE* _M0L1sS1246;
      int32_t _M0L3valS4016;
      int32_t _M0L6_2atmpS4015;
      #line 1346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1240
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1241, _M0L3valS4024);
      _M0L3valS4023 = _M0L1jS1232->$0;
      _M0L6_2atmpS4022 = _M0L3valS4023 + 1;
      #line 1347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1242
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1241, _M0L6_2atmpS4022);
      _M0L3valS4021 = _M0L1jS1232->$0;
      #line 1348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L10pre__firedS1243
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1226, _M0L3valS4021);
      _M0L5tr__xS4019 = _M0L4varsS1233->$0;
      _M0L3valS4020 = _M0L1jS1232->$0;
      #line 1349 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8tr__x__jS1244
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS4019, _M0L3valS4020);
      _M0L5tr__yS4017 = _M0L4varsS1233->$1;
      _M0L3valS4018 = _M0L1jS1232->$0;
      #line 1350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8tr__y__jS1245
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS4017, _M0L3valS4018);
      _M0L1sS1246
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1246)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1246->$0 = _M0L5startS1240;
      while (1) {
        int32_t _M0L3valS3983 = _M0L1sS1246->$0;
        if (_M0L3valS3983 < _M0L3endS1242) {
          int32_t _M0L3valS4014 = _M0L1sS1246->$0;
          int32_t _M0L9post__idxS1247;
          int32_t _M0L11post__firedS1249;
          struct _M0TPB5ArrayGfE* _M0L5to__xS4013;
          float _M0L8to__x__iS1250;
          struct _M0TPB5ArrayGfE* _M0L5to__yS4012;
          float _M0L8to__y__iS1251;
          int32_t _M0L3valS4002;
          float _M0L6_2atmpS4000;
          float _M0L6w__minS4001;
          int32_t _M0L3valS4007;
          float _M0L6_2atmpS4005;
          float _M0L6w__maxS4006;
          int32_t _M0L3valS4011;
          int32_t _M0L6_2atmpS4010;
          #line 1353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1247
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1248, _M0L3valS4014);
          #line 1354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1249
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1228, _M0L9post__idxS1247);
          _M0L5to__xS4013 = _M0L4varsS1233->$2;
          #line 1355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8to__x__iS1250
          = _M0MPC15array5Array2atGfE(_M0L5to__xS4013, _M0L9post__idxS1247);
          _M0L5to__yS4012 = _M0L4varsS1233->$3;
          #line 1356 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8to__y__iS1251
          = _M0MPC15array5Array2atGfE(_M0L5to__yS4012, _M0L9post__idxS1247);
          if (_M0L10pre__firedS1243) {
            float _M0L10alpha__preS3990 = _M0L5paramS1230->$4;
            float _M0L6_2atmpS3991 = _M0L7coef__xS1238 * _M0L8to__x__iS1250;
            float _M0L6_2atmpS3988 = _M0L10alpha__preS3990 + _M0L6_2atmpS3991;
            float _M0L6_2atmpS3989 = _M0L7coef__yS1239 * _M0L8to__y__iS1251;
            float _M0L2dwS1252 = _M0L6_2atmpS3988 - _M0L6_2atmpS3989;
            int32_t _M0L3valS3984 = _M0L1sS1246->$0;
            int32_t _M0L3valS3987 = _M0L1sS1246->$0;
            float _M0L6_2atmpS3986;
            float _M0L6_2atmpS3985;
            #line 1359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3986
            = _M0MPC15array5Array2atGfE(_M0L1wS1253, _M0L3valS3987);
            _M0L6_2atmpS3985 = _M0L6_2atmpS3986 + _M0L2dwS1252;
            #line 1359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1253, _M0L3valS3984, _M0L6_2atmpS3985);
          }
          if (_M0L11post__firedS1249) {
            float _M0L11alpha__postS3998 = _M0L5paramS1230->$5;
            float _M0L6_2atmpS3999 = _M0L7coef__xS1238 * _M0L8tr__x__jS1244;
            float _M0L6_2atmpS3996 =
              _M0L11alpha__postS3998 + _M0L6_2atmpS3999;
            float _M0L6_2atmpS3997 = _M0L7coef__yS1239 * _M0L8tr__y__jS1245;
            float _M0L2dwS1254 = _M0L6_2atmpS3996 - _M0L6_2atmpS3997;
            int32_t _M0L3valS3992 = _M0L1sS1246->$0;
            int32_t _M0L3valS3995 = _M0L1sS1246->$0;
            float _M0L6_2atmpS3994;
            float _M0L6_2atmpS3993;
            #line 1363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3994
            = _M0MPC15array5Array2atGfE(_M0L1wS1253, _M0L3valS3995);
            _M0L6_2atmpS3993 = _M0L6_2atmpS3994 + _M0L2dwS1254;
            #line 1363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1253, _M0L3valS3992, _M0L6_2atmpS3993);
          }
          _M0L3valS4002 = _M0L1sS1246->$0;
          #line 1365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS4000
          = _M0MPC15array5Array2atGfE(_M0L1wS1253, _M0L3valS4002);
          _M0L6w__minS4001 = _M0L5paramS1230->$7;
          if (_M0L6_2atmpS4000 < _M0L6w__minS4001) {
            int32_t _M0L3valS4003 = _M0L1sS1246->$0;
            float _M0L6w__minS4004 = _M0L5paramS1230->$7;
            #line 1365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1253, _M0L3valS4003, _M0L6w__minS4004);
          }
          _M0L3valS4007 = _M0L1sS1246->$0;
          #line 1366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS4005
          = _M0MPC15array5Array2atGfE(_M0L1wS1253, _M0L3valS4007);
          _M0L6w__maxS4006 = _M0L5paramS1230->$6;
          if (_M0L6_2atmpS4005 > _M0L6w__maxS4006) {
            int32_t _M0L3valS4008 = _M0L1sS1246->$0;
            float _M0L6w__maxS4009 = _M0L5paramS1230->$6;
            #line 1366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1253, _M0L3valS4008, _M0L6w__maxS4009);
          }
          _M0L3valS4011 = _M0L1sS1246->$0;
          _M0L6_2atmpS4010 = _M0L3valS4011 + 1;
          _M0L1sS1246->$0 = _M0L6_2atmpS4010;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1246);
        }
        break;
      }
      _M0L3valS4016 = _M0L1jS1232->$0;
      _M0L6_2atmpS4015 = _M0L3valS4016 + 1;
      _M0L1jS1232->$0 = _M0L6_2atmpS4015;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1232);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22stdp__confavreux__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1221,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1198,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1200,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1218,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1212,
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS1206,
  struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L5paramS1203,
  float _M0L6t__nowS1207,
  float _M0L2dtS1202
) {
  int32_t _M0L6n__preS1197;
  int32_t _M0L7n__postS1199;
  float _M0L6_2atmpS3900;
  float _M0L8tau__preS3901;
  float _M0L6_2atmpS3899;
  float _M0L10decay__preS1201;
  float _M0L6_2atmpS3897;
  float _M0L9tau__postS3898;
  float _M0L6_2atmpS3896;
  float _M0L11decay__postS1204;
  struct _M0TPB8MutLocalGiE* _M0L1jS1205;
  struct _M0TPB8MutLocalGiE* _M0L1iS1209;
  #line 1080 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 1091 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1197 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1198);
  #line 1092 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1199 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1200);
  _M0L6_2atmpS3900 = -_M0L2dtS1202;
  _M0L8tau__preS3901 = _M0L5paramS1203->$5;
  _M0L6_2atmpS3899 = _M0L6_2atmpS3900 / _M0L8tau__preS3901;
  #line 1093 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10decay__preS1201 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3899);
  _M0L6_2atmpS3897 = -_M0L2dtS1202;
  _M0L9tau__postS3898 = _M0L5paramS1203->$6;
  _M0L6_2atmpS3896 = _M0L6_2atmpS3897 / _M0L9tau__postS3898;
  #line 1094 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L11decay__postS1204 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3896);
  _M0L1jS1205
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1205)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1205->$0 = 0;
  while (1) {
    int32_t _M0L3valS3816 = _M0L1jS1205->$0;
    if (_M0L3valS3816 < _M0L6n__preS1197) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS3817 = _M0L4varsS1206->$0;
      int32_t _M0L3valS3818 = _M0L1jS1205->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3821 = _M0L4varsS1206->$0;
      int32_t _M0L3valS3822 = _M0L1jS1205->$0;
      float _M0L6_2atmpS3820;
      float _M0L6_2atmpS3819;
      int32_t _M0L3valS3823;
      int32_t _M0L3valS3833;
      int32_t _M0L6_2atmpS3832;
      #line 1098 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3820
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3821, _M0L3valS3822);
      _M0L6_2atmpS3819 = _M0L6_2atmpS3820 * _M0L10decay__preS1201;
      #line 1098 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS3817, _M0L3valS3818, _M0L6_2atmpS3819);
      _M0L3valS3823 = _M0L1jS1205->$0;
      #line 1099 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1198, _M0L3valS3823)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS3824 = _M0L4varsS1206->$0;
        int32_t _M0L3valS3825 = _M0L1jS1205->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS3828 = _M0L4varsS1206->$0;
        int32_t _M0L3valS3829 = _M0L1jS1205->$0;
        float _M0L6_2atmpS3827;
        float _M0L6_2atmpS3826;
        struct _M0TPB5ArrayGfE* _M0L9last__preS3830;
        int32_t _M0L3valS3831;
        #line 1100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3827
        = _M0MPC15array5Array2atGfE(_M0L4tpreS3828, _M0L3valS3829);
        _M0L6_2atmpS3826 = _M0L6_2atmpS3827 + 0x1p+0f;
        #line 1100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS3824, _M0L3valS3825, _M0L6_2atmpS3826);
        _M0L9last__preS3830 = _M0L4varsS1206->$2;
        _M0L3valS3831 = _M0L1jS1205->$0;
        #line 1101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L9last__preS3830, _M0L3valS3831, _M0L6t__nowS1207);
      }
      _M0L3valS3833 = _M0L1jS1205->$0;
      _M0L6_2atmpS3832 = _M0L3valS3833 + 1;
      _M0L1jS1205->$0 = _M0L6_2atmpS3832;
      continue;
    }
    break;
  }
  _M0L1iS1209
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1209)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1209->$0 = 0;
  while (1) {
    int32_t _M0L3valS3834 = _M0L1iS1209->$0;
    if (_M0L3valS3834 < _M0L7n__postS1199) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS3835 = _M0L4varsS1206->$1;
      int32_t _M0L3valS3836 = _M0L1iS1209->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3839 = _M0L4varsS1206->$1;
      int32_t _M0L3valS3840 = _M0L1iS1209->$0;
      float _M0L6_2atmpS3838;
      float _M0L6_2atmpS3837;
      int32_t _M0L3valS3841;
      int32_t _M0L3valS3851;
      int32_t _M0L6_2atmpS3850;
      #line 1107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3838
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3839, _M0L3valS3840);
      _M0L6_2atmpS3837 = _M0L6_2atmpS3838 * _M0L11decay__postS1204;
      #line 1107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS3835, _M0L3valS3836, _M0L6_2atmpS3837);
      _M0L3valS3841 = _M0L1iS1209->$0;
      #line 1108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1200, _M0L3valS3841)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS3842 = _M0L4varsS1206->$1;
        int32_t _M0L3valS3843 = _M0L1iS1209->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS3846 = _M0L4varsS1206->$1;
        int32_t _M0L3valS3847 = _M0L1iS1209->$0;
        float _M0L6_2atmpS3845;
        float _M0L6_2atmpS3844;
        struct _M0TPB5ArrayGfE* _M0L10last__postS3848;
        int32_t _M0L3valS3849;
        #line 1109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3845
        = _M0MPC15array5Array2atGfE(_M0L5tpostS3846, _M0L3valS3847);
        _M0L6_2atmpS3844 = _M0L6_2atmpS3845 + 0x1p+0f;
        #line 1109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS3842, _M0L3valS3843, _M0L6_2atmpS3844);
        _M0L10last__postS3848 = _M0L4varsS1206->$3;
        _M0L3valS3849 = _M0L1iS1209->$0;
        #line 1110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L10last__postS3848, _M0L3valS3849, _M0L6t__nowS1207);
      }
      _M0L3valS3851 = _M0L1iS1209->$0;
      _M0L6_2atmpS3850 = _M0L3valS3851 + 1;
      _M0L1iS1209->$0 = _M0L6_2atmpS3850;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1209);
    }
    break;
  }
  _M0L1jS1205->$0 = 0;
  while (1) {
    int32_t _M0L3valS3852 = _M0L1jS1205->$0;
    if (_M0L3valS3852 < _M0L6n__preS1197) {
      int32_t _M0L3valS3895 = _M0L1jS1205->$0;
      int32_t _M0L5startS1211;
      int32_t _M0L3valS3894;
      int32_t _M0L6_2atmpS3893;
      int32_t _M0L3endS1213;
      int32_t _M0L3valS3892;
      int32_t _M0L10pre__firedS1214;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3890;
      int32_t _M0L3valS3891;
      float _M0L7tpre__jS1215;
      struct _M0TPB8MutLocalGiE* _M0L1sS1216;
      int32_t _M0L3valS3889;
      int32_t _M0L6_2atmpS3888;
      #line 1120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1211
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1212, _M0L3valS3895);
      _M0L3valS3894 = _M0L1jS1205->$0;
      _M0L6_2atmpS3893 = _M0L3valS3894 + 1;
      #line 1121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1213
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1212, _M0L6_2atmpS3893);
      _M0L3valS3892 = _M0L1jS1205->$0;
      #line 1122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L10pre__firedS1214
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1198, _M0L3valS3892);
      _M0L4tpreS3890 = _M0L4varsS1206->$0;
      _M0L3valS3891 = _M0L1jS1205->$0;
      #line 1123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L7tpre__jS1215
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3890, _M0L3valS3891);
      _M0L1sS1216
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1216)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1216->$0 = _M0L5startS1211;
      while (1) {
        int32_t _M0L3valS3853 = _M0L1sS1216->$0;
        if (_M0L3valS3853 < _M0L3endS1213) {
          int32_t _M0L3valS3887 = _M0L1sS1216->$0;
          int32_t _M0L9post__idxS1217;
          int32_t _M0L11post__firedS1219;
          struct _M0TPB5ArrayGfE* _M0L5tpostS3886;
          float _M0L8tpost__iS1220;
          int32_t _M0L3valS3876;
          float _M0L6_2atmpS3874;
          float _M0L6w__minS3875;
          int32_t _M0L3valS3881;
          float _M0L6_2atmpS3879;
          float _M0L6w__maxS3880;
          int32_t _M0L3valS3885;
          int32_t _M0L6_2atmpS3884;
          #line 1126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1217
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1218, _M0L3valS3887);
          #line 1127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1219
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1200, _M0L9post__idxS1217);
          _M0L5tpostS3886 = _M0L4varsS1206->$1;
          #line 1128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8tpost__iS1220
          = _M0MPC15array5Array2atGfE(_M0L5tpostS3886, _M0L9post__idxS1217);
          if (_M0L10pre__firedS1214) {
            int32_t _M0L3valS3854 = _M0L1sS1216->$0;
            int32_t _M0L3valS3863 = _M0L1sS1216->$0;
            float _M0L6_2atmpS3856;
            float _M0L3etaS3858;
            float _M0L5kappaS3862;
            float _M0L6_2atmpS3860;
            float _M0L5alphaS3861;
            float _M0L6_2atmpS3859;
            float _M0L6_2atmpS3857;
            float _M0L6_2atmpS3855;
            #line 1131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3856
            = _M0MPC15array5Array2atGfE(_M0L1wS1221, _M0L3valS3863);
            _M0L3etaS3858 = _M0L5paramS1203->$0;
            _M0L5kappaS3862 = _M0L5paramS1203->$3;
            _M0L6_2atmpS3860 = _M0L5kappaS3862 * _M0L8tpost__iS1220;
            _M0L5alphaS3861 = _M0L5paramS1203->$1;
            _M0L6_2atmpS3859 = _M0L6_2atmpS3860 + _M0L5alphaS3861;
            _M0L6_2atmpS3857 = _M0L3etaS3858 * _M0L6_2atmpS3859;
            _M0L6_2atmpS3855 = _M0L6_2atmpS3856 + _M0L6_2atmpS3857;
            #line 1131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1221, _M0L3valS3854, _M0L6_2atmpS3855);
          }
          if (_M0L11post__firedS1219) {
            int32_t _M0L3valS3864 = _M0L1sS1216->$0;
            int32_t _M0L3valS3873 = _M0L1sS1216->$0;
            float _M0L6_2atmpS3866;
            float _M0L3etaS3868;
            float _M0L5gammaS3872;
            float _M0L6_2atmpS3870;
            float _M0L4betaS3871;
            float _M0L6_2atmpS3869;
            float _M0L6_2atmpS3867;
            float _M0L6_2atmpS3865;
            #line 1135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3866
            = _M0MPC15array5Array2atGfE(_M0L1wS1221, _M0L3valS3873);
            _M0L3etaS3868 = _M0L5paramS1203->$0;
            _M0L5gammaS3872 = _M0L5paramS1203->$4;
            _M0L6_2atmpS3870 = _M0L5gammaS3872 * _M0L7tpre__jS1215;
            _M0L4betaS3871 = _M0L5paramS1203->$2;
            _M0L6_2atmpS3869 = _M0L6_2atmpS3870 + _M0L4betaS3871;
            _M0L6_2atmpS3867 = _M0L3etaS3868 * _M0L6_2atmpS3869;
            _M0L6_2atmpS3865 = _M0L6_2atmpS3866 + _M0L6_2atmpS3867;
            #line 1135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1221, _M0L3valS3864, _M0L6_2atmpS3865);
          }
          _M0L3valS3876 = _M0L1sS1216->$0;
          #line 1138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3874
          = _M0MPC15array5Array2atGfE(_M0L1wS1221, _M0L3valS3876);
          _M0L6w__minS3875 = _M0L5paramS1203->$8;
          if (_M0L6_2atmpS3874 < _M0L6w__minS3875) {
            int32_t _M0L3valS3877 = _M0L1sS1216->$0;
            float _M0L6w__minS3878 = _M0L5paramS1203->$8;
            #line 1138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1221, _M0L3valS3877, _M0L6w__minS3878);
          }
          _M0L3valS3881 = _M0L1sS1216->$0;
          #line 1139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3879
          = _M0MPC15array5Array2atGfE(_M0L1wS1221, _M0L3valS3881);
          _M0L6w__maxS3880 = _M0L5paramS1203->$7;
          if (_M0L6_2atmpS3879 > _M0L6w__maxS3880) {
            int32_t _M0L3valS3882 = _M0L1sS1216->$0;
            float _M0L6w__maxS3883 = _M0L5paramS1203->$7;
            #line 1139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1221, _M0L3valS3882, _M0L6w__maxS3883);
          }
          _M0L3valS3885 = _M0L1sS1216->$0;
          _M0L6_2atmpS3884 = _M0L3valS3885 + 1;
          _M0L1sS1216->$0 = _M0L6_2atmpS3884;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1216);
        }
        break;
      }
      _M0L3valS3889 = _M0L1jS1205->$0;
      _M0L6_2atmpS3888 = _M0L3valS3889 + 1;
      _M0L1jS1205->$0 = _M0L6_2atmpS3888;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1205);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt10stdp__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1194,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1174,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1176,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1191,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1187,
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS1172,
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS1179,
  float _M0L6t__nowS1182,
  float _M0L2dtS1178
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3733;
  int32_t _M0L6_2atmpS3732;
  int32_t _if__result_5958;
  int32_t _M0L6n__preS1173;
  int32_t _M0L7n__postS1175;
  float _M0L6_2atmpS3814;
  float _M0L8tau__preS3815;
  float _M0L6_2atmpS3813;
  float _M0L10decay__preS1177;
  float _M0L6_2atmpS3811;
  float _M0L9tau__postS3812;
  float _M0L6_2atmpS3810;
  float _M0L11decay__postS1180;
  struct _M0TPB8MutLocalGiE* _M0L1jS1181;
  struct _M0TPB8MutLocalGiE* _M0L1iS1184;
  #line 905 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6activeS3733 = _M0L4varsS1172->$4;
  #line 917 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3732 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3733);
  if (_M0L6_2atmpS3732 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3731 = _M0L4varsS1172->$4;
    int32_t _M0L6_2atmpS3730;
    #line 917 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS3730 = _M0MPC15array5Array2atGbE(_M0L6activeS3731, 0);
    _if__result_5958 = !_M0L6_2atmpS3730;
  } else {
    _if__result_5958 = 0;
  }
  if (_if__result_5958) {
    return 0;
  }
  #line 921 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1173 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1174);
  #line 922 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1175 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1176);
  _M0L6_2atmpS3814 = -_M0L2dtS1178;
  _M0L8tau__preS3815 = _M0L5paramS1179->$2;
  _M0L6_2atmpS3813 = _M0L6_2atmpS3814 / _M0L8tau__preS3815;
  #line 923 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10decay__preS1177 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3813);
  _M0L6_2atmpS3811 = -_M0L2dtS1178;
  _M0L9tau__postS3812 = _M0L5paramS1179->$3;
  _M0L6_2atmpS3810 = _M0L6_2atmpS3811 / _M0L9tau__postS3812;
  #line 924 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L11decay__postS1180 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3810);
  _M0L1jS1181
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1181)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1181->$0 = 0;
  while (1) {
    int32_t _M0L3valS3734 = _M0L1jS1181->$0;
    if (_M0L3valS3734 < _M0L6n__preS1173) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS3735 = _M0L4varsS1172->$0;
      int32_t _M0L3valS3736 = _M0L1jS1181->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3739 = _M0L4varsS1172->$0;
      int32_t _M0L3valS3740 = _M0L1jS1181->$0;
      float _M0L6_2atmpS3738;
      float _M0L6_2atmpS3737;
      int32_t _M0L3valS3741;
      int32_t _M0L3valS3752;
      int32_t _M0L6_2atmpS3751;
      #line 927 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3738
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3739, _M0L3valS3740);
      _M0L6_2atmpS3737 = _M0L6_2atmpS3738 * _M0L10decay__preS1177;
      #line 927 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS3735, _M0L3valS3736, _M0L6_2atmpS3737);
      _M0L3valS3741 = _M0L1jS1181->$0;
      #line 928 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1174, _M0L3valS3741)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS3742 = _M0L4varsS1172->$0;
        int32_t _M0L3valS3743 = _M0L1jS1181->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS3747 = _M0L4varsS1172->$0;
        int32_t _M0L3valS3748 = _M0L1jS1181->$0;
        float _M0L6_2atmpS3745;
        float _M0L6a__preS3746;
        float _M0L6_2atmpS3744;
        struct _M0TPB5ArrayGfE* _M0L9last__preS3749;
        int32_t _M0L3valS3750;
        #line 929 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3745
        = _M0MPC15array5Array2atGfE(_M0L4tpreS3747, _M0L3valS3748);
        _M0L6a__preS3746 = _M0L5paramS1179->$0;
        _M0L6_2atmpS3744 = _M0L6_2atmpS3745 + _M0L6a__preS3746;
        #line 929 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS3742, _M0L3valS3743, _M0L6_2atmpS3744);
        _M0L9last__preS3749 = _M0L4varsS1172->$2;
        _M0L3valS3750 = _M0L1jS1181->$0;
        #line 930 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L9last__preS3749, _M0L3valS3750, _M0L6t__nowS1182);
      }
      _M0L3valS3752 = _M0L1jS1181->$0;
      _M0L6_2atmpS3751 = _M0L3valS3752 + 1;
      _M0L1jS1181->$0 = _M0L6_2atmpS3751;
      continue;
    }
    break;
  }
  _M0L1iS1184
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1184)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1184->$0 = 0;
  while (1) {
    int32_t _M0L3valS3753 = _M0L1iS1184->$0;
    if (_M0L3valS3753 < _M0L7n__postS1175) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS3754 = _M0L4varsS1172->$1;
      int32_t _M0L3valS3755 = _M0L1iS1184->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3758 = _M0L4varsS1172->$1;
      int32_t _M0L3valS3759 = _M0L1iS1184->$0;
      float _M0L6_2atmpS3757;
      float _M0L6_2atmpS3756;
      int32_t _M0L3valS3760;
      int32_t _M0L3valS3771;
      int32_t _M0L6_2atmpS3770;
      #line 936 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3757
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3758, _M0L3valS3759);
      _M0L6_2atmpS3756 = _M0L6_2atmpS3757 * _M0L11decay__postS1180;
      #line 936 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS3754, _M0L3valS3755, _M0L6_2atmpS3756);
      _M0L3valS3760 = _M0L1iS1184->$0;
      #line 937 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1176, _M0L3valS3760)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS3761 = _M0L4varsS1172->$1;
        int32_t _M0L3valS3762 = _M0L1iS1184->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS3766 = _M0L4varsS1172->$1;
        int32_t _M0L3valS3767 = _M0L1iS1184->$0;
        float _M0L6_2atmpS3764;
        float _M0L7a__postS3765;
        float _M0L6_2atmpS3763;
        struct _M0TPB5ArrayGfE* _M0L10last__postS3768;
        int32_t _M0L3valS3769;
        #line 938 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3764
        = _M0MPC15array5Array2atGfE(_M0L5tpostS3766, _M0L3valS3767);
        _M0L7a__postS3765 = _M0L5paramS1179->$1;
        _M0L6_2atmpS3763 = _M0L6_2atmpS3764 + _M0L7a__postS3765;
        #line 938 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS3761, _M0L3valS3762, _M0L6_2atmpS3763);
        _M0L10last__postS3768 = _M0L4varsS1172->$3;
        _M0L3valS3769 = _M0L1iS1184->$0;
        #line 939 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L10last__postS3768, _M0L3valS3769, _M0L6t__nowS1182);
      }
      _M0L3valS3771 = _M0L1iS1184->$0;
      _M0L6_2atmpS3770 = _M0L3valS3771 + 1;
      _M0L1iS1184->$0 = _M0L6_2atmpS3770;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1184);
    }
    break;
  }
  _M0L1jS1181->$0 = 0;
  while (1) {
    int32_t _M0L3valS3772 = _M0L1jS1181->$0;
    if (_M0L3valS3772 < _M0L6n__preS1173) {
      int32_t _M0L3valS3809 = _M0L1jS1181->$0;
      int32_t _M0L5startS1186;
      int32_t _M0L3valS3808;
      int32_t _M0L6_2atmpS3807;
      int32_t _M0L3endS1188;
      struct _M0TPB8MutLocalGiE* _M0L1sS1189;
      int32_t _M0L3valS3806;
      int32_t _M0L6_2atmpS3805;
      #line 947 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1186
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1187, _M0L3valS3809);
      _M0L3valS3808 = _M0L1jS1181->$0;
      _M0L6_2atmpS3807 = _M0L3valS3808 + 1;
      #line 948 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1188
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1187, _M0L6_2atmpS3807);
      _M0L1sS1189
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1189)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1189->$0 = _M0L5startS1186;
      while (1) {
        int32_t _M0L3valS3773 = _M0L1sS1189->$0;
        if (_M0L3valS3773 < _M0L3endS1188) {
          int32_t _M0L3valS3804 = _M0L1sS1189->$0;
          int32_t _M0L9post__idxS1190;
          int32_t _M0L3valS3803;
          int32_t _M0L10pre__firedS1192;
          int32_t _M0L11post__firedS1193;
          int32_t _M0L3valS3793;
          float _M0L6_2atmpS3791;
          float _M0L6w__minS3792;
          int32_t _M0L3valS3798;
          float _M0L6_2atmpS3796;
          float _M0L6w__maxS3797;
          int32_t _M0L3valS3802;
          int32_t _M0L6_2atmpS3801;
          #line 951 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1190
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1191, _M0L3valS3804);
          _M0L3valS3803 = _M0L1jS1181->$0;
          #line 952 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L10pre__firedS1192
          = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1174, _M0L3valS3803);
          #line 953 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1193
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1176, _M0L9post__idxS1190);
          if (_M0L10pre__firedS1192) {
            int32_t _M0L3valS3774 = _M0L1sS1189->$0;
            int32_t _M0L3valS3781 = _M0L1sS1189->$0;
            float _M0L6_2atmpS3776;
            float _M0L7a__postS3778;
            struct _M0TPB5ArrayGfE* _M0L5tpostS3780;
            float _M0L6_2atmpS3779;
            float _M0L6_2atmpS3777;
            float _M0L6_2atmpS3775;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3776
            = _M0MPC15array5Array2atGfE(_M0L1wS1194, _M0L3valS3781);
            _M0L7a__postS3778 = _M0L5paramS1179->$1;
            _M0L5tpostS3780 = _M0L4varsS1172->$1;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3779
            = _M0MPC15array5Array2atGfE(_M0L5tpostS3780, _M0L9post__idxS1190);
            _M0L6_2atmpS3777 = _M0L7a__postS3778 * _M0L6_2atmpS3779;
            _M0L6_2atmpS3775 = _M0L6_2atmpS3776 + _M0L6_2atmpS3777;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1194, _M0L3valS3774, _M0L6_2atmpS3775);
          }
          if (_M0L11post__firedS1193) {
            int32_t _M0L3valS3782 = _M0L1sS1189->$0;
            int32_t _M0L3valS3790 = _M0L1sS1189->$0;
            float _M0L6_2atmpS3784;
            float _M0L6a__preS3786;
            struct _M0TPB5ArrayGfE* _M0L4tpreS3788;
            int32_t _M0L3valS3789;
            float _M0L6_2atmpS3787;
            float _M0L6_2atmpS3785;
            float _M0L6_2atmpS3783;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3784
            = _M0MPC15array5Array2atGfE(_M0L1wS1194, _M0L3valS3790);
            _M0L6a__preS3786 = _M0L5paramS1179->$0;
            _M0L4tpreS3788 = _M0L4varsS1172->$0;
            _M0L3valS3789 = _M0L1jS1181->$0;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3787
            = _M0MPC15array5Array2atGfE(_M0L4tpreS3788, _M0L3valS3789);
            _M0L6_2atmpS3785 = _M0L6a__preS3786 * _M0L6_2atmpS3787;
            _M0L6_2atmpS3783 = _M0L6_2atmpS3784 + _M0L6_2atmpS3785;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1194, _M0L3valS3782, _M0L6_2atmpS3783);
          }
          _M0L3valS3793 = _M0L1sS1189->$0;
          #line 963 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3791
          = _M0MPC15array5Array2atGfE(_M0L1wS1194, _M0L3valS3793);
          _M0L6w__minS3792 = _M0L5paramS1179->$5;
          if (_M0L6_2atmpS3791 < _M0L6w__minS3792) {
            int32_t _M0L3valS3794 = _M0L1sS1189->$0;
            float _M0L6w__minS3795 = _M0L5paramS1179->$5;
            #line 963 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1194, _M0L3valS3794, _M0L6w__minS3795);
          }
          _M0L3valS3798 = _M0L1sS1189->$0;
          #line 964 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3796
          = _M0MPC15array5Array2atGfE(_M0L1wS1194, _M0L3valS3798);
          _M0L6w__maxS3797 = _M0L5paramS1179->$4;
          if (_M0L6_2atmpS3796 > _M0L6w__maxS3797) {
            int32_t _M0L3valS3799 = _M0L1sS1189->$0;
            float _M0L6w__maxS3800 = _M0L5paramS1179->$4;
            #line 964 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1194, _M0L3valS3799, _M0L6w__maxS3800);
          }
          _M0L3valS3802 = _M0L1sS1189->$0;
          _M0L6_2atmpS3801 = _M0L3valS3802 + 1;
          _M0L1sS1189->$0 = _M0L6_2atmpS3801;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1189);
        }
        break;
      }
      _M0L3valS3806 = _M0L1jS1181->$0;
      _M0L6_2atmpS3805 = _M0L3valS3806 + 1;
      _M0L1jS1181->$0 = _M0L6_2atmpS3805;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1181);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25stdp__antisymmetric__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1151,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1141,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1143,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1150,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1146,
  struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L4varsS1153,
  struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L5paramS1152,
  float _M0L2dtS1165
) {
  int32_t _M0L6n__preS1140;
  int32_t _M0L7n__postS1142;
  struct _M0TPB8MutLocalGiE* _M0L1jS1144;
  int32_t _M0L3nnzS1156;
  float _M0L4a__xS3728;
  float _M0L6tau__xS3729;
  float _M0L18a__x__over__tau__xS1157;
  struct _M0TPB8MutLocalGiE* _M0L2s2S1158;
  float _M0L6tau__xS3727;
  float _M0L11inv__tau__xS1162;
  float _M0L6tau__yS3726;
  float _M0L11inv__tau__yS1163;
  struct _M0TPB8MutLocalGiE* _M0L1iS1164;
  struct _M0TPB8MutLocalGiE* _M0L2s3S1170;
  #line 622 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 632 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1140 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1141);
  #line 633 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1142 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1143);
  _M0L1jS1144
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1144)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1144->$0 = 0;
  while (1) {
    int32_t _M0L3valS3626 = _M0L1jS1144->$0;
    if (_M0L3valS3626 < _M0L6n__preS1140) {
      int32_t _M0L3valS3627 = _M0L1jS1144->$0;
      int32_t _M0L3valS3648;
      int32_t _M0L6_2atmpS3647;
      #line 637 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1141, _M0L3valS3627)) {
        int32_t _M0L3valS3646 = _M0L1jS1144->$0;
        int32_t _M0L5startS1145;
        int32_t _M0L3valS3645;
        int32_t _M0L6_2atmpS3644;
        int32_t _M0L3endS1147;
        struct _M0TPB8MutLocalGiE* _M0L1sS1148;
        #line 638 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS1145
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1146, _M0L3valS3646);
        _M0L3valS3645 = _M0L1jS1144->$0;
        _M0L6_2atmpS3644 = _M0L3valS3645 + 1;
        #line 639 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3endS1147
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1146, _M0L6_2atmpS3644);
        _M0L1sS1148
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS1148)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS1148->$0 = _M0L5startS1145;
        while (1) {
          int32_t _M0L3valS3628 = _M0L1sS1148->$0;
          if (_M0L3valS3628 < _M0L3endS1147) {
            int32_t _M0L3valS3643 = _M0L1sS1148->$0;
            int32_t _M0L9post__idxS1149;
            int32_t _M0L3valS3629;
            int32_t _M0L3valS3640;
            float _M0L6_2atmpS3638;
            float _M0L10alpha__preS3639;
            float _M0L6_2atmpS3631;
            float _M0L4a__yS3636;
            float _M0L6tau__yS3637;
            float _M0L6_2atmpS3633;
            struct _M0TPB5ArrayGfE* _M0L5to__yS3635;
            float _M0L6_2atmpS3634;
            float _M0L6_2atmpS3632;
            float _M0L6_2atmpS3630;
            int32_t _M0L3valS3642;
            int32_t _M0L6_2atmpS3641;
            #line 642 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L9post__idxS1149
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1150, _M0L3valS3643);
            _M0L3valS3629 = _M0L1sS1148->$0;
            _M0L3valS3640 = _M0L1sS1148->$0;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3638
            = _M0MPC15array5Array2atGfE(_M0L1wS1151, _M0L3valS3640);
            _M0L10alpha__preS3639 = _M0L5paramS1152->$4;
            _M0L6_2atmpS3631 = _M0L6_2atmpS3638 + _M0L10alpha__preS3639;
            _M0L4a__yS3636 = _M0L5paramS1152->$1;
            _M0L6tau__yS3637 = _M0L5paramS1152->$3;
            _M0L6_2atmpS3633 = _M0L4a__yS3636 / _M0L6tau__yS3637;
            _M0L5to__yS3635 = _M0L4varsS1153->$1;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3634
            = _M0MPC15array5Array2atGfE(_M0L5to__yS3635, _M0L9post__idxS1149);
            _M0L6_2atmpS3632 = _M0L6_2atmpS3633 * _M0L6_2atmpS3634;
            _M0L6_2atmpS3630 = _M0L6_2atmpS3631 - _M0L6_2atmpS3632;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1151, _M0L3valS3629, _M0L6_2atmpS3630);
            _M0L3valS3642 = _M0L1sS1148->$0;
            _M0L6_2atmpS3641 = _M0L3valS3642 + 1;
            _M0L1sS1148->$0 = _M0L6_2atmpS3641;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS1148);
          }
          break;
        }
      }
      _M0L3valS3648 = _M0L1jS1144->$0;
      _M0L6_2atmpS3647 = _M0L3valS3648 + 1;
      _M0L1jS1144->$0 = _M0L6_2atmpS3647;
      continue;
    }
    break;
  }
  #line 650 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L3nnzS1156 = _M0MPC15array5Array6lengthGfE(_M0L1wS1151);
  _M0L4a__xS3728 = _M0L5paramS1152->$0;
  _M0L6tau__xS3729 = _M0L5paramS1152->$2;
  _M0L18a__x__over__tau__xS1157 = _M0L4a__xS3728 / _M0L6tau__xS3729;
  _M0L2s2S1158
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S1158)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S1158->$0 = 0;
  while (1) {
    int32_t _M0L3valS3649 = _M0L2s2S1158->$0;
    if (_M0L3valS3649 < _M0L3nnzS1156) {
      int32_t _M0L3valS3662 = _M0L2s2S1158->$0;
      int32_t _M0L9post__idxS1159;
      int32_t _M0L3valS3661;
      int32_t _M0L6_2atmpS3660;
      #line 654 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L9post__idxS1159
      = _M0MPC15array5Array2atGiE(_M0L6colptrS1150, _M0L3valS3662);
      #line 655 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (
        _M0MPC15array5Array2atGbE(_M0L10post__fireS1143, _M0L9post__idxS1159)
      ) {
        int32_t _M0L3valS3659 = _M0L2s2S1158->$0;
        int32_t _M0L6j__preS1160;
        int32_t _M0L3valS3650;
        int32_t _M0L3valS3658;
        float _M0L6_2atmpS3656;
        float _M0L11alpha__postS3657;
        float _M0L6_2atmpS3652;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3655;
        float _M0L6_2atmpS3654;
        float _M0L6_2atmpS3653;
        float _M0L6_2atmpS3651;
        #line 656 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6j__preS1160
        = _M0FP26RiantR8snn__mbt20find__pre__for__conn(_M0L6rowptrS1146, _M0L3valS3659);
        _M0L3valS3650 = _M0L2s2S1158->$0;
        _M0L3valS3658 = _M0L2s2S1158->$0;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3656
        = _M0MPC15array5Array2atGfE(_M0L1wS1151, _M0L3valS3658);
        _M0L11alpha__postS3657 = _M0L5paramS1152->$5;
        _M0L6_2atmpS3652 = _M0L6_2atmpS3656 + _M0L11alpha__postS3657;
        _M0L5tr__xS3655 = _M0L4varsS1153->$0;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3654
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3655, _M0L6j__preS1160);
        _M0L6_2atmpS3653 = _M0L18a__x__over__tau__xS1157 * _M0L6_2atmpS3654;
        _M0L6_2atmpS3651 = _M0L6_2atmpS3652 + _M0L6_2atmpS3653;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1151, _M0L3valS3650, _M0L6_2atmpS3651);
      }
      _M0L3valS3661 = _M0L2s2S1158->$0;
      _M0L6_2atmpS3660 = _M0L3valS3661 + 1;
      _M0L2s2S1158->$0 = _M0L6_2atmpS3660;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S1158);
    }
    break;
  }
  _M0L6tau__xS3727 = _M0L5paramS1152->$2;
  _M0L11inv__tau__xS1162 = 0x1p+0f / _M0L6tau__xS3727;
  _M0L6tau__yS3726 = _M0L5paramS1152->$3;
  _M0L11inv__tau__yS1163 = 0x1p+0f / _M0L6tau__yS3726;
  _M0L1iS1164
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1164)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1164->$0 = 0;
  while (1) {
    int32_t _M0L3valS3663 = _M0L1iS1164->$0;
    if (_M0L3valS3663 < _M0L7n__postS1142) {
      struct _M0TPB5ArrayGfE* _M0L5to__yS3664 = _M0L4varsS1153->$1;
      int32_t _M0L3valS3665 = _M0L1iS1164->$0;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3674 = _M0L4varsS1153->$1;
      int32_t _M0L3valS3675 = _M0L1iS1164->$0;
      float _M0L6_2atmpS3667;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3672;
      int32_t _M0L3valS3673;
      float _M0L6_2atmpS3671;
      float _M0L6_2atmpS3670;
      float _M0L6_2atmpS3669;
      float _M0L6_2atmpS3668;
      float _M0L6_2atmpS3666;
      int32_t _M0L3valS3677;
      int32_t _M0L6_2atmpS3676;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3667
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3674, _M0L3valS3675);
      _M0L5to__yS3672 = _M0L4varsS1153->$1;
      _M0L3valS3673 = _M0L1iS1164->$0;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3671
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3672, _M0L3valS3673);
      _M0L6_2atmpS3670 = -_M0L6_2atmpS3671;
      _M0L6_2atmpS3669 = _M0L2dtS1165 * _M0L6_2atmpS3670;
      _M0L6_2atmpS3668 = _M0L6_2atmpS3669 * _M0L11inv__tau__yS1163;
      _M0L6_2atmpS3666 = _M0L6_2atmpS3667 + _M0L6_2atmpS3668;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__yS3664, _M0L3valS3665, _M0L6_2atmpS3666);
      _M0L3valS3677 = _M0L1iS1164->$0;
      _M0L6_2atmpS3676 = _M0L3valS3677 + 1;
      _M0L1iS1164->$0 = _M0L6_2atmpS3676;
      continue;
    }
    break;
  }
  _M0L1jS1144->$0 = 0;
  while (1) {
    int32_t _M0L3valS3678 = _M0L1jS1144->$0;
    if (_M0L3valS3678 < _M0L6n__preS1140) {
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3679 = _M0L4varsS1153->$0;
      int32_t _M0L3valS3680 = _M0L1jS1144->$0;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3689 = _M0L4varsS1153->$0;
      int32_t _M0L3valS3690 = _M0L1jS1144->$0;
      float _M0L6_2atmpS3682;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3687;
      int32_t _M0L3valS3688;
      float _M0L6_2atmpS3686;
      float _M0L6_2atmpS3685;
      float _M0L6_2atmpS3684;
      float _M0L6_2atmpS3683;
      float _M0L6_2atmpS3681;
      int32_t _M0L3valS3692;
      int32_t _M0L6_2atmpS3691;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3682
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3689, _M0L3valS3690);
      _M0L5tr__xS3687 = _M0L4varsS1153->$0;
      _M0L3valS3688 = _M0L1jS1144->$0;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3686
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3687, _M0L3valS3688);
      _M0L6_2atmpS3685 = -_M0L6_2atmpS3686;
      _M0L6_2atmpS3684 = _M0L2dtS1165 * _M0L6_2atmpS3685;
      _M0L6_2atmpS3683 = _M0L6_2atmpS3684 * _M0L11inv__tau__xS1162;
      _M0L6_2atmpS3681 = _M0L6_2atmpS3682 + _M0L6_2atmpS3683;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__xS3679, _M0L3valS3680, _M0L6_2atmpS3681);
      _M0L3valS3692 = _M0L1jS1144->$0;
      _M0L6_2atmpS3691 = _M0L3valS3692 + 1;
      _M0L1jS1144->$0 = _M0L6_2atmpS3691;
      continue;
    }
    break;
  }
  _M0L1iS1164->$0 = 0;
  while (1) {
    int32_t _M0L3valS3693 = _M0L1iS1164->$0;
    if (_M0L3valS3693 < _M0L7n__postS1142) {
      int32_t _M0L3valS3694 = _M0L1iS1164->$0;
      int32_t _M0L3valS3702;
      int32_t _M0L6_2atmpS3701;
      #line 677 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1143, _M0L3valS3694)) {
        struct _M0TPB5ArrayGfE* _M0L5to__yS3695 = _M0L4varsS1153->$1;
        int32_t _M0L3valS3696 = _M0L1iS1164->$0;
        struct _M0TPB5ArrayGfE* _M0L5to__yS3699 = _M0L4varsS1153->$1;
        int32_t _M0L3valS3700 = _M0L1iS1164->$0;
        float _M0L6_2atmpS3698;
        float _M0L6_2atmpS3697;
        #line 678 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3698
        = _M0MPC15array5Array2atGfE(_M0L5to__yS3699, _M0L3valS3700);
        _M0L6_2atmpS3697 = _M0L6_2atmpS3698 + 0x1p+0f;
        #line 678 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__yS3695, _M0L3valS3696, _M0L6_2atmpS3697);
      }
      _M0L3valS3702 = _M0L1iS1164->$0;
      _M0L6_2atmpS3701 = _M0L3valS3702 + 1;
      _M0L1iS1164->$0 = _M0L6_2atmpS3701;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1164);
    }
    break;
  }
  _M0L1jS1144->$0 = 0;
  while (1) {
    int32_t _M0L3valS3703 = _M0L1jS1144->$0;
    if (_M0L3valS3703 < _M0L6n__preS1140) {
      int32_t _M0L3valS3704 = _M0L1jS1144->$0;
      int32_t _M0L3valS3712;
      int32_t _M0L6_2atmpS3711;
      #line 684 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1141, _M0L3valS3704)) {
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3705 = _M0L4varsS1153->$0;
        int32_t _M0L3valS3706 = _M0L1jS1144->$0;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3709 = _M0L4varsS1153->$0;
        int32_t _M0L3valS3710 = _M0L1jS1144->$0;
        float _M0L6_2atmpS3708;
        float _M0L6_2atmpS3707;
        #line 685 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3708
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3709, _M0L3valS3710);
        _M0L6_2atmpS3707 = _M0L6_2atmpS3708 + 0x1p+0f;
        #line 685 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__xS3705, _M0L3valS3706, _M0L6_2atmpS3707);
      }
      _M0L3valS3712 = _M0L1jS1144->$0;
      _M0L6_2atmpS3711 = _M0L3valS3712 + 1;
      _M0L1jS1144->$0 = _M0L6_2atmpS3711;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1144);
    }
    break;
  }
  _M0L2s3S1170
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s3S1170)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s3S1170->$0 = 0;
  while (1) {
    int32_t _M0L3valS3713 = _M0L2s3S1170->$0;
    if (_M0L3valS3713 < _M0L3nnzS1156) {
      int32_t _M0L3valS3716 = _M0L2s3S1170->$0;
      float _M0L6_2atmpS3714;
      float _M0L6w__minS3715;
      int32_t _M0L3valS3725;
      int32_t _M0L6_2atmpS3724;
      #line 692 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3714
      = _M0MPC15array5Array2atGfE(_M0L1wS1151, _M0L3valS3716);
      _M0L6w__minS3715 = _M0L5paramS1152->$7;
      if (_M0L6_2atmpS3714 < _M0L6w__minS3715) {
        int32_t _M0L3valS3717 = _M0L2s3S1170->$0;
        float _M0L6w__minS3718 = _M0L5paramS1152->$7;
        #line 693 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1151, _M0L3valS3717, _M0L6w__minS3718);
      } else {
        int32_t _M0L3valS3721 = _M0L2s3S1170->$0;
        float _M0L6_2atmpS3719;
        float _M0L6w__maxS3720;
        #line 694 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3719
        = _M0MPC15array5Array2atGfE(_M0L1wS1151, _M0L3valS3721);
        _M0L6w__maxS3720 = _M0L5paramS1152->$6;
        if (_M0L6_2atmpS3719 > _M0L6w__maxS3720) {
          int32_t _M0L3valS3722 = _M0L2s3S1170->$0;
          float _M0L6w__maxS3723 = _M0L5paramS1152->$6;
          #line 695 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0MPC15array5Array3setGfE(_M0L1wS1151, _M0L3valS3722, _M0L6w__maxS3723);
        }
      }
      _M0L3valS3725 = _M0L2s3S1170->$0;
      _M0L6_2atmpS3724 = _M0L3valS3725 + 1;
      _M0L2s3S1170->$0 = _M0L6_2atmpS3724;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s3S1170);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt24stdp__mexican__hat__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1126,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1102,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1104,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1121,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1117,
  struct _M0TPB5ArrayGfE* _M0L4tpreS1112,
  struct _M0TPB5ArrayGfE* _M0L5tpostS1108,
  struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L5paramS1106,
  float _M0L2dtS1109
) {
  int32_t _M0L6n__preS1101;
  int32_t _M0L7n__postS1103;
  float _M0L3tauS3625;
  float _M0L8inv__tauS1105;
  struct _M0TPB8MutLocalGiE* _M0L1iS1107;
  struct _M0TPB8MutLocalGiE* _M0L1jS1111;
  int32_t _M0L3nnzS1129;
  struct _M0TPB8MutLocalGiE* _M0L2s2S1130;
  struct _M0TPB8MutLocalGiE* _M0L2s3S1138;
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 461 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1101 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1102);
  #line 462 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1103 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1104);
  _M0L3tauS3625 = _M0L5paramS1106->$1;
  _M0L8inv__tauS1105 = 0x1p+0f / _M0L3tauS3625;
  _M0L1iS1107
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1107)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1107->$0 = 0;
  while (1) {
    int32_t _M0L3valS3539 = _M0L1iS1107->$0;
    if (_M0L3valS3539 < _M0L7n__postS1103) {
      int32_t _M0L3valS3540 = _M0L1iS1107->$0;
      int32_t _M0L3valS3548 = _M0L1iS1107->$0;
      float _M0L6_2atmpS3542;
      int32_t _M0L3valS3547;
      float _M0L6_2atmpS3546;
      float _M0L6_2atmpS3545;
      float _M0L6_2atmpS3544;
      float _M0L6_2atmpS3543;
      float _M0L6_2atmpS3541;
      int32_t _M0L3valS3550;
      int32_t _M0L6_2atmpS3549;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3542
      = _M0MPC15array5Array2atGfE(_M0L5tpostS1108, _M0L3valS3548);
      _M0L3valS3547 = _M0L1iS1107->$0;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3546
      = _M0MPC15array5Array2atGfE(_M0L5tpostS1108, _M0L3valS3547);
      _M0L6_2atmpS3545 = -_M0L6_2atmpS3546;
      _M0L6_2atmpS3544 = _M0L2dtS1109 * _M0L6_2atmpS3545;
      _M0L6_2atmpS3543 = _M0L6_2atmpS3544 * _M0L8inv__tauS1105;
      _M0L6_2atmpS3541 = _M0L6_2atmpS3542 + _M0L6_2atmpS3543;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS1108, _M0L3valS3540, _M0L6_2atmpS3541);
      _M0L3valS3550 = _M0L1iS1107->$0;
      _M0L6_2atmpS3549 = _M0L3valS3550 + 1;
      _M0L1iS1107->$0 = _M0L6_2atmpS3549;
      continue;
    }
    break;
  }
  _M0L1jS1111
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1111)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1111->$0 = 0;
  while (1) {
    int32_t _M0L3valS3551 = _M0L1jS1111->$0;
    if (_M0L3valS3551 < _M0L6n__preS1101) {
      int32_t _M0L3valS3552 = _M0L1jS1111->$0;
      int32_t _M0L3valS3560 = _M0L1jS1111->$0;
      float _M0L6_2atmpS3554;
      int32_t _M0L3valS3559;
      float _M0L6_2atmpS3558;
      float _M0L6_2atmpS3557;
      float _M0L6_2atmpS3556;
      float _M0L6_2atmpS3555;
      float _M0L6_2atmpS3553;
      int32_t _M0L3valS3562;
      int32_t _M0L6_2atmpS3561;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3554
      = _M0MPC15array5Array2atGfE(_M0L4tpreS1112, _M0L3valS3560);
      _M0L3valS3559 = _M0L1jS1111->$0;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3558
      = _M0MPC15array5Array2atGfE(_M0L4tpreS1112, _M0L3valS3559);
      _M0L6_2atmpS3557 = -_M0L6_2atmpS3558;
      _M0L6_2atmpS3556 = _M0L2dtS1109 * _M0L6_2atmpS3557;
      _M0L6_2atmpS3555 = _M0L6_2atmpS3556 * _M0L8inv__tauS1105;
      _M0L6_2atmpS3553 = _M0L6_2atmpS3554 + _M0L6_2atmpS3555;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS1112, _M0L3valS3552, _M0L6_2atmpS3553);
      _M0L3valS3562 = _M0L1jS1111->$0;
      _M0L6_2atmpS3561 = _M0L3valS3562 + 1;
      _M0L1jS1111->$0 = _M0L6_2atmpS3561;
      continue;
    }
    break;
  }
  _M0L1iS1107->$0 = 0;
  while (1) {
    int32_t _M0L3valS3563 = _M0L1iS1107->$0;
    if (_M0L3valS3563 < _M0L7n__postS1103) {
      int32_t _M0L3valS3564 = _M0L1iS1107->$0;
      int32_t _M0L3valS3570;
      int32_t _M0L6_2atmpS3569;
      #line 478 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1104, _M0L3valS3564)) {
        int32_t _M0L3valS3565 = _M0L1iS1107->$0;
        int32_t _M0L3valS3568 = _M0L1iS1107->$0;
        float _M0L6_2atmpS3567;
        float _M0L6_2atmpS3566;
        #line 479 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3567
        = _M0MPC15array5Array2atGfE(_M0L5tpostS1108, _M0L3valS3568);
        _M0L6_2atmpS3566 = _M0L6_2atmpS3567 + 0x1p+0f;
        #line 479 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS1108, _M0L3valS3565, _M0L6_2atmpS3566);
      }
      _M0L3valS3570 = _M0L1iS1107->$0;
      _M0L6_2atmpS3569 = _M0L3valS3570 + 1;
      _M0L1iS1107->$0 = _M0L6_2atmpS3569;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1107);
    }
    break;
  }
  _M0L1jS1111->$0 = 0;
  while (1) {
    int32_t _M0L3valS3571 = _M0L1jS1111->$0;
    if (_M0L3valS3571 < _M0L6n__preS1101) {
      int32_t _M0L3valS3572 = _M0L1jS1111->$0;
      int32_t _M0L3valS3578;
      int32_t _M0L6_2atmpS3577;
      #line 485 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1102, _M0L3valS3572)) {
        int32_t _M0L3valS3573 = _M0L1jS1111->$0;
        int32_t _M0L3valS3576 = _M0L1jS1111->$0;
        float _M0L6_2atmpS3575;
        float _M0L6_2atmpS3574;
        #line 486 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3575
        = _M0MPC15array5Array2atGfE(_M0L4tpreS1112, _M0L3valS3576);
        _M0L6_2atmpS3574 = _M0L6_2atmpS3575 + 0x1p+0f;
        #line 486 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS1112, _M0L3valS3573, _M0L6_2atmpS3574);
      }
      _M0L3valS3578 = _M0L1jS1111->$0;
      _M0L6_2atmpS3577 = _M0L3valS3578 + 1;
      _M0L1jS1111->$0 = _M0L6_2atmpS3577;
      continue;
    }
    break;
  }
  _M0L1jS1111->$0 = 0;
  while (1) {
    int32_t _M0L3valS3579 = _M0L1jS1111->$0;
    if (_M0L3valS3579 < _M0L6n__preS1101) {
      int32_t _M0L3valS3580 = _M0L1jS1111->$0;
      int32_t _M0L3valS3598;
      int32_t _M0L6_2atmpS3597;
      #line 493 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1102, _M0L3valS3580)) {
        int32_t _M0L3valS3596 = _M0L1jS1111->$0;
        int32_t _M0L5startS1116;
        int32_t _M0L3valS3595;
        int32_t _M0L6_2atmpS3594;
        int32_t _M0L3endS1118;
        struct _M0TPB8MutLocalGiE* _M0L1sS1119;
        #line 494 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS1116
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1117, _M0L3valS3596);
        _M0L3valS3595 = _M0L1jS1111->$0;
        _M0L6_2atmpS3594 = _M0L3valS3595 + 1;
        #line 495 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3endS1118
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1117, _M0L6_2atmpS3594);
        _M0L1sS1119
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS1119)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS1119->$0 = _M0L5startS1116;
        while (1) {
          int32_t _M0L3valS3581 = _M0L1sS1119->$0;
          if (_M0L3valS3581 < _M0L3endS1118) {
            int32_t _M0L3valS3593 = _M0L1sS1119->$0;
            int32_t _M0L9post__idxS1120;
            int32_t _M0L3valS3592;
            float _M0L6_2atmpS3590;
            float _M0L6_2atmpS3591;
            float _M0L5ratioS1122;
            float _M0L3lnxS1123;
            float _M0L1xS1124;
            float _M0L1aS3588;
            float _M0L6_2atmpS3589;
            float _M0L2dwS1125;
            int32_t _M0L3valS3582;
            int32_t _M0L3valS3585;
            float _M0L6_2atmpS3584;
            float _M0L6_2atmpS3583;
            int32_t _M0L3valS3587;
            int32_t _M0L6_2atmpS3586;
            #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L9post__idxS1120
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1121, _M0L3valS3593);
            _M0L3valS3592 = _M0L1jS1111->$0;
            #line 499 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3590
            = _M0MPC15array5Array2atGfE(_M0L4tpreS1112, _M0L3valS3592);
            #line 499 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3591
            = _M0MPC15array5Array2atGfE(_M0L5tpostS1108, _M0L9post__idxS1120);
            _M0L5ratioS1122 = _M0L6_2atmpS3590 / _M0L6_2atmpS3591;
            #line 500 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L3lnxS1123 = _M0FP26RiantR8snn__mbt4logf(_M0L5ratioS1122);
            _M0L1xS1124 = _M0L3lnxS1123 * _M0L3lnxS1123;
            _M0L1aS3588 = _M0L5paramS1106->$0;
            #line 502 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3589
            = _M0FP26RiantR8snn__mbt20mexican__hat__kernel(_M0L1xS1124);
            _M0L2dwS1125 = _M0L1aS3588 * _M0L6_2atmpS3589;
            _M0L3valS3582 = _M0L1sS1119->$0;
            _M0L3valS3585 = _M0L1sS1119->$0;
            #line 503 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3584
            = _M0MPC15array5Array2atGfE(_M0L1wS1126, _M0L3valS3585);
            _M0L6_2atmpS3583 = _M0L6_2atmpS3584 + _M0L2dwS1125;
            #line 503 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1126, _M0L3valS3582, _M0L6_2atmpS3583);
            _M0L3valS3587 = _M0L1sS1119->$0;
            _M0L6_2atmpS3586 = _M0L3valS3587 + 1;
            _M0L1sS1119->$0 = _M0L6_2atmpS3586;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS1119);
          }
          break;
        }
      }
      _M0L3valS3598 = _M0L1jS1111->$0;
      _M0L6_2atmpS3597 = _M0L3valS3598 + 1;
      _M0L1jS1111->$0 = _M0L6_2atmpS3597;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1111);
    }
    break;
  }
  #line 511 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L3nnzS1129 = _M0MPC15array5Array6lengthGfE(_M0L1wS1126);
  _M0L2s2S1130
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S1130)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S1130->$0 = 0;
  while (1) {
    int32_t _M0L3valS3599 = _M0L2s2S1130->$0;
    if (_M0L3valS3599 < _M0L3nnzS1129) {
      int32_t _M0L3valS3611 = _M0L2s2S1130->$0;
      int32_t _M0L9post__idxS1131;
      int32_t _M0L3valS3610;
      int32_t _M0L6_2atmpS3609;
      #line 514 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L9post__idxS1131
      = _M0MPC15array5Array2atGiE(_M0L6colptrS1121, _M0L3valS3611);
      #line 515 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (
        _M0MPC15array5Array2atGbE(_M0L10post__fireS1104, _M0L9post__idxS1131)
      ) {
        int32_t _M0L3valS3608 = _M0L2s2S1130->$0;
        int32_t _M0L6j__preS1132;
        float _M0L6_2atmpS3606;
        float _M0L6_2atmpS3607;
        float _M0L5ratioS1133;
        float _M0L3lnxS1134;
        float _M0L1xS1135;
        float _M0L1aS3604;
        float _M0L6_2atmpS3605;
        float _M0L2dwS1136;
        int32_t _M0L3valS3600;
        int32_t _M0L3valS3603;
        float _M0L6_2atmpS3602;
        float _M0L6_2atmpS3601;
        #line 518 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6j__preS1132
        = _M0FP26RiantR8snn__mbt20find__pre__for__conn(_M0L6rowptrS1117, _M0L3valS3608);
        #line 519 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3606
        = _M0MPC15array5Array2atGfE(_M0L4tpreS1112, _M0L6j__preS1132);
        #line 519 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3607
        = _M0MPC15array5Array2atGfE(_M0L5tpostS1108, _M0L9post__idxS1131);
        _M0L5ratioS1133 = _M0L6_2atmpS3606 / _M0L6_2atmpS3607;
        #line 520 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3lnxS1134 = _M0FP26RiantR8snn__mbt4logf(_M0L5ratioS1133);
        _M0L1xS1135 = _M0L3lnxS1134 * _M0L3lnxS1134;
        _M0L1aS3604 = _M0L5paramS1106->$0;
        #line 522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3605
        = _M0FP26RiantR8snn__mbt20mexican__hat__kernel(_M0L1xS1135);
        _M0L2dwS1136 = _M0L1aS3604 * _M0L6_2atmpS3605;
        _M0L3valS3600 = _M0L2s2S1130->$0;
        _M0L3valS3603 = _M0L2s2S1130->$0;
        #line 523 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3602
        = _M0MPC15array5Array2atGfE(_M0L1wS1126, _M0L3valS3603);
        _M0L6_2atmpS3601 = _M0L6_2atmpS3602 + _M0L2dwS1136;
        #line 523 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1126, _M0L3valS3600, _M0L6_2atmpS3601);
      }
      _M0L3valS3610 = _M0L2s2S1130->$0;
      _M0L6_2atmpS3609 = _M0L3valS3610 + 1;
      _M0L2s2S1130->$0 = _M0L6_2atmpS3609;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S1130);
    }
    break;
  }
  _M0L2s3S1138
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s3S1138)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s3S1138->$0 = 0;
  while (1) {
    int32_t _M0L3valS3612 = _M0L2s3S1138->$0;
    if (_M0L3valS3612 < _M0L3nnzS1129) {
      int32_t _M0L3valS3615 = _M0L2s3S1138->$0;
      float _M0L6_2atmpS3613;
      float _M0L6w__minS3614;
      int32_t _M0L3valS3624;
      int32_t _M0L6_2atmpS3623;
      #line 530 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3613
      = _M0MPC15array5Array2atGfE(_M0L1wS1126, _M0L3valS3615);
      _M0L6w__minS3614 = _M0L5paramS1106->$3;
      if (_M0L6_2atmpS3613 < _M0L6w__minS3614) {
        int32_t _M0L3valS3616 = _M0L2s3S1138->$0;
        float _M0L6w__minS3617 = _M0L5paramS1106->$3;
        #line 531 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1126, _M0L3valS3616, _M0L6w__minS3617);
      } else {
        int32_t _M0L3valS3620 = _M0L2s3S1138->$0;
        float _M0L6_2atmpS3618;
        float _M0L6w__maxS3619;
        #line 532 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3618
        = _M0MPC15array5Array2atGfE(_M0L1wS1126, _M0L3valS3620);
        _M0L6w__maxS3619 = _M0L5paramS1106->$2;
        if (_M0L6_2atmpS3618 > _M0L6w__maxS3619) {
          int32_t _M0L3valS3621 = _M0L2s3S1138->$0;
          float _M0L6w__maxS3622 = _M0L5paramS1106->$2;
          #line 533 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0MPC15array5Array3setGfE(_M0L1wS1126, _M0L3valS3621, _M0L6w__maxS3622);
        }
      }
      _M0L3valS3624 = _M0L2s3S1138->$0;
      _M0L6_2atmpS3623 = _M0L3valS3624 + 1;
      _M0L2s3S1138->$0 = _M0L6_2atmpS3623;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s3S1138);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20find__pre__for__conn(
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1095,
  int32_t _M0L1sS1099
) {
  int32_t _M0L6_2atmpS3538;
  int32_t _M0L1nS1094;
  struct _M0TPB8MutLocalGiE* _M0L2loS1096;
  struct _M0TPB8MutLocalGiE* _M0L2hiS1097;
  int32_t _result_5980;
  #line 542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 543 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3538 = _M0MPC15array5Array6lengthGiE(_M0L6rowptrS1095);
  _M0L1nS1094 = _M0L6_2atmpS3538 - 1;
  _M0L2loS1096
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2loS1096)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2loS1096->$0 = 0;
  _M0L2hiS1097
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2hiS1097)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2hiS1097->$0 = _M0L1nS1094;
  while (1) {
    int32_t _M0L3valS3530 = _M0L2loS1096->$0;
    int32_t _M0L3valS3531 = _M0L2hiS1097->$0;
    if (_M0L3valS3530 < _M0L3valS3531) {
      int32_t _M0L3valS3536 = _M0L2loS1096->$0;
      int32_t _M0L3valS3537 = _M0L2hiS1097->$0;
      int32_t _M0L6_2atmpS3535 = _M0L3valS3536 + _M0L3valS3537;
      int32_t _M0L6_2atmpS3534 = _M0L6_2atmpS3535 + 1;
      int32_t _M0L3midS1098 = _M0L6_2atmpS3534 / 2;
      int32_t _M0L6_2atmpS3532;
      #line 548 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3532
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1095, _M0L3midS1098);
      if (_M0L6_2atmpS3532 <= _M0L1sS1099) {
        _M0L2loS1096->$0 = _M0L3midS1098;
      } else {
        int32_t _M0L6_2atmpS3533 = _M0L3midS1098 - 1;
        _M0L2hiS1097->$0 = _M0L6_2atmpS3533;
      }
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2hiS1097);
    }
    break;
  }
  _result_5980 = _M0L2loS1096->$0;
  moonbit_decref_cycle_free(_M0L2loS1096);
  return _result_5980;
}

float _M0FP26RiantR8snn__mbt20mexican__hat__kernel(float _M0L1xS1091) {
  #line 427 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 428 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  if (_M0MPC15float5Float7is__nan(_M0L1xS1091)) {
    return 0x0p+0f;
  } else {
    float _M0L6_2atmpS3529 = -_M0L1xS1091;
    float _M0L3argS1092 = _M0L6_2atmpS3529 / 0x1.6a09e65dc27dfp+0f;
    float _M0L6_2atmpS3527 = 0x1p+0f - _M0L1xS1091;
    float _M0L6_2atmpS3528;
    float _M0L1vS1093;
    #line 432 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS3528 = _M0FP26RiantR8snn__mbt4expf(_M0L3argS1092);
    _M0L1vS1093 = _M0L6_2atmpS3527 * _M0L6_2atmpS3528;
    #line 433 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    if (_M0MPC15float5Float7is__nan(_M0L1vS1093)) {
      return 0x0p+0f;
    } else {
      return _M0L1vS1093;
    }
  }
}

struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0MP26RiantR8snn__mbt9STDPEntry3new(
  int32_t _M0L11conn__indexS1088,
  int32_t _M0L6n__preS1089,
  int32_t _M0L7n__postS1090
) {
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L6_2atmpS3523;
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L6_2atmpS3524;
  float* _M0L6_2atmpS3526;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3525;
  struct _M0TP26RiantR8snn__mbt9STDPEntry* _block_5981;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3523
  = _M0MP26RiantR8snn__mbt13STDPVariables3new(_M0L6n__preS1089, _M0L7n__postS1090);
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3524 = _M0MP26RiantR8snn__mbt12STDPGerstner3new();
  _M0L6_2atmpS3526 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS3526[0] = 0x0p+0f;
  _M0L6_2atmpS3525
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS3525)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS3525->$0 = _M0L6_2atmpS3526;
  _M0L6_2atmpS3525->$1 = 1;
  _block_5981
  = (struct _M0TP26RiantR8snn__mbt9STDPEntry*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9STDPEntry));
  Moonbit_object_header(_block_5981)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 80, 0);
  _block_5981->$0 = _M0L11conn__indexS1088;
  _block_5981->$1 = _M0L6n__preS1089;
  _block_5981->$2 = _M0L7n__postS1090;
  _block_5981->$3 = _M0L6_2atmpS3523;
  _block_5981->$4 = _M0L6_2atmpS3524;
  _block_5981->$5 = _M0L6_2atmpS3525;
  return _block_5981;
}

struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0MP26RiantR8snn__mbt13STDPVariables3new(
  int32_t _M0L6n__preS1086,
  int32_t _M0L7n__postS1087
) {
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3517;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3518;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3519;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3520;
  uint8_t* _M0L6_2atmpS3522;
  struct _M0TPB5ArrayGbE* _M0L6_2atmpS3521;
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _block_5982;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3517 = _M0MPC15array5Array4makeGfE(_M0L6n__preS1086, 0x0p+0f);
  #line 58 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3518 = _M0MPC15array5Array4makeGfE(_M0L7n__postS1087, 0x0p+0f);
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3519 = _M0MPC15array5Array4makeGfE(_M0L6n__preS1086, 0x0p+0f);
  #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3520 = _M0MPC15array5Array4makeGfE(_M0L7n__postS1087, 0x0p+0f);
  _M0L6_2atmpS3522 = (uint8_t*)moonbit_make_bytes_raw(1);
  _M0L6_2atmpS3522[0] = 1;
  _M0L6_2atmpS3521
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_M0L6_2atmpS3521)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 85, 0);
  _M0L6_2atmpS3521->$0 = _M0L6_2atmpS3522;
  _M0L6_2atmpS3521->$1 = 1;
  _block_5982
  = (struct _M0TP26RiantR8snn__mbt13STDPVariables*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13STDPVariables));
  Moonbit_object_header(_block_5982)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 88, 0);
  _block_5982->$0 = _M0L6_2atmpS3517;
  _block_5982->$1 = _M0L6_2atmpS3518;
  _block_5982->$2 = _M0L6_2atmpS3519;
  _block_5982->$3 = _M0L6_2atmpS3520;
  _block_5982->$4 = _M0L6_2atmpS3521;
  return _block_5982;
}

struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0MP26RiantR8snn__mbt12STDPGerstner3new(
  
) {
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _block_5983;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _block_5983
  = (struct _M0TP26RiantR8snn__mbt12STDPGerstner*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt12STDPGerstner));
  Moonbit_object_header(_block_5983)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5983->$0 = 0x1.47ae147ae147bp-7f;
  _block_5983->$1 = 0x1.47ae147ae147bp-7f;
  _block_5983->$2 = 0x1.4p+4f;
  _block_5983->$3 = 0x1.4p+4f;
  _block_5983->$4 = 0x1.ep+4f;
  _block_5983->$5 = 0x0p+0f;
  return _block_5983;
}

int32_t _M0FP26RiantR8snn__mbt19stimulate__balanced(
  struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L1sS1053,
  float _M0L4timeS1051,
  float _M0L2dtS1062
) {
  int32_t _M0L1nS1052;
  struct _M0TP26RiantR8snn__mbt17BalancedParameter* _M0L5paramS1054;
  float _M0L3kIES1055;
  float _M0L4betaS1056;
  float _M0L3tauS1057;
  float _M0L2r0S1058;
  float _M0L1wS1059;
  float _M0L3wIES1060;
  float _M0L6_2atmpS3516;
  float _M0L11inh__lambdaS1061;
  int32_t _M0L7_2abindS1063;
  int32_t _M0L1kS1064;
  float _M0L6_2atmpS3515;
  float _M0L2ccS1068;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
  _M0L1nS1052 = _M0L1sS1053->$1;
  _M0L5paramS1054 = _M0L1sS1053->$0;
  _M0L3kIES1055 = _M0L5paramS1054->$0;
  _M0L4betaS1056 = _M0L5paramS1054->$1;
  _M0L3tauS1057 = _M0L5paramS1054->$2;
  _M0L2r0S1058 = _M0L5paramS1054->$3;
  _M0L1wS1059 = _M0L5paramS1054->$4;
  _M0L3wIES1060 = _M0L5paramS1054->$5;
  _M0L6_2atmpS3516 = _M0L2r0S1058 * _M0L3kIES1055;
  _M0L11inh__lambdaS1061 = _M0L6_2atmpS3516 * _M0L2dtS1062;
  _M0L7_2abindS1063 = 0;
  _M0L1kS1064 = _M0L7_2abindS1063;
  while (1) {
    if (_M0L1kS1064 < _M0L1nS1052) {
      struct _M0TPB5ArrayGbE* _M0L4fireS3429 = _M0L1sS1053->$4;
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3437;
      int32_t _M0L1mS1067;
      int32_t _M0L6_2atmpS3428;
      #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3429, _M0L1kS1064, 0);
      if (_M0L11inh__lambdaS1061 <= 0x0p+0f) {
        goto join_1065;
      }
      _M0L3rngS3437 = _M0L1sS1053->$7;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
      _M0L1mS1067
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3437, _M0L11inh__lambdaS1061);
      if (_M0L1mS1067 > 0) {
        struct _M0TPB5ArrayGfE* _M0L2giS3430 = _M0L1sS1053->$3;
        struct _M0TPB5ArrayGfE* _M0L2giS3436 = _M0L1sS1053->$3;
        float _M0L6_2atmpS3432;
        float _M0L6_2atmpS3435;
        float _M0L6_2atmpS3434;
        float _M0L6_2atmpS3433;
        float _M0L6_2atmpS3431;
        #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3432
        = _M0MPC15array5Array2atGfE(_M0L2giS3436, _M0L1kS1064);
        _M0L6_2atmpS3435 = (float)_M0L1mS1067;
        _M0L6_2atmpS3434 = _M0L1wS1059 * _M0L6_2atmpS3435;
        _M0L6_2atmpS3433 = _M0L6_2atmpS3434 * _M0L3wIES1060;
        _M0L6_2atmpS3431 = _M0L6_2atmpS3432 + _M0L6_2atmpS3433;
        #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L2giS3430, _M0L1kS1064, _M0L6_2atmpS3431);
      }
      goto join_1065;
      goto joinlet_5985;
      join_1065:;
      _M0L6_2atmpS3428 = _M0L1kS1064 + 1;
      _M0L1kS1064 = _M0L6_2atmpS3428;
      continue;
      joinlet_5985:;
    }
    break;
  }
  _M0L6_2atmpS3515 = _M0L2dtS1062 / _M0L3tauS1057;
  _M0L2ccS1068 = 0x1p+0f - _M0L6_2atmpS3515;
  if (_M0L5paramS1054->$6) {
    struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3475 = _M0L1sS1053->$7;
    double _M0L6_2atmpS3474;
    float _M0L6_2atmpS3473;
    float _M0L2reS1069;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3438;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3443;
    float _M0L6_2atmpS3442;
    float _M0L6_2atmpS3441;
    float _M0L6_2atmpS3440;
    float _M0L6_2atmpS3439;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3472;
    float _M0L6_2atmpS3471;
    float _M0L6_2atmpS3470;
    struct _M0TPB8MutLocalGfE* _M0L2nbS1070;
    float _M0L3valS3444;
    float _M0L3valS3445;
    float _M0L6_2atmpS3468;
    float _M0L3valS3469;
    float _M0L6_2atmpS3465;
    struct _M0TPB5ArrayGfE* _M0L1rS3467;
    float _M0L6_2atmpS3466;
    float _M0L6_2atmpS3464;
    struct _M0TPB8MutLocalGfE* _M0L5erateS1071;
    float _M0L3valS3446;
    struct _M0TPB5ArrayGfE* _M0L1rS3447;
    struct _M0TPB5ArrayGfE* _M0L1rS3454;
    float _M0L6_2atmpS3449;
    float _M0L3valS3453;
    float _M0L6_2atmpS3452;
    float _M0L6_2atmpS3451;
    float _M0L6_2atmpS3450;
    float _M0L6_2atmpS3448;
    float _M0L3valS3463;
    float _M0L11exc__lambdaS1072;
    struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3462;
    int32_t _M0L1mS1073;
    #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3474 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS3475);
    _M0L6_2atmpS3473 = (float)_M0L6_2atmpS3474;
    _M0L2reS1069 = _M0L6_2atmpS3473 - 0x1p-1f;
    _M0L5noiseS3438 = _M0L1sS1053->$6;
    _M0L5noiseS3443 = _M0L1sS1053->$6;
    #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3442 = _M0MPC15array5Array2atGfE(_M0L5noiseS3443, 0);
    _M0L6_2atmpS3441 = _M0L6_2atmpS3442 - _M0L2reS1069;
    _M0L6_2atmpS3440 = _M0L6_2atmpS3441 * _M0L2ccS1068;
    _M0L6_2atmpS3439 = _M0L6_2atmpS3440 + _M0L2reS1069;
    #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0MPC15array5Array3setGfE(_M0L5noiseS3438, 0, _M0L6_2atmpS3439);
    _M0L5noiseS3472 = _M0L1sS1053->$6;
    #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3471 = _M0MPC15array5Array2atGfE(_M0L5noiseS3472, 0);
    _M0L6_2atmpS3470 = _M0L6_2atmpS3471 * _M0L4betaS1056;
    _M0L2nbS1070
    = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
    Moonbit_object_header(_M0L2nbS1070)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L2nbS1070->$0 = _M0L6_2atmpS3470;
    _M0L3valS3444 = _M0L2nbS1070->$0;
    if (_M0L3valS3444 > 0x1p+0f) {
      _M0L2nbS1070->$0 = 0x1p+0f;
    }
    _M0L3valS3445 = _M0L2nbS1070->$0;
    if (_M0L3valS3445 < 0x0p+0f) {
      _M0L2nbS1070->$0 = 0x0p+0f;
    }
    _M0L6_2atmpS3468 = _M0L2r0S1058 / 0x1p+1f;
    _M0L3valS3469 = _M0L2nbS1070->$0;
    moonbit_decref_cycle_free(_M0L2nbS1070);
    _M0L6_2atmpS3465 = _M0L6_2atmpS3468 * _M0L3valS3469;
    _M0L1rS3467 = _M0L1sS1053->$5;
    #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3466 = _M0MPC15array5Array2atGfE(_M0L1rS3467, 0);
    _M0L6_2atmpS3464 = _M0L6_2atmpS3465 + _M0L6_2atmpS3466;
    _M0L5erateS1071
    = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
    Moonbit_object_header(_M0L5erateS1071)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L5erateS1071->$0 = _M0L6_2atmpS3464;
    _M0L3valS3446 = _M0L5erateS1071->$0;
    if (_M0L3valS3446 < 0x0p+0f) {
      _M0L5erateS1071->$0 = 0x0p+0f;
    }
    _M0L1rS3447 = _M0L1sS1053->$5;
    _M0L1rS3454 = _M0L1sS1053->$5;
    #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3449 = _M0MPC15array5Array2atGfE(_M0L1rS3454, 0);
    _M0L3valS3453 = _M0L5erateS1071->$0;
    _M0L6_2atmpS3452 = _M0L2r0S1058 - _M0L3valS3453;
    _M0L6_2atmpS3451 = _M0L6_2atmpS3452 / 0x1.9p+8f;
    _M0L6_2atmpS3450 = _M0L6_2atmpS3451 * _M0L2dtS1062;
    _M0L6_2atmpS3448 = _M0L6_2atmpS3449 + _M0L6_2atmpS3450;
    #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0MPC15array5Array3setGfE(_M0L1rS3447, 0, _M0L6_2atmpS3448);
    _M0L3valS3463 = _M0L5erateS1071->$0;
    moonbit_decref_cycle_free(_M0L5erateS1071);
    _M0L11exc__lambdaS1072 = _M0L3valS3463 * _M0L2dtS1062;
    _M0L3rngS3462 = _M0L1sS1053->$7;
    #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L1mS1073
    = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3462, _M0L11exc__lambdaS1072);
    if (_M0L1mS1073 > 0) {
      float _M0L6_2atmpS3461 = (float)_M0L1mS1073;
      float _M0L3addS1074 = _M0L1wS1059 * _M0L6_2atmpS3461;
      int32_t _M0L7_2abindS1075 = 0;
      int32_t _M0L1iS1076 = _M0L7_2abindS1075;
      while (1) {
        if (_M0L1iS1076 < _M0L1nS1052) {
          struct _M0TPB5ArrayGfE* _M0L2geS3455 = _M0L1sS1053->$2;
          struct _M0TPB5ArrayGfE* _M0L2geS3458 = _M0L1sS1053->$2;
          float _M0L6_2atmpS3457;
          float _M0L6_2atmpS3456;
          struct _M0TPB5ArrayGbE* _M0L4fireS3459;
          int32_t _M0L6_2atmpS3460;
          #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0L6_2atmpS3457
          = _M0MPC15array5Array2atGfE(_M0L2geS3458, _M0L1iS1076);
          _M0L6_2atmpS3456 = _M0L6_2atmpS3457 + _M0L3addS1074;
          #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGfE(_M0L2geS3455, _M0L1iS1076, _M0L6_2atmpS3456);
          _M0L4fireS3459 = _M0L1sS1053->$4;
          #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGbE(_M0L4fireS3459, _M0L1iS1076, 1);
          _M0L6_2atmpS3460 = _M0L1iS1076 + 1;
          _M0L1iS1076 = _M0L6_2atmpS3460;
          continue;
        }
        break;
      }
    }
  } else {
    int32_t _M0L7_2abindS1078 = 0;
    int32_t _M0L1iS1079 = _M0L7_2abindS1078;
    while (1) {
      if (_M0L1iS1079 < _M0L1nS1052) {
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3513 =
          _M0L1sS1053->$7;
        double _M0L6_2atmpS3512;
        float _M0L6_2atmpS3511;
        float _M0L2reS1080;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3476;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3481;
        float _M0L6_2atmpS3480;
        float _M0L6_2atmpS3479;
        float _M0L6_2atmpS3478;
        float _M0L6_2atmpS3477;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3510;
        float _M0L6_2atmpS3509;
        float _M0L6_2atmpS3508;
        struct _M0TPB8MutLocalGfE* _M0L2nbS1081;
        float _M0L3valS3482;
        float _M0L3valS3483;
        float _M0L6_2atmpS3506;
        float _M0L3valS3507;
        float _M0L6_2atmpS3503;
        struct _M0TPB5ArrayGfE* _M0L1rS3505;
        float _M0L6_2atmpS3504;
        float _M0L6_2atmpS3502;
        struct _M0TPB8MutLocalGfE* _M0L5erateS1082;
        float _M0L3valS3484;
        struct _M0TPB5ArrayGfE* _M0L1rS3485;
        struct _M0TPB5ArrayGfE* _M0L1rS3492;
        float _M0L6_2atmpS3487;
        float _M0L3valS3491;
        float _M0L6_2atmpS3490;
        float _M0L6_2atmpS3489;
        float _M0L6_2atmpS3488;
        float _M0L6_2atmpS3486;
        float _M0L3valS3501;
        float _M0L11exc__lambdaS1083;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3500;
        int32_t _M0L1mS1084;
        int32_t _M0L6_2atmpS3514;
        #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3512 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS3513);
        _M0L6_2atmpS3511 = (float)_M0L6_2atmpS3512;
        _M0L2reS1080 = _M0L6_2atmpS3511 - 0x1p-1f;
        _M0L5noiseS3476 = _M0L1sS1053->$6;
        _M0L5noiseS3481 = _M0L1sS1053->$6;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3480
        = _M0MPC15array5Array2atGfE(_M0L5noiseS3481, _M0L1iS1079);
        _M0L6_2atmpS3479 = _M0L6_2atmpS3480 - _M0L2reS1080;
        _M0L6_2atmpS3478 = _M0L6_2atmpS3479 * _M0L2ccS1068;
        _M0L6_2atmpS3477 = _M0L6_2atmpS3478 + _M0L2reS1080;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L5noiseS3476, _M0L1iS1079, _M0L6_2atmpS3477);
        _M0L5noiseS3510 = _M0L1sS1053->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3509
        = _M0MPC15array5Array2atGfE(_M0L5noiseS3510, _M0L1iS1079);
        _M0L6_2atmpS3508 = _M0L6_2atmpS3509 * _M0L4betaS1056;
        _M0L2nbS1081
        = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
        Moonbit_object_header(_M0L2nbS1081)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L2nbS1081->$0 = _M0L6_2atmpS3508;
        _M0L3valS3482 = _M0L2nbS1081->$0;
        if (_M0L3valS3482 > 0x1p+0f) {
          _M0L2nbS1081->$0 = 0x1p+0f;
        }
        _M0L3valS3483 = _M0L2nbS1081->$0;
        if (_M0L3valS3483 < 0x0p+0f) {
          _M0L2nbS1081->$0 = 0x0p+0f;
        }
        _M0L6_2atmpS3506 = _M0L2r0S1058 / 0x1p+1f;
        _M0L3valS3507 = _M0L2nbS1081->$0;
        moonbit_decref_cycle_free(_M0L2nbS1081);
        _M0L6_2atmpS3503 = _M0L6_2atmpS3506 * _M0L3valS3507;
        _M0L1rS3505 = _M0L1sS1053->$5;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3504
        = _M0MPC15array5Array2atGfE(_M0L1rS3505, _M0L1iS1079);
        _M0L6_2atmpS3502 = _M0L6_2atmpS3503 + _M0L6_2atmpS3504;
        _M0L5erateS1082
        = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
        Moonbit_object_header(_M0L5erateS1082)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L5erateS1082->$0 = _M0L6_2atmpS3502;
        _M0L3valS3484 = _M0L5erateS1082->$0;
        if (_M0L3valS3484 < 0x0p+0f) {
          _M0L5erateS1082->$0 = 0x0p+0f;
        }
        _M0L1rS3485 = _M0L1sS1053->$5;
        _M0L1rS3492 = _M0L1sS1053->$5;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3487
        = _M0MPC15array5Array2atGfE(_M0L1rS3492, _M0L1iS1079);
        _M0L3valS3491 = _M0L5erateS1082->$0;
        _M0L6_2atmpS3490 = _M0L2r0S1058 - _M0L3valS3491;
        _M0L6_2atmpS3489 = _M0L6_2atmpS3490 / 0x1.9p+8f;
        _M0L6_2atmpS3488 = _M0L6_2atmpS3489 * _M0L2dtS1062;
        _M0L6_2atmpS3486 = _M0L6_2atmpS3487 + _M0L6_2atmpS3488;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L1rS3485, _M0L1iS1079, _M0L6_2atmpS3486);
        _M0L3valS3501 = _M0L5erateS1082->$0;
        moonbit_decref_cycle_free(_M0L5erateS1082);
        _M0L11exc__lambdaS1083 = _M0L3valS3501 * _M0L2dtS1062;
        _M0L3rngS3500 = _M0L1sS1053->$7;
        #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L1mS1084
        = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3500, _M0L11exc__lambdaS1083);
        if (_M0L1mS1084 > 0) {
          struct _M0TPB5ArrayGfE* _M0L2geS3493 = _M0L1sS1053->$2;
          struct _M0TPB5ArrayGfE* _M0L2geS3498 = _M0L1sS1053->$2;
          float _M0L6_2atmpS3495;
          float _M0L6_2atmpS3497;
          float _M0L6_2atmpS3496;
          float _M0L6_2atmpS3494;
          struct _M0TPB5ArrayGbE* _M0L4fireS3499;
          #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0L6_2atmpS3495
          = _M0MPC15array5Array2atGfE(_M0L2geS3498, _M0L1iS1079);
          _M0L6_2atmpS3497 = (float)_M0L1mS1084;
          _M0L6_2atmpS3496 = _M0L1wS1059 * _M0L6_2atmpS3497;
          _M0L6_2atmpS3494 = _M0L6_2atmpS3495 + _M0L6_2atmpS3496;
          #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGfE(_M0L2geS3493, _M0L1iS1079, _M0L6_2atmpS3494);
          _M0L4fireS3499 = _M0L1sS1053->$4;
          #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGbE(_M0L4fireS3499, _M0L1iS1079, 1);
        }
        _M0L6_2atmpS3514 = _M0L1iS1079 + 1;
        _M0L1iS1079 = _M0L6_2atmpS3514;
        continue;
      }
      break;
    }
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS1049
) {
  struct _M0TUmmmmE* _M0L1sS1048;
  uint64_t _M0L6_2atmpS3427;
  struct _M0TUmmmmE* _M0L1tS1050;
  uint64_t _M0L6_2atmpS3423;
  uint64_t _M0L6_2atmpS3424;
  uint64_t _M0L6_2atmpS3425;
  uint64_t _M0L6_2atmpS3426;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_5988;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS1048 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS1049);
  _M0L6_2atmpS3427 = _M0L1sS1048->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS1050 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS3427);
  _M0L6_2atmpS3423 = _M0L1sS1048->$0;
  _M0L6_2atmpS3424 = _M0L1sS1048->$1;
  _M0L6_2atmpS3425 = _M0L1sS1048->$2;
  moonbit_decref_cycle_free(_M0L1sS1048);
  _M0L6_2atmpS3426 = _M0L1tS1050->$0;
  moonbit_decref_cycle_free(_M0L1tS1050);
  _block_5988
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_5988)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5988->$0 = _M0L6_2atmpS3423;
  _block_5988->$1 = _M0L6_2atmpS3424;
  _block_5988->$2 = _M0L6_2atmpS3425;
  _block_5988->$3 = _M0L6_2atmpS3426;
  return _block_5988;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(
  uint64_t _M0L4seedS1040
) {
  uint64_t _M0L2s1S1039;
  uint64_t _M0L2z1S1041;
  uint64_t _M0L2s2S1042;
  uint64_t _M0L2z2S1043;
  uint64_t _M0L2s3S1044;
  uint64_t _M0L2z3S1045;
  uint64_t _M0L2s4S1046;
  uint64_t _M0L2z4S1047;
  struct _M0TUmmmmE* _block_5989;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S1039 = _M0L4seedS1040 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S1041 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S1039);
  _M0L2s2S1042 = _M0L2s1S1039 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S1043 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S1042);
  _M0L2s3S1044 = _M0L2s2S1042 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S1045 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S1044);
  _M0L2s4S1046 = _M0L2s3S1044 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S1047 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S1046);
  _block_5989 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_5989)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5989->$0 = _M0L2z1S1041;
  _block_5989->$1 = _M0L2z2S1043;
  _block_5989->$2 = _M0L2z3S1045;
  _block_5989->$3 = _M0L2z4S1047;
  return _block_5989;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS1037) {
  uint64_t _M0L6_2atmpS3422;
  uint64_t _M0L6_2atmpS3421;
  uint64_t _M0L1zS1036;
  uint64_t _M0L6_2atmpS3420;
  uint64_t _M0L6_2atmpS3419;
  uint64_t _M0L1zS1038;
  uint64_t _M0L6_2atmpS3418;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS3422 = _M0L1zS1037 >> 30;
  _M0L6_2atmpS3421 = _M0L1zS1037 ^ _M0L6_2atmpS3422;
  _M0L1zS1036 = _M0L6_2atmpS3421 * 13787848793156543929ull;
  _M0L6_2atmpS3420 = _M0L1zS1036 >> 27;
  _M0L6_2atmpS3419 = _M0L1zS1036 ^ _M0L6_2atmpS3420;
  _M0L1zS1038 = _M0L6_2atmpS3419 * 10723151780598845931ull;
  _M0L6_2atmpS3418 = _M0L1zS1038 >> 31;
  return _M0L1zS1038 ^ _M0L6_2atmpS3418;
}

int32_t _M0FP26RiantR8snn__mbt25stimulate__current__array(
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L1sS1024
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3402;
  int32_t _M0L6_2atmpS3401;
  float _M0L12noise__sigmaS3403;
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6activeS3402 = _M0L1sS1024->$1;
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6_2atmpS3401 = _M0MPC15array5Array2atGbE(_M0L6activeS3402, 0);
  if (!_M0L6_2atmpS3401) {
    return 0;
  }
  _M0L12noise__sigmaS3403 = _M0L1sS1024->$4;
  if (_M0L12noise__sigmaS3403 <= 0x0p+0f) {
    int32_t _M0L7_2abindS1025 = 0;
    int32_t _M0L7_2abindS1026 = _M0L1sS1024->$3;
    int32_t _M0L1kS1027 = _M0L7_2abindS1025;
    while (1) {
      if (_M0L1kS1027 < _M0L7_2abindS1026) {
        struct _M0TPB5ArrayGfE* _M0L1iS3404 = _M0L1sS1024->$2;
        float _M0L7i__baseS3405 = _M0L1sS1024->$0;
        int32_t _M0L6_2atmpS3406;
        #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3404, _M0L1kS1027, _M0L7i__baseS3405);
        _M0L6_2atmpS3406 = _M0L1kS1027 + 1;
        _M0L1kS1027 = _M0L6_2atmpS3406;
        continue;
      }
      break;
    }
  } else {
    float _M0L5sigmaS1029 = _M0L1sS1024->$4;
    struct _M0TPB8MutLocalGiE* _M0L1kS1030 =
      (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS1030)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS1030->$0 = 0;
    while (1) {
      int32_t _M0L3valS3407 = _M0L1kS1030->$0;
      int32_t _M0L1nS3408 = _M0L1sS1024->$3;
      if (_M0L3valS3407 < _M0L1nS3408) {
        double _M0L2z1S1032;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3417 =
          _M0L1sS1024->$5;
        struct _M0TUddE* _M0L7_2abindS1033;
        double _M0L5_2az1S1034;
        struct _M0TPB5ArrayGfE* _M0L1iS3409;
        int32_t _M0L3valS3410;
        float _M0L7i__baseS3412;
        float _M0L6_2atmpS3414;
        float _M0L6_2atmpS3413;
        float _M0L6_2atmpS3411;
        int32_t _M0L3valS3416;
        int32_t _M0L6_2atmpS3415;
        #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0L7_2abindS1033
        = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS3417);
        _M0L5_2az1S1034 = _M0L7_2abindS1033->$0;
        moonbit_decref_cycle_free(_M0L7_2abindS1033);
        _M0L2z1S1032 = _M0L5_2az1S1034;
        goto join_1031;
        goto joinlet_5992;
        join_1031:;
        _M0L1iS3409 = _M0L1sS1024->$2;
        _M0L3valS3410 = _M0L1kS1030->$0;
        _M0L7i__baseS3412 = _M0L1sS1024->$0;
        _M0L6_2atmpS3414 = (float)_M0L2z1S1032;
        _M0L6_2atmpS3413 = _M0L5sigmaS1029 * _M0L6_2atmpS3414;
        _M0L6_2atmpS3411 = _M0L7i__baseS3412 + _M0L6_2atmpS3413;
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3409, _M0L3valS3410, _M0L6_2atmpS3411);
        _M0L3valS3416 = _M0L1kS1030->$0;
        _M0L6_2atmpS3415 = _M0L3valS3416 + 1;
        _M0L1kS1030->$0 = _M0L6_2atmpS3415;
        joinlet_5992:;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS1030);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22stimulate__current__if(
  struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L1sS1011
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3386;
  int32_t _M0L6_2atmpS3385;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS1012;
  int32_t _M0L1nS1013;
  float _M0L12noise__sigmaS3387;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6activeS3386 = _M0L1sS1011->$1;
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6_2atmpS3385 = _M0MPC15array5Array2atGbE(_M0L6activeS3386, 0);
  if (!_M0L6_2atmpS3385) {
    return 0;
  }
  _M0L3popS1012 = _M0L1sS1011->$2;
  _M0L1nS1013 = _M0L3popS1012->$2;
  _M0L12noise__sigmaS3387 = _M0L1sS1011->$3;
  if (_M0L12noise__sigmaS3387 <= 0x0p+0f) {
    int32_t _M0L7_2abindS1014 = 0;
    int32_t _M0L1iS1015 = _M0L7_2abindS1014;
    while (1) {
      if (_M0L1iS1015 < _M0L1nS1013) {
        struct _M0TPB5ArrayGfE* _M0L1iS3388 = _M0L3popS1012->$7;
        float _M0L7i__baseS3389 = _M0L1sS1011->$0;
        int32_t _M0L6_2atmpS3390;
        #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3388, _M0L1iS1015, _M0L7i__baseS3389);
        _M0L6_2atmpS3390 = _M0L1iS1015 + 1;
        _M0L1iS1015 = _M0L6_2atmpS3390;
        continue;
      }
      break;
    }
  } else {
    float _M0L5sigmaS1017 = _M0L1sS1011->$3;
    struct _M0TPB8MutLocalGiE* _M0L1kS1018 =
      (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS1018)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS1018->$0 = 0;
    while (1) {
      int32_t _M0L3valS3391 = _M0L1kS1018->$0;
      if (_M0L3valS3391 < _M0L1nS1013) {
        double _M0L2z1S1020;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3400 =
          _M0L1sS1011->$4;
        struct _M0TUddE* _M0L7_2abindS1021;
        double _M0L5_2az1S1022;
        struct _M0TPB5ArrayGfE* _M0L1iS3392;
        int32_t _M0L3valS3393;
        float _M0L7i__baseS3395;
        float _M0L6_2atmpS3397;
        float _M0L6_2atmpS3396;
        float _M0L6_2atmpS3394;
        int32_t _M0L3valS3399;
        int32_t _M0L6_2atmpS3398;
        #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0L7_2abindS1021
        = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS3400);
        _M0L5_2az1S1022 = _M0L7_2abindS1021->$0;
        moonbit_decref_cycle_free(_M0L7_2abindS1021);
        _M0L2z1S1020 = _M0L5_2az1S1022;
        goto join_1019;
        goto joinlet_5995;
        join_1019:;
        _M0L1iS3392 = _M0L3popS1012->$7;
        _M0L3valS3393 = _M0L1kS1018->$0;
        _M0L7i__baseS3395 = _M0L1sS1011->$0;
        _M0L6_2atmpS3397 = (float)_M0L2z1S1020;
        _M0L6_2atmpS3396 = _M0L5sigmaS1017 * _M0L6_2atmpS3397;
        _M0L6_2atmpS3394 = _M0L7i__baseS3395 + _M0L6_2atmpS3396;
        #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3392, _M0L3valS3393, _M0L6_2atmpS3394);
        _M0L3valS3399 = _M0L1kS1018->$0;
        _M0L6_2atmpS3398 = _M0L3valS3399 + 1;
        _M0L1kS1018->$0 = _M0L6_2atmpS3398;
        joinlet_5995:;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS1018);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13stimulate__if(
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L1sS1000,
  float _M0L4timeS1010,
  float _M0L2dtS1002
) {
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3372;
  struct _M0TPB5ArrayGbE* _M0L6activeS3371;
  int32_t _M0L6_2atmpS3370;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3384;
  float _M0L4rateS3383;
  float _M0L6lambdaS1001;
  struct _M0TPB5ArrayGiE* _M0L7_2abindS1003;
  int32_t _M0L7_2abindS1004;
  int32_t* _M0L7_2abindS1005;
  int32_t _M0L2__S1006;
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L5paramS3372 = _M0L1sS1000->$0;
  _M0L6activeS3371 = _M0L5paramS3372->$2;
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3370 = _M0MPC15array5Array2atGbE(_M0L6activeS3371, 0);
  if (!_M0L6_2atmpS3370) {
    return 0;
  }
  _M0L5paramS3384 = _M0L1sS1000->$0;
  _M0L4rateS3383 = _M0L5paramS3384->$0;
  _M0L6lambdaS1001 = _M0L4rateS3383 * _M0L2dtS1002;
  if (_M0L6lambdaS1001 <= 0x0p+0f) {
    return 0;
  }
  _M0L7_2abindS1003 = _M0L1sS1000->$1;
  _M0L7_2abindS1004 = _M0L7_2abindS1003->$1;
  _M0L7_2abindS1005 = _M0L7_2abindS1003->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1005);
  _M0L2__S1006 = 0;
  while (1) {
    if (_M0L2__S1006 < _M0L7_2abindS1004) {
      int32_t _M0L1nS1007 = (int32_t)_M0L7_2abindS1005[_M0L2__S1006];
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3381 = _M0L1sS1000->$3;
      int32_t _M0L1kS1008;
      int32_t _M0L6_2atmpS3382;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
      _M0L1kS1008
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3381, _M0L6lambdaS1001);
      if (_M0L1kS1008 > 0) {
        struct _M0TPB5ArrayGfE* _M0L1gS3373 = _M0L1sS1000->$2;
        struct _M0TPB5ArrayGfE* _M0L1gS3380 = _M0L1sS1000->$2;
        float _M0L6_2atmpS3375;
        struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3379;
        float _M0L2muS3377;
        float _M0L6_2atmpS3378;
        float _M0L6_2atmpS3376;
        float _M0L6_2atmpS3374;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0L6_2atmpS3375
        = _M0MPC15array5Array2atGfE(_M0L1gS3380, _M0L1nS1007);
        _M0L5paramS3379 = _M0L1sS1000->$0;
        _M0L2muS3377 = _M0L5paramS3379->$1;
        _M0L6_2atmpS3378 = (float)_M0L1kS1008;
        _M0L6_2atmpS3376 = _M0L2muS3377 * _M0L6_2atmpS3378;
        _M0L6_2atmpS3374 = _M0L6_2atmpS3375 + _M0L6_2atmpS3376;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0MPC15array5Array3setGfE(_M0L1gS3373, _M0L1nS1007, _M0L6_2atmpS3374);
      }
      _M0L6_2atmpS3382 = _M0L2__S1006 + 1;
      _M0L2__S1006 = _M0L6_2atmpS3382;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1005);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16stimulate__layer(
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L1sS983,
  float _M0L4timeS981,
  float _M0L2dtS986
) {
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS3369;
  int32_t _M0L6n__preS982;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3368;
  int32_t _M0L7n__postS984;
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS3367;
  float _M0L4rateS3366;
  float _M0L6lambdaS985;
  int32_t _M0L7_2abindS987;
  int32_t _M0L1iS988;
  moonbit_string_t _M0L3symS3363;
  struct _M0TPB5ArrayGfE* _M0L9g__targetS990;
  int32_t _M0L7_2abindS991;
  int32_t _M0L1iS992;
  #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  _M0L5paramS3369 = _M0L1sS983->$0;
  _M0L6n__preS982 = _M0L5paramS3369->$1;
  _M0L4postS3368 = _M0L1sS983->$1;
  _M0L7n__postS984 = _M0L4postS3368->$2;
  _M0L5paramS3367 = _M0L1sS983->$0;
  _M0L4rateS3366 = _M0L5paramS3367->$0;
  _M0L6lambdaS985 = _M0L4rateS3366 * _M0L2dtS986;
  _M0L7_2abindS987 = 0;
  _M0L1iS988 = _M0L7_2abindS987;
  while (1) {
    if (_M0L1iS988 < _M0L6n__preS982) {
      struct _M0TPB5ArrayGbE* _M0L4fireS3348 = _M0L1sS983->$3;
      int32_t _M0L6_2atmpS3349;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3348, _M0L1iS988, 0);
      _M0L6_2atmpS3349 = _M0L1iS988 + 1;
      _M0L1iS988 = _M0L6_2atmpS3349;
      continue;
    }
    break;
  }
  if (_M0L6lambdaS985 <= 0x0p+0f) {
    return 0;
  }
  _M0L3symS3363 = _M0L1sS983->$2;
  #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  if (
    _M0L3symS3363 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS3363)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS3363, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS3363) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3364 = _M0L1sS983->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5620 = _M0L4postS3364->$13;
    moonbit_incref_cycle_free(_M0L8_2afieldS5620);
    _M0L9g__targetS990 = _M0L8_2afieldS5620;
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3365 = _M0L1sS983->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5621 = _M0L4postS3365->$14;
    moonbit_incref_cycle_free(_M0L8_2afieldS5621);
    _M0L9g__targetS990 = _M0L8_2afieldS5621;
  }
  _M0L7_2abindS991 = 0;
  _M0L1iS992 = _M0L7_2abindS991;
  while (1) {
    if (_M0L1iS992 < _M0L6n__preS982) {
      struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS3353 =
        _M0L1sS983->$0;
      struct _M0TPB5ArrayGbE* _M0L6activeS3352 = _M0L5paramS3353->$2;
      int32_t _M0L6_2atmpS3351;
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3362;
      int32_t _M0L1kS995;
      int32_t _M0L6_2atmpS3350;
      moonbit_incref_cycle_free(_M0L6activeS3352);
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0L6_2atmpS3351
      = _M0MPC15array5Array2atGbE(_M0L6activeS3352, _M0L1iS992);
      moonbit_decref_cycle_free(_M0L6activeS3352);
      if (!_M0L6_2atmpS3351) {
        goto join_993;
      }
      _M0L3rngS3362 = _M0L1sS983->$6;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0L1kS995
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3362, _M0L6lambdaS985);
      if (_M0L1kS995 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS3354 = _M0L1sS983->$3;
        int32_t _M0L7_2abindS996;
        int32_t _M0L1jS997;
        #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS3354, _M0L1iS992, 1);
        _M0L7_2abindS996 = 0;
        _M0L1jS997 = _M0L7_2abindS996;
        while (1) {
          if (_M0L1jS997 < _M0L7n__postS984) {
            int32_t _M0L6_2atmpS3360 = _M0L1jS997 * _M0L6n__preS982;
            int32_t _M0L3idxS998 = _M0L6_2atmpS3360 + _M0L1iS992;
            struct _M0TPB5ArrayGbE* _M0L12connectivityS3355 = _M0L1sS983->$5;
            int32_t _M0L6_2atmpS3361;
            #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
            if (
              _M0MPC15array5Array2atGbE(_M0L12connectivityS3355, _M0L3idxS998)
            ) {
              float _M0L6_2atmpS3357;
              struct _M0TPB5ArrayGfE* _M0L7weightsS3359;
              float _M0L6_2atmpS3358;
              float _M0L6_2atmpS3356;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0L6_2atmpS3357
              = _M0MPC15array5Array2atGfE(_M0L9g__targetS990, _M0L1jS997);
              _M0L7weightsS3359 = _M0L1sS983->$4;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0L6_2atmpS3358
              = _M0MPC15array5Array2atGfE(_M0L7weightsS3359, _M0L3idxS998);
              _M0L6_2atmpS3356 = _M0L6_2atmpS3357 + _M0L6_2atmpS3358;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0MPC15array5Array3setGfE(_M0L9g__targetS990, _M0L1jS997, _M0L6_2atmpS3356);
            }
            _M0L6_2atmpS3361 = _M0L1jS997 + 1;
            _M0L1jS997 = _M0L6_2atmpS3361;
            continue;
          }
          break;
        }
      }
      goto join_993;
      goto joinlet_5999;
      join_993:;
      _M0L6_2atmpS3350 = _M0L1iS992 + 1;
      _M0L1iS992 = _M0L6_2atmpS3350;
      continue;
      joinlet_5999:;
    } else {
      moonbit_decref_cycle_free(_M0L9g__targetS990);
    }
    break;
  }
  return 0;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS976
) {
  double _M0L2u1S975;
  double _M0L8u1__safeS977;
  double _M0L2u2S978;
  double _M0L6_2atmpS3347;
  double _M0L6_2atmpS3346;
  double _M0L1rS979;
  double _M0L5thetaS980;
  double _M0L6_2atmpS3345;
  double _M0L6_2atmpS3342;
  double _M0L6_2atmpS3344;
  double _M0L6_2atmpS3343;
  struct _M0TUddE* _block_6001;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S975 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS976);
  if (_M0L2u1S975 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS977 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS977 = _M0L2u1S975;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S978 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS976);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS3347 = _M0FPC14math2ln(_M0L8u1__safeS977);
  _M0L6_2atmpS3346 = -0x1p+1 * _M0L6_2atmpS3347;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS979 = sqrt(_M0L6_2atmpS3346);
  _M0L5thetaS980 = 0x1.921fb54442d18p+2 * _M0L2u2S978;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS3345 = _M0FPC14math3cos(_M0L5thetaS980);
  _M0L6_2atmpS3342 = _M0L1rS979 * _M0L6_2atmpS3345;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS3344 = _M0FPC14math3sin(_M0L5thetaS980);
  _M0L6_2atmpS3343 = _M0L1rS979 * _M0L6_2atmpS3344;
  _block_6001 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_6001)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6001->$0 = _M0L6_2atmpS3342;
  _block_6001->$1 = _M0L6_2atmpS3343;
  return _block_6001;
}

int32_t _M0FP26RiantR8snn__mbt15sample__poisson(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS973,
  float _M0L6lambdaS967
) {
  float _M0L6_2atmpS3341;
  float _M0L6_2atmpS3340;
  double _M0L1lS968;
  struct _M0TPB8MutLocalGdE* _M0L1pS969;
  struct _M0TPB8MutLocalGiE* _M0L1kS970;
  float _M0L6_2atmpS3339;
  int32_t _M0L8ten__lamS972;
  int32_t _M0L3capS971;
  int32_t _M0L3valS3338;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  if (_M0L6lambdaS967 <= 0x0p+0f) {
    return 0;
  }
  _M0L6_2atmpS3341 = -_M0L6lambdaS967;
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3340 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3341);
  _M0L1lS968 = (double)_M0L6_2atmpS3340;
  _M0L1pS969
  = (struct _M0TPB8MutLocalGdE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGdE));
  Moonbit_object_header(_M0L1pS969)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1pS969->$0 = 0x1p+0;
  _M0L1kS970
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS970)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS970->$0 = 0;
  _M0L6_2atmpS3339 = _M0L6lambdaS967 * 0x1.4p+3f;
  #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L8ten__lamS972 = _M0MPC15float5Float7to__int(_M0L6_2atmpS3339);
  if (_M0L8ten__lamS972 > 100) {
    _M0L3capS971 = _M0L8ten__lamS972;
  } else {
    _M0L3capS971 = 100;
  }
  while (1) {
    int32_t _M0L3valS3330 = _M0L1kS970->$0;
    int32_t _M0L6_2atmpS3329 = _M0L3valS3330 + 1;
    double _M0L3valS3332;
    double _M0L6_2atmpS3333;
    double _M0L6_2atmpS3331;
    double _M0L3valS3334;
    int32_t _M0L3valS3336;
    _M0L1kS970->$0 = _M0L6_2atmpS3329;
    _M0L3valS3332 = _M0L1pS969->$0;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
    _M0L6_2atmpS3333 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS973);
    _M0L6_2atmpS3331 = _M0L3valS3332 * _M0L6_2atmpS3333;
    _M0L1pS969->$0 = _M0L6_2atmpS3331;
    _M0L3valS3334 = _M0L1pS969->$0;
    if (_M0L3valS3334 < _M0L1lS968) {
      int32_t _M0L3valS3335;
      moonbit_decref_cycle_free(_M0L1pS969);
      _M0L3valS3335 = _M0L1kS970->$0;
      moonbit_decref_cycle_free(_M0L1kS970);
      return _M0L3valS3335 - 1;
    }
    _M0L3valS3336 = _M0L1kS970->$0;
    if (_M0L3valS3336 > _M0L3capS971) {
      int32_t _M0L3valS3337;
      moonbit_decref_cycle_free(_M0L1pS969);
      _M0L3valS3337 = _M0L1kS970->$0;
      moonbit_decref_cycle_free(_M0L1kS970);
      return _M0L3valS3337 - 1;
    }
    continue;
    break;
  }
  _M0L3valS3338 = _M0L1kS970->$0;
  moonbit_decref_cycle_free(_M0L1kS970);
  return _M0L3valS3338 - 1;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS965
) {
  uint64_t _M0L1uS964;
  uint64_t _M0L4bitsS966;
  double _M0L6_2atmpS3328;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS964 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS965);
  _M0L4bitsS966 = _M0L1uS964 >> 11;
  _M0L6_2atmpS3328 = (double)_M0L4bitsS966;
  return _M0L6_2atmpS3328 * 0x1p-53;
}

int32_t _M0FP26RiantR8snn__mbt20stimulate__spiketime(
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L1sS958,
  float _M0L1tS960,
  float _M0L1wS962
) {
  struct _M0TPB8MutLocalGiE* _M0L1iS957;
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L1iS957
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS957)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS957->$0 = 0;
  while (1) {
    int32_t _M0L3valS3290 = _M0L1iS957->$0;
    int32_t _M0L1nS3291 = _M0L1sS958->$0;
    if (_M0L3valS3290 < _M0L1nS3291) {
      struct _M0TPB5ArrayGbE* _M0L4fireS3292 = _M0L1sS958->$4;
      int32_t _M0L3valS3293 = _M0L1iS957->$0;
      int32_t _M0L3valS3295;
      int32_t _M0L6_2atmpS3294;
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3292, _M0L3valS3293, 0);
      _M0L3valS3295 = _M0L1iS957->$0;
      _M0L6_2atmpS3294 = _M0L3valS3295 + 1;
      _M0L1iS957->$0 = _M0L6_2atmpS3294;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS957);
    }
    break;
  }
  while (1) {
    struct _M0TPB5ArrayGiE* _M0L11next__indexS3299 = _M0L1sS958->$3;
    int32_t _M0L6_2atmpS3298;
    int32_t _if__result_6005;
    #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
    _M0L6_2atmpS3298 = _M0MPC15array5Array2atGiE(_M0L11next__indexS3299, 0);
    if (_M0L6_2atmpS3298 >= 0) {
      struct _M0TPB5ArrayGfE* _M0L11next__spikeS3297 = _M0L1sS958->$2;
      float _M0L6_2atmpS3296;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3296 = _M0MPC15array5Array2atGfE(_M0L11next__spikeS3297, 0);
      _if__result_6005 = _M0L6_2atmpS3296 <= _M0L1tS960;
    } else {
      _if__result_6005 = 0;
    }
    if (_if__result_6005) {
      struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS3327 =
        _M0L1sS958->$1;
      struct _M0TPB5ArrayGiE* _M0L7neuronsS3324 = _M0L5paramS3327->$1;
      struct _M0TPB5ArrayGiE* _M0L11next__indexS3326 = _M0L1sS958->$3;
      int32_t _M0L6_2atmpS3325;
      int32_t _M0L1jS961;
      struct _M0TPB5ArrayGbE* _M0L4fireS3300;
      struct _M0TPB5ArrayGfE* _M0L1gS3301;
      struct _M0TPB5ArrayGfE* _M0L1gS3304;
      float _M0L6_2atmpS3303;
      float _M0L6_2atmpS3302;
      struct _M0TPB5ArrayGiE* _M0L11next__indexS3310;
      int32_t _M0L6_2atmpS3309;
      int32_t _M0L6_2atmpS3305;
      struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS3308;
      struct _M0TPB5ArrayGfE* _M0L10spiketimesS3307;
      int32_t _M0L6_2atmpS3306;
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3325 = _M0MPC15array5Array2atGiE(_M0L11next__indexS3326, 0);
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L1jS961
      = _M0MPC15array5Array2atGiE(_M0L7neuronsS3324, _M0L6_2atmpS3325);
      _M0L4fireS3300 = _M0L1sS958->$4;
      #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3300, _M0L1jS961, 1);
      _M0L1gS3301 = _M0L1sS958->$5;
      _M0L1gS3304 = _M0L1sS958->$5;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3303 = _M0MPC15array5Array2atGfE(_M0L1gS3304, _M0L1jS961);
      _M0L6_2atmpS3302 = _M0L6_2atmpS3303 + _M0L1wS962;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS3301, _M0L1jS961, _M0L6_2atmpS3302);
      _M0L11next__indexS3310 = _M0L1sS958->$3;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3309 = _M0MPC15array5Array2atGiE(_M0L11next__indexS3310, 0);
      _M0L6_2atmpS3305 = _M0L6_2atmpS3309 + 1;
      _M0L5paramS3308 = _M0L1sS958->$1;
      _M0L10spiketimesS3307 = _M0L5paramS3308->$0;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3306 = _M0MPC15array5Array6lengthGfE(_M0L10spiketimesS3307);
      if (_M0L6_2atmpS3305 < _M0L6_2atmpS3306) {
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3311 = _M0L1sS958->$3;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3314 = _M0L1sS958->$3;
        int32_t _M0L6_2atmpS3313;
        int32_t _M0L6_2atmpS3312;
        struct _M0TPB5ArrayGfE* _M0L11next__spikeS3315;
        struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS3320;
        struct _M0TPB5ArrayGfE* _M0L10spiketimesS3317;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3319;
        int32_t _M0L6_2atmpS3318;
        float _M0L6_2atmpS3316;
        #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS3313
        = _M0MPC15array5Array2atGiE(_M0L11next__indexS3314, 0);
        _M0L6_2atmpS3312 = _M0L6_2atmpS3313 + 1;
        #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGiE(_M0L11next__indexS3311, 0, _M0L6_2atmpS3312);
        _M0L11next__spikeS3315 = _M0L1sS958->$2;
        _M0L5paramS3320 = _M0L1sS958->$1;
        _M0L10spiketimesS3317 = _M0L5paramS3320->$0;
        _M0L11next__indexS3319 = _M0L1sS958->$3;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS3318
        = _M0MPC15array5Array2atGiE(_M0L11next__indexS3319, 0);
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS3316
        = _M0MPC15array5Array2atGfE(_M0L10spiketimesS3317, _M0L6_2atmpS3318);
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGfE(_M0L11next__spikeS3315, 0, _M0L6_2atmpS3316);
      } else {
        struct _M0TPB5ArrayGfE* _M0L11next__spikeS3321 = _M0L1sS958->$2;
        float _M0L6_2atmpS3322 = 0x0p+0f / (float)MOONBIT_ZERO;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3323;
        #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGfE(_M0L11next__spikeS3321, 0, _M0L6_2atmpS3322);
        _M0L11next__indexS3323 = _M0L1sS958->$3;
        #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGiE(_M0L11next__indexS3323, 0, -1);
      }
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28markram__stp__step__timestep(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS944,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS942,
  struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0L5paramS946,
  float _M0L6t__nowS941,
  float _M0L2dtS951
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3203;
  int32_t _M0L6_2atmpS3202;
  int32_t _if__result_6006;
  int32_t _M0L6n__preS943;
  struct _M0TPB5ArrayGfE* _M0L3rhoS3205;
  int32_t _M0L6_2atmpS3204;
  float _M0L11u__baselineS945;
  float _M0L6tau__fS3289;
  float _M0L11inv__tau__fS947;
  float _M0L6tau__dS3288;
  float _M0L11inv__tau__dS948;
  struct _M0TPB8MutLocalGiE* _M0L1jS949;
  #line 473 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS3203 = _M0L4varsS942->$6;
  #line 482 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3202 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3203);
  if (_M0L6_2atmpS3202 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3201 = _M0L4varsS942->$6;
    int32_t _M0L6_2atmpS3200;
    #line 482 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS3200 = _M0MPC15array5Array2atGbE(_M0L6activeS3201, 0);
    _if__result_6006 = !_M0L6_2atmpS3200;
  } else {
    _if__result_6006 = 0;
  }
  if (_if__result_6006) {
    return 0;
  }
  _M0L6n__preS943 = _M0L4varsS942->$0;
  _M0L3rhoS3205 = _M0L3synS944->$6;
  #line 487 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3204 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS3205);
  if (_M0L6_2atmpS3204 == 0) {
    return 0;
  }
  _M0L11u__baselineS945 = _M0L5paramS946->$0;
  _M0L6tau__fS3289 = _M0L5paramS946->$1;
  _M0L11inv__tau__fS947 = 0x1p+0f / _M0L6tau__fS3289;
  _M0L6tau__dS3288 = _M0L5paramS946->$2;
  _M0L11inv__tau__dS948 = 0x1p+0f / _M0L6tau__dS3288;
  _M0L1jS949
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS949)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS949->$0 = 0;
  while (1) {
    int32_t _M0L3valS3206 = _M0L1jS949->$0;
    if (_M0L3valS3206 < _M0L6n__preS943) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3209 = _M0L3synS944->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3207 = _M0L3preS3209->$5;
      int32_t _M0L3valS3208 = _M0L1jS949->$0;
      int32_t _M0L3valS3236;
      int32_t _M0L6_2atmpS3235;
      #line 496 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3207, _M0L3valS3208)) {
        struct _M0TPB5ArrayGfE* _M0L1uS3210 = _M0L4varsS942->$2;
        int32_t _M0L3valS3211 = _M0L1jS949->$0;
        struct _M0TPB5ArrayGfE* _M0L1uS3219 = _M0L4varsS942->$2;
        int32_t _M0L3valS3220 = _M0L1jS949->$0;
        float _M0L6_2atmpS3213;
        struct _M0TPB5ArrayGfE* _M0L1uS3217;
        int32_t _M0L3valS3218;
        float _M0L6_2atmpS3216;
        float _M0L6_2atmpS3215;
        float _M0L6_2atmpS3214;
        float _M0L6_2atmpS3212;
        struct _M0TPB5ArrayGfE* _M0L1xS3221;
        int32_t _M0L3valS3222;
        struct _M0TPB5ArrayGfE* _M0L1xS3233;
        int32_t _M0L3valS3234;
        float _M0L6_2atmpS3224;
        struct _M0TPB5ArrayGfE* _M0L1uS3231;
        int32_t _M0L3valS3232;
        float _M0L6_2atmpS3230;
        float _M0L6_2atmpS3226;
        struct _M0TPB5ArrayGfE* _M0L1xS3228;
        int32_t _M0L3valS3229;
        float _M0L6_2atmpS3227;
        float _M0L6_2atmpS3225;
        float _M0L6_2atmpS3223;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3213
        = _M0MPC15array5Array2atGfE(_M0L1uS3219, _M0L3valS3220);
        _M0L1uS3217 = _M0L4varsS942->$2;
        _M0L3valS3218 = _M0L1jS949->$0;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3216
        = _M0MPC15array5Array2atGfE(_M0L1uS3217, _M0L3valS3218);
        _M0L6_2atmpS3215 = 0x1p+0f - _M0L6_2atmpS3216;
        _M0L6_2atmpS3214 = _M0L11u__baselineS945 * _M0L6_2atmpS3215;
        _M0L6_2atmpS3212 = _M0L6_2atmpS3213 + _M0L6_2atmpS3214;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3210, _M0L3valS3211, _M0L6_2atmpS3212);
        _M0L1xS3221 = _M0L4varsS942->$3;
        _M0L3valS3222 = _M0L1jS949->$0;
        _M0L1xS3233 = _M0L4varsS942->$3;
        _M0L3valS3234 = _M0L1jS949->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3224
        = _M0MPC15array5Array2atGfE(_M0L1xS3233, _M0L3valS3234);
        _M0L1uS3231 = _M0L4varsS942->$2;
        _M0L3valS3232 = _M0L1jS949->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3230
        = _M0MPC15array5Array2atGfE(_M0L1uS3231, _M0L3valS3232);
        _M0L6_2atmpS3226 = -_M0L6_2atmpS3230;
        _M0L1xS3228 = _M0L4varsS942->$3;
        _M0L3valS3229 = _M0L1jS949->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3227
        = _M0MPC15array5Array2atGfE(_M0L1xS3228, _M0L3valS3229);
        _M0L6_2atmpS3225 = _M0L6_2atmpS3226 * _M0L6_2atmpS3227;
        _M0L6_2atmpS3223 = _M0L6_2atmpS3224 + _M0L6_2atmpS3225;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3221, _M0L3valS3222, _M0L6_2atmpS3223);
      }
      _M0L3valS3236 = _M0L1jS949->$0;
      _M0L6_2atmpS3235 = _M0L3valS3236 + 1;
      _M0L1jS949->$0 = _M0L6_2atmpS3235;
      continue;
    }
    break;
  }
  _M0L1jS949->$0 = 0;
  while (1) {
    int32_t _M0L3valS3237 = _M0L1jS949->$0;
    if (_M0L3valS3237 < _M0L6n__preS943) {
      struct _M0TPB5ArrayGfE* _M0L1uS3238 = _M0L4varsS942->$2;
      int32_t _M0L3valS3239 = _M0L1jS949->$0;
      struct _M0TPB5ArrayGfE* _M0L1uS3248 = _M0L4varsS942->$2;
      int32_t _M0L3valS3249 = _M0L1jS949->$0;
      float _M0L6_2atmpS3241;
      struct _M0TPB5ArrayGfE* _M0L1uS3246;
      int32_t _M0L3valS3247;
      float _M0L6_2atmpS3245;
      float _M0L6_2atmpS3244;
      float _M0L6_2atmpS3243;
      float _M0L6_2atmpS3242;
      float _M0L6_2atmpS3240;
      struct _M0TPB5ArrayGfE* _M0L1xS3250;
      int32_t _M0L3valS3251;
      struct _M0TPB5ArrayGfE* _M0L1xS3260;
      int32_t _M0L3valS3261;
      float _M0L6_2atmpS3253;
      struct _M0TPB5ArrayGfE* _M0L1xS3258;
      int32_t _M0L3valS3259;
      float _M0L6_2atmpS3257;
      float _M0L6_2atmpS3256;
      float _M0L6_2atmpS3255;
      float _M0L6_2atmpS3254;
      float _M0L6_2atmpS3252;
      struct _M0TPB5ArrayGfE* _M0L8rho__preS3262;
      int32_t _M0L3valS3263;
      struct _M0TPB5ArrayGfE* _M0L1uS3269;
      int32_t _M0L3valS3270;
      float _M0L6_2atmpS3265;
      struct _M0TPB5ArrayGfE* _M0L1xS3267;
      int32_t _M0L3valS3268;
      float _M0L6_2atmpS3266;
      float _M0L6_2atmpS3264;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3287;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS3285;
      int32_t _M0L3valS3286;
      int32_t _M0L5startS952;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3284;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS3281;
      int32_t _M0L3valS3283;
      int32_t _M0L6_2atmpS3282;
      int32_t _M0L3endS953;
      struct _M0TPB8MutLocalGiE* _M0L1sS954;
      int32_t _M0L3valS3280;
      int32_t _M0L6_2atmpS3279;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3241
      = _M0MPC15array5Array2atGfE(_M0L1uS3248, _M0L3valS3249);
      _M0L1uS3246 = _M0L4varsS942->$2;
      _M0L3valS3247 = _M0L1jS949->$0;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3245
      = _M0MPC15array5Array2atGfE(_M0L1uS3246, _M0L3valS3247);
      _M0L6_2atmpS3244 = _M0L11u__baselineS945 - _M0L6_2atmpS3245;
      _M0L6_2atmpS3243 = _M0L2dtS951 * _M0L6_2atmpS3244;
      _M0L6_2atmpS3242 = _M0L6_2atmpS3243 * _M0L11inv__tau__fS947;
      _M0L6_2atmpS3240 = _M0L6_2atmpS3241 + _M0L6_2atmpS3242;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS3238, _M0L3valS3239, _M0L6_2atmpS3240);
      _M0L1xS3250 = _M0L4varsS942->$3;
      _M0L3valS3251 = _M0L1jS949->$0;
      _M0L1xS3260 = _M0L4varsS942->$3;
      _M0L3valS3261 = _M0L1jS949->$0;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3253
      = _M0MPC15array5Array2atGfE(_M0L1xS3260, _M0L3valS3261);
      _M0L1xS3258 = _M0L4varsS942->$3;
      _M0L3valS3259 = _M0L1jS949->$0;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3257
      = _M0MPC15array5Array2atGfE(_M0L1xS3258, _M0L3valS3259);
      _M0L6_2atmpS3256 = 0x1p+0f - _M0L6_2atmpS3257;
      _M0L6_2atmpS3255 = _M0L2dtS951 * _M0L6_2atmpS3256;
      _M0L6_2atmpS3254 = _M0L6_2atmpS3255 * _M0L11inv__tau__dS948;
      _M0L6_2atmpS3252 = _M0L6_2atmpS3253 + _M0L6_2atmpS3254;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS3250, _M0L3valS3251, _M0L6_2atmpS3252);
      _M0L8rho__preS3262 = _M0L4varsS942->$4;
      _M0L3valS3263 = _M0L1jS949->$0;
      _M0L1uS3269 = _M0L4varsS942->$2;
      _M0L3valS3270 = _M0L1jS949->$0;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3265
      = _M0MPC15array5Array2atGfE(_M0L1uS3269, _M0L3valS3270);
      _M0L1xS3267 = _M0L4varsS942->$3;
      _M0L3valS3268 = _M0L1jS949->$0;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3266
      = _M0MPC15array5Array2atGfE(_M0L1xS3267, _M0L3valS3268);
      _M0L6_2atmpS3264 = _M0L6_2atmpS3265 * _M0L6_2atmpS3266;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L8rho__preS3262, _M0L3valS3263, _M0L6_2atmpS3264);
      _M0L6matrixS3287 = _M0L3synS944->$4;
      _M0L6rowptrS3285 = _M0L6matrixS3287->$2;
      _M0L3valS3286 = _M0L1jS949->$0;
      #line 509 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L5startS952
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS3285, _M0L3valS3286);
      _M0L6matrixS3284 = _M0L3synS944->$4;
      _M0L6rowptrS3281 = _M0L6matrixS3284->$2;
      _M0L3valS3283 = _M0L1jS949->$0;
      _M0L6_2atmpS3282 = _M0L3valS3283 + 1;
      #line 510 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L3endS953
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS3281, _M0L6_2atmpS3282);
      _M0L1sS954
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS954)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS954->$0 = _M0L5startS952;
      while (1) {
        int32_t _M0L3valS3271 = _M0L1sS954->$0;
        if (_M0L3valS3271 < _M0L3endS953) {
          struct _M0TPB5ArrayGfE* _M0L3rhoS3272 = _M0L3synS944->$6;
          int32_t _M0L3valS3273 = _M0L1sS954->$0;
          struct _M0TPB5ArrayGfE* _M0L8rho__preS3275 = _M0L4varsS942->$4;
          int32_t _M0L3valS3276 = _M0L1jS949->$0;
          float _M0L6_2atmpS3274;
          int32_t _M0L3valS3278;
          int32_t _M0L6_2atmpS3277;
          #line 513 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
          _M0L6_2atmpS3274
          = _M0MPC15array5Array2atGfE(_M0L8rho__preS3275, _M0L3valS3276);
          #line 513 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rhoS3272, _M0L3valS3273, _M0L6_2atmpS3274);
          _M0L3valS3278 = _M0L1sS954->$0;
          _M0L6_2atmpS3277 = _M0L3valS3278 + 1;
          _M0L1sS954->$0 = _M0L6_2atmpS3277;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS954);
        }
        break;
      }
      _M0L3valS3280 = _M0L1jS949->$0;
      _M0L6_2atmpS3279 = _M0L3valS3280 + 1;
      _M0L1jS949->$0 = _M0L6_2atmpS3279;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS949);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23markram__stp__step__het(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS925,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS923,
  struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet* _M0L5paramS928,
  float _M0L6t__nowS932
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3111;
  int32_t _M0L6_2atmpS3110;
  int32_t _if__result_6010;
  int32_t _M0L6n__preS924;
  struct _M0TPB5ArrayGfE* _M0L3rhoS3113;
  int32_t _M0L6_2atmpS3112;
  struct _M0TPB8MutLocalGiE* _M0L1jS926;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS3111 = _M0L4varsS923->$6;
  #line 353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3110 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3111);
  if (_M0L6_2atmpS3110 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3109 = _M0L4varsS923->$6;
    int32_t _M0L6_2atmpS3108;
    #line 353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS3108 = _M0MPC15array5Array2atGbE(_M0L6activeS3109, 0);
    _if__result_6010 = !_M0L6_2atmpS3108;
  } else {
    _if__result_6010 = 0;
  }
  if (_if__result_6010) {
    return 0;
  }
  _M0L6n__preS924 = _M0L4varsS923->$0;
  _M0L3rhoS3113 = _M0L3synS925->$6;
  #line 357 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3112 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS3113);
  if (_M0L6_2atmpS3112 == 0) {
    return 0;
  }
  _M0L1jS926
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS926)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS926->$0 = 0;
  while (1) {
    int32_t _M0L3valS3114 = _M0L1jS926->$0;
    if (_M0L3valS3114 < _M0L6n__preS924) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3117 = _M0L3synS925->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3115 = _M0L3preS3117->$5;
      int32_t _M0L3valS3116 = _M0L1jS926->$0;
      int32_t _M0L3valS3199;
      int32_t _M0L6_2atmpS3198;
      #line 362 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3115, _M0L3valS3116)) {
        struct _M0TPB5ArrayGfE* _M0L6tau__dS3196 = _M0L5paramS928->$0;
        int32_t _M0L3valS3197 = _M0L1jS926->$0;
        float _M0L9tau__d__jS927;
        struct _M0TPB5ArrayGfE* _M0L6tau__fS3194;
        int32_t _M0L3valS3195;
        float _M0L9tau__f__jS929;
        struct _M0TPB5ArrayGfE* _M0L1uS3192;
        int32_t _M0L3valS3193;
        float _M0L14u__baseline__jS930;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS3190;
        int32_t _M0L3valS3191;
        float _M0L6_2atmpS3189;
        float _M0L7dt__preS931;
        float _M0L7dt__preS933;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS3118;
        int32_t _M0L3valS3119;
        float _M0L6_2atmpS3188;
        float _M0L6arg__fS934;
        struct _M0TPB5ArrayGfE* _M0L1uS3120;
        int32_t _M0L3valS3121;
        struct _M0TPB5ArrayGfE* _M0L1uS3127;
        int32_t _M0L3valS3128;
        float _M0L6_2atmpS3126;
        float _M0L6_2atmpS3124;
        float _M0L6_2atmpS3125;
        float _M0L6_2atmpS3123;
        float _M0L6_2atmpS3122;
        float _M0L6_2atmpS3187;
        float _M0L6arg__dS935;
        struct _M0TPB5ArrayGfE* _M0L1xS3129;
        int32_t _M0L3valS3130;
        struct _M0TPB5ArrayGfE* _M0L1xS3136;
        int32_t _M0L3valS3137;
        float _M0L6_2atmpS3135;
        float _M0L6_2atmpS3133;
        float _M0L6_2atmpS3134;
        float _M0L6_2atmpS3132;
        float _M0L6_2atmpS3131;
        struct _M0TPB5ArrayGfE* _M0L8rho__preS3138;
        int32_t _M0L3valS3139;
        struct _M0TPB5ArrayGfE* _M0L1uS3145;
        int32_t _M0L3valS3146;
        float _M0L6_2atmpS3141;
        struct _M0TPB5ArrayGfE* _M0L1xS3143;
        int32_t _M0L3valS3144;
        float _M0L6_2atmpS3142;
        float _M0L6_2atmpS3140;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3186;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3184;
        int32_t _M0L3valS3185;
        int32_t _M0L5startS936;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3183;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3180;
        int32_t _M0L3valS3182;
        int32_t _M0L6_2atmpS3181;
        int32_t _M0L3endS937;
        struct _M0TPB8MutLocalGiE* _M0L1sS938;
        struct _M0TPB5ArrayGfE* _M0L1uS3155;
        int32_t _M0L3valS3156;
        struct _M0TPB5ArrayGfE* _M0L1uS3164;
        int32_t _M0L3valS3165;
        float _M0L6_2atmpS3158;
        struct _M0TPB5ArrayGfE* _M0L1uS3162;
        int32_t _M0L3valS3163;
        float _M0L6_2atmpS3161;
        float _M0L6_2atmpS3160;
        float _M0L6_2atmpS3159;
        float _M0L6_2atmpS3157;
        struct _M0TPB5ArrayGfE* _M0L1xS3166;
        int32_t _M0L3valS3167;
        struct _M0TPB5ArrayGfE* _M0L1xS3178;
        int32_t _M0L3valS3179;
        float _M0L6_2atmpS3169;
        struct _M0TPB5ArrayGfE* _M0L1uS3176;
        int32_t _M0L3valS3177;
        float _M0L6_2atmpS3175;
        float _M0L6_2atmpS3171;
        struct _M0TPB5ArrayGfE* _M0L1xS3173;
        int32_t _M0L3valS3174;
        float _M0L6_2atmpS3172;
        float _M0L6_2atmpS3170;
        float _M0L6_2atmpS3168;
        #line 363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L9tau__d__jS927
        = _M0MPC15array5Array2atGfE(_M0L6tau__dS3196, _M0L3valS3197);
        _M0L6tau__fS3194 = _M0L5paramS928->$1;
        _M0L3valS3195 = _M0L1jS926->$0;
        #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L9tau__f__jS929
        = _M0MPC15array5Array2atGfE(_M0L6tau__fS3194, _M0L3valS3195);
        _M0L1uS3192 = _M0L5paramS928->$2;
        _M0L3valS3193 = _M0L1jS926->$0;
        #line 365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L14u__baseline__jS930
        = _M0MPC15array5Array2atGfE(_M0L1uS3192, _M0L3valS3193);
        _M0L11last__spikeS3190 = _M0L4varsS923->$5;
        _M0L3valS3191 = _M0L1jS926->$0;
        #line 366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3189
        = _M0MPC15array5Array2atGfE(_M0L11last__spikeS3190, _M0L3valS3191);
        _M0L7dt__preS931 = _M0L6t__nowS932 - _M0L6_2atmpS3189;
        if (_M0L7dt__preS931 < 0x0p+0f) {
          _M0L7dt__preS933 = 0x0p+0f;
        } else {
          _M0L7dt__preS933 = _M0L7dt__preS931;
        }
        _M0L11last__spikeS3118 = _M0L4varsS923->$5;
        _M0L3valS3119 = _M0L1jS926->$0;
        #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L11last__spikeS3118, _M0L3valS3119, _M0L6t__nowS932);
        _M0L6_2atmpS3188 = -_M0L7dt__preS933;
        _M0L6arg__fS934 = _M0L6_2atmpS3188 / _M0L9tau__f__jS929;
        _M0L1uS3120 = _M0L4varsS923->$2;
        _M0L3valS3121 = _M0L1jS926->$0;
        _M0L1uS3127 = _M0L4varsS923->$2;
        _M0L3valS3128 = _M0L1jS926->$0;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3126
        = _M0MPC15array5Array2atGfE(_M0L1uS3127, _M0L3valS3128);
        _M0L6_2atmpS3124 = _M0L14u__baseline__jS930 - _M0L6_2atmpS3126;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3125 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__fS934);
        _M0L6_2atmpS3123 = _M0L6_2atmpS3124 * _M0L6_2atmpS3125;
        _M0L6_2atmpS3122 = _M0L14u__baseline__jS930 - _M0L6_2atmpS3123;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3120, _M0L3valS3121, _M0L6_2atmpS3122);
        _M0L6_2atmpS3187 = -_M0L7dt__preS933;
        _M0L6arg__dS935 = _M0L6_2atmpS3187 / _M0L9tau__d__jS927;
        _M0L1xS3129 = _M0L4varsS923->$3;
        _M0L3valS3130 = _M0L1jS926->$0;
        _M0L1xS3136 = _M0L4varsS923->$3;
        _M0L3valS3137 = _M0L1jS926->$0;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3135
        = _M0MPC15array5Array2atGfE(_M0L1xS3136, _M0L3valS3137);
        _M0L6_2atmpS3133 = 0x1p+0f - _M0L6_2atmpS3135;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3134 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__dS935);
        _M0L6_2atmpS3132 = _M0L6_2atmpS3133 * _M0L6_2atmpS3134;
        _M0L6_2atmpS3131 = 0x1p+0f - _M0L6_2atmpS3132;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3129, _M0L3valS3130, _M0L6_2atmpS3131);
        _M0L8rho__preS3138 = _M0L4varsS923->$4;
        _M0L3valS3139 = _M0L1jS926->$0;
        _M0L1uS3145 = _M0L4varsS923->$2;
        _M0L3valS3146 = _M0L1jS926->$0;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3141
        = _M0MPC15array5Array2atGfE(_M0L1uS3145, _M0L3valS3146);
        _M0L1xS3143 = _M0L4varsS923->$3;
        _M0L3valS3144 = _M0L1jS926->$0;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3142
        = _M0MPC15array5Array2atGfE(_M0L1xS3143, _M0L3valS3144);
        _M0L6_2atmpS3140 = _M0L6_2atmpS3141 * _M0L6_2atmpS3142;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L8rho__preS3138, _M0L3valS3139, _M0L6_2atmpS3140);
        _M0L6matrixS3186 = _M0L3synS925->$4;
        _M0L6rowptrS3184 = _M0L6matrixS3186->$2;
        _M0L3valS3185 = _M0L1jS926->$0;
        #line 374 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L5startS936
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3184, _M0L3valS3185);
        _M0L6matrixS3183 = _M0L3synS925->$4;
        _M0L6rowptrS3180 = _M0L6matrixS3183->$2;
        _M0L3valS3182 = _M0L1jS926->$0;
        _M0L6_2atmpS3181 = _M0L3valS3182 + 1;
        #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L3endS937
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3180, _M0L6_2atmpS3181);
        _M0L1sS938
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS938)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS938->$0 = _M0L5startS936;
        while (1) {
          int32_t _M0L3valS3147 = _M0L1sS938->$0;
          if (_M0L3valS3147 < _M0L3endS937) {
            struct _M0TPB5ArrayGfE* _M0L3rhoS3148 = _M0L3synS925->$6;
            int32_t _M0L3valS3149 = _M0L1sS938->$0;
            struct _M0TPB5ArrayGfE* _M0L8rho__preS3151 = _M0L4varsS923->$4;
            int32_t _M0L3valS3152 = _M0L1jS926->$0;
            float _M0L6_2atmpS3150;
            int32_t _M0L3valS3154;
            int32_t _M0L6_2atmpS3153;
            #line 378 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0L6_2atmpS3150
            = _M0MPC15array5Array2atGfE(_M0L8rho__preS3151, _M0L3valS3152);
            #line 378 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0MPC15array5Array3setGfE(_M0L3rhoS3148, _M0L3valS3149, _M0L6_2atmpS3150);
            _M0L3valS3154 = _M0L1sS938->$0;
            _M0L6_2atmpS3153 = _M0L3valS3154 + 1;
            _M0L1sS938->$0 = _M0L6_2atmpS3153;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS938);
          }
          break;
        }
        _M0L1uS3155 = _M0L4varsS923->$2;
        _M0L3valS3156 = _M0L1jS926->$0;
        _M0L1uS3164 = _M0L4varsS923->$2;
        _M0L3valS3165 = _M0L1jS926->$0;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3158
        = _M0MPC15array5Array2atGfE(_M0L1uS3164, _M0L3valS3165);
        _M0L1uS3162 = _M0L4varsS923->$2;
        _M0L3valS3163 = _M0L1jS926->$0;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3161
        = _M0MPC15array5Array2atGfE(_M0L1uS3162, _M0L3valS3163);
        _M0L6_2atmpS3160 = 0x1p+0f - _M0L6_2atmpS3161;
        _M0L6_2atmpS3159 = _M0L14u__baseline__jS930 * _M0L6_2atmpS3160;
        _M0L6_2atmpS3157 = _M0L6_2atmpS3158 + _M0L6_2atmpS3159;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3155, _M0L3valS3156, _M0L6_2atmpS3157);
        _M0L1xS3166 = _M0L4varsS923->$3;
        _M0L3valS3167 = _M0L1jS926->$0;
        _M0L1xS3178 = _M0L4varsS923->$3;
        _M0L3valS3179 = _M0L1jS926->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3169
        = _M0MPC15array5Array2atGfE(_M0L1xS3178, _M0L3valS3179);
        _M0L1uS3176 = _M0L4varsS923->$2;
        _M0L3valS3177 = _M0L1jS926->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3175
        = _M0MPC15array5Array2atGfE(_M0L1uS3176, _M0L3valS3177);
        _M0L6_2atmpS3171 = -_M0L6_2atmpS3175;
        _M0L1xS3173 = _M0L4varsS923->$3;
        _M0L3valS3174 = _M0L1jS926->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3172
        = _M0MPC15array5Array2atGfE(_M0L1xS3173, _M0L3valS3174);
        _M0L6_2atmpS3170 = _M0L6_2atmpS3171 * _M0L6_2atmpS3172;
        _M0L6_2atmpS3168 = _M0L6_2atmpS3169 + _M0L6_2atmpS3170;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3166, _M0L3valS3167, _M0L6_2atmpS3168);
      }
      _M0L3valS3199 = _M0L1jS926->$0;
      _M0L6_2atmpS3198 = _M0L3valS3199 + 1;
      _M0L1jS926->$0 = _M0L6_2atmpS3198;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS926);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt18markram__stp__step(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS907,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS905,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter* _M0L5paramS909,
  float _M0L6t__nowS914
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3025;
  int32_t _M0L6_2atmpS3024;
  int32_t _if__result_6013;
  int32_t _M0L6n__preS906;
  struct _M0TPB5ArrayGfE* _M0L3rhoS3027;
  int32_t _M0L6_2atmpS3026;
  float _M0L6tau__fS908;
  float _M0L6tau__dS910;
  float _M0L11u__baselineS911;
  struct _M0TPB8MutLocalGiE* _M0L1jS912;
  #line 202 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS3025 = _M0L4varsS905->$6;
  #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3024 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3025);
  if (_M0L6_2atmpS3024 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3023 = _M0L4varsS905->$6;
    int32_t _M0L6_2atmpS3022;
    #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS3022 = _M0MPC15array5Array2atGbE(_M0L6activeS3023, 0);
    _if__result_6013 = !_M0L6_2atmpS3022;
  } else {
    _if__result_6013 = 0;
  }
  if (_if__result_6013) {
    return 0;
  }
  _M0L6n__preS906 = _M0L4varsS905->$0;
  _M0L3rhoS3027 = _M0L3synS907->$6;
  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3026 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS3027);
  if (_M0L6_2atmpS3026 == 0) {
    return 0;
  }
  _M0L6tau__fS908 = _M0L5paramS909->$1;
  _M0L6tau__dS910 = _M0L5paramS909->$0;
  _M0L11u__baselineS911 = _M0L5paramS909->$2;
  _M0L1jS912
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS912)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS912->$0 = 0;
  while (1) {
    int32_t _M0L3valS3028 = _M0L1jS912->$0;
    if (_M0L3valS3028 < _M0L6n__preS906) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3031 = _M0L3synS907->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3029 = _M0L3preS3031->$5;
      int32_t _M0L3valS3030 = _M0L1jS912->$0;
      int32_t _M0L3valS3107;
      int32_t _M0L6_2atmpS3106;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3029, _M0L3valS3030)) {
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS3104 = _M0L4varsS905->$5;
        int32_t _M0L3valS3105 = _M0L1jS912->$0;
        float _M0L6_2atmpS3103;
        float _M0L7dt__preS913;
        float _M0L7dt__preS915;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS3032;
        int32_t _M0L3valS3033;
        float _M0L6_2atmpS3102;
        float _M0L6arg__fS916;
        struct _M0TPB5ArrayGfE* _M0L1uS3034;
        int32_t _M0L3valS3035;
        struct _M0TPB5ArrayGfE* _M0L1uS3041;
        int32_t _M0L3valS3042;
        float _M0L6_2atmpS3040;
        float _M0L6_2atmpS3038;
        float _M0L6_2atmpS3039;
        float _M0L6_2atmpS3037;
        float _M0L6_2atmpS3036;
        float _M0L6_2atmpS3101;
        float _M0L6arg__dS917;
        struct _M0TPB5ArrayGfE* _M0L1xS3043;
        int32_t _M0L3valS3044;
        struct _M0TPB5ArrayGfE* _M0L1xS3050;
        int32_t _M0L3valS3051;
        float _M0L6_2atmpS3049;
        float _M0L6_2atmpS3047;
        float _M0L6_2atmpS3048;
        float _M0L6_2atmpS3046;
        float _M0L6_2atmpS3045;
        struct _M0TPB5ArrayGfE* _M0L8rho__preS3052;
        int32_t _M0L3valS3053;
        struct _M0TPB5ArrayGfE* _M0L1uS3059;
        int32_t _M0L3valS3060;
        float _M0L6_2atmpS3055;
        struct _M0TPB5ArrayGfE* _M0L1xS3057;
        int32_t _M0L3valS3058;
        float _M0L6_2atmpS3056;
        float _M0L6_2atmpS3054;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3100;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3098;
        int32_t _M0L3valS3099;
        int32_t _M0L5startS918;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3097;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3094;
        int32_t _M0L3valS3096;
        int32_t _M0L6_2atmpS3095;
        int32_t _M0L3endS919;
        struct _M0TPB8MutLocalGiE* _M0L1sS920;
        struct _M0TPB5ArrayGfE* _M0L1uS3069;
        int32_t _M0L3valS3070;
        struct _M0TPB5ArrayGfE* _M0L1uS3078;
        int32_t _M0L3valS3079;
        float _M0L6_2atmpS3072;
        struct _M0TPB5ArrayGfE* _M0L1uS3076;
        int32_t _M0L3valS3077;
        float _M0L6_2atmpS3075;
        float _M0L6_2atmpS3074;
        float _M0L6_2atmpS3073;
        float _M0L6_2atmpS3071;
        struct _M0TPB5ArrayGfE* _M0L1xS3080;
        int32_t _M0L3valS3081;
        struct _M0TPB5ArrayGfE* _M0L1xS3092;
        int32_t _M0L3valS3093;
        float _M0L6_2atmpS3083;
        struct _M0TPB5ArrayGfE* _M0L1uS3090;
        int32_t _M0L3valS3091;
        float _M0L6_2atmpS3089;
        float _M0L6_2atmpS3085;
        struct _M0TPB5ArrayGfE* _M0L1xS3087;
        int32_t _M0L3valS3088;
        float _M0L6_2atmpS3086;
        float _M0L6_2atmpS3084;
        float _M0L6_2atmpS3082;
        #line 224 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3103
        = _M0MPC15array5Array2atGfE(_M0L11last__spikeS3104, _M0L3valS3105);
        _M0L7dt__preS913 = _M0L6t__nowS914 - _M0L6_2atmpS3103;
        if (_M0L7dt__preS913 < 0x0p+0f) {
          _M0L7dt__preS915 = 0x0p+0f;
        } else {
          _M0L7dt__preS915 = _M0L7dt__preS913;
        }
        _M0L11last__spikeS3032 = _M0L4varsS905->$5;
        _M0L3valS3033 = _M0L1jS912->$0;
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L11last__spikeS3032, _M0L3valS3033, _M0L6t__nowS914);
        _M0L6_2atmpS3102 = -_M0L7dt__preS915;
        _M0L6arg__fS916 = _M0L6_2atmpS3102 / _M0L6tau__fS908;
        _M0L1uS3034 = _M0L4varsS905->$2;
        _M0L3valS3035 = _M0L1jS912->$0;
        _M0L1uS3041 = _M0L4varsS905->$2;
        _M0L3valS3042 = _M0L1jS912->$0;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3040
        = _M0MPC15array5Array2atGfE(_M0L1uS3041, _M0L3valS3042);
        _M0L6_2atmpS3038 = _M0L11u__baselineS911 - _M0L6_2atmpS3040;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3039 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__fS916);
        _M0L6_2atmpS3037 = _M0L6_2atmpS3038 * _M0L6_2atmpS3039;
        _M0L6_2atmpS3036 = _M0L11u__baselineS911 - _M0L6_2atmpS3037;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3034, _M0L3valS3035, _M0L6_2atmpS3036);
        _M0L6_2atmpS3101 = -_M0L7dt__preS915;
        _M0L6arg__dS917 = _M0L6_2atmpS3101 / _M0L6tau__dS910;
        _M0L1xS3043 = _M0L4varsS905->$3;
        _M0L3valS3044 = _M0L1jS912->$0;
        _M0L1xS3050 = _M0L4varsS905->$3;
        _M0L3valS3051 = _M0L1jS912->$0;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3049
        = _M0MPC15array5Array2atGfE(_M0L1xS3050, _M0L3valS3051);
        _M0L6_2atmpS3047 = 0x1p+0f - _M0L6_2atmpS3049;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3048 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__dS917);
        _M0L6_2atmpS3046 = _M0L6_2atmpS3047 * _M0L6_2atmpS3048;
        _M0L6_2atmpS3045 = 0x1p+0f - _M0L6_2atmpS3046;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3043, _M0L3valS3044, _M0L6_2atmpS3045);
        _M0L8rho__preS3052 = _M0L4varsS905->$4;
        _M0L3valS3053 = _M0L1jS912->$0;
        _M0L1uS3059 = _M0L4varsS905->$2;
        _M0L3valS3060 = _M0L1jS912->$0;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3055
        = _M0MPC15array5Array2atGfE(_M0L1uS3059, _M0L3valS3060);
        _M0L1xS3057 = _M0L4varsS905->$3;
        _M0L3valS3058 = _M0L1jS912->$0;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3056
        = _M0MPC15array5Array2atGfE(_M0L1xS3057, _M0L3valS3058);
        _M0L6_2atmpS3054 = _M0L6_2atmpS3055 * _M0L6_2atmpS3056;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L8rho__preS3052, _M0L3valS3053, _M0L6_2atmpS3054);
        _M0L6matrixS3100 = _M0L3synS907->$4;
        _M0L6rowptrS3098 = _M0L6matrixS3100->$2;
        _M0L3valS3099 = _M0L1jS912->$0;
        #line 236 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L5startS918
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3098, _M0L3valS3099);
        _M0L6matrixS3097 = _M0L3synS907->$4;
        _M0L6rowptrS3094 = _M0L6matrixS3097->$2;
        _M0L3valS3096 = _M0L1jS912->$0;
        _M0L6_2atmpS3095 = _M0L3valS3096 + 1;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L3endS919
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3094, _M0L6_2atmpS3095);
        _M0L1sS920
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS920)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS920->$0 = _M0L5startS918;
        while (1) {
          int32_t _M0L3valS3061 = _M0L1sS920->$0;
          if (_M0L3valS3061 < _M0L3endS919) {
            struct _M0TPB5ArrayGfE* _M0L3rhoS3062 = _M0L3synS907->$6;
            int32_t _M0L3valS3063 = _M0L1sS920->$0;
            struct _M0TPB5ArrayGfE* _M0L8rho__preS3065 = _M0L4varsS905->$4;
            int32_t _M0L3valS3066 = _M0L1jS912->$0;
            float _M0L6_2atmpS3064;
            int32_t _M0L3valS3068;
            int32_t _M0L6_2atmpS3067;
            #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0L6_2atmpS3064
            = _M0MPC15array5Array2atGfE(_M0L8rho__preS3065, _M0L3valS3066);
            #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0MPC15array5Array3setGfE(_M0L3rhoS3062, _M0L3valS3063, _M0L6_2atmpS3064);
            _M0L3valS3068 = _M0L1sS920->$0;
            _M0L6_2atmpS3067 = _M0L3valS3068 + 1;
            _M0L1sS920->$0 = _M0L6_2atmpS3067;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS920);
          }
          break;
        }
        _M0L1uS3069 = _M0L4varsS905->$2;
        _M0L3valS3070 = _M0L1jS912->$0;
        _M0L1uS3078 = _M0L4varsS905->$2;
        _M0L3valS3079 = _M0L1jS912->$0;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3072
        = _M0MPC15array5Array2atGfE(_M0L1uS3078, _M0L3valS3079);
        _M0L1uS3076 = _M0L4varsS905->$2;
        _M0L3valS3077 = _M0L1jS912->$0;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3075
        = _M0MPC15array5Array2atGfE(_M0L1uS3076, _M0L3valS3077);
        _M0L6_2atmpS3074 = 0x1p+0f - _M0L6_2atmpS3075;
        _M0L6_2atmpS3073 = _M0L11u__baselineS911 * _M0L6_2atmpS3074;
        _M0L6_2atmpS3071 = _M0L6_2atmpS3072 + _M0L6_2atmpS3073;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3069, _M0L3valS3070, _M0L6_2atmpS3071);
        _M0L1xS3080 = _M0L4varsS905->$3;
        _M0L3valS3081 = _M0L1jS912->$0;
        _M0L1xS3092 = _M0L4varsS905->$3;
        _M0L3valS3093 = _M0L1jS912->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3083
        = _M0MPC15array5Array2atGfE(_M0L1xS3092, _M0L3valS3093);
        _M0L1uS3090 = _M0L4varsS905->$2;
        _M0L3valS3091 = _M0L1jS912->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3089
        = _M0MPC15array5Array2atGfE(_M0L1uS3090, _M0L3valS3091);
        _M0L6_2atmpS3085 = -_M0L6_2atmpS3089;
        _M0L1xS3087 = _M0L4varsS905->$3;
        _M0L3valS3088 = _M0L1jS912->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3086
        = _M0MPC15array5Array2atGfE(_M0L1xS3087, _M0L3valS3088);
        _M0L6_2atmpS3084 = _M0L6_2atmpS3085 * _M0L6_2atmpS3086;
        _M0L6_2atmpS3082 = _M0L6_2atmpS3083 + _M0L6_2atmpS3084;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3080, _M0L3valS3081, _M0L6_2atmpS3082);
      }
      _M0L3valS3107 = _M0L1jS912->$0;
      _M0L6_2atmpS3106 = _M0L3valS3107 + 1;
      _M0L1jS912->$0 = _M0L6_2atmpS3106;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS912);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12update__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS903,
  float _M0L2dtS904
) {
  struct _M0TPB5ArrayGfE* _M0L1tS3014;
  struct _M0TPB5ArrayGfE* _M0L1tS3017;
  float _M0L6_2atmpS3016;
  float _M0L6_2atmpS3015;
  struct _M0TPB5ArrayGiE* _M0L2ttS3018;
  struct _M0TPB5ArrayGiE* _M0L2ttS3021;
  int32_t _M0L6_2atmpS3020;
  int32_t _M0L6_2atmpS3019;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS3014 = _M0L1tS903->$0;
  _M0L1tS3017 = _M0L1tS903->$0;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS3016 = _M0MPC15array5Array2atGfE(_M0L1tS3017, 0);
  _M0L6_2atmpS3015 = _M0L6_2atmpS3016 + _M0L2dtS904;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGfE(_M0L1tS3014, 0, _M0L6_2atmpS3015);
  _M0L2ttS3018 = _M0L1tS903->$1;
  _M0L2ttS3021 = _M0L1tS903->$1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS3020 = _M0MPC15array5Array2atGiE(_M0L2ttS3021, 0);
  _M0L6_2atmpS3019 = _M0L6_2atmpS3020 + 1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGiE(_M0L2ttS3018, 0, _M0L6_2atmpS3019);
  return 0;
}

float _M0FP26RiantR8snn__mbt9get__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS902
) {
  struct _M0TPB5ArrayGfE* _M0L1tS3013;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS3013 = _M0L1tS902->$0;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  return _M0MPC15array5Array2atGfE(_M0L1tS3013, 0);
}

struct _M0TP26RiantR8snn__mbt4Time* _M0MP26RiantR8snn__mbt4Time3new() {
  float* _M0L6_2atmpS3012;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS3009;
  int32_t* _M0L6_2atmpS3011;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS3010;
  struct _M0TP26RiantR8snn__mbt4Time* _block_6016;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS3012 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS3012[0] = 0x0p+0f;
  _M0L6_2atmpS3009
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS3009)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _M0L6_2atmpS3009->$0 = _M0L6_2atmpS3012;
  _M0L6_2atmpS3009->$1 = 1;
  _M0L6_2atmpS3011 = (int32_t*)moonbit_make_int32_array_raw(1);
  _M0L6_2atmpS3011[0] = 0;
  _M0L6_2atmpS3010
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS3010)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _M0L6_2atmpS3010->$0 = _M0L6_2atmpS3011;
  _M0L6_2atmpS3010->$1 = 1;
  _block_6016
  = (struct _M0TP26RiantR8snn__mbt4Time*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt4Time));
  Moonbit_object_header(_block_6016)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 95, 0);
  _block_6016->$0 = _M0L6_2atmpS3009;
  _block_6016->$1 = _M0L6_2atmpS3010;
  _block_6016->$2 = 0x1p-3f;
  return _block_6016;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS900
) {
  uint32_t _M0L1uS899;
  uint32_t _M0L4bitsS901;
  double _M0L6_2atmpS3008;
  double _M0L6_2atmpS3007;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS899 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS900);
  _M0L4bitsS901 = _M0L1uS899 >> 8;
  _M0L6_2atmpS3008 = (double)_M0L4bitsS901;
  _M0L6_2atmpS3007 = _M0L6_2atmpS3008 * 0x1p-24;
  return (float)_M0L6_2atmpS3007;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS898
) {
  uint64_t _M0L1uS897;
  uint64_t _M0L6_2atmpS3006;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS897 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS898);
  _M0L6_2atmpS3006 = _M0L1uS897 >> 32;
  return (uint32_t)_M0L6_2atmpS3006;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS890
) {
  uint64_t _M0L2s0S889;
  uint64_t _M0L2s1S891;
  uint64_t _M0L2s2S892;
  uint64_t _M0L2s3S893;
  uint64_t _M0L3tmpS894;
  uint64_t _M0L6_2atmpS3005;
  uint64_t _M0L3resS895;
  uint64_t _M0L1tS896;
  uint64_t _M0L6_2atmpS2995;
  uint64_t _M0L6_2atmpS2996;
  uint64_t _M0L2s2S2998;
  uint64_t _M0L6_2atmpS2997;
  uint64_t _M0L2s3S3000;
  uint64_t _M0L6_2atmpS2999;
  uint64_t _M0L2s2S3002;
  uint64_t _M0L6_2atmpS3001;
  uint64_t _M0L2s3S3004;
  uint64_t _M0L6_2atmpS3003;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S889 = _M0L1rS890->$0;
  _M0L2s1S891 = _M0L1rS890->$1;
  _M0L2s2S892 = _M0L1rS890->$2;
  _M0L2s3S893 = _M0L1rS890->$3;
  _M0L3tmpS894 = _M0L2s0S889 + _M0L2s3S893;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS3005 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS894, 23);
  _M0L3resS895 = _M0L6_2atmpS3005 + _M0L2s0S889;
  _M0L1tS896 = _M0L2s1S891 << 17;
  _M0L6_2atmpS2995 = _M0L2s2S892 ^ _M0L2s0S889;
  _M0L1rS890->$2 = _M0L6_2atmpS2995;
  _M0L6_2atmpS2996 = _M0L2s3S893 ^ _M0L2s1S891;
  _M0L1rS890->$3 = _M0L6_2atmpS2996;
  _M0L2s2S2998 = _M0L1rS890->$2;
  _M0L6_2atmpS2997 = _M0L2s1S891 ^ _M0L2s2S2998;
  _M0L1rS890->$1 = _M0L6_2atmpS2997;
  _M0L2s3S3000 = _M0L1rS890->$3;
  _M0L6_2atmpS2999 = _M0L2s0S889 ^ _M0L2s3S3000;
  _M0L1rS890->$0 = _M0L6_2atmpS2999;
  _M0L2s2S3002 = _M0L1rS890->$2;
  _M0L6_2atmpS3001 = _M0L2s2S3002 ^ _M0L1tS896;
  _M0L1rS890->$2 = _M0L6_2atmpS3001;
  _M0L2s3S3004 = _M0L1rS890->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS3003 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S3004, 45);
  _M0L1rS890->$3 = _M0L6_2atmpS3003;
  return _M0L3resS895;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS887, int32_t _M0L1kS888) {
  uint64_t _M0L6_2atmpS2992;
  int32_t _M0L6_2atmpS2994;
  uint64_t _M0L6_2atmpS2993;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2992 = _M0L1xS887 << (_M0L1kS888 & 63);
  _M0L6_2atmpS2994 = 64 - _M0L1kS888;
  _M0L6_2atmpS2993 = _M0L1xS887 >> (_M0L6_2atmpS2994 & 63);
  return _M0L6_2atmpS2992 | _M0L6_2atmpS2993;
}

double _M0FPC14math2ln(double _M0L1xS873) {
  struct _M0TUdiE* _M0L7_2abindS874;
  double _M0L5_2af1S875;
  int32_t _M0L5_2akiS876;
  double _M0L1fS878;
  double _M0L1kS879;
  double _M0L6_2atmpS2985;
  double _M0L1sS880;
  double _M0L2s2S881;
  double _M0L2s4S882;
  double _M0L6_2atmpS2984;
  double _M0L6_2atmpS2983;
  double _M0L6_2atmpS2982;
  double _M0L6_2atmpS2981;
  double _M0L6_2atmpS2980;
  double _M0L6_2atmpS2979;
  double _M0L2t1S883;
  double _M0L6_2atmpS2978;
  double _M0L6_2atmpS2977;
  double _M0L6_2atmpS2976;
  double _M0L6_2atmpS2975;
  double _M0L2t2S884;
  double _M0L1rS885;
  double _M0L6_2atmpS2974;
  double _M0L4hfsqS886;
  double _M0L6_2atmpS2967;
  double _M0L6_2atmpS2973;
  double _M0L6_2atmpS2971;
  double _M0L6_2atmpS2972;
  double _M0L6_2atmpS2970;
  double _M0L6_2atmpS2969;
  double _M0L6_2atmpS2968;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS873 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS873)
      || _M0MPC16double6Double7is__inf(_M0L1xS873)
    ) {
      return _M0L1xS873;
    } else if (_M0L1xS873 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS874 = _M0FPC14math5frexp(_M0L1xS873);
  _M0L5_2af1S875 = _M0L7_2abindS874->$0;
  _M0L5_2akiS876 = _M0L7_2abindS874->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS874);
  if (_M0L5_2af1S875 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS2989 = _M0L5_2af1S875 * 0x1p+1;
    double _M0L6_2atmpS2986 = _M0L6_2atmpS2989 - 0x1p+0;
    int32_t _M0L6_2atmpS2988 = _M0L5_2akiS876 - 1;
    double _M0L6_2atmpS2987 = (double)_M0L6_2atmpS2988;
    _M0L1fS878 = _M0L6_2atmpS2986;
    _M0L1kS879 = _M0L6_2atmpS2987;
    goto join_877;
  } else {
    double _M0L6_2atmpS2990 = _M0L5_2af1S875 - 0x1p+0;
    double _M0L6_2atmpS2991 = (double)_M0L5_2akiS876;
    _M0L1fS878 = _M0L6_2atmpS2990;
    _M0L1kS879 = _M0L6_2atmpS2991;
    goto join_877;
  }
  join_877:;
  _M0L6_2atmpS2985 = 0x1p+1 + _M0L1fS878;
  _M0L1sS880 = _M0L1fS878 / _M0L6_2atmpS2985;
  _M0L2s2S881 = _M0L1sS880 * _M0L1sS880;
  _M0L2s4S882 = _M0L2s2S881 * _M0L2s2S881;
  _M0L6_2atmpS2984 = _M0L2s4S882 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS2983 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS2984;
  _M0L6_2atmpS2982 = _M0L2s4S882 * _M0L6_2atmpS2983;
  _M0L6_2atmpS2981 = 0x1.2492494229359p-2 + _M0L6_2atmpS2982;
  _M0L6_2atmpS2980 = _M0L2s4S882 * _M0L6_2atmpS2981;
  _M0L6_2atmpS2979 = 0x1.5555555555593p-1 + _M0L6_2atmpS2980;
  _M0L2t1S883 = _M0L2s2S881 * _M0L6_2atmpS2979;
  _M0L6_2atmpS2978 = _M0L2s4S882 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS2977 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS2978;
  _M0L6_2atmpS2976 = _M0L2s4S882 * _M0L6_2atmpS2977;
  _M0L6_2atmpS2975 = 0x1.999999997fa04p-2 + _M0L6_2atmpS2976;
  _M0L2t2S884 = _M0L2s4S882 * _M0L6_2atmpS2975;
  _M0L1rS885 = _M0L2t1S883 + _M0L2t2S884;
  _M0L6_2atmpS2974 = 0x1p-1 * _M0L1fS878;
  _M0L4hfsqS886 = _M0L6_2atmpS2974 * _M0L1fS878;
  _M0L6_2atmpS2967 = _M0L1kS879 * 0x1.62e42feep-1;
  _M0L6_2atmpS2973 = _M0L4hfsqS886 + _M0L1rS885;
  _M0L6_2atmpS2971 = _M0L1sS880 * _M0L6_2atmpS2973;
  _M0L6_2atmpS2972 = _M0L1kS879 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS2970 = _M0L6_2atmpS2971 + _M0L6_2atmpS2972;
  _M0L6_2atmpS2969 = _M0L4hfsqS886 - _M0L6_2atmpS2970;
  _M0L6_2atmpS2968 = _M0L6_2atmpS2969 - _M0L1fS878;
  return _M0L6_2atmpS2967 - _M0L6_2atmpS2968;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS866) {
  struct _M0TUdiE* _M0L7_2abindS867;
  double _M0L10_2anorm__fS868;
  int32_t _M0L6_2aexpS869;
  uint64_t _M0L1uS870;
  uint64_t _M0L6_2atmpS2966;
  uint64_t _M0L6_2atmpS2965;
  int32_t _M0L6_2atmpS2964;
  int32_t _M0L6_2atmpS2963;
  int32_t _M0L3expS871;
  uint64_t _M0L6_2atmpS2962;
  uint64_t _M0L6_2atmpS2961;
  uint64_t _M0L6_2atmpS2960;
  double _M0L4fracS872;
  struct _M0TUdiE* _block_6019;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS866 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS866)
    || _M0MPC16double6Double7is__nan(_M0L1fS866)
  ) {
    struct _M0TUdiE* _block_6018 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_6018)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_6018->$0 = _M0L1fS866;
    _block_6018->$1 = 0;
    return _block_6018;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS867 = _M0FPC14math9normalize(_M0L1fS866);
  _M0L10_2anorm__fS868 = _M0L7_2abindS867->$0;
  _M0L6_2aexpS869 = _M0L7_2abindS867->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS867);
  _M0L1uS870 = *(int64_t*)&_M0L10_2anorm__fS868;
  _M0L6_2atmpS2966 = _M0L1uS870 >> 52;
  _M0L6_2atmpS2965 = _M0L6_2atmpS2966 & 2047ull;
  _M0L6_2atmpS2964 = (int32_t)_M0L6_2atmpS2965;
  _M0L6_2atmpS2963 = _M0L6_2aexpS869 + _M0L6_2atmpS2964;
  _M0L3expS871 = _M0L6_2atmpS2963 - 1022;
  _M0L6_2atmpS2962 = ~9218868437227405312ull;
  _M0L6_2atmpS2961 = _M0L1uS870 & _M0L6_2atmpS2962;
  _M0L6_2atmpS2960 = _M0L6_2atmpS2961 | 4602678819172646912ull;
  _M0L4fracS872 = *(double*)&_M0L6_2atmpS2960;
  _block_6019 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_6019)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6019->$0 = _M0L4fracS872;
  _block_6019->$1 = _M0L3expS871;
  return _block_6019;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS865) {
  double _M0L6_2atmpS2957;
  struct _M0TUdiE* _block_6021;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS2957 = fabs(_M0L1fS865);
  if (_M0L6_2atmpS2957 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS2959 = (double)4503599627370496ll;
    double _M0L6_2atmpS2958 = _M0L1fS865 * _M0L6_2atmpS2959;
    struct _M0TUdiE* _block_6020 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_6020)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_6020->$0 = _M0L6_2atmpS2958;
    _block_6020->$1 = -52;
    return _block_6020;
  }
  _block_6021 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_6021)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6021->$0 = _M0L1fS865;
  _block_6021->$1 = 0;
  return _block_6021;
}

int32_t _M0MPC15float5Float7is__nan(float _M0L4selfS864) {
  #line 208 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0L4selfS864 != _M0L4selfS864;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS863) {
  double _M0L6_2atmpS2956;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2956 = (double)_M0L4selfS863;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2956);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS862) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS862 != _M0L4selfS862) {
    return 0;
  } else if (_M0L4selfS862 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS862 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS862;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS848,
  float _M0L4elemS850
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS847;
  int32_t _M0L1iS849;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS847 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS848);
  _M0L1iS849 = 0;
  while (1) {
    if (_M0L1iS849 < _M0L3lenS848) {
      float* _M0L3bufS2950 = _M0L3arrS847->$0;
      int32_t _M0L6_2atmpS2951;
      _M0L3bufS2950[_M0L1iS849] = _M0L4elemS850;
      _M0L6_2atmpS2951 = _M0L1iS849 + 1;
      _M0L1iS849 = _M0L6_2atmpS2951;
      continue;
    }
    break;
  }
  return _M0L3arrS847;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS853,
  int32_t _M0L4elemS855
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS852;
  int32_t _M0L1iS854;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS852 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS853);
  _M0L1iS854 = 0;
  while (1) {
    if (_M0L1iS854 < _M0L3lenS853) {
      uint8_t* _M0L3bufS2952 = _M0L3arrS852->$0;
      int32_t _M0L6_2atmpS2953;
      _M0L3bufS2952[_M0L1iS854] = _M0L4elemS855;
      _M0L6_2atmpS2953 = _M0L1iS854 + 1;
      _M0L1iS854 = _M0L6_2atmpS2953;
      continue;
    }
    break;
  }
  return _M0L3arrS852;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS858,
  int32_t _M0L4elemS860
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS857;
  int32_t _M0L1iS859;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS857 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS858);
  _M0L1iS859 = 0;
  while (1) {
    if (_M0L1iS859 < _M0L3lenS858) {
      int32_t* _M0L3bufS2954 = _M0L3arrS857->$0;
      int32_t _M0L6_2atmpS2955;
      _M0L3bufS2954[_M0L1iS859] = _M0L4elemS860;
      _M0L6_2atmpS2955 = _M0L1iS859 + 1;
      _M0L1iS859 = _M0L6_2atmpS2955;
      continue;
    }
    break;
  }
  return _M0L3arrS857;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS836,
  int32_t _M0L5indexS837,
  float _M0L5valueS838
) {
  int32_t _M0L3lenS835;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS835 = _M0L4selfS836->$1;
  if (_M0L5indexS837 >= 0 && _M0L5indexS837 < _M0L3lenS835) {
    float* _M0L6_2atmpS2947;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2947 = _M0MPC15array5Array6bufferGfE(_M0L4selfS836);
    _M0L6_2atmpS2947[_M0L5indexS837] = _M0L5valueS838;
    moonbit_decref_cycle_free(_M0L6_2atmpS2947);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS840,
  int32_t _M0L5indexS841,
  int32_t _M0L5valueS842
) {
  int32_t _M0L3lenS839;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS839 = _M0L4selfS840->$1;
  if (_M0L5indexS841 >= 0 && _M0L5indexS841 < _M0L3lenS839) {
    int32_t* _M0L6_2atmpS2948;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2948 = _M0MPC15array5Array6bufferGiE(_M0L4selfS840);
    _M0L6_2atmpS2948[_M0L5indexS841] = _M0L5valueS842;
    moonbit_decref_cycle_free(_M0L6_2atmpS2948);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS844,
  int32_t _M0L5indexS845,
  int32_t _M0L5valueS846
) {
  int32_t _M0L3lenS843;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS843 = _M0L4selfS844->$1;
  if (_M0L5indexS845 >= 0 && _M0L5indexS845 < _M0L3lenS843) {
    uint8_t* _M0L6_2atmpS2949;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2949 = _M0MPC15array5Array6bufferGbE(_M0L4selfS844);
    _M0L6_2atmpS2949[_M0L5indexS845] = _M0L5valueS846;
    moonbit_decref_cycle_free(_M0L6_2atmpS2949);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array6insertGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS826,
  int32_t _M0L5indexS825,
  int32_t _M0L5valueS828
) {
  int32_t _if__result_6025;
  #line 738 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L5indexS825 >= 0) {
    int32_t _M0L3lenS2917 = _M0L4selfS826->$1;
    _if__result_6025 = _M0L5indexS825 <= _M0L3lenS2917;
  } else {
    _if__result_6025 = 0;
  }
  if (_if__result_6025) {
    int32_t _M0L3lenS2918 = _M0L4selfS826->$1;
    int32_t* _M0L6_2atmpS2920;
    int32_t _M0L6_2atmpS2919;
    int32_t* _M0L6_2atmpS2923;
    int32_t _M0L6_2atmpS2924;
    int32_t* _M0L6_2atmpS2925;
    int32_t _M0L3lenS2927;
    int32_t _M0L6_2atmpS2926;
    int32_t _M0L6lengthS827;
    int32_t* _M0L3bufS2928;
    int32_t _M0L6_2atmpS2929;
    #line 745 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2920 = _M0MPC15array5Array6bufferGiE(_M0L4selfS826);
    _M0L6_2atmpS2919 = Moonbit_array_length(_M0L6_2atmpS2920);
    moonbit_decref_cycle_free(_M0L6_2atmpS2920);
    if (_M0L3lenS2918 == _M0L6_2atmpS2919) {
      int32_t _M0L3lenS2922 = _M0L4selfS826->$1;
      int32_t _M0L6_2atmpS2921 = _M0L3lenS2922 + 1;
      #line 746 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
      _M0MPC15array5Array7reallocGiE(_M0L4selfS826, _M0L6_2atmpS2921);
    }
    #line 749 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2923 = _M0MPC15array5Array6bufferGiE(_M0L4selfS826);
    _M0L6_2atmpS2924 = _M0L5indexS825 + 1;
    #line 751 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2925 = _M0MPC15array5Array6bufferGiE(_M0L4selfS826);
    _M0L3lenS2927 = _M0L4selfS826->$1;
    _M0L6_2atmpS2926 = _M0L3lenS2927 - _M0L5indexS825;
    #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L6_2atmpS2923, _M0L6_2atmpS2924, _M0L6_2atmpS2925, _M0L5indexS825, _M0L6_2atmpS2926);
    moonbit_decref_cycle_free(_M0L6_2atmpS2923);
    moonbit_decref_cycle_free(_M0L6_2atmpS2925);
    _M0L6lengthS827 = _M0L4selfS826->$1;
    _M0L3bufS2928 = _M0L4selfS826->$0;
    _M0L3bufS2928[_M0L5indexS825] = _M0L5valueS828;
    _M0L6_2atmpS2929 = _M0L6lengthS827 + 1;
    _M0L4selfS826->$1 = _M0L6_2atmpS2929;
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS829;
    int32_t _M0L3lenS2931;
    moonbit_string_t _M0L6_2atmpS2930;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L18_2astring__builderS829
    = _M0MPB13StringBuilder21StringBuilder_2einner(60);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS829, (moonbit_string_t)moonbit_string_literal_12.data);
    _M0L3lenS2931 = _M0L4selfS826->$1;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS829, _M0L3lenS2931);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS829, (moonbit_string_t)moonbit_string_literal_13.data);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS829, _M0L5indexS825);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2930
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS829);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS829);
    #line 741 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE(_M0L6_2atmpS2930);
    moonbit_decref_cycle_free(_M0L6_2atmpS2930);
  }
  return 0;
}

int32_t _M0MPC15array5Array6insertGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS831,
  int32_t _M0L5indexS830,
  float _M0L5valueS833
) {
  int32_t _if__result_6026;
  #line 738 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L5indexS830 >= 0) {
    int32_t _M0L3lenS2932 = _M0L4selfS831->$1;
    _if__result_6026 = _M0L5indexS830 <= _M0L3lenS2932;
  } else {
    _if__result_6026 = 0;
  }
  if (_if__result_6026) {
    int32_t _M0L3lenS2933 = _M0L4selfS831->$1;
    float* _M0L6_2atmpS2935;
    int32_t _M0L6_2atmpS2934;
    float* _M0L6_2atmpS2938;
    int32_t _M0L6_2atmpS2939;
    float* _M0L6_2atmpS2940;
    int32_t _M0L3lenS2942;
    int32_t _M0L6_2atmpS2941;
    int32_t _M0L6lengthS832;
    float* _M0L3bufS2943;
    int32_t _M0L6_2atmpS2944;
    #line 745 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2935 = _M0MPC15array5Array6bufferGfE(_M0L4selfS831);
    _M0L6_2atmpS2934 = Moonbit_array_length(_M0L6_2atmpS2935);
    moonbit_decref_cycle_free(_M0L6_2atmpS2935);
    if (_M0L3lenS2933 == _M0L6_2atmpS2934) {
      int32_t _M0L3lenS2937 = _M0L4selfS831->$1;
      int32_t _M0L6_2atmpS2936 = _M0L3lenS2937 + 1;
      #line 746 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
      _M0MPC15array5Array7reallocGfE(_M0L4selfS831, _M0L6_2atmpS2936);
    }
    #line 749 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2938 = _M0MPC15array5Array6bufferGfE(_M0L4selfS831);
    _M0L6_2atmpS2939 = _M0L5indexS830 + 1;
    #line 751 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2940 = _M0MPC15array5Array6bufferGfE(_M0L4selfS831);
    _M0L3lenS2942 = _M0L4selfS831->$1;
    _M0L6_2atmpS2941 = _M0L3lenS2942 - _M0L5indexS830;
    #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L6_2atmpS2938, _M0L6_2atmpS2939, _M0L6_2atmpS2940, _M0L5indexS830, _M0L6_2atmpS2941);
    moonbit_decref_cycle_free(_M0L6_2atmpS2938);
    moonbit_decref_cycle_free(_M0L6_2atmpS2940);
    _M0L6lengthS832 = _M0L4selfS831->$1;
    _M0L3bufS2943 = _M0L4selfS831->$0;
    _M0L3bufS2943[_M0L5indexS830] = _M0L5valueS833;
    _M0L6_2atmpS2944 = _M0L6lengthS832 + 1;
    _M0L4selfS831->$1 = _M0L6_2atmpS2944;
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS834;
    int32_t _M0L3lenS2946;
    moonbit_string_t _M0L6_2atmpS2945;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L18_2astring__builderS834
    = _M0MPB13StringBuilder21StringBuilder_2einner(60);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS834, (moonbit_string_t)moonbit_string_literal_12.data);
    _M0L3lenS2946 = _M0L4selfS831->$1;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS834, _M0L3lenS2946);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS834, (moonbit_string_t)moonbit_string_literal_13.data);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS834, _M0L5indexS830);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2945
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS834);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS834);
    #line 741 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE(_M0L6_2atmpS2945);
    moonbit_decref_cycle_free(_M0L6_2atmpS2945);
  }
  return 0;
}

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE* _M0L4selfS818) {
  int32_t _M0L3lenS817;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS817 = _M0L4selfS818->$1;
  if (_M0L3lenS817 == 0) {
    return (struct moonbit_object*)&moonbit_constant_constructor_0 + 1;
  } else {
    int32_t _M0L5indexS819 = _M0L3lenS817 - 1;
    float* _M0L3bufS2915 = _M0L4selfS818->$0;
    float _M0L1vS820 = (float)_M0L3bufS2915[_M0L5indexS819];
    void* _block_6027;
    _M0L4selfS818->$1 = _M0L5indexS819;
    _block_6027
    = (void*)moonbit_malloc(sizeof(struct _M0DTPC16option6OptionGfE4Some));
    Moonbit_object_header(_block_6027)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 1);
    ((struct _M0DTPC16option6OptionGfE4Some*)_block_6027)->$0 = _M0L1vS820;
    return _block_6027;
  }
}

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE* _M0L4selfS822) {
  int32_t _M0L3lenS821;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS821 = _M0L4selfS822->$1;
  if (_M0L3lenS821 == 0) {
    return 4294967296ll;
  } else {
    int32_t _M0L5indexS823 = _M0L3lenS821 - 1;
    int32_t* _M0L3bufS2916 = _M0L4selfS822->$0;
    int32_t _M0L1vS824 = (int32_t)_M0L3bufS2916[_M0L5indexS823];
    _M0L4selfS822->$1 = _M0L5indexS823;
    return (int64_t)_M0L1vS824;
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS803,
  int32_t _M0L5indexS804
) {
  int32_t _M0L3lenS802;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS802 = _M0L4selfS803->$1;
  if (_M0L5indexS804 >= 0 && _M0L5indexS804 < _M0L3lenS802) {
    float* _M0L6_2atmpS2910;
    float _result_6028;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2910 = _M0MPC15array5Array6bufferGfE(_M0L4selfS803);
    _result_6028 = (float)_M0L6_2atmpS2910[_M0L5indexS804];
    moonbit_decref_cycle_free(_M0L6_2atmpS2910);
    return _result_6028;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L4selfS806,
  int32_t _M0L5indexS807
) {
  int32_t _M0L3lenS805;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS805 = _M0L4selfS806->$1;
  if (_M0L5indexS807 >= 0 && _M0L5indexS807 < _M0L3lenS805) {
    struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L6_2atmpS2911;
    struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L6_2atmpS5622;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2911
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L4selfS806);
    _M0L6_2atmpS5622
    = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L6_2atmpS2911[
        _M0L5indexS807
      ];
    if (_M0L6_2atmpS5622) {
      moonbit_incref_cycle_free(_M0L6_2atmpS5622);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2911);
    return _M0L6_2atmpS5622;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS809,
  int32_t _M0L5indexS810
) {
  int32_t _M0L3lenS808;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS808 = _M0L4selfS809->$1;
  if (_M0L5indexS810 >= 0 && _M0L5indexS810 < _M0L3lenS808) {
    moonbit_string_t* _M0L6_2atmpS2912;
    moonbit_string_t _M0L6_2atmpS5623;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2912 = _M0MPC15array5Array6bufferGsE(_M0L4selfS809);
    _M0L6_2atmpS5623 = (moonbit_string_t)_M0L6_2atmpS2912[_M0L5indexS810];
    moonbit_incref_cycle_free(_M0L6_2atmpS5623);
    moonbit_decref_cycle_free(_M0L6_2atmpS2912);
    return _M0L6_2atmpS5623;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS812,
  int32_t _M0L5indexS813
) {
  int32_t _M0L3lenS811;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS811 = _M0L4selfS812->$1;
  if (_M0L5indexS813 >= 0 && _M0L5indexS813 < _M0L3lenS811) {
    int32_t* _M0L6_2atmpS2913;
    int32_t _result_6029;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2913 = _M0MPC15array5Array6bufferGiE(_M0L4selfS812);
    _result_6029 = (int32_t)_M0L6_2atmpS2913[_M0L5indexS813];
    moonbit_decref_cycle_free(_M0L6_2atmpS2913);
    return _result_6029;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS815,
  int32_t _M0L5indexS816
) {
  int32_t _M0L3lenS814;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS814 = _M0L4selfS815->$1;
  if (_M0L5indexS816 >= 0 && _M0L5indexS816 < _M0L3lenS814) {
    uint8_t* _M0L6_2atmpS2914;
    int32_t _result_6030;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2914 = _M0MPC15array5Array6bufferGbE(_M0L4selfS815);
    _result_6030 = (int32_t)_M0L6_2atmpS2914[_M0L5indexS816];
    moonbit_decref_cycle_free(_M0L6_2atmpS2914);
    return _result_6030;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS801) {
  moonbit_string_t _M0L6_2atmpS2909;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2909 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS801);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2909);
  moonbit_decref_cycle_free(_M0L6_2atmpS2909);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS800) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS800);
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS799) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS799 > _M0FPB18double__max__value
         || _M0L4selfS799 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS798) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS798 != _M0L4selfS798;
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS783) {
  uint64_t _M0L4bitsS786;
  uint64_t _M0L6_2atmpS2908;
  uint64_t _M0L6_2atmpS2907;
  int32_t _M0L8ieeeSignS787;
  uint64_t _M0L12ieeeMantissaS788;
  uint64_t _M0L6_2atmpS2906;
  uint64_t _M0L6_2atmpS2905;
  int32_t _M0L12ieeeExponentS789;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS790;
  struct _M0TPB17FloatingDecimal64* _M0L1vS791;
  moonbit_string_t _result_6032;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS783 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  if (_M0L3valS783 >= -0x1p+53 && _M0L3valS783 <= 0x1p+53) {
    if (_M0L3valS783 >= -0x1p+31 && _M0L3valS783 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS784;
      double _M0L6_2atmpS2894;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS784 = _M0MPC16double6Double7to__int(_M0L3valS783);
      _M0L6_2atmpS2894 = (double)_M0L1iS784;
      if (_M0L6_2atmpS2894 == _M0L3valS783) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS784, 10);
      }
    } else {
      int64_t _M0L1iS785;
      double _M0L6_2atmpS2895;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS785 = _M0MPC16double6Double9to__int64(_M0L3valS783);
      _M0L6_2atmpS2895 = (double)_M0L1iS785;
      if (_M0L6_2atmpS2895 == _M0L3valS783) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS785, 10);
      }
    }
  }
  _M0L4bitsS786 = *(int64_t*)&_M0L3valS783;
  _M0L6_2atmpS2908 = _M0L4bitsS786 >> 63;
  _M0L6_2atmpS2907 = _M0L6_2atmpS2908 & 1ull;
  _M0L8ieeeSignS787 = _M0L6_2atmpS2907 != 0ull;
  _M0L12ieeeMantissaS788 = _M0L4bitsS786 & 4503599627370495ull;
  _M0L6_2atmpS2906 = _M0L4bitsS786 >> 52;
  _M0L6_2atmpS2905 = _M0L6_2atmpS2906 & 2047ull;
  _M0L12ieeeExponentS789 = (int32_t)_M0L6_2atmpS2905;
  if (
    _M0L12ieeeExponentS789 == 2047
    || _M0L12ieeeExponentS789 == 0 && _M0L12ieeeMantissaS788 == 0ull
  ) {
    int32_t _M0L6_2atmpS2896 = _M0L12ieeeExponentS789 != 0;
    int32_t _M0L6_2atmpS2897 = _M0L12ieeeMantissaS788 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS787, _M0L6_2atmpS2896, _M0L6_2atmpS2897);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS790
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS788, _M0L12ieeeExponentS789);
  if (_M0L7_2abindS790 == 0) {
    uint32_t _M0L6_2atmpS2898;
    if (_M0L7_2abindS790) {
      moonbit_decref_cycle_free(_M0L7_2abindS790);
    }
    _M0L6_2atmpS2898 = *(uint32_t*)&_M0L12ieeeExponentS789;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS791 = _M0FPB3d2d(_M0L12ieeeMantissaS788, _M0L6_2atmpS2898);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS792 = _M0L7_2abindS790;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS793 = _M0L7_2aSomeS792;
    struct _M0TPB17FloatingDecimal64* _M0L1xS794 = _M0L4_2afS793;
    while (1) {
      uint64_t _M0L8mantissaS2904 = _M0L1xS794->$0;
      uint64_t _M0L1qS795 = _M0L8mantissaS2904 / 10ull;
      uint64_t _M0L8mantissaS2902 = _M0L1xS794->$0;
      uint64_t _M0L6_2atmpS2903 = 10ull * _M0L1qS795;
      uint64_t _M0L1rS796 = _M0L8mantissaS2902 - _M0L6_2atmpS2903;
      int32_t _M0L8exponentS2901;
      int32_t _M0L6_2atmpS2900;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2899;
      if (_M0L1rS796 != 0ull) {
        _M0L1vS791 = _M0L1xS794;
        break;
      }
      _M0L8exponentS2901 = _M0L1xS794->$1;
      moonbit_decref_cycle_free(_M0L1xS794);
      _M0L6_2atmpS2900 = _M0L8exponentS2901 + 1;
      _M0L6_2atmpS2899
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS2899)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS2899->$0 = _M0L1qS795;
      _M0L6_2atmpS2899->$1 = _M0L6_2atmpS2900;
      _M0L1xS794 = _M0L6_2atmpS2899;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_6032 = _M0FPB9to__chars(_M0L1vS791, _M0L8ieeeSignS787);
  moonbit_decref_cycle_free(_M0L1vS791);
  return _result_6032;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS778,
  int32_t _M0L12ieeeExponentS780
) {
  uint64_t _M0L2m2S777;
  int32_t _M0L6_2atmpS2893;
  int32_t _M0L2e2S779;
  int32_t _M0L6_2atmpS2892;
  uint64_t _M0L6_2atmpS2891;
  uint64_t _M0L4maskS781;
  uint64_t _M0L8fractionS782;
  int32_t _M0L6_2atmpS2890;
  uint64_t _M0L6_2atmpS2889;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2888;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S777 = 4503599627370496ull | _M0L12ieeeMantissaS778;
  _M0L6_2atmpS2893 = _M0L12ieeeExponentS780 - 1023;
  _M0L2e2S779 = _M0L6_2atmpS2893 - 52;
  if (_M0L2e2S779 > 0) {
    return 0;
  }
  if (_M0L2e2S779 < -52) {
    return 0;
  }
  _M0L6_2atmpS2892 = -_M0L2e2S779;
  _M0L6_2atmpS2891 = 1ull << (_M0L6_2atmpS2892 & 63);
  _M0L4maskS781 = _M0L6_2atmpS2891 - 1ull;
  _M0L8fractionS782 = _M0L2m2S777 & _M0L4maskS781;
  if (_M0L8fractionS782 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2890 = -_M0L2e2S779;
  _M0L6_2atmpS2889 = _M0L2m2S777 >> (_M0L6_2atmpS2890 & 63);
  _M0L6_2atmpS2888
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS2888)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS2888->$0 = _M0L6_2atmpS2889;
  _M0L6_2atmpS2888->$1 = 0;
  return _M0L6_2atmpS2888;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS745,
  int32_t _M0L4signS743
) {
  moonbit_bytes_t _M0L6resultS741;
  int32_t _M0Lm5indexS742;
  uint64_t _M0L6outputS744;
  int32_t _M0L7olengthS746;
  int32_t _M0L8exponentS2887;
  int32_t _M0L6_2atmpS2886;
  int32_t _M0Lm3expS747;
  int32_t _M0L6_2atmpS2885;
  int32_t _M0L6_2atmpS2883;
  int32_t _M0L18scientificNotationS748;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS741 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS742 = 0;
  if (_M0L4signS743) {
    int32_t _M0L6_2atmpS2757 = _M0Lm5indexS742;
    int32_t _M0L6_2atmpS2758;
    if (
      _M0L6_2atmpS2757 < 0
      || _M0L6_2atmpS2757 >= Moonbit_array_length(_M0L6resultS741)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS741[_M0L6_2atmpS2757] = 45;
    _M0L6_2atmpS2758 = _M0Lm5indexS742;
    _M0Lm5indexS742 = _M0L6_2atmpS2758 + 1;
  }
  _M0L6outputS744 = _M0L1vS745->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS746 = _M0FPB17decimal__length17(_M0L6outputS744);
  _M0L8exponentS2887 = _M0L1vS745->$1;
  _M0L6_2atmpS2886 = _M0L8exponentS2887 + _M0L7olengthS746;
  _M0Lm3expS747 = _M0L6_2atmpS2886 - 1;
  _M0L6_2atmpS2885 = _M0Lm3expS747;
  if (_M0L6_2atmpS2885 >= -6) {
    int32_t _M0L6_2atmpS2884 = _M0Lm3expS747;
    _M0L6_2atmpS2883 = _M0L6_2atmpS2884 < 21;
  } else {
    _M0L6_2atmpS2883 = 0;
  }
  _M0L18scientificNotationS748 = !_M0L6_2atmpS2883;
  if (_M0L18scientificNotationS748) {
    int32_t _M0L7_2abindS749 = _M0L7olengthS746 - 1;
    uint64_t _M0L6outputS750;
    int32_t _M0L1iS751 = 0;
    uint64_t _M0L6outputS752 = _M0L6outputS744;
    int32_t _M0L6_2atmpS2759;
    int32_t _M0L6_2atmpS2763;
    int32_t _M0L6_2atmpS2762;
    int32_t _M0L6_2atmpS2761;
    int32_t _M0L6_2atmpS2760;
    int32_t _M0L6_2atmpS2767;
    int32_t _M0L6_2atmpS2768;
    int32_t _M0L6_2atmpS2769;
    int32_t _M0L6_2atmpS2770;
    int32_t _M0L6_2atmpS2771;
    int32_t _M0L6_2atmpS2777;
    int32_t _M0L6_2atmpS2810;
    moonbit_string_t _result_6034;
    while (1) {
      if (_M0L1iS751 < _M0L7_2abindS749) {
        uint64_t _M0L1cS753 = _M0L6outputS752 % 10ull;
        int32_t _M0L6_2atmpS2816 = _M0Lm5indexS742;
        int32_t _M0L6_2atmpS2815 = _M0L6_2atmpS2816 + _M0L7olengthS746;
        int32_t _M0L6_2atmpS2811 = _M0L6_2atmpS2815 - _M0L1iS751;
        int32_t _M0L6_2atmpS2814 = (int32_t)_M0L1cS753;
        int32_t _M0L6_2atmpS2813 = 48 + _M0L6_2atmpS2814;
        int32_t _M0L6_2atmpS2812 = _M0L6_2atmpS2813 & 0xff;
        int32_t _M0L6_2atmpS2817;
        uint64_t _M0L6_2atmpS2818;
        if (
          _M0L6_2atmpS2811 < 0
          || _M0L6_2atmpS2811 >= Moonbit_array_length(_M0L6resultS741)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS741[_M0L6_2atmpS2811] = _M0L6_2atmpS2812;
        _M0L6_2atmpS2817 = _M0L1iS751 + 1;
        _M0L6_2atmpS2818 = _M0L6outputS752 / 10ull;
        _M0L1iS751 = _M0L6_2atmpS2817;
        _M0L6outputS752 = _M0L6_2atmpS2818;
        continue;
      } else {
        _M0L6outputS750 = _M0L6outputS752;
      }
      break;
    }
    _M0L6_2atmpS2759 = _M0Lm5indexS742;
    _M0L6_2atmpS2763 = (int32_t)_M0L6outputS750;
    _M0L6_2atmpS2762 = _M0L6_2atmpS2763 % 10;
    _M0L6_2atmpS2761 = 48 + _M0L6_2atmpS2762;
    _M0L6_2atmpS2760 = _M0L6_2atmpS2761 & 0xff;
    if (
      _M0L6_2atmpS2759 < 0
      || _M0L6_2atmpS2759 >= Moonbit_array_length(_M0L6resultS741)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS741[_M0L6_2atmpS2759] = _M0L6_2atmpS2760;
    if (_M0L7olengthS746 > 1) {
      int32_t _M0L6_2atmpS2765 = _M0Lm5indexS742;
      int32_t _M0L6_2atmpS2764 = _M0L6_2atmpS2765 + 1;
      if (
        _M0L6_2atmpS2764 < 0
        || _M0L6_2atmpS2764 >= Moonbit_array_length(_M0L6resultS741)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS741[_M0L6_2atmpS2764] = 46;
    } else {
      int32_t _M0L6_2atmpS2766 = _M0Lm5indexS742;
      _M0Lm5indexS742 = _M0L6_2atmpS2766 - 1;
    }
    _M0L6_2atmpS2767 = _M0Lm5indexS742;
    _M0L6_2atmpS2768 = _M0L7olengthS746 + 1;
    _M0Lm5indexS742 = _M0L6_2atmpS2767 + _M0L6_2atmpS2768;
    _M0L6_2atmpS2769 = _M0Lm5indexS742;
    if (
      _M0L6_2atmpS2769 < 0
      || _M0L6_2atmpS2769 >= Moonbit_array_length(_M0L6resultS741)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS741[_M0L6_2atmpS2769] = 101;
    _M0L6_2atmpS2770 = _M0Lm5indexS742;
    _M0Lm5indexS742 = _M0L6_2atmpS2770 + 1;
    _M0L6_2atmpS2771 = _M0Lm3expS747;
    if (_M0L6_2atmpS2771 < 0) {
      int32_t _M0L6_2atmpS2772 = _M0Lm5indexS742;
      int32_t _M0L6_2atmpS2773;
      int32_t _M0L6_2atmpS2774;
      if (
        _M0L6_2atmpS2772 < 0
        || _M0L6_2atmpS2772 >= Moonbit_array_length(_M0L6resultS741)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS741[_M0L6_2atmpS2772] = 45;
      _M0L6_2atmpS2773 = _M0Lm5indexS742;
      _M0Lm5indexS742 = _M0L6_2atmpS2773 + 1;
      _M0L6_2atmpS2774 = _M0Lm3expS747;
      _M0Lm3expS747 = -_M0L6_2atmpS2774;
    } else {
      int32_t _M0L6_2atmpS2775 = _M0Lm5indexS742;
      int32_t _M0L6_2atmpS2776;
      if (
        _M0L6_2atmpS2775 < 0
        || _M0L6_2atmpS2775 >= Moonbit_array_length(_M0L6resultS741)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS741[_M0L6_2atmpS2775] = 43;
      _M0L6_2atmpS2776 = _M0Lm5indexS742;
      _M0Lm5indexS742 = _M0L6_2atmpS2776 + 1;
    }
    _M0L6_2atmpS2777 = _M0Lm3expS747;
    if (_M0L6_2atmpS2777 >= 100) {
      int32_t _M0L6_2atmpS2793 = _M0Lm3expS747;
      int32_t _M0L1aS755 = _M0L6_2atmpS2793 / 100;
      int32_t _M0L6_2atmpS2792 = _M0Lm3expS747;
      int32_t _M0L6_2atmpS2791 = _M0L6_2atmpS2792 / 10;
      int32_t _M0L1bS756 = _M0L6_2atmpS2791 % 10;
      int32_t _M0L6_2atmpS2790 = _M0Lm3expS747;
      int32_t _M0L1cS757 = _M0L6_2atmpS2790 % 10;
      int32_t _M0L6_2atmpS2778 = _M0Lm5indexS742;
      int32_t _M0L6_2atmpS2780 = 48 + _M0L1aS755;
      int32_t _M0L6_2atmpS2779 = _M0L6_2atmpS2780 & 0xff;
      int32_t _M0L6_2atmpS2784;
      int32_t _M0L6_2atmpS2781;
      int32_t _M0L6_2atmpS2783;
      int32_t _M0L6_2atmpS2782;
      int32_t _M0L6_2atmpS2788;
      int32_t _M0L6_2atmpS2785;
      int32_t _M0L6_2atmpS2787;
      int32_t _M0L6_2atmpS2786;
      int32_t _M0L6_2atmpS2789;
      if (
        _M0L6_2atmpS2778 < 0
        || _M0L6_2atmpS2778 >= Moonbit_array_length(_M0L6resultS741)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS741[_M0L6_2atmpS2778] = _M0L6_2atmpS2779;
      _M0L6_2atmpS2784 = _M0Lm5indexS742;
      _M0L6_2atmpS2781 = _M0L6_2atmpS2784 + 1;
      _M0L6_2atmpS2783 = 48 + _M0L1bS756;
      _M0L6_2atmpS2782 = _M0L6_2atmpS2783 & 0xff;
      if (
        _M0L6_2atmpS2781 < 0
        || _M0L6_2atmpS2781 >= Moonbit_array_length(_M0L6resultS741)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS741[_M0L6_2atmpS2781] = _M0L6_2atmpS2782;
      _M0L6_2atmpS2788 = _M0Lm5indexS742;
      _M0L6_2atmpS2785 = _M0L6_2atmpS2788 + 2;
      _M0L6_2atmpS2787 = 48 + _M0L1cS757;
      _M0L6_2atmpS2786 = _M0L6_2atmpS2787 & 0xff;
      if (
        _M0L6_2atmpS2785 < 0
        || _M0L6_2atmpS2785 >= Moonbit_array_length(_M0L6resultS741)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS741[_M0L6_2atmpS2785] = _M0L6_2atmpS2786;
      _M0L6_2atmpS2789 = _M0Lm5indexS742;
      _M0Lm5indexS742 = _M0L6_2atmpS2789 + 3;
    } else {
      int32_t _M0L6_2atmpS2794 = _M0Lm3expS747;
      if (_M0L6_2atmpS2794 >= 10) {
        int32_t _M0L6_2atmpS2804 = _M0Lm3expS747;
        int32_t _M0L1aS758 = _M0L6_2atmpS2804 / 10;
        int32_t _M0L6_2atmpS2803 = _M0Lm3expS747;
        int32_t _M0L1bS759 = _M0L6_2atmpS2803 % 10;
        int32_t _M0L6_2atmpS2795 = _M0Lm5indexS742;
        int32_t _M0L6_2atmpS2797 = 48 + _M0L1aS758;
        int32_t _M0L6_2atmpS2796 = _M0L6_2atmpS2797 & 0xff;
        int32_t _M0L6_2atmpS2801;
        int32_t _M0L6_2atmpS2798;
        int32_t _M0L6_2atmpS2800;
        int32_t _M0L6_2atmpS2799;
        int32_t _M0L6_2atmpS2802;
        if (
          _M0L6_2atmpS2795 < 0
          || _M0L6_2atmpS2795 >= Moonbit_array_length(_M0L6resultS741)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS741[_M0L6_2atmpS2795] = _M0L6_2atmpS2796;
        _M0L6_2atmpS2801 = _M0Lm5indexS742;
        _M0L6_2atmpS2798 = _M0L6_2atmpS2801 + 1;
        _M0L6_2atmpS2800 = 48 + _M0L1bS759;
        _M0L6_2atmpS2799 = _M0L6_2atmpS2800 & 0xff;
        if (
          _M0L6_2atmpS2798 < 0
          || _M0L6_2atmpS2798 >= Moonbit_array_length(_M0L6resultS741)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS741[_M0L6_2atmpS2798] = _M0L6_2atmpS2799;
        _M0L6_2atmpS2802 = _M0Lm5indexS742;
        _M0Lm5indexS742 = _M0L6_2atmpS2802 + 2;
      } else {
        int32_t _M0L6_2atmpS2805 = _M0Lm5indexS742;
        int32_t _M0L6_2atmpS2808 = _M0Lm3expS747;
        int32_t _M0L6_2atmpS2807 = 48 + _M0L6_2atmpS2808;
        int32_t _M0L6_2atmpS2806 = _M0L6_2atmpS2807 & 0xff;
        int32_t _M0L6_2atmpS2809;
        if (
          _M0L6_2atmpS2805 < 0
          || _M0L6_2atmpS2805 >= Moonbit_array_length(_M0L6resultS741)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS741[_M0L6_2atmpS2805] = _M0L6_2atmpS2806;
        _M0L6_2atmpS2809 = _M0Lm5indexS742;
        _M0Lm5indexS742 = _M0L6_2atmpS2809 + 1;
      }
    }
    _M0L6_2atmpS2810 = _M0Lm5indexS742;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_6034
    = _M0FPB19string__from__bytes(_M0L6resultS741, 0, _M0L6_2atmpS2810);
    moonbit_decref_cycle_free(_M0L6resultS741);
    return _result_6034;
  } else {
    int32_t _M0L6_2atmpS2819 = _M0Lm3expS747;
    int32_t _M0L6_2atmpS2882;
    moonbit_string_t _result_6040;
    if (_M0L6_2atmpS2819 < 0) {
      int32_t _M0L6_2atmpS2820 = _M0Lm5indexS742;
      int32_t _M0L6_2atmpS2822;
      int32_t _M0L6_2atmpS2821;
      int32_t _M0L6_2atmpS2823;
      int32_t _M0L1iS760;
      int32_t _M0L6_2atmpS2838;
      int32_t _M0L6_2atmpS2840;
      int32_t _M0L6_2atmpS2839;
      int32_t _M0L7currentS762;
      int32_t _M0L1iS763;
      uint64_t _M0L6outputS764;
      if (
        _M0L6_2atmpS2820 < 0
        || _M0L6_2atmpS2820 >= Moonbit_array_length(_M0L6resultS741)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS741[_M0L6_2atmpS2820] = 48;
      _M0L6_2atmpS2822 = _M0Lm5indexS742;
      _M0L6_2atmpS2821 = _M0L6_2atmpS2822 + 1;
      if (
        _M0L6_2atmpS2821 < 0
        || _M0L6_2atmpS2821 >= Moonbit_array_length(_M0L6resultS741)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS741[_M0L6_2atmpS2821] = 46;
      _M0L6_2atmpS2823 = _M0Lm5indexS742;
      _M0Lm5indexS742 = _M0L6_2atmpS2823 + 2;
      _M0L1iS760 = -1;
      while (1) {
        int32_t _M0L6_2atmpS2824 = _M0Lm3expS747;
        if (_M0L1iS760 > _M0L6_2atmpS2824) {
          int32_t _M0L6_2atmpS2827 = _M0Lm5indexS742;
          int32_t _M0L6_2atmpS2826 = _M0L6_2atmpS2827 - _M0L1iS760;
          int32_t _M0L6_2atmpS2825 = _M0L6_2atmpS2826 - 1;
          int32_t _M0L6_2atmpS2828;
          if (
            _M0L6_2atmpS2825 < 0
            || _M0L6_2atmpS2825 >= Moonbit_array_length(_M0L6resultS741)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS741[_M0L6_2atmpS2825] = 48;
          _M0L6_2atmpS2828 = _M0L1iS760 - 1;
          _M0L1iS760 = _M0L6_2atmpS2828;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2838 = _M0Lm5indexS742;
      _M0L6_2atmpS2840 = _M0Lm3expS747;
      _M0L6_2atmpS2839 = -1 - _M0L6_2atmpS2840;
      _M0L7currentS762 = _M0L6_2atmpS2838 + _M0L6_2atmpS2839;
      _M0L1iS763 = 0;
      _M0L6outputS764 = _M0L6outputS744;
      while (1) {
        if (_M0L1iS763 < _M0L7olengthS746) {
          int32_t _M0L6_2atmpS2835 = _M0L7currentS762 + _M0L7olengthS746;
          int32_t _M0L6_2atmpS2834 = _M0L6_2atmpS2835 - _M0L1iS763;
          int32_t _M0L6_2atmpS2829 = _M0L6_2atmpS2834 - 1;
          uint64_t _M0L6_2atmpS2833 = _M0L6outputS764 % 10ull;
          int32_t _M0L6_2atmpS2832 = (int32_t)_M0L6_2atmpS2833;
          int32_t _M0L6_2atmpS2831 = 48 + _M0L6_2atmpS2832;
          int32_t _M0L6_2atmpS2830 = _M0L6_2atmpS2831 & 0xff;
          int32_t _M0L6_2atmpS2836;
          uint64_t _M0L6_2atmpS2837;
          if (
            _M0L6_2atmpS2829 < 0
            || _M0L6_2atmpS2829 >= Moonbit_array_length(_M0L6resultS741)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS741[_M0L6_2atmpS2829] = _M0L6_2atmpS2830;
          _M0L6_2atmpS2836 = _M0L1iS763 + 1;
          _M0L6_2atmpS2837 = _M0L6outputS764 / 10ull;
          _M0L1iS763 = _M0L6_2atmpS2836;
          _M0L6outputS764 = _M0L6_2atmpS2837;
          continue;
        }
        break;
      }
      _M0Lm5indexS742 = _M0L7currentS762 + _M0L7olengthS746;
    } else {
      int32_t _M0L6_2atmpS2842 = _M0Lm3expS747;
      int32_t _M0L6_2atmpS2841 = _M0L6_2atmpS2842 + 1;
      if (_M0L6_2atmpS2841 >= _M0L7olengthS746) {
        int32_t _M0L1iS766 = 0;
        uint64_t _M0L6outputS767 = _M0L6outputS744;
        int32_t _M0L6_2atmpS2853;
        int32_t _M0L6_2atmpS2858;
        int32_t _M0L7_2abindS769;
        int32_t _M0L1iS770;
        int32_t _M0L6_2atmpS2859;
        int32_t _M0L6_2atmpS2862;
        int32_t _M0L6_2atmpS2861;
        int32_t _M0L6_2atmpS2860;
        while (1) {
          if (_M0L1iS766 < _M0L7olengthS746) {
            int32_t _M0L6_2atmpS2850 = _M0Lm5indexS742;
            int32_t _M0L6_2atmpS2849 = _M0L6_2atmpS2850 + _M0L7olengthS746;
            int32_t _M0L6_2atmpS2848 = _M0L6_2atmpS2849 - _M0L1iS766;
            int32_t _M0L6_2atmpS2843 = _M0L6_2atmpS2848 - 1;
            uint64_t _M0L6_2atmpS2847 = _M0L6outputS767 % 10ull;
            int32_t _M0L6_2atmpS2846 = (int32_t)_M0L6_2atmpS2847;
            int32_t _M0L6_2atmpS2845 = 48 + _M0L6_2atmpS2846;
            int32_t _M0L6_2atmpS2844 = _M0L6_2atmpS2845 & 0xff;
            int32_t _M0L6_2atmpS2851;
            uint64_t _M0L6_2atmpS2852;
            if (
              _M0L6_2atmpS2843 < 0
              || _M0L6_2atmpS2843 >= Moonbit_array_length(_M0L6resultS741)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS741[_M0L6_2atmpS2843] = _M0L6_2atmpS2844;
            _M0L6_2atmpS2851 = _M0L1iS766 + 1;
            _M0L6_2atmpS2852 = _M0L6outputS767 / 10ull;
            _M0L1iS766 = _M0L6_2atmpS2851;
            _M0L6outputS767 = _M0L6_2atmpS2852;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2853 = _M0Lm5indexS742;
        _M0Lm5indexS742 = _M0L6_2atmpS2853 + _M0L7olengthS746;
        _M0L6_2atmpS2858 = _M0Lm3expS747;
        _M0L7_2abindS769 = _M0L6_2atmpS2858 + 1;
        _M0L1iS770 = _M0L7olengthS746;
        while (1) {
          if (_M0L1iS770 < _M0L7_2abindS769) {
            int32_t _M0L6_2atmpS2856 = _M0Lm5indexS742;
            int32_t _M0L6_2atmpS2855 = _M0L6_2atmpS2856 + _M0L1iS770;
            int32_t _M0L6_2atmpS2854 = _M0L6_2atmpS2855 - _M0L7olengthS746;
            int32_t _M0L6_2atmpS2857;
            if (
              _M0L6_2atmpS2854 < 0
              || _M0L6_2atmpS2854 >= Moonbit_array_length(_M0L6resultS741)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS741[_M0L6_2atmpS2854] = 48;
            _M0L6_2atmpS2857 = _M0L1iS770 + 1;
            _M0L1iS770 = _M0L6_2atmpS2857;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2859 = _M0Lm5indexS742;
        _M0L6_2atmpS2862 = _M0Lm3expS747;
        _M0L6_2atmpS2861 = _M0L6_2atmpS2862 + 1;
        _M0L6_2atmpS2860 = _M0L6_2atmpS2861 - _M0L7olengthS746;
        _M0Lm5indexS742 = _M0L6_2atmpS2859 + _M0L6_2atmpS2860;
      } else {
        int32_t _M0L6_2atmpS2879 = _M0Lm5indexS742;
        int32_t _M0L6_2atmpS2878 = _M0L6_2atmpS2879 + 1;
        int32_t _M0L1iS772 = 0;
        int32_t _M0L7currentS773 = _M0L6_2atmpS2878;
        uint64_t _M0L6outputS774 = _M0L6outputS744;
        int32_t _M0L6_2atmpS2880;
        int32_t _M0L6_2atmpS2881;
        while (1) {
          if (_M0L1iS772 < _M0L7olengthS746) {
            int32_t _M0L6_2atmpS2874 = _M0L7olengthS746 - _M0L1iS772;
            int32_t _M0L6_2atmpS2872 = _M0L6_2atmpS2874 - 1;
            int32_t _M0L6_2atmpS2873 = _M0Lm3expS747;
            int32_t _M0L7currentS775;
            int32_t _M0L6_2atmpS2869;
            int32_t _M0L6_2atmpS2868;
            int32_t _M0L6_2atmpS2863;
            uint64_t _M0L6_2atmpS2867;
            int32_t _M0L6_2atmpS2866;
            int32_t _M0L6_2atmpS2865;
            int32_t _M0L6_2atmpS2864;
            int32_t _M0L6_2atmpS2870;
            uint64_t _M0L6_2atmpS2871;
            if (_M0L6_2atmpS2872 == _M0L6_2atmpS2873) {
              int32_t _M0L6_2atmpS2877 = _M0L7currentS773 + _M0L7olengthS746;
              int32_t _M0L6_2atmpS2876 = _M0L6_2atmpS2877 - _M0L1iS772;
              int32_t _M0L6_2atmpS2875 = _M0L6_2atmpS2876 - 1;
              if (
                _M0L6_2atmpS2875 < 0
                || _M0L6_2atmpS2875 >= Moonbit_array_length(_M0L6resultS741)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS741[_M0L6_2atmpS2875] = 46;
              _M0L7currentS775 = _M0L7currentS773 - 1;
            } else {
              _M0L7currentS775 = _M0L7currentS773;
            }
            _M0L6_2atmpS2869 = _M0L7currentS775 + _M0L7olengthS746;
            _M0L6_2atmpS2868 = _M0L6_2atmpS2869 - _M0L1iS772;
            _M0L6_2atmpS2863 = _M0L6_2atmpS2868 - 1;
            _M0L6_2atmpS2867 = _M0L6outputS774 % 10ull;
            _M0L6_2atmpS2866 = (int32_t)_M0L6_2atmpS2867;
            _M0L6_2atmpS2865 = 48 + _M0L6_2atmpS2866;
            _M0L6_2atmpS2864 = _M0L6_2atmpS2865 & 0xff;
            if (
              _M0L6_2atmpS2863 < 0
              || _M0L6_2atmpS2863 >= Moonbit_array_length(_M0L6resultS741)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS741[_M0L6_2atmpS2863] = _M0L6_2atmpS2864;
            _M0L6_2atmpS2870 = _M0L1iS772 + 1;
            _M0L6_2atmpS2871 = _M0L6outputS774 / 10ull;
            _M0L1iS772 = _M0L6_2atmpS2870;
            _M0L7currentS773 = _M0L7currentS775;
            _M0L6outputS774 = _M0L6_2atmpS2871;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2880 = _M0Lm5indexS742;
        _M0L6_2atmpS2881 = _M0L7olengthS746 + 1;
        _M0Lm5indexS742 = _M0L6_2atmpS2880 + _M0L6_2atmpS2881;
      }
    }
    _M0L6_2atmpS2882 = _M0Lm5indexS742;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_6040
    = _M0FPB19string__from__bytes(_M0L6resultS741, 0, _M0L6_2atmpS2882);
    moonbit_decref_cycle_free(_M0L6resultS741);
    return _result_6040;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS687,
  uint32_t _M0L12ieeeExponentS686
) {
  int32_t _M0Lm2e2S684;
  uint64_t _M0Lm2m2S685;
  uint64_t _M0L6_2atmpS2756;
  uint64_t _M0L6_2atmpS2755;
  int32_t _M0L4evenS688;
  uint64_t _M0L6_2atmpS2754;
  uint64_t _M0L2mvS689;
  int32_t _M0L7mmShiftS690;
  uint64_t _M0Lm2vrS691;
  uint64_t _M0Lm2vpS692;
  uint64_t _M0Lm2vmS693;
  int32_t _M0Lm3e10S694;
  int32_t _M0Lm17vmIsTrailingZerosS695;
  int32_t _M0Lm17vrIsTrailingZerosS696;
  int32_t _M0L6_2atmpS2656;
  int32_t _M0Lm7removedS715;
  int32_t _M0Lm16lastRemovedDigitS716;
  uint64_t _M0Lm6outputS717;
  int32_t _M0L6_2atmpS2752;
  int32_t _M0L6_2atmpS2753;
  int32_t _M0L3expS740;
  uint64_t _M0L6_2atmpS2751;
  struct _M0TPB17FloatingDecimal64* _block_6046;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S684 = 0;
  _M0Lm2m2S685 = 0ull;
  if (_M0L12ieeeExponentS686 == 0u) {
    _M0Lm2e2S684 = -1076;
    _M0Lm2m2S685 = _M0L12ieeeMantissaS687;
  } else {
    int32_t _M0L6_2atmpS2655 = *(int32_t*)&_M0L12ieeeExponentS686;
    int32_t _M0L6_2atmpS2654 = _M0L6_2atmpS2655 - 1023;
    int32_t _M0L6_2atmpS2653 = _M0L6_2atmpS2654 - 52;
    _M0Lm2e2S684 = _M0L6_2atmpS2653 - 2;
    _M0Lm2m2S685 = 4503599627370496ull | _M0L12ieeeMantissaS687;
  }
  _M0L6_2atmpS2756 = _M0Lm2m2S685;
  _M0L6_2atmpS2755 = _M0L6_2atmpS2756 & 1ull;
  _M0L4evenS688 = _M0L6_2atmpS2755 == 0ull;
  _M0L6_2atmpS2754 = _M0Lm2m2S685;
  _M0L2mvS689 = 4ull * _M0L6_2atmpS2754;
  _M0L7mmShiftS690
  = _M0L12ieeeMantissaS687 != 0ull || _M0L12ieeeExponentS686 <= 1u;
  _M0Lm2vrS691 = 0ull;
  _M0Lm2vpS692 = 0ull;
  _M0Lm2vmS693 = 0ull;
  _M0Lm3e10S694 = 0;
  _M0Lm17vmIsTrailingZerosS695 = 0;
  _M0Lm17vrIsTrailingZerosS696 = 0;
  _M0L6_2atmpS2656 = _M0Lm2e2S684;
  if (_M0L6_2atmpS2656 >= 0) {
    int32_t _M0L6_2atmpS2678 = _M0Lm2e2S684;
    int32_t _M0L6_2atmpS2674;
    int32_t _M0L6_2atmpS2677;
    int32_t _M0L6_2atmpS2676;
    int32_t _M0L6_2atmpS2675;
    int32_t _M0L1qS697;
    int32_t _M0L6_2atmpS2673;
    int32_t _M0L6_2atmpS2672;
    int32_t _M0L1kS698;
    int32_t _M0L6_2atmpS2671;
    int32_t _M0L6_2atmpS2670;
    int32_t _M0L6_2atmpS2669;
    int32_t _M0L1iS699;
    struct _M0TPB8Pow5Pair _M0L4pow5S700;
    uint64_t _M0L6_2atmpS2668;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS701;
    uint64_t _M0L8_2avrOutS702;
    uint64_t _M0L8_2avpOutS703;
    uint64_t _M0L8_2avmOutS704;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2674 = _M0FPB9log10Pow2(_M0L6_2atmpS2678);
    _M0L6_2atmpS2677 = _M0Lm2e2S684;
    _M0L6_2atmpS2676 = _M0L6_2atmpS2677 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2675 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS2676);
    _M0L1qS697 = _M0L6_2atmpS2674 - _M0L6_2atmpS2675;
    _M0Lm3e10S694 = _M0L1qS697;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2673 = _M0FPB8pow5bits(_M0L1qS697);
    _M0L6_2atmpS2672 = 125 + _M0L6_2atmpS2673;
    _M0L1kS698 = _M0L6_2atmpS2672 - 1;
    _M0L6_2atmpS2671 = _M0Lm2e2S684;
    _M0L6_2atmpS2670 = -_M0L6_2atmpS2671;
    _M0L6_2atmpS2669 = _M0L6_2atmpS2670 + _M0L1qS697;
    _M0L1iS699 = _M0L6_2atmpS2669 + _M0L1kS698;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S700 = _M0FPB22double__computeInvPow5(_M0L1qS697);
    _M0L6_2atmpS2668 = _M0Lm2m2S685;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS701
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS2668, _M0L4pow5S700, _M0L1iS699, _M0L7mmShiftS690);
    _M0L8_2avrOutS702 = _M0L7_2abindS701.$0;
    _M0L8_2avpOutS703 = _M0L7_2abindS701.$1;
    _M0L8_2avmOutS704 = _M0L7_2abindS701.$2;
    _M0Lm2vrS691 = _M0L8_2avrOutS702;
    _M0Lm2vpS692 = _M0L8_2avpOutS703;
    _M0Lm2vmS693 = _M0L8_2avmOutS704;
    if (_M0L1qS697 <= 21) {
      int32_t _M0L6_2atmpS2664 = (int32_t)_M0L2mvS689;
      uint64_t _M0L6_2atmpS2667 = _M0L2mvS689 / 5ull;
      int32_t _M0L6_2atmpS2666 = (int32_t)_M0L6_2atmpS2667;
      int32_t _M0L6_2atmpS2665 = 5 * _M0L6_2atmpS2666;
      int32_t _M0L6mvMod5S705 = _M0L6_2atmpS2664 - _M0L6_2atmpS2665;
      if (_M0L6mvMod5S705 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS696
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS689, _M0L1qS697);
      } else if (_M0L4evenS688) {
        uint64_t _M0L6_2atmpS2658 = _M0L2mvS689 - 1ull;
        uint64_t _M0L6_2atmpS2659;
        uint64_t _M0L6_2atmpS2657;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2659 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS690);
        _M0L6_2atmpS2657 = _M0L6_2atmpS2658 - _M0L6_2atmpS2659;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS695
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS2657, _M0L1qS697);
      } else {
        uint64_t _M0L6_2atmpS2660 = _M0Lm2vpS692;
        uint64_t _M0L6_2atmpS2663 = _M0L2mvS689 + 2ull;
        int32_t _M0L6_2atmpS2662;
        uint64_t _M0L6_2atmpS2661;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2662
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS2663, _M0L1qS697);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2661 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS2662);
        _M0Lm2vpS692 = _M0L6_2atmpS2660 - _M0L6_2atmpS2661;
      }
    }
  } else {
    int32_t _M0L6_2atmpS2692 = _M0Lm2e2S684;
    int32_t _M0L6_2atmpS2691 = -_M0L6_2atmpS2692;
    int32_t _M0L6_2atmpS2686;
    int32_t _M0L6_2atmpS2690;
    int32_t _M0L6_2atmpS2689;
    int32_t _M0L6_2atmpS2688;
    int32_t _M0L6_2atmpS2687;
    int32_t _M0L1qS706;
    int32_t _M0L6_2atmpS2679;
    int32_t _M0L6_2atmpS2685;
    int32_t _M0L6_2atmpS2684;
    int32_t _M0L1iS707;
    int32_t _M0L6_2atmpS2683;
    int32_t _M0L1kS708;
    int32_t _M0L1jS709;
    struct _M0TPB8Pow5Pair _M0L4pow5S710;
    uint64_t _M0L6_2atmpS2682;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS711;
    uint64_t _M0L8_2avrOutS712;
    uint64_t _M0L8_2avpOutS713;
    uint64_t _M0L8_2avmOutS714;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2686 = _M0FPB9log10Pow5(_M0L6_2atmpS2691);
    _M0L6_2atmpS2690 = _M0Lm2e2S684;
    _M0L6_2atmpS2689 = -_M0L6_2atmpS2690;
    _M0L6_2atmpS2688 = _M0L6_2atmpS2689 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2687 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS2688);
    _M0L1qS706 = _M0L6_2atmpS2686 - _M0L6_2atmpS2687;
    _M0L6_2atmpS2679 = _M0Lm2e2S684;
    _M0Lm3e10S694 = _M0L1qS706 + _M0L6_2atmpS2679;
    _M0L6_2atmpS2685 = _M0Lm2e2S684;
    _M0L6_2atmpS2684 = -_M0L6_2atmpS2685;
    _M0L1iS707 = _M0L6_2atmpS2684 - _M0L1qS706;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2683 = _M0FPB8pow5bits(_M0L1iS707);
    _M0L1kS708 = _M0L6_2atmpS2683 - 125;
    _M0L1jS709 = _M0L1qS706 - _M0L1kS708;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S710 = _M0FPB19double__computePow5(_M0L1iS707);
    _M0L6_2atmpS2682 = _M0Lm2m2S685;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS711
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS2682, _M0L4pow5S710, _M0L1jS709, _M0L7mmShiftS690);
    _M0L8_2avrOutS712 = _M0L7_2abindS711.$0;
    _M0L8_2avpOutS713 = _M0L7_2abindS711.$1;
    _M0L8_2avmOutS714 = _M0L7_2abindS711.$2;
    _M0Lm2vrS691 = _M0L8_2avrOutS712;
    _M0Lm2vpS692 = _M0L8_2avpOutS713;
    _M0Lm2vmS693 = _M0L8_2avmOutS714;
    if (_M0L1qS706 <= 1) {
      _M0Lm17vrIsTrailingZerosS696 = 1;
      if (_M0L4evenS688) {
        int32_t _M0L6_2atmpS2680;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2680 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS690);
        _M0Lm17vmIsTrailingZerosS695 = _M0L6_2atmpS2680 == 1;
      } else {
        uint64_t _M0L6_2atmpS2681 = _M0Lm2vpS692;
        _M0Lm2vpS692 = _M0L6_2atmpS2681 - 1ull;
      }
    } else if (_M0L1qS706 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS696
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS689, _M0L1qS706);
    }
  }
  _M0Lm7removedS715 = 0;
  _M0Lm16lastRemovedDigitS716 = 0;
  _M0Lm6outputS717 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS695 || _M0Lm17vrIsTrailingZerosS696) {
    int32_t _if__result_6043;
    uint64_t _M0L6_2atmpS2722;
    uint64_t _M0L6_2atmpS2728;
    uint64_t _M0L6_2atmpS2729;
    int32_t _if__result_6044;
    int32_t _M0L6_2atmpS2725;
    int64_t _M0L6_2atmpS2724;
    uint64_t _M0L6_2atmpS2723;
    while (1) {
      uint64_t _M0L6_2atmpS2705 = _M0Lm2vpS692;
      uint64_t _M0L7vpDiv10S718 = _M0L6_2atmpS2705 / 10ull;
      uint64_t _M0L6_2atmpS2704 = _M0Lm2vmS693;
      uint64_t _M0L7vmDiv10S719 = _M0L6_2atmpS2704 / 10ull;
      uint64_t _M0L6_2atmpS2703;
      int32_t _M0L6_2atmpS2700;
      int32_t _M0L6_2atmpS2702;
      int32_t _M0L6_2atmpS2701;
      int32_t _M0L7vmMod10S721;
      uint64_t _M0L6_2atmpS2699;
      uint64_t _M0L7vrDiv10S722;
      uint64_t _M0L6_2atmpS2698;
      int32_t _M0L6_2atmpS2695;
      int32_t _M0L6_2atmpS2697;
      int32_t _M0L6_2atmpS2696;
      int32_t _M0L7vrMod10S723;
      int32_t _M0L6_2atmpS2694;
      if (_M0L7vpDiv10S718 <= _M0L7vmDiv10S719) {
        break;
      }
      _M0L6_2atmpS2703 = _M0Lm2vmS693;
      _M0L6_2atmpS2700 = (int32_t)_M0L6_2atmpS2703;
      _M0L6_2atmpS2702 = (int32_t)_M0L7vmDiv10S719;
      _M0L6_2atmpS2701 = 10 * _M0L6_2atmpS2702;
      _M0L7vmMod10S721 = _M0L6_2atmpS2700 - _M0L6_2atmpS2701;
      _M0L6_2atmpS2699 = _M0Lm2vrS691;
      _M0L7vrDiv10S722 = _M0L6_2atmpS2699 / 10ull;
      _M0L6_2atmpS2698 = _M0Lm2vrS691;
      _M0L6_2atmpS2695 = (int32_t)_M0L6_2atmpS2698;
      _M0L6_2atmpS2697 = (int32_t)_M0L7vrDiv10S722;
      _M0L6_2atmpS2696 = 10 * _M0L6_2atmpS2697;
      _M0L7vrMod10S723 = _M0L6_2atmpS2695 - _M0L6_2atmpS2696;
      _M0Lm17vmIsTrailingZerosS695
      = _M0Lm17vmIsTrailingZerosS695 && _M0L7vmMod10S721 == 0;
      if (_M0Lm17vrIsTrailingZerosS696) {
        int32_t _M0L6_2atmpS2693 = _M0Lm16lastRemovedDigitS716;
        _M0Lm17vrIsTrailingZerosS696 = _M0L6_2atmpS2693 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS696 = 0;
      }
      _M0Lm16lastRemovedDigitS716 = _M0L7vrMod10S723;
      _M0Lm2vrS691 = _M0L7vrDiv10S722;
      _M0Lm2vpS692 = _M0L7vpDiv10S718;
      _M0Lm2vmS693 = _M0L7vmDiv10S719;
      _M0L6_2atmpS2694 = _M0Lm7removedS715;
      _M0Lm7removedS715 = _M0L6_2atmpS2694 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS695) {
      while (1) {
        uint64_t _M0L6_2atmpS2718 = _M0Lm2vmS693;
        uint64_t _M0L7vmDiv10S724 = _M0L6_2atmpS2718 / 10ull;
        uint64_t _M0L6_2atmpS2717 = _M0Lm2vmS693;
        int32_t _M0L6_2atmpS2714 = (int32_t)_M0L6_2atmpS2717;
        int32_t _M0L6_2atmpS2716 = (int32_t)_M0L7vmDiv10S724;
        int32_t _M0L6_2atmpS2715 = 10 * _M0L6_2atmpS2716;
        int32_t _M0L7vmMod10S725 = _M0L6_2atmpS2714 - _M0L6_2atmpS2715;
        uint64_t _M0L6_2atmpS2713;
        uint64_t _M0L7vpDiv10S727;
        uint64_t _M0L6_2atmpS2712;
        uint64_t _M0L7vrDiv10S728;
        uint64_t _M0L6_2atmpS2711;
        int32_t _M0L6_2atmpS2708;
        int32_t _M0L6_2atmpS2710;
        int32_t _M0L6_2atmpS2709;
        int32_t _M0L7vrMod10S729;
        int32_t _M0L6_2atmpS2707;
        if (_M0L7vmMod10S725 != 0) {
          break;
        }
        _M0L6_2atmpS2713 = _M0Lm2vpS692;
        _M0L7vpDiv10S727 = _M0L6_2atmpS2713 / 10ull;
        _M0L6_2atmpS2712 = _M0Lm2vrS691;
        _M0L7vrDiv10S728 = _M0L6_2atmpS2712 / 10ull;
        _M0L6_2atmpS2711 = _M0Lm2vrS691;
        _M0L6_2atmpS2708 = (int32_t)_M0L6_2atmpS2711;
        _M0L6_2atmpS2710 = (int32_t)_M0L7vrDiv10S728;
        _M0L6_2atmpS2709 = 10 * _M0L6_2atmpS2710;
        _M0L7vrMod10S729 = _M0L6_2atmpS2708 - _M0L6_2atmpS2709;
        if (_M0Lm17vrIsTrailingZerosS696) {
          int32_t _M0L6_2atmpS2706 = _M0Lm16lastRemovedDigitS716;
          _M0Lm17vrIsTrailingZerosS696 = _M0L6_2atmpS2706 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS696 = 0;
        }
        _M0Lm16lastRemovedDigitS716 = _M0L7vrMod10S729;
        _M0Lm2vrS691 = _M0L7vrDiv10S728;
        _M0Lm2vpS692 = _M0L7vpDiv10S727;
        _M0Lm2vmS693 = _M0L7vmDiv10S724;
        _M0L6_2atmpS2707 = _M0Lm7removedS715;
        _M0Lm7removedS715 = _M0L6_2atmpS2707 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS696) {
      int32_t _M0L6_2atmpS2721 = _M0Lm16lastRemovedDigitS716;
      if (_M0L6_2atmpS2721 == 5) {
        uint64_t _M0L6_2atmpS2720 = _M0Lm2vrS691;
        uint64_t _M0L6_2atmpS2719 = _M0L6_2atmpS2720 % 2ull;
        _if__result_6043 = _M0L6_2atmpS2719 == 0ull;
      } else {
        _if__result_6043 = 0;
      }
    } else {
      _if__result_6043 = 0;
    }
    if (_if__result_6043) {
      _M0Lm16lastRemovedDigitS716 = 4;
    }
    _M0L6_2atmpS2722 = _M0Lm2vrS691;
    _M0L6_2atmpS2728 = _M0Lm2vrS691;
    _M0L6_2atmpS2729 = _M0Lm2vmS693;
    if (_M0L6_2atmpS2728 == _M0L6_2atmpS2729) {
      if (!_M0L4evenS688) {
        _if__result_6044 = 1;
      } else {
        int32_t _M0L6_2atmpS2727 = _M0Lm17vmIsTrailingZerosS695;
        _if__result_6044 = !_M0L6_2atmpS2727;
      }
    } else {
      _if__result_6044 = 0;
    }
    if (_if__result_6044) {
      _M0L6_2atmpS2725 = 1;
    } else {
      int32_t _M0L6_2atmpS2726 = _M0Lm16lastRemovedDigitS716;
      _M0L6_2atmpS2725 = _M0L6_2atmpS2726 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2724 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS2725);
    _M0L6_2atmpS2723 = *(uint64_t*)&_M0L6_2atmpS2724;
    _M0Lm6outputS717 = _M0L6_2atmpS2722 + _M0L6_2atmpS2723;
  } else {
    int32_t _M0Lm7roundUpS730 = 0;
    uint64_t _M0L6_2atmpS2750 = _M0Lm2vpS692;
    uint64_t _M0L8vpDiv100S731 = _M0L6_2atmpS2750 / 100ull;
    uint64_t _M0L6_2atmpS2749 = _M0Lm2vmS693;
    uint64_t _M0L8vmDiv100S732 = _M0L6_2atmpS2749 / 100ull;
    uint64_t _M0L6_2atmpS2744;
    uint64_t _M0L6_2atmpS2747;
    uint64_t _M0L6_2atmpS2748;
    int32_t _M0L6_2atmpS2746;
    uint64_t _M0L6_2atmpS2745;
    if (_M0L8vpDiv100S731 > _M0L8vmDiv100S732) {
      uint64_t _M0L6_2atmpS2735 = _M0Lm2vrS691;
      uint64_t _M0L8vrDiv100S733 = _M0L6_2atmpS2735 / 100ull;
      uint64_t _M0L6_2atmpS2734 = _M0Lm2vrS691;
      int32_t _M0L6_2atmpS2731 = (int32_t)_M0L6_2atmpS2734;
      int32_t _M0L6_2atmpS2733 = (int32_t)_M0L8vrDiv100S733;
      int32_t _M0L6_2atmpS2732 = 100 * _M0L6_2atmpS2733;
      int32_t _M0L8vrMod100S734 = _M0L6_2atmpS2731 - _M0L6_2atmpS2732;
      int32_t _M0L6_2atmpS2730;
      _M0Lm7roundUpS730 = _M0L8vrMod100S734 >= 50;
      _M0Lm2vrS691 = _M0L8vrDiv100S733;
      _M0Lm2vpS692 = _M0L8vpDiv100S731;
      _M0Lm2vmS693 = _M0L8vmDiv100S732;
      _M0L6_2atmpS2730 = _M0Lm7removedS715;
      _M0Lm7removedS715 = _M0L6_2atmpS2730 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS2743 = _M0Lm2vpS692;
      uint64_t _M0L7vpDiv10S735 = _M0L6_2atmpS2743 / 10ull;
      uint64_t _M0L6_2atmpS2742 = _M0Lm2vmS693;
      uint64_t _M0L7vmDiv10S736 = _M0L6_2atmpS2742 / 10ull;
      uint64_t _M0L6_2atmpS2741;
      uint64_t _M0L7vrDiv10S738;
      uint64_t _M0L6_2atmpS2740;
      int32_t _M0L6_2atmpS2737;
      int32_t _M0L6_2atmpS2739;
      int32_t _M0L6_2atmpS2738;
      int32_t _M0L7vrMod10S739;
      int32_t _M0L6_2atmpS2736;
      if (_M0L7vpDiv10S735 <= _M0L7vmDiv10S736) {
        break;
      }
      _M0L6_2atmpS2741 = _M0Lm2vrS691;
      _M0L7vrDiv10S738 = _M0L6_2atmpS2741 / 10ull;
      _M0L6_2atmpS2740 = _M0Lm2vrS691;
      _M0L6_2atmpS2737 = (int32_t)_M0L6_2atmpS2740;
      _M0L6_2atmpS2739 = (int32_t)_M0L7vrDiv10S738;
      _M0L6_2atmpS2738 = 10 * _M0L6_2atmpS2739;
      _M0L7vrMod10S739 = _M0L6_2atmpS2737 - _M0L6_2atmpS2738;
      _M0Lm7roundUpS730 = _M0L7vrMod10S739 >= 5;
      _M0Lm2vrS691 = _M0L7vrDiv10S738;
      _M0Lm2vpS692 = _M0L7vpDiv10S735;
      _M0Lm2vmS693 = _M0L7vmDiv10S736;
      _M0L6_2atmpS2736 = _M0Lm7removedS715;
      _M0Lm7removedS715 = _M0L6_2atmpS2736 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS2744 = _M0Lm2vrS691;
    _M0L6_2atmpS2747 = _M0Lm2vrS691;
    _M0L6_2atmpS2748 = _M0Lm2vmS693;
    _M0L6_2atmpS2746
    = _M0L6_2atmpS2747 == _M0L6_2atmpS2748 || _M0Lm7roundUpS730;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2745 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS2746);
    _M0Lm6outputS717 = _M0L6_2atmpS2744 + _M0L6_2atmpS2745;
  }
  _M0L6_2atmpS2752 = _M0Lm3e10S694;
  _M0L6_2atmpS2753 = _M0Lm7removedS715;
  _M0L3expS740 = _M0L6_2atmpS2752 + _M0L6_2atmpS2753;
  _M0L6_2atmpS2751 = _M0Lm6outputS717;
  _block_6046
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_6046)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_6046->$0 = _M0L6_2atmpS2751;
  _block_6046->$1 = _M0L3expS740;
  return _block_6046;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS683) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS683) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS682) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS682) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS681) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS681) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS680) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS680 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS680 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS680 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS680 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS680 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS680 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS680 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS680 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS680 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS680 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS680 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS680 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS680 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS680 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS680 >= 100ull) {
    return 3;
  }
  if (_M0L1vS680 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS663) {
  int32_t _M0L6_2atmpS2652;
  int32_t _M0L6_2atmpS2651;
  int32_t _M0L4baseS662;
  int32_t _M0L5base2S664;
  int32_t _M0L6offsetS665;
  int32_t _M0L6_2atmpS2650;
  uint64_t _M0L4mul0S666;
  int32_t _M0L6_2atmpS2649;
  int32_t _M0L6_2atmpS2648;
  uint64_t _M0L4mul1S667;
  uint64_t _M0L1mS668;
  struct _M0TPB7Umul128 _M0L7_2abindS669;
  uint64_t _M0L7_2alow1S670;
  uint64_t _M0L8_2ahigh1S671;
  struct _M0TPB7Umul128 _M0L7_2abindS672;
  uint64_t _M0L7_2alow0S673;
  uint64_t _M0L8_2ahigh0S674;
  uint64_t _M0L3sumS675;
  uint64_t _M0Lm5high1S676;
  int32_t _M0L6_2atmpS2646;
  int32_t _M0L6_2atmpS2647;
  int32_t _M0L5deltaS677;
  uint64_t _M0L6_2atmpS2645;
  uint64_t _M0L6_2atmpS2637;
  int32_t _M0L6_2atmpS2644;
  uint32_t _M0L6_2atmpS2641;
  int32_t _M0L6_2atmpS2643;
  int32_t _M0L6_2atmpS2642;
  uint32_t _M0L6_2atmpS2640;
  uint32_t _M0L6_2atmpS2639;
  uint64_t _M0L6_2atmpS2638;
  uint64_t _M0L1aS678;
  uint64_t _M0L6_2atmpS2636;
  uint64_t _M0L1bS679;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2652 = _M0L1iS663 + 26;
  _M0L6_2atmpS2651 = _M0L6_2atmpS2652 - 1;
  _M0L4baseS662 = _M0L6_2atmpS2651 / 26;
  _M0L5base2S664 = _M0L4baseS662 * 26;
  _M0L6offsetS665 = _M0L5base2S664 - _M0L1iS663;
  _M0L6_2atmpS2650 = _M0L4baseS662 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S666
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS2650);
  _M0L6_2atmpS2649 = _M0L4baseS662 * 2;
  _M0L6_2atmpS2648 = _M0L6_2atmpS2649 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S667
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS2648);
  if (_M0L6offsetS665 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S666, .$1 = _M0L4mul1S667};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS668
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS665);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS669 = _M0FPB7umul128(_M0L1mS668, _M0L4mul1S667);
  _M0L7_2alow1S670 = _M0L7_2abindS669.$0;
  _M0L8_2ahigh1S671 = _M0L7_2abindS669.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS672 = _M0FPB7umul128(_M0L1mS668, _M0L4mul0S666);
  _M0L7_2alow0S673 = _M0L7_2abindS672.$0;
  _M0L8_2ahigh0S674 = _M0L7_2abindS672.$1;
  _M0L3sumS675 = _M0L8_2ahigh0S674 + _M0L7_2alow1S670;
  _M0Lm5high1S676 = _M0L8_2ahigh1S671;
  if (_M0L3sumS675 < _M0L8_2ahigh0S674) {
    uint64_t _M0L6_2atmpS2635 = _M0Lm5high1S676;
    _M0Lm5high1S676 = _M0L6_2atmpS2635 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2646 = _M0FPB8pow5bits(_M0L5base2S664);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2647 = _M0FPB8pow5bits(_M0L1iS663);
  _M0L5deltaS677 = _M0L6_2atmpS2646 - _M0L6_2atmpS2647;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2645
  = _M0FPB13shiftright128(_M0L7_2alow0S673, _M0L3sumS675, _M0L5deltaS677);
  _M0L6_2atmpS2637 = _M0L6_2atmpS2645 + 1ull;
  _M0L6_2atmpS2644 = _M0L1iS663 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2641
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS2644);
  _M0L6_2atmpS2643 = _M0L1iS663 % 16;
  _M0L6_2atmpS2642 = _M0L6_2atmpS2643 << 1;
  _M0L6_2atmpS2640 = _M0L6_2atmpS2641 >> (_M0L6_2atmpS2642 & 31);
  _M0L6_2atmpS2639 = _M0L6_2atmpS2640 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2638 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS2639);
  _M0L1aS678 = _M0L6_2atmpS2637 + _M0L6_2atmpS2638;
  _M0L6_2atmpS2636 = _M0Lm5high1S676;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS679
  = _M0FPB13shiftright128(_M0L3sumS675, _M0L6_2atmpS2636, _M0L5deltaS677);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS678, .$1 = _M0L1bS679};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS645) {
  int32_t _M0L4baseS644;
  int32_t _M0L5base2S646;
  int32_t _M0L6offsetS647;
  int32_t _M0L6_2atmpS2634;
  uint64_t _M0L4mul0S648;
  int32_t _M0L6_2atmpS2633;
  int32_t _M0L6_2atmpS2632;
  uint64_t _M0L4mul1S649;
  uint64_t _M0L1mS650;
  struct _M0TPB7Umul128 _M0L7_2abindS651;
  uint64_t _M0L7_2alow1S652;
  uint64_t _M0L8_2ahigh1S653;
  struct _M0TPB7Umul128 _M0L7_2abindS654;
  uint64_t _M0L7_2alow0S655;
  uint64_t _M0L8_2ahigh0S656;
  uint64_t _M0L3sumS657;
  uint64_t _M0Lm5high1S658;
  int32_t _M0L6_2atmpS2630;
  int32_t _M0L6_2atmpS2631;
  int32_t _M0L5deltaS659;
  uint64_t _M0L6_2atmpS2622;
  int32_t _M0L6_2atmpS2629;
  uint32_t _M0L6_2atmpS2626;
  int32_t _M0L6_2atmpS2628;
  int32_t _M0L6_2atmpS2627;
  uint32_t _M0L6_2atmpS2625;
  uint32_t _M0L6_2atmpS2624;
  uint64_t _M0L6_2atmpS2623;
  uint64_t _M0L1aS660;
  uint64_t _M0L6_2atmpS2621;
  uint64_t _M0L1bS661;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS644 = _M0L1iS645 / 26;
  _M0L5base2S646 = _M0L4baseS644 * 26;
  _M0L6offsetS647 = _M0L1iS645 - _M0L5base2S646;
  _M0L6_2atmpS2634 = _M0L4baseS644 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S648
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS2634);
  _M0L6_2atmpS2633 = _M0L4baseS644 * 2;
  _M0L6_2atmpS2632 = _M0L6_2atmpS2633 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S649
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS2632);
  if (_M0L6offsetS647 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S648, .$1 = _M0L4mul1S649};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS650
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS647);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS651 = _M0FPB7umul128(_M0L1mS650, _M0L4mul1S649);
  _M0L7_2alow1S652 = _M0L7_2abindS651.$0;
  _M0L8_2ahigh1S653 = _M0L7_2abindS651.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS654 = _M0FPB7umul128(_M0L1mS650, _M0L4mul0S648);
  _M0L7_2alow0S655 = _M0L7_2abindS654.$0;
  _M0L8_2ahigh0S656 = _M0L7_2abindS654.$1;
  _M0L3sumS657 = _M0L8_2ahigh0S656 + _M0L7_2alow1S652;
  _M0Lm5high1S658 = _M0L8_2ahigh1S653;
  if (_M0L3sumS657 < _M0L8_2ahigh0S656) {
    uint64_t _M0L6_2atmpS2620 = _M0Lm5high1S658;
    _M0Lm5high1S658 = _M0L6_2atmpS2620 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2630 = _M0FPB8pow5bits(_M0L1iS645);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2631 = _M0FPB8pow5bits(_M0L5base2S646);
  _M0L5deltaS659 = _M0L6_2atmpS2630 - _M0L6_2atmpS2631;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2622
  = _M0FPB13shiftright128(_M0L7_2alow0S655, _M0L3sumS657, _M0L5deltaS659);
  _M0L6_2atmpS2629 = _M0L1iS645 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2626
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS2629);
  _M0L6_2atmpS2628 = _M0L1iS645 % 16;
  _M0L6_2atmpS2627 = _M0L6_2atmpS2628 << 1;
  _M0L6_2atmpS2625 = _M0L6_2atmpS2626 >> (_M0L6_2atmpS2627 & 31);
  _M0L6_2atmpS2624 = _M0L6_2atmpS2625 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2623 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS2624);
  _M0L1aS660 = _M0L6_2atmpS2622 + _M0L6_2atmpS2623;
  _M0L6_2atmpS2621 = _M0Lm5high1S658;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS661
  = _M0FPB13shiftright128(_M0L3sumS657, _M0L6_2atmpS2621, _M0L5deltaS659);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS660, .$1 = _M0L1bS661};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS618,
  struct _M0TPB8Pow5Pair _M0L3mulS615,
  int32_t _M0L1jS631,
  int32_t _M0L7mmShiftS633
) {
  uint64_t _M0L7_2amul0S614;
  uint64_t _M0L7_2amul1S616;
  uint64_t _M0L1mS617;
  struct _M0TPB7Umul128 _M0L7_2abindS619;
  uint64_t _M0L5_2aloS620;
  uint64_t _M0L6_2atmpS621;
  struct _M0TPB7Umul128 _M0L7_2abindS622;
  uint64_t _M0L6_2alo2S623;
  uint64_t _M0L6_2ahi2S624;
  uint64_t _M0L3midS625;
  uint64_t _M0L6_2atmpS2619;
  uint64_t _M0L2hiS626;
  uint64_t _M0L3lo2S627;
  uint64_t _M0L6_2atmpS2617;
  uint64_t _M0L6_2atmpS2618;
  uint64_t _M0L4mid2S628;
  uint64_t _M0L6_2atmpS2616;
  uint64_t _M0L3hi2S629;
  int32_t _M0L6_2atmpS2615;
  int32_t _M0L6_2atmpS2614;
  uint64_t _M0L2vpS630;
  uint64_t _M0Lm2vmS632;
  int32_t _M0L6_2atmpS2613;
  int32_t _M0L6_2atmpS2612;
  uint64_t _M0L2vrS643;
  uint64_t _M0L6_2atmpS2611;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S614 = _M0L3mulS615.$0;
  _M0L7_2amul1S616 = _M0L3mulS615.$1;
  _M0L1mS617 = _M0L1mS618 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS619 = _M0FPB7umul128(_M0L1mS617, _M0L7_2amul0S614);
  _M0L5_2aloS620 = _M0L7_2abindS619.$0;
  _M0L6_2atmpS621 = _M0L7_2abindS619.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS622 = _M0FPB7umul128(_M0L1mS617, _M0L7_2amul1S616);
  _M0L6_2alo2S623 = _M0L7_2abindS622.$0;
  _M0L6_2ahi2S624 = _M0L7_2abindS622.$1;
  _M0L3midS625 = _M0L6_2atmpS621 + _M0L6_2alo2S623;
  if (_M0L3midS625 < _M0L6_2atmpS621) {
    _M0L6_2atmpS2619 = 1ull;
  } else {
    _M0L6_2atmpS2619 = 0ull;
  }
  _M0L2hiS626 = _M0L6_2ahi2S624 + _M0L6_2atmpS2619;
  _M0L3lo2S627 = _M0L5_2aloS620 + _M0L7_2amul0S614;
  _M0L6_2atmpS2617 = _M0L3midS625 + _M0L7_2amul1S616;
  if (_M0L3lo2S627 < _M0L5_2aloS620) {
    _M0L6_2atmpS2618 = 1ull;
  } else {
    _M0L6_2atmpS2618 = 0ull;
  }
  _M0L4mid2S628 = _M0L6_2atmpS2617 + _M0L6_2atmpS2618;
  if (_M0L4mid2S628 < _M0L3midS625) {
    _M0L6_2atmpS2616 = 1ull;
  } else {
    _M0L6_2atmpS2616 = 0ull;
  }
  _M0L3hi2S629 = _M0L2hiS626 + _M0L6_2atmpS2616;
  _M0L6_2atmpS2615 = _M0L1jS631 - 64;
  _M0L6_2atmpS2614 = _M0L6_2atmpS2615 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS630
  = _M0FPB13shiftright128(_M0L4mid2S628, _M0L3hi2S629, _M0L6_2atmpS2614);
  _M0Lm2vmS632 = 0ull;
  if (_M0L7mmShiftS633) {
    uint64_t _M0L3lo3S634 = _M0L5_2aloS620 - _M0L7_2amul0S614;
    uint64_t _M0L6_2atmpS2601 = _M0L3midS625 - _M0L7_2amul1S616;
    uint64_t _M0L6_2atmpS2602;
    uint64_t _M0L4mid3S635;
    uint64_t _M0L6_2atmpS2600;
    uint64_t _M0L3hi3S636;
    int32_t _M0L6_2atmpS2599;
    int32_t _M0L6_2atmpS2598;
    if (_M0L5_2aloS620 < _M0L3lo3S634) {
      _M0L6_2atmpS2602 = 1ull;
    } else {
      _M0L6_2atmpS2602 = 0ull;
    }
    _M0L4mid3S635 = _M0L6_2atmpS2601 - _M0L6_2atmpS2602;
    if (_M0L3midS625 < _M0L4mid3S635) {
      _M0L6_2atmpS2600 = 1ull;
    } else {
      _M0L6_2atmpS2600 = 0ull;
    }
    _M0L3hi3S636 = _M0L2hiS626 - _M0L6_2atmpS2600;
    _M0L6_2atmpS2599 = _M0L1jS631 - 64;
    _M0L6_2atmpS2598 = _M0L6_2atmpS2599 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS632
    = _M0FPB13shiftright128(_M0L4mid3S635, _M0L3hi3S636, _M0L6_2atmpS2598);
  } else {
    uint64_t _M0L3lo3S637 = _M0L5_2aloS620 + _M0L5_2aloS620;
    uint64_t _M0L6_2atmpS2609 = _M0L3midS625 + _M0L3midS625;
    uint64_t _M0L6_2atmpS2610;
    uint64_t _M0L4mid3S638;
    uint64_t _M0L6_2atmpS2607;
    uint64_t _M0L6_2atmpS2608;
    uint64_t _M0L3hi3S639;
    uint64_t _M0L3lo4S640;
    uint64_t _M0L6_2atmpS2605;
    uint64_t _M0L6_2atmpS2606;
    uint64_t _M0L4mid4S641;
    uint64_t _M0L6_2atmpS2604;
    uint64_t _M0L3hi4S642;
    int32_t _M0L6_2atmpS2603;
    if (_M0L3lo3S637 < _M0L5_2aloS620) {
      _M0L6_2atmpS2610 = 1ull;
    } else {
      _M0L6_2atmpS2610 = 0ull;
    }
    _M0L4mid3S638 = _M0L6_2atmpS2609 + _M0L6_2atmpS2610;
    _M0L6_2atmpS2607 = _M0L2hiS626 + _M0L2hiS626;
    if (_M0L4mid3S638 < _M0L3midS625) {
      _M0L6_2atmpS2608 = 1ull;
    } else {
      _M0L6_2atmpS2608 = 0ull;
    }
    _M0L3hi3S639 = _M0L6_2atmpS2607 + _M0L6_2atmpS2608;
    _M0L3lo4S640 = _M0L3lo3S637 - _M0L7_2amul0S614;
    _M0L6_2atmpS2605 = _M0L4mid3S638 - _M0L7_2amul1S616;
    if (_M0L3lo3S637 < _M0L3lo4S640) {
      _M0L6_2atmpS2606 = 1ull;
    } else {
      _M0L6_2atmpS2606 = 0ull;
    }
    _M0L4mid4S641 = _M0L6_2atmpS2605 - _M0L6_2atmpS2606;
    if (_M0L4mid3S638 < _M0L4mid4S641) {
      _M0L6_2atmpS2604 = 1ull;
    } else {
      _M0L6_2atmpS2604 = 0ull;
    }
    _M0L3hi4S642 = _M0L3hi3S639 - _M0L6_2atmpS2604;
    _M0L6_2atmpS2603 = _M0L1jS631 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS632
    = _M0FPB13shiftright128(_M0L4mid4S641, _M0L3hi4S642, _M0L6_2atmpS2603);
  }
  _M0L6_2atmpS2613 = _M0L1jS631 - 64;
  _M0L6_2atmpS2612 = _M0L6_2atmpS2613 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS643
  = _M0FPB13shiftright128(_M0L3midS625, _M0L2hiS626, _M0L6_2atmpS2612);
  _M0L6_2atmpS2611 = _M0Lm2vmS632;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS643,
                                                .$1 = _M0L2vpS630,
                                                .$2 = _M0L6_2atmpS2611};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS612,
  int32_t _M0L1pS613
) {
  uint64_t _M0L6_2atmpS2597;
  uint64_t _M0L6_2atmpS2596;
  uint64_t _M0L6_2atmpS2595;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2597 = 1ull << (_M0L1pS613 & 63);
  _M0L6_2atmpS2596 = _M0L6_2atmpS2597 - 1ull;
  _M0L6_2atmpS2595 = _M0L5valueS612 & _M0L6_2atmpS2596;
  return _M0L6_2atmpS2595 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS610,
  int32_t _M0L1pS611
) {
  int32_t _M0L6_2atmpS2594;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2594 = _M0FPB10pow5Factor(_M0L5valueS610);
  return _M0L6_2atmpS2594 >= _M0L1pS611;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS605) {
  uint64_t _M0L6_2atmpS2585;
  uint64_t _M0L6_2atmpS2586;
  uint64_t _M0L6_2atmpS2587;
  uint64_t _M0L6_2atmpS2588;
  uint64_t _M0L6_2atmpS2593;
  int32_t _M0L5countS606;
  uint64_t _M0L1vS607;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2585 = _M0L5valueS605 % 5ull;
  if (_M0L6_2atmpS2585 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2586 = _M0L5valueS605 % 25ull;
  if (_M0L6_2atmpS2586 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS2587 = _M0L5valueS605 % 125ull;
  if (_M0L6_2atmpS2587 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS2588 = _M0L5valueS605 % 625ull;
  if (_M0L6_2atmpS2588 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS2593 = _M0L5valueS605 / 625ull;
  _M0L5countS606 = 4;
  _M0L1vS607 = _M0L6_2atmpS2593;
  while (1) {
    if (_M0L1vS607 > 0ull) {
      uint64_t _M0L6_2atmpS2589 = _M0L1vS607 % 5ull;
      int32_t _M0L6_2atmpS2590;
      uint64_t _M0L6_2atmpS2591;
      if (_M0L6_2atmpS2589 != 0ull) {
        return _M0L5countS606;
      }
      _M0L6_2atmpS2590 = _M0L5countS606 + 1;
      _M0L6_2atmpS2591 = _M0L1vS607 / 5ull;
      _M0L5countS606 = _M0L6_2atmpS2590;
      _M0L1vS607 = _M0L6_2atmpS2591;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS609;
      moonbit_string_t _M0L6_2atmpS2592;
      int32_t _result_6048;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS609
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS609, (moonbit_string_t)moonbit_string_literal_15.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS609, _M0L5valueS605);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS2592
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS609);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS609);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_6048 = _M0FPC15abort5abortGiE(_M0L6_2atmpS2592);
      moonbit_decref_cycle_free(_M0L6_2atmpS2592);
      return _result_6048;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS604,
  uint64_t _M0L2hiS602,
  int32_t _M0L4distS603
) {
  int32_t _M0L6_2atmpS2584;
  uint64_t _M0L6_2atmpS2582;
  uint64_t _M0L6_2atmpS2583;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2584 = 64 - _M0L4distS603;
  _M0L6_2atmpS2582 = _M0L2hiS602 << (_M0L6_2atmpS2584 & 63);
  _M0L6_2atmpS2583 = _M0L2loS604 >> (_M0L4distS603 & 63);
  return _M0L6_2atmpS2582 | _M0L6_2atmpS2583;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS592,
  uint64_t _M0L1bS595
) {
  uint64_t _M0L3aLoS591;
  uint64_t _M0L3aHiS593;
  uint64_t _M0L3bLoS594;
  uint64_t _M0L3bHiS596;
  uint64_t _M0L1xS597;
  uint64_t _M0L6_2atmpS2580;
  uint64_t _M0L6_2atmpS2581;
  uint64_t _M0L1yS598;
  uint64_t _M0L6_2atmpS2578;
  uint64_t _M0L6_2atmpS2579;
  uint64_t _M0L1zS599;
  uint64_t _M0L6_2atmpS2576;
  uint64_t _M0L6_2atmpS2577;
  uint64_t _M0L6_2atmpS2574;
  uint64_t _M0L6_2atmpS2575;
  uint64_t _M0L1wS600;
  uint64_t _M0L2loS601;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS591 = _M0L1aS592 & 4294967295ull;
  _M0L3aHiS593 = _M0L1aS592 >> 32;
  _M0L3bLoS594 = _M0L1bS595 & 4294967295ull;
  _M0L3bHiS596 = _M0L1bS595 >> 32;
  _M0L1xS597 = _M0L3aLoS591 * _M0L3bLoS594;
  _M0L6_2atmpS2580 = _M0L3aHiS593 * _M0L3bLoS594;
  _M0L6_2atmpS2581 = _M0L1xS597 >> 32;
  _M0L1yS598 = _M0L6_2atmpS2580 + _M0L6_2atmpS2581;
  _M0L6_2atmpS2578 = _M0L3aLoS591 * _M0L3bHiS596;
  _M0L6_2atmpS2579 = _M0L1yS598 & 4294967295ull;
  _M0L1zS599 = _M0L6_2atmpS2578 + _M0L6_2atmpS2579;
  _M0L6_2atmpS2576 = _M0L3aHiS593 * _M0L3bHiS596;
  _M0L6_2atmpS2577 = _M0L1yS598 >> 32;
  _M0L6_2atmpS2574 = _M0L6_2atmpS2576 + _M0L6_2atmpS2577;
  _M0L6_2atmpS2575 = _M0L1zS599 >> 32;
  _M0L1wS600 = _M0L6_2atmpS2574 + _M0L6_2atmpS2575;
  _M0L2loS601 = _M0L1aS592 * _M0L1bS595;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS601, .$1 = _M0L1wS600};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS589,
  int32_t _M0L4fromS586,
  int32_t _M0L2toS585
) {
  int32_t _M0L3lenS584;
  int32_t _M0L6_2atmpS2573;
  uint16_t* _M0L6bufferS587;
  int32_t _M0L1iS588;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS584 = _M0L2toS585 - _M0L4fromS586;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2573 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS587
  = (uint16_t*)moonbit_make_string(_M0L3lenS584, _M0L6_2atmpS2573);
  _M0L1iS588 = 0;
  while (1) {
    if (_M0L1iS588 < _M0L3lenS584) {
      int32_t _M0L6_2atmpS2571 = _M0L4fromS586 + _M0L1iS588;
      int32_t _M0L6_2atmpS2570;
      int32_t _M0L6_2atmpS2569;
      int32_t _M0L6_2atmpS2572;
      if (
        _M0L6_2atmpS2571 < 0
        || _M0L6_2atmpS2571 >= Moonbit_array_length(_M0L5bytesS589)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2570 = (int32_t)_M0L5bytesS589[_M0L6_2atmpS2571];
      _M0L6_2atmpS2569 = (uint16_t)_M0L6_2atmpS2570;
      if (
        _M0L1iS588 < 0 || _M0L1iS588 >= Moonbit_array_length(_M0L6bufferS587)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS587[_M0L1iS588] = _M0L6_2atmpS2569;
      _M0L6_2atmpS2572 = _M0L1iS588 + 1;
      _M0L1iS588 = _M0L6_2atmpS2572;
      continue;
    }
    break;
  }
  return _M0L6bufferS587;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS583) {
  int32_t _M0L6_2atmpS2568;
  uint32_t _M0L6_2atmpS2567;
  uint32_t _M0L6_2atmpS2566;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2568 = _M0L1eS583 * 78913;
  _M0L6_2atmpS2567 = *(uint32_t*)&_M0L6_2atmpS2568;
  _M0L6_2atmpS2566 = _M0L6_2atmpS2567 >> 18;
  return *(int32_t*)&_M0L6_2atmpS2566;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS582) {
  int32_t _M0L6_2atmpS2565;
  uint32_t _M0L6_2atmpS2564;
  uint32_t _M0L6_2atmpS2563;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2565 = _M0L1eS582 * 732923;
  _M0L6_2atmpS2564 = *(uint32_t*)&_M0L6_2atmpS2565;
  _M0L6_2atmpS2563 = _M0L6_2atmpS2564 >> 20;
  return *(int32_t*)&_M0L6_2atmpS2563;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS580,
  int32_t _M0L8exponentS581,
  int32_t _M0L8mantissaS578
) {
  moonbit_string_t _M0L1sS579;
  moonbit_string_t _result_6051;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS578) {
    return (moonbit_string_t)moonbit_string_literal_16.data;
  }
  if (_M0L4signS580) {
    _M0L1sS579 = (moonbit_string_t)moonbit_string_literal_17.data;
  } else {
    _M0L1sS579 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS581) {
    moonbit_string_t _result_6050;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_6050
    = moonbit_add_string(_M0L1sS579, (moonbit_string_t)moonbit_string_literal_18.data);
    moonbit_decref_cycle_free(_M0L1sS579);
    return _result_6050;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_6051
  = moonbit_add_string(_M0L1sS579, (moonbit_string_t)moonbit_string_literal_19.data);
  moonbit_decref_cycle_free(_M0L1sS579);
  return _result_6051;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS577) {
  int32_t _M0L6_2atmpS2562;
  uint32_t _M0L6_2atmpS2561;
  uint32_t _M0L6_2atmpS2560;
  int32_t _M0L6_2atmpS2559;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2562 = _M0L1eS577 * 1217359;
  _M0L6_2atmpS2561 = *(uint32_t*)&_M0L6_2atmpS2562;
  _M0L6_2atmpS2560 = _M0L6_2atmpS2561 >> 19;
  _M0L6_2atmpS2559 = *(int32_t*)&_M0L6_2atmpS2560;
  return _M0L6_2atmpS2559 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS576) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS576 != _M0L4selfS576) {
    return 0;
  } else if (_M0L4selfS576 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS576 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS576;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS575) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS575 != _M0L4selfS575) {
    return 0ll;
  } else if (_M0L4selfS575 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS575 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS575;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS572
) {
  float* _M0L6_2atmpS2556;
  struct _M0TPB5ArrayGfE* _block_6052;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2556 = (float*)moonbit_make_float_array_raw(_M0L3lenS572);
  _block_6052
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_6052)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _block_6052->$0 = _M0L6_2atmpS2556;
  _block_6052->$1 = _M0L3lenS572;
  return _block_6052;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS573
) {
  uint8_t* _M0L6_2atmpS2557;
  struct _M0TPB5ArrayGbE* _block_6053;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2557 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS573);
  _block_6053
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_6053)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 85, 0);
  _block_6053->$0 = _M0L6_2atmpS2557;
  _block_6053->$1 = _M0L3lenS573;
  return _block_6053;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS574
) {
  int32_t* _M0L6_2atmpS2558;
  struct _M0TPB5ArrayGiE* _block_6054;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2558 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS574);
  _block_6054
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_6054)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 42, 0);
  _block_6054->$0 = _M0L6_2atmpS2558;
  _block_6054->$1 = _M0L3lenS574;
  return _block_6054;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS568,
  int32_t _M0L5indexS569
) {
  uint64_t* _M0L6_2atmpS2554;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS2554 = _M0L4selfS568;
  if (
    _M0L5indexS569 < 0
    || _M0L5indexS569 >= Moonbit_array_length(_M0L6_2atmpS2554)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS2554[_M0L5indexS569];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS570,
  int32_t _M0L5indexS571
) {
  uint32_t* _M0L6_2atmpS2555;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS2555 = _M0L4selfS570;
  if (
    _M0L5indexS571 < 0
    || _M0L5indexS571 >= Moonbit_array_length(_M0L6_2atmpS2555)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS2555[_M0L5indexS571];
}

int32_t _M0IPC15array5ArrayPB4Show6outputGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS567,
  struct _M0TPB6Logger _M0L6loggerS566
) {
  struct _M0TPB4IterGfE* _M0L6_2atmpS2553;
  #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 269 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2553 = _M0MPC15array5Array4iterGfE(_M0L4selfS567);
  #line 269 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPB6Logger19write__iter_2einnerGfE(_M0L6loggerS566, _M0L6_2atmpS2553, (moonbit_string_t)moonbit_string_literal_20.data, (moonbit_string_t)moonbit_string_literal_21.data, (moonbit_string_t)moonbit_string_literal_22.data, 0);
  moonbit_decref_cycle_free(_M0L6_2atmpS2553);
  return 0;
}

struct _M0TPB4IterGfE* _M0MPC15array5Array4iterGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS565
) {
  float* _M0L3bufS2551;
  int32_t _M0L3lenS2552;
  struct _M0TPB9ArrayViewGfE _M0L6_2atmpS2550;
  struct _M0TPB4IterGfE* _result_6055;
  #line 1749 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3bufS2551 = _M0L4selfS565->$0;
  _M0L3lenS2552 = _M0L4selfS565->$1;
  moonbit_incref_cycle_free(_M0L3bufS2551);
  _M0L6_2atmpS2550
  = (struct _M0TPB9ArrayViewGfE){
    .$0 = _M0L3bufS2551, .$1 = 0, .$2 = _M0L3lenS2552
  };
  #line 1751 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _result_6055 = _M0MPC15array9ArrayView4iterGfE(_M0L6_2atmpS2550);
  moonbit_decref_cycle_free(_M0L6_2atmpS2550.$0);
  return _result_6055;
}

struct _M0TPB4IterGfE* _M0MPC15array9ArrayView4iterGfE(
  struct _M0TPB9ArrayViewGfE _M0L4selfS563
) {
  struct _M0TPB8MutLocalGiE* _M0L1iS561;
  int32_t _M0L3endS2548;
  int32_t _M0L5startS2549;
  int32_t _M0L3lenS562;
  struct _M0R58ArrayView_3a_3aiter_7c_5bFloat_5d_7c_2eanon__u2538__l885__* _closure_6056;
  struct _M0TWERPC16option6OptionGfE* _M0L6_2atmpS2536;
  int64_t _M0L6_2atmpS2537;
  #line 880 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arrayview.mbt"
  _M0L1iS561
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS561)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS561->$0 = 0;
  _M0L3endS2548 = _M0L4selfS563.$2;
  _M0L5startS2549 = _M0L4selfS563.$1;
  _M0L3lenS562 = _M0L3endS2548 - _M0L5startS2549;
  moonbit_incref_cycle_free(_M0L4selfS563.$0);
  _closure_6056
  = (struct _M0R58ArrayView_3a_3aiter_7c_5bFloat_5d_7c_2eanon__u2538__l885__*)moonbit_malloc(sizeof(struct _M0R58ArrayView_3a_3aiter_7c_5bFloat_5d_7c_2eanon__u2538__l885__));
  Moonbit_object_header(_closure_6056)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 102, 0);
  _closure_6056->code = &_M0MPC15array9ArrayView4iterGfEC2538l885;
  _closure_6056->$0 = _M0L4selfS563;
  _closure_6056->$1 = _M0L3lenS562;
  _closure_6056->$2 = _M0L1iS561;
  _M0L6_2atmpS2536 = (struct _M0TWERPC16option6OptionGfE*)_closure_6056;
  _M0L6_2atmpS2537 = (int64_t)_M0L3lenS562;
  #line 884 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arrayview.mbt"
  return _M0MPB4Iter3newGfE(_M0L6_2atmpS2536, _M0L6_2atmpS2537);
}

void* _M0MPC15array9ArrayView4iterGfEC2538l885(
  struct _M0TWERPC16option6OptionGfE* _M0L6_2aenvS2539
) {
  struct _M0R58ArrayView_3a_3aiter_7c_5bFloat_5d_7c_2eanon__u2538__l885__* _M0L14_2acasted__envS2540;
  struct _M0TPB8MutLocalGiE* _M0L1iS561;
  int32_t _M0L3lenS562;
  struct _M0TPB9ArrayViewGfE _M0L4selfS563;
  int32_t _M0L3valS2541;
  #line 885 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arrayview.mbt"
  _M0L14_2acasted__envS2540
  = (struct _M0R58ArrayView_3a_3aiter_7c_5bFloat_5d_7c_2eanon__u2538__l885__*)_M0L6_2aenvS2539;
  _M0L1iS561 = _M0L14_2acasted__envS2540->$2;
  _M0L3lenS562 = _M0L14_2acasted__envS2540->$1;
  _M0L4selfS563 = _M0L14_2acasted__envS2540->$0;
  _M0L3valS2541 = _M0L1iS561->$0;
  if (_M0L3valS2541 < _M0L3lenS562) {
    float* _M0L3bufS2544 = _M0L4selfS563.$0;
    int32_t _M0L5startS2546 = _M0L4selfS563.$1;
    int32_t _M0L3valS2547 = _M0L1iS561->$0;
    int32_t _M0L6_2atmpS2545 = _M0L5startS2546 + _M0L3valS2547;
    float _M0L4elemS564 = (float)_M0L3bufS2544[_M0L6_2atmpS2545];
    int32_t _M0L3valS2543 = _M0L1iS561->$0;
    int32_t _M0L6_2atmpS2542 = _M0L3valS2543 + 1;
    void* _block_6057;
    _M0L1iS561->$0 = _M0L6_2atmpS2542;
    _block_6057
    = (void*)moonbit_malloc(sizeof(struct _M0DTPC16option6OptionGfE4Some));
    Moonbit_object_header(_block_6057)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 1);
    ((struct _M0DTPC16option6OptionGfE4Some*)_block_6057)->$0 = _M0L4elemS564;
    return _block_6057;
  } else {
    return (struct moonbit_object*)&moonbit_constant_constructor_0 + 1;
  }
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS560
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS560, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS559) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS559, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS558) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS558;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS546,
  moonbit_string_t _M0L5valueS548
) {
  int32_t _M0L3lenS2508;
  moonbit_string_t* _M0L6_2atmpS2510;
  int32_t _M0L6_2atmpS2509;
  int32_t _M0L6lengthS547;
  moonbit_string_t* _M0L3bufS2513;
  moonbit_string_t _M0L6_2aoldS5624;
  int32_t _M0L6_2atmpS2514;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2508 = _M0L4selfS546->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2510 = _M0MPC15array5Array6bufferGsE(_M0L4selfS546);
  _M0L6_2atmpS2509 = Moonbit_array_length(_M0L6_2atmpS2510);
  moonbit_decref_cycle_free(_M0L6_2atmpS2510);
  if (_M0L3lenS2508 == _M0L6_2atmpS2509) {
    int32_t _M0L3lenS2512 = _M0L4selfS546->$1;
    int32_t _M0L6_2atmpS2511 = _M0L3lenS2512 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS546, _M0L6_2atmpS2511);
  }
  _M0L6lengthS547 = _M0L4selfS546->$1;
  _M0L3bufS2513 = _M0L4selfS546->$0;
  _M0L6_2aoldS5624 = (moonbit_string_t)_M0L3bufS2513[_M0L6lengthS547];
  moonbit_decref_cycle_free(_M0L6_2aoldS5624);
  _M0L3bufS2513[_M0L6lengthS547] = _M0L5valueS548;
  _M0L6_2atmpS2514 = _M0L6lengthS547 + 1;
  _M0L4selfS546->$1 = _M0L6_2atmpS2514;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS549,
  struct _M0TUsiE* _M0L5valueS551
) {
  int32_t _M0L3lenS2515;
  struct _M0TUsiE** _M0L6_2atmpS2517;
  int32_t _M0L6_2atmpS2516;
  int32_t _M0L6lengthS550;
  struct _M0TUsiE** _M0L3bufS2520;
  struct _M0TUsiE* _M0L6_2aoldS5625;
  int32_t _M0L6_2atmpS2521;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2515 = _M0L4selfS549->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2517 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS549);
  _M0L6_2atmpS2516 = Moonbit_array_length(_M0L6_2atmpS2517);
  moonbit_decref_cycle_free(_M0L6_2atmpS2517);
  if (_M0L3lenS2515 == _M0L6_2atmpS2516) {
    int32_t _M0L3lenS2519 = _M0L4selfS549->$1;
    int32_t _M0L6_2atmpS2518 = _M0L3lenS2519 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS549, _M0L6_2atmpS2518);
  }
  _M0L6lengthS550 = _M0L4selfS549->$1;
  _M0L3bufS2520 = _M0L4selfS549->$0;
  _M0L6_2aoldS5625 = (struct _M0TUsiE*)_M0L3bufS2520[_M0L6lengthS550];
  if (_M0L6_2aoldS5625) {
    moonbit_decref_cycle_free(_M0L6_2aoldS5625);
  }
  _M0L3bufS2520[_M0L6lengthS550] = _M0L5valueS551;
  _M0L6_2atmpS2521 = _M0L6lengthS550 + 1;
  _M0L4selfS549->$1 = _M0L6_2atmpS2521;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS552,
  float _M0L5valueS554
) {
  int32_t _M0L3lenS2522;
  float* _M0L6_2atmpS2524;
  int32_t _M0L6_2atmpS2523;
  int32_t _M0L6lengthS553;
  float* _M0L3bufS2527;
  int32_t _M0L6_2atmpS2528;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2522 = _M0L4selfS552->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2524 = _M0MPC15array5Array6bufferGfE(_M0L4selfS552);
  _M0L6_2atmpS2523 = Moonbit_array_length(_M0L6_2atmpS2524);
  moonbit_decref_cycle_free(_M0L6_2atmpS2524);
  if (_M0L3lenS2522 == _M0L6_2atmpS2523) {
    int32_t _M0L3lenS2526 = _M0L4selfS552->$1;
    int32_t _M0L6_2atmpS2525 = _M0L3lenS2526 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS552, _M0L6_2atmpS2525);
  }
  _M0L6lengthS553 = _M0L4selfS552->$1;
  _M0L3bufS2527 = _M0L4selfS552->$0;
  _M0L3bufS2527[_M0L6lengthS553] = _M0L5valueS554;
  _M0L6_2atmpS2528 = _M0L6lengthS553 + 1;
  _M0L4selfS552->$1 = _M0L6_2atmpS2528;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS555,
  int32_t _M0L5valueS557
) {
  int32_t _M0L3lenS2529;
  int32_t* _M0L6_2atmpS2531;
  int32_t _M0L6_2atmpS2530;
  int32_t _M0L6lengthS556;
  int32_t* _M0L3bufS2534;
  int32_t _M0L6_2atmpS2535;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2529 = _M0L4selfS555->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2531 = _M0MPC15array5Array6bufferGiE(_M0L4selfS555);
  _M0L6_2atmpS2530 = Moonbit_array_length(_M0L6_2atmpS2531);
  moonbit_decref_cycle_free(_M0L6_2atmpS2531);
  if (_M0L3lenS2529 == _M0L6_2atmpS2530) {
    int32_t _M0L3lenS2533 = _M0L4selfS555->$1;
    int32_t _M0L6_2atmpS2532 = _M0L3lenS2533 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS555, _M0L6_2atmpS2532);
  }
  _M0L6lengthS556 = _M0L4selfS555->$1;
  _M0L3bufS2534 = _M0L4selfS555->$0;
  _M0L3bufS2534[_M0L6lengthS556] = _M0L5valueS557;
  _M0L6_2atmpS2535 = _M0L6lengthS556 + 1;
  _M0L4selfS555->$1 = _M0L6_2atmpS2535;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS531,
  int32_t _M0L8requiredS533
) {
  int32_t _M0L8old__capS530;
  int32_t _M0L3lenS2504;
  int32_t _M0L8new__capS532;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS530 = _M0MPC15array5Array8capacityGsE(_M0L4selfS531);
  _M0L3lenS2504 = _M0L4selfS531->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS532
  = _M0FPB23array__growth__capacity(_M0L8old__capS530, _M0L3lenS2504, _M0L8requiredS533);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS531, _M0L8new__capS532);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS535,
  int32_t _M0L8requiredS537
) {
  int32_t _M0L8old__capS534;
  int32_t _M0L3lenS2505;
  int32_t _M0L8new__capS536;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS534 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS535);
  _M0L3lenS2505 = _M0L4selfS535->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS536
  = _M0FPB23array__growth__capacity(_M0L8old__capS534, _M0L3lenS2505, _M0L8requiredS537);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS535, _M0L8new__capS536);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS539,
  int32_t _M0L8requiredS541
) {
  int32_t _M0L8old__capS538;
  int32_t _M0L3lenS2506;
  int32_t _M0L8new__capS540;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS538 = _M0MPC15array5Array8capacityGiE(_M0L4selfS539);
  _M0L3lenS2506 = _M0L4selfS539->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS540
  = _M0FPB23array__growth__capacity(_M0L8old__capS538, _M0L3lenS2506, _M0L8requiredS541);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS539, _M0L8new__capS540);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS543,
  int32_t _M0L8requiredS545
) {
  int32_t _M0L8old__capS542;
  int32_t _M0L3lenS2507;
  int32_t _M0L8new__capS544;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS542 = _M0MPC15array5Array8capacityGfE(_M0L4selfS543);
  _M0L3lenS2507 = _M0L4selfS543->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS544
  = _M0FPB23array__growth__capacity(_M0L8old__capS542, _M0L3lenS2507, _M0L8requiredS545);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS543, _M0L8new__capS544);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS507,
  int32_t _M0L13new__capacityS510
) {
  moonbit_string_t* _M0L8old__bufS506;
  int32_t _M0L3lenS508;
  int32_t _M0L9copy__lenS509;
  moonbit_string_t* _M0L8new__bufS511;
  moonbit_string_t* _M0L6_2aoldS5626;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS506 = _M0L4selfS507->$0;
  _M0L3lenS508 = _M0L4selfS507->$1;
  if (_M0L3lenS508 < _M0L13new__capacityS510) {
    _M0L9copy__lenS509 = _M0L3lenS508;
  } else {
    _M0L9copy__lenS509 = _M0L13new__capacityS510;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS506);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS511
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS506, _M0L13new__capacityS510, _M0L9copy__lenS509, 0, 0);
  _M0L6_2aoldS5626 = _M0L4selfS507->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5626);
  _M0L4selfS507->$0 = _M0L8new__bufS511;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS513,
  int32_t _M0L13new__capacityS516
) {
  struct _M0TUsiE** _M0L8old__bufS512;
  int32_t _M0L3lenS514;
  int32_t _M0L9copy__lenS515;
  struct _M0TUsiE** _M0L8new__bufS517;
  struct _M0TUsiE** _M0L6_2aoldS5627;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS512 = _M0L4selfS513->$0;
  _M0L3lenS514 = _M0L4selfS513->$1;
  if (_M0L3lenS514 < _M0L13new__capacityS516) {
    _M0L9copy__lenS515 = _M0L3lenS514;
  } else {
    _M0L9copy__lenS515 = _M0L13new__capacityS516;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS512);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS517
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS512, _M0L13new__capacityS516, _M0L9copy__lenS515, 0, 0);
  _M0L6_2aoldS5627 = _M0L4selfS513->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5627);
  _M0L4selfS513->$0 = _M0L8new__bufS517;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS519,
  int32_t _M0L13new__capacityS522
) {
  int32_t* _M0L8old__bufS518;
  int32_t _M0L3lenS520;
  int32_t _M0L9copy__lenS521;
  int32_t* _M0L8new__bufS523;
  int32_t* _M0L6_2aoldS5628;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS518 = _M0L4selfS519->$0;
  _M0L3lenS520 = _M0L4selfS519->$1;
  if (_M0L3lenS520 < _M0L13new__capacityS522) {
    _M0L9copy__lenS521 = _M0L3lenS520;
  } else {
    _M0L9copy__lenS521 = _M0L13new__capacityS522;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS518);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS523
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS518, _M0L13new__capacityS522, _M0L9copy__lenS521, 0, 0);
  _M0L6_2aoldS5628 = _M0L4selfS519->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5628);
  _M0L4selfS519->$0 = _M0L8new__bufS523;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS525,
  int32_t _M0L13new__capacityS528
) {
  float* _M0L8old__bufS524;
  int32_t _M0L3lenS526;
  int32_t _M0L9copy__lenS527;
  float* _M0L8new__bufS529;
  float* _M0L6_2aoldS5629;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS524 = _M0L4selfS525->$0;
  _M0L3lenS526 = _M0L4selfS525->$1;
  if (_M0L3lenS526 < _M0L13new__capacityS528) {
    _M0L9copy__lenS527 = _M0L3lenS526;
  } else {
    _M0L9copy__lenS527 = _M0L13new__capacityS528;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS524);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS529
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS524, _M0L13new__capacityS528, _M0L9copy__lenS527, 0, 0);
  _M0L6_2aoldS5629 = _M0L4selfS525->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5629);
  _M0L4selfS525->$0 = _M0L8new__bufS529;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS502
) {
  moonbit_string_t* _M0L6_2atmpS2500;
  int32_t _result_6058;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2500 = _M0MPC15array5Array6bufferGsE(_M0L4selfS502);
  _result_6058 = Moonbit_array_length(_M0L6_2atmpS2500);
  moonbit_decref_cycle_free(_M0L6_2atmpS2500);
  return _result_6058;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS503
) {
  struct _M0TUsiE** _M0L6_2atmpS2501;
  int32_t _result_6059;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2501 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS503);
  _result_6059 = Moonbit_array_length(_M0L6_2atmpS2501);
  moonbit_decref_cycle_free(_M0L6_2atmpS2501);
  return _result_6059;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS504
) {
  int32_t* _M0L6_2atmpS2502;
  int32_t _result_6060;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2502 = _M0MPC15array5Array6bufferGiE(_M0L4selfS504);
  _result_6060 = Moonbit_array_length(_M0L6_2atmpS2502);
  moonbit_decref_cycle_free(_M0L6_2atmpS2502);
  return _result_6060;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS505
) {
  float* _M0L6_2atmpS2503;
  int32_t _result_6061;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2503 = _M0MPC15array5Array6bufferGfE(_M0L4selfS505);
  _result_6061 = Moonbit_array_length(_M0L6_2atmpS2503);
  moonbit_decref_cycle_free(_M0L6_2atmpS2503);
  return _result_6061;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS498,
  int32_t _M0L3lenS496,
  int32_t _M0L8requiredS495
) {
  int32_t _M0L5startS497;
  int32_t _M0L5spaceS499;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS495 < _M0L3lenS496) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_23.data);
  }
  if (_M0L7currentS498 == 0) {
    _M0L5startS497 = 8;
  } else {
    _M0L5startS497 = _M0L7currentS498;
  }
  _M0L5spaceS499 = _M0L5startS497;
  while (1) {
    if (_M0L5spaceS499 < _M0L8requiredS495) {
      int32_t _M0L4nextS500 = _M0L5spaceS499 * 2;
      if (_M0L4nextS500 <= _M0L5spaceS499) {
        return _M0L8requiredS495;
      }
      _M0L5spaceS499 = _M0L4nextS500;
      continue;
    } else {
      return _M0L5spaceS499;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS492) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS492->$1;
}

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE* _M0L4selfS493) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS493->$1;
}

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE* _M0L4selfS494) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS494->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS486) {
  float* _M0L8_2afieldS5630;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5630 = _M0L4selfS486->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5630);
  return _M0L8_2afieldS5630;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L4selfS487
) {
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L8_2afieldS5631;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5631 = _M0L4selfS487->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5631);
  return _M0L8_2afieldS5631;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS488
) {
  moonbit_string_t* _M0L8_2afieldS5632;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5632 = _M0L4selfS488->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5632);
  return _M0L8_2afieldS5632;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS489
) {
  struct _M0TUsiE** _M0L8_2afieldS5633;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5633 = _M0L4selfS489->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5633);
  return _M0L8_2afieldS5633;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS490) {
  int32_t* _M0L8_2afieldS5634;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5634 = _M0L4selfS490->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5634);
  return _M0L8_2afieldS5634;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS491) {
  uint8_t* _M0L8_2afieldS5635;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5635 = _M0L4selfS491->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5635);
  return _M0L8_2afieldS5635;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS485
) {
  #line 220 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref_cycle_free(_M0L4selfS485);
  return _M0L4selfS485;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS484,
  struct _M0TPC16string10StringView _M0L3strS482
) {
  int32_t _M0L3endS2498;
  int32_t _M0L5startS2499;
  int32_t _M0L8str__lenS481;
  int32_t _M0L3lenS2497;
  int32_t _M0L8requiredS483;
  uint16_t* _M0L4dataS2490;
  int32_t _M0L6_2atmpS2489;
  int32_t _if__result_6063;
  uint16_t* _M0L4dataS2491;
  int32_t _M0L3lenS2492;
  moonbit_string_t _M0L6_2atmpS2493;
  int32_t _M0L6_2atmpS2494;
  int32_t _M0L3lenS2496;
  int32_t _M0L6_2atmpS2495;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS2498 = _M0L3strS482.$2;
  _M0L5startS2499 = _M0L3strS482.$1;
  _M0L8str__lenS481 = _M0L3endS2498 - _M0L5startS2499;
  if (_M0L8str__lenS481 == 0) {
    return 0;
  }
  _M0L3lenS2497 = _M0L4selfS484->$1;
  _M0L8requiredS483 = _M0L3lenS2497 + _M0L8str__lenS481;
  _M0L4dataS2490 = _M0L4selfS484->$0;
  _M0L6_2atmpS2489 = Moonbit_array_length(_M0L4dataS2490);
  if (_M0L8requiredS483 > _M0L6_2atmpS2489) {
    _if__result_6063 = 1;
  } else {
    int32_t _M0L3lenS2488 = _M0L4selfS484->$1;
    _if__result_6063 = _M0L8requiredS483 < _M0L3lenS2488;
  }
  if (_if__result_6063) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS484, _M0L8requiredS483);
  }
  _M0L4dataS2491 = _M0L4selfS484->$0;
  _M0L3lenS2492 = _M0L4selfS484->$1;
  moonbit_incref_cycle_free(_M0L4dataS2491);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2493 = _M0MPC16string10StringView4data(_M0L3strS482);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2494 = _M0MPC16string10StringView13start__offset(_M0L3strS482);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS2491, _M0L3lenS2492, _M0L6_2atmpS2493, _M0L6_2atmpS2494, _M0L8str__lenS481);
  moonbit_decref_cycle_free(_M0L4dataS2491);
  moonbit_decref_cycle_free(_M0L6_2atmpS2493);
  _M0L3lenS2496 = _M0L4selfS484->$1;
  _M0L6_2atmpS2495 = _M0L3lenS2496 + _M0L8str__lenS481;
  _M0L4selfS484->$1 = _M0L6_2atmpS2495;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS478,
  int32_t _M0L5startS476,
  int32_t _M0L3endS477
) {
  int32_t _if__result_6064;
  int32_t _M0L3lenS479;
  int32_t _M0L6_2atmpS2487;
  moonbit_bytes_t _M0L5bytesS480;
  moonbit_bytes_t _M0L6_2atmpS2486;
  moonbit_string_t _result_6065;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS476 == 0) {
    int32_t _M0L6_2atmpS2485 = Moonbit_array_length(_M0L3strS478);
    _if__result_6064 = _M0L3endS477 == _M0L6_2atmpS2485;
  } else {
    _if__result_6064 = 0;
  }
  if (_if__result_6064) {
    moonbit_incref_cycle_free(_M0L3strS478);
    return _M0L3strS478;
  }
  _M0L3lenS479 = _M0L3endS477 - _M0L5startS476;
  _M0L6_2atmpS2487 = _M0L3lenS479 * 2;
  _M0L5bytesS480 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS2487, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS480, 0, _M0L3strS478, _M0L5startS476, _M0L3lenS479);
  _M0L6_2atmpS2486 = _M0L5bytesS480;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_6065
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS2486, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS2486);
  return _result_6065;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS471,
  int32_t _M0L6offsetS475,
  int64_t _M0L6lengthS473
) {
  int32_t _M0L3lenS470;
  int32_t _M0L6lengthS472;
  int32_t _if__result_6066;
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L3lenS470 = Moonbit_array_length(_M0L4selfS471);
  if (_M0L6lengthS473 == 4294967296ll) {
    _M0L6lengthS472 = _M0L3lenS470 - _M0L6offsetS475;
  } else {
    int64_t _M0L7_2aSomeS474 = _M0L6lengthS473;
    _M0L6lengthS472 = (int32_t)_M0L7_2aSomeS474;
  }
  if (_M0L6offsetS475 >= 0) {
    if (_M0L6lengthS472 >= 0) {
      int32_t _M0L6_2atmpS2484 = _M0L6offsetS475 + _M0L6lengthS472;
      _if__result_6066 = _M0L6_2atmpS2484 <= _M0L3lenS470;
    } else {
      _if__result_6066 = 0;
    }
  } else {
    _if__result_6066 = 0;
  }
  if (_if__result_6066) {
    moonbit_incref_cycle_free(_M0L4selfS471);
    #line 85 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    return _M0FPB19unsafe__sub__string(_M0L4selfS471, _M0L6offsetS475, _M0L6lengthS472);
  } else {
    #line 84 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array10FixedArray18blit__from__string(
  moonbit_bytes_t _M0L4selfS462,
  int32_t _M0L13bytes__offsetS457,
  moonbit_string_t _M0L3strS464,
  int32_t _M0L11str__offsetS460,
  int32_t _M0L6lengthS458
) {
  int32_t _M0L6_2atmpS2483;
  int32_t _M0L6_2atmpS2482;
  int32_t _M0L2e1S456;
  int32_t _M0L6_2atmpS2481;
  int32_t _M0L2e2S459;
  int32_t _M0L4len1S461;
  int32_t _M0L4len2S463;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS2483 = _M0L6lengthS458 * 2;
  _M0L6_2atmpS2482 = _M0L13bytes__offsetS457 + _M0L6_2atmpS2483;
  _M0L2e1S456 = _M0L6_2atmpS2482 - 1;
  _M0L6_2atmpS2481 = _M0L11str__offsetS460 + _M0L6lengthS458;
  _M0L2e2S459 = _M0L6_2atmpS2481 - 1;
  _M0L4len1S461 = Moonbit_array_length(_M0L4selfS462);
  _M0L4len2S463 = Moonbit_array_length(_M0L3strS464);
  if (
    _M0L6lengthS458 >= 0
    && _M0L13bytes__offsetS457 >= 0
    && _M0L2e1S456 < _M0L4len1S461
    && _M0L11str__offsetS460 >= 0
    && _M0L2e2S459 < _M0L4len2S463
  ) {
    int32_t _M0L16end__str__offsetS465 =
      _M0L11str__offsetS460 + _M0L6lengthS458;
    int32_t _M0L1iS466 = _M0L11str__offsetS460;
    int32_t _M0L1jS467 = _M0L13bytes__offsetS457;
    while (1) {
      if (_M0L1iS466 < _M0L16end__str__offsetS465) {
        int32_t _M0L6_2atmpS2478 = _M0L3strS464[_M0L1iS466];
        int32_t _M0L6_2atmpS2477 = (int32_t)_M0L6_2atmpS2478;
        uint32_t _M0L1cS468 = *(uint32_t*)&_M0L6_2atmpS2477;
        uint32_t _M0L6_2atmpS2473 = _M0L1cS468 & 255u;
        int32_t _M0L6_2atmpS2472;
        int32_t _M0L6_2atmpS2474;
        uint32_t _M0L6_2atmpS2476;
        int32_t _M0L6_2atmpS2475;
        int32_t _M0L6_2atmpS2479;
        int32_t _M0L6_2atmpS2480;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS2472 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS2473);
        if (
          _M0L1jS467 < 0 || _M0L1jS467 >= Moonbit_array_length(_M0L4selfS462)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS462[_M0L1jS467] = _M0L6_2atmpS2472;
        _M0L6_2atmpS2474 = _M0L1jS467 + 1;
        _M0L6_2atmpS2476 = _M0L1cS468 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS2475 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS2476);
        if (
          _M0L6_2atmpS2474 < 0
          || _M0L6_2atmpS2474 >= Moonbit_array_length(_M0L4selfS462)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS462[_M0L6_2atmpS2474] = _M0L6_2atmpS2475;
        _M0L6_2atmpS2479 = _M0L1iS466 + 1;
        _M0L6_2atmpS2480 = _M0L1jS467 + 2;
        _M0L1iS466 = _M0L6_2atmpS2479;
        _M0L1jS467 = _M0L6_2atmpS2480;
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

int32_t _M0MPC14uint4UInt8to__byte(uint32_t _M0L4selfS455) {
  int32_t _M0L6_2atmpS2471;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2471 = *(int32_t*)&_M0L4selfS455;
  return _M0L6_2atmpS2471 & 0xff;
}

struct _M0TPB4IterGfE* _M0MPB4Iter3newGfE(
  struct _M0TWERPC16option6OptionGfE* _M0L1fS454,
  int64_t _M0L10size__hintS451
) {
  int64_t _M0L10size__hintS450;
  struct _M0TPB4IterGfE* _block_6068;
  #line 240 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\iterator.mbt"
  if (_M0L10size__hintS451 == 4294967296ll) {
    _M0L10size__hintS450 = 4294967296ll;
  } else {
    int64_t _M0L7_2aSomeS452 = _M0L10size__hintS451;
    int32_t _M0L4_2anS453 = (int32_t)_M0L7_2aSomeS452;
    if (_M0L4_2anS453 > 0) {
      _M0L10size__hintS450 = (int64_t)_M0L4_2anS453;
    } else {
      _M0L10size__hintS450 = _M0MPB4Iter3newN6constrS10992GfE;
    }
  }
  _block_6068
  = (struct _M0TPB4IterGfE*)moonbit_malloc(sizeof(struct _M0TPB4IterGfE));
  Moonbit_object_header(_block_6068)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 106, 0);
  _block_6068->$0 = _M0L1fS454;
  _block_6068->$1 = _M0L10size__hintS450;
  return _block_6068;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS442,
  int32_t _M0L5radixS441
) {
  uint16_t* _M0L6bufferS443;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS441 < 2 || _M0L5radixS441 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_24.data);
  }
  if (_M0L4selfS442 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  switch (_M0L5radixS441) {
    case 10: {
      int32_t _M0L3lenS444;
      uint16_t* _M0L6bufferS445;
      #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS444 = _M0FPB12dec__count64(_M0L4selfS442);
      _M0L6bufferS445 = (uint16_t*)moonbit_make_string(_M0L3lenS444, 0);
      #line 624 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS445, _M0L4selfS442, 0, _M0L3lenS444);
      _M0L6bufferS443 = _M0L6bufferS445;
      break;
    }
    
    case 16: {
      int32_t _M0L3lenS446;
      uint16_t* _M0L6bufferS447;
      #line 628 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS446 = _M0FPB12hex__count64(_M0L4selfS442);
      _M0L6bufferS447 = (uint16_t*)moonbit_make_string(_M0L3lenS446, 0);
      #line 630 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS447, _M0L4selfS442, 0, _M0L3lenS446);
      _M0L6bufferS443 = _M0L6bufferS447;
      break;
    }
    default: {
      int32_t _M0L3lenS448;
      uint16_t* _M0L6bufferS449;
      #line 634 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS448 = _M0FPB14radix__count64(_M0L4selfS442, _M0L5radixS441);
      _M0L6bufferS449 = (uint16_t*)moonbit_make_string(_M0L3lenS448, 0);
      #line 636 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS449, _M0L4selfS442, 0, _M0L3lenS448, _M0L5radixS441);
      _M0L6bufferS443 = _M0L6bufferS449;
      break;
    }
  }
  return _M0L6bufferS443;
}

moonbit_string_t _M0MPC15int645Int6418to__string_2einner(
  int64_t _M0L4selfS425,
  int32_t _M0L5radixS424
) {
  int32_t _M0L12is__negativeS426;
  uint64_t _M0L3numS427;
  uint16_t* _M0L6bufferS428;
  #line 548 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS424 < 2 || _M0L5radixS424 > 36) {
    #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_24.data);
  }
  if (_M0L4selfS425 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  _M0L12is__negativeS426 = _M0L4selfS425 < 0ll;
  if (_M0L12is__negativeS426) {
    int64_t _M0L6_2atmpS2470 = -_M0L4selfS425;
    _M0L3numS427 = *(uint64_t*)&_M0L6_2atmpS2470;
  } else {
    _M0L3numS427 = *(uint64_t*)&_M0L4selfS425;
  }
  switch (_M0L5radixS424) {
    case 10: {
      int32_t _M0L10digit__lenS429;
      int32_t _M0L6_2atmpS2467;
      int32_t _M0L10total__lenS430;
      uint16_t* _M0L6bufferS431;
      int32_t _M0L12digit__startS432;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS429 = _M0FPB12dec__count64(_M0L3numS427);
      if (_M0L12is__negativeS426) {
        _M0L6_2atmpS2467 = 1;
      } else {
        _M0L6_2atmpS2467 = 0;
      }
      _M0L10total__lenS430 = _M0L10digit__lenS429 + _M0L6_2atmpS2467;
      _M0L6bufferS431
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS430, 0);
      if (_M0L12is__negativeS426) {
        _M0L12digit__startS432 = 1;
      } else {
        _M0L12digit__startS432 = 0;
      }
      #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS431, _M0L3numS427, _M0L12digit__startS432, _M0L10total__lenS430);
      _M0L6bufferS428 = _M0L6bufferS431;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS433;
      int32_t _M0L6_2atmpS2468;
      int32_t _M0L10total__lenS434;
      uint16_t* _M0L6bufferS435;
      int32_t _M0L12digit__startS436;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS433 = _M0FPB12hex__count64(_M0L3numS427);
      if (_M0L12is__negativeS426) {
        _M0L6_2atmpS2468 = 1;
      } else {
        _M0L6_2atmpS2468 = 0;
      }
      _M0L10total__lenS434 = _M0L10digit__lenS433 + _M0L6_2atmpS2468;
      _M0L6bufferS435
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS434, 0);
      if (_M0L12is__negativeS426) {
        _M0L12digit__startS436 = 1;
      } else {
        _M0L12digit__startS436 = 0;
      }
      #line 585 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS435, _M0L3numS427, _M0L12digit__startS436, _M0L10total__lenS434);
      _M0L6bufferS428 = _M0L6bufferS435;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS437;
      int32_t _M0L6_2atmpS2469;
      int32_t _M0L10total__lenS438;
      uint16_t* _M0L6bufferS439;
      int32_t _M0L12digit__startS440;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS437
      = _M0FPB14radix__count64(_M0L3numS427, _M0L5radixS424);
      if (_M0L12is__negativeS426) {
        _M0L6_2atmpS2469 = 1;
      } else {
        _M0L6_2atmpS2469 = 0;
      }
      _M0L10total__lenS438 = _M0L10digit__lenS437 + _M0L6_2atmpS2469;
      _M0L6bufferS439
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS438, 0);
      if (_M0L12is__negativeS426) {
        _M0L12digit__startS440 = 1;
      } else {
        _M0L12digit__startS440 = 0;
      }
      #line 593 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS439, _M0L3numS427, _M0L12digit__startS440, _M0L10total__lenS438, _M0L5radixS424);
      _M0L6bufferS428 = _M0L6bufferS439;
      break;
    }
  }
  if (_M0L12is__negativeS426) {
    _M0L6bufferS428[0] = 45;
  }
  return _M0L6bufferS428;
}

int32_t _M0FPB22int64__to__string__dec(
  uint16_t* _M0L6bufferS410,
  uint64_t _M0L3numS422,
  int32_t _M0L12digit__startS411,
  int32_t _M0L10total__lenS423
) {
  int32_t _M0L6_2atmpS2466;
  uint64_t _M0L3numS400;
  int32_t _M0L6offsetS401;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2466 = _M0L10total__lenS423 - _M0L12digit__startS411;
  _M0L3numS400 = _M0L3numS422;
  _M0L6offsetS401 = _M0L6_2atmpS2466;
  while (1) {
    if (_M0L3numS400 >= 10000ull) {
      uint64_t _M0L1tS402 = _M0L3numS400 / 10000ull;
      uint64_t _M0L6_2atmpS2443 = _M0L3numS400 % 10000ull;
      int32_t _M0L1rS403 = (int32_t)_M0L6_2atmpS2443;
      int32_t _M0L2d1S404 = _M0L1rS403 / 100;
      int32_t _M0L2d2S405 = _M0L1rS403 % 100;
      int32_t _M0L6_2atmpS2442 = _M0L2d1S404 / 10;
      int32_t _M0L6_2atmpS2441 = 48 + _M0L6_2atmpS2442;
      int32_t _M0L6d1__hiS406 = (uint16_t)_M0L6_2atmpS2441;
      int32_t _M0L6_2atmpS2440 = _M0L2d1S404 % 10;
      int32_t _M0L6_2atmpS2439 = 48 + _M0L6_2atmpS2440;
      int32_t _M0L6d1__loS407 = (uint16_t)_M0L6_2atmpS2439;
      int32_t _M0L6_2atmpS2438 = _M0L2d2S405 / 10;
      int32_t _M0L6_2atmpS2437 = 48 + _M0L6_2atmpS2438;
      int32_t _M0L6d2__hiS408 = (uint16_t)_M0L6_2atmpS2437;
      int32_t _M0L6_2atmpS2436 = _M0L2d2S405 % 10;
      int32_t _M0L6_2atmpS2435 = 48 + _M0L6_2atmpS2436;
      int32_t _M0L6d2__loS409 = (uint16_t)_M0L6_2atmpS2435;
      int32_t _M0L6_2atmpS2427 = _M0L12digit__startS411 + _M0L6offsetS401;
      int32_t _M0L6_2atmpS2426 = _M0L6_2atmpS2427 - 4;
      int32_t _M0L6_2atmpS2429;
      int32_t _M0L6_2atmpS2428;
      int32_t _M0L6_2atmpS2431;
      int32_t _M0L6_2atmpS2430;
      int32_t _M0L6_2atmpS2433;
      int32_t _M0L6_2atmpS2432;
      int32_t _M0L6_2atmpS2434;
      _M0L6bufferS410[_M0L6_2atmpS2426] = _M0L6d1__hiS406;
      _M0L6_2atmpS2429 = _M0L12digit__startS411 + _M0L6offsetS401;
      _M0L6_2atmpS2428 = _M0L6_2atmpS2429 - 3;
      _M0L6bufferS410[_M0L6_2atmpS2428] = _M0L6d1__loS407;
      _M0L6_2atmpS2431 = _M0L12digit__startS411 + _M0L6offsetS401;
      _M0L6_2atmpS2430 = _M0L6_2atmpS2431 - 2;
      _M0L6bufferS410[_M0L6_2atmpS2430] = _M0L6d2__hiS408;
      _M0L6_2atmpS2433 = _M0L12digit__startS411 + _M0L6offsetS401;
      _M0L6_2atmpS2432 = _M0L6_2atmpS2433 - 1;
      _M0L6bufferS410[_M0L6_2atmpS2432] = _M0L6d2__loS409;
      _M0L6_2atmpS2434 = _M0L6offsetS401 - 4;
      _M0L3numS400 = _M0L1tS402;
      _M0L6offsetS401 = _M0L6_2atmpS2434;
      continue;
    } else {
      int32_t _M0L6_2atmpS2465 = (int32_t)_M0L3numS400;
      int32_t _M0L9remainingS413 = _M0L6_2atmpS2465;
      int32_t _M0L6offsetS414 = _M0L6offsetS401;
      while (1) {
        if (_M0L9remainingS413 >= 100) {
          int32_t _M0L1tS415 = _M0L9remainingS413 / 100;
          int32_t _M0L1dS416 = _M0L9remainingS413 % 100;
          int32_t _M0L6_2atmpS2452 = _M0L1dS416 / 10;
          int32_t _M0L6_2atmpS2451 = 48 + _M0L6_2atmpS2452;
          int32_t _M0L5d__hiS417 = (uint16_t)_M0L6_2atmpS2451;
          int32_t _M0L6_2atmpS2450 = _M0L1dS416 % 10;
          int32_t _M0L6_2atmpS2449 = 48 + _M0L6_2atmpS2450;
          int32_t _M0L5d__loS418 = (uint16_t)_M0L6_2atmpS2449;
          int32_t _M0L6_2atmpS2445 = _M0L12digit__startS411 + _M0L6offsetS414;
          int32_t _M0L6_2atmpS2444 = _M0L6_2atmpS2445 - 2;
          int32_t _M0L6_2atmpS2447;
          int32_t _M0L6_2atmpS2446;
          int32_t _M0L6_2atmpS2448;
          _M0L6bufferS410[_M0L6_2atmpS2444] = _M0L5d__hiS417;
          _M0L6_2atmpS2447 = _M0L12digit__startS411 + _M0L6offsetS414;
          _M0L6_2atmpS2446 = _M0L6_2atmpS2447 - 1;
          _M0L6bufferS410[_M0L6_2atmpS2446] = _M0L5d__loS418;
          _M0L6_2atmpS2448 = _M0L6offsetS414 - 2;
          _M0L9remainingS413 = _M0L1tS415;
          _M0L6offsetS414 = _M0L6_2atmpS2448;
          continue;
        } else if (_M0L9remainingS413 >= 10) {
          int32_t _M0L6_2atmpS2460 = _M0L9remainingS413 / 10;
          int32_t _M0L6_2atmpS2459 = 48 + _M0L6_2atmpS2460;
          int32_t _M0L5d__hiS420 = (uint16_t)_M0L6_2atmpS2459;
          int32_t _M0L6_2atmpS2458 = _M0L9remainingS413 % 10;
          int32_t _M0L6_2atmpS2457 = 48 + _M0L6_2atmpS2458;
          int32_t _M0L5d__loS421 = (uint16_t)_M0L6_2atmpS2457;
          int32_t _M0L6_2atmpS2454 = _M0L12digit__startS411 + _M0L6offsetS414;
          int32_t _M0L6_2atmpS2453 = _M0L6_2atmpS2454 - 2;
          int32_t _M0L6_2atmpS2456;
          int32_t _M0L6_2atmpS2455;
          _M0L6bufferS410[_M0L6_2atmpS2453] = _M0L5d__hiS420;
          _M0L6_2atmpS2456 = _M0L12digit__startS411 + _M0L6offsetS414;
          _M0L6_2atmpS2455 = _M0L6_2atmpS2456 - 1;
          _M0L6bufferS410[_M0L6_2atmpS2455] = _M0L5d__loS421;
        } else {
          int32_t _M0L6_2atmpS2464 = _M0L12digit__startS411 + _M0L6offsetS414;
          int32_t _M0L6_2atmpS2461 = _M0L6_2atmpS2464 - 1;
          int32_t _M0L6_2atmpS2463 = 48 + _M0L9remainingS413;
          int32_t _M0L6_2atmpS2462 = (uint16_t)_M0L6_2atmpS2463;
          _M0L6bufferS410[_M0L6_2atmpS2461] = _M0L6_2atmpS2462;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB26int64__to__string__generic(
  uint16_t* _M0L6bufferS390,
  uint64_t _M0L3numS394,
  int32_t _M0L12digit__startS391,
  int32_t _M0L10total__lenS393,
  int32_t _M0L5radixS384
) {
  uint64_t _M0L4baseS383;
  int32_t _M0L6_2atmpS2411;
  int32_t _M0L6_2atmpS2410;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS383 = _M0MPC13int3Int10to__uint64(_M0L5radixS384);
  _M0L6_2atmpS2411 = _M0L5radixS384 - 1;
  _M0L6_2atmpS2410 = _M0L5radixS384 & _M0L6_2atmpS2411;
  if (_M0L6_2atmpS2410 == 0) {
    int32_t _M0L5shiftS385;
    uint64_t _M0L4maskS386;
    int32_t _M0L6_2atmpS2418;
    int32_t _M0L6offsetS387;
    uint64_t _M0L1nS388;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS385 = moonbit_ctz32(_M0L5radixS384);
    _M0L4maskS386 = _M0L4baseS383 - 1ull;
    _M0L6_2atmpS2418 = _M0L10total__lenS393 - _M0L12digit__startS391;
    _M0L6offsetS387 = _M0L6_2atmpS2418;
    _M0L1nS388 = _M0L3numS394;
    while (1) {
      if (_M0L1nS388 > 0ull) {
        uint64_t _M0L6_2atmpS2417 = _M0L1nS388 & _M0L4maskS386;
        int32_t _M0L5digitS389 = (int32_t)_M0L6_2atmpS2417;
        int32_t _M0L6_2atmpS2414 = _M0L12digit__startS391 + _M0L6offsetS387;
        int32_t _M0L6_2atmpS2412 = _M0L6_2atmpS2414 - 1;
        int32_t _M0L6_2atmpS2413 =
          ((moonbit_string_t)moonbit_string_literal_25.data)[_M0L5digitS389];
        int32_t _M0L6_2atmpS2415;
        uint64_t _M0L6_2atmpS2416;
        _M0L6bufferS390[_M0L6_2atmpS2412] = _M0L6_2atmpS2413;
        _M0L6_2atmpS2415 = _M0L6offsetS387 - 1;
        _M0L6_2atmpS2416 = _M0L1nS388 >> (_M0L5shiftS385 & 63);
        _M0L6offsetS387 = _M0L6_2atmpS2415;
        _M0L1nS388 = _M0L6_2atmpS2416;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2425 = _M0L10total__lenS393 - _M0L12digit__startS391;
    int32_t _M0L6offsetS395 = _M0L6_2atmpS2425;
    uint64_t _M0L1nS396 = _M0L3numS394;
    while (1) {
      if (_M0L1nS396 > 0ull) {
        uint64_t _M0L1qS397 = _M0L1nS396 / _M0L4baseS383;
        uint64_t _M0L6_2atmpS2424 = _M0L1qS397 * _M0L4baseS383;
        uint64_t _M0L6_2atmpS2423 = _M0L1nS396 - _M0L6_2atmpS2424;
        int32_t _M0L5digitS398 = (int32_t)_M0L6_2atmpS2423;
        int32_t _M0L6_2atmpS2421 = _M0L12digit__startS391 + _M0L6offsetS395;
        int32_t _M0L6_2atmpS2419 = _M0L6_2atmpS2421 - 1;
        int32_t _M0L6_2atmpS2420 =
          ((moonbit_string_t)moonbit_string_literal_25.data)[_M0L5digitS398];
        int32_t _M0L6_2atmpS2422;
        _M0L6bufferS390[_M0L6_2atmpS2419] = _M0L6_2atmpS2420;
        _M0L6_2atmpS2422 = _M0L6offsetS395 - 1;
        _M0L6offsetS395 = _M0L6_2atmpS2422;
        _M0L1nS396 = _M0L1qS397;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB22int64__to__string__hex(
  uint16_t* _M0L6bufferS377,
  uint64_t _M0L3numS382,
  int32_t _M0L12digit__startS378,
  int32_t _M0L10total__lenS381
) {
  int32_t _M0L6_2atmpS2409;
  int32_t _M0L6offsetS372;
  uint64_t _M0L1nS373;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2409 = _M0L10total__lenS381 - _M0L12digit__startS378;
  _M0L6offsetS372 = _M0L6_2atmpS2409;
  _M0L1nS373 = _M0L3numS382;
  while (1) {
    if (_M0L6offsetS372 >= 2) {
      uint64_t _M0L6_2atmpS2406 = _M0L1nS373 & 255ull;
      int32_t _M0L9byte__valS374 = (int32_t)_M0L6_2atmpS2406;
      int32_t _M0L2hiS375 = _M0L9byte__valS374 / 16;
      int32_t _M0L2loS376 = _M0L9byte__valS374 % 16;
      int32_t _M0L6_2atmpS2400 = _M0L12digit__startS378 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS2398 = _M0L6_2atmpS2400 - 2;
      int32_t _M0L6_2atmpS2399 =
        ((moonbit_string_t)moonbit_string_literal_25.data)[_M0L2hiS375];
      int32_t _M0L6_2atmpS2403;
      int32_t _M0L6_2atmpS2401;
      int32_t _M0L6_2atmpS2402;
      int32_t _M0L6_2atmpS2404;
      uint64_t _M0L6_2atmpS2405;
      _M0L6bufferS377[_M0L6_2atmpS2398] = _M0L6_2atmpS2399;
      _M0L6_2atmpS2403 = _M0L12digit__startS378 + _M0L6offsetS372;
      _M0L6_2atmpS2401 = _M0L6_2atmpS2403 - 1;
      _M0L6_2atmpS2402
      = ((moonbit_string_t)moonbit_string_literal_25.data)[
        _M0L2loS376
      ];
      _M0L6bufferS377[_M0L6_2atmpS2401] = _M0L6_2atmpS2402;
      _M0L6_2atmpS2404 = _M0L6offsetS372 - 2;
      _M0L6_2atmpS2405 = _M0L1nS373 >> 8;
      _M0L6offsetS372 = _M0L6_2atmpS2404;
      _M0L1nS373 = _M0L6_2atmpS2405;
      continue;
    } else if (_M0L6offsetS372 == 1) {
      uint64_t _M0L6_2atmpS2408 = _M0L1nS373 & 15ull;
      int32_t _M0L6nibbleS380 = (int32_t)_M0L6_2atmpS2408;
      int32_t _M0L6_2atmpS2407 =
        ((moonbit_string_t)moonbit_string_literal_25.data)[_M0L6nibbleS380];
      _M0L6bufferS377[_M0L12digit__startS378] = _M0L6_2atmpS2407;
    }
    break;
  }
  return 0;
}

int32_t _M0FPB14radix__count64(
  uint64_t _M0L5valueS366,
  int32_t _M0L5radixS368
) {
  uint64_t _M0L4baseS367;
  uint64_t _M0L3numS369;
  int32_t _M0L5countS370;
  #line 419 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS366 == 0ull) {
    return 1;
  }
  #line 424 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS367 = _M0MPC13int3Int10to__uint64(_M0L5radixS368);
  _M0L3numS369 = _M0L5valueS366;
  _M0L5countS370 = 0;
  while (1) {
    if (_M0L3numS369 > 0ull) {
      uint64_t _M0L6_2atmpS2396 = _M0L3numS369 / _M0L4baseS367;
      int32_t _M0L6_2atmpS2397 = _M0L5countS370 + 1;
      _M0L3numS369 = _M0L6_2atmpS2396;
      _M0L5countS370 = _M0L6_2atmpS2397;
      continue;
    } else {
      return _M0L5countS370;
    }
    break;
  }
}

int32_t _M0FPB12hex__count64(uint64_t _M0L5valueS364) {
  #line 407 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS364 == 0ull) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS365;
    int32_t _M0L6_2atmpS2395;
    int32_t _M0L6_2atmpS2394;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS365 = moonbit_clz64(_M0L5valueS364);
    _M0L6_2atmpS2395 = 63 - _M0L14leading__zerosS365;
    _M0L6_2atmpS2394 = _M0L6_2atmpS2395 / 4;
    return _M0L6_2atmpS2394 + 1;
  }
}

int32_t _M0FPB12dec__count64(uint64_t _M0L5valueS363) {
  #line 343 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS363 >= 10000000000ull) {
    if (_M0L5valueS363 >= 100000000000000ull) {
      if (_M0L5valueS363 >= 10000000000000000ull) {
        if (_M0L5valueS363 >= 1000000000000000000ull) {
          if (_M0L5valueS363 >= 10000000000000000000ull) {
            return 20;
          } else {
            return 19;
          }
        } else if (_M0L5valueS363 >= 100000000000000000ull) {
          return 18;
        } else {
          return 17;
        }
      } else if (_M0L5valueS363 >= 1000000000000000ull) {
        return 16;
      } else {
        return 15;
      }
    } else if (_M0L5valueS363 >= 1000000000000ull) {
      if (_M0L5valueS363 >= 10000000000000ull) {
        return 14;
      } else {
        return 13;
      }
    } else if (_M0L5valueS363 >= 100000000000ull) {
      return 12;
    } else {
      return 11;
    }
  } else if (_M0L5valueS363 >= 100000ull) {
    if (_M0L5valueS363 >= 10000000ull) {
      if (_M0L5valueS363 >= 1000000000ull) {
        return 10;
      } else if (_M0L5valueS363 >= 100000000ull) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS363 >= 1000000ull) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS363 >= 1000ull) {
    if (_M0L5valueS363 >= 10000ull) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS363 >= 100ull) {
    return 3;
  } else if (_M0L5valueS363 >= 10ull) {
    return 2;
  } else {
    return 1;
  }
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS347,
  int32_t _M0L5radixS346
) {
  int32_t _M0L12is__negativeS348;
  uint32_t _M0L3numS349;
  uint16_t* _M0L6bufferS350;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS346 < 2 || _M0L5radixS346 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_24.data);
  }
  if (_M0L4selfS347 == 0) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  _M0L12is__negativeS348 = _M0L4selfS347 < 0;
  if (_M0L12is__negativeS348) {
    int32_t _M0L6_2atmpS2393 = -_M0L4selfS347;
    _M0L3numS349 = *(uint32_t*)&_M0L6_2atmpS2393;
  } else {
    _M0L3numS349 = *(uint32_t*)&_M0L4selfS347;
  }
  switch (_M0L5radixS346) {
    case 10: {
      int32_t _M0L10digit__lenS351;
      int32_t _M0L6_2atmpS2390;
      int32_t _M0L10total__lenS352;
      uint16_t* _M0L6bufferS353;
      int32_t _M0L12digit__startS354;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS351 = _M0FPB12dec__count32(_M0L3numS349);
      if (_M0L12is__negativeS348) {
        _M0L6_2atmpS2390 = 1;
      } else {
        _M0L6_2atmpS2390 = 0;
      }
      _M0L10total__lenS352 = _M0L10digit__lenS351 + _M0L6_2atmpS2390;
      _M0L6bufferS353
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS352, 0);
      if (_M0L12is__negativeS348) {
        _M0L12digit__startS354 = 1;
      } else {
        _M0L12digit__startS354 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS353, _M0L3numS349, _M0L12digit__startS354, _M0L10total__lenS352);
      _M0L6bufferS350 = _M0L6bufferS353;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS355;
      int32_t _M0L6_2atmpS2391;
      int32_t _M0L10total__lenS356;
      uint16_t* _M0L6bufferS357;
      int32_t _M0L12digit__startS358;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS355 = _M0FPB12hex__count32(_M0L3numS349);
      if (_M0L12is__negativeS348) {
        _M0L6_2atmpS2391 = 1;
      } else {
        _M0L6_2atmpS2391 = 0;
      }
      _M0L10total__lenS356 = _M0L10digit__lenS355 + _M0L6_2atmpS2391;
      _M0L6bufferS357
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS356, 0);
      if (_M0L12is__negativeS348) {
        _M0L12digit__startS358 = 1;
      } else {
        _M0L12digit__startS358 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS357, _M0L3numS349, _M0L12digit__startS358, _M0L10total__lenS356);
      _M0L6bufferS350 = _M0L6bufferS357;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS359;
      int32_t _M0L6_2atmpS2392;
      int32_t _M0L10total__lenS360;
      uint16_t* _M0L6bufferS361;
      int32_t _M0L12digit__startS362;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS359
      = _M0FPB14radix__count32(_M0L3numS349, _M0L5radixS346);
      if (_M0L12is__negativeS348) {
        _M0L6_2atmpS2392 = 1;
      } else {
        _M0L6_2atmpS2392 = 0;
      }
      _M0L10total__lenS360 = _M0L10digit__lenS359 + _M0L6_2atmpS2392;
      _M0L6bufferS361
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS360, 0);
      if (_M0L12is__negativeS348) {
        _M0L12digit__startS362 = 1;
      } else {
        _M0L12digit__startS362 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS361, _M0L3numS349, _M0L12digit__startS362, _M0L10total__lenS360, _M0L5radixS346);
      _M0L6bufferS350 = _M0L6bufferS361;
      break;
    }
  }
  if (_M0L12is__negativeS348) {
    _M0L6bufferS350[0] = 45;
  }
  return _M0L6bufferS350;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS340,
  int32_t _M0L5radixS342
) {
  uint32_t _M0L4baseS341;
  uint32_t _M0L3numS343;
  int32_t _M0L5countS344;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS340 == 0u) {
    return 1;
  }
  _M0L4baseS341 = *(uint32_t*)&_M0L5radixS342;
  _M0L3numS343 = _M0L5valueS340;
  _M0L5countS344 = 0;
  while (1) {
    if (_M0L3numS343 > 0u) {
      uint32_t _M0L6_2atmpS2388 = _M0L3numS343 / _M0L4baseS341;
      int32_t _M0L6_2atmpS2389 = _M0L5countS344 + 1;
      _M0L3numS343 = _M0L6_2atmpS2388;
      _M0L5countS344 = _M0L6_2atmpS2389;
      continue;
    } else {
      return _M0L5countS344;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS338) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS338 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS339;
    int32_t _M0L6_2atmpS2387;
    int32_t _M0L6_2atmpS2386;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS339 = moonbit_clz32(_M0L5valueS338);
    _M0L6_2atmpS2387 = 31 - _M0L14leading__zerosS339;
    _M0L6_2atmpS2386 = _M0L6_2atmpS2387 / 4;
    return _M0L6_2atmpS2386 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS337) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS337 >= 100000u) {
    if (_M0L5valueS337 >= 10000000u) {
      if (_M0L5valueS337 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS337 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS337 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS337 >= 1000u) {
    if (_M0L5valueS337 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS337 >= 100u) {
    return 3;
  } else if (_M0L5valueS337 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS323,
  uint32_t _M0L3numS335,
  int32_t _M0L12digit__startS324,
  int32_t _M0L10total__lenS336
) {
  int32_t _M0L6_2atmpS2385;
  uint32_t _M0L3numS313;
  int32_t _M0L6offsetS314;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2385 = _M0L10total__lenS336 - _M0L12digit__startS324;
  _M0L3numS313 = _M0L3numS335;
  _M0L6offsetS314 = _M0L6_2atmpS2385;
  while (1) {
    if (_M0L3numS313 >= 10000u) {
      uint32_t _M0L1tS315 = _M0L3numS313 / 10000u;
      uint32_t _M0L6_2atmpS2362 = _M0L3numS313 % 10000u;
      int32_t _M0L1rS316 = *(int32_t*)&_M0L6_2atmpS2362;
      int32_t _M0L2d1S317 = _M0L1rS316 / 100;
      int32_t _M0L2d2S318 = _M0L1rS316 % 100;
      int32_t _M0L6_2atmpS2361 = _M0L2d1S317 / 10;
      int32_t _M0L6_2atmpS2360 = 48 + _M0L6_2atmpS2361;
      int32_t _M0L6d1__hiS319 = (uint16_t)_M0L6_2atmpS2360;
      int32_t _M0L6_2atmpS2359 = _M0L2d1S317 % 10;
      int32_t _M0L6_2atmpS2358 = 48 + _M0L6_2atmpS2359;
      int32_t _M0L6d1__loS320 = (uint16_t)_M0L6_2atmpS2358;
      int32_t _M0L6_2atmpS2357 = _M0L2d2S318 / 10;
      int32_t _M0L6_2atmpS2356 = 48 + _M0L6_2atmpS2357;
      int32_t _M0L6d2__hiS321 = (uint16_t)_M0L6_2atmpS2356;
      int32_t _M0L6_2atmpS2355 = _M0L2d2S318 % 10;
      int32_t _M0L6_2atmpS2354 = 48 + _M0L6_2atmpS2355;
      int32_t _M0L6d2__loS322 = (uint16_t)_M0L6_2atmpS2354;
      int32_t _M0L6_2atmpS2346 = _M0L12digit__startS324 + _M0L6offsetS314;
      int32_t _M0L6_2atmpS2345 = _M0L6_2atmpS2346 - 4;
      int32_t _M0L6_2atmpS2348;
      int32_t _M0L6_2atmpS2347;
      int32_t _M0L6_2atmpS2350;
      int32_t _M0L6_2atmpS2349;
      int32_t _M0L6_2atmpS2352;
      int32_t _M0L6_2atmpS2351;
      int32_t _M0L6_2atmpS2353;
      _M0L6bufferS323[_M0L6_2atmpS2345] = _M0L6d1__hiS319;
      _M0L6_2atmpS2348 = _M0L12digit__startS324 + _M0L6offsetS314;
      _M0L6_2atmpS2347 = _M0L6_2atmpS2348 - 3;
      _M0L6bufferS323[_M0L6_2atmpS2347] = _M0L6d1__loS320;
      _M0L6_2atmpS2350 = _M0L12digit__startS324 + _M0L6offsetS314;
      _M0L6_2atmpS2349 = _M0L6_2atmpS2350 - 2;
      _M0L6bufferS323[_M0L6_2atmpS2349] = _M0L6d2__hiS321;
      _M0L6_2atmpS2352 = _M0L12digit__startS324 + _M0L6offsetS314;
      _M0L6_2atmpS2351 = _M0L6_2atmpS2352 - 1;
      _M0L6bufferS323[_M0L6_2atmpS2351] = _M0L6d2__loS322;
      _M0L6_2atmpS2353 = _M0L6offsetS314 - 4;
      _M0L3numS313 = _M0L1tS315;
      _M0L6offsetS314 = _M0L6_2atmpS2353;
      continue;
    } else {
      int32_t _M0L6_2atmpS2384 = *(int32_t*)&_M0L3numS313;
      int32_t _M0L9remainingS326 = _M0L6_2atmpS2384;
      int32_t _M0L6offsetS327 = _M0L6offsetS314;
      while (1) {
        if (_M0L9remainingS326 >= 100) {
          int32_t _M0L1tS328 = _M0L9remainingS326 / 100;
          int32_t _M0L1dS329 = _M0L9remainingS326 % 100;
          int32_t _M0L6_2atmpS2371 = _M0L1dS329 / 10;
          int32_t _M0L6_2atmpS2370 = 48 + _M0L6_2atmpS2371;
          int32_t _M0L5d__hiS330 = (uint16_t)_M0L6_2atmpS2370;
          int32_t _M0L6_2atmpS2369 = _M0L1dS329 % 10;
          int32_t _M0L6_2atmpS2368 = 48 + _M0L6_2atmpS2369;
          int32_t _M0L5d__loS331 = (uint16_t)_M0L6_2atmpS2368;
          int32_t _M0L6_2atmpS2364 = _M0L12digit__startS324 + _M0L6offsetS327;
          int32_t _M0L6_2atmpS2363 = _M0L6_2atmpS2364 - 2;
          int32_t _M0L6_2atmpS2366;
          int32_t _M0L6_2atmpS2365;
          int32_t _M0L6_2atmpS2367;
          _M0L6bufferS323[_M0L6_2atmpS2363] = _M0L5d__hiS330;
          _M0L6_2atmpS2366 = _M0L12digit__startS324 + _M0L6offsetS327;
          _M0L6_2atmpS2365 = _M0L6_2atmpS2366 - 1;
          _M0L6bufferS323[_M0L6_2atmpS2365] = _M0L5d__loS331;
          _M0L6_2atmpS2367 = _M0L6offsetS327 - 2;
          _M0L9remainingS326 = _M0L1tS328;
          _M0L6offsetS327 = _M0L6_2atmpS2367;
          continue;
        } else if (_M0L9remainingS326 >= 10) {
          int32_t _M0L6_2atmpS2379 = _M0L9remainingS326 / 10;
          int32_t _M0L6_2atmpS2378 = 48 + _M0L6_2atmpS2379;
          int32_t _M0L5d__hiS333 = (uint16_t)_M0L6_2atmpS2378;
          int32_t _M0L6_2atmpS2377 = _M0L9remainingS326 % 10;
          int32_t _M0L6_2atmpS2376 = 48 + _M0L6_2atmpS2377;
          int32_t _M0L5d__loS334 = (uint16_t)_M0L6_2atmpS2376;
          int32_t _M0L6_2atmpS2373 = _M0L12digit__startS324 + _M0L6offsetS327;
          int32_t _M0L6_2atmpS2372 = _M0L6_2atmpS2373 - 2;
          int32_t _M0L6_2atmpS2375;
          int32_t _M0L6_2atmpS2374;
          _M0L6bufferS323[_M0L6_2atmpS2372] = _M0L5d__hiS333;
          _M0L6_2atmpS2375 = _M0L12digit__startS324 + _M0L6offsetS327;
          _M0L6_2atmpS2374 = _M0L6_2atmpS2375 - 1;
          _M0L6bufferS323[_M0L6_2atmpS2374] = _M0L5d__loS334;
        } else {
          int32_t _M0L6_2atmpS2383 = _M0L12digit__startS324 + _M0L6offsetS327;
          int32_t _M0L6_2atmpS2380 = _M0L6_2atmpS2383 - 1;
          int32_t _M0L6_2atmpS2382 = 48 + _M0L9remainingS326;
          int32_t _M0L6_2atmpS2381 = (uint16_t)_M0L6_2atmpS2382;
          _M0L6bufferS323[_M0L6_2atmpS2380] = _M0L6_2atmpS2381;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS303,
  uint32_t _M0L3numS307,
  int32_t _M0L12digit__startS304,
  int32_t _M0L10total__lenS306,
  int32_t _M0L5radixS297
) {
  uint32_t _M0L4baseS296;
  int32_t _M0L6_2atmpS2330;
  int32_t _M0L6_2atmpS2329;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS296 = *(uint32_t*)&_M0L5radixS297;
  _M0L6_2atmpS2330 = _M0L5radixS297 - 1;
  _M0L6_2atmpS2329 = _M0L5radixS297 & _M0L6_2atmpS2330;
  if (_M0L6_2atmpS2329 == 0) {
    int32_t _M0L5shiftS298;
    uint32_t _M0L4maskS299;
    int32_t _M0L6_2atmpS2337;
    int32_t _M0L6offsetS300;
    uint32_t _M0L1nS301;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS298 = moonbit_ctz32(_M0L5radixS297);
    _M0L4maskS299 = _M0L4baseS296 - 1u;
    _M0L6_2atmpS2337 = _M0L10total__lenS306 - _M0L12digit__startS304;
    _M0L6offsetS300 = _M0L6_2atmpS2337;
    _M0L1nS301 = _M0L3numS307;
    while (1) {
      if (_M0L1nS301 > 0u) {
        uint32_t _M0L6_2atmpS2336 = _M0L1nS301 & _M0L4maskS299;
        int32_t _M0L5digitS302 = *(int32_t*)&_M0L6_2atmpS2336;
        int32_t _M0L6_2atmpS2333 = _M0L12digit__startS304 + _M0L6offsetS300;
        int32_t _M0L6_2atmpS2331 = _M0L6_2atmpS2333 - 1;
        int32_t _M0L6_2atmpS2332 =
          ((moonbit_string_t)moonbit_string_literal_25.data)[_M0L5digitS302];
        int32_t _M0L6_2atmpS2334;
        uint32_t _M0L6_2atmpS2335;
        _M0L6bufferS303[_M0L6_2atmpS2331] = _M0L6_2atmpS2332;
        _M0L6_2atmpS2334 = _M0L6offsetS300 - 1;
        _M0L6_2atmpS2335 = _M0L1nS301 >> (_M0L5shiftS298 & 31);
        _M0L6offsetS300 = _M0L6_2atmpS2334;
        _M0L1nS301 = _M0L6_2atmpS2335;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2344 = _M0L10total__lenS306 - _M0L12digit__startS304;
    int32_t _M0L6offsetS308 = _M0L6_2atmpS2344;
    uint32_t _M0L1nS309 = _M0L3numS307;
    while (1) {
      if (_M0L1nS309 > 0u) {
        uint32_t _M0L1qS310 = _M0L1nS309 / _M0L4baseS296;
        uint32_t _M0L6_2atmpS2343 = _M0L1qS310 * _M0L4baseS296;
        uint32_t _M0L6_2atmpS2342 = _M0L1nS309 - _M0L6_2atmpS2343;
        int32_t _M0L5digitS311 = *(int32_t*)&_M0L6_2atmpS2342;
        int32_t _M0L6_2atmpS2340 = _M0L12digit__startS304 + _M0L6offsetS308;
        int32_t _M0L6_2atmpS2338 = _M0L6_2atmpS2340 - 1;
        int32_t _M0L6_2atmpS2339 =
          ((moonbit_string_t)moonbit_string_literal_25.data)[_M0L5digitS311];
        int32_t _M0L6_2atmpS2341;
        _M0L6bufferS303[_M0L6_2atmpS2338] = _M0L6_2atmpS2339;
        _M0L6_2atmpS2341 = _M0L6offsetS308 - 1;
        _M0L6offsetS308 = _M0L6_2atmpS2341;
        _M0L1nS309 = _M0L1qS310;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS290,
  uint32_t _M0L3numS295,
  int32_t _M0L12digit__startS291,
  int32_t _M0L10total__lenS294
) {
  int32_t _M0L6_2atmpS2328;
  int32_t _M0L6offsetS285;
  uint32_t _M0L1nS286;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2328 = _M0L10total__lenS294 - _M0L12digit__startS291;
  _M0L6offsetS285 = _M0L6_2atmpS2328;
  _M0L1nS286 = _M0L3numS295;
  while (1) {
    if (_M0L6offsetS285 >= 2) {
      uint32_t _M0L6_2atmpS2325 = _M0L1nS286 & 255u;
      int32_t _M0L9byte__valS287 = *(int32_t*)&_M0L6_2atmpS2325;
      int32_t _M0L2hiS288 = _M0L9byte__valS287 / 16;
      int32_t _M0L2loS289 = _M0L9byte__valS287 % 16;
      int32_t _M0L6_2atmpS2319 = _M0L12digit__startS291 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS2317 = _M0L6_2atmpS2319 - 2;
      int32_t _M0L6_2atmpS2318 =
        ((moonbit_string_t)moonbit_string_literal_25.data)[_M0L2hiS288];
      int32_t _M0L6_2atmpS2322;
      int32_t _M0L6_2atmpS2320;
      int32_t _M0L6_2atmpS2321;
      int32_t _M0L6_2atmpS2323;
      uint32_t _M0L6_2atmpS2324;
      _M0L6bufferS290[_M0L6_2atmpS2317] = _M0L6_2atmpS2318;
      _M0L6_2atmpS2322 = _M0L12digit__startS291 + _M0L6offsetS285;
      _M0L6_2atmpS2320 = _M0L6_2atmpS2322 - 1;
      _M0L6_2atmpS2321
      = ((moonbit_string_t)moonbit_string_literal_25.data)[
        _M0L2loS289
      ];
      _M0L6bufferS290[_M0L6_2atmpS2320] = _M0L6_2atmpS2321;
      _M0L6_2atmpS2323 = _M0L6offsetS285 - 2;
      _M0L6_2atmpS2324 = _M0L1nS286 >> 8;
      _M0L6offsetS285 = _M0L6_2atmpS2323;
      _M0L1nS286 = _M0L6_2atmpS2324;
      continue;
    } else if (_M0L6offsetS285 == 1) {
      uint32_t _M0L6_2atmpS2327 = _M0L1nS286 & 15u;
      int32_t _M0L6nibbleS293 = *(int32_t*)&_M0L6_2atmpS2327;
      int32_t _M0L6_2atmpS2326 =
        ((moonbit_string_t)moonbit_string_literal_25.data)[_M0L6nibbleS293];
      _M0L6bufferS290[_M0L12digit__startS291] = _M0L6_2atmpS2326;
    }
    break;
  }
  return 0;
}

int32_t _M0MPB6Logger19write__iter_2einnerGfE(
  struct _M0TPB6Logger _M0L4selfS268,
  struct _M0TPB4IterGfE* _M0L4iterS272,
  moonbit_string_t _M0L6prefixS269,
  moonbit_string_t _M0L6suffixS284,
  moonbit_string_t _M0L3sepS275,
  int32_t _M0L8trailingS270
) {
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 197 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4selfS268.$0->$method_0(_M0L4selfS268.$1, _M0L6prefixS269);
  if (_M0L8trailingS270) {
    _2afor_276:;
    while (1) {
      void* _M0L7_2abindS271;
      #line 199 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
      _M0L7_2abindS271 = _M0MPB4Iter4nextGfE(_M0L4iterS272);
      switch (Moonbit_object_tag(_M0L7_2abindS271)) {
        case 1: {
          struct _M0DTPC16option6OptionGfE4Some* _M0L7_2aSomeS273 =
            (struct _M0DTPC16option6OptionGfE4Some*)_M0L7_2abindS271;
          float _M0L4_2axS274 = _M0L7_2aSomeS273->$0;
          moonbit_decref_cycle_free(_M0L7_2aSomeS273);
          #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
          _M0MPB6Logger13write__objectGfE(_M0L4selfS268, _M0L4_2axS274);
          #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
          _M0L4selfS268.$0->$method_0(_M0L4selfS268.$1, _M0L3sepS275);
          goto _2afor_276;
          break;
        }
        default: {
          moonbit_decref_cycle_free(_M0L7_2abindS271);
          break;
        }
      }
      break;
    }
  } else {
    void* _M0L7_2abindS277;
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
    _M0L7_2abindS277 = _M0MPB4Iter4nextGfE(_M0L4iterS272);
    switch (Moonbit_object_tag(_M0L7_2abindS277)) {
      case 1: {
        struct _M0DTPC16option6OptionGfE4Some* _M0L7_2aSomeS278 =
          (struct _M0DTPC16option6OptionGfE4Some*)_M0L7_2abindS277;
        float _M0L4_2axS279 = _M0L7_2aSomeS278->$0;
        moonbit_decref_cycle_free(_M0L7_2aSomeS278);
        #line 204 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
        _M0MPB6Logger13write__objectGfE(_M0L4selfS268, _M0L4_2axS279);
        _2afor_283:;
        while (1) {
          void* _M0L7_2abindS280;
          #line 205 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
          _M0L7_2abindS280 = _M0MPB4Iter4nextGfE(_M0L4iterS272);
          switch (Moonbit_object_tag(_M0L7_2abindS280)) {
            case 1: {
              struct _M0DTPC16option6OptionGfE4Some* _M0L7_2aSomeS281 =
                (struct _M0DTPC16option6OptionGfE4Some*)_M0L7_2abindS280;
              float _M0L4_2axS282 = _M0L7_2aSomeS281->$0;
              moonbit_decref_cycle_free(_M0L7_2aSomeS281);
              #line 206 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
              _M0L4selfS268.$0->$method_0(_M0L4selfS268.$1, _M0L3sepS275);
              #line 207 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
              _M0MPB6Logger13write__objectGfE(_M0L4selfS268, _M0L4_2axS282);
              goto _2afor_283;
              break;
            }
            default: {
              moonbit_decref_cycle_free(_M0L7_2abindS280);
              break;
            }
          }
          break;
        }
        break;
      }
      default: {
        moonbit_decref_cycle_free(_M0L7_2abindS277);
        break;
      }
    }
  }
  #line 210 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4selfS268.$0->$method_0(_M0L4selfS268.$1, _M0L6suffixS284);
  return 0;
}

void* _M0MPB4Iter4nextGfE(struct _M0TPB4IterGfE* _M0L4selfS263) {
  struct _M0TWERPC16option6OptionGfE* _M0L7_2afuncS262;
  void* _M0L6resultS264;
  int64_t _M0L7_2abindS265;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\iterator.mbt"
  _M0L7_2afuncS262 = _M0L4selfS263->$0;
  #line 41 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\iterator.mbt"
  _M0L6resultS264 = _M0L7_2afuncS262->code(_M0L7_2afuncS262);
  _M0L7_2abindS265 = _M0L4selfS263->$1;
  switch (Moonbit_object_tag(_M0L6resultS264)) {
    case 1: {
      if (_M0L7_2abindS265 == 4294967296ll) {
        
      } else {
        int64_t _M0L7_2aSomeS266 = _M0L7_2abindS265;
        int32_t _M0L4_2anS267 = (int32_t)_M0L7_2aSomeS266;
        int64_t _M0L6_2atmpS2315;
        if (_M0L4_2anS267 > 0) {
          int32_t _M0L6_2atmpS2316 = _M0L4_2anS267 - 1;
          _M0L6_2atmpS2315 = (int64_t)_M0L6_2atmpS2316;
        } else {
          _M0L6_2atmpS2315 = _M0MPB4Iter4nextN6constrS10984GfE;
        }
        _M0L4selfS263->$1 = _M0L6_2atmpS2315;
      }
      break;
    }
    default: {
      _M0L4selfS263->$1 = _M0MPB4Iter4nextN6constrS10985GfE;
      break;
    }
  }
  return _M0L6resultS264;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGfE* _M0L4selfS259
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS258;
  struct _M0TPB6Logger _M0L6_2atmpS2313;
  moonbit_string_t _result_6083;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS258 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS258);
  _M0L6_2atmpS2313
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS258
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPC15array5ArrayPB4Show6outputGfE(_M0L4selfS259, _M0L6_2atmpS2313);
  if (_M0L6_2atmpS2313.$1) {
    moonbit_decref(_M0L6_2atmpS2313.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_6083 = _M0MPB13StringBuilder10to__string(_M0L6loggerS258);
  moonbit_decref_cycle_free(_M0L6loggerS258);
  return _result_6083;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS261
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS260;
  struct _M0TPB6Logger _M0L6_2atmpS2314;
  moonbit_string_t _result_6084;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS260 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS260);
  _M0L6_2atmpS2314
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS260
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS261, _M0L6_2atmpS2314);
  if (_M0L6_2atmpS2314.$1) {
    moonbit_decref(_M0L6_2atmpS2314.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_6084 = _M0MPB13StringBuilder10to__string(_M0L6loggerS260);
  moonbit_decref_cycle_free(_M0L6loggerS260);
  return _result_6084;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS2309;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2309 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS2309);
  moonbit_decref_cycle_free(_M0L6_2atmpS2309);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS2310;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2310 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS2310);
  moonbit_decref_cycle_free(_M0L6_2atmpS2310);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGfE(
  float _M0L4selfS255,
  struct _M0TPB6Logger _M0L6loggerS254
) {
  moonbit_string_t _M0L6_2atmpS2311;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2311 = _M0IPC15float5FloatPB4Show10to__string(_M0L4selfS255);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254.$0->$method_0(_M0L6loggerS254.$1, _M0L6_2atmpS2311);
  moonbit_decref_cycle_free(_M0L6_2atmpS2311);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS257,
  struct _M0TPB6Logger _M0L6loggerS256
) {
  moonbit_string_t _M0L6_2atmpS2312;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2312 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS257);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS256.$0->$method_0(_M0L6loggerS256.$1, _M0L6_2atmpS2312);
  moonbit_decref_cycle_free(_M0L6_2atmpS2312);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS249
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS249.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS248
) {
  moonbit_string_t _M0L8_2afieldS5636;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS5636 = _M0L4selfS248.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5636);
  return _M0L8_2afieldS5636;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS244,
  moonbit_string_t _M0L5valueS245,
  int32_t _M0L5startS246,
  int32_t _M0L3lenS247
) {
  int32_t _M0L6_2atmpS2308;
  int64_t _M0L6_2atmpS2307;
  struct _M0TPC16string10StringView _M0L6_2atmpS2306;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2308 = _M0L5startS246 + _M0L3lenS247;
  _M0L6_2atmpS2307 = (int64_t)_M0L6_2atmpS2308;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2306
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS245, _M0L5startS246, _M0L6_2atmpS2307);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS244, _M0L6_2atmpS2306);
  moonbit_decref_cycle_free(_M0L6_2atmpS2306.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String21clamped__view_2einner(
  moonbit_string_t _M0L4selfS237,
  int32_t _M0L5startS239,
  int64_t _M0L3endS241
) {
  int32_t _M0L3lenS236;
  int32_t _M0Lm2loS238;
  int32_t _M0Lm2hiS240;
  int32_t _M0L6_2atmpS2290;
  int32_t _if__result_6085;
  int32_t _M0L6_2atmpS2298;
  int32_t _if__result_6086;
  int32_t _M0L6_2atmpS2300;
  int32_t _M0L6_2atmpS2301;
  #line 698 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS236 = Moonbit_array_length(_M0L4selfS237);
  if (_M0L5startS239 < 0) {
    _M0Lm2loS238 = 0;
  } else if (_M0L5startS239 > _M0L3lenS236) {
    _M0Lm2loS238 = _M0L3lenS236;
  } else {
    _M0Lm2loS238 = _M0L5startS239;
  }
  if (_M0L3endS241 == 4294967296ll) {
    _M0Lm2hiS240 = _M0L3lenS236;
  } else {
    int64_t _M0L7_2aSomeS242 = _M0L3endS241;
    int32_t _M0L4_2aeS243 = (int32_t)_M0L7_2aSomeS242;
    if (_M0L4_2aeS243 < 0) {
      _M0Lm2hiS240 = 0;
    } else if (_M0L4_2aeS243 > _M0L3lenS236) {
      _M0Lm2hiS240 = _M0L3lenS236;
    } else {
      _M0Lm2hiS240 = _M0L4_2aeS243;
    }
  }
  _M0L6_2atmpS2290 = _M0Lm2loS238;
  if (_M0L6_2atmpS2290 > 0) {
    int32_t _M0L6_2atmpS2289 = _M0Lm2loS238;
    if (_M0L6_2atmpS2289 < _M0L3lenS236) {
      int32_t _M0L6_2atmpS2288 = _M0Lm2loS238;
      int32_t _M0L6_2atmpS2287 = _M0L4selfS237[_M0L6_2atmpS2288];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2287)) {
        int32_t _M0L6_2atmpS2286 = _M0Lm2loS238;
        int32_t _M0L6_2atmpS2285 = _M0L6_2atmpS2286 - 1;
        int32_t _M0L6_2atmpS2284 = _M0L4selfS237[_M0L6_2atmpS2285];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_6085
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2284);
      } else {
        _if__result_6085 = 0;
      }
    } else {
      _if__result_6085 = 0;
    }
  } else {
    _if__result_6085 = 0;
  }
  if (_if__result_6085) {
    int32_t _M0L6_2atmpS2291 = _M0Lm2loS238;
    _M0Lm2loS238 = _M0L6_2atmpS2291 + 1;
  }
  _M0L6_2atmpS2298 = _M0Lm2hiS240;
  if (_M0L6_2atmpS2298 > 0) {
    int32_t _M0L6_2atmpS2297 = _M0Lm2hiS240;
    if (_M0L6_2atmpS2297 < _M0L3lenS236) {
      int32_t _M0L6_2atmpS2296 = _M0Lm2hiS240;
      int32_t _M0L6_2atmpS2295 = _M0L4selfS237[_M0L6_2atmpS2296];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2295)) {
        int32_t _M0L6_2atmpS2294 = _M0Lm2hiS240;
        int32_t _M0L6_2atmpS2293 = _M0L6_2atmpS2294 - 1;
        int32_t _M0L6_2atmpS2292 = _M0L4selfS237[_M0L6_2atmpS2293];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_6086
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2292);
      } else {
        _if__result_6086 = 0;
      }
    } else {
      _if__result_6086 = 0;
    }
  } else {
    _if__result_6086 = 0;
  }
  if (_if__result_6086) {
    int32_t _M0L6_2atmpS2299 = _M0Lm2hiS240;
    _M0Lm2hiS240 = _M0L6_2atmpS2299 - 1;
  }
  _M0L6_2atmpS2300 = _M0Lm2loS238;
  _M0L6_2atmpS2301 = _M0Lm2hiS240;
  if (_M0L6_2atmpS2300 >= _M0L6_2atmpS2301) {
    int32_t _M0L6_2atmpS2302 = _M0Lm2loS238;
    int32_t _M0L6_2atmpS2303 = _M0Lm2loS238;
    moonbit_incref_cycle_free(_M0L4selfS237);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS237,
                                                 .$1 = _M0L6_2atmpS2302,
                                                 .$2 = _M0L6_2atmpS2303};
  } else {
    int32_t _M0L6_2atmpS2304 = _M0Lm2loS238;
    int32_t _M0L6_2atmpS2305 = _M0Lm2hiS240;
    moonbit_incref_cycle_free(_M0L4selfS237);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS237,
                                                 .$1 = _M0L6_2atmpS2304,
                                                 .$2 = _M0L6_2atmpS2305};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS235,
  struct _M0TPB4Show _M0L4showS234
) {
  struct _M0TPB6Logger _M0L6_2atmpS2283;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS235);
  _M0L6_2atmpS2283
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS235
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS234.$0->$method_0(_M0L4showS234.$1, _M0L6_2atmpS2283);
  if (_M0L6_2atmpS2283.$1) {
    moonbit_decref(_M0L6_2atmpS2283.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS2282;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS2282
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS2282);
  if (_M0L6_2atmpS2282.$1) {
    moonbit_decref(_M0L6_2atmpS2282.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS231) {
  int64_t _M0L6_2atmpS2281;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2281 = (int64_t)_M0L4selfS231;
  return *(uint64_t*)&_M0L6_2atmpS2281;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

moonbit_string_t _M0MPC16string6String14escape_2einner(
  moonbit_string_t _M0L4selfS229,
  int32_t _M0L5quoteS230
) {
  struct _M0TPB13StringBuilder* _M0L3bufS228;
  int32_t _M0L6_2atmpS2280;
  struct _M0TPC16string10StringView _M0L6_2atmpS2278;
  struct _M0TPB6Logger _M0L6_2atmpS2279;
  moonbit_string_t _result_6087;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS228 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS2280 = Moonbit_array_length(_M0L4selfS229);
  moonbit_incref_cycle_free(_M0L4selfS229);
  _M0L6_2atmpS2278
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS229, .$1 = 0, .$2 = _M0L6_2atmpS2280
  };
  moonbit_incref_cycle_free(_M0L3bufS228);
  _M0L6_2atmpS2279
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS228
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS2278, _M0L6_2atmpS2279, _M0L5quoteS230);
  moonbit_decref_cycle_free(_M0L6_2atmpS2278.$0);
  if (_M0L6_2atmpS2279.$1) {
    moonbit_decref(_M0L6_2atmpS2279.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_6087 = _M0MPB13StringBuilder10to__string(_M0L3bufS228);
  moonbit_decref_cycle_free(_M0L3bufS228);
  return _result_6087;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS220,
  struct _M0TPB6Logger _M0L6loggerS218,
  int32_t _M0L5quoteS217
) {
  int32_t _M0L3endS2276;
  int32_t _M0L5startS2277;
  int32_t _M0L3lenS219;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS221;
  int32_t _M0L1iS222;
  int32_t _M0L3segS223;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS217) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS218.$0->$method_3(_M0L6loggerS218.$1, 34);
  }
  _M0L3endS2276 = _M0L4selfS220.$2;
  _M0L5startS2277 = _M0L4selfS220.$1;
  _M0L3lenS219 = _M0L3endS2276 - _M0L5startS2277;
  moonbit_incref_cycle_free(_M0L4selfS220.$0);
  if (_M0L6loggerS218.$1) {
    moonbit_incref(_M0L6loggerS218.$1);
  }
  _M0L6_2aenvS221
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS221)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 116, 0);
  _M0L6_2aenvS221->$0 = _M0L4selfS220;
  _M0L6_2aenvS221->$1 = _M0L6loggerS218;
  _M0L1iS222 = 0;
  _M0L3segS223 = 0;
  _2afor_224:;
  while (1) {
    moonbit_string_t _M0L3strS2273;
    int32_t _M0L5startS2275;
    int32_t _M0L6_2atmpS2274;
    int32_t _M0L4codeS225;
    int32_t _M0L1cS227;
    int32_t _M0L6_2atmpS2257;
    int32_t _M0L6_2atmpS2258;
    int32_t _M0L6_2atmpS2259;
    if (_M0L1iS222 >= _M0L3lenS219) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS221, _M0L3segS223, _M0L1iS222);
      moonbit_decref_cycle_free(_M0L6_2aenvS221);
      break;
    }
    _M0L3strS2273 = _M0L4selfS220.$0;
    _M0L5startS2275 = _M0L4selfS220.$1;
    _M0L6_2atmpS2274 = _M0L5startS2275 + _M0L1iS222;
    _M0L4codeS225 = _M0L3strS2273[_M0L6_2atmpS2274];
    switch (_M0L4codeS225) {
      case 34: {
        _M0L1cS227 = _M0L4codeS225;
        goto join_226;
        break;
      }
      
      case 92: {
        _M0L1cS227 = _M0L4codeS225;
        goto join_226;
        break;
      }
      
      case 10: {
        int32_t _M0L6_2atmpS2260;
        int32_t _M0L6_2atmpS2261;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS221, _M0L3segS223, _M0L1iS222);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS218.$0->$method_0(_M0L6loggerS218.$1, (moonbit_string_t)moonbit_string_literal_26.data);
        _M0L6_2atmpS2260 = _M0L1iS222 + 1;
        _M0L6_2atmpS2261 = _M0L1iS222 + 1;
        _M0L1iS222 = _M0L6_2atmpS2260;
        _M0L3segS223 = _M0L6_2atmpS2261;
        goto _2afor_224;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS2262;
        int32_t _M0L6_2atmpS2263;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS221, _M0L3segS223, _M0L1iS222);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS218.$0->$method_0(_M0L6loggerS218.$1, (moonbit_string_t)moonbit_string_literal_27.data);
        _M0L6_2atmpS2262 = _M0L1iS222 + 1;
        _M0L6_2atmpS2263 = _M0L1iS222 + 1;
        _M0L1iS222 = _M0L6_2atmpS2262;
        _M0L3segS223 = _M0L6_2atmpS2263;
        goto _2afor_224;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS2264;
        int32_t _M0L6_2atmpS2265;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS221, _M0L3segS223, _M0L1iS222);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS218.$0->$method_0(_M0L6loggerS218.$1, (moonbit_string_t)moonbit_string_literal_28.data);
        _M0L6_2atmpS2264 = _M0L1iS222 + 1;
        _M0L6_2atmpS2265 = _M0L1iS222 + 1;
        _M0L1iS222 = _M0L6_2atmpS2264;
        _M0L3segS223 = _M0L6_2atmpS2265;
        goto _2afor_224;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS2266;
        int32_t _M0L6_2atmpS2267;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS221, _M0L3segS223, _M0L1iS222);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS218.$0->$method_0(_M0L6loggerS218.$1, (moonbit_string_t)moonbit_string_literal_29.data);
        _M0L6_2atmpS2266 = _M0L1iS222 + 1;
        _M0L6_2atmpS2267 = _M0L1iS222 + 1;
        _M0L1iS222 = _M0L6_2atmpS2266;
        _M0L3segS223 = _M0L6_2atmpS2267;
        goto _2afor_224;
        break;
      }
      default: {
        if (_M0L4codeS225 < 32) {
          int32_t _M0L6_2atmpS2269;
          moonbit_string_t _M0L6_2atmpS2268;
          int32_t _M0L6_2atmpS2270;
          int32_t _M0L6_2atmpS2271;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS221, _M0L3segS223, _M0L1iS222);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS218.$0->$method_0(_M0L6loggerS218.$1, (moonbit_string_t)moonbit_string_literal_30.data);
          _M0L6_2atmpS2269 = _M0L4codeS225 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS2268 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS2269);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS218.$0->$method_0(_M0L6loggerS218.$1, _M0L6_2atmpS2268);
          moonbit_decref_cycle_free(_M0L6_2atmpS2268);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS218.$0->$method_0(_M0L6loggerS218.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS2270 = _M0L1iS222 + 1;
          _M0L6_2atmpS2271 = _M0L1iS222 + 1;
          _M0L1iS222 = _M0L6_2atmpS2270;
          _M0L3segS223 = _M0L6_2atmpS2271;
          goto _2afor_224;
        } else {
          int32_t _M0L6_2atmpS2272 = _M0L1iS222 + 1;
          int32_t _tmp_6090 = _M0L3segS223;
          _M0L1iS222 = _M0L6_2atmpS2272;
          _M0L3segS223 = _tmp_6090;
          goto _2afor_224;
        }
        break;
      }
    }
    goto joinlet_6089;
    join_226:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS221, _M0L3segS223, _M0L1iS222);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS218.$0->$method_3(_M0L6loggerS218.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2257 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS227);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS218.$0->$method_3(_M0L6loggerS218.$1, _M0L6_2atmpS2257);
    _M0L6_2atmpS2258 = _M0L1iS222 + 1;
    _M0L6_2atmpS2259 = _M0L1iS222 + 1;
    _M0L1iS222 = _M0L6_2atmpS2258;
    _M0L3segS223 = _M0L6_2atmpS2259;
    continue;
    joinlet_6089:;
    break;
  }
  if (_M0L5quoteS217) {
    #line 202 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS218.$0->$method_3(_M0L6loggerS218.$1, 34);
  }
  return 0;
}

int32_t _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS213,
  int32_t _M0L3segS216,
  int32_t _M0L1iS215
) {
  struct _M0TPB6Logger _M0L6loggerS212;
  struct _M0TPC16string10StringView _M0L4selfS214;
  #line 153 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6loggerS212 = _M0L6_2aenvS213->$1;
  _M0L4selfS214 = _M0L6_2aenvS213->$0;
  if (_M0L1iS215 > _M0L3segS216) {
    int64_t _M0L6_2atmpS2256 = (int64_t)_M0L1iS215;
    struct _M0TPC16string10StringView _M0L6_2atmpS2255;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2255
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS214, _M0L3segS216, _M0L6_2atmpS2256);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS212.$0->$method_2(_M0L6loggerS212.$1, _M0L6_2atmpS2255);
    moonbit_decref_cycle_free(_M0L6_2atmpS2255.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS203,
  int32_t _M0L5startS205,
  int64_t _M0L3endS207
) {
  int32_t _M0L3endS2253;
  int32_t _M0L5startS2254;
  int32_t _M0L3lenS202;
  int32_t _M0Lm2loS204;
  int32_t _M0Lm2hiS206;
  moonbit_string_t _M0L3strS210;
  int32_t _M0L4baseS211;
  int32_t _M0L6_2atmpS2231;
  int32_t _if__result_6091;
  int32_t _M0L6_2atmpS2241;
  int32_t _if__result_6092;
  int32_t _M0L6_2atmpS2243;
  int32_t _M0L6_2atmpS2244;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS2253 = _M0L4selfS203.$2;
  _M0L5startS2254 = _M0L4selfS203.$1;
  _M0L3lenS202 = _M0L3endS2253 - _M0L5startS2254;
  if (_M0L5startS205 < 0) {
    _M0Lm2loS204 = 0;
  } else if (_M0L5startS205 > _M0L3lenS202) {
    _M0Lm2loS204 = _M0L3lenS202;
  } else {
    _M0Lm2loS204 = _M0L5startS205;
  }
  if (_M0L3endS207 == 4294967296ll) {
    _M0Lm2hiS206 = _M0L3lenS202;
  } else {
    int64_t _M0L7_2aSomeS208 = _M0L3endS207;
    int32_t _M0L4_2aeS209 = (int32_t)_M0L7_2aSomeS208;
    if (_M0L4_2aeS209 < 0) {
      _M0Lm2hiS206 = 0;
    } else if (_M0L4_2aeS209 > _M0L3lenS202) {
      _M0Lm2hiS206 = _M0L3lenS202;
    } else {
      _M0Lm2hiS206 = _M0L4_2aeS209;
    }
  }
  _M0L3strS210 = _M0L4selfS203.$0;
  _M0L4baseS211 = _M0L4selfS203.$1;
  _M0L6_2atmpS2231 = _M0Lm2loS204;
  if (_M0L6_2atmpS2231 > 0) {
    int32_t _M0L6_2atmpS2230 = _M0Lm2loS204;
    if (_M0L6_2atmpS2230 < _M0L3lenS202) {
      int32_t _M0L6_2atmpS2229 = _M0Lm2loS204;
      int32_t _M0L6_2atmpS2228 = _M0L4baseS211 + _M0L6_2atmpS2229;
      int32_t _M0L6_2atmpS2227 = _M0L3strS210[_M0L6_2atmpS2228];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2227)) {
        int32_t _M0L6_2atmpS2226 = _M0Lm2loS204;
        int32_t _M0L6_2atmpS2225 = _M0L4baseS211 + _M0L6_2atmpS2226;
        int32_t _M0L6_2atmpS2224 = _M0L6_2atmpS2225 - 1;
        int32_t _M0L6_2atmpS2223 = _M0L3strS210[_M0L6_2atmpS2224];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_6091
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2223);
      } else {
        _if__result_6091 = 0;
      }
    } else {
      _if__result_6091 = 0;
    }
  } else {
    _if__result_6091 = 0;
  }
  if (_if__result_6091) {
    int32_t _M0L6_2atmpS2232 = _M0Lm2loS204;
    _M0Lm2loS204 = _M0L6_2atmpS2232 + 1;
  }
  _M0L6_2atmpS2241 = _M0Lm2hiS206;
  if (_M0L6_2atmpS2241 > 0) {
    int32_t _M0L6_2atmpS2240 = _M0Lm2hiS206;
    if (_M0L6_2atmpS2240 < _M0L3lenS202) {
      int32_t _M0L6_2atmpS2239 = _M0Lm2hiS206;
      int32_t _M0L6_2atmpS2238 = _M0L4baseS211 + _M0L6_2atmpS2239;
      int32_t _M0L6_2atmpS2237 = _M0L3strS210[_M0L6_2atmpS2238];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2237)) {
        int32_t _M0L6_2atmpS2236 = _M0Lm2hiS206;
        int32_t _M0L6_2atmpS2235 = _M0L4baseS211 + _M0L6_2atmpS2236;
        int32_t _M0L6_2atmpS2234 = _M0L6_2atmpS2235 - 1;
        int32_t _M0L6_2atmpS2233 = _M0L3strS210[_M0L6_2atmpS2234];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_6092
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2233);
      } else {
        _if__result_6092 = 0;
      }
    } else {
      _if__result_6092 = 0;
    }
  } else {
    _if__result_6092 = 0;
  }
  if (_if__result_6092) {
    int32_t _M0L6_2atmpS2242 = _M0Lm2hiS206;
    _M0Lm2hiS206 = _M0L6_2atmpS2242 - 1;
  }
  _M0L6_2atmpS2243 = _M0Lm2loS204;
  _M0L6_2atmpS2244 = _M0Lm2hiS206;
  if (_M0L6_2atmpS2243 >= _M0L6_2atmpS2244) {
    int32_t _M0L6_2atmpS2248 = _M0Lm2loS204;
    int32_t _M0L6_2atmpS2245 = _M0L4baseS211 + _M0L6_2atmpS2248;
    int32_t _M0L6_2atmpS2247 = _M0Lm2loS204;
    int32_t _M0L6_2atmpS2246 = _M0L4baseS211 + _M0L6_2atmpS2247;
    moonbit_incref_cycle_free(_M0L3strS210);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS210,
                                                 .$1 = _M0L6_2atmpS2245,
                                                 .$2 = _M0L6_2atmpS2246};
  } else {
    int32_t _M0L6_2atmpS2252 = _M0Lm2loS204;
    int32_t _M0L6_2atmpS2249 = _M0L4baseS211 + _M0L6_2atmpS2252;
    int32_t _M0L6_2atmpS2251 = _M0Lm2hiS206;
    int32_t _M0L6_2atmpS2250 = _M0L4baseS211 + _M0L6_2atmpS2251;
    moonbit_incref_cycle_free(_M0L3strS210);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS210,
                                                 .$1 = _M0L6_2atmpS2249,
                                                 .$2 = _M0L6_2atmpS2250};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS201) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS200;
  int32_t _M0L6_2atmpS2220;
  int32_t _M0L6_2atmpS2219;
  int32_t _M0L6_2atmpS2222;
  int32_t _M0L6_2atmpS2221;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS2218;
  moonbit_string_t _result_6093;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS200 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2220 = _M0IPC14byte4BytePB3Div3div(_M0L1bS201, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2219
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS2220);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS200, _M0L6_2atmpS2219);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2222 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS201, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2221
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS2222);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS200, _M0L6_2atmpS2221);
  _M0L6_2atmpS2218 = _M0L7_2aselfS200;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_6093 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS2218);
  moonbit_decref_cycle_free(_M0L6_2atmpS2218);
  return _result_6093;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS199) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS199 < 10) {
    int32_t _M0L6_2atmpS2215;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2215 = _M0IPC14byte4BytePB3Add3add(_M0L1iS199, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS2215);
  } else {
    int32_t _M0L6_2atmpS2217;
    int32_t _M0L6_2atmpS2216;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2217 = _M0IPC14byte4BytePB3Add3add(_M0L1iS199, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2216 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS2217, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS2216);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS197,
  int32_t _M0L4thatS198
) {
  int32_t _M0L6_2atmpS2213;
  int32_t _M0L6_2atmpS2214;
  int32_t _M0L6_2atmpS2212;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2213 = (int32_t)_M0L4selfS197;
  _M0L6_2atmpS2214 = (int32_t)_M0L4thatS198;
  _M0L6_2atmpS2212 = _M0L6_2atmpS2213 - _M0L6_2atmpS2214;
  return _M0L6_2atmpS2212 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS2210;
  int32_t _M0L6_2atmpS2211;
  int32_t _M0L6_2atmpS2209;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2210 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS2211 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS2209 = _M0L6_2atmpS2210 % _M0L6_2atmpS2211;
  return _M0L6_2atmpS2209 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS2207;
  int32_t _M0L6_2atmpS2208;
  int32_t _M0L6_2atmpS2206;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2207 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS2208 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS2206 = _M0L6_2atmpS2207 / _M0L6_2atmpS2208;
  return _M0L6_2atmpS2206 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS2204;
  int32_t _M0L6_2atmpS2205;
  int32_t _M0L6_2atmpS2203;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2204 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS2205 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS2203 = _M0L6_2atmpS2204 + _M0L6_2atmpS2205;
  return _M0L6_2atmpS2203 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS190) {
  int32_t _M0L6_2atmpS2202;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS2202 = (int32_t)_M0L4selfS190;
  return _M0L6_2atmpS2202;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS189) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS189 >= 56320 && _M0L4selfS189 <= 57343;
}

int32_t _M0MPC16uint166UInt1622is__leading__surrogate(int32_t _M0L4selfS188) {
  #line 28 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS188 >= 55296 && _M0L4selfS188 <= 56319;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS187,
  moonbit_string_t _M0L3strS185
) {
  int32_t _M0L8str__lenS184;
  int32_t _M0L3lenS2201;
  int32_t _M0L8requiredS186;
  uint16_t* _M0L4dataS2196;
  int32_t _M0L6_2atmpS2195;
  int32_t _if__result_6094;
  uint16_t* _M0L4dataS2197;
  int32_t _M0L3lenS2198;
  int32_t _M0L3lenS2200;
  int32_t _M0L6_2atmpS2199;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS184 = Moonbit_array_length(_M0L3strS185);
  if (_M0L8str__lenS184 == 0) {
    return 0;
  }
  _M0L3lenS2201 = _M0L4selfS187->$1;
  _M0L8requiredS186 = _M0L3lenS2201 + _M0L8str__lenS184;
  _M0L4dataS2196 = _M0L4selfS187->$0;
  _M0L6_2atmpS2195 = Moonbit_array_length(_M0L4dataS2196);
  if (_M0L8requiredS186 > _M0L6_2atmpS2195) {
    _if__result_6094 = 1;
  } else {
    int32_t _M0L3lenS2194 = _M0L4selfS187->$1;
    _if__result_6094 = _M0L8requiredS186 < _M0L3lenS2194;
  }
  if (_if__result_6094) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS187, _M0L8requiredS186);
  }
  _M0L4dataS2197 = _M0L4selfS187->$0;
  _M0L3lenS2198 = _M0L4selfS187->$1;
  moonbit_incref_cycle_free(_M0L4dataS2197);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS2197, _M0L3lenS2198, _M0L3strS185, 0, _M0L8str__lenS184);
  moonbit_decref_cycle_free(_M0L4dataS2197);
  _M0L3lenS2200 = _M0L4selfS187->$1;
  _M0L6_2atmpS2199 = _M0L3lenS2200 + _M0L8str__lenS184;
  _M0L4selfS187->$1 = _M0L6_2atmpS2199;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS180,
  int32_t _M0L11dst__offsetS183,
  moonbit_string_t _M0L3strS181,
  int32_t _M0L11str__offsetS176,
  int32_t _M0L3lenS177
) {
  int32_t _M0L16end__str__offsetS175;
  int32_t _M0L1iS178;
  int32_t _M0L1jS179;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS175 = _M0L11str__offsetS176 + _M0L3lenS177;
  _M0L1iS178 = _M0L11str__offsetS176;
  _M0L1jS179 = _M0L11dst__offsetS183;
  while (1) {
    if (_M0L1iS178 < _M0L16end__str__offsetS175) {
      int32_t _M0L6_2atmpS2191 = _M0L3strS181[_M0L1iS178];
      int32_t _M0L6_2atmpS2192;
      int32_t _M0L6_2atmpS2193;
      _M0L4selfS180[_M0L1jS179] = _M0L6_2atmpS2191;
      _M0L6_2atmpS2192 = _M0L1iS178 + 1;
      _M0L6_2atmpS2193 = _M0L1jS179 + 1;
      _M0L1iS178 = _M0L6_2atmpS2192;
      _M0L1jS179 = _M0L6_2atmpS2193;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS173,
  int32_t _M0L2chS172
) {
  uint32_t _M0L4codeS171;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS171 = _M0MPC14char4Char8to__uint(_M0L2chS172);
  if (_M0L4codeS171 <= 65535u) {
    int32_t _M0L3lenS2162 = _M0L4selfS173->$1;
    uint16_t* _M0L4dataS2164 = _M0L4selfS173->$0;
    int32_t _M0L6_2atmpS2163 = Moonbit_array_length(_M0L4dataS2164);
    uint16_t* _M0L4dataS2167;
    int32_t _M0L3lenS2168;
    int32_t _M0L6_2atmpS2169;
    int32_t _M0L3lenS2171;
    int32_t _M0L6_2atmpS2170;
    if (_M0L3lenS2162 >= _M0L6_2atmpS2163) {
      int32_t _M0L3lenS2166 = _M0L4selfS173->$1;
      int32_t _M0L6_2atmpS2165 = _M0L3lenS2166 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS173, _M0L6_2atmpS2165);
    }
    _M0L4dataS2167 = _M0L4selfS173->$0;
    _M0L3lenS2168 = _M0L4selfS173->$1;
    moonbit_incref_cycle_free(_M0L4dataS2167);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS2169 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS171);
    if (
      _M0L3lenS2168 < 0
      || _M0L3lenS2168 >= Moonbit_array_length(_M0L4dataS2167)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS2167[_M0L3lenS2168] = _M0L6_2atmpS2169;
    moonbit_decref_cycle_free(_M0L4dataS2167);
    _M0L3lenS2171 = _M0L4selfS173->$1;
    _M0L6_2atmpS2170 = _M0L3lenS2171 + 1;
    _M0L4selfS173->$1 = _M0L6_2atmpS2170;
  } else if (_M0L4codeS171 <= 1114111u) {
    uint16_t* _M0L4dataS2175 = _M0L4selfS173->$0;
    int32_t _M0L6_2atmpS2173 = Moonbit_array_length(_M0L4dataS2175);
    int32_t _M0L3lenS2174 = _M0L4selfS173->$1;
    int32_t _M0L6_2atmpS2172 = _M0L6_2atmpS2173 - _M0L3lenS2174;
    uint32_t _M0L4codeS174;
    uint16_t* _M0L4dataS2178;
    int32_t _M0L3lenS2179;
    uint32_t _M0L6_2atmpS2182;
    uint32_t _M0L6_2atmpS2181;
    int32_t _M0L6_2atmpS2180;
    uint16_t* _M0L4dataS2183;
    int32_t _M0L3lenS2188;
    int32_t _M0L6_2atmpS2184;
    uint32_t _M0L6_2atmpS2187;
    uint32_t _M0L6_2atmpS2186;
    int32_t _M0L6_2atmpS2185;
    int32_t _M0L3lenS2190;
    int32_t _M0L6_2atmpS2189;
    if (_M0L6_2atmpS2172 < 2) {
      int32_t _M0L3lenS2177 = _M0L4selfS173->$1;
      int32_t _M0L6_2atmpS2176 = _M0L3lenS2177 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS173, _M0L6_2atmpS2176);
    }
    _M0L4codeS174 = _M0L4codeS171 - 65536u;
    _M0L4dataS2178 = _M0L4selfS173->$0;
    _M0L3lenS2179 = _M0L4selfS173->$1;
    _M0L6_2atmpS2182 = _M0L4codeS174 >> 10;
    _M0L6_2atmpS2181 = 55296u + _M0L6_2atmpS2182;
    moonbit_incref_cycle_free(_M0L4dataS2178);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS2180 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS2181);
    if (
      _M0L3lenS2179 < 0
      || _M0L3lenS2179 >= Moonbit_array_length(_M0L4dataS2178)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS2178[_M0L3lenS2179] = _M0L6_2atmpS2180;
    moonbit_decref_cycle_free(_M0L4dataS2178);
    _M0L4dataS2183 = _M0L4selfS173->$0;
    _M0L3lenS2188 = _M0L4selfS173->$1;
    _M0L6_2atmpS2184 = _M0L3lenS2188 + 1;
    _M0L6_2atmpS2187 = _M0L4codeS174 & 1023u;
    _M0L6_2atmpS2186 = 56320u + _M0L6_2atmpS2187;
    moonbit_incref_cycle_free(_M0L4dataS2183);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS2185 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS2186);
    if (
      _M0L6_2atmpS2184 < 0
      || _M0L6_2atmpS2184 >= Moonbit_array_length(_M0L4dataS2183)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS2183[_M0L6_2atmpS2184] = _M0L6_2atmpS2185;
    moonbit_decref_cycle_free(_M0L4dataS2183);
    _M0L3lenS2190 = _M0L4selfS173->$1;
    _M0L6_2atmpS2189 = _M0L3lenS2190 + 2;
    _M0L4selfS173->$1 = _M0L6_2atmpS2189;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_31.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS168,
  int32_t _M0L8requiredS169
) {
  uint16_t* _M0L4dataS2161;
  int32_t _M0L6_2atmpS2159;
  int32_t _M0L3lenS2160;
  int32_t _M0L13new__capacityS167;
  uint16_t* _M0L4dataS2156;
  int32_t _M0L6_2atmpS2157;
  int32_t _M0L3lenS2158;
  uint16_t* _M0L9new__dataS170;
  uint16_t* _M0L6_2aoldS5637;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS2161 = _M0L4selfS168->$0;
  _M0L6_2atmpS2159 = Moonbit_array_length(_M0L4dataS2161);
  _M0L3lenS2160 = _M0L4selfS168->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS167
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS2159, _M0L3lenS2160, _M0L8requiredS169);
  _M0L4dataS2156 = _M0L4selfS168->$0;
  moonbit_incref_cycle_free(_M0L4dataS2156);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2157 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS2158 = _M0L4selfS168->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS170
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS2156, _M0L13new__capacityS167, _M0L6_2atmpS2157, _M0L3lenS2158, 0, 0);
  _M0L6_2aoldS5637 = _M0L4selfS168->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5637);
  _M0L4selfS168->$0 = _M0L9new__dataS170;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS166,
  int32_t _M0L3lenS162,
  int32_t _M0L8requiredS161
) {
  int32_t _M0L5spaceS163;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS161 < _M0L3lenS162) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_32.data);
  }
  _M0L5spaceS163 = _M0L7currentS166;
  while (1) {
    if (_M0L5spaceS163 < _M0L8requiredS161) {
      int32_t _M0L4nextS164 = _M0L5spaceS163 * 2;
      if (_M0L4nextS164 <= _M0L5spaceS163) {
        return _M0L8requiredS161;
      }
      _M0L5spaceS163 = _M0L4nextS164;
      continue;
    } else {
      return _M0L5spaceS163;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS160) {
  int32_t _M0L6_2atmpS2155;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2155 = *(int32_t*)&_M0L4selfS160;
  return (uint16_t)_M0L6_2atmpS2155;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS159) {
  int32_t _M0L6_2atmpS2154;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2154 = _M0L4selfS159;
  return *(uint32_t*)&_M0L6_2atmpS2154;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS157
) {
  int32_t _M0L3lenS2145;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS2145 = _M0L4selfS157->$1;
  if (_M0L3lenS2145 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS2146 = _M0L4selfS157->$1;
    uint16_t* _M0L4dataS2148 = _M0L4selfS157->$0;
    int32_t _M0L6_2atmpS2147 = Moonbit_array_length(_M0L4dataS2148);
    if (_M0L3lenS2146 == _M0L6_2atmpS2147) {
      uint16_t* _M0L4dataS2149 = _M0L4selfS157->$0;
      moonbit_incref_cycle_free(_M0L4dataS2149);
      return _M0L4dataS2149;
    } else {
      uint16_t* _M0L4dataS2150 = _M0L4selfS157->$0;
      int32_t _M0L3lenS2151 = _M0L4selfS157->$1;
      int32_t _M0L6_2atmpS2152;
      int32_t _M0L3lenS2153;
      uint16_t* _M0L4dataS158;
      moonbit_incref_cycle_free(_M0L4dataS2150);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS2152 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS2153 = _M0L4selfS157->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS158
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS2150, _M0L3lenS2151, _M0L6_2atmpS2152, _M0L3lenS2153, 0, 0);
      return _M0L4dataS158;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS154,
  int32_t _M0L13allocate__lenS150,
  int32_t _M0L4initS155,
  int32_t _M0L3lenS151,
  int32_t _M0L11src__offsetS152,
  int32_t _M0L11dst__offsetS153
) {
  int32_t _if__result_6097;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS150 >= 0) {
    if (_M0L3lenS151 >= 0) {
      if (_M0L11src__offsetS152 >= 0) {
        if (_M0L11dst__offsetS153 >= 0) {
          int32_t _M0L6_2atmpS2141 = _M0L11src__offsetS152 + _M0L3lenS151;
          int32_t _M0L6_2atmpS2142 = Moonbit_array_length(_M0L3srcS154);
          if (_M0L6_2atmpS2141 <= _M0L6_2atmpS2142) {
            int32_t _M0L6_2atmpS2140 = _M0L11dst__offsetS153 + _M0L3lenS151;
            _if__result_6097 = _M0L6_2atmpS2140 <= _M0L13allocate__lenS150;
          } else {
            _if__result_6097 = 0;
          }
        } else {
          _if__result_6097 = 0;
        }
      } else {
        _if__result_6097 = 0;
      }
    } else {
      _if__result_6097 = 0;
    }
  } else {
    _if__result_6097 = 0;
  }
  if (_if__result_6097) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS154, _M0L13allocate__lenS150, _M0L4initS155, _M0L11src__offsetS152, _M0L11dst__offsetS153, _M0L3lenS151);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS156;
    int32_t _M0L6_2atmpS2144;
    moonbit_string_t _M0L6_2atmpS2143;
    uint16_t* _result_6098;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS156
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS156, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS156, _M0L13allocate__lenS150);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS156, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS156, _M0L11src__offsetS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS156, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS156, _M0L11dst__offsetS153);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS156, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS156, _M0L3lenS151);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS156, (moonbit_string_t)moonbit_string_literal_37.data);
    _M0L6_2atmpS2144 = Moonbit_array_length(_M0L3srcS154);
    moonbit_decref_cycle_free(_M0L3srcS154);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS156, _M0L6_2atmpS2144);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS2143
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS156);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS156);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_6098 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS2143);
    moonbit_decref_cycle_free(_M0L6_2atmpS2143);
    return _result_6098;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS147,
  int32_t _M0L13allocate__lenS144,
  int32_t _M0L4initS145,
  int32_t _M0L11src__offsetS148,
  int32_t _M0L11dst__offsetS146,
  int32_t _M0L9blit__lenS149
) {
  uint16_t* _M0L3dstS143;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS143
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS144, _M0L4initS145);
  moonbit_incref_cycle_free(_M0L3dstS143);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS143, _M0L11dst__offsetS146, _M0L3srcS147, _M0L11src__offsetS148, _M0L9blit__lenS149, sizeof(uint16_t));
  return _M0L3dstS143;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS141
) {
  int32_t _M0L7initialS140;
  uint16_t* _M0L4dataS142;
  struct _M0TPB13StringBuilder* _block_6099;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS141 < 1) {
    _M0L7initialS140 = 1;
  } else {
    int32_t _M0L6_2atmpS2139 = _M0L10size__hintS141 + 1;
    _M0L7initialS140 = _M0L6_2atmpS2139 / 2;
  }
  _M0L4dataS142 = (uint16_t*)moonbit_make_string(_M0L7initialS140, 0);
  _block_6099
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_6099)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 121, 0);
  _block_6099->$0 = _M0L4dataS142;
  _block_6099->$1 = 0;
  return _block_6099;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS139) {
  int32_t _M0L6_2atmpS2138;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2138 = (int32_t)_M0L4selfS139;
  return _M0L6_2atmpS2138;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS119,
  int32_t _M0L13allocate__lenS115,
  int32_t _M0L3lenS116,
  int32_t _M0L11src__offsetS117,
  int32_t _M0L11dst__offsetS118
) {
  int32_t _if__result_6100;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS115 >= 0) {
    if (_M0L3lenS116 >= 0) {
      if (_M0L11src__offsetS117 >= 0) {
        if (_M0L11dst__offsetS118 >= 0) {
          int32_t _M0L6_2atmpS2119 = _M0L11src__offsetS117 + _M0L3lenS116;
          int32_t _M0L6_2atmpS2120;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2120
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS119);
          if (_M0L6_2atmpS2119 <= _M0L6_2atmpS2120) {
            int32_t _M0L6_2atmpS2118 = _M0L11dst__offsetS118 + _M0L3lenS116;
            _if__result_6100 = _M0L6_2atmpS2118 <= _M0L13allocate__lenS115;
          } else {
            _if__result_6100 = 0;
          }
        } else {
          _if__result_6100 = 0;
        }
      } else {
        _if__result_6100 = 0;
      }
    } else {
      _if__result_6100 = 0;
    }
  } else {
    _if__result_6100 = 0;
  }
  if (_if__result_6100) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS115, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS119, _M0L11src__offsetS117, _M0L11dst__offsetS118, _M0L3lenS116);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS120;
    int32_t _M0L6_2atmpS2122;
    moonbit_string_t _M0L6_2atmpS2121;
    moonbit_string_t* _result_6101;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS120
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS120, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS120, _M0L13allocate__lenS115);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS120, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS120, _M0L11src__offsetS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS120, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS120, _M0L11dst__offsetS118);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS120, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS120, _M0L3lenS116);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS120, (moonbit_string_t)moonbit_string_literal_37.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2122 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS119);
    moonbit_decref_cycle_free(_M0L3srcS119);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS120, _M0L6_2atmpS2122);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2121
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS120);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS120);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_6101
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS2121);
    moonbit_decref_cycle_free(_M0L6_2atmpS2121);
    return _result_6101;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS125,
  int32_t _M0L13allocate__lenS121,
  int32_t _M0L3lenS122,
  int32_t _M0L11src__offsetS123,
  int32_t _M0L11dst__offsetS124
) {
  int32_t _if__result_6102;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS121 >= 0) {
    if (_M0L3lenS122 >= 0) {
      if (_M0L11src__offsetS123 >= 0) {
        if (_M0L11dst__offsetS124 >= 0) {
          int32_t _M0L6_2atmpS2124 = _M0L11src__offsetS123 + _M0L3lenS122;
          int32_t _M0L6_2atmpS2125;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2125
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS125);
          if (_M0L6_2atmpS2124 <= _M0L6_2atmpS2125) {
            int32_t _M0L6_2atmpS2123 = _M0L11dst__offsetS124 + _M0L3lenS122;
            _if__result_6102 = _M0L6_2atmpS2123 <= _M0L13allocate__lenS121;
          } else {
            _if__result_6102 = 0;
          }
        } else {
          _if__result_6102 = 0;
        }
      } else {
        _if__result_6102 = 0;
      }
    } else {
      _if__result_6102 = 0;
    }
  } else {
    _if__result_6102 = 0;
  }
  if (_if__result_6102) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS121, 0, _M0L3srcS125, _M0L11src__offsetS123, _M0L11dst__offsetS124, _M0L3lenS122);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS126;
    int32_t _M0L6_2atmpS2127;
    moonbit_string_t _M0L6_2atmpS2126;
    struct _M0TUsiE** _result_6103;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS126
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L13allocate__lenS121);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L11src__offsetS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L11dst__offsetS124);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L3lenS122);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_37.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2127 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS125);
    moonbit_decref_cycle_free(_M0L3srcS125);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L6_2atmpS2127);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2126
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS126);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS126);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_6103
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS2126);
    moonbit_decref_cycle_free(_M0L6_2atmpS2126);
    return _result_6103;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS131,
  int32_t _M0L13allocate__lenS127,
  int32_t _M0L3lenS128,
  int32_t _M0L11src__offsetS129,
  int32_t _M0L11dst__offsetS130
) {
  int32_t _if__result_6104;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS127 >= 0) {
    if (_M0L3lenS128 >= 0) {
      if (_M0L11src__offsetS129 >= 0) {
        if (_M0L11dst__offsetS130 >= 0) {
          int32_t _M0L6_2atmpS2129 = _M0L11src__offsetS129 + _M0L3lenS128;
          int32_t _M0L6_2atmpS2130;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2130
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS131);
          if (_M0L6_2atmpS2129 <= _M0L6_2atmpS2130) {
            int32_t _M0L6_2atmpS2128 = _M0L11dst__offsetS130 + _M0L3lenS128;
            _if__result_6104 = _M0L6_2atmpS2128 <= _M0L13allocate__lenS127;
          } else {
            _if__result_6104 = 0;
          }
        } else {
          _if__result_6104 = 0;
        }
      } else {
        _if__result_6104 = 0;
      }
    } else {
      _if__result_6104 = 0;
    }
  } else {
    _if__result_6104 = 0;
  }
  if (_if__result_6104) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS131, _M0L13allocate__lenS127, _M0L11src__offsetS129, _M0L11dst__offsetS130, _M0L3lenS128);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS132;
    int32_t _M0L6_2atmpS2132;
    moonbit_string_t _M0L6_2atmpS2131;
    int32_t* _result_6105;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS132
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS132, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS132, _M0L13allocate__lenS127);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS132, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS132, _M0L11src__offsetS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS132, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS132, _M0L11dst__offsetS130);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS132, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS132, _M0L3lenS128);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS132, (moonbit_string_t)moonbit_string_literal_37.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2132 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS131);
    moonbit_decref_cycle_free(_M0L3srcS131);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS132, _M0L6_2atmpS2132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2131
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS132);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS132);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_6105
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS2131);
    moonbit_decref_cycle_free(_M0L6_2atmpS2131);
    return _result_6105;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS137,
  int32_t _M0L13allocate__lenS133,
  int32_t _M0L3lenS134,
  int32_t _M0L11src__offsetS135,
  int32_t _M0L11dst__offsetS136
) {
  int32_t _if__result_6106;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS133 >= 0) {
    if (_M0L3lenS134 >= 0) {
      if (_M0L11src__offsetS135 >= 0) {
        if (_M0L11dst__offsetS136 >= 0) {
          int32_t _M0L6_2atmpS2134 = _M0L11src__offsetS135 + _M0L3lenS134;
          int32_t _M0L6_2atmpS2135;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2135
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS137);
          if (_M0L6_2atmpS2134 <= _M0L6_2atmpS2135) {
            int32_t _M0L6_2atmpS2133 = _M0L11dst__offsetS136 + _M0L3lenS134;
            _if__result_6106 = _M0L6_2atmpS2133 <= _M0L13allocate__lenS133;
          } else {
            _if__result_6106 = 0;
          }
        } else {
          _if__result_6106 = 0;
        }
      } else {
        _if__result_6106 = 0;
      }
    } else {
      _if__result_6106 = 0;
    }
  } else {
    _if__result_6106 = 0;
  }
  if (_if__result_6106) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS137, _M0L13allocate__lenS133, _M0L11src__offsetS135, _M0L11dst__offsetS136, _M0L3lenS134);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS138;
    int32_t _M0L6_2atmpS2137;
    moonbit_string_t _M0L6_2atmpS2136;
    float* _result_6107;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS138
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS138, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS138, _M0L13allocate__lenS133);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS138, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS138, _M0L11src__offsetS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS138, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS138, _M0L11dst__offsetS136);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS138, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS138, _M0L3lenS134);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS138, (moonbit_string_t)moonbit_string_literal_37.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2137 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS137);
    moonbit_decref_cycle_free(_M0L3srcS137);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS138, _M0L6_2atmpS2137);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2136
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS138);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS138);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_6107
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS2136);
    moonbit_decref_cycle_free(_M0L6_2atmpS2136);
    return _result_6107;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  moonbit_string_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS2115;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS2115
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS109, _M0L6_2atmpS2115);
  if (_M0L6_2atmpS2115.$1) {
    moonbit_decref(_M0L6_2atmpS2115.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  int32_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS2116;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS2116
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS111, _M0L6_2atmpS2116);
  if (_M0L6_2atmpS2116.$1) {
    moonbit_decref(_M0L6_2atmpS2116.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS114,
  uint64_t _M0L3objS113
) {
  struct _M0TPB6Logger _M0L6_2atmpS2117;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS114);
  _M0L6_2atmpS2117
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS114
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS113, _M0L6_2atmpS2117);
  if (_M0L6_2atmpS2117.$1) {
    moonbit_decref(_M0L6_2atmpS2117.$1);
  }
  return 0;
}

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t* _M0L3srcS88,
  int32_t _M0L13allocate__lenS86,
  int32_t _M0L11src__offsetS89,
  int32_t _M0L11dst__offsetS87,
  int32_t _M0L9blit__lenS90
) {
  moonbit_string_t* _M0L3dstS85;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS85
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L13allocate__lenS86, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGsE(_M0L3dstS85, _M0L11dst__offsetS87, _M0L3srcS88, _M0L11src__offsetS89, _M0L9blit__lenS90);
  moonbit_decref_cycle_free(_M0L3srcS88);
  return _M0L3dstS85;
}

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE** _M0L3srcS94,
  int32_t _M0L13allocate__lenS92,
  int32_t _M0L11src__offsetS95,
  int32_t _M0L11dst__offsetS93,
  int32_t _M0L9blit__lenS96
) {
  struct _M0TUsiE** _M0L3dstS91;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS91
  = (struct _M0TUsiE**)moonbit_make_ref_array(_M0L13allocate__lenS92, 0);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGUsiEE(_M0L3dstS91, _M0L11dst__offsetS93, _M0L3srcS94, _M0L11src__offsetS95, _M0L9blit__lenS96);
  moonbit_decref_cycle_free(_M0L3srcS94);
  return _M0L3dstS91;
}

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t* _M0L3srcS100,
  int32_t _M0L13allocate__lenS98,
  int32_t _M0L11src__offsetS101,
  int32_t _M0L11dst__offsetS99,
  int32_t _M0L9blit__lenS102
) {
  int32_t* _M0L3dstS97;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS97
  = (int32_t*)moonbit_make_int32_array_raw(_M0L13allocate__lenS98);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L3dstS97, _M0L11dst__offsetS99, _M0L3srcS100, _M0L11src__offsetS101, _M0L9blit__lenS102);
  moonbit_decref_cycle_free(_M0L3srcS100);
  return _M0L3dstS97;
}

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS106,
  int32_t _M0L13allocate__lenS104,
  int32_t _M0L11src__offsetS107,
  int32_t _M0L11dst__offsetS105,
  int32_t _M0L9blit__lenS108
) {
  float* _M0L3dstS103;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS103
  = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS104);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS103, _M0L11dst__offsetS105, _M0L3srcS106, _M0L11src__offsetS107, _M0L9blit__lenS108);
  moonbit_decref_cycle_free(_M0L3srcS106);
  return _M0L3dstS103;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t* _M0L3dstS65,
  int32_t _M0L11dst__offsetS66,
  int32_t* _M0L3srcS67,
  int32_t _M0L11src__offsetS68,
  int32_t _M0L3lenS69
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS67);
  moonbit_incref_cycle_free(_M0L3dstS65);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS65, _M0L11dst__offsetS66, _M0L3srcS67, _M0L11src__offsetS68, _M0L3lenS69, sizeof(int32_t));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS70,
  int32_t _M0L11dst__offsetS71,
  float* _M0L3srcS72,
  int32_t _M0L11src__offsetS73,
  int32_t _M0L3lenS74
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS72);
  moonbit_incref_cycle_free(_M0L3dstS70);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS70, _M0L11dst__offsetS71, _M0L3srcS72, _M0L11src__offsetS73, _M0L3lenS74, sizeof(float));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS75,
  int32_t _M0L11dst__offsetS76,
  moonbit_string_t* _M0L3srcS77,
  int32_t _M0L11src__offsetS78,
  int32_t _M0L3lenS79
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS77);
  moonbit_incref_cycle_free(_M0L3dstS75);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS75, _M0L11dst__offsetS76, _M0L3srcS77, _M0L11src__offsetS78, _M0L3lenS79);
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS80,
  int32_t _M0L11dst__offsetS81,
  struct _M0TUsiE** _M0L3srcS82,
  int32_t _M0L11src__offsetS83,
  int32_t _M0L3lenS84
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS82);
  moonbit_incref_cycle_free(_M0L3dstS80);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS80, _M0L11dst__offsetS81, _M0L3srcS82, _M0L11src__offsetS83, _M0L3lenS84);
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS20,
  int32_t _M0L11dst__offsetS22,
  int32_t* _M0L3srcS21,
  int32_t _M0L11src__offsetS23,
  int32_t _M0L3lenS25
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS20 == _M0L3srcS21 && _M0L11dst__offsetS22 < _M0L11src__offsetS23
  ) {
    int32_t _M0L1iS24 = 0;
    while (1) {
      if (_M0L1iS24 < _M0L3lenS25) {
        int32_t _M0L6_2atmpS2070 = _M0L11dst__offsetS22 + _M0L1iS24;
        int32_t _M0L6_2atmpS2072 = _M0L11src__offsetS23 + _M0L1iS24;
        int32_t _M0L6_2atmpS2071;
        int32_t _M0L6_2atmpS2073;
        if (
          _M0L6_2atmpS2072 < 0
          || _M0L6_2atmpS2072 >= Moonbit_array_length(_M0L3srcS21)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2071 = (int32_t)_M0L3srcS21[_M0L6_2atmpS2072];
        if (
          _M0L6_2atmpS2070 < 0
          || _M0L6_2atmpS2070 >= Moonbit_array_length(_M0L3dstS20)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS20[_M0L6_2atmpS2070] = _M0L6_2atmpS2071;
        _M0L6_2atmpS2073 = _M0L1iS24 + 1;
        _M0L1iS24 = _M0L6_2atmpS2073;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS21);
        moonbit_decref_cycle_free(_M0L3dstS20);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2078 = _M0L3lenS25 - 1;
    int32_t _M0L1iS27 = _M0L6_2atmpS2078;
    while (1) {
      if (_M0L1iS27 >= 0) {
        int32_t _M0L6_2atmpS2074 = _M0L11dst__offsetS22 + _M0L1iS27;
        int32_t _M0L6_2atmpS2076 = _M0L11src__offsetS23 + _M0L1iS27;
        int32_t _M0L6_2atmpS2075;
        int32_t _M0L6_2atmpS2077;
        if (
          _M0L6_2atmpS2076 < 0
          || _M0L6_2atmpS2076 >= Moonbit_array_length(_M0L3srcS21)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2075 = (int32_t)_M0L3srcS21[_M0L6_2atmpS2076];
        if (
          _M0L6_2atmpS2074 < 0
          || _M0L6_2atmpS2074 >= Moonbit_array_length(_M0L3dstS20)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS20[_M0L6_2atmpS2074] = _M0L6_2atmpS2075;
        _M0L6_2atmpS2077 = _M0L1iS27 - 1;
        _M0L1iS27 = _M0L6_2atmpS2077;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS21);
        moonbit_decref_cycle_free(_M0L3dstS20);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS29,
  int32_t _M0L11dst__offsetS31,
  float* _M0L3srcS30,
  int32_t _M0L11src__offsetS32,
  int32_t _M0L3lenS34
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS29 == _M0L3srcS30 && _M0L11dst__offsetS31 < _M0L11src__offsetS32
  ) {
    int32_t _M0L1iS33 = 0;
    while (1) {
      if (_M0L1iS33 < _M0L3lenS34) {
        int32_t _M0L6_2atmpS2079 = _M0L11dst__offsetS31 + _M0L1iS33;
        int32_t _M0L6_2atmpS2081 = _M0L11src__offsetS32 + _M0L1iS33;
        float _M0L6_2atmpS2080;
        int32_t _M0L6_2atmpS2082;
        if (
          _M0L6_2atmpS2081 < 0
          || _M0L6_2atmpS2081 >= Moonbit_array_length(_M0L3srcS30)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2080 = (float)_M0L3srcS30[_M0L6_2atmpS2081];
        if (
          _M0L6_2atmpS2079 < 0
          || _M0L6_2atmpS2079 >= Moonbit_array_length(_M0L3dstS29)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS29[_M0L6_2atmpS2079] = _M0L6_2atmpS2080;
        _M0L6_2atmpS2082 = _M0L1iS33 + 1;
        _M0L1iS33 = _M0L6_2atmpS2082;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS30);
        moonbit_decref_cycle_free(_M0L3dstS29);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2087 = _M0L3lenS34 - 1;
    int32_t _M0L1iS36 = _M0L6_2atmpS2087;
    while (1) {
      if (_M0L1iS36 >= 0) {
        int32_t _M0L6_2atmpS2083 = _M0L11dst__offsetS31 + _M0L1iS36;
        int32_t _M0L6_2atmpS2085 = _M0L11src__offsetS32 + _M0L1iS36;
        float _M0L6_2atmpS2084;
        int32_t _M0L6_2atmpS2086;
        if (
          _M0L6_2atmpS2085 < 0
          || _M0L6_2atmpS2085 >= Moonbit_array_length(_M0L3srcS30)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2084 = (float)_M0L3srcS30[_M0L6_2atmpS2085];
        if (
          _M0L6_2atmpS2083 < 0
          || _M0L6_2atmpS2083 >= Moonbit_array_length(_M0L3dstS29)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS29[_M0L6_2atmpS2083] = _M0L6_2atmpS2084;
        _M0L6_2atmpS2086 = _M0L1iS36 - 1;
        _M0L1iS36 = _M0L6_2atmpS2086;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS30);
        moonbit_decref_cycle_free(_M0L3dstS29);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t* _M0L3dstS38,
  int32_t _M0L11dst__offsetS40,
  uint16_t* _M0L3srcS39,
  int32_t _M0L11src__offsetS41,
  int32_t _M0L3lenS43
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS38 == _M0L3srcS39 && _M0L11dst__offsetS40 < _M0L11src__offsetS41
  ) {
    int32_t _M0L1iS42 = 0;
    while (1) {
      if (_M0L1iS42 < _M0L3lenS43) {
        int32_t _M0L6_2atmpS2088 = _M0L11dst__offsetS40 + _M0L1iS42;
        int32_t _M0L6_2atmpS2090 = _M0L11src__offsetS41 + _M0L1iS42;
        int32_t _M0L6_2atmpS2089;
        int32_t _M0L6_2atmpS2091;
        if (
          _M0L6_2atmpS2090 < 0
          || _M0L6_2atmpS2090 >= Moonbit_array_length(_M0L3srcS39)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2089 = (int32_t)_M0L3srcS39[_M0L6_2atmpS2090];
        if (
          _M0L6_2atmpS2088 < 0
          || _M0L6_2atmpS2088 >= Moonbit_array_length(_M0L3dstS38)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS38[_M0L6_2atmpS2088] = _M0L6_2atmpS2089;
        _M0L6_2atmpS2091 = _M0L1iS42 + 1;
        _M0L1iS42 = _M0L6_2atmpS2091;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS39);
        moonbit_decref_cycle_free(_M0L3dstS38);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2096 = _M0L3lenS43 - 1;
    int32_t _M0L1iS45 = _M0L6_2atmpS2096;
    while (1) {
      if (_M0L1iS45 >= 0) {
        int32_t _M0L6_2atmpS2092 = _M0L11dst__offsetS40 + _M0L1iS45;
        int32_t _M0L6_2atmpS2094 = _M0L11src__offsetS41 + _M0L1iS45;
        int32_t _M0L6_2atmpS2093;
        int32_t _M0L6_2atmpS2095;
        if (
          _M0L6_2atmpS2094 < 0
          || _M0L6_2atmpS2094 >= Moonbit_array_length(_M0L3srcS39)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2093 = (int32_t)_M0L3srcS39[_M0L6_2atmpS2094];
        if (
          _M0L6_2atmpS2092 < 0
          || _M0L6_2atmpS2092 >= Moonbit_array_length(_M0L3dstS38)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS38[_M0L6_2atmpS2092] = _M0L6_2atmpS2093;
        _M0L6_2atmpS2095 = _M0L1iS45 - 1;
        _M0L1iS45 = _M0L6_2atmpS2095;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS39);
        moonbit_decref_cycle_free(_M0L3dstS38);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS47,
  int32_t _M0L11dst__offsetS49,
  moonbit_string_t* _M0L3srcS48,
  int32_t _M0L11src__offsetS50,
  int32_t _M0L3lenS52
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS47 == _M0L3srcS48 && _M0L11dst__offsetS49 < _M0L11src__offsetS50
  ) {
    int32_t _M0L1iS51 = 0;
    while (1) {
      if (_M0L1iS51 < _M0L3lenS52) {
        int32_t _M0L6_2atmpS2097 = _M0L11dst__offsetS49 + _M0L1iS51;
        int32_t _M0L6_2atmpS2099 = _M0L11src__offsetS50 + _M0L1iS51;
        moonbit_string_t _M0L6_2atmpS2098;
        moonbit_string_t _M0L6_2aoldS5638;
        int32_t _M0L6_2atmpS2100;
        if (
          _M0L6_2atmpS2099 < 0
          || _M0L6_2atmpS2099 >= Moonbit_array_length(_M0L3srcS48)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2098 = (moonbit_string_t)_M0L3srcS48[_M0L6_2atmpS2099];
        if (
          _M0L6_2atmpS2097 < 0
          || _M0L6_2atmpS2097 >= Moonbit_array_length(_M0L3dstS47)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5638 = (moonbit_string_t)_M0L3dstS47[_M0L6_2atmpS2097];
        moonbit_incref_cycle_free(_M0L6_2atmpS2098);
        moonbit_decref_cycle_free(_M0L6_2aoldS5638);
        _M0L3dstS47[_M0L6_2atmpS2097] = _M0L6_2atmpS2098;
        _M0L6_2atmpS2100 = _M0L1iS51 + 1;
        _M0L1iS51 = _M0L6_2atmpS2100;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS48);
        moonbit_decref_cycle_free(_M0L3dstS47);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2105 = _M0L3lenS52 - 1;
    int32_t _M0L1iS54 = _M0L6_2atmpS2105;
    while (1) {
      if (_M0L1iS54 >= 0) {
        int32_t _M0L6_2atmpS2101 = _M0L11dst__offsetS49 + _M0L1iS54;
        int32_t _M0L6_2atmpS2103 = _M0L11src__offsetS50 + _M0L1iS54;
        moonbit_string_t _M0L6_2atmpS2102;
        moonbit_string_t _M0L6_2aoldS5639;
        int32_t _M0L6_2atmpS2104;
        if (
          _M0L6_2atmpS2103 < 0
          || _M0L6_2atmpS2103 >= Moonbit_array_length(_M0L3srcS48)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2102 = (moonbit_string_t)_M0L3srcS48[_M0L6_2atmpS2103];
        if (
          _M0L6_2atmpS2101 < 0
          || _M0L6_2atmpS2101 >= Moonbit_array_length(_M0L3dstS47)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5639 = (moonbit_string_t)_M0L3dstS47[_M0L6_2atmpS2101];
        moonbit_incref_cycle_free(_M0L6_2atmpS2102);
        moonbit_decref_cycle_free(_M0L6_2aoldS5639);
        _M0L3dstS47[_M0L6_2atmpS2101] = _M0L6_2atmpS2102;
        _M0L6_2atmpS2104 = _M0L1iS54 - 1;
        _M0L1iS54 = _M0L6_2atmpS2104;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS48);
        moonbit_decref_cycle_free(_M0L3dstS47);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS56,
  int32_t _M0L11dst__offsetS58,
  struct _M0TUsiE** _M0L3srcS57,
  int32_t _M0L11src__offsetS59,
  int32_t _M0L3lenS61
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS56 == _M0L3srcS57 && _M0L11dst__offsetS58 < _M0L11src__offsetS59
  ) {
    int32_t _M0L1iS60 = 0;
    while (1) {
      if (_M0L1iS60 < _M0L3lenS61) {
        int32_t _M0L6_2atmpS2106 = _M0L11dst__offsetS58 + _M0L1iS60;
        int32_t _M0L6_2atmpS2108 = _M0L11src__offsetS59 + _M0L1iS60;
        struct _M0TUsiE* _M0L6_2atmpS2107;
        struct _M0TUsiE* _M0L6_2aoldS5640;
        int32_t _M0L6_2atmpS2109;
        if (
          _M0L6_2atmpS2108 < 0
          || _M0L6_2atmpS2108 >= Moonbit_array_length(_M0L3srcS57)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2107 = (struct _M0TUsiE*)_M0L3srcS57[_M0L6_2atmpS2108];
        if (
          _M0L6_2atmpS2106 < 0
          || _M0L6_2atmpS2106 >= Moonbit_array_length(_M0L3dstS56)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5640 = (struct _M0TUsiE*)_M0L3dstS56[_M0L6_2atmpS2106];
        if (_M0L6_2atmpS2107) {
          moonbit_incref_cycle_free(_M0L6_2atmpS2107);
        }
        if (_M0L6_2aoldS5640) {
          moonbit_decref_cycle_free(_M0L6_2aoldS5640);
        }
        _M0L3dstS56[_M0L6_2atmpS2106] = _M0L6_2atmpS2107;
        _M0L6_2atmpS2109 = _M0L1iS60 + 1;
        _M0L1iS60 = _M0L6_2atmpS2109;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS57);
        moonbit_decref_cycle_free(_M0L3dstS56);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2114 = _M0L3lenS61 - 1;
    int32_t _M0L1iS63 = _M0L6_2atmpS2114;
    while (1) {
      if (_M0L1iS63 >= 0) {
        int32_t _M0L6_2atmpS2110 = _M0L11dst__offsetS58 + _M0L1iS63;
        int32_t _M0L6_2atmpS2112 = _M0L11src__offsetS59 + _M0L1iS63;
        struct _M0TUsiE* _M0L6_2atmpS2111;
        struct _M0TUsiE* _M0L6_2aoldS5641;
        int32_t _M0L6_2atmpS2113;
        if (
          _M0L6_2atmpS2112 < 0
          || _M0L6_2atmpS2112 >= Moonbit_array_length(_M0L3srcS57)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2111 = (struct _M0TUsiE*)_M0L3srcS57[_M0L6_2atmpS2112];
        if (
          _M0L6_2atmpS2110 < 0
          || _M0L6_2atmpS2110 >= Moonbit_array_length(_M0L3dstS56)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5641 = (struct _M0TUsiE*)_M0L3dstS56[_M0L6_2atmpS2110];
        if (_M0L6_2atmpS2111) {
          moonbit_incref_cycle_free(_M0L6_2atmpS2111);
        }
        if (_M0L6_2aoldS5641) {
          moonbit_decref_cycle_free(_M0L6_2aoldS5641);
        }
        _M0L3dstS56[_M0L6_2atmpS2110] = _M0L6_2atmpS2111;
        _M0L6_2atmpS2113 = _M0L1iS63 - 1;
        _M0L1iS63 = _M0L6_2atmpS2113;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS57);
        moonbit_decref_cycle_free(_M0L3dstS56);
      }
      break;
    }
  }
  return 0;
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

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t* _M0L4selfS18) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS18);
}

int32_t _M0MPB18UninitializedArray6lengthGfE(float* _M0L4selfS19) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS19);
}

int32_t _M0IPB7FailurePB4Show6output(
  void* _M0L10_2ax__6387S12,
  struct _M0TPB6Logger _M0L10_2ax__6388S15
) {
  struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS13;
  moonbit_string_t _M0L15_2a_2aarg__6389S14;
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2aFailureS13
  = (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L10_2ax__6387S12;
  _M0L15_2a_2aarg__6389S14 = _M0L10_2aFailureS13->$0;
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S15.$0->$method_0(_M0L10_2ax__6388S15.$1, (moonbit_string_t)moonbit_string_literal_38.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S15, _M0L15_2a_2aarg__6389S14);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S15.$0->$method_0(_M0L10_2ax__6388S15.$1, (moonbit_string_t)moonbit_string_literal_39.data);
  return 0;
}

int32_t _M0MPB6Logger13write__objectGfE(
  struct _M0TPB6Logger _M0L4selfS9,
  float _M0L3objS8
) {
  #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 180 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IP016_24default__implPB4Show6outputGfE(_M0L3objS8, _M0L4selfS9);
  return 0;
}

int32_t _M0MPB6Logger13write__objectGsE(
  struct _M0TPB6Logger _M0L4selfS11,
  moonbit_string_t _M0L3objS10
) {
  #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 180 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS10, _M0L4selfS11);
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

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(
  moonbit_string_t _M0L3msgS5
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS5);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS2037) {
  switch (Moonbit_object_tag(_M0L4_2aeS2037)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_40.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS2037);
      break;
    }
    
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_41.data;
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_42.data;
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_43.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS2065,
  struct _M0TPB4Show _M0L8_2aparamS2064
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2063 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2065;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS2063, _M0L8_2aparamS2064);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS2062,
  struct _M0TPB4Show _M0L8_2aparamS2061
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2060 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2062;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS2060, _M0L8_2aparamS2061);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS2059,
  int32_t _M0L8_2aparamS2058
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2057 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2059;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS2057, _M0L8_2aparamS2058);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS2056,
  struct _M0TPC16string10StringView _M0L8_2aparamS2055
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2054 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2056;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS2054, _M0L8_2aparamS2055);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS2053,
  moonbit_string_t _M0L8_2aparamS2050,
  int32_t _M0L8_2aparamS2051,
  int32_t _M0L8_2aparamS2052
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2049 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2053;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS2049, _M0L8_2aparamS2050, _M0L8_2aparamS2051, _M0L8_2aparamS2052);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS2048,
  moonbit_string_t _M0L8_2aparamS2047
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2046 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2048;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS2046, _M0L8_2aparamS2047);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_6118 = 9218868437227405311ll;
  int64_t _tmp_6119;
  int64_t _tmp_6120;
  int64_t _tmp_6121;
  int64_t _tmp_6122;
  _M0FPB18double__max__value = *(double*)&_tmp_6118;
  _tmp_6119 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_6119;
  _tmp_6120 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_6120;
  _tmp_6121 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_6121;
  _tmp_6122 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_6122;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS2069;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS2030;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS2031;
  int32_t _M0L7_2abindS2032;
  struct _M0TUsiE** _M0L7_2abindS2033;
  int32_t _M0L6_2acntS5818;
  int32_t _M0L2__S2034;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS2069
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS2030
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS2030)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 124, 0);
  _M0L12async__testsS2030->$0 = _M0L6_2atmpS2069;
  _M0L12async__testsS2030->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS2031
  = _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS2032 = _M0L7_2abindS2031->$1;
  _M0L7_2abindS2033 = _M0L7_2abindS2031->$0;
  _M0L6_2acntS5818
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS2031));
  if (_M0L6_2acntS5818 > 1) {
    int32_t _M0L11_2anew__cntS5819 = _M0L6_2acntS5818 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS2031), _M0L11_2anew__cntS5819);
    moonbit_incref_cycle_free(_M0L7_2abindS2033);
  } else if (_M0L6_2acntS5818 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS2031);
  }
  _M0L2__S2034 = 0;
  while (1) {
    if (_M0L2__S2034 < _M0L7_2abindS2032) {
      struct _M0TUsiE* _M0L3argS2035 =
        (struct _M0TUsiE*)_M0L7_2abindS2033[_M0L2__S2034];
      moonbit_string_t _M0L6_2atmpS2066 = _M0L3argS2035->$0;
      int32_t _M0L6_2atmpS2067 = _M0L3argS2035->$1;
      int32_t _M0L6_2atmpS2068;
      moonbit_incref_cycle_free(_M0L6_2atmpS2066);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS2030, _M0L6_2atmpS2066, _M0L6_2atmpS2067);
      moonbit_decref_cycle_free(_M0L6_2atmpS2066);
      _M0L6_2atmpS2068 = _M0L2__S2034 + 1;
      _M0L2__S2034 = _M0L6_2atmpS2068;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS2033);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_compose_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples35stdp__compose__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS2030);
  moonbit_decref_cycle_free(_M0L12async__testsS2030);
  moonbit_flush_cycles();
  return 0;
}