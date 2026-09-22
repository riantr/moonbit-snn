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

struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1960;

struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure;

struct _M0TP26RiantR8snn__mbt4AdEx;

struct _M0TP26RiantR8snn__mbt13AdExPostSpike;

struct _M0TP26RiantR8snn__mbt11WCParameter;

struct _M0TWRPC15error5ErrorEs;

struct _M0TP26RiantR8snn__mbt12PoissonLayer;

struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter;

struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry;

struct _M0TWssbEu;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok;

struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__;

struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__;

struct _M0TUsiE;

struct _M0TPB17FloatingDecimal64;

struct _M0TP26RiantR8snn__mbt15HetRecParameter;

struct _M0TP26RiantR8snn__mbt16BalancedStimulus;

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err;

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

struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__;

struct _M0TP26RiantR8snn__mbt11HHParameter;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__;

struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError;

struct _M0TURPC16string10StringViewRPB6LoggerE;

struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__;

struct _M0TP26RiantR8snn__mbt14SpikingSynapse;

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

struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__;

struct _M0TP26RiantR8snn__mbt11WilsonCowan;

struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError;

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

struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1955;

struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric;

struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables;

struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE;

struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError;

struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet;

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

struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep;

struct _M0BTPB6Logger;

struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind15AntiSymmetric__;

struct _M0TP26RiantR8snn__mbt7Monitor;

struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep;

struct _M0TP26RiantR8snn__mbt6HetRec;

struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__;

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

struct _M0TPB8MutLocalGiE;

struct _M0TP26RiantR8snn__mbt12STDPGerstner;

struct _M0TP26RiantR8snn__mbt11MorrisLecar;

struct _M0TP26RiantR8snn__mbt7Poisson;

struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter;

struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error;

struct _M0DTP26RiantR8snn__mbt7AnyStim14PoissonLayer__;

struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray;

struct _M0DTPC16result6ResultGOuRPC15error5ErrorE3Err;

struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__;

struct _M0TPB8MutLocalGdE;

struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__;

struct _M0BTPB4Show;

struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest;

struct _M0TWuEu;

struct _M0TPC16string10StringView;

struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables;

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

struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1960 {
  int32_t(* code)(
    struct _M0TWssbEu*,
    moonbit_string_t,
    moonbit_string_t,
    int32_t
  );
  int32_t $0;
  moonbit_string_t $1;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test33MoonBitTestDriverInternalSkipTestE2Ok {
  int32_t $0;
  
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

struct _M0DTPC16result6ResultGbRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test33MoonBitTestDriverInternalSkipTestE3Err {
  void* $0;
  
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

struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError {
  moonbit_string_t $0;
  
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

struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1955 {
  int32_t(* code)(struct _M0TWEu*);
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

struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet {
  struct _M0TPB5ArrayGfE* $0;
  struct _M0TPB5ArrayGfE* $1;
  struct _M0TPB5ArrayGfE* $2;
  float $3;
  float $4;
  
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

struct _M0TP26RiantR8snn__mbt7Monitor {
  struct _M0TP26RiantR8snn__mbt2IF* $0;
  moonbit_string_t $1;
  struct _M0TPB5ArrayGfE* $2;
  struct _M0TPB5ArrayGfE* $3;
  int32_t $4;
  int32_t $5;
  int32_t $6;
  
};

struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep {
  float $0;
  float $1;
  float $2;
  float $3;
  float $4;
  
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

struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest {
  moonbit_string_t $0;
  
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

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu*
);

int32_t _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t
);

moonbit_string_t _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1967(
  struct _M0TWRPC15error5ErrorEs*,
  void*
);

int32_t _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1960(
  struct _M0TWssbEu*,
  moonbit_string_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1955(
  struct _M0TWEu*
);

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
);

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1932(
  int32_t,
  moonbit_string_t,
  int32_t
);

int32_t _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1925(
  int32_t,
  moonbit_string_t
);

#define _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi moonbit_rt_get_cli_args

moonbit_string_t _M0MP46RiantR8snn__mbt8examples27timed__stim__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*,
  moonbit_string_t,
  int32_t,
  struct _M0TWEu*,
  struct _M0TWssbEu*,
  struct _M0TWRPC15error5ErrorEs*
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
);

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*
);

int32_t _M0FP26RiantR8snn__mbt23heterogeneous__sim__for(
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel*,
  float
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

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor9new__fire(
  struct _M0TP26RiantR8snn__mbt2IF*,
  int32_t
);

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor6new__v(
  struct _M0TP26RiantR8snn__mbt2IF*,
  int32_t
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

struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0MP26RiantR8snn__mbt17SpikeTimeStimulus3new(
  struct _M0TP26RiantR8snn__mbt2IF*,
  moonbit_string_t,
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGiE*
);

struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0MP26RiantR8snn__mbt18SpikeTimeParameter3new(
  struct _M0TPB5ArrayGfE*,
  struct _M0TPB5ArrayGiE*
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

int32_t _M0MP26RiantR8snn__mbt7Monitor13count__spikes(
  struct _M0TP26RiantR8snn__mbt7Monitor*
);

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

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4copyGfE(struct _M0TPB5ArrayGfE*);

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4copyGiE(struct _M0TPB5ArrayGiE*);

int32_t _M0MPC15array5Array12unsafe__blitGfE(
  struct _M0TPB5ArrayGfE*,
  int32_t,
  struct _M0TPB5ArrayGfE*,
  int32_t,
  int32_t
);

int32_t _M0MPC15array5Array12unsafe__blitGiE(
  struct _M0TPB5ArrayGiE*,
  int32_t,
  struct _M0TPB5ArrayGiE*,
  int32_t,
  int32_t
);

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE*);

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE*);

float _M0MPC15array5Array2atGfE(struct _M0TPB5ArrayGfE*, int32_t);

moonbit_string_t _M0MPC15array5Array2atGsE(struct _M0TPB5ArrayGsE*, int32_t);

int32_t _M0MPC15array5Array2atGiE(struct _M0TPB5ArrayGiE*, int32_t);

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*,
  int32_t
);

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

int32_t _M0MPC15array5Array7reallocGfE(struct _M0TPB5ArrayGfE*, int32_t);

int32_t _M0MPC15array5Array7reallocGiE(struct _M0TPB5ArrayGiE*, int32_t);

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE*,
  int32_t
);

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
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

int32_t _M0MPC15array5Array8capacityGsE(struct _M0TPB5ArrayGsE*);

int32_t _M0MPC15array5Array8capacityGUsiEE(struct _M0TPB5ArrayGUsiEE*);

int32_t _M0MPC15array5Array8capacityGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array8capacityGiE(struct _M0TPB5ArrayGiE*);

int32_t _M0FPB23array__growth__capacity(int32_t, int32_t, int32_t);

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE*);

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE*);

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE*);

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE*);

moonbit_string_t* _M0MPC15array5Array6bufferGsE(struct _M0TPB5ArrayGsE*);

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE*
);

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE*);

struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE*
);

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

int32_t _M0MPB18UninitializedArray6lengthGsE(moonbit_string_t*);

int32_t _M0MPB18UninitializedArray6lengthGUsiEE(struct _M0TUsiE**);

int32_t _M0MPB18UninitializedArray6lengthGfE(float*);

int32_t _M0MPB18UninitializedArray6lengthGiE(int32_t*);

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

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(moonbit_string_t);

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(moonbit_string_t);

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
} const moonbit_string_literal_24 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 116, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_22 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 114, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_30 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 15, 44, 32, 
    100, 115, 116, 95, 111, 102, 102, 115, 101, 116, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[19]; 
} const moonbit_string_literal_26 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 18, 105, 110, 
    118, 97, 108, 105, 100, 32, 99, 111, 100, 101, 32, 112, 111, 105, 
    110, 116, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_15 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 45, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[12]; 
} const moonbit_string_literal_5 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 11, 44, 34, 
    109, 101, 115, 115, 97, 103, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[53]; 
} const moonbit_string_literal_36 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 52, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 46, 83, 110, 97, 112, 115, 
    104, 111, 116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_21 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 110, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[31]; 
} const moonbit_string_literal_19 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 30, 114, 97, 
    100, 105, 120, 32, 109, 117, 115, 116, 32, 98, 101, 32, 98, 101, 
    116, 119, 101, 101, 110, 32, 50, 32, 97, 110, 100, 32, 51, 54, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_16 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 73, 110, 
    102, 105, 110, 105, 116, 121, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_14 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 78, 97, 78, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[25]; 
} const moonbit_string_literal_3 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 24, 123, 34, 
    116, 121, 112, 101, 34, 58, 34, 114, 101, 115, 117, 108, 116, 34, 
    44, 34, 102, 105, 108, 101, 34, 58, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[117]; 
} const moonbit_string_literal_38 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 116, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 116, 105, 109, 101, 100, 95, 115, 
    116, 105, 109, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 
    115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 
    68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 
    83, 107, 105, 112, 84, 101, 115, 116, 46, 77, 111, 111, 110, 66, 
    105, 116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 
    116, 101, 114, 110, 97, 108, 83, 107, 105, 112, 84, 101, 115, 116, 
    0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_12 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[115]; 
} const moonbit_string_literal_37 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 114, 82, 105, 
    97, 110, 116, 82, 47, 115, 110, 110, 95, 109, 98, 116, 47, 101, 120, 
    97, 109, 112, 108, 101, 115, 47, 116, 105, 109, 101, 100, 95, 115, 
    116, 105, 109, 95, 98, 108, 97, 99, 107, 98, 111, 120, 95, 116, 101, 
    115, 116, 46, 77, 111, 111, 110, 66, 105, 116, 84, 101, 115, 116, 
    68, 114, 105, 118, 101, 114, 73, 110, 116, 101, 114, 110, 97, 108, 
    74, 115, 69, 114, 114, 111, 114, 46, 77, 111, 111, 110, 66, 105, 
    116, 84, 101, 115, 116, 68, 114, 105, 118, 101, 114, 73, 110, 116, 
    101, 114, 110, 97, 108, 74, 115, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_31 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 44, 32, 
    108, 101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_28 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 98, 111, 
    117, 110, 100, 115, 32, 99, 104, 101, 99, 107, 32, 102, 97, 105, 
    108, 101, 100, 58, 32, 97, 108, 108, 111, 99, 97, 116, 101, 95, 108, 
    101, 110, 32, 61, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_25 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 92, 117, 123, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_9 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 103, 101, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_34 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 41, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[37]; 
} const moonbit_string_literal_20 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 36, 48, 49, 
    50, 51, 52, 53, 54, 55, 56, 57, 97, 98, 99, 100, 101, 102, 103, 104, 
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 
    118, 119, 120, 121, 122, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[3]; 
} const moonbit_string_literal_23 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 2, 92, 98, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[5]; 
} const moonbit_string_literal_11 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 4, 102, 105, 
    114, 101, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[51]; 
} const moonbit_string_literal_35 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 50, 109, 111, 
    111, 110, 98, 105, 116, 108, 97, 110, 103, 47, 99, 111, 114, 101, 
    47, 98, 117, 105, 108, 116, 105, 110, 46, 73, 110, 115, 112, 101, 
    99, 116, 69, 114, 114, 111, 114, 46, 73, 110, 115, 112, 101, 99, 
    116, 69, 114, 114, 111, 114, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[16]; 
} const moonbit_string_literal_32 =
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
} const moonbit_string_literal_29 =
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
} const moonbit_string_literal_18 =
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
} const moonbit_string_literal_13 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 25, 73, 108, 
    108, 101, 103, 97, 108, 65, 114, 103, 117, 109, 101, 110, 116, 69, 
    120, 99, 101, 112, 116, 105, 111, 110, 32, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[9]; 
} const moonbit_string_literal_33 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 8, 70, 97, 
    105, 108, 117, 114, 101, 40, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[32]; 
} const moonbit_string_literal_27 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 31, 83, 116, 
    114, 105, 110, 103, 66, 117, 105, 108, 100, 101, 114, 32, 99, 97, 
    112, 97, 99, 105, 116, 121, 32, 111, 118, 101, 114, 102, 108, 111, 
    119, 0
  };

struct { int32_t rc; uint32_t meta; uint16_t const data[4]; 
} const moonbit_string_literal_17 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 3, 48, 46, 48, 0};

struct { int32_t rc; uint32_t meta; uint16_t const data[2]; 
} const moonbit_string_literal_6 =
  { Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY), 1, 125, 0};

struct moonbit_object const moonbit_constant_constructor_0 =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0)
  };

struct { int32_t rc; uint32_t meta; struct _M0TWRPC15error5ErrorEs data; 
} const _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1967$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1967
  };

struct { int32_t rc; uint32_t meta; struct _M0TWEu data; 
} const _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure =
  {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REGULAR),
    Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0),
    _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test34MoonBit__Async__Test__Driver__ImplE
  };

uint32_t const moonbit_layout_table_data[105] =
  {
    sizeof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1955)
    / 4, 1,
    offsetof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1955, $1)
    / 4
    * 2,
    sizeof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1960)
    / 4, 1,
    offsetof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1960, $1)
    / 4
    * 2,
    sizeof(struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest)
    / 4, 1,
    offsetof(struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest, $0)
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
    sizeof(struct _M0TPB5ArrayGfE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGfE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt7Monitor) / 4, 4,
    offsetof(struct _M0TP26RiantR8snn__mbt7Monitor, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt7Monitor, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt7Monitor, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt7Monitor, $3) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGiE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGiE, $0) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus) / 4, 5,
    offsetof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus, $1) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus, $2) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus, $3) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus, $4) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus, $5) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter) / 4, 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter, $1) / 4 * 2,
    sizeof(struct _M0TP26RiantR8snn__mbt4Time) / 4, 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4Time, $0) / 4 * 2,
    offsetof(struct _M0TP26RiantR8snn__mbt4Time, $1) / 4 * 2,
    sizeof(struct _M0TPB5ArrayGbE) / 4, 1,
    offsetof(struct _M0TPB5ArrayGbE, $0) / 4 * 2,
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

struct _M0TWEu* _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test28MoonBit__Async__Test__Driver26is__being__cancelled_2ecloGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test34MoonBit__Async__Test__Driver__ImplE =
  (struct _M0TWEu*)&_M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test34MoonBit__Async__Test__Driver__ImplE$closure.data;

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

double _M0FPC16double14not__a__number;

double _M0FPC16double13neg__infinity;

double _M0FPC16double13min__positive;

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test28MoonBit__Async__Test__Driver30is__being__cancelled_2edyncallGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TWEu* _M0L6_2aenvS5533
) {
  return _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test34MoonBit__Async__Test__Driver__ImplE();
}

int32_t _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test44moonbit__test__driver__internal__do__execute(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1988,
  moonbit_string_t _M0L8filenameS1957,
  int32_t _M0L5indexS1959
) {
  struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1955* _closure_5740;
  struct _M0TWEu* _M0L13handle__startS1955;
  struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1960* _closure_5741;
  struct _M0TWssbEu* _M0L14handle__resultS1960;
  struct _M0TWRPC15error5ErrorEs* _M0L17error__to__stringS1967;
  void* _M0L11_2atry__errS1982;
  struct moonbit_result_0 _tmp_5743;
  int32_t _handle__error__result_5744;
  int32_t _M0L6_2atmpS5521;
  void* _M0L3errS1983;
  moonbit_string_t _M0L4nameS1985;
  struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest* _M0L36_2aMoonBitTestDriverInternalSkipTestS1986;
  moonbit_string_t _M0L7_2anameS1987;
  int32_t _M0L6_2acntS5562;
  #line 563 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L8filenameS1957);
  _closure_5740
  = (struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1955*)moonbit_malloc(sizeof(struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1955));
  Moonbit_object_header(_closure_5740)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 0, 0);
  _closure_5740->code
  = &_M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1955;
  _closure_5740->$0 = _M0L5indexS1959;
  _closure_5740->$1 = _M0L8filenameS1957;
  _M0L13handle__startS1955 = (struct _M0TWEu*)_closure_5740;
  moonbit_incref_cycle_free(_M0L8filenameS1957);
  _closure_5741
  = (struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1960*)moonbit_malloc(sizeof(struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1960));
  Moonbit_object_header(_closure_5741)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 3, 0);
  _closure_5741->code
  = &_M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1960;
  _closure_5741->$0 = _M0L5indexS1959;
  _closure_5741->$1 = _M0L8filenameS1957;
  _M0L14handle__resultS1960 = (struct _M0TWssbEu*)_closure_5741;
  _M0L17error__to__stringS1967
  = (struct _M0TWRPC15error5ErrorEs*)&_M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1967$closure.data;
  #line 605 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _tmp_5743
  = _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(_M0L12async__testsS1988, _M0L8filenameS1957, _M0L5indexS1959, _M0L13handle__startS1955, _M0L14handle__resultS1960, _M0L17error__to__stringS1967);
  if (_tmp_5743.tag) {
    int32_t const _M0L5_2aokS5530 = _tmp_5743.data.ok;
    _handle__error__result_5744 = _M0L5_2aokS5530;
  } else {
    void* const _M0L6_2aerrS5531 = _tmp_5743.data.err;
    moonbit_decref_cycle_free(_M0L17error__to__stringS1967);
    moonbit_decref_cycle_free(_M0L13handle__startS1955);
    _M0L11_2atry__errS1982 = _M0L6_2aerrS5531;
    goto join_1981;
  }
  if (_handle__error__result_5744) {
    moonbit_decref_cycle_free(_M0L17error__to__stringS1967);
    moonbit_decref_cycle_free(_M0L13handle__startS1955);
    _M0L6_2atmpS5521 = 1;
  } else {
    struct moonbit_result_0 _tmp_5745;
    int32_t _handle__error__result_5746;
    #line 608 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
    _tmp_5745
    = _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(_M0L12async__testsS1988, _M0L8filenameS1957, _M0L5indexS1959, _M0L13handle__startS1955, _M0L14handle__resultS1960, _M0L17error__to__stringS1967);
    if (_tmp_5745.tag) {
      int32_t const _M0L5_2aokS5528 = _tmp_5745.data.ok;
      _handle__error__result_5746 = _M0L5_2aokS5528;
    } else {
      void* const _M0L6_2aerrS5529 = _tmp_5745.data.err;
      moonbit_decref_cycle_free(_M0L17error__to__stringS1967);
      moonbit_decref_cycle_free(_M0L13handle__startS1955);
      _M0L11_2atry__errS1982 = _M0L6_2aerrS5529;
      goto join_1981;
    }
    if (_handle__error__result_5746) {
      moonbit_decref_cycle_free(_M0L17error__to__stringS1967);
      moonbit_decref_cycle_free(_M0L13handle__startS1955);
      _M0L6_2atmpS5521 = 1;
    } else {
      struct moonbit_result_0 _tmp_5747;
      int32_t _handle__error__result_5748;
      #line 611 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
      _tmp_5747
      = _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(_M0L12async__testsS1988, _M0L8filenameS1957, _M0L5indexS1959, _M0L13handle__startS1955, _M0L14handle__resultS1960, _M0L17error__to__stringS1967);
      if (_tmp_5747.tag) {
        int32_t const _M0L5_2aokS5526 = _tmp_5747.data.ok;
        _handle__error__result_5748 = _M0L5_2aokS5526;
      } else {
        void* const _M0L6_2aerrS5527 = _tmp_5747.data.err;
        moonbit_decref_cycle_free(_M0L17error__to__stringS1967);
        moonbit_decref_cycle_free(_M0L13handle__startS1955);
        _M0L11_2atry__errS1982 = _M0L6_2aerrS5527;
        goto join_1981;
      }
      if (_handle__error__result_5748) {
        moonbit_decref_cycle_free(_M0L17error__to__stringS1967);
        moonbit_decref_cycle_free(_M0L13handle__startS1955);
        _M0L6_2atmpS5521 = 1;
      } else {
        struct moonbit_result_0 _tmp_5749;
        int32_t _handle__error__result_5750;
        #line 614 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
        _tmp_5749
        = _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(_M0L12async__testsS1988, _M0L8filenameS1957, _M0L5indexS1959, _M0L13handle__startS1955, _M0L14handle__resultS1960, _M0L17error__to__stringS1967);
        if (_tmp_5749.tag) {
          int32_t const _M0L5_2aokS5524 = _tmp_5749.data.ok;
          _handle__error__result_5750 = _M0L5_2aokS5524;
        } else {
          void* const _M0L6_2aerrS5525 = _tmp_5749.data.err;
          moonbit_decref_cycle_free(_M0L17error__to__stringS1967);
          moonbit_decref_cycle_free(_M0L13handle__startS1955);
          _M0L11_2atry__errS1982 = _M0L6_2aerrS5525;
          goto join_1981;
        }
        if (_handle__error__result_5750) {
          moonbit_decref_cycle_free(_M0L17error__to__stringS1967);
          moonbit_decref_cycle_free(_M0L13handle__startS1955);
          _M0L6_2atmpS5521 = 1;
        } else {
          struct moonbit_result_0 _tmp_5751;
          #line 617 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
          _tmp_5751
          = _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(_M0L12async__testsS1988, _M0L8filenameS1957, _M0L5indexS1959, _M0L13handle__startS1955, _M0L14handle__resultS1960, _M0L17error__to__stringS1967);
          moonbit_decref_cycle_free(_M0L13handle__startS1955);
          moonbit_decref_cycle_free(_M0L17error__to__stringS1967);
          if (_tmp_5751.tag) {
            int32_t const _M0L5_2aokS5522 = _tmp_5751.data.ok;
            _M0L6_2atmpS5521 = _M0L5_2aokS5522;
          } else {
            void* const _M0L6_2aerrS5523 = _tmp_5751.data.err;
            _M0L11_2atry__errS1982 = _M0L6_2aerrS5523;
            goto join_1981;
          }
        }
      }
    }
  }
  if (!_M0L6_2atmpS5521) {
    void* _M0L130RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5532 =
      (void*)moonbit_malloc(sizeof(struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest));
    Moonbit_object_header(_M0L130RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5532)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 6, 1);
    ((struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L130RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5532)->$0
    = (moonbit_string_t)moonbit_string_literal_0.data;
    _M0L11_2atry__errS1982
    = _M0L130RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTestS5532;
    goto join_1981;
  } else {
    moonbit_decref_cycle_free(_M0L14handle__resultS1960);
  }
  goto joinlet_5742;
  join_1981:;
  _M0L3errS1983 = _M0L11_2atry__errS1982;
  _M0L36_2aMoonBitTestDriverInternalSkipTestS1986
  = (struct _M0DTPC15error5Error130RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2eMoonBitTestDriverInternalSkipTest_2eMoonBitTestDriverInternalSkipTest*)_M0L3errS1983;
  _M0L7_2anameS1987 = _M0L36_2aMoonBitTestDriverInternalSkipTestS1986->$0;
  _M0L6_2acntS5562
  = Moonbit_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1986));
  if (_M0L6_2acntS5562 > 1) {
    int32_t _M0L11_2anew__cntS5563 = _M0L6_2acntS5562 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L36_2aMoonBitTestDriverInternalSkipTestS1986), _M0L11_2anew__cntS5563);
    moonbit_incref_cycle_free(_M0L7_2anameS1987);
  } else if (_M0L6_2acntS5562 == 1) {
    #line 624 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L36_2aMoonBitTestDriverInternalSkipTestS1986);
  }
  _M0L4nameS1985 = _M0L7_2anameS1987;
  goto join_1984;
  goto joinlet_5752;
  join_1984:;
  #line 625 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1960(_M0L14handle__resultS1960, _M0L4nameS1985, (moonbit_string_t)moonbit_string_literal_1.data, 1);
  moonbit_decref_cycle_free(_M0L14handle__resultS1960);
  moonbit_decref_cycle_free(_M0L4nameS1985);
  joinlet_5752:;
  joinlet_5742:;
  return 0;
}

moonbit_string_t _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test44moonbit__test__driver__internal__do__executeN17error__to__stringS1967(
  struct _M0TWRPC15error5ErrorEs* _M0L6_2aenvS5520,
  void* _M0L3errS1968
) {
  void* _M0L1eS1970;
  moonbit_string_t _M0L1eS1972;
  moonbit_string_t _result_5755;
  #line 594 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  switch (Moonbit_object_tag(_M0L3errS1968)) {
    case 0: {
      struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure* _M0L10_2aFailureS1973 =
        (struct _M0DTPC15error5Error48moonbitlang_2fcore_2fbuiltin_2eFailure_2eFailure*)_M0L3errS1968;
      moonbit_string_t _M0L4_2aeS1974 = _M0L10_2aFailureS1973->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1974);
      _M0L1eS1972 = _M0L4_2aeS1974;
      goto join_1971;
      break;
    }
    
    case 2: {
      struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError* _M0L15_2aInspectErrorS1975 =
        (struct _M0DTPC15error5Error58moonbitlang_2fcore_2fbuiltin_2eInspectError_2eInspectError*)_M0L3errS1968;
      moonbit_string_t _M0L4_2aeS1976 = _M0L15_2aInspectErrorS1975->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1976);
      _M0L1eS1972 = _M0L4_2aeS1976;
      goto join_1971;
      break;
    }
    
    case 3: {
      struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError* _M0L16_2aSnapshotErrorS1977 =
        (struct _M0DTPC15error5Error60moonbitlang_2fcore_2fbuiltin_2eSnapshotError_2eSnapshotError*)_M0L3errS1968;
      moonbit_string_t _M0L4_2aeS1978 = _M0L16_2aSnapshotErrorS1977->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1978);
      _M0L1eS1972 = _M0L4_2aeS1978;
      goto join_1971;
      break;
    }
    
    case 4: {
      struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError* _M0L35_2aMoonBitTestDriverInternalJsErrorS1979 =
        (struct _M0DTPC15error5Error128RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2eMoonBitTestDriverInternalJsError_2eMoonBitTestDriverInternalJsError*)_M0L3errS1968;
      moonbit_string_t _M0L4_2aeS1980 =
        _M0L35_2aMoonBitTestDriverInternalJsErrorS1979->$0;
      moonbit_incref_cycle_free(_M0L4_2aeS1980);
      _M0L1eS1972 = _M0L4_2aeS1980;
      goto join_1971;
      break;
    }
    default: {
      moonbit_incref_cycle_free(_M0L3errS1968);
      _M0L1eS1970 = _M0L3errS1968;
      goto join_1969;
      break;
    }
  }
  join_1971:;
  return _M0L1eS1972;
  join_1969:;
  #line 600 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _result_5755 = _M0FP15Error10to__string(_M0L1eS1970);
  moonbit_decref_cycle_free(_M0L1eS1970);
  return _result_5755;
}

int32_t _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test44moonbit__test__driver__internal__do__executeN14handle__resultS1960(
  struct _M0TWssbEu* _M0L6_2aenvS5517,
  moonbit_string_t _M0L10__testnameS1961,
  moonbit_string_t _M0L7messageS1962,
  int32_t _M0L7skippedS1963
) {
  struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1960* _M0L14_2acasted__envS5518;
  moonbit_string_t _M0L8filenameS1957;
  int32_t _M0L5indexS1959;
  moonbit_string_t _M0L10file__nameS1964;
  moonbit_string_t _M0L7messageS1965;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1966;
  moonbit_string_t _M0L6_2atmpS5519;
  #line 579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS5518
  = (struct _M0R132_24RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__result_7c1960*)_M0L6_2aenvS5517;
  _M0L8filenameS1957 = _M0L14_2acasted__envS5518->$1;
  _M0L5indexS1959 = _M0L14_2acasted__envS5518->$0;
  if (!_M0L7skippedS1963 || 0) {
    
  }
  #line 585 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1964
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1957, 1);
  #line 586 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0L7messageS1965
  = _M0MPC16string6String14escape_2einner(_M0L7messageS1962, 1);
  #line 587 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1966
  = _M0MPB13StringBuilder21StringBuilder_2einner(45);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1966, (moonbit_string_t)moonbit_string_literal_3.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1966, _M0L10file__nameS1964);
  moonbit_decref_cycle_free(_M0L10file__nameS1964);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1966, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1966, _M0L5indexS1959);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1966, (moonbit_string_t)moonbit_string_literal_5.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1966, _M0L7messageS1965);
  moonbit_decref_cycle_free(_M0L7messageS1965);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1966, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 589 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5519
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1966);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1966);
  #line 588 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS5519);
  moonbit_decref_cycle_free(_M0L6_2atmpS5519);
  #line 591 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

int32_t _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test44moonbit__test__driver__internal__do__executeN13handle__startS1955(
  struct _M0TWEu* _M0L6_2aenvS5514
) {
  struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1955* _M0L14_2acasted__envS5515;
  moonbit_string_t _M0L8filenameS1957;
  int32_t _M0L5indexS1959;
  moonbit_string_t _M0L10file__nameS1956;
  struct _M0TPB13StringBuilder* _M0L18_2astring__builderS1958;
  moonbit_string_t _M0L6_2atmpS5516;
  #line 570 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0L14_2acasted__envS5515
  = (struct _M0R131_24RiantR_2fsnn__mbt_2fexamples_2ftimed__stim__blackbox__test_2emoonbit__test__driver__internal__do__execute_2ehandle__start_7c1955*)_M0L6_2aenvS5514;
  _M0L8filenameS1957 = _M0L14_2acasted__envS5515->$1;
  _M0L5indexS1959 = _M0L14_2acasted__envS5515->$0;
  #line 571 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0L10file__nameS1956
  = _M0MPC16string6String14escape_2einner(_M0L8filenameS1957, 1);
  #line 572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_2.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0L18_2astring__builderS1958
  = _M0MPB13StringBuilder21StringBuilder_2einner(33);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1958, (moonbit_string_t)moonbit_string_literal_8.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGsE(_M0L18_2astring__builderS1958, _M0L10file__nameS1956);
  moonbit_decref_cycle_free(_M0L10file__nameS1956);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1958, (moonbit_string_t)moonbit_string_literal_4.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS1958, _M0L5indexS1959);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS1958, (moonbit_string_t)moonbit_string_literal_6.data);
  #line 574 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5516
  = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS1958);
  moonbit_decref_cycle_free(_M0L18_2astring__builderS1958);
  #line 573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE(_M0L6_2atmpS5516);
  moonbit_decref_cycle_free(_M0L6_2atmpS5516);
  #line 576 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0FPB7printlnGsE((moonbit_string_t)moonbit_string_literal_7.data);
  return 0;
}

struct _M0TPB5ArrayGUsiEE* _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test52moonbit__test__driver__internal__native__parse__args(
  
) {
  int32_t _M0L45moonbit__test__driver__internal__parse__int__S1925;
  int32_t _M0L51moonbit__test__driver__internal__split__mbt__stringS1932;
  struct _M0TUsiE** _M0L6_2atmpS5513;
  struct _M0TPB5ArrayGUsiEE* _M0L16file__and__indexS1939;
  moonbit_string_t* _M0L9cli__argsS1940;
  moonbit_string_t _M0L6_2atmpS5512;
  moonbit_string_t _M0L6_2atmpS5511;
  struct _M0TPB5ArrayGsE* _M0L10test__argsS1941;
  int32_t _M0L7_2abindS1942;
  moonbit_string_t* _M0L7_2abindS1943;
  int32_t _M0L6_2acntS5564;
  int32_t _M0L2__S1944;
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0L45moonbit__test__driver__internal__parse__int__S1925 = 0;
  _M0L51moonbit__test__driver__internal__split__mbt__stringS1932 = 0;
  _M0L6_2atmpS5513 = (struct _M0TUsiE**)moonbit_empty_ref_array;
  _M0L16file__and__indexS1939
  = (struct _M0TPB5ArrayGUsiEE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGUsiEE));
  Moonbit_object_header(_M0L16file__and__indexS1939)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 9, 0);
  _M0L16file__and__indexS1939->$0 = _M0L6_2atmpS5513;
  _M0L16file__and__indexS1939->$1 = 0;
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0L9cli__argsS1940
  = _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test52moonbit__test__driver__internal__get__cli__args__ffi();
  if (1 < 0 || 1 >= Moonbit_array_length(_M0L9cli__argsS1940)) {
    #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
    moonbit_panic();
  }
  _M0L6_2atmpS5512 = (moonbit_string_t)_M0L9cli__argsS1940[1];
  moonbit_incref_cycle_free(_M0L6_2atmpS5512);
  moonbit_decref_cycle_free(_M0L9cli__argsS1940);
  #line 331 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5511
  = _M0MP46RiantR8snn__mbt8examples27timed__stim__blackbox__test33MoonBitTestDriverInternalOsString10to__string(_M0L6_2atmpS5512);
  moonbit_decref_cycle_free(_M0L6_2atmpS5512);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0L10test__argsS1941
  = _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1932(_M0L51moonbit__test__driver__internal__split__mbt__stringS1932, _M0L6_2atmpS5511, 47);
  moonbit_decref_cycle_free(_M0L6_2atmpS5511);
  _M0L7_2abindS1942 = _M0L10test__argsS1941->$1;
  _M0L7_2abindS1943 = _M0L10test__argsS1941->$0;
  _M0L6_2acntS5564
  = Moonbit_rc_count(Moonbit_object_header(_M0L10test__argsS1941));
  if (_M0L6_2acntS5564 > 1) {
    int32_t _M0L11_2anew__cntS5565 = _M0L6_2acntS5564 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L10test__argsS1941), _M0L11_2anew__cntS5565);
    moonbit_incref_cycle_free(_M0L7_2abindS1943);
  } else if (_M0L6_2acntS5564 == 1) {
    #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L10test__argsS1941);
  }
  _M0L2__S1944 = 0;
  while (1) {
    if (_M0L2__S1944 < _M0L7_2abindS1942) {
      moonbit_string_t _M0L3argS1945 =
        (moonbit_string_t)_M0L7_2abindS1943[_M0L2__S1944];
      struct _M0TPB5ArrayGsE* _M0L16file__and__rangeS1946;
      moonbit_string_t _M0L4fileS1947;
      moonbit_string_t _M0L5rangeS1948;
      struct _M0TPB5ArrayGsE* _M0L15start__and__endS1949;
      moonbit_string_t _M0L6_2atmpS5509;
      int32_t _M0L5startS1950;
      moonbit_string_t _M0L6_2atmpS5508;
      int32_t _M0L3endS1951;
      int32_t _M0L1iS1952;
      int32_t _M0L6_2atmpS5510;
      moonbit_incref_cycle_free(_M0L3argS1945);
      #line 335 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
      _M0L16file__and__rangeS1946
      = _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1932(_M0L51moonbit__test__driver__internal__split__mbt__stringS1932, _M0L3argS1945, 58);
      moonbit_decref_cycle_free(_M0L3argS1945);
      #line 336 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
      _M0L4fileS1947
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1946, 0);
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
      _M0L5rangeS1948
      = _M0MPC15array5Array2atGsE(_M0L16file__and__rangeS1946, 1);
      moonbit_decref_cycle_free(_M0L16file__and__rangeS1946);
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
      _M0L15start__and__endS1949
      = _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1932(_M0L51moonbit__test__driver__internal__split__mbt__stringS1932, _M0L5rangeS1948, 45);
      moonbit_decref_cycle_free(_M0L5rangeS1948);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS5509
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1949, 0);
      #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
      _M0L5startS1950
      = _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1925(_M0L45moonbit__test__driver__internal__parse__int__S1925, _M0L6_2atmpS5509);
      moonbit_decref_cycle_free(_M0L6_2atmpS5509);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
      _M0L6_2atmpS5508
      = _M0MPC15array5Array2atGsE(_M0L15start__and__endS1949, 1);
      moonbit_decref_cycle_free(_M0L15start__and__endS1949);
      #line 342 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
      _M0L3endS1951
      = _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1925(_M0L45moonbit__test__driver__internal__parse__int__S1925, _M0L6_2atmpS5508);
      moonbit_decref_cycle_free(_M0L6_2atmpS5508);
      _M0L1iS1952 = _M0L5startS1950;
      while (1) {
        if (_M0L1iS1952 < _M0L3endS1951) {
          struct _M0TUsiE* _M0L8_2atupleS5506;
          int32_t _M0L6_2atmpS5507;
          moonbit_incref_cycle_free(_M0L4fileS1947);
          _M0L8_2atupleS5506
          = (struct _M0TUsiE*)moonbit_malloc(sizeof(struct _M0TUsiE));
          Moonbit_object_header(_M0L8_2atupleS5506)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 12, 0);
          _M0L8_2atupleS5506->$0 = _M0L4fileS1947;
          _M0L8_2atupleS5506->$1 = _M0L1iS1952;
          #line 344 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
          _M0MPC15array5Array4pushGUsiEE(_M0L16file__and__indexS1939, _M0L8_2atupleS5506);
          _M0L6_2atmpS5507 = _M0L1iS1952 + 1;
          _M0L1iS1952 = _M0L6_2atmpS5507;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L4fileS1947);
        }
        break;
      }
      _M0L6_2atmpS5510 = _M0L2__S1944 + 1;
      _M0L2__S1944 = _M0L6_2atmpS5510;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1943);
    }
    break;
  }
  return _M0L16file__and__indexS1939;
}

struct _M0TPB5ArrayGsE* _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test52moonbit__test__driver__internal__native__parse__argsN51moonbit__test__driver__internal__split__mbt__stringS1932(
  int32_t _M0L6_2aenvS5487,
  moonbit_string_t _M0L1sS1933,
  int32_t _M0L3sepS1934
) {
  moonbit_string_t* _M0L6_2atmpS5505;
  struct _M0TPB5ArrayGsE* _M0L3resS1935;
  struct _M0TPB8MutLocalGiE* _M0L1iS1936;
  struct _M0TPB8MutLocalGiE* _M0L5startS1937;
  int32_t _M0L3valS5500;
  int32_t _M0L6_2atmpS5501;
  #line 308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0L6_2atmpS5505 = (moonbit_string_t*)moonbit_empty_ref_array;
  _M0L3resS1935
  = (struct _M0TPB5ArrayGsE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGsE));
  Moonbit_object_header(_M0L3resS1935)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 15, 0);
  _M0L3resS1935->$0 = _M0L6_2atmpS5505;
  _M0L3resS1935->$1 = 0;
  _M0L1iS1936
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1936)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1936->$0 = 0;
  _M0L5startS1937
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5startS1937)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5startS1937->$0 = 0;
  while (1) {
    int32_t _M0L3valS5488 = _M0L1iS1936->$0;
    int32_t _M0L6_2atmpS5489 = Moonbit_array_length(_M0L1sS1933);
    if (_M0L3valS5488 < _M0L6_2atmpS5489) {
      int32_t _M0L3valS5492 = _M0L1iS1936->$0;
      int32_t _M0L6_2atmpS5491;
      int32_t _M0L6_2atmpS5490;
      int32_t _M0L3valS5499;
      int32_t _M0L6_2atmpS5498;
      if (
        _M0L3valS5492 < 0
        || _M0L3valS5492 >= Moonbit_array_length(_M0L1sS1933)
      ) {
        #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS5491 = _M0L1sS1933[_M0L3valS5492];
      _M0L6_2atmpS5490 = _M0L6_2atmpS5491;
      if (_M0L6_2atmpS5490 == _M0L3sepS1934) {
        int32_t _M0L3valS5494 = _M0L5startS1937->$0;
        int32_t _M0L3valS5495 = _M0L1iS1936->$0;
        moonbit_string_t _M0L6_2atmpS5493;
        int32_t _M0L3valS5497;
        int32_t _M0L6_2atmpS5496;
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
        _M0L6_2atmpS5493
        = _M0MPC16string6String17unsafe__substring(_M0L1sS1933, _M0L3valS5494, _M0L3valS5495);
        #line 317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
        _M0MPC15array5Array4pushGsE(_M0L3resS1935, _M0L6_2atmpS5493);
        _M0L3valS5497 = _M0L1iS1936->$0;
        _M0L6_2atmpS5496 = _M0L3valS5497 + 1;
        _M0L5startS1937->$0 = _M0L6_2atmpS5496;
      }
      _M0L3valS5499 = _M0L1iS1936->$0;
      _M0L6_2atmpS5498 = _M0L3valS5499 + 1;
      _M0L1iS1936->$0 = _M0L6_2atmpS5498;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1936);
    }
    break;
  }
  _M0L3valS5500 = _M0L5startS1937->$0;
  _M0L6_2atmpS5501 = Moonbit_array_length(_M0L1sS1933);
  if (_M0L3valS5500 < _M0L6_2atmpS5501) {
    int32_t _M0L3valS5503 = _M0L5startS1937->$0;
    int32_t _M0L6_2atmpS5504;
    moonbit_string_t _M0L6_2atmpS5502;
    moonbit_decref_cycle_free(_M0L5startS1937);
    _M0L6_2atmpS5504 = Moonbit_array_length(_M0L1sS1933);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
    _M0L6_2atmpS5502
    = _M0MPC16string6String17unsafe__substring(_M0L1sS1933, _M0L3valS5503, _M0L6_2atmpS5504);
    #line 323 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
    _M0MPC15array5Array4pushGsE(_M0L3resS1935, _M0L6_2atmpS5502);
  } else {
    moonbit_decref_cycle_free(_M0L5startS1937);
  }
  return _M0L3resS1935;
}

int32_t _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test52moonbit__test__driver__internal__native__parse__argsN45moonbit__test__driver__internal__parse__int__S1925(
  int32_t _M0L6_2aenvS5480,
  moonbit_string_t _M0L1sS1926
) {
  struct _M0TPB8MutLocalGiE* _M0L3resS1927;
  int32_t _M0L3lenS1928;
  int32_t _M0L7_2abindS1929;
  int32_t _M0L1iS1930;
  int32_t _result_5760;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0L3resS1927
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3resS1927)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3resS1927->$0 = 0;
  _M0L3lenS1928 = Moonbit_array_length(_M0L1sS1926);
  _M0L7_2abindS1929 = 0;
  _M0L1iS1930 = _M0L7_2abindS1929;
  while (1) {
    if (_M0L1iS1930 < _M0L3lenS1928) {
      int32_t _M0L3valS5485 = _M0L3resS1927->$0;
      int32_t _M0L6_2atmpS5482 = _M0L3valS5485 * 10;
      int32_t _M0L6_2atmpS5484;
      int32_t _M0L6_2atmpS5483;
      int32_t _M0L6_2atmpS5481;
      int32_t _M0L6_2atmpS5486;
      if (
        _M0L1iS1930 < 0 || _M0L1iS1930 >= Moonbit_array_length(_M0L1sS1926)
      ) {
        #line 303 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS5484 = _M0L1sS1926[_M0L1iS1930];
      _M0L6_2atmpS5483 = _M0L6_2atmpS5484 - 48;
      _M0L6_2atmpS5481 = _M0L6_2atmpS5482 + _M0L6_2atmpS5483;
      _M0L3resS1927->$0 = _M0L6_2atmpS5481;
      _M0L6_2atmpS5486 = _M0L1iS1930 + 1;
      _M0L1iS1930 = _M0L6_2atmpS5486;
      continue;
    }
    break;
  }
  _result_5760 = _M0L3resS1927->$0;
  moonbit_decref_cycle_free(_M0L3resS1927);
  return _result_5760;
}

moonbit_string_t _M0MP46RiantR8snn__mbt8examples27timed__stim__blackbox__test33MoonBitTestDriverInternalOsString10to__string(
  moonbit_string_t _M0L4selfS1924
) {
  #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  moonbit_incref_cycle_free(_M0L4selfS1924);
  return _M0L4selfS1924;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test41MoonBit__Test__Driver__Internal__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1894,
  moonbit_string_t _M0L12_2adiscard__S1895,
  int32_t _M0L12_2adiscard__S1896,
  struct _M0TWEu* _M0L12_2adiscard__S1897,
  struct _M0TWssbEu* _M0L12_2adiscard__S1898,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1899
) {
  struct moonbit_result_0 _result_5761;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _result_5761.tag = 1;
  _result_5761.data.ok = 0;
  return _result_5761;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test43MoonBit__Test__Driver__Internal__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1900,
  moonbit_string_t _M0L12_2adiscard__S1901,
  int32_t _M0L12_2adiscard__S1902,
  struct _M0TWEu* _M0L12_2adiscard__S1903,
  struct _M0TWssbEu* _M0L12_2adiscard__S1904,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1905
) {
  struct moonbit_result_0 _result_5762;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _result_5762.tag = 1;
  _result_5762.data.ok = 0;
  return _result_5762;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test48MoonBit__Test__Driver__Internal__Async__No__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1906,
  moonbit_string_t _M0L12_2adiscard__S1907,
  int32_t _M0L12_2adiscard__S1908,
  struct _M0TWEu* _M0L12_2adiscard__S1909,
  struct _M0TWssbEu* _M0L12_2adiscard__S1910,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1911
) {
  struct moonbit_result_0 _result_5763;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _result_5763.tag = 1;
  _result_5763.data.ok = 0;
  return _result_5763;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test50MoonBit__Test__Driver__Internal__Async__With__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1912,
  moonbit_string_t _M0L12_2adiscard__S1913,
  int32_t _M0L12_2adiscard__S1914,
  struct _M0TWEu* _M0L12_2adiscard__S1915,
  struct _M0TWssbEu* _M0L12_2adiscard__S1916,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1917
) {
  struct moonbit_result_0 _result_5764;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _result_5764.tag = 1;
  _result_5764.data.ok = 0;
  return _result_5764;
}

struct moonbit_result_0 _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test21MoonBit__Test__Driver9run__testGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test50MoonBit__Test__Driver__Internal__With__Bench__ArgsE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1918,
  moonbit_string_t _M0L12_2adiscard__S1919,
  int32_t _M0L12_2adiscard__S1920,
  struct _M0TWEu* _M0L12_2adiscard__S1921,
  struct _M0TWssbEu* _M0L12_2adiscard__S1922,
  struct _M0TWRPC15error5ErrorEs* _M0L12_2adiscard__S1923
) {
  struct moonbit_result_0 _result_5765;
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _result_5765.tag = 1;
  _result_5765.data.ok = 0;
  return _result_5765;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test28MoonBit__Async__Test__Driver20is__being__cancelledGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  
) {
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test34MoonBit__Async__Test__Driver__ImplE(
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12_2adiscard__S1893
) {
  #line 12 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23heterogeneous__sim__for(
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0L1mS1881,
  float _M0L8durationS1878
) {
  float _M0L2dtS1876;
  float _M0L6_2atmpS5479;
  int32_t _M0L5stepsS1877;
  int32_t _M0L7_2abindS1879;
  int32_t _M0L2__S1880;
  #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L2dtS1876 = 0x1p-3f;
  _M0L6_2atmpS5479 = _M0L8durationS1878 / _M0L2dtS1876;
  #line 273 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L5stepsS1877 = _M0MPC15float5Float7to__int(_M0L6_2atmpS5479);
  _M0L7_2abindS1879 = 0;
  _M0L2__S1880 = _M0L7_2abindS1879;
  while (1) {
    if (_M0L2__S1880 < _M0L5stepsS1877) {
      int32_t _M0L6_2atmpS5478;
      #line 275 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt19step__heterogeneous(_M0L1mS1881, _M0L2dtS1876);
      _M0L6_2atmpS5478 = _M0L2__S1880 + 1;
      _M0L2__S1880 = _M0L6_2atmpS5478;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt19step__heterogeneous(
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0L1mS1778,
  float _M0L2dtS1783
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L7_2abindS1777;
  int32_t _M0L7_2abindS1779;
  void** _M0L7_2abindS1780;
  int32_t _M0L2__S1781;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS1785;
  int32_t _M0L7_2abindS1786;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS1787;
  int32_t _M0L2__S1788;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L7_2abindS1791;
  int32_t _M0L7_2abindS1792;
  void** _M0L7_2abindS1793;
  int32_t _M0L2__S1794;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L7_2abindS1812;
  int32_t _M0L7_2abindS1813;
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L7_2abindS1814;
  int32_t _M0L2__S1815;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L7_2abindS1818;
  int32_t _M0L7_2abindS1819;
  void** _M0L7_2abindS1820;
  int32_t _M0L2__S1821;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L7_2abindS1864;
  int32_t _M0L7_2abindS1865;
  void** _M0L7_2abindS1866;
  int32_t _M0L2__S1867;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2abindS1870;
  int32_t _M0L7_2abindS1871;
  struct _M0TP26RiantR8snn__mbt7Monitor** _M0L7_2abindS1872;
  int32_t _M0L2__S1873;
  struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5477;
  #line 100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L7_2abindS1777 = _M0L1mS1778->$2;
  _M0L7_2abindS1779 = _M0L7_2abindS1777->$1;
  _M0L7_2abindS1780 = _M0L7_2abindS1777->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1780);
  _M0L2__S1781 = 0;
  while (1) {
    if (_M0L2__S1781 < _M0L7_2abindS1779) {
      void* _M0L1sS1782 = (void*)_M0L7_2abindS1780[_M0L2__S1781];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5286 = _M0L1mS1778->$3;
      int32_t _M0L6_2atmpS5287;
      moonbit_incref_cycle_free(_M0L1sS1782);
      #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt14stimulate__any(_M0L1sS1782, _M0L4timeS5286, _M0L2dtS1783);
      moonbit_decref_cycle_free(_M0L1sS1782);
      _M0L6_2atmpS5287 = _M0L2__S1781 + 1;
      _M0L2__S1781 = _M0L6_2atmpS5287;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1780);
    }
    break;
  }
  _M0L7_2abindS1785 = _M0L1mS1778->$1;
  _M0L7_2abindS1786 = _M0L7_2abindS1785->$1;
  _M0L7_2abindS1787 = _M0L7_2abindS1785->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1787);
  _M0L2__S1788 = 0;
  while (1) {
    if (_M0L2__S1788 < _M0L7_2abindS1786) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1789 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS1787[
          _M0L2__S1788
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5289 = _M0L1mS1778->$3;
      float _M0L6_2atmpS5288;
      int32_t _M0L6_2atmpS5290;
      moonbit_incref_cycle_free(_M0L1cS1789);
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5288 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5289);
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt25deliver__pending__synapse(_M0L1cS1789, _M0L6_2atmpS5288);
      moonbit_decref_cycle_free(_M0L1cS1789);
      _M0L6_2atmpS5290 = _M0L2__S1788 + 1;
      _M0L2__S1788 = _M0L6_2atmpS5290;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1787);
    }
    break;
  }
  _M0L7_2abindS1791 = _M0L1mS1778->$6;
  _M0L7_2abindS1792 = _M0L7_2abindS1791->$1;
  _M0L7_2abindS1793 = _M0L7_2abindS1791->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1793);
  _M0L2__S1794 = 0;
  while (1) {
    if (_M0L2__S1794 < _M0L7_2abindS1792) {
      void* _M0L5entryS1795 = (void*)_M0L7_2abindS1793[_M0L2__S1794];
      struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _M0L1eS1797;
      struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet* _M0L1eS1800;
      struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry* _M0L1eS1803;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5307;
      int32_t _M0L11conn__indexS5308;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1804;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS5303;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter* _M0L5paramS5304;
      int32_t _M0L6_2acntS5570;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5306;
      float _M0L6_2atmpS5305;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5301;
      int32_t _M0L11conn__indexS5302;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1801;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS5297;
      struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet* _M0L5paramS5298;
      int32_t _M0L6_2acntS5568;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5300;
      float _M0L6_2atmpS5299;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5295;
      int32_t _M0L11conn__indexS5296;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1798;
      struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS5291;
      struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0L5paramS5292;
      int32_t _M0L6_2acntS5566;
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5294;
      float _M0L6_2atmpS5293;
      int32_t _M0L6_2atmpS5309;
      switch (Moonbit_object_tag(_M0L5entryS1795)) {
        case 0: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__* _M0L15_2aMarkramSTP__S1805 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind12MarkramSTP__*)_M0L5entryS1795;
          struct _M0TP26RiantR8snn__mbt15MarkramSTPEntry* _M0L4_2aeS1806 =
            _M0L15_2aMarkramSTP__S1805->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1806);
          _M0L1eS1803 = _M0L4_2aeS1806;
          goto join_1802;
          break;
        }
        
        case 1: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__* _M0L18_2aMarkramSTPHet__S1807 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind15MarkramSTPHet__*)_M0L5entryS1795;
          struct _M0TP26RiantR8snn__mbt18MarkramSTPEntryHet* _M0L4_2aeS1808 =
            _M0L18_2aMarkramSTPHet__S1807->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1808);
          _M0L1eS1800 = _M0L4_2aeS1808;
          goto join_1799;
          break;
        }
        default: {
          struct _M0DTP26RiantR8snn__mbt12STPEntryKind20MarkramSTPTimestep__* _M0L23_2aMarkramSTPTimestep__S1809 =
            (struct _M0DTP26RiantR8snn__mbt12STPEntryKind20MarkramSTPTimestep__*)_M0L5entryS1795;
          struct _M0TP26RiantR8snn__mbt23MarkramSTPEntryTimestep* _M0L4_2aeS1810 =
            _M0L23_2aMarkramSTPTimestep__S1809->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1810);
          _M0L1eS1797 = _M0L4_2aeS1810;
          goto join_1796;
          break;
        }
      }
      goto joinlet_5772;
      join_1802:;
      _M0L5connsS5307 = _M0L1mS1778->$1;
      _M0L11conn__indexS5308 = _M0L1eS1803->$0;
      #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1804
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5307, _M0L11conn__indexS5308);
      _M0L4varsS5303 = _M0L1eS1803->$1;
      _M0L5paramS5304 = _M0L1eS1803->$2;
      _M0L6_2acntS5570 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1803));
      if (_M0L6_2acntS5570 > 1) {
        int32_t _M0L11_2anew__cntS5571 = _M0L6_2acntS5570 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1803), _M0L11_2anew__cntS5571);
        moonbit_incref_cycle_free(_M0L5paramS5304);
        moonbit_incref_cycle_free(_M0L4varsS5303);
      } else if (_M0L6_2acntS5570 == 1) {
        #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1803);
      }
      _M0L4timeS5306 = _M0L1mS1778->$3;
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5305 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5306);
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt18markram__stp__step(_M0L3synS1804, _M0L4varsS5303, _M0L5paramS5304, _M0L6_2atmpS5305);
      moonbit_decref_cycle_free(_M0L3synS1804);
      moonbit_decref_cycle_free(_M0L4varsS5303);
      moonbit_decref_cycle_free(_M0L5paramS5304);
      joinlet_5772:;
      goto joinlet_5771;
      join_1799:;
      _M0L5connsS5301 = _M0L1mS1778->$1;
      _M0L11conn__indexS5302 = _M0L1eS1800->$0;
      #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1801
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5301, _M0L11conn__indexS5302);
      _M0L4varsS5297 = _M0L1eS1800->$1;
      _M0L5paramS5298 = _M0L1eS1800->$2;
      _M0L6_2acntS5568 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1800));
      if (_M0L6_2acntS5568 > 1) {
        int32_t _M0L11_2anew__cntS5569 = _M0L6_2acntS5568 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1800), _M0L11_2anew__cntS5569);
        moonbit_incref_cycle_free(_M0L5paramS5298);
        moonbit_incref_cycle_free(_M0L4varsS5297);
      } else if (_M0L6_2acntS5568 == 1) {
        #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1800);
      }
      _M0L4timeS5300 = _M0L1mS1778->$3;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5299 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5300);
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt23markram__stp__step__het(_M0L3synS1801, _M0L4varsS5297, _M0L5paramS5298, _M0L6_2atmpS5299);
      moonbit_decref_cycle_free(_M0L3synS1801);
      moonbit_decref_cycle_free(_M0L4varsS5297);
      moonbit_decref_cycle_free(_M0L5paramS5298);
      joinlet_5771:;
      goto joinlet_5770;
      join_1796:;
      _M0L5connsS5295 = _M0L1mS1778->$1;
      _M0L11conn__indexS5296 = _M0L1eS1797->$0;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1798
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5295, _M0L11conn__indexS5296);
      _M0L4varsS5291 = _M0L1eS1797->$1;
      _M0L5paramS5292 = _M0L1eS1797->$2;
      _M0L6_2acntS5566 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1797));
      if (_M0L6_2acntS5566 > 1) {
        int32_t _M0L11_2anew__cntS5567 = _M0L6_2acntS5566 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1797), _M0L11_2anew__cntS5567);
        moonbit_incref_cycle_free(_M0L5paramS5292);
        moonbit_incref_cycle_free(_M0L4varsS5291);
      } else if (_M0L6_2acntS5566 == 1) {
        #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1797);
      }
      _M0L4timeS5294 = _M0L1mS1778->$3;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5293 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5294);
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt28markram__stp__step__timestep(_M0L3synS1798, _M0L4varsS5291, _M0L5paramS5292, _M0L6_2atmpS5293, _M0L2dtS1783);
      moonbit_decref_cycle_free(_M0L3synS1798);
      moonbit_decref_cycle_free(_M0L4varsS5291);
      moonbit_decref_cycle_free(_M0L5paramS5292);
      joinlet_5770:;
      _M0L6_2atmpS5309 = _M0L2__S1794 + 1;
      _M0L2__S1794 = _M0L6_2atmpS5309;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1793);
    }
    break;
  }
  _M0L7_2abindS1812 = _M0L1mS1778->$1;
  _M0L7_2abindS1813 = _M0L7_2abindS1812->$1;
  _M0L7_2abindS1814 = _M0L7_2abindS1812->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1814);
  _M0L2__S1815 = 0;
  while (1) {
    if (_M0L2__S1815 < _M0L7_2abindS1813) {
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1816 =
        (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L7_2abindS1814[
          _M0L2__S1815
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5311 = _M0L1mS1778->$3;
      float _M0L6_2atmpS5310;
      int32_t _M0L6_2atmpS5312;
      moonbit_incref_cycle_free(_M0L1cS1816);
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5310 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5311);
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt16forward__synapse(_M0L1cS1816, _M0L6_2atmpS5310);
      moonbit_decref_cycle_free(_M0L1cS1816);
      _M0L6_2atmpS5312 = _M0L2__S1815 + 1;
      _M0L2__S1815 = _M0L6_2atmpS5312;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1814);
    }
    break;
  }
  _M0L7_2abindS1818 = _M0L1mS1778->$5;
  _M0L7_2abindS1819 = _M0L7_2abindS1818->$1;
  _M0L7_2abindS1820 = _M0L7_2abindS1818->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1820);
  _M0L2__S1821 = 0;
  while (1) {
    if (_M0L2__S1821 < _M0L7_2abindS1819) {
      void* _M0L5entryS1822 = (void*)_M0L7_2abindS1820[_M0L2__S1821];
      struct _M0TP26RiantR8snn__mbt17CaPlasticityEntry* _M0L1eS1824;
      struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric* _M0L1eS1827;
      struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0L1eS1830;
      struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0L1eS1833;
      struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025* _M0L1eS1836;
      struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric* _M0L1eS1839;
      struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat* _M0L1eS1842;
      struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0L1eS1845;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5470;
      int32_t _M0L11conn__indexS5471;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1846;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5465;
      struct _M0TPB5ArrayGfE* _M0L4valsS5452;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5464;
      struct _M0TPB5ArrayGbE* _M0L4fireS5453;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5463;
      struct _M0TPB5ArrayGbE* _M0L4fireS5454;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5462;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5455;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5461;
      int32_t _M0L6_2acntS5719;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5456;
      int32_t _M0L6_2acntS5730;
      struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS5457;
      struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS5458;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5460;
      float _M0L6_2atmpS5459;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5466;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5469;
      int32_t _M0L6_2acntS5734;
      float _M0L6_2atmpS5468;
      float _M0L6_2atmpS5467;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5450;
      int32_t _M0L11conn__indexS5451;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1843;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5445;
      struct _M0TPB5ArrayGfE* _M0L4valsS5433;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5444;
      struct _M0TPB5ArrayGbE* _M0L4fireS5434;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5443;
      struct _M0TPB5ArrayGbE* _M0L4fireS5435;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5442;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5436;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5441;
      int32_t _M0L6_2acntS5699;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5437;
      int32_t _M0L6_2acntS5710;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5438;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5439;
      struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L5paramS5440;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5446;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5449;
      int32_t _M0L6_2acntS5714;
      float _M0L6_2atmpS5448;
      float _M0L6_2atmpS5447;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5431;
      int32_t _M0L11conn__indexS5432;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1840;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5426;
      struct _M0TPB5ArrayGfE* _M0L4valsS5415;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5425;
      struct _M0TPB5ArrayGbE* _M0L4fireS5416;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5424;
      struct _M0TPB5ArrayGbE* _M0L4fireS5417;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5423;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5418;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5422;
      int32_t _M0L6_2acntS5680;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5419;
      int32_t _M0L6_2acntS5691;
      struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L4varsS5420;
      struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L5paramS5421;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5427;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5430;
      int32_t _M0L6_2acntS5695;
      float _M0L6_2atmpS5429;
      float _M0L6_2atmpS5428;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5413;
      int32_t _M0L11conn__indexS5414;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1837;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5408;
      struct _M0TPB5ArrayGfE* _M0L4valsS5395;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5407;
      struct _M0TPB5ArrayGbE* _M0L4fireS5396;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5406;
      struct _M0TPB5ArrayGbE* _M0L4fireS5397;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5405;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5398;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5404;
      int32_t _M0L6_2acntS5661;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5399;
      int32_t _M0L6_2acntS5672;
      struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS5400;
      struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L5paramS5401;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5403;
      float _M0L6_2atmpS5402;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5409;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5412;
      int32_t _M0L6_2acntS5676;
      float _M0L6_2atmpS5411;
      float _M0L6_2atmpS5410;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5393;
      int32_t _M0L11conn__indexS5394;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1834;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5388;
      struct _M0TPB5ArrayGfE* _M0L4valsS5375;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5387;
      struct _M0TPB5ArrayGbE* _M0L4fireS5376;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5386;
      struct _M0TPB5ArrayGbE* _M0L4fireS5377;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5385;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5378;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5384;
      int32_t _M0L6_2acntS5642;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5379;
      int32_t _M0L6_2acntS5653;
      struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L4varsS5380;
      struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS5381;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5383;
      float _M0L6_2atmpS5382;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5389;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5392;
      int32_t _M0L6_2acntS5657;
      float _M0L6_2atmpS5391;
      float _M0L6_2atmpS5390;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5373;
      int32_t _M0L11conn__indexS5374;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1831;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5368;
      struct _M0TPB5ArrayGfE* _M0L4valsS5353;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5367;
      struct _M0TPB5ArrayGbE* _M0L4fireS5354;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5366;
      struct _M0TPB5ArrayGbE* _M0L4fireS5355;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5365;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5356;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5364;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5357;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5363;
      int32_t _M0L6_2acntS5610;
      struct _M0TPB5ArrayGfE* _M0L1vS5358;
      int32_t _M0L6_2acntS5621;
      struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L4varsS5359;
      struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS5360;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5362;
      float _M0L6_2atmpS5361;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5369;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5372;
      int32_t _M0L6_2acntS5638;
      float _M0L6_2atmpS5371;
      float _M0L6_2atmpS5370;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5351;
      int32_t _M0L11conn__indexS5352;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1828;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5346;
      struct _M0TPB5ArrayGfE* _M0L4valsS5333;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5345;
      struct _M0TPB5ArrayGbE* _M0L4fireS5334;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5344;
      struct _M0TPB5ArrayGbE* _M0L4fireS5335;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5343;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5336;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5342;
      int32_t _M0L6_2acntS5591;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5337;
      int32_t _M0L6_2acntS5602;
      struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L4varsS5338;
      struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L5paramS5339;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5341;
      float _M0L6_2atmpS5340;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5347;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5350;
      int32_t _M0L6_2acntS5606;
      float _M0L6_2atmpS5349;
      float _M0L6_2atmpS5348;
      struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS5331;
      int32_t _M0L11conn__indexS5332;
      struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS1825;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5326;
      struct _M0TPB5ArrayGfE* _M0L4valsS5313;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS5325;
      struct _M0TPB5ArrayGbE* _M0L4fireS5314;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS5324;
      struct _M0TPB5ArrayGbE* _M0L4fireS5315;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5323;
      struct _M0TPB5ArrayGiE* _M0L6colptrS5316;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS5322;
      int32_t _M0L6_2acntS5572;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS5317;
      int32_t _M0L6_2acntS5583;
      struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L4varsS5318;
      struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L5paramS5319;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5321;
      float _M0L6_2atmpS5320;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5327;
      struct _M0TPB5ArrayGfE* _M0L6t__nowS5330;
      int32_t _M0L6_2acntS5587;
      float _M0L6_2atmpS5329;
      float _M0L6_2atmpS5328;
      int32_t _M0L6_2atmpS5472;
      switch (Moonbit_object_tag(_M0L5entryS1822)) {
        case 0: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__* _M0L13_2aGerstner__S1847 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind10Gerstner__*)_M0L5entryS1822;
          struct _M0TP26RiantR8snn__mbt9STDPEntry* _M0L4_2aeS1848 =
            _M0L13_2aGerstner__S1847->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1848);
          _M0L1eS1845 = _M0L4_2aeS1848;
          goto join_1844;
          break;
        }
        
        case 1: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind12MexicanHat__* _M0L15_2aMexicanHat__S1849 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind12MexicanHat__*)_M0L5entryS1822;
          struct _M0TP26RiantR8snn__mbt19STDPEntryMexicanHat* _M0L4_2aeS1850 =
            _M0L15_2aMexicanHat__S1849->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1850);
          _M0L1eS1842 = _M0L4_2aeS1850;
          goto join_1841;
          break;
        }
        
        case 2: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind15AntiSymmetric__* _M0L18_2aAntiSymmetric__S1851 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind15AntiSymmetric__*)_M0L5entryS1822;
          struct _M0TP26RiantR8snn__mbt22STDPEntryAntiSymmetric* _M0L4_2aeS1852 =
            _M0L18_2aAntiSymmetric__S1851->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1852);
          _M0L1eS1839 = _M0L4_2aeS1852;
          goto join_1838;
          break;
        }
        
        case 3: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16Confavreux2025__* _M0L19_2aConfavreux2025__S1853 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16Confavreux2025__*)_M0L5entryS1822;
          struct _M0TP26RiantR8snn__mbt23STDPEntryConfavreux2025* _M0L4_2aeS1854 =
            _M0L19_2aConfavreux2025__S1853->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1854);
          _M0L1eS1836 = _M0L4_2aeS1854;
          goto join_1835;
          break;
        }
        
        case 4: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__* _M0L14_2aIstdpRate__S1855 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11IstdpRate__*)_M0L5entryS1822;
          struct _M0TP26RiantR8snn__mbt14IstdpRateEntry* _M0L4_2aeS1856 =
            _M0L14_2aIstdpRate__S1855->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1856);
          _M0L1eS1833 = _M0L4_2aeS1856;
          goto join_1832;
          break;
        }
        
        case 5: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16IstdpPotential__* _M0L19_2aIstdpPotential__S1857 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind16IstdpPotential__*)_M0L5entryS1822;
          struct _M0TP26RiantR8snn__mbt19IstdpPotentialEntry* _M0L4_2aeS1858 =
            _M0L19_2aIstdpPotential__S1857->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1858);
          _M0L1eS1830 = _M0L4_2aeS1858;
          goto join_1829;
          break;
        }
        
        case 6: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11Symmetric__* _M0L14_2aSymmetric__S1859 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind11Symmetric__*)_M0L5entryS1822;
          struct _M0TP26RiantR8snn__mbt18STDPEntrySymmetric* _M0L4_2aeS1860 =
            _M0L14_2aSymmetric__S1859->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1860);
          _M0L1eS1827 = _M0L4_2aeS1860;
          goto join_1826;
          break;
        }
        default: {
          struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__* _M0L17_2aCaPlasticity__S1861 =
            (struct _M0DTP26RiantR8snn__mbt13STDPEntryKind14CaPlasticity__*)_M0L5entryS1822;
          struct _M0TP26RiantR8snn__mbt17CaPlasticityEntry* _M0L4_2aeS1862 =
            _M0L17_2aCaPlasticity__S1861->$0;
          moonbit_incref_cycle_free(_M0L4_2aeS1862);
          _M0L1eS1824 = _M0L4_2aeS1862;
          goto join_1823;
          break;
        }
      }
      goto joinlet_5782;
      join_1844:;
      _M0L5connsS5470 = _M0L1mS1778->$1;
      _M0L11conn__indexS5471 = _M0L1eS1845->$0;
      #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1846
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5470, _M0L11conn__indexS5471);
      _M0L6matrixS5465 = _M0L3synS1846->$4;
      _M0L4valsS5452 = _M0L6matrixS5465->$4;
      _M0L3preS5464 = _M0L3synS1846->$0;
      _M0L4fireS5453 = _M0L3preS5464->$5;
      _M0L4postS5463 = _M0L3synS1846->$1;
      _M0L4fireS5454 = _M0L4postS5463->$5;
      _M0L6matrixS5462 = _M0L3synS1846->$4;
      _M0L6colptrS5455 = _M0L6matrixS5462->$3;
      _M0L6matrixS5461 = _M0L3synS1846->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5455);
      moonbit_incref_cycle_free(_M0L4fireS5454);
      moonbit_incref_cycle_free(_M0L4fireS5453);
      moonbit_incref_cycle_free(_M0L4valsS5452);
      _M0L6_2acntS5719
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1846));
      if (_M0L6_2acntS5719 > 1) {
        int32_t _M0L11_2anew__cntS5729 = _M0L6_2acntS5719 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1846), _M0L11_2anew__cntS5729);
        moonbit_incref_cycle_free(_M0L6matrixS5461);
      } else if (_M0L6_2acntS5719 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5728 = _M0L3synS1846->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5727;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5726;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5725;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5724;
        moonbit_string_t _M0L8_2afieldS5723;
        moonbit_string_t _M0L8_2afieldS5722;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5721;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5720;
        moonbit_decref_cycle_free(_M0L8_2afieldS5728);
        _M0L8_2afieldS5727 = _M0L3synS1846->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5727);
        _M0L8_2afieldS5726 = _M0L3synS1846->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5726);
        _M0L8_2afieldS5725 = _M0L3synS1846->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5725);
        _M0L8_2afieldS5724 = _M0L3synS1846->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5724);
        _M0L8_2afieldS5723 = _M0L3synS1846->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5723);
        _M0L8_2afieldS5722 = _M0L3synS1846->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5722);
        _M0L8_2afieldS5721 = _M0L3synS1846->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5721);
        _M0L8_2afieldS5720 = _M0L3synS1846->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5720);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1846);
      }
      _M0L6rowptrS5456 = _M0L6matrixS5461->$2;
      _M0L6_2acntS5730
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5461));
      if (_M0L6_2acntS5730 > 1) {
        int32_t _M0L11_2anew__cntS5733 = _M0L6_2acntS5730 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5461), _M0L11_2anew__cntS5733);
        moonbit_incref_cycle_free(_M0L6rowptrS5456);
      } else if (_M0L6_2acntS5730 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5732 = _M0L6matrixS5461->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5731;
        moonbit_decref_cycle_free(_M0L8_2afieldS5732);
        _M0L8_2afieldS5731 = _M0L6matrixS5461->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5731);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5461);
      }
      _M0L4varsS5457 = _M0L1eS1845->$3;
      _M0L5paramS5458 = _M0L1eS1845->$4;
      _M0L6t__nowS5460 = _M0L1eS1845->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5460);
      moonbit_incref_cycle_free(_M0L5paramS5458);
      moonbit_incref_cycle_free(_M0L4varsS5457);
      #line 145 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5459 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5460, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5460);
      #line 137 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt10stdp__step(_M0L4valsS5452, _M0L4fireS5453, _M0L4fireS5454, _M0L6colptrS5455, _M0L6rowptrS5456, _M0L4varsS5457, _M0L5paramS5458, _M0L6_2atmpS5459, _M0L2dtS1783);
      moonbit_decref_cycle_free(_M0L4valsS5452);
      moonbit_decref_cycle_free(_M0L4fireS5453);
      moonbit_decref_cycle_free(_M0L4fireS5454);
      moonbit_decref_cycle_free(_M0L6colptrS5455);
      moonbit_decref_cycle_free(_M0L6rowptrS5456);
      moonbit_decref_cycle_free(_M0L4varsS5457);
      moonbit_decref_cycle_free(_M0L5paramS5458);
      _M0L6t__nowS5466 = _M0L1eS1845->$5;
      _M0L6t__nowS5469 = _M0L1eS1845->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5466);
      _M0L6_2acntS5734 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1845));
      if (_M0L6_2acntS5734 > 1) {
        int32_t _M0L11_2anew__cntS5737 = _M0L6_2acntS5734 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1845), _M0L11_2anew__cntS5737);
        moonbit_incref_cycle_free(_M0L6t__nowS5469);
      } else if (_M0L6_2acntS5734 == 1) {
        struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L8_2afieldS5736 =
          _M0L1eS1845->$4;
        struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L8_2afieldS5735;
        moonbit_decref_cycle_free(_M0L8_2afieldS5736);
        _M0L8_2afieldS5735 = _M0L1eS1845->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5735);
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1845);
      }
      #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5468 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5469, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5469);
      _M0L6_2atmpS5467 = _M0L6_2atmpS5468 + _M0L2dtS1783;
      #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5466, 0, _M0L6_2atmpS5467);
      moonbit_decref_cycle_free(_M0L6t__nowS5466);
      joinlet_5782:;
      goto joinlet_5781;
      join_1841:;
      _M0L5connsS5450 = _M0L1mS1778->$1;
      _M0L11conn__indexS5451 = _M0L1eS1842->$0;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1843
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5450, _M0L11conn__indexS5451);
      _M0L6matrixS5445 = _M0L3synS1843->$4;
      _M0L4valsS5433 = _M0L6matrixS5445->$4;
      _M0L3preS5444 = _M0L3synS1843->$0;
      _M0L4fireS5434 = _M0L3preS5444->$5;
      _M0L4postS5443 = _M0L3synS1843->$1;
      _M0L4fireS5435 = _M0L4postS5443->$5;
      _M0L6matrixS5442 = _M0L3synS1843->$4;
      _M0L6colptrS5436 = _M0L6matrixS5442->$3;
      _M0L6matrixS5441 = _M0L3synS1843->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5436);
      moonbit_incref_cycle_free(_M0L4fireS5435);
      moonbit_incref_cycle_free(_M0L4fireS5434);
      moonbit_incref_cycle_free(_M0L4valsS5433);
      _M0L6_2acntS5699
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1843));
      if (_M0L6_2acntS5699 > 1) {
        int32_t _M0L11_2anew__cntS5709 = _M0L6_2acntS5699 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1843), _M0L11_2anew__cntS5709);
        moonbit_incref_cycle_free(_M0L6matrixS5441);
      } else if (_M0L6_2acntS5699 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5708 = _M0L3synS1843->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5707;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5706;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5705;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5704;
        moonbit_string_t _M0L8_2afieldS5703;
        moonbit_string_t _M0L8_2afieldS5702;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5701;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5700;
        moonbit_decref_cycle_free(_M0L8_2afieldS5708);
        _M0L8_2afieldS5707 = _M0L3synS1843->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5707);
        _M0L8_2afieldS5706 = _M0L3synS1843->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5706);
        _M0L8_2afieldS5705 = _M0L3synS1843->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5705);
        _M0L8_2afieldS5704 = _M0L3synS1843->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5704);
        _M0L8_2afieldS5703 = _M0L3synS1843->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5703);
        _M0L8_2afieldS5702 = _M0L3synS1843->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5702);
        _M0L8_2afieldS5701 = _M0L3synS1843->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5701);
        _M0L8_2afieldS5700 = _M0L3synS1843->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5700);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1843);
      }
      _M0L6rowptrS5437 = _M0L6matrixS5441->$2;
      _M0L6_2acntS5710
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5441));
      if (_M0L6_2acntS5710 > 1) {
        int32_t _M0L11_2anew__cntS5713 = _M0L6_2acntS5710 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5441), _M0L11_2anew__cntS5713);
        moonbit_incref_cycle_free(_M0L6rowptrS5437);
      } else if (_M0L6_2acntS5710 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5712 = _M0L6matrixS5441->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5711;
        moonbit_decref_cycle_free(_M0L8_2afieldS5712);
        _M0L8_2afieldS5711 = _M0L6matrixS5441->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5711);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5441);
      }
      _M0L4tpreS5438 = _M0L1eS1842->$4;
      _M0L5tpostS5439 = _M0L1eS1842->$5;
      _M0L5paramS5440 = _M0L1eS1842->$3;
      moonbit_incref_cycle_free(_M0L5paramS5440);
      moonbit_incref_cycle_free(_M0L5tpostS5439);
      moonbit_incref_cycle_free(_M0L4tpreS5438);
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt24stdp__mexican__hat__step(_M0L4valsS5433, _M0L4fireS5434, _M0L4fireS5435, _M0L6colptrS5436, _M0L6rowptrS5437, _M0L4tpreS5438, _M0L5tpostS5439, _M0L5paramS5440, _M0L2dtS1783);
      moonbit_decref_cycle_free(_M0L4valsS5433);
      moonbit_decref_cycle_free(_M0L4fireS5434);
      moonbit_decref_cycle_free(_M0L4fireS5435);
      moonbit_decref_cycle_free(_M0L6colptrS5436);
      moonbit_decref_cycle_free(_M0L6rowptrS5437);
      moonbit_decref_cycle_free(_M0L4tpreS5438);
      moonbit_decref_cycle_free(_M0L5tpostS5439);
      moonbit_decref_cycle_free(_M0L5paramS5440);
      _M0L6t__nowS5446 = _M0L1eS1842->$6;
      _M0L6t__nowS5449 = _M0L1eS1842->$6;
      moonbit_incref_cycle_free(_M0L6t__nowS5446);
      _M0L6_2acntS5714 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1842));
      if (_M0L6_2acntS5714 > 1) {
        int32_t _M0L11_2anew__cntS5718 = _M0L6_2acntS5714 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1842), _M0L11_2anew__cntS5718);
        moonbit_incref_cycle_free(_M0L6t__nowS5449);
      } else if (_M0L6_2acntS5714 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5717 = _M0L1eS1842->$5;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5716;
        struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L8_2afieldS5715;
        moonbit_decref_cycle_free(_M0L8_2afieldS5717);
        _M0L8_2afieldS5716 = _M0L1eS1842->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS5716);
        _M0L8_2afieldS5715 = _M0L1eS1842->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5715);
        #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1842);
      }
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5448 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5449, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5449);
      _M0L6_2atmpS5447 = _M0L6_2atmpS5448 + _M0L2dtS1783;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5446, 0, _M0L6_2atmpS5447);
      moonbit_decref_cycle_free(_M0L6t__nowS5446);
      joinlet_5781:;
      goto joinlet_5780;
      join_1838:;
      _M0L5connsS5431 = _M0L1mS1778->$1;
      _M0L11conn__indexS5432 = _M0L1eS1839->$0;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1840
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5431, _M0L11conn__indexS5432);
      _M0L6matrixS5426 = _M0L3synS1840->$4;
      _M0L4valsS5415 = _M0L6matrixS5426->$4;
      _M0L3preS5425 = _M0L3synS1840->$0;
      _M0L4fireS5416 = _M0L3preS5425->$5;
      _M0L4postS5424 = _M0L3synS1840->$1;
      _M0L4fireS5417 = _M0L4postS5424->$5;
      _M0L6matrixS5423 = _M0L3synS1840->$4;
      _M0L6colptrS5418 = _M0L6matrixS5423->$3;
      _M0L6matrixS5422 = _M0L3synS1840->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5418);
      moonbit_incref_cycle_free(_M0L4fireS5417);
      moonbit_incref_cycle_free(_M0L4fireS5416);
      moonbit_incref_cycle_free(_M0L4valsS5415);
      _M0L6_2acntS5680
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1840));
      if (_M0L6_2acntS5680 > 1) {
        int32_t _M0L11_2anew__cntS5690 = _M0L6_2acntS5680 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1840), _M0L11_2anew__cntS5690);
        moonbit_incref_cycle_free(_M0L6matrixS5422);
      } else if (_M0L6_2acntS5680 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5689 = _M0L3synS1840->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5688;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5687;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5686;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5685;
        moonbit_string_t _M0L8_2afieldS5684;
        moonbit_string_t _M0L8_2afieldS5683;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5682;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5681;
        moonbit_decref_cycle_free(_M0L8_2afieldS5689);
        _M0L8_2afieldS5688 = _M0L3synS1840->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5688);
        _M0L8_2afieldS5687 = _M0L3synS1840->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5687);
        _M0L8_2afieldS5686 = _M0L3synS1840->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5686);
        _M0L8_2afieldS5685 = _M0L3synS1840->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5685);
        _M0L8_2afieldS5684 = _M0L3synS1840->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5684);
        _M0L8_2afieldS5683 = _M0L3synS1840->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5683);
        _M0L8_2afieldS5682 = _M0L3synS1840->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5682);
        _M0L8_2afieldS5681 = _M0L3synS1840->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5681);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1840);
      }
      _M0L6rowptrS5419 = _M0L6matrixS5422->$2;
      _M0L6_2acntS5691
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5422));
      if (_M0L6_2acntS5691 > 1) {
        int32_t _M0L11_2anew__cntS5694 = _M0L6_2acntS5691 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5422), _M0L11_2anew__cntS5694);
        moonbit_incref_cycle_free(_M0L6rowptrS5419);
      } else if (_M0L6_2acntS5691 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5693 = _M0L6matrixS5422->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5692;
        moonbit_decref_cycle_free(_M0L8_2afieldS5693);
        _M0L8_2afieldS5692 = _M0L6matrixS5422->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5692);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5422);
      }
      _M0L4varsS5420 = _M0L1eS1839->$4;
      _M0L5paramS5421 = _M0L1eS1839->$3;
      moonbit_incref_cycle_free(_M0L5paramS5421);
      moonbit_incref_cycle_free(_M0L4varsS5420);
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt25stdp__antisymmetric__step(_M0L4valsS5415, _M0L4fireS5416, _M0L4fireS5417, _M0L6colptrS5418, _M0L6rowptrS5419, _M0L4varsS5420, _M0L5paramS5421, _M0L2dtS1783);
      moonbit_decref_cycle_free(_M0L4valsS5415);
      moonbit_decref_cycle_free(_M0L4fireS5416);
      moonbit_decref_cycle_free(_M0L4fireS5417);
      moonbit_decref_cycle_free(_M0L6colptrS5418);
      moonbit_decref_cycle_free(_M0L6rowptrS5419);
      moonbit_decref_cycle_free(_M0L4varsS5420);
      moonbit_decref_cycle_free(_M0L5paramS5421);
      _M0L6t__nowS5427 = _M0L1eS1839->$5;
      _M0L6t__nowS5430 = _M0L1eS1839->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5427);
      _M0L6_2acntS5695 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1839));
      if (_M0L6_2acntS5695 > 1) {
        int32_t _M0L11_2anew__cntS5698 = _M0L6_2acntS5695 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1839), _M0L11_2anew__cntS5698);
        moonbit_incref_cycle_free(_M0L6t__nowS5430);
      } else if (_M0L6_2acntS5695 == 1) {
        struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L8_2afieldS5697 =
          _M0L1eS1839->$4;
        struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L8_2afieldS5696;
        moonbit_decref_cycle_free(_M0L8_2afieldS5697);
        _M0L8_2afieldS5696 = _M0L1eS1839->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5696);
        #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1839);
      }
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5429 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5430, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5430);
      _M0L6_2atmpS5428 = _M0L6_2atmpS5429 + _M0L2dtS1783;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5427, 0, _M0L6_2atmpS5428);
      moonbit_decref_cycle_free(_M0L6t__nowS5427);
      joinlet_5780:;
      goto joinlet_5779;
      join_1835:;
      _M0L5connsS5413 = _M0L1mS1778->$1;
      _M0L11conn__indexS5414 = _M0L1eS1836->$0;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1837
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5413, _M0L11conn__indexS5414);
      _M0L6matrixS5408 = _M0L3synS1837->$4;
      _M0L4valsS5395 = _M0L6matrixS5408->$4;
      _M0L3preS5407 = _M0L3synS1837->$0;
      _M0L4fireS5396 = _M0L3preS5407->$5;
      _M0L4postS5406 = _M0L3synS1837->$1;
      _M0L4fireS5397 = _M0L4postS5406->$5;
      _M0L6matrixS5405 = _M0L3synS1837->$4;
      _M0L6colptrS5398 = _M0L6matrixS5405->$3;
      _M0L6matrixS5404 = _M0L3synS1837->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5398);
      moonbit_incref_cycle_free(_M0L4fireS5397);
      moonbit_incref_cycle_free(_M0L4fireS5396);
      moonbit_incref_cycle_free(_M0L4valsS5395);
      _M0L6_2acntS5661
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1837));
      if (_M0L6_2acntS5661 > 1) {
        int32_t _M0L11_2anew__cntS5671 = _M0L6_2acntS5661 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1837), _M0L11_2anew__cntS5671);
        moonbit_incref_cycle_free(_M0L6matrixS5404);
      } else if (_M0L6_2acntS5661 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5670 = _M0L3synS1837->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5669;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5668;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5667;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5666;
        moonbit_string_t _M0L8_2afieldS5665;
        moonbit_string_t _M0L8_2afieldS5664;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5663;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5662;
        moonbit_decref_cycle_free(_M0L8_2afieldS5670);
        _M0L8_2afieldS5669 = _M0L3synS1837->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5669);
        _M0L8_2afieldS5668 = _M0L3synS1837->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5668);
        _M0L8_2afieldS5667 = _M0L3synS1837->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5667);
        _M0L8_2afieldS5666 = _M0L3synS1837->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5666);
        _M0L8_2afieldS5665 = _M0L3synS1837->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5665);
        _M0L8_2afieldS5664 = _M0L3synS1837->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5664);
        _M0L8_2afieldS5663 = _M0L3synS1837->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5663);
        _M0L8_2afieldS5662 = _M0L3synS1837->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5662);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1837);
      }
      _M0L6rowptrS5399 = _M0L6matrixS5404->$2;
      _M0L6_2acntS5672
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5404));
      if (_M0L6_2acntS5672 > 1) {
        int32_t _M0L11_2anew__cntS5675 = _M0L6_2acntS5672 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5404), _M0L11_2anew__cntS5675);
        moonbit_incref_cycle_free(_M0L6rowptrS5399);
      } else if (_M0L6_2acntS5672 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5674 = _M0L6matrixS5404->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5673;
        moonbit_decref_cycle_free(_M0L8_2afieldS5674);
        _M0L8_2afieldS5673 = _M0L6matrixS5404->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5673);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5404);
      }
      _M0L4varsS5400 = _M0L1eS1836->$4;
      _M0L5paramS5401 = _M0L1eS1836->$3;
      _M0L6t__nowS5403 = _M0L1eS1836->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5403);
      moonbit_incref_cycle_free(_M0L5paramS5401);
      moonbit_incref_cycle_free(_M0L4varsS5400);
      #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5402 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5403, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5403);
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt22stdp__confavreux__step(_M0L4valsS5395, _M0L4fireS5396, _M0L4fireS5397, _M0L6colptrS5398, _M0L6rowptrS5399, _M0L4varsS5400, _M0L5paramS5401, _M0L6_2atmpS5402, _M0L2dtS1783);
      moonbit_decref_cycle_free(_M0L4valsS5395);
      moonbit_decref_cycle_free(_M0L4fireS5396);
      moonbit_decref_cycle_free(_M0L4fireS5397);
      moonbit_decref_cycle_free(_M0L6colptrS5398);
      moonbit_decref_cycle_free(_M0L6rowptrS5399);
      moonbit_decref_cycle_free(_M0L4varsS5400);
      moonbit_decref_cycle_free(_M0L5paramS5401);
      _M0L6t__nowS5409 = _M0L1eS1836->$5;
      _M0L6t__nowS5412 = _M0L1eS1836->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5409);
      _M0L6_2acntS5676 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1836));
      if (_M0L6_2acntS5676 > 1) {
        int32_t _M0L11_2anew__cntS5679 = _M0L6_2acntS5676 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1836), _M0L11_2anew__cntS5679);
        moonbit_incref_cycle_free(_M0L6t__nowS5412);
      } else if (_M0L6_2acntS5676 == 1) {
        struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L8_2afieldS5678 =
          _M0L1eS1836->$4;
        struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L8_2afieldS5677;
        moonbit_decref_cycle_free(_M0L8_2afieldS5678);
        _M0L8_2afieldS5677 = _M0L1eS1836->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5677);
        #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1836);
      }
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5411 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5412, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5412);
      _M0L6_2atmpS5410 = _M0L6_2atmpS5411 + _M0L2dtS1783;
      #line 192 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5409, 0, _M0L6_2atmpS5410);
      moonbit_decref_cycle_free(_M0L6t__nowS5409);
      joinlet_5779:;
      goto joinlet_5778;
      join_1832:;
      _M0L5connsS5393 = _M0L1mS1778->$1;
      _M0L11conn__indexS5394 = _M0L1eS1833->$0;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1834
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5393, _M0L11conn__indexS5394);
      _M0L6matrixS5388 = _M0L3synS1834->$4;
      _M0L4valsS5375 = _M0L6matrixS5388->$4;
      _M0L3preS5387 = _M0L3synS1834->$0;
      _M0L4fireS5376 = _M0L3preS5387->$5;
      _M0L4postS5386 = _M0L3synS1834->$1;
      _M0L4fireS5377 = _M0L4postS5386->$5;
      _M0L6matrixS5385 = _M0L3synS1834->$4;
      _M0L6colptrS5378 = _M0L6matrixS5385->$3;
      _M0L6matrixS5384 = _M0L3synS1834->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5378);
      moonbit_incref_cycle_free(_M0L4fireS5377);
      moonbit_incref_cycle_free(_M0L4fireS5376);
      moonbit_incref_cycle_free(_M0L4valsS5375);
      _M0L6_2acntS5642
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1834));
      if (_M0L6_2acntS5642 > 1) {
        int32_t _M0L11_2anew__cntS5652 = _M0L6_2acntS5642 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1834), _M0L11_2anew__cntS5652);
        moonbit_incref_cycle_free(_M0L6matrixS5384);
      } else if (_M0L6_2acntS5642 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5651 = _M0L3synS1834->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5650;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5649;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5648;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5647;
        moonbit_string_t _M0L8_2afieldS5646;
        moonbit_string_t _M0L8_2afieldS5645;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5644;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5643;
        moonbit_decref_cycle_free(_M0L8_2afieldS5651);
        _M0L8_2afieldS5650 = _M0L3synS1834->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5650);
        _M0L8_2afieldS5649 = _M0L3synS1834->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5649);
        _M0L8_2afieldS5648 = _M0L3synS1834->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5648);
        _M0L8_2afieldS5647 = _M0L3synS1834->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5647);
        _M0L8_2afieldS5646 = _M0L3synS1834->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5646);
        _M0L8_2afieldS5645 = _M0L3synS1834->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5645);
        _M0L8_2afieldS5644 = _M0L3synS1834->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5644);
        _M0L8_2afieldS5643 = _M0L3synS1834->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5643);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1834);
      }
      _M0L6rowptrS5379 = _M0L6matrixS5384->$2;
      _M0L6_2acntS5653
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5384));
      if (_M0L6_2acntS5653 > 1) {
        int32_t _M0L11_2anew__cntS5656 = _M0L6_2acntS5653 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5384), _M0L11_2anew__cntS5656);
        moonbit_incref_cycle_free(_M0L6rowptrS5379);
      } else if (_M0L6_2acntS5653 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5655 = _M0L6matrixS5384->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5654;
        moonbit_decref_cycle_free(_M0L8_2afieldS5655);
        _M0L8_2afieldS5654 = _M0L6matrixS5384->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5654);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5384);
      }
      _M0L4varsS5380 = _M0L1eS1833->$4;
      _M0L5paramS5381 = _M0L1eS1833->$3;
      _M0L6t__nowS5383 = _M0L1eS1833->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5383);
      moonbit_incref_cycle_free(_M0L5paramS5381);
      moonbit_incref_cycle_free(_M0L4varsS5380);
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5382 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5383, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5383);
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt17istdp__rate__step(_M0L4valsS5375, _M0L4fireS5376, _M0L4fireS5377, _M0L6colptrS5378, _M0L6rowptrS5379, _M0L4varsS5380, _M0L5paramS5381, _M0L6_2atmpS5382, _M0L2dtS1783);
      moonbit_decref_cycle_free(_M0L4valsS5375);
      moonbit_decref_cycle_free(_M0L4fireS5376);
      moonbit_decref_cycle_free(_M0L4fireS5377);
      moonbit_decref_cycle_free(_M0L6colptrS5378);
      moonbit_decref_cycle_free(_M0L6rowptrS5379);
      moonbit_decref_cycle_free(_M0L4varsS5380);
      moonbit_decref_cycle_free(_M0L5paramS5381);
      _M0L6t__nowS5389 = _M0L1eS1833->$5;
      _M0L6t__nowS5392 = _M0L1eS1833->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5389);
      _M0L6_2acntS5657 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1833));
      if (_M0L6_2acntS5657 > 1) {
        int32_t _M0L11_2anew__cntS5660 = _M0L6_2acntS5657 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1833), _M0L11_2anew__cntS5660);
        moonbit_incref_cycle_free(_M0L6t__nowS5392);
      } else if (_M0L6_2acntS5657 == 1) {
        struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L8_2afieldS5659 =
          _M0L1eS1833->$4;
        struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L8_2afieldS5658;
        moonbit_decref_cycle_free(_M0L8_2afieldS5659);
        _M0L8_2afieldS5658 = _M0L1eS1833->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5658);
        #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1833);
      }
      #line 207 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5391 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5392, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5392);
      _M0L6_2atmpS5390 = _M0L6_2atmpS5391 + _M0L2dtS1783;
      #line 207 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5389, 0, _M0L6_2atmpS5390);
      moonbit_decref_cycle_free(_M0L6t__nowS5389);
      joinlet_5778:;
      goto joinlet_5777;
      join_1829:;
      _M0L5connsS5373 = _M0L1mS1778->$1;
      _M0L11conn__indexS5374 = _M0L1eS1830->$0;
      #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1831
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5373, _M0L11conn__indexS5374);
      _M0L6matrixS5368 = _M0L3synS1831->$4;
      _M0L4valsS5353 = _M0L6matrixS5368->$4;
      _M0L3preS5367 = _M0L3synS1831->$0;
      _M0L4fireS5354 = _M0L3preS5367->$5;
      _M0L4postS5366 = _M0L3synS1831->$1;
      _M0L4fireS5355 = _M0L4postS5366->$5;
      _M0L6matrixS5365 = _M0L3synS1831->$4;
      _M0L6colptrS5356 = _M0L6matrixS5365->$3;
      _M0L6matrixS5364 = _M0L3synS1831->$4;
      _M0L6rowptrS5357 = _M0L6matrixS5364->$2;
      _M0L4postS5363 = _M0L3synS1831->$1;
      moonbit_incref_cycle_free(_M0L6rowptrS5357);
      moonbit_incref_cycle_free(_M0L6colptrS5356);
      moonbit_incref_cycle_free(_M0L4fireS5355);
      moonbit_incref_cycle_free(_M0L4fireS5354);
      moonbit_incref_cycle_free(_M0L4valsS5353);
      _M0L6_2acntS5610
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1831));
      if (_M0L6_2acntS5610 > 1) {
        int32_t _M0L11_2anew__cntS5620 = _M0L6_2acntS5610 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1831), _M0L11_2anew__cntS5620);
        moonbit_incref_cycle_free(_M0L4postS5363);
      } else if (_M0L6_2acntS5610 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5619 = _M0L3synS1831->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5618;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5617;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5616;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5615;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L8_2afieldS5614;
        moonbit_string_t _M0L8_2afieldS5613;
        moonbit_string_t _M0L8_2afieldS5612;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5611;
        moonbit_decref_cycle_free(_M0L8_2afieldS5619);
        _M0L8_2afieldS5618 = _M0L3synS1831->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5618);
        _M0L8_2afieldS5617 = _M0L3synS1831->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5617);
        _M0L8_2afieldS5616 = _M0L3synS1831->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5616);
        _M0L8_2afieldS5615 = _M0L3synS1831->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5615);
        _M0L8_2afieldS5614 = _M0L3synS1831->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS5614);
        _M0L8_2afieldS5613 = _M0L3synS1831->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5613);
        _M0L8_2afieldS5612 = _M0L3synS1831->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5612);
        _M0L8_2afieldS5611 = _M0L3synS1831->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5611);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1831);
      }
      _M0L1vS5358 = _M0L4postS5363->$3;
      _M0L6_2acntS5621
      = Moonbit_rc_count(Moonbit_object_header(_M0L4postS5363));
      if (_M0L6_2acntS5621 > 1) {
        int32_t _M0L11_2anew__cntS5637 = _M0L6_2acntS5621 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L4postS5363), _M0L11_2anew__cntS5637);
        moonbit_incref_cycle_free(_M0L1vS5358);
      } else if (_M0L6_2acntS5621 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5636 = _M0L4postS5363->$16;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5635;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5634;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5633;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5632;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5631;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5630;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5629;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5628;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5627;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5626;
        struct _M0TPB5ArrayGbE* _M0L8_2afieldS5625;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5624;
        struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L8_2afieldS5623;
        struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L8_2afieldS5622;
        moonbit_decref_cycle_free(_M0L8_2afieldS5636);
        _M0L8_2afieldS5635 = _M0L4postS5363->$15;
        moonbit_decref_cycle_free(_M0L8_2afieldS5635);
        _M0L8_2afieldS5634 = _M0L4postS5363->$14;
        moonbit_decref_cycle_free(_M0L8_2afieldS5634);
        _M0L8_2afieldS5633 = _M0L4postS5363->$13;
        moonbit_decref_cycle_free(_M0L8_2afieldS5633);
        _M0L8_2afieldS5632 = _M0L4postS5363->$12;
        moonbit_decref_cycle_free(_M0L8_2afieldS5632);
        _M0L8_2afieldS5631 = _M0L4postS5363->$11;
        moonbit_decref_cycle_free(_M0L8_2afieldS5631);
        _M0L8_2afieldS5630 = _M0L4postS5363->$10;
        moonbit_decref_cycle_free(_M0L8_2afieldS5630);
        _M0L8_2afieldS5629 = _M0L4postS5363->$9;
        moonbit_decref_cycle_free(_M0L8_2afieldS5629);
        _M0L8_2afieldS5628 = _M0L4postS5363->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5628);
        _M0L8_2afieldS5627 = _M0L4postS5363->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5627);
        _M0L8_2afieldS5626 = _M0L4postS5363->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5626);
        _M0L8_2afieldS5625 = _M0L4postS5363->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5625);
        _M0L8_2afieldS5624 = _M0L4postS5363->$4;
        moonbit_decref_cycle_free(_M0L8_2afieldS5624);
        _M0L8_2afieldS5623 = _M0L4postS5363->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5623);
        _M0L8_2afieldS5622 = _M0L4postS5363->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5622);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L4postS5363);
      }
      _M0L4varsS5359 = _M0L1eS1830->$4;
      _M0L5paramS5360 = _M0L1eS1830->$3;
      _M0L6t__nowS5362 = _M0L1eS1830->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5362);
      moonbit_incref_cycle_free(_M0L5paramS5360);
      moonbit_incref_cycle_free(_M0L4varsS5359);
      #line 220 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5361 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5362, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5362);
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt22istdp__potential__step(_M0L4valsS5353, _M0L4fireS5354, _M0L4fireS5355, _M0L6colptrS5356, _M0L6rowptrS5357, _M0L1vS5358, _M0L4varsS5359, _M0L5paramS5360, _M0L6_2atmpS5361, _M0L2dtS1783);
      moonbit_decref_cycle_free(_M0L4valsS5353);
      moonbit_decref_cycle_free(_M0L4fireS5354);
      moonbit_decref_cycle_free(_M0L4fireS5355);
      moonbit_decref_cycle_free(_M0L6colptrS5356);
      moonbit_decref_cycle_free(_M0L6rowptrS5357);
      moonbit_decref_cycle_free(_M0L1vS5358);
      moonbit_decref_cycle_free(_M0L4varsS5359);
      moonbit_decref_cycle_free(_M0L5paramS5360);
      _M0L6t__nowS5369 = _M0L1eS1830->$5;
      _M0L6t__nowS5372 = _M0L1eS1830->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5369);
      _M0L6_2acntS5638 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1830));
      if (_M0L6_2acntS5638 > 1) {
        int32_t _M0L11_2anew__cntS5641 = _M0L6_2acntS5638 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1830), _M0L11_2anew__cntS5641);
        moonbit_incref_cycle_free(_M0L6t__nowS5372);
      } else if (_M0L6_2acntS5638 == 1) {
        struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L8_2afieldS5640 =
          _M0L1eS1830->$4;
        struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L8_2afieldS5639;
        moonbit_decref_cycle_free(_M0L8_2afieldS5640);
        _M0L8_2afieldS5639 = _M0L1eS1830->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5639);
        #line 210 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1830);
      }
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5371 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5372, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5372);
      _M0L6_2atmpS5370 = _M0L6_2atmpS5371 + _M0L2dtS1783;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5369, 0, _M0L6_2atmpS5370);
      moonbit_decref_cycle_free(_M0L6t__nowS5369);
      joinlet_5777:;
      goto joinlet_5776;
      join_1826:;
      _M0L5connsS5351 = _M0L1mS1778->$1;
      _M0L11conn__indexS5352 = _M0L1eS1827->$0;
      #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1828
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5351, _M0L11conn__indexS5352);
      _M0L6matrixS5346 = _M0L3synS1828->$4;
      _M0L4valsS5333 = _M0L6matrixS5346->$4;
      _M0L3preS5345 = _M0L3synS1828->$0;
      _M0L4fireS5334 = _M0L3preS5345->$5;
      _M0L4postS5344 = _M0L3synS1828->$1;
      _M0L4fireS5335 = _M0L4postS5344->$5;
      _M0L6matrixS5343 = _M0L3synS1828->$4;
      _M0L6colptrS5336 = _M0L6matrixS5343->$3;
      _M0L6matrixS5342 = _M0L3synS1828->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5336);
      moonbit_incref_cycle_free(_M0L4fireS5335);
      moonbit_incref_cycle_free(_M0L4fireS5334);
      moonbit_incref_cycle_free(_M0L4valsS5333);
      _M0L6_2acntS5591
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1828));
      if (_M0L6_2acntS5591 > 1) {
        int32_t _M0L11_2anew__cntS5601 = _M0L6_2acntS5591 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1828), _M0L11_2anew__cntS5601);
        moonbit_incref_cycle_free(_M0L6matrixS5342);
      } else if (_M0L6_2acntS5591 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5600 = _M0L3synS1828->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5599;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5598;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5597;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5596;
        moonbit_string_t _M0L8_2afieldS5595;
        moonbit_string_t _M0L8_2afieldS5594;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5593;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5592;
        moonbit_decref_cycle_free(_M0L8_2afieldS5600);
        _M0L8_2afieldS5599 = _M0L3synS1828->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5599);
        _M0L8_2afieldS5598 = _M0L3synS1828->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5598);
        _M0L8_2afieldS5597 = _M0L3synS1828->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5597);
        _M0L8_2afieldS5596 = _M0L3synS1828->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5596);
        _M0L8_2afieldS5595 = _M0L3synS1828->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5595);
        _M0L8_2afieldS5594 = _M0L3synS1828->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5594);
        _M0L8_2afieldS5593 = _M0L3synS1828->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5593);
        _M0L8_2afieldS5592 = _M0L3synS1828->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5592);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1828);
      }
      _M0L6rowptrS5337 = _M0L6matrixS5342->$2;
      _M0L6_2acntS5602
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5342));
      if (_M0L6_2acntS5602 > 1) {
        int32_t _M0L11_2anew__cntS5605 = _M0L6_2acntS5602 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5342), _M0L11_2anew__cntS5605);
        moonbit_incref_cycle_free(_M0L6rowptrS5337);
      } else if (_M0L6_2acntS5602 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5604 = _M0L6matrixS5342->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5603;
        moonbit_decref_cycle_free(_M0L8_2afieldS5604);
        _M0L8_2afieldS5603 = _M0L6matrixS5342->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5603);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5342);
      }
      _M0L4varsS5338 = _M0L1eS1827->$4;
      _M0L5paramS5339 = _M0L1eS1827->$3;
      _M0L6t__nowS5341 = _M0L1eS1827->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5341);
      moonbit_incref_cycle_free(_M0L5paramS5339);
      moonbit_incref_cycle_free(_M0L4varsS5338);
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5340 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5341, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5341);
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt21stdp__symmetric__step(_M0L4valsS5333, _M0L4fireS5334, _M0L4fireS5335, _M0L6colptrS5336, _M0L6rowptrS5337, _M0L4varsS5338, _M0L5paramS5339, _M0L6_2atmpS5340, _M0L2dtS1783);
      moonbit_decref_cycle_free(_M0L4valsS5333);
      moonbit_decref_cycle_free(_M0L4fireS5334);
      moonbit_decref_cycle_free(_M0L4fireS5335);
      moonbit_decref_cycle_free(_M0L6colptrS5336);
      moonbit_decref_cycle_free(_M0L6rowptrS5337);
      moonbit_decref_cycle_free(_M0L4varsS5338);
      moonbit_decref_cycle_free(_M0L5paramS5339);
      _M0L6t__nowS5347 = _M0L1eS1827->$5;
      _M0L6t__nowS5350 = _M0L1eS1827->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5347);
      _M0L6_2acntS5606 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1827));
      if (_M0L6_2acntS5606 > 1) {
        int32_t _M0L11_2anew__cntS5609 = _M0L6_2acntS5606 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1827), _M0L11_2anew__cntS5609);
        moonbit_incref_cycle_free(_M0L6t__nowS5350);
      } else if (_M0L6_2acntS5606 == 1) {
        struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L8_2afieldS5608 =
          _M0L1eS1827->$4;
        struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L8_2afieldS5607;
        moonbit_decref_cycle_free(_M0L8_2afieldS5608);
        _M0L8_2afieldS5607 = _M0L1eS1827->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5607);
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1827);
      }
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5349 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5350, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5350);
      _M0L6_2atmpS5348 = _M0L6_2atmpS5349 + _M0L2dtS1783;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5347, 0, _M0L6_2atmpS5348);
      moonbit_decref_cycle_free(_M0L6t__nowS5347);
      joinlet_5776:;
      goto joinlet_5775;
      join_1823:;
      _M0L5connsS5331 = _M0L1mS1778->$1;
      _M0L11conn__indexS5332 = _M0L1eS1824->$0;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L3synS1825
      = _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L5connsS5331, _M0L11conn__indexS5332);
      _M0L6matrixS5326 = _M0L3synS1825->$4;
      _M0L4valsS5313 = _M0L6matrixS5326->$4;
      _M0L3preS5325 = _M0L3synS1825->$0;
      _M0L4fireS5314 = _M0L3preS5325->$5;
      _M0L4postS5324 = _M0L3synS1825->$1;
      _M0L4fireS5315 = _M0L4postS5324->$5;
      _M0L6matrixS5323 = _M0L3synS1825->$4;
      _M0L6colptrS5316 = _M0L6matrixS5323->$3;
      _M0L6matrixS5322 = _M0L3synS1825->$4;
      moonbit_incref_cycle_free(_M0L6colptrS5316);
      moonbit_incref_cycle_free(_M0L4fireS5315);
      moonbit_incref_cycle_free(_M0L4fireS5314);
      moonbit_incref_cycle_free(_M0L4valsS5313);
      _M0L6_2acntS5572
      = Moonbit_rc_count(Moonbit_object_header(_M0L3synS1825));
      if (_M0L6_2acntS5572 > 1) {
        int32_t _M0L11_2anew__cntS5582 = _M0L6_2acntS5572 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L3synS1825), _M0L11_2anew__cntS5582);
        moonbit_incref_cycle_free(_M0L6matrixS5322);
      } else if (_M0L6_2acntS5572 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5581 = _M0L3synS1825->$9;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5580;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5579;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5578;
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5577;
        moonbit_string_t _M0L8_2afieldS5576;
        moonbit_string_t _M0L8_2afieldS5575;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5574;
        struct _M0TP26RiantR8snn__mbt2IF* _M0L8_2afieldS5573;
        moonbit_decref_cycle_free(_M0L8_2afieldS5581);
        _M0L8_2afieldS5580 = _M0L3synS1825->$8;
        moonbit_decref_cycle_free(_M0L8_2afieldS5580);
        _M0L8_2afieldS5579 = _M0L3synS1825->$7;
        moonbit_decref_cycle_free(_M0L8_2afieldS5579);
        _M0L8_2afieldS5578 = _M0L3synS1825->$6;
        moonbit_decref_cycle_free(_M0L8_2afieldS5578);
        _M0L8_2afieldS5577 = _M0L3synS1825->$5;
        moonbit_decref_cycle_free(_M0L8_2afieldS5577);
        _M0L8_2afieldS5576 = _M0L3synS1825->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5576);
        _M0L8_2afieldS5575 = _M0L3synS1825->$2;
        moonbit_decref_cycle_free(_M0L8_2afieldS5575);
        _M0L8_2afieldS5574 = _M0L3synS1825->$1;
        moonbit_decref_cycle_free(_M0L8_2afieldS5574);
        _M0L8_2afieldS5573 = _M0L3synS1825->$0;
        moonbit_decref_cycle_free(_M0L8_2afieldS5573);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L3synS1825);
      }
      _M0L6rowptrS5317 = _M0L6matrixS5322->$2;
      _M0L6_2acntS5583
      = Moonbit_rc_count(Moonbit_object_header(_M0L6matrixS5322));
      if (_M0L6_2acntS5583 > 1) {
        int32_t _M0L11_2anew__cntS5586 = _M0L6_2acntS5583 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L6matrixS5322), _M0L11_2anew__cntS5586);
        moonbit_incref_cycle_free(_M0L6rowptrS5317);
      } else if (_M0L6_2acntS5583 == 1) {
        struct _M0TPB5ArrayGfE* _M0L8_2afieldS5585 = _M0L6matrixS5322->$4;
        struct _M0TPB5ArrayGiE* _M0L8_2afieldS5584;
        moonbit_decref_cycle_free(_M0L8_2afieldS5585);
        _M0L8_2afieldS5584 = _M0L6matrixS5322->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5584);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L6matrixS5322);
      }
      _M0L4varsS5318 = _M0L1eS1824->$4;
      _M0L5paramS5319 = _M0L1eS1824->$3;
      _M0L6t__nowS5321 = _M0L1eS1824->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5321);
      moonbit_incref_cycle_free(_M0L5paramS5319);
      moonbit_incref_cycle_free(_M0L4varsS5318);
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5320 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5321, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5321);
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt20ca__plasticity__step(_M0L4valsS5313, _M0L4fireS5314, _M0L4fireS5315, _M0L6colptrS5316, _M0L6rowptrS5317, _M0L4varsS5318, _M0L5paramS5319, _M0L6_2atmpS5320, _M0L2dtS1783);
      moonbit_decref_cycle_free(_M0L4valsS5313);
      moonbit_decref_cycle_free(_M0L4fireS5314);
      moonbit_decref_cycle_free(_M0L4fireS5315);
      moonbit_decref_cycle_free(_M0L6colptrS5316);
      moonbit_decref_cycle_free(_M0L6rowptrS5317);
      moonbit_decref_cycle_free(_M0L4varsS5318);
      moonbit_decref_cycle_free(_M0L5paramS5319);
      _M0L6t__nowS5327 = _M0L1eS1824->$5;
      _M0L6t__nowS5330 = _M0L1eS1824->$5;
      moonbit_incref_cycle_free(_M0L6t__nowS5327);
      _M0L6_2acntS5587 = Moonbit_rc_count(Moonbit_object_header(_M0L1eS1824));
      if (_M0L6_2acntS5587 > 1) {
        int32_t _M0L11_2anew__cntS5590 = _M0L6_2acntS5587 - 1;
        Moonbit_set_rc_count(Moonbit_object_header(_M0L1eS1824), _M0L11_2anew__cntS5590);
        moonbit_incref_cycle_free(_M0L6t__nowS5330);
      } else if (_M0L6_2acntS5587 == 1) {
        struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L8_2afieldS5589 =
          _M0L1eS1824->$4;
        struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L8_2afieldS5588;
        moonbit_decref_cycle_free(_M0L8_2afieldS5589);
        _M0L8_2afieldS5588 = _M0L1eS1824->$3;
        moonbit_decref_cycle_free(_M0L8_2afieldS5588);
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
        moonbit_free(_M0L1eS1824);
      }
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5329 = _M0MPC15array5Array2atGfE(_M0L6t__nowS5330, 0);
      moonbit_decref_cycle_free(_M0L6t__nowS5330);
      _M0L6_2atmpS5328 = _M0L6_2atmpS5329 + _M0L2dtS1783;
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0MPC15array5Array3setGfE(_M0L6t__nowS5327, 0, _M0L6_2atmpS5328);
      moonbit_decref_cycle_free(_M0L6t__nowS5327);
      joinlet_5775:;
      _M0L6_2atmpS5472 = _M0L2__S1821 + 1;
      _M0L2__S1821 = _M0L6_2atmpS5472;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1820);
    }
    break;
  }
  _M0L7_2abindS1864 = _M0L1mS1778->$0;
  _M0L7_2abindS1865 = _M0L7_2abindS1864->$1;
  _M0L7_2abindS1866 = _M0L7_2abindS1864->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1866);
  _M0L2__S1867 = 0;
  while (1) {
    if (_M0L2__S1867 < _M0L7_2abindS1865) {
      void* _M0L1pS1868 = (void*)_M0L7_2abindS1866[_M0L2__S1867];
      int32_t _M0L6_2atmpS5473;
      moonbit_incref_cycle_free(_M0L1pS1868);
      #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt14integrate__any(_M0L1pS1868, _M0L2dtS1783);
      moonbit_decref_cycle_free(_M0L1pS1868);
      _M0L6_2atmpS5473 = _M0L2__S1867 + 1;
      _M0L2__S1867 = _M0L6_2atmpS5473;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1866);
    }
    break;
  }
  _M0L7_2abindS1870 = _M0L1mS1778->$4;
  _M0L7_2abindS1871 = _M0L7_2abindS1870->$1;
  _M0L7_2abindS1872 = _M0L7_2abindS1870->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS1872);
  _M0L2__S1873 = 0;
  while (1) {
    if (_M0L2__S1873 < _M0L7_2abindS1871) {
      struct _M0TP26RiantR8snn__mbt7Monitor* _M0L3monS1874 =
        (struct _M0TP26RiantR8snn__mbt7Monitor*)_M0L7_2abindS1872[
          _M0L2__S1873
        ];
      struct _M0TP26RiantR8snn__mbt4Time* _M0L4timeS5475 = _M0L1mS1778->$3;
      float _M0L6_2atmpS5474;
      int32_t _M0L6_2atmpS5476;
      moonbit_incref_cycle_free(_M0L3monS1874);
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0L6_2atmpS5474 = _M0FP26RiantR8snn__mbt9get__time(_M0L4timeS5475);
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
      _M0FP26RiantR8snn__mbt11record__one(_M0L3monS1874, _M0L6_2atmpS5474);
      moonbit_decref_cycle_free(_M0L3monS1874);
      _M0L6_2atmpS5476 = _M0L2__S1873 + 1;
      _M0L2__S1873 = _M0L6_2atmpS5476;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1872);
    }
    break;
  }
  _M0L4timeS5477 = _M0L1mS1778->$3;
  #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt12update__time(_M0L4timeS5477, _M0L2dtS1783);
  return 0;
}

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0FP26RiantR8snn__mbt7compose(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L4popsS1775,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS1776,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L11stims_2eoptS1764,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L14monitors_2eoptS1767,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L10stdp_2eoptS1770,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L9stp_2eoptS1773
) {
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L5stimsS1763;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L8monitorsS1766;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L4stdpS1769;
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L3stpS1772;
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _result_5785;
  if (_M0L11stims_2eoptS1764 == 0) {
    void** _M0L6_2atmpS5285 = (void**)moonbit_empty_ref_array;
    _M0L5stimsS1763
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE));
    Moonbit_object_header(_M0L5stimsS1763)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 18, 0);
    _M0L5stimsS1763->$0 = _M0L6_2atmpS5285;
    _M0L5stimsS1763->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L7_2aSomeS1765 =
      _M0L11stims_2eoptS1764;
    if (_M0L7_2aSomeS1765) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1765);
    }
    _M0L5stimsS1763 = _M0L7_2aSomeS1765;
  }
  if (_M0L14monitors_2eoptS1767 == 0) {
    struct _M0TP26RiantR8snn__mbt7Monitor** _M0L6_2atmpS5284 =
      (struct _M0TP26RiantR8snn__mbt7Monitor**)moonbit_empty_ref_array;
    _M0L8monitorsS1766
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE));
    Moonbit_object_header(_M0L8monitorsS1766)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 21, 0);
    _M0L8monitorsS1766->$0 = _M0L6_2atmpS5284;
    _M0L8monitorsS1766->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L7_2aSomeS1768 =
      _M0L14monitors_2eoptS1767;
    if (_M0L7_2aSomeS1768) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1768);
    }
    _M0L8monitorsS1766 = _M0L7_2aSomeS1768;
  }
  if (_M0L10stdp_2eoptS1770 == 0) {
    void** _M0L6_2atmpS5283 = (void**)moonbit_empty_ref_array;
    _M0L4stdpS1769
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE));
    Moonbit_object_header(_M0L4stdpS1769)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 24, 0);
    _M0L4stdpS1769->$0 = _M0L6_2atmpS5283;
    _M0L4stdpS1769->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L7_2aSomeS1771 =
      _M0L10stdp_2eoptS1770;
    if (_M0L7_2aSomeS1771) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1771);
    }
    _M0L4stdpS1769 = _M0L7_2aSomeS1771;
  }
  if (_M0L9stp_2eoptS1773 == 0) {
    void** _M0L6_2atmpS5282 = (void**)moonbit_empty_ref_array;
    _M0L3stpS1772
    = (struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE));
    Moonbit_object_header(_M0L3stpS1772)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 27, 0);
    _M0L3stpS1772->$0 = _M0L6_2atmpS5282;
    _M0L3stpS1772->$1 = 0;
  } else {
    struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L7_2aSomeS1774 =
      _M0L9stp_2eoptS1773;
    if (_M0L7_2aSomeS1774) {
      moonbit_incref_cycle_free(_M0L7_2aSomeS1774);
    }
    _M0L3stpS1772 = _M0L7_2aSomeS1774;
  }
  _result_5785
  = _M0FP26RiantR8snn__mbt15compose_2einner(_M0L4popsS1775, _M0L5connsS1776, _M0L5stimsS1763, _M0L8monitorsS1766, _M0L4stdpS1769, _M0L3stpS1772);
  moonbit_decref_cycle_free(_M0L5stimsS1763);
  moonbit_decref_cycle_free(_M0L8monitorsS1766);
  moonbit_decref_cycle_free(_M0L4stdpS1769);
  moonbit_decref_cycle_free(_M0L3stpS1772);
  return _result_5785;
}

struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _M0FP26RiantR8snn__mbt15compose_2einner(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt6AnyPopE* _M0L4popsS1757,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L5connsS1758,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7AnyStimE* _M0L5stimsS1759,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt7MonitorE* _M0L8monitorsS1760,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt13STDPEntryKindE* _M0L4stdpS1761,
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt12STPEntryKindE* _M0L3stpS1762
) {
  struct _M0TP26RiantR8snn__mbt4Time* _M0L6_2atmpS5281;
  struct _M0TP26RiantR8snn__mbt18HeterogeneousModel* _block_5786;
  #line 79 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5281 = _M0MP26RiantR8snn__mbt4Time3new();
  moonbit_incref_cycle_free(_M0L4popsS1757);
  moonbit_incref_cycle_free(_M0L5connsS1758);
  moonbit_incref_cycle_free(_M0L5stimsS1759);
  moonbit_incref_cycle_free(_M0L8monitorsS1760);
  moonbit_incref_cycle_free(_M0L4stdpS1761);
  moonbit_incref_cycle_free(_M0L3stpS1762);
  _block_5786
  = (struct _M0TP26RiantR8snn__mbt18HeterogeneousModel*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt18HeterogeneousModel));
  Moonbit_object_header(_block_5786)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 30, 0);
  _block_5786->$0 = _M0L4popsS1757;
  _block_5786->$1 = _M0L5connsS1758;
  _block_5786->$2 = _M0L5stimsS1759;
  _block_5786->$3 = _M0L6_2atmpS5281;
  _block_5786->$4 = _M0L8monitorsS1760;
  _block_5786->$5 = _M0L4stdpS1761;
  _block_5786->$6 = _M0L3stpS1762;
  return _block_5786;
}

int32_t _M0FP26RiantR8snn__mbt14stimulate__any(
  void* _M0L1sS1743,
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS1731,
  float _M0L2dtS1738
) {
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L1xS1729;
  float _M0L1wS1730;
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L1xS1733;
  struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L1xS1735;
  struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L1xS1737;
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L1xS1740;
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L1xS1742;
  float _M0L6_2atmpS5280;
  float _M0L6_2atmpS5279;
  float _M0L6_2atmpS5278;
  float _M0L6_2atmpS5277;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  switch (Moonbit_object_tag(_M0L1sS1743)) {
    case 0: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__* _M0L14_2aPoissonIF__S1744 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11PoissonIF__*)_M0L1sS1743;
      struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L4_2axS1745 =
        _M0L14_2aPoissonIF__S1744->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1745);
      _M0L1xS1742 = _M0L4_2axS1745;
      goto join_1741;
      break;
    }
    
    case 1: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim14PoissonLayer__* _M0L17_2aPoissonLayer__S1746 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim14PoissonLayer__*)_M0L1sS1743;
      struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L4_2axS1747 =
        _M0L17_2aPoissonLayer__S1746->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1747);
      _M0L1xS1740 = _M0L4_2axS1747;
      goto join_1739;
      break;
    }
    
    case 2: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim12BalancedIF__* _M0L15_2aBalancedIF__S1748 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim12BalancedIF__*)_M0L1sS1743;
      struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L4_2axS1749 =
        _M0L15_2aBalancedIF__S1748->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1749);
      _M0L1xS1737 = _M0L4_2axS1749;
      goto join_1736;
      break;
    }
    
    case 3: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__* _M0L14_2aCurrentIF__S1750 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11CurrentIF__*)_M0L1sS1743;
      struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L4_2axS1751 =
        _M0L14_2aCurrentIF__S1750->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1751);
      _M0L1xS1735 = _M0L4_2axS1751;
      goto join_1734;
      break;
    }
    
    case 4: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__* _M0L15_2aCurrentArr__S1752 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim12CurrentArr__*)_M0L1sS1743;
      struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L4_2axS1753 =
        _M0L15_2aCurrentArr__S1752->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1753);
      _M0L1xS1733 = _M0L4_2axS1753;
      goto join_1732;
      break;
    }
    default: {
      struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__* _M0L14_2aTimedStim__S1754 =
        (struct _M0DTP26RiantR8snn__mbt7AnyStim11TimedStim__*)_M0L1sS1743;
      struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L4_2axS1755 =
        _M0L14_2aTimedStim__S1754->$0;
      float _M0L4_2awS1756 = _M0L14_2aTimedStim__S1754->$1;
      moonbit_incref_cycle_free(_M0L4_2axS1755);
      _M0L1xS1729 = _M0L4_2axS1755;
      _M0L1wS1730 = _M0L4_2awS1756;
      goto join_1728;
      break;
    }
  }
  goto joinlet_5792;
  join_1741:;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5280 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1731);
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt13stimulate__if(_M0L1xS1742, _M0L6_2atmpS5280, _M0L2dtS1738);
  moonbit_decref_cycle_free(_M0L1xS1742);
  joinlet_5792:;
  goto joinlet_5791;
  join_1739:;
  #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5279 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1731);
  #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt16stimulate__layer(_M0L1xS1740, _M0L6_2atmpS5279, _M0L2dtS1738);
  moonbit_decref_cycle_free(_M0L1xS1740);
  joinlet_5791:;
  goto joinlet_5790;
  join_1736:;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5278 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1731);
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt19stimulate__balanced(_M0L1xS1737, _M0L6_2atmpS5278, _M0L2dtS1738);
  moonbit_decref_cycle_free(_M0L1xS1737);
  joinlet_5790:;
  goto joinlet_5789;
  join_1734:;
  #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt22stimulate__current__if(_M0L1xS1735);
  moonbit_decref_cycle_free(_M0L1xS1735);
  joinlet_5789:;
  goto joinlet_5788;
  join_1732:;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt25stimulate__current__array(_M0L1xS1733);
  moonbit_decref_cycle_free(_M0L1xS1733);
  joinlet_5788:;
  goto joinlet_5787;
  join_1728:;
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0L6_2atmpS5277 = _M0FP26RiantR8snn__mbt9get__time(_M0L1tS1731);
  #line 71 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\compose.mbt"
  _M0FP26RiantR8snn__mbt20stimulate__spiketime(_M0L1xS1729, _M0L6_2atmpS5277, _M0L1wS1730);
  moonbit_decref_cycle_free(_M0L1xS1729);
  joinlet_5787:;
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22istdp__potential__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1724,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1701,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1703,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1720,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1714,
  struct _M0TPB5ArrayGfE* _M0L7v__postS1711,
  struct _M0TP26RiantR8snn__mbt23IstdpPotentialVariables* _M0L4varsS1707,
  struct _M0TP26RiantR8snn__mbt14IstdpPotential* _M0L5paramS1705,
  float _M0L6t__nowS1699,
  float _M0L2dtS1708
) {
  int32_t _M0L6n__preS1700;
  int32_t _M0L7n__postS1702;
  float _M0L6tau__yS5276;
  float _M0L11inv__tau__yS1704;
  struct _M0TPB8MutLocalGiE* _M0L1jS1706;
  struct _M0TPB8MutLocalGiE* _M0L1iS1710;
  #line 316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6n__preS1700 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1701);
  #line 330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L7n__postS1702 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1703);
  _M0L6tau__yS5276 = _M0L5paramS1705->$2;
  _M0L11inv__tau__yS1704 = 0x1p+0f / _M0L6tau__yS5276;
  _M0L1jS1706
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1706)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1706->$0 = 0;
  while (1) {
    int32_t _M0L3valS5193 = _M0L1jS1706->$0;
    if (_M0L3valS5193 < _M0L6n__preS1700) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS5194 = _M0L4varsS1707->$0;
      int32_t _M0L3valS5195 = _M0L1jS1706->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5204 = _M0L4varsS1707->$0;
      int32_t _M0L3valS5205 = _M0L1jS1706->$0;
      float _M0L6_2atmpS5197;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5202;
      int32_t _M0L3valS5203;
      float _M0L6_2atmpS5201;
      float _M0L6_2atmpS5200;
      float _M0L6_2atmpS5199;
      float _M0L6_2atmpS5198;
      float _M0L6_2atmpS5196;
      int32_t _M0L3valS5206;
      int32_t _M0L3valS5214;
      int32_t _M0L6_2atmpS5213;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5197
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5204, _M0L3valS5205);
      _M0L4tpreS5202 = _M0L4varsS1707->$0;
      _M0L3valS5203 = _M0L1jS1706->$0;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5201
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5202, _M0L3valS5203);
      _M0L6_2atmpS5200 = -_M0L6_2atmpS5201;
      _M0L6_2atmpS5199 = _M0L2dtS1708 * _M0L6_2atmpS5200;
      _M0L6_2atmpS5198 = _M0L6_2atmpS5199 * _M0L11inv__tau__yS1704;
      _M0L6_2atmpS5196 = _M0L6_2atmpS5197 + _M0L6_2atmpS5198;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS5194, _M0L3valS5195, _M0L6_2atmpS5196);
      _M0L3valS5206 = _M0L1jS1706->$0;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1701, _M0L3valS5206)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS5207 = _M0L4varsS1707->$0;
        int32_t _M0L3valS5208 = _M0L1jS1706->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS5211 = _M0L4varsS1707->$0;
        int32_t _M0L3valS5212 = _M0L1jS1706->$0;
        float _M0L6_2atmpS5210;
        float _M0L6_2atmpS5209;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5210
        = _M0MPC15array5Array2atGfE(_M0L4tpreS5211, _M0L3valS5212);
        _M0L6_2atmpS5209 = _M0L6_2atmpS5210 + 0x1p+0f;
        #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS5207, _M0L3valS5208, _M0L6_2atmpS5209);
      }
      _M0L3valS5214 = _M0L1jS1706->$0;
      _M0L6_2atmpS5213 = _M0L3valS5214 + 1;
      _M0L1jS1706->$0 = _M0L6_2atmpS5213;
      continue;
    }
    break;
  }
  _M0L1iS1710
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1710)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1710->$0 = 0;
  while (1) {
    int32_t _M0L3valS5215 = _M0L1iS1710->$0;
    if (_M0L3valS5215 < _M0L7n__postS1702) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS5216 = _M0L4varsS1707->$1;
      int32_t _M0L3valS5217 = _M0L1iS1710->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5229 = _M0L4varsS1707->$1;
      int32_t _M0L3valS5230 = _M0L1iS1710->$0;
      float _M0L6_2atmpS5219;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5227;
      int32_t _M0L3valS5228;
      float _M0L6_2atmpS5224;
      int32_t _M0L3valS5226;
      float _M0L6_2atmpS5225;
      float _M0L6_2atmpS5223;
      float _M0L6_2atmpS5222;
      float _M0L6_2atmpS5221;
      float _M0L6_2atmpS5220;
      float _M0L6_2atmpS5218;
      int32_t _M0L3valS5231;
      int32_t _M0L3valS5239;
      int32_t _M0L6_2atmpS5238;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5219
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5229, _M0L3valS5230);
      _M0L5tpostS5227 = _M0L4varsS1707->$1;
      _M0L3valS5228 = _M0L1iS1710->$0;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5224
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5227, _M0L3valS5228);
      _M0L3valS5226 = _M0L1iS1710->$0;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5225
      = _M0MPC15array5Array2atGfE(_M0L7v__postS1711, _M0L3valS5226);
      _M0L6_2atmpS5223 = _M0L6_2atmpS5224 - _M0L6_2atmpS5225;
      _M0L6_2atmpS5222 = -_M0L6_2atmpS5223;
      _M0L6_2atmpS5221 = _M0L2dtS1708 * _M0L6_2atmpS5222;
      _M0L6_2atmpS5220 = _M0L6_2atmpS5221 * _M0L11inv__tau__yS1704;
      _M0L6_2atmpS5218 = _M0L6_2atmpS5219 + _M0L6_2atmpS5220;
      #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS5216, _M0L3valS5217, _M0L6_2atmpS5218);
      _M0L3valS5231 = _M0L1iS1710->$0;
      #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1703, _M0L3valS5231)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS5232 = _M0L4varsS1707->$1;
        int32_t _M0L3valS5233 = _M0L1iS1710->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS5236 = _M0L4varsS1707->$1;
        int32_t _M0L3valS5237 = _M0L1iS1710->$0;
        float _M0L6_2atmpS5235;
        float _M0L6_2atmpS5234;
        #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5235
        = _M0MPC15array5Array2atGfE(_M0L5tpostS5236, _M0L3valS5237);
        _M0L6_2atmpS5234 = _M0L6_2atmpS5235 + 0x1p+0f;
        #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS5232, _M0L3valS5233, _M0L6_2atmpS5234);
      }
      _M0L3valS5239 = _M0L1iS1710->$0;
      _M0L6_2atmpS5238 = _M0L3valS5239 + 1;
      _M0L1iS1710->$0 = _M0L6_2atmpS5238;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1710);
    }
    break;
  }
  _M0L1jS1706->$0 = 0;
  while (1) {
    int32_t _M0L3valS5240 = _M0L1jS1706->$0;
    if (_M0L3valS5240 < _M0L6n__preS1700) {
      int32_t _M0L3valS5275 = _M0L1jS1706->$0;
      int32_t _M0L5startS1713;
      int32_t _M0L3valS5274;
      int32_t _M0L6_2atmpS5273;
      int32_t _M0L3endS1715;
      int32_t _M0L3valS5272;
      int32_t _M0L10pre__firedS1716;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5270;
      int32_t _M0L3valS5271;
      float _M0L7tpre__jS1717;
      struct _M0TPB8MutLocalGiE* _M0L1sS1718;
      int32_t _M0L3valS5269;
      int32_t _M0L6_2atmpS5268;
      #line 358 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L5startS1713
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1714, _M0L3valS5275);
      _M0L3valS5274 = _M0L1jS1706->$0;
      _M0L6_2atmpS5273 = _M0L3valS5274 + 1;
      #line 359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L3endS1715
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1714, _M0L6_2atmpS5273);
      _M0L3valS5272 = _M0L1jS1706->$0;
      #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L10pre__firedS1716
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1701, _M0L3valS5272);
      _M0L4tpreS5270 = _M0L4varsS1707->$0;
      _M0L3valS5271 = _M0L1jS1706->$0;
      #line 361 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L7tpre__jS1717
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5270, _M0L3valS5271);
      _M0L1sS1718
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1718)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1718->$0 = _M0L5startS1713;
      while (1) {
        int32_t _M0L3valS5241 = _M0L1sS1718->$0;
        if (_M0L3valS5241 < _M0L3endS1715) {
          int32_t _M0L3valS5267 = _M0L1sS1718->$0;
          int32_t _M0L9post__idxS1719;
          int32_t _M0L11post__firedS1721;
          struct _M0TPB5ArrayGfE* _M0L5tpostS5266;
          float _M0L8tpost__iS1722;
          int32_t _M0L3valS5256;
          float _M0L6_2atmpS5254;
          float _M0L6w__minS5255;
          int32_t _M0L3valS5261;
          float _M0L6_2atmpS5259;
          float _M0L6w__maxS5260;
          int32_t _M0L3valS5265;
          int32_t _M0L6_2atmpS5264;
          #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L9post__idxS1719
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1720, _M0L3valS5267);
          #line 365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L11post__firedS1721
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1703, _M0L9post__idxS1719);
          _M0L5tpostS5266 = _M0L4varsS1707->$1;
          #line 366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L8tpost__iS1722
          = _M0MPC15array5Array2atGfE(_M0L5tpostS5266, _M0L9post__idxS1719);
          if (_M0L10pre__firedS1716) {
            float _M0L3etaS5246 = _M0L5paramS1705->$0;
            float _M0L2v0S5248 = _M0L5paramS1705->$1;
            float _M0L6_2atmpS5247 = _M0L8tpost__iS1722 - _M0L2v0S5248;
            float _M0L2dwS1723 = _M0L3etaS5246 * _M0L6_2atmpS5247;
            int32_t _M0L3valS5242 = _M0L1sS1718->$0;
            int32_t _M0L3valS5245 = _M0L1sS1718->$0;
            float _M0L6_2atmpS5244;
            float _M0L6_2atmpS5243;
            #line 369 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5244
            = _M0MPC15array5Array2atGfE(_M0L1wS1724, _M0L3valS5245);
            _M0L6_2atmpS5243 = _M0L6_2atmpS5244 + _M0L2dwS1723;
            #line 369 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1724, _M0L3valS5242, _M0L6_2atmpS5243);
          }
          if (_M0L11post__firedS1721) {
            float _M0L3etaS5253 = _M0L5paramS1705->$0;
            float _M0L2dwS1725 = _M0L3etaS5253 * _M0L7tpre__jS1717;
            int32_t _M0L3valS5249 = _M0L1sS1718->$0;
            int32_t _M0L3valS5252 = _M0L1sS1718->$0;
            float _M0L6_2atmpS5251;
            float _M0L6_2atmpS5250;
            #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5251
            = _M0MPC15array5Array2atGfE(_M0L1wS1724, _M0L3valS5252);
            _M0L6_2atmpS5250 = _M0L6_2atmpS5251 + _M0L2dwS1725;
            #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1724, _M0L3valS5249, _M0L6_2atmpS5250);
          }
          _M0L3valS5256 = _M0L1sS1718->$0;
          #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5254
          = _M0MPC15array5Array2atGfE(_M0L1wS1724, _M0L3valS5256);
          _M0L6w__minS5255 = _M0L5paramS1705->$4;
          if (_M0L6_2atmpS5254 < _M0L6w__minS5255) {
            int32_t _M0L3valS5257 = _M0L1sS1718->$0;
            float _M0L6w__minS5258 = _M0L5paramS1705->$4;
            #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1724, _M0L3valS5257, _M0L6w__minS5258);
          }
          _M0L3valS5261 = _M0L1sS1718->$0;
          #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5259
          = _M0MPC15array5Array2atGfE(_M0L1wS1724, _M0L3valS5261);
          _M0L6w__maxS5260 = _M0L5paramS1705->$3;
          if (_M0L6_2atmpS5259 > _M0L6w__maxS5260) {
            int32_t _M0L3valS5262 = _M0L1sS1718->$0;
            float _M0L6w__maxS5263 = _M0L5paramS1705->$3;
            #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1724, _M0L3valS5262, _M0L6w__maxS5263);
          }
          _M0L3valS5265 = _M0L1sS1718->$0;
          _M0L6_2atmpS5264 = _M0L3valS5265 + 1;
          _M0L1sS1718->$0 = _M0L6_2atmpS5264;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1718);
        }
        break;
      }
      _M0L3valS5269 = _M0L1jS1706->$0;
      _M0L6_2atmpS5268 = _M0L3valS5269 + 1;
      _M0L1jS1706->$0 = _M0L6_2atmpS5268;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1706);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt17istdp__rate__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1695,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1673,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1675,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1691,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1685,
  struct _M0TP26RiantR8snn__mbt18IstdpRateVariables* _M0L4varsS1679,
  struct _M0TP26RiantR8snn__mbt9IstdpRate* _M0L5paramS1677,
  float _M0L6t__nowS1671,
  float _M0L2dtS1680
) {
  int32_t _M0L6n__preS1672;
  int32_t _M0L7n__postS1674;
  float _M0L6tau__yS5192;
  float _M0L11inv__tau__yS1676;
  struct _M0TPB8MutLocalGiE* _M0L1jS1678;
  struct _M0TPB8MutLocalGiE* _M0L1iS1682;
  #line 141 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L6n__preS1672 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1673);
  #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
  _M0L7n__postS1674 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1675);
  _M0L6tau__yS5192 = _M0L5paramS1677->$2;
  _M0L11inv__tau__yS1676 = 0x1p+0f / _M0L6tau__yS5192;
  _M0L1jS1678
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1678)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1678->$0 = 0;
  while (1) {
    int32_t _M0L3valS5109 = _M0L1jS1678->$0;
    if (_M0L3valS5109 < _M0L6n__preS1672) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS5110 = _M0L4varsS1679->$0;
      int32_t _M0L3valS5111 = _M0L1jS1678->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5120 = _M0L4varsS1679->$0;
      int32_t _M0L3valS5121 = _M0L1jS1678->$0;
      float _M0L6_2atmpS5113;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5118;
      int32_t _M0L3valS5119;
      float _M0L6_2atmpS5117;
      float _M0L6_2atmpS5116;
      float _M0L6_2atmpS5115;
      float _M0L6_2atmpS5114;
      float _M0L6_2atmpS5112;
      int32_t _M0L3valS5122;
      int32_t _M0L3valS5130;
      int32_t _M0L6_2atmpS5129;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5113
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5120, _M0L3valS5121);
      _M0L4tpreS5118 = _M0L4varsS1679->$0;
      _M0L3valS5119 = _M0L1jS1678->$0;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5117
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5118, _M0L3valS5119);
      _M0L6_2atmpS5116 = -_M0L6_2atmpS5117;
      _M0L6_2atmpS5115 = _M0L2dtS1680 * _M0L6_2atmpS5116;
      _M0L6_2atmpS5114 = _M0L6_2atmpS5115 * _M0L11inv__tau__yS1676;
      _M0L6_2atmpS5112 = _M0L6_2atmpS5113 + _M0L6_2atmpS5114;
      #line 159 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS5110, _M0L3valS5111, _M0L6_2atmpS5112);
      _M0L3valS5122 = _M0L1jS1678->$0;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1673, _M0L3valS5122)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS5123 = _M0L4varsS1679->$0;
        int32_t _M0L3valS5124 = _M0L1jS1678->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS5127 = _M0L4varsS1679->$0;
        int32_t _M0L3valS5128 = _M0L1jS1678->$0;
        float _M0L6_2atmpS5126;
        float _M0L6_2atmpS5125;
        #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5126
        = _M0MPC15array5Array2atGfE(_M0L4tpreS5127, _M0L3valS5128);
        _M0L6_2atmpS5125 = _M0L6_2atmpS5126 + 0x1p+0f;
        #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS5123, _M0L3valS5124, _M0L6_2atmpS5125);
      }
      _M0L3valS5130 = _M0L1jS1678->$0;
      _M0L6_2atmpS5129 = _M0L3valS5130 + 1;
      _M0L1jS1678->$0 = _M0L6_2atmpS5129;
      continue;
    }
    break;
  }
  _M0L1iS1682
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1682)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1682->$0 = 0;
  while (1) {
    int32_t _M0L3valS5131 = _M0L1iS1682->$0;
    if (_M0L3valS5131 < _M0L7n__postS1674) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS5132 = _M0L4varsS1679->$1;
      int32_t _M0L3valS5133 = _M0L1iS1682->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5142 = _M0L4varsS1679->$1;
      int32_t _M0L3valS5143 = _M0L1iS1682->$0;
      float _M0L6_2atmpS5135;
      struct _M0TPB5ArrayGfE* _M0L5tpostS5140;
      int32_t _M0L3valS5141;
      float _M0L6_2atmpS5139;
      float _M0L6_2atmpS5138;
      float _M0L6_2atmpS5137;
      float _M0L6_2atmpS5136;
      float _M0L6_2atmpS5134;
      int32_t _M0L3valS5144;
      int32_t _M0L3valS5152;
      int32_t _M0L6_2atmpS5151;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5135
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5142, _M0L3valS5143);
      _M0L5tpostS5140 = _M0L4varsS1679->$1;
      _M0L3valS5141 = _M0L1iS1682->$0;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L6_2atmpS5139
      = _M0MPC15array5Array2atGfE(_M0L5tpostS5140, _M0L3valS5141);
      _M0L6_2atmpS5138 = -_M0L6_2atmpS5139;
      _M0L6_2atmpS5137 = _M0L2dtS1680 * _M0L6_2atmpS5138;
      _M0L6_2atmpS5136 = _M0L6_2atmpS5137 * _M0L11inv__tau__yS1676;
      _M0L6_2atmpS5134 = _M0L6_2atmpS5135 + _M0L6_2atmpS5136;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS5132, _M0L3valS5133, _M0L6_2atmpS5134);
      _M0L3valS5144 = _M0L1iS1682->$0;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1675, _M0L3valS5144)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS5145 = _M0L4varsS1679->$1;
        int32_t _M0L3valS5146 = _M0L1iS1682->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS5149 = _M0L4varsS1679->$1;
        int32_t _M0L3valS5150 = _M0L1iS1682->$0;
        float _M0L6_2atmpS5148;
        float _M0L6_2atmpS5147;
        #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0L6_2atmpS5148
        = _M0MPC15array5Array2atGfE(_M0L5tpostS5149, _M0L3valS5150);
        _M0L6_2atmpS5147 = _M0L6_2atmpS5148 + 0x1p+0f;
        #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS5145, _M0L3valS5146, _M0L6_2atmpS5147);
      }
      _M0L3valS5152 = _M0L1iS1682->$0;
      _M0L6_2atmpS5151 = _M0L3valS5152 + 1;
      _M0L1iS1682->$0 = _M0L6_2atmpS5151;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1682);
    }
    break;
  }
  _M0L1jS1678->$0 = 0;
  while (1) {
    int32_t _M0L3valS5153 = _M0L1jS1678->$0;
    if (_M0L3valS5153 < _M0L6n__preS1672) {
      int32_t _M0L3valS5191 = _M0L1jS1678->$0;
      int32_t _M0L5startS1684;
      int32_t _M0L3valS5190;
      int32_t _M0L6_2atmpS5189;
      int32_t _M0L3endS1686;
      int32_t _M0L3valS5188;
      int32_t _M0L10pre__firedS1687;
      struct _M0TPB5ArrayGfE* _M0L4tpreS5186;
      int32_t _M0L3valS5187;
      float _M0L7tpre__jS1688;
      struct _M0TPB8MutLocalGiE* _M0L1sS1689;
      int32_t _M0L3valS5185;
      int32_t _M0L6_2atmpS5184;
      #line 179 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L5startS1684
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1685, _M0L3valS5191);
      _M0L3valS5190 = _M0L1jS1678->$0;
      _M0L6_2atmpS5189 = _M0L3valS5190 + 1;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L3endS1686
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1685, _M0L6_2atmpS5189);
      _M0L3valS5188 = _M0L1jS1678->$0;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L10pre__firedS1687
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1673, _M0L3valS5188);
      _M0L4tpreS5186 = _M0L4varsS1679->$0;
      _M0L3valS5187 = _M0L1jS1678->$0;
      #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
      _M0L7tpre__jS1688
      = _M0MPC15array5Array2atGfE(_M0L4tpreS5186, _M0L3valS5187);
      _M0L1sS1689
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1689)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1689->$0 = _M0L5startS1684;
      while (1) {
        int32_t _M0L3valS5154 = _M0L1sS1689->$0;
        if (_M0L3valS5154 < _M0L3endS1686) {
          int32_t _M0L3valS5183 = _M0L1sS1689->$0;
          int32_t _M0L9post__idxS1690;
          int32_t _M0L11post__firedS1692;
          struct _M0TPB5ArrayGfE* _M0L5tpostS5182;
          float _M0L8tpost__iS1693;
          int32_t _M0L3valS5172;
          float _M0L6_2atmpS5170;
          float _M0L6w__minS5171;
          int32_t _M0L3valS5177;
          float _M0L6_2atmpS5175;
          float _M0L6w__maxS5176;
          int32_t _M0L3valS5181;
          int32_t _M0L6_2atmpS5180;
          #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L9post__idxS1690
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1691, _M0L3valS5183);
          #line 186 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L11post__firedS1692
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1675, _M0L9post__idxS1690);
          _M0L5tpostS5182 = _M0L4varsS1679->$1;
          #line 187 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L8tpost__iS1693
          = _M0MPC15array5Array2atGfE(_M0L5tpostS5182, _M0L9post__idxS1690);
          if (_M0L10pre__firedS1687) {
            float _M0L3etaS5159 = _M0L5paramS1677->$0;
            float _M0L1rS5164 = _M0L5paramS1677->$1;
            float _M0L6_2atmpS5162 = 0x1p+1f * _M0L1rS5164;
            float _M0L6tau__yS5163 = _M0L5paramS1677->$2;
            float _M0L6_2atmpS5161 = _M0L6_2atmpS5162 * _M0L6tau__yS5163;
            float _M0L6_2atmpS5160 = _M0L8tpost__iS1693 - _M0L6_2atmpS5161;
            float _M0L2dwS1694 = _M0L3etaS5159 * _M0L6_2atmpS5160;
            int32_t _M0L3valS5155 = _M0L1sS1689->$0;
            int32_t _M0L3valS5158 = _M0L1sS1689->$0;
            float _M0L6_2atmpS5157;
            float _M0L6_2atmpS5156;
            #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5157
            = _M0MPC15array5Array2atGfE(_M0L1wS1695, _M0L3valS5158);
            _M0L6_2atmpS5156 = _M0L6_2atmpS5157 + _M0L2dwS1694;
            #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1695, _M0L3valS5155, _M0L6_2atmpS5156);
          }
          if (_M0L11post__firedS1692) {
            float _M0L3etaS5169 = _M0L5paramS1677->$0;
            float _M0L2dwS1696 = _M0L3etaS5169 * _M0L7tpre__jS1688;
            int32_t _M0L3valS5165 = _M0L1sS1689->$0;
            int32_t _M0L3valS5168 = _M0L1sS1689->$0;
            float _M0L6_2atmpS5167;
            float _M0L6_2atmpS5166;
            #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0L6_2atmpS5167
            = _M0MPC15array5Array2atGfE(_M0L1wS1695, _M0L3valS5168);
            _M0L6_2atmpS5166 = _M0L6_2atmpS5167 + _M0L2dwS1696;
            #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1695, _M0L3valS5165, _M0L6_2atmpS5166);
          }
          _M0L3valS5172 = _M0L1sS1689->$0;
          #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5170
          = _M0MPC15array5Array2atGfE(_M0L1wS1695, _M0L3valS5172);
          _M0L6w__minS5171 = _M0L5paramS1677->$4;
          if (_M0L6_2atmpS5170 < _M0L6w__minS5171) {
            int32_t _M0L3valS5173 = _M0L1sS1689->$0;
            float _M0L6w__minS5174 = _M0L5paramS1677->$4;
            #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1695, _M0L3valS5173, _M0L6w__minS5174);
          }
          _M0L3valS5177 = _M0L1sS1689->$0;
          #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
          _M0L6_2atmpS5175
          = _M0MPC15array5Array2atGfE(_M0L1wS1695, _M0L3valS5177);
          _M0L6w__maxS5176 = _M0L5paramS1677->$3;
          if (_M0L6_2atmpS5175 > _M0L6w__maxS5176) {
            int32_t _M0L3valS5178 = _M0L1sS1689->$0;
            float _M0L6w__maxS5179 = _M0L5paramS1677->$3;
            #line 198 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\istdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1695, _M0L3valS5178, _M0L6w__maxS5179);
          }
          _M0L3valS5181 = _M0L1sS1689->$0;
          _M0L6_2atmpS5180 = _M0L3valS5181 + 1;
          _M0L1sS1689->$0 = _M0L6_2atmpS5180;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1689);
        }
        break;
      }
      _M0L3valS5185 = _M0L1jS1678->$0;
      _M0L6_2atmpS5184 = _M0L3valS5185 + 1;
      _M0L1jS1678->$0 = _M0L6_2atmpS5184;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1678);
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt11IFParameter* _M0MP26RiantR8snn__mbt11IFParameter3new(
  
) {
  float _M0L1cS1669;
  float _M0L2glS1670;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _block_5801;
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1cS1669 = -0x1p+0f;
  _M0L2glS1670 = -0x1p+0f;
  _block_5801
  = (struct _M0TP26RiantR8snn__mbt11IFParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt11IFParameter));
  Moonbit_object_header(_block_5801)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5801->$0 = _M0L1cS1669;
  _block_5801->$1 = _M0L2glS1670;
  _block_5801->$2 = 0x1.ep+3f;
  _block_5801->$3 = -0x1.9p+5f;
  _block_5801->$4 = -0x1.ep+5f;
  _block_5801->$5 = -0x1.18p+6f;
  _block_5801->$6 = 0x1.eb851eb851eb8p-5f;
  _block_5801->$7 = 0x1p+1f;
  _block_5801->$8 = 0x0p+0f;
  _block_5801->$9 = 0x0p+0f;
  _block_5801->$10 = 0x0p+0f;
  return _block_5801;
}

struct _M0TP26RiantR8snn__mbt2IF* _M0MP26RiantR8snn__mbt2IF3new(
  int32_t _M0L1nS1643,
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L5paramS1645,
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS1648
) {
  struct _M0TPB5ArrayGfE* _M0L1vS1642;
  float _M0L2vtS5107;
  float _M0L2vrS5108;
  float _M0L6spreadS1644;
  int32_t _M0L7_2abindS1646;
  int32_t _M0L1kS1647;
  struct _M0TPB5ArrayGfE* _M0L1wS1650;
  struct _M0TPB5ArrayGbE* _M0L4fireS1651;
  struct _M0TPB5ArrayGiE* _M0L4tabsS1652;
  struct _M0TPB5ArrayGfE* _M0L1iS1653;
  struct _M0TPB5ArrayGfE* _M0L9syn__currS1654;
  struct _M0TPB5ArrayGfE* _M0L2geS1655;
  struct _M0TPB5ArrayGfE* _M0L2giS1656;
  struct _M0TPB5ArrayGfE* _M0L2heS1657;
  struct _M0TPB5ArrayGfE* _M0L2hiS1658;
  struct _M0TPB5ArrayGfE* _M0L3gluS1659;
  struct _M0TPB5ArrayGfE* _M0L4gabaS1660;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__eS1661;
  struct _M0TPB5ArrayGfE* _M0L7gsyn__iS1662;
  float _M0L4e__eS1663;
  float _M0L4e__iS1664;
  float _M0L3treS1665;
  float _M0L3tdeS1666;
  float _M0L3triS1667;
  float _M0L3tdiS1668;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L6_2atmpS5106;
  struct _M0TP26RiantR8snn__mbt2IF* _block_5803;
  #line 113 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  #line 114 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1vS1642 = _M0MPC15array5Array4makeGfE(_M0L1nS1643, 0x0p+0f);
  _M0L2vtS5107 = _M0L5paramS1645->$3;
  _M0L2vrS5108 = _M0L5paramS1645->$4;
  _M0L6spreadS1644 = _M0L2vtS5107 - _M0L2vrS5108;
  _M0L7_2abindS1646 = 0;
  _M0L1kS1647 = _M0L7_2abindS1646;
  while (1) {
    if (_M0L1kS1647 < _M0L1nS1643) {
      float _M0L2vrS5102 = _M0L5paramS1645->$4;
      float _M0L6_2atmpS5104;
      float _M0L6_2atmpS5103;
      float _M0L6_2atmpS5101;
      int32_t _M0L6_2atmpS5105;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS5104 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS1648);
      _M0L6_2atmpS5103 = _M0L6_2atmpS5104 * _M0L6spreadS1644;
      _M0L6_2atmpS5101 = _M0L2vrS5102 + _M0L6_2atmpS5103;
      #line 117 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS1642, _M0L1kS1647, _M0L6_2atmpS5101);
      _M0L6_2atmpS5105 = _M0L1kS1647 + 1;
      _M0L1kS1647 = _M0L6_2atmpS5105;
      continue;
    }
    break;
  }
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1wS1650 = _M0MPC15array5Array4makeGfE(_M0L1nS1643, 0x0p+0f);
  #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4fireS1651 = _M0MPC15array5Array4makeGbE(_M0L1nS1643, 0);
  #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4tabsS1652 = _M0MPC15array5Array4makeGiE(_M0L1nS1643, 0);
  #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1iS1653 = _M0MPC15array5Array4makeGfE(_M0L1nS1643, 0x0p+0f);
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L9syn__currS1654 = _M0MPC15array5Array4makeGfE(_M0L1nS1643, 0x0p+0f);
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2geS1655 = _M0MPC15array5Array4makeGfE(_M0L1nS1643, 0x0p+0f);
  #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2giS1656 = _M0MPC15array5Array4makeGfE(_M0L1nS1643, 0x0p+0f);
  #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2heS1657 = _M0MPC15array5Array4makeGfE(_M0L1nS1643, 0x0p+0f);
  #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L2hiS1658 = _M0MPC15array5Array4makeGfE(_M0L1nS1643, 0x0p+0f);
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L3gluS1659 = _M0MPC15array5Array4makeGfE(_M0L1nS1643, 0x0p+0f);
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L4gabaS1660 = _M0MPC15array5Array4makeGfE(_M0L1nS1643, 0x0p+0f);
  #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__eS1661 = _M0MPC15array5Array4makeGfE(_M0L1nS1643, 0x1p+0f);
  #line 131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L7gsyn__iS1662 = _M0MPC15array5Array4makeGfE(_M0L1nS1643, 0x1p+0f);
  _M0L4e__eS1663 = 0x0p+0f;
  _M0L4e__iS1664 = -0x1.2cp+6f;
  _M0L3treS1665 = 0x1p+0f;
  _M0L3tdeS1666 = 0x1.8p+2f;
  _M0L3triS1667 = 0x1p-1f;
  _M0L3tdiS1668 = 0x1p+1f;
  #line 138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L6_2atmpS5106 = _M0MP26RiantR8snn__mbt9PostSpike3new();
  moonbit_incref_cycle_free(_M0L5paramS1645);
  _block_5803
  = (struct _M0TP26RiantR8snn__mbt2IF*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt2IF));
  Moonbit_object_header(_block_5803)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 39, 0);
  _block_5803->$0 = _M0L5paramS1645;
  _block_5803->$1 = _M0L6_2atmpS5106;
  _block_5803->$2 = _M0L1nS1643;
  _block_5803->$3 = _M0L1vS1642;
  _block_5803->$4 = _M0L1wS1650;
  _block_5803->$5 = _M0L4fireS1651;
  _block_5803->$6 = _M0L4tabsS1652;
  _block_5803->$7 = _M0L1iS1653;
  _block_5803->$8 = _M0L9syn__currS1654;
  _block_5803->$9 = _M0L2geS1655;
  _block_5803->$10 = _M0L2giS1656;
  _block_5803->$11 = _M0L2heS1657;
  _block_5803->$12 = _M0L2hiS1658;
  _block_5803->$13 = _M0L3gluS1659;
  _block_5803->$14 = _M0L4gabaS1660;
  _block_5803->$15 = _M0L7gsyn__eS1661;
  _block_5803->$16 = _M0L7gsyn__iS1662;
  _block_5803->$17 = _M0L4e__eS1663;
  _block_5803->$18 = _M0L4e__iS1664;
  _block_5803->$19 = _M0L3treS1665;
  _block_5803->$20 = _M0L3tdeS1666;
  _block_5803->$21 = _M0L3triS1667;
  _block_5803->$22 = _M0L3tdiS1668;
  return _block_5803;
}

struct _M0TP26RiantR8snn__mbt9PostSpike* _M0MP26RiantR8snn__mbt9PostSpike3new(
  
) {
  struct _M0TP26RiantR8snn__mbt9PostSpike* _block_5804;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _block_5804
  = (struct _M0TP26RiantR8snn__mbt9PostSpike*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt9PostSpike));
  Moonbit_object_header(_block_5804)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5804->$0 = 0x1p+1f;
  return _block_5804;
}

int32_t _M0FP26RiantR8snn__mbt14integrate__any(
  void* _M0L1pS1623,
  float _M0L2dtS1606
) {
  struct _M0TP26RiantR8snn__mbt6HetRec* _M0L1xS1605;
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L1xS1608;
  struct _M0TP26RiantR8snn__mbt7Poisson* _M0L1xS1610;
  struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L1xS1612;
  struct _M0TP26RiantR8snn__mbt2HH* _M0L1xS1614;
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1xS1616;
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1xS1618;
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1xS1620;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1xS1622;
  #line 30 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  switch (Moonbit_object_tag(_M0L1pS1623)) {
    case 0: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__* _M0L7_2aIF__S1624 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4IF__*)_M0L1pS1623;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4_2axS1625 =
        _M0L7_2aIF__S1624->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1625);
      _M0L1xS1622 = _M0L4_2axS1625;
      goto join_1621;
      break;
    }
    
    case 1: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop6AdEx__* _M0L9_2aAdEx__S1626 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop6AdEx__*)_M0L1pS1623;
      struct _M0TP26RiantR8snn__mbt4AdEx* _M0L4_2axS1627 =
        _M0L9_2aAdEx__S1626->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1627);
      _M0L1xS1620 = _M0L4_2axS1627;
      goto join_1619;
      break;
    }
    
    case 2: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop12AdExSinExp__* _M0L15_2aAdExSinExp__S1628 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop12AdExSinExp__*)_M0L1pS1623;
      struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L4_2axS1629 =
        _M0L15_2aAdExSinExp__S1628->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1629);
      _M0L1xS1618 = _M0L4_2axS1629;
      goto join_1617;
      break;
    }
    
    case 3: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4IZ__* _M0L7_2aIZ__S1630 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4IZ__*)_M0L1pS1623;
      struct _M0TP26RiantR8snn__mbt2IZ* _M0L4_2axS1631 =
        _M0L7_2aIZ__S1630->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1631);
      _M0L1xS1616 = _M0L4_2axS1631;
      goto join_1615;
      break;
    }
    
    case 4: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__* _M0L7_2aHH__S1632 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4HH__*)_M0L1pS1623;
      struct _M0TP26RiantR8snn__mbt2HH* _M0L4_2axS1633 =
        _M0L7_2aHH__S1632->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1633);
      _M0L1xS1614 = _M0L4_2axS1633;
      goto join_1613;
      break;
    }
    
    case 5: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__* _M0L7_2aML__S1634 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4ML__*)_M0L1pS1623;
      struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L4_2axS1635 =
        _M0L7_2aML__S1634->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1635);
      _M0L1xS1612 = _M0L4_2axS1635;
      goto join_1611;
      break;
    }
    
    case 6: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__* _M0L12_2aPoisson__S1636 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop9Poisson__*)_M0L1pS1623;
      struct _M0TP26RiantR8snn__mbt7Poisson* _M0L4_2axS1637 =
        _M0L12_2aPoisson__S1636->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1637);
      _M0L1xS1610 = _M0L4_2axS1637;
      goto join_1609;
      break;
    }
    
    case 7: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop4WC__* _M0L7_2aWC__S1638 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop4WC__*)_M0L1pS1623;
      struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L4_2axS1639 =
        _M0L7_2aWC__S1638->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1639);
      _M0L1xS1608 = _M0L4_2axS1639;
      goto join_1607;
      break;
    }
    default: {
      struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__* _M0L11_2aHetRec__S1640 =
        (struct _M0DTP26RiantR8snn__mbt6AnyPop8HetRec__*)_M0L1pS1623;
      struct _M0TP26RiantR8snn__mbt6HetRec* _M0L4_2axS1641 =
        _M0L11_2aHetRec__S1640->$0;
      moonbit_incref_cycle_free(_M0L4_2axS1641);
      _M0L1xS1605 = _M0L4_2axS1641;
      goto join_1604;
      break;
    }
  }
  goto joinlet_5813;
  join_1621:;
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt14step__synapses(_M0L1xS1622, _M0L2dtS1606);
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt17synaptic__current(_M0L1xS1622);
  #line 35 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt12step__neuron(_M0L1xS1622, _M0L2dtS1606);
  moonbit_decref_cycle_free(_M0L1xS1622);
  joinlet_5813:;
  goto joinlet_5812;
  join_1619:;
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt20adex__step__synapses(_M0L1xS1620, _M0L2dtS1606);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt23adex__synaptic__current(_M0L1xS1620);
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt10step__adex(_M0L1xS1620, _M0L2dtS1606);
  moonbit_decref_cycle_free(_M0L1xS1620);
  joinlet_5812:;
  goto joinlet_5811;
  join_1617:;
  #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(_M0L1xS1618, _M0L2dtS1606);
  #line 44 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(_M0L1xS1618);
  #line 45 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt18step__adex__sinexp(_M0L1xS1618, _M0L2dtS1606);
  moonbit_decref_cycle_free(_M0L1xS1618);
  joinlet_5811:;
  goto joinlet_5810;
  join_1615:;
  #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__iz(_M0L1xS1616, _M0L2dtS1606);
  moonbit_decref_cycle_free(_M0L1xS1616);
  joinlet_5810:;
  goto joinlet_5809;
  join_1613:;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__hh(_M0L1xS1614, _M0L2dtS1606);
  moonbit_decref_cycle_free(_M0L1xS1614);
  joinlet_5809:;
  goto joinlet_5808;
  join_1611:;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__ml(_M0L1xS1612, _M0L2dtS1606);
  moonbit_decref_cycle_free(_M0L1xS1612);
  joinlet_5808:;
  goto joinlet_5807;
  join_1609:;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt13step__poisson(_M0L1xS1610, _M0L2dtS1606);
  moonbit_decref_cycle_free(_M0L1xS1610);
  joinlet_5807:;
  goto joinlet_5806;
  join_1607:;
  #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt8step__wc(_M0L1xS1608, _M0L2dtS1606);
  moonbit_decref_cycle_free(_M0L1xS1608);
  joinlet_5806:;
  goto joinlet_5805;
  join_1604:;
  #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\population.mbt"
  _M0FP26RiantR8snn__mbt12step__hetrec(_M0L1xS1605, _M0L2dtS1606);
  moonbit_decref_cycle_free(_M0L1xS1605);
  joinlet_5805:;
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__wc(
  struct _M0TP26RiantR8snn__mbt11WilsonCowan* _M0L1pS1599,
  float _M0L2dtS1602
) {
  int32_t _M0L1nS1598;
  int32_t _M0L7_2abindS1600;
  int32_t _M0L1kS1601;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
  _M0L1nS1598 = _M0L1pS1599->$1;
  _M0L7_2abindS1600 = 0;
  _M0L1kS1601 = _M0L7_2abindS1600;
  while (1) {
    if (_M0L1kS1601 < _M0L1nS1598) {
      struct _M0TPB5ArrayGfE* _M0L1xS5081 = _M0L1pS1599->$2;
      struct _M0TPB5ArrayGfE* _M0L1xS5094 = _M0L1pS1599->$2;
      float _M0L6_2atmpS5083;
      struct _M0TPB5ArrayGfE* _M0L1xS5093;
      float _M0L6_2atmpS5092;
      float _M0L6_2atmpS5089;
      struct _M0TPB5ArrayGfE* _M0L1gS5091;
      float _M0L6_2atmpS5090;
      float _M0L6_2atmpS5086;
      struct _M0TPB5ArrayGfE* _M0L1iS5088;
      float _M0L6_2atmpS5087;
      float _M0L6_2atmpS5085;
      float _M0L6_2atmpS5084;
      float _M0L6_2atmpS5082;
      struct _M0TPB5ArrayGfE* _M0L1rS5095;
      struct _M0TPB5ArrayGfE* _M0L1xS5098;
      float _M0L6_2atmpS5097;
      float _M0L6_2atmpS5096;
      struct _M0TPB5ArrayGfE* _M0L1gS5099;
      int32_t _M0L6_2atmpS5100;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5083 = _M0MPC15array5Array2atGfE(_M0L1xS5094, _M0L1kS1601);
      _M0L1xS5093 = _M0L1pS1599->$2;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5092 = _M0MPC15array5Array2atGfE(_M0L1xS5093, _M0L1kS1601);
      _M0L6_2atmpS5089 = -_M0L6_2atmpS5092;
      _M0L1gS5091 = _M0L1pS1599->$4;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5090 = _M0MPC15array5Array2atGfE(_M0L1gS5091, _M0L1kS1601);
      _M0L6_2atmpS5086 = _M0L6_2atmpS5089 + _M0L6_2atmpS5090;
      _M0L1iS5088 = _M0L1pS1599->$5;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5087 = _M0MPC15array5Array2atGfE(_M0L1iS5088, _M0L1kS1601);
      _M0L6_2atmpS5085 = _M0L6_2atmpS5086 + _M0L6_2atmpS5087;
      _M0L6_2atmpS5084 = _M0L2dtS1602 * _M0L6_2atmpS5085;
      _M0L6_2atmpS5082 = _M0L6_2atmpS5083 + _M0L6_2atmpS5084;
      #line 65 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS5081, _M0L1kS1601, _M0L6_2atmpS5082);
      _M0L1rS5095 = _M0L1pS1599->$3;
      _M0L1xS5098 = _M0L1pS1599->$2;
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5097 = _M0MPC15array5Array2atGfE(_M0L1xS5098, _M0L1kS1601);
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0L6_2atmpS5096 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS5097);
      #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1rS5095, _M0L1kS1601, _M0L6_2atmpS5096);
      _M0L1gS5099 = _M0L1pS1599->$4;
      #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_wc.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS5099, _M0L1kS1601, 0x0p+0f);
      _M0L6_2atmpS5100 = _M0L1kS1601 + 1;
      _M0L1kS1601 = _M0L6_2atmpS5100;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13step__poisson(
  struct _M0TP26RiantR8snn__mbt7Poisson* _M0L1pS1591,
  float _M0L2dtS1593
) {
  int32_t _M0L1nS1590;
  struct _M0TP26RiantR8snn__mbt20PoissonHomoParameter* _M0L5paramS5080;
  float _M0L4rateS5079;
  float _M0L8rate__dtS1592;
  int32_t _M0L7_2abindS1594;
  int32_t _M0L1iS1595;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
  _M0L1nS1590 = _M0L1pS1591->$1;
  _M0L5paramS5080 = _M0L1pS1591->$0;
  _M0L4rateS5079 = _M0L5paramS5080->$0;
  _M0L8rate__dtS1592 = _M0L4rateS5079 * _M0L2dtS1593;
  _M0L7_2abindS1594 = 0;
  _M0L1iS1595 = _M0L7_2abindS1594;
  while (1) {
    if (_M0L1iS1595 < _M0L1nS1590) {
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS5077 = _M0L1pS1591->$4;
      float _M0L1uS1596;
      struct _M0TPB5ArrayGfE* _M0L9randcacheS5074;
      struct _M0TPB5ArrayGbE* _M0L4fireS5075;
      int32_t _M0L6_2atmpS5076;
      int32_t _M0L6_2atmpS5078;
      #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0L1uS1596 = _M0FP26RiantR8snn__mbt9next__f32(_M0L3rngS5077);
      _M0L9randcacheS5074 = _M0L1pS1591->$3;
      #line 53 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0MPC15array5Array3setGfE(_M0L9randcacheS5074, _M0L1iS1595, _M0L1uS1596);
      _M0L4fireS5075 = _M0L1pS1591->$2;
      _M0L6_2atmpS5076 = _M0L1uS1596 < _M0L8rate__dtS1592;
      #line 54 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_poisson.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS5075, _M0L1iS1595, _M0L6_2atmpS5076);
      _M0L6_2atmpS5078 = _M0L1iS1595 + 1;
      _M0L1iS1595 = _M0L6_2atmpS5078;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__ml(
  struct _M0TP26RiantR8snn__mbt11MorrisLecar* _M0L1pS1550,
  float _M0L2dtS1579
) {
  int32_t _M0L1nS1549;
  struct _M0TP26RiantR8snn__mbt20MorrisLecarParameter* _M0L3p__S1551;
  float _M0L2cmS1552;
  float _M0L2elS1553;
  float _M0L2ekS1554;
  float _M0L3ecaS1555;
  float _M0L2glS1556;
  float _M0L2gkS1557;
  float _M0L3gcaS1558;
  float _M0L6tau__eS1559;
  float _M0L6tau__iS1560;
  float _M0L2v1S1561;
  float _M0L2v2S1562;
  float _M0L2v3S1563;
  float _M0L2v4S1564;
  float _M0L3phiS1565;
  float _M0L4e__eS1566;
  float _M0L4e__iS1567;
  int32_t _M0L7_2abindS1568;
  int32_t _M0L1iS1569;
  int32_t _M0L7_2abindS1581;
  int32_t _M0L1iS1582;
  int32_t _M0L7_2abindS1584;
  int32_t _M0L1iS1585;
  int32_t _M0L7_2abindS1587;
  int32_t _M0L1iS1588;
  #line 86 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
  _M0L1nS1549 = _M0L1pS1550->$1;
  _M0L3p__S1551 = _M0L1pS1550->$0;
  _M0L2cmS1552 = _M0L3p__S1551->$0;
  _M0L2elS1553 = _M0L3p__S1551->$1;
  _M0L2ekS1554 = _M0L3p__S1551->$2;
  _M0L3ecaS1555 = _M0L3p__S1551->$3;
  _M0L2glS1556 = _M0L3p__S1551->$4;
  _M0L2gkS1557 = _M0L3p__S1551->$5;
  _M0L3gcaS1558 = _M0L3p__S1551->$6;
  _M0L6tau__eS1559 = _M0L3p__S1551->$7;
  _M0L6tau__iS1560 = _M0L3p__S1551->$8;
  _M0L2v1S1561 = _M0L3p__S1551->$9;
  _M0L2v2S1562 = _M0L3p__S1551->$10;
  _M0L2v3S1563 = _M0L3p__S1551->$11;
  _M0L2v4S1564 = _M0L3p__S1551->$12;
  _M0L3phiS1565 = _M0L3p__S1551->$13;
  _M0L4e__eS1566 = _M0L3p__S1551->$14;
  _M0L4e__iS1567 = _M0L3p__S1551->$15;
  _M0L7_2abindS1568 = 0;
  _M0L1iS1569 = _M0L7_2abindS1568;
  while (1) {
    if (_M0L1iS1569 < _M0L1nS1549) {
      struct _M0TPB5ArrayGfE* _M0L1vS5028 = _M0L1pS1550->$2;
      float _M0L1vS1570;
      struct _M0TPB5ArrayGfE* _M0L1wS5027;
      float _M0L1wS1571;
      float _M0L6_2atmpS5026;
      float _M0L6_2atmpS5025;
      float _M0L6_2atmpS5024;
      float _M0L6_2atmpS5023;
      float _M0L5m__ssS1572;
      struct _M0TPB5ArrayGfE* _M0L1iS5022;
      float _M0L6_2atmpS5019;
      float _M0L6_2atmpS5021;
      float _M0L6_2atmpS5020;
      float _M0L6_2atmpS5015;
      float _M0L6_2atmpS5018;
      float _M0L6_2atmpS5017;
      float _M0L6_2atmpS5016;
      float _M0L6_2atmpS5011;
      float _M0L6_2atmpS5014;
      float _M0L6_2atmpS5013;
      float _M0L6_2atmpS5012;
      float _M0L2dvS1573;
      float _M0L6_2atmpS5010;
      float _M0L6_2atmpS5009;
      float _M0L6_2atmpS5008;
      float _M0L6_2atmpS5007;
      float _M0L5n__ssS1574;
      float _M0L6_2atmpS5005;
      float _M0L6_2atmpS5006;
      float _M0L9cosh__argS1575;
      float _M0L6_2atmpS5002;
      float _M0L6_2atmpS5004;
      float _M0L6_2atmpS5003;
      float _M0L6_2atmpS5001;
      float _M0L9cosh__valS1576;
      float _M0L6_2atmpS4999;
      float _M0L3tauS1577;
      float _M0L6_2atmpS4998;
      float _M0L2dwS1578;
      struct _M0TPB5ArrayGfE* _M0L1vS4991;
      float _M0L6_2atmpS4994;
      float _M0L6_2atmpS4993;
      float _M0L6_2atmpS4992;
      struct _M0TPB5ArrayGfE* _M0L1wS4995;
      float _M0L6_2atmpS4997;
      float _M0L6_2atmpS4996;
      int32_t _M0L6_2atmpS5029;
      #line 106 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L1vS1570 = _M0MPC15array5Array2atGfE(_M0L1vS5028, _M0L1iS1569);
      _M0L1wS5027 = _M0L1pS1550->$3;
      #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L1wS1571 = _M0MPC15array5Array2atGfE(_M0L1wS5027, _M0L1iS1569);
      _M0L6_2atmpS5026 = _M0L1vS1570 - _M0L2v1S1561;
      _M0L6_2atmpS5025 = _M0L6_2atmpS5026 / _M0L2v2S1562;
      #line 109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5024 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS5025);
      _M0L6_2atmpS5023 = 0x1p+0f + _M0L6_2atmpS5024;
      _M0L5m__ssS1572 = 0x1p-1f * _M0L6_2atmpS5023;
      _M0L1iS5022 = _M0L1pS1550->$5;
      #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5019 = _M0MPC15array5Array2atGfE(_M0L1iS5022, _M0L1iS1569);
      _M0L6_2atmpS5021 = _M0L2elS1553 - _M0L1vS1570;
      _M0L6_2atmpS5020 = _M0L2glS1556 * _M0L6_2atmpS5021;
      _M0L6_2atmpS5015 = _M0L6_2atmpS5019 + _M0L6_2atmpS5020;
      _M0L6_2atmpS5018 = _M0L3ecaS1555 - _M0L1vS1570;
      _M0L6_2atmpS5017 = _M0L3gcaS1558 * _M0L6_2atmpS5018;
      _M0L6_2atmpS5016 = _M0L6_2atmpS5017 * _M0L5m__ssS1572;
      _M0L6_2atmpS5011 = _M0L6_2atmpS5015 + _M0L6_2atmpS5016;
      _M0L6_2atmpS5014 = _M0L2ekS1554 - _M0L1vS1570;
      _M0L6_2atmpS5013 = _M0L2gkS1557 * _M0L6_2atmpS5014;
      _M0L6_2atmpS5012 = _M0L6_2atmpS5013 * _M0L1wS1571;
      _M0L2dvS1573 = _M0L6_2atmpS5011 + _M0L6_2atmpS5012;
      _M0L6_2atmpS5010 = _M0L1vS1570 - _M0L2v3S1563;
      _M0L6_2atmpS5009 = _M0L6_2atmpS5010 / _M0L2v4S1564;
      #line 112 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5008 = _M0FP26RiantR8snn__mbt5tanhf(_M0L6_2atmpS5009);
      _M0L6_2atmpS5007 = 0x1p+0f + _M0L6_2atmpS5008;
      _M0L5n__ssS1574 = 0x1p-1f * _M0L6_2atmpS5007;
      _M0L6_2atmpS5005 = _M0L1vS1570 - _M0L2v3S1563;
      _M0L6_2atmpS5006 = 0x1p+1f * _M0L2v4S1564;
      _M0L9cosh__argS1575 = _M0L6_2atmpS5005 / _M0L6_2atmpS5006;
      #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5002 = _M0FP26RiantR8snn__mbt4expf(_M0L9cosh__argS1575);
      _M0L6_2atmpS5004 = -_M0L9cosh__argS1575;
      #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5003 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS5004);
      _M0L6_2atmpS5001 = _M0L6_2atmpS5002 + _M0L6_2atmpS5003;
      _M0L9cosh__valS1576 = 0x1p-1f * _M0L6_2atmpS5001;
      _M0L6_2atmpS4999 = _M0L3phiS1565 * _M0L9cosh__valS1576;
      if (_M0L6_2atmpS4999 != 0x0p+0f) {
        float _M0L6_2atmpS5000 = _M0L3phiS1565 * _M0L9cosh__valS1576;
        _M0L3tauS1577 = 0x1p+0f / _M0L6_2atmpS5000;
      } else {
        _M0L3tauS1577 = 0x0p+0f;
      }
      _M0L6_2atmpS4998 = _M0L5n__ssS1574 - _M0L1wS1571;
      _M0L2dwS1578 = _M0L6_2atmpS4998 / _M0L3tauS1577;
      _M0L1vS4991 = _M0L1pS1550->$2;
      _M0L6_2atmpS4994 = _M0L2dtS1579 / _M0L2cmS1552;
      _M0L6_2atmpS4993 = _M0L6_2atmpS4994 * _M0L2dvS1573;
      _M0L6_2atmpS4992 = _M0L1vS1570 + _M0L6_2atmpS4993;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4991, _M0L1iS1569, _M0L6_2atmpS4992);
      _M0L1wS4995 = _M0L1pS1550->$3;
      _M0L6_2atmpS4997 = _M0L2dtS1579 * _M0L2dwS1578;
      _M0L6_2atmpS4996 = _M0L1wS1571 + _M0L6_2atmpS4997;
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4995, _M0L1iS1569, _M0L6_2atmpS4996);
      _M0L6_2atmpS5029 = _M0L1iS1569 + 1;
      _M0L1iS1569 = _M0L6_2atmpS5029;
      continue;
    }
    break;
  }
  _M0L7_2abindS1581 = 0;
  _M0L1iS1582 = _M0L7_2abindS1581;
  while (1) {
    if (_M0L1iS1582 < _M0L1nS1549) {
      struct _M0TPB5ArrayGfE* _M0L1vS5030 = _M0L1pS1550->$2;
      struct _M0TPB5ArrayGfE* _M0L1vS5048 = _M0L1pS1550->$2;
      float _M0L6_2atmpS5032;
      float _M0L6_2atmpS5034;
      struct _M0TPB5ArrayGfE* _M0L2geS5047;
      float _M0L6_2atmpS5043;
      struct _M0TPB5ArrayGfE* _M0L1vS5046;
      float _M0L6_2atmpS5045;
      float _M0L6_2atmpS5044;
      float _M0L6_2atmpS5036;
      struct _M0TPB5ArrayGfE* _M0L2giS5042;
      float _M0L6_2atmpS5038;
      struct _M0TPB5ArrayGfE* _M0L1vS5041;
      float _M0L6_2atmpS5040;
      float _M0L6_2atmpS5039;
      float _M0L6_2atmpS5037;
      float _M0L6_2atmpS5035;
      float _M0L6_2atmpS5033;
      float _M0L6_2atmpS5031;
      int32_t _M0L6_2atmpS5049;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5032 = _M0MPC15array5Array2atGfE(_M0L1vS5048, _M0L1iS1582);
      _M0L6_2atmpS5034 = _M0L2dtS1579 / _M0L2cmS1552;
      _M0L2geS5047 = _M0L1pS1550->$6;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5043 = _M0MPC15array5Array2atGfE(_M0L2geS5047, _M0L1iS1582);
      _M0L1vS5046 = _M0L1pS1550->$2;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5045 = _M0MPC15array5Array2atGfE(_M0L1vS5046, _M0L1iS1582);
      _M0L6_2atmpS5044 = _M0L4e__eS1566 - _M0L6_2atmpS5045;
      _M0L6_2atmpS5036 = _M0L6_2atmpS5043 * _M0L6_2atmpS5044;
      _M0L2giS5042 = _M0L1pS1550->$7;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5038 = _M0MPC15array5Array2atGfE(_M0L2giS5042, _M0L1iS1582);
      _M0L1vS5041 = _M0L1pS1550->$2;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5040 = _M0MPC15array5Array2atGfE(_M0L1vS5041, _M0L1iS1582);
      _M0L6_2atmpS5039 = _M0L4e__iS1567 - _M0L6_2atmpS5040;
      _M0L6_2atmpS5037 = _M0L6_2atmpS5038 * _M0L6_2atmpS5039;
      _M0L6_2atmpS5035 = _M0L6_2atmpS5036 + _M0L6_2atmpS5037;
      _M0L6_2atmpS5033 = _M0L6_2atmpS5034 * _M0L6_2atmpS5035;
      _M0L6_2atmpS5031 = _M0L6_2atmpS5032 + _M0L6_2atmpS5033;
      #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS5030, _M0L1iS1582, _M0L6_2atmpS5031);
      _M0L6_2atmpS5049 = _M0L1iS1582 + 1;
      _M0L1iS1582 = _M0L6_2atmpS5049;
      continue;
    }
    break;
  }
  _M0L7_2abindS1584 = 0;
  _M0L1iS1585 = _M0L7_2abindS1584;
  while (1) {
    if (_M0L1iS1585 < _M0L1nS1549) {
      struct _M0TPB5ArrayGfE* _M0L2geS5050 = _M0L1pS1550->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS5058 = _M0L1pS1550->$6;
      float _M0L6_2atmpS5052;
      struct _M0TPB5ArrayGfE* _M0L2geS5057;
      float _M0L6_2atmpS5056;
      float _M0L6_2atmpS5055;
      float _M0L6_2atmpS5054;
      float _M0L6_2atmpS5053;
      float _M0L6_2atmpS5051;
      struct _M0TPB5ArrayGfE* _M0L2giS5059;
      struct _M0TPB5ArrayGfE* _M0L2giS5067;
      float _M0L6_2atmpS5061;
      struct _M0TPB5ArrayGfE* _M0L2giS5066;
      float _M0L6_2atmpS5065;
      float _M0L6_2atmpS5064;
      float _M0L6_2atmpS5063;
      float _M0L6_2atmpS5062;
      float _M0L6_2atmpS5060;
      int32_t _M0L6_2atmpS5068;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5052 = _M0MPC15array5Array2atGfE(_M0L2geS5058, _M0L1iS1585);
      _M0L2geS5057 = _M0L1pS1550->$6;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5056 = _M0MPC15array5Array2atGfE(_M0L2geS5057, _M0L1iS1585);
      _M0L6_2atmpS5055 = -_M0L6_2atmpS5056;
      _M0L6_2atmpS5054 = _M0L6_2atmpS5055 / _M0L6tau__eS1559;
      _M0L6_2atmpS5053 = _M0L2dtS1579 * _M0L6_2atmpS5054;
      _M0L6_2atmpS5051 = _M0L6_2atmpS5052 + _M0L6_2atmpS5053;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS5050, _M0L1iS1585, _M0L6_2atmpS5051);
      _M0L2giS5059 = _M0L1pS1550->$7;
      _M0L2giS5067 = _M0L1pS1550->$7;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5061 = _M0MPC15array5Array2atGfE(_M0L2giS5067, _M0L1iS1585);
      _M0L2giS5066 = _M0L1pS1550->$7;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5065 = _M0MPC15array5Array2atGfE(_M0L2giS5066, _M0L1iS1585);
      _M0L6_2atmpS5064 = -_M0L6_2atmpS5065;
      _M0L6_2atmpS5063 = _M0L6_2atmpS5064 / _M0L6tau__iS1560;
      _M0L6_2atmpS5062 = _M0L2dtS1579 * _M0L6_2atmpS5063;
      _M0L6_2atmpS5060 = _M0L6_2atmpS5061 + _M0L6_2atmpS5062;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS5059, _M0L1iS1585, _M0L6_2atmpS5060);
      _M0L6_2atmpS5068 = _M0L1iS1585 + 1;
      _M0L1iS1585 = _M0L6_2atmpS5068;
      continue;
    }
    break;
  }
  _M0L7_2abindS1587 = 0;
  _M0L1iS1588 = _M0L7_2abindS1587;
  while (1) {
    if (_M0L1iS1588 < _M0L1nS1549) {
      struct _M0TPB5ArrayGbE* _M0L4fireS5069 = _M0L1pS1550->$4;
      struct _M0TPB5ArrayGfE* _M0L1vS5072 = _M0L1pS1550->$2;
      float _M0L6_2atmpS5071;
      int32_t _M0L6_2atmpS5070;
      int32_t _M0L6_2atmpS5073;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0L6_2atmpS5071 = _M0MPC15array5Array2atGfE(_M0L1vS5072, _M0L1iS1588);
      _M0L6_2atmpS5070 = _M0L6_2atmpS5071 > 0x1.4p+4f;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_ml.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS5069, _M0L1iS1588, _M0L6_2atmpS5070);
      _M0L6_2atmpS5073 = _M0L1iS1588 + 1;
      _M0L1iS1588 = _M0L6_2atmpS5073;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__iz(
  struct _M0TP26RiantR8snn__mbt2IZ* _M0L1pS1518,
  float _M0L2dtS1530
) {
  int32_t _M0L1nS1517;
  struct _M0TP26RiantR8snn__mbt11IZParameter* _M0L3p__S1519;
  float _M0L1aS1520;
  float _M0L1bS1521;
  float _M0L1cS1522;
  float _M0L1dS1523;
  float _M0L6tau__eS1524;
  float _M0L6tau__iS1525;
  float _M0L4e__eS1526;
  float _M0L4e__iS1527;
  int32_t _M0L7_2abindS1528;
  int32_t _M0L1iS1529;
  int32_t _M0L7_2abindS1532;
  int32_t _M0L1iS1533;
  int32_t _M0L7_2abindS1539;
  int32_t _M0L1iS1540;
  int32_t _M0L7_2abindS1543;
  int32_t _M0L1iS1544;
  int32_t _M0L7_2abindS1546;
  int32_t _M0L1iS1547;
  #line 341 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1nS1517 = _M0L1pS1518->$1;
  _M0L3p__S1519 = _M0L1pS1518->$0;
  _M0L1aS1520 = _M0L3p__S1519->$0;
  _M0L1bS1521 = _M0L3p__S1519->$1;
  _M0L1cS1522 = _M0L3p__S1519->$2;
  _M0L1dS1523 = _M0L3p__S1519->$3;
  _M0L6tau__eS1524 = _M0L3p__S1519->$4;
  _M0L6tau__iS1525 = _M0L3p__S1519->$5;
  _M0L4e__eS1526 = _M0L3p__S1519->$6;
  _M0L4e__iS1527 = _M0L3p__S1519->$7;
  _M0L7_2abindS1528 = 0;
  _M0L1iS1529 = _M0L7_2abindS1528;
  while (1) {
    if (_M0L1iS1529 < _M0L1nS1517) {
      struct _M0TPB5ArrayGfE* _M0L2geS4899 = _M0L1pS1518->$6;
      struct _M0TPB5ArrayGfE* _M0L2geS4907 = _M0L1pS1518->$6;
      float _M0L6_2atmpS4901;
      struct _M0TPB5ArrayGfE* _M0L2geS4906;
      float _M0L6_2atmpS4905;
      float _M0L6_2atmpS4904;
      float _M0L6_2atmpS4903;
      float _M0L6_2atmpS4902;
      float _M0L6_2atmpS4900;
      struct _M0TPB5ArrayGfE* _M0L2giS4908;
      struct _M0TPB5ArrayGfE* _M0L2giS4916;
      float _M0L6_2atmpS4910;
      struct _M0TPB5ArrayGfE* _M0L2giS4915;
      float _M0L6_2atmpS4914;
      float _M0L6_2atmpS4913;
      float _M0L6_2atmpS4912;
      float _M0L6_2atmpS4911;
      float _M0L6_2atmpS4909;
      int32_t _M0L6_2atmpS4917;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4901 = _M0MPC15array5Array2atGfE(_M0L2geS4907, _M0L1iS1529);
      _M0L2geS4906 = _M0L1pS1518->$6;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4905 = _M0MPC15array5Array2atGfE(_M0L2geS4906, _M0L1iS1529);
      _M0L6_2atmpS4904 = -_M0L6_2atmpS4905;
      _M0L6_2atmpS4903 = _M0L2dtS1530 * _M0L6_2atmpS4904;
      _M0L6_2atmpS4902 = _M0L6_2atmpS4903 / _M0L6tau__eS1524;
      _M0L6_2atmpS4900 = _M0L6_2atmpS4901 + _M0L6_2atmpS4902;
      #line 354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4899, _M0L1iS1529, _M0L6_2atmpS4900);
      _M0L2giS4908 = _M0L1pS1518->$7;
      _M0L2giS4916 = _M0L1pS1518->$7;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4910 = _M0MPC15array5Array2atGfE(_M0L2giS4916, _M0L1iS1529);
      _M0L2giS4915 = _M0L1pS1518->$7;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4914 = _M0MPC15array5Array2atGfE(_M0L2giS4915, _M0L1iS1529);
      _M0L6_2atmpS4913 = -_M0L6_2atmpS4914;
      _M0L6_2atmpS4912 = _M0L2dtS1530 * _M0L6_2atmpS4913;
      _M0L6_2atmpS4911 = _M0L6_2atmpS4912 / _M0L6tau__iS1525;
      _M0L6_2atmpS4909 = _M0L6_2atmpS4910 + _M0L6_2atmpS4911;
      #line 355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4908, _M0L1iS1529, _M0L6_2atmpS4909);
      _M0L6_2atmpS4917 = _M0L1iS1529 + 1;
      _M0L1iS1529 = _M0L6_2atmpS4917;
      continue;
    }
    break;
  }
  _M0L7_2abindS1532 = 0;
  _M0L1iS1533 = _M0L7_2abindS1532;
  while (1) {
    if (_M0L1iS1533 < _M0L1nS1517) {
      struct _M0TPB5ArrayGfE* _M0L1vS4943 = _M0L1pS1518->$2;
      float _M0L1vS1534;
      struct _M0TPB5ArrayGfE* _M0L1uS4942;
      float _M0L1uS1535;
      struct _M0TPB5ArrayGfE* _M0L1iS4941;
      float _M0L2iiS1536;
      struct _M0TPB5ArrayGfE* _M0L1vS4918;
      float _M0L6_2atmpS4921;
      float _M0L6_2atmpS4928;
      float _M0L6_2atmpS4926;
      float _M0L6_2atmpS4927;
      float _M0L6_2atmpS4925;
      float _M0L6_2atmpS4924;
      float _M0L6_2atmpS4923;
      float _M0L6_2atmpS4922;
      float _M0L6_2atmpS4920;
      float _M0L6_2atmpS4919;
      struct _M0TPB5ArrayGfE* _M0L1vS4940;
      float _M0L2v2S1537;
      struct _M0TPB5ArrayGfE* _M0L1vS4929;
      float _M0L6_2atmpS4932;
      float _M0L6_2atmpS4939;
      float _M0L6_2atmpS4937;
      float _M0L6_2atmpS4938;
      float _M0L6_2atmpS4936;
      float _M0L6_2atmpS4935;
      float _M0L6_2atmpS4934;
      float _M0L6_2atmpS4933;
      float _M0L6_2atmpS4931;
      float _M0L6_2atmpS4930;
      int32_t _M0L6_2atmpS4944;
      #line 359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS1534 = _M0MPC15array5Array2atGfE(_M0L1vS4943, _M0L1iS1533);
      _M0L1uS4942 = _M0L1pS1518->$3;
      #line 360 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1uS1535 = _M0MPC15array5Array2atGfE(_M0L1uS4942, _M0L1iS1533);
      _M0L1iS4941 = _M0L1pS1518->$5;
      #line 361 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2iiS1536 = _M0MPC15array5Array2atGfE(_M0L1iS4941, _M0L1iS1533);
      _M0L1vS4918 = _M0L1pS1518->$2;
      _M0L6_2atmpS4921 = 0x1p-1f * _M0L2dtS1530;
      _M0L6_2atmpS4928 = 0x1.47ae147ae147bp-5f * _M0L1vS1534;
      _M0L6_2atmpS4926 = _M0L6_2atmpS4928 * _M0L1vS1534;
      _M0L6_2atmpS4927 = 0x1.4p+2f * _M0L1vS1534;
      _M0L6_2atmpS4925 = _M0L6_2atmpS4926 + _M0L6_2atmpS4927;
      _M0L6_2atmpS4924 = _M0L6_2atmpS4925 + 0x1.18p+7f;
      _M0L6_2atmpS4923 = _M0L6_2atmpS4924 - _M0L1uS1535;
      _M0L6_2atmpS4922 = _M0L6_2atmpS4923 + _M0L2iiS1536;
      _M0L6_2atmpS4920 = _M0L6_2atmpS4921 * _M0L6_2atmpS4922;
      _M0L6_2atmpS4919 = _M0L1vS1534 + _M0L6_2atmpS4920;
      #line 362 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4918, _M0L1iS1533, _M0L6_2atmpS4919);
      _M0L1vS4940 = _M0L1pS1518->$2;
      #line 363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L2v2S1537 = _M0MPC15array5Array2atGfE(_M0L1vS4940, _M0L1iS1533);
      _M0L1vS4929 = _M0L1pS1518->$2;
      _M0L6_2atmpS4932 = 0x1p-1f * _M0L2dtS1530;
      _M0L6_2atmpS4939 = 0x1.47ae147ae147bp-5f * _M0L2v2S1537;
      _M0L6_2atmpS4937 = _M0L6_2atmpS4939 * _M0L2v2S1537;
      _M0L6_2atmpS4938 = 0x1.4p+2f * _M0L2v2S1537;
      _M0L6_2atmpS4936 = _M0L6_2atmpS4937 + _M0L6_2atmpS4938;
      _M0L6_2atmpS4935 = _M0L6_2atmpS4936 + 0x1.18p+7f;
      _M0L6_2atmpS4934 = _M0L6_2atmpS4935 - _M0L1uS1535;
      _M0L6_2atmpS4933 = _M0L6_2atmpS4934 + _M0L2iiS1536;
      _M0L6_2atmpS4931 = _M0L6_2atmpS4932 * _M0L6_2atmpS4933;
      _M0L6_2atmpS4930 = _M0L2v2S1537 + _M0L6_2atmpS4931;
      #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4929, _M0L1iS1533, _M0L6_2atmpS4930);
      _M0L6_2atmpS4944 = _M0L1iS1533 + 1;
      _M0L1iS1533 = _M0L6_2atmpS4944;
      continue;
    }
    break;
  }
  _M0L7_2abindS1539 = 0;
  _M0L1iS1540 = _M0L7_2abindS1539;
  while (1) {
    if (_M0L1iS1540 < _M0L1nS1517) {
      struct _M0TPB5ArrayGfE* _M0L1vS4955 = _M0L1pS1518->$2;
      float _M0L1vS1541;
      struct _M0TPB5ArrayGfE* _M0L1uS4945;
      struct _M0TPB5ArrayGfE* _M0L1uS4954;
      float _M0L6_2atmpS4947;
      float _M0L6_2atmpS4949;
      float _M0L6_2atmpS4951;
      struct _M0TPB5ArrayGfE* _M0L1uS4953;
      float _M0L6_2atmpS4952;
      float _M0L6_2atmpS4950;
      float _M0L6_2atmpS4948;
      float _M0L6_2atmpS4946;
      int32_t _M0L6_2atmpS4956;
      #line 367 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L1vS1541 = _M0MPC15array5Array2atGfE(_M0L1vS4955, _M0L1iS1540);
      _M0L1uS4945 = _M0L1pS1518->$3;
      _M0L1uS4954 = _M0L1pS1518->$3;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4947 = _M0MPC15array5Array2atGfE(_M0L1uS4954, _M0L1iS1540);
      _M0L6_2atmpS4949 = _M0L2dtS1530 * _M0L1aS1520;
      _M0L6_2atmpS4951 = _M0L1bS1521 * _M0L1vS1541;
      _M0L1uS4953 = _M0L1pS1518->$3;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4952 = _M0MPC15array5Array2atGfE(_M0L1uS4953, _M0L1iS1540);
      _M0L6_2atmpS4950 = _M0L6_2atmpS4951 - _M0L6_2atmpS4952;
      _M0L6_2atmpS4948 = _M0L6_2atmpS4949 * _M0L6_2atmpS4950;
      _M0L6_2atmpS4946 = _M0L6_2atmpS4947 + _M0L6_2atmpS4948;
      #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS4945, _M0L1iS1540, _M0L6_2atmpS4946);
      _M0L6_2atmpS4956 = _M0L1iS1540 + 1;
      _M0L1iS1540 = _M0L6_2atmpS4956;
      continue;
    }
    break;
  }
  _M0L7_2abindS1543 = 0;
  _M0L1iS1544 = _M0L7_2abindS1543;
  while (1) {
    if (_M0L1iS1544 < _M0L1nS1517) {
      struct _M0TPB5ArrayGfE* _M0L1vS4957 = _M0L1pS1518->$2;
      struct _M0TPB5ArrayGfE* _M0L1vS4974 = _M0L1pS1518->$2;
      float _M0L6_2atmpS4959;
      struct _M0TPB5ArrayGfE* _M0L2geS4973;
      float _M0L6_2atmpS4969;
      struct _M0TPB5ArrayGfE* _M0L1vS4972;
      float _M0L6_2atmpS4971;
      float _M0L6_2atmpS4970;
      float _M0L6_2atmpS4962;
      struct _M0TPB5ArrayGfE* _M0L2giS4968;
      float _M0L6_2atmpS4964;
      struct _M0TPB5ArrayGfE* _M0L1vS4967;
      float _M0L6_2atmpS4966;
      float _M0L6_2atmpS4965;
      float _M0L6_2atmpS4963;
      float _M0L6_2atmpS4961;
      float _M0L6_2atmpS4960;
      float _M0L6_2atmpS4958;
      int32_t _M0L6_2atmpS4975;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4959 = _M0MPC15array5Array2atGfE(_M0L1vS4974, _M0L1iS1544);
      _M0L2geS4973 = _M0L1pS1518->$6;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4969 = _M0MPC15array5Array2atGfE(_M0L2geS4973, _M0L1iS1544);
      _M0L1vS4972 = _M0L1pS1518->$2;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4971 = _M0MPC15array5Array2atGfE(_M0L1vS4972, _M0L1iS1544);
      _M0L6_2atmpS4970 = _M0L4e__eS1526 - _M0L6_2atmpS4971;
      _M0L6_2atmpS4962 = _M0L6_2atmpS4969 * _M0L6_2atmpS4970;
      _M0L2giS4968 = _M0L1pS1518->$7;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4964 = _M0MPC15array5Array2atGfE(_M0L2giS4968, _M0L1iS1544);
      _M0L1vS4967 = _M0L1pS1518->$2;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4966 = _M0MPC15array5Array2atGfE(_M0L1vS4967, _M0L1iS1544);
      _M0L6_2atmpS4965 = _M0L4e__iS1527 - _M0L6_2atmpS4966;
      _M0L6_2atmpS4963 = _M0L6_2atmpS4964 * _M0L6_2atmpS4965;
      _M0L6_2atmpS4961 = _M0L6_2atmpS4962 + _M0L6_2atmpS4963;
      _M0L6_2atmpS4960 = _M0L2dtS1530 * _M0L6_2atmpS4961;
      _M0L6_2atmpS4958 = _M0L6_2atmpS4959 + _M0L6_2atmpS4960;
      #line 371 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4957, _M0L1iS1544, _M0L6_2atmpS4958);
      _M0L6_2atmpS4975 = _M0L1iS1544 + 1;
      _M0L1iS1544 = _M0L6_2atmpS4975;
      continue;
    }
    break;
  }
  _M0L7_2abindS1546 = 0;
  _M0L1iS1547 = _M0L7_2abindS1546;
  while (1) {
    if (_M0L1iS1547 < _M0L1nS1517) {
      struct _M0TPB5ArrayGbE* _M0L4fireS4976 = _M0L1pS1518->$4;
      struct _M0TPB5ArrayGfE* _M0L1vS4979 = _M0L1pS1518->$2;
      float _M0L6_2atmpS4978;
      int32_t _M0L6_2atmpS4977;
      struct _M0TPB5ArrayGfE* _M0L1vS4980;
      struct _M0TPB5ArrayGbE* _M0L4fireS4982;
      float _M0L6_2atmpS4981;
      struct _M0TPB5ArrayGfE* _M0L1uS4984;
      struct _M0TPB5ArrayGfE* _M0L1uS4989;
      float _M0L6_2atmpS4986;
      struct _M0TPB5ArrayGbE* _M0L4fireS4988;
      float _M0L6_2atmpS4987;
      float _M0L6_2atmpS4985;
      int32_t _M0L6_2atmpS4990;
      #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4978 = _M0MPC15array5Array2atGfE(_M0L1vS4979, _M0L1iS1547);
      _M0L6_2atmpS4977 = _M0L6_2atmpS4978 > 0x1.ep+4f;
      #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4976, _M0L1iS1547, _M0L6_2atmpS4977);
      _M0L1vS4980 = _M0L1pS1518->$2;
      _M0L4fireS4982 = _M0L1pS1518->$4;
      #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4982, _M0L1iS1547)) {
        _M0L6_2atmpS4981 = _M0L1cS1522;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4983 = _M0L1pS1518->$2;
        #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
        _M0L6_2atmpS4981
        = _M0MPC15array5Array2atGfE(_M0L1vS4983, _M0L1iS1547);
      }
      #line 376 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4980, _M0L1iS1547, _M0L6_2atmpS4981);
      _M0L1uS4984 = _M0L1pS1518->$3;
      _M0L1uS4989 = _M0L1pS1518->$3;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0L6_2atmpS4986 = _M0MPC15array5Array2atGfE(_M0L1uS4989, _M0L1iS1547);
      _M0L4fireS4988 = _M0L1pS1518->$4;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4988, _M0L1iS1547)) {
        _M0L6_2atmpS4987 = _M0L1dS1523;
      } else {
        _M0L6_2atmpS4987 = 0x0p+0f;
      }
      _M0L6_2atmpS4985 = _M0L6_2atmpS4986 + _M0L6_2atmpS4987;
      #line 377 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS4984, _M0L1iS1547, _M0L6_2atmpS4985);
      _M0L6_2atmpS4990 = _M0L1iS1547 + 1;
      _M0L1iS1547 = _M0L6_2atmpS4990;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt8step__hh(
  struct _M0TP26RiantR8snn__mbt2HH* _M0L1pS1474,
  float _M0L2dtS1500
) {
  int32_t _M0L1nS1473;
  struct _M0TP26RiantR8snn__mbt11HHParameter* _M0L3p__S1475;
  float _M0L2cmS1476;
  float _M0L2glS1477;
  float _M0L2elS1478;
  float _M0L2ekS1479;
  float _M0L2enS1480;
  float _M0L2gnS1481;
  float _M0L2gkS1482;
  float _M0L2vtS1483;
  float _M0L6tau__eS1484;
  float _M0L6tau__iS1485;
  float _M0L4e__eS1486;
  float _M0L4e__iS1487;
  int32_t _M0L7_2abindS1488;
  int32_t _M0L1iS1489;
  int32_t _M0L7_2abindS1514;
  int32_t _M0L1iS1515;
  #line 108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
  _M0L1nS1473 = _M0L1pS1474->$1;
  _M0L3p__S1475 = _M0L1pS1474->$0;
  _M0L2cmS1476 = _M0L3p__S1475->$0;
  _M0L2glS1477 = _M0L3p__S1475->$1;
  _M0L2elS1478 = _M0L3p__S1475->$2;
  _M0L2ekS1479 = _M0L3p__S1475->$3;
  _M0L2enS1480 = _M0L3p__S1475->$4;
  _M0L2gnS1481 = _M0L3p__S1475->$5;
  _M0L2gkS1482 = _M0L3p__S1475->$6;
  _M0L2vtS1483 = _M0L3p__S1475->$7;
  _M0L6tau__eS1484 = _M0L3p__S1475->$8;
  _M0L6tau__iS1485 = _M0L3p__S1475->$9;
  _M0L4e__eS1486 = _M0L3p__S1475->$10;
  _M0L4e__iS1487 = _M0L3p__S1475->$11;
  _M0L7_2abindS1488 = 0;
  _M0L1iS1489 = _M0L7_2abindS1488;
  while (1) {
    if (_M0L1iS1489 < _M0L1nS1473) {
      struct _M0TPB5ArrayGfE* _M0L1vS4892 = _M0L1pS1474->$2;
      float _M0L1vS1490;
      struct _M0TPB5ArrayGfE* _M0L1mS4891;
      float _M0L1mS1491;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4890;
      float _M0L2nnS1492;
      struct _M0TPB5ArrayGfE* _M0L1hS4889;
      float _M0L1hS1493;
      struct _M0TPB5ArrayGfE* _M0L2geS4888;
      float _M0L2geS1494;
      struct _M0TPB5ArrayGfE* _M0L2giS4887;
      float _M0L2giS1495;
      struct _M0TPB5ArrayGbE* _M0L4fireS4790;
      float _M0L6_2atmpS4886;
      float _M0L7am__numS1496;
      float _M0L6_2atmpS4885;
      float _M0L7bm__numS1497;
      float _M0L6_2atmpS4880;
      float _M0L6_2atmpS4879;
      float _M0L6_2atmpS4878;
      float _M0L2amS1498;
      float _M0L6_2atmpS4873;
      float _M0L6_2atmpS4872;
      float _M0L6_2atmpS4871;
      float _M0L2bmS1499;
      struct _M0TPB5ArrayGfE* _M0L1mS4791;
      float _M0L6_2atmpS4797;
      float _M0L6_2atmpS4795;
      float _M0L6_2atmpS4796;
      float _M0L6_2atmpS4794;
      float _M0L6_2atmpS4793;
      float _M0L6_2atmpS4792;
      float _M0L6_2atmpS4870;
      float _M0L7an__numS1501;
      float _M0L6_2atmpS4865;
      float _M0L6_2atmpS4864;
      float _M0L6_2atmpS4863;
      float _M0L2anS1502;
      float _M0L6_2atmpS4862;
      float _M0L6_2atmpS4861;
      float _M0L6_2atmpS4860;
      float _M0L6_2atmpS4859;
      float _M0L2bnS1503;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4798;
      float _M0L6_2atmpS4804;
      float _M0L6_2atmpS4802;
      float _M0L6_2atmpS4803;
      float _M0L6_2atmpS4801;
      float _M0L6_2atmpS4800;
      float _M0L6_2atmpS4799;
      float _M0L6_2atmpS4858;
      float _M0L6_2atmpS4857;
      float _M0L6_2atmpS4856;
      float _M0L6_2atmpS4855;
      float _M0L2ahS1504;
      float _M0L6_2atmpS4854;
      float _M0L6_2atmpS4853;
      float _M0L6_2atmpS4852;
      float _M0L6_2atmpS4851;
      float _M0L9bh__denomS1505;
      float _M0L2bhS1506;
      struct _M0TPB5ArrayGfE* _M0L1hS4805;
      float _M0L6_2atmpS4811;
      float _M0L6_2atmpS4809;
      float _M0L6_2atmpS4810;
      float _M0L6_2atmpS4808;
      float _M0L6_2atmpS4807;
      float _M0L6_2atmpS4806;
      struct _M0TPB5ArrayGfE* _M0L1mS4850;
      float _M0L6m__newS1507;
      struct _M0TPB5ArrayGfE* _M0L7n__gateS4849;
      float _M0L6n__newS1508;
      struct _M0TPB5ArrayGfE* _M0L1hS4848;
      float _M0L6h__newS1509;
      float _M0L6_2atmpS4847;
      float _M0L6_2atmpS4846;
      float _M0L3m3hS1510;
      float _M0L6_2atmpS4845;
      float _M0L6_2atmpS4844;
      float _M0L2n4S1511;
      struct _M0TPB5ArrayGfE* _M0L1iS4843;
      float _M0L6_2atmpS4840;
      float _M0L6_2atmpS4842;
      float _M0L6_2atmpS4841;
      float _M0L6_2atmpS4837;
      float _M0L6_2atmpS4839;
      float _M0L6_2atmpS4838;
      float _M0L6_2atmpS4834;
      float _M0L6_2atmpS4836;
      float _M0L6_2atmpS4835;
      float _M0L6_2atmpS4830;
      float _M0L6_2atmpS4832;
      float _M0L6_2atmpS4833;
      float _M0L6_2atmpS4831;
      float _M0L6_2atmpS4826;
      float _M0L6_2atmpS4828;
      float _M0L6_2atmpS4829;
      float _M0L6_2atmpS4827;
      float _M0L7currentS1512;
      struct _M0TPB5ArrayGfE* _M0L1vS4812;
      float _M0L6_2atmpS4815;
      float _M0L6_2atmpS4814;
      float _M0L6_2atmpS4813;
      struct _M0TPB5ArrayGfE* _M0L2geS4816;
      float _M0L6_2atmpS4820;
      float _M0L6_2atmpS4819;
      float _M0L6_2atmpS4818;
      float _M0L6_2atmpS4817;
      struct _M0TPB5ArrayGfE* _M0L2giS4821;
      float _M0L6_2atmpS4825;
      float _M0L6_2atmpS4824;
      float _M0L6_2atmpS4823;
      float _M0L6_2atmpS4822;
      int32_t _M0L6_2atmpS4893;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1vS1490 = _M0MPC15array5Array2atGfE(_M0L1vS4892, _M0L1iS1489);
      _M0L1mS4891 = _M0L1pS1474->$3;
      #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1mS1491 = _M0MPC15array5Array2atGfE(_M0L1mS4891, _M0L1iS1489);
      _M0L7n__gateS4890 = _M0L1pS1474->$4;
      #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2nnS1492
      = _M0MPC15array5Array2atGfE(_M0L7n__gateS4890, _M0L1iS1489);
      _M0L1hS4889 = _M0L1pS1474->$5;
      #line 127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L1hS1493 = _M0MPC15array5Array2atGfE(_M0L1hS4889, _M0L1iS1489);
      _M0L2geS4888 = _M0L1pS1474->$8;
      #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2geS1494 = _M0MPC15array5Array2atGfE(_M0L2geS4888, _M0L1iS1489);
      _M0L2giS4887 = _M0L1pS1474->$9;
      #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L2giS1495 = _M0MPC15array5Array2atGfE(_M0L2giS4887, _M0L1iS1489);
      _M0L4fireS4790 = _M0L1pS1474->$6;
      #line 130 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4790, _M0L1iS1489, 0);
      _M0L6_2atmpS4886 = 0x1.ap+3f - _M0L1vS1490;
      _M0L7am__numS1496 = _M0L6_2atmpS4886 + _M0L2vtS1483;
      _M0L6_2atmpS4885 = _M0L1vS1490 - _M0L2vtS1483;
      _M0L7bm__numS1497 = _M0L6_2atmpS4885 - 0x1.4p+5f;
      _M0L6_2atmpS4880 = _M0L7am__numS1496 / 0x1p+2f;
      #line 134 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4879 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4880);
      _M0L6_2atmpS4878 = _M0L6_2atmpS4879 - 0x1p+0f;
      if (_M0L6_2atmpS4878 != 0x0p+0f) {
        float _M0L6_2atmpS4881 = 0x1.47ae147ae147bp-2f * _M0L7am__numS1496;
        float _M0L6_2atmpS4884 = _M0L7am__numS1496 / 0x1p+2f;
        float _M0L6_2atmpS4883;
        float _M0L6_2atmpS4882;
        #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS4883 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4884);
        _M0L6_2atmpS4882 = _M0L6_2atmpS4883 - 0x1p+0f;
        _M0L2amS1498 = _M0L6_2atmpS4881 / _M0L6_2atmpS4882;
      } else {
        _M0L2amS1498 = 0x0p+0f;
      }
      _M0L6_2atmpS4873 = _M0L7bm__numS1497 / 0x1.4p+2f;
      #line 139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4872 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4873);
      _M0L6_2atmpS4871 = _M0L6_2atmpS4872 - 0x1p+0f;
      if (_M0L6_2atmpS4871 != 0x0p+0f) {
        float _M0L6_2atmpS4874 = 0x1.1eb851eb851ecp-2f * _M0L7bm__numS1497;
        float _M0L6_2atmpS4877 = _M0L7bm__numS1497 / 0x1.4p+2f;
        float _M0L6_2atmpS4876;
        float _M0L6_2atmpS4875;
        #line 140 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS4876 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4877);
        _M0L6_2atmpS4875 = _M0L6_2atmpS4876 - 0x1p+0f;
        _M0L2bmS1499 = _M0L6_2atmpS4874 / _M0L6_2atmpS4875;
      } else {
        _M0L2bmS1499 = 0x0p+0f;
      }
      _M0L1mS4791 = _M0L1pS1474->$3;
      _M0L6_2atmpS4797 = 0x1p+0f - _M0L1mS1491;
      _M0L6_2atmpS4795 = _M0L2amS1498 * _M0L6_2atmpS4797;
      _M0L6_2atmpS4796 = _M0L2bmS1499 * _M0L1mS1491;
      _M0L6_2atmpS4794 = _M0L6_2atmpS4795 - _M0L6_2atmpS4796;
      _M0L6_2atmpS4793 = _M0L2dtS1500 * _M0L6_2atmpS4794;
      _M0L6_2atmpS4792 = _M0L1mS1491 + _M0L6_2atmpS4793;
      #line 144 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1mS4791, _M0L1iS1489, _M0L6_2atmpS4792);
      _M0L6_2atmpS4870 = 0x1.ep+3f - _M0L1vS1490;
      _M0L7an__numS1501 = _M0L6_2atmpS4870 + _M0L2vtS1483;
      _M0L6_2atmpS4865 = _M0L7an__numS1501 / 0x1.4p+2f;
      #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4864 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4865);
      _M0L6_2atmpS4863 = _M0L6_2atmpS4864 - 0x1p+0f;
      if (_M0L6_2atmpS4863 != 0x0p+0f) {
        float _M0L6_2atmpS4866 = 0x1.0624dd2f1a9fcp-5f * _M0L7an__numS1501;
        float _M0L6_2atmpS4869 = _M0L7an__numS1501 / 0x1.4p+2f;
        float _M0L6_2atmpS4868;
        float _M0L6_2atmpS4867;
        #line 148 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
        _M0L6_2atmpS4868 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4869);
        _M0L6_2atmpS4867 = _M0L6_2atmpS4868 - 0x1p+0f;
        _M0L2anS1502 = _M0L6_2atmpS4866 / _M0L6_2atmpS4867;
      } else {
        _M0L2anS1502 = 0x0p+0f;
      }
      _M0L6_2atmpS4862 = 0x1.4p+3f - _M0L1vS1490;
      _M0L6_2atmpS4861 = _M0L6_2atmpS4862 + _M0L2vtS1483;
      _M0L6_2atmpS4860 = _M0L6_2atmpS4861 / 0x1.4p+5f;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4859 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4860);
      _M0L2bnS1503 = 0x1p-1f * _M0L6_2atmpS4859;
      _M0L7n__gateS4798 = _M0L1pS1474->$4;
      _M0L6_2atmpS4804 = 0x1p+0f - _M0L2nnS1492;
      _M0L6_2atmpS4802 = _M0L2anS1502 * _M0L6_2atmpS4804;
      _M0L6_2atmpS4803 = _M0L2bnS1503 * _M0L2nnS1492;
      _M0L6_2atmpS4801 = _M0L6_2atmpS4802 - _M0L6_2atmpS4803;
      _M0L6_2atmpS4800 = _M0L2dtS1500 * _M0L6_2atmpS4801;
      _M0L6_2atmpS4799 = _M0L2nnS1492 + _M0L6_2atmpS4800;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L7n__gateS4798, _M0L1iS1489, _M0L6_2atmpS4799);
      _M0L6_2atmpS4858 = 0x1.1p+4f - _M0L1vS1490;
      _M0L6_2atmpS4857 = _M0L6_2atmpS4858 + _M0L2vtS1483;
      _M0L6_2atmpS4856 = _M0L6_2atmpS4857 / 0x1.2p+4f;
      #line 155 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4855 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4856);
      _M0L2ahS1504 = 0x1.0624dd2f1a9fcp-3f * _M0L6_2atmpS4855;
      _M0L6_2atmpS4854 = 0x1.4p+5f - _M0L1vS1490;
      _M0L6_2atmpS4853 = _M0L6_2atmpS4854 + _M0L2vtS1483;
      _M0L6_2atmpS4852 = _M0L6_2atmpS4853 / 0x1.4p+2f;
      #line 156 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4851 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4852);
      _M0L9bh__denomS1505 = 0x1p+0f + _M0L6_2atmpS4851;
      if (_M0L9bh__denomS1505 != 0x0p+0f) {
        _M0L2bhS1506 = 0x1p+2f / _M0L9bh__denomS1505;
      } else {
        _M0L2bhS1506 = 0x0p+0f;
      }
      _M0L1hS4805 = _M0L1pS1474->$5;
      _M0L6_2atmpS4811 = 0x1p+0f - _M0L1hS1493;
      _M0L6_2atmpS4809 = _M0L2ahS1504 * _M0L6_2atmpS4811;
      _M0L6_2atmpS4810 = _M0L2bhS1506 * _M0L1hS1493;
      _M0L6_2atmpS4808 = _M0L6_2atmpS4809 - _M0L6_2atmpS4810;
      _M0L6_2atmpS4807 = _M0L2dtS1500 * _M0L6_2atmpS4808;
      _M0L6_2atmpS4806 = _M0L1hS1493 + _M0L6_2atmpS4807;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1hS4805, _M0L1iS1489, _M0L6_2atmpS4806);
      _M0L1mS4850 = _M0L1pS1474->$3;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6m__newS1507 = _M0MPC15array5Array2atGfE(_M0L1mS4850, _M0L1iS1489);
      _M0L7n__gateS4849 = _M0L1pS1474->$4;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6n__newS1508
      = _M0MPC15array5Array2atGfE(_M0L7n__gateS4849, _M0L1iS1489);
      _M0L1hS4848 = _M0L1pS1474->$5;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6h__newS1509 = _M0MPC15array5Array2atGfE(_M0L1hS4848, _M0L1iS1489);
      _M0L6_2atmpS4847 = _M0L6m__newS1507 * _M0L6m__newS1507;
      _M0L6_2atmpS4846 = _M0L6_2atmpS4847 * _M0L6m__newS1507;
      _M0L3m3hS1510 = _M0L6_2atmpS4846 * _M0L6h__newS1509;
      _M0L6_2atmpS4845 = _M0L6n__newS1508 * _M0L6n__newS1508;
      _M0L6_2atmpS4844 = _M0L6_2atmpS4845 * _M0L6n__newS1508;
      _M0L2n4S1511 = _M0L6_2atmpS4844 * _M0L6n__newS1508;
      _M0L1iS4843 = _M0L1pS1474->$7;
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4840 = _M0MPC15array5Array2atGfE(_M0L1iS4843, _M0L1iS1489);
      _M0L6_2atmpS4842 = _M0L2elS1478 - _M0L1vS1490;
      _M0L6_2atmpS4841 = _M0L2glS1477 * _M0L6_2atmpS4842;
      _M0L6_2atmpS4837 = _M0L6_2atmpS4840 + _M0L6_2atmpS4841;
      _M0L6_2atmpS4839 = _M0L4e__eS1486 - _M0L1vS1490;
      _M0L6_2atmpS4838 = _M0L2geS1494 * _M0L6_2atmpS4839;
      _M0L6_2atmpS4834 = _M0L6_2atmpS4837 + _M0L6_2atmpS4838;
      _M0L6_2atmpS4836 = _M0L4e__iS1487 - _M0L1vS1490;
      _M0L6_2atmpS4835 = _M0L2giS1495 * _M0L6_2atmpS4836;
      _M0L6_2atmpS4830 = _M0L6_2atmpS4834 + _M0L6_2atmpS4835;
      _M0L6_2atmpS4832 = _M0L2gnS1481 * _M0L3m3hS1510;
      _M0L6_2atmpS4833 = _M0L2enS1480 - _M0L1vS1490;
      _M0L6_2atmpS4831 = _M0L6_2atmpS4832 * _M0L6_2atmpS4833;
      _M0L6_2atmpS4826 = _M0L6_2atmpS4830 + _M0L6_2atmpS4831;
      _M0L6_2atmpS4828 = _M0L2gkS1482 * _M0L2n4S1511;
      _M0L6_2atmpS4829 = _M0L2ekS1479 - _M0L1vS1490;
      _M0L6_2atmpS4827 = _M0L6_2atmpS4828 * _M0L6_2atmpS4829;
      _M0L7currentS1512 = _M0L6_2atmpS4826 + _M0L6_2atmpS4827;
      _M0L1vS4812 = _M0L1pS1474->$2;
      _M0L6_2atmpS4815 = _M0L2dtS1500 / _M0L2cmS1476;
      _M0L6_2atmpS4814 = _M0L6_2atmpS4815 * _M0L7currentS1512;
      _M0L6_2atmpS4813 = _M0L1vS1490 + _M0L6_2atmpS4814;
      #line 171 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4812, _M0L1iS1489, _M0L6_2atmpS4813);
      _M0L2geS4816 = _M0L1pS1474->$8;
      _M0L6_2atmpS4820 = -_M0L2geS1494;
      _M0L6_2atmpS4819 = _M0L6_2atmpS4820 / _M0L6tau__eS1484;
      _M0L6_2atmpS4818 = _M0L2dtS1500 * _M0L6_2atmpS4819;
      _M0L6_2atmpS4817 = _M0L2geS1494 + _M0L6_2atmpS4818;
      #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4816, _M0L1iS1489, _M0L6_2atmpS4817);
      _M0L2giS4821 = _M0L1pS1474->$9;
      _M0L6_2atmpS4825 = -_M0L2giS1495;
      _M0L6_2atmpS4824 = _M0L6_2atmpS4825 / _M0L6tau__iS1485;
      _M0L6_2atmpS4823 = _M0L2dtS1500 * _M0L6_2atmpS4824;
      _M0L6_2atmpS4822 = _M0L2giS1495 + _M0L6_2atmpS4823;
      #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4821, _M0L1iS1489, _M0L6_2atmpS4822);
      _M0L6_2atmpS4893 = _M0L1iS1489 + 1;
      _M0L1iS1489 = _M0L6_2atmpS4893;
      continue;
    }
    break;
  }
  _M0L7_2abindS1514 = 0;
  _M0L1iS1515 = _M0L7_2abindS1514;
  while (1) {
    if (_M0L1iS1515 < _M0L1nS1473) {
      struct _M0TPB5ArrayGbE* _M0L4fireS4894 = _M0L1pS1474->$6;
      struct _M0TPB5ArrayGfE* _M0L1vS4897 = _M0L1pS1474->$2;
      float _M0L6_2atmpS4896;
      int32_t _M0L6_2atmpS4895;
      int32_t _M0L6_2atmpS4898;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0L6_2atmpS4896 = _M0MPC15array5Array2atGfE(_M0L1vS4897, _M0L1iS1515);
      _M0L6_2atmpS4895 = _M0L6_2atmpS4896 > -0x1.4p+4f;
      #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hh.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4894, _M0L1iS1515, _M0L6_2atmpS4895);
      _M0L6_2atmpS4898 = _M0L1iS1515 + 1;
      _M0L1iS1515 = _M0L6_2atmpS4898;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__hetrec(
  struct _M0TP26RiantR8snn__mbt6HetRec* _M0L1pS1444,
  float _M0L2dtS1452
) {
  int32_t _M0L1nS1443;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4789;
  int32_t _M0L2ndS1445;
  int32_t _M0L8total__dS1446;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4788;
  float _M0L9steepnessS1447;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4787;
  float _M0L6tau__mS1448;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4786;
  float _M0L9tau__rateS1449;
  struct _M0TP26RiantR8snn__mbt15HetRecParameter* _M0L5paramS4785;
  float _M0L8tau__absS1450;
  float _M0L6_2atmpS4784;
  int32_t _M0L11tabs__stepsS1451;
  int32_t _M0L7_2abindS1453;
  int32_t _M0L1iS1454;
  int32_t _M0L7_2abindS1457;
  int32_t _M0L1iS1458;
  int32_t _M0L7_2abindS1467;
  int32_t _M0L1iS1468;
  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L1nS1443 = _M0L1pS1444->$1;
  _M0L5paramS4789 = _M0L1pS1444->$0;
  _M0L2ndS1445 = _M0L5paramS4789->$0;
  _M0L8total__dS1446 = _M0L1nS1443 * _M0L2ndS1445;
  _M0L5paramS4788 = _M0L1pS1444->$0;
  _M0L9steepnessS1447 = _M0L5paramS4788->$7;
  _M0L5paramS4787 = _M0L1pS1444->$0;
  _M0L6tau__mS1448 = _M0L5paramS4787->$8;
  _M0L5paramS4786 = _M0L1pS1444->$0;
  _M0L9tau__rateS1449 = _M0L5paramS4786->$9;
  _M0L5paramS4785 = _M0L1pS1444->$0;
  _M0L8tau__absS1450 = _M0L5paramS4785->$6;
  _M0L6_2atmpS4784 = _M0L8tau__absS1450 / _M0L2dtS1452;
  #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
  _M0L11tabs__stepsS1451 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4784);
  _M0L7_2abindS1453 = 0;
  _M0L1iS1454 = _M0L7_2abindS1453;
  while (1) {
    if (_M0L1iS1454 < _M0L8total__dS1446) {
      struct _M0TPB5ArrayGfE* _M0L6tau__dS4712 = _M0L1pS1444->$6;
      float _M0L7tau__diS1455;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4700;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4711;
      float _M0L6_2atmpS4702;
      struct _M0TPB5ArrayGfE* _M0L4v__dS4710;
      float _M0L6_2atmpS4709;
      float _M0L6_2atmpS4706;
      struct _M0TPB5ArrayGfE* _M0L4is__S4708;
      float _M0L6_2atmpS4707;
      float _M0L6_2atmpS4705;
      float _M0L6_2atmpS4704;
      float _M0L6_2atmpS4703;
      float _M0L6_2atmpS4701;
      int32_t _M0L6_2atmpS4713;
      #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L7tau__diS1455
      = _M0MPC15array5Array2atGfE(_M0L6tau__dS4712, _M0L1iS1454);
      _M0L4v__dS4700 = _M0L1pS1444->$2;
      _M0L4v__dS4711 = _M0L1pS1444->$2;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4702
      = _M0MPC15array5Array2atGfE(_M0L4v__dS4711, _M0L1iS1454);
      _M0L4v__dS4710 = _M0L1pS1444->$2;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4709
      = _M0MPC15array5Array2atGfE(_M0L4v__dS4710, _M0L1iS1454);
      _M0L6_2atmpS4706 = -_M0L6_2atmpS4709;
      _M0L4is__S4708 = _M0L1pS1444->$4;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4707
      = _M0MPC15array5Array2atGfE(_M0L4is__S4708, _M0L1iS1454);
      _M0L6_2atmpS4705 = _M0L6_2atmpS4706 - _M0L6_2atmpS4707;
      _M0L6_2atmpS4704 = _M0L2dtS1452 * _M0L6_2atmpS4705;
      _M0L6_2atmpS4703 = _M0L6_2atmpS4704 / _M0L7tau__diS1455;
      _M0L6_2atmpS4701 = _M0L6_2atmpS4702 + _M0L6_2atmpS4703;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L4v__dS4700, _M0L1iS1454, _M0L6_2atmpS4701);
      _M0L6_2atmpS4713 = _M0L1iS1454 + 1;
      _M0L1iS1454 = _M0L6_2atmpS4713;
      continue;
    }
    break;
  }
  _M0L7_2abindS1457 = 0;
  _M0L1iS1458 = _M0L7_2abindS1457;
  while (1) {
    if (_M0L1iS1458 < _M0L1nS1443) {
      struct _M0TPB5ArrayGiE* _M0L6colptrS4734 = _M0L1pS1444->$11;
      int32_t _M0L5startS1459;
      struct _M0TPB5ArrayGiE* _M0L6colptrS4732;
      int32_t _M0L6_2atmpS4733;
      int32_t _M0L3endS1460;
      float _M0L16dt__over__tau__mS1461;
      struct _M0TPB8MutLocalGiE* _M0L1sS1462;
      int32_t _M0L6_2atmpS4735;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L5startS1459
      = _M0MPC15array5Array2atGiE(_M0L6colptrS4734, _M0L1iS1458);
      _M0L6colptrS4732 = _M0L1pS1444->$11;
      _M0L6_2atmpS4733 = _M0L1iS1458 + 1;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L3endS1460
      = _M0MPC15array5Array2atGiE(_M0L6colptrS4732, _M0L6_2atmpS4733);
      _M0L16dt__over__tau__mS1461 = _M0L2dtS1452 / _M0L6tau__mS1448;
      _M0L1sS1462
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1462)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1462->$0 = _M0L5startS1459;
      while (1) {
        int32_t _M0L3valS4714 = _M0L1sS1462->$0;
        if (_M0L3valS4714 < _M0L3endS1460) {
          struct _M0TPB5ArrayGiE* _M0L6i__synS4730 = _M0L1pS1444->$12;
          int32_t _M0L3valS4731 = _M0L1sS1462->$0;
          int32_t _M0L9dend__idxS1463;
          struct _M0TPB5ArrayGfE* _M0L6w__synS4728;
          int32_t _M0L3valS4729;
          float _M0L1wS1464;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4715;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4725;
          float _M0L6_2atmpS4717;
          struct _M0TPB5ArrayGfE* _M0L4v__dS4724;
          float _M0L6_2atmpS4723;
          float _M0L6_2atmpS4720;
          struct _M0TPB5ArrayGfE* _M0L4v__sS4722;
          float _M0L6_2atmpS4721;
          float _M0L6_2atmpS4719;
          float _M0L6_2atmpS4718;
          float _M0L6_2atmpS4716;
          int32_t _M0L3valS4727;
          int32_t _M0L6_2atmpS4726;
          #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L9dend__idxS1463
          = _M0MPC15array5Array2atGiE(_M0L6i__synS4730, _M0L3valS4731);
          _M0L6w__synS4728 = _M0L1pS1444->$13;
          _M0L3valS4729 = _M0L1sS1462->$0;
          #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L1wS1464
          = _M0MPC15array5Array2atGfE(_M0L6w__synS4728, _M0L3valS4729);
          _M0L4v__sS4715 = _M0L1pS1444->$3;
          _M0L4v__sS4725 = _M0L1pS1444->$3;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4717
          = _M0MPC15array5Array2atGfE(_M0L4v__sS4725, _M0L1iS1458);
          _M0L4v__dS4724 = _M0L1pS1444->$2;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4723
          = _M0MPC15array5Array2atGfE(_M0L4v__dS4724, _M0L9dend__idxS1463);
          _M0L6_2atmpS4720 = _M0L1wS1464 * _M0L6_2atmpS4723;
          _M0L4v__sS4722 = _M0L1pS1444->$3;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0L6_2atmpS4721
          = _M0MPC15array5Array2atGfE(_M0L4v__sS4722, _M0L1iS1458);
          _M0L6_2atmpS4719 = _M0L6_2atmpS4720 - _M0L6_2atmpS4721;
          _M0L6_2atmpS4718 = _M0L6_2atmpS4719 * _M0L16dt__over__tau__mS1461;
          _M0L6_2atmpS4716 = _M0L6_2atmpS4717 + _M0L6_2atmpS4718;
          #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
          _M0MPC15array5Array3setGfE(_M0L4v__sS4715, _M0L1iS1458, _M0L6_2atmpS4716);
          _M0L3valS4727 = _M0L1sS1462->$0;
          _M0L6_2atmpS4726 = _M0L3valS4727 + 1;
          _M0L1sS1462->$0 = _M0L6_2atmpS4726;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1462);
        }
        break;
      }
      _M0L6_2atmpS4735 = _M0L1iS1458 + 1;
      _M0L1iS1458 = _M0L6_2atmpS4735;
      continue;
    }
    break;
  }
  _M0L7_2abindS1467 = 0;
  _M0L1iS1468 = _M0L7_2abindS1467;
  while (1) {
    if (_M0L1iS1468 < _M0L1nS1443) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS4737 = _M0L1pS1444->$8;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4740 = _M0L1pS1444->$8;
      int32_t _M0L6_2atmpS4739;
      int32_t _M0L6_2atmpS4738;
      struct _M0TPB5ArrayGbE* _M0L4fireS4741;
      struct _M0TPB5ArrayGfE* _M0L5traceS4742;
      struct _M0TPB5ArrayGfE* _M0L5traceS4750;
      float _M0L6_2atmpS4744;
      struct _M0TPB5ArrayGfE* _M0L5traceS4749;
      float _M0L6_2atmpS4748;
      float _M0L6_2atmpS4747;
      float _M0L6_2atmpS4746;
      float _M0L6_2atmpS4745;
      float _M0L6_2atmpS4743;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4752;
      int32_t _M0L6_2atmpS4751;
      struct _M0TPB5ArrayGfE* _M0L5traceS4753;
      struct _M0TPB5ArrayGfE* _M0L5traceS4762;
      float _M0L6_2atmpS4755;
      struct _M0TPB5ArrayGfE* _M0L4v__sS4761;
      float _M0L6_2atmpS4758;
      struct _M0TPB5ArrayGfE* _M0L5traceS4760;
      float _M0L6_2atmpS4759;
      float _M0L6_2atmpS4757;
      float _M0L6_2atmpS4756;
      float _M0L6_2atmpS4754;
      float _M0L6_2atmpS4778;
      struct _M0TPB5ArrayGfE* _M0L4v__sS4783;
      float _M0L6_2atmpS4780;
      struct _M0TPB5ArrayGfE* _M0L5traceS4782;
      float _M0L6_2atmpS4781;
      float _M0L6_2atmpS4779;
      float _M0L12sigmoid__argS1471;
      float _M0L4rateS1472;
      struct _M0TPB5ArrayGfE* _M0L9randcacheS4764;
      float _M0L6_2atmpS4763;
      int32_t _M0L6_2atmpS4736;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4739
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4740, _M0L1iS1468);
      _M0L6_2atmpS4738 = _M0L6_2atmpS4739 - 1;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4737, _M0L1iS1468, _M0L6_2atmpS4738);
      _M0L4fireS4741 = _M0L1pS1444->$7;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4741, _M0L1iS1468, 0);
      _M0L5traceS4742 = _M0L1pS1444->$9;
      _M0L5traceS4750 = _M0L1pS1444->$9;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4744
      = _M0MPC15array5Array2atGfE(_M0L5traceS4750, _M0L1iS1468);
      _M0L5traceS4749 = _M0L1pS1444->$9;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4748
      = _M0MPC15array5Array2atGfE(_M0L5traceS4749, _M0L1iS1468);
      _M0L6_2atmpS4747 = -_M0L6_2atmpS4748;
      _M0L6_2atmpS4746 = _M0L6_2atmpS4747 / _M0L9tau__rateS1449;
      _M0L6_2atmpS4745 = _M0L2dtS1452 * _M0L6_2atmpS4746;
      _M0L6_2atmpS4743 = _M0L6_2atmpS4744 + _M0L6_2atmpS4745;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L5traceS4742, _M0L1iS1468, _M0L6_2atmpS4743);
      _M0L4tabsS4752 = _M0L1pS1444->$8;
      #line 250 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4751
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4752, _M0L1iS1468);
      if (_M0L6_2atmpS4751 > 0) {
        goto join_1469;
      }
      _M0L5traceS4753 = _M0L1pS1444->$9;
      _M0L5traceS4762 = _M0L1pS1444->$9;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4755
      = _M0MPC15array5Array2atGfE(_M0L5traceS4762, _M0L1iS1468);
      _M0L4v__sS4761 = _M0L1pS1444->$3;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4758
      = _M0MPC15array5Array2atGfE(_M0L4v__sS4761, _M0L1iS1468);
      _M0L5traceS4760 = _M0L1pS1444->$9;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4759
      = _M0MPC15array5Array2atGfE(_M0L5traceS4760, _M0L1iS1468);
      _M0L6_2atmpS4757 = _M0L6_2atmpS4758 - _M0L6_2atmpS4759;
      _M0L6_2atmpS4756 = _M0L6_2atmpS4757 / _M0L9tau__rateS1449;
      _M0L6_2atmpS4754 = _M0L6_2atmpS4755 + _M0L6_2atmpS4756;
      #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0MPC15array5Array3setGfE(_M0L5traceS4753, _M0L1iS1468, _M0L6_2atmpS4754);
      _M0L6_2atmpS4778 = -_M0L9steepnessS1447;
      _M0L4v__sS4783 = _M0L1pS1444->$3;
      #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4780
      = _M0MPC15array5Array2atGfE(_M0L4v__sS4783, _M0L1iS1468);
      _M0L5traceS4782 = _M0L1pS1444->$9;
      #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4781
      = _M0MPC15array5Array2atGfE(_M0L5traceS4782, _M0L1iS1468);
      _M0L6_2atmpS4779 = _M0L6_2atmpS4780 - _M0L6_2atmpS4781;
      _M0L12sigmoid__argS1471 = _M0L6_2atmpS4778 * _M0L6_2atmpS4779;
      if (_M0L12sigmoid__argS1471 > 0x1.6p+6f) {
        struct _M0TPB5ArrayGfE* _M0L1rS4772 = _M0L1pS1444->$5;
        float _M0L6_2atmpS4771;
        #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4771
        = _M0MPC15array5Array2atGfE(_M0L1rS4772, _M0L1iS1468);
        _M0L4rateS1472 = _M0L6_2atmpS4771 * _M0L2dtS1452;
      } else if (_M0L12sigmoid__argS1471 < -0x1.6p+6f) {
        _M0L4rateS1472 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1rS4777 = _M0L1pS1444->$5;
        float _M0L6_2atmpS4776;
        float _M0L6_2atmpS4773;
        float _M0L6_2atmpS4775;
        float _M0L6_2atmpS4774;
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4776
        = _M0MPC15array5Array2atGfE(_M0L1rS4777, _M0L1iS1468);
        _M0L6_2atmpS4773 = _M0L6_2atmpS4776 * _M0L2dtS1452;
        #line 264 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4775
        = _M0FP26RiantR8snn__mbt4expf(_M0L12sigmoid__argS1471);
        _M0L6_2atmpS4774 = 0x1p+0f + _M0L6_2atmpS4775;
        _M0L4rateS1472 = _M0L6_2atmpS4773 / _M0L6_2atmpS4774;
      }
      _M0L9randcacheS4764 = _M0L1pS1444->$10;
      #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
      _M0L6_2atmpS4763
      = _M0MPC15array5Array2atGfE(_M0L9randcacheS4764, _M0L1iS1468);
      if (_M0L6_2atmpS4763 < _M0L4rateS1472) {
        struct _M0TPB5ArrayGbE* _M0L4fireS4765 = _M0L1pS1444->$7;
        struct _M0TPB5ArrayGiE* _M0L4tabsS4766;
        struct _M0TPB5ArrayGfE* _M0L5traceS4767;
        struct _M0TPB5ArrayGfE* _M0L5traceS4770;
        float _M0L6_2atmpS4769;
        float _M0L6_2atmpS4768;
        #line 267 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS4765, _M0L1iS1468, 1);
        _M0L4tabsS4766 = _M0L1pS1444->$8;
        #line 268 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS4766, _M0L1iS1468, _M0L11tabs__stepsS1451);
        _M0L5traceS4767 = _M0L1pS1444->$9;
        _M0L5traceS4770 = _M0L1pS1444->$9;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0L6_2atmpS4769
        = _M0MPC15array5Array2atGfE(_M0L5traceS4770, _M0L1iS1468);
        _M0L6_2atmpS4768 = _M0L6_2atmpS4769 + 0x1p+0f;
        #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_hetrec.mbt"
        _M0MPC15array5Array3setGfE(_M0L5traceS4767, _M0L1iS1468, _M0L6_2atmpS4768);
      }
      goto join_1469;
      goto joinlet_5831;
      join_1469:;
      _M0L6_2atmpS4736 = _M0L1iS1468 + 1;
      _M0L1iS1468 = _M0L6_2atmpS4736;
      continue;
      joinlet_5831:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt18step__adex__sinexp(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1422,
  float _M0L2dtS1437
) {
  int32_t _M0L1nS1421;
  struct _M0TP26RiantR8snn__mbt19AdExSinExpParameter* _M0L3p__S1423;
  float _M0L2tmS1424;
  float _M0L2vtS1425;
  float _M0L2vrS1426;
  float _M0L2elS1427;
  float _M0L1rS1428;
  float _M0L9dt__slopeS1429;
  float _M0L2twS1430;
  float _M0L1aS1431;
  float _M0L1bS1432;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4699;
  float _M0L2atS1433;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4698;
  float _M0L6tau__aS1434;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4697;
  float _M0L11tabs__constS1435;
  float _M0L6_2atmpS4696;
  int32_t _M0L11tabs__stepsS1436;
  int32_t _M0L7_2abindS1438;
  int32_t _M0L1iS1439;
  #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1421 = _M0L1pS1422->$2;
  _M0L3p__S1423 = _M0L1pS1422->$0;
  _M0L2tmS1424 = _M0L3p__S1423->$5;
  _M0L2vtS1425 = _M0L3p__S1423->$2;
  _M0L2vrS1426 = _M0L3p__S1423->$3;
  _M0L2elS1427 = _M0L3p__S1423->$4;
  _M0L1rS1428 = _M0L3p__S1423->$6;
  _M0L9dt__slopeS1429 = _M0L3p__S1423->$7;
  _M0L2twS1430 = _M0L3p__S1423->$8;
  _M0L1aS1431 = _M0L3p__S1423->$9;
  _M0L1bS1432 = _M0L3p__S1423->$10;
  _M0L5spikeS4699 = _M0L1pS1422->$1;
  _M0L2atS1433 = _M0L5spikeS4699->$0;
  _M0L5spikeS4698 = _M0L1pS1422->$1;
  _M0L6tau__aS1434 = _M0L5spikeS4698->$1;
  _M0L5spikeS4697 = _M0L1pS1422->$1;
  _M0L11tabs__constS1435 = _M0L5spikeS4697->$3;
  _M0L6_2atmpS4696 = _M0L11tabs__constS1435 / _M0L2dtS1437;
  #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L11tabs__stepsS1436 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4696);
  _M0L7_2abindS1438 = 0;
  _M0L1iS1439 = _M0L7_2abindS1438;
  while (1) {
    if (_M0L1iS1439 < _M0L1nS1421) {
      struct _M0TPB5ArrayGfE* _M0L1vS4609 = _M0L1pS1422->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS4611 = _M0L1pS1422->$5;
      float _M0L6_2atmpS4610;
      struct _M0TPB5ArrayGbE* _M0L4fireS4613;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4614;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4617;
      int32_t _M0L6_2atmpS4616;
      int32_t _M0L6_2atmpS4615;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4619;
      int32_t _M0L6_2atmpS4618;
      struct _M0TPB5ArrayGfE* _M0L1wS4620;
      struct _M0TPB5ArrayGfE* _M0L1wS4632;
      float _M0L6_2atmpS4622;
      struct _M0TPB5ArrayGfE* _M0L1vS4631;
      float _M0L6_2atmpS4630;
      float _M0L6_2atmpS4629;
      float _M0L6_2atmpS4626;
      struct _M0TPB5ArrayGfE* _M0L1wS4628;
      float _M0L6_2atmpS4627;
      float _M0L6_2atmpS4625;
      float _M0L6_2atmpS4624;
      float _M0L6_2atmpS4623;
      float _M0L6_2atmpS4621;
      float _M0L9exp__termS1442;
      struct _M0TPB5ArrayGfE* _M0L1vS4633;
      struct _M0TPB5ArrayGfE* _M0L1vS4655;
      float _M0L6_2atmpS4635;
      struct _M0TPB5ArrayGfE* _M0L1vS4654;
      float _M0L6_2atmpS4653;
      float _M0L6_2atmpS4652;
      float _M0L6_2atmpS4651;
      float _M0L6_2atmpS4647;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4650;
      float _M0L6_2atmpS4649;
      float _M0L6_2atmpS4648;
      float _M0L6_2atmpS4643;
      struct _M0TPB5ArrayGfE* _M0L1wS4646;
      float _M0L6_2atmpS4645;
      float _M0L6_2atmpS4644;
      float _M0L6_2atmpS4639;
      struct _M0TPB5ArrayGfE* _M0L1iS4642;
      float _M0L6_2atmpS4641;
      float _M0L6_2atmpS4640;
      float _M0L6_2atmpS4638;
      float _M0L6_2atmpS4637;
      float _M0L6_2atmpS4636;
      float _M0L6_2atmpS4634;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4656;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4664;
      float _M0L6_2atmpS4658;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4663;
      float _M0L6_2atmpS4662;
      float _M0L6_2atmpS4661;
      float _M0L6_2atmpS4660;
      float _M0L6_2atmpS4659;
      float _M0L6_2atmpS4657;
      struct _M0TPB5ArrayGbE* _M0L4fireS4665;
      struct _M0TPB5ArrayGfE* _M0L1vS4668;
      float _M0L6_2atmpS4667;
      int32_t _M0L6_2atmpS4666;
      struct _M0TPB5ArrayGfE* _M0L1vS4669;
      struct _M0TPB5ArrayGbE* _M0L4fireS4671;
      float _M0L6_2atmpS4670;
      struct _M0TPB5ArrayGfE* _M0L1wS4673;
      struct _M0TPB5ArrayGbE* _M0L4fireS4675;
      float _M0L6_2atmpS4674;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4679;
      struct _M0TPB5ArrayGbE* _M0L4fireS4681;
      float _M0L6_2atmpS4680;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4685;
      struct _M0TPB5ArrayGbE* _M0L4fireS4687;
      int32_t _M0L6_2atmpS4686;
      int32_t _M0L6_2atmpS4608;
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4611, _M0L1iS1439)) {
        _M0L6_2atmpS4610 = _M0L2vrS1426;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4612 = _M0L1pS1422->$3;
        #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4610
        = _M0MPC15array5Array2atGfE(_M0L1vS4612, _M0L1iS1439);
      }
      #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4609, _M0L1iS1439, _M0L6_2atmpS4610);
      _M0L4fireS4613 = _M0L1pS1422->$5;
      #line 212 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4613, _M0L1iS1439, 0);
      _M0L4tabsS4614 = _M0L1pS1422->$7;
      _M0L4tabsS4617 = _M0L1pS1422->$7;
      #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4616
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4617, _M0L1iS1439);
      _M0L6_2atmpS4615 = _M0L6_2atmpS4616 - 1;
      #line 213 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4614, _M0L1iS1439, _M0L6_2atmpS4615);
      _M0L4tabsS4619 = _M0L1pS1422->$7;
      #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4618
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4619, _M0L1iS1439);
      if (_M0L6_2atmpS4618 > 0) {
        goto join_1440;
      }
      _M0L1wS4620 = _M0L1pS1422->$4;
      _M0L1wS4632 = _M0L1pS1422->$4;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4622 = _M0MPC15array5Array2atGfE(_M0L1wS4632, _M0L1iS1439);
      _M0L1vS4631 = _M0L1pS1422->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4630 = _M0MPC15array5Array2atGfE(_M0L1vS4631, _M0L1iS1439);
      _M0L6_2atmpS4629 = _M0L6_2atmpS4630 - _M0L2elS1427;
      _M0L6_2atmpS4626 = _M0L1aS1431 * _M0L6_2atmpS4629;
      _M0L1wS4628 = _M0L1pS1422->$4;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4627 = _M0MPC15array5Array2atGfE(_M0L1wS4628, _M0L1iS1439);
      _M0L6_2atmpS4625 = _M0L6_2atmpS4626 - _M0L6_2atmpS4627;
      _M0L6_2atmpS4624 = _M0L2dtS1437 * _M0L6_2atmpS4625;
      _M0L6_2atmpS4623 = _M0L6_2atmpS4624 / _M0L2twS1430;
      _M0L6_2atmpS4621 = _M0L6_2atmpS4622 + _M0L6_2atmpS4623;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4620, _M0L1iS1439, _M0L6_2atmpS4621);
      if (_M0L9dt__slopeS1429 < 0x0p+0f) {
        _M0L9exp__termS1442 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4695 = _M0L1pS1422->$3;
        float _M0L6_2atmpS4692;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4694;
        float _M0L6_2atmpS4693;
        float _M0L6_2atmpS4691;
        float _M0L6_2atmpS4690;
        float _M0L6_2atmpS4689;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4692
        = _M0MPC15array5Array2atGfE(_M0L1vS4695, _M0L1iS1439);
        _M0L9thresholdS4694 = _M0L1pS1422->$6;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4693
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4694, _M0L1iS1439);
        _M0L6_2atmpS4691 = _M0L6_2atmpS4692 - _M0L6_2atmpS4693;
        _M0L6_2atmpS4690 = _M0L6_2atmpS4691 / _M0L9dt__slopeS1429;
        #line 225 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4689 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4690);
        _M0L9exp__termS1442 = _M0L9dt__slopeS1429 * _M0L6_2atmpS4689;
      }
      _M0L1vS4633 = _M0L1pS1422->$3;
      _M0L1vS4655 = _M0L1pS1422->$3;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4635 = _M0MPC15array5Array2atGfE(_M0L1vS4655, _M0L1iS1439);
      _M0L1vS4654 = _M0L1pS1422->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4653 = _M0MPC15array5Array2atGfE(_M0L1vS4654, _M0L1iS1439);
      _M0L6_2atmpS4652 = _M0L6_2atmpS4653 - _M0L2elS1427;
      _M0L6_2atmpS4651 = -_M0L6_2atmpS4652;
      _M0L6_2atmpS4647 = _M0L6_2atmpS4651 + _M0L9exp__termS1442;
      _M0L9syn__currS4650 = _M0L1pS1422->$9;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4649
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS4650, _M0L1iS1439);
      _M0L6_2atmpS4648 = _M0L1rS1428 * _M0L6_2atmpS4649;
      _M0L6_2atmpS4643 = _M0L6_2atmpS4647 - _M0L6_2atmpS4648;
      _M0L1wS4646 = _M0L1pS1422->$4;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4645 = _M0MPC15array5Array2atGfE(_M0L1wS4646, _M0L1iS1439);
      _M0L6_2atmpS4644 = _M0L1rS1428 * _M0L6_2atmpS4645;
      _M0L6_2atmpS4639 = _M0L6_2atmpS4643 - _M0L6_2atmpS4644;
      _M0L1iS4642 = _M0L1pS1422->$8;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4641 = _M0MPC15array5Array2atGfE(_M0L1iS4642, _M0L1iS1439);
      _M0L6_2atmpS4640 = _M0L1rS1428 * _M0L6_2atmpS4641;
      _M0L6_2atmpS4638 = _M0L6_2atmpS4639 + _M0L6_2atmpS4640;
      _M0L6_2atmpS4637 = _M0L2dtS1437 * _M0L6_2atmpS4638;
      _M0L6_2atmpS4636 = _M0L6_2atmpS4637 / _M0L2tmS1424;
      _M0L6_2atmpS4634 = _M0L6_2atmpS4635 + _M0L6_2atmpS4636;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4633, _M0L1iS1439, _M0L6_2atmpS4634);
      _M0L9thresholdS4656 = _M0L1pS1422->$6;
      _M0L9thresholdS4664 = _M0L1pS1422->$6;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4658
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4664, _M0L1iS1439);
      _M0L9thresholdS4663 = _M0L1pS1422->$6;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4662
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4663, _M0L1iS1439);
      _M0L6_2atmpS4661 = _M0L2vtS1425 - _M0L6_2atmpS4662;
      _M0L6_2atmpS4660 = _M0L2dtS1437 * _M0L6_2atmpS4661;
      _M0L6_2atmpS4659 = _M0L6_2atmpS4660 / _M0L6tau__aS1434;
      _M0L6_2atmpS4657 = _M0L6_2atmpS4658 + _M0L6_2atmpS4659;
      #line 235 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4656, _M0L1iS1439, _M0L6_2atmpS4657);
      _M0L4fireS4665 = _M0L1pS1422->$5;
      _M0L1vS4668 = _M0L1pS1422->$3;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4667 = _M0MPC15array5Array2atGfE(_M0L1vS4668, _M0L1iS1439);
      _M0L6_2atmpS4666 = _M0L6_2atmpS4667 >= 0x0p+0f;
      #line 238 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4665, _M0L1iS1439, _M0L6_2atmpS4666);
      _M0L1vS4669 = _M0L1pS1422->$3;
      _M0L4fireS4671 = _M0L1pS1422->$5;
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4671, _M0L1iS1439)) {
        _M0L6_2atmpS4670 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4672 = _M0L1pS1422->$3;
        #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4670
        = _M0MPC15array5Array2atGfE(_M0L1vS4672, _M0L1iS1439);
      }
      #line 239 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4669, _M0L1iS1439, _M0L6_2atmpS4670);
      _M0L1wS4673 = _M0L1pS1422->$4;
      _M0L4fireS4675 = _M0L1pS1422->$5;
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4675, _M0L1iS1439)) {
        struct _M0TPB5ArrayGfE* _M0L1wS4677 = _M0L1pS1422->$4;
        float _M0L6_2atmpS4676;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4676
        = _M0MPC15array5Array2atGfE(_M0L1wS4677, _M0L1iS1439);
        _M0L6_2atmpS4674 = _M0L6_2atmpS4676 + _M0L1bS1432;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS4678 = _M0L1pS1422->$4;
        #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4674
        = _M0MPC15array5Array2atGfE(_M0L1wS4678, _M0L1iS1439);
      }
      #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4673, _M0L1iS1439, _M0L6_2atmpS4674);
      _M0L9thresholdS4679 = _M0L1pS1422->$6;
      _M0L4fireS4681 = _M0L1pS1422->$5;
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4681, _M0L1iS1439)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4683 = _M0L1pS1422->$6;
        float _M0L6_2atmpS4682;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4682
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4683, _M0L1iS1439);
        _M0L6_2atmpS4680 = _M0L6_2atmpS4682 + _M0L2atS1433;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4684 = _M0L1pS1422->$6;
        #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4680
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4684, _M0L1iS1439);
      }
      #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4679, _M0L1iS1439, _M0L6_2atmpS4680);
      _M0L4tabsS4685 = _M0L1pS1422->$7;
      _M0L4fireS4687 = _M0L1pS1422->$5;
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4687, _M0L1iS1439)) {
        _M0L6_2atmpS4686 = _M0L11tabs__stepsS1436;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4688 = _M0L1pS1422->$7;
        #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
        _M0L6_2atmpS4686
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4688, _M0L1iS1439);
      }
      #line 242 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4685, _M0L1iS1439, _M0L6_2atmpS4686);
      goto join_1440;
      goto joinlet_5833;
      join_1440:;
      _M0L6_2atmpS4608 = _M0L1iS1439 + 1;
      _M0L1iS1439 = _M0L6_2atmpS4608;
      continue;
      joinlet_5833:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt31adex__sinexp__synaptic__current(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1417
) {
  int32_t _M0L1nS1416;
  int32_t _M0L7_2abindS1418;
  int32_t _M0L1iS1419;
  #line 177 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1416 = _M0L1pS1417->$2;
  _M0L7_2abindS1418 = 0;
  _M0L1iS1419 = _M0L7_2abindS1418;
  while (1) {
    if (_M0L1iS1419 < _M0L1nS1416) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4585 = _M0L1pS1417->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS4606 = _M0L1pS1417->$10;
      float _M0L6_2atmpS4601;
      struct _M0TPB5ArrayGfE* _M0L1vS4605;
      float _M0L6_2atmpS4603;
      float _M0L4e__eS4604;
      float _M0L6_2atmpS4602;
      float _M0L6_2atmpS4598;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS4600;
      float _M0L6_2atmpS4599;
      float _M0L6_2atmpS4587;
      struct _M0TPB5ArrayGfE* _M0L2giS4597;
      float _M0L6_2atmpS4592;
      struct _M0TPB5ArrayGfE* _M0L1vS4596;
      float _M0L6_2atmpS4594;
      float _M0L4e__iS4595;
      float _M0L6_2atmpS4593;
      float _M0L6_2atmpS4589;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS4591;
      float _M0L6_2atmpS4590;
      float _M0L6_2atmpS4588;
      float _M0L6_2atmpS4586;
      int32_t _M0L6_2atmpS4607;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4601 = _M0MPC15array5Array2atGfE(_M0L2geS4606, _M0L1iS1419);
      _M0L1vS4605 = _M0L1pS1417->$3;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4603 = _M0MPC15array5Array2atGfE(_M0L1vS4605, _M0L1iS1419);
      _M0L4e__eS4604 = _M0L1pS1417->$16;
      _M0L6_2atmpS4602 = _M0L6_2atmpS4603 - _M0L4e__eS4604;
      _M0L6_2atmpS4598 = _M0L6_2atmpS4601 * _M0L6_2atmpS4602;
      _M0L7gsyn__eS4600 = _M0L1pS1417->$14;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4599
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS4600, _M0L1iS1419);
      _M0L6_2atmpS4587 = _M0L6_2atmpS4598 * _M0L6_2atmpS4599;
      _M0L2giS4597 = _M0L1pS1417->$11;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4592 = _M0MPC15array5Array2atGfE(_M0L2giS4597, _M0L1iS1419);
      _M0L1vS4596 = _M0L1pS1417->$3;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4594 = _M0MPC15array5Array2atGfE(_M0L1vS4596, _M0L1iS1419);
      _M0L4e__iS4595 = _M0L1pS1417->$17;
      _M0L6_2atmpS4593 = _M0L6_2atmpS4594 - _M0L4e__iS4595;
      _M0L6_2atmpS4589 = _M0L6_2atmpS4592 * _M0L6_2atmpS4593;
      _M0L7gsyn__iS4591 = _M0L1pS1417->$15;
      #line 181 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4590
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS4591, _M0L1iS1419);
      _M0L6_2atmpS4588 = _M0L6_2atmpS4589 * _M0L6_2atmpS4590;
      _M0L6_2atmpS4586 = _M0L6_2atmpS4587 + _M0L6_2atmpS4588;
      #line 180 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS4585, _M0L1iS1419, _M0L6_2atmpS4586);
      _M0L6_2atmpS4607 = _M0L1iS1419 + 1;
      _M0L1iS1419 = _M0L6_2atmpS4607;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt28adex__sinexp__step__synapses(
  struct _M0TP26RiantR8snn__mbt10AdExSinExp* _M0L1pS1406,
  float _M0L2dtS1411
) {
  int32_t _M0L1nS1405;
  float _M0L6tau__eS1407;
  float _M0L6tau__iS1408;
  int32_t _M0L7_2abindS1409;
  int32_t _M0L1iS1410;
  int32_t _M0L7_2abindS1413;
  int32_t _M0L1iS1414;
  #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
  _M0L1nS1405 = _M0L1pS1406->$2;
  _M0L6tau__eS1407 = _M0L1pS1406->$18;
  _M0L6tau__iS1408 = _M0L1pS1406->$19;
  _M0L7_2abindS1409 = 0;
  _M0L1iS1410 = _M0L7_2abindS1409;
  while (1) {
    if (_M0L1iS1410 < _M0L1nS1405) {
      struct _M0TPB5ArrayGfE* _M0L2geS4551 = _M0L1pS1406->$10;
      struct _M0TPB5ArrayGfE* _M0L2geS4556 = _M0L1pS1406->$10;
      float _M0L6_2atmpS4553;
      struct _M0TPB5ArrayGfE* _M0L3gluS4555;
      float _M0L6_2atmpS4554;
      float _M0L6_2atmpS4552;
      struct _M0TPB5ArrayGfE* _M0L2giS4557;
      struct _M0TPB5ArrayGfE* _M0L2giS4562;
      float _M0L6_2atmpS4559;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4561;
      float _M0L6_2atmpS4560;
      float _M0L6_2atmpS4558;
      struct _M0TPB5ArrayGfE* _M0L2geS4563;
      struct _M0TPB5ArrayGfE* _M0L2geS4571;
      float _M0L6_2atmpS4565;
      struct _M0TPB5ArrayGfE* _M0L2geS4570;
      float _M0L6_2atmpS4569;
      float _M0L6_2atmpS4568;
      float _M0L6_2atmpS4567;
      float _M0L6_2atmpS4566;
      float _M0L6_2atmpS4564;
      struct _M0TPB5ArrayGfE* _M0L2giS4572;
      struct _M0TPB5ArrayGfE* _M0L2giS4580;
      float _M0L6_2atmpS4574;
      struct _M0TPB5ArrayGfE* _M0L2giS4579;
      float _M0L6_2atmpS4578;
      float _M0L6_2atmpS4577;
      float _M0L6_2atmpS4576;
      float _M0L6_2atmpS4575;
      float _M0L6_2atmpS4573;
      int32_t _M0L6_2atmpS4581;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4553 = _M0MPC15array5Array2atGfE(_M0L2geS4556, _M0L1iS1410);
      _M0L3gluS4555 = _M0L1pS1406->$12;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4554
      = _M0MPC15array5Array2atGfE(_M0L3gluS4555, _M0L1iS1410);
      _M0L6_2atmpS4552 = _M0L6_2atmpS4553 + _M0L6_2atmpS4554;
      #line 160 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4551, _M0L1iS1410, _M0L6_2atmpS4552);
      _M0L2giS4557 = _M0L1pS1406->$11;
      _M0L2giS4562 = _M0L1pS1406->$11;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4559 = _M0MPC15array5Array2atGfE(_M0L2giS4562, _M0L1iS1410);
      _M0L4gabaS4561 = _M0L1pS1406->$13;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4560
      = _M0MPC15array5Array2atGfE(_M0L4gabaS4561, _M0L1iS1410);
      _M0L6_2atmpS4558 = _M0L6_2atmpS4559 + _M0L6_2atmpS4560;
      #line 161 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4557, _M0L1iS1410, _M0L6_2atmpS4558);
      _M0L2geS4563 = _M0L1pS1406->$10;
      _M0L2geS4571 = _M0L1pS1406->$10;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4565 = _M0MPC15array5Array2atGfE(_M0L2geS4571, _M0L1iS1410);
      _M0L2geS4570 = _M0L1pS1406->$10;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4569 = _M0MPC15array5Array2atGfE(_M0L2geS4570, _M0L1iS1410);
      _M0L6_2atmpS4568 = -_M0L6_2atmpS4569;
      _M0L6_2atmpS4567 = _M0L6_2atmpS4568 / _M0L6tau__eS1407;
      _M0L6_2atmpS4566 = _M0L2dtS1411 * _M0L6_2atmpS4567;
      _M0L6_2atmpS4564 = _M0L6_2atmpS4565 + _M0L6_2atmpS4566;
      #line 162 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4563, _M0L1iS1410, _M0L6_2atmpS4564);
      _M0L2giS4572 = _M0L1pS1406->$11;
      _M0L2giS4580 = _M0L1pS1406->$11;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4574 = _M0MPC15array5Array2atGfE(_M0L2giS4580, _M0L1iS1410);
      _M0L2giS4579 = _M0L1pS1406->$11;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0L6_2atmpS4578 = _M0MPC15array5Array2atGfE(_M0L2giS4579, _M0L1iS1410);
      _M0L6_2atmpS4577 = -_M0L6_2atmpS4578;
      _M0L6_2atmpS4576 = _M0L6_2atmpS4577 / _M0L6tau__iS1408;
      _M0L6_2atmpS4575 = _M0L2dtS1411 * _M0L6_2atmpS4576;
      _M0L6_2atmpS4573 = _M0L6_2atmpS4574 + _M0L6_2atmpS4575;
      #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4572, _M0L1iS1410, _M0L6_2atmpS4573);
      _M0L6_2atmpS4581 = _M0L1iS1410 + 1;
      _M0L1iS1410 = _M0L6_2atmpS4581;
      continue;
    }
    break;
  }
  _M0L7_2abindS1413 = 0;
  _M0L1iS1414 = _M0L7_2abindS1413;
  while (1) {
    if (_M0L1iS1414 < _M0L1nS1405) {
      struct _M0TPB5ArrayGfE* _M0L3gluS4582 = _M0L1pS1406->$12;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4583;
      int32_t _M0L6_2atmpS4584;
      #line 166 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS4582, _M0L1iS1414, 0x0p+0f);
      _M0L4gabaS4583 = _M0L1pS1406->$13;
      #line 167 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex_sinexp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS4583, _M0L1iS1414, 0x0p+0f);
      _M0L6_2atmpS4584 = _M0L1iS1414 + 1;
      _M0L1iS1414 = _M0L6_2atmpS4584;
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
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1384,
  float _M0L2dtS1399
) {
  int32_t _M0L1nS1383;
  struct _M0TP26RiantR8snn__mbt13AdExParameter* _M0L3p__S1385;
  float _M0L2tmS1386;
  float _M0L2vtS1387;
  float _M0L2vrS1388;
  float _M0L2elS1389;
  float _M0L1rS1390;
  float _M0L9dt__slopeS1391;
  float _M0L2twS1392;
  float _M0L1aS1393;
  float _M0L1bS1394;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4550;
  float _M0L2atS1395;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4549;
  float _M0L6tau__aS1396;
  struct _M0TP26RiantR8snn__mbt13AdExPostSpike* _M0L5spikeS4548;
  float _M0L11tabs__constS1397;
  float _M0L6_2atmpS4547;
  int32_t _M0L11tabs__stepsS1398;
  int32_t _M0L7_2abindS1400;
  int32_t _M0L1iS1401;
  #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1383 = _M0L1pS1384->$2;
  _M0L3p__S1385 = _M0L1pS1384->$0;
  _M0L2tmS1386 = _M0L3p__S1385->$5;
  _M0L2vtS1387 = _M0L3p__S1385->$2;
  _M0L2vrS1388 = _M0L3p__S1385->$3;
  _M0L2elS1389 = _M0L3p__S1385->$4;
  _M0L1rS1390 = _M0L3p__S1385->$6;
  _M0L9dt__slopeS1391 = _M0L3p__S1385->$7;
  _M0L2twS1392 = _M0L3p__S1385->$8;
  _M0L1aS1393 = _M0L3p__S1385->$9;
  _M0L1bS1394 = _M0L3p__S1385->$10;
  _M0L5spikeS4550 = _M0L1pS1384->$1;
  _M0L2atS1395 = _M0L5spikeS4550->$0;
  _M0L5spikeS4549 = _M0L1pS1384->$1;
  _M0L6tau__aS1396 = _M0L5spikeS4549->$1;
  _M0L5spikeS4548 = _M0L1pS1384->$1;
  _M0L11tabs__constS1397 = _M0L5spikeS4548->$3;
  _M0L6_2atmpS4547 = _M0L11tabs__constS1397 / _M0L2dtS1399;
  #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L11tabs__stepsS1398 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4547);
  _M0L7_2abindS1400 = 0;
  _M0L1iS1401 = _M0L7_2abindS1400;
  while (1) {
    if (_M0L1iS1401 < _M0L1nS1383) {
      struct _M0TPB5ArrayGfE* _M0L1vS4460 = _M0L1pS1384->$3;
      struct _M0TPB5ArrayGbE* _M0L4fireS4462 = _M0L1pS1384->$5;
      float _M0L6_2atmpS4461;
      struct _M0TPB5ArrayGbE* _M0L4fireS4464;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4465;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4468;
      int32_t _M0L6_2atmpS4467;
      int32_t _M0L6_2atmpS4466;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4470;
      int32_t _M0L6_2atmpS4469;
      struct _M0TPB5ArrayGfE* _M0L1wS4471;
      struct _M0TPB5ArrayGfE* _M0L1wS4483;
      float _M0L6_2atmpS4473;
      struct _M0TPB5ArrayGfE* _M0L1vS4482;
      float _M0L6_2atmpS4481;
      float _M0L6_2atmpS4480;
      float _M0L6_2atmpS4477;
      struct _M0TPB5ArrayGfE* _M0L1wS4479;
      float _M0L6_2atmpS4478;
      float _M0L6_2atmpS4476;
      float _M0L6_2atmpS4475;
      float _M0L6_2atmpS4474;
      float _M0L6_2atmpS4472;
      float _M0L9exp__termS1404;
      struct _M0TPB5ArrayGfE* _M0L1vS4484;
      struct _M0TPB5ArrayGfE* _M0L1vS4506;
      float _M0L6_2atmpS4486;
      struct _M0TPB5ArrayGfE* _M0L1vS4505;
      float _M0L6_2atmpS4504;
      float _M0L6_2atmpS4503;
      float _M0L6_2atmpS4502;
      float _M0L6_2atmpS4498;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4501;
      float _M0L6_2atmpS4500;
      float _M0L6_2atmpS4499;
      float _M0L6_2atmpS4494;
      struct _M0TPB5ArrayGfE* _M0L1wS4497;
      float _M0L6_2atmpS4496;
      float _M0L6_2atmpS4495;
      float _M0L6_2atmpS4490;
      struct _M0TPB5ArrayGfE* _M0L1iS4493;
      float _M0L6_2atmpS4492;
      float _M0L6_2atmpS4491;
      float _M0L6_2atmpS4489;
      float _M0L6_2atmpS4488;
      float _M0L6_2atmpS4487;
      float _M0L6_2atmpS4485;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4507;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4515;
      float _M0L6_2atmpS4509;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4514;
      float _M0L6_2atmpS4513;
      float _M0L6_2atmpS4512;
      float _M0L6_2atmpS4511;
      float _M0L6_2atmpS4510;
      float _M0L6_2atmpS4508;
      struct _M0TPB5ArrayGbE* _M0L4fireS4516;
      struct _M0TPB5ArrayGfE* _M0L1vS4519;
      float _M0L6_2atmpS4518;
      int32_t _M0L6_2atmpS4517;
      struct _M0TPB5ArrayGfE* _M0L1vS4520;
      struct _M0TPB5ArrayGbE* _M0L4fireS4522;
      float _M0L6_2atmpS4521;
      struct _M0TPB5ArrayGfE* _M0L1wS4524;
      struct _M0TPB5ArrayGbE* _M0L4fireS4526;
      float _M0L6_2atmpS4525;
      struct _M0TPB5ArrayGfE* _M0L9thresholdS4530;
      struct _M0TPB5ArrayGbE* _M0L4fireS4532;
      float _M0L6_2atmpS4531;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4536;
      struct _M0TPB5ArrayGbE* _M0L4fireS4538;
      int32_t _M0L6_2atmpS4537;
      int32_t _M0L6_2atmpS4459;
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4462, _M0L1iS1401)) {
        _M0L6_2atmpS4461 = _M0L2vrS1388;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4463 = _M0L1pS1384->$3;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4461
        = _M0MPC15array5Array2atGfE(_M0L1vS4463, _M0L1iS1401);
      }
      #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4460, _M0L1iS1401, _M0L6_2atmpS4461);
      _M0L4fireS4464 = _M0L1pS1384->$5;
      #line 204 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4464, _M0L1iS1401, 0);
      _M0L4tabsS4465 = _M0L1pS1384->$7;
      _M0L4tabsS4468 = _M0L1pS1384->$7;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4467
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4468, _M0L1iS1401);
      _M0L6_2atmpS4466 = _M0L6_2atmpS4467 - 1;
      #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4465, _M0L1iS1401, _M0L6_2atmpS4466);
      _M0L4tabsS4470 = _M0L1pS1384->$7;
      #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4469
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4470, _M0L1iS1401);
      if (_M0L6_2atmpS4469 > 0) {
        goto join_1402;
      }
      _M0L1wS4471 = _M0L1pS1384->$4;
      _M0L1wS4483 = _M0L1pS1384->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4473 = _M0MPC15array5Array2atGfE(_M0L1wS4483, _M0L1iS1401);
      _M0L1vS4482 = _M0L1pS1384->$3;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4481 = _M0MPC15array5Array2atGfE(_M0L1vS4482, _M0L1iS1401);
      _M0L6_2atmpS4480 = _M0L6_2atmpS4481 - _M0L2elS1389;
      _M0L6_2atmpS4477 = _M0L1aS1393 * _M0L6_2atmpS4480;
      _M0L1wS4479 = _M0L1pS1384->$4;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4478 = _M0MPC15array5Array2atGfE(_M0L1wS4479, _M0L1iS1401);
      _M0L6_2atmpS4476 = _M0L6_2atmpS4477 - _M0L6_2atmpS4478;
      _M0L6_2atmpS4475 = _M0L2dtS1399 * _M0L6_2atmpS4476;
      _M0L6_2atmpS4474 = _M0L6_2atmpS4475 / _M0L2twS1392;
      _M0L6_2atmpS4472 = _M0L6_2atmpS4473 + _M0L6_2atmpS4474;
      #line 211 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4471, _M0L1iS1401, _M0L6_2atmpS4472);
      if (_M0L9dt__slopeS1391 < 0x0p+0f) {
        _M0L9exp__termS1404 = 0x0p+0f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4546 = _M0L1pS1384->$3;
        float _M0L6_2atmpS4543;
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4545;
        float _M0L6_2atmpS4544;
        float _M0L6_2atmpS4542;
        float _M0L6_2atmpS4541;
        float _M0L6_2atmpS4540;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4543
        = _M0MPC15array5Array2atGfE(_M0L1vS4546, _M0L1iS1401);
        _M0L9thresholdS4545 = _M0L1pS1384->$6;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4544
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4545, _M0L1iS1401);
        _M0L6_2atmpS4542 = _M0L6_2atmpS4543 - _M0L6_2atmpS4544;
        _M0L6_2atmpS4541 = _M0L6_2atmpS4542 / _M0L9dt__slopeS1391;
        #line 217 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4540 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS4541);
        _M0L9exp__termS1404 = _M0L9dt__slopeS1391 * _M0L6_2atmpS4540;
      }
      _M0L1vS4484 = _M0L1pS1384->$3;
      _M0L1vS4506 = _M0L1pS1384->$3;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4486 = _M0MPC15array5Array2atGfE(_M0L1vS4506, _M0L1iS1401);
      _M0L1vS4505 = _M0L1pS1384->$3;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4504 = _M0MPC15array5Array2atGfE(_M0L1vS4505, _M0L1iS1401);
      _M0L6_2atmpS4503 = _M0L6_2atmpS4504 - _M0L2elS1389;
      _M0L6_2atmpS4502 = -_M0L6_2atmpS4503;
      _M0L6_2atmpS4498 = _M0L6_2atmpS4502 + _M0L9exp__termS1404;
      _M0L9syn__currS4501 = _M0L1pS1384->$9;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4500
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS4501, _M0L1iS1401);
      _M0L6_2atmpS4499 = _M0L1rS1390 * _M0L6_2atmpS4500;
      _M0L6_2atmpS4494 = _M0L6_2atmpS4498 - _M0L6_2atmpS4499;
      _M0L1wS4497 = _M0L1pS1384->$4;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4496 = _M0MPC15array5Array2atGfE(_M0L1wS4497, _M0L1iS1401);
      _M0L6_2atmpS4495 = _M0L1rS1390 * _M0L6_2atmpS4496;
      _M0L6_2atmpS4490 = _M0L6_2atmpS4494 - _M0L6_2atmpS4495;
      _M0L1iS4493 = _M0L1pS1384->$8;
      #line 223 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4492 = _M0MPC15array5Array2atGfE(_M0L1iS4493, _M0L1iS1401);
      _M0L6_2atmpS4491 = _M0L1rS1390 * _M0L6_2atmpS4492;
      _M0L6_2atmpS4489 = _M0L6_2atmpS4490 + _M0L6_2atmpS4491;
      _M0L6_2atmpS4488 = _M0L2dtS1399 * _M0L6_2atmpS4489;
      _M0L6_2atmpS4487 = _M0L6_2atmpS4488 / _M0L2tmS1386;
      _M0L6_2atmpS4485 = _M0L6_2atmpS4486 + _M0L6_2atmpS4487;
      #line 219 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4484, _M0L1iS1401, _M0L6_2atmpS4485);
      _M0L9thresholdS4507 = _M0L1pS1384->$6;
      _M0L9thresholdS4515 = _M0L1pS1384->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4509
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4515, _M0L1iS1401);
      _M0L9thresholdS4514 = _M0L1pS1384->$6;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4513
      = _M0MPC15array5Array2atGfE(_M0L9thresholdS4514, _M0L1iS1401);
      _M0L6_2atmpS4512 = _M0L2vtS1387 - _M0L6_2atmpS4513;
      _M0L6_2atmpS4511 = _M0L2dtS1399 * _M0L6_2atmpS4512;
      _M0L6_2atmpS4510 = _M0L6_2atmpS4511 / _M0L6tau__aS1396;
      _M0L6_2atmpS4508 = _M0L6_2atmpS4509 + _M0L6_2atmpS4510;
      #line 227 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4507, _M0L1iS1401, _M0L6_2atmpS4508);
      _M0L4fireS4516 = _M0L1pS1384->$5;
      _M0L1vS4519 = _M0L1pS1384->$3;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4518 = _M0MPC15array5Array2atGfE(_M0L1vS4519, _M0L1iS1401);
      _M0L6_2atmpS4517 = _M0L6_2atmpS4518 >= 0x0p+0f;
      #line 230 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4516, _M0L1iS1401, _M0L6_2atmpS4517);
      _M0L1vS4520 = _M0L1pS1384->$3;
      _M0L4fireS4522 = _M0L1pS1384->$5;
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4522, _M0L1iS1401)) {
        _M0L6_2atmpS4521 = 0x1.4p+4f;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4523 = _M0L1pS1384->$3;
        #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4521
        = _M0MPC15array5Array2atGfE(_M0L1vS4523, _M0L1iS1401);
      }
      #line 231 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4520, _M0L1iS1401, _M0L6_2atmpS4521);
      _M0L1wS4524 = _M0L1pS1384->$4;
      _M0L4fireS4526 = _M0L1pS1384->$5;
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4526, _M0L1iS1401)) {
        struct _M0TPB5ArrayGfE* _M0L1wS4528 = _M0L1pS1384->$4;
        float _M0L6_2atmpS4527;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4527
        = _M0MPC15array5Array2atGfE(_M0L1wS4528, _M0L1iS1401);
        _M0L6_2atmpS4525 = _M0L6_2atmpS4527 + _M0L1bS1394;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1wS4529 = _M0L1pS1384->$4;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4525
        = _M0MPC15array5Array2atGfE(_M0L1wS4529, _M0L1iS1401);
      }
      #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L1wS4524, _M0L1iS1401, _M0L6_2atmpS4525);
      _M0L9thresholdS4530 = _M0L1pS1384->$6;
      _M0L4fireS4532 = _M0L1pS1384->$5;
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4532, _M0L1iS1401)) {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4534 = _M0L1pS1384->$6;
        float _M0L6_2atmpS4533;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4533
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4534, _M0L1iS1401);
        _M0L6_2atmpS4531 = _M0L6_2atmpS4533 + _M0L2atS1395;
      } else {
        struct _M0TPB5ArrayGfE* _M0L9thresholdS4535 = _M0L1pS1384->$6;
        #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4531
        = _M0MPC15array5Array2atGfE(_M0L9thresholdS4535, _M0L1iS1401);
      }
      #line 233 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9thresholdS4530, _M0L1iS1401, _M0L6_2atmpS4531);
      _M0L4tabsS4536 = _M0L1pS1384->$7;
      _M0L4fireS4538 = _M0L1pS1384->$5;
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4538, _M0L1iS1401)) {
        _M0L6_2atmpS4537 = _M0L11tabs__stepsS1398;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4539 = _M0L1pS1384->$7;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
        _M0L6_2atmpS4537
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4539, _M0L1iS1401);
      }
      #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4536, _M0L1iS1401, _M0L6_2atmpS4537);
      goto join_1402;
      goto joinlet_5838;
      join_1402:;
      _M0L6_2atmpS4459 = _M0L1iS1401 + 1;
      _M0L1iS1401 = _M0L6_2atmpS4459;
      continue;
      joinlet_5838:;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23adex__synaptic__current(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1379
) {
  int32_t _M0L1nS1378;
  int32_t _M0L7_2abindS1380;
  int32_t _M0L1iS1381;
  #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1378 = _M0L1pS1379->$2;
  _M0L7_2abindS1380 = 0;
  _M0L1iS1381 = _M0L7_2abindS1380;
  while (1) {
    if (_M0L1iS1381 < _M0L1nS1378) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4436 = _M0L1pS1379->$9;
      struct _M0TPB5ArrayGfE* _M0L2geS4457 = _M0L1pS1379->$10;
      float _M0L6_2atmpS4452;
      struct _M0TPB5ArrayGfE* _M0L1vS4456;
      float _M0L6_2atmpS4454;
      float _M0L4e__eS4455;
      float _M0L6_2atmpS4453;
      float _M0L6_2atmpS4449;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS4451;
      float _M0L6_2atmpS4450;
      float _M0L6_2atmpS4438;
      struct _M0TPB5ArrayGfE* _M0L2giS4448;
      float _M0L6_2atmpS4443;
      struct _M0TPB5ArrayGfE* _M0L1vS4447;
      float _M0L6_2atmpS4445;
      float _M0L4e__iS4446;
      float _M0L6_2atmpS4444;
      float _M0L6_2atmpS4440;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS4442;
      float _M0L6_2atmpS4441;
      float _M0L6_2atmpS4439;
      float _M0L6_2atmpS4437;
      int32_t _M0L6_2atmpS4458;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4452 = _M0MPC15array5Array2atGfE(_M0L2geS4457, _M0L1iS1381);
      _M0L1vS4456 = _M0L1pS1379->$3;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4454 = _M0MPC15array5Array2atGfE(_M0L1vS4456, _M0L1iS1381);
      _M0L4e__eS4455 = _M0L1pS1379->$18;
      _M0L6_2atmpS4453 = _M0L6_2atmpS4454 - _M0L4e__eS4455;
      _M0L6_2atmpS4449 = _M0L6_2atmpS4452 * _M0L6_2atmpS4453;
      _M0L7gsyn__eS4451 = _M0L1pS1379->$16;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4450
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS4451, _M0L1iS1381);
      _M0L6_2atmpS4438 = _M0L6_2atmpS4449 * _M0L6_2atmpS4450;
      _M0L2giS4448 = _M0L1pS1379->$11;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4443 = _M0MPC15array5Array2atGfE(_M0L2giS4448, _M0L1iS1381);
      _M0L1vS4447 = _M0L1pS1379->$3;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4445 = _M0MPC15array5Array2atGfE(_M0L1vS4447, _M0L1iS1381);
      _M0L4e__iS4446 = _M0L1pS1379->$19;
      _M0L6_2atmpS4444 = _M0L6_2atmpS4445 - _M0L4e__iS4446;
      _M0L6_2atmpS4440 = _M0L6_2atmpS4443 * _M0L6_2atmpS4444;
      _M0L7gsyn__iS4442 = _M0L1pS1379->$17;
      #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4441
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS4442, _M0L1iS1381);
      _M0L6_2atmpS4439 = _M0L6_2atmpS4440 * _M0L6_2atmpS4441;
      _M0L6_2atmpS4437 = _M0L6_2atmpS4438 + _M0L6_2atmpS4439;
      #line 262 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS4436, _M0L1iS1381, _M0L6_2atmpS4437);
      _M0L6_2atmpS4458 = _M0L1iS1381 + 1;
      _M0L1iS1381 = _M0L6_2atmpS4458;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20adex__step__synapses(
  struct _M0TP26RiantR8snn__mbt4AdEx* _M0L1pS1370,
  float _M0L2dtS1373
) {
  int32_t _M0L1nS1369;
  int32_t _M0L7_2abindS1371;
  int32_t _M0L1iS1372;
  int32_t _M0L7_2abindS1375;
  int32_t _M0L1iS1376;
  #line 241 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
  _M0L1nS1369 = _M0L1pS1370->$2;
  _M0L7_2abindS1371 = 0;
  _M0L1iS1372 = _M0L7_2abindS1371;
  while (1) {
    if (_M0L1iS1372 < _M0L1nS1369) {
      struct _M0TPB5ArrayGfE* _M0L2heS4374 = _M0L1pS1370->$12;
      struct _M0TPB5ArrayGfE* _M0L2heS4379 = _M0L1pS1370->$12;
      float _M0L6_2atmpS4376;
      struct _M0TPB5ArrayGfE* _M0L3gluS4378;
      float _M0L6_2atmpS4377;
      float _M0L6_2atmpS4375;
      struct _M0TPB5ArrayGfE* _M0L2hiS4380;
      struct _M0TPB5ArrayGfE* _M0L2hiS4385;
      float _M0L6_2atmpS4382;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4384;
      float _M0L6_2atmpS4383;
      float _M0L6_2atmpS4381;
      struct _M0TPB5ArrayGfE* _M0L2geS4386;
      struct _M0TPB5ArrayGfE* _M0L2geS4398;
      float _M0L6_2atmpS4388;
      struct _M0TPB5ArrayGfE* _M0L2geS4397;
      float _M0L6_2atmpS4396;
      float _M0L6_2atmpS4394;
      float _M0L3tdeS4395;
      float _M0L6_2atmpS4391;
      struct _M0TPB5ArrayGfE* _M0L2heS4393;
      float _M0L6_2atmpS4392;
      float _M0L6_2atmpS4390;
      float _M0L6_2atmpS4389;
      float _M0L6_2atmpS4387;
      struct _M0TPB5ArrayGfE* _M0L2heS4399;
      struct _M0TPB5ArrayGfE* _M0L2heS4408;
      float _M0L6_2atmpS4401;
      struct _M0TPB5ArrayGfE* _M0L2heS4407;
      float _M0L6_2atmpS4406;
      float _M0L6_2atmpS4404;
      float _M0L3treS4405;
      float _M0L6_2atmpS4403;
      float _M0L6_2atmpS4402;
      float _M0L6_2atmpS4400;
      struct _M0TPB5ArrayGfE* _M0L2giS4409;
      struct _M0TPB5ArrayGfE* _M0L2giS4421;
      float _M0L6_2atmpS4411;
      struct _M0TPB5ArrayGfE* _M0L2giS4420;
      float _M0L6_2atmpS4419;
      float _M0L6_2atmpS4417;
      float _M0L3tdiS4418;
      float _M0L6_2atmpS4414;
      struct _M0TPB5ArrayGfE* _M0L2hiS4416;
      float _M0L6_2atmpS4415;
      float _M0L6_2atmpS4413;
      float _M0L6_2atmpS4412;
      float _M0L6_2atmpS4410;
      struct _M0TPB5ArrayGfE* _M0L2hiS4422;
      struct _M0TPB5ArrayGfE* _M0L2hiS4431;
      float _M0L6_2atmpS4424;
      struct _M0TPB5ArrayGfE* _M0L2hiS4430;
      float _M0L6_2atmpS4429;
      float _M0L6_2atmpS4427;
      float _M0L3triS4428;
      float _M0L6_2atmpS4426;
      float _M0L6_2atmpS4425;
      float _M0L6_2atmpS4423;
      int32_t _M0L6_2atmpS4432;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4376 = _M0MPC15array5Array2atGfE(_M0L2heS4379, _M0L1iS1372);
      _M0L3gluS4378 = _M0L1pS1370->$14;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4377
      = _M0MPC15array5Array2atGfE(_M0L3gluS4378, _M0L1iS1372);
      _M0L6_2atmpS4375 = _M0L6_2atmpS4376 + _M0L6_2atmpS4377;
      #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4374, _M0L1iS1372, _M0L6_2atmpS4375);
      _M0L2hiS4380 = _M0L1pS1370->$13;
      _M0L2hiS4385 = _M0L1pS1370->$13;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4382 = _M0MPC15array5Array2atGfE(_M0L2hiS4385, _M0L1iS1372);
      _M0L4gabaS4384 = _M0L1pS1370->$15;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4383
      = _M0MPC15array5Array2atGfE(_M0L4gabaS4384, _M0L1iS1372);
      _M0L6_2atmpS4381 = _M0L6_2atmpS4382 + _M0L6_2atmpS4383;
      #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4380, _M0L1iS1372, _M0L6_2atmpS4381);
      _M0L2geS4386 = _M0L1pS1370->$10;
      _M0L2geS4398 = _M0L1pS1370->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4388 = _M0MPC15array5Array2atGfE(_M0L2geS4398, _M0L1iS1372);
      _M0L2geS4397 = _M0L1pS1370->$10;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4396 = _M0MPC15array5Array2atGfE(_M0L2geS4397, _M0L1iS1372);
      _M0L6_2atmpS4394 = -_M0L6_2atmpS4396;
      _M0L3tdeS4395 = _M0L1pS1370->$21;
      _M0L6_2atmpS4391 = _M0L6_2atmpS4394 / _M0L3tdeS4395;
      _M0L2heS4393 = _M0L1pS1370->$12;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4392 = _M0MPC15array5Array2atGfE(_M0L2heS4393, _M0L1iS1372);
      _M0L6_2atmpS4390 = _M0L6_2atmpS4391 + _M0L6_2atmpS4392;
      _M0L6_2atmpS4389 = _M0L2dtS1373 * _M0L6_2atmpS4390;
      _M0L6_2atmpS4387 = _M0L6_2atmpS4388 + _M0L6_2atmpS4389;
      #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4386, _M0L1iS1372, _M0L6_2atmpS4387);
      _M0L2heS4399 = _M0L1pS1370->$12;
      _M0L2heS4408 = _M0L1pS1370->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4401 = _M0MPC15array5Array2atGfE(_M0L2heS4408, _M0L1iS1372);
      _M0L2heS4407 = _M0L1pS1370->$12;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4406 = _M0MPC15array5Array2atGfE(_M0L2heS4407, _M0L1iS1372);
      _M0L6_2atmpS4404 = -_M0L6_2atmpS4406;
      _M0L3treS4405 = _M0L1pS1370->$20;
      _M0L6_2atmpS4403 = _M0L6_2atmpS4404 / _M0L3treS4405;
      _M0L6_2atmpS4402 = _M0L2dtS1373 * _M0L6_2atmpS4403;
      _M0L6_2atmpS4400 = _M0L6_2atmpS4401 + _M0L6_2atmpS4402;
      #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4399, _M0L1iS1372, _M0L6_2atmpS4400);
      _M0L2giS4409 = _M0L1pS1370->$11;
      _M0L2giS4421 = _M0L1pS1370->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4411 = _M0MPC15array5Array2atGfE(_M0L2giS4421, _M0L1iS1372);
      _M0L2giS4420 = _M0L1pS1370->$11;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4419 = _M0MPC15array5Array2atGfE(_M0L2giS4420, _M0L1iS1372);
      _M0L6_2atmpS4417 = -_M0L6_2atmpS4419;
      _M0L3tdiS4418 = _M0L1pS1370->$23;
      _M0L6_2atmpS4414 = _M0L6_2atmpS4417 / _M0L3tdiS4418;
      _M0L2hiS4416 = _M0L1pS1370->$13;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4415 = _M0MPC15array5Array2atGfE(_M0L2hiS4416, _M0L1iS1372);
      _M0L6_2atmpS4413 = _M0L6_2atmpS4414 + _M0L6_2atmpS4415;
      _M0L6_2atmpS4412 = _M0L2dtS1373 * _M0L6_2atmpS4413;
      _M0L6_2atmpS4410 = _M0L6_2atmpS4411 + _M0L6_2atmpS4412;
      #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4409, _M0L1iS1372, _M0L6_2atmpS4410);
      _M0L2hiS4422 = _M0L1pS1370->$13;
      _M0L2hiS4431 = _M0L1pS1370->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4424 = _M0MPC15array5Array2atGfE(_M0L2hiS4431, _M0L1iS1372);
      _M0L2hiS4430 = _M0L1pS1370->$13;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0L6_2atmpS4429 = _M0MPC15array5Array2atGfE(_M0L2hiS4430, _M0L1iS1372);
      _M0L6_2atmpS4427 = -_M0L6_2atmpS4429;
      _M0L3triS4428 = _M0L1pS1370->$22;
      _M0L6_2atmpS4426 = _M0L6_2atmpS4427 / _M0L3triS4428;
      _M0L6_2atmpS4425 = _M0L2dtS1373 * _M0L6_2atmpS4426;
      _M0L6_2atmpS4423 = _M0L6_2atmpS4424 + _M0L6_2atmpS4425;
      #line 249 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4422, _M0L1iS1372, _M0L6_2atmpS4423);
      _M0L6_2atmpS4432 = _M0L1iS1372 + 1;
      _M0L1iS1372 = _M0L6_2atmpS4432;
      continue;
    }
    break;
  }
  _M0L7_2abindS1375 = 0;
  _M0L1iS1376 = _M0L7_2abindS1375;
  while (1) {
    if (_M0L1iS1376 < _M0L1nS1369) {
      struct _M0TPB5ArrayGfE* _M0L3gluS4433 = _M0L1pS1370->$14;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4434;
      int32_t _M0L6_2atmpS4435;
      #line 252 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS4433, _M0L1iS1376, 0x0p+0f);
      _M0L4gabaS4434 = _M0L1pS1370->$15;
      #line 253 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_adex.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS4434, _M0L1iS1376, 0x0p+0f);
      _M0L6_2atmpS4435 = _M0L1iS1376 + 1;
      _M0L1iS1376 = _M0L6_2atmpS4435;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16forward__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1345,
  float _M0L6t__nowS1356
) {
  struct _M0TPB5ArrayGfE* _M0L6delaysS4373;
  int32_t _M0L6_2atmpS4372;
  int32_t _M0L10use__delayS1344;
  struct _M0TPB5ArrayGfE* _M0L3rhoS4371;
  int32_t _M0L6_2atmpS4370;
  int32_t _M0L8use__rhoS1346;
  #line 246 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6delaysS4373 = _M0L1cS1345->$5;
  #line 247 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS4372 = _M0MPC15array5Array6lengthGfE(_M0L6delaysS4373);
  _M0L10use__delayS1344 = _M0L6_2atmpS4372 > 0;
  _M0L3rhoS4371 = _M0L1cS1345->$6;
  #line 248 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L6_2atmpS4370 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS4371);
  _M0L8use__rhoS1346 = _M0L6_2atmpS4370 > 0;
  if (_M0L10use__delayS1344) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4333 = _M0L1cS1345->$0;
    struct _M0TPB5ArrayGbE* _M0L4fireS4332 = _M0L3preS4333->$5;
    int32_t _M0L6n__preS1347;
    struct _M0TPB8MutLocalGiE* _M0L1jS1348;
    #line 251 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6n__preS1347 = _M0MPC15array5Array6lengthGbE(_M0L4fireS4332);
    _M0L1jS1348
    = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1jS1348)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1jS1348->$0 = 0;
    while (1) {
      int32_t _M0L3valS4301 = _M0L1jS1348->$0;
      if (_M0L3valS4301 < _M0L6n__preS1347) {
        struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4304 = _M0L1cS1345->$0;
        struct _M0TPB5ArrayGbE* _M0L4fireS4302 = _M0L3preS4304->$5;
        int32_t _M0L3valS4303 = _M0L1jS1348->$0;
        int32_t _M0L3valS4331;
        int32_t _M0L6_2atmpS4330;
        #line 254 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        if (_M0MPC15array5Array2atGbE(_M0L4fireS4302, _M0L3valS4303)) {
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4329 =
            _M0L1cS1345->$4;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS4327 = _M0L6matrixS4329->$2;
          int32_t _M0L3valS4328 = _M0L1jS1348->$0;
          int32_t _M0L5startS1349;
          struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4326;
          struct _M0TPB5ArrayGiE* _M0L6rowptrS4323;
          int32_t _M0L3valS4325;
          int32_t _M0L6_2atmpS4324;
          int32_t _M0L3endS1350;
          struct _M0TPB8MutLocalGiE* _M0L1sS1351;
          #line 255 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L5startS1349
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS4327, _M0L3valS4328);
          _M0L6matrixS4326 = _M0L1cS1345->$4;
          _M0L6rowptrS4323 = _M0L6matrixS4326->$2;
          _M0L3valS4325 = _M0L1jS1348->$0;
          _M0L6_2atmpS4324 = _M0L3valS4325 + 1;
          #line 256 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L3endS1350
          = _M0MPC15array5Array2atGiE(_M0L6rowptrS4323, _M0L6_2atmpS4324);
          _M0L1sS1351
          = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
          Moonbit_object_header(_M0L1sS1351)->meta
          = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
          _M0L1sS1351->$0 = _M0L5startS1349;
          while (1) {
            int32_t _M0L3valS4305 = _M0L1sS1351->$0;
            if (_M0L3valS4305 < _M0L3endS1350) {
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4322 =
                _M0L1cS1345->$4;
              struct _M0TPB5ArrayGiE* _M0L6colptrS4320 = _M0L6matrixS4322->$3;
              int32_t _M0L3valS4321 = _M0L1sS1351->$0;
              int32_t _M0L9post__idxS1352;
              struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4319;
              struct _M0TPB5ArrayGfE* _M0L4valsS4317;
              int32_t _M0L3valS4318;
              float _M0L1wS1353;
              struct _M0TPB5ArrayGfE* _M0L6delaysS4315;
              int32_t _M0L3valS4316;
              float _M0L1dS1354;
              float _M0L9w__scaledS1355;
              int32_t _M0L3valS4311;
              int32_t _M0L6_2atmpS4310;
              #line 259 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L9post__idxS1352
              = _M0MPC15array5Array2atGiE(_M0L6colptrS4320, _M0L3valS4321);
              _M0L6matrixS4319 = _M0L1cS1345->$4;
              _M0L4valsS4317 = _M0L6matrixS4319->$4;
              _M0L3valS4318 = _M0L1sS1351->$0;
              #line 260 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1wS1353
              = _M0MPC15array5Array2atGfE(_M0L4valsS4317, _M0L3valS4318);
              _M0L6delaysS4315 = _M0L1cS1345->$5;
              _M0L3valS4316 = _M0L1sS1351->$0;
              #line 261 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
              _M0L1dS1354
              = _M0MPC15array5Array2atGfE(_M0L6delaysS4315, _M0L3valS4316);
              if (_M0L8use__rhoS1346) {
                struct _M0TPB5ArrayGfE* _M0L3rhoS4313 = _M0L1cS1345->$6;
                int32_t _M0L3valS4314 = _M0L1sS1351->$0;
                float _M0L6_2atmpS4312;
                #line 263 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4312
                = _M0MPC15array5Array2atGfE(_M0L3rhoS4313, _M0L3valS4314);
                _M0L9w__scaledS1355 = _M0L1wS1353 * _M0L6_2atmpS4312;
              } else {
                _M0L9w__scaledS1355 = _M0L1wS1353;
              }
              if (_M0L1dS1354 == 0x0p+0f) {
                #line 266 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS1345, _M0L9post__idxS1352, _M0L9w__scaledS1355);
              } else {
                struct _M0TPB5ArrayGfE* _M0L14pending__timesS4306 =
                  _M0L1cS1345->$7;
                float _M0L6_2atmpS4307 = _M0L6t__nowS1356 + _M0L1dS1354;
                struct _M0TPB5ArrayGiE* _M0L14pending__postsS4308;
                struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4309;
                #line 269 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L14pending__timesS4306, _M0L6_2atmpS4307);
                _M0L14pending__postsS4308 = _M0L1cS1345->$8;
                #line 270 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGiE(_M0L14pending__postsS4308, _M0L9post__idxS1352);
                _M0L16pending__weightsS4309 = _M0L1cS1345->$9;
                #line 271 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array4pushGfE(_M0L16pending__weightsS4309, _M0L9w__scaledS1355);
              }
              _M0L3valS4311 = _M0L1sS1351->$0;
              _M0L6_2atmpS4310 = _M0L3valS4311 + 1;
              _M0L1sS1351->$0 = _M0L6_2atmpS4310;
              continue;
            } else {
              moonbit_decref_cycle_free(_M0L1sS1351);
            }
            break;
          }
        }
        _M0L3valS4331 = _M0L1jS1348->$0;
        _M0L6_2atmpS4330 = _M0L3valS4331 + 1;
        _M0L1jS1348->$0 = _M0L6_2atmpS4330;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1jS1348);
      }
      break;
    }
  } else {
    moonbit_string_t _M0L3symS4367 = _M0L1cS1345->$2;
    struct _M0TPB5ArrayGfE* _M0L6targetS1359;
    #line 280 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    if (
      _M0L3symS4367 == (moonbit_string_t)moonbit_string_literal_9.data
      || Moonbit_array_length(_M0L3symS4367)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
         && 0
            == memcmp(_M0L3symS4367, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS4367) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4368 = _M0L1cS1345->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS5534 = _M0L4postS4368->$13;
      moonbit_incref_cycle_free(_M0L8_2afieldS5534);
      _M0L6targetS1359 = _M0L8_2afieldS5534;
    } else {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4369 = _M0L1cS1345->$1;
      struct _M0TPB5ArrayGfE* _M0L8_2afieldS5535 = _M0L4postS4369->$14;
      moonbit_incref_cycle_free(_M0L8_2afieldS5535);
      _M0L6targetS1359 = _M0L8_2afieldS5535;
    }
    if (_M0L8use__rhoS1346) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4363 = _M0L1cS1345->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS4362 = _M0L3preS4363->$5;
      int32_t _M0L6n__preS1360;
      struct _M0TPB8MutLocalGiE* _M0L1jS1361;
      #line 283 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6n__preS1360 = _M0MPC15array5Array6lengthGbE(_M0L4fireS4362);
      _M0L1jS1361
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS1361)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS1361->$0 = 0;
      while (1) {
        int32_t _M0L3valS4334 = _M0L1jS1361->$0;
        if (_M0L3valS4334 < _M0L6n__preS1360) {
          struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4337 = _M0L1cS1345->$0;
          struct _M0TPB5ArrayGbE* _M0L4fireS4335 = _M0L3preS4337->$5;
          int32_t _M0L3valS4336 = _M0L1jS1361->$0;
          int32_t _M0L3valS4361;
          int32_t _M0L6_2atmpS4360;
          #line 286 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          if (_M0MPC15array5Array2atGbE(_M0L4fireS4335, _M0L3valS4336)) {
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4359 =
              _M0L1cS1345->$4;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS4357 = _M0L6matrixS4359->$2;
            int32_t _M0L3valS4358 = _M0L1jS1361->$0;
            int32_t _M0L5startS1362;
            struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4356;
            struct _M0TPB5ArrayGiE* _M0L6rowptrS4353;
            int32_t _M0L3valS4355;
            int32_t _M0L6_2atmpS4354;
            int32_t _M0L3endS1363;
            struct _M0TPB8MutLocalGiE* _M0L1sS1364;
            #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L5startS1362
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS4357, _M0L3valS4358);
            _M0L6matrixS4356 = _M0L1cS1345->$4;
            _M0L6rowptrS4353 = _M0L6matrixS4356->$2;
            _M0L3valS4355 = _M0L1jS1361->$0;
            _M0L6_2atmpS4354 = _M0L3valS4355 + 1;
            #line 288 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
            _M0L3endS1363
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS4353, _M0L6_2atmpS4354);
            _M0L1sS1364
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS1364)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS1364->$0 = _M0L5startS1362;
            while (1) {
              int32_t _M0L3valS4338 = _M0L1sS1364->$0;
              if (_M0L3valS4338 < _M0L3endS1363) {
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4352 =
                  _M0L1cS1345->$4;
                struct _M0TPB5ArrayGiE* _M0L6colptrS4350 =
                  _M0L6matrixS4352->$3;
                int32_t _M0L3valS4351 = _M0L1sS1364->$0;
                int32_t _M0L9post__idxS1365;
                struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4349;
                struct _M0TPB5ArrayGfE* _M0L4valsS4347;
                int32_t _M0L3valS4348;
                float _M0L6_2atmpS4343;
                struct _M0TPB5ArrayGfE* _M0L3rhoS4345;
                int32_t _M0L3valS4346;
                float _M0L6_2atmpS4344;
                float _M0L9w__scaledS1366;
                float _M0L6_2atmpS4340;
                float _M0L6_2atmpS4339;
                int32_t _M0L3valS4342;
                int32_t _M0L6_2atmpS4341;
                #line 291 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L9post__idxS1365
                = _M0MPC15array5Array2atGiE(_M0L6colptrS4350, _M0L3valS4351);
                _M0L6matrixS4349 = _M0L1cS1345->$4;
                _M0L4valsS4347 = _M0L6matrixS4349->$4;
                _M0L3valS4348 = _M0L1sS1364->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4343
                = _M0MPC15array5Array2atGfE(_M0L4valsS4347, _M0L3valS4348);
                _M0L3rhoS4345 = _M0L1cS1345->$6;
                _M0L3valS4346 = _M0L1sS1364->$0;
                #line 292 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4344
                = _M0MPC15array5Array2atGfE(_M0L3rhoS4345, _M0L3valS4346);
                _M0L9w__scaledS1366 = _M0L6_2atmpS4343 * _M0L6_2atmpS4344;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0L6_2atmpS4340
                = _M0MPC15array5Array2atGfE(_M0L6targetS1359, _M0L9post__idxS1365);
                _M0L6_2atmpS4339 = _M0L6_2atmpS4340 + _M0L9w__scaledS1366;
                #line 293 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
                _M0MPC15array5Array3setGfE(_M0L6targetS1359, _M0L9post__idxS1365, _M0L6_2atmpS4339);
                _M0L3valS4342 = _M0L1sS1364->$0;
                _M0L6_2atmpS4341 = _M0L3valS4342 + 1;
                _M0L1sS1364->$0 = _M0L6_2atmpS4341;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS1364);
              }
              break;
            }
          }
          _M0L3valS4361 = _M0L1jS1361->$0;
          _M0L6_2atmpS4360 = _M0L3valS4361 + 1;
          _M0L1jS1361->$0 = _M0L6_2atmpS4360;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1jS1361);
          moonbit_decref_cycle_free(_M0L6targetS1359);
        }
        break;
      }
    } else {
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS4364 =
        _M0L1cS1345->$4;
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS4366 = _M0L1cS1345->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS4365 = _M0L3preS4366->$5;
      #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(_M0L6matrixS4364, _M0L4fireS4365, _M0L6targetS1359);
      moonbit_decref_cycle_free(_M0L6targetS1359);
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25deliver__pending__synapse(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1337,
  float _M0L6t__nowS1340
) {
  struct _M0TPB5ArrayGfE* _M0L14pending__timesS4300;
  int32_t _M0L1nS1336;
  struct _M0TPB8MutLocalGiE* _M0L4keptS1338;
  struct _M0TPB8MutLocalGiE* _M0L1kS1339;
  int32_t _M0L3valS4299;
  int32_t _M0L6_2atmpS4298;
  struct _M0TPB8MutLocalGiE* _M0L4dropS1342;
  #line 312 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L14pending__timesS4300 = _M0L1cS1337->$7;
  #line 313 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L1nS1336 = _M0MPC15array5Array6lengthGfE(_M0L14pending__timesS4300);
  if (_M0L1nS1336 == 0) {
    return 0;
  }
  _M0L4keptS1338
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4keptS1338)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4keptS1338->$0 = 0;
  _M0L1kS1339
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS1339)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS1339->$0 = 0;
  while (1) {
    int32_t _M0L3valS4261 = _M0L1kS1339->$0;
    if (_M0L3valS4261 < _M0L1nS1336) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS4263 = _M0L1cS1337->$7;
      int32_t _M0L3valS4264 = _M0L1kS1339->$0;
      float _M0L6_2atmpS4262;
      int32_t _M0L3valS4291;
      int32_t _M0L6_2atmpS4290;
      #line 320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS4262
      = _M0MPC15array5Array2atGfE(_M0L14pending__timesS4263, _M0L3valS4264);
      if (_M0L6_2atmpS4262 <= _M0L6t__nowS1340) {
        struct _M0TPB5ArrayGiE* _M0L14pending__postsS4269 = _M0L1cS1337->$8;
        int32_t _M0L3valS4270 = _M0L1kS1339->$0;
        int32_t _M0L6_2atmpS4265;
        struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4267;
        int32_t _M0L3valS4268;
        float _M0L6_2atmpS4266;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS4265
        = _M0MPC15array5Array2atGiE(_M0L14pending__postsS4269, _M0L3valS4270);
        _M0L16pending__weightsS4267 = _M0L1cS1337->$9;
        _M0L3valS4268 = _M0L1kS1339->$0;
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0L6_2atmpS4266
        = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS4267, _M0L3valS4268);
        #line 322 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
        _M0FP26RiantR8snn__mbt13apply__weight(_M0L1cS1337, _M0L6_2atmpS4265, _M0L6_2atmpS4266);
      } else {
        int32_t _M0L3valS4271 = _M0L4keptS1338->$0;
        int32_t _M0L3valS4272 = _M0L1kS1339->$0;
        int32_t _M0L3valS4289;
        int32_t _M0L6_2atmpS4288;
        if (_M0L3valS4271 != _M0L3valS4272) {
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS4273 = _M0L1cS1337->$7;
          int32_t _M0L3valS4274 = _M0L4keptS1338->$0;
          struct _M0TPB5ArrayGfE* _M0L14pending__timesS4276 = _M0L1cS1337->$7;
          int32_t _M0L3valS4277 = _M0L1kS1339->$0;
          float _M0L6_2atmpS4275;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS4278;
          int32_t _M0L3valS4279;
          struct _M0TPB5ArrayGiE* _M0L14pending__postsS4281;
          int32_t _M0L3valS4282;
          int32_t _M0L6_2atmpS4280;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4283;
          int32_t _M0L3valS4284;
          struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4286;
          int32_t _M0L3valS4287;
          float _M0L6_2atmpS4285;
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS4275
          = _M0MPC15array5Array2atGfE(_M0L14pending__timesS4276, _M0L3valS4277);
          #line 326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L14pending__timesS4273, _M0L3valS4274, _M0L6_2atmpS4275);
          _M0L14pending__postsS4278 = _M0L1cS1337->$8;
          _M0L3valS4279 = _M0L4keptS1338->$0;
          _M0L14pending__postsS4281 = _M0L1cS1337->$8;
          _M0L3valS4282 = _M0L1kS1339->$0;
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS4280
          = _M0MPC15array5Array2atGiE(_M0L14pending__postsS4281, _M0L3valS4282);
          #line 327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGiE(_M0L14pending__postsS4278, _M0L3valS4279, _M0L6_2atmpS4280);
          _M0L16pending__weightsS4283 = _M0L1cS1337->$9;
          _M0L3valS4284 = _M0L4keptS1338->$0;
          _M0L16pending__weightsS4286 = _M0L1cS1337->$9;
          _M0L3valS4287 = _M0L1kS1339->$0;
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0L6_2atmpS4285
          = _M0MPC15array5Array2atGfE(_M0L16pending__weightsS4286, _M0L3valS4287);
          #line 328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
          _M0MPC15array5Array3setGfE(_M0L16pending__weightsS4283, _M0L3valS4284, _M0L6_2atmpS4285);
        }
        _M0L3valS4289 = _M0L4keptS1338->$0;
        _M0L6_2atmpS4288 = _M0L3valS4289 + 1;
        _M0L4keptS1338->$0 = _M0L6_2atmpS4288;
      }
      _M0L3valS4291 = _M0L1kS1339->$0;
      _M0L6_2atmpS4290 = _M0L3valS4291 + 1;
      _M0L1kS1339->$0 = _M0L6_2atmpS4290;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS1339);
    }
    break;
  }
  _M0L3valS4299 = _M0L4keptS1338->$0;
  moonbit_decref_cycle_free(_M0L4keptS1338);
  _M0L6_2atmpS4298 = _M0L1nS1336 - _M0L3valS4299;
  _M0L4dropS1342
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L4dropS1342)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4dropS1342->$0 = _M0L6_2atmpS4298;
  while (1) {
    int32_t _M0L3valS4292 = _M0L4dropS1342->$0;
    if (_M0L3valS4292 > 0) {
      struct _M0TPB5ArrayGfE* _M0L14pending__timesS4293 = _M0L1cS1337->$7;
      void* _M0L6_2atmpS5537;
      struct _M0TPB5ArrayGiE* _M0L14pending__postsS4294;
      struct _M0TPB5ArrayGfE* _M0L16pending__weightsS4295;
      void* _M0L6_2atmpS5536;
      int32_t _M0L3valS4297;
      int32_t _M0L6_2atmpS4296;
      #line 337 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS5537
      = _M0MPC15array5Array3popGfE(_M0L14pending__timesS4293);
      moonbit_decref_cycle_free(_M0L6_2atmpS5537);
      _M0L14pending__postsS4294 = _M0L1cS1337->$8;
      #line 338 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0MPC15array5Array3popGiE(_M0L14pending__postsS4294);
      _M0L16pending__weightsS4295 = _M0L1cS1337->$9;
      #line 339 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
      _M0L6_2atmpS5536
      = _M0MPC15array5Array3popGfE(_M0L16pending__weightsS4295);
      moonbit_decref_cycle_free(_M0L6_2atmpS5536);
      _M0L3valS4297 = _M0L4dropS1342->$0;
      _M0L6_2atmpS4296 = _M0L3valS4297 - 1;
      _M0L4dropS1342->$0 = _M0L6_2atmpS4296;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4dropS1342);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13apply__weight(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L1cS1333,
  int32_t _M0L9post__idxS1334,
  float _M0L1wS1335
) {
  moonbit_string_t _M0L3symS4248;
  #line 346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  _M0L3symS4248 = _M0L1cS1333->$2;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
  if (
    _M0L3symS4248 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS4248)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS4248, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS4248) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4254 = _M0L1cS1333->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS4249 = _M0L4postS4254->$13;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4253 = _M0L1cS1333->$1;
    struct _M0TPB5ArrayGfE* _M0L3gluS4252 = _M0L4postS4253->$13;
    float _M0L6_2atmpS4251;
    float _M0L6_2atmpS4250;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS4251
    = _M0MPC15array5Array2atGfE(_M0L3gluS4252, _M0L9post__idxS1334);
    _M0L6_2atmpS4250 = _M0L6_2atmpS4251 + _M0L1wS1335;
    #line 348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L3gluS4249, _M0L9post__idxS1334, _M0L6_2atmpS4250);
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4260 = _M0L1cS1333->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS4255 = _M0L4postS4260->$14;
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS4259 = _M0L1cS1333->$1;
    struct _M0TPB5ArrayGfE* _M0L4gabaS4258 = _M0L4postS4259->$14;
    float _M0L6_2atmpS4257;
    float _M0L6_2atmpS4256;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0L6_2atmpS4257
    = _M0MPC15array5Array2atGfE(_M0L4gabaS4258, _M0L9post__idxS1334);
    _M0L6_2atmpS4256 = _M0L6_2atmpS4257 + _M0L1wS1335;
    #line 350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\connection_spiking.mbt"
    _M0MPC15array5Array3setGfE(_M0L4gabaS4255, _M0L9post__idxS1334, _M0L6_2atmpS4256);
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt11record__one(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS1330,
  float _M0L1tS1332
) {
  int32_t _M0L11step__countS4234;
  int32_t _M0L6_2atmpS4233;
  int32_t _M0L11step__countS4236;
  int32_t _M0L9rec__stepS4237;
  int32_t _M0L6_2atmpS4235;
  moonbit_string_t _M0L3symS4240;
  float _M0L1vS1331;
  struct _M0TPB5ArrayGfE* _M0L4dataS4238;
  struct _M0TPB5ArrayGfE* _M0L5timesS4239;
  #line 61 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L11step__countS4234 = _M0L1mS1330->$6;
  _M0L6_2atmpS4233 = _M0L11step__countS4234 + 1;
  _M0L1mS1330->$6 = _M0L6_2atmpS4233;
  _M0L11step__countS4236 = _M0L1mS1330->$6;
  _M0L9rec__stepS4237 = _M0L1mS1330->$5;
  _M0L6_2atmpS4235 = _M0L11step__countS4236 % _M0L9rec__stepS4237;
  if (_M0L6_2atmpS4235 != 0) {
    return 0;
  }
  _M0L3symS4240 = _M0L1mS1330->$1;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  if (
    _M0L3symS4240 == (moonbit_string_t)moonbit_string_literal_10.data
    || Moonbit_array_length(_M0L3symS4240)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_10.data)
       && 0
          == memcmp(_M0L3symS4240, (moonbit_string_t)moonbit_string_literal_10.data, Moonbit_array_length(_M0L3symS4240) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS4243 = _M0L1mS1330->$0;
    struct _M0TPB5ArrayGfE* _M0L1vS4241 = _M0L3popS4243->$3;
    int32_t _M0L6neuronS4242 = _M0L1mS1330->$4;
    #line 67 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    _M0L1vS1331 = _M0MPC15array5Array2atGfE(_M0L1vS4241, _M0L6neuronS4242);
  } else {
    moonbit_string_t _M0L3symS4244 = _M0L1mS1330->$1;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
    if (
      _M0L3symS4244 == (moonbit_string_t)moonbit_string_literal_11.data
      || Moonbit_array_length(_M0L3symS4244)
         == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_11.data)
         && 0
            == memcmp(_M0L3symS4244, (moonbit_string_t)moonbit_string_literal_11.data, Moonbit_array_length(_M0L3symS4244) * 2)
    ) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS4247 = _M0L1mS1330->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS4245 = _M0L3popS4247->$5;
      int32_t _M0L6neuronS4246 = _M0L1mS1330->$4;
      #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4245, _M0L6neuronS4246)) {
        _M0L1vS1331 = 0x1p+0f;
      } else {
        _M0L1vS1331 = 0x0p+0f;
      }
    } else {
      _M0L1vS1331 = 0x0p+0f;
    }
  }
  _M0L4dataS4238 = _M0L1mS1330->$2;
  #line 73 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L4dataS4238, _M0L1vS1331);
  _M0L5timesS4239 = _M0L1mS1330->$3;
  #line 74 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0MPC15array5Array4pushGfE(_M0L5timesS4239, _M0L1tS1332);
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor9new__fire(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS1328,
  int32_t _M0L6neuronS1329
) {
  float* _M0L6_2atmpS4232;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4229;
  float* _M0L6_2atmpS4231;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4230;
  struct _M0TP26RiantR8snn__mbt7Monitor* _block_5848;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6_2atmpS4232 = moonbit_empty_float_array;
  _M0L6_2atmpS4229
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4229)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
  _M0L6_2atmpS4229->$0 = _M0L6_2atmpS4232;
  _M0L6_2atmpS4229->$1 = 0;
  _M0L6_2atmpS4231 = moonbit_empty_float_array;
  _M0L6_2atmpS4230
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4230)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
  _M0L6_2atmpS4230->$0 = _M0L6_2atmpS4231;
  _M0L6_2atmpS4230->$1 = 0;
  moonbit_incref_cycle_free(_M0L3popS1328);
  _block_5848
  = (struct _M0TP26RiantR8snn__mbt7Monitor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Monitor));
  Moonbit_object_header(_block_5848)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 60, 0);
  _block_5848->$0 = _M0L3popS1328;
  _block_5848->$1 = (moonbit_string_t)moonbit_string_literal_11.data;
  _block_5848->$2 = _M0L6_2atmpS4229;
  _block_5848->$3 = _M0L6_2atmpS4230;
  _block_5848->$4 = _M0L6neuronS1329;
  _block_5848->$5 = 1;
  _block_5848->$6 = 0;
  return _block_5848;
}

struct _M0TP26RiantR8snn__mbt7Monitor* _M0MP26RiantR8snn__mbt7Monitor6new__v(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS1326,
  int32_t _M0L6neuronS1327
) {
  float* _M0L6_2atmpS4228;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4225;
  float* _M0L6_2atmpS4227;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS4226;
  struct _M0TP26RiantR8snn__mbt7Monitor* _block_5849;
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sim.mbt"
  _M0L6_2atmpS4228 = moonbit_empty_float_array;
  _M0L6_2atmpS4225
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4225)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
  _M0L6_2atmpS4225->$0 = _M0L6_2atmpS4228;
  _M0L6_2atmpS4225->$1 = 0;
  _M0L6_2atmpS4227 = moonbit_empty_float_array;
  _M0L6_2atmpS4226
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS4226)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
  _M0L6_2atmpS4226->$0 = _M0L6_2atmpS4227;
  _M0L6_2atmpS4226->$1 = 0;
  moonbit_incref_cycle_free(_M0L3popS1326);
  _block_5849
  = (struct _M0TP26RiantR8snn__mbt7Monitor*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Monitor));
  Moonbit_object_header(_block_5849)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 60, 0);
  _block_5849->$0 = _M0L3popS1326;
  _block_5849->$1 = (moonbit_string_t)moonbit_string_literal_10.data;
  _block_5849->$2 = _M0L6_2atmpS4225;
  _block_5849->$3 = _M0L6_2atmpS4226;
  _block_5849->$4 = _M0L6neuronS1327;
  _block_5849->$5 = 1;
  _block_5849->$6 = 0;
  return _block_5849;
}

int32_t _M0FP26RiantR8snn__mbt17synaptic__current(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1322
) {
  int32_t _M0L1nS1321;
  int32_t _M0L7_2abindS1323;
  int32_t _M0L1iS1324;
  #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1321 = _M0L1pS1322->$2;
  _M0L7_2abindS1323 = 0;
  _M0L1iS1324 = _M0L7_2abindS1323;
  while (1) {
    if (_M0L1iS1324 < _M0L1nS1321) {
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4202 = _M0L1pS1322->$8;
      struct _M0TPB5ArrayGfE* _M0L2geS4223 = _M0L1pS1322->$9;
      float _M0L6_2atmpS4218;
      struct _M0TPB5ArrayGfE* _M0L1vS4222;
      float _M0L6_2atmpS4220;
      float _M0L4e__eS4221;
      float _M0L6_2atmpS4219;
      float _M0L6_2atmpS4215;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__eS4217;
      float _M0L6_2atmpS4216;
      float _M0L6_2atmpS4204;
      struct _M0TPB5ArrayGfE* _M0L2giS4214;
      float _M0L6_2atmpS4209;
      struct _M0TPB5ArrayGfE* _M0L1vS4213;
      float _M0L6_2atmpS4211;
      float _M0L4e__iS4212;
      float _M0L6_2atmpS4210;
      float _M0L6_2atmpS4206;
      struct _M0TPB5ArrayGfE* _M0L7gsyn__iS4208;
      float _M0L6_2atmpS4207;
      float _M0L6_2atmpS4205;
      float _M0L6_2atmpS4203;
      int32_t _M0L6_2atmpS4224;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4218 = _M0MPC15array5Array2atGfE(_M0L2geS4223, _M0L1iS1324);
      _M0L1vS4222 = _M0L1pS1322->$3;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4220 = _M0MPC15array5Array2atGfE(_M0L1vS4222, _M0L1iS1324);
      _M0L4e__eS4221 = _M0L1pS1322->$17;
      _M0L6_2atmpS4219 = _M0L6_2atmpS4220 - _M0L4e__eS4221;
      _M0L6_2atmpS4215 = _M0L6_2atmpS4218 * _M0L6_2atmpS4219;
      _M0L7gsyn__eS4217 = _M0L1pS1322->$15;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4216
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__eS4217, _M0L1iS1324);
      _M0L6_2atmpS4204 = _M0L6_2atmpS4215 * _M0L6_2atmpS4216;
      _M0L2giS4214 = _M0L1pS1322->$10;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4209 = _M0MPC15array5Array2atGfE(_M0L2giS4214, _M0L1iS1324);
      _M0L1vS4213 = _M0L1pS1322->$3;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4211 = _M0MPC15array5Array2atGfE(_M0L1vS4213, _M0L1iS1324);
      _M0L4e__iS4212 = _M0L1pS1322->$18;
      _M0L6_2atmpS4210 = _M0L6_2atmpS4211 - _M0L4e__iS4212;
      _M0L6_2atmpS4206 = _M0L6_2atmpS4209 * _M0L6_2atmpS4210;
      _M0L7gsyn__iS4208 = _M0L1pS1322->$16;
      #line 169 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4207
      = _M0MPC15array5Array2atGfE(_M0L7gsyn__iS4208, _M0L1iS1324);
      _M0L6_2atmpS4205 = _M0L6_2atmpS4206 * _M0L6_2atmpS4207;
      _M0L6_2atmpS4203 = _M0L6_2atmpS4204 + _M0L6_2atmpS4205;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L9syn__currS4202, _M0L1iS1324, _M0L6_2atmpS4203);
      _M0L6_2atmpS4224 = _M0L1iS1324 + 1;
      _M0L1iS1324 = _M0L6_2atmpS4224;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt14step__synapses(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1313,
  float _M0L2dtS1316
) {
  int32_t _M0L1nS1312;
  int32_t _M0L7_2abindS1314;
  int32_t _M0L1iS1315;
  int32_t _M0L7_2abindS1318;
  int32_t _M0L1iS1319;
  #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1312 = _M0L1pS1313->$2;
  _M0L7_2abindS1314 = 0;
  _M0L1iS1315 = _M0L7_2abindS1314;
  while (1) {
    if (_M0L1iS1315 < _M0L1nS1312) {
      struct _M0TPB5ArrayGfE* _M0L2heS4140 = _M0L1pS1313->$11;
      struct _M0TPB5ArrayGfE* _M0L2heS4145 = _M0L1pS1313->$11;
      float _M0L6_2atmpS4142;
      struct _M0TPB5ArrayGfE* _M0L3gluS4144;
      float _M0L6_2atmpS4143;
      float _M0L6_2atmpS4141;
      struct _M0TPB5ArrayGfE* _M0L2hiS4146;
      struct _M0TPB5ArrayGfE* _M0L2hiS4151;
      float _M0L6_2atmpS4148;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4150;
      float _M0L6_2atmpS4149;
      float _M0L6_2atmpS4147;
      struct _M0TPB5ArrayGfE* _M0L2geS4152;
      struct _M0TPB5ArrayGfE* _M0L2geS4164;
      float _M0L6_2atmpS4154;
      struct _M0TPB5ArrayGfE* _M0L2geS4163;
      float _M0L6_2atmpS4162;
      float _M0L6_2atmpS4160;
      float _M0L3tdeS4161;
      float _M0L6_2atmpS4157;
      struct _M0TPB5ArrayGfE* _M0L2heS4159;
      float _M0L6_2atmpS4158;
      float _M0L6_2atmpS4156;
      float _M0L6_2atmpS4155;
      float _M0L6_2atmpS4153;
      struct _M0TPB5ArrayGfE* _M0L2heS4165;
      struct _M0TPB5ArrayGfE* _M0L2heS4174;
      float _M0L6_2atmpS4167;
      struct _M0TPB5ArrayGfE* _M0L2heS4173;
      float _M0L6_2atmpS4172;
      float _M0L6_2atmpS4170;
      float _M0L3treS4171;
      float _M0L6_2atmpS4169;
      float _M0L6_2atmpS4168;
      float _M0L6_2atmpS4166;
      struct _M0TPB5ArrayGfE* _M0L2giS4175;
      struct _M0TPB5ArrayGfE* _M0L2giS4187;
      float _M0L6_2atmpS4177;
      struct _M0TPB5ArrayGfE* _M0L2giS4186;
      float _M0L6_2atmpS4185;
      float _M0L6_2atmpS4183;
      float _M0L3tdiS4184;
      float _M0L6_2atmpS4180;
      struct _M0TPB5ArrayGfE* _M0L2hiS4182;
      float _M0L6_2atmpS4181;
      float _M0L6_2atmpS4179;
      float _M0L6_2atmpS4178;
      float _M0L6_2atmpS4176;
      struct _M0TPB5ArrayGfE* _M0L2hiS4188;
      struct _M0TPB5ArrayGfE* _M0L2hiS4197;
      float _M0L6_2atmpS4190;
      struct _M0TPB5ArrayGfE* _M0L2hiS4196;
      float _M0L6_2atmpS4195;
      float _M0L6_2atmpS4193;
      float _M0L3triS4194;
      float _M0L6_2atmpS4192;
      float _M0L6_2atmpS4191;
      float _M0L6_2atmpS4189;
      int32_t _M0L6_2atmpS4198;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4142 = _M0MPC15array5Array2atGfE(_M0L2heS4145, _M0L1iS1315);
      _M0L3gluS4144 = _M0L1pS1313->$13;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4143
      = _M0MPC15array5Array2atGfE(_M0L3gluS4144, _M0L1iS1315);
      _M0L6_2atmpS4141 = _M0L6_2atmpS4142 + _M0L6_2atmpS4143;
      #line 149 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4140, _M0L1iS1315, _M0L6_2atmpS4141);
      _M0L2hiS4146 = _M0L1pS1313->$12;
      _M0L2hiS4151 = _M0L1pS1313->$12;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4148 = _M0MPC15array5Array2atGfE(_M0L2hiS4151, _M0L1iS1315);
      _M0L4gabaS4150 = _M0L1pS1313->$14;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4149
      = _M0MPC15array5Array2atGfE(_M0L4gabaS4150, _M0L1iS1315);
      _M0L6_2atmpS4147 = _M0L6_2atmpS4148 + _M0L6_2atmpS4149;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4146, _M0L1iS1315, _M0L6_2atmpS4147);
      _M0L2geS4152 = _M0L1pS1313->$9;
      _M0L2geS4164 = _M0L1pS1313->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4154 = _M0MPC15array5Array2atGfE(_M0L2geS4164, _M0L1iS1315);
      _M0L2geS4163 = _M0L1pS1313->$9;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4162 = _M0MPC15array5Array2atGfE(_M0L2geS4163, _M0L1iS1315);
      _M0L6_2atmpS4160 = -_M0L6_2atmpS4162;
      _M0L3tdeS4161 = _M0L1pS1313->$20;
      _M0L6_2atmpS4157 = _M0L6_2atmpS4160 / _M0L3tdeS4161;
      _M0L2heS4159 = _M0L1pS1313->$11;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4158 = _M0MPC15array5Array2atGfE(_M0L2heS4159, _M0L1iS1315);
      _M0L6_2atmpS4156 = _M0L6_2atmpS4157 + _M0L6_2atmpS4158;
      _M0L6_2atmpS4155 = _M0L2dtS1316 * _M0L6_2atmpS4156;
      _M0L6_2atmpS4153 = _M0L6_2atmpS4154 + _M0L6_2atmpS4155;
      #line 151 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2geS4152, _M0L1iS1315, _M0L6_2atmpS4153);
      _M0L2heS4165 = _M0L1pS1313->$11;
      _M0L2heS4174 = _M0L1pS1313->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4167 = _M0MPC15array5Array2atGfE(_M0L2heS4174, _M0L1iS1315);
      _M0L2heS4173 = _M0L1pS1313->$11;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4172 = _M0MPC15array5Array2atGfE(_M0L2heS4173, _M0L1iS1315);
      _M0L6_2atmpS4170 = -_M0L6_2atmpS4172;
      _M0L3treS4171 = _M0L1pS1313->$19;
      _M0L6_2atmpS4169 = _M0L6_2atmpS4170 / _M0L3treS4171;
      _M0L6_2atmpS4168 = _M0L2dtS1316 * _M0L6_2atmpS4169;
      _M0L6_2atmpS4166 = _M0L6_2atmpS4167 + _M0L6_2atmpS4168;
      #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2heS4165, _M0L1iS1315, _M0L6_2atmpS4166);
      _M0L2giS4175 = _M0L1pS1313->$10;
      _M0L2giS4187 = _M0L1pS1313->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4177 = _M0MPC15array5Array2atGfE(_M0L2giS4187, _M0L1iS1315);
      _M0L2giS4186 = _M0L1pS1313->$10;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4185 = _M0MPC15array5Array2atGfE(_M0L2giS4186, _M0L1iS1315);
      _M0L6_2atmpS4183 = -_M0L6_2atmpS4185;
      _M0L3tdiS4184 = _M0L1pS1313->$22;
      _M0L6_2atmpS4180 = _M0L6_2atmpS4183 / _M0L3tdiS4184;
      _M0L2hiS4182 = _M0L1pS1313->$12;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4181 = _M0MPC15array5Array2atGfE(_M0L2hiS4182, _M0L1iS1315);
      _M0L6_2atmpS4179 = _M0L6_2atmpS4180 + _M0L6_2atmpS4181;
      _M0L6_2atmpS4178 = _M0L2dtS1316 * _M0L6_2atmpS4179;
      _M0L6_2atmpS4176 = _M0L6_2atmpS4177 + _M0L6_2atmpS4178;
      #line 153 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2giS4175, _M0L1iS1315, _M0L6_2atmpS4176);
      _M0L2hiS4188 = _M0L1pS1313->$12;
      _M0L2hiS4197 = _M0L1pS1313->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4190 = _M0MPC15array5Array2atGfE(_M0L2hiS4197, _M0L1iS1315);
      _M0L2hiS4196 = _M0L1pS1313->$12;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4195 = _M0MPC15array5Array2atGfE(_M0L2hiS4196, _M0L1iS1315);
      _M0L6_2atmpS4193 = -_M0L6_2atmpS4195;
      _M0L3triS4194 = _M0L1pS1313->$21;
      _M0L6_2atmpS4192 = _M0L6_2atmpS4193 / _M0L3triS4194;
      _M0L6_2atmpS4191 = _M0L2dtS1316 * _M0L6_2atmpS4192;
      _M0L6_2atmpS4189 = _M0L6_2atmpS4190 + _M0L6_2atmpS4191;
      #line 154 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L2hiS4188, _M0L1iS1315, _M0L6_2atmpS4189);
      _M0L6_2atmpS4198 = _M0L1iS1315 + 1;
      _M0L1iS1315 = _M0L6_2atmpS4198;
      continue;
    }
    break;
  }
  _M0L7_2abindS1318 = 0;
  _M0L1iS1319 = _M0L7_2abindS1318;
  while (1) {
    if (_M0L1iS1319 < _M0L1nS1312) {
      struct _M0TPB5ArrayGfE* _M0L3gluS4199 = _M0L1pS1313->$13;
      struct _M0TPB5ArrayGfE* _M0L4gabaS4200;
      int32_t _M0L6_2atmpS4201;
      #line 157 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L3gluS4199, _M0L1iS1319, 0x0p+0f);
      _M0L4gabaS4200 = _M0L1pS1313->$14;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L4gabaS4200, _M0L1iS1319, 0x0p+0f);
      _M0L6_2atmpS4201 = _M0L1iS1319 + 1;
      _M0L1iS1319 = _M0L6_2atmpS4201;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12step__neuron(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L1pS1298,
  float _M0L2dtS1307
) {
  int32_t _M0L1nS1297;
  struct _M0TP26RiantR8snn__mbt11IFParameter* _M0L3p__S1299;
  float _M0L2tmS1300;
  float _M0L2elS1301;
  float _M0L1rS1302;
  float _M0L2vtS1303;
  float _M0L2vrS1304;
  struct _M0TP26RiantR8snn__mbt9PostSpike* _M0L5spikeS4139;
  float _M0L11tabs__constS1305;
  float _M0L6_2atmpS4138;
  int32_t _M0L11tabs__stepsS1306;
  int32_t _M0L7_2abindS1308;
  int32_t _M0L1iS1309;
  #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L1nS1297 = _M0L1pS1298->$2;
  _M0L3p__S1299 = _M0L1pS1298->$0;
  _M0L2tmS1300 = _M0L3p__S1299->$2;
  _M0L2elS1301 = _M0L3p__S1299->$5;
  _M0L1rS1302 = _M0L3p__S1299->$6;
  _M0L2vtS1303 = _M0L3p__S1299->$3;
  _M0L2vrS1304 = _M0L3p__S1299->$4;
  _M0L5spikeS4139 = _M0L1pS1298->$1;
  _M0L11tabs__constS1305 = _M0L5spikeS4139->$0;
  _M0L6_2atmpS4138 = _M0L11tabs__constS1305 / _M0L2dtS1307;
  #line 185 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
  _M0L11tabs__stepsS1306 = _M0MPC15float5Float7to__int(_M0L6_2atmpS4138);
  _M0L7_2abindS1308 = 0;
  _M0L1iS1309 = _M0L7_2abindS1308;
  while (1) {
    if (_M0L1iS1309 < _M0L1nS1297) {
      struct _M0TPB5ArrayGiE* _M0L4tabsS4098 = _M0L1pS1298->$6;
      int32_t _M0L6_2atmpS4097;
      struct _M0TPB5ArrayGfE* _M0L1vS4104;
      struct _M0TPB5ArrayGfE* _M0L1vS4125;
      float _M0L6_2atmpS4106;
      float _M0L6_2atmpS4108;
      struct _M0TPB5ArrayGfE* _M0L1vS4124;
      float _M0L6_2atmpS4123;
      float _M0L6_2atmpS4122;
      float _M0L6_2atmpS4114;
      struct _M0TPB5ArrayGfE* _M0L1wS4121;
      float _M0L6_2atmpS4120;
      float _M0L6_2atmpS4117;
      struct _M0TPB5ArrayGfE* _M0L1iS4119;
      float _M0L6_2atmpS4118;
      float _M0L6_2atmpS4116;
      float _M0L6_2atmpS4115;
      float _M0L6_2atmpS4110;
      struct _M0TPB5ArrayGfE* _M0L9syn__currS4113;
      float _M0L6_2atmpS4112;
      float _M0L6_2atmpS4111;
      float _M0L6_2atmpS4109;
      float _M0L6_2atmpS4107;
      float _M0L6_2atmpS4105;
      struct _M0TPB5ArrayGbE* _M0L4fireS4126;
      struct _M0TPB5ArrayGfE* _M0L1vS4129;
      float _M0L6_2atmpS4128;
      int32_t _M0L6_2atmpS4127;
      struct _M0TPB5ArrayGfE* _M0L1vS4130;
      struct _M0TPB5ArrayGbE* _M0L4fireS4132;
      float _M0L6_2atmpS4131;
      struct _M0TPB5ArrayGiE* _M0L4tabsS4134;
      struct _M0TPB5ArrayGbE* _M0L4fireS4136;
      int32_t _M0L6_2atmpS4135;
      int32_t _M0L6_2atmpS4096;
      #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4097
      = _M0MPC15array5Array2atGiE(_M0L4tabsS4098, _M0L1iS1309);
      if (_M0L6_2atmpS4097 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS4099 = _M0L1pS1298->$5;
        struct _M0TPB5ArrayGiE* _M0L4tabsS4100;
        struct _M0TPB5ArrayGiE* _M0L4tabsS4103;
        int32_t _M0L6_2atmpS4102;
        int32_t _M0L6_2atmpS4101;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS4099, _M0L1iS1309, 0);
        _M0L4tabsS4100 = _M0L1pS1298->$6;
        _M0L4tabsS4103 = _M0L1pS1298->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS4102
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4103, _M0L1iS1309);
        _M0L6_2atmpS4101 = _M0L6_2atmpS4102 - 1;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0MPC15array5Array3setGiE(_M0L4tabsS4100, _M0L1iS1309, _M0L6_2atmpS4101);
        goto join_1310;
      }
      _M0L1vS4104 = _M0L1pS1298->$3;
      _M0L1vS4125 = _M0L1pS1298->$3;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4106 = _M0MPC15array5Array2atGfE(_M0L1vS4125, _M0L1iS1309);
      _M0L6_2atmpS4108 = _M0L2dtS1307 / _M0L2tmS1300;
      _M0L1vS4124 = _M0L1pS1298->$3;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4123 = _M0MPC15array5Array2atGfE(_M0L1vS4124, _M0L1iS1309);
      _M0L6_2atmpS4122 = _M0L6_2atmpS4123 - _M0L2elS1301;
      _M0L6_2atmpS4114 = -_M0L6_2atmpS4122;
      _M0L1wS4121 = _M0L1pS1298->$4;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4120 = _M0MPC15array5Array2atGfE(_M0L1wS4121, _M0L1iS1309);
      _M0L6_2atmpS4117 = -_M0L6_2atmpS4120;
      _M0L1iS4119 = _M0L1pS1298->$7;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4118 = _M0MPC15array5Array2atGfE(_M0L1iS4119, _M0L1iS1309);
      _M0L6_2atmpS4116 = _M0L6_2atmpS4117 + _M0L6_2atmpS4118;
      _M0L6_2atmpS4115 = _M0L1rS1302 * _M0L6_2atmpS4116;
      _M0L6_2atmpS4110 = _M0L6_2atmpS4114 + _M0L6_2atmpS4115;
      _M0L9syn__currS4113 = _M0L1pS1298->$8;
      #line 194 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4112
      = _M0MPC15array5Array2atGfE(_M0L9syn__currS4113, _M0L1iS1309);
      _M0L6_2atmpS4111 = _M0L1rS1302 * _M0L6_2atmpS4112;
      _M0L6_2atmpS4109 = _M0L6_2atmpS4110 - _M0L6_2atmpS4111;
      _M0L6_2atmpS4107 = _M0L6_2atmpS4108 * _M0L6_2atmpS4109;
      _M0L6_2atmpS4105 = _M0L6_2atmpS4106 + _M0L6_2atmpS4107;
      #line 193 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4104, _M0L1iS1309, _M0L6_2atmpS4105);
      _M0L4fireS4126 = _M0L1pS1298->$5;
      _M0L1vS4129 = _M0L1pS1298->$3;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0L6_2atmpS4128 = _M0MPC15array5Array2atGfE(_M0L1vS4129, _M0L1iS1309);
      _M0L6_2atmpS4127 = _M0L6_2atmpS4128 > _M0L2vtS1303;
      #line 195 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS4126, _M0L1iS1309, _M0L6_2atmpS4127);
      _M0L1vS4130 = _M0L1pS1298->$3;
      _M0L4fireS4132 = _M0L1pS1298->$5;
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4132, _M0L1iS1309)) {
        _M0L6_2atmpS4131 = _M0L2vrS1304;
      } else {
        struct _M0TPB5ArrayGfE* _M0L1vS4133 = _M0L1pS1298->$3;
        #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS4131
        = _M0MPC15array5Array2atGfE(_M0L1vS4133, _M0L1iS1309);
      }
      #line 196 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGfE(_M0L1vS4130, _M0L1iS1309, _M0L6_2atmpS4131);
      _M0L4tabsS4134 = _M0L1pS1298->$6;
      _M0L4fireS4136 = _M0L1pS1298->$5;
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS4136, _M0L1iS1309)) {
        _M0L6_2atmpS4135 = _M0L11tabs__stepsS1306;
      } else {
        struct _M0TPB5ArrayGiE* _M0L4tabsS4137 = _M0L1pS1298->$6;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
        _M0L6_2atmpS4135
        = _M0MPC15array5Array2atGiE(_M0L4tabsS4137, _M0L1iS1309);
      }
      #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_if.mbt"
      _M0MPC15array5Array3setGiE(_M0L4tabsS4134, _M0L1iS1309, _M0L6_2atmpS4135);
      goto join_1310;
      goto joinlet_5854;
      join_1310:;
      _M0L6_2atmpS4096 = _M0L1iS1309 + 1;
      _M0L1iS1309 = _M0L6_2atmpS4096;
      continue;
      joinlet_5854:;
    }
    break;
  }
  return 0;
}

int32_t _M0MP26RiantR8snn__mbt15SparseMatrixCSR7forward(
  struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L1mS1285,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1288,
  struct _M0TPB5ArrayGfE* _M0L7post__gS1294
) {
  int32_t _M0L4rowsS1284;
  int32_t _M0L7_2abindS1286;
  int32_t _M0L1iS1287;
  #line 287 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
  _M0L4rowsS1284 = _M0L1mS1285->$0;
  _M0L7_2abindS1286 = 0;
  _M0L1iS1287 = _M0L7_2abindS1286;
  while (1) {
    if (_M0L1iS1287 < _M0L4rowsS1284) {
      int32_t _M0L6_2atmpS4095;
      #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1288, _M0L1iS1287)) {
        struct _M0TPB5ArrayGiE* _M0L6rowptrS4094 = _M0L1mS1285->$2;
        int32_t _M0L5startS1289;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS4092;
        int32_t _M0L6_2atmpS4093;
        int32_t _M0L3endS1290;
        int32_t _M0L1kS1291;
        #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L5startS1289
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS4094, _M0L1iS1287);
        _M0L6rowptrS4092 = _M0L1mS1285->$2;
        _M0L6_2atmpS4093 = _M0L1iS1287 + 1;
        #line 296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
        _M0L3endS1290
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS4092, _M0L6_2atmpS4093);
        _M0L1kS1291 = _M0L5startS1289;
        while (1) {
          if (_M0L1kS1291 < _M0L3endS1290) {
            struct _M0TPB5ArrayGiE* _M0L6colptrS4090 = _M0L1mS1285->$3;
            int32_t _M0L9post__idxS1292;
            struct _M0TPB5ArrayGfE* _M0L4valsS4089;
            float _M0L1wS1293;
            float _M0L6_2atmpS4088;
            float _M0L6_2atmpS4087;
            int32_t _M0L6_2atmpS4091;
            #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L9post__idxS1292
            = _M0MPC15array5Array2atGiE(_M0L6colptrS4090, _M0L1kS1291);
            _M0L4valsS4089 = _M0L1mS1285->$4;
            #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L1wS1293
            = _M0MPC15array5Array2atGfE(_M0L4valsS4089, _M0L1kS1291);
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0L6_2atmpS4088
            = _M0MPC15array5Array2atGfE(_M0L7post__gS1294, _M0L9post__idxS1292);
            _M0L6_2atmpS4087 = _M0L6_2atmpS4088 + _M0L1wS1293;
            #line 300 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\sparse_matrix.mbt"
            _M0MPC15array5Array3setGfE(_M0L7post__gS1294, _M0L9post__idxS1292, _M0L6_2atmpS4087);
            _M0L6_2atmpS4091 = _M0L1kS1291 + 1;
            _M0L1kS1291 = _M0L6_2atmpS4091;
            continue;
          }
          break;
        }
      }
      _M0L6_2atmpS4095 = _M0L1iS1287 + 1;
      _M0L1iS1287 = _M0L6_2atmpS4095;
      continue;
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20ca__plasticity__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1262,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1249,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1251,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1261,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1257,
  struct _M0TP26RiantR8snn__mbt21CaPlasticityVariables* _M0L4varsS1246,
  struct _M0TP26RiantR8snn__mbt21CaPlasticityParameter* _M0L5paramS1253,
  float _M0L6t__nowS1247,
  float _M0L2dtS1274
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3978;
  int32_t _M0L6_2atmpS3977;
  int32_t _if__result_5857;
  int32_t _M0L6n__preS1248;
  int32_t _M0L7n__postS1250;
  float _M0L8tau__preS4086;
  float _M0L13inv__tau__preS1252;
  float _M0L9tau__postS4085;
  float _M0L14inv__tau__postS1254;
  struct _M0TPB8MutLocalGiE* _M0L1jS1255;
  struct _M0TPB8MutLocalGiE* _M0L1kS1265;
  struct _M0TPB8MutLocalGiE* _M0L2jjS1273;
  struct _M0TPB8MutLocalGiE* _M0L2iiS1276;
  struct _M0TPB8MutLocalGiE* _M0L3jj2S1278;
  struct _M0TPB8MutLocalGiE* _M0L3ii2S1280;
  struct _M0TPB8MutLocalGiE* _M0L2s2S1282;
  #line 1495 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6activeS3978 = _M0L4varsS1246->$4;
  #line 1507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3977 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3978);
  if (_M0L6_2atmpS3977 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3976 = _M0L4varsS1246->$4;
    int32_t _M0L6_2atmpS3975;
    #line 1507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS3975 = _M0MPC15array5Array2atGbE(_M0L6activeS3976, 0);
    _if__result_5857 = !_M0L6_2atmpS3975;
  } else {
    _if__result_5857 = 0;
  }
  if (_if__result_5857) {
    return 0;
  }
  #line 1509 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1248 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1249);
  #line 1510 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1250 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1251);
  _M0L8tau__preS4086 = _M0L5paramS1253->$2;
  _M0L13inv__tau__preS1252 = 0x1p+0f / _M0L8tau__preS4086;
  _M0L9tau__postS4085 = _M0L5paramS1253->$3;
  _M0L14inv__tau__postS1254 = 0x1p+0f / _M0L9tau__postS4085;
  _M0L1jS1255
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1255)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1255->$0 = 0;
  while (1) {
    int32_t _M0L3valS3979 = _M0L1jS1255->$0;
    if (_M0L3valS3979 < _M0L6n__preS1248) {
      int32_t _M0L3valS3980 = _M0L1jS1255->$0;
      int32_t _M0L3valS3995;
      int32_t _M0L6_2atmpS3994;
      #line 1516 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1249, _M0L3valS3980)) {
        int32_t _M0L3valS3993 = _M0L1jS1255->$0;
        int32_t _M0L5startS1256;
        int32_t _M0L3valS3992;
        int32_t _M0L6_2atmpS3991;
        int32_t _M0L5end__S1258;
        struct _M0TPB8MutLocalGiE* _M0L1sS1259;
        #line 1517 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS1256
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1257, _M0L3valS3993);
        _M0L3valS3992 = _M0L1jS1255->$0;
        _M0L6_2atmpS3991 = _M0L3valS3992 + 1;
        #line 1518 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5end__S1258
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1257, _M0L6_2atmpS3991);
        _M0L1sS1259
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS1259)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS1259->$0 = _M0L5startS1256;
        while (1) {
          int32_t _M0L3valS3981 = _M0L1sS1259->$0;
          if (_M0L3valS3981 < _M0L5end__S1258) {
            int32_t _M0L3valS3990 = _M0L1sS1259->$0;
            int32_t _M0L1iS1260;
            int32_t _M0L3valS3982;
            int32_t _M0L3valS3987;
            float _M0L6_2atmpS3984;
            struct _M0TPB5ArrayGfE* _M0L5tpostS3986;
            float _M0L6_2atmpS3985;
            float _M0L6_2atmpS3983;
            int32_t _M0L3valS3989;
            int32_t _M0L6_2atmpS3988;
            #line 1521 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L1iS1260
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1261, _M0L3valS3990);
            _M0L3valS3982 = _M0L1sS1259->$0;
            _M0L3valS3987 = _M0L1sS1259->$0;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3984
            = _M0MPC15array5Array2atGfE(_M0L1wS1262, _M0L3valS3987);
            _M0L5tpostS3986 = _M0L4varsS1246->$3;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3985
            = _M0MPC15array5Array2atGfE(_M0L5tpostS3986, _M0L1iS1260);
            _M0L6_2atmpS3983 = _M0L6_2atmpS3984 + _M0L6_2atmpS3985;
            #line 1522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1262, _M0L3valS3982, _M0L6_2atmpS3983);
            _M0L3valS3989 = _M0L1sS1259->$0;
            _M0L6_2atmpS3988 = _M0L3valS3989 + 1;
            _M0L1sS1259->$0 = _M0L6_2atmpS3988;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS1259);
          }
          break;
        }
      }
      _M0L3valS3995 = _M0L1jS1255->$0;
      _M0L6_2atmpS3994 = _M0L3valS3995 + 1;
      _M0L1jS1255->$0 = _M0L6_2atmpS3994;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1255);
    }
    break;
  }
  _M0L1kS1265
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS1265)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS1265->$0 = 0;
  while (1) {
    int32_t _M0L3valS3996 = _M0L1kS1265->$0;
    if (_M0L3valS3996 < _M0L7n__postS1250) {
      int32_t _M0L3valS3997 = _M0L1kS1265->$0;
      int32_t _M0L3valS4018;
      int32_t _M0L6_2atmpS4017;
      #line 1531 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1251, _M0L3valS3997)) {
        struct _M0TPB8MutLocalGiE* _M0L2j2S1266 =
          (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L2j2S1266)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L2j2S1266->$0 = 0;
        while (1) {
          int32_t _M0L3valS3998 = _M0L2j2S1266->$0;
          if (_M0L3valS3998 < _M0L6n__preS1248) {
            int32_t _M0L3valS4016 = _M0L2j2S1266->$0;
            int32_t _M0L5startS1267;
            int32_t _M0L3valS4015;
            int32_t _M0L6_2atmpS4014;
            int32_t _M0L5end__S1268;
            struct _M0TPB8MutLocalGiE* _M0L1sS1269;
            int32_t _M0L3valS4013;
            int32_t _M0L6_2atmpS4012;
            #line 1537 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L5startS1267
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS1257, _M0L3valS4016);
            _M0L3valS4015 = _M0L2j2S1266->$0;
            _M0L6_2atmpS4014 = _M0L3valS4015 + 1;
            #line 1538 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L5end__S1268
            = _M0MPC15array5Array2atGiE(_M0L6rowptrS1257, _M0L6_2atmpS4014);
            _M0L1sS1269
            = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
            Moonbit_object_header(_M0L1sS1269)->meta
            = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
            _M0L1sS1269->$0 = _M0L5startS1267;
            while (1) {
              int32_t _M0L3valS3999 = _M0L1sS1269->$0;
              if (_M0L3valS3999 < _M0L5end__S1268) {
                int32_t _M0L3valS4002 = _M0L1sS1269->$0;
                int32_t _M0L6_2atmpS4000;
                int32_t _M0L3valS4001;
                int32_t _M0L3valS4011;
                int32_t _M0L6_2atmpS4010;
                #line 1541 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                _M0L6_2atmpS4000
                = _M0MPC15array5Array2atGiE(_M0L6colptrS1261, _M0L3valS4002);
                _M0L3valS4001 = _M0L1kS1265->$0;
                if (_M0L6_2atmpS4000 == _M0L3valS4001) {
                  int32_t _M0L3valS4003 = _M0L1sS1269->$0;
                  int32_t _M0L3valS4009 = _M0L1sS1269->$0;
                  float _M0L6_2atmpS4005;
                  struct _M0TPB5ArrayGfE* _M0L4tpreS4007;
                  int32_t _M0L3valS4008;
                  float _M0L6_2atmpS4006;
                  float _M0L6_2atmpS4004;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0L6_2atmpS4005
                  = _M0MPC15array5Array2atGfE(_M0L1wS1262, _M0L3valS4009);
                  _M0L4tpreS4007 = _M0L4varsS1246->$2;
                  _M0L3valS4008 = _M0L2j2S1266->$0;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0L6_2atmpS4006
                  = _M0MPC15array5Array2atGfE(_M0L4tpreS4007, _M0L3valS4008);
                  _M0L6_2atmpS4004 = _M0L6_2atmpS4005 + _M0L6_2atmpS4006;
                  #line 1542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
                  _M0MPC15array5Array3setGfE(_M0L1wS1262, _M0L3valS4003, _M0L6_2atmpS4004);
                }
                _M0L3valS4011 = _M0L1sS1269->$0;
                _M0L6_2atmpS4010 = _M0L3valS4011 + 1;
                _M0L1sS1269->$0 = _M0L6_2atmpS4010;
                continue;
              } else {
                moonbit_decref_cycle_free(_M0L1sS1269);
              }
              break;
            }
            _M0L3valS4013 = _M0L2j2S1266->$0;
            _M0L6_2atmpS4012 = _M0L3valS4013 + 1;
            _M0L2j2S1266->$0 = _M0L6_2atmpS4012;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L2j2S1266);
          }
          break;
        }
      }
      _M0L3valS4018 = _M0L1kS1265->$0;
      _M0L6_2atmpS4017 = _M0L3valS4018 + 1;
      _M0L1kS1265->$0 = _M0L6_2atmpS4017;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1kS1265);
    }
    break;
  }
  _M0L2jjS1273
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2jjS1273)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2jjS1273->$0 = 0;
  while (1) {
    int32_t _M0L3valS4019 = _M0L2jjS1273->$0;
    if (_M0L3valS4019 < _M0L6n__preS1248) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS4020 = _M0L4varsS1246->$2;
      int32_t _M0L3valS4021 = _M0L2jjS1273->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4030 = _M0L4varsS1246->$2;
      int32_t _M0L3valS4031 = _M0L2jjS1273->$0;
      float _M0L6_2atmpS4023;
      struct _M0TPB5ArrayGfE* _M0L4tpreS4028;
      int32_t _M0L3valS4029;
      float _M0L6_2atmpS4027;
      float _M0L6_2atmpS4026;
      float _M0L6_2atmpS4025;
      float _M0L6_2atmpS4024;
      float _M0L6_2atmpS4022;
      int32_t _M0L3valS4033;
      int32_t _M0L6_2atmpS4032;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4023
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4030, _M0L3valS4031);
      _M0L4tpreS4028 = _M0L4varsS1246->$2;
      _M0L3valS4029 = _M0L2jjS1273->$0;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4027
      = _M0MPC15array5Array2atGfE(_M0L4tpreS4028, _M0L3valS4029);
      _M0L6_2atmpS4026 = -_M0L6_2atmpS4027;
      _M0L6_2atmpS4025 = _M0L2dtS1274 * _M0L6_2atmpS4026;
      _M0L6_2atmpS4024 = _M0L6_2atmpS4025 * _M0L13inv__tau__preS1252;
      _M0L6_2atmpS4022 = _M0L6_2atmpS4023 + _M0L6_2atmpS4024;
      #line 1554 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS4020, _M0L3valS4021, _M0L6_2atmpS4022);
      _M0L3valS4033 = _M0L2jjS1273->$0;
      _M0L6_2atmpS4032 = _M0L3valS4033 + 1;
      _M0L2jjS1273->$0 = _M0L6_2atmpS4032;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2jjS1273);
    }
    break;
  }
  _M0L2iiS1276
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2iiS1276)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2iiS1276->$0 = 0;
  while (1) {
    int32_t _M0L3valS4034 = _M0L2iiS1276->$0;
    if (_M0L3valS4034 < _M0L7n__postS1250) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS4035 = _M0L4varsS1246->$3;
      int32_t _M0L3valS4036 = _M0L2iiS1276->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS4045 = _M0L4varsS1246->$3;
      int32_t _M0L3valS4046 = _M0L2iiS1276->$0;
      float _M0L6_2atmpS4038;
      struct _M0TPB5ArrayGfE* _M0L5tpostS4043;
      int32_t _M0L3valS4044;
      float _M0L6_2atmpS4042;
      float _M0L6_2atmpS4041;
      float _M0L6_2atmpS4040;
      float _M0L6_2atmpS4039;
      float _M0L6_2atmpS4037;
      int32_t _M0L3valS4048;
      int32_t _M0L6_2atmpS4047;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4038
      = _M0MPC15array5Array2atGfE(_M0L5tpostS4045, _M0L3valS4046);
      _M0L5tpostS4043 = _M0L4varsS1246->$3;
      _M0L3valS4044 = _M0L2iiS1276->$0;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4042
      = _M0MPC15array5Array2atGfE(_M0L5tpostS4043, _M0L3valS4044);
      _M0L6_2atmpS4041 = -_M0L6_2atmpS4042;
      _M0L6_2atmpS4040 = _M0L2dtS1274 * _M0L6_2atmpS4041;
      _M0L6_2atmpS4039 = _M0L6_2atmpS4040 * _M0L14inv__tau__postS1254;
      _M0L6_2atmpS4037 = _M0L6_2atmpS4038 + _M0L6_2atmpS4039;
      #line 1559 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS4035, _M0L3valS4036, _M0L6_2atmpS4037);
      _M0L3valS4048 = _M0L2iiS1276->$0;
      _M0L6_2atmpS4047 = _M0L3valS4048 + 1;
      _M0L2iiS1276->$0 = _M0L6_2atmpS4047;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2iiS1276);
    }
    break;
  }
  _M0L3jj2S1278
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3jj2S1278)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3jj2S1278->$0 = 0;
  while (1) {
    int32_t _M0L3valS4049 = _M0L3jj2S1278->$0;
    if (_M0L3valS4049 < _M0L6n__preS1248) {
      int32_t _M0L3valS4050 = _M0L3jj2S1278->$0;
      int32_t _M0L3valS4059;
      int32_t _M0L6_2atmpS4058;
      #line 1565 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1249, _M0L3valS4050)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS4051 = _M0L4varsS1246->$2;
        int32_t _M0L3valS4052 = _M0L3jj2S1278->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS4056 = _M0L4varsS1246->$2;
        int32_t _M0L3valS4057 = _M0L3jj2S1278->$0;
        float _M0L6_2atmpS4054;
        float _M0L6a__preS4055;
        float _M0L6_2atmpS4053;
        #line 1566 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS4054
        = _M0MPC15array5Array2atGfE(_M0L4tpreS4056, _M0L3valS4057);
        _M0L6a__preS4055 = _M0L5paramS1253->$0;
        _M0L6_2atmpS4053 = _M0L6_2atmpS4054 + _M0L6a__preS4055;
        #line 1566 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS4051, _M0L3valS4052, _M0L6_2atmpS4053);
      }
      _M0L3valS4059 = _M0L3jj2S1278->$0;
      _M0L6_2atmpS4058 = _M0L3valS4059 + 1;
      _M0L3jj2S1278->$0 = _M0L6_2atmpS4058;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L3jj2S1278);
    }
    break;
  }
  _M0L3ii2S1280
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L3ii2S1280)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L3ii2S1280->$0 = 0;
  while (1) {
    int32_t _M0L3valS4060 = _M0L3ii2S1280->$0;
    if (_M0L3valS4060 < _M0L7n__postS1250) {
      int32_t _M0L3valS4061 = _M0L3ii2S1280->$0;
      int32_t _M0L3valS4070;
      int32_t _M0L6_2atmpS4069;
      #line 1572 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1251, _M0L3valS4061)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS4062 = _M0L4varsS1246->$3;
        int32_t _M0L3valS4063 = _M0L3ii2S1280->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS4067 = _M0L4varsS1246->$3;
        int32_t _M0L3valS4068 = _M0L3ii2S1280->$0;
        float _M0L6_2atmpS4065;
        float _M0L7a__postS4066;
        float _M0L6_2atmpS4064;
        #line 1573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS4065
        = _M0MPC15array5Array2atGfE(_M0L5tpostS4067, _M0L3valS4068);
        _M0L7a__postS4066 = _M0L5paramS1253->$1;
        _M0L6_2atmpS4064 = _M0L6_2atmpS4065 + _M0L7a__postS4066;
        #line 1573 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS4062, _M0L3valS4063, _M0L6_2atmpS4064);
      }
      _M0L3valS4070 = _M0L3ii2S1280->$0;
      _M0L6_2atmpS4069 = _M0L3valS4070 + 1;
      _M0L3ii2S1280->$0 = _M0L6_2atmpS4069;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L3ii2S1280);
    }
    break;
  }
  _M0L2s2S1282
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S1282)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S1282->$0 = 0;
  while (1) {
    int32_t _M0L3valS4071 = _M0L2s2S1282->$0;
    int32_t _M0L6_2atmpS4072;
    #line 1579 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS4072 = _M0MPC15array5Array6lengthGfE(_M0L1wS1262);
    if (_M0L3valS4071 < _M0L6_2atmpS4072) {
      int32_t _M0L3valS4075 = _M0L2s2S1282->$0;
      float _M0L6_2atmpS4073;
      float _M0L6w__minS4074;
      int32_t _M0L3valS4080;
      float _M0L6_2atmpS4078;
      float _M0L6w__maxS4079;
      int32_t _M0L3valS4084;
      int32_t _M0L6_2atmpS4083;
      #line 1580 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4073
      = _M0MPC15array5Array2atGfE(_M0L1wS1262, _M0L3valS4075);
      _M0L6w__minS4074 = _M0L5paramS1253->$5;
      if (_M0L6_2atmpS4073 < _M0L6w__minS4074) {
        int32_t _M0L3valS4076 = _M0L2s2S1282->$0;
        float _M0L6w__minS4077 = _M0L5paramS1253->$5;
        #line 1580 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1262, _M0L3valS4076, _M0L6w__minS4077);
      }
      _M0L3valS4080 = _M0L2s2S1282->$0;
      #line 1581 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS4078
      = _M0MPC15array5Array2atGfE(_M0L1wS1262, _M0L3valS4080);
      _M0L6w__maxS4079 = _M0L5paramS1253->$4;
      if (_M0L6_2atmpS4078 > _M0L6w__maxS4079) {
        int32_t _M0L3valS4081 = _M0L2s2S1282->$0;
        float _M0L6w__maxS4082 = _M0L5paramS1253->$4;
        #line 1581 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1262, _M0L3valS4081, _M0L6w__maxS4082);
      }
      _M0L3valS4084 = _M0L2s2S1282->$0;
      _M0L6_2atmpS4083 = _M0L3valS4084 + 1;
      _M0L2s2S1282->$0 = _M0L6_2atmpS4083;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S1282);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt21stdp__symmetric__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1242,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1215,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1217,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1237,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1230,
  struct _M0TP26RiantR8snn__mbt22STDPSymmetricVariables* _M0L4varsS1222,
  struct _M0TP26RiantR8snn__mbt13STDPSymmetric* _M0L5paramS1219,
  float _M0L6t__nowS1213,
  float _M0L2dtS1223
) {
  int32_t _M0L6n__preS1214;
  int32_t _M0L7n__postS1216;
  float _M0L6tau__xS3974;
  float _M0L11inv__tau__xS1218;
  float _M0L6tau__yS3973;
  float _M0L11inv__tau__yS1220;
  struct _M0TPB8MutLocalGiE* _M0L1jS1221;
  struct _M0TPB8MutLocalGiE* _M0L1iS1225;
  float _M0L4a__xS3970;
  float _M0L6tau__xS3972;
  float _M0L6_2atmpS3971;
  float _M0L7coef__xS1227;
  float _M0L4a__yS3967;
  float _M0L6tau__yS3969;
  float _M0L6_2atmpS3968;
  float _M0L7coef__yS1228;
  #line 1296 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 1308 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1214 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1215);
  #line 1309 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1216 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1217);
  _M0L6tau__xS3974 = _M0L5paramS1219->$2;
  _M0L11inv__tau__xS1218 = 0x1p+0f / _M0L6tau__xS3974;
  _M0L6tau__yS3973 = _M0L5paramS1219->$3;
  _M0L11inv__tau__yS1220 = 0x1p+0f / _M0L6tau__yS3973;
  _M0L1jS1221
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1221)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1221->$0 = 0;
  while (1) {
    int32_t _M0L3valS3844 = _M0L1jS1221->$0;
    if (_M0L3valS3844 < _M0L6n__preS1214) {
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3845 = _M0L4varsS1222->$0;
      int32_t _M0L3valS3846 = _M0L1jS1221->$0;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3855 = _M0L4varsS1222->$0;
      int32_t _M0L3valS3856 = _M0L1jS1221->$0;
      float _M0L6_2atmpS3848;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3853;
      int32_t _M0L3valS3854;
      float _M0L6_2atmpS3852;
      float _M0L6_2atmpS3851;
      float _M0L6_2atmpS3850;
      float _M0L6_2atmpS3849;
      float _M0L6_2atmpS3847;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3857;
      int32_t _M0L3valS3858;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3867;
      int32_t _M0L3valS3868;
      float _M0L6_2atmpS3860;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3865;
      int32_t _M0L3valS3866;
      float _M0L6_2atmpS3864;
      float _M0L6_2atmpS3863;
      float _M0L6_2atmpS3862;
      float _M0L6_2atmpS3861;
      float _M0L6_2atmpS3859;
      int32_t _M0L3valS3869;
      int32_t _M0L3valS3883;
      int32_t _M0L6_2atmpS3882;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3848
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3855, _M0L3valS3856);
      _M0L5tr__xS3853 = _M0L4varsS1222->$0;
      _M0L3valS3854 = _M0L1jS1221->$0;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3852
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3853, _M0L3valS3854);
      _M0L6_2atmpS3851 = -_M0L6_2atmpS3852;
      _M0L6_2atmpS3850 = _M0L2dtS1223 * _M0L6_2atmpS3851;
      _M0L6_2atmpS3849 = _M0L6_2atmpS3850 * _M0L11inv__tau__xS1218;
      _M0L6_2atmpS3847 = _M0L6_2atmpS3848 + _M0L6_2atmpS3849;
      #line 1316 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__xS3845, _M0L3valS3846, _M0L6_2atmpS3847);
      _M0L5tr__yS3857 = _M0L4varsS1222->$1;
      _M0L3valS3858 = _M0L1jS1221->$0;
      _M0L5tr__yS3867 = _M0L4varsS1222->$1;
      _M0L3valS3868 = _M0L1jS1221->$0;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3860
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS3867, _M0L3valS3868);
      _M0L5tr__yS3865 = _M0L4varsS1222->$1;
      _M0L3valS3866 = _M0L1jS1221->$0;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3864
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS3865, _M0L3valS3866);
      _M0L6_2atmpS3863 = -_M0L6_2atmpS3864;
      _M0L6_2atmpS3862 = _M0L2dtS1223 * _M0L6_2atmpS3863;
      _M0L6_2atmpS3861 = _M0L6_2atmpS3862 * _M0L11inv__tau__yS1220;
      _M0L6_2atmpS3859 = _M0L6_2atmpS3860 + _M0L6_2atmpS3861;
      #line 1317 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__yS3857, _M0L3valS3858, _M0L6_2atmpS3859);
      _M0L3valS3869 = _M0L1jS1221->$0;
      #line 1318 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1215, _M0L3valS3869)) {
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3870 = _M0L4varsS1222->$0;
        int32_t _M0L3valS3871 = _M0L1jS1221->$0;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3874 = _M0L4varsS1222->$0;
        int32_t _M0L3valS3875 = _M0L1jS1221->$0;
        float _M0L6_2atmpS3873;
        float _M0L6_2atmpS3872;
        struct _M0TPB5ArrayGfE* _M0L5tr__yS3876;
        int32_t _M0L3valS3877;
        struct _M0TPB5ArrayGfE* _M0L5tr__yS3880;
        int32_t _M0L3valS3881;
        float _M0L6_2atmpS3879;
        float _M0L6_2atmpS3878;
        #line 1319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3873
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3874, _M0L3valS3875);
        _M0L6_2atmpS3872 = _M0L6_2atmpS3873 + 0x1p+0f;
        #line 1319 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__xS3870, _M0L3valS3871, _M0L6_2atmpS3872);
        _M0L5tr__yS3876 = _M0L4varsS1222->$1;
        _M0L3valS3877 = _M0L1jS1221->$0;
        _M0L5tr__yS3880 = _M0L4varsS1222->$1;
        _M0L3valS3881 = _M0L1jS1221->$0;
        #line 1320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3879
        = _M0MPC15array5Array2atGfE(_M0L5tr__yS3880, _M0L3valS3881);
        _M0L6_2atmpS3878 = _M0L6_2atmpS3879 + 0x1p+0f;
        #line 1320 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__yS3876, _M0L3valS3877, _M0L6_2atmpS3878);
      }
      _M0L3valS3883 = _M0L1jS1221->$0;
      _M0L6_2atmpS3882 = _M0L3valS3883 + 1;
      _M0L1jS1221->$0 = _M0L6_2atmpS3882;
      continue;
    }
    break;
  }
  _M0L1iS1225
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1225)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1225->$0 = 0;
  while (1) {
    int32_t _M0L3valS3884 = _M0L1iS1225->$0;
    if (_M0L3valS3884 < _M0L7n__postS1216) {
      struct _M0TPB5ArrayGfE* _M0L5to__xS3885 = _M0L4varsS1222->$2;
      int32_t _M0L3valS3886 = _M0L1iS1225->$0;
      struct _M0TPB5ArrayGfE* _M0L5to__xS3895 = _M0L4varsS1222->$2;
      int32_t _M0L3valS3896 = _M0L1iS1225->$0;
      float _M0L6_2atmpS3888;
      struct _M0TPB5ArrayGfE* _M0L5to__xS3893;
      int32_t _M0L3valS3894;
      float _M0L6_2atmpS3892;
      float _M0L6_2atmpS3891;
      float _M0L6_2atmpS3890;
      float _M0L6_2atmpS3889;
      float _M0L6_2atmpS3887;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3897;
      int32_t _M0L3valS3898;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3907;
      int32_t _M0L3valS3908;
      float _M0L6_2atmpS3900;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3905;
      int32_t _M0L3valS3906;
      float _M0L6_2atmpS3904;
      float _M0L6_2atmpS3903;
      float _M0L6_2atmpS3902;
      float _M0L6_2atmpS3901;
      float _M0L6_2atmpS3899;
      int32_t _M0L3valS3909;
      int32_t _M0L3valS3923;
      int32_t _M0L6_2atmpS3922;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3888
      = _M0MPC15array5Array2atGfE(_M0L5to__xS3895, _M0L3valS3896);
      _M0L5to__xS3893 = _M0L4varsS1222->$2;
      _M0L3valS3894 = _M0L1iS1225->$0;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3892
      = _M0MPC15array5Array2atGfE(_M0L5to__xS3893, _M0L3valS3894);
      _M0L6_2atmpS3891 = -_M0L6_2atmpS3892;
      _M0L6_2atmpS3890 = _M0L2dtS1223 * _M0L6_2atmpS3891;
      _M0L6_2atmpS3889 = _M0L6_2atmpS3890 * _M0L11inv__tau__xS1218;
      _M0L6_2atmpS3887 = _M0L6_2atmpS3888 + _M0L6_2atmpS3889;
      #line 1326 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__xS3885, _M0L3valS3886, _M0L6_2atmpS3887);
      _M0L5to__yS3897 = _M0L4varsS1222->$3;
      _M0L3valS3898 = _M0L1iS1225->$0;
      _M0L5to__yS3907 = _M0L4varsS1222->$3;
      _M0L3valS3908 = _M0L1iS1225->$0;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3900
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3907, _M0L3valS3908);
      _M0L5to__yS3905 = _M0L4varsS1222->$3;
      _M0L3valS3906 = _M0L1iS1225->$0;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3904
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3905, _M0L3valS3906);
      _M0L6_2atmpS3903 = -_M0L6_2atmpS3904;
      _M0L6_2atmpS3902 = _M0L2dtS1223 * _M0L6_2atmpS3903;
      _M0L6_2atmpS3901 = _M0L6_2atmpS3902 * _M0L11inv__tau__yS1220;
      _M0L6_2atmpS3899 = _M0L6_2atmpS3900 + _M0L6_2atmpS3901;
      #line 1327 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__yS3897, _M0L3valS3898, _M0L6_2atmpS3899);
      _M0L3valS3909 = _M0L1iS1225->$0;
      #line 1328 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1217, _M0L3valS3909)) {
        struct _M0TPB5ArrayGfE* _M0L5to__xS3910 = _M0L4varsS1222->$2;
        int32_t _M0L3valS3911 = _M0L1iS1225->$0;
        struct _M0TPB5ArrayGfE* _M0L5to__xS3914 = _M0L4varsS1222->$2;
        int32_t _M0L3valS3915 = _M0L1iS1225->$0;
        float _M0L6_2atmpS3913;
        float _M0L6_2atmpS3912;
        struct _M0TPB5ArrayGfE* _M0L5to__yS3916;
        int32_t _M0L3valS3917;
        struct _M0TPB5ArrayGfE* _M0L5to__yS3920;
        int32_t _M0L3valS3921;
        float _M0L6_2atmpS3919;
        float _M0L6_2atmpS3918;
        #line 1329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3913
        = _M0MPC15array5Array2atGfE(_M0L5to__xS3914, _M0L3valS3915);
        _M0L6_2atmpS3912 = _M0L6_2atmpS3913 + 0x1p+0f;
        #line 1329 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__xS3910, _M0L3valS3911, _M0L6_2atmpS3912);
        _M0L5to__yS3916 = _M0L4varsS1222->$3;
        _M0L3valS3917 = _M0L1iS1225->$0;
        _M0L5to__yS3920 = _M0L4varsS1222->$3;
        _M0L3valS3921 = _M0L1iS1225->$0;
        #line 1330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3919
        = _M0MPC15array5Array2atGfE(_M0L5to__yS3920, _M0L3valS3921);
        _M0L6_2atmpS3918 = _M0L6_2atmpS3919 + 0x1p+0f;
        #line 1330 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__yS3916, _M0L3valS3917, _M0L6_2atmpS3918);
      }
      _M0L3valS3923 = _M0L1iS1225->$0;
      _M0L6_2atmpS3922 = _M0L3valS3923 + 1;
      _M0L1iS1225->$0 = _M0L6_2atmpS3922;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1225);
    }
    break;
  }
  _M0L4a__xS3970 = _M0L5paramS1219->$0;
  _M0L6tau__xS3972 = _M0L5paramS1219->$2;
  _M0L6_2atmpS3971 = 0x1p+1f * _M0L6tau__xS3972;
  _M0L7coef__xS1227 = _M0L4a__xS3970 / _M0L6_2atmpS3971;
  _M0L4a__yS3967 = _M0L5paramS1219->$1;
  _M0L6tau__yS3969 = _M0L5paramS1219->$3;
  _M0L6_2atmpS3968 = 0x1p+1f * _M0L6tau__yS3969;
  _M0L7coef__yS1228 = _M0L4a__yS3967 / _M0L6_2atmpS3968;
  _M0L1jS1221->$0 = 0;
  while (1) {
    int32_t _M0L3valS3924 = _M0L1jS1221->$0;
    if (_M0L3valS3924 < _M0L6n__preS1214) {
      int32_t _M0L3valS3966 = _M0L1jS1221->$0;
      int32_t _M0L5startS1229;
      int32_t _M0L3valS3965;
      int32_t _M0L6_2atmpS3964;
      int32_t _M0L3endS1231;
      int32_t _M0L3valS3963;
      int32_t _M0L10pre__firedS1232;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3961;
      int32_t _M0L3valS3962;
      float _M0L8tr__x__jS1233;
      struct _M0TPB5ArrayGfE* _M0L5tr__yS3959;
      int32_t _M0L3valS3960;
      float _M0L8tr__y__jS1234;
      struct _M0TPB8MutLocalGiE* _M0L1sS1235;
      int32_t _M0L3valS3958;
      int32_t _M0L6_2atmpS3957;
      #line 1346 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1229
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1230, _M0L3valS3966);
      _M0L3valS3965 = _M0L1jS1221->$0;
      _M0L6_2atmpS3964 = _M0L3valS3965 + 1;
      #line 1347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1231
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1230, _M0L6_2atmpS3964);
      _M0L3valS3963 = _M0L1jS1221->$0;
      #line 1348 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L10pre__firedS1232
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1215, _M0L3valS3963);
      _M0L5tr__xS3961 = _M0L4varsS1222->$0;
      _M0L3valS3962 = _M0L1jS1221->$0;
      #line 1349 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8tr__x__jS1233
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3961, _M0L3valS3962);
      _M0L5tr__yS3959 = _M0L4varsS1222->$1;
      _M0L3valS3960 = _M0L1jS1221->$0;
      #line 1350 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L8tr__y__jS1234
      = _M0MPC15array5Array2atGfE(_M0L5tr__yS3959, _M0L3valS3960);
      _M0L1sS1235
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1235)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1235->$0 = _M0L5startS1229;
      while (1) {
        int32_t _M0L3valS3925 = _M0L1sS1235->$0;
        if (_M0L3valS3925 < _M0L3endS1231) {
          int32_t _M0L3valS3956 = _M0L1sS1235->$0;
          int32_t _M0L9post__idxS1236;
          int32_t _M0L11post__firedS1238;
          struct _M0TPB5ArrayGfE* _M0L5to__xS3955;
          float _M0L8to__x__iS1239;
          struct _M0TPB5ArrayGfE* _M0L5to__yS3954;
          float _M0L8to__y__iS1240;
          int32_t _M0L3valS3944;
          float _M0L6_2atmpS3942;
          float _M0L6w__minS3943;
          int32_t _M0L3valS3949;
          float _M0L6_2atmpS3947;
          float _M0L6w__maxS3948;
          int32_t _M0L3valS3953;
          int32_t _M0L6_2atmpS3952;
          #line 1353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1236
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1237, _M0L3valS3956);
          #line 1354 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1238
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1217, _M0L9post__idxS1236);
          _M0L5to__xS3955 = _M0L4varsS1222->$2;
          #line 1355 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8to__x__iS1239
          = _M0MPC15array5Array2atGfE(_M0L5to__xS3955, _M0L9post__idxS1236);
          _M0L5to__yS3954 = _M0L4varsS1222->$3;
          #line 1356 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8to__y__iS1240
          = _M0MPC15array5Array2atGfE(_M0L5to__yS3954, _M0L9post__idxS1236);
          if (_M0L10pre__firedS1232) {
            float _M0L10alpha__preS3932 = _M0L5paramS1219->$4;
            float _M0L6_2atmpS3933 = _M0L7coef__xS1227 * _M0L8to__x__iS1239;
            float _M0L6_2atmpS3930 = _M0L10alpha__preS3932 + _M0L6_2atmpS3933;
            float _M0L6_2atmpS3931 = _M0L7coef__yS1228 * _M0L8to__y__iS1240;
            float _M0L2dwS1241 = _M0L6_2atmpS3930 - _M0L6_2atmpS3931;
            int32_t _M0L3valS3926 = _M0L1sS1235->$0;
            int32_t _M0L3valS3929 = _M0L1sS1235->$0;
            float _M0L6_2atmpS3928;
            float _M0L6_2atmpS3927;
            #line 1359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3928
            = _M0MPC15array5Array2atGfE(_M0L1wS1242, _M0L3valS3929);
            _M0L6_2atmpS3927 = _M0L6_2atmpS3928 + _M0L2dwS1241;
            #line 1359 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1242, _M0L3valS3926, _M0L6_2atmpS3927);
          }
          if (_M0L11post__firedS1238) {
            float _M0L11alpha__postS3940 = _M0L5paramS1219->$5;
            float _M0L6_2atmpS3941 = _M0L7coef__xS1227 * _M0L8tr__x__jS1233;
            float _M0L6_2atmpS3938 =
              _M0L11alpha__postS3940 + _M0L6_2atmpS3941;
            float _M0L6_2atmpS3939 = _M0L7coef__yS1228 * _M0L8tr__y__jS1234;
            float _M0L2dwS1243 = _M0L6_2atmpS3938 - _M0L6_2atmpS3939;
            int32_t _M0L3valS3934 = _M0L1sS1235->$0;
            int32_t _M0L3valS3937 = _M0L1sS1235->$0;
            float _M0L6_2atmpS3936;
            float _M0L6_2atmpS3935;
            #line 1363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3936
            = _M0MPC15array5Array2atGfE(_M0L1wS1242, _M0L3valS3937);
            _M0L6_2atmpS3935 = _M0L6_2atmpS3936 + _M0L2dwS1243;
            #line 1363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1242, _M0L3valS3934, _M0L6_2atmpS3935);
          }
          _M0L3valS3944 = _M0L1sS1235->$0;
          #line 1365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3942
          = _M0MPC15array5Array2atGfE(_M0L1wS1242, _M0L3valS3944);
          _M0L6w__minS3943 = _M0L5paramS1219->$7;
          if (_M0L6_2atmpS3942 < _M0L6w__minS3943) {
            int32_t _M0L3valS3945 = _M0L1sS1235->$0;
            float _M0L6w__minS3946 = _M0L5paramS1219->$7;
            #line 1365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1242, _M0L3valS3945, _M0L6w__minS3946);
          }
          _M0L3valS3949 = _M0L1sS1235->$0;
          #line 1366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3947
          = _M0MPC15array5Array2atGfE(_M0L1wS1242, _M0L3valS3949);
          _M0L6w__maxS3948 = _M0L5paramS1219->$6;
          if (_M0L6_2atmpS3947 > _M0L6w__maxS3948) {
            int32_t _M0L3valS3950 = _M0L1sS1235->$0;
            float _M0L6w__maxS3951 = _M0L5paramS1219->$6;
            #line 1366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1242, _M0L3valS3950, _M0L6w__maxS3951);
          }
          _M0L3valS3953 = _M0L1sS1235->$0;
          _M0L6_2atmpS3952 = _M0L3valS3953 + 1;
          _M0L1sS1235->$0 = _M0L6_2atmpS3952;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1235);
        }
        break;
      }
      _M0L3valS3958 = _M0L1jS1221->$0;
      _M0L6_2atmpS3957 = _M0L3valS3958 + 1;
      _M0L1jS1221->$0 = _M0L6_2atmpS3957;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1221);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22stdp__confavreux__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1210,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1187,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1189,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1207,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1201,
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS1195,
  struct _M0TP26RiantR8snn__mbt18STDPConfavreux2025* _M0L5paramS1192,
  float _M0L6t__nowS1196,
  float _M0L2dtS1191
) {
  int32_t _M0L6n__preS1186;
  int32_t _M0L7n__postS1188;
  float _M0L6_2atmpS3842;
  float _M0L8tau__preS3843;
  float _M0L6_2atmpS3841;
  float _M0L10decay__preS1190;
  float _M0L6_2atmpS3839;
  float _M0L9tau__postS3840;
  float _M0L6_2atmpS3838;
  float _M0L11decay__postS1193;
  struct _M0TPB8MutLocalGiE* _M0L1jS1194;
  struct _M0TPB8MutLocalGiE* _M0L1iS1198;
  #line 1080 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 1091 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1186 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1187);
  #line 1092 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1188 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1189);
  _M0L6_2atmpS3842 = -_M0L2dtS1191;
  _M0L8tau__preS3843 = _M0L5paramS1192->$5;
  _M0L6_2atmpS3841 = _M0L6_2atmpS3842 / _M0L8tau__preS3843;
  #line 1093 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10decay__preS1190 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3841);
  _M0L6_2atmpS3839 = -_M0L2dtS1191;
  _M0L9tau__postS3840 = _M0L5paramS1192->$6;
  _M0L6_2atmpS3838 = _M0L6_2atmpS3839 / _M0L9tau__postS3840;
  #line 1094 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L11decay__postS1193 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3838);
  _M0L1jS1194
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1194)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1194->$0 = 0;
  while (1) {
    int32_t _M0L3valS3758 = _M0L1jS1194->$0;
    if (_M0L3valS3758 < _M0L6n__preS1186) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS3759 = _M0L4varsS1195->$0;
      int32_t _M0L3valS3760 = _M0L1jS1194->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3763 = _M0L4varsS1195->$0;
      int32_t _M0L3valS3764 = _M0L1jS1194->$0;
      float _M0L6_2atmpS3762;
      float _M0L6_2atmpS3761;
      int32_t _M0L3valS3765;
      int32_t _M0L3valS3775;
      int32_t _M0L6_2atmpS3774;
      #line 1098 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3762
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3763, _M0L3valS3764);
      _M0L6_2atmpS3761 = _M0L6_2atmpS3762 * _M0L10decay__preS1190;
      #line 1098 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS3759, _M0L3valS3760, _M0L6_2atmpS3761);
      _M0L3valS3765 = _M0L1jS1194->$0;
      #line 1099 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1187, _M0L3valS3765)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS3766 = _M0L4varsS1195->$0;
        int32_t _M0L3valS3767 = _M0L1jS1194->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS3770 = _M0L4varsS1195->$0;
        int32_t _M0L3valS3771 = _M0L1jS1194->$0;
        float _M0L6_2atmpS3769;
        float _M0L6_2atmpS3768;
        struct _M0TPB5ArrayGfE* _M0L9last__preS3772;
        int32_t _M0L3valS3773;
        #line 1100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3769
        = _M0MPC15array5Array2atGfE(_M0L4tpreS3770, _M0L3valS3771);
        _M0L6_2atmpS3768 = _M0L6_2atmpS3769 + 0x1p+0f;
        #line 1100 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS3766, _M0L3valS3767, _M0L6_2atmpS3768);
        _M0L9last__preS3772 = _M0L4varsS1195->$2;
        _M0L3valS3773 = _M0L1jS1194->$0;
        #line 1101 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L9last__preS3772, _M0L3valS3773, _M0L6t__nowS1196);
      }
      _M0L3valS3775 = _M0L1jS1194->$0;
      _M0L6_2atmpS3774 = _M0L3valS3775 + 1;
      _M0L1jS1194->$0 = _M0L6_2atmpS3774;
      continue;
    }
    break;
  }
  _M0L1iS1198
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1198)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1198->$0 = 0;
  while (1) {
    int32_t _M0L3valS3776 = _M0L1iS1198->$0;
    if (_M0L3valS3776 < _M0L7n__postS1188) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS3777 = _M0L4varsS1195->$1;
      int32_t _M0L3valS3778 = _M0L1iS1198->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3781 = _M0L4varsS1195->$1;
      int32_t _M0L3valS3782 = _M0L1iS1198->$0;
      float _M0L6_2atmpS3780;
      float _M0L6_2atmpS3779;
      int32_t _M0L3valS3783;
      int32_t _M0L3valS3793;
      int32_t _M0L6_2atmpS3792;
      #line 1107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3780
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3781, _M0L3valS3782);
      _M0L6_2atmpS3779 = _M0L6_2atmpS3780 * _M0L11decay__postS1193;
      #line 1107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS3777, _M0L3valS3778, _M0L6_2atmpS3779);
      _M0L3valS3783 = _M0L1iS1198->$0;
      #line 1108 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1189, _M0L3valS3783)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS3784 = _M0L4varsS1195->$1;
        int32_t _M0L3valS3785 = _M0L1iS1198->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS3788 = _M0L4varsS1195->$1;
        int32_t _M0L3valS3789 = _M0L1iS1198->$0;
        float _M0L6_2atmpS3787;
        float _M0L6_2atmpS3786;
        struct _M0TPB5ArrayGfE* _M0L10last__postS3790;
        int32_t _M0L3valS3791;
        #line 1109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3787
        = _M0MPC15array5Array2atGfE(_M0L5tpostS3788, _M0L3valS3789);
        _M0L6_2atmpS3786 = _M0L6_2atmpS3787 + 0x1p+0f;
        #line 1109 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS3784, _M0L3valS3785, _M0L6_2atmpS3786);
        _M0L10last__postS3790 = _M0L4varsS1195->$3;
        _M0L3valS3791 = _M0L1iS1198->$0;
        #line 1110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L10last__postS3790, _M0L3valS3791, _M0L6t__nowS1196);
      }
      _M0L3valS3793 = _M0L1iS1198->$0;
      _M0L6_2atmpS3792 = _M0L3valS3793 + 1;
      _M0L1iS1198->$0 = _M0L6_2atmpS3792;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1198);
    }
    break;
  }
  _M0L1jS1194->$0 = 0;
  while (1) {
    int32_t _M0L3valS3794 = _M0L1jS1194->$0;
    if (_M0L3valS3794 < _M0L6n__preS1186) {
      int32_t _M0L3valS3837 = _M0L1jS1194->$0;
      int32_t _M0L5startS1200;
      int32_t _M0L3valS3836;
      int32_t _M0L6_2atmpS3835;
      int32_t _M0L3endS1202;
      int32_t _M0L3valS3834;
      int32_t _M0L10pre__firedS1203;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3832;
      int32_t _M0L3valS3833;
      float _M0L7tpre__jS1204;
      struct _M0TPB8MutLocalGiE* _M0L1sS1205;
      int32_t _M0L3valS3831;
      int32_t _M0L6_2atmpS3830;
      #line 1120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1200
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1201, _M0L3valS3837);
      _M0L3valS3836 = _M0L1jS1194->$0;
      _M0L6_2atmpS3835 = _M0L3valS3836 + 1;
      #line 1121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1202
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1201, _M0L6_2atmpS3835);
      _M0L3valS3834 = _M0L1jS1194->$0;
      #line 1122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L10pre__firedS1203
      = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1187, _M0L3valS3834);
      _M0L4tpreS3832 = _M0L4varsS1195->$0;
      _M0L3valS3833 = _M0L1jS1194->$0;
      #line 1123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L7tpre__jS1204
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3832, _M0L3valS3833);
      _M0L1sS1205
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1205)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1205->$0 = _M0L5startS1200;
      while (1) {
        int32_t _M0L3valS3795 = _M0L1sS1205->$0;
        if (_M0L3valS3795 < _M0L3endS1202) {
          int32_t _M0L3valS3829 = _M0L1sS1205->$0;
          int32_t _M0L9post__idxS1206;
          int32_t _M0L11post__firedS1208;
          struct _M0TPB5ArrayGfE* _M0L5tpostS3828;
          float _M0L8tpost__iS1209;
          int32_t _M0L3valS3818;
          float _M0L6_2atmpS3816;
          float _M0L6w__minS3817;
          int32_t _M0L3valS3823;
          float _M0L6_2atmpS3821;
          float _M0L6w__maxS3822;
          int32_t _M0L3valS3827;
          int32_t _M0L6_2atmpS3826;
          #line 1126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1206
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1207, _M0L3valS3829);
          #line 1127 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1208
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1189, _M0L9post__idxS1206);
          _M0L5tpostS3828 = _M0L4varsS1195->$1;
          #line 1128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L8tpost__iS1209
          = _M0MPC15array5Array2atGfE(_M0L5tpostS3828, _M0L9post__idxS1206);
          if (_M0L10pre__firedS1203) {
            int32_t _M0L3valS3796 = _M0L1sS1205->$0;
            int32_t _M0L3valS3805 = _M0L1sS1205->$0;
            float _M0L6_2atmpS3798;
            float _M0L3etaS3800;
            float _M0L5kappaS3804;
            float _M0L6_2atmpS3802;
            float _M0L5alphaS3803;
            float _M0L6_2atmpS3801;
            float _M0L6_2atmpS3799;
            float _M0L6_2atmpS3797;
            #line 1131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3798
            = _M0MPC15array5Array2atGfE(_M0L1wS1210, _M0L3valS3805);
            _M0L3etaS3800 = _M0L5paramS1192->$0;
            _M0L5kappaS3804 = _M0L5paramS1192->$3;
            _M0L6_2atmpS3802 = _M0L5kappaS3804 * _M0L8tpost__iS1209;
            _M0L5alphaS3803 = _M0L5paramS1192->$1;
            _M0L6_2atmpS3801 = _M0L6_2atmpS3802 + _M0L5alphaS3803;
            _M0L6_2atmpS3799 = _M0L3etaS3800 * _M0L6_2atmpS3801;
            _M0L6_2atmpS3797 = _M0L6_2atmpS3798 + _M0L6_2atmpS3799;
            #line 1131 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1210, _M0L3valS3796, _M0L6_2atmpS3797);
          }
          if (_M0L11post__firedS1208) {
            int32_t _M0L3valS3806 = _M0L1sS1205->$0;
            int32_t _M0L3valS3815 = _M0L1sS1205->$0;
            float _M0L6_2atmpS3808;
            float _M0L3etaS3810;
            float _M0L5gammaS3814;
            float _M0L6_2atmpS3812;
            float _M0L4betaS3813;
            float _M0L6_2atmpS3811;
            float _M0L6_2atmpS3809;
            float _M0L6_2atmpS3807;
            #line 1135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3808
            = _M0MPC15array5Array2atGfE(_M0L1wS1210, _M0L3valS3815);
            _M0L3etaS3810 = _M0L5paramS1192->$0;
            _M0L5gammaS3814 = _M0L5paramS1192->$4;
            _M0L6_2atmpS3812 = _M0L5gammaS3814 * _M0L7tpre__jS1204;
            _M0L4betaS3813 = _M0L5paramS1192->$2;
            _M0L6_2atmpS3811 = _M0L6_2atmpS3812 + _M0L4betaS3813;
            _M0L6_2atmpS3809 = _M0L3etaS3810 * _M0L6_2atmpS3811;
            _M0L6_2atmpS3807 = _M0L6_2atmpS3808 + _M0L6_2atmpS3809;
            #line 1135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1210, _M0L3valS3806, _M0L6_2atmpS3807);
          }
          _M0L3valS3818 = _M0L1sS1205->$0;
          #line 1138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3816
          = _M0MPC15array5Array2atGfE(_M0L1wS1210, _M0L3valS3818);
          _M0L6w__minS3817 = _M0L5paramS1192->$8;
          if (_M0L6_2atmpS3816 < _M0L6w__minS3817) {
            int32_t _M0L3valS3819 = _M0L1sS1205->$0;
            float _M0L6w__minS3820 = _M0L5paramS1192->$8;
            #line 1138 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1210, _M0L3valS3819, _M0L6w__minS3820);
          }
          _M0L3valS3823 = _M0L1sS1205->$0;
          #line 1139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3821
          = _M0MPC15array5Array2atGfE(_M0L1wS1210, _M0L3valS3823);
          _M0L6w__maxS3822 = _M0L5paramS1192->$7;
          if (_M0L6_2atmpS3821 > _M0L6w__maxS3822) {
            int32_t _M0L3valS3824 = _M0L1sS1205->$0;
            float _M0L6w__maxS3825 = _M0L5paramS1192->$7;
            #line 1139 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1210, _M0L3valS3824, _M0L6w__maxS3825);
          }
          _M0L3valS3827 = _M0L1sS1205->$0;
          _M0L6_2atmpS3826 = _M0L3valS3827 + 1;
          _M0L1sS1205->$0 = _M0L6_2atmpS3826;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1205);
        }
        break;
      }
      _M0L3valS3831 = _M0L1jS1194->$0;
      _M0L6_2atmpS3830 = _M0L3valS3831 + 1;
      _M0L1jS1194->$0 = _M0L6_2atmpS3830;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1194);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt10stdp__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1183,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1163,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1165,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1180,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1176,
  struct _M0TP26RiantR8snn__mbt13STDPVariables* _M0L4varsS1161,
  struct _M0TP26RiantR8snn__mbt12STDPGerstner* _M0L5paramS1168,
  float _M0L6t__nowS1171,
  float _M0L2dtS1167
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3675;
  int32_t _M0L6_2atmpS3674;
  int32_t _if__result_5876;
  int32_t _M0L6n__preS1162;
  int32_t _M0L7n__postS1164;
  float _M0L6_2atmpS3756;
  float _M0L8tau__preS3757;
  float _M0L6_2atmpS3755;
  float _M0L10decay__preS1166;
  float _M0L6_2atmpS3753;
  float _M0L9tau__postS3754;
  float _M0L6_2atmpS3752;
  float _M0L11decay__postS1169;
  struct _M0TPB8MutLocalGiE* _M0L1jS1170;
  struct _M0TPB8MutLocalGiE* _M0L1iS1173;
  #line 905 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6activeS3675 = _M0L4varsS1161->$4;
  #line 917 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3674 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3675);
  if (_M0L6_2atmpS3674 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3673 = _M0L4varsS1161->$4;
    int32_t _M0L6_2atmpS3672;
    #line 917 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS3672 = _M0MPC15array5Array2atGbE(_M0L6activeS3673, 0);
    _if__result_5876 = !_M0L6_2atmpS3672;
  } else {
    _if__result_5876 = 0;
  }
  if (_if__result_5876) {
    return 0;
  }
  #line 921 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1162 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1163);
  #line 922 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1164 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1165);
  _M0L6_2atmpS3756 = -_M0L2dtS1167;
  _M0L8tau__preS3757 = _M0L5paramS1168->$2;
  _M0L6_2atmpS3755 = _M0L6_2atmpS3756 / _M0L8tau__preS3757;
  #line 923 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L10decay__preS1166 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3755);
  _M0L6_2atmpS3753 = -_M0L2dtS1167;
  _M0L9tau__postS3754 = _M0L5paramS1168->$3;
  _M0L6_2atmpS3752 = _M0L6_2atmpS3753 / _M0L9tau__postS3754;
  #line 924 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L11decay__postS1169 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3752);
  _M0L1jS1170
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1170)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1170->$0 = 0;
  while (1) {
    int32_t _M0L3valS3676 = _M0L1jS1170->$0;
    if (_M0L3valS3676 < _M0L6n__preS1162) {
      struct _M0TPB5ArrayGfE* _M0L4tpreS3677 = _M0L4varsS1161->$0;
      int32_t _M0L3valS3678 = _M0L1jS1170->$0;
      struct _M0TPB5ArrayGfE* _M0L4tpreS3681 = _M0L4varsS1161->$0;
      int32_t _M0L3valS3682 = _M0L1jS1170->$0;
      float _M0L6_2atmpS3680;
      float _M0L6_2atmpS3679;
      int32_t _M0L3valS3683;
      int32_t _M0L3valS3694;
      int32_t _M0L6_2atmpS3693;
      #line 927 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3680
      = _M0MPC15array5Array2atGfE(_M0L4tpreS3681, _M0L3valS3682);
      _M0L6_2atmpS3679 = _M0L6_2atmpS3680 * _M0L10decay__preS1166;
      #line 927 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS3677, _M0L3valS3678, _M0L6_2atmpS3679);
      _M0L3valS3683 = _M0L1jS1170->$0;
      #line 928 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1163, _M0L3valS3683)) {
        struct _M0TPB5ArrayGfE* _M0L4tpreS3684 = _M0L4varsS1161->$0;
        int32_t _M0L3valS3685 = _M0L1jS1170->$0;
        struct _M0TPB5ArrayGfE* _M0L4tpreS3689 = _M0L4varsS1161->$0;
        int32_t _M0L3valS3690 = _M0L1jS1170->$0;
        float _M0L6_2atmpS3687;
        float _M0L6a__preS3688;
        float _M0L6_2atmpS3686;
        struct _M0TPB5ArrayGfE* _M0L9last__preS3691;
        int32_t _M0L3valS3692;
        #line 929 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3687
        = _M0MPC15array5Array2atGfE(_M0L4tpreS3689, _M0L3valS3690);
        _M0L6a__preS3688 = _M0L5paramS1168->$0;
        _M0L6_2atmpS3686 = _M0L6_2atmpS3687 + _M0L6a__preS3688;
        #line 929 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS3684, _M0L3valS3685, _M0L6_2atmpS3686);
        _M0L9last__preS3691 = _M0L4varsS1161->$2;
        _M0L3valS3692 = _M0L1jS1170->$0;
        #line 930 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L9last__preS3691, _M0L3valS3692, _M0L6t__nowS1171);
      }
      _M0L3valS3694 = _M0L1jS1170->$0;
      _M0L6_2atmpS3693 = _M0L3valS3694 + 1;
      _M0L1jS1170->$0 = _M0L6_2atmpS3693;
      continue;
    }
    break;
  }
  _M0L1iS1173
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1173)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1173->$0 = 0;
  while (1) {
    int32_t _M0L3valS3695 = _M0L1iS1173->$0;
    if (_M0L3valS3695 < _M0L7n__postS1164) {
      struct _M0TPB5ArrayGfE* _M0L5tpostS3696 = _M0L4varsS1161->$1;
      int32_t _M0L3valS3697 = _M0L1iS1173->$0;
      struct _M0TPB5ArrayGfE* _M0L5tpostS3700 = _M0L4varsS1161->$1;
      int32_t _M0L3valS3701 = _M0L1iS1173->$0;
      float _M0L6_2atmpS3699;
      float _M0L6_2atmpS3698;
      int32_t _M0L3valS3702;
      int32_t _M0L3valS3713;
      int32_t _M0L6_2atmpS3712;
      #line 936 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3699
      = _M0MPC15array5Array2atGfE(_M0L5tpostS3700, _M0L3valS3701);
      _M0L6_2atmpS3698 = _M0L6_2atmpS3699 * _M0L11decay__postS1169;
      #line 936 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS3696, _M0L3valS3697, _M0L6_2atmpS3698);
      _M0L3valS3702 = _M0L1iS1173->$0;
      #line 937 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1165, _M0L3valS3702)) {
        struct _M0TPB5ArrayGfE* _M0L5tpostS3703 = _M0L4varsS1161->$1;
        int32_t _M0L3valS3704 = _M0L1iS1173->$0;
        struct _M0TPB5ArrayGfE* _M0L5tpostS3708 = _M0L4varsS1161->$1;
        int32_t _M0L3valS3709 = _M0L1iS1173->$0;
        float _M0L6_2atmpS3706;
        float _M0L7a__postS3707;
        float _M0L6_2atmpS3705;
        struct _M0TPB5ArrayGfE* _M0L10last__postS3710;
        int32_t _M0L3valS3711;
        #line 938 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3706
        = _M0MPC15array5Array2atGfE(_M0L5tpostS3708, _M0L3valS3709);
        _M0L7a__postS3707 = _M0L5paramS1168->$1;
        _M0L6_2atmpS3705 = _M0L6_2atmpS3706 + _M0L7a__postS3707;
        #line 938 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS3703, _M0L3valS3704, _M0L6_2atmpS3705);
        _M0L10last__postS3710 = _M0L4varsS1161->$3;
        _M0L3valS3711 = _M0L1iS1173->$0;
        #line 939 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L10last__postS3710, _M0L3valS3711, _M0L6t__nowS1171);
      }
      _M0L3valS3713 = _M0L1iS1173->$0;
      _M0L6_2atmpS3712 = _M0L3valS3713 + 1;
      _M0L1iS1173->$0 = _M0L6_2atmpS3712;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1173);
    }
    break;
  }
  _M0L1jS1170->$0 = 0;
  while (1) {
    int32_t _M0L3valS3714 = _M0L1jS1170->$0;
    if (_M0L3valS3714 < _M0L6n__preS1162) {
      int32_t _M0L3valS3751 = _M0L1jS1170->$0;
      int32_t _M0L5startS1175;
      int32_t _M0L3valS3750;
      int32_t _M0L6_2atmpS3749;
      int32_t _M0L3endS1177;
      struct _M0TPB8MutLocalGiE* _M0L1sS1178;
      int32_t _M0L3valS3748;
      int32_t _M0L6_2atmpS3747;
      #line 947 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L5startS1175
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1176, _M0L3valS3751);
      _M0L3valS3750 = _M0L1jS1170->$0;
      _M0L6_2atmpS3749 = _M0L3valS3750 + 1;
      #line 948 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L3endS1177
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1176, _M0L6_2atmpS3749);
      _M0L1sS1178
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS1178)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS1178->$0 = _M0L5startS1175;
      while (1) {
        int32_t _M0L3valS3715 = _M0L1sS1178->$0;
        if (_M0L3valS3715 < _M0L3endS1177) {
          int32_t _M0L3valS3746 = _M0L1sS1178->$0;
          int32_t _M0L9post__idxS1179;
          int32_t _M0L3valS3745;
          int32_t _M0L10pre__firedS1181;
          int32_t _M0L11post__firedS1182;
          int32_t _M0L3valS3735;
          float _M0L6_2atmpS3733;
          float _M0L6w__minS3734;
          int32_t _M0L3valS3740;
          float _M0L6_2atmpS3738;
          float _M0L6w__maxS3739;
          int32_t _M0L3valS3744;
          int32_t _M0L6_2atmpS3743;
          #line 951 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L9post__idxS1179
          = _M0MPC15array5Array2atGiE(_M0L6colptrS1180, _M0L3valS3746);
          _M0L3valS3745 = _M0L1jS1170->$0;
          #line 952 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L10pre__firedS1181
          = _M0MPC15array5Array2atGbE(_M0L9pre__fireS1163, _M0L3valS3745);
          #line 953 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L11post__firedS1182
          = _M0MPC15array5Array2atGbE(_M0L10post__fireS1165, _M0L9post__idxS1179);
          if (_M0L10pre__firedS1181) {
            int32_t _M0L3valS3716 = _M0L1sS1178->$0;
            int32_t _M0L3valS3723 = _M0L1sS1178->$0;
            float _M0L6_2atmpS3718;
            float _M0L7a__postS3720;
            struct _M0TPB5ArrayGfE* _M0L5tpostS3722;
            float _M0L6_2atmpS3721;
            float _M0L6_2atmpS3719;
            float _M0L6_2atmpS3717;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3718
            = _M0MPC15array5Array2atGfE(_M0L1wS1183, _M0L3valS3723);
            _M0L7a__postS3720 = _M0L5paramS1168->$1;
            _M0L5tpostS3722 = _M0L4varsS1161->$1;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3721
            = _M0MPC15array5Array2atGfE(_M0L5tpostS3722, _M0L9post__idxS1179);
            _M0L6_2atmpS3719 = _M0L7a__postS3720 * _M0L6_2atmpS3721;
            _M0L6_2atmpS3717 = _M0L6_2atmpS3718 + _M0L6_2atmpS3719;
            #line 956 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1183, _M0L3valS3716, _M0L6_2atmpS3717);
          }
          if (_M0L11post__firedS1182) {
            int32_t _M0L3valS3724 = _M0L1sS1178->$0;
            int32_t _M0L3valS3732 = _M0L1sS1178->$0;
            float _M0L6_2atmpS3726;
            float _M0L6a__preS3728;
            struct _M0TPB5ArrayGfE* _M0L4tpreS3730;
            int32_t _M0L3valS3731;
            float _M0L6_2atmpS3729;
            float _M0L6_2atmpS3727;
            float _M0L6_2atmpS3725;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3726
            = _M0MPC15array5Array2atGfE(_M0L1wS1183, _M0L3valS3732);
            _M0L6a__preS3728 = _M0L5paramS1168->$0;
            _M0L4tpreS3730 = _M0L4varsS1161->$0;
            _M0L3valS3731 = _M0L1jS1170->$0;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3729
            = _M0MPC15array5Array2atGfE(_M0L4tpreS3730, _M0L3valS3731);
            _M0L6_2atmpS3727 = _M0L6a__preS3728 * _M0L6_2atmpS3729;
            _M0L6_2atmpS3725 = _M0L6_2atmpS3726 + _M0L6_2atmpS3727;
            #line 960 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1183, _M0L3valS3724, _M0L6_2atmpS3725);
          }
          _M0L3valS3735 = _M0L1sS1178->$0;
          #line 963 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3733
          = _M0MPC15array5Array2atGfE(_M0L1wS1183, _M0L3valS3735);
          _M0L6w__minS3734 = _M0L5paramS1168->$5;
          if (_M0L6_2atmpS3733 < _M0L6w__minS3734) {
            int32_t _M0L3valS3736 = _M0L1sS1178->$0;
            float _M0L6w__minS3737 = _M0L5paramS1168->$5;
            #line 963 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1183, _M0L3valS3736, _M0L6w__minS3737);
          }
          _M0L3valS3740 = _M0L1sS1178->$0;
          #line 964 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0L6_2atmpS3738
          = _M0MPC15array5Array2atGfE(_M0L1wS1183, _M0L3valS3740);
          _M0L6w__maxS3739 = _M0L5paramS1168->$4;
          if (_M0L6_2atmpS3738 > _M0L6w__maxS3739) {
            int32_t _M0L3valS3741 = _M0L1sS1178->$0;
            float _M0L6w__maxS3742 = _M0L5paramS1168->$4;
            #line 964 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1183, _M0L3valS3741, _M0L6w__maxS3742);
          }
          _M0L3valS3744 = _M0L1sS1178->$0;
          _M0L6_2atmpS3743 = _M0L3valS3744 + 1;
          _M0L1sS1178->$0 = _M0L6_2atmpS3743;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS1178);
        }
        break;
      }
      _M0L3valS3748 = _M0L1jS1170->$0;
      _M0L6_2atmpS3747 = _M0L3valS3748 + 1;
      _M0L1jS1170->$0 = _M0L6_2atmpS3747;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1170);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt25stdp__antisymmetric__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1140,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1130,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1132,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1139,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1135,
  struct _M0TP26RiantR8snn__mbt26STDPAntiSymmetricVariables* _M0L4varsS1142,
  struct _M0TP26RiantR8snn__mbt17STDPAntiSymmetric* _M0L5paramS1141,
  float _M0L2dtS1154
) {
  int32_t _M0L6n__preS1129;
  int32_t _M0L7n__postS1131;
  struct _M0TPB8MutLocalGiE* _M0L1jS1133;
  int32_t _M0L3nnzS1145;
  float _M0L4a__xS3670;
  float _M0L6tau__xS3671;
  float _M0L18a__x__over__tau__xS1146;
  struct _M0TPB8MutLocalGiE* _M0L2s2S1147;
  float _M0L6tau__xS3669;
  float _M0L11inv__tau__xS1151;
  float _M0L6tau__yS3668;
  float _M0L11inv__tau__yS1152;
  struct _M0TPB8MutLocalGiE* _M0L1iS1153;
  struct _M0TPB8MutLocalGiE* _M0L2s3S1159;
  #line 622 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 632 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1129 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1130);
  #line 633 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1131 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1132);
  _M0L1jS1133
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1133)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1133->$0 = 0;
  while (1) {
    int32_t _M0L3valS3568 = _M0L1jS1133->$0;
    if (_M0L3valS3568 < _M0L6n__preS1129) {
      int32_t _M0L3valS3569 = _M0L1jS1133->$0;
      int32_t _M0L3valS3590;
      int32_t _M0L6_2atmpS3589;
      #line 637 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1130, _M0L3valS3569)) {
        int32_t _M0L3valS3588 = _M0L1jS1133->$0;
        int32_t _M0L5startS1134;
        int32_t _M0L3valS3587;
        int32_t _M0L6_2atmpS3586;
        int32_t _M0L3endS1136;
        struct _M0TPB8MutLocalGiE* _M0L1sS1137;
        #line 638 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS1134
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1135, _M0L3valS3588);
        _M0L3valS3587 = _M0L1jS1133->$0;
        _M0L6_2atmpS3586 = _M0L3valS3587 + 1;
        #line 639 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3endS1136
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1135, _M0L6_2atmpS3586);
        _M0L1sS1137
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS1137)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS1137->$0 = _M0L5startS1134;
        while (1) {
          int32_t _M0L3valS3570 = _M0L1sS1137->$0;
          if (_M0L3valS3570 < _M0L3endS1136) {
            int32_t _M0L3valS3585 = _M0L1sS1137->$0;
            int32_t _M0L9post__idxS1138;
            int32_t _M0L3valS3571;
            int32_t _M0L3valS3582;
            float _M0L6_2atmpS3580;
            float _M0L10alpha__preS3581;
            float _M0L6_2atmpS3573;
            float _M0L4a__yS3578;
            float _M0L6tau__yS3579;
            float _M0L6_2atmpS3575;
            struct _M0TPB5ArrayGfE* _M0L5to__yS3577;
            float _M0L6_2atmpS3576;
            float _M0L6_2atmpS3574;
            float _M0L6_2atmpS3572;
            int32_t _M0L3valS3584;
            int32_t _M0L6_2atmpS3583;
            #line 642 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L9post__idxS1138
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1139, _M0L3valS3585);
            _M0L3valS3571 = _M0L1sS1137->$0;
            _M0L3valS3582 = _M0L1sS1137->$0;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3580
            = _M0MPC15array5Array2atGfE(_M0L1wS1140, _M0L3valS3582);
            _M0L10alpha__preS3581 = _M0L5paramS1141->$4;
            _M0L6_2atmpS3573 = _M0L6_2atmpS3580 + _M0L10alpha__preS3581;
            _M0L4a__yS3578 = _M0L5paramS1141->$1;
            _M0L6tau__yS3579 = _M0L5paramS1141->$3;
            _M0L6_2atmpS3575 = _M0L4a__yS3578 / _M0L6tau__yS3579;
            _M0L5to__yS3577 = _M0L4varsS1142->$1;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3576
            = _M0MPC15array5Array2atGfE(_M0L5to__yS3577, _M0L9post__idxS1138);
            _M0L6_2atmpS3574 = _M0L6_2atmpS3575 * _M0L6_2atmpS3576;
            _M0L6_2atmpS3572 = _M0L6_2atmpS3573 - _M0L6_2atmpS3574;
            #line 643 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1140, _M0L3valS3571, _M0L6_2atmpS3572);
            _M0L3valS3584 = _M0L1sS1137->$0;
            _M0L6_2atmpS3583 = _M0L3valS3584 + 1;
            _M0L1sS1137->$0 = _M0L6_2atmpS3583;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS1137);
          }
          break;
        }
      }
      _M0L3valS3590 = _M0L1jS1133->$0;
      _M0L6_2atmpS3589 = _M0L3valS3590 + 1;
      _M0L1jS1133->$0 = _M0L6_2atmpS3589;
      continue;
    }
    break;
  }
  #line 650 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L3nnzS1145 = _M0MPC15array5Array6lengthGfE(_M0L1wS1140);
  _M0L4a__xS3670 = _M0L5paramS1141->$0;
  _M0L6tau__xS3671 = _M0L5paramS1141->$2;
  _M0L18a__x__over__tau__xS1146 = _M0L4a__xS3670 / _M0L6tau__xS3671;
  _M0L2s2S1147
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S1147)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S1147->$0 = 0;
  while (1) {
    int32_t _M0L3valS3591 = _M0L2s2S1147->$0;
    if (_M0L3valS3591 < _M0L3nnzS1145) {
      int32_t _M0L3valS3604 = _M0L2s2S1147->$0;
      int32_t _M0L9post__idxS1148;
      int32_t _M0L3valS3603;
      int32_t _M0L6_2atmpS3602;
      #line 654 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L9post__idxS1148
      = _M0MPC15array5Array2atGiE(_M0L6colptrS1139, _M0L3valS3604);
      #line 655 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (
        _M0MPC15array5Array2atGbE(_M0L10post__fireS1132, _M0L9post__idxS1148)
      ) {
        int32_t _M0L3valS3601 = _M0L2s2S1147->$0;
        int32_t _M0L6j__preS1149;
        int32_t _M0L3valS3592;
        int32_t _M0L3valS3600;
        float _M0L6_2atmpS3598;
        float _M0L11alpha__postS3599;
        float _M0L6_2atmpS3594;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3597;
        float _M0L6_2atmpS3596;
        float _M0L6_2atmpS3595;
        float _M0L6_2atmpS3593;
        #line 656 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6j__preS1149
        = _M0FP26RiantR8snn__mbt20find__pre__for__conn(_M0L6rowptrS1135, _M0L3valS3601);
        _M0L3valS3592 = _M0L2s2S1147->$0;
        _M0L3valS3600 = _M0L2s2S1147->$0;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3598
        = _M0MPC15array5Array2atGfE(_M0L1wS1140, _M0L3valS3600);
        _M0L11alpha__postS3599 = _M0L5paramS1141->$5;
        _M0L6_2atmpS3594 = _M0L6_2atmpS3598 + _M0L11alpha__postS3599;
        _M0L5tr__xS3597 = _M0L4varsS1142->$0;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3596
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3597, _M0L6j__preS1149);
        _M0L6_2atmpS3595 = _M0L18a__x__over__tau__xS1146 * _M0L6_2atmpS3596;
        _M0L6_2atmpS3593 = _M0L6_2atmpS3594 + _M0L6_2atmpS3595;
        #line 657 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1140, _M0L3valS3592, _M0L6_2atmpS3593);
      }
      _M0L3valS3603 = _M0L2s2S1147->$0;
      _M0L6_2atmpS3602 = _M0L3valS3603 + 1;
      _M0L2s2S1147->$0 = _M0L6_2atmpS3602;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S1147);
    }
    break;
  }
  _M0L6tau__xS3669 = _M0L5paramS1141->$2;
  _M0L11inv__tau__xS1151 = 0x1p+0f / _M0L6tau__xS3669;
  _M0L6tau__yS3668 = _M0L5paramS1141->$3;
  _M0L11inv__tau__yS1152 = 0x1p+0f / _M0L6tau__yS3668;
  _M0L1iS1153
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1153)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1153->$0 = 0;
  while (1) {
    int32_t _M0L3valS3605 = _M0L1iS1153->$0;
    if (_M0L3valS3605 < _M0L7n__postS1131) {
      struct _M0TPB5ArrayGfE* _M0L5to__yS3606 = _M0L4varsS1142->$1;
      int32_t _M0L3valS3607 = _M0L1iS1153->$0;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3616 = _M0L4varsS1142->$1;
      int32_t _M0L3valS3617 = _M0L1iS1153->$0;
      float _M0L6_2atmpS3609;
      struct _M0TPB5ArrayGfE* _M0L5to__yS3614;
      int32_t _M0L3valS3615;
      float _M0L6_2atmpS3613;
      float _M0L6_2atmpS3612;
      float _M0L6_2atmpS3611;
      float _M0L6_2atmpS3610;
      float _M0L6_2atmpS3608;
      int32_t _M0L3valS3619;
      int32_t _M0L6_2atmpS3618;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3609
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3616, _M0L3valS3617);
      _M0L5to__yS3614 = _M0L4varsS1142->$1;
      _M0L3valS3615 = _M0L1iS1153->$0;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3613
      = _M0MPC15array5Array2atGfE(_M0L5to__yS3614, _M0L3valS3615);
      _M0L6_2atmpS3612 = -_M0L6_2atmpS3613;
      _M0L6_2atmpS3611 = _M0L2dtS1154 * _M0L6_2atmpS3612;
      _M0L6_2atmpS3610 = _M0L6_2atmpS3611 * _M0L11inv__tau__yS1152;
      _M0L6_2atmpS3608 = _M0L6_2atmpS3609 + _M0L6_2atmpS3610;
      #line 666 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5to__yS3606, _M0L3valS3607, _M0L6_2atmpS3608);
      _M0L3valS3619 = _M0L1iS1153->$0;
      _M0L6_2atmpS3618 = _M0L3valS3619 + 1;
      _M0L1iS1153->$0 = _M0L6_2atmpS3618;
      continue;
    }
    break;
  }
  _M0L1jS1133->$0 = 0;
  while (1) {
    int32_t _M0L3valS3620 = _M0L1jS1133->$0;
    if (_M0L3valS3620 < _M0L6n__preS1129) {
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3621 = _M0L4varsS1142->$0;
      int32_t _M0L3valS3622 = _M0L1jS1133->$0;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3631 = _M0L4varsS1142->$0;
      int32_t _M0L3valS3632 = _M0L1jS1133->$0;
      float _M0L6_2atmpS3624;
      struct _M0TPB5ArrayGfE* _M0L5tr__xS3629;
      int32_t _M0L3valS3630;
      float _M0L6_2atmpS3628;
      float _M0L6_2atmpS3627;
      float _M0L6_2atmpS3626;
      float _M0L6_2atmpS3625;
      float _M0L6_2atmpS3623;
      int32_t _M0L3valS3634;
      int32_t _M0L6_2atmpS3633;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3624
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3631, _M0L3valS3632);
      _M0L5tr__xS3629 = _M0L4varsS1142->$0;
      _M0L3valS3630 = _M0L1jS1133->$0;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3628
      = _M0MPC15array5Array2atGfE(_M0L5tr__xS3629, _M0L3valS3630);
      _M0L6_2atmpS3627 = -_M0L6_2atmpS3628;
      _M0L6_2atmpS3626 = _M0L2dtS1154 * _M0L6_2atmpS3627;
      _M0L6_2atmpS3625 = _M0L6_2atmpS3626 * _M0L11inv__tau__xS1151;
      _M0L6_2atmpS3623 = _M0L6_2atmpS3624 + _M0L6_2atmpS3625;
      #line 671 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tr__xS3621, _M0L3valS3622, _M0L6_2atmpS3623);
      _M0L3valS3634 = _M0L1jS1133->$0;
      _M0L6_2atmpS3633 = _M0L3valS3634 + 1;
      _M0L1jS1133->$0 = _M0L6_2atmpS3633;
      continue;
    }
    break;
  }
  _M0L1iS1153->$0 = 0;
  while (1) {
    int32_t _M0L3valS3635 = _M0L1iS1153->$0;
    if (_M0L3valS3635 < _M0L7n__postS1131) {
      int32_t _M0L3valS3636 = _M0L1iS1153->$0;
      int32_t _M0L3valS3644;
      int32_t _M0L6_2atmpS3643;
      #line 677 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1132, _M0L3valS3636)) {
        struct _M0TPB5ArrayGfE* _M0L5to__yS3637 = _M0L4varsS1142->$1;
        int32_t _M0L3valS3638 = _M0L1iS1153->$0;
        struct _M0TPB5ArrayGfE* _M0L5to__yS3641 = _M0L4varsS1142->$1;
        int32_t _M0L3valS3642 = _M0L1iS1153->$0;
        float _M0L6_2atmpS3640;
        float _M0L6_2atmpS3639;
        #line 678 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3640
        = _M0MPC15array5Array2atGfE(_M0L5to__yS3641, _M0L3valS3642);
        _M0L6_2atmpS3639 = _M0L6_2atmpS3640 + 0x1p+0f;
        #line 678 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5to__yS3637, _M0L3valS3638, _M0L6_2atmpS3639);
      }
      _M0L3valS3644 = _M0L1iS1153->$0;
      _M0L6_2atmpS3643 = _M0L3valS3644 + 1;
      _M0L1iS1153->$0 = _M0L6_2atmpS3643;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1153);
    }
    break;
  }
  _M0L1jS1133->$0 = 0;
  while (1) {
    int32_t _M0L3valS3645 = _M0L1jS1133->$0;
    if (_M0L3valS3645 < _M0L6n__preS1129) {
      int32_t _M0L3valS3646 = _M0L1jS1133->$0;
      int32_t _M0L3valS3654;
      int32_t _M0L6_2atmpS3653;
      #line 684 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1130, _M0L3valS3646)) {
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3647 = _M0L4varsS1142->$0;
        int32_t _M0L3valS3648 = _M0L1jS1133->$0;
        struct _M0TPB5ArrayGfE* _M0L5tr__xS3651 = _M0L4varsS1142->$0;
        int32_t _M0L3valS3652 = _M0L1jS1133->$0;
        float _M0L6_2atmpS3650;
        float _M0L6_2atmpS3649;
        #line 685 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3650
        = _M0MPC15array5Array2atGfE(_M0L5tr__xS3651, _M0L3valS3652);
        _M0L6_2atmpS3649 = _M0L6_2atmpS3650 + 0x1p+0f;
        #line 685 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tr__xS3647, _M0L3valS3648, _M0L6_2atmpS3649);
      }
      _M0L3valS3654 = _M0L1jS1133->$0;
      _M0L6_2atmpS3653 = _M0L3valS3654 + 1;
      _M0L1jS1133->$0 = _M0L6_2atmpS3653;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1133);
    }
    break;
  }
  _M0L2s3S1159
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s3S1159)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s3S1159->$0 = 0;
  while (1) {
    int32_t _M0L3valS3655 = _M0L2s3S1159->$0;
    if (_M0L3valS3655 < _M0L3nnzS1145) {
      int32_t _M0L3valS3658 = _M0L2s3S1159->$0;
      float _M0L6_2atmpS3656;
      float _M0L6w__minS3657;
      int32_t _M0L3valS3667;
      int32_t _M0L6_2atmpS3666;
      #line 692 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3656
      = _M0MPC15array5Array2atGfE(_M0L1wS1140, _M0L3valS3658);
      _M0L6w__minS3657 = _M0L5paramS1141->$7;
      if (_M0L6_2atmpS3656 < _M0L6w__minS3657) {
        int32_t _M0L3valS3659 = _M0L2s3S1159->$0;
        float _M0L6w__minS3660 = _M0L5paramS1141->$7;
        #line 693 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1140, _M0L3valS3659, _M0L6w__minS3660);
      } else {
        int32_t _M0L3valS3663 = _M0L2s3S1159->$0;
        float _M0L6_2atmpS3661;
        float _M0L6w__maxS3662;
        #line 694 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3661
        = _M0MPC15array5Array2atGfE(_M0L1wS1140, _M0L3valS3663);
        _M0L6w__maxS3662 = _M0L5paramS1141->$6;
        if (_M0L6_2atmpS3661 > _M0L6w__maxS3662) {
          int32_t _M0L3valS3664 = _M0L2s3S1159->$0;
          float _M0L6w__maxS3665 = _M0L5paramS1141->$6;
          #line 695 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0MPC15array5Array3setGfE(_M0L1wS1140, _M0L3valS3664, _M0L6w__maxS3665);
        }
      }
      _M0L3valS3667 = _M0L2s3S1159->$0;
      _M0L6_2atmpS3666 = _M0L3valS3667 + 1;
      _M0L2s3S1159->$0 = _M0L6_2atmpS3666;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s3S1159);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt24stdp__mexican__hat__step(
  struct _M0TPB5ArrayGfE* _M0L1wS1115,
  struct _M0TPB5ArrayGbE* _M0L9pre__fireS1091,
  struct _M0TPB5ArrayGbE* _M0L10post__fireS1093,
  struct _M0TPB5ArrayGiE* _M0L6colptrS1110,
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1106,
  struct _M0TPB5ArrayGfE* _M0L4tpreS1101,
  struct _M0TPB5ArrayGfE* _M0L5tpostS1097,
  struct _M0TP26RiantR8snn__mbt14STDPMexicanHat* _M0L5paramS1095,
  float _M0L2dtS1098
) {
  int32_t _M0L6n__preS1090;
  int32_t _M0L7n__postS1092;
  float _M0L3tauS3567;
  float _M0L8inv__tauS1094;
  struct _M0TPB8MutLocalGiE* _M0L1iS1096;
  struct _M0TPB8MutLocalGiE* _M0L1jS1100;
  int32_t _M0L3nnzS1118;
  struct _M0TPB8MutLocalGiE* _M0L2s2S1119;
  struct _M0TPB8MutLocalGiE* _M0L2s3S1127;
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 461 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6n__preS1090 = _M0MPC15array5Array6lengthGbE(_M0L9pre__fireS1091);
  #line 462 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L7n__postS1092 = _M0MPC15array5Array6lengthGbE(_M0L10post__fireS1093);
  _M0L3tauS3567 = _M0L5paramS1095->$1;
  _M0L8inv__tauS1094 = 0x1p+0f / _M0L3tauS3567;
  _M0L1iS1096
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS1096)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS1096->$0 = 0;
  while (1) {
    int32_t _M0L3valS3481 = _M0L1iS1096->$0;
    if (_M0L3valS3481 < _M0L7n__postS1092) {
      int32_t _M0L3valS3482 = _M0L1iS1096->$0;
      int32_t _M0L3valS3490 = _M0L1iS1096->$0;
      float _M0L6_2atmpS3484;
      int32_t _M0L3valS3489;
      float _M0L6_2atmpS3488;
      float _M0L6_2atmpS3487;
      float _M0L6_2atmpS3486;
      float _M0L6_2atmpS3485;
      float _M0L6_2atmpS3483;
      int32_t _M0L3valS3492;
      int32_t _M0L6_2atmpS3491;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3484
      = _M0MPC15array5Array2atGfE(_M0L5tpostS1097, _M0L3valS3490);
      _M0L3valS3489 = _M0L1iS1096->$0;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3488
      = _M0MPC15array5Array2atGfE(_M0L5tpostS1097, _M0L3valS3489);
      _M0L6_2atmpS3487 = -_M0L6_2atmpS3488;
      _M0L6_2atmpS3486 = _M0L2dtS1098 * _M0L6_2atmpS3487;
      _M0L6_2atmpS3485 = _M0L6_2atmpS3486 * _M0L8inv__tauS1094;
      _M0L6_2atmpS3483 = _M0L6_2atmpS3484 + _M0L6_2atmpS3485;
      #line 467 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L5tpostS1097, _M0L3valS3482, _M0L6_2atmpS3483);
      _M0L3valS3492 = _M0L1iS1096->$0;
      _M0L6_2atmpS3491 = _M0L3valS3492 + 1;
      _M0L1iS1096->$0 = _M0L6_2atmpS3491;
      continue;
    }
    break;
  }
  _M0L1jS1100
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS1100)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS1100->$0 = 0;
  while (1) {
    int32_t _M0L3valS3493 = _M0L1jS1100->$0;
    if (_M0L3valS3493 < _M0L6n__preS1090) {
      int32_t _M0L3valS3494 = _M0L1jS1100->$0;
      int32_t _M0L3valS3502 = _M0L1jS1100->$0;
      float _M0L6_2atmpS3496;
      int32_t _M0L3valS3501;
      float _M0L6_2atmpS3500;
      float _M0L6_2atmpS3499;
      float _M0L6_2atmpS3498;
      float _M0L6_2atmpS3497;
      float _M0L6_2atmpS3495;
      int32_t _M0L3valS3504;
      int32_t _M0L6_2atmpS3503;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3496
      = _M0MPC15array5Array2atGfE(_M0L4tpreS1101, _M0L3valS3502);
      _M0L3valS3501 = _M0L1jS1100->$0;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3500
      = _M0MPC15array5Array2atGfE(_M0L4tpreS1101, _M0L3valS3501);
      _M0L6_2atmpS3499 = -_M0L6_2atmpS3500;
      _M0L6_2atmpS3498 = _M0L2dtS1098 * _M0L6_2atmpS3499;
      _M0L6_2atmpS3497 = _M0L6_2atmpS3498 * _M0L8inv__tauS1094;
      _M0L6_2atmpS3495 = _M0L6_2atmpS3496 + _M0L6_2atmpS3497;
      #line 472 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0MPC15array5Array3setGfE(_M0L4tpreS1101, _M0L3valS3494, _M0L6_2atmpS3495);
      _M0L3valS3504 = _M0L1jS1100->$0;
      _M0L6_2atmpS3503 = _M0L3valS3504 + 1;
      _M0L1jS1100->$0 = _M0L6_2atmpS3503;
      continue;
    }
    break;
  }
  _M0L1iS1096->$0 = 0;
  while (1) {
    int32_t _M0L3valS3505 = _M0L1iS1096->$0;
    if (_M0L3valS3505 < _M0L7n__postS1092) {
      int32_t _M0L3valS3506 = _M0L1iS1096->$0;
      int32_t _M0L3valS3512;
      int32_t _M0L6_2atmpS3511;
      #line 478 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L10post__fireS1093, _M0L3valS3506)) {
        int32_t _M0L3valS3507 = _M0L1iS1096->$0;
        int32_t _M0L3valS3510 = _M0L1iS1096->$0;
        float _M0L6_2atmpS3509;
        float _M0L6_2atmpS3508;
        #line 479 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3509
        = _M0MPC15array5Array2atGfE(_M0L5tpostS1097, _M0L3valS3510);
        _M0L6_2atmpS3508 = _M0L6_2atmpS3509 + 0x1p+0f;
        #line 479 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L5tpostS1097, _M0L3valS3507, _M0L6_2atmpS3508);
      }
      _M0L3valS3512 = _M0L1iS1096->$0;
      _M0L6_2atmpS3511 = _M0L3valS3512 + 1;
      _M0L1iS1096->$0 = _M0L6_2atmpS3511;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS1096);
    }
    break;
  }
  _M0L1jS1100->$0 = 0;
  while (1) {
    int32_t _M0L3valS3513 = _M0L1jS1100->$0;
    if (_M0L3valS3513 < _M0L6n__preS1090) {
      int32_t _M0L3valS3514 = _M0L1jS1100->$0;
      int32_t _M0L3valS3520;
      int32_t _M0L6_2atmpS3519;
      #line 485 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1091, _M0L3valS3514)) {
        int32_t _M0L3valS3515 = _M0L1jS1100->$0;
        int32_t _M0L3valS3518 = _M0L1jS1100->$0;
        float _M0L6_2atmpS3517;
        float _M0L6_2atmpS3516;
        #line 486 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3517
        = _M0MPC15array5Array2atGfE(_M0L4tpreS1101, _M0L3valS3518);
        _M0L6_2atmpS3516 = _M0L6_2atmpS3517 + 0x1p+0f;
        #line 486 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L4tpreS1101, _M0L3valS3515, _M0L6_2atmpS3516);
      }
      _M0L3valS3520 = _M0L1jS1100->$0;
      _M0L6_2atmpS3519 = _M0L3valS3520 + 1;
      _M0L1jS1100->$0 = _M0L6_2atmpS3519;
      continue;
    }
    break;
  }
  _M0L1jS1100->$0 = 0;
  while (1) {
    int32_t _M0L3valS3521 = _M0L1jS1100->$0;
    if (_M0L3valS3521 < _M0L6n__preS1090) {
      int32_t _M0L3valS3522 = _M0L1jS1100->$0;
      int32_t _M0L3valS3540;
      int32_t _M0L6_2atmpS3539;
      #line 493 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L9pre__fireS1091, _M0L3valS3522)) {
        int32_t _M0L3valS3538 = _M0L1jS1100->$0;
        int32_t _M0L5startS1105;
        int32_t _M0L3valS3537;
        int32_t _M0L6_2atmpS3536;
        int32_t _M0L3endS1107;
        struct _M0TPB8MutLocalGiE* _M0L1sS1108;
        #line 494 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L5startS1105
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1106, _M0L3valS3538);
        _M0L3valS3537 = _M0L1jS1100->$0;
        _M0L6_2atmpS3536 = _M0L3valS3537 + 1;
        #line 495 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3endS1107
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS1106, _M0L6_2atmpS3536);
        _M0L1sS1108
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS1108)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS1108->$0 = _M0L5startS1105;
        while (1) {
          int32_t _M0L3valS3523 = _M0L1sS1108->$0;
          if (_M0L3valS3523 < _M0L3endS1107) {
            int32_t _M0L3valS3535 = _M0L1sS1108->$0;
            int32_t _M0L9post__idxS1109;
            int32_t _M0L3valS3534;
            float _M0L6_2atmpS3532;
            float _M0L6_2atmpS3533;
            float _M0L5ratioS1111;
            float _M0L3lnxS1112;
            float _M0L1xS1113;
            float _M0L1aS3530;
            float _M0L6_2atmpS3531;
            float _M0L2dwS1114;
            int32_t _M0L3valS3524;
            int32_t _M0L3valS3527;
            float _M0L6_2atmpS3526;
            float _M0L6_2atmpS3525;
            int32_t _M0L3valS3529;
            int32_t _M0L6_2atmpS3528;
            #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L9post__idxS1109
            = _M0MPC15array5Array2atGiE(_M0L6colptrS1110, _M0L3valS3535);
            _M0L3valS3534 = _M0L1jS1100->$0;
            #line 499 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3532
            = _M0MPC15array5Array2atGfE(_M0L4tpreS1101, _M0L3valS3534);
            #line 499 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3533
            = _M0MPC15array5Array2atGfE(_M0L5tpostS1097, _M0L9post__idxS1109);
            _M0L5ratioS1111 = _M0L6_2atmpS3532 / _M0L6_2atmpS3533;
            #line 500 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L3lnxS1112 = _M0FP26RiantR8snn__mbt4logf(_M0L5ratioS1111);
            _M0L1xS1113 = _M0L3lnxS1112 * _M0L3lnxS1112;
            _M0L1aS3530 = _M0L5paramS1095->$0;
            #line 502 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3531
            = _M0FP26RiantR8snn__mbt20mexican__hat__kernel(_M0L1xS1113);
            _M0L2dwS1114 = _M0L1aS3530 * _M0L6_2atmpS3531;
            _M0L3valS3524 = _M0L1sS1108->$0;
            _M0L3valS3527 = _M0L1sS1108->$0;
            #line 503 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0L6_2atmpS3526
            = _M0MPC15array5Array2atGfE(_M0L1wS1115, _M0L3valS3527);
            _M0L6_2atmpS3525 = _M0L6_2atmpS3526 + _M0L2dwS1114;
            #line 503 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
            _M0MPC15array5Array3setGfE(_M0L1wS1115, _M0L3valS3524, _M0L6_2atmpS3525);
            _M0L3valS3529 = _M0L1sS1108->$0;
            _M0L6_2atmpS3528 = _M0L3valS3529 + 1;
            _M0L1sS1108->$0 = _M0L6_2atmpS3528;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS1108);
          }
          break;
        }
      }
      _M0L3valS3540 = _M0L1jS1100->$0;
      _M0L6_2atmpS3539 = _M0L3valS3540 + 1;
      _M0L1jS1100->$0 = _M0L6_2atmpS3539;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS1100);
    }
    break;
  }
  #line 511 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L3nnzS1118 = _M0MPC15array5Array6lengthGfE(_M0L1wS1115);
  _M0L2s2S1119
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s2S1119)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s2S1119->$0 = 0;
  while (1) {
    int32_t _M0L3valS3541 = _M0L2s2S1119->$0;
    if (_M0L3valS3541 < _M0L3nnzS1118) {
      int32_t _M0L3valS3553 = _M0L2s2S1119->$0;
      int32_t _M0L9post__idxS1120;
      int32_t _M0L3valS3552;
      int32_t _M0L6_2atmpS3551;
      #line 514 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L9post__idxS1120
      = _M0MPC15array5Array2atGiE(_M0L6colptrS1110, _M0L3valS3553);
      #line 515 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      if (
        _M0MPC15array5Array2atGbE(_M0L10post__fireS1093, _M0L9post__idxS1120)
      ) {
        int32_t _M0L3valS3550 = _M0L2s2S1119->$0;
        int32_t _M0L6j__preS1121;
        float _M0L6_2atmpS3548;
        float _M0L6_2atmpS3549;
        float _M0L5ratioS1122;
        float _M0L3lnxS1123;
        float _M0L1xS1124;
        float _M0L1aS3546;
        float _M0L6_2atmpS3547;
        float _M0L2dwS1125;
        int32_t _M0L3valS3542;
        int32_t _M0L3valS3545;
        float _M0L6_2atmpS3544;
        float _M0L6_2atmpS3543;
        #line 518 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6j__preS1121
        = _M0FP26RiantR8snn__mbt20find__pre__for__conn(_M0L6rowptrS1106, _M0L3valS3550);
        #line 519 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3548
        = _M0MPC15array5Array2atGfE(_M0L4tpreS1101, _M0L6j__preS1121);
        #line 519 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3549
        = _M0MPC15array5Array2atGfE(_M0L5tpostS1097, _M0L9post__idxS1120);
        _M0L5ratioS1122 = _M0L6_2atmpS3548 / _M0L6_2atmpS3549;
        #line 520 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L3lnxS1123 = _M0FP26RiantR8snn__mbt4logf(_M0L5ratioS1122);
        _M0L1xS1124 = _M0L3lnxS1123 * _M0L3lnxS1123;
        _M0L1aS3546 = _M0L5paramS1095->$0;
        #line 522 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3547
        = _M0FP26RiantR8snn__mbt20mexican__hat__kernel(_M0L1xS1124);
        _M0L2dwS1125 = _M0L1aS3546 * _M0L6_2atmpS3547;
        _M0L3valS3542 = _M0L2s2S1119->$0;
        _M0L3valS3545 = _M0L2s2S1119->$0;
        #line 523 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3544
        = _M0MPC15array5Array2atGfE(_M0L1wS1115, _M0L3valS3545);
        _M0L6_2atmpS3543 = _M0L6_2atmpS3544 + _M0L2dwS1125;
        #line 523 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1115, _M0L3valS3542, _M0L6_2atmpS3543);
      }
      _M0L3valS3552 = _M0L2s2S1119->$0;
      _M0L6_2atmpS3551 = _M0L3valS3552 + 1;
      _M0L2s2S1119->$0 = _M0L6_2atmpS3551;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s2S1119);
    }
    break;
  }
  _M0L2s3S1127
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2s3S1127)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2s3S1127->$0 = 0;
  while (1) {
    int32_t _M0L3valS3554 = _M0L2s3S1127->$0;
    if (_M0L3valS3554 < _M0L3nnzS1118) {
      int32_t _M0L3valS3557 = _M0L2s3S1127->$0;
      float _M0L6_2atmpS3555;
      float _M0L6w__minS3556;
      int32_t _M0L3valS3566;
      int32_t _M0L6_2atmpS3565;
      #line 530 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3555
      = _M0MPC15array5Array2atGfE(_M0L1wS1115, _M0L3valS3557);
      _M0L6w__minS3556 = _M0L5paramS1095->$3;
      if (_M0L6_2atmpS3555 < _M0L6w__minS3556) {
        int32_t _M0L3valS3558 = _M0L2s3S1127->$0;
        float _M0L6w__minS3559 = _M0L5paramS1095->$3;
        #line 531 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1wS1115, _M0L3valS3558, _M0L6w__minS3559);
      } else {
        int32_t _M0L3valS3562 = _M0L2s3S1127->$0;
        float _M0L6_2atmpS3560;
        float _M0L6w__maxS3561;
        #line 532 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
        _M0L6_2atmpS3560
        = _M0MPC15array5Array2atGfE(_M0L1wS1115, _M0L3valS3562);
        _M0L6w__maxS3561 = _M0L5paramS1095->$2;
        if (_M0L6_2atmpS3560 > _M0L6w__maxS3561) {
          int32_t _M0L3valS3563 = _M0L2s3S1127->$0;
          float _M0L6w__maxS3564 = _M0L5paramS1095->$2;
          #line 533 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
          _M0MPC15array5Array3setGfE(_M0L1wS1115, _M0L3valS3563, _M0L6w__maxS3564);
        }
      }
      _M0L3valS3566 = _M0L2s3S1127->$0;
      _M0L6_2atmpS3565 = _M0L3valS3566 + 1;
      _M0L2s3S1127->$0 = _M0L6_2atmpS3565;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2s3S1127);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt20find__pre__for__conn(
  struct _M0TPB5ArrayGiE* _M0L6rowptrS1084,
  int32_t _M0L1sS1088
) {
  int32_t _M0L6_2atmpS3480;
  int32_t _M0L1nS1083;
  struct _M0TPB8MutLocalGiE* _M0L2loS1085;
  struct _M0TPB8MutLocalGiE* _M0L2hiS1086;
  int32_t _result_5898;
  #line 542 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 543 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  _M0L6_2atmpS3480 = _M0MPC15array5Array6lengthGiE(_M0L6rowptrS1084);
  _M0L1nS1083 = _M0L6_2atmpS3480 - 1;
  _M0L2loS1085
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2loS1085)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2loS1085->$0 = 0;
  _M0L2hiS1086
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L2hiS1086)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L2hiS1086->$0 = _M0L1nS1083;
  while (1) {
    int32_t _M0L3valS3472 = _M0L2loS1085->$0;
    int32_t _M0L3valS3473 = _M0L2hiS1086->$0;
    if (_M0L3valS3472 < _M0L3valS3473) {
      int32_t _M0L3valS3478 = _M0L2loS1085->$0;
      int32_t _M0L3valS3479 = _M0L2hiS1086->$0;
      int32_t _M0L6_2atmpS3477 = _M0L3valS3478 + _M0L3valS3479;
      int32_t _M0L6_2atmpS3476 = _M0L6_2atmpS3477 + 1;
      int32_t _M0L3midS1087 = _M0L6_2atmpS3476 / 2;
      int32_t _M0L6_2atmpS3474;
      #line 548 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
      _M0L6_2atmpS3474
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS1084, _M0L3midS1087);
      if (_M0L6_2atmpS3474 <= _M0L1sS1088) {
        _M0L2loS1085->$0 = _M0L3midS1087;
      } else {
        int32_t _M0L6_2atmpS3475 = _M0L3midS1087 - 1;
        _M0L2hiS1086->$0 = _M0L6_2atmpS3475;
      }
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L2hiS1086);
    }
    break;
  }
  _result_5898 = _M0L2loS1085->$0;
  moonbit_decref_cycle_free(_M0L2loS1085);
  return _result_5898;
}

float _M0FP26RiantR8snn__mbt20mexican__hat__kernel(float _M0L1xS1080) {
  #line 427 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  #line 428 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
  if (_M0MPC15float5Float7is__nan(_M0L1xS1080)) {
    return 0x0p+0f;
  } else {
    float _M0L6_2atmpS3471 = -_M0L1xS1080;
    float _M0L3argS1081 = _M0L6_2atmpS3471 / 0x1.6a09e65dc27dfp+0f;
    float _M0L6_2atmpS3469 = 0x1p+0f - _M0L1xS1080;
    float _M0L6_2atmpS3470;
    float _M0L1vS1082;
    #line 432 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    _M0L6_2atmpS3470 = _M0FP26RiantR8snn__mbt4expf(_M0L3argS1081);
    _M0L1vS1082 = _M0L6_2atmpS3469 * _M0L6_2atmpS3470;
    #line 433 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stdp.mbt"
    if (_M0MPC15float5Float7is__nan(_M0L1vS1082)) {
      return 0x0p+0f;
    } else {
      return _M0L1vS1082;
    }
  }
}

int32_t _M0FP26RiantR8snn__mbt19stimulate__balanced(
  struct _M0TP26RiantR8snn__mbt16BalancedStimulus* _M0L1sS1047,
  float _M0L4timeS1045,
  float _M0L2dtS1056
) {
  int32_t _M0L1nS1046;
  struct _M0TP26RiantR8snn__mbt17BalancedParameter* _M0L5paramS1048;
  float _M0L3kIES1049;
  float _M0L4betaS1050;
  float _M0L3tauS1051;
  float _M0L2r0S1052;
  float _M0L1wS1053;
  float _M0L3wIES1054;
  float _M0L6_2atmpS3468;
  float _M0L11inh__lambdaS1055;
  int32_t _M0L7_2abindS1057;
  int32_t _M0L1kS1058;
  float _M0L6_2atmpS3467;
  float _M0L2ccS1062;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
  _M0L1nS1046 = _M0L1sS1047->$1;
  _M0L5paramS1048 = _M0L1sS1047->$0;
  _M0L3kIES1049 = _M0L5paramS1048->$0;
  _M0L4betaS1050 = _M0L5paramS1048->$1;
  _M0L3tauS1051 = _M0L5paramS1048->$2;
  _M0L2r0S1052 = _M0L5paramS1048->$3;
  _M0L1wS1053 = _M0L5paramS1048->$4;
  _M0L3wIES1054 = _M0L5paramS1048->$5;
  _M0L6_2atmpS3468 = _M0L2r0S1052 * _M0L3kIES1049;
  _M0L11inh__lambdaS1055 = _M0L6_2atmpS3468 * _M0L2dtS1056;
  _M0L7_2abindS1057 = 0;
  _M0L1kS1058 = _M0L7_2abindS1057;
  while (1) {
    if (_M0L1kS1058 < _M0L1nS1046) {
      struct _M0TPB5ArrayGbE* _M0L4fireS3381 = _M0L1sS1047->$4;
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3389;
      int32_t _M0L1mS1061;
      int32_t _M0L6_2atmpS3380;
      #line 146 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3381, _M0L1kS1058, 0);
      if (_M0L11inh__lambdaS1055 <= 0x0p+0f) {
        goto join_1059;
      }
      _M0L3rngS3389 = _M0L1sS1047->$7;
      #line 150 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
      _M0L1mS1061
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3389, _M0L11inh__lambdaS1055);
      if (_M0L1mS1061 > 0) {
        struct _M0TPB5ArrayGfE* _M0L2giS3382 = _M0L1sS1047->$3;
        struct _M0TPB5ArrayGfE* _M0L2giS3388 = _M0L1sS1047->$3;
        float _M0L6_2atmpS3384;
        float _M0L6_2atmpS3387;
        float _M0L6_2atmpS3386;
        float _M0L6_2atmpS3385;
        float _M0L6_2atmpS3383;
        #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3384
        = _M0MPC15array5Array2atGfE(_M0L2giS3388, _M0L1kS1058);
        _M0L6_2atmpS3387 = (float)_M0L1mS1061;
        _M0L6_2atmpS3386 = _M0L1wS1053 * _M0L6_2atmpS3387;
        _M0L6_2atmpS3385 = _M0L6_2atmpS3386 * _M0L3wIES1054;
        _M0L6_2atmpS3383 = _M0L6_2atmpS3384 + _M0L6_2atmpS3385;
        #line 152 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L2giS3382, _M0L1kS1058, _M0L6_2atmpS3383);
      }
      goto join_1059;
      goto joinlet_5900;
      join_1059:;
      _M0L6_2atmpS3380 = _M0L1kS1058 + 1;
      _M0L1kS1058 = _M0L6_2atmpS3380;
      continue;
      joinlet_5900:;
    }
    break;
  }
  _M0L6_2atmpS3467 = _M0L2dtS1056 / _M0L3tauS1051;
  _M0L2ccS1062 = 0x1p+0f - _M0L6_2atmpS3467;
  if (_M0L5paramS1048->$6) {
    struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3427 = _M0L1sS1047->$7;
    double _M0L6_2atmpS3426;
    float _M0L6_2atmpS3425;
    float _M0L2reS1063;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3390;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3395;
    float _M0L6_2atmpS3394;
    float _M0L6_2atmpS3393;
    float _M0L6_2atmpS3392;
    float _M0L6_2atmpS3391;
    struct _M0TPB5ArrayGfE* _M0L5noiseS3424;
    float _M0L6_2atmpS3423;
    float _M0L6_2atmpS3422;
    struct _M0TPB8MutLocalGfE* _M0L2nbS1064;
    float _M0L3valS3396;
    float _M0L3valS3397;
    float _M0L6_2atmpS3420;
    float _M0L3valS3421;
    float _M0L6_2atmpS3417;
    struct _M0TPB5ArrayGfE* _M0L1rS3419;
    float _M0L6_2atmpS3418;
    float _M0L6_2atmpS3416;
    struct _M0TPB8MutLocalGfE* _M0L5erateS1065;
    float _M0L3valS3398;
    struct _M0TPB5ArrayGfE* _M0L1rS3399;
    struct _M0TPB5ArrayGfE* _M0L1rS3406;
    float _M0L6_2atmpS3401;
    float _M0L3valS3405;
    float _M0L6_2atmpS3404;
    float _M0L6_2atmpS3403;
    float _M0L6_2atmpS3402;
    float _M0L6_2atmpS3400;
    float _M0L3valS3415;
    float _M0L11exc__lambdaS1066;
    struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3414;
    int32_t _M0L1mS1067;
    #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3426 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS3427);
    _M0L6_2atmpS3425 = (float)_M0L6_2atmpS3426;
    _M0L2reS1063 = _M0L6_2atmpS3425 - 0x1p-1f;
    _M0L5noiseS3390 = _M0L1sS1047->$6;
    _M0L5noiseS3395 = _M0L1sS1047->$6;
    #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3394 = _M0MPC15array5Array2atGfE(_M0L5noiseS3395, 0);
    _M0L6_2atmpS3393 = _M0L6_2atmpS3394 - _M0L2reS1063;
    _M0L6_2atmpS3392 = _M0L6_2atmpS3393 * _M0L2ccS1062;
    _M0L6_2atmpS3391 = _M0L6_2atmpS3392 + _M0L2reS1063;
    #line 164 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0MPC15array5Array3setGfE(_M0L5noiseS3390, 0, _M0L6_2atmpS3391);
    _M0L5noiseS3424 = _M0L1sS1047->$6;
    #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3423 = _M0MPC15array5Array2atGfE(_M0L5noiseS3424, 0);
    _M0L6_2atmpS3422 = _M0L6_2atmpS3423 * _M0L4betaS1050;
    _M0L2nbS1064
    = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
    Moonbit_object_header(_M0L2nbS1064)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L2nbS1064->$0 = _M0L6_2atmpS3422;
    _M0L3valS3396 = _M0L2nbS1064->$0;
    if (_M0L3valS3396 > 0x1p+0f) {
      _M0L2nbS1064->$0 = 0x1p+0f;
    }
    _M0L3valS3397 = _M0L2nbS1064->$0;
    if (_M0L3valS3397 < 0x0p+0f) {
      _M0L2nbS1064->$0 = 0x0p+0f;
    }
    _M0L6_2atmpS3420 = _M0L2r0S1052 / 0x1p+1f;
    _M0L3valS3421 = _M0L2nbS1064->$0;
    moonbit_decref_cycle_free(_M0L2nbS1064);
    _M0L6_2atmpS3417 = _M0L6_2atmpS3420 * _M0L3valS3421;
    _M0L1rS3419 = _M0L1sS1047->$5;
    #line 172 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3418 = _M0MPC15array5Array2atGfE(_M0L1rS3419, 0);
    _M0L6_2atmpS3416 = _M0L6_2atmpS3417 + _M0L6_2atmpS3418;
    _M0L5erateS1065
    = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
    Moonbit_object_header(_M0L5erateS1065)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L5erateS1065->$0 = _M0L6_2atmpS3416;
    _M0L3valS3398 = _M0L5erateS1065->$0;
    if (_M0L3valS3398 < 0x0p+0f) {
      _M0L5erateS1065->$0 = 0x0p+0f;
    }
    _M0L1rS3399 = _M0L1sS1047->$5;
    _M0L1rS3406 = _M0L1sS1047->$5;
    #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L6_2atmpS3401 = _M0MPC15array5Array2atGfE(_M0L1rS3406, 0);
    _M0L3valS3405 = _M0L5erateS1065->$0;
    _M0L6_2atmpS3404 = _M0L2r0S1052 - _M0L3valS3405;
    _M0L6_2atmpS3403 = _M0L6_2atmpS3404 / 0x1.9p+8f;
    _M0L6_2atmpS3402 = _M0L6_2atmpS3403 * _M0L2dtS1056;
    _M0L6_2atmpS3400 = _M0L6_2atmpS3401 + _M0L6_2atmpS3402;
    #line 176 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0MPC15array5Array3setGfE(_M0L1rS3399, 0, _M0L6_2atmpS3400);
    _M0L3valS3415 = _M0L5erateS1065->$0;
    moonbit_decref_cycle_free(_M0L5erateS1065);
    _M0L11exc__lambdaS1066 = _M0L3valS3415 * _M0L2dtS1056;
    _M0L3rngS3414 = _M0L1sS1047->$7;
    #line 178 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
    _M0L1mS1067
    = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3414, _M0L11exc__lambdaS1066);
    if (_M0L1mS1067 > 0) {
      float _M0L6_2atmpS3413 = (float)_M0L1mS1067;
      float _M0L3addS1068 = _M0L1wS1053 * _M0L6_2atmpS3413;
      int32_t _M0L7_2abindS1069 = 0;
      int32_t _M0L1iS1070 = _M0L7_2abindS1069;
      while (1) {
        if (_M0L1iS1070 < _M0L1nS1046) {
          struct _M0TPB5ArrayGfE* _M0L2geS3407 = _M0L1sS1047->$2;
          struct _M0TPB5ArrayGfE* _M0L2geS3410 = _M0L1sS1047->$2;
          float _M0L6_2atmpS3409;
          float _M0L6_2atmpS3408;
          struct _M0TPB5ArrayGbE* _M0L4fireS3411;
          int32_t _M0L6_2atmpS3412;
          #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0L6_2atmpS3409
          = _M0MPC15array5Array2atGfE(_M0L2geS3410, _M0L1iS1070);
          _M0L6_2atmpS3408 = _M0L6_2atmpS3409 + _M0L3addS1068;
          #line 182 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGfE(_M0L2geS3407, _M0L1iS1070, _M0L6_2atmpS3408);
          _M0L4fireS3411 = _M0L1sS1047->$4;
          #line 183 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGbE(_M0L4fireS3411, _M0L1iS1070, 1);
          _M0L6_2atmpS3412 = _M0L1iS1070 + 1;
          _M0L1iS1070 = _M0L6_2atmpS3412;
          continue;
        }
        break;
      }
    }
  } else {
    int32_t _M0L7_2abindS1072 = 0;
    int32_t _M0L1iS1073 = _M0L7_2abindS1072;
    while (1) {
      if (_M0L1iS1073 < _M0L1nS1046) {
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3465 =
          _M0L1sS1047->$7;
        double _M0L6_2atmpS3464;
        float _M0L6_2atmpS3463;
        float _M0L2reS1074;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3428;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3433;
        float _M0L6_2atmpS3432;
        float _M0L6_2atmpS3431;
        float _M0L6_2atmpS3430;
        float _M0L6_2atmpS3429;
        struct _M0TPB5ArrayGfE* _M0L5noiseS3462;
        float _M0L6_2atmpS3461;
        float _M0L6_2atmpS3460;
        struct _M0TPB8MutLocalGfE* _M0L2nbS1075;
        float _M0L3valS3434;
        float _M0L3valS3435;
        float _M0L6_2atmpS3458;
        float _M0L3valS3459;
        float _M0L6_2atmpS3455;
        struct _M0TPB5ArrayGfE* _M0L1rS3457;
        float _M0L6_2atmpS3456;
        float _M0L6_2atmpS3454;
        struct _M0TPB8MutLocalGfE* _M0L5erateS1076;
        float _M0L3valS3436;
        struct _M0TPB5ArrayGfE* _M0L1rS3437;
        struct _M0TPB5ArrayGfE* _M0L1rS3444;
        float _M0L6_2atmpS3439;
        float _M0L3valS3443;
        float _M0L6_2atmpS3442;
        float _M0L6_2atmpS3441;
        float _M0L6_2atmpS3440;
        float _M0L6_2atmpS3438;
        float _M0L3valS3453;
        float _M0L11exc__lambdaS1077;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3452;
        int32_t _M0L1mS1078;
        int32_t _M0L6_2atmpS3466;
        #line 188 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3464 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS3465);
        _M0L6_2atmpS3463 = (float)_M0L6_2atmpS3464;
        _M0L2reS1074 = _M0L6_2atmpS3463 - 0x1p-1f;
        _M0L5noiseS3428 = _M0L1sS1047->$6;
        _M0L5noiseS3433 = _M0L1sS1047->$6;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3432
        = _M0MPC15array5Array2atGfE(_M0L5noiseS3433, _M0L1iS1073);
        _M0L6_2atmpS3431 = _M0L6_2atmpS3432 - _M0L2reS1074;
        _M0L6_2atmpS3430 = _M0L6_2atmpS3431 * _M0L2ccS1062;
        _M0L6_2atmpS3429 = _M0L6_2atmpS3430 + _M0L2reS1074;
        #line 189 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L5noiseS3428, _M0L1iS1073, _M0L6_2atmpS3429);
        _M0L5noiseS3462 = _M0L1sS1047->$6;
        #line 190 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3461
        = _M0MPC15array5Array2atGfE(_M0L5noiseS3462, _M0L1iS1073);
        _M0L6_2atmpS3460 = _M0L6_2atmpS3461 * _M0L4betaS1050;
        _M0L2nbS1075
        = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
        Moonbit_object_header(_M0L2nbS1075)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L2nbS1075->$0 = _M0L6_2atmpS3460;
        _M0L3valS3434 = _M0L2nbS1075->$0;
        if (_M0L3valS3434 > 0x1p+0f) {
          _M0L2nbS1075->$0 = 0x1p+0f;
        }
        _M0L3valS3435 = _M0L2nbS1075->$0;
        if (_M0L3valS3435 < 0x0p+0f) {
          _M0L2nbS1075->$0 = 0x0p+0f;
        }
        _M0L6_2atmpS3458 = _M0L2r0S1052 / 0x1p+1f;
        _M0L3valS3459 = _M0L2nbS1075->$0;
        moonbit_decref_cycle_free(_M0L2nbS1075);
        _M0L6_2atmpS3455 = _M0L6_2atmpS3458 * _M0L3valS3459;
        _M0L1rS3457 = _M0L1sS1047->$5;
        #line 197 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3456
        = _M0MPC15array5Array2atGfE(_M0L1rS3457, _M0L1iS1073);
        _M0L6_2atmpS3454 = _M0L6_2atmpS3455 + _M0L6_2atmpS3456;
        _M0L5erateS1076
        = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
        Moonbit_object_header(_M0L5erateS1076)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L5erateS1076->$0 = _M0L6_2atmpS3454;
        _M0L3valS3436 = _M0L5erateS1076->$0;
        if (_M0L3valS3436 < 0x0p+0f) {
          _M0L5erateS1076->$0 = 0x0p+0f;
        }
        _M0L1rS3437 = _M0L1sS1047->$5;
        _M0L1rS3444 = _M0L1sS1047->$5;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L6_2atmpS3439
        = _M0MPC15array5Array2atGfE(_M0L1rS3444, _M0L1iS1073);
        _M0L3valS3443 = _M0L5erateS1076->$0;
        _M0L6_2atmpS3442 = _M0L2r0S1052 - _M0L3valS3443;
        _M0L6_2atmpS3441 = _M0L6_2atmpS3442 / 0x1.9p+8f;
        _M0L6_2atmpS3440 = _M0L6_2atmpS3441 * _M0L2dtS1056;
        _M0L6_2atmpS3438 = _M0L6_2atmpS3439 + _M0L6_2atmpS3440;
        #line 201 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0MPC15array5Array3setGfE(_M0L1rS3437, _M0L1iS1073, _M0L6_2atmpS3438);
        _M0L3valS3453 = _M0L5erateS1076->$0;
        moonbit_decref_cycle_free(_M0L5erateS1076);
        _M0L11exc__lambdaS1077 = _M0L3valS3453 * _M0L2dtS1056;
        _M0L3rngS3452 = _M0L1sS1047->$7;
        #line 203 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
        _M0L1mS1078
        = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3452, _M0L11exc__lambdaS1077);
        if (_M0L1mS1078 > 0) {
          struct _M0TPB5ArrayGfE* _M0L2geS3445 = _M0L1sS1047->$2;
          struct _M0TPB5ArrayGfE* _M0L2geS3450 = _M0L1sS1047->$2;
          float _M0L6_2atmpS3447;
          float _M0L6_2atmpS3449;
          float _M0L6_2atmpS3448;
          float _M0L6_2atmpS3446;
          struct _M0TPB5ArrayGbE* _M0L4fireS3451;
          #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0L6_2atmpS3447
          = _M0MPC15array5Array2atGfE(_M0L2geS3450, _M0L1iS1073);
          _M0L6_2atmpS3449 = (float)_M0L1mS1078;
          _M0L6_2atmpS3448 = _M0L1wS1053 * _M0L6_2atmpS3449;
          _M0L6_2atmpS3446 = _M0L6_2atmpS3447 + _M0L6_2atmpS3448;
          #line 205 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGfE(_M0L2geS3445, _M0L1iS1073, _M0L6_2atmpS3446);
          _M0L4fireS3451 = _M0L1sS1047->$4;
          #line 206 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_balanced.mbt"
          _M0MPC15array5Array3setGbE(_M0L4fireS3451, _M0L1iS1073, 1);
        }
        _M0L6_2atmpS3466 = _M0L1iS1073 + 1;
        _M0L1iS1073 = _M0L6_2atmpS3466;
        continue;
      }
      break;
    }
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0MP26RiantR8snn__mbt7Xoshiro3new(
  uint64_t _M0L4seedS1043
) {
  struct _M0TUmmmmE* _M0L1sS1042;
  uint64_t _M0L6_2atmpS3379;
  struct _M0TUmmmmE* _M0L1tS1044;
  uint64_t _M0L6_2atmpS3375;
  uint64_t _M0L6_2atmpS3376;
  uint64_t _M0L6_2atmpS3377;
  uint64_t _M0L6_2atmpS3378;
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _block_5903;
  #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1sS1042 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L4seedS1043);
  _M0L6_2atmpS3379 = _M0L1sS1042->$3;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1tS1044 = _M0FP26RiantR8snn__mbt10splitmix64(_M0L6_2atmpS3379);
  _M0L6_2atmpS3375 = _M0L1sS1042->$0;
  _M0L6_2atmpS3376 = _M0L1sS1042->$1;
  _M0L6_2atmpS3377 = _M0L1sS1042->$2;
  moonbit_decref_cycle_free(_M0L1sS1042);
  _M0L6_2atmpS3378 = _M0L1tS1044->$0;
  moonbit_decref_cycle_free(_M0L1tS1044);
  _block_5903
  = (struct _M0TP26RiantR8snn__mbt7Xoshiro*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt7Xoshiro));
  Moonbit_object_header(_block_5903)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5903->$0 = _M0L6_2atmpS3375;
  _block_5903->$1 = _M0L6_2atmpS3376;
  _block_5903->$2 = _M0L6_2atmpS3377;
  _block_5903->$3 = _M0L6_2atmpS3378;
  return _block_5903;
}

struct _M0TUmmmmE* _M0FP26RiantR8snn__mbt10splitmix64(
  uint64_t _M0L4seedS1034
) {
  uint64_t _M0L2s1S1033;
  uint64_t _M0L2z1S1035;
  uint64_t _M0L2s2S1036;
  uint64_t _M0L2z2S1037;
  uint64_t _M0L2s3S1038;
  uint64_t _M0L2z3S1039;
  uint64_t _M0L2s4S1040;
  uint64_t _M0L2z4S1041;
  struct _M0TUmmmmE* _block_5904;
  #line 62 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s1S1033 = _M0L4seedS1034 + 11400714819323198485ull;
  #line 64 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z1S1035 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s1S1033);
  _M0L2s2S1036 = _M0L2s1S1033 + 11400714819323198485ull;
  #line 66 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z2S1037 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s2S1036);
  _M0L2s3S1038 = _M0L2s2S1036 + 11400714819323198485ull;
  #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z3S1039 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s3S1038);
  _M0L2s4S1040 = _M0L2s3S1038 + 11400714819323198485ull;
  #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2z4S1041 = _M0FP26RiantR8snn__mbt5mix64(_M0L2s4S1040);
  _block_5904 = (struct _M0TUmmmmE*)moonbit_malloc(sizeof(struct _M0TUmmmmE));
  Moonbit_object_header(_block_5904)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5904->$0 = _M0L2z1S1035;
  _block_5904->$1 = _M0L2z2S1037;
  _block_5904->$2 = _M0L2z3S1039;
  _block_5904->$3 = _M0L2z4S1041;
  return _block_5904;
}

uint64_t _M0FP26RiantR8snn__mbt5mix64(uint64_t _M0L1zS1031) {
  uint64_t _M0L6_2atmpS3374;
  uint64_t _M0L6_2atmpS3373;
  uint64_t _M0L1zS1030;
  uint64_t _M0L6_2atmpS3372;
  uint64_t _M0L6_2atmpS3371;
  uint64_t _M0L1zS1032;
  uint64_t _M0L6_2atmpS3370;
  #line 75 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS3374 = _M0L1zS1031 >> 30;
  _M0L6_2atmpS3373 = _M0L1zS1031 ^ _M0L6_2atmpS3374;
  _M0L1zS1030 = _M0L6_2atmpS3373 * 13787848793156543929ull;
  _M0L6_2atmpS3372 = _M0L1zS1030 >> 27;
  _M0L6_2atmpS3371 = _M0L1zS1030 ^ _M0L6_2atmpS3372;
  _M0L1zS1032 = _M0L6_2atmpS3371 * 10723151780598845931ull;
  _M0L6_2atmpS3370 = _M0L1zS1032 >> 31;
  return _M0L1zS1032 ^ _M0L6_2atmpS3370;
}

int32_t _M0FP26RiantR8snn__mbt25stimulate__current__array(
  struct _M0TP26RiantR8snn__mbt20CurrentStimulusArray* _M0L1sS1018
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3354;
  int32_t _M0L6_2atmpS3353;
  float _M0L12noise__sigmaS3355;
  #line 123 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6activeS3354 = _M0L1sS1018->$1;
  #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6_2atmpS3353 = _M0MPC15array5Array2atGbE(_M0L6activeS3354, 0);
  if (!_M0L6_2atmpS3353) {
    return 0;
  }
  _M0L12noise__sigmaS3355 = _M0L1sS1018->$4;
  if (_M0L12noise__sigmaS3355 <= 0x0p+0f) {
    int32_t _M0L7_2abindS1019 = 0;
    int32_t _M0L7_2abindS1020 = _M0L1sS1018->$3;
    int32_t _M0L1kS1021 = _M0L7_2abindS1019;
    while (1) {
      if (_M0L1kS1021 < _M0L7_2abindS1020) {
        struct _M0TPB5ArrayGfE* _M0L1iS3356 = _M0L1sS1018->$2;
        float _M0L7i__baseS3357 = _M0L1sS1018->$0;
        int32_t _M0L6_2atmpS3358;
        #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3356, _M0L1kS1021, _M0L7i__baseS3357);
        _M0L6_2atmpS3358 = _M0L1kS1021 + 1;
        _M0L1kS1021 = _M0L6_2atmpS3358;
        continue;
      }
      break;
    }
  } else {
    float _M0L5sigmaS1023 = _M0L1sS1018->$4;
    struct _M0TPB8MutLocalGiE* _M0L1kS1024 =
      (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS1024)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS1024->$0 = 0;
    while (1) {
      int32_t _M0L3valS3359 = _M0L1kS1024->$0;
      int32_t _M0L1nS3360 = _M0L1sS1018->$3;
      if (_M0L3valS3359 < _M0L1nS3360) {
        double _M0L2z1S1026;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3369 =
          _M0L1sS1018->$5;
        struct _M0TUddE* _M0L7_2abindS1027;
        double _M0L5_2az1S1028;
        struct _M0TPB5ArrayGfE* _M0L1iS3361;
        int32_t _M0L3valS3362;
        float _M0L7i__baseS3364;
        float _M0L6_2atmpS3366;
        float _M0L6_2atmpS3365;
        float _M0L6_2atmpS3363;
        int32_t _M0L3valS3368;
        int32_t _M0L6_2atmpS3367;
        #line 135 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0L7_2abindS1027
        = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS3369);
        _M0L5_2az1S1028 = _M0L7_2abindS1027->$0;
        moonbit_decref_cycle_free(_M0L7_2abindS1027);
        _M0L2z1S1026 = _M0L5_2az1S1028;
        goto join_1025;
        goto joinlet_5907;
        join_1025:;
        _M0L1iS3361 = _M0L1sS1018->$2;
        _M0L3valS3362 = _M0L1kS1024->$0;
        _M0L7i__baseS3364 = _M0L1sS1018->$0;
        _M0L6_2atmpS3366 = (float)_M0L2z1S1026;
        _M0L6_2atmpS3365 = _M0L5sigmaS1023 * _M0L6_2atmpS3366;
        _M0L6_2atmpS3363 = _M0L7i__baseS3364 + _M0L6_2atmpS3365;
        #line 136 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3361, _M0L3valS3362, _M0L6_2atmpS3363);
        _M0L3valS3368 = _M0L1kS1024->$0;
        _M0L6_2atmpS3367 = _M0L3valS3368 + 1;
        _M0L1kS1024->$0 = _M0L6_2atmpS3367;
        joinlet_5907:;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS1024);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt22stimulate__current__if(
  struct _M0TP26RiantR8snn__mbt17CurrentStimulusIF* _M0L1sS1005
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3338;
  int32_t _M0L6_2atmpS3337;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L3popS1006;
  int32_t _M0L1nS1007;
  float _M0L12noise__sigmaS3339;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6activeS3338 = _M0L1sS1005->$1;
  #line 56 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
  _M0L6_2atmpS3337 = _M0MPC15array5Array2atGbE(_M0L6activeS3338, 0);
  if (!_M0L6_2atmpS3337) {
    return 0;
  }
  _M0L3popS1006 = _M0L1sS1005->$2;
  _M0L1nS1007 = _M0L3popS1006->$2;
  _M0L12noise__sigmaS3339 = _M0L1sS1005->$3;
  if (_M0L12noise__sigmaS3339 <= 0x0p+0f) {
    int32_t _M0L7_2abindS1008 = 0;
    int32_t _M0L1iS1009 = _M0L7_2abindS1008;
    while (1) {
      if (_M0L1iS1009 < _M0L1nS1007) {
        struct _M0TPB5ArrayGfE* _M0L1iS3340 = _M0L3popS1006->$7;
        float _M0L7i__baseS3341 = _M0L1sS1005->$0;
        int32_t _M0L6_2atmpS3342;
        #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3340, _M0L1iS1009, _M0L7i__baseS3341);
        _M0L6_2atmpS3342 = _M0L1iS1009 + 1;
        _M0L1iS1009 = _M0L6_2atmpS3342;
        continue;
      }
      break;
    }
  } else {
    float _M0L5sigmaS1011 = _M0L1sS1005->$3;
    struct _M0TPB8MutLocalGiE* _M0L1kS1012 =
      (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
    Moonbit_object_header(_M0L1kS1012)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _M0L1kS1012->$0 = 0;
    while (1) {
      int32_t _M0L3valS3343 = _M0L1kS1012->$0;
      if (_M0L3valS3343 < _M0L1nS1007) {
        double _M0L2z1S1014;
        struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3352 =
          _M0L1sS1005->$4;
        struct _M0TUddE* _M0L7_2abindS1015;
        double _M0L5_2az1S1016;
        struct _M0TPB5ArrayGfE* _M0L1iS3344;
        int32_t _M0L3valS3345;
        float _M0L7i__baseS3347;
        float _M0L6_2atmpS3349;
        float _M0L6_2atmpS3348;
        float _M0L6_2atmpS3346;
        int32_t _M0L3valS3351;
        int32_t _M0L6_2atmpS3350;
        #line 69 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0L7_2abindS1015
        = _M0FP26RiantR8snn__mbt11box__muller(_M0L3rngS3352);
        _M0L5_2az1S1016 = _M0L7_2abindS1015->$0;
        moonbit_decref_cycle_free(_M0L7_2abindS1015);
        _M0L2z1S1014 = _M0L5_2az1S1016;
        goto join_1013;
        goto joinlet_5910;
        join_1013:;
        _M0L1iS3344 = _M0L3popS1006->$7;
        _M0L3valS3345 = _M0L1kS1012->$0;
        _M0L7i__baseS3347 = _M0L1sS1005->$0;
        _M0L6_2atmpS3349 = (float)_M0L2z1S1014;
        _M0L6_2atmpS3348 = _M0L5sigmaS1011 * _M0L6_2atmpS3349;
        _M0L6_2atmpS3346 = _M0L7i__baseS3347 + _M0L6_2atmpS3348;
        #line 70 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_current.mbt"
        _M0MPC15array5Array3setGfE(_M0L1iS3344, _M0L3valS3345, _M0L6_2atmpS3346);
        _M0L3valS3351 = _M0L1kS1012->$0;
        _M0L6_2atmpS3350 = _M0L3valS3351 + 1;
        _M0L1kS1012->$0 = _M0L6_2atmpS3350;
        joinlet_5910:;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L1kS1012);
      }
      break;
    }
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt13stimulate__if(
  struct _M0TP26RiantR8snn__mbt17PoissonStimulusIF* _M0L1sS994,
  float _M0L4timeS1004,
  float _M0L2dtS996
) {
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3324;
  struct _M0TPB5ArrayGbE* _M0L6activeS3323;
  int32_t _M0L6_2atmpS3322;
  struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3336;
  float _M0L4rateS3335;
  float _M0L6lambdaS995;
  struct _M0TPB5ArrayGiE* _M0L7_2abindS997;
  int32_t _M0L7_2abindS998;
  int32_t* _M0L7_2abindS999;
  int32_t _M0L2__S1000;
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L5paramS3324 = _M0L1sS994->$0;
  _M0L6activeS3323 = _M0L5paramS3324->$2;
  #line 116 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3322 = _M0MPC15array5Array2atGbE(_M0L6activeS3323, 0);
  if (!_M0L6_2atmpS3322) {
    return 0;
  }
  _M0L5paramS3336 = _M0L1sS994->$0;
  _M0L4rateS3335 = _M0L5paramS3336->$0;
  _M0L6lambdaS995 = _M0L4rateS3335 * _M0L2dtS996;
  if (_M0L6lambdaS995 <= 0x0p+0f) {
    return 0;
  }
  _M0L7_2abindS997 = _M0L1sS994->$1;
  _M0L7_2abindS998 = _M0L7_2abindS997->$1;
  _M0L7_2abindS999 = _M0L7_2abindS997->$0;
  moonbit_incref_cycle_free(_M0L7_2abindS999);
  _M0L2__S1000 = 0;
  while (1) {
    if (_M0L2__S1000 < _M0L7_2abindS998) {
      int32_t _M0L1nS1001 = (int32_t)_M0L7_2abindS999[_M0L2__S1000];
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3333 = _M0L1sS994->$3;
      int32_t _M0L1kS1002;
      int32_t _M0L6_2atmpS3334;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
      _M0L1kS1002
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3333, _M0L6lambdaS995);
      if (_M0L1kS1002 > 0) {
        struct _M0TPB5ArrayGfE* _M0L1gS3325 = _M0L1sS994->$2;
        struct _M0TPB5ArrayGfE* _M0L1gS3332 = _M0L1sS994->$2;
        float _M0L6_2atmpS3327;
        struct _M0TP26RiantR8snn__mbt12PoissonFixed* _M0L5paramS3331;
        float _M0L2muS3329;
        float _M0L6_2atmpS3330;
        float _M0L6_2atmpS3328;
        float _M0L6_2atmpS3326;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0L6_2atmpS3327
        = _M0MPC15array5Array2atGfE(_M0L1gS3332, _M0L1nS1001);
        _M0L5paramS3331 = _M0L1sS994->$0;
        _M0L2muS3329 = _M0L5paramS3331->$1;
        _M0L6_2atmpS3330 = (float)_M0L1kS1002;
        _M0L6_2atmpS3328 = _M0L2muS3329 * _M0L6_2atmpS3330;
        _M0L6_2atmpS3326 = _M0L6_2atmpS3327 + _M0L6_2atmpS3328;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
        _M0MPC15array5Array3setGfE(_M0L1gS3325, _M0L1nS1001, _M0L6_2atmpS3326);
      }
      _M0L6_2atmpS3334 = _M0L2__S1000 + 1;
      _M0L2__S1000 = _M0L6_2atmpS3334;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS999);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt16stimulate__layer(
  struct _M0TP26RiantR8snn__mbt20PoissonLayerStimulus* _M0L1sS977,
  float _M0L4timeS975,
  float _M0L2dtS980
) {
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS3321;
  int32_t _M0L6n__preS976;
  struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3320;
  int32_t _M0L7n__postS978;
  struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS3319;
  float _M0L4rateS3318;
  float _M0L6lambdaS979;
  int32_t _M0L7_2abindS981;
  int32_t _M0L1iS982;
  moonbit_string_t _M0L3symS3315;
  struct _M0TPB5ArrayGfE* _M0L9g__targetS984;
  int32_t _M0L7_2abindS985;
  int32_t _M0L1iS986;
  #line 147 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  _M0L5paramS3321 = _M0L1sS977->$0;
  _M0L6n__preS976 = _M0L5paramS3321->$1;
  _M0L4postS3320 = _M0L1sS977->$1;
  _M0L7n__postS978 = _M0L4postS3320->$2;
  _M0L5paramS3319 = _M0L1sS977->$0;
  _M0L4rateS3318 = _M0L5paramS3319->$0;
  _M0L6lambdaS979 = _M0L4rateS3318 * _M0L2dtS980;
  _M0L7_2abindS981 = 0;
  _M0L1iS982 = _M0L7_2abindS981;
  while (1) {
    if (_M0L1iS982 < _M0L6n__preS976) {
      struct _M0TPB5ArrayGbE* _M0L4fireS3300 = _M0L1sS977->$3;
      int32_t _M0L6_2atmpS3301;
      #line 158 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3300, _M0L1iS982, 0);
      _M0L6_2atmpS3301 = _M0L1iS982 + 1;
      _M0L1iS982 = _M0L6_2atmpS3301;
      continue;
    }
    break;
  }
  if (_M0L6lambdaS979 <= 0x0p+0f) {
    return 0;
  }
  _M0L3symS3315 = _M0L1sS977->$2;
  #line 163 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
  if (
    _M0L3symS3315 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS3315)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS3315, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS3315) * 2)
  ) {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3316 = _M0L1sS977->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5538 = _M0L4postS3316->$13;
    moonbit_incref_cycle_free(_M0L8_2afieldS5538);
    _M0L9g__targetS984 = _M0L8_2afieldS5538;
  } else {
    struct _M0TP26RiantR8snn__mbt2IF* _M0L4postS3317 = _M0L1sS977->$1;
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5539 = _M0L4postS3317->$14;
    moonbit_incref_cycle_free(_M0L8_2afieldS5539);
    _M0L9g__targetS984 = _M0L8_2afieldS5539;
  }
  _M0L7_2abindS985 = 0;
  _M0L1iS986 = _M0L7_2abindS985;
  while (1) {
    if (_M0L1iS986 < _M0L6n__preS976) {
      struct _M0TP26RiantR8snn__mbt12PoissonLayer* _M0L5paramS3305 =
        _M0L1sS977->$0;
      struct _M0TPB5ArrayGbE* _M0L6activeS3304 = _M0L5paramS3305->$2;
      int32_t _M0L6_2atmpS3303;
      struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS3314;
      int32_t _M0L1kS989;
      int32_t _M0L6_2atmpS3302;
      moonbit_incref_cycle_free(_M0L6activeS3304);
      #line 165 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0L6_2atmpS3303
      = _M0MPC15array5Array2atGbE(_M0L6activeS3304, _M0L1iS986);
      moonbit_decref_cycle_free(_M0L6activeS3304);
      if (!_M0L6_2atmpS3303) {
        goto join_987;
      }
      _M0L3rngS3314 = _M0L1sS977->$6;
      #line 168 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
      _M0L1kS989
      = _M0FP26RiantR8snn__mbt15sample__poisson(_M0L3rngS3314, _M0L6lambdaS979);
      if (_M0L1kS989 > 0) {
        struct _M0TPB5ArrayGbE* _M0L4fireS3306 = _M0L1sS977->$3;
        int32_t _M0L7_2abindS990;
        int32_t _M0L1jS991;
        #line 170 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
        _M0MPC15array5Array3setGbE(_M0L4fireS3306, _M0L1iS986, 1);
        _M0L7_2abindS990 = 0;
        _M0L1jS991 = _M0L7_2abindS990;
        while (1) {
          if (_M0L1jS991 < _M0L7n__postS978) {
            int32_t _M0L6_2atmpS3312 = _M0L1jS991 * _M0L6n__preS976;
            int32_t _M0L3idxS992 = _M0L6_2atmpS3312 + _M0L1iS986;
            struct _M0TPB5ArrayGbE* _M0L12connectivityS3307 = _M0L1sS977->$5;
            int32_t _M0L6_2atmpS3313;
            #line 173 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
            if (
              _M0MPC15array5Array2atGbE(_M0L12connectivityS3307, _M0L3idxS992)
            ) {
              float _M0L6_2atmpS3309;
              struct _M0TPB5ArrayGfE* _M0L7weightsS3311;
              float _M0L6_2atmpS3310;
              float _M0L6_2atmpS3308;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0L6_2atmpS3309
              = _M0MPC15array5Array2atGfE(_M0L9g__targetS984, _M0L1jS991);
              _M0L7weightsS3311 = _M0L1sS977->$4;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0L6_2atmpS3310
              = _M0MPC15array5Array2atGfE(_M0L7weightsS3311, _M0L3idxS992);
              _M0L6_2atmpS3308 = _M0L6_2atmpS3309 + _M0L6_2atmpS3310;
              #line 174 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson_layer.mbt"
              _M0MPC15array5Array3setGfE(_M0L9g__targetS984, _M0L1jS991, _M0L6_2atmpS3308);
            }
            _M0L6_2atmpS3313 = _M0L1jS991 + 1;
            _M0L1jS991 = _M0L6_2atmpS3313;
            continue;
          }
          break;
        }
      }
      goto join_987;
      goto joinlet_5914;
      join_987:;
      _M0L6_2atmpS3302 = _M0L1iS986 + 1;
      _M0L1iS986 = _M0L6_2atmpS3302;
      continue;
      joinlet_5914:;
    } else {
      moonbit_decref_cycle_free(_M0L9g__targetS984);
    }
    break;
  }
  return 0;
}

struct _M0TUddE* _M0FP26RiantR8snn__mbt11box__muller(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS970
) {
  double _M0L2u1S969;
  double _M0L8u1__safeS971;
  double _M0L2u2S972;
  double _M0L6_2atmpS3299;
  double _M0L6_2atmpS3298;
  double _M0L1rS973;
  double _M0L5thetaS974;
  double _M0L6_2atmpS3297;
  double _M0L6_2atmpS3294;
  double _M0L6_2atmpS3296;
  double _M0L6_2atmpS3295;
  struct _M0TUddE* _block_5916;
  #line 294 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  #line 295 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u1S969 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS970);
  if (_M0L2u1S969 < 0x1.203af9ee75616p-50) {
    _M0L8u1__safeS971 = 0x1.203af9ee75616p-50;
  } else {
    _M0L8u1__safeS971 = _M0L2u1S969;
  }
  #line 298 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L2u2S972 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS970);
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS3299 = _M0FPC14math2ln(_M0L8u1__safeS971);
  _M0L6_2atmpS3298 = -0x1p+1 * _M0L6_2atmpS3299;
  #line 299 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L1rS973 = sqrt(_M0L6_2atmpS3298);
  _M0L5thetaS974 = 0x1.921fb54442d18p+2 * _M0L2u2S972;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS3297 = _M0FPC14math3cos(_M0L5thetaS974);
  _M0L6_2atmpS3294 = _M0L1rS973 * _M0L6_2atmpS3297;
  #line 301 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\neuron_iz.mbt"
  _M0L6_2atmpS3296 = _M0FPC14math3sin(_M0L5thetaS974);
  _M0L6_2atmpS3295 = _M0L1rS973 * _M0L6_2atmpS3296;
  _block_5916 = (struct _M0TUddE*)moonbit_malloc(sizeof(struct _M0TUddE));
  Moonbit_object_header(_block_5916)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5916->$0 = _M0L6_2atmpS3294;
  _block_5916->$1 = _M0L6_2atmpS3295;
  return _block_5916;
}

int32_t _M0FP26RiantR8snn__mbt15sample__poisson(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L3rngS967,
  float _M0L6lambdaS961
) {
  float _M0L6_2atmpS3293;
  float _M0L6_2atmpS3292;
  double _M0L1lS962;
  struct _M0TPB8MutLocalGdE* _M0L1pS963;
  struct _M0TPB8MutLocalGiE* _M0L1kS964;
  float _M0L6_2atmpS3291;
  int32_t _M0L8ten__lamS966;
  int32_t _M0L3capS965;
  int32_t _M0L3valS3290;
  #line 55 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  if (_M0L6lambdaS961 <= 0x0p+0f) {
    return 0;
  }
  _M0L6_2atmpS3293 = -_M0L6lambdaS961;
  #line 59 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L6_2atmpS3292 = _M0FP26RiantR8snn__mbt4expf(_M0L6_2atmpS3293);
  _M0L1lS962 = (double)_M0L6_2atmpS3292;
  _M0L1pS963
  = (struct _M0TPB8MutLocalGdE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGdE));
  Moonbit_object_header(_M0L1pS963)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1pS963->$0 = 0x1p+0;
  _M0L1kS964
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1kS964)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1kS964->$0 = 0;
  _M0L6_2atmpS3291 = _M0L6lambdaS961 * 0x1.4p+3f;
  #line 63 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
  _M0L8ten__lamS966 = _M0MPC15float5Float7to__int(_M0L6_2atmpS3291);
  if (_M0L8ten__lamS966 > 100) {
    _M0L3capS965 = _M0L8ten__lamS966;
  } else {
    _M0L3capS965 = 100;
  }
  while (1) {
    int32_t _M0L3valS3282 = _M0L1kS964->$0;
    int32_t _M0L6_2atmpS3281 = _M0L3valS3282 + 1;
    double _M0L3valS3284;
    double _M0L6_2atmpS3285;
    double _M0L6_2atmpS3283;
    double _M0L3valS3286;
    int32_t _M0L3valS3288;
    _M0L1kS964->$0 = _M0L6_2atmpS3281;
    _M0L3valS3284 = _M0L1pS963->$0;
    #line 68 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_poisson.mbt"
    _M0L6_2atmpS3285 = _M0FP26RiantR8snn__mbt9next__f64(_M0L3rngS967);
    _M0L6_2atmpS3283 = _M0L3valS3284 * _M0L6_2atmpS3285;
    _M0L1pS963->$0 = _M0L6_2atmpS3283;
    _M0L3valS3286 = _M0L1pS963->$0;
    if (_M0L3valS3286 < _M0L1lS962) {
      int32_t _M0L3valS3287;
      moonbit_decref_cycle_free(_M0L1pS963);
      _M0L3valS3287 = _M0L1kS964->$0;
      moonbit_decref_cycle_free(_M0L1kS964);
      return _M0L3valS3287 - 1;
    }
    _M0L3valS3288 = _M0L1kS964->$0;
    if (_M0L3valS3288 > _M0L3capS965) {
      int32_t _M0L3valS3289;
      moonbit_decref_cycle_free(_M0L1pS963);
      _M0L3valS3289 = _M0L1kS964->$0;
      moonbit_decref_cycle_free(_M0L1kS964);
      return _M0L3valS3289 - 1;
    }
    continue;
    break;
  }
  _M0L3valS3290 = _M0L1kS964->$0;
  moonbit_decref_cycle_free(_M0L1kS964);
  return _M0L3valS3290 - 1;
}

double _M0FP26RiantR8snn__mbt9next__f64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS959
) {
  uint64_t _M0L1uS958;
  uint64_t _M0L4bitsS960;
  double _M0L6_2atmpS3280;
  #line 118 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS958 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS959);
  _M0L4bitsS960 = _M0L1uS958 >> 11;
  _M0L6_2atmpS3280 = (double)_M0L4bitsS960;
  return _M0L6_2atmpS3280 * 0x1p-53;
}

int32_t _M0FP26RiantR8snn__mbt20stimulate__spiketime(
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0L1sS952,
  float _M0L1tS954,
  float _M0L1wS956
) {
  struct _M0TPB8MutLocalGiE* _M0L1iS951;
  #line 107 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L1iS951
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS951)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS951->$0 = 0;
  while (1) {
    int32_t _M0L3valS3242 = _M0L1iS951->$0;
    int32_t _M0L1nS3243 = _M0L1sS952->$0;
    if (_M0L3valS3242 < _M0L1nS3243) {
      struct _M0TPB5ArrayGbE* _M0L4fireS3244 = _M0L1sS952->$4;
      int32_t _M0L3valS3245 = _M0L1iS951->$0;
      int32_t _M0L3valS3247;
      int32_t _M0L6_2atmpS3246;
      #line 115 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3244, _M0L3valS3245, 0);
      _M0L3valS3247 = _M0L1iS951->$0;
      _M0L6_2atmpS3246 = _M0L3valS3247 + 1;
      _M0L1iS951->$0 = _M0L6_2atmpS3246;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS951);
    }
    break;
  }
  while (1) {
    struct _M0TPB5ArrayGiE* _M0L11next__indexS3251 = _M0L1sS952->$3;
    int32_t _M0L6_2atmpS3250;
    int32_t _if__result_5920;
    #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
    _M0L6_2atmpS3250 = _M0MPC15array5Array2atGiE(_M0L11next__indexS3251, 0);
    if (_M0L6_2atmpS3250 >= 0) {
      struct _M0TPB5ArrayGfE* _M0L11next__spikeS3249 = _M0L1sS952->$2;
      float _M0L6_2atmpS3248;
      #line 119 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3248 = _M0MPC15array5Array2atGfE(_M0L11next__spikeS3249, 0);
      _if__result_5920 = _M0L6_2atmpS3248 <= _M0L1tS954;
    } else {
      _if__result_5920 = 0;
    }
    if (_if__result_5920) {
      struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS3279 =
        _M0L1sS952->$1;
      struct _M0TPB5ArrayGiE* _M0L7neuronsS3276 = _M0L5paramS3279->$1;
      struct _M0TPB5ArrayGiE* _M0L11next__indexS3278 = _M0L1sS952->$3;
      int32_t _M0L6_2atmpS3277;
      int32_t _M0L1jS955;
      struct _M0TPB5ArrayGbE* _M0L4fireS3252;
      struct _M0TPB5ArrayGfE* _M0L1gS3253;
      struct _M0TPB5ArrayGfE* _M0L1gS3256;
      float _M0L6_2atmpS3255;
      float _M0L6_2atmpS3254;
      struct _M0TPB5ArrayGiE* _M0L11next__indexS3262;
      int32_t _M0L6_2atmpS3261;
      int32_t _M0L6_2atmpS3257;
      struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS3260;
      struct _M0TPB5ArrayGfE* _M0L10spiketimesS3259;
      int32_t _M0L6_2atmpS3258;
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3277 = _M0MPC15array5Array2atGiE(_M0L11next__indexS3278, 0);
      #line 120 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L1jS955
      = _M0MPC15array5Array2atGiE(_M0L7neuronsS3276, _M0L6_2atmpS3277);
      _M0L4fireS3252 = _M0L1sS952->$4;
      #line 121 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGbE(_M0L4fireS3252, _M0L1jS955, 1);
      _M0L1gS3253 = _M0L1sS952->$5;
      _M0L1gS3256 = _M0L1sS952->$5;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3255 = _M0MPC15array5Array2atGfE(_M0L1gS3256, _M0L1jS955);
      _M0L6_2atmpS3254 = _M0L6_2atmpS3255 + _M0L1wS956;
      #line 122 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGfE(_M0L1gS3253, _M0L1jS955, _M0L6_2atmpS3254);
      _M0L11next__indexS3262 = _M0L1sS952->$3;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3261 = _M0MPC15array5Array2atGiE(_M0L11next__indexS3262, 0);
      _M0L6_2atmpS3257 = _M0L6_2atmpS3261 + 1;
      _M0L5paramS3260 = _M0L1sS952->$1;
      _M0L10spiketimesS3259 = _M0L5paramS3260->$0;
      #line 124 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6_2atmpS3258 = _M0MPC15array5Array6lengthGfE(_M0L10spiketimesS3259);
      if (_M0L6_2atmpS3257 < _M0L6_2atmpS3258) {
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3263 = _M0L1sS952->$3;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3266 = _M0L1sS952->$3;
        int32_t _M0L6_2atmpS3265;
        int32_t _M0L6_2atmpS3264;
        struct _M0TPB5ArrayGfE* _M0L11next__spikeS3267;
        struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS3272;
        struct _M0TPB5ArrayGfE* _M0L10spiketimesS3269;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3271;
        int32_t _M0L6_2atmpS3270;
        float _M0L6_2atmpS3268;
        #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS3265
        = _M0MPC15array5Array2atGiE(_M0L11next__indexS3266, 0);
        _M0L6_2atmpS3264 = _M0L6_2atmpS3265 + 1;
        #line 125 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGiE(_M0L11next__indexS3263, 0, _M0L6_2atmpS3264);
        _M0L11next__spikeS3267 = _M0L1sS952->$2;
        _M0L5paramS3272 = _M0L1sS952->$1;
        _M0L10spiketimesS3269 = _M0L5paramS3272->$0;
        _M0L11next__indexS3271 = _M0L1sS952->$3;
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS3270
        = _M0MPC15array5Array2atGiE(_M0L11next__indexS3271, 0);
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0L6_2atmpS3268
        = _M0MPC15array5Array2atGfE(_M0L10spiketimesS3269, _M0L6_2atmpS3270);
        #line 126 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGfE(_M0L11next__spikeS3267, 0, _M0L6_2atmpS3268);
      } else {
        struct _M0TPB5ArrayGfE* _M0L11next__spikeS3273 = _M0L1sS952->$2;
        float _M0L6_2atmpS3274 = 0x0p+0f / (float)MOONBIT_ZERO;
        struct _M0TPB5ArrayGiE* _M0L11next__indexS3275;
        #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGfE(_M0L11next__spikeS3273, 0, _M0L6_2atmpS3274);
        _M0L11next__indexS3275 = _M0L1sS952->$3;
        #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
        _M0MPC15array5Array3setGiE(_M0L11next__indexS3275, 0, -1);
      }
      continue;
    }
    break;
  }
  return 0;
}

struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _M0MP26RiantR8snn__mbt17SpikeTimeStimulus3new(
  struct _M0TP26RiantR8snn__mbt2IF* _M0L6e__popS942,
  moonbit_string_t _M0L3symS948,
  struct _M0TPB5ArrayGfE* _M0L10spiketimesS944,
  struct _M0TPB5ArrayGiE* _M0L7neuronsS945
) {
  int32_t _M0L1nS941;
  struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0L5paramS943;
  struct _M0TPB5ArrayGbE* _M0L4fireS946;
  struct _M0TPB5ArrayGfE* _M0L1gS947;
  struct _M0TPB5ArrayGfE* _M0L10spiketimesS3236;
  int32_t _M0L6_2atmpS3235;
  struct _M0TPB5ArrayGfE* _M0L11next__spikeS949;
  struct _M0TPB5ArrayGfE* _M0L10spiketimesS3232;
  int32_t _M0L6_2atmpS3231;
  struct _M0TPB5ArrayGiE* _M0L11next__indexS950;
  struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus* _block_5921;
  #line 79 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L1nS941 = _M0L6e__popS942->$2;
  #line 86 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L5paramS943
  = _M0MP26RiantR8snn__mbt18SpikeTimeParameter3new(_M0L10spiketimesS944, _M0L7neuronsS945);
  #line 87 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L4fireS946 = _M0MPC15array5Array4makeGbE(_M0L1nS941, 0);
  #line 88 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  if (
    _M0L3symS948 == (moonbit_string_t)moonbit_string_literal_9.data
    || Moonbit_array_length(_M0L3symS948)
       == Moonbit_array_length((moonbit_string_t)moonbit_string_literal_9.data)
       && 0
          == memcmp(_M0L3symS948, (moonbit_string_t)moonbit_string_literal_9.data, Moonbit_array_length(_M0L3symS948) * 2)
  ) {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5540 = _M0L6e__popS942->$13;
    moonbit_incref_cycle_free(_M0L8_2afieldS5540);
    _M0L1gS947 = _M0L8_2afieldS5540;
  } else {
    struct _M0TPB5ArrayGfE* _M0L8_2afieldS5541 = _M0L6e__popS942->$14;
    moonbit_incref_cycle_free(_M0L8_2afieldS5541);
    _M0L1gS947 = _M0L8_2afieldS5541;
  }
  _M0L10spiketimesS3236 = _M0L5paramS943->$0;
  moonbit_incref_cycle_free(_M0L10spiketimesS3236);
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L6_2atmpS3235 = _M0MPC15array5Array6lengthGfE(_M0L10spiketimesS3236);
  moonbit_decref_cycle_free(_M0L10spiketimesS3236);
  if (_M0L6_2atmpS3235 > 0) {
    struct _M0TPB5ArrayGfE* _M0L10spiketimesS3239 = _M0L5paramS943->$0;
    float _M0L6_2atmpS3238;
    float* _M0L6_2atmpS3237;
    moonbit_incref_cycle_free(_M0L10spiketimesS3239);
    #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
    _M0L6_2atmpS3238 = _M0MPC15array5Array2atGfE(_M0L10spiketimesS3239, 0);
    moonbit_decref_cycle_free(_M0L10spiketimesS3239);
    _M0L6_2atmpS3237 = (float*)moonbit_make_float_array_raw(1);
    _M0L6_2atmpS3237[0] = _M0L6_2atmpS3238;
    _M0L11next__spikeS949
    = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
    Moonbit_object_header(_M0L11next__spikeS949)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
    _M0L11next__spikeS949->$0 = _M0L6_2atmpS3237;
    _M0L11next__spikeS949->$1 = 1;
  } else {
    float _M0L6_2atmpS3241 = 0x0p+0f / (float)MOONBIT_ZERO;
    float* _M0L6_2atmpS3240 = (float*)moonbit_make_float_array_raw(1);
    _M0L6_2atmpS3240[0] = _M0L6_2atmpS3241;
    _M0L11next__spikeS949
    = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
    Moonbit_object_header(_M0L11next__spikeS949)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
    _M0L11next__spikeS949->$0 = _M0L6_2atmpS3240;
    _M0L11next__spikeS949->$1 = 1;
  }
  _M0L10spiketimesS3232 = _M0L5paramS943->$0;
  moonbit_incref_cycle_free(_M0L10spiketimesS3232);
  #line 94 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L6_2atmpS3231 = _M0MPC15array5Array6lengthGfE(_M0L10spiketimesS3232);
  moonbit_decref_cycle_free(_M0L10spiketimesS3232);
  if (_M0L6_2atmpS3231 > 0) {
    int32_t* _M0L6_2atmpS3233 = (int32_t*)moonbit_make_int32_array_raw(1);
    _M0L6_2atmpS3233[0] = 0;
    _M0L11next__indexS950
    = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
    Moonbit_object_header(_M0L11next__indexS950)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 66, 0);
    _M0L11next__indexS950->$0 = _M0L6_2atmpS3233;
    _M0L11next__indexS950->$1 = 1;
  } else {
    int32_t* _M0L6_2atmpS3234 = (int32_t*)moonbit_make_int32_array_raw(1);
    _M0L6_2atmpS3234[0] = -1;
    _M0L11next__indexS950
    = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
    Moonbit_object_header(_M0L11next__indexS950)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 66, 0);
    _M0L11next__indexS950->$0 = _M0L6_2atmpS3234;
    _M0L11next__indexS950->$1 = 1;
  }
  _block_5921
  = (struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt17SpikeTimeStimulus));
  Moonbit_object_header(_block_5921)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 69, 0);
  _block_5921->$0 = _M0L1nS941;
  _block_5921->$1 = _M0L5paramS943;
  _block_5921->$2 = _M0L11next__spikeS949;
  _block_5921->$3 = _M0L11next__indexS950;
  _block_5921->$4 = _M0L4fireS946;
  _block_5921->$5 = _M0L1gS947;
  return _block_5921;
}

struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _M0MP26RiantR8snn__mbt18SpikeTimeParameter3new(
  struct _M0TPB5ArrayGfE* _M0L10spiketimesS931,
  struct _M0TPB5ArrayGiE* _M0L7neuronsS934
) {
  int32_t _M0L1nS930;
  struct _M0TPB5ArrayGfE* _M0L9sorted__tS932;
  struct _M0TPB5ArrayGiE* _M0L9sorted__nS933;
  struct _M0TPB8MutLocalGiE* _M0L1iS935;
  struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter* _block_5925;
  #line 33 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  #line 38 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L1nS930 = _M0MPC15array5Array6lengthGfE(_M0L10spiketimesS931);
  #line 39 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L9sorted__tS932 = _M0MPC15array5Array4copyGfE(_M0L10spiketimesS931);
  #line 40 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
  _M0L9sorted__nS933 = _M0MPC15array5Array4copyGiE(_M0L7neuronsS934);
  _M0L1iS935
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1iS935)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1iS935->$0 = 1;
  while (1) {
    int32_t _M0L3valS3209 = _M0L1iS935->$0;
    if (_M0L3valS3209 < _M0L1nS930) {
      int32_t _M0L3valS3230 = _M0L1iS935->$0;
      float _M0L6key__tS936;
      int32_t _M0L3valS3229;
      int32_t _M0L6key__nS937;
      int32_t _M0L3valS3228;
      struct _M0TPB8MutLocalGiE* _M0L1jS938;
      int32_t _M0L3valS3224;
      int32_t _M0L3valS3225;
      int32_t _M0L3valS3227;
      int32_t _M0L6_2atmpS3226;
      #line 43 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6key__tS936
      = _M0MPC15array5Array2atGfE(_M0L9sorted__tS932, _M0L3valS3230);
      _M0L3valS3229 = _M0L1iS935->$0;
      #line 44 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0L6key__nS937
      = _M0MPC15array5Array2atGiE(_M0L9sorted__nS933, _M0L3valS3229);
      _M0L3valS3228 = _M0L1iS935->$0;
      _M0L1jS938
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1jS938)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1jS938->$0 = _M0L3valS3228;
      while (1) {
        int32_t _M0L3valS3213 = _M0L1jS938->$0;
        int32_t _if__result_5924;
        if (_M0L3valS3213 > 0) {
          int32_t _M0L3valS3212 = _M0L1jS938->$0;
          int32_t _M0L6_2atmpS3211 = _M0L3valS3212 - 1;
          float _M0L6_2atmpS3210;
          #line 46 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
          _M0L6_2atmpS3210
          = _M0MPC15array5Array2atGfE(_M0L9sorted__tS932, _M0L6_2atmpS3211);
          _if__result_5924 = _M0L6_2atmpS3210 > _M0L6key__tS936;
        } else {
          _if__result_5924 = 0;
        }
        if (_if__result_5924) {
          int32_t _M0L3valS3214 = _M0L1jS938->$0;
          int32_t _M0L3valS3217 = _M0L1jS938->$0;
          int32_t _M0L6_2atmpS3216 = _M0L3valS3217 - 1;
          float _M0L6_2atmpS3215;
          int32_t _M0L3valS3218;
          int32_t _M0L3valS3221;
          int32_t _M0L6_2atmpS3220;
          int32_t _M0L6_2atmpS3219;
          int32_t _M0L3valS3223;
          int32_t _M0L6_2atmpS3222;
          #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
          _M0L6_2atmpS3215
          = _M0MPC15array5Array2atGfE(_M0L9sorted__tS932, _M0L6_2atmpS3216);
          #line 47 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
          _M0MPC15array5Array3setGfE(_M0L9sorted__tS932, _M0L3valS3214, _M0L6_2atmpS3215);
          _M0L3valS3218 = _M0L1jS938->$0;
          _M0L3valS3221 = _M0L1jS938->$0;
          _M0L6_2atmpS3220 = _M0L3valS3221 - 1;
          #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
          _M0L6_2atmpS3219
          = _M0MPC15array5Array2atGiE(_M0L9sorted__nS933, _M0L6_2atmpS3220);
          #line 48 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
          _M0MPC15array5Array3setGiE(_M0L9sorted__nS933, _M0L3valS3218, _M0L6_2atmpS3219);
          _M0L3valS3223 = _M0L1jS938->$0;
          _M0L6_2atmpS3222 = _M0L3valS3223 - 1;
          _M0L1jS938->$0 = _M0L6_2atmpS3222;
          continue;
        }
        break;
      }
      _M0L3valS3224 = _M0L1jS938->$0;
      #line 51 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGfE(_M0L9sorted__tS932, _M0L3valS3224, _M0L6key__tS936);
      _M0L3valS3225 = _M0L1jS938->$0;
      moonbit_decref_cycle_free(_M0L1jS938);
      #line 52 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stimulus_timed.mbt"
      _M0MPC15array5Array3setGiE(_M0L9sorted__nS933, _M0L3valS3225, _M0L6key__nS937);
      _M0L3valS3227 = _M0L1iS935->$0;
      _M0L6_2atmpS3226 = _M0L3valS3227 + 1;
      _M0L1iS935->$0 = _M0L6_2atmpS3226;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1iS935);
    }
    break;
  }
  _block_5925
  = (struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt18SpikeTimeParameter));
  Moonbit_object_header(_block_5925)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 76, 0);
  _block_5925->$0 = _M0L9sorted__tS932;
  _block_5925->$1 = _M0L9sorted__nS933;
  return _block_5925;
}

int32_t _M0FP26RiantR8snn__mbt28markram__stp__step__timestep(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS917,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS915,
  struct _M0TP26RiantR8snn__mbt27MarkramSTPParameterTimestep* _M0L5paramS919,
  float _M0L6t__nowS914,
  float _M0L2dtS924
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3122;
  int32_t _M0L6_2atmpS3121;
  int32_t _if__result_5926;
  int32_t _M0L6n__preS916;
  struct _M0TPB5ArrayGfE* _M0L3rhoS3124;
  int32_t _M0L6_2atmpS3123;
  float _M0L11u__baselineS918;
  float _M0L6tau__fS3208;
  float _M0L11inv__tau__fS920;
  float _M0L6tau__dS3207;
  float _M0L11inv__tau__dS921;
  struct _M0TPB8MutLocalGiE* _M0L1jS922;
  #line 473 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS3122 = _M0L4varsS915->$6;
  #line 482 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3121 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3122);
  if (_M0L6_2atmpS3121 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3120 = _M0L4varsS915->$6;
    int32_t _M0L6_2atmpS3119;
    #line 482 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS3119 = _M0MPC15array5Array2atGbE(_M0L6activeS3120, 0);
    _if__result_5926 = !_M0L6_2atmpS3119;
  } else {
    _if__result_5926 = 0;
  }
  if (_if__result_5926) {
    return 0;
  }
  _M0L6n__preS916 = _M0L4varsS915->$0;
  _M0L3rhoS3124 = _M0L3synS917->$6;
  #line 487 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3123 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS3124);
  if (_M0L6_2atmpS3123 == 0) {
    return 0;
  }
  _M0L11u__baselineS918 = _M0L5paramS919->$0;
  _M0L6tau__fS3208 = _M0L5paramS919->$1;
  _M0L11inv__tau__fS920 = 0x1p+0f / _M0L6tau__fS3208;
  _M0L6tau__dS3207 = _M0L5paramS919->$2;
  _M0L11inv__tau__dS921 = 0x1p+0f / _M0L6tau__dS3207;
  _M0L1jS922
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS922)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS922->$0 = 0;
  while (1) {
    int32_t _M0L3valS3125 = _M0L1jS922->$0;
    if (_M0L3valS3125 < _M0L6n__preS916) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3128 = _M0L3synS917->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3126 = _M0L3preS3128->$5;
      int32_t _M0L3valS3127 = _M0L1jS922->$0;
      int32_t _M0L3valS3155;
      int32_t _M0L6_2atmpS3154;
      #line 496 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3126, _M0L3valS3127)) {
        struct _M0TPB5ArrayGfE* _M0L1uS3129 = _M0L4varsS915->$2;
        int32_t _M0L3valS3130 = _M0L1jS922->$0;
        struct _M0TPB5ArrayGfE* _M0L1uS3138 = _M0L4varsS915->$2;
        int32_t _M0L3valS3139 = _M0L1jS922->$0;
        float _M0L6_2atmpS3132;
        struct _M0TPB5ArrayGfE* _M0L1uS3136;
        int32_t _M0L3valS3137;
        float _M0L6_2atmpS3135;
        float _M0L6_2atmpS3134;
        float _M0L6_2atmpS3133;
        float _M0L6_2atmpS3131;
        struct _M0TPB5ArrayGfE* _M0L1xS3140;
        int32_t _M0L3valS3141;
        struct _M0TPB5ArrayGfE* _M0L1xS3152;
        int32_t _M0L3valS3153;
        float _M0L6_2atmpS3143;
        struct _M0TPB5ArrayGfE* _M0L1uS3150;
        int32_t _M0L3valS3151;
        float _M0L6_2atmpS3149;
        float _M0L6_2atmpS3145;
        struct _M0TPB5ArrayGfE* _M0L1xS3147;
        int32_t _M0L3valS3148;
        float _M0L6_2atmpS3146;
        float _M0L6_2atmpS3144;
        float _M0L6_2atmpS3142;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3132
        = _M0MPC15array5Array2atGfE(_M0L1uS3138, _M0L3valS3139);
        _M0L1uS3136 = _M0L4varsS915->$2;
        _M0L3valS3137 = _M0L1jS922->$0;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3135
        = _M0MPC15array5Array2atGfE(_M0L1uS3136, _M0L3valS3137);
        _M0L6_2atmpS3134 = 0x1p+0f - _M0L6_2atmpS3135;
        _M0L6_2atmpS3133 = _M0L11u__baselineS918 * _M0L6_2atmpS3134;
        _M0L6_2atmpS3131 = _M0L6_2atmpS3132 + _M0L6_2atmpS3133;
        #line 497 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3129, _M0L3valS3130, _M0L6_2atmpS3131);
        _M0L1xS3140 = _M0L4varsS915->$3;
        _M0L3valS3141 = _M0L1jS922->$0;
        _M0L1xS3152 = _M0L4varsS915->$3;
        _M0L3valS3153 = _M0L1jS922->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3143
        = _M0MPC15array5Array2atGfE(_M0L1xS3152, _M0L3valS3153);
        _M0L1uS3150 = _M0L4varsS915->$2;
        _M0L3valS3151 = _M0L1jS922->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3149
        = _M0MPC15array5Array2atGfE(_M0L1uS3150, _M0L3valS3151);
        _M0L6_2atmpS3145 = -_M0L6_2atmpS3149;
        _M0L1xS3147 = _M0L4varsS915->$3;
        _M0L3valS3148 = _M0L1jS922->$0;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3146
        = _M0MPC15array5Array2atGfE(_M0L1xS3147, _M0L3valS3148);
        _M0L6_2atmpS3144 = _M0L6_2atmpS3145 * _M0L6_2atmpS3146;
        _M0L6_2atmpS3142 = _M0L6_2atmpS3143 + _M0L6_2atmpS3144;
        #line 498 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3140, _M0L3valS3141, _M0L6_2atmpS3142);
      }
      _M0L3valS3155 = _M0L1jS922->$0;
      _M0L6_2atmpS3154 = _M0L3valS3155 + 1;
      _M0L1jS922->$0 = _M0L6_2atmpS3154;
      continue;
    }
    break;
  }
  _M0L1jS922->$0 = 0;
  while (1) {
    int32_t _M0L3valS3156 = _M0L1jS922->$0;
    if (_M0L3valS3156 < _M0L6n__preS916) {
      struct _M0TPB5ArrayGfE* _M0L1uS3157 = _M0L4varsS915->$2;
      int32_t _M0L3valS3158 = _M0L1jS922->$0;
      struct _M0TPB5ArrayGfE* _M0L1uS3167 = _M0L4varsS915->$2;
      int32_t _M0L3valS3168 = _M0L1jS922->$0;
      float _M0L6_2atmpS3160;
      struct _M0TPB5ArrayGfE* _M0L1uS3165;
      int32_t _M0L3valS3166;
      float _M0L6_2atmpS3164;
      float _M0L6_2atmpS3163;
      float _M0L6_2atmpS3162;
      float _M0L6_2atmpS3161;
      float _M0L6_2atmpS3159;
      struct _M0TPB5ArrayGfE* _M0L1xS3169;
      int32_t _M0L3valS3170;
      struct _M0TPB5ArrayGfE* _M0L1xS3179;
      int32_t _M0L3valS3180;
      float _M0L6_2atmpS3172;
      struct _M0TPB5ArrayGfE* _M0L1xS3177;
      int32_t _M0L3valS3178;
      float _M0L6_2atmpS3176;
      float _M0L6_2atmpS3175;
      float _M0L6_2atmpS3174;
      float _M0L6_2atmpS3173;
      float _M0L6_2atmpS3171;
      struct _M0TPB5ArrayGfE* _M0L8rho__preS3181;
      int32_t _M0L3valS3182;
      struct _M0TPB5ArrayGfE* _M0L1uS3188;
      int32_t _M0L3valS3189;
      float _M0L6_2atmpS3184;
      struct _M0TPB5ArrayGfE* _M0L1xS3186;
      int32_t _M0L3valS3187;
      float _M0L6_2atmpS3185;
      float _M0L6_2atmpS3183;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3206;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS3204;
      int32_t _M0L3valS3205;
      int32_t _M0L5startS925;
      struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3203;
      struct _M0TPB5ArrayGiE* _M0L6rowptrS3200;
      int32_t _M0L3valS3202;
      int32_t _M0L6_2atmpS3201;
      int32_t _M0L3endS926;
      struct _M0TPB8MutLocalGiE* _M0L1sS927;
      int32_t _M0L3valS3199;
      int32_t _M0L6_2atmpS3198;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3160
      = _M0MPC15array5Array2atGfE(_M0L1uS3167, _M0L3valS3168);
      _M0L1uS3165 = _M0L4varsS915->$2;
      _M0L3valS3166 = _M0L1jS922->$0;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3164
      = _M0MPC15array5Array2atGfE(_M0L1uS3165, _M0L3valS3166);
      _M0L6_2atmpS3163 = _M0L11u__baselineS918 - _M0L6_2atmpS3164;
      _M0L6_2atmpS3162 = _M0L2dtS924 * _M0L6_2atmpS3163;
      _M0L6_2atmpS3161 = _M0L6_2atmpS3162 * _M0L11inv__tau__fS920;
      _M0L6_2atmpS3159 = _M0L6_2atmpS3160 + _M0L6_2atmpS3161;
      #line 505 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1uS3157, _M0L3valS3158, _M0L6_2atmpS3159);
      _M0L1xS3169 = _M0L4varsS915->$3;
      _M0L3valS3170 = _M0L1jS922->$0;
      _M0L1xS3179 = _M0L4varsS915->$3;
      _M0L3valS3180 = _M0L1jS922->$0;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3172
      = _M0MPC15array5Array2atGfE(_M0L1xS3179, _M0L3valS3180);
      _M0L1xS3177 = _M0L4varsS915->$3;
      _M0L3valS3178 = _M0L1jS922->$0;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3176
      = _M0MPC15array5Array2atGfE(_M0L1xS3177, _M0L3valS3178);
      _M0L6_2atmpS3175 = 0x1p+0f - _M0L6_2atmpS3176;
      _M0L6_2atmpS3174 = _M0L2dtS924 * _M0L6_2atmpS3175;
      _M0L6_2atmpS3173 = _M0L6_2atmpS3174 * _M0L11inv__tau__dS921;
      _M0L6_2atmpS3171 = _M0L6_2atmpS3172 + _M0L6_2atmpS3173;
      #line 506 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L1xS3169, _M0L3valS3170, _M0L6_2atmpS3171);
      _M0L8rho__preS3181 = _M0L4varsS915->$4;
      _M0L3valS3182 = _M0L1jS922->$0;
      _M0L1uS3188 = _M0L4varsS915->$2;
      _M0L3valS3189 = _M0L1jS922->$0;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3184
      = _M0MPC15array5Array2atGfE(_M0L1uS3188, _M0L3valS3189);
      _M0L1xS3186 = _M0L4varsS915->$3;
      _M0L3valS3187 = _M0L1jS922->$0;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L6_2atmpS3185
      = _M0MPC15array5Array2atGfE(_M0L1xS3186, _M0L3valS3187);
      _M0L6_2atmpS3183 = _M0L6_2atmpS3184 * _M0L6_2atmpS3185;
      #line 507 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0MPC15array5Array3setGfE(_M0L8rho__preS3181, _M0L3valS3182, _M0L6_2atmpS3183);
      _M0L6matrixS3206 = _M0L3synS917->$4;
      _M0L6rowptrS3204 = _M0L6matrixS3206->$2;
      _M0L3valS3205 = _M0L1jS922->$0;
      #line 509 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L5startS925
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS3204, _M0L3valS3205);
      _M0L6matrixS3203 = _M0L3synS917->$4;
      _M0L6rowptrS3200 = _M0L6matrixS3203->$2;
      _M0L3valS3202 = _M0L1jS922->$0;
      _M0L6_2atmpS3201 = _M0L3valS3202 + 1;
      #line 510 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      _M0L3endS926
      = _M0MPC15array5Array2atGiE(_M0L6rowptrS3200, _M0L6_2atmpS3201);
      _M0L1sS927
      = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
      Moonbit_object_header(_M0L1sS927)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L1sS927->$0 = _M0L5startS925;
      while (1) {
        int32_t _M0L3valS3190 = _M0L1sS927->$0;
        if (_M0L3valS3190 < _M0L3endS926) {
          struct _M0TPB5ArrayGfE* _M0L3rhoS3191 = _M0L3synS917->$6;
          int32_t _M0L3valS3192 = _M0L1sS927->$0;
          struct _M0TPB5ArrayGfE* _M0L8rho__preS3194 = _M0L4varsS915->$4;
          int32_t _M0L3valS3195 = _M0L1jS922->$0;
          float _M0L6_2atmpS3193;
          int32_t _M0L3valS3197;
          int32_t _M0L6_2atmpS3196;
          #line 513 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
          _M0L6_2atmpS3193
          = _M0MPC15array5Array2atGfE(_M0L8rho__preS3194, _M0L3valS3195);
          #line 513 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
          _M0MPC15array5Array3setGfE(_M0L3rhoS3191, _M0L3valS3192, _M0L6_2atmpS3193);
          _M0L3valS3197 = _M0L1sS927->$0;
          _M0L6_2atmpS3196 = _M0L3valS3197 + 1;
          _M0L1sS927->$0 = _M0L6_2atmpS3196;
          continue;
        } else {
          moonbit_decref_cycle_free(_M0L1sS927);
        }
        break;
      }
      _M0L3valS3199 = _M0L1jS922->$0;
      _M0L6_2atmpS3198 = _M0L3valS3199 + 1;
      _M0L1jS922->$0 = _M0L6_2atmpS3198;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS922);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt23markram__stp__step__het(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS898,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS896,
  struct _M0TP26RiantR8snn__mbt22MarkramSTPParameterHet* _M0L5paramS901,
  float _M0L6t__nowS905
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS3030;
  int32_t _M0L6_2atmpS3029;
  int32_t _if__result_5930;
  int32_t _M0L6n__preS897;
  struct _M0TPB5ArrayGfE* _M0L3rhoS3032;
  int32_t _M0L6_2atmpS3031;
  struct _M0TPB8MutLocalGiE* _M0L1jS899;
  #line 347 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS3030 = _M0L4varsS896->$6;
  #line 353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3029 = _M0MPC15array5Array6lengthGbE(_M0L6activeS3030);
  if (_M0L6_2atmpS3029 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS3028 = _M0L4varsS896->$6;
    int32_t _M0L6_2atmpS3027;
    #line 353 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS3027 = _M0MPC15array5Array2atGbE(_M0L6activeS3028, 0);
    _if__result_5930 = !_M0L6_2atmpS3027;
  } else {
    _if__result_5930 = 0;
  }
  if (_if__result_5930) {
    return 0;
  }
  _M0L6n__preS897 = _M0L4varsS896->$0;
  _M0L3rhoS3032 = _M0L3synS898->$6;
  #line 357 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS3031 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS3032);
  if (_M0L6_2atmpS3031 == 0) {
    return 0;
  }
  _M0L1jS899
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS899)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS899->$0 = 0;
  while (1) {
    int32_t _M0L3valS3033 = _M0L1jS899->$0;
    if (_M0L3valS3033 < _M0L6n__preS897) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS3036 = _M0L3synS898->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS3034 = _M0L3preS3036->$5;
      int32_t _M0L3valS3035 = _M0L1jS899->$0;
      int32_t _M0L3valS3118;
      int32_t _M0L6_2atmpS3117;
      #line 362 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS3034, _M0L3valS3035)) {
        struct _M0TPB5ArrayGfE* _M0L6tau__dS3115 = _M0L5paramS901->$0;
        int32_t _M0L3valS3116 = _M0L1jS899->$0;
        float _M0L9tau__d__jS900;
        struct _M0TPB5ArrayGfE* _M0L6tau__fS3113;
        int32_t _M0L3valS3114;
        float _M0L9tau__f__jS902;
        struct _M0TPB5ArrayGfE* _M0L1uS3111;
        int32_t _M0L3valS3112;
        float _M0L14u__baseline__jS903;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS3109;
        int32_t _M0L3valS3110;
        float _M0L6_2atmpS3108;
        float _M0L7dt__preS904;
        float _M0L7dt__preS906;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS3037;
        int32_t _M0L3valS3038;
        float _M0L6_2atmpS3107;
        float _M0L6arg__fS907;
        struct _M0TPB5ArrayGfE* _M0L1uS3039;
        int32_t _M0L3valS3040;
        struct _M0TPB5ArrayGfE* _M0L1uS3046;
        int32_t _M0L3valS3047;
        float _M0L6_2atmpS3045;
        float _M0L6_2atmpS3043;
        float _M0L6_2atmpS3044;
        float _M0L6_2atmpS3042;
        float _M0L6_2atmpS3041;
        float _M0L6_2atmpS3106;
        float _M0L6arg__dS908;
        struct _M0TPB5ArrayGfE* _M0L1xS3048;
        int32_t _M0L3valS3049;
        struct _M0TPB5ArrayGfE* _M0L1xS3055;
        int32_t _M0L3valS3056;
        float _M0L6_2atmpS3054;
        float _M0L6_2atmpS3052;
        float _M0L6_2atmpS3053;
        float _M0L6_2atmpS3051;
        float _M0L6_2atmpS3050;
        struct _M0TPB5ArrayGfE* _M0L8rho__preS3057;
        int32_t _M0L3valS3058;
        struct _M0TPB5ArrayGfE* _M0L1uS3064;
        int32_t _M0L3valS3065;
        float _M0L6_2atmpS3060;
        struct _M0TPB5ArrayGfE* _M0L1xS3062;
        int32_t _M0L3valS3063;
        float _M0L6_2atmpS3061;
        float _M0L6_2atmpS3059;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3105;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3103;
        int32_t _M0L3valS3104;
        int32_t _M0L5startS909;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3102;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3099;
        int32_t _M0L3valS3101;
        int32_t _M0L6_2atmpS3100;
        int32_t _M0L3endS910;
        struct _M0TPB8MutLocalGiE* _M0L1sS911;
        struct _M0TPB5ArrayGfE* _M0L1uS3074;
        int32_t _M0L3valS3075;
        struct _M0TPB5ArrayGfE* _M0L1uS3083;
        int32_t _M0L3valS3084;
        float _M0L6_2atmpS3077;
        struct _M0TPB5ArrayGfE* _M0L1uS3081;
        int32_t _M0L3valS3082;
        float _M0L6_2atmpS3080;
        float _M0L6_2atmpS3079;
        float _M0L6_2atmpS3078;
        float _M0L6_2atmpS3076;
        struct _M0TPB5ArrayGfE* _M0L1xS3085;
        int32_t _M0L3valS3086;
        struct _M0TPB5ArrayGfE* _M0L1xS3097;
        int32_t _M0L3valS3098;
        float _M0L6_2atmpS3088;
        struct _M0TPB5ArrayGfE* _M0L1uS3095;
        int32_t _M0L3valS3096;
        float _M0L6_2atmpS3094;
        float _M0L6_2atmpS3090;
        struct _M0TPB5ArrayGfE* _M0L1xS3092;
        int32_t _M0L3valS3093;
        float _M0L6_2atmpS3091;
        float _M0L6_2atmpS3089;
        float _M0L6_2atmpS3087;
        #line 363 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L9tau__d__jS900
        = _M0MPC15array5Array2atGfE(_M0L6tau__dS3115, _M0L3valS3116);
        _M0L6tau__fS3113 = _M0L5paramS901->$1;
        _M0L3valS3114 = _M0L1jS899->$0;
        #line 364 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L9tau__f__jS902
        = _M0MPC15array5Array2atGfE(_M0L6tau__fS3113, _M0L3valS3114);
        _M0L1uS3111 = _M0L5paramS901->$2;
        _M0L3valS3112 = _M0L1jS899->$0;
        #line 365 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L14u__baseline__jS903
        = _M0MPC15array5Array2atGfE(_M0L1uS3111, _M0L3valS3112);
        _M0L11last__spikeS3109 = _M0L4varsS896->$5;
        _M0L3valS3110 = _M0L1jS899->$0;
        #line 366 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3108
        = _M0MPC15array5Array2atGfE(_M0L11last__spikeS3109, _M0L3valS3110);
        _M0L7dt__preS904 = _M0L6t__nowS905 - _M0L6_2atmpS3108;
        if (_M0L7dt__preS904 < 0x0p+0f) {
          _M0L7dt__preS906 = 0x0p+0f;
        } else {
          _M0L7dt__preS906 = _M0L7dt__preS904;
        }
        _M0L11last__spikeS3037 = _M0L4varsS896->$5;
        _M0L3valS3038 = _M0L1jS899->$0;
        #line 368 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L11last__spikeS3037, _M0L3valS3038, _M0L6t__nowS905);
        _M0L6_2atmpS3107 = -_M0L7dt__preS906;
        _M0L6arg__fS907 = _M0L6_2atmpS3107 / _M0L9tau__f__jS902;
        _M0L1uS3039 = _M0L4varsS896->$2;
        _M0L3valS3040 = _M0L1jS899->$0;
        _M0L1uS3046 = _M0L4varsS896->$2;
        _M0L3valS3047 = _M0L1jS899->$0;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3045
        = _M0MPC15array5Array2atGfE(_M0L1uS3046, _M0L3valS3047);
        _M0L6_2atmpS3043 = _M0L14u__baseline__jS903 - _M0L6_2atmpS3045;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3044 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__fS907);
        _M0L6_2atmpS3042 = _M0L6_2atmpS3043 * _M0L6_2atmpS3044;
        _M0L6_2atmpS3041 = _M0L14u__baseline__jS903 - _M0L6_2atmpS3042;
        #line 370 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3039, _M0L3valS3040, _M0L6_2atmpS3041);
        _M0L6_2atmpS3106 = -_M0L7dt__preS906;
        _M0L6arg__dS908 = _M0L6_2atmpS3106 / _M0L9tau__d__jS900;
        _M0L1xS3048 = _M0L4varsS896->$3;
        _M0L3valS3049 = _M0L1jS899->$0;
        _M0L1xS3055 = _M0L4varsS896->$3;
        _M0L3valS3056 = _M0L1jS899->$0;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3054
        = _M0MPC15array5Array2atGfE(_M0L1xS3055, _M0L3valS3056);
        _M0L6_2atmpS3052 = 0x1p+0f - _M0L6_2atmpS3054;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3053 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__dS908);
        _M0L6_2atmpS3051 = _M0L6_2atmpS3052 * _M0L6_2atmpS3053;
        _M0L6_2atmpS3050 = 0x1p+0f - _M0L6_2atmpS3051;
        #line 372 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3048, _M0L3valS3049, _M0L6_2atmpS3050);
        _M0L8rho__preS3057 = _M0L4varsS896->$4;
        _M0L3valS3058 = _M0L1jS899->$0;
        _M0L1uS3064 = _M0L4varsS896->$2;
        _M0L3valS3065 = _M0L1jS899->$0;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3060
        = _M0MPC15array5Array2atGfE(_M0L1uS3064, _M0L3valS3065);
        _M0L1xS3062 = _M0L4varsS896->$3;
        _M0L3valS3063 = _M0L1jS899->$0;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3061
        = _M0MPC15array5Array2atGfE(_M0L1xS3062, _M0L3valS3063);
        _M0L6_2atmpS3059 = _M0L6_2atmpS3060 * _M0L6_2atmpS3061;
        #line 373 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L8rho__preS3057, _M0L3valS3058, _M0L6_2atmpS3059);
        _M0L6matrixS3105 = _M0L3synS898->$4;
        _M0L6rowptrS3103 = _M0L6matrixS3105->$2;
        _M0L3valS3104 = _M0L1jS899->$0;
        #line 374 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L5startS909
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3103, _M0L3valS3104);
        _M0L6matrixS3102 = _M0L3synS898->$4;
        _M0L6rowptrS3099 = _M0L6matrixS3102->$2;
        _M0L3valS3101 = _M0L1jS899->$0;
        _M0L6_2atmpS3100 = _M0L3valS3101 + 1;
        #line 375 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L3endS910
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3099, _M0L6_2atmpS3100);
        _M0L1sS911
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS911)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS911->$0 = _M0L5startS909;
        while (1) {
          int32_t _M0L3valS3066 = _M0L1sS911->$0;
          if (_M0L3valS3066 < _M0L3endS910) {
            struct _M0TPB5ArrayGfE* _M0L3rhoS3067 = _M0L3synS898->$6;
            int32_t _M0L3valS3068 = _M0L1sS911->$0;
            struct _M0TPB5ArrayGfE* _M0L8rho__preS3070 = _M0L4varsS896->$4;
            int32_t _M0L3valS3071 = _M0L1jS899->$0;
            float _M0L6_2atmpS3069;
            int32_t _M0L3valS3073;
            int32_t _M0L6_2atmpS3072;
            #line 378 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0L6_2atmpS3069
            = _M0MPC15array5Array2atGfE(_M0L8rho__preS3070, _M0L3valS3071);
            #line 378 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0MPC15array5Array3setGfE(_M0L3rhoS3067, _M0L3valS3068, _M0L6_2atmpS3069);
            _M0L3valS3073 = _M0L1sS911->$0;
            _M0L6_2atmpS3072 = _M0L3valS3073 + 1;
            _M0L1sS911->$0 = _M0L6_2atmpS3072;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS911);
          }
          break;
        }
        _M0L1uS3074 = _M0L4varsS896->$2;
        _M0L3valS3075 = _M0L1jS899->$0;
        _M0L1uS3083 = _M0L4varsS896->$2;
        _M0L3valS3084 = _M0L1jS899->$0;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3077
        = _M0MPC15array5Array2atGfE(_M0L1uS3083, _M0L3valS3084);
        _M0L1uS3081 = _M0L4varsS896->$2;
        _M0L3valS3082 = _M0L1jS899->$0;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3080
        = _M0MPC15array5Array2atGfE(_M0L1uS3081, _M0L3valS3082);
        _M0L6_2atmpS3079 = 0x1p+0f - _M0L6_2atmpS3080;
        _M0L6_2atmpS3078 = _M0L14u__baseline__jS903 * _M0L6_2atmpS3079;
        _M0L6_2atmpS3076 = _M0L6_2atmpS3077 + _M0L6_2atmpS3078;
        #line 381 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS3074, _M0L3valS3075, _M0L6_2atmpS3076);
        _M0L1xS3085 = _M0L4varsS896->$3;
        _M0L3valS3086 = _M0L1jS899->$0;
        _M0L1xS3097 = _M0L4varsS896->$3;
        _M0L3valS3098 = _M0L1jS899->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3088
        = _M0MPC15array5Array2atGfE(_M0L1xS3097, _M0L3valS3098);
        _M0L1uS3095 = _M0L4varsS896->$2;
        _M0L3valS3096 = _M0L1jS899->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3094
        = _M0MPC15array5Array2atGfE(_M0L1uS3095, _M0L3valS3096);
        _M0L6_2atmpS3090 = -_M0L6_2atmpS3094;
        _M0L1xS3092 = _M0L4varsS896->$3;
        _M0L3valS3093 = _M0L1jS899->$0;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3091
        = _M0MPC15array5Array2atGfE(_M0L1xS3092, _M0L3valS3093);
        _M0L6_2atmpS3089 = _M0L6_2atmpS3090 * _M0L6_2atmpS3091;
        _M0L6_2atmpS3087 = _M0L6_2atmpS3088 + _M0L6_2atmpS3089;
        #line 382 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS3085, _M0L3valS3086, _M0L6_2atmpS3087);
      }
      _M0L3valS3118 = _M0L1jS899->$0;
      _M0L6_2atmpS3117 = _M0L3valS3118 + 1;
      _M0L1jS899->$0 = _M0L6_2atmpS3117;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS899);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt18markram__stp__step(
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L3synS880,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPVariables* _M0L4varsS878,
  struct _M0TP26RiantR8snn__mbt19MarkramSTPParameter* _M0L5paramS882,
  float _M0L6t__nowS887
) {
  struct _M0TPB5ArrayGbE* _M0L6activeS2944;
  int32_t _M0L6_2atmpS2943;
  int32_t _if__result_5933;
  int32_t _M0L6n__preS879;
  struct _M0TPB5ArrayGfE* _M0L3rhoS2946;
  int32_t _M0L6_2atmpS2945;
  float _M0L6tau__fS881;
  float _M0L6tau__dS883;
  float _M0L11u__baselineS884;
  struct _M0TPB8MutLocalGiE* _M0L1jS885;
  #line 202 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6activeS2944 = _M0L4varsS878->$6;
  #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS2943 = _M0MPC15array5Array6lengthGbE(_M0L6activeS2944);
  if (_M0L6_2atmpS2943 > 0) {
    struct _M0TPB5ArrayGbE* _M0L6activeS2942 = _M0L4varsS878->$6;
    int32_t _M0L6_2atmpS2941;
    #line 209 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
    _M0L6_2atmpS2941 = _M0MPC15array5Array2atGbE(_M0L6activeS2942, 0);
    _if__result_5933 = !_M0L6_2atmpS2941;
  } else {
    _if__result_5933 = 0;
  }
  if (_if__result_5933) {
    return 0;
  }
  _M0L6n__preS879 = _M0L4varsS878->$0;
  _M0L3rhoS2946 = _M0L3synS880->$6;
  #line 214 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
  _M0L6_2atmpS2945 = _M0MPC15array5Array6lengthGfE(_M0L3rhoS2946);
  if (_M0L6_2atmpS2945 == 0) {
    return 0;
  }
  _M0L6tau__fS881 = _M0L5paramS882->$1;
  _M0L6tau__dS883 = _M0L5paramS882->$0;
  _M0L11u__baselineS884 = _M0L5paramS882->$2;
  _M0L1jS885
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L1jS885)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L1jS885->$0 = 0;
  while (1) {
    int32_t _M0L3valS2947 = _M0L1jS885->$0;
    if (_M0L3valS2947 < _M0L6n__preS879) {
      struct _M0TP26RiantR8snn__mbt2IF* _M0L3preS2950 = _M0L3synS880->$0;
      struct _M0TPB5ArrayGbE* _M0L4fireS2948 = _M0L3preS2950->$5;
      int32_t _M0L3valS2949 = _M0L1jS885->$0;
      int32_t _M0L3valS3026;
      int32_t _M0L6_2atmpS3025;
      #line 222 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
      if (_M0MPC15array5Array2atGbE(_M0L4fireS2948, _M0L3valS2949)) {
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS3023 = _M0L4varsS878->$5;
        int32_t _M0L3valS3024 = _M0L1jS885->$0;
        float _M0L6_2atmpS3022;
        float _M0L7dt__preS886;
        float _M0L7dt__preS888;
        struct _M0TPB5ArrayGfE* _M0L11last__spikeS2951;
        int32_t _M0L3valS2952;
        float _M0L6_2atmpS3021;
        float _M0L6arg__fS889;
        struct _M0TPB5ArrayGfE* _M0L1uS2953;
        int32_t _M0L3valS2954;
        struct _M0TPB5ArrayGfE* _M0L1uS2960;
        int32_t _M0L3valS2961;
        float _M0L6_2atmpS2959;
        float _M0L6_2atmpS2957;
        float _M0L6_2atmpS2958;
        float _M0L6_2atmpS2956;
        float _M0L6_2atmpS2955;
        float _M0L6_2atmpS3020;
        float _M0L6arg__dS890;
        struct _M0TPB5ArrayGfE* _M0L1xS2962;
        int32_t _M0L3valS2963;
        struct _M0TPB5ArrayGfE* _M0L1xS2969;
        int32_t _M0L3valS2970;
        float _M0L6_2atmpS2968;
        float _M0L6_2atmpS2966;
        float _M0L6_2atmpS2967;
        float _M0L6_2atmpS2965;
        float _M0L6_2atmpS2964;
        struct _M0TPB5ArrayGfE* _M0L8rho__preS2971;
        int32_t _M0L3valS2972;
        struct _M0TPB5ArrayGfE* _M0L1uS2978;
        int32_t _M0L3valS2979;
        float _M0L6_2atmpS2974;
        struct _M0TPB5ArrayGfE* _M0L1xS2976;
        int32_t _M0L3valS2977;
        float _M0L6_2atmpS2975;
        float _M0L6_2atmpS2973;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3019;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3017;
        int32_t _M0L3valS3018;
        int32_t _M0L5startS891;
        struct _M0TP26RiantR8snn__mbt15SparseMatrixCSR* _M0L6matrixS3016;
        struct _M0TPB5ArrayGiE* _M0L6rowptrS3013;
        int32_t _M0L3valS3015;
        int32_t _M0L6_2atmpS3014;
        int32_t _M0L3endS892;
        struct _M0TPB8MutLocalGiE* _M0L1sS893;
        struct _M0TPB5ArrayGfE* _M0L1uS2988;
        int32_t _M0L3valS2989;
        struct _M0TPB5ArrayGfE* _M0L1uS2997;
        int32_t _M0L3valS2998;
        float _M0L6_2atmpS2991;
        struct _M0TPB5ArrayGfE* _M0L1uS2995;
        int32_t _M0L3valS2996;
        float _M0L6_2atmpS2994;
        float _M0L6_2atmpS2993;
        float _M0L6_2atmpS2992;
        float _M0L6_2atmpS2990;
        struct _M0TPB5ArrayGfE* _M0L1xS2999;
        int32_t _M0L3valS3000;
        struct _M0TPB5ArrayGfE* _M0L1xS3011;
        int32_t _M0L3valS3012;
        float _M0L6_2atmpS3002;
        struct _M0TPB5ArrayGfE* _M0L1uS3009;
        int32_t _M0L3valS3010;
        float _M0L6_2atmpS3008;
        float _M0L6_2atmpS3004;
        struct _M0TPB5ArrayGfE* _M0L1xS3006;
        int32_t _M0L3valS3007;
        float _M0L6_2atmpS3005;
        float _M0L6_2atmpS3003;
        float _M0L6_2atmpS3001;
        #line 224 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3022
        = _M0MPC15array5Array2atGfE(_M0L11last__spikeS3023, _M0L3valS3024);
        _M0L7dt__preS886 = _M0L6t__nowS887 - _M0L6_2atmpS3022;
        if (_M0L7dt__preS886 < 0x0p+0f) {
          _M0L7dt__preS888 = 0x0p+0f;
        } else {
          _M0L7dt__preS888 = _M0L7dt__preS886;
        }
        _M0L11last__spikeS2951 = _M0L4varsS878->$5;
        _M0L3valS2952 = _M0L1jS885->$0;
        #line 226 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L11last__spikeS2951, _M0L3valS2952, _M0L6t__nowS887);
        _M0L6_2atmpS3021 = -_M0L7dt__preS888;
        _M0L6arg__fS889 = _M0L6_2atmpS3021 / _M0L6tau__fS881;
        _M0L1uS2953 = _M0L4varsS878->$2;
        _M0L3valS2954 = _M0L1jS885->$0;
        _M0L1uS2960 = _M0L4varsS878->$2;
        _M0L3valS2961 = _M0L1jS885->$0;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2959
        = _M0MPC15array5Array2atGfE(_M0L1uS2960, _M0L3valS2961);
        _M0L6_2atmpS2957 = _M0L11u__baselineS884 - _M0L6_2atmpS2959;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2958 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__fS889);
        _M0L6_2atmpS2956 = _M0L6_2atmpS2957 * _M0L6_2atmpS2958;
        _M0L6_2atmpS2955 = _M0L11u__baselineS884 - _M0L6_2atmpS2956;
        #line 229 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS2953, _M0L3valS2954, _M0L6_2atmpS2955);
        _M0L6_2atmpS3020 = -_M0L7dt__preS888;
        _M0L6arg__dS890 = _M0L6_2atmpS3020 / _M0L6tau__dS883;
        _M0L1xS2962 = _M0L4varsS878->$3;
        _M0L3valS2963 = _M0L1jS885->$0;
        _M0L1xS2969 = _M0L4varsS878->$3;
        _M0L3valS2970 = _M0L1jS885->$0;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2968
        = _M0MPC15array5Array2atGfE(_M0L1xS2969, _M0L3valS2970);
        _M0L6_2atmpS2966 = 0x1p+0f - _M0L6_2atmpS2968;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2967 = _M0FP26RiantR8snn__mbt4expf(_M0L6arg__dS890);
        _M0L6_2atmpS2965 = _M0L6_2atmpS2966 * _M0L6_2atmpS2967;
        _M0L6_2atmpS2964 = 0x1p+0f - _M0L6_2atmpS2965;
        #line 232 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS2962, _M0L3valS2963, _M0L6_2atmpS2964);
        _M0L8rho__preS2971 = _M0L4varsS878->$4;
        _M0L3valS2972 = _M0L1jS885->$0;
        _M0L1uS2978 = _M0L4varsS878->$2;
        _M0L3valS2979 = _M0L1jS885->$0;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2974
        = _M0MPC15array5Array2atGfE(_M0L1uS2978, _M0L3valS2979);
        _M0L1xS2976 = _M0L4varsS878->$3;
        _M0L3valS2977 = _M0L1jS885->$0;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2975
        = _M0MPC15array5Array2atGfE(_M0L1xS2976, _M0L3valS2977);
        _M0L6_2atmpS2973 = _M0L6_2atmpS2974 * _M0L6_2atmpS2975;
        #line 234 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L8rho__preS2971, _M0L3valS2972, _M0L6_2atmpS2973);
        _M0L6matrixS3019 = _M0L3synS880->$4;
        _M0L6rowptrS3017 = _M0L6matrixS3019->$2;
        _M0L3valS3018 = _M0L1jS885->$0;
        #line 236 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L5startS891
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3017, _M0L3valS3018);
        _M0L6matrixS3016 = _M0L3synS880->$4;
        _M0L6rowptrS3013 = _M0L6matrixS3016->$2;
        _M0L3valS3015 = _M0L1jS885->$0;
        _M0L6_2atmpS3014 = _M0L3valS3015 + 1;
        #line 237 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L3endS892
        = _M0MPC15array5Array2atGiE(_M0L6rowptrS3013, _M0L6_2atmpS3014);
        _M0L1sS893
        = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
        Moonbit_object_header(_M0L1sS893)->meta
        = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
        _M0L1sS893->$0 = _M0L5startS891;
        while (1) {
          int32_t _M0L3valS2980 = _M0L1sS893->$0;
          if (_M0L3valS2980 < _M0L3endS892) {
            struct _M0TPB5ArrayGfE* _M0L3rhoS2981 = _M0L3synS880->$6;
            int32_t _M0L3valS2982 = _M0L1sS893->$0;
            struct _M0TPB5ArrayGfE* _M0L8rho__preS2984 = _M0L4varsS878->$4;
            int32_t _M0L3valS2985 = _M0L1jS885->$0;
            float _M0L6_2atmpS2983;
            int32_t _M0L3valS2987;
            int32_t _M0L6_2atmpS2986;
            #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0L6_2atmpS2983
            = _M0MPC15array5Array2atGfE(_M0L8rho__preS2984, _M0L3valS2985);
            #line 240 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
            _M0MPC15array5Array3setGfE(_M0L3rhoS2981, _M0L3valS2982, _M0L6_2atmpS2983);
            _M0L3valS2987 = _M0L1sS893->$0;
            _M0L6_2atmpS2986 = _M0L3valS2987 + 1;
            _M0L1sS893->$0 = _M0L6_2atmpS2986;
            continue;
          } else {
            moonbit_decref_cycle_free(_M0L1sS893);
          }
          break;
        }
        _M0L1uS2988 = _M0L4varsS878->$2;
        _M0L3valS2989 = _M0L1jS885->$0;
        _M0L1uS2997 = _M0L4varsS878->$2;
        _M0L3valS2998 = _M0L1jS885->$0;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2991
        = _M0MPC15array5Array2atGfE(_M0L1uS2997, _M0L3valS2998);
        _M0L1uS2995 = _M0L4varsS878->$2;
        _M0L3valS2996 = _M0L1jS885->$0;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS2994
        = _M0MPC15array5Array2atGfE(_M0L1uS2995, _M0L3valS2996);
        _M0L6_2atmpS2993 = 0x1p+0f - _M0L6_2atmpS2994;
        _M0L6_2atmpS2992 = _M0L11u__baselineS884 * _M0L6_2atmpS2993;
        _M0L6_2atmpS2990 = _M0L6_2atmpS2991 + _M0L6_2atmpS2992;
        #line 244 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1uS2988, _M0L3valS2989, _M0L6_2atmpS2990);
        _M0L1xS2999 = _M0L4varsS878->$3;
        _M0L3valS3000 = _M0L1jS885->$0;
        _M0L1xS3011 = _M0L4varsS878->$3;
        _M0L3valS3012 = _M0L1jS885->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3002
        = _M0MPC15array5Array2atGfE(_M0L1xS3011, _M0L3valS3012);
        _M0L1uS3009 = _M0L4varsS878->$2;
        _M0L3valS3010 = _M0L1jS885->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3008
        = _M0MPC15array5Array2atGfE(_M0L1uS3009, _M0L3valS3010);
        _M0L6_2atmpS3004 = -_M0L6_2atmpS3008;
        _M0L1xS3006 = _M0L4varsS878->$3;
        _M0L3valS3007 = _M0L1jS885->$0;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0L6_2atmpS3005
        = _M0MPC15array5Array2atGfE(_M0L1xS3006, _M0L3valS3007);
        _M0L6_2atmpS3003 = _M0L6_2atmpS3004 * _M0L6_2atmpS3005;
        _M0L6_2atmpS3001 = _M0L6_2atmpS3002 + _M0L6_2atmpS3003;
        #line 245 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\stp.mbt"
        _M0MPC15array5Array3setGfE(_M0L1xS2999, _M0L3valS3000, _M0L6_2atmpS3001);
      }
      _M0L3valS3026 = _M0L1jS885->$0;
      _M0L6_2atmpS3025 = _M0L3valS3026 + 1;
      _M0L1jS885->$0 = _M0L6_2atmpS3025;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L1jS885);
    }
    break;
  }
  return 0;
}

int32_t _M0FP26RiantR8snn__mbt12update__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS876,
  float _M0L2dtS877
) {
  struct _M0TPB5ArrayGfE* _M0L1tS2933;
  struct _M0TPB5ArrayGfE* _M0L1tS2936;
  float _M0L6_2atmpS2935;
  float _M0L6_2atmpS2934;
  struct _M0TPB5ArrayGiE* _M0L2ttS2937;
  struct _M0TPB5ArrayGiE* _M0L2ttS2940;
  int32_t _M0L6_2atmpS2939;
  int32_t _M0L6_2atmpS2938;
  #line 89 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS2933 = _M0L1tS876->$0;
  _M0L1tS2936 = _M0L1tS876->$0;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2935 = _M0MPC15array5Array2atGfE(_M0L1tS2936, 0);
  _M0L6_2atmpS2934 = _M0L6_2atmpS2935 + _M0L2dtS877;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGfE(_M0L1tS2933, 0, _M0L6_2atmpS2934);
  _M0L2ttS2937 = _M0L1tS876->$1;
  _M0L2ttS2940 = _M0L1tS876->$1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2939 = _M0MPC15array5Array2atGiE(_M0L2ttS2940, 0);
  _M0L6_2atmpS2938 = _M0L6_2atmpS2939 + 1;
  #line 91 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0MPC15array5Array3setGiE(_M0L2ttS2937, 0, _M0L6_2atmpS2938);
  return 0;
}

float _M0FP26RiantR8snn__mbt9get__time(
  struct _M0TP26RiantR8snn__mbt4Time* _M0L1tS875
) {
  struct _M0TPB5ArrayGfE* _M0L1tS2932;
  #line 49 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L1tS2932 = _M0L1tS875->$0;
  #line 50 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  return _M0MPC15array5Array2atGfE(_M0L1tS2932, 0);
}

struct _M0TP26RiantR8snn__mbt4Time* _M0MP26RiantR8snn__mbt4Time3new() {
  float* _M0L6_2atmpS2931;
  struct _M0TPB5ArrayGfE* _M0L6_2atmpS2928;
  int32_t* _M0L6_2atmpS2930;
  struct _M0TPB5ArrayGiE* _M0L6_2atmpS2929;
  struct _M0TP26RiantR8snn__mbt4Time* _block_5936;
  #line 34 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\time.mbt"
  _M0L6_2atmpS2931 = (float*)moonbit_make_float_array_raw(1);
  _M0L6_2atmpS2931[0] = 0x0p+0f;
  _M0L6_2atmpS2928
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_M0L6_2atmpS2928)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
  _M0L6_2atmpS2928->$0 = _M0L6_2atmpS2931;
  _M0L6_2atmpS2928->$1 = 1;
  _M0L6_2atmpS2930 = (int32_t*)moonbit_make_int32_array_raw(1);
  _M0L6_2atmpS2930[0] = 0;
  _M0L6_2atmpS2929
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_M0L6_2atmpS2929)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 66, 0);
  _M0L6_2atmpS2929->$0 = _M0L6_2atmpS2930;
  _M0L6_2atmpS2929->$1 = 1;
  _block_5936
  = (struct _M0TP26RiantR8snn__mbt4Time*)moonbit_malloc(sizeof(struct _M0TP26RiantR8snn__mbt4Time));
  Moonbit_object_header(_block_5936)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 80, 0);
  _block_5936->$0 = _M0L6_2atmpS2928;
  _block_5936->$1 = _M0L6_2atmpS2929;
  _block_5936->$2 = 0x1p-3f;
  return _block_5936;
}

float _M0FP26RiantR8snn__mbt9next__f32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS873
) {
  uint32_t _M0L1uS872;
  uint32_t _M0L4bitsS874;
  double _M0L6_2atmpS2927;
  double _M0L6_2atmpS2926;
  #line 128 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 129 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS872 = _M0FP26RiantR8snn__mbt9next__u32(_M0L1rS873);
  _M0L4bitsS874 = _M0L1uS872 >> 8;
  _M0L6_2atmpS2927 = (double)_M0L4bitsS874;
  _M0L6_2atmpS2926 = _M0L6_2atmpS2927 * 0x1p-24;
  return (float)_M0L6_2atmpS2926;
}

uint32_t _M0FP26RiantR8snn__mbt9next__u32(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS871
) {
  uint64_t _M0L1uS870;
  uint64_t _M0L6_2atmpS2925;
  #line 110 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  #line 111 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L1uS870 = _M0FP26RiantR8snn__mbt9next__u64(_M0L1rS871);
  _M0L6_2atmpS2925 = _M0L1uS870 >> 32;
  return (uint32_t)_M0L6_2atmpS2925;
}

uint64_t _M0FP26RiantR8snn__mbt9next__u64(
  struct _M0TP26RiantR8snn__mbt7Xoshiro* _M0L1rS863
) {
  uint64_t _M0L2s0S862;
  uint64_t _M0L2s1S864;
  uint64_t _M0L2s2S865;
  uint64_t _M0L2s3S866;
  uint64_t _M0L3tmpS867;
  uint64_t _M0L6_2atmpS2924;
  uint64_t _M0L3resS868;
  uint64_t _M0L1tS869;
  uint64_t _M0L6_2atmpS2914;
  uint64_t _M0L6_2atmpS2915;
  uint64_t _M0L2s2S2917;
  uint64_t _M0L6_2atmpS2916;
  uint64_t _M0L2s3S2919;
  uint64_t _M0L6_2atmpS2918;
  uint64_t _M0L2s2S2921;
  uint64_t _M0L6_2atmpS2920;
  uint64_t _M0L2s3S2923;
  uint64_t _M0L6_2atmpS2922;
  #line 90 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L2s0S862 = _M0L1rS863->$0;
  _M0L2s1S864 = _M0L1rS863->$1;
  _M0L2s2S865 = _M0L1rS863->$2;
  _M0L2s3S866 = _M0L1rS863->$3;
  _M0L3tmpS867 = _M0L2s0S862 + _M0L2s3S866;
  #line 96 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2924 = _M0FP26RiantR8snn__mbt4rotl(_M0L3tmpS867, 23);
  _M0L3resS868 = _M0L6_2atmpS2924 + _M0L2s0S862;
  _M0L1tS869 = _M0L2s1S864 << 17;
  _M0L6_2atmpS2914 = _M0L2s2S865 ^ _M0L2s0S862;
  _M0L1rS863->$2 = _M0L6_2atmpS2914;
  _M0L6_2atmpS2915 = _M0L2s3S866 ^ _M0L2s1S864;
  _M0L1rS863->$3 = _M0L6_2atmpS2915;
  _M0L2s2S2917 = _M0L1rS863->$2;
  _M0L6_2atmpS2916 = _M0L2s1S864 ^ _M0L2s2S2917;
  _M0L1rS863->$1 = _M0L6_2atmpS2916;
  _M0L2s3S2919 = _M0L1rS863->$3;
  _M0L6_2atmpS2918 = _M0L2s0S862 ^ _M0L2s3S2919;
  _M0L1rS863->$0 = _M0L6_2atmpS2918;
  _M0L2s2S2921 = _M0L1rS863->$2;
  _M0L6_2atmpS2920 = _M0L2s2S2921 ^ _M0L1tS869;
  _M0L1rS863->$2 = _M0L6_2atmpS2920;
  _M0L2s3S2923 = _M0L1rS863->$3;
  #line 103 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2922 = _M0FP26RiantR8snn__mbt4rotl(_M0L2s3S2923, 45);
  _M0L1rS863->$3 = _M0L6_2atmpS2922;
  return _M0L3resS868;
}

uint64_t _M0FP26RiantR8snn__mbt4rotl(uint64_t _M0L1xS860, int32_t _M0L1kS861) {
  uint64_t _M0L6_2atmpS2911;
  int32_t _M0L6_2atmpS2913;
  uint64_t _M0L6_2atmpS2912;
  #line 83 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\rng.mbt"
  _M0L6_2atmpS2911 = _M0L1xS860 << (_M0L1kS861 & 63);
  _M0L6_2atmpS2913 = 64 - _M0L1kS861;
  _M0L6_2atmpS2912 = _M0L1xS860 >> (_M0L6_2atmpS2913 & 63);
  return _M0L6_2atmpS2911 | _M0L6_2atmpS2912;
}

int32_t _M0MP26RiantR8snn__mbt7Monitor13count__spikes(
  struct _M0TP26RiantR8snn__mbt7Monitor* _M0L1mS853
) {
  struct _M0TPB5ArrayGfE* _M0L4dataS2910;
  int32_t _M0L1nS852;
  struct _M0TPB8MutLocalGiE* _M0L5countS854;
  struct _M0TPB8MutLocalGfE* _M0L4prevS855;
  int32_t _M0L7_2abindS856;
  int32_t _M0L1iS857;
  int32_t _result_5938;
  #line 17 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L4dataS2910 = _M0L1mS853->$2;
  #line 18 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
  _M0L1nS852 = _M0MPC15array5Array6lengthGfE(_M0L4dataS2910);
  if (_M0L1nS852 == 0) {
    return 0;
  }
  _M0L5countS854
  = (struct _M0TPB8MutLocalGiE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGiE));
  Moonbit_object_header(_M0L5countS854)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L5countS854->$0 = 0;
  _M0L4prevS855
  = (struct _M0TPB8MutLocalGfE*)moonbit_malloc(sizeof(struct _M0TPB8MutLocalGfE));
  Moonbit_object_header(_M0L4prevS855)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L4prevS855->$0 = 0x0p+0f;
  _M0L7_2abindS856 = 0;
  _M0L1iS857 = _M0L7_2abindS856;
  while (1) {
    if (_M0L1iS857 < _M0L1nS852) {
      struct _M0TPB5ArrayGfE* _M0L4dataS2908 = _M0L1mS853->$2;
      float _M0L3curS858;
      float _M0L3valS2905;
      int32_t _M0L6_2atmpS2909;
      #line 25 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\vecplot.mbt"
      _M0L3curS858 = _M0MPC15array5Array2atGfE(_M0L4dataS2908, _M0L1iS857);
      _M0L3valS2905 = _M0L4prevS855->$0;
      if (_M0L3valS2905 < 0x1p-1f && _M0L3curS858 >= 0x1p-1f) {
        int32_t _M0L3valS2907 = _M0L5countS854->$0;
        int32_t _M0L6_2atmpS2906 = _M0L3valS2907 + 1;
        _M0L5countS854->$0 = _M0L6_2atmpS2906;
      }
      _M0L4prevS855->$0 = _M0L3curS858;
      _M0L6_2atmpS2909 = _M0L1iS857 + 1;
      _M0L1iS857 = _M0L6_2atmpS2909;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L4prevS855);
    }
    break;
  }
  _result_5938 = _M0L5countS854->$0;
  moonbit_decref_cycle_free(_M0L5countS854);
  return _result_5938;
}

double _M0FPC14math2ln(double _M0L1xS838) {
  struct _M0TUdiE* _M0L7_2abindS839;
  double _M0L5_2af1S840;
  int32_t _M0L5_2akiS841;
  double _M0L1fS843;
  double _M0L1kS844;
  double _M0L6_2atmpS2898;
  double _M0L1sS845;
  double _M0L2s2S846;
  double _M0L2s4S847;
  double _M0L6_2atmpS2897;
  double _M0L6_2atmpS2896;
  double _M0L6_2atmpS2895;
  double _M0L6_2atmpS2894;
  double _M0L6_2atmpS2893;
  double _M0L6_2atmpS2892;
  double _M0L2t1S848;
  double _M0L6_2atmpS2891;
  double _M0L6_2atmpS2890;
  double _M0L6_2atmpS2889;
  double _M0L6_2atmpS2888;
  double _M0L2t2S849;
  double _M0L1rS850;
  double _M0L6_2atmpS2887;
  double _M0L4hfsqS851;
  double _M0L6_2atmpS2880;
  double _M0L6_2atmpS2886;
  double _M0L6_2atmpS2884;
  double _M0L6_2atmpS2885;
  double _M0L6_2atmpS2883;
  double _M0L6_2atmpS2882;
  double _M0L6_2atmpS2881;
  #line 55 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  if (_M0L1xS838 < 0x0p+0) {
    return _M0FPC16double14not__a__number;
  } else {
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    #line 65 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
    if (
      _M0MPC16double6Double7is__nan(_M0L1xS838)
      || _M0MPC16double6Double7is__inf(_M0L1xS838)
    ) {
      return _M0L1xS838;
    } else if (_M0L1xS838 == 0x0p+0) {
      return _M0FPC16double13neg__infinity;
    }
  }
  #line 70 "C:\\Users\\31379\\.moon\\lib\\core\\math\\log_double_nonjs.mbt"
  _M0L7_2abindS839 = _M0FPC14math5frexp(_M0L1xS838);
  _M0L5_2af1S840 = _M0L7_2abindS839->$0;
  _M0L5_2akiS841 = _M0L7_2abindS839->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS839);
  if (_M0L5_2af1S840 < 0x1.6a09e667f3bcdp-1) {
    double _M0L6_2atmpS2902 = _M0L5_2af1S840 * 0x1p+1;
    double _M0L6_2atmpS2899 = _M0L6_2atmpS2902 - 0x1p+0;
    int32_t _M0L6_2atmpS2901 = _M0L5_2akiS841 - 1;
    double _M0L6_2atmpS2900 = (double)_M0L6_2atmpS2901;
    _M0L1fS843 = _M0L6_2atmpS2899;
    _M0L1kS844 = _M0L6_2atmpS2900;
    goto join_842;
  } else {
    double _M0L6_2atmpS2903 = _M0L5_2af1S840 - 0x1p+0;
    double _M0L6_2atmpS2904 = (double)_M0L5_2akiS841;
    _M0L1fS843 = _M0L6_2atmpS2903;
    _M0L1kS844 = _M0L6_2atmpS2904;
    goto join_842;
  }
  join_842:;
  _M0L6_2atmpS2898 = 0x1p+1 + _M0L1fS843;
  _M0L1sS845 = _M0L1fS843 / _M0L6_2atmpS2898;
  _M0L2s2S846 = _M0L1sS845 * _M0L1sS845;
  _M0L2s4S847 = _M0L2s2S846 * _M0L2s2S846;
  _M0L6_2atmpS2897 = _M0L2s4S847 * 0x1.2f112df3e5244p-3;
  _M0L6_2atmpS2896 = 0x1.7466496cb03dep-3 + _M0L6_2atmpS2897;
  _M0L6_2atmpS2895 = _M0L2s4S847 * _M0L6_2atmpS2896;
  _M0L6_2atmpS2894 = 0x1.2492494229359p-2 + _M0L6_2atmpS2895;
  _M0L6_2atmpS2893 = _M0L2s4S847 * _M0L6_2atmpS2894;
  _M0L6_2atmpS2892 = 0x1.5555555555593p-1 + _M0L6_2atmpS2893;
  _M0L2t1S848 = _M0L2s2S846 * _M0L6_2atmpS2892;
  _M0L6_2atmpS2891 = _M0L2s4S847 * 0x1.39a09d078c69fp-3;
  _M0L6_2atmpS2890 = 0x1.c71c51d8e78afp-3 + _M0L6_2atmpS2891;
  _M0L6_2atmpS2889 = _M0L2s4S847 * _M0L6_2atmpS2890;
  _M0L6_2atmpS2888 = 0x1.999999997fa04p-2 + _M0L6_2atmpS2889;
  _M0L2t2S849 = _M0L2s4S847 * _M0L6_2atmpS2888;
  _M0L1rS850 = _M0L2t1S848 + _M0L2t2S849;
  _M0L6_2atmpS2887 = 0x1p-1 * _M0L1fS843;
  _M0L4hfsqS851 = _M0L6_2atmpS2887 * _M0L1fS843;
  _M0L6_2atmpS2880 = _M0L1kS844 * 0x1.62e42feep-1;
  _M0L6_2atmpS2886 = _M0L4hfsqS851 + _M0L1rS850;
  _M0L6_2atmpS2884 = _M0L1sS845 * _M0L6_2atmpS2886;
  _M0L6_2atmpS2885 = _M0L1kS844 * 0x1.a39ef35793c76p-33;
  _M0L6_2atmpS2883 = _M0L6_2atmpS2884 + _M0L6_2atmpS2885;
  _M0L6_2atmpS2882 = _M0L4hfsqS851 - _M0L6_2atmpS2883;
  _M0L6_2atmpS2881 = _M0L6_2atmpS2882 - _M0L1fS843;
  return _M0L6_2atmpS2880 - _M0L6_2atmpS2881;
}

struct _M0TUdiE* _M0FPC14math5frexp(double _M0L1fS831) {
  struct _M0TUdiE* _M0L7_2abindS832;
  double _M0L10_2anorm__fS833;
  int32_t _M0L6_2aexpS834;
  uint64_t _M0L1uS835;
  uint64_t _M0L6_2atmpS2879;
  uint64_t _M0L6_2atmpS2878;
  int32_t _M0L6_2atmpS2877;
  int32_t _M0L6_2atmpS2876;
  int32_t _M0L3expS836;
  uint64_t _M0L6_2atmpS2875;
  uint64_t _M0L6_2atmpS2874;
  uint64_t _M0L6_2atmpS2873;
  double _M0L4fracS837;
  struct _M0TUdiE* _block_5941;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  if (
    _M0L1fS831 == 0x0p+0
    || _M0MPC16double6Double7is__inf(_M0L1fS831)
    || _M0MPC16double6Double7is__nan(_M0L1fS831)
  ) {
    struct _M0TUdiE* _block_5940 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_5940)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_5940->$0 = _M0L1fS831;
    _block_5940->$1 = 0;
    return _block_5940;
  }
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L7_2abindS832 = _M0FPC14math9normalize(_M0L1fS831);
  _M0L10_2anorm__fS833 = _M0L7_2abindS832->$0;
  _M0L6_2aexpS834 = _M0L7_2abindS832->$1;
  moonbit_decref_cycle_free(_M0L7_2abindS832);
  _M0L1uS835 = *(int64_t*)&_M0L10_2anorm__fS833;
  _M0L6_2atmpS2879 = _M0L1uS835 >> 52;
  _M0L6_2atmpS2878 = _M0L6_2atmpS2879 & 2047ull;
  _M0L6_2atmpS2877 = (int32_t)_M0L6_2atmpS2878;
  _M0L6_2atmpS2876 = _M0L6_2aexpS834 + _M0L6_2atmpS2877;
  _M0L3expS836 = _M0L6_2atmpS2876 - 1022;
  _M0L6_2atmpS2875 = ~9218868437227405312ull;
  _M0L6_2atmpS2874 = _M0L1uS835 & _M0L6_2atmpS2875;
  _M0L6_2atmpS2873 = _M0L6_2atmpS2874 | 4602678819172646912ull;
  _M0L4fracS837 = *(double*)&_M0L6_2atmpS2873;
  _block_5941 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_5941)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5941->$0 = _M0L4fracS837;
  _block_5941->$1 = _M0L3expS836;
  return _block_5941;
}

struct _M0TUdiE* _M0FPC14math9normalize(double _M0L1fS830) {
  double _M0L6_2atmpS2870;
  struct _M0TUdiE* _block_5943;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\math\\utils.mbt"
  _M0L6_2atmpS2870 = fabs(_M0L1fS830);
  if (_M0L6_2atmpS2870 < _M0FPC16double13min__positive) {
    double _M0L6_2atmpS2872 = (double)4503599627370496ll;
    double _M0L6_2atmpS2871 = _M0L1fS830 * _M0L6_2atmpS2872;
    struct _M0TUdiE* _block_5942 =
      (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
    Moonbit_object_header(_block_5942)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
    _block_5942->$0 = _M0L6_2atmpS2871;
    _block_5942->$1 = -52;
    return _block_5942;
  }
  _block_5943 = (struct _M0TUdiE*)moonbit_malloc(sizeof(struct _M0TUdiE));
  Moonbit_object_header(_block_5943)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5943->$0 = _M0L1fS830;
  _block_5943->$1 = 0;
  return _block_5943;
}

int32_t _M0MPC15float5Float7is__nan(float _M0L4selfS829) {
  #line 208 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0L4selfS829 != _M0L4selfS829;
}

moonbit_string_t _M0IPC15float5FloatPB4Show10to__string(float _M0L4selfS828) {
  double _M0L6_2atmpS2869;
  #line 16 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  _M0L6_2atmpS2869 = (double)_M0L4selfS828;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\float\\methods.mbt"
  return _M0MPC16double6Double10to__string(_M0L6_2atmpS2869);
}

int32_t _M0MPC15float5Float7to__int(float _M0L4selfS827) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\float\\to_int.mbt"
  if (_M0L4selfS827 != _M0L4selfS827) {
    return 0;
  } else if (_M0L4selfS827 >= 0x1.fffffffcp+30f) {
    return 2147483647;
  } else if (_M0L4selfS827 <= -0x1p+31f) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS827;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4makeGfE(
  int32_t _M0L3lenS813,
  float _M0L4elemS815
) {
  struct _M0TPB5ArrayGfE* _M0L3arrS812;
  int32_t _M0L1iS814;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS812 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS813);
  _M0L1iS814 = 0;
  while (1) {
    if (_M0L1iS814 < _M0L3lenS813) {
      float* _M0L3bufS2863 = _M0L3arrS812->$0;
      int32_t _M0L6_2atmpS2864;
      _M0L3bufS2863[_M0L1iS814] = _M0L4elemS815;
      _M0L6_2atmpS2864 = _M0L1iS814 + 1;
      _M0L1iS814 = _M0L6_2atmpS2864;
      continue;
    }
    break;
  }
  return _M0L3arrS812;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array4makeGbE(
  int32_t _M0L3lenS818,
  int32_t _M0L4elemS820
) {
  struct _M0TPB5ArrayGbE* _M0L3arrS817;
  int32_t _M0L1iS819;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS817 = _M0MPC15array5Array20unsafe__make__uninitGbE(_M0L3lenS818);
  _M0L1iS819 = 0;
  while (1) {
    if (_M0L1iS819 < _M0L3lenS818) {
      uint8_t* _M0L3bufS2865 = _M0L3arrS817->$0;
      int32_t _M0L6_2atmpS2866;
      _M0L3bufS2865[_M0L1iS819] = _M0L4elemS820;
      _M0L6_2atmpS2866 = _M0L1iS819 + 1;
      _M0L1iS819 = _M0L6_2atmpS2866;
      continue;
    }
    break;
  }
  return _M0L3arrS817;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4makeGiE(
  int32_t _M0L3lenS823,
  int32_t _M0L4elemS825
) {
  struct _M0TPB5ArrayGiE* _M0L3arrS822;
  int32_t _M0L1iS824;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3arrS822 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS823);
  _M0L1iS824 = 0;
  while (1) {
    if (_M0L1iS824 < _M0L3lenS823) {
      int32_t* _M0L3bufS2867 = _M0L3arrS822->$0;
      int32_t _M0L6_2atmpS2868;
      _M0L3bufS2867[_M0L1iS824] = _M0L4elemS825;
      _M0L6_2atmpS2868 = _M0L1iS824 + 1;
      _M0L1iS824 = _M0L6_2atmpS2868;
      continue;
    }
    break;
  }
  return _M0L3arrS822;
}

int32_t _M0MPC15array5Array3setGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS801,
  int32_t _M0L5indexS802,
  float _M0L5valueS803
) {
  int32_t _M0L3lenS800;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS800 = _M0L4selfS801->$1;
  if (_M0L5indexS802 >= 0 && _M0L5indexS802 < _M0L3lenS800) {
    float* _M0L6_2atmpS2860;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2860 = _M0MPC15array5Array6bufferGfE(_M0L4selfS801);
    _M0L6_2atmpS2860[_M0L5indexS802] = _M0L5valueS803;
    moonbit_decref_cycle_free(_M0L6_2atmpS2860);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS805,
  int32_t _M0L5indexS806,
  int32_t _M0L5valueS807
) {
  int32_t _M0L3lenS804;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS804 = _M0L4selfS805->$1;
  if (_M0L5indexS806 >= 0 && _M0L5indexS806 < _M0L3lenS804) {
    int32_t* _M0L6_2atmpS2861;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2861 = _M0MPC15array5Array6bufferGiE(_M0L4selfS805);
    _M0L6_2atmpS2861[_M0L5indexS806] = _M0L5valueS807;
    moonbit_decref_cycle_free(_M0L6_2atmpS2861);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

int32_t _M0MPC15array5Array3setGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS809,
  int32_t _M0L5indexS810,
  int32_t _M0L5valueS811
) {
  int32_t _M0L3lenS808;
  #line 262 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS808 = _M0L4selfS809->$1;
  if (_M0L5indexS810 >= 0 && _M0L5indexS810 < _M0L3lenS808) {
    uint8_t* _M0L6_2atmpS2862;
    #line 268 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2862 = _M0MPC15array5Array6bufferGbE(_M0L4selfS809);
    _M0L6_2atmpS2862[_M0L5indexS810] = _M0L5valueS811;
    moonbit_decref_cycle_free(_M0L6_2atmpS2862);
  } else {
    #line 267 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
  return 0;
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array4copyGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS795
) {
  int32_t _M0L3lenS794;
  struct _M0TPB5ArrayGfE* _M0L3arrS796;
  #line 842 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS794 = _M0L4selfS795->$1;
  if (_M0L3lenS794 == 0) {
    float* _M0L6_2atmpS2858 = moonbit_empty_float_array;
    struct _M0TPB5ArrayGfE* _block_5947 =
      (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
    Moonbit_object_header(_block_5947)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
    _block_5947->$0 = _M0L6_2atmpS2858;
    _block_5947->$1 = 0;
    return _block_5947;
  }
  #line 848 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3arrS796 = _M0MPC15array5Array20unsafe__make__uninitGfE(_M0L3lenS794);
  #line 849 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array12unsafe__blitGfE(_M0L3arrS796, 0, _M0L4selfS795, 0, _M0L3lenS794);
  return _M0L3arrS796;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array4copyGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS798
) {
  int32_t _M0L3lenS797;
  struct _M0TPB5ArrayGiE* _M0L3arrS799;
  #line 842 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS797 = _M0L4selfS798->$1;
  if (_M0L3lenS797 == 0) {
    int32_t* _M0L6_2atmpS2859 = (int32_t*)moonbit_empty_int32_array;
    struct _M0TPB5ArrayGiE* _block_5948 =
      (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
    Moonbit_object_header(_block_5948)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 66, 0);
    _block_5948->$0 = _M0L6_2atmpS2859;
    _block_5948->$1 = 0;
    return _block_5948;
  }
  #line 848 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3arrS799 = _M0MPC15array5Array20unsafe__make__uninitGiE(_M0L3lenS797);
  #line 849 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array12unsafe__blitGiE(_M0L3arrS799, 0, _M0L4selfS798, 0, _M0L3lenS797);
  return _M0L3arrS799;
}

int32_t _M0MPC15array5Array12unsafe__blitGfE(
  struct _M0TPB5ArrayGfE* _M0L3dstS784,
  int32_t _M0L11dst__offsetS785,
  struct _M0TPB5ArrayGfE* _M0L3srcS786,
  int32_t _M0L11src__offsetS787,
  int32_t _M0L3lenS788
) {
  float* _M0L6_2atmpS2854;
  float* _M0L6_2atmpS2855;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  _M0L6_2atmpS2854 = _M0MPC15array5Array6bufferGfE(_M0L3dstS784);
  #line 60 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  _M0L6_2atmpS2855 = _M0MPC15array5Array6bufferGfE(_M0L3srcS786);
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L6_2atmpS2854, _M0L11dst__offsetS785, _M0L6_2atmpS2855, _M0L11src__offsetS787, _M0L3lenS788, sizeof(float));
  return 0;
}

int32_t _M0MPC15array5Array12unsafe__blitGiE(
  struct _M0TPB5ArrayGiE* _M0L3dstS789,
  int32_t _M0L11dst__offsetS790,
  struct _M0TPB5ArrayGiE* _M0L3srcS791,
  int32_t _M0L11src__offsetS792,
  int32_t _M0L3lenS793
) {
  int32_t* _M0L6_2atmpS2856;
  int32_t* _M0L6_2atmpS2857;
  #line 48 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  _M0L6_2atmpS2856 = _M0MPC15array5Array6bufferGiE(_M0L3dstS789);
  #line 60 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  _M0L6_2atmpS2857 = _M0MPC15array5Array6bufferGiE(_M0L3srcS791);
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array_block.mbt"
  moonbit_unsafe_val_array_blit(_M0L6_2atmpS2856, _M0L11dst__offsetS790, _M0L6_2atmpS2857, _M0L11src__offsetS792, _M0L3lenS793, sizeof(int32_t));
  return 0;
}

void* _M0MPC15array5Array3popGfE(struct _M0TPB5ArrayGfE* _M0L4selfS777) {
  int32_t _M0L3lenS776;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS776 = _M0L4selfS777->$1;
  if (_M0L3lenS776 == 0) {
    return (struct moonbit_object*)&moonbit_constant_constructor_0 + 1;
  } else {
    int32_t _M0L5indexS778 = _M0L3lenS776 - 1;
    float* _M0L3bufS2852 = _M0L4selfS777->$0;
    float _M0L1vS779 = (float)_M0L3bufS2852[_M0L5indexS778];
    void* _block_5949;
    _M0L4selfS777->$1 = _M0L5indexS778;
    _block_5949
    = (void*)moonbit_malloc(sizeof(struct _M0DTPC16option6OptionGfE4Some));
    Moonbit_object_header(_block_5949)->meta
    = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 1);
    ((struct _M0DTPC16option6OptionGfE4Some*)_block_5949)->$0 = _M0L1vS779;
    return _block_5949;
  }
}

int64_t _M0MPC15array5Array3popGiE(struct _M0TPB5ArrayGiE* _M0L4selfS781) {
  int32_t _M0L3lenS780;
  #line 592 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS780 = _M0L4selfS781->$1;
  if (_M0L3lenS780 == 0) {
    return 4294967296ll;
  } else {
    int32_t _M0L5indexS782 = _M0L3lenS780 - 1;
    int32_t* _M0L3bufS2853 = _M0L4selfS781->$0;
    int32_t _M0L1vS783 = (int32_t)_M0L3bufS2853[_M0L5indexS782];
    _M0L4selfS781->$1 = _M0L5indexS782;
    return (int64_t)_M0L1vS783;
  }
}

float _M0MPC15array5Array2atGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS762,
  int32_t _M0L5indexS763
) {
  int32_t _M0L3lenS761;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS761 = _M0L4selfS762->$1;
  if (_M0L5indexS763 >= 0 && _M0L5indexS763 < _M0L3lenS761) {
    float* _M0L6_2atmpS2847;
    float _result_5950;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2847 = _M0MPC15array5Array6bufferGfE(_M0L4selfS762);
    _result_5950 = (float)_M0L6_2atmpS2847[_M0L5indexS763];
    moonbit_decref_cycle_free(_M0L6_2atmpS2847);
    return _result_5950;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

moonbit_string_t _M0MPC15array5Array2atGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS765,
  int32_t _M0L5indexS766
) {
  int32_t _M0L3lenS764;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS764 = _M0L4selfS765->$1;
  if (_M0L5indexS766 >= 0 && _M0L5indexS766 < _M0L3lenS764) {
    moonbit_string_t* _M0L6_2atmpS2848;
    moonbit_string_t _M0L6_2atmpS5542;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2848 = _M0MPC15array5Array6bufferGsE(_M0L4selfS765);
    _M0L6_2atmpS5542 = (moonbit_string_t)_M0L6_2atmpS2848[_M0L5indexS766];
    moonbit_incref_cycle_free(_M0L6_2atmpS5542);
    moonbit_decref_cycle_free(_M0L6_2atmpS2848);
    return _M0L6_2atmpS5542;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS768,
  int32_t _M0L5indexS769
) {
  int32_t _M0L3lenS767;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS767 = _M0L4selfS768->$1;
  if (_M0L5indexS769 >= 0 && _M0L5indexS769 < _M0L3lenS767) {
    int32_t* _M0L6_2atmpS2849;
    int32_t _result_5951;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2849 = _M0MPC15array5Array6bufferGiE(_M0L4selfS768);
    _result_5951 = (int32_t)_M0L6_2atmpS2849[_M0L5indexS769];
    moonbit_decref_cycle_free(_M0L6_2atmpS2849);
    return _result_5951;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0MPC15array5Array2atGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L4selfS771,
  int32_t _M0L5indexS772
) {
  int32_t _M0L3lenS770;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS770 = _M0L4selfS771->$1;
  if (_M0L5indexS772 >= 0 && _M0L5indexS772 < _M0L3lenS770) {
    struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L6_2atmpS2850;
    struct _M0TP26RiantR8snn__mbt14SpikingSynapse* _M0L6_2atmpS5543;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2850
    = _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(_M0L4selfS771);
    _M0L6_2atmpS5543
    = (struct _M0TP26RiantR8snn__mbt14SpikingSynapse*)_M0L6_2atmpS2850[
        _M0L5indexS772
      ];
    if (_M0L6_2atmpS5543) {
      moonbit_incref_cycle_free(_M0L6_2atmpS5543);
    }
    moonbit_decref_cycle_free(_M0L6_2atmpS2850);
    return _M0L6_2atmpS5543;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0MPC15array5Array2atGbE(
  struct _M0TPB5ArrayGbE* _M0L4selfS774,
  int32_t _M0L5indexS775
) {
  int32_t _M0L3lenS773;
  #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L3lenS773 = _M0L4selfS774->$1;
  if (_M0L5indexS775 >= 0 && _M0L5indexS775 < _M0L3lenS773) {
    uint8_t* _M0L6_2atmpS2851;
    int32_t _result_5952;
    #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    _M0L6_2atmpS2851 = _M0MPC15array5Array6bufferGbE(_M0L4selfS774);
    _result_5952 = (int32_t)_M0L6_2atmpS2851[_M0L5indexS775];
    moonbit_decref_cycle_free(_M0L6_2atmpS2851);
    return _result_5952;
  } else {
    #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
    moonbit_panic();
  }
}

int32_t _M0FPB7printlnGsE(moonbit_string_t _M0L5inputS760) {
  moonbit_string_t _M0L6_2atmpS2846;
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  _M0L6_2atmpS2846 = _M0IPC16string6StringPB4Show10to__string(_M0L5inputS760);
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\console.mbt"
  moonbit_println(_M0L6_2atmpS2846);
  moonbit_decref_cycle_free(_M0L6_2atmpS2846);
  return 0;
}

moonbit_string_t _M0MPC16double6Double10to__string(double _M0L4selfS759) {
  #line 296 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  #line 298 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0FPB15ryu__to__string(_M0L4selfS759);
}

int32_t _M0MPC16double6Double7is__inf(double _M0L4selfS758) {
  #line 221 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS758 > _M0FPB18double__max__value
         || _M0L4selfS758 < _M0FPB18double__min__value;
}

int32_t _M0MPC16double6Double7is__nan(double _M0L4selfS757) {
  #line 196 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double.mbt"
  return _M0L4selfS757 != _M0L4selfS757;
}

moonbit_string_t _M0FPB15ryu__to__string(double _M0L3valS742) {
  uint64_t _M0L4bitsS745;
  uint64_t _M0L6_2atmpS2845;
  uint64_t _M0L6_2atmpS2844;
  int32_t _M0L8ieeeSignS746;
  uint64_t _M0L12ieeeMantissaS747;
  uint64_t _M0L6_2atmpS2843;
  uint64_t _M0L6_2atmpS2842;
  int32_t _M0L12ieeeExponentS748;
  struct _M0TPB17FloatingDecimal64* _M0L7_2abindS749;
  struct _M0TPB17FloatingDecimal64* _M0L1vS750;
  moonbit_string_t _result_5954;
  #line 668 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L3valS742 == 0x0p+0) {
    return (moonbit_string_t)moonbit_string_literal_12.data;
  }
  if (_M0L3valS742 >= -0x1p+53 && _M0L3valS742 <= 0x1p+53) {
    if (_M0L3valS742 >= -0x1p+31 && _M0L3valS742 <= 0x1.fffffffcp+30) {
      int32_t _M0L1iS743;
      double _M0L6_2atmpS2831;
      #line 683 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS743 = _M0MPC16double6Double7to__int(_M0L3valS742);
      _M0L6_2atmpS2831 = (double)_M0L1iS743;
      if (_M0L6_2atmpS2831 == _M0L3valS742) {
        #line 685 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC13int3Int18to__string_2einner(_M0L1iS743, 10);
      }
    } else {
      int64_t _M0L1iS744;
      double _M0L6_2atmpS2832;
      #line 688 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L1iS744 = _M0MPC16double6Double9to__int64(_M0L3valS742);
      _M0L6_2atmpS2832 = (double)_M0L1iS744;
      if (_M0L6_2atmpS2832 == _M0L3valS742) {
        #line 690 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        return _M0MPC15int645Int6418to__string_2einner(_M0L1iS744, 10);
      }
    }
  }
  _M0L4bitsS745 = *(int64_t*)&_M0L3valS742;
  _M0L6_2atmpS2845 = _M0L4bitsS745 >> 63;
  _M0L6_2atmpS2844 = _M0L6_2atmpS2845 & 1ull;
  _M0L8ieeeSignS746 = _M0L6_2atmpS2844 != 0ull;
  _M0L12ieeeMantissaS747 = _M0L4bitsS745 & 4503599627370495ull;
  _M0L6_2atmpS2843 = _M0L4bitsS745 >> 52;
  _M0L6_2atmpS2842 = _M0L6_2atmpS2843 & 2047ull;
  _M0L12ieeeExponentS748 = (int32_t)_M0L6_2atmpS2842;
  if (
    _M0L12ieeeExponentS748 == 2047
    || _M0L12ieeeExponentS748 == 0 && _M0L12ieeeMantissaS747 == 0ull
  ) {
    int32_t _M0L6_2atmpS2833 = _M0L12ieeeExponentS748 != 0;
    int32_t _M0L6_2atmpS2834 = _M0L12ieeeMantissaS747 != 0ull;
    #line 707 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    return _M0FPB18copy__special__str(_M0L8ieeeSignS746, _M0L6_2atmpS2833, _M0L6_2atmpS2834);
  }
  #line 709 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS749
  = _M0FPB15d2d__small__int(_M0L12ieeeMantissaS747, _M0L12ieeeExponentS748);
  if (_M0L7_2abindS749 == 0) {
    uint32_t _M0L6_2atmpS2835;
    if (_M0L7_2abindS749) {
      moonbit_decref_cycle_free(_M0L7_2abindS749);
    }
    _M0L6_2atmpS2835 = *(uint32_t*)&_M0L12ieeeExponentS748;
    #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L1vS750 = _M0FPB3d2d(_M0L12ieeeMantissaS747, _M0L6_2atmpS2835);
  } else {
    struct _M0TPB17FloatingDecimal64* _M0L7_2aSomeS751 = _M0L7_2abindS749;
    struct _M0TPB17FloatingDecimal64* _M0L4_2afS752 = _M0L7_2aSomeS751;
    struct _M0TPB17FloatingDecimal64* _M0L1xS753 = _M0L4_2afS752;
    while (1) {
      uint64_t _M0L8mantissaS2841 = _M0L1xS753->$0;
      uint64_t _M0L1qS754 = _M0L8mantissaS2841 / 10ull;
      uint64_t _M0L8mantissaS2839 = _M0L1xS753->$0;
      uint64_t _M0L6_2atmpS2840 = 10ull * _M0L1qS754;
      uint64_t _M0L1rS755 = _M0L8mantissaS2839 - _M0L6_2atmpS2840;
      int32_t _M0L8exponentS2838;
      int32_t _M0L6_2atmpS2837;
      struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2836;
      if (_M0L1rS755 != 0ull) {
        _M0L1vS750 = _M0L1xS753;
        break;
      }
      _M0L8exponentS2838 = _M0L1xS753->$1;
      moonbit_decref_cycle_free(_M0L1xS753);
      _M0L6_2atmpS2837 = _M0L8exponentS2838 + 1;
      _M0L6_2atmpS2836
      = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
      Moonbit_object_header(_M0L6_2atmpS2836)->meta
      = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
      _M0L6_2atmpS2836->$0 = _M0L1qS754;
      _M0L6_2atmpS2836->$1 = _M0L6_2atmpS2837;
      _M0L1xS753 = _M0L6_2atmpS2836;
      continue;
      break;
    }
  }
  #line 721 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_5954 = _M0FPB9to__chars(_M0L1vS750, _M0L8ieeeSignS746);
  moonbit_decref_cycle_free(_M0L1vS750);
  return _result_5954;
}

struct _M0TPB17FloatingDecimal64* _M0FPB15d2d__small__int(
  uint64_t _M0L12ieeeMantissaS737,
  int32_t _M0L12ieeeExponentS739
) {
  uint64_t _M0L2m2S736;
  int32_t _M0L6_2atmpS2830;
  int32_t _M0L2e2S738;
  int32_t _M0L6_2atmpS2829;
  uint64_t _M0L6_2atmpS2828;
  uint64_t _M0L4maskS740;
  uint64_t _M0L8fractionS741;
  int32_t _M0L6_2atmpS2827;
  uint64_t _M0L6_2atmpS2826;
  struct _M0TPB17FloatingDecimal64* _M0L6_2atmpS2825;
  #line 637 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2m2S736 = 4503599627370496ull | _M0L12ieeeMantissaS737;
  _M0L6_2atmpS2830 = _M0L12ieeeExponentS739 - 1023;
  _M0L2e2S738 = _M0L6_2atmpS2830 - 52;
  if (_M0L2e2S738 > 0) {
    return 0;
  }
  if (_M0L2e2S738 < -52) {
    return 0;
  }
  _M0L6_2atmpS2829 = -_M0L2e2S738;
  _M0L6_2atmpS2828 = 1ull << (_M0L6_2atmpS2829 & 63);
  _M0L4maskS740 = _M0L6_2atmpS2828 - 1ull;
  _M0L8fractionS741 = _M0L2m2S736 & _M0L4maskS740;
  if (_M0L8fractionS741 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2827 = -_M0L2e2S738;
  _M0L6_2atmpS2826 = _M0L2m2S736 >> (_M0L6_2atmpS2827 & 63);
  _M0L6_2atmpS2825
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_M0L6_2atmpS2825)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _M0L6_2atmpS2825->$0 = _M0L6_2atmpS2826;
  _M0L6_2atmpS2825->$1 = 0;
  return _M0L6_2atmpS2825;
}

moonbit_string_t _M0FPB9to__chars(
  struct _M0TPB17FloatingDecimal64* _M0L1vS704,
  int32_t _M0L4signS702
) {
  moonbit_bytes_t _M0L6resultS700;
  int32_t _M0Lm5indexS701;
  uint64_t _M0L6outputS703;
  int32_t _M0L7olengthS705;
  int32_t _M0L8exponentS2824;
  int32_t _M0L6_2atmpS2823;
  int32_t _M0Lm3expS706;
  int32_t _M0L6_2atmpS2822;
  int32_t _M0L6_2atmpS2820;
  int32_t _M0L18scientificNotationS707;
  #line 530 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6resultS700 = (moonbit_bytes_t)moonbit_make_bytes(25, 0);
  _M0Lm5indexS701 = 0;
  if (_M0L4signS702) {
    int32_t _M0L6_2atmpS2694 = _M0Lm5indexS701;
    int32_t _M0L6_2atmpS2695;
    if (
      _M0L6_2atmpS2694 < 0
      || _M0L6_2atmpS2694 >= Moonbit_array_length(_M0L6resultS700)
    ) {
      #line 535 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS700[_M0L6_2atmpS2694] = 45;
    _M0L6_2atmpS2695 = _M0Lm5indexS701;
    _M0Lm5indexS701 = _M0L6_2atmpS2695 + 1;
  }
  _M0L6outputS703 = _M0L1vS704->$0;
  #line 539 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7olengthS705 = _M0FPB17decimal__length17(_M0L6outputS703);
  _M0L8exponentS2824 = _M0L1vS704->$1;
  _M0L6_2atmpS2823 = _M0L8exponentS2824 + _M0L7olengthS705;
  _M0Lm3expS706 = _M0L6_2atmpS2823 - 1;
  _M0L6_2atmpS2822 = _M0Lm3expS706;
  if (_M0L6_2atmpS2822 >= -6) {
    int32_t _M0L6_2atmpS2821 = _M0Lm3expS706;
    _M0L6_2atmpS2820 = _M0L6_2atmpS2821 < 21;
  } else {
    _M0L6_2atmpS2820 = 0;
  }
  _M0L18scientificNotationS707 = !_M0L6_2atmpS2820;
  if (_M0L18scientificNotationS707) {
    int32_t _M0L7_2abindS708 = _M0L7olengthS705 - 1;
    uint64_t _M0L6outputS709;
    int32_t _M0L1iS710 = 0;
    uint64_t _M0L6outputS711 = _M0L6outputS703;
    int32_t _M0L6_2atmpS2696;
    int32_t _M0L6_2atmpS2700;
    int32_t _M0L6_2atmpS2699;
    int32_t _M0L6_2atmpS2698;
    int32_t _M0L6_2atmpS2697;
    int32_t _M0L6_2atmpS2704;
    int32_t _M0L6_2atmpS2705;
    int32_t _M0L6_2atmpS2706;
    int32_t _M0L6_2atmpS2707;
    int32_t _M0L6_2atmpS2708;
    int32_t _M0L6_2atmpS2714;
    int32_t _M0L6_2atmpS2747;
    moonbit_string_t _result_5956;
    while (1) {
      if (_M0L1iS710 < _M0L7_2abindS708) {
        uint64_t _M0L1cS712 = _M0L6outputS711 % 10ull;
        int32_t _M0L6_2atmpS2753 = _M0Lm5indexS701;
        int32_t _M0L6_2atmpS2752 = _M0L6_2atmpS2753 + _M0L7olengthS705;
        int32_t _M0L6_2atmpS2748 = _M0L6_2atmpS2752 - _M0L1iS710;
        int32_t _M0L6_2atmpS2751 = (int32_t)_M0L1cS712;
        int32_t _M0L6_2atmpS2750 = 48 + _M0L6_2atmpS2751;
        int32_t _M0L6_2atmpS2749 = _M0L6_2atmpS2750 & 0xff;
        int32_t _M0L6_2atmpS2754;
        uint64_t _M0L6_2atmpS2755;
        if (
          _M0L6_2atmpS2748 < 0
          || _M0L6_2atmpS2748 >= Moonbit_array_length(_M0L6resultS700)
        ) {
          #line 547 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS700[_M0L6_2atmpS2748] = _M0L6_2atmpS2749;
        _M0L6_2atmpS2754 = _M0L1iS710 + 1;
        _M0L6_2atmpS2755 = _M0L6outputS711 / 10ull;
        _M0L1iS710 = _M0L6_2atmpS2754;
        _M0L6outputS711 = _M0L6_2atmpS2755;
        continue;
      } else {
        _M0L6outputS709 = _M0L6outputS711;
      }
      break;
    }
    _M0L6_2atmpS2696 = _M0Lm5indexS701;
    _M0L6_2atmpS2700 = (int32_t)_M0L6outputS709;
    _M0L6_2atmpS2699 = _M0L6_2atmpS2700 % 10;
    _M0L6_2atmpS2698 = 48 + _M0L6_2atmpS2699;
    _M0L6_2atmpS2697 = _M0L6_2atmpS2698 & 0xff;
    if (
      _M0L6_2atmpS2696 < 0
      || _M0L6_2atmpS2696 >= Moonbit_array_length(_M0L6resultS700)
    ) {
      #line 552 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS700[_M0L6_2atmpS2696] = _M0L6_2atmpS2697;
    if (_M0L7olengthS705 > 1) {
      int32_t _M0L6_2atmpS2702 = _M0Lm5indexS701;
      int32_t _M0L6_2atmpS2701 = _M0L6_2atmpS2702 + 1;
      if (
        _M0L6_2atmpS2701 < 0
        || _M0L6_2atmpS2701 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 554 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2701] = 46;
    } else {
      int32_t _M0L6_2atmpS2703 = _M0Lm5indexS701;
      _M0Lm5indexS701 = _M0L6_2atmpS2703 - 1;
    }
    _M0L6_2atmpS2704 = _M0Lm5indexS701;
    _M0L6_2atmpS2705 = _M0L7olengthS705 + 1;
    _M0Lm5indexS701 = _M0L6_2atmpS2704 + _M0L6_2atmpS2705;
    _M0L6_2atmpS2706 = _M0Lm5indexS701;
    if (
      _M0L6_2atmpS2706 < 0
      || _M0L6_2atmpS2706 >= Moonbit_array_length(_M0L6resultS700)
    ) {
      #line 562 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      moonbit_panic();
    }
    _M0L6resultS700[_M0L6_2atmpS2706] = 101;
    _M0L6_2atmpS2707 = _M0Lm5indexS701;
    _M0Lm5indexS701 = _M0L6_2atmpS2707 + 1;
    _M0L6_2atmpS2708 = _M0Lm3expS706;
    if (_M0L6_2atmpS2708 < 0) {
      int32_t _M0L6_2atmpS2709 = _M0Lm5indexS701;
      int32_t _M0L6_2atmpS2710;
      int32_t _M0L6_2atmpS2711;
      if (
        _M0L6_2atmpS2709 < 0
        || _M0L6_2atmpS2709 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 565 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2709] = 45;
      _M0L6_2atmpS2710 = _M0Lm5indexS701;
      _M0Lm5indexS701 = _M0L6_2atmpS2710 + 1;
      _M0L6_2atmpS2711 = _M0Lm3expS706;
      _M0Lm3expS706 = -_M0L6_2atmpS2711;
    } else {
      int32_t _M0L6_2atmpS2712 = _M0Lm5indexS701;
      int32_t _M0L6_2atmpS2713;
      if (
        _M0L6_2atmpS2712 < 0
        || _M0L6_2atmpS2712 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 569 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2712] = 43;
      _M0L6_2atmpS2713 = _M0Lm5indexS701;
      _M0Lm5indexS701 = _M0L6_2atmpS2713 + 1;
    }
    _M0L6_2atmpS2714 = _M0Lm3expS706;
    if (_M0L6_2atmpS2714 >= 100) {
      int32_t _M0L6_2atmpS2730 = _M0Lm3expS706;
      int32_t _M0L1aS714 = _M0L6_2atmpS2730 / 100;
      int32_t _M0L6_2atmpS2729 = _M0Lm3expS706;
      int32_t _M0L6_2atmpS2728 = _M0L6_2atmpS2729 / 10;
      int32_t _M0L1bS715 = _M0L6_2atmpS2728 % 10;
      int32_t _M0L6_2atmpS2727 = _M0Lm3expS706;
      int32_t _M0L1cS716 = _M0L6_2atmpS2727 % 10;
      int32_t _M0L6_2atmpS2715 = _M0Lm5indexS701;
      int32_t _M0L6_2atmpS2717 = 48 + _M0L1aS714;
      int32_t _M0L6_2atmpS2716 = _M0L6_2atmpS2717 & 0xff;
      int32_t _M0L6_2atmpS2721;
      int32_t _M0L6_2atmpS2718;
      int32_t _M0L6_2atmpS2720;
      int32_t _M0L6_2atmpS2719;
      int32_t _M0L6_2atmpS2725;
      int32_t _M0L6_2atmpS2722;
      int32_t _M0L6_2atmpS2724;
      int32_t _M0L6_2atmpS2723;
      int32_t _M0L6_2atmpS2726;
      if (
        _M0L6_2atmpS2715 < 0
        || _M0L6_2atmpS2715 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2715] = _M0L6_2atmpS2716;
      _M0L6_2atmpS2721 = _M0Lm5indexS701;
      _M0L6_2atmpS2718 = _M0L6_2atmpS2721 + 1;
      _M0L6_2atmpS2720 = 48 + _M0L1bS715;
      _M0L6_2atmpS2719 = _M0L6_2atmpS2720 & 0xff;
      if (
        _M0L6_2atmpS2718 < 0
        || _M0L6_2atmpS2718 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 577 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2718] = _M0L6_2atmpS2719;
      _M0L6_2atmpS2725 = _M0Lm5indexS701;
      _M0L6_2atmpS2722 = _M0L6_2atmpS2725 + 2;
      _M0L6_2atmpS2724 = 48 + _M0L1cS716;
      _M0L6_2atmpS2723 = _M0L6_2atmpS2724 & 0xff;
      if (
        _M0L6_2atmpS2722 < 0
        || _M0L6_2atmpS2722 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 578 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2722] = _M0L6_2atmpS2723;
      _M0L6_2atmpS2726 = _M0Lm5indexS701;
      _M0Lm5indexS701 = _M0L6_2atmpS2726 + 3;
    } else {
      int32_t _M0L6_2atmpS2731 = _M0Lm3expS706;
      if (_M0L6_2atmpS2731 >= 10) {
        int32_t _M0L6_2atmpS2741 = _M0Lm3expS706;
        int32_t _M0L1aS717 = _M0L6_2atmpS2741 / 10;
        int32_t _M0L6_2atmpS2740 = _M0Lm3expS706;
        int32_t _M0L1bS718 = _M0L6_2atmpS2740 % 10;
        int32_t _M0L6_2atmpS2732 = _M0Lm5indexS701;
        int32_t _M0L6_2atmpS2734 = 48 + _M0L1aS717;
        int32_t _M0L6_2atmpS2733 = _M0L6_2atmpS2734 & 0xff;
        int32_t _M0L6_2atmpS2738;
        int32_t _M0L6_2atmpS2735;
        int32_t _M0L6_2atmpS2737;
        int32_t _M0L6_2atmpS2736;
        int32_t _M0L6_2atmpS2739;
        if (
          _M0L6_2atmpS2732 < 0
          || _M0L6_2atmpS2732 >= Moonbit_array_length(_M0L6resultS700)
        ) {
          #line 583 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS700[_M0L6_2atmpS2732] = _M0L6_2atmpS2733;
        _M0L6_2atmpS2738 = _M0Lm5indexS701;
        _M0L6_2atmpS2735 = _M0L6_2atmpS2738 + 1;
        _M0L6_2atmpS2737 = 48 + _M0L1bS718;
        _M0L6_2atmpS2736 = _M0L6_2atmpS2737 & 0xff;
        if (
          _M0L6_2atmpS2735 < 0
          || _M0L6_2atmpS2735 >= Moonbit_array_length(_M0L6resultS700)
        ) {
          #line 584 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS700[_M0L6_2atmpS2735] = _M0L6_2atmpS2736;
        _M0L6_2atmpS2739 = _M0Lm5indexS701;
        _M0Lm5indexS701 = _M0L6_2atmpS2739 + 2;
      } else {
        int32_t _M0L6_2atmpS2742 = _M0Lm5indexS701;
        int32_t _M0L6_2atmpS2745 = _M0Lm3expS706;
        int32_t _M0L6_2atmpS2744 = 48 + _M0L6_2atmpS2745;
        int32_t _M0L6_2atmpS2743 = _M0L6_2atmpS2744 & 0xff;
        int32_t _M0L6_2atmpS2746;
        if (
          _M0L6_2atmpS2742 < 0
          || _M0L6_2atmpS2742 >= Moonbit_array_length(_M0L6resultS700)
        ) {
          #line 587 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
          moonbit_panic();
        }
        _M0L6resultS700[_M0L6_2atmpS2742] = _M0L6_2atmpS2743;
        _M0L6_2atmpS2746 = _M0Lm5indexS701;
        _M0Lm5indexS701 = _M0L6_2atmpS2746 + 1;
      }
    }
    _M0L6_2atmpS2747 = _M0Lm5indexS701;
    #line 590 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_5956
    = _M0FPB19string__from__bytes(_M0L6resultS700, 0, _M0L6_2atmpS2747);
    moonbit_decref_cycle_free(_M0L6resultS700);
    return _result_5956;
  } else {
    int32_t _M0L6_2atmpS2756 = _M0Lm3expS706;
    int32_t _M0L6_2atmpS2819;
    moonbit_string_t _result_5962;
    if (_M0L6_2atmpS2756 < 0) {
      int32_t _M0L6_2atmpS2757 = _M0Lm5indexS701;
      int32_t _M0L6_2atmpS2759;
      int32_t _M0L6_2atmpS2758;
      int32_t _M0L6_2atmpS2760;
      int32_t _M0L1iS719;
      int32_t _M0L6_2atmpS2775;
      int32_t _M0L6_2atmpS2777;
      int32_t _M0L6_2atmpS2776;
      int32_t _M0L7currentS721;
      int32_t _M0L1iS722;
      uint64_t _M0L6outputS723;
      if (
        _M0L6_2atmpS2757 < 0
        || _M0L6_2atmpS2757 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 595 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2757] = 48;
      _M0L6_2atmpS2759 = _M0Lm5indexS701;
      _M0L6_2atmpS2758 = _M0L6_2atmpS2759 + 1;
      if (
        _M0L6_2atmpS2758 < 0
        || _M0L6_2atmpS2758 >= Moonbit_array_length(_M0L6resultS700)
      ) {
        #line 596 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6resultS700[_M0L6_2atmpS2758] = 46;
      _M0L6_2atmpS2760 = _M0Lm5indexS701;
      _M0Lm5indexS701 = _M0L6_2atmpS2760 + 2;
      _M0L1iS719 = -1;
      while (1) {
        int32_t _M0L6_2atmpS2761 = _M0Lm3expS706;
        if (_M0L1iS719 > _M0L6_2atmpS2761) {
          int32_t _M0L6_2atmpS2764 = _M0Lm5indexS701;
          int32_t _M0L6_2atmpS2763 = _M0L6_2atmpS2764 - _M0L1iS719;
          int32_t _M0L6_2atmpS2762 = _M0L6_2atmpS2763 - 1;
          int32_t _M0L6_2atmpS2765;
          if (
            _M0L6_2atmpS2762 < 0
            || _M0L6_2atmpS2762 >= Moonbit_array_length(_M0L6resultS700)
          ) {
            #line 599 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS700[_M0L6_2atmpS2762] = 48;
          _M0L6_2atmpS2765 = _M0L1iS719 - 1;
          _M0L1iS719 = _M0L6_2atmpS2765;
          continue;
        }
        break;
      }
      _M0L6_2atmpS2775 = _M0Lm5indexS701;
      _M0L6_2atmpS2777 = _M0Lm3expS706;
      _M0L6_2atmpS2776 = -1 - _M0L6_2atmpS2777;
      _M0L7currentS721 = _M0L6_2atmpS2775 + _M0L6_2atmpS2776;
      _M0L1iS722 = 0;
      _M0L6outputS723 = _M0L6outputS703;
      while (1) {
        if (_M0L1iS722 < _M0L7olengthS705) {
          int32_t _M0L6_2atmpS2772 = _M0L7currentS721 + _M0L7olengthS705;
          int32_t _M0L6_2atmpS2771 = _M0L6_2atmpS2772 - _M0L1iS722;
          int32_t _M0L6_2atmpS2766 = _M0L6_2atmpS2771 - 1;
          uint64_t _M0L6_2atmpS2770 = _M0L6outputS723 % 10ull;
          int32_t _M0L6_2atmpS2769 = (int32_t)_M0L6_2atmpS2770;
          int32_t _M0L6_2atmpS2768 = 48 + _M0L6_2atmpS2769;
          int32_t _M0L6_2atmpS2767 = _M0L6_2atmpS2768 & 0xff;
          int32_t _M0L6_2atmpS2773;
          uint64_t _M0L6_2atmpS2774;
          if (
            _M0L6_2atmpS2766 < 0
            || _M0L6_2atmpS2766 >= Moonbit_array_length(_M0L6resultS700)
          ) {
            #line 603 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
            moonbit_panic();
          }
          _M0L6resultS700[_M0L6_2atmpS2766] = _M0L6_2atmpS2767;
          _M0L6_2atmpS2773 = _M0L1iS722 + 1;
          _M0L6_2atmpS2774 = _M0L6outputS723 / 10ull;
          _M0L1iS722 = _M0L6_2atmpS2773;
          _M0L6outputS723 = _M0L6_2atmpS2774;
          continue;
        }
        break;
      }
      _M0Lm5indexS701 = _M0L7currentS721 + _M0L7olengthS705;
    } else {
      int32_t _M0L6_2atmpS2779 = _M0Lm3expS706;
      int32_t _M0L6_2atmpS2778 = _M0L6_2atmpS2779 + 1;
      if (_M0L6_2atmpS2778 >= _M0L7olengthS705) {
        int32_t _M0L1iS725 = 0;
        uint64_t _M0L6outputS726 = _M0L6outputS703;
        int32_t _M0L6_2atmpS2790;
        int32_t _M0L6_2atmpS2795;
        int32_t _M0L7_2abindS728;
        int32_t _M0L1iS729;
        int32_t _M0L6_2atmpS2796;
        int32_t _M0L6_2atmpS2799;
        int32_t _M0L6_2atmpS2798;
        int32_t _M0L6_2atmpS2797;
        while (1) {
          if (_M0L1iS725 < _M0L7olengthS705) {
            int32_t _M0L6_2atmpS2787 = _M0Lm5indexS701;
            int32_t _M0L6_2atmpS2786 = _M0L6_2atmpS2787 + _M0L7olengthS705;
            int32_t _M0L6_2atmpS2785 = _M0L6_2atmpS2786 - _M0L1iS725;
            int32_t _M0L6_2atmpS2780 = _M0L6_2atmpS2785 - 1;
            uint64_t _M0L6_2atmpS2784 = _M0L6outputS726 % 10ull;
            int32_t _M0L6_2atmpS2783 = (int32_t)_M0L6_2atmpS2784;
            int32_t _M0L6_2atmpS2782 = 48 + _M0L6_2atmpS2783;
            int32_t _M0L6_2atmpS2781 = _M0L6_2atmpS2782 & 0xff;
            int32_t _M0L6_2atmpS2788;
            uint64_t _M0L6_2atmpS2789;
            if (
              _M0L6_2atmpS2780 < 0
              || _M0L6_2atmpS2780 >= Moonbit_array_length(_M0L6resultS700)
            ) {
              #line 610 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS700[_M0L6_2atmpS2780] = _M0L6_2atmpS2781;
            _M0L6_2atmpS2788 = _M0L1iS725 + 1;
            _M0L6_2atmpS2789 = _M0L6outputS726 / 10ull;
            _M0L1iS725 = _M0L6_2atmpS2788;
            _M0L6outputS726 = _M0L6_2atmpS2789;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2790 = _M0Lm5indexS701;
        _M0Lm5indexS701 = _M0L6_2atmpS2790 + _M0L7olengthS705;
        _M0L6_2atmpS2795 = _M0Lm3expS706;
        _M0L7_2abindS728 = _M0L6_2atmpS2795 + 1;
        _M0L1iS729 = _M0L7olengthS705;
        while (1) {
          if (_M0L1iS729 < _M0L7_2abindS728) {
            int32_t _M0L6_2atmpS2793 = _M0Lm5indexS701;
            int32_t _M0L6_2atmpS2792 = _M0L6_2atmpS2793 + _M0L1iS729;
            int32_t _M0L6_2atmpS2791 = _M0L6_2atmpS2792 - _M0L7olengthS705;
            int32_t _M0L6_2atmpS2794;
            if (
              _M0L6_2atmpS2791 < 0
              || _M0L6_2atmpS2791 >= Moonbit_array_length(_M0L6resultS700)
            ) {
              #line 615 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS700[_M0L6_2atmpS2791] = 48;
            _M0L6_2atmpS2794 = _M0L1iS729 + 1;
            _M0L1iS729 = _M0L6_2atmpS2794;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2796 = _M0Lm5indexS701;
        _M0L6_2atmpS2799 = _M0Lm3expS706;
        _M0L6_2atmpS2798 = _M0L6_2atmpS2799 + 1;
        _M0L6_2atmpS2797 = _M0L6_2atmpS2798 - _M0L7olengthS705;
        _M0Lm5indexS701 = _M0L6_2atmpS2796 + _M0L6_2atmpS2797;
      } else {
        int32_t _M0L6_2atmpS2816 = _M0Lm5indexS701;
        int32_t _M0L6_2atmpS2815 = _M0L6_2atmpS2816 + 1;
        int32_t _M0L1iS731 = 0;
        int32_t _M0L7currentS732 = _M0L6_2atmpS2815;
        uint64_t _M0L6outputS733 = _M0L6outputS703;
        int32_t _M0L6_2atmpS2817;
        int32_t _M0L6_2atmpS2818;
        while (1) {
          if (_M0L1iS731 < _M0L7olengthS705) {
            int32_t _M0L6_2atmpS2811 = _M0L7olengthS705 - _M0L1iS731;
            int32_t _M0L6_2atmpS2809 = _M0L6_2atmpS2811 - 1;
            int32_t _M0L6_2atmpS2810 = _M0Lm3expS706;
            int32_t _M0L7currentS734;
            int32_t _M0L6_2atmpS2806;
            int32_t _M0L6_2atmpS2805;
            int32_t _M0L6_2atmpS2800;
            uint64_t _M0L6_2atmpS2804;
            int32_t _M0L6_2atmpS2803;
            int32_t _M0L6_2atmpS2802;
            int32_t _M0L6_2atmpS2801;
            int32_t _M0L6_2atmpS2807;
            uint64_t _M0L6_2atmpS2808;
            if (_M0L6_2atmpS2809 == _M0L6_2atmpS2810) {
              int32_t _M0L6_2atmpS2814 = _M0L7currentS732 + _M0L7olengthS705;
              int32_t _M0L6_2atmpS2813 = _M0L6_2atmpS2814 - _M0L1iS731;
              int32_t _M0L6_2atmpS2812 = _M0L6_2atmpS2813 - 1;
              if (
                _M0L6_2atmpS2812 < 0
                || _M0L6_2atmpS2812 >= Moonbit_array_length(_M0L6resultS700)
              ) {
                #line 622 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
                moonbit_panic();
              }
              _M0L6resultS700[_M0L6_2atmpS2812] = 46;
              _M0L7currentS734 = _M0L7currentS732 - 1;
            } else {
              _M0L7currentS734 = _M0L7currentS732;
            }
            _M0L6_2atmpS2806 = _M0L7currentS734 + _M0L7olengthS705;
            _M0L6_2atmpS2805 = _M0L6_2atmpS2806 - _M0L1iS731;
            _M0L6_2atmpS2800 = _M0L6_2atmpS2805 - 1;
            _M0L6_2atmpS2804 = _M0L6outputS733 % 10ull;
            _M0L6_2atmpS2803 = (int32_t)_M0L6_2atmpS2804;
            _M0L6_2atmpS2802 = 48 + _M0L6_2atmpS2803;
            _M0L6_2atmpS2801 = _M0L6_2atmpS2802 & 0xff;
            if (
              _M0L6_2atmpS2800 < 0
              || _M0L6_2atmpS2800 >= Moonbit_array_length(_M0L6resultS700)
            ) {
              #line 627 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
              moonbit_panic();
            }
            _M0L6resultS700[_M0L6_2atmpS2800] = _M0L6_2atmpS2801;
            _M0L6_2atmpS2807 = _M0L1iS731 + 1;
            _M0L6_2atmpS2808 = _M0L6outputS733 / 10ull;
            _M0L1iS731 = _M0L6_2atmpS2807;
            _M0L7currentS732 = _M0L7currentS734;
            _M0L6outputS733 = _M0L6_2atmpS2808;
            continue;
          }
          break;
        }
        _M0L6_2atmpS2817 = _M0Lm5indexS701;
        _M0L6_2atmpS2818 = _M0L7olengthS705 + 1;
        _M0Lm5indexS701 = _M0L6_2atmpS2817 + _M0L6_2atmpS2818;
      }
    }
    _M0L6_2atmpS2819 = _M0Lm5indexS701;
    #line 632 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_5962
    = _M0FPB19string__from__bytes(_M0L6resultS700, 0, _M0L6_2atmpS2819);
    moonbit_decref_cycle_free(_M0L6resultS700);
    return _result_5962;
  }
}

struct _M0TPB17FloatingDecimal64* _M0FPB3d2d(
  uint64_t _M0L12ieeeMantissaS646,
  uint32_t _M0L12ieeeExponentS645
) {
  int32_t _M0Lm2e2S643;
  uint64_t _M0Lm2m2S644;
  uint64_t _M0L6_2atmpS2693;
  uint64_t _M0L6_2atmpS2692;
  int32_t _M0L4evenS647;
  uint64_t _M0L6_2atmpS2691;
  uint64_t _M0L2mvS648;
  int32_t _M0L7mmShiftS649;
  uint64_t _M0Lm2vrS650;
  uint64_t _M0Lm2vpS651;
  uint64_t _M0Lm2vmS652;
  int32_t _M0Lm3e10S653;
  int32_t _M0Lm17vmIsTrailingZerosS654;
  int32_t _M0Lm17vrIsTrailingZerosS655;
  int32_t _M0L6_2atmpS2593;
  int32_t _M0Lm7removedS674;
  int32_t _M0Lm16lastRemovedDigitS675;
  uint64_t _M0Lm6outputS676;
  int32_t _M0L6_2atmpS2689;
  int32_t _M0L6_2atmpS2690;
  int32_t _M0L3expS699;
  uint64_t _M0L6_2atmpS2688;
  struct _M0TPB17FloatingDecimal64* _block_5968;
  #line 347 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0Lm2e2S643 = 0;
  _M0Lm2m2S644 = 0ull;
  if (_M0L12ieeeExponentS645 == 0u) {
    _M0Lm2e2S643 = -1076;
    _M0Lm2m2S644 = _M0L12ieeeMantissaS646;
  } else {
    int32_t _M0L6_2atmpS2592 = *(int32_t*)&_M0L12ieeeExponentS645;
    int32_t _M0L6_2atmpS2591 = _M0L6_2atmpS2592 - 1023;
    int32_t _M0L6_2atmpS2590 = _M0L6_2atmpS2591 - 52;
    _M0Lm2e2S643 = _M0L6_2atmpS2590 - 2;
    _M0Lm2m2S644 = 4503599627370496ull | _M0L12ieeeMantissaS646;
  }
  _M0L6_2atmpS2693 = _M0Lm2m2S644;
  _M0L6_2atmpS2692 = _M0L6_2atmpS2693 & 1ull;
  _M0L4evenS647 = _M0L6_2atmpS2692 == 0ull;
  _M0L6_2atmpS2691 = _M0Lm2m2S644;
  _M0L2mvS648 = 4ull * _M0L6_2atmpS2691;
  _M0L7mmShiftS649
  = _M0L12ieeeMantissaS646 != 0ull || _M0L12ieeeExponentS645 <= 1u;
  _M0Lm2vrS650 = 0ull;
  _M0Lm2vpS651 = 0ull;
  _M0Lm2vmS652 = 0ull;
  _M0Lm3e10S653 = 0;
  _M0Lm17vmIsTrailingZerosS654 = 0;
  _M0Lm17vrIsTrailingZerosS655 = 0;
  _M0L6_2atmpS2593 = _M0Lm2e2S643;
  if (_M0L6_2atmpS2593 >= 0) {
    int32_t _M0L6_2atmpS2615 = _M0Lm2e2S643;
    int32_t _M0L6_2atmpS2611;
    int32_t _M0L6_2atmpS2614;
    int32_t _M0L6_2atmpS2613;
    int32_t _M0L6_2atmpS2612;
    int32_t _M0L1qS656;
    int32_t _M0L6_2atmpS2610;
    int32_t _M0L6_2atmpS2609;
    int32_t _M0L1kS657;
    int32_t _M0L6_2atmpS2608;
    int32_t _M0L6_2atmpS2607;
    int32_t _M0L6_2atmpS2606;
    int32_t _M0L1iS658;
    struct _M0TPB8Pow5Pair _M0L4pow5S659;
    uint64_t _M0L6_2atmpS2605;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS660;
    uint64_t _M0L8_2avrOutS661;
    uint64_t _M0L8_2avpOutS662;
    uint64_t _M0L8_2avmOutS663;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2611 = _M0FPB9log10Pow2(_M0L6_2atmpS2615);
    _M0L6_2atmpS2614 = _M0Lm2e2S643;
    _M0L6_2atmpS2613 = _M0L6_2atmpS2614 > 3;
    #line 383 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2612 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS2613);
    _M0L1qS656 = _M0L6_2atmpS2611 - _M0L6_2atmpS2612;
    _M0Lm3e10S653 = _M0L1qS656;
    #line 385 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2610 = _M0FPB8pow5bits(_M0L1qS656);
    _M0L6_2atmpS2609 = 125 + _M0L6_2atmpS2610;
    _M0L1kS657 = _M0L6_2atmpS2609 - 1;
    _M0L6_2atmpS2608 = _M0Lm2e2S643;
    _M0L6_2atmpS2607 = -_M0L6_2atmpS2608;
    _M0L6_2atmpS2606 = _M0L6_2atmpS2607 + _M0L1qS656;
    _M0L1iS658 = _M0L6_2atmpS2606 + _M0L1kS657;
    #line 387 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S659 = _M0FPB22double__computeInvPow5(_M0L1qS656);
    _M0L6_2atmpS2605 = _M0Lm2m2S644;
    #line 388 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS660
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS2605, _M0L4pow5S659, _M0L1iS658, _M0L7mmShiftS649);
    _M0L8_2avrOutS661 = _M0L7_2abindS660.$0;
    _M0L8_2avpOutS662 = _M0L7_2abindS660.$1;
    _M0L8_2avmOutS663 = _M0L7_2abindS660.$2;
    _M0Lm2vrS650 = _M0L8_2avrOutS661;
    _M0Lm2vpS651 = _M0L8_2avpOutS662;
    _M0Lm2vmS652 = _M0L8_2avmOutS663;
    if (_M0L1qS656 <= 21) {
      int32_t _M0L6_2atmpS2601 = (int32_t)_M0L2mvS648;
      uint64_t _M0L6_2atmpS2604 = _M0L2mvS648 / 5ull;
      int32_t _M0L6_2atmpS2603 = (int32_t)_M0L6_2atmpS2604;
      int32_t _M0L6_2atmpS2602 = 5 * _M0L6_2atmpS2603;
      int32_t _M0L6mvMod5S664 = _M0L6_2atmpS2601 - _M0L6_2atmpS2602;
      if (_M0L6mvMod5S664 == 0) {
        #line 400 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vrIsTrailingZerosS655
        = _M0FPB18multipleOfPowerOf5(_M0L2mvS648, _M0L1qS656);
      } else if (_M0L4evenS647) {
        uint64_t _M0L6_2atmpS2595 = _M0L2mvS648 - 1ull;
        uint64_t _M0L6_2atmpS2596;
        uint64_t _M0L6_2atmpS2594;
        #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2596 = _M0MPC14bool4Bool10to__uint64(_M0L7mmShiftS649);
        _M0L6_2atmpS2594 = _M0L6_2atmpS2595 - _M0L6_2atmpS2596;
        #line 405 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0Lm17vmIsTrailingZerosS654
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS2594, _M0L1qS656);
      } else {
        uint64_t _M0L6_2atmpS2597 = _M0Lm2vpS651;
        uint64_t _M0L6_2atmpS2600 = _M0L2mvS648 + 2ull;
        int32_t _M0L6_2atmpS2599;
        uint64_t _M0L6_2atmpS2598;
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2599
        = _M0FPB18multipleOfPowerOf5(_M0L6_2atmpS2600, _M0L1qS656);
        #line 410 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2598 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS2599);
        _M0Lm2vpS651 = _M0L6_2atmpS2597 - _M0L6_2atmpS2598;
      }
    }
  } else {
    int32_t _M0L6_2atmpS2629 = _M0Lm2e2S643;
    int32_t _M0L6_2atmpS2628 = -_M0L6_2atmpS2629;
    int32_t _M0L6_2atmpS2623;
    int32_t _M0L6_2atmpS2627;
    int32_t _M0L6_2atmpS2626;
    int32_t _M0L6_2atmpS2625;
    int32_t _M0L6_2atmpS2624;
    int32_t _M0L1qS665;
    int32_t _M0L6_2atmpS2616;
    int32_t _M0L6_2atmpS2622;
    int32_t _M0L6_2atmpS2621;
    int32_t _M0L1iS666;
    int32_t _M0L6_2atmpS2620;
    int32_t _M0L1kS667;
    int32_t _M0L1jS668;
    struct _M0TPB8Pow5Pair _M0L4pow5S669;
    uint64_t _M0L6_2atmpS2619;
    struct _M0TPB19MulShiftAll64Result _M0L7_2abindS670;
    uint64_t _M0L8_2avrOutS671;
    uint64_t _M0L8_2avpOutS672;
    uint64_t _M0L8_2avmOutS673;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2623 = _M0FPB9log10Pow5(_M0L6_2atmpS2628);
    _M0L6_2atmpS2627 = _M0Lm2e2S643;
    _M0L6_2atmpS2626 = -_M0L6_2atmpS2627;
    _M0L6_2atmpS2625 = _M0L6_2atmpS2626 > 1;
    #line 415 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2624 = _M0MPC14bool4Bool7to__int(_M0L6_2atmpS2625);
    _M0L1qS665 = _M0L6_2atmpS2623 - _M0L6_2atmpS2624;
    _M0L6_2atmpS2616 = _M0Lm2e2S643;
    _M0Lm3e10S653 = _M0L1qS665 + _M0L6_2atmpS2616;
    _M0L6_2atmpS2622 = _M0Lm2e2S643;
    _M0L6_2atmpS2621 = -_M0L6_2atmpS2622;
    _M0L1iS666 = _M0L6_2atmpS2621 - _M0L1qS665;
    #line 418 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2620 = _M0FPB8pow5bits(_M0L1iS666);
    _M0L1kS667 = _M0L6_2atmpS2620 - 125;
    _M0L1jS668 = _M0L1qS665 - _M0L1kS667;
    #line 420 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L4pow5S669 = _M0FPB19double__computePow5(_M0L1iS666);
    _M0L6_2atmpS2619 = _M0Lm2m2S644;
    #line 421 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L7_2abindS670
    = _M0FPB13mulShiftAll64(_M0L6_2atmpS2619, _M0L4pow5S669, _M0L1jS668, _M0L7mmShiftS649);
    _M0L8_2avrOutS671 = _M0L7_2abindS670.$0;
    _M0L8_2avpOutS672 = _M0L7_2abindS670.$1;
    _M0L8_2avmOutS673 = _M0L7_2abindS670.$2;
    _M0Lm2vrS650 = _M0L8_2avrOutS671;
    _M0Lm2vpS651 = _M0L8_2avpOutS672;
    _M0Lm2vmS652 = _M0L8_2avmOutS673;
    if (_M0L1qS665 <= 1) {
      _M0Lm17vrIsTrailingZerosS655 = 1;
      if (_M0L4evenS647) {
        int32_t _M0L6_2atmpS2617;
        #line 432 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        _M0L6_2atmpS2617 = _M0MPC14bool4Bool7to__int(_M0L7mmShiftS649);
        _M0Lm17vmIsTrailingZerosS654 = _M0L6_2atmpS2617 == 1;
      } else {
        uint64_t _M0L6_2atmpS2618 = _M0Lm2vpS651;
        _M0Lm2vpS651 = _M0L6_2atmpS2618 - 1ull;
      }
    } else if (_M0L1qS665 < 63) {
      #line 437 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0Lm17vrIsTrailingZerosS655
      = _M0FPB18multipleOfPowerOf2(_M0L2mvS648, _M0L1qS665);
    }
  }
  _M0Lm7removedS674 = 0;
  _M0Lm16lastRemovedDigitS675 = 0;
  _M0Lm6outputS676 = 0ull;
  if (_M0Lm17vmIsTrailingZerosS654 || _M0Lm17vrIsTrailingZerosS655) {
    int32_t _if__result_5965;
    uint64_t _M0L6_2atmpS2659;
    uint64_t _M0L6_2atmpS2665;
    uint64_t _M0L6_2atmpS2666;
    int32_t _if__result_5966;
    int32_t _M0L6_2atmpS2662;
    int64_t _M0L6_2atmpS2661;
    uint64_t _M0L6_2atmpS2660;
    while (1) {
      uint64_t _M0L6_2atmpS2642 = _M0Lm2vpS651;
      uint64_t _M0L7vpDiv10S677 = _M0L6_2atmpS2642 / 10ull;
      uint64_t _M0L6_2atmpS2641 = _M0Lm2vmS652;
      uint64_t _M0L7vmDiv10S678 = _M0L6_2atmpS2641 / 10ull;
      uint64_t _M0L6_2atmpS2640;
      int32_t _M0L6_2atmpS2637;
      int32_t _M0L6_2atmpS2639;
      int32_t _M0L6_2atmpS2638;
      int32_t _M0L7vmMod10S680;
      uint64_t _M0L6_2atmpS2636;
      uint64_t _M0L7vrDiv10S681;
      uint64_t _M0L6_2atmpS2635;
      int32_t _M0L6_2atmpS2632;
      int32_t _M0L6_2atmpS2634;
      int32_t _M0L6_2atmpS2633;
      int32_t _M0L7vrMod10S682;
      int32_t _M0L6_2atmpS2631;
      if (_M0L7vpDiv10S677 <= _M0L7vmDiv10S678) {
        break;
      }
      _M0L6_2atmpS2640 = _M0Lm2vmS652;
      _M0L6_2atmpS2637 = (int32_t)_M0L6_2atmpS2640;
      _M0L6_2atmpS2639 = (int32_t)_M0L7vmDiv10S678;
      _M0L6_2atmpS2638 = 10 * _M0L6_2atmpS2639;
      _M0L7vmMod10S680 = _M0L6_2atmpS2637 - _M0L6_2atmpS2638;
      _M0L6_2atmpS2636 = _M0Lm2vrS650;
      _M0L7vrDiv10S681 = _M0L6_2atmpS2636 / 10ull;
      _M0L6_2atmpS2635 = _M0Lm2vrS650;
      _M0L6_2atmpS2632 = (int32_t)_M0L6_2atmpS2635;
      _M0L6_2atmpS2634 = (int32_t)_M0L7vrDiv10S681;
      _M0L6_2atmpS2633 = 10 * _M0L6_2atmpS2634;
      _M0L7vrMod10S682 = _M0L6_2atmpS2632 - _M0L6_2atmpS2633;
      _M0Lm17vmIsTrailingZerosS654
      = _M0Lm17vmIsTrailingZerosS654 && _M0L7vmMod10S680 == 0;
      if (_M0Lm17vrIsTrailingZerosS655) {
        int32_t _M0L6_2atmpS2630 = _M0Lm16lastRemovedDigitS675;
        _M0Lm17vrIsTrailingZerosS655 = _M0L6_2atmpS2630 == 0;
      } else {
        _M0Lm17vrIsTrailingZerosS655 = 0;
      }
      _M0Lm16lastRemovedDigitS675 = _M0L7vrMod10S682;
      _M0Lm2vrS650 = _M0L7vrDiv10S681;
      _M0Lm2vpS651 = _M0L7vpDiv10S677;
      _M0Lm2vmS652 = _M0L7vmDiv10S678;
      _M0L6_2atmpS2631 = _M0Lm7removedS674;
      _M0Lm7removedS674 = _M0L6_2atmpS2631 + 1;
      continue;
      break;
    }
    if (_M0Lm17vmIsTrailingZerosS654) {
      while (1) {
        uint64_t _M0L6_2atmpS2655 = _M0Lm2vmS652;
        uint64_t _M0L7vmDiv10S683 = _M0L6_2atmpS2655 / 10ull;
        uint64_t _M0L6_2atmpS2654 = _M0Lm2vmS652;
        int32_t _M0L6_2atmpS2651 = (int32_t)_M0L6_2atmpS2654;
        int32_t _M0L6_2atmpS2653 = (int32_t)_M0L7vmDiv10S683;
        int32_t _M0L6_2atmpS2652 = 10 * _M0L6_2atmpS2653;
        int32_t _M0L7vmMod10S684 = _M0L6_2atmpS2651 - _M0L6_2atmpS2652;
        uint64_t _M0L6_2atmpS2650;
        uint64_t _M0L7vpDiv10S686;
        uint64_t _M0L6_2atmpS2649;
        uint64_t _M0L7vrDiv10S687;
        uint64_t _M0L6_2atmpS2648;
        int32_t _M0L6_2atmpS2645;
        int32_t _M0L6_2atmpS2647;
        int32_t _M0L6_2atmpS2646;
        int32_t _M0L7vrMod10S688;
        int32_t _M0L6_2atmpS2644;
        if (_M0L7vmMod10S684 != 0) {
          break;
        }
        _M0L6_2atmpS2650 = _M0Lm2vpS651;
        _M0L7vpDiv10S686 = _M0L6_2atmpS2650 / 10ull;
        _M0L6_2atmpS2649 = _M0Lm2vrS650;
        _M0L7vrDiv10S687 = _M0L6_2atmpS2649 / 10ull;
        _M0L6_2atmpS2648 = _M0Lm2vrS650;
        _M0L6_2atmpS2645 = (int32_t)_M0L6_2atmpS2648;
        _M0L6_2atmpS2647 = (int32_t)_M0L7vrDiv10S687;
        _M0L6_2atmpS2646 = 10 * _M0L6_2atmpS2647;
        _M0L7vrMod10S688 = _M0L6_2atmpS2645 - _M0L6_2atmpS2646;
        if (_M0Lm17vrIsTrailingZerosS655) {
          int32_t _M0L6_2atmpS2643 = _M0Lm16lastRemovedDigitS675;
          _M0Lm17vrIsTrailingZerosS655 = _M0L6_2atmpS2643 == 0;
        } else {
          _M0Lm17vrIsTrailingZerosS655 = 0;
        }
        _M0Lm16lastRemovedDigitS675 = _M0L7vrMod10S688;
        _M0Lm2vrS650 = _M0L7vrDiv10S687;
        _M0Lm2vpS651 = _M0L7vpDiv10S686;
        _M0Lm2vmS652 = _M0L7vmDiv10S683;
        _M0L6_2atmpS2644 = _M0Lm7removedS674;
        _M0Lm7removedS674 = _M0L6_2atmpS2644 + 1;
        continue;
        break;
      }
    }
    if (_M0Lm17vrIsTrailingZerosS655) {
      int32_t _M0L6_2atmpS2658 = _M0Lm16lastRemovedDigitS675;
      if (_M0L6_2atmpS2658 == 5) {
        uint64_t _M0L6_2atmpS2657 = _M0Lm2vrS650;
        uint64_t _M0L6_2atmpS2656 = _M0L6_2atmpS2657 % 2ull;
        _if__result_5965 = _M0L6_2atmpS2656 == 0ull;
      } else {
        _if__result_5965 = 0;
      }
    } else {
      _if__result_5965 = 0;
    }
    if (_if__result_5965) {
      _M0Lm16lastRemovedDigitS675 = 4;
    }
    _M0L6_2atmpS2659 = _M0Lm2vrS650;
    _M0L6_2atmpS2665 = _M0Lm2vrS650;
    _M0L6_2atmpS2666 = _M0Lm2vmS652;
    if (_M0L6_2atmpS2665 == _M0L6_2atmpS2666) {
      if (!_M0L4evenS647) {
        _if__result_5966 = 1;
      } else {
        int32_t _M0L6_2atmpS2664 = _M0Lm17vmIsTrailingZerosS654;
        _if__result_5966 = !_M0L6_2atmpS2664;
      }
    } else {
      _if__result_5966 = 0;
    }
    if (_if__result_5966) {
      _M0L6_2atmpS2662 = 1;
    } else {
      int32_t _M0L6_2atmpS2663 = _M0Lm16lastRemovedDigitS675;
      _M0L6_2atmpS2662 = _M0L6_2atmpS2663 >= 5;
    }
    #line 487 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2661 = _M0MPC14bool4Bool9to__int64(_M0L6_2atmpS2662);
    _M0L6_2atmpS2660 = *(uint64_t*)&_M0L6_2atmpS2661;
    _M0Lm6outputS676 = _M0L6_2atmpS2659 + _M0L6_2atmpS2660;
  } else {
    int32_t _M0Lm7roundUpS689 = 0;
    uint64_t _M0L6_2atmpS2687 = _M0Lm2vpS651;
    uint64_t _M0L8vpDiv100S690 = _M0L6_2atmpS2687 / 100ull;
    uint64_t _M0L6_2atmpS2686 = _M0Lm2vmS652;
    uint64_t _M0L8vmDiv100S691 = _M0L6_2atmpS2686 / 100ull;
    uint64_t _M0L6_2atmpS2681;
    uint64_t _M0L6_2atmpS2684;
    uint64_t _M0L6_2atmpS2685;
    int32_t _M0L6_2atmpS2683;
    uint64_t _M0L6_2atmpS2682;
    if (_M0L8vpDiv100S690 > _M0L8vmDiv100S691) {
      uint64_t _M0L6_2atmpS2672 = _M0Lm2vrS650;
      uint64_t _M0L8vrDiv100S692 = _M0L6_2atmpS2672 / 100ull;
      uint64_t _M0L6_2atmpS2671 = _M0Lm2vrS650;
      int32_t _M0L6_2atmpS2668 = (int32_t)_M0L6_2atmpS2671;
      int32_t _M0L6_2atmpS2670 = (int32_t)_M0L8vrDiv100S692;
      int32_t _M0L6_2atmpS2669 = 100 * _M0L6_2atmpS2670;
      int32_t _M0L8vrMod100S693 = _M0L6_2atmpS2668 - _M0L6_2atmpS2669;
      int32_t _M0L6_2atmpS2667;
      _M0Lm7roundUpS689 = _M0L8vrMod100S693 >= 50;
      _M0Lm2vrS650 = _M0L8vrDiv100S692;
      _M0Lm2vpS651 = _M0L8vpDiv100S690;
      _M0Lm2vmS652 = _M0L8vmDiv100S691;
      _M0L6_2atmpS2667 = _M0Lm7removedS674;
      _M0Lm7removedS674 = _M0L6_2atmpS2667 + 2;
    }
    while (1) {
      uint64_t _M0L6_2atmpS2680 = _M0Lm2vpS651;
      uint64_t _M0L7vpDiv10S694 = _M0L6_2atmpS2680 / 10ull;
      uint64_t _M0L6_2atmpS2679 = _M0Lm2vmS652;
      uint64_t _M0L7vmDiv10S695 = _M0L6_2atmpS2679 / 10ull;
      uint64_t _M0L6_2atmpS2678;
      uint64_t _M0L7vrDiv10S697;
      uint64_t _M0L6_2atmpS2677;
      int32_t _M0L6_2atmpS2674;
      int32_t _M0L6_2atmpS2676;
      int32_t _M0L6_2atmpS2675;
      int32_t _M0L7vrMod10S698;
      int32_t _M0L6_2atmpS2673;
      if (_M0L7vpDiv10S694 <= _M0L7vmDiv10S695) {
        break;
      }
      _M0L6_2atmpS2678 = _M0Lm2vrS650;
      _M0L7vrDiv10S697 = _M0L6_2atmpS2678 / 10ull;
      _M0L6_2atmpS2677 = _M0Lm2vrS650;
      _M0L6_2atmpS2674 = (int32_t)_M0L6_2atmpS2677;
      _M0L6_2atmpS2676 = (int32_t)_M0L7vrDiv10S697;
      _M0L6_2atmpS2675 = 10 * _M0L6_2atmpS2676;
      _M0L7vrMod10S698 = _M0L6_2atmpS2674 - _M0L6_2atmpS2675;
      _M0Lm7roundUpS689 = _M0L7vrMod10S698 >= 5;
      _M0Lm2vrS650 = _M0L7vrDiv10S697;
      _M0Lm2vpS651 = _M0L7vpDiv10S694;
      _M0Lm2vmS652 = _M0L7vmDiv10S695;
      _M0L6_2atmpS2673 = _M0Lm7removedS674;
      _M0Lm7removedS674 = _M0L6_2atmpS2673 + 1;
      continue;
      break;
    }
    _M0L6_2atmpS2681 = _M0Lm2vrS650;
    _M0L6_2atmpS2684 = _M0Lm2vrS650;
    _M0L6_2atmpS2685 = _M0Lm2vmS652;
    _M0L6_2atmpS2683
    = _M0L6_2atmpS2684 == _M0L6_2atmpS2685 || _M0Lm7roundUpS689;
    #line 522 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0L6_2atmpS2682 = _M0MPC14bool4Bool10to__uint64(_M0L6_2atmpS2683);
    _M0Lm6outputS676 = _M0L6_2atmpS2681 + _M0L6_2atmpS2682;
  }
  _M0L6_2atmpS2689 = _M0Lm3e10S653;
  _M0L6_2atmpS2690 = _M0Lm7removedS674;
  _M0L3expS699 = _M0L6_2atmpS2689 + _M0L6_2atmpS2690;
  _M0L6_2atmpS2688 = _M0Lm6outputS676;
  _block_5968
  = (struct _M0TPB17FloatingDecimal64*)moonbit_malloc(sizeof(struct _M0TPB17FloatingDecimal64));
  Moonbit_object_header(_block_5968)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR, 0, 0);
  _block_5968->$0 = _M0L6_2atmpS2688;
  _block_5968->$1 = _M0L3expS699;
  return _block_5968;
}

uint64_t _M0MPC14bool4Bool10to__uint64(int32_t _M0L4selfS642) {
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS642) {
    return 1ull;
  } else {
    return 0ull;
  }
}

int64_t _M0MPC14bool4Bool9to__int64(int32_t _M0L4selfS641) {
  #line 58 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS641) {
    return 1ll;
  } else {
    return 0ll;
  }
}

int32_t _M0MPC14bool4Bool7to__int(int32_t _M0L4selfS640) {
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bool.mbt"
  if (_M0L4selfS640) {
    return 1;
  } else {
    return 0;
  }
}

int32_t _M0FPB17decimal__length17(uint64_t _M0L1vS639) {
  #line 280 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L1vS639 >= 10000000000000000ull) {
    return 17;
  }
  if (_M0L1vS639 >= 1000000000000000ull) {
    return 16;
  }
  if (_M0L1vS639 >= 100000000000000ull) {
    return 15;
  }
  if (_M0L1vS639 >= 10000000000000ull) {
    return 14;
  }
  if (_M0L1vS639 >= 1000000000000ull) {
    return 13;
  }
  if (_M0L1vS639 >= 100000000000ull) {
    return 12;
  }
  if (_M0L1vS639 >= 10000000000ull) {
    return 11;
  }
  if (_M0L1vS639 >= 1000000000ull) {
    return 10;
  }
  if (_M0L1vS639 >= 100000000ull) {
    return 9;
  }
  if (_M0L1vS639 >= 10000000ull) {
    return 8;
  }
  if (_M0L1vS639 >= 1000000ull) {
    return 7;
  }
  if (_M0L1vS639 >= 100000ull) {
    return 6;
  }
  if (_M0L1vS639 >= 10000ull) {
    return 5;
  }
  if (_M0L1vS639 >= 1000ull) {
    return 4;
  }
  if (_M0L1vS639 >= 100ull) {
    return 3;
  }
  if (_M0L1vS639 >= 10ull) {
    return 2;
  }
  return 1;
}

struct _M0TPB8Pow5Pair _M0FPB22double__computeInvPow5(int32_t _M0L1iS622) {
  int32_t _M0L6_2atmpS2589;
  int32_t _M0L6_2atmpS2588;
  int32_t _M0L4baseS621;
  int32_t _M0L5base2S623;
  int32_t _M0L6offsetS624;
  int32_t _M0L6_2atmpS2587;
  uint64_t _M0L4mul0S625;
  int32_t _M0L6_2atmpS2586;
  int32_t _M0L6_2atmpS2585;
  uint64_t _M0L4mul1S626;
  uint64_t _M0L1mS627;
  struct _M0TPB7Umul128 _M0L7_2abindS628;
  uint64_t _M0L7_2alow1S629;
  uint64_t _M0L8_2ahigh1S630;
  struct _M0TPB7Umul128 _M0L7_2abindS631;
  uint64_t _M0L7_2alow0S632;
  uint64_t _M0L8_2ahigh0S633;
  uint64_t _M0L3sumS634;
  uint64_t _M0Lm5high1S635;
  int32_t _M0L6_2atmpS2583;
  int32_t _M0L6_2atmpS2584;
  int32_t _M0L5deltaS636;
  uint64_t _M0L6_2atmpS2582;
  uint64_t _M0L6_2atmpS2574;
  int32_t _M0L6_2atmpS2581;
  uint32_t _M0L6_2atmpS2578;
  int32_t _M0L6_2atmpS2580;
  int32_t _M0L6_2atmpS2579;
  uint32_t _M0L6_2atmpS2577;
  uint32_t _M0L6_2atmpS2576;
  uint64_t _M0L6_2atmpS2575;
  uint64_t _M0L1aS637;
  uint64_t _M0L6_2atmpS2573;
  uint64_t _M0L1bS638;
  #line 239 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2589 = _M0L1iS622 + 26;
  _M0L6_2atmpS2588 = _M0L6_2atmpS2589 - 1;
  _M0L4baseS621 = _M0L6_2atmpS2588 / 26;
  _M0L5base2S623 = _M0L4baseS621 * 26;
  _M0L6offsetS624 = _M0L5base2S623 - _M0L1iS622;
  _M0L6_2atmpS2587 = _M0L4baseS621 * 2;
  #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S625
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS2587);
  _M0L6_2atmpS2586 = _M0L4baseS621 * 2;
  _M0L6_2atmpS2585 = _M0L6_2atmpS2586 + 1;
  #line 244 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S626
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB26gDOUBLE__POW5__INV__SPLIT2, _M0L6_2atmpS2585);
  if (_M0L6offsetS624 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S625, .$1 = _M0L4mul1S626};
  }
  #line 248 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS627
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS624);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS628 = _M0FPB7umul128(_M0L1mS627, _M0L4mul1S626);
  _M0L7_2alow1S629 = _M0L7_2abindS628.$0;
  _M0L8_2ahigh1S630 = _M0L7_2abindS628.$1;
  #line 250 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS631 = _M0FPB7umul128(_M0L1mS627, _M0L4mul0S625);
  _M0L7_2alow0S632 = _M0L7_2abindS631.$0;
  _M0L8_2ahigh0S633 = _M0L7_2abindS631.$1;
  _M0L3sumS634 = _M0L8_2ahigh0S633 + _M0L7_2alow1S629;
  _M0Lm5high1S635 = _M0L8_2ahigh1S630;
  if (_M0L3sumS634 < _M0L8_2ahigh0S633) {
    uint64_t _M0L6_2atmpS2572 = _M0Lm5high1S635;
    _M0Lm5high1S635 = _M0L6_2atmpS2572 + 1ull;
  }
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2583 = _M0FPB8pow5bits(_M0L5base2S623);
  #line 256 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2584 = _M0FPB8pow5bits(_M0L1iS622);
  _M0L5deltaS636 = _M0L6_2atmpS2583 - _M0L6_2atmpS2584;
  #line 257 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2582
  = _M0FPB13shiftright128(_M0L7_2alow0S632, _M0L3sumS634, _M0L5deltaS636);
  _M0L6_2atmpS2574 = _M0L6_2atmpS2582 + 1ull;
  _M0L6_2atmpS2581 = _M0L1iS622 / 16;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2578
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB19gPOW5__INV__OFFSETS, _M0L6_2atmpS2581);
  _M0L6_2atmpS2580 = _M0L1iS622 % 16;
  _M0L6_2atmpS2579 = _M0L6_2atmpS2580 << 1;
  _M0L6_2atmpS2577 = _M0L6_2atmpS2578 >> (_M0L6_2atmpS2579 & 31);
  _M0L6_2atmpS2576 = _M0L6_2atmpS2577 & 3u;
  #line 259 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2575 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS2576);
  _M0L1aS637 = _M0L6_2atmpS2574 + _M0L6_2atmpS2575;
  _M0L6_2atmpS2573 = _M0Lm5high1S635;
  #line 260 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS638
  = _M0FPB13shiftright128(_M0L3sumS634, _M0L6_2atmpS2573, _M0L5deltaS636);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS637, .$1 = _M0L1bS638};
}

struct _M0TPB8Pow5Pair _M0FPB19double__computePow5(int32_t _M0L1iS604) {
  int32_t _M0L4baseS603;
  int32_t _M0L5base2S605;
  int32_t _M0L6offsetS606;
  int32_t _M0L6_2atmpS2571;
  uint64_t _M0L4mul0S607;
  int32_t _M0L6_2atmpS2570;
  int32_t _M0L6_2atmpS2569;
  uint64_t _M0L4mul1S608;
  uint64_t _M0L1mS609;
  struct _M0TPB7Umul128 _M0L7_2abindS610;
  uint64_t _M0L7_2alow1S611;
  uint64_t _M0L8_2ahigh1S612;
  struct _M0TPB7Umul128 _M0L7_2abindS613;
  uint64_t _M0L7_2alow0S614;
  uint64_t _M0L8_2ahigh0S615;
  uint64_t _M0L3sumS616;
  uint64_t _M0Lm5high1S617;
  int32_t _M0L6_2atmpS2567;
  int32_t _M0L6_2atmpS2568;
  int32_t _M0L5deltaS618;
  uint64_t _M0L6_2atmpS2559;
  int32_t _M0L6_2atmpS2566;
  uint32_t _M0L6_2atmpS2563;
  int32_t _M0L6_2atmpS2565;
  int32_t _M0L6_2atmpS2564;
  uint32_t _M0L6_2atmpS2562;
  uint32_t _M0L6_2atmpS2561;
  uint64_t _M0L6_2atmpS2560;
  uint64_t _M0L1aS619;
  uint64_t _M0L6_2atmpS2558;
  uint64_t _M0L1bS620;
  #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4baseS603 = _M0L1iS604 / 26;
  _M0L5base2S605 = _M0L4baseS603 * 26;
  _M0L6offsetS606 = _M0L1iS604 - _M0L5base2S605;
  _M0L6_2atmpS2571 = _M0L4baseS603 * 2;
  #line 217 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul0S607
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS2571);
  _M0L6_2atmpS2570 = _M0L4baseS603 * 2;
  _M0L6_2atmpS2569 = _M0L6_2atmpS2570 + 1;
  #line 218 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L4mul1S608
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB21gDOUBLE__POW5__SPLIT2, _M0L6_2atmpS2569);
  if (_M0L6offsetS606 == 0) {
    return (struct _M0TPB8Pow5Pair){.$0 = _M0L4mul0S607, .$1 = _M0L4mul1S608};
  }
  #line 222 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1mS609
  = _M0MPC15array13ReadOnlyArray2atGmE(_M0FPB20gDOUBLE__POW5__TABLE, _M0L6offsetS606);
  #line 223 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS610 = _M0FPB7umul128(_M0L1mS609, _M0L4mul1S608);
  _M0L7_2alow1S611 = _M0L7_2abindS610.$0;
  _M0L8_2ahigh1S612 = _M0L7_2abindS610.$1;
  #line 224 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS613 = _M0FPB7umul128(_M0L1mS609, _M0L4mul0S607);
  _M0L7_2alow0S614 = _M0L7_2abindS613.$0;
  _M0L8_2ahigh0S615 = _M0L7_2abindS613.$1;
  _M0L3sumS616 = _M0L8_2ahigh0S615 + _M0L7_2alow1S611;
  _M0Lm5high1S617 = _M0L8_2ahigh1S612;
  if (_M0L3sumS616 < _M0L8_2ahigh0S615) {
    uint64_t _M0L6_2atmpS2557 = _M0Lm5high1S617;
    _M0Lm5high1S617 = _M0L6_2atmpS2557 + 1ull;
  }
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2567 = _M0FPB8pow5bits(_M0L1iS604);
  #line 230 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2568 = _M0FPB8pow5bits(_M0L5base2S605);
  _M0L5deltaS618 = _M0L6_2atmpS2567 - _M0L6_2atmpS2568;
  #line 231 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2559
  = _M0FPB13shiftright128(_M0L7_2alow0S614, _M0L3sumS616, _M0L5deltaS618);
  _M0L6_2atmpS2566 = _M0L1iS604 / 16;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2563
  = _M0MPC15array13ReadOnlyArray2atGjE(_M0FPB14gPOW5__OFFSETS, _M0L6_2atmpS2566);
  _M0L6_2atmpS2565 = _M0L1iS604 % 16;
  _M0L6_2atmpS2564 = _M0L6_2atmpS2565 << 1;
  _M0L6_2atmpS2562 = _M0L6_2atmpS2563 >> (_M0L6_2atmpS2564 & 31);
  _M0L6_2atmpS2561 = _M0L6_2atmpS2562 & 3u;
  #line 232 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2560 = _M0MPC14uint4UInt10to__uint64(_M0L6_2atmpS2561);
  _M0L1aS619 = _M0L6_2atmpS2559 + _M0L6_2atmpS2560;
  _M0L6_2atmpS2558 = _M0Lm5high1S617;
  #line 233 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L1bS620
  = _M0FPB13shiftright128(_M0L3sumS616, _M0L6_2atmpS2558, _M0L5deltaS618);
  return (struct _M0TPB8Pow5Pair){.$0 = _M0L1aS619, .$1 = _M0L1bS620};
}

struct _M0TPB19MulShiftAll64Result _M0FPB13mulShiftAll64(
  uint64_t _M0L1mS577,
  struct _M0TPB8Pow5Pair _M0L3mulS574,
  int32_t _M0L1jS590,
  int32_t _M0L7mmShiftS592
) {
  uint64_t _M0L7_2amul0S573;
  uint64_t _M0L7_2amul1S575;
  uint64_t _M0L1mS576;
  struct _M0TPB7Umul128 _M0L7_2abindS578;
  uint64_t _M0L5_2aloS579;
  uint64_t _M0L6_2atmpS580;
  struct _M0TPB7Umul128 _M0L7_2abindS581;
  uint64_t _M0L6_2alo2S582;
  uint64_t _M0L6_2ahi2S583;
  uint64_t _M0L3midS584;
  uint64_t _M0L6_2atmpS2556;
  uint64_t _M0L2hiS585;
  uint64_t _M0L3lo2S586;
  uint64_t _M0L6_2atmpS2554;
  uint64_t _M0L6_2atmpS2555;
  uint64_t _M0L4mid2S587;
  uint64_t _M0L6_2atmpS2553;
  uint64_t _M0L3hi2S588;
  int32_t _M0L6_2atmpS2552;
  int32_t _M0L6_2atmpS2551;
  uint64_t _M0L2vpS589;
  uint64_t _M0Lm2vmS591;
  int32_t _M0L6_2atmpS2550;
  int32_t _M0L6_2atmpS2549;
  uint64_t _M0L2vrS602;
  uint64_t _M0L6_2atmpS2548;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2amul0S573 = _M0L3mulS574.$0;
  _M0L7_2amul1S575 = _M0L3mulS574.$1;
  _M0L1mS576 = _M0L1mS577 << 1;
  #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS578 = _M0FPB7umul128(_M0L1mS576, _M0L7_2amul0S573);
  _M0L5_2aloS579 = _M0L7_2abindS578.$0;
  _M0L6_2atmpS580 = _M0L7_2abindS578.$1;
  #line 138 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L7_2abindS581 = _M0FPB7umul128(_M0L1mS576, _M0L7_2amul1S575);
  _M0L6_2alo2S582 = _M0L7_2abindS581.$0;
  _M0L6_2ahi2S583 = _M0L7_2abindS581.$1;
  _M0L3midS584 = _M0L6_2atmpS580 + _M0L6_2alo2S582;
  if (_M0L3midS584 < _M0L6_2atmpS580) {
    _M0L6_2atmpS2556 = 1ull;
  } else {
    _M0L6_2atmpS2556 = 0ull;
  }
  _M0L2hiS585 = _M0L6_2ahi2S583 + _M0L6_2atmpS2556;
  _M0L3lo2S586 = _M0L5_2aloS579 + _M0L7_2amul0S573;
  _M0L6_2atmpS2554 = _M0L3midS584 + _M0L7_2amul1S575;
  if (_M0L3lo2S586 < _M0L5_2aloS579) {
    _M0L6_2atmpS2555 = 1ull;
  } else {
    _M0L6_2atmpS2555 = 0ull;
  }
  _M0L4mid2S587 = _M0L6_2atmpS2554 + _M0L6_2atmpS2555;
  if (_M0L4mid2S587 < _M0L3midS584) {
    _M0L6_2atmpS2553 = 1ull;
  } else {
    _M0L6_2atmpS2553 = 0ull;
  }
  _M0L3hi2S588 = _M0L2hiS585 + _M0L6_2atmpS2553;
  _M0L6_2atmpS2552 = _M0L1jS590 - 64;
  _M0L6_2atmpS2551 = _M0L6_2atmpS2552 - 1;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vpS589
  = _M0FPB13shiftright128(_M0L4mid2S587, _M0L3hi2S588, _M0L6_2atmpS2551);
  _M0Lm2vmS591 = 0ull;
  if (_M0L7mmShiftS592) {
    uint64_t _M0L3lo3S593 = _M0L5_2aloS579 - _M0L7_2amul0S573;
    uint64_t _M0L6_2atmpS2538 = _M0L3midS584 - _M0L7_2amul1S575;
    uint64_t _M0L6_2atmpS2539;
    uint64_t _M0L4mid3S594;
    uint64_t _M0L6_2atmpS2537;
    uint64_t _M0L3hi3S595;
    int32_t _M0L6_2atmpS2536;
    int32_t _M0L6_2atmpS2535;
    if (_M0L5_2aloS579 < _M0L3lo3S593) {
      _M0L6_2atmpS2539 = 1ull;
    } else {
      _M0L6_2atmpS2539 = 0ull;
    }
    _M0L4mid3S594 = _M0L6_2atmpS2538 - _M0L6_2atmpS2539;
    if (_M0L3midS584 < _M0L4mid3S594) {
      _M0L6_2atmpS2537 = 1ull;
    } else {
      _M0L6_2atmpS2537 = 0ull;
    }
    _M0L3hi3S595 = _M0L2hiS585 - _M0L6_2atmpS2537;
    _M0L6_2atmpS2536 = _M0L1jS590 - 64;
    _M0L6_2atmpS2535 = _M0L6_2atmpS2536 - 1;
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS591
    = _M0FPB13shiftright128(_M0L4mid3S594, _M0L3hi3S595, _M0L6_2atmpS2535);
  } else {
    uint64_t _M0L3lo3S596 = _M0L5_2aloS579 + _M0L5_2aloS579;
    uint64_t _M0L6_2atmpS2546 = _M0L3midS584 + _M0L3midS584;
    uint64_t _M0L6_2atmpS2547;
    uint64_t _M0L4mid3S597;
    uint64_t _M0L6_2atmpS2544;
    uint64_t _M0L6_2atmpS2545;
    uint64_t _M0L3hi3S598;
    uint64_t _M0L3lo4S599;
    uint64_t _M0L6_2atmpS2542;
    uint64_t _M0L6_2atmpS2543;
    uint64_t _M0L4mid4S600;
    uint64_t _M0L6_2atmpS2541;
    uint64_t _M0L3hi4S601;
    int32_t _M0L6_2atmpS2540;
    if (_M0L3lo3S596 < _M0L5_2aloS579) {
      _M0L6_2atmpS2547 = 1ull;
    } else {
      _M0L6_2atmpS2547 = 0ull;
    }
    _M0L4mid3S597 = _M0L6_2atmpS2546 + _M0L6_2atmpS2547;
    _M0L6_2atmpS2544 = _M0L2hiS585 + _M0L2hiS585;
    if (_M0L4mid3S597 < _M0L3midS584) {
      _M0L6_2atmpS2545 = 1ull;
    } else {
      _M0L6_2atmpS2545 = 0ull;
    }
    _M0L3hi3S598 = _M0L6_2atmpS2544 + _M0L6_2atmpS2545;
    _M0L3lo4S599 = _M0L3lo3S596 - _M0L7_2amul0S573;
    _M0L6_2atmpS2542 = _M0L4mid3S597 - _M0L7_2amul1S575;
    if (_M0L3lo3S596 < _M0L3lo4S599) {
      _M0L6_2atmpS2543 = 1ull;
    } else {
      _M0L6_2atmpS2543 = 0ull;
    }
    _M0L4mid4S600 = _M0L6_2atmpS2542 - _M0L6_2atmpS2543;
    if (_M0L4mid3S597 < _M0L4mid4S600) {
      _M0L6_2atmpS2541 = 1ull;
    } else {
      _M0L6_2atmpS2541 = 0ull;
    }
    _M0L3hi4S601 = _M0L3hi3S598 - _M0L6_2atmpS2541;
    _M0L6_2atmpS2540 = _M0L1jS590 - 64;
    #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _M0Lm2vmS591
    = _M0FPB13shiftright128(_M0L4mid4S600, _M0L3hi4S601, _M0L6_2atmpS2540);
  }
  _M0L6_2atmpS2550 = _M0L1jS590 - 64;
  _M0L6_2atmpS2549 = _M0L6_2atmpS2550 - 1;
  #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L2vrS602
  = _M0FPB13shiftright128(_M0L3midS584, _M0L2hiS585, _M0L6_2atmpS2549);
  _M0L6_2atmpS2548 = _M0Lm2vmS591;
  return (struct _M0TPB19MulShiftAll64Result){.$0 = _M0L2vrS602,
                                                .$1 = _M0L2vpS589,
                                                .$2 = _M0L6_2atmpS2548};
}

int32_t _M0FPB18multipleOfPowerOf2(
  uint64_t _M0L5valueS571,
  int32_t _M0L1pS572
) {
  uint64_t _M0L6_2atmpS2534;
  uint64_t _M0L6_2atmpS2533;
  uint64_t _M0L6_2atmpS2532;
  #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2534 = 1ull << (_M0L1pS572 & 63);
  _M0L6_2atmpS2533 = _M0L6_2atmpS2534 - 1ull;
  _M0L6_2atmpS2532 = _M0L5valueS571 & _M0L6_2atmpS2533;
  return _M0L6_2atmpS2532 == 0ull;
}

int32_t _M0FPB18multipleOfPowerOf5(
  uint64_t _M0L5valueS569,
  int32_t _M0L1pS570
) {
  int32_t _M0L6_2atmpS2531;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  #line 120 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2531 = _M0FPB10pow5Factor(_M0L5valueS569);
  return _M0L6_2atmpS2531 >= _M0L1pS570;
}

int32_t _M0FPB10pow5Factor(uint64_t _M0L5valueS564) {
  uint64_t _M0L6_2atmpS2522;
  uint64_t _M0L6_2atmpS2523;
  uint64_t _M0L6_2atmpS2524;
  uint64_t _M0L6_2atmpS2525;
  uint64_t _M0L6_2atmpS2530;
  int32_t _M0L5countS565;
  uint64_t _M0L1vS566;
  #line 94 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2522 = _M0L5valueS564 % 5ull;
  if (_M0L6_2atmpS2522 != 0ull) {
    return 0;
  }
  _M0L6_2atmpS2523 = _M0L5valueS564 % 25ull;
  if (_M0L6_2atmpS2523 != 0ull) {
    return 1;
  }
  _M0L6_2atmpS2524 = _M0L5valueS564 % 125ull;
  if (_M0L6_2atmpS2524 != 0ull) {
    return 2;
  }
  _M0L6_2atmpS2525 = _M0L5valueS564 % 625ull;
  if (_M0L6_2atmpS2525 != 0ull) {
    return 3;
  }
  _M0L6_2atmpS2530 = _M0L5valueS564 / 625ull;
  _M0L5countS565 = 4;
  _M0L1vS566 = _M0L6_2atmpS2530;
  while (1) {
    if (_M0L1vS566 > 0ull) {
      uint64_t _M0L6_2atmpS2526 = _M0L1vS566 % 5ull;
      int32_t _M0L6_2atmpS2527;
      uint64_t _M0L6_2atmpS2528;
      if (_M0L6_2atmpS2526 != 0ull) {
        return _M0L5countS565;
      }
      _M0L6_2atmpS2527 = _M0L5countS565 + 1;
      _M0L6_2atmpS2528 = _M0L1vS566 / 5ull;
      _M0L5countS565 = _M0L6_2atmpS2527;
      _M0L1vS566 = _M0L6_2atmpS2528;
      continue;
    } else {
      struct _M0TPB13StringBuilder* _M0L18_2astring__builderS568;
      moonbit_string_t _M0L6_2atmpS2529;
      int32_t _result_5970;
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L18_2astring__builderS568
      = _M0MPB13StringBuilder21StringBuilder_2einner(25);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS568, (moonbit_string_t)moonbit_string_literal_13.data);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0MPB13StringBuilder13write__objectGmE(_M0L18_2astring__builderS568, _M0L5valueS564);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _M0L6_2atmpS2529
      = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS568);
      moonbit_decref_cycle_free(_M0L18_2astring__builderS568);
      #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
      _result_5970 = _M0FPC15abort5abortGiE(_M0L6_2atmpS2529);
      moonbit_decref_cycle_free(_M0L6_2atmpS2529);
      return _result_5970;
    }
    break;
  }
}

uint64_t _M0FPB13shiftright128(
  uint64_t _M0L2loS563,
  uint64_t _M0L2hiS561,
  int32_t _M0L4distS562
) {
  int32_t _M0L6_2atmpS2521;
  uint64_t _M0L6_2atmpS2519;
  uint64_t _M0L6_2atmpS2520;
  #line 89 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2521 = 64 - _M0L4distS562;
  _M0L6_2atmpS2519 = _M0L2hiS561 << (_M0L6_2atmpS2521 & 63);
  _M0L6_2atmpS2520 = _M0L2loS563 >> (_M0L4distS562 & 63);
  return _M0L6_2atmpS2519 | _M0L6_2atmpS2520;
}

struct _M0TPB7Umul128 _M0FPB7umul128(
  uint64_t _M0L1aS551,
  uint64_t _M0L1bS554
) {
  uint64_t _M0L3aLoS550;
  uint64_t _M0L3aHiS552;
  uint64_t _M0L3bLoS553;
  uint64_t _M0L3bHiS555;
  uint64_t _M0L1xS556;
  uint64_t _M0L6_2atmpS2517;
  uint64_t _M0L6_2atmpS2518;
  uint64_t _M0L1yS557;
  uint64_t _M0L6_2atmpS2515;
  uint64_t _M0L6_2atmpS2516;
  uint64_t _M0L1zS558;
  uint64_t _M0L6_2atmpS2513;
  uint64_t _M0L6_2atmpS2514;
  uint64_t _M0L6_2atmpS2511;
  uint64_t _M0L6_2atmpS2512;
  uint64_t _M0L1wS559;
  uint64_t _M0L2loS560;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3aLoS550 = _M0L1aS551 & 4294967295ull;
  _M0L3aHiS552 = _M0L1aS551 >> 32;
  _M0L3bLoS553 = _M0L1bS554 & 4294967295ull;
  _M0L3bHiS555 = _M0L1bS554 >> 32;
  _M0L1xS556 = _M0L3aLoS550 * _M0L3bLoS553;
  _M0L6_2atmpS2517 = _M0L3aHiS552 * _M0L3bLoS553;
  _M0L6_2atmpS2518 = _M0L1xS556 >> 32;
  _M0L1yS557 = _M0L6_2atmpS2517 + _M0L6_2atmpS2518;
  _M0L6_2atmpS2515 = _M0L3aLoS550 * _M0L3bHiS555;
  _M0L6_2atmpS2516 = _M0L1yS557 & 4294967295ull;
  _M0L1zS558 = _M0L6_2atmpS2515 + _M0L6_2atmpS2516;
  _M0L6_2atmpS2513 = _M0L3aHiS552 * _M0L3bHiS555;
  _M0L6_2atmpS2514 = _M0L1yS557 >> 32;
  _M0L6_2atmpS2511 = _M0L6_2atmpS2513 + _M0L6_2atmpS2514;
  _M0L6_2atmpS2512 = _M0L1zS558 >> 32;
  _M0L1wS559 = _M0L6_2atmpS2511 + _M0L6_2atmpS2512;
  _M0L2loS560 = _M0L1aS551 * _M0L1bS554;
  return (struct _M0TPB7Umul128){.$0 = _M0L2loS560, .$1 = _M0L1wS559};
}

moonbit_string_t _M0FPB19string__from__bytes(
  moonbit_bytes_t _M0L5bytesS548,
  int32_t _M0L4fromS545,
  int32_t _M0L2toS544
) {
  int32_t _M0L3lenS543;
  int32_t _M0L6_2atmpS2510;
  uint16_t* _M0L6bufferS546;
  int32_t _M0L1iS547;
  #line 52 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L3lenS543 = _M0L2toS544 - _M0L4fromS545;
  #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2510 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L6bufferS546
  = (uint16_t*)moonbit_make_string(_M0L3lenS543, _M0L6_2atmpS2510);
  _M0L1iS547 = 0;
  while (1) {
    if (_M0L1iS547 < _M0L3lenS543) {
      int32_t _M0L6_2atmpS2508 = _M0L4fromS545 + _M0L1iS547;
      int32_t _M0L6_2atmpS2507;
      int32_t _M0L6_2atmpS2506;
      int32_t _M0L6_2atmpS2509;
      if (
        _M0L6_2atmpS2508 < 0
        || _M0L6_2atmpS2508 >= Moonbit_array_length(_M0L5bytesS548)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6_2atmpS2507 = (int32_t)_M0L5bytesS548[_M0L6_2atmpS2508];
      _M0L6_2atmpS2506 = (uint16_t)_M0L6_2atmpS2507;
      if (
        _M0L1iS547 < 0 || _M0L1iS547 >= Moonbit_array_length(_M0L6bufferS546)
      ) {
        #line 56 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
        moonbit_panic();
      }
      _M0L6bufferS546[_M0L1iS547] = _M0L6_2atmpS2506;
      _M0L6_2atmpS2509 = _M0L1iS547 + 1;
      _M0L1iS547 = _M0L6_2atmpS2509;
      continue;
    }
    break;
  }
  return _M0L6bufferS546;
}

int32_t _M0FPB9log10Pow2(int32_t _M0L1eS542) {
  int32_t _M0L6_2atmpS2505;
  uint32_t _M0L6_2atmpS2504;
  uint32_t _M0L6_2atmpS2503;
  #line 44 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2505 = _M0L1eS542 * 78913;
  _M0L6_2atmpS2504 = *(uint32_t*)&_M0L6_2atmpS2505;
  _M0L6_2atmpS2503 = _M0L6_2atmpS2504 >> 18;
  return *(int32_t*)&_M0L6_2atmpS2503;
}

int32_t _M0FPB9log10Pow5(int32_t _M0L1eS541) {
  int32_t _M0L6_2atmpS2502;
  uint32_t _M0L6_2atmpS2501;
  uint32_t _M0L6_2atmpS2500;
  #line 37 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2502 = _M0L1eS541 * 732923;
  _M0L6_2atmpS2501 = *(uint32_t*)&_M0L6_2atmpS2502;
  _M0L6_2atmpS2500 = _M0L6_2atmpS2501 >> 20;
  return *(int32_t*)&_M0L6_2atmpS2500;
}

moonbit_string_t _M0FPB18copy__special__str(
  int32_t _M0L4signS539,
  int32_t _M0L8exponentS540,
  int32_t _M0L8mantissaS537
) {
  moonbit_string_t _M0L1sS538;
  moonbit_string_t _result_5973;
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  if (_M0L8mantissaS537) {
    return (moonbit_string_t)moonbit_string_literal_14.data;
  }
  if (_M0L4signS539) {
    _M0L1sS538 = (moonbit_string_t)moonbit_string_literal_15.data;
  } else {
    _M0L1sS538 = (moonbit_string_t)moonbit_string_literal_0.data;
  }
  if (_M0L8exponentS540) {
    moonbit_string_t _result_5972;
    #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
    _result_5972
    = moonbit_add_string(_M0L1sS538, (moonbit_string_t)moonbit_string_literal_16.data);
    moonbit_decref_cycle_free(_M0L1sS538);
    return _result_5972;
  }
  #line 31 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _result_5973
  = moonbit_add_string(_M0L1sS538, (moonbit_string_t)moonbit_string_literal_17.data);
  moonbit_decref_cycle_free(_M0L1sS538);
  return _result_5973;
}

int32_t _M0FPB8pow5bits(int32_t _M0L1eS536) {
  int32_t _M0L6_2atmpS2499;
  uint32_t _M0L6_2atmpS2498;
  uint32_t _M0L6_2atmpS2497;
  int32_t _M0L6_2atmpS2496;
  #line 18 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_ryu_nonjs.mbt"
  _M0L6_2atmpS2499 = _M0L1eS536 * 1217359;
  _M0L6_2atmpS2498 = *(uint32_t*)&_M0L6_2atmpS2499;
  _M0L6_2atmpS2497 = _M0L6_2atmpS2498 >> 19;
  _M0L6_2atmpS2496 = *(int32_t*)&_M0L6_2atmpS2497;
  return _M0L6_2atmpS2496 + 1;
}

int32_t _M0MPC16double6Double7to__int(double _M0L4selfS535) {
  #line 43 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int.mbt"
  if (_M0L4selfS535 != _M0L4selfS535) {
    return 0;
  } else if (_M0L4selfS535 >= 0x1.fffffffcp+30) {
    return 2147483647;
  } else if (_M0L4selfS535 <= -0x1p+31) {
    return (int32_t)0x80000000;
  } else {
    return (int32_t)_M0L4selfS535;
  }
}

int64_t _M0MPC16double6Double9to__int64(double _M0L4selfS534) {
  #line 46 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\double_to_int64_native.mbt"
  if (_M0L4selfS534 != _M0L4selfS534) {
    return 0ll;
  } else if (_M0L4selfS534 >= 0x1p+63) {
    return 9223372036854775807ll;
  } else if (_M0L4selfS534 <= -0x1p+63) {
    return (int64_t)0x8000000000000000ll;
  } else {
    return (int64_t)_M0L4selfS534;
  }
}

struct _M0TPB5ArrayGfE* _M0MPC15array5Array20unsafe__make__uninitGfE(
  int32_t _M0L3lenS531
) {
  float* _M0L6_2atmpS2493;
  struct _M0TPB5ArrayGfE* _block_5974;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2493 = (float*)moonbit_make_float_array_raw(_M0L3lenS531);
  _block_5974
  = (struct _M0TPB5ArrayGfE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGfE));
  Moonbit_object_header(_block_5974)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 57, 0);
  _block_5974->$0 = _M0L6_2atmpS2493;
  _block_5974->$1 = _M0L3lenS531;
  return _block_5974;
}

struct _M0TPB5ArrayGbE* _M0MPC15array5Array20unsafe__make__uninitGbE(
  int32_t _M0L3lenS532
) {
  uint8_t* _M0L6_2atmpS2494;
  struct _M0TPB5ArrayGbE* _block_5975;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2494 = (uint8_t*)moonbit_make_bytes_raw(_M0L3lenS532);
  _block_5975
  = (struct _M0TPB5ArrayGbE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGbE));
  Moonbit_object_header(_block_5975)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 84, 0);
  _block_5975->$0 = _M0L6_2atmpS2494;
  _block_5975->$1 = _M0L3lenS532;
  return _block_5975;
}

struct _M0TPB5ArrayGiE* _M0MPC15array5Array20unsafe__make__uninitGiE(
  int32_t _M0L3lenS533
) {
  int32_t* _M0L6_2atmpS2495;
  struct _M0TPB5ArrayGiE* _block_5976;
  #line 30 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2495 = (int32_t*)moonbit_make_int32_array_raw(_M0L3lenS533);
  _block_5976
  = (struct _M0TPB5ArrayGiE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGiE));
  Moonbit_object_header(_block_5976)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 66, 0);
  _block_5976->$0 = _M0L6_2atmpS2495;
  _block_5976->$1 = _M0L3lenS533;
  return _block_5976;
}

uint64_t _M0MPC15array13ReadOnlyArray2atGmE(
  uint64_t* _M0L4selfS527,
  int32_t _M0L5indexS528
) {
  uint64_t* _M0L6_2atmpS2491;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS2491 = _M0L4selfS527;
  if (
    _M0L5indexS528 < 0
    || _M0L5indexS528 >= Moonbit_array_length(_M0L6_2atmpS2491)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint64_t)_M0L6_2atmpS2491[_M0L5indexS528];
}

uint32_t _M0MPC15array13ReadOnlyArray2atGjE(
  uint32_t* _M0L4selfS529,
  int32_t _M0L5indexS530
) {
  uint32_t* _M0L6_2atmpS2492;
  #line 38 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
  _M0L6_2atmpS2492 = _M0L4selfS529;
  if (
    _M0L5indexS530 < 0
    || _M0L5indexS530 >= Moonbit_array_length(_M0L6_2atmpS2492)
  ) {
    #line 40 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\readonlyarray.mbt"
    moonbit_panic();
  }
  return (uint32_t)_M0L6_2atmpS2492[_M0L5indexS530];
}

moonbit_string_t _M0IPC16uint646UInt64PB4Show10to__string(
  uint64_t _M0L4selfS526
) {
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 51 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC16uint646UInt6418to__string_2einner(_M0L4selfS526, 10);
}

moonbit_string_t _M0IPC13int3IntPB4Show10to__string(int32_t _M0L4selfS525) {
  #line 35 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 36 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  return _M0MPC13int3Int18to__string_2einner(_M0L4selfS525, 10);
}

uint64_t _M0MPC14uint4UInt10to__uint64(uint32_t _M0L4selfS524) {
  #line 2576 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  return (uint64_t)_M0L4selfS524;
}

int32_t _M0MPC15array5Array4pushGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS512,
  moonbit_string_t _M0L5valueS514
) {
  int32_t _M0L3lenS2463;
  moonbit_string_t* _M0L6_2atmpS2465;
  int32_t _M0L6_2atmpS2464;
  int32_t _M0L6lengthS513;
  moonbit_string_t* _M0L3bufS2468;
  moonbit_string_t _M0L6_2aoldS5544;
  int32_t _M0L6_2atmpS2469;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2463 = _M0L4selfS512->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2465 = _M0MPC15array5Array6bufferGsE(_M0L4selfS512);
  _M0L6_2atmpS2464 = Moonbit_array_length(_M0L6_2atmpS2465);
  moonbit_decref_cycle_free(_M0L6_2atmpS2465);
  if (_M0L3lenS2463 == _M0L6_2atmpS2464) {
    int32_t _M0L3lenS2467 = _M0L4selfS512->$1;
    int32_t _M0L6_2atmpS2466 = _M0L3lenS2467 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGsE(_M0L4selfS512, _M0L6_2atmpS2466);
  }
  _M0L6lengthS513 = _M0L4selfS512->$1;
  _M0L3bufS2468 = _M0L4selfS512->$0;
  _M0L6_2aoldS5544 = (moonbit_string_t)_M0L3bufS2468[_M0L6lengthS513];
  moonbit_decref_cycle_free(_M0L6_2aoldS5544);
  _M0L3bufS2468[_M0L6lengthS513] = _M0L5valueS514;
  _M0L6_2atmpS2469 = _M0L6lengthS513 + 1;
  _M0L4selfS512->$1 = _M0L6_2atmpS2469;
  return 0;
}

int32_t _M0MPC15array5Array4pushGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS515,
  struct _M0TUsiE* _M0L5valueS517
) {
  int32_t _M0L3lenS2470;
  struct _M0TUsiE** _M0L6_2atmpS2472;
  int32_t _M0L6_2atmpS2471;
  int32_t _M0L6lengthS516;
  struct _M0TUsiE** _M0L3bufS2475;
  struct _M0TUsiE* _M0L6_2aoldS5545;
  int32_t _M0L6_2atmpS2476;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2470 = _M0L4selfS515->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2472 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS515);
  _M0L6_2atmpS2471 = Moonbit_array_length(_M0L6_2atmpS2472);
  moonbit_decref_cycle_free(_M0L6_2atmpS2472);
  if (_M0L3lenS2470 == _M0L6_2atmpS2471) {
    int32_t _M0L3lenS2474 = _M0L4selfS515->$1;
    int32_t _M0L6_2atmpS2473 = _M0L3lenS2474 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGUsiEE(_M0L4selfS515, _M0L6_2atmpS2473);
  }
  _M0L6lengthS516 = _M0L4selfS515->$1;
  _M0L3bufS2475 = _M0L4selfS515->$0;
  _M0L6_2aoldS5545 = (struct _M0TUsiE*)_M0L3bufS2475[_M0L6lengthS516];
  if (_M0L6_2aoldS5545) {
    moonbit_decref_cycle_free(_M0L6_2aoldS5545);
  }
  _M0L3bufS2475[_M0L6lengthS516] = _M0L5valueS517;
  _M0L6_2atmpS2476 = _M0L6lengthS516 + 1;
  _M0L4selfS515->$1 = _M0L6_2atmpS2476;
  return 0;
}

int32_t _M0MPC15array5Array4pushGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS518,
  float _M0L5valueS520
) {
  int32_t _M0L3lenS2477;
  float* _M0L6_2atmpS2479;
  int32_t _M0L6_2atmpS2478;
  int32_t _M0L6lengthS519;
  float* _M0L3bufS2482;
  int32_t _M0L6_2atmpS2483;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2477 = _M0L4selfS518->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2479 = _M0MPC15array5Array6bufferGfE(_M0L4selfS518);
  _M0L6_2atmpS2478 = Moonbit_array_length(_M0L6_2atmpS2479);
  moonbit_decref_cycle_free(_M0L6_2atmpS2479);
  if (_M0L3lenS2477 == _M0L6_2atmpS2478) {
    int32_t _M0L3lenS2481 = _M0L4selfS518->$1;
    int32_t _M0L6_2atmpS2480 = _M0L3lenS2481 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGfE(_M0L4selfS518, _M0L6_2atmpS2480);
  }
  _M0L6lengthS519 = _M0L4selfS518->$1;
  _M0L3bufS2482 = _M0L4selfS518->$0;
  _M0L3bufS2482[_M0L6lengthS519] = _M0L5valueS520;
  _M0L6_2atmpS2483 = _M0L6lengthS519 + 1;
  _M0L4selfS518->$1 = _M0L6_2atmpS2483;
  return 0;
}

int32_t _M0MPC15array5Array4pushGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS521,
  int32_t _M0L5valueS523
) {
  int32_t _M0L3lenS2484;
  int32_t* _M0L6_2atmpS2486;
  int32_t _M0L6_2atmpS2485;
  int32_t _M0L6lengthS522;
  int32_t* _M0L3bufS2489;
  int32_t _M0L6_2atmpS2490;
  #line 406 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L3lenS2484 = _M0L4selfS521->$1;
  #line 408 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L6_2atmpS2486 = _M0MPC15array5Array6bufferGiE(_M0L4selfS521);
  _M0L6_2atmpS2485 = Moonbit_array_length(_M0L6_2atmpS2486);
  moonbit_decref_cycle_free(_M0L6_2atmpS2486);
  if (_M0L3lenS2484 == _M0L6_2atmpS2485) {
    int32_t _M0L3lenS2488 = _M0L4selfS521->$1;
    int32_t _M0L6_2atmpS2487 = _M0L3lenS2488 + 1;
    #line 409 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0MPC15array5Array7reallocGiE(_M0L4selfS521, _M0L6_2atmpS2487);
  }
  _M0L6lengthS522 = _M0L4selfS521->$1;
  _M0L3bufS2489 = _M0L4selfS521->$0;
  _M0L3bufS2489[_M0L6lengthS522] = _M0L5valueS523;
  _M0L6_2atmpS2490 = _M0L6lengthS522 + 1;
  _M0L4selfS521->$1 = _M0L6_2atmpS2490;
  return 0;
}

int32_t _M0MPC15array5Array7reallocGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS497,
  int32_t _M0L8requiredS499
) {
  int32_t _M0L8old__capS496;
  int32_t _M0L3lenS2459;
  int32_t _M0L8new__capS498;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS496 = _M0MPC15array5Array8capacityGsE(_M0L4selfS497);
  _M0L3lenS2459 = _M0L4selfS497->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS498
  = _M0FPB23array__growth__capacity(_M0L8old__capS496, _M0L3lenS2459, _M0L8requiredS499);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGsE(_M0L4selfS497, _M0L8new__capS498);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS501,
  int32_t _M0L8requiredS503
) {
  int32_t _M0L8old__capS500;
  int32_t _M0L3lenS2460;
  int32_t _M0L8new__capS502;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS500 = _M0MPC15array5Array8capacityGUsiEE(_M0L4selfS501);
  _M0L3lenS2460 = _M0L4selfS501->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS502
  = _M0FPB23array__growth__capacity(_M0L8old__capS500, _M0L3lenS2460, _M0L8requiredS503);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGUsiEE(_M0L4selfS501, _M0L8new__capS502);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS505,
  int32_t _M0L8requiredS507
) {
  int32_t _M0L8old__capS504;
  int32_t _M0L3lenS2461;
  int32_t _M0L8new__capS506;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS504 = _M0MPC15array5Array8capacityGfE(_M0L4selfS505);
  _M0L3lenS2461 = _M0L4selfS505->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS506
  = _M0FPB23array__growth__capacity(_M0L8old__capS504, _M0L3lenS2461, _M0L8requiredS507);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGfE(_M0L4selfS505, _M0L8new__capS506);
  return 0;
}

int32_t _M0MPC15array5Array7reallocGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS509,
  int32_t _M0L8requiredS511
) {
  int32_t _M0L8old__capS508;
  int32_t _M0L3lenS2462;
  int32_t _M0L8new__capS510;
  #line 302 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  #line 304 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__capS508 = _M0MPC15array5Array8capacityGiE(_M0L4selfS509);
  _M0L3lenS2462 = _M0L4selfS509->$1;
  #line 305 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__capS510
  = _M0FPB23array__growth__capacity(_M0L8old__capS508, _M0L3lenS2462, _M0L8requiredS511);
  #line 306 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0MPC15array5Array14resize__bufferGiE(_M0L4selfS509, _M0L8new__capS510);
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS473,
  int32_t _M0L13new__capacityS476
) {
  moonbit_string_t* _M0L8old__bufS472;
  int32_t _M0L3lenS474;
  int32_t _M0L9copy__lenS475;
  moonbit_string_t* _M0L8new__bufS477;
  moonbit_string_t* _M0L6_2aoldS5546;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS472 = _M0L4selfS473->$0;
  _M0L3lenS474 = _M0L4selfS473->$1;
  if (_M0L3lenS474 < _M0L13new__capacityS476) {
    _M0L9copy__lenS475 = _M0L3lenS474;
  } else {
    _M0L9copy__lenS475 = _M0L13new__capacityS476;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS472);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS477
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(_M0L8old__bufS472, _M0L13new__capacityS476, _M0L9copy__lenS475, 0, 0);
  _M0L6_2aoldS5546 = _M0L4selfS473->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5546);
  _M0L4selfS473->$0 = _M0L8new__bufS477;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS479,
  int32_t _M0L13new__capacityS482
) {
  struct _M0TUsiE** _M0L8old__bufS478;
  int32_t _M0L3lenS480;
  int32_t _M0L9copy__lenS481;
  struct _M0TUsiE** _M0L8new__bufS483;
  struct _M0TUsiE** _M0L6_2aoldS5547;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS478 = _M0L4selfS479->$0;
  _M0L3lenS480 = _M0L4selfS479->$1;
  if (_M0L3lenS480 < _M0L13new__capacityS482) {
    _M0L9copy__lenS481 = _M0L3lenS480;
  } else {
    _M0L9copy__lenS481 = _M0L13new__capacityS482;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS478);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS483
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(_M0L8old__bufS478, _M0L13new__capacityS482, _M0L9copy__lenS481, 0, 0);
  _M0L6_2aoldS5547 = _M0L4selfS479->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5547);
  _M0L4selfS479->$0 = _M0L8new__bufS483;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS485,
  int32_t _M0L13new__capacityS488
) {
  float* _M0L8old__bufS484;
  int32_t _M0L3lenS486;
  int32_t _M0L9copy__lenS487;
  float* _M0L8new__bufS489;
  float* _M0L6_2aoldS5548;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS484 = _M0L4selfS485->$0;
  _M0L3lenS486 = _M0L4selfS485->$1;
  if (_M0L3lenS486 < _M0L13new__capacityS488) {
    _M0L9copy__lenS487 = _M0L3lenS486;
  } else {
    _M0L9copy__lenS487 = _M0L13new__capacityS488;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS484);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS489
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(_M0L8old__bufS484, _M0L13new__capacityS488, _M0L9copy__lenS487, 0, 0);
  _M0L6_2aoldS5548 = _M0L4selfS485->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5548);
  _M0L4selfS485->$0 = _M0L8new__bufS489;
  return 0;
}

int32_t _M0MPC15array5Array14resize__bufferGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS491,
  int32_t _M0L13new__capacityS494
) {
  int32_t* _M0L8old__bufS490;
  int32_t _M0L3lenS492;
  int32_t _M0L9copy__lenS493;
  int32_t* _M0L8new__bufS495;
  int32_t* _M0L6_2aoldS5549;
  #line 242 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8old__bufS490 = _M0L4selfS491->$0;
  _M0L3lenS492 = _M0L4selfS491->$1;
  if (_M0L3lenS492 < _M0L13new__capacityS494) {
    _M0L9copy__lenS493 = _M0L3lenS492;
  } else {
    _M0L9copy__lenS493 = _M0L13new__capacityS494;
  }
  moonbit_incref_cycle_free(_M0L8old__bufS490);
  #line 249 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8new__bufS495
  = _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(_M0L8old__bufS490, _M0L13new__capacityS494, _M0L9copy__lenS493, 0, 0);
  _M0L6_2aoldS5549 = _M0L4selfS491->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5549);
  _M0L4selfS491->$0 = _M0L8new__bufS495;
  return 0;
}

int32_t _M0MPC15array5Array8capacityGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS468
) {
  moonbit_string_t* _M0L6_2atmpS2455;
  int32_t _result_5977;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2455 = _M0MPC15array5Array6bufferGsE(_M0L4selfS468);
  _result_5977 = Moonbit_array_length(_M0L6_2atmpS2455);
  moonbit_decref_cycle_free(_M0L6_2atmpS2455);
  return _result_5977;
}

int32_t _M0MPC15array5Array8capacityGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS469
) {
  struct _M0TUsiE** _M0L6_2atmpS2456;
  int32_t _result_5978;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2456 = _M0MPC15array5Array6bufferGUsiEE(_M0L4selfS469);
  _result_5978 = Moonbit_array_length(_M0L6_2atmpS2456);
  moonbit_decref_cycle_free(_M0L6_2atmpS2456);
  return _result_5978;
}

int32_t _M0MPC15array5Array8capacityGfE(
  struct _M0TPB5ArrayGfE* _M0L4selfS470
) {
  float* _M0L6_2atmpS2457;
  int32_t _result_5979;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2457 = _M0MPC15array5Array6bufferGfE(_M0L4selfS470);
  _result_5979 = Moonbit_array_length(_M0L6_2atmpS2457);
  moonbit_decref_cycle_free(_M0L6_2atmpS2457);
  return _result_5979;
}

int32_t _M0MPC15array5Array8capacityGiE(
  struct _M0TPB5ArrayGiE* _M0L4selfS471
) {
  int32_t* _M0L6_2atmpS2458;
  int32_t _result_5980;
  #line 132 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\array.mbt"
  _M0L6_2atmpS2458 = _M0MPC15array5Array6bufferGiE(_M0L4selfS471);
  _result_5980 = Moonbit_array_length(_M0L6_2atmpS2458);
  moonbit_decref_cycle_free(_M0L6_2atmpS2458);
  return _result_5980;
}

int32_t _M0FPB23array__growth__capacity(
  int32_t _M0L7currentS464,
  int32_t _M0L3lenS462,
  int32_t _M0L8requiredS461
) {
  int32_t _M0L5startS463;
  int32_t _M0L5spaceS465;
  #line 200 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  if (_M0L8requiredS461 < _M0L3lenS462) {
    #line 203 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_18.data);
  }
  if (_M0L7currentS464 == 0) {
    _M0L5startS463 = 8;
  } else {
    _M0L5startS463 = _M0L7currentS464;
  }
  _M0L5spaceS465 = _M0L5startS463;
  while (1) {
    if (_M0L5spaceS465 < _M0L8requiredS461) {
      int32_t _M0L4nextS466 = _M0L5spaceS465 * 2;
      if (_M0L4nextS466 <= _M0L5spaceS465) {
        return _M0L8requiredS461;
      }
      _M0L5spaceS465 = _M0L4nextS466;
      continue;
    } else {
      return _M0L5spaceS465;
    }
    break;
  }
}

int32_t _M0MPC15array5Array6lengthGfE(struct _M0TPB5ArrayGfE* _M0L4selfS458) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS458->$1;
}

int32_t _M0MPC15array5Array6lengthGbE(struct _M0TPB5ArrayGbE* _M0L4selfS459) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS459->$1;
}

int32_t _M0MPC15array5Array6lengthGiE(struct _M0TPB5ArrayGiE* _M0L4selfS460) {
  #line 147 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  return _M0L4selfS460->$1;
}

float* _M0MPC15array5Array6bufferGfE(struct _M0TPB5ArrayGfE* _M0L4selfS452) {
  float* _M0L8_2afieldS5550;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5550 = _M0L4selfS452->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5550);
  return _M0L8_2afieldS5550;
}

moonbit_string_t* _M0MPC15array5Array6bufferGsE(
  struct _M0TPB5ArrayGsE* _M0L4selfS453
) {
  moonbit_string_t* _M0L8_2afieldS5551;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5551 = _M0L4selfS453->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5551);
  return _M0L8_2afieldS5551;
}

struct _M0TUsiE** _M0MPC15array5Array6bufferGUsiEE(
  struct _M0TPB5ArrayGUsiEE* _M0L4selfS454
) {
  struct _M0TUsiE** _M0L8_2afieldS5552;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5552 = _M0L4selfS454->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5552);
  return _M0L8_2afieldS5552;
}

int32_t* _M0MPC15array5Array6bufferGiE(struct _M0TPB5ArrayGiE* _M0L4selfS455) {
  int32_t* _M0L8_2afieldS5553;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5553 = _M0L4selfS455->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5553);
  return _M0L8_2afieldS5553;
}

struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0MPC15array5Array6bufferGRP26RiantR8snn__mbt14SpikingSynapseE(
  struct _M0TPB5ArrayGRP26RiantR8snn__mbt14SpikingSynapseE* _M0L4selfS456
) {
  struct _M0TP26RiantR8snn__mbt14SpikingSynapse** _M0L8_2afieldS5554;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5554 = _M0L4selfS456->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5554);
  return _M0L8_2afieldS5554;
}

uint8_t* _M0MPC15array5Array6bufferGbE(struct _M0TPB5ArrayGbE* _M0L4selfS457) {
  uint8_t* _M0L8_2afieldS5555;
  #line 192 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\arraycore_nonjs.mbt"
  _M0L8_2afieldS5555 = _M0L4selfS457->$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5555);
  return _M0L8_2afieldS5555;
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
  int32_t _M0L3endS2453;
  int32_t _M0L5startS2454;
  int32_t _M0L8str__lenS447;
  int32_t _M0L3lenS2452;
  int32_t _M0L8requiredS449;
  uint16_t* _M0L4dataS2445;
  int32_t _M0L6_2atmpS2444;
  int32_t _if__result_5982;
  uint16_t* _M0L4dataS2446;
  int32_t _M0L3lenS2447;
  moonbit_string_t _M0L6_2atmpS2448;
  int32_t _M0L6_2atmpS2449;
  int32_t _M0L3lenS2451;
  int32_t _M0L6_2atmpS2450;
  #line 158 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3endS2453 = _M0L3strS448.$2;
  _M0L5startS2454 = _M0L3strS448.$1;
  _M0L8str__lenS447 = _M0L3endS2453 - _M0L5startS2454;
  if (_M0L8str__lenS447 == 0) {
    return 0;
  }
  _M0L3lenS2452 = _M0L4selfS450->$1;
  _M0L8requiredS449 = _M0L3lenS2452 + _M0L8str__lenS447;
  _M0L4dataS2445 = _M0L4selfS450->$0;
  _M0L6_2atmpS2444 = Moonbit_array_length(_M0L4dataS2445);
  if (_M0L8requiredS449 > _M0L6_2atmpS2444) {
    _if__result_5982 = 1;
  } else {
    int32_t _M0L3lenS2443 = _M0L4selfS450->$1;
    _if__result_5982 = _M0L8requiredS449 < _M0L3lenS2443;
  }
  if (_if__result_5982) {
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS450, _M0L8requiredS449);
  }
  _M0L4dataS2446 = _M0L4selfS450->$0;
  _M0L3lenS2447 = _M0L4selfS450->$1;
  moonbit_incref_cycle_free(_M0L4dataS2446);
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2448 = _M0MPC16string10StringView4data(_M0L3strS448);
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2449 = _M0MPC16string10StringView13start__offset(_M0L3strS448);
  #line 170 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS2446, _M0L3lenS2447, _M0L6_2atmpS2448, _M0L6_2atmpS2449, _M0L8str__lenS447);
  moonbit_decref_cycle_free(_M0L4dataS2446);
  moonbit_decref_cycle_free(_M0L6_2atmpS2448);
  _M0L3lenS2451 = _M0L4selfS450->$1;
  _M0L6_2atmpS2450 = _M0L3lenS2451 + _M0L8str__lenS447;
  _M0L4selfS450->$1 = _M0L6_2atmpS2450;
  return 0;
}

moonbit_string_t _M0MPC16string6String17unsafe__substring(
  moonbit_string_t _M0L3strS444,
  int32_t _M0L5startS442,
  int32_t _M0L3endS443
) {
  int32_t _if__result_5983;
  int32_t _M0L3lenS445;
  int32_t _M0L6_2atmpS2442;
  moonbit_bytes_t _M0L5bytesS446;
  moonbit_bytes_t _M0L6_2atmpS2441;
  moonbit_string_t _result_5984;
  #line 91 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  if (_M0L5startS442 == 0) {
    int32_t _M0L6_2atmpS2440 = Moonbit_array_length(_M0L3strS444);
    _if__result_5983 = _M0L3endS443 == _M0L6_2atmpS2440;
  } else {
    _if__result_5983 = 0;
  }
  if (_if__result_5983) {
    moonbit_incref_cycle_free(_M0L3strS444);
    return _M0L3strS444;
  }
  _M0L3lenS445 = _M0L3endS443 - _M0L5startS442;
  _M0L6_2atmpS2442 = _M0L3lenS445 * 2;
  _M0L5bytesS446 = (moonbit_bytes_t)moonbit_make_bytes(_M0L6_2atmpS2442, 0);
  #line 102 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _M0MPC15array10FixedArray18blit__from__string(_M0L5bytesS446, 0, _M0L3strS444, _M0L5startS442, _M0L3lenS445);
  _M0L6_2atmpS2441 = _M0L5bytesS446;
  #line 103 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\string.mbt"
  _result_5984
  = _M0MPC15bytes5Bytes29to__unchecked__string_2einner(_M0L6_2atmpS2441, 0, 4294967296ll);
  moonbit_decref_cycle_free(_M0L6_2atmpS2441);
  return _result_5984;
}

moonbit_string_t _M0MPC15bytes5Bytes29to__unchecked__string_2einner(
  moonbit_bytes_t _M0L4selfS437,
  int32_t _M0L6offsetS441,
  int64_t _M0L6lengthS439
) {
  int32_t _M0L3lenS436;
  int32_t _M0L6lengthS438;
  int32_t _if__result_5985;
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
      int32_t _M0L6_2atmpS2439 = _M0L6offsetS441 + _M0L6lengthS438;
      _if__result_5985 = _M0L6_2atmpS2439 <= _M0L3lenS436;
    } else {
      _if__result_5985 = 0;
    }
  } else {
    _if__result_5985 = 0;
  }
  if (_if__result_5985) {
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
  int32_t _M0L6_2atmpS2438;
  int32_t _M0L6_2atmpS2437;
  int32_t _M0L2e1S422;
  int32_t _M0L6_2atmpS2436;
  int32_t _M0L2e2S425;
  int32_t _M0L4len1S427;
  int32_t _M0L4len2S429;
  #line 125 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
  _M0L6_2atmpS2438 = _M0L6lengthS424 * 2;
  _M0L6_2atmpS2437 = _M0L13bytes__offsetS423 + _M0L6_2atmpS2438;
  _M0L2e1S422 = _M0L6_2atmpS2437 - 1;
  _M0L6_2atmpS2436 = _M0L11str__offsetS426 + _M0L6lengthS424;
  _M0L2e2S425 = _M0L6_2atmpS2436 - 1;
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
        int32_t _M0L6_2atmpS2433 = _M0L3strS430[_M0L1iS432];
        int32_t _M0L6_2atmpS2432 = (int32_t)_M0L6_2atmpS2433;
        uint32_t _M0L1cS434 = *(uint32_t*)&_M0L6_2atmpS2432;
        uint32_t _M0L6_2atmpS2428 = _M0L1cS434 & 255u;
        int32_t _M0L6_2atmpS2427;
        int32_t _M0L6_2atmpS2429;
        uint32_t _M0L6_2atmpS2431;
        int32_t _M0L6_2atmpS2430;
        int32_t _M0L6_2atmpS2434;
        int32_t _M0L6_2atmpS2435;
        #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS2427 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS2428);
        if (
          _M0L1jS433 < 0 || _M0L1jS433 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 142 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L1jS433] = _M0L6_2atmpS2427;
        _M0L6_2atmpS2429 = _M0L1jS433 + 1;
        _M0L6_2atmpS2431 = _M0L1cS434 >> 8;
        #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
        _M0L6_2atmpS2430 = _M0MPC14uint4UInt8to__byte(_M0L6_2atmpS2431);
        if (
          _M0L6_2atmpS2429 < 0
          || _M0L6_2atmpS2429 >= Moonbit_array_length(_M0L4selfS428)
        ) {
          #line 143 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\bytes.mbt"
          moonbit_panic();
        }
        _M0L4selfS428[_M0L6_2atmpS2429] = _M0L6_2atmpS2430;
        _M0L6_2atmpS2434 = _M0L1iS432 + 1;
        _M0L6_2atmpS2435 = _M0L1jS433 + 2;
        _M0L1iS432 = _M0L6_2atmpS2434;
        _M0L1jS433 = _M0L6_2atmpS2435;
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
  int32_t _M0L6_2atmpS2426;
  #line 2601 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2426 = *(int32_t*)&_M0L4selfS421;
  return _M0L6_2atmpS2426 & 0xff;
}

moonbit_string_t _M0MPC16uint646UInt6418to__string_2einner(
  uint64_t _M0L4selfS413,
  int32_t _M0L5radixS412
) {
  uint16_t* _M0L6bufferS414;
  #line 607 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  if (_M0L5radixS412 < 2 || _M0L5radixS412 > 36) {
    #line 611 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_19.data);
  }
  if (_M0L4selfS413 == 0ull) {
    return (moonbit_string_t)moonbit_string_literal_12.data;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_19.data);
  }
  if (_M0L4selfS396 == 0ll) {
    return (moonbit_string_t)moonbit_string_literal_12.data;
  }
  _M0L12is__negativeS397 = _M0L4selfS396 < 0ll;
  if (_M0L12is__negativeS397) {
    int64_t _M0L6_2atmpS2425 = -_M0L4selfS396;
    _M0L3numS398 = *(uint64_t*)&_M0L6_2atmpS2425;
  } else {
    _M0L3numS398 = *(uint64_t*)&_M0L4selfS396;
  }
  switch (_M0L5radixS395) {
    case 10: {
      int32_t _M0L10digit__lenS400;
      int32_t _M0L6_2atmpS2422;
      int32_t _M0L10total__lenS401;
      uint16_t* _M0L6bufferS402;
      int32_t _M0L12digit__startS403;
      #line 573 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS400 = _M0FPB12dec__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS2422 = 1;
      } else {
        _M0L6_2atmpS2422 = 0;
      }
      _M0L10total__lenS401 = _M0L10digit__lenS400 + _M0L6_2atmpS2422;
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
      int32_t _M0L6_2atmpS2423;
      int32_t _M0L10total__lenS405;
      uint16_t* _M0L6bufferS406;
      int32_t _M0L12digit__startS407;
      #line 581 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS404 = _M0FPB12hex__count64(_M0L3numS398);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS2423 = 1;
      } else {
        _M0L6_2atmpS2423 = 0;
      }
      _M0L10total__lenS405 = _M0L10digit__lenS404 + _M0L6_2atmpS2423;
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
      int32_t _M0L6_2atmpS2424;
      int32_t _M0L10total__lenS409;
      uint16_t* _M0L6bufferS410;
      int32_t _M0L12digit__startS411;
      #line 589 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS408
      = _M0FPB14radix__count64(_M0L3numS398, _M0L5radixS395);
      if (_M0L12is__negativeS397) {
        _M0L6_2atmpS2424 = 1;
      } else {
        _M0L6_2atmpS2424 = 0;
      }
      _M0L10total__lenS409 = _M0L10digit__lenS408 + _M0L6_2atmpS2424;
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
  int32_t _M0L6_2atmpS2421;
  uint64_t _M0L3numS371;
  int32_t _M0L6offsetS372;
  #line 493 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2421 = _M0L10total__lenS394 - _M0L12digit__startS382;
  _M0L3numS371 = _M0L3numS393;
  _M0L6offsetS372 = _M0L6_2atmpS2421;
  while (1) {
    if (_M0L3numS371 >= 10000ull) {
      uint64_t _M0L1tS373 = _M0L3numS371 / 10000ull;
      uint64_t _M0L6_2atmpS2398 = _M0L3numS371 % 10000ull;
      int32_t _M0L1rS374 = (int32_t)_M0L6_2atmpS2398;
      int32_t _M0L2d1S375 = _M0L1rS374 / 100;
      int32_t _M0L2d2S376 = _M0L1rS374 % 100;
      int32_t _M0L6_2atmpS2397 = _M0L2d1S375 / 10;
      int32_t _M0L6_2atmpS2396 = 48 + _M0L6_2atmpS2397;
      int32_t _M0L6d1__hiS377 = (uint16_t)_M0L6_2atmpS2396;
      int32_t _M0L6_2atmpS2395 = _M0L2d1S375 % 10;
      int32_t _M0L6_2atmpS2394 = 48 + _M0L6_2atmpS2395;
      int32_t _M0L6d1__loS378 = (uint16_t)_M0L6_2atmpS2394;
      int32_t _M0L6_2atmpS2393 = _M0L2d2S376 / 10;
      int32_t _M0L6_2atmpS2392 = 48 + _M0L6_2atmpS2393;
      int32_t _M0L6d2__hiS379 = (uint16_t)_M0L6_2atmpS2392;
      int32_t _M0L6_2atmpS2391 = _M0L2d2S376 % 10;
      int32_t _M0L6_2atmpS2390 = 48 + _M0L6_2atmpS2391;
      int32_t _M0L6d2__loS380 = (uint16_t)_M0L6_2atmpS2390;
      int32_t _M0L6_2atmpS2382 = _M0L12digit__startS382 + _M0L6offsetS372;
      int32_t _M0L6_2atmpS2381 = _M0L6_2atmpS2382 - 4;
      int32_t _M0L6_2atmpS2384;
      int32_t _M0L6_2atmpS2383;
      int32_t _M0L6_2atmpS2386;
      int32_t _M0L6_2atmpS2385;
      int32_t _M0L6_2atmpS2388;
      int32_t _M0L6_2atmpS2387;
      int32_t _M0L6_2atmpS2389;
      _M0L6bufferS381[_M0L6_2atmpS2381] = _M0L6d1__hiS377;
      _M0L6_2atmpS2384 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS2383 = _M0L6_2atmpS2384 - 3;
      _M0L6bufferS381[_M0L6_2atmpS2383] = _M0L6d1__loS378;
      _M0L6_2atmpS2386 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS2385 = _M0L6_2atmpS2386 - 2;
      _M0L6bufferS381[_M0L6_2atmpS2385] = _M0L6d2__hiS379;
      _M0L6_2atmpS2388 = _M0L12digit__startS382 + _M0L6offsetS372;
      _M0L6_2atmpS2387 = _M0L6_2atmpS2388 - 1;
      _M0L6bufferS381[_M0L6_2atmpS2387] = _M0L6d2__loS380;
      _M0L6_2atmpS2389 = _M0L6offsetS372 - 4;
      _M0L3numS371 = _M0L1tS373;
      _M0L6offsetS372 = _M0L6_2atmpS2389;
      continue;
    } else {
      int32_t _M0L6_2atmpS2420 = (int32_t)_M0L3numS371;
      int32_t _M0L9remainingS384 = _M0L6_2atmpS2420;
      int32_t _M0L6offsetS385 = _M0L6offsetS372;
      while (1) {
        if (_M0L9remainingS384 >= 100) {
          int32_t _M0L1tS386 = _M0L9remainingS384 / 100;
          int32_t _M0L1dS387 = _M0L9remainingS384 % 100;
          int32_t _M0L6_2atmpS2407 = _M0L1dS387 / 10;
          int32_t _M0L6_2atmpS2406 = 48 + _M0L6_2atmpS2407;
          int32_t _M0L5d__hiS388 = (uint16_t)_M0L6_2atmpS2406;
          int32_t _M0L6_2atmpS2405 = _M0L1dS387 % 10;
          int32_t _M0L6_2atmpS2404 = 48 + _M0L6_2atmpS2405;
          int32_t _M0L5d__loS389 = (uint16_t)_M0L6_2atmpS2404;
          int32_t _M0L6_2atmpS2400 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS2399 = _M0L6_2atmpS2400 - 2;
          int32_t _M0L6_2atmpS2402;
          int32_t _M0L6_2atmpS2401;
          int32_t _M0L6_2atmpS2403;
          _M0L6bufferS381[_M0L6_2atmpS2399] = _M0L5d__hiS388;
          _M0L6_2atmpS2402 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS2401 = _M0L6_2atmpS2402 - 1;
          _M0L6bufferS381[_M0L6_2atmpS2401] = _M0L5d__loS389;
          _M0L6_2atmpS2403 = _M0L6offsetS385 - 2;
          _M0L9remainingS384 = _M0L1tS386;
          _M0L6offsetS385 = _M0L6_2atmpS2403;
          continue;
        } else if (_M0L9remainingS384 >= 10) {
          int32_t _M0L6_2atmpS2415 = _M0L9remainingS384 / 10;
          int32_t _M0L6_2atmpS2414 = 48 + _M0L6_2atmpS2415;
          int32_t _M0L5d__hiS391 = (uint16_t)_M0L6_2atmpS2414;
          int32_t _M0L6_2atmpS2413 = _M0L9remainingS384 % 10;
          int32_t _M0L6_2atmpS2412 = 48 + _M0L6_2atmpS2413;
          int32_t _M0L5d__loS392 = (uint16_t)_M0L6_2atmpS2412;
          int32_t _M0L6_2atmpS2409 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS2408 = _M0L6_2atmpS2409 - 2;
          int32_t _M0L6_2atmpS2411;
          int32_t _M0L6_2atmpS2410;
          _M0L6bufferS381[_M0L6_2atmpS2408] = _M0L5d__hiS391;
          _M0L6_2atmpS2411 = _M0L12digit__startS382 + _M0L6offsetS385;
          _M0L6_2atmpS2410 = _M0L6_2atmpS2411 - 1;
          _M0L6bufferS381[_M0L6_2atmpS2410] = _M0L5d__loS392;
        } else {
          int32_t _M0L6_2atmpS2419 = _M0L12digit__startS382 + _M0L6offsetS385;
          int32_t _M0L6_2atmpS2416 = _M0L6_2atmpS2419 - 1;
          int32_t _M0L6_2atmpS2418 = 48 + _M0L9remainingS384;
          int32_t _M0L6_2atmpS2417 = (uint16_t)_M0L6_2atmpS2418;
          _M0L6bufferS381[_M0L6_2atmpS2416] = _M0L6_2atmpS2417;
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
  int32_t _M0L6_2atmpS2366;
  int32_t _M0L6_2atmpS2365;
  #line 462 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  #line 470 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS354 = _M0MPC13int3Int10to__uint64(_M0L5radixS355);
  _M0L6_2atmpS2366 = _M0L5radixS355 - 1;
  _M0L6_2atmpS2365 = _M0L5radixS355 & _M0L6_2atmpS2366;
  if (_M0L6_2atmpS2365 == 0) {
    int32_t _M0L5shiftS356;
    uint64_t _M0L4maskS357;
    int32_t _M0L6_2atmpS2373;
    int32_t _M0L6offsetS358;
    uint64_t _M0L1nS359;
    #line 473 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS356 = moonbit_ctz32(_M0L5radixS355);
    _M0L4maskS357 = _M0L4baseS354 - 1ull;
    _M0L6_2atmpS2373 = _M0L10total__lenS364 - _M0L12digit__startS362;
    _M0L6offsetS358 = _M0L6_2atmpS2373;
    _M0L1nS359 = _M0L3numS365;
    while (1) {
      if (_M0L1nS359 > 0ull) {
        uint64_t _M0L6_2atmpS2372 = _M0L1nS359 & _M0L4maskS357;
        int32_t _M0L5digitS360 = (int32_t)_M0L6_2atmpS2372;
        int32_t _M0L6_2atmpS2369 = _M0L12digit__startS362 + _M0L6offsetS358;
        int32_t _M0L6_2atmpS2367 = _M0L6_2atmpS2369 - 1;
        int32_t _M0L6_2atmpS2368 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS360];
        int32_t _M0L6_2atmpS2370;
        uint64_t _M0L6_2atmpS2371;
        _M0L6bufferS361[_M0L6_2atmpS2367] = _M0L6_2atmpS2368;
        _M0L6_2atmpS2370 = _M0L6offsetS358 - 1;
        _M0L6_2atmpS2371 = _M0L1nS359 >> (_M0L5shiftS356 & 63);
        _M0L6offsetS358 = _M0L6_2atmpS2370;
        _M0L1nS359 = _M0L6_2atmpS2371;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2380 = _M0L10total__lenS364 - _M0L12digit__startS362;
    int32_t _M0L6offsetS366 = _M0L6_2atmpS2380;
    uint64_t _M0L1nS367 = _M0L3numS365;
    while (1) {
      if (_M0L1nS367 > 0ull) {
        uint64_t _M0L1qS368 = _M0L1nS367 / _M0L4baseS354;
        uint64_t _M0L6_2atmpS2379 = _M0L1qS368 * _M0L4baseS354;
        uint64_t _M0L6_2atmpS2378 = _M0L1nS367 - _M0L6_2atmpS2379;
        int32_t _M0L5digitS369 = (int32_t)_M0L6_2atmpS2378;
        int32_t _M0L6_2atmpS2376 = _M0L12digit__startS362 + _M0L6offsetS366;
        int32_t _M0L6_2atmpS2374 = _M0L6_2atmpS2376 - 1;
        int32_t _M0L6_2atmpS2375 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS369];
        int32_t _M0L6_2atmpS2377;
        _M0L6bufferS361[_M0L6_2atmpS2374] = _M0L6_2atmpS2375;
        _M0L6_2atmpS2377 = _M0L6offsetS366 - 1;
        _M0L6offsetS366 = _M0L6_2atmpS2377;
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
  int32_t _M0L6_2atmpS2364;
  int32_t _M0L6offsetS343;
  uint64_t _M0L1nS344;
  #line 434 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2364 = _M0L10total__lenS352 - _M0L12digit__startS349;
  _M0L6offsetS343 = _M0L6_2atmpS2364;
  _M0L1nS344 = _M0L3numS353;
  while (1) {
    if (_M0L6offsetS343 >= 2) {
      uint64_t _M0L6_2atmpS2361 = _M0L1nS344 & 255ull;
      int32_t _M0L9byte__valS345 = (int32_t)_M0L6_2atmpS2361;
      int32_t _M0L2hiS346 = _M0L9byte__valS345 / 16;
      int32_t _M0L2loS347 = _M0L9byte__valS345 % 16;
      int32_t _M0L6_2atmpS2355 = _M0L12digit__startS349 + _M0L6offsetS343;
      int32_t _M0L6_2atmpS2353 = _M0L6_2atmpS2355 - 2;
      int32_t _M0L6_2atmpS2354 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L2hiS346];
      int32_t _M0L6_2atmpS2358;
      int32_t _M0L6_2atmpS2356;
      int32_t _M0L6_2atmpS2357;
      int32_t _M0L6_2atmpS2359;
      uint64_t _M0L6_2atmpS2360;
      _M0L6bufferS348[_M0L6_2atmpS2353] = _M0L6_2atmpS2354;
      _M0L6_2atmpS2358 = _M0L12digit__startS349 + _M0L6offsetS343;
      _M0L6_2atmpS2356 = _M0L6_2atmpS2358 - 1;
      _M0L6_2atmpS2357
      = ((moonbit_string_t)moonbit_string_literal_20.data)[
        _M0L2loS347
      ];
      _M0L6bufferS348[_M0L6_2atmpS2356] = _M0L6_2atmpS2357;
      _M0L6_2atmpS2359 = _M0L6offsetS343 - 2;
      _M0L6_2atmpS2360 = _M0L1nS344 >> 8;
      _M0L6offsetS343 = _M0L6_2atmpS2359;
      _M0L1nS344 = _M0L6_2atmpS2360;
      continue;
    } else if (_M0L6offsetS343 == 1) {
      uint64_t _M0L6_2atmpS2363 = _M0L1nS344 & 15ull;
      int32_t _M0L6nibbleS351 = (int32_t)_M0L6_2atmpS2363;
      int32_t _M0L6_2atmpS2362 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L6nibbleS351];
      _M0L6bufferS348[_M0L12digit__startS349] = _M0L6_2atmpS2362;
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
      uint64_t _M0L6_2atmpS2351 = _M0L3numS340 / _M0L4baseS338;
      int32_t _M0L6_2atmpS2352 = _M0L5countS341 + 1;
      _M0L3numS340 = _M0L6_2atmpS2351;
      _M0L5countS341 = _M0L6_2atmpS2352;
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
    int32_t _M0L6_2atmpS2350;
    int32_t _M0L6_2atmpS2349;
    #line 412 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS336 = moonbit_clz64(_M0L5valueS335);
    _M0L6_2atmpS2350 = 63 - _M0L14leading__zerosS336;
    _M0L6_2atmpS2349 = _M0L6_2atmpS2350 / 4;
    return _M0L6_2atmpS2349 + 1;
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_19.data);
  }
  if (_M0L4selfS318 == 0) {
    return (moonbit_string_t)moonbit_string_literal_12.data;
  }
  _M0L12is__negativeS319 = _M0L4selfS318 < 0;
  if (_M0L12is__negativeS319) {
    int32_t _M0L6_2atmpS2348 = -_M0L4selfS318;
    _M0L3numS320 = *(uint32_t*)&_M0L6_2atmpS2348;
  } else {
    _M0L3numS320 = *(uint32_t*)&_M0L4selfS318;
  }
  switch (_M0L5radixS317) {
    case 10: {
      int32_t _M0L10digit__lenS322;
      int32_t _M0L6_2atmpS2345;
      int32_t _M0L10total__lenS323;
      uint16_t* _M0L6bufferS324;
      int32_t _M0L12digit__startS325;
      #line 235 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS322 = _M0FPB12dec__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS2345 = 1;
      } else {
        _M0L6_2atmpS2345 = 0;
      }
      _M0L10total__lenS323 = _M0L10digit__lenS322 + _M0L6_2atmpS2345;
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
      int32_t _M0L6_2atmpS2346;
      int32_t _M0L10total__lenS327;
      uint16_t* _M0L6bufferS328;
      int32_t _M0L12digit__startS329;
      #line 243 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS326 = _M0FPB12hex__count32(_M0L3numS320);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS2346 = 1;
      } else {
        _M0L6_2atmpS2346 = 0;
      }
      _M0L10total__lenS327 = _M0L10digit__lenS326 + _M0L6_2atmpS2346;
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
      int32_t _M0L6_2atmpS2347;
      int32_t _M0L10total__lenS331;
      uint16_t* _M0L6bufferS332;
      int32_t _M0L12digit__startS333;
      #line 251 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
      _M0L10digit__lenS330
      = _M0FPB14radix__count32(_M0L3numS320, _M0L5radixS317);
      if (_M0L12is__negativeS319) {
        _M0L6_2atmpS2347 = 1;
      } else {
        _M0L6_2atmpS2347 = 0;
      }
      _M0L10total__lenS331 = _M0L10digit__lenS330 + _M0L6_2atmpS2347;
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
      uint32_t _M0L6_2atmpS2343 = _M0L3numS314 / _M0L4baseS312;
      int32_t _M0L6_2atmpS2344 = _M0L5countS315 + 1;
      _M0L3numS314 = _M0L6_2atmpS2343;
      _M0L5countS315 = _M0L6_2atmpS2344;
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
    int32_t _M0L6_2atmpS2342;
    int32_t _M0L6_2atmpS2341;
    #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L14leading__zerosS310 = moonbit_clz32(_M0L5valueS309);
    _M0L6_2atmpS2342 = 31 - _M0L14leading__zerosS310;
    _M0L6_2atmpS2341 = _M0L6_2atmpS2342 / 4;
    return _M0L6_2atmpS2341 + 1;
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
  int32_t _M0L6_2atmpS2340;
  uint32_t _M0L3numS284;
  int32_t _M0L6offsetS285;
  #line 88 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2340 = _M0L10total__lenS307 - _M0L12digit__startS295;
  _M0L3numS284 = _M0L3numS306;
  _M0L6offsetS285 = _M0L6_2atmpS2340;
  while (1) {
    if (_M0L3numS284 >= 10000u) {
      uint32_t _M0L1tS286 = _M0L3numS284 / 10000u;
      uint32_t _M0L6_2atmpS2317 = _M0L3numS284 % 10000u;
      int32_t _M0L1rS287 = *(int32_t*)&_M0L6_2atmpS2317;
      int32_t _M0L2d1S288 = _M0L1rS287 / 100;
      int32_t _M0L2d2S289 = _M0L1rS287 % 100;
      int32_t _M0L6_2atmpS2316 = _M0L2d1S288 / 10;
      int32_t _M0L6_2atmpS2315 = 48 + _M0L6_2atmpS2316;
      int32_t _M0L6d1__hiS290 = (uint16_t)_M0L6_2atmpS2315;
      int32_t _M0L6_2atmpS2314 = _M0L2d1S288 % 10;
      int32_t _M0L6_2atmpS2313 = 48 + _M0L6_2atmpS2314;
      int32_t _M0L6d1__loS291 = (uint16_t)_M0L6_2atmpS2313;
      int32_t _M0L6_2atmpS2312 = _M0L2d2S289 / 10;
      int32_t _M0L6_2atmpS2311 = 48 + _M0L6_2atmpS2312;
      int32_t _M0L6d2__hiS292 = (uint16_t)_M0L6_2atmpS2311;
      int32_t _M0L6_2atmpS2310 = _M0L2d2S289 % 10;
      int32_t _M0L6_2atmpS2309 = 48 + _M0L6_2atmpS2310;
      int32_t _M0L6d2__loS293 = (uint16_t)_M0L6_2atmpS2309;
      int32_t _M0L6_2atmpS2301 = _M0L12digit__startS295 + _M0L6offsetS285;
      int32_t _M0L6_2atmpS2300 = _M0L6_2atmpS2301 - 4;
      int32_t _M0L6_2atmpS2303;
      int32_t _M0L6_2atmpS2302;
      int32_t _M0L6_2atmpS2305;
      int32_t _M0L6_2atmpS2304;
      int32_t _M0L6_2atmpS2307;
      int32_t _M0L6_2atmpS2306;
      int32_t _M0L6_2atmpS2308;
      _M0L6bufferS294[_M0L6_2atmpS2300] = _M0L6d1__hiS290;
      _M0L6_2atmpS2303 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS2302 = _M0L6_2atmpS2303 - 3;
      _M0L6bufferS294[_M0L6_2atmpS2302] = _M0L6d1__loS291;
      _M0L6_2atmpS2305 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS2304 = _M0L6_2atmpS2305 - 2;
      _M0L6bufferS294[_M0L6_2atmpS2304] = _M0L6d2__hiS292;
      _M0L6_2atmpS2307 = _M0L12digit__startS295 + _M0L6offsetS285;
      _M0L6_2atmpS2306 = _M0L6_2atmpS2307 - 1;
      _M0L6bufferS294[_M0L6_2atmpS2306] = _M0L6d2__loS293;
      _M0L6_2atmpS2308 = _M0L6offsetS285 - 4;
      _M0L3numS284 = _M0L1tS286;
      _M0L6offsetS285 = _M0L6_2atmpS2308;
      continue;
    } else {
      int32_t _M0L6_2atmpS2339 = *(int32_t*)&_M0L3numS284;
      int32_t _M0L9remainingS297 = _M0L6_2atmpS2339;
      int32_t _M0L6offsetS298 = _M0L6offsetS285;
      while (1) {
        if (_M0L9remainingS297 >= 100) {
          int32_t _M0L1tS299 = _M0L9remainingS297 / 100;
          int32_t _M0L1dS300 = _M0L9remainingS297 % 100;
          int32_t _M0L6_2atmpS2326 = _M0L1dS300 / 10;
          int32_t _M0L6_2atmpS2325 = 48 + _M0L6_2atmpS2326;
          int32_t _M0L5d__hiS301 = (uint16_t)_M0L6_2atmpS2325;
          int32_t _M0L6_2atmpS2324 = _M0L1dS300 % 10;
          int32_t _M0L6_2atmpS2323 = 48 + _M0L6_2atmpS2324;
          int32_t _M0L5d__loS302 = (uint16_t)_M0L6_2atmpS2323;
          int32_t _M0L6_2atmpS2319 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS2318 = _M0L6_2atmpS2319 - 2;
          int32_t _M0L6_2atmpS2321;
          int32_t _M0L6_2atmpS2320;
          int32_t _M0L6_2atmpS2322;
          _M0L6bufferS294[_M0L6_2atmpS2318] = _M0L5d__hiS301;
          _M0L6_2atmpS2321 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS2320 = _M0L6_2atmpS2321 - 1;
          _M0L6bufferS294[_M0L6_2atmpS2320] = _M0L5d__loS302;
          _M0L6_2atmpS2322 = _M0L6offsetS298 - 2;
          _M0L9remainingS297 = _M0L1tS299;
          _M0L6offsetS298 = _M0L6_2atmpS2322;
          continue;
        } else if (_M0L9remainingS297 >= 10) {
          int32_t _M0L6_2atmpS2334 = _M0L9remainingS297 / 10;
          int32_t _M0L6_2atmpS2333 = 48 + _M0L6_2atmpS2334;
          int32_t _M0L5d__hiS304 = (uint16_t)_M0L6_2atmpS2333;
          int32_t _M0L6_2atmpS2332 = _M0L9remainingS297 % 10;
          int32_t _M0L6_2atmpS2331 = 48 + _M0L6_2atmpS2332;
          int32_t _M0L5d__loS305 = (uint16_t)_M0L6_2atmpS2331;
          int32_t _M0L6_2atmpS2328 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS2327 = _M0L6_2atmpS2328 - 2;
          int32_t _M0L6_2atmpS2330;
          int32_t _M0L6_2atmpS2329;
          _M0L6bufferS294[_M0L6_2atmpS2327] = _M0L5d__hiS304;
          _M0L6_2atmpS2330 = _M0L12digit__startS295 + _M0L6offsetS298;
          _M0L6_2atmpS2329 = _M0L6_2atmpS2330 - 1;
          _M0L6bufferS294[_M0L6_2atmpS2329] = _M0L5d__loS305;
        } else {
          int32_t _M0L6_2atmpS2338 = _M0L12digit__startS295 + _M0L6offsetS298;
          int32_t _M0L6_2atmpS2335 = _M0L6_2atmpS2338 - 1;
          int32_t _M0L6_2atmpS2337 = 48 + _M0L9remainingS297;
          int32_t _M0L6_2atmpS2336 = (uint16_t)_M0L6_2atmpS2337;
          _M0L6bufferS294[_M0L6_2atmpS2335] = _M0L6_2atmpS2336;
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
  int32_t _M0L6_2atmpS2285;
  int32_t _M0L6_2atmpS2284;
  #line 57 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L4baseS267 = *(uint32_t*)&_M0L5radixS268;
  _M0L6_2atmpS2285 = _M0L5radixS268 - 1;
  _M0L6_2atmpS2284 = _M0L5radixS268 & _M0L6_2atmpS2285;
  if (_M0L6_2atmpS2284 == 0) {
    int32_t _M0L5shiftS269;
    uint32_t _M0L4maskS270;
    int32_t _M0L6_2atmpS2292;
    int32_t _M0L6offsetS271;
    uint32_t _M0L1nS272;
    #line 68 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
    _M0L5shiftS269 = moonbit_ctz32(_M0L5radixS268);
    _M0L4maskS270 = _M0L4baseS267 - 1u;
    _M0L6_2atmpS2292 = _M0L10total__lenS277 - _M0L12digit__startS275;
    _M0L6offsetS271 = _M0L6_2atmpS2292;
    _M0L1nS272 = _M0L3numS278;
    while (1) {
      if (_M0L1nS272 > 0u) {
        uint32_t _M0L6_2atmpS2291 = _M0L1nS272 & _M0L4maskS270;
        int32_t _M0L5digitS273 = *(int32_t*)&_M0L6_2atmpS2291;
        int32_t _M0L6_2atmpS2288 = _M0L12digit__startS275 + _M0L6offsetS271;
        int32_t _M0L6_2atmpS2286 = _M0L6_2atmpS2288 - 1;
        int32_t _M0L6_2atmpS2287 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS273];
        int32_t _M0L6_2atmpS2289;
        uint32_t _M0L6_2atmpS2290;
        _M0L6bufferS274[_M0L6_2atmpS2286] = _M0L6_2atmpS2287;
        _M0L6_2atmpS2289 = _M0L6offsetS271 - 1;
        _M0L6_2atmpS2290 = _M0L1nS272 >> (_M0L5shiftS269 & 31);
        _M0L6offsetS271 = _M0L6_2atmpS2289;
        _M0L1nS272 = _M0L6_2atmpS2290;
        continue;
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2299 = _M0L10total__lenS277 - _M0L12digit__startS275;
    int32_t _M0L6offsetS279 = _M0L6_2atmpS2299;
    uint32_t _M0L1nS280 = _M0L3numS278;
    while (1) {
      if (_M0L1nS280 > 0u) {
        uint32_t _M0L1qS281 = _M0L1nS280 / _M0L4baseS267;
        uint32_t _M0L6_2atmpS2298 = _M0L1qS281 * _M0L4baseS267;
        uint32_t _M0L6_2atmpS2297 = _M0L1nS280 - _M0L6_2atmpS2298;
        int32_t _M0L5digitS282 = *(int32_t*)&_M0L6_2atmpS2297;
        int32_t _M0L6_2atmpS2295 = _M0L12digit__startS275 + _M0L6offsetS279;
        int32_t _M0L6_2atmpS2293 = _M0L6_2atmpS2295 - 1;
        int32_t _M0L6_2atmpS2294 =
          ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L5digitS282];
        int32_t _M0L6_2atmpS2296;
        _M0L6bufferS274[_M0L6_2atmpS2293] = _M0L6_2atmpS2294;
        _M0L6_2atmpS2296 = _M0L6offsetS279 - 1;
        _M0L6offsetS279 = _M0L6_2atmpS2296;
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
  int32_t _M0L6_2atmpS2283;
  int32_t _M0L6offsetS256;
  uint32_t _M0L1nS257;
  #line 29 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\to_string.mbt"
  _M0L6_2atmpS2283 = _M0L10total__lenS265 - _M0L12digit__startS262;
  _M0L6offsetS256 = _M0L6_2atmpS2283;
  _M0L1nS257 = _M0L3numS266;
  while (1) {
    if (_M0L6offsetS256 >= 2) {
      uint32_t _M0L6_2atmpS2280 = _M0L1nS257 & 255u;
      int32_t _M0L9byte__valS258 = *(int32_t*)&_M0L6_2atmpS2280;
      int32_t _M0L2hiS259 = _M0L9byte__valS258 / 16;
      int32_t _M0L2loS260 = _M0L9byte__valS258 % 16;
      int32_t _M0L6_2atmpS2274 = _M0L12digit__startS262 + _M0L6offsetS256;
      int32_t _M0L6_2atmpS2272 = _M0L6_2atmpS2274 - 2;
      int32_t _M0L6_2atmpS2273 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L2hiS259];
      int32_t _M0L6_2atmpS2277;
      int32_t _M0L6_2atmpS2275;
      int32_t _M0L6_2atmpS2276;
      int32_t _M0L6_2atmpS2278;
      uint32_t _M0L6_2atmpS2279;
      _M0L6bufferS261[_M0L6_2atmpS2272] = _M0L6_2atmpS2273;
      _M0L6_2atmpS2277 = _M0L12digit__startS262 + _M0L6offsetS256;
      _M0L6_2atmpS2275 = _M0L6_2atmpS2277 - 1;
      _M0L6_2atmpS2276
      = ((moonbit_string_t)moonbit_string_literal_20.data)[
        _M0L2loS260
      ];
      _M0L6bufferS261[_M0L6_2atmpS2275] = _M0L6_2atmpS2276;
      _M0L6_2atmpS2278 = _M0L6offsetS256 - 2;
      _M0L6_2atmpS2279 = _M0L1nS257 >> 8;
      _M0L6offsetS256 = _M0L6_2atmpS2278;
      _M0L1nS257 = _M0L6_2atmpS2279;
      continue;
    } else if (_M0L6offsetS256 == 1) {
      uint32_t _M0L6_2atmpS2282 = _M0L1nS257 & 15u;
      int32_t _M0L6nibbleS264 = *(int32_t*)&_M0L6_2atmpS2282;
      int32_t _M0L6_2atmpS2281 =
        ((moonbit_string_t)moonbit_string_literal_20.data)[_M0L6nibbleS264];
      _M0L6bufferS261[_M0L12digit__startS262] = _M0L6_2atmpS2281;
    }
    break;
  }
  return 0;
}

moonbit_string_t _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(
  void* _M0L4selfS255
) {
  struct _M0TPB13StringBuilder* _M0L6loggerS254;
  struct _M0TPB6Logger _M0L6_2atmpS2271;
  moonbit_string_t _result_5999;
  #line 171 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS254 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  moonbit_incref_cycle_free(_M0L6loggerS254);
  _M0L6_2atmpS2271
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L6loggerS254
  };
  #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB7FailurePB4Show6output(_M0L4selfS255, _M0L6_2atmpS2271);
  if (_M0L6_2atmpS2271.$1) {
    moonbit_decref(_M0L6_2atmpS2271.$1);
  }
  #line 174 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _result_5999 = _M0MPB13StringBuilder10to__string(_M0L6loggerS254);
  moonbit_decref_cycle_free(_M0L6loggerS254);
  return _result_5999;
}

int32_t _M0IP016_24default__implPB4Show6outputGsE(
  moonbit_string_t _M0L4selfS249,
  struct _M0TPB6Logger _M0L6loggerS248
) {
  moonbit_string_t _M0L6_2atmpS2268;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2268 = _M0IPC16string6StringPB4Show10to__string(_M0L4selfS249);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS248.$0->$method_0(_M0L6loggerS248.$1, _M0L6_2atmpS2268);
  moonbit_decref_cycle_free(_M0L6_2atmpS2268);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGiE(
  int32_t _M0L4selfS251,
  struct _M0TPB6Logger _M0L6loggerS250
) {
  moonbit_string_t _M0L6_2atmpS2269;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2269 = _M0IPC13int3IntPB4Show10to__string(_M0L4selfS251);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS250.$0->$method_0(_M0L6loggerS250.$1, _M0L6_2atmpS2269);
  moonbit_decref_cycle_free(_M0L6_2atmpS2269);
  return 0;
}

int32_t _M0IP016_24default__implPB4Show6outputGmE(
  uint64_t _M0L4selfS253,
  struct _M0TPB6Logger _M0L6loggerS252
) {
  moonbit_string_t _M0L6_2atmpS2270;
  #line 165 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2270 = _M0IPC16uint646UInt64PB4Show10to__string(_M0L4selfS253);
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6loggerS252.$0->$method_0(_M0L6loggerS252.$1, _M0L6_2atmpS2270);
  moonbit_decref_cycle_free(_M0L6_2atmpS2270);
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
  moonbit_string_t _M0L8_2afieldS5556;
  #line 92 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L8_2afieldS5556 = _M0L4selfS246.$0;
  moonbit_incref_cycle_free(_M0L8_2afieldS5556);
  return _M0L8_2afieldS5556;
}

int32_t _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS242,
  moonbit_string_t _M0L5valueS243,
  int32_t _M0L5startS244,
  int32_t _M0L3lenS245
) {
  int32_t _M0L6_2atmpS2267;
  int64_t _M0L6_2atmpS2266;
  struct _M0TPC16string10StringView _M0L6_2atmpS2265;
  #line 128 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2267 = _M0L5startS244 + _M0L3lenS245;
  _M0L6_2atmpS2266 = (int64_t)_M0L6_2atmpS2267;
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L6_2atmpS2265
  = _M0MPC16string6String21clamped__view_2einner(_M0L5valueS243, _M0L5startS244, _M0L6_2atmpS2266);
  #line 129 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L4selfS242, _M0L6_2atmpS2265);
  moonbit_decref_cycle_free(_M0L6_2atmpS2265.$0);
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
  int32_t _M0L6_2atmpS2249;
  int32_t _if__result_6000;
  int32_t _M0L6_2atmpS2257;
  int32_t _if__result_6001;
  int32_t _M0L6_2atmpS2259;
  int32_t _M0L6_2atmpS2260;
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
  _M0L6_2atmpS2249 = _M0Lm2loS236;
  if (_M0L6_2atmpS2249 > 0) {
    int32_t _M0L6_2atmpS2248 = _M0Lm2loS236;
    if (_M0L6_2atmpS2248 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS2247 = _M0Lm2loS236;
      int32_t _M0L6_2atmpS2246 = _M0L4selfS235[_M0L6_2atmpS2247];
      #line 712 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2246)) {
        int32_t _M0L6_2atmpS2245 = _M0Lm2loS236;
        int32_t _M0L6_2atmpS2244 = _M0L6_2atmpS2245 - 1;
        int32_t _M0L6_2atmpS2243 = _M0L4selfS235[_M0L6_2atmpS2244];
        #line 713 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_6000
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2243);
      } else {
        _if__result_6000 = 0;
      }
    } else {
      _if__result_6000 = 0;
    }
  } else {
    _if__result_6000 = 0;
  }
  if (_if__result_6000) {
    int32_t _M0L6_2atmpS2250 = _M0Lm2loS236;
    _M0Lm2loS236 = _M0L6_2atmpS2250 + 1;
  }
  _M0L6_2atmpS2257 = _M0Lm2hiS238;
  if (_M0L6_2atmpS2257 > 0) {
    int32_t _M0L6_2atmpS2256 = _M0Lm2hiS238;
    if (_M0L6_2atmpS2256 < _M0L3lenS234) {
      int32_t _M0L6_2atmpS2255 = _M0Lm2hiS238;
      int32_t _M0L6_2atmpS2254 = _M0L4selfS235[_M0L6_2atmpS2255];
      #line 718 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2254)) {
        int32_t _M0L6_2atmpS2253 = _M0Lm2hiS238;
        int32_t _M0L6_2atmpS2252 = _M0L6_2atmpS2253 - 1;
        int32_t _M0L6_2atmpS2251 = _M0L4selfS235[_M0L6_2atmpS2252];
        #line 719 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_6001
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2251);
      } else {
        _if__result_6001 = 0;
      }
    } else {
      _if__result_6001 = 0;
    }
  } else {
    _if__result_6001 = 0;
  }
  if (_if__result_6001) {
    int32_t _M0L6_2atmpS2258 = _M0Lm2hiS238;
    _M0Lm2hiS238 = _M0L6_2atmpS2258 - 1;
  }
  _M0L6_2atmpS2259 = _M0Lm2loS236;
  _M0L6_2atmpS2260 = _M0Lm2hiS238;
  if (_M0L6_2atmpS2259 >= _M0L6_2atmpS2260) {
    int32_t _M0L6_2atmpS2261 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS2262 = _M0Lm2loS236;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS2261,
                                                 .$2 = _M0L6_2atmpS2262};
  } else {
    int32_t _M0L6_2atmpS2263 = _M0Lm2loS236;
    int32_t _M0L6_2atmpS2264 = _M0Lm2hiS238;
    moonbit_incref_cycle_free(_M0L4selfS235);
    return (struct _M0TPC16string10StringView){.$0 = _M0L4selfS235,
                                                 .$1 = _M0L6_2atmpS2263,
                                                 .$2 = _M0L6_2atmpS2264};
  }
}

int32_t _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS233,
  struct _M0TPB4Show _M0L4showS232
) {
  struct _M0TPB6Logger _M0L6_2atmpS2242;
  #line 122 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS233);
  _M0L6_2atmpS2242
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS233
  };
  #line 123 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS232.$0->$method_0(_M0L4showS232.$1, _M0L6_2atmpS2242);
  if (_M0L6_2atmpS2242.$1) {
    moonbit_decref(_M0L6_2atmpS2242.$1);
  }
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(
  struct _M0TPB13StringBuilder* _M0L4selfS231,
  struct _M0TPB4Show _M0L4showS230
) {
  struct _M0TPB6Logger _M0L6_2atmpS2241;
  #line 117 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  moonbit_incref_cycle_free(_M0L4selfS231);
  _M0L6_2atmpS2241
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS231
  };
  #line 118 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\traits.mbt"
  _M0L4showS230.$0->$method_0(_M0L4showS230.$1, _M0L6_2atmpS2241);
  if (_M0L6_2atmpS2241.$1) {
    moonbit_decref(_M0L6_2atmpS2241.$1);
  }
  return 0;
}

uint64_t _M0MPC13int3Int10to__uint64(int32_t _M0L4selfS229) {
  int64_t _M0L6_2atmpS2240;
  #line 989 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2240 = (int64_t)_M0L4selfS229;
  return *(uint64_t*)&_M0L6_2atmpS2240;
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
  int32_t _M0L6_2atmpS2239;
  struct _M0TPC16string10StringView _M0L6_2atmpS2237;
  struct _M0TPB6Logger _M0L6_2atmpS2238;
  moonbit_string_t _result_6002;
  #line 110 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 111 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L3bufS226 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  _M0L6_2atmpS2239 = Moonbit_array_length(_M0L4selfS227);
  moonbit_incref_cycle_free(_M0L4selfS227);
  _M0L6_2atmpS2237
  = (struct _M0TPC16string10StringView){
    .$0 = _M0L4selfS227, .$1 = 0, .$2 = _M0L6_2atmpS2239
  };
  moonbit_incref_cycle_free(_M0L3bufS226);
  _M0L6_2atmpS2238
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L3bufS226
  };
  #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0MPC16string10StringView18escape__to_2einner(_M0L6_2atmpS2237, _M0L6_2atmpS2238, _M0L5quoteS228);
  moonbit_decref_cycle_free(_M0L6_2atmpS2237.$0);
  if (_M0L6_2atmpS2238.$1) {
    moonbit_decref(_M0L6_2atmpS2238.$1);
  }
  #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_6002 = _M0MPB13StringBuilder10to__string(_M0L3bufS226);
  moonbit_decref_cycle_free(_M0L3bufS226);
  return _result_6002;
}

int32_t _M0MPC16string10StringView18escape__to_2einner(
  struct _M0TPC16string10StringView _M0L4selfS218,
  struct _M0TPB6Logger _M0L6loggerS216,
  int32_t _M0L5quoteS215
) {
  int32_t _M0L3endS2235;
  int32_t _M0L5startS2236;
  int32_t _M0L3lenS217;
  struct _M0TURPC16string10StringViewRPB6LoggerE* _M0L6_2aenvS219;
  int32_t _M0L1iS220;
  int32_t _M0L3segS221;
  #line 144 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L5quoteS215) {
    #line 150 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 34);
  }
  _M0L3endS2235 = _M0L4selfS218.$2;
  _M0L5startS2236 = _M0L4selfS218.$1;
  _M0L3lenS217 = _M0L3endS2235 - _M0L5startS2236;
  moonbit_incref_cycle_free(_M0L4selfS218.$0);
  if (_M0L6loggerS216.$1) {
    moonbit_incref(_M0L6loggerS216.$1);
  }
  _M0L6_2aenvS219
  = (struct _M0TURPC16string10StringViewRPB6LoggerE*)moonbit_malloc(sizeof(struct _M0TURPC16string10StringViewRPB6LoggerE));
  Moonbit_object_header(_M0L6_2aenvS219)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 94, 0);
  _M0L6_2aenvS219->$0 = _M0L4selfS218;
  _M0L6_2aenvS219->$1 = _M0L6loggerS216;
  _M0L1iS220 = 0;
  _M0L3segS221 = 0;
  _2afor_222:;
  while (1) {
    moonbit_string_t _M0L3strS2232;
    int32_t _M0L5startS2234;
    int32_t _M0L6_2atmpS2233;
    int32_t _M0L4codeS223;
    int32_t _M0L1cS225;
    int32_t _M0L6_2atmpS2216;
    int32_t _M0L6_2atmpS2217;
    int32_t _M0L6_2atmpS2218;
    if (_M0L1iS220 >= _M0L3lenS217) {
      #line 160 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
      _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
      moonbit_decref_cycle_free(_M0L6_2aenvS219);
      break;
    }
    _M0L3strS2232 = _M0L4selfS218.$0;
    _M0L5startS2234 = _M0L4selfS218.$1;
    _M0L6_2atmpS2233 = _M0L5startS2234 + _M0L1iS220;
    _M0L4codeS223 = _M0L3strS2232[_M0L6_2atmpS2233];
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
        int32_t _M0L6_2atmpS2219;
        int32_t _M0L6_2atmpS2220;
        #line 172 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 173 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_21.data);
        _M0L6_2atmpS2219 = _M0L1iS220 + 1;
        _M0L6_2atmpS2220 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS2219;
        _M0L3segS221 = _M0L6_2atmpS2220;
        goto _2afor_222;
        break;
      }
      
      case 13: {
        int32_t _M0L6_2atmpS2221;
        int32_t _M0L6_2atmpS2222;
        #line 177 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 178 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_22.data);
        _M0L6_2atmpS2221 = _M0L1iS220 + 1;
        _M0L6_2atmpS2222 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS2221;
        _M0L3segS221 = _M0L6_2atmpS2222;
        goto _2afor_222;
        break;
      }
      
      case 8: {
        int32_t _M0L6_2atmpS2223;
        int32_t _M0L6_2atmpS2224;
        #line 182 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 183 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_23.data);
        _M0L6_2atmpS2223 = _M0L1iS220 + 1;
        _M0L6_2atmpS2224 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS2223;
        _M0L3segS221 = _M0L6_2atmpS2224;
        goto _2afor_222;
        break;
      }
      
      case 9: {
        int32_t _M0L6_2atmpS2225;
        int32_t _M0L6_2atmpS2226;
        #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
        #line 188 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
        _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_24.data);
        _M0L6_2atmpS2225 = _M0L1iS220 + 1;
        _M0L6_2atmpS2226 = _M0L1iS220 + 1;
        _M0L1iS220 = _M0L6_2atmpS2225;
        _M0L3segS221 = _M0L6_2atmpS2226;
        goto _2afor_222;
        break;
      }
      default: {
        if (_M0L4codeS223 < 32) {
          int32_t _M0L6_2atmpS2228;
          moonbit_string_t _M0L6_2atmpS2227;
          int32_t _M0L6_2atmpS2229;
          int32_t _M0L6_2atmpS2230;
          #line 193 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_25.data);
          _M0L6_2atmpS2228 = _M0L4codeS223 & 0xff;
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6_2atmpS2227 = _M0MPC14byte4Byte7to__hex(_M0L6_2atmpS2228);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, _M0L6_2atmpS2227);
          moonbit_decref_cycle_free(_M0L6_2atmpS2227);
          #line 194 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
          _M0L6loggerS216.$0->$method_0(_M0L6loggerS216.$1, (moonbit_string_t)moonbit_string_literal_6.data);
          _M0L6_2atmpS2229 = _M0L1iS220 + 1;
          _M0L6_2atmpS2230 = _M0L1iS220 + 1;
          _M0L1iS220 = _M0L6_2atmpS2229;
          _M0L3segS221 = _M0L6_2atmpS2230;
          goto _2afor_222;
        } else {
          int32_t _M0L6_2atmpS2231 = _M0L1iS220 + 1;
          int32_t _tmp_6005 = _M0L3segS221;
          _M0L1iS220 = _M0L6_2atmpS2231;
          _M0L3segS221 = _tmp_6005;
          goto _2afor_222;
        }
        break;
      }
    }
    goto joinlet_6004;
    join_224:;
    #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0MPC16string10StringView18escape__to_2einnerN14flush__segmentS4374(_M0L6_2aenvS219, _M0L3segS221, _M0L1iS220);
    #line 167 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, 92);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2216 = _M0MPC16uint166UInt1616unsafe__to__char(_M0L1cS225);
    #line 168 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS216.$0->$method_3(_M0L6loggerS216.$1, _M0L6_2atmpS2216);
    _M0L6_2atmpS2217 = _M0L1iS220 + 1;
    _M0L6_2atmpS2218 = _M0L1iS220 + 1;
    _M0L1iS220 = _M0L6_2atmpS2217;
    _M0L3segS221 = _M0L6_2atmpS2218;
    continue;
    joinlet_6004:;
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
    int64_t _M0L6_2atmpS2215 = (int64_t)_M0L1iS213;
    struct _M0TPC16string10StringView _M0L6_2atmpS2214;
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2214
    = _M0MPC16string10StringView21clamped__view_2einner(_M0L4selfS212, _M0L3segS214, _M0L6_2atmpS2215);
    #line 155 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6loggerS210.$0->$method_2(_M0L6loggerS210.$1, _M0L6_2atmpS2214);
    moonbit_decref_cycle_free(_M0L6_2atmpS2214.$0);
  }
  return 0;
}

struct _M0TPC16string10StringView _M0MPC16string10StringView21clamped__view_2einner(
  struct _M0TPC16string10StringView _M0L4selfS201,
  int32_t _M0L5startS203,
  int64_t _M0L3endS205
) {
  int32_t _M0L3endS2212;
  int32_t _M0L5startS2213;
  int32_t _M0L3lenS200;
  int32_t _M0Lm2loS202;
  int32_t _M0Lm2hiS204;
  moonbit_string_t _M0L3strS208;
  int32_t _M0L4baseS209;
  int32_t _M0L6_2atmpS2190;
  int32_t _if__result_6006;
  int32_t _M0L6_2atmpS2200;
  int32_t _if__result_6007;
  int32_t _M0L6_2atmpS2202;
  int32_t _M0L6_2atmpS2203;
  #line 748 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
  _M0L3endS2212 = _M0L4selfS201.$2;
  _M0L5startS2213 = _M0L4selfS201.$1;
  _M0L3lenS200 = _M0L3endS2212 - _M0L5startS2213;
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
  _M0L6_2atmpS2190 = _M0Lm2loS202;
  if (_M0L6_2atmpS2190 > 0) {
    int32_t _M0L6_2atmpS2189 = _M0Lm2loS202;
    if (_M0L6_2atmpS2189 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS2188 = _M0Lm2loS202;
      int32_t _M0L6_2atmpS2187 = _M0L4baseS209 + _M0L6_2atmpS2188;
      int32_t _M0L6_2atmpS2186 = _M0L3strS208[_M0L6_2atmpS2187];
      #line 764 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2186)) {
        int32_t _M0L6_2atmpS2185 = _M0Lm2loS202;
        int32_t _M0L6_2atmpS2184 = _M0L4baseS209 + _M0L6_2atmpS2185;
        int32_t _M0L6_2atmpS2183 = _M0L6_2atmpS2184 - 1;
        int32_t _M0L6_2atmpS2182 = _M0L3strS208[_M0L6_2atmpS2183];
        #line 765 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_6006
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2182);
      } else {
        _if__result_6006 = 0;
      }
    } else {
      _if__result_6006 = 0;
    }
  } else {
    _if__result_6006 = 0;
  }
  if (_if__result_6006) {
    int32_t _M0L6_2atmpS2191 = _M0Lm2loS202;
    _M0Lm2loS202 = _M0L6_2atmpS2191 + 1;
  }
  _M0L6_2atmpS2200 = _M0Lm2hiS204;
  if (_M0L6_2atmpS2200 > 0) {
    int32_t _M0L6_2atmpS2199 = _M0Lm2hiS204;
    if (_M0L6_2atmpS2199 < _M0L3lenS200) {
      int32_t _M0L6_2atmpS2198 = _M0Lm2hiS204;
      int32_t _M0L6_2atmpS2197 = _M0L4baseS209 + _M0L6_2atmpS2198;
      int32_t _M0L6_2atmpS2196 = _M0L3strS208[_M0L6_2atmpS2197];
      #line 770 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
      if (_M0MPC16uint166UInt1623is__trailing__surrogate(_M0L6_2atmpS2196)) {
        int32_t _M0L6_2atmpS2195 = _M0Lm2hiS204;
        int32_t _M0L6_2atmpS2194 = _M0L4baseS209 + _M0L6_2atmpS2195;
        int32_t _M0L6_2atmpS2193 = _M0L6_2atmpS2194 - 1;
        int32_t _M0L6_2atmpS2192 = _M0L3strS208[_M0L6_2atmpS2193];
        #line 771 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringview.mbt"
        _if__result_6007
        = _M0MPC16uint166UInt1622is__leading__surrogate(_M0L6_2atmpS2192);
      } else {
        _if__result_6007 = 0;
      }
    } else {
      _if__result_6007 = 0;
    }
  } else {
    _if__result_6007 = 0;
  }
  if (_if__result_6007) {
    int32_t _M0L6_2atmpS2201 = _M0Lm2hiS204;
    _M0Lm2hiS204 = _M0L6_2atmpS2201 - 1;
  }
  _M0L6_2atmpS2202 = _M0Lm2loS202;
  _M0L6_2atmpS2203 = _M0Lm2hiS204;
  if (_M0L6_2atmpS2202 >= _M0L6_2atmpS2203) {
    int32_t _M0L6_2atmpS2207 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS2204 = _M0L4baseS209 + _M0L6_2atmpS2207;
    int32_t _M0L6_2atmpS2206 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS2205 = _M0L4baseS209 + _M0L6_2atmpS2206;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS2204,
                                                 .$2 = _M0L6_2atmpS2205};
  } else {
    int32_t _M0L6_2atmpS2211 = _M0Lm2loS202;
    int32_t _M0L6_2atmpS2208 = _M0L4baseS209 + _M0L6_2atmpS2211;
    int32_t _M0L6_2atmpS2210 = _M0Lm2hiS204;
    int32_t _M0L6_2atmpS2209 = _M0L4baseS209 + _M0L6_2atmpS2210;
    moonbit_incref_cycle_free(_M0L3strS208);
    return (struct _M0TPC16string10StringView){.$0 = _M0L3strS208,
                                                 .$1 = _M0L6_2atmpS2208,
                                                 .$2 = _M0L6_2atmpS2209};
  }
}

moonbit_string_t _M0MPC14byte4Byte7to__hex(int32_t _M0L1bS199) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS198;
  int32_t _M0L6_2atmpS2179;
  int32_t _M0L6_2atmpS2178;
  int32_t _M0L6_2atmpS2181;
  int32_t _M0L6_2atmpS2180;
  struct _M0TPB13StringBuilder* _M0L6_2atmpS2177;
  moonbit_string_t _result_6008;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L7_2aselfS198 = _M0MPB13StringBuilder21StringBuilder_2einner(0);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2179 = _M0IPC14byte4BytePB3Div3div(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2178
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS2179);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS2178);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2181 = _M0IPC14byte4BytePB3Mod3mod(_M0L1bS199, 16);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0L6_2atmpS2180
  = _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(_M0L6_2atmpS2181);
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS198, _M0L6_2atmpS2180);
  _M0L6_2atmpS2177 = _M0L7_2aselfS198;
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  _result_6008 = _M0MPB13StringBuilder10to__string(_M0L6_2atmpS2177);
  moonbit_decref_cycle_free(_M0L6_2atmpS2177);
  return _result_6008;
}

int32_t _M0MPC14byte4Byte7to__hexN14to__hex__digitS4391(int32_t _M0L1iS197) {
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
  if (_M0L1iS197 < 10) {
    int32_t _M0L6_2atmpS2174;
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2174 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 48);
    #line 77 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS2174);
  } else {
    int32_t _M0L6_2atmpS2176;
    int32_t _M0L6_2atmpS2175;
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2176 = _M0IPC14byte4BytePB3Add3add(_M0L1iS197, 97);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    _M0L6_2atmpS2175 = _M0IPC14byte4BytePB3Sub3sub(_M0L6_2atmpS2176, 10);
    #line 79 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\show.mbt"
    return _M0MPC14byte4Byte8to__char(_M0L6_2atmpS2175);
  }
}

int32_t _M0IPC14byte4BytePB3Sub3sub(
  int32_t _M0L4selfS195,
  int32_t _M0L4thatS196
) {
  int32_t _M0L6_2atmpS2172;
  int32_t _M0L6_2atmpS2173;
  int32_t _M0L6_2atmpS2171;
  #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2172 = (int32_t)_M0L4selfS195;
  _M0L6_2atmpS2173 = (int32_t)_M0L4thatS196;
  _M0L6_2atmpS2171 = _M0L6_2atmpS2172 - _M0L6_2atmpS2173;
  return _M0L6_2atmpS2171 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Mod3mod(
  int32_t _M0L4selfS193,
  int32_t _M0L4thatS194
) {
  int32_t _M0L6_2atmpS2169;
  int32_t _M0L6_2atmpS2170;
  int32_t _M0L6_2atmpS2168;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2169 = (int32_t)_M0L4selfS193;
  _M0L6_2atmpS2170 = (int32_t)_M0L4thatS194;
  _M0L6_2atmpS2168 = _M0L6_2atmpS2169 % _M0L6_2atmpS2170;
  return _M0L6_2atmpS2168 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Div3div(
  int32_t _M0L4selfS191,
  int32_t _M0L4thatS192
) {
  int32_t _M0L6_2atmpS2166;
  int32_t _M0L6_2atmpS2167;
  int32_t _M0L6_2atmpS2165;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2166 = (int32_t)_M0L4selfS191;
  _M0L6_2atmpS2167 = (int32_t)_M0L4thatS192;
  _M0L6_2atmpS2165 = _M0L6_2atmpS2166 / _M0L6_2atmpS2167;
  return _M0L6_2atmpS2165 & 0xff;
}

int32_t _M0IPC14byte4BytePB3Add3add(
  int32_t _M0L4selfS189,
  int32_t _M0L4thatS190
) {
  int32_t _M0L6_2atmpS2163;
  int32_t _M0L6_2atmpS2164;
  int32_t _M0L6_2atmpS2162;
  #line 119 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\byte.mbt"
  _M0L6_2atmpS2163 = (int32_t)_M0L4selfS189;
  _M0L6_2atmpS2164 = (int32_t)_M0L4thatS190;
  _M0L6_2atmpS2162 = _M0L6_2atmpS2163 + _M0L6_2atmpS2164;
  return _M0L6_2atmpS2162 & 0xff;
}

int32_t _M0MPC16uint166UInt1616unsafe__to__char(int32_t _M0L4selfS188) {
  int32_t _M0L6_2atmpS2161;
  #line 81 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uint16_char.mbt"
  _M0L6_2atmpS2161 = (int32_t)_M0L4selfS188;
  return _M0L6_2atmpS2161;
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
  int32_t _M0L3lenS2160;
  int32_t _M0L8requiredS184;
  uint16_t* _M0L4dataS2155;
  int32_t _M0L6_2atmpS2154;
  int32_t _if__result_6009;
  uint16_t* _M0L4dataS2156;
  int32_t _M0L3lenS2157;
  int32_t _M0L3lenS2159;
  int32_t _M0L6_2atmpS2158;
  #line 105 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L8str__lenS182 = Moonbit_array_length(_M0L3strS183);
  if (_M0L8str__lenS182 == 0) {
    return 0;
  }
  _M0L3lenS2160 = _M0L4selfS185->$1;
  _M0L8requiredS184 = _M0L3lenS2160 + _M0L8str__lenS182;
  _M0L4dataS2155 = _M0L4selfS185->$0;
  _M0L6_2atmpS2154 = Moonbit_array_length(_M0L4dataS2155);
  if (_M0L8requiredS184 > _M0L6_2atmpS2154) {
    _if__result_6009 = 1;
  } else {
    int32_t _M0L3lenS2153 = _M0L4selfS185->$1;
    _if__result_6009 = _M0L8requiredS184 < _M0L3lenS2153;
  }
  if (_if__result_6009) {
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0MPB13StringBuilder4grow(_M0L4selfS185, _M0L8requiredS184);
  }
  _M0L4dataS2156 = _M0L4selfS185->$0;
  _M0L3lenS2157 = _M0L4selfS185->$1;
  moonbit_incref_cycle_free(_M0L4dataS2156);
  #line 114 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0MPC15array10FixedArray26unsafe__blit__from__string(_M0L4dataS2156, _M0L3lenS2157, _M0L3strS183, 0, _M0L8str__lenS182);
  moonbit_decref_cycle_free(_M0L4dataS2156);
  _M0L3lenS2159 = _M0L4selfS185->$1;
  _M0L6_2atmpS2158 = _M0L3lenS2159 + _M0L8str__lenS182;
  _M0L4selfS185->$1 = _M0L6_2atmpS2158;
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
      int32_t _M0L6_2atmpS2150 = _M0L3strS179[_M0L1iS176];
      int32_t _M0L6_2atmpS2151;
      int32_t _M0L6_2atmpS2152;
      _M0L4selfS178[_M0L1jS177] = _M0L6_2atmpS2150;
      _M0L6_2atmpS2151 = _M0L1iS176 + 1;
      _M0L6_2atmpS2152 = _M0L1jS177 + 1;
      _M0L1iS176 = _M0L6_2atmpS2151;
      _M0L1jS177 = _M0L6_2atmpS2152;
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
    int32_t _M0L3lenS2121 = _M0L4selfS171->$1;
    uint16_t* _M0L4dataS2123 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS2122 = Moonbit_array_length(_M0L4dataS2123);
    uint16_t* _M0L4dataS2126;
    int32_t _M0L3lenS2127;
    int32_t _M0L6_2atmpS2128;
    int32_t _M0L3lenS2130;
    int32_t _M0L6_2atmpS2129;
    if (_M0L3lenS2121 >= _M0L6_2atmpS2122) {
      int32_t _M0L3lenS2125 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS2124 = _M0L3lenS2125 + 1;
      #line 124 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS2124);
    }
    _M0L4dataS2126 = _M0L4selfS171->$0;
    _M0L3lenS2127 = _M0L4selfS171->$1;
    moonbit_incref_cycle_free(_M0L4dataS2126);
    #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS2128 = _M0MPC14uint4UInt10to__uint16(_M0L4codeS169);
    if (
      _M0L3lenS2127 < 0
      || _M0L3lenS2127 >= Moonbit_array_length(_M0L4dataS2126)
    ) {
      #line 126 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS2126[_M0L3lenS2127] = _M0L6_2atmpS2128;
    moonbit_decref_cycle_free(_M0L4dataS2126);
    _M0L3lenS2130 = _M0L4selfS171->$1;
    _M0L6_2atmpS2129 = _M0L3lenS2130 + 1;
    _M0L4selfS171->$1 = _M0L6_2atmpS2129;
  } else if (_M0L4codeS169 <= 1114111u) {
    uint16_t* _M0L4dataS2134 = _M0L4selfS171->$0;
    int32_t _M0L6_2atmpS2132 = Moonbit_array_length(_M0L4dataS2134);
    int32_t _M0L3lenS2133 = _M0L4selfS171->$1;
    int32_t _M0L6_2atmpS2131 = _M0L6_2atmpS2132 - _M0L3lenS2133;
    uint32_t _M0L4codeS172;
    uint16_t* _M0L4dataS2137;
    int32_t _M0L3lenS2138;
    uint32_t _M0L6_2atmpS2141;
    uint32_t _M0L6_2atmpS2140;
    int32_t _M0L6_2atmpS2139;
    uint16_t* _M0L4dataS2142;
    int32_t _M0L3lenS2147;
    int32_t _M0L6_2atmpS2143;
    uint32_t _M0L6_2atmpS2146;
    uint32_t _M0L6_2atmpS2145;
    int32_t _M0L6_2atmpS2144;
    int32_t _M0L3lenS2149;
    int32_t _M0L6_2atmpS2148;
    if (_M0L6_2atmpS2131 < 2) {
      int32_t _M0L3lenS2136 = _M0L4selfS171->$1;
      int32_t _M0L6_2atmpS2135 = _M0L3lenS2136 + 2;
      #line 130 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0MPB13StringBuilder4grow(_M0L4selfS171, _M0L6_2atmpS2135);
    }
    _M0L4codeS172 = _M0L4codeS169 - 65536u;
    _M0L4dataS2137 = _M0L4selfS171->$0;
    _M0L3lenS2138 = _M0L4selfS171->$1;
    _M0L6_2atmpS2141 = _M0L4codeS172 >> 10;
    _M0L6_2atmpS2140 = 55296u + _M0L6_2atmpS2141;
    moonbit_incref_cycle_free(_M0L4dataS2137);
    #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS2139 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS2140);
    if (
      _M0L3lenS2138 < 0
      || _M0L3lenS2138 >= Moonbit_array_length(_M0L4dataS2137)
    ) {
      #line 133 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS2137[_M0L3lenS2138] = _M0L6_2atmpS2139;
    moonbit_decref_cycle_free(_M0L4dataS2137);
    _M0L4dataS2142 = _M0L4selfS171->$0;
    _M0L3lenS2147 = _M0L4selfS171->$1;
    _M0L6_2atmpS2143 = _M0L3lenS2147 + 1;
    _M0L6_2atmpS2146 = _M0L4codeS172 & 1023u;
    _M0L6_2atmpS2145 = 56320u + _M0L6_2atmpS2146;
    moonbit_incref_cycle_free(_M0L4dataS2142);
    #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0L6_2atmpS2144 = _M0MPC14uint4UInt10to__uint16(_M0L6_2atmpS2145);
    if (
      _M0L6_2atmpS2143 < 0
      || _M0L6_2atmpS2143 >= Moonbit_array_length(_M0L4dataS2142)
    ) {
      #line 134 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      moonbit_panic();
    }
    _M0L4dataS2142[_M0L6_2atmpS2143] = _M0L6_2atmpS2144;
    moonbit_decref_cycle_free(_M0L4dataS2142);
    _M0L3lenS2149 = _M0L4selfS171->$1;
    _M0L6_2atmpS2148 = _M0L3lenS2149 + 2;
    _M0L4selfS171->$1 = _M0L6_2atmpS2148;
  } else {
    #line 137 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_26.data);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder4grow(
  struct _M0TPB13StringBuilder* _M0L4selfS166,
  int32_t _M0L8requiredS167
) {
  uint16_t* _M0L4dataS2120;
  int32_t _M0L6_2atmpS2118;
  int32_t _M0L3lenS2119;
  int32_t _M0L13new__capacityS165;
  uint16_t* _M0L4dataS2115;
  int32_t _M0L6_2atmpS2116;
  int32_t _M0L3lenS2117;
  uint16_t* _M0L9new__dataS168;
  uint16_t* _M0L6_2aoldS5557;
  #line 74 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L4dataS2120 = _M0L4selfS166->$0;
  _M0L6_2atmpS2118 = Moonbit_array_length(_M0L4dataS2120);
  _M0L3lenS2119 = _M0L4selfS166->$1;
  #line 75 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L13new__capacityS165
  = _M0FPB31stringbuilder__growth__capacity(_M0L6_2atmpS2118, _M0L3lenS2119, _M0L8requiredS167);
  _M0L4dataS2115 = _M0L4selfS166->$0;
  moonbit_incref_cycle_free(_M0L4dataS2115);
  #line 83 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L6_2atmpS2116 = _M0IPC16uint166UInt16PB7Default7default();
  _M0L3lenS2117 = _M0L4selfS166->$1;
  #line 80 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L9new__dataS168
  = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS2115, _M0L13new__capacityS165, _M0L6_2atmpS2116, _M0L3lenS2117, 0, 0);
  _M0L6_2aoldS5557 = _M0L4selfS166->$0;
  moonbit_decref_cycle_free(_M0L6_2aoldS5557);
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
    _M0FPC15abort5abortGuE((moonbit_string_t)moonbit_string_literal_27.data);
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
  int32_t _M0L6_2atmpS2114;
  #line 2785 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2114 = *(int32_t*)&_M0L4selfS158;
  return (uint16_t)_M0L6_2atmpS2114;
}

uint32_t _M0MPC14char4Char8to__uint(int32_t _M0L4selfS157) {
  int32_t _M0L6_2atmpS2113;
  #line 1335 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2113 = _M0L4selfS157;
  return *(uint32_t*)&_M0L6_2atmpS2113;
}

moonbit_string_t _M0MPB13StringBuilder10to__string(
  struct _M0TPB13StringBuilder* _M0L4selfS155
) {
  int32_t _M0L3lenS2104;
  #line 181 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  _M0L3lenS2104 = _M0L4selfS155->$1;
  if (_M0L3lenS2104 == 0) {
    return (moonbit_string_t)moonbit_string_literal_0.data;
  } else {
    int32_t _M0L3lenS2105 = _M0L4selfS155->$1;
    uint16_t* _M0L4dataS2107 = _M0L4selfS155->$0;
    int32_t _M0L6_2atmpS2106 = Moonbit_array_length(_M0L4dataS2107);
    if (_M0L3lenS2105 == _M0L6_2atmpS2106) {
      uint16_t* _M0L4dataS2108 = _M0L4selfS155->$0;
      moonbit_incref_cycle_free(_M0L4dataS2108);
      return _M0L4dataS2108;
    } else {
      uint16_t* _M0L4dataS2109 = _M0L4selfS155->$0;
      int32_t _M0L3lenS2110 = _M0L4selfS155->$1;
      int32_t _M0L6_2atmpS2111;
      int32_t _M0L3lenS2112;
      uint16_t* _M0L4dataS156;
      moonbit_incref_cycle_free(_M0L4dataS2109);
      #line 190 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L6_2atmpS2111 = _M0IPC16uint166UInt16PB7Default7default();
      _M0L3lenS2112 = _M0L4selfS155->$1;
      #line 187 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
      _M0L4dataS156
      = _M0MPC15array10FixedArray23make__and__blit_2einnerGkE(_M0L4dataS2109, _M0L3lenS2110, _M0L6_2atmpS2111, _M0L3lenS2112, 0, 0);
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
  int32_t _if__result_6012;
  #line 97 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
  if (_M0L13allocate__lenS148 >= 0) {
    if (_M0L3lenS149 >= 0) {
      if (_M0L11src__offsetS150 >= 0) {
        if (_M0L11dst__offsetS151 >= 0) {
          int32_t _M0L6_2atmpS2100 = _M0L11src__offsetS150 + _M0L3lenS149;
          int32_t _M0L6_2atmpS2101 = Moonbit_array_length(_M0L3srcS152);
          if (_M0L6_2atmpS2100 <= _M0L6_2atmpS2101) {
            int32_t _M0L6_2atmpS2099 = _M0L11dst__offsetS151 + _M0L3lenS149;
            _if__result_6012 = _M0L6_2atmpS2099 <= _M0L13allocate__lenS148;
          } else {
            _if__result_6012 = 0;
          }
        } else {
          _if__result_6012 = 0;
        }
      } else {
        _if__result_6012 = 0;
      }
    } else {
      _if__result_6012 = 0;
    }
  } else {
    _if__result_6012 = 0;
  }
  if (_if__result_6012) {
    #line 116 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    return _M0MPC15array10FixedArray23unsafe__make__and__blitGkE(_M0L3srcS152, _M0L13allocate__lenS148, _M0L4initS153, _M0L11src__offsetS150, _M0L11dst__offsetS151, _M0L3lenS149);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS154;
    int32_t _M0L6_2atmpS2103;
    moonbit_string_t _M0L6_2atmpS2102;
    uint16_t* _result_6013;
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L18_2astring__builderS154
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L13allocate__lenS148);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11src__offsetS150);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L11dst__offsetS151);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L3lenS149);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS154, (moonbit_string_t)moonbit_string_literal_32.data);
    _M0L6_2atmpS2103 = Moonbit_array_length(_M0L3srcS152);
    moonbit_decref_cycle_free(_M0L3srcS152);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS154, _M0L6_2atmpS2103);
    #line 113 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _M0L6_2atmpS2102
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS154);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS154);
    #line 112 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
    _result_6013 = _M0FPC15abort5abortGAkE(_M0L6_2atmpS2102);
    moonbit_decref_cycle_free(_M0L6_2atmpS2102);
    return _result_6013;
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
  struct _M0TPB13StringBuilder* _block_6014;
  #line 32 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder_buffer.mbt"
  if (_M0L10size__hintS139 < 1) {
    _M0L7initialS138 = 1;
  } else {
    int32_t _M0L6_2atmpS2098 = _M0L10size__hintS139 + 1;
    _M0L7initialS138 = _M0L6_2atmpS2098 / 2;
  }
  _M0L4dataS140 = (uint16_t*)moonbit_make_string(_M0L7initialS138, 0);
  _block_6014
  = (struct _M0TPB13StringBuilder*)moonbit_malloc(sizeof(struct _M0TPB13StringBuilder));
  Moonbit_object_header(_block_6014)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 99, 0);
  _block_6014->$0 = _M0L4dataS140;
  _block_6014->$1 = 0;
  return _block_6014;
}

int32_t _M0MPC14byte4Byte8to__char(int32_t _M0L4selfS137) {
  int32_t _M0L6_2atmpS2097;
  #line 1950 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\intrinsics.mbt"
  _M0L6_2atmpS2097 = (int32_t)_M0L4selfS137;
  return _M0L6_2atmpS2097;
}

moonbit_string_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGsE(
  moonbit_string_t* _M0L3srcS117,
  int32_t _M0L13allocate__lenS113,
  int32_t _M0L3lenS114,
  int32_t _M0L11src__offsetS115,
  int32_t _M0L11dst__offsetS116
) {
  int32_t _if__result_6015;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS113 >= 0) {
    if (_M0L3lenS114 >= 0) {
      if (_M0L11src__offsetS115 >= 0) {
        if (_M0L11dst__offsetS116 >= 0) {
          int32_t _M0L6_2atmpS2078 = _M0L11src__offsetS115 + _M0L3lenS114;
          int32_t _M0L6_2atmpS2079;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2079
          = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS117);
          if (_M0L6_2atmpS2078 <= _M0L6_2atmpS2079) {
            int32_t _M0L6_2atmpS2077 = _M0L11dst__offsetS116 + _M0L3lenS114;
            _if__result_6015 = _M0L6_2atmpS2077 <= _M0L13allocate__lenS113;
          } else {
            _if__result_6015 = 0;
          }
        } else {
          _if__result_6015 = 0;
        }
      } else {
        _if__result_6015 = 0;
      }
    } else {
      _if__result_6015 = 0;
    }
  } else {
    _if__result_6015 = 0;
  }
  if (_if__result_6015) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (moonbit_string_t*)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS113, (moonbit_string_t)moonbit_string_literal_0.data, _M0L3srcS117, _M0L11src__offsetS115, _M0L11dst__offsetS116, _M0L3lenS114);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS118;
    int32_t _M0L6_2atmpS2081;
    moonbit_string_t _M0L6_2atmpS2080;
    moonbit_string_t* _result_6016;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS118
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L13allocate__lenS113);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11src__offsetS115);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L11dst__offsetS116);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L3lenS114);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS118, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2081 = _M0MPB18UninitializedArray6lengthGsE(_M0L3srcS117);
    moonbit_decref_cycle_free(_M0L3srcS117);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS118, _M0L6_2atmpS2081);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2080
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS118);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS118);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_6016
    = _M0FPC15abort5abortGRPB18UninitializedArrayGsEE(_M0L6_2atmpS2080);
    moonbit_decref_cycle_free(_M0L6_2atmpS2080);
    return _result_6016;
  }
}

struct _M0TUsiE** _M0MPB18UninitializedArray23make__and__blit_2einnerGUsiEE(
  struct _M0TUsiE** _M0L3srcS123,
  int32_t _M0L13allocate__lenS119,
  int32_t _M0L3lenS120,
  int32_t _M0L11src__offsetS121,
  int32_t _M0L11dst__offsetS122
) {
  int32_t _if__result_6017;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS119 >= 0) {
    if (_M0L3lenS120 >= 0) {
      if (_M0L11src__offsetS121 >= 0) {
        if (_M0L11dst__offsetS122 >= 0) {
          int32_t _M0L6_2atmpS2083 = _M0L11src__offsetS121 + _M0L3lenS120;
          int32_t _M0L6_2atmpS2084;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2084
          = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS123);
          if (_M0L6_2atmpS2083 <= _M0L6_2atmpS2084) {
            int32_t _M0L6_2atmpS2082 = _M0L11dst__offsetS122 + _M0L3lenS120;
            _if__result_6017 = _M0L6_2atmpS2082 <= _M0L13allocate__lenS119;
          } else {
            _if__result_6017 = 0;
          }
        } else {
          _if__result_6017 = 0;
        }
      } else {
        _if__result_6017 = 0;
      }
    } else {
      _if__result_6017 = 0;
    }
  } else {
    _if__result_6017 = 0;
  }
  if (_if__result_6017) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return (struct _M0TUsiE**)moonbit_make_ref_array_with_blit(_M0L13allocate__lenS119, 0, _M0L3srcS123, _M0L11src__offsetS121, _M0L11dst__offsetS122, _M0L3lenS120);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS124;
    int32_t _M0L6_2atmpS2086;
    moonbit_string_t _M0L6_2atmpS2085;
    struct _M0TUsiE** _result_6018;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS124
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L13allocate__lenS119);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11src__offsetS121);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L11dst__offsetS122);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L3lenS120);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS124, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2086 = _M0MPB18UninitializedArray6lengthGUsiEE(_M0L3srcS123);
    moonbit_decref_cycle_free(_M0L3srcS123);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS124, _M0L6_2atmpS2086);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2085
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS124);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS124);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_6018
    = _M0FPC15abort5abortGRPB18UninitializedArrayGUsiEEE(_M0L6_2atmpS2085);
    moonbit_decref_cycle_free(_M0L6_2atmpS2085);
    return _result_6018;
  }
}

float* _M0MPB18UninitializedArray23make__and__blit_2einnerGfE(
  float* _M0L3srcS129,
  int32_t _M0L13allocate__lenS125,
  int32_t _M0L3lenS126,
  int32_t _M0L11src__offsetS127,
  int32_t _M0L11dst__offsetS128
) {
  int32_t _if__result_6019;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS125 >= 0) {
    if (_M0L3lenS126 >= 0) {
      if (_M0L11src__offsetS127 >= 0) {
        if (_M0L11dst__offsetS128 >= 0) {
          int32_t _M0L6_2atmpS2088 = _M0L11src__offsetS127 + _M0L3lenS126;
          int32_t _M0L6_2atmpS2089;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2089
          = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS129);
          if (_M0L6_2atmpS2088 <= _M0L6_2atmpS2089) {
            int32_t _M0L6_2atmpS2087 = _M0L11dst__offsetS128 + _M0L3lenS126;
            _if__result_6019 = _M0L6_2atmpS2087 <= _M0L13allocate__lenS125;
          } else {
            _if__result_6019 = 0;
          }
        } else {
          _if__result_6019 = 0;
        }
      } else {
        _if__result_6019 = 0;
      }
    } else {
      _if__result_6019 = 0;
    }
  } else {
    _if__result_6019 = 0;
  }
  if (_if__result_6019) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(_M0L3srcS129, _M0L13allocate__lenS125, _M0L11src__offsetS127, _M0L11dst__offsetS128, _M0L3lenS126);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS130;
    int32_t _M0L6_2atmpS2091;
    moonbit_string_t _M0L6_2atmpS2090;
    float* _result_6020;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS130
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L13allocate__lenS125);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11src__offsetS127);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L11dst__offsetS128);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L3lenS126);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS130, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2091 = _M0MPB18UninitializedArray6lengthGfE(_M0L3srcS129);
    moonbit_decref_cycle_free(_M0L3srcS129);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS130, _M0L6_2atmpS2091);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2090
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS130);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS130);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_6020
    = _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(_M0L6_2atmpS2090);
    moonbit_decref_cycle_free(_M0L6_2atmpS2090);
    return _result_6020;
  }
}

int32_t* _M0MPB18UninitializedArray23make__and__blit_2einnerGiE(
  int32_t* _M0L3srcS135,
  int32_t _M0L13allocate__lenS131,
  int32_t _M0L3lenS132,
  int32_t _M0L11src__offsetS133,
  int32_t _M0L11dst__offsetS134
) {
  int32_t _if__result_6021;
  #line 201 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  if (_M0L13allocate__lenS131 >= 0) {
    if (_M0L3lenS132 >= 0) {
      if (_M0L11src__offsetS133 >= 0) {
        if (_M0L11dst__offsetS134 >= 0) {
          int32_t _M0L6_2atmpS2093 = _M0L11src__offsetS133 + _M0L3lenS132;
          int32_t _M0L6_2atmpS2094;
          #line 213 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
          _M0L6_2atmpS2094
          = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS135);
          if (_M0L6_2atmpS2093 <= _M0L6_2atmpS2094) {
            int32_t _M0L6_2atmpS2092 = _M0L11dst__offsetS134 + _M0L3lenS132;
            _if__result_6021 = _M0L6_2atmpS2092 <= _M0L13allocate__lenS131;
          } else {
            _if__result_6021 = 0;
          }
        } else {
          _if__result_6021 = 0;
        }
      } else {
        _if__result_6021 = 0;
      }
    } else {
      _if__result_6021 = 0;
    }
  } else {
    _if__result_6021 = 0;
  }
  if (_if__result_6021) {
    #line 219 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    return _M0MPB18UninitializedArray23unsafe__make__and__blitGiE(_M0L3srcS135, _M0L13allocate__lenS131, _M0L11src__offsetS133, _M0L11dst__offsetS134, _M0L3lenS132);
  } else {
    struct _M0TPB13StringBuilder* _M0L18_2astring__builderS136;
    int32_t _M0L6_2atmpS2096;
    moonbit_string_t _M0L6_2atmpS2095;
    int32_t* _result_6022;
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L18_2astring__builderS136
    = _M0MPB13StringBuilder21StringBuilder_2einner(89);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_28.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L13allocate__lenS131);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_29.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11src__offsetS133);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_30.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L11dst__offsetS134);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_31.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L3lenS132);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0IPB13StringBuilderPB6Logger13write__string(_M0L18_2astring__builderS136, (moonbit_string_t)moonbit_string_literal_32.data);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2096 = _M0MPB18UninitializedArray6lengthGiE(_M0L3srcS135);
    moonbit_decref_cycle_free(_M0L3srcS135);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0MPB13StringBuilder13write__objectGiE(_M0L18_2astring__builderS136, _M0L6_2atmpS2096);
    #line 216 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _M0L6_2atmpS2095
    = _M0MPB13StringBuilder10to__string(_M0L18_2astring__builderS136);
    moonbit_decref_cycle_free(_M0L18_2astring__builderS136);
    #line 215 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
    _result_6022
    = _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(_M0L6_2atmpS2095);
    moonbit_decref_cycle_free(_M0L6_2atmpS2095);
    return _result_6022;
  }
}

int32_t _M0MPB13StringBuilder13write__objectGsE(
  struct _M0TPB13StringBuilder* _M0L4selfS108,
  moonbit_string_t _M0L3objS107
) {
  struct _M0TPB6Logger _M0L6_2atmpS2074;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS108);
  _M0L6_2atmpS2074
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS108
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGsE(_M0L3objS107, _M0L6_2atmpS2074);
  if (_M0L6_2atmpS2074.$1) {
    moonbit_decref(_M0L6_2atmpS2074.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGiE(
  struct _M0TPB13StringBuilder* _M0L4selfS110,
  int32_t _M0L3objS109
) {
  struct _M0TPB6Logger _M0L6_2atmpS2075;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS110);
  _M0L6_2atmpS2075
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS110
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGiE(_M0L3objS109, _M0L6_2atmpS2075);
  if (_M0L6_2atmpS2075.$1) {
    moonbit_decref(_M0L6_2atmpS2075.$1);
  }
  return 0;
}

int32_t _M0MPB13StringBuilder13write__objectGmE(
  struct _M0TPB13StringBuilder* _M0L4selfS112,
  uint64_t _M0L3objS111
) {
  struct _M0TPB6Logger _M0L6_2atmpS2076;
  #line 17 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  moonbit_incref_cycle_free(_M0L4selfS112);
  _M0L6_2atmpS2076
  = (struct _M0TPB6Logger){
    _M0FP0119moonbitlang_2fcore_2fbuiltin_2fStringBuilder_2eas___40moonbitlang_2fcore_2fbuiltin_2eLogger_2estatic__method__table__id,
      _M0L4selfS112
  };
  #line 23 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\stringbuilder.mbt"
  _M0IP016_24default__implPB4Show6outputGmE(_M0L3objS111, _M0L6_2atmpS2076);
  if (_M0L6_2atmpS2076.$1) {
    moonbit_decref(_M0L6_2atmpS2076.$1);
  }
  return 0;
}

moonbit_string_t* _M0MPB18UninitializedArray23unsafe__make__and__blitGsE(
  moonbit_string_t* _M0L3srcS86,
  int32_t _M0L13allocate__lenS84,
  int32_t _M0L11src__offsetS87,
  int32_t _M0L11dst__offsetS85,
  int32_t _M0L9blit__lenS88
) {
  moonbit_string_t* _M0L3dstS83;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS83
  = (moonbit_string_t*)moonbit_make_ref_array(_M0L13allocate__lenS84, (moonbit_string_t)moonbit_string_literal_0.data);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGsE(_M0L3dstS83, _M0L11dst__offsetS85, _M0L3srcS86, _M0L11src__offsetS87, _M0L9blit__lenS88);
  moonbit_decref_cycle_free(_M0L3srcS86);
  return _M0L3dstS83;
}

struct _M0TUsiE** _M0MPB18UninitializedArray23unsafe__make__and__blitGUsiEE(
  struct _M0TUsiE** _M0L3srcS92,
  int32_t _M0L13allocate__lenS90,
  int32_t _M0L11src__offsetS93,
  int32_t _M0L11dst__offsetS91,
  int32_t _M0L9blit__lenS94
) {
  struct _M0TUsiE** _M0L3dstS89;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS89
  = (struct _M0TUsiE**)moonbit_make_ref_array(_M0L13allocate__lenS90, 0);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGUsiEE(_M0L3dstS89, _M0L11dst__offsetS91, _M0L3srcS92, _M0L11src__offsetS93, _M0L9blit__lenS94);
  moonbit_decref_cycle_free(_M0L3srcS92);
  return _M0L3dstS89;
}

float* _M0MPB18UninitializedArray23unsafe__make__and__blitGfE(
  float* _M0L3srcS98,
  int32_t _M0L13allocate__lenS96,
  int32_t _M0L11src__offsetS99,
  int32_t _M0L11dst__offsetS97,
  int32_t _M0L9blit__lenS100
) {
  float* _M0L3dstS95;
  #line 166 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0L3dstS95 = (float*)moonbit_make_float_array_raw(_M0L13allocate__lenS96);
  #line 176 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  _M0MPB18UninitializedArray12unsafe__blitGfE(_M0L3dstS95, _M0L11dst__offsetS97, _M0L3srcS98, _M0L11src__offsetS99, _M0L9blit__lenS100);
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGsE(
  moonbit_string_t* _M0L3dstS63,
  int32_t _M0L11dst__offsetS64,
  moonbit_string_t* _M0L3srcS65,
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

int32_t _M0MPB18UninitializedArray12unsafe__blitGUsiEE(
  struct _M0TUsiE** _M0L3dstS68,
  int32_t _M0L11dst__offsetS69,
  struct _M0TUsiE** _M0L3srcS70,
  int32_t _M0L11src__offsetS71,
  int32_t _M0L3lenS72
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS70);
  moonbit_incref_cycle_free(_M0L3dstS68);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_ref_array_blit(_M0L3dstS68, _M0L11dst__offsetS69, _M0L3srcS70, _M0L11src__offsetS71, _M0L3lenS72);
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGfE(
  float* _M0L3dstS73,
  int32_t _M0L11dst__offsetS74,
  float* _M0L3srcS75,
  int32_t _M0L11src__offsetS76,
  int32_t _M0L3lenS77
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS75);
  moonbit_incref_cycle_free(_M0L3dstS73);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS73, _M0L11dst__offsetS74, _M0L3srcS75, _M0L11src__offsetS76, _M0L3lenS77, sizeof(float));
  return 0;
}

int32_t _M0MPB18UninitializedArray12unsafe__blitGiE(
  int32_t* _M0L3dstS78,
  int32_t _M0L11dst__offsetS79,
  int32_t* _M0L3srcS80,
  int32_t _M0L11src__offsetS81,
  int32_t _M0L3lenS82
) {
  #line 152 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_incref_cycle_free(_M0L3srcS80);
  moonbit_incref_cycle_free(_M0L3dstS78);
  #line 161 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\uninitialized_array.mbt"
  moonbit_unsafe_val_array_blit(_M0L3dstS78, _M0L11dst__offsetS79, _M0L3srcS80, _M0L11src__offsetS81, _M0L3lenS82, sizeof(int32_t));
  return 0;
}

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGfEE(
  float* _M0L3dstS18,
  int32_t _M0L11dst__offsetS20,
  float* _M0L3srcS19,
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
        int32_t _M0L6_2atmpS2029 = _M0L11dst__offsetS20 + _M0L1iS22;
        int32_t _M0L6_2atmpS2031 = _M0L11src__offsetS21 + _M0L1iS22;
        float _M0L6_2atmpS2030;
        int32_t _M0L6_2atmpS2032;
        if (
          _M0L6_2atmpS2031 < 0
          || _M0L6_2atmpS2031 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2030 = (float)_M0L3srcS19[_M0L6_2atmpS2031];
        if (
          _M0L6_2atmpS2029 < 0
          || _M0L6_2atmpS2029 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS2029] = _M0L6_2atmpS2030;
        _M0L6_2atmpS2032 = _M0L1iS22 + 1;
        _M0L1iS22 = _M0L6_2atmpS2032;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS19);
        moonbit_decref_cycle_free(_M0L3dstS18);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2037 = _M0L3lenS23 - 1;
    int32_t _M0L1iS25 = _M0L6_2atmpS2037;
    while (1) {
      if (_M0L1iS25 >= 0) {
        int32_t _M0L6_2atmpS2033 = _M0L11dst__offsetS20 + _M0L1iS25;
        int32_t _M0L6_2atmpS2035 = _M0L11src__offsetS21 + _M0L1iS25;
        float _M0L6_2atmpS2034;
        int32_t _M0L6_2atmpS2036;
        if (
          _M0L6_2atmpS2035 < 0
          || _M0L6_2atmpS2035 >= Moonbit_array_length(_M0L3srcS19)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2034 = (float)_M0L3srcS19[_M0L6_2atmpS2035];
        if (
          _M0L6_2atmpS2033 < 0
          || _M0L6_2atmpS2033 >= Moonbit_array_length(_M0L3dstS18)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS18[_M0L6_2atmpS2033] = _M0L6_2atmpS2034;
        _M0L6_2atmpS2036 = _M0L1iS25 - 1;
        _M0L1iS25 = _M0L6_2atmpS2036;
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

int32_t _M0MPC15array10FixedArray12unsafe__blitGRPB17UnsafeMaybeUninitGiEE(
  int32_t* _M0L3dstS27,
  int32_t _M0L11dst__offsetS29,
  int32_t* _M0L3srcS28,
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
        int32_t _M0L6_2atmpS2038 = _M0L11dst__offsetS29 + _M0L1iS31;
        int32_t _M0L6_2atmpS2040 = _M0L11src__offsetS30 + _M0L1iS31;
        int32_t _M0L6_2atmpS2039;
        int32_t _M0L6_2atmpS2041;
        if (
          _M0L6_2atmpS2040 < 0
          || _M0L6_2atmpS2040 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2039 = (int32_t)_M0L3srcS28[_M0L6_2atmpS2040];
        if (
          _M0L6_2atmpS2038 < 0
          || _M0L6_2atmpS2038 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS2038] = _M0L6_2atmpS2039;
        _M0L6_2atmpS2041 = _M0L1iS31 + 1;
        _M0L1iS31 = _M0L6_2atmpS2041;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS28);
        moonbit_decref_cycle_free(_M0L3dstS27);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2046 = _M0L3lenS32 - 1;
    int32_t _M0L1iS34 = _M0L6_2atmpS2046;
    while (1) {
      if (_M0L1iS34 >= 0) {
        int32_t _M0L6_2atmpS2042 = _M0L11dst__offsetS29 + _M0L1iS34;
        int32_t _M0L6_2atmpS2044 = _M0L11src__offsetS30 + _M0L1iS34;
        int32_t _M0L6_2atmpS2043;
        int32_t _M0L6_2atmpS2045;
        if (
          _M0L6_2atmpS2044 < 0
          || _M0L6_2atmpS2044 >= Moonbit_array_length(_M0L3srcS28)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2043 = (int32_t)_M0L3srcS28[_M0L6_2atmpS2044];
        if (
          _M0L6_2atmpS2042 < 0
          || _M0L6_2atmpS2042 >= Moonbit_array_length(_M0L3dstS27)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS27[_M0L6_2atmpS2042] = _M0L6_2atmpS2043;
        _M0L6_2atmpS2045 = _M0L1iS34 - 1;
        _M0L1iS34 = _M0L6_2atmpS2045;
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
        int32_t _M0L6_2atmpS2047 = _M0L11dst__offsetS38 + _M0L1iS40;
        int32_t _M0L6_2atmpS2049 = _M0L11src__offsetS39 + _M0L1iS40;
        int32_t _M0L6_2atmpS2048;
        int32_t _M0L6_2atmpS2050;
        if (
          _M0L6_2atmpS2049 < 0
          || _M0L6_2atmpS2049 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2048 = (int32_t)_M0L3srcS37[_M0L6_2atmpS2049];
        if (
          _M0L6_2atmpS2047 < 0
          || _M0L6_2atmpS2047 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS36[_M0L6_2atmpS2047] = _M0L6_2atmpS2048;
        _M0L6_2atmpS2050 = _M0L1iS40 + 1;
        _M0L1iS40 = _M0L6_2atmpS2050;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS37);
        moonbit_decref_cycle_free(_M0L3dstS36);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2055 = _M0L3lenS41 - 1;
    int32_t _M0L1iS43 = _M0L6_2atmpS2055;
    while (1) {
      if (_M0L1iS43 >= 0) {
        int32_t _M0L6_2atmpS2051 = _M0L11dst__offsetS38 + _M0L1iS43;
        int32_t _M0L6_2atmpS2053 = _M0L11src__offsetS39 + _M0L1iS43;
        int32_t _M0L6_2atmpS2052;
        int32_t _M0L6_2atmpS2054;
        if (
          _M0L6_2atmpS2053 < 0
          || _M0L6_2atmpS2053 >= Moonbit_array_length(_M0L3srcS37)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2052 = (int32_t)_M0L3srcS37[_M0L6_2atmpS2053];
        if (
          _M0L6_2atmpS2051 < 0
          || _M0L6_2atmpS2051 >= Moonbit_array_length(_M0L3dstS36)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L3dstS36[_M0L6_2atmpS2051] = _M0L6_2atmpS2052;
        _M0L6_2atmpS2054 = _M0L1iS43 - 1;
        _M0L1iS43 = _M0L6_2atmpS2054;
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
        int32_t _M0L6_2atmpS2056 = _M0L11dst__offsetS47 + _M0L1iS49;
        int32_t _M0L6_2atmpS2058 = _M0L11src__offsetS48 + _M0L1iS49;
        moonbit_string_t _M0L6_2atmpS2057;
        moonbit_string_t _M0L6_2aoldS5558;
        int32_t _M0L6_2atmpS2059;
        if (
          _M0L6_2atmpS2058 < 0
          || _M0L6_2atmpS2058 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2057 = (moonbit_string_t)_M0L3srcS46[_M0L6_2atmpS2058];
        if (
          _M0L6_2atmpS2056 < 0
          || _M0L6_2atmpS2056 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5558 = (moonbit_string_t)_M0L3dstS45[_M0L6_2atmpS2056];
        moonbit_incref_cycle_free(_M0L6_2atmpS2057);
        moonbit_decref_cycle_free(_M0L6_2aoldS5558);
        _M0L3dstS45[_M0L6_2atmpS2056] = _M0L6_2atmpS2057;
        _M0L6_2atmpS2059 = _M0L1iS49 + 1;
        _M0L1iS49 = _M0L6_2atmpS2059;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS46);
        moonbit_decref_cycle_free(_M0L3dstS45);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2064 = _M0L3lenS50 - 1;
    int32_t _M0L1iS52 = _M0L6_2atmpS2064;
    while (1) {
      if (_M0L1iS52 >= 0) {
        int32_t _M0L6_2atmpS2060 = _M0L11dst__offsetS47 + _M0L1iS52;
        int32_t _M0L6_2atmpS2062 = _M0L11src__offsetS48 + _M0L1iS52;
        moonbit_string_t _M0L6_2atmpS2061;
        moonbit_string_t _M0L6_2aoldS5559;
        int32_t _M0L6_2atmpS2063;
        if (
          _M0L6_2atmpS2062 < 0
          || _M0L6_2atmpS2062 >= Moonbit_array_length(_M0L3srcS46)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2061 = (moonbit_string_t)_M0L3srcS46[_M0L6_2atmpS2062];
        if (
          _M0L6_2atmpS2060 < 0
          || _M0L6_2atmpS2060 >= Moonbit_array_length(_M0L3dstS45)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5559 = (moonbit_string_t)_M0L3dstS45[_M0L6_2atmpS2060];
        moonbit_incref_cycle_free(_M0L6_2atmpS2061);
        moonbit_decref_cycle_free(_M0L6_2aoldS5559);
        _M0L3dstS45[_M0L6_2atmpS2060] = _M0L6_2atmpS2061;
        _M0L6_2atmpS2063 = _M0L1iS52 - 1;
        _M0L1iS52 = _M0L6_2atmpS2063;
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
        int32_t _M0L6_2atmpS2065 = _M0L11dst__offsetS56 + _M0L1iS58;
        int32_t _M0L6_2atmpS2067 = _M0L11src__offsetS57 + _M0L1iS58;
        struct _M0TUsiE* _M0L6_2atmpS2066;
        struct _M0TUsiE* _M0L6_2aoldS5560;
        int32_t _M0L6_2atmpS2068;
        if (
          _M0L6_2atmpS2067 < 0
          || _M0L6_2atmpS2067 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2066 = (struct _M0TUsiE*)_M0L3srcS55[_M0L6_2atmpS2067];
        if (
          _M0L6_2atmpS2065 < 0
          || _M0L6_2atmpS2065 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5560 = (struct _M0TUsiE*)_M0L3dstS54[_M0L6_2atmpS2065];
        if (_M0L6_2atmpS2066) {
          moonbit_incref_cycle_free(_M0L6_2atmpS2066);
        }
        if (_M0L6_2aoldS5560) {
          moonbit_decref_cycle_free(_M0L6_2aoldS5560);
        }
        _M0L3dstS54[_M0L6_2atmpS2065] = _M0L6_2atmpS2066;
        _M0L6_2atmpS2068 = _M0L1iS58 + 1;
        _M0L1iS58 = _M0L6_2atmpS2068;
        continue;
      } else {
        moonbit_decref_cycle_free(_M0L3srcS55);
        moonbit_decref_cycle_free(_M0L3dstS54);
      }
      break;
    }
  } else {
    int32_t _M0L6_2atmpS2073 = _M0L3lenS59 - 1;
    int32_t _M0L1iS61 = _M0L6_2atmpS2073;
    while (1) {
      if (_M0L1iS61 >= 0) {
        int32_t _M0L6_2atmpS2069 = _M0L11dst__offsetS56 + _M0L1iS61;
        int32_t _M0L6_2atmpS2071 = _M0L11src__offsetS57 + _M0L1iS61;
        struct _M0TUsiE* _M0L6_2atmpS2070;
        struct _M0TUsiE* _M0L6_2aoldS5561;
        int32_t _M0L6_2atmpS2072;
        if (
          _M0L6_2atmpS2071 < 0
          || _M0L6_2atmpS2071 >= Moonbit_array_length(_M0L3srcS55)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2atmpS2070 = (struct _M0TUsiE*)_M0L3srcS55[_M0L6_2atmpS2071];
        if (
          _M0L6_2atmpS2069 < 0
          || _M0L6_2atmpS2069 >= Moonbit_array_length(_M0L3dstS54)
        ) {
          #line 54 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\fixedarray_block.mbt"
          moonbit_panic();
        }
        _M0L6_2aoldS5561 = (struct _M0TUsiE*)_M0L3dstS54[_M0L6_2atmpS2069];
        if (_M0L6_2atmpS2070) {
          moonbit_incref_cycle_free(_M0L6_2atmpS2070);
        }
        if (_M0L6_2aoldS5561) {
          moonbit_decref_cycle_free(_M0L6_2aoldS5561);
        }
        _M0L3dstS54[_M0L6_2atmpS2069] = _M0L6_2atmpS2070;
        _M0L6_2atmpS2072 = _M0L1iS61 - 1;
        _M0L1iS61 = _M0L6_2atmpS2072;
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

int32_t _M0MPB18UninitializedArray6lengthGfE(float* _M0L4selfS16) {
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
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_33.data);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0MPB6Logger13write__objectGsE(_M0L10_2ax__6388S13, _M0L15_2a_2aarg__6389S12);
  #line 39 "C:\\Users\\31379\\.moon\\lib\\core\\builtin\\failure.mbt"
  _M0L10_2ax__6388S13.$0->$method_0(_M0L10_2ax__6388S13.$1, (moonbit_string_t)moonbit_string_literal_34.data);
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

float* _M0FPC15abort5abortGRPB18UninitializedArrayGfEE(
  moonbit_string_t _M0L3msgS6
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS6);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

int32_t* _M0FPC15abort5abortGRPB18UninitializedArrayGiEE(
  moonbit_string_t _M0L3msgS7
) {
  #line 47 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  #line 49 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_println(_M0L3msgS7);
  #line 50 "C:\\Users\\31379\\.moon\\lib\\core\\abort\\abort.mbt"
  moonbit_panic();
}

moonbit_string_t _M0FP15Error10to__string(void* _M0L4_2aeS1996) {
  switch (Moonbit_object_tag(_M0L4_2aeS1996)) {
    case 2: {
      return (moonbit_string_t)moonbit_string_literal_35.data;
      break;
    }
    
    case 0: {
      return _M0IP016_24default__implPB4Show10to__stringGRPB7FailureE(_M0L4_2aeS1996);
      break;
    }
    
    case 3: {
      return (moonbit_string_t)moonbit_string_literal_36.data;
      break;
    }
    
    case 4: {
      return (moonbit_string_t)moonbit_string_literal_37.data;
      break;
    }
    default: {
      return (moonbit_string_t)moonbit_string_literal_38.data;
      break;
    }
  }
}

int32_t _M0IP016_24default__implPB6Logger61write_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS2024,
  struct _M0TPB4Show _M0L8_2aparamS2023
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2022 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2024;
  _M0IP016_24default__implPB6Logger5writeGRPB13StringBuilderE(_M0L7_2aselfS2022, _M0L8_2aparamS2023);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger84write__string__interpolation_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS2021,
  struct _M0TPB4Show _M0L8_2aparamS2020
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2019 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2021;
  _M0IP016_24default__implPB6Logger28write__string__interpolationGRPB13StringBuilderE(_M0L7_2aselfS2019, _M0L8_2aparamS2020);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__char_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS2018,
  int32_t _M0L8_2aparamS2017
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2016 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2018;
  _M0IPB13StringBuilderPB6Logger11write__char(_M0L7_2aselfS2016, _M0L8_2aparamS2017);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger67write__view_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS2015,
  struct _M0TPC16string10StringView _M0L8_2aparamS2014
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2013 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2015;
  _M0IPB13StringBuilderPB6Logger11write__view(_M0L7_2aselfS2013, _M0L8_2aparamS2014);
  return 0;
}

int32_t _M0IP016_24default__implPB6Logger72write__substring_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLoggerGRPB13StringBuilderE(
  void* _M0L11_2aobj__ptrS2012,
  moonbit_string_t _M0L8_2aparamS2009,
  int32_t _M0L8_2aparamS2010,
  int32_t _M0L8_2aparamS2011
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2008 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2012;
  _M0IP016_24default__implPB6Logger16write__substringGRPB13StringBuilderE(_M0L7_2aselfS2008, _M0L8_2aparamS2009, _M0L8_2aparamS2010, _M0L8_2aparamS2011);
  return 0;
}

int32_t _M0IPB13StringBuilderPB6Logger69write__string_2edyncall__as___40moonbitlang_2fcore_2fbuiltin_2eLogger(
  void* _M0L11_2aobj__ptrS2007,
  moonbit_string_t _M0L8_2aparamS2006
) {
  struct _M0TPB13StringBuilder* _M0L7_2aselfS2005 =
    (struct _M0TPB13StringBuilder*)_M0L11_2aobj__ptrS2007;
  _M0IPB13StringBuilderPB6Logger13write__string(_M0L7_2aselfS2005, _M0L8_2aparamS2006);
  return 0;
}

void moonbit_init() {
  moonbit_layout_table = moonbit_layout_table_data;
  int64_t _tmp_6033 = 9218868437227405311ll;
  int64_t _tmp_6034;
  int64_t _tmp_6035;
  int64_t _tmp_6036;
  int64_t _tmp_6037;
  _M0FPB18double__max__value = *(double*)&_tmp_6033;
  _tmp_6034 = -4503599627370497ll;
  _M0FPB18double__min__value = *(double*)&_tmp_6034;
  _tmp_6035 = 9221120237041090561ll;
  _M0FPC16double14not__a__number = *(double*)&_tmp_6035;
  _tmp_6036 = -4503599627370496ll;
  _M0FPC16double13neg__infinity = *(double*)&_tmp_6036;
  _tmp_6037 = 4503599627370496ll;
  _M0FPC16double13min__positive = *(double*)&_tmp_6037;
}

int main(int argc, char** argv) {
  struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error** _M0L6_2atmpS2028;
  struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE* _M0L12async__testsS1989;
  struct _M0TPB5ArrayGUsiEE* _M0L7_2abindS1990;
  int32_t _M0L7_2abindS1991;
  struct _M0TUsiE** _M0L7_2abindS1992;
  int32_t _M0L6_2acntS5738;
  int32_t _M0L2__S1993;
  moonbit_runtime_init(argc, argv);
  moonbit_init();
  _M0L6_2atmpS2028
  = (struct _M0TWWuEuWRPC15error5ErrorEuEOuQRPC15error5Error**)moonbit_empty_ref_array;
  _M0L12async__testsS1989
  = (struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE*)moonbit_malloc(sizeof(struct _M0TPB5ArrayGVWEuQRPC15error5ErrorE));
  Moonbit_object_header(_M0L12async__testsS1989)->meta
  = Moonbit_make_regular_object_header(MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED, 102, 0);
  _M0L12async__testsS1989->$0 = _M0L6_2atmpS2028;
  _M0L12async__testsS1989->$1 = 0;
  #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0L7_2abindS1990
  = _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test52moonbit__test__driver__internal__native__parse__args();
  _M0L7_2abindS1991 = _M0L7_2abindS1990->$1;
  _M0L7_2abindS1992 = _M0L7_2abindS1990->$0;
  _M0L6_2acntS5738
  = Moonbit_rc_count(Moonbit_object_header(_M0L7_2abindS1990));
  if (_M0L6_2acntS5738 > 1) {
    int32_t _M0L11_2anew__cntS5739 = _M0L6_2acntS5738 - 1;
    Moonbit_set_rc_count(Moonbit_object_header(_M0L7_2abindS1990), _M0L11_2anew__cntS5739);
    moonbit_incref_cycle_free(_M0L7_2abindS1992);
  } else if (_M0L6_2acntS5738 == 1) {
    #line 447 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
    moonbit_free(_M0L7_2abindS1990);
  }
  _M0L2__S1993 = 0;
  while (1) {
    if (_M0L2__S1993 < _M0L7_2abindS1991) {
      struct _M0TUsiE* _M0L3argS1994 =
        (struct _M0TUsiE*)_M0L7_2abindS1992[_M0L2__S1993];
      moonbit_string_t _M0L6_2atmpS2025 = _M0L3argS1994->$0;
      int32_t _M0L6_2atmpS2026 = _M0L3argS1994->$1;
      int32_t _M0L6_2atmpS2027;
      moonbit_incref_cycle_free(_M0L6_2atmpS2025);
      #line 448 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
      _M0FP46RiantR8snn__mbt8examples27timed__stim__blackbox__test44moonbit__test__driver__internal__do__execute(_M0L12async__testsS1989, _M0L6_2atmpS2025, _M0L6_2atmpS2026);
      moonbit_decref_cycle_free(_M0L6_2atmpS2025);
      _M0L6_2atmpS2027 = _M0L2__S1993 + 1;
      _M0L2__S1993 = _M0L6_2atmpS2027;
      continue;
    } else {
      moonbit_decref_cycle_free(_M0L7_2abindS1992);
    }
    break;
  }
  #line 450 "D:\\src\\MiniMax\\Projects\\MoonBit\\moonbit-snn\\mbt\\examples\\timed_stim\\__generated_driver_for_blackbox_test.mbt"
  _M0IP016_24default__implP46RiantR8snn__mbt8examples27timed__stim__blackbox__test28MoonBit__Async__Test__Driver17run__async__testsGRP46RiantR8snn__mbt8examples27timed__stim__blackbox__test34MoonBit__Async__Test__Driver__ImplE(_M0L12async__testsS1989);
  moonbit_decref_cycle_free(_M0L12async__testsS1989);
  moonbit_flush_cycles();
  return 0;
}