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

struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables;

struct _M0TWRPC15error5ErrorEs;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0BTPB6Logger;

struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep;

struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep;

struct _M0TP26RiantR8snn__mbt2IF;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1029;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1034;

struct _M0TP26RiantR8snn__mbt7Xoshiro;

struct _M0TUmmmmE;

struct _M0TPB5ArrayGiE;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TWRPC15error5ErrorEu;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TPB8MutLocalGiE;

struct _M0TP26RiantR8snn__mbt14SpikingSynapse;

struct _M0TPB4Show;

struct _M0TPB8MutLocalGfE;

struct _M0TP26RiantR8snn__mbt9PostSpike;

struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TPB5ArrayGbE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0TPB8MutLocalGbE;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB5ArrayGsE;

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR;

struct _M0TWEu;

struct _M0TP26RiantR8snn__mbt11IFParameter;

struct _M0TPB7Umul128;

struct _M0TPB8Pow5Pair;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure {
  moonbit_string_t $0;
  
};

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError {
  moonbit_string_t $0;
  
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

struct _M0BTPB6Logger {
  int32_t(* $method_0)(void*, moonbit_string_t);
  int32_t(* $method_1)(void*, moonbit_string_t, int32_t, int32_t);
  int32_t(* $method_2)(void*, struct _M0TPC16string10StringView);
  int32_t(* $method_3)(void*, int32_t);
  int32_t(* $method_4)(void*, struct _M0TPB4Show);
  int32_t(* $method_5)(void*, struct _M0TPB4Show);
  
};

struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep {
  int32_t $0;
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* $1;
  struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* $2;
  
};

struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  
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

struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1029 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok {
  int32_t $0;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
};

struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1034 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

struct _M0TPB8MutLocalGiE {
  int32_t $0;
  
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

struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
};

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error {
  struct moonbit_result_0(* code)(
    struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error*,
    struct _M0TWuEu*,
    struct _M0TWRPC15error5ErrorEu*
  );
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1041(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1034(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1029(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1006(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S999(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

int32_t _M0MP26RiantR8snn__mbt14SpikingSynapse9init__rho(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse*
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

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t
);

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t);

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t);

int32_t _M0FP26RiantR8snn__mbt28markram__stp__step__timestep(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse*,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables*,
  struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep*,
  float,
  float
);

struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _M0MP26RiantR8snn__mbt23MarkramSTPEntryTimestep3new(
  int32_t,
  int32_t,
  int32_t,
  struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep*
);

struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _M0MP26RiantR8snn__mbt23MarkramSTPEntryTimestep11new_2einner(
  int32_t,
  int32_t,
  int32_t,
  struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep*
);

struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0MP26RiantR8snn__mbt27MarkramSTPParameterTimestep3new(
  
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

int32_t _M0MPC15float5Float7is__nan(float);

int32_t _M0MPC15float5Float7is__inf(float);

int32_t _M0MPC15float5Float12is__pos__inf(float);

int32_t _M0MPC15float5Float12is__neg__inf(float);

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

int32_t _M0MPC15array5Array5clearGfE(struct _M0TPB5ArrayGfE*);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(int32_t, int32_t);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(int32_t, int32_t);

int32_t _M0MPC15array5Array3setGbE(struct _M0TPB5ArrayGbE*, int32_t, int32_t);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

int32_t _M0MPC15array5Array3setGiE(struct _M0TPB5ArrayGiE*, int32_t, int32_t);

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

int32_t _M0MPC15array5Array2atGbE(struct _M0TPB5ArrayGbE*, int32_t);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array28unsafe__truncate__to__lengthGfE(
  struct _M0TPB5ArrayGfE*,
  int32_t
);

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

moonbit_string_t _M0IPC14bool4BoolPB4Show10to__string(int32_t);

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

int32_t _M0MPC15array5Array7reallocGiE(struct _M0TPB5ArrayGiE*, int32_t);

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

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE*,
  int32_t
);

int32_t _M0MPC15array5Array8capacityGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array8capacityGsE(struct _M0TPB5ArrayGsE*);

int32_t _M0MPC15array5Array8capacityGUsiEE(struct _M0TPB5ArrayGUsiEE*);

int32_t _M0MPC15array5Array8capacityGiE(struct _M0TPB5ArrayGiE*);

int32_t _M0FPB23array__growth__capacity(int32_t, int32_t, int32_t);

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE*);

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

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t*,
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

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t*,
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

int32_t _M0MPB18UninitializedArray6lengthGfE(float*);

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t*);

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(struct _M0TUsiE**);

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t*);

int32_t _M0IPB7FailurePB4Show6output(void*, struct _M0TPB6Logger);

int32_t _M0MPB6Logger13write__objectGsE(
  struct _M0TPB6Logger,
  moonbit_string_t
);

int32_t _M0FPC15abort5abortGuE(moonbit_string_t);

uint16_t* _M0FPC15abort5abortGAkE(moonbit_string_t);

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(moonbit_string_t);

moonbit_string_t* _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(
  moonbit_string_t
);

struct _M0TUsiE** _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(
  moonbit_string_t
);

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(moonbit_string_t);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[119]; 
} const moonbit_string_literal_37 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 118, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 115, 116, 112, 95, 116, 105, 109, 
    101, 115, 116, 101, 112, 95, 98, 108, 97, 99, 107, 98, 111, 120, 
    95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 
    101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 
    110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 46, 77, 111, 
    111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 
    114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 
    101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_26 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[117]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 116, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 115, 116, 112, 95, 116, 105, 109, 
    101, 115, 116, 101, 112, 95, 98, 108, 97, 99, 107, 98, 111, 120, 
    95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 
    101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 
    110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 46, 77, 111, 111, 
    110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 
    73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 
    114, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[43]; 
} const moonbit_string_literal_9 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 42, 105, 110, 
    100, 101, 120, 32, 111, 117, 116, 32, 111, 102, 32, 98, 111, 117, 
    110, 100, 115, 58, 32, 116, 104, 101, 32, 108, 101, 110, 32, 105, 
    115, 32, 102, 114, 111, 109, 32, 48, 32, 116, 111, 32, 0
  };

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

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_10 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 32, 98, 
    117, 116, 32, 116, 104, 101, 32, 105, 110, 100, 101, 120, 32, 105, 
    115, 32, 0
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
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1041$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1041
  };

uint32_t const moonbit_layout_table_data[91] =
  {
    sizeof(struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1029)
    / 4, 1,
    offsetof(struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1029, $1)
    / 4
    * 2,
    sizeof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1034)
    / 4, 1,
    offsetof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1034, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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
    sizeof(struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables) / 4, 5,
    offsetof(struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables, $5) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables, $6) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep) / 4, 
    2,
    offsetof(struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep, $1)
    / 4
    * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep, $2)
    / 4
    * 2, sizeof(struct _M0TPC16string10StringView) / 4, 1,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

float _M0FPC15float10min__value;

float _M0FPC15float10max__value;

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS2178
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1062,
  moonbit_string_t _M0L8filenameS1031,
  int32_t _M0L5indexS1033
) {
  struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1029* _closure_2203;
  struct _M0TWEu* _M0L13handle__startS1029;
  struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1034* _closure_2204;
  struct _M0TWssbEu* _M0L14handle__resultS1034;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1041;
  void* _M0L11_2atry__errS1056;
  struct moonbit_result_0 _tmp_2206;
  int32_t _handle__error__result_2207;
  int32_t _M0L6_2atmpS2166;
  void* _M0L3errS1057;
  moonbit_string_t _M0L4nameS1059;
  struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1060;
  moonbit_string_t _M0L7_2anameS1061;
  int32_t _M0L6_2acntS2197;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1031);
  _closure_2203
  = (struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1029*)moonbit_malloc(sizeof(struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1029));
  Moonbit_object_header(_closure_2203)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_2203->code
  = &_M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1029;
  _closure_2203->$0 = _M0L5indexS1033;
  _closure_2203->$1 = _M0L8filenameS1031;
  _M0L13handle__startS1029 = (struct _M0TWEu*)_closure_2203;
  moonbit_incref_cycle_free(_M0L8filenameS1031);
  _closure_2204
  = (struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1034*)moonbit_malloc(sizeof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1034));
  Moonbit_object_header(_closure_2204)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_2204->code
  = &_M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1034;
  _closure_2204->$0 = _M0L5indexS1033;
  _closure_2204->$1 = _M0L8filenameS1031;
  _M0L14handle__resultS1034 = (struct _M0TWssbEu*)_closure_2204;
  _M0L17error__to__stringS1041
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1041$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _tmp_2206
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1062, _M0L8filenameS1031, _M0L5indexS1033, _M0L13handle__startS1029, _M0L14handle__resultS1034, _M0L17error__to__stringS1041);
  if (_tmp_2206.tag) {
    int32_t const _M0L5_2aokS2175 = _tmp_2206.data.ok;
    _handle__error__result_2207 = _M0L5_2aokS2175;
  } else {
    void* const _M0L6_2aerrS2176 = _tmp_2206.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1041);
    moonbit_decref_cycle_free(_M0L13handle__startS1029);
    _M0L11_2atry__errS1056 = _M0L6_2aerrS2176;
    goto join_1055;
  }
  if (_handle__error__result_2207) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1041);
    moonbit_decref_cycle_free(_M0L13handle__startS1029);
    _M0L6_2atmpS2166 = 1;
  } else {
    struct moonbit_result_0 _tmp_2208;
    int32_t _handle__error__result_2209;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
    _tmp_2208
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1062, _M0L8filenameS1031, _M0L5indexS1033, _M0L13handle__startS1029, _M0L14handle__resultS1034, _M0L17error__to__stringS1041);
    if (_tmp_2208.tag) {
      int32_t const _M0L5_2aokS2173 = _tmp_2208.data.ok;
      _handle__error__result_2209 = _M0L5_2aokS2173;
    } else {
      void* const _M0L6_2aerrS2174 = _tmp_2208.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1041);
      moonbit_decref_cycle_free(_M0L13handle__startS1029);
      _M0L11_2atry__errS1056 = _M0L6_2aerrS2174;
      goto join_1055;
    }
    if (_handle__error__result_2209) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1041);
      moonbit_decref_cycle_free(_M0L13handle__startS1029);
      _M0L6_2atmpS2166 = 1;
    } else {
      struct moonbit_result_0 _tmp_2210;
      int32_t _handle__error__result_2211;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
      _tmp_2210
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1062, _M0L8filenameS1031, _M0L5indexS1033, _M0L13handle__startS1029, _M0L14handle__resultS1034, _M0L17error__to__stringS1041);
      if (_tmp_2210.tag) {
        int32_t const _M0L5_2aokS2171 = _tmp_2210.data.ok;
        _handle__error__result_2211 = _M0L5_2aokS2171;
      } else {
        void* const _M0L6_2aerrS2172 = _tmp_2210.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1041);
        moonbit_decref_cycle_free(_M0L13handle__startS1029);
        _M0L11_2atry__errS1056 = _M0L6_2aerrS2172;
        goto join_1055;
      }
      if (_handle__error__result_2211) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1041);
        moonbit_decref_cycle_free(_M0L13handle__startS1029);
        _M0L6_2atmpS2166 = 1;
      } else {
        struct moonbit_result_0 _tmp_2212;
        int32_t _handle__error__result_2213;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
        _tmp_2212
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1062, _M0L8filenameS1031, _M0L5indexS1033, _M0L13handle__startS1029, _M0L14handle__resultS1034, _M0L17error__to__stringS1041);
        if (_tmp_2212.tag) {
          int32_t const _M0L5_2aokS2169 = _tmp_2212.data.ok;
          _handle__error__result_2213 = _M0L5_2aokS2169;
        } else {
          void* const _M0L6_2aerrS2170 = _tmp_2212.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1041);
          moonbit_decref_cycle_free(_M0L13handle__startS1029);
          _M0L11_2atry__errS1056 = _M0L6_2aerrS2170;
          goto join_1055;
        }
        if (_handle__error__result_2213) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1041);
          moonbit_decref_cycle_free(_M0L13handle__startS1029);
          _M0L6_2atmpS2166 = 1;
        } else {
          struct moonbit_result_0 _tmp_2214;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
          _tmp_2214
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1062, _M0L8filenameS1031, _M0L5indexS1033, _M0L13handle__startS1029, _M0L14handle__resultS1034, _M0L17error__to__stringS1041);
          moonbit_decref_cycle_free(_M0L13handle__startS1029);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1041);
          if (_tmp_2214.tag) {
            int32_t const _M0L5_2aokS2167 = _tmp_2214.data.ok;
            _M0L6_2atmpS2166 = _M0L5_2aokS2167;
          } else {
            void* const _M0L6_2aerrS2168 = _tmp_2214.data.err;
            _M0L11_2atry__errS1056 = _M0L6_2aerrS2168;
            goto join_1055;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS2166) {
    void* _M0L132RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2177 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L132RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2177)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L132RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2177)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1056
    = _M0L132RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS2177;
    goto join_1055;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1034);
  }
  goto joinlet_2205;
  join_1055:;
  _M0L3errS1057 = _M0L11_2atry__errS1056;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1060
  = (struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1057;
  _M0L7_2anameS1061 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1060->$0;
  _M0L6_2acntS2197
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1060));
  if (_M0L6_2acntS2197 > 1) {
    int32_t _M0L11_2anew__cntS2198 = _M0L6_2acntS2197 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1060), _M0L11_2anew__cntS2198);
    moonbit_incref_cycle_free(_M0L7_2anameS1061);
  } else if (_M0L6_2acntS2197 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1060);
  }
  _M0L4nameS1059 = _M0L7_2anameS1061;
  goto join_1058;
  goto joinlet_2215;
  join_1058:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1034(_M0L14handle__resultS1034, _M0L4nameS1059, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1034);
  moonbit_decref_cycle_free(_M0L4nameS1059);
  joinlet_2215:;
  joinlet_2205:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1041(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS2165,
  void* _M0L3errS1042
) {
  void* _M0L1eS1044;
  moonbit_string_t _M0L1eS1046;
  moonbit_string_t _result_2218;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1042)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1047 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1042;
      moonbit_string_t _M0L4_2aeS1048 = _M0L10_2aFailureS1047->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1048);
      _M0L1eS1046 = _M0L4_2aeS1048;
      goto join_1045;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1049 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1042;
      moonbit_string_t _M0L4_2aeS1050 = _M0L15_2aInspectErrorS1049->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1050);
      _M0L1eS1046 = _M0L4_2aeS1050;
      goto join_1045;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1051 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1042;
      moonbit_string_t _M0L4_2aeS1052 = _M0L16_2aSnapshotErrorS1051->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1052);
      _M0L1eS1046 = _M0L4_2aeS1052;
      goto join_1045;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1053 =
        (struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1042;
      moonbit_string_t _M0L4_2aeS1054 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1053->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1054);
      _M0L1eS1046 = _M0L4_2aeS1054;
      goto join_1045;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1042);
      _M0L1eS1044 = _M0L3errS1042;
      goto join_1043;
      break;
    }
  }
  join_1045:;
  return _M0L1eS1046;
  join_1043:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _result_2218 = _M0FP15Error10to__string(_M0L1eS1044);
  moonbit_decref_cycle_free(_M0L1eS1044);
  return _result_2218;
}

int32_t _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1034(
  struct _M0TWssbEu* _M0L6_2aenvS2162,
  moonbit_string_t _M0L10__testnameS1035,
  moonbit_string_t _M0L7messageS1036,
  int32_t _M0L7skippedS1037
) {
  struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1034* _M0L14_2acasted__envS2163;
  moonbit_string_t _M0L8filenameS1031;
  int32_t _M0L5indexS1033;
  moonbit_string_t _M0L10file__nameS1038;
  moonbit_string_t _M0L7messageS1039;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1040;
  moonbit_string_t _M0L6_2atmpS2164;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2163
  = (struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1034*)_M0L6_2aenvS2162;
  _M0L8filenameS1031 = _M0L14_2acasted__envS2163->$1;
  _M0L5indexS1033 = _M0L14_2acasted__envS2163->$0;
  if (!_M0L7skippedS1037 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1038
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1031, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1039
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1036, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1040
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1040, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1040, _M0L10file__nameS1038);
  moonbit_decref_cycle_free(_M0L10file__nameS1038);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1040, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1040, _M0L5indexS1033);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1040, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1040, _M0L7messageS1039);
  moonbit_decref_cycle_free(_M0L7messageS1039);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1040, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2164
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1040);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1040);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2164);
  moonbit_decref_cycle_free(_M0L6_2atmpS2164);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1029(
  struct _M0TWEu* _M0L6_2aenvS2159
) {
  struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1029* _M0L14_2acasted__envS2160;
  moonbit_string_t _M0L8filenameS1031;
  int32_t _M0L5indexS1033;
  moonbit_string_t _M0L10file__nameS1030;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1032;
  moonbit_string_t _M0L6_2atmpS2161;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS2160
  = (struct _M0R133_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1029*)_M0L6_2aenvS2159;
  _M0L8filenameS1031 = _M0L14_2acasted__envS2160->$1;
  _M0L5indexS1033 = _M0L14_2acasted__envS2160->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1030
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1031, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1032
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1032, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1032, _M0L10file__nameS1030);
  moonbit_decref_cycle_free(_M0L10file__nameS1030);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1032, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1032, _M0L5indexS1033);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1032, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2161
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1032);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1032);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS2161);
  moonbit_decref_cycle_free(_M0L6_2atmpS2161);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S999;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1006;
  struct _M0TUsiE** _M0L6_2atmpS2158;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1013;
  moonbit_string_t* _M0L9cli__argsS1014;
  moonbit_string_t _M0L6_2atmpS2157;
  moonbit_string_t _M0L6_2atmpS2156;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1015;
  int32_t _M0L7_2abindS1016;
  moonbit_string_t* _M0L7_2abindS1017;
  int32_t _M0L6_2acntS2199;
  int32_t _M0L2__S1018;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S999 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1006 = 0;
  _M0L6_2atmpS2158 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1013
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1013)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1013->$0 = _M0L6_2atmpS2158;
  _M0L16file__and__indexS1013->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1014
  = _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1014)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS2157 = (moonbit_string_t)_M0L9cli__argsS1014[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS2157);
  moonbit_decref_cycle_free(_M0L9cli__argsS1014);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2156
  = _M0MP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS2157);
  moonbit_decref_cycle_free(_M0L6_2atmpS2157);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1015
  = _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1006(_M0L51moonbit__test__driver__internal__split__mbt__stringS1006, _M0L6_2atmpS2156, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS2156);
  _M0L7_2abindS1016 = _M0L10test__argsS1015->$1;
  _M0L7_2abindS1017 = _M0L10test__argsS1015->$0;
  _M0L6_2acntS2199
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1015));
  if (_M0L6_2acntS2199 > 1) {
    int32_t _M0L11_2anew__cntS2200 = _M0L6_2acntS2199 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1015), _M0L11_2anew__cntS2200);
    moonbit_incref_cycle_free(_M0L7_2abindS1017);
  } else if (_M0L6_2acntS2199 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1015);
  }
  _M0L2__S1018 = 0;
  while (1) {
    if (_M0L2__S1018 < _M0L7_2abindS1016) {
      moonbit_string_t _M0L3argS1019 =
        (moonbit_string_t)_M0L7_2abindS1017[_M0L2__S1018];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1020;
      moonbit_string_t _M0L4fileS1021;
      moonbit_string_t _M0L5rangeS1022;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1023;
      moonbit_string_t _M0L6_2atmpS2154;
      int32_t _M0L5startS1024;
      moonbit_string_t _M0L6_2atmpS2153;
      int32_t _M0L3endS1025;
      int32_t _M0L1iS1026;
      int32_t _M0L6_2atmpS2155;
      moonbit_incref_cycle_free(_M0L3argS1019);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1020
      = _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1006(_M0L51moonbit__test__driver__internal__split__mbt__stringS1006, _M0L3argS1019, 58);
      moonbit_decref_cycle_free(_M0L3argS1019);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1021
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1020, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1022
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1020, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1020);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1023
      = _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1006(_M0L51moonbit__test__driver__internal__split__mbt__stringS1006, _M0L5rangeS1022, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1022);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2154
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1023, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1024
      = _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S999(_M0L45moonbit__test__driver__internal__parse__int__S999, _M0L6_2atmpS2154);
      moonbit_decref_cycle_free(_M0L6_2atmpS2154);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS2153
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1023, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1023);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1025
      = _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S999(_M0L45moonbit__test__driver__internal__parse__int__S999, _M0L6_2atmpS2153);
      moonbit_decref_cycle_free(_M0L6_2atmpS2153);
      _M0L1iS1026 = _M0L5startS1024;
      while (1) {
        if (_M0L1iS1026 < _M0L3endS1025) {
          struct _M0TUsiE* _M0L8_2atupleS2151;
          int32_t _M0L6_2atmpS2152;
          moonbit_incref_cycle_free(_M0L4fileS1021);
          _M0L8_2atupleS2151
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS2151)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS2151->$0 = _M0L4fileS1021;
          _M0L8_2atupleS2151->$1 = _M0L1iS1026;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1013, _M0L8_2atupleS2151);
          _M0L6_2atmpS2152 = _M0L1iS1026 + 1;
          _M0L1iS1026 = _M0L6_2atmpS2152;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1021);
        }
        break;
      }
      _M0L6_2atmpS2155 = _M0L2__S1018 + 1;
      _M0L2__S1018 = _M0L6_2atmpS2155;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1017);
    }
    break;
  }
  return _M0L16file__and__indexS1013;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1006(
  int32_t _M0L6_2aenvS2132,
  moonbit_string_t _M0L1sS1007,
  int32_t _M0L3sepS1008
) {
  moonbit_string_t* _M0L6_2atmpS2150;
  struct _M0TPB5ArrayGsE* _M0L3resS1009;
  struct _M0TPB8MutLocalGiE* _M0L1iS1010;
  struct _M0TPB8MutLocalGiE* _M0L5startS1011;
  int32_t _M0L3valS2145;
  int32_t _M0L6_2atmpS2146;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS2150 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1009
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1009)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1009->$0 = _M0L6_2atmpS2150;
  _M0L3resS1009->$1 = 0;
  _M0L1iS1010
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1010)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1010->$0 = 0;
  _M0L5startS1011
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1011)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1011->$0 = 0;
  while (1) {
    int32_t _M0L3valS2133 = _M0L1iS1010->$0;
    int32_t _M0L6_2atmpS2134 = Moonbit_array_length(_M0L1sS1007);
    if (_M0L3valS2133 < _M0L6_2atmpS2134) {
      int32_t _M0L3valS2137 = _M0L1iS1010->$0;
      int32_t _M0L6_2atmpS2136;
      int32_t _M0L6_2atmpS2135;
      int32_t _M0L3valS2144;
      int32_t _M0L6_2atmpS2143;
      if (
        _M0L3valS2137 < 0
        || _M0L3valS2137 >= Moonbit_array_length(_M0L1sS1007)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2136 = _M0L1sS1007[_M0L3valS2137];
      _M0L6_2atmpS2135 = _M0L6_2atmpS2136;
      if (_M0L6_2atmpS2135 == _M0L3sepS1008) {
        int32_t _M0L3valS2139 = _M0L5startS1011->$0;
        int32_t _M0L3valS2140 = _M0L1iS1010->$0;
        moonbit_string_t _M0L6_2atmpS2138;
        int32_t _M0L3valS2142;
        int32_t _M0L6_2atmpS2141;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS2138
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1007, _M0L3valS2139, _M0L3valS2140);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1009, _M0L6_2atmpS2138);
        _M0L3valS2142 = _M0L1iS1010->$0;
        _M0L6_2atmpS2141 = _M0L3valS2142 + 1;
        _M0L5startS1011->$0 = _M0L6_2atmpS2141;
      }
      _M0L3valS2144 = _M0L1iS1010->$0;
      _M0L6_2atmpS2143 = _M0L3valS2144 + 1;
      _M0L1iS1010->$0 = _M0L6_2atmpS2143;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1010);
    }
    break;
  }
  _M0L3valS2145 = _M0L5startS1011->$0;
  _M0L6_2atmpS2146 = Moonbit_array_length(_M0L1sS1007);
  if (_M0L3valS2145 < _M0L6_2atmpS2146) {
    int32_t _M0L3valS2148 = _M0L5startS1011->$0;
    int32_t _M0L6_2atmpS2149;
    moonbit_string_t _M0L6_2atmpS2147;
    moonbit_decref_cycle_free(_M0L5startS1011);
    _M0L6_2atmpS2149 = Moonbit_array_length(_M0L1sS1007);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS2147
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1007, _M0L3valS2148, _M0L6_2atmpS2149);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1009, _M0L6_2atmpS2147);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1011);
  }
  return _M0L3resS1009;
}

int32_t _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S999(
  int32_t _M0L6_2aenvS2125,
  moonbit_string_t _M0L1sS1000
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1001;
  int32_t _M0L3lenS1002;
  int32_t _M0L7_2abindS1003;
  int32_t _M0L1iS1004;
  int32_t _result_2223;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1001
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1001)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1001->$0 = 0;
  _M0L3lenS1002 = Moonbit_array_length(_M0L1sS1000);
  _M0L7_2abindS1003 = 0;
  _M0L1iS1004 = _M0L7_2abindS1003;
  while (1) {
    if (_M0L1iS1004 < _M0L3lenS1002) {
      int32_t _M0L3valS2130 = _M0L3resS1001->$0;
      int32_t _M0L6_2atmpS2127 = _M0L3valS2130 * 10;
      int32_t _M0L6_2atmpS2129;
      int32_t _M0L6_2atmpS2128;
      int32_t _M0L6_2atmpS2126;
      int32_t _M0L6_2atmpS2131;
      if (
        _M0L1iS1004 < 0 || _M0L1iS1004 >= Moonbit_array_length(_M0L1sS1000)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2129 = _M0L1sS1000[_M0L1iS1004];
      _M0L6_2atmpS2128 = _M0L6_2atmpS2129 - 48;
      _M0L6_2atmpS2126 = _M0L6_2atmpS2127 + _M0L6_2atmpS2128;
      _M0L3resS1001->$0 = _M0L6_2atmpS2126;
      _M0L6_2atmpS2131 = _M0L1iS1004 + 1;
      _M0L1iS1004 = _M0L6_2atmpS2131;
      continue;
    }
    break;
  }
  _result_2223 = _M0L3resS1001->$0;
  moonbit_decref_cycle_free(_M0L3resS1001);
  return _result_2223;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS998
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS998);
  return _M0L4selfS998;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S968,
  moonbit_string_t _M0L12_2adiscard__S969,
  int32_t _M0L12_2adiscard__S970,
  struct _M0TWEu* _M0L12_2adiscard__S971,
  struct _M0TWssbEu* _M0L12_2adiscard__S972,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S973
) {
  struct moonbit_result_0 _result_2224;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _result_2224.tag = 1;
  _result_2224.data.ok = 0;
  return _result_2224;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S974,
  moonbit_string_t _M0L12_2adiscard__S975,
  int32_t _M0L12_2adiscard__S976,
  struct _M0TWEu* _M0L12_2adiscard__S977,
  struct _M0TWssbEu* _M0L12_2adiscard__S978,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S979
) {
  struct moonbit_result_0 _result_2225;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _result_2225.tag = 1;
  _result_2225.data.ok = 0;
  return _result_2225;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S980,
  moonbit_string_t _M0L12_2adiscard__S981,
  int32_t _M0L12_2adiscard__S982,
  struct _M0TWEu* _M0L12_2adiscard__S983,
  struct _M0TWssbEu* _M0L12_2adiscard__S984,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S985
) {
  struct moonbit_result_0 _result_2226;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _result_2226.tag = 1;
  _result_2226.data.ok = 0;
  return _result_2226;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S986,
  moonbit_string_t _M0L12_2adiscard__S987,
  int32_t _M0L12_2adiscard__S988,
  struct _M0TWEu* _M0L12_2adiscard__S989,
  struct _M0TWssbEu* _M0L12_2adiscard__S990,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S991
) {
  struct moonbit_result_0 _result_2227;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _result_2227.tag = 1;
  _result_2227.data.ok = 0;
  return _result_2227;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S992,
  moonbit_string_t _M0L12_2adiscard__S993,
  int32_t _M0L12_2adiscard__S994,
  struct _M0TWEu* _M0L12_2adiscard__S995,
  struct _M0TWssbEu* _M0L12_2adiscard__S996,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S997
) {
  struct moonbit_result_0 _result_2228;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _result_2228.tag = 1;
  _result_2228.data.ok = 0;
  return _result_2228;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S967
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt14SpikingSynapse9init__rho(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS930
) {
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2124;
  struct _M0TPB5ArrayGfE* _M0L4valsS2123;
  int32_t _M0L1nS929;
  struct _M0TPB5ArrayGfE* _M0L3rhoS2118;
  struct _M0TPB8MutLocalGiE* _M0L1kS931;
  #line 224 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6matrixS2124 = _M0L1cS930->$4;
  _M0L4valsS2123 = _M0L6matrixS2124->$4;
  #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS929 = _M0MPC15array5Array6lengthGfE(_M0L4valsS2123);
  _M0L3rhoS2118 = _M0L1cS930->$6;
  #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0MPC15array5Array5clearGfE(_M0L3rhoS2118);
  _M0L1kS931
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS931)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS931->$0 = 0;
  while (1) {
    int32_t _M0L3valS2119 = _M0L1kS931->$0;
    if (_M0L3valS2119 < _M0L1nS929) {
      struct _M0TPB5ArrayGfE* _M0L3rhoS2120 = _M0L1cS930->$6;
      int32_t _M0L3valS2122;
      int32_t _M0L6_2atmpS2121;
      #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MPC15array5Array4pushGfE(_M0L3rhoS2120, 0x1p+0f);
      _M0L3valS2122 = _M0L1kS931->$0;
      _M0L6_2atmpS2121 = _M0L3valS2122 + 1;
      _M0L1kS931->$0 = _M0L6_2atmpS2121;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS931);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16spiking__connect(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS927,
  int32_t _M0L3preS924,
  int32_t _M0L4postS926,
  float _M0L1wS928
) {
  int32_t _M0L8pre__idxS923;
  int32_t _M0L9post__idxS925;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2117;
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L8pre__idxS923 = _M0L3preS924 - 1;
  _M0L9post__idxS925 = _M0L4postS926 - 1;
  _M0L6matrixS2117 = _M0L1cS927->$4;
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0MP26RiantR8snn__mbt15SparseMatrixCSR3set(_M0L6matrixS2117, _M0L8pre__idxS923, _M0L9post__idxS925, _M0L1wS928);
  return 0;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MP26RiantR8snn__mbt14SpikingSynapse3new(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS920,
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS921,
  moonbit_string_t _M0L3symS922
) {
  int32_t _M0L1nS2115;
  int32_t _M0L1nS2116;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS919;
  float* _M0L6_2atmpS2114;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2105;
  float* _M0L6_2atmpS2113;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2106;
  float* _M0L6_2atmpS2112;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2107;
  int32_t* _M0L6_2atmpS2111;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2108;
  float* _M0L6_2atmpS2110;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2109;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _block_2230;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS2115 = _M0L3preS920->$2;
  _M0L1nS2116 = _M0L4postS921->$2;
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6matrixS919
  = _M0MP26RiantR8snn__mbt15SparseMatrixCSR5empty(_M0L1nS2115, _M0L1nS2116);
  _M0L6_2atmpS2114 = moonbit_empty_float_array;
  _M0L6_2atmpS2105
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2105)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2105->$0 = _M0L6_2atmpS2114;
  _M0L6_2atmpS2105->$1 = 0;
  _M0L6_2atmpS2113 = moonbit_empty_float_array;
  _M0L6_2atmpS2106
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2106)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2106->$0 = _M0L6_2atmpS2113;
  _M0L6_2atmpS2106->$1 = 0;
  _M0L6_2atmpS2112 = moonbit_empty_float_array;
  _M0L6_2atmpS2107
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2107)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2107->$0 = _M0L6_2atmpS2112;
  _M0L6_2atmpS2107->$1 = 0;
  _M0L6_2atmpS2111 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS2108
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2108)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS2108->$0 = _M0L6_2atmpS2111;
  _M0L6_2atmpS2108->$1 = 0;
  _M0L6_2atmpS2110 = moonbit_empty_float_array;
  _M0L6_2atmpS2109
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2109)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2109->$0 = _M0L6_2atmpS2110;
  _M0L6_2atmpS2109->$1 = 0;
  moonbit_incref_cycle_free(_M0L3preS920);
  moonbit_incref_cycle_free(_M0L4postS921);
  moonbit_incref_cycle_free(_M0L3symS922);
  _block_2230
  = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14SpikingSynapse));
  Moonbit_object_header(_block_2230)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
  _block_2230->$0 = _M0L3preS920;
  _block_2230->$1 = _M0L4postS921;
  _block_2230->$2 = _M0L3symS922;
  _block_2230->$3 = (moonbit_string_t)moonbit_string_literal_0.data;
  _block_2230->$4 = _M0L6matrixS919;
  _block_2230->$5 = _M0L6_2atmpS2105;
  _block_2230->$6 = _M0L6_2atmpS2106;
  _block_2230->$7 = _M0L6_2atmpS2107;
  _block_2230->$8 = _M0L6_2atmpS2108;
  _block_2230->$9 = _M0L6_2atmpS2109;
  return _block_2230;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter3new(
  
) {
  float _M0L1cS917;
  float _M0L2glS918;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_2231;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS917 = -0x1p+0f;
  _M0L2glS918 = -0x1p+0f;
  _block_2231
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_2231)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2231->$0 = _M0L1cS917;
  _block_2231->$1 = _M0L2glS918;
  _block_2231->$2 = 0x1.ep+3f;
  _block_2231->$3 = -0x1.9p+5f;
  _block_2231->$4 = -0x1.ep+5f;
  _block_2231->$5 = -0x1.18p+6f;
  _block_2231->$6 = 0x1.eb851eb851eb8p-5f;
  _block_2231->$7 = 0x1p+1f;
  _block_2231->$8 = 0x0p+0f;
  _block_2231->$9 = 0x0p+0f;
  _block_2231->$10 = 0x0p+0f;
  return _block_2231;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS891,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS893,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS896
) {
  struct _M0TPB5ArrayGfE* _M0L1vS890;
  float _M0L2vtS2103;
  float _M0L2vrS2104;
  float _M0L6spreadS892;
  int32_t _M0L7_2abindS894;
  int32_t _M0L1kS895;
  struct _M0TPB5ArrayGfE* _M0L1wS898;
  struct _M0TPB5ArrayGbE* _M0L4fireS899;
  struct _M0TPB5ArrayGiE* _M0L4tabsS900;
  struct _M0TPB5ArrayGfE* _M0L1iS901;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS902;
  struct _M0TPB5ArrayGfE* _M0L2geS903;
  struct _M0TPB5ArrayGfE* _M0L2giS904;
  struct _M0TPB5ArrayGfE* _M0L2heS905;
  struct _M0TPB5ArrayGfE* _M0L2hiS906;
  struct _M0TPB5ArrayGfE* _M0L3gluS907;
  struct _M0TPB5ArrayGfE* _M0L4gabaS908;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS909;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS910;
  float _M0L4e__eS911;
  float _M0L4e__iS912;
  float _M0L3treS913;
  float _M0L3tdeS914;
  float _M0L3triS915;
  float _M0L3tdiS916;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS2102;
  struct _M0TP26RiantR8snn__mbt2IF* _block_2233;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS890 = _M0MPC15array5Array4makeGfE(_M0L1nS891, 0x0p+0f);
  _M0L2vtS2103 = _M0L5paramS893->$3;
  _M0L2vrS2104 = _M0L5paramS893->$4;
  _M0L6spreadS892 = _M0L2vtS2103 - _M0L2vrS2104;
  _M0L7_2abindS894 = 0;
  _M0L1kS895 = _M0L7_2abindS894;
  while (1) {
    if (_M0L1kS895 < _M0L1nS891) {
      float _M0L2vrS2098 = _M0L5paramS893->$4;
      float _M0L6_2atmpS2100;
      float _M0L6_2atmpS2099;
      float _M0L6_2atmpS2097;
      int32_t _M0L6_2atmpS2101;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS2100 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS896);
      _M0L6_2atmpS2099 = _M0L6_2atmpS2100 * _M0L6spreadS892;
      _M0L6_2atmpS2097 = _M0L2vrS2098 + _M0L6_2atmpS2099;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS890, _M0L1kS895, _M0L6_2atmpS2097);
      _M0L6_2atmpS2101 = _M0L1kS895 + 1;
      _M0L1kS895 = _M0L6_2atmpS2101;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS898 = _M0MPC15array5Array4makeGfE(_M0L1nS891, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS899 = _M0MPC15array5Array4makeGbE(_M0L1nS891, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS900 = _M0MPC15array5Array4makeGiE(_M0L1nS891, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS901 = _M0MPC15array5Array4makeGfE(_M0L1nS891, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS902 = _M0MPC15array5Array4makeGfE(_M0L1nS891, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS903 = _M0MPC15array5Array4makeGfE(_M0L1nS891, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS904 = _M0MPC15array5Array4makeGfE(_M0L1nS891, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS905 = _M0MPC15array5Array4makeGfE(_M0L1nS891, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS906 = _M0MPC15array5Array4makeGfE(_M0L1nS891, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS907 = _M0MPC15array5Array4makeGfE(_M0L1nS891, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS908 = _M0MPC15array5Array4makeGfE(_M0L1nS891, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS909 = _M0MPC15array5Array4makeGfE(_M0L1nS891, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS910 = _M0MPC15array5Array4makeGfE(_M0L1nS891, 0x1p+0f);
  _M0L4e__eS911 = 0x0p+0f;
  _M0L4e__iS912 = -0x1.2cp+6f;
  _M0L3treS913 = 0x1p+0f;
  _M0L3tdeS914 = 0x1.8p+2f;
  _M0L3triS915 = 0x1p-1f;
  _M0L3tdiS916 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS2102 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS893);
  _block_2233
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_2233)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _block_2233->$0 = _M0L5paramS893;
  _block_2233->$1 = _M0L6_2atmpS2102;
  _block_2233->$2 = _M0L1nS891;
  _block_2233->$3 = _M0L1vS890;
  _block_2233->$4 = _M0L1wS898;
  _block_2233->$5 = _M0L4fireS899;
  _block_2233->$6 = _M0L4tabsS900;
  _block_2233->$7 = _M0L1iS901;
  _block_2233->$8 = _M0L9syn__currS902;
  _block_2233->$9 = _M0L2geS903;
  _block_2233->$10 = _M0L2giS904;
  _block_2233->$11 = _M0L2heS905;
  _block_2233->$12 = _M0L2hiS906;
  _block_2233->$13 = _M0L3gluS907;
  _block_2233->$14 = _M0L4gabaS908;
  _block_2233->$15 = _M0L7gsyn__eS909;
  _block_2233->$16 = _M0L7gsyn__iS910;
  _block_2233->$17 = _M0L4e__eS911;
  _block_2233->$18 = _M0L4e__iS912;
  _block_2233->$19 = _M0L3treS913;
  _block_2233->$20 = _M0L3tdeS914;
  _block_2233->$21 = _M0L3triS915;
  _block_2233->$22 = _M0L3tdiS916;
  return _block_2233;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_2234;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_2234
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_2234)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2234->$0 = 0x1p+1f;
  return _block_2234;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro7default(
  
) {
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 57 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  return _M0MP26RiantR8snn__mbt7Xoshiro3new(0ull);
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR3set(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS877,
  int32_t _M0L1iS876,
  int32_t _M0L1jS882,
  float _M0L1vS883
) {
  int32_t _if__result_2235;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS2096;
  int32_t _M0L5startS878;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS2094;
  int32_t _M0L6_2atmpS2095;
  int32_t _M0L3endS879;
  struct _M0TPB8MutLocalGbE* _M0L5foundS880;
  int32_t _M0L1kS881;
  int32_t _M0L3valS2086;
  #line 258 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  if (_M0L1iS876 < 0) {
    _if__result_2235 = 1;
  } else {
    int32_t _M0L4rowsS2081 = _M0L1mS877->$0;
    _if__result_2235 = _M0L1iS876 >= _M0L4rowsS2081;
  }
  if (_if__result_2235) {
    return 0;
  }
  _M0L6rowptrS2096 = _M0L1mS877->$2;
  #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L5startS878 = _M0MPC15array5Array2atGiE(_M0L6rowptrS2096, _M0L1iS876);
  _M0L6rowptrS2094 = _M0L1mS877->$2;
  _M0L6_2atmpS2095 = _M0L1iS876 + 1;
  #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L3endS879
  = _M0MPC15array5Array2atGiE(_M0L6rowptrS2094, _M0L6_2atmpS2095);
  _M0L5foundS880
  = (struct _M0TPB8MutLocalGbE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGbE));
  Moonbit_object_header(_M0L5foundS880)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5foundS880->$0 = 0;
  _M0L1kS881 = _M0L5startS878;
  while (1) {
    if (_M0L1kS881 < _M0L3endS879) {
      struct _M0TPB5ArrayGiE* _M0L6colptrS2083 = _M0L1mS877->$3;
      int32_t _M0L6_2atmpS2082;
      int32_t _M0L6_2atmpS2085;
      #line 267 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      _M0L6_2atmpS2082
      = _M0MPC15array5Array2atGiE(_M0L6colptrS2083, _M0L1kS881);
      if (_M0L6_2atmpS2082 == _M0L1jS882) {
        struct _M0TPB5ArrayGfE* _M0L4valsS2084 = _M0L1mS877->$4;
        #line 268 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0MPC15array5Array3setGfE(_M0L4valsS2084, _M0L1kS881, _M0L1vS883);
        _M0L5foundS880->$0 = 1;
        break;
      }
      _M0L6_2atmpS2085 = _M0L1kS881 + 1;
      _M0L1kS881 = _M0L6_2atmpS2085;
      continue;
    }
    break;
  }
  _M0L3valS2086 = _M0L5foundS880->$0;
  moonbit_decref_cycle_free(_M0L5foundS880);
  if (!_M0L3valS2086) {
    struct _M0TPB5ArrayGiE* _M0L6colptrS2087 = _M0L1mS877->$3;
    struct _M0TPB5ArrayGfE* _M0L4valsS2088;
    int32_t _M0L7n__rowsS885;
    int32_t _M0L7_2abindS886;
    int32_t _M0L7_2abindS887;
    int32_t _M0L1rS888;
    #line 275 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
    _M0MPC15array5Array6insertGiE(_M0L6colptrS2087, _M0L3endS879, _M0L1jS882);
    _M0L4valsS2088 = _M0L1mS877->$4;
    #line 276 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
    _M0MPC15array5Array6insertGfE(_M0L4valsS2088, _M0L3endS879, _M0L1vS883);
    _M0L7n__rowsS885 = _M0L1mS877->$0;
    _M0L7_2abindS886 = _M0L1iS876 + 1;
    _M0L7_2abindS887 = _M0L7n__rowsS885 + 1;
    _M0L1rS888 = _M0L7_2abindS886;
    while (1) {
      if (_M0L1rS888 < _M0L7_2abindS887) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2089 = _M0L1mS877->$2;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS2092 = _M0L1mS877->$2;
        int32_t _M0L6_2atmpS2091;
        int32_t _M0L6_2atmpS2090;
        int32_t _M0L6_2atmpS2093;
        #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L6_2atmpS2091
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS2092, _M0L1rS888);
        _M0L6_2atmpS2090 = _M0L6_2atmpS2091 + 1;
        #line 279 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0MPC15array5Array3setGiE(_M0L6rowptrS2089, _M0L1rS888, _M0L6_2atmpS2090);
        _M0L6_2atmpS2093 = _M0L1rS888 + 1;
        _M0L1rS888 = _M0L6_2atmpS2093;
        continue;
      }
      break;
    }
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0MP26RiantR8snn__mbt15SparseMatrixCSR5empty(
  int32_t _M0L4rowsS874,
  int32_t _M0L4colsS875
) {
  int32_t _M0L6_2atmpS2080;
  struct _M0TPB5ArrayGiE* _M0L6rowptrS873;
  int32_t* _M0L6_2atmpS2079;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2076;
  float* _M0L6_2atmpS2078;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2077;
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _block_2238;
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6_2atmpS2080 = _M0L4rowsS874 + 1;
  #line 41 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L6rowptrS873 = _M0MPC15array5Array4makeGiE(_M0L6_2atmpS2080, 0);
  _M0L6_2atmpS2079 = (int32_t*)moonbit_empty_int32_array;
  _M0L6_2atmpS2076
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2076)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _M0L6_2atmpS2076->$0 = _M0L6_2atmpS2079;
  _M0L6_2atmpS2076->$1 = 0;
  _M0L6_2atmpS2078 = moonbit_empty_float_array;
  _M0L6_2atmpS2077
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2077)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _M0L6_2atmpS2077->$0 = _M0L6_2atmpS2078;
  _M0L6_2atmpS2077->$1 = 0;
  _block_2238
  = (struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR));
  Moonbit_object_header(_block_2238)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 54, 0);
  _block_2238->$0 = _M0L4rowsS874;
  _block_2238->$1 = _M0L4colsS875;
  _block_2238->$2 = _M0L6rowptrS873;
  _block_2238->$3 = _M0L6_2atmpS2076;
  _block_2238->$4 = _M0L6_2atmpS2077;
  return _block_2238;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS871
) {
  struct _M0TUmmmmE* _M0L1sS870;
  uint64_t _M0L6_2atmpS2075;
  struct _M0TUmmmmE* _M0L1tS872;
  uint64_t _M0L6_2atmpS2071;
  uint64_t _M0L6_2atmpS2072;
  uint64_t _M0L6_2atmpS2073;
  uint64_t _M0L6_2atmpS2074;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_2239;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS870 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS871);
  _M0L6_2atmpS2075 = _M0L1sS870->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS872 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS2075);
  _M0L6_2atmpS2071 = _M0L1sS870->$0;
  _M0L6_2atmpS2072 = _M0L1sS870->$1;
  _M0L6_2atmpS2073 = _M0L1sS870->$2;
  moonbit_decref_cycle_free(_M0L1sS870);
  _M0L6_2atmpS2074 = _M0L1tS872->$0;
  moonbit_decref_cycle_free(_M0L1tS872);
  _block_2239
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_2239)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2239->$0 = _M0L6_2atmpS2071;
  _block_2239->$1 = _M0L6_2atmpS2072;
  _block_2239->$2 = _M0L6_2atmpS2073;
  _block_2239->$3 = _M0L6_2atmpS2074;
  return _block_2239;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(uint64_t _M0L4seedS862) {
  uint64_t _M0L2s1S861;
  uint64_t _M0L2z1S863;
  uint64_t _M0L2s2S864;
  uint64_t _M0L2z2S865;
  uint64_t _M0L2s3S866;
  uint64_t _M0L2z3S867;
  uint64_t _M0L2s4S868;
  uint64_t _M0L2z4S869;
  struct _M0TUmmmmE* _block_2240;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S861 = _M0L4seedS862 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S863 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S861);
  _M0L2s2S864 = _M0L2s1S861 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S865 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S864);
  _M0L2s3S866 = _M0L2s2S864 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S867 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S866);
  _M0L2s4S868 = _M0L2s3S866 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S869 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S868);
  _block_2240 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_2240)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2240->$0 = _M0L2z1S863;
  _block_2240->$1 = _M0L2z2S865;
  _block_2240->$2 = _M0L2z3S867;
  _block_2240->$3 = _M0L2z4S869;
  return _block_2240;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS859) {
  uint64_t _M0L6_2atmpS2070;
  uint64_t _M0L6_2atmpS2069;
  uint64_t _M0L1zS858;
  uint64_t _M0L6_2atmpS2068;
  uint64_t _M0L6_2atmpS2067;
  uint64_t _M0L1zS860;
  uint64_t _M0L6_2atmpS2066;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2070 = _M0L1zS859 >> 30;
  _M0L6_2atmpS2069 = _M0L1zS859 ^ _M0L6_2atmpS2070;
  _M0L1zS858 = _M0L6_2atmpS2069 * 13787848793156543929ull;
  _M0L6_2atmpS2068 = _M0L1zS858 >> 27;
  _M0L6_2atmpS2067 = _M0L1zS858 ^ _M0L6_2atmpS2068;
  _M0L1zS860 = _M0L6_2atmpS2067 * 10723151780598845931ull;
  _M0L6_2atmpS2066 = _M0L1zS860 >> 31;
  return _M0L1zS860 ^ _M0L6_2atmpS2066;
}

int32_t _M0FP26RiantR8snn__mbt28markram__stp__step__timestep(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS845,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS843,
  struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0L5paramS847,
  float _M0L6t__nowS842,
  float _M0L2dtS852
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS1979;
  int32_t _M0L6_2atmpS1978;
  int32_t _if__result_2241;
  int32_t _M0L6n__preS844;
  struct _M0TPB5ArrayGfE* _M0L3rhoS1981;
  int32_t _M0L6_2atmpS1980;
  float _M0L11u__baselineS846;
  float _M0L6tau__fS2065;
  float _M0L11inv__tau__fS848;
  float _M0L6tau__dS2064;
  float _M0L11inv__tau__dS849;
  struct _M0TPB8MutLocalGiE* _M0L1jS850;
  #line 473 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS1979 = _M0L4varsS843->$6;
  #line 482 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS1978 = _M0MPC15array5Array6lengthGbE(_M0L6activeS1979);
  if (_M0L6_2atmpS1978 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS1977 = _M0L4varsS843->$6;
    int32_t _M0L6_2atmpS1976;
    #line 482 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS1976 = _M0MPC15array5Array2atGbE(_M0L6activeS1977, 0);
    _if__result_2241 = !_M0L6_2atmpS1976;
  } else {
    _if__result_2241 = 0;
  }
  if (_if__result_2241) {
    return 0;
  }
  _M0L6n__preS844 = _M0L4varsS843->$0;
  _M0L3rhoS1981 = _M0L3synS845->$6;
  #line 487 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS1980 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS1981);
  if (_M0L6_2atmpS1980 == 0) {
    return 0;
  }
  _M0L11u__baselineS846 = _M0L5paramS847->$0;
  _M0L6tau__fS2065 = _M0L5paramS847->$1;
  _M0L11inv__tau__fS848 = 0x1p+0f / _M0L6tau__fS2065;
  _M0L6tau__dS2064 = _M0L5paramS847->$2;
  _M0L11inv__tau__dS849 = 0x1p+0f / _M0L6tau__dS2064;
  _M0L1jS850
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS850)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS850->$0 = 0;
  while (1) {
    int32_t _M0L3valS1982 = _M0L1jS850->$0;
    if (_M0L3valS1982 < _M0L6n__preS844) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS1985 = _M0L3synS845->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS1983 = _M0L3preS1985->$5;
      int32_t _M0L3valS1984 = _M0L1jS850->$0;
      int32_t _M0L3valS2012;
      int32_t _M0L6_2atmpS2011;
      #line 496 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS1983, _M0L3valS1984)) {
        struct _M0TPB5ArrayGfE* _M0L1uS1986 = _M0L4varsS843->$2;
        int32_t _M0L3valS1987 = _M0L1jS850->$0;
        struct _M0TPB5ArrayGfE* _M0L1uS1995 = _M0L4varsS843->$2;
        int32_t _M0L3valS1996 = _M0L1jS850->$0;
        float _M0L6_2atmpS1989;
        struct _M0TPB5ArrayGfE* _M0L1uS1993;
        int32_t _M0L3valS1994;
        float _M0L6_2atmpS1992;
        float _M0L6_2atmpS1991;
        float _M0L6_2atmpS1990;
        float _M0L6_2atmpS1988;
        struct _M0TPB5ArrayGfE* _M0L1xS1997;
        int32_t _M0L3valS1998;
        struct _M0TPB5ArrayGfE* _M0L1xS2009;
        int32_t _M0L3valS2010;
        float _M0L6_2atmpS2000;
        struct _M0TPB5ArrayGfE* _M0L1uS2007;
        int32_t _M0L3valS2008;
        float _M0L6_2atmpS2006;
        float _M0L6_2atmpS2002;
        struct _M0TPB5ArrayGfE* _M0L1xS2004;
        int32_t _M0L3valS2005;
        float _M0L6_2atmpS2003;
        float _M0L6_2atmpS2001;
        float _M0L6_2atmpS1999;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS1989
        = _M0MPC15array5Array2atGfE(_M0L1uS1995, _M0L3valS1996);
        _M0L1uS1993 = _M0L4varsS843->$2;
        _M0L3valS1994 = _M0L1jS850->$0;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS1992
        = _M0MPC15array5Array2atGfE(_M0L1uS1993, _M0L3valS1994);
        _M0L6_2atmpS1991 = 0x1p+0f - _M0L6_2atmpS1992;
        _M0L6_2atmpS1990 = _M0L11u__baselineS846 * _M0L6_2atmpS1991;
        _M0L6_2atmpS1988 = _M0L6_2atmpS1989 + _M0L6_2atmpS1990;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS1986, _M0L3valS1987, _M0L6_2atmpS1988);
        _M0L1xS1997 = _M0L4varsS843->$3;
        _M0L3valS1998 = _M0L1jS850->$0;
        _M0L1xS2009 = _M0L4varsS843->$3;
        _M0L3valS2010 = _M0L1jS850->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2000
        = _M0MPC15array5Array2atGfE(_M0L1xS2009, _M0L3valS2010);
        _M0L1uS2007 = _M0L4varsS843->$2;
        _M0L3valS2008 = _M0L1jS850->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2006
        = _M0MPC15array5Array2atGfE(_M0L1uS2007, _M0L3valS2008);
        _M0L6_2atmpS2002 = -_M0L6_2atmpS2006;
        _M0L1xS2004 = _M0L4varsS843->$3;
        _M0L3valS2005 = _M0L1jS850->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2003
        = _M0MPC15array5Array2atGfE(_M0L1xS2004, _M0L3valS2005);
        _M0L6_2atmpS2001 = _M0L6_2atmpS2002 * _M0L6_2atmpS2003;
        _M0L6_2atmpS1999 = _M0L6_2atmpS2000 + _M0L6_2atmpS2001;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS1997, _M0L3valS1998, _M0L6_2atmpS1999);
      }
      _M0L3valS2012 = _M0L1jS850->$0;
      _M0L6_2atmpS2011 = _M0L3valS2012 + 1;
      _M0L1jS850->$0 = _M0L6_2atmpS2011;
      continue;
    }
    break;
  }
  _M0L1jS850->$0 = 0;
  while (1) {
    int32_t _M0L3valS2013 = _M0L1jS850->$0;
    if (_M0L3valS2013 < _M0L6n__preS844) {
      struct _M0TPB5ArrayGfE* _M0L1uS2014 = _M0L4varsS843->$2;
      int32_t _M0L3valS2015 = _M0L1jS850->$0;
      struct _M0TPB5ArrayGfE* _M0L1uS2024 = _M0L4varsS843->$2;
      int32_t _M0L3valS2025 = _M0L1jS850->$0;
      float _M0L6_2atmpS2017;
      struct _M0TPB5ArrayGfE* _M0L1uS2022;
      int32_t _M0L3valS2023;
      float _M0L6_2atmpS2021;
      float _M0L6_2atmpS2020;
      float _M0L6_2atmpS2019;
      float _M0L6_2atmpS2018;
      float _M0L6_2atmpS2016;
      struct _M0TPB5ArrayGfE* _M0L1xS2026;
      int32_t _M0L3valS2027;
      struct _M0TPB5ArrayGfE* _M0L1xS2036;
      int32_t _M0L3valS2037;
      float _M0L6_2atmpS2029;
      struct _M0TPB5ArrayGfE* _M0L1xS2034;
      int32_t _M0L3valS2035;
      float _M0L6_2atmpS2033;
      float _M0L6_2atmpS2032;
      float _M0L6_2atmpS2031;
      float _M0L6_2atmpS2030;
      float _M0L6_2atmpS2028;
      struct _M0TPB5ArrayGfE* _M0L8rho__preS2038;
      int32_t _M0L3valS2039;
      struct _M0TPB5ArrayGfE* _M0L1uS2045;
      int32_t _M0L3valS2046;
      float _M0L6_2atmpS2041;
      struct _M0TPB5ArrayGfE* _M0L1xS2043;
      int32_t _M0L3valS2044;
      float _M0L6_2atmpS2042;
      float _M0L6_2atmpS2040;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2063;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS2061;
      int32_t _M0L3valS2062;
      int32_t _M0L5startS853;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS2060;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS2057;
      int32_t _M0L3valS2059;
      int32_t _M0L6_2atmpS2058;
      int32_t _M0L3endS854;
      struct _M0TPB8MutLocalGiE* _M0L1sS855;
      int32_t _M0L3valS2056;
      int32_t _M0L6_2atmpS2055;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS2017
      = _M0MPC15array5Array2atGfE(_M0L1uS2024, _M0L3valS2025);
      _M0L1uS2022 = _M0L4varsS843->$2;
      _M0L3valS2023 = _M0L1jS850->$0;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS2021
      = _M0MPC15array5Array2atGfE(_M0L1uS2022, _M0L3valS2023);
      _M0L6_2atmpS2020 = _M0L11u__baselineS846 - _M0L6_2atmpS2021;
      _M0L6_2atmpS2019 = _M0L2dtS852 * _M0L6_2atmpS2020;
      _M0L6_2atmpS2018 = _M0L6_2atmpS2019 * _M0L11inv__tau__fS848;
      _M0L6_2atmpS2016 = _M0L6_2atmpS2017 + _M0L6_2atmpS2018;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS2014, _M0L3valS2015, _M0L6_2atmpS2016);
      _M0L1xS2026 = _M0L4varsS843->$3;
      _M0L3valS2027 = _M0L1jS850->$0;
      _M0L1xS2036 = _M0L4varsS843->$3;
      _M0L3valS2037 = _M0L1jS850->$0;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS2029
      = _M0MPC15array5Array2atGfE(_M0L1xS2036, _M0L3valS2037);
      _M0L1xS2034 = _M0L4varsS843->$3;
      _M0L3valS2035 = _M0L1jS850->$0;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS2033
      = _M0MPC15array5Array2atGfE(_M0L1xS2034, _M0L3valS2035);
      _M0L6_2atmpS2032 = 0x1p+0f - _M0L6_2atmpS2033;
      _M0L6_2atmpS2031 = _M0L2dtS852 * _M0L6_2atmpS2032;
      _M0L6_2atmpS2030 = _M0L6_2atmpS2031 * _M0L11inv__tau__dS849;
      _M0L6_2atmpS2028 = _M0L6_2atmpS2029 + _M0L6_2atmpS2030;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS2026, _M0L3valS2027, _M0L6_2atmpS2028);
      _M0L8rho__preS2038 = _M0L4varsS843->$4;
      _M0L3valS2039 = _M0L1jS850->$0;
      _M0L1uS2045 = _M0L4varsS843->$2;
      _M0L3valS2046 = _M0L1jS850->$0;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS2041
      = _M0MPC15array5Array2atGfE(_M0L1uS2045, _M0L3valS2046);
      _M0L1xS2043 = _M0L4varsS843->$3;
      _M0L3valS2044 = _M0L1jS850->$0;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS2042
      = _M0MPC15array5Array2atGfE(_M0L1xS2043, _M0L3valS2044);
      _M0L6_2atmpS2040 = _M0L6_2atmpS2041 * _M0L6_2atmpS2042;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L8rho__preS2038, _M0L3valS2039, _M0L6_2atmpS2040);
      _M0L6matrixS2063 = _M0L3synS845->$4;
      _M0L6rowptrS2061 = _M0L6matrixS2063->$2;
      _M0L3valS2062 = _M0L1jS850->$0;
      #line 509 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L5startS853
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS2061, _M0L3valS2062);
      _M0L6matrixS2060 = _M0L3synS845->$4;
      _M0L6rowptrS2057 = _M0L6matrixS2060->$2;
      _M0L3valS2059 = _M0L1jS850->$0;
      _M0L6_2atmpS2058 = _M0L3valS2059 + 1;
      #line 510 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L3endS854
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS2057, _M0L6_2atmpS2058);
      _M0L1sS855
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS855)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS855->$0 = _M0L5startS853;
      while (1) {
        int32_t _M0L3valS2047 = _M0L1sS855->$0;
        if (_M0L3valS2047 < _M0L3endS854) {
          struct _M0TPB5ArrayGfE* _M0L3rhoS2048 = _M0L3synS845->$6;
          int32_t _M0L3valS2049 = _M0L1sS855->$0;
          struct _M0TPB5ArrayGfE* _M0L8rho__preS2051 = _M0L4varsS843->$4;
          int32_t _M0L3valS2052 = _M0L1jS850->$0;
          float _M0L6_2atmpS2050;
          int32_t _M0L3valS2054;
          int32_t _M0L6_2atmpS2053;
          #line 513 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
          _M0L6_2atmpS2050
          = _M0MPC15array5Array2atGfE(_M0L8rho__preS2051, _M0L3valS2052);
          #line 513 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rhoS2048, _M0L3valS2049, _M0L6_2atmpS2050);
          _M0L3valS2054 = _M0L1sS855->$0;
          _M0L6_2atmpS2053 = _M0L3valS2054 + 1;
          _M0L1sS855->$0 = _M0L6_2atmpS2053;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS855);
        }
        break;
      }
      _M0L3valS2056 = _M0L1jS850->$0;
      _M0L6_2atmpS2055 = _M0L3valS2056 + 1;
      _M0L1jS850->$0 = _M0L6_2atmpS2055;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS850);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _M0MP26RiantR8snn__mbt23MarkramSTPEntryTimestep3new(
  int32_t _M0L11conn__indexS839,
  int32_t _M0L6n__preS840,
  int32_t _M0L7n__postS841,
  struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0L11param_2eoptS837
) {
  struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0L5paramS836;
  struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _result_2245;
  if (_M0L11param_2eoptS837 == 0) {
    #line 445 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L5paramS836
    = _M0MP26RiantR8snn__mbt27MarkramSTPParameterTimestep3new();
  } else {
    struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0L7_2aSomeS838 =
      _M0L11param_2eoptS837;
    if (_M0L7_2aSomeS838) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS838);
    }
    _M0L5paramS836 = _M0L7_2aSomeS838;
  }
  _result_2245
  = _M0MP26RiantR8snn__mbt23MarkramSTPEntryTimestep11new_2einner(_M0L11conn__indexS839, _M0L6n__preS840, _M0L7n__postS841, _M0L5paramS836);
  moonbit_decref_cycle_free(_M0L5paramS836);
  return _result_2245;
}

struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _M0MP26RiantR8snn__mbt23MarkramSTPEntryTimestep11new_2einner(
  int32_t _M0L11conn__indexS835,
  int32_t _M0L6n__preS828,
  int32_t _M0L7n__postS834,
  struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0L5paramS829
) {
  float _M0L1uS1975;
  struct _M0TPB5ArrayGfE* _M0L6u__arrS827;
  struct _M0TPB5ArrayGfE* _M0L6x__arrS830;
  float _M0L1uS1974;
  struct _M0TPB5ArrayGfE* _M0L8rho__arrS831;
  float _M0L6_2atmpS1973;
  struct _M0TPB5ArrayGfE* _M0L7ls__arrS832;
  uint8_t* _M0L6_2atmpS1972;
  struct _M0TPB5ArrayGbE* _M0L6_2atmpS1971;
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS833;
  struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _block_2246;
  #line 441 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L1uS1975 = _M0L5paramS829->$0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6u__arrS827 = _M0MPC15array5Array4makeGfE(_M0L6n__preS828, _M0L1uS1975);
  #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6x__arrS830 = _M0MPC15array5Array4makeGfE(_M0L6n__preS828, 0x1p+0f);
  _M0L1uS1974 = _M0L5paramS829->$0;
  #line 449 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L8rho__arrS831
  = _M0MPC15array5Array4makeGfE(_M0L6n__preS828, _M0L1uS1974);
  _M0L6_2atmpS1973 = -0x1p+0f / (float)MOONBIT_ZERO;
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L7ls__arrS832
  = _M0MPC15array5Array4makeGfE(_M0L6n__preS828, _M0L6_2atmpS1973);
  _M0L6_2atmpS1972 = (uint8_t*)moonbit_make_bytes_raw(1);
  _M0L6_2atmpS1972[0] = 1;
  _M0L6_2atmpS1971
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_M0L6_2atmpS1971)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 59, 0);
  _M0L6_2atmpS1971->$0 = _M0L6_2atmpS1972;
  _M0L6_2atmpS1971->$1 = 1;
  _M0L4varsS833
  = (struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables));
  Moonbit_object_header(_M0L4varsS833)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 62, 0);
  _M0L4varsS833->$0 = _M0L6n__preS828;
  _M0L4varsS833->$1 = _M0L7n__postS834;
  _M0L4varsS833->$2 = _M0L6u__arrS827;
  _M0L4varsS833->$3 = _M0L6x__arrS830;
  _M0L4varsS833->$4 = _M0L8rho__arrS831;
  _M0L4varsS833->$5 = _M0L7ls__arrS832;
  _M0L4varsS833->$6 = _M0L6_2atmpS1971;
  moonbit_incref_cycle_free(_M0L5paramS829);
  _block_2246
  = (struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep));
  Moonbit_object_header(_block_2246)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 69, 0);
  _block_2246->$0 = _M0L11conn__indexS835;
  _block_2246->$1 = _M0L4varsS833;
  _block_2246->$2 = _M0L5paramS829;
  return _block_2246;
}

struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0MP26RiantR8snn__mbt27MarkramSTPParameterTimestep3new(
  
) {
  struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _block_2247;
  #line 422 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _block_2247
  = (struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep));
  Moonbit_object_header(_block_2247)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2247->$0 = 0x1p-1f;
  _block_2247->$1 = 0x1.77p+9f;
  _block_2247->$2 = 0x1.f4p+7f;
  _block_2247->$3 = 0x1.dcd65p+29f;
  _block_2247->$4 = -0x1.dcd65p+29f;
  return _block_2247;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS825
) {
  uint32_t _M0L1uS824;
  uint32_t _M0L4bitsS826;
  double _M0L6_2atmpS1970;
  double _M0L6_2atmpS1969;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS824 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS825);
  _M0L4bitsS826 = _M0L1uS824 >> 8;
  _M0L6_2atmpS1970 = (double)_M0L4bitsS826;
  _M0L6_2atmpS1969 = _M0L6_2atmpS1970 * 0x1p-24;
  return (float)_M0L6_2atmpS1969;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS823
) {
  uint64_t _M0L1uS822;
  uint64_t _M0L6_2atmpS1968;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS822 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS823);
  _M0L6_2atmpS1968 = _M0L1uS822 >> 32;
  return (uint32_t)_M0L6_2atmpS1968;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS815
) {
  uint64_t _M0L2s0S814;
  uint64_t _M0L2s1S816;
  uint64_t _M0L2s2S817;
  uint64_t _M0L2s3S818;
  uint64_t _M0L3tmpS819;
  uint64_t _M0L6_2atmpS1967;
  uint64_t _M0L3resS820;
  uint64_t _M0L1tS821;
  uint64_t _M0L6_2atmpS1957;
  uint64_t _M0L6_2atmpS1958;
  uint64_t _M0L2s2S1960;
  uint64_t _M0L6_2atmpS1959;
  uint64_t _M0L2s3S1962;
  uint64_t _M0L6_2atmpS1961;
  uint64_t _M0L2s2S1964;
  uint64_t _M0L6_2atmpS1963;
  uint64_t _M0L2s3S1966;
  uint64_t _M0L6_2atmpS1965;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S814 = _M0L1rS815->$0;
  _M0L2s1S816 = _M0L1rS815->$1;
  _M0L2s2S817 = _M0L1rS815->$2;
  _M0L2s3S818 = _M0L1rS815->$3;
  _M0L3tmpS819 = _M0L2s0S814 + _M0L2s3S818;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1967 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS819, 23);
  _M0L3resS820 = _M0L6_2atmpS1967 + _M0L2s0S814;
  _M0L1tS821 = _M0L2s1S816 << 17;
  _M0L6_2atmpS1957 = _M0L2s2S817 ^ _M0L2s0S814;
  _M0L1rS815->$2 = _M0L6_2atmpS1957;
  _M0L6_2atmpS1958 = _M0L2s3S818 ^ _M0L2s1S816;
  _M0L1rS815->$3 = _M0L6_2atmpS1958;
  _M0L2s2S1960 = _M0L1rS815->$2;
  _M0L6_2atmpS1959 = _M0L2s1S816 ^ _M0L2s2S1960;
  _M0L1rS815->$1 = _M0L6_2atmpS1959;
  _M0L2s3S1962 = _M0L1rS815->$3;
  _M0L6_2atmpS1961 = _M0L2s0S814 ^ _M0L2s3S1962;
  _M0L1rS815->$0 = _M0L6_2atmpS1961;
  _M0L2s2S1964 = _M0L1rS815->$2;
  _M0L6_2atmpS1963 = _M0L2s2S1964 ^ _M0L1tS821;
  _M0L1rS815->$2 = _M0L6_2atmpS1963;
  _M0L2s3S1966 = _M0L1rS815->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1965 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S1966, 45);
  _M0L1rS815->$3 = _M0L6_2atmpS1965;
  return _M0L3resS820;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS812, int32_t _M0L1kS813) {
  uint64_t _M0L6_2atmpS1954;
  int32_t _M0L6_2atmpS1956;
  uint64_t _M0L6_2atmpS1955;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS1954 = _M0L1xS812 << (_M0L1kS813 & 63);
  _M0L6_2atmpS1956 = 64 - _M0L1kS813;
  _M0L6_2atmpS1955 = _M0L1xS812 >> (_M0L6_2atmpS1956 & 63);
  return _M0L6_2atmpS1954 | _M0L6_2atmpS1955;
}

int32_t _M0MPC15float5Float7is__nan(float _M0L4selfS811) {
  #line 208 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0L4selfS811 != _M0L4selfS811;
}

int32_t _M0MPC15float5Float7is__inf(float _M0L4selfS810) {
  #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC15float5Float12is__pos__inf(_M0L4selfS810)
         || _M0MPC15float5Float12is__neg__inf(_M0L4selfS810);
}

int32_t _M0MPC15float5Float12is__pos__inf(float _M0L4selfS809) {
  #line 164 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0L4selfS809 > _M0FPC15float10max__value;
}

int32_t _M0MPC15float5Float12is__neg__inf(float _M0L4selfS808) {
  #line 186 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0L4selfS808 < _M0FPC15float10min__value;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS807) {
  double _M0L6_2atmpS1953;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1953 = (double)_M0L4selfS807;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1953);
}

int32_t _M0MPC15array5Array5clearGfE(struct _M0TPB5ArrayGfE* _M0L4selfS806) {
  #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 579 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0MPC15array5Array28unsafe__truncate__to__lengthGfE(_M0L4selfS806, 0);
  return 0;
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS792,
  float _M0L4elemS794
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS791;
  int32_t _M0L1iS793;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS791 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS792);
  _M0L1iS793 = 0;
  while (1) {
    if (_M0L1iS793 < _M0L3lenS792) {
      float* _M0L3bufS1947 = _M0L3arrS791->$0;
      int32_t _M0L6_2atmpS1948;
      _M0L3bufS1947[_M0L1iS793] = _M0L4elemS794;
      _M0L6_2atmpS1948 = _M0L1iS793 + 1;
      _M0L1iS793 = _M0L6_2atmpS1948;
      continue;
    }
    break;
  }
  return _M0L3arrS791;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS797,
  int32_t _M0L4elemS799
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS796;
  int32_t _M0L1iS798;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS796 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS797);
  _M0L1iS798 = 0;
  while (1) {
    if (_M0L1iS798 < _M0L3lenS797) {
      uint8_t* _M0L3bufS1949 = _M0L3arrS796->$0;
      int32_t _M0L6_2atmpS1950;
      _M0L3bufS1949[_M0L1iS798] = _M0L4elemS799;
      _M0L6_2atmpS1950 = _M0L1iS798 + 1;
      _M0L1iS798 = _M0L6_2atmpS1950;
      continue;
    }
    break;
  }
  return _M0L3arrS796;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS802,
  int32_t _M0L4elemS804
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS801;
  int32_t _M0L1iS803;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS801 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS802);
  _M0L1iS803 = 0;
  while (1) {
    if (_M0L1iS803 < _M0L3lenS802) {
      int32_t* _M0L3bufS1951 = _M0L3arrS801->$0;
      int32_t _M0L6_2atmpS1952;
      _M0L3bufS1951[_M0L1iS803] = _M0L4elemS804;
      _M0L6_2atmpS1952 = _M0L1iS803 + 1;
      _M0L1iS803 = _M0L6_2atmpS1952;
      continue;
    }
    break;
  }
  return _M0L3arrS801;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS780,
  int32_t _M0L5indexS781,
  int32_t _M0L5valueS782
) {
  int32_t _M0L3lenS779;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS779 = _M0L4selfS780->$1;
  if (_M0L5indexS781 >= 0 && _M0L5indexS781 < _M0L3lenS779) {
    uint8_t* _M0L6_2atmpS1944;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1944 = _M0MPC15array5Array6bufferGbE(_M0L4selfS780);
    _M0L6_2atmpS1944[_M0L5indexS781] = _M0L5valueS782;
    moonbit_decref_cycle_free(_M0L6_2atmpS1944);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS784,
  int32_t _M0L5indexS785,
  float _M0L5valueS786
) {
  int32_t _M0L3lenS783;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS783 = _M0L4selfS784->$1;
  if (_M0L5indexS785 >= 0 && _M0L5indexS785 < _M0L3lenS783) {
    float* _M0L6_2atmpS1945;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1945 = _M0MPC15array5Array6bufferGfE(_M0L4selfS784);
    _M0L6_2atmpS1945[_M0L5indexS785] = _M0L5valueS786;
    moonbit_decref_cycle_free(_M0L6_2atmpS1945);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS788,
  int32_t _M0L5indexS789,
  int32_t _M0L5valueS790
) {
  int32_t _M0L3lenS787;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS787 = _M0L4selfS788->$1;
  if (_M0L5indexS789 >= 0 && _M0L5indexS789 < _M0L3lenS787) {
    int32_t* _M0L6_2atmpS1946;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1946 = _M0MPC15array5Array6bufferGiE(_M0L4selfS788);
    _M0L6_2atmpS1946[_M0L5indexS789] = _M0L5valueS790;
    moonbit_decref_cycle_free(_M0L6_2atmpS1946);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array6insertGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS770,
  int32_t _M0L5indexS769,
  int32_t _M0L5valueS772
) {
  int32_t _if__result_2251;
  #line 738 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L5indexS769 >= 0) {
    int32_t _M0L3lenS1914 = _M0L4selfS770->$1;
    _if__result_2251 = _M0L5indexS769 <= _M0L3lenS1914;
  } else {
    _if__result_2251 = 0;
  }
  if (_if__result_2251) {
    int32_t _M0L3lenS1915 = _M0L4selfS770->$1;
    int32_t* _M0L6_2atmpS1917;
    int32_t _M0L6_2atmpS1916;
    int32_t* _M0L6_2atmpS1920;
    int32_t _M0L6_2atmpS1921;
    int32_t* _M0L6_2atmpS1922;
    int32_t _M0L3lenS1924;
    int32_t _M0L6_2atmpS1923;
    int32_t _M0L6lengthS771;
    int32_t* _M0L3bufS1925;
    int32_t _M0L6_2atmpS1926;
    #line 745 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS1917 = _M0MPC15array5Array6bufferGiE(_M0L4selfS770);
    _M0L6_2atmpS1916 = Moonbit_array_length(_M0L6_2atmpS1917);
    moonbit_decref_cycle_free(_M0L6_2atmpS1917);
    if (_M0L3lenS1915 == _M0L6_2atmpS1916) {
      int32_t _M0L3lenS1919 = _M0L4selfS770->$1;
      int32_t _M0L6_2atmpS1918 = _M0L3lenS1919 + 1;
      #line 746 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
      _M0MPC15array5Array7reallocGiE(_M0L4selfS770, _M0L6_2atmpS1918);
    }
    #line 749 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS1920 = _M0MPC15array5Array6bufferGiE(_M0L4selfS770);
    _M0L6_2atmpS1921 = _M0L5indexS769 + 1;
    #line 751 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS1922 = _M0MPC15array5Array6bufferGiE(_M0L4selfS770);
    _M0L3lenS1924 = _M0L4selfS770->$1;
    _M0L6_2atmpS1923 = _M0L3lenS1924 - _M0L5indexS769;
    #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L6_2atmpS1920, _M0L6_2atmpS1921, _M0L6_2atmpS1922, _M0L5indexS769, _M0L6_2atmpS1923);
    moonbit_decref_cycle_free(_M0L6_2atmpS1920);
    moonbit_decref_cycle_free(_M0L6_2atmpS1922);
    _M0L6lengthS771 = _M0L4selfS770->$1;
    _M0L3bufS1925 = _M0L4selfS770->$0;
    _M0L3bufS1925[_M0L5indexS769] = _M0L5valueS772;
    _M0L6_2atmpS1926 = _M0L6lengthS771 + 1;
    _M0L4selfS770->$1 = _M0L6_2atmpS1926;
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS773;
    int32_t _M0L3lenS1928;
    moonbit_string_t _M0L6_2atmpS1927;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L18_2astring__builderS773
    = _M0MPB13StringBuilder21StringBuilder_2einner(60);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS773, (moonbit_string_t)moonbit_string_literal_9.data);
    _M0L3lenS1928 = _M0L4selfS770->$1;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS773, _M0L3lenS1928);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS773, (moonbit_string_t)moonbit_string_literal_10.data);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS773, _M0L5indexS769);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS1927
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS773);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS773);
    #line 741 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE(_M0L6_2atmpS1927);
    moonbit_decref_cycle_free(_M0L6_2atmpS1927);
  }
  return 0;
}

int32_t _M0MPC15array5Array6insertGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS775,
  int32_t _M0L5indexS774,
  float _M0L5valueS777
) {
  int32_t _if__result_2252;
  #line 738 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L5indexS774 >= 0) {
    int32_t _M0L3lenS1929 = _M0L4selfS775->$1;
    _if__result_2252 = _M0L5indexS774 <= _M0L3lenS1929;
  } else {
    _if__result_2252 = 0;
  }
  if (_if__result_2252) {
    int32_t _M0L3lenS1930 = _M0L4selfS775->$1;
    float* _M0L6_2atmpS1932;
    int32_t _M0L6_2atmpS1931;
    float* _M0L6_2atmpS1935;
    int32_t _M0L6_2atmpS1936;
    float* _M0L6_2atmpS1937;
    int32_t _M0L3lenS1939;
    int32_t _M0L6_2atmpS1938;
    int32_t _M0L6lengthS776;
    float* _M0L3bufS1940;
    int32_t _M0L6_2atmpS1941;
    #line 745 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS1932 = _M0MPC15array5Array6bufferGfE(_M0L4selfS775);
    _M0L6_2atmpS1931 = Moonbit_array_length(_M0L6_2atmpS1932);
    moonbit_decref_cycle_free(_M0L6_2atmpS1932);
    if (_M0L3lenS1930 == _M0L6_2atmpS1931) {
      int32_t _M0L3lenS1934 = _M0L4selfS775->$1;
      int32_t _M0L6_2atmpS1933 = _M0L3lenS1934 + 1;
      #line 746 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
      _M0MPC15array5Array7reallocGfE(_M0L4selfS775, _M0L6_2atmpS1933);
    }
    #line 749 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS1935 = _M0MPC15array5Array6bufferGfE(_M0L4selfS775);
    _M0L6_2atmpS1936 = _M0L5indexS774 + 1;
    #line 751 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS1937 = _M0MPC15array5Array6bufferGfE(_M0L4selfS775);
    _M0L3lenS1939 = _M0L4selfS775->$1;
    _M0L6_2atmpS1938 = _M0L3lenS1939 - _M0L5indexS774;
    #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L6_2atmpS1935, _M0L6_2atmpS1936, _M0L6_2atmpS1937, _M0L5indexS774, _M0L6_2atmpS1938);
    moonbit_decref_cycle_free(_M0L6_2atmpS1935);
    moonbit_decref_cycle_free(_M0L6_2atmpS1937);
    _M0L6lengthS776 = _M0L4selfS775->$1;
    _M0L3bufS1940 = _M0L4selfS775->$0;
    _M0L3bufS1940[_M0L5indexS774] = _M0L5valueS777;
    _M0L6_2atmpS1941 = _M0L6lengthS776 + 1;
    _M0L4selfS775->$1 = _M0L6_2atmpS1941;
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS778;
    int32_t _M0L3lenS1943;
    moonbit_string_t _M0L6_2atmpS1942;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L18_2astring__builderS778
    = _M0MPB13StringBuilder21StringBuilder_2einner(60);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS778, (moonbit_string_t)moonbit_string_literal_9.data);
    _M0L3lenS1943 = _M0L4selfS775->$1;
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS778, _M0L3lenS1943);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS778, (moonbit_string_t)moonbit_string_literal_10.data);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS778, _M0L5indexS774);
    #line 742 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0L6_2atmpS1942
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS778);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS778);
    #line 741 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE(_M0L6_2atmpS1942);
    moonbit_decref_cycle_free(_M0L6_2atmpS1942);
  }
  return 0;
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS758,
  int32_t _M0L5indexS759
) {
  int32_t _M0L3lenS757;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS757 = _M0L4selfS758->$1;
  if (_M0L5indexS759 >= 0 && _M0L5indexS759 < _M0L3lenS757) {
    float* _M0L6_2atmpS1910;
    float _result_2253;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1910 = _M0MPC15array5Array6bufferGfE(_M0L4selfS758);
    _result_2253 = (float)_M0L6_2atmpS1910[_M0L5indexS759];
    moonbit_decref_cycle_free(_M0L6_2atmpS1910);
    return _result_2253;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS761,
  int32_t _M0L5indexS762
) {
  int32_t _M0L3lenS760;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS760 = _M0L4selfS761->$1;
  if (_M0L5indexS762 >= 0 && _M0L5indexS762 < _M0L3lenS760) {
    uint8_t* _M0L6_2atmpS1911;
    int32_t _result_2254;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1911 = _M0MPC15array5Array6bufferGbE(_M0L4selfS761);
    _result_2254 = (int32_t)_M0L6_2atmpS1911[_M0L5indexS762];
    moonbit_decref_cycle_free(_M0L6_2atmpS1911);
    return _result_2254;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS764,
  int32_t _M0L5indexS765
) {
  int32_t _M0L3lenS763;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS763 = _M0L4selfS764->$1;
  if (_M0L5indexS765 >= 0 && _M0L5indexS765 < _M0L3lenS763) {
    int32_t* _M0L6_2atmpS1912;
    int32_t _result_2255;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1912 = _M0MPC15array5Array6bufferGiE(_M0L4selfS764);
    _result_2255 = (int32_t)_M0L6_2atmpS1912[_M0L5indexS765];
    moonbit_decref_cycle_free(_M0L6_2atmpS1912);
    return _result_2255;
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
    moonbit_string_t* _M0L6_2atmpS1913;
    moonbit_string_t _M0L6_2atmpS2179;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1913 = _M0MPC15array5Array6bufferGsE(_M0L4selfS767);
    _M0L6_2atmpS2179 = (moonbit_string_t)_M0L6_2atmpS1913[_M0L5indexS768];
    moonbit_incref_cycle_free(_M0L6_2atmpS2179);
    moonbit_decref_cycle_free(_M0L6_2atmpS1913);
    return _M0L6_2atmpS2179;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array28unsafe__truncate__to__lengthGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS756,
  int32_t _M0L8new__lenS755
) {
  int32_t _M0L3lenS1909;
  #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1909 = _M0L4selfS756->$1;
  if (_M0L8new__lenS755 <= _M0L3lenS1909) {
    _M0L4selfS756->$1 = _M0L8new__lenS755;
  } else {
    #line 180 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS754) {
  moonbit_string_t _M0L6_2atmpS1908;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1908 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS754);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1908);
  moonbit_decref_cycle_free(_M0L6_2atmpS1908);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS753) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS753);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS738) {
  uint64_t _M0L4bitsS741;
  uint64_t _M0L6_2atmpS1907;
  uint64_t _M0L6_2atmpS1906;
  int32_t _M0L8ieeeSignS742;
  uint64_t _M0L12ieeeMantissaS743;
  uint64_t _M0L6_2atmpS1905;
  uint64_t _M0L6_2atmpS1904;
  int32_t _M0L12ieeeExponentS744;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS745;
  struct _M0TPB17FloatingDecimal64* _M0L1vS746;
  moonbit_string_t _result_2257;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS738 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  if (_M0L3valS738 >= -0x1p+53 && _M0L3valS738 <= 0x1p+53) {
    if (_M0L3valS738 >= -0x1p+31 && _M0L3valS738 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS739;
      double _M0L6_2atmpS1893;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS739 = _M0MPC16double6Double7to__int(_M0L3valS738);
      _M0L6_2atmpS1893 = (double)_M0L1iS739;
      if (_M0L6_2atmpS1893 == _M0L3valS738) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS739, 10);
      }
    } else {
      int64_t _M0L1iS740;
      double _M0L6_2atmpS1894;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS740 = _M0MPC16double6Double9to__int64(_M0L3valS738);
      _M0L6_2atmpS1894 = (double)_M0L1iS740;
      if (_M0L6_2atmpS1894 == _M0L3valS738) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS740, 10);
      }
    }
  }
  _M0L4bitsS741 = *(int64_t*)&_M0L3valS738;
  _M0L6_2atmpS1907 = _M0L4bitsS741 >> 63;
  _M0L6_2atmpS1906 = _M0L6_2atmpS1907 & 1ull;
  _M0L8ieeeSignS742 = _M0L6_2atmpS1906 != 0ull;
  _M0L12ieeeMantissaS743 = _M0L4bitsS741 & 4503599627370495ull;
  _M0L6_2atmpS1905 = _M0L4bitsS741 >> 52;
  _M0L6_2atmpS1904 = _M0L6_2atmpS1905 & 2047ull;
  _M0L12ieeeExponentS744 = (int32_t)_M0L6_2atmpS1904;
  if (
    _M0L12ieeeExponentS744 == 2047
    || _M0L12ieeeExponentS744 == 0 && _M0L12ieeeMantissaS743 == 0ull
  ) {
    int32_t _M0L6_2atmpS1895 = _M0L12ieeeExponentS744 != 0;
    int32_t _M0L6_2atmpS1896 = _M0L12ieeeMantissaS743 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS742, _M0L6_2atmpS1895, _M0L6_2atmpS1896);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS745
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS743, _M0L12ieeeExponentS744);
  if (_M0L7_2abindS745 == 0) {
    uint32_t _M0L6_2atmpS1897;
    if (_M0L7_2abindS745) {
      moonbit_decref_cycle_free(_M0L7_2abindS745);
    }
    _M0L6_2atmpS1897 = *(uint32_t*)&_M0L12ieeeExponentS744;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS746 = _M0FPB3d2d(_M0L12ieeeMantissaS743, _M0L6_2atmpS1897);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS747 = _M0L7_2abindS745;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS748 = _M0L7_2aSomeS747;
    struct _M0TPB17FloatingDecimal64* _M0L1xS749 = _M0L4_2afS748;
    while (1) {
      uint64_t _M0L8mantissaS1903 = _M0L1xS749->$0;
      uint64_t _M0L1qS750 = _M0L8mantissaS1903 / 10ull;
      uint64_t _M0L8mantissaS1901 = _M0L1xS749->$0;
      uint64_t _M0L6_2atmpS1902 = 10ull * _M0L1qS750;
      uint64_t _M0L1rS751 = _M0L8mantissaS1901 - _M0L6_2atmpS1902;
      int32_t _M0L8exponentS1900;
      int32_t _M0L6_2atmpS1899;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1898;
      if (_M0L1rS751 != 0ull) {
        _M0L1vS746 = _M0L1xS749;
        break;
      }
      _M0L8exponentS1900 = _M0L1xS749->$1;
      moonbit_decref_cycle_free(_M0L1xS749);
      _M0L6_2atmpS1899 = _M0L8exponentS1900 + 1;
      _M0L6_2atmpS1898
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1898)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1898->$0 = _M0L1qS750;
      _M0L6_2atmpS1898->$1 = _M0L6_2atmpS1899;
      _M0L1xS749 = _M0L6_2atmpS1898;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2257 = _M0FPB9to__chars(_M0L1vS746, _M0L8ieeeSignS742);
  moonbit_decref_cycle_free(_M0L1vS746);
  return _result_2257;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS733,
  int32_t _M0L12ieeeExponentS735
) {
  uint64_t _M0L2m2S732;
  int32_t _M0L6_2atmpS1892;
  int32_t _M0L2e2S734;
  int32_t _M0L6_2atmpS1891;
  uint64_t _M0L6_2atmpS1890;
  uint64_t _M0L4maskS736;
  uint64_t _M0L8fractionS737;
  int32_t _M0L6_2atmpS1889;
  uint64_t _M0L6_2atmpS1888;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1887;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S732 = 4503599627370496ull | _M0L12ieeeMantissaS733;
  _M0L6_2atmpS1892 = _M0L12ieeeExponentS735 - 1023;
  _M0L2e2S734 = _M0L6_2atmpS1892 - 52;
  if (_M0L2e2S734 > 0) {
    return 0;
  }
  if (_M0L2e2S734 < -52) {
    return 0;
  }
  _M0L6_2atmpS1891 = -_M0L2e2S734;
  _M0L6_2atmpS1890 = 1ull << (_M0L6_2atmpS1891 & 63);
  _M0L4maskS736 = _M0L6_2atmpS1890 - 1ull;
  _M0L8fractionS737 = _M0L2m2S732 & _M0L4maskS736;
  if (_M0L8fractionS737 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1889 = -_M0L2e2S734;
  _M0L6_2atmpS1888 = _M0L2m2S732 >> (_M0L6_2atmpS1889 & 63);
  _M0L6_2atmpS1887
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1887)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1887->$0 = _M0L6_2atmpS1888;
  _M0L6_2atmpS1887->$1 = 0;
  return _M0L6_2atmpS1887;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS700,
  int32_t _M0L4signS698
) {
  moonbit_bytes_t _M0L6resultS696;
  int32_t _M0Lm5indexS697;
  uint64_t _M0L6outputS699;
  int32_t _M0L7olengthS701;
  int32_t _M0L8exponentS1886;
  int32_t _M0L6_2atmpS1885;
  int32_t _M0Lm3expS702;
  int32_t _M0L6_2atmpS1884;
  int32_t _M0L6_2atmpS1882;
  int32_t _M0L18scientificNotationS703;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS696 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS697 = 0;
  if (_M0L4signS698) {
    int32_t _M0L6_2atmpS1756 = _M0Lm5indexS697;
    int32_t _M0L6_2atmpS1757;
    if (
      _M0L6_2atmpS1756 < 0
      || _M0L6_2atmpS1756 >= Moonbit_array_length(_M0L6resultS696)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS696[_M0L6_2atmpS1756] = 45;
    _M0L6_2atmpS1757 = _M0Lm5indexS697;
    _M0Lm5indexS697 = _M0L6_2atmpS1757 + 1;
  }
  _M0L6outputS699 = _M0L1vS700->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS701 = _M0FPB17decimal__length17(_M0L6outputS699);
  _M0L8exponentS1886 = _M0L1vS700->$1;
  _M0L6_2atmpS1885 = _M0L8exponentS1886 + _M0L7olengthS701;
  _M0Lm3expS702 = _M0L6_2atmpS1885 - 1;
  _M0L6_2atmpS1884 = _M0Lm3expS702;
  if (_M0L6_2atmpS1884 >= -6) {
    int32_t _M0L6_2atmpS1883 = _M0Lm3expS702;
    _M0L6_2atmpS1882 = _M0L6_2atmpS1883 < 21;
  } else {
    _M0L6_2atmpS1882 = 0;
  }
  _M0L18scientificNotationS703 = !_M0L6_2atmpS1882;
  if (_M0L18scientificNotationS703) {
    int32_t _M0L7_2abindS704 = _M0L7olengthS701 - 1;
    uint64_t _M0L6outputS705;
    int32_t _M0L1iS706 = 0;
    uint64_t _M0L6outputS707 = _M0L6outputS699;
    int32_t _M0L6_2atmpS1758;
    int32_t _M0L6_2atmpS1762;
    int32_t _M0L6_2atmpS1761;
    int32_t _M0L6_2atmpS1760;
    int32_t _M0L6_2atmpS1759;
    int32_t _M0L6_2atmpS1766;
    int32_t _M0L6_2atmpS1767;
    int32_t _M0L6_2atmpS1768;
    int32_t _M0L6_2atmpS1769;
    int32_t _M0L6_2atmpS1770;
    int32_t _M0L6_2atmpS1776;
    int32_t _M0L6_2atmpS1809;
    moonbit_string_t _result_2259;
    while (1) {
      if (_M0L1iS706 < _M0L7_2abindS704) {
        uint64_t _M0L1cS708 = _M0L6outputS707 % 10ull;
        int32_t _M0L6_2atmpS1815 = _M0Lm5indexS697;
        int32_t _M0L6_2atmpS1814 = _M0L6_2atmpS1815 + _M0L7olengthS701;
        int32_t _M0L6_2atmpS1810 = _M0L6_2atmpS1814 - _M0L1iS706;
        int32_t _M0L6_2atmpS1813 = (int32_t)_M0L1cS708;
        int32_t _M0L6_2atmpS1812 = 48 + _M0L6_2atmpS1813;
        int32_t _M0L6_2atmpS1811 = _M0L6_2atmpS1812 & 0xff;
        int32_t _M0L6_2atmpS1816;
        uint64_t _M0L6_2atmpS1817;
        if (
          _M0L6_2atmpS1810 < 0
          || _M0L6_2atmpS1810 >= Moonbit_array_length(_M0L6resultS696)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS696[_M0L6_2atmpS1810] = _M0L6_2atmpS1811;
        _M0L6_2atmpS1816 = _M0L1iS706 + 1;
        _M0L6_2atmpS1817 = _M0L6outputS707 / 10ull;
        _M0L1iS706 = _M0L6_2atmpS1816;
        _M0L6outputS707 = _M0L6_2atmpS1817;
        continue;
      } else {
        _M0L6outputS705 = _M0L6outputS707;
      }
      break;
    }
    _M0L6_2atmpS1758 = _M0Lm5indexS697;
    _M0L6_2atmpS1762 = (int32_t)_M0L6outputS705;
    _M0L6_2atmpS1761 = _M0L6_2atmpS1762 % 10;
    _M0L6_2atmpS1760 = 48 + _M0L6_2atmpS1761;
    _M0L6_2atmpS1759 = _M0L6_2atmpS1760 & 0xff;
    if (
      _M0L6_2atmpS1758 < 0
      || _M0L6_2atmpS1758 >= Moonbit_array_length(_M0L6resultS696)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS696[_M0L6_2atmpS1758] = _M0L6_2atmpS1759;
    if (_M0L7olengthS701 > 1) {
      int32_t _M0L6_2atmpS1764 = _M0Lm5indexS697;
      int32_t _M0L6_2atmpS1763 = _M0L6_2atmpS1764 + 1;
      if (
        _M0L6_2atmpS1763 < 0
        || _M0L6_2atmpS1763 >= Moonbit_array_length(_M0L6resultS696)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS696[_M0L6_2atmpS1763] = 46;
    } else {
      int32_t _M0L6_2atmpS1765 = _M0Lm5indexS697;
      _M0Lm5indexS697 = _M0L6_2atmpS1765 - 1;
    }
    _M0L6_2atmpS1766 = _M0Lm5indexS697;
    _M0L6_2atmpS1767 = _M0L7olengthS701 + 1;
    _M0Lm5indexS697 = _M0L6_2atmpS1766 + _M0L6_2atmpS1767;
    _M0L6_2atmpS1768 = _M0Lm5indexS697;
    if (
      _M0L6_2atmpS1768 < 0
      || _M0L6_2atmpS1768 >= Moonbit_array_length(_M0L6resultS696)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS696[_M0L6_2atmpS1768] = 101;
    _M0L6_2atmpS1769 = _M0Lm5indexS697;
    _M0Lm5indexS697 = _M0L6_2atmpS1769 + 1;
    _M0L6_2atmpS1770 = _M0Lm3expS702;
    if (_M0L6_2atmpS1770 < 0) {
      int32_t _M0L6_2atmpS1771 = _M0Lm5indexS697;
      int32_t _M0L6_2atmpS1772;
      int32_t _M0L6_2atmpS1773;
      if (
        _M0L6_2atmpS1771 < 0
        || _M0L6_2atmpS1771 >= Moonbit_array_length(_M0L6resultS696)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS696[_M0L6_2atmpS1771] = 45;
      _M0L6_2atmpS1772 = _M0Lm5indexS697;
      _M0Lm5indexS697 = _M0L6_2atmpS1772 + 1;
      _M0L6_2atmpS1773 = _M0Lm3expS702;
      _M0Lm3expS702 = -_M0L6_2atmpS1773;
    } else {
      int32_t _M0L6_2atmpS1774 = _M0Lm5indexS697;
      int32_t _M0L6_2atmpS1775;
      if (
        _M0L6_2atmpS1774 < 0
        || _M0L6_2atmpS1774 >= Moonbit_array_length(_M0L6resultS696)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS696[_M0L6_2atmpS1774] = 43;
      _M0L6_2atmpS1775 = _M0Lm5indexS697;
      _M0Lm5indexS697 = _M0L6_2atmpS1775 + 1;
    }
    _M0L6_2atmpS1776 = _M0Lm3expS702;
    if (_M0L6_2atmpS1776 >= 100) {
      int32_t _M0L6_2atmpS1792 = _M0Lm3expS702;
      int32_t _M0L1aS710 = _M0L6_2atmpS1792 / 100;
      int32_t _M0L6_2atmpS1791 = _M0Lm3expS702;
      int32_t _M0L6_2atmpS1790 = _M0L6_2atmpS1791 / 10;
      int32_t _M0L1bS711 = _M0L6_2atmpS1790 % 10;
      int32_t _M0L6_2atmpS1789 = _M0Lm3expS702;
      int32_t _M0L1cS712 = _M0L6_2atmpS1789 % 10;
      int32_t _M0L6_2atmpS1777 = _M0Lm5indexS697;
      int32_t _M0L6_2atmpS1779 = 48 + _M0L1aS710;
      int32_t _M0L6_2atmpS1778 = _M0L6_2atmpS1779 & 0xff;
      int32_t _M0L6_2atmpS1783;
      int32_t _M0L6_2atmpS1780;
      int32_t _M0L6_2atmpS1782;
      int32_t _M0L6_2atmpS1781;
      int32_t _M0L6_2atmpS1787;
      int32_t _M0L6_2atmpS1784;
      int32_t _M0L6_2atmpS1786;
      int32_t _M0L6_2atmpS1785;
      int32_t _M0L6_2atmpS1788;
      if (
        _M0L6_2atmpS1777 < 0
        || _M0L6_2atmpS1777 >= Moonbit_array_length(_M0L6resultS696)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS696[_M0L6_2atmpS1777] = _M0L6_2atmpS1778;
      _M0L6_2atmpS1783 = _M0Lm5indexS697;
      _M0L6_2atmpS1780 = _M0L6_2atmpS1783 + 1;
      _M0L6_2atmpS1782 = 48 + _M0L1bS711;
      _M0L6_2atmpS1781 = _M0L6_2atmpS1782 & 0xff;
      if (
        _M0L6_2atmpS1780 < 0
        || _M0L6_2atmpS1780 >= Moonbit_array_length(_M0L6resultS696)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS696[_M0L6_2atmpS1780] = _M0L6_2atmpS1781;
      _M0L6_2atmpS1787 = _M0Lm5indexS697;
      _M0L6_2atmpS1784 = _M0L6_2atmpS1787 + 2;
      _M0L6_2atmpS1786 = 48 + _M0L1cS712;
      _M0L6_2atmpS1785 = _M0L6_2atmpS1786 & 0xff;
      if (
        _M0L6_2atmpS1784 < 0
        || _M0L6_2atmpS1784 >= Moonbit_array_length(_M0L6resultS696)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS696[_M0L6_2atmpS1784] = _M0L6_2atmpS1785;
      _M0L6_2atmpS1788 = _M0Lm5indexS697;
      _M0Lm5indexS697 = _M0L6_2atmpS1788 + 3;
    } else {
      int32_t _M0L6_2atmpS1793 = _M0Lm3expS702;
      if (_M0L6_2atmpS1793 >= 10) {
        int32_t _M0L6_2atmpS1803 = _M0Lm3expS702;
        int32_t _M0L1aS713 = _M0L6_2atmpS1803 / 10;
        int32_t _M0L6_2atmpS1802 = _M0Lm3expS702;
        int32_t _M0L1bS714 = _M0L6_2atmpS1802 % 10;
        int32_t _M0L6_2atmpS1794 = _M0Lm5indexS697;
        int32_t _M0L6_2atmpS1796 = 48 + _M0L1aS713;
        int32_t _M0L6_2atmpS1795 = _M0L6_2atmpS1796 & 0xff;
        int32_t _M0L6_2atmpS1800;
        int32_t _M0L6_2atmpS1797;
        int32_t _M0L6_2atmpS1799;
        int32_t _M0L6_2atmpS1798;
        int32_t _M0L6_2atmpS1801;
        if (
          _M0L6_2atmpS1794 < 0
          || _M0L6_2atmpS1794 >= Moonbit_array_length(_M0L6resultS696)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS696[_M0L6_2atmpS1794] = _M0L6_2atmpS1795;
        _M0L6_2atmpS1800 = _M0Lm5indexS697;
        _M0L6_2atmpS1797 = _M0L6_2atmpS1800 + 1;
        _M0L6_2atmpS1799 = 48 + _M0L1bS714;
        _M0L6_2atmpS1798 = _M0L6_2atmpS1799 & 0xff;
        if (
          _M0L6_2atmpS1797 < 0
          || _M0L6_2atmpS1797 >= Moonbit_array_length(_M0L6resultS696)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS696[_M0L6_2atmpS1797] = _M0L6_2atmpS1798;
        _M0L6_2atmpS1801 = _M0Lm5indexS697;
        _M0Lm5indexS697 = _M0L6_2atmpS1801 + 2;
      } else {
        int32_t _M0L6_2atmpS1804 = _M0Lm5indexS697;
        int32_t _M0L6_2atmpS1807 = _M0Lm3expS702;
        int32_t _M0L6_2atmpS1806 = 48 + _M0L6_2atmpS1807;
        int32_t _M0L6_2atmpS1805 = _M0L6_2atmpS1806 & 0xff;
        int32_t _M0L6_2atmpS1808;
        if (
          _M0L6_2atmpS1804 < 0
          || _M0L6_2atmpS1804 >= Moonbit_array_length(_M0L6resultS696)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS696[_M0L6_2atmpS1804] = _M0L6_2atmpS1805;
        _M0L6_2atmpS1808 = _M0Lm5indexS697;
        _M0Lm5indexS697 = _M0L6_2atmpS1808 + 1;
      }
    }
    _M0L6_2atmpS1809 = _M0Lm5indexS697;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2259
    = _M0FPB19string__from__bytes(_M0L6resultS696, 0, _M0L6_2atmpS1809);
    moonbit_decref_cycle_free(_M0L6resultS696);
    return _result_2259;
  } else {
    int32_t _M0L6_2atmpS1818 = _M0Lm3expS702;
    int32_t _M0L6_2atmpS1881;
    moonbit_string_t _result_2265;
    if (_M0L6_2atmpS1818 < 0) {
      int32_t _M0L6_2atmpS1819 = _M0Lm5indexS697;
      int32_t _M0L6_2atmpS1821;
      int32_t _M0L6_2atmpS1820;
      int32_t _M0L6_2atmpS1822;
      int32_t _M0L1iS715;
      int32_t _M0L6_2atmpS1837;
      int32_t _M0L6_2atmpS1839;
      int32_t _M0L6_2atmpS1838;
      int32_t _M0L7currentS717;
      int32_t _M0L1iS718;
      uint64_t _M0L6outputS719;
      if (
        _M0L6_2atmpS1819 < 0
        || _M0L6_2atmpS1819 >= Moonbit_array_length(_M0L6resultS696)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS696[_M0L6_2atmpS1819] = 48;
      _M0L6_2atmpS1821 = _M0Lm5indexS697;
      _M0L6_2atmpS1820 = _M0L6_2atmpS1821 + 1;
      if (
        _M0L6_2atmpS1820 < 0
        || _M0L6_2atmpS1820 >= Moonbit_array_length(_M0L6resultS696)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS696[_M0L6_2atmpS1820] = 46;
      _M0L6_2atmpS1822 = _M0Lm5indexS697;
      _M0Lm5indexS697 = _M0L6_2atmpS1822 + 2;
      _M0L1iS715 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1823 = _M0Lm3expS702;
        if (_M0L1iS715 > _M0L6_2atmpS1823) {
          int32_t _M0L6_2atmpS1826 = _M0Lm5indexS697;
          int32_t _M0L6_2atmpS1825 = _M0L6_2atmpS1826 - _M0L1iS715;
          int32_t _M0L6_2atmpS1824 = _M0L6_2atmpS1825 - 1;
          int32_t _M0L6_2atmpS1827;
          if (
            _M0L6_2atmpS1824 < 0
            || _M0L6_2atmpS1824 >= Moonbit_array_length(_M0L6resultS696)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS696[_M0L6_2atmpS1824] = 48;
          _M0L6_2atmpS1827 = _M0L1iS715 - 1;
          _M0L1iS715 = _M0L6_2atmpS1827;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1837 = _M0Lm5indexS697;
      _M0L6_2atmpS1839 = _M0Lm3expS702;
      _M0L6_2atmpS1838 = -1 - _M0L6_2atmpS1839;
      _M0L7currentS717 = _M0L6_2atmpS1837 + _M0L6_2atmpS1838;
      _M0L1iS718 = 0;
      _M0L6outputS719 = _M0L6outputS699;
      while (1) {
        if (_M0L1iS718 < _M0L7olengthS701) {
          int32_t _M0L6_2atmpS1834 = _M0L7currentS717 + _M0L7olengthS701;
          int32_t _M0L6_2atmpS1833 = _M0L6_2atmpS1834 - _M0L1iS718;
          int32_t _M0L6_2atmpS1828 = _M0L6_2atmpS1833 - 1;
          uint64_t _M0L6_2atmpS1832 = _M0L6outputS719 % 10ull;
          int32_t _M0L6_2atmpS1831 = (int32_t)_M0L6_2atmpS1832;
          int32_t _M0L6_2atmpS1830 = 48 + _M0L6_2atmpS1831;
          int32_t _M0L6_2atmpS1829 = _M0L6_2atmpS1830 & 0xff;
          int32_t _M0L6_2atmpS1835;
          uint64_t _M0L6_2atmpS1836;
          if (
            _M0L6_2atmpS1828 < 0
            || _M0L6_2atmpS1828 >= Moonbit_array_length(_M0L6resultS696)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS696[_M0L6_2atmpS1828] = _M0L6_2atmpS1829;
          _M0L6_2atmpS1835 = _M0L1iS718 + 1;
          _M0L6_2atmpS1836 = _M0L6outputS719 / 10ull;
          _M0L1iS718 = _M0L6_2atmpS1835;
          _M0L6outputS719 = _M0L6_2atmpS1836;
          continue;
        }
        break;
      }
      _M0Lm5indexS697 = _M0L7currentS717 + _M0L7olengthS701;
    } else {
      int32_t _M0L6_2atmpS1841 = _M0Lm3expS702;
      int32_t _M0L6_2atmpS1840 = _M0L6_2atmpS1841 + 1;
      if (_M0L6_2atmpS1840 >= _M0L7olengthS701) {
        int32_t _M0L1iS721 = 0;
        uint64_t _M0L6outputS722 = _M0L6outputS699;
        int32_t _M0L6_2atmpS1852;
        int32_t _M0L6_2atmpS1857;
        int32_t _M0L7_2abindS724;
        int32_t _M0L1iS725;
        int32_t _M0L6_2atmpS1858;
        int32_t _M0L6_2atmpS1861;
        int32_t _M0L6_2atmpS1860;
        int32_t _M0L6_2atmpS1859;
        while (1) {
          if (_M0L1iS721 < _M0L7olengthS701) {
            int32_t _M0L6_2atmpS1849 = _M0Lm5indexS697;
            int32_t _M0L6_2atmpS1848 = _M0L6_2atmpS1849 + _M0L7olengthS701;
            int32_t _M0L6_2atmpS1847 = _M0L6_2atmpS1848 - _M0L1iS721;
            int32_t _M0L6_2atmpS1842 = _M0L6_2atmpS1847 - 1;
            uint64_t _M0L6_2atmpS1846 = _M0L6outputS722 % 10ull;
            int32_t _M0L6_2atmpS1845 = (int32_t)_M0L6_2atmpS1846;
            int32_t _M0L6_2atmpS1844 = 48 + _M0L6_2atmpS1845;
            int32_t _M0L6_2atmpS1843 = _M0L6_2atmpS1844 & 0xff;
            int32_t _M0L6_2atmpS1850;
            uint64_t _M0L6_2atmpS1851;
            if (
              _M0L6_2atmpS1842 < 0
              || _M0L6_2atmpS1842 >= Moonbit_array_length(_M0L6resultS696)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS696[_M0L6_2atmpS1842] = _M0L6_2atmpS1843;
            _M0L6_2atmpS1850 = _M0L1iS721 + 1;
            _M0L6_2atmpS1851 = _M0L6outputS722 / 10ull;
            _M0L1iS721 = _M0L6_2atmpS1850;
            _M0L6outputS722 = _M0L6_2atmpS1851;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1852 = _M0Lm5indexS697;
        _M0Lm5indexS697 = _M0L6_2atmpS1852 + _M0L7olengthS701;
        _M0L6_2atmpS1857 = _M0Lm3expS702;
        _M0L7_2abindS724 = _M0L6_2atmpS1857 + 1;
        _M0L1iS725 = _M0L7olengthS701;
        while (1) {
          if (_M0L1iS725 < _M0L7_2abindS724) {
            int32_t _M0L6_2atmpS1855 = _M0Lm5indexS697;
            int32_t _M0L6_2atmpS1854 = _M0L6_2atmpS1855 + _M0L1iS725;
            int32_t _M0L6_2atmpS1853 = _M0L6_2atmpS1854 - _M0L7olengthS701;
            int32_t _M0L6_2atmpS1856;
            if (
              _M0L6_2atmpS1853 < 0
              || _M0L6_2atmpS1853 >= Moonbit_array_length(_M0L6resultS696)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS696[_M0L6_2atmpS1853] = 48;
            _M0L6_2atmpS1856 = _M0L1iS725 + 1;
            _M0L1iS725 = _M0L6_2atmpS1856;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1858 = _M0Lm5indexS697;
        _M0L6_2atmpS1861 = _M0Lm3expS702;
        _M0L6_2atmpS1860 = _M0L6_2atmpS1861 + 1;
        _M0L6_2atmpS1859 = _M0L6_2atmpS1860 - _M0L7olengthS701;
        _M0Lm5indexS697 = _M0L6_2atmpS1858 + _M0L6_2atmpS1859;
      } else {
        int32_t _M0L6_2atmpS1878 = _M0Lm5indexS697;
        int32_t _M0L6_2atmpS1877 = _M0L6_2atmpS1878 + 1;
        int32_t _M0L1iS727 = 0;
        int32_t _M0L7currentS728 = _M0L6_2atmpS1877;
        uint64_t _M0L6outputS729 = _M0L6outputS699;
        int32_t _M0L6_2atmpS1879;
        int32_t _M0L6_2atmpS1880;
        while (1) {
          if (_M0L1iS727 < _M0L7olengthS701) {
            int32_t _M0L6_2atmpS1873 = _M0L7olengthS701 - _M0L1iS727;
            int32_t _M0L6_2atmpS1871 = _M0L6_2atmpS1873 - 1;
            int32_t _M0L6_2atmpS1872 = _M0Lm3expS702;
            int32_t _M0L7currentS730;
            int32_t _M0L6_2atmpS1868;
            int32_t _M0L6_2atmpS1867;
            int32_t _M0L6_2atmpS1862;
            uint64_t _M0L6_2atmpS1866;
            int32_t _M0L6_2atmpS1865;
            int32_t _M0L6_2atmpS1864;
            int32_t _M0L6_2atmpS1863;
            int32_t _M0L6_2atmpS1869;
            uint64_t _M0L6_2atmpS1870;
            if (_M0L6_2atmpS1871 == _M0L6_2atmpS1872) {
              int32_t _M0L6_2atmpS1876 = _M0L7currentS728 + _M0L7olengthS701;
              int32_t _M0L6_2atmpS1875 = _M0L6_2atmpS1876 - _M0L1iS727;
              int32_t _M0L6_2atmpS1874 = _M0L6_2atmpS1875 - 1;
              if (
                _M0L6_2atmpS1874 < 0
                || _M0L6_2atmpS1874 >= Moonbit_array_length(_M0L6resultS696)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS696[_M0L6_2atmpS1874] = 46;
              _M0L7currentS730 = _M0L7currentS728 - 1;
            } else {
              _M0L7currentS730 = _M0L7currentS728;
            }
            _M0L6_2atmpS1868 = _M0L7currentS730 + _M0L7olengthS701;
            _M0L6_2atmpS1867 = _M0L6_2atmpS1868 - _M0L1iS727;
            _M0L6_2atmpS1862 = _M0L6_2atmpS1867 - 1;
            _M0L6_2atmpS1866 = _M0L6outputS729 % 10ull;
            _M0L6_2atmpS1865 = (int32_t)_M0L6_2atmpS1866;
            _M0L6_2atmpS1864 = 48 + _M0L6_2atmpS1865;
            _M0L6_2atmpS1863 = _M0L6_2atmpS1864 & 0xff;
            if (
              _M0L6_2atmpS1862 < 0
              || _M0L6_2atmpS1862 >= Moonbit_array_length(_M0L6resultS696)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS696[_M0L6_2atmpS1862] = _M0L6_2atmpS1863;
            _M0L6_2atmpS1869 = _M0L1iS727 + 1;
            _M0L6_2atmpS1870 = _M0L6outputS729 / 10ull;
            _M0L1iS727 = _M0L6_2atmpS1869;
            _M0L7currentS728 = _M0L7currentS730;
            _M0L6outputS729 = _M0L6_2atmpS1870;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1879 = _M0Lm5indexS697;
        _M0L6_2atmpS1880 = _M0L7olengthS701 + 1;
        _M0Lm5indexS697 = _M0L6_2atmpS1879 + _M0L6_2atmpS1880;
      }
    }
    _M0L6_2atmpS1881 = _M0Lm5indexS697;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2265
    = _M0FPB19string__from__bytes(_M0L6resultS696, 0, _M0L6_2atmpS1881);
    moonbit_decref_cycle_free(_M0L6resultS696);
    return _result_2265;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS642,
  uint32_t _M0L12ieeeExponentS641
) {
  int32_t _M0Lm2e2S639;
  uint64_t _M0Lm2m2S640;
  uint64_t _M0L6_2atmpS1755;
  uint64_t _M0L6_2atmpS1754;
  int32_t _M0L4evenS643;
  uint64_t _M0L6_2atmpS1753;
  uint64_t _M0L2mvS644;
  int32_t _M0L7mmShiftS645;
  uint64_t _M0Lm2vrS646;
  uint64_t _M0Lm2vpS647;
  uint64_t _M0Lm2vmS648;
  int32_t _M0Lm3e10S649;
  int32_t _M0Lm17vmIsTrailingZerosS650;
  int32_t _M0Lm17vrIsTrailingZerosS651;
  int32_t _M0L6_2atmpS1655;
  int32_t _M0Lm7removedS670;
  int32_t _M0Lm16lastRemovedDigitS671;
  uint64_t _M0Lm6outputS672;
  int32_t _M0L6_2atmpS1751;
  int32_t _M0L6_2atmpS1752;
  int32_t _M0L3expS695;
  uint64_t _M0L6_2atmpS1750;
  struct _M0TPB17FloatingDecimal64* _block_2271;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S639 = 0;
  _M0Lm2m2S640 = 0ull;
  if (_M0L12ieeeExponentS641 == 0u) {
    _M0Lm2e2S639 = -1076;
    _M0Lm2m2S640 = _M0L12ieeeMantissaS642;
  } else {
    int32_t _M0L6_2atmpS1654 = *(int32_t*)&_M0L12ieeeExponentS641;
    int32_t _M0L6_2atmpS1653 = _M0L6_2atmpS1654 - 1023;
    int32_t _M0L6_2atmpS1652 = _M0L6_2atmpS1653 - 52;
    _M0Lm2e2S639 = _M0L6_2atmpS1652 - 2;
    _M0Lm2m2S640 = 4503599627370496ull | _M0L12ieeeMantissaS642;
  }
  _M0L6_2atmpS1755 = _M0Lm2m2S640;
  _M0L6_2atmpS1754 = _M0L6_2atmpS1755 & 1ull;
  _M0L4evenS643 = _M0L6_2atmpS1754 == 0ull;
  _M0L6_2atmpS1753 = _M0Lm2m2S640;
  _M0L2mvS644 = 4ull * _M0L6_2atmpS1753;
  _M0L7mmShiftS645
  = _M0L12ieeeMantissaS642 != 0ull || _M0L12ieeeExponentS641 <= 1u;
  _M0Lm2vrS646 = 0ull;
  _M0Lm2vpS647 = 0ull;
  _M0Lm2vmS648 = 0ull;
  _M0Lm3e10S649 = 0;
  _M0Lm17vmIsTrailingZerosS650 = 0;
  _M0Lm17vrIsTrailingZerosS651 = 0;
  _M0L6_2atmpS1655 = _M0Lm2e2S639;
  if (_M0L6_2atmpS1655 >= 0) {
    int32_t _M0L6_2atmpS1677 = _M0Lm2e2S639;
    int32_t _M0L6_2atmpS1673;
    int32_t _M0L6_2atmpS1676;
    int32_t _M0L6_2atmpS1675;
    int32_t _M0L6_2atmpS1674;
    int32_t _M0L1qS652;
    int32_t _M0L6_2atmpS1672;
    int32_t _M0L6_2atmpS1671;
    int32_t _M0L1kS653;
    int32_t _M0L6_2atmpS1670;
    int32_t _M0L6_2atmpS1669;
    int32_t _M0L6_2atmpS1668;
    int32_t _M0L1iS654;
    struct _M0TPB8Pow5Pair _M0L4pow5S655;
    uint64_t _M0L6_2atmpS1667;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS656;
    uint64_t _M0L8_2avrOutS657;
    uint64_t _M0L8_2avpOutS658;
    uint64_t _M0L8_2avmOutS659;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1673 = _M0FPB9log10Pow2(_M0L6_2atmpS1677);
    _M0L6_2atmpS1676 = _M0Lm2e2S639;
    _M0L6_2atmpS1675 = _M0L6_2atmpS1676 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1674 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1675);
    _M0L1qS652 = _M0L6_2atmpS1673 - _M0L6_2atmpS1674;
    _M0Lm3e10S649 = _M0L1qS652;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1672 = _M0FPB8pow5bits(_M0L1qS652);
    _M0L6_2atmpS1671 = 125 + _M0L6_2atmpS1672;
    _M0L1kS653 = _M0L6_2atmpS1671 - 1;
    _M0L6_2atmpS1670 = _M0Lm2e2S639;
    _M0L6_2atmpS1669 = -_M0L6_2atmpS1670;
    _M0L6_2atmpS1668 = _M0L6_2atmpS1669 + _M0L1qS652;
    _M0L1iS654 = _M0L6_2atmpS1668 + _M0L1kS653;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S655 = _M0FPB22double__computeInvPow5(_M0L1qS652);
    _M0L6_2atmpS1667 = _M0Lm2m2S640;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS656
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1667, _M0L4pow5S655, _M0L1iS654, _M0L7mmShiftS645);
    _M0L8_2avrOutS657 = _M0L7_2abindS656.$0;
    _M0L8_2avpOutS658 = _M0L7_2abindS656.$1;
    _M0L8_2avmOutS659 = _M0L7_2abindS656.$2;
    _M0Lm2vrS646 = _M0L8_2avrOutS657;
    _M0Lm2vpS647 = _M0L8_2avpOutS658;
    _M0Lm2vmS648 = _M0L8_2avmOutS659;
    if (_M0L1qS652 <= 21) {
      int32_t _M0L6_2atmpS1663 = (int32_t)_M0L2mvS644;
      uint64_t _M0L6_2atmpS1666 = _M0L2mvS644 / 5ull;
      int32_t _M0L6_2atmpS1665 = (int32_t)_M0L6_2atmpS1666;
      int32_t _M0L6_2atmpS1664 = 5 * _M0L6_2atmpS1665;
      int32_t _M0L6mvMod5S660 = _M0L6_2atmpS1663 - _M0L6_2atmpS1664;
      if (_M0L6mvMod5S660 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS651
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS644, _M0L1qS652);
      } else if (_M0L4evenS643) {
        uint64_t _M0L6_2atmpS1657 = _M0L2mvS644 - 1ull;
        uint64_t _M0L6_2atmpS1658;
        uint64_t _M0L6_2atmpS1656;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1658 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS645);
        _M0L6_2atmpS1656 = _M0L6_2atmpS1657 - _M0L6_2atmpS1658;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS650
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1656, _M0L1qS652);
      } else {
        uint64_t _M0L6_2atmpS1659 = _M0Lm2vpS647;
        uint64_t _M0L6_2atmpS1662 = _M0L2mvS644 + 2ull;
        int32_t _M0L6_2atmpS1661;
        uint64_t _M0L6_2atmpS1660;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1661
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1662, _M0L1qS652);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1660 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1661);
        _M0Lm2vpS647 = _M0L6_2atmpS1659 - _M0L6_2atmpS1660;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1691 = _M0Lm2e2S639;
    int32_t _M0L6_2atmpS1690 = -_M0L6_2atmpS1691;
    int32_t _M0L6_2atmpS1685;
    int32_t _M0L6_2atmpS1689;
    int32_t _M0L6_2atmpS1688;
    int32_t _M0L6_2atmpS1687;
    int32_t _M0L6_2atmpS1686;
    int32_t _M0L1qS661;
    int32_t _M0L6_2atmpS1678;
    int32_t _M0L6_2atmpS1684;
    int32_t _M0L6_2atmpS1683;
    int32_t _M0L1iS662;
    int32_t _M0L6_2atmpS1682;
    int32_t _M0L1kS663;
    int32_t _M0L1jS664;
    struct _M0TPB8Pow5Pair _M0L4pow5S665;
    uint64_t _M0L6_2atmpS1681;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS666;
    uint64_t _M0L8_2avrOutS667;
    uint64_t _M0L8_2avpOutS668;
    uint64_t _M0L8_2avmOutS669;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1685 = _M0FPB9log10Pow5(_M0L6_2atmpS1690);
    _M0L6_2atmpS1689 = _M0Lm2e2S639;
    _M0L6_2atmpS1688 = -_M0L6_2atmpS1689;
    _M0L6_2atmpS1687 = _M0L6_2atmpS1688 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1686 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1687);
    _M0L1qS661 = _M0L6_2atmpS1685 - _M0L6_2atmpS1686;
    _M0L6_2atmpS1678 = _M0Lm2e2S639;
    _M0Lm3e10S649 = _M0L1qS661 + _M0L6_2atmpS1678;
    _M0L6_2atmpS1684 = _M0Lm2e2S639;
    _M0L6_2atmpS1683 = -_M0L6_2atmpS1684;
    _M0L1iS662 = _M0L6_2atmpS1683 - _M0L1qS661;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1682 = _M0FPB8pow5bits(_M0L1iS662);
    _M0L1kS663 = _M0L6_2atmpS1682 - 125;
    _M0L1jS664 = _M0L1qS661 - _M0L1kS663;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S665 = _M0FPB19double__computePow5(_M0L1iS662);
    _M0L6_2atmpS1681 = _M0Lm2m2S640;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS666
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1681, _M0L4pow5S665, _M0L1jS664, _M0L7mmShiftS645);
    _M0L8_2avrOutS667 = _M0L7_2abindS666.$0;
    _M0L8_2avpOutS668 = _M0L7_2abindS666.$1;
    _M0L8_2avmOutS669 = _M0L7_2abindS666.$2;
    _M0Lm2vrS646 = _M0L8_2avrOutS667;
    _M0Lm2vpS647 = _M0L8_2avpOutS668;
    _M0Lm2vmS648 = _M0L8_2avmOutS669;
    if (_M0L1qS661 <= 1) {
      _M0Lm17vrIsTrailingZerosS651 = 1;
      if (_M0L4evenS643) {
        int32_t _M0L6_2atmpS1679;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1679 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS645);
        _M0Lm17vmIsTrailingZerosS650 = _M0L6_2atmpS1679 == 1;
      } else {
        uint64_t _M0L6_2atmpS1680 = _M0Lm2vpS647;
        _M0Lm2vpS647 = _M0L6_2atmpS1680 - 1ull;
      }
    } else if (_M0L1qS661 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS651
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS644, _M0L1qS661);
    }
  }
  _M0Lm7removedS670 = 0;
  _M0Lm16lastRemovedDigitS671 = 0;
  _M0Lm6outputS672 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS650 || _M0Lm17vrIsTrailingZerosS651) {
    int32_t _if__result_2268;
    uint64_t _M0L6_2atmpS1721;
    uint64_t _M0L6_2atmpS1727;
    uint64_t _M0L6_2atmpS1728;
    int32_t _if__result_2269;
    int32_t _M0L6_2atmpS1724;
    int64_t _M0L6_2atmpS1723;
    uint64_t _M0L6_2atmpS1722;
    while (1) {
      uint64_t _M0L6_2atmpS1704 = _M0Lm2vpS647;
      uint64_t _M0L7vpDiv10S673 = _M0L6_2atmpS1704 / 10ull;
      uint64_t _M0L6_2atmpS1703 = _M0Lm2vmS648;
      uint64_t _M0L7vmDiv10S674 = _M0L6_2atmpS1703 / 10ull;
      uint64_t _M0L6_2atmpS1702;
      int32_t _M0L6_2atmpS1699;
      int32_t _M0L6_2atmpS1701;
      int32_t _M0L6_2atmpS1700;
      int32_t _M0L7vmMod10S676;
      uint64_t _M0L6_2atmpS1698;
      uint64_t _M0L7vrDiv10S677;
      uint64_t _M0L6_2atmpS1697;
      int32_t _M0L6_2atmpS1694;
      int32_t _M0L6_2atmpS1696;
      int32_t _M0L6_2atmpS1695;
      int32_t _M0L7vrMod10S678;
      int32_t _M0L6_2atmpS1693;
      if (_M0L7vpDiv10S673 <= _M0L7vmDiv10S674) {
        break;
      }
      _M0L6_2atmpS1702 = _M0Lm2vmS648;
      _M0L6_2atmpS1699 = (int32_t)_M0L6_2atmpS1702;
      _M0L6_2atmpS1701 = (int32_t)_M0L7vmDiv10S674;
      _M0L6_2atmpS1700 = 10 * _M0L6_2atmpS1701;
      _M0L7vmMod10S676 = _M0L6_2atmpS1699 - _M0L6_2atmpS1700;
      _M0L6_2atmpS1698 = _M0Lm2vrS646;
      _M0L7vrDiv10S677 = _M0L6_2atmpS1698 / 10ull;
      _M0L6_2atmpS1697 = _M0Lm2vrS646;
      _M0L6_2atmpS1694 = (int32_t)_M0L6_2atmpS1697;
      _M0L6_2atmpS1696 = (int32_t)_M0L7vrDiv10S677;
      _M0L6_2atmpS1695 = 10 * _M0L6_2atmpS1696;
      _M0L7vrMod10S678 = _M0L6_2atmpS1694 - _M0L6_2atmpS1695;
      _M0Lm17vmIsTrailingZerosS650
      = _M0Lm17vmIsTrailingZerosS650 && _M0L7vmMod10S676 == 0;
      if (_M0Lm17vrIsTrailingZerosS651) {
        int32_t _M0L6_2atmpS1692 = _M0Lm16lastRemovedDigitS671;
        _M0Lm17vrIsTrailingZerosS651 = _M0L6_2atmpS1692 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS651 = 0;
      }
      _M0Lm16lastRemovedDigitS671 = _M0L7vrMod10S678;
      _M0Lm2vrS646 = _M0L7vrDiv10S677;
      _M0Lm2vpS647 = _M0L7vpDiv10S673;
      _M0Lm2vmS648 = _M0L7vmDiv10S674;
      _M0L6_2atmpS1693 = _M0Lm7removedS670;
      _M0Lm7removedS670 = _M0L6_2atmpS1693 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS650) {
      while (1) {
        uint64_t _M0L6_2atmpS1717 = _M0Lm2vmS648;
        uint64_t _M0L7vmDiv10S679 = _M0L6_2atmpS1717 / 10ull;
        uint64_t _M0L6_2atmpS1716 = _M0Lm2vmS648;
        int32_t _M0L6_2atmpS1713 = (int32_t)_M0L6_2atmpS1716;
        int32_t _M0L6_2atmpS1715 = (int32_t)_M0L7vmDiv10S679;
        int32_t _M0L6_2atmpS1714 = 10 * _M0L6_2atmpS1715;
        int32_t _M0L7vmMod10S680 = _M0L6_2atmpS1713 - _M0L6_2atmpS1714;
        uint64_t _M0L6_2atmpS1712;
        uint64_t _M0L7vpDiv10S682;
        uint64_t _M0L6_2atmpS1711;
        uint64_t _M0L7vrDiv10S683;
        uint64_t _M0L6_2atmpS1710;
        int32_t _M0L6_2atmpS1707;
        int32_t _M0L6_2atmpS1709;
        int32_t _M0L6_2atmpS1708;
        int32_t _M0L7vrMod10S684;
        int32_t _M0L6_2atmpS1706;
        if (_M0L7vmMod10S680 != 0) {
          break;
        }
        _M0L6_2atmpS1712 = _M0Lm2vpS647;
        _M0L7vpDiv10S682 = _M0L6_2atmpS1712 / 10ull;
        _M0L6_2atmpS1711 = _M0Lm2vrS646;
        _M0L7vrDiv10S683 = _M0L6_2atmpS1711 / 10ull;
        _M0L6_2atmpS1710 = _M0Lm2vrS646;
        _M0L6_2atmpS1707 = (int32_t)_M0L6_2atmpS1710;
        _M0L6_2atmpS1709 = (int32_t)_M0L7vrDiv10S683;
        _M0L6_2atmpS1708 = 10 * _M0L6_2atmpS1709;
        _M0L7vrMod10S684 = _M0L6_2atmpS1707 - _M0L6_2atmpS1708;
        if (_M0Lm17vrIsTrailingZerosS651) {
          int32_t _M0L6_2atmpS1705 = _M0Lm16lastRemovedDigitS671;
          _M0Lm17vrIsTrailingZerosS651 = _M0L6_2atmpS1705 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS651 = 0;
        }
        _M0Lm16lastRemovedDigitS671 = _M0L7vrMod10S684;
        _M0Lm2vrS646 = _M0L7vrDiv10S683;
        _M0Lm2vpS647 = _M0L7vpDiv10S682;
        _M0Lm2vmS648 = _M0L7vmDiv10S679;
        _M0L6_2atmpS1706 = _M0Lm7removedS670;
        _M0Lm7removedS670 = _M0L6_2atmpS1706 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS651) {
      int32_t _M0L6_2atmpS1720 = _M0Lm16lastRemovedDigitS671;
      if (_M0L6_2atmpS1720 == 5) {
        uint64_t _M0L6_2atmpS1719 = _M0Lm2vrS646;
        uint64_t _M0L6_2atmpS1718 = _M0L6_2atmpS1719 % 2ull;
        _if__result_2268 = _M0L6_2atmpS1718 == 0ull;
      } else {
        _if__result_2268 = 0;
      }
    } else {
      _if__result_2268 = 0;
    }
    if (_if__result_2268) {
      _M0Lm16lastRemovedDigitS671 = 4;
    }
    _M0L6_2atmpS1721 = _M0Lm2vrS646;
    _M0L6_2atmpS1727 = _M0Lm2vrS646;
    _M0L6_2atmpS1728 = _M0Lm2vmS648;
    if (_M0L6_2atmpS1727 == _M0L6_2atmpS1728) {
      if (!_M0L4evenS643) {
        _if__result_2269 = 1;
      } else {
        int32_t _M0L6_2atmpS1726 = _M0Lm17vmIsTrailingZerosS650;
        _if__result_2269 = !_M0L6_2atmpS1726;
      }
    } else {
      _if__result_2269 = 0;
    }
    if (_if__result_2269) {
      _M0L6_2atmpS1724 = 1;
    } else {
      int32_t _M0L6_2atmpS1725 = _M0Lm16lastRemovedDigitS671;
      _M0L6_2atmpS1724 = _M0L6_2atmpS1725 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1723 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1724);
    _M0L6_2atmpS1722 = *(uint64_t*)&_M0L6_2atmpS1723;
    _M0Lm6outputS672 = _M0L6_2atmpS1721 + _M0L6_2atmpS1722;
  } else {
    int32_t _M0Lm7roundUpS685 = 0;
    uint64_t _M0L6_2atmpS1749 = _M0Lm2vpS647;
    uint64_t _M0L8vpDiv100S686 = _M0L6_2atmpS1749 / 100ull;
    uint64_t _M0L6_2atmpS1748 = _M0Lm2vmS648;
    uint64_t _M0L8vmDiv100S687 = _M0L6_2atmpS1748 / 100ull;
    uint64_t _M0L6_2atmpS1743;
    uint64_t _M0L6_2atmpS1746;
    uint64_t _M0L6_2atmpS1747;
    int32_t _M0L6_2atmpS1745;
    uint64_t _M0L6_2atmpS1744;
    if (_M0L8vpDiv100S686 > _M0L8vmDiv100S687) {
      uint64_t _M0L6_2atmpS1734 = _M0Lm2vrS646;
      uint64_t _M0L8vrDiv100S688 = _M0L6_2atmpS1734 / 100ull;
      uint64_t _M0L6_2atmpS1733 = _M0Lm2vrS646;
      int32_t _M0L6_2atmpS1730 = (int32_t)_M0L6_2atmpS1733;
      int32_t _M0L6_2atmpS1732 = (int32_t)_M0L8vrDiv100S688;
      int32_t _M0L6_2atmpS1731 = 100 * _M0L6_2atmpS1732;
      int32_t _M0L8vrMod100S689 = _M0L6_2atmpS1730 - _M0L6_2atmpS1731;
      int32_t _M0L6_2atmpS1729;
      _M0Lm7roundUpS685 = _M0L8vrMod100S689 >= 50;
      _M0Lm2vrS646 = _M0L8vrDiv100S688;
      _M0Lm2vpS647 = _M0L8vpDiv100S686;
      _M0Lm2vmS648 = _M0L8vmDiv100S687;
      _M0L6_2atmpS1729 = _M0Lm7removedS670;
      _M0Lm7removedS670 = _M0L6_2atmpS1729 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1742 = _M0Lm2vpS647;
      uint64_t _M0L7vpDiv10S690 = _M0L6_2atmpS1742 / 10ull;
      uint64_t _M0L6_2atmpS1741 = _M0Lm2vmS648;
      uint64_t _M0L7vmDiv10S691 = _M0L6_2atmpS1741 / 10ull;
      uint64_t _M0L6_2atmpS1740;
      uint64_t _M0L7vrDiv10S693;
      uint64_t _M0L6_2atmpS1739;
      int32_t _M0L6_2atmpS1736;
      int32_t _M0L6_2atmpS1738;
      int32_t _M0L6_2atmpS1737;
      int32_t _M0L7vrMod10S694;
      int32_t _M0L6_2atmpS1735;
      if (_M0L7vpDiv10S690 <= _M0L7vmDiv10S691) {
        break;
      }
      _M0L6_2atmpS1740 = _M0Lm2vrS646;
      _M0L7vrDiv10S693 = _M0L6_2atmpS1740 / 10ull;
      _M0L6_2atmpS1739 = _M0Lm2vrS646;
      _M0L6_2atmpS1736 = (int32_t)_M0L6_2atmpS1739;
      _M0L6_2atmpS1738 = (int32_t)_M0L7vrDiv10S693;
      _M0L6_2atmpS1737 = 10 * _M0L6_2atmpS1738;
      _M0L7vrMod10S694 = _M0L6_2atmpS1736 - _M0L6_2atmpS1737;
      _M0Lm7roundUpS685 = _M0L7vrMod10S694 >= 5;
      _M0Lm2vrS646 = _M0L7vrDiv10S693;
      _M0Lm2vpS647 = _M0L7vpDiv10S690;
      _M0Lm2vmS648 = _M0L7vmDiv10S691;
      _M0L6_2atmpS1735 = _M0Lm7removedS670;
      _M0Lm7removedS670 = _M0L6_2atmpS1735 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1743 = _M0Lm2vrS646;
    _M0L6_2atmpS1746 = _M0Lm2vrS646;
    _M0L6_2atmpS1747 = _M0Lm2vmS648;
    _M0L6_2atmpS1745
    = _M0L6_2atmpS1746 == _M0L6_2atmpS1747 || _M0Lm7roundUpS685;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1744 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1745);
    _M0Lm6outputS672 = _M0L6_2atmpS1743 + _M0L6_2atmpS1744;
  }
  _M0L6_2atmpS1751 = _M0Lm3e10S649;
  _M0L6_2atmpS1752 = _M0Lm7removedS670;
  _M0L3expS695 = _M0L6_2atmpS1751 + _M0L6_2atmpS1752;
  _M0L6_2atmpS1750 = _M0Lm6outputS672;
  _block_2271
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2271)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2271->$0 = _M0L6_2atmpS1750;
  _block_2271->$1 = _M0L3expS695;
  return _block_2271;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS638) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS638) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS637) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS637) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS636) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS636) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS635) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS635 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS635 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS635 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS635 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS635 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS635 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS635 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS635 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS635 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS635 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS635 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS635 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS635 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS635 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS635 >= 100ull) {
    return 3;
  }
  if (_M0L1vS635 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS618) {
  int32_t _M0L6_2atmpS1651;
  int32_t _M0L6_2atmpS1650;
  int32_t _M0L4baseS617;
  int32_t _M0L5base2S619;
  int32_t _M0L6offsetS620;
  int32_t _M0L6_2atmpS1649;
  uint64_t _M0L4mul0S621;
  int32_t _M0L6_2atmpS1648;
  int32_t _M0L6_2atmpS1647;
  uint64_t _M0L4mul1S622;
  uint64_t _M0L1mS623;
  struct _M0TPB7Umul128 _M0L7_2abindS624;
  uint64_t _M0L7_2alow1S625;
  uint64_t _M0L8_2ahigh1S626;
  struct _M0TPB7Umul128 _M0L7_2abindS627;
  uint64_t _M0L7_2alow0S628;
  uint64_t _M0L8_2ahigh0S629;
  uint64_t _M0L3sumS630;
  uint64_t _M0Lm5high1S631;
  int32_t _M0L6_2atmpS1645;
  int32_t _M0L6_2atmpS1646;
  int32_t _M0L5deltaS632;
  uint64_t _M0L6_2atmpS1644;
  uint64_t _M0L6_2atmpS1636;
  int32_t _M0L6_2atmpS1643;
  uint32_t _M0L6_2atmpS1640;
  int32_t _M0L6_2atmpS1642;
  int32_t _M0L6_2atmpS1641;
  uint32_t _M0L6_2atmpS1639;
  uint32_t _M0L6_2atmpS1638;
  uint64_t _M0L6_2atmpS1637;
  uint64_t _M0L1aS633;
  uint64_t _M0L6_2atmpS1635;
  uint64_t _M0L1bS634;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1651 = _M0L1iS618 + 26;
  _M0L6_2atmpS1650 = _M0L6_2atmpS1651 - 1;
  _M0L4baseS617 = _M0L6_2atmpS1650 / 26;
  _M0L5base2S619 = _M0L4baseS617 * 26;
  _M0L6offsetS620 = _M0L5base2S619 - _M0L1iS618;
  _M0L6_2atmpS1649 = _M0L4baseS617 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S621
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1649);
  _M0L6_2atmpS1648 = _M0L4baseS617 * 2;
  _M0L6_2atmpS1647 = _M0L6_2atmpS1648 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S622
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1647);
  if (_M0L6offsetS620 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S621, .$1 = _M0L4mul1S622};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS623
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS620);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS624 = _M0FPB7umul128(_M0L1mS623, _M0L4mul1S622);
  _M0L7_2alow1S625 = _M0L7_2abindS624.$0;
  _M0L8_2ahigh1S626 = _M0L7_2abindS624.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS627 = _M0FPB7umul128(_M0L1mS623, _M0L4mul0S621);
  _M0L7_2alow0S628 = _M0L7_2abindS627.$0;
  _M0L8_2ahigh0S629 = _M0L7_2abindS627.$1;
  _M0L3sumS630 = _M0L8_2ahigh0S629 + _M0L7_2alow1S625;
  _M0Lm5high1S631 = _M0L8_2ahigh1S626;
  if (_M0L3sumS630 < _M0L8_2ahigh0S629) {
    uint64_t _M0L6_2atmpS1634 = _M0Lm5high1S631;
    _M0Lm5high1S631 = _M0L6_2atmpS1634 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1645 = _M0FPB8pow5bits(_M0L5base2S619);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1646 = _M0FPB8pow5bits(_M0L1iS618);
  _M0L5deltaS632 = _M0L6_2atmpS1645 - _M0L6_2atmpS1646;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1644
  = _M0FPB13shiftright128(_M0L7_2alow0S628, _M0L3sumS630, _M0L5deltaS632);
  _M0L6_2atmpS1636 = _M0L6_2atmpS1644 + 1ull;
  _M0L6_2atmpS1643 = _M0L1iS618 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1640
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1643);
  _M0L6_2atmpS1642 = _M0L1iS618 % 16;
  _M0L6_2atmpS1641 = _M0L6_2atmpS1642 << 1;
  _M0L6_2atmpS1639 = _M0L6_2atmpS1640 >> (_M0L6_2atmpS1641 & 31);
  _M0L6_2atmpS1638 = _M0L6_2atmpS1639 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1637 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1638);
  _M0L1aS633 = _M0L6_2atmpS1636 + _M0L6_2atmpS1637;
  _M0L6_2atmpS1635 = _M0Lm5high1S631;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS634
  = _M0FPB13shiftright128(_M0L3sumS630, _M0L6_2atmpS1635, _M0L5deltaS632);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS633, .$1 = _M0L1bS634};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS600) {
  int32_t _M0L4baseS599;
  int32_t _M0L5base2S601;
  int32_t _M0L6offsetS602;
  int32_t _M0L6_2atmpS1633;
  uint64_t _M0L4mul0S603;
  int32_t _M0L6_2atmpS1632;
  int32_t _M0L6_2atmpS1631;
  uint64_t _M0L4mul1S604;
  uint64_t _M0L1mS605;
  struct _M0TPB7Umul128 _M0L7_2abindS606;
  uint64_t _M0L7_2alow1S607;
  uint64_t _M0L8_2ahigh1S608;
  struct _M0TPB7Umul128 _M0L7_2abindS609;
  uint64_t _M0L7_2alow0S610;
  uint64_t _M0L8_2ahigh0S611;
  uint64_t _M0L3sumS612;
  uint64_t _M0Lm5high1S613;
  int32_t _M0L6_2atmpS1629;
  int32_t _M0L6_2atmpS1630;
  int32_t _M0L5deltaS614;
  uint64_t _M0L6_2atmpS1621;
  int32_t _M0L6_2atmpS1628;
  uint32_t _M0L6_2atmpS1625;
  int32_t _M0L6_2atmpS1627;
  int32_t _M0L6_2atmpS1626;
  uint32_t _M0L6_2atmpS1624;
  uint32_t _M0L6_2atmpS1623;
  uint64_t _M0L6_2atmpS1622;
  uint64_t _M0L1aS615;
  uint64_t _M0L6_2atmpS1620;
  uint64_t _M0L1bS616;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS599 = _M0L1iS600 / 26;
  _M0L5base2S601 = _M0L4baseS599 * 26;
  _M0L6offsetS602 = _M0L1iS600 - _M0L5base2S601;
  _M0L6_2atmpS1633 = _M0L4baseS599 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S603
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1633);
  _M0L6_2atmpS1632 = _M0L4baseS599 * 2;
  _M0L6_2atmpS1631 = _M0L6_2atmpS1632 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S604
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1631);
  if (_M0L6offsetS602 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S603, .$1 = _M0L4mul1S604};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS605
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS602);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS606 = _M0FPB7umul128(_M0L1mS605, _M0L4mul1S604);
  _M0L7_2alow1S607 = _M0L7_2abindS606.$0;
  _M0L8_2ahigh1S608 = _M0L7_2abindS606.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS609 = _M0FPB7umul128(_M0L1mS605, _M0L4mul0S603);
  _M0L7_2alow0S610 = _M0L7_2abindS609.$0;
  _M0L8_2ahigh0S611 = _M0L7_2abindS609.$1;
  _M0L3sumS612 = _M0L8_2ahigh0S611 + _M0L7_2alow1S607;
  _M0Lm5high1S613 = _M0L8_2ahigh1S608;
  if (_M0L3sumS612 < _M0L8_2ahigh0S611) {
    uint64_t _M0L6_2atmpS1619 = _M0Lm5high1S613;
    _M0Lm5high1S613 = _M0L6_2atmpS1619 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1629 = _M0FPB8pow5bits(_M0L1iS600);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1630 = _M0FPB8pow5bits(_M0L5base2S601);
  _M0L5deltaS614 = _M0L6_2atmpS1629 - _M0L6_2atmpS1630;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1621
  = _M0FPB13shiftright128(_M0L7_2alow0S610, _M0L3sumS612, _M0L5deltaS614);
  _M0L6_2atmpS1628 = _M0L1iS600 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1625
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1628);
  _M0L6_2atmpS1627 = _M0L1iS600 % 16;
  _M0L6_2atmpS1626 = _M0L6_2atmpS1627 << 1;
  _M0L6_2atmpS1624 = _M0L6_2atmpS1625 >> (_M0L6_2atmpS1626 & 31);
  _M0L6_2atmpS1623 = _M0L6_2atmpS1624 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1622 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1623);
  _M0L1aS615 = _M0L6_2atmpS1621 + _M0L6_2atmpS1622;
  _M0L6_2atmpS1620 = _M0Lm5high1S613;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS616
  = _M0FPB13shiftright128(_M0L3sumS612, _M0L6_2atmpS1620, _M0L5deltaS614);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS615, .$1 = _M0L1bS616};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS573,
  struct _M0TPB8Pow5Pair _M0L3mulS570,
  int32_t _M0L1jS586,
  int32_t _M0L7mmShiftS588
) {
  uint64_t _M0L7_2amul0S569;
  uint64_t _M0L7_2amul1S571;
  uint64_t _M0L1mS572;
  struct _M0TPB7Umul128 _M0L7_2abindS574;
  uint64_t _M0L5_2aloS575;
  uint64_t _M0L6_2atmpS576;
  struct _M0TPB7Umul128 _M0L7_2abindS577;
  uint64_t _M0L6_2alo2S578;
  uint64_t _M0L6_2ahi2S579;
  uint64_t _M0L3midS580;
  uint64_t _M0L6_2atmpS1618;
  uint64_t _M0L2hiS581;
  uint64_t _M0L3lo2S582;
  uint64_t _M0L6_2atmpS1616;
  uint64_t _M0L6_2atmpS1617;
  uint64_t _M0L4mid2S583;
  uint64_t _M0L6_2atmpS1615;
  uint64_t _M0L3hi2S584;
  int32_t _M0L6_2atmpS1614;
  int32_t _M0L6_2atmpS1613;
  uint64_t _M0L2vpS585;
  uint64_t _M0Lm2vmS587;
  int32_t _M0L6_2atmpS1612;
  int32_t _M0L6_2atmpS1611;
  uint64_t _M0L2vrS598;
  uint64_t _M0L6_2atmpS1610;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S569 = _M0L3mulS570.$0;
  _M0L7_2amul1S571 = _M0L3mulS570.$1;
  _M0L1mS572 = _M0L1mS573 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS574 = _M0FPB7umul128(_M0L1mS572, _M0L7_2amul0S569);
  _M0L5_2aloS575 = _M0L7_2abindS574.$0;
  _M0L6_2atmpS576 = _M0L7_2abindS574.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS577 = _M0FPB7umul128(_M0L1mS572, _M0L7_2amul1S571);
  _M0L6_2alo2S578 = _M0L7_2abindS577.$0;
  _M0L6_2ahi2S579 = _M0L7_2abindS577.$1;
  _M0L3midS580 = _M0L6_2atmpS576 + _M0L6_2alo2S578;
  if (_M0L3midS580 < _M0L6_2atmpS576) {
    _M0L6_2atmpS1618 = 1ull;
  } else {
    _M0L6_2atmpS1618 = 0ull;
  }
  _M0L2hiS581 = _M0L6_2ahi2S579 + _M0L6_2atmpS1618;
  _M0L3lo2S582 = _M0L5_2aloS575 + _M0L7_2amul0S569;
  _M0L6_2atmpS1616 = _M0L3midS580 + _M0L7_2amul1S571;
  if (_M0L3lo2S582 < _M0L5_2aloS575) {
    _M0L6_2atmpS1617 = 1ull;
  } else {
    _M0L6_2atmpS1617 = 0ull;
  }
  _M0L4mid2S583 = _M0L6_2atmpS1616 + _M0L6_2atmpS1617;
  if (_M0L4mid2S583 < _M0L3midS580) {
    _M0L6_2atmpS1615 = 1ull;
  } else {
    _M0L6_2atmpS1615 = 0ull;
  }
  _M0L3hi2S584 = _M0L2hiS581 + _M0L6_2atmpS1615;
  _M0L6_2atmpS1614 = _M0L1jS586 - 64;
  _M0L6_2atmpS1613 = _M0L6_2atmpS1614 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS585
  = _M0FPB13shiftright128(_M0L4mid2S583, _M0L3hi2S584, _M0L6_2atmpS1613);
  _M0Lm2vmS587 = 0ull;
  if (_M0L7mmShiftS588) {
    uint64_t _M0L3lo3S589 = _M0L5_2aloS575 - _M0L7_2amul0S569;
    uint64_t _M0L6_2atmpS1600 = _M0L3midS580 - _M0L7_2amul1S571;
    uint64_t _M0L6_2atmpS1601;
    uint64_t _M0L4mid3S590;
    uint64_t _M0L6_2atmpS1599;
    uint64_t _M0L3hi3S591;
    int32_t _M0L6_2atmpS1598;
    int32_t _M0L6_2atmpS1597;
    if (_M0L5_2aloS575 < _M0L3lo3S589) {
      _M0L6_2atmpS1601 = 1ull;
    } else {
      _M0L6_2atmpS1601 = 0ull;
    }
    _M0L4mid3S590 = _M0L6_2atmpS1600 - _M0L6_2atmpS1601;
    if (_M0L3midS580 < _M0L4mid3S590) {
      _M0L6_2atmpS1599 = 1ull;
    } else {
      _M0L6_2atmpS1599 = 0ull;
    }
    _M0L3hi3S591 = _M0L2hiS581 - _M0L6_2atmpS1599;
    _M0L6_2atmpS1598 = _M0L1jS586 - 64;
    _M0L6_2atmpS1597 = _M0L6_2atmpS1598 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS587
    = _M0FPB13shiftright128(_M0L4mid3S590, _M0L3hi3S591, _M0L6_2atmpS1597);
  } else {
    uint64_t _M0L3lo3S592 = _M0L5_2aloS575 + _M0L5_2aloS575;
    uint64_t _M0L6_2atmpS1608 = _M0L3midS580 + _M0L3midS580;
    uint64_t _M0L6_2atmpS1609;
    uint64_t _M0L4mid3S593;
    uint64_t _M0L6_2atmpS1606;
    uint64_t _M0L6_2atmpS1607;
    uint64_t _M0L3hi3S594;
    uint64_t _M0L3lo4S595;
    uint64_t _M0L6_2atmpS1604;
    uint64_t _M0L6_2atmpS1605;
    uint64_t _M0L4mid4S596;
    uint64_t _M0L6_2atmpS1603;
    uint64_t _M0L3hi4S597;
    int32_t _M0L6_2atmpS1602;
    if (_M0L3lo3S592 < _M0L5_2aloS575) {
      _M0L6_2atmpS1609 = 1ull;
    } else {
      _M0L6_2atmpS1609 = 0ull;
    }
    _M0L4mid3S593 = _M0L6_2atmpS1608 + _M0L6_2atmpS1609;
    _M0L6_2atmpS1606 = _M0L2hiS581 + _M0L2hiS581;
    if (_M0L4mid3S593 < _M0L3midS580) {
      _M0L6_2atmpS1607 = 1ull;
    } else {
      _M0L6_2atmpS1607 = 0ull;
    }
    _M0L3hi3S594 = _M0L6_2atmpS1606 + _M0L6_2atmpS1607;
    _M0L3lo4S595 = _M0L3lo3S592 - _M0L7_2amul0S569;
    _M0L6_2atmpS1604 = _M0L4mid3S593 - _M0L7_2amul1S571;
    if (_M0L3lo3S592 < _M0L3lo4S595) {
      _M0L6_2atmpS1605 = 1ull;
    } else {
      _M0L6_2atmpS1605 = 0ull;
    }
    _M0L4mid4S596 = _M0L6_2atmpS1604 - _M0L6_2atmpS1605;
    if (_M0L4mid3S593 < _M0L4mid4S596) {
      _M0L6_2atmpS1603 = 1ull;
    } else {
      _M0L6_2atmpS1603 = 0ull;
    }
    _M0L3hi4S597 = _M0L3hi3S594 - _M0L6_2atmpS1603;
    _M0L6_2atmpS1602 = _M0L1jS586 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS587
    = _M0FPB13shiftright128(_M0L4mid4S596, _M0L3hi4S597, _M0L6_2atmpS1602);
  }
  _M0L6_2atmpS1612 = _M0L1jS586 - 64;
  _M0L6_2atmpS1611 = _M0L6_2atmpS1612 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS598
  = _M0FPB13shiftright128(_M0L3midS580, _M0L2hiS581, _M0L6_2atmpS1611);
  _M0L6_2atmpS1610 = _M0Lm2vmS587;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS598,
                                                .$1 = _M0L2vpS585,
                                                .$2 = _M0L6_2atmpS1610};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS567,
  int32_t _M0L1pS568
) {
  uint64_t _M0L6_2atmpS1596;
  uint64_t _M0L6_2atmpS1595;
  uint64_t _M0L6_2atmpS1594;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1596 = 1ull << (_M0L1pS568 & 63);
  _M0L6_2atmpS1595 = _M0L6_2atmpS1596 - 1ull;
  _M0L6_2atmpS1594 = _M0L5valueS567 & _M0L6_2atmpS1595;
  return _M0L6_2atmpS1594 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS565,
  int32_t _M0L1pS566
) {
  int32_t _M0L6_2atmpS1593;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1593 = _M0FPB10pow5Factor(_M0L5valueS565);
  return _M0L6_2atmpS1593 >= _M0L1pS566;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS560) {
  uint64_t _M0L6_2atmpS1584;
  uint64_t _M0L6_2atmpS1585;
  uint64_t _M0L6_2atmpS1586;
  uint64_t _M0L6_2atmpS1587;
  uint64_t _M0L6_2atmpS1592;
  int32_t _M0L5countS561;
  uint64_t _M0L1vS562;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1584 = _M0L5valueS560 % 5ull;
  if (_M0L6_2atmpS1584 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1585 = _M0L5valueS560 % 25ull;
  if (_M0L6_2atmpS1585 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1586 = _M0L5valueS560 % 125ull;
  if (_M0L6_2atmpS1586 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1587 = _M0L5valueS560 % 625ull;
  if (_M0L6_2atmpS1587 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1592 = _M0L5valueS560 / 625ull;
  _M0L5countS561 = 4;
  _M0L1vS562 = _M0L6_2atmpS1592;
  while (1) {
    if (_M0L1vS562 > 0ull) {
      uint64_t _M0L6_2atmpS1588 = _M0L1vS562 % 5ull;
      int32_t _M0L6_2atmpS1589;
      uint64_t _M0L6_2atmpS1590;
      if (_M0L6_2atmpS1588 != 0ull) {
        return _M0L5countS561;
      }
      _M0L6_2atmpS1589 = _M0L5countS561 + 1;
      _M0L6_2atmpS1590 = _M0L1vS562 / 5ull;
      _M0L5countS561 = _M0L6_2atmpS1589;
      _M0L1vS562 = _M0L6_2atmpS1590;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS564;
      moonbit_string_t _M0L6_2atmpS1591;
      int32_t _result_2273;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS564
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS564, (moonbit_string_t)moonbit_string_literal_12.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS564, _M0L5valueS560);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1591
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS564);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS564);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2273 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1591);
      moonbit_decref_cycle_free(_M0L6_2atmpS1591);
      return _result_2273;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS559,
  uint64_t _M0L2hiS557,
  int32_t _M0L4distS558
) {
  int32_t _M0L6_2atmpS1583;
  uint64_t _M0L6_2atmpS1581;
  uint64_t _M0L6_2atmpS1582;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1583 = 64 - _M0L4distS558;
  _M0L6_2atmpS1581 = _M0L2hiS557 << (_M0L6_2atmpS1583 & 63);
  _M0L6_2atmpS1582 = _M0L2loS559 >> (_M0L4distS558 & 63);
  return _M0L6_2atmpS1581 | _M0L6_2atmpS1582;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS547,
  uint64_t _M0L1bS550
) {
  uint64_t _M0L3aLoS546;
  uint64_t _M0L3aHiS548;
  uint64_t _M0L3bLoS549;
  uint64_t _M0L3bHiS551;
  uint64_t _M0L1xS552;
  uint64_t _M0L6_2atmpS1579;
  uint64_t _M0L6_2atmpS1580;
  uint64_t _M0L1yS553;
  uint64_t _M0L6_2atmpS1577;
  uint64_t _M0L6_2atmpS1578;
  uint64_t _M0L1zS554;
  uint64_t _M0L6_2atmpS1575;
  uint64_t _M0L6_2atmpS1576;
  uint64_t _M0L6_2atmpS1573;
  uint64_t _M0L6_2atmpS1574;
  uint64_t _M0L1wS555;
  uint64_t _M0L2loS556;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS546 = _M0L1aS547 & 4294967295ull;
  _M0L3aHiS548 = _M0L1aS547 >> 32;
  _M0L3bLoS549 = _M0L1bS550 & 4294967295ull;
  _M0L3bHiS551 = _M0L1bS550 >> 32;
  _M0L1xS552 = _M0L3aLoS546 * _M0L3bLoS549;
  _M0L6_2atmpS1579 = _M0L3aHiS548 * _M0L3bLoS549;
  _M0L6_2atmpS1580 = _M0L1xS552 >> 32;
  _M0L1yS553 = _M0L6_2atmpS1579 + _M0L6_2atmpS1580;
  _M0L6_2atmpS1577 = _M0L3aLoS546 * _M0L3bHiS551;
  _M0L6_2atmpS1578 = _M0L1yS553 & 4294967295ull;
  _M0L1zS554 = _M0L6_2atmpS1577 + _M0L6_2atmpS1578;
  _M0L6_2atmpS1575 = _M0L3aHiS548 * _M0L3bHiS551;
  _M0L6_2atmpS1576 = _M0L1yS553 >> 32;
  _M0L6_2atmpS1573 = _M0L6_2atmpS1575 + _M0L6_2atmpS1576;
  _M0L6_2atmpS1574 = _M0L1zS554 >> 32;
  _M0L1wS555 = _M0L6_2atmpS1573 + _M0L6_2atmpS1574;
  _M0L2loS556 = _M0L1aS547 * _M0L1bS550;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS556, .$1 = _M0L1wS555};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS544,
  int32_t _M0L4fromS541,
  int32_t _M0L2toS540
) {
  int32_t _M0L3lenS539;
  int32_t _M0L6_2atmpS1572;
  uint16_t* _M0L6bufferS542;
  int32_t _M0L1iS543;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS539 = _M0L2toS540 - _M0L4fromS541;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1572 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS542
  = (uint16_t*)moonbit_make_string(_M0L3lenS539, _M0L6_2atmpS1572);
  _M0L1iS543 = 0;
  while (1) {
    if (_M0L1iS543 < _M0L3lenS539) {
      int32_t _M0L6_2atmpS1570 = _M0L4fromS541 + _M0L1iS543;
      int32_t _M0L6_2atmpS1569;
      int32_t _M0L6_2atmpS1568;
      int32_t _M0L6_2atmpS1571;
      if (
        _M0L6_2atmpS1570 < 0
        || _M0L6_2atmpS1570 >= Moonbit_array_length(_M0L5bytesS544)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1569 = (int32_t)_M0L5bytesS544[_M0L6_2atmpS1570];
      _M0L6_2atmpS1568 = (uint16_t)_M0L6_2atmpS1569;
      if (
        _M0L1iS543 < 0 || _M0L1iS543 >= Moonbit_array_length(_M0L6bufferS542)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS542[_M0L1iS543] = _M0L6_2atmpS1568;
      _M0L6_2atmpS1571 = _M0L1iS543 + 1;
      _M0L1iS543 = _M0L6_2atmpS1571;
      continue;
    }
    break;
  }
  return _M0L6bufferS542;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS538) {
  int32_t _M0L6_2atmpS1567;
  uint32_t _M0L6_2atmpS1566;
  uint32_t _M0L6_2atmpS1565;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1567 = _M0L1eS538 * 78913;
  _M0L6_2atmpS1566 = *(uint32_t*)&_M0L6_2atmpS1567;
  _M0L6_2atmpS1565 = _M0L6_2atmpS1566 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1565;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS537) {
  int32_t _M0L6_2atmpS1564;
  uint32_t _M0L6_2atmpS1563;
  uint32_t _M0L6_2atmpS1562;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1564 = _M0L1eS537 * 732923;
  _M0L6_2atmpS1563 = *(uint32_t*)&_M0L6_2atmpS1564;
  _M0L6_2atmpS1562 = _M0L6_2atmpS1563 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1562;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS535,
  int32_t _M0L8exponentS536,
  int32_t _M0L8mantissaS533
) {
  moonbit_string_t _M0L1sS534;
  moonbit_string_t _result_2276;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS533) {
    return (moonbit_string_t)moonbit_string_literal_13.data;
  }
  if (_M0L4signS535) {
    _M0L1sS534 = (moonbit_string_t)moonbit_string_literal_14.data;
  } else {
    _M0L1sS534 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS536) {
    moonbit_string_t _result_2275;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2275
    = moonbit_add_string(_M0L1sS534, (moonbit_string_t)moonbit_string_literal_15.data);
    moonbit_decref_cycle_free(_M0L1sS534);
    return _result_2275;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2276
  = moonbit_add_string(_M0L1sS534, (moonbit_string_t)moonbit_string_literal_16.data);
  moonbit_decref_cycle_free(_M0L1sS534);
  return _result_2276;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS532) {
  int32_t _M0L6_2atmpS1561;
  uint32_t _M0L6_2atmpS1560;
  uint32_t _M0L6_2atmpS1559;
  int32_t _M0L6_2atmpS1558;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1561 = _M0L1eS532 * 1217359;
  _M0L6_2atmpS1560 = *(uint32_t*)&_M0L6_2atmpS1561;
  _M0L6_2atmpS1559 = _M0L6_2atmpS1560 >> 19;
  _M0L6_2atmpS1558 = *(int32_t*)&_M0L6_2atmpS1559;
  return _M0L6_2atmpS1558 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS531) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS531 != _M0L4selfS531) {
    return 0;
  } else if (_M0L4selfS531 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS531 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS531;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS530) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS530 != _M0L4selfS530) {
    return 0ll;
  } else if (_M0L4selfS530 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS530 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS530;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS527
) {
  float* _M0L6_2atmpS1555;
  struct _M0TPB5ArrayGfE* _block_2277;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1555 = (float*)moonbit_make_float_array_raw(_M0L3lenS527);
  _block_2277
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2277)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2277->$0 = _M0L6_2atmpS1555;
  _block_2277->$1 = _M0L3lenS527;
  return _block_2277;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS528
) {
  uint8_t* _M0L6_2atmpS1556;
  struct _M0TPB5ArrayGbE* _block_2278;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1556 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS528);
  _block_2278
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_2278)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 59, 0);
  _block_2278->$0 = _M0L6_2atmpS1556;
  _block_2278->$1 = _M0L3lenS528;
  return _block_2278;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS529
) {
  int32_t* _M0L6_2atmpS1557;
  struct _M0TPB5ArrayGiE* _block_2279;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1557 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS529);
  _block_2279
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_2279)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
  _block_2279->$0 = _M0L6_2atmpS1557;
  _block_2279->$1 = _M0L3lenS529;
  return _block_2279;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS523,
  int32_t _M0L5indexS524
) {
  uint64_t* _M0L6_2atmpS1553;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1553 = _M0L4selfS523;
  if (
    _M0L5indexS524 < 0
    || _M0L5indexS524 >= Moonbit_array_length(_M0L6_2atmpS1553)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1553[_M0L5indexS524];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS525,
  int32_t _M0L5indexS526
) {
  uint32_t* _M0L6_2atmpS1554;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1554 = _M0L4selfS525;
  if (
    _M0L5indexS526 < 0
    || _M0L5indexS526 >= Moonbit_array_length(_M0L6_2atmpS1554)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1554[_M0L5indexS526];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS522
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS522, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS521) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS521, 10);
}

moonbit_string_t _M0IPC14bool4BoolPB4Show10to__string(int32_t _M0L4selfS520) {
  #line 26 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L4selfS520) {
    return (moonbit_string_t)moonbit_string_literal_17.data;
  } else {
    return (moonbit_string_t)moonbit_string_literal_18.data;
  }
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS519) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS519;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS510,
  float _M0L5valueS512
) {
  int32_t _M0L3lenS1532;
  float* _M0L6_2atmpS1534;
  int32_t _M0L6_2atmpS1533;
  int32_t _M0L6lengthS511;
  float* _M0L3bufS1537;
  int32_t _M0L6_2atmpS1538;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1532 = _M0L4selfS510->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1534 = _M0MPC15array5Array6bufferGfE(_M0L4selfS510);
  _M0L6_2atmpS1533 = Moonbit_array_length(_M0L6_2atmpS1534);
  moonbit_decref_cycle_free(_M0L6_2atmpS1534);
  if (_M0L3lenS1532 == _M0L6_2atmpS1533) {
    int32_t _M0L3lenS1536 = _M0L4selfS510->$1;
    int32_t _M0L6_2atmpS1535 = _M0L3lenS1536 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS510, _M0L6_2atmpS1535);
  }
  _M0L6lengthS511 = _M0L4selfS510->$1;
  _M0L3bufS1537 = _M0L4selfS510->$0;
  _M0L3bufS1537[_M0L6lengthS511] = _M0L5valueS512;
  _M0L6_2atmpS1538 = _M0L6lengthS511 + 1;
  _M0L4selfS510->$1 = _M0L6_2atmpS1538;
  return 0;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS513,
  moonbit_string_t _M0L5valueS515
) {
  int32_t _M0L3lenS1539;
  moonbit_string_t* _M0L6_2atmpS1541;
  int32_t _M0L6_2atmpS1540;
  int32_t _M0L6lengthS514;
  moonbit_string_t* _M0L3bufS1544;
  moonbit_string_t _M0L6_2aoldS2180;
  int32_t _M0L6_2atmpS1545;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1539 = _M0L4selfS513->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1541 = _M0MPC15array5Array6bufferGsE(_M0L4selfS513);
  _M0L6_2atmpS1540 = Moonbit_array_length(_M0L6_2atmpS1541);
  moonbit_decref_cycle_free(_M0L6_2atmpS1541);
  if (_M0L3lenS1539 == _M0L6_2atmpS1540) {
    int32_t _M0L3lenS1543 = _M0L4selfS513->$1;
    int32_t _M0L6_2atmpS1542 = _M0L3lenS1543 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS513, _M0L6_2atmpS1542);
  }
  _M0L6lengthS514 = _M0L4selfS513->$1;
  _M0L3bufS1544 = _M0L4selfS513->$0;
  _M0L6_2aoldS2180 = (moonbit_string_t)_M0L3bufS1544[_M0L6lengthS514];
  moonbit_decref_cycle_free(_M0L6_2aoldS2180);
  _M0L3bufS1544[_M0L6lengthS514] = _M0L5valueS515;
  _M0L6_2atmpS1545 = _M0L6lengthS514 + 1;
  _M0L4selfS513->$1 = _M0L6_2atmpS1545;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS516,
  struct _M0TUsiE* _M0L5valueS518
) {
  int32_t _M0L3lenS1546;
  struct _M0TUsiE** _M0L6_2atmpS1548;
  int32_t _M0L6_2atmpS1547;
  int32_t _M0L6lengthS517;
  struct _M0TUsiE** _M0L3bufS1551;
  struct _M0TUsiE* _M0L6_2aoldS2181;
  int32_t _M0L6_2atmpS1552;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1546 = _M0L4selfS516->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1548 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS516);
  _M0L6_2atmpS1547 = Moonbit_array_length(_M0L6_2atmpS1548);
  moonbit_decref_cycle_free(_M0L6_2atmpS1548);
  if (_M0L3lenS1546 == _M0L6_2atmpS1547) {
    int32_t _M0L3lenS1550 = _M0L4selfS516->$1;
    int32_t _M0L6_2atmpS1549 = _M0L3lenS1550 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS516, _M0L6_2atmpS1549);
  }
  _M0L6lengthS517 = _M0L4selfS516->$1;
  _M0L3bufS1551 = _M0L4selfS516->$0;
  _M0L6_2aoldS2181 = (struct _M0TUsiE*)_M0L3bufS1551[_M0L6lengthS517];
  if (_M0L6_2aoldS2181) {
    moonbit_decref_cycle_free(_M0L6_2aoldS2181);
  }
  _M0L3bufS1551[_M0L6lengthS517] = _M0L5valueS518;
  _M0L6_2atmpS1552 = _M0L6lengthS517 + 1;
  _M0L4selfS516->$1 = _M0L6_2atmpS1552;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS495,
  int32_t _M0L8requiredS497
) {
  int32_t _M0L8old__capS494;
  int32_t _M0L3lenS1528;
  int32_t _M0L8new__capS496;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS494 = _M0MPC15array5Array8capacityGfE(_M0L4selfS495);
  _M0L3lenS1528 = _M0L4selfS495->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS496
  = _M0FPB23array__growth__capacity(_M0L8old__capS494, _M0L3lenS1528, _M0L8requiredS497);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS495, _M0L8new__capS496);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS499,
  int32_t _M0L8requiredS501
) {
  int32_t _M0L8old__capS498;
  int32_t _M0L3lenS1529;
  int32_t _M0L8new__capS500;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS498 = _M0MPC15array5Array8capacityGsE(_M0L4selfS499);
  _M0L3lenS1529 = _M0L4selfS499->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS500
  = _M0FPB23array__growth__capacity(_M0L8old__capS498, _M0L3lenS1529, _M0L8requiredS501);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS499, _M0L8new__capS500);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS503,
  int32_t _M0L8requiredS505
) {
  int32_t _M0L8old__capS502;
  int32_t _M0L3lenS1530;
  int32_t _M0L8new__capS504;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS502 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS503);
  _M0L3lenS1530 = _M0L4selfS503->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS504
  = _M0FPB23array__growth__capacity(_M0L8old__capS502, _M0L3lenS1530, _M0L8requiredS505);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS503, _M0L8new__capS504);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS507,
  int32_t _M0L8requiredS509
) {
  int32_t _M0L8old__capS506;
  int32_t _M0L3lenS1531;
  int32_t _M0L8new__capS508;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS506 = _M0MPC15array5Array8capacityGiE(_M0L4selfS507);
  _M0L3lenS1531 = _M0L4selfS507->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS508
  = _M0FPB23array__growth__capacity(_M0L8old__capS506, _M0L3lenS1531, _M0L8requiredS509);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS507, _M0L8new__capS508);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS471,
  int32_t _M0L13new__capacityS474
) {
  float* _M0L8old__bufS470;
  int32_t _M0L3lenS472;
  int32_t _M0L9copy__lenS473;
  float* _M0L8new__bufS475;
  float* _M0L6_2aoldS2182;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS470 = _M0L4selfS471->$0;
  _M0L3lenS472 = _M0L4selfS471->$1;
  if (_M0L3lenS472 < _M0L13new__capacityS474) {
    _M0L9copy__lenS473 = _M0L3lenS472;
  } else {
    _M0L9copy__lenS473 = _M0L13new__capacityS474;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS470);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS475
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS470, _M0L13new__capacityS474, _M0L9copy__lenS473, 0, 0);
  _M0L6_2aoldS2182 = _M0L4selfS471->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2182);
  _M0L4selfS471->$0 = _M0L8new__bufS475;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS477,
  int32_t _M0L13new__capacityS480
) {
  moonbit_string_t* _M0L8old__bufS476;
  int32_t _M0L3lenS478;
  int32_t _M0L9copy__lenS479;
  moonbit_string_t* _M0L8new__bufS481;
  moonbit_string_t* _M0L6_2aoldS2183;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS476 = _M0L4selfS477->$0;
  _M0L3lenS478 = _M0L4selfS477->$1;
  if (_M0L3lenS478 < _M0L13new__capacityS480) {
    _M0L9copy__lenS479 = _M0L3lenS478;
  } else {
    _M0L9copy__lenS479 = _M0L13new__capacityS480;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS476);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS481
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS476, _M0L13new__capacityS480, _M0L9copy__lenS479, 0, 0);
  _M0L6_2aoldS2183 = _M0L4selfS477->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2183);
  _M0L4selfS477->$0 = _M0L8new__bufS481;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS483,
  int32_t _M0L13new__capacityS486
) {
  struct _M0TUsiE** _M0L8old__bufS482;
  int32_t _M0L3lenS484;
  int32_t _M0L9copy__lenS485;
  struct _M0TUsiE** _M0L8new__bufS487;
  struct _M0TUsiE** _M0L6_2aoldS2184;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS482 = _M0L4selfS483->$0;
  _M0L3lenS484 = _M0L4selfS483->$1;
  if (_M0L3lenS484 < _M0L13new__capacityS486) {
    _M0L9copy__lenS485 = _M0L3lenS484;
  } else {
    _M0L9copy__lenS485 = _M0L13new__capacityS486;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS482);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS487
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS482, _M0L13new__capacityS486, _M0L9copy__lenS485, 0, 0);
  _M0L6_2aoldS2184 = _M0L4selfS483->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2184);
  _M0L4selfS483->$0 = _M0L8new__bufS487;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS489,
  int32_t _M0L13new__capacityS492
) {
  int32_t* _M0L8old__bufS488;
  int32_t _M0L3lenS490;
  int32_t _M0L9copy__lenS491;
  int32_t* _M0L8new__bufS493;
  int32_t* _M0L6_2aoldS2185;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS488 = _M0L4selfS489->$0;
  _M0L3lenS490 = _M0L4selfS489->$1;
  if (_M0L3lenS490 < _M0L13new__capacityS492) {
    _M0L9copy__lenS491 = _M0L3lenS490;
  } else {
    _M0L9copy__lenS491 = _M0L13new__capacityS492;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS488);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS493
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS488, _M0L13new__capacityS492, _M0L9copy__lenS491, 0, 0);
  _M0L6_2aoldS2185 = _M0L4selfS489->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2185);
  _M0L4selfS489->$0 = _M0L8new__bufS493;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS466
) {
  float* _M0L6_2atmpS1524;
  int32_t _result_2280;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1524 = _M0MPC15array5Array6bufferGfE(_M0L4selfS466);
  _result_2280 = Moonbit_array_length(_M0L6_2atmpS1524);
  moonbit_decref_cycle_free(_M0L6_2atmpS1524);
  return _result_2280;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS467
) {
  moonbit_string_t* _M0L6_2atmpS1525;
  int32_t _result_2281;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1525 = _M0MPC15array5Array6bufferGsE(_M0L4selfS467);
  _result_2281 = Moonbit_array_length(_M0L6_2atmpS1525);
  moonbit_decref_cycle_free(_M0L6_2atmpS1525);
  return _result_2281;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS468
) {
  struct _M0TUsiE** _M0L6_2atmpS1526;
  int32_t _result_2282;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1526 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS468);
  _result_2282 = Moonbit_array_length(_M0L6_2atmpS1526);
  moonbit_decref_cycle_free(_M0L6_2atmpS1526);
  return _result_2282;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS469
) {
  int32_t* _M0L6_2atmpS1527;
  int32_t _result_2283;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1527 = _M0MPC15array5Array6bufferGiE(_M0L4selfS469);
  _result_2283 = Moonbit_array_length(_M0L6_2atmpS1527);
  moonbit_decref_cycle_free(_M0L6_2atmpS1527);
  return _result_2283;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS462,
  int32_t _M0L3lenS460,
  int32_t _M0L8requiredS459
) {
  int32_t _M0L5startS461;
  int32_t _M0L5spaceS463;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS459 < _M0L3lenS460) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_19.data);
  }
  if (_M0L7currentS462 == 0) {
    _M0L5startS461 = 8;
  } else {
    _M0L5startS461 = _M0L7currentS462;
  }
  _M0L5spaceS463 = _M0L5startS461;
  while (1) {
    if (_M0L5spaceS463 < _M0L8requiredS459) {
      int32_t _M0L4nextS464 = _M0L5spaceS463 * 2;
      if (_M0L4nextS464 <= _M0L5spaceS463) {
        return _M0L8requiredS459;
      }
      _M0L5spaceS463 = _M0L4nextS464;
      continue;
    } else {
      return _M0L5spaceS463;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS457) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS457->$1;
}

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE* _M0L4selfS458) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS458->$1;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS452) {
  uint8_t* _M0L8_2afieldS2186;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2186 = _M0L4selfS452->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2186);
  return _M0L8_2afieldS2186;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS453) {
  float* _M0L8_2afieldS2187;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2187 = _M0L4selfS453->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2187);
  return _M0L8_2afieldS2187;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS454) {
  int32_t* _M0L8_2afieldS2188;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2188 = _M0L4selfS454->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2188);
  return _M0L8_2afieldS2188;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS455
) {
  moonbit_string_t* _M0L8_2afieldS2189;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2189 = _M0L4selfS455->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2189);
  return _M0L8_2afieldS2189;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS456
) {
  struct _M0TUsiE** _M0L8_2afieldS2190;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS2190 = _M0L4selfS456->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2190);
  return _M0L8_2afieldS2190;
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
  int32_t _M0L3endS1522;
  int32_t _M0L5startS1523;
  int32_t _M0L8str__lenS447;
  int32_t _M0L3lenS1521;
  int32_t _M0L8requiredS449;
  uint16_t* _M0L4dataS1514;
  int32_t _M0L6_2atmpS1513;
  int32_t _if__result_2285;
  uint16_t* _M0L4dataS1515;
  int32_t _M0L3lenS1516;
  moonbit_string_t _M0L6_2atmpS1517;
  int32_t _M0L6_2atmpS1518;
  int32_t _M0L3lenS1520;
  int32_t _M0L6_2atmpS1519;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1522 = _M0L3strS448.$2;
  _M0L5startS1523 = _M0L3strS448.$1;
  _M0L8str__lenS447 = _M0L3endS1522 - _M0L5startS1523;
  if (_M0L8str__lenS447 == 0) {
    return 0;
  }
  _M0L3lenS1521 = _M0L4selfS450->$1;
  _M0L8requiredS449 = _M0L3lenS1521 + _M0L8str__lenS447;
  _M0L4dataS1514 = _M0L4selfS450->$0;
  _M0L6_2atmpS1513 = Moonbit_array_length(_M0L4dataS1514);
  if (_M0L8requiredS449 > _M0L6_2atmpS1513) {
    _if__result_2285 = 1;
  } else {
    int32_t _M0L3lenS1512 = _M0L4selfS450->$1;
    _if__result_2285 = _M0L8requiredS449 < _M0L3lenS1512;
  }
  if (_if__result_2285) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS450, _M0L8requiredS449);
  }
  _M0L4dataS1515 = _M0L4selfS450->$0;
  _M0L3lenS1516 = _M0L4selfS450->$1;
  moonbit_incref_cycle_free(_M0L4dataS1515);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1517 = _M0MPC16string10StringView4data(_M0L3strS448);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1518 = _M0MPC16string10StringView13start__offset(_M0L3strS448);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1515, _M0L3lenS1516, _M0L6_2atmpS1517, _M0L6_2atmpS1518, _M0L8str__lenS447);
  moonbit_decref_cycle_free(_M0L4dataS1515);
  moonbit_decref_cycle_free(_M0L6_2atmpS1517);
  _M0L3lenS1520 = _M0L4selfS450->$1;
  _M0L6_2atmpS1519 = _M0L3lenS1520 + _M0L8str__lenS447;
  _M0L4selfS450->$1 = _M0L6_2atmpS1519;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS444,
  int32_t _M0L5startS442,
  int32_t _M0L3endS443
) {
  int32_t _if__result_2286;
  int32_t _M0L3lenS445;
  int32_t _M0L6_2atmpS1511;
  moonbit_bytes_t _M0L5bytesS446;
  moonbit_bytes_t _M0L6_2atmpS1510;
  moonbit_string_t _result_2287;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS442 == 0) {
    int32_t _M0L6_2atmpS1509 = Moonbit_array_length(_M0L3strS444);
    _if__result_2286 = _M0L3endS443 == _M0L6_2atmpS1509;
  } else {
    _if__result_2286 = 0;
  }
  if (_if__result_2286) {
    moonbit_incref_cycle_free(_M0L3strS444);
    return _M0L3strS444;
  }
  _M0L3lenS445 = _M0L3endS443 - _M0L5startS442;
  _M0L6_2atmpS1511 = _M0L3lenS445 * 2;
  _M0L5bytesS446 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1511, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS446, 0, _M0L3strS444, _M0L5startS442, _M0L3lenS445);
  _M0L6_2atmpS1510 = _M0L5bytesS446;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2287
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1510, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1510);
  return _result_2287;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS437,
  int32_t _M0L6offsetS441,
  int64_t _M0L6lengthS439
) {
  int32_t _M0L3lenS436;
  int32_t _M0L6lengthS438;
  int32_t _if__result_2288;
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
      int32_t _M0L6_2atmpS1508 = _M0L6offsetS441 + _M0L6lengthS438;
      _if__result_2288 = _M0L6_2atmpS1508 <= _M0L3lenS436;
    } else {
      _if__result_2288 = 0;
    }
  } else {
    _if__result_2288 = 0;
  }
  if (_if__result_2288) {
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
  int32_t _M0L6_2atmpS1507;
  int32_t _M0L6_2atmpS1506;
  int32_t _M0L2e1S422;
  int32_t _M0L6_2atmpS1505;
  int32_t _M0L2e2S425;
  int32_t _M0L4len1S427;
  int32_t _M0L4len2S429;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1507 = _M0L6lengthS424 * 2;
  _M0L6_2atmpS1506 = _M0L13bytes__offsetS423 + _M0L6_2atmpS1507;
  _M0L2e1S422 = _M0L6_2atmpS1506 - 1;
  _M0L6_2atmpS1505 = _M0L11str__offsetS426 + _M0L6lengthS424;
  _M0L2e2S425 = _M0L6_2atmpS1505 - 1;
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
        int32_t _M0L6_2atmpS1502 = _M0L3strS430[_M0L1iS432];
        int32_t _M0L6_2atmpS1501 = (int32_t)_M0L6_2atmpS1502;
        uint32_t _M0L1cS434 = *(uint32_t*)&_M0L6_2atmpS1501;
        uint32_t _M0L6_2atmpS1497 = _M0L1cS434 & 255u;
        int32_t _M0L6_2atmpS1496;
        int32_t _M0L6_2atmpS1498;
        uint32_t _M0L6_2atmpS1500;
        int32_t _M0L6_2atmpS1499;
        int32_t _M0L6_2atmpS1503;
        int32_t _M0L6_2atmpS1504;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1496 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1497);
        if (
          _M0L1jS433 < 0 || _M0L1jS433 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L1jS433] = _M0L6_2atmpS1496;
        _M0L6_2atmpS1498 = _M0L1jS433 + 1;
        _M0L6_2atmpS1500 = _M0L1cS434 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1499 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1500);
        if (
          _M0L6_2atmpS1498 < 0
          || _M0L6_2atmpS1498 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L6_2atmpS1498] = _M0L6_2atmpS1499;
        _M0L6_2atmpS1503 = _M0L1iS432 + 1;
        _M0L6_2atmpS1504 = _M0L1jS433 + 2;
        _M0L1iS432 = _M0L6_2atmpS1503;
        _M0L1jS433 = _M0L6_2atmpS1504;
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
  int32_t _M0L6_2atmpS1495;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1495 = *(int32_t*)&_M0L4selfS421;
  return _M0L6_2atmpS1495 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS413,
  int32_t _M0L5radixS412
) {
  uint16_t* _M0L6bufferS414;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS412 < 2 || _M0L5radixS412 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_20.data);
  }
  if (_M0L4selfS413 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_20.data);
  }
  if (_M0L4selfS396 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  _M0L12is__negativeS397 = _M0L4selfS396 < 0ll;
  if (_M0L12is__negativeS397) {
    int64_t _M0L6_2atmpS1494 = -_M0L4selfS396;
    _M0L3numS398 = *(uint64_t*)&_M0L6_2atmpS1494;
  } else {
    _M0L3numS398 = *(uint64_t*)&_M0L4selfS396;
  }
  switch (_M0L5radixS395) {
    case 10: {
      int32_t _M0L10digit__lenS400;
      int32_t _M0L6_2atmpS1491;
      int32_t _M0L10total__lenS401;
      uint16_t* _M0L6bufferS402;
      int32_t _M0L12digit__startS403;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS400 = _M0FPB12dec__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1491 = 1;
      } else {
        _M0L6_2atmpS1491 = 0;
      }
      _M0L10total__lenS401 = _M0L10digit__lenS400 + _M0L6_2atmpS1491;
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
      int32_t _M0L6_2atmpS1492;
      int32_t _M0L10total__lenS405;
      uint16_t* _M0L6bufferS406;
      int32_t _M0L12digit__startS407;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS404 = _M0FPB12hex__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1492 = 1;
      } else {
        _M0L6_2atmpS1492 = 0;
      }
      _M0L10total__lenS405 = _M0L10digit__lenS404 + _M0L6_2atmpS1492;
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
      int32_t _M0L6_2atmpS1493;
      int32_t _M0L10total__lenS409;
      uint16_t* _M0L6bufferS410;
      int32_t _M0L12digit__startS411;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS408
      = _M0FPB14radix__count64(_M0L3numS398, _M0L5radixS395);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS1493 = 1;
      } else {
        _M0L6_2atmpS1493 = 0;
      }
      _M0L10total__lenS409 = _M0L10digit__lenS408 + _M0L6_2atmpS1493;
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
  int32_t _M0L6_2atmpS1490;
  uint64_t _M0L3numS371;
  int32_t _M0L6offsetS372;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1490 = _M0L10total__lenS394 - _M0L12digit__startS382;
  _M0L3numS371 = _M0L3numS393;
  _M0L6offsetS372 = _M0L6_2atmpS1490;
  while (1) {
    if (_M0L3numS371 >= 10000ull) {
      uint64_t _M0L1tS373 = _M0L3numS371 / 10000ull;
      uint64_t _M0L6_2atmpS1467 = _M0L3numS371 % 10000ull;
      int32_t _M0L1rS374 = (int32_t)_M0L6_2atmpS1467;
      int32_t _M0L2d1S375 = _M0L1rS374 / 100;
      int32_t _M0L2d2S376 = _M0L1rS374 % 100;
      int32_t _M0L6_2atmpS1466 = _M0L2d1S375 / 10;
      int32_t _M0L6_2atmpS1465 = 48 + _M0L6_2atmpS1466;
      int32_t _M0L6d1__hiS377 = (uint16_t)_M0L6_2atmpS1465;
      int32_t _M0L6_2atmpS1464 = _M0L2d1S375 % 10;
      int32_t _M0L6_2atmpS1463 = 48 + _M0L6_2atmpS1464;
      int32_t _M0L6d1__loS378 = (uint16_t)_M0L6_2atmpS1463;
      int32_t _M0L6_2atmpS1462 = _M0L2d2S376 / 10;
      int32_t _M0L6_2atmpS1461 = 48 + _M0L6_2atmpS1462;
      int32_t _M0L6d2__hiS379 = (uint16_t)_M0L6_2atmpS1461;
      int32_t _M0L6_2atmpS1460 = _M0L2d2S376 % 10;
      int32_t _M0L6_2atmpS1459 = 48 + _M0L6_2atmpS1460;
      int32_t _M0L6d2__loS380 = (uint16_t)_M0L6_2atmpS1459;
      int32_t _M0L6_2atmpS1451 = _M0L12digit__startS382 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS1450 = _M0L6_2atmpS1451 - 4;
      int32_t _M0L6_2atmpS1453;
      int32_t _M0L6_2atmpS1452;
      int32_t _M0L6_2atmpS1455;
      int32_t _M0L6_2atmpS1454;
      int32_t _M0L6_2atmpS1457;
      int32_t _M0L6_2atmpS1456;
      int32_t _M0L6_2atmpS1458;
      _M0L6bufferS381[_M0L6_2atmpS1450] = _M0L6d1__hiS377;
      _M0L6_2atmpS1453 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1452 = _M0L6_2atmpS1453 - 3;
      _M0L6bufferS381[_M0L6_2atmpS1452] = _M0L6d1__loS378;
      _M0L6_2atmpS1455 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1454 = _M0L6_2atmpS1455 - 2;
      _M0L6bufferS381[_M0L6_2atmpS1454] = _M0L6d2__hiS379;
      _M0L6_2atmpS1457 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS1456 = _M0L6_2atmpS1457 - 1;
      _M0L6bufferS381[_M0L6_2atmpS1456] = _M0L6d2__loS380;
      _M0L6_2atmpS1458 = _M0L6offsetS372 - 4;
      _M0L3numS371 = _M0L1tS373;
      _M0L6offsetS372 = _M0L6_2atmpS1458;
      continue;
    } else {
      int32_t _M0L6_2atmpS1489 = (int32_t)_M0L3numS371;
      int32_t _M0L9remainingS384 = _M0L6_2atmpS1489;
      int32_t _M0L6offsetS385 = _M0L6offsetS372;
      while (1) {
        if (_M0L9remainingS384 >= 100) {
          int32_t _M0L1tS386 = _M0L9remainingS384 / 100;
          int32_t _M0L1dS387 = _M0L9remainingS384 % 100;
          int32_t _M0L6_2atmpS1476 = _M0L1dS387 / 10;
          int32_t _M0L6_2atmpS1475 = 48 + _M0L6_2atmpS1476;
          int32_t _M0L5d__hiS388 = (uint16_t)_M0L6_2atmpS1475;
          int32_t _M0L6_2atmpS1474 = _M0L1dS387 % 10;
          int32_t _M0L6_2atmpS1473 = 48 + _M0L6_2atmpS1474;
          int32_t _M0L5d__loS389 = (uint16_t)_M0L6_2atmpS1473;
          int32_t _M0L6_2atmpS1469 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1468 = _M0L6_2atmpS1469 - 2;
          int32_t _M0L6_2atmpS1471;
          int32_t _M0L6_2atmpS1470;
          int32_t _M0L6_2atmpS1472;
          _M0L6bufferS381[_M0L6_2atmpS1468] = _M0L5d__hiS388;
          _M0L6_2atmpS1471 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1470 = _M0L6_2atmpS1471 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1470] = _M0L5d__loS389;
          _M0L6_2atmpS1472 = _M0L6offsetS385 - 2;
          _M0L9remainingS384 = _M0L1tS386;
          _M0L6offsetS385 = _M0L6_2atmpS1472;
          continue;
        } else if (_M0L9remainingS384 >= 10) {
          int32_t _M0L6_2atmpS1484 = _M0L9remainingS384 / 10;
          int32_t _M0L6_2atmpS1483 = 48 + _M0L6_2atmpS1484;
          int32_t _M0L5d__hiS391 = (uint16_t)_M0L6_2atmpS1483;
          int32_t _M0L6_2atmpS1482 = _M0L9remainingS384 % 10;
          int32_t _M0L6_2atmpS1481 = 48 + _M0L6_2atmpS1482;
          int32_t _M0L5d__loS392 = (uint16_t)_M0L6_2atmpS1481;
          int32_t _M0L6_2atmpS1478 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1477 = _M0L6_2atmpS1478 - 2;
          int32_t _M0L6_2atmpS1480;
          int32_t _M0L6_2atmpS1479;
          _M0L6bufferS381[_M0L6_2atmpS1477] = _M0L5d__hiS391;
          _M0L6_2atmpS1480 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS1479 = _M0L6_2atmpS1480 - 1;
          _M0L6bufferS381[_M0L6_2atmpS1479] = _M0L5d__loS392;
        } else {
          int32_t _M0L6_2atmpS1488 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS1485 = _M0L6_2atmpS1488 - 1;
          int32_t _M0L6_2atmpS1487 = 48 + _M0L9remainingS384;
          int32_t _M0L6_2atmpS1486 = (uint16_t)_M0L6_2atmpS1487;
          _M0L6bufferS381[_M0L6_2atmpS1485] = _M0L6_2atmpS1486;
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
  int32_t _M0L6_2atmpS1435;
  int32_t _M0L6_2atmpS1434;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS354 = _M0MPC13int3Int10to__uint64(_M0L5radixS355);
  _M0L6_2atmpS1435 = _M0L5radixS355 - 1;
  _M0L6_2atmpS1434 = _M0L5radixS355 & _M0L6_2atmpS1435;
  if (_M0L6_2atmpS1434 == 0) {
    int32_t _M0L5shiftS356;
    uint64_t _M0L4maskS357;
    int32_t _M0L6_2atmpS1442;
    int32_t _M0L6offsetS358;
    uint64_t _M0L1nS359;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS356 = moonbit_ctz32(_M0L5radixS355);
    _M0L4maskS357 = _M0L4baseS354 - 1ull;
    _M0L6_2atmpS1442 = _M0L10total__lenS364 - _M0L12digit__startS362;
    _M0L6offsetS358 = _M0L6_2atmpS1442;
    _M0L1nS359 = _M0L3numS365;
    while (1) {
      if (_M0L1nS359 > 0ull) {
        uint64_t _M0L6_2atmpS1441 = _M0L1nS359 & _M0L4maskS357;
        int32_t _M0L5digitS360 = (int32_t)_M0L6_2atmpS1441;
        int32_t _M0L6_2atmpS1438 = _M0L12digit__startS362 + _M0L6offsetS358;
        int32_t _M0L6_2atmpS1436 = _M0L6_2atmpS1438 - 1;
        int32_t _M0L6_2atmpS1437 =
          ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L5digitS360];
        int32_t _M0L6_2atmpS1439;
        uint64_t _M0L6_2atmpS1440;
        _M0L6bufferS361[_M0L6_2atmpS1436] = _M0L6_2atmpS1437;
        _M0L6_2atmpS1439 = _M0L6offsetS358 - 1;
        _M0L6_2atmpS1440 = _M0L1nS359 >> (_M0L5shiftS356 & 63);
        _M0L6offsetS358 = _M0L6_2atmpS1439;
        _M0L1nS359 = _M0L6_2atmpS1440;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1449 = _M0L10total__lenS364 - _M0L12digit__startS362;
    int32_t _M0L6offsetS366 = _M0L6_2atmpS1449;
    uint64_t _M0L1nS367 = _M0L3numS365;
    while (1) {
      if (_M0L1nS367 > 0ull) {
        uint64_t _M0L1qS368 = _M0L1nS367 / _M0L4baseS354;
        uint64_t _M0L6_2atmpS1448 = _M0L1qS368 * _M0L4baseS354;
        uint64_t _M0L6_2atmpS1447 = _M0L1nS367 - _M0L6_2atmpS1448;
        int32_t _M0L5digitS369 = (int32_t)_M0L6_2atmpS1447;
        int32_t _M0L6_2atmpS1445 = _M0L12digit__startS362 + _M0L6offsetS366;
        int32_t _M0L6_2atmpS1443 = _M0L6_2atmpS1445 - 1;
        int32_t _M0L6_2atmpS1444 =
          ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L5digitS369];
        int32_t _M0L6_2atmpS1446;
        _M0L6bufferS361[_M0L6_2atmpS1443] = _M0L6_2atmpS1444;
        _M0L6_2atmpS1446 = _M0L6offsetS366 - 1;
        _M0L6offsetS366 = _M0L6_2atmpS1446;
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
  int32_t _M0L6_2atmpS1433;
  int32_t _M0L6offsetS343;
  uint64_t _M0L1nS344;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1433 = _M0L10total__lenS352 - _M0L12digit__startS349;
  _M0L6offsetS343 = _M0L6_2atmpS1433;
  _M0L1nS344 = _M0L3numS353;
  while (1) {
    if (_M0L6offsetS343 >= 2) {
      uint64_t _M0L6_2atmpS1430 = _M0L1nS344 & 255ull;
      int32_t _M0L9byte__valS345 = (int32_t)_M0L6_2atmpS1430;
      int32_t _M0L2hiS346 = _M0L9byte__valS345 / 16;
      int32_t _M0L2loS347 = _M0L9byte__valS345 % 16;
      int32_t _M0L6_2atmpS1424 = _M0L12digit__startS349 + _M0L6offsetS343;
      int32_t _M0L6_2atmpS1422 = _M0L6_2atmpS1424 - 2;
      int32_t _M0L6_2atmpS1423 =
        ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L2hiS346];
      int32_t _M0L6_2atmpS1427;
      int32_t _M0L6_2atmpS1425;
      int32_t _M0L6_2atmpS1426;
      int32_t _M0L6_2atmpS1428;
      uint64_t _M0L6_2atmpS1429;
      _M0L6bufferS348[_M0L6_2atmpS1422] = _M0L6_2atmpS1423;
      _M0L6_2atmpS1427 = _M0L12digit__startS349 + _M0L6offsetS343;
      _M0L6_2atmpS1425 = _M0L6_2atmpS1427 - 1;
      _M0L6_2atmpS1426
      = ((moonbit_string_t)moonbit_string_literal_21.data)[
        _M0L2loS347
      ];
      _M0L6bufferS348[_M0L6_2atmpS1425] = _M0L6_2atmpS1426;
      _M0L6_2atmpS1428 = _M0L6offsetS343 - 2;
      _M0L6_2atmpS1429 = _M0L1nS344 >> 8;
      _M0L6offsetS343 = _M0L6_2atmpS1428;
      _M0L1nS344 = _M0L6_2atmpS1429;
      continue;
    } else if (_M0L6offsetS343 == 1) {
      uint64_t _M0L6_2atmpS1432 = _M0L1nS344 & 15ull;
      int32_t _M0L6nibbleS351 = (int32_t)_M0L6_2atmpS1432;
      int32_t _M0L6_2atmpS1431 =
        ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L6nibbleS351];
      _M0L6bufferS348[_M0L12digit__startS349] = _M0L6_2atmpS1431;
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
      uint64_t _M0L6_2atmpS1420 = _M0L3numS340 / _M0L4baseS338;
      int32_t _M0L6_2atmpS1421 = _M0L5countS341 + 1;
      _M0L3numS340 = _M0L6_2atmpS1420;
      _M0L5countS341 = _M0L6_2atmpS1421;
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
    int32_t _M0L6_2atmpS1419;
    int32_t _M0L6_2atmpS1418;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS336 = moonbit_clz64(_M0L5valueS335);
    _M0L6_2atmpS1419 = 63 - _M0L14leading__zerosS336;
    _M0L6_2atmpS1418 = _M0L6_2atmpS1419 / 4;
    return _M0L6_2atmpS1418 + 1;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_20.data);
  }
  if (_M0L4selfS318 == 0) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  _M0L12is__negativeS319 = _M0L4selfS318 < 0;
  if (_M0L12is__negativeS319) {
    int32_t _M0L6_2atmpS1417 = -_M0L4selfS318;
    _M0L3numS320 = *(uint32_t*)&_M0L6_2atmpS1417;
  } else {
    _M0L3numS320 = *(uint32_t*)&_M0L4selfS318;
  }
  switch (_M0L5radixS317) {
    case 10: {
      int32_t _M0L10digit__lenS322;
      int32_t _M0L6_2atmpS1414;
      int32_t _M0L10total__lenS323;
      uint16_t* _M0L6bufferS324;
      int32_t _M0L12digit__startS325;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS322 = _M0FPB12dec__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1414 = 1;
      } else {
        _M0L6_2atmpS1414 = 0;
      }
      _M0L10total__lenS323 = _M0L10digit__lenS322 + _M0L6_2atmpS1414;
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
      int32_t _M0L6_2atmpS1415;
      int32_t _M0L10total__lenS327;
      uint16_t* _M0L6bufferS328;
      int32_t _M0L12digit__startS329;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS326 = _M0FPB12hex__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1415 = 1;
      } else {
        _M0L6_2atmpS1415 = 0;
      }
      _M0L10total__lenS327 = _M0L10digit__lenS326 + _M0L6_2atmpS1415;
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
      int32_t _M0L6_2atmpS1416;
      int32_t _M0L10total__lenS331;
      uint16_t* _M0L6bufferS332;
      int32_t _M0L12digit__startS333;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS330
      = _M0FPB14radix__count32(_M0L3numS320, _M0L5radixS317);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS1416 = 1;
      } else {
        _M0L6_2atmpS1416 = 0;
      }
      _M0L10total__lenS331 = _M0L10digit__lenS330 + _M0L6_2atmpS1416;
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
      uint32_t _M0L6_2atmpS1412 = _M0L3numS314 / _M0L4baseS312;
      int32_t _M0L6_2atmpS1413 = _M0L5countS315 + 1;
      _M0L3numS314 = _M0L6_2atmpS1412;
      _M0L5countS315 = _M0L6_2atmpS1413;
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
    int32_t _M0L6_2atmpS1411;
    int32_t _M0L6_2atmpS1410;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS310 = moonbit_clz32(_M0L5valueS309);
    _M0L6_2atmpS1411 = 31 - _M0L14leading__zerosS310;
    _M0L6_2atmpS1410 = _M0L6_2atmpS1411 / 4;
    return _M0L6_2atmpS1410 + 1;
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
  int32_t _M0L6_2atmpS1409;
  uint32_t _M0L3numS284;
  int32_t _M0L6offsetS285;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1409 = _M0L10total__lenS307 - _M0L12digit__startS295;
  _M0L3numS284 = _M0L3numS306;
  _M0L6offsetS285 = _M0L6_2atmpS1409;
  while (1) {
    if (_M0L3numS284 >= 10000u) {
      uint32_t _M0L1tS286 = _M0L3numS284 / 10000u;
      uint32_t _M0L6_2atmpS1386 = _M0L3numS284 % 10000u;
      int32_t _M0L1rS287 = *(int32_t*)&_M0L6_2atmpS1386;
      int32_t _M0L2d1S288 = _M0L1rS287 / 100;
      int32_t _M0L2d2S289 = _M0L1rS287 % 100;
      int32_t _M0L6_2atmpS1385 = _M0L2d1S288 / 10;
      int32_t _M0L6_2atmpS1384 = 48 + _M0L6_2atmpS1385;
      int32_t _M0L6d1__hiS290 = (uint16_t)_M0L6_2atmpS1384;
      int32_t _M0L6_2atmpS1383 = _M0L2d1S288 % 10;
      int32_t _M0L6_2atmpS1382 = 48 + _M0L6_2atmpS1383;
      int32_t _M0L6d1__loS291 = (uint16_t)_M0L6_2atmpS1382;
      int32_t _M0L6_2atmpS1381 = _M0L2d2S289 / 10;
      int32_t _M0L6_2atmpS1380 = 48 + _M0L6_2atmpS1381;
      int32_t _M0L6d2__hiS292 = (uint16_t)_M0L6_2atmpS1380;
      int32_t _M0L6_2atmpS1379 = _M0L2d2S289 % 10;
      int32_t _M0L6_2atmpS1378 = 48 + _M0L6_2atmpS1379;
      int32_t _M0L6d2__loS293 = (uint16_t)_M0L6_2atmpS1378;
      int32_t _M0L6_2atmpS1370 = _M0L12digit__startS295 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS1369 = _M0L6_2atmpS1370 - 4;
      int32_t _M0L6_2atmpS1372;
      int32_t _M0L6_2atmpS1371;
      int32_t _M0L6_2atmpS1374;
      int32_t _M0L6_2atmpS1373;
      int32_t _M0L6_2atmpS1376;
      int32_t _M0L6_2atmpS1375;
      int32_t _M0L6_2atmpS1377;
      _M0L6bufferS294[_M0L6_2atmpS1369] = _M0L6d1__hiS290;
      _M0L6_2atmpS1372 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1371 = _M0L6_2atmpS1372 - 3;
      _M0L6bufferS294[_M0L6_2atmpS1371] = _M0L6d1__loS291;
      _M0L6_2atmpS1374 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1373 = _M0L6_2atmpS1374 - 2;
      _M0L6bufferS294[_M0L6_2atmpS1373] = _M0L6d2__hiS292;
      _M0L6_2atmpS1376 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS1375 = _M0L6_2atmpS1376 - 1;
      _M0L6bufferS294[_M0L6_2atmpS1375] = _M0L6d2__loS293;
      _M0L6_2atmpS1377 = _M0L6offsetS285 - 4;
      _M0L3numS284 = _M0L1tS286;
      _M0L6offsetS285 = _M0L6_2atmpS1377;
      continue;
    } else {
      int32_t _M0L6_2atmpS1408 = *(int32_t*)&_M0L3numS284;
      int32_t _M0L9remainingS297 = _M0L6_2atmpS1408;
      int32_t _M0L6offsetS298 = _M0L6offsetS285;
      while (1) {
        if (_M0L9remainingS297 >= 100) {
          int32_t _M0L1tS299 = _M0L9remainingS297 / 100;
          int32_t _M0L1dS300 = _M0L9remainingS297 % 100;
          int32_t _M0L6_2atmpS1395 = _M0L1dS300 / 10;
          int32_t _M0L6_2atmpS1394 = 48 + _M0L6_2atmpS1395;
          int32_t _M0L5d__hiS301 = (uint16_t)_M0L6_2atmpS1394;
          int32_t _M0L6_2atmpS1393 = _M0L1dS300 % 10;
          int32_t _M0L6_2atmpS1392 = 48 + _M0L6_2atmpS1393;
          int32_t _M0L5d__loS302 = (uint16_t)_M0L6_2atmpS1392;
          int32_t _M0L6_2atmpS1388 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1387 = _M0L6_2atmpS1388 - 2;
          int32_t _M0L6_2atmpS1390;
          int32_t _M0L6_2atmpS1389;
          int32_t _M0L6_2atmpS1391;
          _M0L6bufferS294[_M0L6_2atmpS1387] = _M0L5d__hiS301;
          _M0L6_2atmpS1390 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1389 = _M0L6_2atmpS1390 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1389] = _M0L5d__loS302;
          _M0L6_2atmpS1391 = _M0L6offsetS298 - 2;
          _M0L9remainingS297 = _M0L1tS299;
          _M0L6offsetS298 = _M0L6_2atmpS1391;
          continue;
        } else if (_M0L9remainingS297 >= 10) {
          int32_t _M0L6_2atmpS1403 = _M0L9remainingS297 / 10;
          int32_t _M0L6_2atmpS1402 = 48 + _M0L6_2atmpS1403;
          int32_t _M0L5d__hiS304 = (uint16_t)_M0L6_2atmpS1402;
          int32_t _M0L6_2atmpS1401 = _M0L9remainingS297 % 10;
          int32_t _M0L6_2atmpS1400 = 48 + _M0L6_2atmpS1401;
          int32_t _M0L5d__loS305 = (uint16_t)_M0L6_2atmpS1400;
          int32_t _M0L6_2atmpS1397 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1396 = _M0L6_2atmpS1397 - 2;
          int32_t _M0L6_2atmpS1399;
          int32_t _M0L6_2atmpS1398;
          _M0L6bufferS294[_M0L6_2atmpS1396] = _M0L5d__hiS304;
          _M0L6_2atmpS1399 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS1398 = _M0L6_2atmpS1399 - 1;
          _M0L6bufferS294[_M0L6_2atmpS1398] = _M0L5d__loS305;
        } else {
          int32_t _M0L6_2atmpS1407 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS1404 = _M0L6_2atmpS1407 - 1;
          int32_t _M0L6_2atmpS1406 = 48 + _M0L9remainingS297;
          int32_t _M0L6_2atmpS1405 = (uint16_t)_M0L6_2atmpS1406;
          _M0L6bufferS294[_M0L6_2atmpS1404] = _M0L6_2atmpS1405;
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
  int32_t _M0L6_2atmpS1354;
  int32_t _M0L6_2atmpS1353;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS267 = *(uint32_t*)&_M0L5radixS268;
  _M0L6_2atmpS1354 = _M0L5radixS268 - 1;
  _M0L6_2atmpS1353 = _M0L5radixS268 & _M0L6_2atmpS1354;
  if (_M0L6_2atmpS1353 == 0) {
    int32_t _M0L5shiftS269;
    uint32_t _M0L4maskS270;
    int32_t _M0L6_2atmpS1361;
    int32_t _M0L6offsetS271;
    uint32_t _M0L1nS272;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS269 = moonbit_ctz32(_M0L5radixS268);
    _M0L4maskS270 = _M0L4baseS267 - 1u;
    _M0L6_2atmpS1361 = _M0L10total__lenS277 - _M0L12digit__startS275;
    _M0L6offsetS271 = _M0L6_2atmpS1361;
    _M0L1nS272 = _M0L3numS278;
    while (1) {
      if (_M0L1nS272 > 0u) {
        uint32_t _M0L6_2atmpS1360 = _M0L1nS272 & _M0L4maskS270;
        int32_t _M0L5digitS273 = *(int32_t*)&_M0L6_2atmpS1360;
        int32_t _M0L6_2atmpS1357 = _M0L12digit__startS275 + _M0L6offsetS271;
        int32_t _M0L6_2atmpS1355 = _M0L6_2atmpS1357 - 1;
        int32_t _M0L6_2atmpS1356 =
          ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L5digitS273];
        int32_t _M0L6_2atmpS1358;
        uint32_t _M0L6_2atmpS1359;
        _M0L6bufferS274[_M0L6_2atmpS1355] = _M0L6_2atmpS1356;
        _M0L6_2atmpS1358 = _M0L6offsetS271 - 1;
        _M0L6_2atmpS1359 = _M0L1nS272 >> (_M0L5shiftS269 & 31);
        _M0L6offsetS271 = _M0L6_2atmpS1358;
        _M0L1nS272 = _M0L6_2atmpS1359;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1368 = _M0L10total__lenS277 - _M0L12digit__startS275;
    int32_t _M0L6offsetS279 = _M0L6_2atmpS1368;
    uint32_t _M0L1nS280 = _M0L3numS278;
    while (1) {
      if (_M0L1nS280 > 0u) {
        uint32_t _M0L1qS281 = _M0L1nS280 / _M0L4baseS267;
        uint32_t _M0L6_2atmpS1367 = _M0L1qS281 * _M0L4baseS267;
        uint32_t _M0L6_2atmpS1366 = _M0L1nS280 - _M0L6_2atmpS1367;
        int32_t _M0L5digitS282 = *(int32_t*)&_M0L6_2atmpS1366;
        int32_t _M0L6_2atmpS1364 = _M0L12digit__startS275 + _M0L6offsetS279;
        int32_t _M0L6_2atmpS1362 = _M0L6_2atmpS1364 - 1;
        int32_t _M0L6_2atmpS1363 =
          ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L5digitS282];
        int32_t _M0L6_2atmpS1365;
        _M0L6bufferS274[_M0L6_2atmpS1362] = _M0L6_2atmpS1363;
        _M0L6_2atmpS1365 = _M0L6offsetS279 - 1;
        _M0L6offsetS279 = _M0L6_2atmpS1365;
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
  int32_t _M0L6_2atmpS1352;
  int32_t _M0L6offsetS256;
  uint32_t _M0L1nS257;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1352 = _M0L10total__lenS265 - _M0L12digit__startS262;
  _M0L6offsetS256 = _M0L6_2atmpS1352;
  _M0L1nS257 = _M0L3numS266;
  while (1) {
    if (_M0L6offsetS256 >= 2) {
      uint32_t _M0L6_2atmpS1349 = _M0L1nS257 & 255u;
      int32_t _M0L9byte__valS258 = *(int32_t*)&_M0L6_2atmpS1349;
      int32_t _M0L2hiS259 = _M0L9byte__valS258 / 16;
      int32_t _M0L2loS260 = _M0L9byte__valS258 % 16;
      int32_t _M0L6_2atmpS1343 = _M0L12digit__startS262 + _M0L6offsetS256;
      int32_t _M0L6_2atmpS1341 = _M0L6_2atmpS1343 - 2;
      int32_t _M0L6_2atmpS1342 =
        ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L2hiS259];
      int32_t _M0L6_2atmpS1346;
      int32_t _M0L6_2atmpS1344;
      int32_t _M0L6_2atmpS1345;
      int32_t _M0L6_2atmpS1347;
      uint32_t _M0L6_2atmpS1348;
      _M0L6bufferS261[_M0L6_2atmpS1341] = _M0L6_2atmpS1342;
      _M0L6_2atmpS1346 = _M0L12digit__startS262 + _M0L6offsetS256;
      _M0L6_2atmpS1344 = _M0L6_2atmpS1346 - 1;
      _M0L6_2atmpS1345
      = ((moonbit_string_t)moonbit_string_literal_21.data)[
        _M0L2loS260
      ];
      _M0L6bufferS261[_M0L6_2atmpS1344] = _M0L6_2atmpS1345;
      _M0L6_2atmpS1347 = _M0L6offsetS256 - 2;
      _M0L6_2atmpS1348 = _M0L1nS257 >> 8;
      _M0L6offsetS256 = _M0L6_2atmpS1347;
      _M0L1nS257 = _M0L6_2atmpS1348;
      continue;
    } else if (_M0L6offsetS256 == 1) {
      uint32_t _M0L6_2atmpS1351 = _M0L1nS257 & 15u;
      int32_t _M0L6nibbleS264 = *(int32_t*)&_M0L6_2atmpS1351;
      int32_t _M0L6_2atmpS1350 =
        ((moonbit_string_t)moonbit_string_literal_21.data)[_M0L6nibbleS264];
      _M0L6bufferS261[_M0L12digit__startS262] = _M0L6_2atmpS1350;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS255
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS254;
  struct _M0TPB6Logger _M0L6_2atmpS1340;
  moonbit_string_t _result_2302;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS254);
  _M0L6_2atmpS1340
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS254
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS255, _M0L6_2atmpS1340);
  if (_M0L6_2atmpS1340.$1) {
    moonbit_decref(_M0L6_2atmpS1340.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2302 = _M0MPB13StringBuilder10to__string(_M0L6loggerS254);
  moonbit_decref_cycle_free(_M0L6loggerS254);
  return _result_2302;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS249,
  struct _M0TPB6Logger _M0L6loggerS248
) {
  moonbit_string_t _M0L6_2atmpS1337;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1337 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS249);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248.$0->$method_0(_M0L6loggerS248.$1, _M0L6_2atmpS1337);
  moonbit_decref_cycle_free(_M0L6_2atmpS1337);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS1338;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1338 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS1338);
  moonbit_decref_cycle_free(_M0L6_2atmpS1338);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS1339;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1339 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS1339);
  moonbit_decref_cycle_free(_M0L6_2atmpS1339);
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
  moonbit_string_t _M0L8_2afieldS2191;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS2191 = _M0L4selfS246.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS2191);
  return _M0L8_2afieldS2191;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS242,
  moonbit_string_t _M0L5valueS243,
  int32_t _M0L5startS244,
  int32_t _M0L3lenS245
) {
  int32_t _M0L6_2atmpS1336;
  int64_t _M0L6_2atmpS1335;
  struct _M0TPC16string10StringView _M0L6_2atmpS1334;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1336 = _M0L5startS244 + _M0L3lenS245;
  _M0L6_2atmpS1335 = (int64_t)_M0L6_2atmpS1336;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1334
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS243, _M0L5startS244, _M0L6_2atmpS1335);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS242, _M0L6_2atmpS1334);
  moonbit_decref_cycle_free(_M0L6_2atmpS1334.$0);
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
  int32_t _M0L6_2atmpS1318;
  int32_t _if__result_2303;
  int32_t _M0L6_2atmpS1326;
  int32_t _if__result_2304;
  int32_t _M0L6_2atmpS1328;
  int32_t _M0L6_2atmpS1329;
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
  _M0L6_2atmpS1318 = _M0Lm2loS236;
  if (_M0L6_2atmpS1318 > 0) {
    int32_t _M0L6_2atmpS1317 = _M0Lm2loS236;
    if (_M0L6_2atmpS1317 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1316 = _M0Lm2loS236;
      int32_t _M0L6_2atmpS1315 = _M0L4selfS235[_M0L6_2atmpS1316];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1315)) {
        int32_t _M0L6_2atmpS1314 = _M0Lm2loS236;
        int32_t _M0L6_2atmpS1313 = _M0L6_2atmpS1314 - 1;
        int32_t _M0L6_2atmpS1312 = _M0L4selfS235[_M0L6_2atmpS1313];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2303
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1312);
      } else {
        _if__result_2303 = 0;
      }
    } else {
      _if__result_2303 = 0;
    }
  } else {
    _if__result_2303 = 0;
  }
  if (_if__result_2303) {
    int32_t _M0L6_2atmpS1319 = _M0Lm2loS236;
    _M0Lm2loS236 = _M0L6_2atmpS1319 + 1;
  }
  _M0L6_2atmpS1326 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1326 > 0) {
    int32_t _M0L6_2atmpS1325 = _M0Lm2hiS238;
    if (_M0L6_2atmpS1325 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS1324 = _M0Lm2hiS238;
      int32_t _M0L6_2atmpS1323 = _M0L4selfS235[_M0L6_2atmpS1324];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1323)) {
        int32_t _M0L6_2atmpS1322 = _M0Lm2hiS238;
        int32_t _M0L6_2atmpS1321 = _M0L6_2atmpS1322 - 1;
        int32_t _M0L6_2atmpS1320 = _M0L4selfS235[_M0L6_2atmpS1321];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2304
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1320);
      } else {
        _if__result_2304 = 0;
      }
    } else {
      _if__result_2304 = 0;
    }
  } else {
    _if__result_2304 = 0;
  }
  if (_if__result_2304) {
    int32_t _M0L6_2atmpS1327 = _M0Lm2hiS238;
    _M0Lm2hiS238 = _M0L6_2atmpS1327 - 1;
  }
  _M0L6_2atmpS1328 = _M0Lm2loS236;
  _M0L6_2atmpS1329 = _M0Lm2hiS238;
  if (_M0L6_2atmpS1328 >= _M0L6_2atmpS1329) {
    int32_t _M0L6_2atmpS1330 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1331 = _M0Lm2loS236;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1330,
                                                 .$2 = _M0L6_2atmpS1331};
  } else {
    int32_t _M0L6_2atmpS1332 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS1333 = _M0Lm2hiS238;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS1332,
                                                 .$2 = _M0L6_2atmpS1333};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS1311;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS1311
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS1311);
  if (_M0L6_2atmpS1311.$1) {
    moonbit_decref(_M0L6_2atmpS1311.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS231,
  struct _M0TPB4Show _M0L4showS230
) {
  struct _M0TPB6Logger _M0L6_2atmpS1310;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS231);
  _M0L6_2atmpS1310
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS231
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS230.$0->$method_0(_M0L4showS230.$1, _M0L6_2atmpS1310);
  if (_M0L6_2atmpS1310.$1) {
    moonbit_decref(_M0L6_2atmpS1310.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS229) {
  int64_t _M0L6_2atmpS1309;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1309 = (int64_t)_M0L4selfS229;
  return *(uint64_t*)&_M0L6_2atmpS1309;
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
  int32_t _M0L6_2atmpS1308;
  struct _M0TPC16string10StringView _M0L6_2atmpS1306;
  struct _M0TPB6Logger _M0L6_2atmpS1307;
  moonbit_string_t _result_2305;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1308 = Moonbit_array_length(_M0L4selfS227);
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS1306
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS227, .$1 = 0, .$2 = _M0L6_2atmpS1308
  };
  moonbit_incref_cycle_free(_M0L3bufS226);
  _M0L6_2atmpS1307
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS226
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1306, _M0L6_2atmpS1307, _M0L5quoteS228);
  moonbit_decref_cycle_free(_M0L6_2atmpS1306.$0);
  if (_M0L6_2atmpS1307.$1) {
    moonbit_decref(_M0L6_2atmpS1307.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2305 = _M0MPB13StringBuilder10to__string(_M0L3bufS226);
  moonbit_decref_cycle_free(_M0L3bufS226);
  return _result_2305;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS218,
  struct _M0TPB6Logger _M0L6loggerS216,
  int32_t _M0L5quoteS215
) {
  int32_t _M0L3endS1304;
  int32_t _M0L5startS1305;
  int32_t _M0L3lenS217;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS219;
  int32_t _M0L1iS220;
  int32_t _M0L3segS221;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS215) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  _M0L3endS1304 = _M0L4selfS218.$2;
  _M0L5startS1305 = _M0L4selfS218.$1;
  _M0L3lenS217 = _M0L3endS1304 - _M0L5startS1305;
  moonbit_incref_cycle_free(_M0L4selfS218.$0);
  if (_M0L6loggerS216.$1) {
    moonbit_incref(_M0L6loggerS216.$1);
  }
  _M0L6_2aenvS219
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 80, 0);
  _M0L6_2aenvS219->$0 = _M0L4selfS218;
  _M0L6_2aenvS219->$1 = _M0L6loggerS216;
  _M0L1iS220 = 0;
  _M0L3segS221 = 0;
  _2afor_222:;
  while (1) {
    moonbit_string_t _M0L3strS1301;
    int32_t _M0L5startS1303;
    int32_t _M0L6_2atmpS1302;
    int32_t _M0L4codeS223;
    int32_t _M0L1cS225;
    int32_t _M0L6_2atmpS1285;
    int32_t _M0L6_2atmpS1286;
    int32_t _M0L6_2atmpS1287;
    if (_M0L1iS220 >= _M0L3lenS217) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
      moonbit_decref_cycle_free(_M0L6_2aenvS219);
      break;
    }
    _M0L3strS1301 = _M0L4selfS218.$0;
    _M0L5startS1303 = _M0L4selfS218.$1;
    _M0L6_2atmpS1302 = _M0L5startS1303 + _M0L1iS220;
    _M0L4codeS223 = _M0L3strS1301[_M0L6_2atmpS1302];
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
        int32_t _M0L6_2atmpS1288;
        int32_t _M0L6_2atmpS1289;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_22.data);
        _M0L6_2atmpS1288 = _M0L1iS220 + 1;
        _M0L6_2atmpS1289 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1288;
        _M0L3segS221 = _M0L6_2atmpS1289;
        goto _2afor_222;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1290;
        int32_t _M0L6_2atmpS1291;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_23.data);
        _M0L6_2atmpS1290 = _M0L1iS220 + 1;
        _M0L6_2atmpS1291 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1290;
        _M0L3segS221 = _M0L6_2atmpS1291;
        goto _2afor_222;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1292;
        int32_t _M0L6_2atmpS1293;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_24.data);
        _M0L6_2atmpS1292 = _M0L1iS220 + 1;
        _M0L6_2atmpS1293 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1292;
        _M0L3segS221 = _M0L6_2atmpS1293;
        goto _2afor_222;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1294;
        int32_t _M0L6_2atmpS1295;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_25.data);
        _M0L6_2atmpS1294 = _M0L1iS220 + 1;
        _M0L6_2atmpS1295 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS1294;
        _M0L3segS221 = _M0L6_2atmpS1295;
        goto _2afor_222;
        break;
      }
      default: {
        if (_M0L4codeS223 < 32) {
          int32_t _M0L6_2atmpS1297;
          moonbit_string_t _M0L6_2atmpS1296;
          int32_t _M0L6_2atmpS1298;
          int32_t _M0L6_2atmpS1299;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_26.data);
          _M0L6_2atmpS1297 = _M0L4codeS223 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1296 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1297);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, _M0L6_2atmpS1296);
          moonbit_decref_cycle_free(_M0L6_2atmpS1296);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1298 = _M0L1iS220 + 1;
          _M0L6_2atmpS1299 = _M0L1iS220 + 1;
          _M0L1iS220 = _M0L6_2atmpS1298;
          _M0L3segS221 = _M0L6_2atmpS1299;
          goto _2afor_222;
        } else {
          int32_t _M0L6_2atmpS1300 = _M0L1iS220 + 1;
          int32_t _tmp_2308 = _M0L3segS221;
          _M0L1iS220 = _M0L6_2atmpS1300;
          _M0L3segS221 = _tmp_2308;
          goto _2afor_222;
        }
        break;
      }
    }
    goto joinlet_2307;
    join_224:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1285 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS225);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, _M0L6_2atmpS1285);
    _M0L6_2atmpS1286 = _M0L1iS220 + 1;
    _M0L6_2atmpS1287 = _M0L1iS220 + 1;
    _M0L1iS220 = _M0L6_2atmpS1286;
    _M0L3segS221 = _M0L6_2atmpS1287;
    continue;
    joinlet_2307:;
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
    int64_t _M0L6_2atmpS1284 = (int64_t)_M0L1iS213;
    struct _M0TPC16string10StringView _M0L6_2atmpS1283;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1283
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS212, _M0L3segS214, _M0L6_2atmpS1284);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS210.$0->$method_2(_M0L6loggerS210.$1, _M0L6_2atmpS1283);
    moonbit_decref_cycle_free(_M0L6_2atmpS1283.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS201,
  int32_t _M0L5startS203,
  int64_t _M0L3endS205
) {
  int32_t _M0L3endS1281;
  int32_t _M0L5startS1282;
  int32_t _M0L3lenS200;
  int32_t _M0Lm2loS202;
  int32_t _M0Lm2hiS204;
  moonbit_string_t _M0L3strS208;
  int32_t _M0L4baseS209;
  int32_t _M0L6_2atmpS1259;
  int32_t _if__result_2309;
  int32_t _M0L6_2atmpS1269;
  int32_t _if__result_2310;
  int32_t _M0L6_2atmpS1271;
  int32_t _M0L6_2atmpS1272;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1281 = _M0L4selfS201.$2;
  _M0L5startS1282 = _M0L4selfS201.$1;
  _M0L3lenS200 = _M0L3endS1281 - _M0L5startS1282;
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
  _M0L6_2atmpS1259 = _M0Lm2loS202;
  if (_M0L6_2atmpS1259 > 0) {
    int32_t _M0L6_2atmpS1258 = _M0Lm2loS202;
    if (_M0L6_2atmpS1258 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1257 = _M0Lm2loS202;
      int32_t _M0L6_2atmpS1256 = _M0L4baseS209 + _M0L6_2atmpS1257;
      int32_t _M0L6_2atmpS1255 = _M0L3strS208[_M0L6_2atmpS1256];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1255)) {
        int32_t _M0L6_2atmpS1254 = _M0Lm2loS202;
        int32_t _M0L6_2atmpS1253 = _M0L4baseS209 + _M0L6_2atmpS1254;
        int32_t _M0L6_2atmpS1252 = _M0L6_2atmpS1253 - 1;
        int32_t _M0L6_2atmpS1251 = _M0L3strS208[_M0L6_2atmpS1252];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2309
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1251);
      } else {
        _if__result_2309 = 0;
      }
    } else {
      _if__result_2309 = 0;
    }
  } else {
    _if__result_2309 = 0;
  }
  if (_if__result_2309) {
    int32_t _M0L6_2atmpS1260 = _M0Lm2loS202;
    _M0Lm2loS202 = _M0L6_2atmpS1260 + 1;
  }
  _M0L6_2atmpS1269 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1269 > 0) {
    int32_t _M0L6_2atmpS1268 = _M0Lm2hiS204;
    if (_M0L6_2atmpS1268 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS1267 = _M0Lm2hiS204;
      int32_t _M0L6_2atmpS1266 = _M0L4baseS209 + _M0L6_2atmpS1267;
      int32_t _M0L6_2atmpS1265 = _M0L3strS208[_M0L6_2atmpS1266];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1265)) {
        int32_t _M0L6_2atmpS1264 = _M0Lm2hiS204;
        int32_t _M0L6_2atmpS1263 = _M0L4baseS209 + _M0L6_2atmpS1264;
        int32_t _M0L6_2atmpS1262 = _M0L6_2atmpS1263 - 1;
        int32_t _M0L6_2atmpS1261 = _M0L3strS208[_M0L6_2atmpS1262];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2310
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1261);
      } else {
        _if__result_2310 = 0;
      }
    } else {
      _if__result_2310 = 0;
    }
  } else {
    _if__result_2310 = 0;
  }
  if (_if__result_2310) {
    int32_t _M0L6_2atmpS1270 = _M0Lm2hiS204;
    _M0Lm2hiS204 = _M0L6_2atmpS1270 - 1;
  }
  _M0L6_2atmpS1271 = _M0Lm2loS202;
  _M0L6_2atmpS1272 = _M0Lm2hiS204;
  if (_M0L6_2atmpS1271 >= _M0L6_2atmpS1272) {
    int32_t _M0L6_2atmpS1276 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1273 = _M0L4baseS209 + _M0L6_2atmpS1276;
    int32_t _M0L6_2atmpS1275 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1274 = _M0L4baseS209 + _M0L6_2atmpS1275;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1273,
                                                 .$2 = _M0L6_2atmpS1274};
  } else {
    int32_t _M0L6_2atmpS1280 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS1277 = _M0L4baseS209 + _M0L6_2atmpS1280;
    int32_t _M0L6_2atmpS1279 = _M0Lm2hiS204;
    int32_t _M0L6_2atmpS1278 = _M0L4baseS209 + _M0L6_2atmpS1279;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS1277,
                                                 .$2 = _M0L6_2atmpS1278};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS199) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS198;
  int32_t _M0L6_2atmpS1248;
  int32_t _M0L6_2atmpS1247;
  int32_t _M0L6_2atmpS1250;
  int32_t _M0L6_2atmpS1249;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1246;
  moonbit_string_t _result_2311;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1248 = _M0IPC14byte4BytePB3Div3div(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1247
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1248);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1247);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1250 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1249
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1250);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS1249);
  _M0L6_2atmpS1246 = _M0L7_2aselfS198;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2311 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1246);
  moonbit_decref_cycle_free(_M0L6_2atmpS1246);
  return _result_2311;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS197) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS197 < 10) {
    int32_t _M0L6_2atmpS1243;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1243 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1243);
  } else {
    int32_t _M0L6_2atmpS1245;
    int32_t _M0L6_2atmpS1244;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1245 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1244 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1245, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1244);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS1241;
  int32_t _M0L6_2atmpS1242;
  int32_t _M0L6_2atmpS1240;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1241 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS1242 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS1240 = _M0L6_2atmpS1241 - _M0L6_2atmpS1242;
  return _M0L6_2atmpS1240 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS1238;
  int32_t _M0L6_2atmpS1239;
  int32_t _M0L6_2atmpS1237;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1238 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS1239 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS1237 = _M0L6_2atmpS1238 % _M0L6_2atmpS1239;
  return _M0L6_2atmpS1237 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS1235;
  int32_t _M0L6_2atmpS1236;
  int32_t _M0L6_2atmpS1234;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1235 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS1236 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS1234 = _M0L6_2atmpS1235 / _M0L6_2atmpS1236;
  return _M0L6_2atmpS1234 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS189,
  int32_t _M0L4thatS190
) {
  int32_t _M0L6_2atmpS1232;
  int32_t _M0L6_2atmpS1233;
  int32_t _M0L6_2atmpS1231;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1232 = (int32_t)_M0L4selfS189;
  _M0L6_2atmpS1233 = (int32_t)_M0L4thatS190;
  _M0L6_2atmpS1231 = _M0L6_2atmpS1232 + _M0L6_2atmpS1233;
  return _M0L6_2atmpS1231 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS188) {
  int32_t _M0L6_2atmpS1230;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS1230 = (int32_t)_M0L4selfS188;
  return _M0L6_2atmpS1230;
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
  int32_t _M0L3lenS1229;
  int32_t _M0L8requiredS184;
  uint16_t* _M0L4dataS1224;
  int32_t _M0L6_2atmpS1223;
  int32_t _if__result_2312;
  uint16_t* _M0L4dataS1225;
  int32_t _M0L3lenS1226;
  int32_t _M0L3lenS1228;
  int32_t _M0L6_2atmpS1227;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS182 = Moonbit_array_length(_M0L3strS183);
  if (_M0L8str__lenS182 == 0) {
    return 0;
  }
  _M0L3lenS1229 = _M0L4selfS185->$1;
  _M0L8requiredS184 = _M0L3lenS1229 + _M0L8str__lenS182;
  _M0L4dataS1224 = _M0L4selfS185->$0;
  _M0L6_2atmpS1223 = Moonbit_array_length(_M0L4dataS1224);
  if (_M0L8requiredS184 > _M0L6_2atmpS1223) {
    _if__result_2312 = 1;
  } else {
    int32_t _M0L3lenS1222 = _M0L4selfS185->$1;
    _if__result_2312 = _M0L8requiredS184 < _M0L3lenS1222;
  }
  if (_if__result_2312) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS185, _M0L8requiredS184);
  }
  _M0L4dataS1225 = _M0L4selfS185->$0;
  _M0L3lenS1226 = _M0L4selfS185->$1;
  moonbit_incref_cycle_free(_M0L4dataS1225);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1225, _M0L3lenS1226, _M0L3strS183, 0, _M0L8str__lenS182);
  moonbit_decref_cycle_free(_M0L4dataS1225);
  _M0L3lenS1228 = _M0L4selfS185->$1;
  _M0L6_2atmpS1227 = _M0L3lenS1228 + _M0L8str__lenS182;
  _M0L4selfS185->$1 = _M0L6_2atmpS1227;
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
      int32_t _M0L6_2atmpS1219 = _M0L3strS179[_M0L1iS176];
      int32_t _M0L6_2atmpS1220;
      int32_t _M0L6_2atmpS1221;
      _M0L4selfS178[_M0L1jS177] = _M0L6_2atmpS1219;
      _M0L6_2atmpS1220 = _M0L1iS176 + 1;
      _M0L6_2atmpS1221 = _M0L1jS177 + 1;
      _M0L1iS176 = _M0L6_2atmpS1220;
      _M0L1jS177 = _M0L6_2atmpS1221;
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
    int32_t _M0L3lenS1190 = _M0L4selfS171->$1;
    uint16_t* _M0L4dataS1192 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1191 = Moonbit_array_length(_M0L4dataS1192);
    uint16_t* _M0L4dataS1195;
    int32_t _M0L3lenS1196;
    int32_t _M0L6_2atmpS1197;
    int32_t _M0L3lenS1199;
    int32_t _M0L6_2atmpS1198;
    if (_M0L3lenS1190 >= _M0L6_2atmpS1191) {
      int32_t _M0L3lenS1194 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1193 = _M0L3lenS1194 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1193);
    }
    _M0L4dataS1195 = _M0L4selfS171->$0;
    _M0L3lenS1196 = _M0L4selfS171->$1;
    moonbit_incref_cycle_free(_M0L4dataS1195);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1197 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS169);
    if (
      _M0L3lenS1196 < 0
      || _M0L3lenS1196 >= Moonbit_array_length(_M0L4dataS1195)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1195[_M0L3lenS1196] = _M0L6_2atmpS1197;
    moonbit_decref_cycle_free(_M0L4dataS1195);
    _M0L3lenS1199 = _M0L4selfS171->$1;
    _M0L6_2atmpS1198 = _M0L3lenS1199 + 1;
    _M0L4selfS171->$1 = _M0L6_2atmpS1198;
  } else if (_M0L4codeS169 <= 1114111u) {
    uint16_t* _M0L4dataS1203 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS1201 = Moonbit_array_length(_M0L4dataS1203);
    int32_t _M0L3lenS1202 = _M0L4selfS171->$1;
    int32_t _M0L6_2atmpS1200 = _M0L6_2atmpS1201 - _M0L3lenS1202;
    uint32_t _M0L4codeS172;
    uint16_t* _M0L4dataS1206;
    int32_t _M0L3lenS1207;
    uint32_t _M0L6_2atmpS1210;
    uint32_t _M0L6_2atmpS1209;
    int32_t _M0L6_2atmpS1208;
    uint16_t* _M0L4dataS1211;
    int32_t _M0L3lenS1216;
    int32_t _M0L6_2atmpS1212;
    uint32_t _M0L6_2atmpS1215;
    uint32_t _M0L6_2atmpS1214;
    int32_t _M0L6_2atmpS1213;
    int32_t _M0L3lenS1218;
    int32_t _M0L6_2atmpS1217;
    if (_M0L6_2atmpS1200 < 2) {
      int32_t _M0L3lenS1205 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS1204 = _M0L3lenS1205 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS1204);
    }
    _M0L4codeS172 = _M0L4codeS169 - 65536u;
    _M0L4dataS1206 = _M0L4selfS171->$0;
    _M0L3lenS1207 = _M0L4selfS171->$1;
    _M0L6_2atmpS1210 = _M0L4codeS172 >> 10;
    _M0L6_2atmpS1209 = 55296u + _M0L6_2atmpS1210;
    moonbit_incref_cycle_free(_M0L4dataS1206);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1208 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1209);
    if (
      _M0L3lenS1207 < 0
      || _M0L3lenS1207 >= Moonbit_array_length(_M0L4dataS1206)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1206[_M0L3lenS1207] = _M0L6_2atmpS1208;
    moonbit_decref_cycle_free(_M0L4dataS1206);
    _M0L4dataS1211 = _M0L4selfS171->$0;
    _M0L3lenS1216 = _M0L4selfS171->$1;
    _M0L6_2atmpS1212 = _M0L3lenS1216 + 1;
    _M0L6_2atmpS1215 = _M0L4codeS172 & 1023u;
    _M0L6_2atmpS1214 = 56320u + _M0L6_2atmpS1215;
    moonbit_incref_cycle_free(_M0L4dataS1211);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS1213 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS1214);
    if (
      _M0L6_2atmpS1212 < 0
      || _M0L6_2atmpS1212 >= Moonbit_array_length(_M0L4dataS1211)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS1211[_M0L6_2atmpS1212] = _M0L6_2atmpS1213;
    moonbit_decref_cycle_free(_M0L4dataS1211);
    _M0L3lenS1218 = _M0L4selfS171->$1;
    _M0L6_2atmpS1217 = _M0L3lenS1218 + 2;
    _M0L4selfS171->$1 = _M0L6_2atmpS1217;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_27.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS166,
  int32_t _M0L8requiredS167
) {
  uint16_t* _M0L4dataS1189;
  int32_t _M0L6_2atmpS1187;
  int32_t _M0L3lenS1188;
  int32_t _M0L13new__capacityS165;
  uint16_t* _M0L4dataS1184;
  int32_t _M0L6_2atmpS1185;
  int32_t _M0L3lenS1186;
  uint16_t* _M0L9new__dataS168;
  uint16_t* _M0L6_2aoldS2192;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS1189 = _M0L4selfS166->$0;
  _M0L6_2atmpS1187 = Moonbit_array_length(_M0L4dataS1189);
  _M0L3lenS1188 = _M0L4selfS166->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS165
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS1187, _M0L3lenS1188, _M0L8requiredS167);
  _M0L4dataS1184 = _M0L4selfS166->$0;
  moonbit_incref_cycle_free(_M0L4dataS1184);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1185 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS1186 = _M0L4selfS166->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS168
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1184, _M0L13new__capacityS165, _M0L6_2atmpS1185, _M0L3lenS1186, 0, 0);
  _M0L6_2aoldS2192 = _M0L4selfS166->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS2192);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_28.data);
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
  int32_t _M0L6_2atmpS1183;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1183 = *(int32_t*)&_M0L4selfS158;
  return (uint16_t)_M0L6_2atmpS1183;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS157) {
  int32_t _M0L6_2atmpS1182;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1182 = _M0L4selfS157;
  return *(uint32_t*)&_M0L6_2atmpS1182;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS155
) {
  int32_t _M0L3lenS1173;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS1173 = _M0L4selfS155->$1;
  if (_M0L3lenS1173 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS1174 = _M0L4selfS155->$1;
    uint16_t* _M0L4dataS1176 = _M0L4selfS155->$0;
    int32_t _M0L6_2atmpS1175 = Moonbit_array_length(_M0L4dataS1176);
    if (_M0L3lenS1174 == _M0L6_2atmpS1175) {
      uint16_t* _M0L4dataS1177 = _M0L4selfS155->$0;
      moonbit_incref_cycle_free(_M0L4dataS1177);
      return _M0L4dataS1177;
    } else {
      uint16_t* _M0L4dataS1178 = _M0L4selfS155->$0;
      int32_t _M0L3lenS1179 = _M0L4selfS155->$1;
      int32_t _M0L6_2atmpS1180;
      int32_t _M0L3lenS1181;
      uint16_t* _M0L4dataS156;
      moonbit_incref_cycle_free(_M0L4dataS1178);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS1180 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS1181 = _M0L4selfS155->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS156
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS1178, _M0L3lenS1179, _M0L6_2atmpS1180, _M0L3lenS1181, 0, 0);
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
  int32_t _if__result_2315;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS148 >= 0) {
    if (_M0L3lenS149 >= 0) {
      if (_M0L11src__offsetS150 >= 0) {
        if (_M0L11dst__offsetS151 >= 0) {
          int32_t _M0L6_2atmpS1169 = _M0L11src__offsetS150 + _M0L3lenS149;
          int32_t _M0L6_2atmpS1170 = Moonbit_array_length(_M0L3srcS152);
          if (_M0L6_2atmpS1169 <= _M0L6_2atmpS1170) {
            int32_t _M0L6_2atmpS1168 = _M0L11dst__offsetS151 + _M0L3lenS149;
            _if__result_2315 = _M0L6_2atmpS1168 <= _M0L13allocate__lenS148;
          } else {
            _if__result_2315 = 0;
          }
        } else {
          _if__result_2315 = 0;
        }
      } else {
        _if__result_2315 = 0;
      }
    } else {
      _if__result_2315 = 0;
    }
  } else {
    _if__result_2315 = 0;
  }
  if (_if__result_2315) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS152, _M0L13allocate__lenS148, _M0L4initS153, _M0L11src__offsetS150, _M0L11dst__offsetS151, _M0L3lenS149);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS154;
    int32_t _M0L6_2atmpS1172;
    moonbit_string_t _M0L6_2atmpS1171;
    uint16_t* _result_2316;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS154
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L13allocate__lenS148);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11src__offsetS150);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11dst__offsetS151);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L3lenS149);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_33.data);
    _M0L6_2atmpS1172 = Moonbit_array_length(_M0L3srcS152);
    moonbit_decref_cycle_free(_M0L3srcS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L6_2atmpS1172);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS1171
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS154);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS154);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2316 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS1171);
    moonbit_decref_cycle_free(_M0L6_2atmpS1171);
    return _result_2316;
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
  struct _M0TPB13StringBuilder* _block_2317;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS139 < 1) {
    _M0L7initialS138 = 1;
  } else {
    int32_t _M0L6_2atmpS1167 = _M0L10size__hintS139 + 1;
    _M0L7initialS138 = _M0L6_2atmpS1167 / 2;
  }
  _M0L4dataS140 = (uint16_t*)moonbit_make_string(_M0L7initialS138, 0);
  _block_2317
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2317)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 85, 0);
  _block_2317->$0 = _M0L4dataS140;
  _block_2317->$1 = 0;
  return _block_2317;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS137) {
  int32_t _M0L6_2atmpS1166;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1166 = (int32_t)_M0L4selfS137;
  return _M0L6_2atmpS1166;
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS117,
  int32_t _M0L13allocate__lenS113,
  int32_t _M0L3lenS114,
  int32_t _M0L11src__offsetS115,
  int32_t _M0L11dst__offsetS116
) {
  int32_t _if__result_2318;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS113 >= 0) {
    if (_M0L3lenS114 >= 0) {
      if (_M0L11src__offsetS115 >= 0) {
        if (_M0L11dst__offsetS116 >= 0) {
          int32_t _M0L6_2atmpS1147 = _M0L11src__offsetS115 + _M0L3lenS114;
          int32_t _M0L6_2atmpS1148;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1148
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS117);
          if (_M0L6_2atmpS1147 <= _M0L6_2atmpS1148) {
            int32_t _M0L6_2atmpS1146 = _M0L11dst__offsetS116 + _M0L3lenS114;
            _if__result_2318 = _M0L6_2atmpS1146 <= _M0L13allocate__lenS113;
          } else {
            _if__result_2318 = 0;
          }
        } else {
          _if__result_2318 = 0;
        }
      } else {
        _if__result_2318 = 0;
      }
    } else {
      _if__result_2318 = 0;
    }
  } else {
    _if__result_2318 = 0;
  }
  if (_if__result_2318) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS117, _M0L13allocate__lenS113, _M0L11src__offsetS115, _M0L11dst__offsetS116, _M0L3lenS114);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS118;
    int32_t _M0L6_2atmpS1150;
    moonbit_string_t _M0L6_2atmpS1149;
    float* _result_2319;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS118
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L13allocate__lenS113);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11src__offsetS115);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11dst__offsetS116);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L3lenS114);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1150 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS117);
    moonbit_decref_cycle_free(_M0L3srcS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L6_2atmpS1150);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1149
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS118);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS118);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2319
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS1149);
    moonbit_decref_cycle_free(_M0L6_2atmpS1149);
    return _result_2319;
  }
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS123,
  int32_t _M0L13allocate__lenS119,
  int32_t _M0L3lenS120,
  int32_t _M0L11src__offsetS121,
  int32_t _M0L11dst__offsetS122
) {
  int32_t _if__result_2320;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS119 >= 0) {
    if (_M0L3lenS120 >= 0) {
      if (_M0L11src__offsetS121 >= 0) {
        if (_M0L11dst__offsetS122 >= 0) {
          int32_t _M0L6_2atmpS1152 = _M0L11src__offsetS121 + _M0L3lenS120;
          int32_t _M0L6_2atmpS1153;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1153
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS123);
          if (_M0L6_2atmpS1152 <= _M0L6_2atmpS1153) {
            int32_t _M0L6_2atmpS1151 = _M0L11dst__offsetS122 + _M0L3lenS120;
            _if__result_2320 = _M0L6_2atmpS1151 <= _M0L13allocate__lenS119;
          } else {
            _if__result_2320 = 0;
          }
        } else {
          _if__result_2320 = 0;
        }
      } else {
        _if__result_2320 = 0;
      }
    } else {
      _if__result_2320 = 0;
    }
  } else {
    _if__result_2320 = 0;
  }
  if (_if__result_2320) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS119, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS123, _M0L11src__offsetS121, _M0L11dst__offsetS122, _M0L3lenS120);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS124;
    int32_t _M0L6_2atmpS1155;
    moonbit_string_t _M0L6_2atmpS1154;
    moonbit_string_t* _result_2321;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS124
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L13allocate__lenS119);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11src__offsetS121);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11dst__offsetS122);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L3lenS120);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1155 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS123);
    moonbit_decref_cycle_free(_M0L3srcS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L6_2atmpS1155);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1154
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS124);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS124);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2321
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS1154);
    moonbit_decref_cycle_free(_M0L6_2atmpS1154);
    return _result_2321;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS129,
  int32_t _M0L13allocate__lenS125,
  int32_t _M0L3lenS126,
  int32_t _M0L11src__offsetS127,
  int32_t _M0L11dst__offsetS128
) {
  int32_t _if__result_2322;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS125 >= 0) {
    if (_M0L3lenS126 >= 0) {
      if (_M0L11src__offsetS127 >= 0) {
        if (_M0L11dst__offsetS128 >= 0) {
          int32_t _M0L6_2atmpS1157 = _M0L11src__offsetS127 + _M0L3lenS126;
          int32_t _M0L6_2atmpS1158;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1158
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS129);
          if (_M0L6_2atmpS1157 <= _M0L6_2atmpS1158) {
            int32_t _M0L6_2atmpS1156 = _M0L11dst__offsetS128 + _M0L3lenS126;
            _if__result_2322 = _M0L6_2atmpS1156 <= _M0L13allocate__lenS125;
          } else {
            _if__result_2322 = 0;
          }
        } else {
          _if__result_2322 = 0;
        }
      } else {
        _if__result_2322 = 0;
      }
    } else {
      _if__result_2322 = 0;
    }
  } else {
    _if__result_2322 = 0;
  }
  if (_if__result_2322) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS125, 0, _M0L3srcS129, _M0L11src__offsetS127, _M0L11dst__offsetS128, _M0L3lenS126);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS130;
    int32_t _M0L6_2atmpS1160;
    moonbit_string_t _M0L6_2atmpS1159;
    struct _M0TUsiE** _result_2323;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS130
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L13allocate__lenS125);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11src__offsetS127);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11dst__offsetS128);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L3lenS126);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1160 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS129);
    moonbit_decref_cycle_free(_M0L3srcS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L6_2atmpS1160);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1159
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS130);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS130);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2323
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS1159);
    moonbit_decref_cycle_free(_M0L6_2atmpS1159);
    return _result_2323;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS135,
  int32_t _M0L13allocate__lenS131,
  int32_t _M0L3lenS132,
  int32_t _M0L11src__offsetS133,
  int32_t _M0L11dst__offsetS134
) {
  int32_t _if__result_2324;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS131 >= 0) {
    if (_M0L3lenS132 >= 0) {
      if (_M0L11src__offsetS133 >= 0) {
        if (_M0L11dst__offsetS134 >= 0) {
          int32_t _M0L6_2atmpS1162 = _M0L11src__offsetS133 + _M0L3lenS132;
          int32_t _M0L6_2atmpS1163;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS1163
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS135);
          if (_M0L6_2atmpS1162 <= _M0L6_2atmpS1163) {
            int32_t _M0L6_2atmpS1161 = _M0L11dst__offsetS134 + _M0L3lenS132;
            _if__result_2324 = _M0L6_2atmpS1161 <= _M0L13allocate__lenS131;
          } else {
            _if__result_2324 = 0;
          }
        } else {
          _if__result_2324 = 0;
        }
      } else {
        _if__result_2324 = 0;
      }
    } else {
      _if__result_2324 = 0;
    }
  } else {
    _if__result_2324 = 0;
  }
  if (_if__result_2324) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS135, _M0L13allocate__lenS131, _M0L11src__offsetS133, _M0L11dst__offsetS134, _M0L3lenS132);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS136;
    int32_t _M0L6_2atmpS1165;
    moonbit_string_t _M0L6_2atmpS1164;
    int32_t* _result_2325;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS136
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L13allocate__lenS131);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11src__offsetS133);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11dst__offsetS134);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L3lenS132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_33.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1165 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS135);
    moonbit_decref_cycle_free(_M0L3srcS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L6_2atmpS1165);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS1164
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS136);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS136);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2325
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS1164);
    moonbit_decref_cycle_free(_M0L6_2atmpS1164);
    return _result_2325;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L3objS107
) {
  struct _M0TPB6Logger _M0L6_2atmpS1143;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS108);
  _M0L6_2atmpS1143
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS108
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS107, _M0L6_2atmpS1143);
  if (_M0L6_2atmpS1143.$1) {
    moonbit_decref(_M0L6_2atmpS1143.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS1144;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS1144
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS109, _M0L6_2atmpS1144);
  if (_M0L6_2atmpS1144.$1) {
    moonbit_decref(_M0L6_2atmpS1144.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  uint64_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS1145;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS1145
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS111, _M0L6_2atmpS1145);
  if (_M0L6_2atmpS1145.$1) {
    moonbit_decref(_M0L6_2atmpS1145.$1);
  }
  return 0;
}

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS86,
  int32_t _M0L13allocate__lenS84,
  int32_t _M0L11src__offsetS87,
  int32_t _M0L11dst__offsetS85,
  int32_t _M0L9blit__lenS88
) {
  float* _M0L3dstS83;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS83 = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS84);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS83, _M0L11dst__offsetS85, _M0L3srcS86, _M0L11src__offsetS87, _M0L9blit__lenS88);
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

int32_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(
  int32_t* _M0L3srcS104,
  int32_t _M0L13allocate__lenS102,
  int32_t _M0L11src__offsetS105,
  int32_t _M0L11dst__offsetS103,
  int32_t _M0L9blit__lenS106
) {
  int32_t* _M0L3dstS101;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS101
  = (int32_t*)moonbit_make_int32_array_raw(_M0L13allocate__lenS102);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGiE(_M0L3dstS101, _M0L11dst__offsetS103, _M0L3srcS104, _M0L11src__offsetS105, _M0L9blit__lenS106);
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS68,
  int32_t _M0L11dst__offsetS69,
  float* _M0L3srcS70,
  int32_t _M0L11src__offsetS71,
  int32_t _M0L3lenS72
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS70);
  moonbit_incref_cycle_free(_M0L3dstS68);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS68, _M0L11dst__offsetS69, _M0L3srcS70, _M0L11src__offsetS71, _M0L3lenS72, sizeof(float));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS73,
  int32_t _M0L11dst__offsetS74,
  moonbit_string_t* _M0L3srcS75,
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS78,
  int32_t _M0L11dst__offsetS79,
  struct _M0TUsiE** _M0L3srcS80,
  int32_t _M0L11src__offsetS81,
  int32_t _M0L3lenS82
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS80);
  moonbit_incref_cycle_free(_M0L3dstS78);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS78, _M0L11dst__offsetS79, _M0L3srcS80, _M0L11src__offsetS81, _M0L3lenS82);
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS18,
  int32_t _M0L11dst__offsetS20,
  int32_t* _M0L3srcS19,
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
        int32_t _M0L6_2atmpS1098 = _M0L11dst__offsetS20 + _M0L1iS22;
        int32_t _M0L6_2atmpS1100 = _M0L11src__offsetS21 + _M0L1iS22;
        int32_t _M0L6_2atmpS1099;
        int32_t _M0L6_2atmpS1101;
        if (
          _M0L6_2atmpS1100 < 0
          || _M0L6_2atmpS1100 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1099 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1100];
        if (
          _M0L6_2atmpS1098 < 0
          || _M0L6_2atmpS1098 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1098] = _M0L6_2atmpS1099;
        _M0L6_2atmpS1101 = _M0L1iS22 + 1;
        _M0L1iS22 = _M0L6_2atmpS1101;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1106 = _M0L3lenS23 - 1;
    int32_t _M0L1iS25 = _M0L6_2atmpS1106;
    while (1) {
      if (_M0L1iS25 >= 0) {
        int32_t _M0L6_2atmpS1102 = _M0L11dst__offsetS20 + _M0L1iS25;
        int32_t _M0L6_2atmpS1104 = _M0L11src__offsetS21 + _M0L1iS25;
        int32_t _M0L6_2atmpS1103;
        int32_t _M0L6_2atmpS1105;
        if (
          _M0L6_2atmpS1104 < 0
          || _M0L6_2atmpS1104 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1103 = (int32_t)_M0L3srcS19[_M0L6_2atmpS1104];
        if (
          _M0L6_2atmpS1102 < 0
          || _M0L6_2atmpS1102 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS1102] = _M0L6_2atmpS1103;
        _M0L6_2atmpS1105 = _M0L1iS25 - 1;
        _M0L1iS25 = _M0L6_2atmpS1105;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS27,
  int32_t _M0L11dst__offsetS29,
  float* _M0L3srcS28,
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
        int32_t _M0L6_2atmpS1107 = _M0L11dst__offsetS29 + _M0L1iS31;
        int32_t _M0L6_2atmpS1109 = _M0L11src__offsetS30 + _M0L1iS31;
        float _M0L6_2atmpS1108;
        int32_t _M0L6_2atmpS1110;
        if (
          _M0L6_2atmpS1109 < 0
          || _M0L6_2atmpS1109 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1108 = (float)_M0L3srcS28[_M0L6_2atmpS1109];
        if (
          _M0L6_2atmpS1107 < 0
          || _M0L6_2atmpS1107 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS1107] = _M0L6_2atmpS1108;
        _M0L6_2atmpS1110 = _M0L1iS31 + 1;
        _M0L1iS31 = _M0L6_2atmpS1110;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1115 = _M0L3lenS32 - 1;
    int32_t _M0L1iS34 = _M0L6_2atmpS1115;
    while (1) {
      if (_M0L1iS34 >= 0) {
        int32_t _M0L6_2atmpS1111 = _M0L11dst__offsetS29 + _M0L1iS34;
        int32_t _M0L6_2atmpS1113 = _M0L11src__offsetS30 + _M0L1iS34;
        float _M0L6_2atmpS1112;
        int32_t _M0L6_2atmpS1114;
        if (
          _M0L6_2atmpS1113 < 0
          || _M0L6_2atmpS1113 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1112 = (float)_M0L3srcS28[_M0L6_2atmpS1113];
        if (
          _M0L6_2atmpS1111 < 0
          || _M0L6_2atmpS1111 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS1111] = _M0L6_2atmpS1112;
        _M0L6_2atmpS1114 = _M0L1iS34 - 1;
        _M0L1iS34 = _M0L6_2atmpS1114;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t* _M0L3dstS36,
  int32_t _M0L11dst__offsetS38,
  uint16_t* _M0L3srcS37,
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
        int32_t _M0L6_2atmpS1116 = _M0L11dst__offsetS38 + _M0L1iS40;
        int32_t _M0L6_2atmpS1118 = _M0L11src__offsetS39 + _M0L1iS40;
        int32_t _M0L6_2atmpS1117;
        int32_t _M0L6_2atmpS1119;
        if (
          _M0L6_2atmpS1118 < 0
          || _M0L6_2atmpS1118 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1117 = (int32_t)_M0L3srcS37[_M0L6_2atmpS1118];
        if (
          _M0L6_2atmpS1116 < 0
          || _M0L6_2atmpS1116 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS36[_M0L6_2atmpS1116] = _M0L6_2atmpS1117;
        _M0L6_2atmpS1119 = _M0L1iS40 + 1;
        _M0L1iS40 = _M0L6_2atmpS1119;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1124 = _M0L3lenS41 - 1;
    int32_t _M0L1iS43 = _M0L6_2atmpS1124;
    while (1) {
      if (_M0L1iS43 >= 0) {
        int32_t _M0L6_2atmpS1120 = _M0L11dst__offsetS38 + _M0L1iS43;
        int32_t _M0L6_2atmpS1122 = _M0L11src__offsetS39 + _M0L1iS43;
        int32_t _M0L6_2atmpS1121;
        int32_t _M0L6_2atmpS1123;
        if (
          _M0L6_2atmpS1122 < 0
          || _M0L6_2atmpS1122 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1121 = (int32_t)_M0L3srcS37[_M0L6_2atmpS1122];
        if (
          _M0L6_2atmpS1120 < 0
          || _M0L6_2atmpS1120 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS36[_M0L6_2atmpS1120] = _M0L6_2atmpS1121;
        _M0L6_2atmpS1123 = _M0L1iS43 - 1;
        _M0L1iS43 = _M0L6_2atmpS1123;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS45,
  int32_t _M0L11dst__offsetS47,
  moonbit_string_t* _M0L3srcS46,
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
        int32_t _M0L6_2atmpS1125 = _M0L11dst__offsetS47 + _M0L1iS49;
        int32_t _M0L6_2atmpS1127 = _M0L11src__offsetS48 + _M0L1iS49;
        moonbit_string_t _M0L6_2atmpS1126;
        moonbit_string_t _M0L6_2aoldS2193;
        int32_t _M0L6_2atmpS1128;
        if (
          _M0L6_2atmpS1127 < 0
          || _M0L6_2atmpS1127 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1126 = (moonbit_string_t)_M0L3srcS46[_M0L6_2atmpS1127];
        if (
          _M0L6_2atmpS1125 < 0
          || _M0L6_2atmpS1125 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2193 = (moonbit_string_t)_M0L3dstS45[_M0L6_2atmpS1125];
        moonbit_incref_cycle_free(_M0L6_2atmpS1126);
        moonbit_decref_cycle_free(_M0L6_2aoldS2193);
        _M0L3dstS45[_M0L6_2atmpS1125] = _M0L6_2atmpS1126;
        _M0L6_2atmpS1128 = _M0L1iS49 + 1;
        _M0L1iS49 = _M0L6_2atmpS1128;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1133 = _M0L3lenS50 - 1;
    int32_t _M0L1iS52 = _M0L6_2atmpS1133;
    while (1) {
      if (_M0L1iS52 >= 0) {
        int32_t _M0L6_2atmpS1129 = _M0L11dst__offsetS47 + _M0L1iS52;
        int32_t _M0L6_2atmpS1131 = _M0L11src__offsetS48 + _M0L1iS52;
        moonbit_string_t _M0L6_2atmpS1130;
        moonbit_string_t _M0L6_2aoldS2194;
        int32_t _M0L6_2atmpS1132;
        if (
          _M0L6_2atmpS1131 < 0
          || _M0L6_2atmpS1131 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1130 = (moonbit_string_t)_M0L3srcS46[_M0L6_2atmpS1131];
        if (
          _M0L6_2atmpS1129 < 0
          || _M0L6_2atmpS1129 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2194 = (moonbit_string_t)_M0L3dstS45[_M0L6_2atmpS1129];
        moonbit_incref_cycle_free(_M0L6_2atmpS1130);
        moonbit_decref_cycle_free(_M0L6_2aoldS2194);
        _M0L3dstS45[_M0L6_2atmpS1129] = _M0L6_2atmpS1130;
        _M0L6_2atmpS1132 = _M0L1iS52 - 1;
        _M0L1iS52 = _M0L6_2atmpS1132;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS54,
  int32_t _M0L11dst__offsetS56,
  struct _M0TUsiE** _M0L3srcS55,
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
        int32_t _M0L6_2atmpS1134 = _M0L11dst__offsetS56 + _M0L1iS58;
        int32_t _M0L6_2atmpS1136 = _M0L11src__offsetS57 + _M0L1iS58;
        struct _M0TUsiE* _M0L6_2atmpS1135;
        struct _M0TUsiE* _M0L6_2aoldS2195;
        int32_t _M0L6_2atmpS1137;
        if (
          _M0L6_2atmpS1136 < 0
          || _M0L6_2atmpS1136 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1135 = (struct _M0TUsiE*)_M0L3srcS55[_M0L6_2atmpS1136];
        if (
          _M0L6_2atmpS1134 < 0
          || _M0L6_2atmpS1134 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2195 = (struct _M0TUsiE*)_M0L3dstS54[_M0L6_2atmpS1134];
        if (_M0L6_2atmpS1135) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1135);
        }
        if (_M0L6_2aoldS2195) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2195);
        }
        _M0L3dstS54[_M0L6_2atmpS1134] = _M0L6_2atmpS1135;
        _M0L6_2atmpS1137 = _M0L1iS58 + 1;
        _M0L1iS58 = _M0L6_2atmpS1137;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1142 = _M0L3lenS59 - 1;
    int32_t _M0L1iS61 = _M0L6_2atmpS1142;
    while (1) {
      if (_M0L1iS61 >= 0) {
        int32_t _M0L6_2atmpS1138 = _M0L11dst__offsetS56 + _M0L1iS61;
        int32_t _M0L6_2atmpS1140 = _M0L11src__offsetS57 + _M0L1iS61;
        struct _M0TUsiE* _M0L6_2atmpS1139;
        struct _M0TUsiE* _M0L6_2aoldS2196;
        int32_t _M0L6_2atmpS1141;
        if (
          _M0L6_2atmpS1140 < 0
          || _M0L6_2atmpS1140 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS1139 = (struct _M0TUsiE*)_M0L3srcS55[_M0L6_2atmpS1140];
        if (
          _M0L6_2atmpS1138 < 0
          || _M0L6_2atmpS1138 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS2196 = (struct _M0TUsiE*)_M0L3dstS54[_M0L6_2atmpS1138];
        if (_M0L6_2atmpS1139) {
          moonbit_incref_cycle_free(_M0L6_2atmpS1139);
        }
        if (_M0L6_2aoldS2196) {
          moonbit_decref_cycle_free(_M0L6_2aoldS2196);
        }
        _M0L3dstS54[_M0L6_2atmpS1138] = _M0L6_2atmpS1139;
        _M0L6_2atmpS1141 = _M0L1iS61 - 1;
        _M0L1iS61 = _M0L6_2atmpS1141;
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

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t* _M0L4selfS17) {
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
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_34.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S13, _M0L15_2a_2aarg__6389S12);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_35.data);
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

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(
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

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1070) {
  switch (Moonbit_object_tag(_M0L4_2aeS1070)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_36.data;
      break;
    }
    
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_37.data;
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_38.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1070);
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_39.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1093,
  struct _M0TPB4Show _M0L8_2aparamS1092
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1091 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1093;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS1091, _M0L8_2aparamS1092);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1090,
  struct _M0TPB4Show _M0L8_2aparamS1089
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1088 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1090;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS1088, _M0L8_2aparamS1089);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1087,
  int32_t _M0L8_2aparamS1086
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1085 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1087;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS1085, _M0L8_2aparamS1086);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1084,
  struct _M0TPC16string10StringView _M0L8_2aparamS1083
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1082 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1084;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS1082, _M0L8_2aparamS1083);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS1081,
  moonbit_string_t _M0L8_2aparamS1078,
  int32_t _M0L8_2aparamS1079,
  int32_t _M0L8_2aparamS1080
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1077 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1081;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS1077, _M0L8_2aparamS1078, _M0L8_2aparamS1079, _M0L8_2aparamS1080);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS1076,
  moonbit_string_t _M0L8_2aparamS1075
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS1074 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS1076;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS1074, _M0L8_2aparamS1075);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int32_t _tmp_2336 = -8388609;
  int32_t _tmp_2337;
  _M0FPC15float10min__value = *(float*)&_tmp_2336;
  _tmp_2337 = 2139095039;
  _M0FPC15float10max__value = *(float*)&_tmp_2337;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS1097;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1063;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1064;
  int32_t _M0L7_2abindS1065;
  struct _M0TUsiE** _M0L7_2abindS1066;
  int32_t _M0L6_2acntS2201;
  int32_t _M0L2__S1067;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS1097
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1063
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1063)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 88, 0);
  _M0L12async__testsS1063->$0 = _M0L6_2atmpS1097;
  _M0L12async__testsS1063->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1064
  = _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1065 = _M0L7_2abindS1064->$1;
  _M0L7_2abindS1066 = _M0L7_2abindS1064->$0;
  _M0L6_2acntS2201
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1064));
  if (_M0L6_2acntS2201 > 1) {
    int32_t _M0L11_2anew__cntS2202 = _M0L6_2acntS2201 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1064), _M0L11_2anew__cntS2202);
    moonbit_incref_cycle_free(_M0L7_2abindS1066);
  } else if (_M0L6_2acntS2201 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1064);
  }
  _M0L2__S1067 = 0;
  while (1) {
    if (_M0L2__S1067 < _M0L7_2abindS1065) {
      struct _M0TUsiE* _M0L3argS1068 =
        (struct _M0TUsiE*)_M0L7_2abindS1066[_M0L2__S1067];
      moonbit_string_t _M0L6_2atmpS1094 = _M0L3argS1068->$0;
      int32_t _M0L6_2atmpS1095 = _M0L3argS1068->$1;
      int32_t _M0L6_2atmpS1096;
      moonbit_incref_cycle_free(_M0L6_2atmpS1094);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1063, _M0L6_2atmpS1094, _M0L6_2atmpS1095);
      moonbit_decref_cycle_free(_M0L6_2atmpS1094);
      _M0L6_2atmpS1096 = _M0L2__S1067 + 1;
      _M0L2__S1067 = _M0L6_2atmpS1096;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1066);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples29stp__timestep__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1063);
  moonbit_decref_cycle_free(_M0L12async__testsS1063);
  moonbit_flush_cycles();
  return 0;
}