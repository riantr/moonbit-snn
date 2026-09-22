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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c870;

struct _M0TPB8MutLocalGiE;

struct _M0TP26RiantR8snn__mbt12STDPGerstner;

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

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c875;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TPB5ArrayGUsiEE;

struct _M0TPB5ArrayGsE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0DTPC16option6OptionGfE4Some;

struct _M0TWEu;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
};

struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c870 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error {
  struct moonbit_result_0(* code)(
    struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error*,
    struct _M0TWuEu*,
    struct _M0TWRPC15error5ErrorEu*
  );
  
};

struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c875 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
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

struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
};

struct _M0TWuEu {
  int32_t(* code)(struct _M0TWuEu*, int32_t);
  
};

struct _M0KTPB6LoggerTPB13StringBuilder {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

struct _M0DTPC16option6OptionGfE4Some {
  float $0;
  
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

struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS882(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS875(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS870(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS847(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S840(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
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

struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0MP26RiantR8snn__mbt12STDPGerstner3new(
  
);

#define _M0FP26RiantR8snn__mbt4expf expf

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

struct _M0TPB5ArrayGsE* _M0MPC15array5Array4makeGsE(
  int32_t,
  moonbit_string_t
);

int32_t _M0MPC15array5Array3setGsE(
  struct _M0TPB5ArrayGsE*,
  int32_t,
  moonbit_string_t
);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

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

struct _M0TPB5ArrayGsE* _M0MPC15array5Array20unsafe__make__uninitGsE(int32_t);

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(uint64_t*, int32_t);

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(uint32_t*, int32_t);

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(uint64_t);

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t);

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t);

int32_t _M0MPC15array5Array4pushGfE(struct _M0TPB5ArrayGfE*, float);

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE*,
  moonbit_string_t
);

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
  struct _M0TUsiE*
);

int32_t _M0MPC15array5Array7reallocGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array7reallocGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE*,
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

int32_t _M0MPC15array5Array8capacityGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array8capacityGsE(struct _M0TPB5ArrayGsE*);

int32_t _M0MPC15array5Array8capacityGUsiEE(struct _M0TPB5ArrayGUsiEE*);

int32_t _M0FPB23array__growth__capacity(int32_t, int32_t, int32_t);

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

int32_t _M0MPB18UninitializedArray6lengthGfE(float*);

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t*);

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(struct _M0TUsiE**);

int32_t _M0IPB7FailurePB4Show6output(void*, struct _M0TPB6Logger);

int32_t _M0MPB6Logger13write__objectGsE(
  struct _M0TPB6Logger,
  moonbit_string_t
);

moonbit_string_t _M0FPC15abort5abortGsE(moonbit_string_t);

int32_t _M0FPC15abort5abortGuE(moonbit_string_t);

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t);

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

moonbit_string_t* moonbit_rt_get_cli_args();

float expf(float);

struct { int32_t rc; uint32_t meta; uint16_t const data[118]; 
} const moonbit_string_literal_41 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 117, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 115, 116, 100, 112, 95, 107, 101, 
    114, 110, 101, 108, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 
    101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 
    116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 
    108, 83, 107, 105, 112, 84, 101, 115, 116, 46, 77, 111, 111, 110, 
    66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 
    110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 
    116, 0
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
} const moonbit_string_literal_28 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_26 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_34 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_30 =
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
} const moonbit_string_literal_42 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[116]; 
} const moonbit_string_literal_39 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 115, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 115, 116, 100, 112, 95, 107, 101, 
    114, 110, 101, 108, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 
    101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 
    116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 
    108, 74, 115, 69, 114, 114, 111, 114, 46, 77, 111, 111, 110, 66, 
    105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 
    116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_25 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_23 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_13 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 32, 8594, 
    32, 43, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_32 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_29 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_38 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_11 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 32, 124, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_24 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[15]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 14, 105, 110, 
    118, 97, 108, 105, 100, 32, 108, 101, 110, 103, 116, 104, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_27 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_9 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 32, 0};

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
} const moonbit_string_literal_36 =
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
} const moonbit_string_literal_33 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[36]; 
} const moonbit_string_literal_10 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 35, 83, 84, 
    68, 80, 32, 107, 101, 114, 110, 101, 108, 32, 40, 71, 101, 114, 115, 
    116, 110, 101, 114, 32, 49, 57, 57, 54, 41, 58, 32, 916, 87, 40, 
    916, 116, 41, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_37 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_20 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_14 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 32, 109, 115, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[7]; 
} const moonbit_string_literal_12 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 6, 916, 116, 
    32, 61, 32, 45, 0
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS882$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS882
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[39] =
  {
    sizeof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c870)
    / 4, 1,
    offsetof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c870, $1)
    / 4
    * 2,
    sizeof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c875)
    / 4, 1,
    offsetof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c875, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS1890
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS903,
  moonbit_string_t _M0L8filenameS872,
  int32_t _M0L5indexS874
) {
  struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c870* _closure_1914;
  struct _M0TWEu* _M0L13handle__startS870;
  struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c875* _closure_1915;
  struct _M0TWssbEu* _M0L14handle__resultS875;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS882;
  void* _M0L11_2atry__errS897;
  struct moonbit_result_0 _tmp_1917;
  int32_t _handle__error__result_1918;
  int32_t _M0L6_2atmpS1878;
  void* _M0L3errS898;
  moonbit_string_t _M0L4nameS900;
  struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS901;
  moonbit_string_t _M0L7_2anameS902;
  int32_t _M0L6_2acntS1908;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS872);
  _closure_1914
  = (struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c870*)moonbit_malloc(sizeof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c870));
  Moonbit_object_header(_closure_1914)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_1914->code
  = &_M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS870;
  _closure_1914->$0 = _M0L5indexS874;
  _closure_1914->$1 = _M0L8filenameS872;
  _M0L13handle__startS870 = (struct _M0TWEu*)_closure_1914;
  moonbit_incref_cycle_free(_M0L8filenameS872);
  _closure_1915
  = (struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c875*)moonbit_malloc(sizeof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c875));
  Moonbit_object_header(_closure_1915)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_1915->code
  = &_M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS875;
  _closure_1915->$0 = _M0L5indexS874;
  _closure_1915->$1 = _M0L8filenameS872;
  _M0L14handle__resultS875 = (struct _M0TWssbEu*)_closure_1915;
  _M0L17error__to__stringS882
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS882$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _tmp_1917
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS903, _M0L8filenameS872, _M0L5indexS874, _M0L13handle__startS870, _M0L14handle__resultS875, _M0L17error__to__stringS882);
  if (_tmp_1917.tag) {
    int32_t const _M0L5_2aokS1887 = _tmp_1917.data.ok;
    _handle__error__result_1918 = _M0L5_2aokS1887;
  } else {
    void* const _M0L6_2aerrS1888 = _tmp_1917.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS882);
    moonbit_decref_cycle_free(_M0L13handle__startS870);
    _M0L11_2atry__errS897 = _M0L6_2aerrS1888;
    goto join_896;
  }
  if (_handle__error__result_1918) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS882);
    moonbit_decref_cycle_free(_M0L13handle__startS870);
    _M0L6_2atmpS1878 = 1;
  } else {
    struct moonbit_result_0 _tmp_1919;
    int32_t _handle__error__result_1920;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
    _tmp_1919
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS903, _M0L8filenameS872, _M0L5indexS874, _M0L13handle__startS870, _M0L14handle__resultS875, _M0L17error__to__stringS882);
    if (_tmp_1919.tag) {
      int32_t const _M0L5_2aokS1885 = _tmp_1919.data.ok;
      _handle__error__result_1920 = _M0L5_2aokS1885;
    } else {
      void* const _M0L6_2aerrS1886 = _tmp_1919.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS882);
      moonbit_decref_cycle_free(_M0L13handle__startS870);
      _M0L11_2atry__errS897 = _M0L6_2aerrS1886;
      goto join_896;
    }
    if (_handle__error__result_1920) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS882);
      moonbit_decref_cycle_free(_M0L13handle__startS870);
      _M0L6_2atmpS1878 = 1;
    } else {
      struct moonbit_result_0 _tmp_1921;
      int32_t _handle__error__result_1922;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
      _tmp_1921
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS903, _M0L8filenameS872, _M0L5indexS874, _M0L13handle__startS870, _M0L14handle__resultS875, _M0L17error__to__stringS882);
      if (_tmp_1921.tag) {
        int32_t const _M0L5_2aokS1883 = _tmp_1921.data.ok;
        _handle__error__result_1922 = _M0L5_2aokS1883;
      } else {
        void* const _M0L6_2aerrS1884 = _tmp_1921.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS882);
        moonbit_decref_cycle_free(_M0L13handle__startS870);
        _M0L11_2atry__errS897 = _M0L6_2aerrS1884;
        goto join_896;
      }
      if (_handle__error__result_1922) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS882);
        moonbit_decref_cycle_free(_M0L13handle__startS870);
        _M0L6_2atmpS1878 = 1;
      } else {
        struct moonbit_result_0 _tmp_1923;
        int32_t _handle__error__result_1924;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
        _tmp_1923
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS903, _M0L8filenameS872, _M0L5indexS874, _M0L13handle__startS870, _M0L14handle__resultS875, _M0L17error__to__stringS882);
        if (_tmp_1923.tag) {
          int32_t const _M0L5_2aokS1881 = _tmp_1923.data.ok;
          _handle__error__result_1924 = _M0L5_2aokS1881;
        } else {
          void* const _M0L6_2aerrS1882 = _tmp_1923.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS882);
          moonbit_decref_cycle_free(_M0L13handle__startS870);
          _M0L11_2atry__errS897 = _M0L6_2aerrS1882;
          goto join_896;
        }
        if (_handle__error__result_1924) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS882);
          moonbit_decref_cycle_free(_M0L13handle__startS870);
          _M0L6_2atmpS1878 = 1;
        } else {
          struct moonbit_result_0 _tmp_1925;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
          _tmp_1925
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS903, _M0L8filenameS872, _M0L5indexS874, _M0L13handle__startS870, _M0L14handle__resultS875, _M0L17error__to__stringS882);
          moonbit_decref_cycle_free(_M0L13handle__startS870);
          moonbit_decref_cycle_free(_M0L17error__to__stringS882);
          if (_tmp_1925.tag) {
            int32_t const _M0L5_2aokS1879 = _tmp_1925.data.ok;
            _M0L6_2atmpS1878 = _M0L5_2aokS1879;
          } else {
            void* const _M0L6_2aerrS1880 = _tmp_1925.data.err;
            _M0L11_2atry__errS897 = _M0L6_2aerrS1880;
            goto join_896;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS1878) {
    void* _M0L131RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1889 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L131RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1889)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L131RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1889)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS897
    = _M0L131RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1889;
    goto join_896;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS875);
  }
  goto joinlet_1916;
  join_896:;
  _M0L3errS898 = _M0L11_2atry__errS897;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS901
  = (struct _M0DTPC15error5Error131RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS898;
  _M0L7_2anameS902 = _M0L36_2aMoonBitTestDriverInternalSkipTestS901->$0;
  _M0L6_2acntS1908
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS901));
  if (_M0L6_2acntS1908 > 1) {
    int32_t _M0L11_2anew__cntS1909 = _M0L6_2acntS1908 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS901), _M0L11_2anew__cntS1909);
    moonbit_incref_cycle_free(_M0L7_2anameS902);
  } else if (_M0L6_2acntS1908 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS901);
  }
  _M0L4nameS900 = _M0L7_2anameS902;
  goto join_899;
  goto joinlet_1926;
  join_899:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS875(_M0L14handle__resultS875, _M0L4nameS900, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS875);
  moonbit_decref_cycle_free(_M0L4nameS900);
  joinlet_1926:;
  joinlet_1916:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS882(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS1877,
  void* _M0L3errS883
) {
  void* _M0L1eS885;
  moonbit_string_t _M0L1eS887;
  moonbit_string_t _result_1929;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS883)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS888 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS883;
      moonbit_string_t _M0L4_2aeS889 = _M0L10_2aFailureS888->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS889);
      _M0L1eS887 = _M0L4_2aeS889;
      goto join_886;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS890 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS883;
      moonbit_string_t _M0L4_2aeS891 = _M0L15_2aInspectErrorS890->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS891);
      _M0L1eS887 = _M0L4_2aeS891;
      goto join_886;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS892 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS883;
      moonbit_string_t _M0L4_2aeS893 = _M0L16_2aSnapshotErrorS892->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS893);
      _M0L1eS887 = _M0L4_2aeS893;
      goto join_886;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS894 =
        (struct _M0DTPC15error5Error129RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS883;
      moonbit_string_t _M0L4_2aeS895 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS894->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS895);
      _M0L1eS887 = _M0L4_2aeS895;
      goto join_886;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS883);
      _M0L1eS885 = _M0L3errS883;
      goto join_884;
      break;
    }
  }
  join_886:;
  return _M0L1eS887;
  join_884:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _result_1929 = _M0FP15Error10to__string(_M0L1eS885);
  moonbit_decref_cycle_free(_M0L1eS885);
  return _result_1929;
}

int32_t _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS875(
  struct _M0TWssbEu* _M0L6_2aenvS1874,
  moonbit_string_t _M0L10__testnameS876,
  moonbit_string_t _M0L7messageS877,
  int32_t _M0L7skippedS878
) {
  struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c875* _M0L14_2acasted__envS1875;
  moonbit_string_t _M0L8filenameS872;
  int32_t _M0L5indexS874;
  moonbit_string_t _M0L10file__nameS879;
  moonbit_string_t _M0L7messageS880;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS881;
  moonbit_string_t _M0L6_2atmpS1876;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1875
  = (struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c875*)_M0L6_2aenvS1874;
  _M0L8filenameS872 = _M0L14_2acasted__envS1875->$1;
  _M0L5indexS874 = _M0L14_2acasted__envS1875->$0;
  if (!_M0L7skippedS878 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS879
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS872, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS880
  = _M0MPC16string6String14escape_2einner(_M0L7messageS877, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS881
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS881, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS881, _M0L10file__nameS879);
  moonbit_decref_cycle_free(_M0L10file__nameS879);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS881, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS881, _M0L5indexS874);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS881, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS881, _M0L7messageS880);
  moonbit_decref_cycle_free(_M0L7messageS880);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS881, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1876
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS881);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS881);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1876);
  moonbit_decref_cycle_free(_M0L6_2atmpS1876);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS870(
  struct _M0TWEu* _M0L6_2aenvS1871
) {
  struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c870* _M0L14_2acasted__envS1872;
  moonbit_string_t _M0L8filenameS872;
  int32_t _M0L5indexS874;
  moonbit_string_t _M0L10file__nameS871;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS873;
  moonbit_string_t _M0L6_2atmpS1873;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1872
  = (struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2fstdp__kernel__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c870*)_M0L6_2aenvS1871;
  _M0L8filenameS872 = _M0L14_2acasted__envS1872->$1;
  _M0L5indexS874 = _M0L14_2acasted__envS1872->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS871
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS872, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS873
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS873, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS873, _M0L10file__nameS871);
  moonbit_decref_cycle_free(_M0L10file__nameS871);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS873, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS873, _M0L5indexS874);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS873, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1873
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS873);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS873);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1873);
  moonbit_decref_cycle_free(_M0L6_2atmpS1873);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S840;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS847;
  struct _M0TUsiE** _M0L6_2atmpS1870;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS854;
  moonbit_string_t* _M0L9cli__argsS855;
  moonbit_string_t _M0L6_2atmpS1869;
  moonbit_string_t _M0L6_2atmpS1868;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS856;
  int32_t _M0L7_2abindS857;
  moonbit_string_t* _M0L7_2abindS858;
  int32_t _M0L6_2acntS1910;
  int32_t _M0L2__S859;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S840 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS847 = 0;
  _M0L6_2atmpS1870 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS854
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS854)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS854->$0 = _M0L6_2atmpS1870;
  _M0L16file__and__indexS854->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS855
  = _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS855)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS1869 = (moonbit_string_t)_M0L9cli__argsS855[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS1869);
  moonbit_decref_cycle_free(_M0L9cli__argsS855);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1868
  = _M0MP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS1869);
  moonbit_decref_cycle_free(_M0L6_2atmpS1869);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS856
  = _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS847(_M0L51moonbit__test__driver__internal__split__mbt__stringS847, _M0L6_2atmpS1868, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS1868);
  _M0L7_2abindS857 = _M0L10test__argsS856->$1;
  _M0L7_2abindS858 = _M0L10test__argsS856->$0;
  _M0L6_2acntS1910
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS856));
  if (_M0L6_2acntS1910 > 1) {
    int32_t _M0L11_2anew__cntS1911 = _M0L6_2acntS1910 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS856), _M0L11_2anew__cntS1911);
    moonbit_incref_cycle_free(_M0L7_2abindS858);
  } else if (_M0L6_2acntS1910 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS856);
  }
  _M0L2__S859 = 0;
  while (1) {
    if (_M0L2__S859 < _M0L7_2abindS857) {
      moonbit_string_t _M0L3argS860 =
        (moonbit_string_t)_M0L7_2abindS858[_M0L2__S859];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS861;
      moonbit_string_t _M0L4fileS862;
      moonbit_string_t _M0L5rangeS863;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS864;
      moonbit_string_t _M0L6_2atmpS1866;
      int32_t _M0L5startS865;
      moonbit_string_t _M0L6_2atmpS1865;
      int32_t _M0L3endS866;
      int32_t _M0L1iS867;
      int32_t _M0L6_2atmpS1867;
      moonbit_incref_cycle_free(_M0L3argS860);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS861
      = _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS847(_M0L51moonbit__test__driver__internal__split__mbt__stringS847, _M0L3argS860, 58);
      moonbit_decref_cycle_free(_M0L3argS860);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS862
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS861, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS863
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS861, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS861);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS864
      = _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS847(_M0L51moonbit__test__driver__internal__split__mbt__stringS847, _M0L5rangeS863, 45);
      moonbit_decref_cycle_free(_M0L5rangeS863);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1866
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS864, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS865
      = _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S840(_M0L45moonbit__test__driver__internal__parse__int__S840, _M0L6_2atmpS1866);
      moonbit_decref_cycle_free(_M0L6_2atmpS1866);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1865
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS864, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS864);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS866
      = _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S840(_M0L45moonbit__test__driver__internal__parse__int__S840, _M0L6_2atmpS1865);
      moonbit_decref_cycle_free(_M0L6_2atmpS1865);
      _M0L1iS867 = _M0L5startS865;
      while (1) {
        if (_M0L1iS867 < _M0L3endS866) {
          struct _M0TUsiE* _M0L8_2atupleS1863;
          int32_t _M0L6_2atmpS1864;
          moonbit_incref_cycle_free(_M0L4fileS862);
          _M0L8_2atupleS1863
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS1863)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS1863->$0 = _M0L4fileS862;
          _M0L8_2atupleS1863->$1 = _M0L1iS867;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS854, _M0L8_2atupleS1863);
          _M0L6_2atmpS1864 = _M0L1iS867 + 1;
          _M0L1iS867 = _M0L6_2atmpS1864;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS862);
        }
        break;
      }
      _M0L6_2atmpS1867 = _M0L2__S859 + 1;
      _M0L2__S859 = _M0L6_2atmpS1867;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS858);
    }
    break;
  }
  return _M0L16file__and__indexS854;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS847(
  int32_t _M0L6_2aenvS1844,
  moonbit_string_t _M0L1sS848,
  int32_t _M0L3sepS849
) {
  moonbit_string_t* _M0L6_2atmpS1862;
  struct _M0TPB5ArrayGsE* _M0L3resS850;
  struct _M0TPB8MutLocalGiE* _M0L1iS851;
  struct _M0TPB8MutLocalGiE* _M0L5startS852;
  int32_t _M0L3valS1857;
  int32_t _M0L6_2atmpS1858;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1862 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS850
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS850)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS850->$0 = _M0L6_2atmpS1862;
  _M0L3resS850->$1 = 0;
  _M0L1iS851
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS851)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS851->$0 = 0;
  _M0L5startS852
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS852)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS852->$0 = 0;
  while (1) {
    int32_t _M0L3valS1845 = _M0L1iS851->$0;
    int32_t _M0L6_2atmpS1846 = Moonbit_array_length(_M0L1sS848);
    if (_M0L3valS1845 < _M0L6_2atmpS1846) {
      int32_t _M0L3valS1849 = _M0L1iS851->$0;
      int32_t _M0L6_2atmpS1848;
      int32_t _M0L6_2atmpS1847;
      int32_t _M0L3valS1856;
      int32_t _M0L6_2atmpS1855;
      if (
        _M0L3valS1849 < 0
        || _M0L3valS1849 >= Moonbit_array_length(_M0L1sS848)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1848 = _M0L1sS848[_M0L3valS1849];
      _M0L6_2atmpS1847 = _M0L6_2atmpS1848;
      if (_M0L6_2atmpS1847 == _M0L3sepS849) {
        int32_t _M0L3valS1851 = _M0L5startS852->$0;
        int32_t _M0L3valS1852 = _M0L1iS851->$0;
        moonbit_string_t _M0L6_2atmpS1850;
        int32_t _M0L3valS1854;
        int32_t _M0L6_2atmpS1853;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS1850
        = _M0MPC16string6String17unsafe__substring(_M0L1sS848, _M0L3valS1851, _M0L3valS1852);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS850, _M0L6_2atmpS1850);
        _M0L3valS1854 = _M0L1iS851->$0;
        _M0L6_2atmpS1853 = _M0L3valS1854 + 1;
        _M0L5startS852->$0 = _M0L6_2atmpS1853;
      }
      _M0L3valS1856 = _M0L1iS851->$0;
      _M0L6_2atmpS1855 = _M0L3valS1856 + 1;
      _M0L1iS851->$0 = _M0L6_2atmpS1855;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS851);
    }
    break;
  }
  _M0L3valS1857 = _M0L5startS852->$0;
  _M0L6_2atmpS1858 = Moonbit_array_length(_M0L1sS848);
  if (_M0L3valS1857 < _M0L6_2atmpS1858) {
    int32_t _M0L3valS1860 = _M0L5startS852->$0;
    int32_t _M0L6_2atmpS1861;
    moonbit_string_t _M0L6_2atmpS1859;
    moonbit_decref_cycle_free(_M0L5startS852);
    _M0L6_2atmpS1861 = Moonbit_array_length(_M0L1sS848);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS1859
    = _M0MPC16string6String17unsafe__substring(_M0L1sS848, _M0L3valS1860, _M0L6_2atmpS1861);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS850, _M0L6_2atmpS1859);
  } else {
    moonbit_decref_cycle_free(_M0L5startS852);
  }
  return _M0L3resS850;
}

int32_t _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S840(
  int32_t _M0L6_2aenvS1837,
  moonbit_string_t _M0L1sS841
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS842;
  int32_t _M0L3lenS843;
  int32_t _M0L7_2abindS844;
  int32_t _M0L1iS845;
  int32_t _result_1934;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS842
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS842)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS842->$0 = 0;
  _M0L3lenS843 = Moonbit_array_length(_M0L1sS841);
  _M0L7_2abindS844 = 0;
  _M0L1iS845 = _M0L7_2abindS844;
  while (1) {
    if (_M0L1iS845 < _M0L3lenS843) {
      int32_t _M0L3valS1842 = _M0L3resS842->$0;
      int32_t _M0L6_2atmpS1839 = _M0L3valS1842 * 10;
      int32_t _M0L6_2atmpS1841;
      int32_t _M0L6_2atmpS1840;
      int32_t _M0L6_2atmpS1838;
      int32_t _M0L6_2atmpS1843;
      if (_M0L1iS845 < 0 || _M0L1iS845 >= Moonbit_array_length(_M0L1sS841)) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1841 = _M0L1sS841[_M0L1iS845];
      _M0L6_2atmpS1840 = _M0L6_2atmpS1841 - 48;
      _M0L6_2atmpS1838 = _M0L6_2atmpS1839 + _M0L6_2atmpS1840;
      _M0L3resS842->$0 = _M0L6_2atmpS1838;
      _M0L6_2atmpS1843 = _M0L1iS845 + 1;
      _M0L1iS845 = _M0L6_2atmpS1843;
      continue;
    }
    break;
  }
  _result_1934 = _M0L3resS842->$0;
  moonbit_decref_cycle_free(_M0L3resS842);
  return _result_1934;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS839
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS839);
  return _M0L4selfS839;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S809,
  moonbit_string_t _M0L12_2adiscard__S810,
  int32_t _M0L12_2adiscard__S811,
  struct _M0TWEu* _M0L12_2adiscard__S812,
  struct _M0TWssbEu* _M0L12_2adiscard__S813,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S814
) {
  struct moonbit_result_0 _result_1935;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _result_1935.tag = 1;
  _result_1935.data.ok = 0;
  return _result_1935;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S815,
  moonbit_string_t _M0L12_2adiscard__S816,
  int32_t _M0L12_2adiscard__S817,
  struct _M0TWEu* _M0L12_2adiscard__S818,
  struct _M0TWssbEu* _M0L12_2adiscard__S819,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S820
) {
  struct moonbit_result_0 _result_1936;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _result_1936.tag = 1;
  _result_1936.data.ok = 0;
  return _result_1936;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S821,
  moonbit_string_t _M0L12_2adiscard__S822,
  int32_t _M0L12_2adiscard__S823,
  struct _M0TWEu* _M0L12_2adiscard__S824,
  struct _M0TWssbEu* _M0L12_2adiscard__S825,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S826
) {
  struct moonbit_result_0 _result_1937;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _result_1937.tag = 1;
  _result_1937.data.ok = 0;
  return _result_1937;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S827,
  moonbit_string_t _M0L12_2adiscard__S828,
  int32_t _M0L12_2adiscard__S829,
  struct _M0TWEu* _M0L12_2adiscard__S830,
  struct _M0TWssbEu* _M0L12_2adiscard__S831,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S832
) {
  struct moonbit_result_0 _result_1938;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _result_1938.tag = 1;
  _result_1938.data.ok = 0;
  return _result_1938;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S833,
  moonbit_string_t _M0L12_2adiscard__S834,
  int32_t _M0L12_2adiscard__S835,
  struct _M0TWEu* _M0L12_2adiscard__S836,
  struct _M0TWssbEu* _M0L12_2adiscard__S837,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S838
) {
  struct moonbit_result_0 _result_1939;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _result_1939.tag = 1;
  _result_1939.data.ok = 0;
  return _result_1939;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S808
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt26stdp__kernel__plot_2einner(
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS771,
  float _M0L6t__maxS769,
  int32_t _M0L5widthS763,
  int32_t _M0L6heightS775
) {
  int32_t _M0L7n__colsS762;
  struct _M0TPB8MutLocalGfE* _M0L2mnS764;
  struct _M0TPB8MutLocalGfE* _M0L2mxS765;
  float* _M0L6_2atmpS1836;
  struct _M0TPB5ArrayGfE* _M0L4valsS766;
  struct _M0TPB8MutLocalGiE* _M0L1iS767;
  float _M0L3valS1792;
  float _M0L3valS1793;
  float _M0L6_2atmpS1791;
  float _M0L3valS1834;
  float _M0L3valS1835;
  float _M0L6vrangeS773;
  struct _M0TPB5ArrayGsE* _M0L6canvasS774;
  moonbit_string_t _M0L3padS776;
  moonbit_string_t _M0L9row__initS777;
  int32_t _M0L7_2abindS778;
  int32_t _M0L1rS779;
  struct _M0TPB8MutLocalGiE* _M0L1cS781;
  float _M0L3valS1833;
  moonbit_string_t _M0L10max__labelS790;
  float _M0L3valS1831;
  float _M0L3valS1832;
  float _M0L6_2atmpS1830;
  float _M0L6_2atmpS1829;
  moonbit_string_t _M0L10mid__labelS791;
  float _M0L3valS1828;
  moonbit_string_t _M0L10min__labelS792;
  int32_t _M0L1aS794;
  int32_t _M0L1bS795;
  int32_t _M0L1cS796;
  int32_t _M0L1mS797;
  int32_t _M0L12label__widthS793;
  int32_t _M0L7_2abindS798;
  int32_t _M0L1rS799;
  int32_t _M0L6_2atmpS1827;
  moonbit_string_t _M0L8pad__strS804;
  moonbit_string_t _M0L6_2atmpS1825;
  moonbit_string_t _M0L6_2atmpS1826;
  moonbit_string_t _M0L6_2atmpS1824;
  moonbit_string_t _M0L6_2atmpS1822;
  moonbit_string_t _M0L6_2atmpS1823;
  moonbit_string_t _M0L6_2atmpS1821;
  moonbit_string_t _M0L6_2atmpS1820;
  #line 276 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  if (_M0L5widthS763 > 1) {
    _M0L7n__colsS762 = _M0L5widthS763;
  } else {
    _M0L7n__colsS762 = 1;
  }
  _M0L2mnS764
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L2mnS764)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2mnS764->$0 = 0x0p+0f;
  _M0L2mxS765
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L2mxS765)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2mxS765->$0 = 0x0p+0f;
  _M0L6_2atmpS1836 = moonbit_empty_float_array;
  _M0L4valsS766
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L4valsS766)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L4valsS766->$0 = _M0L6_2atmpS1836;
  _M0L4valsS766->$1 = 0;
  _M0L1iS767
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS767)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS767->$0 = 0;
  while (1) {
    int32_t _M0L3valS1773 = _M0L1iS767->$0;
    if (_M0L3valS1773 < _M0L7n__colsS762) {
      float _M0L6_2atmpS1783 = -_M0L6t__maxS769;
      int32_t _M0L3valS1790 = _M0L1iS767->$0;
      float _M0L6_2atmpS1788 = (float)_M0L3valS1790;
      float _M0L6_2atmpS1789 = 0x1p+1f * _M0L6t__maxS769;
      float _M0L6_2atmpS1785 = _M0L6_2atmpS1788 * _M0L6_2atmpS1789;
      int32_t _M0L6_2atmpS1787 = _M0L7n__colsS762 - 1;
      float _M0L6_2atmpS1786 = (float)_M0L6_2atmpS1787;
      float _M0L6_2atmpS1784 = _M0L6_2atmpS1785 / _M0L6_2atmpS1786;
      float _M0L2dtS768 = _M0L6_2atmpS1783 + _M0L6_2atmpS1784;
      float _M0L8tau__preS1779 = _M0L5paramS771->$2;
      float _M0L9tau__postS1780 = _M0L5paramS771->$3;
      float _M0L6a__preS1781 = _M0L5paramS771->$0;
      float _M0L7a__postS1782 = _M0L5paramS771->$1;
      float _M0L1wS770;
      int32_t _M0L3valS1774;
      int32_t _M0L3valS1778;
      int32_t _M0L6_2atmpS1777;
      #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L1wS770
      = _M0FP26RiantR8snn__mbt16gerstner__kernel(_M0L2dtS768, _M0L8tau__preS1779, _M0L9tau__postS1780, _M0L6a__preS1781, _M0L7a__postS1782);
      #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array4pushGfE(_M0L4valsS766, _M0L1wS770);
      _M0L3valS1774 = _M0L1iS767->$0;
      if (_M0L3valS1774 == 0) {
        _M0L2mnS764->$0 = _M0L1wS770;
        _M0L2mxS765->$0 = _M0L1wS770;
      } else {
        float _M0L3valS1775 = _M0L2mnS764->$0;
        float _M0L3valS1776;
        if (_M0L1wS770 < _M0L3valS1775) {
          _M0L2mnS764->$0 = _M0L1wS770;
        }
        _M0L3valS1776 = _M0L2mxS765->$0;
        if (_M0L1wS770 > _M0L3valS1776) {
          _M0L2mxS765->$0 = _M0L1wS770;
        }
      }
      _M0L3valS1778 = _M0L1iS767->$0;
      _M0L6_2atmpS1777 = _M0L3valS1778 + 1;
      _M0L1iS767->$0 = _M0L6_2atmpS1777;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS767);
    }
    break;
  }
  _M0L3valS1792 = _M0L2mxS765->$0;
  _M0L3valS1793 = _M0L2mnS764->$0;
  _M0L6_2atmpS1791 = _M0L3valS1792 - _M0L3valS1793;
  if (_M0L6_2atmpS1791 < 0x1.12e0be826d695p-30f) {
    float _M0L3valS1795 = _M0L2mnS764->$0;
    float _M0L6_2atmpS1794 = _M0L3valS1795 + 0x1.12e0be826d695p-30f;
    _M0L2mxS765->$0 = _M0L6_2atmpS1794;
  }
  _M0L3valS1834 = _M0L2mxS765->$0;
  _M0L3valS1835 = _M0L2mnS764->$0;
  _M0L6vrangeS773 = _M0L3valS1834 - _M0L3valS1835;
  #line 311 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6canvasS774
  = _M0MPC15array5Array4makeGsE(_M0L6heightS775, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L3padS776 = _M0MPC16string6String4make(_M0L7n__colsS762, 32);
  _M0L9row__initS777 = (moonbit_string_t)moonbit_string_literal_9.data;
  _M0L7_2abindS778 = 0;
  _M0L1rS779 = _M0L7_2abindS778;
  while (1) {
    if (_M0L1rS779 < _M0L6heightS775) {
      moonbit_string_t _M0L6_2atmpS1796;
      int32_t _M0L6_2atmpS1797;
      #line 315 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS1796 = moonbit_add_string(_M0L9row__initS777, _M0L3padS776);
      #line 315 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGsE(_M0L6canvasS774, _M0L1rS779, _M0L6_2atmpS1796);
      _M0L6_2atmpS1797 = _M0L1rS779 + 1;
      _M0L1rS779 = _M0L6_2atmpS1797;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L3padS776);
    }
    break;
  }
  _M0L1cS781
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1cS781)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1cS781->$0 = 0;
  while (1) {
    int32_t _M0L3valS1798 = _M0L1cS781->$0;
    if (_M0L3valS1798 < _M0L7n__colsS762) {
      int32_t _M0L3valS1810 = _M0L1cS781->$0;
      float _M0L1wS782;
      float _M0L3valS1809;
      float _M0L6_2atmpS1808;
      float _M0L10normalizedS783;
      float _M0L6_2atmpS1805;
      float _M0L6_2atmpS1807;
      float _M0L6_2atmpS1806;
      float _M0L6_2atmpS1804;
      int32_t _M0L14row__from__topS784;
      int32_t _M0L1rS785;
      int32_t _M0L2chS786;
      moonbit_string_t _M0L8row__strS787;
      int32_t _M0L3valS1802;
      int32_t _M0L6_2atmpS1803;
      int32_t _M0L6_2atmpS1801;
      moonbit_string_t _M0L8new__rowS788;
      int32_t _M0L3valS1800;
      int32_t _M0L6_2atmpS1799;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L1wS782 = _M0MPC15array5Array2atGfE(_M0L4valsS766, _M0L3valS1810);
      _M0L3valS1809 = _M0L2mnS764->$0;
      _M0L6_2atmpS1808 = _M0L1wS782 - _M0L3valS1809;
      _M0L10normalizedS783 = _M0L6_2atmpS1808 / _M0L6vrangeS773;
      _M0L6_2atmpS1805 = (float)_M0L6heightS775;
      _M0L6_2atmpS1807 = (float)1;
      _M0L6_2atmpS1806 = _M0L6_2atmpS1807 * _M0L10normalizedS783;
      _M0L6_2atmpS1804 = _M0L6_2atmpS1805 - _M0L6_2atmpS1806;
      #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L14row__from__topS784
      = _M0MPC15float5Float7to__int(_M0L6_2atmpS1804);
      if (_M0L14row__from__topS784 < 0) {
        _M0L1rS785 = 0;
      } else if (_M0L14row__from__topS784 >= _M0L6heightS775) {
        _M0L1rS785 = _M0L6heightS775 - 1;
      } else {
        _M0L1rS785 = _M0L14row__from__topS784;
      }
      if (_M0L1wS782 >= 0x0p+0f) {
        _M0L2chS786 = 42;
      } else {
        _M0L2chS786 = 46;
      }
      #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8row__strS787
      = _M0MPC15array5Array2atGsE(_M0L6canvasS774, _M0L1rS785);
      _M0L3valS1802 = _M0L1cS781->$0;
      _M0L6_2atmpS1803 = Moonbit_array_length(_M0L9row__initS777);
      _M0L6_2atmpS1801 = _M0L3valS1802 + _M0L6_2atmpS1803;
      #line 332 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8new__rowS788
      = _M0FP26RiantR8snn__mbt19row__int__set__char(_M0L8row__strS787, _M0L6_2atmpS1801, _M0L2chS786);
      moonbit_decref_cycle_free(_M0L8row__strS787);
      #line 333 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGsE(_M0L6canvasS774, _M0L1rS785, _M0L8new__rowS788);
      _M0L3valS1800 = _M0L1cS781->$0;
      _M0L6_2atmpS1799 = _M0L3valS1800 + 1;
      _M0L1cS781->$0 = _M0L6_2atmpS1799;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1cS781);
      moonbit_decref_cycle_free(_M0L9row__initS777);
      moonbit_decref_cycle_free(_M0L4valsS766);
    }
    break;
  }
  #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_10.data);
  _M0L3valS1833 = _M0L2mxS765->$0;
  #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10max__labelS790
  = _M0FP26RiantR8snn__mbt27format__axis__label__kernel(_M0L3valS1833);
  _M0L3valS1831 = _M0L2mxS765->$0;
  moonbit_decref_cycle_free(_M0L2mxS765);
  _M0L3valS1832 = _M0L2mnS764->$0;
  _M0L6_2atmpS1830 = _M0L3valS1831 + _M0L3valS1832;
  _M0L6_2atmpS1829 = _M0L6_2atmpS1830 / 0x1p+1f;
  #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10mid__labelS791
  = _M0FP26RiantR8snn__mbt27format__axis__label__kernel(_M0L6_2atmpS1829);
  _M0L3valS1828 = _M0L2mnS764->$0;
  moonbit_decref_cycle_free(_M0L2mnS764);
  #line 340 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10min__labelS792
  = _M0FP26RiantR8snn__mbt27format__axis__label__kernel(_M0L3valS1828);
  _M0L1aS794 = Moonbit_array_length(_M0L10max__labelS790);
  _M0L1bS795 = Moonbit_array_length(_M0L10mid__labelS791);
  _M0L1cS796 = Moonbit_array_length(_M0L10min__labelS792);
  if (_M0L1aS794 > _M0L1bS795) {
    _M0L1mS797 = _M0L1aS794;
  } else {
    _M0L1mS797 = _M0L1bS795;
  }
  if (_M0L1mS797 > _M0L1cS796) {
    _M0L12label__widthS793 = _M0L1mS797;
  } else {
    _M0L12label__widthS793 = _M0L1cS796;
  }
  _M0L7_2abindS798 = 0;
  _M0L1rS799 = _M0L7_2abindS798;
  while (1) {
    if (_M0L1rS799 < _M0L6heightS775) {
      moonbit_string_t _M0L5labelS800;
      int32_t _M0L6_2atmpS1815;
      int32_t _M0L10pad__countS801;
      moonbit_string_t _M0L6paddedS802;
      moonbit_string_t _M0L6_2atmpS1814;
      moonbit_string_t _M0L6_2atmpS1812;
      moonbit_string_t _M0L6_2atmpS1813;
      moonbit_string_t _M0L6_2atmpS1811;
      int32_t _M0L6_2atmpS1819;
      if (_M0L1rS799 == 0) {
        moonbit_incref_cycle_free(_M0L10max__labelS790);
        _M0L5labelS800 = _M0L10max__labelS790;
      } else {
        int32_t _M0L6_2atmpS1817 = _M0L6heightS775 - 1;
        if (_M0L1rS799 == _M0L6_2atmpS1817) {
          moonbit_incref_cycle_free(_M0L10min__labelS792);
          _M0L5labelS800 = _M0L10min__labelS792;
        } else {
          int32_t _M0L6_2atmpS1818 = _M0L6heightS775 / 2;
          if (_M0L1rS799 == _M0L6_2atmpS1818) {
            moonbit_incref_cycle_free(_M0L10mid__labelS791);
            _M0L5labelS800 = _M0L10mid__labelS791;
          } else {
            _M0L5labelS800 = (moonbit_string_t)moonbit_string_literal_0.data;
          }
        }
      }
      _M0L6_2atmpS1815 = Moonbit_array_length(_M0L5labelS800);
      if (_M0L12label__widthS793 > _M0L6_2atmpS1815) {
        int32_t _M0L6_2atmpS1816 = Moonbit_array_length(_M0L5labelS800);
        _M0L10pad__countS801 = _M0L12label__widthS793 - _M0L6_2atmpS1816;
      } else {
        _M0L10pad__countS801 = 0;
      }
      #line 363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6paddedS802 = _M0MPC16string6String4make(_M0L10pad__countS801, 32);
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS1814 = moonbit_add_string(_M0L6paddedS802, _M0L5labelS800);
      moonbit_decref_cycle_free(_M0L5labelS800);
      moonbit_decref_cycle_free(_M0L6paddedS802);
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS1812
      = moonbit_add_string(_M0L6_2atmpS1814, (moonbit_string_t)moonbit_string_literal_11.data);
      moonbit_decref_cycle_free(_M0L6_2atmpS1814);
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS1813
      = _M0MPC15array5Array2atGsE(_M0L6canvasS774, _M0L1rS799);
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS1811
      = moonbit_add_string(_M0L6_2atmpS1812, _M0L6_2atmpS1813);
      moonbit_decref_cycle_free(_M0L6_2atmpS1813);
      moonbit_decref_cycle_free(_M0L6_2atmpS1812);
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0FPB7printlnGsE(_M0L6_2atmpS1811);
      moonbit_decref_cycle_free(_M0L6_2atmpS1811);
      _M0L6_2atmpS1819 = _M0L1rS799 + 1;
      _M0L1rS799 = _M0L6_2atmpS1819;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L10min__labelS792);
      moonbit_decref_cycle_free(_M0L10mid__labelS791);
      moonbit_decref_cycle_free(_M0L10max__labelS790);
      moonbit_decref_cycle_free(_M0L6canvasS774);
    }
    break;
  }
  _M0L6_2atmpS1827 = _M0L12label__widthS793 + 3;
  #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L8pad__strS804 = _M0MPC16string6String4make(_M0L6_2atmpS1827, 32);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS1825
  = moonbit_add_string(_M0L8pad__strS804, (moonbit_string_t)moonbit_string_literal_12.data);
  moonbit_decref_cycle_free(_M0L8pad__strS804);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS1826 = _M0IPC15float5FloatPB4Show10to__string(_M0L6t__maxS769);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS1824 = moonbit_add_string(_M0L6_2atmpS1825, _M0L6_2atmpS1826);
  moonbit_decref_cycle_free(_M0L6_2atmpS1826);
  moonbit_decref_cycle_free(_M0L6_2atmpS1825);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS1822
  = moonbit_add_string(_M0L6_2atmpS1824, (moonbit_string_t)moonbit_string_literal_13.data);
  moonbit_decref_cycle_free(_M0L6_2atmpS1824);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS1823 = _M0IPC15float5FloatPB4Show10to__string(_M0L6t__maxS769);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS1821 = moonbit_add_string(_M0L6_2atmpS1822, _M0L6_2atmpS1823);
  moonbit_decref_cycle_free(_M0L6_2atmpS1823);
  moonbit_decref_cycle_free(_M0L6_2atmpS1822);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS1820
  = moonbit_add_string(_M0L6_2atmpS1821, (moonbit_string_t)moonbit_string_literal_14.data);
  moonbit_decref_cycle_free(_M0L6_2atmpS1821);
  #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1820);
  moonbit_decref_cycle_free(_M0L6_2atmpS1820);
  return 0;
}

moonbit_string_t _M0FP26RiantR8snn__mbt27format__axis__label__kernel(
  float _M0L1vS759
) {
  float _M0L6scaledS758;
  float _M0L6_2atmpS1772;
  int32_t _M0L7roundedS760;
  float _M0L6_2atmpS1771;
  float _M0L12scaled__backS761;
  #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6scaledS758 = _M0L1vS759 * 0x1.388p+13f;
  #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS1772 = _M0MPC15float5Float5round(_M0L6scaledS758);
  #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7roundedS760 = _M0MPC15float5Float7to__int(_M0L6_2atmpS1772);
  _M0L6_2atmpS1771 = (float)_M0L7roundedS760;
  _M0L12scaled__backS761 = _M0L6_2atmpS1771 / 0x1.388p+13f;
  #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  return _M0IPC15float5FloatPB4Show10to__string(_M0L12scaled__backS761);
}

float _M0FP26RiantR8snn__mbt16gerstner__kernel(
  float _M0L2dtS751,
  float _M0L8tau__preS753,
  float _M0L9tau__postS756,
  float _M0L6a__preS754,
  float _M0L7a__postS757
) {
  #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  if (_M0L2dtS751 > 0x0p+0f) {
    float _M0L6_2atmpS1768 = -_M0L2dtS751;
    float _M0L3argS752 = _M0L6_2atmpS1768 / _M0L8tau__preS753;
    float _M0L6_2atmpS1767;
    #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS1767 = _M0FP26RiantR8snn__mbt4expf(_M0L3argS752);
    return _M0L6a__preS754 * _M0L6_2atmpS1767;
  } else if (_M0L2dtS751 < 0x0p+0f) {
    float _M0L3argS755 = _M0L2dtS751 / _M0L9tau__postS756;
    float _M0L6_2atmpS1769 = -_M0L7a__postS757;
    float _M0L6_2atmpS1770;
    #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS1770 = _M0FP26RiantR8snn__mbt4expf(_M0L3argS755);
    return _M0L6_2atmpS1769 * _M0L6_2atmpS1770;
  } else {
    return 0x0p+0f;
  }
}

struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0MP26RiantR8snn__mbt12STDPGerstner3new(
  
) {
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _block_1944;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _block_1944
  = (struct _M0TP26RiantR8snn__mbt12STDPGerstner*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt12STDPGerstner));
  Moonbit_object_header(_block_1944)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1944->$0 = 0x1.47ae147ae147bp-7f;
  _block_1944->$1 = 0x1.47ae147ae147bp-7f;
  _block_1944->$2 = 0x1.4p+4f;
  _block_1944->$3 = 0x1.4p+4f;
  _block_1944->$4 = 0x1.ep+4f;
  _block_1944->$5 = 0x0p+0f;
  return _block_1944;
}

moonbit_string_t _M0FP26RiantR8snn__mbt19row__int__set__char(
  moonbit_string_t _M0L1sS741,
  int32_t _M0L3posS744,
  int32_t _M0L2chS748
) {
  int32_t _M0L6_2atmpS1766;
  struct _M0TPB13StringBuilder* _M0L2sbS740;
  int32_t _M0L3lenS742;
  int32_t _M0L11prefix__endS743;
  int32_t _M0L6_2atmpS1758;
  moonbit_string_t _result_1947;
  #line 353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L6_2atmpS1766 = Moonbit_array_length(_M0L1sS741);
  #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L2sbS740
  = _M0MPB13StringBuilder21StringBuilder_2einner(_M0L6_2atmpS1766);
  _M0L3lenS742 = Moonbit_array_length(_M0L1sS741);
  if (_M0L3posS744 < _M0L3lenS742) {
    _M0L11prefix__endS743 = _M0L3posS744;
  } else {
    _M0L11prefix__endS743 = _M0L3lenS742;
  }
  if (_M0L11prefix__endS743 > 0) {
    moonbit_string_t _M0L6prefixS745;
    struct _M0TPB8MutLocalGiE* _M0L1kS746;
    #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
    _M0L6prefixS745 = _M0MPC16string6String4make(_M0L11prefix__endS743, 32);
    _M0L1kS746
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS746)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS746->$0 = 0;
    while (1) {
      int32_t _M0L3valS1752 = _M0L1kS746->$0;
      if (_M0L3valS1752 < _M0L11prefix__endS743) {
        int32_t _M0L3valS1755 = _M0L1kS746->$0;
        int32_t _M0L6_2atmpS1754;
        int32_t _M0L6_2atmpS1753;
        int32_t _M0L3valS1757;
        int32_t _M0L6_2atmpS1756;
        if (
          _M0L3valS1755 < 0
          || _M0L3valS1755 >= Moonbit_array_length(_M0L1sS741)
        ) {
          #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1754 = _M0L1sS741[_M0L3valS1755];
        #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
        _M0L6_2atmpS1753
        = _M0MPC16uint166UInt1616unsafe__to__char(_M0L6_2atmpS1754);
        #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
        _M0IPB13StringBuilderPB6Logger11write__char(_M0L2sbS740, _M0L6_2atmpS1753);
        _M0L3valS1757 = _M0L1kS746->$0;
        _M0L6_2atmpS1756 = _M0L3valS1757 + 1;
        _M0L1kS746->$0 = _M0L6_2atmpS1756;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS746);
      }
      break;
    }
    moonbit_decref_cycle_free(_M0L6prefixS745);
  }
  #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L2sbS740, _M0L2chS748);
  _M0L6_2atmpS1758 = _M0L3posS744 + 1;
  if (_M0L6_2atmpS1758 < _M0L3lenS742) {
    int32_t _M0L6_2atmpS1765 = _M0L3posS744 + 1;
    struct _M0TPB8MutLocalGiE* _M0L1kS749 =
      (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS749)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS749->$0 = _M0L6_2atmpS1765;
    while (1) {
      int32_t _M0L3valS1759 = _M0L1kS749->$0;
      if (_M0L3valS1759 < _M0L3lenS742) {
        int32_t _M0L3valS1762 = _M0L1kS749->$0;
        int32_t _M0L6_2atmpS1761;
        int32_t _M0L6_2atmpS1760;
        int32_t _M0L3valS1764;
        int32_t _M0L6_2atmpS1763;
        if (
          _M0L3valS1762 < 0
          || _M0L3valS1762 >= Moonbit_array_length(_M0L1sS741)
        ) {
          #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1761 = _M0L1sS741[_M0L3valS1762];
        #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
        _M0L6_2atmpS1760
        = _M0MPC16uint166UInt1616unsafe__to__char(_M0L6_2atmpS1761);
        #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
        _M0IPB13StringBuilderPB6Logger11write__char(_M0L2sbS740, _M0L6_2atmpS1760);
        _M0L3valS1764 = _M0L1kS749->$0;
        _M0L6_2atmpS1763 = _M0L3valS1764 + 1;
        _M0L1kS749->$0 = _M0L6_2atmpS1763;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS749);
      }
      break;
    }
  }
  #line 380 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _result_1947 = _M0MPB13StringBuilder10to__string(_M0L2sbS740);
  moonbit_decref_cycle_free(_M0L2sbS740);
  return _result_1947;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS739) {
  double _M0L6_2atmpS1751;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1751 = (double)_M0L4selfS739;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1751);
}

float _M0MPC15float5Float5round(float _M0L4selfS738) {
  float _M0L6_2atmpS1750;
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\float\\round.mbt"
  _M0L6_2atmpS1750 = _M0L4selfS738 + 0x1p-1f;
  #line 145 "C:\\Users\\31379\\.moon\\lib\\core\\float\\round.mbt"
  return _M0MPC15float5Float5floor(_M0L6_2atmpS1750);
}

float _M0MPC15float5Float5floor(float _M0L4selfS737) {
  float _M0L7truncedS736;
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\float\\round.mbt"
  #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\float\\round.mbt"
  _M0L7truncedS736 = _M0MPC15float5Float5trunc(_M0L4selfS737);
  if (_M0L4selfS737 < _M0L7truncedS736) {
    return _M0L7truncedS736 - 0x1p+0f;
  } else {
    return _M0L7truncedS736;
  }
}

float _M0MPC15float5Float5trunc(float _M0L4selfS732) {
  uint32_t _M0L3u32S731;
  uint32_t _M0L6_2atmpS1749;
  uint32_t _M0L6_2atmpS1748;
  int32_t _M0L11biased__expS733;
  int32_t _M0L6_2atmpS1747;
  int32_t _M0L11mask__shiftS734;
  uint32_t _tmp_1948;
  int32_t _M0L6_2atmpS1746;
  int32_t _M0L6_2atmpS1745;
  uint32_t _M0L11trunc__maskS735;
  uint32_t _M0L6_2atmpS1744;
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\float\\round.mbt"
  _M0L3u32S731 = *(int32_t*)&_M0L4selfS732;
  _M0L6_2atmpS1749 = _M0L3u32S731 >> 23;
  _M0L6_2atmpS1748 = _M0L6_2atmpS1749 & 255u;
  _M0L11biased__expS733 = *(int32_t*)&_M0L6_2atmpS1748;
  if (_M0L11biased__expS733 < 127) {
    uint32_t _M0L6_2atmpS1743 = _M0L3u32S731 & 2147483648u;
    return *(float*)&_M0L6_2atmpS1743;
  } else if (_M0L11biased__expS733 >= 150) {
    return _M0L4selfS732;
  }
  _M0L6_2atmpS1747 = _M0L11biased__expS733 - 127;
  _M0L11mask__shiftS734 = _M0L6_2atmpS1747 + 8;
  _tmp_1948 = 2147483648u;
  _M0L6_2atmpS1746 = *(int32_t*)&_tmp_1948;
  _M0L6_2atmpS1745 = _M0L6_2atmpS1746 >> (_M0L11mask__shiftS734 & 31);
  _M0L11trunc__maskS735 = *(uint32_t*)&_M0L6_2atmpS1745;
  _M0L6_2atmpS1744 = _M0L3u32S731 & _M0L11trunc__maskS735;
  return *(float*)&_M0L6_2atmpS1744;
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS730) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS730 != _M0L4selfS730) {
    return 0;
  } else if (_M0L4selfS730 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS730 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS730;
  }
}

struct _M0TPB5ArrayGsE* _M0MPC15array5Array4makeGsE(
  int32_t _M0L3lenS726,
  moonbit_string_t _M0L4elemS728
) {
  struct _M0TPB5ArrayGsE* _M0L3arrS725;
  int32_t _M0L1iS727;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS725 = _M0MPC15array5Array20unsafe__make__uninitGsE(_M0L3lenS726);
  _M0L1iS727 = 0;
  while (1) {
    if (_M0L1iS727 < _M0L3lenS726) {
      moonbit_string_t* _M0L3bufS1741 = _M0L3arrS725->$0;
      moonbit_string_t _M0L6_2aoldS1891 =
        (moonbit_string_t)_M0L3bufS1741[_M0L1iS727];
      int32_t _M0L6_2atmpS1742;
      moonbit_incref_cycle_free(_M0L4elemS728);
      moonbit_decref_cycle_free(_M0L6_2aoldS1891);
      _M0L3bufS1741[_M0L1iS727] = _M0L4elemS728;
      _M0L6_2atmpS1742 = _M0L1iS727 + 1;
      _M0L1iS727 = _M0L6_2atmpS1742;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4elemS728);
    }
    break;
  }
  return _M0L3arrS725;
}

int32_t _M0MPC15array5Array3setGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS722,
  int32_t _M0L5indexS723,
  moonbit_string_t _M0L5valueS724
) {
  int32_t _M0L3lenS721;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS721 = _M0L4selfS722->$1;
  if (_M0L5indexS723 >= 0 && _M0L5indexS723 < _M0L3lenS721) {
    moonbit_string_t* _M0L6_2atmpS1740;
    moonbit_string_t _M0L6_2aoldS1892;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1740 = _M0MPC15array5Array6bufferGsE(_M0L4selfS722);
    _M0L6_2aoldS1892 = (moonbit_string_t)_M0L6_2atmpS1740[_M0L5indexS723];
    moonbit_decref_cycle_free(_M0L6_2aoldS1892);
    _M0L6_2atmpS1740[_M0L5indexS723] = _M0L5valueS724;
    moonbit_decref_cycle_free(_M0L6_2atmpS1740);
  } else {
    moonbit_decref_cycle_free(_M0L5valueS724);
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS716,
  int32_t _M0L5indexS717
) {
  int32_t _M0L3lenS715;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS715 = _M0L4selfS716->$1;
  if (_M0L5indexS717 >= 0 && _M0L5indexS717 < _M0L3lenS715) {
    float* _M0L6_2atmpS1738;
    float _result_1950;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1738 = _M0MPC15array5Array6bufferGfE(_M0L4selfS716);
    _result_1950 = (float)_M0L6_2atmpS1738[_M0L5indexS717];
    moonbit_decref_cycle_free(_M0L6_2atmpS1738);
    return _result_1950;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS719,
  int32_t _M0L5indexS720
) {
  int32_t _M0L3lenS718;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS718 = _M0L4selfS719->$1;
  if (_M0L5indexS720 >= 0 && _M0L5indexS720 < _M0L3lenS718) {
    moonbit_string_t* _M0L6_2atmpS1739;
    moonbit_string_t _M0L6_2atmpS1893;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1739 = _M0MPC15array5Array6bufferGsE(_M0L4selfS719);
    _M0L6_2atmpS1893 = (moonbit_string_t)_M0L6_2atmpS1739[_M0L5indexS720];
    moonbit_incref_cycle_free(_M0L6_2atmpS1893);
    moonbit_decref_cycle_free(_M0L6_2atmpS1739);
    return _M0L6_2atmpS1893;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS714) {
  moonbit_string_t _M0L6_2atmpS1737;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1737 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS714);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1737);
  moonbit_decref_cycle_free(_M0L6_2atmpS1737);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS713) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS713);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS698) {
  uint64_t _M0L4bitsS701;
  uint64_t _M0L6_2atmpS1736;
  uint64_t _M0L6_2atmpS1735;
  int32_t _M0L8ieeeSignS702;
  uint64_t _M0L12ieeeMantissaS703;
  uint64_t _M0L6_2atmpS1734;
  uint64_t _M0L6_2atmpS1733;
  int32_t _M0L12ieeeExponentS704;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS705;
  struct _M0TPB17FloatingDecimal64* _M0L1vS706;
  moonbit_string_t _result_1952;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS698 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_15.data;
  }
  if (_M0L3valS698 >= -0x1p+53 && _M0L3valS698 <= 0x1p+53) {
    if (_M0L3valS698 >= -0x1p+31 && _M0L3valS698 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS699;
      double _M0L6_2atmpS1722;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS699 = _M0MPC16double6Double7to__int(_M0L3valS698);
      _M0L6_2atmpS1722 = (double)_M0L1iS699;
      if (_M0L6_2atmpS1722 == _M0L3valS698) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS699, 10);
      }
    } else {
      int64_t _M0L1iS700;
      double _M0L6_2atmpS1723;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS700 = _M0MPC16double6Double9to__int64(_M0L3valS698);
      _M0L6_2atmpS1723 = (double)_M0L1iS700;
      if (_M0L6_2atmpS1723 == _M0L3valS698) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS700, 10);
      }
    }
  }
  _M0L4bitsS701 = *(int64_t*)&_M0L3valS698;
  _M0L6_2atmpS1736 = _M0L4bitsS701 >> 63;
  _M0L6_2atmpS1735 = _M0L6_2atmpS1736 & 1ull;
  _M0L8ieeeSignS702 = _M0L6_2atmpS1735 != 0ull;
  _M0L12ieeeMantissaS703 = _M0L4bitsS701 & 4503599627370495ull;
  _M0L6_2atmpS1734 = _M0L4bitsS701 >> 52;
  _M0L6_2atmpS1733 = _M0L6_2atmpS1734 & 2047ull;
  _M0L12ieeeExponentS704 = (int32_t)_M0L6_2atmpS1733;
  if (
    _M0L12ieeeExponentS704 == 2047
    || _M0L12ieeeExponentS704 == 0 && _M0L12ieeeMantissaS703 == 0ull
  ) {
    int32_t _M0L6_2atmpS1724 = _M0L12ieeeExponentS704 != 0;
    int32_t _M0L6_2atmpS1725 = _M0L12ieeeMantissaS703 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS702, _M0L6_2atmpS1724, _M0L6_2atmpS1725);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS705
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS703, _M0L12ieeeExponentS704);
  if (_M0L7_2abindS705 == 0) {
    uint32_t _M0L6_2atmpS1726;
    if (_M0L7_2abindS705) {
      moonbit_decref_cycle_free(_M0L7_2abindS705);
    }
    _M0L6_2atmpS1726 = *(uint32_t*)&_M0L12ieeeExponentS704;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS706 = _M0FPB3d2d(_M0L12ieeeMantissaS703, _M0L6_2atmpS1726);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS707 = _M0L7_2abindS705;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS708 = _M0L7_2aSomeS707;
    struct _M0TPB17FloatingDecimal64* _M0L1xS709 = _M0L4_2afS708;
    while (1) {
      uint64_t _M0L8mantissaS1732 = _M0L1xS709->$0;
      uint64_t _M0L1qS710 = _M0L8mantissaS1732 / 10ull;
      uint64_t _M0L8mantissaS1730 = _M0L1xS709->$0;
      uint64_t _M0L6_2atmpS1731 = 10ull * _M0L1qS710;
      uint64_t _M0L1rS711 = _M0L8mantissaS1730 - _M0L6_2atmpS1731;
      int32_t _M0L8exponentS1729;
      int32_t _M0L6_2atmpS1728;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1727;
      if (_M0L1rS711 != 0ull) {
        _M0L1vS706 = _M0L1xS709;
        break;
      }
      _M0L8exponentS1729 = _M0L1xS709->$1;
      moonbit_decref_cycle_free(_M0L1xS709);
      _M0L6_2atmpS1728 = _M0L8exponentS1729 + 1;
      _M0L6_2atmpS1727
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1727)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1727->$0 = _M0L1qS710;
      _M0L6_2atmpS1727->$1 = _M0L6_2atmpS1728;
      _M0L1xS709 = _M0L6_2atmpS1727;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1952 = _M0FPB9to__chars(_M0L1vS706, _M0L8ieeeSignS702);
  moonbit_decref_cycle_free(_M0L1vS706);
  return _result_1952;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS693,
  int32_t _M0L12ieeeExponentS695
) {
  uint64_t _M0L2m2S692;
  int32_t _M0L6_2atmpS1721;
  int32_t _M0L2e2S694;
  int32_t _M0L6_2atmpS1720;
  uint64_t _M0L6_2atmpS1719;
  uint64_t _M0L4maskS696;
  uint64_t _M0L8fractionS697;
  int32_t _M0L6_2atmpS1718;
  uint64_t _M0L6_2atmpS1717;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1716;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S692 = 4503599627370496ull | _M0L12ieeeMantissaS693;
  _M0L6_2atmpS1721 = _M0L12ieeeExponentS695 - 1023;
  _M0L2e2S694 = _M0L6_2atmpS1721 - 52;
  if (_M0L2e2S694 > 0) {
    return 0;
  }
  if (_M0L2e2S694 < -52) {
    return 0;
  }
  _M0L6_2atmpS1720 = -_M0L2e2S694;
  _M0L6_2atmpS1719 = 1ull << (_M0L6_2atmpS1720 & 63);
  _M0L4maskS696 = _M0L6_2atmpS1719 - 1ull;
  _M0L8fractionS697 = _M0L2m2S692 & _M0L4maskS696;
  if (_M0L8fractionS697 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1718 = -_M0L2e2S694;
  _M0L6_2atmpS1717 = _M0L2m2S692 >> (_M0L6_2atmpS1718 & 63);
  _M0L6_2atmpS1716
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1716)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1716->$0 = _M0L6_2atmpS1717;
  _M0L6_2atmpS1716->$1 = 0;
  return _M0L6_2atmpS1716;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS660,
  int32_t _M0L4signS658
) {
  moonbit_bytes_t _M0L6resultS656;
  int32_t _M0Lm5indexS657;
  uint64_t _M0L6outputS659;
  int32_t _M0L7olengthS661;
  int32_t _M0L8exponentS1715;
  int32_t _M0L6_2atmpS1714;
  int32_t _M0Lm3expS662;
  int32_t _M0L6_2atmpS1713;
  int32_t _M0L6_2atmpS1711;
  int32_t _M0L18scientificNotationS663;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS656 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS657 = 0;
  if (_M0L4signS658) {
    int32_t _M0L6_2atmpS1585 = _M0Lm5indexS657;
    int32_t _M0L6_2atmpS1586;
    if (
      _M0L6_2atmpS1585 < 0
      || _M0L6_2atmpS1585 >= Moonbit_array_length(_M0L6resultS656)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS656[_M0L6_2atmpS1585] = 45;
    _M0L6_2atmpS1586 = _M0Lm5indexS657;
    _M0Lm5indexS657 = _M0L6_2atmpS1586 + 1;
  }
  _M0L6outputS659 = _M0L1vS660->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS661 = _M0FPB17decimal__length17(_M0L6outputS659);
  _M0L8exponentS1715 = _M0L1vS660->$1;
  _M0L6_2atmpS1714 = _M0L8exponentS1715 + _M0L7olengthS661;
  _M0Lm3expS662 = _M0L6_2atmpS1714 - 1;
  _M0L6_2atmpS1713 = _M0Lm3expS662;
  if (_M0L6_2atmpS1713 >= -6) {
    int32_t _M0L6_2atmpS1712 = _M0Lm3expS662;
    _M0L6_2atmpS1711 = _M0L6_2atmpS1712 < 21;
  } else {
    _M0L6_2atmpS1711 = 0;
  }
  _M0L18scientificNotationS663 = !_M0L6_2atmpS1711;
  if (_M0L18scientificNotationS663) {
    int32_t _M0L7_2abindS664 = _M0L7olengthS661 - 1;
    uint64_t _M0L6outputS665;
    int32_t _M0L1iS666 = 0;
    uint64_t _M0L6outputS667 = _M0L6outputS659;
    int32_t _M0L6_2atmpS1587;
    int32_t _M0L6_2atmpS1591;
    int32_t _M0L6_2atmpS1590;
    int32_t _M0L6_2atmpS1589;
    int32_t _M0L6_2atmpS1588;
    int32_t _M0L6_2atmpS1595;
    int32_t _M0L6_2atmpS1596;
    int32_t _M0L6_2atmpS1597;
    int32_t _M0L6_2atmpS1598;
    int32_t _M0L6_2atmpS1599;
    int32_t _M0L6_2atmpS1605;
    int32_t _M0L6_2atmpS1638;
    moonbit_string_t _result_1954;
    while (1) {
      if (_M0L1iS666 < _M0L7_2abindS664) {
        uint64_t _M0L1cS668 = _M0L6outputS667 % 10ull;
        int32_t _M0L6_2atmpS1644 = _M0Lm5indexS657;
        int32_t _M0L6_2atmpS1643 = _M0L6_2atmpS1644 + _M0L7olengthS661;
        int32_t _M0L6_2atmpS1639 = _M0L6_2atmpS1643 - _M0L1iS666;
        int32_t _M0L6_2atmpS1642 = (int32_t)_M0L1cS668;
        int32_t _M0L6_2atmpS1641 = 48 + _M0L6_2atmpS1642;
        int32_t _M0L6_2atmpS1640 = _M0L6_2atmpS1641 & 0xff;
        int32_t _M0L6_2atmpS1645;
        uint64_t _M0L6_2atmpS1646;
        if (
          _M0L6_2atmpS1639 < 0
          || _M0L6_2atmpS1639 >= Moonbit_array_length(_M0L6resultS656)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS656[_M0L6_2atmpS1639] = _M0L6_2atmpS1640;
        _M0L6_2atmpS1645 = _M0L1iS666 + 1;
        _M0L6_2atmpS1646 = _M0L6outputS667 / 10ull;
        _M0L1iS666 = _M0L6_2atmpS1645;
        _M0L6outputS667 = _M0L6_2atmpS1646;
        continue;
      } else {
        _M0L6outputS665 = _M0L6outputS667;
      }
      break;
    }
    _M0L6_2atmpS1587 = _M0Lm5indexS657;
    _M0L6_2atmpS1591 = (int32_t)_M0L6outputS665;
    _M0L6_2atmpS1590 = _M0L6_2atmpS1591 % 10;
    _M0L6_2atmpS1589 = 48 + _M0L6_2atmpS1590;
    _M0L6_2atmpS1588 = _M0L6_2atmpS1589 & 0xff;
    if (
      _M0L6_2atmpS1587 < 0
      || _M0L6_2atmpS1587 >= Moonbit_array_length(_M0L6resultS656)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS656[_M0L6_2atmpS1587] = _M0L6_2atmpS1588;
    if (_M0L7olengthS661 > 1) {
      int32_t _M0L6_2atmpS1593 = _M0Lm5indexS657;
      int32_t _M0L6_2atmpS1592 = _M0L6_2atmpS1593 + 1;
      if (
        _M0L6_2atmpS1592 < 0
        || _M0L6_2atmpS1592 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1592] = 46;
    } else {
      int32_t _M0L6_2atmpS1594 = _M0Lm5indexS657;
      _M0Lm5indexS657 = _M0L6_2atmpS1594 - 1;
    }
    _M0L6_2atmpS1595 = _M0Lm5indexS657;
    _M0L6_2atmpS1596 = _M0L7olengthS661 + 1;
    _M0Lm5indexS657 = _M0L6_2atmpS1595 + _M0L6_2atmpS1596;
    _M0L6_2atmpS1597 = _M0Lm5indexS657;
    if (
      _M0L6_2atmpS1597 < 0
      || _M0L6_2atmpS1597 >= Moonbit_array_length(_M0L6resultS656)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS656[_M0L6_2atmpS1597] = 101;
    _M0L6_2atmpS1598 = _M0Lm5indexS657;
    _M0Lm5indexS657 = _M0L6_2atmpS1598 + 1;
    _M0L6_2atmpS1599 = _M0Lm3expS662;
    if (_M0L6_2atmpS1599 < 0) {
      int32_t _M0L6_2atmpS1600 = _M0Lm5indexS657;
      int32_t _M0L6_2atmpS1601;
      int32_t _M0L6_2atmpS1602;
      if (
        _M0L6_2atmpS1600 < 0
        || _M0L6_2atmpS1600 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1600] = 45;
      _M0L6_2atmpS1601 = _M0Lm5indexS657;
      _M0Lm5indexS657 = _M0L6_2atmpS1601 + 1;
      _M0L6_2atmpS1602 = _M0Lm3expS662;
      _M0Lm3expS662 = -_M0L6_2atmpS1602;
    } else {
      int32_t _M0L6_2atmpS1603 = _M0Lm5indexS657;
      int32_t _M0L6_2atmpS1604;
      if (
        _M0L6_2atmpS1603 < 0
        || _M0L6_2atmpS1603 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1603] = 43;
      _M0L6_2atmpS1604 = _M0Lm5indexS657;
      _M0Lm5indexS657 = _M0L6_2atmpS1604 + 1;
    }
    _M0L6_2atmpS1605 = _M0Lm3expS662;
    if (_M0L6_2atmpS1605 >= 100) {
      int32_t _M0L6_2atmpS1621 = _M0Lm3expS662;
      int32_t _M0L1aS670 = _M0L6_2atmpS1621 / 100;
      int32_t _M0L6_2atmpS1620 = _M0Lm3expS662;
      int32_t _M0L6_2atmpS1619 = _M0L6_2atmpS1620 / 10;
      int32_t _M0L1bS671 = _M0L6_2atmpS1619 % 10;
      int32_t _M0L6_2atmpS1618 = _M0Lm3expS662;
      int32_t _M0L1cS672 = _M0L6_2atmpS1618 % 10;
      int32_t _M0L6_2atmpS1606 = _M0Lm5indexS657;
      int32_t _M0L6_2atmpS1608 = 48 + _M0L1aS670;
      int32_t _M0L6_2atmpS1607 = _M0L6_2atmpS1608 & 0xff;
      int32_t _M0L6_2atmpS1612;
      int32_t _M0L6_2atmpS1609;
      int32_t _M0L6_2atmpS1611;
      int32_t _M0L6_2atmpS1610;
      int32_t _M0L6_2atmpS1616;
      int32_t _M0L6_2atmpS1613;
      int32_t _M0L6_2atmpS1615;
      int32_t _M0L6_2atmpS1614;
      int32_t _M0L6_2atmpS1617;
      if (
        _M0L6_2atmpS1606 < 0
        || _M0L6_2atmpS1606 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1606] = _M0L6_2atmpS1607;
      _M0L6_2atmpS1612 = _M0Lm5indexS657;
      _M0L6_2atmpS1609 = _M0L6_2atmpS1612 + 1;
      _M0L6_2atmpS1611 = 48 + _M0L1bS671;
      _M0L6_2atmpS1610 = _M0L6_2atmpS1611 & 0xff;
      if (
        _M0L6_2atmpS1609 < 0
        || _M0L6_2atmpS1609 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1609] = _M0L6_2atmpS1610;
      _M0L6_2atmpS1616 = _M0Lm5indexS657;
      _M0L6_2atmpS1613 = _M0L6_2atmpS1616 + 2;
      _M0L6_2atmpS1615 = 48 + _M0L1cS672;
      _M0L6_2atmpS1614 = _M0L6_2atmpS1615 & 0xff;
      if (
        _M0L6_2atmpS1613 < 0
        || _M0L6_2atmpS1613 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1613] = _M0L6_2atmpS1614;
      _M0L6_2atmpS1617 = _M0Lm5indexS657;
      _M0Lm5indexS657 = _M0L6_2atmpS1617 + 3;
    } else {
      int32_t _M0L6_2atmpS1622 = _M0Lm3expS662;
      if (_M0L6_2atmpS1622 >= 10) {
        int32_t _M0L6_2atmpS1632 = _M0Lm3expS662;
        int32_t _M0L1aS673 = _M0L6_2atmpS1632 / 10;
        int32_t _M0L6_2atmpS1631 = _M0Lm3expS662;
        int32_t _M0L1bS674 = _M0L6_2atmpS1631 % 10;
        int32_t _M0L6_2atmpS1623 = _M0Lm5indexS657;
        int32_t _M0L6_2atmpS1625 = 48 + _M0L1aS673;
        int32_t _M0L6_2atmpS1624 = _M0L6_2atmpS1625 & 0xff;
        int32_t _M0L6_2atmpS1629;
        int32_t _M0L6_2atmpS1626;
        int32_t _M0L6_2atmpS1628;
        int32_t _M0L6_2atmpS1627;
        int32_t _M0L6_2atmpS1630;
        if (
          _M0L6_2atmpS1623 < 0
          || _M0L6_2atmpS1623 >= Moonbit_array_length(_M0L6resultS656)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS656[_M0L6_2atmpS1623] = _M0L6_2atmpS1624;
        _M0L6_2atmpS1629 = _M0Lm5indexS657;
        _M0L6_2atmpS1626 = _M0L6_2atmpS1629 + 1;
        _M0L6_2atmpS1628 = 48 + _M0L1bS674;
        _M0L6_2atmpS1627 = _M0L6_2atmpS1628 & 0xff;
        if (
          _M0L6_2atmpS1626 < 0
          || _M0L6_2atmpS1626 >= Moonbit_array_length(_M0L6resultS656)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS656[_M0L6_2atmpS1626] = _M0L6_2atmpS1627;
        _M0L6_2atmpS1630 = _M0Lm5indexS657;
        _M0Lm5indexS657 = _M0L6_2atmpS1630 + 2;
      } else {
        int32_t _M0L6_2atmpS1633 = _M0Lm5indexS657;
        int32_t _M0L6_2atmpS1636 = _M0Lm3expS662;
        int32_t _M0L6_2atmpS1635 = 48 + _M0L6_2atmpS1636;
        int32_t _M0L6_2atmpS1634 = _M0L6_2atmpS1635 & 0xff;
        int32_t _M0L6_2atmpS1637;
        if (
          _M0L6_2atmpS1633 < 0
          || _M0L6_2atmpS1633 >= Moonbit_array_length(_M0L6resultS656)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS656[_M0L6_2atmpS1633] = _M0L6_2atmpS1634;
        _M0L6_2atmpS1637 = _M0Lm5indexS657;
        _M0Lm5indexS657 = _M0L6_2atmpS1637 + 1;
      }
    }
    _M0L6_2atmpS1638 = _M0Lm5indexS657;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1954
    = _M0FPB19string__from__bytes(_M0L6resultS656, 0, _M0L6_2atmpS1638);
    moonbit_decref_cycle_free(_M0L6resultS656);
    return _result_1954;
  } else {
    int32_t _M0L6_2atmpS1647 = _M0Lm3expS662;
    int32_t _M0L6_2atmpS1710;
    moonbit_string_t _result_1960;
    if (_M0L6_2atmpS1647 < 0) {
      int32_t _M0L6_2atmpS1648 = _M0Lm5indexS657;
      int32_t _M0L6_2atmpS1650;
      int32_t _M0L6_2atmpS1649;
      int32_t _M0L6_2atmpS1651;
      int32_t _M0L1iS675;
      int32_t _M0L6_2atmpS1666;
      int32_t _M0L6_2atmpS1668;
      int32_t _M0L6_2atmpS1667;
      int32_t _M0L7currentS677;
      int32_t _M0L1iS678;
      uint64_t _M0L6outputS679;
      if (
        _M0L6_2atmpS1648 < 0
        || _M0L6_2atmpS1648 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1648] = 48;
      _M0L6_2atmpS1650 = _M0Lm5indexS657;
      _M0L6_2atmpS1649 = _M0L6_2atmpS1650 + 1;
      if (
        _M0L6_2atmpS1649 < 0
        || _M0L6_2atmpS1649 >= Moonbit_array_length(_M0L6resultS656)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS656[_M0L6_2atmpS1649] = 46;
      _M0L6_2atmpS1651 = _M0Lm5indexS657;
      _M0Lm5indexS657 = _M0L6_2atmpS1651 + 2;
      _M0L1iS675 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1652 = _M0Lm3expS662;
        if (_M0L1iS675 > _M0L6_2atmpS1652) {
          int32_t _M0L6_2atmpS1655 = _M0Lm5indexS657;
          int32_t _M0L6_2atmpS1654 = _M0L6_2atmpS1655 - _M0L1iS675;
          int32_t _M0L6_2atmpS1653 = _M0L6_2atmpS1654 - 1;
          int32_t _M0L6_2atmpS1656;
          if (
            _M0L6_2atmpS1653 < 0
            || _M0L6_2atmpS1653 >= Moonbit_array_length(_M0L6resultS656)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS656[_M0L6_2atmpS1653] = 48;
          _M0L6_2atmpS1656 = _M0L1iS675 - 1;
          _M0L1iS675 = _M0L6_2atmpS1656;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1666 = _M0Lm5indexS657;
      _M0L6_2atmpS1668 = _M0Lm3expS662;
      _M0L6_2atmpS1667 = -1 - _M0L6_2atmpS1668;
      _M0L7currentS677 = _M0L6_2atmpS1666 + _M0L6_2atmpS1667;
      _M0L1iS678 = 0;
      _M0L6outputS679 = _M0L6outputS659;
      while (1) {
        if (_M0L1iS678 < _M0L7olengthS661) {
          int32_t _M0L6_2atmpS1663 = _M0L7currentS677 + _M0L7olengthS661;
          int32_t _M0L6_2atmpS1662 = _M0L6_2atmpS1663 - _M0L1iS678;
          int32_t _M0L6_2atmpS1657 = _M0L6_2atmpS1662 - 1;
          uint64_t _M0L6_2atmpS1661 = _M0L6outputS679 % 10ull;
          int32_t _M0L6_2atmpS1660 = (int32_t)_M0L6_2atmpS1661;
          int32_t _M0L6_2atmpS1659 = 48 + _M0L6_2atmpS1660;
          int32_t _M0L6_2atmpS1658 = _M0L6_2atmpS1659 & 0xff;
          int32_t _M0L6_2atmpS1664;
          uint64_t _M0L6_2atmpS1665;
          if (
            _M0L6_2atmpS1657 < 0
            || _M0L6_2atmpS1657 >= Moonbit_array_length(_M0L6resultS656)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS656[_M0L6_2atmpS1657] = _M0L6_2atmpS1658;
          _M0L6_2atmpS1664 = _M0L1iS678 + 1;
          _M0L6_2atmpS1665 = _M0L6outputS679 / 10ull;
          _M0L1iS678 = _M0L6_2atmpS1664;
          _M0L6outputS679 = _M0L6_2atmpS1665;
          continue;
        }
        break;
      }
      _M0Lm5indexS657 = _M0L7currentS677 + _M0L7olengthS661;
    } else {
      int32_t _M0L6_2atmpS1670 = _M0Lm3expS662;
      int32_t _M0L6_2atmpS1669 = _M0L6_2atmpS1670 + 1;
      if (_M0L6_2atmpS1669 >= _M0L7olengthS661) {
        int32_t _M0L1iS681 = 0;
        uint64_t _M0L6outputS682 = _M0L6outputS659;
        int32_t _M0L6_2atmpS1681;
        int32_t _M0L6_2atmpS1686;
        int32_t _M0L7_2abindS684;
        int32_t _M0L1iS685;
        int32_t _M0L6_2atmpS1687;
        int32_t _M0L6_2atmpS1690;
        int32_t _M0L6_2atmpS1689;
        int32_t _M0L6_2atmpS1688;
        while (1) {
          if (_M0L1iS681 < _M0L7olengthS661) {
            int32_t _M0L6_2atmpS1678 = _M0Lm5indexS657;
            int32_t _M0L6_2atmpS1677 = _M0L6_2atmpS1678 + _M0L7olengthS661;
            int32_t _M0L6_2atmpS1676 = _M0L6_2atmpS1677 - _M0L1iS681;
            int32_t _M0L6_2atmpS1671 = _M0L6_2atmpS1676 - 1;
            uint64_t _M0L6_2atmpS1675 = _M0L6outputS682 % 10ull;
            int32_t _M0L6_2atmpS1674 = (int32_t)_M0L6_2atmpS1675;
            int32_t _M0L6_2atmpS1673 = 48 + _M0L6_2atmpS1674;
            int32_t _M0L6_2atmpS1672 = _M0L6_2atmpS1673 & 0xff;
            int32_t _M0L6_2atmpS1679;
            uint64_t _M0L6_2atmpS1680;
            if (
              _M0L6_2atmpS1671 < 0
              || _M0L6_2atmpS1671 >= Moonbit_array_length(_M0L6resultS656)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS656[_M0L6_2atmpS1671] = _M0L6_2atmpS1672;
            _M0L6_2atmpS1679 = _M0L1iS681 + 1;
            _M0L6_2atmpS1680 = _M0L6outputS682 / 10ull;
            _M0L1iS681 = _M0L6_2atmpS1679;
            _M0L6outputS682 = _M0L6_2atmpS1680;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1681 = _M0Lm5indexS657;
        _M0Lm5indexS657 = _M0L6_2atmpS1681 + _M0L7olengthS661;
        _M0L6_2atmpS1686 = _M0Lm3expS662;
        _M0L7_2abindS684 = _M0L6_2atmpS1686 + 1;
        _M0L1iS685 = _M0L7olengthS661;
        while (1) {
          if (_M0L1iS685 < _M0L7_2abindS684) {
            int32_t _M0L6_2atmpS1684 = _M0Lm5indexS657;
            int32_t _M0L6_2atmpS1683 = _M0L6_2atmpS1684 + _M0L1iS685;
            int32_t _M0L6_2atmpS1682 = _M0L6_2atmpS1683 - _M0L7olengthS661;
            int32_t _M0L6_2atmpS1685;
            if (
              _M0L6_2atmpS1682 < 0
              || _M0L6_2atmpS1682 >= Moonbit_array_length(_M0L6resultS656)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS656[_M0L6_2atmpS1682] = 48;
            _M0L6_2atmpS1685 = _M0L1iS685 + 1;
            _M0L1iS685 = _M0L6_2atmpS1685;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1687 = _M0Lm5indexS657;
        _M0L6_2atmpS1690 = _M0Lm3expS662;
        _M0L6_2atmpS1689 = _M0L6_2atmpS1690 + 1;
        _M0L6_2atmpS1688 = _M0L6_2atmpS1689 - _M0L7olengthS661;
        _M0Lm5indexS657 = _M0L6_2atmpS1687 + _M0L6_2atmpS1688;
      } else {
        int32_t _M0L6_2atmpS1707 = _M0Lm5indexS657;
        int32_t _M0L6_2atmpS1706 = _M0L6_2atmpS1707 + 1;
        int32_t _M0L1iS687 = 0;
        int32_t _M0L7currentS688 = _M0L6_2atmpS1706;
        uint64_t _M0L6outputS689 = _M0L6outputS659;
        int32_t _M0L6_2atmpS1708;
        int32_t _M0L6_2atmpS1709;
        while (1) {
          if (_M0L1iS687 < _M0L7olengthS661) {
            int32_t _M0L6_2atmpS1702 = _M0L7olengthS661 - _M0L1iS687;
            int32_t _M0L6_2atmpS1700 = _M0L6_2atmpS1702 - 1;
            int32_t _M0L6_2atmpS1701 = _M0Lm3expS662;
            int32_t _M0L7currentS690;
            int32_t _M0L6_2atmpS1697;
            int32_t _M0L6_2atmpS1696;
            int32_t _M0L6_2atmpS1691;
            uint64_t _M0L6_2atmpS1695;
            int32_t _M0L6_2atmpS1694;
            int32_t _M0L6_2atmpS1693;
            int32_t _M0L6_2atmpS1692;
            int32_t _M0L6_2atmpS1698;
            uint64_t _M0L6_2atmpS1699;
            if (_M0L6_2atmpS1700 == _M0L6_2atmpS1701) {
              int32_t _M0L6_2atmpS1705 = _M0L7currentS688 + _M0L7olengthS661;
              int32_t _M0L6_2atmpS1704 = _M0L6_2atmpS1705 - _M0L1iS687;
              int32_t _M0L6_2atmpS1703 = _M0L6_2atmpS1704 - 1;
              if (
                _M0L6_2atmpS1703 < 0
                || _M0L6_2atmpS1703 >= Moonbit_array_length(_M0L6resultS656)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS656[_M0L6_2atmpS1703] = 46;
              _M0L7currentS690 = _M0L7currentS688 - 1;
            } else {
              _M0L7currentS690 = _M0L7currentS688;
            }
            _M0L6_2atmpS1697 = _M0L7currentS690 + _M0L7olengthS661;
            _M0L6_2atmpS1696 = _M0L6_2atmpS1697 - _M0L1iS687;
            _M0L6_2atmpS1691 = _M0L6_2atmpS1696 - 1;
            _M0L6_2atmpS1695 = _M0L6outputS689 % 10ull;
            _M0L6_2atmpS1694 = (int32_t)_M0L6_2atmpS1695;
            _M0L6_2atmpS1693 = 48 + _M0L6_2atmpS1694;
            _M0L6_2atmpS1692 = _M0L6_2atmpS1693 & 0xff;
            if (
              _M0L6_2atmpS1691 < 0
              || _M0L6_2atmpS1691 >= Moonbit_array_length(_M0L6resultS656)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS656[_M0L6_2atmpS1691] = _M0L6_2atmpS1692;
            _M0L6_2atmpS1698 = _M0L1iS687 + 1;
            _M0L6_2atmpS1699 = _M0L6outputS689 / 10ull;
            _M0L1iS687 = _M0L6_2atmpS1698;
            _M0L7currentS688 = _M0L7currentS690;
            _M0L6outputS689 = _M0L6_2atmpS1699;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1708 = _M0Lm5indexS657;
        _M0L6_2atmpS1709 = _M0L7olengthS661 + 1;
        _M0Lm5indexS657 = _M0L6_2atmpS1708 + _M0L6_2atmpS1709;
      }
    }
    _M0L6_2atmpS1710 = _M0Lm5indexS657;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1960
    = _M0FPB19string__from__bytes(_M0L6resultS656, 0, _M0L6_2atmpS1710);
    moonbit_decref_cycle_free(_M0L6resultS656);
    return _result_1960;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS602,
  uint32_t _M0L12ieeeExponentS601
) {
  int32_t _M0Lm2e2S599;
  uint64_t _M0Lm2m2S600;
  uint64_t _M0L6_2atmpS1584;
  uint64_t _M0L6_2atmpS1583;
  int32_t _M0L4evenS603;
  uint64_t _M0L6_2atmpS1582;
  uint64_t _M0L2mvS604;
  int32_t _M0L7mmShiftS605;
  uint64_t _M0Lm2vrS606;
  uint64_t _M0Lm2vpS607;
  uint64_t _M0Lm2vmS608;
  int32_t _M0Lm3e10S609;
  int32_t _M0Lm17vmIsTrailingZerosS610;
  int32_t _M0Lm17vrIsTrailingZerosS611;
  int32_t _M0L6_2atmpS1484;
  int32_t _M0Lm7removedS630;
  int32_t _M0Lm16lastRemovedDigitS631;
  uint64_t _M0Lm6outputS632;
  int32_t _M0L6_2atmpS1580;
  int32_t _M0L6_2atmpS1581;
  int32_t _M0L3expS655;
  uint64_t _M0L6_2atmpS1579;
  struct _M0TPB17FloatingDecimal64* _block_1966;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S599 = 0;
  _M0Lm2m2S600 = 0ull;
  if (_M0L12ieeeExponentS601 == 0u) {
    _M0Lm2e2S599 = -1076;
    _M0Lm2m2S600 = _M0L12ieeeMantissaS602;
  } else {
    int32_t _M0L6_2atmpS1483 = *(int32_t*)&_M0L12ieeeExponentS601;
    int32_t _M0L6_2atmpS1482 = _M0L6_2atmpS1483 - 1023;
    int32_t _M0L6_2atmpS1481 = _M0L6_2atmpS1482 - 52;
    _M0Lm2e2S599 = _M0L6_2atmpS1481 - 2;
    _M0Lm2m2S600 = 4503599627370496ull | _M0L12ieeeMantissaS602;
  }
  _M0L6_2atmpS1584 = _M0Lm2m2S600;
  _M0L6_2atmpS1583 = _M0L6_2atmpS1584 & 1ull;
  _M0L4evenS603 = _M0L6_2atmpS1583 == 0ull;
  _M0L6_2atmpS1582 = _M0Lm2m2S600;
  _M0L2mvS604 = 4ull * _M0L6_2atmpS1582;
  _M0L7mmShiftS605
  = _M0L12ieeeMantissaS602 != 0ull || _M0L12ieeeExponentS601 <= 1u;
  _M0Lm2vrS606 = 0ull;
  _M0Lm2vpS607 = 0ull;
  _M0Lm2vmS608 = 0ull;
  _M0Lm3e10S609 = 0;
  _M0Lm17vmIsTrailingZerosS610 = 0;
  _M0Lm17vrIsTrailingZerosS611 = 0;
  _M0L6_2atmpS1484 = _M0Lm2e2S599;
  if (_M0L6_2atmpS1484 >= 0) {
    int32_t _M0L6_2atmpS1506 = _M0Lm2e2S599;
    int32_t _M0L6_2atmpS1502;
    int32_t _M0L6_2atmpS1505;
    int32_t _M0L6_2atmpS1504;
    int32_t _M0L6_2atmpS1503;
    int32_t _M0L1qS612;
    int32_t _M0L6_2atmpS1501;
    int32_t _M0L6_2atmpS1500;
    int32_t _M0L1kS613;
    int32_t _M0L6_2atmpS1499;
    int32_t _M0L6_2atmpS1498;
    int32_t _M0L6_2atmpS1497;
    int32_t _M0L1iS614;
    struct _M0TPB8Pow5Pair _M0L4pow5S615;
    uint64_t _M0L6_2atmpS1496;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS616;
    uint64_t _M0L8_2avrOutS617;
    uint64_t _M0L8_2avpOutS618;
    uint64_t _M0L8_2avmOutS619;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1502 = _M0FPB9log10Pow2(_M0L6_2atmpS1506);
    _M0L6_2atmpS1505 = _M0Lm2e2S599;
    _M0L6_2atmpS1504 = _M0L6_2atmpS1505 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1503 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1504);
    _M0L1qS612 = _M0L6_2atmpS1502 - _M0L6_2atmpS1503;
    _M0Lm3e10S609 = _M0L1qS612;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1501 = _M0FPB8pow5bits(_M0L1qS612);
    _M0L6_2atmpS1500 = 125 + _M0L6_2atmpS1501;
    _M0L1kS613 = _M0L6_2atmpS1500 - 1;
    _M0L6_2atmpS1499 = _M0Lm2e2S599;
    _M0L6_2atmpS1498 = -_M0L6_2atmpS1499;
    _M0L6_2atmpS1497 = _M0L6_2atmpS1498 + _M0L1qS612;
    _M0L1iS614 = _M0L6_2atmpS1497 + _M0L1kS613;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S615 = _M0FPB22double__computeInvPow5(_M0L1qS612);
    _M0L6_2atmpS1496 = _M0Lm2m2S600;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS616
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1496, _M0L4pow5S615, _M0L1iS614, _M0L7mmShiftS605);
    _M0L8_2avrOutS617 = _M0L7_2abindS616.$0;
    _M0L8_2avpOutS618 = _M0L7_2abindS616.$1;
    _M0L8_2avmOutS619 = _M0L7_2abindS616.$2;
    _M0Lm2vrS606 = _M0L8_2avrOutS617;
    _M0Lm2vpS607 = _M0L8_2avpOutS618;
    _M0Lm2vmS608 = _M0L8_2avmOutS619;
    if (_M0L1qS612 <= 21) {
      int32_t _M0L6_2atmpS1492 = (int32_t)_M0L2mvS604;
      uint64_t _M0L6_2atmpS1495 = _M0L2mvS604 / 5ull;
      int32_t _M0L6_2atmpS1494 = (int32_t)_M0L6_2atmpS1495;
      int32_t _M0L6_2atmpS1493 = 5 * _M0L6_2atmpS1494;
      int32_t _M0L6mvMod5S620 = _M0L6_2atmpS1492 - _M0L6_2atmpS1493;
      if (_M0L6mvMod5S620 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS611
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS604, _M0L1qS612);
      } else if (_M0L4evenS603) {
        uint64_t _M0L6_2atmpS1486 = _M0L2mvS604 - 1ull;
        uint64_t _M0L6_2atmpS1487;
        uint64_t _M0L6_2atmpS1485;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1487 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS605);
        _M0L6_2atmpS1485 = _M0L6_2atmpS1486 - _M0L6_2atmpS1487;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS610
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1485, _M0L1qS612);
      } else {
        uint64_t _M0L6_2atmpS1488 = _M0Lm2vpS607;
        uint64_t _M0L6_2atmpS1491 = _M0L2mvS604 + 2ull;
        int32_t _M0L6_2atmpS1490;
        uint64_t _M0L6_2atmpS1489;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1490
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1491, _M0L1qS612);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1489 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1490);
        _M0Lm2vpS607 = _M0L6_2atmpS1488 - _M0L6_2atmpS1489;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1520 = _M0Lm2e2S599;
    int32_t _M0L6_2atmpS1519 = -_M0L6_2atmpS1520;
    int32_t _M0L6_2atmpS1514;
    int32_t _M0L6_2atmpS1518;
    int32_t _M0L6_2atmpS1517;
    int32_t _M0L6_2atmpS1516;
    int32_t _M0L6_2atmpS1515;
    int32_t _M0L1qS621;
    int32_t _M0L6_2atmpS1507;
    int32_t _M0L6_2atmpS1513;
    int32_t _M0L6_2atmpS1512;
    int32_t _M0L1iS622;
    int32_t _M0L6_2atmpS1511;
    int32_t _M0L1kS623;
    int32_t _M0L1jS624;
    struct _M0TPB8Pow5Pair _M0L4pow5S625;
    uint64_t _M0L6_2atmpS1510;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS626;
    uint64_t _M0L8_2avrOutS627;
    uint64_t _M0L8_2avpOutS628;
    uint64_t _M0L8_2avmOutS629;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1514 = _M0FPB9log10Pow5(_M0L6_2atmpS1519);
    _M0L6_2atmpS1518 = _M0Lm2e2S599;
    _M0L6_2atmpS1517 = -_M0L6_2atmpS1518;
    _M0L6_2atmpS1516 = _M0L6_2atmpS1517 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1515 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1516);
    _M0L1qS621 = _M0L6_2atmpS1514 - _M0L6_2atmpS1515;
    _M0L6_2atmpS1507 = _M0Lm2e2S599;
    _M0Lm3e10S609 = _M0L1qS621 + _M0L6_2atmpS1507;
    _M0L6_2atmpS1513 = _M0Lm2e2S599;
    _M0L6_2atmpS1512 = -_M0L6_2atmpS1513;
    _M0L1iS622 = _M0L6_2atmpS1512 - _M0L1qS621;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1511 = _M0FPB8pow5bits(_M0L1iS622);
    _M0L1kS623 = _M0L6_2atmpS1511 - 125;
    _M0L1jS624 = _M0L1qS621 - _M0L1kS623;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S625 = _M0FPB19double__computePow5(_M0L1iS622);
    _M0L6_2atmpS1510 = _M0Lm2m2S600;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS626
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1510, _M0L4pow5S625, _M0L1jS624, _M0L7mmShiftS605);
    _M0L8_2avrOutS627 = _M0L7_2abindS626.$0;
    _M0L8_2avpOutS628 = _M0L7_2abindS626.$1;
    _M0L8_2avmOutS629 = _M0L7_2abindS626.$2;
    _M0Lm2vrS606 = _M0L8_2avrOutS627;
    _M0Lm2vpS607 = _M0L8_2avpOutS628;
    _M0Lm2vmS608 = _M0L8_2avmOutS629;
    if (_M0L1qS621 <= 1) {
      _M0Lm17vrIsTrailingZerosS611 = 1;
      if (_M0L4evenS603) {
        int32_t _M0L6_2atmpS1508;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1508 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS605);
        _M0Lm17vmIsTrailingZerosS610 = _M0L6_2atmpS1508 == 1;
      } else {
        uint64_t _M0L6_2atmpS1509 = _M0Lm2vpS607;
        _M0Lm2vpS607 = _M0L6_2atmpS1509 - 1ull;
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
    int32_t _if__result_1963;
    uint64_t _M0L6_2atmpS1550;
    uint64_t _M0L6_2atmpS1556;
    uint64_t _M0L6_2atmpS1557;
    int32_t _if__result_1964;
    int32_t _M0L6_2atmpS1553;
    int64_t _M0L6_2atmpS1552;
    uint64_t _M0L6_2atmpS1551;
    while (1) {
      uint64_t _M0L6_2atmpS1533 = _M0Lm2vpS607;
      uint64_t _M0L7vpDiv10S633 = _M0L6_2atmpS1533 / 10ull;
      uint64_t _M0L6_2atmpS1532 = _M0Lm2vmS608;
      uint64_t _M0L7vmDiv10S634 = _M0L6_2atmpS1532 / 10ull;
      uint64_t _M0L6_2atmpS1531;
      int32_t _M0L6_2atmpS1528;
      int32_t _M0L6_2atmpS1530;
      int32_t _M0L6_2atmpS1529;
      int32_t _M0L7vmMod10S636;
      uint64_t _M0L6_2atmpS1527;
      uint64_t _M0L7vrDiv10S637;
      uint64_t _M0L6_2atmpS1526;
      int32_t _M0L6_2atmpS1523;
      int32_t _M0L6_2atmpS1525;
      int32_t _M0L6_2atmpS1524;
      int32_t _M0L7vrMod10S638;
      int32_t _M0L6_2atmpS1522;
      if (_M0L7vpDiv10S633 <= _M0L7vmDiv10S634) {
        break;
      }
      _M0L6_2atmpS1531 = _M0Lm2vmS608;
      _M0L6_2atmpS1528 = (int32_t)_M0L6_2atmpS1531;
      _M0L6_2atmpS1530 = (int32_t)_M0L7vmDiv10S634;
      _M0L6_2atmpS1529 = 10 * _M0L6_2atmpS1530;
      _M0L7vmMod10S636 = _M0L6_2atmpS1528 - _M0L6_2atmpS1529;
      _M0L6_2atmpS1527 = _M0Lm2vrS606;
      _M0L7vrDiv10S637 = _M0L6_2atmpS1527 / 10ull;
      _M0L6_2atmpS1526 = _M0Lm2vrS606;
      _M0L6_2atmpS1523 = (int32_t)_M0L6_2atmpS1526;
      _M0L6_2atmpS1525 = (int32_t)_M0L7vrDiv10S637;
      _M0L6_2atmpS1524 = 10 * _M0L6_2atmpS1525;
      _M0L7vrMod10S638 = _M0L6_2atmpS1523 - _M0L6_2atmpS1524;
      _M0Lm17vmIsTrailingZerosS610
      = _M0Lm17vmIsTrailingZerosS610 && _M0L7vmMod10S636 == 0;
      if (_M0Lm17vrIsTrailingZerosS611) {
        int32_t _M0L6_2atmpS1521 = _M0Lm16lastRemovedDigitS631;
        _M0Lm17vrIsTrailingZerosS611 = _M0L6_2atmpS1521 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS611 = 0;
      }
      _M0Lm16lastRemovedDigitS631 = _M0L7vrMod10S638;
      _M0Lm2vrS606 = _M0L7vrDiv10S637;
      _M0Lm2vpS607 = _M0L7vpDiv10S633;
      _M0Lm2vmS608 = _M0L7vmDiv10S634;
      _M0L6_2atmpS1522 = _M0Lm7removedS630;
      _M0Lm7removedS630 = _M0L6_2atmpS1522 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS610) {
      while (1) {
        uint64_t _M0L6_2atmpS1546 = _M0Lm2vmS608;
        uint64_t _M0L7vmDiv10S639 = _M0L6_2atmpS1546 / 10ull;
        uint64_t _M0L6_2atmpS1545 = _M0Lm2vmS608;
        int32_t _M0L6_2atmpS1542 = (int32_t)_M0L6_2atmpS1545;
        int32_t _M0L6_2atmpS1544 = (int32_t)_M0L7vmDiv10S639;
        int32_t _M0L6_2atmpS1543 = 10 * _M0L6_2atmpS1544;
        int32_t _M0L7vmMod10S640 = _M0L6_2atmpS1542 - _M0L6_2atmpS1543;
        uint64_t _M0L6_2atmpS1541;
        uint64_t _M0L7vpDiv10S642;
        uint64_t _M0L6_2atmpS1540;
        uint64_t _M0L7vrDiv10S643;
        uint64_t _M0L6_2atmpS1539;
        int32_t _M0L6_2atmpS1536;
        int32_t _M0L6_2atmpS1538;
        int32_t _M0L6_2atmpS1537;
        int32_t _M0L7vrMod10S644;
        int32_t _M0L6_2atmpS1535;
        if (_M0L7vmMod10S640 != 0) {
          break;
        }
        _M0L6_2atmpS1541 = _M0Lm2vpS607;
        _M0L7vpDiv10S642 = _M0L6_2atmpS1541 / 10ull;
        _M0L6_2atmpS1540 = _M0Lm2vrS606;
        _M0L7vrDiv10S643 = _M0L6_2atmpS1540 / 10ull;
        _M0L6_2atmpS1539 = _M0Lm2vrS606;
        _M0L6_2atmpS1536 = (int32_t)_M0L6_2atmpS1539;
        _M0L6_2atmpS1538 = (int32_t)_M0L7vrDiv10S643;
        _M0L6_2atmpS1537 = 10 * _M0L6_2atmpS1538;
        _M0L7vrMod10S644 = _M0L6_2atmpS1536 - _M0L6_2atmpS1537;
        if (_M0Lm17vrIsTrailingZerosS611) {
          int32_t _M0L6_2atmpS1534 = _M0Lm16lastRemovedDigitS631;
          _M0Lm17vrIsTrailingZerosS611 = _M0L6_2atmpS1534 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS611 = 0;
        }
        _M0Lm16lastRemovedDigitS631 = _M0L7vrMod10S644;
        _M0Lm2vrS606 = _M0L7vrDiv10S643;
        _M0Lm2vpS607 = _M0L7vpDiv10S642;
        _M0Lm2vmS608 = _M0L7vmDiv10S639;
        _M0L6_2atmpS1535 = _M0Lm7removedS630;
        _M0Lm7removedS630 = _M0L6_2atmpS1535 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS611) {
      int32_t _M0L6_2atmpS1549 = _M0Lm16lastRemovedDigitS631;
      if (_M0L6_2atmpS1549 == 5) {
        uint64_t _M0L6_2atmpS1548 = _M0Lm2vrS606;
        uint64_t _M0L6_2atmpS1547 = _M0L6_2atmpS1548 % 2ull;
        _if__result_1963 = _M0L6_2atmpS1547 == 0ull;
      } else {
        _if__result_1963 = 0;
      }
    } else {
      _if__result_1963 = 0;
    }
    if (_if__result_1963) {
      _M0Lm16lastRemovedDigitS631 = 4;
    }
    _M0L6_2atmpS1550 = _M0Lm2vrS606;
    _M0L6_2atmpS1556 = _M0Lm2vrS606;
    _M0L6_2atmpS1557 = _M0Lm2vmS608;
    if (_M0L6_2atmpS1556 == _M0L6_2atmpS1557) {
      if (!_M0L4evenS603) {
        _if__result_1964 = 1;
      } else {
        int32_t _M0L6_2atmpS1555 = _M0Lm17vmIsTrailingZerosS610;
        _if__result_1964 = !_M0L6_2atmpS1555;
      }
    } else {
      _if__result_1964 = 0;
    }
    if (_if__result_1964) {
      _M0L6_2atmpS1553 = 1;
    } else {
      int32_t _M0L6_2atmpS1554 = _M0Lm16lastRemovedDigitS631;
      _M0L6_2atmpS1553 = _M0L6_2atmpS1554 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1552 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1553);
    _M0L6_2atmpS1551 = *(uint64_t*)&_M0L6_2atmpS1552;
    _M0Lm6outputS632 = _M0L6_2atmpS1550 + _M0L6_2atmpS1551;
  } else {
    int32_t _M0Lm7roundUpS645 = 0;
    uint64_t _M0L6_2atmpS1578 = _M0Lm2vpS607;
    uint64_t _M0L8vpDiv100S646 = _M0L6_2atmpS1578 / 100ull;
    uint64_t _M0L6_2atmpS1577 = _M0Lm2vmS608;
    uint64_t _M0L8vmDiv100S647 = _M0L6_2atmpS1577 / 100ull;
    uint64_t _M0L6_2atmpS1572;
    uint64_t _M0L6_2atmpS1575;
    uint64_t _M0L6_2atmpS1576;
    int32_t _M0L6_2atmpS1574;
    uint64_t _M0L6_2atmpS1573;
    if (_M0L8vpDiv100S646 > _M0L8vmDiv100S647) {
      uint64_t _M0L6_2atmpS1563 = _M0Lm2vrS606;
      uint64_t _M0L8vrDiv100S648 = _M0L6_2atmpS1563 / 100ull;
      uint64_t _M0L6_2atmpS1562 = _M0Lm2vrS606;
      int32_t _M0L6_2atmpS1559 = (int32_t)_M0L6_2atmpS1562;
      int32_t _M0L6_2atmpS1561 = (int32_t)_M0L8vrDiv100S648;
      int32_t _M0L6_2atmpS1560 = 100 * _M0L6_2atmpS1561;
      int32_t _M0L8vrMod100S649 = _M0L6_2atmpS1559 - _M0L6_2atmpS1560;
      int32_t _M0L6_2atmpS1558;
      _M0Lm7roundUpS645 = _M0L8vrMod100S649 >= 50;
      _M0Lm2vrS606 = _M0L8vrDiv100S648;
      _M0Lm2vpS607 = _M0L8vpDiv100S646;
      _M0Lm2vmS608 = _M0L8vmDiv100S647;
      _M0L6_2atmpS1558 = _M0Lm7removedS630;
      _M0Lm7removedS630 = _M0L6_2atmpS1558 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1571 = _M0Lm2vpS607;
      uint64_t _M0L7vpDiv10S650 = _M0L6_2atmpS1571 / 10ull;
      uint64_t _M0L6_2atmpS1570 = _M0Lm2vmS608;
      uint64_t _M0L7vmDiv10S651 = _M0L6_2atmpS1570 / 10ull;
      uint64_t _M0L6_2atmpS1569;
      uint64_t _M0L7vrDiv10S653;
      uint64_t _M0L6_2atmpS1568;
      int32_t _M0L6_2atmpS1565;
      int32_t _M0L6_2atmpS1567;
      int32_t _M0L6_2atmpS1566;
      int32_t _M0L7vrMod10S654;
      int32_t _M0L6_2atmpS1564;
      if (_M0L7vpDiv10S650 <= _M0L7vmDiv10S651) {
        break;
      }
      _M0L6_2atmpS1569 = _M0Lm2vrS606;
      _M0L7vrDiv10S653 = _M0L6_2atmpS1569 / 10ull;
      _M0L6_2atmpS1568 = _M0Lm2vrS606;
      _M0L6_2atmpS1565 = (int32_t)_M0L6_2atmpS1568;
      _M0L6_2atmpS1567 = (int32_t)_M0L7vrDiv10S653;
      _M0L6_2atmpS1566 = 10 * _M0L6_2atmpS1567;
      _M0L7vrMod10S654 = _M0L6_2atmpS1565 - _M0L6_2atmpS1566;
      _M0Lm7roundUpS645 = _M0L7vrMod10S654 >= 5;
      _M0Lm2vrS606 = _M0L7vrDiv10S653;
      _M0Lm2vpS607 = _M0L7vpDiv10S650;
      _M0Lm2vmS608 = _M0L7vmDiv10S651;
      _M0L6_2atmpS1564 = _M0Lm7removedS630;
      _M0Lm7removedS630 = _M0L6_2atmpS1564 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1572 = _M0Lm2vrS606;
    _M0L6_2atmpS1575 = _M0Lm2vrS606;
    _M0L6_2atmpS1576 = _M0Lm2vmS608;
    _M0L6_2atmpS1574
    = _M0L6_2atmpS1575 == _M0L6_2atmpS1576 || _M0Lm7roundUpS645;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1573 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1574);
    _M0Lm6outputS632 = _M0L6_2atmpS1572 + _M0L6_2atmpS1573;
  }
  _M0L6_2atmpS1580 = _M0Lm3e10S609;
  _M0L6_2atmpS1581 = _M0Lm7removedS630;
  _M0L3expS655 = _M0L6_2atmpS1580 + _M0L6_2atmpS1581;
  _M0L6_2atmpS1579 = _M0Lm6outputS632;
  _block_1966
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_1966)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_1966->$0 = _M0L6_2atmpS1579;
  _block_1966->$1 = _M0L3expS655;
  return _block_1966;
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
  int32_t _M0L6_2atmpS1480;
  int32_t _M0L6_2atmpS1479;
  int32_t _M0L4baseS577;
  int32_t _M0L5base2S579;
  int32_t _M0L6offsetS580;
  int32_t _M0L6_2atmpS1478;
  uint64_t _M0L4mul0S581;
  int32_t _M0L6_2atmpS1477;
  int32_t _M0L6_2atmpS1476;
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
  int32_t _M0L6_2atmpS1474;
  int32_t _M0L6_2atmpS1475;
  int32_t _M0L5deltaS592;
  uint64_t _M0L6_2atmpS1473;
  uint64_t _M0L6_2atmpS1465;
  int32_t _M0L6_2atmpS1472;
  uint32_t _M0L6_2atmpS1469;
  int32_t _M0L6_2atmpS1471;
  int32_t _M0L6_2atmpS1470;
  uint32_t _M0L6_2atmpS1468;
  uint32_t _M0L6_2atmpS1467;
  uint64_t _M0L6_2atmpS1466;
  uint64_t _M0L1aS593;
  uint64_t _M0L6_2atmpS1464;
  uint64_t _M0L1bS594;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1480 = _M0L1iS578 + 26;
  _M0L6_2atmpS1479 = _M0L6_2atmpS1480 - 1;
  _M0L4baseS577 = _M0L6_2atmpS1479 / 26;
  _M0L5base2S579 = _M0L4baseS577 * 26;
  _M0L6offsetS580 = _M0L5base2S579 - _M0L1iS578;
  _M0L6_2atmpS1478 = _M0L4baseS577 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S581
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1478);
  _M0L6_2atmpS1477 = _M0L4baseS577 * 2;
  _M0L6_2atmpS1476 = _M0L6_2atmpS1477 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S582
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1476);
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
    uint64_t _M0L6_2atmpS1463 = _M0Lm5high1S591;
    _M0Lm5high1S591 = _M0L6_2atmpS1463 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1474 = _M0FPB8pow5bits(_M0L5base2S579);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1475 = _M0FPB8pow5bits(_M0L1iS578);
  _M0L5deltaS592 = _M0L6_2atmpS1474 - _M0L6_2atmpS1475;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1473
  = _M0FPB13shiftright128(_M0L7_2alow0S588, _M0L3sumS590, _M0L5deltaS592);
  _M0L6_2atmpS1465 = _M0L6_2atmpS1473 + 1ull;
  _M0L6_2atmpS1472 = _M0L1iS578 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1469
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1472);
  _M0L6_2atmpS1471 = _M0L1iS578 % 16;
  _M0L6_2atmpS1470 = _M0L6_2atmpS1471 << 1;
  _M0L6_2atmpS1468 = _M0L6_2atmpS1469 >> (_M0L6_2atmpS1470 & 31);
  _M0L6_2atmpS1467 = _M0L6_2atmpS1468 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1466 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1467);
  _M0L1aS593 = _M0L6_2atmpS1465 + _M0L6_2atmpS1466;
  _M0L6_2atmpS1464 = _M0Lm5high1S591;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS594
  = _M0FPB13shiftright128(_M0L3sumS590, _M0L6_2atmpS1464, _M0L5deltaS592);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS593, .$1 = _M0L1bS594};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS560) {
  int32_t _M0L4baseS559;
  int32_t _M0L5base2S561;
  int32_t _M0L6offsetS562;
  int32_t _M0L6_2atmpS1462;
  uint64_t _M0L4mul0S563;
  int32_t _M0L6_2atmpS1461;
  int32_t _M0L6_2atmpS1460;
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
  int32_t _M0L6_2atmpS1458;
  int32_t _M0L6_2atmpS1459;
  int32_t _M0L5deltaS574;
  uint64_t _M0L6_2atmpS1450;
  int32_t _M0L6_2atmpS1457;
  uint32_t _M0L6_2atmpS1454;
  int32_t _M0L6_2atmpS1456;
  int32_t _M0L6_2atmpS1455;
  uint32_t _M0L6_2atmpS1453;
  uint32_t _M0L6_2atmpS1452;
  uint64_t _M0L6_2atmpS1451;
  uint64_t _M0L1aS575;
  uint64_t _M0L6_2atmpS1449;
  uint64_t _M0L1bS576;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS559 = _M0L1iS560 / 26;
  _M0L5base2S561 = _M0L4baseS559 * 26;
  _M0L6offsetS562 = _M0L1iS560 - _M0L5base2S561;
  _M0L6_2atmpS1462 = _M0L4baseS559 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S563
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1462);
  _M0L6_2atmpS1461 = _M0L4baseS559 * 2;
  _M0L6_2atmpS1460 = _M0L6_2atmpS1461 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S564
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1460);
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
    uint64_t _M0L6_2atmpS1448 = _M0Lm5high1S573;
    _M0Lm5high1S573 = _M0L6_2atmpS1448 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1458 = _M0FPB8pow5bits(_M0L1iS560);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1459 = _M0FPB8pow5bits(_M0L5base2S561);
  _M0L5deltaS574 = _M0L6_2atmpS1458 - _M0L6_2atmpS1459;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1450
  = _M0FPB13shiftright128(_M0L7_2alow0S570, _M0L3sumS572, _M0L5deltaS574);
  _M0L6_2atmpS1457 = _M0L1iS560 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1454
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1457);
  _M0L6_2atmpS1456 = _M0L1iS560 % 16;
  _M0L6_2atmpS1455 = _M0L6_2atmpS1456 << 1;
  _M0L6_2atmpS1453 = _M0L6_2atmpS1454 >> (_M0L6_2atmpS1455 & 31);
  _M0L6_2atmpS1452 = _M0L6_2atmpS1453 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1451 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1452);
  _M0L1aS575 = _M0L6_2atmpS1450 + _M0L6_2atmpS1451;
  _M0L6_2atmpS1449 = _M0Lm5high1S573;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS576
  = _M0FPB13shiftright128(_M0L3sumS572, _M0L6_2atmpS1449, _M0L5deltaS574);
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
  uint64_t _M0L6_2atmpS1447;
  uint64_t _M0L2hiS541;
  uint64_t _M0L3lo2S542;
  uint64_t _M0L6_2atmpS1445;
  uint64_t _M0L6_2atmpS1446;
  uint64_t _M0L4mid2S543;
  uint64_t _M0L6_2atmpS1444;
  uint64_t _M0L3hi2S544;
  int32_t _M0L6_2atmpS1443;
  int32_t _M0L6_2atmpS1442;
  uint64_t _M0L2vpS545;
  uint64_t _M0Lm2vmS547;
  int32_t _M0L6_2atmpS1441;
  int32_t _M0L6_2atmpS1440;
  uint64_t _M0L2vrS558;
  uint64_t _M0L6_2atmpS1439;
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
    _M0L6_2atmpS1447 = 1ull;
  } else {
    _M0L6_2atmpS1447 = 0ull;
  }
  _M0L2hiS541 = _M0L6_2ahi2S539 + _M0L6_2atmpS1447;
  _M0L3lo2S542 = _M0L5_2aloS535 + _M0L7_2amul0S529;
  _M0L6_2atmpS1445 = _M0L3midS540 + _M0L7_2amul1S531;
  if (_M0L3lo2S542 < _M0L5_2aloS535) {
    _M0L6_2atmpS1446 = 1ull;
  } else {
    _M0L6_2atmpS1446 = 0ull;
  }
  _M0L4mid2S543 = _M0L6_2atmpS1445 + _M0L6_2atmpS1446;
  if (_M0L4mid2S543 < _M0L3midS540) {
    _M0L6_2atmpS1444 = 1ull;
  } else {
    _M0L6_2atmpS1444 = 0ull;
  }
  _M0L3hi2S544 = _M0L2hiS541 + _M0L6_2atmpS1444;
  _M0L6_2atmpS1443 = _M0L1jS546 - 64;
  _M0L6_2atmpS1442 = _M0L6_2atmpS1443 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS545
  = _M0FPB13shiftright128(_M0L4mid2S543, _M0L3hi2S544, _M0L6_2atmpS1442);
  _M0Lm2vmS547 = 0ull;
  if (_M0L7mmShiftS548) {
    uint64_t _M0L3lo3S549 = _M0L5_2aloS535 - _M0L7_2amul0S529;
    uint64_t _M0L6_2atmpS1429 = _M0L3midS540 - _M0L7_2amul1S531;
    uint64_t _M0L6_2atmpS1430;
    uint64_t _M0L4mid3S550;
    uint64_t _M0L6_2atmpS1428;
    uint64_t _M0L3hi3S551;
    int32_t _M0L6_2atmpS1427;
    int32_t _M0L6_2atmpS1426;
    if (_M0L5_2aloS535 < _M0L3lo3S549) {
      _M0L6_2atmpS1430 = 1ull;
    } else {
      _M0L6_2atmpS1430 = 0ull;
    }
    _M0L4mid3S550 = _M0L6_2atmpS1429 - _M0L6_2atmpS1430;
    if (_M0L3midS540 < _M0L4mid3S550) {
      _M0L6_2atmpS1428 = 1ull;
    } else {
      _M0L6_2atmpS1428 = 0ull;
    }
    _M0L3hi3S551 = _M0L2hiS541 - _M0L6_2atmpS1428;
    _M0L6_2atmpS1427 = _M0L1jS546 - 64;
    _M0L6_2atmpS1426 = _M0L6_2atmpS1427 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS547
    = _M0FPB13shiftright128(_M0L4mid3S550, _M0L3hi3S551, _M0L6_2atmpS1426);
  } else {
    uint64_t _M0L3lo3S552 = _M0L5_2aloS535 + _M0L5_2aloS535;
    uint64_t _M0L6_2atmpS1437 = _M0L3midS540 + _M0L3midS540;
    uint64_t _M0L6_2atmpS1438;
    uint64_t _M0L4mid3S553;
    uint64_t _M0L6_2atmpS1435;
    uint64_t _M0L6_2atmpS1436;
    uint64_t _M0L3hi3S554;
    uint64_t _M0L3lo4S555;
    uint64_t _M0L6_2atmpS1433;
    uint64_t _M0L6_2atmpS1434;
    uint64_t _M0L4mid4S556;
    uint64_t _M0L6_2atmpS1432;
    uint64_t _M0L3hi4S557;
    int32_t _M0L6_2atmpS1431;
    if (_M0L3lo3S552 < _M0L5_2aloS535) {
      _M0L6_2atmpS1438 = 1ull;
    } else {
      _M0L6_2atmpS1438 = 0ull;
    }
    _M0L4mid3S553 = _M0L6_2atmpS1437 + _M0L6_2atmpS1438;
    _M0L6_2atmpS1435 = _M0L2hiS541 + _M0L2hiS541;
    if (_M0L4mid3S553 < _M0L3midS540) {
      _M0L6_2atmpS1436 = 1ull;
    } else {
      _M0L6_2atmpS1436 = 0ull;
    }
    _M0L3hi3S554 = _M0L6_2atmpS1435 + _M0L6_2atmpS1436;
    _M0L3lo4S555 = _M0L3lo3S552 - _M0L7_2amul0S529;
    _M0L6_2atmpS1433 = _M0L4mid3S553 - _M0L7_2amul1S531;
    if (_M0L3lo3S552 < _M0L3lo4S555) {
      _M0L6_2atmpS1434 = 1ull;
    } else {
      _M0L6_2atmpS1434 = 0ull;
    }
    _M0L4mid4S556 = _M0L6_2atmpS1433 - _M0L6_2atmpS1434;
    if (_M0L4mid3S553 < _M0L4mid4S556) {
      _M0L6_2atmpS1432 = 1ull;
    } else {
      _M0L6_2atmpS1432 = 0ull;
    }
    _M0L3hi4S557 = _M0L3hi3S554 - _M0L6_2atmpS1432;
    _M0L6_2atmpS1431 = _M0L1jS546 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS547
    = _M0FPB13shiftright128(_M0L4mid4S556, _M0L3hi4S557, _M0L6_2atmpS1431);
  }
  _M0L6_2atmpS1441 = _M0L1jS546 - 64;
  _M0L6_2atmpS1440 = _M0L6_2atmpS1441 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS558
  = _M0FPB13shiftright128(_M0L3midS540, _M0L2hiS541, _M0L6_2atmpS1440);
  _M0L6_2atmpS1439 = _M0Lm2vmS547;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS558,
                                                .$1 = _M0L2vpS545,
                                                .$2 = _M0L6_2atmpS1439};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS527,
  int32_t _M0L1pS528
) {
  uint64_t _M0L6_2atmpS1425;
  uint64_t _M0L6_2atmpS1424;
  uint64_t _M0L6_2atmpS1423;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1425 = 1ull << (_M0L1pS528 & 63);
  _M0L6_2atmpS1424 = _M0L6_2atmpS1425 - 1ull;
  _M0L6_2atmpS1423 = _M0L5valueS527 & _M0L6_2atmpS1424;
  return _M0L6_2atmpS1423 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS525,
  int32_t _M0L1pS526
) {
  int32_t _M0L6_2atmpS1422;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1422 = _M0FPB10pow5Factor(_M0L5valueS525);
  return _M0L6_2atmpS1422 >= _M0L1pS526;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS520) {
  uint64_t _M0L6_2atmpS1413;
  uint64_t _M0L6_2atmpS1414;
  uint64_t _M0L6_2atmpS1415;
  uint64_t _M0L6_2atmpS1416;
  uint64_t _M0L6_2atmpS1421;
  int32_t _M0L5countS521;
  uint64_t _M0L1vS522;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1413 = _M0L5valueS520 % 5ull;
  if (_M0L6_2atmpS1413 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1414 = _M0L5valueS520 % 25ull;
  if (_M0L6_2atmpS1414 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1415 = _M0L5valueS520 % 125ull;
  if (_M0L6_2atmpS1415 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1416 = _M0L5valueS520 % 625ull;
  if (_M0L6_2atmpS1416 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1421 = _M0L5valueS520 / 625ull;
  _M0L5countS521 = 4;
  _M0L1vS522 = _M0L6_2atmpS1421;
  while (1) {
    if (_M0L1vS522 > 0ull) {
      uint64_t _M0L6_2atmpS1417 = _M0L1vS522 % 5ull;
      int32_t _M0L6_2atmpS1418;
      uint64_t _M0L6_2atmpS1419;
      if (_M0L6_2atmpS1417 != 0ull) {
        return _M0L5countS521;
      }
      _M0L6_2atmpS1418 = _M0L5countS521 + 1;
      _M0L6_2atmpS1419 = _M0L1vS522 / 5ull;
      _M0L5countS521 = _M0L6_2atmpS1418;
      _M0L1vS522 = _M0L6_2atmpS1419;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS524;
      moonbit_string_t _M0L6_2atmpS1420;
      int32_t _result_1968;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS524
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS524, (moonbit_string_t)moonbit_string_literal_16.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS524, _M0L5valueS520);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1420
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS524);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS524);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_1968 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1420);
      moonbit_decref_cycle_free(_M0L6_2atmpS1420);
      return _result_1968;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS519,
  uint64_t _M0L2hiS517,
  int32_t _M0L4distS518
) {
  int32_t _M0L6_2atmpS1412;
  uint64_t _M0L6_2atmpS1410;
  uint64_t _M0L6_2atmpS1411;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1412 = 64 - _M0L4distS518;
  _M0L6_2atmpS1410 = _M0L2hiS517 << (_M0L6_2atmpS1412 & 63);
  _M0L6_2atmpS1411 = _M0L2loS519 >> (_M0L4distS518 & 63);
  return _M0L6_2atmpS1410 | _M0L6_2atmpS1411;
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
  uint64_t _M0L6_2atmpS1408;
  uint64_t _M0L6_2atmpS1409;
  uint64_t _M0L1yS513;
  uint64_t _M0L6_2atmpS1406;
  uint64_t _M0L6_2atmpS1407;
  uint64_t _M0L1zS514;
  uint64_t _M0L6_2atmpS1404;
  uint64_t _M0L6_2atmpS1405;
  uint64_t _M0L6_2atmpS1402;
  uint64_t _M0L6_2atmpS1403;
  uint64_t _M0L1wS515;
  uint64_t _M0L2loS516;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS506 = _M0L1aS507 & 4294967295ull;
  _M0L3aHiS508 = _M0L1aS507 >> 32;
  _M0L3bLoS509 = _M0L1bS510 & 4294967295ull;
  _M0L3bHiS511 = _M0L1bS510 >> 32;
  _M0L1xS512 = _M0L3aLoS506 * _M0L3bLoS509;
  _M0L6_2atmpS1408 = _M0L3aHiS508 * _M0L3bLoS509;
  _M0L6_2atmpS1409 = _M0L1xS512 >> 32;
  _M0L1yS513 = _M0L6_2atmpS1408 + _M0L6_2atmpS1409;
  _M0L6_2atmpS1406 = _M0L3aLoS506 * _M0L3bHiS511;
  _M0L6_2atmpS1407 = _M0L1yS513 & 4294967295ull;
  _M0L1zS514 = _M0L6_2atmpS1406 + _M0L6_2atmpS1407;
  _M0L6_2atmpS1404 = _M0L3aHiS508 * _M0L3bHiS511;
  _M0L6_2atmpS1405 = _M0L1yS513 >> 32;
  _M0L6_2atmpS1402 = _M0L6_2atmpS1404 + _M0L6_2atmpS1405;
  _M0L6_2atmpS1403 = _M0L1zS514 >> 32;
  _M0L1wS515 = _M0L6_2atmpS1402 + _M0L6_2atmpS1403;
  _M0L2loS516 = _M0L1aS507 * _M0L1bS510;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS516, .$1 = _M0L1wS515};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS504,
  int32_t _M0L4fromS501,
  int32_t _M0L2toS500
) {
  int32_t _M0L3lenS499;
  int32_t _M0L6_2atmpS1401;
  uint16_t* _M0L6bufferS502;
  int32_t _M0L1iS503;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS499 = _M0L2toS500 - _M0L4fromS501;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1401 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS502
  = (uint16_t*)moonbit_make_string(_M0L3lenS499, _M0L6_2atmpS1401);
  _M0L1iS503 = 0;
  while (1) {
    if (_M0L1iS503 < _M0L3lenS499) {
      int32_t _M0L6_2atmpS1399 = _M0L4fromS501 + _M0L1iS503;
      int32_t _M0L6_2atmpS1398;
      int32_t _M0L6_2atmpS1397;
      int32_t _M0L6_2atmpS1400;
      if (
        _M0L6_2atmpS1399 < 0
        || _M0L6_2atmpS1399 >= Moonbit_array_length(_M0L5bytesS504)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1398 = (int32_t)_M0L5bytesS504[_M0L6_2atmpS1399];
      _M0L6_2atmpS1397 = (uint16_t)_M0L6_2atmpS1398;
      if (
        _M0L1iS503 < 0 || _M0L1iS503 >= Moonbit_array_length(_M0L6bufferS502)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS502[_M0L1iS503] = _M0L6_2atmpS1397;
      _M0L6_2atmpS1400 = _M0L1iS503 + 1;
      _M0L1iS503 = _M0L6_2atmpS1400;
      continue;
    }
    break;
  }
  return _M0L6bufferS502;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS498) {
  int32_t _M0L6_2atmpS1396;
  uint32_t _M0L6_2atmpS1395;
  uint32_t _M0L6_2atmpS1394;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1396 = _M0L1eS498 * 78913;
  _M0L6_2atmpS1395 = *(uint32_t*)&_M0L6_2atmpS1396;
  _M0L6_2atmpS1394 = _M0L6_2atmpS1395 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1394;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS497) {
  int32_t _M0L6_2atmpS1393;
  uint32_t _M0L6_2atmpS1392;
  uint32_t _M0L6_2atmpS1391;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1393 = _M0L1eS497 * 732923;
  _M0L6_2atmpS1392 = *(uint32_t*)&_M0L6_2atmpS1393;
  _M0L6_2atmpS1391 = _M0L6_2atmpS1392 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1391;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS495,
  int32_t _M0L8exponentS496,
  int32_t _M0L8mantissaS493
) {
  moonbit_string_t _M0L1sS494;
  moonbit_string_t _result_1971;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS493) {
    return (moonbit_string_t)moonbit_string_literal_17.data;
  }
  if (_M0L4signS495) {
    _M0L1sS494 = (moonbit_string_t)moonbit_string_literal_18.data;
  } else {
    _M0L1sS494 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS496) {
    moonbit_string_t _result_1970;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_1970
    = moonbit_add_string(_M0L1sS494, (moonbit_string_t)moonbit_string_literal_19.data);
    moonbit_decref_cycle_free(_M0L1sS494);
    return _result_1970;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_1971
  = moonbit_add_string(_M0L1sS494, (moonbit_string_t)moonbit_string_literal_20.data);
  moonbit_decref_cycle_free(_M0L1sS494);
  return _result_1971;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS492) {
  int32_t _M0L6_2atmpS1390;
  uint32_t _M0L6_2atmpS1389;
  uint32_t _M0L6_2atmpS1388;
  int32_t _M0L6_2atmpS1387;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1390 = _M0L1eS492 * 1217359;
  _M0L6_2atmpS1389 = *(uint32_t*)&_M0L6_2atmpS1390;
  _M0L6_2atmpS1388 = _M0L6_2atmpS1389 >> 19;
  _M0L6_2atmpS1387 = *(int32_t*)&_M0L6_2atmpS1388;
  return _M0L6_2atmpS1387 + 1;
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

struct _M0TPB5ArrayGsE* _M0MPC15array5Array20unsafe__make__uninitGsE(
  int32_t _M0L3lenS489
) {
  moonbit_string_t* _M0L6_2atmpS1386;
  struct _M0TPB5ArrayGsE* _block_1972;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1386
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L3lenS489, (moonbit_string_t)moonbit_string_literal_0.data);
  _block_1972
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_block_1972)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _block_1972->$0 = _M0L6_2atmpS1386;
  _block_1972->$1 = _M0L3lenS489;
  return _block_1972;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS485,
  int32_t _M0L5indexS486
) {
  uint64_t* _M0L6_2atmpS1384;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1384 = _M0L4selfS485;
  if (
    _M0L5indexS486 < 0
    || _M0L5indexS486 >= Moonbit_array_length(_M0L6_2atmpS1384)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1384[_M0L5indexS486];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS487,
  int32_t _M0L5indexS488
) {
  uint32_t* _M0L6_2atmpS1385;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1385 = _M0L4selfS487;
  if (
    _M0L5indexS488 < 0
    || _M0L5indexS488 >= Moonbit_array_length(_M0L6_2atmpS1385)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1385[_M0L5indexS488];
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

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS482) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS482;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS473,
  float _M0L5valueS475
) {
  int32_t _M0L3lenS1363;
  float* _M0L6_2atmpS1365;
  int32_t _M0L6_2atmpS1364;
  int32_t _M0L6lengthS474;
  float* _M0L3bufS1368;
  int32_t _M0L6_2atmpS1369;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1363 = _M0L4selfS473->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1365 = _M0MPC15array5Array6bufferGfE(_M0L4selfS473);
  _M0L6_2atmpS1364 = Moonbit_array_length(_M0L6_2atmpS1365);
  moonbit_decref_cycle_free(_M0L6_2atmpS1365);
  if (_M0L3lenS1363 == _M0L6_2atmpS1364) {
    int32_t _M0L3lenS1367 = _M0L4selfS473->$1;
    int32_t _M0L6_2atmpS1366 = _M0L3lenS1367 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS473, _M0L6_2atmpS1366);
  }
  _M0L6lengthS474 = _M0L4selfS473->$1;
  _M0L3bufS1368 = _M0L4selfS473->$0;
  _M0L3bufS1368[_M0L6lengthS474] = _M0L5valueS475;
  _M0L6_2atmpS1369 = _M0L6lengthS474 + 1;
  _M0L4selfS473->$1 = _M0L6_2atmpS1369;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS476,
  moonbit_string_t _M0L5valueS478
) {
  int32_t _M0L3lenS1370;
  moonbit_string_t* _M0L6_2atmpS1372;
  int32_t _M0L6_2atmpS1371;
  int32_t _M0L6lengthS477;
  moonbit_string_t* _M0L3bufS1375;
  moonbit_string_t _M0L6_2aoldS1894;
  int32_t _M0L6_2atmpS1376;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1370 = _M0L4selfS476->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1372 = _M0MPC15array5Array6bufferGsE(_M0L4selfS476);
  _M0L6_2atmpS1371 = Moonbit_array_length(_M0L6_2atmpS1372);
  moonbit_decref_cycle_free(_M0L6_2atmpS1372);
  if (_M0L3lenS1370 == _M0L6_2atmpS1371) {
    int32_t _M0L3lenS1374 = _M0L4selfS476->$1;
    int32_t _M0L6_2atmpS1373 = _M0L3lenS1374 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS476, _M0L6_2atmpS1373);
  }
  _M0L6lengthS477 = _M0L4selfS476->$1;
  _M0L3bufS1375 = _M0L4selfS476->$0;
  _M0L6_2aoldS1894 = (moonbit_string_t)_M0L3bufS1375[_M0L6lengthS477];
  moonbit_decref_cycle_free(_M0L6_2aoldS1894);
  _M0L3bufS1375[_M0L6lengthS477] = _M0L5valueS478;
  _M0L6_2atmpS1376 = _M0L6lengthS477 + 1;
  _M0L4selfS476->$1 = _M0L6_2atmpS1376;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS479,
  struct _M0TUsiE* _M0L5valueS481
) {
  int32_t _M0L3lenS1377;
  struct _M0TUsiE** _M0L6_2atmpS1379;
  int32_t _M0L6_2atmpS1378;
  int32_t _M0L6lengthS480;
  struct _M0TUsiE** _M0L3bufS1382;
  struct _M0TUsiE* _M0L6_2aoldS1895;
  int32_t _M0L6_2atmpS1383;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1377 = _M0L4selfS479->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1379 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS479);
  _M0L6_2atmpS1378 = Moonbit_array_length(_M0L6_2atmpS1379);
  moonbit_decref_cycle_free(_M0L6_2atmpS1379);
  if (_M0L3lenS1377 == _M0L6_2atmpS1378) {
    int32_t _M0L3lenS1381 = _M0L4selfS479->$1;
    int32_t _M0L6_2atmpS1380 = _M0L3lenS1381 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS479, _M0L6_2atmpS1380);
  }
  _M0L6lengthS480 = _M0L4selfS479->$1;
  _M0L3bufS1382 = _M0L4selfS479->$0;
  _M0L6_2aoldS1895 = (struct _M0TUsiE*)_M0L3bufS1382[_M0L6lengthS480];
  if (_M0L6_2aoldS1895) {
    moonbit_decref_cycle_free(_M0L6_2aoldS1895);
  }
  _M0L3bufS1382[_M0L6lengthS480] = _M0L5valueS481;
  _M0L6_2atmpS1383 = _M0L6lengthS480 + 1;
  _M0L4selfS479->$1 = _M0L6_2atmpS1383;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS462,
  int32_t _M0L8requiredS464
) {
  int32_t _M0L8old__capS461;
  int32_t _M0L3lenS1360;
  int32_t _M0L8new__capS463;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS461 = _M0MPC15array5Array8capacityGfE(_M0L4selfS462);
  _M0L3lenS1360 = _M0L4selfS462->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS463
  = _M0FPB23array__growth__capacity(_M0L8old__capS461, _M0L3lenS1360, _M0L8requiredS464);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS462, _M0L8new__capS463);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS466,
  int32_t _M0L8requiredS468
) {
  int32_t _M0L8old__capS465;
  int32_t _M0L3lenS1361;
  int32_t _M0L8new__capS467;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS465 = _M0MPC15array5Array8capacityGsE(_M0L4selfS466);
  _M0L3lenS1361 = _M0L4selfS466->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS467
  = _M0FPB23array__growth__capacity(_M0L8old__capS465, _M0L3lenS1361, _M0L8requiredS468);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS466, _M0L8new__capS467);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS470,
  int32_t _M0L8requiredS472
) {
  int32_t _M0L8old__capS469;
  int32_t _M0L3lenS1362;
  int32_t _M0L8new__capS471;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS469 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS470);
  _M0L3lenS1362 = _M0L4selfS470->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS471
  = _M0FPB23array__growth__capacity(_M0L8old__capS469, _M0L3lenS1362, _M0L8requiredS472);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS470, _M0L8new__capS471);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS444,
  int32_t _M0L13new__capacityS447
) {
  float* _M0L8old__bufS443;
  int32_t _M0L3lenS445;
  int32_t _M0L9copy__lenS446;
  float* _M0L8new__bufS448;
  float* _M0L6_2aoldS1896;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS443 = _M0L4selfS444->$0;
  _M0L3lenS445 = _M0L4selfS444->$1;
  if (_M0L3lenS445 < _M0L13new__capacityS447) {
    _M0L9copy__lenS446 = _M0L3lenS445;
  } else {
    _M0L9copy__lenS446 = _M0L13new__capacityS447;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS443);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS448
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS443, _M0L13new__capacityS447, _M0L9copy__lenS446, 0, 0);
  _M0L6_2aoldS1896 = _M0L4selfS444->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1896);
  _M0L4selfS444->$0 = _M0L8new__bufS448;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS450,
  int32_t _M0L13new__capacityS453
) {
  moonbit_string_t* _M0L8old__bufS449;
  int32_t _M0L3lenS451;
  int32_t _M0L9copy__lenS452;
  moonbit_string_t* _M0L8new__bufS454;
  moonbit_string_t* _M0L6_2aoldS1897;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS449 = _M0L4selfS450->$0;
  _M0L3lenS451 = _M0L4selfS450->$1;
  if (_M0L3lenS451 < _M0L13new__capacityS453) {
    _M0L9copy__lenS452 = _M0L3lenS451;
  } else {
    _M0L9copy__lenS452 = _M0L13new__capacityS453;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS449);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS454
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS449, _M0L13new__capacityS453, _M0L9copy__lenS452, 0, 0);
  _M0L6_2aoldS1897 = _M0L4selfS450->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1897);
  _M0L4selfS450->$0 = _M0L8new__bufS454;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS456,
  int32_t _M0L13new__capacityS459
) {
  struct _M0TUsiE** _M0L8old__bufS455;
  int32_t _M0L3lenS457;
  int32_t _M0L9copy__lenS458;
  struct _M0TUsiE** _M0L8new__bufS460;
  struct _M0TUsiE** _M0L6_2aoldS1898;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS455 = _M0L4selfS456->$0;
  _M0L3lenS457 = _M0L4selfS456->$1;
  if (_M0L3lenS457 < _M0L13new__capacityS459) {
    _M0L9copy__lenS458 = _M0L3lenS457;
  } else {
    _M0L9copy__lenS458 = _M0L13new__capacityS459;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS455);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS460
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS455, _M0L13new__capacityS459, _M0L9copy__lenS458, 0, 0);
  _M0L6_2aoldS1898 = _M0L4selfS456->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1898);
  _M0L4selfS456->$0 = _M0L8new__bufS460;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS440
) {
  float* _M0L6_2atmpS1357;
  int32_t _result_1973;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1357 = _M0MPC15array5Array6bufferGfE(_M0L4selfS440);
  _result_1973 = Moonbit_array_length(_M0L6_2atmpS1357);
  moonbit_decref_cycle_free(_M0L6_2atmpS1357);
  return _result_1973;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS441
) {
  moonbit_string_t* _M0L6_2atmpS1358;
  int32_t _result_1974;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1358 = _M0MPC15array5Array6bufferGsE(_M0L4selfS441);
  _result_1974 = Moonbit_array_length(_M0L6_2atmpS1358);
  moonbit_decref_cycle_free(_M0L6_2atmpS1358);
  return _result_1974;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS442
) {
  struct _M0TUsiE** _M0L6_2atmpS1359;
  int32_t _result_1975;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1359 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS442);
  _result_1975 = Moonbit_array_length(_M0L6_2atmpS1359);
  moonbit_decref_cycle_free(_M0L6_2atmpS1359);
  return _result_1975;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS436,
  int32_t _M0L3lenS434,
  int32_t _M0L8requiredS433
) {
  int32_t _M0L5startS435;
  int32_t _M0L5spaceS437;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS433 < _M0L3lenS434) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_21.data);
  }
  if (_M0L7currentS436 == 0) {
    _M0L5startS435 = 8;
  } else {
    _M0L5startS435 = _M0L7currentS436;
  }
  _M0L5spaceS437 = _M0L5startS435;
  while (1) {
    if (_M0L5spaceS437 < _M0L8requiredS433) {
      int32_t _M0L4nextS438 = _M0L5spaceS437 * 2;
      if (_M0L4nextS438 <= _M0L5spaceS437) {
        return _M0L8requiredS433;
      }
      _M0L5spaceS437 = _M0L4nextS438;
      continue;
    } else {
      return _M0L5spaceS437;
    }
    break;
  }
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS430) {
  float* _M0L8_2afieldS1899;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1899 = _M0L4selfS430->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1899);
  return _M0L8_2afieldS1899;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS431
) {
  moonbit_string_t* _M0L8_2afieldS1900;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1900 = _M0L4selfS431->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1900);
  return _M0L8_2afieldS1900;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS432
) {
  struct _M0TUsiE** _M0L8_2afieldS1901;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1901 = _M0L4selfS432->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1901);
  return _M0L8_2afieldS1901;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS429
) {
  #line 220 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref_cycle_free(_M0L4selfS429);
  return _M0L4selfS429;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS428,
  struct _M0TPC16string10StringView _M0L3strS426
) {
  int32_t _M0L3endS1355;
  int32_t _M0L5startS1356;
  int32_t _M0L8str__lenS425;
  int32_t _M0L3lenS1354;
  int32_t _M0L8requiredS427;
  uint16_t* _M0L4dataS1347;
  int32_t _M0L6_2atmpS1346;
  int32_t _if__result_1977;
  uint16_t* _M0L4dataS1348;
  int32_t _M0L3lenS1349;
  moonbit_string_t _M0L6_2atmpS1350;
  int32_t _M0L6_2atmpS1351;
  int32_t _M0L3lenS1353;
  int32_t _M0L6_2atmpS1352;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1355 = _M0L3strS426.$2;
  _M0L5startS1356 = _M0L3strS426.$1;
  _M0L8str__lenS425 = _M0L3endS1355 - _M0L5startS1356;
  if (_M0L8str__lenS425 == 0) {
    return 0;
  }
  _M0L3lenS1354 = _M0L4selfS428->$1;
  _M0L8requiredS427 = _M0L3lenS1354 + _M0L8str__lenS425;
  _M0L4dataS1347 = _M0L4selfS428->$0;
  _M0L6_2atmpS1346 = Moonbit_array_length(_M0L4dataS1347);
  if (_M0L8requiredS427 > _M0L6_2atmpS1346) {
    _if__result_1977 = 1;
  } else {
    int32_t _M0L3lenS1345 = _M0L4selfS428->$1;
    _if__result_1977 = _M0L8requiredS427 < _M0L3lenS1345;
  }
  if (_if__result_1977) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS428, _M0L8requiredS427);
  }
  _M0L4dataS1348 = _M0L4selfS428->$0;
  _M0L3lenS1349 = _M0L4selfS428->$1;
  moonbit_incref_cycle_free(_M0L4dataS1348);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1350 = _M0MPC16string10StringView4data(_M0L3strS426);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1351 = _M0MPC16string10StringView13start__offset(_M0L3strS426);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1348, _M0L3lenS1349, _M0L6_2atmpS1350, _M0L6_2atmpS1351, _M0L8str__lenS425);
  moonbit_decref_cycle_free(_M0L4dataS1348);
  moonbit_decref_cycle_free(_M0L6_2atmpS1350);
  _M0L3lenS1353 = _M0L4selfS428->$1;
  _M0L6_2atmpS1352 = _M0L3lenS1353 + _M0L8str__lenS425;
  _M0L4selfS428->$1 = _M0L6_2atmpS1352;
  return 0;
}

moonbit_string_t _M0MPC16string6String4make(
  int32_t _M0L6lengthS420,
  int32_t _M0L5valueS421
) {
  #line 26 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L6lengthS420 >= 0) {
    int32_t _M0L6_2atmpS1342 = _M0L5valueS421;
    if (_M0L6_2atmpS1342 <= 65535) {
      #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
      return _M0FPB20unsafe__make__string(_M0L6lengthS420, _M0L5valueS421);
    } else {
      int32_t _M0L6_2atmpS1344 = 2 * _M0L6lengthS420;
      struct _M0TPB13StringBuilder* _M0L3bufS422;
      int32_t _M0L2__S423;
      moonbit_string_t _result_1979;
      #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
      _M0L3bufS422
      = _M0MPB13StringBuilder21StringBuilder_2einner(_M0L6_2atmpS1344);
      _M0L2__S423 = 0;
      while (1) {
        if (_M0L2__S423 < _M0L6lengthS420) {
          int32_t _M0L6_2atmpS1343;
          #line 33 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
          _M0IPB13StringBuilderPB6Logger11write__char(_M0L3bufS422, _M0L5valueS421);
          _M0L6_2atmpS1343 = _M0L2__S423 + 1;
          _M0L2__S423 = _M0L6_2atmpS1343;
          continue;
        }
        break;
      }
      #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
      _result_1979 = _M0MPB13StringBuilder10to__string(_M0L3bufS422);
      moonbit_decref_cycle_free(_M0L3bufS422);
      return _result_1979;
    }
  } else {
    #line 27 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
    return _M0FPC15abort5abortGsE((moonbit_string_t)moonbit_string_literal_22.data);
  }
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS417,
  int32_t _M0L5startS415,
  int32_t _M0L3endS416
) {
  int32_t _if__result_1980;
  int32_t _M0L3lenS418;
  int32_t _M0L6_2atmpS1341;
  moonbit_bytes_t _M0L5bytesS419;
  moonbit_bytes_t _M0L6_2atmpS1340;
  moonbit_string_t _result_1981;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS415 == 0) {
    int32_t _M0L6_2atmpS1339 = Moonbit_array_length(_M0L3strS417);
    _if__result_1980 = _M0L3endS416 == _M0L6_2atmpS1339;
  } else {
    _if__result_1980 = 0;
  }
  if (_if__result_1980) {
    moonbit_incref_cycle_free(_M0L3strS417);
    return _M0L3strS417;
  }
  _M0L3lenS418 = _M0L3endS416 - _M0L5startS415;
  _M0L6_2atmpS1341 = _M0L3lenS418 * 2;
  _M0L5bytesS419 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1341, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS419, 0, _M0L3strS417, _M0L5startS415, _M0L3lenS418);
  _M0L6_2atmpS1340 = _M0L5bytesS419;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_1981
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1340, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1340);
  return _result_1981;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS410,
  int32_t _M0L6offsetS414,
  int64_t _M0L6lengthS412
) {
  int32_t _M0L3lenS409;
  int32_t _M0L6lengthS411;
  int32_t _if__result_1982;
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L3lenS409 = Moonbit_array_length(_M0L4selfS410);
  if (_M0L6lengthS412 == 4294967296ll) {
    _M0L6lengthS411 = _M0L3lenS409 - _M0L6offsetS414;
  } else {
    int64_t _M0L7_2aSomeS413 = _M0L6lengthS412;
    _M0L6lengthS411 = (int32_t)_M0L7_2aSomeS413;
  }
  if (_M0L6offsetS414 >= 0) {
    if (_M0L6lengthS411 >= 0) {
      int32_t _M0L6_2atmpS1338 = _M0L6offsetS414 + _M0L6lengthS411;
      _if__result_1982 = _M0L6_2atmpS1338 <= _M0L3lenS409;
    } else {
      _if__result_1982 = 0;
    }
  } else {
    _if__result_1982 = 0;
  }
  if (_if__result_1982) {
    moonbit_incref_cycle_free(_M0L4selfS410);
    #line 85 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    return _M0FPB19unsafe__sub__string(_M0L4selfS410, _M0L6offsetS414, _M0L6lengthS411);
  } else {
    #line 84 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array10FixedArray18blit__from__string(
  moonbit_bytes_t _M0L4selfS401,
  int32_t _M0L13bytes__offsetS396,
  moonbit_string_t _M0L3strS403,
  int32_t _M0L11str__offsetS399,
  int32_t _M0L6lengthS397
) {
  int32_t _M0L6_2atmpS1337;
  int32_t _M0L6_2atmpS1336;
  int32_t _M0L2e1S395;
  int32_t _M0L6_2atmpS1335;
  int32_t _M0L2e2S398;
  int32_t _M0L4len1S400;
  int32_t _M0L4len2S402;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1337 = _M0L6lengthS397 * 2;
  _M0L6_2atmpS1336 = _M0L13bytes__offsetS396 + _M0L6_2atmpS1337;
  _M0L2e1S395 = _M0L6_2atmpS1336 - 1;
  _M0L6_2atmpS1335 = _M0L11str__offsetS399 + _M0L6lengthS397;
  _M0L2e2S398 = _M0L6_2atmpS1335 - 1;
  _M0L4len1S400 = Moonbit_array_length(_M0L4selfS401);
  _M0L4len2S402 = Moonbit_array_length(_M0L3strS403);
  if (
    _M0L6lengthS397 >= 0
    && _M0L13bytes__offsetS396 >= 0
    && _M0L2e1S395 < _M0L4len1S400
    && _M0L11str__offsetS399 >= 0
    && _M0L2e2S398 < _M0L4len2S402
  ) {
    int32_t _M0L16end__str__offsetS404 =
      _M0L11str__offsetS399 + _M0L6lengthS397;
    int32_t _M0L1iS405 = _M0L11str__offsetS399;
    int32_t _M0L1jS406 = _M0L13bytes__offsetS396;
    while (1) {
      if (_M0L1iS405 < _M0L16end__str__offsetS404) {
        int32_t _M0L6_2atmpS1332 = _M0L3strS403[_M0L1iS405];
        int32_t _M0L6_2atmpS1331 = (int32_t)_M0L6_2atmpS1332;
        uint32_t _M0L1cS407 = *(uint32_t*)&_M0L6_2atmpS1331;
        uint32_t _M0L6_2atmpS1327 = _M0L1cS407 & 255u;
        int32_t _M0L6_2atmpS1326;
        int32_t _M0L6_2atmpS1328;
        uint32_t _M0L6_2atmpS1330;
        int32_t _M0L6_2atmpS1329;
        int32_t _M0L6_2atmpS1333;
        int32_t _M0L6_2atmpS1334;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1326 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1327);
        if (
          _M0L1jS406 < 0 || _M0L1jS406 >= Moonbit_array_length(_M0L4selfS401)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS401[_M0L1jS406] = _M0L6_2atmpS1326;
        _M0L6_2atmpS1328 = _M0L1jS406 + 1;
        _M0L6_2atmpS1330 = _M0L1cS407 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1329 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1330);
        if (
          _M0L6_2atmpS1328 < 0
          || _M0L6_2atmpS1328 >= Moonbit_array_length(_M0L4selfS401)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS401[_M0L6_2atmpS1328] = _M0L6_2atmpS1329;
        _M0L6_2atmpS1333 = _M0L1iS405 + 1;
        _M0L6_2atmpS1334 = _M0L1jS406 + 2;
        _M0L1iS405 = _M0L6_2atmpS1333;
        _M0L1jS406 = _M0L6_2atmpS1334;
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

int32_t _M0MPC14uint4UInt8to__byte(uint32_t _M0L4selfS394) {
  int32_t _M0L6_2atmpS1325;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1325 = *(int32_t*)&_M0L4selfS394;
  return _M0L6_2atmpS1325 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS386,
  int32_t _M0L5radixS385
) {
  uint16_t* _M0L6bufferS387;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS385 < 2 || _M0L5radixS385 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_23.data);
  }
  if (_M0L4selfS386 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_15.data;
  }
  switch (_M0L5radixS385) {
    case 10: {
      int32_t _M0L3lenS388;
      uint16_t* _M0L6bufferS389;
      #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS388 = _M0FPB12dec__count64(_M0L4selfS386);
      _M0L6bufferS389 = (uint16_t*)moonbit_make_string(_M0L3lenS388, 0);
      #line 624 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS389, _M0L4selfS386, 0, _M0L3lenS388);
      _M0L6bufferS387 = _M0L6bufferS389;
      break;
    }
    
    case 16: {
      int32_t _M0L3lenS390;
      uint16_t* _M0L6bufferS391;
      #line 628 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS390 = _M0FPB12hex__count64(_M0L4selfS386);
      _M0L6bufferS391 = (uint16_t*)moonbit_make_string(_M0L3lenS390, 0);
      #line 630 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS391, _M0L4selfS386, 0, _M0L3lenS390);
      _M0L6bufferS387 = _M0L6bufferS391;
      break;
    }
    default: {
      int32_t _M0L3lenS392;
      uint16_t* _M0L6bufferS393;
      #line 634 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L3lenS392 = _M0FPB14radix__count64(_M0L4selfS386, _M0L5radixS385);
      _M0L6bufferS393 = (uint16_t*)moonbit_make_string(_M0L3lenS392, 0);
      #line 636 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS393, _M0L4selfS386, 0, _M0L3lenS392, _M0L5radixS385);
      _M0L6bufferS387 = _M0L6bufferS393;
      break;
    }
  }
  return _M0L6bufferS387;
}

moonbit_string_t _M0MPC15int645Int6418to__string_2einner(
  int64_t _M0L4selfS369,
  int32_t _M0L5radixS368
) {
  int32_t _M0L12is__negativeS370;
  uint64_t _M0L3numS371;
  uint16_t* _M0L6bufferS372;
  #line 548 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS368 < 2 || _M0L5radixS368 > 36) {
    #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_23.data);
  }
  if (_M0L4selfS369 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_15.data;
  }
  _M0L12is__negativeS370 = _M0L4selfS369 < 0ll;
  if (_M0L12is__negativeS370) {
    int64_t _M0L6_2atmpS1324 = -_M0L4selfS369;
    _M0L3numS371 = *(uint64_t*)&_M0L6_2atmpS1324;
  } else {
    _M0L3numS371 = *(uint64_t*)&_M0L4selfS369;
  }
  switch (_M0L5radixS368) {
    case 10: {
      int32_t _M0L10digit__lenS373;
      int32_t _M0L6_2atmpS1321;
      int32_t _M0L10total__lenS374;
      uint16_t* _M0L6bufferS375;
      int32_t _M0L12digit__startS376;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS373 = _M0FPB12dec__count64(_M0L3numS371);
      if (_M0L12is__negativeS370) {
        _M0L6_2atmpS1321 = 1;
      } else {
        _M0L6_2atmpS1321 = 0;
      }
      _M0L10total__lenS374 = _M0L10digit__lenS373 + _M0L6_2atmpS1321;
      _M0L6bufferS375
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS374, 0);
      if (_M0L12is__negativeS370) {
        _M0L12digit__startS376 = 1;
      } else {
        _M0L12digit__startS376 = 0;
      }
      #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__dec(_M0L6bufferS375, _M0L3numS371, _M0L12digit__startS376, _M0L10total__lenS374);
      _M0L6bufferS372 = _M0L6bufferS375;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS377;
      int32_t _M0L6_2atmpS1322;
      int32_t _M0L10total__lenS378;
      uint16_t* _M0L6bufferS379;
      int32_t _M0L12digit__startS380;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS377 = _M0FPB12hex__count64(_M0L3numS371);
      if (_M0L12is__negativeS370) {
        _M0L6_2atmpS1322 = 1;
      } else {
        _M0L6_2atmpS1322 = 0;
      }
      _M0L10total__lenS378 = _M0L10digit__lenS377 + _M0L6_2atmpS1322;
      _M0L6bufferS379
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS378, 0);
      if (_M0L12is__negativeS370) {
        _M0L12digit__startS380 = 1;
      } else {
        _M0L12digit__startS380 = 0;
      }
      #line 585 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB22int64__to__string__hex(_M0L6bufferS379, _M0L3numS371, _M0L12digit__startS380, _M0L10total__lenS378);
      _M0L6bufferS372 = _M0L6bufferS379;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS381;
      int32_t _M0L6_2atmpS1323;
      int32_t _M0L10total__lenS382;
      uint16_t* _M0L6bufferS383;
      int32_t _M0L12digit__startS384;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS381
      = _M0FPB14radix__count64(_M0L3numS371, _M0L5radixS368);
      if (_M0L12is__negativeS370) {
        _M0L6_2atmpS1323 = 1;
      } else {
        _M0L6_2atmpS1323 = 0;
      }
      _M0L10total__lenS382 = _M0L10digit__lenS381 + _M0L6_2atmpS1323;
      _M0L6bufferS383
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS382, 0);
      if (_M0L12is__negativeS370) {
        _M0L12digit__startS384 = 1;
      } else {
        _M0L12digit__startS384 = 0;
      }
      #line 593 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB26int64__to__string__generic(_M0L6bufferS383, _M0L3numS371, _M0L12digit__startS384, _M0L10total__lenS382, _M0L5radixS368);
      _M0L6bufferS372 = _M0L6bufferS383;
      break;
    }
  }
  if (_M0L12is__negativeS370) {
    _M0L6bufferS372[0] = 45;
  }
  return _M0L6bufferS372;
}

int32_t _M0FPB22int64__to__string__dec(
  uint16_t* _M0L6bufferS354,
  uint64_t _M0L3numS366,
  int32_t _M0L12digit__startS355,
  int32_t _M0L10total__lenS367
) {
  int32_t _M0L6_2atmpS1320;
  uint64_t _M0L3numS344;
  int32_t _M0L6offsetS345;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1320 = _M0L10total__lenS367 - _M0L12digit__startS355;
  _M0L3numS344 = _M0L3numS366;
  _M0L6offsetS345 = _M0L6_2atmpS1320;
  while (1) {
    if (_M0L3numS344 >= 10000ull) {
      uint64_t _M0L1tS346 = _M0L3numS344 / 10000ull;
      uint64_t _M0L6_2atmpS1297 = _M0L3numS344 % 10000ull;
      int32_t _M0L1rS347 = (int32_t)_M0L6_2atmpS1297;
      int32_t _M0L2d1S348 = _M0L1rS347 / 100;
      int32_t _M0L2d2S349 = _M0L1rS347 % 100;
      int32_t _M0L6_2atmpS1296 = _M0L2d1S348 / 10;
      int32_t _M0L6_2atmpS1295 = 48 + _M0L6_2atmpS1296;
      int32_t _M0L6d1__hiS350 = (uint16_t)_M0L6_2atmpS1295;
      int32_t _M0L6_2atmpS1294 = _M0L2d1S348 % 10;
      int32_t _M0L6_2atmpS1293 = 48 + _M0L6_2atmpS1294;
      int32_t _M0L6d1__loS351 = (uint16_t)_M0L6_2atmpS1293;
      int32_t _M0L6_2atmpS1292 = _M0L2d2S349 / 10;
      int32_t _M0L6_2atmpS1291 = 48 + _M0L6_2atmpS1292;
      int32_t _M0L6d2__hiS352 = (uint16_t)_M0L6_2atmpS1291;
      int32_t _M0L6_2atmpS1290 = _M0L2d2S349 % 10;
      int32_t _M0L6_2atmpS1289 = 48 + _M0L6_2atmpS1290;
      int32_t _M0L6d2__loS353 = (uint16_t)_M0L6_2atmpS1289;
      int32_t _M0L6_2atmpS1281 = _M0L12digit__startS355 + _M0L6offsetS345;
      int32_t _M0L6_2atmpS1280 = _M0L6_2atmpS1281 - 4;
      int32_t _M0L6_2atmpS1283;
      int32_t _M0L6_2atmpS1282;
      int32_t _M0L6_2atmpS1285;
      int32_t _M0L6_2atmpS1284;
      int32_t _M0L6_2atmpS1287;
      int32_t _M0L6_2atmpS1286;
      int32_t _M0L6_2atmpS1288;
      _M0L6bufferS354[_M0L6_2atmpS1280] = _M0L6d1__hiS350;
      _M0L6_2atmpS1283 = _M0L12digit__startS355 + _M0L6offsetS345;
      _M0L6_2atmpS1282 = _M0L6_2atmpS1283 - 3;
      _M0L6bufferS354[_M0L6_2atmpS1282] = _M0L6d1__loS351;
      _M0L6_2atmpS1285 = _M0L12digit__startS355 + _M0L6offsetS345;
      _M0L6_2atmpS1284 = _M0L6_2atmpS1285 - 2;
      _M0L6bufferS354[_M0L6_2atmpS1284] = _M0L6d2__hiS352;
      _M0L6_2atmpS1287 = _M0L12digit__startS355 + _M0L6offsetS345;
      _M0L6_2atmpS1286 = _M0L6_2atmpS1287 - 1;
      _M0L6bufferS354[_M0L6_2atmpS1286] = _M0L6d2__loS353;
      _M0L6_2atmpS1288 = _M0L6offsetS345 - 4;
      _M0L3numS344 = _M0L1tS346;
      _M0L6offsetS345 = _M0L6_2atmpS1288;
      continue;
    } else {
      int32_t _M0L6_2atmpS1319 = (int32_t)_M0L3numS344;
      int32_t _M0L9remainingS357 = _M0L6_2atmpS1319;
      int32_t _M0L6offsetS358 = _M0L6offsetS345;
      while (1) {
        if (_M0L9remainingS357 >= 100) {
          int32_t _M0L1tS359 = _M0L9remainingS357 / 100;
          int32_t _M0L1dS360 = _M0L9remainingS357 % 100;
          int32_t _M0L6_2atmpS1306 = _M0L1dS360 / 10;
          int32_t _M0L6_2atmpS1305 = 48 + _M0L6_2atmpS1306;
          int32_t _M0L5d__hiS361 = (uint16_t)_M0L6_2atmpS1305;
          int32_t _M0L6_2atmpS1304 = _M0L1dS360 % 10;
          int32_t _M0L6_2atmpS1303 = 48 + _M0L6_2atmpS1304;
          int32_t _M0L5d__loS362 = (uint16_t)_M0L6_2atmpS1303;
          int32_t _M0L6_2atmpS1299 = _M0L12digit__startS355 + _M0L6offsetS358;
          int32_t _M0L6_2atmpS1298 = _M0L6_2atmpS1299 - 2;
          int32_t _M0L6_2atmpS1301;
          int32_t _M0L6_2atmpS1300;
          int32_t _M0L6_2atmpS1302;
          _M0L6bufferS354[_M0L6_2atmpS1298] = _M0L5d__hiS361;
          _M0L6_2atmpS1301 = _M0L12digit__startS355 + _M0L6offsetS358;
          _M0L6_2atmpS1300 = _M0L6_2atmpS1301 - 1;
          _M0L6bufferS354[_M0L6_2atmpS1300] = _M0L5d__loS362;
          _M0L6_2atmpS1302 = _M0L6offsetS358 - 2;
          _M0L9remainingS357 = _M0L1tS359;
          _M0L6offsetS358 = _M0L6_2atmpS1302;
          continue;
        } else if (_M0L9remainingS357 >= 10) {
          int32_t _M0L6_2atmpS1314 = _M0L9remainingS357 / 10;
          int32_t _M0L6_2atmpS1313 = 48 + _M0L6_2atmpS1314;
          int32_t _M0L5d__hiS364 = (uint16_t)_M0L6_2atmpS1313;
          int32_t _M0L6_2atmpS1312 = _M0L9remainingS357 % 10;
          int32_t _M0L6_2atmpS1311 = 48 + _M0L6_2atmpS1312;
          int32_t _M0L5d__loS365 = (uint16_t)_M0L6_2atmpS1311;
          int32_t _M0L6_2atmpS1308 = _M0L12digit__startS355 + _M0L6offsetS358;
          int32_t _M0L6_2atmpS1307 = _M0L6_2atmpS1308 - 2;
          int32_t _M0L6_2atmpS1310;
          int32_t _M0L6_2atmpS1309;
          _M0L6bufferS354[_M0L6_2atmpS1307] = _M0L5d__hiS364;
          _M0L6_2atmpS1310 = _M0L12digit__startS355 + _M0L6offsetS358;
          _M0L6_2atmpS1309 = _M0L6_2atmpS1310 - 1;
          _M0L6bufferS354[_M0L6_2atmpS1309] = _M0L5d__loS365;
        } else {
          int32_t _M0L6_2atmpS1318 = _M0L12digit__startS355 + _M0L6offsetS358;
          int32_t _M0L6_2atmpS1315 = _M0L6_2atmpS1318 - 1;
          int32_t _M0L6_2atmpS1317 = 48 + _M0L9remainingS357;
          int32_t _M0L6_2atmpS1316 = (uint16_t)_M0L6_2atmpS1317;
          _M0L6bufferS354[_M0L6_2atmpS1315] = _M0L6_2atmpS1316;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB26int64__to__string__generic(
  uint16_t* _M0L6bufferS334,
  uint64_t _M0L3numS338,
  int32_t _M0L12digit__startS335,
  int32_t _M0L10total__lenS337,
  int32_t _M0L5radixS328
) {
  uint64_t _M0L4baseS327;
  int32_t _M0L6_2atmpS1265;
  int32_t _M0L6_2atmpS1264;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS327 = _M0MPC13int3Int10to__uint64(_M0L5radixS328);
  _M0L6_2atmpS1265 = _M0L5radixS328 - 1;
  _M0L6_2atmpS1264 = _M0L5radixS328 & _M0L6_2atmpS1265;
  if (_M0L6_2atmpS1264 == 0) {
    int32_t _M0L5shiftS329;
    uint64_t _M0L4maskS330;
    int32_t _M0L6_2atmpS1272;
    int32_t _M0L6offsetS331;
    uint64_t _M0L1nS332;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS329 = moonbit_ctz32(_M0L5radixS328);
    _M0L4maskS330 = _M0L4baseS327 - 1ull;
    _M0L6_2atmpS1272 = _M0L10total__lenS337 - _M0L12digit__startS335;
    _M0L6offsetS331 = _M0L6_2atmpS1272;
    _M0L1nS332 = _M0L3numS338;
    while (1) {
      if (_M0L1nS332 > 0ull) {
        uint64_t _M0L6_2atmpS1271 = _M0L1nS332 & _M0L4maskS330;
        int32_t _M0L5digitS333 = (int32_t)_M0L6_2atmpS1271;
        int32_t _M0L6_2atmpS1268 = _M0L12digit__startS335 + _M0L6offsetS331;
        int32_t _M0L6_2atmpS1266 = _M0L6_2atmpS1268 - 1;
        int32_t _M0L6_2atmpS1267 =
          ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L5digitS333];
        int32_t _M0L6_2atmpS1269;
        uint64_t _M0L6_2atmpS1270;
        _M0L6bufferS334[_M0L6_2atmpS1266] = _M0L6_2atmpS1267;
        _M0L6_2atmpS1269 = _M0L6offsetS331 - 1;
        _M0L6_2atmpS1270 = _M0L1nS332 >> (_M0L5shiftS329 & 63);
        _M0L6offsetS331 = _M0L6_2atmpS1269;
        _M0L1nS332 = _M0L6_2atmpS1270;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1279 = _M0L10total__lenS337 - _M0L12digit__startS335;
    int32_t _M0L6offsetS339 = _M0L6_2atmpS1279;
    uint64_t _M0L1nS340 = _M0L3numS338;
    while (1) {
      if (_M0L1nS340 > 0ull) {
        uint64_t _M0L1qS341 = _M0L1nS340 / _M0L4baseS327;
        uint64_t _M0L6_2atmpS1278 = _M0L1qS341 * _M0L4baseS327;
        uint64_t _M0L6_2atmpS1277 = _M0L1nS340 - _M0L6_2atmpS1278;
        int32_t _M0L5digitS342 = (int32_t)_M0L6_2atmpS1277;
        int32_t _M0L6_2atmpS1275 = _M0L12digit__startS335 + _M0L6offsetS339;
        int32_t _M0L6_2atmpS1273 = _M0L6_2atmpS1275 - 1;
        int32_t _M0L6_2atmpS1274 =
          ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L5digitS342];
        int32_t _M0L6_2atmpS1276;
        _M0L6bufferS334[_M0L6_2atmpS1273] = _M0L6_2atmpS1274;
        _M0L6_2atmpS1276 = _M0L6offsetS339 - 1;
        _M0L6offsetS339 = _M0L6_2atmpS1276;
        _M0L1nS340 = _M0L1qS341;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB22int64__to__string__hex(
  uint16_t* _M0L6bufferS321,
  uint64_t _M0L3numS326,
  int32_t _M0L12digit__startS322,
  int32_t _M0L10total__lenS325
) {
  int32_t _M0L6_2atmpS1263;
  int32_t _M0L6offsetS316;
  uint64_t _M0L1nS317;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1263 = _M0L10total__lenS325 - _M0L12digit__startS322;
  _M0L6offsetS316 = _M0L6_2atmpS1263;
  _M0L1nS317 = _M0L3numS326;
  while (1) {
    if (_M0L6offsetS316 >= 2) {
      uint64_t _M0L6_2atmpS1260 = _M0L1nS317 & 255ull;
      int32_t _M0L9byte__valS318 = (int32_t)_M0L6_2atmpS1260;
      int32_t _M0L2hiS319 = _M0L9byte__valS318 / 16;
      int32_t _M0L2loS320 = _M0L9byte__valS318 % 16;
      int32_t _M0L6_2atmpS1254 = _M0L12digit__startS322 + _M0L6offsetS316;
      int32_t _M0L6_2atmpS1252 = _M0L6_2atmpS1254 - 2;
      int32_t _M0L6_2atmpS1253 =
        ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L2hiS319];
      int32_t _M0L6_2atmpS1257;
      int32_t _M0L6_2atmpS1255;
      int32_t _M0L6_2atmpS1256;
      int32_t _M0L6_2atmpS1258;
      uint64_t _M0L6_2atmpS1259;
      _M0L6bufferS321[_M0L6_2atmpS1252] = _M0L6_2atmpS1253;
      _M0L6_2atmpS1257 = _M0L12digit__startS322 + _M0L6offsetS316;
      _M0L6_2atmpS1255 = _M0L6_2atmpS1257 - 1;
      _M0L6_2atmpS1256
      = ((moonbit_string_t)moonbit_string_literal_24.data)[
        _M0L2loS320
      ];
      _M0L6bufferS321[_M0L6_2atmpS1255] = _M0L6_2atmpS1256;
      _M0L6_2atmpS1258 = _M0L6offsetS316 - 2;
      _M0L6_2atmpS1259 = _M0L1nS317 >> 8;
      _M0L6offsetS316 = _M0L6_2atmpS1258;
      _M0L1nS317 = _M0L6_2atmpS1259;
      continue;
    } else if (_M0L6offsetS316 == 1) {
      uint64_t _M0L6_2atmpS1262 = _M0L1nS317 & 15ull;
      int32_t _M0L6nibbleS324 = (int32_t)_M0L6_2atmpS1262;
      int32_t _M0L6_2atmpS1261 =
        ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L6nibbleS324];
      _M0L6bufferS321[_M0L12digit__startS322] = _M0L6_2atmpS1261;
    }
    break;
  }
  return 0;
}

int32_t _M0FPB14radix__count64(
  uint64_t _M0L5valueS310,
  int32_t _M0L5radixS312
) {
  uint64_t _M0L4baseS311;
  uint64_t _M0L3numS313;
  int32_t _M0L5countS314;
  #line 419 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS310 == 0ull) {
    return 1;
  }
  #line 424 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS311 = _M0MPC13int3Int10to__uint64(_M0L5radixS312);
  _M0L3numS313 = _M0L5valueS310;
  _M0L5countS314 = 0;
  while (1) {
    if (_M0L3numS313 > 0ull) {
      uint64_t _M0L6_2atmpS1250 = _M0L3numS313 / _M0L4baseS311;
      int32_t _M0L6_2atmpS1251 = _M0L5countS314 + 1;
      _M0L3numS313 = _M0L6_2atmpS1250;
      _M0L5countS314 = _M0L6_2atmpS1251;
      continue;
    } else {
      return _M0L5countS314;
    }
    break;
  }
}

int32_t _M0FPB12hex__count64(uint64_t _M0L5valueS308) {
  #line 407 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS308 == 0ull) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS309;
    int32_t _M0L6_2atmpS1249;
    int32_t _M0L6_2atmpS1248;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS309 = moonbit_clz64(_M0L5valueS308);
    _M0L6_2atmpS1249 = 63 - _M0L14leading__zerosS309;
    _M0L6_2atmpS1248 = _M0L6_2atmpS1249 / 4;
    return _M0L6_2atmpS1248 + 1;
  }
}

int32_t _M0FPB12dec__count64(uint64_t _M0L5valueS307) {
  #line 343 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS307 >= 10000000000ull) {
    if (_M0L5valueS307 >= 100000000000000ull) {
      if (_M0L5valueS307 >= 10000000000000000ull) {
        if (_M0L5valueS307 >= 1000000000000000000ull) {
          if (_M0L5valueS307 >= 10000000000000000000ull) {
            return 20;
          } else {
            return 19;
          }
        } else if (_M0L5valueS307 >= 100000000000000000ull) {
          return 18;
        } else {
          return 17;
        }
      } else if (_M0L5valueS307 >= 1000000000000000ull) {
        return 16;
      } else {
        return 15;
      }
    } else if (_M0L5valueS307 >= 1000000000000ull) {
      if (_M0L5valueS307 >= 10000000000000ull) {
        return 14;
      } else {
        return 13;
      }
    } else if (_M0L5valueS307 >= 100000000000ull) {
      return 12;
    } else {
      return 11;
    }
  } else if (_M0L5valueS307 >= 100000ull) {
    if (_M0L5valueS307 >= 10000000ull) {
      if (_M0L5valueS307 >= 1000000000ull) {
        return 10;
      } else if (_M0L5valueS307 >= 100000000ull) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS307 >= 1000000ull) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS307 >= 1000ull) {
    if (_M0L5valueS307 >= 10000ull) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS307 >= 100ull) {
    return 3;
  } else if (_M0L5valueS307 >= 10ull) {
    return 2;
  } else {
    return 1;
  }
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS291,
  int32_t _M0L5radixS290
) {
  int32_t _M0L12is__negativeS292;
  uint32_t _M0L3numS293;
  uint16_t* _M0L6bufferS294;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS290 < 2 || _M0L5radixS290 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_23.data);
  }
  if (_M0L4selfS291 == 0) {
    return (moonbit_string_t)moonbit_string_literal_15.data;
  }
  _M0L12is__negativeS292 = _M0L4selfS291 < 0;
  if (_M0L12is__negativeS292) {
    int32_t _M0L6_2atmpS1247 = -_M0L4selfS291;
    _M0L3numS293 = *(uint32_t*)&_M0L6_2atmpS1247;
  } else {
    _M0L3numS293 = *(uint32_t*)&_M0L4selfS291;
  }
  switch (_M0L5radixS290) {
    case 10: {
      int32_t _M0L10digit__lenS295;
      int32_t _M0L6_2atmpS1244;
      int32_t _M0L10total__lenS296;
      uint16_t* _M0L6bufferS297;
      int32_t _M0L12digit__startS298;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS295 = _M0FPB12dec__count32(_M0L3numS293);
      if (_M0L12is__negativeS292) {
        _M0L6_2atmpS1244 = 1;
      } else {
        _M0L6_2atmpS1244 = 0;
      }
      _M0L10total__lenS296 = _M0L10digit__lenS295 + _M0L6_2atmpS1244;
      _M0L6bufferS297
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS296, 0);
      if (_M0L12is__negativeS292) {
        _M0L12digit__startS298 = 1;
      } else {
        _M0L12digit__startS298 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS297, _M0L3numS293, _M0L12digit__startS298, _M0L10total__lenS296);
      _M0L6bufferS294 = _M0L6bufferS297;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS299;
      int32_t _M0L6_2atmpS1245;
      int32_t _M0L10total__lenS300;
      uint16_t* _M0L6bufferS301;
      int32_t _M0L12digit__startS302;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS299 = _M0FPB12hex__count32(_M0L3numS293);
      if (_M0L12is__negativeS292) {
        _M0L6_2atmpS1245 = 1;
      } else {
        _M0L6_2atmpS1245 = 0;
      }
      _M0L10total__lenS300 = _M0L10digit__lenS299 + _M0L6_2atmpS1245;
      _M0L6bufferS301
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS300, 0);
      if (_M0L12is__negativeS292) {
        _M0L12digit__startS302 = 1;
      } else {
        _M0L12digit__startS302 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS301, _M0L3numS293, _M0L12digit__startS302, _M0L10total__lenS300);
      _M0L6bufferS294 = _M0L6bufferS301;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS303;
      int32_t _M0L6_2atmpS1246;
      int32_t _M0L10total__lenS304;
      uint16_t* _M0L6bufferS305;
      int32_t _M0L12digit__startS306;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS303
      = _M0FPB14radix__count32(_M0L3numS293, _M0L5radixS290);
      if (_M0L12is__negativeS292) {
        _M0L6_2atmpS1246 = 1;
      } else {
        _M0L6_2atmpS1246 = 0;
      }
      _M0L10total__lenS304 = _M0L10digit__lenS303 + _M0L6_2atmpS1246;
      _M0L6bufferS305
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS304, 0);
      if (_M0L12is__negativeS292) {
        _M0L12digit__startS306 = 1;
      } else {
        _M0L12digit__startS306 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS305, _M0L3numS293, _M0L12digit__startS306, _M0L10total__lenS304, _M0L5radixS290);
      _M0L6bufferS294 = _M0L6bufferS305;
      break;
    }
  }
  if (_M0L12is__negativeS292) {
    _M0L6bufferS294[0] = 45;
  }
  return _M0L6bufferS294;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS284,
  int32_t _M0L5radixS286
) {
  uint32_t _M0L4baseS285;
  uint32_t _M0L3numS287;
  int32_t _M0L5countS288;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS284 == 0u) {
    return 1;
  }
  _M0L4baseS285 = *(uint32_t*)&_M0L5radixS286;
  _M0L3numS287 = _M0L5valueS284;
  _M0L5countS288 = 0;
  while (1) {
    if (_M0L3numS287 > 0u) {
      uint32_t _M0L6_2atmpS1242 = _M0L3numS287 / _M0L4baseS285;
      int32_t _M0L6_2atmpS1243 = _M0L5countS288 + 1;
      _M0L3numS287 = _M0L6_2atmpS1242;
      _M0L5countS288 = _M0L6_2atmpS1243;
      continue;
    } else {
      return _M0L5countS288;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS282) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS282 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS283;
    int32_t _M0L6_2atmpS1241;
    int32_t _M0L6_2atmpS1240;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS283 = moonbit_clz32(_M0L5valueS282);
    _M0L6_2atmpS1241 = 31 - _M0L14leading__zerosS283;
    _M0L6_2atmpS1240 = _M0L6_2atmpS1241 / 4;
    return _M0L6_2atmpS1240 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS281) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS281 >= 100000u) {
    if (_M0L5valueS281 >= 10000000u) {
      if (_M0L5valueS281 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS281 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS281 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS281 >= 1000u) {
    if (_M0L5valueS281 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS281 >= 100u) {
    return 3;
  } else if (_M0L5valueS281 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS267,
  uint32_t _M0L3numS279,
  int32_t _M0L12digit__startS268,
  int32_t _M0L10total__lenS280
) {
  int32_t _M0L6_2atmpS1239;
  uint32_t _M0L3numS257;
  int32_t _M0L6offsetS258;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1239 = _M0L10total__lenS280 - _M0L12digit__startS268;
  _M0L3numS257 = _M0L3numS279;
  _M0L6offsetS258 = _M0L6_2atmpS1239;
  while (1) {
    if (_M0L3numS257 >= 10000u) {
      uint32_t _M0L1tS259 = _M0L3numS257 / 10000u;
      uint32_t _M0L6_2atmpS1216 = _M0L3numS257 % 10000u;
      int32_t _M0L1rS260 = *(int32_t*)&_M0L6_2atmpS1216;
      int32_t _M0L2d1S261 = _M0L1rS260 / 100;
      int32_t _M0L2d2S262 = _M0L1rS260 % 100;
      int32_t _M0L6_2atmpS1215 = _M0L2d1S261 / 10;
      int32_t _M0L6_2atmpS1214 = 48 + _M0L6_2atmpS1215;
      int32_t _M0L6d1__hiS263 = (uint16_t)_M0L6_2atmpS1214;
      int32_t _M0L6_2atmpS1213 = _M0L2d1S261 % 10;
      int32_t _M0L6_2atmpS1212 = 48 + _M0L6_2atmpS1213;
      int32_t _M0L6d1__loS264 = (uint16_t)_M0L6_2atmpS1212;
      int32_t _M0L6_2atmpS1211 = _M0L2d2S262 / 10;
      int32_t _M0L6_2atmpS1210 = 48 + _M0L6_2atmpS1211;
      int32_t _M0L6d2__hiS265 = (uint16_t)_M0L6_2atmpS1210;
      int32_t _M0L6_2atmpS1209 = _M0L2d2S262 % 10;
      int32_t _M0L6_2atmpS1208 = 48 + _M0L6_2atmpS1209;
      int32_t _M0L6d2__loS266 = (uint16_t)_M0L6_2atmpS1208;
      int32_t _M0L6_2atmpS1200 = _M0L12digit__startS268 + _M0L6offsetS258;
      int32_t _M0L6_2atmpS1199 = _M0L6_2atmpS1200 - 4;
      int32_t _M0L6_2atmpS1202;
      int32_t _M0L6_2atmpS1201;
      int32_t _M0L6_2atmpS1204;
      int32_t _M0L6_2atmpS1203;
      int32_t _M0L6_2atmpS1206;
      int32_t _M0L6_2atmpS1205;
      int32_t _M0L6_2atmpS1207;
      _M0L6bufferS267[_M0L6_2atmpS1199] = _M0L6d1__hiS263;
      _M0L6_2atmpS1202 = _M0L12digit__startS268 + _M0L6offsetS258;
      _M0L6_2atmpS1201 = _M0L6_2atmpS1202 - 3;
      _M0L6bufferS267[_M0L6_2atmpS1201] = _M0L6d1__loS264;
      _M0L6_2atmpS1204 = _M0L12digit__startS268 + _M0L6offsetS258;
      _M0L6_2atmpS1203 = _M0L6_2atmpS1204 - 2;
      _M0L6bufferS267[_M0L6_2atmpS1203] = _M0L6d2__hiS265;
      _M0L6_2atmpS1206 = _M0L12digit__startS268 + _M0L6offsetS258;
      _M0L6_2atmpS1205 = _M0L6_2atmpS1206 - 1;
      _M0L6bufferS267[_M0L6_2atmpS1205] = _M0L6d2__loS266;
      _M0L6_2atmpS1207 = _M0L6offsetS258 - 4;
      _M0L3numS257 = _M0L1tS259;
      _M0L6offsetS258 = _M0L6_2atmpS1207;
      continue;
    } else {
      int32_t _M0L6_2atmpS1238 = *(int32_t*)&_M0L3numS257;
      int32_t _M0L9remainingS270 = _M0L6_2atmpS1238;
      int32_t _M0L6offsetS271 = _M0L6offsetS258;
      while (1) {
        if (_M0L9remainingS270 >= 100) {
          int32_t _M0L1tS272 = _M0L9remainingS270 / 100;
          int32_t _M0L1dS273 = _M0L9remainingS270 % 100;
          int32_t _M0L6_2atmpS1225 = _M0L1dS273 / 10;
          int32_t _M0L6_2atmpS1224 = 48 + _M0L6_2atmpS1225;
          int32_t _M0L5d__hiS274 = (uint16_t)_M0L6_2atmpS1224;
          int32_t _M0L6_2atmpS1223 = _M0L1dS273 % 10;
          int32_t _M0L6_2atmpS1222 = 48 + _M0L6_2atmpS1223;
          int32_t _M0L5d__loS275 = (uint16_t)_M0L6_2atmpS1222;
          int32_t _M0L6_2atmpS1218 = _M0L12digit__startS268 + _M0L6offsetS271;
          int32_t _M0L6_2atmpS1217 = _M0L6_2atmpS1218 - 2;
          int32_t _M0L6_2atmpS1220;
          int32_t _M0L6_2atmpS1219;
          int32_t _M0L6_2atmpS1221;
          _M0L6bufferS267[_M0L6_2atmpS1217] = _M0L5d__hiS274;
          _M0L6_2atmpS1220 = _M0L12digit__startS268 + _M0L6offsetS271;
          _M0L6_2atmpS1219 = _M0L6_2atmpS1220 - 1;
          _M0L6bufferS267[_M0L6_2atmpS1219] = _M0L5d__loS275;
          _M0L6_2atmpS1221 = _M0L6offsetS271 - 2;
          _M0L9remainingS270 = _M0L1tS272;
          _M0L6offsetS271 = _M0L6_2atmpS1221;
          continue;
        } else if (_M0L9remainingS270 >= 10) {
          int32_t _M0L6_2atmpS1233 = _M0L9remainingS270 / 10;
          int32_t _M0L6_2atmpS1232 = 48 + _M0L6_2atmpS1233;
          int32_t _M0L5d__hiS277 = (uint16_t)_M0L6_2atmpS1232;
          int32_t _M0L6_2atmpS1231 = _M0L9remainingS270 % 10;
          int32_t _M0L6_2atmpS1230 = 48 + _M0L6_2atmpS1231;
          int32_t _M0L5d__loS278 = (uint16_t)_M0L6_2atmpS1230;
          int32_t _M0L6_2atmpS1227 = _M0L12digit__startS268 + _M0L6offsetS271;
          int32_t _M0L6_2atmpS1226 = _M0L6_2atmpS1227 - 2;
          int32_t _M0L6_2atmpS1229;
          int32_t _M0L6_2atmpS1228;
          _M0L6bufferS267[_M0L6_2atmpS1226] = _M0L5d__hiS277;
          _M0L6_2atmpS1229 = _M0L12digit__startS268 + _M0L6offsetS271;
          _M0L6_2atmpS1228 = _M0L6_2atmpS1229 - 1;
          _M0L6bufferS267[_M0L6_2atmpS1228] = _M0L5d__loS278;
        } else {
          int32_t _M0L6_2atmpS1237 = _M0L12digit__startS268 + _M0L6offsetS271;
          int32_t _M0L6_2atmpS1234 = _M0L6_2atmpS1237 - 1;
          int32_t _M0L6_2atmpS1236 = 48 + _M0L9remainingS270;
          int32_t _M0L6_2atmpS1235 = (uint16_t)_M0L6_2atmpS1236;
          _M0L6bufferS267[_M0L6_2atmpS1234] = _M0L6_2atmpS1235;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS247,
  uint32_t _M0L3numS251,
  int32_t _M0L12digit__startS248,
  int32_t _M0L10total__lenS250,
  int32_t _M0L5radixS241
) {
  uint32_t _M0L4baseS240;
  int32_t _M0L6_2atmpS1184;
  int32_t _M0L6_2atmpS1183;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS240 = *(uint32_t*)&_M0L5radixS241;
  _M0L6_2atmpS1184 = _M0L5radixS241 - 1;
  _M0L6_2atmpS1183 = _M0L5radixS241 & _M0L6_2atmpS1184;
  if (_M0L6_2atmpS1183 == 0) {
    int32_t _M0L5shiftS242;
    uint32_t _M0L4maskS243;
    int32_t _M0L6_2atmpS1191;
    int32_t _M0L6offsetS244;
    uint32_t _M0L1nS245;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS242 = moonbit_ctz32(_M0L5radixS241);
    _M0L4maskS243 = _M0L4baseS240 - 1u;
    _M0L6_2atmpS1191 = _M0L10total__lenS250 - _M0L12digit__startS248;
    _M0L6offsetS244 = _M0L6_2atmpS1191;
    _M0L1nS245 = _M0L3numS251;
    while (1) {
      if (_M0L1nS245 > 0u) {
        uint32_t _M0L6_2atmpS1190 = _M0L1nS245 & _M0L4maskS243;
        int32_t _M0L5digitS246 = *(int32_t*)&_M0L6_2atmpS1190;
        int32_t _M0L6_2atmpS1187 = _M0L12digit__startS248 + _M0L6offsetS244;
        int32_t _M0L6_2atmpS1185 = _M0L6_2atmpS1187 - 1;
        int32_t _M0L6_2atmpS1186 =
          ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L5digitS246];
        int32_t _M0L6_2atmpS1188;
        uint32_t _M0L6_2atmpS1189;
        _M0L6bufferS247[_M0L6_2atmpS1185] = _M0L6_2atmpS1186;
        _M0L6_2atmpS1188 = _M0L6offsetS244 - 1;
        _M0L6_2atmpS1189 = _M0L1nS245 >> (_M0L5shiftS242 & 31);
        _M0L6offsetS244 = _M0L6_2atmpS1188;
        _M0L1nS245 = _M0L6_2atmpS1189;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1198 = _M0L10total__lenS250 - _M0L12digit__startS248;
    int32_t _M0L6offsetS252 = _M0L6_2atmpS1198;
    uint32_t _M0L1nS253 = _M0L3numS251;
    while (1) {
      if (_M0L1nS253 > 0u) {
        uint32_t _M0L1qS254 = _M0L1nS253 / _M0L4baseS240;
        uint32_t _M0L6_2atmpS1197 = _M0L1qS254 * _M0L4baseS240;
        uint32_t _M0L6_2atmpS1196 = _M0L1nS253 - _M0L6_2atmpS1197;
        int32_t _M0L5digitS255 = *(int32_t*)&_M0L6_2atmpS1196;
        int32_t _M0L6_2atmpS1194 = _M0L12digit__startS248 + _M0L6offsetS252;
        int32_t _M0L6_2atmpS1192 = _M0L6_2atmpS1194 - 1;
        int32_t _M0L6_2atmpS1193 =
          ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L5digitS255];
        int32_t _M0L6_2atmpS1195;
        _M0L6bufferS247[_M0L6_2atmpS1192] = _M0L6_2atmpS1193;
        _M0L6_2atmpS1195 = _M0L6offsetS252 - 1;
        _M0L6offsetS252 = _M0L6_2atmpS1195;
        _M0L1nS253 = _M0L1qS254;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS234,
  uint32_t _M0L3numS239,
  int32_t _M0L12digit__startS235,
  int32_t _M0L10total__lenS238
) {
  int32_t _M0L6_2atmpS1182;
  int32_t _M0L6offsetS229;
  uint32_t _M0L1nS230;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1182 = _M0L10total__lenS238 - _M0L12digit__startS235;
  _M0L6offsetS229 = _M0L6_2atmpS1182;
  _M0L1nS230 = _M0L3numS239;
  while (1) {
    if (_M0L6offsetS229 >= 2) {
      uint32_t _M0L6_2atmpS1179 = _M0L1nS230 & 255u;
      int32_t _M0L9byte__valS231 = *(int32_t*)&_M0L6_2atmpS1179;
      int32_t _M0L2hiS232 = _M0L9byte__valS231 / 16;
      int32_t _M0L2loS233 = _M0L9byte__valS231 % 16;
      int32_t _M0L6_2atmpS1173 = _M0L12digit__startS235 + _M0L6offsetS229;
      int32_t _M0L6_2atmpS1171 = _M0L6_2atmpS1173 - 2;
      int32_t _M0L6_2atmpS1172 =
        ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L2hiS232];
      int32_t _M0L6_2atmpS1176;
      int32_t _M0L6_2atmpS1174;
      int32_t _M0L6_2atmpS1175;
      int32_t _M0L6_2atmpS1177;
      uint32_t _M0L6_2atmpS1178;
      _M0L6bufferS234[_M0L6_2atmpS1171] = _M0L6_2atmpS1172;
      _M0L6_2atmpS1176 = _M0L12digit__startS235 + _M0L6offsetS229;
      _M0L6_2atmpS1174 = _M0L6_2atmpS1176 - 1;
      _M0L6_2atmpS1175
      = ((moonbit_string_t)moonbit_string_literal_24.data)[
        _M0L2loS233
      ];
      _M0L6bufferS234[_M0L6_2atmpS1174] = _M0L6_2atmpS1175;
      _M0L6_2atmpS1177 = _M0L6offsetS229 - 2;
      _M0L6_2atmpS1178 = _M0L1nS230 >> 8;
      _M0L6offsetS229 = _M0L6_2atmpS1177;
      _M0L1nS230 = _M0L6_2atmpS1178;
      continue;
    } else if (_M0L6offsetS229 == 1) {
      uint32_t _M0L6_2atmpS1181 = _M0L1nS230 & 15u;
      int32_t _M0L6nibbleS237 = *(int32_t*)&_M0L6_2atmpS1181;
      int32_t _M0L6_2atmpS1180 =
        ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L6nibbleS237];
      _M0L6bufferS234[_M0L12digit__startS235] = _M0L6_2atmpS1180;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS228
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS227;
  struct _M0TPB6Logger _M0L6_2atmpS1170;
  moonbit_string_t _result_1996;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS227 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS227);
  _M0L6_2atmpS1170
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS227
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS228, _M0L6_2atmpS1170);
  if (_M0L6_2atmpS1170.$1) {
    moonbit_decref(_M0L6_2atmpS1170.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_1996 = _M0MPB13StringBuilder10to__string(_M0L6loggerS227);
  moonbit_decref_cycle_free(_M0L6loggerS227);
  return _result_1996;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS222,
  struct _M0TPB6Logger _M0L6loggerS221
) {
  moonbit_string_t _M0L6_2atmpS1167;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1167 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS222);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS221.$0->$method_0(_M0L6loggerS221.$1, _M0L6_2atmpS1167);
  moonbit_decref_cycle_free(_M0L6_2atmpS1167);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS224,
  struct _M0TPB6Logger _M0L6loggerS223
) {
  moonbit_string_t _M0L6_2atmpS1168;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1168 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS224);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS223.$0->$method_0(_M0L6loggerS223.$1, _M0L6_2atmpS1168);
  moonbit_decref_cycle_free(_M0L6_2atmpS1168);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS226,
  struct _M0TPB6Logger _M0L6loggerS225
) {
  moonbit_string_t _M0L6_2atmpS1169;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1169 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS226);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS225.$0->$method_0(_M0L6loggerS225.$1, _M0L6_2atmpS1169);
  moonbit_decref_cycle_free(_M0L6_2atmpS1169);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS220
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS220.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS219
) {
  moonbit_string_t _M0L8_2afieldS1902;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS1902 = _M0L4selfS219.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1902);
  return _M0L8_2afieldS1902;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS215,
  moonbit_string_t _M0L5valueS216,
  int32_t _M0L5startS217,
  int32_t _M0L3lenS218
) {
  int32_t _M0L6_2atmpS1166;
  int64_t _M0L6_2atmpS1165;
  struct _M0TPC16string10StringView _M0L6_2atmpS1164;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1166 = _M0L5startS217 + _M0L3lenS218;
  _M0L6_2atmpS1165 = (int64_t)_M0L6_2atmpS1166;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1164
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS216, _M0L5startS217, _M0L6_2atmpS1165);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS215, _M0L6_2atmpS1164);
  moonbit_decref_cycle_free(_M0L6_2atmpS1164.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String21clamped__view_2einner(
  moonbit_string_t _M0L4selfS208,
  int32_t _M0L5startS210,
  int64_t _M0L3endS212
) {
  int32_t _M0L3lenS207;
  int32_t _M0Lm2loS209;
  int32_t _M0Lm2hiS211;
  int32_t _M0L6_2atmpS1148;
  int32_t _if__result_1997;
  int32_t _M0L6_2atmpS1156;
  int32_t _if__result_1998;
  int32_t _M0L6_2atmpS1158;
  int32_t _M0L6_2atmpS1159;
  #line 698 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS207 = Moonbit_array_length(_M0L4selfS208);
  if (_M0L5startS210 < 0) {
    _M0Lm2loS209 = 0;
  } else if (_M0L5startS210 > _M0L3lenS207) {
    _M0Lm2loS209 = _M0L3lenS207;
  } else {
    _M0Lm2loS209 = _M0L5startS210;
  }
  if (_M0L3endS212 == 4294967296ll) {
    _M0Lm2hiS211 = _M0L3lenS207;
  } else {
    int64_t _M0L7_2aSomeS213 = _M0L3endS212;
    int32_t _M0L4_2aeS214 = (int32_t)_M0L7_2aSomeS213;
    if (_M0L4_2aeS214 < 0) {
      _M0Lm2hiS211 = 0;
    } else if (_M0L4_2aeS214 > _M0L3lenS207) {
      _M0Lm2hiS211 = _M0L3lenS207;
    } else {
      _M0Lm2hiS211 = _M0L4_2aeS214;
    }
  }
  _M0L6_2atmpS1148 = _M0Lm2loS209;
  if (_M0L6_2atmpS1148 > 0) {
    int32_t _M0L6_2atmpS1147 = _M0Lm2loS209;
    if (_M0L6_2atmpS1147 < _M0L3lenS207) {
      int32_t _M0L6_2atmpS1146 = _M0Lm2loS209;
      int32_t _M0L6_2atmpS1145 = _M0L4selfS208[_M0L6_2atmpS1146];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1145)) {
        int32_t _M0L6_2atmpS1144 = _M0Lm2loS209;
        int32_t _M0L6_2atmpS1143 = _M0L6_2atmpS1144 - 1;
        int32_t _M0L6_2atmpS1142 = _M0L4selfS208[_M0L6_2atmpS1143];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_1997
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1142);
      } else {
        _if__result_1997 = 0;
      }
    } else {
      _if__result_1997 = 0;
    }
  } else {
    _if__result_1997 = 0;
  }
  if (_if__result_1997) {
    int32_t _M0L6_2atmpS1149 = _M0Lm2loS209;
    _M0Lm2loS209 = _M0L6_2atmpS1149 + 1;
  }
  _M0L6_2atmpS1156 = _M0Lm2hiS211;
  if (_M0L6_2atmpS1156 > 0) {
    int32_t _M0L6_2atmpS1155 = _M0Lm2hiS211;
    if (_M0L6_2atmpS1155 < _M0L3lenS207) {
      int32_t _M0L6_2atmpS1154 = _M0Lm2hiS211;
      int32_t _M0L6_2atmpS1153 = _M0L4selfS208[_M0L6_2atmpS1154];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1153)) {
        int32_t _M0L6_2atmpS1152 = _M0Lm2hiS211;
        int32_t _M0L6_2atmpS1151 = _M0L6_2atmpS1152 - 1;
        int32_t _M0L6_2atmpS1150 = _M0L4selfS208[_M0L6_2atmpS1151];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_1998
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1150);
      } else {
        _if__result_1998 = 0;
      }
    } else {
      _if__result_1998 = 0;
    }
  } else {
    _if__result_1998 = 0;
  }
  if (_if__result_1998) {
    int32_t _M0L6_2atmpS1157 = _M0Lm2hiS211;
    _M0Lm2hiS211 = _M0L6_2atmpS1157 - 1;
  }
  _M0L6_2atmpS1158 = _M0Lm2loS209;
  _M0L6_2atmpS1159 = _M0Lm2hiS211;
  if (_M0L6_2atmpS1158 >= _M0L6_2atmpS1159) {
    int32_t _M0L6_2atmpS1160 = _M0Lm2loS209;
    int32_t _M0L6_2atmpS1161 = _M0Lm2loS209;
    moonbit_incref_cycle_free(_M0L4selfS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS208,
                                                 .$1 = _M0L6_2atmpS1160,
                                                 .$2 = _M0L6_2atmpS1161};
  } else {
    int32_t _M0L6_2atmpS1162 = _M0Lm2loS209;
    int32_t _M0L6_2atmpS1163 = _M0Lm2hiS211;
    moonbit_incref_cycle_free(_M0L4selfS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS208,
                                                 .$1 = _M0L6_2atmpS1162,
                                                 .$2 = _M0L6_2atmpS1163};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS206,
  struct _M0TPB4Show _M0L4showS205
) {
  struct _M0TPB6Logger _M0L6_2atmpS1141;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS206);
  _M0L6_2atmpS1141
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS206
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS205.$0->$method_0(_M0L4showS205.$1, _M0L6_2atmpS1141);
  if (_M0L6_2atmpS1141.$1) {
    moonbit_decref(_M0L6_2atmpS1141.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS204,
  struct _M0TPB4Show _M0L4showS203
) {
  struct _M0TPB6Logger _M0L6_2atmpS1140;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS204);
  _M0L6_2atmpS1140
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS204
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS203.$0->$method_0(_M0L4showS203.$1, _M0L6_2atmpS1140);
  if (_M0L6_2atmpS1140.$1) {
    moonbit_decref(_M0L6_2atmpS1140.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS202) {
  int64_t _M0L6_2atmpS1139;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1139 = (int64_t)_M0L4selfS202;
  return *(uint64_t*)&_M0L6_2atmpS1139;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

moonbit_string_t _M0MPC16string6String14escape_2einner(
  moonbit_string_t _M0L4selfS200,
  int32_t _M0L5quoteS201
) {
  struct _M0TPB13StringBuilder* _M0L3bufS199;
  int32_t _M0L6_2atmpS1138;
  struct _M0TPC16string10StringView _M0L6_2atmpS1136;
  struct _M0TPB6Logger _M0L6_2atmpS1137;
  moonbit_string_t _result_1999;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS199 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1138 = Moonbit_array_length(_M0L4selfS200);
  moonbit_incref_cycle_free(_M0L4selfS200);
  _M0L6_2atmpS1136
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS200, .$1 = 0, .$2 = _M0L6_2atmpS1138
  };
  moonbit_incref_cycle_free(_M0L3bufS199);
  _M0L6_2atmpS1137
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS199
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1136, _M0L6_2atmpS1137, _M0L5quoteS201);
  moonbit_decref_cycle_free(_M0L6_2atmpS1136.$0);
  if (_M0L6_2atmpS1137.$1) {
    moonbit_decref(_M0L6_2atmpS1137.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_1999 = _M0MPB13StringBuilder10to__string(_M0L3bufS199);
  moonbit_decref_cycle_free(_M0L3bufS199);
  return _result_1999;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS191,
  struct _M0TPB6Logger _M0L6loggerS189,
  int32_t _M0L5quoteS188
) {
  int32_t _M0L3endS1134;
  int32_t _M0L5startS1135;
  int32_t _M0L3lenS190;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS192;
  int32_t _M0L1iS193;
  int32_t _M0L3segS194;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS188) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS189.$0->$method_3(_M0L6loggerS189.$1, 34);
  }
  _M0L3endS1134 = _M0L4selfS191.$2;
  _M0L5startS1135 = _M0L4selfS191.$1;
  _M0L3lenS190 = _M0L3endS1134 - _M0L5startS1135;
  moonbit_incref_cycle_free(_M0L4selfS191.$0);
  if (_M0L6loggerS189.$1) {
    moonbit_incref(_M0L6loggerS189.$1);
  }
  _M0L6_2aenvS192
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS192)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 28, 0);
  _M0L6_2aenvS192->$0 = _M0L4selfS191;
  _M0L6_2aenvS192->$1 = _M0L6loggerS189;
  _M0L1iS193 = 0;
  _M0L3segS194 = 0;
  _2afor_195:;
  while (1) {
    moonbit_string_t _M0L3strS1131;
    int32_t _M0L5startS1133;
    int32_t _M0L6_2atmpS1132;
    int32_t _M0L4codeS196;
    int32_t _M0L1cS198;
    int32_t _M0L6_2atmpS1115;
    int32_t _M0L6_2atmpS1116;
    int32_t _M0L6_2atmpS1117;
    if (_M0L1iS193 >= _M0L3lenS190) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS192, _M0L3segS194, _M0L1iS193);
      moonbit_decref_cycle_free(_M0L6_2aenvS192);
      break;
    }
    _M0L3strS1131 = _M0L4selfS191.$0;
    _M0L5startS1133 = _M0L4selfS191.$1;
    _M0L6_2atmpS1132 = _M0L5startS1133 + _M0L1iS193;
    _M0L4codeS196 = _M0L3strS1131[_M0L6_2atmpS1132];
    switch (_M0L4codeS196) {
      case 34: {
        _M0L1cS198 = _M0L4codeS196;
        goto join_197;
        break;
      }
      
      case 92: {
        _M0L1cS198 = _M0L4codeS196;
        goto join_197;
        break;
      }
      
      case 10: {
        int32_t _M0L6_2atmpS1118;
        int32_t _M0L6_2atmpS1119;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS192, _M0L3segS194, _M0L1iS193);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS189.$0->$method_0(_M0L6loggerS189.$1, (moonbit_string_t)moonbit_string_literal_25.data);
        _M0L6_2atmpS1118 = _M0L1iS193 + 1;
        _M0L6_2atmpS1119 = _M0L1iS193 + 1;
        _M0L1iS193 = _M0L6_2atmpS1118;
        _M0L3segS194 = _M0L6_2atmpS1119;
        goto _2afor_195;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1120;
        int32_t _M0L6_2atmpS1121;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS192, _M0L3segS194, _M0L1iS193);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS189.$0->$method_0(_M0L6loggerS189.$1, (moonbit_string_t)moonbit_string_literal_26.data);
        _M0L6_2atmpS1120 = _M0L1iS193 + 1;
        _M0L6_2atmpS1121 = _M0L1iS193 + 1;
        _M0L1iS193 = _M0L6_2atmpS1120;
        _M0L3segS194 = _M0L6_2atmpS1121;
        goto _2afor_195;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1122;
        int32_t _M0L6_2atmpS1123;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS192, _M0L3segS194, _M0L1iS193);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS189.$0->$method_0(_M0L6loggerS189.$1, (moonbit_string_t)moonbit_string_literal_27.data);
        _M0L6_2atmpS1122 = _M0L1iS193 + 1;
        _M0L6_2atmpS1123 = _M0L1iS193 + 1;
        _M0L1iS193 = _M0L6_2atmpS1122;
        _M0L3segS194 = _M0L6_2atmpS1123;
        goto _2afor_195;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1124;
        int32_t _M0L6_2atmpS1125;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS192, _M0L3segS194, _M0L1iS193);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS189.$0->$method_0(_M0L6loggerS189.$1, (moonbit_string_t)moonbit_string_literal_28.data);
        _M0L6_2atmpS1124 = _M0L1iS193 + 1;
        _M0L6_2atmpS1125 = _M0L1iS193 + 1;
        _M0L1iS193 = _M0L6_2atmpS1124;
        _M0L3segS194 = _M0L6_2atmpS1125;
        goto _2afor_195;
        break;
      }
      default: {
        if (_M0L4codeS196 < 32) {
          int32_t _M0L6_2atmpS1127;
          moonbit_string_t _M0L6_2atmpS1126;
          int32_t _M0L6_2atmpS1128;
          int32_t _M0L6_2atmpS1129;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS192, _M0L3segS194, _M0L1iS193);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS189.$0->$method_0(_M0L6loggerS189.$1, (moonbit_string_t)moonbit_string_literal_29.data);
          _M0L6_2atmpS1127 = _M0L4codeS196 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1126 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1127);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS189.$0->$method_0(_M0L6loggerS189.$1, _M0L6_2atmpS1126);
          moonbit_decref_cycle_free(_M0L6_2atmpS1126);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS189.$0->$method_0(_M0L6loggerS189.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1128 = _M0L1iS193 + 1;
          _M0L6_2atmpS1129 = _M0L1iS193 + 1;
          _M0L1iS193 = _M0L6_2atmpS1128;
          _M0L3segS194 = _M0L6_2atmpS1129;
          goto _2afor_195;
        } else {
          int32_t _M0L6_2atmpS1130 = _M0L1iS193 + 1;
          int32_t _tmp_2002 = _M0L3segS194;
          _M0L1iS193 = _M0L6_2atmpS1130;
          _M0L3segS194 = _tmp_2002;
          goto _2afor_195;
        }
        break;
      }
    }
    goto joinlet_2001;
    join_197:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS192, _M0L3segS194, _M0L1iS193);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS189.$0->$method_3(_M0L6loggerS189.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1115 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS198);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS189.$0->$method_3(_M0L6loggerS189.$1, _M0L6_2atmpS1115);
    _M0L6_2atmpS1116 = _M0L1iS193 + 1;
    _M0L6_2atmpS1117 = _M0L1iS193 + 1;
    _M0L1iS193 = _M0L6_2atmpS1116;
    _M0L3segS194 = _M0L6_2atmpS1117;
    continue;
    joinlet_2001:;
    break;
  }
  if (_M0L5quoteS188) {
    #line 202 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS189.$0->$method_3(_M0L6loggerS189.$1, 34);
  }
  return 0;
}

int32_t _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS184,
  int32_t _M0L3segS187,
  int32_t _M0L1iS186
) {
  struct _M0TPB6Logger _M0L6loggerS183;
  struct _M0TPC16string10StringView _M0L4selfS185;
  #line 153 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6loggerS183 = _M0L6_2aenvS184->$1;
  _M0L4selfS185 = _M0L6_2aenvS184->$0;
  if (_M0L1iS186 > _M0L3segS187) {
    int64_t _M0L6_2atmpS1114 = (int64_t)_M0L1iS186;
    struct _M0TPC16string10StringView _M0L6_2atmpS1113;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1113
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS185, _M0L3segS187, _M0L6_2atmpS1114);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS183.$0->$method_2(_M0L6loggerS183.$1, _M0L6_2atmpS1113);
    moonbit_decref_cycle_free(_M0L6_2atmpS1113.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS174,
  int32_t _M0L5startS176,
  int64_t _M0L3endS178
) {
  int32_t _M0L3endS1111;
  int32_t _M0L5startS1112;
  int32_t _M0L3lenS173;
  int32_t _M0Lm2loS175;
  int32_t _M0Lm2hiS177;
  moonbit_string_t _M0L3strS181;
  int32_t _M0L4baseS182;
  int32_t _M0L6_2atmpS1089;
  int32_t _if__result_2003;
  int32_t _M0L6_2atmpS1099;
  int32_t _if__result_2004;
  int32_t _M0L6_2atmpS1101;
  int32_t _M0L6_2atmpS1102;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1111 = _M0L4selfS174.$2;
  _M0L5startS1112 = _M0L4selfS174.$1;
  _M0L3lenS173 = _M0L3endS1111 - _M0L5startS1112;
  if (_M0L5startS176 < 0) {
    _M0Lm2loS175 = 0;
  } else if (_M0L5startS176 > _M0L3lenS173) {
    _M0Lm2loS175 = _M0L3lenS173;
  } else {
    _M0Lm2loS175 = _M0L5startS176;
  }
  if (_M0L3endS178 == 4294967296ll) {
    _M0Lm2hiS177 = _M0L3lenS173;
  } else {
    int64_t _M0L7_2aSomeS179 = _M0L3endS178;
    int32_t _M0L4_2aeS180 = (int32_t)_M0L7_2aSomeS179;
    if (_M0L4_2aeS180 < 0) {
      _M0Lm2hiS177 = 0;
    } else if (_M0L4_2aeS180 > _M0L3lenS173) {
      _M0Lm2hiS177 = _M0L3lenS173;
    } else {
      _M0Lm2hiS177 = _M0L4_2aeS180;
    }
  }
  _M0L3strS181 = _M0L4selfS174.$0;
  _M0L4baseS182 = _M0L4selfS174.$1;
  _M0L6_2atmpS1089 = _M0Lm2loS175;
  if (_M0L6_2atmpS1089 > 0) {
    int32_t _M0L6_2atmpS1088 = _M0Lm2loS175;
    if (_M0L6_2atmpS1088 < _M0L3lenS173) {
      int32_t _M0L6_2atmpS1087 = _M0Lm2loS175;
      int32_t _M0L6_2atmpS1086 = _M0L4baseS182 + _M0L6_2atmpS1087;
      int32_t _M0L6_2atmpS1085 = _M0L3strS181[_M0L6_2atmpS1086];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1085)) {
        int32_t _M0L6_2atmpS1084 = _M0Lm2loS175;
        int32_t _M0L6_2atmpS1083 = _M0L4baseS182 + _M0L6_2atmpS1084;
        int32_t _M0L6_2atmpS1082 = _M0L6_2atmpS1083 - 1;
        int32_t _M0L6_2atmpS1081 = _M0L3strS181[_M0L6_2atmpS1082];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2003
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1081);
      } else {
        _if__result_2003 = 0;
      }
    } else {
      _if__result_2003 = 0;
    }
  } else {
    _if__result_2003 = 0;
  }
  if (_if__result_2003) {
    int32_t _M0L6_2atmpS1090 = _M0Lm2loS175;
    _M0Lm2loS175 = _M0L6_2atmpS1090 + 1;
  }
  _M0L6_2atmpS1099 = _M0Lm2hiS177;
  if (_M0L6_2atmpS1099 > 0) {
    int32_t _M0L6_2atmpS1098 = _M0Lm2hiS177;
    if (_M0L6_2atmpS1098 < _M0L3lenS173) {
      int32_t _M0L6_2atmpS1097 = _M0Lm2hiS177;
      int32_t _M0L6_2atmpS1096 = _M0L4baseS182 + _M0L6_2atmpS1097;
      int32_t _M0L6_2atmpS1095 = _M0L3strS181[_M0L6_2atmpS1096];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1095)) {
        int32_t _M0L6_2atmpS1094 = _M0Lm2hiS177;
        int32_t _M0L6_2atmpS1093 = _M0L4baseS182 + _M0L6_2atmpS1094;
        int32_t _M0L6_2atmpS1092 = _M0L6_2atmpS1093 - 1;
        int32_t _M0L6_2atmpS1091 = _M0L3strS181[_M0L6_2atmpS1092];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2004
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1091);
      } else {
        _if__result_2004 = 0;
      }
    } else {
      _if__result_2004 = 0;
    }
  } else {
    _if__result_2004 = 0;
  }
  if (_if__result_2004) {
    int32_t _M0L6_2atmpS1100 = _M0Lm2hiS177;
    _M0Lm2hiS177 = _M0L6_2atmpS1100 - 1;
  }
  _M0L6_2atmpS1101 = _M0Lm2loS175;
  _M0L6_2atmpS1102 = _M0Lm2hiS177;
  if (_M0L6_2atmpS1101 >= _M0L6_2atmpS1102) {
    int32_t _M0L6_2atmpS1106 = _M0Lm2loS175;
    int32_t _M0L6_2atmpS1103 = _M0L4baseS182 + _M0L6_2atmpS1106;
    int32_t _M0L6_2atmpS1105 = _M0Lm2loS175;
    int32_t _M0L6_2atmpS1104 = _M0L4baseS182 + _M0L6_2atmpS1105;
    moonbit_incref_cycle_free(_M0L3strS181);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS181,
                                                 .$1 = _M0L6_2atmpS1103,
                                                 .$2 = _M0L6_2atmpS1104};
  } else {
    int32_t _M0L6_2atmpS1110 = _M0Lm2loS175;
    int32_t _M0L6_2atmpS1107 = _M0L4baseS182 + _M0L6_2atmpS1110;
    int32_t _M0L6_2atmpS1109 = _M0Lm2hiS177;
    int32_t _M0L6_2atmpS1108 = _M0L4baseS182 + _M0L6_2atmpS1109;
    moonbit_incref_cycle_free(_M0L3strS181);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS181,
                                                 .$1 = _M0L6_2atmpS1107,
                                                 .$2 = _M0L6_2atmpS1108};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS172) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS171;
  int32_t _M0L6_2atmpS1078;
  int32_t _M0L6_2atmpS1077;
  int32_t _M0L6_2atmpS1080;
  int32_t _M0L6_2atmpS1079;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1076;
  moonbit_string_t _result_2005;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS171 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1078 = _M0IPC14byte4BytePB3Div3div(_M0L1bS172, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1077
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1078);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS171, _M0L6_2atmpS1077);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1080 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS172, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1079
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1080);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS171, _M0L6_2atmpS1079);
  _M0L6_2atmpS1076 = _M0L7_2aselfS171;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2005 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1076);
  moonbit_decref_cycle_free(_M0L6_2atmpS1076);
  return _result_2005;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS170) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS170 < 10) {
    int32_t _M0L6_2atmpS1073;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1073 = _M0IPC14byte4BytePB3Add3add(_M0L1iS170, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1073);
  } else {
    int32_t _M0L6_2atmpS1075;
    int32_t _M0L6_2atmpS1074;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1075 = _M0IPC14byte4BytePB3Add3add(_M0L1iS170, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1074 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1075, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1074);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS168,
  int32_t _M0L4thatS169
) {
  int32_t _M0L6_2atmpS1071;
  int32_t _M0L6_2atmpS1072;
  int32_t _M0L6_2atmpS1070;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1071 = (int32_t)_M0L4selfS168;
  _M0L6_2atmpS1072 = (int32_t)_M0L4thatS169;
  _M0L6_2atmpS1070 = _M0L6_2atmpS1071 - _M0L6_2atmpS1072;
  return _M0L6_2atmpS1070 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS166,
  int32_t _M0L4thatS167
) {
  int32_t _M0L6_2atmpS1068;
  int32_t _M0L6_2atmpS1069;
  int32_t _M0L6_2atmpS1067;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1068 = (int32_t)_M0L4selfS166;
  _M0L6_2atmpS1069 = (int32_t)_M0L4thatS167;
  _M0L6_2atmpS1067 = _M0L6_2atmpS1068 % _M0L6_2atmpS1069;
  return _M0L6_2atmpS1067 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS164,
  int32_t _M0L4thatS165
) {
  int32_t _M0L6_2atmpS1065;
  int32_t _M0L6_2atmpS1066;
  int32_t _M0L6_2atmpS1064;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1065 = (int32_t)_M0L4selfS164;
  _M0L6_2atmpS1066 = (int32_t)_M0L4thatS165;
  _M0L6_2atmpS1064 = _M0L6_2atmpS1065 / _M0L6_2atmpS1066;
  return _M0L6_2atmpS1064 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS162,
  int32_t _M0L4thatS163
) {
  int32_t _M0L6_2atmpS1062;
  int32_t _M0L6_2atmpS1063;
  int32_t _M0L6_2atmpS1061;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1062 = (int32_t)_M0L4selfS162;
  _M0L6_2atmpS1063 = (int32_t)_M0L4thatS163;
  _M0L6_2atmpS1061 = _M0L6_2atmpS1062 + _M0L6_2atmpS1063;
  return _M0L6_2atmpS1061 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS161) {
  int32_t _M0L6_2atmpS1060;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1060 = (int32_t)_M0L4selfS161;
  return _M0L6_2atmpS1060;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS160) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS160 >= 56320 && _M0L4selfS160 <= 57343;
}

int32_t _M0MPC16uint166UInt1622is__leading__surrogate(int32_t _M0L4selfS159) {
  #line 28 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS159 >= 55296 && _M0L4selfS159 <= 56319;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS158,
  moonbit_string_t _M0L3strS156
) {
  int32_t _M0L8str__lenS155;
  int32_t _M0L3lenS1059;
  int32_t _M0L8requiredS157;
  uint16_t* _M0L4dataS1054;
  int32_t _M0L6_2atmpS1053;
  int32_t _if__result_2006;
  uint16_t* _M0L4dataS1055;
  int32_t _M0L3lenS1056;
  int32_t _M0L3lenS1058;
  int32_t _M0L6_2atmpS1057;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS155 = Moonbit_array_length(_M0L3strS156);
  if (_M0L8str__lenS155 == 0) {
    return 0;
  }
  _M0L3lenS1059 = _M0L4selfS158->$1;
  _M0L8requiredS157 = _M0L3lenS1059 + _M0L8str__lenS155;
  _M0L4dataS1054 = _M0L4selfS158->$0;
  _M0L6_2atmpS1053 = Moonbit_array_length(_M0L4dataS1054);
  if (_M0L8requiredS157 > _M0L6_2atmpS1053) {
    _if__result_2006 = 1;
  } else {
    int32_t _M0L3lenS1052 = _M0L4selfS158->$1;
    _if__result_2006 = _M0L8requiredS157 < _M0L3lenS1052;
  }
  if (_if__result_2006) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS158, _M0L8requiredS157);
  }
  _M0L4dataS1055 = _M0L4selfS158->$0;
  _M0L3lenS1056 = _M0L4selfS158->$1;
  moonbit_incref_cycle_free(_M0L4dataS1055);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1055, _M0L3lenS1056, _M0L3strS156, 0, _M0L8str__lenS155);
  moonbit_decref_cycle_free(_M0L4dataS1055);
  _M0L3lenS1058 = _M0L4selfS158->$1;
  _M0L6_2atmpS1057 = _M0L3lenS1058 + _M0L8str__lenS155;
  _M0L4selfS158->$1 = _M0L6_2atmpS1057;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS151,
  int32_t _M0L11dst__offsetS154,
  moonbit_string_t _M0L3strS152,
  int32_t _M0L11str__offsetS147,
  int32_t _M0L3lenS148
) {
  int32_t _M0L16end__str__offsetS146;
  int32_t _M0L1iS149;
  int32_t _M0L1jS150;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS146 = _M0L11str__offsetS147 + _M0L3lenS148;
  _M0L1iS149 = _M0L11str__offsetS147;
  _M0L1jS150 = _M0L11dst__offsetS154;
  while (1) {
    if (_M0L1iS149 < _M0L16end__str__offsetS146) {
      int32_t _M0L6_2atmpS1049 = _M0L3strS152[_M0L1iS149];
      int32_t _M0L6_2atmpS1050;
      int32_t _M0L6_2atmpS1051;
      _M0L4selfS151[_M0L1jS150] = _M0L6_2atmpS1049;
      _M0L6_2atmpS1050 = _M0L1iS149 + 1;
      _M0L6_2atmpS1051 = _M0L1jS150 + 1;
      _M0L1iS149 = _M0L6_2atmpS1050;
      _M0L1jS150 = _M0L6_2atmpS1051;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS144,
  int32_t _M0L2chS143
) {
  uint32_t _M0L4codeS142;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS142 = _M0MPC14char4Char8to__uint(_M0L2chS143);
  if (_M0L4codeS142 <= 65535u) {
    int32_t _M0L3lenS1020 = _M0L4selfS144->$1;
    uint16_t* _M0L4dataS1022 = _M0L4selfS144->$0;
    int32_t _M0L6_2atmpS1021 = Moonbit_array_length(_M0L4dataS1022);
    uint16_t* _M0L4dataS1025;
    int32_t _M0L3lenS1026;
    int32_t _M0L6_2atmpS1027;
    int32_t _M0L3lenS1029;
    int32_t _M0L6_2atmpS1028;
    if (_M0L3lenS1020 >= _M0L6_2atmpS1021) {
      int32_t _M0L3lenS1024 = _M0L4selfS144->$1;
      int32_t _M0L6_2atmpS1023 = _M0L3lenS1024 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS144, _M0L6_2atmpS1023);
    }
    _M0L4dataS1025 = _M0L4selfS144->$0;
    _M0L3lenS1026 = _M0L4selfS144->$1;
    moonbit_incref_cycle_free(_M0L4dataS1025);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1027 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS142);
    if (
      _M0L3lenS1026 < 0
      || _M0L3lenS1026 >= Moonbit_array_length(_M0L4dataS1025)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1025[_M0L3lenS1026] = _M0L6_2atmpS1027;
    moonbit_decref_cycle_free(_M0L4dataS1025);
    _M0L3lenS1029 = _M0L4selfS144->$1;
    _M0L6_2atmpS1028 = _M0L3lenS1029 + 1;
    _M0L4selfS144->$1 = _M0L6_2atmpS1028;
  } else if (_M0L4codeS142 <= 1114111u) {
    uint16_t* _M0L4dataS1033 = _M0L4selfS144->$0;
    int32_t _M0L6_2atmpS1031 = Moonbit_array_length(_M0L4dataS1033);
    int32_t _M0L3lenS1032 = _M0L4selfS144->$1;
    int32_t _M0L6_2atmpS1030 = _M0L6_2atmpS1031 - _M0L3lenS1032;
    uint32_t _M0L4codeS145;
    uint16_t* _M0L4dataS1036;
    int32_t _M0L3lenS1037;
    uint32_t _M0L6_2atmpS1040;
    uint32_t _M0L6_2atmpS1039;
    int32_t _M0L6_2atmpS1038;
    uint16_t* _M0L4dataS1041;
    int32_t _M0L3lenS1046;
    int32_t _M0L6_2atmpS1042;
    uint32_t _M0L6_2atmpS1045;
    uint32_t _M0L6_2atmpS1044;
    int32_t _M0L6_2atmpS1043;
    int32_t _M0L3lenS1048;
    int32_t _M0L6_2atmpS1047;
    if (_M0L6_2atmpS1030 < 2) {
      int32_t _M0L3lenS1035 = _M0L4selfS144->$1;
      int32_t _M0L6_2atmpS1034 = _M0L3lenS1035 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS144, _M0L6_2atmpS1034);
    }
    _M0L4codeS145 = _M0L4codeS142 - 65536u;
    _M0L4dataS1036 = _M0L4selfS144->$0;
    _M0L3lenS1037 = _M0L4selfS144->$1;
    _M0L6_2atmpS1040 = _M0L4codeS145 >> 10;
    _M0L6_2atmpS1039 = 55296u + _M0L6_2atmpS1040;
    moonbit_incref_cycle_free(_M0L4dataS1036);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1038 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1039);
    if (
      _M0L3lenS1037 < 0
      || _M0L3lenS1037 >= Moonbit_array_length(_M0L4dataS1036)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1036[_M0L3lenS1037] = _M0L6_2atmpS1038;
    moonbit_decref_cycle_free(_M0L4dataS1036);
    _M0L4dataS1041 = _M0L4selfS144->$0;
    _M0L3lenS1046 = _M0L4selfS144->$1;
    _M0L6_2atmpS1042 = _M0L3lenS1046 + 1;
    _M0L6_2atmpS1045 = _M0L4codeS145 & 1023u;
    _M0L6_2atmpS1044 = 56320u + _M0L6_2atmpS1045;
    moonbit_incref_cycle_free(_M0L4dataS1041);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1043 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1044);
    if (
      _M0L6_2atmpS1042 < 0
      || _M0L6_2atmpS1042 >= Moonbit_array_length(_M0L4dataS1041)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1041[_M0L6_2atmpS1042] = _M0L6_2atmpS1043;
    moonbit_decref_cycle_free(_M0L4dataS1041);
    _M0L3lenS1048 = _M0L4selfS144->$1;
    _M0L6_2atmpS1047 = _M0L3lenS1048 + 2;
    _M0L4selfS144->$1 = _M0L6_2atmpS1047;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_30.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS139,
  int32_t _M0L8requiredS140
) {
  uint16_t* _M0L4dataS1019;
  int32_t _M0L6_2atmpS1017;
  int32_t _M0L3lenS1018;
  int32_t _M0L13new__capacityS138;
  uint16_t* _M0L4dataS1014;
  int32_t _M0L6_2atmpS1015;
  int32_t _M0L3lenS1016;
  uint16_t* _M0L9new__dataS141;
  uint16_t* _M0L6_2aoldS1903;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1019 = _M0L4selfS139->$0;
  _M0L6_2atmpS1017 = Moonbit_array_length(_M0L4dataS1019);
  _M0L3lenS1018 = _M0L4selfS139->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS138
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1017, _M0L3lenS1018, _M0L8requiredS140);
  _M0L4dataS1014 = _M0L4selfS139->$0;
  moonbit_incref_cycle_free(_M0L4dataS1014);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1015 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1016 = _M0L4selfS139->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS141
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1014, _M0L13new__capacityS138, _M0L6_2atmpS1015, _M0L3lenS1016, 0, 0);
  _M0L6_2aoldS1903 = _M0L4selfS139->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1903);
  _M0L4selfS139->$0 = _M0L9new__dataS141;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS137,
  int32_t _M0L3lenS133,
  int32_t _M0L8requiredS132
) {
  int32_t _M0L5spaceS134;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS132 < _M0L3lenS133) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_31.data);
  }
  _M0L5spaceS134 = _M0L7currentS137;
  while (1) {
    if (_M0L5spaceS134 < _M0L8requiredS132) {
      int32_t _M0L4nextS135 = _M0L5spaceS134 * 2;
      if (_M0L4nextS135 <= _M0L5spaceS134) {
        return _M0L8requiredS132;
      }
      _M0L5spaceS134 = _M0L4nextS135;
      continue;
    } else {
      return _M0L5spaceS134;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS131) {
  int32_t _M0L6_2atmpS1013;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1013 = *(int32_t*)&_M0L4selfS131;
  return (uint16_t)_M0L6_2atmpS1013;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS130) {
  int32_t _M0L6_2atmpS1012;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1012 = _M0L4selfS130;
  return *(uint32_t*)&_M0L6_2atmpS1012;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS128
) {
  int32_t _M0L3lenS1003;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1003 = _M0L4selfS128->$1;
  if (_M0L3lenS1003 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1004 = _M0L4selfS128->$1;
    uint16_t* _M0L4dataS1006 = _M0L4selfS128->$0;
    int32_t _M0L6_2atmpS1005 = Moonbit_array_length(_M0L4dataS1006);
    if (_M0L3lenS1004 == _M0L6_2atmpS1005) {
      uint16_t* _M0L4dataS1007 = _M0L4selfS128->$0;
      moonbit_incref_cycle_free(_M0L4dataS1007);
      return _M0L4dataS1007;
    } else {
      uint16_t* _M0L4dataS1008 = _M0L4selfS128->$0;
      int32_t _M0L3lenS1009 = _M0L4selfS128->$1;
      int32_t _M0L6_2atmpS1010;
      int32_t _M0L3lenS1011;
      uint16_t* _M0L4dataS129;
      moonbit_incref_cycle_free(_M0L4dataS1008);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1010 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1011 = _M0L4selfS128->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS129
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1008, _M0L3lenS1009, _M0L6_2atmpS1010, _M0L3lenS1011, 0, 0);
      return _M0L4dataS129;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS125,
  int32_t _M0L13allocate__lenS121,
  int32_t _M0L4initS126,
  int32_t _M0L3lenS122,
  int32_t _M0L11src__offsetS123,
  int32_t _M0L11dst__offsetS124
) {
  int32_t _if__result_2009;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS121 >= 0) {
    if (_M0L3lenS122 >= 0) {
      if (_M0L11src__offsetS123 >= 0) {
        if (_M0L11dst__offsetS124 >= 0) {
          int32_t _M0L6_2atmpS999 = _M0L11src__offsetS123 + _M0L3lenS122;
          int32_t _M0L6_2atmpS1000 = Moonbit_array_length(_M0L3srcS125);
          if (_M0L6_2atmpS999 <= _M0L6_2atmpS1000) {
            int32_t _M0L6_2atmpS998 = _M0L11dst__offsetS124 + _M0L3lenS122;
            _if__result_2009 = _M0L6_2atmpS998 <= _M0L13allocate__lenS121;
          } else {
            _if__result_2009 = 0;
          }
        } else {
          _if__result_2009 = 0;
        }
      } else {
        _if__result_2009 = 0;
      }
    } else {
      _if__result_2009 = 0;
    }
  } else {
    _if__result_2009 = 0;
  }
  if (_if__result_2009) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS125, _M0L13allocate__lenS121, _M0L4initS126, _M0L11src__offsetS123, _M0L11dst__offsetS124, _M0L3lenS122);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS127;
    int32_t _M0L6_2atmpS1002;
    moonbit_string_t _M0L6_2atmpS1001;
    uint16_t* _result_2010;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS127
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L13allocate__lenS121);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L11src__offsetS123);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L11dst__offsetS124);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L3lenS122);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS127, (moonbit_string_t)moonbit_string_literal_36.data);
    _M0L6_2atmpS1002 = Moonbit_array_length(_M0L3srcS125);
    moonbit_decref_cycle_free(_M0L3srcS125);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS127, _M0L6_2atmpS1002);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1001
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS127);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS127);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2010 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1001);
    moonbit_decref_cycle_free(_M0L6_2atmpS1001);
    return _result_2010;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS118,
  int32_t _M0L13allocate__lenS115,
  int32_t _M0L4initS116,
  int32_t _M0L11src__offsetS119,
  int32_t _M0L11dst__offsetS117,
  int32_t _M0L9blit__lenS120
) {
  uint16_t* _M0L3dstS114;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS114
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS115, _M0L4initS116);
  moonbit_incref_cycle_free(_M0L3dstS114);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS114, _M0L11dst__offsetS117, _M0L3srcS118, _M0L11src__offsetS119, _M0L9blit__lenS120, sizeof(uint16_t));
  return _M0L3dstS114;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS112
) {
  int32_t _M0L7initialS111;
  uint16_t* _M0L4dataS113;
  struct _M0TPB13StringBuilder* _block_2011;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS112 < 1) {
    _M0L7initialS111 = 1;
  } else {
    int32_t _M0L6_2atmpS997 = _M0L10size__hintS112 + 1;
    _M0L7initialS111 = _M0L6_2atmpS997 / 2;
  }
  _M0L4dataS113 = (uint16_t*)moonbit_make_string(_M0L7initialS111, 0);
  _block_2011
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2011)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 33, 0);
  _block_2011->$0 = _M0L4dataS113;
  _block_2011->$1 = 0;
  return _block_2011;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS110) {
  int32_t _M0L6_2atmpS996;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS996 = (int32_t)_M0L4selfS110;
  return _M0L6_2atmpS996;
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS96,
  int32_t _M0L13allocate__lenS92,
  int32_t _M0L3lenS93,
  int32_t _M0L11src__offsetS94,
  int32_t _M0L11dst__offsetS95
) {
  int32_t _if__result_2012;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS92 >= 0) {
    if (_M0L3lenS93 >= 0) {
      if (_M0L11src__offsetS94 >= 0) {
        if (_M0L11dst__offsetS95 >= 0) {
          int32_t _M0L6_2atmpS982 = _M0L11src__offsetS94 + _M0L3lenS93;
          int32_t _M0L6_2atmpS983;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS983 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS96);
          if (_M0L6_2atmpS982 <= _M0L6_2atmpS983) {
            int32_t _M0L6_2atmpS981 = _M0L11dst__offsetS95 + _M0L3lenS93;
            _if__result_2012 = _M0L6_2atmpS981 <= _M0L13allocate__lenS92;
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
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS96, _M0L13allocate__lenS92, _M0L11src__offsetS94, _M0L11dst__offsetS95, _M0L3lenS93);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS97;
    int32_t _M0L6_2atmpS985;
    moonbit_string_t _M0L6_2atmpS984;
    float* _result_2013;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS97
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS97, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS97, _M0L13allocate__lenS92);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS97, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS97, _M0L11src__offsetS94);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS97, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS97, _M0L11dst__offsetS95);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS97, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS97, _M0L3lenS93);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS97, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS985 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS96);
    moonbit_decref_cycle_free(_M0L3srcS96);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS97, _M0L6_2atmpS985);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS984
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS97);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS97);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2013
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS984);
    moonbit_decref_cycle_free(_M0L6_2atmpS984);
    return _result_2013;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS102,
  int32_t _M0L13allocate__lenS98,
  int32_t _M0L3lenS99,
  int32_t _M0L11src__offsetS100,
  int32_t _M0L11dst__offsetS101
) {
  int32_t _if__result_2014;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS98 >= 0) {
    if (_M0L3lenS99 >= 0) {
      if (_M0L11src__offsetS100 >= 0) {
        if (_M0L11dst__offsetS101 >= 0) {
          int32_t _M0L6_2atmpS987 = _M0L11src__offsetS100 + _M0L3lenS99;
          int32_t _M0L6_2atmpS988;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS988
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS102);
          if (_M0L6_2atmpS987 <= _M0L6_2atmpS988) {
            int32_t _M0L6_2atmpS986 = _M0L11dst__offsetS101 + _M0L3lenS99;
            _if__result_2014 = _M0L6_2atmpS986 <= _M0L13allocate__lenS98;
          } else {
            _if__result_2014 = 0;
          }
        } else {
          _if__result_2014 = 0;
        }
      } else {
        _if__result_2014 = 0;
      }
    } else {
      _if__result_2014 = 0;
    }
  } else {
    _if__result_2014 = 0;
  }
  if (_if__result_2014) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS98, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS102, _M0L11src__offsetS100, _M0L11dst__offsetS101, _M0L3lenS99);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS103;
    int32_t _M0L6_2atmpS990;
    moonbit_string_t _M0L6_2atmpS989;
    moonbit_string_t* _result_2015;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS103
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS103, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS103, _M0L13allocate__lenS98);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS103, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS103, _M0L11src__offsetS100);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS103, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS103, _M0L11dst__offsetS101);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS103, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS103, _M0L3lenS99);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS103, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS990 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS102);
    moonbit_decref_cycle_free(_M0L3srcS102);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS103, _M0L6_2atmpS990);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS989
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS103);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS103);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2015
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS989);
    moonbit_decref_cycle_free(_M0L6_2atmpS989);
    return _result_2015;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS108,
  int32_t _M0L13allocate__lenS104,
  int32_t _M0L3lenS105,
  int32_t _M0L11src__offsetS106,
  int32_t _M0L11dst__offsetS107
) {
  int32_t _if__result_2016;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS104 >= 0) {
    if (_M0L3lenS105 >= 0) {
      if (_M0L11src__offsetS106 >= 0) {
        if (_M0L11dst__offsetS107 >= 0) {
          int32_t _M0L6_2atmpS992 = _M0L11src__offsetS106 + _M0L3lenS105;
          int32_t _M0L6_2atmpS993;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS993
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS108);
          if (_M0L6_2atmpS992 <= _M0L6_2atmpS993) {
            int32_t _M0L6_2atmpS991 = _M0L11dst__offsetS107 + _M0L3lenS105;
            _if__result_2016 = _M0L6_2atmpS991 <= _M0L13allocate__lenS104;
          } else {
            _if__result_2016 = 0;
          }
        } else {
          _if__result_2016 = 0;
        }
      } else {
        _if__result_2016 = 0;
      }
    } else {
      _if__result_2016 = 0;
    }
  } else {
    _if__result_2016 = 0;
  }
  if (_if__result_2016) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS104, 0, _M0L3srcS108, _M0L11src__offsetS106, _M0L11dst__offsetS107, _M0L3lenS105);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS109;
    int32_t _M0L6_2atmpS995;
    moonbit_string_t _M0L6_2atmpS994;
    struct _M0TUsiE** _result_2017;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS109
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS109, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS109, _M0L13allocate__lenS104);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS109, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS109, _M0L11src__offsetS106);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS109, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS109, _M0L11dst__offsetS107);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS109, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS109, _M0L3lenS105);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS109, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS995 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS108);
    moonbit_decref_cycle_free(_M0L3srcS108);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS109, _M0L6_2atmpS995);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS994
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS109);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS109);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2017
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS994);
    moonbit_decref_cycle_free(_M0L6_2atmpS994);
    return _result_2017;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS87,
  moonbit_string_t _M0L3objS86
) {
  struct _M0TPB6Logger _M0L6_2atmpS978;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS87);
  _M0L6_2atmpS978
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS87
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS86, _M0L6_2atmpS978);
  if (_M0L6_2atmpS978.$1) {
    moonbit_decref(_M0L6_2atmpS978.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS89,
  int32_t _M0L3objS88
) {
  struct _M0TPB6Logger _M0L6_2atmpS979;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS89);
  _M0L6_2atmpS979
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS89
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS88, _M0L6_2atmpS979);
  if (_M0L6_2atmpS979.$1) {
    moonbit_decref(_M0L6_2atmpS979.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS91,
  uint64_t _M0L3objS90
) {
  struct _M0TPB6Logger _M0L6_2atmpS980;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS91);
  _M0L6_2atmpS980
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS91
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS90, _M0L6_2atmpS980);
  if (_M0L6_2atmpS980.$1) {
    moonbit_decref(_M0L6_2atmpS980.$1);
  }
  return 0;
}

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS71,
  int32_t _M0L13allocate__lenS69,
  int32_t _M0L11src__offsetS72,
  int32_t _M0L11dst__offsetS70,
  int32_t _M0L9blit__lenS73
) {
  float* _M0L3dstS68;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS68 = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS69);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS68, _M0L11dst__offsetS70, _M0L3srcS71, _M0L11src__offsetS72, _M0L9blit__lenS73);
  moonbit_decref_cycle_free(_M0L3srcS71);
  return _M0L3dstS68;
}

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t* _M0L3srcS77,
  int32_t _M0L13allocate__lenS75,
  int32_t _M0L11src__offsetS78,
  int32_t _M0L11dst__offsetS76,
  int32_t _M0L9blit__lenS79
) {
  moonbit_string_t* _M0L3dstS74;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS74
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L13allocate__lenS75, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGsE(_M0L3dstS74, _M0L11dst__offsetS76, _M0L3srcS77, _M0L11src__offsetS78, _M0L9blit__lenS79);
  moonbit_decref_cycle_free(_M0L3srcS77);
  return _M0L3dstS74;
}

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE** _M0L3srcS83,
  int32_t _M0L13allocate__lenS81,
  int32_t _M0L11src__offsetS84,
  int32_t _M0L11dst__offsetS82,
  int32_t _M0L9blit__lenS85
) {
  struct _M0TUsiE** _M0L3dstS80;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS80
  = (struct _M0TUsiE**)moonbit_make_ref_array(_M0L13allocate__lenS81, 0);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGUsiEE(_M0L3dstS80, _M0L11dst__offsetS82, _M0L3srcS83, _M0L11src__offsetS84, _M0L9blit__lenS85);
  moonbit_decref_cycle_free(_M0L3srcS83);
  return _M0L3dstS80;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS53,
  int32_t _M0L11dst__offsetS54,
  float* _M0L3srcS55,
  int32_t _M0L11src__offsetS56,
  int32_t _M0L3lenS57
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS55);
  moonbit_incref_cycle_free(_M0L3dstS53);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS53, _M0L11dst__offsetS54, _M0L3srcS55, _M0L11src__offsetS56, _M0L3lenS57, sizeof(float));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS58,
  int32_t _M0L11dst__offsetS59,
  moonbit_string_t* _M0L3srcS60,
  int32_t _M0L11src__offsetS61,
  int32_t _M0L3lenS62
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS60);
  moonbit_incref_cycle_free(_M0L3dstS58);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS58, _M0L11dst__offsetS59, _M0L3srcS60, _M0L11src__offsetS61, _M0L3lenS62);
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS63,
  int32_t _M0L11dst__offsetS64,
  struct _M0TUsiE** _M0L3srcS65,
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
        int32_t _M0L6_2atmpS942 = _M0L11dst__offsetS19 + _M0L1iS21;
        int32_t _M0L6_2atmpS944 = _M0L11src__offsetS20 + _M0L1iS21;
        int32_t _M0L6_2atmpS943;
        int32_t _M0L6_2atmpS945;
        if (
          _M0L6_2atmpS944 < 0
          || _M0L6_2atmpS944 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS943 = (int32_t)_M0L3srcS18[_M0L6_2atmpS944];
        if (
          _M0L6_2atmpS942 < 0
          || _M0L6_2atmpS942 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS942] = _M0L6_2atmpS943;
        _M0L6_2atmpS945 = _M0L1iS21 + 1;
        _M0L1iS21 = _M0L6_2atmpS945;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS18);
        moonbit_decref_cycle_free(_M0L3dstS17);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS950 = _M0L3lenS22 - 1;
    int32_t _M0L1iS24 = _M0L6_2atmpS950;
    while (1) {
      if (_M0L1iS24 >= 0) {
        int32_t _M0L6_2atmpS946 = _M0L11dst__offsetS19 + _M0L1iS24;
        int32_t _M0L6_2atmpS948 = _M0L11src__offsetS20 + _M0L1iS24;
        int32_t _M0L6_2atmpS947;
        int32_t _M0L6_2atmpS949;
        if (
          _M0L6_2atmpS948 < 0
          || _M0L6_2atmpS948 >= Moonbit_array_length(_M0L3srcS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS947 = (int32_t)_M0L3srcS18[_M0L6_2atmpS948];
        if (
          _M0L6_2atmpS946 < 0
          || _M0L6_2atmpS946 >= Moonbit_array_length(_M0L3dstS17)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS17[_M0L6_2atmpS946] = _M0L6_2atmpS947;
        _M0L6_2atmpS949 = _M0L1iS24 - 1;
        _M0L1iS24 = _M0L6_2atmpS949;
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
        int32_t _M0L6_2atmpS951 = _M0L11dst__offsetS28 + _M0L1iS30;
        int32_t _M0L6_2atmpS953 = _M0L11src__offsetS29 + _M0L1iS30;
        float _M0L6_2atmpS952;
        int32_t _M0L6_2atmpS954;
        if (
          _M0L6_2atmpS953 < 0
          || _M0L6_2atmpS953 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS952 = (float)_M0L3srcS27[_M0L6_2atmpS953];
        if (
          _M0L6_2atmpS951 < 0
          || _M0L6_2atmpS951 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS951] = _M0L6_2atmpS952;
        _M0L6_2atmpS954 = _M0L1iS30 + 1;
        _M0L1iS30 = _M0L6_2atmpS954;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS27);
        moonbit_decref_cycle_free(_M0L3dstS26);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS959 = _M0L3lenS31 - 1;
    int32_t _M0L1iS33 = _M0L6_2atmpS959;
    while (1) {
      if (_M0L1iS33 >= 0) {
        int32_t _M0L6_2atmpS955 = _M0L11dst__offsetS28 + _M0L1iS33;
        int32_t _M0L6_2atmpS957 = _M0L11src__offsetS29 + _M0L1iS33;
        float _M0L6_2atmpS956;
        int32_t _M0L6_2atmpS958;
        if (
          _M0L6_2atmpS957 < 0
          || _M0L6_2atmpS957 >= Moonbit_array_length(_M0L3srcS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS956 = (float)_M0L3srcS27[_M0L6_2atmpS957];
        if (
          _M0L6_2atmpS955 < 0
          || _M0L6_2atmpS955 >= Moonbit_array_length(_M0L3dstS26)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS26[_M0L6_2atmpS955] = _M0L6_2atmpS956;
        _M0L6_2atmpS958 = _M0L1iS33 - 1;
        _M0L1iS33 = _M0L6_2atmpS958;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS35,
  int32_t _M0L11dst__offsetS37,
  moonbit_string_t* _M0L3srcS36,
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
        int32_t _M0L6_2atmpS960 = _M0L11dst__offsetS37 + _M0L1iS39;
        int32_t _M0L6_2atmpS962 = _M0L11src__offsetS38 + _M0L1iS39;
        moonbit_string_t _M0L6_2atmpS961;
        moonbit_string_t _M0L6_2aoldS1904;
        int32_t _M0L6_2atmpS963;
        if (
          _M0L6_2atmpS962 < 0
          || _M0L6_2atmpS962 >= Moonbit_array_length(_M0L3srcS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS961 = (moonbit_string_t)_M0L3srcS36[_M0L6_2atmpS962];
        if (
          _M0L6_2atmpS960 < 0
          || _M0L6_2atmpS960 >= Moonbit_array_length(_M0L3dstS35)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1904 = (moonbit_string_t)_M0L3dstS35[_M0L6_2atmpS960];
        moonbit_incref_cycle_free(_M0L6_2atmpS961);
        moonbit_decref_cycle_free(_M0L6_2aoldS1904);
        _M0L3dstS35[_M0L6_2atmpS960] = _M0L6_2atmpS961;
        _M0L6_2atmpS963 = _M0L1iS39 + 1;
        _M0L1iS39 = _M0L6_2atmpS963;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS36);
        moonbit_decref_cycle_free(_M0L3dstS35);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS968 = _M0L3lenS40 - 1;
    int32_t _M0L1iS42 = _M0L6_2atmpS968;
    while (1) {
      if (_M0L1iS42 >= 0) {
        int32_t _M0L6_2atmpS964 = _M0L11dst__offsetS37 + _M0L1iS42;
        int32_t _M0L6_2atmpS966 = _M0L11src__offsetS38 + _M0L1iS42;
        moonbit_string_t _M0L6_2atmpS965;
        moonbit_string_t _M0L6_2aoldS1905;
        int32_t _M0L6_2atmpS967;
        if (
          _M0L6_2atmpS966 < 0
          || _M0L6_2atmpS966 >= Moonbit_array_length(_M0L3srcS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS965 = (moonbit_string_t)_M0L3srcS36[_M0L6_2atmpS966];
        if (
          _M0L6_2atmpS964 < 0
          || _M0L6_2atmpS964 >= Moonbit_array_length(_M0L3dstS35)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1905 = (moonbit_string_t)_M0L3dstS35[_M0L6_2atmpS964];
        moonbit_incref_cycle_free(_M0L6_2atmpS965);
        moonbit_decref_cycle_free(_M0L6_2aoldS1905);
        _M0L3dstS35[_M0L6_2atmpS964] = _M0L6_2atmpS965;
        _M0L6_2atmpS967 = _M0L1iS42 - 1;
        _M0L1iS42 = _M0L6_2atmpS967;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS44,
  int32_t _M0L11dst__offsetS46,
  struct _M0TUsiE** _M0L3srcS45,
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
        int32_t _M0L6_2atmpS969 = _M0L11dst__offsetS46 + _M0L1iS48;
        int32_t _M0L6_2atmpS971 = _M0L11src__offsetS47 + _M0L1iS48;
        struct _M0TUsiE* _M0L6_2atmpS970;
        struct _M0TUsiE* _M0L6_2aoldS1906;
        int32_t _M0L6_2atmpS972;
        if (
          _M0L6_2atmpS971 < 0
          || _M0L6_2atmpS971 >= Moonbit_array_length(_M0L3srcS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS970 = (struct _M0TUsiE*)_M0L3srcS45[_M0L6_2atmpS971];
        if (
          _M0L6_2atmpS969 < 0
          || _M0L6_2atmpS969 >= Moonbit_array_length(_M0L3dstS44)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1906 = (struct _M0TUsiE*)_M0L3dstS44[_M0L6_2atmpS969];
        if (_M0L6_2atmpS970) {
          moonbit_incref_cycle_free(_M0L6_2atmpS970);
        }
        if (_M0L6_2aoldS1906) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1906);
        }
        _M0L3dstS44[_M0L6_2atmpS969] = _M0L6_2atmpS970;
        _M0L6_2atmpS972 = _M0L1iS48 + 1;
        _M0L1iS48 = _M0L6_2atmpS972;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS45);
        moonbit_decref_cycle_free(_M0L3dstS44);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS977 = _M0L3lenS49 - 1;
    int32_t _M0L1iS51 = _M0L6_2atmpS977;
    while (1) {
      if (_M0L1iS51 >= 0) {
        int32_t _M0L6_2atmpS973 = _M0L11dst__offsetS46 + _M0L1iS51;
        int32_t _M0L6_2atmpS975 = _M0L11src__offsetS47 + _M0L1iS51;
        struct _M0TUsiE* _M0L6_2atmpS974;
        struct _M0TUsiE* _M0L6_2aoldS1907;
        int32_t _M0L6_2atmpS976;
        if (
          _M0L6_2atmpS975 < 0
          || _M0L6_2atmpS975 >= Moonbit_array_length(_M0L3srcS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS974 = (struct _M0TUsiE*)_M0L3srcS45[_M0L6_2atmpS975];
        if (
          _M0L6_2atmpS973 < 0
          || _M0L6_2atmpS973 >= Moonbit_array_length(_M0L3dstS44)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1907 = (struct _M0TUsiE*)_M0L3dstS44[_M0L6_2atmpS973];
        if (_M0L6_2atmpS974) {
          moonbit_incref_cycle_free(_M0L6_2atmpS974);
        }
        if (_M0L6_2aoldS1907) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1907);
        }
        _M0L3dstS44[_M0L6_2atmpS973] = _M0L6_2atmpS974;
        _M0L6_2atmpS976 = _M0L1iS51 - 1;
        _M0L1iS51 = _M0L6_2atmpS976;
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

int32_t _M0MPB18UninitializedArray6lengthGfE(float* _M0L4selfS14) {
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
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_37.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S13, _M0L15_2a_2aarg__6389S12);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_38.data);
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

moonbit_string_t _M0FPC15abort5abortGsE(moonbit_string_t _M0L3msgS1) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS1);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

int32_t _M0FPC15abort5abortGuE(moonbit_string_t _M0L3msgS2) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS2);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
  return 0;
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS911) {
  switch (Moonbit_object_tag(_M0L4_2aeS911)) {
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_39.data;
      break;
    }
    
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_40.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS911);
      break;
    }
    
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_41.data;
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_42.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS937,
  struct _M0TPB4Show _M0L8_2aparamS936
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS935 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS937;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS935, _M0L8_2aparamS936);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS934,
  struct _M0TPB4Show _M0L8_2aparamS933
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS932 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS934;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS932, _M0L8_2aparamS933);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS931,
  int32_t _M0L8_2aparamS930
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS929 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS931;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS929, _M0L8_2aparamS930);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS928,
  struct _M0TPC16string10StringView _M0L8_2aparamS927
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS926 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS928;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS926, _M0L8_2aparamS927);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS925,
  moonbit_string_t _M0L8_2aparamS922,
  int32_t _M0L8_2aparamS923,
  int32_t _M0L8_2aparamS924
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS921 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS925;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS921, _M0L8_2aparamS922, _M0L8_2aparamS923, _M0L8_2aparamS924);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS920,
  moonbit_string_t _M0L8_2aparamS919
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS918 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS920;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS918, _M0L8_2aparamS919);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS941;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS904;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS905;
  int32_t _M0L7_2abindS906;
  struct _M0TUsiE** _M0L7_2abindS907;
  int32_t _M0L6_2acntS1912;
  int32_t _M0L2__S908;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS941
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS904
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS904)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _M0L12async__testsS904->$0 = _M0L6_2atmpS941;
  _M0L12async__testsS904->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS905
  = _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS906 = _M0L7_2abindS905->$1;
  _M0L7_2abindS907 = _M0L7_2abindS905->$0;
  _M0L6_2acntS1912
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS905));
  if (_M0L6_2acntS1912 > 1) {
    int32_t _M0L11_2anew__cntS1913 = _M0L6_2acntS1912 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS905), _M0L11_2anew__cntS1913);
    moonbit_incref_cycle_free(_M0L7_2abindS907);
  } else if (_M0L6_2acntS1912 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS905);
  }
  _M0L2__S908 = 0;
  while (1) {
    if (_M0L2__S908 < _M0L7_2abindS906) {
      struct _M0TUsiE* _M0L3argS909 =
        (struct _M0TUsiE*)_M0L7_2abindS907[_M0L2__S908];
      moonbit_string_t _M0L6_2atmpS938 = _M0L3argS909->$0;
      int32_t _M0L6_2atmpS939 = _M0L3argS909->$1;
      int32_t _M0L6_2atmpS940;
      moonbit_incref_cycle_free(_M0L6_2atmpS938);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS904, _M0L6_2atmpS938, _M0L6_2atmpS939);
      moonbit_decref_cycle_free(_M0L6_2atmpS938);
      _M0L6_2atmpS940 = _M0L2__S908 + 1;
      _M0L2__S908 = _M0L6_2atmpS940;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS907);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stdp_kernel\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples28stdp__kernel__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS904);
  moonbit_decref_cycle_free(_M0L12async__testsS904);
  moonbit_flush_cycles();
  return 0;
}