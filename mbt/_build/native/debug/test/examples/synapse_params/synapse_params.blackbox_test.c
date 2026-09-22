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

struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c822;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TWRPC15error5ErrorEs;

struct _M0TPB4Show;

struct _M0TWssbEu;

struct _M0TUsiE;

struct _M0TPB13StringBuilder;

struct _M0TPB5ArrayGfE;

struct _M0TPB17FloatingDecimal64;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0BTPB6Logger;

struct _M0TP26RiantR8snn__mbt16DoubleExpSynapse;

struct _M0BTPB4Show;

struct _M0TWuEu;

struct _M0TP26RiantR8snn__mbt16SingleExpSynapse;

struct _M0TPC16string10StringView;

struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c827;

struct _M0KTPB6LoggerTPB13StringBuilder;

struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TPB6Logger;

struct _M0TP26RiantR8snn__mbt20DoubleExpSynapseVars;

struct _M0TPB5ArrayGUsiEE;

struct _M0TP26RiantR8snn__mbt20SingleExpSynapseVars;

struct _M0TPB5ArrayGsE;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE2Ok;

struct _M0TWEu;

struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

struct _M0TPB19MulShiftAll64Result;

struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0TP26RiantR8snn__mbt18CurrentSynapseVars;

struct _M0TP26RiantR8snn__mbt16DeltaSynapseVars;

struct _M0TP26RiantR8snn__mbt14CurrentSynapse;

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

struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c822 {
  int32_t(* code)(struct _M0TWEu*);
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

struct _M0TP26RiantR8snn__mbt16DoubleExpSynapse {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  float $5;
  float $6;
  float $7;
  
};

struct _M0BTPB4Show {
  int32_t(* $method_0)(void*, struct _M0TPB6Logger);
  moonbit_string_t(* $method_1)(void*);
  
};

struct _M0TWuEu {
  int32_t(* code)(struct _M0TWuEu*, int32_t);
  
};

struct _M0TP26RiantR8snn__mbt16SingleExpSynapse {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  float $5;
  
};

struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c827 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
};

struct _M0KTPB6LoggerTPB13StringBuilder {
  struct _M0BTPB6Logger* $0;
  void* $1;
  
};

struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
};

struct _M0TP26RiantR8snn__mbt20DoubleExpSynapseVars {
  int32_t $0;
  struct _M0TPB5ArrayGfE* $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  struct _M0TPB5ArrayGfE* $4;
  
};

struct _M0TPB5ArrayGUsiEE {
  struct _M0TUsiE** $0;
  int32_t $1;
  
};

struct _M0TP26RiantR8snn__mbt20SingleExpSynapseVars {
  int32_t $0;
  struct _M0TPB5ArrayGfE* $1;
  struct _M0TPB5ArrayGfE* $2;
  
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

struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
};

struct _M0TP26RiantR8snn__mbt18CurrentSynapseVars {
  int32_t $0;
  struct _M0TPB5ArrayGfE* $1;
  struct _M0TPB5ArrayGfE* $2;
  
};

struct _M0TP26RiantR8snn__mbt16DeltaSynapseVars {
  int32_t $0;
  struct _M0TPB5ArrayGfE* $1;
  struct _M0TPB5ArrayGfE* $2;
  
};

struct _M0TP26RiantR8snn__mbt14CurrentSynapse {
  float $0;
  float $1;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS834(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS827(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS822(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS799(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S792(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples31synapse__params__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

int32_t _M0FP26RiantR8snn__mbt33double__exp__conductance__current(
  struct _M0TP26RiantR8snn__mbt20DoubleExpSynapseVars*,
  struct _M0TP26RiantR8snn__mbt16DoubleExpSynapse*,
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0FP26RiantR8snn__mbt29conductance__synapse__current(
  struct _M0TP26RiantR8snn__mbt20SingleExpSynapseVars*,
  struct _M0TP26RiantR8snn__mbt16SingleExpSynapse*,
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0FP26RiantR8snn__mbt25current__synapse__current(
  struct _M0TP26RiantR8snn__mbt18CurrentSynapseVars*,
  struct _M0TPB5ArrayGfE*
);

int32_t _M0FP26RiantR8snn__mbt22current__synapse__step(
  struct _M0TP26RiantR8snn__mbt18CurrentSynapseVars*,
  struct _M0TP26RiantR8snn__mbt14CurrentSynapse*,
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGfE*,
  float
);

int32_t _M0FP26RiantR8snn__mbt26double__exp__synapse__step(
  struct _M0TP26RiantR8snn__mbt20DoubleExpSynapseVars*,
  struct _M0TP26RiantR8snn__mbt16DoubleExpSynapse*,
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGfE*,
  float
);

int32_t _M0FP26RiantR8snn__mbt26single__exp__synapse__step(
  struct _M0TP26RiantR8snn__mbt20SingleExpSynapseVars*,
  struct _M0TP26RiantR8snn__mbt16SingleExpSynapse*,
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGfE*,
  float
);

int32_t _M0FP26RiantR8snn__mbt20delta__synapse__step(
  struct _M0TP26RiantR8snn__mbt16DeltaSynapseVars*,
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGfE*
);

struct _M0TP26RiantR8snn__mbt18CurrentSynapseVars* _M0MP26RiantR8snn__mbt18CurrentSynapseVars3new(
  int32_t
);

struct _M0TP26RiantR8snn__mbt14CurrentSynapse* _M0MP26RiantR8snn__mbt14CurrentSynapse3new(
  
);

struct _M0TP26RiantR8snn__mbt20DoubleExpSynapseVars* _M0MP26RiantR8snn__mbt20DoubleExpSynapseVars3new(
  int32_t
);

struct _M0TP26RiantR8snn__mbt16DoubleExpSynapse* _M0MP26RiantR8snn__mbt16DoubleExpSynapse3new(
  
);

struct _M0TP26RiantR8snn__mbt20SingleExpSynapseVars* _M0MP26RiantR8snn__mbt20SingleExpSynapseVars3new(
  int32_t
);

struct _M0TP26RiantR8snn__mbt16SingleExpSynapse* _M0MP26RiantR8snn__mbt16SingleExpSynapse3new(
  
);

struct _M0TP26RiantR8snn__mbt16DeltaSynapseVars* _M0MP26RiantR8snn__mbt16DeltaSynapseVars3new(
  int32_t
);

int32_t _M0MP26RiantR8snn__mbt12DeltaSynapse3new();

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float);

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(int32_t, float);

int32_t _M0MPC15array5Array3setGfE(struct _M0TPB5ArrayGfE*, int32_t, float);

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

struct { int32_t rc; uint32_t meta; uint16_t const data[119]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 118, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 115, 121, 110, 97, 112, 115, 101, 
    95, 112, 97, 114, 97, 109, 115, 95, 98, 108, 97, 99, 107, 98, 111, 
    120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 
    84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 
    114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 46, 77, 111, 
    111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 
    114, 73, 110, 116, 101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 
    111, 114, 0
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

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[121]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 120, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 115, 121, 110, 97, 112, 115, 101, 
    95, 112, 97, 114, 97, 109, 115, 95, 98, 108, 97, 99, 107, 98, 111, 
    120, 95, 116, 101, 115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 
    84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 
    114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 46, 77, 
    111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 
    101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 
    84, 101, 115, 116, 0
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS834$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS834
  };

uint32_t const moonbit_layout_table_data[57] =
  {
    sizeof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c822)
    / 4, 1,
    offsetof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c822, $1)
    / 4
    * 2,
    sizeof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c827)
    / 4, 1,
    offsetof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c827, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
    / 4
    * 2, sizeof(struct _M0TPB5ArrayGUsiEE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGUsiEE, $0) / 4 * 2,
    sizeof(struct _M0TUsiE) / 4, 1, offsetof(struct _M0TUsiE, $0) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGsE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGsE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt18CurrentSynapseVars) / 4, 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18CurrentSynapseVars, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18CurrentSynapseVars, $2) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt20DoubleExpSynapseVars) / 4, 
    4,
    offsetof(struct _M0TP26RiantR8snn__mbt20DoubleExpSynapseVars, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt20DoubleExpSynapseVars, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt20DoubleExpSynapseVars, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt20DoubleExpSynapseVars, $4) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt20SingleExpSynapseVars) / 4, 
    2,
    offsetof(struct _M0TP26RiantR8snn__mbt20SingleExpSynapseVars, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt20SingleExpSynapseVars, $2) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt16DeltaSynapseVars) / 4, 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16DeltaSynapseVars, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt16DeltaSynapseVars, $2) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS1966
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS855,
  moonbit_string_t _M0L8filenameS824,
  int32_t _M0L5indexS826
) {
  struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c822* _closure_1987;
  struct _M0TWEu* _M0L13handle__startS822;
  struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c827* _closure_1988;
  struct _M0TWssbEu* _M0L14handle__resultS827;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS834;
  void* _M0L11_2atry__errS849;
  struct moonbit_result_0 _tmp_1990;
  int32_t _handle__error__result_1991;
  int32_t _M0L6_2atmpS1954;
  void* _M0L3errS850;
  moonbit_string_t _M0L4nameS852;
  struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS853;
  moonbit_string_t _M0L7_2anameS854;
  int32_t _M0L6_2acntS1981;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS824);
  _closure_1987
  = (struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c822*)moonbit_malloc(sizeof(struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c822));
  Moonbit_object_header(_closure_1987)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_1987->code
  = &_M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS822;
  _closure_1987->$0 = _M0L5indexS826;
  _closure_1987->$1 = _M0L8filenameS824;
  _M0L13handle__startS822 = (struct _M0TWEu*)_closure_1987;
  moonbit_incref_cycle_free(_M0L8filenameS824);
  _closure_1988
  = (struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c827*)moonbit_malloc(sizeof(struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c827));
  Moonbit_object_header(_closure_1988)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_1988->code
  = &_M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS827;
  _closure_1988->$0 = _M0L5indexS826;
  _closure_1988->$1 = _M0L8filenameS824;
  _M0L14handle__resultS827 = (struct _M0TWssbEu*)_closure_1988;
  _M0L17error__to__stringS834
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS834$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _tmp_1990
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS855, _M0L8filenameS824, _M0L5indexS826, _M0L13handle__startS822, _M0L14handle__resultS827, _M0L17error__to__stringS834);
  if (_tmp_1990.tag) {
    int32_t const _M0L5_2aokS1963 = _tmp_1990.data.ok;
    _handle__error__result_1991 = _M0L5_2aokS1963;
  } else {
    void* const _M0L6_2aerrS1964 = _tmp_1990.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS834);
    moonbit_decref_cycle_free(_M0L13handle__startS822);
    _M0L11_2atry__errS849 = _M0L6_2aerrS1964;
    goto join_848;
  }
  if (_handle__error__result_1991) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS834);
    moonbit_decref_cycle_free(_M0L13handle__startS822);
    _M0L6_2atmpS1954 = 1;
  } else {
    struct moonbit_result_0 _tmp_1992;
    int32_t _handle__error__result_1993;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
    _tmp_1992
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS855, _M0L8filenameS824, _M0L5indexS826, _M0L13handle__startS822, _M0L14handle__resultS827, _M0L17error__to__stringS834);
    if (_tmp_1992.tag) {
      int32_t const _M0L5_2aokS1961 = _tmp_1992.data.ok;
      _handle__error__result_1993 = _M0L5_2aokS1961;
    } else {
      void* const _M0L6_2aerrS1962 = _tmp_1992.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS834);
      moonbit_decref_cycle_free(_M0L13handle__startS822);
      _M0L11_2atry__errS849 = _M0L6_2aerrS1962;
      goto join_848;
    }
    if (_handle__error__result_1993) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS834);
      moonbit_decref_cycle_free(_M0L13handle__startS822);
      _M0L6_2atmpS1954 = 1;
    } else {
      struct moonbit_result_0 _tmp_1994;
      int32_t _handle__error__result_1995;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
      _tmp_1994
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS855, _M0L8filenameS824, _M0L5indexS826, _M0L13handle__startS822, _M0L14handle__resultS827, _M0L17error__to__stringS834);
      if (_tmp_1994.tag) {
        int32_t const _M0L5_2aokS1959 = _tmp_1994.data.ok;
        _handle__error__result_1995 = _M0L5_2aokS1959;
      } else {
        void* const _M0L6_2aerrS1960 = _tmp_1994.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS834);
        moonbit_decref_cycle_free(_M0L13handle__startS822);
        _M0L11_2atry__errS849 = _M0L6_2aerrS1960;
        goto join_848;
      }
      if (_handle__error__result_1995) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS834);
        moonbit_decref_cycle_free(_M0L13handle__startS822);
        _M0L6_2atmpS1954 = 1;
      } else {
        struct moonbit_result_0 _tmp_1996;
        int32_t _handle__error__result_1997;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
        _tmp_1996
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS855, _M0L8filenameS824, _M0L5indexS826, _M0L13handle__startS822, _M0L14handle__resultS827, _M0L17error__to__stringS834);
        if (_tmp_1996.tag) {
          int32_t const _M0L5_2aokS1957 = _tmp_1996.data.ok;
          _handle__error__result_1997 = _M0L5_2aokS1957;
        } else {
          void* const _M0L6_2aerrS1958 = _tmp_1996.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS834);
          moonbit_decref_cycle_free(_M0L13handle__startS822);
          _M0L11_2atry__errS849 = _M0L6_2aerrS1958;
          goto join_848;
        }
        if (_handle__error__result_1997) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS834);
          moonbit_decref_cycle_free(_M0L13handle__startS822);
          _M0L6_2atmpS1954 = 1;
        } else {
          struct moonbit_result_0 _tmp_1998;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
          _tmp_1998
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS855, _M0L8filenameS824, _M0L5indexS826, _M0L13handle__startS822, _M0L14handle__resultS827, _M0L17error__to__stringS834);
          moonbit_decref_cycle_free(_M0L13handle__startS822);
          moonbit_decref_cycle_free(_M0L17error__to__stringS834);
          if (_tmp_1998.tag) {
            int32_t const _M0L5_2aokS1955 = _tmp_1998.data.ok;
            _M0L6_2atmpS1954 = _M0L5_2aokS1955;
          } else {
            void* const _M0L6_2aerrS1956 = _tmp_1998.data.err;
            _M0L11_2atry__errS849 = _M0L6_2aerrS1956;
            goto join_848;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS1954) {
    void* _M0L134RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1965 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L134RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1965)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L134RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1965)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS849
    = _M0L134RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS1965;
    goto join_848;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS827);
  }
  goto joinlet_1989;
  join_848:;
  _M0L3errS850 = _M0L11_2atry__errS849;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS853
  = (struct _M0DTPC15error5Error134RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS850;
  _M0L7_2anameS854 = _M0L36_2aMoonBitTestDriverInternalSkipTestS853->$0;
  _M0L6_2acntS1981
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS853));
  if (_M0L6_2acntS1981 > 1) {
    int32_t _M0L11_2anew__cntS1982 = _M0L6_2acntS1981 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS853), _M0L11_2anew__cntS1982);
    moonbit_incref_cycle_free(_M0L7_2anameS854);
  } else if (_M0L6_2acntS1981 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS853);
  }
  _M0L4nameS852 = _M0L7_2anameS854;
  goto join_851;
  goto joinlet_1999;
  join_851:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS827(_M0L14handle__resultS827, _M0L4nameS852, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS827);
  moonbit_decref_cycle_free(_M0L4nameS852);
  joinlet_1999:;
  joinlet_1989:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS834(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS1953,
  void* _M0L3errS835
) {
  void* _M0L1eS837;
  moonbit_string_t _M0L1eS839;
  moonbit_string_t _result_2002;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS835)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS840 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS835;
      moonbit_string_t _M0L4_2aeS841 = _M0L10_2aFailureS840->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS841);
      _M0L1eS839 = _M0L4_2aeS841;
      goto join_838;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS842 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS835;
      moonbit_string_t _M0L4_2aeS843 = _M0L15_2aInspectErrorS842->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS843);
      _M0L1eS839 = _M0L4_2aeS843;
      goto join_838;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS844 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS835;
      moonbit_string_t _M0L4_2aeS845 = _M0L16_2aSnapshotErrorS844->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS845);
      _M0L1eS839 = _M0L4_2aeS845;
      goto join_838;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS846 =
        (struct _M0DTPC15error5Error132RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS835;
      moonbit_string_t _M0L4_2aeS847 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS846->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS847);
      _M0L1eS839 = _M0L4_2aeS847;
      goto join_838;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS835);
      _M0L1eS837 = _M0L3errS835;
      goto join_836;
      break;
    }
  }
  join_838:;
  return _M0L1eS839;
  join_836:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _result_2002 = _M0FP15Error10to__string(_M0L1eS837);
  moonbit_decref_cycle_free(_M0L1eS837);
  return _result_2002;
}

int32_t _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS827(
  struct _M0TWssbEu* _M0L6_2aenvS1950,
  moonbit_string_t _M0L10__testnameS828,
  moonbit_string_t _M0L7messageS829,
  int32_t _M0L7skippedS830
) {
  struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c827* _M0L14_2acasted__envS1951;
  moonbit_string_t _M0L8filenameS824;
  int32_t _M0L5indexS826;
  moonbit_string_t _M0L10file__nameS831;
  moonbit_string_t _M0L7messageS832;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS833;
  moonbit_string_t _M0L6_2atmpS1952;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1951
  = (struct _M0R135_24RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c827*)_M0L6_2aenvS1950;
  _M0L8filenameS824 = _M0L14_2acasted__envS1951->$1;
  _M0L5indexS826 = _M0L14_2acasted__envS1951->$0;
  if (!_M0L7skippedS830 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS831
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS824, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS832
  = _M0MPC16string6String14escape_2einner(_M0L7messageS829, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS833
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS833, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS833, _M0L10file__nameS831);
  moonbit_decref_cycle_free(_M0L10file__nameS831);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS833, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS833, _M0L5indexS826);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS833, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS833, _M0L7messageS832);
  moonbit_decref_cycle_free(_M0L7messageS832);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS833, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1952
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS833);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS833);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1952);
  moonbit_decref_cycle_free(_M0L6_2atmpS1952);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS822(
  struct _M0TWEu* _M0L6_2aenvS1947
) {
  struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c822* _M0L14_2acasted__envS1948;
  moonbit_string_t _M0L8filenameS824;
  int32_t _M0L5indexS826;
  moonbit_string_t _M0L10file__nameS823;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS825;
  moonbit_string_t _M0L6_2atmpS1949;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS1948
  = (struct _M0R134_24RiantR_2fsnn__mbt_2fexamples_2fsynapse__params__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c822*)_M0L6_2aenvS1947;
  _M0L8filenameS824 = _M0L14_2acasted__envS1948->$1;
  _M0L5indexS826 = _M0L14_2acasted__envS1948->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS823
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS824, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS825
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS825, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS825, _M0L10file__nameS823);
  moonbit_decref_cycle_free(_M0L10file__nameS823);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS825, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS825, _M0L5indexS826);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS825, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1949
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS825);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS825);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS1949);
  moonbit_decref_cycle_free(_M0L6_2atmpS1949);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S792;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS799;
  struct _M0TUsiE** _M0L6_2atmpS1946;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS806;
  moonbit_string_t* _M0L9cli__argsS807;
  moonbit_string_t _M0L6_2atmpS1945;
  moonbit_string_t _M0L6_2atmpS1944;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS808;
  int32_t _M0L7_2abindS809;
  moonbit_string_t* _M0L7_2abindS810;
  int32_t _M0L6_2acntS1983;
  int32_t _M0L2__S811;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S792 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS799 = 0;
  _M0L6_2atmpS1946 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS806
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS806)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS806->$0 = _M0L6_2atmpS1946;
  _M0L16file__and__indexS806->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS807
  = _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS807)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS1945 = (moonbit_string_t)_M0L9cli__argsS807[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS1945);
  moonbit_decref_cycle_free(_M0L9cli__argsS807);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1944
  = _M0MP46RiantR8snn__mbt8examples31synapse__params__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS1945);
  moonbit_decref_cycle_free(_M0L6_2atmpS1945);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS808
  = _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS799(_M0L51moonbit__test__driver__internal__split__mbt__stringS799, _M0L6_2atmpS1944, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS1944);
  _M0L7_2abindS809 = _M0L10test__argsS808->$1;
  _M0L7_2abindS810 = _M0L10test__argsS808->$0;
  _M0L6_2acntS1983
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS808));
  if (_M0L6_2acntS1983 > 1) {
    int32_t _M0L11_2anew__cntS1984 = _M0L6_2acntS1983 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS808), _M0L11_2anew__cntS1984);
    moonbit_incref_cycle_free(_M0L7_2abindS810);
  } else if (_M0L6_2acntS1983 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS808);
  }
  _M0L2__S811 = 0;
  while (1) {
    if (_M0L2__S811 < _M0L7_2abindS809) {
      moonbit_string_t _M0L3argS812 =
        (moonbit_string_t)_M0L7_2abindS810[_M0L2__S811];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS813;
      moonbit_string_t _M0L4fileS814;
      moonbit_string_t _M0L5rangeS815;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS816;
      moonbit_string_t _M0L6_2atmpS1942;
      int32_t _M0L5startS817;
      moonbit_string_t _M0L6_2atmpS1941;
      int32_t _M0L3endS818;
      int32_t _M0L1iS819;
      int32_t _M0L6_2atmpS1943;
      moonbit_incref_cycle_free(_M0L3argS812);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS813
      = _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS799(_M0L51moonbit__test__driver__internal__split__mbt__stringS799, _M0L3argS812, 58);
      moonbit_decref_cycle_free(_M0L3argS812);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS814
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS813, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS815
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS813, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS813);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS816
      = _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS799(_M0L51moonbit__test__driver__internal__split__mbt__stringS799, _M0L5rangeS815, 45);
      moonbit_decref_cycle_free(_M0L5rangeS815);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1942
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS816, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS817
      = _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S792(_M0L45moonbit__test__driver__internal__parse__int__S792, _M0L6_2atmpS1942);
      moonbit_decref_cycle_free(_M0L6_2atmpS1942);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS1941
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS816, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS816);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS818
      = _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S792(_M0L45moonbit__test__driver__internal__parse__int__S792, _M0L6_2atmpS1941);
      moonbit_decref_cycle_free(_M0L6_2atmpS1941);
      _M0L1iS819 = _M0L5startS817;
      while (1) {
        if (_M0L1iS819 < _M0L3endS818) {
          struct _M0TUsiE* _M0L8_2atupleS1939;
          int32_t _M0L6_2atmpS1940;
          moonbit_incref_cycle_free(_M0L4fileS814);
          _M0L8_2atupleS1939
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS1939)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS1939->$0 = _M0L4fileS814;
          _M0L8_2atupleS1939->$1 = _M0L1iS819;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS806, _M0L8_2atupleS1939);
          _M0L6_2atmpS1940 = _M0L1iS819 + 1;
          _M0L1iS819 = _M0L6_2atmpS1940;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS814);
        }
        break;
      }
      _M0L6_2atmpS1943 = _M0L2__S811 + 1;
      _M0L2__S811 = _M0L6_2atmpS1943;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS810);
    }
    break;
  }
  return _M0L16file__and__indexS806;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS799(
  int32_t _M0L6_2aenvS1920,
  moonbit_string_t _M0L1sS800,
  int32_t _M0L3sepS801
) {
  moonbit_string_t* _M0L6_2atmpS1938;
  struct _M0TPB5ArrayGsE* _M0L3resS802;
  struct _M0TPB8MutLocalGiE* _M0L1iS803;
  struct _M0TPB8MutLocalGiE* _M0L5startS804;
  int32_t _M0L3valS1933;
  int32_t _M0L6_2atmpS1934;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS1938 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS802
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS802)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS802->$0 = _M0L6_2atmpS1938;
  _M0L3resS802->$1 = 0;
  _M0L1iS803
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS803)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS803->$0 = 0;
  _M0L5startS804
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS804)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS804->$0 = 0;
  while (1) {
    int32_t _M0L3valS1921 = _M0L1iS803->$0;
    int32_t _M0L6_2atmpS1922 = Moonbit_array_length(_M0L1sS800);
    if (_M0L3valS1921 < _M0L6_2atmpS1922) {
      int32_t _M0L3valS1925 = _M0L1iS803->$0;
      int32_t _M0L6_2atmpS1924;
      int32_t _M0L6_2atmpS1923;
      int32_t _M0L3valS1932;
      int32_t _M0L6_2atmpS1931;
      if (
        _M0L3valS1925 < 0
        || _M0L3valS1925 >= Moonbit_array_length(_M0L1sS800)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1924 = _M0L1sS800[_M0L3valS1925];
      _M0L6_2atmpS1923 = _M0L6_2atmpS1924;
      if (_M0L6_2atmpS1923 == _M0L3sepS801) {
        int32_t _M0L3valS1927 = _M0L5startS804->$0;
        int32_t _M0L3valS1928 = _M0L1iS803->$0;
        moonbit_string_t _M0L6_2atmpS1926;
        int32_t _M0L3valS1930;
        int32_t _M0L6_2atmpS1929;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS1926
        = _M0MPC16string6String17unsafe__substring(_M0L1sS800, _M0L3valS1927, _M0L3valS1928);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS802, _M0L6_2atmpS1926);
        _M0L3valS1930 = _M0L1iS803->$0;
        _M0L6_2atmpS1929 = _M0L3valS1930 + 1;
        _M0L5startS804->$0 = _M0L6_2atmpS1929;
      }
      _M0L3valS1932 = _M0L1iS803->$0;
      _M0L6_2atmpS1931 = _M0L3valS1932 + 1;
      _M0L1iS803->$0 = _M0L6_2atmpS1931;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS803);
    }
    break;
  }
  _M0L3valS1933 = _M0L5startS804->$0;
  _M0L6_2atmpS1934 = Moonbit_array_length(_M0L1sS800);
  if (_M0L3valS1933 < _M0L6_2atmpS1934) {
    int32_t _M0L3valS1936 = _M0L5startS804->$0;
    int32_t _M0L6_2atmpS1937;
    moonbit_string_t _M0L6_2atmpS1935;
    moonbit_decref_cycle_free(_M0L5startS804);
    _M0L6_2atmpS1937 = Moonbit_array_length(_M0L1sS800);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS1935
    = _M0MPC16string6String17unsafe__substring(_M0L1sS800, _M0L3valS1936, _M0L6_2atmpS1937);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS802, _M0L6_2atmpS1935);
  } else {
    moonbit_decref_cycle_free(_M0L5startS804);
  }
  return _M0L3resS802;
}

int32_t _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S792(
  int32_t _M0L6_2aenvS1913,
  moonbit_string_t _M0L1sS793
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS794;
  int32_t _M0L3lenS795;
  int32_t _M0L7_2abindS796;
  int32_t _M0L1iS797;
  int32_t _result_2007;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS794
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS794)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS794->$0 = 0;
  _M0L3lenS795 = Moonbit_array_length(_M0L1sS793);
  _M0L7_2abindS796 = 0;
  _M0L1iS797 = _M0L7_2abindS796;
  while (1) {
    if (_M0L1iS797 < _M0L3lenS795) {
      int32_t _M0L3valS1918 = _M0L3resS794->$0;
      int32_t _M0L6_2atmpS1915 = _M0L3valS1918 * 10;
      int32_t _M0L6_2atmpS1917;
      int32_t _M0L6_2atmpS1916;
      int32_t _M0L6_2atmpS1914;
      int32_t _M0L6_2atmpS1919;
      if (_M0L1iS797 < 0 || _M0L1iS797 >= Moonbit_array_length(_M0L1sS793)) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1917 = _M0L1sS793[_M0L1iS797];
      _M0L6_2atmpS1916 = _M0L6_2atmpS1917 - 48;
      _M0L6_2atmpS1914 = _M0L6_2atmpS1915 + _M0L6_2atmpS1916;
      _M0L3resS794->$0 = _M0L6_2atmpS1914;
      _M0L6_2atmpS1919 = _M0L1iS797 + 1;
      _M0L1iS797 = _M0L6_2atmpS1919;
      continue;
    }
    break;
  }
  _result_2007 = _M0L3resS794->$0;
  moonbit_decref_cycle_free(_M0L3resS794);
  return _result_2007;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples31synapse__params__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS791
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS791);
  return _M0L4selfS791;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S761,
  moonbit_string_t _M0L12_2adiscard__S762,
  int32_t _M0L12_2adiscard__S763,
  struct _M0TWEu* _M0L12_2adiscard__S764,
  struct _M0TWssbEu* _M0L12_2adiscard__S765,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S766
) {
  struct moonbit_result_0 _result_2008;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _result_2008.tag = 1;
  _result_2008.data.ok = 0;
  return _result_2008;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S767,
  moonbit_string_t _M0L12_2adiscard__S768,
  int32_t _M0L12_2adiscard__S769,
  struct _M0TWEu* _M0L12_2adiscard__S770,
  struct _M0TWssbEu* _M0L12_2adiscard__S771,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S772
) {
  struct moonbit_result_0 _result_2009;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _result_2009.tag = 1;
  _result_2009.data.ok = 0;
  return _result_2009;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S773,
  moonbit_string_t _M0L12_2adiscard__S774,
  int32_t _M0L12_2adiscard__S775,
  struct _M0TWEu* _M0L12_2adiscard__S776,
  struct _M0TWssbEu* _M0L12_2adiscard__S777,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S778
) {
  struct moonbit_result_0 _result_2010;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _result_2010.tag = 1;
  _result_2010.data.ok = 0;
  return _result_2010;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S779,
  moonbit_string_t _M0L12_2adiscard__S780,
  int32_t _M0L12_2adiscard__S781,
  struct _M0TWEu* _M0L12_2adiscard__S782,
  struct _M0TWssbEu* _M0L12_2adiscard__S783,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S784
) {
  struct moonbit_result_0 _result_2011;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _result_2011.tag = 1;
  _result_2011.data.ok = 0;
  return _result_2011;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S785,
  moonbit_string_t _M0L12_2adiscard__S786,
  int32_t _M0L12_2adiscard__S787,
  struct _M0TWEu* _M0L12_2adiscard__S788,
  struct _M0TWssbEu* _M0L12_2adiscard__S789,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S790
) {
  struct moonbit_result_0 _result_2012;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _result_2012.tag = 1;
  _result_2012.data.ok = 0;
  return _result_2012;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S760
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt33double__exp__conductance__current(
  struct _M0TP26RiantR8snn__mbt20DoubleExpSynapseVars* _M0L4varsS738,
  struct _M0TP26RiantR8snn__mbt16DoubleExpSynapse* _M0L5paramS742,
  struct _M0TPB5ArrayGfE* _M0L1vS741,
  struct _M0TPB5ArrayGfE* _M0L9syn__currS740
) {
  int32_t _M0L1nS737;
  struct _M0TPB8MutLocalGiE* _M0L1iS739;
  #line 333 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  _M0L1nS737 = _M0L4varsS738->$0;
  _M0L1iS739
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS739)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS739->$0 = 0;
  while (1) {
    int32_t _M0L3valS1888 = _M0L1iS739->$0;
    if (_M0L3valS1888 < _M0L1nS737) {
      int32_t _M0L3valS1889 = _M0L1iS739->$0;
      struct _M0TPB5ArrayGfE* _M0L2geS1909 = _M0L4varsS738->$1;
      int32_t _M0L3valS1910 = _M0L1iS739->$0;
      float _M0L6_2atmpS1904;
      int32_t _M0L3valS1908;
      float _M0L6_2atmpS1906;
      float _M0L4e__eS1907;
      float _M0L6_2atmpS1905;
      float _M0L6_2atmpS1902;
      float _M0L7gsyn__eS1903;
      float _M0L6_2atmpS1891;
      struct _M0TPB5ArrayGfE* _M0L2giS1900;
      int32_t _M0L3valS1901;
      float _M0L6_2atmpS1895;
      int32_t _M0L3valS1899;
      float _M0L6_2atmpS1897;
      float _M0L4e__iS1898;
      float _M0L6_2atmpS1896;
      float _M0L6_2atmpS1893;
      float _M0L7gsyn__iS1894;
      float _M0L6_2atmpS1892;
      float _M0L6_2atmpS1890;
      int32_t _M0L3valS1912;
      int32_t _M0L6_2atmpS1911;
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1904
      = _M0MPC15array5Array2atGfE(_M0L2geS1909, _M0L3valS1910);
      _M0L3valS1908 = _M0L1iS739->$0;
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1906 = _M0MPC15array5Array2atGfE(_M0L1vS741, _M0L3valS1908);
      _M0L4e__eS1907 = _M0L5paramS742->$5;
      _M0L6_2atmpS1905 = _M0L6_2atmpS1906 - _M0L4e__eS1907;
      _M0L6_2atmpS1902 = _M0L6_2atmpS1904 * _M0L6_2atmpS1905;
      _M0L7gsyn__eS1903 = _M0L5paramS742->$6;
      _M0L6_2atmpS1891 = _M0L6_2atmpS1902 * _M0L7gsyn__eS1903;
      _M0L2giS1900 = _M0L4varsS738->$2;
      _M0L3valS1901 = _M0L1iS739->$0;
      #line 343 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1895
      = _M0MPC15array5Array2atGfE(_M0L2giS1900, _M0L3valS1901);
      _M0L3valS1899 = _M0L1iS739->$0;
      #line 343 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1897 = _M0MPC15array5Array2atGfE(_M0L1vS741, _M0L3valS1899);
      _M0L4e__iS1898 = _M0L5paramS742->$4;
      _M0L6_2atmpS1896 = _M0L6_2atmpS1897 - _M0L4e__iS1898;
      _M0L6_2atmpS1893 = _M0L6_2atmpS1895 * _M0L6_2atmpS1896;
      _M0L7gsyn__iS1894 = _M0L5paramS742->$7;
      _M0L6_2atmpS1892 = _M0L6_2atmpS1893 * _M0L7gsyn__iS1894;
      _M0L6_2atmpS1890 = _M0L6_2atmpS1891 + _M0L6_2atmpS1892;
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS740, _M0L3valS1889, _M0L6_2atmpS1890);
      _M0L3valS1912 = _M0L1iS739->$0;
      _M0L6_2atmpS1911 = _M0L3valS1912 + 1;
      _M0L1iS739->$0 = _M0L6_2atmpS1911;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS739);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt29conductance__synapse__current(
  struct _M0TP26RiantR8snn__mbt20SingleExpSynapseVars* _M0L4varsS731,
  struct _M0TP26RiantR8snn__mbt16SingleExpSynapse* _M0L5paramS735,
  struct _M0TPB5ArrayGfE* _M0L1vS734,
  struct _M0TPB5ArrayGfE* _M0L9syn__currS733
) {
  int32_t _M0L1nS730;
  struct _M0TPB8MutLocalGiE* _M0L1iS732;
  #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  _M0L1nS730 = _M0L4varsS731->$0;
  _M0L1iS732
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS732)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS732->$0 = 0;
  while (1) {
    int32_t _M0L3valS1863 = _M0L1iS732->$0;
    if (_M0L3valS1863 < _M0L1nS730) {
      int32_t _M0L3valS1864 = _M0L1iS732->$0;
      struct _M0TPB5ArrayGfE* _M0L2geS1884 = _M0L4varsS731->$1;
      int32_t _M0L3valS1885 = _M0L1iS732->$0;
      float _M0L6_2atmpS1879;
      int32_t _M0L3valS1883;
      float _M0L6_2atmpS1881;
      float _M0L4e__eS1882;
      float _M0L6_2atmpS1880;
      float _M0L6_2atmpS1877;
      float _M0L7gsyn__eS1878;
      float _M0L6_2atmpS1866;
      struct _M0TPB5ArrayGfE* _M0L2giS1875;
      int32_t _M0L3valS1876;
      float _M0L6_2atmpS1870;
      int32_t _M0L3valS1874;
      float _M0L6_2atmpS1872;
      float _M0L4e__iS1873;
      float _M0L6_2atmpS1871;
      float _M0L6_2atmpS1868;
      float _M0L7gsyn__iS1869;
      float _M0L6_2atmpS1867;
      float _M0L6_2atmpS1865;
      int32_t _M0L3valS1887;
      int32_t _M0L6_2atmpS1886;
      #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1879
      = _M0MPC15array5Array2atGfE(_M0L2geS1884, _M0L3valS1885);
      _M0L3valS1883 = _M0L1iS732->$0;
      #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1881 = _M0MPC15array5Array2atGfE(_M0L1vS734, _M0L3valS1883);
      _M0L4e__eS1882 = _M0L5paramS735->$3;
      _M0L6_2atmpS1880 = _M0L6_2atmpS1881 - _M0L4e__eS1882;
      _M0L6_2atmpS1877 = _M0L6_2atmpS1879 * _M0L6_2atmpS1880;
      _M0L7gsyn__eS1878 = _M0L5paramS735->$4;
      _M0L6_2atmpS1866 = _M0L6_2atmpS1877 * _M0L7gsyn__eS1878;
      _M0L2giS1875 = _M0L4varsS731->$2;
      _M0L3valS1876 = _M0L1iS732->$0;
      #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1870
      = _M0MPC15array5Array2atGfE(_M0L2giS1875, _M0L3valS1876);
      _M0L3valS1874 = _M0L1iS732->$0;
      #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1872 = _M0MPC15array5Array2atGfE(_M0L1vS734, _M0L3valS1874);
      _M0L4e__iS1873 = _M0L5paramS735->$2;
      _M0L6_2atmpS1871 = _M0L6_2atmpS1872 - _M0L4e__iS1873;
      _M0L6_2atmpS1868 = _M0L6_2atmpS1870 * _M0L6_2atmpS1871;
      _M0L7gsyn__iS1869 = _M0L5paramS735->$5;
      _M0L6_2atmpS1867 = _M0L6_2atmpS1868 * _M0L7gsyn__iS1869;
      _M0L6_2atmpS1865 = _M0L6_2atmpS1866 + _M0L6_2atmpS1867;
      #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS733, _M0L3valS1864, _M0L6_2atmpS1865);
      _M0L3valS1887 = _M0L1iS732->$0;
      _M0L6_2atmpS1886 = _M0L3valS1887 + 1;
      _M0L1iS732->$0 = _M0L6_2atmpS1886;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS732);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25current__synapse__current(
  struct _M0TP26RiantR8snn__mbt18CurrentSynapseVars* _M0L4varsS726,
  struct _M0TPB5ArrayGfE* _M0L9syn__currS728
) {
  int32_t _M0L1nS725;
  struct _M0TPB8MutLocalGiE* _M0L1iS727;
  #line 302 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  _M0L1nS725 = _M0L4varsS726->$0;
  _M0L1iS727
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS727)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS727->$0 = 0;
  while (1) {
    int32_t _M0L3valS1851 = _M0L1iS727->$0;
    if (_M0L3valS1851 < _M0L1nS725) {
      int32_t _M0L3valS1852 = _M0L1iS727->$0;
      struct _M0TPB5ArrayGfE* _M0L2geS1859 = _M0L4varsS726->$1;
      int32_t _M0L3valS1860 = _M0L1iS727->$0;
      float _M0L6_2atmpS1855;
      struct _M0TPB5ArrayGfE* _M0L2giS1857;
      int32_t _M0L3valS1858;
      float _M0L6_2atmpS1856;
      float _M0L6_2atmpS1854;
      float _M0L6_2atmpS1853;
      int32_t _M0L3valS1862;
      int32_t _M0L6_2atmpS1861;
      #line 309 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1855
      = _M0MPC15array5Array2atGfE(_M0L2geS1859, _M0L3valS1860);
      _M0L2giS1857 = _M0L4varsS726->$2;
      _M0L3valS1858 = _M0L1iS727->$0;
      #line 309 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1856
      = _M0MPC15array5Array2atGfE(_M0L2giS1857, _M0L3valS1858);
      _M0L6_2atmpS1854 = _M0L6_2atmpS1855 - _M0L6_2atmpS1856;
      _M0L6_2atmpS1853 = -_M0L6_2atmpS1854;
      #line 309 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS728, _M0L3valS1852, _M0L6_2atmpS1853);
      _M0L3valS1862 = _M0L1iS727->$0;
      _M0L6_2atmpS1861 = _M0L3valS1862 + 1;
      _M0L1iS727->$0 = _M0L6_2atmpS1861;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS727);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22current__synapse__step(
  struct _M0TP26RiantR8snn__mbt18CurrentSynapseVars* _M0L4varsS716,
  struct _M0TP26RiantR8snn__mbt14CurrentSynapse* _M0L5paramS718,
  struct _M0TPB5ArrayGfE* _M0L3gluS721,
  struct _M0TPB5ArrayGfE* _M0L4gabaS722,
  float _M0L2dtS723
) {
  int32_t _M0L1nS715;
  float _M0L6tau__eS1850;
  float _M0L11inv__tau__eS717;
  float _M0L6tau__iS1849;
  float _M0L11inv__tau__iS719;
  struct _M0TPB8MutLocalGiE* _M0L1iS720;
  #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  _M0L1nS715 = _M0L4varsS716->$0;
  _M0L6tau__eS1850 = _M0L5paramS718->$0;
  _M0L11inv__tau__eS717 = 0x1p+0f / _M0L6tau__eS1850;
  _M0L6tau__iS1849 = _M0L5paramS718->$1;
  _M0L11inv__tau__iS719 = 0x1p+0f / _M0L6tau__iS1849;
  _M0L1iS720
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS720)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS720->$0 = 0;
  while (1) {
    int32_t _M0L3valS1806 = _M0L1iS720->$0;
    if (_M0L3valS1806 < _M0L1nS715) {
      struct _M0TPB5ArrayGfE* _M0L2geS1807 = _M0L4varsS716->$1;
      int32_t _M0L3valS1808 = _M0L1iS720->$0;
      struct _M0TPB5ArrayGfE* _M0L2geS1813 = _M0L4varsS716->$1;
      int32_t _M0L3valS1814 = _M0L1iS720->$0;
      float _M0L6_2atmpS1810;
      int32_t _M0L3valS1812;
      float _M0L6_2atmpS1811;
      float _M0L6_2atmpS1809;
      struct _M0TPB5ArrayGfE* _M0L2giS1815;
      int32_t _M0L3valS1816;
      struct _M0TPB5ArrayGfE* _M0L2giS1821;
      int32_t _M0L3valS1822;
      float _M0L6_2atmpS1818;
      int32_t _M0L3valS1820;
      float _M0L6_2atmpS1819;
      float _M0L6_2atmpS1817;
      struct _M0TPB5ArrayGfE* _M0L2geS1823;
      int32_t _M0L3valS1824;
      struct _M0TPB5ArrayGfE* _M0L2geS1833;
      int32_t _M0L3valS1834;
      float _M0L6_2atmpS1826;
      struct _M0TPB5ArrayGfE* _M0L2geS1831;
      int32_t _M0L3valS1832;
      float _M0L6_2atmpS1830;
      float _M0L6_2atmpS1829;
      float _M0L6_2atmpS1828;
      float _M0L6_2atmpS1827;
      float _M0L6_2atmpS1825;
      struct _M0TPB5ArrayGfE* _M0L2giS1835;
      int32_t _M0L3valS1836;
      struct _M0TPB5ArrayGfE* _M0L2giS1845;
      int32_t _M0L3valS1846;
      float _M0L6_2atmpS1838;
      struct _M0TPB5ArrayGfE* _M0L2giS1843;
      int32_t _M0L3valS1844;
      float _M0L6_2atmpS1842;
      float _M0L6_2atmpS1841;
      float _M0L6_2atmpS1840;
      float _M0L6_2atmpS1839;
      float _M0L6_2atmpS1837;
      int32_t _M0L3valS1848;
      int32_t _M0L6_2atmpS1847;
      #line 272 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1810
      = _M0MPC15array5Array2atGfE(_M0L2geS1813, _M0L3valS1814);
      _M0L3valS1812 = _M0L1iS720->$0;
      #line 272 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1811
      = _M0MPC15array5Array2atGfE(_M0L3gluS721, _M0L3valS1812);
      _M0L6_2atmpS1809 = _M0L6_2atmpS1810 + _M0L6_2atmpS1811;
      #line 272 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1807, _M0L3valS1808, _M0L6_2atmpS1809);
      _M0L2giS1815 = _M0L4varsS716->$2;
      _M0L3valS1816 = _M0L1iS720->$0;
      _M0L2giS1821 = _M0L4varsS716->$2;
      _M0L3valS1822 = _M0L1iS720->$0;
      #line 273 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1818
      = _M0MPC15array5Array2atGfE(_M0L2giS1821, _M0L3valS1822);
      _M0L3valS1820 = _M0L1iS720->$0;
      #line 273 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1819
      = _M0MPC15array5Array2atGfE(_M0L4gabaS722, _M0L3valS1820);
      _M0L6_2atmpS1817 = _M0L6_2atmpS1818 + _M0L6_2atmpS1819;
      #line 273 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1815, _M0L3valS1816, _M0L6_2atmpS1817);
      _M0L2geS1823 = _M0L4varsS716->$1;
      _M0L3valS1824 = _M0L1iS720->$0;
      _M0L2geS1833 = _M0L4varsS716->$1;
      _M0L3valS1834 = _M0L1iS720->$0;
      #line 274 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1826
      = _M0MPC15array5Array2atGfE(_M0L2geS1833, _M0L3valS1834);
      _M0L2geS1831 = _M0L4varsS716->$1;
      _M0L3valS1832 = _M0L1iS720->$0;
      #line 274 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1830
      = _M0MPC15array5Array2atGfE(_M0L2geS1831, _M0L3valS1832);
      _M0L6_2atmpS1829 = -_M0L6_2atmpS1830;
      _M0L6_2atmpS1828 = _M0L2dtS723 * _M0L6_2atmpS1829;
      _M0L6_2atmpS1827 = _M0L6_2atmpS1828 * _M0L11inv__tau__eS717;
      _M0L6_2atmpS1825 = _M0L6_2atmpS1826 + _M0L6_2atmpS1827;
      #line 274 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1823, _M0L3valS1824, _M0L6_2atmpS1825);
      _M0L2giS1835 = _M0L4varsS716->$2;
      _M0L3valS1836 = _M0L1iS720->$0;
      _M0L2giS1845 = _M0L4varsS716->$2;
      _M0L3valS1846 = _M0L1iS720->$0;
      #line 275 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1838
      = _M0MPC15array5Array2atGfE(_M0L2giS1845, _M0L3valS1846);
      _M0L2giS1843 = _M0L4varsS716->$2;
      _M0L3valS1844 = _M0L1iS720->$0;
      #line 275 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1842
      = _M0MPC15array5Array2atGfE(_M0L2giS1843, _M0L3valS1844);
      _M0L6_2atmpS1841 = -_M0L6_2atmpS1842;
      _M0L6_2atmpS1840 = _M0L2dtS723 * _M0L6_2atmpS1841;
      _M0L6_2atmpS1839 = _M0L6_2atmpS1840 * _M0L11inv__tau__iS719;
      _M0L6_2atmpS1837 = _M0L6_2atmpS1838 + _M0L6_2atmpS1839;
      #line 275 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1835, _M0L3valS1836, _M0L6_2atmpS1837);
      _M0L3valS1848 = _M0L1iS720->$0;
      _M0L6_2atmpS1847 = _M0L3valS1848 + 1;
      _M0L1iS720->$0 = _M0L6_2atmpS1847;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS720);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt26double__exp__synapse__step(
  struct _M0TP26RiantR8snn__mbt20DoubleExpSynapseVars* _M0L4varsS704,
  struct _M0TP26RiantR8snn__mbt16DoubleExpSynapse* _M0L5paramS706,
  struct _M0TPB5ArrayGfE* _M0L3gluS712,
  struct _M0TPB5ArrayGfE* _M0L4gabaS713,
  float _M0L2dtS711
) {
  int32_t _M0L1nS703;
  float _M0L7tau__reS1805;
  float _M0L12inv__tau__reS705;
  float _M0L7tau__deS1804;
  float _M0L12inv__tau__deS707;
  float _M0L7tau__riS1803;
  float _M0L12inv__tau__riS708;
  float _M0L7tau__diS1802;
  float _M0L12inv__tau__diS709;
  struct _M0TPB8MutLocalGiE* _M0L1iS710;
  #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  _M0L1nS703 = _M0L4varsS704->$0;
  _M0L7tau__reS1805 = _M0L5paramS706->$0;
  _M0L12inv__tau__reS705 = 0x1p+0f / _M0L7tau__reS1805;
  _M0L7tau__deS1804 = _M0L5paramS706->$1;
  _M0L12inv__tau__deS707 = 0x1p+0f / _M0L7tau__deS1804;
  _M0L7tau__riS1803 = _M0L5paramS706->$2;
  _M0L12inv__tau__riS708 = 0x1p+0f / _M0L7tau__riS1803;
  _M0L7tau__diS1802 = _M0L5paramS706->$3;
  _M0L12inv__tau__diS709 = 0x1p+0f / _M0L7tau__diS1802;
  _M0L1iS710
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS710)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS710->$0 = 0;
  while (1) {
    int32_t _M0L3valS1741 = _M0L1iS710->$0;
    if (_M0L3valS1741 < _M0L1nS703) {
      struct _M0TPB5ArrayGfE* _M0L2heS1742 = _M0L4varsS704->$3;
      int32_t _M0L3valS1743 = _M0L1iS710->$0;
      struct _M0TPB5ArrayGfE* _M0L2heS1754 = _M0L4varsS704->$3;
      int32_t _M0L3valS1755 = _M0L1iS710->$0;
      float _M0L6_2atmpS1745;
      int32_t _M0L3valS1753;
      float _M0L6_2atmpS1749;
      struct _M0TPB5ArrayGfE* _M0L2heS1751;
      int32_t _M0L3valS1752;
      float _M0L6_2atmpS1750;
      float _M0L6_2atmpS1748;
      float _M0L6_2atmpS1747;
      float _M0L6_2atmpS1746;
      float _M0L6_2atmpS1744;
      struct _M0TPB5ArrayGfE* _M0L2hiS1756;
      int32_t _M0L3valS1757;
      struct _M0TPB5ArrayGfE* _M0L2hiS1768;
      int32_t _M0L3valS1769;
      float _M0L6_2atmpS1759;
      int32_t _M0L3valS1767;
      float _M0L6_2atmpS1763;
      struct _M0TPB5ArrayGfE* _M0L2hiS1765;
      int32_t _M0L3valS1766;
      float _M0L6_2atmpS1764;
      float _M0L6_2atmpS1762;
      float _M0L6_2atmpS1761;
      float _M0L6_2atmpS1760;
      float _M0L6_2atmpS1758;
      struct _M0TPB5ArrayGfE* _M0L2geS1770;
      int32_t _M0L3valS1771;
      struct _M0TPB5ArrayGfE* _M0L2geS1783;
      int32_t _M0L3valS1784;
      float _M0L6_2atmpS1773;
      struct _M0TPB5ArrayGfE* _M0L2heS1781;
      int32_t _M0L3valS1782;
      float _M0L6_2atmpS1777;
      struct _M0TPB5ArrayGfE* _M0L2geS1779;
      int32_t _M0L3valS1780;
      float _M0L6_2atmpS1778;
      float _M0L6_2atmpS1776;
      float _M0L6_2atmpS1775;
      float _M0L6_2atmpS1774;
      float _M0L6_2atmpS1772;
      struct _M0TPB5ArrayGfE* _M0L2giS1785;
      int32_t _M0L3valS1786;
      struct _M0TPB5ArrayGfE* _M0L2giS1798;
      int32_t _M0L3valS1799;
      float _M0L6_2atmpS1788;
      struct _M0TPB5ArrayGfE* _M0L2hiS1796;
      int32_t _M0L3valS1797;
      float _M0L6_2atmpS1792;
      struct _M0TPB5ArrayGfE* _M0L2giS1794;
      int32_t _M0L3valS1795;
      float _M0L6_2atmpS1793;
      float _M0L6_2atmpS1791;
      float _M0L6_2atmpS1790;
      float _M0L6_2atmpS1789;
      float _M0L6_2atmpS1787;
      int32_t _M0L3valS1801;
      int32_t _M0L6_2atmpS1800;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1745
      = _M0MPC15array5Array2atGfE(_M0L2heS1754, _M0L3valS1755);
      _M0L3valS1753 = _M0L1iS710->$0;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1749
      = _M0MPC15array5Array2atGfE(_M0L3gluS712, _M0L3valS1753);
      _M0L2heS1751 = _M0L4varsS704->$3;
      _M0L3valS1752 = _M0L1iS710->$0;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1750
      = _M0MPC15array5Array2atGfE(_M0L2heS1751, _M0L3valS1752);
      _M0L6_2atmpS1748 = _M0L6_2atmpS1749 - _M0L6_2atmpS1750;
      _M0L6_2atmpS1747 = _M0L2dtS711 * _M0L6_2atmpS1748;
      _M0L6_2atmpS1746 = _M0L6_2atmpS1747 * _M0L12inv__tau__reS705;
      _M0L6_2atmpS1744 = _M0L6_2atmpS1745 + _M0L6_2atmpS1746;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS1742, _M0L3valS1743, _M0L6_2atmpS1744);
      _M0L2hiS1756 = _M0L4varsS704->$4;
      _M0L3valS1757 = _M0L1iS710->$0;
      _M0L2hiS1768 = _M0L4varsS704->$4;
      _M0L3valS1769 = _M0L1iS710->$0;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1759
      = _M0MPC15array5Array2atGfE(_M0L2hiS1768, _M0L3valS1769);
      _M0L3valS1767 = _M0L1iS710->$0;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1763
      = _M0MPC15array5Array2atGfE(_M0L4gabaS713, _M0L3valS1767);
      _M0L2hiS1765 = _M0L4varsS704->$4;
      _M0L3valS1766 = _M0L1iS710->$0;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1764
      = _M0MPC15array5Array2atGfE(_M0L2hiS1765, _M0L3valS1766);
      _M0L6_2atmpS1762 = _M0L6_2atmpS1763 - _M0L6_2atmpS1764;
      _M0L6_2atmpS1761 = _M0L2dtS711 * _M0L6_2atmpS1762;
      _M0L6_2atmpS1760 = _M0L6_2atmpS1761 * _M0L12inv__tau__riS708;
      _M0L6_2atmpS1758 = _M0L6_2atmpS1759 + _M0L6_2atmpS1760;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS1756, _M0L3valS1757, _M0L6_2atmpS1758);
      _M0L2geS1770 = _M0L4varsS704->$1;
      _M0L3valS1771 = _M0L1iS710->$0;
      _M0L2geS1783 = _M0L4varsS704->$1;
      _M0L3valS1784 = _M0L1iS710->$0;
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1773
      = _M0MPC15array5Array2atGfE(_M0L2geS1783, _M0L3valS1784);
      _M0L2heS1781 = _M0L4varsS704->$3;
      _M0L3valS1782 = _M0L1iS710->$0;
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1777
      = _M0MPC15array5Array2atGfE(_M0L2heS1781, _M0L3valS1782);
      _M0L2geS1779 = _M0L4varsS704->$1;
      _M0L3valS1780 = _M0L1iS710->$0;
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1778
      = _M0MPC15array5Array2atGfE(_M0L2geS1779, _M0L3valS1780);
      _M0L6_2atmpS1776 = _M0L6_2atmpS1777 - _M0L6_2atmpS1778;
      _M0L6_2atmpS1775 = _M0L2dtS711 * _M0L6_2atmpS1776;
      _M0L6_2atmpS1774 = _M0L6_2atmpS1775 * _M0L12inv__tau__deS707;
      _M0L6_2atmpS1772 = _M0L6_2atmpS1773 + _M0L6_2atmpS1774;
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1770, _M0L3valS1771, _M0L6_2atmpS1772);
      _M0L2giS1785 = _M0L4varsS704->$2;
      _M0L3valS1786 = _M0L1iS710->$0;
      _M0L2giS1798 = _M0L4varsS704->$2;
      _M0L3valS1799 = _M0L1iS710->$0;
      #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1788
      = _M0MPC15array5Array2atGfE(_M0L2giS1798, _M0L3valS1799);
      _M0L2hiS1796 = _M0L4varsS704->$4;
      _M0L3valS1797 = _M0L1iS710->$0;
      #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1792
      = _M0MPC15array5Array2atGfE(_M0L2hiS1796, _M0L3valS1797);
      _M0L2giS1794 = _M0L4varsS704->$2;
      _M0L3valS1795 = _M0L1iS710->$0;
      #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1793
      = _M0MPC15array5Array2atGfE(_M0L2giS1794, _M0L3valS1795);
      _M0L6_2atmpS1791 = _M0L6_2atmpS1792 - _M0L6_2atmpS1793;
      _M0L6_2atmpS1790 = _M0L2dtS711 * _M0L6_2atmpS1791;
      _M0L6_2atmpS1789 = _M0L6_2atmpS1790 * _M0L12inv__tau__diS709;
      _M0L6_2atmpS1787 = _M0L6_2atmpS1788 + _M0L6_2atmpS1789;
      #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1785, _M0L3valS1786, _M0L6_2atmpS1787);
      _M0L3valS1801 = _M0L1iS710->$0;
      _M0L6_2atmpS1800 = _M0L3valS1801 + 1;
      _M0L1iS710->$0 = _M0L6_2atmpS1800;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS710);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt26single__exp__synapse__step(
  struct _M0TP26RiantR8snn__mbt20SingleExpSynapseVars* _M0L4varsS694,
  struct _M0TP26RiantR8snn__mbt16SingleExpSynapse* _M0L5paramS696,
  struct _M0TPB5ArrayGfE* _M0L3gluS699,
  struct _M0TPB5ArrayGfE* _M0L4gabaS700,
  float _M0L2dtS701
) {
  int32_t _M0L1nS693;
  float _M0L6tau__eS1740;
  float _M0L11inv__tau__eS695;
  float _M0L6tau__iS1739;
  float _M0L11inv__tau__iS697;
  struct _M0TPB8MutLocalGiE* _M0L1iS698;
  #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  _M0L1nS693 = _M0L4varsS694->$0;
  _M0L6tau__eS1740 = _M0L5paramS696->$0;
  _M0L11inv__tau__eS695 = 0x1p+0f / _M0L6tau__eS1740;
  _M0L6tau__iS1739 = _M0L5paramS696->$1;
  _M0L11inv__tau__iS697 = 0x1p+0f / _M0L6tau__iS1739;
  _M0L1iS698
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS698)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS698->$0 = 0;
  while (1) {
    int32_t _M0L3valS1696 = _M0L1iS698->$0;
    if (_M0L3valS1696 < _M0L1nS693) {
      struct _M0TPB5ArrayGfE* _M0L2geS1697 = _M0L4varsS694->$1;
      int32_t _M0L3valS1698 = _M0L1iS698->$0;
      struct _M0TPB5ArrayGfE* _M0L2geS1703 = _M0L4varsS694->$1;
      int32_t _M0L3valS1704 = _M0L1iS698->$0;
      float _M0L6_2atmpS1700;
      int32_t _M0L3valS1702;
      float _M0L6_2atmpS1701;
      float _M0L6_2atmpS1699;
      struct _M0TPB5ArrayGfE* _M0L2giS1705;
      int32_t _M0L3valS1706;
      struct _M0TPB5ArrayGfE* _M0L2giS1711;
      int32_t _M0L3valS1712;
      float _M0L6_2atmpS1708;
      int32_t _M0L3valS1710;
      float _M0L6_2atmpS1709;
      float _M0L6_2atmpS1707;
      struct _M0TPB5ArrayGfE* _M0L2geS1713;
      int32_t _M0L3valS1714;
      struct _M0TPB5ArrayGfE* _M0L2geS1723;
      int32_t _M0L3valS1724;
      float _M0L6_2atmpS1716;
      struct _M0TPB5ArrayGfE* _M0L2geS1721;
      int32_t _M0L3valS1722;
      float _M0L6_2atmpS1720;
      float _M0L6_2atmpS1719;
      float _M0L6_2atmpS1718;
      float _M0L6_2atmpS1717;
      float _M0L6_2atmpS1715;
      struct _M0TPB5ArrayGfE* _M0L2giS1725;
      int32_t _M0L3valS1726;
      struct _M0TPB5ArrayGfE* _M0L2giS1735;
      int32_t _M0L3valS1736;
      float _M0L6_2atmpS1728;
      struct _M0TPB5ArrayGfE* _M0L2giS1733;
      int32_t _M0L3valS1734;
      float _M0L6_2atmpS1732;
      float _M0L6_2atmpS1731;
      float _M0L6_2atmpS1730;
      float _M0L6_2atmpS1729;
      float _M0L6_2atmpS1727;
      int32_t _M0L3valS1738;
      int32_t _M0L6_2atmpS1737;
      #line 218 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1700
      = _M0MPC15array5Array2atGfE(_M0L2geS1703, _M0L3valS1704);
      _M0L3valS1702 = _M0L1iS698->$0;
      #line 218 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1701
      = _M0MPC15array5Array2atGfE(_M0L3gluS699, _M0L3valS1702);
      _M0L6_2atmpS1699 = _M0L6_2atmpS1700 + _M0L6_2atmpS1701;
      #line 218 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1697, _M0L3valS1698, _M0L6_2atmpS1699);
      _M0L2giS1705 = _M0L4varsS694->$2;
      _M0L3valS1706 = _M0L1iS698->$0;
      _M0L2giS1711 = _M0L4varsS694->$2;
      _M0L3valS1712 = _M0L1iS698->$0;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1708
      = _M0MPC15array5Array2atGfE(_M0L2giS1711, _M0L3valS1712);
      _M0L3valS1710 = _M0L1iS698->$0;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1709
      = _M0MPC15array5Array2atGfE(_M0L4gabaS700, _M0L3valS1710);
      _M0L6_2atmpS1707 = _M0L6_2atmpS1708 + _M0L6_2atmpS1709;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1705, _M0L3valS1706, _M0L6_2atmpS1707);
      _M0L2geS1713 = _M0L4varsS694->$1;
      _M0L3valS1714 = _M0L1iS698->$0;
      _M0L2geS1723 = _M0L4varsS694->$1;
      _M0L3valS1724 = _M0L1iS698->$0;
      #line 220 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1716
      = _M0MPC15array5Array2atGfE(_M0L2geS1723, _M0L3valS1724);
      _M0L2geS1721 = _M0L4varsS694->$1;
      _M0L3valS1722 = _M0L1iS698->$0;
      #line 220 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1720
      = _M0MPC15array5Array2atGfE(_M0L2geS1721, _M0L3valS1722);
      _M0L6_2atmpS1719 = -_M0L6_2atmpS1720;
      _M0L6_2atmpS1718 = _M0L2dtS701 * _M0L6_2atmpS1719;
      _M0L6_2atmpS1717 = _M0L6_2atmpS1718 * _M0L11inv__tau__eS695;
      _M0L6_2atmpS1715 = _M0L6_2atmpS1716 + _M0L6_2atmpS1717;
      #line 220 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1713, _M0L3valS1714, _M0L6_2atmpS1715);
      _M0L2giS1725 = _M0L4varsS694->$2;
      _M0L3valS1726 = _M0L1iS698->$0;
      _M0L2giS1735 = _M0L4varsS694->$2;
      _M0L3valS1736 = _M0L1iS698->$0;
      #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1728
      = _M0MPC15array5Array2atGfE(_M0L2giS1735, _M0L3valS1736);
      _M0L2giS1733 = _M0L4varsS694->$2;
      _M0L3valS1734 = _M0L1iS698->$0;
      #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1732
      = _M0MPC15array5Array2atGfE(_M0L2giS1733, _M0L3valS1734);
      _M0L6_2atmpS1731 = -_M0L6_2atmpS1732;
      _M0L6_2atmpS1730 = _M0L2dtS701 * _M0L6_2atmpS1731;
      _M0L6_2atmpS1729 = _M0L6_2atmpS1730 * _M0L11inv__tau__iS697;
      _M0L6_2atmpS1727 = _M0L6_2atmpS1728 + _M0L6_2atmpS1729;
      #line 221 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1725, _M0L3valS1726, _M0L6_2atmpS1727);
      _M0L3valS1738 = _M0L1iS698->$0;
      _M0L6_2atmpS1737 = _M0L3valS1738 + 1;
      _M0L1iS698->$0 = _M0L6_2atmpS1737;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS698);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20delta__synapse__step(
  struct _M0TP26RiantR8snn__mbt16DeltaSynapseVars* _M0L4varsS688,
  struct _M0TPB5ArrayGfE* _M0L3gluS690,
  struct _M0TPB5ArrayGfE* _M0L4gabaS691
) {
  int32_t _M0L1nS687;
  struct _M0TPB8MutLocalGiE* _M0L1iS689;
  #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  _M0L1nS687 = _M0L4varsS688->$0;
  _M0L1iS689
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS689)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS689->$0 = 0;
  while (1) {
    int32_t _M0L3valS1677 = _M0L1iS689->$0;
    if (_M0L3valS1677 < _M0L1nS687) {
      struct _M0TPB5ArrayGfE* _M0L2geS1678 = _M0L4varsS688->$1;
      int32_t _M0L3valS1679 = _M0L1iS689->$0;
      struct _M0TPB5ArrayGfE* _M0L2geS1684 = _M0L4varsS688->$1;
      int32_t _M0L3valS1685 = _M0L1iS689->$0;
      float _M0L6_2atmpS1681;
      int32_t _M0L3valS1683;
      float _M0L6_2atmpS1682;
      float _M0L6_2atmpS1680;
      struct _M0TPB5ArrayGfE* _M0L2giS1686;
      int32_t _M0L3valS1687;
      struct _M0TPB5ArrayGfE* _M0L2giS1692;
      int32_t _M0L3valS1693;
      float _M0L6_2atmpS1689;
      int32_t _M0L3valS1691;
      float _M0L6_2atmpS1690;
      float _M0L6_2atmpS1688;
      int32_t _M0L3valS1695;
      int32_t _M0L6_2atmpS1694;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1681
      = _M0MPC15array5Array2atGfE(_M0L2geS1684, _M0L3valS1685);
      _M0L3valS1683 = _M0L1iS689->$0;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1682
      = _M0MPC15array5Array2atGfE(_M0L3gluS690, _M0L3valS1683);
      _M0L6_2atmpS1680 = _M0L6_2atmpS1681 + _M0L6_2atmpS1682;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS1678, _M0L3valS1679, _M0L6_2atmpS1680);
      _M0L2giS1686 = _M0L4varsS688->$2;
      _M0L3valS1687 = _M0L1iS689->$0;
      _M0L2giS1692 = _M0L4varsS688->$2;
      _M0L3valS1693 = _M0L1iS689->$0;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1689
      = _M0MPC15array5Array2atGfE(_M0L2giS1692, _M0L3valS1693);
      _M0L3valS1691 = _M0L1iS689->$0;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0L6_2atmpS1690
      = _M0MPC15array5Array2atGfE(_M0L4gabaS691, _M0L3valS1691);
      _M0L6_2atmpS1688 = _M0L6_2atmpS1689 + _M0L6_2atmpS1690;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS1686, _M0L3valS1687, _M0L6_2atmpS1688);
      _M0L3valS1695 = _M0L1iS689->$0;
      _M0L6_2atmpS1694 = _M0L3valS1695 + 1;
      _M0L1iS689->$0 = _M0L6_2atmpS1694;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS689);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt18CurrentSynapseVars* _M0MP26RiantR8snn__mbt18CurrentSynapseVars3new(
  int32_t _M0L1nS686
) {
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1675;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1676;
  struct _M0TP26RiantR8snn__mbt18CurrentSynapseVars* _block_2020;
  #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  _M0L6_2atmpS1675 = _M0MPC15array5Array4makeGfE(_M0L1nS686, 0x0p+0f);
  #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  _M0L6_2atmpS1676 = _M0MPC15array5Array4makeGfE(_M0L1nS686, 0x0p+0f);
  _block_2020
  = (struct _M0TP26RiantR8snn__mbt18CurrentSynapseVars*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt18CurrentSynapseVars));
  Moonbit_object_header(_block_2020)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
  _block_2020->$0 = _M0L1nS686;
  _block_2020->$1 = _M0L6_2atmpS1675;
  _block_2020->$2 = _M0L6_2atmpS1676;
  return _block_2020;
}

struct _M0TP26RiantR8snn__mbt14CurrentSynapse* _M0MP26RiantR8snn__mbt14CurrentSynapse3new(
  
) {
  struct _M0TP26RiantR8snn__mbt14CurrentSynapse* _block_2021;
  #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  _block_2021
  = (struct _M0TP26RiantR8snn__mbt14CurrentSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt14CurrentSynapse));
  Moonbit_object_header(_block_2021)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2021->$0 = 0x1.8p+2f;
  _block_2021->$1 = 0x1p+1f;
  return _block_2021;
}

struct _M0TP26RiantR8snn__mbt20DoubleExpSynapseVars* _M0MP26RiantR8snn__mbt20DoubleExpSynapseVars3new(
  int32_t _M0L1nS685
) {
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1671;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1672;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1673;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1674;
  struct _M0TP26RiantR8snn__mbt20DoubleExpSynapseVars* _block_2022;
  #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  _M0L6_2atmpS1671 = _M0MPC15array5Array4makeGfE(_M0L1nS685, 0x0p+0f);
  #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  _M0L6_2atmpS1672 = _M0MPC15array5Array4makeGfE(_M0L1nS685, 0x0p+0f);
  #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  _M0L6_2atmpS1673 = _M0MPC15array5Array4makeGfE(_M0L1nS685, 0x0p+0f);
  #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  _M0L6_2atmpS1674 = _M0MPC15array5Array4makeGfE(_M0L1nS685, 0x0p+0f);
  _block_2022
  = (struct _M0TP26RiantR8snn__mbt20DoubleExpSynapseVars*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt20DoubleExpSynapseVars));
  Moonbit_object_header(_block_2022)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 22, 0);
  _block_2022->$0 = _M0L1nS685;
  _block_2022->$1 = _M0L6_2atmpS1671;
  _block_2022->$2 = _M0L6_2atmpS1672;
  _block_2022->$3 = _M0L6_2atmpS1673;
  _block_2022->$4 = _M0L6_2atmpS1674;
  return _block_2022;
}

struct _M0TP26RiantR8snn__mbt16DoubleExpSynapse* _M0MP26RiantR8snn__mbt16DoubleExpSynapse3new(
  
) {
  struct _M0TP26RiantR8snn__mbt16DoubleExpSynapse* _block_2023;
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  _block_2023
  = (struct _M0TP26RiantR8snn__mbt16DoubleExpSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt16DoubleExpSynapse));
  Moonbit_object_header(_block_2023)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2023->$0 = 0x1p+0f;
  _block_2023->$1 = 0x1.8p+2f;
  _block_2023->$2 = 0x1p-1f;
  _block_2023->$3 = 0x1p+1f;
  _block_2023->$4 = -0x1.2cp+6f;
  _block_2023->$5 = 0x0p+0f;
  _block_2023->$6 = 0x1p+0f;
  _block_2023->$7 = 0x1p+0f;
  return _block_2023;
}

struct _M0TP26RiantR8snn__mbt20SingleExpSynapseVars* _M0MP26RiantR8snn__mbt20SingleExpSynapseVars3new(
  int32_t _M0L1nS684
) {
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1669;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1670;
  struct _M0TP26RiantR8snn__mbt20SingleExpSynapseVars* _block_2024;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  _M0L6_2atmpS1669 = _M0MPC15array5Array4makeGfE(_M0L1nS684, 0x0p+0f);
  #line 97 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  _M0L6_2atmpS1670 = _M0MPC15array5Array4makeGfE(_M0L1nS684, 0x0p+0f);
  _block_2024
  = (struct _M0TP26RiantR8snn__mbt20SingleExpSynapseVars*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt20SingleExpSynapseVars));
  Moonbit_object_header(_block_2024)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 28, 0);
  _block_2024->$0 = _M0L1nS684;
  _block_2024->$1 = _M0L6_2atmpS1669;
  _block_2024->$2 = _M0L6_2atmpS1670;
  return _block_2024;
}

struct _M0TP26RiantR8snn__mbt16SingleExpSynapse* _M0MP26RiantR8snn__mbt16SingleExpSynapse3new(
  
) {
  struct _M0TP26RiantR8snn__mbt16SingleExpSynapse* _block_2025;
  #line 76 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  _block_2025
  = (struct _M0TP26RiantR8snn__mbt16SingleExpSynapse*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt16SingleExpSynapse));
  Moonbit_object_header(_block_2025)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2025->$0 = 0x1.8p+2f;
  _block_2025->$1 = 0x1p-1f;
  _block_2025->$2 = -0x1.2cp+6f;
  _block_2025->$3 = 0x0p+0f;
  _block_2025->$4 = 0x1p+0f;
  _block_2025->$5 = 0x1p+0f;
  return _block_2025;
}

struct _M0TP26RiantR8snn__mbt16DeltaSynapseVars* _M0MP26RiantR8snn__mbt16DeltaSynapseVars3new(
  int32_t _M0L1nS683
) {
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1667;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS1668;
  struct _M0TP26RiantR8snn__mbt16DeltaSynapseVars* _block_2026;
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  _M0L6_2atmpS1667 = _M0MPC15array5Array4makeGfE(_M0L1nS683, 0x0p+0f);
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  _M0L6_2atmpS1668 = _M0MPC15array5Array4makeGfE(_M0L1nS683, 0x0p+0f);
  _block_2026
  = (struct _M0TP26RiantR8snn__mbt16DeltaSynapseVars*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt16DeltaSynapseVars));
  Moonbit_object_header(_block_2026)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 32, 0);
  _block_2026->$0 = _M0L1nS683;
  _block_2026->$1 = _M0L6_2atmpS1667;
  _block_2026->$2 = _M0L6_2atmpS1668;
  return _block_2026;
}

int32_t _M0MP26RiantR8snn__mbt12DeltaSynapse3new() {
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\synapse_params.mbt"
  return 0;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS682) {
  double _M0L6_2atmpS1666;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS1666 = (double)_M0L4selfS682;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS1666);
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS678,
  float _M0L4elemS680
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS677;
  int32_t _M0L1iS679;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS677 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS678);
  _M0L1iS679 = 0;
  while (1) {
    if (_M0L1iS679 < _M0L3lenS678) {
      float* _M0L3bufS1664 = _M0L3arrS677->$0;
      int32_t _M0L6_2atmpS1665;
      _M0L3bufS1664[_M0L1iS679] = _M0L4elemS680;
      _M0L6_2atmpS1665 = _M0L1iS679 + 1;
      _M0L1iS679 = _M0L6_2atmpS1665;
      continue;
    }
    break;
  }
  return _M0L3arrS677;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS674,
  int32_t _M0L5indexS675,
  float _M0L5valueS676
) {
  int32_t _M0L3lenS673;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS673 = _M0L4selfS674->$1;
  if (_M0L5indexS675 >= 0 && _M0L5indexS675 < _M0L3lenS673) {
    float* _M0L6_2atmpS1663;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1663 = _M0MPC15array5Array6bufferGfE(_M0L4selfS674);
    _M0L6_2atmpS1663[_M0L5indexS675] = _M0L5valueS676;
    moonbit_decref_cycle_free(_M0L6_2atmpS1663);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS668,
  int32_t _M0L5indexS669
) {
  int32_t _M0L3lenS667;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS667 = _M0L4selfS668->$1;
  if (_M0L5indexS669 >= 0 && _M0L5indexS669 < _M0L3lenS667) {
    float* _M0L6_2atmpS1661;
    float _result_2028;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1661 = _M0MPC15array5Array6bufferGfE(_M0L4selfS668);
    _result_2028 = (float)_M0L6_2atmpS1661[_M0L5indexS669];
    moonbit_decref_cycle_free(_M0L6_2atmpS1661);
    return _result_2028;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS671,
  int32_t _M0L5indexS672
) {
  int32_t _M0L3lenS670;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS670 = _M0L4selfS671->$1;
  if (_M0L5indexS672 >= 0 && _M0L5indexS672 < _M0L3lenS670) {
    moonbit_string_t* _M0L6_2atmpS1662;
    moonbit_string_t _M0L6_2atmpS1967;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS1662 = _M0MPC15array5Array6bufferGsE(_M0L4selfS671);
    _M0L6_2atmpS1967 = (moonbit_string_t)_M0L6_2atmpS1662[_M0L5indexS672];
    moonbit_incref_cycle_free(_M0L6_2atmpS1967);
    moonbit_decref_cycle_free(_M0L6_2atmpS1662);
    return _M0L6_2atmpS1967;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS666) {
  moonbit_string_t _M0L6_2atmpS1660;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS1660 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS666);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS1660);
  moonbit_decref_cycle_free(_M0L6_2atmpS1660);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS665) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS665);
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS650) {
  uint64_t _M0L4bitsS653;
  uint64_t _M0L6_2atmpS1659;
  uint64_t _M0L6_2atmpS1658;
  int32_t _M0L8ieeeSignS654;
  uint64_t _M0L12ieeeMantissaS655;
  uint64_t _M0L6_2atmpS1657;
  uint64_t _M0L6_2atmpS1656;
  int32_t _M0L12ieeeExponentS656;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS657;
  struct _M0TPB17FloatingDecimal64* _M0L1vS658;
  moonbit_string_t _result_2030;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS650 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_9.data;
  }
  if (_M0L3valS650 >= -0x1p+53 && _M0L3valS650 <= 0x1p+53) {
    if (_M0L3valS650 >= -0x1p+31 && _M0L3valS650 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS651;
      double _M0L6_2atmpS1645;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS651 = _M0MPC16double6Double7to__int(_M0L3valS650);
      _M0L6_2atmpS1645 = (double)_M0L1iS651;
      if (_M0L6_2atmpS1645 == _M0L3valS650) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS651, 10);
      }
    } else {
      int64_t _M0L1iS652;
      double _M0L6_2atmpS1646;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS652 = _M0MPC16double6Double9to__int64(_M0L3valS650);
      _M0L6_2atmpS1646 = (double)_M0L1iS652;
      if (_M0L6_2atmpS1646 == _M0L3valS650) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS652, 10);
      }
    }
  }
  _M0L4bitsS653 = *(int64_t*)&_M0L3valS650;
  _M0L6_2atmpS1659 = _M0L4bitsS653 >> 63;
  _M0L6_2atmpS1658 = _M0L6_2atmpS1659 & 1ull;
  _M0L8ieeeSignS654 = _M0L6_2atmpS1658 != 0ull;
  _M0L12ieeeMantissaS655 = _M0L4bitsS653 & 4503599627370495ull;
  _M0L6_2atmpS1657 = _M0L4bitsS653 >> 52;
  _M0L6_2atmpS1656 = _M0L6_2atmpS1657 & 2047ull;
  _M0L12ieeeExponentS656 = (int32_t)_M0L6_2atmpS1656;
  if (
    _M0L12ieeeExponentS656 == 2047
    || _M0L12ieeeExponentS656 == 0 && _M0L12ieeeMantissaS655 == 0ull
  ) {
    int32_t _M0L6_2atmpS1647 = _M0L12ieeeExponentS656 != 0;
    int32_t _M0L6_2atmpS1648 = _M0L12ieeeMantissaS655 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS654, _M0L6_2atmpS1647, _M0L6_2atmpS1648);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS657
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS655, _M0L12ieeeExponentS656);
  if (_M0L7_2abindS657 == 0) {
    uint32_t _M0L6_2atmpS1649;
    if (_M0L7_2abindS657) {
      moonbit_decref_cycle_free(_M0L7_2abindS657);
    }
    _M0L6_2atmpS1649 = *(uint32_t*)&_M0L12ieeeExponentS656;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS658 = _M0FPB3d2d(_M0L12ieeeMantissaS655, _M0L6_2atmpS1649);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS659 = _M0L7_2abindS657;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS660 = _M0L7_2aSomeS659;
    struct _M0TPB17FloatingDecimal64* _M0L1xS661 = _M0L4_2afS660;
    while (1) {
      uint64_t _M0L8mantissaS1655 = _M0L1xS661->$0;
      uint64_t _M0L1qS662 = _M0L8mantissaS1655 / 10ull;
      uint64_t _M0L8mantissaS1653 = _M0L1xS661->$0;
      uint64_t _M0L6_2atmpS1654 = 10ull * _M0L1qS662;
      uint64_t _M0L1rS663 = _M0L8mantissaS1653 - _M0L6_2atmpS1654;
      int32_t _M0L8exponentS1652;
      int32_t _M0L6_2atmpS1651;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1650;
      if (_M0L1rS663 != 0ull) {
        _M0L1vS658 = _M0L1xS661;
        break;
      }
      _M0L8exponentS1652 = _M0L1xS661->$1;
      moonbit_decref_cycle_free(_M0L1xS661);
      _M0L6_2atmpS1651 = _M0L8exponentS1652 + 1;
      _M0L6_2atmpS1650
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS1650)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS1650->$0 = _M0L1qS662;
      _M0L6_2atmpS1650->$1 = _M0L6_2atmpS1651;
      _M0L1xS661 = _M0L6_2atmpS1650;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2030 = _M0FPB9to__chars(_M0L1vS658, _M0L8ieeeSignS654);
  moonbit_decref_cycle_free(_M0L1vS658);
  return _result_2030;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS645,
  int32_t _M0L12ieeeExponentS647
) {
  uint64_t _M0L2m2S644;
  int32_t _M0L6_2atmpS1644;
  int32_t _M0L2e2S646;
  int32_t _M0L6_2atmpS1643;
  uint64_t _M0L6_2atmpS1642;
  uint64_t _M0L4maskS648;
  uint64_t _M0L8fractionS649;
  int32_t _M0L6_2atmpS1641;
  uint64_t _M0L6_2atmpS1640;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS1639;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S644 = 4503599627370496ull | _M0L12ieeeMantissaS645;
  _M0L6_2atmpS1644 = _M0L12ieeeExponentS647 - 1023;
  _M0L2e2S646 = _M0L6_2atmpS1644 - 52;
  if (_M0L2e2S646 > 0) {
    return 0;
  }
  if (_M0L2e2S646 < -52) {
    return 0;
  }
  _M0L6_2atmpS1643 = -_M0L2e2S646;
  _M0L6_2atmpS1642 = 1ull << (_M0L6_2atmpS1643 & 63);
  _M0L4maskS648 = _M0L6_2atmpS1642 - 1ull;
  _M0L8fractionS649 = _M0L2m2S644 & _M0L4maskS648;
  if (_M0L8fractionS649 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1641 = -_M0L2e2S646;
  _M0L6_2atmpS1640 = _M0L2m2S644 >> (_M0L6_2atmpS1641 & 63);
  _M0L6_2atmpS1639
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS1639)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS1639->$0 = _M0L6_2atmpS1640;
  _M0L6_2atmpS1639->$1 = 0;
  return _M0L6_2atmpS1639;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS612,
  int32_t _M0L4signS610
) {
  moonbit_bytes_t _M0L6resultS608;
  int32_t _M0Lm5indexS609;
  uint64_t _M0L6outputS611;
  int32_t _M0L7olengthS613;
  int32_t _M0L8exponentS1638;
  int32_t _M0L6_2atmpS1637;
  int32_t _M0Lm3expS614;
  int32_t _M0L6_2atmpS1636;
  int32_t _M0L6_2atmpS1634;
  int32_t _M0L18scientificNotationS615;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS608 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS609 = 0;
  if (_M0L4signS610) {
    int32_t _M0L6_2atmpS1508 = _M0Lm5indexS609;
    int32_t _M0L6_2atmpS1509;
    if (
      _M0L6_2atmpS1508 < 0
      || _M0L6_2atmpS1508 >= Moonbit_array_length(_M0L6resultS608)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS608[_M0L6_2atmpS1508] = 45;
    _M0L6_2atmpS1509 = _M0Lm5indexS609;
    _M0Lm5indexS609 = _M0L6_2atmpS1509 + 1;
  }
  _M0L6outputS611 = _M0L1vS612->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS613 = _M0FPB17decimal__length17(_M0L6outputS611);
  _M0L8exponentS1638 = _M0L1vS612->$1;
  _M0L6_2atmpS1637 = _M0L8exponentS1638 + _M0L7olengthS613;
  _M0Lm3expS614 = _M0L6_2atmpS1637 - 1;
  _M0L6_2atmpS1636 = _M0Lm3expS614;
  if (_M0L6_2atmpS1636 >= -6) {
    int32_t _M0L6_2atmpS1635 = _M0Lm3expS614;
    _M0L6_2atmpS1634 = _M0L6_2atmpS1635 < 21;
  } else {
    _M0L6_2atmpS1634 = 0;
  }
  _M0L18scientificNotationS615 = !_M0L6_2atmpS1634;
  if (_M0L18scientificNotationS615) {
    int32_t _M0L7_2abindS616 = _M0L7olengthS613 - 1;
    uint64_t _M0L6outputS617;
    int32_t _M0L1iS618 = 0;
    uint64_t _M0L6outputS619 = _M0L6outputS611;
    int32_t _M0L6_2atmpS1510;
    int32_t _M0L6_2atmpS1514;
    int32_t _M0L6_2atmpS1513;
    int32_t _M0L6_2atmpS1512;
    int32_t _M0L6_2atmpS1511;
    int32_t _M0L6_2atmpS1518;
    int32_t _M0L6_2atmpS1519;
    int32_t _M0L6_2atmpS1520;
    int32_t _M0L6_2atmpS1521;
    int32_t _M0L6_2atmpS1522;
    int32_t _M0L6_2atmpS1528;
    int32_t _M0L6_2atmpS1561;
    moonbit_string_t _result_2032;
    while (1) {
      if (_M0L1iS618 < _M0L7_2abindS616) {
        uint64_t _M0L1cS620 = _M0L6outputS619 % 10ull;
        int32_t _M0L6_2atmpS1567 = _M0Lm5indexS609;
        int32_t _M0L6_2atmpS1566 = _M0L6_2atmpS1567 + _M0L7olengthS613;
        int32_t _M0L6_2atmpS1562 = _M0L6_2atmpS1566 - _M0L1iS618;
        int32_t _M0L6_2atmpS1565 = (int32_t)_M0L1cS620;
        int32_t _M0L6_2atmpS1564 = 48 + _M0L6_2atmpS1565;
        int32_t _M0L6_2atmpS1563 = _M0L6_2atmpS1564 & 0xff;
        int32_t _M0L6_2atmpS1568;
        uint64_t _M0L6_2atmpS1569;
        if (
          _M0L6_2atmpS1562 < 0
          || _M0L6_2atmpS1562 >= Moonbit_array_length(_M0L6resultS608)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS608[_M0L6_2atmpS1562] = _M0L6_2atmpS1563;
        _M0L6_2atmpS1568 = _M0L1iS618 + 1;
        _M0L6_2atmpS1569 = _M0L6outputS619 / 10ull;
        _M0L1iS618 = _M0L6_2atmpS1568;
        _M0L6outputS619 = _M0L6_2atmpS1569;
        continue;
      } else {
        _M0L6outputS617 = _M0L6outputS619;
      }
      break;
    }
    _M0L6_2atmpS1510 = _M0Lm5indexS609;
    _M0L6_2atmpS1514 = (int32_t)_M0L6outputS617;
    _M0L6_2atmpS1513 = _M0L6_2atmpS1514 % 10;
    _M0L6_2atmpS1512 = 48 + _M0L6_2atmpS1513;
    _M0L6_2atmpS1511 = _M0L6_2atmpS1512 & 0xff;
    if (
      _M0L6_2atmpS1510 < 0
      || _M0L6_2atmpS1510 >= Moonbit_array_length(_M0L6resultS608)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS608[_M0L6_2atmpS1510] = _M0L6_2atmpS1511;
    if (_M0L7olengthS613 > 1) {
      int32_t _M0L6_2atmpS1516 = _M0Lm5indexS609;
      int32_t _M0L6_2atmpS1515 = _M0L6_2atmpS1516 + 1;
      if (
        _M0L6_2atmpS1515 < 0
        || _M0L6_2atmpS1515 >= Moonbit_array_length(_M0L6resultS608)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS608[_M0L6_2atmpS1515] = 46;
    } else {
      int32_t _M0L6_2atmpS1517 = _M0Lm5indexS609;
      _M0Lm5indexS609 = _M0L6_2atmpS1517 - 1;
    }
    _M0L6_2atmpS1518 = _M0Lm5indexS609;
    _M0L6_2atmpS1519 = _M0L7olengthS613 + 1;
    _M0Lm5indexS609 = _M0L6_2atmpS1518 + _M0L6_2atmpS1519;
    _M0L6_2atmpS1520 = _M0Lm5indexS609;
    if (
      _M0L6_2atmpS1520 < 0
      || _M0L6_2atmpS1520 >= Moonbit_array_length(_M0L6resultS608)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS608[_M0L6_2atmpS1520] = 101;
    _M0L6_2atmpS1521 = _M0Lm5indexS609;
    _M0Lm5indexS609 = _M0L6_2atmpS1521 + 1;
    _M0L6_2atmpS1522 = _M0Lm3expS614;
    if (_M0L6_2atmpS1522 < 0) {
      int32_t _M0L6_2atmpS1523 = _M0Lm5indexS609;
      int32_t _M0L6_2atmpS1524;
      int32_t _M0L6_2atmpS1525;
      if (
        _M0L6_2atmpS1523 < 0
        || _M0L6_2atmpS1523 >= Moonbit_array_length(_M0L6resultS608)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS608[_M0L6_2atmpS1523] = 45;
      _M0L6_2atmpS1524 = _M0Lm5indexS609;
      _M0Lm5indexS609 = _M0L6_2atmpS1524 + 1;
      _M0L6_2atmpS1525 = _M0Lm3expS614;
      _M0Lm3expS614 = -_M0L6_2atmpS1525;
    } else {
      int32_t _M0L6_2atmpS1526 = _M0Lm5indexS609;
      int32_t _M0L6_2atmpS1527;
      if (
        _M0L6_2atmpS1526 < 0
        || _M0L6_2atmpS1526 >= Moonbit_array_length(_M0L6resultS608)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS608[_M0L6_2atmpS1526] = 43;
      _M0L6_2atmpS1527 = _M0Lm5indexS609;
      _M0Lm5indexS609 = _M0L6_2atmpS1527 + 1;
    }
    _M0L6_2atmpS1528 = _M0Lm3expS614;
    if (_M0L6_2atmpS1528 >= 100) {
      int32_t _M0L6_2atmpS1544 = _M0Lm3expS614;
      int32_t _M0L1aS622 = _M0L6_2atmpS1544 / 100;
      int32_t _M0L6_2atmpS1543 = _M0Lm3expS614;
      int32_t _M0L6_2atmpS1542 = _M0L6_2atmpS1543 / 10;
      int32_t _M0L1bS623 = _M0L6_2atmpS1542 % 10;
      int32_t _M0L6_2atmpS1541 = _M0Lm3expS614;
      int32_t _M0L1cS624 = _M0L6_2atmpS1541 % 10;
      int32_t _M0L6_2atmpS1529 = _M0Lm5indexS609;
      int32_t _M0L6_2atmpS1531 = 48 + _M0L1aS622;
      int32_t _M0L6_2atmpS1530 = _M0L6_2atmpS1531 & 0xff;
      int32_t _M0L6_2atmpS1535;
      int32_t _M0L6_2atmpS1532;
      int32_t _M0L6_2atmpS1534;
      int32_t _M0L6_2atmpS1533;
      int32_t _M0L6_2atmpS1539;
      int32_t _M0L6_2atmpS1536;
      int32_t _M0L6_2atmpS1538;
      int32_t _M0L6_2atmpS1537;
      int32_t _M0L6_2atmpS1540;
      if (
        _M0L6_2atmpS1529 < 0
        || _M0L6_2atmpS1529 >= Moonbit_array_length(_M0L6resultS608)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS608[_M0L6_2atmpS1529] = _M0L6_2atmpS1530;
      _M0L6_2atmpS1535 = _M0Lm5indexS609;
      _M0L6_2atmpS1532 = _M0L6_2atmpS1535 + 1;
      _M0L6_2atmpS1534 = 48 + _M0L1bS623;
      _M0L6_2atmpS1533 = _M0L6_2atmpS1534 & 0xff;
      if (
        _M0L6_2atmpS1532 < 0
        || _M0L6_2atmpS1532 >= Moonbit_array_length(_M0L6resultS608)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS608[_M0L6_2atmpS1532] = _M0L6_2atmpS1533;
      _M0L6_2atmpS1539 = _M0Lm5indexS609;
      _M0L6_2atmpS1536 = _M0L6_2atmpS1539 + 2;
      _M0L6_2atmpS1538 = 48 + _M0L1cS624;
      _M0L6_2atmpS1537 = _M0L6_2atmpS1538 & 0xff;
      if (
        _M0L6_2atmpS1536 < 0
        || _M0L6_2atmpS1536 >= Moonbit_array_length(_M0L6resultS608)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS608[_M0L6_2atmpS1536] = _M0L6_2atmpS1537;
      _M0L6_2atmpS1540 = _M0Lm5indexS609;
      _M0Lm5indexS609 = _M0L6_2atmpS1540 + 3;
    } else {
      int32_t _M0L6_2atmpS1545 = _M0Lm3expS614;
      if (_M0L6_2atmpS1545 >= 10) {
        int32_t _M0L6_2atmpS1555 = _M0Lm3expS614;
        int32_t _M0L1aS625 = _M0L6_2atmpS1555 / 10;
        int32_t _M0L6_2atmpS1554 = _M0Lm3expS614;
        int32_t _M0L1bS626 = _M0L6_2atmpS1554 % 10;
        int32_t _M0L6_2atmpS1546 = _M0Lm5indexS609;
        int32_t _M0L6_2atmpS1548 = 48 + _M0L1aS625;
        int32_t _M0L6_2atmpS1547 = _M0L6_2atmpS1548 & 0xff;
        int32_t _M0L6_2atmpS1552;
        int32_t _M0L6_2atmpS1549;
        int32_t _M0L6_2atmpS1551;
        int32_t _M0L6_2atmpS1550;
        int32_t _M0L6_2atmpS1553;
        if (
          _M0L6_2atmpS1546 < 0
          || _M0L6_2atmpS1546 >= Moonbit_array_length(_M0L6resultS608)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS608[_M0L6_2atmpS1546] = _M0L6_2atmpS1547;
        _M0L6_2atmpS1552 = _M0Lm5indexS609;
        _M0L6_2atmpS1549 = _M0L6_2atmpS1552 + 1;
        _M0L6_2atmpS1551 = 48 + _M0L1bS626;
        _M0L6_2atmpS1550 = _M0L6_2atmpS1551 & 0xff;
        if (
          _M0L6_2atmpS1549 < 0
          || _M0L6_2atmpS1549 >= Moonbit_array_length(_M0L6resultS608)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS608[_M0L6_2atmpS1549] = _M0L6_2atmpS1550;
        _M0L6_2atmpS1553 = _M0Lm5indexS609;
        _M0Lm5indexS609 = _M0L6_2atmpS1553 + 2;
      } else {
        int32_t _M0L6_2atmpS1556 = _M0Lm5indexS609;
        int32_t _M0L6_2atmpS1559 = _M0Lm3expS614;
        int32_t _M0L6_2atmpS1558 = 48 + _M0L6_2atmpS1559;
        int32_t _M0L6_2atmpS1557 = _M0L6_2atmpS1558 & 0xff;
        int32_t _M0L6_2atmpS1560;
        if (
          _M0L6_2atmpS1556 < 0
          || _M0L6_2atmpS1556 >= Moonbit_array_length(_M0L6resultS608)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS608[_M0L6_2atmpS1556] = _M0L6_2atmpS1557;
        _M0L6_2atmpS1560 = _M0Lm5indexS609;
        _M0Lm5indexS609 = _M0L6_2atmpS1560 + 1;
      }
    }
    _M0L6_2atmpS1561 = _M0Lm5indexS609;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2032
    = _M0FPB19string__from__bytes(_M0L6resultS608, 0, _M0L6_2atmpS1561);
    moonbit_decref_cycle_free(_M0L6resultS608);
    return _result_2032;
  } else {
    int32_t _M0L6_2atmpS1570 = _M0Lm3expS614;
    int32_t _M0L6_2atmpS1633;
    moonbit_string_t _result_2038;
    if (_M0L6_2atmpS1570 < 0) {
      int32_t _M0L6_2atmpS1571 = _M0Lm5indexS609;
      int32_t _M0L6_2atmpS1573;
      int32_t _M0L6_2atmpS1572;
      int32_t _M0L6_2atmpS1574;
      int32_t _M0L1iS627;
      int32_t _M0L6_2atmpS1589;
      int32_t _M0L6_2atmpS1591;
      int32_t _M0L6_2atmpS1590;
      int32_t _M0L7currentS629;
      int32_t _M0L1iS630;
      uint64_t _M0L6outputS631;
      if (
        _M0L6_2atmpS1571 < 0
        || _M0L6_2atmpS1571 >= Moonbit_array_length(_M0L6resultS608)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS608[_M0L6_2atmpS1571] = 48;
      _M0L6_2atmpS1573 = _M0Lm5indexS609;
      _M0L6_2atmpS1572 = _M0L6_2atmpS1573 + 1;
      if (
        _M0L6_2atmpS1572 < 0
        || _M0L6_2atmpS1572 >= Moonbit_array_length(_M0L6resultS608)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS608[_M0L6_2atmpS1572] = 46;
      _M0L6_2atmpS1574 = _M0Lm5indexS609;
      _M0Lm5indexS609 = _M0L6_2atmpS1574 + 2;
      _M0L1iS627 = -1;
      while (1) {
        int32_t _M0L6_2atmpS1575 = _M0Lm3expS614;
        if (_M0L1iS627 > _M0L6_2atmpS1575) {
          int32_t _M0L6_2atmpS1578 = _M0Lm5indexS609;
          int32_t _M0L6_2atmpS1577 = _M0L6_2atmpS1578 - _M0L1iS627;
          int32_t _M0L6_2atmpS1576 = _M0L6_2atmpS1577 - 1;
          int32_t _M0L6_2atmpS1579;
          if (
            _M0L6_2atmpS1576 < 0
            || _M0L6_2atmpS1576 >= Moonbit_array_length(_M0L6resultS608)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS608[_M0L6_2atmpS1576] = 48;
          _M0L6_2atmpS1579 = _M0L1iS627 - 1;
          _M0L1iS627 = _M0L6_2atmpS1579;
          continue;
        }
        break;
      }
      _M0L6_2atmpS1589 = _M0Lm5indexS609;
      _M0L6_2atmpS1591 = _M0Lm3expS614;
      _M0L6_2atmpS1590 = -1 - _M0L6_2atmpS1591;
      _M0L7currentS629 = _M0L6_2atmpS1589 + _M0L6_2atmpS1590;
      _M0L1iS630 = 0;
      _M0L6outputS631 = _M0L6outputS611;
      while (1) {
        if (_M0L1iS630 < _M0L7olengthS613) {
          int32_t _M0L6_2atmpS1586 = _M0L7currentS629 + _M0L7olengthS613;
          int32_t _M0L6_2atmpS1585 = _M0L6_2atmpS1586 - _M0L1iS630;
          int32_t _M0L6_2atmpS1580 = _M0L6_2atmpS1585 - 1;
          uint64_t _M0L6_2atmpS1584 = _M0L6outputS631 % 10ull;
          int32_t _M0L6_2atmpS1583 = (int32_t)_M0L6_2atmpS1584;
          int32_t _M0L6_2atmpS1582 = 48 + _M0L6_2atmpS1583;
          int32_t _M0L6_2atmpS1581 = _M0L6_2atmpS1582 & 0xff;
          int32_t _M0L6_2atmpS1587;
          uint64_t _M0L6_2atmpS1588;
          if (
            _M0L6_2atmpS1580 < 0
            || _M0L6_2atmpS1580 >= Moonbit_array_length(_M0L6resultS608)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS608[_M0L6_2atmpS1580] = _M0L6_2atmpS1581;
          _M0L6_2atmpS1587 = _M0L1iS630 + 1;
          _M0L6_2atmpS1588 = _M0L6outputS631 / 10ull;
          _M0L1iS630 = _M0L6_2atmpS1587;
          _M0L6outputS631 = _M0L6_2atmpS1588;
          continue;
        }
        break;
      }
      _M0Lm5indexS609 = _M0L7currentS629 + _M0L7olengthS613;
    } else {
      int32_t _M0L6_2atmpS1593 = _M0Lm3expS614;
      int32_t _M0L6_2atmpS1592 = _M0L6_2atmpS1593 + 1;
      if (_M0L6_2atmpS1592 >= _M0L7olengthS613) {
        int32_t _M0L1iS633 = 0;
        uint64_t _M0L6outputS634 = _M0L6outputS611;
        int32_t _M0L6_2atmpS1604;
        int32_t _M0L6_2atmpS1609;
        int32_t _M0L7_2abindS636;
        int32_t _M0L1iS637;
        int32_t _M0L6_2atmpS1610;
        int32_t _M0L6_2atmpS1613;
        int32_t _M0L6_2atmpS1612;
        int32_t _M0L6_2atmpS1611;
        while (1) {
          if (_M0L1iS633 < _M0L7olengthS613) {
            int32_t _M0L6_2atmpS1601 = _M0Lm5indexS609;
            int32_t _M0L6_2atmpS1600 = _M0L6_2atmpS1601 + _M0L7olengthS613;
            int32_t _M0L6_2atmpS1599 = _M0L6_2atmpS1600 - _M0L1iS633;
            int32_t _M0L6_2atmpS1594 = _M0L6_2atmpS1599 - 1;
            uint64_t _M0L6_2atmpS1598 = _M0L6outputS634 % 10ull;
            int32_t _M0L6_2atmpS1597 = (int32_t)_M0L6_2atmpS1598;
            int32_t _M0L6_2atmpS1596 = 48 + _M0L6_2atmpS1597;
            int32_t _M0L6_2atmpS1595 = _M0L6_2atmpS1596 & 0xff;
            int32_t _M0L6_2atmpS1602;
            uint64_t _M0L6_2atmpS1603;
            if (
              _M0L6_2atmpS1594 < 0
              || _M0L6_2atmpS1594 >= Moonbit_array_length(_M0L6resultS608)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS608[_M0L6_2atmpS1594] = _M0L6_2atmpS1595;
            _M0L6_2atmpS1602 = _M0L1iS633 + 1;
            _M0L6_2atmpS1603 = _M0L6outputS634 / 10ull;
            _M0L1iS633 = _M0L6_2atmpS1602;
            _M0L6outputS634 = _M0L6_2atmpS1603;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1604 = _M0Lm5indexS609;
        _M0Lm5indexS609 = _M0L6_2atmpS1604 + _M0L7olengthS613;
        _M0L6_2atmpS1609 = _M0Lm3expS614;
        _M0L7_2abindS636 = _M0L6_2atmpS1609 + 1;
        _M0L1iS637 = _M0L7olengthS613;
        while (1) {
          if (_M0L1iS637 < _M0L7_2abindS636) {
            int32_t _M0L6_2atmpS1607 = _M0Lm5indexS609;
            int32_t _M0L6_2atmpS1606 = _M0L6_2atmpS1607 + _M0L1iS637;
            int32_t _M0L6_2atmpS1605 = _M0L6_2atmpS1606 - _M0L7olengthS613;
            int32_t _M0L6_2atmpS1608;
            if (
              _M0L6_2atmpS1605 < 0
              || _M0L6_2atmpS1605 >= Moonbit_array_length(_M0L6resultS608)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS608[_M0L6_2atmpS1605] = 48;
            _M0L6_2atmpS1608 = _M0L1iS637 + 1;
            _M0L1iS637 = _M0L6_2atmpS1608;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1610 = _M0Lm5indexS609;
        _M0L6_2atmpS1613 = _M0Lm3expS614;
        _M0L6_2atmpS1612 = _M0L6_2atmpS1613 + 1;
        _M0L6_2atmpS1611 = _M0L6_2atmpS1612 - _M0L7olengthS613;
        _M0Lm5indexS609 = _M0L6_2atmpS1610 + _M0L6_2atmpS1611;
      } else {
        int32_t _M0L6_2atmpS1630 = _M0Lm5indexS609;
        int32_t _M0L6_2atmpS1629 = _M0L6_2atmpS1630 + 1;
        int32_t _M0L1iS639 = 0;
        int32_t _M0L7currentS640 = _M0L6_2atmpS1629;
        uint64_t _M0L6outputS641 = _M0L6outputS611;
        int32_t _M0L6_2atmpS1631;
        int32_t _M0L6_2atmpS1632;
        while (1) {
          if (_M0L1iS639 < _M0L7olengthS613) {
            int32_t _M0L6_2atmpS1625 = _M0L7olengthS613 - _M0L1iS639;
            int32_t _M0L6_2atmpS1623 = _M0L6_2atmpS1625 - 1;
            int32_t _M0L6_2atmpS1624 = _M0Lm3expS614;
            int32_t _M0L7currentS642;
            int32_t _M0L6_2atmpS1620;
            int32_t _M0L6_2atmpS1619;
            int32_t _M0L6_2atmpS1614;
            uint64_t _M0L6_2atmpS1618;
            int32_t _M0L6_2atmpS1617;
            int32_t _M0L6_2atmpS1616;
            int32_t _M0L6_2atmpS1615;
            int32_t _M0L6_2atmpS1621;
            uint64_t _M0L6_2atmpS1622;
            if (_M0L6_2atmpS1623 == _M0L6_2atmpS1624) {
              int32_t _M0L6_2atmpS1628 = _M0L7currentS640 + _M0L7olengthS613;
              int32_t _M0L6_2atmpS1627 = _M0L6_2atmpS1628 - _M0L1iS639;
              int32_t _M0L6_2atmpS1626 = _M0L6_2atmpS1627 - 1;
              if (
                _M0L6_2atmpS1626 < 0
                || _M0L6_2atmpS1626 >= Moonbit_array_length(_M0L6resultS608)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS608[_M0L6_2atmpS1626] = 46;
              _M0L7currentS642 = _M0L7currentS640 - 1;
            } else {
              _M0L7currentS642 = _M0L7currentS640;
            }
            _M0L6_2atmpS1620 = _M0L7currentS642 + _M0L7olengthS613;
            _M0L6_2atmpS1619 = _M0L6_2atmpS1620 - _M0L1iS639;
            _M0L6_2atmpS1614 = _M0L6_2atmpS1619 - 1;
            _M0L6_2atmpS1618 = _M0L6outputS641 % 10ull;
            _M0L6_2atmpS1617 = (int32_t)_M0L6_2atmpS1618;
            _M0L6_2atmpS1616 = 48 + _M0L6_2atmpS1617;
            _M0L6_2atmpS1615 = _M0L6_2atmpS1616 & 0xff;
            if (
              _M0L6_2atmpS1614 < 0
              || _M0L6_2atmpS1614 >= Moonbit_array_length(_M0L6resultS608)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS608[_M0L6_2atmpS1614] = _M0L6_2atmpS1615;
            _M0L6_2atmpS1621 = _M0L1iS639 + 1;
            _M0L6_2atmpS1622 = _M0L6outputS641 / 10ull;
            _M0L1iS639 = _M0L6_2atmpS1621;
            _M0L7currentS640 = _M0L7currentS642;
            _M0L6outputS641 = _M0L6_2atmpS1622;
            continue;
          }
          break;
        }
        _M0L6_2atmpS1631 = _M0Lm5indexS609;
        _M0L6_2atmpS1632 = _M0L7olengthS613 + 1;
        _M0Lm5indexS609 = _M0L6_2atmpS1631 + _M0L6_2atmpS1632;
      }
    }
    _M0L6_2atmpS1633 = _M0Lm5indexS609;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2038
    = _M0FPB19string__from__bytes(_M0L6resultS608, 0, _M0L6_2atmpS1633);
    moonbit_decref_cycle_free(_M0L6resultS608);
    return _result_2038;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS554,
  uint32_t _M0L12ieeeExponentS553
) {
  int32_t _M0Lm2e2S551;
  uint64_t _M0Lm2m2S552;
  uint64_t _M0L6_2atmpS1507;
  uint64_t _M0L6_2atmpS1506;
  int32_t _M0L4evenS555;
  uint64_t _M0L6_2atmpS1505;
  uint64_t _M0L2mvS556;
  int32_t _M0L7mmShiftS557;
  uint64_t _M0Lm2vrS558;
  uint64_t _M0Lm2vpS559;
  uint64_t _M0Lm2vmS560;
  int32_t _M0Lm3e10S561;
  int32_t _M0Lm17vmIsTrailingZerosS562;
  int32_t _M0Lm17vrIsTrailingZerosS563;
  int32_t _M0L6_2atmpS1407;
  int32_t _M0Lm7removedS582;
  int32_t _M0Lm16lastRemovedDigitS583;
  uint64_t _M0Lm6outputS584;
  int32_t _M0L6_2atmpS1503;
  int32_t _M0L6_2atmpS1504;
  int32_t _M0L3expS607;
  uint64_t _M0L6_2atmpS1502;
  struct _M0TPB17FloatingDecimal64* _block_2044;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S551 = 0;
  _M0Lm2m2S552 = 0ull;
  if (_M0L12ieeeExponentS553 == 0u) {
    _M0Lm2e2S551 = -1076;
    _M0Lm2m2S552 = _M0L12ieeeMantissaS554;
  } else {
    int32_t _M0L6_2atmpS1406 = *(int32_t*)&_M0L12ieeeExponentS553;
    int32_t _M0L6_2atmpS1405 = _M0L6_2atmpS1406 - 1023;
    int32_t _M0L6_2atmpS1404 = _M0L6_2atmpS1405 - 52;
    _M0Lm2e2S551 = _M0L6_2atmpS1404 - 2;
    _M0Lm2m2S552 = 4503599627370496ull | _M0L12ieeeMantissaS554;
  }
  _M0L6_2atmpS1507 = _M0Lm2m2S552;
  _M0L6_2atmpS1506 = _M0L6_2atmpS1507 & 1ull;
  _M0L4evenS555 = _M0L6_2atmpS1506 == 0ull;
  _M0L6_2atmpS1505 = _M0Lm2m2S552;
  _M0L2mvS556 = 4ull * _M0L6_2atmpS1505;
  _M0L7mmShiftS557
  = _M0L12ieeeMantissaS554 != 0ull || _M0L12ieeeExponentS553 <= 1u;
  _M0Lm2vrS558 = 0ull;
  _M0Lm2vpS559 = 0ull;
  _M0Lm2vmS560 = 0ull;
  _M0Lm3e10S561 = 0;
  _M0Lm17vmIsTrailingZerosS562 = 0;
  _M0Lm17vrIsTrailingZerosS563 = 0;
  _M0L6_2atmpS1407 = _M0Lm2e2S551;
  if (_M0L6_2atmpS1407 >= 0) {
    int32_t _M0L6_2atmpS1429 = _M0Lm2e2S551;
    int32_t _M0L6_2atmpS1425;
    int32_t _M0L6_2atmpS1428;
    int32_t _M0L6_2atmpS1427;
    int32_t _M0L6_2atmpS1426;
    int32_t _M0L1qS564;
    int32_t _M0L6_2atmpS1424;
    int32_t _M0L6_2atmpS1423;
    int32_t _M0L1kS565;
    int32_t _M0L6_2atmpS1422;
    int32_t _M0L6_2atmpS1421;
    int32_t _M0L6_2atmpS1420;
    int32_t _M0L1iS566;
    struct _M0TPB8Pow5Pair _M0L4pow5S567;
    uint64_t _M0L6_2atmpS1419;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS568;
    uint64_t _M0L8_2avrOutS569;
    uint64_t _M0L8_2avpOutS570;
    uint64_t _M0L8_2avmOutS571;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1425 = _M0FPB9log10Pow2(_M0L6_2atmpS1429);
    _M0L6_2atmpS1428 = _M0Lm2e2S551;
    _M0L6_2atmpS1427 = _M0L6_2atmpS1428 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1426 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1427);
    _M0L1qS564 = _M0L6_2atmpS1425 - _M0L6_2atmpS1426;
    _M0Lm3e10S561 = _M0L1qS564;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1424 = _M0FPB8pow5bits(_M0L1qS564);
    _M0L6_2atmpS1423 = 125 + _M0L6_2atmpS1424;
    _M0L1kS565 = _M0L6_2atmpS1423 - 1;
    _M0L6_2atmpS1422 = _M0Lm2e2S551;
    _M0L6_2atmpS1421 = -_M0L6_2atmpS1422;
    _M0L6_2atmpS1420 = _M0L6_2atmpS1421 + _M0L1qS564;
    _M0L1iS566 = _M0L6_2atmpS1420 + _M0L1kS565;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S567 = _M0FPB22double__computeInvPow5(_M0L1qS564);
    _M0L6_2atmpS1419 = _M0Lm2m2S552;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS568
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1419, _M0L4pow5S567, _M0L1iS566, _M0L7mmShiftS557);
    _M0L8_2avrOutS569 = _M0L7_2abindS568.$0;
    _M0L8_2avpOutS570 = _M0L7_2abindS568.$1;
    _M0L8_2avmOutS571 = _M0L7_2abindS568.$2;
    _M0Lm2vrS558 = _M0L8_2avrOutS569;
    _M0Lm2vpS559 = _M0L8_2avpOutS570;
    _M0Lm2vmS560 = _M0L8_2avmOutS571;
    if (_M0L1qS564 <= 21) {
      int32_t _M0L6_2atmpS1415 = (int32_t)_M0L2mvS556;
      uint64_t _M0L6_2atmpS1418 = _M0L2mvS556 / 5ull;
      int32_t _M0L6_2atmpS1417 = (int32_t)_M0L6_2atmpS1418;
      int32_t _M0L6_2atmpS1416 = 5 * _M0L6_2atmpS1417;
      int32_t _M0L6mvMod5S572 = _M0L6_2atmpS1415 - _M0L6_2atmpS1416;
      if (_M0L6mvMod5S572 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS563
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS556, _M0L1qS564);
      } else if (_M0L4evenS555) {
        uint64_t _M0L6_2atmpS1409 = _M0L2mvS556 - 1ull;
        uint64_t _M0L6_2atmpS1410;
        uint64_t _M0L6_2atmpS1408;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1410 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS557);
        _M0L6_2atmpS1408 = _M0L6_2atmpS1409 - _M0L6_2atmpS1410;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS562
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1408, _M0L1qS564);
      } else {
        uint64_t _M0L6_2atmpS1411 = _M0Lm2vpS559;
        uint64_t _M0L6_2atmpS1414 = _M0L2mvS556 + 2ull;
        int32_t _M0L6_2atmpS1413;
        uint64_t _M0L6_2atmpS1412;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1413
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS1414, _M0L1qS564);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1412 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1413);
        _M0Lm2vpS559 = _M0L6_2atmpS1411 - _M0L6_2atmpS1412;
      }
    }
  } else {
    int32_t _M0L6_2atmpS1443 = _M0Lm2e2S551;
    int32_t _M0L6_2atmpS1442 = -_M0L6_2atmpS1443;
    int32_t _M0L6_2atmpS1437;
    int32_t _M0L6_2atmpS1441;
    int32_t _M0L6_2atmpS1440;
    int32_t _M0L6_2atmpS1439;
    int32_t _M0L6_2atmpS1438;
    int32_t _M0L1qS573;
    int32_t _M0L6_2atmpS1430;
    int32_t _M0L6_2atmpS1436;
    int32_t _M0L6_2atmpS1435;
    int32_t _M0L1iS574;
    int32_t _M0L6_2atmpS1434;
    int32_t _M0L1kS575;
    int32_t _M0L1jS576;
    struct _M0TPB8Pow5Pair _M0L4pow5S577;
    uint64_t _M0L6_2atmpS1433;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS578;
    uint64_t _M0L8_2avrOutS579;
    uint64_t _M0L8_2avpOutS580;
    uint64_t _M0L8_2avmOutS581;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1437 = _M0FPB9log10Pow5(_M0L6_2atmpS1442);
    _M0L6_2atmpS1441 = _M0Lm2e2S551;
    _M0L6_2atmpS1440 = -_M0L6_2atmpS1441;
    _M0L6_2atmpS1439 = _M0L6_2atmpS1440 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1438 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS1439);
    _M0L1qS573 = _M0L6_2atmpS1437 - _M0L6_2atmpS1438;
    _M0L6_2atmpS1430 = _M0Lm2e2S551;
    _M0Lm3e10S561 = _M0L1qS573 + _M0L6_2atmpS1430;
    _M0L6_2atmpS1436 = _M0Lm2e2S551;
    _M0L6_2atmpS1435 = -_M0L6_2atmpS1436;
    _M0L1iS574 = _M0L6_2atmpS1435 - _M0L1qS573;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1434 = _M0FPB8pow5bits(_M0L1iS574);
    _M0L1kS575 = _M0L6_2atmpS1434 - 125;
    _M0L1jS576 = _M0L1qS573 - _M0L1kS575;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S577 = _M0FPB19double__computePow5(_M0L1iS574);
    _M0L6_2atmpS1433 = _M0Lm2m2S552;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS578
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS1433, _M0L4pow5S577, _M0L1jS576, _M0L7mmShiftS557);
    _M0L8_2avrOutS579 = _M0L7_2abindS578.$0;
    _M0L8_2avpOutS580 = _M0L7_2abindS578.$1;
    _M0L8_2avmOutS581 = _M0L7_2abindS578.$2;
    _M0Lm2vrS558 = _M0L8_2avrOutS579;
    _M0Lm2vpS559 = _M0L8_2avpOutS580;
    _M0Lm2vmS560 = _M0L8_2avmOutS581;
    if (_M0L1qS573 <= 1) {
      _M0Lm17vrIsTrailingZerosS563 = 1;
      if (_M0L4evenS555) {
        int32_t _M0L6_2atmpS1431;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS1431 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS557);
        _M0Lm17vmIsTrailingZerosS562 = _M0L6_2atmpS1431 == 1;
      } else {
        uint64_t _M0L6_2atmpS1432 = _M0Lm2vpS559;
        _M0Lm2vpS559 = _M0L6_2atmpS1432 - 1ull;
      }
    } else if (_M0L1qS573 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS563
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS556, _M0L1qS573);
    }
  }
  _M0Lm7removedS582 = 0;
  _M0Lm16lastRemovedDigitS583 = 0;
  _M0Lm6outputS584 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS562 || _M0Lm17vrIsTrailingZerosS563) {
    int32_t _if__result_2041;
    uint64_t _M0L6_2atmpS1473;
    uint64_t _M0L6_2atmpS1479;
    uint64_t _M0L6_2atmpS1480;
    int32_t _if__result_2042;
    int32_t _M0L6_2atmpS1476;
    int64_t _M0L6_2atmpS1475;
    uint64_t _M0L6_2atmpS1474;
    while (1) {
      uint64_t _M0L6_2atmpS1456 = _M0Lm2vpS559;
      uint64_t _M0L7vpDiv10S585 = _M0L6_2atmpS1456 / 10ull;
      uint64_t _M0L6_2atmpS1455 = _M0Lm2vmS560;
      uint64_t _M0L7vmDiv10S586 = _M0L6_2atmpS1455 / 10ull;
      uint64_t _M0L6_2atmpS1454;
      int32_t _M0L6_2atmpS1451;
      int32_t _M0L6_2atmpS1453;
      int32_t _M0L6_2atmpS1452;
      int32_t _M0L7vmMod10S588;
      uint64_t _M0L6_2atmpS1450;
      uint64_t _M0L7vrDiv10S589;
      uint64_t _M0L6_2atmpS1449;
      int32_t _M0L6_2atmpS1446;
      int32_t _M0L6_2atmpS1448;
      int32_t _M0L6_2atmpS1447;
      int32_t _M0L7vrMod10S590;
      int32_t _M0L6_2atmpS1445;
      if (_M0L7vpDiv10S585 <= _M0L7vmDiv10S586) {
        break;
      }
      _M0L6_2atmpS1454 = _M0Lm2vmS560;
      _M0L6_2atmpS1451 = (int32_t)_M0L6_2atmpS1454;
      _M0L6_2atmpS1453 = (int32_t)_M0L7vmDiv10S586;
      _M0L6_2atmpS1452 = 10 * _M0L6_2atmpS1453;
      _M0L7vmMod10S588 = _M0L6_2atmpS1451 - _M0L6_2atmpS1452;
      _M0L6_2atmpS1450 = _M0Lm2vrS558;
      _M0L7vrDiv10S589 = _M0L6_2atmpS1450 / 10ull;
      _M0L6_2atmpS1449 = _M0Lm2vrS558;
      _M0L6_2atmpS1446 = (int32_t)_M0L6_2atmpS1449;
      _M0L6_2atmpS1448 = (int32_t)_M0L7vrDiv10S589;
      _M0L6_2atmpS1447 = 10 * _M0L6_2atmpS1448;
      _M0L7vrMod10S590 = _M0L6_2atmpS1446 - _M0L6_2atmpS1447;
      _M0Lm17vmIsTrailingZerosS562
      = _M0Lm17vmIsTrailingZerosS562 && _M0L7vmMod10S588 == 0;
      if (_M0Lm17vrIsTrailingZerosS563) {
        int32_t _M0L6_2atmpS1444 = _M0Lm16lastRemovedDigitS583;
        _M0Lm17vrIsTrailingZerosS563 = _M0L6_2atmpS1444 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS563 = 0;
      }
      _M0Lm16lastRemovedDigitS583 = _M0L7vrMod10S590;
      _M0Lm2vrS558 = _M0L7vrDiv10S589;
      _M0Lm2vpS559 = _M0L7vpDiv10S585;
      _M0Lm2vmS560 = _M0L7vmDiv10S586;
      _M0L6_2atmpS1445 = _M0Lm7removedS582;
      _M0Lm7removedS582 = _M0L6_2atmpS1445 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS562) {
      while (1) {
        uint64_t _M0L6_2atmpS1469 = _M0Lm2vmS560;
        uint64_t _M0L7vmDiv10S591 = _M0L6_2atmpS1469 / 10ull;
        uint64_t _M0L6_2atmpS1468 = _M0Lm2vmS560;
        int32_t _M0L6_2atmpS1465 = (int32_t)_M0L6_2atmpS1468;
        int32_t _M0L6_2atmpS1467 = (int32_t)_M0L7vmDiv10S591;
        int32_t _M0L6_2atmpS1466 = 10 * _M0L6_2atmpS1467;
        int32_t _M0L7vmMod10S592 = _M0L6_2atmpS1465 - _M0L6_2atmpS1466;
        uint64_t _M0L6_2atmpS1464;
        uint64_t _M0L7vpDiv10S594;
        uint64_t _M0L6_2atmpS1463;
        uint64_t _M0L7vrDiv10S595;
        uint64_t _M0L6_2atmpS1462;
        int32_t _M0L6_2atmpS1459;
        int32_t _M0L6_2atmpS1461;
        int32_t _M0L6_2atmpS1460;
        int32_t _M0L7vrMod10S596;
        int32_t _M0L6_2atmpS1458;
        if (_M0L7vmMod10S592 != 0) {
          break;
        }
        _M0L6_2atmpS1464 = _M0Lm2vpS559;
        _M0L7vpDiv10S594 = _M0L6_2atmpS1464 / 10ull;
        _M0L6_2atmpS1463 = _M0Lm2vrS558;
        _M0L7vrDiv10S595 = _M0L6_2atmpS1463 / 10ull;
        _M0L6_2atmpS1462 = _M0Lm2vrS558;
        _M0L6_2atmpS1459 = (int32_t)_M0L6_2atmpS1462;
        _M0L6_2atmpS1461 = (int32_t)_M0L7vrDiv10S595;
        _M0L6_2atmpS1460 = 10 * _M0L6_2atmpS1461;
        _M0L7vrMod10S596 = _M0L6_2atmpS1459 - _M0L6_2atmpS1460;
        if (_M0Lm17vrIsTrailingZerosS563) {
          int32_t _M0L6_2atmpS1457 = _M0Lm16lastRemovedDigitS583;
          _M0Lm17vrIsTrailingZerosS563 = _M0L6_2atmpS1457 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS563 = 0;
        }
        _M0Lm16lastRemovedDigitS583 = _M0L7vrMod10S596;
        _M0Lm2vrS558 = _M0L7vrDiv10S595;
        _M0Lm2vpS559 = _M0L7vpDiv10S594;
        _M0Lm2vmS560 = _M0L7vmDiv10S591;
        _M0L6_2atmpS1458 = _M0Lm7removedS582;
        _M0Lm7removedS582 = _M0L6_2atmpS1458 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS563) {
      int32_t _M0L6_2atmpS1472 = _M0Lm16lastRemovedDigitS583;
      if (_M0L6_2atmpS1472 == 5) {
        uint64_t _M0L6_2atmpS1471 = _M0Lm2vrS558;
        uint64_t _M0L6_2atmpS1470 = _M0L6_2atmpS1471 % 2ull;
        _if__result_2041 = _M0L6_2atmpS1470 == 0ull;
      } else {
        _if__result_2041 = 0;
      }
    } else {
      _if__result_2041 = 0;
    }
    if (_if__result_2041) {
      _M0Lm16lastRemovedDigitS583 = 4;
    }
    _M0L6_2atmpS1473 = _M0Lm2vrS558;
    _M0L6_2atmpS1479 = _M0Lm2vrS558;
    _M0L6_2atmpS1480 = _M0Lm2vmS560;
    if (_M0L6_2atmpS1479 == _M0L6_2atmpS1480) {
      if (!_M0L4evenS555) {
        _if__result_2042 = 1;
      } else {
        int32_t _M0L6_2atmpS1478 = _M0Lm17vmIsTrailingZerosS562;
        _if__result_2042 = !_M0L6_2atmpS1478;
      }
    } else {
      _if__result_2042 = 0;
    }
    if (_if__result_2042) {
      _M0L6_2atmpS1476 = 1;
    } else {
      int32_t _M0L6_2atmpS1477 = _M0Lm16lastRemovedDigitS583;
      _M0L6_2atmpS1476 = _M0L6_2atmpS1477 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1475 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS1476);
    _M0L6_2atmpS1474 = *(uint64_t*)&_M0L6_2atmpS1475;
    _M0Lm6outputS584 = _M0L6_2atmpS1473 + _M0L6_2atmpS1474;
  } else {
    int32_t _M0Lm7roundUpS597 = 0;
    uint64_t _M0L6_2atmpS1501 = _M0Lm2vpS559;
    uint64_t _M0L8vpDiv100S598 = _M0L6_2atmpS1501 / 100ull;
    uint64_t _M0L6_2atmpS1500 = _M0Lm2vmS560;
    uint64_t _M0L8vmDiv100S599 = _M0L6_2atmpS1500 / 100ull;
    uint64_t _M0L6_2atmpS1495;
    uint64_t _M0L6_2atmpS1498;
    uint64_t _M0L6_2atmpS1499;
    int32_t _M0L6_2atmpS1497;
    uint64_t _M0L6_2atmpS1496;
    if (_M0L8vpDiv100S598 > _M0L8vmDiv100S599) {
      uint64_t _M0L6_2atmpS1486 = _M0Lm2vrS558;
      uint64_t _M0L8vrDiv100S600 = _M0L6_2atmpS1486 / 100ull;
      uint64_t _M0L6_2atmpS1485 = _M0Lm2vrS558;
      int32_t _M0L6_2atmpS1482 = (int32_t)_M0L6_2atmpS1485;
      int32_t _M0L6_2atmpS1484 = (int32_t)_M0L8vrDiv100S600;
      int32_t _M0L6_2atmpS1483 = 100 * _M0L6_2atmpS1484;
      int32_t _M0L8vrMod100S601 = _M0L6_2atmpS1482 - _M0L6_2atmpS1483;
      int32_t _M0L6_2atmpS1481;
      _M0Lm7roundUpS597 = _M0L8vrMod100S601 >= 50;
      _M0Lm2vrS558 = _M0L8vrDiv100S600;
      _M0Lm2vpS559 = _M0L8vpDiv100S598;
      _M0Lm2vmS560 = _M0L8vmDiv100S599;
      _M0L6_2atmpS1481 = _M0Lm7removedS582;
      _M0Lm7removedS582 = _M0L6_2atmpS1481 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS1494 = _M0Lm2vpS559;
      uint64_t _M0L7vpDiv10S602 = _M0L6_2atmpS1494 / 10ull;
      uint64_t _M0L6_2atmpS1493 = _M0Lm2vmS560;
      uint64_t _M0L7vmDiv10S603 = _M0L6_2atmpS1493 / 10ull;
      uint64_t _M0L6_2atmpS1492;
      uint64_t _M0L7vrDiv10S605;
      uint64_t _M0L6_2atmpS1491;
      int32_t _M0L6_2atmpS1488;
      int32_t _M0L6_2atmpS1490;
      int32_t _M0L6_2atmpS1489;
      int32_t _M0L7vrMod10S606;
      int32_t _M0L6_2atmpS1487;
      if (_M0L7vpDiv10S602 <= _M0L7vmDiv10S603) {
        break;
      }
      _M0L6_2atmpS1492 = _M0Lm2vrS558;
      _M0L7vrDiv10S605 = _M0L6_2atmpS1492 / 10ull;
      _M0L6_2atmpS1491 = _M0Lm2vrS558;
      _M0L6_2atmpS1488 = (int32_t)_M0L6_2atmpS1491;
      _M0L6_2atmpS1490 = (int32_t)_M0L7vrDiv10S605;
      _M0L6_2atmpS1489 = 10 * _M0L6_2atmpS1490;
      _M0L7vrMod10S606 = _M0L6_2atmpS1488 - _M0L6_2atmpS1489;
      _M0Lm7roundUpS597 = _M0L7vrMod10S606 >= 5;
      _M0Lm2vrS558 = _M0L7vrDiv10S605;
      _M0Lm2vpS559 = _M0L7vpDiv10S602;
      _M0Lm2vmS560 = _M0L7vmDiv10S603;
      _M0L6_2atmpS1487 = _M0Lm7removedS582;
      _M0Lm7removedS582 = _M0L6_2atmpS1487 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS1495 = _M0Lm2vrS558;
    _M0L6_2atmpS1498 = _M0Lm2vrS558;
    _M0L6_2atmpS1499 = _M0Lm2vmS560;
    _M0L6_2atmpS1497
    = _M0L6_2atmpS1498 == _M0L6_2atmpS1499 || _M0Lm7roundUpS597;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS1496 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS1497);
    _M0Lm6outputS584 = _M0L6_2atmpS1495 + _M0L6_2atmpS1496;
  }
  _M0L6_2atmpS1503 = _M0Lm3e10S561;
  _M0L6_2atmpS1504 = _M0Lm7removedS582;
  _M0L3expS607 = _M0L6_2atmpS1503 + _M0L6_2atmpS1504;
  _M0L6_2atmpS1502 = _M0Lm6outputS584;
  _block_2044
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_2044)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_2044->$0 = _M0L6_2atmpS1502;
  _block_2044->$1 = _M0L3expS607;
  return _block_2044;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS550) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS550) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS549) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS549) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS548) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS548) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS547) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS547 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS547 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS547 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS547 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS547 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS547 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS547 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS547 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS547 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS547 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS547 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS547 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS547 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS547 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS547 >= 100ull) {
    return 3;
  }
  if (_M0L1vS547 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS530) {
  int32_t _M0L6_2atmpS1403;
  int32_t _M0L6_2atmpS1402;
  int32_t _M0L4baseS529;
  int32_t _M0L5base2S531;
  int32_t _M0L6offsetS532;
  int32_t _M0L6_2atmpS1401;
  uint64_t _M0L4mul0S533;
  int32_t _M0L6_2atmpS1400;
  int32_t _M0L6_2atmpS1399;
  uint64_t _M0L4mul1S534;
  uint64_t _M0L1mS535;
  struct _M0TPB7Umul128 _M0L7_2abindS536;
  uint64_t _M0L7_2alow1S537;
  uint64_t _M0L8_2ahigh1S538;
  struct _M0TPB7Umul128 _M0L7_2abindS539;
  uint64_t _M0L7_2alow0S540;
  uint64_t _M0L8_2ahigh0S541;
  uint64_t _M0L3sumS542;
  uint64_t _M0Lm5high1S543;
  int32_t _M0L6_2atmpS1397;
  int32_t _M0L6_2atmpS1398;
  int32_t _M0L5deltaS544;
  uint64_t _M0L6_2atmpS1396;
  uint64_t _M0L6_2atmpS1388;
  int32_t _M0L6_2atmpS1395;
  uint32_t _M0L6_2atmpS1392;
  int32_t _M0L6_2atmpS1394;
  int32_t _M0L6_2atmpS1393;
  uint32_t _M0L6_2atmpS1391;
  uint32_t _M0L6_2atmpS1390;
  uint64_t _M0L6_2atmpS1389;
  uint64_t _M0L1aS545;
  uint64_t _M0L6_2atmpS1387;
  uint64_t _M0L1bS546;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1403 = _M0L1iS530 + 26;
  _M0L6_2atmpS1402 = _M0L6_2atmpS1403 - 1;
  _M0L4baseS529 = _M0L6_2atmpS1402 / 26;
  _M0L5base2S531 = _M0L4baseS529 * 26;
  _M0L6offsetS532 = _M0L5base2S531 - _M0L1iS530;
  _M0L6_2atmpS1401 = _M0L4baseS529 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S533
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1401);
  _M0L6_2atmpS1400 = _M0L4baseS529 * 2;
  _M0L6_2atmpS1399 = _M0L6_2atmpS1400 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S534
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS1399);
  if (_M0L6offsetS532 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S533, .$1 = _M0L4mul1S534};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS535
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS532);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS536 = _M0FPB7umul128(_M0L1mS535, _M0L4mul1S534);
  _M0L7_2alow1S537 = _M0L7_2abindS536.$0;
  _M0L8_2ahigh1S538 = _M0L7_2abindS536.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS539 = _M0FPB7umul128(_M0L1mS535, _M0L4mul0S533);
  _M0L7_2alow0S540 = _M0L7_2abindS539.$0;
  _M0L8_2ahigh0S541 = _M0L7_2abindS539.$1;
  _M0L3sumS542 = _M0L8_2ahigh0S541 + _M0L7_2alow1S537;
  _M0Lm5high1S543 = _M0L8_2ahigh1S538;
  if (_M0L3sumS542 < _M0L8_2ahigh0S541) {
    uint64_t _M0L6_2atmpS1386 = _M0Lm5high1S543;
    _M0Lm5high1S543 = _M0L6_2atmpS1386 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1397 = _M0FPB8pow5bits(_M0L5base2S531);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1398 = _M0FPB8pow5bits(_M0L1iS530);
  _M0L5deltaS544 = _M0L6_2atmpS1397 - _M0L6_2atmpS1398;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1396
  = _M0FPB13shiftright128(_M0L7_2alow0S540, _M0L3sumS542, _M0L5deltaS544);
  _M0L6_2atmpS1388 = _M0L6_2atmpS1396 + 1ull;
  _M0L6_2atmpS1395 = _M0L1iS530 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1392
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS1395);
  _M0L6_2atmpS1394 = _M0L1iS530 % 16;
  _M0L6_2atmpS1393 = _M0L6_2atmpS1394 << 1;
  _M0L6_2atmpS1391 = _M0L6_2atmpS1392 >> (_M0L6_2atmpS1393 & 31);
  _M0L6_2atmpS1390 = _M0L6_2atmpS1391 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1389 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1390);
  _M0L1aS545 = _M0L6_2atmpS1388 + _M0L6_2atmpS1389;
  _M0L6_2atmpS1387 = _M0Lm5high1S543;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS546
  = _M0FPB13shiftright128(_M0L3sumS542, _M0L6_2atmpS1387, _M0L5deltaS544);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS545, .$1 = _M0L1bS546};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS512) {
  int32_t _M0L4baseS511;
  int32_t _M0L5base2S513;
  int32_t _M0L6offsetS514;
  int32_t _M0L6_2atmpS1385;
  uint64_t _M0L4mul0S515;
  int32_t _M0L6_2atmpS1384;
  int32_t _M0L6_2atmpS1383;
  uint64_t _M0L4mul1S516;
  uint64_t _M0L1mS517;
  struct _M0TPB7Umul128 _M0L7_2abindS518;
  uint64_t _M0L7_2alow1S519;
  uint64_t _M0L8_2ahigh1S520;
  struct _M0TPB7Umul128 _M0L7_2abindS521;
  uint64_t _M0L7_2alow0S522;
  uint64_t _M0L8_2ahigh0S523;
  uint64_t _M0L3sumS524;
  uint64_t _M0Lm5high1S525;
  int32_t _M0L6_2atmpS1381;
  int32_t _M0L6_2atmpS1382;
  int32_t _M0L5deltaS526;
  uint64_t _M0L6_2atmpS1373;
  int32_t _M0L6_2atmpS1380;
  uint32_t _M0L6_2atmpS1377;
  int32_t _M0L6_2atmpS1379;
  int32_t _M0L6_2atmpS1378;
  uint32_t _M0L6_2atmpS1376;
  uint32_t _M0L6_2atmpS1375;
  uint64_t _M0L6_2atmpS1374;
  uint64_t _M0L1aS527;
  uint64_t _M0L6_2atmpS1372;
  uint64_t _M0L1bS528;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS511 = _M0L1iS512 / 26;
  _M0L5base2S513 = _M0L4baseS511 * 26;
  _M0L6offsetS514 = _M0L1iS512 - _M0L5base2S513;
  _M0L6_2atmpS1385 = _M0L4baseS511 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S515
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1385);
  _M0L6_2atmpS1384 = _M0L4baseS511 * 2;
  _M0L6_2atmpS1383 = _M0L6_2atmpS1384 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S516
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS1383);
  if (_M0L6offsetS514 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S515, .$1 = _M0L4mul1S516};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS517
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS514);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS518 = _M0FPB7umul128(_M0L1mS517, _M0L4mul1S516);
  _M0L7_2alow1S519 = _M0L7_2abindS518.$0;
  _M0L8_2ahigh1S520 = _M0L7_2abindS518.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS521 = _M0FPB7umul128(_M0L1mS517, _M0L4mul0S515);
  _M0L7_2alow0S522 = _M0L7_2abindS521.$0;
  _M0L8_2ahigh0S523 = _M0L7_2abindS521.$1;
  _M0L3sumS524 = _M0L8_2ahigh0S523 + _M0L7_2alow1S519;
  _M0Lm5high1S525 = _M0L8_2ahigh1S520;
  if (_M0L3sumS524 < _M0L8_2ahigh0S523) {
    uint64_t _M0L6_2atmpS1371 = _M0Lm5high1S525;
    _M0Lm5high1S525 = _M0L6_2atmpS1371 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1381 = _M0FPB8pow5bits(_M0L1iS512);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1382 = _M0FPB8pow5bits(_M0L5base2S513);
  _M0L5deltaS526 = _M0L6_2atmpS1381 - _M0L6_2atmpS1382;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1373
  = _M0FPB13shiftright128(_M0L7_2alow0S522, _M0L3sumS524, _M0L5deltaS526);
  _M0L6_2atmpS1380 = _M0L1iS512 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1377
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS1380);
  _M0L6_2atmpS1379 = _M0L1iS512 % 16;
  _M0L6_2atmpS1378 = _M0L6_2atmpS1379 << 1;
  _M0L6_2atmpS1376 = _M0L6_2atmpS1377 >> (_M0L6_2atmpS1378 & 31);
  _M0L6_2atmpS1375 = _M0L6_2atmpS1376 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1374 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS1375);
  _M0L1aS527 = _M0L6_2atmpS1373 + _M0L6_2atmpS1374;
  _M0L6_2atmpS1372 = _M0Lm5high1S525;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS528
  = _M0FPB13shiftright128(_M0L3sumS524, _M0L6_2atmpS1372, _M0L5deltaS526);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS527, .$1 = _M0L1bS528};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS485,
  struct _M0TPB8Pow5Pair _M0L3mulS482,
  int32_t _M0L1jS498,
  int32_t _M0L7mmShiftS500
) {
  uint64_t _M0L7_2amul0S481;
  uint64_t _M0L7_2amul1S483;
  uint64_t _M0L1mS484;
  struct _M0TPB7Umul128 _M0L7_2abindS486;
  uint64_t _M0L5_2aloS487;
  uint64_t _M0L6_2atmpS488;
  struct _M0TPB7Umul128 _M0L7_2abindS489;
  uint64_t _M0L6_2alo2S490;
  uint64_t _M0L6_2ahi2S491;
  uint64_t _M0L3midS492;
  uint64_t _M0L6_2atmpS1370;
  uint64_t _M0L2hiS493;
  uint64_t _M0L3lo2S494;
  uint64_t _M0L6_2atmpS1368;
  uint64_t _M0L6_2atmpS1369;
  uint64_t _M0L4mid2S495;
  uint64_t _M0L6_2atmpS1367;
  uint64_t _M0L3hi2S496;
  int32_t _M0L6_2atmpS1366;
  int32_t _M0L6_2atmpS1365;
  uint64_t _M0L2vpS497;
  uint64_t _M0Lm2vmS499;
  int32_t _M0L6_2atmpS1364;
  int32_t _M0L6_2atmpS1363;
  uint64_t _M0L2vrS510;
  uint64_t _M0L6_2atmpS1362;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S481 = _M0L3mulS482.$0;
  _M0L7_2amul1S483 = _M0L3mulS482.$1;
  _M0L1mS484 = _M0L1mS485 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS486 = _M0FPB7umul128(_M0L1mS484, _M0L7_2amul0S481);
  _M0L5_2aloS487 = _M0L7_2abindS486.$0;
  _M0L6_2atmpS488 = _M0L7_2abindS486.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS489 = _M0FPB7umul128(_M0L1mS484, _M0L7_2amul1S483);
  _M0L6_2alo2S490 = _M0L7_2abindS489.$0;
  _M0L6_2ahi2S491 = _M0L7_2abindS489.$1;
  _M0L3midS492 = _M0L6_2atmpS488 + _M0L6_2alo2S490;
  if (_M0L3midS492 < _M0L6_2atmpS488) {
    _M0L6_2atmpS1370 = 1ull;
  } else {
    _M0L6_2atmpS1370 = 0ull;
  }
  _M0L2hiS493 = _M0L6_2ahi2S491 + _M0L6_2atmpS1370;
  _M0L3lo2S494 = _M0L5_2aloS487 + _M0L7_2amul0S481;
  _M0L6_2atmpS1368 = _M0L3midS492 + _M0L7_2amul1S483;
  if (_M0L3lo2S494 < _M0L5_2aloS487) {
    _M0L6_2atmpS1369 = 1ull;
  } else {
    _M0L6_2atmpS1369 = 0ull;
  }
  _M0L4mid2S495 = _M0L6_2atmpS1368 + _M0L6_2atmpS1369;
  if (_M0L4mid2S495 < _M0L3midS492) {
    _M0L6_2atmpS1367 = 1ull;
  } else {
    _M0L6_2atmpS1367 = 0ull;
  }
  _M0L3hi2S496 = _M0L2hiS493 + _M0L6_2atmpS1367;
  _M0L6_2atmpS1366 = _M0L1jS498 - 64;
  _M0L6_2atmpS1365 = _M0L6_2atmpS1366 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS497
  = _M0FPB13shiftright128(_M0L4mid2S495, _M0L3hi2S496, _M0L6_2atmpS1365);
  _M0Lm2vmS499 = 0ull;
  if (_M0L7mmShiftS500) {
    uint64_t _M0L3lo3S501 = _M0L5_2aloS487 - _M0L7_2amul0S481;
    uint64_t _M0L6_2atmpS1352 = _M0L3midS492 - _M0L7_2amul1S483;
    uint64_t _M0L6_2atmpS1353;
    uint64_t _M0L4mid3S502;
    uint64_t _M0L6_2atmpS1351;
    uint64_t _M0L3hi3S503;
    int32_t _M0L6_2atmpS1350;
    int32_t _M0L6_2atmpS1349;
    if (_M0L5_2aloS487 < _M0L3lo3S501) {
      _M0L6_2atmpS1353 = 1ull;
    } else {
      _M0L6_2atmpS1353 = 0ull;
    }
    _M0L4mid3S502 = _M0L6_2atmpS1352 - _M0L6_2atmpS1353;
    if (_M0L3midS492 < _M0L4mid3S502) {
      _M0L6_2atmpS1351 = 1ull;
    } else {
      _M0L6_2atmpS1351 = 0ull;
    }
    _M0L3hi3S503 = _M0L2hiS493 - _M0L6_2atmpS1351;
    _M0L6_2atmpS1350 = _M0L1jS498 - 64;
    _M0L6_2atmpS1349 = _M0L6_2atmpS1350 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS499
    = _M0FPB13shiftright128(_M0L4mid3S502, _M0L3hi3S503, _M0L6_2atmpS1349);
  } else {
    uint64_t _M0L3lo3S504 = _M0L5_2aloS487 + _M0L5_2aloS487;
    uint64_t _M0L6_2atmpS1360 = _M0L3midS492 + _M0L3midS492;
    uint64_t _M0L6_2atmpS1361;
    uint64_t _M0L4mid3S505;
    uint64_t _M0L6_2atmpS1358;
    uint64_t _M0L6_2atmpS1359;
    uint64_t _M0L3hi3S506;
    uint64_t _M0L3lo4S507;
    uint64_t _M0L6_2atmpS1356;
    uint64_t _M0L6_2atmpS1357;
    uint64_t _M0L4mid4S508;
    uint64_t _M0L6_2atmpS1355;
    uint64_t _M0L3hi4S509;
    int32_t _M0L6_2atmpS1354;
    if (_M0L3lo3S504 < _M0L5_2aloS487) {
      _M0L6_2atmpS1361 = 1ull;
    } else {
      _M0L6_2atmpS1361 = 0ull;
    }
    _M0L4mid3S505 = _M0L6_2atmpS1360 + _M0L6_2atmpS1361;
    _M0L6_2atmpS1358 = _M0L2hiS493 + _M0L2hiS493;
    if (_M0L4mid3S505 < _M0L3midS492) {
      _M0L6_2atmpS1359 = 1ull;
    } else {
      _M0L6_2atmpS1359 = 0ull;
    }
    _M0L3hi3S506 = _M0L6_2atmpS1358 + _M0L6_2atmpS1359;
    _M0L3lo4S507 = _M0L3lo3S504 - _M0L7_2amul0S481;
    _M0L6_2atmpS1356 = _M0L4mid3S505 - _M0L7_2amul1S483;
    if (_M0L3lo3S504 < _M0L3lo4S507) {
      _M0L6_2atmpS1357 = 1ull;
    } else {
      _M0L6_2atmpS1357 = 0ull;
    }
    _M0L4mid4S508 = _M0L6_2atmpS1356 - _M0L6_2atmpS1357;
    if (_M0L4mid3S505 < _M0L4mid4S508) {
      _M0L6_2atmpS1355 = 1ull;
    } else {
      _M0L6_2atmpS1355 = 0ull;
    }
    _M0L3hi4S509 = _M0L3hi3S506 - _M0L6_2atmpS1355;
    _M0L6_2atmpS1354 = _M0L1jS498 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS499
    = _M0FPB13shiftright128(_M0L4mid4S508, _M0L3hi4S509, _M0L6_2atmpS1354);
  }
  _M0L6_2atmpS1364 = _M0L1jS498 - 64;
  _M0L6_2atmpS1363 = _M0L6_2atmpS1364 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS510
  = _M0FPB13shiftright128(_M0L3midS492, _M0L2hiS493, _M0L6_2atmpS1363);
  _M0L6_2atmpS1362 = _M0Lm2vmS499;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS510,
                                                .$1 = _M0L2vpS497,
                                                .$2 = _M0L6_2atmpS1362};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS479,
  int32_t _M0L1pS480
) {
  uint64_t _M0L6_2atmpS1348;
  uint64_t _M0L6_2atmpS1347;
  uint64_t _M0L6_2atmpS1346;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1348 = 1ull << (_M0L1pS480 & 63);
  _M0L6_2atmpS1347 = _M0L6_2atmpS1348 - 1ull;
  _M0L6_2atmpS1346 = _M0L5valueS479 & _M0L6_2atmpS1347;
  return _M0L6_2atmpS1346 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS477,
  int32_t _M0L1pS478
) {
  int32_t _M0L6_2atmpS1345;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1345 = _M0FPB10pow5Factor(_M0L5valueS477);
  return _M0L6_2atmpS1345 >= _M0L1pS478;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS472) {
  uint64_t _M0L6_2atmpS1336;
  uint64_t _M0L6_2atmpS1337;
  uint64_t _M0L6_2atmpS1338;
  uint64_t _M0L6_2atmpS1339;
  uint64_t _M0L6_2atmpS1344;
  int32_t _M0L5countS473;
  uint64_t _M0L1vS474;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1336 = _M0L5valueS472 % 5ull;
  if (_M0L6_2atmpS1336 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS1337 = _M0L5valueS472 % 25ull;
  if (_M0L6_2atmpS1337 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS1338 = _M0L5valueS472 % 125ull;
  if (_M0L6_2atmpS1338 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS1339 = _M0L5valueS472 % 625ull;
  if (_M0L6_2atmpS1339 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS1344 = _M0L5valueS472 / 625ull;
  _M0L5countS473 = 4;
  _M0L1vS474 = _M0L6_2atmpS1344;
  while (1) {
    if (_M0L1vS474 > 0ull) {
      uint64_t _M0L6_2atmpS1340 = _M0L1vS474 % 5ull;
      int32_t _M0L6_2atmpS1341;
      uint64_t _M0L6_2atmpS1342;
      if (_M0L6_2atmpS1340 != 0ull) {
        return _M0L5countS473;
      }
      _M0L6_2atmpS1341 = _M0L5countS473 + 1;
      _M0L6_2atmpS1342 = _M0L1vS474 / 5ull;
      _M0L5countS473 = _M0L6_2atmpS1341;
      _M0L1vS474 = _M0L6_2atmpS1342;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS476;
      moonbit_string_t _M0L6_2atmpS1343;
      int32_t _result_2046;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS476
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS476, (moonbit_string_t)moonbit_string_literal_10.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS476, _M0L5valueS472);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS1343
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS476);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS476);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_2046 = _M0FPC15abort5abortGiE(_M0L6_2atmpS1343);
      moonbit_decref_cycle_free(_M0L6_2atmpS1343);
      return _result_2046;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS471,
  uint64_t _M0L2hiS469,
  int32_t _M0L4distS470
) {
  int32_t _M0L6_2atmpS1335;
  uint64_t _M0L6_2atmpS1333;
  uint64_t _M0L6_2atmpS1334;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1335 = 64 - _M0L4distS470;
  _M0L6_2atmpS1333 = _M0L2hiS469 << (_M0L6_2atmpS1335 & 63);
  _M0L6_2atmpS1334 = _M0L2loS471 >> (_M0L4distS470 & 63);
  return _M0L6_2atmpS1333 | _M0L6_2atmpS1334;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS459,
  uint64_t _M0L1bS462
) {
  uint64_t _M0L3aLoS458;
  uint64_t _M0L3aHiS460;
  uint64_t _M0L3bLoS461;
  uint64_t _M0L3bHiS463;
  uint64_t _M0L1xS464;
  uint64_t _M0L6_2atmpS1331;
  uint64_t _M0L6_2atmpS1332;
  uint64_t _M0L1yS465;
  uint64_t _M0L6_2atmpS1329;
  uint64_t _M0L6_2atmpS1330;
  uint64_t _M0L1zS466;
  uint64_t _M0L6_2atmpS1327;
  uint64_t _M0L6_2atmpS1328;
  uint64_t _M0L6_2atmpS1325;
  uint64_t _M0L6_2atmpS1326;
  uint64_t _M0L1wS467;
  uint64_t _M0L2loS468;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS458 = _M0L1aS459 & 4294967295ull;
  _M0L3aHiS460 = _M0L1aS459 >> 32;
  _M0L3bLoS461 = _M0L1bS462 & 4294967295ull;
  _M0L3bHiS463 = _M0L1bS462 >> 32;
  _M0L1xS464 = _M0L3aLoS458 * _M0L3bLoS461;
  _M0L6_2atmpS1331 = _M0L3aHiS460 * _M0L3bLoS461;
  _M0L6_2atmpS1332 = _M0L1xS464 >> 32;
  _M0L1yS465 = _M0L6_2atmpS1331 + _M0L6_2atmpS1332;
  _M0L6_2atmpS1329 = _M0L3aLoS458 * _M0L3bHiS463;
  _M0L6_2atmpS1330 = _M0L1yS465 & 4294967295ull;
  _M0L1zS466 = _M0L6_2atmpS1329 + _M0L6_2atmpS1330;
  _M0L6_2atmpS1327 = _M0L3aHiS460 * _M0L3bHiS463;
  _M0L6_2atmpS1328 = _M0L1yS465 >> 32;
  _M0L6_2atmpS1325 = _M0L6_2atmpS1327 + _M0L6_2atmpS1328;
  _M0L6_2atmpS1326 = _M0L1zS466 >> 32;
  _M0L1wS467 = _M0L6_2atmpS1325 + _M0L6_2atmpS1326;
  _M0L2loS468 = _M0L1aS459 * _M0L1bS462;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS468, .$1 = _M0L1wS467};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS456,
  int32_t _M0L4fromS453,
  int32_t _M0L2toS452
) {
  int32_t _M0L3lenS451;
  int32_t _M0L6_2atmpS1324;
  uint16_t* _M0L6bufferS454;
  int32_t _M0L1iS455;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS451 = _M0L2toS452 - _M0L4fromS453;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1324 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS454
  = (uint16_t*)moonbit_make_string(_M0L3lenS451, _M0L6_2atmpS1324);
  _M0L1iS455 = 0;
  while (1) {
    if (_M0L1iS455 < _M0L3lenS451) {
      int32_t _M0L6_2atmpS1322 = _M0L4fromS453 + _M0L1iS455;
      int32_t _M0L6_2atmpS1321;
      int32_t _M0L6_2atmpS1320;
      int32_t _M0L6_2atmpS1323;
      if (
        _M0L6_2atmpS1322 < 0
        || _M0L6_2atmpS1322 >= Moonbit_array_length(_M0L5bytesS456)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS1321 = (int32_t)_M0L5bytesS456[_M0L6_2atmpS1322];
      _M0L6_2atmpS1320 = (uint16_t)_M0L6_2atmpS1321;
      if (
        _M0L1iS455 < 0 || _M0L1iS455 >= Moonbit_array_length(_M0L6bufferS454)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS454[_M0L1iS455] = _M0L6_2atmpS1320;
      _M0L6_2atmpS1323 = _M0L1iS455 + 1;
      _M0L1iS455 = _M0L6_2atmpS1323;
      continue;
    }
    break;
  }
  return _M0L6bufferS454;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS450) {
  int32_t _M0L6_2atmpS1319;
  uint32_t _M0L6_2atmpS1318;
  uint32_t _M0L6_2atmpS1317;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1319 = _M0L1eS450 * 78913;
  _M0L6_2atmpS1318 = *(uint32_t*)&_M0L6_2atmpS1319;
  _M0L6_2atmpS1317 = _M0L6_2atmpS1318 >> 18;
  return *(int32_t*)&_M0L6_2atmpS1317;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS449) {
  int32_t _M0L6_2atmpS1316;
  uint32_t _M0L6_2atmpS1315;
  uint32_t _M0L6_2atmpS1314;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1316 = _M0L1eS449 * 732923;
  _M0L6_2atmpS1315 = *(uint32_t*)&_M0L6_2atmpS1316;
  _M0L6_2atmpS1314 = _M0L6_2atmpS1315 >> 20;
  return *(int32_t*)&_M0L6_2atmpS1314;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS447,
  int32_t _M0L8exponentS448,
  int32_t _M0L8mantissaS445
) {
  moonbit_string_t _M0L1sS446;
  moonbit_string_t _result_2049;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS445) {
    return (moonbit_string_t)moonbit_string_literal_11.data;
  }
  if (_M0L4signS447) {
    _M0L1sS446 = (moonbit_string_t)moonbit_string_literal_12.data;
  } else {
    _M0L1sS446 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS448) {
    moonbit_string_t _result_2048;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_2048
    = moonbit_add_string(_M0L1sS446, (moonbit_string_t)moonbit_string_literal_13.data);
    moonbit_decref_cycle_free(_M0L1sS446);
    return _result_2048;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_2049
  = moonbit_add_string(_M0L1sS446, (moonbit_string_t)moonbit_string_literal_14.data);
  moonbit_decref_cycle_free(_M0L1sS446);
  return _result_2049;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS444) {
  int32_t _M0L6_2atmpS1313;
  uint32_t _M0L6_2atmpS1312;
  uint32_t _M0L6_2atmpS1311;
  int32_t _M0L6_2atmpS1310;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS1313 = _M0L1eS444 * 1217359;
  _M0L6_2atmpS1312 = *(uint32_t*)&_M0L6_2atmpS1313;
  _M0L6_2atmpS1311 = _M0L6_2atmpS1312 >> 19;
  _M0L6_2atmpS1310 = *(int32_t*)&_M0L6_2atmpS1311;
  return _M0L6_2atmpS1310 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS443) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS443 != _M0L4selfS443) {
    return 0;
  } else if (_M0L4selfS443 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS443 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS443;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS442) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS442 != _M0L4selfS442) {
    return 0ll;
  } else if (_M0L4selfS442 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS442 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS442;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS441
) {
  float* _M0L6_2atmpS1309;
  struct _M0TPB5ArrayGfE* _block_2050;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1309 = (float*)moonbit_make_float_array_raw(_M0L3lenS441);
  _block_2050
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_2050)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 36, 0);
  _block_2050->$0 = _M0L6_2atmpS1309;
  _block_2050->$1 = _M0L3lenS441;
  return _block_2050;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS437,
  int32_t _M0L5indexS438
) {
  uint64_t* _M0L6_2atmpS1307;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1307 = _M0L4selfS437;
  if (
    _M0L5indexS438 < 0
    || _M0L5indexS438 >= Moonbit_array_length(_M0L6_2atmpS1307)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS1307[_M0L5indexS438];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS439,
  int32_t _M0L5indexS440
) {
  uint32_t* _M0L6_2atmpS1308;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS1308 = _M0L4selfS439;
  if (
    _M0L5indexS440 < 0
    || _M0L5indexS440 >= Moonbit_array_length(_M0L6_2atmpS1308)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS1308[_M0L5indexS440];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS436
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS436, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS435) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS435, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS434) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS434;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS428,
  moonbit_string_t _M0L5valueS430
) {
  int32_t _M0L3lenS1293;
  moonbit_string_t* _M0L6_2atmpS1295;
  int32_t _M0L6_2atmpS1294;
  int32_t _M0L6lengthS429;
  moonbit_string_t* _M0L3bufS1298;
  moonbit_string_t _M0L6_2aoldS1968;
  int32_t _M0L6_2atmpS1299;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1293 = _M0L4selfS428->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1295 = _M0MPC15array5Array6bufferGsE(_M0L4selfS428);
  _M0L6_2atmpS1294 = Moonbit_array_length(_M0L6_2atmpS1295);
  moonbit_decref_cycle_free(_M0L6_2atmpS1295);
  if (_M0L3lenS1293 == _M0L6_2atmpS1294) {
    int32_t _M0L3lenS1297 = _M0L4selfS428->$1;
    int32_t _M0L6_2atmpS1296 = _M0L3lenS1297 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS428, _M0L6_2atmpS1296);
  }
  _M0L6lengthS429 = _M0L4selfS428->$1;
  _M0L3bufS1298 = _M0L4selfS428->$0;
  _M0L6_2aoldS1968 = (moonbit_string_t)_M0L3bufS1298[_M0L6lengthS429];
  moonbit_decref_cycle_free(_M0L6_2aoldS1968);
  _M0L3bufS1298[_M0L6lengthS429] = _M0L5valueS430;
  _M0L6_2atmpS1299 = _M0L6lengthS429 + 1;
  _M0L4selfS428->$1 = _M0L6_2atmpS1299;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS431,
  struct _M0TUsiE* _M0L5valueS433
) {
  int32_t _M0L3lenS1300;
  struct _M0TUsiE** _M0L6_2atmpS1302;
  int32_t _M0L6_2atmpS1301;
  int32_t _M0L6lengthS432;
  struct _M0TUsiE** _M0L3bufS1305;
  struct _M0TUsiE* _M0L6_2aoldS1969;
  int32_t _M0L6_2atmpS1306;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS1300 = _M0L4selfS431->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS1302 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS431);
  _M0L6_2atmpS1301 = Moonbit_array_length(_M0L6_2atmpS1302);
  moonbit_decref_cycle_free(_M0L6_2atmpS1302);
  if (_M0L3lenS1300 == _M0L6_2atmpS1301) {
    int32_t _M0L3lenS1304 = _M0L4selfS431->$1;
    int32_t _M0L6_2atmpS1303 = _M0L3lenS1304 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS431, _M0L6_2atmpS1303);
  }
  _M0L6lengthS432 = _M0L4selfS431->$1;
  _M0L3bufS1305 = _M0L4selfS431->$0;
  _M0L6_2aoldS1969 = (struct _M0TUsiE*)_M0L3bufS1305[_M0L6lengthS432];
  if (_M0L6_2aoldS1969) {
    moonbit_decref_cycle_free(_M0L6_2aoldS1969);
  }
  _M0L3bufS1305[_M0L6lengthS432] = _M0L5valueS433;
  _M0L6_2atmpS1306 = _M0L6lengthS432 + 1;
  _M0L4selfS431->$1 = _M0L6_2atmpS1306;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS421,
  int32_t _M0L8requiredS423
) {
  int32_t _M0L8old__capS420;
  int32_t _M0L3lenS1291;
  int32_t _M0L8new__capS422;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS420 = _M0MPC15array5Array8capacityGsE(_M0L4selfS421);
  _M0L3lenS1291 = _M0L4selfS421->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS422
  = _M0FPB23array__growth__capacity(_M0L8old__capS420, _M0L3lenS1291, _M0L8requiredS423);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS421, _M0L8new__capS422);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS425,
  int32_t _M0L8requiredS427
) {
  int32_t _M0L8old__capS424;
  int32_t _M0L3lenS1292;
  int32_t _M0L8new__capS426;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS424 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS425);
  _M0L3lenS1292 = _M0L4selfS425->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS426
  = _M0FPB23array__growth__capacity(_M0L8old__capS424, _M0L3lenS1292, _M0L8requiredS427);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS425, _M0L8new__capS426);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS409,
  int32_t _M0L13new__capacityS412
) {
  moonbit_string_t* _M0L8old__bufS408;
  int32_t _M0L3lenS410;
  int32_t _M0L9copy__lenS411;
  moonbit_string_t* _M0L8new__bufS413;
  moonbit_string_t* _M0L6_2aoldS1970;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS408 = _M0L4selfS409->$0;
  _M0L3lenS410 = _M0L4selfS409->$1;
  if (_M0L3lenS410 < _M0L13new__capacityS412) {
    _M0L9copy__lenS411 = _M0L3lenS410;
  } else {
    _M0L9copy__lenS411 = _M0L13new__capacityS412;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS408);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS413
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS408, _M0L13new__capacityS412, _M0L9copy__lenS411, 0, 0);
  _M0L6_2aoldS1970 = _M0L4selfS409->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1970);
  _M0L4selfS409->$0 = _M0L8new__bufS413;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS415,
  int32_t _M0L13new__capacityS418
) {
  struct _M0TUsiE** _M0L8old__bufS414;
  int32_t _M0L3lenS416;
  int32_t _M0L9copy__lenS417;
  struct _M0TUsiE** _M0L8new__bufS419;
  struct _M0TUsiE** _M0L6_2aoldS1971;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS414 = _M0L4selfS415->$0;
  _M0L3lenS416 = _M0L4selfS415->$1;
  if (_M0L3lenS416 < _M0L13new__capacityS418) {
    _M0L9copy__lenS417 = _M0L3lenS416;
  } else {
    _M0L9copy__lenS417 = _M0L13new__capacityS418;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS414);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS419
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS414, _M0L13new__capacityS418, _M0L9copy__lenS417, 0, 0);
  _M0L6_2aoldS1971 = _M0L4selfS415->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1971);
  _M0L4selfS415->$0 = _M0L8new__bufS419;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS406
) {
  moonbit_string_t* _M0L6_2atmpS1289;
  int32_t _result_2051;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1289 = _M0MPC15array5Array6bufferGsE(_M0L4selfS406);
  _result_2051 = Moonbit_array_length(_M0L6_2atmpS1289);
  moonbit_decref_cycle_free(_M0L6_2atmpS1289);
  return _result_2051;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS407
) {
  struct _M0TUsiE** _M0L6_2atmpS1290;
  int32_t _result_2052;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS1290 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS407);
  _result_2052 = Moonbit_array_length(_M0L6_2atmpS1290);
  moonbit_decref_cycle_free(_M0L6_2atmpS1290);
  return _result_2052;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS402,
  int32_t _M0L3lenS400,
  int32_t _M0L8requiredS399
) {
  int32_t _M0L5startS401;
  int32_t _M0L5spaceS403;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS399 < _M0L3lenS400) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_15.data);
  }
  if (_M0L7currentS402 == 0) {
    _M0L5startS401 = 8;
  } else {
    _M0L5startS401 = _M0L7currentS402;
  }
  _M0L5spaceS403 = _M0L5startS401;
  while (1) {
    if (_M0L5spaceS403 < _M0L8requiredS399) {
      int32_t _M0L4nextS404 = _M0L5spaceS403 * 2;
      if (_M0L4nextS404 <= _M0L5spaceS403) {
        return _M0L8requiredS399;
      }
      _M0L5spaceS403 = _M0L4nextS404;
      continue;
    } else {
      return _M0L5spaceS403;
    }
    break;
  }
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS396) {
  float* _M0L8_2afieldS1972;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1972 = _M0L4selfS396->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1972);
  return _M0L8_2afieldS1972;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS397
) {
  moonbit_string_t* _M0L8_2afieldS1973;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1973 = _M0L4selfS397->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1973);
  return _M0L8_2afieldS1973;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS398
) {
  struct _M0TUsiE** _M0L8_2afieldS1974;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS1974 = _M0L4selfS398->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1974);
  return _M0L8_2afieldS1974;
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
  int32_t _M0L3endS1287;
  int32_t _M0L5startS1288;
  int32_t _M0L8str__lenS391;
  int32_t _M0L3lenS1286;
  int32_t _M0L8requiredS393;
  uint16_t* _M0L4dataS1279;
  int32_t _M0L6_2atmpS1278;
  int32_t _if__result_2054;
  uint16_t* _M0L4dataS1280;
  int32_t _M0L3lenS1281;
  moonbit_string_t _M0L6_2atmpS1282;
  int32_t _M0L6_2atmpS1283;
  int32_t _M0L3lenS1285;
  int32_t _M0L6_2atmpS1284;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS1287 = _M0L3strS392.$2;
  _M0L5startS1288 = _M0L3strS392.$1;
  _M0L8str__lenS391 = _M0L3endS1287 - _M0L5startS1288;
  if (_M0L8str__lenS391 == 0) {
    return 0;
  }
  _M0L3lenS1286 = _M0L4selfS394->$1;
  _M0L8requiredS393 = _M0L3lenS1286 + _M0L8str__lenS391;
  _M0L4dataS1279 = _M0L4selfS394->$0;
  _M0L6_2atmpS1278 = Moonbit_array_length(_M0L4dataS1279);
  if (_M0L8requiredS393 > _M0L6_2atmpS1278) {
    _if__result_2054 = 1;
  } else {
    int32_t _M0L3lenS1277 = _M0L4selfS394->$1;
    _if__result_2054 = _M0L8requiredS393 < _M0L3lenS1277;
  }
  if (_if__result_2054) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS394, _M0L8requiredS393);
  }
  _M0L4dataS1280 = _M0L4selfS394->$0;
  _M0L3lenS1281 = _M0L4selfS394->$1;
  moonbit_incref_cycle_free(_M0L4dataS1280);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1282 = _M0MPC16string10StringView4data(_M0L3strS392);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS1283 = _M0MPC16string10StringView13start__offset(_M0L3strS392);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS1280, _M0L3lenS1281, _M0L6_2atmpS1282, _M0L6_2atmpS1283, _M0L8str__lenS391);
  moonbit_decref_cycle_free(_M0L4dataS1280);
  moonbit_decref_cycle_free(_M0L6_2atmpS1282);
  _M0L3lenS1285 = _M0L4selfS394->$1;
  _M0L6_2atmpS1284 = _M0L3lenS1285 + _M0L8str__lenS391;
  _M0L4selfS394->$1 = _M0L6_2atmpS1284;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS388,
  int32_t _M0L5startS386,
  int32_t _M0L3endS387
) {
  int32_t _if__result_2055;
  int32_t _M0L3lenS389;
  int32_t _M0L6_2atmpS1276;
  moonbit_bytes_t _M0L5bytesS390;
  moonbit_bytes_t _M0L6_2atmpS1275;
  moonbit_string_t _result_2056;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS386 == 0) {
    int32_t _M0L6_2atmpS1274 = Moonbit_array_length(_M0L3strS388);
    _if__result_2055 = _M0L3endS387 == _M0L6_2atmpS1274;
  } else {
    _if__result_2055 = 0;
  }
  if (_if__result_2055) {
    moonbit_incref_cycle_free(_M0L3strS388);
    return _M0L3strS388;
  }
  _M0L3lenS389 = _M0L3endS387 - _M0L5startS386;
  _M0L6_2atmpS1276 = _M0L3lenS389 * 2;
  _M0L5bytesS390 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS1276, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS390, 0, _M0L3strS388, _M0L5startS386, _M0L3lenS389);
  _M0L6_2atmpS1275 = _M0L5bytesS390;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_2056
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS1275, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS1275);
  return _result_2056;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS381,
  int32_t _M0L6offsetS385,
  int64_t _M0L6lengthS383
) {
  int32_t _M0L3lenS380;
  int32_t _M0L6lengthS382;
  int32_t _if__result_2057;
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
      int32_t _M0L6_2atmpS1273 = _M0L6offsetS385 + _M0L6lengthS382;
      _if__result_2057 = _M0L6_2atmpS1273 <= _M0L3lenS380;
    } else {
      _if__result_2057 = 0;
    }
  } else {
    _if__result_2057 = 0;
  }
  if (_if__result_2057) {
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
  int32_t _M0L6_2atmpS1272;
  int32_t _M0L6_2atmpS1271;
  int32_t _M0L2e1S366;
  int32_t _M0L6_2atmpS1270;
  int32_t _M0L2e2S369;
  int32_t _M0L4len1S371;
  int32_t _M0L4len2S373;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS1272 = _M0L6lengthS368 * 2;
  _M0L6_2atmpS1271 = _M0L13bytes__offsetS367 + _M0L6_2atmpS1272;
  _M0L2e1S366 = _M0L6_2atmpS1271 - 1;
  _M0L6_2atmpS1270 = _M0L11str__offsetS370 + _M0L6lengthS368;
  _M0L2e2S369 = _M0L6_2atmpS1270 - 1;
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
        int32_t _M0L6_2atmpS1267 = _M0L3strS374[_M0L1iS376];
        int32_t _M0L6_2atmpS1266 = (int32_t)_M0L6_2atmpS1267;
        uint32_t _M0L1cS378 = *(uint32_t*)&_M0L6_2atmpS1266;
        uint32_t _M0L6_2atmpS1262 = _M0L1cS378 & 255u;
        int32_t _M0L6_2atmpS1261;
        int32_t _M0L6_2atmpS1263;
        uint32_t _M0L6_2atmpS1265;
        int32_t _M0L6_2atmpS1264;
        int32_t _M0L6_2atmpS1268;
        int32_t _M0L6_2atmpS1269;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1261 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1262);
        if (
          _M0L1jS377 < 0 || _M0L1jS377 >= Moonbit_array_length(_M0L4selfS372)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS372[_M0L1jS377] = _M0L6_2atmpS1261;
        _M0L6_2atmpS1263 = _M0L1jS377 + 1;
        _M0L6_2atmpS1265 = _M0L1cS378 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS1264 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS1265);
        if (
          _M0L6_2atmpS1263 < 0
          || _M0L6_2atmpS1263 >= Moonbit_array_length(_M0L4selfS372)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS372[_M0L6_2atmpS1263] = _M0L6_2atmpS1264;
        _M0L6_2atmpS1268 = _M0L1iS376 + 1;
        _M0L6_2atmpS1269 = _M0L1jS377 + 2;
        _M0L1iS376 = _M0L6_2atmpS1268;
        _M0L1jS377 = _M0L6_2atmpS1269;
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
  int32_t _M0L6_2atmpS1260;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1260 = *(int32_t*)&_M0L4selfS365;
  return _M0L6_2atmpS1260 & 0xff;
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
    int64_t _M0L6_2atmpS1259 = -_M0L4selfS340;
    _M0L3numS342 = *(uint64_t*)&_M0L6_2atmpS1259;
  } else {
    _M0L3numS342 = *(uint64_t*)&_M0L4selfS340;
  }
  switch (_M0L5radixS339) {
    case 10: {
      int32_t _M0L10digit__lenS344;
      int32_t _M0L6_2atmpS1256;
      int32_t _M0L10total__lenS345;
      uint16_t* _M0L6bufferS346;
      int32_t _M0L12digit__startS347;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS344 = _M0FPB12dec__count64(_M0L3numS342);
      if (_M0L12is__negativeS341) {
        _M0L6_2atmpS1256 = 1;
      } else {
        _M0L6_2atmpS1256 = 0;
      }
      _M0L10total__lenS345 = _M0L10digit__lenS344 + _M0L6_2atmpS1256;
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
      int32_t _M0L6_2atmpS1257;
      int32_t _M0L10total__lenS349;
      uint16_t* _M0L6bufferS350;
      int32_t _M0L12digit__startS351;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS348 = _M0FPB12hex__count64(_M0L3numS342);
      if (_M0L12is__negativeS341) {
        _M0L6_2atmpS1257 = 1;
      } else {
        _M0L6_2atmpS1257 = 0;
      }
      _M0L10total__lenS349 = _M0L10digit__lenS348 + _M0L6_2atmpS1257;
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
      int32_t _M0L6_2atmpS1258;
      int32_t _M0L10total__lenS353;
      uint16_t* _M0L6bufferS354;
      int32_t _M0L12digit__startS355;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS352
      = _M0FPB14radix__count64(_M0L3numS342, _M0L5radixS339);
      if (_M0L12is__negativeS341) {
        _M0L6_2atmpS1258 = 1;
      } else {
        _M0L6_2atmpS1258 = 0;
      }
      _M0L10total__lenS353 = _M0L10digit__lenS352 + _M0L6_2atmpS1258;
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
  int32_t _M0L6_2atmpS1255;
  uint64_t _M0L3numS315;
  int32_t _M0L6offsetS316;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1255 = _M0L10total__lenS338 - _M0L12digit__startS326;
  _M0L3numS315 = _M0L3numS337;
  _M0L6offsetS316 = _M0L6_2atmpS1255;
  while (1) {
    if (_M0L3numS315 >= 10000ull) {
      uint64_t _M0L1tS317 = _M0L3numS315 / 10000ull;
      uint64_t _M0L6_2atmpS1232 = _M0L3numS315 % 10000ull;
      int32_t _M0L1rS318 = (int32_t)_M0L6_2atmpS1232;
      int32_t _M0L2d1S319 = _M0L1rS318 / 100;
      int32_t _M0L2d2S320 = _M0L1rS318 % 100;
      int32_t _M0L6_2atmpS1231 = _M0L2d1S319 / 10;
      int32_t _M0L6_2atmpS1230 = 48 + _M0L6_2atmpS1231;
      int32_t _M0L6d1__hiS321 = (uint16_t)_M0L6_2atmpS1230;
      int32_t _M0L6_2atmpS1229 = _M0L2d1S319 % 10;
      int32_t _M0L6_2atmpS1228 = 48 + _M0L6_2atmpS1229;
      int32_t _M0L6d1__loS322 = (uint16_t)_M0L6_2atmpS1228;
      int32_t _M0L6_2atmpS1227 = _M0L2d2S320 / 10;
      int32_t _M0L6_2atmpS1226 = 48 + _M0L6_2atmpS1227;
      int32_t _M0L6d2__hiS323 = (uint16_t)_M0L6_2atmpS1226;
      int32_t _M0L6_2atmpS1225 = _M0L2d2S320 % 10;
      int32_t _M0L6_2atmpS1224 = 48 + _M0L6_2atmpS1225;
      int32_t _M0L6d2__loS324 = (uint16_t)_M0L6_2atmpS1224;
      int32_t _M0L6_2atmpS1216 = _M0L12digit__startS326 + _M0L6offsetS316;
      int32_t _M0L6_2atmpS1215 = _M0L6_2atmpS1216 - 4;
      int32_t _M0L6_2atmpS1218;
      int32_t _M0L6_2atmpS1217;
      int32_t _M0L6_2atmpS1220;
      int32_t _M0L6_2atmpS1219;
      int32_t _M0L6_2atmpS1222;
      int32_t _M0L6_2atmpS1221;
      int32_t _M0L6_2atmpS1223;
      _M0L6bufferS325[_M0L6_2atmpS1215] = _M0L6d1__hiS321;
      _M0L6_2atmpS1218 = _M0L12digit__startS326 + _M0L6offsetS316;
      _M0L6_2atmpS1217 = _M0L6_2atmpS1218 - 3;
      _M0L6bufferS325[_M0L6_2atmpS1217] = _M0L6d1__loS322;
      _M0L6_2atmpS1220 = _M0L12digit__startS326 + _M0L6offsetS316;
      _M0L6_2atmpS1219 = _M0L6_2atmpS1220 - 2;
      _M0L6bufferS325[_M0L6_2atmpS1219] = _M0L6d2__hiS323;
      _M0L6_2atmpS1222 = _M0L12digit__startS326 + _M0L6offsetS316;
      _M0L6_2atmpS1221 = _M0L6_2atmpS1222 - 1;
      _M0L6bufferS325[_M0L6_2atmpS1221] = _M0L6d2__loS324;
      _M0L6_2atmpS1223 = _M0L6offsetS316 - 4;
      _M0L3numS315 = _M0L1tS317;
      _M0L6offsetS316 = _M0L6_2atmpS1223;
      continue;
    } else {
      int32_t _M0L6_2atmpS1254 = (int32_t)_M0L3numS315;
      int32_t _M0L9remainingS328 = _M0L6_2atmpS1254;
      int32_t _M0L6offsetS329 = _M0L6offsetS316;
      while (1) {
        if (_M0L9remainingS328 >= 100) {
          int32_t _M0L1tS330 = _M0L9remainingS328 / 100;
          int32_t _M0L1dS331 = _M0L9remainingS328 % 100;
          int32_t _M0L6_2atmpS1241 = _M0L1dS331 / 10;
          int32_t _M0L6_2atmpS1240 = 48 + _M0L6_2atmpS1241;
          int32_t _M0L5d__hiS332 = (uint16_t)_M0L6_2atmpS1240;
          int32_t _M0L6_2atmpS1239 = _M0L1dS331 % 10;
          int32_t _M0L6_2atmpS1238 = 48 + _M0L6_2atmpS1239;
          int32_t _M0L5d__loS333 = (uint16_t)_M0L6_2atmpS1238;
          int32_t _M0L6_2atmpS1234 = _M0L12digit__startS326 + _M0L6offsetS329;
          int32_t _M0L6_2atmpS1233 = _M0L6_2atmpS1234 - 2;
          int32_t _M0L6_2atmpS1236;
          int32_t _M0L6_2atmpS1235;
          int32_t _M0L6_2atmpS1237;
          _M0L6bufferS325[_M0L6_2atmpS1233] = _M0L5d__hiS332;
          _M0L6_2atmpS1236 = _M0L12digit__startS326 + _M0L6offsetS329;
          _M0L6_2atmpS1235 = _M0L6_2atmpS1236 - 1;
          _M0L6bufferS325[_M0L6_2atmpS1235] = _M0L5d__loS333;
          _M0L6_2atmpS1237 = _M0L6offsetS329 - 2;
          _M0L9remainingS328 = _M0L1tS330;
          _M0L6offsetS329 = _M0L6_2atmpS1237;
          continue;
        } else if (_M0L9remainingS328 >= 10) {
          int32_t _M0L6_2atmpS1249 = _M0L9remainingS328 / 10;
          int32_t _M0L6_2atmpS1248 = 48 + _M0L6_2atmpS1249;
          int32_t _M0L5d__hiS335 = (uint16_t)_M0L6_2atmpS1248;
          int32_t _M0L6_2atmpS1247 = _M0L9remainingS328 % 10;
          int32_t _M0L6_2atmpS1246 = 48 + _M0L6_2atmpS1247;
          int32_t _M0L5d__loS336 = (uint16_t)_M0L6_2atmpS1246;
          int32_t _M0L6_2atmpS1243 = _M0L12digit__startS326 + _M0L6offsetS329;
          int32_t _M0L6_2atmpS1242 = _M0L6_2atmpS1243 - 2;
          int32_t _M0L6_2atmpS1245;
          int32_t _M0L6_2atmpS1244;
          _M0L6bufferS325[_M0L6_2atmpS1242] = _M0L5d__hiS335;
          _M0L6_2atmpS1245 = _M0L12digit__startS326 + _M0L6offsetS329;
          _M0L6_2atmpS1244 = _M0L6_2atmpS1245 - 1;
          _M0L6bufferS325[_M0L6_2atmpS1244] = _M0L5d__loS336;
        } else {
          int32_t _M0L6_2atmpS1253 = _M0L12digit__startS326 + _M0L6offsetS329;
          int32_t _M0L6_2atmpS1250 = _M0L6_2atmpS1253 - 1;
          int32_t _M0L6_2atmpS1252 = 48 + _M0L9remainingS328;
          int32_t _M0L6_2atmpS1251 = (uint16_t)_M0L6_2atmpS1252;
          _M0L6bufferS325[_M0L6_2atmpS1250] = _M0L6_2atmpS1251;
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
  int32_t _M0L6_2atmpS1200;
  int32_t _M0L6_2atmpS1199;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS298 = _M0MPC13int3Int10to__uint64(_M0L5radixS299);
  _M0L6_2atmpS1200 = _M0L5radixS299 - 1;
  _M0L6_2atmpS1199 = _M0L5radixS299 & _M0L6_2atmpS1200;
  if (_M0L6_2atmpS1199 == 0) {
    int32_t _M0L5shiftS300;
    uint64_t _M0L4maskS301;
    int32_t _M0L6_2atmpS1207;
    int32_t _M0L6offsetS302;
    uint64_t _M0L1nS303;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS300 = moonbit_ctz32(_M0L5radixS299);
    _M0L4maskS301 = _M0L4baseS298 - 1ull;
    _M0L6_2atmpS1207 = _M0L10total__lenS308 - _M0L12digit__startS306;
    _M0L6offsetS302 = _M0L6_2atmpS1207;
    _M0L1nS303 = _M0L3numS309;
    while (1) {
      if (_M0L1nS303 > 0ull) {
        uint64_t _M0L6_2atmpS1206 = _M0L1nS303 & _M0L4maskS301;
        int32_t _M0L5digitS304 = (int32_t)_M0L6_2atmpS1206;
        int32_t _M0L6_2atmpS1203 = _M0L12digit__startS306 + _M0L6offsetS302;
        int32_t _M0L6_2atmpS1201 = _M0L6_2atmpS1203 - 1;
        int32_t _M0L6_2atmpS1202 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS304];
        int32_t _M0L6_2atmpS1204;
        uint64_t _M0L6_2atmpS1205;
        _M0L6bufferS305[_M0L6_2atmpS1201] = _M0L6_2atmpS1202;
        _M0L6_2atmpS1204 = _M0L6offsetS302 - 1;
        _M0L6_2atmpS1205 = _M0L1nS303 >> (_M0L5shiftS300 & 63);
        _M0L6offsetS302 = _M0L6_2atmpS1204;
        _M0L1nS303 = _M0L6_2atmpS1205;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1214 = _M0L10total__lenS308 - _M0L12digit__startS306;
    int32_t _M0L6offsetS310 = _M0L6_2atmpS1214;
    uint64_t _M0L1nS311 = _M0L3numS309;
    while (1) {
      if (_M0L1nS311 > 0ull) {
        uint64_t _M0L1qS312 = _M0L1nS311 / _M0L4baseS298;
        uint64_t _M0L6_2atmpS1213 = _M0L1qS312 * _M0L4baseS298;
        uint64_t _M0L6_2atmpS1212 = _M0L1nS311 - _M0L6_2atmpS1213;
        int32_t _M0L5digitS313 = (int32_t)_M0L6_2atmpS1212;
        int32_t _M0L6_2atmpS1210 = _M0L12digit__startS306 + _M0L6offsetS310;
        int32_t _M0L6_2atmpS1208 = _M0L6_2atmpS1210 - 1;
        int32_t _M0L6_2atmpS1209 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS313];
        int32_t _M0L6_2atmpS1211;
        _M0L6bufferS305[_M0L6_2atmpS1208] = _M0L6_2atmpS1209;
        _M0L6_2atmpS1211 = _M0L6offsetS310 - 1;
        _M0L6offsetS310 = _M0L6_2atmpS1211;
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
  int32_t _M0L6_2atmpS1198;
  int32_t _M0L6offsetS287;
  uint64_t _M0L1nS288;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1198 = _M0L10total__lenS296 - _M0L12digit__startS293;
  _M0L6offsetS287 = _M0L6_2atmpS1198;
  _M0L1nS288 = _M0L3numS297;
  while (1) {
    if (_M0L6offsetS287 >= 2) {
      uint64_t _M0L6_2atmpS1195 = _M0L1nS288 & 255ull;
      int32_t _M0L9byte__valS289 = (int32_t)_M0L6_2atmpS1195;
      int32_t _M0L2hiS290 = _M0L9byte__valS289 / 16;
      int32_t _M0L2loS291 = _M0L9byte__valS289 % 16;
      int32_t _M0L6_2atmpS1189 = _M0L12digit__startS293 + _M0L6offsetS287;
      int32_t _M0L6_2atmpS1187 = _M0L6_2atmpS1189 - 2;
      int32_t _M0L6_2atmpS1188 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L2hiS290];
      int32_t _M0L6_2atmpS1192;
      int32_t _M0L6_2atmpS1190;
      int32_t _M0L6_2atmpS1191;
      int32_t _M0L6_2atmpS1193;
      uint64_t _M0L6_2atmpS1194;
      _M0L6bufferS292[_M0L6_2atmpS1187] = _M0L6_2atmpS1188;
      _M0L6_2atmpS1192 = _M0L12digit__startS293 + _M0L6offsetS287;
      _M0L6_2atmpS1190 = _M0L6_2atmpS1192 - 1;
      _M0L6_2atmpS1191
      = ((moonbit_string_t)moonbit_string_literal_17.data)[
        _M0L2loS291
      ];
      _M0L6bufferS292[_M0L6_2atmpS1190] = _M0L6_2atmpS1191;
      _M0L6_2atmpS1193 = _M0L6offsetS287 - 2;
      _M0L6_2atmpS1194 = _M0L1nS288 >> 8;
      _M0L6offsetS287 = _M0L6_2atmpS1193;
      _M0L1nS288 = _M0L6_2atmpS1194;
      continue;
    } else if (_M0L6offsetS287 == 1) {
      uint64_t _M0L6_2atmpS1197 = _M0L1nS288 & 15ull;
      int32_t _M0L6nibbleS295 = (int32_t)_M0L6_2atmpS1197;
      int32_t _M0L6_2atmpS1196 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L6nibbleS295];
      _M0L6bufferS292[_M0L12digit__startS293] = _M0L6_2atmpS1196;
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
      uint64_t _M0L6_2atmpS1185 = _M0L3numS284 / _M0L4baseS282;
      int32_t _M0L6_2atmpS1186 = _M0L5countS285 + 1;
      _M0L3numS284 = _M0L6_2atmpS1185;
      _M0L5countS285 = _M0L6_2atmpS1186;
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
    int32_t _M0L6_2atmpS1184;
    int32_t _M0L6_2atmpS1183;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS280 = moonbit_clz64(_M0L5valueS279);
    _M0L6_2atmpS1184 = 63 - _M0L14leading__zerosS280;
    _M0L6_2atmpS1183 = _M0L6_2atmpS1184 / 4;
    return _M0L6_2atmpS1183 + 1;
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
    int32_t _M0L6_2atmpS1182 = -_M0L4selfS262;
    _M0L3numS264 = *(uint32_t*)&_M0L6_2atmpS1182;
  } else {
    _M0L3numS264 = *(uint32_t*)&_M0L4selfS262;
  }
  switch (_M0L5radixS261) {
    case 10: {
      int32_t _M0L10digit__lenS266;
      int32_t _M0L6_2atmpS1179;
      int32_t _M0L10total__lenS267;
      uint16_t* _M0L6bufferS268;
      int32_t _M0L12digit__startS269;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS266 = _M0FPB12dec__count32(_M0L3numS264);
      if (_M0L12is__negativeS263) {
        _M0L6_2atmpS1179 = 1;
      } else {
        _M0L6_2atmpS1179 = 0;
      }
      _M0L10total__lenS267 = _M0L10digit__lenS266 + _M0L6_2atmpS1179;
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
      int32_t _M0L6_2atmpS1180;
      int32_t _M0L10total__lenS271;
      uint16_t* _M0L6bufferS272;
      int32_t _M0L12digit__startS273;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS270 = _M0FPB12hex__count32(_M0L3numS264);
      if (_M0L12is__negativeS263) {
        _M0L6_2atmpS1180 = 1;
      } else {
        _M0L6_2atmpS1180 = 0;
      }
      _M0L10total__lenS271 = _M0L10digit__lenS270 + _M0L6_2atmpS1180;
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
      int32_t _M0L6_2atmpS1181;
      int32_t _M0L10total__lenS275;
      uint16_t* _M0L6bufferS276;
      int32_t _M0L12digit__startS277;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS274
      = _M0FPB14radix__count32(_M0L3numS264, _M0L5radixS261);
      if (_M0L12is__negativeS263) {
        _M0L6_2atmpS1181 = 1;
      } else {
        _M0L6_2atmpS1181 = 0;
      }
      _M0L10total__lenS275 = _M0L10digit__lenS274 + _M0L6_2atmpS1181;
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
      uint32_t _M0L6_2atmpS1177 = _M0L3numS258 / _M0L4baseS256;
      int32_t _M0L6_2atmpS1178 = _M0L5countS259 + 1;
      _M0L3numS258 = _M0L6_2atmpS1177;
      _M0L5countS259 = _M0L6_2atmpS1178;
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
    int32_t _M0L6_2atmpS1176;
    int32_t _M0L6_2atmpS1175;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS254 = moonbit_clz32(_M0L5valueS253);
    _M0L6_2atmpS1176 = 31 - _M0L14leading__zerosS254;
    _M0L6_2atmpS1175 = _M0L6_2atmpS1176 / 4;
    return _M0L6_2atmpS1175 + 1;
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
  int32_t _M0L6_2atmpS1174;
  uint32_t _M0L3numS228;
  int32_t _M0L6offsetS229;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1174 = _M0L10total__lenS251 - _M0L12digit__startS239;
  _M0L3numS228 = _M0L3numS250;
  _M0L6offsetS229 = _M0L6_2atmpS1174;
  while (1) {
    if (_M0L3numS228 >= 10000u) {
      uint32_t _M0L1tS230 = _M0L3numS228 / 10000u;
      uint32_t _M0L6_2atmpS1151 = _M0L3numS228 % 10000u;
      int32_t _M0L1rS231 = *(int32_t*)&_M0L6_2atmpS1151;
      int32_t _M0L2d1S232 = _M0L1rS231 / 100;
      int32_t _M0L2d2S233 = _M0L1rS231 % 100;
      int32_t _M0L6_2atmpS1150 = _M0L2d1S232 / 10;
      int32_t _M0L6_2atmpS1149 = 48 + _M0L6_2atmpS1150;
      int32_t _M0L6d1__hiS234 = (uint16_t)_M0L6_2atmpS1149;
      int32_t _M0L6_2atmpS1148 = _M0L2d1S232 % 10;
      int32_t _M0L6_2atmpS1147 = 48 + _M0L6_2atmpS1148;
      int32_t _M0L6d1__loS235 = (uint16_t)_M0L6_2atmpS1147;
      int32_t _M0L6_2atmpS1146 = _M0L2d2S233 / 10;
      int32_t _M0L6_2atmpS1145 = 48 + _M0L6_2atmpS1146;
      int32_t _M0L6d2__hiS236 = (uint16_t)_M0L6_2atmpS1145;
      int32_t _M0L6_2atmpS1144 = _M0L2d2S233 % 10;
      int32_t _M0L6_2atmpS1143 = 48 + _M0L6_2atmpS1144;
      int32_t _M0L6d2__loS237 = (uint16_t)_M0L6_2atmpS1143;
      int32_t _M0L6_2atmpS1135 = _M0L12digit__startS239 + _M0L6offsetS229;
      int32_t _M0L6_2atmpS1134 = _M0L6_2atmpS1135 - 4;
      int32_t _M0L6_2atmpS1137;
      int32_t _M0L6_2atmpS1136;
      int32_t _M0L6_2atmpS1139;
      int32_t _M0L6_2atmpS1138;
      int32_t _M0L6_2atmpS1141;
      int32_t _M0L6_2atmpS1140;
      int32_t _M0L6_2atmpS1142;
      _M0L6bufferS238[_M0L6_2atmpS1134] = _M0L6d1__hiS234;
      _M0L6_2atmpS1137 = _M0L12digit__startS239 + _M0L6offsetS229;
      _M0L6_2atmpS1136 = _M0L6_2atmpS1137 - 3;
      _M0L6bufferS238[_M0L6_2atmpS1136] = _M0L6d1__loS235;
      _M0L6_2atmpS1139 = _M0L12digit__startS239 + _M0L6offsetS229;
      _M0L6_2atmpS1138 = _M0L6_2atmpS1139 - 2;
      _M0L6bufferS238[_M0L6_2atmpS1138] = _M0L6d2__hiS236;
      _M0L6_2atmpS1141 = _M0L12digit__startS239 + _M0L6offsetS229;
      _M0L6_2atmpS1140 = _M0L6_2atmpS1141 - 1;
      _M0L6bufferS238[_M0L6_2atmpS1140] = _M0L6d2__loS237;
      _M0L6_2atmpS1142 = _M0L6offsetS229 - 4;
      _M0L3numS228 = _M0L1tS230;
      _M0L6offsetS229 = _M0L6_2atmpS1142;
      continue;
    } else {
      int32_t _M0L6_2atmpS1173 = *(int32_t*)&_M0L3numS228;
      int32_t _M0L9remainingS241 = _M0L6_2atmpS1173;
      int32_t _M0L6offsetS242 = _M0L6offsetS229;
      while (1) {
        if (_M0L9remainingS241 >= 100) {
          int32_t _M0L1tS243 = _M0L9remainingS241 / 100;
          int32_t _M0L1dS244 = _M0L9remainingS241 % 100;
          int32_t _M0L6_2atmpS1160 = _M0L1dS244 / 10;
          int32_t _M0L6_2atmpS1159 = 48 + _M0L6_2atmpS1160;
          int32_t _M0L5d__hiS245 = (uint16_t)_M0L6_2atmpS1159;
          int32_t _M0L6_2atmpS1158 = _M0L1dS244 % 10;
          int32_t _M0L6_2atmpS1157 = 48 + _M0L6_2atmpS1158;
          int32_t _M0L5d__loS246 = (uint16_t)_M0L6_2atmpS1157;
          int32_t _M0L6_2atmpS1153 = _M0L12digit__startS239 + _M0L6offsetS242;
          int32_t _M0L6_2atmpS1152 = _M0L6_2atmpS1153 - 2;
          int32_t _M0L6_2atmpS1155;
          int32_t _M0L6_2atmpS1154;
          int32_t _M0L6_2atmpS1156;
          _M0L6bufferS238[_M0L6_2atmpS1152] = _M0L5d__hiS245;
          _M0L6_2atmpS1155 = _M0L12digit__startS239 + _M0L6offsetS242;
          _M0L6_2atmpS1154 = _M0L6_2atmpS1155 - 1;
          _M0L6bufferS238[_M0L6_2atmpS1154] = _M0L5d__loS246;
          _M0L6_2atmpS1156 = _M0L6offsetS242 - 2;
          _M0L9remainingS241 = _M0L1tS243;
          _M0L6offsetS242 = _M0L6_2atmpS1156;
          continue;
        } else if (_M0L9remainingS241 >= 10) {
          int32_t _M0L6_2atmpS1168 = _M0L9remainingS241 / 10;
          int32_t _M0L6_2atmpS1167 = 48 + _M0L6_2atmpS1168;
          int32_t _M0L5d__hiS248 = (uint16_t)_M0L6_2atmpS1167;
          int32_t _M0L6_2atmpS1166 = _M0L9remainingS241 % 10;
          int32_t _M0L6_2atmpS1165 = 48 + _M0L6_2atmpS1166;
          int32_t _M0L5d__loS249 = (uint16_t)_M0L6_2atmpS1165;
          int32_t _M0L6_2atmpS1162 = _M0L12digit__startS239 + _M0L6offsetS242;
          int32_t _M0L6_2atmpS1161 = _M0L6_2atmpS1162 - 2;
          int32_t _M0L6_2atmpS1164;
          int32_t _M0L6_2atmpS1163;
          _M0L6bufferS238[_M0L6_2atmpS1161] = _M0L5d__hiS248;
          _M0L6_2atmpS1164 = _M0L12digit__startS239 + _M0L6offsetS242;
          _M0L6_2atmpS1163 = _M0L6_2atmpS1164 - 1;
          _M0L6bufferS238[_M0L6_2atmpS1163] = _M0L5d__loS249;
        } else {
          int32_t _M0L6_2atmpS1172 = _M0L12digit__startS239 + _M0L6offsetS242;
          int32_t _M0L6_2atmpS1169 = _M0L6_2atmpS1172 - 1;
          int32_t _M0L6_2atmpS1171 = 48 + _M0L9remainingS241;
          int32_t _M0L6_2atmpS1170 = (uint16_t)_M0L6_2atmpS1171;
          _M0L6bufferS238[_M0L6_2atmpS1169] = _M0L6_2atmpS1170;
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
  int32_t _M0L6_2atmpS1119;
  int32_t _M0L6_2atmpS1118;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS211 = *(uint32_t*)&_M0L5radixS212;
  _M0L6_2atmpS1119 = _M0L5radixS212 - 1;
  _M0L6_2atmpS1118 = _M0L5radixS212 & _M0L6_2atmpS1119;
  if (_M0L6_2atmpS1118 == 0) {
    int32_t _M0L5shiftS213;
    uint32_t _M0L4maskS214;
    int32_t _M0L6_2atmpS1126;
    int32_t _M0L6offsetS215;
    uint32_t _M0L1nS216;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS213 = moonbit_ctz32(_M0L5radixS212);
    _M0L4maskS214 = _M0L4baseS211 - 1u;
    _M0L6_2atmpS1126 = _M0L10total__lenS221 - _M0L12digit__startS219;
    _M0L6offsetS215 = _M0L6_2atmpS1126;
    _M0L1nS216 = _M0L3numS222;
    while (1) {
      if (_M0L1nS216 > 0u) {
        uint32_t _M0L6_2atmpS1125 = _M0L1nS216 & _M0L4maskS214;
        int32_t _M0L5digitS217 = *(int32_t*)&_M0L6_2atmpS1125;
        int32_t _M0L6_2atmpS1122 = _M0L12digit__startS219 + _M0L6offsetS215;
        int32_t _M0L6_2atmpS1120 = _M0L6_2atmpS1122 - 1;
        int32_t _M0L6_2atmpS1121 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS217];
        int32_t _M0L6_2atmpS1123;
        uint32_t _M0L6_2atmpS1124;
        _M0L6bufferS218[_M0L6_2atmpS1120] = _M0L6_2atmpS1121;
        _M0L6_2atmpS1123 = _M0L6offsetS215 - 1;
        _M0L6_2atmpS1124 = _M0L1nS216 >> (_M0L5shiftS213 & 31);
        _M0L6offsetS215 = _M0L6_2atmpS1123;
        _M0L1nS216 = _M0L6_2atmpS1124;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS1133 = _M0L10total__lenS221 - _M0L12digit__startS219;
    int32_t _M0L6offsetS223 = _M0L6_2atmpS1133;
    uint32_t _M0L1nS224 = _M0L3numS222;
    while (1) {
      if (_M0L1nS224 > 0u) {
        uint32_t _M0L1qS225 = _M0L1nS224 / _M0L4baseS211;
        uint32_t _M0L6_2atmpS1132 = _M0L1qS225 * _M0L4baseS211;
        uint32_t _M0L6_2atmpS1131 = _M0L1nS224 - _M0L6_2atmpS1132;
        int32_t _M0L5digitS226 = *(int32_t*)&_M0L6_2atmpS1131;
        int32_t _M0L6_2atmpS1129 = _M0L12digit__startS219 + _M0L6offsetS223;
        int32_t _M0L6_2atmpS1127 = _M0L6_2atmpS1129 - 1;
        int32_t _M0L6_2atmpS1128 =
          ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L5digitS226];
        int32_t _M0L6_2atmpS1130;
        _M0L6bufferS218[_M0L6_2atmpS1127] = _M0L6_2atmpS1128;
        _M0L6_2atmpS1130 = _M0L6offsetS223 - 1;
        _M0L6offsetS223 = _M0L6_2atmpS1130;
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
  int32_t _M0L6_2atmpS1117;
  int32_t _M0L6offsetS200;
  uint32_t _M0L1nS201;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS1117 = _M0L10total__lenS209 - _M0L12digit__startS206;
  _M0L6offsetS200 = _M0L6_2atmpS1117;
  _M0L1nS201 = _M0L3numS210;
  while (1) {
    if (_M0L6offsetS200 >= 2) {
      uint32_t _M0L6_2atmpS1114 = _M0L1nS201 & 255u;
      int32_t _M0L9byte__valS202 = *(int32_t*)&_M0L6_2atmpS1114;
      int32_t _M0L2hiS203 = _M0L9byte__valS202 / 16;
      int32_t _M0L2loS204 = _M0L9byte__valS202 % 16;
      int32_t _M0L6_2atmpS1108 = _M0L12digit__startS206 + _M0L6offsetS200;
      int32_t _M0L6_2atmpS1106 = _M0L6_2atmpS1108 - 2;
      int32_t _M0L6_2atmpS1107 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L2hiS203];
      int32_t _M0L6_2atmpS1111;
      int32_t _M0L6_2atmpS1109;
      int32_t _M0L6_2atmpS1110;
      int32_t _M0L6_2atmpS1112;
      uint32_t _M0L6_2atmpS1113;
      _M0L6bufferS205[_M0L6_2atmpS1106] = _M0L6_2atmpS1107;
      _M0L6_2atmpS1111 = _M0L12digit__startS206 + _M0L6offsetS200;
      _M0L6_2atmpS1109 = _M0L6_2atmpS1111 - 1;
      _M0L6_2atmpS1110
      = ((moonbit_string_t)moonbit_string_literal_17.data)[
        _M0L2loS204
      ];
      _M0L6bufferS205[_M0L6_2atmpS1109] = _M0L6_2atmpS1110;
      _M0L6_2atmpS1112 = _M0L6offsetS200 - 2;
      _M0L6_2atmpS1113 = _M0L1nS201 >> 8;
      _M0L6offsetS200 = _M0L6_2atmpS1112;
      _M0L1nS201 = _M0L6_2atmpS1113;
      continue;
    } else if (_M0L6offsetS200 == 1) {
      uint32_t _M0L6_2atmpS1116 = _M0L1nS201 & 15u;
      int32_t _M0L6nibbleS208 = *(int32_t*)&_M0L6_2atmpS1116;
      int32_t _M0L6_2atmpS1115 =
        ((moonbit_string_t)moonbit_string_literal_17.data)[_M0L6nibbleS208];
      _M0L6bufferS205[_M0L12digit__startS206] = _M0L6_2atmpS1115;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS199
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS198;
  struct _M0TPB6Logger _M0L6_2atmpS1105;
  moonbit_string_t _result_2071;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS198);
  _M0L6_2atmpS1105
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS198
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS199, _M0L6_2atmpS1105);
  if (_M0L6_2atmpS1105.$1) {
    moonbit_decref(_M0L6_2atmpS1105.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_2071 = _M0MPB13StringBuilder10to__string(_M0L6loggerS198);
  moonbit_decref_cycle_free(_M0L6loggerS198);
  return _result_2071;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS193,
  struct _M0TPB6Logger _M0L6loggerS192
) {
  moonbit_string_t _M0L6_2atmpS1102;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1102 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS193);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS192.$0->$method_0(_M0L6loggerS192.$1, _M0L6_2atmpS1102);
  moonbit_decref_cycle_free(_M0L6_2atmpS1102);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS195,
  struct _M0TPB6Logger _M0L6loggerS194
) {
  moonbit_string_t _M0L6_2atmpS1103;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1103 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS195);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS194.$0->$method_0(_M0L6loggerS194.$1, _M0L6_2atmpS1103);
  moonbit_decref_cycle_free(_M0L6_2atmpS1103);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS197,
  struct _M0TPB6Logger _M0L6loggerS196
) {
  moonbit_string_t _M0L6_2atmpS1104;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1104 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS197);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS196.$0->$method_0(_M0L6loggerS196.$1, _M0L6_2atmpS1104);
  moonbit_decref_cycle_free(_M0L6_2atmpS1104);
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
  moonbit_string_t _M0L8_2afieldS1975;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS1975 = _M0L4selfS190.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS1975);
  return _M0L8_2afieldS1975;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS186,
  moonbit_string_t _M0L5valueS187,
  int32_t _M0L5startS188,
  int32_t _M0L3lenS189
) {
  int32_t _M0L6_2atmpS1101;
  int64_t _M0L6_2atmpS1100;
  struct _M0TPC16string10StringView _M0L6_2atmpS1099;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1101 = _M0L5startS188 + _M0L3lenS189;
  _M0L6_2atmpS1100 = (int64_t)_M0L6_2atmpS1101;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS1099
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS187, _M0L5startS188, _M0L6_2atmpS1100);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS186, _M0L6_2atmpS1099);
  moonbit_decref_cycle_free(_M0L6_2atmpS1099.$0);
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
  int32_t _M0L6_2atmpS1083;
  int32_t _if__result_2072;
  int32_t _M0L6_2atmpS1091;
  int32_t _if__result_2073;
  int32_t _M0L6_2atmpS1093;
  int32_t _M0L6_2atmpS1094;
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
  _M0L6_2atmpS1083 = _M0Lm2loS180;
  if (_M0L6_2atmpS1083 > 0) {
    int32_t _M0L6_2atmpS1082 = _M0Lm2loS180;
    if (_M0L6_2atmpS1082 < _M0L3lenS178) {
      int32_t _M0L6_2atmpS1081 = _M0Lm2loS180;
      int32_t _M0L6_2atmpS1080 = _M0L4selfS179[_M0L6_2atmpS1081];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1080)) {
        int32_t _M0L6_2atmpS1079 = _M0Lm2loS180;
        int32_t _M0L6_2atmpS1078 = _M0L6_2atmpS1079 - 1;
        int32_t _M0L6_2atmpS1077 = _M0L4selfS179[_M0L6_2atmpS1078];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2072
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1077);
      } else {
        _if__result_2072 = 0;
      }
    } else {
      _if__result_2072 = 0;
    }
  } else {
    _if__result_2072 = 0;
  }
  if (_if__result_2072) {
    int32_t _M0L6_2atmpS1084 = _M0Lm2loS180;
    _M0Lm2loS180 = _M0L6_2atmpS1084 + 1;
  }
  _M0L6_2atmpS1091 = _M0Lm2hiS182;
  if (_M0L6_2atmpS1091 > 0) {
    int32_t _M0L6_2atmpS1090 = _M0Lm2hiS182;
    if (_M0L6_2atmpS1090 < _M0L3lenS178) {
      int32_t _M0L6_2atmpS1089 = _M0Lm2hiS182;
      int32_t _M0L6_2atmpS1088 = _M0L4selfS179[_M0L6_2atmpS1089];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1088)) {
        int32_t _M0L6_2atmpS1087 = _M0Lm2hiS182;
        int32_t _M0L6_2atmpS1086 = _M0L6_2atmpS1087 - 1;
        int32_t _M0L6_2atmpS1085 = _M0L4selfS179[_M0L6_2atmpS1086];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2073
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1085);
      } else {
        _if__result_2073 = 0;
      }
    } else {
      _if__result_2073 = 0;
    }
  } else {
    _if__result_2073 = 0;
  }
  if (_if__result_2073) {
    int32_t _M0L6_2atmpS1092 = _M0Lm2hiS182;
    _M0Lm2hiS182 = _M0L6_2atmpS1092 - 1;
  }
  _M0L6_2atmpS1093 = _M0Lm2loS180;
  _M0L6_2atmpS1094 = _M0Lm2hiS182;
  if (_M0L6_2atmpS1093 >= _M0L6_2atmpS1094) {
    int32_t _M0L6_2atmpS1095 = _M0Lm2loS180;
    int32_t _M0L6_2atmpS1096 = _M0Lm2loS180;
    moonbit_incref_cycle_free(_M0L4selfS179);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS179,
                                                 .$1 = _M0L6_2atmpS1095,
                                                 .$2 = _M0L6_2atmpS1096};
  } else {
    int32_t _M0L6_2atmpS1097 = _M0Lm2loS180;
    int32_t _M0L6_2atmpS1098 = _M0Lm2hiS182;
    moonbit_incref_cycle_free(_M0L4selfS179);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS179,
                                                 .$1 = _M0L6_2atmpS1097,
                                                 .$2 = _M0L6_2atmpS1098};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS177,
  struct _M0TPB4Show _M0L4showS176
) {
  struct _M0TPB6Logger _M0L6_2atmpS1076;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS177);
  _M0L6_2atmpS1076
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS177
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS176.$0->$method_0(_M0L4showS176.$1, _M0L6_2atmpS1076);
  if (_M0L6_2atmpS1076.$1) {
    moonbit_decref(_M0L6_2atmpS1076.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS175,
  struct _M0TPB4Show _M0L4showS174
) {
  struct _M0TPB6Logger _M0L6_2atmpS1075;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS175);
  _M0L6_2atmpS1075
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS175
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS174.$0->$method_0(_M0L4showS174.$1, _M0L6_2atmpS1075);
  if (_M0L6_2atmpS1075.$1) {
    moonbit_decref(_M0L6_2atmpS1075.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS173) {
  int64_t _M0L6_2atmpS1074;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS1074 = (int64_t)_M0L4selfS173;
  return *(uint64_t*)&_M0L6_2atmpS1074;
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
  int32_t _M0L6_2atmpS1073;
  struct _M0TPC16string10StringView _M0L6_2atmpS1071;
  struct _M0TPB6Logger _M0L6_2atmpS1072;
  moonbit_string_t _result_2074;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS170 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS1073 = Moonbit_array_length(_M0L4selfS171);
  moonbit_incref_cycle_free(_M0L4selfS171);
  _M0L6_2atmpS1071
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS171, .$1 = 0, .$2 = _M0L6_2atmpS1073
  };
  moonbit_incref_cycle_free(_M0L3bufS170);
  _M0L6_2atmpS1072
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS170
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS1071, _M0L6_2atmpS1072, _M0L5quoteS172);
  moonbit_decref_cycle_free(_M0L6_2atmpS1071.$0);
  if (_M0L6_2atmpS1072.$1) {
    moonbit_decref(_M0L6_2atmpS1072.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2074 = _M0MPB13StringBuilder10to__string(_M0L3bufS170);
  moonbit_decref_cycle_free(_M0L3bufS170);
  return _result_2074;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS162,
  struct _M0TPB6Logger _M0L6loggerS160,
  int32_t _M0L5quoteS159
) {
  int32_t _M0L3endS1069;
  int32_t _M0L5startS1070;
  int32_t _M0L3lenS161;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS163;
  int32_t _M0L1iS164;
  int32_t _M0L3segS165;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS159) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS160.$0->$method_3(_M0L6loggerS160.$1, 34);
  }
  _M0L3endS1069 = _M0L4selfS162.$2;
  _M0L5startS1070 = _M0L4selfS162.$1;
  _M0L3lenS161 = _M0L3endS1069 - _M0L5startS1070;
  moonbit_incref_cycle_free(_M0L4selfS162.$0);
  if (_M0L6loggerS160.$1) {
    moonbit_incref(_M0L6loggerS160.$1);
  }
  _M0L6_2aenvS163
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS163)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 46, 0);
  _M0L6_2aenvS163->$0 = _M0L4selfS162;
  _M0L6_2aenvS163->$1 = _M0L6loggerS160;
  _M0L1iS164 = 0;
  _M0L3segS165 = 0;
  _2afor_166:;
  while (1) {
    moonbit_string_t _M0L3strS1066;
    int32_t _M0L5startS1068;
    int32_t _M0L6_2atmpS1067;
    int32_t _M0L4codeS167;
    int32_t _M0L1cS169;
    int32_t _M0L6_2atmpS1050;
    int32_t _M0L6_2atmpS1051;
    int32_t _M0L6_2atmpS1052;
    if (_M0L1iS164 >= _M0L3lenS161) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
      moonbit_decref_cycle_free(_M0L6_2aenvS163);
      break;
    }
    _M0L3strS1066 = _M0L4selfS162.$0;
    _M0L5startS1068 = _M0L4selfS162.$1;
    _M0L6_2atmpS1067 = _M0L5startS1068 + _M0L1iS164;
    _M0L4codeS167 = _M0L3strS1066[_M0L6_2atmpS1067];
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
        int32_t _M0L6_2atmpS1053;
        int32_t _M0L6_2atmpS1054;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_18.data);
        _M0L6_2atmpS1053 = _M0L1iS164 + 1;
        _M0L6_2atmpS1054 = _M0L1iS164 + 1;
        _M0L1iS164 = _M0L6_2atmpS1053;
        _M0L3segS165 = _M0L6_2atmpS1054;
        goto _2afor_166;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS1055;
        int32_t _M0L6_2atmpS1056;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_19.data);
        _M0L6_2atmpS1055 = _M0L1iS164 + 1;
        _M0L6_2atmpS1056 = _M0L1iS164 + 1;
        _M0L1iS164 = _M0L6_2atmpS1055;
        _M0L3segS165 = _M0L6_2atmpS1056;
        goto _2afor_166;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS1057;
        int32_t _M0L6_2atmpS1058;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_20.data);
        _M0L6_2atmpS1057 = _M0L1iS164 + 1;
        _M0L6_2atmpS1058 = _M0L1iS164 + 1;
        _M0L1iS164 = _M0L6_2atmpS1057;
        _M0L3segS165 = _M0L6_2atmpS1058;
        goto _2afor_166;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS1059;
        int32_t _M0L6_2atmpS1060;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS1059 = _M0L1iS164 + 1;
        _M0L6_2atmpS1060 = _M0L1iS164 + 1;
        _M0L1iS164 = _M0L6_2atmpS1059;
        _M0L3segS165 = _M0L6_2atmpS1060;
        goto _2afor_166;
        break;
      }
      default: {
        if (_M0L4codeS167 < 32) {
          int32_t _M0L6_2atmpS1062;
          moonbit_string_t _M0L6_2atmpS1061;
          int32_t _M0L6_2atmpS1063;
          int32_t _M0L6_2atmpS1064;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_22.data);
          _M0L6_2atmpS1062 = _M0L4codeS167 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS1061 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS1062);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, _M0L6_2atmpS1061);
          moonbit_decref_cycle_free(_M0L6_2atmpS1061);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS160.$0->$method_0(_M0L6loggerS160.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS1063 = _M0L1iS164 + 1;
          _M0L6_2atmpS1064 = _M0L1iS164 + 1;
          _M0L1iS164 = _M0L6_2atmpS1063;
          _M0L3segS165 = _M0L6_2atmpS1064;
          goto _2afor_166;
        } else {
          int32_t _M0L6_2atmpS1065 = _M0L1iS164 + 1;
          int32_t _tmp_2077 = _M0L3segS165;
          _M0L1iS164 = _M0L6_2atmpS1065;
          _M0L3segS165 = _tmp_2077;
          goto _2afor_166;
        }
        break;
      }
    }
    goto joinlet_2076;
    join_168:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS163, _M0L3segS165, _M0L1iS164);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS160.$0->$method_3(_M0L6loggerS160.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1050 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS169);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS160.$0->$method_3(_M0L6loggerS160.$1, _M0L6_2atmpS1050);
    _M0L6_2atmpS1051 = _M0L1iS164 + 1;
    _M0L6_2atmpS1052 = _M0L1iS164 + 1;
    _M0L1iS164 = _M0L6_2atmpS1051;
    _M0L3segS165 = _M0L6_2atmpS1052;
    continue;
    joinlet_2076:;
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
    int64_t _M0L6_2atmpS1049 = (int64_t)_M0L1iS157;
    struct _M0TPC16string10StringView _M0L6_2atmpS1048;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1048
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS156, _M0L3segS158, _M0L6_2atmpS1049);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS154.$0->$method_2(_M0L6loggerS154.$1, _M0L6_2atmpS1048);
    moonbit_decref_cycle_free(_M0L6_2atmpS1048.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS145,
  int32_t _M0L5startS147,
  int64_t _M0L3endS149
) {
  int32_t _M0L3endS1046;
  int32_t _M0L5startS1047;
  int32_t _M0L3lenS144;
  int32_t _M0Lm2loS146;
  int32_t _M0Lm2hiS148;
  moonbit_string_t _M0L3strS152;
  int32_t _M0L4baseS153;
  int32_t _M0L6_2atmpS1024;
  int32_t _if__result_2078;
  int32_t _M0L6_2atmpS1034;
  int32_t _if__result_2079;
  int32_t _M0L6_2atmpS1036;
  int32_t _M0L6_2atmpS1037;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS1046 = _M0L4selfS145.$2;
  _M0L5startS1047 = _M0L4selfS145.$1;
  _M0L3lenS144 = _M0L3endS1046 - _M0L5startS1047;
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
  _M0L6_2atmpS1024 = _M0Lm2loS146;
  if (_M0L6_2atmpS1024 > 0) {
    int32_t _M0L6_2atmpS1023 = _M0Lm2loS146;
    if (_M0L6_2atmpS1023 < _M0L3lenS144) {
      int32_t _M0L6_2atmpS1022 = _M0Lm2loS146;
      int32_t _M0L6_2atmpS1021 = _M0L4baseS153 + _M0L6_2atmpS1022;
      int32_t _M0L6_2atmpS1020 = _M0L3strS152[_M0L6_2atmpS1021];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1020)) {
        int32_t _M0L6_2atmpS1019 = _M0Lm2loS146;
        int32_t _M0L6_2atmpS1018 = _M0L4baseS153 + _M0L6_2atmpS1019;
        int32_t _M0L6_2atmpS1017 = _M0L6_2atmpS1018 - 1;
        int32_t _M0L6_2atmpS1016 = _M0L3strS152[_M0L6_2atmpS1017];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2078
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1016);
      } else {
        _if__result_2078 = 0;
      }
    } else {
      _if__result_2078 = 0;
    }
  } else {
    _if__result_2078 = 0;
  }
  if (_if__result_2078) {
    int32_t _M0L6_2atmpS1025 = _M0Lm2loS146;
    _M0Lm2loS146 = _M0L6_2atmpS1025 + 1;
  }
  _M0L6_2atmpS1034 = _M0Lm2hiS148;
  if (_M0L6_2atmpS1034 > 0) {
    int32_t _M0L6_2atmpS1033 = _M0Lm2hiS148;
    if (_M0L6_2atmpS1033 < _M0L3lenS144) {
      int32_t _M0L6_2atmpS1032 = _M0Lm2hiS148;
      int32_t _M0L6_2atmpS1031 = _M0L4baseS153 + _M0L6_2atmpS1032;
      int32_t _M0L6_2atmpS1030 = _M0L3strS152[_M0L6_2atmpS1031];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS1030)) {
        int32_t _M0L6_2atmpS1029 = _M0Lm2hiS148;
        int32_t _M0L6_2atmpS1028 = _M0L4baseS153 + _M0L6_2atmpS1029;
        int32_t _M0L6_2atmpS1027 = _M0L6_2atmpS1028 - 1;
        int32_t _M0L6_2atmpS1026 = _M0L3strS152[_M0L6_2atmpS1027];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_2079
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS1026);
      } else {
        _if__result_2079 = 0;
      }
    } else {
      _if__result_2079 = 0;
    }
  } else {
    _if__result_2079 = 0;
  }
  if (_if__result_2079) {
    int32_t _M0L6_2atmpS1035 = _M0Lm2hiS148;
    _M0Lm2hiS148 = _M0L6_2atmpS1035 - 1;
  }
  _M0L6_2atmpS1036 = _M0Lm2loS146;
  _M0L6_2atmpS1037 = _M0Lm2hiS148;
  if (_M0L6_2atmpS1036 >= _M0L6_2atmpS1037) {
    int32_t _M0L6_2atmpS1041 = _M0Lm2loS146;
    int32_t _M0L6_2atmpS1038 = _M0L4baseS153 + _M0L6_2atmpS1041;
    int32_t _M0L6_2atmpS1040 = _M0Lm2loS146;
    int32_t _M0L6_2atmpS1039 = _M0L4baseS153 + _M0L6_2atmpS1040;
    moonbit_incref_cycle_free(_M0L3strS152);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS152,
                                                 .$1 = _M0L6_2atmpS1038,
                                                 .$2 = _M0L6_2atmpS1039};
  } else {
    int32_t _M0L6_2atmpS1045 = _M0Lm2loS146;
    int32_t _M0L6_2atmpS1042 = _M0L4baseS153 + _M0L6_2atmpS1045;
    int32_t _M0L6_2atmpS1044 = _M0Lm2hiS148;
    int32_t _M0L6_2atmpS1043 = _M0L4baseS153 + _M0L6_2atmpS1044;
    moonbit_incref_cycle_free(_M0L3strS152);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS152,
                                                 .$1 = _M0L6_2atmpS1042,
                                                 .$2 = _M0L6_2atmpS1043};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS143) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS142;
  int32_t _M0L6_2atmpS1013;
  int32_t _M0L6_2atmpS1012;
  int32_t _M0L6_2atmpS1015;
  int32_t _M0L6_2atmpS1014;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS1011;
  moonbit_string_t _result_2080;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS142 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1013 = _M0IPC14byte4BytePB3Div3div(_M0L1bS143, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1012
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1013);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS142, _M0L6_2atmpS1012);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1015 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS143, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS1014
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS1015);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS142, _M0L6_2atmpS1014);
  _M0L6_2atmpS1011 = _M0L7_2aselfS142;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_2080 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS1011);
  moonbit_decref_cycle_free(_M0L6_2atmpS1011);
  return _result_2080;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS141) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS141 < 10) {
    int32_t _M0L6_2atmpS1008;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1008 = _M0IPC14byte4BytePB3Add3add(_M0L1iS141, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1008);
  } else {
    int32_t _M0L6_2atmpS1010;
    int32_t _M0L6_2atmpS1009;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1010 = _M0IPC14byte4BytePB3Add3add(_M0L1iS141, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS1009 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS1010, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS1009);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS139,
  int32_t _M0L4thatS140
) {
  int32_t _M0L6_2atmpS1006;
  int32_t _M0L6_2atmpS1007;
  int32_t _M0L6_2atmpS1005;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1006 = (int32_t)_M0L4selfS139;
  _M0L6_2atmpS1007 = (int32_t)_M0L4thatS140;
  _M0L6_2atmpS1005 = _M0L6_2atmpS1006 - _M0L6_2atmpS1007;
  return _M0L6_2atmpS1005 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS137,
  int32_t _M0L4thatS138
) {
  int32_t _M0L6_2atmpS1003;
  int32_t _M0L6_2atmpS1004;
  int32_t _M0L6_2atmpS1002;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1003 = (int32_t)_M0L4selfS137;
  _M0L6_2atmpS1004 = (int32_t)_M0L4thatS138;
  _M0L6_2atmpS1002 = _M0L6_2atmpS1003 % _M0L6_2atmpS1004;
  return _M0L6_2atmpS1002 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS135,
  int32_t _M0L4thatS136
) {
  int32_t _M0L6_2atmpS1000;
  int32_t _M0L6_2atmpS1001;
  int32_t _M0L6_2atmpS999;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS1000 = (int32_t)_M0L4selfS135;
  _M0L6_2atmpS1001 = (int32_t)_M0L4thatS136;
  _M0L6_2atmpS999 = _M0L6_2atmpS1000 / _M0L6_2atmpS1001;
  return _M0L6_2atmpS999 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS133,
  int32_t _M0L4thatS134
) {
  int32_t _M0L6_2atmpS997;
  int32_t _M0L6_2atmpS998;
  int32_t _M0L6_2atmpS996;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS997 = (int32_t)_M0L4selfS133;
  _M0L6_2atmpS998 = (int32_t)_M0L4thatS134;
  _M0L6_2atmpS996 = _M0L6_2atmpS997 + _M0L6_2atmpS998;
  return _M0L6_2atmpS996 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS132) {
  int32_t _M0L6_2atmpS995;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS995 = (int32_t)_M0L4selfS132;
  return _M0L6_2atmpS995;
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
  int32_t _M0L3lenS994;
  int32_t _M0L8requiredS128;
  uint16_t* _M0L4dataS989;
  int32_t _M0L6_2atmpS988;
  int32_t _if__result_2081;
  uint16_t* _M0L4dataS990;
  int32_t _M0L3lenS991;
  int32_t _M0L3lenS993;
  int32_t _M0L6_2atmpS992;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS126 = Moonbit_array_length(_M0L3strS127);
  if (_M0L8str__lenS126 == 0) {
    return 0;
  }
  _M0L3lenS994 = _M0L4selfS129->$1;
  _M0L8requiredS128 = _M0L3lenS994 + _M0L8str__lenS126;
  _M0L4dataS989 = _M0L4selfS129->$0;
  _M0L6_2atmpS988 = Moonbit_array_length(_M0L4dataS989);
  if (_M0L8requiredS128 > _M0L6_2atmpS988) {
    _if__result_2081 = 1;
  } else {
    int32_t _M0L3lenS987 = _M0L4selfS129->$1;
    _if__result_2081 = _M0L8requiredS128 < _M0L3lenS987;
  }
  if (_if__result_2081) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS129, _M0L8requiredS128);
  }
  _M0L4dataS990 = _M0L4selfS129->$0;
  _M0L3lenS991 = _M0L4selfS129->$1;
  moonbit_incref_cycle_free(_M0L4dataS990);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS990, _M0L3lenS991, _M0L3strS127, 0, _M0L8str__lenS126);
  moonbit_decref_cycle_free(_M0L4dataS990);
  _M0L3lenS993 = _M0L4selfS129->$1;
  _M0L6_2atmpS992 = _M0L3lenS993 + _M0L8str__lenS126;
  _M0L4selfS129->$1 = _M0L6_2atmpS992;
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
      int32_t _M0L6_2atmpS984 = _M0L3strS123[_M0L1iS120];
      int32_t _M0L6_2atmpS985;
      int32_t _M0L6_2atmpS986;
      _M0L4selfS122[_M0L1jS121] = _M0L6_2atmpS984;
      _M0L6_2atmpS985 = _M0L1iS120 + 1;
      _M0L6_2atmpS986 = _M0L1jS121 + 1;
      _M0L1iS120 = _M0L6_2atmpS985;
      _M0L1jS121 = _M0L6_2atmpS986;
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
    int32_t _M0L3lenS955 = _M0L4selfS115->$1;
    uint16_t* _M0L4dataS957 = _M0L4selfS115->$0;
    int32_t _M0L6_2atmpS956 = Moonbit_array_length(_M0L4dataS957);
    uint16_t* _M0L4dataS960;
    int32_t _M0L3lenS961;
    int32_t _M0L6_2atmpS962;
    int32_t _M0L3lenS964;
    int32_t _M0L6_2atmpS963;
    if (_M0L3lenS955 >= _M0L6_2atmpS956) {
      int32_t _M0L3lenS959 = _M0L4selfS115->$1;
      int32_t _M0L6_2atmpS958 = _M0L3lenS959 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS115, _M0L6_2atmpS958);
    }
    _M0L4dataS960 = _M0L4selfS115->$0;
    _M0L3lenS961 = _M0L4selfS115->$1;
    moonbit_incref_cycle_free(_M0L4dataS960);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS962 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS113);
    if (
      _M0L3lenS961 < 0 || _M0L3lenS961 >= Moonbit_array_length(_M0L4dataS960)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS960[_M0L3lenS961] = _M0L6_2atmpS962;
    moonbit_decref_cycle_free(_M0L4dataS960);
    _M0L3lenS964 = _M0L4selfS115->$1;
    _M0L6_2atmpS963 = _M0L3lenS964 + 1;
    _M0L4selfS115->$1 = _M0L6_2atmpS963;
  } else if (_M0L4codeS113 <= 1114111u) {
    uint16_t* _M0L4dataS968 = _M0L4selfS115->$0;
    int32_t _M0L6_2atmpS966 = Moonbit_array_length(_M0L4dataS968);
    int32_t _M0L3lenS967 = _M0L4selfS115->$1;
    int32_t _M0L6_2atmpS965 = _M0L6_2atmpS966 - _M0L3lenS967;
    uint32_t _M0L4codeS116;
    uint16_t* _M0L4dataS971;
    int32_t _M0L3lenS972;
    uint32_t _M0L6_2atmpS975;
    uint32_t _M0L6_2atmpS974;
    int32_t _M0L6_2atmpS973;
    uint16_t* _M0L4dataS976;
    int32_t _M0L3lenS981;
    int32_t _M0L6_2atmpS977;
    uint32_t _M0L6_2atmpS980;
    uint32_t _M0L6_2atmpS979;
    int32_t _M0L6_2atmpS978;
    int32_t _M0L3lenS983;
    int32_t _M0L6_2atmpS982;
    if (_M0L6_2atmpS965 < 2) {
      int32_t _M0L3lenS970 = _M0L4selfS115->$1;
      int32_t _M0L6_2atmpS969 = _M0L3lenS970 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS115, _M0L6_2atmpS969);
    }
    _M0L4codeS116 = _M0L4codeS113 - 65536u;
    _M0L4dataS971 = _M0L4selfS115->$0;
    _M0L3lenS972 = _M0L4selfS115->$1;
    _M0L6_2atmpS975 = _M0L4codeS116 >> 10;
    _M0L6_2atmpS974 = 55296u + _M0L6_2atmpS975;
    moonbit_incref_cycle_free(_M0L4dataS971);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS973 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS974);
    if (
      _M0L3lenS972 < 0 || _M0L3lenS972 >= Moonbit_array_length(_M0L4dataS971)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS971[_M0L3lenS972] = _M0L6_2atmpS973;
    moonbit_decref_cycle_free(_M0L4dataS971);
    _M0L4dataS976 = _M0L4selfS115->$0;
    _M0L3lenS981 = _M0L4selfS115->$1;
    _M0L6_2atmpS977 = _M0L3lenS981 + 1;
    _M0L6_2atmpS980 = _M0L4codeS116 & 1023u;
    _M0L6_2atmpS979 = 56320u + _M0L6_2atmpS980;
    moonbit_incref_cycle_free(_M0L4dataS976);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS978 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS979);
    if (
      _M0L6_2atmpS977 < 0
      || _M0L6_2atmpS977 >= Moonbit_array_length(_M0L4dataS976)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS976[_M0L6_2atmpS977] = _M0L6_2atmpS978;
    moonbit_decref_cycle_free(_M0L4dataS976);
    _M0L3lenS983 = _M0L4selfS115->$1;
    _M0L6_2atmpS982 = _M0L3lenS983 + 2;
    _M0L4selfS115->$1 = _M0L6_2atmpS982;
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
  uint16_t* _M0L4dataS954;
  int32_t _M0L6_2atmpS952;
  int32_t _M0L3lenS953;
  int32_t _M0L13new__capacityS109;
  uint16_t* _M0L4dataS949;
  int32_t _M0L6_2atmpS950;
  int32_t _M0L3lenS951;
  uint16_t* _M0L9new__dataS112;
  uint16_t* _M0L6_2aoldS1976;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS954 = _M0L4selfS110->$0;
  _M0L6_2atmpS952 = Moonbit_array_length(_M0L4dataS954);
  _M0L3lenS953 = _M0L4selfS110->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS109
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS952, _M0L3lenS953, _M0L8requiredS111);
  _M0L4dataS949 = _M0L4selfS110->$0;
  moonbit_incref_cycle_free(_M0L4dataS949);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS950 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS951 = _M0L4selfS110->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS112
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS949, _M0L13new__capacityS109, _M0L6_2atmpS950, _M0L3lenS951, 0, 0);
  _M0L6_2aoldS1976 = _M0L4selfS110->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS1976);
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
  int32_t _M0L6_2atmpS948;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS948 = *(int32_t*)&_M0L4selfS102;
  return (uint16_t)_M0L6_2atmpS948;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS101) {
  int32_t _M0L6_2atmpS947;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS947 = _M0L4selfS101;
  return *(uint32_t*)&_M0L6_2atmpS947;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS99
) {
  int32_t _M0L3lenS938;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS938 = _M0L4selfS99->$1;
  if (_M0L3lenS938 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS939 = _M0L4selfS99->$1;
    uint16_t* _M0L4dataS941 = _M0L4selfS99->$0;
    int32_t _M0L6_2atmpS940 = Moonbit_array_length(_M0L4dataS941);
    if (_M0L3lenS939 == _M0L6_2atmpS940) {
      uint16_t* _M0L4dataS942 = _M0L4selfS99->$0;
      moonbit_incref_cycle_free(_M0L4dataS942);
      return _M0L4dataS942;
    } else {
      uint16_t* _M0L4dataS943 = _M0L4selfS99->$0;
      int32_t _M0L3lenS944 = _M0L4selfS99->$1;
      int32_t _M0L6_2atmpS945;
      int32_t _M0L3lenS946;
      uint16_t* _M0L4dataS100;
      moonbit_incref_cycle_free(_M0L4dataS943);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS945 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS946 = _M0L4selfS99->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS100
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS943, _M0L3lenS944, _M0L6_2atmpS945, _M0L3lenS946, 0, 0);
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
  int32_t _if__result_2084;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS92 >= 0) {
    if (_M0L3lenS93 >= 0) {
      if (_M0L11src__offsetS94 >= 0) {
        if (_M0L11dst__offsetS95 >= 0) {
          int32_t _M0L6_2atmpS934 = _M0L11src__offsetS94 + _M0L3lenS93;
          int32_t _M0L6_2atmpS935 = Moonbit_array_length(_M0L3srcS96);
          if (_M0L6_2atmpS934 <= _M0L6_2atmpS935) {
            int32_t _M0L6_2atmpS933 = _M0L11dst__offsetS95 + _M0L3lenS93;
            _if__result_2084 = _M0L6_2atmpS933 <= _M0L13allocate__lenS92;
          } else {
            _if__result_2084 = 0;
          }
        } else {
          _if__result_2084 = 0;
        }
      } else {
        _if__result_2084 = 0;
      }
    } else {
      _if__result_2084 = 0;
    }
  } else {
    _if__result_2084 = 0;
  }
  if (_if__result_2084) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS96, _M0L13allocate__lenS92, _M0L4initS97, _M0L11src__offsetS94, _M0L11dst__offsetS95, _M0L3lenS93);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS98;
    int32_t _M0L6_2atmpS937;
    moonbit_string_t _M0L6_2atmpS936;
    uint16_t* _result_2085;
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
    _M0L6_2atmpS937 = Moonbit_array_length(_M0L3srcS96);
    moonbit_decref_cycle_free(_M0L3srcS96);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS98, _M0L6_2atmpS937);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS936
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS98);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS98);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_2085 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS936);
    moonbit_decref_cycle_free(_M0L6_2atmpS936);
    return _result_2085;
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
  struct _M0TPB13StringBuilder* _block_2086;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS83 < 1) {
    _M0L7initialS82 = 1;
  } else {
    int32_t _M0L6_2atmpS932 = _M0L10size__hintS83 + 1;
    _M0L7initialS82 = _M0L6_2atmpS932 / 2;
  }
  _M0L4dataS84 = (uint16_t*)moonbit_make_string(_M0L7initialS82, 0);
  _block_2086
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_2086)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 51, 0);
  _block_2086->$0 = _M0L4dataS84;
  _block_2086->$1 = 0;
  return _block_2086;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS81) {
  int32_t _M0L6_2atmpS931;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS931 = (int32_t)_M0L4selfS81;
  return _M0L6_2atmpS931;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS73,
  int32_t _M0L13allocate__lenS69,
  int32_t _M0L3lenS70,
  int32_t _M0L11src__offsetS71,
  int32_t _M0L11dst__offsetS72
) {
  int32_t _if__result_2087;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS69 >= 0) {
    if (_M0L3lenS70 >= 0) {
      if (_M0L11src__offsetS71 >= 0) {
        if (_M0L11dst__offsetS72 >= 0) {
          int32_t _M0L6_2atmpS922 = _M0L11src__offsetS71 + _M0L3lenS70;
          int32_t _M0L6_2atmpS923;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS923 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS73);
          if (_M0L6_2atmpS922 <= _M0L6_2atmpS923) {
            int32_t _M0L6_2atmpS921 = _M0L11dst__offsetS72 + _M0L3lenS70;
            _if__result_2087 = _M0L6_2atmpS921 <= _M0L13allocate__lenS69;
          } else {
            _if__result_2087 = 0;
          }
        } else {
          _if__result_2087 = 0;
        }
      } else {
        _if__result_2087 = 0;
      }
    } else {
      _if__result_2087 = 0;
    }
  } else {
    _if__result_2087 = 0;
  }
  if (_if__result_2087) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS69, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS73, _M0L11src__offsetS71, _M0L11dst__offsetS72, _M0L3lenS70);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS74;
    int32_t _M0L6_2atmpS925;
    moonbit_string_t _M0L6_2atmpS924;
    moonbit_string_t* _result_2088;
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
    _M0L6_2atmpS925 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS73);
    moonbit_decref_cycle_free(_M0L3srcS73);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS74, _M0L6_2atmpS925);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS924
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS74);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS74);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2088
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS924);
    moonbit_decref_cycle_free(_M0L6_2atmpS924);
    return _result_2088;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS79,
  int32_t _M0L13allocate__lenS75,
  int32_t _M0L3lenS76,
  int32_t _M0L11src__offsetS77,
  int32_t _M0L11dst__offsetS78
) {
  int32_t _if__result_2089;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS75 >= 0) {
    if (_M0L3lenS76 >= 0) {
      if (_M0L11src__offsetS77 >= 0) {
        if (_M0L11dst__offsetS78 >= 0) {
          int32_t _M0L6_2atmpS927 = _M0L11src__offsetS77 + _M0L3lenS76;
          int32_t _M0L6_2atmpS928;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS928
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS79);
          if (_M0L6_2atmpS927 <= _M0L6_2atmpS928) {
            int32_t _M0L6_2atmpS926 = _M0L11dst__offsetS78 + _M0L3lenS76;
            _if__result_2089 = _M0L6_2atmpS926 <= _M0L13allocate__lenS75;
          } else {
            _if__result_2089 = 0;
          }
        } else {
          _if__result_2089 = 0;
        }
      } else {
        _if__result_2089 = 0;
      }
    } else {
      _if__result_2089 = 0;
    }
  } else {
    _if__result_2089 = 0;
  }
  if (_if__result_2089) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS75, 0, _M0L3srcS79, _M0L11src__offsetS77, _M0L11dst__offsetS78, _M0L3lenS76);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS80;
    int32_t _M0L6_2atmpS930;
    moonbit_string_t _M0L6_2atmpS929;
    struct _M0TUsiE** _result_2090;
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
    _M0L6_2atmpS930 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS79);
    moonbit_decref_cycle_free(_M0L3srcS79);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS80, _M0L6_2atmpS930);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS929
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS80);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS80);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_2090
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS929);
    moonbit_decref_cycle_free(_M0L6_2atmpS929);
    return _result_2090;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS64,
  moonbit_string_t _M0L3objS63
) {
  struct _M0TPB6Logger _M0L6_2atmpS918;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS64);
  _M0L6_2atmpS918
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS64
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS63, _M0L6_2atmpS918);
  if (_M0L6_2atmpS918.$1) {
    moonbit_decref(_M0L6_2atmpS918.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS66,
  int32_t _M0L3objS65
) {
  struct _M0TPB6Logger _M0L6_2atmpS919;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS66);
  _M0L6_2atmpS919
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS66
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS65, _M0L6_2atmpS919);
  if (_M0L6_2atmpS919.$1) {
    moonbit_decref(_M0L6_2atmpS919.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS68,
  uint64_t _M0L3objS67
) {
  struct _M0TPB6Logger _M0L6_2atmpS920;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS68);
  _M0L6_2atmpS920
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS68
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS67, _M0L6_2atmpS920);
  if (_M0L6_2atmpS920.$1) {
    moonbit_decref(_M0L6_2atmpS920.$1);
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
        int32_t _M0L6_2atmpS891 = _M0L11dst__offsetS16 + _M0L1iS18;
        int32_t _M0L6_2atmpS893 = _M0L11src__offsetS17 + _M0L1iS18;
        int32_t _M0L6_2atmpS892;
        int32_t _M0L6_2atmpS894;
        if (
          _M0L6_2atmpS893 < 0
          || _M0L6_2atmpS893 >= Moonbit_array_length(_M0L3srcS15)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS892 = (int32_t)_M0L3srcS15[_M0L6_2atmpS893];
        if (
          _M0L6_2atmpS891 < 0
          || _M0L6_2atmpS891 >= Moonbit_array_length(_M0L3dstS14)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS14[_M0L6_2atmpS891] = _M0L6_2atmpS892;
        _M0L6_2atmpS894 = _M0L1iS18 + 1;
        _M0L1iS18 = _M0L6_2atmpS894;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS15);
        moonbit_decref_cycle_free(_M0L3dstS14);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS899 = _M0L3lenS19 - 1;
    int32_t _M0L1iS21 = _M0L6_2atmpS899;
    while (1) {
      if (_M0L1iS21 >= 0) {
        int32_t _M0L6_2atmpS895 = _M0L11dst__offsetS16 + _M0L1iS21;
        int32_t _M0L6_2atmpS897 = _M0L11src__offsetS17 + _M0L1iS21;
        int32_t _M0L6_2atmpS896;
        int32_t _M0L6_2atmpS898;
        if (
          _M0L6_2atmpS897 < 0
          || _M0L6_2atmpS897 >= Moonbit_array_length(_M0L3srcS15)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS896 = (int32_t)_M0L3srcS15[_M0L6_2atmpS897];
        if (
          _M0L6_2atmpS895 < 0
          || _M0L6_2atmpS895 >= Moonbit_array_length(_M0L3dstS14)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS14[_M0L6_2atmpS895] = _M0L6_2atmpS896;
        _M0L6_2atmpS898 = _M0L1iS21 - 1;
        _M0L1iS21 = _M0L6_2atmpS898;
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
        int32_t _M0L6_2atmpS900 = _M0L11dst__offsetS25 + _M0L1iS27;
        int32_t _M0L6_2atmpS902 = _M0L11src__offsetS26 + _M0L1iS27;
        moonbit_string_t _M0L6_2atmpS901;
        moonbit_string_t _M0L6_2aoldS1977;
        int32_t _M0L6_2atmpS903;
        if (
          _M0L6_2atmpS902 < 0
          || _M0L6_2atmpS902 >= Moonbit_array_length(_M0L3srcS24)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS901 = (moonbit_string_t)_M0L3srcS24[_M0L6_2atmpS902];
        if (
          _M0L6_2atmpS900 < 0
          || _M0L6_2atmpS900 >= Moonbit_array_length(_M0L3dstS23)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1977 = (moonbit_string_t)_M0L3dstS23[_M0L6_2atmpS900];
        moonbit_incref_cycle_free(_M0L6_2atmpS901);
        moonbit_decref_cycle_free(_M0L6_2aoldS1977);
        _M0L3dstS23[_M0L6_2atmpS900] = _M0L6_2atmpS901;
        _M0L6_2atmpS903 = _M0L1iS27 + 1;
        _M0L1iS27 = _M0L6_2atmpS903;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS24);
        moonbit_decref_cycle_free(_M0L3dstS23);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS908 = _M0L3lenS28 - 1;
    int32_t _M0L1iS30 = _M0L6_2atmpS908;
    while (1) {
      if (_M0L1iS30 >= 0) {
        int32_t _M0L6_2atmpS904 = _M0L11dst__offsetS25 + _M0L1iS30;
        int32_t _M0L6_2atmpS906 = _M0L11src__offsetS26 + _M0L1iS30;
        moonbit_string_t _M0L6_2atmpS905;
        moonbit_string_t _M0L6_2aoldS1978;
        int32_t _M0L6_2atmpS907;
        if (
          _M0L6_2atmpS906 < 0
          || _M0L6_2atmpS906 >= Moonbit_array_length(_M0L3srcS24)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS905 = (moonbit_string_t)_M0L3srcS24[_M0L6_2atmpS906];
        if (
          _M0L6_2atmpS904 < 0
          || _M0L6_2atmpS904 >= Moonbit_array_length(_M0L3dstS23)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1978 = (moonbit_string_t)_M0L3dstS23[_M0L6_2atmpS904];
        moonbit_incref_cycle_free(_M0L6_2atmpS905);
        moonbit_decref_cycle_free(_M0L6_2aoldS1978);
        _M0L3dstS23[_M0L6_2atmpS904] = _M0L6_2atmpS905;
        _M0L6_2atmpS907 = _M0L1iS30 - 1;
        _M0L1iS30 = _M0L6_2atmpS907;
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
        int32_t _M0L6_2atmpS909 = _M0L11dst__offsetS34 + _M0L1iS36;
        int32_t _M0L6_2atmpS911 = _M0L11src__offsetS35 + _M0L1iS36;
        struct _M0TUsiE* _M0L6_2atmpS910;
        struct _M0TUsiE* _M0L6_2aoldS1979;
        int32_t _M0L6_2atmpS912;
        if (
          _M0L6_2atmpS911 < 0
          || _M0L6_2atmpS911 >= Moonbit_array_length(_M0L3srcS33)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS910 = (struct _M0TUsiE*)_M0L3srcS33[_M0L6_2atmpS911];
        if (
          _M0L6_2atmpS909 < 0
          || _M0L6_2atmpS909 >= Moonbit_array_length(_M0L3dstS32)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1979 = (struct _M0TUsiE*)_M0L3dstS32[_M0L6_2atmpS909];
        if (_M0L6_2atmpS910) {
          moonbit_incref_cycle_free(_M0L6_2atmpS910);
        }
        if (_M0L6_2aoldS1979) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1979);
        }
        _M0L3dstS32[_M0L6_2atmpS909] = _M0L6_2atmpS910;
        _M0L6_2atmpS912 = _M0L1iS36 + 1;
        _M0L1iS36 = _M0L6_2atmpS912;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS33);
        moonbit_decref_cycle_free(_M0L3dstS32);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS917 = _M0L3lenS37 - 1;
    int32_t _M0L1iS39 = _M0L6_2atmpS917;
    while (1) {
      if (_M0L1iS39 >= 0) {
        int32_t _M0L6_2atmpS913 = _M0L11dst__offsetS34 + _M0L1iS39;
        int32_t _M0L6_2atmpS915 = _M0L11src__offsetS35 + _M0L1iS39;
        struct _M0TUsiE* _M0L6_2atmpS914;
        struct _M0TUsiE* _M0L6_2aoldS1980;
        int32_t _M0L6_2atmpS916;
        if (
          _M0L6_2atmpS915 < 0
          || _M0L6_2atmpS915 >= Moonbit_array_length(_M0L3srcS33)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS914 = (struct _M0TUsiE*)_M0L3srcS33[_M0L6_2atmpS915];
        if (
          _M0L6_2atmpS913 < 0
          || _M0L6_2atmpS913 >= Moonbit_array_length(_M0L3dstS32)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS1980 = (struct _M0TUsiE*)_M0L3dstS32[_M0L6_2atmpS913];
        if (_M0L6_2atmpS914) {
          moonbit_incref_cycle_free(_M0L6_2atmpS914);
        }
        if (_M0L6_2aoldS1980) {
          moonbit_decref_cycle_free(_M0L6_2aoldS1980);
        }
        _M0L3dstS32[_M0L6_2atmpS913] = _M0L6_2atmpS914;
        _M0L6_2atmpS916 = _M0L1iS39 - 1;
        _M0L1iS39 = _M0L6_2atmpS916;
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

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS863) {
  switch (Moonbit_object_tag(_M0L4_2aeS863)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_32.data;
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_33.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS863);
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
  void* _M0L11_2aobj__ptrS886,
  struct _M0TPB4Show _M0L8_2aparamS885
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS884 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS886;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS884, _M0L8_2aparamS885);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS883,
  struct _M0TPB4Show _M0L8_2aparamS882
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS881 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS883;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS881, _M0L8_2aparamS882);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS880,
  int32_t _M0L8_2aparamS879
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS878 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS880;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS878, _M0L8_2aparamS879);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS877,
  struct _M0TPC16string10StringView _M0L8_2aparamS876
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS875 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS877;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS875, _M0L8_2aparamS876);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS874,
  moonbit_string_t _M0L8_2aparamS871,
  int32_t _M0L8_2aparamS872,
  int32_t _M0L8_2aparamS873
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS870 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS874;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS870, _M0L8_2aparamS871, _M0L8_2aparamS872, _M0L8_2aparamS873);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS869,
  moonbit_string_t _M0L8_2aparamS868
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS867 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS869;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS867, _M0L8_2aparamS868);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS890;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS856;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS857;
  int32_t _M0L7_2abindS858;
  struct _M0TUsiE** _M0L7_2abindS859;
  int32_t _M0L6_2acntS1985;
  int32_t _M0L2__S860;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS890
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS856
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS856)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 54, 0);
  _M0L12async__testsS856->$0 = _M0L6_2atmpS890;
  _M0L12async__testsS856->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS857
  = _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS858 = _M0L7_2abindS857->$1;
  _M0L7_2abindS859 = _M0L7_2abindS857->$0;
  _M0L6_2acntS1985
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS857));
  if (_M0L6_2acntS1985 > 1) {
    int32_t _M0L11_2anew__cntS1986 = _M0L6_2acntS1985 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS857), _M0L11_2anew__cntS1986);
    moonbit_incref_cycle_free(_M0L7_2abindS859);
  } else if (_M0L6_2acntS1985 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS857);
  }
  _M0L2__S860 = 0;
  while (1) {
    if (_M0L2__S860 < _M0L7_2abindS858) {
      struct _M0TUsiE* _M0L3argS861 =
        (struct _M0TUsiE*)_M0L7_2abindS859[_M0L2__S860];
      moonbit_string_t _M0L6_2atmpS887 = _M0L3argS861->$0;
      int32_t _M0L6_2atmpS888 = _M0L3argS861->$1;
      int32_t _M0L6_2atmpS889;
      moonbit_incref_cycle_free(_M0L6_2atmpS887);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples31synapse__params__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS856, _M0L6_2atmpS887, _M0L6_2atmpS888);
      moonbit_decref_cycle_free(_M0L6_2atmpS887);
      _M0L6_2atmpS889 = _M0L2__S860 + 1;
      _M0L2__S860 = _M0L6_2atmpS889;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS859);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\synapse_params\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples31synapse__params__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples31synapse__params__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS856);
  moonbit_decref_cycle_free(_M0L12async__testsS856);
  moonbit_flush_cycles();
  return 0;
}