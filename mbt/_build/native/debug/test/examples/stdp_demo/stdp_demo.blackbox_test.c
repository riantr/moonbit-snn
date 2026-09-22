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
struct _M0TP26RiantR8snn__mbt13STDPVariables;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TWRPC15error5ErrorEs;

struct _M0TWssbEu;

struct _M0DTPC15error5Error127RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0BTPB6Logger;

struct _M0TPB4IterGfE;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0TPB6Logger;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TPB5ArrayGUsiEE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1222;

struct _M0TUmmmmE;

struct _M0DTPC16option6OptionGfE4Some;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TWRPC15error5ErrorEu;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TPB9ArrayViewGfE;

struct _M0TPB8MutLocalGiE;

struct _M0TP26RiantR8snn__mbt12STDPGerstner;

struct _M0TP26RiantR8snn__mbt14SpikingSynapse;

struct _M0TWERPC16option6OptionGfE;

struct _M0TPB4Show;

struct _M0TPB8MutLocalGfE;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0R58ArrayView_3a_3aiter_7c_5bFloat_5d_7c_2eanon__u1760__l885__;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0TPB5ArrayGbE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0TPB8MutLocalGbE;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB5ArrayGsE;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1217;

struct _M0TWEu;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0TP26RiantR8snn__mbt13STDPVariables {
  struct _M0TPB5ArrayGfE* $0;
  struct _M0TPB5ArrayGfE* $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGbE* $4;
  
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

struct _M0DTPC15error5Error127RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
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

struct _M0TPB5ArrayGfE {
  float* $0;
  int32_t $1;
  
};

struct _M0TPB17FloatingDecimal64 {
  uint64_t $0;
  int32_t $1;
  
};

struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
};

struct _M0BTPB6Logger {
  int32_t(* $method_0)(void*, moonbit_string_t);
  int32_t(* $method_1)(void*, moonbit_string_t, int32_t, int32_t);
  int32_t(* $method_2)(void*, struct _M0TPC16string10StringView);
  int32_t(* $method_3)(void*, int32_t);
  int32_t(* $method_4)(void*, struct _M0TPB4Show);
  int32_t(* $method_5)(void*, struct _M0TPB4Show);
  
};

struct _M0TPB4IterGfE {
  struct _M0TWERPC16option6OptionGfE* $0;
  int64_t $1;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1222 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

struct _M0R58ArrayView_3a_3aiter_7c_5bFloat_5d_7c_2eanon__u1760__l885__ {
  void*(* code)(struct _M0TWERPC16option6OptionGfE*);
  struct _M0TPB9ArrayViewGfE $0;
  int32_t $1;
  struct _M0TPB8MutLocalGiE* $2;
  
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

struct _M0BTPB4Show {
  int32_t(* $method_0)(void*, struct _M0TPB6Logger);
  moonbit_string_t(* $method_1)(void*);
  
};

struct _M0TWuEu {
  int32_t(* code)(struct _M0TWuEu*, int32_t);
  
};

struct _M0TPB8MutLocalGbE {
  int32_t $0;
  
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

struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1217 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1229(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1222(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1217(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1194(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1187(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
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

int32_t _M0FP26RiantR8snn__mbt16forward__synapse(
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

int32_t _M0FP26RiantR8snn__mbt26stdp__kernel__plot_2einner(
  struct _M0TP26RiantR8snn__mbt12STDPGerstner*,
  float,
  int32_t,
  int32_t
);

moonbit_string_t _M0FP26RiantR8snn__mbt27format__axis__label__kernel(float);

float _M0FP26RiantR8snn__mbt16gerstner__kernel(
  float,
  float,
  float,
  float,
  float
);

struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0MP26RiantR8snn__mbt13STDPVariables3new(
  int32_t,
  int32_t
);

struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0MP26RiantR8snn__mbt12STDPGerstner3new(
  
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t
);

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t);

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t);

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

moonbit_string_t _M0FP26RiantR8snn__mbt19row__int__set__char(
  moonbit_string_t,
  int32_t,
  int32_t
);

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

float _M0MPC15float5Float5round(float);

float _M0MPC15float5Float5floor(float);

float _M0MPC15float5Float5trunc(float);

int32_t _M0MPC15float5Float7to__int(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

struct _M0TPB5ArrayGsE* _M0MPC15array5Array4makeGsE(
  int32_t,
  moonbit_string_t
);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGsE(
  struct _M0TPB5ArrayGsE*,
  int32_t,
  moonbit_string_t
);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4copyGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array12unsafe__blitGfE(
  struct _M0TPB5ArrayGfE*,
  int32_t,
  struct _M0TPB5ArrayGfE*,
  int32_t,
  int32_t
);

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

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

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

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(int32_t);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(int32_t);

struct _M0TPB5ArrayGsE* _M0MPC15array5Array20unsafe__make__uninitGsE(int32_t);

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

void* _M0MPC15array9ArrayView4iterGfEC1760l885(
  struct _M0TWERPC16option6OptionGfE*
);

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(uint64_t);

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t);

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t);

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

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE*);

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE*);

moonbit_string_t* _M0MPC15array5Array6bufferGsE(struct _M0TPB5ArrayGsE*);

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE*
);

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(moonbit_string_t);

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder*,
  struct _M0TPC16string10StringView
);

moonbit_string_t _M0MPC16string6String4make(int32_t, int32_t);

#define _M0FPB20unsafe__make__string moonbit_unsafe_make_string

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

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder*,
  uint64_t
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

int32_t _M0MPB18UninitializedArray6lengthGfE(float*);

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t*);

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t*);

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(struct _M0TUsiE**);

int32_t _M0IPB7FailurePB4Show6output(void*, struct _M0TPB6Logger);

int32_t _M0MPB6Logger13write__objectGfE(struct _M0TPB6Logger, float);

int32_t _M0MPB6Logger13write__objectGsE(
  struct _M0TPB6Logger,
  moonbit_string_t
);

int32_t _M0FPC15abort5abortGuE(moonbit_string_t);

moonbit_string_t _M0FPC15abort5abortGsE(moonbit_string_t);

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t);

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(moonbit_string_t);

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
} const moonbit_string_literal_34 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_32 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_26 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 44, 32, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_40 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_36 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_21 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[114]; 
} const moonbit_string_literal_46 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 113, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 115, 116, 100, 112, 95, 100, 101, 
    109, 111, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 
    116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 
    114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 
    115, 69, 114, 114, 111, 114, 46, 77, 111, 111, 110, 66, 105, 116, 
    84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 
    114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[12]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 11, 44, 34, 
    109, 101, 115, 115, 97, 103, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[53]; 
} const moonbit_string_literal_47 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_31 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_20 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[25]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 24, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 114, 101, 115, 117, 108, 116, 34, 
    44, 34, 102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_18 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_14 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 32, 8594, 
    32, 43, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_41 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_35 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_9 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_25 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 93, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_44 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_24 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 91, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_12 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 32, 124, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[15]; 
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 14, 105, 110, 
    118, 97, 108, 105, 100, 32, 108, 101, 110, 103, 116, 104, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_33 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[43]; 
} const moonbit_string_literal_16 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 42, 105, 110, 
    100, 101, 120, 32, 111, 117, 116, 32, 111, 102, 32, 98, 111, 117, 
    110, 100, 115, 58, 32, 116, 104, 101, 32, 108, 101, 110, 32, 105, 
    115, 32, 102, 114, 111, 109, 32, 48, 32, 116, 111, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_10 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 32, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_45 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 73, 110, 115, 112, 101, 
    99, 116, 69, 114, 114, 111, 114, 46, 73, 110, 115, 112, 101, 99, 
    116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_42 =
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
} const moonbit_string_literal_39 =
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
} const moonbit_string_literal_27 =
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
} const moonbit_string_literal_19 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_17 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 98, 
    117, 116, 32, 116, 104, 101, 32, 105, 110, 100, 101, 120, 32, 105, 
    115, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[36]; 
} const moonbit_string_literal_11 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 35, 83, 84, 
    68, 80, 32, 107, 101, 114, 110, 101, 108, 32, 40, 71, 101, 114, 115, 
    116, 110, 101, 114, 32, 49, 57, 57, 54, 41, 58, 32, 916, 87, 40, 
    916, 116, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_43 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_37 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_23 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_15 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 32, 109, 115, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[116]; 
} const moonbit_string_literal_48 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 115, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 115, 116, 100, 112, 95, 100, 101, 
    109, 111, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 
    116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 
    114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 
    107, 105, 112, 84, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 
    116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 
    101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[7]; 
} const moonbit_string_literal_13 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 6, 916, 116, 
    32, 61, 32, 45, 0
  };

struct moonbit_object const moonbit_constant_constructor_0 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0)
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1229$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1229
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[97] =
  {
    sizeof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1217)
    / 4, 1,
    offsetof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1217, $1)
    / 4
    * 2,
    sizeof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1222)
    / 4, 1,
    offsetof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1222, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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
    sizeof(struct _M0TP26RiantR8snn__mbt13STDPVariables) / 4, 5,
    offsetof(struct _M0TP26RiantR8snn__mbt13STDPVariables, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt13STDPVariables, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt13STDPVariables, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt13STDPVariables, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt13STDPVariables, $4) / 4 * 2,
    sizeof(struct _M0TPB9ArrayViewGfE) / 4, 1,
    offsetof(struct _M0TPB9ArrayViewGfE, $0) / 4 * 2,
    sizeof(struct _M0R58ArrayView_3a_3aiter_7c_5bFloat_5d_7c_2eanon__u1760__l885__)
    / 4, 2,
    (offsetof(struct _M0R58ArrayView_3a_3aiter_7c_5bFloat_5d_7c_2eanon__u1760__l885__, $0)
     + offsetof(struct _M0TPB9ArrayViewGfE, $0))
    / 4
    * 2,
    offsetof(struct _M0R58ArrayView_3a_3aiter_7c_5bFloat_5d_7c_2eanon__u1760__l885__, $2)
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int64_t _M0MPB4Iter4nextN6constrS10984GfE = 0ll;

int64_t _M0MPB4Iter4nextN6constrS10985GfE = 0ll;

int64_t _M0MPB4Iter3newN6constrS10992GfE = 0ll;

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2714
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1250,
  moonbit_string_t _M0L8filenameS1219,
  int32_t _M0L5indexS1221
) {
  struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1217* _closure_2743;
  struct _M0TWEu* _M0L13handle__startS1217;
  struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1222* _closure_2744;
  struct _M0TWssbEu* _M0L14handle__resultS1222;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1229;
  void* _M0L11_2atry__errS1244;
  struct moonbit_result_0 _tmp_2746;
  int32_t _handle__error__result_2747;
  int32_t _M0L6_2atmpS2702;
  void* _M0L3errS1245;
  moonbit_string_t _M0L4nameS1247;
  struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1248;
  moonbit_string_t _M0L7_2anameS1249;
  int32_t _M0L6_2acntS2737;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1219);
  _closure_2743
  = (struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1217*)moonbit_malloc(sizeof(struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1217));
  Moonbit_object_header(_closure_2743)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2743->code
  = &_M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1217;
  _closure_2743->$0 = _M0L5indexS1221;
  _closure_2743->$1 = _M0L8filenameS1219;
  _M0L13handle__startS1217 = (struct _M0TWEu*)_closure_2743;
  moonbit_incref_cycle_free(_M0L8filenameS1219);
  _closure_2744
  = (struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1222*)moonbit_malloc(sizeof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1222));
  Moonbit_object_header(_closure_2744)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2744->code
  = &_M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1222;
  _closure_2744->$0 = _M0L5indexS1221;
  _closure_2744->$1 = _M0L8filenameS1219;
  _M0L14handle__resultS1222 = (struct _M0TWssbEu*)_closure_2744;
  _M0L17error__to__stringS1229
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1229$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2746
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1250, _M0L8filenameS1219, _M0L5indexS1221, _M0L13handle__startS1217, _M0L14handle__resultS1222, _M0L17error__to__stringS1229);
  if (_tmp_2746.tag) {
    int32_t const _M0L5_2aokS2711 = _tmp_2746.data.ok;
    _handle__error__result_2747 = _M0L5_2aokS2711;
  } else {
    void* const _M0L6_2aerrS2712 = _tmp_2746.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1229);
    moonbit_decref_cycle_free(_M0L13handle__startS1217);
    _M0L11_2atry__errS1244 = _M0L6_2aerrS2712;
    goto join_1243;
  }
  if (_handle__error__result_2747) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1229);
    moonbit_decref_cycle_free(_M0L13handle__startS1217);
    _M0L6_2atmpS2702 = 1;
  } else {
    struct moonbit_result_0 _tmp_2748;
    int32_t _handle__error__result_2749;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2748
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1250, _M0L8filenameS1219, _M0L5indexS1221, _M0L13handle__startS1217, _M0L14handle__resultS1222, _M0L17error__to__stringS1229);
    if (_tmp_2748.tag) {
      int32_t const _M0L5_2aokS2709 = _tmp_2748.data.ok;
      _handle__error__result_2749 = _M0L5_2aokS2709;
    } else {
      void* const _M0L6_2aerrS2710 = _tmp_2748.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1229);
      moonbit_decref_cycle_free(_M0L13handle__startS1217);
      _M0L11_2atry__errS1244 = _M0L6_2aerrS2710;
      goto join_1243;
    }
    if (_handle__error__result_2749) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1229);
      moonbit_decref_cycle_free(_M0L13handle__startS1217);
      _M0L6_2atmpS2702 = 1;
    } else {
      struct moonbit_result_0 _tmp_2750;
      int32_t _handle__error__result_2751;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2750
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1250, _M0L8filenameS1219, _M0L5indexS1221, _M0L13handle__startS1217, _M0L14handle__resultS1222, _M0L17error__to__stringS1229);
      if (_tmp_2750.tag) {
        int32_t const _M0L5_2aokS2707 = _tmp_2750.data.ok;
        _handle__error__result_2751 = _M0L5_2aokS2707;
      } else {
        void* const _M0L6_2aerrS2708 = _tmp_2750.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1229);
        moonbit_decref_cycle_free(_M0L13handle__startS1217);
        _M0L11_2atry__errS1244 = _M0L6_2aerrS2708;
        goto join_1243;
      }
      if (_handle__error__result_2751) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1229);
        moonbit_decref_cycle_free(_M0L13handle__startS1217);
        _M0L6_2atmpS2702 = 1;
      } else {
        struct moonbit_result_0 _tmp_2752;
        int32_t _handle__error__result_2753;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2752
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1250, _M0L8filenameS1219, _M0L5indexS1221, _M0L13handle__startS1217, _M0L14handle__resultS1222, _M0L17error__to__stringS1229);
        if (_tmp_2752.tag) {
          int32_t const _M0L5_2aokS2705 = _tmp_2752.data.ok;
          _handle__error__result_2753 = _M0L5_2aokS2705;
        } else {
          void* const _M0L6_2aerrS2706 = _tmp_2752.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1229);
          moonbit_decref_cycle_free(_M0L13handle__startS1217);
          _M0L11_2atry__errS1244 = _M0L6_2aerrS2706;
          goto join_1243;
        }
        if (_handle__error__result_2753) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1229);
          moonbit_decref_cycle_free(_M0L13handle__startS1217);
          _M0L6_2atmpS2702 = 1;
        } else {
          struct moonbit_result_0 _tmp_2754;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2754
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1250, _M0L8filenameS1219, _M0L5indexS1221, _M0L13handle__startS1217, _M0L14handle__resultS1222, _M0L17error__to__stringS1229);
          moonbit_decref_cycle_free(_M0L13handle__startS1217);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1229);
          if (_tmp_2754.tag) {
            int32_t const _M0L5_2aokS2703 = _tmp_2754.data.ok;
            _M0L6_2atmpS2702 = _M0L5_2aokS2703;
          } else {
            void* const _M0L6_2aerrS2704 = _tmp_2754.data.err;
            _M0L11_2atry__errS1244 = _M0L6_2aerrS2704;
            goto join_1243;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2702) {
    void* _M0L129RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2713 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L129RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2713)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L129RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2713)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1244
    = _M0L129RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2713;
    goto join_1243;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1222);
  }
  goto joinlet_2745;
  join_1243:;
  _M0L3errS1245 = _M0L11_2atry__errS1244;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1248
  = (struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1245;
  _M0L7_2anameS1249 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1248->$0;
  _M0L6_2acntS2737
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1248));
  if (_M0L6_2acntS2737 > 1) {
    int32_t _M0L11_2anew__cntS2738 = _M0L6_2acntS2737 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1248), _M0L11_2anew__cntS2738);
    moonbit_incref_cycle_free(_M0L7_2anameS1249);
  } else if (_M0L6_2acntS2737 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1248);
  }
  _M0L4nameS1247 = _M0L7_2anameS1249;
  goto join_1246;
  goto joinlet_2755;
  join_1246:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1222(_M0L14handle__resultS1222, _M0L4nameS1247, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1222);
  moonbit_decref_cycle_free(_M0L4nameS1247);
  joinlet_2755:;
  joinlet_2745:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1229(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2701,
  void* _M0L3errS1230
) {
  void* _M0L1eS1232;
  moonbit_string_t _M0L1eS1234;
  moonbit_string_t _result_2758;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1230)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1235 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1230;
      moonbit_string_t _M0L4_2aeS1236 = _M0L10_2aFailureS1235->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1236);
      _M0L1eS1234 = _M0L4_2aeS1236;
      goto join_1233;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1237 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1230;
      moonbit_string_t _M0L4_2aeS1238 = _M0L15_2aInspectErrorS1237->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1238);
      _M0L1eS1234 = _M0L4_2aeS1238;
      goto join_1233;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1239 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1230;
      moonbit_string_t _M0L4_2aeS1240 = _M0L16_2aSnapshotErrorS1239->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1240);
      _M0L1eS1234 = _M0L4_2aeS1240;
      goto join_1233;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error127RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1241 =
        (struct _M0DTPC15error5Error127RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1230;
      moonbit_string_t _M0L4_2aeS1242 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1241->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1242);
      _M0L1eS1234 = _M0L4_2aeS1242;
      goto join_1233;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1230);
      _M0L1eS1232 = _M0L3errS1230;
      goto join_1231;
      break;
    }
  }
  join_1233:;
  return _M0L1eS1234;
  join_1231:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_2758 = _M0FP15Error10to__string(_M0L1eS1232);
  moonbit_decref_cycle_free(_M0L1eS1232);
  return _result_2758;
}

int32_t _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1222(
  struct _M0TWssbEu* _M0L6_2aenvS2698,
  moonbit_string_t _M0L10__testnameS1223,
  moonbit_string_t _M0L7messageS1224,
  int32_t _M0L7skippedS1225
) {
  struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1222* _M0L14_2acasted__envS2699;
  moonbit_string_t _M0L8filenameS1219;
  int32_t _M0L5indexS1221;
  moonbit_string_t _M0L10file__nameS1226;
  moonbit_string_t _M0L7messageS1227;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1228;
  moonbit_string_t _M0L6_2atmpS2700;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2699
  = (struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1222*)_M0L6_2aenvS2698;
  _M0L8filenameS1219 = _M0L14_2acasted__envS2699->$1;
  _M0L5indexS1221 = _M0L14_2acasted__envS2699->$0;
  if (!_M0L7skippedS1225 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1226
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1219, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1227
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1224, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1228
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1228, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1228, _M0L10file__nameS1226);
  moonbit_decref_cycle_free(_M0L10file__nameS1226);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1228, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1228, _M0L5indexS1221);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1228, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1228, _M0L7messageS1227);
  moonbit_decref_cycle_free(_M0L7messageS1227);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1228, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2700
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1228);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1228);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2700);
  moonbit_decref_cycle_free(_M0L6_2atmpS2700);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1217(
  struct _M0TWEu* _M0L6_2aenvS2695
) {
  struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1217* _M0L14_2acasted__envS2696;
  moonbit_string_t _M0L8filenameS1219;
  int32_t _M0L5indexS1221;
  moonbit_string_t _M0L10file__nameS1218;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1220;
  moonbit_string_t _M0L6_2atmpS2697;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2696
  = (struct _M0R130_24RiantR_2fsnn__mbt_2fexamples_2fstdp__demo__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1217*)_M0L6_2aenvS2695;
  _M0L8filenameS1219 = _M0L14_2acasted__envS2696->$1;
  _M0L5indexS1221 = _M0L14_2acasted__envS2696->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1218
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1219, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1220
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1220, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1220, _M0L10file__nameS1218);
  moonbit_decref_cycle_free(_M0L10file__nameS1218);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1220, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1220, _M0L5indexS1221);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1220, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2697
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1220);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1220);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2697);
  moonbit_decref_cycle_free(_M0L6_2atmpS2697);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1187;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1194;
  struct _M0TUsiE** _M0L6_2atmpS2694;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1201;
  moonbit_string_t* _M0L9cli__argsS1202;
  moonbit_string_t _M0L6_2atmpS2693;
  moonbit_string_t _M0L6_2atmpS2692;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1203;
  int32_t _M0L7_2abindS1204;
  moonbit_string_t* _M0L7_2abindS1205;
  int32_t _M0L6_2acntS2739;
  int32_t _M0L2__S1206;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1187 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1194 = 0;
  _M0L6_2atmpS2694 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1201
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1201)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1201->$0 = _M0L6_2atmpS2694;
  _M0L16file__and__indexS1201->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1202
  = _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1202)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2693 = (moonbit_string_t)_M0L9cli__argsS1202[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2693);
  moonbit_decref_cycle_free(_M0L9cli__argsS1202);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2692
  = _M0MP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2693);
  moonbit_decref_cycle_free(_M0L6_2atmpS2693);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1203
  = _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1194(_M0L51moonbit__test__driver__internal__split__mbt__stringS1194, _M0L6_2atmpS2692, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2692);
  _M0L7_2abindS1204 = _M0L10test__argsS1203->$1;
  _M0L7_2abindS1205 = _M0L10test__argsS1203->$0;
  _M0L6_2acntS2739
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1203));
  if (_M0L6_2acntS2739 > 1) {
    int32_t _M0L11_2anew__cntS2740 = _M0L6_2acntS2739 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1203), _M0L11_2anew__cntS2740);
    moonbit_incref_cycle_free(_M0L7_2abindS1205);
  } else if (_M0L6_2acntS2739 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1203);
  }
  _M0L2__S1206 = 0;
  while (1) {
    if (_M0L2__S1206 < _M0L7_2abindS1204) {
      moonbit_string_t _M0L3argS1207 =
        (moonbit_string_t)_M0L7_2abindS1205[_M0L2__S1206];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1208;
      moonbit_string_t _M0L4fileS1209;
      moonbit_string_t _M0L5rangeS1210;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1211;
      moonbit_string_t _M0L6_2atmpS2690;
      int32_t _M0L5startS1212;
      moonbit_string_t _M0L6_2atmpS2689;
      int32_t _M0L3endS1213;
      int32_t _M0L1iS1214;
      int32_t _M0L6_2atmpS2691;
      moonbit_incref_cycle_free(_M0L3argS1207);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1208
      = _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1194(_M0L51moonbit__test__driver__internal__split__mbt__stringS1194, _M0L3argS1207, 58);
      moonbit_decref_cycle_free(_M0L3argS1207);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1209
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1208, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1210
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1208, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1208);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1211
      = _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1194(_M0L51moonbit__test__driver__internal__split__mbt__stringS1194, _M0L5rangeS1210, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1210);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2690
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1211, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1212
      = _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1187(_M0L45moonbit__test__driver__internal__parse__int__S1187, _M0L6_2atmpS2690);
      moonbit_decref_cycle_free(_M0L6_2atmpS2690);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2689
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1211, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1211);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1213
      = _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1187(_M0L45moonbit__test__driver__internal__parse__int__S1187, _M0L6_2atmpS2689);
      moonbit_decref_cycle_free(_M0L6_2atmpS2689);
      _M0L1iS1214 = _M0L5startS1212;
      while (1) {
        if (_M0L1iS1214 < _M0L3endS1213) {
          struct _M0TUsiE* _M0L8_2atupleS2687;
          int32_t _M0L6_2atmpS2688;
          moonbit_incref_cycle_free(_M0L4fileS1209);
          _M0L8_2atupleS2687
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS2687)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS2687->$0 = _M0L4fileS1209;
          _M0L8_2atupleS2687->$1 = _M0L1iS1214;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1201, _M0L8_2atupleS2687);
          _M0L6_2atmpS2688 = _M0L1iS1214 + 1;
          _M0L1iS1214 = _M0L6_2atmpS2688;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1209);
        }
        break;
      }
      _M0L6_2atmpS2691 = _M0L2__S1206 + 1;
      _M0L2__S1206 = _M0L6_2atmpS2691;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1205);
    }
    break;
  }
  return _M0L16file__and__indexS1201;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1194(
  int32_t _M0L6_2aenvS2668,
  moonbit_string_t _M0L1sS1195,
  int32_t _M0L3sepS1196
) {
  moonbit_string_t* _M0L6_2atmpS2686;
  struct _M0TPB5ArrayGsE* _M0L3resS1197;
  struct _M0TPB8MutLocalGiE* _M0L1iS1198;
  struct _M0TPB8MutLocalGiE* _M0L5startS1199;
  int32_t _M0L3valS2681;
  int32_t _M0L6_2atmpS2682;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2686 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1197
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1197)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1197->$0 = _M0L6_2atmpS2686;
  _M0L3resS1197->$1 = 0;
  _M0L1iS1198
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1198)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1198->$0 = 0;
  _M0L5startS1199
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1199)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1199->$0 = 0;
  while (1) {
    int32_t _M0L3valS2669 = _M0L1iS1198->$0;
    int32_t _M0L6_2atmpS2670 = Moonbit_array_length(_M0L1sS1195);
    if (_M0L3valS2669 < _M0L6_2atmpS2670) {
      int32_t _M0L3valS2673 = _M0L1iS1198->$0;
      int32_t _M0L6_2atmpS2672;
      int32_t _M0L6_2atmpS2671;
      int32_t _M0L3valS2680;
      int32_t _M0L6_2atmpS2679;
      if (
        _M0L3valS2673 < 0
        || _M0L3valS2673 >= Moonbit_array_length(_M0L1sS1195)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2672 = _M0L1sS1195[_M0L3valS2673];
      _M0L6_2atmpS2671 = _M0L6_2atmpS2672;
      if (_M0L6_2atmpS2671 == _M0L3sepS1196) {
        int32_t _M0L3valS2675 = _M0L5startS1199->$0;
        int32_t _M0L3valS2676 = _M0L1iS1198->$0;
        moonbit_string_t _M0L6_2atmpS2674;
        int32_t _M0L3valS2678;
        int32_t _M0L6_2atmpS2677;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS2674
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1195, _M0L3valS2675, _M0L3valS2676);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1197, _M0L6_2atmpS2674);
        _M0L3valS2678 = _M0L1iS1198->$0;
        _M0L6_2atmpS2677 = _M0L3valS2678 + 1;
        _M0L5startS1199->$0 = _M0L6_2atmpS2677;
      }
      _M0L3valS2680 = _M0L1iS1198->$0;
      _M0L6_2atmpS2679 = _M0L3valS2680 + 1;
      _M0L1iS1198->$0 = _M0L6_2atmpS2679;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1198);
    }
    break;
  }
  _M0L3valS2681 = _M0L5startS1199->$0;
  _M0L6_2atmpS2682 = Moonbit_array_length(_M0L1sS1195);
  if (_M0L3valS2681 < _M0L6_2atmpS2682) {
    int32_t _M0L3valS2684 = _M0L5startS1199->$0;
    int32_t _M0L6_2atmpS2685;
    moonbit_string_t _M0L6_2atmpS2683;
    moonbit_decref_cycle_free(_M0L5startS1199);
    _M0L6_2atmpS2685 = Moonbit_array_length(_M0L1sS1195);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS2683
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1195, _M0L3valS2684, _M0L6_2atmpS2685);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1197, _M0L6_2atmpS2683);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1199);
  }
  return _M0L3resS1197;
}

int32_t _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1187(
  int32_t _M0L6_2aenvS2661,
  moonbit_string_t _M0L1sS1188
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1189;
  int32_t _M0L3lenS1190;
  int32_t _M0L7_2abindS1191;
  int32_t _M0L1iS1192;
  int32_t _result_2763;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1189
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1189)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1189->$0 = 0;
  _M0L3lenS1190 = Moonbit_array_length(_M0L1sS1188);
  _M0L7_2abindS1191 = 0;
  _M0L1iS1192 = _M0L7_2abindS1191;
  while (1) {
    if (_M0L1iS1192 < _M0L3lenS1190) {
      int32_t _M0L3valS2666 = _M0L3resS1189->$0;
      int32_t _M0L6_2atmpS2663 = _M0L3valS2666 * 10;
      int32_t _M0L6_2atmpS2665;
      int32_t _M0L6_2atmpS2664;
      int32_t _M0L6_2atmpS2662;
      int32_t _M0L6_2atmpS2667;
      if (
        _M0L1iS1192 < 0 || _M0L1iS1192 >= Moonbit_array_length(_M0L1sS1188)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2665 = _M0L1sS1188[_M0L1iS1192];
      _M0L6_2atmpS2664 = _M0L6_2atmpS2665 - 48;
      _M0L6_2atmpS2662 = _M0L6_2atmpS2663 + _M0L6_2atmpS2664;
      _M0L3resS1189->$0 = _M0L6_2atmpS2662;
      _M0L6_2atmpS2667 = _M0L1iS1192 + 1;
      _M0L1iS1192 = _M0L6_2atmpS2667;
      continue;
    }
    break;
  }
  _result_2763 = _M0L3resS1189->$0;
  moonbit_decref_cycle_free(_M0L3resS1189);
  return _result_2763;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1186
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1186);
  return _M0L4selfS1186;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1156,
  moonbit_string_t _M0L12_2adiscard__S1157,
  int32_t _M0L12_2adiscard__S1158,
  struct _M0TWEu* _M0L12_2adiscard__S1159,
  struct _M0TWssbEu* _M0L12_2adiscard__S1160,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1161
) {
  struct moonbit_result_0 _result_2764;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_2764.tag = 1;
  _result_2764.data.ok = 0;
  return _result_2764;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1162,
  moonbit_string_t _M0L12_2adiscard__S1163,
  int32_t _M0L12_2adiscard__S1164,
  struct _M0TWEu* _M0L12_2adiscard__S1165,
  struct _M0TWssbEu* _M0L12_2adiscard__S1166,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1167
) {
  struct moonbit_result_0 _result_2765;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_2765.tag = 1;
  _result_2765.data.ok = 0;
  return _result_2765;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1168,
  moonbit_string_t _M0L12_2adiscard__S1169,
  int32_t _M0L12_2adiscard__S1170,
  struct _M0TWEu* _M0L12_2adiscard__S1171,
  struct _M0TWssbEu* _M0L12_2adiscard__S1172,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1173
) {
  struct moonbit_result_0 _result_2766;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_2766.tag = 1;
  _result_2766.data.ok = 0;
  return _result_2766;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1174,
  moonbit_string_t _M0L12_2adiscard__S1175,
  int32_t _M0L12_2adiscard__S1176,
  struct _M0TWEu* _M0L12_2adiscard__S1177,
  struct _M0TWssbEu* _M0L12_2adiscard__S1178,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1179
) {
  struct moonbit_result_0 _result_2767;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_2767.tag = 1;
  _result_2767.data.ok = 0;
  return _result_2767;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1180,
  moonbit_string_t _M0L12_2adiscard__S1181,
  int32_t _M0L12_2adiscard__S1182,
  struct _M0TWEu* _M0L12_2adiscard__S1183,
  struct _M0TWssbEu* _M0L12_2adiscard__S1184,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1185
) {
  struct moonbit_result_0 _result_2768;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _result_2768.tag = 1;
  _result_2768.data.ok = 0;
  return _result_2768;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1155
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16spiking__connect(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1127,
  int32_t _M0L3preS1124,
  int32_t _M0L4postS1126,
  float _M0L1wS1128
) {
  int32_t _M0L8pre__idxS1123;
  int32_t _M0L9post__idxS1125;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2660;
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L8pre__idxS1123 = _M0L3preS1124 - 1;
  _M0L9post__idxS1125 = _M0L4postS1126 - 1;
  _M0L6matrixS2660 = _M0L1cS1127->$4;
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0MP26RiantR8snn__mbt15SparseMatrixCSR3set(_M0L6matrixS2660, _M0L8pre__idxS1123, _M0L9post__idxS1125, _M0L1wS1128);
  return 0;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse3new(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1120,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS1121,
  moonbit_string_t _M0L3symS1122
) {
  int32_t _M0L1nS2658;
  int32_t _M0L1nS2659;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS1119;
  float* _M0L6_2atmpS2657;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2648;
  float* _M0L6_2atmpS2656;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2649;
  float* _M0L6_2atmpS2655;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2650;
  int32_t* _M0L6_2atmpS2654;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2651;
  float* _M0L6_2atmpS2653;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2652;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _block_2769;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS2658 = _M0L3preS1120->$2;
  _M0L1nS2659 = _M0L4postS1121->$2;
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6matrixS1119
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR5empty(_M0L1nS2658, _M0L1nS2659);
  _M0L6_2atmpS2657 = moonbit_empty_float_array;
  _M0L6_2atmpS2648
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2648)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2648->$0 = _M0L6_2atmpS2657;
  _M0L6_2atmpS2648->$1 = 0;
  _M0L6_2atmpS2656 = moonbit_empty_float_array;
  _M0L6_2atmpS2649
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2649)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2649->$0 = _M0L6_2atmpS2656;
  _M0L6_2atmpS2649->$1 = 0;
  _M0L6_2atmpS2655 = moonbit_empty_float_array;
  _M0L6_2atmpS2650
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2650)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2650->$0 = _M0L6_2atmpS2655;
  _M0L6_2atmpS2650->$1 = 0;
  _M0L6_2atmpS2654 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS2651
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2651)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS2651->$0 = _M0L6_2atmpS2654;
  _M0L6_2atmpS2651->$1 = 0;
  _M0L6_2atmpS2653 = moonbit_empty_float_array;
  _M0L6_2atmpS2652
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2652)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2652->$0 = _M0L6_2atmpS2653;
  _M0L6_2atmpS2652->$1 = 0;
  moonbit_incref_cycle_free(_M0L3preS1120);
  moonbit_incref_cycle_free(_M0L4postS1121);
  moonbit_incref_cycle_free(_M0L3symS1122);
  _block_2769
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse));
  Moonbit_object_header(_block_2769)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _block_2769->$0 = _M0L3preS1120;
  _block_2769->$1 = _M0L4postS1121;
  _block_2769->$2 = _M0L3symS1122;
  _block_2769->$3 = (moonbit_string_t)moonbit_string_literal_0.data;
  _block_2769->$4 = _M0L6matrixS1119;
  _block_2769->$5 = _M0L6_2atmpS2648;
  _block_2769->$6 = _M0L6_2atmpS2649;
  _block_2769->$7 = _M0L6_2atmpS2650;
  _block_2769->$8 = _M0L6_2atmpS2651;
  _block_2769->$9 = _M0L6_2atmpS2652;
  return _block_2769;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter3new(
  
) {
  float _M0L1cS1117;
  float _M0L2glS1118;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_2770;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS1117 = -0x1p+0f;
  _M0L2glS1118 = -0x1p+0f;
  _block_2770
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_2770)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2770->$0 = _M0L1cS1117;
  _block_2770->$1 = _M0L2glS1118;
  _block_2770->$2 = 0x1.ep+3f;
  _block_2770->$3 = -0x1.9p+5f;
  _block_2770->$4 = -0x1.ep+5f;
  _block_2770->$5 = -0x1.18p+6f;
  _block_2770->$6 = 0x1.eb851eb851eb8p-5f;
  _block_2770->$7 = 0x1p+1f;
  _block_2770->$8 = 0x0p+0f;
  _block_2770->$9 = 0x0p+0f;
  _block_2770->$10 = 0x0p+0f;
  return _block_2770;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS1091,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS1093,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1096
) {
  struct _M0TPB5ArrayGfE* _M0L1vS1090;
  float _M0L2vtS2646;
  float _M0L2vrS2647;
  float _M0L6spreadS1092;
  int32_t _M0L7_2abindS1094;
  int32_t _M0L1kS1095;
  struct _M0TPB5ArrayGfE* _M0L1wS1098;
  struct _M0TPB5ArrayGbE* _M0L4fireS1099;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1100;
  struct _M0TPB5ArrayGfE* _M0L1iS1101;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS1102;
  struct _M0TPB5ArrayGfE* _M0L2geS1103;
  struct _M0TPB5ArrayGfE* _M0L2giS1104;
  struct _M0TPB5ArrayGfE* _M0L2heS1105;
  struct _M0TPB5ArrayGfE* _M0L2hiS1106;
  struct _M0TPB5ArrayGfE* _M0L3gluS1107;
  struct _M0TPB5ArrayGfE* _M0L4gabaS1108;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1109;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1110;
  float _M0L4e__eS1111;
  float _M0L4e__iS1112;
  float _M0L3treS1113;
  float _M0L3tdeS1114;
  float _M0L3triS1115;
  float _M0L3tdiS1116;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS2645;
  struct _M0TP26RiantR8snn__mbt2IF* _block_2772;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS1090 = _M0MPC15array5Array4makeGfE(_M0L1nS1091, 0x0p+0f);
  _M0L2vtS2646 = _M0L5paramS1093->$3;
  _M0L2vrS2647 = _M0L5paramS1093->$4;
  _M0L6spreadS1092 = _M0L2vtS2646 - _M0L2vrS2647;
  _M0L7_2abindS1094 = 0;
  _M0L1kS1095 = _M0L7_2abindS1094;
  while (1) {
    if (_M0L1kS1095 < _M0L1nS1091) {
      float _M0L2vrS2641 = _M0L5paramS1093->$4;
      float _M0L6_2atmpS2643;
      float _M0L6_2atmpS2642;
      float _M0L6_2atmpS2640;
      int32_t _M0L6_2atmpS2644;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2643 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1096);
      _M0L6_2atmpS2642 = _M0L6_2atmpS2643 * _M0L6spreadS1092;
      _M0L6_2atmpS2640 = _M0L2vrS2641 + _M0L6_2atmpS2642;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1090, _M0L1kS1095, _M0L6_2atmpS2640);
      _M0L6_2atmpS2644 = _M0L1kS1095 + 1;
      _M0L1kS1095 = _M0L6_2atmpS2644;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS1098 = _M0MPC15array5Array4makeGfE(_M0L1nS1091, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS1099 = _M0MPC15array5Array4makeGbE(_M0L1nS1091, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS1100 = _M0MPC15array5Array4makeGiE(_M0L1nS1091, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS1101 = _M0MPC15array5Array4makeGfE(_M0L1nS1091, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS1102 = _M0MPC15array5Array4makeGfE(_M0L1nS1091, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS1103 = _M0MPC15array5Array4makeGfE(_M0L1nS1091, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS1104 = _M0MPC15array5Array4makeGfE(_M0L1nS1091, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS1105 = _M0MPC15array5Array4makeGfE(_M0L1nS1091, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS1106 = _M0MPC15array5Array4makeGfE(_M0L1nS1091, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS1107 = _M0MPC15array5Array4makeGfE(_M0L1nS1091, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS1108 = _M0MPC15array5Array4makeGfE(_M0L1nS1091, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS1109 = _M0MPC15array5Array4makeGfE(_M0L1nS1091, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS1110 = _M0MPC15array5Array4makeGfE(_M0L1nS1091, 0x1p+0f);
  _M0L4e__eS1111 = 0x0p+0f;
  _M0L4e__iS1112 = -0x1.2cp+6f;
  _M0L3treS1113 = 0x1p+0f;
  _M0L3tdeS1114 = 0x1.8p+2f;
  _M0L3triS1115 = 0x1p-1f;
  _M0L3tdiS1116 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS2645 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS1093);
  _block_2772
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_2772)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _block_2772->$0 = _M0L5paramS1093;
  _block_2772->$1 = _M0L6_2atmpS2645;
  _block_2772->$2 = _M0L1nS1091;
  _block_2772->$3 = _M0L1vS1090;
  _block_2772->$4 = _M0L1wS1098;
  _block_2772->$5 = _M0L4fireS1099;
  _block_2772->$6 = _M0L4tabsS1100;
  _block_2772->$7 = _M0L1iS1101;
  _block_2772->$8 = _M0L9syn__currS1102;
  _block_2772->$9 = _M0L2geS1103;
  _block_2772->$10 = _M0L2giS1104;
  _block_2772->$11 = _M0L2heS1105;
  _block_2772->$12 = _M0L2hiS1106;
  _block_2772->$13 = _M0L3gluS1107;
  _block_2772->$14 = _M0L4gabaS1108;
  _block_2772->$15 = _M0L7gsyn__eS1109;
  _block_2772->$16 = _M0L7gsyn__iS1110;
  _block_2772->$17 = _M0L4e__eS1111;
  _block_2772->$18 = _M0L4e__iS1112;
  _block_2772->$19 = _M0L3treS1113;
  _block_2772->$20 = _M0L3tdeS1114;
  _block_2772->$21 = _M0L3triS1115;
  _block_2772->$22 = _M0L3tdiS1116;
  return _block_2772;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_2773;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_2773
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_2773)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2773->$0 = 0x1p+1f;
  return _block_2773;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0FP26RiantR8snn__mbt16forward__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1066,
  float _M0L6t__nowS1077
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS2639;
  int32_t _M0L6_2atmpS2638;
  int32_t _M0L10use__delayS1065;
  struct _M0TPB5ArrayGfE* _M0L3rhoS2637;
  int32_t _M0L6_2atmpS2636;
  int32_t _M0L8use__rhoS1067;
  #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6delaysS2639 = _M0L1cS1066->$5;
  #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS2638 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS2639);
  _M0L10use__delayS1065 = _M0L6_2atmpS2638 > 0;
  _M0L3rhoS2637 = _M0L1cS1066->$6;
  #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS2636 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS2637);
  _M0L8use__rhoS1067 = _M0L6_2atmpS2636 > 0;
  if (_M0L10use__delayS1065) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2599 = _M0L1cS1066->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS2598 = _M0L3preS2599->$5;
    int32_t _M0L6n__preS1068;
    struct _M0TPB8MutLocalGiE* _M0L1jS1069;
    #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6n__preS1068 = _M0MPC15array5Array6lengthGbE(_M0L4fireS2598);
    _M0L1jS1069
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS1069)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS1069->$0 = 0;
    while (1) {
      int32_t _M0L3valS2567 = _M0L1jS1069->$0;
      if (_M0L3valS2567 < _M0L6n__preS1068) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2570 = _M0L1cS1066->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS2568 = _M0L3preS2570->$5;
        int32_t _M0L3valS2569 = _M0L1jS1069->$0;
        int32_t _M0L3valS2597;
        int32_t _M0L6_2atmpS2596;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS2568, _M0L3valS2569)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2595 =
            _M0L1cS1066->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS2593 = _M0L6matrixS2595->$2;
          int32_t _M0L3valS2594 = _M0L1jS1069->$0;
          int32_t _M0L5startS1070;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2592;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS2589;
          int32_t _M0L3valS2591;
          int32_t _M0L6_2atmpS2590;
          int32_t _M0L3endS1071;
          struct _M0TPB8MutLocalGiE* _M0L1sS1072;
          #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L5startS1070
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS2593, _M0L3valS2594);
          _M0L6matrixS2592 = _M0L1cS1066->$4;
          _M0L6rowptrS2589 = _M0L6matrixS2592->$2;
          _M0L3valS2591 = _M0L1jS1069->$0;
          _M0L6_2atmpS2590 = _M0L3valS2591 + 1;
          #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L3endS1071
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS2589, _M0L6_2atmpS2590);
          _M0L1sS1072
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS1072)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS1072->$0 = _M0L5startS1070;
          while (1) {
            int32_t _M0L3valS2571 = _M0L1sS1072->$0;
            if (_M0L3valS2571 < _M0L3endS1071) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2588 =
                _M0L1cS1066->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS2586 = _M0L6matrixS2588->$3;
              int32_t _M0L3valS2587 = _M0L1sS1072->$0;
              int32_t _M0L9post__idxS1073;
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2585;
              struct _M0TPB5ArrayGfE* _M0L4valsS2583;
              int32_t _M0L3valS2584;
              float _M0L1wS1074;
              struct _M0TPB5ArrayGfE* _M0L6delaysS2581;
              int32_t _M0L3valS2582;
              float _M0L1dS1075;
              float _M0L9w__scaledS1076;
              int32_t _M0L3valS2577;
              int32_t _M0L6_2atmpS2576;
              #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L9post__idxS1073
              = _M0MPC15array5Array2atGiE(_M0L6colptrS2586, _M0L3valS2587);
              _M0L6matrixS2585 = _M0L1cS1066->$4;
              _M0L4valsS2583 = _M0L6matrixS2585->$4;
              _M0L3valS2584 = _M0L1sS1072->$0;
              #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1wS1074
              = _M0MPC15array5Array2atGfE(_M0L4valsS2583, _M0L3valS2584);
              _M0L6delaysS2581 = _M0L1cS1066->$5;
              _M0L3valS2582 = _M0L1sS1072->$0;
              #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1dS1075
              = _M0MPC15array5Array2atGfE(_M0L6delaysS2581, _M0L3valS2582);
              if (_M0L8use__rhoS1067) {
                struct _M0TPB5ArrayGfE* _M0L3rhoS2579 = _M0L1cS1066->$6;
                int32_t _M0L3valS2580 = _M0L1sS1072->$0;
                float _M0L6_2atmpS2578;
                #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS2578
                = _M0MPC15array5Array2atGfE(_M0L3rhoS2579, _M0L3valS2580);
                _M0L9w__scaledS1076 = _M0L1wS1074 * _M0L6_2atmpS2578;
              } else {
                _M0L9w__scaledS1076 = _M0L1wS1074;
              }
              if (_M0L1dS1075 == 0x0p+0f) {
                #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS1066, _M0L9post__idxS1073, _M0L9w__scaledS1076);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS2572 =
                  _M0L1cS1066->$7;
                float _M0L6_2atmpS2573 = _M0L6t__nowS1077 + _M0L1dS1075;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS2574;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS2575;
                #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS2572, _M0L6_2atmpS2573);
                _M0L14pending__postsS2574 = _M0L1cS1066->$8;
                #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS2574, _M0L9post__idxS1073);
                _M0L16pending__weightsS2575 = _M0L1cS1066->$9;
                #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS2575, _M0L9w__scaledS1076);
              }
              _M0L3valS2577 = _M0L1sS1072->$0;
              _M0L6_2atmpS2576 = _M0L3valS2577 + 1;
              _M0L1sS1072->$0 = _M0L6_2atmpS2576;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1sS1072);
            }
            break;
          }
        }
        _M0L3valS2597 = _M0L1jS1069->$0;
        _M0L6_2atmpS2596 = _M0L3valS2597 + 1;
        _M0L1jS1069->$0 = _M0L6_2atmpS2596;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1jS1069);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS2633 = _M0L1cS1066->$2;
    struct _M0TPB5ArrayGfE* _M0L6targetS1080;
    #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    if (
      _M0L3symS2633 == (moonbit_string_t)moonbit_string_literal_9.data
      || Moonbit_array_length(_M0L3symS2633)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
         && 0
            == memcmp(_M0L3symS2633, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS2633) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2634 = _M0L1cS1066->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2715 = _M0L4postS2634->$13;
      moonbit_incref_cycle_free(_M0L8_2afieldS2715);
      _M0L6targetS1080 = _M0L8_2afieldS2715;
    } else {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2635 = _M0L1cS1066->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS2716 = _M0L4postS2635->$14;
      moonbit_incref_cycle_free(_M0L8_2afieldS2716);
      _M0L6targetS1080 = _M0L8_2afieldS2716;
    }
    if (_M0L8use__rhoS1067) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2629 = _M0L1cS1066->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2628 = _M0L3preS2629->$5;
      int32_t _M0L6n__preS1081;
      struct _M0TPB8MutLocalGiE* _M0L1jS1082;
      #line 283 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6n__preS1081 = _M0MPC15array5Array6lengthGbE(_M0L4fireS2628);
      _M0L1jS1082
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS1082)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS1082->$0 = 0;
      while (1) {
        int32_t _M0L3valS2600 = _M0L1jS1082->$0;
        if (_M0L3valS2600 < _M0L6n__preS1081) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2603 = _M0L1cS1066->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS2601 = _M0L3preS2603->$5;
          int32_t _M0L3valS2602 = _M0L1jS1082->$0;
          int32_t _M0L3valS2627;
          int32_t _M0L6_2atmpS2626;
          #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS2601, _M0L3valS2602)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2625 =
              _M0L1cS1066->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS2623 = _M0L6matrixS2625->$2;
            int32_t _M0L3valS2624 = _M0L1jS1082->$0;
            int32_t _M0L5startS1083;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2622;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS2619;
            int32_t _M0L3valS2621;
            int32_t _M0L6_2atmpS2620;
            int32_t _M0L3endS1084;
            struct _M0TPB8MutLocalGiE* _M0L1sS1085;
            #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L5startS1083
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS2623, _M0L3valS2624);
            _M0L6matrixS2622 = _M0L1cS1066->$4;
            _M0L6rowptrS2619 = _M0L6matrixS2622->$2;
            _M0L3valS2621 = _M0L1jS1082->$0;
            _M0L6_2atmpS2620 = _M0L3valS2621 + 1;
            #line 288 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L3endS1084
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS2619, _M0L6_2atmpS2620);
            _M0L1sS1085
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS1085)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS1085->$0 = _M0L5startS1083;
            while (1) {
              int32_t _M0L3valS2604 = _M0L1sS1085->$0;
              if (_M0L3valS2604 < _M0L3endS1084) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2618 =
                  _M0L1cS1066->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS2616 =
                  _M0L6matrixS2618->$3;
                int32_t _M0L3valS2617 = _M0L1sS1085->$0;
                int32_t _M0L9post__idxS1086;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2615;
                struct _M0TPB5ArrayGfE* _M0L4valsS2613;
                int32_t _M0L3valS2614;
                float _M0L6_2atmpS2609;
                struct _M0TPB5ArrayGfE* _M0L3rhoS2611;
                int32_t _M0L3valS2612;
                float _M0L6_2atmpS2610;
                float _M0L9w__scaledS1087;
                float _M0L6_2atmpS2606;
                float _M0L6_2atmpS2605;
                int32_t _M0L3valS2608;
                int32_t _M0L6_2atmpS2607;
                #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L9post__idxS1086
                = _M0MPC15array5Array2atGiE(_M0L6colptrS2616, _M0L3valS2617);
                _M0L6matrixS2615 = _M0L1cS1066->$4;
                _M0L4valsS2613 = _M0L6matrixS2615->$4;
                _M0L3valS2614 = _M0L1sS1085->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS2609
                = _M0MPC15array5Array2atGfE(_M0L4valsS2613, _M0L3valS2614);
                _M0L3rhoS2611 = _M0L1cS1066->$6;
                _M0L3valS2612 = _M0L1sS1085->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS2610
                = _M0MPC15array5Array2atGfE(_M0L3rhoS2611, _M0L3valS2612);
                _M0L9w__scaledS1087 = _M0L6_2atmpS2609 * _M0L6_2atmpS2610;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS2606
                = _M0MPC15array5Array2atGfE(_M0L6targetS1080, _M0L9post__idxS1086);
                _M0L6_2atmpS2605 = _M0L6_2atmpS2606 + _M0L9w__scaledS1087;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array3setGfE(_M0L6targetS1080, _M0L9post__idxS1086, _M0L6_2atmpS2605);
                _M0L3valS2608 = _M0L1sS1085->$0;
                _M0L6_2atmpS2607 = _M0L3valS2608 + 1;
                _M0L1sS1085->$0 = _M0L6_2atmpS2607;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS1085);
              }
              break;
            }
          }
          _M0L3valS2627 = _M0L1jS1082->$0;
          _M0L6_2atmpS2626 = _M0L3valS2627 + 1;
          _M0L1jS1082->$0 = _M0L6_2atmpS2626;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1jS1082);
          moonbit_decref_cycle_free(_M0L6targetS1080);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2630 =
        _M0L1cS1066->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2632 = _M0L1cS1066->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2631 = _M0L3preS2632->$5;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS2630, _M0L4fireS2631, _M0L6targetS1080);
      moonbit_decref_cycle_free(_M0L6targetS1080);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13apply__weight(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1062,
  int32_t _M0L9post__idxS1063,
  float _M0L1wS1064
) {
  moonbit_string_t _M0L3symS2554;
  #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L3symS2554 = _M0L1cS1062->$2;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  if (
    _M0L3symS2554 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS2554)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS2554, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS2554) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2560 = _M0L1cS1062->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS2555 = _M0L4postS2560->$13;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2559 = _M0L1cS1062->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS2558 = _M0L4postS2559->$13;
    float _M0L6_2atmpS2557;
    float _M0L6_2atmpS2556;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS2557
    = _M0MPC15array5Array2atGfE(_M0L3gluS2558, _M0L9post__idxS1063);
    _M0L6_2atmpS2556 = _M0L6_2atmpS2557 + _M0L1wS1064;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L3gluS2555, _M0L9post__idxS1063, _M0L6_2atmpS2556);
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2566 = _M0L1cS1062->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS2561 = _M0L4postS2566->$14;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS2565 = _M0L1cS1062->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS2564 = _M0L4postS2565->$14;
    float _M0L6_2atmpS2563;
    float _M0L6_2atmpS2562;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS2563
    = _M0MPC15array5Array2atGfE(_M0L4gabaS2564, _M0L9post__idxS1063);
    _M0L6_2atmpS2562 = _M0L6_2atmpS2563 + _M0L1wS1064;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L4gabaS2561, _M0L9post__idxS1063, _M0L6_2atmpS2562);
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1058
) {
  int32_t _M0L1nS1057;
  int32_t _M0L7_2abindS1059;
  int32_t _M0L1iS1060;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1057 = _M0L1pS1058->$2;
  _M0L7_2abindS1059 = 0;
  _M0L1iS1060 = _M0L7_2abindS1059;
  while (1) {
    if (_M0L1iS1060 < _M0L1nS1057) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2531 = _M0L1pS1058->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS2552 = _M0L1pS1058->$9;
      float _M0L6_2atmpS2547;
      struct _M0TPB5ArrayGfE* _M0L1vS2551;
      float _M0L6_2atmpS2549;
      float _M0L4e__eS2550;
      float _M0L6_2atmpS2548;
      float _M0L6_2atmpS2544;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS2546;
      float _M0L6_2atmpS2545;
      float _M0L6_2atmpS2533;
      struct _M0TPB5ArrayGfE* _M0L2giS2543;
      float _M0L6_2atmpS2538;
      struct _M0TPB5ArrayGfE* _M0L1vS2542;
      float _M0L6_2atmpS2540;
      float _M0L4e__iS2541;
      float _M0L6_2atmpS2539;
      float _M0L6_2atmpS2535;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS2537;
      float _M0L6_2atmpS2536;
      float _M0L6_2atmpS2534;
      float _M0L6_2atmpS2532;
      int32_t _M0L6_2atmpS2553;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2547 = _M0MPC15array5Array2atGfE(_M0L2geS2552, _M0L1iS1060);
      _M0L1vS2551 = _M0L1pS1058->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2549 = _M0MPC15array5Array2atGfE(_M0L1vS2551, _M0L1iS1060);
      _M0L4e__eS2550 = _M0L1pS1058->$17;
      _M0L6_2atmpS2548 = _M0L6_2atmpS2549 - _M0L4e__eS2550;
      _M0L6_2atmpS2544 = _M0L6_2atmpS2547 * _M0L6_2atmpS2548;
      _M0L7gsyn__eS2546 = _M0L1pS1058->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2545
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS2546, _M0L1iS1060);
      _M0L6_2atmpS2533 = _M0L6_2atmpS2544 * _M0L6_2atmpS2545;
      _M0L2giS2543 = _M0L1pS1058->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2538 = _M0MPC15array5Array2atGfE(_M0L2giS2543, _M0L1iS1060);
      _M0L1vS2542 = _M0L1pS1058->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2540 = _M0MPC15array5Array2atGfE(_M0L1vS2542, _M0L1iS1060);
      _M0L4e__iS2541 = _M0L1pS1058->$18;
      _M0L6_2atmpS2539 = _M0L6_2atmpS2540 - _M0L4e__iS2541;
      _M0L6_2atmpS2535 = _M0L6_2atmpS2538 * _M0L6_2atmpS2539;
      _M0L7gsyn__iS2537 = _M0L1pS1058->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2536
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS2537, _M0L1iS1060);
      _M0L6_2atmpS2534 = _M0L6_2atmpS2535 * _M0L6_2atmpS2536;
      _M0L6_2atmpS2532 = _M0L6_2atmpS2533 + _M0L6_2atmpS2534;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS2531, _M0L1iS1060, _M0L6_2atmpS2532);
      _M0L6_2atmpS2553 = _M0L1iS1060 + 1;
      _M0L1iS1060 = _M0L6_2atmpS2553;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1049,
  float _M0L2dtS1052
) {
  int32_t _M0L1nS1048;
  int32_t _M0L7_2abindS1050;
  int32_t _M0L1iS1051;
  int32_t _M0L7_2abindS1054;
  int32_t _M0L1iS1055;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1048 = _M0L1pS1049->$2;
  _M0L7_2abindS1050 = 0;
  _M0L1iS1051 = _M0L7_2abindS1050;
  while (1) {
    if (_M0L1iS1051 < _M0L1nS1048) {
      struct _M0TPB5ArrayGfE* _M0L2heS2469 = _M0L1pS1049->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS2474 = _M0L1pS1049->$11;
      float _M0L6_2atmpS2471;
      struct _M0TPB5ArrayGfE* _M0L3gluS2473;
      float _M0L6_2atmpS2472;
      float _M0L6_2atmpS2470;
      struct _M0TPB5ArrayGfE* _M0L2hiS2475;
      struct _M0TPB5ArrayGfE* _M0L2hiS2480;
      float _M0L6_2atmpS2477;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2479;
      float _M0L6_2atmpS2478;
      float _M0L6_2atmpS2476;
      struct _M0TPB5ArrayGfE* _M0L2geS2481;
      struct _M0TPB5ArrayGfE* _M0L2geS2493;
      float _M0L6_2atmpS2483;
      struct _M0TPB5ArrayGfE* _M0L2geS2492;
      float _M0L6_2atmpS2491;
      float _M0L6_2atmpS2489;
      float _M0L3tdeS2490;
      float _M0L6_2atmpS2486;
      struct _M0TPB5ArrayGfE* _M0L2heS2488;
      float _M0L6_2atmpS2487;
      float _M0L6_2atmpS2485;
      float _M0L6_2atmpS2484;
      float _M0L6_2atmpS2482;
      struct _M0TPB5ArrayGfE* _M0L2heS2494;
      struct _M0TPB5ArrayGfE* _M0L2heS2503;
      float _M0L6_2atmpS2496;
      struct _M0TPB5ArrayGfE* _M0L2heS2502;
      float _M0L6_2atmpS2501;
      float _M0L6_2atmpS2499;
      float _M0L3treS2500;
      float _M0L6_2atmpS2498;
      float _M0L6_2atmpS2497;
      float _M0L6_2atmpS2495;
      struct _M0TPB5ArrayGfE* _M0L2giS2504;
      struct _M0TPB5ArrayGfE* _M0L2giS2516;
      float _M0L6_2atmpS2506;
      struct _M0TPB5ArrayGfE* _M0L2giS2515;
      float _M0L6_2atmpS2514;
      float _M0L6_2atmpS2512;
      float _M0L3tdiS2513;
      float _M0L6_2atmpS2509;
      struct _M0TPB5ArrayGfE* _M0L2hiS2511;
      float _M0L6_2atmpS2510;
      float _M0L6_2atmpS2508;
      float _M0L6_2atmpS2507;
      float _M0L6_2atmpS2505;
      struct _M0TPB5ArrayGfE* _M0L2hiS2517;
      struct _M0TPB5ArrayGfE* _M0L2hiS2526;
      float _M0L6_2atmpS2519;
      struct _M0TPB5ArrayGfE* _M0L2hiS2525;
      float _M0L6_2atmpS2524;
      float _M0L6_2atmpS2522;
      float _M0L3triS2523;
      float _M0L6_2atmpS2521;
      float _M0L6_2atmpS2520;
      float _M0L6_2atmpS2518;
      int32_t _M0L6_2atmpS2527;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2471 = _M0MPC15array5Array2atGfE(_M0L2heS2474, _M0L1iS1051);
      _M0L3gluS2473 = _M0L1pS1049->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2472
      = _M0MPC15array5Array2atGfE(_M0L3gluS2473, _M0L1iS1051);
      _M0L6_2atmpS2470 = _M0L6_2atmpS2471 + _M0L6_2atmpS2472;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS2469, _M0L1iS1051, _M0L6_2atmpS2470);
      _M0L2hiS2475 = _M0L1pS1049->$12;
      _M0L2hiS2480 = _M0L1pS1049->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2477 = _M0MPC15array5Array2atGfE(_M0L2hiS2480, _M0L1iS1051);
      _M0L4gabaS2479 = _M0L1pS1049->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2478
      = _M0MPC15array5Array2atGfE(_M0L4gabaS2479, _M0L1iS1051);
      _M0L6_2atmpS2476 = _M0L6_2atmpS2477 + _M0L6_2atmpS2478;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2475, _M0L1iS1051, _M0L6_2atmpS2476);
      _M0L2geS2481 = _M0L1pS1049->$9;
      _M0L2geS2493 = _M0L1pS1049->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2483 = _M0MPC15array5Array2atGfE(_M0L2geS2493, _M0L1iS1051);
      _M0L2geS2492 = _M0L1pS1049->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2491 = _M0MPC15array5Array2atGfE(_M0L2geS2492, _M0L1iS1051);
      _M0L6_2atmpS2489 = -_M0L6_2atmpS2491;
      _M0L3tdeS2490 = _M0L1pS1049->$20;
      _M0L6_2atmpS2486 = _M0L6_2atmpS2489 / _M0L3tdeS2490;
      _M0L2heS2488 = _M0L1pS1049->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2487 = _M0MPC15array5Array2atGfE(_M0L2heS2488, _M0L1iS1051);
      _M0L6_2atmpS2485 = _M0L6_2atmpS2486 + _M0L6_2atmpS2487;
      _M0L6_2atmpS2484 = _M0L2dtS1052 * _M0L6_2atmpS2485;
      _M0L6_2atmpS2482 = _M0L6_2atmpS2483 + _M0L6_2atmpS2484;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS2481, _M0L1iS1051, _M0L6_2atmpS2482);
      _M0L2heS2494 = _M0L1pS1049->$11;
      _M0L2heS2503 = _M0L1pS1049->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2496 = _M0MPC15array5Array2atGfE(_M0L2heS2503, _M0L1iS1051);
      _M0L2heS2502 = _M0L1pS1049->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2501 = _M0MPC15array5Array2atGfE(_M0L2heS2502, _M0L1iS1051);
      _M0L6_2atmpS2499 = -_M0L6_2atmpS2501;
      _M0L3treS2500 = _M0L1pS1049->$19;
      _M0L6_2atmpS2498 = _M0L6_2atmpS2499 / _M0L3treS2500;
      _M0L6_2atmpS2497 = _M0L2dtS1052 * _M0L6_2atmpS2498;
      _M0L6_2atmpS2495 = _M0L6_2atmpS2496 + _M0L6_2atmpS2497;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS2494, _M0L1iS1051, _M0L6_2atmpS2495);
      _M0L2giS2504 = _M0L1pS1049->$10;
      _M0L2giS2516 = _M0L1pS1049->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2506 = _M0MPC15array5Array2atGfE(_M0L2giS2516, _M0L1iS1051);
      _M0L2giS2515 = _M0L1pS1049->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2514 = _M0MPC15array5Array2atGfE(_M0L2giS2515, _M0L1iS1051);
      _M0L6_2atmpS2512 = -_M0L6_2atmpS2514;
      _M0L3tdiS2513 = _M0L1pS1049->$22;
      _M0L6_2atmpS2509 = _M0L6_2atmpS2512 / _M0L3tdiS2513;
      _M0L2hiS2511 = _M0L1pS1049->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2510 = _M0MPC15array5Array2atGfE(_M0L2hiS2511, _M0L1iS1051);
      _M0L6_2atmpS2508 = _M0L6_2atmpS2509 + _M0L6_2atmpS2510;
      _M0L6_2atmpS2507 = _M0L2dtS1052 * _M0L6_2atmpS2508;
      _M0L6_2atmpS2505 = _M0L6_2atmpS2506 + _M0L6_2atmpS2507;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS2504, _M0L1iS1051, _M0L6_2atmpS2505);
      _M0L2hiS2517 = _M0L1pS1049->$12;
      _M0L2hiS2526 = _M0L1pS1049->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2519 = _M0MPC15array5Array2atGfE(_M0L2hiS2526, _M0L1iS1051);
      _M0L2hiS2525 = _M0L1pS1049->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2524 = _M0MPC15array5Array2atGfE(_M0L2hiS2525, _M0L1iS1051);
      _M0L6_2atmpS2522 = -_M0L6_2atmpS2524;
      _M0L3triS2523 = _M0L1pS1049->$21;
      _M0L6_2atmpS2521 = _M0L6_2atmpS2522 / _M0L3triS2523;
      _M0L6_2atmpS2520 = _M0L2dtS1052 * _M0L6_2atmpS2521;
      _M0L6_2atmpS2518 = _M0L6_2atmpS2519 + _M0L6_2atmpS2520;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS2517, _M0L1iS1051, _M0L6_2atmpS2518);
      _M0L6_2atmpS2527 = _M0L1iS1051 + 1;
      _M0L1iS1051 = _M0L6_2atmpS2527;
      continue;
    }
    break;
  }
  _M0L7_2abindS1054 = 0;
  _M0L1iS1055 = _M0L7_2abindS1054;
  while (1) {
    if (_M0L1iS1055 < _M0L1nS1048) {
      struct _M0TPB5ArrayGfE* _M0L3gluS2528 = _M0L1pS1049->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS2529;
      int32_t _M0L6_2atmpS2530;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS2528, _M0L1iS1055, 0x0p+0f);
      _M0L4gabaS2529 = _M0L1pS1049->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS2529, _M0L1iS1055, 0x0p+0f);
      _M0L6_2atmpS2530 = _M0L1iS1055 + 1;
      _M0L1iS1055 = _M0L6_2atmpS2530;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1034,
  float _M0L2dtS1043
) {
  int32_t _M0L1nS1033;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S1035;
  float _M0L2tmS1036;
  float _M0L2elS1037;
  float _M0L1rS1038;
  float _M0L2vtS1039;
  float _M0L2vrS1040;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS2468;
  float _M0L11tabs__constS1041;
  float _M0L6_2atmpS2467;
  int32_t _M0L11tabs__stepsS1042;
  int32_t _M0L7_2abindS1044;
  int32_t _M0L1iS1045;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1033 = _M0L1pS1034->$2;
  _M0L3p__S1035 = _M0L1pS1034->$0;
  _M0L2tmS1036 = _M0L3p__S1035->$2;
  _M0L2elS1037 = _M0L3p__S1035->$5;
  _M0L1rS1038 = _M0L3p__S1035->$6;
  _M0L2vtS1039 = _M0L3p__S1035->$3;
  _M0L2vrS1040 = _M0L3p__S1035->$4;
  _M0L5spikeS2468 = _M0L1pS1034->$1;
  _M0L11tabs__constS1041 = _M0L5spikeS2468->$0;
  _M0L6_2atmpS2467 = _M0L11tabs__constS1041 / _M0L2dtS1043;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS1042 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2467);
  _M0L7_2abindS1044 = 0;
  _M0L1iS1045 = _M0L7_2abindS1044;
  while (1) {
    if (_M0L1iS1045 < _M0L1nS1033) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS2427 = _M0L1pS1034->$6;
      int32_t _M0L6_2atmpS2426;
      struct _M0TPB5ArrayGfE* _M0L1vS2433;
      struct _M0TPB5ArrayGfE* _M0L1vS2454;
      float _M0L6_2atmpS2435;
      float _M0L6_2atmpS2437;
      struct _M0TPB5ArrayGfE* _M0L1vS2453;
      float _M0L6_2atmpS2452;
      float _M0L6_2atmpS2451;
      float _M0L6_2atmpS2443;
      struct _M0TPB5ArrayGfE* _M0L1wS2450;
      float _M0L6_2atmpS2449;
      float _M0L6_2atmpS2446;
      struct _M0TPB5ArrayGfE* _M0L1iS2448;
      float _M0L6_2atmpS2447;
      float _M0L6_2atmpS2445;
      float _M0L6_2atmpS2444;
      float _M0L6_2atmpS2439;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS2442;
      float _M0L6_2atmpS2441;
      float _M0L6_2atmpS2440;
      float _M0L6_2atmpS2438;
      float _M0L6_2atmpS2436;
      float _M0L6_2atmpS2434;
      struct _M0TPB5ArrayGbE* _M0L4fireS2455;
      struct _M0TPB5ArrayGfE* _M0L1vS2458;
      float _M0L6_2atmpS2457;
      int32_t _M0L6_2atmpS2456;
      struct _M0TPB5ArrayGfE* _M0L1vS2459;
      struct _M0TPB5ArrayGbE* _M0L4fireS2461;
      float _M0L6_2atmpS2460;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2463;
      struct _M0TPB5ArrayGbE* _M0L4fireS2465;
      int32_t _M0L6_2atmpS2464;
      int32_t _M0L6_2atmpS2425;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2426
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2427, _M0L1iS1045);
      if (_M0L6_2atmpS2426 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS2428 = _M0L1pS1034->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2429;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2432;
        int32_t _M0L6_2atmpS2431;
        int32_t _M0L6_2atmpS2430;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS2428, _M0L1iS1045, 0);
        _M0L4tabsS2429 = _M0L1pS1034->$6;
        _M0L4tabsS2432 = _M0L1pS1034->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2431
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2432, _M0L1iS1045);
        _M0L6_2atmpS2430 = _M0L6_2atmpS2431 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS2429, _M0L1iS1045, _M0L6_2atmpS2430);
        goto join_1046;
      }
      _M0L1vS2433 = _M0L1pS1034->$3;
      _M0L1vS2454 = _M0L1pS1034->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2435 = _M0MPC15array5Array2atGfE(_M0L1vS2454, _M0L1iS1045);
      _M0L6_2atmpS2437 = _M0L2dtS1043 / _M0L2tmS1036;
      _M0L1vS2453 = _M0L1pS1034->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2452 = _M0MPC15array5Array2atGfE(_M0L1vS2453, _M0L1iS1045);
      _M0L6_2atmpS2451 = _M0L6_2atmpS2452 - _M0L2elS1037;
      _M0L6_2atmpS2443 = -_M0L6_2atmpS2451;
      _M0L1wS2450 = _M0L1pS1034->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2449 = _M0MPC15array5Array2atGfE(_M0L1wS2450, _M0L1iS1045);
      _M0L6_2atmpS2446 = -_M0L6_2atmpS2449;
      _M0L1iS2448 = _M0L1pS1034->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2447 = _M0MPC15array5Array2atGfE(_M0L1iS2448, _M0L1iS1045);
      _M0L6_2atmpS2445 = _M0L6_2atmpS2446 + _M0L6_2atmpS2447;
      _M0L6_2atmpS2444 = _M0L1rS1038 * _M0L6_2atmpS2445;
      _M0L6_2atmpS2439 = _M0L6_2atmpS2443 + _M0L6_2atmpS2444;
      _M0L9syn__currS2442 = _M0L1pS1034->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2441
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS2442, _M0L1iS1045);
      _M0L6_2atmpS2440 = _M0L1rS1038 * _M0L6_2atmpS2441;
      _M0L6_2atmpS2438 = _M0L6_2atmpS2439 - _M0L6_2atmpS2440;
      _M0L6_2atmpS2436 = _M0L6_2atmpS2437 * _M0L6_2atmpS2438;
      _M0L6_2atmpS2434 = _M0L6_2atmpS2435 + _M0L6_2atmpS2436;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2433, _M0L1iS1045, _M0L6_2atmpS2434);
      _M0L4fireS2455 = _M0L1pS1034->$5;
      _M0L1vS2458 = _M0L1pS1034->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2457 = _M0MPC15array5Array2atGfE(_M0L1vS2458, _M0L1iS1045);
      _M0L6_2atmpS2456 = _M0L6_2atmpS2457 > _M0L2vtS1039;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2455, _M0L1iS1045, _M0L6_2atmpS2456);
      _M0L1vS2459 = _M0L1pS1034->$3;
      _M0L4fireS2461 = _M0L1pS1034->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2461, _M0L1iS1045)) {
        _M0L6_2atmpS2460 = _M0L2vrS1040;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS2462 = _M0L1pS1034->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2460
        = _M0MPC15array5Array2atGfE(_M0L1vS2462, _M0L1iS1045);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS2459, _M0L1iS1045, _M0L6_2atmpS2460);
      _M0L4tabsS2463 = _M0L1pS1034->$6;
      _M0L4fireS2465 = _M0L1pS1034->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2465, _M0L1iS1045)) {
        _M0L6_2atmpS2464 = _M0L11tabs__stepsS1042;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS2466 = _M0L1pS1034->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS2464
        = _M0MPC15array5Array2atGiE(_M0L4tabsS2466, _M0L1iS1045);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2463, _M0L1iS1045, _M0L6_2atmpS2464);
      goto join_1046;
      goto joinlet_2782;
      join_1046:;
      _M0L6_2atmpS2425 = _M0L1iS1045 + 1;
      _M0L1iS1045 = _M0L6_2atmpS2425;
      continue;
      joinlet_2782:;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS1021,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1024,
  struct _M0TPB5ArrayGfE* _M0L7post__gS1030
) {
  int32_t _M0L4rowsS1020;
  int32_t _M0L7_2abindS1022;
  int32_t _M0L1iS1023;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS1020 = _M0L1mS1021->$0;
  _M0L7_2abindS1022 = 0;
  _M0L1iS1023 = _M0L7_2abindS1022;
  while (1) {
    if (_M0L1iS1023 < _M0L4rowsS1020) {
      int32_t _M0L6_2atmpS2424;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1024, _M0L1iS1023)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2423 = _M0L1mS1021->$2;
        int32_t _M0L5startS1025;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2421;
        int32_t _M0L6_2atmpS2422;
        int32_t _M0L3endS1026;
        int32_t _M0L1kS1027;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS1025
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2423, _M0L1iS1023);
        _M0L6rowptrS2421 = _M0L1mS1021->$2;
        _M0L6_2atmpS2422 = _M0L1iS1023 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS1026
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2421, _M0L6_2atmpS2422);
        _M0L1kS1027 = _M0L5startS1025;
        while (1) {
          if (_M0L1kS1027 < _M0L3endS1026) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS2419 = _M0L1mS1021->$3;
            int32_t _M0L9post__idxS1028;
            struct _M0TPB5ArrayGfE* _M0L4valsS2418;
            float _M0L1wS1029;
            float _M0L6_2atmpS2417;
            float _M0L6_2atmpS2416;
            int32_t _M0L6_2atmpS2420;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS1028
            = _M0MPC15array5Array2atGiE(_M0L6colptrS2419, _M0L1kS1027);
            _M0L4valsS2418 = _M0L1mS1021->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS1029
            = _M0MPC15array5Array2atGfE(_M0L4valsS2418, _M0L1kS1027);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS2417
            = _M0MPC15array5Array2atGfE(_M0L7post__gS1030, _M0L9post__idxS1028);
            _M0L6_2atmpS2416 = _M0L6_2atmpS2417 + _M0L1wS1029;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS1030, _M0L9post__idxS1028, _M0L6_2atmpS2416);
            _M0L6_2atmpS2420 = _M0L1kS1027 + 1;
            _M0L1kS1027 = _M0L6_2atmpS2420;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS2424 = _M0L1iS1023 + 1;
      _M0L1iS1023 = _M0L6_2atmpS2424;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3set(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS1007,
  int32_t _M0L1iS1006,
  int32_t _M0L1jS1012,
  float _M0L1vS1013
) {
  int32_t _if__result_2785;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS2415;
  int32_t _M0L5startS1008;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS2413;
  int32_t _M0L6_2atmpS2414;
  int32_t _M0L3endS1009;
  struct _M0TPB8MutLocalGbE* _M0L5foundS1010;
  int32_t _M0L1kS1011;
  int32_t _M0L3valS2405;
  #line 258 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  if (_M0L1iS1006 < 0) {
    _if__result_2785 = 1;
  } else {
    int32_t _M0L4rowsS2400 = _M0L1mS1007->$0;
    _if__result_2785 = _M0L1iS1006 >= _M0L4rowsS2400;
  }
  if (_if__result_2785) {
    return 0;
  }
  _M0L6rowptrS2415 = _M0L1mS1007->$2;
  #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5startS1008 = _M0MPC15array5Array2atGiE(_M0L6rowptrS2415, _M0L1iS1006);
  _M0L6rowptrS2413 = _M0L1mS1007->$2;
  _M0L6_2atmpS2414 = _M0L1iS1006 + 1;
  #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L3endS1009
  = _M0MPC15array5Array2atGiE(_M0L6rowptrS2413, _M0L6_2atmpS2414);
  _M0L5foundS1010
  = (struct _M0TPB8MutLocalGbE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGbE));
  Moonbit_object_header(_M0L5foundS1010)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5foundS1010->$0 = 0;
  _M0L1kS1011 = _M0L5startS1008;
  while (1) {
    if (_M0L1kS1011 < _M0L3endS1009) {
      struct _M0TPB5ArrayGiE* _M0L6colptrS2402 = _M0L1mS1007->$3;
      int32_t _M0L6_2atmpS2401;
      int32_t _M0L6_2atmpS2404;
      #line 267 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS2401
      = _M0MPC15array5Array2atGiE(_M0L6colptrS2402, _M0L1kS1011);
      if (_M0L6_2atmpS2401 == _M0L1jS1012) {
        struct _M0TPB5ArrayGfE* _M0L4valsS2403 = _M0L1mS1007->$4;
        #line 268 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0MPC15array5Array3setGfE(_M0L4valsS2403, _M0L1kS1011, _M0L1vS1013);
        _M0L5foundS1010->$0 = 1;
        break;
      }
      _M0L6_2atmpS2404 = _M0L1kS1011 + 1;
      _M0L1kS1011 = _M0L6_2atmpS2404;
      continue;
    }
    break;
  }
  _M0L3valS2405 = _M0L5foundS1010->$0;
  moonbit_decref_cycle_free(_M0L5foundS1010);
  if (!_M0L3valS2405) {
    struct _M0TPB5ArrayGiE* _M0L6colptrS2406 = _M0L1mS1007->$3;
    struct _M0TPB5ArrayGfE* _M0L4valsS2407;
    int32_t _M0L7n__rowsS1015;
    int32_t _M0L7_2abindS1016;
    int32_t _M0L7_2abindS1017;
    int32_t _M0L1rS1018;
    #line 275 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
    _M0MPC15array5Array6insertGiE(_M0L6colptrS2406, _M0L3endS1009, _M0L1jS1012);
    _M0L4valsS2407 = _M0L1mS1007->$4;
    #line 276 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
    _M0MPC15array5Array6insertGfE(_M0L4valsS2407, _M0L3endS1009, _M0L1vS1013);
    _M0L7n__rowsS1015 = _M0L1mS1007->$0;
    _M0L7_2abindS1016 = _M0L1iS1006 + 1;
    _M0L7_2abindS1017 = _M0L7n__rowsS1015 + 1;
    _M0L1rS1018 = _M0L7_2abindS1016;
    while (1) {
      if (_M0L1rS1018 < _M0L7_2abindS1017) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2408 = _M0L1mS1007->$2;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2411 = _M0L1mS1007->$2;
        int32_t _M0L6_2atmpS2410;
        int32_t _M0L6_2atmpS2409;
        int32_t _M0L6_2atmpS2412;
        #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L6_2atmpS2410
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2411, _M0L1rS1018);
        _M0L6_2atmpS2409 = _M0L6_2atmpS2410 + 1;
        #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0MPC15array5Array3setGiE(_M0L6rowptrS2408, _M0L1rS1018, _M0L6_2atmpS2409);
        _M0L6_2atmpS2412 = _M0L1rS1018 + 1;
        _M0L1rS1018 = _M0L6_2atmpS2412;
        continue;
      }
      break;
    }
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR5empty(
  int32_t _M0L4rowsS1004,
  int32_t _M0L4colsS1005
) {
  int32_t _M0L6_2atmpS2399;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1003;
  int32_t* _M0L6_2atmpS2398;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2395;
  float* _M0L6_2atmpS2397;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2396;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_2788;
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2399 = _M0L4rowsS1004 + 1;
  #line 41 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS1003 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS2399, 0);
  _M0L6_2atmpS2398 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS2395
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2395)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS2395->$0 = _M0L6_2atmpS2398;
  _M0L6_2atmpS2395->$1 = 0;
  _M0L6_2atmpS2397 = moonbit_empty_float_array;
  _M0L6_2atmpS2396
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2396)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2396->$0 = _M0L6_2atmpS2397;
  _M0L6_2atmpS2396->$1 = 0;
  _block_2788
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_2788)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 54, 0);
  _block_2788->$0 = _M0L4rowsS1004;
  _block_2788->$1 = _M0L4colsS1005;
  _block_2788->$2 = _M0L6rowptrS1003;
  _block_2788->$3 = _M0L6_2atmpS2395;
  _block_2788->$4 = _M0L6_2atmpS2396;
  return _block_2788;
}

int32_t _M0FP26RiantR8snn__mbt10stdp__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1000,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS980,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS982,
  struct _M0TPB5ArrayGiE* _M0L6colptrS997,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS993,
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS978,
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS985,
  float _M0L6t__nowS988,
  float _M0L2dtS984
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS2312;
  int32_t _M0L6_2atmpS2311;
  int32_t _if__result_2789;
  int32_t _M0L6n__preS979;
  int32_t _M0L7n__postS981;
  float _M0L6_2atmpS2393;
  float _M0L8tau__preS2394;
  float _M0L6_2atmpS2392;
  float _M0L10decay__preS983;
  float _M0L6_2atmpS2390;
  float _M0L9tau__postS2391;
  float _M0L6_2atmpS2389;
  float _M0L11decay__postS986;
  struct _M0TPB8MutLocalGiE* _M0L1jS987;
  struct _M0TPB8MutLocalGiE* _M0L1iS990;
  #line 905 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6activeS2312 = _M0L4varsS978->$4;
  #line 917 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS2311 = _M0MPC15array5Array6lengthGbE(_M0L6activeS2312);
  if (_M0L6_2atmpS2311 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS2310 = _M0L4varsS978->$4;
    int32_t _M0L6_2atmpS2309;
    #line 917 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS2309 = _M0MPC15array5Array2atGbE(_M0L6activeS2310, 0);
    _if__result_2789 = !_M0L6_2atmpS2309;
  } else {
    _if__result_2789 = 0;
  }
  if (_if__result_2789) {
    return 0;
  }
  #line 921 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS979 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS980);
  #line 922 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS981 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS982);
  _M0L6_2atmpS2393 = -_M0L2dtS984;
  _M0L8tau__preS2394 = _M0L5paramS985->$2;
  _M0L6_2atmpS2392 = _M0L6_2atmpS2393 / _M0L8tau__preS2394;
  #line 923 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10decay__preS983 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2392);
  _M0L6_2atmpS2390 = -_M0L2dtS984;
  _M0L9tau__postS2391 = _M0L5paramS985->$3;
  _M0L6_2atmpS2389 = _M0L6_2atmpS2390 / _M0L9tau__postS2391;
  #line 924 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L11decay__postS986 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2389);
  _M0L1jS987
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS987)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS987->$0 = 0;
  while (1) {
    int32_t _M0L3valS2313 = _M0L1jS987->$0;
    if (_M0L3valS2313 < _M0L6n__preS979) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS2314 = _M0L4varsS978->$0;
      int32_t _M0L3valS2315 = _M0L1jS987->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS2318 = _M0L4varsS978->$0;
      int32_t _M0L3valS2319 = _M0L1jS987->$0;
      float _M0L6_2atmpS2317;
      float _M0L6_2atmpS2316;
      int32_t _M0L3valS2320;
      int32_t _M0L3valS2331;
      int32_t _M0L6_2atmpS2330;
      #line 927 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS2317
      = _M0MPC15array5Array2atGfE(_M0L4tpreS2318, _M0L3valS2319);
      _M0L6_2atmpS2316 = _M0L6_2atmpS2317 * _M0L10decay__preS983;
      #line 927 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS2314, _M0L3valS2315, _M0L6_2atmpS2316);
      _M0L3valS2320 = _M0L1jS987->$0;
      #line 928 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS980, _M0L3valS2320)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS2321 = _M0L4varsS978->$0;
        int32_t _M0L3valS2322 = _M0L1jS987->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS2326 = _M0L4varsS978->$0;
        int32_t _M0L3valS2327 = _M0L1jS987->$0;
        float _M0L6_2atmpS2324;
        float _M0L6a__preS2325;
        float _M0L6_2atmpS2323;
        struct _M0TPB5ArrayGfE* _M0L9last__preS2328;
        int32_t _M0L3valS2329;
        #line 929 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS2324
        = _M0MPC15array5Array2atGfE(_M0L4tpreS2326, _M0L3valS2327);
        _M0L6a__preS2325 = _M0L5paramS985->$0;
        _M0L6_2atmpS2323 = _M0L6_2atmpS2324 + _M0L6a__preS2325;
        #line 929 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS2321, _M0L3valS2322, _M0L6_2atmpS2323);
        _M0L9last__preS2328 = _M0L4varsS978->$2;
        _M0L3valS2329 = _M0L1jS987->$0;
        #line 930 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L9last__preS2328, _M0L3valS2329, _M0L6t__nowS988);
      }
      _M0L3valS2331 = _M0L1jS987->$0;
      _M0L6_2atmpS2330 = _M0L3valS2331 + 1;
      _M0L1jS987->$0 = _M0L6_2atmpS2330;
      continue;
    }
    break;
  }
  _M0L1iS990
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS990)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS990->$0 = 0;
  while (1) {
    int32_t _M0L3valS2332 = _M0L1iS990->$0;
    if (_M0L3valS2332 < _M0L7n__postS981) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS2333 = _M0L4varsS978->$1;
      int32_t _M0L3valS2334 = _M0L1iS990->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS2337 = _M0L4varsS978->$1;
      int32_t _M0L3valS2338 = _M0L1iS990->$0;
      float _M0L6_2atmpS2336;
      float _M0L6_2atmpS2335;
      int32_t _M0L3valS2339;
      int32_t _M0L3valS2350;
      int32_t _M0L6_2atmpS2349;
      #line 936 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS2336
      = _M0MPC15array5Array2atGfE(_M0L5tpostS2337, _M0L3valS2338);
      _M0L6_2atmpS2335 = _M0L6_2atmpS2336 * _M0L11decay__postS986;
      #line 936 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS2333, _M0L3valS2334, _M0L6_2atmpS2335);
      _M0L3valS2339 = _M0L1iS990->$0;
      #line 937 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS982, _M0L3valS2339)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS2340 = _M0L4varsS978->$1;
        int32_t _M0L3valS2341 = _M0L1iS990->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS2345 = _M0L4varsS978->$1;
        int32_t _M0L3valS2346 = _M0L1iS990->$0;
        float _M0L6_2atmpS2343;
        float _M0L7a__postS2344;
        float _M0L6_2atmpS2342;
        struct _M0TPB5ArrayGfE* _M0L10last__postS2347;
        int32_t _M0L3valS2348;
        #line 938 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS2343
        = _M0MPC15array5Array2atGfE(_M0L5tpostS2345, _M0L3valS2346);
        _M0L7a__postS2344 = _M0L5paramS985->$1;
        _M0L6_2atmpS2342 = _M0L6_2atmpS2343 + _M0L7a__postS2344;
        #line 938 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS2340, _M0L3valS2341, _M0L6_2atmpS2342);
        _M0L10last__postS2347 = _M0L4varsS978->$3;
        _M0L3valS2348 = _M0L1iS990->$0;
        #line 939 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L10last__postS2347, _M0L3valS2348, _M0L6t__nowS988);
      }
      _M0L3valS2350 = _M0L1iS990->$0;
      _M0L6_2atmpS2349 = _M0L3valS2350 + 1;
      _M0L1iS990->$0 = _M0L6_2atmpS2349;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS990);
    }
    break;
  }
  _M0L1jS987->$0 = 0;
  while (1) {
    int32_t _M0L3valS2351 = _M0L1jS987->$0;
    if (_M0L3valS2351 < _M0L6n__preS979) {
      int32_t _M0L3valS2388 = _M0L1jS987->$0;
      int32_t _M0L5startS992;
      int32_t _M0L3valS2387;
      int32_t _M0L6_2atmpS2386;
      int32_t _M0L3endS994;
      struct _M0TPB8MutLocalGiE* _M0L1sS995;
      int32_t _M0L3valS2385;
      int32_t _M0L6_2atmpS2384;
      #line 947 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS992
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS993, _M0L3valS2388);
      _M0L3valS2387 = _M0L1jS987->$0;
      _M0L6_2atmpS2386 = _M0L3valS2387 + 1;
      #line 948 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS994
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS993, _M0L6_2atmpS2386);
      _M0L1sS995
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS995)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS995->$0 = _M0L5startS992;
      while (1) {
        int32_t _M0L3valS2352 = _M0L1sS995->$0;
        if (_M0L3valS2352 < _M0L3endS994) {
          int32_t _M0L3valS2383 = _M0L1sS995->$0;
          int32_t _M0L9post__idxS996;
          int32_t _M0L3valS2382;
          int32_t _M0L10pre__firedS998;
          int32_t _M0L11post__firedS999;
          int32_t _M0L3valS2372;
          float _M0L6_2atmpS2370;
          float _M0L6w__minS2371;
          int32_t _M0L3valS2377;
          float _M0L6_2atmpS2375;
          float _M0L6w__maxS2376;
          int32_t _M0L3valS2381;
          int32_t _M0L6_2atmpS2380;
          #line 951 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS996
          = _M0MPC15array5Array2atGiE(_M0L6colptrS997, _M0L3valS2383);
          _M0L3valS2382 = _M0L1jS987->$0;
          #line 952 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L10pre__firedS998
          = _M0MPC15array5Array2atGbE(_M0L9pre__fireS980, _M0L3valS2382);
          #line 953 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS999
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS982, _M0L9post__idxS996);
          if (_M0L10pre__firedS998) {
            int32_t _M0L3valS2353 = _M0L1sS995->$0;
            int32_t _M0L3valS2360 = _M0L1sS995->$0;
            float _M0L6_2atmpS2355;
            float _M0L7a__postS2357;
            struct _M0TPB5ArrayGfE* _M0L5tpostS2359;
            float _M0L6_2atmpS2358;
            float _M0L6_2atmpS2356;
            float _M0L6_2atmpS2354;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS2355
            = _M0MPC15array5Array2atGfE(_M0L1wS1000, _M0L3valS2360);
            _M0L7a__postS2357 = _M0L5paramS985->$1;
            _M0L5tpostS2359 = _M0L4varsS978->$1;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS2358
            = _M0MPC15array5Array2atGfE(_M0L5tpostS2359, _M0L9post__idxS996);
            _M0L6_2atmpS2356 = _M0L7a__postS2357 * _M0L6_2atmpS2358;
            _M0L6_2atmpS2354 = _M0L6_2atmpS2355 + _M0L6_2atmpS2356;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1000, _M0L3valS2353, _M0L6_2atmpS2354);
          }
          if (_M0L11post__firedS999) {
            int32_t _M0L3valS2361 = _M0L1sS995->$0;
            int32_t _M0L3valS2369 = _M0L1sS995->$0;
            float _M0L6_2atmpS2363;
            float _M0L6a__preS2365;
            struct _M0TPB5ArrayGfE* _M0L4tpreS2367;
            int32_t _M0L3valS2368;
            float _M0L6_2atmpS2366;
            float _M0L6_2atmpS2364;
            float _M0L6_2atmpS2362;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS2363
            = _M0MPC15array5Array2atGfE(_M0L1wS1000, _M0L3valS2369);
            _M0L6a__preS2365 = _M0L5paramS985->$0;
            _M0L4tpreS2367 = _M0L4varsS978->$0;
            _M0L3valS2368 = _M0L1jS987->$0;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS2366
            = _M0MPC15array5Array2atGfE(_M0L4tpreS2367, _M0L3valS2368);
            _M0L6_2atmpS2364 = _M0L6a__preS2365 * _M0L6_2atmpS2366;
            _M0L6_2atmpS2362 = _M0L6_2atmpS2363 + _M0L6_2atmpS2364;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1000, _M0L3valS2361, _M0L6_2atmpS2362);
          }
          _M0L3valS2372 = _M0L1sS995->$0;
          #line 963 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS2370
          = _M0MPC15array5Array2atGfE(_M0L1wS1000, _M0L3valS2372);
          _M0L6w__minS2371 = _M0L5paramS985->$5;
          if (_M0L6_2atmpS2370 < _M0L6w__minS2371) {
            int32_t _M0L3valS2373 = _M0L1sS995->$0;
            float _M0L6w__minS2374 = _M0L5paramS985->$5;
            #line 963 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1000, _M0L3valS2373, _M0L6w__minS2374);
          }
          _M0L3valS2377 = _M0L1sS995->$0;
          #line 964 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS2375
          = _M0MPC15array5Array2atGfE(_M0L1wS1000, _M0L3valS2377);
          _M0L6w__maxS2376 = _M0L5paramS985->$4;
          if (_M0L6_2atmpS2375 > _M0L6w__maxS2376) {
            int32_t _M0L3valS2378 = _M0L1sS995->$0;
            float _M0L6w__maxS2379 = _M0L5paramS985->$4;
            #line 964 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1000, _M0L3valS2378, _M0L6w__maxS2379);
          }
          _M0L3valS2381 = _M0L1sS995->$0;
          _M0L6_2atmpS2380 = _M0L3valS2381 + 1;
          _M0L1sS995->$0 = _M0L6_2atmpS2380;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS995);
        }
        break;
      }
      _M0L3valS2385 = _M0L1jS987->$0;
      _M0L6_2atmpS2384 = _M0L3valS2385 + 1;
      _M0L1jS987->$0 = _M0L6_2atmpS2384;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS987);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt26stdp__kernel__plot_2einner(
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS944,
  float _M0L6t__maxS942,
  int32_t _M0L5widthS936,
  int32_t _M0L6heightS948
) {
  int32_t _M0L7n__colsS935;
  struct _M0TPB8MutLocalGfE* _M0L2mnS937;
  struct _M0TPB8MutLocalGfE* _M0L2mxS938;
  float* _M0L6_2atmpS2308;
  struct _M0TPB5ArrayGfE* _M0L4valsS939;
  struct _M0TPB8MutLocalGiE* _M0L1iS940;
  float _M0L3valS2264;
  float _M0L3valS2265;
  float _M0L6_2atmpS2263;
  float _M0L3valS2306;
  float _M0L3valS2307;
  float _M0L6vrangeS946;
  struct _M0TPB5ArrayGsE* _M0L6canvasS947;
  moonbit_string_t _M0L3padS949;
  moonbit_string_t _M0L9row__initS950;
  int32_t _M0L7_2abindS951;
  int32_t _M0L1rS952;
  struct _M0TPB8MutLocalGiE* _M0L1cS954;
  float _M0L3valS2305;
  moonbit_string_t _M0L10max__labelS963;
  float _M0L3valS2303;
  float _M0L3valS2304;
  float _M0L6_2atmpS2302;
  float _M0L6_2atmpS2301;
  moonbit_string_t _M0L10mid__labelS964;
  float _M0L3valS2300;
  moonbit_string_t _M0L10min__labelS965;
  int32_t _M0L1aS967;
  int32_t _M0L1bS968;
  int32_t _M0L1cS969;
  int32_t _M0L1mS970;
  int32_t _M0L12label__widthS966;
  int32_t _M0L7_2abindS971;
  int32_t _M0L1rS972;
  int32_t _M0L6_2atmpS2299;
  moonbit_string_t _M0L8pad__strS977;
  moonbit_string_t _M0L6_2atmpS2297;
  moonbit_string_t _M0L6_2atmpS2298;
  moonbit_string_t _M0L6_2atmpS2296;
  moonbit_string_t _M0L6_2atmpS2294;
  moonbit_string_t _M0L6_2atmpS2295;
  moonbit_string_t _M0L6_2atmpS2293;
  moonbit_string_t _M0L6_2atmpS2292;
  #line 276 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  if (_M0L5widthS936 > 1) {
    _M0L7n__colsS935 = _M0L5widthS936;
  } else {
    _M0L7n__colsS935 = 1;
  }
  _M0L2mnS937
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L2mnS937)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2mnS937->$0 = 0x0p+0f;
  _M0L2mxS938
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L2mxS938)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2mxS938->$0 = 0x0p+0f;
  _M0L6_2atmpS2308 = moonbit_empty_float_array;
  _M0L4valsS939
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS939)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L4valsS939->$0 = _M0L6_2atmpS2308;
  _M0L4valsS939->$1 = 0;
  _M0L1iS940
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS940)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS940->$0 = 0;
  while (1) {
    int32_t _M0L3valS2245 = _M0L1iS940->$0;
    if (_M0L3valS2245 < _M0L7n__colsS935) {
      float _M0L6_2atmpS2255 = -_M0L6t__maxS942;
      int32_t _M0L3valS2262 = _M0L1iS940->$0;
      float _M0L6_2atmpS2260 = (float)_M0L3valS2262;
      float _M0L6_2atmpS2261 = 0x1p+1f * _M0L6t__maxS942;
      float _M0L6_2atmpS2257 = _M0L6_2atmpS2260 * _M0L6_2atmpS2261;
      int32_t _M0L6_2atmpS2259 = _M0L7n__colsS935 - 1;
      float _M0L6_2atmpS2258 = (float)_M0L6_2atmpS2259;
      float _M0L6_2atmpS2256 = _M0L6_2atmpS2257 / _M0L6_2atmpS2258;
      float _M0L2dtS941 = _M0L6_2atmpS2255 + _M0L6_2atmpS2256;
      float _M0L8tau__preS2251 = _M0L5paramS944->$2;
      float _M0L9tau__postS2252 = _M0L5paramS944->$3;
      float _M0L6a__preS2253 = _M0L5paramS944->$0;
      float _M0L7a__postS2254 = _M0L5paramS944->$1;
      float _M0L1wS943;
      int32_t _M0L3valS2246;
      int32_t _M0L3valS2250;
      int32_t _M0L6_2atmpS2249;
      #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L1wS943
      = _M0FP26RiantR8snn__mbt16gerstner__kernel(_M0L2dtS941, _M0L8tau__preS2251, _M0L9tau__postS2252, _M0L6a__preS2253, _M0L7a__postS2254);
      #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array4pushGfE(_M0L4valsS939, _M0L1wS943);
      _M0L3valS2246 = _M0L1iS940->$0;
      if (_M0L3valS2246 == 0) {
        _M0L2mnS937->$0 = _M0L1wS943;
        _M0L2mxS938->$0 = _M0L1wS943;
      } else {
        float _M0L3valS2247 = _M0L2mnS937->$0;
        float _M0L3valS2248;
        if (_M0L1wS943 < _M0L3valS2247) {
          _M0L2mnS937->$0 = _M0L1wS943;
        }
        _M0L3valS2248 = _M0L2mxS938->$0;
        if (_M0L1wS943 > _M0L3valS2248) {
          _M0L2mxS938->$0 = _M0L1wS943;
        }
      }
      _M0L3valS2250 = _M0L1iS940->$0;
      _M0L6_2atmpS2249 = _M0L3valS2250 + 1;
      _M0L1iS940->$0 = _M0L6_2atmpS2249;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS940);
    }
    break;
  }
  _M0L3valS2264 = _M0L2mxS938->$0;
  _M0L3valS2265 = _M0L2mnS937->$0;
  _M0L6_2atmpS2263 = _M0L3valS2264 - _M0L3valS2265;
  if (_M0L6_2atmpS2263 < 0x1.12e0be826d695p-30f) {
    float _M0L3valS2267 = _M0L2mnS937->$0;
    float _M0L6_2atmpS2266 = _M0L3valS2267 + 0x1.12e0be826d695p-30f;
    _M0L2mxS938->$0 = _M0L6_2atmpS2266;
  }
  _M0L3valS2306 = _M0L2mxS938->$0;
  _M0L3valS2307 = _M0L2mnS937->$0;
  _M0L6vrangeS946 = _M0L3valS2306 - _M0L3valS2307;
  #line 311 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6canvasS947
  = _M0MPC15array5Array4makeGsE(_M0L6heightS948, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L3padS949 = _M0MPC16string6String4make(_M0L7n__colsS935, 32);
  _M0L9row__initS950 = (moonbit_string_t)moonbit_string_literal_10.data;
  _M0L7_2abindS951 = 0;
  _M0L1rS952 = _M0L7_2abindS951;
  while (1) {
    if (_M0L1rS952 < _M0L6heightS948) {
      moonbit_string_t _M0L6_2atmpS2268;
      int32_t _M0L6_2atmpS2269;
      #line 315 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS2268 = moonbit_add_string(_M0L9row__initS950, _M0L3padS949);
      #line 315 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGsE(_M0L6canvasS947, _M0L1rS952, _M0L6_2atmpS2268);
      _M0L6_2atmpS2269 = _M0L1rS952 + 1;
      _M0L1rS952 = _M0L6_2atmpS2269;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L3padS949);
    }
    break;
  }
  _M0L1cS954
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1cS954)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1cS954->$0 = 0;
  while (1) {
    int32_t _M0L3valS2270 = _M0L1cS954->$0;
    if (_M0L3valS2270 < _M0L7n__colsS935) {
      int32_t _M0L3valS2282 = _M0L1cS954->$0;
      float _M0L1wS955;
      float _M0L3valS2281;
      float _M0L6_2atmpS2280;
      float _M0L10normalizedS956;
      float _M0L6_2atmpS2277;
      float _M0L6_2atmpS2279;
      float _M0L6_2atmpS2278;
      float _M0L6_2atmpS2276;
      int32_t _M0L14row__from__topS957;
      int32_t _M0L1rS958;
      int32_t _M0L2chS959;
      moonbit_string_t _M0L8row__strS960;
      int32_t _M0L3valS2274;
      int32_t _M0L6_2atmpS2275;
      int32_t _M0L6_2atmpS2273;
      moonbit_string_t _M0L8new__rowS961;
      int32_t _M0L3valS2272;
      int32_t _M0L6_2atmpS2271;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L1wS955 = _M0MPC15array5Array2atGfE(_M0L4valsS939, _M0L3valS2282);
      _M0L3valS2281 = _M0L2mnS937->$0;
      _M0L6_2atmpS2280 = _M0L1wS955 - _M0L3valS2281;
      _M0L10normalizedS956 = _M0L6_2atmpS2280 / _M0L6vrangeS946;
      _M0L6_2atmpS2277 = (float)_M0L6heightS948;
      _M0L6_2atmpS2279 = (float)1;
      _M0L6_2atmpS2278 = _M0L6_2atmpS2279 * _M0L10normalizedS956;
      _M0L6_2atmpS2276 = _M0L6_2atmpS2277 - _M0L6_2atmpS2278;
      #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L14row__from__topS957
      = _M0MPC15float5Float7to__int(_M0L6_2atmpS2276);
      if (_M0L14row__from__topS957 < 0) {
        _M0L1rS958 = 0;
      } else if (_M0L14row__from__topS957 >= _M0L6heightS948) {
        _M0L1rS958 = _M0L6heightS948 - 1;
      } else {
        _M0L1rS958 = _M0L14row__from__topS957;
      }
      if (_M0L1wS955 >= 0x0p+0f) {
        _M0L2chS959 = 42;
      } else {
        _M0L2chS959 = 46;
      }
      #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8row__strS960
      = _M0MPC15array5Array2atGsE(_M0L6canvasS947, _M0L1rS958);
      _M0L3valS2274 = _M0L1cS954->$0;
      _M0L6_2atmpS2275 = Moonbit_array_length(_M0L9row__initS950);
      _M0L6_2atmpS2273 = _M0L3valS2274 + _M0L6_2atmpS2275;
      #line 332 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8new__rowS961
      = _M0FP26RiantR8snn__mbt19row__int__set__char(_M0L8row__strS960, _M0L6_2atmpS2273, _M0L2chS959);
      moonbit_decref_cycle_free(_M0L8row__strS960);
      #line 333 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGsE(_M0L6canvasS947, _M0L1rS958, _M0L8new__rowS961);
      _M0L3valS2272 = _M0L1cS954->$0;
      _M0L6_2atmpS2271 = _M0L3valS2272 + 1;
      _M0L1cS954->$0 = _M0L6_2atmpS2271;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1cS954);
      moonbit_decref_cycle_free(_M0L9row__initS950);
      moonbit_decref_cycle_free(_M0L4valsS939);
    }
    break;
  }
  #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_11.data);
  _M0L3valS2305 = _M0L2mxS938->$0;
  #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10max__labelS963
  = _M0FP26RiantR8snn__mbt27format__axis__label__kernel(_M0L3valS2305);
  _M0L3valS2303 = _M0L2mxS938->$0;
  moonbit_decref_cycle_free(_M0L2mxS938);
  _M0L3valS2304 = _M0L2mnS937->$0;
  _M0L6_2atmpS2302 = _M0L3valS2303 + _M0L3valS2304;
  _M0L6_2atmpS2301 = _M0L6_2atmpS2302 / 0x1p+1f;
  #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10mid__labelS964
  = _M0FP26RiantR8snn__mbt27format__axis__label__kernel(_M0L6_2atmpS2301);
  _M0L3valS2300 = _M0L2mnS937->$0;
  moonbit_decref_cycle_free(_M0L2mnS937);
  #line 340 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10min__labelS965
  = _M0FP26RiantR8snn__mbt27format__axis__label__kernel(_M0L3valS2300);
  _M0L1aS967 = Moonbit_array_length(_M0L10max__labelS963);
  _M0L1bS968 = Moonbit_array_length(_M0L10mid__labelS964);
  _M0L1cS969 = Moonbit_array_length(_M0L10min__labelS965);
  if (_M0L1aS967 > _M0L1bS968) {
    _M0L1mS970 = _M0L1aS967;
  } else {
    _M0L1mS970 = _M0L1bS968;
  }
  if (_M0L1mS970 > _M0L1cS969) {
    _M0L12label__widthS966 = _M0L1mS970;
  } else {
    _M0L12label__widthS966 = _M0L1cS969;
  }
  _M0L7_2abindS971 = 0;
  _M0L1rS972 = _M0L7_2abindS971;
  while (1) {
    if (_M0L1rS972 < _M0L6heightS948) {
      moonbit_string_t _M0L5labelS973;
      int32_t _M0L6_2atmpS2287;
      int32_t _M0L10pad__countS974;
      moonbit_string_t _M0L6paddedS975;
      moonbit_string_t _M0L6_2atmpS2286;
      moonbit_string_t _M0L6_2atmpS2284;
      moonbit_string_t _M0L6_2atmpS2285;
      moonbit_string_t _M0L6_2atmpS2283;
      int32_t _M0L6_2atmpS2291;
      if (_M0L1rS972 == 0) {
        moonbit_incref_cycle_free(_M0L10max__labelS963);
        _M0L5labelS973 = _M0L10max__labelS963;
      } else {
        int32_t _M0L6_2atmpS2289 = _M0L6heightS948 - 1;
        if (_M0L1rS972 == _M0L6_2atmpS2289) {
          moonbit_incref_cycle_free(_M0L10min__labelS965);
          _M0L5labelS973 = _M0L10min__labelS965;
        } else {
          int32_t _M0L6_2atmpS2290 = _M0L6heightS948 / 2;
          if (_M0L1rS972 == _M0L6_2atmpS2290) {
            moonbit_incref_cycle_free(_M0L10mid__labelS964);
            _M0L5labelS973 = _M0L10mid__labelS964;
          } else {
            _M0L5labelS973 = (moonbit_string_t)moonbit_string_literal_0.data;
          }
        }
      }
      _M0L6_2atmpS2287 = Moonbit_array_length(_M0L5labelS973);
      if (_M0L12label__widthS966 > _M0L6_2atmpS2287) {
        int32_t _M0L6_2atmpS2288 = Moonbit_array_length(_M0L5labelS973);
        _M0L10pad__countS974 = _M0L12label__widthS966 - _M0L6_2atmpS2288;
      } else {
        _M0L10pad__countS974 = 0;
      }
      #line 363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6paddedS975 = _M0MPC16string6String4make(_M0L10pad__countS974, 32);
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS2286 = moonbit_add_string(_M0L6paddedS975, _M0L5labelS973);
      moonbit_decref_cycle_free(_M0L5labelS973);
      moonbit_decref_cycle_free(_M0L6paddedS975);
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS2284
      = moonbit_add_string(_M0L6_2atmpS2286, (moonbit_string_t)moonbit_string_literal_12.data);
      moonbit_decref_cycle_free(_M0L6_2atmpS2286);
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS2285
      = _M0MPC15array5Array2atGsE(_M0L6canvasS947, _M0L1rS972);
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS2283
      = moonbit_add_string(_M0L6_2atmpS2284, _M0L6_2atmpS2285);
      moonbit_decref_cycle_free(_M0L6_2atmpS2285);
      moonbit_decref_cycle_free(_M0L6_2atmpS2284);
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0FPB7printlnGsE(_M0L6_2atmpS2283);
      moonbit_decref_cycle_free(_M0L6_2atmpS2283);
      _M0L6_2atmpS2291 = _M0L1rS972 + 1;
      _M0L1rS972 = _M0L6_2atmpS2291;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L10min__labelS965);
      moonbit_decref_cycle_free(_M0L10mid__labelS964);
      moonbit_decref_cycle_free(_M0L10max__labelS963);
      moonbit_decref_cycle_free(_M0L6canvasS947);
    }
    break;
  }
  _M0L6_2atmpS2299 = _M0L12label__widthS966 + 3;
  #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L8pad__strS977 = _M0MPC16string6String4make(_M0L6_2atmpS2299, 32);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS2297
  = moonbit_add_string(_M0L8pad__strS977, (moonbit_string_t)moonbit_string_literal_13.data);
  moonbit_decref_cycle_free(_M0L8pad__strS977);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS2298 = _M0IPC15float5FloatPB4Show10to__string(_M0L6t__maxS942);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS2296 = moonbit_add_string(_M0L6_2atmpS2297, _M0L6_2atmpS2298);
  moonbit_decref_cycle_free(_M0L6_2atmpS2298);
  moonbit_decref_cycle_free(_M0L6_2atmpS2297);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS2294
  = moonbit_add_string(_M0L6_2atmpS2296, (moonbit_string_t)moonbit_string_literal_14.data);
  moonbit_decref_cycle_free(_M0L6_2atmpS2296);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS2295 = _M0IPC15float5FloatPB4Show10to__string(_M0L6t__maxS942);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS2293 = moonbit_add_string(_M0L6_2atmpS2294, _M0L6_2atmpS2295);
  moonbit_decref_cycle_free(_M0L6_2atmpS2295);
  moonbit_decref_cycle_free(_M0L6_2atmpS2294);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS2292
  = moonbit_add_string(_M0L6_2atmpS2293, (moonbit_string_t)moonbit_string_literal_15.data);
  moonbit_decref_cycle_free(_M0L6_2atmpS2293);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2292);
  moonbit_decref_cycle_free(_M0L6_2atmpS2292);
  return 0;
}

moonbit_string_t _M0FP26RiantR8snn__mbt27format__axis__label__kernel(
  float _M0L1vS932
) {
  float _M0L6scaledS931;
  float _M0L6_2atmpS2244;
  int32_t _M0L7roundedS933;
  float _M0L6_2atmpS2243;
  float _M0L12scaled__backS934;
  #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6scaledS931 = _M0L1vS932 * 0x1.388p+13f;
  #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS2244 = _M0MPC15float5Float5round(_M0L6scaledS931);
  #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7roundedS933 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2244);
  _M0L6_2atmpS2243 = (float)_M0L7roundedS933;
  _M0L12scaled__backS934 = _M0L6_2atmpS2243 / 0x1.388p+13f;
  #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  return _M0IPC15float5FloatPB4Show10to__string(_M0L12scaled__backS934);
}

float _M0FP26RiantR8snn__mbt16gerstner__kernel(
  float _M0L2dtS924,
  float _M0L8tau__preS926,
  float _M0L9tau__postS929,
  float _M0L6a__preS927,
  float _M0L7a__postS930
) {
  #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  if (_M0L2dtS924 > 0x0p+0f) {
    float _M0L6_2atmpS2240 = -_M0L2dtS924;
    float _M0L3argS925 = _M0L6_2atmpS2240 / _M0L8tau__preS926;
    float _M0L6_2atmpS2239;
    #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS2239 = _M0FP26RiantR8snn__mbt4expf(_M0L3argS925);
    return _M0L6a__preS927 * _M0L6_2atmpS2239;
  } else if (_M0L2dtS924 < 0x0p+0f) {
    float _M0L3argS928 = _M0L2dtS924 / _M0L9tau__postS929;
    float _M0L6_2atmpS2241 = -_M0L7a__postS930;
    float _M0L6_2atmpS2242;
    #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS2242 = _M0FP26RiantR8snn__mbt4expf(_M0L3argS928);
    return _M0L6_2atmpS2241 * _M0L6_2atmpS2242;
  } else {
    return 0x0p+0f;
  }
}

struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0MP26RiantR8snn__mbt13STDPVariables3new(
  int32_t _M0L6n__preS922,
  int32_t _M0L7n__postS923
) {
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2233;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2234;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2235;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2236;
  uint8_t* _M0L6_2atmpS2238;
  struct _M0TPB5ArrayGbE* _M0L6_2atmpS2237;
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _block_2798;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS2233 = _M0MPC15array5Array4makeGfE(_M0L6n__preS922, 0x0p+0f);
  #line 58 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS2234 = _M0MPC15array5Array4makeGfE(_M0L7n__postS923, 0x0p+0f);
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS2235 = _M0MPC15array5Array4makeGfE(_M0L6n__preS922, 0x0p+0f);
  #line 60 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS2236 = _M0MPC15array5Array4makeGfE(_M0L7n__postS923, 0x0p+0f);
  _M0L6_2atmpS2238 = (uint8_t*)moonbit_make_bytes_raw(1);
  _M0L6_2atmpS2238[0] = 1;
  _M0L6_2atmpS2237
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_M0L6_2atmpS2237)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 59, 0);
  _M0L6_2atmpS2237->$0 = _M0L6_2atmpS2238;
  _M0L6_2atmpS2237->$1 = 1;
  _block_2798
  = (struct _M0TP26RiantR8snn__mbt13STDPVariables*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13STDPVariables));
  Moonbit_object_header(_block_2798)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 62, 0);
  _block_2798->$0 = _M0L6_2atmpS2233;
  _block_2798->$1 = _M0L6_2atmpS2234;
  _block_2798->$2 = _M0L6_2atmpS2235;
  _block_2798->$3 = _M0L6_2atmpS2236;
  _block_2798->$4 = _M0L6_2atmpS2237;
  return _block_2798;
}

struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0MP26RiantR8snn__mbt12STDPGerstner3new(
  
) {
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _block_2799;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _block_2799
  = (struct _M0TP26RiantR8snn__mbt12STDPGerstner*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt12STDPGerstner));
  Moonbit_object_header(_block_2799)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2799->$0 = 0x1.47ae147ae147bp-7f;
  _block_2799->$1 = 0x1.47ae147ae147bp-7f;
  _block_2799->$2 = 0x1.4p+4f;
  _block_2799->$3 = 0x1.4p+4f;
  _block_2799->$4 = 0x1.ep+4f;
  _block_2799->$5 = 0x0p+0f;
  return _block_2799;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS920
) {
  struct _M0TUmmmmE* _M0L1sS919;
  uint64_t _M0L6_2atmpS2232;
  struct _M0TUmmmmE* _M0L1tS921;
  uint64_t _M0L6_2atmpS2228;
  uint64_t _M0L6_2atmpS2229;
  uint64_t _M0L6_2atmpS2230;
  uint64_t _M0L6_2atmpS2231;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2800;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS919 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS920);
  _M0L6_2atmpS2232 = _M0L1sS919->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS921 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2232);
  _M0L6_2atmpS2228 = _M0L1sS919->$0;
  _M0L6_2atmpS2229 = _M0L1sS919->$1;
  _M0L6_2atmpS2230 = _M0L1sS919->$2;
  moonbit_decref_cycle_free(_M0L1sS919);
  _M0L6_2atmpS2231 = _M0L1tS921->$0;
  moonbit_decref_cycle_free(_M0L1tS921);
  _block_2800
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2800)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2800->$0 = _M0L6_2atmpS2228;
  _block_2800->$1 = _M0L6_2atmpS2229;
  _block_2800->$2 = _M0L6_2atmpS2230;
  _block_2800->$3 = _M0L6_2atmpS2231;
  return _block_2800;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS911) {
  uint64_t _M0L2s1S910;
  uint64_t _M0L2z1S912;
  uint64_t _M0L2s2S913;
  uint64_t _M0L2z2S914;
  uint64_t _M0L2s3S915;
  uint64_t _M0L2z3S916;
  uint64_t _M0L2s4S917;
  uint64_t _M0L2z4S918;
  struct _M0TUmmmmE* _block_2801;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S910 = _M0L4seedS911 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S912 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S910);
  _M0L2s2S913 = _M0L2s1S910 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S914 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S913);
  _M0L2s3S915 = _M0L2s2S913 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S916 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S915);
  _M0L2s4S917 = _M0L2s3S915 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S918 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S917);
  _block_2801 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2801)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2801->$0 = _M0L2z1S912;
  _block_2801->$1 = _M0L2z2S914;
  _block_2801->$2 = _M0L2z3S916;
  _block_2801->$3 = _M0L2z4S918;
  return _block_2801;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS908) {
  uint64_t _M0L6_2atmpS2227;
  uint64_t _M0L6_2atmpS2226;
  uint64_t _M0L1zS907;
  uint64_t _M0L6_2atmpS2225;
  uint64_t _M0L6_2atmpS2224;
  uint64_t _M0L1zS909;
  uint64_t _M0L6_2atmpS2223;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2227 = _M0L1zS908 >> 30;
  _M0L6_2atmpS2226 = _M0L1zS908 ^ _M0L6_2atmpS2227;
  _M0L1zS907 = _M0L6_2atmpS2226 * 13787848793156543929ull;
  _M0L6_2atmpS2225 = _M0L1zS907 >> 27;
  _M0L6_2atmpS2224 = _M0L1zS907 ^ _M0L6_2atmpS2225;
  _M0L1zS909 = _M0L6_2atmpS2224 * 10723151780598845931ull;
  _M0L6_2atmpS2223 = _M0L1zS909 >> 31;
  return _M0L1zS909 ^ _M0L6_2atmpS2223;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS905
) {
  uint32_t _M0L1uS904;
  uint32_t _M0L4bitsS906;
  double _M0L6_2atmpS2222;
  double _M0L6_2atmpS2221;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS904 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS905);
  _M0L4bitsS906 = _M0L1uS904 >> 8;
  _M0L6_2atmpS2222 = (double)_M0L4bitsS906;
  _M0L6_2atmpS2221 = _M0L6_2atmpS2222 * 0x1p-24;
  return (float)_M0L6_2atmpS2221;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS903
) {
  uint64_t _M0L1uS902;
  uint64_t _M0L6_2atmpS2220;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS902 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS903);
  _M0L6_2atmpS2220 = _M0L1uS902 >> 32;
  return (uint32_t)_M0L6_2atmpS2220;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS895
) {
  uint64_t _M0L2s0S894;
  uint64_t _M0L2s1S896;
  uint64_t _M0L2s2S897;
  uint64_t _M0L2s3S898;
  uint64_t _M0L3tmpS899;
  uint64_t _M0L6_2atmpS2219;
  uint64_t _M0L3resS900;
  uint64_t _M0L1tS901;
  uint64_t _M0L6_2atmpS2209;
  uint64_t _M0L6_2atmpS2210;
  uint64_t _M0L2s2S2212;
  uint64_t _M0L6_2atmpS2211;
  uint64_t _M0L2s3S2214;
  uint64_t _M0L6_2atmpS2213;
  uint64_t _M0L2s2S2216;
  uint64_t _M0L6_2atmpS2215;
  uint64_t _M0L2s3S2218;
  uint64_t _M0L6_2atmpS2217;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S894 = _M0L1rS895->$0;
  _M0L2s1S896 = _M0L1rS895->$1;
  _M0L2s2S897 = _M0L1rS895->$2;
  _M0L2s3S898 = _M0L1rS895->$3;
  _M0L3tmpS899 = _M0L2s0S894 + _M0L2s3S898;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2219 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS899, 23);
  _M0L3resS900 = _M0L6_2atmpS2219 + _M0L2s0S894;
  _M0L1tS901 = _M0L2s1S896 << 17;
  _M0L6_2atmpS2209 = _M0L2s2S897 ^ _M0L2s0S894;
  _M0L1rS895->$2 = _M0L6_2atmpS2209;
  _M0L6_2atmpS2210 = _M0L2s3S898 ^ _M0L2s1S896;
  _M0L1rS895->$3 = _M0L6_2atmpS2210;
  _M0L2s2S2212 = _M0L1rS895->$2;
  _M0L6_2atmpS2211 = _M0L2s1S896 ^ _M0L2s2S2212;
  _M0L1rS895->$1 = _M0L6_2atmpS2211;
  _M0L2s3S2214 = _M0L1rS895->$3;
  _M0L6_2atmpS2213 = _M0L2s0S894 ^ _M0L2s3S2214;
  _M0L1rS895->$0 = _M0L6_2atmpS2213;
  _M0L2s2S2216 = _M0L1rS895->$2;
  _M0L6_2atmpS2215 = _M0L2s2S2216 ^ _M0L1tS901;
  _M0L1rS895->$2 = _M0L6_2atmpS2215;
  _M0L2s3S2218 = _M0L1rS895->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2217 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S2218, 45);
  _M0L1rS895->$3 = _M0L6_2atmpS2217;
  return _M0L3resS900;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS892, int32_t _M0L1kS893) {
  uint64_t _M0L6_2atmpS2206;
  int32_t _M0L6_2atmpS2208;
  uint64_t _M0L6_2atmpS2207;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2206 = _M0L1xS892 << (_M0L1kS893 & 63);
  _M0L6_2atmpS2208 = 64 - _M0L1kS893;
  _M0L6_2atmpS2207 = _M0L1xS892 >> (_M0L6_2atmpS2208 & 63);
  return _M0L6_2atmpS2206 | _M0L6_2atmpS2207;
}

moonbit_string_t _M0FP26RiantR8snn__mbt19row__int__set__char(
  moonbit_string_t _M0L1sS882,
  int32_t _M0L3posS885,
  int32_t _M0L2chS889
) {
  int32_t _M0L6_2atmpS2205;
  struct _M0TPB13StringBuilder* _M0L2sbS881;
  int32_t _M0L3lenS883;
  int32_t _M0L11prefix__endS884;
  int32_t _M0L6_2atmpS2197;
  moonbit_string_t _result_2804;
  #line 353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS2205 = Moonbit_array_length(_M0L1sS882);
  #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L2sbS881
  = _M0MPB13StringBuilder21StringBuilder_2einner(_M0L6_2atmpS2205);
  _M0L3lenS883 = Moonbit_array_length(_M0L1sS882);
  if (_M0L3posS885 < _M0L3lenS883) {
    _M0L11prefix__endS884 = _M0L3posS885;
  } else {
    _M0L11prefix__endS884 = _M0L3lenS883;
  }
  if (_M0L11prefix__endS884 > 0) {
    moonbit_string_t _M0L6prefixS886;
    struct _M0TPB8MutLocalGiE* _M0L1kS887;
    #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0L6prefixS886 = _M0MPC16string6String4make(_M0L11prefix__endS884, 32);
    _M0L1kS887
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS887)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS887->$0 = 0;
    while (1) {
      int32_t _M0L3valS2191 = _M0L1kS887->$0;
      if (_M0L3valS2191 < _M0L11prefix__endS884) {
        int32_t _M0L3valS2194 = _M0L1kS887->$0;
        int32_t _M0L6_2atmpS2193;
        int32_t _M0L6_2atmpS2192;
        int32_t _M0L3valS2196;
        int32_t _M0L6_2atmpS2195;
        if (
          _M0L3valS2194 < 0
          || _M0L3valS2194 >= Moonbit_array_length(_M0L1sS882)
        ) {
          #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2193 = _M0L1sS882[_M0L3valS2194];
        #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
        _M0L6_2atmpS2192
        = _M0MPC16uint166UInt1616unsafe__to__char(_M0L6_2atmpS2193);
        #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
        _M0IPB13StringBuilderPB6Logger11write__char(_M0L2sbS881, _M0L6_2atmpS2192);
        _M0L3valS2196 = _M0L1kS887->$0;
        _M0L6_2atmpS2195 = _M0L3valS2196 + 1;
        _M0L1kS887->$0 = _M0L6_2atmpS2195;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS887);
      }
      break;
    }
    moonbit_decref_cycle_free(_M0L6prefixS886);
  }
  #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L2sbS881, _M0L2chS889);
  _M0L6_2atmpS2197 = _M0L3posS885 + 1;
  if (_M0L6_2atmpS2197 < _M0L3lenS883) {
    int32_t _M0L6_2atmpS2204 = _M0L3posS885 + 1;
    struct _M0TPB8MutLocalGiE* _M0L1kS890 =
      (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS890)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS890->$0 = _M0L6_2atmpS2204;
    while (1) {
      int32_t _M0L3valS2198 = _M0L1kS890->$0;
      if (_M0L3valS2198 < _M0L3lenS883) {
        int32_t _M0L3valS2201 = _M0L1kS890->$0;
        int32_t _M0L6_2atmpS2200;
        int32_t _M0L6_2atmpS2199;
        int32_t _M0L3valS2203;
        int32_t _M0L6_2atmpS2202;
        if (
          _M0L3valS2201 < 0
          || _M0L3valS2201 >= Moonbit_array_length(_M0L1sS882)
        ) {
          #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2200 = _M0L1sS882[_M0L3valS2201];
        #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
        _M0L6_2atmpS2199
        = _M0MPC16uint166UInt1616unsafe__to__char(_M0L6_2atmpS2200);
        #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
        _M0IPB13StringBuilderPB6Logger11write__char(_M0L2sbS881, _M0L6_2atmpS2199);
        _M0L3valS2203 = _M0L1kS890->$0;
        _M0L6_2atmpS2202 = _M0L3valS2203 + 1;
        _M0L1kS890->$0 = _M0L6_2atmpS2202;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS890);
      }
      break;
    }
  }
  #line 380 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _result_2804 = _M0MPB13StringBuilder10to__string(_M0L2sbS881);
  moonbit_decref_cycle_free(_M0L2sbS881);
  return _result_2804;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS880) {
  double _M0L6_2atmpS2190;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2190 = (double)_M0L4selfS880;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2190);
}

float _M0MPC15float5Float5round(float _M0L4selfS879) {
  float _M0L6_2atmpS2189;
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\float\\round.mbt"
  _M0L6_2atmpS2189 = _M0L4selfS879 + 0x1p-1f;
  #line 145 "C:\\Users\\31379\\.moon\\lib\\core\\float\\round.mbt"
  return _M0MPC15float5Float5floor(_M0L6_2atmpS2189);
}

float _M0MPC15float5Float5floor(float _M0L4selfS878) {
  float _M0L7truncedS877;
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\float\\round.mbt"
  #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\float\\round.mbt"
  _M0L7truncedS877 = _M0MPC15float5Float5trunc(_M0L4selfS878);
  if (_M0L4selfS878 < _M0L7truncedS877) {
    return _M0L7truncedS877 - 0x1p+0f;
  } else {
    return _M0L7truncedS877;
  }
}

float _M0MPC15float5Float5trunc(float _M0L4selfS873) {
  uint32_t _M0L3u32S872;
  uint32_t _M0L6_2atmpS2188;
  uint32_t _M0L6_2atmpS2187;
  int32_t _M0L11biased__expS874;
  int32_t _M0L6_2atmpS2186;
  int32_t _M0L11mask__shiftS875;
  uint32_t _tmp_2805;
  int32_t _M0L6_2atmpS2185;
  int32_t _M0L6_2atmpS2184;
  uint32_t _M0L11trunc__maskS876;
  uint32_t _M0L6_2atmpS2183;
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\float\\round.mbt"
  _M0L3u32S872 = *(int32_t*)&_M0L4selfS873;
  _M0L6_2atmpS2188 = _M0L3u32S872 >> 23;
  _M0L6_2atmpS2187 = _M0L6_2atmpS2188 & 255u;
  _M0L11biased__expS874 = *(int32_t*)&_M0L6_2atmpS2187;
  if (_M0L11biased__expS874 < 127) {
    uint32_t _M0L6_2atmpS2182 = _M0L3u32S872 & 2147483648u;
    return *(float*)&_M0L6_2atmpS2182;
  } else if (_M0L11biased__expS874 >= 150) {
    return _M0L4selfS873;
  }
  _M0L6_2atmpS2186 = _M0L11biased__expS874 - 127;
  _M0L11mask__shiftS875 = _M0L6_2atmpS2186 + 8;
  _tmp_2805 = 2147483648u;
  _M0L6_2atmpS2185 = *(int32_t*)&_tmp_2805;
  _M0L6_2atmpS2184 = _M0L6_2atmpS2185 >> (_M0L11mask__shiftS875 & 31);
  _M0L11trunc__maskS876 = *(uint32_t*)&_M0L6_2atmpS2184;
  _M0L6_2atmpS2183 = _M0L3u32S872 & _M0L11trunc__maskS876;
  return *(float*)&_M0L6_2atmpS2183;
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS871) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS871 != _M0L4selfS871) {
    return 0;
  } else if (_M0L4selfS871 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS871 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS871;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS852,
  float _M0L4elemS854
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS851;
  int32_t _M0L1iS853;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS851 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS852);
  _M0L1iS853 = 0;
  while (1) {
    if (_M0L1iS853 < _M0L3lenS852) {
      float* _M0L3bufS2174 = _M0L3arrS851->$0;
      int32_t _M0L6_2atmpS2175;
      _M0L3bufS2174[_M0L1iS853] = _M0L4elemS854;
      _M0L6_2atmpS2175 = _M0L1iS853 + 1;
      _M0L1iS853 = _M0L6_2atmpS2175;
      continue;
    }
    break;
  }
  return _M0L3arrS851;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS857,
  int32_t _M0L4elemS859
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS856;
  int32_t _M0L1iS858;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS856 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS857);
  _M0L1iS858 = 0;
  while (1) {
    if (_M0L1iS858 < _M0L3lenS857) {
      uint8_t* _M0L3bufS2176 = _M0L3arrS856->$0;
      int32_t _M0L6_2atmpS2177;
      _M0L3bufS2176[_M0L1iS858] = _M0L4elemS859;
      _M0L6_2atmpS2177 = _M0L1iS858 + 1;
      _M0L1iS858 = _M0L6_2atmpS2177;
      continue;
    }
    break;
  }
  return _M0L3arrS856;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS862,
  int32_t _M0L4elemS864
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS861;
  int32_t _M0L1iS863;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS861 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS862);
  _M0L1iS863 = 0;
  while (1) {
    if (_M0L1iS863 < _M0L3lenS862) {
      int32_t* _M0L3bufS2178 = _M0L3arrS861->$0;
      int32_t _M0L6_2atmpS2179;
      _M0L3bufS2178[_M0L1iS863] = _M0L4elemS864;
      _M0L6_2atmpS2179 = _M0L1iS863 + 1;
      _M0L1iS863 = _M0L6_2atmpS2179;
      continue;
    }
    break;
  }
  return _M0L3arrS861;
}

struct _M0TPB5ArrayGsE* _M0MPC15array5Array4makeGsE(
  int32_t _M0L3lenS867,
  moonbit_string_t _M0L4elemS869
) {
  struct _M0TPB5ArrayGsE* _M0L3arrS866;
  int32_t _M0L1iS868;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS866 = _M0MPC15array5Array20unsafe__make__uninitGsE(_M0L3lenS867);
  _M0L1iS868 = 0;
  while (1) {
    if (_M0L1iS868 < _M0L3lenS867) {
      moonbit_string_t* _M0L3bufS2180 = _M0L3arrS866->$0;
      moonbit_string_t _M0L6_2aoldS2717 =
        (moonbit_string_t)_M0L3bufS2180[_M0L1iS868];
      int32_t _M0L6_2atmpS2181;
      moonbit_incref_cycle_free(_M0L4elemS869);
      moonbit_decref_cycle_free(_M0L6_2aoldS2717);
      _M0L3bufS2180[_M0L1iS868] = _M0L4elemS869;
      _M0L6_2atmpS2181 = _M0L1iS868 + 1;
      _M0L1iS868 = _M0L6_2atmpS2181;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS869);
    }
    break;
  }
  return _M0L3arrS866;
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
    float* _M0L6_2atmpS2170;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2170 = _M0MPC15array5Array6bufferGfE(_M0L4selfS836);
    _M0L6_2atmpS2170[_M0L5indexS837] = _M0L5valueS838;
    moonbit_decref_cycle_free(_M0L6_2atmpS2170);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS840,
  int32_t _M0L5indexS841,
  int32_t _M0L5valueS842
) {
  int32_t _M0L3lenS839;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS839 = _M0L4selfS840->$1;
  if (_M0L5indexS841 >= 0 && _M0L5indexS841 < _M0L3lenS839) {
    uint8_t* _M0L6_2atmpS2171;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2171 = _M0MPC15array5Array6bufferGbE(_M0L4selfS840);
    _M0L6_2atmpS2171[_M0L5indexS841] = _M0L5valueS842;
    moonbit_decref_cycle_free(_M0L6_2atmpS2171);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS844,
  int32_t _M0L5indexS845,
  int32_t _M0L5valueS846
) {
  int32_t _M0L3lenS843;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS843 = _M0L4selfS844->$1;
  if (_M0L5indexS845 >= 0 && _M0L5indexS845 < _M0L3lenS843) {
    int32_t* _M0L6_2atmpS2172;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2172 = _M0MPC15array5Array6bufferGiE(_M0L4selfS844);
    _M0L6_2atmpS2172[_M0L5indexS845] = _M0L5valueS846;
    moonbit_decref_cycle_free(_M0L6_2atmpS2172);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS848,
  int32_t _M0L5indexS849,
  moonbit_string_t _M0L5valueS850
) {
  int32_t _M0L3lenS847;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS847 = _M0L4selfS848->$1;
  if (_M0L5indexS849 >= 0 && _M0L5indexS849 < _M0L3lenS847) {
    moonbit_string_t* _M0L6_2atmpS2173;
    moonbit_string_t _M0L6_2aoldS2718;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2173 = _M0MPC15array5Array6bufferGsE(_M0L4selfS848);
    _M0L6_2aoldS2718 = (moonbit_string_t)_M0L6_2atmpS2173[_M0L5indexS849];
    moonbit_decref_cycle_free(_M0L6_2aoldS2718);
    _M0L6_2atmpS2173[_M0L5indexS849] = _M0L5valueS850;
    moonbit_decref_cycle_free(_M0L6_2atmpS2173);
  } else {
    moonbit_decref_cycle_free(_M0L5valueS850);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4copyGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS833
) {
  int32_t _M0L3lenS832;
  struct _M0TPB5ArrayGfE* _M0L3arrS834;
  #line 842 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS832 = _M0L4selfS833->$1;
  if (_M0L3lenS832 == 0) {
    float* _M0L6_2atmpS2169 = moonbit_empty_float_array;
    struct _M0TPB5ArrayGfE* _block_2810 =
      (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
    Moonbit_object_header(_block_2810)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
    _block_2810->$0 = _M0L6_2atmpS2169;
    _block_2810->$1 = 0;
    return _block_2810;
  }
  #line 848 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3arrS834 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS832);
  #line 849 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array12unsafe__blitGfE(_M0L3arrS834, 0, _M0L4selfS833, 0, _M0L3lenS832);
  return _M0L3arrS834;
}

int32_t _M0MPC15array5Array12unsafe__blitGfE(
  struct _M0TPB5ArrayGfE* _M0L3dstS827,
  int32_t _M0L11dst__offsetS828,
  struct _M0TPB5ArrayGfE* _M0L3srcS829,
  int32_t _M0L11src__offsetS830,
  int32_t _M0L3lenS831
) {
  float* _M0L6_2atmpS2167;
  float* _M0L6_2atmpS2168;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  _M0L6_2atmpS2167 = _M0MPC15array5Array6bufferGfE(_M0L3dstS827);
  #line 60 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  _M0L6_2atmpS2168 = _M0MPC15array5Array6bufferGfE(_M0L3srcS829);
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L6_2atmpS2167, _M0L11dst__offsetS828, _M0L6_2atmpS2168, _M0L11src__offsetS830, _M0L3lenS831, sizeof(float));
  return 0;
}

int32_t _M0MPC15array5Array6insertGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS818,
  int32_t _M0L5indexS817,
  int32_t _M0L5valueS820
) {
  int32_t _if__result_2811;
  #line 738 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L5indexS817 >= 0) {
    int32_t _M0L3lenS2137 = _M0L4selfS818->$1;
    _if__result_2811 = _M0L5indexS817 <= _M0L3lenS2137;
  } else {
    _if__result_2811 = 0;
  }
  if (_if__result_2811) {
    int32_t _M0L3lenS2138 = _M0L4selfS818->$1;
    int32_t* _M0L6_2atmpS2140;
    int32_t _M0L6_2atmpS2139;
    int32_t* _M0L6_2atmpS2143;
    int32_t _M0L6_2atmpS2144;
    int32_t* _M0L6_2atmpS2145;
    int32_t _M0L3lenS2147;
    int32_t _M0L6_2atmpS2146;
    int32_t _M0L6lengthS819;
    int32_t* _M0L3bufS2148;
    int32_t _M0L6_2atmpS2149;
    #line 745 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2140 = _M0MPC15array5Array6bufferGiE(_M0L4selfS818);
    _M0L6_2atmpS2139 = Moonbit_array_length(_M0L6_2atmpS2140);
    moonbit_decref_cycle_free(_M0L6_2atmpS2140);
    if (_M0L3lenS2138 == _M0L6_2atmpS2139) {
      int32_t _M0L3lenS2142 = _M0L4selfS818->$1;
      int32_t _M0L6_2atmpS2141 = _M0L3lenS2142 + 1;
      #line 746 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
      _M0MPC15array5Array7reallocGiE(_M0L4selfS818, _M0L6_2atmpS2141);
    }
    #line 749 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2143 = _M0MPC15array5Array6bufferGiE(_M0L4selfS818);
    _M0L6_2atmpS2144 = _M0L5indexS817 + 1;
    #line 751 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2145 = _M0MPC15array5Array6bufferGiE(_M0L4selfS818);
    _M0L3lenS2147 = _M0L4selfS818->$1;
    _M0L6_2atmpS2146 = _M0L3lenS2147 - _M0L5indexS817;
    #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L6_2atmpS2143, _M0L6_2atmpS2144, _M0L6_2atmpS2145, _M0L5indexS817, _M0L6_2atmpS2146);
    moonbit_decref_cycle_free(_M0L6_2atmpS2143);
    moonbit_decref_cycle_free(_M0L6_2atmpS2145);
    _M0L6lengthS819 = _M0L4selfS818->$1;
    _M0L3bufS2148 = _M0L4selfS818->$0;
    _M0L3bufS2148[_M0L5indexS817] = _M0L5valueS820;
    _M0L6_2atmpS2149 = _M0L6lengthS819 + 1;
    _M0L4selfS818->$1 = _M0L6_2atmpS2149;
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS821;
    int32_t _M0L3lenS2151;
    moonbit_string_t _M0L6_2atmpS2150;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L18_2astring__builderS821
    = _M0MPB13StringBuilder21StringBuilder_2einner(60);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS821, (moonbit_string_t)moonbit_string_literal_16.data);
    _M0L3lenS2151 = _M0L4selfS818->$1;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS821, _M0L3lenS2151);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS821, (moonbit_string_t)moonbit_string_literal_17.data);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS821, _M0L5indexS817);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2150
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS821);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS821);
    #line 741 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE(_M0L6_2atmpS2150);
    moonbit_decref_cycle_free(_M0L6_2atmpS2150);
  }
  return 0;
}

int32_t _M0MPC15array5Array6insertGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS823,
  int32_t _M0L5indexS822,
  float _M0L5valueS825
) {
  int32_t _if__result_2812;
  #line 738 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L5indexS822 >= 0) {
    int32_t _M0L3lenS2152 = _M0L4selfS823->$1;
    _if__result_2812 = _M0L5indexS822 <= _M0L3lenS2152;
  } else {
    _if__result_2812 = 0;
  }
  if (_if__result_2812) {
    int32_t _M0L3lenS2153 = _M0L4selfS823->$1;
    float* _M0L6_2atmpS2155;
    int32_t _M0L6_2atmpS2154;
    float* _M0L6_2atmpS2158;
    int32_t _M0L6_2atmpS2159;
    float* _M0L6_2atmpS2160;
    int32_t _M0L3lenS2162;
    int32_t _M0L6_2atmpS2161;
    int32_t _M0L6lengthS824;
    float* _M0L3bufS2163;
    int32_t _M0L6_2atmpS2164;
    #line 745 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2155 = _M0MPC15array5Array6bufferGfE(_M0L4selfS823);
    _M0L6_2atmpS2154 = Moonbit_array_length(_M0L6_2atmpS2155);
    moonbit_decref_cycle_free(_M0L6_2atmpS2155);
    if (_M0L3lenS2153 == _M0L6_2atmpS2154) {
      int32_t _M0L3lenS2157 = _M0L4selfS823->$1;
      int32_t _M0L6_2atmpS2156 = _M0L3lenS2157 + 1;
      #line 746 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
      _M0MPC15array5Array7reallocGfE(_M0L4selfS823, _M0L6_2atmpS2156);
    }
    #line 749 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2158 = _M0MPC15array5Array6bufferGfE(_M0L4selfS823);
    _M0L6_2atmpS2159 = _M0L5indexS822 + 1;
    #line 751 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2160 = _M0MPC15array5Array6bufferGfE(_M0L4selfS823);
    _M0L3lenS2162 = _M0L4selfS823->$1;
    _M0L6_2atmpS2161 = _M0L3lenS2162 - _M0L5indexS822;
    #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L6_2atmpS2158, _M0L6_2atmpS2159, _M0L6_2atmpS2160, _M0L5indexS822, _M0L6_2atmpS2161);
    moonbit_decref_cycle_free(_M0L6_2atmpS2158);
    moonbit_decref_cycle_free(_M0L6_2atmpS2160);
    _M0L6lengthS824 = _M0L4selfS823->$1;
    _M0L3bufS2163 = _M0L4selfS823->$0;
    _M0L3bufS2163[_M0L5indexS822] = _M0L5valueS825;
    _M0L6_2atmpS2164 = _M0L6lengthS824 + 1;
    _M0L4selfS823->$1 = _M0L6_2atmpS2164;
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS826;
    int32_t _M0L3lenS2166;
    moonbit_string_t _M0L6_2atmpS2165;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L18_2astring__builderS826
    = _M0MPB13StringBuilder21StringBuilder_2einner(60);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS826, (moonbit_string_t)moonbit_string_literal_16.data);
    _M0L3lenS2166 = _M0L4selfS823->$1;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS826, _M0L3lenS2166);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS826, (moonbit_string_t)moonbit_string_literal_17.data);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS826, _M0L5indexS822);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS2165
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS826);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS826);
    #line 741 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE(_M0L6_2atmpS2165);
    moonbit_decref_cycle_free(_M0L6_2atmpS2165);
  }
  return 0;
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS806,
  int32_t _M0L5indexS807
) {
  int32_t _M0L3lenS805;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS805 = _M0L4selfS806->$1;
  if (_M0L5indexS807 >= 0 && _M0L5indexS807 < _M0L3lenS805) {
    float* _M0L6_2atmpS2133;
    float _result_2813;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2133 = _M0MPC15array5Array6bufferGfE(_M0L4selfS806);
    _result_2813 = (float)_M0L6_2atmpS2133[_M0L5indexS807];
    moonbit_decref_cycle_free(_M0L6_2atmpS2133);
    return _result_2813;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS809,
  int32_t _M0L5indexS810
) {
  int32_t _M0L3lenS808;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS808 = _M0L4selfS809->$1;
  if (_M0L5indexS810 >= 0 && _M0L5indexS810 < _M0L3lenS808) {
    int32_t* _M0L6_2atmpS2134;
    int32_t _result_2814;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2134 = _M0MPC15array5Array6bufferGiE(_M0L4selfS809);
    _result_2814 = (int32_t)_M0L6_2atmpS2134[_M0L5indexS810];
    moonbit_decref_cycle_free(_M0L6_2atmpS2134);
    return _result_2814;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS812,
  int32_t _M0L5indexS813
) {
  int32_t _M0L3lenS811;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS811 = _M0L4selfS812->$1;
  if (_M0L5indexS813 >= 0 && _M0L5indexS813 < _M0L3lenS811) {
    uint8_t* _M0L6_2atmpS2135;
    int32_t _result_2815;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2135 = _M0MPC15array5Array6bufferGbE(_M0L4selfS812);
    _result_2815 = (int32_t)_M0L6_2atmpS2135[_M0L5indexS813];
    moonbit_decref_cycle_free(_M0L6_2atmpS2135);
    return _result_2815;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS815,
  int32_t _M0L5indexS816
) {
  int32_t _M0L3lenS814;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS814 = _M0L4selfS815->$1;
  if (_M0L5indexS816 >= 0 && _M0L5indexS816 < _M0L3lenS814) {
    moonbit_string_t* _M0L6_2atmpS2136;
    moonbit_string_t _M0L6_2atmpS2719;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2136 = _M0MPC15array5Array6bufferGsE(_M0L4selfS815);
    _M0L6_2atmpS2719 = (moonbit_string_t)_M0L6_2atmpS2136[_M0L5indexS816];
    moonbit_incref_cycle_free(_M0L6_2atmpS2719);
    moonbit_decref_cycle_free(_M0L6_2atmpS2136);
    return _M0L6_2atmpS2719;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS804) {
  moonbit_string_t _M0L6_2atmpS2132;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2132 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS804);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2132);
  moonbit_decref_cycle_free(_M0L6_2atmpS2132);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS803) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS803);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS788) {
  uint64_t _M0L4bitsS791;
  uint64_t _M0L6_2atmpS2131;
  uint64_t _M0L6_2atmpS2130;
  int32_t _M0L8ieeeSignS792;
  uint64_t _M0L12ieeeMantissaS793;
  uint64_t _M0L6_2atmpS2129;
  uint64_t _M0L6_2atmpS2128;
  int32_t _M0L12ieeeExponentS794;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS795;
  struct _M0TPB17FloatingDecimal64* _M0L1vS796;
  moonbit_string_t _result_2817;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS788 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_18.data;
  }
  if (_M0L3valS788 >= -0x1p+53 && _M0L3valS788 <= 0x1p+53) {
    if (_M0L3valS788 >= -0x1p+31 && _M0L3valS788 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS789;
      double _M0L6_2atmpS2117;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS789 = _M0MPC16double6Double7to__int(_M0L3valS788);
      _M0L6_2atmpS2117 = (double)_M0L1iS789;
      if (_M0L6_2atmpS2117 == _M0L3valS788) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS789, 10);
      }
    } else {
      int64_t _M0L1iS790;
      double _M0L6_2atmpS2118;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS790 = _M0MPC16double6Double9to__int64(_M0L3valS788);
      _M0L6_2atmpS2118 = (double)_M0L1iS790;
      if (_M0L6_2atmpS2118 == _M0L3valS788) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS790, 10);
      }
    }
  }
  _M0L4bitsS791 = *(int64_t*)&_M0L3valS788;
  _M0L6_2atmpS2131 = _M0L4bitsS791 >> 63;
  _M0L6_2atmpS2130 = _M0L6_2atmpS2131 & 1ull;
  _M0L8ieeeSignS792 = _M0L6_2atmpS2130 != 0ull;
  _M0L12ieeeMantissaS793 = _M0L4bitsS791 & 4503599627370495ull;
  _M0L6_2atmpS2129 = _M0L4bitsS791 >> 52;
  _M0L6_2atmpS2128 = _M0L6_2atmpS2129 & 2047ull;
  _M0L12ieeeExponentS794 = (int32_t)_M0L6_2atmpS2128;
  if (
    _M0L12ieeeExponentS794 == 2047
    || _M0L12ieeeExponentS794 == 0 && _M0L12ieeeMantissaS793 == 0ull
  ) {
    int32_t _M0L6_2atmpS2119 = _M0L12ieeeExponentS794 != 0;
    int32_t _M0L6_2atmpS2120 = _M0L12ieeeMantissaS793 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS792, _M0L6_2atmpS2119, _M0L6_2atmpS2120);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS795
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS793, _M0L12ieeeExponentS794);
  if (_M0L7_2abindS795 == 0) {
    uint32_t _M0L6_2atmpS2121;
    if (_M0L7_2abindS795) {
      moonbit_decref_cycle_free(_M0L7_2abindS795);
    }
    _M0L6_2atmpS2121 = *(uint32_t*)&_M0L12ieeeExponentS794;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS796 = _M0FPB3d2d(_M0L12ieeeMantissaS793, _M0L6_2atmpS2121);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS797 = _M0L7_2abindS795;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS798 = _M0L7_2aSomeS797;
    struct _M0TPB17FloatingDecimal64* _M0L1xS799 = _M0L4_2afS798;
    while (1) {
      uint64_t _M0L8mantissaS2127 = _M0L1xS799->$0;
      uint64_t _M0L1qS800 = _M0L8mantissaS2127 / 10ull;
      uint64_t _M0L8mantissaS2125 = _M0L1xS799->$0;
      uint64_t _M0L6_2atmpS2126 = 10ull * _M0L1qS800;
      uint64_t _M0L1rS801 = _M0L8mantissaS2125 - _M0L6_2atmpS2126;
      int32_t _M0L8exponentS2124;
      int32_t _M0L6_2atmpS2123;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2122;
      if (_M0L1rS801 != 0ull) {
        _M0L1vS796 = _M0L1xS799;
        break;
      }
      _M0L8exponentS2124 = _M0L1xS799->$1;
      moonbit_decref_cycle_free(_M0L1xS799);
      _M0L6_2atmpS2123 = _M0L8exponentS2124 + 1;
      _M0L6_2atmpS2122
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS2122)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS2122->$0 = _M0L1qS800;
      _M0L6_2atmpS2122->$1 = _M0L6_2atmpS2123;
      _M0L1xS799 = _M0L6_2atmpS2122;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2817 = _M0FPB9to__chars(_M0L1vS796, _M0L8ieeeSignS792);
  moonbit_decref_cycle_free(_M0L1vS796);
  return _result_2817;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS783,
  int32_t _M0L12ieeeExponentS785
) {
  uint64_t _M0L2m2S782;
  int32_t _M0L6_2atmpS2116;
  int32_t _M0L2e2S784;
  int32_t _M0L6_2atmpS2115;
  uint64_t _M0L6_2atmpS2114;
  uint64_t _M0L4maskS786;
  uint64_t _M0L8fractionS787;
  int32_t _M0L6_2atmpS2113;
  uint64_t _M0L6_2atmpS2112;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2111;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S782 = 4503599627370496ull | _M0L12ieeeMantissaS783;
  _M0L6_2atmpS2116 = _M0L12ieeeExponentS785 - 1023;
  _M0L2e2S784 = _M0L6_2atmpS2116 - 52;
  if (_M0L2e2S784 > 0) {
    return 0;
  }
  if (_M0L2e2S784 < -52) {
    return 0;
  }
  _M0L6_2atmpS2115 = -_M0L2e2S784;
  _M0L6_2atmpS2114 = 1ull << (_M0L6_2atmpS2115 & 63);
  _M0L4maskS786 = _M0L6_2atmpS2114 - 1ull;
  _M0L8fractionS787 = _M0L2m2S782 & _M0L4maskS786;
  if (_M0L8fractionS787 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2113 = -_M0L2e2S784;
  _M0L6_2atmpS2112 = _M0L2m2S782 >> (_M0L6_2atmpS2113 & 63);
  _M0L6_2atmpS2111
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS2111)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS2111->$0 = _M0L6_2atmpS2112;
  _M0L6_2atmpS2111->$1 = 0;
  return _M0L6_2atmpS2111;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS750,
  int32_t _M0L4signS748
) {
  moonbit_bytes_t _M0L6resultS746;
  int32_t _M0Lm5indexS747;
  uint64_t _M0L6outputS749;
  int32_t _M0L7olengthS751;
  int32_t _M0L8exponentS2110;
  int32_t _M0L6_2atmpS2109;
  int32_t _M0Lm3expS752;
  int32_t _M0L6_2atmpS2108;
  int32_t _M0L6_2atmpS2106;
  int32_t _M0L18scientificNotationS753;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS746 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS747 = 0;
  if (_M0L4signS748) {
    int32_t _M0L6_2atmpS1980 = _M0Lm5indexS747;
    int32_t _M0L6_2atmpS1981;
    if (
      _M0L6_2atmpS1980 < 0
      || _M0L6_2atmpS1980 >= Moonbit_array_length(_M0L6resultS746)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS746[_M0L6_2atmpS1980] = 45;
    _M0L6_2atmpS1981 = _M0Lm5indexS747;
    _M0Lm5indexS747 = _M0L6_2atmpS1981 + 1;
  }
  _M0L6outputS749 = _M0L1vS750->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS751 = _M0FPB17decimal__length17(_M0L6outputS749);
  _M0L8exponentS2110 = _M0L1vS750->$1;
  _M0L6_2atmpS2109 = _M0L8exponentS2110 + _M0L7olengthS751;
  _M0Lm3expS752 = _M0L6_2atmpS2109 - 1;
  _M0L6_2atmpS2108 = _M0Lm3expS752;
  if (_M0L6_2atmpS2108 >= -6) {
    int32_t _M0L6_2atmpS2107 = _M0Lm3expS752;
    _M0L6_2atmpS2106 = _M0L6_2atmpS2107 < 21;
  } else {
    _M0L6_2atmpS2106 = 0;
  }
  _M0L18scientificNotationS753 = !_M0L6_2atmpS2106;
  if (_M0L18scientificNotationS753) {
    int32_t _M0L7_2abindS754 = _M0L7olengthS751 - 1;
    uint64_t _M0L6outputS755;
    int32_t _M0L1iS756 = 0;
    uint64_t _M0L6outputS757 = _M0L6outputS749;
    int32_t _M0L6_2atmpS1982;
    int32_t _M0L6_2atmpS1986;
    int32_t _M0L6_2atmpS1985;
    int32_t _M0L6_2atmpS1984;
    int32_t _M0L6_2atmpS1983;
    int32_t _M0L6_2atmpS1990;
    int32_t _M0L6_2atmpS1991;
    int32_t _M0L6_2atmpS1992;
    int32_t _M0L6_2atmpS1993;
    int32_t _M0L6_2atmpS1994;
    int32_t _M0L6_2atmpS2000;
    int32_t _M0L6_2atmpS2033;
    moonbit_string_t _result_2819;
    while (1) {
      if (_M0L1iS756 < _M0L7_2abindS754) {
        uint64_t _M0L1cS758 = _M0L6outputS757 % 10ull;
        int32_t _M0L6_2atmpS2039 = _M0Lm5indexS747;
        int32_t _M0L6_2atmpS2038 = _M0L6_2atmpS2039 + _M0L7olengthS751;
        int32_t _M0L6_2atmpS2034 = _M0L6_2atmpS2038 - _M0L1iS756;
        int32_t _M0L6_2atmpS2037 = (int32_t)_M0L1cS758;
        int32_t _M0L6_2atmpS2036 = 48 + _M0L6_2atmpS2037;
        int32_t _M0L6_2atmpS2035 = _M0L6_2atmpS2036 & 0xff;
        int32_t _M0L6_2atmpS2040;
        uint64_t _M0L6_2atmpS2041;
        if (
          _M0L6_2atmpS2034 < 0
          || _M0L6_2atmpS2034 >= Moonbit_array_length(_M0L6resultS746)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS746[_M0L6_2atmpS2034] = _M0L6_2atmpS2035;
        _M0L6_2atmpS2040 = _M0L1iS756 + 1;
        _M0L6_2atmpS2041 = _M0L6outputS757 / 10ull;
        _M0L1iS756 = _M0L6_2atmpS2040;
        _M0L6outputS757 = _M0L6_2atmpS2041;
        continue;
      } else {
        _M0L6outputS755 = _M0L6outputS757;
      }
      break;
    }
    _M0L6_2atmpS1982 = _M0Lm5indexS747;
    _M0L6_2atmpS1986 = (int32_t)_M0L6outputS755;
    _M0L6_2atmpS1985 = _M0L6_2atmpS1986 % 10;
    _M0L6_2atmpS1984 = 48 + _M0L6_2atmpS1985;
    _M0L6_2atmpS1983 = _M0L6_2atmpS1984 & 0xff;
    if (
      _M0L6_2atmpS1982 < 0
      || _M0L6_2atmpS1982 >= Moonbit_array_length(_M0L6resultS746)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS746[_M0L6_2atmpS1982] = _M0L6_2atmpS1983;
    if (_M0L7olengthS751 > 1) {
      int32_t _M0L6_2atmpS1988 = _M0Lm5indexS747;
      int32_t _M0L6_2atmpS1987 = _M0L6_2atmpS1988 + 1;
      if (
        _M0L6_2atmpS1987 < 0
        || _M0L6_2atmpS1987 >= Moonbit_array_length(_M0L6resultS746)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS746[_M0L6_2atmpS1987] = 46;
    } else {
      int32_t _M0L6_2atmpS1989 = _M0Lm5indexS747;
      _M0Lm5indexS747 = _M0L6_2atmpS1989 - 1;
    }
    _M0L6_2atmpS1990 = _M0Lm5indexS747;
    _M0L6_2atmpS1991 = _M0L7olengthS751 + 1;
    _M0Lm5indexS747 = _M0L6_2atmpS1990 + _M0L6_2atmpS1991;
    _M0L6_2atmpS1992 = _M0Lm5indexS747;
    if (
      _M0L6_2atmpS1992 < 0
      || _M0L6_2atmpS1992 >= Moonbit_array_length(_M0L6resultS746)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS746[_M0L6_2atmpS1992] = 101;
    _M0L6_2atmpS1993 = _M0Lm5indexS747;
    _M0Lm5indexS747 = _M0L6_2atmpS1993 + 1;
    _M0L6_2atmpS1994 = _M0Lm3expS752;
    if (_M0L6_2atmpS1994 < 0) {
      int32_t _M0L6_2atmpS1995 = _M0Lm5indexS747;
      int32_t _M0L6_2atmpS1996;
      int32_t _M0L6_2atmpS1997;
      if (
        _M0L6_2atmpS1995 < 0
        || _M0L6_2atmpS1995 >= Moonbit_array_length(_M0L6resultS746)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS746[_M0L6_2atmpS1995] = 45;
      _M0L6_2atmpS1996 = _M0Lm5indexS747;
      _M0Lm5indexS747 = _M0L6_2atmpS1996 + 1;
      _M0L6_2atmpS1997 = _M0Lm3expS752;
      _M0Lm3expS752 = -_M0L6_2atmpS1997;
    } else {
      int32_t _M0L6_2atmpS1998 = _M0Lm5indexS747;
      int32_t _M0L6_2atmpS1999;
      if (
        _M0L6_2atmpS1998 < 0
        || _M0L6_2atmpS1998 >= Moonbit_array_length(_M0L6resultS746)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS746[_M0L6_2atmpS1998] = 43;
      _M0L6_2atmpS1999 = _M0Lm5indexS747;
      _M0Lm5indexS747 = _M0L6_2atmpS1999 + 1;
    }
    _M0L6_2atmpS2000 = _M0Lm3expS752;
    if (_M0L6_2atmpS2000 >= 100) {
      int32_t _M0L6_2atmpS2016 = _M0Lm3expS752;
      int32_t _M0L1aS760 = _M0L6_2atmpS2016 / 100;
      int32_t _M0L6_2atmpS2015 = _M0Lm3expS752;
      int32_t _M0L6_2atmpS2014 = _M0L6_2atmpS2015 / 10;
      int32_t _M0L1bS761 = _M0L6_2atmpS2014 % 10;
      int32_t _M0L6_2atmpS2013 = _M0Lm3expS752;
      int32_t _M0L1cS762 = _M0L6_2atmpS2013 % 10;
      int32_t _M0L6_2atmpS2001 = _M0Lm5indexS747;
      int32_t _M0L6_2atmpS2003 = 48 + _M0L1aS760;
      int32_t _M0L6_2atmpS2002 = _M0L6_2atmpS2003 & 0xff;
      int32_t _M0L6_2atmpS2007;
      int32_t _M0L6_2atmpS2004;
      int32_t _M0L6_2atmpS2006;
      int32_t _M0L6_2atmpS2005;
      int32_t _M0L6_2atmpS2011;
      int32_t _M0L6_2atmpS2008;
      int32_t _M0L6_2atmpS2010;
      int32_t _M0L6_2atmpS2009;
      int32_t _M0L6_2atmpS2012;
      if (
        _M0L6_2atmpS2001 < 0
        || _M0L6_2atmpS2001 >= Moonbit_array_length(_M0L6resultS746)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS746[_M0L6_2atmpS2001] = _M0L6_2atmpS2002;
      _M0L6_2atmpS2007 = _M0Lm5indexS747;
      _M0L6_2atmpS2004 = _M0L6_2atmpS2007 + 1;
      _M0L6_2atmpS2006 = 48 + _M0L1bS761;
      _M0L6_2atmpS2005 = _M0L6_2atmpS2006 & 0xff;
      if (
        _M0L6_2atmpS2004 < 0
        || _M0L6_2atmpS2004 >= Moonbit_array_length(_M0L6resultS746)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS746[_M0L6_2atmpS2004] = _M0L6_2atmpS2005;
      _M0L6_2atmpS2011 = _M0Lm5indexS747;
      _M0L6_2atmpS2008 = _M0L6_2atmpS2011 + 2;
      _M0L6_2atmpS2010 = 48 + _M0L1cS762;
      _M0L6_2atmpS2009 = _M0L6_2atmpS2010 & 0xff;
      if (
        _M0L6_2atmpS2008 < 0
        || _M0L6_2atmpS2008 >= Moonbit_array_length(_M0L6resultS746)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS746[_M0L6_2atmpS2008] = _M0L6_2atmpS2009;
      _M0L6_2atmpS2012 = _M0Lm5indexS747;
      _M0Lm5indexS747 = _M0L6_2atmpS2012 + 3;
    } else {
      int32_t _M0L6_2atmpS2017 = _M0Lm3expS752;
      if (_M0L6_2atmpS2017 >= 10) {
        int32_t _M0L6_2atmpS2027 = _M0Lm3expS752;
        int32_t _M0L1aS763 = _M0L6_2atmpS2027 / 10;
        int32_t _M0L6_2atmpS2026 = _M0Lm3expS752;
        int32_t _M0L1bS764 = _M0L6_2atmpS2026 % 10;
        int32_t _M0L6_2atmpS2018 = _M0Lm5indexS747;
        int32_t _M0L6_2atmpS2020 = 48 + _M0L1aS763;
        int32_t _M0L6_2atmpS2019 = _M0L6_2atmpS2020 & 0xff;
        int32_t _M0L6_2atmpS2024;
        int32_t _M0L6_2atmpS2021;
        int32_t _M0L6_2atmpS2023;
        int32_t _M0L6_2atmpS2022;
        int32_t _M0L6_2atmpS2025;
        if (
          _M0L6_2atmpS2018 < 0
          || _M0L6_2atmpS2018 >= Moonbit_array_length(_M0L6resultS746)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS746[_M0L6_2atmpS2018] = _M0L6_2atmpS2019;
        _M0L6_2atmpS2024 = _M0Lm5indexS747;
        _M0L6_2atmpS2021 = _M0L6_2atmpS2024 + 1;
        _M0L6_2atmpS2023 = 48 + _M0L1bS764;
        _M0L6_2atmpS2022 = _M0L6_2atmpS2023 & 0xff;
        if (
          _M0L6_2atmpS2021 < 0
          || _M0L6_2atmpS2021 >= Moonbit_array_length(_M0L6resultS746)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS746[_M0L6_2atmpS2021] = _M0L6_2atmpS2022;
        _M0L6_2atmpS2025 = _M0Lm5indexS747;
        _M0Lm5indexS747 = _M0L6_2atmpS2025 + 2;
      } else {
        int32_t _M0L6_2atmpS2028 = _M0Lm5indexS747;
        int32_t _M0L6_2atmpS2031 = _M0Lm3expS752;
        int32_t _M0L6_2atmpS2030 = 48 + _M0L6_2atmpS2031;
        int32_t _M0L6_2atmpS2029 = _M0L6_2atmpS2030 & 0xff;
        int32_t _M0L6_2atmpS2032;
        if (
          _M0L6_2atmpS2028 < 0
          || _M0L6_2atmpS2028 >= Moonbit_array_length(_M0L6resultS746)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS746[_M0L6_2atmpS2028] = _M0L6_2atmpS2029;
        _M0L6_2atmpS2032 = _M0Lm5indexS747;
        _M0Lm5indexS747 = _M0L6_2atmpS2032 + 1;
      }
    }
    _M0L6_2atmpS2033 = _M0Lm5indexS747;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2819
    = _M0FPB19string__from__bytes(_M0L6resultS746, 0, _M0L6_2atmpS2033);
    moonbit_decref_cycle_free(_M0L6resultS746);
    return _result_2819;
  } else {
    int32_t _M0L6_2atmpS2042 = _M0Lm3expS752;
    int32_t _M0L6_2atmpS2105;
    moonbit_string_t _result_2825;
    if (_M0L6_2atmpS2042 < 0) {
      int32_t _M0L6_2atmpS2043 = _M0Lm5indexS747;
      int32_t _M0L6_2atmpS2045;
      int32_t _M0L6_2atmpS2044;
      int32_t _M0L6_2atmpS2046;
      int32_t _M0L1iS765;
      int32_t _M0L6_2atmpS2061;
      int32_t _M0L6_2atmpS2063;
      int32_t _M0L6_2atmpS2062;
      int32_t _M0L7currentS767;
      int32_t _M0L1iS768;
      uint64_t _M0L6outputS769;
      if (
        _M0L6_2atmpS2043 < 0
        || _M0L6_2atmpS2043 >= Moonbit_array_length(_M0L6resultS746)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS746[_M0L6_2atmpS2043] = 48;
      _M0L6_2atmpS2045 = _M0Lm5indexS747;
      _M0L6_2atmpS2044 = _M0L6_2atmpS2045 + 1;
      if (
        _M0L6_2atmpS2044 < 0
        || _M0L6_2atmpS2044 >= Moonbit_array_length(_M0L6resultS746)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS746[_M0L6_2atmpS2044] = 46;
      _M0L6_2atmpS2046 = _M0Lm5indexS747;
      _M0Lm5indexS747 = _M0L6_2atmpS2046 + 2;
      _M0L1iS765 = -1;
      while (1) {
        int32_t _M0L6_2atmpS2047 = _M0Lm3expS752;
        if (_M0L1iS765 > _M0L6_2atmpS2047) {
          int32_t _M0L6_2atmpS2050 = _M0Lm5indexS747;
          int32_t _M0L6_2atmpS2049 = _M0L6_2atmpS2050 - _M0L1iS765;
          int32_t _M0L6_2atmpS2048 = _M0L6_2atmpS2049 - 1;
          int32_t _M0L6_2atmpS2051;
          if (
            _M0L6_2atmpS2048 < 0
            || _M0L6_2atmpS2048 >= Moonbit_array_length(_M0L6resultS746)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS746[_M0L6_2atmpS2048] = 48;
          _M0L6_2atmpS2051 = _M0L1iS765 - 1;
          _M0L1iS765 = _M0L6_2atmpS2051;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2061 = _M0Lm5indexS747;
      _M0L6_2atmpS2063 = _M0Lm3expS752;
      _M0L6_2atmpS2062 = -1 - _M0L6_2atmpS2063;
      _M0L7currentS767 = _M0L6_2atmpS2061 + _M0L6_2atmpS2062;
      _M0L1iS768 = 0;
      _M0L6outputS769 = _M0L6outputS749;
      while (1) {
        if (_M0L1iS768 < _M0L7olengthS751) {
          int32_t _M0L6_2atmpS2058 = _M0L7currentS767 + _M0L7olengthS751;
          int32_t _M0L6_2atmpS2057 = _M0L6_2atmpS2058 - _M0L1iS768;
          int32_t _M0L6_2atmpS2052 = _M0L6_2atmpS2057 - 1;
          uint64_t _M0L6_2atmpS2056 = _M0L6outputS769 % 10ull;
          int32_t _M0L6_2atmpS2055 = (int32_t)_M0L6_2atmpS2056;
          int32_t _M0L6_2atmpS2054 = 48 + _M0L6_2atmpS2055;
          int32_t _M0L6_2atmpS2053 = _M0L6_2atmpS2054 & 0xff;
          int32_t _M0L6_2atmpS2059;
          uint64_t _M0L6_2atmpS2060;
          if (
            _M0L6_2atmpS2052 < 0
            || _M0L6_2atmpS2052 >= Moonbit_array_length(_M0L6resultS746)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS746[_M0L6_2atmpS2052] = _M0L6_2atmpS2053;
          _M0L6_2atmpS2059 = _M0L1iS768 + 1;
          _M0L6_2atmpS2060 = _M0L6outputS769 / 10ull;
          _M0L1iS768 = _M0L6_2atmpS2059;
          _M0L6outputS769 = _M0L6_2atmpS2060;
          continue;
        }
        break;
      }
      _M0Lm5indexS747 = _M0L7currentS767 + _M0L7olengthS751;
    } else {
      int32_t _M0L6_2atmpS2065 = _M0Lm3expS752;
      int32_t _M0L6_2atmpS2064 = _M0L6_2atmpS2065 + 1;
      if (_M0L6_2atmpS2064 >= _M0L7olengthS751) {
        int32_t _M0L1iS771 = 0;
        uint64_t _M0L6outputS772 = _M0L6outputS749;
        int32_t _M0L6_2atmpS2076;
        int32_t _M0L6_2atmpS2081;
        int32_t _M0L7_2abindS774;
        int32_t _M0L1iS775;
        int32_t _M0L6_2atmpS2082;
        int32_t _M0L6_2atmpS2085;
        int32_t _M0L6_2atmpS2084;
        int32_t _M0L6_2atmpS2083;
        while (1) {
          if (_M0L1iS771 < _M0L7olengthS751) {
            int32_t _M0L6_2atmpS2073 = _M0Lm5indexS747;
            int32_t _M0L6_2atmpS2072 = _M0L6_2atmpS2073 + _M0L7olengthS751;
            int32_t _M0L6_2atmpS2071 = _M0L6_2atmpS2072 - _M0L1iS771;
            int32_t _M0L6_2atmpS2066 = _M0L6_2atmpS2071 - 1;
            uint64_t _M0L6_2atmpS2070 = _M0L6outputS772 % 10ull;
            int32_t _M0L6_2atmpS2069 = (int32_t)_M0L6_2atmpS2070;
            int32_t _M0L6_2atmpS2068 = 48 + _M0L6_2atmpS2069;
            int32_t _M0L6_2atmpS2067 = _M0L6_2atmpS2068 & 0xff;
            int32_t _M0L6_2atmpS2074;
            uint64_t _M0L6_2atmpS2075;
            if (
              _M0L6_2atmpS2066 < 0
              || _M0L6_2atmpS2066 >= Moonbit_array_length(_M0L6resultS746)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS746[_M0L6_2atmpS2066] = _M0L6_2atmpS2067;
            _M0L6_2atmpS2074 = _M0L1iS771 + 1;
            _M0L6_2atmpS2075 = _M0L6outputS772 / 10ull;
            _M0L1iS771 = _M0L6_2atmpS2074;
            _M0L6outputS772 = _M0L6_2atmpS2075;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2076 = _M0Lm5indexS747;
        _M0Lm5indexS747 = _M0L6_2atmpS2076 + _M0L7olengthS751;
        _M0L6_2atmpS2081 = _M0Lm3expS752;
        _M0L7_2abindS774 = _M0L6_2atmpS2081 + 1;
        _M0L1iS775 = _M0L7olengthS751;
        while (1) {
          if (_M0L1iS775 < _M0L7_2abindS774) {
            int32_t _M0L6_2atmpS2079 = _M0Lm5indexS747;
            int32_t _M0L6_2atmpS2078 = _M0L6_2atmpS2079 + _M0L1iS775;
            int32_t _M0L6_2atmpS2077 = _M0L6_2atmpS2078 - _M0L7olengthS751;
            int32_t _M0L6_2atmpS2080;
            if (
              _M0L6_2atmpS2077 < 0
              || _M0L6_2atmpS2077 >= Moonbit_array_length(_M0L6resultS746)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS746[_M0L6_2atmpS2077] = 48;
            _M0L6_2atmpS2080 = _M0L1iS775 + 1;
            _M0L1iS775 = _M0L6_2atmpS2080;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2082 = _M0Lm5indexS747;
        _M0L6_2atmpS2085 = _M0Lm3expS752;
        _M0L6_2atmpS2084 = _M0L6_2atmpS2085 + 1;
        _M0L6_2atmpS2083 = _M0L6_2atmpS2084 - _M0L7olengthS751;
        _M0Lm5indexS747 = _M0L6_2atmpS2082 + _M0L6_2atmpS2083;
      } else {
        int32_t _M0L6_2atmpS2102 = _M0Lm5indexS747;
        int32_t _M0L6_2atmpS2101 = _M0L6_2atmpS2102 + 1;
        int32_t _M0L1iS777 = 0;
        int32_t _M0L7currentS778 = _M0L6_2atmpS2101;
        uint64_t _M0L6outputS779 = _M0L6outputS749;
        int32_t _M0L6_2atmpS2103;
        int32_t _M0L6_2atmpS2104;
        while (1) {
          if (_M0L1iS777 < _M0L7olengthS751) {
            int32_t _M0L6_2atmpS2097 = _M0L7olengthS751 - _M0L1iS777;
            int32_t _M0L6_2atmpS2095 = _M0L6_2atmpS2097 - 1;
            int32_t _M0L6_2atmpS2096 = _M0Lm3expS752;
            int32_t _M0L7currentS780;
            int32_t _M0L6_2atmpS2092;
            int32_t _M0L6_2atmpS2091;
            int32_t _M0L6_2atmpS2086;
            uint64_t _M0L6_2atmpS2090;
            int32_t _M0L6_2atmpS2089;
            int32_t _M0L6_2atmpS2088;
            int32_t _M0L6_2atmpS2087;
            int32_t _M0L6_2atmpS2093;
            uint64_t _M0L6_2atmpS2094;
            if (_M0L6_2atmpS2095 == _M0L6_2atmpS2096) {
              int32_t _M0L6_2atmpS2100 = _M0L7currentS778 + _M0L7olengthS751;
              int32_t _M0L6_2atmpS2099 = _M0L6_2atmpS2100 - _M0L1iS777;
              int32_t _M0L6_2atmpS2098 = _M0L6_2atmpS2099 - 1;
              if (
                _M0L6_2atmpS2098 < 0
                || _M0L6_2atmpS2098 >= Moonbit_array_length(_M0L6resultS746)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS746[_M0L6_2atmpS2098] = 46;
              _M0L7currentS780 = _M0L7currentS778 - 1;
            } else {
              _M0L7currentS780 = _M0L7currentS778;
            }
            _M0L6_2atmpS2092 = _M0L7currentS780 + _M0L7olengthS751;
            _M0L6_2atmpS2091 = _M0L6_2atmpS2092 - _M0L1iS777;
            _M0L6_2atmpS2086 = _M0L6_2atmpS2091 - 1;
            _M0L6_2atmpS2090 = _M0L6outputS779 % 10ull;
            _M0L6_2atmpS2089 = (int32_t)_M0L6_2atmpS2090;
            _M0L6_2atmpS2088 = 48 + _M0L6_2atmpS2089;
            _M0L6_2atmpS2087 = _M0L6_2atmpS2088 & 0xff;
            if (
              _M0L6_2atmpS2086 < 0
              || _M0L6_2atmpS2086 >= Moonbit_array_length(_M0L6resultS746)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS746[_M0L6_2atmpS2086] = _M0L6_2atmpS2087;
            _M0L6_2atmpS2093 = _M0L1iS777 + 1;
            _M0L6_2atmpS2094 = _M0L6outputS779 / 10ull;
            _M0L1iS777 = _M0L6_2atmpS2093;
            _M0L7currentS778 = _M0L7currentS780;
            _M0L6outputS779 = _M0L6_2atmpS2094;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2103 = _M0Lm5indexS747;
        _M0L6_2atmpS2104 = _M0L7olengthS751 + 1;
        _M0Lm5indexS747 = _M0L6_2atmpS2103 + _M0L6_2atmpS2104;
      }
    }
    _M0L6_2atmpS2105 = _M0Lm5indexS747;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2825
    = _M0FPB19string__from__bytes(_M0L6resultS746, 0, _M0L6_2atmpS2105);
    moonbit_decref_cycle_free(_M0L6resultS746);
    return _result_2825;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS692,
  uint32_t _M0L12ieeeExponentS691
) {
  int32_t _M0Lm2e2S689;
  uint64_t _M0Lm2m2S690;
  uint64_t _M0L6_2atmpS1979;
  uint64_t _M0L6_2atmpS1978;
  int32_t _M0L4evenS693;
  uint64_t _M0L6_2atmpS1977;
  uint64_t _M0L2mvS694;
  int32_t _M0L7mmShiftS695;
  uint64_t _M0Lm2vrS696;
  uint64_t _M0Lm2vpS697;
  uint64_t _M0Lm2vmS698;
  int32_t _M0Lm3e10S699;
  int32_t _M0Lm17vmIsTrailingZerosS700;
  int32_t _M0Lm17vrIsTrailingZerosS701;
  int32_t _M0L6_2atmpS1879;
  int32_t _M0Lm7removedS720;
  int32_t _M0Lm16lastRemovedDigitS721;
  uint64_t _M0Lm6outputS722;
  int32_t _M0L6_2atmpS1975;
  int32_t _M0L6_2atmpS1976;
  int32_t _M0L3expS745;
  uint64_t _M0L6_2atmpS1974;
  struct _M0TPB17FloatingDecimal64* _block_2831;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S689 = 0;
  _M0Lm2m2S690 = 0ull;
  if (_M0L12ieeeExponentS691 == 0u) {
    _M0Lm2e2S689 = -1076;
    _M0Lm2m2S690 = _M0L12ieeeMantissaS692;
  } else {
    int32_t _M0L6_2atmpS1878 = *(int32_t*)&_M0L12ieeeExponentS691;
    int32_t _M0L6_2atmpS1877 = _M0L6_2atmpS1878 - 1023;
    int32_t _M0L6_2atmpS1876 = _M0L6_2atmpS1877 - 52;
    _M0Lm2e2S689 = _M0L6_2atmpS1876 - 2;
    _M0Lm2m2S690 = 4503599627370496ull | _M0L12ieeeMantissaS692;
  }
  _M0L6_2atmpS1979 = _M0Lm2m2S690;
  _M0L6_2atmpS1978 = _M0L6_2atmpS1979 & 1ull;
  _M0L4evenS693 = _M0L6_2atmpS1978 == 0ull;
  _M0L6_2atmpS1977 = _M0Lm2m2S690;
  _M0L2mvS694 = 4ull * _M0L6_2atmpS1977;
  _M0L7mmShiftS695
  = _M0L12ieeeMantissaS692 != 0ull || _M0L12ieeeExponentS691 <= 1u;
  _M0Lm2vrS696 = 0ull;
  _M0Lm2vpS697 = 0ull;
  _M0Lm2vmS698 = 0ull;
  _M0Lm3e10S699 = 0;
  _M0Lm17vmIsTrailingZerosS700 = 0;
  _M0Lm17vrIsTrailingZerosS701 = 0;
  _M0L6_2atmpS1879 = _M0Lm2e2S689;
  if (_M0L6_2atmpS1879 >= 0) {
    int32_t _M0L6_2atmpS1901 = _M0Lm2e2S689;
    int32_t _M0L6_2atmpS1897;
    int32_t _M0L6_2atmpS1900;
    int32_t _M0L6_2atmpS1899;
    int32_t _M0L6_2atmpS1898;
    int32_t _M0L1qS702;
    int32_t _M0L6_2atmpS1896;
    int32_t _M0L6_2atmpS1895;
    int32_t _M0L1kS703;
    int32_t _M0L6_2atmpS1894;
    int32_t _M0L6_2atmpS1893;
    int32_t _M0L6_2atmpS1892;
    int32_t _M0L1iS704;
    struct _M0TPB8Pow5Pair _M0L4pow5S705;
    uint64_t _M0L6_2atmpS1891;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS706;
    uint64_t _M0L8_2avrOutS707;
    uint64_t _M0L8_2avpOutS708;
    uint64_t _M0L8_2avmOutS709;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1897 = _M0FPB9log10Pow2(_M0L6_2atmpS1901);
    _M0L6_2atmpS1900 = _M0Lm2e2S689;
    _M0L6_2atmpS1899 = _M0L6_2atmpS1900 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1898 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1899);
    _M0L1qS702 = _M0L6_2atmpS1897 - _M0L6_2atmpS1898;
    _M0Lm3e10S699 = _M0L1qS702;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1896 = _M0FPB8pow5bits(_M0L1qS702);
    _M0L6_2atmpS1895 = 125 + _M0L6_2atmpS1896;
    _M0L1kS703 = _M0L6_2atmpS1895 - 1;
    _M0L6_2atmpS1894 = _M0Lm2e2S689;
    _M0L6_2atmpS1893 = -_M0L6_2atmpS1894;
    _M0L6_2atmpS1892 = _M0L6_2atmpS1893 + _M0L1qS702;
    _M0L1iS704 = _M0L6_2atmpS1892 + _M0L1kS703;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S705 = _M0FPB22double__computeInvPow5(_M0L1qS702);
    _M0L6_2atmpS1891 = _M0Lm2m2S690;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS706
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1891, _M0L4pow5S705, _M0L1iS704, _M0L7mmShiftS695);
    _M0L8_2avrOutS707 = _M0L7_2abindS706.$0;
    _M0L8_2avpOutS708 = _M0L7_2abindS706.$1;
    _M0L8_2avmOutS709 = _M0L7_2abindS706.$2;
    _M0Lm2vrS696 = _M0L8_2avrOutS707;
    _M0Lm2vpS697 = _M0L8_2avpOutS708;
    _M0Lm2vmS698 = _M0L8_2avmOutS709;
    if (_M0L1qS702 <= 21) {
      int32_t _M0L6_2atmpS1887 = (int32_t)_M0L2mvS694;
      uint64_t _M0L6_2atmpS1890 = _M0L2mvS694 / 5ull;
      int32_t _M0L6_2atmpS1889 = (int32_t)_M0L6_2atmpS1890;
      int32_t _M0L6_2atmpS1888 = 5 * _M0L6_2atmpS1889;
      int32_t _M0L6mvMod5S710 = _M0L6_2atmpS1887 - _M0L6_2atmpS1888;
      if (_M0L6mvMod5S710 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS701
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS694, _M0L1qS702);
      } else if (_M0L4evenS693) {
        uint64_t _M0L6_2atmpS1881 = _M0L2mvS694 - 1ull;
        uint64_t _M0L6_2atmpS1882;
        uint64_t _M0L6_2atmpS1880;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1882 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS695);
        _M0L6_2atmpS1880 = _M0L6_2atmpS1881 - _M0L6_2atmpS1882;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS700
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1880, _M0L1qS702);
      } else {
        uint64_t _M0L6_2atmpS1883 = _M0Lm2vpS697;
        uint64_t _M0L6_2atmpS1886 = _M0L2mvS694 + 2ull;
        int32_t _M0L6_2atmpS1885;
        uint64_t _M0L6_2atmpS1884;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1885
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1886, _M0L1qS702);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1884 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1885);
        _M0Lm2vpS697 = _M0L6_2atmpS1883 - _M0L6_2atmpS1884;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1915 = _M0Lm2e2S689;
    int32_t _M0L6_2atmpS1914 = -_M0L6_2atmpS1915;
    int32_t _M0L6_2atmpS1909;
    int32_t _M0L6_2atmpS1913;
    int32_t _M0L6_2atmpS1912;
    int32_t _M0L6_2atmpS1911;
    int32_t _M0L6_2atmpS1910;
    int32_t _M0L1qS711;
    int32_t _M0L6_2atmpS1902;
    int32_t _M0L6_2atmpS1908;
    int32_t _M0L6_2atmpS1907;
    int32_t _M0L1iS712;
    int32_t _M0L6_2atmpS1906;
    int32_t _M0L1kS713;
    int32_t _M0L1jS714;
    struct _M0TPB8Pow5Pair _M0L4pow5S715;
    uint64_t _M0L6_2atmpS1905;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS716;
    uint64_t _M0L8_2avrOutS717;
    uint64_t _M0L8_2avpOutS718;
    uint64_t _M0L8_2avmOutS719;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1909 = _M0FPB9log10Pow5(_M0L6_2atmpS1914);
    _M0L6_2atmpS1913 = _M0Lm2e2S689;
    _M0L6_2atmpS1912 = -_M0L6_2atmpS1913;
    _M0L6_2atmpS1911 = _M0L6_2atmpS1912 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1910 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1911);
    _M0L1qS711 = _M0L6_2atmpS1909 - _M0L6_2atmpS1910;
    _M0L6_2atmpS1902 = _M0Lm2e2S689;
    _M0Lm3e10S699 = _M0L1qS711 + _M0L6_2atmpS1902;
    _M0L6_2atmpS1908 = _M0Lm2e2S689;
    _M0L6_2atmpS1907 = -_M0L6_2atmpS1908;
    _M0L1iS712 = _M0L6_2atmpS1907 - _M0L1qS711;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1906 = _M0FPB8pow5bits(_M0L1iS712);
    _M0L1kS713 = _M0L6_2atmpS1906 - 125;
    _M0L1jS714 = _M0L1qS711 - _M0L1kS713;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S715 = _M0FPB19double__computePow5(_M0L1iS712);
    _M0L6_2atmpS1905 = _M0Lm2m2S690;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS716
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1905, _M0L4pow5S715, _M0L1jS714, _M0L7mmShiftS695);
    _M0L8_2avrOutS717 = _M0L7_2abindS716.$0;
    _M0L8_2avpOutS718 = _M0L7_2abindS716.$1;
    _M0L8_2avmOutS719 = _M0L7_2abindS716.$2;
    _M0Lm2vrS696 = _M0L8_2avrOutS717;
    _M0Lm2vpS697 = _M0L8_2avpOutS718;
    _M0Lm2vmS698 = _M0L8_2avmOutS719;
    if (_M0L1qS711 <= 1) {
      _M0Lm17vrIsTrailingZerosS701 = 1;
      if (_M0L4evenS693) {
        int32_t _M0L6_2atmpS1903;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1903 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS695);
        _M0Lm17vmIsTrailingZerosS700 = _M0L6_2atmpS1903 == 1;
      } else {
        uint64_t _M0L6_2atmpS1904 = _M0Lm2vpS697;
        _M0Lm2vpS697 = _M0L6_2atmpS1904 - 1ull;
      }
    } else if (_M0L1qS711 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS701
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS694, _M0L1qS711);
    }
  }
  _M0Lm7removedS720 = 0;
  _M0Lm16lastRemovedDigitS721 = 0;
  _M0Lm6outputS722 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS700 || _M0Lm17vrIsTrailingZerosS701) {
    int32_t _if__result_2828;
    uint64_t _M0L6_2atmpS1945;
    uint64_t _M0L6_2atmpS1951;
    uint64_t _M0L6_2atmpS1952;
    int32_t _if__result_2829;
    int32_t _M0L6_2atmpS1948;
    int64_t _M0L6_2atmpS1947;
    uint64_t _M0L6_2atmpS1946;
    while (1) {
      uint64_t _M0L6_2atmpS1928 = _M0Lm2vpS697;
      uint64_t _M0L7vpDiv10S723 = _M0L6_2atmpS1928 / 10ull;
      uint64_t _M0L6_2atmpS1927 = _M0Lm2vmS698;
      uint64_t _M0L7vmDiv10S724 = _M0L6_2atmpS1927 / 10ull;
      uint64_t _M0L6_2atmpS1926;
      int32_t _M0L6_2atmpS1923;
      int32_t _M0L6_2atmpS1925;
      int32_t _M0L6_2atmpS1924;
      int32_t _M0L7vmMod10S726;
      uint64_t _M0L6_2atmpS1922;
      uint64_t _M0L7vrDiv10S727;
      uint64_t _M0L6_2atmpS1921;
      int32_t _M0L6_2atmpS1918;
      int32_t _M0L6_2atmpS1920;
      int32_t _M0L6_2atmpS1919;
      int32_t _M0L7vrMod10S728;
      int32_t _M0L6_2atmpS1917;
      if (_M0L7vpDiv10S723 <= _M0L7vmDiv10S724) {
        break;
      }
      _M0L6_2atmpS1926 = _M0Lm2vmS698;
      _M0L6_2atmpS1923 = (int32_t)_M0L6_2atmpS1926;
      _M0L6_2atmpS1925 = (int32_t)_M0L7vmDiv10S724;
      _M0L6_2atmpS1924 = 10 * _M0L6_2atmpS1925;
      _M0L7vmMod10S726 = _M0L6_2atmpS1923 - _M0L6_2atmpS1924;
      _M0L6_2atmpS1922 = _M0Lm2vrS696;
      _M0L7vrDiv10S727 = _M0L6_2atmpS1922 / 10ull;
      _M0L6_2atmpS1921 = _M0Lm2vrS696;
      _M0L6_2atmpS1918 = (int32_t)_M0L6_2atmpS1921;
      _M0L6_2atmpS1920 = (int32_t)_M0L7vrDiv10S727;
      _M0L6_2atmpS1919 = 10 * _M0L6_2atmpS1920;
      _M0L7vrMod10S728 = _M0L6_2atmpS1918 - _M0L6_2atmpS1919;
      _M0Lm17vmIsTrailingZerosS700
      = _M0Lm17vmIsTrailingZerosS700 && _M0L7vmMod10S726 == 0;
      if (_M0Lm17vrIsTrailingZerosS701) {
        int32_t _M0L6_2atmpS1916 = _M0Lm16lastRemovedDigitS721;
        _M0Lm17vrIsTrailingZerosS701 = _M0L6_2atmpS1916 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS701 = 0;
      }
      _M0Lm16lastRemovedDigitS721 = _M0L7vrMod10S728;
      _M0Lm2vrS696 = _M0L7vrDiv10S727;
      _M0Lm2vpS697 = _M0L7vpDiv10S723;
      _M0Lm2vmS698 = _M0L7vmDiv10S724;
      _M0L6_2atmpS1917 = _M0Lm7removedS720;
      _M0Lm7removedS720 = _M0L6_2atmpS1917 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS700) {
      while (1) {
        uint64_t _M0L6_2atmpS1941 = _M0Lm2vmS698;
        uint64_t _M0L7vmDiv10S729 = _M0L6_2atmpS1941 / 10ull;
        uint64_t _M0L6_2atmpS1940 = _M0Lm2vmS698;
        int32_t _M0L6_2atmpS1937 = (int32_t)_M0L6_2atmpS1940;
        int32_t _M0L6_2atmpS1939 = (int32_t)_M0L7vmDiv10S729;
        int32_t _M0L6_2atmpS1938 = 10 * _M0L6_2atmpS1939;
        int32_t _M0L7vmMod10S730 = _M0L6_2atmpS1937 - _M0L6_2atmpS1938;
        uint64_t _M0L6_2atmpS1936;
        uint64_t _M0L7vpDiv10S732;
        uint64_t _M0L6_2atmpS1935;
        uint64_t _M0L7vrDiv10S733;
        uint64_t _M0L6_2atmpS1934;
        int32_t _M0L6_2atmpS1931;
        int32_t _M0L6_2atmpS1933;
        int32_t _M0L6_2atmpS1932;
        int32_t _M0L7vrMod10S734;
        int32_t _M0L6_2atmpS1930;
        if (_M0L7vmMod10S730 != 0) {
          break;
        }
        _M0L6_2atmpS1936 = _M0Lm2vpS697;
        _M0L7vpDiv10S732 = _M0L6_2atmpS1936 / 10ull;
        _M0L6_2atmpS1935 = _M0Lm2vrS696;
        _M0L7vrDiv10S733 = _M0L6_2atmpS1935 / 10ull;
        _M0L6_2atmpS1934 = _M0Lm2vrS696;
        _M0L6_2atmpS1931 = (int32_t)_M0L6_2atmpS1934;
        _M0L6_2atmpS1933 = (int32_t)_M0L7vrDiv10S733;
        _M0L6_2atmpS1932 = 10 * _M0L6_2atmpS1933;
        _M0L7vrMod10S734 = _M0L6_2atmpS1931 - _M0L6_2atmpS1932;
        if (_M0Lm17vrIsTrailingZerosS701) {
          int32_t _M0L6_2atmpS1929 = _M0Lm16lastRemovedDigitS721;
          _M0Lm17vrIsTrailingZerosS701 = _M0L6_2atmpS1929 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS701 = 0;
        }
        _M0Lm16lastRemovedDigitS721 = _M0L7vrMod10S734;
        _M0Lm2vrS696 = _M0L7vrDiv10S733;
        _M0Lm2vpS697 = _M0L7vpDiv10S732;
        _M0Lm2vmS698 = _M0L7vmDiv10S729;
        _M0L6_2atmpS1930 = _M0Lm7removedS720;
        _M0Lm7removedS720 = _M0L6_2atmpS1930 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS701) {
      int32_t _M0L6_2atmpS1944 = _M0Lm16lastRemovedDigitS721;
      if (_M0L6_2atmpS1944 == 5) {
        uint64_t _M0L6_2atmpS1943 = _M0Lm2vrS696;
        uint64_t _M0L6_2atmpS1942 = _M0L6_2atmpS1943 % 2ull;
        _if__result_2828 = _M0L6_2atmpS1942 == 0ull;
      } else {
        _if__result_2828 = 0;
      }
    } else {
      _if__result_2828 = 0;
    }
    if (_if__result_2828) {
      _M0Lm16lastRemovedDigitS721 = 4;
    }
    _M0L6_2atmpS1945 = _M0Lm2vrS696;
    _M0L6_2atmpS1951 = _M0Lm2vrS696;
    _M0L6_2atmpS1952 = _M0Lm2vmS698;
    if (_M0L6_2atmpS1951 == _M0L6_2atmpS1952) {
      if (!_M0L4evenS693) {
        _if__result_2829 = 1;
      } else {
        int32_t _M0L6_2atmpS1950 = _M0Lm17vmIsTrailingZerosS700;
        _if__result_2829 = !_M0L6_2atmpS1950;
      }
    } else {
      _if__result_2829 = 0;
    }
    if (_if__result_2829) {
      _M0L6_2atmpS1948 = 1;
    } else {
      int32_t _M0L6_2atmpS1949 = _M0Lm16lastRemovedDigitS721;
      _M0L6_2atmpS1948 = _M0L6_2atmpS1949 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1947 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1948);
    _M0L6_2atmpS1946 = *(uint64_t*)&_M0L6_2atmpS1947;
    _M0Lm6outputS722 = _M0L6_2atmpS1945 + _M0L6_2atmpS1946;
  } else {
    int32_t _M0Lm7roundUpS735 = 0;
    uint64_t _M0L6_2atmpS1973 = _M0Lm2vpS697;
    uint64_t _M0L8vpDiv100S736 = _M0L6_2atmpS1973 / 100ull;
    uint64_t _M0L6_2atmpS1972 = _M0Lm2vmS698;
    uint64_t _M0L8vmDiv100S737 = _M0L6_2atmpS1972 / 100ull;
    uint64_t _M0L6_2atmpS1967;
    uint64_t _M0L6_2atmpS1970;
    uint64_t _M0L6_2atmpS1971;
    int32_t _M0L6_2atmpS1969;
    uint64_t _M0L6_2atmpS1968;
    if (_M0L8vpDiv100S736 > _M0L8vmDiv100S737) {
      uint64_t _M0L6_2atmpS1958 = _M0Lm2vrS696;
      uint64_t _M0L8vrDiv100S738 = _M0L6_2atmpS1958 / 100ull;
      uint64_t _M0L6_2atmpS1957 = _M0Lm2vrS696;
      int32_t _M0L6_2atmpS1954 = (int32_t)_M0L6_2atmpS1957;
      int32_t _M0L6_2atmpS1956 = (int32_t)_M0L8vrDiv100S738;
      int32_t _M0L6_2atmpS1955 = 100 * _M0L6_2atmpS1956;
      int32_t _M0L8vrMod100S739 = _M0L6_2atmpS1954 - _M0L6_2atmpS1955;
      int32_t _M0L6_2atmpS1953;
      _M0Lm7roundUpS735 = _M0L8vrMod100S739 >= 50;
      _M0Lm2vrS696 = _M0L8vrDiv100S738;
      _M0Lm2vpS697 = _M0L8vpDiv100S736;
      _M0Lm2vmS698 = _M0L8vmDiv100S737;
      _M0L6_2atmpS1953 = _M0Lm7removedS720;
      _M0Lm7removedS720 = _M0L6_2atmpS1953 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1966 = _M0Lm2vpS697;
      uint64_t _M0L7vpDiv10S740 = _M0L6_2atmpS1966 / 10ull;
      uint64_t _M0L6_2atmpS1965 = _M0Lm2vmS698;
      uint64_t _M0L7vmDiv10S741 = _M0L6_2atmpS1965 / 10ull;
      uint64_t _M0L6_2atmpS1964;
      uint64_t _M0L7vrDiv10S743;
      uint64_t _M0L6_2atmpS1963;
      int32_t _M0L6_2atmpS1960;
      int32_t _M0L6_2atmpS1962;
      int32_t _M0L6_2atmpS1961;
      int32_t _M0L7vrMod10S744;
      int32_t _M0L6_2atmpS1959;
      if (_M0L7vpDiv10S740 <= _M0L7vmDiv10S741) {
        break;
      }
      _M0L6_2atmpS1964 = _M0Lm2vrS696;
      _M0L7vrDiv10S743 = _M0L6_2atmpS1964 / 10ull;
      _M0L6_2atmpS1963 = _M0Lm2vrS696;
      _M0L6_2atmpS1960 = (int32_t)_M0L6_2atmpS1963;
      _M0L6_2atmpS1962 = (int32_t)_M0L7vrDiv10S743;
      _M0L6_2atmpS1961 = 10 * _M0L6_2atmpS1962;
      _M0L7vrMod10S744 = _M0L6_2atmpS1960 - _M0L6_2atmpS1961;
      _M0Lm7roundUpS735 = _M0L7vrMod10S744 >= 5;
      _M0Lm2vrS696 = _M0L7vrDiv10S743;
      _M0Lm2vpS697 = _M0L7vpDiv10S740;
      _M0Lm2vmS698 = _M0L7vmDiv10S741;
      _M0L6_2atmpS1959 = _M0Lm7removedS720;
      _M0Lm7removedS720 = _M0L6_2atmpS1959 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1967 = _M0Lm2vrS696;
    _M0L6_2atmpS1970 = _M0Lm2vrS696;
    _M0L6_2atmpS1971 = _M0Lm2vmS698;
    _M0L6_2atmpS1969
    = _M0L6_2atmpS1970 == _M0L6_2atmpS1971 || _M0Lm7roundUpS735;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1968 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1969);
    _M0Lm6outputS722 = _M0L6_2atmpS1967 + _M0L6_2atmpS1968;
  }
  _M0L6_2atmpS1975 = _M0Lm3e10S699;
  _M0L6_2atmpS1976 = _M0Lm7removedS720;
  _M0L3expS745 = _M0L6_2atmpS1975 + _M0L6_2atmpS1976;
  _M0L6_2atmpS1974 = _M0Lm6outputS722;
  _block_2831
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2831)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2831->$0 = _M0L6_2atmpS1974;
  _block_2831->$1 = _M0L3expS745;
  return _block_2831;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS688) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS688) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS687) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS687) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS686) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS686) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS685) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS685 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS685 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS685 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS685 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS685 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS685 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS685 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS685 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS685 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS685 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS685 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS685 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS685 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS685 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS685 >= 100ull) {
    return 3;
  }
  if (_M0L1vS685 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS668) {
  int32_t _M0L6_2atmpS1875;
  int32_t _M0L6_2atmpS1874;
  int32_t _M0L4baseS667;
  int32_t _M0L5base2S669;
  int32_t _M0L6offsetS670;
  int32_t _M0L6_2atmpS1873;
  uint64_t _M0L4mul0S671;
  int32_t _M0L6_2atmpS1872;
  int32_t _M0L6_2atmpS1871;
  uint64_t _M0L4mul1S672;
  uint64_t _M0L1mS673;
  struct _M0TPB7Umul128 _M0L7_2abindS674;
  uint64_t _M0L7_2alow1S675;
  uint64_t _M0L8_2ahigh1S676;
  struct _M0TPB7Umul128 _M0L7_2abindS677;
  uint64_t _M0L7_2alow0S678;
  uint64_t _M0L8_2ahigh0S679;
  uint64_t _M0L3sumS680;
  uint64_t _M0Lm5high1S681;
  int32_t _M0L6_2atmpS1869;
  int32_t _M0L6_2atmpS1870;
  int32_t _M0L5deltaS682;
  uint64_t _M0L6_2atmpS1868;
  uint64_t _M0L6_2atmpS1860;
  int32_t _M0L6_2atmpS1867;
  uint32_t _M0L6_2atmpS1864;
  int32_t _M0L6_2atmpS1866;
  int32_t _M0L6_2atmpS1865;
  uint32_t _M0L6_2atmpS1863;
  uint32_t _M0L6_2atmpS1862;
  uint64_t _M0L6_2atmpS1861;
  uint64_t _M0L1aS683;
  uint64_t _M0L6_2atmpS1859;
  uint64_t _M0L1bS684;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1875 = _M0L1iS668 + 26;
  _M0L6_2atmpS1874 = _M0L6_2atmpS1875 - 1;
  _M0L4baseS667 = _M0L6_2atmpS1874 / 26;
  _M0L5base2S669 = _M0L4baseS667 * 26;
  _M0L6offsetS670 = _M0L5base2S669 - _M0L1iS668;
  _M0L6_2atmpS1873 = _M0L4baseS667 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S671
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1873);
  _M0L6_2atmpS1872 = _M0L4baseS667 * 2;
  _M0L6_2atmpS1871 = _M0L6_2atmpS1872 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S672
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1871);
  if (_M0L6offsetS670 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S671, .$1 = _M0L4mul1S672};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS673
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS670);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS674 = _M0FPB7umul128(_M0L1mS673, _M0L4mul1S672);
  _M0L7_2alow1S675 = _M0L7_2abindS674.$0;
  _M0L8_2ahigh1S676 = _M0L7_2abindS674.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS677 = _M0FPB7umul128(_M0L1mS673, _M0L4mul0S671);
  _M0L7_2alow0S678 = _M0L7_2abindS677.$0;
  _M0L8_2ahigh0S679 = _M0L7_2abindS677.$1;
  _M0L3sumS680 = _M0L8_2ahigh0S679 + _M0L7_2alow1S675;
  _M0Lm5high1S681 = _M0L8_2ahigh1S676;
  if (_M0L3sumS680 < _M0L8_2ahigh0S679) {
    uint64_t _M0L6_2atmpS1858 = _M0Lm5high1S681;
    _M0Lm5high1S681 = _M0L6_2atmpS1858 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1869 = _M0FPB8pow5bits(_M0L5base2S669);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1870 = _M0FPB8pow5bits(_M0L1iS668);
  _M0L5deltaS682 = _M0L6_2atmpS1869 - _M0L6_2atmpS1870;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1868
  = _M0FPB13shiftright128(_M0L7_2alow0S678, _M0L3sumS680, _M0L5deltaS682);
  _M0L6_2atmpS1860 = _M0L6_2atmpS1868 + 1ull;
  _M0L6_2atmpS1867 = _M0L1iS668 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1864
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1867);
  _M0L6_2atmpS1866 = _M0L1iS668 % 16;
  _M0L6_2atmpS1865 = _M0L6_2atmpS1866 << 1;
  _M0L6_2atmpS1863 = _M0L6_2atmpS1864 >> (_M0L6_2atmpS1865 & 31);
  _M0L6_2atmpS1862 = _M0L6_2atmpS1863 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1861 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1862);
  _M0L1aS683 = _M0L6_2atmpS1860 + _M0L6_2atmpS1861;
  _M0L6_2atmpS1859 = _M0Lm5high1S681;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS684
  = _M0FPB13shiftright128(_M0L3sumS680, _M0L6_2atmpS1859, _M0L5deltaS682);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS683, .$1 = _M0L1bS684};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS650) {
  int32_t _M0L4baseS649;
  int32_t _M0L5base2S651;
  int32_t _M0L6offsetS652;
  int32_t _M0L6_2atmpS1857;
  uint64_t _M0L4mul0S653;
  int32_t _M0L6_2atmpS1856;
  int32_t _M0L6_2atmpS1855;
  uint64_t _M0L4mul1S654;
  uint64_t _M0L1mS655;
  struct _M0TPB7Umul128 _M0L7_2abindS656;
  uint64_t _M0L7_2alow1S657;
  uint64_t _M0L8_2ahigh1S658;
  struct _M0TPB7Umul128 _M0L7_2abindS659;
  uint64_t _M0L7_2alow0S660;
  uint64_t _M0L8_2ahigh0S661;
  uint64_t _M0L3sumS662;
  uint64_t _M0Lm5high1S663;
  int32_t _M0L6_2atmpS1853;
  int32_t _M0L6_2atmpS1854;
  int32_t _M0L5deltaS664;
  uint64_t _M0L6_2atmpS1845;
  int32_t _M0L6_2atmpS1852;
  uint32_t _M0L6_2atmpS1849;
  int32_t _M0L6_2atmpS1851;
  int32_t _M0L6_2atmpS1850;
  uint32_t _M0L6_2atmpS1848;
  uint32_t _M0L6_2atmpS1847;
  uint64_t _M0L6_2atmpS1846;
  uint64_t _M0L1aS665;
  uint64_t _M0L6_2atmpS1844;
  uint64_t _M0L1bS666;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS649 = _M0L1iS650 / 26;
  _M0L5base2S651 = _M0L4baseS649 * 26;
  _M0L6offsetS652 = _M0L1iS650 - _M0L5base2S651;
  _M0L6_2atmpS1857 = _M0L4baseS649 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S653
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1857);
  _M0L6_2atmpS1856 = _M0L4baseS649 * 2;
  _M0L6_2atmpS1855 = _M0L6_2atmpS1856 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S654
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1855);
  if (_M0L6offsetS652 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S653, .$1 = _M0L4mul1S654};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS655
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS652);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS656 = _M0FPB7umul128(_M0L1mS655, _M0L4mul1S654);
  _M0L7_2alow1S657 = _M0L7_2abindS656.$0;
  _M0L8_2ahigh1S658 = _M0L7_2abindS656.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS659 = _M0FPB7umul128(_M0L1mS655, _M0L4mul0S653);
  _M0L7_2alow0S660 = _M0L7_2abindS659.$0;
  _M0L8_2ahigh0S661 = _M0L7_2abindS659.$1;
  _M0L3sumS662 = _M0L8_2ahigh0S661 + _M0L7_2alow1S657;
  _M0Lm5high1S663 = _M0L8_2ahigh1S658;
  if (_M0L3sumS662 < _M0L8_2ahigh0S661) {
    uint64_t _M0L6_2atmpS1843 = _M0Lm5high1S663;
    _M0Lm5high1S663 = _M0L6_2atmpS1843 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1853 = _M0FPB8pow5bits(_M0L1iS650);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1854 = _M0FPB8pow5bits(_M0L5base2S651);
  _M0L5deltaS664 = _M0L6_2atmpS1853 - _M0L6_2atmpS1854;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1845
  = _M0FPB13shiftright128(_M0L7_2alow0S660, _M0L3sumS662, _M0L5deltaS664);
  _M0L6_2atmpS1852 = _M0L1iS650 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1849
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1852);
  _M0L6_2atmpS1851 = _M0L1iS650 % 16;
  _M0L6_2atmpS1850 = _M0L6_2atmpS1851 << 1;
  _M0L6_2atmpS1848 = _M0L6_2atmpS1849 >> (_M0L6_2atmpS1850 & 31);
  _M0L6_2atmpS1847 = _M0L6_2atmpS1848 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1846 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1847);
  _M0L1aS665 = _M0L6_2atmpS1845 + _M0L6_2atmpS1846;
  _M0L6_2atmpS1844 = _M0Lm5high1S663;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS666
  = _M0FPB13shiftright128(_M0L3sumS662, _M0L6_2atmpS1844, _M0L5deltaS664);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS665, .$1 = _M0L1bS666};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS623,
  struct _M0TPB8Pow5Pair _M0L3mulS620,
  int32_t _M0L1jS636,
  int32_t _M0L7mmShiftS638
) {
  uint64_t _M0L7_2amul0S619;
  uint64_t _M0L7_2amul1S621;
  uint64_t _M0L1mS622;
  struct _M0TPB7Umul128 _M0L7_2abindS624;
  uint64_t _M0L5_2aloS625;
  uint64_t _M0L6_2atmpS626;
  struct _M0TPB7Umul128 _M0L7_2abindS627;
  uint64_t _M0L6_2alo2S628;
  uint64_t _M0L6_2ahi2S629;
  uint64_t _M0L3midS630;
  uint64_t _M0L6_2atmpS1842;
  uint64_t _M0L2hiS631;
  uint64_t _M0L3lo2S632;
  uint64_t _M0L6_2atmpS1840;
  uint64_t _M0L6_2atmpS1841;
  uint64_t _M0L4mid2S633;
  uint64_t _M0L6_2atmpS1839;
  uint64_t _M0L3hi2S634;
  int32_t _M0L6_2atmpS1838;
  int32_t _M0L6_2atmpS1837;
  uint64_t _M0L2vpS635;
  uint64_t _M0Lm2vmS637;
  int32_t _M0L6_2atmpS1836;
  int32_t _M0L6_2atmpS1835;
  uint64_t _M0L2vrS648;
  uint64_t _M0L6_2atmpS1834;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S619 = _M0L3mulS620.$0;
  _M0L7_2amul1S621 = _M0L3mulS620.$1;
  _M0L1mS622 = _M0L1mS623 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS624 = _M0FPB7umul128(_M0L1mS622, _M0L7_2amul0S619);
  _M0L5_2aloS625 = _M0L7_2abindS624.$0;
  _M0L6_2atmpS626 = _M0L7_2abindS624.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS627 = _M0FPB7umul128(_M0L1mS622, _M0L7_2amul1S621);
  _M0L6_2alo2S628 = _M0L7_2abindS627.$0;
  _M0L6_2ahi2S629 = _M0L7_2abindS627.$1;
  _M0L3midS630 = _M0L6_2atmpS626 + _M0L6_2alo2S628;
  if (_M0L3midS630 < _M0L6_2atmpS626) {
    _M0L6_2atmpS1842 = 1ull;
  } else {
    _M0L6_2atmpS1842 = 0ull;
  }
  _M0L2hiS631 = _M0L6_2ahi2S629 + _M0L6_2atmpS1842;
  _M0L3lo2S632 = _M0L5_2aloS625 + _M0L7_2amul0S619;
  _M0L6_2atmpS1840 = _M0L3midS630 + _M0L7_2amul1S621;
  if (_M0L3lo2S632 < _M0L5_2aloS625) {
    _M0L6_2atmpS1841 = 1ull;
  } else {
    _M0L6_2atmpS1841 = 0ull;
  }
  _M0L4mid2S633 = _M0L6_2atmpS1840 + _M0L6_2atmpS1841;
  if (_M0L4mid2S633 < _M0L3midS630) {
    _M0L6_2atmpS1839 = 1ull;
  } else {
    _M0L6_2atmpS1839 = 0ull;
  }
  _M0L3hi2S634 = _M0L2hiS631 + _M0L6_2atmpS1839;
  _M0L6_2atmpS1838 = _M0L1jS636 - 64;
  _M0L6_2atmpS1837 = _M0L6_2atmpS1838 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS635
  = _M0FPB13shiftright128(_M0L4mid2S633, _M0L3hi2S634, _M0L6_2atmpS1837);
  _M0Lm2vmS637 = 0ull;
  if (_M0L7mmShiftS638) {
    uint64_t _M0L3lo3S639 = _M0L5_2aloS625 - _M0L7_2amul0S619;
    uint64_t _M0L6_2atmpS1824 = _M0L3midS630 - _M0L7_2amul1S621;
    uint64_t _M0L6_2atmpS1825;
    uint64_t _M0L4mid3S640;
    uint64_t _M0L6_2atmpS1823;
    uint64_t _M0L3hi3S641;
    int32_t _M0L6_2atmpS1822;
    int32_t _M0L6_2atmpS1821;
    if (_M0L5_2aloS625 < _M0L3lo3S639) {
      _M0L6_2atmpS1825 = 1ull;
    } else {
      _M0L6_2atmpS1825 = 0ull;
    }
    _M0L4mid3S640 = _M0L6_2atmpS1824 - _M0L6_2atmpS1825;
    if (_M0L3midS630 < _M0L4mid3S640) {
      _M0L6_2atmpS1823 = 1ull;
    } else {
      _M0L6_2atmpS1823 = 0ull;
    }
    _M0L3hi3S641 = _M0L2hiS631 - _M0L6_2atmpS1823;
    _M0L6_2atmpS1822 = _M0L1jS636 - 64;
    _M0L6_2atmpS1821 = _M0L6_2atmpS1822 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS637
    = _M0FPB13shiftright128(_M0L4mid3S640, _M0L3hi3S641, _M0L6_2atmpS1821);
  } else {
    uint64_t _M0L3lo3S642 = _M0L5_2aloS625 + _M0L5_2aloS625;
    uint64_t _M0L6_2atmpS1832 = _M0L3midS630 + _M0L3midS630;
    uint64_t _M0L6_2atmpS1833;
    uint64_t _M0L4mid3S643;
    uint64_t _M0L6_2atmpS1830;
    uint64_t _M0L6_2atmpS1831;
    uint64_t _M0L3hi3S644;
    uint64_t _M0L3lo4S645;
    uint64_t _M0L6_2atmpS1828;
    uint64_t _M0L6_2atmpS1829;
    uint64_t _M0L4mid4S646;
    uint64_t _M0L6_2atmpS1827;
    uint64_t _M0L3hi4S647;
    int32_t _M0L6_2atmpS1826;
    if (_M0L3lo3S642 < _M0L5_2aloS625) {
      _M0L6_2atmpS1833 = 1ull;
    } else {
      _M0L6_2atmpS1833 = 0ull;
    }
    _M0L4mid3S643 = _M0L6_2atmpS1832 + _M0L6_2atmpS1833;
    _M0L6_2atmpS1830 = _M0L2hiS631 + _M0L2hiS631;
    if (_M0L4mid3S643 < _M0L3midS630) {
      _M0L6_2atmpS1831 = 1ull;
    } else {
      _M0L6_2atmpS1831 = 0ull;
    }
    _M0L3hi3S644 = _M0L6_2atmpS1830 + _M0L6_2atmpS1831;
    _M0L3lo4S645 = _M0L3lo3S642 - _M0L7_2amul0S619;
    _M0L6_2atmpS1828 = _M0L4mid3S643 - _M0L7_2amul1S621;
    if (_M0L3lo3S642 < _M0L3lo4S645) {
      _M0L6_2atmpS1829 = 1ull;
    } else {
      _M0L6_2atmpS1829 = 0ull;
    }
    _M0L4mid4S646 = _M0L6_2atmpS1828 - _M0L6_2atmpS1829;
    if (_M0L4mid3S643 < _M0L4mid4S646) {
      _M0L6_2atmpS1827 = 1ull;
    } else {
      _M0L6_2atmpS1827 = 0ull;
    }
    _M0L3hi4S647 = _M0L3hi3S644 - _M0L6_2atmpS1827;
    _M0L6_2atmpS1826 = _M0L1jS636 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS637
    = _M0FPB13shiftright128(_M0L4mid4S646, _M0L3hi4S647, _M0L6_2atmpS1826);
  }
  _M0L6_2atmpS1836 = _M0L1jS636 - 64;
  _M0L6_2atmpS1835 = _M0L6_2atmpS1836 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS648
  = _M0FPB13shiftright128(_M0L3midS630, _M0L2hiS631, _M0L6_2atmpS1835);
  _M0L6_2atmpS1834 = _M0Lm2vmS637;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS648,
                                                .$1 = _M0L2vpS635,
                                                .$2 = _M0L6_2atmpS1834};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS617,
  int32_t _M0L1pS618
) {
  uint64_t _M0L6_2atmpS1820;
  uint64_t _M0L6_2atmpS1819;
  uint64_t _M0L6_2atmpS1818;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1820 = 1ull << (_M0L1pS618 & 63);
  _M0L6_2atmpS1819 = _M0L6_2atmpS1820 - 1ull;
  _M0L6_2atmpS1818 = _M0L5valueS617 & _M0L6_2atmpS1819;
  return _M0L6_2atmpS1818 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS615,
  int32_t _M0L1pS616
) {
  int32_t _M0L6_2atmpS1817;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1817 = _M0FPB10pow5Factor(_M0L5valueS615);
  return _M0L6_2atmpS1817 >= _M0L1pS616;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS610) {
  uint64_t _M0L6_2atmpS1808;
  uint64_t _M0L6_2atmpS1809;
  uint64_t _M0L6_2atmpS1810;
  uint64_t _M0L6_2atmpS1811;
  uint64_t _M0L6_2atmpS1816;
  int32_t _M0L5countS611;
  uint64_t _M0L1vS612;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1808 = _M0L5valueS610 % 5ull;
  if (_M0L6_2atmpS1808 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1809 = _M0L5valueS610 % 25ull;
  if (_M0L6_2atmpS1809 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1810 = _M0L5valueS610 % 125ull;
  if (_M0L6_2atmpS1810 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1811 = _M0L5valueS610 % 625ull;
  if (_M0L6_2atmpS1811 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1816 = _M0L5valueS610 / 625ull;
  _M0L5countS611 = 4;
  _M0L1vS612 = _M0L6_2atmpS1816;
  while (1) {
    if (_M0L1vS612 > 0ull) {
      uint64_t _M0L6_2atmpS1812 = _M0L1vS612 % 5ull;
      int32_t _M0L6_2atmpS1813;
      uint64_t _M0L6_2atmpS1814;
      if (_M0L6_2atmpS1812 != 0ull) {
        return _M0L5countS611;
      }
      _M0L6_2atmpS1813 = _M0L5countS611 + 1;
      _M0L6_2atmpS1814 = _M0L1vS612 / 5ull;
      _M0L5countS611 = _M0L6_2atmpS1813;
      _M0L1vS612 = _M0L6_2atmpS1814;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS614;
      moonbit_string_t _M0L6_2atmpS1815;
      int32_t _result_2833;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS614
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS614, (moonbit_string_t)moonbit_string_literal_19.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS614, _M0L5valueS610);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1815
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS614);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS614);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2833 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1815);
      moonbit_decref_cycle_free(_M0L6_2atmpS1815);
      return _result_2833;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS609,
  uint64_t _M0L2hiS607,
  int32_t _M0L4distS608
) {
  int32_t _M0L6_2atmpS1807;
  uint64_t _M0L6_2atmpS1805;
  uint64_t _M0L6_2atmpS1806;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1807 = 64 - _M0L4distS608;
  _M0L6_2atmpS1805 = _M0L2hiS607 << (_M0L6_2atmpS1807 & 63);
  _M0L6_2atmpS1806 = _M0L2loS609 >> (_M0L4distS608 & 63);
  return _M0L6_2atmpS1805 | _M0L6_2atmpS1806;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS597,
  uint64_t _M0L1bS600
) {
  uint64_t _M0L3aLoS596;
  uint64_t _M0L3aHiS598;
  uint64_t _M0L3bLoS599;
  uint64_t _M0L3bHiS601;
  uint64_t _M0L1xS602;
  uint64_t _M0L6_2atmpS1803;
  uint64_t _M0L6_2atmpS1804;
  uint64_t _M0L1yS603;
  uint64_t _M0L6_2atmpS1801;
  uint64_t _M0L6_2atmpS1802;
  uint64_t _M0L1zS604;
  uint64_t _M0L6_2atmpS1799;
  uint64_t _M0L6_2atmpS1800;
  uint64_t _M0L6_2atmpS1797;
  uint64_t _M0L6_2atmpS1798;
  uint64_t _M0L1wS605;
  uint64_t _M0L2loS606;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS596 = _M0L1aS597 & 4294967295ull;
  _M0L3aHiS598 = _M0L1aS597 >> 32;
  _M0L3bLoS599 = _M0L1bS600 & 4294967295ull;
  _M0L3bHiS601 = _M0L1bS600 >> 32;
  _M0L1xS602 = _M0L3aLoS596 * _M0L3bLoS599;
  _M0L6_2atmpS1803 = _M0L3aHiS598 * _M0L3bLoS599;
  _M0L6_2atmpS1804 = _M0L1xS602 >> 32;
  _M0L1yS603 = _M0L6_2atmpS1803 + _M0L6_2atmpS1804;
  _M0L6_2atmpS1801 = _M0L3aLoS596 * _M0L3bHiS601;
  _M0L6_2atmpS1802 = _M0L1yS603 & 4294967295ull;
  _M0L1zS604 = _M0L6_2atmpS1801 + _M0L6_2atmpS1802;
  _M0L6_2atmpS1799 = _M0L3aHiS598 * _M0L3bHiS601;
  _M0L6_2atmpS1800 = _M0L1yS603 >> 32;
  _M0L6_2atmpS1797 = _M0L6_2atmpS1799 + _M0L6_2atmpS1800;
  _M0L6_2atmpS1798 = _M0L1zS604 >> 32;
  _M0L1wS605 = _M0L6_2atmpS1797 + _M0L6_2atmpS1798;
  _M0L2loS606 = _M0L1aS597 * _M0L1bS600;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS606, .$1 = _M0L1wS605};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS594,
  int32_t _M0L4fromS591,
  int32_t _M0L2toS590
) {
  int32_t _M0L3lenS589;
  int32_t _M0L6_2atmpS1796;
  uint16_t* _M0L6bufferS592;
  int32_t _M0L1iS593;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS589 = _M0L2toS590 - _M0L4fromS591;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1796 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS592
  = (uint16_t*)moonbit_make_string(_M0L3lenS589, _M0L6_2atmpS1796);
  _M0L1iS593 = 0;
  while (1) {
    if (_M0L1iS593 < _M0L3lenS589) {
      int32_t _M0L6_2atmpS1794 = _M0L4fromS591 + _M0L1iS593;
      int32_t _M0L6_2atmpS1793;
      int32_t _M0L6_2atmpS1792;
      int32_t _M0L6_2atmpS1795;
      if (
        _M0L6_2atmpS1794 < 0
        || _M0L6_2atmpS1794 >= Moonbit_array_length(_M0L5bytesS594)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1793 = (int32_t)_M0L5bytesS594[_M0L6_2atmpS1794];
      _M0L6_2atmpS1792 = (uint16_t)_M0L6_2atmpS1793;
      if (
        _M0L1iS593 < 0 || _M0L1iS593 >= Moonbit_array_length(_M0L6bufferS592)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS592[_M0L1iS593] = _M0L6_2atmpS1792;
      _M0L6_2atmpS1795 = _M0L1iS593 + 1;
      _M0L1iS593 = _M0L6_2atmpS1795;
      continue;
    }
    break;
  }
  return _M0L6bufferS592;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS588) {
  int32_t _M0L6_2atmpS1791;
  uint32_t _M0L6_2atmpS1790;
  uint32_t _M0L6_2atmpS1789;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1791 = _M0L1eS588 * 78913;
  _M0L6_2atmpS1790 = *(uint32_t*)&_M0L6_2atmpS1791;
  _M0L6_2atmpS1789 = _M0L6_2atmpS1790 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1789;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS587) {
  int32_t _M0L6_2atmpS1788;
  uint32_t _M0L6_2atmpS1787;
  uint32_t _M0L6_2atmpS1786;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1788 = _M0L1eS587 * 732923;
  _M0L6_2atmpS1787 = *(uint32_t*)&_M0L6_2atmpS1788;
  _M0L6_2atmpS1786 = _M0L6_2atmpS1787 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1786;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS585,
  int32_t _M0L8exponentS586,
  int32_t _M0L8mantissaS583
) {
  moonbit_string_t _M0L1sS584;
  moonbit_string_t _result_2836;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS583) {
    return (moonbit_string_t)moonbit_string_literal_20.data;
  }
  if (_M0L4signS585) {
    _M0L1sS584 = (moonbit_string_t)moonbit_string_literal_21.data;
  } else {
    _M0L1sS584 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS586) {
    moonbit_string_t _result_2835;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2835
    = moonbit_add_string(_M0L1sS584, (moonbit_string_t)moonbit_string_literal_22.data);
    moonbit_decref_cycle_free(_M0L1sS584);
    return _result_2835;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2836
  = moonbit_add_string(_M0L1sS584, (moonbit_string_t)moonbit_string_literal_23.data);
  moonbit_decref_cycle_free(_M0L1sS584);
  return _result_2836;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS582) {
  int32_t _M0L6_2atmpS1785;
  uint32_t _M0L6_2atmpS1784;
  uint32_t _M0L6_2atmpS1783;
  int32_t _M0L6_2atmpS1782;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1785 = _M0L1eS582 * 1217359;
  _M0L6_2atmpS1784 = *(uint32_t*)&_M0L6_2atmpS1785;
  _M0L6_2atmpS1783 = _M0L6_2atmpS1784 >> 19;
  _M0L6_2atmpS1782 = *(int32_t*)&_M0L6_2atmpS1783;
  return _M0L6_2atmpS1782 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS581) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS581 != _M0L4selfS581) {
    return 0;
  } else if (_M0L4selfS581 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS581 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS581;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS580) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS580 != _M0L4selfS580) {
    return 0ll;
  } else if (_M0L4selfS580 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS580 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS580;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS576
) {
  float* _M0L6_2atmpS1778;
  struct _M0TPB5ArrayGfE* _block_2837;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1778 = (float*)moonbit_make_float_array_raw(_M0L3lenS576);
  _block_2837
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2837)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2837->$0 = _M0L6_2atmpS1778;
  _block_2837->$1 = _M0L3lenS576;
  return _block_2837;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS577
) {
  uint8_t* _M0L6_2atmpS1779;
  struct _M0TPB5ArrayGbE* _block_2838;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1779 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS577);
  _block_2838
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2838)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 59, 0);
  _block_2838->$0 = _M0L6_2atmpS1779;
  _block_2838->$1 = _M0L3lenS577;
  return _block_2838;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS578
) {
  int32_t* _M0L6_2atmpS1780;
  struct _M0TPB5ArrayGiE* _block_2839;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1780 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS578);
  _block_2839
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2839)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _block_2839->$0 = _M0L6_2atmpS1780;
  _block_2839->$1 = _M0L3lenS578;
  return _block_2839;
}

struct _M0TPB5ArrayGsE* _M0MPC15array5Array20unsafe__make__uninitGsE(
  int32_t _M0L3lenS579
) {
  moonbit_string_t* _M0L6_2atmpS1781;
  struct _M0TPB5ArrayGsE* _block_2840;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1781
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L3lenS579, (moonbit_string_t)moonbit_string_literal_0.data);
  _block_2840
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_block_2840)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _block_2840->$0 = _M0L6_2atmpS1781;
  _block_2840->$1 = _M0L3lenS579;
  return _block_2840;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS572,
  int32_t _M0L5indexS573
) {
  uint64_t* _M0L6_2atmpS1776;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1776 = _M0L4selfS572;
  if (
    _M0L5indexS573 < 0
    || _M0L5indexS573 >= Moonbit_array_length(_M0L6_2atmpS1776)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1776[_M0L5indexS573];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS574,
  int32_t _M0L5indexS575
) {
  uint32_t* _M0L6_2atmpS1777;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1777 = _M0L4selfS574;
  if (
    _M0L5indexS575 < 0
    || _M0L5indexS575 >= Moonbit_array_length(_M0L6_2atmpS1777)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1777[_M0L5indexS575];
}

int32_t _M0IPC15array5ArrayPB4Show6outputGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS571,
  struct _M0TPB6Logger _M0L6loggerS570
) {
  struct _M0TPB4IterGfE* _M0L6_2atmpS1775;
  #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 269 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1775 = _M0MPC15array5Array4iterGfE(_M0L4selfS571);
  #line 269 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPB6Logger19write__iter_2einnerGfE(_M0L6loggerS570, _M0L6_2atmpS1775, (moonbit_string_t)moonbit_string_literal_24.data, (moonbit_string_t)moonbit_string_literal_25.data, (moonbit_string_t)moonbit_string_literal_26.data, 0);
  moonbit_decref_cycle_free(_M0L6_2atmpS1775);
  return 0;
}

struct _M0TPB4IterGfE* _M0MPC15array5Array4iterGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS569
) {
  float* _M0L3bufS1773;
  int32_t _M0L3lenS1774;
  struct _M0TPB9ArrayViewGfE _M0L6_2atmpS1772;
  struct _M0TPB4IterGfE* _result_2841;
  #line 1749 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3bufS1773 = _M0L4selfS569->$0;
  _M0L3lenS1774 = _M0L4selfS569->$1;
  moonbit_incref_cycle_free(_M0L3bufS1773);
  _M0L6_2atmpS1772
  = (struct _M0TPB9ArrayViewGfE){
    .$0 = _M0L3bufS1773, .$1 = 0, .$2 = _M0L3lenS1774
  };
  #line 1751 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _result_2841 = _M0MPC15array9ArrayView4iterGfE(_M0L6_2atmpS1772);
  moonbit_decref_cycle_free(_M0L6_2atmpS1772.$0);
  return _result_2841;
}

struct _M0TPB4IterGfE* _M0MPC15array9ArrayView4iterGfE(
  struct _M0TPB9ArrayViewGfE _M0L4selfS567
) {
  struct _M0TPB8MutLocalGiE* _M0L1iS565;
  int32_t _M0L3endS1770;
  int32_t _M0L5startS1771;
  int32_t _M0L3lenS566;
  struct _M0R58ArrayView_3a_3aiter_7c_5bFloat_5d_7c_2eanon__u1760__l885__* _closure_2842;
  struct _M0TWERPC16option6OptionGfE* _M0L6_2atmpS1758;
  int64_t _M0L6_2atmpS1759;
  #line 880 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arrayview.mbt"
  _M0L1iS565
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS565)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS565->$0 = 0;
  _M0L3endS1770 = _M0L4selfS567.$2;
  _M0L5startS1771 = _M0L4selfS567.$1;
  _M0L3lenS566 = _M0L3endS1770 - _M0L5startS1771;
  moonbit_incref_cycle_free(_M0L4selfS567.$0);
  _closure_2842
  = (struct _M0R58ArrayView_3a_3aiter_7c_5bFloat_5d_7c_2eanon__u1760__l885__*)moonbit_malloc(sizeof(struct _M0R58ArrayView_3a_3aiter_7c_5bFloat_5d_7c_2eanon__u1760__l885__));
  Moonbit_object_header(_closure_2842)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 72, 0);
  _closure_2842->code = &_M0MPC15array9ArrayView4iterGfEC1760l885;
  _closure_2842->$0 = _M0L4selfS567;
  _closure_2842->$1 = _M0L3lenS566;
  _closure_2842->$2 = _M0L1iS565;
  _M0L6_2atmpS1758 = (struct _M0TWERPC16option6OptionGfE*)_closure_2842;
  _M0L6_2atmpS1759 = (int64_t)_M0L3lenS566;
  #line 884 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arrayview.mbt"
  return _M0MPB4Iter3newGfE(_M0L6_2atmpS1758, _M0L6_2atmpS1759);
}

void* _M0MPC15array9ArrayView4iterGfEC1760l885(
  struct _M0TWERPC16option6OptionGfE* _M0L6_2aenvS1761
) {
  struct _M0R58ArrayView_3a_3aiter_7c_5bFloat_5d_7c_2eanon__u1760__l885__* _M0L14_2acasted__envS1762;
  struct _M0TPB8MutLocalGiE* _M0L1iS565;
  int32_t _M0L3lenS566;
  struct _M0TPB9ArrayViewGfE _M0L4selfS567;
  int32_t _M0L3valS1763;
  #line 885 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arrayview.mbt"
  _M0L14_2acasted__envS1762
  = (struct _M0R58ArrayView_3a_3aiter_7c_5bFloat_5d_7c_2eanon__u1760__l885__*)_M0L6_2aenvS1761;
  _M0L1iS565 = _M0L14_2acasted__envS1762->$2;
  _M0L3lenS566 = _M0L14_2acasted__envS1762->$1;
  _M0L4selfS567 = _M0L14_2acasted__envS1762->$0;
  _M0L3valS1763 = _M0L1iS565->$0;
  if (_M0L3valS1763 < _M0L3lenS566) {
    float* _M0L3bufS1766 = _M0L4selfS567.$0;
    int32_t _M0L5startS1768 = _M0L4selfS567.$1;
    int32_t _M0L3valS1769 = _M0L1iS565->$0;
    int32_t _M0L6_2atmpS1767 = _M0L5startS1768 + _M0L3valS1769;
    float _M0L4elemS568 = (float)_M0L3bufS1766[_M0L6_2atmpS1767];
    int32_t _M0L3valS1765 = _M0L1iS565->$0;
    int32_t _M0L6_2atmpS1764 = _M0L3valS1765 + 1;
    void* _block_2843;
    _M0L1iS565->$0 = _M0L6_2atmpS1764;
    _block_2843
    = (void*)moonbit_malloc(sizeof(struct _M0DTPC16option6OptionGfE4Some));
    Moonbit_object_header(_block_2843)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 1);
    ((struct _M0DTPC16option6OptionGfE4Some*)_block_2843)->$0 = _M0L4elemS568;
    return _block_2843;
  } else {
    return (struct moonbit_object*)&moonbit_constant_constructor_0 + 1;
  }
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS564
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS564, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS563) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS563, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS562) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS562;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS550,
  float _M0L5valueS552
) {
  int32_t _M0L3lenS1730;
  float* _M0L6_2atmpS1732;
  int32_t _M0L6_2atmpS1731;
  int32_t _M0L6lengthS551;
  float* _M0L3bufS1735;
  int32_t _M0L6_2atmpS1736;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1730 = _M0L4selfS550->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1732 = _M0MPC15array5Array6bufferGfE(_M0L4selfS550);
  _M0L6_2atmpS1731 = Moonbit_array_length(_M0L6_2atmpS1732);
  moonbit_decref_cycle_free(_M0L6_2atmpS1732);
  if (_M0L3lenS1730 == _M0L6_2atmpS1731) {
    int32_t _M0L3lenS1734 = _M0L4selfS550->$1;
    int32_t _M0L6_2atmpS1733 = _M0L3lenS1734 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS550, _M0L6_2atmpS1733);
  }
  _M0L6lengthS551 = _M0L4selfS550->$1;
  _M0L3bufS1735 = _M0L4selfS550->$0;
  _M0L3bufS1735[_M0L6lengthS551] = _M0L5valueS552;
  _M0L6_2atmpS1736 = _M0L6lengthS551 + 1;
  _M0L4selfS550->$1 = _M0L6_2atmpS1736;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS553,
  int32_t _M0L5valueS555
) {
  int32_t _M0L3lenS1737;
  int32_t* _M0L6_2atmpS1739;
  int32_t _M0L6_2atmpS1738;
  int32_t _M0L6lengthS554;
  int32_t* _M0L3bufS1742;
  int32_t _M0L6_2atmpS1743;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1737 = _M0L4selfS553->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1739 = _M0MPC15array5Array6bufferGiE(_M0L4selfS553);
  _M0L6_2atmpS1738 = Moonbit_array_length(_M0L6_2atmpS1739);
  moonbit_decref_cycle_free(_M0L6_2atmpS1739);
  if (_M0L3lenS1737 == _M0L6_2atmpS1738) {
    int32_t _M0L3lenS1741 = _M0L4selfS553->$1;
    int32_t _M0L6_2atmpS1740 = _M0L3lenS1741 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS553, _M0L6_2atmpS1740);
  }
  _M0L6lengthS554 = _M0L4selfS553->$1;
  _M0L3bufS1742 = _M0L4selfS553->$0;
  _M0L3bufS1742[_M0L6lengthS554] = _M0L5valueS555;
  _M0L6_2atmpS1743 = _M0L6lengthS554 + 1;
  _M0L4selfS553->$1 = _M0L6_2atmpS1743;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS556,
  moonbit_string_t _M0L5valueS558
) {
  int32_t _M0L3lenS1744;
  moonbit_string_t* _M0L6_2atmpS1746;
  int32_t _M0L6_2atmpS1745;
  int32_t _M0L6lengthS557;
  moonbit_string_t* _M0L3bufS1749;
  moonbit_string_t _M0L6_2aoldS2720;
  int32_t _M0L6_2atmpS1750;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1744 = _M0L4selfS556->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1746 = _M0MPC15array5Array6bufferGsE(_M0L4selfS556);
  _M0L6_2atmpS1745 = Moonbit_array_length(_M0L6_2atmpS1746);
  moonbit_decref_cycle_free(_M0L6_2atmpS1746);
  if (_M0L3lenS1744 == _M0L6_2atmpS1745) {
    int32_t _M0L3lenS1748 = _M0L4selfS556->$1;
    int32_t _M0L6_2atmpS1747 = _M0L3lenS1748 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS556, _M0L6_2atmpS1747);
  }
  _M0L6lengthS557 = _M0L4selfS556->$1;
  _M0L3bufS1749 = _M0L4selfS556->$0;
  _M0L6_2aoldS2720 = (moonbit_string_t)_M0L3bufS1749[_M0L6lengthS557];
  moonbit_decref_cycle_free(_M0L6_2aoldS2720);
  _M0L3bufS1749[_M0L6lengthS557] = _M0L5valueS558;
  _M0L6_2atmpS1750 = _M0L6lengthS557 + 1;
  _M0L4selfS556->$1 = _M0L6_2atmpS1750;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS559,
  struct _M0TUsiE* _M0L5valueS561
) {
  int32_t _M0L3lenS1751;
  struct _M0TUsiE** _M0L6_2atmpS1753;
  int32_t _M0L6_2atmpS1752;
  int32_t _M0L6lengthS560;
  struct _M0TUsiE** _M0L3bufS1756;
  struct _M0TUsiE* _M0L6_2aoldS2721;
  int32_t _M0L6_2atmpS1757;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1751 = _M0L4selfS559->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1753 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS559);
  _M0L6_2atmpS1752 = Moonbit_array_length(_M0L6_2atmpS1753);
  moonbit_decref_cycle_free(_M0L6_2atmpS1753);
  if (_M0L3lenS1751 == _M0L6_2atmpS1752) {
    int32_t _M0L3lenS1755 = _M0L4selfS559->$1;
    int32_t _M0L6_2atmpS1754 = _M0L3lenS1755 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS559, _M0L6_2atmpS1754);
  }
  _M0L6lengthS560 = _M0L4selfS559->$1;
  _M0L3bufS1756 = _M0L4selfS559->$0;
  _M0L6_2aoldS2721 = (struct _M0TUsiE*)_M0L3bufS1756[_M0L6lengthS560];
  if (_M0L6_2aoldS2721) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2721);
  }
  _M0L3bufS1756[_M0L6lengthS560] = _M0L5valueS561;
  _M0L6_2atmpS1757 = _M0L6lengthS560 + 1;
  _M0L4selfS559->$1 = _M0L6_2atmpS1757;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS535,
  int32_t _M0L8requiredS537
) {
  int32_t _M0L8old__capS534;
  int32_t _M0L3lenS1726;
  int32_t _M0L8new__capS536;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS534 = _M0MPC15array5Array8capacityGfE(_M0L4selfS535);
  _M0L3lenS1726 = _M0L4selfS535->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS536
  = _M0FPB23array__growth__capacity(_M0L8old__capS534, _M0L3lenS1726, _M0L8requiredS537);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS535, _M0L8new__capS536);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS539,
  int32_t _M0L8requiredS541
) {
  int32_t _M0L8old__capS538;
  int32_t _M0L3lenS1727;
  int32_t _M0L8new__capS540;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS538 = _M0MPC15array5Array8capacityGiE(_M0L4selfS539);
  _M0L3lenS1727 = _M0L4selfS539->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS540
  = _M0FPB23array__growth__capacity(_M0L8old__capS538, _M0L3lenS1727, _M0L8requiredS541);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS539, _M0L8new__capS540);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS543,
  int32_t _M0L8requiredS545
) {
  int32_t _M0L8old__capS542;
  int32_t _M0L3lenS1728;
  int32_t _M0L8new__capS544;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS542 = _M0MPC15array5Array8capacityGsE(_M0L4selfS543);
  _M0L3lenS1728 = _M0L4selfS543->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS544
  = _M0FPB23array__growth__capacity(_M0L8old__capS542, _M0L3lenS1728, _M0L8requiredS545);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS543, _M0L8new__capS544);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS547,
  int32_t _M0L8requiredS549
) {
  int32_t _M0L8old__capS546;
  int32_t _M0L3lenS1729;
  int32_t _M0L8new__capS548;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS546 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS547);
  _M0L3lenS1729 = _M0L4selfS547->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS548
  = _M0FPB23array__growth__capacity(_M0L8old__capS546, _M0L3lenS1729, _M0L8requiredS549);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS547, _M0L8new__capS548);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS511,
  int32_t _M0L13new__capacityS514
) {
  float* _M0L8old__bufS510;
  int32_t _M0L3lenS512;
  int32_t _M0L9copy__lenS513;
  float* _M0L8new__bufS515;
  float* _M0L6_2aoldS2722;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS510 = _M0L4selfS511->$0;
  _M0L3lenS512 = _M0L4selfS511->$1;
  if (_M0L3lenS512 < _M0L13new__capacityS514) {
    _M0L9copy__lenS513 = _M0L3lenS512;
  } else {
    _M0L9copy__lenS513 = _M0L13new__capacityS514;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS510);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS515
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS510, _M0L13new__capacityS514, _M0L9copy__lenS513, 0, 0);
  _M0L6_2aoldS2722 = _M0L4selfS511->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2722);
  _M0L4selfS511->$0 = _M0L8new__bufS515;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS517,
  int32_t _M0L13new__capacityS520
) {
  int32_t* _M0L8old__bufS516;
  int32_t _M0L3lenS518;
  int32_t _M0L9copy__lenS519;
  int32_t* _M0L8new__bufS521;
  int32_t* _M0L6_2aoldS2723;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS516 = _M0L4selfS517->$0;
  _M0L3lenS518 = _M0L4selfS517->$1;
  if (_M0L3lenS518 < _M0L13new__capacityS520) {
    _M0L9copy__lenS519 = _M0L3lenS518;
  } else {
    _M0L9copy__lenS519 = _M0L13new__capacityS520;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS516);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS521
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS516, _M0L13new__capacityS520, _M0L9copy__lenS519, 0, 0);
  _M0L6_2aoldS2723 = _M0L4selfS517->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2723);
  _M0L4selfS517->$0 = _M0L8new__bufS521;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS523,
  int32_t _M0L13new__capacityS526
) {
  moonbit_string_t* _M0L8old__bufS522;
  int32_t _M0L3lenS524;
  int32_t _M0L9copy__lenS525;
  moonbit_string_t* _M0L8new__bufS527;
  moonbit_string_t* _M0L6_2aoldS2724;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS522 = _M0L4selfS523->$0;
  _M0L3lenS524 = _M0L4selfS523->$1;
  if (_M0L3lenS524 < _M0L13new__capacityS526) {
    _M0L9copy__lenS525 = _M0L3lenS524;
  } else {
    _M0L9copy__lenS525 = _M0L13new__capacityS526;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS522);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS527
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS522, _M0L13new__capacityS526, _M0L9copy__lenS525, 0, 0);
  _M0L6_2aoldS2724 = _M0L4selfS523->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2724);
  _M0L4selfS523->$0 = _M0L8new__bufS527;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS529,
  int32_t _M0L13new__capacityS532
) {
  struct _M0TUsiE** _M0L8old__bufS528;
  int32_t _M0L3lenS530;
  int32_t _M0L9copy__lenS531;
  struct _M0TUsiE** _M0L8new__bufS533;
  struct _M0TUsiE** _M0L6_2aoldS2725;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS528 = _M0L4selfS529->$0;
  _M0L3lenS530 = _M0L4selfS529->$1;
  if (_M0L3lenS530 < _M0L13new__capacityS532) {
    _M0L9copy__lenS531 = _M0L3lenS530;
  } else {
    _M0L9copy__lenS531 = _M0L13new__capacityS532;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS528);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS533
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS528, _M0L13new__capacityS532, _M0L9copy__lenS531, 0, 0);
  _M0L6_2aoldS2725 = _M0L4selfS529->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2725);
  _M0L4selfS529->$0 = _M0L8new__bufS533;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS506
) {
  float* _M0L6_2atmpS1722;
  int32_t _result_2844;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1722 = _M0MPC15array5Array6bufferGfE(_M0L4selfS506);
  _result_2844 = Moonbit_array_length(_M0L6_2atmpS1722);
  moonbit_decref_cycle_free(_M0L6_2atmpS1722);
  return _result_2844;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS507
) {
  int32_t* _M0L6_2atmpS1723;
  int32_t _result_2845;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1723 = _M0MPC15array5Array6bufferGiE(_M0L4selfS507);
  _result_2845 = Moonbit_array_length(_M0L6_2atmpS1723);
  moonbit_decref_cycle_free(_M0L6_2atmpS1723);
  return _result_2845;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS508
) {
  moonbit_string_t* _M0L6_2atmpS1724;
  int32_t _result_2846;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1724 = _M0MPC15array5Array6bufferGsE(_M0L4selfS508);
  _result_2846 = Moonbit_array_length(_M0L6_2atmpS1724);
  moonbit_decref_cycle_free(_M0L6_2atmpS1724);
  return _result_2846;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS509
) {
  struct _M0TUsiE** _M0L6_2atmpS1725;
  int32_t _result_2847;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1725 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS509);
  _result_2847 = Moonbit_array_length(_M0L6_2atmpS1725);
  moonbit_decref_cycle_free(_M0L6_2atmpS1725);
  return _result_2847;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS502,
  int32_t _M0L3lenS500,
  int32_t _M0L8requiredS499
) {
  int32_t _M0L5startS501;
  int32_t _M0L5spaceS503;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS499 < _M0L3lenS500) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_27.data);
  }
  if (_M0L7currentS502 == 0) {
    _M0L5startS501 = 8;
  } else {
    _M0L5startS501 = _M0L7currentS502;
  }
  _M0L5spaceS503 = _M0L5startS501;
  while (1) {
    if (_M0L5spaceS503 < _M0L8requiredS499) {
      int32_t _M0L4nextS504 = _M0L5spaceS503 * 2;
      if (_M0L4nextS504 <= _M0L5spaceS503) {
        return _M0L8requiredS499;
      }
      _M0L5spaceS503 = _M0L4nextS504;
      continue;
    } else {
      return _M0L5spaceS503;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS497) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS497->$1;
}

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE* _M0L4selfS498) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS498->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS492) {
  float* _M0L8_2afieldS2726;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2726 = _M0L4selfS492->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2726);
  return _M0L8_2afieldS2726;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS493) {
  int32_t* _M0L8_2afieldS2727;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2727 = _M0L4selfS493->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2727);
  return _M0L8_2afieldS2727;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS494) {
  uint8_t* _M0L8_2afieldS2728;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2728 = _M0L4selfS494->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2728);
  return _M0L8_2afieldS2728;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS495
) {
  moonbit_string_t* _M0L8_2afieldS2729;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2729 = _M0L4selfS495->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2729);
  return _M0L8_2afieldS2729;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS496
) {
  struct _M0TUsiE** _M0L8_2afieldS2730;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2730 = _M0L4selfS496->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2730);
  return _M0L8_2afieldS2730;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS491
) {
  #line 220 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref_cycle_free(_M0L4selfS491);
  return _M0L4selfS491;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS490,
  struct _M0TPC16string10StringView _M0L3strS488
) {
  int32_t _M0L3endS1720;
  int32_t _M0L5startS1721;
  int32_t _M0L8str__lenS487;
  int32_t _M0L3lenS1719;
  int32_t _M0L8requiredS489;
  uint16_t* _M0L4dataS1712;
  int32_t _M0L6_2atmpS1711;
  int32_t _if__result_2849;
  uint16_t* _M0L4dataS1713;
  int32_t _M0L3lenS1714;
  moonbit_string_t _M0L6_2atmpS1715;
  int32_t _M0L6_2atmpS1716;
  int32_t _M0L3lenS1718;
  int32_t _M0L6_2atmpS1717;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1720 = _M0L3strS488.$2;
  _M0L5startS1721 = _M0L3strS488.$1;
  _M0L8str__lenS487 = _M0L3endS1720 - _M0L5startS1721;
  if (_M0L8str__lenS487 == 0) {
    return 0;
  }
  _M0L3lenS1719 = _M0L4selfS490->$1;
  _M0L8requiredS489 = _M0L3lenS1719 + _M0L8str__lenS487;
  _M0L4dataS1712 = _M0L4selfS490->$0;
  _M0L6_2atmpS1711 = Moonbit_array_length(_M0L4dataS1712);
  if (_M0L8requiredS489 > _M0L6_2atmpS1711) {
    _if__result_2849 = 1;
  } else {
    int32_t _M0L3lenS1710 = _M0L4selfS490->$1;
    _if__result_2849 = _M0L8requiredS489 < _M0L3lenS1710;
  }
  if (_if__result_2849) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS490, _M0L8requiredS489);
  }
  _M0L4dataS1713 = _M0L4selfS490->$0;
  _M0L3lenS1714 = _M0L4selfS490->$1;
  moonbit_incref_cycle_free(_M0L4dataS1713);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1715 = _M0MPC16string10StringView4data(_M0L3strS488);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1716 = _M0MPC16string10StringView13start__offset(_M0L3strS488);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1713, _M0L3lenS1714, _M0L6_2atmpS1715, _M0L6_2atmpS1716, _M0L8str__lenS487);
  moonbit_decref_cycle_free(_M0L4dataS1713);
  moonbit_decref_cycle_free(_M0L6_2atmpS1715);
  _M0L3lenS1718 = _M0L4selfS490->$1;
  _M0L6_2atmpS1717 = _M0L3lenS1718 + _M0L8str__lenS487;
  _M0L4selfS490->$1 = _M0L6_2atmpS1717;
  return 0;
}

moonbit_string_t _M0MPC16string6String4make(
  int32_t _M0L6lengthS482,
  int32_t _M0L5valueS483
) {
  #line 26 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L6lengthS482 >= 0) {
    int32_t _M0L6_2atmpS1707 = _M0L5valueS483;
    if (_M0L6_2atmpS1707 <= 65535) {
      #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
      return _M0FPB20unsafe__make__string(_M0L6lengthS482, _M0L5valueS483);
    } else {
      int32_t _M0L6_2atmpS1709 = 2 * _M0L6lengthS482;
      struct _M0TPB13StringBuilder* _M0L3bufS484;
      int32_t _M0L2__S485;
      moonbit_string_t _result_2851;
      #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
      _M0L3bufS484
      = _M0MPB13StringBuilder21StringBuilder_2einner(_M0L6_2atmpS1709);
      _M0L2__S485 = 0;
      while (1) {
        if (_M0L2__S485 < _M0L6lengthS482) {
          int32_t _M0L6_2atmpS1708;
          #line 33 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
          _M0IPB13StringBuilderPB6Logger11write__char(_M0L3bufS484, _M0L5valueS483);
          _M0L6_2atmpS1708 = _M0L2__S485 + 1;
          _M0L2__S485 = _M0L6_2atmpS1708;
          continue;
        }
        break;
      }
      #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
      _result_2851 = _M0MPB13StringBuilder10to__string(_M0L3bufS484);
      moonbit_decref_cycle_free(_M0L3bufS484);
      return _result_2851;
    }
  } else {
    #line 27 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
    return _M0FPC15abort5abortGsE((moonbit_string_t)moonbit_string_literal_28.data);
  }
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS479,
  int32_t _M0L5startS477,
  int32_t _M0L3endS478
) {
  int32_t _if__result_2852;
  int32_t _M0L3lenS480;
  int32_t _M0L6_2atmpS1706;
  moonbit_bytes_t _M0L5bytesS481;
  moonbit_bytes_t _M0L6_2atmpS1705;
  moonbit_string_t _result_2853;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS477 == 0) {
    int32_t _M0L6_2atmpS1704 = Moonbit_array_length(_M0L3strS479);
    _if__result_2852 = _M0L3endS478 == _M0L6_2atmpS1704;
  } else {
    _if__result_2852 = 0;
  }
  if (_if__result_2852) {
    moonbit_incref_cycle_free(_M0L3strS479);
    return _M0L3strS479;
  }
  _M0L3lenS480 = _M0L3endS478 - _M0L5startS477;
  _M0L6_2atmpS1706 = _M0L3lenS480 * 2;
  _M0L5bytesS481 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1706, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS481, 0, _M0L3strS479, _M0L5startS477, _M0L3lenS480);
  _M0L6_2atmpS1705 = _M0L5bytesS481;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2853
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1705, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1705);
  return _result_2853;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS472,
  int32_t _M0L6offsetS476,
  int64_t _M0L6lengthS474
) {
  int32_t _M0L3lenS471;
  int32_t _M0L6lengthS473;
  int32_t _if__result_2854;
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L3lenS471 = Moonbit_array_length(_M0L4selfS472);
  if (_M0L6lengthS474 == 4294967296ll) {
    _M0L6lengthS473 = _M0L3lenS471 - _M0L6offsetS476;
  } else {
    int64_t _M0L7_2aSomeS475 = _M0L6lengthS474;
    _M0L6lengthS473 = (int32_t)_M0L7_2aSomeS475;
  }
  if (_M0L6offsetS476 >= 0) {
    if (_M0L6lengthS473 >= 0) {
      int32_t _M0L6_2atmpS1703 = _M0L6offsetS476 + _M0L6lengthS473;
      _if__result_2854 = _M0L6_2atmpS1703 <= _M0L3lenS471;
    } else {
      _if__result_2854 = 0;
    }
  } else {
    _if__result_2854 = 0;
  }
  if (_if__result_2854) {
    moonbit_incref_cycle_free(_M0L4selfS472);
    #line 85 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    return _M0FPB19unsafe__sub__string(_M0L4selfS472, _M0L6offsetS476, _M0L6lengthS473);
  } else {
    #line 84 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array10FixedArray18blit__from__string(
  moonbit_bytes_t _M0L4selfS463,
  int32_t _M0L13bytes__offsetS458,
  moonbit_string_t _M0L3strS465,
  int32_t _M0L11str__offsetS461,
  int32_t _M0L6lengthS459
) {
  int32_t _M0L6_2atmpS1702;
  int32_t _M0L6_2atmpS1701;
  int32_t _M0L2e1S457;
  int32_t _M0L6_2atmpS1700;
  int32_t _M0L2e2S460;
  int32_t _M0L4len1S462;
  int32_t _M0L4len2S464;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1702 = _M0L6lengthS459 * 2;
  _M0L6_2atmpS1701 = _M0L13bytes__offsetS458 + _M0L6_2atmpS1702;
  _M0L2e1S457 = _M0L6_2atmpS1701 - 1;
  _M0L6_2atmpS1700 = _M0L11str__offsetS461 + _M0L6lengthS459;
  _M0L2e2S460 = _M0L6_2atmpS1700 - 1;
  _M0L4len1S462 = Moonbit_array_length(_M0L4selfS463);
  _M0L4len2S464 = Moonbit_array_length(_M0L3strS465);
  if (
    _M0L6lengthS459 >= 0
    && _M0L13bytes__offsetS458 >= 0
    && _M0L2e1S457 < _M0L4len1S462
    && _M0L11str__offsetS461 >= 0
    && _M0L2e2S460 < _M0L4len2S464
  ) {
    int32_t _M0L16end__str__offsetS466 =
      _M0L11str__offsetS461 + _M0L6lengthS459;
    int32_t _M0L1iS467 = _M0L11str__offsetS461;
    int32_t _M0L1jS468 = _M0L13bytes__offsetS458;
    while (1) {
      if (_M0L1iS467 < _M0L16end__str__offsetS466) {
        int32_t _M0L6_2atmpS1697 = _M0L3strS465[_M0L1iS467];
        int32_t _M0L6_2atmpS1696 = (int32_t)_M0L6_2atmpS1697;
        uint32_t _M0L1cS469 = *(uint32_t*)&_M0L6_2atmpS1696;
        uint32_t _M0L6_2atmpS1692 = _M0L1cS469 & 255u;
        int32_t _M0L6_2atmpS1691;
        int32_t _M0L6_2atmpS1693;
        uint32_t _M0L6_2atmpS1695;
        int32_t _M0L6_2atmpS1694;
        int32_t _M0L6_2atmpS1698;
        int32_t _M0L6_2atmpS1699;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1691 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1692);
        if (
          _M0L1jS468 < 0 || _M0L1jS468 >= Moonbit_array_length(_M0L4selfS463)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS463[_M0L1jS468] = _M0L6_2atmpS1691;
        _M0L6_2atmpS1693 = _M0L1jS468 + 1;
        _M0L6_2atmpS1695 = _M0L1cS469 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1694 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1695);
        if (
          _M0L6_2atmpS1693 < 0
          || _M0L6_2atmpS1693 >= Moonbit_array_length(_M0L4selfS463)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS463[_M0L6_2atmpS1693] = _M0L6_2atmpS1694;
        _M0L6_2atmpS1698 = _M0L1iS467 + 1;
        _M0L6_2atmpS1699 = _M0L1jS468 + 2;
        _M0L1iS467 = _M0L6_2atmpS1698;
        _M0L1jS468 = _M0L6_2atmpS1699;
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

int32_t _M0MPC14uint4UInt8to__byte(uint32_t _M0L4selfS456) {
  int32_t _M0L6_2atmpS1690;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1690 = *(int32_t*)&_M0L4selfS456;
  return _M0L6_2atmpS1690 & 0xff;
}

struct _M0TPB4IterGfE* _M0MPB4Iter3newGfE(
  struct _M0TWERPC16option6OptionGfE* _M0L1fS455,
  int64_t _M0L10size__hintS452
) {
  int64_t _M0L10size__hintS451;
  struct _M0TPB4IterGfE* _block_2856;
  #line 240 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\iterator.mbt"
  if (_M0L10size__hintS452 == 4294967296ll) {
    _M0L10size__hintS451 = 4294967296ll;
  } else {
    int64_t _M0L7_2aSomeS453 = _M0L10size__hintS452;
    int32_t _M0L4_2anS454 = (int32_t)_M0L7_2aSomeS453;
    if (_M0L4_2anS454 > 0) {
      _M0L10size__hintS451 = (int64_t)_M0L4_2anS454;
    } else {
      _M0L10size__hintS451 = _M0MPB4Iter3newN6constrS10992GfE;
    }
  }
  _block_2856
  = (struct _M0TPB4IterGfE*)moonbit_malloc(sizeof(struct _M0TPB4IterGfE));
  Moonbit_object_header(_block_2856)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 76, 0);
  _block_2856->$0 = _M0L1fS455;
  _block_2856->$1 = _M0L10size__hintS451;
  return _block_2856;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS443,
  int32_t _M0L5radixS442
) {
  uint16_t* _M0L6bufferS444;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS442 < 2 || _M0L5radixS442 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_29.data);
  }
  if (_M0L4selfS443 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_18.data;
  }
  switch (_M0L5radixS442) {
    case 10: {
      int32_t _M0L3lenS445;
      uint16_t* _M0L6bufferS446;
      #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS445 = _M0FPB12dec__count64(_M0L4selfS443);
      _M0L6bufferS446 = (uint16_t*)moonbit_make_string(_M0L3lenS445, 0);
      #line 624 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS446, _M0L4selfS443, 0, _M0L3lenS445);
      _M0L6bufferS444 = _M0L6bufferS446;
      break;
    }
    
    case 16: {
      int32_t _M0L3lenS447;
      uint16_t* _M0L6bufferS448;
      #line 628 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS447 = _M0FPB12hex__count64(_M0L4selfS443);
      _M0L6bufferS448 = (uint16_t*)moonbit_make_string(_M0L3lenS447, 0);
      #line 630 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS448, _M0L4selfS443, 0, _M0L3lenS447);
      _M0L6bufferS444 = _M0L6bufferS448;
      break;
    }
    default: {
      int32_t _M0L3lenS449;
      uint16_t* _M0L6bufferS450;
      #line 634 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS449 = _M0FPB14radix__count64(_M0L4selfS443, _M0L5radixS442);
      _M0L6bufferS450 = (uint16_t*)moonbit_make_string(_M0L3lenS449, 0);
      #line 636 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS450, _M0L4selfS443, 0, _M0L3lenS449, _M0L5radixS442);
      _M0L6bufferS444 = _M0L6bufferS450;
      break;
    }
  }
  return _M0L6bufferS444;
}

moonbit_string_t _M0MPC15int645Int6418to__string_2einner(
  int64_t _M0L4selfS426,
  int32_t _M0L5radixS425
) {
  int32_t _M0L12is__negativeS427;
  uint64_t _M0L3numS428;
  uint16_t* _M0L6bufferS429;
  #line 548 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS425 < 2 || _M0L5radixS425 > 36) {
    #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_29.data);
  }
  if (_M0L4selfS426 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_18.data;
  }
  _M0L12is__negativeS427 = _M0L4selfS426 < 0ll;
  if (_M0L12is__negativeS427) {
    int64_t _M0L6_2atmpS1689 = -_M0L4selfS426;
    _M0L3numS428 = *(uint64_t*)&_M0L6_2atmpS1689;
  } else {
    _M0L3numS428 = *(uint64_t*)&_M0L4selfS426;
  }
  switch (_M0L5radixS425) {
    case 10: {
      int32_t _M0L10digit__lenS430;
      int32_t _M0L6_2atmpS1686;
      int32_t _M0L10total__lenS431;
      uint16_t* _M0L6bufferS432;
      int32_t _M0L12digit__startS433;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS430 = _M0FPB12dec__count64(_M0L3numS428);
      if (_M0L12is__negativeS427) {
        _M0L6_2atmpS1686 = 1;
      } else {
        _M0L6_2atmpS1686 = 0;
      }
      _M0L10total__lenS431 = _M0L10digit__lenS430 + _M0L6_2atmpS1686;
      _M0L6bufferS432
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS431, 0);
      if (_M0L12is__negativeS427) {
        _M0L12digit__startS433 = 1;
      } else {
        _M0L12digit__startS433 = 0;
      }
      #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS432, _M0L3numS428, _M0L12digit__startS433, _M0L10total__lenS431);
      _M0L6bufferS429 = _M0L6bufferS432;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS434;
      int32_t _M0L6_2atmpS1687;
      int32_t _M0L10total__lenS435;
      uint16_t* _M0L6bufferS436;
      int32_t _M0L12digit__startS437;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS434 = _M0FPB12hex__count64(_M0L3numS428);
      if (_M0L12is__negativeS427) {
        _M0L6_2atmpS1687 = 1;
      } else {
        _M0L6_2atmpS1687 = 0;
      }
      _M0L10total__lenS435 = _M0L10digit__lenS434 + _M0L6_2atmpS1687;
      _M0L6bufferS436
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS435, 0);
      if (_M0L12is__negativeS427) {
        _M0L12digit__startS437 = 1;
      } else {
        _M0L12digit__startS437 = 0;
      }
      #line 585 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS436, _M0L3numS428, _M0L12digit__startS437, _M0L10total__lenS435);
      _M0L6bufferS429 = _M0L6bufferS436;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS438;
      int32_t _M0L6_2atmpS1688;
      int32_t _M0L10total__lenS439;
      uint16_t* _M0L6bufferS440;
      int32_t _M0L12digit__startS441;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS438
      = _M0FPB14radix__count64(_M0L3numS428, _M0L5radixS425);
      if (_M0L12is__negativeS427) {
        _M0L6_2atmpS1688 = 1;
      } else {
        _M0L6_2atmpS1688 = 0;
      }
      _M0L10total__lenS439 = _M0L10digit__lenS438 + _M0L6_2atmpS1688;
      _M0L6bufferS440
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS439, 0);
      if (_M0L12is__negativeS427) {
        _M0L12digit__startS441 = 1;
      } else {
        _M0L12digit__startS441 = 0;
      }
      #line 593 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS440, _M0L3numS428, _M0L12digit__startS441, _M0L10total__lenS439, _M0L5radixS425);
      _M0L6bufferS429 = _M0L6bufferS440;
      break;
    }
  }
  if (_M0L12is__negativeS427) {
    _M0L6bufferS429[0] = 45;
  }
  return _M0L6bufferS429;
}

int32_t _M0FPB22int64__to__string__dec(
  uint16_t* _M0L6bufferS411,
  uint64_t _M0L3numS423,
  int32_t _M0L12digit__startS412,
  int32_t _M0L10total__lenS424
) {
  int32_t _M0L6_2atmpS1685;
  uint64_t _M0L3numS401;
  int32_t _M0L6offsetS402;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1685 = _M0L10total__lenS424 - _M0L12digit__startS412;
  _M0L3numS401 = _M0L3numS423;
  _M0L6offsetS402 = _M0L6_2atmpS1685;
  while (1) {
    if (_M0L3numS401 >= 10000ull) {
      uint64_t _M0L1tS403 = _M0L3numS401 / 10000ull;
      uint64_t _M0L6_2atmpS1662 = _M0L3numS401 % 10000ull;
      int32_t _M0L1rS404 = (int32_t)_M0L6_2atmpS1662;
      int32_t _M0L2d1S405 = _M0L1rS404 / 100;
      int32_t _M0L2d2S406 = _M0L1rS404 % 100;
      int32_t _M0L6_2atmpS1661 = _M0L2d1S405 / 10;
      int32_t _M0L6_2atmpS1660 = 48 + _M0L6_2atmpS1661;
      int32_t _M0L6d1__hiS407 = (uint16_t)_M0L6_2atmpS1660;
      int32_t _M0L6_2atmpS1659 = _M0L2d1S405 % 10;
      int32_t _M0L6_2atmpS1658 = 48 + _M0L6_2atmpS1659;
      int32_t _M0L6d1__loS408 = (uint16_t)_M0L6_2atmpS1658;
      int32_t _M0L6_2atmpS1657 = _M0L2d2S406 / 10;
      int32_t _M0L6_2atmpS1656 = 48 + _M0L6_2atmpS1657;
      int32_t _M0L6d2__hiS409 = (uint16_t)_M0L6_2atmpS1656;
      int32_t _M0L6_2atmpS1655 = _M0L2d2S406 % 10;
      int32_t _M0L6_2atmpS1654 = 48 + _M0L6_2atmpS1655;
      int32_t _M0L6d2__loS410 = (uint16_t)_M0L6_2atmpS1654;
      int32_t _M0L6_2atmpS1646 = _M0L12digit__startS412 + _M0L6offsetS402;
      int32_t _M0L6_2atmpS1645 = _M0L6_2atmpS1646 - 4;
      int32_t _M0L6_2atmpS1648;
      int32_t _M0L6_2atmpS1647;
      int32_t _M0L6_2atmpS1650;
      int32_t _M0L6_2atmpS1649;
      int32_t _M0L6_2atmpS1652;
      int32_t _M0L6_2atmpS1651;
      int32_t _M0L6_2atmpS1653;
      _M0L6bufferS411[_M0L6_2atmpS1645] = _M0L6d1__hiS407;
      _M0L6_2atmpS1648 = _M0L12digit__startS412 + _M0L6offsetS402;
      _M0L6_2atmpS1647 = _M0L6_2atmpS1648 - 3;
      _M0L6bufferS411[_M0L6_2atmpS1647] = _M0L6d1__loS408;
      _M0L6_2atmpS1650 = _M0L12digit__startS412 + _M0L6offsetS402;
      _M0L6_2atmpS1649 = _M0L6_2atmpS1650 - 2;
      _M0L6bufferS411[_M0L6_2atmpS1649] = _M0L6d2__hiS409;
      _M0L6_2atmpS1652 = _M0L12digit__startS412 + _M0L6offsetS402;
      _M0L6_2atmpS1651 = _M0L6_2atmpS1652 - 1;
      _M0L6bufferS411[_M0L6_2atmpS1651] = _M0L6d2__loS410;
      _M0L6_2atmpS1653 = _M0L6offsetS402 - 4;
      _M0L3numS401 = _M0L1tS403;
      _M0L6offsetS402 = _M0L6_2atmpS1653;
      continue;
    } else {
      int32_t _M0L6_2atmpS1684 = (int32_t)_M0L3numS401;
      int32_t _M0L9remainingS414 = _M0L6_2atmpS1684;
      int32_t _M0L6offsetS415 = _M0L6offsetS402;
      while (1) {
        if (_M0L9remainingS414 >= 100) {
          int32_t _M0L1tS416 = _M0L9remainingS414 / 100;
          int32_t _M0L1dS417 = _M0L9remainingS414 % 100;
          int32_t _M0L6_2atmpS1671 = _M0L1dS417 / 10;
          int32_t _M0L6_2atmpS1670 = 48 + _M0L6_2atmpS1671;
          int32_t _M0L5d__hiS418 = (uint16_t)_M0L6_2atmpS1670;
          int32_t _M0L6_2atmpS1669 = _M0L1dS417 % 10;
          int32_t _M0L6_2atmpS1668 = 48 + _M0L6_2atmpS1669;
          int32_t _M0L5d__loS419 = (uint16_t)_M0L6_2atmpS1668;
          int32_t _M0L6_2atmpS1664 = _M0L12digit__startS412 + _M0L6offsetS415;
          int32_t _M0L6_2atmpS1663 = _M0L6_2atmpS1664 - 2;
          int32_t _M0L6_2atmpS1666;
          int32_t _M0L6_2atmpS1665;
          int32_t _M0L6_2atmpS1667;
          _M0L6bufferS411[_M0L6_2atmpS1663] = _M0L5d__hiS418;
          _M0L6_2atmpS1666 = _M0L12digit__startS412 + _M0L6offsetS415;
          _M0L6_2atmpS1665 = _M0L6_2atmpS1666 - 1;
          _M0L6bufferS411[_M0L6_2atmpS1665] = _M0L5d__loS419;
          _M0L6_2atmpS1667 = _M0L6offsetS415 - 2;
          _M0L9remainingS414 = _M0L1tS416;
          _M0L6offsetS415 = _M0L6_2atmpS1667;
          continue;
        } else if (_M0L9remainingS414 >= 10) {
          int32_t _M0L6_2atmpS1679 = _M0L9remainingS414 / 10;
          int32_t _M0L6_2atmpS1678 = 48 + _M0L6_2atmpS1679;
          int32_t _M0L5d__hiS421 = (uint16_t)_M0L6_2atmpS1678;
          int32_t _M0L6_2atmpS1677 = _M0L9remainingS414 % 10;
          int32_t _M0L6_2atmpS1676 = 48 + _M0L6_2atmpS1677;
          int32_t _M0L5d__loS422 = (uint16_t)_M0L6_2atmpS1676;
          int32_t _M0L6_2atmpS1673 = _M0L12digit__startS412 + _M0L6offsetS415;
          int32_t _M0L6_2atmpS1672 = _M0L6_2atmpS1673 - 2;
          int32_t _M0L6_2atmpS1675;
          int32_t _M0L6_2atmpS1674;
          _M0L6bufferS411[_M0L6_2atmpS1672] = _M0L5d__hiS421;
          _M0L6_2atmpS1675 = _M0L12digit__startS412 + _M0L6offsetS415;
          _M0L6_2atmpS1674 = _M0L6_2atmpS1675 - 1;
          _M0L6bufferS411[_M0L6_2atmpS1674] = _M0L5d__loS422;
        } else {
          int32_t _M0L6_2atmpS1683 = _M0L12digit__startS412 + _M0L6offsetS415;
          int32_t _M0L6_2atmpS1680 = _M0L6_2atmpS1683 - 1;
          int32_t _M0L6_2atmpS1682 = 48 + _M0L9remainingS414;
          int32_t _M0L6_2atmpS1681 = (uint16_t)_M0L6_2atmpS1682;
          _M0L6bufferS411[_M0L6_2atmpS1680] = _M0L6_2atmpS1681;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB26int64__to__string__generic(
  uint16_t* _M0L6bufferS391,
  uint64_t _M0L3numS395,
  int32_t _M0L12digit__startS392,
  int32_t _M0L10total__lenS394,
  int32_t _M0L5radixS385
) {
  uint64_t _M0L4baseS384;
  int32_t _M0L6_2atmpS1630;
  int32_t _M0L6_2atmpS1629;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS384 = _M0MPC13int3Int10to__uint64(_M0L5radixS385);
  _M0L6_2atmpS1630 = _M0L5radixS385 - 1;
  _M0L6_2atmpS1629 = _M0L5radixS385 & _M0L6_2atmpS1630;
  if (_M0L6_2atmpS1629 == 0) {
    int32_t _M0L5shiftS386;
    uint64_t _M0L4maskS387;
    int32_t _M0L6_2atmpS1637;
    int32_t _M0L6offsetS388;
    uint64_t _M0L1nS389;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS386 = moonbit_ctz32(_M0L5radixS385);
    _M0L4maskS387 = _M0L4baseS384 - 1ull;
    _M0L6_2atmpS1637 = _M0L10total__lenS394 - _M0L12digit__startS392;
    _M0L6offsetS388 = _M0L6_2atmpS1637;
    _M0L1nS389 = _M0L3numS395;
    while (1) {
      if (_M0L1nS389 > 0ull) {
        uint64_t _M0L6_2atmpS1636 = _M0L1nS389 & _M0L4maskS387;
        int32_t _M0L5digitS390 = (int32_t)_M0L6_2atmpS1636;
        int32_t _M0L6_2atmpS1633 = _M0L12digit__startS392 + _M0L6offsetS388;
        int32_t _M0L6_2atmpS1631 = _M0L6_2atmpS1633 - 1;
        int32_t _M0L6_2atmpS1632 =
          ((moonbit_string_t)moonbit_string_literal_30.data)[_M0L5digitS390];
        int32_t _M0L6_2atmpS1634;
        uint64_t _M0L6_2atmpS1635;
        _M0L6bufferS391[_M0L6_2atmpS1631] = _M0L6_2atmpS1632;
        _M0L6_2atmpS1634 = _M0L6offsetS388 - 1;
        _M0L6_2atmpS1635 = _M0L1nS389 >> (_M0L5shiftS386 & 63);
        _M0L6offsetS388 = _M0L6_2atmpS1634;
        _M0L1nS389 = _M0L6_2atmpS1635;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1644 = _M0L10total__lenS394 - _M0L12digit__startS392;
    int32_t _M0L6offsetS396 = _M0L6_2atmpS1644;
    uint64_t _M0L1nS397 = _M0L3numS395;
    while (1) {
      if (_M0L1nS397 > 0ull) {
        uint64_t _M0L1qS398 = _M0L1nS397 / _M0L4baseS384;
        uint64_t _M0L6_2atmpS1643 = _M0L1qS398 * _M0L4baseS384;
        uint64_t _M0L6_2atmpS1642 = _M0L1nS397 - _M0L6_2atmpS1643;
        int32_t _M0L5digitS399 = (int32_t)_M0L6_2atmpS1642;
        int32_t _M0L6_2atmpS1640 = _M0L12digit__startS392 + _M0L6offsetS396;
        int32_t _M0L6_2atmpS1638 = _M0L6_2atmpS1640 - 1;
        int32_t _M0L6_2atmpS1639 =
          ((moonbit_string_t)moonbit_string_literal_30.data)[_M0L5digitS399];
        int32_t _M0L6_2atmpS1641;
        _M0L6bufferS391[_M0L6_2atmpS1638] = _M0L6_2atmpS1639;
        _M0L6_2atmpS1641 = _M0L6offsetS396 - 1;
        _M0L6offsetS396 = _M0L6_2atmpS1641;
        _M0L1nS397 = _M0L1qS398;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB22int64__to__string__hex(
  uint16_t* _M0L6bufferS378,
  uint64_t _M0L3numS383,
  int32_t _M0L12digit__startS379,
  int32_t _M0L10total__lenS382
) {
  int32_t _M0L6_2atmpS1628;
  int32_t _M0L6offsetS373;
  uint64_t _M0L1nS374;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1628 = _M0L10total__lenS382 - _M0L12digit__startS379;
  _M0L6offsetS373 = _M0L6_2atmpS1628;
  _M0L1nS374 = _M0L3numS383;
  while (1) {
    if (_M0L6offsetS373 >= 2) {
      uint64_t _M0L6_2atmpS1625 = _M0L1nS374 & 255ull;
      int32_t _M0L9byte__valS375 = (int32_t)_M0L6_2atmpS1625;
      int32_t _M0L2hiS376 = _M0L9byte__valS375 / 16;
      int32_t _M0L2loS377 = _M0L9byte__valS375 % 16;
      int32_t _M0L6_2atmpS1619 = _M0L12digit__startS379 + _M0L6offsetS373;
      int32_t _M0L6_2atmpS1617 = _M0L6_2atmpS1619 - 2;
      int32_t _M0L6_2atmpS1618 =
        ((moonbit_string_t)moonbit_string_literal_30.data)[_M0L2hiS376];
      int32_t _M0L6_2atmpS1622;
      int32_t _M0L6_2atmpS1620;
      int32_t _M0L6_2atmpS1621;
      int32_t _M0L6_2atmpS1623;
      uint64_t _M0L6_2atmpS1624;
      _M0L6bufferS378[_M0L6_2atmpS1617] = _M0L6_2atmpS1618;
      _M0L6_2atmpS1622 = _M0L12digit__startS379 + _M0L6offsetS373;
      _M0L6_2atmpS1620 = _M0L6_2atmpS1622 - 1;
      _M0L6_2atmpS1621
      = ((moonbit_string_t)moonbit_string_literal_30.data)[
        _M0L2loS377
      ];
      _M0L6bufferS378[_M0L6_2atmpS1620] = _M0L6_2atmpS1621;
      _M0L6_2atmpS1623 = _M0L6offsetS373 - 2;
      _M0L6_2atmpS1624 = _M0L1nS374 >> 8;
      _M0L6offsetS373 = _M0L6_2atmpS1623;
      _M0L1nS374 = _M0L6_2atmpS1624;
      continue;
    } else if (_M0L6offsetS373 == 1) {
      uint64_t _M0L6_2atmpS1627 = _M0L1nS374 & 15ull;
      int32_t _M0L6nibbleS381 = (int32_t)_M0L6_2atmpS1627;
      int32_t _M0L6_2atmpS1626 =
        ((moonbit_string_t)moonbit_string_literal_30.data)[_M0L6nibbleS381];
      _M0L6bufferS378[_M0L12digit__startS379] = _M0L6_2atmpS1626;
    }
    break;
  }
  return 0;
}

int32_t _M0FPB14radix__count64(
  uint64_t _M0L5valueS367,
  int32_t _M0L5radixS369
) {
  uint64_t _M0L4baseS368;
  uint64_t _M0L3numS370;
  int32_t _M0L5countS371;
  #line 419 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS367 == 0ull) {
    return 1;
  }
  #line 424 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS368 = _M0MPC13int3Int10to__uint64(_M0L5radixS369);
  _M0L3numS370 = _M0L5valueS367;
  _M0L5countS371 = 0;
  while (1) {
    if (_M0L3numS370 > 0ull) {
      uint64_t _M0L6_2atmpS1615 = _M0L3numS370 / _M0L4baseS368;
      int32_t _M0L6_2atmpS1616 = _M0L5countS371 + 1;
      _M0L3numS370 = _M0L6_2atmpS1615;
      _M0L5countS371 = _M0L6_2atmpS1616;
      continue;
    } else {
      return _M0L5countS371;
    }
    break;
  }
}

int32_t _M0FPB12hex__count64(uint64_t _M0L5valueS365) {
  #line 407 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS365 == 0ull) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS366;
    int32_t _M0L6_2atmpS1614;
    int32_t _M0L6_2atmpS1613;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS366 = moonbit_clz64(_M0L5valueS365);
    _M0L6_2atmpS1614 = 63 - _M0L14leading__zerosS366;
    _M0L6_2atmpS1613 = _M0L6_2atmpS1614 / 4;
    return _M0L6_2atmpS1613 + 1;
  }
}

int32_t _M0FPB12dec__count64(uint64_t _M0L5valueS364) {
  #line 343 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS364 >= 10000000000ull) {
    if (_M0L5valueS364 >= 100000000000000ull) {
      if (_M0L5valueS364 >= 10000000000000000ull) {
        if (_M0L5valueS364 >= 1000000000000000000ull) {
          if (_M0L5valueS364 >= 10000000000000000000ull) {
            return 20;
          } else {
            return 19;
          }
        } else if (_M0L5valueS364 >= 100000000000000000ull) {
          return 18;
        } else {
          return 17;
        }
      } else if (_M0L5valueS364 >= 1000000000000000ull) {
        return 16;
      } else {
        return 15;
      }
    } else if (_M0L5valueS364 >= 1000000000000ull) {
      if (_M0L5valueS364 >= 10000000000000ull) {
        return 14;
      } else {
        return 13;
      }
    } else if (_M0L5valueS364 >= 100000000000ull) {
      return 12;
    } else {
      return 11;
    }
  } else if (_M0L5valueS364 >= 100000ull) {
    if (_M0L5valueS364 >= 10000000ull) {
      if (_M0L5valueS364 >= 1000000000ull) {
        return 10;
      } else if (_M0L5valueS364 >= 100000000ull) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS364 >= 1000000ull) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS364 >= 1000ull) {
    if (_M0L5valueS364 >= 10000ull) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS364 >= 100ull) {
    return 3;
  } else if (_M0L5valueS364 >= 10ull) {
    return 2;
  } else {
    return 1;
  }
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS348,
  int32_t _M0L5radixS347
) {
  int32_t _M0L12is__negativeS349;
  uint32_t _M0L3numS350;
  uint16_t* _M0L6bufferS351;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS347 < 2 || _M0L5radixS347 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_29.data);
  }
  if (_M0L4selfS348 == 0) {
    return (moonbit_string_t)moonbit_string_literal_18.data;
  }
  _M0L12is__negativeS349 = _M0L4selfS348 < 0;
  if (_M0L12is__negativeS349) {
    int32_t _M0L6_2atmpS1612 = -_M0L4selfS348;
    _M0L3numS350 = *(uint32_t*)&_M0L6_2atmpS1612;
  } else {
    _M0L3numS350 = *(uint32_t*)&_M0L4selfS348;
  }
  switch (_M0L5radixS347) {
    case 10: {
      int32_t _M0L10digit__lenS352;
      int32_t _M0L6_2atmpS1609;
      int32_t _M0L10total__lenS353;
      uint16_t* _M0L6bufferS354;
      int32_t _M0L12digit__startS355;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS352 = _M0FPB12dec__count32(_M0L3numS350);
      if (_M0L12is__negativeS349) {
        _M0L6_2atmpS1609 = 1;
      } else {
        _M0L6_2atmpS1609 = 0;
      }
      _M0L10total__lenS353 = _M0L10digit__lenS352 + _M0L6_2atmpS1609;
      _M0L6bufferS354
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS353, 0);
      if (_M0L12is__negativeS349) {
        _M0L12digit__startS355 = 1;
      } else {
        _M0L12digit__startS355 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS354, _M0L3numS350, _M0L12digit__startS355, _M0L10total__lenS353);
      _M0L6bufferS351 = _M0L6bufferS354;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS356;
      int32_t _M0L6_2atmpS1610;
      int32_t _M0L10total__lenS357;
      uint16_t* _M0L6bufferS358;
      int32_t _M0L12digit__startS359;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS356 = _M0FPB12hex__count32(_M0L3numS350);
      if (_M0L12is__negativeS349) {
        _M0L6_2atmpS1610 = 1;
      } else {
        _M0L6_2atmpS1610 = 0;
      }
      _M0L10total__lenS357 = _M0L10digit__lenS356 + _M0L6_2atmpS1610;
      _M0L6bufferS358
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS357, 0);
      if (_M0L12is__negativeS349) {
        _M0L12digit__startS359 = 1;
      } else {
        _M0L12digit__startS359 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS358, _M0L3numS350, _M0L12digit__startS359, _M0L10total__lenS357);
      _M0L6bufferS351 = _M0L6bufferS358;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS360;
      int32_t _M0L6_2atmpS1611;
      int32_t _M0L10total__lenS361;
      uint16_t* _M0L6bufferS362;
      int32_t _M0L12digit__startS363;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS360
      = _M0FPB14radix__count32(_M0L3numS350, _M0L5radixS347);
      if (_M0L12is__negativeS349) {
        _M0L6_2atmpS1611 = 1;
      } else {
        _M0L6_2atmpS1611 = 0;
      }
      _M0L10total__lenS361 = _M0L10digit__lenS360 + _M0L6_2atmpS1611;
      _M0L6bufferS362
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS361, 0);
      if (_M0L12is__negativeS349) {
        _M0L12digit__startS363 = 1;
      } else {
        _M0L12digit__startS363 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS362, _M0L3numS350, _M0L12digit__startS363, _M0L10total__lenS361, _M0L5radixS347);
      _M0L6bufferS351 = _M0L6bufferS362;
      break;
    }
  }
  if (_M0L12is__negativeS349) {
    _M0L6bufferS351[0] = 45;
  }
  return _M0L6bufferS351;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS341,
  int32_t _M0L5radixS343
) {
  uint32_t _M0L4baseS342;
  uint32_t _M0L3numS344;
  int32_t _M0L5countS345;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS341 == 0u) {
    return 1;
  }
  _M0L4baseS342 = *(uint32_t*)&_M0L5radixS343;
  _M0L3numS344 = _M0L5valueS341;
  _M0L5countS345 = 0;
  while (1) {
    if (_M0L3numS344 > 0u) {
      uint32_t _M0L6_2atmpS1607 = _M0L3numS344 / _M0L4baseS342;
      int32_t _M0L6_2atmpS1608 = _M0L5countS345 + 1;
      _M0L3numS344 = _M0L6_2atmpS1607;
      _M0L5countS345 = _M0L6_2atmpS1608;
      continue;
    } else {
      return _M0L5countS345;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS339) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS339 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS340;
    int32_t _M0L6_2atmpS1606;
    int32_t _M0L6_2atmpS1605;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS340 = moonbit_clz32(_M0L5valueS339);
    _M0L6_2atmpS1606 = 31 - _M0L14leading__zerosS340;
    _M0L6_2atmpS1605 = _M0L6_2atmpS1606 / 4;
    return _M0L6_2atmpS1605 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS338) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS338 >= 100000u) {
    if (_M0L5valueS338 >= 10000000u) {
      if (_M0L5valueS338 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS338 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS338 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS338 >= 1000u) {
    if (_M0L5valueS338 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS338 >= 100u) {
    return 3;
  } else if (_M0L5valueS338 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS324,
  uint32_t _M0L3numS336,
  int32_t _M0L12digit__startS325,
  int32_t _M0L10total__lenS337
) {
  int32_t _M0L6_2atmpS1604;
  uint32_t _M0L3numS314;
  int32_t _M0L6offsetS315;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1604 = _M0L10total__lenS337 - _M0L12digit__startS325;
  _M0L3numS314 = _M0L3numS336;
  _M0L6offsetS315 = _M0L6_2atmpS1604;
  while (1) {
    if (_M0L3numS314 >= 10000u) {
      uint32_t _M0L1tS316 = _M0L3numS314 / 10000u;
      uint32_t _M0L6_2atmpS1581 = _M0L3numS314 % 10000u;
      int32_t _M0L1rS317 = *(int32_t*)&_M0L6_2atmpS1581;
      int32_t _M0L2d1S318 = _M0L1rS317 / 100;
      int32_t _M0L2d2S319 = _M0L1rS317 % 100;
      int32_t _M0L6_2atmpS1580 = _M0L2d1S318 / 10;
      int32_t _M0L6_2atmpS1579 = 48 + _M0L6_2atmpS1580;
      int32_t _M0L6d1__hiS320 = (uint16_t)_M0L6_2atmpS1579;
      int32_t _M0L6_2atmpS1578 = _M0L2d1S318 % 10;
      int32_t _M0L6_2atmpS1577 = 48 + _M0L6_2atmpS1578;
      int32_t _M0L6d1__loS321 = (uint16_t)_M0L6_2atmpS1577;
      int32_t _M0L6_2atmpS1576 = _M0L2d2S319 / 10;
      int32_t _M0L6_2atmpS1575 = 48 + _M0L6_2atmpS1576;
      int32_t _M0L6d2__hiS322 = (uint16_t)_M0L6_2atmpS1575;
      int32_t _M0L6_2atmpS1574 = _M0L2d2S319 % 10;
      int32_t _M0L6_2atmpS1573 = 48 + _M0L6_2atmpS1574;
      int32_t _M0L6d2__loS323 = (uint16_t)_M0L6_2atmpS1573;
      int32_t _M0L6_2atmpS1565 = _M0L12digit__startS325 + _M0L6offsetS315;
      int32_t _M0L6_2atmpS1564 = _M0L6_2atmpS1565 - 4;
      int32_t _M0L6_2atmpS1567;
      int32_t _M0L6_2atmpS1566;
      int32_t _M0L6_2atmpS1569;
      int32_t _M0L6_2atmpS1568;
      int32_t _M0L6_2atmpS1571;
      int32_t _M0L6_2atmpS1570;
      int32_t _M0L6_2atmpS1572;
      _M0L6bufferS324[_M0L6_2atmpS1564] = _M0L6d1__hiS320;
      _M0L6_2atmpS1567 = _M0L12digit__startS325 + _M0L6offsetS315;
      _M0L6_2atmpS1566 = _M0L6_2atmpS1567 - 3;
      _M0L6bufferS324[_M0L6_2atmpS1566] = _M0L6d1__loS321;
      _M0L6_2atmpS1569 = _M0L12digit__startS325 + _M0L6offsetS315;
      _M0L6_2atmpS1568 = _M0L6_2atmpS1569 - 2;
      _M0L6bufferS324[_M0L6_2atmpS1568] = _M0L6d2__hiS322;
      _M0L6_2atmpS1571 = _M0L12digit__startS325 + _M0L6offsetS315;
      _M0L6_2atmpS1570 = _M0L6_2atmpS1571 - 1;
      _M0L6bufferS324[_M0L6_2atmpS1570] = _M0L6d2__loS323;
      _M0L6_2atmpS1572 = _M0L6offsetS315 - 4;
      _M0L3numS314 = _M0L1tS316;
      _M0L6offsetS315 = _M0L6_2atmpS1572;
      continue;
    } else {
      int32_t _M0L6_2atmpS1603 = *(int32_t*)&_M0L3numS314;
      int32_t _M0L9remainingS327 = _M0L6_2atmpS1603;
      int32_t _M0L6offsetS328 = _M0L6offsetS315;
      while (1) {
        if (_M0L9remainingS327 >= 100) {
          int32_t _M0L1tS329 = _M0L9remainingS327 / 100;
          int32_t _M0L1dS330 = _M0L9remainingS327 % 100;
          int32_t _M0L6_2atmpS1590 = _M0L1dS330 / 10;
          int32_t _M0L6_2atmpS1589 = 48 + _M0L6_2atmpS1590;
          int32_t _M0L5d__hiS331 = (uint16_t)_M0L6_2atmpS1589;
          int32_t _M0L6_2atmpS1588 = _M0L1dS330 % 10;
          int32_t _M0L6_2atmpS1587 = 48 + _M0L6_2atmpS1588;
          int32_t _M0L5d__loS332 = (uint16_t)_M0L6_2atmpS1587;
          int32_t _M0L6_2atmpS1583 = _M0L12digit__startS325 + _M0L6offsetS328;
          int32_t _M0L6_2atmpS1582 = _M0L6_2atmpS1583 - 2;
          int32_t _M0L6_2atmpS1585;
          int32_t _M0L6_2atmpS1584;
          int32_t _M0L6_2atmpS1586;
          _M0L6bufferS324[_M0L6_2atmpS1582] = _M0L5d__hiS331;
          _M0L6_2atmpS1585 = _M0L12digit__startS325 + _M0L6offsetS328;
          _M0L6_2atmpS1584 = _M0L6_2atmpS1585 - 1;
          _M0L6bufferS324[_M0L6_2atmpS1584] = _M0L5d__loS332;
          _M0L6_2atmpS1586 = _M0L6offsetS328 - 2;
          _M0L9remainingS327 = _M0L1tS329;
          _M0L6offsetS328 = _M0L6_2atmpS1586;
          continue;
        } else if (_M0L9remainingS327 >= 10) {
          int32_t _M0L6_2atmpS1598 = _M0L9remainingS327 / 10;
          int32_t _M0L6_2atmpS1597 = 48 + _M0L6_2atmpS1598;
          int32_t _M0L5d__hiS334 = (uint16_t)_M0L6_2atmpS1597;
          int32_t _M0L6_2atmpS1596 = _M0L9remainingS327 % 10;
          int32_t _M0L6_2atmpS1595 = 48 + _M0L6_2atmpS1596;
          int32_t _M0L5d__loS335 = (uint16_t)_M0L6_2atmpS1595;
          int32_t _M0L6_2atmpS1592 = _M0L12digit__startS325 + _M0L6offsetS328;
          int32_t _M0L6_2atmpS1591 = _M0L6_2atmpS1592 - 2;
          int32_t _M0L6_2atmpS1594;
          int32_t _M0L6_2atmpS1593;
          _M0L6bufferS324[_M0L6_2atmpS1591] = _M0L5d__hiS334;
          _M0L6_2atmpS1594 = _M0L12digit__startS325 + _M0L6offsetS328;
          _M0L6_2atmpS1593 = _M0L6_2atmpS1594 - 1;
          _M0L6bufferS324[_M0L6_2atmpS1593] = _M0L5d__loS335;
        } else {
          int32_t _M0L6_2atmpS1602 = _M0L12digit__startS325 + _M0L6offsetS328;
          int32_t _M0L6_2atmpS1599 = _M0L6_2atmpS1602 - 1;
          int32_t _M0L6_2atmpS1601 = 48 + _M0L9remainingS327;
          int32_t _M0L6_2atmpS1600 = (uint16_t)_M0L6_2atmpS1601;
          _M0L6bufferS324[_M0L6_2atmpS1599] = _M0L6_2atmpS1600;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS304,
  uint32_t _M0L3numS308,
  int32_t _M0L12digit__startS305,
  int32_t _M0L10total__lenS307,
  int32_t _M0L5radixS298
) {
  uint32_t _M0L4baseS297;
  int32_t _M0L6_2atmpS1549;
  int32_t _M0L6_2atmpS1548;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS297 = *(uint32_t*)&_M0L5radixS298;
  _M0L6_2atmpS1549 = _M0L5radixS298 - 1;
  _M0L6_2atmpS1548 = _M0L5radixS298 & _M0L6_2atmpS1549;
  if (_M0L6_2atmpS1548 == 0) {
    int32_t _M0L5shiftS299;
    uint32_t _M0L4maskS300;
    int32_t _M0L6_2atmpS1556;
    int32_t _M0L6offsetS301;
    uint32_t _M0L1nS302;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS299 = moonbit_ctz32(_M0L5radixS298);
    _M0L4maskS300 = _M0L4baseS297 - 1u;
    _M0L6_2atmpS1556 = _M0L10total__lenS307 - _M0L12digit__startS305;
    _M0L6offsetS301 = _M0L6_2atmpS1556;
    _M0L1nS302 = _M0L3numS308;
    while (1) {
      if (_M0L1nS302 > 0u) {
        uint32_t _M0L6_2atmpS1555 = _M0L1nS302 & _M0L4maskS300;
        int32_t _M0L5digitS303 = *(int32_t*)&_M0L6_2atmpS1555;
        int32_t _M0L6_2atmpS1552 = _M0L12digit__startS305 + _M0L6offsetS301;
        int32_t _M0L6_2atmpS1550 = _M0L6_2atmpS1552 - 1;
        int32_t _M0L6_2atmpS1551 =
          ((moonbit_string_t)moonbit_string_literal_30.data)[_M0L5digitS303];
        int32_t _M0L6_2atmpS1553;
        uint32_t _M0L6_2atmpS1554;
        _M0L6bufferS304[_M0L6_2atmpS1550] = _M0L6_2atmpS1551;
        _M0L6_2atmpS1553 = _M0L6offsetS301 - 1;
        _M0L6_2atmpS1554 = _M0L1nS302 >> (_M0L5shiftS299 & 31);
        _M0L6offsetS301 = _M0L6_2atmpS1553;
        _M0L1nS302 = _M0L6_2atmpS1554;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1563 = _M0L10total__lenS307 - _M0L12digit__startS305;
    int32_t _M0L6offsetS309 = _M0L6_2atmpS1563;
    uint32_t _M0L1nS310 = _M0L3numS308;
    while (1) {
      if (_M0L1nS310 > 0u) {
        uint32_t _M0L1qS311 = _M0L1nS310 / _M0L4baseS297;
        uint32_t _M0L6_2atmpS1562 = _M0L1qS311 * _M0L4baseS297;
        uint32_t _M0L6_2atmpS1561 = _M0L1nS310 - _M0L6_2atmpS1562;
        int32_t _M0L5digitS312 = *(int32_t*)&_M0L6_2atmpS1561;
        int32_t _M0L6_2atmpS1559 = _M0L12digit__startS305 + _M0L6offsetS309;
        int32_t _M0L6_2atmpS1557 = _M0L6_2atmpS1559 - 1;
        int32_t _M0L6_2atmpS1558 =
          ((moonbit_string_t)moonbit_string_literal_30.data)[_M0L5digitS312];
        int32_t _M0L6_2atmpS1560;
        _M0L6bufferS304[_M0L6_2atmpS1557] = _M0L6_2atmpS1558;
        _M0L6_2atmpS1560 = _M0L6offsetS309 - 1;
        _M0L6offsetS309 = _M0L6_2atmpS1560;
        _M0L1nS310 = _M0L1qS311;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS291,
  uint32_t _M0L3numS296,
  int32_t _M0L12digit__startS292,
  int32_t _M0L10total__lenS295
) {
  int32_t _M0L6_2atmpS1547;
  int32_t _M0L6offsetS286;
  uint32_t _M0L1nS287;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1547 = _M0L10total__lenS295 - _M0L12digit__startS292;
  _M0L6offsetS286 = _M0L6_2atmpS1547;
  _M0L1nS287 = _M0L3numS296;
  while (1) {
    if (_M0L6offsetS286 >= 2) {
      uint32_t _M0L6_2atmpS1544 = _M0L1nS287 & 255u;
      int32_t _M0L9byte__valS288 = *(int32_t*)&_M0L6_2atmpS1544;
      int32_t _M0L2hiS289 = _M0L9byte__valS288 / 16;
      int32_t _M0L2loS290 = _M0L9byte__valS288 % 16;
      int32_t _M0L6_2atmpS1538 = _M0L12digit__startS292 + _M0L6offsetS286;
      int32_t _M0L6_2atmpS1536 = _M0L6_2atmpS1538 - 2;
      int32_t _M0L6_2atmpS1537 =
        ((moonbit_string_t)moonbit_string_literal_30.data)[_M0L2hiS289];
      int32_t _M0L6_2atmpS1541;
      int32_t _M0L6_2atmpS1539;
      int32_t _M0L6_2atmpS1540;
      int32_t _M0L6_2atmpS1542;
      uint32_t _M0L6_2atmpS1543;
      _M0L6bufferS291[_M0L6_2atmpS1536] = _M0L6_2atmpS1537;
      _M0L6_2atmpS1541 = _M0L12digit__startS292 + _M0L6offsetS286;
      _M0L6_2atmpS1539 = _M0L6_2atmpS1541 - 1;
      _M0L6_2atmpS1540
      = ((moonbit_string_t)moonbit_string_literal_30.data)[
        _M0L2loS290
      ];
      _M0L6bufferS291[_M0L6_2atmpS1539] = _M0L6_2atmpS1540;
      _M0L6_2atmpS1542 = _M0L6offsetS286 - 2;
      _M0L6_2atmpS1543 = _M0L1nS287 >> 8;
      _M0L6offsetS286 = _M0L6_2atmpS1542;
      _M0L1nS287 = _M0L6_2atmpS1543;
      continue;
    } else if (_M0L6offsetS286 == 1) {
      uint32_t _M0L6_2atmpS1546 = _M0L1nS287 & 15u;
      int32_t _M0L6nibbleS294 = *(int32_t*)&_M0L6_2atmpS1546;
      int32_t _M0L6_2atmpS1545 =
        ((moonbit_string_t)moonbit_string_literal_30.data)[_M0L6nibbleS294];
      _M0L6bufferS291[_M0L12digit__startS292] = _M0L6_2atmpS1545;
    }
    break;
  }
  return 0;
}

int32_t _M0MPB6Logger19write__iter_2einnerGfE(
  struct _M0TPB6Logger _M0L4selfS269,
  struct _M0TPB4IterGfE* _M0L4iterS273,
  moonbit_string_t _M0L6prefixS270,
  moonbit_string_t _M0L6suffixS285,
  moonbit_string_t _M0L3sepS276,
  int32_t _M0L8trailingS271
) {
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 197 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4selfS269.$0->$method_0(_M0L4selfS269.$1, _M0L6prefixS270);
  if (_M0L8trailingS271) {
    _2afor_277:;
    while (1) {
      void* _M0L7_2abindS272;
      #line 199 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
      _M0L7_2abindS272 = _M0MPB4Iter4nextGfE(_M0L4iterS273);
      switch (Moonbit_object_tag(_M0L7_2abindS272)) {
        case 1: {
          struct _M0DTPC16option6OptionGfE4Some* _M0L7_2aSomeS274 =
            (struct _M0DTPC16option6OptionGfE4Some*)_M0L7_2abindS272;
          float _M0L4_2axS275 = _M0L7_2aSomeS274->$0;
          moonbit_decref_cycle_free(_M0L7_2aSomeS274);
          #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
          _M0MPB6Logger13write__objectGfE(_M0L4selfS269, _M0L4_2axS275);
          #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
          _M0L4selfS269.$0->$method_0(_M0L4selfS269.$1, _M0L3sepS276);
          goto _2afor_277;
          break;
        }
        default: {
          moonbit_decref_cycle_free(_M0L7_2abindS272);
          break;
        }
      }
      break;
    }
  } else {
    void* _M0L7_2abindS278;
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
    _M0L7_2abindS278 = _M0MPB4Iter4nextGfE(_M0L4iterS273);
    switch (Moonbit_object_tag(_M0L7_2abindS278)) {
      case 1: {
        struct _M0DTPC16option6OptionGfE4Some* _M0L7_2aSomeS279 =
          (struct _M0DTPC16option6OptionGfE4Some*)_M0L7_2abindS278;
        float _M0L4_2axS280 = _M0L7_2aSomeS279->$0;
        moonbit_decref_cycle_free(_M0L7_2aSomeS279);
        #line 204 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
        _M0MPB6Logger13write__objectGfE(_M0L4selfS269, _M0L4_2axS280);
        _2afor_284:;
        while (1) {
          void* _M0L7_2abindS281;
          #line 205 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
          _M0L7_2abindS281 = _M0MPB4Iter4nextGfE(_M0L4iterS273);
          switch (Moonbit_object_tag(_M0L7_2abindS281)) {
            case 1: {
              struct _M0DTPC16option6OptionGfE4Some* _M0L7_2aSomeS282 =
                (struct _M0DTPC16option6OptionGfE4Some*)_M0L7_2abindS281;
              float _M0L4_2axS283 = _M0L7_2aSomeS282->$0;
              moonbit_decref_cycle_free(_M0L7_2aSomeS282);
              #line 206 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
              _M0L4selfS269.$0->$method_0(_M0L4selfS269.$1, _M0L3sepS276);
              #line 207 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
              _M0MPB6Logger13write__objectGfE(_M0L4selfS269, _M0L4_2axS283);
              goto _2afor_284;
              break;
            }
            default: {
              moonbit_decref_cycle_free(_M0L7_2abindS281);
              break;
            }
          }
          break;
        }
        break;
      }
      default: {
        moonbit_decref_cycle_free(_M0L7_2abindS278);
        break;
      }
    }
  }
  #line 210 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4selfS269.$0->$method_0(_M0L4selfS269.$1, _M0L6suffixS285);
  return 0;
}

void* _M0MPB4Iter4nextGfE(struct _M0TPB4IterGfE* _M0L4selfS264) {
  struct _M0TWERPC16option6OptionGfE* _M0L7_2afuncS263;
  void* _M0L6resultS265;
  int64_t _M0L7_2abindS266;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\iterator.mbt"
  _M0L7_2afuncS263 = _M0L4selfS264->$0;
  #line 41 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\iterator.mbt"
  _M0L6resultS265 = _M0L7_2afuncS263->code(_M0L7_2afuncS263);
  _M0L7_2abindS266 = _M0L4selfS264->$1;
  switch (Moonbit_object_tag(_M0L6resultS265)) {
    case 1: {
      if (_M0L7_2abindS266 == 4294967296ll) {
        
      } else {
        int64_t _M0L7_2aSomeS267 = _M0L7_2abindS266;
        int32_t _M0L4_2anS268 = (int32_t)_M0L7_2aSomeS267;
        int64_t _M0L6_2atmpS1534;
        if (_M0L4_2anS268 > 0) {
          int32_t _M0L6_2atmpS1535 = _M0L4_2anS268 - 1;
          _M0L6_2atmpS1534 = (int64_t)_M0L6_2atmpS1535;
        } else {
          _M0L6_2atmpS1534 = _M0MPB4Iter4nextN6constrS10984GfE;
        }
        _M0L4selfS264->$1 = _M0L6_2atmpS1534;
      }
      break;
    }
    default: {
      _M0L4selfS264->$1 = _M0MPB4Iter4nextN6constrS10985GfE;
      break;
    }
  }
  return _M0L6resultS265;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB5ArrayGfEE(
  struct _M0TPB5ArrayGfE* _M0L4selfS260
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS259;
  struct _M0TPB6Logger _M0L6_2atmpS1532;
  moonbit_string_t _result_2871;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS259 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS259);
  _M0L6_2atmpS1532
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS259
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPC15array5ArrayPB4Show6outputGfE(_M0L4selfS260, _M0L6_2atmpS1532);
  if (_M0L6_2atmpS1532.$1) {
    moonbit_decref(_M0L6_2atmpS1532.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2871 = _M0MPB13StringBuilder10to__string(_M0L6loggerS259);
  moonbit_decref_cycle_free(_M0L6loggerS259);
  return _result_2871;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS262
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS261;
  struct _M0TPB6Logger _M0L6_2atmpS1533;
  moonbit_string_t _result_2872;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS261 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS261);
  _M0L6_2atmpS1533
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS261
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS262, _M0L6_2atmpS1533);
  if (_M0L6_2atmpS1533.$1) {
    moonbit_decref(_M0L6_2atmpS1533.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2872 = _M0MPB13StringBuilder10to__string(_M0L6loggerS261);
  moonbit_decref_cycle_free(_M0L6loggerS261);
  return _result_2872;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS252,
  struct _M0TPB6Logger _M0L6loggerS251
) {
  moonbit_string_t _M0L6_2atmpS1528;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1528 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS252);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS251.$0->$method_0(_M0L6loggerS251.$1, _M0L6_2atmpS1528);
  moonbit_decref_cycle_free(_M0L6_2atmpS1528);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS254,
  struct _M0TPB6Logger _M0L6loggerS253
) {
  moonbit_string_t _M0L6_2atmpS1529;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1529 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS254);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS253.$0->$method_0(_M0L6loggerS253.$1, _M0L6_2atmpS1529);
  moonbit_decref_cycle_free(_M0L6_2atmpS1529);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGfE(
  float _M0L4selfS256,
  struct _M0TPB6Logger _M0L6loggerS255
) {
  moonbit_string_t _M0L6_2atmpS1530;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1530 = _M0IPC15float5FloatPB4Show10to__string(_M0L4selfS256);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS255.$0->$method_0(_M0L6loggerS255.$1, _M0L6_2atmpS1530);
  moonbit_decref_cycle_free(_M0L6_2atmpS1530);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS258,
  struct _M0TPB6Logger _M0L6loggerS257
) {
  moonbit_string_t _M0L6_2atmpS1531;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1531 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS258);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS257.$0->$method_0(_M0L6loggerS257.$1, _M0L6_2atmpS1531);
  moonbit_decref_cycle_free(_M0L6_2atmpS1531);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS250
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS250.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS249
) {
  moonbit_string_t _M0L8_2afieldS2731;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2731 = _M0L4selfS249.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2731);
  return _M0L8_2afieldS2731;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS245,
  moonbit_string_t _M0L5valueS246,
  int32_t _M0L5startS247,
  int32_t _M0L3lenS248
) {
  int32_t _M0L6_2atmpS1527;
  int64_t _M0L6_2atmpS1526;
  struct _M0TPC16string10StringView _M0L6_2atmpS1525;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1527 = _M0L5startS247 + _M0L3lenS248;
  _M0L6_2atmpS1526 = (int64_t)_M0L6_2atmpS1527;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1525
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS246, _M0L5startS247, _M0L6_2atmpS1526);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS245, _M0L6_2atmpS1525);
  moonbit_decref_cycle_free(_M0L6_2atmpS1525.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String21clamped__view_2einner(
  moonbit_string_t _M0L4selfS238,
  int32_t _M0L5startS240,
  int64_t _M0L3endS242
) {
  int32_t _M0L3lenS237;
  int32_t _M0Lm2loS239;
  int32_t _M0Lm2hiS241;
  int32_t _M0L6_2atmpS1509;
  int32_t _if__result_2873;
  int32_t _M0L6_2atmpS1517;
  int32_t _if__result_2874;
  int32_t _M0L6_2atmpS1519;
  int32_t _M0L6_2atmpS1520;
  #line 698 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS237 = Moonbit_array_length(_M0L4selfS238);
  if (_M0L5startS240 < 0) {
    _M0Lm2loS239 = 0;
  } else if (_M0L5startS240 > _M0L3lenS237) {
    _M0Lm2loS239 = _M0L3lenS237;
  } else {
    _M0Lm2loS239 = _M0L5startS240;
  }
  if (_M0L3endS242 == 4294967296ll) {
    _M0Lm2hiS241 = _M0L3lenS237;
  } else {
    int64_t _M0L7_2aSomeS243 = _M0L3endS242;
    int32_t _M0L4_2aeS244 = (int32_t)_M0L7_2aSomeS243;
    if (_M0L4_2aeS244 < 0) {
      _M0Lm2hiS241 = 0;
    } else if (_M0L4_2aeS244 > _M0L3lenS237) {
      _M0Lm2hiS241 = _M0L3lenS237;
    } else {
      _M0Lm2hiS241 = _M0L4_2aeS244;
    }
  }
  _M0L6_2atmpS1509 = _M0Lm2loS239;
  if (_M0L6_2atmpS1509 > 0) {
    int32_t _M0L6_2atmpS1508 = _M0Lm2loS239;
    if (_M0L6_2atmpS1508 < _M0L3lenS237) {
      int32_t _M0L6_2atmpS1507 = _M0Lm2loS239;
      int32_t _M0L6_2atmpS1506 = _M0L4selfS238[_M0L6_2atmpS1507];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1506)) {
        int32_t _M0L6_2atmpS1505 = _M0Lm2loS239;
        int32_t _M0L6_2atmpS1504 = _M0L6_2atmpS1505 - 1;
        int32_t _M0L6_2atmpS1503 = _M0L4selfS238[_M0L6_2atmpS1504];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2873
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1503);
      } else {
        _if__result_2873 = 0;
      }
    } else {
      _if__result_2873 = 0;
    }
  } else {
    _if__result_2873 = 0;
  }
  if (_if__result_2873) {
    int32_t _M0L6_2atmpS1510 = _M0Lm2loS239;
    _M0Lm2loS239 = _M0L6_2atmpS1510 + 1;
  }
  _M0L6_2atmpS1517 = _M0Lm2hiS241;
  if (_M0L6_2atmpS1517 > 0) {
    int32_t _M0L6_2atmpS1516 = _M0Lm2hiS241;
    if (_M0L6_2atmpS1516 < _M0L3lenS237) {
      int32_t _M0L6_2atmpS1515 = _M0Lm2hiS241;
      int32_t _M0L6_2atmpS1514 = _M0L4selfS238[_M0L6_2atmpS1515];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1514)) {
        int32_t _M0L6_2atmpS1513 = _M0Lm2hiS241;
        int32_t _M0L6_2atmpS1512 = _M0L6_2atmpS1513 - 1;
        int32_t _M0L6_2atmpS1511 = _M0L4selfS238[_M0L6_2atmpS1512];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2874
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1511);
      } else {
        _if__result_2874 = 0;
      }
    } else {
      _if__result_2874 = 0;
    }
  } else {
    _if__result_2874 = 0;
  }
  if (_if__result_2874) {
    int32_t _M0L6_2atmpS1518 = _M0Lm2hiS241;
    _M0Lm2hiS241 = _M0L6_2atmpS1518 - 1;
  }
  _M0L6_2atmpS1519 = _M0Lm2loS239;
  _M0L6_2atmpS1520 = _M0Lm2hiS241;
  if (_M0L6_2atmpS1519 >= _M0L6_2atmpS1520) {
    int32_t _M0L6_2atmpS1521 = _M0Lm2loS239;
    int32_t _M0L6_2atmpS1522 = _M0Lm2loS239;
    moonbit_incref_cycle_free(_M0L4selfS238);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS238,
                                                 .$1 = _M0L6_2atmpS1521,
                                                 .$2 = _M0L6_2atmpS1522};
  } else {
    int32_t _M0L6_2atmpS1523 = _M0Lm2loS239;
    int32_t _M0L6_2atmpS1524 = _M0Lm2hiS241;
    moonbit_incref_cycle_free(_M0L4selfS238);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS238,
                                                 .$1 = _M0L6_2atmpS1523,
                                                 .$2 = _M0L6_2atmpS1524};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS236,
  struct _M0TPB4Show _M0L4showS235
) {
  struct _M0TPB6Logger _M0L6_2atmpS1502;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS236);
  _M0L6_2atmpS1502
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS236
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS235.$0->$method_0(_M0L4showS235.$1, _M0L6_2atmpS1502);
  if (_M0L6_2atmpS1502.$1) {
    moonbit_decref(_M0L6_2atmpS1502.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS234,
  struct _M0TPB4Show _M0L4showS233
) {
  struct _M0TPB6Logger _M0L6_2atmpS1501;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS234);
  _M0L6_2atmpS1501
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS234
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS233.$0->$method_0(_M0L4showS233.$1, _M0L6_2atmpS1501);
  if (_M0L6_2atmpS1501.$1) {
    moonbit_decref(_M0L6_2atmpS1501.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS232) {
  int64_t _M0L6_2atmpS1500;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1500 = (int64_t)_M0L4selfS232;
  return *(uint64_t*)&_M0L6_2atmpS1500;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

moonbit_string_t _M0MPC16string6String14escape_2einner(
  moonbit_string_t _M0L4selfS230,
  int32_t _M0L5quoteS231
) {
  struct _M0TPB13StringBuilder* _M0L3bufS229;
  int32_t _M0L6_2atmpS1499;
  struct _M0TPC16string10StringView _M0L6_2atmpS1497;
  struct _M0TPB6Logger _M0L6_2atmpS1498;
  moonbit_string_t _result_2875;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS229 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1499 = Moonbit_array_length(_M0L4selfS230);
  moonbit_incref_cycle_free(_M0L4selfS230);
  _M0L6_2atmpS1497
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS230, .$1 = 0, .$2 = _M0L6_2atmpS1499
  };
  moonbit_incref_cycle_free(_M0L3bufS229);
  _M0L6_2atmpS1498
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS229
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1497, _M0L6_2atmpS1498, _M0L5quoteS231);
  moonbit_decref_cycle_free(_M0L6_2atmpS1497.$0);
  if (_M0L6_2atmpS1498.$1) {
    moonbit_decref(_M0L6_2atmpS1498.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2875 = _M0MPB13StringBuilder10to__string(_M0L3bufS229);
  moonbit_decref_cycle_free(_M0L3bufS229);
  return _result_2875;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS221,
  struct _M0TPB6Logger _M0L6loggerS219,
  int32_t _M0L5quoteS218
) {
  int32_t _M0L3endS1495;
  int32_t _M0L5startS1496;
  int32_t _M0L3lenS220;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS222;
  int32_t _M0L1iS223;
  int32_t _M0L3segS224;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS218) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS219.$0->$method_3(_M0L6loggerS219.$1, 34);
  }
  _M0L3endS1495 = _M0L4selfS221.$2;
  _M0L5startS1496 = _M0L4selfS221.$1;
  _M0L3lenS220 = _M0L3endS1495 - _M0L5startS1496;
  moonbit_incref_cycle_free(_M0L4selfS221.$0);
  if (_M0L6loggerS219.$1) {
    moonbit_incref(_M0L6loggerS219.$1);
  }
  _M0L6_2aenvS222
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS222)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 86, 0);
  _M0L6_2aenvS222->$0 = _M0L4selfS221;
  _M0L6_2aenvS222->$1 = _M0L6loggerS219;
  _M0L1iS223 = 0;
  _M0L3segS224 = 0;
  _2afor_225:;
  while (1) {
    moonbit_string_t _M0L3strS1492;
    int32_t _M0L5startS1494;
    int32_t _M0L6_2atmpS1493;
    int32_t _M0L4codeS226;
    int32_t _M0L1cS228;
    int32_t _M0L6_2atmpS1476;
    int32_t _M0L6_2atmpS1477;
    int32_t _M0L6_2atmpS1478;
    if (_M0L1iS223 >= _M0L3lenS220) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS222, _M0L3segS224, _M0L1iS223);
      moonbit_decref_cycle_free(_M0L6_2aenvS222);
      break;
    }
    _M0L3strS1492 = _M0L4selfS221.$0;
    _M0L5startS1494 = _M0L4selfS221.$1;
    _M0L6_2atmpS1493 = _M0L5startS1494 + _M0L1iS223;
    _M0L4codeS226 = _M0L3strS1492[_M0L6_2atmpS1493];
    switch (_M0L4codeS226) {
      case 34: {
        _M0L1cS228 = _M0L4codeS226;
        goto join_227;
        break;
      }
      
      case 92: {
        _M0L1cS228 = _M0L4codeS226;
        goto join_227;
        break;
      }
      
      case 10: {
        int32_t _M0L6_2atmpS1479;
        int32_t _M0L6_2atmpS1480;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS222, _M0L3segS224, _M0L1iS223);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS219.$0->$method_0(_M0L6loggerS219.$1, (moonbit_string_t)moonbit_string_literal_31.data);
        _M0L6_2atmpS1479 = _M0L1iS223 + 1;
        _M0L6_2atmpS1480 = _M0L1iS223 + 1;
        _M0L1iS223 = _M0L6_2atmpS1479;
        _M0L3segS224 = _M0L6_2atmpS1480;
        goto _2afor_225;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1481;
        int32_t _M0L6_2atmpS1482;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS222, _M0L3segS224, _M0L1iS223);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS219.$0->$method_0(_M0L6loggerS219.$1, (moonbit_string_t)moonbit_string_literal_32.data);
        _M0L6_2atmpS1481 = _M0L1iS223 + 1;
        _M0L6_2atmpS1482 = _M0L1iS223 + 1;
        _M0L1iS223 = _M0L6_2atmpS1481;
        _M0L3segS224 = _M0L6_2atmpS1482;
        goto _2afor_225;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1483;
        int32_t _M0L6_2atmpS1484;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS222, _M0L3segS224, _M0L1iS223);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS219.$0->$method_0(_M0L6loggerS219.$1, (moonbit_string_t)moonbit_string_literal_33.data);
        _M0L6_2atmpS1483 = _M0L1iS223 + 1;
        _M0L6_2atmpS1484 = _M0L1iS223 + 1;
        _M0L1iS223 = _M0L6_2atmpS1483;
        _M0L3segS224 = _M0L6_2atmpS1484;
        goto _2afor_225;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1485;
        int32_t _M0L6_2atmpS1486;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS222, _M0L3segS224, _M0L1iS223);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS219.$0->$method_0(_M0L6loggerS219.$1, (moonbit_string_t)moonbit_string_literal_34.data);
        _M0L6_2atmpS1485 = _M0L1iS223 + 1;
        _M0L6_2atmpS1486 = _M0L1iS223 + 1;
        _M0L1iS223 = _M0L6_2atmpS1485;
        _M0L3segS224 = _M0L6_2atmpS1486;
        goto _2afor_225;
        break;
      }
      default: {
        if (_M0L4codeS226 < 32) {
          int32_t _M0L6_2atmpS1488;
          moonbit_string_t _M0L6_2atmpS1487;
          int32_t _M0L6_2atmpS1489;
          int32_t _M0L6_2atmpS1490;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS222, _M0L3segS224, _M0L1iS223);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS219.$0->$method_0(_M0L6loggerS219.$1, (moonbit_string_t)moonbit_string_literal_35.data);
          _M0L6_2atmpS1488 = _M0L4codeS226 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1487 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1488);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS219.$0->$method_0(_M0L6loggerS219.$1, _M0L6_2atmpS1487);
          moonbit_decref_cycle_free(_M0L6_2atmpS1487);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS219.$0->$method_0(_M0L6loggerS219.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1489 = _M0L1iS223 + 1;
          _M0L6_2atmpS1490 = _M0L1iS223 + 1;
          _M0L1iS223 = _M0L6_2atmpS1489;
          _M0L3segS224 = _M0L6_2atmpS1490;
          goto _2afor_225;
        } else {
          int32_t _M0L6_2atmpS1491 = _M0L1iS223 + 1;
          int32_t _tmp_2878 = _M0L3segS224;
          _M0L1iS223 = _M0L6_2atmpS1491;
          _M0L3segS224 = _tmp_2878;
          goto _2afor_225;
        }
        break;
      }
    }
    goto joinlet_2877;
    join_227:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS222, _M0L3segS224, _M0L1iS223);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS219.$0->$method_3(_M0L6loggerS219.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1476 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS228);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS219.$0->$method_3(_M0L6loggerS219.$1, _M0L6_2atmpS1476);
    _M0L6_2atmpS1477 = _M0L1iS223 + 1;
    _M0L6_2atmpS1478 = _M0L1iS223 + 1;
    _M0L1iS223 = _M0L6_2atmpS1477;
    _M0L3segS224 = _M0L6_2atmpS1478;
    continue;
    joinlet_2877:;
    break;
  }
  if (_M0L5quoteS218) {
    #line 202 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS219.$0->$method_3(_M0L6loggerS219.$1, 34);
  }
  return 0;
}

int32_t _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS214,
  int32_t _M0L3segS217,
  int32_t _M0L1iS216
) {
  struct _M0TPB6Logger _M0L6loggerS213;
  struct _M0TPC16string10StringView _M0L4selfS215;
  #line 153 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6loggerS213 = _M0L6_2aenvS214->$1;
  _M0L4selfS215 = _M0L6_2aenvS214->$0;
  if (_M0L1iS216 > _M0L3segS217) {
    int64_t _M0L6_2atmpS1475 = (int64_t)_M0L1iS216;
    struct _M0TPC16string10StringView _M0L6_2atmpS1474;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1474
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS215, _M0L3segS217, _M0L6_2atmpS1475);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS213.$0->$method_2(_M0L6loggerS213.$1, _M0L6_2atmpS1474);
    moonbit_decref_cycle_free(_M0L6_2atmpS1474.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS204,
  int32_t _M0L5startS206,
  int64_t _M0L3endS208
) {
  int32_t _M0L3endS1472;
  int32_t _M0L5startS1473;
  int32_t _M0L3lenS203;
  int32_t _M0Lm2loS205;
  int32_t _M0Lm2hiS207;
  moonbit_string_t _M0L3strS211;
  int32_t _M0L4baseS212;
  int32_t _M0L6_2atmpS1450;
  int32_t _if__result_2879;
  int32_t _M0L6_2atmpS1460;
  int32_t _if__result_2880;
  int32_t _M0L6_2atmpS1462;
  int32_t _M0L6_2atmpS1463;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1472 = _M0L4selfS204.$2;
  _M0L5startS1473 = _M0L4selfS204.$1;
  _M0L3lenS203 = _M0L3endS1472 - _M0L5startS1473;
  if (_M0L5startS206 < 0) {
    _M0Lm2loS205 = 0;
  } else if (_M0L5startS206 > _M0L3lenS203) {
    _M0Lm2loS205 = _M0L3lenS203;
  } else {
    _M0Lm2loS205 = _M0L5startS206;
  }
  if (_M0L3endS208 == 4294967296ll) {
    _M0Lm2hiS207 = _M0L3lenS203;
  } else {
    int64_t _M0L7_2aSomeS209 = _M0L3endS208;
    int32_t _M0L4_2aeS210 = (int32_t)_M0L7_2aSomeS209;
    if (_M0L4_2aeS210 < 0) {
      _M0Lm2hiS207 = 0;
    } else if (_M0L4_2aeS210 > _M0L3lenS203) {
      _M0Lm2hiS207 = _M0L3lenS203;
    } else {
      _M0Lm2hiS207 = _M0L4_2aeS210;
    }
  }
  _M0L3strS211 = _M0L4selfS204.$0;
  _M0L4baseS212 = _M0L4selfS204.$1;
  _M0L6_2atmpS1450 = _M0Lm2loS205;
  if (_M0L6_2atmpS1450 > 0) {
    int32_t _M0L6_2atmpS1449 = _M0Lm2loS205;
    if (_M0L6_2atmpS1449 < _M0L3lenS203) {
      int32_t _M0L6_2atmpS1448 = _M0Lm2loS205;
      int32_t _M0L6_2atmpS1447 = _M0L4baseS212 + _M0L6_2atmpS1448;
      int32_t _M0L6_2atmpS1446 = _M0L3strS211[_M0L6_2atmpS1447];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1446)) {
        int32_t _M0L6_2atmpS1445 = _M0Lm2loS205;
        int32_t _M0L6_2atmpS1444 = _M0L4baseS212 + _M0L6_2atmpS1445;
        int32_t _M0L6_2atmpS1443 = _M0L6_2atmpS1444 - 1;
        int32_t _M0L6_2atmpS1442 = _M0L3strS211[_M0L6_2atmpS1443];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2879
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1442);
      } else {
        _if__result_2879 = 0;
      }
    } else {
      _if__result_2879 = 0;
    }
  } else {
    _if__result_2879 = 0;
  }
  if (_if__result_2879) {
    int32_t _M0L6_2atmpS1451 = _M0Lm2loS205;
    _M0Lm2loS205 = _M0L6_2atmpS1451 + 1;
  }
  _M0L6_2atmpS1460 = _M0Lm2hiS207;
  if (_M0L6_2atmpS1460 > 0) {
    int32_t _M0L6_2atmpS1459 = _M0Lm2hiS207;
    if (_M0L6_2atmpS1459 < _M0L3lenS203) {
      int32_t _M0L6_2atmpS1458 = _M0Lm2hiS207;
      int32_t _M0L6_2atmpS1457 = _M0L4baseS212 + _M0L6_2atmpS1458;
      int32_t _M0L6_2atmpS1456 = _M0L3strS211[_M0L6_2atmpS1457];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1456)) {
        int32_t _M0L6_2atmpS1455 = _M0Lm2hiS207;
        int32_t _M0L6_2atmpS1454 = _M0L4baseS212 + _M0L6_2atmpS1455;
        int32_t _M0L6_2atmpS1453 = _M0L6_2atmpS1454 - 1;
        int32_t _M0L6_2atmpS1452 = _M0L3strS211[_M0L6_2atmpS1453];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2880
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1452);
      } else {
        _if__result_2880 = 0;
      }
    } else {
      _if__result_2880 = 0;
    }
  } else {
    _if__result_2880 = 0;
  }
  if (_if__result_2880) {
    int32_t _M0L6_2atmpS1461 = _M0Lm2hiS207;
    _M0Lm2hiS207 = _M0L6_2atmpS1461 - 1;
  }
  _M0L6_2atmpS1462 = _M0Lm2loS205;
  _M0L6_2atmpS1463 = _M0Lm2hiS207;
  if (_M0L6_2atmpS1462 >= _M0L6_2atmpS1463) {
    int32_t _M0L6_2atmpS1467 = _M0Lm2loS205;
    int32_t _M0L6_2atmpS1464 = _M0L4baseS212 + _M0L6_2atmpS1467;
    int32_t _M0L6_2atmpS1466 = _M0Lm2loS205;
    int32_t _M0L6_2atmpS1465 = _M0L4baseS212 + _M0L6_2atmpS1466;
    moonbit_incref_cycle_free(_M0L3strS211);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS211,
                                                 .$1 = _M0L6_2atmpS1464,
                                                 .$2 = _M0L6_2atmpS1465};
  } else {
    int32_t _M0L6_2atmpS1471 = _M0Lm2loS205;
    int32_t _M0L6_2atmpS1468 = _M0L4baseS212 + _M0L6_2atmpS1471;
    int32_t _M0L6_2atmpS1470 = _M0Lm2hiS207;
    int32_t _M0L6_2atmpS1469 = _M0L4baseS212 + _M0L6_2atmpS1470;
    moonbit_incref_cycle_free(_M0L3strS211);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS211,
                                                 .$1 = _M0L6_2atmpS1468,
                                                 .$2 = _M0L6_2atmpS1469};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS202) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS201;
  int32_t _M0L6_2atmpS1439;
  int32_t _M0L6_2atmpS1438;
  int32_t _M0L6_2atmpS1441;
  int32_t _M0L6_2atmpS1440;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1437;
  moonbit_string_t _result_2881;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS201 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1439 = _M0IPC14byte4BytePB3Div3div(_M0L1bS202, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1438
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1439);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS201, _M0L6_2atmpS1438);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1441 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS202, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1440
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1441);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS201, _M0L6_2atmpS1440);
  _M0L6_2atmpS1437 = _M0L7_2aselfS201;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2881 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1437);
  moonbit_decref_cycle_free(_M0L6_2atmpS1437);
  return _result_2881;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS200) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS200 < 10) {
    int32_t _M0L6_2atmpS1434;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1434 = _M0IPC14byte4BytePB3Add3add(_M0L1iS200, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1434);
  } else {
    int32_t _M0L6_2atmpS1436;
    int32_t _M0L6_2atmpS1435;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1436 = _M0IPC14byte4BytePB3Add3add(_M0L1iS200, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1435 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1436, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1435);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS198,
  int32_t _M0L4thatS199
) {
  int32_t _M0L6_2atmpS1432;
  int32_t _M0L6_2atmpS1433;
  int32_t _M0L6_2atmpS1431;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1432 = (int32_t)_M0L4selfS198;
  _M0L6_2atmpS1433 = (int32_t)_M0L4thatS199;
  _M0L6_2atmpS1431 = _M0L6_2atmpS1432 - _M0L6_2atmpS1433;
  return _M0L6_2atmpS1431 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS196,
  int32_t _M0L4thatS197
) {
  int32_t _M0L6_2atmpS1429;
  int32_t _M0L6_2atmpS1430;
  int32_t _M0L6_2atmpS1428;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1429 = (int32_t)_M0L4selfS196;
  _M0L6_2atmpS1430 = (int32_t)_M0L4thatS197;
  _M0L6_2atmpS1428 = _M0L6_2atmpS1429 % _M0L6_2atmpS1430;
  return _M0L6_2atmpS1428 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS194,
  int32_t _M0L4thatS195
) {
  int32_t _M0L6_2atmpS1426;
  int32_t _M0L6_2atmpS1427;
  int32_t _M0L6_2atmpS1425;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1426 = (int32_t)_M0L4selfS194;
  _M0L6_2atmpS1427 = (int32_t)_M0L4thatS195;
  _M0L6_2atmpS1425 = _M0L6_2atmpS1426 / _M0L6_2atmpS1427;
  return _M0L6_2atmpS1425 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS192,
  int32_t _M0L4thatS193
) {
  int32_t _M0L6_2atmpS1423;
  int32_t _M0L6_2atmpS1424;
  int32_t _M0L6_2atmpS1422;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1423 = (int32_t)_M0L4selfS192;
  _M0L6_2atmpS1424 = (int32_t)_M0L4thatS193;
  _M0L6_2atmpS1422 = _M0L6_2atmpS1423 + _M0L6_2atmpS1424;
  return _M0L6_2atmpS1422 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS191) {
  int32_t _M0L6_2atmpS1421;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1421 = (int32_t)_M0L4selfS191;
  return _M0L6_2atmpS1421;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS190) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS190 >= 56320 && _M0L4selfS190 <= 57343;
}

int32_t _M0MPC16uint166UInt1622is__leading__surrogate(int32_t _M0L4selfS189) {
  #line 28 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS189 >= 55296 && _M0L4selfS189 <= 56319;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS188,
  moonbit_string_t _M0L3strS186
) {
  int32_t _M0L8str__lenS185;
  int32_t _M0L3lenS1420;
  int32_t _M0L8requiredS187;
  uint16_t* _M0L4dataS1415;
  int32_t _M0L6_2atmpS1414;
  int32_t _if__result_2882;
  uint16_t* _M0L4dataS1416;
  int32_t _M0L3lenS1417;
  int32_t _M0L3lenS1419;
  int32_t _M0L6_2atmpS1418;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS185 = Moonbit_array_length(_M0L3strS186);
  if (_M0L8str__lenS185 == 0) {
    return 0;
  }
  _M0L3lenS1420 = _M0L4selfS188->$1;
  _M0L8requiredS187 = _M0L3lenS1420 + _M0L8str__lenS185;
  _M0L4dataS1415 = _M0L4selfS188->$0;
  _M0L6_2atmpS1414 = Moonbit_array_length(_M0L4dataS1415);
  if (_M0L8requiredS187 > _M0L6_2atmpS1414) {
    _if__result_2882 = 1;
  } else {
    int32_t _M0L3lenS1413 = _M0L4selfS188->$1;
    _if__result_2882 = _M0L8requiredS187 < _M0L3lenS1413;
  }
  if (_if__result_2882) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS188, _M0L8requiredS187);
  }
  _M0L4dataS1416 = _M0L4selfS188->$0;
  _M0L3lenS1417 = _M0L4selfS188->$1;
  moonbit_incref_cycle_free(_M0L4dataS1416);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1416, _M0L3lenS1417, _M0L3strS186, 0, _M0L8str__lenS185);
  moonbit_decref_cycle_free(_M0L4dataS1416);
  _M0L3lenS1419 = _M0L4selfS188->$1;
  _M0L6_2atmpS1418 = _M0L3lenS1419 + _M0L8str__lenS185;
  _M0L4selfS188->$1 = _M0L6_2atmpS1418;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS181,
  int32_t _M0L11dst__offsetS184,
  moonbit_string_t _M0L3strS182,
  int32_t _M0L11str__offsetS177,
  int32_t _M0L3lenS178
) {
  int32_t _M0L16end__str__offsetS176;
  int32_t _M0L1iS179;
  int32_t _M0L1jS180;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS176 = _M0L11str__offsetS177 + _M0L3lenS178;
  _M0L1iS179 = _M0L11str__offsetS177;
  _M0L1jS180 = _M0L11dst__offsetS184;
  while (1) {
    if (_M0L1iS179 < _M0L16end__str__offsetS176) {
      int32_t _M0L6_2atmpS1410 = _M0L3strS182[_M0L1iS179];
      int32_t _M0L6_2atmpS1411;
      int32_t _M0L6_2atmpS1412;
      _M0L4selfS181[_M0L1jS180] = _M0L6_2atmpS1410;
      _M0L6_2atmpS1411 = _M0L1iS179 + 1;
      _M0L6_2atmpS1412 = _M0L1jS180 + 1;
      _M0L1iS179 = _M0L6_2atmpS1411;
      _M0L1jS180 = _M0L6_2atmpS1412;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS174,
  int32_t _M0L2chS173
) {
  uint32_t _M0L4codeS172;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS172 = _M0MPC14char4Char8to__uint(_M0L2chS173);
  if (_M0L4codeS172 <= 65535u) {
    int32_t _M0L3lenS1381 = _M0L4selfS174->$1;
    uint16_t* _M0L4dataS1383 = _M0L4selfS174->$0;
    int32_t _M0L6_2atmpS1382 = Moonbit_array_length(_M0L4dataS1383);
    uint16_t* _M0L4dataS1386;
    int32_t _M0L3lenS1387;
    int32_t _M0L6_2atmpS1388;
    int32_t _M0L3lenS1390;
    int32_t _M0L6_2atmpS1389;
    if (_M0L3lenS1381 >= _M0L6_2atmpS1382) {
      int32_t _M0L3lenS1385 = _M0L4selfS174->$1;
      int32_t _M0L6_2atmpS1384 = _M0L3lenS1385 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS174, _M0L6_2atmpS1384);
    }
    _M0L4dataS1386 = _M0L4selfS174->$0;
    _M0L3lenS1387 = _M0L4selfS174->$1;
    moonbit_incref_cycle_free(_M0L4dataS1386);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1388 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS172);
    if (
      _M0L3lenS1387 < 0
      || _M0L3lenS1387 >= Moonbit_array_length(_M0L4dataS1386)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1386[_M0L3lenS1387] = _M0L6_2atmpS1388;
    moonbit_decref_cycle_free(_M0L4dataS1386);
    _M0L3lenS1390 = _M0L4selfS174->$1;
    _M0L6_2atmpS1389 = _M0L3lenS1390 + 1;
    _M0L4selfS174->$1 = _M0L6_2atmpS1389;
  } else if (_M0L4codeS172 <= 1114111u) {
    uint16_t* _M0L4dataS1394 = _M0L4selfS174->$0;
    int32_t _M0L6_2atmpS1392 = Moonbit_array_length(_M0L4dataS1394);
    int32_t _M0L3lenS1393 = _M0L4selfS174->$1;
    int32_t _M0L6_2atmpS1391 = _M0L6_2atmpS1392 - _M0L3lenS1393;
    uint32_t _M0L4codeS175;
    uint16_t* _M0L4dataS1397;
    int32_t _M0L3lenS1398;
    uint32_t _M0L6_2atmpS1401;
    uint32_t _M0L6_2atmpS1400;
    int32_t _M0L6_2atmpS1399;
    uint16_t* _M0L4dataS1402;
    int32_t _M0L3lenS1407;
    int32_t _M0L6_2atmpS1403;
    uint32_t _M0L6_2atmpS1406;
    uint32_t _M0L6_2atmpS1405;
    int32_t _M0L6_2atmpS1404;
    int32_t _M0L3lenS1409;
    int32_t _M0L6_2atmpS1408;
    if (_M0L6_2atmpS1391 < 2) {
      int32_t _M0L3lenS1396 = _M0L4selfS174->$1;
      int32_t _M0L6_2atmpS1395 = _M0L3lenS1396 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS174, _M0L6_2atmpS1395);
    }
    _M0L4codeS175 = _M0L4codeS172 - 65536u;
    _M0L4dataS1397 = _M0L4selfS174->$0;
    _M0L3lenS1398 = _M0L4selfS174->$1;
    _M0L6_2atmpS1401 = _M0L4codeS175 >> 10;
    _M0L6_2atmpS1400 = 55296u + _M0L6_2atmpS1401;
    moonbit_incref_cycle_free(_M0L4dataS1397);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1399 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1400);
    if (
      _M0L3lenS1398 < 0
      || _M0L3lenS1398 >= Moonbit_array_length(_M0L4dataS1397)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1397[_M0L3lenS1398] = _M0L6_2atmpS1399;
    moonbit_decref_cycle_free(_M0L4dataS1397);
    _M0L4dataS1402 = _M0L4selfS174->$0;
    _M0L3lenS1407 = _M0L4selfS174->$1;
    _M0L6_2atmpS1403 = _M0L3lenS1407 + 1;
    _M0L6_2atmpS1406 = _M0L4codeS175 & 1023u;
    _M0L6_2atmpS1405 = 56320u + _M0L6_2atmpS1406;
    moonbit_incref_cycle_free(_M0L4dataS1402);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1404 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1405);
    if (
      _M0L6_2atmpS1403 < 0
      || _M0L6_2atmpS1403 >= Moonbit_array_length(_M0L4dataS1402)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1402[_M0L6_2atmpS1403] = _M0L6_2atmpS1404;
    moonbit_decref_cycle_free(_M0L4dataS1402);
    _M0L3lenS1409 = _M0L4selfS174->$1;
    _M0L6_2atmpS1408 = _M0L3lenS1409 + 2;
    _M0L4selfS174->$1 = _M0L6_2atmpS1408;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_36.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS169,
  int32_t _M0L8requiredS170
) {
  uint16_t* _M0L4dataS1380;
  int32_t _M0L6_2atmpS1378;
  int32_t _M0L3lenS1379;
  int32_t _M0L13new__capacityS168;
  uint16_t* _M0L4dataS1375;
  int32_t _M0L6_2atmpS1376;
  int32_t _M0L3lenS1377;
  uint16_t* _M0L9new__dataS171;
  uint16_t* _M0L6_2aoldS2732;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1380 = _M0L4selfS169->$0;
  _M0L6_2atmpS1378 = Moonbit_array_length(_M0L4dataS1380);
  _M0L3lenS1379 = _M0L4selfS169->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS168
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1378, _M0L3lenS1379, _M0L8requiredS170);
  _M0L4dataS1375 = _M0L4selfS169->$0;
  moonbit_incref_cycle_free(_M0L4dataS1375);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1376 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1377 = _M0L4selfS169->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS171
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1375, _M0L13new__capacityS168, _M0L6_2atmpS1376, _M0L3lenS1377, 0, 0);
  _M0L6_2aoldS2732 = _M0L4selfS169->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2732);
  _M0L4selfS169->$0 = _M0L9new__dataS171;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS167,
  int32_t _M0L3lenS163,
  int32_t _M0L8requiredS162
) {
  int32_t _M0L5spaceS164;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS162 < _M0L3lenS163) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_37.data);
  }
  _M0L5spaceS164 = _M0L7currentS167;
  while (1) {
    if (_M0L5spaceS164 < _M0L8requiredS162) {
      int32_t _M0L4nextS165 = _M0L5spaceS164 * 2;
      if (_M0L4nextS165 <= _M0L5spaceS164) {
        return _M0L8requiredS162;
      }
      _M0L5spaceS164 = _M0L4nextS165;
      continue;
    } else {
      return _M0L5spaceS164;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS161) {
  int32_t _M0L6_2atmpS1374;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1374 = *(int32_t*)&_M0L4selfS161;
  return (uint16_t)_M0L6_2atmpS1374;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS160) {
  int32_t _M0L6_2atmpS1373;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1373 = _M0L4selfS160;
  return *(uint32_t*)&_M0L6_2atmpS1373;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS158
) {
  int32_t _M0L3lenS1364;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1364 = _M0L4selfS158->$1;
  if (_M0L3lenS1364 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1365 = _M0L4selfS158->$1;
    uint16_t* _M0L4dataS1367 = _M0L4selfS158->$0;
    int32_t _M0L6_2atmpS1366 = Moonbit_array_length(_M0L4dataS1367);
    if (_M0L3lenS1365 == _M0L6_2atmpS1366) {
      uint16_t* _M0L4dataS1368 = _M0L4selfS158->$0;
      moonbit_incref_cycle_free(_M0L4dataS1368);
      return _M0L4dataS1368;
    } else {
      uint16_t* _M0L4dataS1369 = _M0L4selfS158->$0;
      int32_t _M0L3lenS1370 = _M0L4selfS158->$1;
      int32_t _M0L6_2atmpS1371;
      int32_t _M0L3lenS1372;
      uint16_t* _M0L4dataS159;
      moonbit_incref_cycle_free(_M0L4dataS1369);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1371 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1372 = _M0L4selfS158->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS159
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1369, _M0L3lenS1370, _M0L6_2atmpS1371, _M0L3lenS1372, 0, 0);
      return _M0L4dataS159;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS155,
  int32_t _M0L13allocate__lenS151,
  int32_t _M0L4initS156,
  int32_t _M0L3lenS152,
  int32_t _M0L11src__offsetS153,
  int32_t _M0L11dst__offsetS154
) {
  int32_t _if__result_2885;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS151 >= 0) {
    if (_M0L3lenS152 >= 0) {
      if (_M0L11src__offsetS153 >= 0) {
        if (_M0L11dst__offsetS154 >= 0) {
          int32_t _M0L6_2atmpS1360 = _M0L11src__offsetS153 + _M0L3lenS152;
          int32_t _M0L6_2atmpS1361 = Moonbit_array_length(_M0L3srcS155);
          if (_M0L6_2atmpS1360 <= _M0L6_2atmpS1361) {
            int32_t _M0L6_2atmpS1359 = _M0L11dst__offsetS154 + _M0L3lenS152;
            _if__result_2885 = _M0L6_2atmpS1359 <= _M0L13allocate__lenS151;
          } else {
            _if__result_2885 = 0;
          }
        } else {
          _if__result_2885 = 0;
        }
      } else {
        _if__result_2885 = 0;
      }
    } else {
      _if__result_2885 = 0;
    }
  } else {
    _if__result_2885 = 0;
  }
  if (_if__result_2885) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS155, _M0L13allocate__lenS151, _M0L4initS156, _M0L11src__offsetS153, _M0L11dst__offsetS154, _M0L3lenS152);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS157;
    int32_t _M0L6_2atmpS1363;
    moonbit_string_t _M0L6_2atmpS1362;
    uint16_t* _result_2886;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS157
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS157, (moonbit_string_t)moonbit_string_literal_38.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS157, _M0L13allocate__lenS151);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS157, (moonbit_string_t)moonbit_string_literal_39.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS157, _M0L11src__offsetS153);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS157, (moonbit_string_t)moonbit_string_literal_40.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS157, _M0L11dst__offsetS154);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS157, (moonbit_string_t)moonbit_string_literal_41.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS157, _M0L3lenS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS157, (moonbit_string_t)moonbit_string_literal_42.data);
    _M0L6_2atmpS1363 = Moonbit_array_length(_M0L3srcS155);
    moonbit_decref_cycle_free(_M0L3srcS155);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS157, _M0L6_2atmpS1363);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1362
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS157);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS157);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2886 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1362);
    moonbit_decref_cycle_free(_M0L6_2atmpS1362);
    return _result_2886;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS148,
  int32_t _M0L13allocate__lenS145,
  int32_t _M0L4initS146,
  int32_t _M0L11src__offsetS149,
  int32_t _M0L11dst__offsetS147,
  int32_t _M0L9blit__lenS150
) {
  uint16_t* _M0L3dstS144;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS144
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS145, _M0L4initS146);
  moonbit_incref_cycle_free(_M0L3dstS144);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS144, _M0L11dst__offsetS147, _M0L3srcS148, _M0L11src__offsetS149, _M0L9blit__lenS150, sizeof(uint16_t));
  return _M0L3dstS144;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS142
) {
  int32_t _M0L7initialS141;
  uint16_t* _M0L4dataS143;
  struct _M0TPB13StringBuilder* _block_2887;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS142 < 1) {
    _M0L7initialS141 = 1;
  } else {
    int32_t _M0L6_2atmpS1358 = _M0L10size__hintS142 + 1;
    _M0L7initialS141 = _M0L6_2atmpS1358 / 2;
  }
  _M0L4dataS143 = (uint16_t*)moonbit_make_string(_M0L7initialS141, 0);
  _block_2887
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2887)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 91, 0);
  _block_2887->$0 = _M0L4dataS143;
  _block_2887->$1 = 0;
  return _block_2887;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS140) {
  int32_t _M0L6_2atmpS1357;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1357 = (int32_t)_M0L4selfS140;
  return _M0L6_2atmpS1357;
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS120,
  int32_t _M0L13allocate__lenS116,
  int32_t _M0L3lenS117,
  int32_t _M0L11src__offsetS118,
  int32_t _M0L11dst__offsetS119
) {
  int32_t _if__result_2888;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS116 >= 0) {
    if (_M0L3lenS117 >= 0) {
      if (_M0L11src__offsetS118 >= 0) {
        if (_M0L11dst__offsetS119 >= 0) {
          int32_t _M0L6_2atmpS1338 = _M0L11src__offsetS118 + _M0L3lenS117;
          int32_t _M0L6_2atmpS1339;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1339
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS120);
          if (_M0L6_2atmpS1338 <= _M0L6_2atmpS1339) {
            int32_t _M0L6_2atmpS1337 = _M0L11dst__offsetS119 + _M0L3lenS117;
            _if__result_2888 = _M0L6_2atmpS1337 <= _M0L13allocate__lenS116;
          } else {
            _if__result_2888 = 0;
          }
        } else {
          _if__result_2888 = 0;
        }
      } else {
        _if__result_2888 = 0;
      }
    } else {
      _if__result_2888 = 0;
    }
  } else {
    _if__result_2888 = 0;
  }
  if (_if__result_2888) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS120, _M0L13allocate__lenS116, _M0L11src__offsetS118, _M0L11dst__offsetS119, _M0L3lenS117);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS121;
    int32_t _M0L6_2atmpS1341;
    moonbit_string_t _M0L6_2atmpS1340;
    float* _result_2889;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS121
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS121, (moonbit_string_t)moonbit_string_literal_38.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L13allocate__lenS116);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS121, (moonbit_string_t)moonbit_string_literal_39.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L11src__offsetS118);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS121, (moonbit_string_t)moonbit_string_literal_40.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L11dst__offsetS119);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS121, (moonbit_string_t)moonbit_string_literal_41.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L3lenS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS121, (moonbit_string_t)moonbit_string_literal_42.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1341 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS120);
    moonbit_decref_cycle_free(_M0L3srcS120);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS121, _M0L6_2atmpS1341);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1340
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS121);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS121);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2889
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1340);
    moonbit_decref_cycle_free(_M0L6_2atmpS1340);
    return _result_2889;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS126,
  int32_t _M0L13allocate__lenS122,
  int32_t _M0L3lenS123,
  int32_t _M0L11src__offsetS124,
  int32_t _M0L11dst__offsetS125
) {
  int32_t _if__result_2890;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS122 >= 0) {
    if (_M0L3lenS123 >= 0) {
      if (_M0L11src__offsetS124 >= 0) {
        if (_M0L11dst__offsetS125 >= 0) {
          int32_t _M0L6_2atmpS1343 = _M0L11src__offsetS124 + _M0L3lenS123;
          int32_t _M0L6_2atmpS1344;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1344
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS126);
          if (_M0L6_2atmpS1343 <= _M0L6_2atmpS1344) {
            int32_t _M0L6_2atmpS1342 = _M0L11dst__offsetS125 + _M0L3lenS123;
            _if__result_2890 = _M0L6_2atmpS1342 <= _M0L13allocate__lenS122;
          } else {
            _if__result_2890 = 0;
          }
        } else {
          _if__result_2890 = 0;
        }
      } else {
        _if__result_2890 = 0;
      }
    } else {
      _if__result_2890 = 0;
    }
  } else {
    _if__result_2890 = 0;
  }
  if (_if__result_2890) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS126, _M0L13allocate__lenS122, _M0L11src__offsetS124, _M0L11dst__offsetS125, _M0L3lenS123);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS127;
    int32_t _M0L6_2atmpS1346;
    moonbit_string_t _M0L6_2atmpS1345;
    int32_t* _result_2891;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS127
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_38.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L13allocate__lenS122);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_39.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L11src__offsetS124);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_40.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L11dst__offsetS125);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_41.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L3lenS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_42.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1346 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS126);
    moonbit_decref_cycle_free(_M0L3srcS126);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L6_2atmpS1346);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1345
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS127);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS127);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2891
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1345);
    moonbit_decref_cycle_free(_M0L6_2atmpS1345);
    return _result_2891;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS132,
  int32_t _M0L13allocate__lenS128,
  int32_t _M0L3lenS129,
  int32_t _M0L11src__offsetS130,
  int32_t _M0L11dst__offsetS131
) {
  int32_t _if__result_2892;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS128 >= 0) {
    if (_M0L3lenS129 >= 0) {
      if (_M0L11src__offsetS130 >= 0) {
        if (_M0L11dst__offsetS131 >= 0) {
          int32_t _M0L6_2atmpS1348 = _M0L11src__offsetS130 + _M0L3lenS129;
          int32_t _M0L6_2atmpS1349;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1349
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS132);
          if (_M0L6_2atmpS1348 <= _M0L6_2atmpS1349) {
            int32_t _M0L6_2atmpS1347 = _M0L11dst__offsetS131 + _M0L3lenS129;
            _if__result_2892 = _M0L6_2atmpS1347 <= _M0L13allocate__lenS128;
          } else {
            _if__result_2892 = 0;
          }
        } else {
          _if__result_2892 = 0;
        }
      } else {
        _if__result_2892 = 0;
      }
    } else {
      _if__result_2892 = 0;
    }
  } else {
    _if__result_2892 = 0;
  }
  if (_if__result_2892) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS128, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS132, _M0L11src__offsetS130, _M0L11dst__offsetS131, _M0L3lenS129);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS133;
    int32_t _M0L6_2atmpS1351;
    moonbit_string_t _M0L6_2atmpS1350;
    moonbit_string_t* _result_2893;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS133
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS133, (moonbit_string_t)moonbit_string_literal_38.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L13allocate__lenS128);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS133, (moonbit_string_t)moonbit_string_literal_39.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L11src__offsetS130);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS133, (moonbit_string_t)moonbit_string_literal_40.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L11dst__offsetS131);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS133, (moonbit_string_t)moonbit_string_literal_41.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L3lenS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS133, (moonbit_string_t)moonbit_string_literal_42.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1351 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS132);
    moonbit_decref_cycle_free(_M0L3srcS132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS133, _M0L6_2atmpS1351);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1350
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS133);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS133);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2893
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1350);
    moonbit_decref_cycle_free(_M0L6_2atmpS1350);
    return _result_2893;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS138,
  int32_t _M0L13allocate__lenS134,
  int32_t _M0L3lenS135,
  int32_t _M0L11src__offsetS136,
  int32_t _M0L11dst__offsetS137
) {
  int32_t _if__result_2894;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS134 >= 0) {
    if (_M0L3lenS135 >= 0) {
      if (_M0L11src__offsetS136 >= 0) {
        if (_M0L11dst__offsetS137 >= 0) {
          int32_t _M0L6_2atmpS1353 = _M0L11src__offsetS136 + _M0L3lenS135;
          int32_t _M0L6_2atmpS1354;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1354
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS138);
          if (_M0L6_2atmpS1353 <= _M0L6_2atmpS1354) {
            int32_t _M0L6_2atmpS1352 = _M0L11dst__offsetS137 + _M0L3lenS135;
            _if__result_2894 = _M0L6_2atmpS1352 <= _M0L13allocate__lenS134;
          } else {
            _if__result_2894 = 0;
          }
        } else {
          _if__result_2894 = 0;
        }
      } else {
        _if__result_2894 = 0;
      }
    } else {
      _if__result_2894 = 0;
    }
  } else {
    _if__result_2894 = 0;
  }
  if (_if__result_2894) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS134, 0, _M0L3srcS138, _M0L11src__offsetS136, _M0L11dst__offsetS137, _M0L3lenS135);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS139;
    int32_t _M0L6_2atmpS1356;
    moonbit_string_t _M0L6_2atmpS1355;
    struct _M0TUsiE** _result_2895;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS139
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS139, (moonbit_string_t)moonbit_string_literal_38.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS139, _M0L13allocate__lenS134);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS139, (moonbit_string_t)moonbit_string_literal_39.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS139, _M0L11src__offsetS136);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS139, (moonbit_string_t)moonbit_string_literal_40.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS139, _M0L11dst__offsetS137);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS139, (moonbit_string_t)moonbit_string_literal_41.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS139, _M0L3lenS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS139, (moonbit_string_t)moonbit_string_literal_42.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1356 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS138);
    moonbit_decref_cycle_free(_M0L3srcS138);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS139, _M0L6_2atmpS1356);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1355
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS139);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS139);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2895
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1355);
    moonbit_decref_cycle_free(_M0L6_2atmpS1355);
    return _result_2895;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS111,
  moonbit_string_t _M0L3objS110
) {
  struct _M0TPB6Logger _M0L6_2atmpS1334;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS111);
  _M0L6_2atmpS1334
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS111
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS110, _M0L6_2atmpS1334);
  if (_M0L6_2atmpS1334.$1) {
    moonbit_decref(_M0L6_2atmpS1334.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS113,
  int32_t _M0L3objS112
) {
  struct _M0TPB6Logger _M0L6_2atmpS1335;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS113);
  _M0L6_2atmpS1335
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS113
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS112, _M0L6_2atmpS1335);
  if (_M0L6_2atmpS1335.$1) {
    moonbit_decref(_M0L6_2atmpS1335.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS115,
  uint64_t _M0L3objS114
) {
  struct _M0TPB6Logger _M0L6_2atmpS1336;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS115);
  _M0L6_2atmpS1336
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS115
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS114, _M0L6_2atmpS1336);
  if (_M0L6_2atmpS1336.$1) {
    moonbit_decref(_M0L6_2atmpS1336.$1);
  }
  return 0;
}

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS89,
  int32_t _M0L13allocate__lenS87,
  int32_t _M0L11src__offsetS90,
  int32_t _M0L11dst__offsetS88,
  int32_t _M0L9blit__lenS91
) {
  float* _M0L3dstS86;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS86 = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS87);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS86, _M0L11dst__offsetS88, _M0L3srcS89, _M0L11src__offsetS90, _M0L9blit__lenS91);
  moonbit_decref_cycle_free(_M0L3srcS89);
  return _M0L3dstS86;
}

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t* _M0L3srcS95,
  int32_t _M0L13allocate__lenS93,
  int32_t _M0L11src__offsetS96,
  int32_t _M0L11dst__offsetS94,
  int32_t _M0L9blit__lenS97
) {
  int32_t* _M0L3dstS92;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS92
  = (int32_t*)moonbit_make_int32_array_raw(_M0L13allocate__lenS93);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L3dstS92, _M0L11dst__offsetS94, _M0L3srcS95, _M0L11src__offsetS96, _M0L9blit__lenS97);
  moonbit_decref_cycle_free(_M0L3srcS95);
  return _M0L3dstS92;
}

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t* _M0L3srcS101,
  int32_t _M0L13allocate__lenS99,
  int32_t _M0L11src__offsetS102,
  int32_t _M0L11dst__offsetS100,
  int32_t _M0L9blit__lenS103
) {
  moonbit_string_t* _M0L3dstS98;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS98
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L13allocate__lenS99, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGsE(_M0L3dstS98, _M0L11dst__offsetS100, _M0L3srcS101, _M0L11src__offsetS102, _M0L9blit__lenS103);
  moonbit_decref_cycle_free(_M0L3srcS101);
  return _M0L3dstS98;
}

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE** _M0L3srcS107,
  int32_t _M0L13allocate__lenS105,
  int32_t _M0L11src__offsetS108,
  int32_t _M0L11dst__offsetS106,
  int32_t _M0L9blit__lenS109
) {
  struct _M0TUsiE** _M0L3dstS104;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS104
  = (struct _M0TUsiE**)moonbit_make_ref_array(_M0L13allocate__lenS105, 0);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGUsiEE(_M0L3dstS104, _M0L11dst__offsetS106, _M0L3srcS107, _M0L11src__offsetS108, _M0L9blit__lenS109);
  moonbit_decref_cycle_free(_M0L3srcS107);
  return _M0L3dstS104;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t* _M0L3dstS66,
  int32_t _M0L11dst__offsetS67,
  int32_t* _M0L3srcS68,
  int32_t _M0L11src__offsetS69,
  int32_t _M0L3lenS70
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS68);
  moonbit_incref_cycle_free(_M0L3dstS66);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS66, _M0L11dst__offsetS67, _M0L3srcS68, _M0L11src__offsetS69, _M0L3lenS70, sizeof(int32_t));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS71,
  int32_t _M0L11dst__offsetS72,
  float* _M0L3srcS73,
  int32_t _M0L11src__offsetS74,
  int32_t _M0L3lenS75
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS73);
  moonbit_incref_cycle_free(_M0L3dstS71);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS71, _M0L11dst__offsetS72, _M0L3srcS73, _M0L11src__offsetS74, _M0L3lenS75, sizeof(float));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS76,
  int32_t _M0L11dst__offsetS77,
  moonbit_string_t* _M0L3srcS78,
  int32_t _M0L11src__offsetS79,
  int32_t _M0L3lenS80
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS78);
  moonbit_incref_cycle_free(_M0L3dstS76);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS76, _M0L11dst__offsetS77, _M0L3srcS78, _M0L11src__offsetS79, _M0L3lenS80);
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS81,
  int32_t _M0L11dst__offsetS82,
  struct _M0TUsiE** _M0L3srcS83,
  int32_t _M0L11src__offsetS84,
  int32_t _M0L3lenS85
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS83);
  moonbit_incref_cycle_free(_M0L3dstS81);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS81, _M0L11dst__offsetS82, _M0L3srcS83, _M0L11src__offsetS84, _M0L3lenS85);
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS21,
  int32_t _M0L11dst__offsetS23,
  float* _M0L3srcS22,
  int32_t _M0L11src__offsetS24,
  int32_t _M0L3lenS26
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS21 == _M0L3srcS22 && _M0L11dst__offsetS23 < _M0L11src__offsetS24
  ) {
    int32_t _M0L1iS25 = 0;
    while (1) {
      if (_M0L1iS25 < _M0L3lenS26) {
        int32_t _M0L6_2atmpS1289 = _M0L11dst__offsetS23 + _M0L1iS25;
        int32_t _M0L6_2atmpS1291 = _M0L11src__offsetS24 + _M0L1iS25;
        float _M0L6_2atmpS1290;
        int32_t _M0L6_2atmpS1292;
        if (
          _M0L6_2atmpS1291 < 0
          || _M0L6_2atmpS1291 >= Moonbit_array_length(_M0L3srcS22)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1290 = (float)_M0L3srcS22[_M0L6_2atmpS1291];
        if (
          _M0L6_2atmpS1289 < 0
          || _M0L6_2atmpS1289 >= Moonbit_array_length(_M0L3dstS21)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS21[_M0L6_2atmpS1289] = _M0L6_2atmpS1290;
        _M0L6_2atmpS1292 = _M0L1iS25 + 1;
        _M0L1iS25 = _M0L6_2atmpS1292;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS22);
        moonbit_decref_cycle_free(_M0L3dstS21);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1297 = _M0L3lenS26 - 1;
    int32_t _M0L1iS28 = _M0L6_2atmpS1297;
    while (1) {
      if (_M0L1iS28 >= 0) {
        int32_t _M0L6_2atmpS1293 = _M0L11dst__offsetS23 + _M0L1iS28;
        int32_t _M0L6_2atmpS1295 = _M0L11src__offsetS24 + _M0L1iS28;
        float _M0L6_2atmpS1294;
        int32_t _M0L6_2atmpS1296;
        if (
          _M0L6_2atmpS1295 < 0
          || _M0L6_2atmpS1295 >= Moonbit_array_length(_M0L3srcS22)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1294 = (float)_M0L3srcS22[_M0L6_2atmpS1295];
        if (
          _M0L6_2atmpS1293 < 0
          || _M0L6_2atmpS1293 >= Moonbit_array_length(_M0L3dstS21)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS21[_M0L6_2atmpS1293] = _M0L6_2atmpS1294;
        _M0L6_2atmpS1296 = _M0L1iS28 - 1;
        _M0L1iS28 = _M0L6_2atmpS1296;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS22);
        moonbit_decref_cycle_free(_M0L3dstS21);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS30,
  int32_t _M0L11dst__offsetS32,
  int32_t* _M0L3srcS31,
  int32_t _M0L11src__offsetS33,
  int32_t _M0L3lenS35
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS30 == _M0L3srcS31 && _M0L11dst__offsetS32 < _M0L11src__offsetS33
  ) {
    int32_t _M0L1iS34 = 0;
    while (1) {
      if (_M0L1iS34 < _M0L3lenS35) {
        int32_t _M0L6_2atmpS1298 = _M0L11dst__offsetS32 + _M0L1iS34;
        int32_t _M0L6_2atmpS1300 = _M0L11src__offsetS33 + _M0L1iS34;
        int32_t _M0L6_2atmpS1299;
        int32_t _M0L6_2atmpS1301;
        if (
          _M0L6_2atmpS1300 < 0
          || _M0L6_2atmpS1300 >= Moonbit_array_length(_M0L3srcS31)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1299 = (int32_t)_M0L3srcS31[_M0L6_2atmpS1300];
        if (
          _M0L6_2atmpS1298 < 0
          || _M0L6_2atmpS1298 >= Moonbit_array_length(_M0L3dstS30)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS30[_M0L6_2atmpS1298] = _M0L6_2atmpS1299;
        _M0L6_2atmpS1301 = _M0L1iS34 + 1;
        _M0L1iS34 = _M0L6_2atmpS1301;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS31);
        moonbit_decref_cycle_free(_M0L3dstS30);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1306 = _M0L3lenS35 - 1;
    int32_t _M0L1iS37 = _M0L6_2atmpS1306;
    while (1) {
      if (_M0L1iS37 >= 0) {
        int32_t _M0L6_2atmpS1302 = _M0L11dst__offsetS32 + _M0L1iS37;
        int32_t _M0L6_2atmpS1304 = _M0L11src__offsetS33 + _M0L1iS37;
        int32_t _M0L6_2atmpS1303;
        int32_t _M0L6_2atmpS1305;
        if (
          _M0L6_2atmpS1304 < 0
          || _M0L6_2atmpS1304 >= Moonbit_array_length(_M0L3srcS31)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1303 = (int32_t)_M0L3srcS31[_M0L6_2atmpS1304];
        if (
          _M0L6_2atmpS1302 < 0
          || _M0L6_2atmpS1302 >= Moonbit_array_length(_M0L3dstS30)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS30[_M0L6_2atmpS1302] = _M0L6_2atmpS1303;
        _M0L6_2atmpS1305 = _M0L1iS37 - 1;
        _M0L1iS37 = _M0L6_2atmpS1305;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS31);
        moonbit_decref_cycle_free(_M0L3dstS30);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t* _M0L3dstS39,
  int32_t _M0L11dst__offsetS41,
  uint16_t* _M0L3srcS40,
  int32_t _M0L11src__offsetS42,
  int32_t _M0L3lenS44
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS39 == _M0L3srcS40 && _M0L11dst__offsetS41 < _M0L11src__offsetS42
  ) {
    int32_t _M0L1iS43 = 0;
    while (1) {
      if (_M0L1iS43 < _M0L3lenS44) {
        int32_t _M0L6_2atmpS1307 = _M0L11dst__offsetS41 + _M0L1iS43;
        int32_t _M0L6_2atmpS1309 = _M0L11src__offsetS42 + _M0L1iS43;
        int32_t _M0L6_2atmpS1308;
        int32_t _M0L6_2atmpS1310;
        if (
          _M0L6_2atmpS1309 < 0
          || _M0L6_2atmpS1309 >= Moonbit_array_length(_M0L3srcS40)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1308 = (int32_t)_M0L3srcS40[_M0L6_2atmpS1309];
        if (
          _M0L6_2atmpS1307 < 0
          || _M0L6_2atmpS1307 >= Moonbit_array_length(_M0L3dstS39)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS39[_M0L6_2atmpS1307] = _M0L6_2atmpS1308;
        _M0L6_2atmpS1310 = _M0L1iS43 + 1;
        _M0L1iS43 = _M0L6_2atmpS1310;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS40);
        moonbit_decref_cycle_free(_M0L3dstS39);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1315 = _M0L3lenS44 - 1;
    int32_t _M0L1iS46 = _M0L6_2atmpS1315;
    while (1) {
      if (_M0L1iS46 >= 0) {
        int32_t _M0L6_2atmpS1311 = _M0L11dst__offsetS41 + _M0L1iS46;
        int32_t _M0L6_2atmpS1313 = _M0L11src__offsetS42 + _M0L1iS46;
        int32_t _M0L6_2atmpS1312;
        int32_t _M0L6_2atmpS1314;
        if (
          _M0L6_2atmpS1313 < 0
          || _M0L6_2atmpS1313 >= Moonbit_array_length(_M0L3srcS40)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1312 = (int32_t)_M0L3srcS40[_M0L6_2atmpS1313];
        if (
          _M0L6_2atmpS1311 < 0
          || _M0L6_2atmpS1311 >= Moonbit_array_length(_M0L3dstS39)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS39[_M0L6_2atmpS1311] = _M0L6_2atmpS1312;
        _M0L6_2atmpS1314 = _M0L1iS46 - 1;
        _M0L1iS46 = _M0L6_2atmpS1314;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS40);
        moonbit_decref_cycle_free(_M0L3dstS39);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS48,
  int32_t _M0L11dst__offsetS50,
  moonbit_string_t* _M0L3srcS49,
  int32_t _M0L11src__offsetS51,
  int32_t _M0L3lenS53
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS48 == _M0L3srcS49 && _M0L11dst__offsetS50 < _M0L11src__offsetS51
  ) {
    int32_t _M0L1iS52 = 0;
    while (1) {
      if (_M0L1iS52 < _M0L3lenS53) {
        int32_t _M0L6_2atmpS1316 = _M0L11dst__offsetS50 + _M0L1iS52;
        int32_t _M0L6_2atmpS1318 = _M0L11src__offsetS51 + _M0L1iS52;
        moonbit_string_t _M0L6_2atmpS1317;
        moonbit_string_t _M0L6_2aoldS2733;
        int32_t _M0L6_2atmpS1319;
        if (
          _M0L6_2atmpS1318 < 0
          || _M0L6_2atmpS1318 >= Moonbit_array_length(_M0L3srcS49)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1317 = (moonbit_string_t)_M0L3srcS49[_M0L6_2atmpS1318];
        if (
          _M0L6_2atmpS1316 < 0
          || _M0L6_2atmpS1316 >= Moonbit_array_length(_M0L3dstS48)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2733 = (moonbit_string_t)_M0L3dstS48[_M0L6_2atmpS1316];
        moonbit_incref_cycle_free(_M0L6_2atmpS1317);
        moonbit_decref_cycle_free(_M0L6_2aoldS2733);
        _M0L3dstS48[_M0L6_2atmpS1316] = _M0L6_2atmpS1317;
        _M0L6_2atmpS1319 = _M0L1iS52 + 1;
        _M0L1iS52 = _M0L6_2atmpS1319;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS49);
        moonbit_decref_cycle_free(_M0L3dstS48);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1324 = _M0L3lenS53 - 1;
    int32_t _M0L1iS55 = _M0L6_2atmpS1324;
    while (1) {
      if (_M0L1iS55 >= 0) {
        int32_t _M0L6_2atmpS1320 = _M0L11dst__offsetS50 + _M0L1iS55;
        int32_t _M0L6_2atmpS1322 = _M0L11src__offsetS51 + _M0L1iS55;
        moonbit_string_t _M0L6_2atmpS1321;
        moonbit_string_t _M0L6_2aoldS2734;
        int32_t _M0L6_2atmpS1323;
        if (
          _M0L6_2atmpS1322 < 0
          || _M0L6_2atmpS1322 >= Moonbit_array_length(_M0L3srcS49)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1321 = (moonbit_string_t)_M0L3srcS49[_M0L6_2atmpS1322];
        if (
          _M0L6_2atmpS1320 < 0
          || _M0L6_2atmpS1320 >= Moonbit_array_length(_M0L3dstS48)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2734 = (moonbit_string_t)_M0L3dstS48[_M0L6_2atmpS1320];
        moonbit_incref_cycle_free(_M0L6_2atmpS1321);
        moonbit_decref_cycle_free(_M0L6_2aoldS2734);
        _M0L3dstS48[_M0L6_2atmpS1320] = _M0L6_2atmpS1321;
        _M0L6_2atmpS1323 = _M0L1iS55 - 1;
        _M0L1iS55 = _M0L6_2atmpS1323;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS49);
        moonbit_decref_cycle_free(_M0L3dstS48);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS57,
  int32_t _M0L11dst__offsetS59,
  struct _M0TUsiE** _M0L3srcS58,
  int32_t _M0L11src__offsetS60,
  int32_t _M0L3lenS62
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS57 == _M0L3srcS58 && _M0L11dst__offsetS59 < _M0L11src__offsetS60
  ) {
    int32_t _M0L1iS61 = 0;
    while (1) {
      if (_M0L1iS61 < _M0L3lenS62) {
        int32_t _M0L6_2atmpS1325 = _M0L11dst__offsetS59 + _M0L1iS61;
        int32_t _M0L6_2atmpS1327 = _M0L11src__offsetS60 + _M0L1iS61;
        struct _M0TUsiE* _M0L6_2atmpS1326;
        struct _M0TUsiE* _M0L6_2aoldS2735;
        int32_t _M0L6_2atmpS1328;
        if (
          _M0L6_2atmpS1327 < 0
          || _M0L6_2atmpS1327 >= Moonbit_array_length(_M0L3srcS58)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1326 = (struct _M0TUsiE*)_M0L3srcS58[_M0L6_2atmpS1327];
        if (
          _M0L6_2atmpS1325 < 0
          || _M0L6_2atmpS1325 >= Moonbit_array_length(_M0L3dstS57)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2735 = (struct _M0TUsiE*)_M0L3dstS57[_M0L6_2atmpS1325];
        if (_M0L6_2atmpS1326) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1326);
        }
        if (_M0L6_2aoldS2735) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2735);
        }
        _M0L3dstS57[_M0L6_2atmpS1325] = _M0L6_2atmpS1326;
        _M0L6_2atmpS1328 = _M0L1iS61 + 1;
        _M0L1iS61 = _M0L6_2atmpS1328;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS58);
        moonbit_decref_cycle_free(_M0L3dstS57);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1333 = _M0L3lenS62 - 1;
    int32_t _M0L1iS64 = _M0L6_2atmpS1333;
    while (1) {
      if (_M0L1iS64 >= 0) {
        int32_t _M0L6_2atmpS1329 = _M0L11dst__offsetS59 + _M0L1iS64;
        int32_t _M0L6_2atmpS1331 = _M0L11src__offsetS60 + _M0L1iS64;
        struct _M0TUsiE* _M0L6_2atmpS1330;
        struct _M0TUsiE* _M0L6_2aoldS2736;
        int32_t _M0L6_2atmpS1332;
        if (
          _M0L6_2atmpS1331 < 0
          || _M0L6_2atmpS1331 >= Moonbit_array_length(_M0L3srcS58)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1330 = (struct _M0TUsiE*)_M0L3srcS58[_M0L6_2atmpS1331];
        if (
          _M0L6_2atmpS1329 < 0
          || _M0L6_2atmpS1329 >= Moonbit_array_length(_M0L3dstS57)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2736 = (struct _M0TUsiE*)_M0L3dstS57[_M0L6_2atmpS1329];
        if (_M0L6_2atmpS1330) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1330);
        }
        if (_M0L6_2aoldS2736) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2736);
        }
        _M0L3dstS57[_M0L6_2atmpS1329] = _M0L6_2atmpS1330;
        _M0L6_2atmpS1332 = _M0L1iS64 - 1;
        _M0L1iS64 = _M0L6_2atmpS1332;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS58);
        moonbit_decref_cycle_free(_M0L3dstS57);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPB18UninitializedArray6lengthGfE(float* _M0L4selfS17) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS17);
}

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t* _M0L4selfS18) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS18);
}

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t* _M0L4selfS19) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS19);
}

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(
  struct _M0TUsiE** _M0L4selfS20
) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS20);
}

int32_t _M0IPB7FailurePB4Show6output(
  void* _M0L10_2ax__6387S13,
  struct _M0TPB6Logger _M0L10_2ax__6388S16
) {
  struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS14;
  moonbit_string_t _M0L15_2a_2aarg__6389S15;
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2aFailureS14
  = (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L10_2ax__6387S13;
  _M0L15_2a_2aarg__6389S15 = _M0L10_2aFailureS14->$0;
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S16.$0->$method_0(_M0L10_2ax__6388S16.$1, (moonbit_string_t)moonbit_string_literal_43.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S16, _M0L15_2a_2aarg__6389S15);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S16.$0->$method_0(_M0L10_2ax__6388S16.$1, (moonbit_string_t)moonbit_string_literal_44.data);
  return 0;
}

int32_t _M0MPB6Logger13write__objectGfE(
  struct _M0TPB6Logger _M0L4selfS10,
  float _M0L3objS9
) {
  #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 180 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IP016_24default__implPB4Show6outputGfE(_M0L3objS9, _M0L4selfS10);
  return 0;
}

int32_t _M0MPB6Logger13write__objectGsE(
  struct _M0TPB6Logger _M0L4selfS12,
  moonbit_string_t _M0L3objS11
) {
  #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 180 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS11, _M0L4selfS12);
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

moonbit_string_t _M0FPC15abort5abortGsE(moonbit_string_t _M0L3msgS2) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS2);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t _M0L3msgS3) {
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

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(
  moonbit_string_t _M0L3msgS5
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS5);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

moonbit_string_t* _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(
  moonbit_string_t _M0L3msgS6
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS6);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

struct _M0TUsiE** _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(
  moonbit_string_t _M0L3msgS7
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS7);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

int32_t _M0FPC15abort5abortGiE(moonbit_string_t _M0L3msgS8) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS8);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1258) {
  switch (Moonbit_object_tag(_M0L4_2aeS1258)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_45.data;
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_46.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1258);
      break;
    }
    
    case 3: {
      return (moonbit_string_t)moonbit_string_literal_47.data;
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_48.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1284,
  struct _M0TPB4Show _M0L8_2aparamS1283
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1282 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1284;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1282, _M0L8_2aparamS1283);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1281,
  struct _M0TPB4Show _M0L8_2aparamS1280
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1279 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1281;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1279, _M0L8_2aparamS1280);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1278,
  int32_t _M0L8_2aparamS1277
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1276 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1278;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1276, _M0L8_2aparamS1277);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1275,
  struct _M0TPC16string10StringView _M0L8_2aparamS1274
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1273 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1275;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1273, _M0L8_2aparamS1274);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1272,
  moonbit_string_t _M0L8_2aparamS1269,
  int32_t _M0L8_2aparamS1270,
  int32_t _M0L8_2aparamS1271
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1268 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1272;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1268, _M0L8_2aparamS1269, _M0L8_2aparamS1270, _M0L8_2aparamS1271);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1267,
  moonbit_string_t _M0L8_2aparamS1266
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1265 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1267;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1265, _M0L8_2aparamS1266);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1288;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1251;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1252;
  int32_t _M0L7_2abindS1253;
  struct _M0TUsiE** _M0L7_2abindS1254;
  int32_t _M0L6_2acntS2741;
  int32_t _M0L2__S1255;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1288
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1251
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1251)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 94, 0);
  _M0L12async__testsS1251->$0 = _M0L6_2atmpS1288;
  _M0L12async__testsS1251->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1252
  = _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1253 = _M0L7_2abindS1252->$1;
  _M0L7_2abindS1254 = _M0L7_2abindS1252->$0;
  _M0L6_2acntS2741
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1252));
  if (_M0L6_2acntS2741 > 1) {
    int32_t _M0L11_2anew__cntS2742 = _M0L6_2acntS2741 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1252), _M0L11_2anew__cntS2742);
    moonbit_incref_cycle_free(_M0L7_2abindS1254);
  } else if (_M0L6_2acntS2741 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1252);
  }
  _M0L2__S1255 = 0;
  while (1) {
    if (_M0L2__S1255 < _M0L7_2abindS1253) {
      struct _M0TUsiE* _M0L3argS1256 =
        (struct _M0TUsiE*)_M0L7_2abindS1254[_M0L2__S1255];
      moonbit_string_t _M0L6_2atmpS1285 = _M0L3argS1256->$0;
      int32_t _M0L6_2atmpS1286 = _M0L3argS1256->$1;
      int32_t _M0L6_2atmpS1287;
      moonbit_incref_cycle_free(_M0L6_2atmpS1285);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1251, _M0L6_2atmpS1285, _M0L6_2atmpS1286);
      moonbit_decref_cycle_free(_M0L6_2atmpS1285);
      _M0L6_2atmpS1287 = _M0L2__S1255 + 1;
      _M0L2__S1255 = _M0L6_2atmpS1287;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1254);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_demo\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples26stdp__demo__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1251);
  moonbit_decref_cycle_free(_M0L12async__testsS1251);
  moonbit_flush_cycles();
  return 0;
}