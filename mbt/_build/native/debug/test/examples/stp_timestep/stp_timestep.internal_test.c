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

struct _M0R116_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c375;

struct _M0TPB8MutLocalGiE;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples13stp__timestep33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TWRPC15error5ErrorEs;

struct _M0TPB4Show;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0DTPC15error5Error114RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0R117_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c380;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB6Logger;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0TPB6Logger;

struct _M0TPB5ArrayGUsiEE;

struct _M0TPB5ArrayGsE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TWEu;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0DTPC15error5Error116RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples13stp__timestep33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

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

struct _M0R116_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c375 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0TPB8MutLocalGiE {
  int32_t $0;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples13stp__timestep33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

struct _M0DTPC15error5Error114RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
};

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error {
  struct moonbit_result_0(* code)(
    struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error*,
    struct _M0TWuEu*,
    struct _M0TWRPC15error5ErrorEu*
  );
  
};

struct _M0R117_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c380 {
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

struct _M0TWEu {
  int32_t(* code)(struct _M0TWEu*);
  
};

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** $0;
  int32_t $1;
  
};

struct _M0DTPC15error5Error116RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
};

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples13stp__timestep33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples13stp__timestep34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples13stp__timestep34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples13stp__timestep34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples13stp__timestep52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples13stp__timestep52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS417(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples13stp__timestep52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S410(
  int32_t,
  moonbit_string_t
);

moonbit_string_t _M0MP46RiantR8snn__mbt8examples13stp__timestep33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples13stp__timestep52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

int32_t _M0FP46RiantR8snn__mbt8examples13stp__timestep44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples13stp__timestep44moonbit__test__driver__internal__do__executeN17error__to__stringS387(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples13stp__timestep44moonbit__test__driver__internal__do__executeN14handle__resultS380(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples13stp__timestep44moonbit__test__driver__internal__do__executeN13handle__startS375(
  struct _M0TWEu*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples13stp__timestep41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples13stp__timestep43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples13stp__timestep48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples13stp__timestep50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples13stp__timestep50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0FPB7printlnGsE(moonbit_string_t);

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t);

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
} const moonbit_string_literal_16 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_14 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_22 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_18 =
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
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[103]; 
} const moonbit_string_literal_29 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 102, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 115, 116, 112, 95, 116, 105, 109, 
    101, 115, 116, 101, 112, 46, 77, 111, 111, 110, 66, 105, 116, 84, 
    101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 
    110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 46, 77, 111, 111, 
    110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 
    73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 
    114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_13 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_10 =
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

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_11 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_23 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[105]; 
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 104, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 115, 116, 112, 95, 116, 105, 109, 
    101, 115, 116, 101, 112, 46, 77, 111, 111, 110, 66, 105, 116, 84, 
    101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 
    110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 46, 77, 111, 
    111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 
    114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 
    101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_20 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_17 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_26 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_12 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_15 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 73, 110, 115, 112, 101, 
    99, 116, 69, 114, 114, 111, 114, 46, 73, 110, 115, 112, 101, 99, 
    116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_24 =
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
} const moonbit_string_literal_21 =
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
} const moonbit_string_literal_9 =
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
} const moonbit_string_literal_25 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_19 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples13stp__timestep34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples13stp__timestep34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples13stp__timestep44moonbit__test__driver__internal__do__executeN17error__to__stringS387$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples13stp__timestep44moonbit__test__driver__internal__do__executeN17error__to__stringS387
  };

uint32_t const moonbit_layout_table_data[36] =
  {
    sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
    sizeof(struct _M0R116_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c375)
    / 4, 1,
    offsetof(struct _M0R116_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c375, $1)
    / 4
    * 2,
    sizeof(struct _M0R117_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c380)
    / 4, 1,
    offsetof(struct _M0R117_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c380, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error116RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error116RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples13stp__timestep34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples13stp__timestep34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples13stp__timestep34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS867
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples13stp__timestep34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples13stp__timestep34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples13stp__timestep34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S440
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples13stp__timestep52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S410;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS417;
  struct _M0TUsiE** _M0L6_2atmpS866;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS424;
  moonbit_string_t* _M0L9cli__argsS425;
  moonbit_string_t _M0L6_2atmpS865;
  moonbit_string_t _M0L6_2atmpS864;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS426;
  int32_t _M0L7_2abindS427;
  moonbit_string_t* _M0L7_2abindS428;
  int32_t _M0L6_2acntS881;
  int32_t _M0L2__S429;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S410 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS417 = 0;
  _M0L6_2atmpS866 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS424
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS424)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _M0L16file__and__indexS424->$0 = _M0L6_2atmpS866;
  _M0L16file__and__indexS424->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0L9cli__argsS425
  = _M0FP46RiantR8snn__mbt8examples13stp__timestep52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS425)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS865 = (moonbit_string_t)_M0L9cli__argsS425[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS865);
  moonbit_decref_cycle_free(_M0L9cli__argsS425);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0L6_2atmpS864
  = _M0MP46RiantR8snn__mbt8examples13stp__timestep33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS865);
  moonbit_decref_cycle_free(_M0L6_2atmpS865);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0L10test__argsS426
  = _M0FP46RiantR8snn__mbt8examples13stp__timestep52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS417(_M0L51moonbit__test__driver__internal__split__mbt__stringS417, _M0L6_2atmpS864, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS864);
  _M0L7_2abindS427 = _M0L10test__argsS426->$1;
  _M0L7_2abindS428 = _M0L10test__argsS426->$0;
  _M0L6_2acntS881
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS426));
  if (_M0L6_2acntS881 > 1) {
    int32_t _M0L11_2anew__cntS882 = _M0L6_2acntS881 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS426), _M0L11_2anew__cntS882);
    moonbit_incref_cycle_free(_M0L7_2abindS428);
  } else if (_M0L6_2acntS881 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
    moonbit_free(_M0L10test__argsS426);
  }
  _M0L2__S429 = 0;
  while (1) {
    if (_M0L2__S429 < _M0L7_2abindS427) {
      moonbit_string_t _M0L3argS430 =
        (moonbit_string_t)_M0L7_2abindS428[_M0L2__S429];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS431;
      moonbit_string_t _M0L4fileS432;
      moonbit_string_t _M0L5rangeS433;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS434;
      moonbit_string_t _M0L6_2atmpS862;
      int32_t _M0L5startS435;
      moonbit_string_t _M0L6_2atmpS861;
      int32_t _M0L3endS436;
      int32_t _M0L1iS437;
      int32_t _M0L6_2atmpS863;
      moonbit_incref_cycle_free(_M0L3argS430);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
      _M0L16file__and__rangeS431
      = _M0FP46RiantR8snn__mbt8examples13stp__timestep52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS417(_M0L51moonbit__test__driver__internal__split__mbt__stringS417, _M0L3argS430, 58);
      moonbit_decref_cycle_free(_M0L3argS430);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
      _M0L4fileS432
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS431, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
      _M0L5rangeS433
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS431, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS431);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
      _M0L15start__and__endS434
      = _M0FP46RiantR8snn__mbt8examples13stp__timestep52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS417(_M0L51moonbit__test__driver__internal__split__mbt__stringS417, _M0L5rangeS433, 45);
      moonbit_decref_cycle_free(_M0L5rangeS433);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
      _M0L6_2atmpS862
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS434, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
      _M0L5startS435
      = _M0FP46RiantR8snn__mbt8examples13stp__timestep52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S410(_M0L45moonbit__test__driver__internal__parse__int__S410, _M0L6_2atmpS862);
      moonbit_decref_cycle_free(_M0L6_2atmpS862);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
      _M0L6_2atmpS861
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS434, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS434);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
      _M0L3endS436
      = _M0FP46RiantR8snn__mbt8examples13stp__timestep52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S410(_M0L45moonbit__test__driver__internal__parse__int__S410, _M0L6_2atmpS861);
      moonbit_decref_cycle_free(_M0L6_2atmpS861);
      _M0L1iS437 = _M0L5startS435;
      while (1) {
        if (_M0L1iS437 < _M0L3endS436) {
          struct _M0TUsiE* _M0L8_2atupleS859;
          int32_t _M0L6_2atmpS860;
          moonbit_incref_cycle_free(_M0L4fileS432);
          _M0L8_2atupleS859
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS859)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
          _M0L8_2atupleS859->$0 = _M0L4fileS432;
          _M0L8_2atupleS859->$1 = _M0L1iS437;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS424, _M0L8_2atupleS859);
          _M0L6_2atmpS860 = _M0L1iS437 + 1;
          _M0L1iS437 = _M0L6_2atmpS860;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS432);
        }
        break;
      }
      _M0L6_2atmpS863 = _M0L2__S429 + 1;
      _M0L2__S429 = _M0L6_2atmpS863;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS428);
    }
    break;
  }
  return _M0L16file__and__indexS424;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples13stp__timestep52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS417(
  int32_t _M0L6_2aenvS840,
  moonbit_string_t _M0L1sS418,
  int32_t _M0L3sepS419
) {
  moonbit_string_t* _M0L6_2atmpS858;
  struct _M0TPB5ArrayGsE* _M0L3resS420;
  struct _M0TPB8MutLocalGiE* _M0L1iS421;
  struct _M0TPB8MutLocalGiE* _M0L5startS422;
  int32_t _M0L3valS853;
  int32_t _M0L6_2atmpS854;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0L6_2atmpS858 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS420
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS420)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 0);
  _M0L3resS420->$0 = _M0L6_2atmpS858;
  _M0L3resS420->$1 = 0;
  _M0L1iS421
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS421)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS421->$0 = 0;
  _M0L5startS422
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS422)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS422->$0 = 0;
  while (1) {
    int32_t _M0L3valS841 = _M0L1iS421->$0;
    int32_t _M0L6_2atmpS842 = Moonbit_array_length(_M0L1sS418);
    if (_M0L3valS841 < _M0L6_2atmpS842) {
      int32_t _M0L3valS845 = _M0L1iS421->$0;
      int32_t _M0L6_2atmpS844;
      int32_t _M0L6_2atmpS843;
      int32_t _M0L3valS852;
      int32_t _M0L6_2atmpS851;
      if (
        _M0L3valS845 < 0 || _M0L3valS845 >= Moonbit_array_length(_M0L1sS418)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS844 = _M0L1sS418[_M0L3valS845];
      _M0L6_2atmpS843 = _M0L6_2atmpS844;
      if (_M0L6_2atmpS843 == _M0L3sepS419) {
        int32_t _M0L3valS847 = _M0L5startS422->$0;
        int32_t _M0L3valS848 = _M0L1iS421->$0;
        moonbit_string_t _M0L6_2atmpS846;
        int32_t _M0L3valS850;
        int32_t _M0L6_2atmpS849;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
        _M0L6_2atmpS846
        = _M0MPC16string6String17unsafe__substring(_M0L1sS418, _M0L3valS847, _M0L3valS848);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS420, _M0L6_2atmpS846);
        _M0L3valS850 = _M0L1iS421->$0;
        _M0L6_2atmpS849 = _M0L3valS850 + 1;
        _M0L5startS422->$0 = _M0L6_2atmpS849;
      }
      _M0L3valS852 = _M0L1iS421->$0;
      _M0L6_2atmpS851 = _M0L3valS852 + 1;
      _M0L1iS421->$0 = _M0L6_2atmpS851;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS421);
    }
    break;
  }
  _M0L3valS853 = _M0L5startS422->$0;
  _M0L6_2atmpS854 = Moonbit_array_length(_M0L1sS418);
  if (_M0L3valS853 < _M0L6_2atmpS854) {
    int32_t _M0L3valS856 = _M0L5startS422->$0;
    int32_t _M0L6_2atmpS857;
    moonbit_string_t _M0L6_2atmpS855;
    moonbit_decref_cycle_free(_M0L5startS422);
    _M0L6_2atmpS857 = Moonbit_array_length(_M0L1sS418);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
    _M0L6_2atmpS855
    = _M0MPC16string6String17unsafe__substring(_M0L1sS418, _M0L3valS856, _M0L6_2atmpS857);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS420, _M0L6_2atmpS855);
  } else {
    moonbit_decref_cycle_free(_M0L5startS422);
  }
  return _M0L3resS420;
}

int32_t _M0FP46RiantR8snn__mbt8examples13stp__timestep52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S410(
  int32_t _M0L6_2aenvS833,
  moonbit_string_t _M0L1sS411
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS412;
  int32_t _M0L3lenS413;
  int32_t _M0L7_2abindS414;
  int32_t _M0L1iS415;
  int32_t _result_891;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0L3resS412
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS412)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS412->$0 = 0;
  _M0L3lenS413 = Moonbit_array_length(_M0L1sS411);
  _M0L7_2abindS414 = 0;
  _M0L1iS415 = _M0L7_2abindS414;
  while (1) {
    if (_M0L1iS415 < _M0L3lenS413) {
      int32_t _M0L3valS838 = _M0L3resS412->$0;
      int32_t _M0L6_2atmpS835 = _M0L3valS838 * 10;
      int32_t _M0L6_2atmpS837;
      int32_t _M0L6_2atmpS836;
      int32_t _M0L6_2atmpS834;
      int32_t _M0L6_2atmpS839;
      if (_M0L1iS415 < 0 || _M0L1iS415 >= Moonbit_array_length(_M0L1sS411)) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS837 = _M0L1sS411[_M0L1iS415];
      _M0L6_2atmpS836 = _M0L6_2atmpS837 - 48;
      _M0L6_2atmpS834 = _M0L6_2atmpS835 + _M0L6_2atmpS836;
      _M0L3resS412->$0 = _M0L6_2atmpS834;
      _M0L6_2atmpS839 = _M0L1iS415 + 1;
      _M0L1iS415 = _M0L6_2atmpS839;
      continue;
    }
    break;
  }
  _result_891 = _M0L3resS412->$0;
  moonbit_decref_cycle_free(_M0L3resS412);
  return _result_891;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples13stp__timestep33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS409
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS409);
  return _M0L4selfS409;
}

int32_t _M0FP46RiantR8snn__mbt8examples13stp__timestep44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS408,
  moonbit_string_t _M0L8filenameS377,
  int32_t _M0L5indexS379
) {
  struct _M0R116_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c375* _closure_892;
  struct _M0TWEu* _M0L13handle__startS375;
  struct _M0R117_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c380* _closure_893;
  struct _M0TWssbEu* _M0L14handle__resultS380;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS387;
  void* _M0L11_2atry__errS402;
  struct moonbit_result_0 _tmp_895;
  int32_t _handle__error__result_896;
  int32_t _M0L6_2atmpS821;
  void* _M0L3errS403;
  moonbit_string_t _M0L4nameS405;
  struct _M0DTPC15error5Error116RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS406;
  moonbit_string_t _M0L7_2anameS407;
  int32_t _M0L6_2acntS883;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS377);
  _closure_892
  = (struct _M0R116_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c375*)moonbit_malloc(sizeof(struct _M0R116_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c375));
  Moonbit_object_header(_closure_892)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _closure_892->code
  = &_M0FP46RiantR8snn__mbt8examples13stp__timestep44moonbit__test__driver__internal__do__executeN13handle__startS375;
  _closure_892->$0 = _M0L5indexS379;
  _closure_892->$1 = _M0L8filenameS377;
  _M0L13handle__startS375 = (struct _M0TWEu*)_closure_892;
  moonbit_incref_cycle_free(_M0L8filenameS377);
  _closure_893
  = (struct _M0R117_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c380*)moonbit_malloc(sizeof(struct _M0R117_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c380));
  Moonbit_object_header(_closure_893)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
  _closure_893->code
  = &_M0FP46RiantR8snn__mbt8examples13stp__timestep44moonbit__test__driver__internal__do__executeN14handle__resultS380;
  _closure_893->$0 = _M0L5indexS379;
  _closure_893->$1 = _M0L8filenameS377;
  _M0L14handle__resultS380 = (struct _M0TWssbEu*)_closure_893;
  _M0L17error__to__stringS387
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples13stp__timestep44moonbit__test__driver__internal__do__executeN17error__to__stringS387$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _tmp_895
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples13stp__timestep41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS408, _M0L8filenameS377, _M0L5indexS379, _M0L13handle__startS375, _M0L14handle__resultS380, _M0L17error__to__stringS387);
  if (_tmp_895.tag) {
    int32_t const _M0L5_2aokS830 = _tmp_895.data.ok;
    _handle__error__result_896 = _M0L5_2aokS830;
  } else {
    void* const _M0L6_2aerrS831 = _tmp_895.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS387);
    moonbit_decref_cycle_free(_M0L13handle__startS375);
    _M0L11_2atry__errS402 = _M0L6_2aerrS831;
    goto join_401;
  }
  if (_handle__error__result_896) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS387);
    moonbit_decref_cycle_free(_M0L13handle__startS375);
    _M0L6_2atmpS821 = 1;
  } else {
    struct moonbit_result_0 _tmp_897;
    int32_t _handle__error__result_898;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
    _tmp_897
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples13stp__timestep43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS408, _M0L8filenameS377, _M0L5indexS379, _M0L13handle__startS375, _M0L14handle__resultS380, _M0L17error__to__stringS387);
    if (_tmp_897.tag) {
      int32_t const _M0L5_2aokS828 = _tmp_897.data.ok;
      _handle__error__result_898 = _M0L5_2aokS828;
    } else {
      void* const _M0L6_2aerrS829 = _tmp_897.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS387);
      moonbit_decref_cycle_free(_M0L13handle__startS375);
      _M0L11_2atry__errS402 = _M0L6_2aerrS829;
      goto join_401;
    }
    if (_handle__error__result_898) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS387);
      moonbit_decref_cycle_free(_M0L13handle__startS375);
      _M0L6_2atmpS821 = 1;
    } else {
      struct moonbit_result_0 _tmp_899;
      int32_t _handle__error__result_900;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
      _tmp_899
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples13stp__timestep48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS408, _M0L8filenameS377, _M0L5indexS379, _M0L13handle__startS375, _M0L14handle__resultS380, _M0L17error__to__stringS387);
      if (_tmp_899.tag) {
        int32_t const _M0L5_2aokS826 = _tmp_899.data.ok;
        _handle__error__result_900 = _M0L5_2aokS826;
      } else {
        void* const _M0L6_2aerrS827 = _tmp_899.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS387);
        moonbit_decref_cycle_free(_M0L13handle__startS375);
        _M0L11_2atry__errS402 = _M0L6_2aerrS827;
        goto join_401;
      }
      if (_handle__error__result_900) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS387);
        moonbit_decref_cycle_free(_M0L13handle__startS375);
        _M0L6_2atmpS821 = 1;
      } else {
        struct moonbit_result_0 _tmp_901;
        int32_t _handle__error__result_902;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
        _tmp_901
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples13stp__timestep50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS408, _M0L8filenameS377, _M0L5indexS379, _M0L13handle__startS375, _M0L14handle__resultS380, _M0L17error__to__stringS387);
        if (_tmp_901.tag) {
          int32_t const _M0L5_2aokS824 = _tmp_901.data.ok;
          _handle__error__result_902 = _M0L5_2aokS824;
        } else {
          void* const _M0L6_2aerrS825 = _tmp_901.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS387);
          moonbit_decref_cycle_free(_M0L13handle__startS375);
          _M0L11_2atry__errS402 = _M0L6_2aerrS825;
          goto join_401;
        }
        if (_handle__error__result_902) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS387);
          moonbit_decref_cycle_free(_M0L13handle__startS375);
          _M0L6_2atmpS821 = 1;
        } else {
          struct moonbit_result_0 _tmp_903;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
          _tmp_903
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples13stp__timestep50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS408, _M0L8filenameS377, _M0L5indexS379, _M0L13handle__startS375, _M0L14handle__resultS380, _M0L17error__to__stringS387);
          moonbit_decref_cycle_free(_M0L13handle__startS375);
          moonbit_decref_cycle_free(_M0L17error__to__stringS387);
          if (_tmp_903.tag) {
            int32_t const _M0L5_2aokS822 = _tmp_903.data.ok;
            _M0L6_2atmpS821 = _M0L5_2aokS822;
          } else {
            void* const _M0L6_2aerrS823 = _tmp_903.data.err;
            _M0L11_2atry__errS402 = _M0L6_2aerrS823;
            goto join_401;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS821) {
    void* _M0L116RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS832 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error116RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L116RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS832)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 1);
    ((struct _M0DTPC15error5Error116RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L116RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS832)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS402
    = _M0L116RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS832;
    goto join_401;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS380);
  }
  goto joinlet_894;
  join_401:;
  _M0L3errS403 = _M0L11_2atry__errS402;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS406
  = (struct _M0DTPC15error5Error116RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS403;
  _M0L7_2anameS407 = _M0L36_2aMoonBitTestDriverInternalSkipTestS406->$0;
  _M0L6_2acntS883
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS406));
  if (_M0L6_2acntS883 > 1) {
    int32_t _M0L11_2anew__cntS884 = _M0L6_2acntS883 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS406), _M0L11_2anew__cntS884);
    moonbit_incref_cycle_free(_M0L7_2anameS407);
  } else if (_M0L6_2acntS883 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS406);
  }
  _M0L4nameS405 = _M0L7_2anameS407;
  goto join_404;
  goto joinlet_904;
  join_404:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0FP46RiantR8snn__mbt8examples13stp__timestep44moonbit__test__driver__internal__do__executeN14handle__resultS380(_M0L14handle__resultS380, _M0L4nameS405, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS380);
  moonbit_decref_cycle_free(_M0L4nameS405);
  joinlet_904:;
  joinlet_894:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples13stp__timestep44moonbit__test__driver__internal__do__executeN17error__to__stringS387(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS820,
  void* _M0L3errS388
) {
  void* _M0L1eS390;
  moonbit_string_t _M0L1eS392;
  moonbit_string_t _result_907;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS388)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS393 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS388;
      moonbit_string_t _M0L4_2aeS394 = _M0L10_2aFailureS393->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS394);
      _M0L1eS392 = _M0L4_2aeS394;
      goto join_391;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS395 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS388;
      moonbit_string_t _M0L4_2aeS396 = _M0L15_2aInspectErrorS395->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS396);
      _M0L1eS392 = _M0L4_2aeS396;
      goto join_391;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS397 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS388;
      moonbit_string_t _M0L4_2aeS398 = _M0L16_2aSnapshotErrorS397->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS398);
      _M0L1eS392 = _M0L4_2aeS398;
      goto join_391;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error114RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS399 =
        (struct _M0DTPC15error5Error114RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS388;
      moonbit_string_t _M0L4_2aeS400 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS399->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS400);
      _M0L1eS392 = _M0L4_2aeS400;
      goto join_391;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS388);
      _M0L1eS390 = _M0L3errS388;
      goto join_389;
      break;
    }
  }
  join_391:;
  return _M0L1eS392;
  join_389:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _result_907 = _M0FP15Error10to__string(_M0L1eS390);
  moonbit_decref_cycle_free(_M0L1eS390);
  return _result_907;
}

int32_t _M0FP46RiantR8snn__mbt8examples13stp__timestep44moonbit__test__driver__internal__do__executeN14handle__resultS380(
  struct _M0TWssbEu* _M0L6_2aenvS817,
  moonbit_string_t _M0L10__testnameS381,
  moonbit_string_t _M0L7messageS382,
  int32_t _M0L7skippedS383
) {
  struct _M0R117_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c380* _M0L14_2acasted__envS818;
  moonbit_string_t _M0L8filenameS377;
  int32_t _M0L5indexS379;
  moonbit_string_t _M0L10file__nameS384;
  moonbit_string_t _M0L7messageS385;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS386;
  moonbit_string_t _M0L6_2atmpS819;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0L14_2acasted__envS818
  = (struct _M0R117_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c380*)_M0L6_2aenvS817;
  _M0L8filenameS377 = _M0L14_2acasted__envS818->$1;
  _M0L5indexS379 = _M0L14_2acasted__envS818->$0;
  if (!_M0L7skippedS383 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0L10file__nameS384
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS377, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0L7messageS385
  = _M0MPC16string6String14escape_2einner(_M0L7messageS382, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0L18_2astring__builderS386
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS386, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS386, _M0L10file__nameS384);
  moonbit_decref_cycle_free(_M0L10file__nameS384);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS386, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS386, _M0L5indexS379);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS386, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS386, _M0L7messageS385);
  moonbit_decref_cycle_free(_M0L7messageS385);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS386, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0L6_2atmpS819
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS386);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS386);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS819);
  moonbit_decref_cycle_free(_M0L6_2atmpS819);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples13stp__timestep44moonbit__test__driver__internal__do__executeN13handle__startS375(
  struct _M0TWEu* _M0L6_2aenvS814
) {
  struct _M0R116_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c375* _M0L14_2acasted__envS815;
  moonbit_string_t _M0L8filenameS377;
  int32_t _M0L5indexS379;
  moonbit_string_t _M0L10file__nameS376;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS378;
  moonbit_string_t _M0L6_2atmpS816;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0L14_2acasted__envS815
  = (struct _M0R116_24RiantR_2fsnn__mbt_2fexamples_2fstp__timestep_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c375*)_M0L6_2aenvS814;
  _M0L8filenameS377 = _M0L14_2acasted__envS815->$1;
  _M0L5indexS379 = _M0L14_2acasted__envS815->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0L10file__nameS376
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS377, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0L18_2astring__builderS378
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS378, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS378, _M0L10file__nameS376);
  moonbit_decref_cycle_free(_M0L10file__nameS376);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS378, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS378, _M0L5indexS379);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS378, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0L6_2atmpS816
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS378);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS378);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS816);
  moonbit_decref_cycle_free(_M0L6_2atmpS816);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples13stp__timestep41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S345,
  moonbit_string_t _M0L12_2adiscard__S346,
  int32_t _M0L12_2adiscard__S347,
  struct _M0TWEu* _M0L12_2adiscard__S348,
  struct _M0TWssbEu* _M0L12_2adiscard__S349,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S350
) {
  struct moonbit_result_0 _result_908;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _result_908.tag = 1;
  _result_908.data.ok = 0;
  return _result_908;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples13stp__timestep43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S351,
  moonbit_string_t _M0L12_2adiscard__S352,
  int32_t _M0L12_2adiscard__S353,
  struct _M0TWEu* _M0L12_2adiscard__S354,
  struct _M0TWssbEu* _M0L12_2adiscard__S355,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S356
) {
  struct moonbit_result_0 _result_909;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _result_909.tag = 1;
  _result_909.data.ok = 0;
  return _result_909;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples13stp__timestep48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S357,
  moonbit_string_t _M0L12_2adiscard__S358,
  int32_t _M0L12_2adiscard__S359,
  struct _M0TWEu* _M0L12_2adiscard__S360,
  struct _M0TWssbEu* _M0L12_2adiscard__S361,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S362
) {
  struct moonbit_result_0 _result_910;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _result_910.tag = 1;
  _result_910.data.ok = 0;
  return _result_910;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples13stp__timestep50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S363,
  moonbit_string_t _M0L12_2adiscard__S364,
  int32_t _M0L12_2adiscard__S365,
  struct _M0TWEu* _M0L12_2adiscard__S366,
  struct _M0TWssbEu* _M0L12_2adiscard__S367,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S368
) {
  struct moonbit_result_0 _result_911;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _result_911.tag = 1;
  _result_911.data.ok = 0;
  return _result_911;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples13stp__timestep50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S369,
  moonbit_string_t _M0L12_2adiscard__S370,
  int32_t _M0L12_2adiscard__S371,
  struct _M0TWEu* _M0L12_2adiscard__S372,
  struct _M0TWssbEu* _M0L12_2adiscard__S373,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S374
) {
  struct moonbit_result_0 _result_912;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _result_912.tag = 1;
  _result_912.data.ok = 0;
  return _result_912;
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS343,
  int32_t _M0L5indexS344
) {
  int32_t _M0L3lenS342;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS342 = _M0L4selfS343->$1;
  if (_M0L5indexS344 >= 0 && _M0L5indexS344 < _M0L3lenS342) {
    moonbit_string_t* _M0L6_2atmpS813;
    moonbit_string_t _M0L6_2atmpS868;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS813 = _M0MPC15array5Array6bufferGsE(_M0L4selfS343);
    _M0L6_2atmpS868 = (moonbit_string_t)_M0L6_2atmpS813[_M0L5indexS344];
    moonbit_incref_cycle_free(_M0L6_2atmpS868);
    moonbit_decref_cycle_free(_M0L6_2atmpS813);
    return _M0L6_2atmpS868;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS341) {
  moonbit_string_t _M0L6_2atmpS812;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS812 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS341);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS812);
  moonbit_decref_cycle_free(_M0L6_2atmpS812);
  return 0;
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS340) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS340, 10);
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS334,
  moonbit_string_t _M0L5valueS336
) {
  int32_t _M0L3lenS798;
  moonbit_string_t* _M0L6_2atmpS800;
  int32_t _M0L6_2atmpS799;
  int32_t _M0L6lengthS335;
  moonbit_string_t* _M0L3bufS803;
  moonbit_string_t _M0L6_2aoldS869;
  int32_t _M0L6_2atmpS804;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS798 = _M0L4selfS334->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS800 = _M0MPC15array5Array6bufferGsE(_M0L4selfS334);
  _M0L6_2atmpS799 = Moonbit_array_length(_M0L6_2atmpS800);
  moonbit_decref_cycle_free(_M0L6_2atmpS800);
  if (_M0L3lenS798 == _M0L6_2atmpS799) {
    int32_t _M0L3lenS802 = _M0L4selfS334->$1;
    int32_t _M0L6_2atmpS801 = _M0L3lenS802 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS334, _M0L6_2atmpS801);
  }
  _M0L6lengthS335 = _M0L4selfS334->$1;
  _M0L3bufS803 = _M0L4selfS334->$0;
  _M0L6_2aoldS869 = (moonbit_string_t)_M0L3bufS803[_M0L6lengthS335];
  moonbit_decref_cycle_free(_M0L6_2aoldS869);
  _M0L3bufS803[_M0L6lengthS335] = _M0L5valueS336;
  _M0L6_2atmpS804 = _M0L6lengthS335 + 1;
  _M0L4selfS334->$1 = _M0L6_2atmpS804;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS337,
  struct _M0TUsiE* _M0L5valueS339
) {
  int32_t _M0L3lenS805;
  struct _M0TUsiE** _M0L6_2atmpS807;
  int32_t _M0L6_2atmpS806;
  int32_t _M0L6lengthS338;
  struct _M0TUsiE** _M0L3bufS810;
  struct _M0TUsiE* _M0L6_2aoldS870;
  int32_t _M0L6_2atmpS811;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS805 = _M0L4selfS337->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS807 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS337);
  _M0L6_2atmpS806 = Moonbit_array_length(_M0L6_2atmpS807);
  moonbit_decref_cycle_free(_M0L6_2atmpS807);
  if (_M0L3lenS805 == _M0L6_2atmpS806) {
    int32_t _M0L3lenS809 = _M0L4selfS337->$1;
    int32_t _M0L6_2atmpS808 = _M0L3lenS809 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS337, _M0L6_2atmpS808);
  }
  _M0L6lengthS338 = _M0L4selfS337->$1;
  _M0L3bufS810 = _M0L4selfS337->$0;
  _M0L6_2aoldS870 = (struct _M0TUsiE*)_M0L3bufS810[_M0L6lengthS338];
  if (_M0L6_2aoldS870) {
    moonbit_decref_cycle_free(_M0L6_2aoldS870);
  }
  _M0L3bufS810[_M0L6lengthS338] = _M0L5valueS339;
  _M0L6_2atmpS811 = _M0L6lengthS338 + 1;
  _M0L4selfS337->$1 = _M0L6_2atmpS811;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS327,
  int32_t _M0L8requiredS329
) {
  int32_t _M0L8old__capS326;
  int32_t _M0L3lenS796;
  int32_t _M0L8new__capS328;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS326 = _M0MPC15array5Array8capacityGsE(_M0L4selfS327);
  _M0L3lenS796 = _M0L4selfS327->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS328
  = _M0FPB23array__growth__capacity(_M0L8old__capS326, _M0L3lenS796, _M0L8requiredS329);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS327, _M0L8new__capS328);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS331,
  int32_t _M0L8requiredS333
) {
  int32_t _M0L8old__capS330;
  int32_t _M0L3lenS797;
  int32_t _M0L8new__capS332;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS330 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS331);
  _M0L3lenS797 = _M0L4selfS331->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS332
  = _M0FPB23array__growth__capacity(_M0L8old__capS330, _M0L3lenS797, _M0L8requiredS333);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS331, _M0L8new__capS332);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS315,
  int32_t _M0L13new__capacityS318
) {
  moonbit_string_t* _M0L8old__bufS314;
  int32_t _M0L3lenS316;
  int32_t _M0L9copy__lenS317;
  moonbit_string_t* _M0L8new__bufS319;
  moonbit_string_t* _M0L6_2aoldS871;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS314 = _M0L4selfS315->$0;
  _M0L3lenS316 = _M0L4selfS315->$1;
  if (_M0L3lenS316 < _M0L13new__capacityS318) {
    _M0L9copy__lenS317 = _M0L3lenS316;
  } else {
    _M0L9copy__lenS317 = _M0L13new__capacityS318;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS314);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS319
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS314, _M0L13new__capacityS318, _M0L9copy__lenS317, 0, 0);
  _M0L6_2aoldS871 = _M0L4selfS315->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS871);
  _M0L4selfS315->$0 = _M0L8new__bufS319;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS321,
  int32_t _M0L13new__capacityS324
) {
  struct _M0TUsiE** _M0L8old__bufS320;
  int32_t _M0L3lenS322;
  int32_t _M0L9copy__lenS323;
  struct _M0TUsiE** _M0L8new__bufS325;
  struct _M0TUsiE** _M0L6_2aoldS872;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS320 = _M0L4selfS321->$0;
  _M0L3lenS322 = _M0L4selfS321->$1;
  if (_M0L3lenS322 < _M0L13new__capacityS324) {
    _M0L9copy__lenS323 = _M0L3lenS322;
  } else {
    _M0L9copy__lenS323 = _M0L13new__capacityS324;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS320);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS325
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS320, _M0L13new__capacityS324, _M0L9copy__lenS323, 0, 0);
  _M0L6_2aoldS872 = _M0L4selfS321->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS872);
  _M0L4selfS321->$0 = _M0L8new__bufS325;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS312
) {
  moonbit_string_t* _M0L6_2atmpS794;
  int32_t _result_913;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS794 = _M0MPC15array5Array6bufferGsE(_M0L4selfS312);
  _result_913 = Moonbit_array_length(_M0L6_2atmpS794);
  moonbit_decref_cycle_free(_M0L6_2atmpS794);
  return _result_913;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS313
) {
  struct _M0TUsiE** _M0L6_2atmpS795;
  int32_t _result_914;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS795 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS313);
  _result_914 = Moonbit_array_length(_M0L6_2atmpS795);
  moonbit_decref_cycle_free(_M0L6_2atmpS795);
  return _result_914;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS308,
  int32_t _M0L3lenS306,
  int32_t _M0L8requiredS305
) {
  int32_t _M0L5startS307;
  int32_t _M0L5spaceS309;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS305 < _M0L3lenS306) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_9.data);
  }
  if (_M0L7currentS308 == 0) {
    _M0L5startS307 = 8;
  } else {
    _M0L5startS307 = _M0L7currentS308;
  }
  _M0L5spaceS309 = _M0L5startS307;
  while (1) {
    if (_M0L5spaceS309 < _M0L8requiredS305) {
      int32_t _M0L4nextS310 = _M0L5spaceS309 * 2;
      if (_M0L4nextS310 <= _M0L5spaceS309) {
        return _M0L8requiredS305;
      }
      _M0L5spaceS309 = _M0L4nextS310;
      continue;
    } else {
      return _M0L5spaceS309;
    }
    break;
  }
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS303
) {
  moonbit_string_t* _M0L8_2afieldS873;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS873 = _M0L4selfS303->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS873);
  return _M0L8_2afieldS873;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS304
) {
  struct _M0TUsiE** _M0L8_2afieldS874;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS874 = _M0L4selfS304->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS874);
  return _M0L8_2afieldS874;
}

moonbit_string_t _M0IPC16string6StringPB4Show10to__string(
  moonbit_string_t _M0L4selfS302
) {
  #line 220 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  moonbit_incref_cycle_free(_M0L4selfS302);
  return _M0L4selfS302;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__view(
  struct _M0TPB13StringBuilder* _M0L4selfS301,
  struct _M0TPC16string10StringView _M0L3strS299
) {
  int32_t _M0L3endS792;
  int32_t _M0L5startS793;
  int32_t _M0L8str__lenS298;
  int32_t _M0L3lenS791;
  int32_t _M0L8requiredS300;
  uint16_t* _M0L4dataS784;
  int32_t _M0L6_2atmpS783;
  int32_t _if__result_916;
  uint16_t* _M0L4dataS785;
  int32_t _M0L3lenS786;
  moonbit_string_t _M0L6_2atmpS787;
  int32_t _M0L6_2atmpS788;
  int32_t _M0L3lenS790;
  int32_t _M0L6_2atmpS789;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS792 = _M0L3strS299.$2;
  _M0L5startS793 = _M0L3strS299.$1;
  _M0L8str__lenS298 = _M0L3endS792 - _M0L5startS793;
  if (_M0L8str__lenS298 == 0) {
    return 0;
  }
  _M0L3lenS791 = _M0L4selfS301->$1;
  _M0L8requiredS300 = _M0L3lenS791 + _M0L8str__lenS298;
  _M0L4dataS784 = _M0L4selfS301->$0;
  _M0L6_2atmpS783 = Moonbit_array_length(_M0L4dataS784);
  if (_M0L8requiredS300 > _M0L6_2atmpS783) {
    _if__result_916 = 1;
  } else {
    int32_t _M0L3lenS782 = _M0L4selfS301->$1;
    _if__result_916 = _M0L8requiredS300 < _M0L3lenS782;
  }
  if (_if__result_916) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS301, _M0L8requiredS300);
  }
  _M0L4dataS785 = _M0L4selfS301->$0;
  _M0L3lenS786 = _M0L4selfS301->$1;
  moonbit_incref_cycle_free(_M0L4dataS785);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS787 = _M0MPC16string10StringView4data(_M0L3strS299);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS788 = _M0MPC16string10StringView13start__offset(_M0L3strS299);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS785, _M0L3lenS786, _M0L6_2atmpS787, _M0L6_2atmpS788, _M0L8str__lenS298);
  moonbit_decref_cycle_free(_M0L4dataS785);
  moonbit_decref_cycle_free(_M0L6_2atmpS787);
  _M0L3lenS790 = _M0L4selfS301->$1;
  _M0L6_2atmpS789 = _M0L3lenS790 + _M0L8str__lenS298;
  _M0L4selfS301->$1 = _M0L6_2atmpS789;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS295,
  int32_t _M0L5startS293,
  int32_t _M0L3endS294
) {
  int32_t _if__result_917;
  int32_t _M0L3lenS296;
  int32_t _M0L6_2atmpS781;
  moonbit_bytes_t _M0L5bytesS297;
  moonbit_bytes_t _M0L6_2atmpS780;
  moonbit_string_t _result_918;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS293 == 0) {
    int32_t _M0L6_2atmpS779 = Moonbit_array_length(_M0L3strS295);
    _if__result_917 = _M0L3endS294 == _M0L6_2atmpS779;
  } else {
    _if__result_917 = 0;
  }
  if (_if__result_917) {
    moonbit_incref_cycle_free(_M0L3strS295);
    return _M0L3strS295;
  }
  _M0L3lenS296 = _M0L3endS294 - _M0L5startS293;
  _M0L6_2atmpS781 = _M0L3lenS296 * 2;
  _M0L5bytesS297 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS781, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS297, 0, _M0L3strS295, _M0L5startS293, _M0L3lenS296);
  _M0L6_2atmpS780 = _M0L5bytesS297;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_918
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS780, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS780);
  return _result_918;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS288,
  int32_t _M0L6offsetS292,
  int64_t _M0L6lengthS290
) {
  int32_t _M0L3lenS287;
  int32_t _M0L6lengthS289;
  int32_t _if__result_919;
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L3lenS287 = Moonbit_array_length(_M0L4selfS288);
  if (_M0L6lengthS290 == 4294967296ll) {
    _M0L6lengthS289 = _M0L3lenS287 - _M0L6offsetS292;
  } else {
    int64_t _M0L7_2aSomeS291 = _M0L6lengthS290;
    _M0L6lengthS289 = (int32_t)_M0L7_2aSomeS291;
  }
  if (_M0L6offsetS292 >= 0) {
    if (_M0L6lengthS289 >= 0) {
      int32_t _M0L6_2atmpS778 = _M0L6offsetS292 + _M0L6lengthS289;
      _if__result_919 = _M0L6_2atmpS778 <= _M0L3lenS287;
    } else {
      _if__result_919 = 0;
    }
  } else {
    _if__result_919 = 0;
  }
  if (_if__result_919) {
    moonbit_incref_cycle_free(_M0L4selfS288);
    #line 85 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    return _M0FPB19unsafe__sub__string(_M0L4selfS288, _M0L6offsetS292, _M0L6lengthS289);
  } else {
    #line 84 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array10FixedArray18blit__from__string(
  moonbit_bytes_t _M0L4selfS279,
  int32_t _M0L13bytes__offsetS274,
  moonbit_string_t _M0L3strS281,
  int32_t _M0L11str__offsetS277,
  int32_t _M0L6lengthS275
) {
  int32_t _M0L6_2atmpS777;
  int32_t _M0L6_2atmpS776;
  int32_t _M0L2e1S273;
  int32_t _M0L6_2atmpS775;
  int32_t _M0L2e2S276;
  int32_t _M0L4len1S278;
  int32_t _M0L4len2S280;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS777 = _M0L6lengthS275 * 2;
  _M0L6_2atmpS776 = _M0L13bytes__offsetS274 + _M0L6_2atmpS777;
  _M0L2e1S273 = _M0L6_2atmpS776 - 1;
  _M0L6_2atmpS775 = _M0L11str__offsetS277 + _M0L6lengthS275;
  _M0L2e2S276 = _M0L6_2atmpS775 - 1;
  _M0L4len1S278 = Moonbit_array_length(_M0L4selfS279);
  _M0L4len2S280 = Moonbit_array_length(_M0L3strS281);
  if (
    _M0L6lengthS275 >= 0
    && _M0L13bytes__offsetS274 >= 0
    && _M0L2e1S273 < _M0L4len1S278
    && _M0L11str__offsetS277 >= 0
    && _M0L2e2S276 < _M0L4len2S280
  ) {
    int32_t _M0L16end__str__offsetS282 =
      _M0L11str__offsetS277 + _M0L6lengthS275;
    int32_t _M0L1iS283 = _M0L11str__offsetS277;
    int32_t _M0L1jS284 = _M0L13bytes__offsetS274;
    while (1) {
      if (_M0L1iS283 < _M0L16end__str__offsetS282) {
        int32_t _M0L6_2atmpS772 = _M0L3strS281[_M0L1iS283];
        int32_t _M0L6_2atmpS771 = (int32_t)_M0L6_2atmpS772;
        uint32_t _M0L1cS285 = *(uint32_t*)&_M0L6_2atmpS771;
        uint32_t _M0L6_2atmpS767 = _M0L1cS285 & 255u;
        int32_t _M0L6_2atmpS766;
        int32_t _M0L6_2atmpS768;
        uint32_t _M0L6_2atmpS770;
        int32_t _M0L6_2atmpS769;
        int32_t _M0L6_2atmpS773;
        int32_t _M0L6_2atmpS774;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS766 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS767);
        if (
          _M0L1jS284 < 0 || _M0L1jS284 >= Moonbit_array_length(_M0L4selfS279)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS279[_M0L1jS284] = _M0L6_2atmpS766;
        _M0L6_2atmpS768 = _M0L1jS284 + 1;
        _M0L6_2atmpS770 = _M0L1cS285 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS769 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS770);
        if (
          _M0L6_2atmpS768 < 0
          || _M0L6_2atmpS768 >= Moonbit_array_length(_M0L4selfS279)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS279[_M0L6_2atmpS768] = _M0L6_2atmpS769;
        _M0L6_2atmpS773 = _M0L1iS283 + 1;
        _M0L6_2atmpS774 = _M0L1jS284 + 2;
        _M0L1iS283 = _M0L6_2atmpS773;
        _M0L1jS284 = _M0L6_2atmpS774;
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

int32_t _M0MPC14uint4UInt8to__byte(uint32_t _M0L4selfS272) {
  int32_t _M0L6_2atmpS765;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS765 = *(int32_t*)&_M0L4selfS272;
  return _M0L6_2atmpS765 & 0xff;
}

moonbit_string_t _M0MPC13int3Int18to__string_2einner(
  int32_t _M0L4selfS256,
  int32_t _M0L5radixS255
) {
  int32_t _M0L12is__negativeS257;
  uint32_t _M0L3numS258;
  uint16_t* _M0L6bufferS259;
  #line 209 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS255 < 2 || _M0L5radixS255 > 36) {
    #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_10.data);
  }
  if (_M0L4selfS256 == 0) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  _M0L12is__negativeS257 = _M0L4selfS256 < 0;
  if (_M0L12is__negativeS257) {
    int32_t _M0L6_2atmpS764 = -_M0L4selfS256;
    _M0L3numS258 = *(uint32_t*)&_M0L6_2atmpS764;
  } else {
    _M0L3numS258 = *(uint32_t*)&_M0L4selfS256;
  }
  switch (_M0L5radixS255) {
    case 10: {
      int32_t _M0L10digit__lenS260;
      int32_t _M0L6_2atmpS761;
      int32_t _M0L10total__lenS261;
      uint16_t* _M0L6bufferS262;
      int32_t _M0L12digit__startS263;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS260 = _M0FPB12dec__count32(_M0L3numS258);
      if (_M0L12is__negativeS257) {
        _M0L6_2atmpS761 = 1;
      } else {
        _M0L6_2atmpS761 = 0;
      }
      _M0L10total__lenS261 = _M0L10digit__lenS260 + _M0L6_2atmpS761;
      _M0L6bufferS262
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS261, 0);
      if (_M0L12is__negativeS257) {
        _M0L12digit__startS263 = 1;
      } else {
        _M0L12digit__startS263 = 0;
      }
      #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__dec(_M0L6bufferS262, _M0L3numS258, _M0L12digit__startS263, _M0L10total__lenS261);
      _M0L6bufferS259 = _M0L6bufferS262;
      break;
    }
    
    case 16: {
      int32_t _M0L10digit__lenS264;
      int32_t _M0L6_2atmpS762;
      int32_t _M0L10total__lenS265;
      uint16_t* _M0L6bufferS266;
      int32_t _M0L12digit__startS267;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS264 = _M0FPB12hex__count32(_M0L3numS258);
      if (_M0L12is__negativeS257) {
        _M0L6_2atmpS762 = 1;
      } else {
        _M0L6_2atmpS762 = 0;
      }
      _M0L10total__lenS265 = _M0L10digit__lenS264 + _M0L6_2atmpS762;
      _M0L6bufferS266
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS265, 0);
      if (_M0L12is__negativeS257) {
        _M0L12digit__startS267 = 1;
      } else {
        _M0L12digit__startS267 = 0;
      }
      #line 247 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB20int__to__string__hex(_M0L6bufferS266, _M0L3numS258, _M0L12digit__startS267, _M0L10total__lenS265);
      _M0L6bufferS259 = _M0L6bufferS266;
      break;
    }
    default: {
      int32_t _M0L10digit__lenS268;
      int32_t _M0L6_2atmpS763;
      int32_t _M0L10total__lenS269;
      uint16_t* _M0L6bufferS270;
      int32_t _M0L12digit__startS271;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS268
      = _M0FPB14radix__count32(_M0L3numS258, _M0L5radixS255);
      if (_M0L12is__negativeS257) {
        _M0L6_2atmpS763 = 1;
      } else {
        _M0L6_2atmpS763 = 0;
      }
      _M0L10total__lenS269 = _M0L10digit__lenS268 + _M0L6_2atmpS763;
      _M0L6bufferS270
      = (uint16_t*)moonbit_make_string(_M0L10total__lenS269, 0);
      if (_M0L12is__negativeS257) {
        _M0L12digit__startS271 = 1;
      } else {
        _M0L12digit__startS271 = 0;
      }
      #line 255 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0FPB24int__to__string__generic(_M0L6bufferS270, _M0L3numS258, _M0L12digit__startS271, _M0L10total__lenS269, _M0L5radixS255);
      _M0L6bufferS259 = _M0L6bufferS270;
      break;
    }
  }
  if (_M0L12is__negativeS257) {
    _M0L6bufferS259[0] = 45;
  }
  return _M0L6bufferS259;
}

int32_t _M0FPB14radix__count32(
  uint32_t _M0L5valueS249,
  int32_t _M0L5radixS251
) {
  uint32_t _M0L4baseS250;
  uint32_t _M0L3numS252;
  int32_t _M0L5countS253;
  #line 189 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS249 == 0u) {
    return 1;
  }
  _M0L4baseS250 = *(uint32_t*)&_M0L5radixS251;
  _M0L3numS252 = _M0L5valueS249;
  _M0L5countS253 = 0;
  while (1) {
    if (_M0L3numS252 > 0u) {
      uint32_t _M0L6_2atmpS759 = _M0L3numS252 / _M0L4baseS250;
      int32_t _M0L6_2atmpS760 = _M0L5countS253 + 1;
      _M0L3numS252 = _M0L6_2atmpS759;
      _M0L5countS253 = _M0L6_2atmpS760;
      continue;
    } else {
      return _M0L5countS253;
    }
    break;
  }
}

int32_t _M0FPB12hex__count32(uint32_t _M0L5valueS247) {
  #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS247 == 0u) {
    return 1;
  } else {
    int32_t _M0L14leading__zerosS248;
    int32_t _M0L6_2atmpS758;
    int32_t _M0L6_2atmpS757;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS248 = moonbit_clz32(_M0L5valueS247);
    _M0L6_2atmpS758 = 31 - _M0L14leading__zerosS248;
    _M0L6_2atmpS757 = _M0L6_2atmpS758 / 4;
    return _M0L6_2atmpS757 + 1;
  }
}

int32_t _M0FPB12dec__count32(uint32_t _M0L5valueS246) {
  #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5valueS246 >= 100000u) {
    if (_M0L5valueS246 >= 10000000u) {
      if (_M0L5valueS246 >= 1000000000u) {
        return 10;
      } else if (_M0L5valueS246 >= 100000000u) {
        return 9;
      } else {
        return 8;
      }
    } else if (_M0L5valueS246 >= 1000000u) {
      return 7;
    } else {
      return 6;
    }
  } else if (_M0L5valueS246 >= 1000u) {
    if (_M0L5valueS246 >= 10000u) {
      return 5;
    } else {
      return 4;
    }
  } else if (_M0L5valueS246 >= 100u) {
    return 3;
  } else if (_M0L5valueS246 >= 10u) {
    return 2;
  } else {
    return 1;
  }
}

int32_t _M0FPB20int__to__string__dec(
  uint16_t* _M0L6bufferS232,
  uint32_t _M0L3numS244,
  int32_t _M0L12digit__startS233,
  int32_t _M0L10total__lenS245
) {
  int32_t _M0L6_2atmpS756;
  uint32_t _M0L3numS222;
  int32_t _M0L6offsetS223;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS756 = _M0L10total__lenS245 - _M0L12digit__startS233;
  _M0L3numS222 = _M0L3numS244;
  _M0L6offsetS223 = _M0L6_2atmpS756;
  while (1) {
    if (_M0L3numS222 >= 10000u) {
      uint32_t _M0L1tS224 = _M0L3numS222 / 10000u;
      uint32_t _M0L6_2atmpS733 = _M0L3numS222 % 10000u;
      int32_t _M0L1rS225 = *(int32_t*)&_M0L6_2atmpS733;
      int32_t _M0L2d1S226 = _M0L1rS225 / 100;
      int32_t _M0L2d2S227 = _M0L1rS225 % 100;
      int32_t _M0L6_2atmpS732 = _M0L2d1S226 / 10;
      int32_t _M0L6_2atmpS731 = 48 + _M0L6_2atmpS732;
      int32_t _M0L6d1__hiS228 = (uint16_t)_M0L6_2atmpS731;
      int32_t _M0L6_2atmpS730 = _M0L2d1S226 % 10;
      int32_t _M0L6_2atmpS729 = 48 + _M0L6_2atmpS730;
      int32_t _M0L6d1__loS229 = (uint16_t)_M0L6_2atmpS729;
      int32_t _M0L6_2atmpS728 = _M0L2d2S227 / 10;
      int32_t _M0L6_2atmpS727 = 48 + _M0L6_2atmpS728;
      int32_t _M0L6d2__hiS230 = (uint16_t)_M0L6_2atmpS727;
      int32_t _M0L6_2atmpS726 = _M0L2d2S227 % 10;
      int32_t _M0L6_2atmpS725 = 48 + _M0L6_2atmpS726;
      int32_t _M0L6d2__loS231 = (uint16_t)_M0L6_2atmpS725;
      int32_t _M0L6_2atmpS717 = _M0L12digit__startS233 + _M0L6offsetS223;
      int32_t _M0L6_2atmpS716 = _M0L6_2atmpS717 - 4;
      int32_t _M0L6_2atmpS719;
      int32_t _M0L6_2atmpS718;
      int32_t _M0L6_2atmpS721;
      int32_t _M0L6_2atmpS720;
      int32_t _M0L6_2atmpS723;
      int32_t _M0L6_2atmpS722;
      int32_t _M0L6_2atmpS724;
      _M0L6bufferS232[_M0L6_2atmpS716] = _M0L6d1__hiS228;
      _M0L6_2atmpS719 = _M0L12digit__startS233 + _M0L6offsetS223;
      _M0L6_2atmpS718 = _M0L6_2atmpS719 - 3;
      _M0L6bufferS232[_M0L6_2atmpS718] = _M0L6d1__loS229;
      _M0L6_2atmpS721 = _M0L12digit__startS233 + _M0L6offsetS223;
      _M0L6_2atmpS720 = _M0L6_2atmpS721 - 2;
      _M0L6bufferS232[_M0L6_2atmpS720] = _M0L6d2__hiS230;
      _M0L6_2atmpS723 = _M0L12digit__startS233 + _M0L6offsetS223;
      _M0L6_2atmpS722 = _M0L6_2atmpS723 - 1;
      _M0L6bufferS232[_M0L6_2atmpS722] = _M0L6d2__loS231;
      _M0L6_2atmpS724 = _M0L6offsetS223 - 4;
      _M0L3numS222 = _M0L1tS224;
      _M0L6offsetS223 = _M0L6_2atmpS724;
      continue;
    } else {
      int32_t _M0L6_2atmpS755 = *(int32_t*)&_M0L3numS222;
      int32_t _M0L9remainingS235 = _M0L6_2atmpS755;
      int32_t _M0L6offsetS236 = _M0L6offsetS223;
      while (1) {
        if (_M0L9remainingS235 >= 100) {
          int32_t _M0L1tS237 = _M0L9remainingS235 / 100;
          int32_t _M0L1dS238 = _M0L9remainingS235 % 100;
          int32_t _M0L6_2atmpS742 = _M0L1dS238 / 10;
          int32_t _M0L6_2atmpS741 = 48 + _M0L6_2atmpS742;
          int32_t _M0L5d__hiS239 = (uint16_t)_M0L6_2atmpS741;
          int32_t _M0L6_2atmpS740 = _M0L1dS238 % 10;
          int32_t _M0L6_2atmpS739 = 48 + _M0L6_2atmpS740;
          int32_t _M0L5d__loS240 = (uint16_t)_M0L6_2atmpS739;
          int32_t _M0L6_2atmpS735 = _M0L12digit__startS233 + _M0L6offsetS236;
          int32_t _M0L6_2atmpS734 = _M0L6_2atmpS735 - 2;
          int32_t _M0L6_2atmpS737;
          int32_t _M0L6_2atmpS736;
          int32_t _M0L6_2atmpS738;
          _M0L6bufferS232[_M0L6_2atmpS734] = _M0L5d__hiS239;
          _M0L6_2atmpS737 = _M0L12digit__startS233 + _M0L6offsetS236;
          _M0L6_2atmpS736 = _M0L6_2atmpS737 - 1;
          _M0L6bufferS232[_M0L6_2atmpS736] = _M0L5d__loS240;
          _M0L6_2atmpS738 = _M0L6offsetS236 - 2;
          _M0L9remainingS235 = _M0L1tS237;
          _M0L6offsetS236 = _M0L6_2atmpS738;
          continue;
        } else if (_M0L9remainingS235 >= 10) {
          int32_t _M0L6_2atmpS750 = _M0L9remainingS235 / 10;
          int32_t _M0L6_2atmpS749 = 48 + _M0L6_2atmpS750;
          int32_t _M0L5d__hiS242 = (uint16_t)_M0L6_2atmpS749;
          int32_t _M0L6_2atmpS748 = _M0L9remainingS235 % 10;
          int32_t _M0L6_2atmpS747 = 48 + _M0L6_2atmpS748;
          int32_t _M0L5d__loS243 = (uint16_t)_M0L6_2atmpS747;
          int32_t _M0L6_2atmpS744 = _M0L12digit__startS233 + _M0L6offsetS236;
          int32_t _M0L6_2atmpS743 = _M0L6_2atmpS744 - 2;
          int32_t _M0L6_2atmpS746;
          int32_t _M0L6_2atmpS745;
          _M0L6bufferS232[_M0L6_2atmpS743] = _M0L5d__hiS242;
          _M0L6_2atmpS746 = _M0L12digit__startS233 + _M0L6offsetS236;
          _M0L6_2atmpS745 = _M0L6_2atmpS746 - 1;
          _M0L6bufferS232[_M0L6_2atmpS745] = _M0L5d__loS243;
        } else {
          int32_t _M0L6_2atmpS754 = _M0L12digit__startS233 + _M0L6offsetS236;
          int32_t _M0L6_2atmpS751 = _M0L6_2atmpS754 - 1;
          int32_t _M0L6_2atmpS753 = 48 + _M0L9remainingS235;
          int32_t _M0L6_2atmpS752 = (uint16_t)_M0L6_2atmpS753;
          _M0L6bufferS232[_M0L6_2atmpS751] = _M0L6_2atmpS752;
        }
        break;
      }
    }
    break;
  }
  return 0;
}

int32_t _M0FPB24int__to__string__generic(
  uint16_t* _M0L6bufferS212,
  uint32_t _M0L3numS216,
  int32_t _M0L12digit__startS213,
  int32_t _M0L10total__lenS215,
  int32_t _M0L5radixS206
) {
  uint32_t _M0L4baseS205;
  int32_t _M0L6_2atmpS701;
  int32_t _M0L6_2atmpS700;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS205 = *(uint32_t*)&_M0L5radixS206;
  _M0L6_2atmpS701 = _M0L5radixS206 - 1;
  _M0L6_2atmpS700 = _M0L5radixS206 & _M0L6_2atmpS701;
  if (_M0L6_2atmpS700 == 0) {
    int32_t _M0L5shiftS207;
    uint32_t _M0L4maskS208;
    int32_t _M0L6_2atmpS708;
    int32_t _M0L6offsetS209;
    uint32_t _M0L1nS210;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS207 = moonbit_ctz32(_M0L5radixS206);
    _M0L4maskS208 = _M0L4baseS205 - 1u;
    _M0L6_2atmpS708 = _M0L10total__lenS215 - _M0L12digit__startS213;
    _M0L6offsetS209 = _M0L6_2atmpS708;
    _M0L1nS210 = _M0L3numS216;
    while (1) {
      if (_M0L1nS210 > 0u) {
        uint32_t _M0L6_2atmpS707 = _M0L1nS210 & _M0L4maskS208;
        int32_t _M0L5digitS211 = *(int32_t*)&_M0L6_2atmpS707;
        int32_t _M0L6_2atmpS704 = _M0L12digit__startS213 + _M0L6offsetS209;
        int32_t _M0L6_2atmpS702 = _M0L6_2atmpS704 - 1;
        int32_t _M0L6_2atmpS703 =
          ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L5digitS211];
        int32_t _M0L6_2atmpS705;
        uint32_t _M0L6_2atmpS706;
        _M0L6bufferS212[_M0L6_2atmpS702] = _M0L6_2atmpS703;
        _M0L6_2atmpS705 = _M0L6offsetS209 - 1;
        _M0L6_2atmpS706 = _M0L1nS210 >> (_M0L5shiftS207 & 31);
        _M0L6offsetS209 = _M0L6_2atmpS705;
        _M0L1nS210 = _M0L6_2atmpS706;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS715 = _M0L10total__lenS215 - _M0L12digit__startS213;
    int32_t _M0L6offsetS217 = _M0L6_2atmpS715;
    uint32_t _M0L1nS218 = _M0L3numS216;
    while (1) {
      if (_M0L1nS218 > 0u) {
        uint32_t _M0L1qS219 = _M0L1nS218 / _M0L4baseS205;
        uint32_t _M0L6_2atmpS714 = _M0L1qS219 * _M0L4baseS205;
        uint32_t _M0L6_2atmpS713 = _M0L1nS218 - _M0L6_2atmpS714;
        int32_t _M0L5digitS220 = *(int32_t*)&_M0L6_2atmpS713;
        int32_t _M0L6_2atmpS711 = _M0L12digit__startS213 + _M0L6offsetS217;
        int32_t _M0L6_2atmpS709 = _M0L6_2atmpS711 - 1;
        int32_t _M0L6_2atmpS710 =
          ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L5digitS220];
        int32_t _M0L6_2atmpS712;
        _M0L6bufferS212[_M0L6_2atmpS709] = _M0L6_2atmpS710;
        _M0L6_2atmpS712 = _M0L6offsetS217 - 1;
        _M0L6offsetS217 = _M0L6_2atmpS712;
        _M0L1nS218 = _M0L1qS219;
        continue;
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FPB20int__to__string__hex(
  uint16_t* _M0L6bufferS199,
  uint32_t _M0L3numS204,
  int32_t _M0L12digit__startS200,
  int32_t _M0L10total__lenS203
) {
  int32_t _M0L6_2atmpS699;
  int32_t _M0L6offsetS194;
  uint32_t _M0L1nS195;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS699 = _M0L10total__lenS203 - _M0L12digit__startS200;
  _M0L6offsetS194 = _M0L6_2atmpS699;
  _M0L1nS195 = _M0L3numS204;
  while (1) {
    if (_M0L6offsetS194 >= 2) {
      uint32_t _M0L6_2atmpS696 = _M0L1nS195 & 255u;
      int32_t _M0L9byte__valS196 = *(int32_t*)&_M0L6_2atmpS696;
      int32_t _M0L2hiS197 = _M0L9byte__valS196 / 16;
      int32_t _M0L2loS198 = _M0L9byte__valS196 % 16;
      int32_t _M0L6_2atmpS690 = _M0L12digit__startS200 + _M0L6offsetS194;
      int32_t _M0L6_2atmpS688 = _M0L6_2atmpS690 - 2;
      int32_t _M0L6_2atmpS689 =
        ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L2hiS197];
      int32_t _M0L6_2atmpS693;
      int32_t _M0L6_2atmpS691;
      int32_t _M0L6_2atmpS692;
      int32_t _M0L6_2atmpS694;
      uint32_t _M0L6_2atmpS695;
      _M0L6bufferS199[_M0L6_2atmpS688] = _M0L6_2atmpS689;
      _M0L6_2atmpS693 = _M0L12digit__startS200 + _M0L6offsetS194;
      _M0L6_2atmpS691 = _M0L6_2atmpS693 - 1;
      _M0L6_2atmpS692
      = ((moonbit_string_t)moonbit_string_literal_12.data)[
        _M0L2loS198
      ];
      _M0L6bufferS199[_M0L6_2atmpS691] = _M0L6_2atmpS692;
      _M0L6_2atmpS694 = _M0L6offsetS194 - 2;
      _M0L6_2atmpS695 = _M0L1nS195 >> 8;
      _M0L6offsetS194 = _M0L6_2atmpS694;
      _M0L1nS195 = _M0L6_2atmpS695;
      continue;
    } else if (_M0L6offsetS194 == 1) {
      uint32_t _M0L6_2atmpS698 = _M0L1nS195 & 15u;
      int32_t _M0L6nibbleS202 = *(int32_t*)&_M0L6_2atmpS698;
      int32_t _M0L6_2atmpS697 =
        ((moonbit_string_t)moonbit_string_literal_12.data)[_M0L6nibbleS202];
      _M0L6bufferS199[_M0L12digit__startS200] = _M0L6_2atmpS697;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS193
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS192;
  struct _M0TPB6Logger _M0L6_2atmpS687;
  moonbit_string_t _result_927;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS192 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS192);
  _M0L6_2atmpS687
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS192
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS193, _M0L6_2atmpS687);
  if (_M0L6_2atmpS687.$1) {
    moonbit_decref(_M0L6_2atmpS687.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_927 = _M0MPB13StringBuilder10to__string(_M0L6loggerS192);
  moonbit_decref_cycle_free(_M0L6loggerS192);
  return _result_927;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS189,
  struct _M0TPB6Logger _M0L6loggerS188
) {
  moonbit_string_t _M0L6_2atmpS685;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS685 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS189);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS188.$0->$method_0(_M0L6loggerS188.$1, _M0L6_2atmpS685);
  moonbit_decref_cycle_free(_M0L6_2atmpS685);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS191,
  struct _M0TPB6Logger _M0L6loggerS190
) {
  moonbit_string_t _M0L6_2atmpS686;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS686 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS191);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS190.$0->$method_0(_M0L6loggerS190.$1, _M0L6_2atmpS686);
  moonbit_decref_cycle_free(_M0L6_2atmpS686);
  return 0;
}

int32_t _M0MPC16string10StringView13start__offset(
  struct _M0TPC16string10StringView _M0L4selfS187
) {
  #line 99 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  return _M0L4selfS187.$1;
}

moonbit_string_t _M0MPC16string10StringView4data(
  struct _M0TPC16string10StringView _M0L4selfS186
) {
  moonbit_string_t _M0L8_2afieldS875;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS875 = _M0L4selfS186.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS875);
  return _M0L8_2afieldS875;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS182,
  moonbit_string_t _M0L5valueS183,
  int32_t _M0L5startS184,
  int32_t _M0L3lenS185
) {
  int32_t _M0L6_2atmpS684;
  int64_t _M0L6_2atmpS683;
  struct _M0TPC16string10StringView _M0L6_2atmpS682;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS684 = _M0L5startS184 + _M0L3lenS185;
  _M0L6_2atmpS683 = (int64_t)_M0L6_2atmpS684;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS682
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS183, _M0L5startS184, _M0L6_2atmpS683);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS182, _M0L6_2atmpS682);
  moonbit_decref_cycle_free(_M0L6_2atmpS682.$0);
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string6String21clamped__view_2einner(
  moonbit_string_t _M0L4selfS175,
  int32_t _M0L5startS177,
  int64_t _M0L3endS179
) {
  int32_t _M0L3lenS174;
  int32_t _M0Lm2loS176;
  int32_t _M0Lm2hiS178;
  int32_t _M0L6_2atmpS666;
  int32_t _if__result_928;
  int32_t _M0L6_2atmpS674;
  int32_t _if__result_929;
  int32_t _M0L6_2atmpS676;
  int32_t _M0L6_2atmpS677;
  #line 698 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3lenS174 = Moonbit_array_length(_M0L4selfS175);
  if (_M0L5startS177 < 0) {
    _M0Lm2loS176 = 0;
  } else if (_M0L5startS177 > _M0L3lenS174) {
    _M0Lm2loS176 = _M0L3lenS174;
  } else {
    _M0Lm2loS176 = _M0L5startS177;
  }
  if (_M0L3endS179 == 4294967296ll) {
    _M0Lm2hiS178 = _M0L3lenS174;
  } else {
    int64_t _M0L7_2aSomeS180 = _M0L3endS179;
    int32_t _M0L4_2aeS181 = (int32_t)_M0L7_2aSomeS180;
    if (_M0L4_2aeS181 < 0) {
      _M0Lm2hiS178 = 0;
    } else if (_M0L4_2aeS181 > _M0L3lenS174) {
      _M0Lm2hiS178 = _M0L3lenS174;
    } else {
      _M0Lm2hiS178 = _M0L4_2aeS181;
    }
  }
  _M0L6_2atmpS666 = _M0Lm2loS176;
  if (_M0L6_2atmpS666 > 0) {
    int32_t _M0L6_2atmpS665 = _M0Lm2loS176;
    if (_M0L6_2atmpS665 < _M0L3lenS174) {
      int32_t _M0L6_2atmpS664 = _M0Lm2loS176;
      int32_t _M0L6_2atmpS663 = _M0L4selfS175[_M0L6_2atmpS664];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS663)) {
        int32_t _M0L6_2atmpS662 = _M0Lm2loS176;
        int32_t _M0L6_2atmpS661 = _M0L6_2atmpS662 - 1;
        int32_t _M0L6_2atmpS660 = _M0L4selfS175[_M0L6_2atmpS661];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_928
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS660);
      } else {
        _if__result_928 = 0;
      }
    } else {
      _if__result_928 = 0;
    }
  } else {
    _if__result_928 = 0;
  }
  if (_if__result_928) {
    int32_t _M0L6_2atmpS667 = _M0Lm2loS176;
    _M0Lm2loS176 = _M0L6_2atmpS667 + 1;
  }
  _M0L6_2atmpS674 = _M0Lm2hiS178;
  if (_M0L6_2atmpS674 > 0) {
    int32_t _M0L6_2atmpS673 = _M0Lm2hiS178;
    if (_M0L6_2atmpS673 < _M0L3lenS174) {
      int32_t _M0L6_2atmpS672 = _M0Lm2hiS178;
      int32_t _M0L6_2atmpS671 = _M0L4selfS175[_M0L6_2atmpS672];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS671)) {
        int32_t _M0L6_2atmpS670 = _M0Lm2hiS178;
        int32_t _M0L6_2atmpS669 = _M0L6_2atmpS670 - 1;
        int32_t _M0L6_2atmpS668 = _M0L4selfS175[_M0L6_2atmpS669];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_929
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS668);
      } else {
        _if__result_929 = 0;
      }
    } else {
      _if__result_929 = 0;
    }
  } else {
    _if__result_929 = 0;
  }
  if (_if__result_929) {
    int32_t _M0L6_2atmpS675 = _M0Lm2hiS178;
    _M0Lm2hiS178 = _M0L6_2atmpS675 - 1;
  }
  _M0L6_2atmpS676 = _M0Lm2loS176;
  _M0L6_2atmpS677 = _M0Lm2hiS178;
  if (_M0L6_2atmpS676 >= _M0L6_2atmpS677) {
    int32_t _M0L6_2atmpS678 = _M0Lm2loS176;
    int32_t _M0L6_2atmpS679 = _M0Lm2loS176;
    moonbit_incref_cycle_free(_M0L4selfS175);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS175,
                                                 .$1 = _M0L6_2atmpS678,
                                                 .$2 = _M0L6_2atmpS679};
  } else {
    int32_t _M0L6_2atmpS680 = _M0Lm2loS176;
    int32_t _M0L6_2atmpS681 = _M0Lm2hiS178;
    moonbit_incref_cycle_free(_M0L4selfS175);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS175,
                                                 .$1 = _M0L6_2atmpS680,
                                                 .$2 = _M0L6_2atmpS681};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS173,
  struct _M0TPB4Show _M0L4showS172
) {
  struct _M0TPB6Logger _M0L6_2atmpS659;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS173);
  _M0L6_2atmpS659
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS173
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS172.$0->$method_0(_M0L4showS172.$1, _M0L6_2atmpS659);
  if (_M0L6_2atmpS659.$1) {
    moonbit_decref(_M0L6_2atmpS659.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS171,
  struct _M0TPB4Show _M0L4showS170
) {
  struct _M0TPB6Logger _M0L6_2atmpS658;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS171);
  _M0L6_2atmpS658
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS171
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS170.$0->$method_0(_M0L4showS170.$1, _M0L6_2atmpS658);
  if (_M0L6_2atmpS658.$1) {
    moonbit_decref(_M0L6_2atmpS658.$1);
  }
  return 0;
}

int32_t _M0IPC16uint166UInt16PB7Default7default() {
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return 0;
}

moonbit_string_t _M0MPC16string6String14escape_2einner(
  moonbit_string_t _M0L4selfS168,
  int32_t _M0L5quoteS169
) {
  struct _M0TPB13StringBuilder* _M0L3bufS167;
  int32_t _M0L6_2atmpS657;
  struct _M0TPC16string10StringView _M0L6_2atmpS655;
  struct _M0TPB6Logger _M0L6_2atmpS656;
  moonbit_string_t _result_930;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS167 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS657 = Moonbit_array_length(_M0L4selfS168);
  moonbit_incref_cycle_free(_M0L4selfS168);
  _M0L6_2atmpS655
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS168, .$1 = 0, .$2 = _M0L6_2atmpS657
  };
  moonbit_incref_cycle_free(_M0L3bufS167);
  _M0L6_2atmpS656
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS167
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS655, _M0L6_2atmpS656, _M0L5quoteS169);
  moonbit_decref_cycle_free(_M0L6_2atmpS655.$0);
  if (_M0L6_2atmpS656.$1) {
    moonbit_decref(_M0L6_2atmpS656.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_930 = _M0MPB13StringBuilder10to__string(_M0L3bufS167);
  moonbit_decref_cycle_free(_M0L3bufS167);
  return _result_930;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS159,
  struct _M0TPB6Logger _M0L6loggerS157,
  int32_t _M0L5quoteS156
) {
  int32_t _M0L3endS653;
  int32_t _M0L5startS654;
  int32_t _M0L3lenS158;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS160;
  int32_t _M0L1iS161;
  int32_t _M0L3segS162;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS156) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS157.$0->$method_3(_M0L6loggerS157.$1, 34);
  }
  _M0L3endS653 = _M0L4selfS159.$2;
  _M0L5startS654 = _M0L4selfS159.$1;
  _M0L3lenS158 = _M0L3endS653 - _M0L5startS654;
  moonbit_incref_cycle_free(_M0L4selfS159.$0);
  if (_M0L6loggerS157.$1) {
    moonbit_incref(_M0L6loggerS157.$1);
  }
  _M0L6_2aenvS160
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS160)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 25, 0);
  _M0L6_2aenvS160->$0 = _M0L4selfS159;
  _M0L6_2aenvS160->$1 = _M0L6loggerS157;
  _M0L1iS161 = 0;
  _M0L3segS162 = 0;
  _2afor_163:;
  while (1) {
    moonbit_string_t _M0L3strS650;
    int32_t _M0L5startS652;
    int32_t _M0L6_2atmpS651;
    int32_t _M0L4codeS164;
    int32_t _M0L1cS166;
    int32_t _M0L6_2atmpS634;
    int32_t _M0L6_2atmpS635;
    int32_t _M0L6_2atmpS636;
    if (_M0L1iS161 >= _M0L3lenS158) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS160, _M0L3segS162, _M0L1iS161);
      moonbit_decref_cycle_free(_M0L6_2aenvS160);
      break;
    }
    _M0L3strS650 = _M0L4selfS159.$0;
    _M0L5startS652 = _M0L4selfS159.$1;
    _M0L6_2atmpS651 = _M0L5startS652 + _M0L1iS161;
    _M0L4codeS164 = _M0L3strS650[_M0L6_2atmpS651];
    switch (_M0L4codeS164) {
      case 34: {
        _M0L1cS166 = _M0L4codeS164;
        goto join_165;
        break;
      }
      
      case 92: {
        _M0L1cS166 = _M0L4codeS164;
        goto join_165;
        break;
      }
      
      case 10: {
        int32_t _M0L6_2atmpS637;
        int32_t _M0L6_2atmpS638;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS160, _M0L3segS162, _M0L1iS161);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS157.$0->$method_0(_M0L6loggerS157.$1, (moonbit_string_t)moonbit_string_literal_13.data);
        _M0L6_2atmpS637 = _M0L1iS161 + 1;
        _M0L6_2atmpS638 = _M0L1iS161 + 1;
        _M0L1iS161 = _M0L6_2atmpS637;
        _M0L3segS162 = _M0L6_2atmpS638;
        goto _2afor_163;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS639;
        int32_t _M0L6_2atmpS640;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS160, _M0L3segS162, _M0L1iS161);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS157.$0->$method_0(_M0L6loggerS157.$1, (moonbit_string_t)moonbit_string_literal_14.data);
        _M0L6_2atmpS639 = _M0L1iS161 + 1;
        _M0L6_2atmpS640 = _M0L1iS161 + 1;
        _M0L1iS161 = _M0L6_2atmpS639;
        _M0L3segS162 = _M0L6_2atmpS640;
        goto _2afor_163;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS641;
        int32_t _M0L6_2atmpS642;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS160, _M0L3segS162, _M0L1iS161);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS157.$0->$method_0(_M0L6loggerS157.$1, (moonbit_string_t)moonbit_string_literal_15.data);
        _M0L6_2atmpS641 = _M0L1iS161 + 1;
        _M0L6_2atmpS642 = _M0L1iS161 + 1;
        _M0L1iS161 = _M0L6_2atmpS641;
        _M0L3segS162 = _M0L6_2atmpS642;
        goto _2afor_163;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS643;
        int32_t _M0L6_2atmpS644;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS160, _M0L3segS162, _M0L1iS161);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS157.$0->$method_0(_M0L6loggerS157.$1, (moonbit_string_t)moonbit_string_literal_16.data);
        _M0L6_2atmpS643 = _M0L1iS161 + 1;
        _M0L6_2atmpS644 = _M0L1iS161 + 1;
        _M0L1iS161 = _M0L6_2atmpS643;
        _M0L3segS162 = _M0L6_2atmpS644;
        goto _2afor_163;
        break;
      }
      default: {
        if (_M0L4codeS164 < 32) {
          int32_t _M0L6_2atmpS646;
          moonbit_string_t _M0L6_2atmpS645;
          int32_t _M0L6_2atmpS647;
          int32_t _M0L6_2atmpS648;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS160, _M0L3segS162, _M0L1iS161);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS157.$0->$method_0(_M0L6loggerS157.$1, (moonbit_string_t)moonbit_string_literal_17.data);
          _M0L6_2atmpS646 = _M0L4codeS164 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS645 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS646);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS157.$0->$method_0(_M0L6loggerS157.$1, _M0L6_2atmpS645);
          moonbit_decref_cycle_free(_M0L6_2atmpS645);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS157.$0->$method_0(_M0L6loggerS157.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS647 = _M0L1iS161 + 1;
          _M0L6_2atmpS648 = _M0L1iS161 + 1;
          _M0L1iS161 = _M0L6_2atmpS647;
          _M0L3segS162 = _M0L6_2atmpS648;
          goto _2afor_163;
        } else {
          int32_t _M0L6_2atmpS649 = _M0L1iS161 + 1;
          int32_t _tmp_933 = _M0L3segS162;
          _M0L1iS161 = _M0L6_2atmpS649;
          _M0L3segS162 = _tmp_933;
          goto _2afor_163;
        }
        break;
      }
    }
    goto joinlet_932;
    join_165:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS160, _M0L3segS162, _M0L1iS161);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS157.$0->$method_3(_M0L6loggerS157.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS634 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS166);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS157.$0->$method_3(_M0L6loggerS157.$1, _M0L6_2atmpS634);
    _M0L6_2atmpS635 = _M0L1iS161 + 1;
    _M0L6_2atmpS636 = _M0L1iS161 + 1;
    _M0L1iS161 = _M0L6_2atmpS635;
    _M0L3segS162 = _M0L6_2atmpS636;
    continue;
    joinlet_932:;
    break;
  }
  if (_M0L5quoteS156) {
    #line 202 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS157.$0->$method_3(_M0L6loggerS157.$1, 34);
  }
  return 0;
}

int32_t _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS152,
  int32_t _M0L3segS155,
  int32_t _M0L1iS154
) {
  struct _M0TPB6Logger _M0L6loggerS151;
  struct _M0TPC16string10StringView _M0L4selfS153;
  #line 153 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6loggerS151 = _M0L6_2aenvS152->$1;
  _M0L4selfS153 = _M0L6_2aenvS152->$0;
  if (_M0L1iS154 > _M0L3segS155) {
    int64_t _M0L6_2atmpS633 = (int64_t)_M0L1iS154;
    struct _M0TPC16string10StringView _M0L6_2atmpS632;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS632
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS153, _M0L3segS155, _M0L6_2atmpS633);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS151.$0->$method_2(_M0L6loggerS151.$1, _M0L6_2atmpS632);
    moonbit_decref_cycle_free(_M0L6_2atmpS632.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS142,
  int32_t _M0L5startS144,
  int64_t _M0L3endS146
) {
  int32_t _M0L3endS630;
  int32_t _M0L5startS631;
  int32_t _M0L3lenS141;
  int32_t _M0Lm2loS143;
  int32_t _M0Lm2hiS145;
  moonbit_string_t _M0L3strS149;
  int32_t _M0L4baseS150;
  int32_t _M0L6_2atmpS608;
  int32_t _if__result_934;
  int32_t _M0L6_2atmpS618;
  int32_t _if__result_935;
  int32_t _M0L6_2atmpS620;
  int32_t _M0L6_2atmpS621;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS630 = _M0L4selfS142.$2;
  _M0L5startS631 = _M0L4selfS142.$1;
  _M0L3lenS141 = _M0L3endS630 - _M0L5startS631;
  if (_M0L5startS144 < 0) {
    _M0Lm2loS143 = 0;
  } else if (_M0L5startS144 > _M0L3lenS141) {
    _M0Lm2loS143 = _M0L3lenS141;
  } else {
    _M0Lm2loS143 = _M0L5startS144;
  }
  if (_M0L3endS146 == 4294967296ll) {
    _M0Lm2hiS145 = _M0L3lenS141;
  } else {
    int64_t _M0L7_2aSomeS147 = _M0L3endS146;
    int32_t _M0L4_2aeS148 = (int32_t)_M0L7_2aSomeS147;
    if (_M0L4_2aeS148 < 0) {
      _M0Lm2hiS145 = 0;
    } else if (_M0L4_2aeS148 > _M0L3lenS141) {
      _M0Lm2hiS145 = _M0L3lenS141;
    } else {
      _M0Lm2hiS145 = _M0L4_2aeS148;
    }
  }
  _M0L3strS149 = _M0L4selfS142.$0;
  _M0L4baseS150 = _M0L4selfS142.$1;
  _M0L6_2atmpS608 = _M0Lm2loS143;
  if (_M0L6_2atmpS608 > 0) {
    int32_t _M0L6_2atmpS607 = _M0Lm2loS143;
    if (_M0L6_2atmpS607 < _M0L3lenS141) {
      int32_t _M0L6_2atmpS606 = _M0Lm2loS143;
      int32_t _M0L6_2atmpS605 = _M0L4baseS150 + _M0L6_2atmpS606;
      int32_t _M0L6_2atmpS604 = _M0L3strS149[_M0L6_2atmpS605];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS604)) {
        int32_t _M0L6_2atmpS603 = _M0Lm2loS143;
        int32_t _M0L6_2atmpS602 = _M0L4baseS150 + _M0L6_2atmpS603;
        int32_t _M0L6_2atmpS601 = _M0L6_2atmpS602 - 1;
        int32_t _M0L6_2atmpS600 = _M0L3strS149[_M0L6_2atmpS601];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_934
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS600);
      } else {
        _if__result_934 = 0;
      }
    } else {
      _if__result_934 = 0;
    }
  } else {
    _if__result_934 = 0;
  }
  if (_if__result_934) {
    int32_t _M0L6_2atmpS609 = _M0Lm2loS143;
    _M0Lm2loS143 = _M0L6_2atmpS609 + 1;
  }
  _M0L6_2atmpS618 = _M0Lm2hiS145;
  if (_M0L6_2atmpS618 > 0) {
    int32_t _M0L6_2atmpS617 = _M0Lm2hiS145;
    if (_M0L6_2atmpS617 < _M0L3lenS141) {
      int32_t _M0L6_2atmpS616 = _M0Lm2hiS145;
      int32_t _M0L6_2atmpS615 = _M0L4baseS150 + _M0L6_2atmpS616;
      int32_t _M0L6_2atmpS614 = _M0L3strS149[_M0L6_2atmpS615];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS614)) {
        int32_t _M0L6_2atmpS613 = _M0Lm2hiS145;
        int32_t _M0L6_2atmpS612 = _M0L4baseS150 + _M0L6_2atmpS613;
        int32_t _M0L6_2atmpS611 = _M0L6_2atmpS612 - 1;
        int32_t _M0L6_2atmpS610 = _M0L3strS149[_M0L6_2atmpS611];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_935
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS610);
      } else {
        _if__result_935 = 0;
      }
    } else {
      _if__result_935 = 0;
    }
  } else {
    _if__result_935 = 0;
  }
  if (_if__result_935) {
    int32_t _M0L6_2atmpS619 = _M0Lm2hiS145;
    _M0Lm2hiS145 = _M0L6_2atmpS619 - 1;
  }
  _M0L6_2atmpS620 = _M0Lm2loS143;
  _M0L6_2atmpS621 = _M0Lm2hiS145;
  if (_M0L6_2atmpS620 >= _M0L6_2atmpS621) {
    int32_t _M0L6_2atmpS625 = _M0Lm2loS143;
    int32_t _M0L6_2atmpS622 = _M0L4baseS150 + _M0L6_2atmpS625;
    int32_t _M0L6_2atmpS624 = _M0Lm2loS143;
    int32_t _M0L6_2atmpS623 = _M0L4baseS150 + _M0L6_2atmpS624;
    moonbit_incref_cycle_free(_M0L3strS149);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS149,
                                                 .$1 = _M0L6_2atmpS622,
                                                 .$2 = _M0L6_2atmpS623};
  } else {
    int32_t _M0L6_2atmpS629 = _M0Lm2loS143;
    int32_t _M0L6_2atmpS626 = _M0L4baseS150 + _M0L6_2atmpS629;
    int32_t _M0L6_2atmpS628 = _M0Lm2hiS145;
    int32_t _M0L6_2atmpS627 = _M0L4baseS150 + _M0L6_2atmpS628;
    moonbit_incref_cycle_free(_M0L3strS149);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS149,
                                                 .$1 = _M0L6_2atmpS626,
                                                 .$2 = _M0L6_2atmpS627};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS140) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS139;
  int32_t _M0L6_2atmpS597;
  int32_t _M0L6_2atmpS596;
  int32_t _M0L6_2atmpS599;
  int32_t _M0L6_2atmpS598;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS595;
  moonbit_string_t _result_936;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS139 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS597 = _M0IPC14byte4BytePB3Div3div(_M0L1bS140, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS596
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS597);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS139, _M0L6_2atmpS596);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS599 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS140, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS598
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS599);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS139, _M0L6_2atmpS598);
  _M0L6_2atmpS595 = _M0L7_2aselfS139;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_936 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS595);
  moonbit_decref_cycle_free(_M0L6_2atmpS595);
  return _result_936;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS138) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS138 < 10) {
    int32_t _M0L6_2atmpS592;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS592 = _M0IPC14byte4BytePB3Add3add(_M0L1iS138, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS592);
  } else {
    int32_t _M0L6_2atmpS594;
    int32_t _M0L6_2atmpS593;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS594 = _M0IPC14byte4BytePB3Add3add(_M0L1iS138, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS593 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS594, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS593);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS136,
  int32_t _M0L4thatS137
) {
  int32_t _M0L6_2atmpS590;
  int32_t _M0L6_2atmpS591;
  int32_t _M0L6_2atmpS589;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS590 = (int32_t)_M0L4selfS136;
  _M0L6_2atmpS591 = (int32_t)_M0L4thatS137;
  _M0L6_2atmpS589 = _M0L6_2atmpS590 - _M0L6_2atmpS591;
  return _M0L6_2atmpS589 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS134,
  int32_t _M0L4thatS135
) {
  int32_t _M0L6_2atmpS587;
  int32_t _M0L6_2atmpS588;
  int32_t _M0L6_2atmpS586;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS587 = (int32_t)_M0L4selfS134;
  _M0L6_2atmpS588 = (int32_t)_M0L4thatS135;
  _M0L6_2atmpS586 = _M0L6_2atmpS587 % _M0L6_2atmpS588;
  return _M0L6_2atmpS586 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS132,
  int32_t _M0L4thatS133
) {
  int32_t _M0L6_2atmpS584;
  int32_t _M0L6_2atmpS585;
  int32_t _M0L6_2atmpS583;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS584 = (int32_t)_M0L4selfS132;
  _M0L6_2atmpS585 = (int32_t)_M0L4thatS133;
  _M0L6_2atmpS583 = _M0L6_2atmpS584 / _M0L6_2atmpS585;
  return _M0L6_2atmpS583 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS130,
  int32_t _M0L4thatS131
) {
  int32_t _M0L6_2atmpS581;
  int32_t _M0L6_2atmpS582;
  int32_t _M0L6_2atmpS580;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS581 = (int32_t)_M0L4selfS130;
  _M0L6_2atmpS582 = (int32_t)_M0L4thatS131;
  _M0L6_2atmpS580 = _M0L6_2atmpS581 + _M0L6_2atmpS582;
  return _M0L6_2atmpS580 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS129) {
  int32_t _M0L6_2atmpS579;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS579 = (int32_t)_M0L4selfS129;
  return _M0L6_2atmpS579;
}

int32_t _M0MPC16uint166UInt1623is__trailing__surrogate(int32_t _M0L4selfS128) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS128 >= 56320 && _M0L4selfS128 <= 57343;
}

int32_t _M0MPC16uint166UInt1622is__leading__surrogate(int32_t _M0L4selfS127) {
  #line 28 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  return _M0L4selfS127 >= 55296 && _M0L4selfS127 <= 56319;
}

int32_t _M0IPB13StringBuilderPB6Logger13write__string(
  struct _M0TPB13StringBuilder* _M0L4selfS126,
  moonbit_string_t _M0L3strS124
) {
  int32_t _M0L8str__lenS123;
  int32_t _M0L3lenS578;
  int32_t _M0L8requiredS125;
  uint16_t* _M0L4dataS573;
  int32_t _M0L6_2atmpS572;
  int32_t _if__result_937;
  uint16_t* _M0L4dataS574;
  int32_t _M0L3lenS575;
  int32_t _M0L3lenS577;
  int32_t _M0L6_2atmpS576;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS123 = Moonbit_array_length(_M0L3strS124);
  if (_M0L8str__lenS123 == 0) {
    return 0;
  }
  _M0L3lenS578 = _M0L4selfS126->$1;
  _M0L8requiredS125 = _M0L3lenS578 + _M0L8str__lenS123;
  _M0L4dataS573 = _M0L4selfS126->$0;
  _M0L6_2atmpS572 = Moonbit_array_length(_M0L4dataS573);
  if (_M0L8requiredS125 > _M0L6_2atmpS572) {
    _if__result_937 = 1;
  } else {
    int32_t _M0L3lenS571 = _M0L4selfS126->$1;
    _if__result_937 = _M0L8requiredS125 < _M0L3lenS571;
  }
  if (_if__result_937) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS126, _M0L8requiredS125);
  }
  _M0L4dataS574 = _M0L4selfS126->$0;
  _M0L3lenS575 = _M0L4selfS126->$1;
  moonbit_incref_cycle_free(_M0L4dataS574);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS574, _M0L3lenS575, _M0L3strS124, 0, _M0L8str__lenS123);
  moonbit_decref_cycle_free(_M0L4dataS574);
  _M0L3lenS577 = _M0L4selfS126->$1;
  _M0L6_2atmpS576 = _M0L3lenS577 + _M0L8str__lenS123;
  _M0L4selfS126->$1 = _M0L6_2atmpS576;
  return 0;
}

int32_t _M0MPC15array10FixedArray26unsafe__blit__from__string(
  uint16_t* _M0L4selfS119,
  int32_t _M0L11dst__offsetS122,
  moonbit_string_t _M0L3strS120,
  int32_t _M0L11str__offsetS115,
  int32_t _M0L3lenS116
) {
  int32_t _M0L16end__str__offsetS114;
  int32_t _M0L1iS117;
  int32_t _M0L1jS118;
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L16end__str__offsetS114 = _M0L11str__offsetS115 + _M0L3lenS116;
  _M0L1iS117 = _M0L11str__offsetS115;
  _M0L1jS118 = _M0L11dst__offsetS122;
  while (1) {
    if (_M0L1iS117 < _M0L16end__str__offsetS114) {
      int32_t _M0L6_2atmpS568 = _M0L3strS120[_M0L1iS117];
      int32_t _M0L6_2atmpS569;
      int32_t _M0L6_2atmpS570;
      _M0L4selfS119[_M0L1jS118] = _M0L6_2atmpS568;
      _M0L6_2atmpS569 = _M0L1iS117 + 1;
      _M0L6_2atmpS570 = _M0L1jS118 + 1;
      _M0L1iS117 = _M0L6_2atmpS569;
      _M0L1jS118 = _M0L6_2atmpS570;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger11write__char(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  int32_t _M0L2chS111
) {
  uint32_t _M0L4codeS110;
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  #line 121 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4codeS110 = _M0MPC14char4Char8to__uint(_M0L2chS111);
  if (_M0L4codeS110 <= 65535u) {
    int32_t _M0L3lenS539 = _M0L4selfS112->$1;
    uint16_t* _M0L4dataS541 = _M0L4selfS112->$0;
    int32_t _M0L6_2atmpS540 = Moonbit_array_length(_M0L4dataS541);
    uint16_t* _M0L4dataS544;
    int32_t _M0L3lenS545;
    int32_t _M0L6_2atmpS546;
    int32_t _M0L3lenS548;
    int32_t _M0L6_2atmpS547;
    if (_M0L3lenS539 >= _M0L6_2atmpS540) {
      int32_t _M0L3lenS543 = _M0L4selfS112->$1;
      int32_t _M0L6_2atmpS542 = _M0L3lenS543 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS112, _M0L6_2atmpS542);
    }
    _M0L4dataS544 = _M0L4selfS112->$0;
    _M0L3lenS545 = _M0L4selfS112->$1;
    moonbit_incref_cycle_free(_M0L4dataS544);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS546 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS110);
    if (
      _M0L3lenS545 < 0 || _M0L3lenS545 >= Moonbit_array_length(_M0L4dataS544)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS544[_M0L3lenS545] = _M0L6_2atmpS546;
    moonbit_decref_cycle_free(_M0L4dataS544);
    _M0L3lenS548 = _M0L4selfS112->$1;
    _M0L6_2atmpS547 = _M0L3lenS548 + 1;
    _M0L4selfS112->$1 = _M0L6_2atmpS547;
  } else if (_M0L4codeS110 <= 1114111u) {
    uint16_t* _M0L4dataS552 = _M0L4selfS112->$0;
    int32_t _M0L6_2atmpS550 = Moonbit_array_length(_M0L4dataS552);
    int32_t _M0L3lenS551 = _M0L4selfS112->$1;
    int32_t _M0L6_2atmpS549 = _M0L6_2atmpS550 - _M0L3lenS551;
    uint32_t _M0L4codeS113;
    uint16_t* _M0L4dataS555;
    int32_t _M0L3lenS556;
    uint32_t _M0L6_2atmpS559;
    uint32_t _M0L6_2atmpS558;
    int32_t _M0L6_2atmpS557;
    uint16_t* _M0L4dataS560;
    int32_t _M0L3lenS565;
    int32_t _M0L6_2atmpS561;
    uint32_t _M0L6_2atmpS564;
    uint32_t _M0L6_2atmpS563;
    int32_t _M0L6_2atmpS562;
    int32_t _M0L3lenS567;
    int32_t _M0L6_2atmpS566;
    if (_M0L6_2atmpS549 < 2) {
      int32_t _M0L3lenS554 = _M0L4selfS112->$1;
      int32_t _M0L6_2atmpS553 = _M0L3lenS554 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS112, _M0L6_2atmpS553);
    }
    _M0L4codeS113 = _M0L4codeS110 - 65536u;
    _M0L4dataS555 = _M0L4selfS112->$0;
    _M0L3lenS556 = _M0L4selfS112->$1;
    _M0L6_2atmpS559 = _M0L4codeS113 >> 10;
    _M0L6_2atmpS558 = 55296u + _M0L6_2atmpS559;
    moonbit_incref_cycle_free(_M0L4dataS555);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS557 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS558);
    if (
      _M0L3lenS556 < 0 || _M0L3lenS556 >= Moonbit_array_length(_M0L4dataS555)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS555[_M0L3lenS556] = _M0L6_2atmpS557;
    moonbit_decref_cycle_free(_M0L4dataS555);
    _M0L4dataS560 = _M0L4selfS112->$0;
    _M0L3lenS565 = _M0L4selfS112->$1;
    _M0L6_2atmpS561 = _M0L3lenS565 + 1;
    _M0L6_2atmpS564 = _M0L4codeS113 & 1023u;
    _M0L6_2atmpS563 = 56320u + _M0L6_2atmpS564;
    moonbit_incref_cycle_free(_M0L4dataS560);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS562 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS563);
    if (
      _M0L6_2atmpS561 < 0
      || _M0L6_2atmpS561 >= Moonbit_array_length(_M0L4dataS560)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS560[_M0L6_2atmpS561] = _M0L6_2atmpS562;
    moonbit_decref_cycle_free(_M0L4dataS560);
    _M0L3lenS567 = _M0L4selfS112->$1;
    _M0L6_2atmpS566 = _M0L3lenS567 + 2;
    _M0L4selfS112->$1 = _M0L6_2atmpS566;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_18.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS107,
  int32_t _M0L8requiredS108
) {
  uint16_t* _M0L4dataS538;
  int32_t _M0L6_2atmpS536;
  int32_t _M0L3lenS537;
  int32_t _M0L13new__capacityS106;
  uint16_t* _M0L4dataS533;
  int32_t _M0L6_2atmpS534;
  int32_t _M0L3lenS535;
  uint16_t* _M0L9new__dataS109;
  uint16_t* _M0L6_2aoldS876;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS538 = _M0L4selfS107->$0;
  _M0L6_2atmpS536 = Moonbit_array_length(_M0L4dataS538);
  _M0L3lenS537 = _M0L4selfS107->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS106
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS536, _M0L3lenS537, _M0L8requiredS108);
  _M0L4dataS533 = _M0L4selfS107->$0;
  moonbit_incref_cycle_free(_M0L4dataS533);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS534 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS535 = _M0L4selfS107->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS109
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS533, _M0L13new__capacityS106, _M0L6_2atmpS534, _M0L3lenS535, 0, 0);
  _M0L6_2aoldS876 = _M0L4selfS107->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS876);
  _M0L4selfS107->$0 = _M0L9new__dataS109;
  return 0;
}

int32_t _M0FPB31stringbuilder__growth__capacity(
  int32_t _M0L7currentS105,
  int32_t _M0L3lenS101,
  int32_t _M0L8requiredS100
) {
  int32_t _M0L5spaceS102;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L8requiredS100 < _M0L3lenS101) {
    #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_19.data);
  }
  _M0L5spaceS102 = _M0L7currentS105;
  while (1) {
    if (_M0L5spaceS102 < _M0L8requiredS100) {
      int32_t _M0L4nextS103 = _M0L5spaceS102 * 2;
      if (_M0L4nextS103 <= _M0L5spaceS102) {
        return _M0L8requiredS100;
      }
      _M0L5spaceS102 = _M0L4nextS103;
      continue;
    } else {
      return _M0L5spaceS102;
    }
    break;
  }
}

int32_t _M0MPC14uint4UInt10to__uint16(uint32_t _M0L4selfS99) {
  int32_t _M0L6_2atmpS532;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS532 = *(int32_t*)&_M0L4selfS99;
  return (uint16_t)_M0L6_2atmpS532;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS98) {
  int32_t _M0L6_2atmpS531;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS531 = _M0L4selfS98;
  return *(uint32_t*)&_M0L6_2atmpS531;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS96
) {
  int32_t _M0L3lenS522;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS522 = _M0L4selfS96->$1;
  if (_M0L3lenS522 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS523 = _M0L4selfS96->$1;
    uint16_t* _M0L4dataS525 = _M0L4selfS96->$0;
    int32_t _M0L6_2atmpS524 = Moonbit_array_length(_M0L4dataS525);
    if (_M0L3lenS523 == _M0L6_2atmpS524) {
      uint16_t* _M0L4dataS526 = _M0L4selfS96->$0;
      moonbit_incref_cycle_free(_M0L4dataS526);
      return _M0L4dataS526;
    } else {
      uint16_t* _M0L4dataS527 = _M0L4selfS96->$0;
      int32_t _M0L3lenS528 = _M0L4selfS96->$1;
      int32_t _M0L6_2atmpS529;
      int32_t _M0L3lenS530;
      uint16_t* _M0L4dataS97;
      moonbit_incref_cycle_free(_M0L4dataS527);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS529 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS530 = _M0L4selfS96->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS97
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS527, _M0L3lenS528, _M0L6_2atmpS529, _M0L3lenS530, 0, 0);
      return _M0L4dataS97;
    }
  }
}

uint16_t* _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(
  uint16_t* _M0L3srcS93,
  int32_t _M0L13allocate__lenS89,
  int32_t _M0L4initS94,
  int32_t _M0L3lenS90,
  int32_t _M0L11src__offsetS91,
  int32_t _M0L11dst__offsetS92
) {
  int32_t _if__result_940;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS89 >= 0) {
    if (_M0L3lenS90 >= 0) {
      if (_M0L11src__offsetS91 >= 0) {
        if (_M0L11dst__offsetS92 >= 0) {
          int32_t _M0L6_2atmpS518 = _M0L11src__offsetS91 + _M0L3lenS90;
          int32_t _M0L6_2atmpS519 = Moonbit_array_length(_M0L3srcS93);
          if (_M0L6_2atmpS518 <= _M0L6_2atmpS519) {
            int32_t _M0L6_2atmpS517 = _M0L11dst__offsetS92 + _M0L3lenS90;
            _if__result_940 = _M0L6_2atmpS517 <= _M0L13allocate__lenS89;
          } else {
            _if__result_940 = 0;
          }
        } else {
          _if__result_940 = 0;
        }
      } else {
        _if__result_940 = 0;
      }
    } else {
      _if__result_940 = 0;
    }
  } else {
    _if__result_940 = 0;
  }
  if (_if__result_940) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS93, _M0L13allocate__lenS89, _M0L4initS94, _M0L11src__offsetS91, _M0L11dst__offsetS92, _M0L3lenS90);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS95;
    int32_t _M0L6_2atmpS521;
    moonbit_string_t _M0L6_2atmpS520;
    uint16_t* _result_941;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS95
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS95, (moonbit_string_t)moonbit_string_literal_20.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS95, _M0L13allocate__lenS89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS95, (moonbit_string_t)moonbit_string_literal_21.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS95, _M0L11src__offsetS91);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS95, (moonbit_string_t)moonbit_string_literal_22.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS95, _M0L11dst__offsetS92);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS95, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS95, _M0L3lenS90);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS95, (moonbit_string_t)moonbit_string_literal_24.data);
    _M0L6_2atmpS521 = Moonbit_array_length(_M0L3srcS93);
    moonbit_decref_cycle_free(_M0L3srcS93);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS95, _M0L6_2atmpS521);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS520
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS95);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS95);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_941 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS520);
    moonbit_decref_cycle_free(_M0L6_2atmpS520);
    return _result_941;
  }
}

uint16_t* _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(
  uint16_t* _M0L3srcS86,
  int32_t _M0L13allocate__lenS83,
  int32_t _M0L4initS84,
  int32_t _M0L11src__offsetS87,
  int32_t _M0L11dst__offsetS85,
  int32_t _M0L9blit__lenS88
) {
  uint16_t* _M0L3dstS82;
  #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  _M0L3dstS82
  = (uint16_t*)moonbit_make_string(_M0L13allocate__lenS83, _M0L4initS84);
  moonbit_incref_cycle_free(_M0L3dstS82);
  #line 90 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS82, _M0L11dst__offsetS85, _M0L3srcS86, _M0L11src__offsetS87, _M0L9blit__lenS88, sizeof(uint16_t));
  return _M0L3dstS82;
}

struct _M0TPB13StringBuilder* _M0MPB13StringBuilder21StringBuilder_2einner(
  int32_t _M0L10size__hintS80
) {
  int32_t _M0L7initialS79;
  uint16_t* _M0L4dataS81;
  struct _M0TPB13StringBuilder* _block_942;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS80 < 1) {
    _M0L7initialS79 = 1;
  } else {
    int32_t _M0L6_2atmpS516 = _M0L10size__hintS80 + 1;
    _M0L7initialS79 = _M0L6_2atmpS516 / 2;
  }
  _M0L4dataS81 = (uint16_t*)moonbit_make_string(_M0L7initialS79, 0);
  _block_942
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_942)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 30, 0);
  _block_942->$0 = _M0L4dataS81;
  _block_942->$1 = 0;
  return _block_942;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS78) {
  int32_t _M0L6_2atmpS515;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS515 = (int32_t)_M0L4selfS78;
  return _M0L6_2atmpS515;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS70,
  int32_t _M0L13allocate__lenS66,
  int32_t _M0L3lenS67,
  int32_t _M0L11src__offsetS68,
  int32_t _M0L11dst__offsetS69
) {
  int32_t _if__result_943;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS66 >= 0) {
    if (_M0L3lenS67 >= 0) {
      if (_M0L11src__offsetS68 >= 0) {
        if (_M0L11dst__offsetS69 >= 0) {
          int32_t _M0L6_2atmpS506 = _M0L11src__offsetS68 + _M0L3lenS67;
          int32_t _M0L6_2atmpS507;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS507 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS70);
          if (_M0L6_2atmpS506 <= _M0L6_2atmpS507) {
            int32_t _M0L6_2atmpS505 = _M0L11dst__offsetS69 + _M0L3lenS67;
            _if__result_943 = _M0L6_2atmpS505 <= _M0L13allocate__lenS66;
          } else {
            _if__result_943 = 0;
          }
        } else {
          _if__result_943 = 0;
        }
      } else {
        _if__result_943 = 0;
      }
    } else {
      _if__result_943 = 0;
    }
  } else {
    _if__result_943 = 0;
  }
  if (_if__result_943) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS66, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS70, _M0L11src__offsetS68, _M0L11dst__offsetS69, _M0L3lenS67);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS71;
    int32_t _M0L6_2atmpS509;
    moonbit_string_t _M0L6_2atmpS508;
    moonbit_string_t* _result_944;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS71
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS71, (moonbit_string_t)moonbit_string_literal_20.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS71, _M0L13allocate__lenS66);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS71, (moonbit_string_t)moonbit_string_literal_21.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS71, _M0L11src__offsetS68);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS71, (moonbit_string_t)moonbit_string_literal_22.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS71, _M0L11dst__offsetS69);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS71, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS71, _M0L3lenS67);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS71, (moonbit_string_t)moonbit_string_literal_24.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS509 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS70);
    moonbit_decref_cycle_free(_M0L3srcS70);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS71, _M0L6_2atmpS509);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS508
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS71);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS71);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_944
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS508);
    moonbit_decref_cycle_free(_M0L6_2atmpS508);
    return _result_944;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS76,
  int32_t _M0L13allocate__lenS72,
  int32_t _M0L3lenS73,
  int32_t _M0L11src__offsetS74,
  int32_t _M0L11dst__offsetS75
) {
  int32_t _if__result_945;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS72 >= 0) {
    if (_M0L3lenS73 >= 0) {
      if (_M0L11src__offsetS74 >= 0) {
        if (_M0L11dst__offsetS75 >= 0) {
          int32_t _M0L6_2atmpS511 = _M0L11src__offsetS74 + _M0L3lenS73;
          int32_t _M0L6_2atmpS512;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS512
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS76);
          if (_M0L6_2atmpS511 <= _M0L6_2atmpS512) {
            int32_t _M0L6_2atmpS510 = _M0L11dst__offsetS75 + _M0L3lenS73;
            _if__result_945 = _M0L6_2atmpS510 <= _M0L13allocate__lenS72;
          } else {
            _if__result_945 = 0;
          }
        } else {
          _if__result_945 = 0;
        }
      } else {
        _if__result_945 = 0;
      }
    } else {
      _if__result_945 = 0;
    }
  } else {
    _if__result_945 = 0;
  }
  if (_if__result_945) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS72, 0, _M0L3srcS76, _M0L11src__offsetS74, _M0L11dst__offsetS75, _M0L3lenS73);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS77;
    int32_t _M0L6_2atmpS514;
    moonbit_string_t _M0L6_2atmpS513;
    struct _M0TUsiE** _result_946;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS77
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS77, (moonbit_string_t)moonbit_string_literal_20.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS77, _M0L13allocate__lenS72);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS77, (moonbit_string_t)moonbit_string_literal_21.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS77, _M0L11src__offsetS74);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS77, (moonbit_string_t)moonbit_string_literal_22.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS77, _M0L11dst__offsetS75);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS77, (moonbit_string_t)moonbit_string_literal_23.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS77, _M0L3lenS73);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS77, (moonbit_string_t)moonbit_string_literal_24.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS514 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS76);
    moonbit_decref_cycle_free(_M0L3srcS76);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS77, _M0L6_2atmpS514);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS513
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS77);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS77);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_946
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS513);
    moonbit_decref_cycle_free(_M0L6_2atmpS513);
    return _result_946;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS63,
  moonbit_string_t _M0L3objS62
) {
  struct _M0TPB6Logger _M0L6_2atmpS503;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS63);
  _M0L6_2atmpS503
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS63
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS62, _M0L6_2atmpS503);
  if (_M0L6_2atmpS503.$1) {
    moonbit_decref(_M0L6_2atmpS503.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS65,
  int32_t _M0L3objS64
) {
  struct _M0TPB6Logger _M0L6_2atmpS504;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS65);
  _M0L6_2atmpS504
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS65
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS64, _M0L6_2atmpS504);
  if (_M0L6_2atmpS504.$1) {
    moonbit_decref(_M0L6_2atmpS504.$1);
  }
  return 0;
}

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t* _M0L3srcS53,
  int32_t _M0L13allocate__lenS51,
  int32_t _M0L11src__offsetS54,
  int32_t _M0L11dst__offsetS52,
  int32_t _M0L9blit__lenS55
) {
  moonbit_string_t* _M0L3dstS50;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS50
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L13allocate__lenS51, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGsE(_M0L3dstS50, _M0L11dst__offsetS52, _M0L3srcS53, _M0L11src__offsetS54, _M0L9blit__lenS55);
  moonbit_decref_cycle_free(_M0L3srcS53);
  return _M0L3dstS50;
}

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE** _M0L3srcS59,
  int32_t _M0L13allocate__lenS57,
  int32_t _M0L11src__offsetS60,
  int32_t _M0L11dst__offsetS58,
  int32_t _M0L9blit__lenS61
) {
  struct _M0TUsiE** _M0L3dstS56;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS56
  = (struct _M0TUsiE**)moonbit_make_ref_array(_M0L13allocate__lenS57, 0);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGUsiEE(_M0L3dstS56, _M0L11dst__offsetS58, _M0L3srcS59, _M0L11src__offsetS60, _M0L9blit__lenS61);
  moonbit_decref_cycle_free(_M0L3srcS59);
  return _M0L3dstS56;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS40,
  int32_t _M0L11dst__offsetS41,
  moonbit_string_t* _M0L3srcS42,
  int32_t _M0L11src__offsetS43,
  int32_t _M0L3lenS44
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS42);
  moonbit_incref_cycle_free(_M0L3dstS40);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS40, _M0L11dst__offsetS41, _M0L3srcS42, _M0L11src__offsetS43, _M0L3lenS44);
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS45,
  int32_t _M0L11dst__offsetS46,
  struct _M0TUsiE** _M0L3srcS47,
  int32_t _M0L11src__offsetS48,
  int32_t _M0L3lenS49
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS47);
  moonbit_incref_cycle_free(_M0L3dstS45);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS45, _M0L11dst__offsetS46, _M0L3srcS47, _M0L11src__offsetS48, _M0L3lenS49);
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGkE(
  uint16_t* _M0L3dstS13,
  int32_t _M0L11dst__offsetS15,
  uint16_t* _M0L3srcS14,
  int32_t _M0L11src__offsetS16,
  int32_t _M0L3lenS18
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS13 == _M0L3srcS14 && _M0L11dst__offsetS15 < _M0L11src__offsetS16
  ) {
    int32_t _M0L1iS17 = 0;
    while (1) {
      if (_M0L1iS17 < _M0L3lenS18) {
        int32_t _M0L6_2atmpS476 = _M0L11dst__offsetS15 + _M0L1iS17;
        int32_t _M0L6_2atmpS478 = _M0L11src__offsetS16 + _M0L1iS17;
        int32_t _M0L6_2atmpS477;
        int32_t _M0L6_2atmpS479;
        if (
          _M0L6_2atmpS478 < 0
          || _M0L6_2atmpS478 >= Moonbit_array_length(_M0L3srcS14)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS477 = (int32_t)_M0L3srcS14[_M0L6_2atmpS478];
        if (
          _M0L6_2atmpS476 < 0
          || _M0L6_2atmpS476 >= Moonbit_array_length(_M0L3dstS13)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS13[_M0L6_2atmpS476] = _M0L6_2atmpS477;
        _M0L6_2atmpS479 = _M0L1iS17 + 1;
        _M0L1iS17 = _M0L6_2atmpS479;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS14);
        moonbit_decref_cycle_free(_M0L3dstS13);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS484 = _M0L3lenS18 - 1;
    int32_t _M0L1iS20 = _M0L6_2atmpS484;
    while (1) {
      if (_M0L1iS20 >= 0) {
        int32_t _M0L6_2atmpS480 = _M0L11dst__offsetS15 + _M0L1iS20;
        int32_t _M0L6_2atmpS482 = _M0L11src__offsetS16 + _M0L1iS20;
        int32_t _M0L6_2atmpS481;
        int32_t _M0L6_2atmpS483;
        if (
          _M0L6_2atmpS482 < 0
          || _M0L6_2atmpS482 >= Moonbit_array_length(_M0L3srcS14)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS481 = (int32_t)_M0L3srcS14[_M0L6_2atmpS482];
        if (
          _M0L6_2atmpS480 < 0
          || _M0L6_2atmpS480 >= Moonbit_array_length(_M0L3dstS13)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS13[_M0L6_2atmpS480] = _M0L6_2atmpS481;
        _M0L6_2atmpS483 = _M0L1iS20 - 1;
        _M0L1iS20 = _M0L6_2atmpS483;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS14);
        moonbit_decref_cycle_free(_M0L3dstS13);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGsEE(
  moonbit_string_t* _M0L3dstS22,
  int32_t _M0L11dst__offsetS24,
  moonbit_string_t* _M0L3srcS23,
  int32_t _M0L11src__offsetS25,
  int32_t _M0L3lenS27
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS22 == _M0L3srcS23 && _M0L11dst__offsetS24 < _M0L11src__offsetS25
  ) {
    int32_t _M0L1iS26 = 0;
    while (1) {
      if (_M0L1iS26 < _M0L3lenS27) {
        int32_t _M0L6_2atmpS485 = _M0L11dst__offsetS24 + _M0L1iS26;
        int32_t _M0L6_2atmpS487 = _M0L11src__offsetS25 + _M0L1iS26;
        moonbit_string_t _M0L6_2atmpS486;
        moonbit_string_t _M0L6_2aoldS877;
        int32_t _M0L6_2atmpS488;
        if (
          _M0L6_2atmpS487 < 0
          || _M0L6_2atmpS487 >= Moonbit_array_length(_M0L3srcS23)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS486 = (moonbit_string_t)_M0L3srcS23[_M0L6_2atmpS487];
        if (
          _M0L6_2atmpS485 < 0
          || _M0L6_2atmpS485 >= Moonbit_array_length(_M0L3dstS22)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS877 = (moonbit_string_t)_M0L3dstS22[_M0L6_2atmpS485];
        moonbit_incref_cycle_free(_M0L6_2atmpS486);
        moonbit_decref_cycle_free(_M0L6_2aoldS877);
        _M0L3dstS22[_M0L6_2atmpS485] = _M0L6_2atmpS486;
        _M0L6_2atmpS488 = _M0L1iS26 + 1;
        _M0L1iS26 = _M0L6_2atmpS488;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS23);
        moonbit_decref_cycle_free(_M0L3dstS22);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS493 = _M0L3lenS27 - 1;
    int32_t _M0L1iS29 = _M0L6_2atmpS493;
    while (1) {
      if (_M0L1iS29 >= 0) {
        int32_t _M0L6_2atmpS489 = _M0L11dst__offsetS24 + _M0L1iS29;
        int32_t _M0L6_2atmpS491 = _M0L11src__offsetS25 + _M0L1iS29;
        moonbit_string_t _M0L6_2atmpS490;
        moonbit_string_t _M0L6_2aoldS878;
        int32_t _M0L6_2atmpS492;
        if (
          _M0L6_2atmpS491 < 0
          || _M0L6_2atmpS491 >= Moonbit_array_length(_M0L3srcS23)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS490 = (moonbit_string_t)_M0L3srcS23[_M0L6_2atmpS491];
        if (
          _M0L6_2atmpS489 < 0
          || _M0L6_2atmpS489 >= Moonbit_array_length(_M0L3dstS22)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS878 = (moonbit_string_t)_M0L3dstS22[_M0L6_2atmpS489];
        moonbit_incref_cycle_free(_M0L6_2atmpS490);
        moonbit_decref_cycle_free(_M0L6_2aoldS878);
        _M0L3dstS22[_M0L6_2atmpS489] = _M0L6_2atmpS490;
        _M0L6_2atmpS492 = _M0L1iS29 - 1;
        _M0L1iS29 = _M0L6_2atmpS492;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS23);
        moonbit_decref_cycle_free(_M0L3dstS22);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGUsiEEE(
  struct _M0TUsiE** _M0L3dstS31,
  int32_t _M0L11dst__offsetS33,
  struct _M0TUsiE** _M0L3srcS32,
  int32_t _M0L11src__offsetS34,
  int32_t _M0L3lenS36
) {
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (
    _M0L3dstS31 == _M0L3srcS32 && _M0L11dst__offsetS33 < _M0L11src__offsetS34
  ) {
    int32_t _M0L1iS35 = 0;
    while (1) {
      if (_M0L1iS35 < _M0L3lenS36) {
        int32_t _M0L6_2atmpS494 = _M0L11dst__offsetS33 + _M0L1iS35;
        int32_t _M0L6_2atmpS496 = _M0L11src__offsetS34 + _M0L1iS35;
        struct _M0TUsiE* _M0L6_2atmpS495;
        struct _M0TUsiE* _M0L6_2aoldS879;
        int32_t _M0L6_2atmpS497;
        if (
          _M0L6_2atmpS496 < 0
          || _M0L6_2atmpS496 >= Moonbit_array_length(_M0L3srcS32)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS495 = (struct _M0TUsiE*)_M0L3srcS32[_M0L6_2atmpS496];
        if (
          _M0L6_2atmpS494 < 0
          || _M0L6_2atmpS494 >= Moonbit_array_length(_M0L3dstS31)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS879 = (struct _M0TUsiE*)_M0L3dstS31[_M0L6_2atmpS494];
        if (_M0L6_2atmpS495) {
          moonbit_incref_cycle_free(_M0L6_2atmpS495);
        }
        if (_M0L6_2aoldS879) {
          moonbit_decref_cycle_free(_M0L6_2aoldS879);
        }
        _M0L3dstS31[_M0L6_2atmpS494] = _M0L6_2atmpS495;
        _M0L6_2atmpS497 = _M0L1iS35 + 1;
        _M0L1iS35 = _M0L6_2atmpS497;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS32);
        moonbit_decref_cycle_free(_M0L3dstS31);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS502 = _M0L3lenS36 - 1;
    int32_t _M0L1iS38 = _M0L6_2atmpS502;
    while (1) {
      if (_M0L1iS38 >= 0) {
        int32_t _M0L6_2atmpS498 = _M0L11dst__offsetS33 + _M0L1iS38;
        int32_t _M0L6_2atmpS500 = _M0L11src__offsetS34 + _M0L1iS38;
        struct _M0TUsiE* _M0L6_2atmpS499;
        struct _M0TUsiE* _M0L6_2aoldS880;
        int32_t _M0L6_2atmpS501;
        if (
          _M0L6_2atmpS500 < 0
          || _M0L6_2atmpS500 >= Moonbit_array_length(_M0L3srcS32)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS499 = (struct _M0TUsiE*)_M0L3srcS32[_M0L6_2atmpS500];
        if (
          _M0L6_2atmpS498 < 0
          || _M0L6_2atmpS498 >= Moonbit_array_length(_M0L3dstS31)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS880 = (struct _M0TUsiE*)_M0L3dstS31[_M0L6_2atmpS498];
        if (_M0L6_2atmpS499) {
          moonbit_incref_cycle_free(_M0L6_2atmpS499);
        }
        if (_M0L6_2aoldS880) {
          moonbit_decref_cycle_free(_M0L6_2aoldS880);
        }
        _M0L3dstS31[_M0L6_2atmpS498] = _M0L6_2atmpS499;
        _M0L6_2atmpS501 = _M0L1iS38 - 1;
        _M0L1iS38 = _M0L6_2atmpS501;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS32);
        moonbit_decref_cycle_free(_M0L3dstS31);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t* _M0L4selfS11) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS11);
}

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(
  struct _M0TUsiE** _M0L4selfS12
) {
  #line 146 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  return Moonbit_array_length(_M0L4selfS12);
}

int32_t _M0IPB7FailurePB4Show6output(
  void* _M0L10_2ax__6387S7,
  struct _M0TPB6Logger _M0L10_2ax__6388S10
) {
  struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS8;
  moonbit_string_t _M0L15_2a_2aarg__6389S9;
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2aFailureS8
  = (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L10_2ax__6387S7;
  _M0L15_2a_2aarg__6389S9 = _M0L10_2aFailureS8->$0;
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S10.$0->$method_0(_M0L10_2ax__6388S10.$1, (moonbit_string_t)moonbit_string_literal_25.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S10, _M0L15_2a_2aarg__6389S9);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S10.$0->$method_0(_M0L10_2ax__6388S10.$1, (moonbit_string_t)moonbit_string_literal_26.data);
  return 0;
}

int32_t _M0MPB6Logger13write__objectGsE(
  struct _M0TPB6Logger _M0L4selfS6,
  moonbit_string_t _M0L3objS5
) {
  #line 179 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 180 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS5, _M0L4selfS6);
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS448) {
  switch (Moonbit_object_tag(_M0L4_2aeS448)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_27.data;
      break;
    }
    
    case 1: {
      return (moonbit_string_t)moonbit_string_literal_28.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS448);
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_29.data;
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_30.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS471,
  struct _M0TPB4Show _M0L8_2aparamS470
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS469 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS471;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS469, _M0L8_2aparamS470);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS468,
  struct _M0TPB4Show _M0L8_2aparamS467
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS466 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS468;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS466, _M0L8_2aparamS467);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS465,
  int32_t _M0L8_2aparamS464
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS463 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS465;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS463, _M0L8_2aparamS464);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS462,
  struct _M0TPC16string10StringView _M0L8_2aparamS461
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS460 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS462;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS460, _M0L8_2aparamS461);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS459,
  moonbit_string_t _M0L8_2aparamS456,
  int32_t _M0L8_2aparamS457,
  int32_t _M0L8_2aparamS458
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS455 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS459;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS455, _M0L8_2aparamS456, _M0L8_2aparamS457, _M0L8_2aparamS458);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS454,
  moonbit_string_t _M0L8_2aparamS453
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS452 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS454;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS452, _M0L8_2aparamS453);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS475;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS441;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS442;
  int32_t _M0L7_2abindS443;
  struct _M0TUsiE** _M0L7_2abindS444;
  int32_t _M0L6_2acntS885;
  int32_t _M0L2__S445;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS475
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS441
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS441)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 33, 0);
  _M0L12async__testsS441->$0 = _M0L6_2atmpS475;
  _M0L12async__testsS441->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0L7_2abindS442
  = _M0FP46RiantR8snn__mbt8examples13stp__timestep52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS443 = _M0L7_2abindS442->$1;
  _M0L7_2abindS444 = _M0L7_2abindS442->$0;
  _M0L6_2acntS885 = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS442));
  if (_M0L6_2acntS885 > 1) {
    int32_t _M0L11_2anew__cntS886 = _M0L6_2acntS885 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS442), _M0L11_2anew__cntS886);
    moonbit_incref_cycle_free(_M0L7_2abindS444);
  } else if (_M0L6_2acntS885 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
    moonbit_free(_M0L7_2abindS442);
  }
  _M0L2__S445 = 0;
  while (1) {
    if (_M0L2__S445 < _M0L7_2abindS443) {
      struct _M0TUsiE* _M0L3argS446 =
        (struct _M0TUsiE*)_M0L7_2abindS444[_M0L2__S445];
      moonbit_string_t _M0L6_2atmpS472 = _M0L3argS446->$0;
      int32_t _M0L6_2atmpS473 = _M0L3argS446->$1;
      int32_t _M0L6_2atmpS474;
      moonbit_incref_cycle_free(_M0L6_2atmpS472);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
      _M0FP46RiantR8snn__mbt8examples13stp__timestep44moonbit__test__driver__internal__do__execute(_M0L12async__testsS441, _M0L6_2atmpS472, _M0L6_2atmpS473);
      moonbit_decref_cycle_free(_M0L6_2atmpS472);
      _M0L6_2atmpS474 = _M0L2__S445 + 1;
      _M0L2__S445 = _M0L6_2atmpS474;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS444);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\stp_timestep\\__generated_driver_for_internal_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples13stp__timestep28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples13stp__timestep34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS441);
  moonbit_decref_cycle_free(_M0L12async__testsS441);
  moonbit_flush_cycles();
  return 0;
}