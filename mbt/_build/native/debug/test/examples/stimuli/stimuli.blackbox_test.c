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
struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TP26RiantR8snn__mbt13AdExPostSpike;

struct _M0TWRPC15error5ErrorEs;

struct _M0TP26RiantR8snn__mbt12PoissonLayer;

struct _M0TP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt13AdExParameter;

struct _M0BTPB6Logger;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1213;

struct _M0TUmmmmE;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0TP26RiantR8snn__mbt8Dendrite;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TWRPC15error5ErrorEu;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TP26RiantR8snn__mbt9TripodHet;

struct _M0TPB8MutLocalGiE;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples23stimuli__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0TPB4Show;

struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1208;

struct _M0TP26RiantR8snn__mbt16AdExParameterHet;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples23stimuli__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TPB5ArrayGbE;

struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0TPB8MutLocalGdE;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TPC16string10StringView;

struct _M0TP26RiantR8snn__mbt12BallAndStick;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TPB5ArrayGsE;

struct _M0TWEu;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure {
  moonbit_string_t $0;
  
};

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError {
  moonbit_string_t $0;
  
};

struct _M0TP26RiantR8snn__mbt13AdExPostSpike {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  
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

struct _M0TP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick {
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* $0;
  struct _M0TP26RiantR8snn__mbt12BallAndStick* $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGbE* $3;
  moonbit_string_t $4;
  moonbit_string_t $5;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* $6;
  
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

struct _M0BTPB6Logger {
  int32_t(* $method_0)(void*, moonbit_string_t);
  int32_t(* $method_1)(void*, moonbit_string_t, int32_t, int32_t);
  int32_t(* $method_2)(void*, struct _M0TPC16string10StringView);
  int32_t(* $method_3)(void*, int32_t);
  int32_t(* $method_4)(void*, struct _M0TPB4Show);
  int32_t(* $method_5)(void*, struct _M0TPB4Show);
  
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

struct _M0TP26RiantR8snn__mbt7Xoshiro {
  uint64_t $0;
  uint64_t $1;
  uint64_t $2;
  uint64_t $3;
  
};

struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1213 {
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

struct _M0TP26RiantR8snn__mbt8Dendrite {
  int32_t $0;
  struct _M0TPB5ArrayGfE* $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGfE* $5;
  struct _M0TPB5ArrayGfE* $6;
  struct _M0TPB5ArrayGfE* $7;
  
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

struct _M0TP26RiantR8snn__mbt9TripodHet {
  struct _M0TP26RiantR8snn__mbt16AdExParameterHet* $0;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TP26RiantR8snn__mbt8Dendrite* $4;
  struct _M0TP26RiantR8snn__mbt8Dendrite* $5;
  struct _M0TPB5ArrayGfE* $6;
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
  struct _M0TPB5ArrayGfE* $17;
  struct _M0TPB5ArrayGfE* $18;
  struct _M0TPB5ArrayGfE* $19;
  struct _M0TPB5ArrayGfE* $20;
  float $21;
  float $22;
  float $23;
  float $24;
  float $25;
  float $26;
  int32_t $27;
  struct _M0TPB5ArrayGfE* $28;
  struct _M0TPB5ArrayGfE* $29;
  struct _M0TPB5ArrayGfE* $30;
  struct _M0TPB5ArrayGfE* $31;
  struct _M0TPB5ArrayGbE* $32;
  struct _M0TPB5ArrayGfE* $33;
  struct _M0TPB5ArrayGiE* $34;
  struct _M0TPB5ArrayGfE* $35;
  struct _M0TPB5ArrayGfE* $36;
  struct _M0TPB5ArrayGfE* $37;
  struct _M0TPB5ArrayGfE* $38;
  struct _M0TPB5ArrayGfE* $39;
  
};

struct _M0TPB8MutLocalGiE {
  int32_t $0;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples23stimuli__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
};

struct _M0TPB4Show {
  struct _M0BTPB4Show* $0;
  void* $1;
  
};

struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1208 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0TP26RiantR8snn__mbt16AdExParameterHet {
  struct _M0TPB5ArrayGfE* $0;
  struct _M0TPB5ArrayGfE* $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGfE* $5;
  struct _M0TPB5ArrayGfE* $6;
  struct _M0TPB5ArrayGfE* $7;
  struct _M0TPB5ArrayGfE* $8;
  
};

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error {
  struct moonbit_result_0(* code)(
    struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error*,
    struct _M0TWuEu*,
    struct _M0TWRPC15error5ErrorEu*
  );
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples23stimuli__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
};

struct _M0TPB5ArrayGbE {
  uint8_t* $0;
  int32_t $1;
  
};

struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod {
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* $0;
  struct _M0TP26RiantR8snn__mbt9TripodHet* $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGbE* $3;
  moonbit_string_t $4;
  moonbit_string_t $5;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* $6;
  
};

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err {
  void* $0;
  
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

struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
};

struct _M0TP26RiantR8snn__mbt12BallAndStick {
  struct _M0TP26RiantR8snn__mbt13AdExParameter* $0;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* $1;
  struct _M0TP26RiantR8snn__mbt8Dendrite* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGfE* $4;
  struct _M0TPB5ArrayGfE* $5;
  struct _M0TPB5ArrayGfE* $6;
  struct _M0TPB5ArrayGfE* $7;
  struct _M0TPB5ArrayGfE* $8;
  struct _M0TPB5ArrayGfE* $9;
  struct _M0TPB5ArrayGfE* $10;
  struct _M0TPB5ArrayGfE* $11;
  struct _M0TPB5ArrayGfE* $12;
  float $13;
  float $14;
  float $15;
  float $16;
  float $17;
  float $18;
  int32_t $19;
  struct _M0TPB5ArrayGfE* $20;
  struct _M0TPB5ArrayGfE* $21;
  struct _M0TPB5ArrayGfE* $22;
  struct _M0TPB5ArrayGbE* $23;
  struct _M0TPB5ArrayGfE* $24;
  struct _M0TPB5ArrayGiE* $25;
  struct _M0TPB5ArrayGfE* $26;
  struct _M0TPB5ArrayGfE* $27;
  struct _M0TPB5ArrayGfE* $28;
  struct _M0TPB5ArrayGfE* $29;
  float $30;
  
};

struct _M0KTPB6LoggerTPB13StringBuilder {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
};

struct _M0TPB5ArrayGsE {
  moonbit_string_t* $0;
  int32_t $1;
  
};

struct _M0TWEu {
  int32_t(* code)(struct _M0TWEu*);
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1220(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1213(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1208(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1185(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1178(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples23stimuli__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0MP26RiantR8snn__mbt16AdExParameterHet11homogeneous(
  int32_t,
  struct _M0TP26RiantR8snn__mbt13AdExParameter*
);

int32_t _M0FP26RiantR8snn__mbt18step__ballandstick(
  struct _M0TP26RiantR8snn__mbt12BallAndStick*,
  float
);

int32_t _M0FP26RiantR8snn__mbt24ballandstick__heun__step(
  struct _M0TP26RiantR8snn__mbt12BallAndStick*,
  float,
  int32_t
);

int32_t _M0FP26RiantR8snn__mbt29ballandstick__syn__curr__dend(
  struct _M0TP26RiantR8snn__mbt12BallAndStick*
);

int32_t _M0FP26RiantR8snn__mbt29ballandstick__syn__curr__soma(
  struct _M0TP26RiantR8snn__mbt12BallAndStick*
);

int32_t _M0FP26RiantR8snn__mbt34ballandstick__dend__step__synapses(
  struct _M0TP26RiantR8snn__mbt12BallAndStick*,
  float
);

int32_t _M0FP26RiantR8snn__mbt34ballandstick__soma__step__synapses(
  struct _M0TP26RiantR8snn__mbt12BallAndStick*,
  float
);

struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0MP26RiantR8snn__mbt12BallAndStick3new(
  int32_t,
  struct _M0TP26RiantR8snn__mbt13AdExParameter*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0MP26RiantR8snn__mbt13AdExParameter3new(
  
);

int32_t _M0FP26RiantR8snn__mbt17step__tripod__het(
  struct _M0TP26RiantR8snn__mbt9TripodHet*,
  float
);

int32_t _M0FP26RiantR8snn__mbt23tripod__het__heun__step(
  struct _M0TP26RiantR8snn__mbt9TripodHet*,
  float,
  int32_t
);

int32_t _M0FP26RiantR8snn__mbt29tripod__het__syn__curr__dends(
  struct _M0TP26RiantR8snn__mbt9TripodHet*
);

int32_t _M0FP26RiantR8snn__mbt28tripod__het__syn__curr__soma(
  struct _M0TP26RiantR8snn__mbt9TripodHet*
);

int32_t _M0FP26RiantR8snn__mbt33tripod__het__dend__step__synapses(
  struct _M0TP26RiantR8snn__mbt9TripodHet*,
  float
);

int32_t _M0FP26RiantR8snn__mbt33tripod__het__soma__step__synapses(
  struct _M0TP26RiantR8snn__mbt9TripodHet*,
  float
);

struct _M0TP26RiantR8snn__mbt9TripodHet* _M0MP26RiantR8snn__mbt9TripodHet3new(
  int32_t,
  struct _M0TP26RiantR8snn__mbt16AdExParameterHet*,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TP26RiantR8snn__mbt8Dendrite* _M0MP26RiantR8snn__mbt8Dendrite3new(
  int32_t
);

struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0MP26RiantR8snn__mbt13AdExPostSpike3new(
  
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
);

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t
);

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t);

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t);

struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0MP26RiantR8snn__mbt12PoissonLayer7with__n(
  float,
  int32_t
);

int32_t _M0FP26RiantR8snn__mbt22stimulate__layer__ball(
  struct _M0TP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick*,
  float,
  float
);

struct _M0TPB5ArrayGfE* _M0FP26RiantR8snn__mbt20target__buffer__ball(
  struct _M0TP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick*
);

struct _M0TP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick* _M0MP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick3new(
  struct _M0TP26RiantR8snn__mbt12PoissonLayer*,
  struct _M0TP26RiantR8snn__mbt12BallAndStick*,
  moonbit_string_t,
  moonbit_string_t,
  float,
  float,
  float,
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

int32_t _M0FP26RiantR8snn__mbt24stimulate__layer__tripod(
  struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod*,
  float,
  float
);

int32_t _M0FP26RiantR8snn__mbt15sample__poisson(
  struct _M0TP26RiantR8snn__mbt7Xoshiro*,
  float
);

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro*
);

struct _M0TPB5ArrayGfE* _M0FP26RiantR8snn__mbt22target__buffer__tripod(
  struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod*
);

struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod* _M0MP26RiantR8snn__mbt26PoissonLayerStimulusTripod3new(
  struct _M0TP26RiantR8snn__mbt12PoissonLayer*,
  struct _M0TP26RiantR8snn__mbt9TripodHet*,
  moonbit_string_t,
  moonbit_string_t,
  float,
  float,
  float,
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
} const moonbit_string_literal_19 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[12]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 11, 44, 34, 
    109, 101, 115, 115, 97, 103, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[112]; 
} const moonbit_string_literal_42 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 111, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 115, 116, 105, 109, 117, 108, 105, 
    95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 
    77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 
    118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 
    114, 114, 111, 114, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 
    115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 
    97, 108, 74, 115, 69, 114, 114, 111, 114, 0
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
} const moonbit_string_literal_20 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_18 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[25]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 24, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 114, 101, 115, 117, 108, 116, 34, 
    44, 34, 102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[7]; 
} const moonbit_string_literal_13 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 6, 78, 111, 
    114, 109, 97, 108, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_16 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

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
} const moonbit_string_literal_10 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 100, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_38 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_24 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_14 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 100, 50, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_27 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_39 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_11 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 103, 108, 117, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[114]; 
} const moonbit_string_literal_40 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 113, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 115, 116, 105, 109, 117, 108, 105, 
    95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 115, 116, 46, 
    77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 
    118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 
    112, 84, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 
    101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 
    110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_15 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 100, 49, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[6]; 
} const moonbit_string_literal_9 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 5, 70, 105, 
    120, 101, 100, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_12 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 115, 111, 
    109, 97, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[24]; 
} const moonbit_string_literal_22 =
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
} const moonbit_string_literal_17 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
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
} const moonbit_string_literal_21 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1220$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1220
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[148] =
  {
    sizeof(struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1208)
    / 4, 1,
    offsetof(struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1208, $1)
    / 4
    * 2,
    sizeof(struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1213)
    / 4, 1,
    offsetof(struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1213, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet) / 4, 9,
    offsetof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet, $7) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet, $8) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt12BallAndStick) / 4, 23,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $7) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $8) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $9) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $10) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $11) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $12) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $20) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $21) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $22) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $23) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $24) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $25) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $26) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $27) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $28) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12BallAndStick, $29) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt9TripodHet) / 4, 33,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $7) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $8) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $9) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $10) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $11) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $12) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $13) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $14) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $15) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $16) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $17) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $18) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $19) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $20) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $28) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $29) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $30) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $31) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $32) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $33) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $34) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $35) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $36) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $37) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $38) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt9TripodHet, $39) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt8Dendrite) / 4, 7,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt8Dendrite, $7) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGbE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGbE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt12PoissonLayer) / 4, 3,
    offsetof(struct _M0TP26RiantR8snn__mbt12PoissonLayer, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12PoissonLayer, $6) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt12PoissonLayer, $7) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick)
    / 4, 7,
    offsetof(struct _M0TP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick, $0)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick, $1)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick, $2)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick, $3)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick, $4)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick, $5)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick, $6)
    / 4
    * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod) / 4, 
    7,
    offsetof(struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod, $0)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod, $1)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod, $2)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod, $3)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod, $4)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod, $5)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod, $6)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS3162
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1241,
  moonbit_string_t _M0L8filenameS1210,
  int32_t _M0L5indexS1212
) {
  struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1208* _closure_3198;
  struct _M0TWEu* _M0L13handle__startS1208;
  struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1213* _closure_3199;
  struct _M0TWssbEu* _M0L14handle__resultS1213;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1220;
  void* _M0L11_2atry__errS1235;
  struct moonbit_result_0 _tmp_3201;
  int32_t _handle__error__result_3202;
  int32_t _M0L6_2atmpS3150;
  void* _M0L3errS1236;
  moonbit_string_t _M0L4nameS1238;
  struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1239;
  moonbit_string_t _M0L7_2anameS1240;
  int32_t _M0L6_2acntS3192;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1210);
  _closure_3198
  = (struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1208*)moonbit_malloc(sizeof(struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1208));
  Moonbit_object_header(_closure_3198)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_3198->code
  = &_M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1208;
  _closure_3198->$0 = _M0L5indexS1212;
  _closure_3198->$1 = _M0L8filenameS1210;
  _M0L13handle__startS1208 = (struct _M0TWEu*)_closure_3198;
  moonbit_incref_cycle_free(_M0L8filenameS1210);
  _closure_3199
  = (struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1213*)moonbit_malloc(sizeof(struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1213));
  Moonbit_object_header(_closure_3199)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_3199->code
  = &_M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1213;
  _closure_3199->$0 = _M0L5indexS1212;
  _closure_3199->$1 = _M0L8filenameS1210;
  _M0L14handle__resultS1213 = (struct _M0TWssbEu*)_closure_3199;
  _M0L17error__to__stringS1220
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1220$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _tmp_3201
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1241, _M0L8filenameS1210, _M0L5indexS1212, _M0L13handle__startS1208, _M0L14handle__resultS1213, _M0L17error__to__stringS1220);
  if (_tmp_3201.tag) {
    int32_t const _M0L5_2aokS3159 = _tmp_3201.data.ok;
    _handle__error__result_3202 = _M0L5_2aokS3159;
  } else {
    void* const _M0L6_2aerrS3160 = _tmp_3201.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1220);
    moonbit_decref_cycle_free(_M0L13handle__startS1208);
    _M0L11_2atry__errS1235 = _M0L6_2aerrS3160;
    goto join_1234;
  }
  if (_handle__error__result_3202) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1220);
    moonbit_decref_cycle_free(_M0L13handle__startS1208);
    _M0L6_2atmpS3150 = 1;
  } else {
    struct moonbit_result_0 _tmp_3203;
    int32_t _handle__error__result_3204;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
    _tmp_3203
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1241, _M0L8filenameS1210, _M0L5indexS1212, _M0L13handle__startS1208, _M0L14handle__resultS1213, _M0L17error__to__stringS1220);
    if (_tmp_3203.tag) {
      int32_t const _M0L5_2aokS3157 = _tmp_3203.data.ok;
      _handle__error__result_3204 = _M0L5_2aokS3157;
    } else {
      void* const _M0L6_2aerrS3158 = _tmp_3203.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1220);
      moonbit_decref_cycle_free(_M0L13handle__startS1208);
      _M0L11_2atry__errS1235 = _M0L6_2aerrS3158;
      goto join_1234;
    }
    if (_handle__error__result_3204) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1220);
      moonbit_decref_cycle_free(_M0L13handle__startS1208);
      _M0L6_2atmpS3150 = 1;
    } else {
      struct moonbit_result_0 _tmp_3205;
      int32_t _handle__error__result_3206;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
      _tmp_3205
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1241, _M0L8filenameS1210, _M0L5indexS1212, _M0L13handle__startS1208, _M0L14handle__resultS1213, _M0L17error__to__stringS1220);
      if (_tmp_3205.tag) {
        int32_t const _M0L5_2aokS3155 = _tmp_3205.data.ok;
        _handle__error__result_3206 = _M0L5_2aokS3155;
      } else {
        void* const _M0L6_2aerrS3156 = _tmp_3205.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1220);
        moonbit_decref_cycle_free(_M0L13handle__startS1208);
        _M0L11_2atry__errS1235 = _M0L6_2aerrS3156;
        goto join_1234;
      }
      if (_handle__error__result_3206) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1220);
        moonbit_decref_cycle_free(_M0L13handle__startS1208);
        _M0L6_2atmpS3150 = 1;
      } else {
        struct moonbit_result_0 _tmp_3207;
        int32_t _handle__error__result_3208;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
        _tmp_3207
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1241, _M0L8filenameS1210, _M0L5indexS1212, _M0L13handle__startS1208, _M0L14handle__resultS1213, _M0L17error__to__stringS1220);
        if (_tmp_3207.tag) {
          int32_t const _M0L5_2aokS3153 = _tmp_3207.data.ok;
          _handle__error__result_3208 = _M0L5_2aokS3153;
        } else {
          void* const _M0L6_2aerrS3154 = _tmp_3207.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1220);
          moonbit_decref_cycle_free(_M0L13handle__startS1208);
          _M0L11_2atry__errS1235 = _M0L6_2aerrS3154;
          goto join_1234;
        }
        if (_handle__error__result_3208) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1220);
          moonbit_decref_cycle_free(_M0L13handle__startS1208);
          _M0L6_2atmpS3150 = 1;
        } else {
          struct moonbit_result_0 _tmp_3209;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
          _tmp_3209
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1241, _M0L8filenameS1210, _M0L5indexS1212, _M0L13handle__startS1208, _M0L14handle__resultS1213, _M0L17error__to__stringS1220);
          moonbit_decref_cycle_free(_M0L13handle__startS1208);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1220);
          if (_tmp_3209.tag) {
            int32_t const _M0L5_2aokS3151 = _tmp_3209.data.ok;
            _M0L6_2atmpS3150 = _M0L5_2aokS3151;
          } else {
            void* const _M0L6_2aerrS3152 = _tmp_3209.data.err;
            _M0L11_2atry__errS1235 = _M0L6_2aerrS3152;
            goto join_1234;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS3150) {
    void* _M0L126RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS3161 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L126RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS3161)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L126RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS3161)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1235
    = _M0L126RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS3161;
    goto join_1234;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1213);
  }
  goto joinlet_3200;
  join_1234:;
  _M0L3errS1236 = _M0L11_2atry__errS1235;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1239
  = (struct _M0DTPC15error5Error126RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1236;
  _M0L7_2anameS1240 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1239->$0;
  _M0L6_2acntS3192
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1239));
  if (_M0L6_2acntS3192 > 1) {
    int32_t _M0L11_2anew__cntS3193 = _M0L6_2acntS3192 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1239), _M0L11_2anew__cntS3193);
    moonbit_incref_cycle_free(_M0L7_2anameS1240);
  } else if (_M0L6_2acntS3192 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1239);
  }
  _M0L4nameS1238 = _M0L7_2anameS1240;
  goto join_1237;
  goto joinlet_3210;
  join_1237:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1213(_M0L14handle__resultS1213, _M0L4nameS1238, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1213);
  moonbit_decref_cycle_free(_M0L4nameS1238);
  joinlet_3210:;
  joinlet_3200:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1220(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS3149,
  void* _M0L3errS1221
) {
  void* _M0L1eS1223;
  moonbit_string_t _M0L1eS1225;
  moonbit_string_t _result_3213;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1221)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1226 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1221;
      moonbit_string_t _M0L4_2aeS1227 = _M0L10_2aFailureS1226->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1227);
      _M0L1eS1225 = _M0L4_2aeS1227;
      goto join_1224;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1228 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1221;
      moonbit_string_t _M0L4_2aeS1229 = _M0L15_2aInspectErrorS1228->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1229);
      _M0L1eS1225 = _M0L4_2aeS1229;
      goto join_1224;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1230 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1221;
      moonbit_string_t _M0L4_2aeS1231 = _M0L16_2aSnapshotErrorS1230->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1231);
      _M0L1eS1225 = _M0L4_2aeS1231;
      goto join_1224;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1232 =
        (struct _M0DTPC15error5Error124RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1221;
      moonbit_string_t _M0L4_2aeS1233 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1232->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1233);
      _M0L1eS1225 = _M0L4_2aeS1233;
      goto join_1224;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1221);
      _M0L1eS1223 = _M0L3errS1221;
      goto join_1222;
      break;
    }
  }
  join_1224:;
  return _M0L1eS1225;
  join_1222:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _result_3213 = _M0FP15Error10to__string(_M0L1eS1223);
  moonbit_decref_cycle_free(_M0L1eS1223);
  return _result_3213;
}

int32_t _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1213(
  struct _M0TWssbEu* _M0L6_2aenvS3146,
  moonbit_string_t _M0L10__testnameS1214,
  moonbit_string_t _M0L7messageS1215,
  int32_t _M0L7skippedS1216
) {
  struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1213* _M0L14_2acasted__envS3147;
  moonbit_string_t _M0L8filenameS1210;
  int32_t _M0L5indexS1212;
  moonbit_string_t _M0L10file__nameS1217;
  moonbit_string_t _M0L7messageS1218;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1219;
  moonbit_string_t _M0L6_2atmpS3148;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS3147
  = (struct _M0R128_24RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1213*)_M0L6_2aenvS3146;
  _M0L8filenameS1210 = _M0L14_2acasted__envS3147->$1;
  _M0L5indexS1212 = _M0L14_2acasted__envS3147->$0;
  if (!_M0L7skippedS1216 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1217
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1210, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1218
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1215, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1219
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1219, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1219, _M0L10file__nameS1217);
  moonbit_decref_cycle_free(_M0L10file__nameS1217);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1219, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1219, _M0L5indexS1212);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1219, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1219, _M0L7messageS1218);
  moonbit_decref_cycle_free(_M0L7messageS1218);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1219, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS3148
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1219);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1219);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS3148);
  moonbit_decref_cycle_free(_M0L6_2atmpS3148);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1208(
  struct _M0TWEu* _M0L6_2aenvS3143
) {
  struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1208* _M0L14_2acasted__envS3144;
  moonbit_string_t _M0L8filenameS1210;
  int32_t _M0L5indexS1212;
  moonbit_string_t _M0L10file__nameS1209;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1211;
  moonbit_string_t _M0L6_2atmpS3145;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS3144
  = (struct _M0R127_24RiantR_2fsnn__mbt_2fexamples_2fstimuli__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1208*)_M0L6_2aenvS3143;
  _M0L8filenameS1210 = _M0L14_2acasted__envS3144->$1;
  _M0L5indexS1212 = _M0L14_2acasted__envS3144->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1209
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1210, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1211
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1211, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1211, _M0L10file__nameS1209);
  moonbit_decref_cycle_free(_M0L10file__nameS1209);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1211, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1211, _M0L5indexS1212);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1211, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS3145
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1211);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1211);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS3145);
  moonbit_decref_cycle_free(_M0L6_2atmpS3145);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1178;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1185;
  struct _M0TUsiE** _M0L6_2atmpS3142;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1192;
  moonbit_string_t* _M0L9cli__argsS1193;
  moonbit_string_t _M0L6_2atmpS3141;
  moonbit_string_t _M0L6_2atmpS3140;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1194;
  int32_t _M0L7_2abindS1195;
  moonbit_string_t* _M0L7_2abindS1196;
  int32_t _M0L6_2acntS3194;
  int32_t _M0L2__S1197;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1178 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1185 = 0;
  _M0L6_2atmpS3142 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1192
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1192)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1192->$0 = _M0L6_2atmpS3142;
  _M0L16file__and__indexS1192->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1193
  = _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1193)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS3141 = (moonbit_string_t)_M0L9cli__argsS1193[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS3141);
  moonbit_decref_cycle_free(_M0L9cli__argsS1193);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS3140
  = _M0MP46RiantR8snn__mbt8examples23stimuli__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS3141);
  moonbit_decref_cycle_free(_M0L6_2atmpS3141);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1194
  = _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1185(_M0L51moonbit__test__driver__internal__split__mbt__stringS1185, _M0L6_2atmpS3140, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS3140);
  _M0L7_2abindS1195 = _M0L10test__argsS1194->$1;
  _M0L7_2abindS1196 = _M0L10test__argsS1194->$0;
  _M0L6_2acntS3194
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1194));
  if (_M0L6_2acntS3194 > 1) {
    int32_t _M0L11_2anew__cntS3195 = _M0L6_2acntS3194 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1194), _M0L11_2anew__cntS3195);
    moonbit_incref_cycle_free(_M0L7_2abindS1196);
  } else if (_M0L6_2acntS3194 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1194);
  }
  _M0L2__S1197 = 0;
  while (1) {
    if (_M0L2__S1197 < _M0L7_2abindS1195) {
      moonbit_string_t _M0L3argS1198 =
        (moonbit_string_t)_M0L7_2abindS1196[_M0L2__S1197];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1199;
      moonbit_string_t _M0L4fileS1200;
      moonbit_string_t _M0L5rangeS1201;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1202;
      moonbit_string_t _M0L6_2atmpS3138;
      int32_t _M0L5startS1203;
      moonbit_string_t _M0L6_2atmpS3137;
      int32_t _M0L3endS1204;
      int32_t _M0L1iS1205;
      int32_t _M0L6_2atmpS3139;
      moonbit_incref_cycle_free(_M0L3argS1198);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1199
      = _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1185(_M0L51moonbit__test__driver__internal__split__mbt__stringS1185, _M0L3argS1198, 58);
      moonbit_decref_cycle_free(_M0L3argS1198);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1200
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1199, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1201
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1199, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1199);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1202
      = _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1185(_M0L51moonbit__test__driver__internal__split__mbt__stringS1185, _M0L5rangeS1201, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1201);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS3138
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1202, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1203
      = _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1178(_M0L45moonbit__test__driver__internal__parse__int__S1178, _M0L6_2atmpS3138);
      moonbit_decref_cycle_free(_M0L6_2atmpS3138);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS3137
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1202, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1202);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1204
      = _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1178(_M0L45moonbit__test__driver__internal__parse__int__S1178, _M0L6_2atmpS3137);
      moonbit_decref_cycle_free(_M0L6_2atmpS3137);
      _M0L1iS1205 = _M0L5startS1203;
      while (1) {
        if (_M0L1iS1205 < _M0L3endS1204) {
          struct _M0TUsiE* _M0L8_2atupleS3135;
          int32_t _M0L6_2atmpS3136;
          moonbit_incref_cycle_free(_M0L4fileS1200);
          _M0L8_2atupleS3135
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS3135)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS3135->$0 = _M0L4fileS1200;
          _M0L8_2atupleS3135->$1 = _M0L1iS1205;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1192, _M0L8_2atupleS3135);
          _M0L6_2atmpS3136 = _M0L1iS1205 + 1;
          _M0L1iS1205 = _M0L6_2atmpS3136;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1200);
        }
        break;
      }
      _M0L6_2atmpS3139 = _M0L2__S1197 + 1;
      _M0L2__S1197 = _M0L6_2atmpS3139;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1196);
    }
    break;
  }
  return _M0L16file__and__indexS1192;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1185(
  int32_t _M0L6_2aenvS3116,
  moonbit_string_t _M0L1sS1186,
  int32_t _M0L3sepS1187
) {
  moonbit_string_t* _M0L6_2atmpS3134;
  struct _M0TPB5ArrayGsE* _M0L3resS1188;
  struct _M0TPB8MutLocalGiE* _M0L1iS1189;
  struct _M0TPB8MutLocalGiE* _M0L5startS1190;
  int32_t _M0L3valS3129;
  int32_t _M0L6_2atmpS3130;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS3134 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1188
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1188)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1188->$0 = _M0L6_2atmpS3134;
  _M0L3resS1188->$1 = 0;
  _M0L1iS1189
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1189)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1189->$0 = 0;
  _M0L5startS1190
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1190)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1190->$0 = 0;
  while (1) {
    int32_t _M0L3valS3117 = _M0L1iS1189->$0;
    int32_t _M0L6_2atmpS3118 = Moonbit_array_length(_M0L1sS1186);
    if (_M0L3valS3117 < _M0L6_2atmpS3118) {
      int32_t _M0L3valS3121 = _M0L1iS1189->$0;
      int32_t _M0L6_2atmpS3120;
      int32_t _M0L6_2atmpS3119;
      int32_t _M0L3valS3128;
      int32_t _M0L6_2atmpS3127;
      if (
        _M0L3valS3121 < 0
        || _M0L3valS3121 >= Moonbit_array_length(_M0L1sS1186)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS3120 = _M0L1sS1186[_M0L3valS3121];
      _M0L6_2atmpS3119 = _M0L6_2atmpS3120;
      if (_M0L6_2atmpS3119 == _M0L3sepS1187) {
        int32_t _M0L3valS3123 = _M0L5startS1190->$0;
        int32_t _M0L3valS3124 = _M0L1iS1189->$0;
        moonbit_string_t _M0L6_2atmpS3122;
        int32_t _M0L3valS3126;
        int32_t _M0L6_2atmpS3125;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS3122
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1186, _M0L3valS3123, _M0L3valS3124);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1188, _M0L6_2atmpS3122);
        _M0L3valS3126 = _M0L1iS1189->$0;
        _M0L6_2atmpS3125 = _M0L3valS3126 + 1;
        _M0L5startS1190->$0 = _M0L6_2atmpS3125;
      }
      _M0L3valS3128 = _M0L1iS1189->$0;
      _M0L6_2atmpS3127 = _M0L3valS3128 + 1;
      _M0L1iS1189->$0 = _M0L6_2atmpS3127;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1189);
    }
    break;
  }
  _M0L3valS3129 = _M0L5startS1190->$0;
  _M0L6_2atmpS3130 = Moonbit_array_length(_M0L1sS1186);
  if (_M0L3valS3129 < _M0L6_2atmpS3130) {
    int32_t _M0L3valS3132 = _M0L5startS1190->$0;
    int32_t _M0L6_2atmpS3133;
    moonbit_string_t _M0L6_2atmpS3131;
    moonbit_decref_cycle_free(_M0L5startS1190);
    _M0L6_2atmpS3133 = Moonbit_array_length(_M0L1sS1186);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS3131
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1186, _M0L3valS3132, _M0L6_2atmpS3133);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1188, _M0L6_2atmpS3131);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1190);
  }
  return _M0L3resS1188;
}

int32_t _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1178(
  int32_t _M0L6_2aenvS3109,
  moonbit_string_t _M0L1sS1179
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1180;
  int32_t _M0L3lenS1181;
  int32_t _M0L7_2abindS1182;
  int32_t _M0L1iS1183;
  int32_t _result_3218;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1180
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1180)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1180->$0 = 0;
  _M0L3lenS1181 = Moonbit_array_length(_M0L1sS1179);
  _M0L7_2abindS1182 = 0;
  _M0L1iS1183 = _M0L7_2abindS1182;
  while (1) {
    if (_M0L1iS1183 < _M0L3lenS1181) {
      int32_t _M0L3valS3114 = _M0L3resS1180->$0;
      int32_t _M0L6_2atmpS3111 = _M0L3valS3114 * 10;
      int32_t _M0L6_2atmpS3113;
      int32_t _M0L6_2atmpS3112;
      int32_t _M0L6_2atmpS3110;
      int32_t _M0L6_2atmpS3115;
      if (
        _M0L1iS1183 < 0 || _M0L1iS1183 >= Moonbit_array_length(_M0L1sS1179)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS3113 = _M0L1sS1179[_M0L1iS1183];
      _M0L6_2atmpS3112 = _M0L6_2atmpS3113 - 48;
      _M0L6_2atmpS3110 = _M0L6_2atmpS3111 + _M0L6_2atmpS3112;
      _M0L3resS1180->$0 = _M0L6_2atmpS3110;
      _M0L6_2atmpS3115 = _M0L1iS1183 + 1;
      _M0L1iS1183 = _M0L6_2atmpS3115;
      continue;
    }
    break;
  }
  _result_3218 = _M0L3resS1180->$0;
  moonbit_decref_cycle_free(_M0L3resS1180);
  return _result_3218;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples23stimuli__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1177
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1177);
  return _M0L4selfS1177;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1147,
  moonbit_string_t _M0L12_2adiscard__S1148,
  int32_t _M0L12_2adiscard__S1149,
  struct _M0TWEu* _M0L12_2adiscard__S1150,
  struct _M0TWssbEu* _M0L12_2adiscard__S1151,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1152
) {
  struct moonbit_result_0 _result_3219;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _result_3219.tag = 1;
  _result_3219.data.ok = 0;
  return _result_3219;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1153,
  moonbit_string_t _M0L12_2adiscard__S1154,
  int32_t _M0L12_2adiscard__S1155,
  struct _M0TWEu* _M0L12_2adiscard__S1156,
  struct _M0TWssbEu* _M0L12_2adiscard__S1157,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1158
) {
  struct moonbit_result_0 _result_3220;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _result_3220.tag = 1;
  _result_3220.data.ok = 0;
  return _result_3220;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1159,
  moonbit_string_t _M0L12_2adiscard__S1160,
  int32_t _M0L12_2adiscard__S1161,
  struct _M0TWEu* _M0L12_2adiscard__S1162,
  struct _M0TWssbEu* _M0L12_2adiscard__S1163,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1164
) {
  struct moonbit_result_0 _result_3221;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _result_3221.tag = 1;
  _result_3221.data.ok = 0;
  return _result_3221;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1165,
  moonbit_string_t _M0L12_2adiscard__S1166,
  int32_t _M0L12_2adiscard__S1167,
  struct _M0TWEu* _M0L12_2adiscard__S1168,
  struct _M0TWssbEu* _M0L12_2adiscard__S1169,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1170
) {
  struct moonbit_result_0 _result_3222;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _result_3222.tag = 1;
  _result_3222.data.ok = 0;
  return _result_3222;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1171,
  moonbit_string_t _M0L12_2adiscard__S1172,
  int32_t _M0L12_2adiscard__S1173,
  struct _M0TWEu* _M0L12_2adiscard__S1174,
  struct _M0TWssbEu* _M0L12_2adiscard__S1175,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1176
) {
  struct moonbit_result_0 _result_3223;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _result_3223.tag = 1;
  _result_3223.data.ok = 0;
  return _result_3223;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1146
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0MP26RiantR8snn__mbt16AdExParameterHet11homogeneous(
  int32_t _M0L1nS1114,
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L1pS1115
) {
  float _M0L2vtS3108;
  struct _M0TPB5ArrayGfE* _M0L7vt__arrS1113;
  float _M0L2vrS3107;
  struct _M0TPB5ArrayGfE* _M0L7vr__arrS1116;
  float _M0L2elS3106;
  struct _M0TPB5ArrayGfE* _M0L7el__arrS1117;
  float _M0L2tmS3105;
  struct _M0TPB5ArrayGfE* _M0L7tm__arrS1118;
  float _M0L1rS3104;
  struct _M0TPB5ArrayGfE* _M0L6r__arrS1119;
  float _M0L9dt__slopeS3103;
  struct _M0TPB5ArrayGfE* _M0L14dt__slope__arrS1120;
  float _M0L2twS3102;
  struct _M0TPB5ArrayGfE* _M0L7tw__arrS1121;
  float _M0L1aS3101;
  struct _M0TPB5ArrayGfE* _M0L6a__arrS1122;
  float _M0L1bS3100;
  struct _M0TPB5ArrayGfE* _M0L6b__arrS1123;
  struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _block_3224;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L2vtS3108 = _M0L1pS1115->$2;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7vt__arrS1113 = _M0MPC15array5Array4makeGfE(_M0L1nS1114, _M0L2vtS3108);
  _M0L2vrS3107 = _M0L1pS1115->$3;
  #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7vr__arrS1116 = _M0MPC15array5Array4makeGfE(_M0L1nS1114, _M0L2vrS3107);
  _M0L2elS3106 = _M0L1pS1115->$4;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7el__arrS1117 = _M0MPC15array5Array4makeGfE(_M0L1nS1114, _M0L2elS3106);
  _M0L2tmS3105 = _M0L1pS1115->$5;
  #line 302 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7tm__arrS1118 = _M0MPC15array5Array4makeGfE(_M0L1nS1114, _M0L2tmS3105);
  _M0L1rS3104 = _M0L1pS1115->$6;
  #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L6r__arrS1119 = _M0MPC15array5Array4makeGfE(_M0L1nS1114, _M0L1rS3104);
  _M0L9dt__slopeS3103 = _M0L1pS1115->$7;
  #line 304 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L14dt__slope__arrS1120
  = _M0MPC15array5Array4makeGfE(_M0L1nS1114, _M0L9dt__slopeS3103);
  _M0L2twS3102 = _M0L1pS1115->$8;
  #line 305 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L7tw__arrS1121 = _M0MPC15array5Array4makeGfE(_M0L1nS1114, _M0L2twS3102);
  _M0L1aS3101 = _M0L1pS1115->$9;
  #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L6a__arrS1122 = _M0MPC15array5Array4makeGfE(_M0L1nS1114, _M0L1aS3101);
  _M0L1bS3100 = _M0L1pS1115->$10;
  #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L6b__arrS1123 = _M0MPC15array5Array4makeGfE(_M0L1nS1114, _M0L1bS3100);
  _block_3224
  = (struct _M0TP26RiantR8snn__mbt16AdExParameterHet*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt16AdExParameterHet));
  Moonbit_object_header(_block_3224)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_3224->$0 = _M0L7vt__arrS1113;
  _block_3224->$1 = _M0L7vr__arrS1116;
  _block_3224->$2 = _M0L7el__arrS1117;
  _block_3224->$3 = _M0L7tm__arrS1118;
  _block_3224->$4 = _M0L6r__arrS1119;
  _block_3224->$5 = _M0L14dt__slope__arrS1120;
  _block_3224->$6 = _M0L7tw__arrS1121;
  _block_3224->$7 = _M0L6a__arrS1122;
  _block_3224->$8 = _M0L6b__arrS1123;
  return _block_3224;
}

int32_t _M0FP26RiantR8snn__mbt18step__ballandstick(
  struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L1pS1092,
  float _M0L2dtS1103
) {
  int32_t _M0L1nS1091;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L3p__S1093;
  float _M0L2vtS1094;
  float _M0L2vrS1095;
  float _M0L1bS1096;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS3099;
  float _M0L2atS1097;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS3098;
  float _M0L6tau__aS1098;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS3097;
  float _M0L11tabs__constS1099;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS3096;
  float _M0L2upS1100;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS3095;
  float _M0L12ap__membraneS1101;
  float _M0L6_2atmpS3094;
  float _M0L6_2atmpS3093;
  int32_t _M0L11tabs__stepsS1102;
  int32_t _M0L7_2abindS1104;
  int32_t _M0L7_2abindS1105;
  int32_t _M0L1iS1106;
  int32_t _M0L7_2abindS1108;
  int32_t _M0L1kS1109;
  #line 276 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L1nS1091 = _M0L1pS1092->$19;
  _M0L3p__S1093 = _M0L1pS1092->$0;
  _M0L2vtS1094 = _M0L3p__S1093->$2;
  _M0L2vrS1095 = _M0L3p__S1093->$3;
  _M0L1bS1096 = _M0L3p__S1093->$10;
  _M0L11soma__spikeS3099 = _M0L1pS1092->$1;
  _M0L2atS1097 = _M0L11soma__spikeS3099->$0;
  _M0L11soma__spikeS3098 = _M0L1pS1092->$1;
  _M0L6tau__aS1098 = _M0L11soma__spikeS3098->$1;
  _M0L11soma__spikeS3097 = _M0L1pS1092->$1;
  _M0L11tabs__constS1099 = _M0L11soma__spikeS3097->$3;
  _M0L11soma__spikeS3096 = _M0L1pS1092->$1;
  _M0L2upS1100 = _M0L11soma__spikeS3096->$4;
  _M0L11soma__spikeS3095 = _M0L1pS1092->$1;
  _M0L12ap__membraneS1101 = _M0L11soma__spikeS3095->$2;
  _M0L6_2atmpS3094 = _M0L2upS1100 + _M0L11tabs__constS1099;
  _M0L6_2atmpS3093 = _M0L6_2atmpS3094 / _M0L2dtS1103;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L11tabs__stepsS1102 = _M0MPC15float5Float7to__int(_M0L6_2atmpS3093);
  #line 290 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0FP26RiantR8snn__mbt34ballandstick__soma__step__synapses(_M0L1pS1092, _M0L2dtS1103);
  #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0FP26RiantR8snn__mbt34ballandstick__dend__step__synapses(_M0L1pS1092, _M0L2dtS1103);
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0FP26RiantR8snn__mbt29ballandstick__syn__curr__soma(_M0L1pS1092);
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0FP26RiantR8snn__mbt29ballandstick__syn__curr__dend(_M0L1pS1092);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0FP26RiantR8snn__mbt24ballandstick__heun__step(_M0L1pS1092, _M0L2dtS1103, 0);
  _M0L7_2abindS1104 = 0;
  _M0L7_2abindS1105 = _M0L1nS1091 * 3;
  _M0L1iS1106 = _M0L7_2abindS1104;
  while (1) {
    if (_M0L1iS1106 < _M0L7_2abindS1105) {
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2987 = _M0L1pS1092->$27;
      struct _M0TPB5ArrayGfE* _M0L2dvS2989 = _M0L1pS1092->$26;
      float _M0L6_2atmpS2988;
      int32_t _M0L6_2atmpS2990;
      #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2988 = _M0MPC15array5Array2atGfE(_M0L2dvS2989, _M0L1iS1106);
      #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L8dv__tempS2987, _M0L1iS1106, _M0L6_2atmpS2988);
      _M0L6_2atmpS2990 = _M0L1iS1106 + 1;
      _M0L1iS1106 = _M0L6_2atmpS2990;
      continue;
    }
    break;
  }
  #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0FP26RiantR8snn__mbt24ballandstick__heun__step(_M0L1pS1092, _M0L2dtS1103, 1);
  _M0L7_2abindS1108 = 0;
  _M0L1kS1109 = _M0L7_2abindS1108;
  while (1) {
    if (_M0L1kS1109 < _M0L1nS1091) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS2992 = _M0L1pS1092->$25;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2995 = _M0L1pS1092->$25;
      int32_t _M0L6_2atmpS2994;
      int32_t _M0L6_2atmpS2993;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2996;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS3004;
      float _M0L6_2atmpS2998;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS3003;
      float _M0L6_2atmpS3002;
      float _M0L6_2atmpS3001;
      float _M0L6_2atmpS3000;
      float _M0L6_2atmpS2999;
      float _M0L6_2atmpS2997;
      struct _M0TPB5ArrayGiE* _M0L4tabsS3006;
      int32_t _M0L6_2atmpS3005;
      struct _M0TPB5ArrayGfE* _M0L4v__sS3092;
      float _M0L6_2atmpS3087;
      struct _M0TPB5ArrayGfE* _M0L2dvS3090;
      int32_t _M0L6_2atmpS3091;
      float _M0L6_2atmpS3089;
      float _M0L6_2atmpS3088;
      float _M0L10v__s__predS1112;
      struct _M0TPB5ArrayGbE* _M0L4fireS3026;
      int32_t _M0L6_2atmpS3027;
      struct _M0TPB5ArrayGbE* _M0L4fireS3028;
      struct _M0TPB5ArrayGfE* _M0L4v__sS3044;
      struct _M0TPB5ArrayGfE* _M0L4v__sS3056;
      float _M0L6_2atmpS3046;
      float _M0L6_2atmpS3048;
      struct _M0TPB5ArrayGfE* _M0L2dvS3054;
      int32_t _M0L6_2atmpS3055;
      float _M0L6_2atmpS3050;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS3052;
      int32_t _M0L6_2atmpS3053;
      float _M0L6_2atmpS3051;
      float _M0L6_2atmpS3049;
      float _M0L6_2atmpS3047;
      float _M0L6_2atmpS3045;
      struct _M0TPB5ArrayGfE* _M0L4v__dS3057;
      struct _M0TPB5ArrayGfE* _M0L4v__dS3071;
      float _M0L6_2atmpS3059;
      float _M0L6_2atmpS3061;
      struct _M0TPB5ArrayGfE* _M0L2dvS3068;
      int32_t _M0L6_2atmpS3070;
      int32_t _M0L6_2atmpS3069;
      float _M0L6_2atmpS3063;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS3065;
      int32_t _M0L6_2atmpS3067;
      int32_t _M0L6_2atmpS3066;
      float _M0L6_2atmpS3064;
      float _M0L6_2atmpS3062;
      float _M0L6_2atmpS3060;
      float _M0L6_2atmpS3058;
      struct _M0TPB5ArrayGfE* _M0L4w__sS3072;
      struct _M0TPB5ArrayGfE* _M0L4w__sS3086;
      float _M0L6_2atmpS3074;
      float _M0L6_2atmpS3076;
      struct _M0TPB5ArrayGfE* _M0L2dvS3083;
      int32_t _M0L6_2atmpS3085;
      int32_t _M0L6_2atmpS3084;
      float _M0L6_2atmpS3078;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS3080;
      int32_t _M0L6_2atmpS3082;
      int32_t _M0L6_2atmpS3081;
      float _M0L6_2atmpS3079;
      float _M0L6_2atmpS3077;
      float _M0L6_2atmpS3075;
      float _M0L6_2atmpS3073;
      int32_t _M0L6_2atmpS2991;
      #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2994
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2995, _M0L1kS1109);
      _M0L6_2atmpS2993 = _M0L6_2atmpS2994 - 1;
      #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2992, _M0L1kS1109, _M0L6_2atmpS2993);
      _M0L9thresholdS2996 = _M0L1pS1092->$24;
      _M0L9thresholdS3004 = _M0L1pS1092->$24;
      #line 310 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2998
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS3004, _M0L1kS1109);
      _M0L9thresholdS3003 = _M0L1pS1092->$24;
      #line 310 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS3002
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS3003, _M0L1kS1109);
      _M0L6_2atmpS3001 = _M0L2vtS1094 - _M0L6_2atmpS3002;
      _M0L6_2atmpS3000 = _M0L2dtS1103 * _M0L6_2atmpS3001;
      _M0L6_2atmpS2999 = _M0L6_2atmpS3000 / _M0L6tau__aS1098;
      _M0L6_2atmpS2997 = _M0L6_2atmpS2998 + _M0L6_2atmpS2999;
      #line 310 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS2996, _M0L1kS1109, _M0L6_2atmpS2997);
      _M0L4tabsS3006 = _M0L1pS1092->$25;
      #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS3005
      = _M0MPC15array5Array2atGiE(_M0L4tabsS3006, _M0L1kS1109);
      if (_M0L6_2atmpS3005 > 0) {
        struct _M0TPB5ArrayGfE* _M0L4v__sS3007 = _M0L1pS1092->$20;
        struct _M0TPB5ArrayGfE* _M0L4v__dS3008;
        struct _M0TPB5ArrayGfE* _M0L4v__dS3025;
        float _M0L6_2atmpS3010;
        struct _M0TPB5ArrayGfE* _M0L4v__sS3024;
        float _M0L6_2atmpS3021;
        struct _M0TPB5ArrayGfE* _M0L4v__dS3023;
        float _M0L6_2atmpS3022;
        float _M0L6_2atmpS3020;
        float _M0L6_2atmpS3016;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L4dendS3019;
        struct _M0TPB5ArrayGfE* _M0L3gaxS3018;
        float _M0L6_2atmpS3017;
        float _M0L6_2atmpS3012;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L4dendS3015;
        struct _M0TPB5ArrayGfE* _M0L1cS3014;
        float _M0L6_2atmpS3013;
        float _M0L6_2atmpS3011;
        float _M0L6_2atmpS3009;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0MPC15array5Array3setGfE(_M0L4v__sS3007, _M0L1kS1109, _M0L2vrS1095);
        _M0L4v__dS3008 = _M0L1pS1092->$22;
        _M0L4v__dS3025 = _M0L1pS1092->$22;
        #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0L6_2atmpS3010
        = _M0MPC15array5Array2atGfE(_M0L4v__dS3025, _M0L1kS1109);
        _M0L4v__sS3024 = _M0L1pS1092->$20;
        #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0L6_2atmpS3021
        = _M0MPC15array5Array2atGfE(_M0L4v__sS3024, _M0L1kS1109);
        _M0L4v__dS3023 = _M0L1pS1092->$22;
        #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0L6_2atmpS3022
        = _M0MPC15array5Array2atGfE(_M0L4v__dS3023, _M0L1kS1109);
        _M0L6_2atmpS3020 = _M0L6_2atmpS3021 - _M0L6_2atmpS3022;
        _M0L6_2atmpS3016 = _M0L2dtS1103 * _M0L6_2atmpS3020;
        _M0L4dendS3019 = _M0L1pS1092->$2;
        _M0L3gaxS3018 = _M0L4dendS3019->$3;
        #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0L6_2atmpS3017
        = _M0MPC15array5Array2atGfE(_M0L3gaxS3018, _M0L1kS1109);
        _M0L6_2atmpS3012 = _M0L6_2atmpS3016 * _M0L6_2atmpS3017;
        _M0L4dendS3015 = _M0L1pS1092->$2;
        _M0L1cS3014 = _M0L4dendS3015->$2;
        #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0L6_2atmpS3013
        = _M0MPC15array5Array2atGfE(_M0L1cS3014, _M0L1kS1109);
        _M0L6_2atmpS3011 = _M0L6_2atmpS3012 / _M0L6_2atmpS3013;
        _M0L6_2atmpS3009 = _M0L6_2atmpS3010 + _M0L6_2atmpS3011;
        #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0MPC15array5Array3setGfE(_M0L4v__dS3008, _M0L1kS1109, _M0L6_2atmpS3009);
        goto join_1110;
      }
      _M0L4v__sS3092 = _M0L1pS1092->$20;
      #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS3087
      = _M0MPC15array5Array2atGfE(_M0L4v__sS3092, _M0L1kS1109);
      _M0L2dvS3090 = _M0L1pS1092->$26;
      _M0L6_2atmpS3091 = _M0L1kS1109 * 3;
      #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS3089
      = _M0MPC15array5Array2atGfE(_M0L2dvS3090, _M0L6_2atmpS3091);
      _M0L6_2atmpS3088 = _M0L6_2atmpS3089 * _M0L2dtS1103;
      _M0L10v__s__predS1112 = _M0L6_2atmpS3087 + _M0L6_2atmpS3088;
      _M0L4fireS3026 = _M0L1pS1092->$23;
      _M0L6_2atmpS3027 = _M0L10v__s__predS1112 >= -0x1.4p+3f;
      #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3026, _M0L1kS1109, _M0L6_2atmpS3027);
      _M0L4fireS3028 = _M0L1pS1092->$23;
      #line 332 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3028, _M0L1kS1109)) {
        struct _M0TPB5ArrayGfE* _M0L2dvS3029 = _M0L1pS1092->$26;
        int32_t _M0L6_2atmpS3030 = _M0L1kS1109 * 3;
        struct _M0TPB5ArrayGfE* _M0L4v__sS3033 = _M0L1pS1092->$20;
        float _M0L6_2atmpS3032;
        float _M0L6_2atmpS3031;
        struct _M0TPB5ArrayGfE* _M0L4v__sS3034;
        struct _M0TPB5ArrayGfE* _M0L4w__sS3035;
        struct _M0TPB5ArrayGfE* _M0L4w__sS3038;
        float _M0L6_2atmpS3037;
        float _M0L6_2atmpS3036;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS3039;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS3042;
        float _M0L6_2atmpS3041;
        float _M0L6_2atmpS3040;
        struct _M0TPB5ArrayGiE* _M0L4tabsS3043;
        #line 334 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0L6_2atmpS3032
        = _M0MPC15array5Array2atGfE(_M0L4v__sS3033, _M0L1kS1109);
        _M0L6_2atmpS3031 = _M0L12ap__membraneS1101 - _M0L6_2atmpS3032;
        #line 334 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS3029, _M0L6_2atmpS3030, _M0L6_2atmpS3031);
        _M0L4v__sS3034 = _M0L1pS1092->$20;
        #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0MPC15array5Array3setGfE(_M0L4v__sS3034, _M0L1kS1109, _M0L12ap__membraneS1101);
        _M0L4w__sS3035 = _M0L1pS1092->$21;
        _M0L4w__sS3038 = _M0L1pS1092->$21;
        #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0L6_2atmpS3037
        = _M0MPC15array5Array2atGfE(_M0L4w__sS3038, _M0L1kS1109);
        _M0L6_2atmpS3036 = _M0L6_2atmpS3037 + _M0L1bS1096;
        #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0MPC15array5Array3setGfE(_M0L4w__sS3035, _M0L1kS1109, _M0L6_2atmpS3036);
        _M0L9thresholdS3039 = _M0L1pS1092->$24;
        _M0L9thresholdS3042 = _M0L1pS1092->$24;
        #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0L6_2atmpS3041
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS3042, _M0L1kS1109);
        _M0L6_2atmpS3040 = _M0L6_2atmpS3041 + _M0L2atS1097;
        #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0MPC15array5Array3setGfE(_M0L9thresholdS3039, _M0L1kS1109, _M0L6_2atmpS3040);
        _M0L4tabsS3043 = _M0L1pS1092->$25;
        #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS3043, _M0L1kS1109, _M0L11tabs__stepsS1102);
        goto join_1110;
      }
      _M0L4v__sS3044 = _M0L1pS1092->$20;
      _M0L4v__sS3056 = _M0L1pS1092->$20;
      #line 345 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS3046
      = _M0MPC15array5Array2atGfE(_M0L4v__sS3056, _M0L1kS1109);
      _M0L6_2atmpS3048 = 0x1p-1f * _M0L2dtS1103;
      _M0L2dvS3054 = _M0L1pS1092->$26;
      _M0L6_2atmpS3055 = _M0L1kS1109 * 3;
      #line 345 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS3050
      = _M0MPC15array5Array2atGfE(_M0L2dvS3054, _M0L6_2atmpS3055);
      _M0L8dv__tempS3052 = _M0L1pS1092->$27;
      _M0L6_2atmpS3053 = _M0L1kS1109 * 3;
      #line 345 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS3051
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS3052, _M0L6_2atmpS3053);
      _M0L6_2atmpS3049 = _M0L6_2atmpS3050 + _M0L6_2atmpS3051;
      _M0L6_2atmpS3047 = _M0L6_2atmpS3048 * _M0L6_2atmpS3049;
      _M0L6_2atmpS3045 = _M0L6_2atmpS3046 + _M0L6_2atmpS3047;
      #line 345 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__sS3044, _M0L1kS1109, _M0L6_2atmpS3045);
      _M0L4v__dS3057 = _M0L1pS1092->$22;
      _M0L4v__dS3071 = _M0L1pS1092->$22;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS3059
      = _M0MPC15array5Array2atGfE(_M0L4v__dS3071, _M0L1kS1109);
      _M0L6_2atmpS3061 = 0x1p-1f * _M0L2dtS1103;
      _M0L2dvS3068 = _M0L1pS1092->$26;
      _M0L6_2atmpS3070 = _M0L1kS1109 * 3;
      _M0L6_2atmpS3069 = _M0L6_2atmpS3070 + 1;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS3063
      = _M0MPC15array5Array2atGfE(_M0L2dvS3068, _M0L6_2atmpS3069);
      _M0L8dv__tempS3065 = _M0L1pS1092->$27;
      _M0L6_2atmpS3067 = _M0L1kS1109 * 3;
      _M0L6_2atmpS3066 = _M0L6_2atmpS3067 + 1;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS3064
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS3065, _M0L6_2atmpS3066);
      _M0L6_2atmpS3062 = _M0L6_2atmpS3063 + _M0L6_2atmpS3064;
      _M0L6_2atmpS3060 = _M0L6_2atmpS3061 * _M0L6_2atmpS3062;
      _M0L6_2atmpS3058 = _M0L6_2atmpS3059 + _M0L6_2atmpS3060;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__dS3057, _M0L1kS1109, _M0L6_2atmpS3058);
      _M0L4w__sS3072 = _M0L1pS1092->$21;
      _M0L4w__sS3086 = _M0L1pS1092->$21;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS3074
      = _M0MPC15array5Array2atGfE(_M0L4w__sS3086, _M0L1kS1109);
      _M0L6_2atmpS3076 = 0x1p-1f * _M0L2dtS1103;
      _M0L2dvS3083 = _M0L1pS1092->$26;
      _M0L6_2atmpS3085 = _M0L1kS1109 * 3;
      _M0L6_2atmpS3084 = _M0L6_2atmpS3085 + 2;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS3078
      = _M0MPC15array5Array2atGfE(_M0L2dvS3083, _M0L6_2atmpS3084);
      _M0L8dv__tempS3080 = _M0L1pS1092->$27;
      _M0L6_2atmpS3082 = _M0L1kS1109 * 3;
      _M0L6_2atmpS3081 = _M0L6_2atmpS3082 + 2;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS3079
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS3080, _M0L6_2atmpS3081);
      _M0L6_2atmpS3077 = _M0L6_2atmpS3078 + _M0L6_2atmpS3079;
      _M0L6_2atmpS3075 = _M0L6_2atmpS3076 * _M0L6_2atmpS3077;
      _M0L6_2atmpS3073 = _M0L6_2atmpS3074 + _M0L6_2atmpS3075;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L4w__sS3072, _M0L1kS1109, _M0L6_2atmpS3073);
      goto join_1110;
      goto joinlet_3227;
      join_1110:;
      _M0L6_2atmpS2991 = _M0L1kS1109 + 1;
      _M0L1kS1109 = _M0L6_2atmpS2991;
      continue;
      joinlet_3227:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt24ballandstick__heun__step(
  struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L1pS1071,
  float _M0L2dtS1082,
  int32_t _M0L11store__tempS1081
) {
  int32_t _M0L1nS1070;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L3p__S1072;
  float _M0L1cS1073;
  float _M0L2glS1074;
  float _M0L2elS1075;
  float _M0L9dt__slopeS1076;
  float _M0L2twS1077;
  float _M0L1aS1078;
  struct _M0TPB8MutLocalGiE* _M0L1kS1079;
  #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L1nS1070 = _M0L1pS1071->$19;
  _M0L3p__S1072 = _M0L1pS1071->$0;
  _M0L1cS1073 = _M0L3p__S1072->$0;
  _M0L2glS1074 = _M0L3p__S1072->$1;
  _M0L2elS1075 = _M0L3p__S1072->$4;
  _M0L9dt__slopeS1076 = _M0L3p__S1072->$7;
  _M0L2twS1077 = _M0L3p__S1072->$8;
  _M0L1aS1078 = _M0L3p__S1072->$9;
  _M0L1kS1079
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS1079)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS1079->$0 = 0;
  while (1) {
    int32_t _M0L3valS2870 = _M0L1kS1079->$0;
    if (_M0L3valS2870 < _M0L1nS1070) {
      float _M0L2dsS1080;
      float _M0L2ddS1083;
      float _M0L2dwS1084;
      struct _M0TPB5ArrayGfE* _M0L4v__dS2972;
      int32_t _M0L3valS2973;
      float _M0L6_2atmpS2971;
      float _M0L6_2atmpS2967;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2969;
      int32_t _M0L3valS2970;
      float _M0L6_2atmpS2968;
      float _M0L6_2atmpS2966;
      float _M0L6_2atmpS2965;
      float _M0L6_2atmpS2960;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L4dendS2964;
      struct _M0TPB5ArrayGfE* _M0L3gaxS2962;
      int32_t _M0L3valS2963;
      float _M0L6_2atmpS2961;
      float _M0L7ic__valS1085;
      float _M0L9exp__termS1086;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2948;
      int32_t _M0L3valS2949;
      float _M0L6_2atmpS2947;
      float _M0L6_2atmpS2946;
      float _M0L6_2atmpS2945;
      float _M0L6_2atmpS2944;
      float _M0L6_2atmpS2940;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2942;
      int32_t _M0L3valS2943;
      float _M0L6_2atmpS2941;
      float _M0L6_2atmpS2939;
      float _M0L6_2atmpS2935;
      struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS2937;
      int32_t _M0L3valS2938;
      float _M0L6_2atmpS2936;
      float _M0L6_2atmpS2934;
      float _M0L6_2atmpS2930;
      struct _M0TPB5ArrayGfE* _M0L4i__sS2932;
      int32_t _M0L3valS2933;
      float _M0L6_2atmpS2931;
      float _M0L6_2atmpS2929;
      float _M0L10dv__s__valS1087;
      struct _M0TPB5ArrayGfE* _M0L4v__dS2927;
      int32_t _M0L3valS2928;
      float _M0L6_2atmpS2926;
      float _M0L6_2atmpS2925;
      float _M0L6_2atmpS2920;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L4dendS2924;
      struct _M0TPB5ArrayGfE* _M0L2gmS2922;
      int32_t _M0L3valS2923;
      float _M0L6_2atmpS2921;
      float _M0L6_2atmpS2916;
      struct _M0TPB5ArrayGfE* _M0L12syn__curr__dS2918;
      int32_t _M0L3valS2919;
      float _M0L6_2atmpS2917;
      float _M0L6_2atmpS2915;
      float _M0L6_2atmpS2911;
      struct _M0TPB5ArrayGfE* _M0L4i__dS2913;
      int32_t _M0L3valS2914;
      float _M0L6_2atmpS2912;
      float _M0L6_2atmpS2906;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L4dendS2910;
      struct _M0TPB5ArrayGfE* _M0L1cS2908;
      int32_t _M0L3valS2909;
      float _M0L6_2atmpS2907;
      float _M0L10dv__d__valS1088;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2904;
      int32_t _M0L3valS2905;
      float _M0L6_2atmpS2903;
      float _M0L6_2atmpS2902;
      float _M0L6_2atmpS2901;
      float _M0L6_2atmpS2896;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2899;
      int32_t _M0L3valS2900;
      float _M0L6_2atmpS2898;
      float _M0L6_2atmpS2897;
      float _M0L6_2atmpS2895;
      float _M0L7dw__valS1089;
      int32_t _M0L3valS2894;
      int32_t _M0L6_2atmpS2893;
      if (_M0L11store__tempS1081) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2984 = _M0L1pS1071->$27;
        int32_t _M0L3valS2986 = _M0L1kS1079->$0;
        int32_t _M0L6_2atmpS2985 = _M0L3valS2986 * 3;
        float _M0L6_2atmpS2983;
        #line 208 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0L6_2atmpS2983
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2984, _M0L6_2atmpS2985);
        _M0L2dsS1080 = _M0L6_2atmpS2983 * _M0L2dtS1082;
      } else {
        _M0L2dsS1080 = 0x0p+0f;
      }
      if (_M0L11store__tempS1081) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2979 = _M0L1pS1071->$27;
        int32_t _M0L3valS2982 = _M0L1kS1079->$0;
        int32_t _M0L6_2atmpS2981 = _M0L3valS2982 * 3;
        int32_t _M0L6_2atmpS2980 = _M0L6_2atmpS2981 + 1;
        float _M0L6_2atmpS2978;
        #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0L6_2atmpS2978
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2979, _M0L6_2atmpS2980);
        _M0L2ddS1083 = _M0L6_2atmpS2978 * _M0L2dtS1082;
      } else {
        _M0L2ddS1083 = 0x0p+0f;
      }
      if (_M0L11store__tempS1081) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2974 = _M0L1pS1071->$27;
        int32_t _M0L3valS2977 = _M0L1kS1079->$0;
        int32_t _M0L6_2atmpS2976 = _M0L3valS2977 * 3;
        int32_t _M0L6_2atmpS2975 = _M0L6_2atmpS2976 + 2;
        #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0L2dwS1084
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2974, _M0L6_2atmpS2975);
      } else {
        _M0L2dwS1084 = 0x0p+0f;
      }
      _M0L4v__dS2972 = _M0L1pS1071->$22;
      _M0L3valS2973 = _M0L1kS1079->$0;
      #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2971
      = _M0MPC15array5Array2atGfE(_M0L4v__dS2972, _M0L3valS2973);
      _M0L6_2atmpS2967 = _M0L6_2atmpS2971 + _M0L2ddS1083;
      _M0L4v__sS2969 = _M0L1pS1071->$20;
      _M0L3valS2970 = _M0L1kS1079->$0;
      #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2968
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2969, _M0L3valS2970);
      _M0L6_2atmpS2966 = _M0L6_2atmpS2967 - _M0L6_2atmpS2968;
      _M0L6_2atmpS2965 = _M0L6_2atmpS2966 - _M0L2dsS1080;
      _M0L6_2atmpS2960 = -_M0L6_2atmpS2965;
      _M0L4dendS2964 = _M0L1pS1071->$2;
      _M0L3gaxS2962 = _M0L4dendS2964->$3;
      _M0L3valS2963 = _M0L1kS1079->$0;
      #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2961
      = _M0MPC15array5Array2atGfE(_M0L3gaxS2962, _M0L3valS2963);
      _M0L7ic__valS1085 = _M0L6_2atmpS2960 * _M0L6_2atmpS2961;
      if (_M0L9dt__slopeS1076 < 0x0p+0f) {
        _M0L9exp__termS1086 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L4v__sS2958 = _M0L1pS1071->$20;
        int32_t _M0L3valS2959 = _M0L1kS1079->$0;
        float _M0L6_2atmpS2957;
        float _M0L6_2atmpS2953;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2955;
        int32_t _M0L3valS2956;
        float _M0L6_2atmpS2954;
        float _M0L6_2atmpS2952;
        float _M0L6_2atmpS2951;
        float _M0L6_2atmpS2950;
        #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0L6_2atmpS2957
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2958, _M0L3valS2959);
        _M0L6_2atmpS2953 = _M0L6_2atmpS2957 + _M0L2dsS1080;
        _M0L9thresholdS2955 = _M0L1pS1071->$24;
        _M0L3valS2956 = _M0L1kS1079->$0;
        #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0L6_2atmpS2954
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2955, _M0L3valS2956);
        _M0L6_2atmpS2952 = _M0L6_2atmpS2953 - _M0L6_2atmpS2954;
        _M0L6_2atmpS2951 = _M0L6_2atmpS2952 / _M0L9dt__slopeS1076;
        #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0L6_2atmpS2950 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2951);
        _M0L9exp__termS1086 = _M0L9dt__slopeS1076 * _M0L6_2atmpS2950;
      }
      _M0L4v__sS2948 = _M0L1pS1071->$20;
      _M0L3valS2949 = _M0L1kS1079->$0;
      #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2947
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2948, _M0L3valS2949);
      _M0L6_2atmpS2946 = _M0L2elS1075 - _M0L6_2atmpS2947;
      _M0L6_2atmpS2945 = _M0L6_2atmpS2946 - _M0L2dsS1080;
      _M0L6_2atmpS2944 = _M0L2glS1074 * _M0L6_2atmpS2945;
      _M0L6_2atmpS2940 = _M0L6_2atmpS2944 + _M0L9exp__termS1086;
      _M0L4w__sS2942 = _M0L1pS1071->$21;
      _M0L3valS2943 = _M0L1kS1079->$0;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2941
      = _M0MPC15array5Array2atGfE(_M0L4w__sS2942, _M0L3valS2943);
      _M0L6_2atmpS2939 = _M0L6_2atmpS2940 - _M0L6_2atmpS2941;
      _M0L6_2atmpS2935 = _M0L6_2atmpS2939 - _M0L2dwS1084;
      _M0L12syn__curr__sS2937 = _M0L1pS1071->$28;
      _M0L3valS2938 = _M0L1kS1079->$0;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2936
      = _M0MPC15array5Array2atGfE(_M0L12syn__curr__sS2937, _M0L3valS2938);
      _M0L6_2atmpS2934 = _M0L6_2atmpS2935 - _M0L6_2atmpS2936;
      _M0L6_2atmpS2930 = _M0L6_2atmpS2934 - _M0L7ic__valS1085;
      _M0L4i__sS2932 = _M0L1pS1071->$3;
      _M0L3valS2933 = _M0L1kS1079->$0;
      #line 228 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2931
      = _M0MPC15array5Array2atGfE(_M0L4i__sS2932, _M0L3valS2933);
      _M0L6_2atmpS2929 = _M0L6_2atmpS2930 + _M0L6_2atmpS2931;
      _M0L10dv__s__valS1087 = _M0L6_2atmpS2929 / _M0L1cS1073;
      _M0L4v__dS2927 = _M0L1pS1071->$22;
      _M0L3valS2928 = _M0L1kS1079->$0;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2926
      = _M0MPC15array5Array2atGfE(_M0L4v__dS2927, _M0L3valS2928);
      _M0L6_2atmpS2925 = _M0L2elS1075 - _M0L6_2atmpS2926;
      _M0L6_2atmpS2920 = _M0L6_2atmpS2925 - _M0L2ddS1083;
      _M0L4dendS2924 = _M0L1pS1071->$2;
      _M0L2gmS2922 = _M0L4dendS2924->$4;
      _M0L3valS2923 = _M0L1kS1079->$0;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2921
      = _M0MPC15array5Array2atGfE(_M0L2gmS2922, _M0L3valS2923);
      _M0L6_2atmpS2916 = _M0L6_2atmpS2920 * _M0L6_2atmpS2921;
      _M0L12syn__curr__dS2918 = _M0L1pS1071->$29;
      _M0L3valS2919 = _M0L1kS1079->$0;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2917
      = _M0MPC15array5Array2atGfE(_M0L12syn__curr__dS2918, _M0L3valS2919);
      _M0L6_2atmpS2915 = _M0L6_2atmpS2916 - _M0L6_2atmpS2917;
      _M0L6_2atmpS2911 = _M0L6_2atmpS2915 + _M0L7ic__valS1085;
      _M0L4i__dS2913 = _M0L1pS1071->$4;
      _M0L3valS2914 = _M0L1kS1079->$0;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2912
      = _M0MPC15array5Array2atGfE(_M0L4i__dS2913, _M0L3valS2914);
      _M0L6_2atmpS2906 = _M0L6_2atmpS2911 + _M0L6_2atmpS2912;
      _M0L4dendS2910 = _M0L1pS1071->$2;
      _M0L1cS2908 = _M0L4dendS2910->$2;
      _M0L3valS2909 = _M0L1kS1079->$0;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2907
      = _M0MPC15array5Array2atGfE(_M0L1cS2908, _M0L3valS2909);
      _M0L10dv__d__valS1088 = _M0L6_2atmpS2906 / _M0L6_2atmpS2907;
      _M0L4v__sS2904 = _M0L1pS1071->$20;
      _M0L3valS2905 = _M0L1kS1079->$0;
      #line 236 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2903
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2904, _M0L3valS2905);
      _M0L6_2atmpS2902 = _M0L6_2atmpS2903 + _M0L2dsS1080;
      _M0L6_2atmpS2901 = _M0L6_2atmpS2902 - _M0L2elS1075;
      _M0L6_2atmpS2896 = _M0L1aS1078 * _M0L6_2atmpS2901;
      _M0L4w__sS2899 = _M0L1pS1071->$21;
      _M0L3valS2900 = _M0L1kS1079->$0;
      #line 236 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2898
      = _M0MPC15array5Array2atGfE(_M0L4w__sS2899, _M0L3valS2900);
      _M0L6_2atmpS2897 = _M0L6_2atmpS2898 + _M0L2dwS1084;
      _M0L6_2atmpS2895 = _M0L6_2atmpS2896 - _M0L6_2atmpS2897;
      _M0L7dw__valS1089 = _M0L6_2atmpS2895 / _M0L2twS1077;
      if (_M0L11store__tempS1081) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2871 = _M0L1pS1071->$27;
        int32_t _M0L3valS2873 = _M0L1kS1079->$0;
        int32_t _M0L6_2atmpS2872 = _M0L3valS2873 * 3;
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2874;
        int32_t _M0L3valS2877;
        int32_t _M0L6_2atmpS2876;
        int32_t _M0L6_2atmpS2875;
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2878;
        int32_t _M0L3valS2881;
        int32_t _M0L6_2atmpS2880;
        int32_t _M0L6_2atmpS2879;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2871, _M0L6_2atmpS2872, _M0L10dv__s__valS1087);
        _M0L8dv__tempS2874 = _M0L1pS1071->$27;
        _M0L3valS2877 = _M0L1kS1079->$0;
        _M0L6_2atmpS2876 = _M0L3valS2877 * 3;
        _M0L6_2atmpS2875 = _M0L6_2atmpS2876 + 1;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2874, _M0L6_2atmpS2875, _M0L10dv__d__valS1088);
        _M0L8dv__tempS2878 = _M0L1pS1071->$27;
        _M0L3valS2881 = _M0L1kS1079->$0;
        _M0L6_2atmpS2880 = _M0L3valS2881 * 3;
        _M0L6_2atmpS2879 = _M0L6_2atmpS2880 + 2;
        #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2878, _M0L6_2atmpS2879, _M0L7dw__valS1089);
      } else {
        struct _M0TPB5ArrayGfE* _M0L2dvS2882 = _M0L1pS1071->$26;
        int32_t _M0L3valS2884 = _M0L1kS1079->$0;
        int32_t _M0L6_2atmpS2883 = _M0L3valS2884 * 3;
        struct _M0TPB5ArrayGfE* _M0L2dvS2885;
        int32_t _M0L3valS2888;
        int32_t _M0L6_2atmpS2887;
        int32_t _M0L6_2atmpS2886;
        struct _M0TPB5ArrayGfE* _M0L2dvS2889;
        int32_t _M0L3valS2892;
        int32_t _M0L6_2atmpS2891;
        int32_t _M0L6_2atmpS2890;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2882, _M0L6_2atmpS2883, _M0L10dv__s__valS1087);
        _M0L2dvS2885 = _M0L1pS1071->$26;
        _M0L3valS2888 = _M0L1kS1079->$0;
        _M0L6_2atmpS2887 = _M0L3valS2888 * 3;
        _M0L6_2atmpS2886 = _M0L6_2atmpS2887 + 1;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2885, _M0L6_2atmpS2886, _M0L10dv__d__valS1088);
        _M0L2dvS2889 = _M0L1pS1071->$26;
        _M0L3valS2892 = _M0L1kS1079->$0;
        _M0L6_2atmpS2891 = _M0L3valS2892 * 3;
        _M0L6_2atmpS2890 = _M0L6_2atmpS2891 + 2;
        #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2889, _M0L6_2atmpS2890, _M0L7dw__valS1089);
      }
      _M0L3valS2894 = _M0L1kS1079->$0;
      _M0L6_2atmpS2893 = _M0L3valS2894 + 1;
      _M0L1kS1079->$0 = _M0L6_2atmpS2893;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS1079);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt29ballandstick__syn__curr__dend(
  struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L1pS1066
) {
  int32_t _M0L1nS1065;
  int32_t _M0L7_2abindS1067;
  int32_t _M0L1iS1068;
  #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L1nS1065 = _M0L1pS1066->$19;
  _M0L7_2abindS1067 = 0;
  _M0L1iS1068 = _M0L7_2abindS1067;
  while (1) {
    if (_M0L1iS1068 < _M0L1nS1065) {
      struct _M0TPB5ArrayGfE* _M0L12syn__curr__dS2849 = _M0L1pS1066->$29;
      struct _M0TPB5ArrayGfE* _M0L5ge__dS2868 = _M0L1pS1066->$7;
      float _M0L6_2atmpS2863;
      struct _M0TPB5ArrayGfE* _M0L4v__dS2867;
      float _M0L6_2atmpS2865;
      float _M0L4e__eS2866;
      float _M0L6_2atmpS2864;
      float _M0L6_2atmpS2861;
      float _M0L7gsyn__eS2862;
      float _M0L6_2atmpS2851;
      struct _M0TPB5ArrayGfE* _M0L5gi__dS2860;
      float _M0L6_2atmpS2855;
      struct _M0TPB5ArrayGfE* _M0L4v__dS2859;
      float _M0L6_2atmpS2857;
      float _M0L4e__iS2858;
      float _M0L6_2atmpS2856;
      float _M0L6_2atmpS2853;
      float _M0L7gsyn__iS2854;
      float _M0L6_2atmpS2852;
      float _M0L6_2atmpS2850;
      int32_t _M0L6_2atmpS2869;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2863
      = _M0MPC15array5Array2atGfE(_M0L5ge__dS2868, _M0L1iS1068);
      _M0L4v__dS2867 = _M0L1pS1066->$22;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2865
      = _M0MPC15array5Array2atGfE(_M0L4v__dS2867, _M0L1iS1068);
      _M0L4e__eS2866 = _M0L1pS1066->$13;
      _M0L6_2atmpS2864 = _M0L6_2atmpS2865 - _M0L4e__eS2866;
      _M0L6_2atmpS2861 = _M0L6_2atmpS2863 * _M0L6_2atmpS2864;
      _M0L7gsyn__eS2862 = _M0L1pS1066->$17;
      _M0L6_2atmpS2851 = _M0L6_2atmpS2861 * _M0L7gsyn__eS2862;
      _M0L5gi__dS2860 = _M0L1pS1066->$8;
      #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2855
      = _M0MPC15array5Array2atGfE(_M0L5gi__dS2860, _M0L1iS1068);
      _M0L4v__dS2859 = _M0L1pS1066->$22;
      #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2857
      = _M0MPC15array5Array2atGfE(_M0L4v__dS2859, _M0L1iS1068);
      _M0L4e__iS2858 = _M0L1pS1066->$14;
      _M0L6_2atmpS2856 = _M0L6_2atmpS2857 - _M0L4e__iS2858;
      _M0L6_2atmpS2853 = _M0L6_2atmpS2855 * _M0L6_2atmpS2856;
      _M0L7gsyn__iS2854 = _M0L1pS1066->$18;
      _M0L6_2atmpS2852 = _M0L6_2atmpS2853 * _M0L7gsyn__iS2854;
      _M0L6_2atmpS2850 = _M0L6_2atmpS2851 + _M0L6_2atmpS2852;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L12syn__curr__dS2849, _M0L1iS1068, _M0L6_2atmpS2850);
      _M0L6_2atmpS2869 = _M0L1iS1068 + 1;
      _M0L1iS1068 = _M0L6_2atmpS2869;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt29ballandstick__syn__curr__soma(
  struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L1pS1061
) {
  int32_t _M0L1nS1060;
  int32_t _M0L7_2abindS1062;
  int32_t _M0L1iS1063;
  #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L1nS1060 = _M0L1pS1061->$19;
  _M0L7_2abindS1062 = 0;
  _M0L1iS1063 = _M0L7_2abindS1062;
  while (1) {
    if (_M0L1iS1063 < _M0L1nS1060) {
      struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS2828 = _M0L1pS1061->$28;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2847 = _M0L1pS1061->$5;
      float _M0L6_2atmpS2842;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2846;
      float _M0L6_2atmpS2844;
      float _M0L4e__eS2845;
      float _M0L6_2atmpS2843;
      float _M0L6_2atmpS2840;
      float _M0L7gsyn__eS2841;
      float _M0L6_2atmpS2830;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2839;
      float _M0L6_2atmpS2834;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2838;
      float _M0L6_2atmpS2836;
      float _M0L4e__iS2837;
      float _M0L6_2atmpS2835;
      float _M0L6_2atmpS2832;
      float _M0L7gsyn__iS2833;
      float _M0L6_2atmpS2831;
      float _M0L6_2atmpS2829;
      int32_t _M0L6_2atmpS2848;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2842
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2847, _M0L1iS1063);
      _M0L4v__sS2846 = _M0L1pS1061->$20;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2844
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2846, _M0L1iS1063);
      _M0L4e__eS2845 = _M0L1pS1061->$13;
      _M0L6_2atmpS2843 = _M0L6_2atmpS2844 - _M0L4e__eS2845;
      _M0L6_2atmpS2840 = _M0L6_2atmpS2842 * _M0L6_2atmpS2843;
      _M0L7gsyn__eS2841 = _M0L1pS1061->$17;
      _M0L6_2atmpS2830 = _M0L6_2atmpS2840 * _M0L7gsyn__eS2841;
      _M0L5gi__sS2839 = _M0L1pS1061->$6;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2834
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2839, _M0L1iS1063);
      _M0L4v__sS2838 = _M0L1pS1061->$20;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2836
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2838, _M0L1iS1063);
      _M0L4e__iS2837 = _M0L1pS1061->$14;
      _M0L6_2atmpS2835 = _M0L6_2atmpS2836 - _M0L4e__iS2837;
      _M0L6_2atmpS2832 = _M0L6_2atmpS2834 * _M0L6_2atmpS2835;
      _M0L7gsyn__iS2833 = _M0L1pS1061->$18;
      _M0L6_2atmpS2831 = _M0L6_2atmpS2832 * _M0L7gsyn__iS2833;
      _M0L6_2atmpS2829 = _M0L6_2atmpS2830 + _M0L6_2atmpS2831;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L12syn__curr__sS2828, _M0L1iS1063, _M0L6_2atmpS2829);
      _M0L6_2atmpS2848 = _M0L1iS1063 + 1;
      _M0L1iS1063 = _M0L6_2atmpS2848;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt34ballandstick__dend__step__synapses(
  struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L1pS1052,
  float _M0L2dtS1055
) {
  int32_t _M0L1nS1051;
  int32_t _M0L7_2abindS1053;
  int32_t _M0L1iS1054;
  int32_t _M0L7_2abindS1057;
  int32_t _M0L1iS1058;
  #line 142 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L1nS1051 = _M0L1pS1052->$19;
  _M0L7_2abindS1053 = 0;
  _M0L1iS1054 = _M0L7_2abindS1053;
  while (1) {
    if (_M0L1iS1054 < _M0L1nS1051) {
      struct _M0TPB5ArrayGfE* _M0L5ge__dS2792 = _M0L1pS1052->$7;
      struct _M0TPB5ArrayGfE* _M0L5ge__dS2797 = _M0L1pS1052->$7;
      float _M0L6_2atmpS2794;
      struct _M0TPB5ArrayGfE* _M0L6glu__dS2796;
      float _M0L6_2atmpS2795;
      float _M0L6_2atmpS2793;
      struct _M0TPB5ArrayGfE* _M0L5gi__dS2798;
      struct _M0TPB5ArrayGfE* _M0L5gi__dS2803;
      float _M0L6_2atmpS2800;
      struct _M0TPB5ArrayGfE* _M0L7gaba__dS2802;
      float _M0L6_2atmpS2801;
      float _M0L6_2atmpS2799;
      struct _M0TPB5ArrayGfE* _M0L5ge__dS2804;
      struct _M0TPB5ArrayGfE* _M0L5ge__dS2813;
      float _M0L6_2atmpS2806;
      struct _M0TPB5ArrayGfE* _M0L5ge__dS2812;
      float _M0L6_2atmpS2811;
      float _M0L6_2atmpS2809;
      float _M0L6tau__eS2810;
      float _M0L6_2atmpS2808;
      float _M0L6_2atmpS2807;
      float _M0L6_2atmpS2805;
      struct _M0TPB5ArrayGfE* _M0L5gi__dS2814;
      struct _M0TPB5ArrayGfE* _M0L5gi__dS2823;
      float _M0L6_2atmpS2816;
      struct _M0TPB5ArrayGfE* _M0L5gi__dS2822;
      float _M0L6_2atmpS2821;
      float _M0L6_2atmpS2819;
      float _M0L6tau__iS2820;
      float _M0L6_2atmpS2818;
      float _M0L6_2atmpS2817;
      float _M0L6_2atmpS2815;
      int32_t _M0L6_2atmpS2824;
      #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2794
      = _M0MPC15array5Array2atGfE(_M0L5ge__dS2797, _M0L1iS1054);
      _M0L6glu__dS2796 = _M0L1pS1052->$11;
      #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2795
      = _M0MPC15array5Array2atGfE(_M0L6glu__dS2796, _M0L1iS1054);
      _M0L6_2atmpS2793 = _M0L6_2atmpS2794 + _M0L6_2atmpS2795;
      #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L5ge__dS2792, _M0L1iS1054, _M0L6_2atmpS2793);
      _M0L5gi__dS2798 = _M0L1pS1052->$8;
      _M0L5gi__dS2803 = _M0L1pS1052->$8;
      #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2800
      = _M0MPC15array5Array2atGfE(_M0L5gi__dS2803, _M0L1iS1054);
      _M0L7gaba__dS2802 = _M0L1pS1052->$12;
      #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2801
      = _M0MPC15array5Array2atGfE(_M0L7gaba__dS2802, _M0L1iS1054);
      _M0L6_2atmpS2799 = _M0L6_2atmpS2800 + _M0L6_2atmpS2801;
      #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L5gi__dS2798, _M0L1iS1054, _M0L6_2atmpS2799);
      _M0L5ge__dS2804 = _M0L1pS1052->$7;
      _M0L5ge__dS2813 = _M0L1pS1052->$7;
      #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2806
      = _M0MPC15array5Array2atGfE(_M0L5ge__dS2813, _M0L1iS1054);
      _M0L5ge__dS2812 = _M0L1pS1052->$7;
      #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2811
      = _M0MPC15array5Array2atGfE(_M0L5ge__dS2812, _M0L1iS1054);
      _M0L6_2atmpS2809 = -_M0L6_2atmpS2811;
      _M0L6tau__eS2810 = _M0L1pS1052->$15;
      _M0L6_2atmpS2808 = _M0L6_2atmpS2809 / _M0L6tau__eS2810;
      _M0L6_2atmpS2807 = _M0L2dtS1055 * _M0L6_2atmpS2808;
      _M0L6_2atmpS2805 = _M0L6_2atmpS2806 + _M0L6_2atmpS2807;
      #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L5ge__dS2804, _M0L1iS1054, _M0L6_2atmpS2805);
      _M0L5gi__dS2814 = _M0L1pS1052->$8;
      _M0L5gi__dS2823 = _M0L1pS1052->$8;
      #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2816
      = _M0MPC15array5Array2atGfE(_M0L5gi__dS2823, _M0L1iS1054);
      _M0L5gi__dS2822 = _M0L1pS1052->$8;
      #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2821
      = _M0MPC15array5Array2atGfE(_M0L5gi__dS2822, _M0L1iS1054);
      _M0L6_2atmpS2819 = -_M0L6_2atmpS2821;
      _M0L6tau__iS2820 = _M0L1pS1052->$16;
      _M0L6_2atmpS2818 = _M0L6_2atmpS2819 / _M0L6tau__iS2820;
      _M0L6_2atmpS2817 = _M0L2dtS1055 * _M0L6_2atmpS2818;
      _M0L6_2atmpS2815 = _M0L6_2atmpS2816 + _M0L6_2atmpS2817;
      #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L5gi__dS2814, _M0L1iS1054, _M0L6_2atmpS2815);
      _M0L6_2atmpS2824 = _M0L1iS1054 + 1;
      _M0L1iS1054 = _M0L6_2atmpS2824;
      continue;
    }
    break;
  }
  _M0L7_2abindS1057 = 0;
  _M0L1iS1058 = _M0L7_2abindS1057;
  while (1) {
    if (_M0L1iS1058 < _M0L1nS1051) {
      struct _M0TPB5ArrayGfE* _M0L6glu__dS2825 = _M0L1pS1052->$11;
      struct _M0TPB5ArrayGfE* _M0L7gaba__dS2826;
      int32_t _M0L6_2atmpS2827;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L6glu__dS2825, _M0L1iS1058, 0x0p+0f);
      _M0L7gaba__dS2826 = _M0L1pS1052->$12;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L7gaba__dS2826, _M0L1iS1058, 0x0p+0f);
      _M0L6_2atmpS2827 = _M0L1iS1058 + 1;
      _M0L1iS1058 = _M0L6_2atmpS2827;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt34ballandstick__soma__step__synapses(
  struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L1pS1043,
  float _M0L2dtS1046
) {
  int32_t _M0L1nS1042;
  int32_t _M0L7_2abindS1044;
  int32_t _M0L1iS1045;
  int32_t _M0L7_2abindS1048;
  int32_t _M0L1iS1049;
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L1nS1042 = _M0L1pS1043->$19;
  _M0L7_2abindS1044 = 0;
  _M0L1iS1045 = _M0L7_2abindS1044;
  while (1) {
    if (_M0L1iS1045 < _M0L1nS1042) {
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2756 = _M0L1pS1043->$5;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2761 = _M0L1pS1043->$5;
      float _M0L6_2atmpS2758;
      struct _M0TPB5ArrayGfE* _M0L6glu__sS2760;
      float _M0L6_2atmpS2759;
      float _M0L6_2atmpS2757;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2762;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2767;
      float _M0L6_2atmpS2764;
      struct _M0TPB5ArrayGfE* _M0L7gaba__sS2766;
      float _M0L6_2atmpS2765;
      float _M0L6_2atmpS2763;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2768;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2777;
      float _M0L6_2atmpS2770;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2776;
      float _M0L6_2atmpS2775;
      float _M0L6_2atmpS2773;
      float _M0L6tau__eS2774;
      float _M0L6_2atmpS2772;
      float _M0L6_2atmpS2771;
      float _M0L6_2atmpS2769;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2778;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2787;
      float _M0L6_2atmpS2780;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2786;
      float _M0L6_2atmpS2785;
      float _M0L6_2atmpS2783;
      float _M0L6tau__iS2784;
      float _M0L6_2atmpS2782;
      float _M0L6_2atmpS2781;
      float _M0L6_2atmpS2779;
      int32_t _M0L6_2atmpS2788;
      #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2758
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2761, _M0L1iS1045);
      _M0L6glu__sS2760 = _M0L1pS1043->$9;
      #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2759
      = _M0MPC15array5Array2atGfE(_M0L6glu__sS2760, _M0L1iS1045);
      _M0L6_2atmpS2757 = _M0L6_2atmpS2758 + _M0L6_2atmpS2759;
      #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L5ge__sS2756, _M0L1iS1045, _M0L6_2atmpS2757);
      _M0L5gi__sS2762 = _M0L1pS1043->$6;
      _M0L5gi__sS2767 = _M0L1pS1043->$6;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2764
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2767, _M0L1iS1045);
      _M0L7gaba__sS2766 = _M0L1pS1043->$10;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2765
      = _M0MPC15array5Array2atGfE(_M0L7gaba__sS2766, _M0L1iS1045);
      _M0L6_2atmpS2763 = _M0L6_2atmpS2764 + _M0L6_2atmpS2765;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L5gi__sS2762, _M0L1iS1045, _M0L6_2atmpS2763);
      _M0L5ge__sS2768 = _M0L1pS1043->$5;
      _M0L5ge__sS2777 = _M0L1pS1043->$5;
      #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2770
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2777, _M0L1iS1045);
      _M0L5ge__sS2776 = _M0L1pS1043->$5;
      #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2775
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2776, _M0L1iS1045);
      _M0L6_2atmpS2773 = -_M0L6_2atmpS2775;
      _M0L6tau__eS2774 = _M0L1pS1043->$15;
      _M0L6_2atmpS2772 = _M0L6_2atmpS2773 / _M0L6tau__eS2774;
      _M0L6_2atmpS2771 = _M0L2dtS1046 * _M0L6_2atmpS2772;
      _M0L6_2atmpS2769 = _M0L6_2atmpS2770 + _M0L6_2atmpS2771;
      #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L5ge__sS2768, _M0L1iS1045, _M0L6_2atmpS2769);
      _M0L5gi__sS2778 = _M0L1pS1043->$6;
      _M0L5gi__sS2787 = _M0L1pS1043->$6;
      #line 132 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2780
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2787, _M0L1iS1045);
      _M0L5gi__sS2786 = _M0L1pS1043->$6;
      #line 132 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2785
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2786, _M0L1iS1045);
      _M0L6_2atmpS2783 = -_M0L6_2atmpS2785;
      _M0L6tau__iS2784 = _M0L1pS1043->$16;
      _M0L6_2atmpS2782 = _M0L6_2atmpS2783 / _M0L6tau__iS2784;
      _M0L6_2atmpS2781 = _M0L2dtS1046 * _M0L6_2atmpS2782;
      _M0L6_2atmpS2779 = _M0L6_2atmpS2780 + _M0L6_2atmpS2781;
      #line 132 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L5gi__sS2778, _M0L1iS1045, _M0L6_2atmpS2779);
      _M0L6_2atmpS2788 = _M0L1iS1045 + 1;
      _M0L1iS1045 = _M0L6_2atmpS2788;
      continue;
    }
    break;
  }
  _M0L7_2abindS1048 = 0;
  _M0L1iS1049 = _M0L7_2abindS1048;
  while (1) {
    if (_M0L1iS1049 < _M0L1nS1042) {
      struct _M0TPB5ArrayGfE* _M0L6glu__sS2789 = _M0L1pS1043->$9;
      struct _M0TPB5ArrayGfE* _M0L7gaba__sS2790;
      int32_t _M0L6_2atmpS2791;
      #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L6glu__sS2789, _M0L1iS1049, 0x0p+0f);
      _M0L7gaba__sS2790 = _M0L1pS1043->$10;
      #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L7gaba__sS2790, _M0L1iS1049, 0x0p+0f);
      _M0L6_2atmpS2791 = _M0L1iS1049 + 1;
      _M0L1iS1049 = _M0L6_2atmpS2791;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0MP26RiantR8snn__mbt12BallAndStick3new(
  int32_t _M0L1nS1011,
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L11soma__paramS1013,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1016
) {
  struct _M0TPB5ArrayGfE* _M0L4v__sS1010;
  float _M0L2vtS2754;
  float _M0L2vrS2755;
  float _M0L6spreadS1012;
  int32_t _M0L7_2abindS1014;
  int32_t _M0L1kS1015;
  struct _M0TPB5ArrayGfE* _M0L4w__sS1018;
  struct _M0TPB5ArrayGfE* _M0L4v__dS1019;
  int32_t _M0L7_2abindS1020;
  int32_t _M0L1kS1021;
  struct _M0TPB5ArrayGbE* _M0L4fireS1023;
  float _M0L2vtS2753;
  struct _M0TPB5ArrayGfE* _M0L9thresholdS1024;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1025;
  struct _M0TPB5ArrayGfE* _M0L4i__sS1026;
  struct _M0TPB5ArrayGfE* _M0L4i__dS1027;
  struct _M0TPB5ArrayGfE* _M0L5ge__sS1028;
  struct _M0TPB5ArrayGfE* _M0L5gi__sS1029;
  struct _M0TPB5ArrayGfE* _M0L5ge__dS1030;
  struct _M0TPB5ArrayGfE* _M0L5gi__dS1031;
  struct _M0TPB5ArrayGfE* _M0L6glu__sS1032;
  struct _M0TPB5ArrayGfE* _M0L7gaba__sS1033;
  struct _M0TPB5ArrayGfE* _M0L6glu__dS1034;
  struct _M0TPB5ArrayGfE* _M0L7gaba__dS1035;
  int32_t _M0L6total3S1036;
  struct _M0TPB5ArrayGfE* _M0L2dvS1037;
  struct _M0TPB5ArrayGfE* _M0L8dv__tempS1038;
  struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS1039;
  struct _M0TPB5ArrayGfE* _M0L12syn__curr__dS1040;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L4dendS1041;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L6_2atmpS2752;
  struct _M0TP26RiantR8snn__mbt12BallAndStick* _block_3237;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L4v__sS1010 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  _M0L2vtS2754 = _M0L11soma__paramS1013->$2;
  _M0L2vrS2755 = _M0L11soma__paramS1013->$3;
  _M0L6spreadS1012 = _M0L2vtS2754 - _M0L2vrS2755;
  _M0L7_2abindS1014 = 0;
  _M0L1kS1015 = _M0L7_2abindS1014;
  while (1) {
    if (_M0L1kS1015 < _M0L1nS1011) {
      float _M0L2vrS2743 = _M0L11soma__paramS1013->$3;
      float _M0L6_2atmpS2745;
      float _M0L6_2atmpS2744;
      float _M0L6_2atmpS2742;
      int32_t _M0L6_2atmpS2746;
      #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2745 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1016);
      _M0L6_2atmpS2744 = _M0L6_2atmpS2745 * _M0L6spreadS1012;
      _M0L6_2atmpS2742 = _M0L2vrS2743 + _M0L6_2atmpS2744;
      #line 77 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__sS1010, _M0L1kS1015, _M0L6_2atmpS2742);
      _M0L6_2atmpS2746 = _M0L1kS1015 + 1;
      _M0L1kS1015 = _M0L6_2atmpS2746;
      continue;
    }
    break;
  }
  #line 79 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L4w__sS1018 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 81 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L4v__dS1019 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  _M0L7_2abindS1020 = 0;
  _M0L1kS1021 = _M0L7_2abindS1020;
  while (1) {
    if (_M0L1kS1021 < _M0L1nS1011) {
      float _M0L2vrS2748 = _M0L11soma__paramS1013->$3;
      float _M0L6_2atmpS2750;
      float _M0L6_2atmpS2749;
      float _M0L6_2atmpS2747;
      int32_t _M0L6_2atmpS2751;
      #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0L6_2atmpS2750 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1016);
      _M0L6_2atmpS2749 = _M0L6_2atmpS2750 * _M0L6spreadS1012;
      _M0L6_2atmpS2747 = _M0L2vrS2748 + _M0L6_2atmpS2749;
      #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__dS1019, _M0L1kS1021, _M0L6_2atmpS2747);
      _M0L6_2atmpS2751 = _M0L1kS1021 + 1;
      _M0L1kS1021 = _M0L6_2atmpS2751;
      continue;
    }
    break;
  }
  #line 85 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L4fireS1023 = _M0MPC15array5Array4makeGbE(_M0L1nS1011, 0);
  _M0L2vtS2753 = _M0L11soma__paramS1013->$2;
  #line 86 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L9thresholdS1024
  = _M0MPC15array5Array4makeGfE(_M0L1nS1011, _M0L2vtS2753);
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L4tabsS1025 = _M0MPC15array5Array4makeGiE(_M0L1nS1011, 1);
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L4i__sS1026 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L4i__dS1027 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L5ge__sS1028 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L5gi__sS1029 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 93 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L5ge__dS1030 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L5gi__dS1031 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L6glu__sS1032 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L7gaba__sS1033 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L6glu__dS1034 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L7gaba__dS1035 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  _M0L6total3S1036 = _M0L1nS1011 * 3;
  #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L2dvS1037 = _M0MPC15array5Array4makeGfE(_M0L6total3S1036, 0x0p+0f);
  #line 102 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L8dv__tempS1038 = _M0MPC15array5Array4makeGfE(_M0L6total3S1036, 0x0p+0f);
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L12syn__curr__sS1039 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 104 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L12syn__curr__dS1040 = _M0MPC15array5Array4makeGfE(_M0L1nS1011, 0x0p+0f);
  #line 105 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L4dendS1041 = _M0MP26RiantR8snn__mbt8Dendrite3new(_M0L1nS1011);
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ballandstick.mbt"
  _M0L6_2atmpS2752 = _M0MP26RiantR8snn__mbt13AdExPostSpike3new();
  moonbit_incref_cycle_free(_M0L11soma__paramS1013);
  _block_3237
  = (struct _M0TP26RiantR8snn__mbt12BallAndStick*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt12BallAndStick));
  Moonbit_object_header(_block_3237)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 29, 0);
  _block_3237->$0 = _M0L11soma__paramS1013;
  _block_3237->$1 = _M0L6_2atmpS2752;
  _block_3237->$2 = _M0L4dendS1041;
  _block_3237->$3 = _M0L4i__sS1026;
  _block_3237->$4 = _M0L4i__dS1027;
  _block_3237->$5 = _M0L5ge__sS1028;
  _block_3237->$6 = _M0L5gi__sS1029;
  _block_3237->$7 = _M0L5ge__dS1030;
  _block_3237->$8 = _M0L5gi__dS1031;
  _block_3237->$9 = _M0L6glu__sS1032;
  _block_3237->$10 = _M0L7gaba__sS1033;
  _block_3237->$11 = _M0L6glu__dS1034;
  _block_3237->$12 = _M0L7gaba__dS1035;
  _block_3237->$13 = 0x0p+0f;
  _block_3237->$14 = -0x1.2cp+6f;
  _block_3237->$15 = 0x1.8p+2f;
  _block_3237->$16 = 0x1p+1f;
  _block_3237->$17 = 0x1p+0f;
  _block_3237->$18 = 0x1p+0f;
  _block_3237->$19 = _M0L1nS1011;
  _block_3237->$20 = _M0L4v__sS1010;
  _block_3237->$21 = _M0L4w__sS1018;
  _block_3237->$22 = _M0L4v__dS1019;
  _block_3237->$23 = _M0L4fireS1023;
  _block_3237->$24 = _M0L9thresholdS1024;
  _block_3237->$25 = _M0L4tabsS1025;
  _block_3237->$26 = _M0L2dvS1037;
  _block_3237->$27 = _M0L8dv__tempS1038;
  _block_3237->$28 = _M0L12syn__curr__sS1039;
  _block_3237->$29 = _M0L12syn__curr__dS1040;
  _block_3237->$30 = 0x0p+0f;
  return _block_3237;
}

struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0MP26RiantR8snn__mbt13AdExParameter3new(
  
) {
  float _M0L1cS1006;
  float _M0L2glS1007;
  float _M0L2tmS1008;
  float _M0L1rS1009;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _block_3238;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1cS1006 = 0x1.19p+8f;
  _M0L2glS1007 = 0x1.4p+5f;
  _M0L2tmS1008 = 0x1.19p+8f / 0x1.4p+5f;
  _M0L1rS1009 = 0x1p+0f / 0x1.4p+5f;
  _block_3238
  = (struct _M0TP26RiantR8snn__mbt13AdExParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExParameter));
  Moonbit_object_header(_block_3238)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3238->$0 = _M0L1cS1006;
  _block_3238->$1 = _M0L2glS1007;
  _block_3238->$2 = -0x1.9p+5f;
  _block_3238->$3 = -0x1.1a66666666666p+6f;
  _block_3238->$4 = -0x1.1a66666666666p+6f;
  _block_3238->$5 = _M0L2tmS1008;
  _block_3238->$6 = _M0L1rS1009;
  _block_3238->$7 = 0x1p+1f;
  _block_3238->$8 = 0x1.2p+7f;
  _block_3238->$9 = 0x1p+2f;
  _block_3238->$10 = 0x1.42p+6f;
  return _block_3238;
}

int32_t _M0FP26RiantR8snn__mbt17step__tripod__het(
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L1pS987,
  float _M0L2dtS994
) {
  int32_t _M0L1nS986;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2741;
  float _M0L2atS988;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2740;
  float _M0L6tau__aS989;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2739;
  float _M0L11tabs__constS990;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2738;
  float _M0L2upS991;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L11soma__spikeS2737;
  float _M0L12ap__membraneS992;
  float _M0L6_2atmpS2736;
  float _M0L6_2atmpS2735;
  int32_t _M0L11tabs__stepsS993;
  int32_t _M0L7_2abindS995;
  int32_t _M0L7_2abindS996;
  int32_t _M0L1iS997;
  int32_t _M0L7_2abindS999;
  int32_t _M0L1kS1000;
  #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1nS986 = _M0L1pS987->$27;
  _M0L11soma__spikeS2741 = _M0L1pS987->$1;
  _M0L2atS988 = _M0L11soma__spikeS2741->$0;
  _M0L11soma__spikeS2740 = _M0L1pS987->$1;
  _M0L6tau__aS989 = _M0L11soma__spikeS2740->$1;
  _M0L11soma__spikeS2739 = _M0L1pS987->$1;
  _M0L11tabs__constS990 = _M0L11soma__spikeS2739->$3;
  _M0L11soma__spikeS2738 = _M0L1pS987->$1;
  _M0L2upS991 = _M0L11soma__spikeS2738->$4;
  _M0L11soma__spikeS2737 = _M0L1pS987->$1;
  _M0L12ap__membraneS992 = _M0L11soma__spikeS2737->$2;
  _M0L6_2atmpS2736 = _M0L2upS991 + _M0L11tabs__constS990;
  _M0L6_2atmpS2735 = _M0L6_2atmpS2736 / _M0L2dtS994;
  #line 278 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L11tabs__stepsS993 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2735);
  #line 281 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0FP26RiantR8snn__mbt33tripod__het__soma__step__synapses(_M0L1pS987, _M0L2dtS994);
  #line 282 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0FP26RiantR8snn__mbt33tripod__het__dend__step__synapses(_M0L1pS987, _M0L2dtS994);
  #line 285 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0FP26RiantR8snn__mbt28tripod__het__syn__curr__soma(_M0L1pS987);
  #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0FP26RiantR8snn__mbt29tripod__het__syn__curr__dends(_M0L1pS987);
  #line 289 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0FP26RiantR8snn__mbt23tripod__het__heun__step(_M0L1pS987, _M0L2dtS994, 0);
  _M0L7_2abindS995 = 0;
  _M0L7_2abindS996 = _M0L1nS986 * 4;
  _M0L1iS997 = _M0L7_2abindS995;
  while (1) {
    if (_M0L1iS997 < _M0L7_2abindS996) {
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2589 = _M0L1pS987->$36;
      struct _M0TPB5ArrayGfE* _M0L2dvS2591 = _M0L1pS987->$35;
      float _M0L6_2atmpS2590;
      int32_t _M0L6_2atmpS2592;
      #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2590 = _M0MPC15array5Array2atGfE(_M0L2dvS2591, _M0L1iS997);
      #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L8dv__tempS2589, _M0L1iS997, _M0L6_2atmpS2590);
      _M0L6_2atmpS2592 = _M0L1iS997 + 1;
      _M0L1iS997 = _M0L6_2atmpS2592;
      continue;
    }
    break;
  }
  #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0FP26RiantR8snn__mbt23tripod__het__heun__step(_M0L1pS987, _M0L2dtS994, 1);
  _M0L7_2abindS999 = 0;
  _M0L1kS1000 = _M0L7_2abindS999;
  while (1) {
    if (_M0L1kS1000 < _M0L1nS986) {
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2734 =
        _M0L1pS987->$0;
      struct _M0TPB5ArrayGfE* _M0L2vrS2733 = _M0L11soma__paramS2734->$1;
      float _M0L5vr__kS1003;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2732;
      struct _M0TPB5ArrayGfE* _M0L1bS2731;
      float _M0L4b__kS1004;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2594;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2597;
      int32_t _M0L6_2atmpS2596;
      int32_t _M0L6_2atmpS2595;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2598;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2609;
      float _M0L6_2atmpS2600;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2608;
      struct _M0TPB5ArrayGfE* _M0L2vtS2607;
      float _M0L6_2atmpS2604;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS2606;
      float _M0L6_2atmpS2605;
      float _M0L6_2atmpS2603;
      float _M0L6_2atmpS2602;
      float _M0L6_2atmpS2601;
      float _M0L6_2atmpS2599;
      struct _M0TPB5ArrayGiE* _M0L4tabsS2611;
      int32_t _M0L6_2atmpS2610;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2730;
      float _M0L6_2atmpS2725;
      struct _M0TPB5ArrayGfE* _M0L2dvS2728;
      int32_t _M0L6_2atmpS2729;
      float _M0L6_2atmpS2727;
      float _M0L6_2atmpS2726;
      float _M0L10v__s__predS1005;
      struct _M0TPB5ArrayGbE* _M0L4fireS2649;
      int32_t _M0L6_2atmpS2650;
      struct _M0TPB5ArrayGbE* _M0L4fireS2651;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2667;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2679;
      float _M0L6_2atmpS2669;
      float _M0L6_2atmpS2671;
      struct _M0TPB5ArrayGfE* _M0L2dvS2677;
      int32_t _M0L6_2atmpS2678;
      float _M0L6_2atmpS2673;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2675;
      int32_t _M0L6_2atmpS2676;
      float _M0L6_2atmpS2674;
      float _M0L6_2atmpS2672;
      float _M0L6_2atmpS2670;
      float _M0L6_2atmpS2668;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2680;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2694;
      float _M0L6_2atmpS2682;
      float _M0L6_2atmpS2684;
      struct _M0TPB5ArrayGfE* _M0L2dvS2691;
      int32_t _M0L6_2atmpS2693;
      int32_t _M0L6_2atmpS2692;
      float _M0L6_2atmpS2686;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2688;
      int32_t _M0L6_2atmpS2690;
      int32_t _M0L6_2atmpS2689;
      float _M0L6_2atmpS2687;
      float _M0L6_2atmpS2685;
      float _M0L6_2atmpS2683;
      float _M0L6_2atmpS2681;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2695;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2709;
      float _M0L6_2atmpS2697;
      float _M0L6_2atmpS2699;
      struct _M0TPB5ArrayGfE* _M0L2dvS2706;
      int32_t _M0L6_2atmpS2708;
      int32_t _M0L6_2atmpS2707;
      float _M0L6_2atmpS2701;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2703;
      int32_t _M0L6_2atmpS2705;
      int32_t _M0L6_2atmpS2704;
      float _M0L6_2atmpS2702;
      float _M0L6_2atmpS2700;
      float _M0L6_2atmpS2698;
      float _M0L6_2atmpS2696;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2710;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2724;
      float _M0L6_2atmpS2712;
      float _M0L6_2atmpS2714;
      struct _M0TPB5ArrayGfE* _M0L2dvS2721;
      int32_t _M0L6_2atmpS2723;
      int32_t _M0L6_2atmpS2722;
      float _M0L6_2atmpS2716;
      struct _M0TPB5ArrayGfE* _M0L8dv__tempS2718;
      int32_t _M0L6_2atmpS2720;
      int32_t _M0L6_2atmpS2719;
      float _M0L6_2atmpS2717;
      float _M0L6_2atmpS2715;
      float _M0L6_2atmpS2713;
      float _M0L6_2atmpS2711;
      int32_t _M0L6_2atmpS2593;
      #line 297 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L5vr__kS1003 = _M0MPC15array5Array2atGfE(_M0L2vrS2733, _M0L1kS1000);
      _M0L11soma__paramS2732 = _M0L1pS987->$0;
      _M0L1bS2731 = _M0L11soma__paramS2732->$8;
      #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L4b__kS1004 = _M0MPC15array5Array2atGfE(_M0L1bS2731, _M0L1kS1000);
      _M0L4tabsS2594 = _M0L1pS987->$34;
      _M0L4tabsS2597 = _M0L1pS987->$34;
      #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2596
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2597, _M0L1kS1000);
      _M0L6_2atmpS2595 = _M0L6_2atmpS2596 - 1;
      #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS2594, _M0L1kS1000, _M0L6_2atmpS2595);
      _M0L9thresholdS2598 = _M0L1pS987->$33;
      _M0L9thresholdS2609 = _M0L1pS987->$33;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2600
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS2609, _M0L1kS1000);
      _M0L11soma__paramS2608 = _M0L1pS987->$0;
      _M0L2vtS2607 = _M0L11soma__paramS2608->$0;
      #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2604 = _M0MPC15array5Array2atGfE(_M0L2vtS2607, _M0L1kS1000);
      _M0L9thresholdS2606 = _M0L1pS987->$33;
      #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2605
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS2606, _M0L1kS1000);
      _M0L6_2atmpS2603 = _M0L6_2atmpS2604 - _M0L6_2atmpS2605;
      _M0L6_2atmpS2602 = _M0L2dtS994 * _M0L6_2atmpS2603;
      _M0L6_2atmpS2601 = _M0L6_2atmpS2602 / _M0L6tau__aS989;
      _M0L6_2atmpS2599 = _M0L6_2atmpS2600 + _M0L6_2atmpS2601;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS2598, _M0L1kS1000, _M0L6_2atmpS2599);
      _M0L4tabsS2611 = _M0L1pS987->$34;
      #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2610
      = _M0MPC15array5Array2atGiE(_M0L4tabsS2611, _M0L1kS1000);
      if (_M0L6_2atmpS2610 > 0) {
        struct _M0TPB5ArrayGfE* _M0L4v__sS2612 = _M0L1pS987->$28;
        struct _M0TPB5ArrayGfE* _M0L5v__d1S2613;
        struct _M0TPB5ArrayGfE* _M0L5v__d1S2630;
        float _M0L6_2atmpS2615;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2629;
        float _M0L6_2atmpS2626;
        struct _M0TPB5ArrayGfE* _M0L5v__d1S2628;
        float _M0L6_2atmpS2627;
        float _M0L6_2atmpS2625;
        float _M0L6_2atmpS2621;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2624;
        struct _M0TPB5ArrayGfE* _M0L3gaxS2623;
        float _M0L6_2atmpS2622;
        float _M0L6_2atmpS2617;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2620;
        struct _M0TPB5ArrayGfE* _M0L1cS2619;
        float _M0L6_2atmpS2618;
        float _M0L6_2atmpS2616;
        float _M0L6_2atmpS2614;
        struct _M0TPB5ArrayGfE* _M0L5v__d2S2631;
        struct _M0TPB5ArrayGfE* _M0L5v__d2S2648;
        float _M0L6_2atmpS2633;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2647;
        float _M0L6_2atmpS2644;
        struct _M0TPB5ArrayGfE* _M0L5v__d2S2646;
        float _M0L6_2atmpS2645;
        float _M0L6_2atmpS2643;
        float _M0L6_2atmpS2639;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2642;
        struct _M0TPB5ArrayGfE* _M0L3gaxS2641;
        float _M0L6_2atmpS2640;
        float _M0L6_2atmpS2635;
        struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2638;
        struct _M0TPB5ArrayGfE* _M0L1cS2637;
        float _M0L6_2atmpS2636;
        float _M0L6_2atmpS2634;
        float _M0L6_2atmpS2632;
        #line 305 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L4v__sS2612, _M0L1kS1000, _M0L5vr__kS1003);
        _M0L5v__d1S2613 = _M0L1pS987->$30;
        _M0L5v__d1S2630 = _M0L1pS987->$30;
        #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2615
        = _M0MPC15array5Array2atGfE(_M0L5v__d1S2630, _M0L1kS1000);
        _M0L4v__sS2629 = _M0L1pS987->$28;
        #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2626
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2629, _M0L1kS1000);
        _M0L5v__d1S2628 = _M0L1pS987->$30;
        #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2627
        = _M0MPC15array5Array2atGfE(_M0L5v__d1S2628, _M0L1kS1000);
        _M0L6_2atmpS2625 = _M0L6_2atmpS2626 - _M0L6_2atmpS2627;
        _M0L6_2atmpS2621 = _M0L2dtS994 * _M0L6_2atmpS2625;
        _M0L2d1S2624 = _M0L1pS987->$4;
        _M0L3gaxS2623 = _M0L2d1S2624->$3;
        #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2622
        = _M0MPC15array5Array2atGfE(_M0L3gaxS2623, _M0L1kS1000);
        _M0L6_2atmpS2617 = _M0L6_2atmpS2621 * _M0L6_2atmpS2622;
        _M0L2d1S2620 = _M0L1pS987->$4;
        _M0L1cS2619 = _M0L2d1S2620->$2;
        #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2618
        = _M0MPC15array5Array2atGfE(_M0L1cS2619, _M0L1kS1000);
        _M0L6_2atmpS2616 = _M0L6_2atmpS2617 / _M0L6_2atmpS2618;
        _M0L6_2atmpS2614 = _M0L6_2atmpS2615 + _M0L6_2atmpS2616;
        #line 306 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L5v__d1S2613, _M0L1kS1000, _M0L6_2atmpS2614);
        _M0L5v__d2S2631 = _M0L1pS987->$31;
        _M0L5v__d2S2648 = _M0L1pS987->$31;
        #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2633
        = _M0MPC15array5Array2atGfE(_M0L5v__d2S2648, _M0L1kS1000);
        _M0L4v__sS2647 = _M0L1pS987->$28;
        #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2644
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2647, _M0L1kS1000);
        _M0L5v__d2S2646 = _M0L1pS987->$31;
        #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2645
        = _M0MPC15array5Array2atGfE(_M0L5v__d2S2646, _M0L1kS1000);
        _M0L6_2atmpS2643 = _M0L6_2atmpS2644 - _M0L6_2atmpS2645;
        _M0L6_2atmpS2639 = _M0L2dtS994 * _M0L6_2atmpS2643;
        _M0L2d2S2642 = _M0L1pS987->$5;
        _M0L3gaxS2641 = _M0L2d2S2642->$3;
        #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2640
        = _M0MPC15array5Array2atGfE(_M0L3gaxS2641, _M0L1kS1000);
        _M0L6_2atmpS2635 = _M0L6_2atmpS2639 * _M0L6_2atmpS2640;
        _M0L2d2S2638 = _M0L1pS987->$5;
        _M0L1cS2637 = _M0L2d2S2638->$2;
        #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2636
        = _M0MPC15array5Array2atGfE(_M0L1cS2637, _M0L1kS1000);
        _M0L6_2atmpS2634 = _M0L6_2atmpS2635 / _M0L6_2atmpS2636;
        _M0L6_2atmpS2632 = _M0L6_2atmpS2633 + _M0L6_2atmpS2634;
        #line 307 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L5v__d2S2631, _M0L1kS1000, _M0L6_2atmpS2632);
        goto join_1001;
      }
      _M0L4v__sS2730 = _M0L1pS987->$28;
      #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2725
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2730, _M0L1kS1000);
      _M0L2dvS2728 = _M0L1pS987->$35;
      _M0L6_2atmpS2729 = _M0L1kS1000 * 4;
      #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2727
      = _M0MPC15array5Array2atGfE(_M0L2dvS2728, _M0L6_2atmpS2729);
      _M0L6_2atmpS2726 = _M0L6_2atmpS2727 * _M0L2dtS994;
      _M0L10v__s__predS1005 = _M0L6_2atmpS2725 + _M0L6_2atmpS2726;
      _M0L4fireS2649 = _M0L1pS987->$32;
      _M0L6_2atmpS2650 = _M0L10v__s__predS1005 >= -0x1.4p+3f;
      #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS2649, _M0L1kS1000, _M0L6_2atmpS2650);
      _M0L4fireS2651 = _M0L1pS987->$32;
      #line 315 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2651, _M0L1kS1000)) {
        struct _M0TPB5ArrayGfE* _M0L2dvS2652 = _M0L1pS987->$35;
        int32_t _M0L6_2atmpS2653 = _M0L1kS1000 * 4;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2656 = _M0L1pS987->$28;
        float _M0L6_2atmpS2655;
        float _M0L6_2atmpS2654;
        struct _M0TPB5ArrayGfE* _M0L4v__sS2657;
        struct _M0TPB5ArrayGfE* _M0L4w__sS2658;
        struct _M0TPB5ArrayGfE* _M0L4w__sS2661;
        float _M0L6_2atmpS2660;
        float _M0L6_2atmpS2659;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2662;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2665;
        float _M0L6_2atmpS2664;
        float _M0L6_2atmpS2663;
        struct _M0TPB5ArrayGiE* _M0L4tabsS2666;
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2655
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2656, _M0L1kS1000);
        _M0L6_2atmpS2654 = _M0L12ap__membraneS992 - _M0L6_2atmpS2655;
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2652, _M0L6_2atmpS2653, _M0L6_2atmpS2654);
        _M0L4v__sS2657 = _M0L1pS987->$28;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L4v__sS2657, _M0L1kS1000, _M0L12ap__membraneS992);
        _M0L4w__sS2658 = _M0L1pS987->$29;
        _M0L4w__sS2661 = _M0L1pS987->$29;
        #line 318 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2660
        = _M0MPC15array5Array2atGfE(_M0L4w__sS2661, _M0L1kS1000);
        _M0L6_2atmpS2659 = _M0L6_2atmpS2660 + _M0L4b__kS1004;
        #line 318 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L4w__sS2658, _M0L1kS1000, _M0L6_2atmpS2659);
        _M0L9thresholdS2662 = _M0L1pS987->$33;
        _M0L9thresholdS2665 = _M0L1pS987->$33;
        #line 319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2664
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2665, _M0L1kS1000);
        _M0L6_2atmpS2663 = _M0L6_2atmpS2664 + _M0L2atS988;
        #line 319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L9thresholdS2662, _M0L1kS1000, _M0L6_2atmpS2663);
        _M0L4tabsS2666 = _M0L1pS987->$34;
        #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS2666, _M0L1kS1000, _M0L11tabs__stepsS993);
        goto join_1001;
      }
      _M0L4v__sS2667 = _M0L1pS987->$28;
      _M0L4v__sS2679 = _M0L1pS987->$28;
      #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2669
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2679, _M0L1kS1000);
      _M0L6_2atmpS2671 = 0x1p-1f * _M0L2dtS994;
      _M0L2dvS2677 = _M0L1pS987->$35;
      _M0L6_2atmpS2678 = _M0L1kS1000 * 4;
      #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2673
      = _M0MPC15array5Array2atGfE(_M0L2dvS2677, _M0L6_2atmpS2678);
      _M0L8dv__tempS2675 = _M0L1pS987->$36;
      _M0L6_2atmpS2676 = _M0L1kS1000 * 4;
      #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2674
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2675, _M0L6_2atmpS2676);
      _M0L6_2atmpS2672 = _M0L6_2atmpS2673 + _M0L6_2atmpS2674;
      _M0L6_2atmpS2670 = _M0L6_2atmpS2671 * _M0L6_2atmpS2672;
      _M0L6_2atmpS2668 = _M0L6_2atmpS2669 + _M0L6_2atmpS2670;
      #line 325 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__sS2667, _M0L1kS1000, _M0L6_2atmpS2668);
      _M0L5v__d1S2680 = _M0L1pS987->$30;
      _M0L5v__d1S2694 = _M0L1pS987->$30;
      #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2682
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2694, _M0L1kS1000);
      _M0L6_2atmpS2684 = 0x1p-1f * _M0L2dtS994;
      _M0L2dvS2691 = _M0L1pS987->$35;
      _M0L6_2atmpS2693 = _M0L1kS1000 * 4;
      _M0L6_2atmpS2692 = _M0L6_2atmpS2693 + 1;
      #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2686
      = _M0MPC15array5Array2atGfE(_M0L2dvS2691, _M0L6_2atmpS2692);
      _M0L8dv__tempS2688 = _M0L1pS987->$36;
      _M0L6_2atmpS2690 = _M0L1kS1000 * 4;
      _M0L6_2atmpS2689 = _M0L6_2atmpS2690 + 1;
      #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2687
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2688, _M0L6_2atmpS2689);
      _M0L6_2atmpS2685 = _M0L6_2atmpS2686 + _M0L6_2atmpS2687;
      _M0L6_2atmpS2683 = _M0L6_2atmpS2684 * _M0L6_2atmpS2685;
      _M0L6_2atmpS2681 = _M0L6_2atmpS2682 + _M0L6_2atmpS2683;
      #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d1S2680, _M0L1kS1000, _M0L6_2atmpS2681);
      _M0L5v__d2S2695 = _M0L1pS987->$31;
      _M0L5v__d2S2709 = _M0L1pS987->$31;
      #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2697
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2709, _M0L1kS1000);
      _M0L6_2atmpS2699 = 0x1p-1f * _M0L2dtS994;
      _M0L2dvS2706 = _M0L1pS987->$35;
      _M0L6_2atmpS2708 = _M0L1kS1000 * 4;
      _M0L6_2atmpS2707 = _M0L6_2atmpS2708 + 2;
      #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2701
      = _M0MPC15array5Array2atGfE(_M0L2dvS2706, _M0L6_2atmpS2707);
      _M0L8dv__tempS2703 = _M0L1pS987->$36;
      _M0L6_2atmpS2705 = _M0L1kS1000 * 4;
      _M0L6_2atmpS2704 = _M0L6_2atmpS2705 + 2;
      #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2702
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2703, _M0L6_2atmpS2704);
      _M0L6_2atmpS2700 = _M0L6_2atmpS2701 + _M0L6_2atmpS2702;
      _M0L6_2atmpS2698 = _M0L6_2atmpS2699 * _M0L6_2atmpS2700;
      _M0L6_2atmpS2696 = _M0L6_2atmpS2697 + _M0L6_2atmpS2698;
      #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d2S2695, _M0L1kS1000, _M0L6_2atmpS2696);
      _M0L4w__sS2710 = _M0L1pS987->$29;
      _M0L4w__sS2724 = _M0L1pS987->$29;
      #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2712
      = _M0MPC15array5Array2atGfE(_M0L4w__sS2724, _M0L1kS1000);
      _M0L6_2atmpS2714 = 0x1p-1f * _M0L2dtS994;
      _M0L2dvS2721 = _M0L1pS987->$35;
      _M0L6_2atmpS2723 = _M0L1kS1000 * 4;
      _M0L6_2atmpS2722 = _M0L6_2atmpS2723 + 3;
      #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2716
      = _M0MPC15array5Array2atGfE(_M0L2dvS2721, _M0L6_2atmpS2722);
      _M0L8dv__tempS2718 = _M0L1pS987->$36;
      _M0L6_2atmpS2720 = _M0L1kS1000 * 4;
      _M0L6_2atmpS2719 = _M0L6_2atmpS2720 + 3;
      #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2717
      = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2718, _M0L6_2atmpS2719);
      _M0L6_2atmpS2715 = _M0L6_2atmpS2716 + _M0L6_2atmpS2717;
      _M0L6_2atmpS2713 = _M0L6_2atmpS2714 * _M0L6_2atmpS2715;
      _M0L6_2atmpS2711 = _M0L6_2atmpS2712 + _M0L6_2atmpS2713;
      #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L4w__sS2710, _M0L1kS1000, _M0L6_2atmpS2711);
      goto join_1001;
      goto joinlet_3241;
      join_1001:;
      _M0L6_2atmpS2593 = _M0L1kS1000 + 1;
      _M0L1kS1000 = _M0L6_2atmpS2593;
      continue;
      joinlet_3241:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23tripod__het__heun__step(
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L1pS961,
  float _M0L2dtS974,
  int32_t _M0L11store__tempS973
) {
  int32_t _M0L1nS960;
  struct _M0TPB8MutLocalGiE* _M0L1kS962;
  #line 212 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1nS960 = _M0L1pS961->$27;
  _M0L1kS962
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS962)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS962->$0 = 0;
  while (1) {
    int32_t _M0L3valS2396 = _M0L1kS962->$0;
    if (_M0L3valS2396 < _M0L1nS960) {
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2588 =
        _M0L1pS961->$0;
      struct _M0TPB5ArrayGfE* _M0L2vtS2586 = _M0L11soma__paramS2588->$0;
      int32_t _M0L3valS2587 = _M0L1kS962->$0;
      float _M0L2vtS963;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2585;
      struct _M0TPB5ArrayGfE* _M0L2elS2583;
      int32_t _M0L3valS2584;
      float _M0L2elS964;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2582;
      struct _M0TPB5ArrayGfE* _M0L2tmS2580;
      int32_t _M0L3valS2581;
      float _M0L2tmS965;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2579;
      struct _M0TPB5ArrayGfE* _M0L1rS2577;
      int32_t _M0L3valS2578;
      float _M0L6r__valS966;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2576;
      struct _M0TPB5ArrayGfE* _M0L9dt__slopeS2574;
      int32_t _M0L3valS2575;
      float _M0L9dt__slopeS967;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2573;
      struct _M0TPB5ArrayGfE* _M0L2twS2571;
      int32_t _M0L3valS2572;
      float _M0L2twS968;
      struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS2570;
      struct _M0TPB5ArrayGfE* _M0L1aS2568;
      int32_t _M0L3valS2569;
      float _M0L1aS969;
      struct _M0TPB5ArrayGfE* _M0L1cS2566;
      int32_t _M0L3valS2567;
      float _M0L6c__valS970;
      struct _M0TPB5ArrayGfE* _M0L2glS2564;
      int32_t _M0L3valS2565;
      float _M0L7gl__valS971;
      float _M0L2dsS972;
      float _M0L3dd1S975;
      float _M0L3dd2S976;
      float _M0L2dwS977;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2544;
      int32_t _M0L3valS2545;
      float _M0L6_2atmpS2543;
      float _M0L6_2atmpS2539;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2541;
      int32_t _M0L3valS2542;
      float _M0L6_2atmpS2540;
      float _M0L6_2atmpS2538;
      float _M0L6_2atmpS2537;
      float _M0L6_2atmpS2532;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2536;
      struct _M0TPB5ArrayGfE* _M0L3gaxS2534;
      int32_t _M0L3valS2535;
      float _M0L6_2atmpS2533;
      float _M0L3ic1S978;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2530;
      int32_t _M0L3valS2531;
      float _M0L6_2atmpS2529;
      float _M0L6_2atmpS2525;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2527;
      int32_t _M0L3valS2528;
      float _M0L6_2atmpS2526;
      float _M0L6_2atmpS2524;
      float _M0L6_2atmpS2523;
      float _M0L6_2atmpS2518;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2522;
      struct _M0TPB5ArrayGfE* _M0L3gaxS2520;
      int32_t _M0L3valS2521;
      float _M0L6_2atmpS2519;
      float _M0L3ic2S979;
      float _M0L9exp__termS980;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2506;
      int32_t _M0L3valS2507;
      float _M0L6_2atmpS2505;
      float _M0L6_2atmpS2504;
      float _M0L6_2atmpS2503;
      float _M0L6_2atmpS2502;
      float _M0L6_2atmpS2498;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2500;
      int32_t _M0L3valS2501;
      float _M0L6_2atmpS2499;
      float _M0L6_2atmpS2497;
      float _M0L6_2atmpS2493;
      struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS2495;
      int32_t _M0L3valS2496;
      float _M0L6_2atmpS2494;
      float _M0L6_2atmpS2491;
      float _M0L6_2atmpS2492;
      float _M0L6_2atmpS2487;
      struct _M0TPB5ArrayGfE* _M0L4i__sS2489;
      int32_t _M0L3valS2490;
      float _M0L6_2atmpS2488;
      float _M0L6_2atmpS2486;
      float _M0L10dv__s__valS981;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2484;
      int32_t _M0L3valS2485;
      float _M0L6_2atmpS2483;
      float _M0L6_2atmpS2482;
      float _M0L6_2atmpS2477;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2481;
      struct _M0TPB5ArrayGfE* _M0L2gmS2479;
      int32_t _M0L3valS2480;
      float _M0L6_2atmpS2478;
      float _M0L6_2atmpS2473;
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d1S2475;
      int32_t _M0L3valS2476;
      float _M0L6_2atmpS2474;
      float _M0L6_2atmpS2472;
      float _M0L6_2atmpS2468;
      struct _M0TPB5ArrayGfE* _M0L5i__d1S2470;
      int32_t _M0L3valS2471;
      float _M0L6_2atmpS2469;
      float _M0L6_2atmpS2463;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S2467;
      struct _M0TPB5ArrayGfE* _M0L1cS2465;
      int32_t _M0L3valS2466;
      float _M0L6_2atmpS2464;
      float _M0L11dv__d1__valS982;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2461;
      int32_t _M0L3valS2462;
      float _M0L6_2atmpS2460;
      float _M0L6_2atmpS2459;
      float _M0L6_2atmpS2454;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2458;
      struct _M0TPB5ArrayGfE* _M0L2gmS2456;
      int32_t _M0L3valS2457;
      float _M0L6_2atmpS2455;
      float _M0L6_2atmpS2450;
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d2S2452;
      int32_t _M0L3valS2453;
      float _M0L6_2atmpS2451;
      float _M0L6_2atmpS2449;
      float _M0L6_2atmpS2445;
      struct _M0TPB5ArrayGfE* _M0L5i__d2S2447;
      int32_t _M0L3valS2448;
      float _M0L6_2atmpS2446;
      float _M0L6_2atmpS2440;
      struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S2444;
      struct _M0TPB5ArrayGfE* _M0L1cS2442;
      int32_t _M0L3valS2443;
      float _M0L6_2atmpS2441;
      float _M0L11dv__d2__valS983;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2438;
      int32_t _M0L3valS2439;
      float _M0L6_2atmpS2437;
      float _M0L6_2atmpS2436;
      float _M0L6_2atmpS2435;
      float _M0L6_2atmpS2430;
      struct _M0TPB5ArrayGfE* _M0L4w__sS2433;
      int32_t _M0L3valS2434;
      float _M0L6_2atmpS2432;
      float _M0L6_2atmpS2431;
      float _M0L6_2atmpS2429;
      float _M0L7dw__valS984;
      int32_t _M0L3valS2428;
      int32_t _M0L6_2atmpS2427;
      #line 216 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L2vtS963 = _M0MPC15array5Array2atGfE(_M0L2vtS2586, _M0L3valS2587);
      _M0L11soma__paramS2585 = _M0L1pS961->$0;
      _M0L2elS2583 = _M0L11soma__paramS2585->$2;
      _M0L3valS2584 = _M0L1kS962->$0;
      #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L2elS964 = _M0MPC15array5Array2atGfE(_M0L2elS2583, _M0L3valS2584);
      _M0L11soma__paramS2582 = _M0L1pS961->$0;
      _M0L2tmS2580 = _M0L11soma__paramS2582->$3;
      _M0L3valS2581 = _M0L1kS962->$0;
      #line 218 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L2tmS965 = _M0MPC15array5Array2atGfE(_M0L2tmS2580, _M0L3valS2581);
      _M0L11soma__paramS2579 = _M0L1pS961->$0;
      _M0L1rS2577 = _M0L11soma__paramS2579->$4;
      _M0L3valS2578 = _M0L1kS962->$0;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6r__valS966 = _M0MPC15array5Array2atGfE(_M0L1rS2577, _M0L3valS2578);
      _M0L11soma__paramS2576 = _M0L1pS961->$0;
      _M0L9dt__slopeS2574 = _M0L11soma__paramS2576->$5;
      _M0L3valS2575 = _M0L1kS962->$0;
      #line 220 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L9dt__slopeS967
      = _M0MPC15array5Array2atGfE(_M0L9dt__slopeS2574, _M0L3valS2575);
      _M0L11soma__paramS2573 = _M0L1pS961->$0;
      _M0L2twS2571 = _M0L11soma__paramS2573->$6;
      _M0L3valS2572 = _M0L1kS962->$0;
      #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L2twS968 = _M0MPC15array5Array2atGfE(_M0L2twS2571, _M0L3valS2572);
      _M0L11soma__paramS2570 = _M0L1pS961->$0;
      _M0L1aS2568 = _M0L11soma__paramS2570->$7;
      _M0L3valS2569 = _M0L1kS962->$0;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L1aS969 = _M0MPC15array5Array2atGfE(_M0L1aS2568, _M0L3valS2569);
      _M0L1cS2566 = _M0L1pS961->$2;
      _M0L3valS2567 = _M0L1kS962->$0;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6c__valS970 = _M0MPC15array5Array2atGfE(_M0L1cS2566, _M0L3valS2567);
      _M0L2glS2564 = _M0L1pS961->$3;
      _M0L3valS2565 = _M0L1kS962->$0;
      #line 224 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L7gl__valS971
      = _M0MPC15array5Array2atGfE(_M0L2glS2564, _M0L3valS2565);
      if (_M0L11store__tempS973) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2561 = _M0L1pS961->$36;
        int32_t _M0L3valS2563 = _M0L1kS962->$0;
        int32_t _M0L6_2atmpS2562 = _M0L3valS2563 * 4;
        float _M0L6_2atmpS2560;
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2560
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2561, _M0L6_2atmpS2562);
        _M0L2dsS972 = _M0L6_2atmpS2560 * _M0L2dtS974;
      } else {
        _M0L2dsS972 = 0x0p+0f;
      }
      if (_M0L11store__tempS973) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2556 = _M0L1pS961->$36;
        int32_t _M0L3valS2559 = _M0L1kS962->$0;
        int32_t _M0L6_2atmpS2558 = _M0L3valS2559 * 4;
        int32_t _M0L6_2atmpS2557 = _M0L6_2atmpS2558 + 1;
        float _M0L6_2atmpS2555;
        #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2555
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2556, _M0L6_2atmpS2557);
        _M0L3dd1S975 = _M0L6_2atmpS2555 * _M0L2dtS974;
      } else {
        _M0L3dd1S975 = 0x0p+0f;
      }
      if (_M0L11store__tempS973) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2551 = _M0L1pS961->$36;
        int32_t _M0L3valS2554 = _M0L1kS962->$0;
        int32_t _M0L6_2atmpS2553 = _M0L3valS2554 * 4;
        int32_t _M0L6_2atmpS2552 = _M0L6_2atmpS2553 + 2;
        float _M0L6_2atmpS2550;
        #line 228 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2550
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2551, _M0L6_2atmpS2552);
        _M0L3dd2S976 = _M0L6_2atmpS2550 * _M0L2dtS974;
      } else {
        _M0L3dd2S976 = 0x0p+0f;
      }
      if (_M0L11store__tempS973) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2546 = _M0L1pS961->$36;
        int32_t _M0L3valS2549 = _M0L1kS962->$0;
        int32_t _M0L6_2atmpS2548 = _M0L3valS2549 * 4;
        int32_t _M0L6_2atmpS2547 = _M0L6_2atmpS2548 + 3;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L2dwS977
        = _M0MPC15array5Array2atGfE(_M0L8dv__tempS2546, _M0L6_2atmpS2547);
      } else {
        _M0L2dwS977 = 0x0p+0f;
      }
      _M0L5v__d1S2544 = _M0L1pS961->$30;
      _M0L3valS2545 = _M0L1kS962->$0;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2543
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2544, _M0L3valS2545);
      _M0L6_2atmpS2539 = _M0L6_2atmpS2543 + _M0L3dd1S975;
      _M0L4v__sS2541 = _M0L1pS961->$28;
      _M0L3valS2542 = _M0L1kS962->$0;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2540
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2541, _M0L3valS2542);
      _M0L6_2atmpS2538 = _M0L6_2atmpS2539 - _M0L6_2atmpS2540;
      _M0L6_2atmpS2537 = _M0L6_2atmpS2538 - _M0L2dsS972;
      _M0L6_2atmpS2532 = -_M0L6_2atmpS2537;
      _M0L2d1S2536 = _M0L1pS961->$4;
      _M0L3gaxS2534 = _M0L2d1S2536->$3;
      _M0L3valS2535 = _M0L1kS962->$0;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2533
      = _M0MPC15array5Array2atGfE(_M0L3gaxS2534, _M0L3valS2535);
      _M0L3ic1S978 = _M0L6_2atmpS2532 * _M0L6_2atmpS2533;
      _M0L5v__d2S2530 = _M0L1pS961->$31;
      _M0L3valS2531 = _M0L1kS962->$0;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2529
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2530, _M0L3valS2531);
      _M0L6_2atmpS2525 = _M0L6_2atmpS2529 + _M0L3dd2S976;
      _M0L4v__sS2527 = _M0L1pS961->$28;
      _M0L3valS2528 = _M0L1kS962->$0;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2526
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2527, _M0L3valS2528);
      _M0L6_2atmpS2524 = _M0L6_2atmpS2525 - _M0L6_2atmpS2526;
      _M0L6_2atmpS2523 = _M0L6_2atmpS2524 - _M0L2dsS972;
      _M0L6_2atmpS2518 = -_M0L6_2atmpS2523;
      _M0L2d2S2522 = _M0L1pS961->$5;
      _M0L3gaxS2520 = _M0L2d2S2522->$3;
      _M0L3valS2521 = _M0L1kS962->$0;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2519
      = _M0MPC15array5Array2atGfE(_M0L3gaxS2520, _M0L3valS2521);
      _M0L3ic2S979 = _M0L6_2atmpS2518 * _M0L6_2atmpS2519;
      if (_M0L9dt__slopeS967 < 0x0p+0f) {
        _M0L9exp__termS980 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L4v__sS2516 = _M0L1pS961->$28;
        int32_t _M0L3valS2517 = _M0L1kS962->$0;
        float _M0L6_2atmpS2515;
        float _M0L6_2atmpS2511;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS2513;
        int32_t _M0L3valS2514;
        float _M0L6_2atmpS2512;
        float _M0L6_2atmpS2510;
        float _M0L6_2atmpS2509;
        float _M0L6_2atmpS2508;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2515
        = _M0MPC15array5Array2atGfE(_M0L4v__sS2516, _M0L3valS2517);
        _M0L6_2atmpS2511 = _M0L6_2atmpS2515 + _M0L2dsS972;
        _M0L9thresholdS2513 = _M0L1pS961->$33;
        _M0L3valS2514 = _M0L1kS962->$0;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2512
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS2513, _M0L3valS2514);
        _M0L6_2atmpS2510 = _M0L6_2atmpS2511 - _M0L6_2atmpS2512;
        _M0L6_2atmpS2509 = _M0L6_2atmpS2510 / _M0L9dt__slopeS967;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0L6_2atmpS2508 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2509);
        _M0L9exp__termS980 = _M0L9dt__slopeS967 * _M0L6_2atmpS2508;
      }
      _M0L4v__sS2506 = _M0L1pS961->$28;
      _M0L3valS2507 = _M0L1kS962->$0;
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2505
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2506, _M0L3valS2507);
      _M0L6_2atmpS2504 = _M0L2elS964 - _M0L6_2atmpS2505;
      _M0L6_2atmpS2503 = _M0L6_2atmpS2504 - _M0L2dsS972;
      _M0L6_2atmpS2502 = _M0L7gl__valS971 * _M0L6_2atmpS2503;
      _M0L6_2atmpS2498 = _M0L6_2atmpS2502 + _M0L9exp__termS980;
      _M0L4w__sS2500 = _M0L1pS961->$29;
      _M0L3valS2501 = _M0L1kS962->$0;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2499
      = _M0MPC15array5Array2atGfE(_M0L4w__sS2500, _M0L3valS2501);
      _M0L6_2atmpS2497 = _M0L6_2atmpS2498 - _M0L6_2atmpS2499;
      _M0L6_2atmpS2493 = _M0L6_2atmpS2497 - _M0L2dwS977;
      _M0L12syn__curr__sS2495 = _M0L1pS961->$37;
      _M0L3valS2496 = _M0L1kS962->$0;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2494
      = _M0MPC15array5Array2atGfE(_M0L12syn__curr__sS2495, _M0L3valS2496);
      _M0L6_2atmpS2491 = _M0L6_2atmpS2493 - _M0L6_2atmpS2494;
      _M0L6_2atmpS2492 = _M0L3ic1S978 + _M0L3ic2S979;
      _M0L6_2atmpS2487 = _M0L6_2atmpS2491 - _M0L6_2atmpS2492;
      _M0L4i__sS2489 = _M0L1pS961->$6;
      _M0L3valS2490 = _M0L1kS962->$0;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2488
      = _M0MPC15array5Array2atGfE(_M0L4i__sS2489, _M0L3valS2490);
      _M0L6_2atmpS2486 = _M0L6_2atmpS2487 + _M0L6_2atmpS2488;
      _M0L10dv__s__valS981 = _M0L6_2atmpS2486 / _M0L6c__valS970;
      _M0L5v__d1S2484 = _M0L1pS961->$30;
      _M0L3valS2485 = _M0L1kS962->$0;
      #line 243 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2483
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2484, _M0L3valS2485);
      _M0L6_2atmpS2482 = _M0L2elS964 - _M0L6_2atmpS2483;
      _M0L6_2atmpS2477 = _M0L6_2atmpS2482 - _M0L3dd1S975;
      _M0L2d1S2481 = _M0L1pS961->$4;
      _M0L2gmS2479 = _M0L2d1S2481->$4;
      _M0L3valS2480 = _M0L1kS962->$0;
      #line 243 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2478
      = _M0MPC15array5Array2atGfE(_M0L2gmS2479, _M0L3valS2480);
      _M0L6_2atmpS2473 = _M0L6_2atmpS2477 * _M0L6_2atmpS2478;
      _M0L13syn__curr__d1S2475 = _M0L1pS961->$38;
      _M0L3valS2476 = _M0L1kS962->$0;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2474
      = _M0MPC15array5Array2atGfE(_M0L13syn__curr__d1S2475, _M0L3valS2476);
      _M0L6_2atmpS2472 = _M0L6_2atmpS2473 - _M0L6_2atmpS2474;
      _M0L6_2atmpS2468 = _M0L6_2atmpS2472 + _M0L3ic1S978;
      _M0L5i__d1S2470 = _M0L1pS961->$7;
      _M0L3valS2471 = _M0L1kS962->$0;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2469
      = _M0MPC15array5Array2atGfE(_M0L5i__d1S2470, _M0L3valS2471);
      _M0L6_2atmpS2463 = _M0L6_2atmpS2468 + _M0L6_2atmpS2469;
      _M0L2d1S2467 = _M0L1pS961->$4;
      _M0L1cS2465 = _M0L2d1S2467->$2;
      _M0L3valS2466 = _M0L1kS962->$0;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2464
      = _M0MPC15array5Array2atGfE(_M0L1cS2465, _M0L3valS2466);
      _M0L11dv__d1__valS982 = _M0L6_2atmpS2463 / _M0L6_2atmpS2464;
      _M0L5v__d2S2461 = _M0L1pS961->$31;
      _M0L3valS2462 = _M0L1kS962->$0;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2460
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2461, _M0L3valS2462);
      _M0L6_2atmpS2459 = _M0L2elS964 - _M0L6_2atmpS2460;
      _M0L6_2atmpS2454 = _M0L6_2atmpS2459 - _M0L3dd2S976;
      _M0L2d2S2458 = _M0L1pS961->$5;
      _M0L2gmS2456 = _M0L2d2S2458->$4;
      _M0L3valS2457 = _M0L1kS962->$0;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2455
      = _M0MPC15array5Array2atGfE(_M0L2gmS2456, _M0L3valS2457);
      _M0L6_2atmpS2450 = _M0L6_2atmpS2454 * _M0L6_2atmpS2455;
      _M0L13syn__curr__d2S2452 = _M0L1pS961->$39;
      _M0L3valS2453 = _M0L1kS962->$0;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2451
      = _M0MPC15array5Array2atGfE(_M0L13syn__curr__d2S2452, _M0L3valS2453);
      _M0L6_2atmpS2449 = _M0L6_2atmpS2450 - _M0L6_2atmpS2451;
      _M0L6_2atmpS2445 = _M0L6_2atmpS2449 + _M0L3ic2S979;
      _M0L5i__d2S2447 = _M0L1pS961->$8;
      _M0L3valS2448 = _M0L1kS962->$0;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2446
      = _M0MPC15array5Array2atGfE(_M0L5i__d2S2447, _M0L3valS2448);
      _M0L6_2atmpS2440 = _M0L6_2atmpS2445 + _M0L6_2atmpS2446;
      _M0L2d2S2444 = _M0L1pS961->$5;
      _M0L1cS2442 = _M0L2d2S2444->$2;
      _M0L3valS2443 = _M0L1kS962->$0;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2441
      = _M0MPC15array5Array2atGfE(_M0L1cS2442, _M0L3valS2443);
      _M0L11dv__d2__valS983 = _M0L6_2atmpS2440 / _M0L6_2atmpS2441;
      _M0L4v__sS2438 = _M0L1pS961->$28;
      _M0L3valS2439 = _M0L1kS962->$0;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2437
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2438, _M0L3valS2439);
      _M0L6_2atmpS2436 = _M0L6_2atmpS2437 + _M0L2dsS972;
      _M0L6_2atmpS2435 = _M0L6_2atmpS2436 - _M0L2elS964;
      _M0L6_2atmpS2430 = _M0L1aS969 * _M0L6_2atmpS2435;
      _M0L4w__sS2433 = _M0L1pS961->$29;
      _M0L3valS2434 = _M0L1kS962->$0;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2432
      = _M0MPC15array5Array2atGfE(_M0L4w__sS2433, _M0L3valS2434);
      _M0L6_2atmpS2431 = _M0L6_2atmpS2432 + _M0L2dwS977;
      _M0L6_2atmpS2429 = _M0L6_2atmpS2430 - _M0L6_2atmpS2431;
      _M0L7dw__valS984 = _M0L6_2atmpS2429 / _M0L2twS968;
      if (_M0L11store__tempS973) {
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2397 = _M0L1pS961->$36;
        int32_t _M0L3valS2399 = _M0L1kS962->$0;
        int32_t _M0L6_2atmpS2398 = _M0L3valS2399 * 4;
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2400;
        int32_t _M0L3valS2403;
        int32_t _M0L6_2atmpS2402;
        int32_t _M0L6_2atmpS2401;
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2404;
        int32_t _M0L3valS2407;
        int32_t _M0L6_2atmpS2406;
        int32_t _M0L6_2atmpS2405;
        struct _M0TPB5ArrayGfE* _M0L8dv__tempS2408;
        int32_t _M0L3valS2411;
        int32_t _M0L6_2atmpS2410;
        int32_t _M0L6_2atmpS2409;
        #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2397, _M0L6_2atmpS2398, _M0L10dv__s__valS981);
        _M0L8dv__tempS2400 = _M0L1pS961->$36;
        _M0L3valS2403 = _M0L1kS962->$0;
        _M0L6_2atmpS2402 = _M0L3valS2403 * 4;
        _M0L6_2atmpS2401 = _M0L6_2atmpS2402 + 1;
        #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2400, _M0L6_2atmpS2401, _M0L11dv__d1__valS982);
        _M0L8dv__tempS2404 = _M0L1pS961->$36;
        _M0L3valS2407 = _M0L1kS962->$0;
        _M0L6_2atmpS2406 = _M0L3valS2407 * 4;
        _M0L6_2atmpS2405 = _M0L6_2atmpS2406 + 2;
        #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2404, _M0L6_2atmpS2405, _M0L11dv__d2__valS983);
        _M0L8dv__tempS2408 = _M0L1pS961->$36;
        _M0L3valS2411 = _M0L1kS962->$0;
        _M0L6_2atmpS2410 = _M0L3valS2411 * 4;
        _M0L6_2atmpS2409 = _M0L6_2atmpS2410 + 3;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L8dv__tempS2408, _M0L6_2atmpS2409, _M0L7dw__valS984);
      } else {
        struct _M0TPB5ArrayGfE* _M0L2dvS2412 = _M0L1pS961->$35;
        int32_t _M0L3valS2414 = _M0L1kS962->$0;
        int32_t _M0L6_2atmpS2413 = _M0L3valS2414 * 4;
        struct _M0TPB5ArrayGfE* _M0L2dvS2415;
        int32_t _M0L3valS2418;
        int32_t _M0L6_2atmpS2417;
        int32_t _M0L6_2atmpS2416;
        struct _M0TPB5ArrayGfE* _M0L2dvS2419;
        int32_t _M0L3valS2422;
        int32_t _M0L6_2atmpS2421;
        int32_t _M0L6_2atmpS2420;
        struct _M0TPB5ArrayGfE* _M0L2dvS2423;
        int32_t _M0L3valS2426;
        int32_t _M0L6_2atmpS2425;
        int32_t _M0L6_2atmpS2424;
        #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2412, _M0L6_2atmpS2413, _M0L10dv__s__valS981);
        _M0L2dvS2415 = _M0L1pS961->$35;
        _M0L3valS2418 = _M0L1kS962->$0;
        _M0L6_2atmpS2417 = _M0L3valS2418 * 4;
        _M0L6_2atmpS2416 = _M0L6_2atmpS2417 + 1;
        #line 257 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2415, _M0L6_2atmpS2416, _M0L11dv__d1__valS982);
        _M0L2dvS2419 = _M0L1pS961->$35;
        _M0L3valS2422 = _M0L1kS962->$0;
        _M0L6_2atmpS2421 = _M0L3valS2422 * 4;
        _M0L6_2atmpS2420 = _M0L6_2atmpS2421 + 2;
        #line 258 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2419, _M0L6_2atmpS2420, _M0L11dv__d2__valS983);
        _M0L2dvS2423 = _M0L1pS961->$35;
        _M0L3valS2426 = _M0L1kS962->$0;
        _M0L6_2atmpS2425 = _M0L3valS2426 * 4;
        _M0L6_2atmpS2424 = _M0L6_2atmpS2425 + 3;
        #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
        _M0MPC15array5Array3setGfE(_M0L2dvS2423, _M0L6_2atmpS2424, _M0L7dw__valS984);
      }
      _M0L3valS2428 = _M0L1kS962->$0;
      _M0L6_2atmpS2427 = _M0L3valS2428 + 1;
      _M0L1kS962->$0 = _M0L6_2atmpS2427;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS962);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt29tripod__het__syn__curr__dends(
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L1pS956
) {
  int32_t _M0L1nS955;
  int32_t _M0L7_2abindS957;
  int32_t _M0L1iS958;
  #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1nS955 = _M0L1pS956->$27;
  _M0L7_2abindS957 = 0;
  _M0L1iS958 = _M0L7_2abindS957;
  while (1) {
    if (_M0L1iS958 < _M0L1nS955) {
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d1S2355 = _M0L1pS956->$38;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2374 = _M0L1pS956->$11;
      float _M0L6_2atmpS2369;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2373;
      float _M0L6_2atmpS2371;
      float _M0L4e__eS2372;
      float _M0L6_2atmpS2370;
      float _M0L6_2atmpS2367;
      float _M0L7gsyn__eS2368;
      float _M0L6_2atmpS2357;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2366;
      float _M0L6_2atmpS2361;
      struct _M0TPB5ArrayGfE* _M0L5v__d1S2365;
      float _M0L6_2atmpS2363;
      float _M0L4e__iS2364;
      float _M0L6_2atmpS2362;
      float _M0L6_2atmpS2359;
      float _M0L7gsyn__iS2360;
      float _M0L6_2atmpS2358;
      float _M0L6_2atmpS2356;
      struct _M0TPB5ArrayGfE* _M0L13syn__curr__d2S2375;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2394;
      float _M0L6_2atmpS2389;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2393;
      float _M0L6_2atmpS2391;
      float _M0L4e__eS2392;
      float _M0L6_2atmpS2390;
      float _M0L6_2atmpS2387;
      float _M0L7gsyn__eS2388;
      float _M0L6_2atmpS2377;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2386;
      float _M0L6_2atmpS2381;
      struct _M0TPB5ArrayGfE* _M0L5v__d2S2385;
      float _M0L6_2atmpS2383;
      float _M0L4e__iS2384;
      float _M0L6_2atmpS2382;
      float _M0L6_2atmpS2379;
      float _M0L7gsyn__iS2380;
      float _M0L6_2atmpS2378;
      float _M0L6_2atmpS2376;
      int32_t _M0L6_2atmpS2395;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2369
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S2374, _M0L1iS958);
      _M0L5v__d1S2373 = _M0L1pS956->$30;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2371
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2373, _M0L1iS958);
      _M0L4e__eS2372 = _M0L1pS956->$21;
      _M0L6_2atmpS2370 = _M0L6_2atmpS2371 - _M0L4e__eS2372;
      _M0L6_2atmpS2367 = _M0L6_2atmpS2369 * _M0L6_2atmpS2370;
      _M0L7gsyn__eS2368 = _M0L1pS956->$25;
      _M0L6_2atmpS2357 = _M0L6_2atmpS2367 * _M0L7gsyn__eS2368;
      _M0L6gi__d1S2366 = _M0L1pS956->$12;
      #line 202 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2361
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S2366, _M0L1iS958);
      _M0L5v__d1S2365 = _M0L1pS956->$30;
      #line 202 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2363
      = _M0MPC15array5Array2atGfE(_M0L5v__d1S2365, _M0L1iS958);
      _M0L4e__iS2364 = _M0L1pS956->$22;
      _M0L6_2atmpS2362 = _M0L6_2atmpS2363 - _M0L4e__iS2364;
      _M0L6_2atmpS2359 = _M0L6_2atmpS2361 * _M0L6_2atmpS2362;
      _M0L7gsyn__iS2360 = _M0L1pS956->$26;
      _M0L6_2atmpS2358 = _M0L6_2atmpS2359 * _M0L7gsyn__iS2360;
      _M0L6_2atmpS2356 = _M0L6_2atmpS2357 + _M0L6_2atmpS2358;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L13syn__curr__d1S2355, _M0L1iS958, _M0L6_2atmpS2356);
      _M0L13syn__curr__d2S2375 = _M0L1pS956->$39;
      _M0L6ge__d2S2394 = _M0L1pS956->$13;
      #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2389
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2394, _M0L1iS958);
      _M0L5v__d2S2393 = _M0L1pS956->$31;
      #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2391
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2393, _M0L1iS958);
      _M0L4e__eS2392 = _M0L1pS956->$21;
      _M0L6_2atmpS2390 = _M0L6_2atmpS2391 - _M0L4e__eS2392;
      _M0L6_2atmpS2387 = _M0L6_2atmpS2389 * _M0L6_2atmpS2390;
      _M0L7gsyn__eS2388 = _M0L1pS956->$25;
      _M0L6_2atmpS2377 = _M0L6_2atmpS2387 * _M0L7gsyn__eS2388;
      _M0L6gi__d2S2386 = _M0L1pS956->$14;
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2381
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2386, _M0L1iS958);
      _M0L5v__d2S2385 = _M0L1pS956->$31;
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2383
      = _M0MPC15array5Array2atGfE(_M0L5v__d2S2385, _M0L1iS958);
      _M0L4e__iS2384 = _M0L1pS956->$22;
      _M0L6_2atmpS2382 = _M0L6_2atmpS2383 - _M0L4e__iS2384;
      _M0L6_2atmpS2379 = _M0L6_2atmpS2381 * _M0L6_2atmpS2382;
      _M0L7gsyn__iS2380 = _M0L1pS956->$26;
      _M0L6_2atmpS2378 = _M0L6_2atmpS2379 * _M0L7gsyn__iS2380;
      _M0L6_2atmpS2376 = _M0L6_2atmpS2377 + _M0L6_2atmpS2378;
      #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L13syn__curr__d2S2375, _M0L1iS958, _M0L6_2atmpS2376);
      _M0L6_2atmpS2395 = _M0L1iS958 + 1;
      _M0L1iS958 = _M0L6_2atmpS2395;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28tripod__het__syn__curr__soma(
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L1pS951
) {
  int32_t _M0L1nS950;
  int32_t _M0L7_2abindS952;
  int32_t _M0L1iS953;
  #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1nS950 = _M0L1pS951->$27;
  _M0L7_2abindS952 = 0;
  _M0L1iS953 = _M0L7_2abindS952;
  while (1) {
    if (_M0L1iS953 < _M0L1nS950) {
      struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS2334 = _M0L1pS951->$37;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2353 = _M0L1pS951->$9;
      float _M0L6_2atmpS2348;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2352;
      float _M0L6_2atmpS2350;
      float _M0L4e__eS2351;
      float _M0L6_2atmpS2349;
      float _M0L6_2atmpS2346;
      float _M0L7gsyn__eS2347;
      float _M0L6_2atmpS2336;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2345;
      float _M0L6_2atmpS2340;
      struct _M0TPB5ArrayGfE* _M0L4v__sS2344;
      float _M0L6_2atmpS2342;
      float _M0L4e__iS2343;
      float _M0L6_2atmpS2341;
      float _M0L6_2atmpS2338;
      float _M0L7gsyn__iS2339;
      float _M0L6_2atmpS2337;
      float _M0L6_2atmpS2335;
      int32_t _M0L6_2atmpS2354;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2348
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2353, _M0L1iS953);
      _M0L4v__sS2352 = _M0L1pS951->$28;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2350
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2352, _M0L1iS953);
      _M0L4e__eS2351 = _M0L1pS951->$21;
      _M0L6_2atmpS2349 = _M0L6_2atmpS2350 - _M0L4e__eS2351;
      _M0L6_2atmpS2346 = _M0L6_2atmpS2348 * _M0L6_2atmpS2349;
      _M0L7gsyn__eS2347 = _M0L1pS951->$25;
      _M0L6_2atmpS2336 = _M0L6_2atmpS2346 * _M0L7gsyn__eS2347;
      _M0L5gi__sS2345 = _M0L1pS951->$10;
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2340
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2345, _M0L1iS953);
      _M0L4v__sS2344 = _M0L1pS951->$28;
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2342
      = _M0MPC15array5Array2atGfE(_M0L4v__sS2344, _M0L1iS953);
      _M0L4e__iS2343 = _M0L1pS951->$22;
      _M0L6_2atmpS2341 = _M0L6_2atmpS2342 - _M0L4e__iS2343;
      _M0L6_2atmpS2338 = _M0L6_2atmpS2340 * _M0L6_2atmpS2341;
      _M0L7gsyn__iS2339 = _M0L1pS951->$26;
      _M0L6_2atmpS2337 = _M0L6_2atmpS2338 * _M0L7gsyn__iS2339;
      _M0L6_2atmpS2335 = _M0L6_2atmpS2336 + _M0L6_2atmpS2337;
      #line 191 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L12syn__curr__sS2334, _M0L1iS953, _M0L6_2atmpS2335);
      _M0L6_2atmpS2354 = _M0L1iS953 + 1;
      _M0L1iS953 = _M0L6_2atmpS2354;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt33tripod__het__dend__step__synapses(
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L1pS942,
  float _M0L2dtS945
) {
  int32_t _M0L1nS941;
  int32_t _M0L7_2abindS943;
  int32_t _M0L1iS944;
  int32_t _M0L7_2abindS947;
  int32_t _M0L1iS948;
  #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1nS941 = _M0L1pS942->$27;
  _M0L7_2abindS943 = 0;
  _M0L1iS944 = _M0L7_2abindS943;
  while (1) {
    if (_M0L1iS944 < _M0L1nS941) {
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2264 = _M0L1pS942->$11;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2269 = _M0L1pS942->$11;
      float _M0L6_2atmpS2266;
      struct _M0TPB5ArrayGfE* _M0L7glu__d1S2268;
      float _M0L6_2atmpS2267;
      float _M0L6_2atmpS2265;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2270;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2275;
      float _M0L6_2atmpS2272;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d1S2274;
      float _M0L6_2atmpS2273;
      float _M0L6_2atmpS2271;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2276;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2285;
      float _M0L6_2atmpS2278;
      struct _M0TPB5ArrayGfE* _M0L6ge__d1S2284;
      float _M0L6_2atmpS2283;
      float _M0L6_2atmpS2281;
      float _M0L6tau__eS2282;
      float _M0L6_2atmpS2280;
      float _M0L6_2atmpS2279;
      float _M0L6_2atmpS2277;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2286;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2295;
      float _M0L6_2atmpS2288;
      struct _M0TPB5ArrayGfE* _M0L6gi__d1S2294;
      float _M0L6_2atmpS2293;
      float _M0L6_2atmpS2291;
      float _M0L6tau__iS2292;
      float _M0L6_2atmpS2290;
      float _M0L6_2atmpS2289;
      float _M0L6_2atmpS2287;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2296;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2301;
      float _M0L6_2atmpS2298;
      struct _M0TPB5ArrayGfE* _M0L7glu__d2S2300;
      float _M0L6_2atmpS2299;
      float _M0L6_2atmpS2297;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2302;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2307;
      float _M0L6_2atmpS2304;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d2S2306;
      float _M0L6_2atmpS2305;
      float _M0L6_2atmpS2303;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2308;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2317;
      float _M0L6_2atmpS2310;
      struct _M0TPB5ArrayGfE* _M0L6ge__d2S2316;
      float _M0L6_2atmpS2315;
      float _M0L6_2atmpS2313;
      float _M0L6tau__eS2314;
      float _M0L6_2atmpS2312;
      float _M0L6_2atmpS2311;
      float _M0L6_2atmpS2309;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2318;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2327;
      float _M0L6_2atmpS2320;
      struct _M0TPB5ArrayGfE* _M0L6gi__d2S2326;
      float _M0L6_2atmpS2325;
      float _M0L6_2atmpS2323;
      float _M0L6tau__iS2324;
      float _M0L6_2atmpS2322;
      float _M0L6_2atmpS2321;
      float _M0L6_2atmpS2319;
      int32_t _M0L6_2atmpS2328;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2266
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S2269, _M0L1iS944);
      _M0L7glu__d1S2268 = _M0L1pS942->$17;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2267
      = _M0MPC15array5Array2atGfE(_M0L7glu__d1S2268, _M0L1iS944);
      _M0L6_2atmpS2265 = _M0L6_2atmpS2266 + _M0L6_2atmpS2267;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d1S2264, _M0L1iS944, _M0L6_2atmpS2265);
      _M0L6gi__d1S2270 = _M0L1pS942->$12;
      _M0L6gi__d1S2275 = _M0L1pS942->$12;
      #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2272
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S2275, _M0L1iS944);
      _M0L8gaba__d1S2274 = _M0L1pS942->$18;
      #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2273
      = _M0MPC15array5Array2atGfE(_M0L8gaba__d1S2274, _M0L1iS944);
      _M0L6_2atmpS2271 = _M0L6_2atmpS2272 + _M0L6_2atmpS2273;
      #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d1S2270, _M0L1iS944, _M0L6_2atmpS2271);
      _M0L6ge__d1S2276 = _M0L1pS942->$11;
      _M0L6ge__d1S2285 = _M0L1pS942->$11;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2278
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S2285, _M0L1iS944);
      _M0L6ge__d1S2284 = _M0L1pS942->$11;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2283
      = _M0MPC15array5Array2atGfE(_M0L6ge__d1S2284, _M0L1iS944);
      _M0L6_2atmpS2281 = -_M0L6_2atmpS2283;
      _M0L6tau__eS2282 = _M0L1pS942->$23;
      _M0L6_2atmpS2280 = _M0L6_2atmpS2281 / _M0L6tau__eS2282;
      _M0L6_2atmpS2279 = _M0L2dtS945 * _M0L6_2atmpS2280;
      _M0L6_2atmpS2277 = _M0L6_2atmpS2278 + _M0L6_2atmpS2279;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d1S2276, _M0L1iS944, _M0L6_2atmpS2277);
      _M0L6gi__d1S2286 = _M0L1pS942->$12;
      _M0L6gi__d1S2295 = _M0L1pS942->$12;
      #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2288
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S2295, _M0L1iS944);
      _M0L6gi__d1S2294 = _M0L1pS942->$12;
      #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2293
      = _M0MPC15array5Array2atGfE(_M0L6gi__d1S2294, _M0L1iS944);
      _M0L6_2atmpS2291 = -_M0L6_2atmpS2293;
      _M0L6tau__iS2292 = _M0L1pS942->$24;
      _M0L6_2atmpS2290 = _M0L6_2atmpS2291 / _M0L6tau__iS2292;
      _M0L6_2atmpS2289 = _M0L2dtS945 * _M0L6_2atmpS2290;
      _M0L6_2atmpS2287 = _M0L6_2atmpS2288 + _M0L6_2atmpS2289;
      #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d1S2286, _M0L1iS944, _M0L6_2atmpS2287);
      _M0L6ge__d2S2296 = _M0L1pS942->$13;
      _M0L6ge__d2S2301 = _M0L1pS942->$13;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2298
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2301, _M0L1iS944);
      _M0L7glu__d2S2300 = _M0L1pS942->$19;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2299
      = _M0MPC15array5Array2atGfE(_M0L7glu__d2S2300, _M0L1iS944);
      _M0L6_2atmpS2297 = _M0L6_2atmpS2298 + _M0L6_2atmpS2299;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d2S2296, _M0L1iS944, _M0L6_2atmpS2297);
      _M0L6gi__d2S2302 = _M0L1pS942->$14;
      _M0L6gi__d2S2307 = _M0L1pS942->$14;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2304
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2307, _M0L1iS944);
      _M0L8gaba__d2S2306 = _M0L1pS942->$20;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2305
      = _M0MPC15array5Array2atGfE(_M0L8gaba__d2S2306, _M0L1iS944);
      _M0L6_2atmpS2303 = _M0L6_2atmpS2304 + _M0L6_2atmpS2305;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d2S2302, _M0L1iS944, _M0L6_2atmpS2303);
      _M0L6ge__d2S2308 = _M0L1pS942->$13;
      _M0L6ge__d2S2317 = _M0L1pS942->$13;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2310
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2317, _M0L1iS944);
      _M0L6ge__d2S2316 = _M0L1pS942->$13;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2315
      = _M0MPC15array5Array2atGfE(_M0L6ge__d2S2316, _M0L1iS944);
      _M0L6_2atmpS2313 = -_M0L6_2atmpS2315;
      _M0L6tau__eS2314 = _M0L1pS942->$23;
      _M0L6_2atmpS2312 = _M0L6_2atmpS2313 / _M0L6tau__eS2314;
      _M0L6_2atmpS2311 = _M0L2dtS945 * _M0L6_2atmpS2312;
      _M0L6_2atmpS2309 = _M0L6_2atmpS2310 + _M0L6_2atmpS2311;
      #line 175 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6ge__d2S2308, _M0L1iS944, _M0L6_2atmpS2309);
      _M0L6gi__d2S2318 = _M0L1pS942->$14;
      _M0L6gi__d2S2327 = _M0L1pS942->$14;
      #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2320
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2327, _M0L1iS944);
      _M0L6gi__d2S2326 = _M0L1pS942->$14;
      #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2325
      = _M0MPC15array5Array2atGfE(_M0L6gi__d2S2326, _M0L1iS944);
      _M0L6_2atmpS2323 = -_M0L6_2atmpS2325;
      _M0L6tau__iS2324 = _M0L1pS942->$24;
      _M0L6_2atmpS2322 = _M0L6_2atmpS2323 / _M0L6tau__iS2324;
      _M0L6_2atmpS2321 = _M0L2dtS945 * _M0L6_2atmpS2322;
      _M0L6_2atmpS2319 = _M0L6_2atmpS2320 + _M0L6_2atmpS2321;
      #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6gi__d2S2318, _M0L1iS944, _M0L6_2atmpS2319);
      _M0L6_2atmpS2328 = _M0L1iS944 + 1;
      _M0L1iS944 = _M0L6_2atmpS2328;
      continue;
    }
    break;
  }
  _M0L7_2abindS947 = 0;
  _M0L1iS948 = _M0L7_2abindS947;
  while (1) {
    if (_M0L1iS948 < _M0L1nS941) {
      struct _M0TPB5ArrayGfE* _M0L7glu__d1S2329 = _M0L1pS942->$17;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d1S2330;
      struct _M0TPB5ArrayGfE* _M0L7glu__d2S2331;
      struct _M0TPB5ArrayGfE* _M0L8gaba__d2S2332;
      int32_t _M0L6_2atmpS2333;
      #line 179 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L7glu__d1S2329, _M0L1iS948, 0x0p+0f);
      _M0L8gaba__d1S2330 = _M0L1pS942->$18;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L8gaba__d1S2330, _M0L1iS948, 0x0p+0f);
      _M0L7glu__d2S2331 = _M0L1pS942->$19;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L7glu__d2S2331, _M0L1iS948, 0x0p+0f);
      _M0L8gaba__d2S2332 = _M0L1pS942->$20;
      #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L8gaba__d2S2332, _M0L1iS948, 0x0p+0f);
      _M0L6_2atmpS2333 = _M0L1iS948 + 1;
      _M0L1iS948 = _M0L6_2atmpS2333;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt33tripod__het__soma__step__synapses(
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L1pS933,
  float _M0L2dtS936
) {
  int32_t _M0L1nS932;
  int32_t _M0L7_2abindS934;
  int32_t _M0L1iS935;
  int32_t _M0L7_2abindS938;
  int32_t _M0L1iS939;
  #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1nS932 = _M0L1pS933->$27;
  _M0L7_2abindS934 = 0;
  _M0L1iS935 = _M0L7_2abindS934;
  while (1) {
    if (_M0L1iS935 < _M0L1nS932) {
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2228 = _M0L1pS933->$9;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2233 = _M0L1pS933->$9;
      float _M0L6_2atmpS2230;
      struct _M0TPB5ArrayGfE* _M0L6glu__sS2232;
      float _M0L6_2atmpS2231;
      float _M0L6_2atmpS2229;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2234;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2239;
      float _M0L6_2atmpS2236;
      struct _M0TPB5ArrayGfE* _M0L7gaba__sS2238;
      float _M0L6_2atmpS2237;
      float _M0L6_2atmpS2235;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2240;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2249;
      float _M0L6_2atmpS2242;
      struct _M0TPB5ArrayGfE* _M0L5ge__sS2248;
      float _M0L6_2atmpS2247;
      float _M0L6_2atmpS2245;
      float _M0L6tau__eS2246;
      float _M0L6_2atmpS2244;
      float _M0L6_2atmpS2243;
      float _M0L6_2atmpS2241;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2250;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2259;
      float _M0L6_2atmpS2252;
      struct _M0TPB5ArrayGfE* _M0L5gi__sS2258;
      float _M0L6_2atmpS2257;
      float _M0L6_2atmpS2255;
      float _M0L6tau__iS2256;
      float _M0L6_2atmpS2254;
      float _M0L6_2atmpS2253;
      float _M0L6_2atmpS2251;
      int32_t _M0L6_2atmpS2260;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2230
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2233, _M0L1iS935);
      _M0L6glu__sS2232 = _M0L1pS933->$15;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2231
      = _M0MPC15array5Array2atGfE(_M0L6glu__sS2232, _M0L1iS935);
      _M0L6_2atmpS2229 = _M0L6_2atmpS2230 + _M0L6_2atmpS2231;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5ge__sS2228, _M0L1iS935, _M0L6_2atmpS2229);
      _M0L5gi__sS2234 = _M0L1pS933->$10;
      _M0L5gi__sS2239 = _M0L1pS933->$10;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2236
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2239, _M0L1iS935);
      _M0L7gaba__sS2238 = _M0L1pS933->$16;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2237
      = _M0MPC15array5Array2atGfE(_M0L7gaba__sS2238, _M0L1iS935);
      _M0L6_2atmpS2235 = _M0L6_2atmpS2236 + _M0L6_2atmpS2237;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5gi__sS2234, _M0L1iS935, _M0L6_2atmpS2235);
      _M0L5ge__sS2240 = _M0L1pS933->$9;
      _M0L5ge__sS2249 = _M0L1pS933->$9;
      #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2242
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2249, _M0L1iS935);
      _M0L5ge__sS2248 = _M0L1pS933->$9;
      #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2247
      = _M0MPC15array5Array2atGfE(_M0L5ge__sS2248, _M0L1iS935);
      _M0L6_2atmpS2245 = -_M0L6_2atmpS2247;
      _M0L6tau__eS2246 = _M0L1pS933->$23;
      _M0L6_2atmpS2244 = _M0L6_2atmpS2245 / _M0L6tau__eS2246;
      _M0L6_2atmpS2243 = _M0L2dtS936 * _M0L6_2atmpS2244;
      _M0L6_2atmpS2241 = _M0L6_2atmpS2242 + _M0L6_2atmpS2243;
      #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5ge__sS2240, _M0L1iS935, _M0L6_2atmpS2241);
      _M0L5gi__sS2250 = _M0L1pS933->$10;
      _M0L5gi__sS2259 = _M0L1pS933->$10;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2252
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2259, _M0L1iS935);
      _M0L5gi__sS2258 = _M0L1pS933->$10;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2257
      = _M0MPC15array5Array2atGfE(_M0L5gi__sS2258, _M0L1iS935);
      _M0L6_2atmpS2255 = -_M0L6_2atmpS2257;
      _M0L6tau__iS2256 = _M0L1pS933->$24;
      _M0L6_2atmpS2254 = _M0L6_2atmpS2255 / _M0L6tau__iS2256;
      _M0L6_2atmpS2253 = _M0L2dtS936 * _M0L6_2atmpS2254;
      _M0L6_2atmpS2251 = _M0L6_2atmpS2252 + _M0L6_2atmpS2253;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5gi__sS2250, _M0L1iS935, _M0L6_2atmpS2251);
      _M0L6_2atmpS2260 = _M0L1iS935 + 1;
      _M0L1iS935 = _M0L6_2atmpS2260;
      continue;
    }
    break;
  }
  _M0L7_2abindS938 = 0;
  _M0L1iS939 = _M0L7_2abindS938;
  while (1) {
    if (_M0L1iS939 < _M0L1nS932) {
      struct _M0TPB5ArrayGfE* _M0L6glu__sS2261 = _M0L1pS933->$15;
      struct _M0TPB5ArrayGfE* _M0L7gaba__sS2262;
      int32_t _M0L6_2atmpS2263;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L6glu__sS2261, _M0L1iS939, 0x0p+0f);
      _M0L7gaba__sS2262 = _M0L1pS933->$16;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L7gaba__sS2262, _M0L1iS939, 0x0p+0f);
      _M0L6_2atmpS2263 = _M0L1iS939 + 1;
      _M0L1iS939 = _M0L6_2atmpS2263;
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt9TripodHet* _M0MP26RiantR8snn__mbt9TripodHet3new(
  int32_t _M0L1nS888,
  struct _M0TP26RiantR8snn__mbt16AdExParameterHet* _M0L11soma__paramS892,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS900
) {
  struct _M0TPB5ArrayGfE* _M0L1cS887;
  struct _M0TPB5ArrayGfE* _M0L2glS889;
  int32_t _M0L7_2abindS890;
  int32_t _M0L1kS891;
  struct _M0TPB5ArrayGfE* _M0L4v__sS894;
  struct _M0TPB5ArrayGfE* _M0L5v__d1S895;
  struct _M0TPB5ArrayGfE* _M0L5v__d2S896;
  int32_t _M0L7_2abindS897;
  int32_t _M0L1kS898;
  struct _M0TPB5ArrayGfE* _M0L4w__sS902;
  struct _M0TPB5ArrayGbE* _M0L4fireS903;
  struct _M0TPB5ArrayGfE* _M0L9thresholdS904;
  int32_t _M0L7_2abindS905;
  int32_t _M0L1kS906;
  struct _M0TPB5ArrayGiE* _M0L4tabsS908;
  struct _M0TPB5ArrayGfE* _M0L4i__sS909;
  struct _M0TPB5ArrayGfE* _M0L5i__d1S910;
  struct _M0TPB5ArrayGfE* _M0L5i__d2S911;
  struct _M0TPB5ArrayGfE* _M0L5ge__sS912;
  struct _M0TPB5ArrayGfE* _M0L5gi__sS913;
  struct _M0TPB5ArrayGfE* _M0L6ge__d1S914;
  struct _M0TPB5ArrayGfE* _M0L6gi__d1S915;
  struct _M0TPB5ArrayGfE* _M0L6ge__d2S916;
  struct _M0TPB5ArrayGfE* _M0L6gi__d2S917;
  struct _M0TPB5ArrayGfE* _M0L6glu__sS918;
  struct _M0TPB5ArrayGfE* _M0L7gaba__sS919;
  struct _M0TPB5ArrayGfE* _M0L7glu__d1S920;
  struct _M0TPB5ArrayGfE* _M0L8gaba__d1S921;
  struct _M0TPB5ArrayGfE* _M0L7glu__d2S922;
  struct _M0TPB5ArrayGfE* _M0L8gaba__d2S923;
  int32_t _M0L6total4S924;
  struct _M0TPB5ArrayGfE* _M0L2dvS925;
  struct _M0TPB5ArrayGfE* _M0L8dv__tempS926;
  struct _M0TPB5ArrayGfE* _M0L12syn__curr__sS927;
  struct _M0TPB5ArrayGfE* _M0L13syn__curr__d1S928;
  struct _M0TPB5ArrayGfE* _M0L13syn__curr__d2S929;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d1S930;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _M0L2d2S931;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L6_2atmpS2227;
  struct _M0TP26RiantR8snn__mbt9TripodHet* _block_3252;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L1cS887 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 84 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L2glS889 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  _M0L7_2abindS890 = 0;
  _M0L1kS891 = _M0L7_2abindS890;
  while (1) {
    if (_M0L1kS891 < _M0L1nS888) {
      struct _M0TPB5ArrayGfE* _M0L2tmS2199 = _M0L11soma__paramS892->$3;
      float _M0L6_2atmpS2195;
      struct _M0TPB5ArrayGfE* _M0L1rS2198;
      float _M0L6_2atmpS2197;
      float _M0L6_2atmpS2196;
      float _M0L6_2atmpS2194;
      struct _M0TPB5ArrayGfE* _M0L1rS2202;
      float _M0L6_2atmpS2201;
      float _M0L6_2atmpS2200;
      int32_t _M0L6_2atmpS2203;
      #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2195 = _M0MPC15array5Array2atGfE(_M0L2tmS2199, _M0L1kS891);
      _M0L1rS2198 = _M0L11soma__paramS892->$4;
      #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2197 = _M0MPC15array5Array2atGfE(_M0L1rS2198, _M0L1kS891);
      _M0L6_2atmpS2196 = 0x1p+0f / _M0L6_2atmpS2197;
      _M0L6_2atmpS2194 = _M0L6_2atmpS2195 * _M0L6_2atmpS2196;
      #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L1cS887, _M0L1kS891, _M0L6_2atmpS2194);
      _M0L1rS2202 = _M0L11soma__paramS892->$4;
      #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2201 = _M0MPC15array5Array2atGfE(_M0L1rS2202, _M0L1kS891);
      _M0L6_2atmpS2200 = 0x1p+0f / _M0L6_2atmpS2201;
      #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L2glS889, _M0L1kS891, _M0L6_2atmpS2200);
      _M0L6_2atmpS2203 = _M0L1kS891 + 1;
      _M0L1kS891 = _M0L6_2atmpS2203;
      continue;
    }
    break;
  }
  #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L4v__sS894 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 93 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L5v__d1S895 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L5v__d2S896 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  _M0L7_2abindS897 = 0;
  _M0L1kS898 = _M0L7_2abindS897;
  while (1) {
    if (_M0L1kS898 < _M0L1nS888) {
      struct _M0TPB5ArrayGfE* _M0L2vtS2222 = _M0L11soma__paramS892->$0;
      float _M0L6_2atmpS2219;
      struct _M0TPB5ArrayGfE* _M0L2vrS2221;
      float _M0L6_2atmpS2220;
      float _M0L6spreadS899;
      struct _M0TPB5ArrayGfE* _M0L2vrS2208;
      float _M0L6_2atmpS2205;
      float _M0L6_2atmpS2207;
      float _M0L6_2atmpS2206;
      float _M0L6_2atmpS2204;
      struct _M0TPB5ArrayGfE* _M0L2vrS2213;
      float _M0L6_2atmpS2210;
      float _M0L6_2atmpS2212;
      float _M0L6_2atmpS2211;
      float _M0L6_2atmpS2209;
      struct _M0TPB5ArrayGfE* _M0L2vrS2218;
      float _M0L6_2atmpS2215;
      float _M0L6_2atmpS2217;
      float _M0L6_2atmpS2216;
      float _M0L6_2atmpS2214;
      int32_t _M0L6_2atmpS2223;
      #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2219 = _M0MPC15array5Array2atGfE(_M0L2vtS2222, _M0L1kS898);
      _M0L2vrS2221 = _M0L11soma__paramS892->$1;
      #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2220 = _M0MPC15array5Array2atGfE(_M0L2vrS2221, _M0L1kS898);
      _M0L6spreadS899 = _M0L6_2atmpS2219 - _M0L6_2atmpS2220;
      _M0L2vrS2208 = _M0L11soma__paramS892->$1;
      #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2205 = _M0MPC15array5Array2atGfE(_M0L2vrS2208, _M0L1kS898);
      #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2207 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS900);
      _M0L6_2atmpS2206 = _M0L6_2atmpS2207 * _M0L6spreadS899;
      _M0L6_2atmpS2204 = _M0L6_2atmpS2205 + _M0L6_2atmpS2206;
      #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__sS894, _M0L1kS898, _M0L6_2atmpS2204);
      _M0L2vrS2213 = _M0L11soma__paramS892->$1;
      #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2210 = _M0MPC15array5Array2atGfE(_M0L2vrS2213, _M0L1kS898);
      #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2212 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS900);
      _M0L6_2atmpS2211 = _M0L6_2atmpS2212 * _M0L6spreadS899;
      _M0L6_2atmpS2209 = _M0L6_2atmpS2210 + _M0L6_2atmpS2211;
      #line 98 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d1S895, _M0L1kS898, _M0L6_2atmpS2209);
      _M0L2vrS2218 = _M0L11soma__paramS892->$1;
      #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2215 = _M0MPC15array5Array2atGfE(_M0L2vrS2218, _M0L1kS898);
      #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2217 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS900);
      _M0L6_2atmpS2216 = _M0L6_2atmpS2217 * _M0L6spreadS899;
      _M0L6_2atmpS2214 = _M0L6_2atmpS2215 + _M0L6_2atmpS2216;
      #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L5v__d2S896, _M0L1kS898, _M0L6_2atmpS2214);
      _M0L6_2atmpS2223 = _M0L1kS898 + 1;
      _M0L1kS898 = _M0L6_2atmpS2223;
      continue;
    }
    break;
  }
  #line 101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L4w__sS902 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 102 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L4fireS903 = _M0MPC15array5Array4makeGbE(_M0L1nS888, 0);
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L9thresholdS904 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  _M0L7_2abindS905 = 0;
  _M0L1kS906 = _M0L7_2abindS905;
  while (1) {
    if (_M0L1kS906 < _M0L1nS888) {
      struct _M0TPB5ArrayGfE* _M0L2vtS2225 = _M0L11soma__paramS892->$0;
      float _M0L6_2atmpS2224;
      int32_t _M0L6_2atmpS2226;
      #line 105 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0L6_2atmpS2224 = _M0MPC15array5Array2atGfE(_M0L2vtS2225, _M0L1kS906);
      #line 105 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS904, _M0L1kS906, _M0L6_2atmpS2224);
      _M0L6_2atmpS2226 = _M0L1kS906 + 1;
      _M0L1kS906 = _M0L6_2atmpS2226;
      continue;
    }
    break;
  }
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L4tabsS908 = _M0MPC15array5Array4makeGiE(_M0L1nS888, 1);
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L4i__sS909 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L5i__d1S910 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L5i__d2S911 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L5ge__sS912 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 112 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L5gi__sS913 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L6ge__d1S914 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L6gi__d1S915 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L6ge__d2S916 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L6gi__d2S917 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L6glu__sS918 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L7gaba__sS919 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L7glu__d1S920 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L8gaba__d1S921 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L7glu__d2S922 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L8gaba__d2S923 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  _M0L6total4S924 = _M0L1nS888 * 4;
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L2dvS925 = _M0MPC15array5Array4makeGfE(_M0L6total4S924, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L8dv__tempS926 = _M0MPC15array5Array4makeGfE(_M0L6total4S924, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L12syn__curr__sS927 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L13syn__curr__d1S928 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L13syn__curr__d2S929 = _M0MPC15array5Array4makeGfE(_M0L1nS888, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L2d1S930 = _M0MP26RiantR8snn__mbt8Dendrite3new(_M0L1nS888);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L2d2S931 = _M0MP26RiantR8snn__mbt8Dendrite3new(_M0L1nS888);
  #line 133 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_tripod_het.mbt"
  _M0L6_2atmpS2227 = _M0MP26RiantR8snn__mbt13AdExPostSpike3new();
  moonbit_incref_cycle_free(_M0L11soma__paramS892);
  _block_3252
  = (struct _M0TP26RiantR8snn__mbt9TripodHet*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9TripodHet));
  Moonbit_object_header(_block_3252)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 54, 0);
  _block_3252->$0 = _M0L11soma__paramS892;
  _block_3252->$1 = _M0L6_2atmpS2227;
  _block_3252->$2 = _M0L1cS887;
  _block_3252->$3 = _M0L2glS889;
  _block_3252->$4 = _M0L2d1S930;
  _block_3252->$5 = _M0L2d2S931;
  _block_3252->$6 = _M0L4i__sS909;
  _block_3252->$7 = _M0L5i__d1S910;
  _block_3252->$8 = _M0L5i__d2S911;
  _block_3252->$9 = _M0L5ge__sS912;
  _block_3252->$10 = _M0L5gi__sS913;
  _block_3252->$11 = _M0L6ge__d1S914;
  _block_3252->$12 = _M0L6gi__d1S915;
  _block_3252->$13 = _M0L6ge__d2S916;
  _block_3252->$14 = _M0L6gi__d2S917;
  _block_3252->$15 = _M0L6glu__sS918;
  _block_3252->$16 = _M0L7gaba__sS919;
  _block_3252->$17 = _M0L7glu__d1S920;
  _block_3252->$18 = _M0L8gaba__d1S921;
  _block_3252->$19 = _M0L7glu__d2S922;
  _block_3252->$20 = _M0L8gaba__d2S923;
  _block_3252->$21 = 0x0p+0f;
  _block_3252->$22 = -0x1.2cp+6f;
  _block_3252->$23 = 0x1.8p+2f;
  _block_3252->$24 = 0x1p+1f;
  _block_3252->$25 = 0x1p+0f;
  _block_3252->$26 = 0x1p+0f;
  _block_3252->$27 = _M0L1nS888;
  _block_3252->$28 = _M0L4v__sS894;
  _block_3252->$29 = _M0L4w__sS902;
  _block_3252->$30 = _M0L5v__d1S895;
  _block_3252->$31 = _M0L5v__d2S896;
  _block_3252->$32 = _M0L4fireS903;
  _block_3252->$33 = _M0L9thresholdS904;
  _block_3252->$34 = _M0L4tabsS908;
  _block_3252->$35 = _M0L2dvS925;
  _block_3252->$36 = _M0L8dv__tempS926;
  _block_3252->$37 = _M0L12syn__curr__sS927;
  _block_3252->$38 = _M0L13syn__curr__d1S928;
  _block_3252->$39 = _M0L13syn__curr__d2S929;
  return _block_3252;
}

struct _M0TP26RiantR8snn__mbt8Dendrite* _M0MP26RiantR8snn__mbt8Dendrite3new(
  int32_t _M0L1nS880
) {
  struct _M0TPB5ArrayGfE* _M0L2elS879;
  struct _M0TPB5ArrayGfE* _M0L1cS881;
  struct _M0TPB5ArrayGfE* _M0L3gaxS882;
  struct _M0TPB5ArrayGfE* _M0L2gmS883;
  struct _M0TPB5ArrayGfE* _M0L1lS884;
  struct _M0TPB5ArrayGfE* _M0L1dS885;
  struct _M0TPB5ArrayGfE* _M0L11gax__parentS886;
  struct _M0TP26RiantR8snn__mbt8Dendrite* _block_3253;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L2elS879
  = _M0MPC15array5Array4makeGfE(_M0L1nS880, -0x1.1a66666666666p+6f);
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1cS881 = _M0MPC15array5Array4makeGfE(_M0L1nS880, 0x1.4p+3f);
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L3gaxS882 = _M0MPC15array5Array4makeGfE(_M0L1nS880, 0x1.4p+3f);
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L2gmS883 = _M0MPC15array5Array4makeGfE(_M0L1nS880, 0x1p+0f);
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1lS884 = _M0MPC15array5Array4makeGfE(_M0L1nS880, 0x1.2cp+7f);
  #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L1dS885 = _M0MPC15array5Array4makeGfE(_M0L1nS880, 0x1p+2f);
  #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_dendrite.mbt"
  _M0L11gax__parentS886 = _M0MPC15array5Array4makeGfE(_M0L1nS880, 0x0p+0f);
  _block_3253
  = (struct _M0TP26RiantR8snn__mbt8Dendrite*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt8Dendrite));
  Moonbit_object_header(_block_3253)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 89, 0);
  _block_3253->$0 = _M0L1nS880;
  _block_3253->$1 = _M0L2elS879;
  _block_3253->$2 = _M0L1cS881;
  _block_3253->$3 = _M0L3gaxS882;
  _block_3253->$4 = _M0L2gmS883;
  _block_3253->$5 = _M0L1lS884;
  _block_3253->$6 = _M0L1dS885;
  _block_3253->$7 = _M0L11gax__parentS886;
  return _block_3253;
}

struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0MP26RiantR8snn__mbt13AdExPostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _block_3254;
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _block_3254
  = (struct _M0TP26RiantR8snn__mbt13AdExPostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt13AdExPostSpike));
  Moonbit_object_header(_block_3254)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3254->$0 = 0x0p+0f;
  _block_3254->$1 = 0x1.4p+3f;
  _block_3254->$2 = 0x1.4p+3f;
  _block_3254->$3 = 0x1p+0f;
  _block_3254->$4 = 0x1p+0f;
  return _block_3254;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS877
) {
  struct _M0TUmmmmE* _M0L1sS876;
  uint64_t _M0L6_2atmpS2193;
  struct _M0TUmmmmE* _M0L1tS878;
  uint64_t _M0L6_2atmpS2189;
  uint64_t _M0L6_2atmpS2190;
  uint64_t _M0L6_2atmpS2191;
  uint64_t _M0L6_2atmpS2192;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_3255;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS876 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS877);
  _M0L6_2atmpS2193 = _M0L1sS876->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS878 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2193);
  _M0L6_2atmpS2189 = _M0L1sS876->$0;
  _M0L6_2atmpS2190 = _M0L1sS876->$1;
  _M0L6_2atmpS2191 = _M0L1sS876->$2;
  moonbit_decref_cycle_free(_M0L1sS876);
  _M0L6_2atmpS2192 = _M0L1tS878->$0;
  moonbit_decref_cycle_free(_M0L1tS878);
  _block_3255
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_3255)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3255->$0 = _M0L6_2atmpS2189;
  _block_3255->$1 = _M0L6_2atmpS2190;
  _block_3255->$2 = _M0L6_2atmpS2191;
  _block_3255->$3 = _M0L6_2atmpS2192;
  return _block_3255;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS868) {
  uint64_t _M0L2s1S867;
  uint64_t _M0L2z1S869;
  uint64_t _M0L2s2S870;
  uint64_t _M0L2z2S871;
  uint64_t _M0L2s3S872;
  uint64_t _M0L2z3S873;
  uint64_t _M0L2s4S874;
  uint64_t _M0L2z4S875;
  struct _M0TUmmmmE* _block_3256;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S867 = _M0L4seedS868 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S869 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S867);
  _M0L2s2S870 = _M0L2s1S867 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S871 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S870);
  _M0L2s3S872 = _M0L2s2S870 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S873 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S872);
  _M0L2s4S874 = _M0L2s3S872 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S875 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S874);
  _block_3256 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_3256)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3256->$0 = _M0L2z1S869;
  _block_3256->$1 = _M0L2z2S871;
  _block_3256->$2 = _M0L2z3S873;
  _block_3256->$3 = _M0L2z4S875;
  return _block_3256;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS865) {
  uint64_t _M0L6_2atmpS2188;
  uint64_t _M0L6_2atmpS2187;
  uint64_t _M0L1zS864;
  uint64_t _M0L6_2atmpS2186;
  uint64_t _M0L6_2atmpS2185;
  uint64_t _M0L1zS866;
  uint64_t _M0L6_2atmpS2184;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2188 = _M0L1zS865 >> 30;
  _M0L6_2atmpS2187 = _M0L1zS865 ^ _M0L6_2atmpS2188;
  _M0L1zS864 = _M0L6_2atmpS2187 * 13787848793156543929ull;
  _M0L6_2atmpS2186 = _M0L1zS864 >> 27;
  _M0L6_2atmpS2185 = _M0L1zS864 ^ _M0L6_2atmpS2186;
  _M0L1zS866 = _M0L6_2atmpS2185 * 10723151780598845931ull;
  _M0L6_2atmpS2184 = _M0L1zS866 >> 31;
  return _M0L1zS866 ^ _M0L6_2atmpS2184;
}

struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0MP26RiantR8snn__mbt12PoissonLayer7with__n(
  float _M0L4rateS863,
  int32_t _M0L10n__sourcesS861
) {
  uint8_t* _M0L6_2atmpS2183;
  struct _M0TPB5ArrayGbE* _M0L6activeS858;
  int32_t _M0L7_2abindS859;
  int32_t _M0L2__S860;
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _block_3258;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  _M0L6_2atmpS2183 = (uint8_t*)moonbit_empty_int8_array;
  _M0L6activeS858
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_M0L6activeS858)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 98, 0);
  _M0L6activeS858->$0 = _M0L6_2atmpS2183;
  _M0L6activeS858->$1 = 0;
  _M0L7_2abindS859 = 0;
  _M0L2__S860 = _M0L7_2abindS859;
  while (1) {
    if (_M0L2__S860 < _M0L10n__sourcesS861) {
      int32_t _M0L6_2atmpS2182;
      #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0MPC15array5Array4pushGbE(_M0L6activeS858, 1);
      _M0L6_2atmpS2182 = _M0L2__S860 + 1;
      _M0L2__S860 = _M0L6_2atmpS2182;
      continue;
    }
    break;
  }
  _block_3258
  = (struct _M0TP26RiantR8snn__mbt12PoissonLayer*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt12PoissonLayer));
  Moonbit_object_header(_block_3258)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 101, 0);
  _block_3258->$0 = _M0L4rateS863;
  _block_3258->$1 = _M0L10n__sourcesS861;
  _block_3258->$2 = _M0L6activeS858;
  _block_3258->$3 = 0x1p+0f;
  _block_3258->$4 = 0x0p+0f;
  _block_3258->$5 = 0x1p+0f;
  _block_3258->$6 = (moonbit_string_t)moonbit_string_literal_9.data;
  _block_3258->$7 = (moonbit_string_t)moonbit_string_literal_9.data;
  return _block_3258;
}

int32_t _M0FP26RiantR8snn__mbt22stimulate__layer__ball(
  struct _M0TP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick* _M0L1sS844,
  float _M0L4timeS842,
  float _M0L2dtS847
) {
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS2181;
  int32_t _M0L6n__preS843;
  struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L4postS2180;
  int32_t _M0L7n__postS845;
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS2179;
  float _M0L4rateS2178;
  float _M0L6lambdaS846;
  struct _M0TPB5ArrayGfE* _M0L3bufS848;
  int32_t _M0L7_2abindS849;
  int32_t _M0L1iS850;
  #line 78 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_ballandstick.mbt"
  _M0L5paramS2181 = _M0L1sS844->$0;
  _M0L6n__preS843 = _M0L5paramS2181->$1;
  _M0L4postS2180 = _M0L1sS844->$1;
  _M0L7n__postS845 = _M0L4postS2180->$19;
  _M0L5paramS2179 = _M0L1sS844->$0;
  _M0L4rateS2178 = _M0L5paramS2179->$0;
  _M0L6lambdaS846 = _M0L4rateS2178 * _M0L2dtS847;
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_ballandstick.mbt"
  _M0L3bufS848 = _M0FP26RiantR8snn__mbt20target__buffer__ball(_M0L1sS844);
  if (_M0L6lambdaS846 <= 0x0p+0f) {
    moonbit_decref_cycle_free(_M0L3bufS848);
    return 0;
  }
  _M0L7_2abindS849 = 0;
  _M0L1iS850 = _M0L7_2abindS849;
  while (1) {
    if (_M0L1iS850 < _M0L6n__preS843) {
      struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS2169 =
        _M0L1sS844->$0;
      struct _M0TPB5ArrayGbE* _M0L6activeS2168 = _M0L5paramS2169->$2;
      int32_t _M0L6_2atmpS2167;
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS2177;
      int32_t _M0L1kS853;
      int32_t _M0L6_2atmpS2166;
      moonbit_incref_cycle_free(_M0L6activeS2168);
      #line 92 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_ballandstick.mbt"
      _M0L6_2atmpS2167
      = _M0MPC15array5Array2atGbE(_M0L6activeS2168, _M0L1iS850);
      moonbit_decref_cycle_free(_M0L6activeS2168);
      if (!_M0L6_2atmpS2167) {
        goto join_851;
      }
      _M0L3rngS2177 = _M0L1sS844->$6;
      #line 95 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_ballandstick.mbt"
      _M0L1kS853
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS2177, _M0L6lambdaS846);
      if (_M0L1kS853 > 0) {
        int32_t _M0L7_2abindS854 = 0;
        int32_t _M0L1jS855 = _M0L7_2abindS854;
        while (1) {
          if (_M0L1jS855 < _M0L7n__postS845) {
            int32_t _M0L6_2atmpS2175 = _M0L1jS855 * _M0L6n__preS843;
            int32_t _M0L3idxS856 = _M0L6_2atmpS2175 + _M0L1iS850;
            struct _M0TPB5ArrayGbE* _M0L12connectivityS2170 = _M0L1sS844->$3;
            int32_t _M0L6_2atmpS2176;
            #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_ballandstick.mbt"
            if (
              _M0MPC15array5Array2atGbE(_M0L12connectivityS2170, _M0L3idxS856)
            ) {
              float _M0L6_2atmpS2172;
              struct _M0TPB5ArrayGfE* _M0L7weightsS2174;
              float _M0L6_2atmpS2173;
              float _M0L6_2atmpS2171;
              #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_ballandstick.mbt"
              _M0L6_2atmpS2172
              = _M0MPC15array5Array2atGfE(_M0L3bufS848, _M0L1jS855);
              _M0L7weightsS2174 = _M0L1sS844->$2;
              #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_ballandstick.mbt"
              _M0L6_2atmpS2173
              = _M0MPC15array5Array2atGfE(_M0L7weightsS2174, _M0L3idxS856);
              _M0L6_2atmpS2171 = _M0L6_2atmpS2172 + _M0L6_2atmpS2173;
              #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_ballandstick.mbt"
              _M0MPC15array5Array3setGfE(_M0L3bufS848, _M0L1jS855, _M0L6_2atmpS2171);
            }
            _M0L6_2atmpS2176 = _M0L1jS855 + 1;
            _M0L1jS855 = _M0L6_2atmpS2176;
            continue;
          }
          break;
        }
      }
      goto join_851;
      goto joinlet_3260;
      join_851:;
      _M0L6_2atmpS2166 = _M0L1iS850 + 1;
      _M0L1iS850 = _M0L6_2atmpS2166;
      continue;
      joinlet_3260:;
    } else {
      moonbit_decref_cycle_free(_M0L3bufS848);
    }
    break;
  }
  return 0;
}

struct _M0TPB5ArrayGfE* _M0FP26RiantR8snn__mbt20target__buffer__ball(
  struct _M0TP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick* _M0L1sS840
) {
  moonbit_string_t _M0L7_2abindS839;
  moonbit_string_t _M0L7_2abindS841;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_ballandstick.mbt"
  _M0L7_2abindS839 = _M0L1sS840->$4;
  _M0L7_2abindS841 = _M0L1sS840->$5;
  if (
    _M0L7_2abindS839 == (moonbit_string_t)moonbit_string_literal_12.data
    || Moonbit_array_length(_M0L7_2abindS839) == 4
       && 0
          == memcmp(_M0L7_2abindS839, (moonbit_string_t)moonbit_string_literal_12.data, 8)
  ) {
    if (
      _M0L7_2abindS841 == (moonbit_string_t)moonbit_string_literal_11.data
      || Moonbit_array_length(_M0L7_2abindS841) == 3
         && 0
            == memcmp(_M0L7_2abindS841, (moonbit_string_t)moonbit_string_literal_11.data, 6)
    ) {
      struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L4postS2161 =
        _M0L1sS840->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3163 = _M0L4postS2161->$9;
      moonbit_incref_cycle_free(_M0L8_2afieldS3163);
      return _M0L8_2afieldS3163;
    } else {
      struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L4postS2162 =
        _M0L1sS840->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3164 = _M0L4postS2162->$10;
      moonbit_incref_cycle_free(_M0L8_2afieldS3164);
      return _M0L8_2afieldS3164;
    }
  } else if (
           _M0L7_2abindS839
           == (moonbit_string_t)moonbit_string_literal_10.data
           || Moonbit_array_length(_M0L7_2abindS839) == 1
              && 0
                 == memcmp(_M0L7_2abindS839, (moonbit_string_t)moonbit_string_literal_10.data, 2)
         ) {
    if (
      _M0L7_2abindS841 == (moonbit_string_t)moonbit_string_literal_11.data
      || Moonbit_array_length(_M0L7_2abindS841) == 3
         && 0
            == memcmp(_M0L7_2abindS841, (moonbit_string_t)moonbit_string_literal_11.data, 6)
    ) {
      struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L4postS2163 =
        _M0L1sS840->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3165 = _M0L4postS2163->$11;
      moonbit_incref_cycle_free(_M0L8_2afieldS3165);
      return _M0L8_2afieldS3165;
    } else {
      struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L4postS2164 =
        _M0L1sS840->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3166 = _M0L4postS2164->$12;
      moonbit_incref_cycle_free(_M0L8_2afieldS3166);
      return _M0L8_2afieldS3166;
    }
  } else {
    struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L4postS2165 =
      _M0L1sS840->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS3167 = _M0L4postS2165->$9;
    moonbit_incref_cycle_free(_M0L8_2afieldS3167);
    return _M0L8_2afieldS3167;
  }
}

struct _M0TP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick* _M0MP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick3new(
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS820,
  struct _M0TP26RiantR8snn__mbt12BallAndStick* _M0L4postS822,
  moonbit_string_t _M0L19target__compartmentS837,
  moonbit_string_t _M0L12target__kindS838,
  float _M0L2muS833,
  float _M0L5sigmaS834,
  float _M0L7p__connS832,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS831
) {
  int32_t _M0L6n__preS819;
  int32_t _M0L7n__postS821;
  int32_t _M0L6_2atmpS2160;
  struct _M0TPB5ArrayGfE* _M0L7weightsS823;
  int32_t _M0L6_2atmpS2159;
  struct _M0TPB5ArrayGbE* _M0L12connectivityS824;
  moonbit_string_t _M0L4distS2158;
  int32_t _M0L11use__normalS825;
  int32_t _M0L7_2abindS826;
  int32_t _M0L1iS827;
  struct _M0TP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick* _block_3264;
  #line 31 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_ballandstick.mbt"
  _M0L6n__preS819 = _M0L5paramS820->$1;
  _M0L7n__postS821 = _M0L4postS822->$19;
  _M0L6_2atmpS2160 = _M0L7n__postS821 * _M0L6n__preS819;
  #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_ballandstick.mbt"
  _M0L7weightsS823 = _M0MPC15array5Array4makeGfE(_M0L6_2atmpS2160, 0x0p+0f);
  _M0L6_2atmpS2159 = _M0L7n__postS821 * _M0L6n__preS819;
  #line 44 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_ballandstick.mbt"
  _M0L12connectivityS824 = _M0MPC15array5Array4makeGbE(_M0L6_2atmpS2159, 0);
  _M0L4distS2158 = _M0L5paramS820->$6;
  #line 45 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_ballandstick.mbt"
  _M0L11use__normalS825
  = _M0L4distS2158 == (moonbit_string_t)moonbit_string_literal_13.data
    || Moonbit_array_length(_M0L4distS2158)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_13.data)
       && 0
          == memcmp(_M0L4distS2158, (moonbit_string_t)moonbit_string_literal_13.data, Moonbit_array_length(_M0L4distS2158) * 2);
  _M0L7_2abindS826 = 0;
  _M0L1iS827 = _M0L7_2abindS826;
  while (1) {
    if (_M0L1iS827 < _M0L6n__preS819) {
      int32_t _M0L7_2abindS828 = 0;
      int32_t _M0L1jS829 = _M0L7_2abindS828;
      int32_t _M0L6_2atmpS2157;
      while (1) {
        if (_M0L1jS829 < _M0L7n__postS821) {
          int32_t _M0L6_2atmpS2155 = _M0L1jS829 * _M0L6n__preS819;
          int32_t _M0L3idxS830 = _M0L6_2atmpS2155 + _M0L1iS827;
          float _M0L6_2atmpS2151;
          int32_t _M0L6_2atmpS2156;
          #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_ballandstick.mbt"
          _M0L6_2atmpS2151 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS831);
          if (_M0L6_2atmpS2151 < _M0L7p__connS832) {
            float _M0L6_2atmpS2152;
            #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_ballandstick.mbt"
            _M0MPC15array5Array3setGbE(_M0L12connectivityS824, _M0L3idxS830, 1);
            if (_M0L11use__normalS825) {
              float _M0L6_2atmpS2154;
              float _M0L6_2atmpS2153;
              #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_ballandstick.mbt"
              _M0L6_2atmpS2154
              = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS831);
              _M0L6_2atmpS2153 = _M0L6_2atmpS2154 * _M0L5sigmaS834;
              _M0L6_2atmpS2152 = _M0L2muS833 + _M0L6_2atmpS2153;
            } else {
              _M0L6_2atmpS2152 = _M0L2muS833;
            }
            #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_ballandstick.mbt"
            _M0MPC15array5Array3setGfE(_M0L7weightsS823, _M0L3idxS830, _M0L6_2atmpS2152);
          }
          _M0L6_2atmpS2156 = _M0L1jS829 + 1;
          _M0L1jS829 = _M0L6_2atmpS2156;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2157 = _M0L1iS827 + 1;
      _M0L1iS827 = _M0L6_2atmpS2157;
      continue;
    }
    break;
  }
  moonbit_incref_cycle_free(_M0L5paramS820);
  moonbit_incref_cycle_free(_M0L4postS822);
  moonbit_incref_cycle_free(_M0L19target__compartmentS837);
  moonbit_incref_cycle_free(_M0L12target__kindS838);
  moonbit_incref_cycle_free(_M0L3rngS831);
  _block_3264
  = (struct _M0TP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt32PoissonLayerStimulusBallAndStick));
  Moonbit_object_header(_block_3264)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 106, 0);
  _block_3264->$0 = _M0L5paramS820;
  _block_3264->$1 = _M0L4postS822;
  _block_3264->$2 = _M0L7weightsS823;
  _block_3264->$3 = _M0L12connectivityS824;
  _block_3264->$4 = _M0L19target__compartmentS837;
  _block_3264->$5 = _M0L12target__kindS838;
  _block_3264->$6 = _M0L3rngS831;
  return _block_3264;
}

int32_t _M0FP26RiantR8snn__mbt24stimulate__layer__tripod(
  struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod* _M0L1sS805,
  float _M0L4timeS803,
  float _M0L2dtS808
) {
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS2150;
  int32_t _M0L6n__preS804;
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS2149;
  int32_t _M0L7n__postS806;
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS2148;
  float _M0L4rateS2147;
  float _M0L6lambdaS807;
  struct _M0TPB5ArrayGfE* _M0L3bufS809;
  int32_t _M0L7_2abindS810;
  int32_t _M0L1iS811;
  #line 85 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
  _M0L5paramS2150 = _M0L1sS805->$0;
  _M0L6n__preS804 = _M0L5paramS2150->$1;
  _M0L4postS2149 = _M0L1sS805->$1;
  _M0L7n__postS806 = _M0L4postS2149->$27;
  _M0L5paramS2148 = _M0L1sS805->$0;
  _M0L4rateS2147 = _M0L5paramS2148->$0;
  _M0L6lambdaS807 = _M0L4rateS2147 * _M0L2dtS808;
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
  _M0L3bufS809 = _M0FP26RiantR8snn__mbt22target__buffer__tripod(_M0L1sS805);
  if (_M0L6lambdaS807 <= 0x0p+0f) {
    moonbit_decref_cycle_free(_M0L3bufS809);
    return 0;
  }
  _M0L7_2abindS810 = 0;
  _M0L1iS811 = _M0L7_2abindS810;
  while (1) {
    if (_M0L1iS811 < _M0L6n__preS804) {
      struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS2138 =
        _M0L1sS805->$0;
      struct _M0TPB5ArrayGbE* _M0L6activeS2137 = _M0L5paramS2138->$2;
      int32_t _M0L6_2atmpS2136;
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS2146;
      int32_t _M0L1kS814;
      int32_t _M0L6_2atmpS2135;
      moonbit_incref_cycle_free(_M0L6activeS2137);
      #line 99 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
      _M0L6_2atmpS2136
      = _M0MPC15array5Array2atGbE(_M0L6activeS2137, _M0L1iS811);
      moonbit_decref_cycle_free(_M0L6activeS2137);
      if (!_M0L6_2atmpS2136) {
        goto join_812;
      }
      _M0L3rngS2146 = _M0L1sS805->$6;
      #line 102 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
      _M0L1kS814
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS2146, _M0L6lambdaS807);
      if (_M0L1kS814 > 0) {
        int32_t _M0L7_2abindS815 = 0;
        int32_t _M0L1jS816 = _M0L7_2abindS815;
        while (1) {
          if (_M0L1jS816 < _M0L7n__postS806) {
            int32_t _M0L6_2atmpS2144 = _M0L1jS816 * _M0L6n__preS804;
            int32_t _M0L3idxS817 = _M0L6_2atmpS2144 + _M0L1iS811;
            struct _M0TPB5ArrayGbE* _M0L12connectivityS2139 = _M0L1sS805->$3;
            int32_t _M0L6_2atmpS2145;
            #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
            if (
              _M0MPC15array5Array2atGbE(_M0L12connectivityS2139, _M0L3idxS817)
            ) {
              float _M0L6_2atmpS2141;
              struct _M0TPB5ArrayGfE* _M0L7weightsS2143;
              float _M0L6_2atmpS2142;
              float _M0L6_2atmpS2140;
              #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
              _M0L6_2atmpS2141
              = _M0MPC15array5Array2atGfE(_M0L3bufS809, _M0L1jS816);
              _M0L7weightsS2143 = _M0L1sS805->$2;
              #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
              _M0L6_2atmpS2142
              = _M0MPC15array5Array2atGfE(_M0L7weightsS2143, _M0L3idxS817);
              _M0L6_2atmpS2140 = _M0L6_2atmpS2141 + _M0L6_2atmpS2142;
              #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
              _M0MPC15array5Array3setGfE(_M0L3bufS809, _M0L1jS816, _M0L6_2atmpS2140);
            }
            _M0L6_2atmpS2145 = _M0L1jS816 + 1;
            _M0L1jS816 = _M0L6_2atmpS2145;
            continue;
          }
          break;
        }
      }
      goto join_812;
      goto joinlet_3266;
      join_812:;
      _M0L6_2atmpS2135 = _M0L1iS811 + 1;
      _M0L1iS811 = _M0L6_2atmpS2135;
      continue;
      joinlet_3266:;
    } else {
      moonbit_decref_cycle_free(_M0L3bufS809);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt15sample__poisson(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS801,
  float _M0L6lambdaS795
) {
  float _M0L6_2atmpS2134;
  float _M0L6_2atmpS2133;
  double _M0L1lS796;
  struct _M0TPB8MutLocalGdE* _M0L1pS797;
  struct _M0TPB8MutLocalGiE* _M0L1kS798;
  float _M0L6_2atmpS2132;
  int32_t _M0L8ten__lamS800;
  int32_t _M0L3capS799;
  int32_t _M0L3valS2131;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  if (_M0L6lambdaS795 <= 0x0p+0f) {
    return 0;
  }
  _M0L6_2atmpS2134 = -_M0L6lambdaS795;
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS2133 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS2134);
  _M0L1lS796 = (double)_M0L6_2atmpS2133;
  _M0L1pS797
  = (struct _M0TPB8MutLocalGdE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGdE));
  Moonbit_object_header(_M0L1pS797)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1pS797->$0 = 0x1p+0;
  _M0L1kS798
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS798)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS798->$0 = 0;
  _M0L6_2atmpS2132 = _M0L6lambdaS795 * 0x1.4p+3f;
  #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L8ten__lamS800 = _M0MPC15float5Float7to__int(_M0L6_2atmpS2132);
  if (_M0L8ten__lamS800 > 100) {
    _M0L3capS799 = _M0L8ten__lamS800;
  } else {
    _M0L3capS799 = 100;
  }
  while (1) {
    int32_t _M0L3valS2123 = _M0L1kS798->$0;
    int32_t _M0L6_2atmpS2122 = _M0L3valS2123 + 1;
    double _M0L3valS2125;
    double _M0L6_2atmpS2126;
    double _M0L6_2atmpS2124;
    double _M0L3valS2127;
    int32_t _M0L3valS2129;
    _M0L1kS798->$0 = _M0L6_2atmpS2122;
    _M0L3valS2125 = _M0L1pS797->$0;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
    _M0L6_2atmpS2126 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS801);
    _M0L6_2atmpS2124 = _M0L3valS2125 * _M0L6_2atmpS2126;
    _M0L1pS797->$0 = _M0L6_2atmpS2124;
    _M0L3valS2127 = _M0L1pS797->$0;
    if (_M0L3valS2127 < _M0L1lS796) {
      int32_t _M0L3valS2128;
      moonbit_decref_cycle_free(_M0L1pS797);
      _M0L3valS2128 = _M0L1kS798->$0;
      moonbit_decref_cycle_free(_M0L1kS798);
      return _M0L3valS2128 - 1;
    }
    _M0L3valS2129 = _M0L1kS798->$0;
    if (_M0L3valS2129 > _M0L3capS799) {
      int32_t _M0L3valS2130;
      moonbit_decref_cycle_free(_M0L1pS797);
      _M0L3valS2130 = _M0L1kS798->$0;
      moonbit_decref_cycle_free(_M0L1kS798);
      return _M0L3valS2130 - 1;
    }
    continue;
    break;
  }
  _M0L3valS2131 = _M0L1kS798->$0;
  moonbit_decref_cycle_free(_M0L1kS798);
  return _M0L3valS2131 - 1;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS793
) {
  uint64_t _M0L1uS792;
  uint64_t _M0L4bitsS794;
  double _M0L6_2atmpS2121;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS792 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS793);
  _M0L4bitsS794 = _M0L1uS792 >> 11;
  _M0L6_2atmpS2121 = (double)_M0L4bitsS794;
  return _M0L6_2atmpS2121 * 0x1p-53;
}

struct _M0TPB5ArrayGfE* _M0FP26RiantR8snn__mbt22target__buffer__tripod(
  struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod* _M0L1sS790
) {
  moonbit_string_t _M0L7_2abindS789;
  moonbit_string_t _M0L7_2abindS791;
  #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
  _M0L7_2abindS789 = _M0L1sS790->$4;
  _M0L7_2abindS791 = _M0L1sS790->$5;
  if (
    _M0L7_2abindS789 == (moonbit_string_t)moonbit_string_literal_12.data
    || Moonbit_array_length(_M0L7_2abindS789) == 4
       && 0
          == memcmp(_M0L7_2abindS789, (moonbit_string_t)moonbit_string_literal_12.data, 8)
  ) {
    if (
      _M0L7_2abindS791 == (moonbit_string_t)moonbit_string_literal_11.data
      || Moonbit_array_length(_M0L7_2abindS791) == 3
         && 0
            == memcmp(_M0L7_2abindS791, (moonbit_string_t)moonbit_string_literal_11.data, 6)
    ) {
      struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS2114 =
        _M0L1sS790->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3168 = _M0L4postS2114->$15;
      moonbit_incref_cycle_free(_M0L8_2afieldS3168);
      return _M0L8_2afieldS3168;
    } else {
      struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS2115 =
        _M0L1sS790->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3169 = _M0L4postS2115->$16;
      moonbit_incref_cycle_free(_M0L8_2afieldS3169);
      return _M0L8_2afieldS3169;
    }
  } else if (
           _M0L7_2abindS789
           == (moonbit_string_t)moonbit_string_literal_15.data
           || Moonbit_array_length(_M0L7_2abindS789) == 2
              && 0
                 == memcmp(_M0L7_2abindS789, (moonbit_string_t)moonbit_string_literal_15.data, 4)
         ) {
    if (
      _M0L7_2abindS791 == (moonbit_string_t)moonbit_string_literal_11.data
      || Moonbit_array_length(_M0L7_2abindS791) == 3
         && 0
            == memcmp(_M0L7_2abindS791, (moonbit_string_t)moonbit_string_literal_11.data, 6)
    ) {
      struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS2116 =
        _M0L1sS790->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3170 = _M0L4postS2116->$17;
      moonbit_incref_cycle_free(_M0L8_2afieldS3170);
      return _M0L8_2afieldS3170;
    } else {
      struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS2117 =
        _M0L1sS790->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3171 = _M0L4postS2117->$18;
      moonbit_incref_cycle_free(_M0L8_2afieldS3171);
      return _M0L8_2afieldS3171;
    }
  } else if (
           _M0L7_2abindS789
           == (moonbit_string_t)moonbit_string_literal_14.data
           || Moonbit_array_length(_M0L7_2abindS789) == 2
              && 0
                 == memcmp(_M0L7_2abindS789, (moonbit_string_t)moonbit_string_literal_14.data, 4)
         ) {
    if (
      _M0L7_2abindS791 == (moonbit_string_t)moonbit_string_literal_11.data
      || Moonbit_array_length(_M0L7_2abindS791) == 3
         && 0
            == memcmp(_M0L7_2abindS791, (moonbit_string_t)moonbit_string_literal_11.data, 6)
    ) {
      struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS2118 =
        _M0L1sS790->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3172 = _M0L4postS2118->$19;
      moonbit_incref_cycle_free(_M0L8_2afieldS3172);
      return _M0L8_2afieldS3172;
    } else {
      struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS2119 =
        _M0L1sS790->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS3173 = _M0L4postS2119->$20;
      moonbit_incref_cycle_free(_M0L8_2afieldS3173);
      return _M0L8_2afieldS3173;
    }
  } else {
    struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS2120 = _M0L1sS790->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS3174 = _M0L4postS2120->$17;
    moonbit_incref_cycle_free(_M0L8_2afieldS3174);
    return _M0L8_2afieldS3174;
  }
}

struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod* _M0MP26RiantR8snn__mbt26PoissonLayerStimulusTripod3new(
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS770,
  struct _M0TP26RiantR8snn__mbt9TripodHet* _M0L4postS772,
  moonbit_string_t _M0L19target__compartmentS787,
  moonbit_string_t _M0L12target__kindS788,
  float _M0L2muS783,
  float _M0L5sigmaS784,
  float _M0L7p__connS782,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS781
) {
  int32_t _M0L6n__preS769;
  int32_t _M0L7n__postS771;
  int32_t _M0L6_2atmpS2113;
  struct _M0TPB5ArrayGfE* _M0L7weightsS773;
  int32_t _M0L6_2atmpS2112;
  struct _M0TPB5ArrayGbE* _M0L12connectivityS774;
  moonbit_string_t _M0L4distS2111;
  int32_t _M0L11use__normalS775;
  int32_t _M0L7_2abindS776;
  int32_t _M0L1iS777;
  struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod* _block_3271;
  #line 36 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
  _M0L6n__preS769 = _M0L5paramS770->$1;
  _M0L7n__postS771 = _M0L4postS772->$27;
  _M0L6_2atmpS2113 = _M0L7n__postS771 * _M0L6n__preS769;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
  _M0L7weightsS773 = _M0MPC15array5Array4makeGfE(_M0L6_2atmpS2113, 0x0p+0f);
  _M0L6_2atmpS2112 = _M0L7n__postS771 * _M0L6n__preS769;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
  _M0L12connectivityS774 = _M0MPC15array5Array4makeGbE(_M0L6_2atmpS2112, 0);
  _M0L4distS2111 = _M0L5paramS770->$6;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
  _M0L11use__normalS775
  = _M0L4distS2111 == (moonbit_string_t)moonbit_string_literal_13.data
    || Moonbit_array_length(_M0L4distS2111)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_13.data)
       && 0
          == memcmp(_M0L4distS2111, (moonbit_string_t)moonbit_string_literal_13.data, Moonbit_array_length(_M0L4distS2111) * 2);
  _M0L7_2abindS776 = 0;
  _M0L1iS777 = _M0L7_2abindS776;
  while (1) {
    if (_M0L1iS777 < _M0L6n__preS769) {
      int32_t _M0L7_2abindS778 = 0;
      int32_t _M0L1jS779 = _M0L7_2abindS778;
      int32_t _M0L6_2atmpS2110;
      while (1) {
        if (_M0L1jS779 < _M0L7n__postS771) {
          int32_t _M0L6_2atmpS2108 = _M0L1jS779 * _M0L6n__preS769;
          int32_t _M0L3idxS780 = _M0L6_2atmpS2108 + _M0L1iS777;
          float _M0L6_2atmpS2104;
          int32_t _M0L6_2atmpS2109;
          #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
          _M0L6_2atmpS2104 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS781);
          if (_M0L6_2atmpS2104 < _M0L7p__connS782) {
            float _M0L6_2atmpS2105;
            #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
            _M0MPC15array5Array3setGbE(_M0L12connectivityS774, _M0L3idxS780, 1);
            if (_M0L11use__normalS775) {
              float _M0L6_2atmpS2107;
              float _M0L6_2atmpS2106;
              #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
              _M0L6_2atmpS2107
              = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS781);
              _M0L6_2atmpS2106 = _M0L6_2atmpS2107 * _M0L5sigmaS784;
              _M0L6_2atmpS2105 = _M0L2muS783 + _M0L6_2atmpS2106;
            } else {
              _M0L6_2atmpS2105 = _M0L2muS783;
            }
            #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer_tripod.mbt"
            _M0MPC15array5Array3setGfE(_M0L7weightsS773, _M0L3idxS780, _M0L6_2atmpS2105);
          }
          _M0L6_2atmpS2109 = _M0L1jS779 + 1;
          _M0L1jS779 = _M0L6_2atmpS2109;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2110 = _M0L1iS777 + 1;
      _M0L1iS777 = _M0L6_2atmpS2110;
      continue;
    }
    break;
  }
  moonbit_incref_cycle_free(_M0L5paramS770);
  moonbit_incref_cycle_free(_M0L4postS772);
  moonbit_incref_cycle_free(_M0L19target__compartmentS787);
  moonbit_incref_cycle_free(_M0L12target__kindS788);
  moonbit_incref_cycle_free(_M0L3rngS781);
  _block_3271
  = (struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt26PoissonLayerStimulusTripod));
  Moonbit_object_header(_block_3271)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 115, 0);
  _block_3271->$0 = _M0L5paramS770;
  _block_3271->$1 = _M0L4postS772;
  _block_3271->$2 = _M0L7weightsS773;
  _block_3271->$3 = _M0L12connectivityS774;
  _block_3271->$4 = _M0L19target__compartmentS787;
  _block_3271->$5 = _M0L12target__kindS788;
  _block_3271->$6 = _M0L3rngS781;
  return _block_3271;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS767
) {
  uint32_t _M0L1uS766;
  uint32_t _M0L4bitsS768;
  double _M0L6_2atmpS2103;
  double _M0L6_2atmpS2102;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS766 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS767);
  _M0L4bitsS768 = _M0L1uS766 >> 8;
  _M0L6_2atmpS2103 = (double)_M0L4bitsS768;
  _M0L6_2atmpS2102 = _M0L6_2atmpS2103 * 0x1p-24;
  return (float)_M0L6_2atmpS2102;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS765
) {
  uint64_t _M0L1uS764;
  uint64_t _M0L6_2atmpS2101;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS764 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS765);
  _M0L6_2atmpS2101 = _M0L1uS764 >> 32;
  return (uint32_t)_M0L6_2atmpS2101;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS757
) {
  uint64_t _M0L2s0S756;
  uint64_t _M0L2s1S758;
  uint64_t _M0L2s2S759;
  uint64_t _M0L2s3S760;
  uint64_t _M0L3tmpS761;
  uint64_t _M0L6_2atmpS2100;
  uint64_t _M0L3resS762;
  uint64_t _M0L1tS763;
  uint64_t _M0L6_2atmpS2090;
  uint64_t _M0L6_2atmpS2091;
  uint64_t _M0L2s2S2093;
  uint64_t _M0L6_2atmpS2092;
  uint64_t _M0L2s3S2095;
  uint64_t _M0L6_2atmpS2094;
  uint64_t _M0L2s2S2097;
  uint64_t _M0L6_2atmpS2096;
  uint64_t _M0L2s3S2099;
  uint64_t _M0L6_2atmpS2098;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S756 = _M0L1rS757->$0;
  _M0L2s1S758 = _M0L1rS757->$1;
  _M0L2s2S759 = _M0L1rS757->$2;
  _M0L2s3S760 = _M0L1rS757->$3;
  _M0L3tmpS761 = _M0L2s0S756 + _M0L2s3S760;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2100 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS761, 23);
  _M0L3resS762 = _M0L6_2atmpS2100 + _M0L2s0S756;
  _M0L1tS763 = _M0L2s1S758 << 17;
  _M0L6_2atmpS2090 = _M0L2s2S759 ^ _M0L2s0S756;
  _M0L1rS757->$2 = _M0L6_2atmpS2090;
  _M0L6_2atmpS2091 = _M0L2s3S760 ^ _M0L2s1S758;
  _M0L1rS757->$3 = _M0L6_2atmpS2091;
  _M0L2s2S2093 = _M0L1rS757->$2;
  _M0L6_2atmpS2092 = _M0L2s1S758 ^ _M0L2s2S2093;
  _M0L1rS757->$1 = _M0L6_2atmpS2092;
  _M0L2s3S2095 = _M0L1rS757->$3;
  _M0L6_2atmpS2094 = _M0L2s0S756 ^ _M0L2s3S2095;
  _M0L1rS757->$0 = _M0L6_2atmpS2094;
  _M0L2s2S2097 = _M0L1rS757->$2;
  _M0L6_2atmpS2096 = _M0L2s2S2097 ^ _M0L1tS763;
  _M0L1rS757->$2 = _M0L6_2atmpS2096;
  _M0L2s3S2099 = _M0L1rS757->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2098 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S2099, 45);
  _M0L1rS757->$3 = _M0L6_2atmpS2098;
  return _M0L3resS762;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS754, int32_t _M0L1kS755) {
  uint64_t _M0L6_2atmpS2087;
  int32_t _M0L6_2atmpS2089;
  uint64_t _M0L6_2atmpS2088;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2087 = _M0L1xS754 << (_M0L1kS755 & 63);
  _M0L6_2atmpS2089 = 64 - _M0L1kS755;
  _M0L6_2atmpS2088 = _M0L1xS754 >> (_M0L6_2atmpS2089 & 63);
  return _M0L6_2atmpS2087 | _M0L6_2atmpS2088;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS753) {
  double _M0L6_2atmpS2086;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2086 = (double)_M0L4selfS753;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2086);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS752) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS752 != _M0L4selfS752) {
    return 0;
  } else if (_M0L4selfS752 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS752 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS752;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS738,
  float _M0L4elemS740
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS737;
  int32_t _M0L1iS739;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS737 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS738);
  _M0L1iS739 = 0;
  while (1) {
    if (_M0L1iS739 < _M0L3lenS738) {
      float* _M0L3bufS2080 = _M0L3arrS737->$0;
      int32_t _M0L6_2atmpS2081;
      _M0L3bufS2080[_M0L1iS739] = _M0L4elemS740;
      _M0L6_2atmpS2081 = _M0L1iS739 + 1;
      _M0L1iS739 = _M0L6_2atmpS2081;
      continue;
    }
    break;
  }
  return _M0L3arrS737;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS743,
  int32_t _M0L4elemS745
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS742;
  int32_t _M0L1iS744;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS742 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS743);
  _M0L1iS744 = 0;
  while (1) {
    if (_M0L1iS744 < _M0L3lenS743) {
      uint8_t* _M0L3bufS2082 = _M0L3arrS742->$0;
      int32_t _M0L6_2atmpS2083;
      _M0L3bufS2082[_M0L1iS744] = _M0L4elemS745;
      _M0L6_2atmpS2083 = _M0L1iS744 + 1;
      _M0L1iS744 = _M0L6_2atmpS2083;
      continue;
    }
    break;
  }
  return _M0L3arrS742;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS748,
  int32_t _M0L4elemS750
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS747;
  int32_t _M0L1iS749;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS747 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS748);
  _M0L1iS749 = 0;
  while (1) {
    if (_M0L1iS749 < _M0L3lenS748) {
      int32_t* _M0L3bufS2084 = _M0L3arrS747->$0;
      int32_t _M0L6_2atmpS2085;
      _M0L3bufS2084[_M0L1iS749] = _M0L4elemS750;
      _M0L6_2atmpS2085 = _M0L1iS749 + 1;
      _M0L1iS749 = _M0L6_2atmpS2085;
      continue;
    }
    break;
  }
  return _M0L3arrS747;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS726,
  int32_t _M0L5indexS727,
  float _M0L5valueS728
) {
  int32_t _M0L3lenS725;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS725 = _M0L4selfS726->$1;
  if (_M0L5indexS727 >= 0 && _M0L5indexS727 < _M0L3lenS725) {
    float* _M0L6_2atmpS2077;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2077 = _M0MPC15array5Array6bufferGfE(_M0L4selfS726);
    _M0L6_2atmpS2077[_M0L5indexS727] = _M0L5valueS728;
    moonbit_decref_cycle_free(_M0L6_2atmpS2077);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS730,
  int32_t _M0L5indexS731,
  int32_t _M0L5valueS732
) {
  int32_t _M0L3lenS729;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS729 = _M0L4selfS730->$1;
  if (_M0L5indexS731 >= 0 && _M0L5indexS731 < _M0L3lenS729) {
    uint8_t* _M0L6_2atmpS2078;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2078 = _M0MPC15array5Array6bufferGbE(_M0L4selfS730);
    _M0L6_2atmpS2078[_M0L5indexS731] = _M0L5valueS732;
    moonbit_decref_cycle_free(_M0L6_2atmpS2078);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS734,
  int32_t _M0L5indexS735,
  int32_t _M0L5valueS736
) {
  int32_t _M0L3lenS733;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS733 = _M0L4selfS734->$1;
  if (_M0L5indexS735 >= 0 && _M0L5indexS735 < _M0L3lenS733) {
    int32_t* _M0L6_2atmpS2079;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2079 = _M0MPC15array5Array6bufferGiE(_M0L4selfS734);
    _M0L6_2atmpS2079[_M0L5indexS735] = _M0L5valueS736;
    moonbit_decref_cycle_free(_M0L6_2atmpS2079);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS714,
  int32_t _M0L5indexS715
) {
  int32_t _M0L3lenS713;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS713 = _M0L4selfS714->$1;
  if (_M0L5indexS715 >= 0 && _M0L5indexS715 < _M0L3lenS713) {
    uint8_t* _M0L6_2atmpS2073;
    int32_t _result_3275;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2073 = _M0MPC15array5Array6bufferGbE(_M0L4selfS714);
    _result_3275 = (int32_t)_M0L6_2atmpS2073[_M0L5indexS715];
    moonbit_decref_cycle_free(_M0L6_2atmpS2073);
    return _result_3275;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS717,
  int32_t _M0L5indexS718
) {
  int32_t _M0L3lenS716;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS716 = _M0L4selfS717->$1;
  if (_M0L5indexS718 >= 0 && _M0L5indexS718 < _M0L3lenS716) {
    float* _M0L6_2atmpS2074;
    float _result_3276;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2074 = _M0MPC15array5Array6bufferGfE(_M0L4selfS717);
    _result_3276 = (float)_M0L6_2atmpS2074[_M0L5indexS718];
    moonbit_decref_cycle_free(_M0L6_2atmpS2074);
    return _result_3276;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS720,
  int32_t _M0L5indexS721
) {
  int32_t _M0L3lenS719;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS719 = _M0L4selfS720->$1;
  if (_M0L5indexS721 >= 0 && _M0L5indexS721 < _M0L3lenS719) {
    int32_t* _M0L6_2atmpS2075;
    int32_t _result_3277;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2075 = _M0MPC15array5Array6bufferGiE(_M0L4selfS720);
    _result_3277 = (int32_t)_M0L6_2atmpS2075[_M0L5indexS721];
    moonbit_decref_cycle_free(_M0L6_2atmpS2075);
    return _result_3277;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS723,
  int32_t _M0L5indexS724
) {
  int32_t _M0L3lenS722;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS722 = _M0L4selfS723->$1;
  if (_M0L5indexS724 >= 0 && _M0L5indexS724 < _M0L3lenS722) {
    moonbit_string_t* _M0L6_2atmpS2076;
    moonbit_string_t _M0L6_2atmpS3175;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2076 = _M0MPC15array5Array6bufferGsE(_M0L4selfS723);
    _M0L6_2atmpS3175 = (moonbit_string_t)_M0L6_2atmpS2076[_M0L5indexS724];
    moonbit_incref_cycle_free(_M0L6_2atmpS3175);
    moonbit_decref_cycle_free(_M0L6_2atmpS2076);
    return _M0L6_2atmpS3175;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS712) {
  moonbit_string_t _M0L6_2atmpS2072;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2072 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS712);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2072);
  moonbit_decref_cycle_free(_M0L6_2atmpS2072);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS711) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS711);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS696) {
  uint64_t _M0L4bitsS699;
  uint64_t _M0L6_2atmpS2071;
  uint64_t _M0L6_2atmpS2070;
  int32_t _M0L8ieeeSignS700;
  uint64_t _M0L12ieeeMantissaS701;
  uint64_t _M0L6_2atmpS2069;
  uint64_t _M0L6_2atmpS2068;
  int32_t _M0L12ieeeExponentS702;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS703;
  struct _M0TPB17FloatingDecimal64* _M0L1vS704;
  moonbit_string_t _result_3279;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS696 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_16.data;
  }
  if (_M0L3valS696 >= -0x1p+53 && _M0L3valS696 <= 0x1p+53) {
    if (_M0L3valS696 >= -0x1p+31 && _M0L3valS696 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS697;
      double _M0L6_2atmpS2057;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS697 = _M0MPC16double6Double7to__int(_M0L3valS696);
      _M0L6_2atmpS2057 = (double)_M0L1iS697;
      if (_M0L6_2atmpS2057 == _M0L3valS696) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS697, 10);
      }
    } else {
      int64_t _M0L1iS698;
      double _M0L6_2atmpS2058;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS698 = _M0MPC16double6Double9to__int64(_M0L3valS696);
      _M0L6_2atmpS2058 = (double)_M0L1iS698;
      if (_M0L6_2atmpS2058 == _M0L3valS696) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS698, 10);
      }
    }
  }
  _M0L4bitsS699 = *(int64_t*)&_M0L3valS696;
  _M0L6_2atmpS2071 = _M0L4bitsS699 >> 63;
  _M0L6_2atmpS2070 = _M0L6_2atmpS2071 & 1ull;
  _M0L8ieeeSignS700 = _M0L6_2atmpS2070 != 0ull;
  _M0L12ieeeMantissaS701 = _M0L4bitsS699 & 4503599627370495ull;
  _M0L6_2atmpS2069 = _M0L4bitsS699 >> 52;
  _M0L6_2atmpS2068 = _M0L6_2atmpS2069 & 2047ull;
  _M0L12ieeeExponentS702 = (int32_t)_M0L6_2atmpS2068;
  if (
    _M0L12ieeeExponentS702 == 2047
    || _M0L12ieeeExponentS702 == 0 && _M0L12ieeeMantissaS701 == 0ull
  ) {
    int32_t _M0L6_2atmpS2059 = _M0L12ieeeExponentS702 != 0;
    int32_t _M0L6_2atmpS2060 = _M0L12ieeeMantissaS701 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS700, _M0L6_2atmpS2059, _M0L6_2atmpS2060);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS703
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS701, _M0L12ieeeExponentS702);
  if (_M0L7_2abindS703 == 0) {
    uint32_t _M0L6_2atmpS2061;
    if (_M0L7_2abindS703) {
      moonbit_decref_cycle_free(_M0L7_2abindS703);
    }
    _M0L6_2atmpS2061 = *(uint32_t*)&_M0L12ieeeExponentS702;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS704 = _M0FPB3d2d(_M0L12ieeeMantissaS701, _M0L6_2atmpS2061);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS705 = _M0L7_2abindS703;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS706 = _M0L7_2aSomeS705;
    struct _M0TPB17FloatingDecimal64* _M0L1xS707 = _M0L4_2afS706;
    while (1) {
      uint64_t _M0L8mantissaS2067 = _M0L1xS707->$0;
      uint64_t _M0L1qS708 = _M0L8mantissaS2067 / 10ull;
      uint64_t _M0L8mantissaS2065 = _M0L1xS707->$0;
      uint64_t _M0L6_2atmpS2066 = 10ull * _M0L1qS708;
      uint64_t _M0L1rS709 = _M0L8mantissaS2065 - _M0L6_2atmpS2066;
      int32_t _M0L8exponentS2064;
      int32_t _M0L6_2atmpS2063;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2062;
      if (_M0L1rS709 != 0ull) {
        _M0L1vS704 = _M0L1xS707;
        break;
      }
      _M0L8exponentS2064 = _M0L1xS707->$1;
      moonbit_decref_cycle_free(_M0L1xS707);
      _M0L6_2atmpS2063 = _M0L8exponentS2064 + 1;
      _M0L6_2atmpS2062
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS2062)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS2062->$0 = _M0L1qS708;
      _M0L6_2atmpS2062->$1 = _M0L6_2atmpS2063;
      _M0L1xS707 = _M0L6_2atmpS2062;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_3279 = _M0FPB9to__chars(_M0L1vS704, _M0L8ieeeSignS700);
  moonbit_decref_cycle_free(_M0L1vS704);
  return _result_3279;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS691,
  int32_t _M0L12ieeeExponentS693
) {
  uint64_t _M0L2m2S690;
  int32_t _M0L6_2atmpS2056;
  int32_t _M0L2e2S692;
  int32_t _M0L6_2atmpS2055;
  uint64_t _M0L6_2atmpS2054;
  uint64_t _M0L4maskS694;
  uint64_t _M0L8fractionS695;
  int32_t _M0L6_2atmpS2053;
  uint64_t _M0L6_2atmpS2052;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2051;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S690 = 4503599627370496ull | _M0L12ieeeMantissaS691;
  _M0L6_2atmpS2056 = _M0L12ieeeExponentS693 - 1023;
  _M0L2e2S692 = _M0L6_2atmpS2056 - 52;
  if (_M0L2e2S692 > 0) {
    return 0;
  }
  if (_M0L2e2S692 < -52) {
    return 0;
  }
  _M0L6_2atmpS2055 = -_M0L2e2S692;
  _M0L6_2atmpS2054 = 1ull << (_M0L6_2atmpS2055 & 63);
  _M0L4maskS694 = _M0L6_2atmpS2054 - 1ull;
  _M0L8fractionS695 = _M0L2m2S690 & _M0L4maskS694;
  if (_M0L8fractionS695 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2053 = -_M0L2e2S692;
  _M0L6_2atmpS2052 = _M0L2m2S690 >> (_M0L6_2atmpS2053 & 63);
  _M0L6_2atmpS2051
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS2051)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS2051->$0 = _M0L6_2atmpS2052;
  _M0L6_2atmpS2051->$1 = 0;
  return _M0L6_2atmpS2051;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS658,
  int32_t _M0L4signS656
) {
  moonbit_bytes_t _M0L6resultS654;
  int32_t _M0Lm5indexS655;
  uint64_t _M0L6outputS657;
  int32_t _M0L7olengthS659;
  int32_t _M0L8exponentS2050;
  int32_t _M0L6_2atmpS2049;
  int32_t _M0Lm3expS660;
  int32_t _M0L6_2atmpS2048;
  int32_t _M0L6_2atmpS2046;
  int32_t _M0L18scientificNotationS661;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS654 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS655 = 0;
  if (_M0L4signS656) {
    int32_t _M0L6_2atmpS1920 = _M0Lm5indexS655;
    int32_t _M0L6_2atmpS1921;
    if (
      _M0L6_2atmpS1920 < 0
      || _M0L6_2atmpS1920 >= Moonbit_array_length(_M0L6resultS654)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS654[_M0L6_2atmpS1920] = 45;
    _M0L6_2atmpS1921 = _M0Lm5indexS655;
    _M0Lm5indexS655 = _M0L6_2atmpS1921 + 1;
  }
  _M0L6outputS657 = _M0L1vS658->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS659 = _M0FPB17decimal__length17(_M0L6outputS657);
  _M0L8exponentS2050 = _M0L1vS658->$1;
  _M0L6_2atmpS2049 = _M0L8exponentS2050 + _M0L7olengthS659;
  _M0Lm3expS660 = _M0L6_2atmpS2049 - 1;
  _M0L6_2atmpS2048 = _M0Lm3expS660;
  if (_M0L6_2atmpS2048 >= -6) {
    int32_t _M0L6_2atmpS2047 = _M0Lm3expS660;
    _M0L6_2atmpS2046 = _M0L6_2atmpS2047 < 21;
  } else {
    _M0L6_2atmpS2046 = 0;
  }
  _M0L18scientificNotationS661 = !_M0L6_2atmpS2046;
  if (_M0L18scientificNotationS661) {
    int32_t _M0L7_2abindS662 = _M0L7olengthS659 - 1;
    uint64_t _M0L6outputS663;
    int32_t _M0L1iS664 = 0;
    uint64_t _M0L6outputS665 = _M0L6outputS657;
    int32_t _M0L6_2atmpS1922;
    int32_t _M0L6_2atmpS1926;
    int32_t _M0L6_2atmpS1925;
    int32_t _M0L6_2atmpS1924;
    int32_t _M0L6_2atmpS1923;
    int32_t _M0L6_2atmpS1930;
    int32_t _M0L6_2atmpS1931;
    int32_t _M0L6_2atmpS1932;
    int32_t _M0L6_2atmpS1933;
    int32_t _M0L6_2atmpS1934;
    int32_t _M0L6_2atmpS1940;
    int32_t _M0L6_2atmpS1973;
    moonbit_string_t _result_3281;
    while (1) {
      if (_M0L1iS664 < _M0L7_2abindS662) {
        uint64_t _M0L1cS666 = _M0L6outputS665 % 10ull;
        int32_t _M0L6_2atmpS1979 = _M0Lm5indexS655;
        int32_t _M0L6_2atmpS1978 = _M0L6_2atmpS1979 + _M0L7olengthS659;
        int32_t _M0L6_2atmpS1974 = _M0L6_2atmpS1978 - _M0L1iS664;
        int32_t _M0L6_2atmpS1977 = (int32_t)_M0L1cS666;
        int32_t _M0L6_2atmpS1976 = 48 + _M0L6_2atmpS1977;
        int32_t _M0L6_2atmpS1975 = _M0L6_2atmpS1976 & 0xff;
        int32_t _M0L6_2atmpS1980;
        uint64_t _M0L6_2atmpS1981;
        if (
          _M0L6_2atmpS1974 < 0
          || _M0L6_2atmpS1974 >= Moonbit_array_length(_M0L6resultS654)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS654[_M0L6_2atmpS1974] = _M0L6_2atmpS1975;
        _M0L6_2atmpS1980 = _M0L1iS664 + 1;
        _M0L6_2atmpS1981 = _M0L6outputS665 / 10ull;
        _M0L1iS664 = _M0L6_2atmpS1980;
        _M0L6outputS665 = _M0L6_2atmpS1981;
        continue;
      } else {
        _M0L6outputS663 = _M0L6outputS665;
      }
      break;
    }
    _M0L6_2atmpS1922 = _M0Lm5indexS655;
    _M0L6_2atmpS1926 = (int32_t)_M0L6outputS663;
    _M0L6_2atmpS1925 = _M0L6_2atmpS1926 % 10;
    _M0L6_2atmpS1924 = 48 + _M0L6_2atmpS1925;
    _M0L6_2atmpS1923 = _M0L6_2atmpS1924 & 0xff;
    if (
      _M0L6_2atmpS1922 < 0
      || _M0L6_2atmpS1922 >= Moonbit_array_length(_M0L6resultS654)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS654[_M0L6_2atmpS1922] = _M0L6_2atmpS1923;
    if (_M0L7olengthS659 > 1) {
      int32_t _M0L6_2atmpS1928 = _M0Lm5indexS655;
      int32_t _M0L6_2atmpS1927 = _M0L6_2atmpS1928 + 1;
      if (
        _M0L6_2atmpS1927 < 0
        || _M0L6_2atmpS1927 >= Moonbit_array_length(_M0L6resultS654)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS654[_M0L6_2atmpS1927] = 46;
    } else {
      int32_t _M0L6_2atmpS1929 = _M0Lm5indexS655;
      _M0Lm5indexS655 = _M0L6_2atmpS1929 - 1;
    }
    _M0L6_2atmpS1930 = _M0Lm5indexS655;
    _M0L6_2atmpS1931 = _M0L7olengthS659 + 1;
    _M0Lm5indexS655 = _M0L6_2atmpS1930 + _M0L6_2atmpS1931;
    _M0L6_2atmpS1932 = _M0Lm5indexS655;
    if (
      _M0L6_2atmpS1932 < 0
      || _M0L6_2atmpS1932 >= Moonbit_array_length(_M0L6resultS654)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS654[_M0L6_2atmpS1932] = 101;
    _M0L6_2atmpS1933 = _M0Lm5indexS655;
    _M0Lm5indexS655 = _M0L6_2atmpS1933 + 1;
    _M0L6_2atmpS1934 = _M0Lm3expS660;
    if (_M0L6_2atmpS1934 < 0) {
      int32_t _M0L6_2atmpS1935 = _M0Lm5indexS655;
      int32_t _M0L6_2atmpS1936;
      int32_t _M0L6_2atmpS1937;
      if (
        _M0L6_2atmpS1935 < 0
        || _M0L6_2atmpS1935 >= Moonbit_array_length(_M0L6resultS654)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS654[_M0L6_2atmpS1935] = 45;
      _M0L6_2atmpS1936 = _M0Lm5indexS655;
      _M0Lm5indexS655 = _M0L6_2atmpS1936 + 1;
      _M0L6_2atmpS1937 = _M0Lm3expS660;
      _M0Lm3expS660 = -_M0L6_2atmpS1937;
    } else {
      int32_t _M0L6_2atmpS1938 = _M0Lm5indexS655;
      int32_t _M0L6_2atmpS1939;
      if (
        _M0L6_2atmpS1938 < 0
        || _M0L6_2atmpS1938 >= Moonbit_array_length(_M0L6resultS654)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS654[_M0L6_2atmpS1938] = 43;
      _M0L6_2atmpS1939 = _M0Lm5indexS655;
      _M0Lm5indexS655 = _M0L6_2atmpS1939 + 1;
    }
    _M0L6_2atmpS1940 = _M0Lm3expS660;
    if (_M0L6_2atmpS1940 >= 100) {
      int32_t _M0L6_2atmpS1956 = _M0Lm3expS660;
      int32_t _M0L1aS668 = _M0L6_2atmpS1956 / 100;
      int32_t _M0L6_2atmpS1955 = _M0Lm3expS660;
      int32_t _M0L6_2atmpS1954 = _M0L6_2atmpS1955 / 10;
      int32_t _M0L1bS669 = _M0L6_2atmpS1954 % 10;
      int32_t _M0L6_2atmpS1953 = _M0Lm3expS660;
      int32_t _M0L1cS670 = _M0L6_2atmpS1953 % 10;
      int32_t _M0L6_2atmpS1941 = _M0Lm5indexS655;
      int32_t _M0L6_2atmpS1943 = 48 + _M0L1aS668;
      int32_t _M0L6_2atmpS1942 = _M0L6_2atmpS1943 & 0xff;
      int32_t _M0L6_2atmpS1947;
      int32_t _M0L6_2atmpS1944;
      int32_t _M0L6_2atmpS1946;
      int32_t _M0L6_2atmpS1945;
      int32_t _M0L6_2atmpS1951;
      int32_t _M0L6_2atmpS1948;
      int32_t _M0L6_2atmpS1950;
      int32_t _M0L6_2atmpS1949;
      int32_t _M0L6_2atmpS1952;
      if (
        _M0L6_2atmpS1941 < 0
        || _M0L6_2atmpS1941 >= Moonbit_array_length(_M0L6resultS654)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS654[_M0L6_2atmpS1941] = _M0L6_2atmpS1942;
      _M0L6_2atmpS1947 = _M0Lm5indexS655;
      _M0L6_2atmpS1944 = _M0L6_2atmpS1947 + 1;
      _M0L6_2atmpS1946 = 48 + _M0L1bS669;
      _M0L6_2atmpS1945 = _M0L6_2atmpS1946 & 0xff;
      if (
        _M0L6_2atmpS1944 < 0
        || _M0L6_2atmpS1944 >= Moonbit_array_length(_M0L6resultS654)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS654[_M0L6_2atmpS1944] = _M0L6_2atmpS1945;
      _M0L6_2atmpS1951 = _M0Lm5indexS655;
      _M0L6_2atmpS1948 = _M0L6_2atmpS1951 + 2;
      _M0L6_2atmpS1950 = 48 + _M0L1cS670;
      _M0L6_2atmpS1949 = _M0L6_2atmpS1950 & 0xff;
      if (
        _M0L6_2atmpS1948 < 0
        || _M0L6_2atmpS1948 >= Moonbit_array_length(_M0L6resultS654)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS654[_M0L6_2atmpS1948] = _M0L6_2atmpS1949;
      _M0L6_2atmpS1952 = _M0Lm5indexS655;
      _M0Lm5indexS655 = _M0L6_2atmpS1952 + 3;
    } else {
      int32_t _M0L6_2atmpS1957 = _M0Lm3expS660;
      if (_M0L6_2atmpS1957 >= 10) {
        int32_t _M0L6_2atmpS1967 = _M0Lm3expS660;
        int32_t _M0L1aS671 = _M0L6_2atmpS1967 / 10;
        int32_t _M0L6_2atmpS1966 = _M0Lm3expS660;
        int32_t _M0L1bS672 = _M0L6_2atmpS1966 % 10;
        int32_t _M0L6_2atmpS1958 = _M0Lm5indexS655;
        int32_t _M0L6_2atmpS1960 = 48 + _M0L1aS671;
        int32_t _M0L6_2atmpS1959 = _M0L6_2atmpS1960 & 0xff;
        int32_t _M0L6_2atmpS1964;
        int32_t _M0L6_2atmpS1961;
        int32_t _M0L6_2atmpS1963;
        int32_t _M0L6_2atmpS1962;
        int32_t _M0L6_2atmpS1965;
        if (
          _M0L6_2atmpS1958 < 0
          || _M0L6_2atmpS1958 >= Moonbit_array_length(_M0L6resultS654)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS654[_M0L6_2atmpS1958] = _M0L6_2atmpS1959;
        _M0L6_2atmpS1964 = _M0Lm5indexS655;
        _M0L6_2atmpS1961 = _M0L6_2atmpS1964 + 1;
        _M0L6_2atmpS1963 = 48 + _M0L1bS672;
        _M0L6_2atmpS1962 = _M0L6_2atmpS1963 & 0xff;
        if (
          _M0L6_2atmpS1961 < 0
          || _M0L6_2atmpS1961 >= Moonbit_array_length(_M0L6resultS654)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS654[_M0L6_2atmpS1961] = _M0L6_2atmpS1962;
        _M0L6_2atmpS1965 = _M0Lm5indexS655;
        _M0Lm5indexS655 = _M0L6_2atmpS1965 + 2;
      } else {
        int32_t _M0L6_2atmpS1968 = _M0Lm5indexS655;
        int32_t _M0L6_2atmpS1971 = _M0Lm3expS660;
        int32_t _M0L6_2atmpS1970 = 48 + _M0L6_2atmpS1971;
        int32_t _M0L6_2atmpS1969 = _M0L6_2atmpS1970 & 0xff;
        int32_t _M0L6_2atmpS1972;
        if (
          _M0L6_2atmpS1968 < 0
          || _M0L6_2atmpS1968 >= Moonbit_array_length(_M0L6resultS654)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS654[_M0L6_2atmpS1968] = _M0L6_2atmpS1969;
        _M0L6_2atmpS1972 = _M0Lm5indexS655;
        _M0Lm5indexS655 = _M0L6_2atmpS1972 + 1;
      }
    }
    _M0L6_2atmpS1973 = _M0Lm5indexS655;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_3281
    = _M0FPB19string__from__bytes(_M0L6resultS654, 0, _M0L6_2atmpS1973);
    moonbit_decref_cycle_free(_M0L6resultS654);
    return _result_3281;
  } else {
    int32_t _M0L6_2atmpS1982 = _M0Lm3expS660;
    int32_t _M0L6_2atmpS2045;
    moonbit_string_t _result_3287;
    if (_M0L6_2atmpS1982 < 0) {
      int32_t _M0L6_2atmpS1983 = _M0Lm5indexS655;
      int32_t _M0L6_2atmpS1985;
      int32_t _M0L6_2atmpS1984;
      int32_t _M0L6_2atmpS1986;
      int32_t _M0L1iS673;
      int32_t _M0L6_2atmpS2001;
      int32_t _M0L6_2atmpS2003;
      int32_t _M0L6_2atmpS2002;
      int32_t _M0L7currentS675;
      int32_t _M0L1iS676;
      uint64_t _M0L6outputS677;
      if (
        _M0L6_2atmpS1983 < 0
        || _M0L6_2atmpS1983 >= Moonbit_array_length(_M0L6resultS654)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS654[_M0L6_2atmpS1983] = 48;
      _M0L6_2atmpS1985 = _M0Lm5indexS655;
      _M0L6_2atmpS1984 = _M0L6_2atmpS1985 + 1;
      if (
        _M0L6_2atmpS1984 < 0
        || _M0L6_2atmpS1984 >= Moonbit_array_length(_M0L6resultS654)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS654[_M0L6_2atmpS1984] = 46;
      _M0L6_2atmpS1986 = _M0Lm5indexS655;
      _M0Lm5indexS655 = _M0L6_2atmpS1986 + 2;
      _M0L1iS673 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1987 = _M0Lm3expS660;
        if (_M0L1iS673 > _M0L6_2atmpS1987) {
          int32_t _M0L6_2atmpS1990 = _M0Lm5indexS655;
          int32_t _M0L6_2atmpS1989 = _M0L6_2atmpS1990 - _M0L1iS673;
          int32_t _M0L6_2atmpS1988 = _M0L6_2atmpS1989 - 1;
          int32_t _M0L6_2atmpS1991;
          if (
            _M0L6_2atmpS1988 < 0
            || _M0L6_2atmpS1988 >= Moonbit_array_length(_M0L6resultS654)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS654[_M0L6_2atmpS1988] = 48;
          _M0L6_2atmpS1991 = _M0L1iS673 - 1;
          _M0L1iS673 = _M0L6_2atmpS1991;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2001 = _M0Lm5indexS655;
      _M0L6_2atmpS2003 = _M0Lm3expS660;
      _M0L6_2atmpS2002 = -1 - _M0L6_2atmpS2003;
      _M0L7currentS675 = _M0L6_2atmpS2001 + _M0L6_2atmpS2002;
      _M0L1iS676 = 0;
      _M0L6outputS677 = _M0L6outputS657;
      while (1) {
        if (_M0L1iS676 < _M0L7olengthS659) {
          int32_t _M0L6_2atmpS1998 = _M0L7currentS675 + _M0L7olengthS659;
          int32_t _M0L6_2atmpS1997 = _M0L6_2atmpS1998 - _M0L1iS676;
          int32_t _M0L6_2atmpS1992 = _M0L6_2atmpS1997 - 1;
          uint64_t _M0L6_2atmpS1996 = _M0L6outputS677 % 10ull;
          int32_t _M0L6_2atmpS1995 = (int32_t)_M0L6_2atmpS1996;
          int32_t _M0L6_2atmpS1994 = 48 + _M0L6_2atmpS1995;
          int32_t _M0L6_2atmpS1993 = _M0L6_2atmpS1994 & 0xff;
          int32_t _M0L6_2atmpS1999;
          uint64_t _M0L6_2atmpS2000;
          if (
            _M0L6_2atmpS1992 < 0
            || _M0L6_2atmpS1992 >= Moonbit_array_length(_M0L6resultS654)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS654[_M0L6_2atmpS1992] = _M0L6_2atmpS1993;
          _M0L6_2atmpS1999 = _M0L1iS676 + 1;
          _M0L6_2atmpS2000 = _M0L6outputS677 / 10ull;
          _M0L1iS676 = _M0L6_2atmpS1999;
          _M0L6outputS677 = _M0L6_2atmpS2000;
          continue;
        }
        break;
      }
      _M0Lm5indexS655 = _M0L7currentS675 + _M0L7olengthS659;
    } else {
      int32_t _M0L6_2atmpS2005 = _M0Lm3expS660;
      int32_t _M0L6_2atmpS2004 = _M0L6_2atmpS2005 + 1;
      if (_M0L6_2atmpS2004 >= _M0L7olengthS659) {
        int32_t _M0L1iS679 = 0;
        uint64_t _M0L6outputS680 = _M0L6outputS657;
        int32_t _M0L6_2atmpS2016;
        int32_t _M0L6_2atmpS2021;
        int32_t _M0L7_2abindS682;
        int32_t _M0L1iS683;
        int32_t _M0L6_2atmpS2022;
        int32_t _M0L6_2atmpS2025;
        int32_t _M0L6_2atmpS2024;
        int32_t _M0L6_2atmpS2023;
        while (1) {
          if (_M0L1iS679 < _M0L7olengthS659) {
            int32_t _M0L6_2atmpS2013 = _M0Lm5indexS655;
            int32_t _M0L6_2atmpS2012 = _M0L6_2atmpS2013 + _M0L7olengthS659;
            int32_t _M0L6_2atmpS2011 = _M0L6_2atmpS2012 - _M0L1iS679;
            int32_t _M0L6_2atmpS2006 = _M0L6_2atmpS2011 - 1;
            uint64_t _M0L6_2atmpS2010 = _M0L6outputS680 % 10ull;
            int32_t _M0L6_2atmpS2009 = (int32_t)_M0L6_2atmpS2010;
            int32_t _M0L6_2atmpS2008 = 48 + _M0L6_2atmpS2009;
            int32_t _M0L6_2atmpS2007 = _M0L6_2atmpS2008 & 0xff;
            int32_t _M0L6_2atmpS2014;
            uint64_t _M0L6_2atmpS2015;
            if (
              _M0L6_2atmpS2006 < 0
              || _M0L6_2atmpS2006 >= Moonbit_array_length(_M0L6resultS654)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS654[_M0L6_2atmpS2006] = _M0L6_2atmpS2007;
            _M0L6_2atmpS2014 = _M0L1iS679 + 1;
            _M0L6_2atmpS2015 = _M0L6outputS680 / 10ull;
            _M0L1iS679 = _M0L6_2atmpS2014;
            _M0L6outputS680 = _M0L6_2atmpS2015;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2016 = _M0Lm5indexS655;
        _M0Lm5indexS655 = _M0L6_2atmpS2016 + _M0L7olengthS659;
        _M0L6_2atmpS2021 = _M0Lm3expS660;
        _M0L7_2abindS682 = _M0L6_2atmpS2021 + 1;
        _M0L1iS683 = _M0L7olengthS659;
        while (1) {
          if (_M0L1iS683 < _M0L7_2abindS682) {
            int32_t _M0L6_2atmpS2019 = _M0Lm5indexS655;
            int32_t _M0L6_2atmpS2018 = _M0L6_2atmpS2019 + _M0L1iS683;
            int32_t _M0L6_2atmpS2017 = _M0L6_2atmpS2018 - _M0L7olengthS659;
            int32_t _M0L6_2atmpS2020;
            if (
              _M0L6_2atmpS2017 < 0
              || _M0L6_2atmpS2017 >= Moonbit_array_length(_M0L6resultS654)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS654[_M0L6_2atmpS2017] = 48;
            _M0L6_2atmpS2020 = _M0L1iS683 + 1;
            _M0L1iS683 = _M0L6_2atmpS2020;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2022 = _M0Lm5indexS655;
        _M0L6_2atmpS2025 = _M0Lm3expS660;
        _M0L6_2atmpS2024 = _M0L6_2atmpS2025 + 1;
        _M0L6_2atmpS2023 = _M0L6_2atmpS2024 - _M0L7olengthS659;
        _M0Lm5indexS655 = _M0L6_2atmpS2022 + _M0L6_2atmpS2023;
      } else {
        int32_t _M0L6_2atmpS2042 = _M0Lm5indexS655;
        int32_t _M0L6_2atmpS2041 = _M0L6_2atmpS2042 + 1;
        int32_t _M0L1iS685 = 0;
        int32_t _M0L7currentS686 = _M0L6_2atmpS2041;
        uint64_t _M0L6outputS687 = _M0L6outputS657;
        int32_t _M0L6_2atmpS2043;
        int32_t _M0L6_2atmpS2044;
        while (1) {
          if (_M0L1iS685 < _M0L7olengthS659) {
            int32_t _M0L6_2atmpS2037 = _M0L7olengthS659 - _M0L1iS685;
            int32_t _M0L6_2atmpS2035 = _M0L6_2atmpS2037 - 1;
            int32_t _M0L6_2atmpS2036 = _M0Lm3expS660;
            int32_t _M0L7currentS688;
            int32_t _M0L6_2atmpS2032;
            int32_t _M0L6_2atmpS2031;
            int32_t _M0L6_2atmpS2026;
            uint64_t _M0L6_2atmpS2030;
            int32_t _M0L6_2atmpS2029;
            int32_t _M0L6_2atmpS2028;
            int32_t _M0L6_2atmpS2027;
            int32_t _M0L6_2atmpS2033;
            uint64_t _M0L6_2atmpS2034;
            if (_M0L6_2atmpS2035 == _M0L6_2atmpS2036) {
              int32_t _M0L6_2atmpS2040 = _M0L7currentS686 + _M0L7olengthS659;
              int32_t _M0L6_2atmpS2039 = _M0L6_2atmpS2040 - _M0L1iS685;
              int32_t _M0L6_2atmpS2038 = _M0L6_2atmpS2039 - 1;
              if (
                _M0L6_2atmpS2038 < 0
                || _M0L6_2atmpS2038 >= Moonbit_array_length(_M0L6resultS654)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS654[_M0L6_2atmpS2038] = 46;
              _M0L7currentS688 = _M0L7currentS686 - 1;
            } else {
              _M0L7currentS688 = _M0L7currentS686;
            }
            _M0L6_2atmpS2032 = _M0L7currentS688 + _M0L7olengthS659;
            _M0L6_2atmpS2031 = _M0L6_2atmpS2032 - _M0L1iS685;
            _M0L6_2atmpS2026 = _M0L6_2atmpS2031 - 1;
            _M0L6_2atmpS2030 = _M0L6outputS687 % 10ull;
            _M0L6_2atmpS2029 = (int32_t)_M0L6_2atmpS2030;
            _M0L6_2atmpS2028 = 48 + _M0L6_2atmpS2029;
            _M0L6_2atmpS2027 = _M0L6_2atmpS2028 & 0xff;
            if (
              _M0L6_2atmpS2026 < 0
              || _M0L6_2atmpS2026 >= Moonbit_array_length(_M0L6resultS654)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS654[_M0L6_2atmpS2026] = _M0L6_2atmpS2027;
            _M0L6_2atmpS2033 = _M0L1iS685 + 1;
            _M0L6_2atmpS2034 = _M0L6outputS687 / 10ull;
            _M0L1iS685 = _M0L6_2atmpS2033;
            _M0L7currentS686 = _M0L7currentS688;
            _M0L6outputS687 = _M0L6_2atmpS2034;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2043 = _M0Lm5indexS655;
        _M0L6_2atmpS2044 = _M0L7olengthS659 + 1;
        _M0Lm5indexS655 = _M0L6_2atmpS2043 + _M0L6_2atmpS2044;
      }
    }
    _M0L6_2atmpS2045 = _M0Lm5indexS655;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_3287
    = _M0FPB19string__from__bytes(_M0L6resultS654, 0, _M0L6_2atmpS2045);
    moonbit_decref_cycle_free(_M0L6resultS654);
    return _result_3287;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS600,
  uint32_t _M0L12ieeeExponentS599
) {
  int32_t _M0Lm2e2S597;
  uint64_t _M0Lm2m2S598;
  uint64_t _M0L6_2atmpS1919;
  uint64_t _M0L6_2atmpS1918;
  int32_t _M0L4evenS601;
  uint64_t _M0L6_2atmpS1917;
  uint64_t _M0L2mvS602;
  int32_t _M0L7mmShiftS603;
  uint64_t _M0Lm2vrS604;
  uint64_t _M0Lm2vpS605;
  uint64_t _M0Lm2vmS606;
  int32_t _M0Lm3e10S607;
  int32_t _M0Lm17vmIsTrailingZerosS608;
  int32_t _M0Lm17vrIsTrailingZerosS609;
  int32_t _M0L6_2atmpS1819;
  int32_t _M0Lm7removedS628;
  int32_t _M0Lm16lastRemovedDigitS629;
  uint64_t _M0Lm6outputS630;
  int32_t _M0L6_2atmpS1915;
  int32_t _M0L6_2atmpS1916;
  int32_t _M0L3expS653;
  uint64_t _M0L6_2atmpS1914;
  struct _M0TPB17FloatingDecimal64* _block_3293;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S597 = 0;
  _M0Lm2m2S598 = 0ull;
  if (_M0L12ieeeExponentS599 == 0u) {
    _M0Lm2e2S597 = -1076;
    _M0Lm2m2S598 = _M0L12ieeeMantissaS600;
  } else {
    int32_t _M0L6_2atmpS1818 = *(int32_t*)&_M0L12ieeeExponentS599;
    int32_t _M0L6_2atmpS1817 = _M0L6_2atmpS1818 - 1023;
    int32_t _M0L6_2atmpS1816 = _M0L6_2atmpS1817 - 52;
    _M0Lm2e2S597 = _M0L6_2atmpS1816 - 2;
    _M0Lm2m2S598 = 4503599627370496ull | _M0L12ieeeMantissaS600;
  }
  _M0L6_2atmpS1919 = _M0Lm2m2S598;
  _M0L6_2atmpS1918 = _M0L6_2atmpS1919 & 1ull;
  _M0L4evenS601 = _M0L6_2atmpS1918 == 0ull;
  _M0L6_2atmpS1917 = _M0Lm2m2S598;
  _M0L2mvS602 = 4ull * _M0L6_2atmpS1917;
  _M0L7mmShiftS603
  = _M0L12ieeeMantissaS600 != 0ull || _M0L12ieeeExponentS599 <= 1u;
  _M0Lm2vrS604 = 0ull;
  _M0Lm2vpS605 = 0ull;
  _M0Lm2vmS606 = 0ull;
  _M0Lm3e10S607 = 0;
  _M0Lm17vmIsTrailingZerosS608 = 0;
  _M0Lm17vrIsTrailingZerosS609 = 0;
  _M0L6_2atmpS1819 = _M0Lm2e2S597;
  if (_M0L6_2atmpS1819 >= 0) {
    int32_t _M0L6_2atmpS1841 = _M0Lm2e2S597;
    int32_t _M0L6_2atmpS1837;
    int32_t _M0L6_2atmpS1840;
    int32_t _M0L6_2atmpS1839;
    int32_t _M0L6_2atmpS1838;
    int32_t _M0L1qS610;
    int32_t _M0L6_2atmpS1836;
    int32_t _M0L6_2atmpS1835;
    int32_t _M0L1kS611;
    int32_t _M0L6_2atmpS1834;
    int32_t _M0L6_2atmpS1833;
    int32_t _M0L6_2atmpS1832;
    int32_t _M0L1iS612;
    struct _M0TPB8Pow5Pair _M0L4pow5S613;
    uint64_t _M0L6_2atmpS1831;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS614;
    uint64_t _M0L8_2avrOutS615;
    uint64_t _M0L8_2avpOutS616;
    uint64_t _M0L8_2avmOutS617;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1837 = _M0FPB9log10Pow2(_M0L6_2atmpS1841);
    _M0L6_2atmpS1840 = _M0Lm2e2S597;
    _M0L6_2atmpS1839 = _M0L6_2atmpS1840 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1838 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1839);
    _M0L1qS610 = _M0L6_2atmpS1837 - _M0L6_2atmpS1838;
    _M0Lm3e10S607 = _M0L1qS610;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1836 = _M0FPB8pow5bits(_M0L1qS610);
    _M0L6_2atmpS1835 = 125 + _M0L6_2atmpS1836;
    _M0L1kS611 = _M0L6_2atmpS1835 - 1;
    _M0L6_2atmpS1834 = _M0Lm2e2S597;
    _M0L6_2atmpS1833 = -_M0L6_2atmpS1834;
    _M0L6_2atmpS1832 = _M0L6_2atmpS1833 + _M0L1qS610;
    _M0L1iS612 = _M0L6_2atmpS1832 + _M0L1kS611;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S613 = _M0FPB22double__computeInvPow5(_M0L1qS610);
    _M0L6_2atmpS1831 = _M0Lm2m2S598;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS614
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1831, _M0L4pow5S613, _M0L1iS612, _M0L7mmShiftS603);
    _M0L8_2avrOutS615 = _M0L7_2abindS614.$0;
    _M0L8_2avpOutS616 = _M0L7_2abindS614.$1;
    _M0L8_2avmOutS617 = _M0L7_2abindS614.$2;
    _M0Lm2vrS604 = _M0L8_2avrOutS615;
    _M0Lm2vpS605 = _M0L8_2avpOutS616;
    _M0Lm2vmS606 = _M0L8_2avmOutS617;
    if (_M0L1qS610 <= 21) {
      int32_t _M0L6_2atmpS1827 = (int32_t)_M0L2mvS602;
      uint64_t _M0L6_2atmpS1830 = _M0L2mvS602 / 5ull;
      int32_t _M0L6_2atmpS1829 = (int32_t)_M0L6_2atmpS1830;
      int32_t _M0L6_2atmpS1828 = 5 * _M0L6_2atmpS1829;
      int32_t _M0L6mvMod5S618 = _M0L6_2atmpS1827 - _M0L6_2atmpS1828;
      if (_M0L6mvMod5S618 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS609
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS602, _M0L1qS610);
      } else if (_M0L4evenS601) {
        uint64_t _M0L6_2atmpS1821 = _M0L2mvS602 - 1ull;
        uint64_t _M0L6_2atmpS1822;
        uint64_t _M0L6_2atmpS1820;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1822 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS603);
        _M0L6_2atmpS1820 = _M0L6_2atmpS1821 - _M0L6_2atmpS1822;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS608
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1820, _M0L1qS610);
      } else {
        uint64_t _M0L6_2atmpS1823 = _M0Lm2vpS605;
        uint64_t _M0L6_2atmpS1826 = _M0L2mvS602 + 2ull;
        int32_t _M0L6_2atmpS1825;
        uint64_t _M0L6_2atmpS1824;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1825
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1826, _M0L1qS610);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1824 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1825);
        _M0Lm2vpS605 = _M0L6_2atmpS1823 - _M0L6_2atmpS1824;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1855 = _M0Lm2e2S597;
    int32_t _M0L6_2atmpS1854 = -_M0L6_2atmpS1855;
    int32_t _M0L6_2atmpS1849;
    int32_t _M0L6_2atmpS1853;
    int32_t _M0L6_2atmpS1852;
    int32_t _M0L6_2atmpS1851;
    int32_t _M0L6_2atmpS1850;
    int32_t _M0L1qS619;
    int32_t _M0L6_2atmpS1842;
    int32_t _M0L6_2atmpS1848;
    int32_t _M0L6_2atmpS1847;
    int32_t _M0L1iS620;
    int32_t _M0L6_2atmpS1846;
    int32_t _M0L1kS621;
    int32_t _M0L1jS622;
    struct _M0TPB8Pow5Pair _M0L4pow5S623;
    uint64_t _M0L6_2atmpS1845;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS624;
    uint64_t _M0L8_2avrOutS625;
    uint64_t _M0L8_2avpOutS626;
    uint64_t _M0L8_2avmOutS627;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1849 = _M0FPB9log10Pow5(_M0L6_2atmpS1854);
    _M0L6_2atmpS1853 = _M0Lm2e2S597;
    _M0L6_2atmpS1852 = -_M0L6_2atmpS1853;
    _M0L6_2atmpS1851 = _M0L6_2atmpS1852 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1850 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1851);
    _M0L1qS619 = _M0L6_2atmpS1849 - _M0L6_2atmpS1850;
    _M0L6_2atmpS1842 = _M0Lm2e2S597;
    _M0Lm3e10S607 = _M0L1qS619 + _M0L6_2atmpS1842;
    _M0L6_2atmpS1848 = _M0Lm2e2S597;
    _M0L6_2atmpS1847 = -_M0L6_2atmpS1848;
    _M0L1iS620 = _M0L6_2atmpS1847 - _M0L1qS619;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1846 = _M0FPB8pow5bits(_M0L1iS620);
    _M0L1kS621 = _M0L6_2atmpS1846 - 125;
    _M0L1jS622 = _M0L1qS619 - _M0L1kS621;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S623 = _M0FPB19double__computePow5(_M0L1iS620);
    _M0L6_2atmpS1845 = _M0Lm2m2S598;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS624
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1845, _M0L4pow5S623, _M0L1jS622, _M0L7mmShiftS603);
    _M0L8_2avrOutS625 = _M0L7_2abindS624.$0;
    _M0L8_2avpOutS626 = _M0L7_2abindS624.$1;
    _M0L8_2avmOutS627 = _M0L7_2abindS624.$2;
    _M0Lm2vrS604 = _M0L8_2avrOutS625;
    _M0Lm2vpS605 = _M0L8_2avpOutS626;
    _M0Lm2vmS606 = _M0L8_2avmOutS627;
    if (_M0L1qS619 <= 1) {
      _M0Lm17vrIsTrailingZerosS609 = 1;
      if (_M0L4evenS601) {
        int32_t _M0L6_2atmpS1843;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1843 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS603);
        _M0Lm17vmIsTrailingZerosS608 = _M0L6_2atmpS1843 == 1;
      } else {
        uint64_t _M0L6_2atmpS1844 = _M0Lm2vpS605;
        _M0Lm2vpS605 = _M0L6_2atmpS1844 - 1ull;
      }
    } else if (_M0L1qS619 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS609
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS602, _M0L1qS619);
    }
  }
  _M0Lm7removedS628 = 0;
  _M0Lm16lastRemovedDigitS629 = 0;
  _M0Lm6outputS630 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS608 || _M0Lm17vrIsTrailingZerosS609) {
    int32_t _if__result_3290;
    uint64_t _M0L6_2atmpS1885;
    uint64_t _M0L6_2atmpS1891;
    uint64_t _M0L6_2atmpS1892;
    int32_t _if__result_3291;
    int32_t _M0L6_2atmpS1888;
    int64_t _M0L6_2atmpS1887;
    uint64_t _M0L6_2atmpS1886;
    while (1) {
      uint64_t _M0L6_2atmpS1868 = _M0Lm2vpS605;
      uint64_t _M0L7vpDiv10S631 = _M0L6_2atmpS1868 / 10ull;
      uint64_t _M0L6_2atmpS1867 = _M0Lm2vmS606;
      uint64_t _M0L7vmDiv10S632 = _M0L6_2atmpS1867 / 10ull;
      uint64_t _M0L6_2atmpS1866;
      int32_t _M0L6_2atmpS1863;
      int32_t _M0L6_2atmpS1865;
      int32_t _M0L6_2atmpS1864;
      int32_t _M0L7vmMod10S634;
      uint64_t _M0L6_2atmpS1862;
      uint64_t _M0L7vrDiv10S635;
      uint64_t _M0L6_2atmpS1861;
      int32_t _M0L6_2atmpS1858;
      int32_t _M0L6_2atmpS1860;
      int32_t _M0L6_2atmpS1859;
      int32_t _M0L7vrMod10S636;
      int32_t _M0L6_2atmpS1857;
      if (_M0L7vpDiv10S631 <= _M0L7vmDiv10S632) {
        break;
      }
      _M0L6_2atmpS1866 = _M0Lm2vmS606;
      _M0L6_2atmpS1863 = (int32_t)_M0L6_2atmpS1866;
      _M0L6_2atmpS1865 = (int32_t)_M0L7vmDiv10S632;
      _M0L6_2atmpS1864 = 10 * _M0L6_2atmpS1865;
      _M0L7vmMod10S634 = _M0L6_2atmpS1863 - _M0L6_2atmpS1864;
      _M0L6_2atmpS1862 = _M0Lm2vrS604;
      _M0L7vrDiv10S635 = _M0L6_2atmpS1862 / 10ull;
      _M0L6_2atmpS1861 = _M0Lm2vrS604;
      _M0L6_2atmpS1858 = (int32_t)_M0L6_2atmpS1861;
      _M0L6_2atmpS1860 = (int32_t)_M0L7vrDiv10S635;
      _M0L6_2atmpS1859 = 10 * _M0L6_2atmpS1860;
      _M0L7vrMod10S636 = _M0L6_2atmpS1858 - _M0L6_2atmpS1859;
      _M0Lm17vmIsTrailingZerosS608
      = _M0Lm17vmIsTrailingZerosS608 && _M0L7vmMod10S634 == 0;
      if (_M0Lm17vrIsTrailingZerosS609) {
        int32_t _M0L6_2atmpS1856 = _M0Lm16lastRemovedDigitS629;
        _M0Lm17vrIsTrailingZerosS609 = _M0L6_2atmpS1856 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS609 = 0;
      }
      _M0Lm16lastRemovedDigitS629 = _M0L7vrMod10S636;
      _M0Lm2vrS604 = _M0L7vrDiv10S635;
      _M0Lm2vpS605 = _M0L7vpDiv10S631;
      _M0Lm2vmS606 = _M0L7vmDiv10S632;
      _M0L6_2atmpS1857 = _M0Lm7removedS628;
      _M0Lm7removedS628 = _M0L6_2atmpS1857 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS608) {
      while (1) {
        uint64_t _M0L6_2atmpS1881 = _M0Lm2vmS606;
        uint64_t _M0L7vmDiv10S637 = _M0L6_2atmpS1881 / 10ull;
        uint64_t _M0L6_2atmpS1880 = _M0Lm2vmS606;
        int32_t _M0L6_2atmpS1877 = (int32_t)_M0L6_2atmpS1880;
        int32_t _M0L6_2atmpS1879 = (int32_t)_M0L7vmDiv10S637;
        int32_t _M0L6_2atmpS1878 = 10 * _M0L6_2atmpS1879;
        int32_t _M0L7vmMod10S638 = _M0L6_2atmpS1877 - _M0L6_2atmpS1878;
        uint64_t _M0L6_2atmpS1876;
        uint64_t _M0L7vpDiv10S640;
        uint64_t _M0L6_2atmpS1875;
        uint64_t _M0L7vrDiv10S641;
        uint64_t _M0L6_2atmpS1874;
        int32_t _M0L6_2atmpS1871;
        int32_t _M0L6_2atmpS1873;
        int32_t _M0L6_2atmpS1872;
        int32_t _M0L7vrMod10S642;
        int32_t _M0L6_2atmpS1870;
        if (_M0L7vmMod10S638 != 0) {
          break;
        }
        _M0L6_2atmpS1876 = _M0Lm2vpS605;
        _M0L7vpDiv10S640 = _M0L6_2atmpS1876 / 10ull;
        _M0L6_2atmpS1875 = _M0Lm2vrS604;
        _M0L7vrDiv10S641 = _M0L6_2atmpS1875 / 10ull;
        _M0L6_2atmpS1874 = _M0Lm2vrS604;
        _M0L6_2atmpS1871 = (int32_t)_M0L6_2atmpS1874;
        _M0L6_2atmpS1873 = (int32_t)_M0L7vrDiv10S641;
        _M0L6_2atmpS1872 = 10 * _M0L6_2atmpS1873;
        _M0L7vrMod10S642 = _M0L6_2atmpS1871 - _M0L6_2atmpS1872;
        if (_M0Lm17vrIsTrailingZerosS609) {
          int32_t _M0L6_2atmpS1869 = _M0Lm16lastRemovedDigitS629;
          _M0Lm17vrIsTrailingZerosS609 = _M0L6_2atmpS1869 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS609 = 0;
        }
        _M0Lm16lastRemovedDigitS629 = _M0L7vrMod10S642;
        _M0Lm2vrS604 = _M0L7vrDiv10S641;
        _M0Lm2vpS605 = _M0L7vpDiv10S640;
        _M0Lm2vmS606 = _M0L7vmDiv10S637;
        _M0L6_2atmpS1870 = _M0Lm7removedS628;
        _M0Lm7removedS628 = _M0L6_2atmpS1870 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS609) {
      int32_t _M0L6_2atmpS1884 = _M0Lm16lastRemovedDigitS629;
      if (_M0L6_2atmpS1884 == 5) {
        uint64_t _M0L6_2atmpS1883 = _M0Lm2vrS604;
        uint64_t _M0L6_2atmpS1882 = _M0L6_2atmpS1883 % 2ull;
        _if__result_3290 = _M0L6_2atmpS1882 == 0ull;
      } else {
        _if__result_3290 = 0;
      }
    } else {
      _if__result_3290 = 0;
    }
    if (_if__result_3290) {
      _M0Lm16lastRemovedDigitS629 = 4;
    }
    _M0L6_2atmpS1885 = _M0Lm2vrS604;
    _M0L6_2atmpS1891 = _M0Lm2vrS604;
    _M0L6_2atmpS1892 = _M0Lm2vmS606;
    if (_M0L6_2atmpS1891 == _M0L6_2atmpS1892) {
      if (!_M0L4evenS601) {
        _if__result_3291 = 1;
      } else {
        int32_t _M0L6_2atmpS1890 = _M0Lm17vmIsTrailingZerosS608;
        _if__result_3291 = !_M0L6_2atmpS1890;
      }
    } else {
      _if__result_3291 = 0;
    }
    if (_if__result_3291) {
      _M0L6_2atmpS1888 = 1;
    } else {
      int32_t _M0L6_2atmpS1889 = _M0Lm16lastRemovedDigitS629;
      _M0L6_2atmpS1888 = _M0L6_2atmpS1889 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1887 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1888);
    _M0L6_2atmpS1886 = *(uint64_t*)&_M0L6_2atmpS1887;
    _M0Lm6outputS630 = _M0L6_2atmpS1885 + _M0L6_2atmpS1886;
  } else {
    int32_t _M0Lm7roundUpS643 = 0;
    uint64_t _M0L6_2atmpS1913 = _M0Lm2vpS605;
    uint64_t _M0L8vpDiv100S644 = _M0L6_2atmpS1913 / 100ull;
    uint64_t _M0L6_2atmpS1912 = _M0Lm2vmS606;
    uint64_t _M0L8vmDiv100S645 = _M0L6_2atmpS1912 / 100ull;
    uint64_t _M0L6_2atmpS1907;
    uint64_t _M0L6_2atmpS1910;
    uint64_t _M0L6_2atmpS1911;
    int32_t _M0L6_2atmpS1909;
    uint64_t _M0L6_2atmpS1908;
    if (_M0L8vpDiv100S644 > _M0L8vmDiv100S645) {
      uint64_t _M0L6_2atmpS1898 = _M0Lm2vrS604;
      uint64_t _M0L8vrDiv100S646 = _M0L6_2atmpS1898 / 100ull;
      uint64_t _M0L6_2atmpS1897 = _M0Lm2vrS604;
      int32_t _M0L6_2atmpS1894 = (int32_t)_M0L6_2atmpS1897;
      int32_t _M0L6_2atmpS1896 = (int32_t)_M0L8vrDiv100S646;
      int32_t _M0L6_2atmpS1895 = 100 * _M0L6_2atmpS1896;
      int32_t _M0L8vrMod100S647 = _M0L6_2atmpS1894 - _M0L6_2atmpS1895;
      int32_t _M0L6_2atmpS1893;
      _M0Lm7roundUpS643 = _M0L8vrMod100S647 >= 50;
      _M0Lm2vrS604 = _M0L8vrDiv100S646;
      _M0Lm2vpS605 = _M0L8vpDiv100S644;
      _M0Lm2vmS606 = _M0L8vmDiv100S645;
      _M0L6_2atmpS1893 = _M0Lm7removedS628;
      _M0Lm7removedS628 = _M0L6_2atmpS1893 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1906 = _M0Lm2vpS605;
      uint64_t _M0L7vpDiv10S648 = _M0L6_2atmpS1906 / 10ull;
      uint64_t _M0L6_2atmpS1905 = _M0Lm2vmS606;
      uint64_t _M0L7vmDiv10S649 = _M0L6_2atmpS1905 / 10ull;
      uint64_t _M0L6_2atmpS1904;
      uint64_t _M0L7vrDiv10S651;
      uint64_t _M0L6_2atmpS1903;
      int32_t _M0L6_2atmpS1900;
      int32_t _M0L6_2atmpS1902;
      int32_t _M0L6_2atmpS1901;
      int32_t _M0L7vrMod10S652;
      int32_t _M0L6_2atmpS1899;
      if (_M0L7vpDiv10S648 <= _M0L7vmDiv10S649) {
        break;
      }
      _M0L6_2atmpS1904 = _M0Lm2vrS604;
      _M0L7vrDiv10S651 = _M0L6_2atmpS1904 / 10ull;
      _M0L6_2atmpS1903 = _M0Lm2vrS604;
      _M0L6_2atmpS1900 = (int32_t)_M0L6_2atmpS1903;
      _M0L6_2atmpS1902 = (int32_t)_M0L7vrDiv10S651;
      _M0L6_2atmpS1901 = 10 * _M0L6_2atmpS1902;
      _M0L7vrMod10S652 = _M0L6_2atmpS1900 - _M0L6_2atmpS1901;
      _M0Lm7roundUpS643 = _M0L7vrMod10S652 >= 5;
      _M0Lm2vrS604 = _M0L7vrDiv10S651;
      _M0Lm2vpS605 = _M0L7vpDiv10S648;
      _M0Lm2vmS606 = _M0L7vmDiv10S649;
      _M0L6_2atmpS1899 = _M0Lm7removedS628;
      _M0Lm7removedS628 = _M0L6_2atmpS1899 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1907 = _M0Lm2vrS604;
    _M0L6_2atmpS1910 = _M0Lm2vrS604;
    _M0L6_2atmpS1911 = _M0Lm2vmS606;
    _M0L6_2atmpS1909
    = _M0L6_2atmpS1910 == _M0L6_2atmpS1911 || _M0Lm7roundUpS643;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1908 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1909);
    _M0Lm6outputS630 = _M0L6_2atmpS1907 + _M0L6_2atmpS1908;
  }
  _M0L6_2atmpS1915 = _M0Lm3e10S607;
  _M0L6_2atmpS1916 = _M0Lm7removedS628;
  _M0L3expS653 = _M0L6_2atmpS1915 + _M0L6_2atmpS1916;
  _M0L6_2atmpS1914 = _M0Lm6outputS630;
  _block_3293
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_3293)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_3293->$0 = _M0L6_2atmpS1914;
  _block_3293->$1 = _M0L3expS653;
  return _block_3293;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS596) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS596) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS595) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS595) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS594) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS594) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS593) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS593 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS593 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS593 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS593 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS593 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS593 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS593 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS593 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS593 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS593 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS593 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS593 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS593 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS593 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS593 >= 100ull) {
    return 3;
  }
  if (_M0L1vS593 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS576) {
  int32_t _M0L6_2atmpS1815;
  int32_t _M0L6_2atmpS1814;
  int32_t _M0L4baseS575;
  int32_t _M0L5base2S577;
  int32_t _M0L6offsetS578;
  int32_t _M0L6_2atmpS1813;
  uint64_t _M0L4mul0S579;
  int32_t _M0L6_2atmpS1812;
  int32_t _M0L6_2atmpS1811;
  uint64_t _M0L4mul1S580;
  uint64_t _M0L1mS581;
  struct _M0TPB7Umul128 _M0L7_2abindS582;
  uint64_t _M0L7_2alow1S583;
  uint64_t _M0L8_2ahigh1S584;
  struct _M0TPB7Umul128 _M0L7_2abindS585;
  uint64_t _M0L7_2alow0S586;
  uint64_t _M0L8_2ahigh0S587;
  uint64_t _M0L3sumS588;
  uint64_t _M0Lm5high1S589;
  int32_t _M0L6_2atmpS1809;
  int32_t _M0L6_2atmpS1810;
  int32_t _M0L5deltaS590;
  uint64_t _M0L6_2atmpS1808;
  uint64_t _M0L6_2atmpS1800;
  int32_t _M0L6_2atmpS1807;
  uint32_t _M0L6_2atmpS1804;
  int32_t _M0L6_2atmpS1806;
  int32_t _M0L6_2atmpS1805;
  uint32_t _M0L6_2atmpS1803;
  uint32_t _M0L6_2atmpS1802;
  uint64_t _M0L6_2atmpS1801;
  uint64_t _M0L1aS591;
  uint64_t _M0L6_2atmpS1799;
  uint64_t _M0L1bS592;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1815 = _M0L1iS576 + 26;
  _M0L6_2atmpS1814 = _M0L6_2atmpS1815 - 1;
  _M0L4baseS575 = _M0L6_2atmpS1814 / 26;
  _M0L5base2S577 = _M0L4baseS575 * 26;
  _M0L6offsetS578 = _M0L5base2S577 - _M0L1iS576;
  _M0L6_2atmpS1813 = _M0L4baseS575 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S579
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1813);
  _M0L6_2atmpS1812 = _M0L4baseS575 * 2;
  _M0L6_2atmpS1811 = _M0L6_2atmpS1812 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S580
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1811);
  if (_M0L6offsetS578 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S579, .$1 = _M0L4mul1S580};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS581
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS578);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS582 = _M0FPB7umul128(_M0L1mS581, _M0L4mul1S580);
  _M0L7_2alow1S583 = _M0L7_2abindS582.$0;
  _M0L8_2ahigh1S584 = _M0L7_2abindS582.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS585 = _M0FPB7umul128(_M0L1mS581, _M0L4mul0S579);
  _M0L7_2alow0S586 = _M0L7_2abindS585.$0;
  _M0L8_2ahigh0S587 = _M0L7_2abindS585.$1;
  _M0L3sumS588 = _M0L8_2ahigh0S587 + _M0L7_2alow1S583;
  _M0Lm5high1S589 = _M0L8_2ahigh1S584;
  if (_M0L3sumS588 < _M0L8_2ahigh0S587) {
    uint64_t _M0L6_2atmpS1798 = _M0Lm5high1S589;
    _M0Lm5high1S589 = _M0L6_2atmpS1798 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1809 = _M0FPB8pow5bits(_M0L5base2S577);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1810 = _M0FPB8pow5bits(_M0L1iS576);
  _M0L5deltaS590 = _M0L6_2atmpS1809 - _M0L6_2atmpS1810;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1808
  = _M0FPB13shiftright128(_M0L7_2alow0S586, _M0L3sumS588, _M0L5deltaS590);
  _M0L6_2atmpS1800 = _M0L6_2atmpS1808 + 1ull;
  _M0L6_2atmpS1807 = _M0L1iS576 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1804
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1807);
  _M0L6_2atmpS1806 = _M0L1iS576 % 16;
  _M0L6_2atmpS1805 = _M0L6_2atmpS1806 << 1;
  _M0L6_2atmpS1803 = _M0L6_2atmpS1804 >> (_M0L6_2atmpS1805 & 31);
  _M0L6_2atmpS1802 = _M0L6_2atmpS1803 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1801 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1802);
  _M0L1aS591 = _M0L6_2atmpS1800 + _M0L6_2atmpS1801;
  _M0L6_2atmpS1799 = _M0Lm5high1S589;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS592
  = _M0FPB13shiftright128(_M0L3sumS588, _M0L6_2atmpS1799, _M0L5deltaS590);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS591, .$1 = _M0L1bS592};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS558) {
  int32_t _M0L4baseS557;
  int32_t _M0L5base2S559;
  int32_t _M0L6offsetS560;
  int32_t _M0L6_2atmpS1797;
  uint64_t _M0L4mul0S561;
  int32_t _M0L6_2atmpS1796;
  int32_t _M0L6_2atmpS1795;
  uint64_t _M0L4mul1S562;
  uint64_t _M0L1mS563;
  struct _M0TPB7Umul128 _M0L7_2abindS564;
  uint64_t _M0L7_2alow1S565;
  uint64_t _M0L8_2ahigh1S566;
  struct _M0TPB7Umul128 _M0L7_2abindS567;
  uint64_t _M0L7_2alow0S568;
  uint64_t _M0L8_2ahigh0S569;
  uint64_t _M0L3sumS570;
  uint64_t _M0Lm5high1S571;
  int32_t _M0L6_2atmpS1793;
  int32_t _M0L6_2atmpS1794;
  int32_t _M0L5deltaS572;
  uint64_t _M0L6_2atmpS1785;
  int32_t _M0L6_2atmpS1792;
  uint32_t _M0L6_2atmpS1789;
  int32_t _M0L6_2atmpS1791;
  int32_t _M0L6_2atmpS1790;
  uint32_t _M0L6_2atmpS1788;
  uint32_t _M0L6_2atmpS1787;
  uint64_t _M0L6_2atmpS1786;
  uint64_t _M0L1aS573;
  uint64_t _M0L6_2atmpS1784;
  uint64_t _M0L1bS574;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS557 = _M0L1iS558 / 26;
  _M0L5base2S559 = _M0L4baseS557 * 26;
  _M0L6offsetS560 = _M0L1iS558 - _M0L5base2S559;
  _M0L6_2atmpS1797 = _M0L4baseS557 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S561
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1797);
  _M0L6_2atmpS1796 = _M0L4baseS557 * 2;
  _M0L6_2atmpS1795 = _M0L6_2atmpS1796 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S562
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1795);
  if (_M0L6offsetS560 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S561, .$1 = _M0L4mul1S562};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS563
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS560);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS564 = _M0FPB7umul128(_M0L1mS563, _M0L4mul1S562);
  _M0L7_2alow1S565 = _M0L7_2abindS564.$0;
  _M0L8_2ahigh1S566 = _M0L7_2abindS564.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS567 = _M0FPB7umul128(_M0L1mS563, _M0L4mul0S561);
  _M0L7_2alow0S568 = _M0L7_2abindS567.$0;
  _M0L8_2ahigh0S569 = _M0L7_2abindS567.$1;
  _M0L3sumS570 = _M0L8_2ahigh0S569 + _M0L7_2alow1S565;
  _M0Lm5high1S571 = _M0L8_2ahigh1S566;
  if (_M0L3sumS570 < _M0L8_2ahigh0S569) {
    uint64_t _M0L6_2atmpS1783 = _M0Lm5high1S571;
    _M0Lm5high1S571 = _M0L6_2atmpS1783 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1793 = _M0FPB8pow5bits(_M0L1iS558);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1794 = _M0FPB8pow5bits(_M0L5base2S559);
  _M0L5deltaS572 = _M0L6_2atmpS1793 - _M0L6_2atmpS1794;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1785
  = _M0FPB13shiftright128(_M0L7_2alow0S568, _M0L3sumS570, _M0L5deltaS572);
  _M0L6_2atmpS1792 = _M0L1iS558 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1789
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1792);
  _M0L6_2atmpS1791 = _M0L1iS558 % 16;
  _M0L6_2atmpS1790 = _M0L6_2atmpS1791 << 1;
  _M0L6_2atmpS1788 = _M0L6_2atmpS1789 >> (_M0L6_2atmpS1790 & 31);
  _M0L6_2atmpS1787 = _M0L6_2atmpS1788 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1786 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1787);
  _M0L1aS573 = _M0L6_2atmpS1785 + _M0L6_2atmpS1786;
  _M0L6_2atmpS1784 = _M0Lm5high1S571;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS574
  = _M0FPB13shiftright128(_M0L3sumS570, _M0L6_2atmpS1784, _M0L5deltaS572);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS573, .$1 = _M0L1bS574};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS531,
  struct _M0TPB8Pow5Pair _M0L3mulS528,
  int32_t _M0L1jS544,
  int32_t _M0L7mmShiftS546
) {
  uint64_t _M0L7_2amul0S527;
  uint64_t _M0L7_2amul1S529;
  uint64_t _M0L1mS530;
  struct _M0TPB7Umul128 _M0L7_2abindS532;
  uint64_t _M0L5_2aloS533;
  uint64_t _M0L6_2atmpS534;
  struct _M0TPB7Umul128 _M0L7_2abindS535;
  uint64_t _M0L6_2alo2S536;
  uint64_t _M0L6_2ahi2S537;
  uint64_t _M0L3midS538;
  uint64_t _M0L6_2atmpS1782;
  uint64_t _M0L2hiS539;
  uint64_t _M0L3lo2S540;
  uint64_t _M0L6_2atmpS1780;
  uint64_t _M0L6_2atmpS1781;
  uint64_t _M0L4mid2S541;
  uint64_t _M0L6_2atmpS1779;
  uint64_t _M0L3hi2S542;
  int32_t _M0L6_2atmpS1778;
  int32_t _M0L6_2atmpS1777;
  uint64_t _M0L2vpS543;
  uint64_t _M0Lm2vmS545;
  int32_t _M0L6_2atmpS1776;
  int32_t _M0L6_2atmpS1775;
  uint64_t _M0L2vrS556;
  uint64_t _M0L6_2atmpS1774;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S527 = _M0L3mulS528.$0;
  _M0L7_2amul1S529 = _M0L3mulS528.$1;
  _M0L1mS530 = _M0L1mS531 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS532 = _M0FPB7umul128(_M0L1mS530, _M0L7_2amul0S527);
  _M0L5_2aloS533 = _M0L7_2abindS532.$0;
  _M0L6_2atmpS534 = _M0L7_2abindS532.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS535 = _M0FPB7umul128(_M0L1mS530, _M0L7_2amul1S529);
  _M0L6_2alo2S536 = _M0L7_2abindS535.$0;
  _M0L6_2ahi2S537 = _M0L7_2abindS535.$1;
  _M0L3midS538 = _M0L6_2atmpS534 + _M0L6_2alo2S536;
  if (_M0L3midS538 < _M0L6_2atmpS534) {
    _M0L6_2atmpS1782 = 1ull;
  } else {
    _M0L6_2atmpS1782 = 0ull;
  }
  _M0L2hiS539 = _M0L6_2ahi2S537 + _M0L6_2atmpS1782;
  _M0L3lo2S540 = _M0L5_2aloS533 + _M0L7_2amul0S527;
  _M0L6_2atmpS1780 = _M0L3midS538 + _M0L7_2amul1S529;
  if (_M0L3lo2S540 < _M0L5_2aloS533) {
    _M0L6_2atmpS1781 = 1ull;
  } else {
    _M0L6_2atmpS1781 = 0ull;
  }
  _M0L4mid2S541 = _M0L6_2atmpS1780 + _M0L6_2atmpS1781;
  if (_M0L4mid2S541 < _M0L3midS538) {
    _M0L6_2atmpS1779 = 1ull;
  } else {
    _M0L6_2atmpS1779 = 0ull;
  }
  _M0L3hi2S542 = _M0L2hiS539 + _M0L6_2atmpS1779;
  _M0L6_2atmpS1778 = _M0L1jS544 - 64;
  _M0L6_2atmpS1777 = _M0L6_2atmpS1778 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS543
  = _M0FPB13shiftright128(_M0L4mid2S541, _M0L3hi2S542, _M0L6_2atmpS1777);
  _M0Lm2vmS545 = 0ull;
  if (_M0L7mmShiftS546) {
    uint64_t _M0L3lo3S547 = _M0L5_2aloS533 - _M0L7_2amul0S527;
    uint64_t _M0L6_2atmpS1764 = _M0L3midS538 - _M0L7_2amul1S529;
    uint64_t _M0L6_2atmpS1765;
    uint64_t _M0L4mid3S548;
    uint64_t _M0L6_2atmpS1763;
    uint64_t _M0L3hi3S549;
    int32_t _M0L6_2atmpS1762;
    int32_t _M0L6_2atmpS1761;
    if (_M0L5_2aloS533 < _M0L3lo3S547) {
      _M0L6_2atmpS1765 = 1ull;
    } else {
      _M0L6_2atmpS1765 = 0ull;
    }
    _M0L4mid3S548 = _M0L6_2atmpS1764 - _M0L6_2atmpS1765;
    if (_M0L3midS538 < _M0L4mid3S548) {
      _M0L6_2atmpS1763 = 1ull;
    } else {
      _M0L6_2atmpS1763 = 0ull;
    }
    _M0L3hi3S549 = _M0L2hiS539 - _M0L6_2atmpS1763;
    _M0L6_2atmpS1762 = _M0L1jS544 - 64;
    _M0L6_2atmpS1761 = _M0L6_2atmpS1762 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS545
    = _M0FPB13shiftright128(_M0L4mid3S548, _M0L3hi3S549, _M0L6_2atmpS1761);
  } else {
    uint64_t _M0L3lo3S550 = _M0L5_2aloS533 + _M0L5_2aloS533;
    uint64_t _M0L6_2atmpS1772 = _M0L3midS538 + _M0L3midS538;
    uint64_t _M0L6_2atmpS1773;
    uint64_t _M0L4mid3S551;
    uint64_t _M0L6_2atmpS1770;
    uint64_t _M0L6_2atmpS1771;
    uint64_t _M0L3hi3S552;
    uint64_t _M0L3lo4S553;
    uint64_t _M0L6_2atmpS1768;
    uint64_t _M0L6_2atmpS1769;
    uint64_t _M0L4mid4S554;
    uint64_t _M0L6_2atmpS1767;
    uint64_t _M0L3hi4S555;
    int32_t _M0L6_2atmpS1766;
    if (_M0L3lo3S550 < _M0L5_2aloS533) {
      _M0L6_2atmpS1773 = 1ull;
    } else {
      _M0L6_2atmpS1773 = 0ull;
    }
    _M0L4mid3S551 = _M0L6_2atmpS1772 + _M0L6_2atmpS1773;
    _M0L6_2atmpS1770 = _M0L2hiS539 + _M0L2hiS539;
    if (_M0L4mid3S551 < _M0L3midS538) {
      _M0L6_2atmpS1771 = 1ull;
    } else {
      _M0L6_2atmpS1771 = 0ull;
    }
    _M0L3hi3S552 = _M0L6_2atmpS1770 + _M0L6_2atmpS1771;
    _M0L3lo4S553 = _M0L3lo3S550 - _M0L7_2amul0S527;
    _M0L6_2atmpS1768 = _M0L4mid3S551 - _M0L7_2amul1S529;
    if (_M0L3lo3S550 < _M0L3lo4S553) {
      _M0L6_2atmpS1769 = 1ull;
    } else {
      _M0L6_2atmpS1769 = 0ull;
    }
    _M0L4mid4S554 = _M0L6_2atmpS1768 - _M0L6_2atmpS1769;
    if (_M0L4mid3S551 < _M0L4mid4S554) {
      _M0L6_2atmpS1767 = 1ull;
    } else {
      _M0L6_2atmpS1767 = 0ull;
    }
    _M0L3hi4S555 = _M0L3hi3S552 - _M0L6_2atmpS1767;
    _M0L6_2atmpS1766 = _M0L1jS544 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS545
    = _M0FPB13shiftright128(_M0L4mid4S554, _M0L3hi4S555, _M0L6_2atmpS1766);
  }
  _M0L6_2atmpS1776 = _M0L1jS544 - 64;
  _M0L6_2atmpS1775 = _M0L6_2atmpS1776 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS556
  = _M0FPB13shiftright128(_M0L3midS538, _M0L2hiS539, _M0L6_2atmpS1775);
  _M0L6_2atmpS1774 = _M0Lm2vmS545;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS556,
                                                .$1 = _M0L2vpS543,
                                                .$2 = _M0L6_2atmpS1774};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS525,
  int32_t _M0L1pS526
) {
  uint64_t _M0L6_2atmpS1760;
  uint64_t _M0L6_2atmpS1759;
  uint64_t _M0L6_2atmpS1758;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1760 = 1ull << (_M0L1pS526 & 63);
  _M0L6_2atmpS1759 = _M0L6_2atmpS1760 - 1ull;
  _M0L6_2atmpS1758 = _M0L5valueS525 & _M0L6_2atmpS1759;
  return _M0L6_2atmpS1758 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS523,
  int32_t _M0L1pS524
) {
  int32_t _M0L6_2atmpS1757;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1757 = _M0FPB10pow5Factor(_M0L5valueS523);
  return _M0L6_2atmpS1757 >= _M0L1pS524;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS518) {
  uint64_t _M0L6_2atmpS1748;
  uint64_t _M0L6_2atmpS1749;
  uint64_t _M0L6_2atmpS1750;
  uint64_t _M0L6_2atmpS1751;
  uint64_t _M0L6_2atmpS1756;
  int32_t _M0L5countS519;
  uint64_t _M0L1vS520;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1748 = _M0L5valueS518 % 5ull;
  if (_M0L6_2atmpS1748 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1749 = _M0L5valueS518 % 25ull;
  if (_M0L6_2atmpS1749 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1750 = _M0L5valueS518 % 125ull;
  if (_M0L6_2atmpS1750 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1751 = _M0L5valueS518 % 625ull;
  if (_M0L6_2atmpS1751 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1756 = _M0L5valueS518 / 625ull;
  _M0L5countS519 = 4;
  _M0L1vS520 = _M0L6_2atmpS1756;
  while (1) {
    if (_M0L1vS520 > 0ull) {
      uint64_t _M0L6_2atmpS1752 = _M0L1vS520 % 5ull;
      int32_t _M0L6_2atmpS1753;
      uint64_t _M0L6_2atmpS1754;
      if (_M0L6_2atmpS1752 != 0ull) {
        return _M0L5countS519;
      }
      _M0L6_2atmpS1753 = _M0L5countS519 + 1;
      _M0L6_2atmpS1754 = _M0L1vS520 / 5ull;
      _M0L5countS519 = _M0L6_2atmpS1753;
      _M0L1vS520 = _M0L6_2atmpS1754;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS522;
      moonbit_string_t _M0L6_2atmpS1755;
      int32_t _result_3295;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS522
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS522, (moonbit_string_t)moonbit_string_literal_17.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS522, _M0L5valueS518);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1755
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS522);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS522);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_3295 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1755);
      moonbit_decref_cycle_free(_M0L6_2atmpS1755);
      return _result_3295;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS517,
  uint64_t _M0L2hiS515,
  int32_t _M0L4distS516
) {
  int32_t _M0L6_2atmpS1747;
  uint64_t _M0L6_2atmpS1745;
  uint64_t _M0L6_2atmpS1746;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1747 = 64 - _M0L4distS516;
  _M0L6_2atmpS1745 = _M0L2hiS515 << (_M0L6_2atmpS1747 & 63);
  _M0L6_2atmpS1746 = _M0L2loS517 >> (_M0L4distS516 & 63);
  return _M0L6_2atmpS1745 | _M0L6_2atmpS1746;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS505,
  uint64_t _M0L1bS508
) {
  uint64_t _M0L3aLoS504;
  uint64_t _M0L3aHiS506;
  uint64_t _M0L3bLoS507;
  uint64_t _M0L3bHiS509;
  uint64_t _M0L1xS510;
  uint64_t _M0L6_2atmpS1743;
  uint64_t _M0L6_2atmpS1744;
  uint64_t _M0L1yS511;
  uint64_t _M0L6_2atmpS1741;
  uint64_t _M0L6_2atmpS1742;
  uint64_t _M0L1zS512;
  uint64_t _M0L6_2atmpS1739;
  uint64_t _M0L6_2atmpS1740;
  uint64_t _M0L6_2atmpS1737;
  uint64_t _M0L6_2atmpS1738;
  uint64_t _M0L1wS513;
  uint64_t _M0L2loS514;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS504 = _M0L1aS505 & 4294967295ull;
  _M0L3aHiS506 = _M0L1aS505 >> 32;
  _M0L3bLoS507 = _M0L1bS508 & 4294967295ull;
  _M0L3bHiS509 = _M0L1bS508 >> 32;
  _M0L1xS510 = _M0L3aLoS504 * _M0L3bLoS507;
  _M0L6_2atmpS1743 = _M0L3aHiS506 * _M0L3bLoS507;
  _M0L6_2atmpS1744 = _M0L1xS510 >> 32;
  _M0L1yS511 = _M0L6_2atmpS1743 + _M0L6_2atmpS1744;
  _M0L6_2atmpS1741 = _M0L3aLoS504 * _M0L3bHiS509;
  _M0L6_2atmpS1742 = _M0L1yS511 & 4294967295ull;
  _M0L1zS512 = _M0L6_2atmpS1741 + _M0L6_2atmpS1742;
  _M0L6_2atmpS1739 = _M0L3aHiS506 * _M0L3bHiS509;
  _M0L6_2atmpS1740 = _M0L1yS511 >> 32;
  _M0L6_2atmpS1737 = _M0L6_2atmpS1739 + _M0L6_2atmpS1740;
  _M0L6_2atmpS1738 = _M0L1zS512 >> 32;
  _M0L1wS513 = _M0L6_2atmpS1737 + _M0L6_2atmpS1738;
  _M0L2loS514 = _M0L1aS505 * _M0L1bS508;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS514, .$1 = _M0L1wS513};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS502,
  int32_t _M0L4fromS499,
  int32_t _M0L2toS498
) {
  int32_t _M0L3lenS497;
  int32_t _M0L6_2atmpS1736;
  uint16_t* _M0L6bufferS500;
  int32_t _M0L1iS501;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS497 = _M0L2toS498 - _M0L4fromS499;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1736 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS500
  = (uint16_t*)moonbit_make_string(_M0L3lenS497, _M0L6_2atmpS1736);
  _M0L1iS501 = 0;
  while (1) {
    if (_M0L1iS501 < _M0L3lenS497) {
      int32_t _M0L6_2atmpS1734 = _M0L4fromS499 + _M0L1iS501;
      int32_t _M0L6_2atmpS1733;
      int32_t _M0L6_2atmpS1732;
      int32_t _M0L6_2atmpS1735;
      if (
        _M0L6_2atmpS1734 < 0
        || _M0L6_2atmpS1734 >= Moonbit_array_length(_M0L5bytesS502)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1733 = (int32_t)_M0L5bytesS502[_M0L6_2atmpS1734];
      _M0L6_2atmpS1732 = (uint16_t)_M0L6_2atmpS1733;
      if (
        _M0L1iS501 < 0 || _M0L1iS501 >= Moonbit_array_length(_M0L6bufferS500)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS500[_M0L1iS501] = _M0L6_2atmpS1732;
      _M0L6_2atmpS1735 = _M0L1iS501 + 1;
      _M0L1iS501 = _M0L6_2atmpS1735;
      continue;
    }
    break;
  }
  return _M0L6bufferS500;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS496) {
  int32_t _M0L6_2atmpS1731;
  uint32_t _M0L6_2atmpS1730;
  uint32_t _M0L6_2atmpS1729;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1731 = _M0L1eS496 * 78913;
  _M0L6_2atmpS1730 = *(uint32_t*)&_M0L6_2atmpS1731;
  _M0L6_2atmpS1729 = _M0L6_2atmpS1730 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1729;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS495) {
  int32_t _M0L6_2atmpS1728;
  uint32_t _M0L6_2atmpS1727;
  uint32_t _M0L6_2atmpS1726;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1728 = _M0L1eS495 * 732923;
  _M0L6_2atmpS1727 = *(uint32_t*)&_M0L6_2atmpS1728;
  _M0L6_2atmpS1726 = _M0L6_2atmpS1727 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1726;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS493,
  int32_t _M0L8exponentS494,
  int32_t _M0L8mantissaS491
) {
  moonbit_string_t _M0L1sS492;
  moonbit_string_t _result_3298;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS491) {
    return (moonbit_string_t)moonbit_string_literal_18.data;
  }
  if (_M0L4signS493) {
    _M0L1sS492 = (moonbit_string_t)moonbit_string_literal_19.data;
  } else {
    _M0L1sS492 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS494) {
    moonbit_string_t _result_3297;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_3297
    = moonbit_add_string(_M0L1sS492, (moonbit_string_t)moonbit_string_literal_20.data);
    moonbit_decref_cycle_free(_M0L1sS492);
    return _result_3297;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_3298
  = moonbit_add_string(_M0L1sS492, (moonbit_string_t)moonbit_string_literal_21.data);
  moonbit_decref_cycle_free(_M0L1sS492);
  return _result_3298;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS490) {
  int32_t _M0L6_2atmpS1725;
  uint32_t _M0L6_2atmpS1724;
  uint32_t _M0L6_2atmpS1723;
  int32_t _M0L6_2atmpS1722;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1725 = _M0L1eS490 * 1217359;
  _M0L6_2atmpS1724 = *(uint32_t*)&_M0L6_2atmpS1725;
  _M0L6_2atmpS1723 = _M0L6_2atmpS1724 >> 19;
  _M0L6_2atmpS1722 = *(int32_t*)&_M0L6_2atmpS1723;
  return _M0L6_2atmpS1722 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS489) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS489 != _M0L4selfS489) {
    return 0;
  } else if (_M0L4selfS489 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS489 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS489;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS488) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS488 != _M0L4selfS488) {
    return 0ll;
  } else if (_M0L4selfS488 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS488 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS488;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS485
) {
  float* _M0L6_2atmpS1719;
  struct _M0TPB5ArrayGfE* _block_3299;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1719 = (float*)moonbit_make_float_array_raw(_M0L3lenS485);
  _block_3299
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_3299)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 124, 0);
  _block_3299->$0 = _M0L6_2atmpS1719;
  _block_3299->$1 = _M0L3lenS485;
  return _block_3299;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS486
) {
  uint8_t* _M0L6_2atmpS1720;
  struct _M0TPB5ArrayGbE* _block_3300;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1720 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS486);
  _block_3300
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_3300)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 98, 0);
  _block_3300->$0 = _M0L6_2atmpS1720;
  _block_3300->$1 = _M0L3lenS486;
  return _block_3300;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS487
) {
  int32_t* _M0L6_2atmpS1721;
  struct _M0TPB5ArrayGiE* _block_3301;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1721 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS487);
  _block_3301
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_3301)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 127, 0);
  _block_3301->$0 = _M0L6_2atmpS1721;
  _block_3301->$1 = _M0L3lenS487;
  return _block_3301;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS481,
  int32_t _M0L5indexS482
) {
  uint64_t* _M0L6_2atmpS1717;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1717 = _M0L4selfS481;
  if (
    _M0L5indexS482 < 0
    || _M0L5indexS482 >= Moonbit_array_length(_M0L6_2atmpS1717)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1717[_M0L5indexS482];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS483,
  int32_t _M0L5indexS484
) {
  uint32_t* _M0L6_2atmpS1718;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1718 = _M0L4selfS483;
  if (
    _M0L5indexS484 < 0
    || _M0L5indexS484 >= Moonbit_array_length(_M0L6_2atmpS1718)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1718[_M0L5indexS484];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS480
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS480, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS479) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS479, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS478) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS478;
}

int32_t _M0MPC15array5Array4pushGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS469,
  int32_t _M0L5valueS471
) {
  int32_t _M0L3lenS1696;
  uint8_t* _M0L6_2atmpS1698;
  int32_t _M0L6_2atmpS1697;
  int32_t _M0L6lengthS470;
  uint8_t* _M0L3bufS1701;
  int32_t _M0L6_2atmpS1702;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1696 = _M0L4selfS469->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1698 = _M0MPC15array5Array6bufferGbE(_M0L4selfS469);
  _M0L6_2atmpS1697 = Moonbit_array_length(_M0L6_2atmpS1698);
  moonbit_decref_cycle_free(_M0L6_2atmpS1698);
  if (_M0L3lenS1696 == _M0L6_2atmpS1697) {
    int32_t _M0L3lenS1700 = _M0L4selfS469->$1;
    int32_t _M0L6_2atmpS1699 = _M0L3lenS1700 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGbE(_M0L4selfS469, _M0L6_2atmpS1699);
  }
  _M0L6lengthS470 = _M0L4selfS469->$1;
  _M0L3bufS1701 = _M0L4selfS469->$0;
  _M0L3bufS1701[_M0L6lengthS470] = _M0L5valueS471;
  _M0L6_2atmpS1702 = _M0L6lengthS470 + 1;
  _M0L4selfS469->$1 = _M0L6_2atmpS1702;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS472,
  moonbit_string_t _M0L5valueS474
) {
  int32_t _M0L3lenS1703;
  moonbit_string_t* _M0L6_2atmpS1705;
  int32_t _M0L6_2atmpS1704;
  int32_t _M0L6lengthS473;
  moonbit_string_t* _M0L3bufS1708;
  moonbit_string_t _M0L6_2aoldS3176;
  int32_t _M0L6_2atmpS1709;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1703 = _M0L4selfS472->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1705 = _M0MPC15array5Array6bufferGsE(_M0L4selfS472);
  _M0L6_2atmpS1704 = Moonbit_array_length(_M0L6_2atmpS1705);
  moonbit_decref_cycle_free(_M0L6_2atmpS1705);
  if (_M0L3lenS1703 == _M0L6_2atmpS1704) {
    int32_t _M0L3lenS1707 = _M0L4selfS472->$1;
    int32_t _M0L6_2atmpS1706 = _M0L3lenS1707 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS472, _M0L6_2atmpS1706);
  }
  _M0L6lengthS473 = _M0L4selfS472->$1;
  _M0L3bufS1708 = _M0L4selfS472->$0;
  _M0L6_2aoldS3176 = (moonbit_string_t)_M0L3bufS1708[_M0L6lengthS473];
  moonbit_decref_cycle_free(_M0L6_2aoldS3176);
  _M0L3bufS1708[_M0L6lengthS473] = _M0L5valueS474;
  _M0L6_2atmpS1709 = _M0L6lengthS473 + 1;
  _M0L4selfS472->$1 = _M0L6_2atmpS1709;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS475,
  struct _M0TUsiE* _M0L5valueS477
) {
  int32_t _M0L3lenS1710;
  struct _M0TUsiE** _M0L6_2atmpS1712;
  int32_t _M0L6_2atmpS1711;
  int32_t _M0L6lengthS476;
  struct _M0TUsiE** _M0L3bufS1715;
  struct _M0TUsiE* _M0L6_2aoldS3177;
  int32_t _M0L6_2atmpS1716;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1710 = _M0L4selfS475->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1712 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS475);
  _M0L6_2atmpS1711 = Moonbit_array_length(_M0L6_2atmpS1712);
  moonbit_decref_cycle_free(_M0L6_2atmpS1712);
  if (_M0L3lenS1710 == _M0L6_2atmpS1711) {
    int32_t _M0L3lenS1714 = _M0L4selfS475->$1;
    int32_t _M0L6_2atmpS1713 = _M0L3lenS1714 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS475, _M0L6_2atmpS1713);
  }
  _M0L6lengthS476 = _M0L4selfS475->$1;
  _M0L3bufS1715 = _M0L4selfS475->$0;
  _M0L6_2aoldS3177 = (struct _M0TUsiE*)_M0L3bufS1715[_M0L6lengthS476];
  if (_M0L6_2aoldS3177) {
    moonbit_decref_cycle_free(_M0L6_2aoldS3177);
  }
  _M0L3bufS1715[_M0L6lengthS476] = _M0L5valueS477;
  _M0L6_2atmpS1716 = _M0L6lengthS476 + 1;
  _M0L4selfS475->$1 = _M0L6_2atmpS1716;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS458,
  int32_t _M0L8requiredS460
) {
  int32_t _M0L8old__capS457;
  int32_t _M0L3lenS1693;
  int32_t _M0L8new__capS459;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS457 = _M0MPC15array5Array8capacityGbE(_M0L4selfS458);
  _M0L3lenS1693 = _M0L4selfS458->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS459
  = _M0FPB23array__growth__capacity(_M0L8old__capS457, _M0L3lenS1693, _M0L8requiredS460);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGbE(_M0L4selfS458, _M0L8new__capS459);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS462,
  int32_t _M0L8requiredS464
) {
  int32_t _M0L8old__capS461;
  int32_t _M0L3lenS1694;
  int32_t _M0L8new__capS463;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS461 = _M0MPC15array5Array8capacityGsE(_M0L4selfS462);
  _M0L3lenS1694 = _M0L4selfS462->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS463
  = _M0FPB23array__growth__capacity(_M0L8old__capS461, _M0L3lenS1694, _M0L8requiredS464);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS462, _M0L8new__capS463);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS466,
  int32_t _M0L8requiredS468
) {
  int32_t _M0L8old__capS465;
  int32_t _M0L3lenS1695;
  int32_t _M0L8new__capS467;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS465 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS466);
  _M0L3lenS1695 = _M0L4selfS466->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS467
  = _M0FPB23array__growth__capacity(_M0L8old__capS465, _M0L3lenS1695, _M0L8requiredS468);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS466, _M0L8new__capS467);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS440,
  int32_t _M0L13new__capacityS443
) {
  uint8_t* _M0L8old__bufS439;
  int32_t _M0L3lenS441;
  int32_t _M0L9copy__lenS442;
  uint8_t* _M0L8new__bufS444;
  uint8_t* _M0L6_2aoldS3178;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS439 = _M0L4selfS440->$0;
  _M0L3lenS441 = _M0L4selfS440->$1;
  if (_M0L3lenS441 < _M0L13new__capacityS443) {
    _M0L9copy__lenS442 = _M0L3lenS441;
  } else {
    _M0L9copy__lenS442 = _M0L13new__capacityS443;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS439);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS444
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGbE(_M0L8old__bufS439, _M0L13new__capacityS443, _M0L9copy__lenS442, 0, 0);
  _M0L6_2aoldS3178 = _M0L4selfS440->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS3178);
  _M0L4selfS440->$0 = _M0L8new__bufS444;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS446,
  int32_t _M0L13new__capacityS449
) {
  moonbit_string_t* _M0L8old__bufS445;
  int32_t _M0L3lenS447;
  int32_t _M0L9copy__lenS448;
  moonbit_string_t* _M0L8new__bufS450;
  moonbit_string_t* _M0L6_2aoldS3179;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS445 = _M0L4selfS446->$0;
  _M0L3lenS447 = _M0L4selfS446->$1;
  if (_M0L3lenS447 < _M0L13new__capacityS449) {
    _M0L9copy__lenS448 = _M0L3lenS447;
  } else {
    _M0L9copy__lenS448 = _M0L13new__capacityS449;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS445);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS450
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS445, _M0L13new__capacityS449, _M0L9copy__lenS448, 0, 0);
  _M0L6_2aoldS3179 = _M0L4selfS446->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS3179);
  _M0L4selfS446->$0 = _M0L8new__bufS450;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS452,
  int32_t _M0L13new__capacityS455
) {
  struct _M0TUsiE** _M0L8old__bufS451;
  int32_t _M0L3lenS453;
  int32_t _M0L9copy__lenS454;
  struct _M0TUsiE** _M0L8new__bufS456;
  struct _M0TUsiE** _M0L6_2aoldS3180;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS451 = _M0L4selfS452->$0;
  _M0L3lenS453 = _M0L4selfS452->$1;
  if (_M0L3lenS453 < _M0L13new__capacityS455) {
    _M0L9copy__lenS454 = _M0L3lenS453;
  } else {
    _M0L9copy__lenS454 = _M0L13new__capacityS455;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS451);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS456
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS451, _M0L13new__capacityS455, _M0L9copy__lenS454, 0, 0);
  _M0L6_2aoldS3180 = _M0L4selfS452->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS3180);
  _M0L4selfS452->$0 = _M0L8new__bufS456;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS436
) {
  uint8_t* _M0L6_2atmpS1690;
  int32_t _result_3302;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1690 = _M0MPC15array5Array6bufferGbE(_M0L4selfS436);
  _result_3302 = Moonbit_array_length(_M0L6_2atmpS1690);
  moonbit_decref_cycle_free(_M0L6_2atmpS1690);
  return _result_3302;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS437
) {
  moonbit_string_t* _M0L6_2atmpS1691;
  int32_t _result_3303;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1691 = _M0MPC15array5Array6bufferGsE(_M0L4selfS437);
  _result_3303 = Moonbit_array_length(_M0L6_2atmpS1691);
  moonbit_decref_cycle_free(_M0L6_2atmpS1691);
  return _result_3303;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS438
) {
  struct _M0TUsiE** _M0L6_2atmpS1692;
  int32_t _result_3304;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1692 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS438);
  _result_3304 = Moonbit_array_length(_M0L6_2atmpS1692);
  moonbit_decref_cycle_free(_M0L6_2atmpS1692);
  return _result_3304;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS432,
  int32_t _M0L3lenS430,
  int32_t _M0L8requiredS429
) {
  int32_t _M0L5startS431;
  int32_t _M0L5spaceS433;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS429 < _M0L3lenS430) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_22.data);
  }
  if (_M0L7currentS432 == 0) {
    _M0L5startS431 = 8;
  } else {
    _M0L5startS431 = _M0L7currentS432;
  }
  _M0L5spaceS433 = _M0L5startS431;
  while (1) {
    if (_M0L5spaceS433 < _M0L8requiredS429) {
      int32_t _M0L4nextS434 = _M0L5spaceS433 * 2;
      if (_M0L4nextS434 <= _M0L5spaceS433) {
        return _M0L8requiredS429;
      }
      _M0L5spaceS433 = _M0L4nextS434;
      continue;
    } else {
      return _M0L5spaceS433;
    }
    break;
  }
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS424) {
  uint8_t* _M0L8_2afieldS3181;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3181 = _M0L4selfS424->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3181);
  return _M0L8_2afieldS3181;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS425) {
  float* _M0L8_2afieldS3182;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3182 = _M0L4selfS425->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3182);
  return _M0L8_2afieldS3182;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS426) {
  int32_t* _M0L8_2afieldS3183;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3183 = _M0L4selfS426->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3183);
  return _M0L8_2afieldS3183;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS427
) {
  moonbit_string_t* _M0L8_2afieldS3184;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3184 = _M0L4selfS427->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3184);
  return _M0L8_2afieldS3184;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS428
) {
  struct _M0TUsiE** _M0L8_2afieldS3185;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS3185 = _M0L4selfS428->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3185);
  return _M0L8_2afieldS3185;
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
  int32_t _M0L3endS1688;
  int32_t _M0L5startS1689;
  int32_t _M0L8str__lenS419;
  int32_t _M0L3lenS1687;
  int32_t _M0L8requiredS421;
  uint16_t* _M0L4dataS1680;
  int32_t _M0L6_2atmpS1679;
  int32_t _if__result_3306;
  uint16_t* _M0L4dataS1681;
  int32_t _M0L3lenS1682;
  moonbit_string_t _M0L6_2atmpS1683;
  int32_t _M0L6_2atmpS1684;
  int32_t _M0L3lenS1686;
  int32_t _M0L6_2atmpS1685;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1688 = _M0L3strS420.$2;
  _M0L5startS1689 = _M0L3strS420.$1;
  _M0L8str__lenS419 = _M0L3endS1688 - _M0L5startS1689;
  if (_M0L8str__lenS419 == 0) {
    return 0;
  }
  _M0L3lenS1687 = _M0L4selfS422->$1;
  _M0L8requiredS421 = _M0L3lenS1687 + _M0L8str__lenS419;
  _M0L4dataS1680 = _M0L4selfS422->$0;
  _M0L6_2atmpS1679 = Moonbit_array_length(_M0L4dataS1680);
  if (_M0L8requiredS421 > _M0L6_2atmpS1679) {
    _if__result_3306 = 1;
  } else {
    int32_t _M0L3lenS1678 = _M0L4selfS422->$1;
    _if__result_3306 = _M0L8requiredS421 < _M0L3lenS1678;
  }
  if (_if__result_3306) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS422, _M0L8requiredS421);
  }
  _M0L4dataS1681 = _M0L4selfS422->$0;
  _M0L3lenS1682 = _M0L4selfS422->$1;
  moonbit_incref_cycle_free(_M0L4dataS1681);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1683 = _M0MPC16string10StringView4data(_M0L3strS420);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1684 = _M0MPC16string10StringView13start__offset(_M0L3strS420);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1681, _M0L3lenS1682, _M0L6_2atmpS1683, _M0L6_2atmpS1684, _M0L8str__lenS419);
  moonbit_decref_cycle_free(_M0L4dataS1681);
  moonbit_decref_cycle_free(_M0L6_2atmpS1683);
  _M0L3lenS1686 = _M0L4selfS422->$1;
  _M0L6_2atmpS1685 = _M0L3lenS1686 + _M0L8str__lenS419;
  _M0L4selfS422->$1 = _M0L6_2atmpS1685;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS416,
  int32_t _M0L5startS414,
  int32_t _M0L3endS415
) {
  int32_t _if__result_3307;
  int32_t _M0L3lenS417;
  int32_t _M0L6_2atmpS1677;
  moonbit_bytes_t _M0L5bytesS418;
  moonbit_bytes_t _M0L6_2atmpS1676;
  moonbit_string_t _result_3308;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS414 == 0) {
    int32_t _M0L6_2atmpS1675 = Moonbit_array_length(_M0L3strS416);
    _if__result_3307 = _M0L3endS415 == _M0L6_2atmpS1675;
  } else {
    _if__result_3307 = 0;
  }
  if (_if__result_3307) {
    moonbit_incref_cycle_free(_M0L3strS416);
    return _M0L3strS416;
  }
  _M0L3lenS417 = _M0L3endS415 - _M0L5startS414;
  _M0L6_2atmpS1677 = _M0L3lenS417 * 2;
  _M0L5bytesS418 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1677, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS418, 0, _M0L3strS416, _M0L5startS414, _M0L3lenS417);
  _M0L6_2atmpS1676 = _M0L5bytesS418;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_3308
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1676, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1676);
  return _result_3308;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS409,
  int32_t _M0L6offsetS413,
  int64_t _M0L6lengthS411
) {
  int32_t _M0L3lenS408;
  int32_t _M0L6lengthS410;
  int32_t _if__result_3309;
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
      int32_t _M0L6_2atmpS1674 = _M0L6offsetS413 + _M0L6lengthS410;
      _if__result_3309 = _M0L6_2atmpS1674 <= _M0L3lenS408;
    } else {
      _if__result_3309 = 0;
    }
  } else {
    _if__result_3309 = 0;
  }
  if (_if__result_3309) {
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
  int32_t _M0L6_2atmpS1673;
  int32_t _M0L6_2atmpS1672;
  int32_t _M0L2e1S394;
  int32_t _M0L6_2atmpS1671;
  int32_t _M0L2e2S397;
  int32_t _M0L4len1S399;
  int32_t _M0L4len2S401;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1673 = _M0L6lengthS396 * 2;
  _M0L6_2atmpS1672 = _M0L13bytes__offsetS395 + _M0L6_2atmpS1673;
  _M0L2e1S394 = _M0L6_2atmpS1672 - 1;
  _M0L6_2atmpS1671 = _M0L11str__offsetS398 + _M0L6lengthS396;
  _M0L2e2S397 = _M0L6_2atmpS1671 - 1;
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
        int32_t _M0L6_2atmpS1668 = _M0L3strS402[_M0L1iS404];
        int32_t _M0L6_2atmpS1667 = (int32_t)_M0L6_2atmpS1668;
        uint32_t _M0L1cS406 = *(uint32_t*)&_M0L6_2atmpS1667;
        uint32_t _M0L6_2atmpS1663 = _M0L1cS406 & 255u;
        int32_t _M0L6_2atmpS1662;
        int32_t _M0L6_2atmpS1664;
        uint32_t _M0L6_2atmpS1666;
        int32_t _M0L6_2atmpS1665;
        int32_t _M0L6_2atmpS1669;
        int32_t _M0L6_2atmpS1670;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1662 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1663);
        if (
          _M0L1jS405 < 0 || _M0L1jS405 >= Moonbit_array_length(_M0L4selfS400)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS400[_M0L1jS405] = _M0L6_2atmpS1662;
        _M0L6_2atmpS1664 = _M0L1jS405 + 1;
        _M0L6_2atmpS1666 = _M0L1cS406 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1665 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1666);
        if (
          _M0L6_2atmpS1664 < 0
          || _M0L6_2atmpS1664 >= Moonbit_array_length(_M0L4selfS400)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS400[_M0L6_2atmpS1664] = _M0L6_2atmpS1665;
        _M0L6_2atmpS1669 = _M0L1iS404 + 1;
        _M0L6_2atmpS1670 = _M0L1jS405 + 2;
        _M0L1iS404 = _M0L6_2atmpS1669;
        _M0L1jS405 = _M0L6_2atmpS1670;
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
  int32_t _M0L6_2atmpS1661;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1661 = *(int32_t*)&_M0L4selfS393;
  return _M0L6_2atmpS1661 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS385,
  int32_t _M0L5radixS384
) {
  uint16_t* _M0L6bufferS386;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS384 < 2 || _M0L5radixS384 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_23.data);
  }
  if (_M0L4selfS385 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_16.data;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_23.data);
  }
  if (_M0L4selfS368 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_16.data;
  }
  _M0L12is__negativeS369 = _M0L4selfS368 < 0ll;
  if (_M0L12is__negativeS369) {
    int64_t _M0L6_2atmpS1660 = -_M0L4selfS368;
    _M0L3numS370 = *(uint64_t*)&_M0L6_2atmpS1660;
  } else {
    _M0L3numS370 = *(uint64_t*)&_M0L4selfS368;
  }
  switch (_M0L5radixS367) {
    case 10: {
      int32_t _M0L10digit__lenS372;
      int32_t _M0L6_2atmpS1657;
      int32_t _M0L10total__lenS373;
      uint16_t* _M0L6bufferS374;
      int32_t _M0L12digit__startS375;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS372 = _M0FPB12dec__count64(_M0L3numS370);
      if (_M0L12is__negativeS369) {
        _M0L6_2atmpS1657 = 1;
      } else {
        _M0L6_2atmpS1657 = 0;
      }
      _M0L10total__lenS373 = _M0L10digit__lenS372 + _M0L6_2atmpS1657;
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
      int32_t _M0L6_2atmpS1658;
      int32_t _M0L10total__lenS377;
      uint16_t* _M0L6bufferS378;
      int32_t _M0L12digit__startS379;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS376 = _M0FPB12hex__count64(_M0L3numS370);
      if (_M0L12is__negativeS369) {
        _M0L6_2atmpS1658 = 1;
      } else {
        _M0L6_2atmpS1658 = 0;
      }
      _M0L10total__lenS377 = _M0L10digit__lenS376 + _M0L6_2atmpS1658;
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
      int32_t _M0L6_2atmpS1659;
      int32_t _M0L10total__lenS381;
      uint16_t* _M0L6bufferS382;
      int32_t _M0L12digit__startS383;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS380
      = _M0FPB14radix__count64(_M0L3numS370, _M0L5radixS367);
      if (_M0L12is__negativeS369) {
        _M0L6_2atmpS1659 = 1;
      } else {
        _M0L6_2atmpS1659 = 0;
      }
      _M0L10total__lenS381 = _M0L10digit__lenS380 + _M0L6_2atmpS1659;
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
  int32_t _M0L6_2atmpS1656;
  uint64_t _M0L3numS343;
  int32_t _M0L6offsetS344;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1656 = _M0L10total__lenS366 - _M0L12digit__startS354;
  _M0L3numS343 = _M0L3numS365;
  _M0L6offsetS344 = _M0L6_2atmpS1656;
  while (1) {
    if (_M0L3numS343 >= 10000ull) {
      uint64_t _M0L1tS345 = _M0L3numS343 / 10000ull;
      uint64_t _M0L6_2atmpS1633 = _M0L3numS343 % 10000ull;
      int32_t _M0L1rS346 = (int32_t)_M0L6_2atmpS1633;
      int32_t _M0L2d1S347 = _M0L1rS346 / 100;
      int32_t _M0L2d2S348 = _M0L1rS346 % 100;
      int32_t _M0L6_2atmpS1632 = _M0L2d1S347 / 10;
      int32_t _M0L6_2atmpS1631 = 48 + _M0L6_2atmpS1632;
      int32_t _M0L6d1__hiS349 = (uint16_t)_M0L6_2atmpS1631;
      int32_t _M0L6_2atmpS1630 = _M0L2d1S347 % 10;
      int32_t _M0L6_2atmpS1629 = 48 + _M0L6_2atmpS1630;
      int32_t _M0L6d1__loS350 = (uint16_t)_M0L6_2atmpS1629;
      int32_t _M0L6_2atmpS1628 = _M0L2d2S348 / 10;
      int32_t _M0L6_2atmpS1627 = 48 + _M0L6_2atmpS1628;
      int32_t _M0L6d2__hiS351 = (uint16_t)_M0L6_2atmpS1627;
      int32_t _M0L6_2atmpS1626 = _M0L2d2S348 % 10;
      int32_t _M0L6_2atmpS1625 = 48 + _M0L6_2atmpS1626;
      int32_t _M0L6d2__loS352 = (uint16_t)_M0L6_2atmpS1625;
      int32_t _M0L6_2atmpS1617 = _M0L12digit__startS354 + _M0L6offsetS344;
      int32_t _M0L6_2atmpS1616 = _M0L6_2atmpS1617 - 4;
      int32_t _M0L6_2atmpS1619;
      int32_t _M0L6_2atmpS1618;
      int32_t _M0L6_2atmpS1621;
      int32_t _M0L6_2atmpS1620;
      int32_t _M0L6_2atmpS1623;
      int32_t _M0L6_2atmpS1622;
      int32_t _M0L6_2atmpS1624;
      _M0L6bufferS353[_M0L6_2atmpS1616] = _M0L6d1__hiS349;
      _M0L6_2atmpS1619 = _M0L12digit__startS354 + _M0L6offsetS344;
      _M0L6_2atmpS1618 = _M0L6_2atmpS1619 - 3;
      _M0L6bufferS353[_M0L6_2atmpS1618] = _M0L6d1__loS350;
      _M0L6_2atmpS1621 = _M0L12digit__startS354 + _M0L6offsetS344;
      _M0L6_2atmpS1620 = _M0L6_2atmpS1621 - 2;
      _M0L6bufferS353[_M0L6_2atmpS1620] = _M0L6d2__hiS351;
      _M0L6_2atmpS1623 = _M0L12digit__startS354 + _M0L6offsetS344;
      _M0L6_2atmpS1622 = _M0L6_2atmpS1623 - 1;
      _M0L6bufferS353[_M0L6_2atmpS1622] = _M0L6d2__loS352;
      _M0L6_2atmpS1624 = _M0L6offsetS344 - 4;
      _M0L3numS343 = _M0L1tS345;
      _M0L6offsetS344 = _M0L6_2atmpS1624;
      continue;
    } else {
      int32_t _M0L6_2atmpS1655 = (int32_t)_M0L3numS343;
      int32_t _M0L9remainingS356 = _M0L6_2atmpS1655;
      int32_t _M0L6offsetS357 = _M0L6offsetS344;
      while (1) {
        if (_M0L9remainingS356 >= 100) {
          int32_t _M0L1tS358 = _M0L9remainingS356 / 100;
          int32_t _M0L1dS359 = _M0L9remainingS356 % 100;
          int32_t _M0L6_2atmpS1642 = _M0L1dS359 / 10;
          int32_t _M0L6_2atmpS1641 = 48 + _M0L6_2atmpS1642;
          int32_t _M0L5d__hiS360 = (uint16_t)_M0L6_2atmpS1641;
          int32_t _M0L6_2atmpS1640 = _M0L1dS359 % 10;
          int32_t _M0L6_2atmpS1639 = 48 + _M0L6_2atmpS1640;
          int32_t _M0L5d__loS361 = (uint16_t)_M0L6_2atmpS1639;
          int32_t _M0L6_2atmpS1635 = _M0L12digit__startS354 + _M0L6offsetS357;
          int32_t _M0L6_2atmpS1634 = _M0L6_2atmpS1635 - 2;
          int32_t _M0L6_2atmpS1637;
          int32_t _M0L6_2atmpS1636;
          int32_t _M0L6_2atmpS1638;
          _M0L6bufferS353[_M0L6_2atmpS1634] = _M0L5d__hiS360;
          _M0L6_2atmpS1637 = _M0L12digit__startS354 + _M0L6offsetS357;
          _M0L6_2atmpS1636 = _M0L6_2atmpS1637 - 1;
          _M0L6bufferS353[_M0L6_2atmpS1636] = _M0L5d__loS361;
          _M0L6_2atmpS1638 = _M0L6offsetS357 - 2;
          _M0L9remainingS356 = _M0L1tS358;
          _M0L6offsetS357 = _M0L6_2atmpS1638;
          continue;
        } else if (_M0L9remainingS356 >= 10) {
          int32_t _M0L6_2atmpS1650 = _M0L9remainingS356 / 10;
          int32_t _M0L6_2atmpS1649 = 48 + _M0L6_2atmpS1650;
          int32_t _M0L5d__hiS363 = (uint16_t)_M0L6_2atmpS1649;
          int32_t _M0L6_2atmpS1648 = _M0L9remainingS356 % 10;
          int32_t _M0L6_2atmpS1647 = 48 + _M0L6_2atmpS1648;
          int32_t _M0L5d__loS364 = (uint16_t)_M0L6_2atmpS1647;
          int32_t _M0L6_2atmpS1644 = _M0L12digit__startS354 + _M0L6offsetS357;
          int32_t _M0L6_2atmpS1643 = _M0L6_2atmpS1644 - 2;
          int32_t _M0L6_2atmpS1646;
          int32_t _M0L6_2atmpS1645;
          _M0L6bufferS353[_M0L6_2atmpS1643] = _M0L5d__hiS363;
          _M0L6_2atmpS1646 = _M0L12digit__startS354 + _M0L6offsetS357;
          _M0L6_2atmpS1645 = _M0L6_2atmpS1646 - 1;
          _M0L6bufferS353[_M0L6_2atmpS1645] = _M0L5d__loS364;
        } else {
          int32_t _M0L6_2atmpS1654 = _M0L12digit__startS354 + _M0L6offsetS357;
          int32_t _M0L6_2atmpS1651 = _M0L6_2atmpS1654 - 1;
          int32_t _M0L6_2atmpS1653 = 48 + _M0L9remainingS356;
          int32_t _M0L6_2atmpS1652 = (uint16_t)_M0L6_2atmpS1653;
          _M0L6bufferS353[_M0L6_2atmpS1651] = _M0L6_2atmpS1652;
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
  int32_t _M0L6_2atmpS1601;
  int32_t _M0L6_2atmpS1600;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS326 = _M0MPC13int3Int10to__uint64(_M0L5radixS327);
  _M0L6_2atmpS1601 = _M0L5radixS327 - 1;
  _M0L6_2atmpS1600 = _M0L5radixS327 & _M0L6_2atmpS1601;
  if (_M0L6_2atmpS1600 == 0) {
    int32_t _M0L5shiftS328;
    uint64_t _M0L4maskS329;
    int32_t _M0L6_2atmpS1608;
    int32_t _M0L6offsetS330;
    uint64_t _M0L1nS331;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS328 = moonbit_ctz32(_M0L5radixS327);
    _M0L4maskS329 = _M0L4baseS326 - 1ull;
    _M0L6_2atmpS1608 = _M0L10total__lenS336 - _M0L12digit__startS334;
    _M0L6offsetS330 = _M0L6_2atmpS1608;
    _M0L1nS331 = _M0L3numS337;
    while (1) {
      if (_M0L1nS331 > 0ull) {
        uint64_t _M0L6_2atmpS1607 = _M0L1nS331 & _M0L4maskS329;
        int32_t _M0L5digitS332 = (int32_t)_M0L6_2atmpS1607;
        int32_t _M0L6_2atmpS1604 = _M0L12digit__startS334 + _M0L6offsetS330;
        int32_t _M0L6_2atmpS1602 = _M0L6_2atmpS1604 - 1;
        int32_t _M0L6_2atmpS1603 =
          ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L5digitS332];
        int32_t _M0L6_2atmpS1605;
        uint64_t _M0L6_2atmpS1606;
        _M0L6bufferS333[_M0L6_2atmpS1602] = _M0L6_2atmpS1603;
        _M0L6_2atmpS1605 = _M0L6offsetS330 - 1;
        _M0L6_2atmpS1606 = _M0L1nS331 >> (_M0L5shiftS328 & 63);
        _M0L6offsetS330 = _M0L6_2atmpS1605;
        _M0L1nS331 = _M0L6_2atmpS1606;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1615 = _M0L10total__lenS336 - _M0L12digit__startS334;
    int32_t _M0L6offsetS338 = _M0L6_2atmpS1615;
    uint64_t _M0L1nS339 = _M0L3numS337;
    while (1) {
      if (_M0L1nS339 > 0ull) {
        uint64_t _M0L1qS340 = _M0L1nS339 / _M0L4baseS326;
        uint64_t _M0L6_2atmpS1614 = _M0L1qS340 * _M0L4baseS326;
        uint64_t _M0L6_2atmpS1613 = _M0L1nS339 - _M0L6_2atmpS1614;
        int32_t _M0L5digitS341 = (int32_t)_M0L6_2atmpS1613;
        int32_t _M0L6_2atmpS1611 = _M0L12digit__startS334 + _M0L6offsetS338;
        int32_t _M0L6_2atmpS1609 = _M0L6_2atmpS1611 - 1;
        int32_t _M0L6_2atmpS1610 =
          ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L5digitS341];
        int32_t _M0L6_2atmpS1612;
        _M0L6bufferS333[_M0L6_2atmpS1609] = _M0L6_2atmpS1610;
        _M0L6_2atmpS1612 = _M0L6offsetS338 - 1;
        _M0L6offsetS338 = _M0L6_2atmpS1612;
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
  int32_t _M0L6_2atmpS1599;
  int32_t _M0L6offsetS315;
  uint64_t _M0L1nS316;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1599 = _M0L10total__lenS324 - _M0L12digit__startS321;
  _M0L6offsetS315 = _M0L6_2atmpS1599;
  _M0L1nS316 = _M0L3numS325;
  while (1) {
    if (_M0L6offsetS315 >= 2) {
      uint64_t _M0L6_2atmpS1596 = _M0L1nS316 & 255ull;
      int32_t _M0L9byte__valS317 = (int32_t)_M0L6_2atmpS1596;
      int32_t _M0L2hiS318 = _M0L9byte__valS317 / 16;
      int32_t _M0L2loS319 = _M0L9byte__valS317 % 16;
      int32_t _M0L6_2atmpS1590 = _M0L12digit__startS321 + _M0L6offsetS315;
      int32_t _M0L6_2atmpS1588 = _M0L6_2atmpS1590 - 2;
      int32_t _M0L6_2atmpS1589 =
        ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L2hiS318];
      int32_t _M0L6_2atmpS1593;
      int32_t _M0L6_2atmpS1591;
      int32_t _M0L6_2atmpS1592;
      int32_t _M0L6_2atmpS1594;
      uint64_t _M0L6_2atmpS1595;
      _M0L6bufferS320[_M0L6_2atmpS1588] = _M0L6_2atmpS1589;
      _M0L6_2atmpS1593 = _M0L12digit__startS321 + _M0L6offsetS315;
      _M0L6_2atmpS1591 = _M0L6_2atmpS1593 - 1;
      _M0L6_2atmpS1592
      = ((moonbit_string_t)moonbit_string_literal_24.data)[
        _M0L2loS319
      ];
      _M0L6bufferS320[_M0L6_2atmpS1591] = _M0L6_2atmpS1592;
      _M0L6_2atmpS1594 = _M0L6offsetS315 - 2;
      _M0L6_2atmpS1595 = _M0L1nS316 >> 8;
      _M0L6offsetS315 = _M0L6_2atmpS1594;
      _M0L1nS316 = _M0L6_2atmpS1595;
      continue;
    } else if (_M0L6offsetS315 == 1) {
      uint64_t _M0L6_2atmpS1598 = _M0L1nS316 & 15ull;
      int32_t _M0L6nibbleS323 = (int32_t)_M0L6_2atmpS1598;
      int32_t _M0L6_2atmpS1597 =
        ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L6nibbleS323];
      _M0L6bufferS320[_M0L12digit__startS321] = _M0L6_2atmpS1597;
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
      uint64_t _M0L6_2atmpS1586 = _M0L3numS312 / _M0L4baseS310;
      int32_t _M0L6_2atmpS1587 = _M0L5countS313 + 1;
      _M0L3numS312 = _M0L6_2atmpS1586;
      _M0L5countS313 = _M0L6_2atmpS1587;
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
    int32_t _M0L6_2atmpS1585;
    int32_t _M0L6_2atmpS1584;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS308 = moonbit_clz64(_M0L5valueS307);
    _M0L6_2atmpS1585 = 63 - _M0L14leading__zerosS308;
    _M0L6_2atmpS1584 = _M0L6_2atmpS1585 / 4;
    return _M0L6_2atmpS1584 + 1;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_23.data);
  }
  if (_M0L4selfS290 == 0) {
    return (moonbit_string_t)moonbit_string_literal_16.data;
  }
  _M0L12is__negativeS291 = _M0L4selfS290 < 0;
  if (_M0L12is__negativeS291) {
    int32_t _M0L6_2atmpS1583 = -_M0L4selfS290;
    _M0L3numS292 = *(uint32_t*)&_M0L6_2atmpS1583;
  } else {
    _M0L3numS292 = *(uint32_t*)&_M0L4selfS290;
  }
  switch (_M0L5radixS289) {
    case 10: {
      int32_t _M0L10digit__lenS294;
      int32_t _M0L6_2atmpS1580;
      int32_t _M0L10total__lenS295;
      uint16_t* _M0L6bufferS296;
      int32_t _M0L12digit__startS297;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS294 = _M0FPB12dec__count32(_M0L3numS292);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1580 = 1;
      } else {
        _M0L6_2atmpS1580 = 0;
      }
      _M0L10total__lenS295 = _M0L10digit__lenS294 + _M0L6_2atmpS1580;
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
      int32_t _M0L6_2atmpS1581;
      int32_t _M0L10total__lenS299;
      uint16_t* _M0L6bufferS300;
      int32_t _M0L12digit__startS301;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS298 = _M0FPB12hex__count32(_M0L3numS292);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1581 = 1;
      } else {
        _M0L6_2atmpS1581 = 0;
      }
      _M0L10total__lenS299 = _M0L10digit__lenS298 + _M0L6_2atmpS1581;
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
      int32_t _M0L6_2atmpS1582;
      int32_t _M0L10total__lenS303;
      uint16_t* _M0L6bufferS304;
      int32_t _M0L12digit__startS305;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS302
      = _M0FPB14radix__count32(_M0L3numS292, _M0L5radixS289);
      if (_M0L12is__negativeS291) {
        _M0L6_2atmpS1582 = 1;
      } else {
        _M0L6_2atmpS1582 = 0;
      }
      _M0L10total__lenS303 = _M0L10digit__lenS302 + _M0L6_2atmpS1582;
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
      uint32_t _M0L6_2atmpS1578 = _M0L3numS286 / _M0L4baseS284;
      int32_t _M0L6_2atmpS1579 = _M0L5countS287 + 1;
      _M0L3numS286 = _M0L6_2atmpS1578;
      _M0L5countS287 = _M0L6_2atmpS1579;
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
    int32_t _M0L6_2atmpS1577;
    int32_t _M0L6_2atmpS1576;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS282 = moonbit_clz32(_M0L5valueS281);
    _M0L6_2atmpS1577 = 31 - _M0L14leading__zerosS282;
    _M0L6_2atmpS1576 = _M0L6_2atmpS1577 / 4;
    return _M0L6_2atmpS1576 + 1;
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
  int32_t _M0L6_2atmpS1575;
  uint32_t _M0L3numS256;
  int32_t _M0L6offsetS257;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1575 = _M0L10total__lenS279 - _M0L12digit__startS267;
  _M0L3numS256 = _M0L3numS278;
  _M0L6offsetS257 = _M0L6_2atmpS1575;
  while (1) {
    if (_M0L3numS256 >= 10000u) {
      uint32_t _M0L1tS258 = _M0L3numS256 / 10000u;
      uint32_t _M0L6_2atmpS1552 = _M0L3numS256 % 10000u;
      int32_t _M0L1rS259 = *(int32_t*)&_M0L6_2atmpS1552;
      int32_t _M0L2d1S260 = _M0L1rS259 / 100;
      int32_t _M0L2d2S261 = _M0L1rS259 % 100;
      int32_t _M0L6_2atmpS1551 = _M0L2d1S260 / 10;
      int32_t _M0L6_2atmpS1550 = 48 + _M0L6_2atmpS1551;
      int32_t _M0L6d1__hiS262 = (uint16_t)_M0L6_2atmpS1550;
      int32_t _M0L6_2atmpS1549 = _M0L2d1S260 % 10;
      int32_t _M0L6_2atmpS1548 = 48 + _M0L6_2atmpS1549;
      int32_t _M0L6d1__loS263 = (uint16_t)_M0L6_2atmpS1548;
      int32_t _M0L6_2atmpS1547 = _M0L2d2S261 / 10;
      int32_t _M0L6_2atmpS1546 = 48 + _M0L6_2atmpS1547;
      int32_t _M0L6d2__hiS264 = (uint16_t)_M0L6_2atmpS1546;
      int32_t _M0L6_2atmpS1545 = _M0L2d2S261 % 10;
      int32_t _M0L6_2atmpS1544 = 48 + _M0L6_2atmpS1545;
      int32_t _M0L6d2__loS265 = (uint16_t)_M0L6_2atmpS1544;
      int32_t _M0L6_2atmpS1536 = _M0L12digit__startS267 + _M0L6offsetS257;
      int32_t _M0L6_2atmpS1535 = _M0L6_2atmpS1536 - 4;
      int32_t _M0L6_2atmpS1538;
      int32_t _M0L6_2atmpS1537;
      int32_t _M0L6_2atmpS1540;
      int32_t _M0L6_2atmpS1539;
      int32_t _M0L6_2atmpS1542;
      int32_t _M0L6_2atmpS1541;
      int32_t _M0L6_2atmpS1543;
      _M0L6bufferS266[_M0L6_2atmpS1535] = _M0L6d1__hiS262;
      _M0L6_2atmpS1538 = _M0L12digit__startS267 + _M0L6offsetS257;
      _M0L6_2atmpS1537 = _M0L6_2atmpS1538 - 3;
      _M0L6bufferS266[_M0L6_2atmpS1537] = _M0L6d1__loS263;
      _M0L6_2atmpS1540 = _M0L12digit__startS267 + _M0L6offsetS257;
      _M0L6_2atmpS1539 = _M0L6_2atmpS1540 - 2;
      _M0L6bufferS266[_M0L6_2atmpS1539] = _M0L6d2__hiS264;
      _M0L6_2atmpS1542 = _M0L12digit__startS267 + _M0L6offsetS257;
      _M0L6_2atmpS1541 = _M0L6_2atmpS1542 - 1;
      _M0L6bufferS266[_M0L6_2atmpS1541] = _M0L6d2__loS265;
      _M0L6_2atmpS1543 = _M0L6offsetS257 - 4;
      _M0L3numS256 = _M0L1tS258;
      _M0L6offsetS257 = _M0L6_2atmpS1543;
      continue;
    } else {
      int32_t _M0L6_2atmpS1574 = *(int32_t*)&_M0L3numS256;
      int32_t _M0L9remainingS269 = _M0L6_2atmpS1574;
      int32_t _M0L6offsetS270 = _M0L6offsetS257;
      while (1) {
        if (_M0L9remainingS269 >= 100) {
          int32_t _M0L1tS271 = _M0L9remainingS269 / 100;
          int32_t _M0L1dS272 = _M0L9remainingS269 % 100;
          int32_t _M0L6_2atmpS1561 = _M0L1dS272 / 10;
          int32_t _M0L6_2atmpS1560 = 48 + _M0L6_2atmpS1561;
          int32_t _M0L5d__hiS273 = (uint16_t)_M0L6_2atmpS1560;
          int32_t _M0L6_2atmpS1559 = _M0L1dS272 % 10;
          int32_t _M0L6_2atmpS1558 = 48 + _M0L6_2atmpS1559;
          int32_t _M0L5d__loS274 = (uint16_t)_M0L6_2atmpS1558;
          int32_t _M0L6_2atmpS1554 = _M0L12digit__startS267 + _M0L6offsetS270;
          int32_t _M0L6_2atmpS1553 = _M0L6_2atmpS1554 - 2;
          int32_t _M0L6_2atmpS1556;
          int32_t _M0L6_2atmpS1555;
          int32_t _M0L6_2atmpS1557;
          _M0L6bufferS266[_M0L6_2atmpS1553] = _M0L5d__hiS273;
          _M0L6_2atmpS1556 = _M0L12digit__startS267 + _M0L6offsetS270;
          _M0L6_2atmpS1555 = _M0L6_2atmpS1556 - 1;
          _M0L6bufferS266[_M0L6_2atmpS1555] = _M0L5d__loS274;
          _M0L6_2atmpS1557 = _M0L6offsetS270 - 2;
          _M0L9remainingS269 = _M0L1tS271;
          _M0L6offsetS270 = _M0L6_2atmpS1557;
          continue;
        } else if (_M0L9remainingS269 >= 10) {
          int32_t _M0L6_2atmpS1569 = _M0L9remainingS269 / 10;
          int32_t _M0L6_2atmpS1568 = 48 + _M0L6_2atmpS1569;
          int32_t _M0L5d__hiS276 = (uint16_t)_M0L6_2atmpS1568;
          int32_t _M0L6_2atmpS1567 = _M0L9remainingS269 % 10;
          int32_t _M0L6_2atmpS1566 = 48 + _M0L6_2atmpS1567;
          int32_t _M0L5d__loS277 = (uint16_t)_M0L6_2atmpS1566;
          int32_t _M0L6_2atmpS1563 = _M0L12digit__startS267 + _M0L6offsetS270;
          int32_t _M0L6_2atmpS1562 = _M0L6_2atmpS1563 - 2;
          int32_t _M0L6_2atmpS1565;
          int32_t _M0L6_2atmpS1564;
          _M0L6bufferS266[_M0L6_2atmpS1562] = _M0L5d__hiS276;
          _M0L6_2atmpS1565 = _M0L12digit__startS267 + _M0L6offsetS270;
          _M0L6_2atmpS1564 = _M0L6_2atmpS1565 - 1;
          _M0L6bufferS266[_M0L6_2atmpS1564] = _M0L5d__loS277;
        } else {
          int32_t _M0L6_2atmpS1573 = _M0L12digit__startS267 + _M0L6offsetS270;
          int32_t _M0L6_2atmpS1570 = _M0L6_2atmpS1573 - 1;
          int32_t _M0L6_2atmpS1572 = 48 + _M0L9remainingS269;
          int32_t _M0L6_2atmpS1571 = (uint16_t)_M0L6_2atmpS1572;
          _M0L6bufferS266[_M0L6_2atmpS1570] = _M0L6_2atmpS1571;
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
  int32_t _M0L6_2atmpS1520;
  int32_t _M0L6_2atmpS1519;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS239 = *(uint32_t*)&_M0L5radixS240;
  _M0L6_2atmpS1520 = _M0L5radixS240 - 1;
  _M0L6_2atmpS1519 = _M0L5radixS240 & _M0L6_2atmpS1520;
  if (_M0L6_2atmpS1519 == 0) {
    int32_t _M0L5shiftS241;
    uint32_t _M0L4maskS242;
    int32_t _M0L6_2atmpS1527;
    int32_t _M0L6offsetS243;
    uint32_t _M0L1nS244;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS241 = moonbit_ctz32(_M0L5radixS240);
    _M0L4maskS242 = _M0L4baseS239 - 1u;
    _M0L6_2atmpS1527 = _M0L10total__lenS249 - _M0L12digit__startS247;
    _M0L6offsetS243 = _M0L6_2atmpS1527;
    _M0L1nS244 = _M0L3numS250;
    while (1) {
      if (_M0L1nS244 > 0u) {
        uint32_t _M0L6_2atmpS1526 = _M0L1nS244 & _M0L4maskS242;
        int32_t _M0L5digitS245 = *(int32_t*)&_M0L6_2atmpS1526;
        int32_t _M0L6_2atmpS1523 = _M0L12digit__startS247 + _M0L6offsetS243;
        int32_t _M0L6_2atmpS1521 = _M0L6_2atmpS1523 - 1;
        int32_t _M0L6_2atmpS1522 =
          ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L5digitS245];
        int32_t _M0L6_2atmpS1524;
        uint32_t _M0L6_2atmpS1525;
        _M0L6bufferS246[_M0L6_2atmpS1521] = _M0L6_2atmpS1522;
        _M0L6_2atmpS1524 = _M0L6offsetS243 - 1;
        _M0L6_2atmpS1525 = _M0L1nS244 >> (_M0L5shiftS241 & 31);
        _M0L6offsetS243 = _M0L6_2atmpS1524;
        _M0L1nS244 = _M0L6_2atmpS1525;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1534 = _M0L10total__lenS249 - _M0L12digit__startS247;
    int32_t _M0L6offsetS251 = _M0L6_2atmpS1534;
    uint32_t _M0L1nS252 = _M0L3numS250;
    while (1) {
      if (_M0L1nS252 > 0u) {
        uint32_t _M0L1qS253 = _M0L1nS252 / _M0L4baseS239;
        uint32_t _M0L6_2atmpS1533 = _M0L1qS253 * _M0L4baseS239;
        uint32_t _M0L6_2atmpS1532 = _M0L1nS252 - _M0L6_2atmpS1533;
        int32_t _M0L5digitS254 = *(int32_t*)&_M0L6_2atmpS1532;
        int32_t _M0L6_2atmpS1530 = _M0L12digit__startS247 + _M0L6offsetS251;
        int32_t _M0L6_2atmpS1528 = _M0L6_2atmpS1530 - 1;
        int32_t _M0L6_2atmpS1529 =
          ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L5digitS254];
        int32_t _M0L6_2atmpS1531;
        _M0L6bufferS246[_M0L6_2atmpS1528] = _M0L6_2atmpS1529;
        _M0L6_2atmpS1531 = _M0L6offsetS251 - 1;
        _M0L6offsetS251 = _M0L6_2atmpS1531;
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
  int32_t _M0L6_2atmpS1518;
  int32_t _M0L6offsetS228;
  uint32_t _M0L1nS229;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1518 = _M0L10total__lenS237 - _M0L12digit__startS234;
  _M0L6offsetS228 = _M0L6_2atmpS1518;
  _M0L1nS229 = _M0L3numS238;
  while (1) {
    if (_M0L6offsetS228 >= 2) {
      uint32_t _M0L6_2atmpS1515 = _M0L1nS229 & 255u;
      int32_t _M0L9byte__valS230 = *(int32_t*)&_M0L6_2atmpS1515;
      int32_t _M0L2hiS231 = _M0L9byte__valS230 / 16;
      int32_t _M0L2loS232 = _M0L9byte__valS230 % 16;
      int32_t _M0L6_2atmpS1509 = _M0L12digit__startS234 + _M0L6offsetS228;
      int32_t _M0L6_2atmpS1507 = _M0L6_2atmpS1509 - 2;
      int32_t _M0L6_2atmpS1508 =
        ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L2hiS231];
      int32_t _M0L6_2atmpS1512;
      int32_t _M0L6_2atmpS1510;
      int32_t _M0L6_2atmpS1511;
      int32_t _M0L6_2atmpS1513;
      uint32_t _M0L6_2atmpS1514;
      _M0L6bufferS233[_M0L6_2atmpS1507] = _M0L6_2atmpS1508;
      _M0L6_2atmpS1512 = _M0L12digit__startS234 + _M0L6offsetS228;
      _M0L6_2atmpS1510 = _M0L6_2atmpS1512 - 1;
      _M0L6_2atmpS1511
      = ((moonbit_string_t)moonbit_string_literal_24.data)[
        _M0L2loS232
      ];
      _M0L6bufferS233[_M0L6_2atmpS1510] = _M0L6_2atmpS1511;
      _M0L6_2atmpS1513 = _M0L6offsetS228 - 2;
      _M0L6_2atmpS1514 = _M0L1nS229 >> 8;
      _M0L6offsetS228 = _M0L6_2atmpS1513;
      _M0L1nS229 = _M0L6_2atmpS1514;
      continue;
    } else if (_M0L6offsetS228 == 1) {
      uint32_t _M0L6_2atmpS1517 = _M0L1nS229 & 15u;
      int32_t _M0L6nibbleS236 = *(int32_t*)&_M0L6_2atmpS1517;
      int32_t _M0L6_2atmpS1516 =
        ((moonbit_string_t)moonbit_string_literal_24.data)[_M0L6nibbleS236];
      _M0L6bufferS233[_M0L12digit__startS234] = _M0L6_2atmpS1516;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS227
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS226;
  struct _M0TPB6Logger _M0L6_2atmpS1506;
  moonbit_string_t _result_3323;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS226);
  _M0L6_2atmpS1506
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS226
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS227, _M0L6_2atmpS1506);
  if (_M0L6_2atmpS1506.$1) {
    moonbit_decref(_M0L6_2atmpS1506.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_3323 = _M0MPB13StringBuilder10to__string(_M0L6loggerS226);
  moonbit_decref_cycle_free(_M0L6loggerS226);
  return _result_3323;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS221,
  struct _M0TPB6Logger _M0L6loggerS220
) {
  moonbit_string_t _M0L6_2atmpS1503;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1503 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS221);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS220.$0->$method_0(_M0L6loggerS220.$1, _M0L6_2atmpS1503);
  moonbit_decref_cycle_free(_M0L6_2atmpS1503);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS223,
  struct _M0TPB6Logger _M0L6loggerS222
) {
  moonbit_string_t _M0L6_2atmpS1504;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1504 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS223);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS222.$0->$method_0(_M0L6loggerS222.$1, _M0L6_2atmpS1504);
  moonbit_decref_cycle_free(_M0L6_2atmpS1504);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS225,
  struct _M0TPB6Logger _M0L6loggerS224
) {
  moonbit_string_t _M0L6_2atmpS1505;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1505 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS225);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS224.$0->$method_0(_M0L6loggerS224.$1, _M0L6_2atmpS1505);
  moonbit_decref_cycle_free(_M0L6_2atmpS1505);
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
  moonbit_string_t _M0L8_2afieldS3186;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS3186 = _M0L4selfS218.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS3186);
  return _M0L8_2afieldS3186;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS214,
  moonbit_string_t _M0L5valueS215,
  int32_t _M0L5startS216,
  int32_t _M0L3lenS217
) {
  int32_t _M0L6_2atmpS1502;
  int64_t _M0L6_2atmpS1501;
  struct _M0TPC16string10StringView _M0L6_2atmpS1500;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1502 = _M0L5startS216 + _M0L3lenS217;
  _M0L6_2atmpS1501 = (int64_t)_M0L6_2atmpS1502;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1500
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS215, _M0L5startS216, _M0L6_2atmpS1501);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS214, _M0L6_2atmpS1500);
  moonbit_decref_cycle_free(_M0L6_2atmpS1500.$0);
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
  int32_t _M0L6_2atmpS1484;
  int32_t _if__result_3324;
  int32_t _M0L6_2atmpS1492;
  int32_t _if__result_3325;
  int32_t _M0L6_2atmpS1494;
  int32_t _M0L6_2atmpS1495;
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
  _M0L6_2atmpS1484 = _M0Lm2loS208;
  if (_M0L6_2atmpS1484 > 0) {
    int32_t _M0L6_2atmpS1483 = _M0Lm2loS208;
    if (_M0L6_2atmpS1483 < _M0L3lenS206) {
      int32_t _M0L6_2atmpS1482 = _M0Lm2loS208;
      int32_t _M0L6_2atmpS1481 = _M0L4selfS207[_M0L6_2atmpS1482];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1481)) {
        int32_t _M0L6_2atmpS1480 = _M0Lm2loS208;
        int32_t _M0L6_2atmpS1479 = _M0L6_2atmpS1480 - 1;
        int32_t _M0L6_2atmpS1478 = _M0L4selfS207[_M0L6_2atmpS1479];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_3324
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1478);
      } else {
        _if__result_3324 = 0;
      }
    } else {
      _if__result_3324 = 0;
    }
  } else {
    _if__result_3324 = 0;
  }
  if (_if__result_3324) {
    int32_t _M0L6_2atmpS1485 = _M0Lm2loS208;
    _M0Lm2loS208 = _M0L6_2atmpS1485 + 1;
  }
  _M0L6_2atmpS1492 = _M0Lm2hiS210;
  if (_M0L6_2atmpS1492 > 0) {
    int32_t _M0L6_2atmpS1491 = _M0Lm2hiS210;
    if (_M0L6_2atmpS1491 < _M0L3lenS206) {
      int32_t _M0L6_2atmpS1490 = _M0Lm2hiS210;
      int32_t _M0L6_2atmpS1489 = _M0L4selfS207[_M0L6_2atmpS1490];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1489)) {
        int32_t _M0L6_2atmpS1488 = _M0Lm2hiS210;
        int32_t _M0L6_2atmpS1487 = _M0L6_2atmpS1488 - 1;
        int32_t _M0L6_2atmpS1486 = _M0L4selfS207[_M0L6_2atmpS1487];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_3325
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1486);
      } else {
        _if__result_3325 = 0;
      }
    } else {
      _if__result_3325 = 0;
    }
  } else {
    _if__result_3325 = 0;
  }
  if (_if__result_3325) {
    int32_t _M0L6_2atmpS1493 = _M0Lm2hiS210;
    _M0Lm2hiS210 = _M0L6_2atmpS1493 - 1;
  }
  _M0L6_2atmpS1494 = _M0Lm2loS208;
  _M0L6_2atmpS1495 = _M0Lm2hiS210;
  if (_M0L6_2atmpS1494 >= _M0L6_2atmpS1495) {
    int32_t _M0L6_2atmpS1496 = _M0Lm2loS208;
    int32_t _M0L6_2atmpS1497 = _M0Lm2loS208;
    moonbit_incref_cycle_free(_M0L4selfS207);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS207,
                                                 .$1 = _M0L6_2atmpS1496,
                                                 .$2 = _M0L6_2atmpS1497};
  } else {
    int32_t _M0L6_2atmpS1498 = _M0Lm2loS208;
    int32_t _M0L6_2atmpS1499 = _M0Lm2hiS210;
    moonbit_incref_cycle_free(_M0L4selfS207);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS207,
                                                 .$1 = _M0L6_2atmpS1498,
                                                 .$2 = _M0L6_2atmpS1499};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS205,
  struct _M0TPB4Show _M0L4showS204
) {
  struct _M0TPB6Logger _M0L6_2atmpS1477;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS205);
  _M0L6_2atmpS1477
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS205
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS204.$0->$method_0(_M0L4showS204.$1, _M0L6_2atmpS1477);
  if (_M0L6_2atmpS1477.$1) {
    moonbit_decref(_M0L6_2atmpS1477.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS203,
  struct _M0TPB4Show _M0L4showS202
) {
  struct _M0TPB6Logger _M0L6_2atmpS1476;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS203);
  _M0L6_2atmpS1476
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS203
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS202.$0->$method_0(_M0L4showS202.$1, _M0L6_2atmpS1476);
  if (_M0L6_2atmpS1476.$1) {
    moonbit_decref(_M0L6_2atmpS1476.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS201) {
  int64_t _M0L6_2atmpS1475;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1475 = (int64_t)_M0L4selfS201;
  return *(uint64_t*)&_M0L6_2atmpS1475;
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
  int32_t _M0L6_2atmpS1474;
  struct _M0TPC16string10StringView _M0L6_2atmpS1472;
  struct _M0TPB6Logger _M0L6_2atmpS1473;
  moonbit_string_t _result_3326;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1474 = Moonbit_array_length(_M0L4selfS199);
  moonbit_incref_cycle_free(_M0L4selfS199);
  _M0L6_2atmpS1472
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS199, .$1 = 0, .$2 = _M0L6_2atmpS1474
  };
  moonbit_incref_cycle_free(_M0L3bufS198);
  _M0L6_2atmpS1473
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS198
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1472, _M0L6_2atmpS1473, _M0L5quoteS200);
  moonbit_decref_cycle_free(_M0L6_2atmpS1472.$0);
  if (_M0L6_2atmpS1473.$1) {
    moonbit_decref(_M0L6_2atmpS1473.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_3326 = _M0MPB13StringBuilder10to__string(_M0L3bufS198);
  moonbit_decref_cycle_free(_M0L3bufS198);
  return _result_3326;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS190,
  struct _M0TPB6Logger _M0L6loggerS188,
  int32_t _M0L5quoteS187
) {
  int32_t _M0L3endS1470;
  int32_t _M0L5startS1471;
  int32_t _M0L3lenS189;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS191;
  int32_t _M0L1iS192;
  int32_t _M0L3segS193;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS187) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS188.$0->$method_3(_M0L6loggerS188.$1, 34);
  }
  _M0L3endS1470 = _M0L4selfS190.$2;
  _M0L5startS1471 = _M0L4selfS190.$1;
  _M0L3lenS189 = _M0L3endS1470 - _M0L5startS1471;
  moonbit_incref_cycle_free(_M0L4selfS190.$0);
  if (_M0L6loggerS188.$1) {
    moonbit_incref(_M0L6loggerS188.$1);
  }
  _M0L6_2aenvS191
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS191)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 137, 0);
  _M0L6_2aenvS191->$0 = _M0L4selfS190;
  _M0L6_2aenvS191->$1 = _M0L6loggerS188;
  _M0L1iS192 = 0;
  _M0L3segS193 = 0;
  _2afor_194:;
  while (1) {
    moonbit_string_t _M0L3strS1467;
    int32_t _M0L5startS1469;
    int32_t _M0L6_2atmpS1468;
    int32_t _M0L4codeS195;
    int32_t _M0L1cS197;
    int32_t _M0L6_2atmpS1451;
    int32_t _M0L6_2atmpS1452;
    int32_t _M0L6_2atmpS1453;
    if (_M0L1iS192 >= _M0L3lenS189) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
      moonbit_decref_cycle_free(_M0L6_2aenvS191);
      break;
    }
    _M0L3strS1467 = _M0L4selfS190.$0;
    _M0L5startS1469 = _M0L4selfS190.$1;
    _M0L6_2atmpS1468 = _M0L5startS1469 + _M0L1iS192;
    _M0L4codeS195 = _M0L3strS1467[_M0L6_2atmpS1468];
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
        int32_t _M0L6_2atmpS1454;
        int32_t _M0L6_2atmpS1455;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_25.data);
        _M0L6_2atmpS1454 = _M0L1iS192 + 1;
        _M0L6_2atmpS1455 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1454;
        _M0L3segS193 = _M0L6_2atmpS1455;
        goto _2afor_194;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1456;
        int32_t _M0L6_2atmpS1457;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_26.data);
        _M0L6_2atmpS1456 = _M0L1iS192 + 1;
        _M0L6_2atmpS1457 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1456;
        _M0L3segS193 = _M0L6_2atmpS1457;
        goto _2afor_194;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1458;
        int32_t _M0L6_2atmpS1459;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_27.data);
        _M0L6_2atmpS1458 = _M0L1iS192 + 1;
        _M0L6_2atmpS1459 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1458;
        _M0L3segS193 = _M0L6_2atmpS1459;
        goto _2afor_194;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1460;
        int32_t _M0L6_2atmpS1461;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_28.data);
        _M0L6_2atmpS1460 = _M0L1iS192 + 1;
        _M0L6_2atmpS1461 = _M0L1iS192 + 1;
        _M0L1iS192 = _M0L6_2atmpS1460;
        _M0L3segS193 = _M0L6_2atmpS1461;
        goto _2afor_194;
        break;
      }
      default: {
        if (_M0L4codeS195 < 32) {
          int32_t _M0L6_2atmpS1463;
          moonbit_string_t _M0L6_2atmpS1462;
          int32_t _M0L6_2atmpS1464;
          int32_t _M0L6_2atmpS1465;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_29.data);
          _M0L6_2atmpS1463 = _M0L4codeS195 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1462 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1463);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, _M0L6_2atmpS1462);
          moonbit_decref_cycle_free(_M0L6_2atmpS1462);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1464 = _M0L1iS192 + 1;
          _M0L6_2atmpS1465 = _M0L1iS192 + 1;
          _M0L1iS192 = _M0L6_2atmpS1464;
          _M0L3segS193 = _M0L6_2atmpS1465;
          goto _2afor_194;
        } else {
          int32_t _M0L6_2atmpS1466 = _M0L1iS192 + 1;
          int32_t _tmp_3329 = _M0L3segS193;
          _M0L1iS192 = _M0L6_2atmpS1466;
          _M0L3segS193 = _tmp_3329;
          goto _2afor_194;
        }
        break;
      }
    }
    goto joinlet_3328;
    join_196:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS191, _M0L3segS193, _M0L1iS192);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS188.$0->$method_3(_M0L6loggerS188.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1451 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS197);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS188.$0->$method_3(_M0L6loggerS188.$1, _M0L6_2atmpS1451);
    _M0L6_2atmpS1452 = _M0L1iS192 + 1;
    _M0L6_2atmpS1453 = _M0L1iS192 + 1;
    _M0L1iS192 = _M0L6_2atmpS1452;
    _M0L3segS193 = _M0L6_2atmpS1453;
    continue;
    joinlet_3328:;
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
    int64_t _M0L6_2atmpS1450 = (int64_t)_M0L1iS185;
    struct _M0TPC16string10StringView _M0L6_2atmpS1449;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1449
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS184, _M0L3segS186, _M0L6_2atmpS1450);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS182.$0->$method_2(_M0L6loggerS182.$1, _M0L6_2atmpS1449);
    moonbit_decref_cycle_free(_M0L6_2atmpS1449.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS173,
  int32_t _M0L5startS175,
  int64_t _M0L3endS177
) {
  int32_t _M0L3endS1447;
  int32_t _M0L5startS1448;
  int32_t _M0L3lenS172;
  int32_t _M0Lm2loS174;
  int32_t _M0Lm2hiS176;
  moonbit_string_t _M0L3strS180;
  int32_t _M0L4baseS181;
  int32_t _M0L6_2atmpS1425;
  int32_t _if__result_3330;
  int32_t _M0L6_2atmpS1435;
  int32_t _if__result_3331;
  int32_t _M0L6_2atmpS1437;
  int32_t _M0L6_2atmpS1438;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1447 = _M0L4selfS173.$2;
  _M0L5startS1448 = _M0L4selfS173.$1;
  _M0L3lenS172 = _M0L3endS1447 - _M0L5startS1448;
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
  _M0L6_2atmpS1425 = _M0Lm2loS174;
  if (_M0L6_2atmpS1425 > 0) {
    int32_t _M0L6_2atmpS1424 = _M0Lm2loS174;
    if (_M0L6_2atmpS1424 < _M0L3lenS172) {
      int32_t _M0L6_2atmpS1423 = _M0Lm2loS174;
      int32_t _M0L6_2atmpS1422 = _M0L4baseS181 + _M0L6_2atmpS1423;
      int32_t _M0L6_2atmpS1421 = _M0L3strS180[_M0L6_2atmpS1422];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1421)) {
        int32_t _M0L6_2atmpS1420 = _M0Lm2loS174;
        int32_t _M0L6_2atmpS1419 = _M0L4baseS181 + _M0L6_2atmpS1420;
        int32_t _M0L6_2atmpS1418 = _M0L6_2atmpS1419 - 1;
        int32_t _M0L6_2atmpS1417 = _M0L3strS180[_M0L6_2atmpS1418];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_3330
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1417);
      } else {
        _if__result_3330 = 0;
      }
    } else {
      _if__result_3330 = 0;
    }
  } else {
    _if__result_3330 = 0;
  }
  if (_if__result_3330) {
    int32_t _M0L6_2atmpS1426 = _M0Lm2loS174;
    _M0Lm2loS174 = _M0L6_2atmpS1426 + 1;
  }
  _M0L6_2atmpS1435 = _M0Lm2hiS176;
  if (_M0L6_2atmpS1435 > 0) {
    int32_t _M0L6_2atmpS1434 = _M0Lm2hiS176;
    if (_M0L6_2atmpS1434 < _M0L3lenS172) {
      int32_t _M0L6_2atmpS1433 = _M0Lm2hiS176;
      int32_t _M0L6_2atmpS1432 = _M0L4baseS181 + _M0L6_2atmpS1433;
      int32_t _M0L6_2atmpS1431 = _M0L3strS180[_M0L6_2atmpS1432];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1431)) {
        int32_t _M0L6_2atmpS1430 = _M0Lm2hiS176;
        int32_t _M0L6_2atmpS1429 = _M0L4baseS181 + _M0L6_2atmpS1430;
        int32_t _M0L6_2atmpS1428 = _M0L6_2atmpS1429 - 1;
        int32_t _M0L6_2atmpS1427 = _M0L3strS180[_M0L6_2atmpS1428];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_3331
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1427);
      } else {
        _if__result_3331 = 0;
      }
    } else {
      _if__result_3331 = 0;
    }
  } else {
    _if__result_3331 = 0;
  }
  if (_if__result_3331) {
    int32_t _M0L6_2atmpS1436 = _M0Lm2hiS176;
    _M0Lm2hiS176 = _M0L6_2atmpS1436 - 1;
  }
  _M0L6_2atmpS1437 = _M0Lm2loS174;
  _M0L6_2atmpS1438 = _M0Lm2hiS176;
  if (_M0L6_2atmpS1437 >= _M0L6_2atmpS1438) {
    int32_t _M0L6_2atmpS1442 = _M0Lm2loS174;
    int32_t _M0L6_2atmpS1439 = _M0L4baseS181 + _M0L6_2atmpS1442;
    int32_t _M0L6_2atmpS1441 = _M0Lm2loS174;
    int32_t _M0L6_2atmpS1440 = _M0L4baseS181 + _M0L6_2atmpS1441;
    moonbit_incref_cycle_free(_M0L3strS180);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS180,
                                                 .$1 = _M0L6_2atmpS1439,
                                                 .$2 = _M0L6_2atmpS1440};
  } else {
    int32_t _M0L6_2atmpS1446 = _M0Lm2loS174;
    int32_t _M0L6_2atmpS1443 = _M0L4baseS181 + _M0L6_2atmpS1446;
    int32_t _M0L6_2atmpS1445 = _M0Lm2hiS176;
    int32_t _M0L6_2atmpS1444 = _M0L4baseS181 + _M0L6_2atmpS1445;
    moonbit_incref_cycle_free(_M0L3strS180);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS180,
                                                 .$1 = _M0L6_2atmpS1443,
                                                 .$2 = _M0L6_2atmpS1444};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS171) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS170;
  int32_t _M0L6_2atmpS1414;
  int32_t _M0L6_2atmpS1413;
  int32_t _M0L6_2atmpS1416;
  int32_t _M0L6_2atmpS1415;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1412;
  moonbit_string_t _result_3332;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS170 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1414 = _M0IPC14byte4BytePB3Div3div(_M0L1bS171, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1413
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1414);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS170, _M0L6_2atmpS1413);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1416 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS171, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1415
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1416);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS170, _M0L6_2atmpS1415);
  _M0L6_2atmpS1412 = _M0L7_2aselfS170;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_3332 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1412);
  moonbit_decref_cycle_free(_M0L6_2atmpS1412);
  return _result_3332;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS169) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS169 < 10) {
    int32_t _M0L6_2atmpS1409;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1409 = _M0IPC14byte4BytePB3Add3add(_M0L1iS169, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1409);
  } else {
    int32_t _M0L6_2atmpS1411;
    int32_t _M0L6_2atmpS1410;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1411 = _M0IPC14byte4BytePB3Add3add(_M0L1iS169, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1410 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1411, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1410);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS167,
  int32_t _M0L4thatS168
) {
  int32_t _M0L6_2atmpS1407;
  int32_t _M0L6_2atmpS1408;
  int32_t _M0L6_2atmpS1406;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1407 = (int32_t)_M0L4selfS167;
  _M0L6_2atmpS1408 = (int32_t)_M0L4thatS168;
  _M0L6_2atmpS1406 = _M0L6_2atmpS1407 - _M0L6_2atmpS1408;
  return _M0L6_2atmpS1406 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS165,
  int32_t _M0L4thatS166
) {
  int32_t _M0L6_2atmpS1404;
  int32_t _M0L6_2atmpS1405;
  int32_t _M0L6_2atmpS1403;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1404 = (int32_t)_M0L4selfS165;
  _M0L6_2atmpS1405 = (int32_t)_M0L4thatS166;
  _M0L6_2atmpS1403 = _M0L6_2atmpS1404 % _M0L6_2atmpS1405;
  return _M0L6_2atmpS1403 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS163,
  int32_t _M0L4thatS164
) {
  int32_t _M0L6_2atmpS1401;
  int32_t _M0L6_2atmpS1402;
  int32_t _M0L6_2atmpS1400;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1401 = (int32_t)_M0L4selfS163;
  _M0L6_2atmpS1402 = (int32_t)_M0L4thatS164;
  _M0L6_2atmpS1400 = _M0L6_2atmpS1401 / _M0L6_2atmpS1402;
  return _M0L6_2atmpS1400 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS161,
  int32_t _M0L4thatS162
) {
  int32_t _M0L6_2atmpS1398;
  int32_t _M0L6_2atmpS1399;
  int32_t _M0L6_2atmpS1397;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1398 = (int32_t)_M0L4selfS161;
  _M0L6_2atmpS1399 = (int32_t)_M0L4thatS162;
  _M0L6_2atmpS1397 = _M0L6_2atmpS1398 + _M0L6_2atmpS1399;
  return _M0L6_2atmpS1397 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS160) {
  int32_t _M0L6_2atmpS1396;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1396 = (int32_t)_M0L4selfS160;
  return _M0L6_2atmpS1396;
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
  int32_t _M0L3lenS1395;
  int32_t _M0L8requiredS156;
  uint16_t* _M0L4dataS1390;
  int32_t _M0L6_2atmpS1389;
  int32_t _if__result_3333;
  uint16_t* _M0L4dataS1391;
  int32_t _M0L3lenS1392;
  int32_t _M0L3lenS1394;
  int32_t _M0L6_2atmpS1393;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS154 = Moonbit_array_length(_M0L3strS155);
  if (_M0L8str__lenS154 == 0) {
    return 0;
  }
  _M0L3lenS1395 = _M0L4selfS157->$1;
  _M0L8requiredS156 = _M0L3lenS1395 + _M0L8str__lenS154;
  _M0L4dataS1390 = _M0L4selfS157->$0;
  _M0L6_2atmpS1389 = Moonbit_array_length(_M0L4dataS1390);
  if (_M0L8requiredS156 > _M0L6_2atmpS1389) {
    _if__result_3333 = 1;
  } else {
    int32_t _M0L3lenS1388 = _M0L4selfS157->$1;
    _if__result_3333 = _M0L8requiredS156 < _M0L3lenS1388;
  }
  if (_if__result_3333) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS157, _M0L8requiredS156);
  }
  _M0L4dataS1391 = _M0L4selfS157->$0;
  _M0L3lenS1392 = _M0L4selfS157->$1;
  moonbit_incref_cycle_free(_M0L4dataS1391);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1391, _M0L3lenS1392, _M0L3strS155, 0, _M0L8str__lenS154);
  moonbit_decref_cycle_free(_M0L4dataS1391);
  _M0L3lenS1394 = _M0L4selfS157->$1;
  _M0L6_2atmpS1393 = _M0L3lenS1394 + _M0L8str__lenS154;
  _M0L4selfS157->$1 = _M0L6_2atmpS1393;
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
      int32_t _M0L6_2atmpS1385 = _M0L3strS151[_M0L1iS148];
      int32_t _M0L6_2atmpS1386;
      int32_t _M0L6_2atmpS1387;
      _M0L4selfS150[_M0L1jS149] = _M0L6_2atmpS1385;
      _M0L6_2atmpS1386 = _M0L1iS148 + 1;
      _M0L6_2atmpS1387 = _M0L1jS149 + 1;
      _M0L1iS148 = _M0L6_2atmpS1386;
      _M0L1jS149 = _M0L6_2atmpS1387;
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
    int32_t _M0L3lenS1356 = _M0L4selfS143->$1;
    uint16_t* _M0L4dataS1358 = _M0L4selfS143->$0;
    int32_t _M0L6_2atmpS1357 = Moonbit_array_length(_M0L4dataS1358);
    uint16_t* _M0L4dataS1361;
    int32_t _M0L3lenS1362;
    int32_t _M0L6_2atmpS1363;
    int32_t _M0L3lenS1365;
    int32_t _M0L6_2atmpS1364;
    if (_M0L3lenS1356 >= _M0L6_2atmpS1357) {
      int32_t _M0L3lenS1360 = _M0L4selfS143->$1;
      int32_t _M0L6_2atmpS1359 = _M0L3lenS1360 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS143, _M0L6_2atmpS1359);
    }
    _M0L4dataS1361 = _M0L4selfS143->$0;
    _M0L3lenS1362 = _M0L4selfS143->$1;
    moonbit_incref_cycle_free(_M0L4dataS1361);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1363 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS141);
    if (
      _M0L3lenS1362 < 0
      || _M0L3lenS1362 >= Moonbit_array_length(_M0L4dataS1361)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1361[_M0L3lenS1362] = _M0L6_2atmpS1363;
    moonbit_decref_cycle_free(_M0L4dataS1361);
    _M0L3lenS1365 = _M0L4selfS143->$1;
    _M0L6_2atmpS1364 = _M0L3lenS1365 + 1;
    _M0L4selfS143->$1 = _M0L6_2atmpS1364;
  } else if (_M0L4codeS141 <= 1114111u) {
    uint16_t* _M0L4dataS1369 = _M0L4selfS143->$0;
    int32_t _M0L6_2atmpS1367 = Moonbit_array_length(_M0L4dataS1369);
    int32_t _M0L3lenS1368 = _M0L4selfS143->$1;
    int32_t _M0L6_2atmpS1366 = _M0L6_2atmpS1367 - _M0L3lenS1368;
    uint32_t _M0L4codeS144;
    uint16_t* _M0L4dataS1372;
    int32_t _M0L3lenS1373;
    uint32_t _M0L6_2atmpS1376;
    uint32_t _M0L6_2atmpS1375;
    int32_t _M0L6_2atmpS1374;
    uint16_t* _M0L4dataS1377;
    int32_t _M0L3lenS1382;
    int32_t _M0L6_2atmpS1378;
    uint32_t _M0L6_2atmpS1381;
    uint32_t _M0L6_2atmpS1380;
    int32_t _M0L6_2atmpS1379;
    int32_t _M0L3lenS1384;
    int32_t _M0L6_2atmpS1383;
    if (_M0L6_2atmpS1366 < 2) {
      int32_t _M0L3lenS1371 = _M0L4selfS143->$1;
      int32_t _M0L6_2atmpS1370 = _M0L3lenS1371 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS143, _M0L6_2atmpS1370);
    }
    _M0L4codeS144 = _M0L4codeS141 - 65536u;
    _M0L4dataS1372 = _M0L4selfS143->$0;
    _M0L3lenS1373 = _M0L4selfS143->$1;
    _M0L6_2atmpS1376 = _M0L4codeS144 >> 10;
    _M0L6_2atmpS1375 = 55296u + _M0L6_2atmpS1376;
    moonbit_incref_cycle_free(_M0L4dataS1372);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1374 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1375);
    if (
      _M0L3lenS1373 < 0
      || _M0L3lenS1373 >= Moonbit_array_length(_M0L4dataS1372)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1372[_M0L3lenS1373] = _M0L6_2atmpS1374;
    moonbit_decref_cycle_free(_M0L4dataS1372);
    _M0L4dataS1377 = _M0L4selfS143->$0;
    _M0L3lenS1382 = _M0L4selfS143->$1;
    _M0L6_2atmpS1378 = _M0L3lenS1382 + 1;
    _M0L6_2atmpS1381 = _M0L4codeS144 & 1023u;
    _M0L6_2atmpS1380 = 56320u + _M0L6_2atmpS1381;
    moonbit_incref_cycle_free(_M0L4dataS1377);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1379 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1380);
    if (
      _M0L6_2atmpS1378 < 0
      || _M0L6_2atmpS1378 >= Moonbit_array_length(_M0L4dataS1377)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1377[_M0L6_2atmpS1378] = _M0L6_2atmpS1379;
    moonbit_decref_cycle_free(_M0L4dataS1377);
    _M0L3lenS1384 = _M0L4selfS143->$1;
    _M0L6_2atmpS1383 = _M0L3lenS1384 + 2;
    _M0L4selfS143->$1 = _M0L6_2atmpS1383;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_30.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS138,
  int32_t _M0L8requiredS139
) {
  uint16_t* _M0L4dataS1355;
  int32_t _M0L6_2atmpS1353;
  int32_t _M0L3lenS1354;
  int32_t _M0L13new__capacityS137;
  uint16_t* _M0L4dataS1350;
  int32_t _M0L6_2atmpS1351;
  int32_t _M0L3lenS1352;
  uint16_t* _M0L9new__dataS140;
  uint16_t* _M0L6_2aoldS3187;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1355 = _M0L4selfS138->$0;
  _M0L6_2atmpS1353 = Moonbit_array_length(_M0L4dataS1355);
  _M0L3lenS1354 = _M0L4selfS138->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS137
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1353, _M0L3lenS1354, _M0L8requiredS139);
  _M0L4dataS1350 = _M0L4selfS138->$0;
  moonbit_incref_cycle_free(_M0L4dataS1350);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1351 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1352 = _M0L4selfS138->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS140
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1350, _M0L13new__capacityS137, _M0L6_2atmpS1351, _M0L3lenS1352, 0, 0);
  _M0L6_2aoldS3187 = _M0L4selfS138->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS3187);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_31.data);
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
  int32_t _M0L6_2atmpS1349;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1349 = *(int32_t*)&_M0L4selfS130;
  return (uint16_t)_M0L6_2atmpS1349;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS129) {
  int32_t _M0L6_2atmpS1348;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1348 = _M0L4selfS129;
  return *(uint32_t*)&_M0L6_2atmpS1348;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS127
) {
  int32_t _M0L3lenS1339;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1339 = _M0L4selfS127->$1;
  if (_M0L3lenS1339 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1340 = _M0L4selfS127->$1;
    uint16_t* _M0L4dataS1342 = _M0L4selfS127->$0;
    int32_t _M0L6_2atmpS1341 = Moonbit_array_length(_M0L4dataS1342);
    if (_M0L3lenS1340 == _M0L6_2atmpS1341) {
      uint16_t* _M0L4dataS1343 = _M0L4selfS127->$0;
      moonbit_incref_cycle_free(_M0L4dataS1343);
      return _M0L4dataS1343;
    } else {
      uint16_t* _M0L4dataS1344 = _M0L4selfS127->$0;
      int32_t _M0L3lenS1345 = _M0L4selfS127->$1;
      int32_t _M0L6_2atmpS1346;
      int32_t _M0L3lenS1347;
      uint16_t* _M0L4dataS128;
      moonbit_incref_cycle_free(_M0L4dataS1344);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1346 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1347 = _M0L4selfS127->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS128
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1344, _M0L3lenS1345, _M0L6_2atmpS1346, _M0L3lenS1347, 0, 0);
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
  int32_t _if__result_3336;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS120 >= 0) {
    if (_M0L3lenS121 >= 0) {
      if (_M0L11src__offsetS122 >= 0) {
        if (_M0L11dst__offsetS123 >= 0) {
          int32_t _M0L6_2atmpS1335 = _M0L11src__offsetS122 + _M0L3lenS121;
          int32_t _M0L6_2atmpS1336 = Moonbit_array_length(_M0L3srcS124);
          if (_M0L6_2atmpS1335 <= _M0L6_2atmpS1336) {
            int32_t _M0L6_2atmpS1334 = _M0L11dst__offsetS123 + _M0L3lenS121;
            _if__result_3336 = _M0L6_2atmpS1334 <= _M0L13allocate__lenS120;
          } else {
            _if__result_3336 = 0;
          }
        } else {
          _if__result_3336 = 0;
        }
      } else {
        _if__result_3336 = 0;
      }
    } else {
      _if__result_3336 = 0;
    }
  } else {
    _if__result_3336 = 0;
  }
  if (_if__result_3336) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS124, _M0L13allocate__lenS120, _M0L4initS125, _M0L11src__offsetS122, _M0L11dst__offsetS123, _M0L3lenS121);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS126;
    int32_t _M0L6_2atmpS1338;
    moonbit_string_t _M0L6_2atmpS1337;
    uint16_t* _result_3337;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS126
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L13allocate__lenS120);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L11src__offsetS122);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L11dst__offsetS123);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L3lenS121);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS126, (moonbit_string_t)moonbit_string_literal_36.data);
    _M0L6_2atmpS1338 = Moonbit_array_length(_M0L3srcS124);
    moonbit_decref_cycle_free(_M0L3srcS124);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS126, _M0L6_2atmpS1338);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1337
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS126);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS126);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_3337 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1337);
    moonbit_decref_cycle_free(_M0L6_2atmpS1337);
    return _result_3337;
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
  struct _M0TPB13StringBuilder* _block_3338;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS111 < 1) {
    _M0L7initialS110 = 1;
  } else {
    int32_t _M0L6_2atmpS1333 = _M0L10size__hintS111 + 1;
    _M0L7initialS110 = _M0L6_2atmpS1333 / 2;
  }
  _M0L4dataS112 = (uint16_t*)moonbit_make_string(_M0L7initialS110, 0);
  _block_3338
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_3338)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 142, 0);
  _block_3338->$0 = _M0L4dataS112;
  _block_3338->$1 = 0;
  return _block_3338;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS109) {
  int32_t _M0L6_2atmpS1332;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1332 = (int32_t)_M0L4selfS109;
  return _M0L6_2atmpS1332;
}

uint8_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGbE(
  uint8_t* _M0L3srcS95,
  int32_t _M0L13allocate__lenS91,
  int32_t _M0L3lenS92,
  int32_t _M0L11src__offsetS93,
  int32_t _M0L11dst__offsetS94
) {
  int32_t _if__result_3339;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS91 >= 0) {
    if (_M0L3lenS92 >= 0) {
      if (_M0L11src__offsetS93 >= 0) {
        if (_M0L11dst__offsetS94 >= 0) {
          int32_t _M0L6_2atmpS1318 = _M0L11src__offsetS93 + _M0L3lenS92;
          int32_t _M0L6_2atmpS1319;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1319
          = _M0MPB18UninitializedArray6lengthGbE(_M0L3srcS95);
          if (_M0L6_2atmpS1318 <= _M0L6_2atmpS1319) {
            int32_t _M0L6_2atmpS1317 = _M0L11dst__offsetS94 + _M0L3lenS92;
            _if__result_3339 = _M0L6_2atmpS1317 <= _M0L13allocate__lenS91;
          } else {
            _if__result_3339 = 0;
          }
        } else {
          _if__result_3339 = 0;
        }
      } else {
        _if__result_3339 = 0;
      }
    } else {
      _if__result_3339 = 0;
    }
  } else {
    _if__result_3339 = 0;
  }
  if (_if__result_3339) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGbE(_M0L3srcS95, _M0L13allocate__lenS91, _M0L11src__offsetS93, _M0L11dst__offsetS94, _M0L3lenS92);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS96;
    int32_t _M0L6_2atmpS1321;
    moonbit_string_t _M0L6_2atmpS1320;
    uint8_t* _result_3340;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS96
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L13allocate__lenS91);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L11src__offsetS93);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L11dst__offsetS94);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L3lenS92);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS96, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1321 = _M0MPB18UninitializedArray6lengthGbE(_M0L3srcS95);
    moonbit_decref_cycle_free(_M0L3srcS95);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS96, _M0L6_2atmpS1321);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1320
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS96);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS96);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_3340
    = _M0FPC15abort5abortGRPB18UninitializedArrayGbEE(_M0L6_2atmpS1320);
    moonbit_decref_cycle_free(_M0L6_2atmpS1320);
    return _result_3340;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS101,
  int32_t _M0L13allocate__lenS97,
  int32_t _M0L3lenS98,
  int32_t _M0L11src__offsetS99,
  int32_t _M0L11dst__offsetS100
) {
  int32_t _if__result_3341;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS97 >= 0) {
    if (_M0L3lenS98 >= 0) {
      if (_M0L11src__offsetS99 >= 0) {
        if (_M0L11dst__offsetS100 >= 0) {
          int32_t _M0L6_2atmpS1323 = _M0L11src__offsetS99 + _M0L3lenS98;
          int32_t _M0L6_2atmpS1324;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1324
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS101);
          if (_M0L6_2atmpS1323 <= _M0L6_2atmpS1324) {
            int32_t _M0L6_2atmpS1322 = _M0L11dst__offsetS100 + _M0L3lenS98;
            _if__result_3341 = _M0L6_2atmpS1322 <= _M0L13allocate__lenS97;
          } else {
            _if__result_3341 = 0;
          }
        } else {
          _if__result_3341 = 0;
        }
      } else {
        _if__result_3341 = 0;
      }
    } else {
      _if__result_3341 = 0;
    }
  } else {
    _if__result_3341 = 0;
  }
  if (_if__result_3341) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS97, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS101, _M0L11src__offsetS99, _M0L11dst__offsetS100, _M0L3lenS98);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS102;
    int32_t _M0L6_2atmpS1326;
    moonbit_string_t _M0L6_2atmpS1325;
    moonbit_string_t* _result_3342;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS102
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L13allocate__lenS97);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L11src__offsetS99);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L11dst__offsetS100);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L3lenS98);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS102, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1326 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS101);
    moonbit_decref_cycle_free(_M0L3srcS101);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS102, _M0L6_2atmpS1326);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1325
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS102);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS102);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_3342
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1325);
    moonbit_decref_cycle_free(_M0L6_2atmpS1325);
    return _result_3342;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS107,
  int32_t _M0L13allocate__lenS103,
  int32_t _M0L3lenS104,
  int32_t _M0L11src__offsetS105,
  int32_t _M0L11dst__offsetS106
) {
  int32_t _if__result_3343;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS103 >= 0) {
    if (_M0L3lenS104 >= 0) {
      if (_M0L11src__offsetS105 >= 0) {
        if (_M0L11dst__offsetS106 >= 0) {
          int32_t _M0L6_2atmpS1328 = _M0L11src__offsetS105 + _M0L3lenS104;
          int32_t _M0L6_2atmpS1329;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1329
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS107);
          if (_M0L6_2atmpS1328 <= _M0L6_2atmpS1329) {
            int32_t _M0L6_2atmpS1327 = _M0L11dst__offsetS106 + _M0L3lenS104;
            _if__result_3343 = _M0L6_2atmpS1327 <= _M0L13allocate__lenS103;
          } else {
            _if__result_3343 = 0;
          }
        } else {
          _if__result_3343 = 0;
        }
      } else {
        _if__result_3343 = 0;
      }
    } else {
      _if__result_3343 = 0;
    }
  } else {
    _if__result_3343 = 0;
  }
  if (_if__result_3343) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS103, 0, _M0L3srcS107, _M0L11src__offsetS105, _M0L11dst__offsetS106, _M0L3lenS104);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS108;
    int32_t _M0L6_2atmpS1331;
    moonbit_string_t _M0L6_2atmpS1330;
    struct _M0TUsiE** _result_3344;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS108
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L13allocate__lenS103);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L11src__offsetS105);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_34.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L11dst__offsetS106);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_35.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L3lenS104);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS108, (moonbit_string_t)moonbit_string_literal_36.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1331 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS107);
    moonbit_decref_cycle_free(_M0L3srcS107);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS108, _M0L6_2atmpS1331);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1330
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS108);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS108);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_3344
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1330);
    moonbit_decref_cycle_free(_M0L6_2atmpS1330);
    return _result_3344;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS86,
  moonbit_string_t _M0L3objS85
) {
  struct _M0TPB6Logger _M0L6_2atmpS1314;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS86);
  _M0L6_2atmpS1314
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS86
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS85, _M0L6_2atmpS1314);
  if (_M0L6_2atmpS1314.$1) {
    moonbit_decref(_M0L6_2atmpS1314.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS88,
  int32_t _M0L3objS87
) {
  struct _M0TPB6Logger _M0L6_2atmpS1315;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS88);
  _M0L6_2atmpS1315
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS88
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS87, _M0L6_2atmpS1315);
  if (_M0L6_2atmpS1315.$1) {
    moonbit_decref(_M0L6_2atmpS1315.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS90,
  uint64_t _M0L3objS89
) {
  struct _M0TPB6Logger _M0L6_2atmpS1316;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS90);
  _M0L6_2atmpS1316
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS90
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS89, _M0L6_2atmpS1316);
  if (_M0L6_2atmpS1316.$1) {
    moonbit_decref(_M0L6_2atmpS1316.$1);
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
        int32_t _M0L6_2atmpS1278 = _M0L11dst__offsetS18 + _M0L1iS20;
        int32_t _M0L6_2atmpS1280 = _M0L11src__offsetS19 + _M0L1iS20;
        int32_t _M0L6_2atmpS1279;
        int32_t _M0L6_2atmpS1281;
        if (
          _M0L6_2atmpS1280 < 0
          || _M0L6_2atmpS1280 >= Moonbit_array_length(_M0L3srcS17)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1279 = (int32_t)_M0L3srcS17[_M0L6_2atmpS1280];
        if (
          _M0L6_2atmpS1278 < 0
          || _M0L6_2atmpS1278 >= Moonbit_array_length(_M0L3dstS16)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS16[_M0L6_2atmpS1278] = _M0L6_2atmpS1279;
        _M0L6_2atmpS1281 = _M0L1iS20 + 1;
        _M0L1iS20 = _M0L6_2atmpS1281;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS17);
        moonbit_decref_cycle_free(_M0L3dstS16);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1286 = _M0L3lenS21 - 1;
    int32_t _M0L1iS23 = _M0L6_2atmpS1286;
    while (1) {
      if (_M0L1iS23 >= 0) {
        int32_t _M0L6_2atmpS1282 = _M0L11dst__offsetS18 + _M0L1iS23;
        int32_t _M0L6_2atmpS1284 = _M0L11src__offsetS19 + _M0L1iS23;
        int32_t _M0L6_2atmpS1283;
        int32_t _M0L6_2atmpS1285;
        if (
          _M0L6_2atmpS1284 < 0
          || _M0L6_2atmpS1284 >= Moonbit_array_length(_M0L3srcS17)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1283 = (int32_t)_M0L3srcS17[_M0L6_2atmpS1284];
        if (
          _M0L6_2atmpS1282 < 0
          || _M0L6_2atmpS1282 >= Moonbit_array_length(_M0L3dstS16)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS16[_M0L6_2atmpS1282] = _M0L6_2atmpS1283;
        _M0L6_2atmpS1285 = _M0L1iS23 - 1;
        _M0L1iS23 = _M0L6_2atmpS1285;
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
        int32_t _M0L6_2atmpS1287 = _M0L11dst__offsetS27 + _M0L1iS29;
        int32_t _M0L6_2atmpS1289 = _M0L11src__offsetS28 + _M0L1iS29;
        int32_t _M0L6_2atmpS1288;
        int32_t _M0L6_2atmpS1290;
        if (
          _M0L6_2atmpS1289 < 0
          || _M0L6_2atmpS1289 >= Moonbit_array_length(_M0L3srcS26)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1288 = (int32_t)_M0L3srcS26[_M0L6_2atmpS1289];
        if (
          _M0L6_2atmpS1287 < 0
          || _M0L6_2atmpS1287 >= Moonbit_array_length(_M0L3dstS25)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS25[_M0L6_2atmpS1287] = _M0L6_2atmpS1288;
        _M0L6_2atmpS1290 = _M0L1iS29 + 1;
        _M0L1iS29 = _M0L6_2atmpS1290;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS26);
        moonbit_decref_cycle_free(_M0L3dstS25);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1295 = _M0L3lenS30 - 1;
    int32_t _M0L1iS32 = _M0L6_2atmpS1295;
    while (1) {
      if (_M0L1iS32 >= 0) {
        int32_t _M0L6_2atmpS1291 = _M0L11dst__offsetS27 + _M0L1iS32;
        int32_t _M0L6_2atmpS1293 = _M0L11src__offsetS28 + _M0L1iS32;
        int32_t _M0L6_2atmpS1292;
        int32_t _M0L6_2atmpS1294;
        if (
          _M0L6_2atmpS1293 < 0
          || _M0L6_2atmpS1293 >= Moonbit_array_length(_M0L3srcS26)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1292 = (int32_t)_M0L3srcS26[_M0L6_2atmpS1293];
        if (
          _M0L6_2atmpS1291 < 0
          || _M0L6_2atmpS1291 >= Moonbit_array_length(_M0L3dstS25)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS25[_M0L6_2atmpS1291] = _M0L6_2atmpS1292;
        _M0L6_2atmpS1294 = _M0L1iS32 - 1;
        _M0L1iS32 = _M0L6_2atmpS1294;
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
        int32_t _M0L6_2atmpS1296 = _M0L11dst__offsetS36 + _M0L1iS38;
        int32_t _M0L6_2atmpS1298 = _M0L11src__offsetS37 + _M0L1iS38;
        moonbit_string_t _M0L6_2atmpS1297;
        moonbit_string_t _M0L6_2aoldS3188;
        int32_t _M0L6_2atmpS1299;
        if (
          _M0L6_2atmpS1298 < 0
          || _M0L6_2atmpS1298 >= Moonbit_array_length(_M0L3srcS35)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1297 = (moonbit_string_t)_M0L3srcS35[_M0L6_2atmpS1298];
        if (
          _M0L6_2atmpS1296 < 0
          || _M0L6_2atmpS1296 >= Moonbit_array_length(_M0L3dstS34)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS3188 = (moonbit_string_t)_M0L3dstS34[_M0L6_2atmpS1296];
        moonbit_incref_cycle_free(_M0L6_2atmpS1297);
        moonbit_decref_cycle_free(_M0L6_2aoldS3188);
        _M0L3dstS34[_M0L6_2atmpS1296] = _M0L6_2atmpS1297;
        _M0L6_2atmpS1299 = _M0L1iS38 + 1;
        _M0L1iS38 = _M0L6_2atmpS1299;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS35);
        moonbit_decref_cycle_free(_M0L3dstS34);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1304 = _M0L3lenS39 - 1;
    int32_t _M0L1iS41 = _M0L6_2atmpS1304;
    while (1) {
      if (_M0L1iS41 >= 0) {
        int32_t _M0L6_2atmpS1300 = _M0L11dst__offsetS36 + _M0L1iS41;
        int32_t _M0L6_2atmpS1302 = _M0L11src__offsetS37 + _M0L1iS41;
        moonbit_string_t _M0L6_2atmpS1301;
        moonbit_string_t _M0L6_2aoldS3189;
        int32_t _M0L6_2atmpS1303;
        if (
          _M0L6_2atmpS1302 < 0
          || _M0L6_2atmpS1302 >= Moonbit_array_length(_M0L3srcS35)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1301 = (moonbit_string_t)_M0L3srcS35[_M0L6_2atmpS1302];
        if (
          _M0L6_2atmpS1300 < 0
          || _M0L6_2atmpS1300 >= Moonbit_array_length(_M0L3dstS34)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS3189 = (moonbit_string_t)_M0L3dstS34[_M0L6_2atmpS1300];
        moonbit_incref_cycle_free(_M0L6_2atmpS1301);
        moonbit_decref_cycle_free(_M0L6_2aoldS3189);
        _M0L3dstS34[_M0L6_2atmpS1300] = _M0L6_2atmpS1301;
        _M0L6_2atmpS1303 = _M0L1iS41 - 1;
        _M0L1iS41 = _M0L6_2atmpS1303;
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
        int32_t _M0L6_2atmpS1305 = _M0L11dst__offsetS45 + _M0L1iS47;
        int32_t _M0L6_2atmpS1307 = _M0L11src__offsetS46 + _M0L1iS47;
        struct _M0TUsiE* _M0L6_2atmpS1306;
        struct _M0TUsiE* _M0L6_2aoldS3190;
        int32_t _M0L6_2atmpS1308;
        if (
          _M0L6_2atmpS1307 < 0
          || _M0L6_2atmpS1307 >= Moonbit_array_length(_M0L3srcS44)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1306 = (struct _M0TUsiE*)_M0L3srcS44[_M0L6_2atmpS1307];
        if (
          _M0L6_2atmpS1305 < 0
          || _M0L6_2atmpS1305 >= Moonbit_array_length(_M0L3dstS43)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS3190 = (struct _M0TUsiE*)_M0L3dstS43[_M0L6_2atmpS1305];
        if (_M0L6_2atmpS1306) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1306);
        }
        if (_M0L6_2aoldS3190) {
          moonbit_decref_cycle_free(_M0L6_2aoldS3190);
        }
        _M0L3dstS43[_M0L6_2atmpS1305] = _M0L6_2atmpS1306;
        _M0L6_2atmpS1308 = _M0L1iS47 + 1;
        _M0L1iS47 = _M0L6_2atmpS1308;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS44);
        moonbit_decref_cycle_free(_M0L3dstS43);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1313 = _M0L3lenS48 - 1;
    int32_t _M0L1iS50 = _M0L6_2atmpS1313;
    while (1) {
      if (_M0L1iS50 >= 0) {
        int32_t _M0L6_2atmpS1309 = _M0L11dst__offsetS45 + _M0L1iS50;
        int32_t _M0L6_2atmpS1311 = _M0L11src__offsetS46 + _M0L1iS50;
        struct _M0TUsiE* _M0L6_2atmpS1310;
        struct _M0TUsiE* _M0L6_2aoldS3191;
        int32_t _M0L6_2atmpS1312;
        if (
          _M0L6_2atmpS1311 < 0
          || _M0L6_2atmpS1311 >= Moonbit_array_length(_M0L3srcS44)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1310 = (struct _M0TUsiE*)_M0L3srcS44[_M0L6_2atmpS1311];
        if (
          _M0L6_2atmpS1309 < 0
          || _M0L6_2atmpS1309 >= Moonbit_array_length(_M0L3dstS43)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS3191 = (struct _M0TUsiE*)_M0L3dstS43[_M0L6_2atmpS1309];
        if (_M0L6_2atmpS1310) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1310);
        }
        if (_M0L6_2aoldS3191) {
          moonbit_decref_cycle_free(_M0L6_2aoldS3191);
        }
        _M0L3dstS43[_M0L6_2atmpS1309] = _M0L6_2atmpS1310;
        _M0L6_2atmpS1312 = _M0L1iS50 - 1;
        _M0L1iS50 = _M0L6_2atmpS1312;
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
  _M0L10_2ax__6388S12.$0->$method_0(_M0L10_2ax__6388S12.$1, (moonbit_string_t)moonbit_string_literal_37.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S12, _M0L15_2a_2aarg__6389S11);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S12.$0->$method_0(_M0L10_2ax__6388S12.$1, (moonbit_string_t)moonbit_string_literal_38.data);
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1249) {
  switch (Moonbit_object_tag(_M0L4_2aeS1249)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_39.data;
      break;
    }
    
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_40.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1249);
      break;
    }
    
    case 3: {
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
  void* _M0L11_2aobj__ptrS1273,
  struct _M0TPB4Show _M0L8_2aparamS1272
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1271 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1273;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1271, _M0L8_2aparamS1272);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1270,
  struct _M0TPB4Show _M0L8_2aparamS1269
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1268 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1270;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1268, _M0L8_2aparamS1269);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1267,
  int32_t _M0L8_2aparamS1266
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1265 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1267;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1265, _M0L8_2aparamS1266);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1264,
  struct _M0TPC16string10StringView _M0L8_2aparamS1263
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1262 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1264;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1262, _M0L8_2aparamS1263);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1261,
  moonbit_string_t _M0L8_2aparamS1258,
  int32_t _M0L8_2aparamS1259,
  int32_t _M0L8_2aparamS1260
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1257 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1261;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1257, _M0L8_2aparamS1258, _M0L8_2aparamS1259, _M0L8_2aparamS1260);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1256,
  moonbit_string_t _M0L8_2aparamS1255
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1254 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1256;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1254, _M0L8_2aparamS1255);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1277;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1242;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1243;
  int32_t _M0L7_2abindS1244;
  struct _M0TUsiE** _M0L7_2abindS1245;
  int32_t _M0L6_2acntS3196;
  int32_t _M0L2__S1246;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1277
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1242
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1242)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 145, 0);
  _M0L12async__testsS1242->$0 = _M0L6_2atmpS1277;
  _M0L12async__testsS1242->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1243
  = _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1244 = _M0L7_2abindS1243->$1;
  _M0L7_2abindS1245 = _M0L7_2abindS1243->$0;
  _M0L6_2acntS3196
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1243));
  if (_M0L6_2acntS3196 > 1) {
    int32_t _M0L11_2anew__cntS3197 = _M0L6_2acntS3196 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1243), _M0L11_2anew__cntS3197);
    moonbit_incref_cycle_free(_M0L7_2abindS1245);
  } else if (_M0L6_2acntS3196 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1243);
  }
  _M0L2__S1246 = 0;
  while (1) {
    if (_M0L2__S1246 < _M0L7_2abindS1244) {
      struct _M0TUsiE* _M0L3argS1247 =
        (struct _M0TUsiE*)_M0L7_2abindS1245[_M0L2__S1246];
      moonbit_string_t _M0L6_2atmpS1274 = _M0L3argS1247->$0;
      int32_t _M0L6_2atmpS1275 = _M0L3argS1247->$1;
      int32_t _M0L6_2atmpS1276;
      moonbit_incref_cycle_free(_M0L6_2atmpS1274);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples23stimuli__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1242, _M0L6_2atmpS1274, _M0L6_2atmpS1275);
      moonbit_decref_cycle_free(_M0L6_2atmpS1274);
      _M0L6_2atmpS1276 = _M0L2__S1246 + 1;
      _M0L2__S1246 = _M0L6_2atmpS1276;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1245);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stimuli\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples23stimuli__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples23stimuli__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1242);
  moonbit_decref_cycle_free(_M0L12async__testsS1242);
  moonbit_flush_cycles();
  return 0;
}